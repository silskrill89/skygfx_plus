// water.cpp — CWaterLevel water/wake direct-flush hooks + the temp-buffer
// re-render, extracted from main.cpp (byte-identical bodies;
// docs/plans/2026-11-08-master-plan.md §4). Owns:
//   * RwIm3DRenderIndexedPrimitive wrapper (game 0x7EF550) + forceFFPNeutral()
//   * WaterDirectFlush_Draw + WaterDirectFlush1/2_hook — W1 teal-slab fix
//     (inline RenderWater flushes, sites 0x6EF8AC / 0x6F0099)
//   * WakeDirectFlush_Draw + WakeDirectFlush1/2_hook — W5 grey-smear fix
//     (inline wake flushes, sites 0x6EA2E8 / 0x6EDD7C)
//   * g_waterTempBufferHook — S1 double-render kill-switch (config.cpp externs it)
//   * CWaterLevel__RenderAndEmptyRenderBuffer_hook + orig ptr (4 call sites)
//   * water_installHooks(void) — the byte-verified installs + waterPipe_init()
// Called once from main.cpp DllMain at the original install position
// (inside InjectDelayedPatches, which DllMain calls).
// Local-extern idiom (no shared header), matching shadows.cpp / rw_fixes.cpp.
#include "skygfx.h"
#include "waterPipe.h"
#include "particles_gpu.h"
#include <injector\hooking.hpp>

// ===== Local externs =====
// Symbols still owned by main.cpp (game TempBuffer aliases + the RW
// cache-flush wrapper). Local-extern idiom (no shared header), matching
// shadows.cpp / rw_fixes.cpp / config.cpp.
extern DWORD& TempBufferVerticesStored;	// main.cpp — 0xC4B950
extern DWORD& TempBufferIndicesStored;	// main.cpp — 0xC4B954
extern RwImVertexIndex* TempBufferRenderIndexList;	// main.cpp — 0xC4B958
extern RwIm3DVertex* TempVertexBuffer;	// main.cpp — 0xC4D958
void _rwD3D9RenderStateFlushCache(void);	// main.cpp WRAPPER (0x7FC200) — same decl idiom as buildingPipe.cpp

// ============================================================================
// SUBSYSTEM: WATER/WAKE DIRECT-FLUSH + TEMP-BUFFER RE-RENDER   (extraction: render/water.cpp)
// ============================================================================
RwBool RwIm3DRenderIndexedPrimitive(RwPrimitiveType primType, RwImVertexIndex* indices, RwInt32 numIndices) {
	return ((RwBool(__cdecl*)(RwPrimitiveType, RwImVertexIndex*, RwInt32))0x7EF550)(primType, indices, numIndices);
}

// ---- W1 teal-slab fix: the two INLINE Im3D flushes inside
// CWaterLevel::RenderWater (0x6EF650, exe 1.0 US) ----
// RenderWater flushes the TempBuffer in two places OUTSIDE the hooked
// RenderIfDoesntFit path (only 4 call sites of RenderIfDoesntFit exist and
// all are intercepted above; these two draws call RwIm3DTransform/
// RwIm3DRenderIndexedPrimitive/RwIm3DEnd directly):
//   site 1 (call 0x6EF8AC): the OUTSIDE-WORLD ocean sheet — the
//   m_BlocksToBeRenderedOutsideWorld quads pushed by 0x6E6A10/0x6E6870,
//   drawn BEFORE RenderWater binds the waterclear256 raster (0x6EFB78).
//   site 2 (call 0x6F0099): the FINAL remainder of the water.dat quad loop,
//   drawn AFTER the pipe flushes whose restore leaves stage 0 NULL.
// Both draws execute with whatever VS/PS the last object pipe left bound
// (the hook's unpark restores the pre-water stage) and no water texture:
// the world-space ocean verts (+-3000) project through a stale object WVP
// as giant screen-covering slabs shaded by the stale PS constant — the W1
// "flat untextured teal slabs" (2026-10-29 finalgate-0042; the pipe flushes
// themselves were healthy, verts=2044/indices=3066 sane).
// Fix (W1 seam wave, 2026-10-30): route through the water pipe FIRST via
// waterPipe_directFlushRoute() (waterPipe.cpp) — full Water_VS + style PS
// bind (WVP/time, c10 gate, c14/c15 body-foam contract, s0-s3, Z pin,
// classify) + the raw pinned-stage draw, so these flushes get the same
// parallax/caustics/SHORE-FOAM band as the hooked flushes (the band lives
// in the parallax PS; FFP water has none — that was the missing coast foam
// and the hard vertical seam). FFP + waterclear256Raster (0xC228A8) below
// is now only the fail-open fallback when the pipe cannot run. RW's Im3D
// transform (the call immediately before each site) owns decl/FVF/matrices;
// the raster bind is deliberately NOT restored (RW setter keeps cache+device
// in sync; vanilla leaves it bound too — water state block 0x6EFB78 /
// RenderBoatWakes' wake raster 0x6ED9AA re-bind their own).
// FFP-neutral idiom (R3, 2026-11-09): raw NULL VS/PS on the device AND seed
// the RW value-compare shader caches (_rwD3D9Last*ShaderUsed 0x8E2448/0x8E244C)
// to NULL so the next RW shader bind sees a real transition. Eight hand-copied
// sites (water/wake flush paths) consolidated here — a dropped cache seed on
// any one path is the teal-slab/hole defect class, so the seed stays
// byte-identical at every site.
static void
forceFFPNeutral(void)
{
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
}

RwBool (*CWaterLevel__RenderWaterDirectFlush1)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
RwBool (*CWaterLevel__RenderWaterDirectFlush2)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
static RwBool
WaterDirectFlush_Draw(int site, RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// ---- W1 diagnostic: water_flatpath (first-hit unconditional + 2s
	// throttle + counter — same starvation-proof pattern as water_badbuf).
	// Logs which site, the flush counts, and the stale device state the
	// draw would have run with (VS/PS handles + stage 0/s3 bindings).
	// The same capture doubles as the park (Get* AddRef — released below).
	static unsigned s_flatCount = 0;
	static unsigned s_siteHit[3] = {0, 0, 0};	// W1 gen-9: per-site running
	static bool s_siteFirst[3] = {true, true, true};	// counter + first-hit (cold-vs-starved proof)
	s_flatCount++;
	unsigned siteHits = 0;
	if(site >= 0 && site < 3){
		s_siteHit[site]++;
		siteHits = s_siteHit[site];
	}
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DBaseTexture9 *staleS0 = NULL, *staleS3 = NULL;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetTexture(0, &staleS0);
		d3d9device->GetTexture(3, &staleS3);
	}
	if(site >= 0 && site < 3 && s_siteFirst[site]){
		s_siteFirst[site] = false;
		dbglog("[water] flatpath first hit: site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p siteHits=%u total=%u",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       siteHits, s_flatCount);
	}
	if(staleS0) staleS0->Release();
	if(staleS3) staleS3->Release();

	// ---- W1 seam fix: pipe route FIRST, FFP only as fail-open ----
	// waterPipe_directFlushRoute() binds Water_VS + the style parallax PS +
	// the full pipe constant set (WVP, time, c14/c15 foam contract, s0-s3)
	// and draws this SAME TempBuffer through the pipe; TRUE = consumed.
	// FALSE = pipe unavailable (disabled/lost/insane counts) — the FFP
	// fallback below keeps the old fail-open path. The route restores the
	// pre-route stage internally, so on TRUE we only release our capture.
	extern bool waterPipe_directFlushRoute(void);	// waterPipe.cpp (function-local extern idiom)
	bool routed = waterPipe_directFlushRoute();
	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on every exit ----
	// The route just restored its pre-route stage = the captured object-pipe
	// shaders; re-publishing them (old behaviour) hands parked shader state
	// to the vanilla FFP draws that follow these sites inside
	// CWaterLevel::RenderWater. NULL VS/PS on BOTH layers (raw device +
	// _rwD3D9Last*Used seeds at 0x8E2448/0x8E244C) instead of prevVS/
	// prevPS; pipe consumers re-bind their shaders at their own callback
	// entries. AddRef/Release pairing unchanged — the captures are still
	// released below/at each exit.
	forceFFPNeutral();
	if(dbglog_throttle("water_flatpath"))
		dbglog("[water] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p (count=%u) -> %s",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       s_flatCount,
		       routed ? "pipe route (Water_VS+parallax PS, foam live)"
			      : "FFP fallback (pipe unavailable)");
	if(routed){
		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		// m0336: once-per-tick particle update+render (latched inside; the
		// first flush of the frame wins, so wake-flush calls later in the
		// frame only spawn). No-op when gpuParticlesEnable=0.
		GPUParticles_FrameFlush("water");
		return TRUE;
	}

	// Force FFP: raw NULL + seed the RW value-compare caches so the next RW
	// bind sees a real transition (vanilla fall-through idiom).
	forceFFPNeutral();

	// Re-bind the vanilla water raster for the draw (see block comment).
	RwRaster *waterRaster = *(RwRaster**)0xC228A8;	// waterclear256Raster
	if(waterRaster)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)waterRaster);

	// Flood-alpha parity (waterPipe M1 guard): the pipe route fades
	// out-of-poly verts via vertex alpha 13/255, but this FFP fallback ran
	// with VERTEXALPHAENABLE=false so the fade was ignored and the fallback
	// could still paint the over-land slab. Enable vertex-alpha blending for
	// the draw, then restore the captured trio (RW setter keeps cache+device
	// in sync; pre-seeded per POD convention).
	BOOL fvaSaved = FALSE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&fvaSaved);
	void *fsrcSaved = (void*)rwBLENDSRCALPHA, *fdstSaved = (void*)rwBLENDINVSRCALPHA;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &fsrcSaved);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &fdstSaved);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	// Original RwIm3DRenderIndexedPrimitive (0x7EF550) via the named wrapper above.
	RwBool res = RwIm3DRenderIndexedPrimitive(primType, indices, numIndices);

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)fvaSaved);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, fsrcSaved);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, fdstSaved);

	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on exit ----
	// Do NOT restore prevVS/prevPS: RW fixed-function Im3D/Im2D consumers
	// (shadows, particles, wake/splash paths) do not unbind shaders, so a
	// parked object-pipe VS/PS would process every vanilla FFP draw that
	// follows this site inside CWaterLevel::RenderWater. Leave FFP NULL on
	// BOTH layers (raw device + _rwD3D9Last*Used cache seeds at
	// 0x8E2448/0x8E244C) so the next shader-owning pipeline's bind is a
	// real transition; pipe consumers re-bind their shaders at their own
	// callback entries. RA-style release of the parked handles only.
	forceFFPNeutral();
	if(prevVS) prevVS->Release();
	if(prevPS) prevPS->Release();
	// m0336: particle update+render fallback for the FFP path (latched —
	// see the routed exit above). No-op when gpuParticlesEnable=0.
	GPUParticles_FrameFlush("water");
	return res;
}

static RwBool __cdecl
WaterDirectFlush1_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 1: outside-world ocean sheet (before the water raster bind)
	return WaterDirectFlush_Draw(1, primType, indices, numIndices);
}

static RwBool __cdecl
WaterDirectFlush2_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 2: final remainder of the water.dat quad loop (after the pipe
	// flushes cleared stage 0)
	return WaterDirectFlush_Draw(2, primType, indices, numIndices);
}

// ---- W5 grey-smear fix: the two INLINE Im3D flushes in the wake path ----
// RenderWakeSegment (triplet 0x6EA2CE/0x6EA2E8/0x6EA2F0) and
// RenderBoatWakes (0x6EDD62/0x6EDD7C/0x6EDD84) call RwIm3DTransform/
// RwIm3DRenderIndexedPrimitive/RwIm3DEnd directly — outside every hooked
// RenderIfDoesntFit site — so the draws execute with whatever VS/PS the
// last skygfx object pipe left device-bound (vanilla Im3D sets FVF/decl
// through RW but never unbinds shaders). The world-space wake verts
// project through the stale WVP and get shaded by the stale PS constant:
// translucent grey-blue smear quads (W5, seen over pier railing,
// 2026-10-29 finalgate-0042). Same family as the W1 teal-slab defect
// above; byte-verified triplets (E8 rel32 scan, exe 1.0 US), both drawing
// TempBuffer strips (primType 3 = TRISTRIP, verts 0xC4D958 /
// TempBufferVerticesStored 0xC4B950 / indices 0xC4B958 /
// TempBufferIndicesStored 0xC4B954).
// Fix: identical park-and-set idiom as WaterDirectFlush_Draw — force FFP
// (raw NULL VS/PS + RW cache seeds) and re-bind the vanilla wake raster
// (0xC228B8, loaded by RenderBoatWakes at 0x6ED9AA) for the draw. The
// raster bind is deliberately NOT restored (same reasoning as the W1
// water raster: RW setter keeps cache+device in sync, vanilla leaves it
// bound across these draws, next RW consumer re-binds its own).
RwBool (*CWaterLevel__RenderWakeDirectFlush1)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
RwBool (*CWaterLevel__RenderWakeDirectFlush2)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
static RwBool
WakeDirectFlush_Draw(int site, RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// ---- W5 diagnostic: wake_flatpath (first-hit unconditional + 2s
	// throttle + counter — same starvation-proof pattern as
	// water_flatpath). Logs which site, the flush counts, and the stale
	// device state the draw would have run with (VS/PS handles + stage 0
	// binding). The same capture doubles as the park (Get* AddRef —
	// released below).
	static unsigned s_wakeFlatCount = 0;
	static unsigned s_wakeSiteHit[3] = {0, 0, 0};	// W5 gen-9: per-site running
	static bool s_wakeSiteFirst[3] = {true, true, true};	// counter + first-hit (cold-vs-starved proof)
	s_wakeFlatCount++;
	unsigned siteHits = 0;
	if(site >= 0 && site < 3){
		s_wakeSiteHit[site]++;
		siteHits = s_wakeSiteHit[site];
	}
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DBaseTexture9 *staleS0 = NULL;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetTexture(0, &staleS0);
	}
	if(site >= 0 && site < 3 && s_wakeSiteFirst[site]){
		s_wakeSiteFirst[site] = false;
		dbglog("[wake] flatpath first hit: site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p siteHits=%u total=%u",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0,
		       siteHits, s_wakeFlatCount);
	}
	if(dbglog_throttle("wake_flatpath"))
		dbglog("[wake] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p (count=%u siteHits=%u) -> FFP+vanilla raster",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0,
		       s_wakeFlatCount, siteHits);
	if(staleS0) staleS0->Release();

	// Force FFP: raw NULL + seed the RW value-compare caches so the next
	// RW bind sees a real transition (vanilla fall-through idiom).
	forceFFPNeutral();

	// Re-bind the vanilla wake raster for the draw (see block comment).
	RwRaster *wakeRaster = *(RwRaster**)0xC228B8;
	if(wakeRaster)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)wakeRaster);

	// Original RwIm3DRenderIndexedPrimitive (0x7EF550) via the named wrapper above.
	RwBool res = RwIm3DRenderIndexedPrimitive(primType, indices, numIndices);

	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on exit ----
	// Same defect class as WaterDirectFlush_Draw: these sites sit inside
	// vanilla RenderWakeSegment / RenderBoatWakes where everything after
	// the flush is FFP. Do NOT re-publish the captured object-pipe shaders
	// (that parked shader state into every following vanilla FFP draw) —
	// leave FFP NULL on BOTH layers (raw device + _rwD3D9Last*Used cache
	// seeds at 0x8E2448/0x8E244C); pipe consumers re-bind their shaders at
	// their own callback entries. RA-style release of the parked handles
	// only.
	forceFFPNeutral();
	if(prevVS) prevVS->Release();
	if(prevPS) prevPS->Release();
	// m0336 GPU particle lane: site 1 (RenderWakeSegment) spawns wake foam
	// from the TempBuffer verts; the update+render is latched once per game
	// tick inside (the water-flush calls earlier in the frame usually win
	// the render slot, so spawns show next frame). No-op when
	// gpuParticlesEnable=0 (default) — zero device-state traffic.
	if(config && config->gpuParticlesEnable){
		if(site == 1)
			GPUParticles_WakeSpawnFromTempBuffer(site);
		GPUParticles_FrameFlush("wake");
	}
	return res;
}

static RwBool __cdecl
WakeDirectFlush1_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 1: RenderWakeSegment flush (per-segment wake strip)
	return WakeDirectFlush_Draw(1, primType, indices, numIndices);
}

static RwBool __cdecl
WakeDirectFlush2_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 2: RenderBoatWakes flush (remainder pass at the end of the
	// wake loop; no call into RenderWakeSegment after it — independent
	// draw, byte-verified)
	return WakeDirectFlush_Draw(2, primType, indices, numIndices);
}

// [S1 / W2e mixed-path gate] This temp-buffer re-render draws the game's
// water TempBuffer through the water pipe shader at four intercepted call
// sites. The original S1 kill-switch (waterTempBufferHook, default 0) was
// added because running this re-render ALONGSIDE waterPipe's other water
// renderer produced the elevated pale-blue sheet that eats the mid-ground
// house band (the "hole" symptom). W2e (2026-11-24) re-verified the
// double-draw premise against the CURRENT code and it is FALSE — every
// TempBuffer renderer empties the buffer and suppresses the next one:
//   * this hook's pipe path empties TempBuffer after the draw (see the
//     TempBufferVerticesStored = 0 below) and never calls the original
//     RenderAndEmptyRenderBuffer on success;
//   * waterPipe_directFlushRoute empties on success (waterPipe.cpp, the
//     draw-AND-empty in its success block) and the W1 caller skips the
//     original Im3D draw when it returns TRUE;
//   * every fall-through path hands the buffer to the ONE vanilla draw,
//     which empties it (vanilla RenderAndEmptyRenderBuffer contract).
// So each filled buffer has exactly one renderer per fill. What S1=0 DID
// cause under the old gate: this body path fell through to vanilla FFP
// while the W1 direct flushes kept drawing through the pipe — MIXED
// pipe/FFP water in one frame (the ×28/session "[water] gate failed
// (enable=1 ...)" receipts). W2e drops the kill-switch from the gate: the
// body draw routes through the pipe whenever the pipe can run (INI enable +
// style shader + non-empty buffer), so a frame is uniformly pipe
// (waterParallaxEnable=1) or uniformly FFP (=0). The INI key and the config
// default are untouched (config.cpp still owns the symbol); the two S1
// receipts below stay byte-stable and now only fire when the pipe genuinely
// cannot run. Parity gap (unchanged): the ripple/wake presentation this
// hook carried is not re-routed to the M-series yet (pending the
// waterPipe.cpp lane).
bool g_waterTempBufferHook = false;	// shared with config.cpp (local extern)
void (*CWaterLevel__RenderAndEmptyRenderBuffer)(void);
void
CWaterLevel__RenderAndEmptyRenderBuffer_hook(void)
{
	// Gate (W2e): only take over the water draw when the water pipe can run
	// — INI-enabled AND style-selected shader compiled AND a non-empty
	// buffer. g_waterTempBufferHook no longer participates in this decision
	// (see the block comment above: the double-draw premise is disproven by
	// the empty-on-success contract, and keeping the switch here was what
	// mixed FFP body water with pipe direct-flush water in one frame).
	// Otherwise fall through to the game's original draw — stale PBR VS/PS
	// left bound from a previous pipe was producing garbage water.
	void *waterPS = waterPipe_pixelShader();
	bool canUseWaterPipe = TempBufferVerticesStored != 0 &&
	                       config->waterParallaxEnable &&
	                       waterPS && Water_VS;
	if(!canUseWaterPipe){
		if(!g_waterTempBufferHook && TempBufferVerticesStored && dbglog_once("water_tbuf_off"))
			dbglog("[water] temp-buffer re-render disabled (waterTempBufferHook=0, S1 kill-switch) -> vanilla draw");
		else if(TempBufferVerticesStored && dbglog_throttle("water_fallthru"))
			dbglog("[water] gate failed (enable=%d vs=%p ps=%p) -> original draw",
			       config->waterParallaxEnable ? 1 : 0, waterPS, Water_VS);
		// Vanilla Im3D is fixed-function: it sets FVF/decl through RW but
		// does NOT unbind VS/PS, so a stale pipe Water_VS/waterPS still
		// device-bound would project the water TempBuffer through the last
		// object's WVP as giant screen-covering slabs (the teal-quad defect,
		// 2026-10-29). Force FFP: raw NULL + seed the RW value-compare
		// caches so the next RW bind sees a real transition.
		forceFFPNeutral();
		if(dbglog_once("water_vanilla_fallthrough"))
			dbglog("water: vanilla fall-through with pipe shaders neutralised (site=%s)",
			       "gate");
		// Original function renders AND empties the buffer (vanilla behaviour)
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		return;
	}

	_rwD3D9RenderStateFlushCache();

	// ---- Water raw-draw state park (coronaDrawRawShield discipline) ----
	// The flush below is a raw RwD3D9DrawIndexedPrimitiveUP: RW's value-
	// compare shader/FVF caches (_rwD3D9Last*Used, 0x8E2440-0x8E2450) are
	// only updated by the RW setters, so a raw-only pin leaves them naming
	// the previous pass's shader/decl and the next RW bind no-ops. Capture
	// the pre-water device state here (Get* AddRefs — released at the bottom)
	// and put it back after the draw so the flush cannot leak a NULL/foreign
	// stage into the next consumer.
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DVertexDeclaration9 *prevDecl = NULL;
	DWORD prevFVF = 0;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetVertexDeclaration(&prevDecl);
		d3d9device->GetFVF(&prevFVF);
	}

	// Set water pipe render state (our VS/PS/constants)
	waterPipe_setRenderState();

	if(g_waterParallaxActive){
		// W2e one-shot proof receipt (ADDITIVE, new key): the temp-buffer
		// body draw ran through the pipe under the live INI. Fires once per
		// session; log-only, no render path reads it.
		if(dbglog_once("water_s1_routed"))
			dbglog("[water] temp-buffer body routed through the pipe (waterTempBufferHook=%d) -> single renderer: buffer emptied here, original suppressed",
			       g_waterTempBufferHook ? 1 : 0);
		// ---- pin the raw draw stage, immediately before the flush ----
		// (a) Force a NULL transition through BOTH layers first. The RW
		// setter is value-compare gated against 0x8E2448/0x8E244C, so a
		// raw-only NULL would leave the cache naming the previous pass's
		// shader. Then raw-bind the water shaders and seed the SAME cache
		// values: the NULL step guarantees a real cache transition, while
		// the water bind keeps Water_VS/Water_Parallax live for the draw
		// (a NULL pair here would silently switch the flush to fixed
		// function and bypass the WVP/constant upload the pipe just did).
		forceFFPNeutral();
		if(d3d9device){
			d3d9device->SetVertexShader((IDirect3DVertexShader9*)Water_VS);
			d3d9device->SetPixelShader((IDirect3DPixelShader9*)waterPS);
		}
		*(void**)0x8E2448 = Water_VS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = waterPS;	// _rwD3D9LastPixelShaderUsed

		// (b) Pin the Im3D input layout. 0x152 = XYZ|NORMAL|DIFFUSE|TEX1,
		// the real 36-byte RwIm3DVertex layout (0x112 omits D3DFVF_DIFFUSE
		// and would read COLOR0/TEXCOORD0 at the wrong offsets).
		// SetVertexDeclaration(NULL) makes the FVF win over the foreign PBR
		// declaration left by the last atomic.
		if(d3d9device){
			d3d9device->SetVertexDeclaration(NULL);
			d3d9device->SetFVF(0x152);
		}
		*(void**)0x8E2444 = NULL;		// _rwD3D9LastVertexDeclarationUsed
		*(void**)0x8E2440 = (void*)0x152;	// _rwD3D9LastFVFUsed

		// (c) One-shot pre-draw depth-state receipt (plain static one-shot;
		// GetDepthStencilSurface AddRefs the surface — Released here).
		static bool waterStateLogged = false;
		if(!waterStateLogged){
			waterStateLogged = true;
			DWORD zen = 0, zf = 0, zw = 0;
			IDirect3DSurface9 *ds = NULL;
			if(d3d9device){
				d3d9device->GetRenderState(D3DRS_ZENABLE, &zen);
				d3d9device->GetRenderState(D3DRS_ZFUNC, &zf);
				d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
				d3d9device->GetDepthStencilSurface(&ds);
			}
			dbglog("[water] water_state pre-draw dev zenable=%d zfunc=%d zwrite=%d ds=%p",
			       (int)zen, (int)zf, (int)zw, (void*)ds);
			if(ds) ds->Release();
		}

		// VERTEXALPHAENABLE is set inside waterPipe_setRenderState() — keep
		// set/restore paired inside this branch so an early-out can't leak it.
		// D3D9 primitive enum, NOT the RW one: this maps to
		// IDirect3DDevice9::DrawIndexedPrimitiveUP (rwcore.h
		// _rwD3D9DrawIndexedPrimitiveUPMacro forwards primitiveType raw), so
		// rwPRIMTYPETRILIST(3) lands on D3DPT_LINESTRIP(3) — the water quads
		// drew as one connected polyline ("blue wireframe streaks" bug).
		// D3DPT_TRIANGLELIST = 4. The 4th arg is a PRIMITIVE count (D3D9
		// convention), not an index count (RwIm3DRenderIndexedPrimitive's
		// convention): tri-list primitives = numIndices/3.
		RwD3D9DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, TempBufferVerticesStored,
		                            TempBufferIndicesStored / 3,
		                            TempBufferRenderIndexList, TempVertexBuffer, sizeof(RwIm3DVertex));

		// Restore previous state
		waterPipe_restoreRenderState();

		// ---- unpark: put the pre-water stage back on BOTH layers ----
		// waterPipe_restoreRenderState() unbinds VS/PS through the RW setter
		// (cache -> NULL); the raw restore below is followed by the matching
		// cache seeds so the next RW value-compare bind sees the same state
		// the device actually holds. Decl and FVF are mutually exclusive:
		// restore whichever mode prevDecl says (forwardplus.cpp pattern).
		if(d3d9device){
			d3d9device->SetVertexShader(prevVS);
			d3d9device->SetPixelShader(prevPS);
		}
		*(void**)0x8E2448 = prevVS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = prevPS;	// _rwD3D9LastPixelShaderUsed
		if(prevDecl){
			if(d3d9device) d3d9device->SetVertexDeclaration(prevDecl);
			*(void**)0x8E2444 = prevDecl;		// _rwD3D9LastVertexDeclarationUsed
			*(void**)0x8E2440 = (void*)0xFFFFFFFF;	// _rwD3D9LastFVFUsed
		}else{
			if(d3d9device){
				d3d9device->SetVertexDeclaration(NULL);
				d3d9device->SetFVF(prevFVF);
			}
			*(void**)0x8E2444 = NULL;		// _rwD3D9LastVertexDeclarationUsed
			*(void**)0x8E2440 = (void*)prevFVF;	// _rwD3D9LastFVFUsed
		}

		TempBufferVerticesStored = 0;
		TempBufferIndicesStored = 0;

		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		if(prevDecl) prevDecl->Release();
	}else{
		// Setup failed mid-way (device lost / refraction RT unavailable /
		// buffer sanity) — hand the buffer to the vanilla draw instead of
		// drawing half-configured. Same stale-shader projection risk as the
		// gate fall-through above (teal-quad defect, 2026-10-29): force FFP
		// BEFORE the vanilla call. prevVS/prevPS were captured above (Get*
		// AddRef) and every waterPipe_setRenderState() early-out returns
		// before touching device VS/PS, so they still name the pre-water
		// stage.
		forceFFPNeutral();
		if(dbglog_once("water_vanilla_fallthrough"))
			dbglog("water: vanilla fall-through with pipe shaders neutralised (site=%s)",
			       "mid_setup");
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		// Unpark: restore the pre-water stage on BOTH layers before the
		// Releases so the neutralised NULL cannot leak into the next
		// consumer (same structure as the g_waterParallaxActive unpark).
		if(d3d9device){
			d3d9device->SetVertexShader(prevVS);
			d3d9device->SetPixelShader(prevPS);
		}
		*(void**)0x8E2448 = prevVS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = prevPS;	// _rwD3D9LastPixelShaderUsed
		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		if(prevDecl) prevDecl->Release();
		return;
	}
}

// water_installHooks — the byte-verified installs, moved verbatim from
// main.cpp InjectDelayedPatches (original position in the init sequence).
void
water_installHooks(void)
{
	// Water pipe hooks — intercept CWaterLevel::RenderAndEmptyRenderBuffer
	// at all four call sites to inject custom water rendering.
	dbglog("  HOOK: WaterLevel ::RenderAndEmptyRenderBuffer (4 sites)");
	waterPipe_init();
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8790);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8EF1);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E91E4);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E9963);

	// W1 teal-slab fix: the two INLINE Im3D flushes inside RenderWater
	// (outside-world ocean sheet 0x6EF8AC + final quad-loop remainder
	// 0x6F0099) bypass the RenderIfDoesntFit hook and drew with stale pipe
	// VS/PS + no water texture. Force FFP + vanilla raster per draw.
	dbglog("  HOOK: WaterLevel ::RenderWater inline flushes (2 sites)");
	InterceptCall(&CWaterLevel__RenderWaterDirectFlush1, WaterDirectFlush1_hook, 0x6EF8AC);
	InterceptCall(&CWaterLevel__RenderWaterDirectFlush2, WaterDirectFlush2_hook, 0x6F0099);

	// W5 grey-smear fix: the two INLINE Im3D flushes in the wake path
	// (RenderWakeSegment 0x6EA2E8 + RenderBoatWakes 0x6EDD7C) drew with
	// stale pipe VS/PS + no wake texture. Force FFP + vanilla raster per
	// draw (same idiom as the W1 water flushes above).
	dbglog("  HOOK: WaterLevel ::RenderWakeSegment/::RenderBoatWakes inline flushes (2 sites)");
	InterceptCall(&CWaterLevel__RenderWakeDirectFlush1, WakeDirectFlush1_hook, 0x6EA2E8);
	InterceptCall(&CWaterLevel__RenderWakeDirectFlush2, WakeDirectFlush2_hook, 0x6EDD7C);
}
