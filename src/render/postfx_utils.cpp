// postfx_utils.cpp - shared postfx helpers (m0060 split from postfx.cpp):
// colorfilterVerts/Indices, tracked rasters, screen-size cache, menu guard,
// fullscreen quad, raw-geom save/restore.
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "profiler.h"
RwIm2DVertex *colorfilterVerts = (RwIm2DVertex*)0xC400D8;
RwImVertexIndex *colorfilterIndices = (RwImVertexIndex*)0x8D5174;

// Track initialized CAMERATEXTURE rasters for RasterEnsureSurfaceReady.
// Max 8 tracked rasters — plenty for all our passes.
#define MAX_TRACKED_RASTERS 8
static RwRaster *s_trackedRasters[MAX_TRACKED_RASTERS] = {NULL};
static int s_trackedRasterCount = 0;

void TrackRaster(RwRaster *ras) {
	if(!ras || s_trackedRasterCount >= MAX_TRACKED_RASTERS) return;
	for(int i = 0; i < s_trackedRasterCount; i++)
		if(s_trackedRasters[i] == ras) return;
	s_trackedRasters[s_trackedRasterCount++] = ras;
}

void UntrackRaster(RwRaster *ras) {
	if(!ras) return;
	for(int i = 0; i < s_trackedRasterCount; i++) {
		if(s_trackedRasters[i] == ras) {
			s_trackedRasters[i] = s_trackedRasters[--s_trackedRasterCount];
			s_trackedRasters[s_trackedRasterCount] = NULL;
			return;
		}
	}
}

// ============================================================
// RasterEnsureSurfaceReady — check if a CAMERATEXTURE raster
// has a valid D3D9 surface before binding as RT or texture.
// Returns true if the raster is safe to use.
// CAMERATEXTURE rasters created via RwRasterCreate do NOT have
// a D3D9 surface until RwCameraBeginUpdate runs on them.
// Binding one without surface = crash at RwD3D9SetRenderTarget.
// Non-CAMERATEXTURE rasters are always safe.
// ============================================================
bool RasterEnsureSurfaceReady(RwRaster *ras, const char *tag) {
	if(!ras) {
		dbglog("[RasterInit] %s: raster is NULL, skipping", tag);
		return false;
	}
	// Only CAMERATEXTURE rasters need the surface check
	if((ras->cType & rwRASTERTYPEMASK) != rwRASTERTYPECAMERATEXTURE)
		return true;
	// Check if tracked (initialized via camera path)
	for(int i = 0; i < s_trackedRasterCount; i++) {
		if(s_trackedRasters[i] == ras)
			return true;
	}
	// Not tracked — surface may not exist
	if(dbglog_throttle("ras_unsafe"))
		dbglog("[RasterInit] %s: CAMERATEXTURE raster %p not initialized yet, skipping", tag, ras);
	return false;
}

/////
///// Authoritative screen-size cache.
/////
///// RwCameraGetRaster(Scene.camera) is NOT a stable screen-size source
///// outside the postfx entry points: other passes (envmap/reflection and
///// friends) temporarily point the main camera at a NON-screen raster, and
///// every geometry-phase reader observes that transient. Measured in
///// skygfx_dbg.log (PBR pipeline + COLORFILTER_MODERN session, m0169):
/////
/////   [PipeChain] classify RT created 2048x1024   <- transient camRas read
/////   [PipeChain] classify RT created 1920x1080   <- same frame, restored
/////   GetIBLTexture: OK 512x256                   <- camRas/4 == 2048x1024/4
/////
///// i.e. the live read reported 2048x1024 while the actual screen is
///// 1920x1080, TWICE per frame for the whole PBR + Modern-filter window.
///// Anything SIZED during that window inherits the wrong dimensions: the
///// pipe-chain classify pack RT was destroyed+recreated twice per frame
///// (every recreate CLEARS the pack PipeChain pass 3 consumes), the IBL
///// capture was allocated 2:1 instead of 16:9 (its per-frame viewport never
///// matched the texture), and any UV remap derived from the read composites
///// at the wrong scale/coords.
/////
///// Contract: capture ONLY from on-screen postfx entry points
///// (ColourFilter_switch / DrawPipeChain — both verified 1920x1080 every
///// frame in the same log); geometry-phase and mid-frame readers use the
///// cache instead of the live read. The cache self-corrects on resolution
///// changes at the next postfx entry (worst case one frame of stale size —
///// the same failure mode as today, but bounded to one frame instead of
///// every frame).
/////
static int g_screenSizeW = 0;
static int g_screenSizeH = 0;

void CaptureScreenSize(RwRaster *camRas)
{
	if(camRas && camRas->width > 0 && camRas->height > 0){
		if((g_screenSizeW != camRas->width || g_screenSizeH != camRas->height) &&
		   dbglog_throttle("pf_scr"))
			dbglog("[PF] screen size cache: %dx%d -> %dx%d (drives RT sizing)",
				g_screenSizeW, g_screenSizeH, camRas->width, camRas->height);
		g_screenSizeW = camRas->width;
		g_screenSizeH = camRas->height;
	}
}

// Returns true when a trusted screen size has been captured at least once.
bool GetScreenSize(int *w, int *h)
{
	if(g_screenSizeW > 0 && g_screenSizeH > 0){
		*w = g_screenSizeW;
		*h = g_screenSizeH;
		return true;
	}
	return false;
}
/////
///// Menu state guard — skip expensive PostFX during menus/loading/pause
/////
bool IsGameInMenuOrPaused() {
	// Shared pass gate: menus/pause/cutscene disable every postpass in this
	// lane (and m_bDisableAllPostEffect). Logged from the pass entries that
	// consult it so a masked frame can be reconstructed from the log.
	bool skip = CMenuManager__m_bMenuActive || CCutsceneMgr__ms_running || CPostEffects::m_bDisableAllPostEffect;
	if(skip && dbglog_throttle("pf_menu_gate"))
		dbglog("[PF] IsGameInMenuOrPaused: menu=%d cutscene=%d disableAll=%d -> postfx SKIPPED",
			(int)CMenuManager__m_bMenuActive, (int)CCutsceneMgr__ms_running,
			(int)CPostEffects::m_bDisableAllPostEffect);
	return skip;
}

/////
///// Local fullscreen quad — SSAO composites and the IV grade draw with this
///// instead of the game's colorfilterVerts. colorfilterVerts carry
///// emissiveColor=0x00000000 (they are only coloured by the game's own
///// colour-filter paths, which COLORFILTER_MODERN never runs), so any
///// fixed-function draw using them modulates texture x 0 = black; they are
///// also a hardcoded 2048x2048 quad whose UVs misalign on smaller camera
///// rasters. White + RT-sized = correct multiply/replace and UV 0..1.
/////
RwIm2DVertex s_ffQuad[4];
RwImVertexIndex s_ffQuadIdx[6] = { 0, 1, 2, 0, 2, 3 };

void
SetupFullscreenQuad(float w, float h)
{
	// Corner order matches colorfilterVerts: TL, BL, BR, TR
	static const float px[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const float py[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float uu[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const float vv[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	for(int i = 0; i < 4; i++){
		RwIm2DVertex *v = &s_ffQuad[i];
		v->x = px[i] * w;
		v->y = py[i] * h;
		v->z = 0.0f;
		v->rhw = 1.0f;
		v->u = uu[i];
		v->v = vv[i];
		RwIm2DVertexSetIntRGBA(v, 255, 255, 255, 255);
	}
}

// ============================================================
// Raw geometry render-state guard.
// ImmediateModeRenderStates{Store,Set,ReStore} only round-trip the 10 RW-
// cached state IDs; every postfx pass ALSO writes raw D3D9 states
// (ALPHATEST/CULL/Z*/blend factors) that bypass that cache. A pass that
// exits without re-asserting them — or faults mid-way — leaks the values
// into the NEXT frame's geometry (evidenced: vegetation drawing over
// buildings after the SSAO multiply left ALPHABLEND/SRCBLEND/DESTBLEND
// wrong). Save/restore the raw states around every pass that writes them.
// Save BEFORE DepthHook_Suspend (it writes raw ZENABLE itself); restore
// AFTER ReStore so these values win on any overlap.
// ============================================================
static const D3DRENDERSTATETYPE s_rawGeomStateIds[9] = {
	D3DRS_ALPHATESTENABLE,
	D3DRS_CULLMODE,
	D3DRS_ZENABLE,
	D3DRS_ZWRITEENABLE,
	D3DRS_ALPHABLENDENABLE,
	D3DRS_SRCBLEND,
	D3DRS_DESTBLEND,
	D3DRS_BLENDOP,      // raw-written by Radiosity_VCS/Blur_VCS — NOT in the
	D3DRS_BLENDFACTOR   // game ReStore's 10-state list, leaks past ReStore
};

bool
SaveRawGeomStates(DWORD *out)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return false;
	for(int i = 0; i < RAW_GEOM_STATE_COUNT; i++)
		dev->GetRenderState(s_rawGeomStateIds[i], &out[i]);
	return true;
}

// ============================================================
// Park-and-set three-layer restore - THE desync repair (this file's
// previous raw-verbatim restore was the GENERATOR, see below).
//
// Model: (1) rw cache RwRenderStateSet/Get, (2) RwD3D9SetRenderState
// driver cache (pending[]+dirty at 0x7FC2D0, flushed to the device
// pre-draw by _rwD3D9RenderStateFlushCache 0x7FC200 ONLY when its
// applied[] shadow differs), (3) raw d3d9device->SetRenderState.
// Layers (1) and (2) both SKIP on a cache-hit, so a raw layer-(3)-only
// write (or a raw-verbatim restore from a snapshot that already
// disagrees with the caches) desyncs the device from both caches
// PERMANENTLY: every later RwRenderStateSet / RwD3D9SetRenderState to
// the drifted value is a double no-op and can never push the device
// back (log proof: recurring rw(vtx=1) dev(vtx=0) and [PED] ZWRITE
// rw=1 dev=0 despite the old entry-repair tail).
//
// Repair idiom = the canonical primitives exported by pipelinecommon.cpp
// (pipeRwParkSet / pipeL2ParkSet / pipeL3Set), whose ONE park order is
// PARK-THEN-TARGET PER LAYER:
//     L1 park -> L1 target, L2 park -> L2 target, L3 raw target.
// Per state: write the OPPOSITE value through L1 then L2 first
// (guaranteed real transition - neither cache can no-op a value it does
// not hold), then the TARGET through L1 then L2, then the target via
// raw L3. End state: all three layers hold the CAPTURED value (never
// hardcoded), and the next Rw*Set of any other value is a real
// transition again.
// ============================================================
static void
RestoreOneRawGeomState(RwRenderState rwState, DWORD rwPark, DWORD rwTarget,
                       D3DRENDERSTATETYPE d3dState, DWORD park, DWORD target)
{
	// (1) L1 rw cache: park, then target. rwState ==
	// rwRENDERSTATENARENDERSTATE marks a state with no rw-cache twin
	// (BLENDOP / BLENDFACTOR / ALPHATESTENABLE) - L1 leg skipped.
	if(rwState != rwRENDERSTATENARENDERSTATE)
		pipeRwParkSet(rwState, (RwUInt32)rwPark, (RwUInt32)rwTarget);
	// (2) L2 RW driver cache: park, then target - a real cache
	// transition by construction (park != target on every call).
	pipeL2ParkSet(d3dState, (RwUInt32)park, (RwUInt32)target);
	// (3) Raw L3 pin: unconditional authoritative device write - the
	// applied[]-gated flush can never repair a device a foreign raw
	// writer moved, so this is what actually re-syncs layer (3).
	pipeL3Set(d3dState, target);
}

void
RestoreRawGeomStates(const DWORD *in)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(g_pfEnable)
		PfNote("rawGeomRestore");
	// Per-state park-and-set, in s_rawGeomStateIds order. rw/D3D enum
	// domains overlap 1:1 where an rw twin exists (RwBlendFunction ==
	// D3DBLEND for 1..11, RwCullMode == D3DCULL for 1..3, bools are
	// 0/1), so the captured device value doubles as the rw value and
	// the same park works on both layers. L2/L3 always get the captured
	// value VERBATIM (snapshot semantics preserved, incl. out-of-domain
	// raw extras like D3DBLEND_BLENDFACTOR(14)).

	// [0] D3DRS_ALPHATESTENABLE - no rw-cache twin (rwRENDERSTATEALPHATEST-
	// FUNCTION carries func+ref, not the enable bit; see pipeEnterAlphaMode)
	// -> L2 park-and-set + raw L3 only.
	RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
		D3DRS_ALPHATESTENABLE, in[0] ? 0u : 1u, in[0]);

	// [1] D3DRS_CULLMODE - twin rwRENDERSTATECULLMODE (rw/D3D cull enums
	// are index-identical 1..3: NONE=1, BACK/CW=2, FRONT/CCW=3 - see the
	// pipeForceCullMode mapping note). Out-of-domain capture: L1 leg
	// skipped, L2/L3 still restored verbatim.
	DWORD cullPark = (in[1] == (DWORD)D3DCULL_CW) ? (DWORD)D3DCULL_NONE : (DWORD)D3DCULL_CW;
	if(in[1] >= (DWORD)D3DCULL_NONE && in[1] <= (DWORD)D3DCULL_CCW)
		RestoreOneRawGeomState(rwRENDERSTATECULLMODE, cullPark, in[1],
			D3DRS_CULLMODE, cullPark, in[1]);
	else
		RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
			D3DRS_CULLMODE, cullPark, in[1]);

	// [2] D3DRS_ZENABLE - twin rwRENDERSTATEZTESTENABLE (the "rw(vtx=1)
	// dev(vtx=0)" class of drift dies here and on [3]/[4]).
	RestoreOneRawGeomState(rwRENDERSTATEZTESTENABLE,
		in[2] ? (DWORD)FALSE : (DWORD)TRUE, in[2] ? (DWORD)TRUE : (DWORD)FALSE,
		D3DRS_ZENABLE, in[2] ? 0u : 1u, in[2]);

	// [3] D3DRS_ZWRITEENABLE - twin rwRENDERSTATEZWRITEENABLE ("[PED]
	// ZWRITE rw=1 dev=0" class of drift).
	RestoreOneRawGeomState(rwRENDERSTATEZWRITEENABLE,
		in[3] ? (DWORD)FALSE : (DWORD)TRUE, in[3] ? (DWORD)TRUE : (DWORD)FALSE,
		D3DRS_ZWRITEENABLE, in[3] ? 0u : 1u, in[3]);

	// [4] D3DRS_ALPHABLENDENABLE - twin rwRENDERSTATEVERTEXALPHAENABLE
	// (the park-and-transition the old blend tail applied - now generic).
	RestoreOneRawGeomState(rwRENDERSTATEVERTEXALPHAENABLE,
		in[4] ? (DWORD)FALSE : (DWORD)TRUE, in[4] ? (DWORD)TRUE : (DWORD)FALSE,
		D3DRS_ALPHABLENDENABLE, in[4] ? 0u : 1u, in[4]);

	// [5] D3DRS_SRCBLEND - twin rwRENDERSTATESRCBLEND. The rw domain
	// test and the mapping are the canonical pipelinecommon.cpp pair
	// (pipeBlendHasRwTwin / pipeBlendToD3D) - no local predicate copy.
	// Raw-only extras (D3DBLEND_BLENDFACTOR=14 from the SSS blend-back)
	// have no rw twin -> L1 leg skipped, value restored verbatim.
	DWORD srcPark = (in[5] == (DWORD)D3DBLEND_ONE) ? (DWORD)D3DBLEND_ZERO : (DWORD)D3DBLEND_ONE;
	if(pipeBlendHasRwTwin((RwUInt32)in[5])){
		// In-domain mapping is the identity (pipeBlendToD3D); the
		// fallback is the pipe-entry default and is unreachable here.
		DWORD srcD3D = (DWORD)pipeBlendToD3D((RwUInt32)in[5], (RwUInt32)D3DBLEND_SRCALPHA);
		RestoreOneRawGeomState(rwRENDERSTATESRCBLEND, srcPark, srcD3D,
			D3DRS_SRCBLEND, srcPark, srcD3D);
	}else{
		RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
			D3DRS_SRCBLEND, srcPark, in[5]);
	}

	// [6] D3DRS_DESTBLEND - twin rwRENDERSTATEDESTBLEND (same canonical
	// domain test + mapping).
	DWORD dstPark = (in[6] == (DWORD)D3DBLEND_ONE) ? (DWORD)D3DBLEND_ZERO : (DWORD)D3DBLEND_ONE;
	if(pipeBlendHasRwTwin((RwUInt32)in[6])){
		DWORD dstD3D = (DWORD)pipeBlendToD3D((RwUInt32)in[6], (RwUInt32)D3DBLEND_INVSRCALPHA);
		RestoreOneRawGeomState(rwRENDERSTATEDESTBLEND, dstPark, dstD3D,
			D3DRS_DESTBLEND, dstPark, dstD3D);
	}else{
		RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
			D3DRS_DESTBLEND, dstPark, in[6]);
	}

	// [7] D3DRS_BLENDOP - NO rw-cache twin exists (Radiosity_VCS/Blur_VCS
	// leave REVSUBTRACT behind; unfixable rw-side) -> raw-set via L2
	// park-and-set + L3 only.
	RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
		D3DRS_BLENDOP,
		(in[7] == (DWORD)D3DBLENDOP_ADD) ? (DWORD)D3DBLENDOP_REVSUBTRACT : (DWORD)D3DBLENDOP_ADD,
		in[7]);

	// [8] D3DRS_BLENDFACTOR - NO rw-cache twin (D3DCOLOR, raw-only) ->
	// L2 park-and-set + L3 only; any different value parks the cache.
	RestoreOneRawGeomState(rwRENDERSTATENARENDERSTATE, 0, 0,
		D3DRS_BLENDFACTOR, ~in[8], in[8]);
}
