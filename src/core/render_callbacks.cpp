// render_callbacks.cpp — default atomic render-callback machinery
// extracted from main.cpp (2026-11-09). Bodies byte-identical to the
// pre-extraction main.cpp. Owns:
//   * atomicHasAlpha() — colour/prelit/texture alpha probe for the dual-pass gate
//   * the PED OPAQUE CONTRACT (PedOpaqueState + pedOpaqueCapture/Diverged/
//     Enforce/Restore) — three-layer canonical-opaque signature for skin
//   * pedStage0Resync() — stage-0 texture-raster cache<->device repair
//   * vs_probe() — vertex-stage drift probe (ped_exit; frame_end via main.cpp)
//   * myDefaultCallback() — the game's AtomicDefaultRenderCallBack
//     (hooked at 0x7491C0 from main.cpp DllMain)
// No install hook: the 0x7491C0 intercept stays in main.cpp DllMain.
#include "skygfx.h"
#include "debug_handl.h"
#include "peds/peds_pipe.h"
#include "peds/skin.h"
#include "texdefs.h"

// ===== Local externs =====
// Symbols still owned by main.cpp (the alpha-ref trio / dual-pass helpers) and
// by other TUs (pipelinecommon.cpp). Local-extern idiom (no shared header),
// matching shadows.cpp / rw_fixes.cpp / config.cpp.
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);          // pipelinecommon.cpp
extern int clampAlphaRef(int ref);                            // main.cpp
extern void setAlphaTestTwoLayer(RwUInt32 rwFunc, int ref);   // main.cpp
extern void setZWriteTwoLayer(RwBool on);                     // main.cpp

// buildingPipe.cpp — three-layer alpha/blend repair (block comment there).
// Forward-declared for the PED pipe-group entry contract below: the ped
// half needs the SAME layer-3 repair the building Switch gets, and the
// declaration near the skidmark draw (line ~1240) sits after this use.
extern void pipeForceAlphaBlock(void);

// pipelinecommon.cpp — the default-pipe entry contract below routes EVERY
// render-state write through pipeDeviceStateSet (the shared 3-layer
// chokepoint: L1 rw twin + L2 driver pending[] + L3 raw device from the
// ONE value, so the contract can never birth an L2=1/L3=0 split) and then
// commits pending[]->applied[] with _rwD3D9RenderStateFlushCache (the RW
// driver-cache flush, main.cpp WRAPPER — same local-extern idiom
// buildingPipe.cpp:60 / water.cpp:28 use). Local-extern idiom as above.
extern void pipeDeviceStateSet(D3DRENDERSTATETYPE state, DWORD value);
void _rwD3D9RenderStateFlushCache(void);

// Atomic-level analogue of D3D9RenderDual's per-mesh gate (see the
// `dual && hasAlpha && zwrite` test in D3D9RenderDual). That helper reads
// D3DRS_ALPHABLENDENABLE — the vertex-alpha state the callback has ALREADY
// set for the mesh about to draw — which does not exist yet here: this
// callback runs the whole pipeline as one unit, before any per-mesh state.
// Probe the geometry the instance callback derives that flag from instead:
// material colour alpha plus the prelit colour alpha channel (the same
// source _rpD3D9VertexDeclarationInstColor folds into instData->vertexAlpha).
// True only if the atomic can actually produce a blended mesh — the only
// case the two-leg pair helps: leg 1 seeds depth at the threshold, leg 2
// (LESS@threshold, zwrite OFF) paints the part below it. On an opaque mesh
// leg 2 is pure overdraw, and with blending off its LESS test rasterises
// every texture texel below the threshold into a solid quad.
// `includeTextureAlpha` is the PED-side extension (diagnostic only: the ped
// dual pass is unconditional, see the legacy gate below). CJ head/hair/
// bandana materials are alpha-255 over CUTOUT texture masks, which the
// colour/prelit probe above cannot see. TexAlpha_Classify caches per raster,
// so the extra walk is cheap.
static bool
atomicHasAlpha(RpAtomic *atomic, bool includeTextureAlpha)
{
	RpGeometry *geom = atomic ? atomic->geometry : NULL;
	if(!geom)
		return false;
	if(geom->matList.materials){
		for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
			RpMaterial *m = geom->matList.materials[i];
			if(m && m->color.alpha != 255)
				return true;
		}
		if(includeTextureAlpha){
			// Local extern (texalpha.cpp): TAC_CUTOUT=2, TAC_FADED=3
			// (enum TexAlphaClass in texalpha.h; not included here).
			extern int TexAlpha_Classify(const RwTexture *tex);
			for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
				RpMaterial *tm = geom->matList.materials[i];
				if(tm && tm->texture){
					int tac = TexAlpha_Classify(tm->texture);
					if(tac == 2 || tac == 3)
						return true;
				}
			}
		}
	}
	if(geom->preLitLum){
		const RwRGBA *c = geom->preLitLum;
		for(RwInt32 i = 0; i < geom->numVertices; i++)
			if(c[i].alpha != 255)
				return true;
	}
	return false;
}

// ============================================================================ 
// SUBSYSTEM: CALLBACK HELPERS (vs_probe vertex-stage drift probe) 
// ============================================================================ 
// render_callbacks.cpp keeps the vertex-stage drift probe (vs_probe) and the
// [PedRS] monitor helper. The PedOpaqueState contract moved into
// src/peds/skin.cpp (SkinEntryContract / SkinExitContract) when pedsPipeEnable
// flipped to 1 (commit 1f3352e); this file no longer carries it.
// ============================================================================ 

void
vs_probe(const char *site, const char *tag)
{
	if(!d3d9device) return;
	void *cacheVS = *(void**)0x8E2448;
	void *cacheDecl = *(void**)0x8E2444;
	IDirect3DVertexShader9 *devVS = NULL;
	IDirect3DVertexDeclaration9 *devDecl = NULL;
	d3d9device->GetVertexShader(&devVS);
	d3d9device->GetVertexDeclaration(&devDecl);
	if((void*)devVS != cacheVS || (void*)devDecl != cacheDecl){
		// First mismatch is UNTHROTTLED: throttle buckets are shared and can
		// starve, which made a missing vs_probe line inconclusive.
		static bool s_vsProbeFirstMismatchLogged = false;
		if(!s_vsProbeFirstMismatchLogged){
			s_vsProbeFirstMismatchLogged = true;
			dbglog("vs_probe: FIRST mismatch site=%s cacheVS=%p devVS=%p cacheDecl=%p devDecl=%p",
				site, cacheVS, (void*)devVS, cacheDecl, (void*)devDecl);
		}
		if(dbglog_throttle(tag))
			dbglog("vs_probe: site=%s cacheVS=%p devVS=%p cacheDecl=%p devDecl=%p",
				site, cacheVS, (void*)devVS, cacheDecl, (void*)devDecl);
	}
	if(devVS) devVS->Release();
	if(devDecl) devDecl->Release();
}

// Resync the stage-0 texture from the RW cache truth so the skin pipeline's
// per-material bind is a real transition. A raw NULL bind earlier can leave
// the device on a foreign raster while the cache still reports the old one;
// setting the cached value alone would then be a cache-hit no-op.
//
// NULL-cache recovery (test-4 grey-mannequin fix): when the rw texture cache
// itself holds NULL (postfx/water passes leave stage-0 NULL/leaked on exterior
// peds), the old park/set re-bound NULL and the L3 extraction had nothing to
// resolve — the ped drew untextured (grey skin, black legs). Walk the atomic
// being drawn (RpAtomic -> RpGeometry -> RpMaterialList -> RpMaterial->texture)
// for the real diffuse RwTexture and bind it through ALL THREE layers, the
// vehBindDiffuse3L idiom (vehiclePipe.cpp:767-821):
//   L1  RwRenderStateSet(rwRENDERSTATETEXTURERASTER) park-and-set — park on a
//       guaranteed-different raster first so the real set is always a
//       transition, never a cache-hit no-op;
//   L2  RwD3D9SetTexture(resolved, 0) — RW driver/applied[] cache;
//   L3  d3d9device->SetTexture(0, TexDefsGetD3DTexture(resolved)) — raw device,
//       the only layer a cache-hit skip can never hold back.
// No material texture found (or none with a raster) resolves to gpWhiteTexture
// — the pipe's neutral opaque sample — so the ped draws paint colour via
// MODULATE(white, DIFFUSE) instead of inheriting the previous pass's raster.
static void
pedStage0Resync(RpAtomic *atomic, PedPipeState *pdsState)
{
	if(!d3d9device)
		return;
	// PedsPipe absorb (design doc §3.1 — SkinStage0Resync state lane):
	// per-clump stage-0 latch bookkeeping. pdsState is non-NULL only when
	// the module is enabled AND owns this atomic (classification produced);
	// the receipts below are the legacy fallback and stay byte-identical.
	if(pdsState){
		pdsState->stage0Latch = 1;	// resync armed
		pdsState->lastResyncFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
	}
	void *rasRaw = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rasRaw);
	RwRaster *cur = (RwRaster*)rasRaw;
	if(cur == (RwRaster*)0xFFFFFFFF)
		cur = NULL;	// failed Get sentinel

	// ===== NULL-cache recovery path =====
	// The cache holds NULL: there is no cached truth to re-bind, so recover
	// the texture from the atomic's own materials and push it through all
	// three layers (see block comment above). Fully replaces the park/set
	// below for this case — parking white then setting NULL is exactly the
	// defect being repaired.
	if(cur == NULL){
		RwTexture *recovered = NULL;
		if(atomic && atomic->geometry && atomic->geometry->matList.materials){
			RpGeometry *geom = atomic->geometry;
			for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
				RpMaterial *m = geom->matList.materials[i];
				// First material with a real diffuse texture wins (skin
				// materials share one diffuse; later mats are heads/details
				// that re-bind per-material anyway).
				if(m && m->texture){
					recovered = m->texture;
					break;
				}
			}
		}
		// N9 policy honor (twin vehPolicyTexture, vehiclePipe.cpp:870-878):
		// `recovered` can be (1) NULL — legitimate "no texture" on the walk
		// path, still resolved to gpWhiteTexture below, unchanged; (2) a
		// POLICY MISS SENTINEL — TexDefsFindCB bound it in place of a failed
		// lookup, so the NULL test above can never see it; (3) a real
		// texture. This is a WORLD consumer: a sprite-class sentinel
		// (skygfx_missclear, 1x1 transparent magenta) must never be bound on
		// ped skin (magenta skin, or alpha-ref failure instead of visible
		// grey) — re-map it to the grey world sentinel. The discrimination
		// is pointer-identity (TexDefsMissTextureKind, texdefs.h:136-147),
		// which never allocates — the TexDefsMissSentinel* accessors would
		// create on demand and must not be called with a real texture in
		// hand. WORLD-kind (grey already) binds AS-IS.
		RwTexture *resolved = TexDefsResolveAtDraw(recovered);	// 2026-11-16: sentinel -> real if TXD loaded since
		if(resolved && TexDefsMissTextureKind(resolved) == TEXDEFS_MISS_TEX_SPRITE){
			if(dbglog_throttle("n9_pedcb_remap"))
				dbglog("[PedTex] N9: sprite-class miss sentinel re-mapped to grey world sentinel (was '%s')",
				       recovered->name[0] ? recovered->name : "?");
			resolved = TexDefsRemapToWorld(resolved);	// name-preserving world remap
		}
		if(!resolved){
			RwTexture *recovered = TexDefsResolveNullDraw();	// A2: late-loaded TXD recovery
			if(recovered)
				resolved = recovered;
			else
				resolved = TexDefsMissSentinelWorld();	// A1: NULL from find-CB (miss) -> grey sentinel at draw (game keeps NULL)
		}
		// L1 rw cache — park-and-set (vehBindDiffuse3L stage-0 discipline).
		if(resolved && resolved->raster){
			RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
				? gpWhiteTexture->raster : NULL;
			RwRaster *targetRas = resolved->raster;
			RwRaster *parkRas = (targetRas == whiteRas) ? NULL : whiteRas;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)targetRas);
		}
		// L2 RW driver cache / texture apply (maps raster -> d3d internally).
		RwD3D9SetTexture(resolved, 0);
		// L3 raw device: bypass every cache. Only pin on a guarded extraction
		// success — never clear a valid bind on extraction failure (L2 already
		// bound the resolved texture correctly).
		if(resolved){
			IDirect3DTexture9 *d3d = TexDefsGetD3DTexture(resolved);
			if(d3d)
				d3d9device->SetTexture(0, d3d);
		}
		// Receipts: the first successful recovery names the texture
		// (dbglog_once — cannot be starved by bucket collisions); a repeated
		// walk failure is throttled so a broken atomic cannot spam the log.
		if(recovered){
			if(pdsState)
				pdsState->stage0Latch = 2;	// recovered this clump
			if(dbglog_once("ped_tex_recovered"))
				dbglog("[PedTex] recovered %s (atomic=%p geom=%p nmats=%d)",
				       recovered->name[0] ? recovered->name : "?",
				       (void*)atomic, (void*)atomic ? (void*)atomic->geometry : NULL,
				       atomic && atomic->geometry ? (int)atomic->geometry->matList.numMaterials : -1);
		}else{
			if(pdsState)
				pdsState->stage0FailCount++;	// consecutive recovery failures
			if(dbglog_throttle("ped_tex_recover_fail"))
				dbglog("[PedTex] recover FAILED: no material texture on atomic %p — gpWhiteTexture fallback",
				       (void*)atomic);
		}
		return;
	}

	// Park with a value guaranteed != target so both caches must transition.
	RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
	RwRaster *park = (cur == whiteRas) ? (RwRaster*)NULL : whiteRas;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)cur);
	// L3: resolve the cached raster to its D3D texture and pin it raw.
	// Proven extraction (texalpha.cpp:571-584 / texdefs.cpp TexDefsGetNormalMap):
	// ext = raster + RwD3D9RasterExtOffset; ext+0 = IDirect3DTexture9*. The
	// ext+8 "valid" byte can be stale, so accept a plausible pointer on its own.
	IDirect3DTexture9 *tex = NULL;
	if(cur){
		DWORD extOff = RwD3D9RasterExtOffset;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)cur + extOff);
			if(cand &&
			   (uintptr_t)cand >= 0x10000u &&
			   (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
	}
	if(tex)
		d3d9device->SetTexture(0, tex);
	if(dbglog_once("ped_tex_resync"))
		dbglog("[PedTex] stage0 resync cached=%p dev=%p", (void*)cur, (void*)tex);
}

// myDefaultCallback — the game's AtomicDefaultRenderCallBack (hooked at
// 0x7491C0). Every atomic without a custom renderCallBack dispatches here,
// which makes it the PED pipe group's render path: skinPipe atomics are
// recognised by `pipe == skinPipe` (the ambient override and dual pass
// below). The building and vehicle groups each own a Switch callback with a
// three-layer state entry contract; this one had none — see the ped entry
// block below.
RpAtomic*
myDefaultCallback(RpAtomic *atomic)
{
	RxPipeline *pipe;
	// Pre-seeded (never bare): a failed RwRenderStateGet leaves the out-param
	// untouched, and the dual-pass restore pushes these straight back — an
	// uninitialized func/ref must never reach the device. Same convention as
	// D3D9RenderDual / grassRenderCallback / CSkidmarks__Render.
	int zwrite = TRUE;
	int alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
	int alpharef = 1;
	int dodual = 0;
	int detach = 0;
	bool defContract = false;	// default-pipe entry contract armed (Spec 10 exit leg)

	// Atomic guard (audit reorder): every atomic deref below (atomic->pipeline
	// here, atomic->clump / Skin_ClassifyAtomic in the PedsPipe acquire) is now
	// covered by this early-out — the old `&& atomic` term in the acquire sat
	// AFTER the atomic->pipeline read it was meant to protect. NULL return
	// matches the `pipe ? atomic : NULL` exit contract.
	if(atomic == NULL)
		return NULL;

	pipe = atomic->pipeline;
	// Ped pipe group flag, captured BEFORE the NULL-pipe branch overwrites
	// `pipe` with the default pipeline. Scoped to peds only: this callback
	// also runs every other default-pipeline atomic (props, pickups, marker
	// objects), which must keep their current (unguarded) behaviour.
	bool isPedPipe = (pipe == skinPipe);
	// ===== PedsPipe Phase 2 wrap (design doc §2.1 wrap-first) =====
	// The 0x7491C0 hook STAYS in main.cpp; the module only supplies
	// per-clump state. Acquire + classify at ped-span entry: both no-op
	// while pedsPipeEnable=0 (single bool read) and Skin_ClassifyAtomic is
	// a Phase-1 stub (always false), so every ped block below keeps its
	// legacy statics/behaviour byte-identical until real classification
	// lands in skin.cpp. pedsPipeState is the span-wide
	// PedsPipe_GetState(atomic) read (AcquireState is idempotent and
	// returns the live slot value).
	PedPipeState *pedsPipeState = NULL;
	bool pedsPipeClassified = false;
	if(isPedPipe && PedsPipe_GetEnabled() && atomic){
		pedsPipeState = PedsPipe_AcquireState(atomic->clump);
		if(pedsPipeState){
			pedsPipeState->lastSeenFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
			pedsPipeClassified = Skin_ClassifyAtomic(atomic, pedsPipeState);
			// Design doc §7 Phase 1 acceptance probe: one-shot wrap
			// receipt correlating with the existing ped_opaque_pre.
			if(PedsPipe_ProbeOnce("peds_pipe_wrap"))
				dbglog("[PedsPipe] wrap active (atomic=%p clump=%p state=%p classified=%d slot=%d)",
					(void*)atomic, (void*)atomic->clump, (void*)pedsPipeState,
					(int)pedsPipeClassified, PedsPipe_GetSlotOffset());
		}
	}
	if(pipe == NULL){
		// NULL-pipe fallback (audit): guard the RW engine instance at
		// 0xC97B24 (same global texdefs.cpp polls before attach — NULL until
		// RwEngineInit completes) AND the resolved default pipeline. A NULL
		// after both attempts early-outs BEFORE the entry-contract writes
		// below: on this path nothing render-state has been touched yet (the
		// PedsPipe acquire above only writes its diagnostic struct), so the
		// plain NULL return is the same `pipe ? atomic : NULL` contract the
		// exit already uses — no restore needed.
		DWORD engine = *(DWORD*)0xC97B24;
		pipe = engine ? *(RxPipeline**)(engine + 0x3C + dword_C9BC60) : NULL;
		if(pipe == NULL)
			return NULL;
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwrite);
		if(zwrite && config->dualPassDefault)
			dodual = 1;
		// ===== DEFAULT-pipe entry contract =====
		// The default pipeline (props, pickups, marker objects) had NO
		// entry contract at all — unlike the ped contract below, the
		// building Switch and the vehicle Switch — so it drew with
		// whatever the previous pass leaked: a CCoronas::Render exit
		// leaves additive ONE/ONE + VERTEXALPHA TRUE + ZWRITE FALSE live
		// (vanilla restores nothing there), and these atomics ghosted
		// (additive opaque geometry, no depth writes). Force the canonical
		// world signature through ALL THREE layers (rw cache / RW driver
		// cache / raw device) BEFORE the draw, mirroring the ped entry
		// contract above. The zwrite read above deliberately stays FIRST:
		// the dual-pass gate must keep sampling the INHERITED state
		// (vanilla semantics), not the forced value.
		// COHERENT-PIN DOCTRINE (grey-puff upstream repair): every state
		// below goes through pipeDeviceStateSet — the shared 3-layer
		// chokepoint (pipelinecommon.cpp) that derives the L1 rw twin, the
		// L2 driver pending[] and the L3 raw device from the SAME value —
		// instead of the old hand-rolled L1 block + L2 block + a SEPARATE
		// raw L3 pin. That hand-rolled shape is how an L2=1/L3=0 split was
		// born here: the contract pinned the device raw while applied[]
		// still shadowed the pre-contract value, so a later cache-only
		// write of that old value value-compared equal against applied[]
		// and the applied[]-gated pre-draw flush SKIPPED — stranding the
		// raw-pinned device at blend OFF under a cache that says ON (the
		// exact "wrong device value survives the whole scene" mechanism
		// pipeDeviceStateSet's block comment documents). The single
		// _rwD3D9RenderStateFlushCache() after the writes commits
		// pending[]->applied[] while it holds the canonical values, so
		// the contract hands off pending==applied==device and every later
		// cache-only write becomes a REAL transition the flush must push.
		// No save/restore: the VALUE left behind is still the world's own
		// geometry-entry normal state (blend OFF — the opaque world; the
		// same canonical pipeEnterAlphaMode forces at every pipe entry,
		// unchanged by this repair), so leaving it behind is the repair,
		// not a leak. What this repair changes is the COHERENCE of that
		// handoff: the repair target is all three layers AGREEING, never
		// layered disagreement — a split is the defect class (grey puff
		// quads / grey-box cars / opaque sprites downstream), not a
		// resting state. BLENDOP and ALPHATESTENABLE have no rw render
		// state (same rule as pipeForceAlphaBlock) — the chokepoint rides
		// L2/L3 only for those. ALPHAFUNC/ALPHAREF DO have rw twins and
		// are forced through ALL THREE layers (fix-34): without them the
		// markers alpha-tested at whatever ref the previous pass inherited
		// (e.g. envmap.cpp:448's outdoor park at 140), discarding every
		// marker texel below the leaked ref.
		// ZFUNC has no rw render state (L2+L3 paired, same rule as
		// pipeForceAlphaBlock / pedOpaqueEnforce). Build-11 "props draw
		// over CJ": this contract forced ZTEST/ZENABLE but never ZFUNC, so
		// a postfx/DepthHook raw drift (pipelinecommon :1400 evidence
		// "ZFUNC drv=4/dev=8" — device at D3DCMP_ALWAYS) survived here and
		// every prop/markers draw after CJ passed its depth test over him.
		// (1)+(2)+(3) — one coherent chokepoint write per state:
		defContract = true;	// entry contract armed � exit leg publishes canonical normal-alpha
		pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE, FALSE);
		pipeDeviceStateSet(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		pipeDeviceStateSet(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		pipeDeviceStateSet(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		pipeDeviceStateSet(D3DRS_ZWRITEENABLE, TRUE);
		pipeDeviceStateSet(D3DRS_ZENABLE, TRUE);
		pipeDeviceStateSet(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		pipeDeviceStateSet(D3DRS_ALPHATESTENABLE, TRUE);
		pipeDeviceStateSet(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONGREATEREQUAL));
		pipeDeviceStateSet(D3DRS_ALPHAREF, 1);
		// Doctrine step: commit pending[]->applied[] NOW, while it holds
		// the canonical values (the pipeEnterAlphaMode / pipeApplyWorld-
		// Baseline flush idiom). Null-device safe: with no device the
		// chokepoint early-outs before even the L1 leg, so there is no
		// pending state to commit (the old code wrote L1/L2 unconditionally
		// and only guarded the raw pin — inert either way: this callback
		// only runs from a live draw, and a recreated device is re-pinned
		// by pipeApplyWorldBaseline("DeviceReset")).
		if(d3d9device)
			_rwD3D9RenderStateFlushCache();
		// One-shot throttled log tag "defpipe_contract".
		static bool defpipeLogged = false;
		if(!defpipeLogged && dbglog_throttle("defpipe_contract")){
			defpipeLogged = true;
			dbglog("[DefPipe] entry contract forced canonical world "
			       "signature (vtxalpha=0 src=SRCALPHA dst=INVSRCALPHA "
			       "op=ADD zwrite=1 ztest=1 zfunc=LE atest=1 afunc=GE "
			       "aref=1) via L1+L2+L3; victims if zfunc leaked: CJ "
			       "overdrawn by props/markers (draw-over-CJ)");
		}
	}else if(isPedPipe && ((config->dualPassPed && config->zwriteThresholdPed > 0) || (pedsPipeState && (pedsPipeState->dualPassMask & 1u)))){
		dodual = 1;
		// PedsPipe absorb (design doc §3.1 — SkinDrawDual gate): a
		// classified state vetoes the dual pass for atomics whose
		// dualPassMask bit0 is clear (bit set = needs the second leg).
		// When dualPassPed=0 (MODE_PS2 / MODE_GTAIV) the per-atomic
		// dualPassMask bit (set by Skin_ClassifyAtomic for every textured
		// atomic, head parts included — skin.h SKINDUAL_SECOND_LEG) keeps
		// the second leg for head parts; the config-only gate remains the
		// byte-identical fallback on the legacy (pedsPipeState == NULL)
		// rollback path, where the mask term is a constant FALSE.
		if(pedsPipeClassified && pedsPipeState && (pedsPipeState->dualPassMask & 1u) == 0)
			dodual = 0;
	}

	// ===== PED pipe group entry contract =====
	// Mirror of the building Switch (buildingPipe.cpp:1167-1204) and the
	// vehicle Switch (vehiclePipe.cpp:2385-2416): cull + alpha/blend + the
	// dual-layer ZWRITE force, all three layers (rw cache / driver pending[]
	// / device applied[]) per pipelinecommon.cpp:883. Until now the ped path
	// was the ONLY pipe group with no repair at all — it read and wrote
	// ZWRITE and alpha-test func+ref through the rw cache ONLY, so a DEVICE
	// that drifted behind an unchanged rw cache (postfx Save/RestoreRawGeomStates
	// raw writes, DepthHook raw ZENABLE, the fullscreen Im2D passes) stayed
	// wrong for the whole ped pass:
	//   rw ZWRITE=TRUE / dev ZWRITE=FALSE -> peds drew WITHOUT depth writes,
	//     the depth buffer never received the ped's depth, and everything
	//     drawn after peds (pickup marker/cone, effects, world geometry)
	//     depth-tested against a buffer with no peds in it: the marker
	//     painting in front of a ped's head, other pipes' geometry clipping
	//     through peds. Building/vehicle repair this at THEIR entry, so the
	//     hole only ever showed on the one pipe with no guard — the ped pipe.
	//   cull / alpha-test drift -> double-sided or wrongly-tested peds, plus
	//     the same stale state handed to the next consumer.
	//
	// Save/restore contract identical to the other two switches: force each
	// block through both layers for the duration of this cb; exit restores
	// each layer to its OWN pre-entry value (postfx manages the raw device
	// side itself with its own save/restore — syncing the sides here would
	// fight that contract).
	if(isPedPipe){
		// PedsPipe absorb point (design doc §3.1 — SkinEntryContract lane:
		// cull-mode A/B lever, pedAlphaTest A/B lever, sticky ZWRITE repair,
		// depth-trio force, opaque contract capture/enforce). Phase 3
		// FLIPPED: the module owns this span when pedsPipeState is non-NULL
		// (acquired above) — SkinEntryContract runs the whole absorbed chain
		// from skin.cpp, contract state in PedPipeState. pedsPipeState ==
		// NULL keeps the legacy inline block below byte-for-byte (the
		// pedsPipeEnable=0 rollback lever, skin.h:8 / peds_pipe.h:34).
		SkinEntryContract(pedsPipeState);

		// Stage-0 texture-raster resync: the skin pipeline's per-material
		// bind must be a real transition (cache<->device split repair).
		// INI pedTexResync (default 1 = ON): 0 = skip the park/set resync —
		// bisect lever for the ped stage-0 texture repair.
		// PedsPipe absorb point (design doc §3.1 — SkinStage0Resync):
		// Phase 3 FLIPPED — SkinStage0Resync (skin.cpp) owns the bind chain
		// + stage-0 latch bookkeeping when the module owns this span
		// (pedsPipeState non-NULL, acquired at entry — classification is
		// produced for exactly that case, so this is the same key the
		// pdsState ternary below uses). pedsPipeState == NULL keeps the
		// legacy inline helper call byte-for-byte (pedStage0Resync, NULL
		// state lane — the pedsPipeEnable=0 rollback lever, skin.h:8 /
		// peds_pipe.h:34).
		if(!config || config->pedTexResync){
			SkinStage0Resync(atomic, pedsPipeState);
		}
	}

	// ===== Default-callback shader hygiene =====
	// Shared by every atomic without a custom renderCallBack (peds, props,
	// pickups, marker objects). RW's D3D9Set*Shader are value-compare gated
	// against _rwD3D9Last*ShaderUsed (0x8E2448/0x8E244C): a stale PS/VS bound
	// by an earlier pass (postfx Im2D override, a pipe callback that
	// early-outs) can survive into a default-pipeline draw whose fixed-
	// function path never rebinds. Unbind through the RW setters (cache) AND
	// raw (device) so every layer agrees. Gated by defaultCbShaderHygiene.
	// One-shot log tag "defcb_shader" records the PRE-unbind device + RW-cache
	// pointers (Get* refs Released). POD locals only.
	if(config->defaultCbShaderHygiene){
		static bool defcbShaderLogged = false;
		// Unthrottled first-hit receipt: the "defcb_shader" throttle bucket
		// can be starved, which made the absence of a [DefCB] line ambiguous.
		static bool defcbShaderProbeLogged = false;
		if(!defcbShaderProbeLogged){
			defcbShaderProbeLogged = true;
			dbglog("[probe] defcb_shader path taken");
		}
		if(!defcbShaderLogged && dbglog_throttle("defcb_shader")){
			defcbShaderLogged = true;
			void *cacheVS = *(void**)0x8E2448;	// _rwD3D9LastVertexShaderUsed
			void *cachePS = *(void**)0x8E244C;	// _rwD3D9LastPixelShaderUsed
			IDirect3DVertexShader9 *devVS = NULL;
			IDirect3DPixelShader9 *devPS = NULL;
			if(d3d9device){
				d3d9device->GetVertexShader(&devVS);
				d3d9device->GetPixelShader(&devPS);
			}
			dbglog("[DefCB] shader hygiene: pre-unbind devVS=%p devPS=%p "
			       "cacheVS=%p cachePS=%p",
				(void*)devVS, (void*)devPS, cacheVS, cachePS);
			if(devVS) devVS->Release();
			if(devPS) devPS->Release();
		}
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
		if(d3d9device){
			d3d9device->SetPixelShader(NULL);
			d3d9device->SetVertexShader(NULL);
		}
	}else{
		// fix-34 always-on safety net: the INI lever above may stay OFF,
		// but a stale PS/VS bound by an earlier pass must never reach a
		// fixed-function default-pipe draw — correctness does not depend
		// on the lever. Cheap gate: two RW-cache dword reads (the same
		// value-compare globals the RW setters gate against); the unbind
		// (RW setters + raw device) only runs when a stale shader is
		// actually resident, so the steady-state cost is the reads.
		void *netCacheVS = *(void**)0x8E2448;	// _rwD3D9LastVertexShaderUsed
		void *netCachePS = *(void**)0x8E244C;	// _rwD3D9LastPixelShaderUsed
		if(netCacheVS || netCachePS){
			RwD3D9SetPixelShader(NULL);
			RwD3D9SetVertexShader(NULL);
			if(d3d9device){
				d3d9device->SetPixelShader(NULL);
				d3d9device->SetVertexShader(NULL);
			}
			if(dbglog_throttle("defcb_shader_net"))
				dbglog("[DefCB] safety net unbound stale shader "
				       "(cacheVS=%p cachePS=%p)", netCacheVS, netCachePS);
		}
	}

	// Override pAmbient with shared timecycle ambient for ped/skin rendering.
	// All pipelines (buildings, vehicles, peds) now use the same ambient source.
	// Sep-3 semantics (W3 revert — docs/plans/2026-10-30-sep3-correlation/
	// git-chars-lighting.md §4.2): plain GetTimecycleAmbient() — no max()
	// with the game's own ambient, no ambient-floor hue-preserving lift (the
	// pedAmbientFloor lever was removed 2026-11-10 - see wave-20-record §5),
	// no cutscene 0.15 special. The 0.20 floor saturated the FFP skin pipe
	// (final ≈ pAmbient + pDirect·NdotL) to flat white at noon.
	if(pipe == skinPipe && pAmbient){
		// PedsPipe absorb point (design doc §3.1 — SkinAmbientOverride):
		// Phase 3 FLIPPED — when the module owns this span (pedsPipeState
		// non-NULL, acquired at entry), SkinAmbientOverrideEnter runs the
		// saved-capture / black-guard pair from skin.cpp (state in
		// PedPipeState). pedsPipeState == NULL keeps the legacy inline
		// block below byte-for-byte (rollback lever). The ped_ambient
		// receipts stay on both paths (skin.cpp logs the same tags).
		SkinAmbientOverrideEnter(pedsPipeState);
	}

	// ===== DUAL PASS =====
	// PED (skin pipe): restore the ORIGINAL SkyGfx behaviour
	// (backup_original/src/main.cpp:422-436). The legacy callback ran the
	// two legs UNCONDITIONALLY whenever dualPassPed && zwriteThresholdPed>0:
	// leg 1 = GREATEREQUAL@threshold with ZWRITE ON (seeds depth at the
	// cutout edge), leg 2 = LESS@threshold with ZWRITE OFF (paints the
	// below-threshold fringe without disturbing that depth). It never asked
	// whether the atomic "has alpha": CJ head/hair/bandana materials are
	// alpha-255 over texture CUTOUT masks, so atomicHasAlpha() (colour +
	// prelit only) returned false and the hasAlpha/zwrite gate this replaces
	// starved the whole pass (field log: `[PED] dual pass skipped:
	// zwrite=1 hasAlpha=0` 9/9, zero "ran"). Keep that gate ONLY for the
	// default-pipe branch (pipe==NULL): default/vehicle/building dual-pass
	// behaviour is unchanged.
	if(dodual && !isPedPipe){
		int zwriteNow = TRUE;
		bool hasAlpha = atomicHasAlpha(atomic, false);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwriteNow);
		if(!zwriteNow || !hasAlpha){
			if(dbglog_throttle("ped_dual_skip"))
				dbglog("[PED] dual pass skipped: zwrite=%d hasAlpha=%d pipe=%p",
				       zwriteNow, (int)hasAlpha, (void*)pipe);
			dodual = 0;
		}
	}

	// S5 diagnostic: one-shot head-part (bandana/hair) texture receipt —
	// texname + TexAlpha class + attach frame (implemented in chars.cpp).
	// PedsPipe absorb point (design doc §3.1 — head-part receipt router):
	// Phase 3 FLIPPED — the module path routes the receipt through
	// SkinHeadPartReceipt (skin.cpp); pedsPipeState == NULL keeps the
	// legacy inline router byte-for-byte (the pedsPipeEnable=0 rollback
	// lever, skin.h:8 / peds_pipe.h:34). The receipt corpus is identical
	// on either leg.
	if(isPedPipe){
		SkinHeadPartReceipt(atomic);
	}

	// ===== ped draw-boundary cull re-assert (3-layer paired) =====
	// vehiclePipe.cpp:2389-2396 "Draw-boundary re-asserts" idiom, mirrored
	// for the ped span: every state-writing shield between the entry
	// contract and the draw (pipeEnterAlphaMode / pipeForceAlphaBlock
	// flushes, pedOpaqueEnforce, pedStage0Resync binds, the shader-hygiene
	// unbind, the chars receipt) is answered here on the LAST line before
	// RxPipelineExecute. Audit result: no shield/restore block in this
	// callback writes CULLMODE — but a foreign raw write mid-entry (the
	// Im2D/leftover class buildingPipe.cpp:2731-2735 and
	// vehiclePipe.cpp:2930-2940 re-assert past) would otherwise survive to
	// the draw: CULLNONE there = backfaces showing through skin
	// (angle-dependent see-through), CULLFRONT = inside-out ped.
	// Re-asserts the SAME mode the entry forced (pedCullDraw — CULLBACK in
	// the main view, the pedCullMode lever's override, or a foreign cull
	// scope's live mode), never a hardcoded BACK. Covers both dual-pass
	// legs: the leg states between them never touch cull.
	// Module path: the entry-forced value lives in PedPipeState
	// (contractCullDraw, written by SkinEntryContract); legacy path: the
	// local the inline entry filled.
	if(isPedPipe)
		pipeForceCullMode(pedsPipeState ? pedsPipeState->contractCullDraw
					    : (RwUInt32)rwCULLMODECULLBACK);
	if(dodual){
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&alphatest);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		// Sanitize the saved func/ref before the restore write (same rule as
		// D3D9RenderDual / CSkidmarks__Render): rw NA(0)/garbage must never
		// reach the device. The threshold is parse-clamped [0,255]; route it
		// through clampAlphaRef for the single bound (defense-in-depth).
		if(alphatest < rwALPHATESTFUNCTIONNEVER || alphatest > rwALPHATESTFUNCTIONALWAYS)
			alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
		alpharef = clampAlphaRef(alpharef);
		int zt = clampAlphaRef(config->zwriteThresholdPed);
		// zwriteThresholdPed, not zwriteThreshold: the ped key is parsed
		// (readIniFile, config.cpp) and menu'd (debugmenu_ui) but was never
		// consumed — peds silently shared the building/vehicle cut. Same
		// 0..255 clamp as its siblings, so the two are interchangeable
		// until the user actually moves the ped slider.
		// Two-layer leg states (see setAlphaTestTwoLayer): the old rw-only
		// writes let a drifted device run the same test for both legs.
		if(isPedPipe){
			// PedsPipe absorb point (design doc §3.1 — SkinDrawDual):
			// Phase 3 FLIPPED — SkinDrawDual (skin.cpp) owns the two-leg
			// draw + the leg-progression state lane when the module owns
			// this span (pedsPipeState non-NULL, acquired at entry).
			// The legacy inline ped block was deleted by the P5 shrink
			// (2026-11-15, commit 1f3352e) — module path is canonical.
			pipe = SkinDrawDual(pipe, atomic, pedsPipeState,
					    alphatest, alpharef, zt);
		}else{
			// Non-pipe-group (default/vehicle/building) dual pass —
			// RESTORED 2026-11-16: P5's brace surgery deleted this
			// two-leg block AND the !dodual single-execute else below,
			// so dodual==0 executed NOTHING (the :581 hasAlpha gate
			// clears dodual for opaque world geometry -> buildings/
			// roads vanished; peds stayed visible through SkinDrawDual).
			// Leg 1: GREATEREQUAL@zt seeds depth for cutout texels,
			// leg 2: LESS@zt draws the fringe (pre-P5 sequence).
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
			RxPipelineExecute(pipe, atomic, 1);
			setZWriteTwoLayer(FALSE);
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
			pipe = RxPipelineExecute(pipe, atomic, 1);
			setZWriteTwoLayer(TRUE);
			setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
		}
	}else
		pipe = RxPipelineExecute(pipe, atomic, 1);

	// ===== DEFAULT-pipe EXIT contract (Spec 10) =====
	// Publishes canonical normal-alpha (TRUE/SRCALPHA/INVSRCALPHA/ADD) through
	// L1+L2+L3 + flush, mirroring the sticky-exit doctrine
	// (pipelinecommon.cpp:2328-2347). Gated on defContract (only pipe==NULL
	// path sets it); ped atomics take SkinExitContract and never reach here.
	if(defContract){
		pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE, TRUE);
		pipeDeviceStateSet(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		pipeDeviceStateSet(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		pipeDeviceStateSet(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		if(d3d9device)
			_rwD3D9RenderStateFlushCache();
		static bool defpipeExitLogged = false;
		if(!defpipeExitLogged && dbglog_throttle("defpipe_contract_exit")){
			defpipeExitLogged = true;
			dbglog("[DefPipe] exit contract published canonical normal-alpha "
			       "(vtxalpha=1 src=SRCALPHA dst=INVSRCALPHA op=ADD) via L1+L2+L3 "
			       "+ flush; handoff to effects/sprite phase");
		}
	}

	// Restore original ambient after ped rendering — SkinAmbientOverrideExit
	// on the module path (re-entrant: no-op unless Enter ran this span),
	// legacy capture restore otherwise.
	if(pedsPipeState)
		SkinAmbientOverrideExit(pedsPipeState);

	// ===== PED pipe group exit contract — mirror of buildingPipe.cpp:1223-1227
	// and vehiclePipe.cpp:2480-2484. Also closes the dual pass above, which
	// leaves rw ZWRITE forced TRUE (and alpha func/ref at whatever the pass
	// last wrote) — each layer goes back to its own pre-entry value here, so
	// the ped pipe can no longer hand a forced/derived state to the next
	// consumer (Im2D marker passes, effects, the next pipe group).
	if(isPedPipe){
		// PedsPipe absorb point (design doc §3.1 — SkinExitContract lane):
		// Phase 3 FLIPPED — when the module owns this span (pedsPipeState
		// non-NULL), SkinExitContract runs the absorbed exit chain from
		// skin.cpp (pipeExitAlphaMode / sticky-or-restore ZWRITE /
		// pipeExitCullMode / pedOpaqueRestore-or-GE@1-reset / sticky-depth
		// repair — a divergent capture is NEVER re-emitted), state in
		// PedPipeState. pedsPipeState == NULL keeps the legacy inline
		// blocks below byte-for-byte (rollback lever). The [PedRS] exit
		// monitor is absorbed on its own dual-path flip (SkinExitMonitor)
		// right below and runs after either path.
		SkinExitContract(pedsPipeState);

		// ===== [PedRS] exit state monitor =====
		// PedsPipe absorb point (design doc §3.1 — exit [PedRS] monitor):
		// Phase 3 FLIPPED — the module path runs the 21-read triplet
		// monitor from skin.cpp (SkinExitMonitor; ped_rs receipt corpus
		// unchanged). pedsPipeState == NULL keeps the legacy inline
		// monitor below byte-for-byte (the pedsPipeEnable=0 rollback
		// lever, skin.h:8 / peds_pipe.h:34). It still runs after EITHER
		// exit contract (module or legacy), before vs_probe.
		SkinExitMonitor();
	}

	// FIX 3: sample the vertex stage at ped/default-pipe callback exit.
	vs_probe("ped_exit", "vs_probe_ped");
	return pipe ? atomic : NULL;
}
