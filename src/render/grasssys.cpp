// grasssys.cpp - consolidated PC plant island translation unit.
//
// Merged 2026-10-27 from TWO former TUs, concatenated in this order:
//   1. src/render/PC_GrassRenderer.cpp - PC plant renderer (legacy path)
//   2. src/render/PC_GrassInstancer.cpp - batched/instanced plant drawing
//
// 2026-11-01 dead-code sweep (source-inventory §2): the never-wired
// CGrassRenderer / CPPTriPlantBuffer draw path and the CGrassInstancer
// batching API (BeginFrame/EndFrame/BeginBatch/EndBatch/AddInstance/
// DrawTriPlant/ShaderReady/StatInstances/StatDraws — zero callers, gate
// DEFAULT OFF) were removed. Remaining live surface:
//   - CGrassInstancer::IsEnabled / SetEnabled  (main.cpp grassInstancing gate)
//   - GrassResetShaderLatches                  (depthhook.cpp, device Reset)
//   - GrassEnforceDepthContract                (main.cpp grassRenderCallback)
// The s_grassInstPS / s_grassInstPSTried latch statics are kept because both
// live functions reference them.

// ---- Includes ----
#include <rwcore.h>
#include <rpworld.h>
#include "skygfx.h"
#include "grasssys.h"
#include "../rw/gta.h"		// GrassEnforceDepthContract: d3d9device (gta.h:282)
#include "texdefs.h"		// GrassEnforceDepthContract: TexDefsGetD3DTexture (L3 pin)


//
// ---- TASK 2 gate: master switch, DEFAULT OFF -------------------------------
// Only main.cpp flips this (CGrassInstancer::SetEnabled, driven by the
// grassInstancing INI key). While false the game's own CPlantMgr::Render
// (0x5DBAE0) keeps drawing and the instancer draw path never runs — a
// half-wired state can never double-render.
//
static bool		s_masterEnable = false;

bool
CGrassInstancer::IsEnabled(void)
{
	return s_masterEnable;
}

void
CGrassInstancer::SetEnabled(bool enable)
{
	// Staged main.cpp install path only — see DEFERRED DIFF (checkpoint).
	s_masterEnable = enable;
}


//
// GrassInstanced.ps latch state. The lazy loader itself was removed with the
// dead instancer draw path; these statics remain because
// GrassResetShaderLatches clears them and the GrassEnforceDepthContract
// one-shot receipt logs them.
//
static void*		s_grassInstPS = NULL;
static bool			s_grassInstPSTried = false;


//
// Device-Reset shader-latch resetter (L11; called by depthhook.cpp after
// Pipeline_InvalidateShaders()/CreateShaders()). The lazy loader above caches
// the PS handle and a one-shot load-tried latch; a device Reset dead-handles
// the PS but leaves the latch set, so the next draw would keep binding the
// dead handle. Clear both so the next grass draw re-creates GrassInstanced.ps
// for the live device. Fail-open: if the reload fails, the loader logs once
// and SubmitChunk() degrades to the fixed-function Im3D path, unchanged.
//
void
GrassResetShaderLatches(void)
{
	s_grassInstPS = NULL;
	s_grassInstPSTried = false;

	// S6 (test-4) — the LIVE plant mod PS is a reset casualty too.
	// grassPixelShader (main.cpp:121) is created exactly once by
	// CPlantMgr_Initialise (main.cpp CPlantMgr_Initialise, IDR_GRASSPS) and
	// is covered by NEITHER Pipeline_InvalidateShaders/CreateShaders (the
	// makePS/makeVS cache, pipelinecommon.cpp:662-712) NOR the lazy-latch
	// pattern above (its only consumer, grassRenderCallback, BINDS the
	// handle and never re-creates it). After a device Reset the handle is
	// dead; depthhook.cpp:466-467 seeds _rwD3D9LastPixelShaderUsed=NULL, so
	// the default-callback hook's value-compare forces a REAL
	// SetPixelShader(dead handle) — which fails silently and leaves the
	// previous draw's shader bound (incompatible constant layout: its c0 is
	// colorScale/fxParams, not the plant material colour) => the plant pass
	// samples black => pure-black vegetation silhouettes. Exactly the
	// failure class documented at depthhook.cpp:394-399. Recreate it here
	// against the live device from the same embedded CSO (IDR_GRASSPS),
	// mirroring makePS semantics (pipelinecommon.cpp:662-687): the dead
	// handle is NOT Released (pipelinecommon.cpp:726 convention) and a
	// failed recreate leaves NULL — fail-open to the FFP plant draw, which
	// is visible (texture x material colour), never black.
	// Cross-lane extern: same function-local-extern idiom as the resetter
	// block at depthhook.cpp:410-414 — no header outside this lane edited.
	extern void *grassPixelShader;		// main.cpp:121
	// S6 hardening: null the handle BEFORE the dllModule guard. The old code
	// only nulled it inside `if(dllModule)`, so a NULL dllModule left the DEAD
	// pre-Reset handle cached — grassRenderCallback binds it, the
	// value-compare hook issues a real SetPixelShader(dead handle) that fails
	// silently, and the previous draw's shader (incompatible constant layout)
	// stays bound => pure-black plant silhouettes. NULL instead fails open to
	// the FFP plant draw (texture x material colour — visible, never black),
	// exactly the fail-open the block comment above promises.
	grassPixelShader = NULL;
	if(dllModule)
	{
		HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(IDR_GRASSPS), RT_RCDATA);
		RwUInt32 *shader = resource ? (RwUInt32*)LoadResource(dllModule, resource) : NULL;
		if(shader == NULL){
			dbglog("[veg] grassPixelShader post-Reset recreate: resource load FAILED (res=%p) — plants fall back to FFP", (void*)resource);
			return;
		}
		RwD3D9CreatePixelShader(shader, &grassPixelShader);
		FreeResource(shader);
		dbglog("[veg] grassPixelShader recreated post-Reset: ps=%p", grassPixelShader);
	}
}


// debug_handl.cpp — local decl (debugmenu_ui.cpp:4249 idiom; debug_handl.h
// deliberately not included in this TU's include set)
extern unsigned DebugFrameId(void);

//
// GrassEnforceDepthContract — §4.1 vegetation draw-order fix (2026-10-30).
//
// ROOT CAUSE. In vanilla SA the plant pass runs INSIDE RenderScene at step 16
// (gtasa_src Main.cpp:3379) — AFTER all world geometry (roads :3262, everything-
// bar-roads :3270, water :3301, shadows :3316) — and relies on the Z-test that
// RenderScene step 3 restored (Main.cpp:3239-3241) still being on at the device.
// Vanilla plant state is ZWRITE off / SRCALPHA-INVSRCALPHA / alpha-test
// ALWAYS@0 (SKYGFXPLUS_DOCS Pipelines/Grass Rendering.md:52), so occlusion comes
// entirely from Z-testing against the completed scene depth.
//
// This build writes render state through three layers (L1 rw cache, L2 RW
// driver cache, L3 raw device). A raw L3 write that leaves the device with
// Z-test off while the L1 cache still says TRUE makes every later L1-only
// ZTEST set a value-compare NO-OP — the device stays Z-off and the plant pass
// draws through walls and over peds. The failure class is MEASURED in
// skygfx_dbg.log ("[PedOpaque] pre-entry dev ... ZWRITE=0 ZENABLE=0 ZFUNC=8"
// — Z-test off + ZFUNC ALWAYS behind a TRUE rw cache) and is the same
// rw-cache-vs-device no-op mechanism documented at the corona entry shield
// (main.cpp, "ZTEST ENFORCEMENT (always-on-top round)").
//
// Every other world-geometry entry already carries a three-layer entry repair:
// buildings (DefPipe "entry contract forced canonical ... ztest=1 via L1+L2+L3"),
// peds (PedOpaque "contract repaired divergent entry"), coronas (CoronaShield
// ZTEST enforcement), water (its own pre-draw ZENABLE/ZFUNC pin). The plant
// pass was the one unshielded world-geometry draw — hence vegetation alone
// showed the always-on-top symptom while the Sep 3 caps were correct.
//
// FIX. Enforce the world depth baseline (Z-test ON, ZFUNC LESSEQUAL — the
// pipeApplyWorldBaseline targets, pipelinecommon.cpp:2029-2030) at the plant
// draw, repaired through ALL THREE layers so the caches agree with the device
// and a later RW flush cannot re-drift. Ground truth is the raw device
// (GetRenderState), never the rw wrapper cache — same doctrine as the corona
// shield. Healthy frames cost two GetRenderState calls and no writes.
//
// Deliberately NOT touched here:
//   * ZWRITE — owned by the dual-pass legs in grassRenderCallback (leg 1
//     ZWRITE on / leg 2 off); vanilla grass runs ZWRITE off.
//   * Alpha test / blend — pipeApplyWorldBaseline excludes the alpha block for
//     the same reason (pipelinecommon.cpp:1968-1972): a global func/ref would
//     break the caller-owned cutout legs.
//   * Cull mode — grassRenderCallback patches it only when grassBackfaceCull=0
//     and restores CULLBACK; the world baseline is BACK.
//
// Call site (main.cpp lane — one line at the top of grassRenderCallback, the
// per-atomic render callback attached by CPlantMgr_Initialise @0x5BF8EA):
//     GrassEnforceDepthContract();
// Declared in grasssys.h so main.cpp needs no declaration edit (it already
// includes PC_GrassInstancer.h -> grasssys.h).
//
// No restore-after: the repair moves the device TO the world baseline (the
// same enforce-not-park pattern as PedOpaque/DefPipe/CoronaShield), and the
// vanilla flow after the plant pass (bottom clouds / rain / sun reflection,
// RenderEffects) re-derives its own state; leaving Z-test on matches the
// RenderScene step-3 baseline those passes expect.
//
// HOT-PATH GATE (2026-10-31): the caller is per-atomic, but the check runs at
// most ONCE per RW render frame (RWSRCGLOBAL(renderFrame) gate, softbody
// idiom) — the drift sources are frame-boundary passes, so the first plant
// atomic of the frame checks+repairs for the whole pass. Device calls per
// frame drop from N_atomics x 2 to 2 (plus the one-shot receipt's reads on
// the very first frame only).
//
void
GrassEnforceDepthContract(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;

	// ---- once-per-RW-frame gate (hot-path cost fix, 2026-10-31) ------------
	// grassRenderCallback (main.cpp:768) runs this PER PLANT ATOMIC — with
	// thousands of plant atomics per frame the two raw GetRenderState reads
	// below (plus the drifted-path GetTexture readback) were thousands of
	// driver calls per frame, a measurable hot-path cost. The drift sources
	// are frame-boundary passes (postfx/water/mirror raw writes), so the
	// device state cannot drift WITHIN one plant pass in a way that
	// per-atomic checking uniquely catches: checking once at the FIRST plant
	// atomic of each RW render frame and repairing there protects the whole
	// pass — the repair moves the device TO the baseline and nothing in the
	// plant pass itself writes Z/texture state behind the caches. Same idiom
	// as the softbody renderFrame gate (softbody.cpp:1153-1158); RwUInt32
	// counter (treeWind clock idiom, treeWind.cpp:119) so a 16-bit wrap can
	// never skip a check. Mirror/shadow re-entries that bump renderFrame get
	// their own check — correct, since state between camera passes can drift.
	static RwUInt32 s_lastCheckedFrame = (RwUInt32)-1;
	RwUInt32 curFrame = RWSRCGLOBAL(renderFrame);
	if(curFrame == s_lastCheckedFrame)
		return;
	s_lastCheckedFrame = curFrame;

	// ---- defense-in-depth DebugFrameId latch (perf audit fix, 2026-11-01) --
	// DOUBLE-GATED by design: (1) the call site in main.cpp
	// grassRenderCallback is being gated once-per-frame by the main.cpp
	// owner (parallel lane — do NOT rely on it from here), and (2) this
	// TU's own internal latches. The RWSRCGLOBAL(renderFrame) latch above
	// already limits the body to once per RW render frame; this second
	// latch keys on DebugFrameId() (the same renderFrame tick, widened to
	// 32 bits with a wrap epoch — debug_handl.cpp) so the body stays
	// once-per-frame even if the renderFrame latch is ever loosened or the
	// caller pattern changes. The repair logic below is unchanged.
	static unsigned s_lastDbgFrame = (unsigned)-1;
	unsigned dbgFrame = DebugFrameId();
	if(dbgFrame == s_lastDbgFrame)
		return;
	s_lastDbgFrame = dbgFrame;

	// ---- [veg] one-shot S6 receipt (test-4 black-silhouette hunt) ---------
	// Runs at the silhouette draw site (grassRenderCallback calls this at
	// the top of every plant-atomic render, BEFORE the pipeline binds
	// anything). One line capturing all three black-silhouette candidates:
	//   (a) stage-0 texture: L3 device ground truth (GetTexture) vs the L1
	//       rw-cache raster vs the game's own plant texture ptr (0xC02DC0 —
	//       what setTextureAndColor just wrote into the material) + name.
	//       A dev=NULL / l1ras!=NULL split is the value-compare no-op class.
	//   (b) the modulate colour term: PS c0 as the device holds it (what
	//       grassPS multiplies; grassRenderCallback uploads the material
	//       colour here when ps2ModulateGrass is on). All-zero => black.
	//   (c) the bound PS vs grassPixelShader (a dead/stale handle means the
	//       device kept the PREVIOUS draw's shader — incompatible constant
	//       layout) + this TU's latch state (instancer PS latch, treeAlphaPS).
	// One-shot: the symptom is persistent, so the first plant draw of the
	// session discriminates all three. GetTexture/GetPixelShader AddRef —
	// both released below.
	static bool s_vegReceiptDone = false;
	if(!s_vegReceiptDone)
	{
		s_vegReceiptDone = true;
		IDirect3DBaseTexture9 *tex0 = NULL;
		IDirect3DPixelShader9 *ps0 = NULL;
		float c0[4] = { -1.0f, -1.0f, -1.0f, -1.0f };	// pre-seeded: failed Get leaves it
		HRESULT hrT = dev->GetTexture(0, &tex0);
		HRESULT hrP = dev->GetPixelShader(&ps0);
		HRESULT hrC = dev->GetPixelShaderConstantF(0, c0, 1);
		RwRaster *l1Ras = NULL;							// pre-seeded: failed Get leaves it
		RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&l1Ras);
		RwTexture **plantTex = (RwTexture**)0xC02DC0;	// game CPlantMgr current texture (main.cpp tmptexture)
		extern void *grassPixelShader;					// main.cpp:121 — live plant mod PS
		extern void *treeAlphaPS;						// treeWind.cpp — console tree overlay PS
		dbglog("[veg] tex0: dev=%p hr=0x%08X l1ras=%p plantTex=%p name='%s' | ps: dev=%p grassPS=%p hr=0x%08X | c0=(%.3f,%.3f,%.3f,%.3f) hr=0x%08X | latches: instPS=%p tried=%d treeAlphaPS=%p",
		       (void*)tex0, (unsigned)hrT, (void*)l1Ras,
		       (plantTex && *plantTex) ? (void*)*plantTex : NULL,
		       (plantTex && *plantTex) ? (*plantTex)->name : "",
		       (void*)ps0, grassPixelShader, (unsigned)hrP,
		       c0[0], c0[1], c0[2], c0[3], (unsigned)hrC,
		       (void*)s_grassInstPS, (int)s_grassInstPSTried, treeAlphaPS);
		if(tex0)
			tex0->Release();
		if(ps0)
			ps0->Release();
		// (b) vertex/material colour discriminator — read straight off the
		// first plant atomic (grassRenderCallback is attached to exactly
		// these 8, main.cpp plantTab0/plantTab1 @0xC039F0/0xC03A00). The mod
		// PS output is tex * c0 (c0 = materials[0]->color, uploaded by
		// grassRenderCallback main.cpp:819) and the FFP path is
		// tex * prelight * material — any zeroed term paints the card pure
		// black. A black mat colour points at the tmpintensity /
		// setTextureAndColor chain (main.cpp:2569); a black prelight at the
		// geometry itself.
		extern RpAtomic *(*plantTab0)[4];	// main.cpp:805 (non-static global)
		RpAtomic *vegA = (*plantTab0)[0];
		// AUDIT: materials[0] is derefed below — a corrupt clump can carry a
		// NULL material slot even with numMaterials > 0. This receipt runs
		// OUTSIDE any __try, so guard the deref instead of faulting the
		// plant pass on a one-shot diagnostic.
		if(vegA && vegA->geometry && vegA->geometry->matList.numMaterials > 0
		   && vegA->geometry->matList.materials[0]){
			RpMaterial *vm = vegA->geometry->matList.materials[0];
			RwRGBA *vpre = RpGeometryGetPreLightColors(vegA->geometry);
			dbglog("[veg] plant0 geom=%p mat=(%u,%u,%u,%u) prelight0=(%u,%u,%u,%u) prelit=%d",
			       (void*)vegA->geometry,
			       vm->color.red, vm->color.green, vm->color.blue, vm->color.alpha,
			       vpre ? vpre[0].red : 0u, vpre ? vpre[0].green : 0u,
			       vpre ? vpre[0].blue : 0u, vpre ? vpre[0].alpha : 0u,
			       vpre ? 1 : 0);
		}
	}

	// Ground truth = the raw device (L3). The rw wrapper cache can disagree
	// with the device — reading L1/L2 here would trust the very caches that
	// no-op'd the vanilla restore.
	DWORD devZen = TRUE;
	DWORD devZFunc = D3DCMP_LESSEQUAL;
	if(FAILED(dev->GetRenderState(D3DRS_ZENABLE, &devZen)) ||
	   FAILED(dev->GetRenderState(D3DRS_ZFUNC, &devZFunc)))
		return;

	// Healthy world baseline (pipeApplyWorldBaseline): Z-test on, LESSEQUAL.
	if(devZen && devZFunc == D3DCMP_LESSEQUAL)
		return;

	// Drifted — repair through all three layers (L1 rw cache / L2 RW driver
	// cache / L3 raw device) so none is left disagreeing:
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	dev->SetRenderState(D3DRS_ZENABLE, TRUE);
	dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	if(dbglog_throttle("grass_zfix"))
		dbglog("[grass_zfix] device Z drift repaired at plant draw: ZENABLE=%u ZFUNC=%u -> TRUE/LESSEQUAL",
		       (unsigned)devZen, (unsigned)devZFunc);

	// ---- stage-0 texture contract (S6 candidate (a), the bind family) ------
	// The plant pass is the one world-geometry draw with NO three-layer entry
	// bind: its diffuse reaches device stage 0 through the game's own RW
	// default render callback -> RwD3D9SetTexture, which is change-gated
	// against RW's texture cache (L2 ONLY). When postfx/water leaves a
	// screen-family raster (or NULL) on device stage 0 while RW's cache still
	// names the plant raster, that per-mesh bind is a cache-hit NO-OP — the
	// device keeps sampling the leaked raster / NULL and the mod PS
	// (tex * c0) outputs pure black. This is the same measured class
	// bldBindDiffuse3L (buildingPipe.cpp:1152-1204) was built for — the
	// "dominant test-4 symptom" — and the plant pass was the one unshielded
	// draw, exactly like the Z contract above.
	//
	// Ground truth is the raw device (GetTexture readback) vs the INTENDED
	// bind (the game's current plant texture, *tmptexture @0xC02DC0 — what
	// setTextureAndColor main.cpp:2574 just put on the material). The repair
	// only fires when the rw cache ALREADY names the target raster: that is
	// the one state where RW's own bind during RxPipelineExecute can never
	// heal the device (cache-hit no-op). A cache miss means RW's bind is a
	// real transition — nothing to do. Healthy frames cost one rw-cache Get
	// and one device Get, no writes; a same-texture rebind is state-identical
	// (binding-only, mirrors vehBindDiffuse3L vehiclePipe.cpp:794).
	{
		RwTexture **plantTexPtr = (RwTexture**)0xC02DC0;	// game CPlantMgr current texture
		RwTexture *want = plantTexPtr ? *plantTexPtr : NULL;
		if(want && want->raster){
			RwRaster *l1Ras = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&l1Ras);
			if(l1Ras == want->raster){
				IDirect3DBaseTexture9 *dev0 = NULL;
				if(SUCCEEDED(dev->GetTexture(0, &dev0))){
					IDirect3DTexture9 *wantD3D = TexDefsGetD3DTexture(want);
					bool diverged = (dev0 != (IDirect3DBaseTexture9*)wantD3D);
					if(dev0)
						dev0->Release();	// GetTexture AddRefs
					if(diverged && wantD3D){
						// L1 rw cache — PARK-AND-SET (park on a
						// guaranteed-different raster first so the real set
						// is a transition, never a cache-hit no-op).
						RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
							? gpWhiteTexture->raster : NULL;
						RwRaster *parkRas = (want->raster == whiteRas) ? NULL : whiteRas;
						RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
						RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)want->raster);
						// L2 RW driver cache / texture apply.
						RwD3D9SetTexture(want, 0);
						// L3 raw device — the only layer a cache-hit skip
						// can never hold back. wantD3D is a BORROWED
						// TexDefsGetD3DTexture pointer (never released).
						dev->SetTexture(0, wantD3D);
						if(dbglog_throttle("veg_tex0"))
							dbglog("[veg] stage-0 drift repaired at plant draw: dev0=%p want=%p tex='%s' (rw cache already named the raster — RW's L2 bind was a cache-hit no-op)",
							       (void*)dev0, (void*)wantD3D,
							       want->name ? want->name : "");
					}
				}
			}
		}
	}
}
