#include "skygfx.h"
#include "chars.h"
#include "transparency.h"
#include <string>
#include <map>

// ============================================================
// BRDF database for character parts
// ============================================================
PartSSSParams g_partSSSParams[NUM_PARTTYPES] = {
	// roughness, reflectance, subsurface, specIntensity, blurStrength, blurRadius
	{ 0.0f,  0.0f,  0.0f,  0.0f, 0.0f,  0.0f  },  // NONE
	{ 0.55f, 0.04f, 0.80f, 0.6f, 0.35f, 4.0f  },  // SKIN   - soft scattering, warm
	{ 0.40f, 0.05f, 0.30f, 0.8f, 0.15f, 2.0f  },  // HAIR   - anisotropic-like, low SSS
	{ 0.80f, 0.02f, 0.10f, 0.3f, 0.05f, 1.0f  },  // CLOTH  - rough, minimal SSS
	{ 0.70f, 0.03f, 0.05f, 0.4f, 0.03f, 0.5f  },  // SHOES  - rough leather/rubber
	{ 0.10f, 0.08f, 0.00f, 1.0f, 0.00f, 0.0f  },  // EYES   - mirror-like, no SSS
	{ 0.30f, 0.50f, 0.00f, 0.8f, 0.00f, 0.0f  },  // ACCESS - metallic, no SSS
};

// ============================================================
// S5 diagnostic receipt — head-part texture probe (bandana family)
// ============================================================
// Called from the ped skin-pipe draw path (main.cpp myDefaultCallback,
// isPedPipe branch) once per run for the first ped atomic that carries a
// head-part texture. Names the texture, its TexAlpha class, and the frame
// the atomic is attached to — the three facts needed to attribute the
// "bandana green mask over the face" symptom (S5) to either (a) the
// texture's alpha class (a bandana mask should classify TAC_CUTOUT) or
// (b) the draw state at the ped pipe (alpha test OFF + blending OFF paints
// the chroma-green RGB stored in the transparent texels as SOLID fill —
// the same mechanism as the ped dual-pass leg 2, which is why the leg-2
// blend fix at main.cpp:2188-2204 and this receipt name the same family).
//
// NOTE: there is NO bandana-specific draw path and NO component
// attach/orientation code anywhere in this mod — the bandana is a game-side
// ped component rendered through the skin pipe like every other ped atomic.
// The historical "bandana draws backwards" note therefore cannot be an
// attach flip in this codebase; this receipt exists to prove which texture
// and class actually draw on the head part.
static bool icontains_ascii(const char *hay, const char *needle)
{
	if(!hay || !needle) return false;
	for(; *hay; hay++){
		const char *h = hay, *n = needle;
		while(*n){
			char hc = *h, nc = *n;
			if(hc >= 'A' && hc <= 'Z') hc += 32;
			if(nc >= 'A' && nc <= 'Z') nc += 32;
			if(hc != nc) break;
			h++; n++;
		}
		if(!*n) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Head-part classification (shared — the S5 keyword set lives HERE, one copy).
// ---------------------------------------------------------------------------
// chars_TextureIsHeadPart — texture-name predicate for the head/face/hair
// accessory family. chars_IsHeadPartAtomic — the same test walked over an
// atomic's whole material list (a head part carries several materials: face
// skin + hair + bandana/glasses, and only ONE of them may be name-matched).
// Both are pure reads: no state, no receipts, no render-state writes, so they
// are safe to call per atomic per frame from either dual-path arm.
//
// The keyword set is deliberately the one the one-shot [CharsHead] receipt
// already used — the receipt corpus must not change (grep-stable directive).
// 2026-11-09 head-blob lane EXTENDED (diagnostic trigger only — nothing in
// the draw path reads this predicate): two W0/W0.1 sessions logged nothing
// but identical 'neckcross'-burned one-shots because neither the head
// texture family nor the necklace are covered by the original five
// keywords. The real CJ head component textures, per the extracted asset
// maps (SKYGFXPLUS_DOCS player.img contents): 'head'/'head8bit' (head.txd,
// also probed by the texalpha watch receipts), 'face'/'face_afro'/…
// (player_face.txd and the hairstyle TXDs), and the neck accessories
// 'neck'/'neckcross'. These additions make chars_IsHeadPartAtomic actually
// name-match the head atomic, which wakes the [PedHead]/[PedHeadPath]/
// [PedHeadBind] receipt set on it.
// ---------------------------------------------------------------------------
bool chars_TextureIsHeadPart(const char *tn)
{
	return icontains_ascii(tn, "bandana") ||
		icontains_ascii(tn, "hair") ||
		icontains_ascii(tn, "afro") ||
		icontains_ascii(tn, "cap") ||
		icontains_ascii(tn, "hat") ||
		icontains_ascii(tn, "head") ||
		icontains_ascii(tn, "face") ||
		icontains_ascii(tn, "neck");
}

bool chars_IsHeadPartAtomic(const RpAtomic *atomic)
{
	if(!atomic || !atomic->geometry || !atomic->geometry->matList.materials)
		return false;
	RpGeometry *geom = atomic->geometry;
	for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
		RpMaterial *m = geom->matList.materials[i];
		if(m && m->texture && chars_TextureIsHeadPart(m->texture->name))
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Head-part receipt router — ONE-SHOT fires ONLY on a NAME-MATCHED head part.
// ---------------------------------------------------------------------------
// 2026-11-09 head-blob lane rework (trigger semantics only; all existing
// receipt lines byte-identical). Log evidence that forced the change: both
// W0 and W0.1 burned the single shot on the necklace accessory 'neckcross'
// (frame ' Pelvis', matched by the old "any TAC_CUTOUT/FADED texture counts"
// trigger) and the actual head component was NEVER characterized — two
// sessions, two identical neckcross receipts, zero head-part data. Now:
//   - trigger: a head-part NAME MATCH only (keyword set extended above),
//   - a bounded 4096-call scan replaces the unconditional early-out, so the
//     head atomic cannot be permanently starved by an earlier accessory,
//   - [CharsHead] keeps its exact text (grep-stable corpus),
//   - [PedHeadPath] moves to its own one-shot, fired on the SAME atomic,
//   - NEW [PedHeadBind] (additive token, documented in the lane report):
//     names the matched head texture's raster and the LIVE stage-0 raster
//     (L1 rw cache — the slot SkinStage0Resync/pedStage0Resync repairs, and
//     the level the skin pipe's per-material bind must agree with). A
//     foreign/NULL stage-0 at the head draw is the wrong-bind signature;
//     a clean bind with the foreign blob persists means the green/black
//     mass is foreign geometry at the ped head, not a ped-pipe defect.
// ---------------------------------------------------------------------------

// stage-0 raster read with the skin.cpp:900-904 sentinel convention
// (a failed RwRenderStateGet leaves 0xFFFFFFFF in the out-param).
static RwRaster *chars_getStage0Raster(void)
{
	void *ras = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &ras);
	if(ras == (void*)0xFFFFFFFF)
		return nil;
	return (RwRaster*)ras;
}

void chars_logHeadPartReceipt(const RpAtomic *atomic)
{
	static bool logged = false;
	static int  headScanBudget = 4096;
	if(logged) return;
	if(!atomic) return;
	if(headScanBudget <= 0){
		logged = true;	// never woke on a head part — stop paying the scan
		dbglog("[PedHeadScan] exhausted (4096 calls, no name-matched head "
		       "part) — head-part receipts stood down");
		return;
	}
	headScanBudget--;
	RpGeometry *geom = atomic->geometry;
	if(!geom || !geom->matList.materials) return;

	// Attach frame: the atomic's own frame plus the clump root (the ped
	// hierarchy root), so the receipt names where the head part hangs.
	RwFrame *frame = RpAtomicGetFrame((RpAtomic*)atomic);
	const char *frameName = frame ? GetFrameNodeName(frame) : NULL;
	RpClump *clump = RpAtomicGetClump((RpAtomic*)atomic);
	RwFrame *rootFrame = clump ? RpClumpGetFrame(clump) : NULL;
	const char *rootName = rootFrame ? GetFrameNodeName(rootFrame) : NULL;

	// Local extern (texalpha.cpp): TAC_CUTOUT=2, TAC_FADED=3 — same pattern
	// as main.cpp atomicHasAlpha; enum TexAlphaClass lives in texalpha.h.
	extern int TexAlpha_Classify(const RwTexture *tex);

	char texList[256];
	size_t pos = 0;
	texList[0] = 0;
	const char *headTexName = NULL;
	RwTexture *headTex = NULL;
	for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
		RpMaterial *m = geom->matList.materials[i];
		if(!m || !m->texture) continue;
		const char *tn = m->texture->name;
		bool isHeadPart = chars_TextureIsHeadPart(tn);
		if(isHeadPart && !headTex){
			// FIRST name-matched head-part texture: the trigger + the
			// [PedHeadBind] subject.
			headTex = m->texture;
			headTexName = tn ? tn : "(null)";
		}
		int tac = TexAlpha_Classify(m->texture);
		// Report head-part textures by name, plus any alpha-carrying mask
		// (CUTOUT/FADED) — head-part planes are the chroma-green carriers
		// even when the texture name does not match the keywords.
		if(isHeadPart || tac == 2 || tac == 3){
			int cls = isHeadPart ? 1 : 0;	// 1=name-matched head part
			int w = _snprintf_s(texList + pos, sizeof(texList) - pos,
				_TRUNCATE, "%s[%s tac=%d]%s",
				pos ? " " : "", tn ? tn : "(null)", tac,
				cls ? "*" : "");
			if(w < 0) break;	// list full — first entries are the important ones
			pos += (size_t)w;
		}
	}
	// TRIGGER (2026-11-09): only a NAME-MATCHED head part wakes the receipt
	// now — an accessory cutout (neckcross) is logged inside the list of the
	// atomic that actually matches, never as the trigger itself.
	if(!headTex) return;
	logged = true;
	dbglog("[CharsHead] receipt: atomic=%p frame='%s' root='%s' textures:%s "
		"(*=name-matched head part; tac: 1=OPAQUE 2=CUTOUT 3=FADED 0=UNKNOWN)",
		(const void*)atomic,
		frameName ? frameName : "(null)",
		rootName ? rootName : "(null)",
		texList[0] ? texList : " (none)");

	// NEW [PedHeadBind] — the wrong-bind discriminator (fires BEFORE
	// [PedHeadPath]'s read of atomic->renderCallBack, on the same atomic,
	// before any later shader-hygiene unbind can clear the device).
	{
		RwRaster *stage0 = chars_getStage0Raster();
		dbglog("[PedHeadBind] atomic=%p tex='%s' raster=%p stage0raster=%p "
		       "matlist=%d",
			(const void*)atomic,
			headTexName ? headTexName : "(null)",
			(void*)(headTex ? headTex->raster : nil),
			(void*)stage0,
			(int)geom->matList.numMaterials);
	}

	// ---------------------------------------------------------------------------
	// Additive discriminator — WHICH pipe actually draws the head part.
	// ---------------------------------------------------------------------------
	// This is the evidence line for the "head atomic not going through the ped
	// entry contract" hypothesis. myDefaultCallback (hooked 0x7491C0) only
	// runs SkinEntryContract / pedOpaqueEnforce / the dual pass / the cull
	// re-assert / SkinAmbientOverride when `atomic->pipeline == skinPipe`
	// (main.cpp isPedPipe). For a head part:
	//   isPed=1  -> the ped contract DOES cover it; if it still renders wrong
	//               the defect is inside the ped span (classifier / dual legs
	//               / entry-exit contract), not a missing contract.
	//   isPed=0  -> the head is drawn by another branch: pipe==NULL falls into
	//               the default-pipe contract (NO ped opaque enforce, NO cull
	//               force, NO dual pass, NO pAmbient override) which is
	//               exactly the "dark head + solid chroma-green fringe"
	//               signature; a foreign pipeline is worse still.
	// renderCallBack is the +0x48 member (gta.h SA layout contract) — anything
	// other than AtomicDefaultRenderCallBack means myDefaultCallback never
	// sees this atomic at all, so no ped contract of either dual-path arm can
	// ever reach it.
	// One-shot, fires on the same atomic as [CharsHead]; additive tag so the
	// existing receipt corpus stays byte-identical.
	dbglog("[PedHeadPath] atomic=%p frame='%s' pipe=%p skinPipe=%p isPed=%d "
		"renderCb=%p defaultCb=%p",
		(const void*)atomic,
		frameName ? frameName : "(null)",
		(void*)atomic->pipeline, (void*)skinPipe,
		(int)(atomic->pipeline == skinPipe),
		(void*)atomic->renderCallBack, (void*)AtomicDefaultRenderCallBack);
}

// ============================================================
// Classification buffer for SSS blur - DELETED (decision ③A)
// ============================================================
// chars_classifyClump had ZERO callers anywhere in src/, so
// EnsureClassifyBuffer / g_charsClassifyTex / g_charsClassifySurf /
// g_charsClassifyRaster / s_charsClassifyFresh never had a producer. The
// whole classify-mask lane was deleted rather than wired: chars_drawSSSBlur
// now early-outs unconditionally before any raster work (see its gate
// below), and the chars.h extern of g_charsClassifyTex has no remaining
// definition or reader in any TU.

// ============================================================
// SSS blur post-process - GATE KEPT, PASS BODY DELETED (decision ③A)
// ============================================================
// chars_drawSSSBlur now runs only its config/receipt gate (postfx.cpp calls
// it from two sites; chars.h declares it) and then early-outs: the
// classify-mask lane it guarded was deleted, not wired. EnsureSSSRasters and
// RestoreSSSStage0AndSampler below are kept as the documented 3L helpers but
// currently have no callers (their only call sites lived in the deleted pass
// body); chars_shutdown / chars_OnDeviceReset keep their cleanup.
extern IDirect3DTexture9 *g_ssaoDepthTex;
extern void *overrideIm2dPixelShader;
extern RwIm2DVertex *colorfilterVerts;
extern RwImVertexIndex *colorfilterIndices;
// Trusted screen-size cache — defined in postfx.cpp, captured at the on-screen
// postfx entry (ColourFilter_switch -> CaptureScreenSize). A live
// RwCameraGetRaster read can observe a transient/swapped raster (the env-map
// reflection pass briefly swaps Scene.camera onto the reflection targets) and
// mis-size the SSS helper rasters; this cache is the same source the rest of
// the postfx chain sizes from.
bool GetScreenSize(int *w, int *h);

static RwRaster *sssBlurRasterA = nil;
static RwRaster *sssBlurRasterB = nil;
static int sssLastW = 0, sssLastH = 0;

static void EnsureSSSRasters(int w, int h)
{
	if(sssLastW == w && sssLastH == h && sssBlurRasterA)
		return;
	if(sssBlurRasterA) RwRasterDestroy(sssBlurRasterA);
	if(sssBlurRasterB) RwRasterDestroy(sssBlurRasterB);
	sssBlurRasterA = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE);
	sssBlurRasterB = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE);
	sssLastW = w;
	sssLastH = h;
}

// SSS exit contract, shared by the normal path and the SEH fault path.
// rwRENDERSTATETEXTURERASTER is NOT one of ImmediateModeRenderStatesReStore's
// 10 rw states, so the pass's sssBlurRasterA/B stage-0 binds would be inherited
// by the next consumer as its texture. Park on a guaranteed-different raster
// first (so the L1 cache cannot no-op), set the captured entry raster back,
// then raw-pin its resolved IDirect3DTexture9 (L3). Sampler stage 1 is
// raw-device state too: the CLAMP forced for the depth bind must go back to
// the captured entry fields.
static void RestoreSSSStage0AndSampler(RwRaster *saved0,
	DWORD s1AddrU, DWORD s1AddrV, DWORD s1Min, DWORD s1Mag, bool s1Valid)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	RwRaster *park = sssBlurRasterA;
	if(park == saved0) park = sssBlurRasterB;
	if(park == saved0) park = NULL;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)saved0);

	// L3 raw pin — proven extraction (main.cpp pedStage0Resync): ext =
	// raster + *(DWORD*)0xB4E9E0, ext+0 = IDirect3DTexture9*. An empty entry
	// raster must still clear the device bind, otherwise sssBlurRasterB's
	// texture would survive the rw-level NULL.
	IDirect3DTexture9 *tex = NULL;
	if(saved0){
		DWORD extOff = *(DWORD*)0xB4E9E0;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)saved0 + extOff);
			if(cand && (uintptr_t)cand >= 0x10000u && (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
	}
	if(tex || !saved0)
		dev->SetTexture(0, tex);

	if(s1Valid){
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, s1AddrU);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, s1AddrV);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, s1Min);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, s1Mag);
	} else {
		// Capture failed: fall back to the old default rather than leaving the
		// depth bind's CLAMP behind.
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	}
}

void chars_drawSSSBlur(void)
{
	if(dbglog_throttle( "sss_blur"))
		dbglog("[PostFX] chars_drawSSSBlur ENTER sssPostProcessEnable=%d SSS_Blur=%p pRasterFrontBuffer=%p skinEnhance=%d",
			config->sssPostProcessEnable, SSS_Blur, CPostEffects::pRasterFrontBuffer,
			config->skinEnhanceEnable);
	// Master switch: menu "Screen-space SSS". This key was read/written by
	// the config code but NEVER consulted by any draw path — the toggle was
	// dead. Now it gates the whole pass (sssPostProcessEnable remains the
	// per-effect switch below, so every SSS knob stays individually
	// toggleable).
	if(!config->sssEnable){
		if(dbglog_throttle( "sss_bail0"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: sssEnable=0 (master switch)");
		return;
	}
	if(!config->sssPostProcessEnable){
		if(dbglog_throttle( "sss_bail1"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: sssPostProcessEnable=0");
		return;
	}
	if(!SSS_Blur){
		if(dbglog_throttle( "sss_bail2"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: SSS_Blur=NULL");
		return;
	}
	if(!CPostEffects::pRasterFrontBuffer){
		if(dbglog_throttle( "sss_bail3"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: pRasterFrontBuffer=NULL");
		return;
	}
	// SSS needs valid scene depth for the character mask. With SSAO disabled the
	// depth texture is absent or stale (never rendered) — sampling it smears the
	// blur across the whole frame and corrupts the tonemapped image. Skip until
	// a standalone depth fallback exists.
	if(!config->ssaoEnable || !g_ssaoDepthTex){
		if(dbglog_throttle( "sss_bail4"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: no valid depth (ssaoEnable=%d depthTex=%p)",
				config->ssaoEnable, g_ssaoDepthTex);
		return;
	}

	// EARLY-OUT - decision ③A (DELETE, not wire). The classify-mask
	// lane is GONE: chars_classifyClump had ZERO callers anywhere in src/, so
	// s_charsClassifyFresh / g_charsClassifyTex never had a producer; the
	// producer pair (EnsureClassifyBuffer + both raw globals + the latch) was
	// deleted with it rather than wired up. Passes 0/1 are a full-screen copy
	// plus a two-tap blur pair whose only consumer was the gated Pass-2
	// composite, so they would render two camera-sized rasters for nothing.
	// Bail BEFORE any raster creation/copy/blur: no camera raster has been
	// touched yet (the first RwCameraSetRaster lived inside the pass body),
	// so no camera restore is owed here; every bail above stays verbatim,
	// receipts included. Everything past this point (screen-size capture,
	// EnsureSSSRasters, the copy/blur passes, the SEH-fenced pass body and
	// its restore tail) was unreachable and has been deleted. The symbol
	// survives for its two postfx.cpp call sites (chars.h declares it); the
	// early-out receipt text below is kept byte-identical (grep-stable).
	if(dbglog_throttle("sss_early"))
		dbglog("[PostFX] chars_drawSSSBlur early-out: %s — Passes 0/1 (copy+blur) skipped, Pass 2 could not composite",
			"no fresh classify mask (s_charsClassifyFresh has no producer)");
	return;
}

// ============================================================
// Init / Shutdown
// ============================================================
void chars_shutdown(void)
{
	if(sssBlurRasterA){ RwRasterDestroy(sssBlurRasterA); sssBlurRasterA = nil; }
	if(sssBlurRasterB){ RwRasterDestroy(sssBlurRasterB); sssBlurRasterB = nil; }
	sssLastW = sssLastH = 0;
}

// Device-reset release (called from postfx.cpp OnDeviceReset).
//
// Decision ③A deleted the classify lane and with it the raw
// D3DPOOL_DEFAULT render-target texture (old g_charsClassifyTex, lazily
// created by the deleted EnsureClassifyBuffer) whose liveness across
// IDirect3DDevice9::Reset produced D3DERR_INVALIDCALL -> RW's unchecked
// _RwD3D9RenderSurface Release -> NULL read at 0x7F9A3C (crash log
// 2026-10-17 07:04:00.350, 1.3s after the frame-1 Reset). That texture can
// no longer be created, so the release branch went with it; the SSS blur
// rasters below are RW CAMERATEXTURE rasters covered by RW's restore list,
// and destroying them here keeps the module device-reset-clean (they
// recreate lazily on the next pass).
void chars_OnDeviceReset(void)
{
	// SSS blur rasters are RW CAMERATEXTURE rasters (DEFAULT-pool RTs under the
	// hood); RW's restore list covers them, but destroying here keeps the
	// module fully device-reset-clean and they recreate lazily next pass.
	if(sssBlurRasterA){ RwRasterDestroy(sssBlurRasterA); sssBlurRasterA = nil; }
	if(sssBlurRasterB){ RwRasterDestroy(sssBlurRasterB); sssBlurRasterB = nil; }
	sssLastW = sssLastH = 0;
}
