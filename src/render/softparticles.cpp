// softparticles.cpp - SOFT PARTICLE SYSTEM (umbrella) - extraction 2026-11-28
// Owns: shared FX machinery - live render-prim gate, per-draw stage-0/blend pin,
//       fxSystem_c::Render hook (0x4A930A), splash-foam install.
// Family policy lives in siblings: smoke.cpp / fire.cpp / watersplash.cpp.
// Bodies are byte-identical moves from neopipe.cpp; receipts/hooks unchanged.
// Called once from injectDelayedPatches via hookSplashFoamFix (neopipe.h decl).
// 
#include "skygfx.h"
#include "neopipe.h"
#include <DirectXMath.h>
#include "MemoryMgr.h"
#include "d3d9.h"
#include "d3d9types.h"
#include <time.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <injector\injector.hpp>
#include <injector\hooking.hpp>
#include <injector\calling.hpp>
#include <injector\assembly.hpp>
#include <injector\utility.hpp>

// family predicates (siblings; local-extern idiom, no shared header)
bool IsWaterSplashSystem(void *sys);
bool IsWhiteSmokeSystem(void *sys);
bool IsFirePrim(BYTE *prim);

// ============================================================
// W2 splash-foam white blowout fix (finalgate-0042 findings W2).
//
// Root cause (disassembly + effects.fxp audit, 1.0 US):
//   * Every FX prim renders through the standard particle renderer
//     0x4A2C40 (the only render in the prim-class vtable 0x85A788; the
//     FLAT helper 0x4A1940 is dead code - its gate flag [prim+0x3d] has
//     no writer anywhere in the FX region, and its only call site
//     0x4A2CA2 sits behind an argument that is always 0 on the live
//     chain FxManager_c::Render 0x4A92A0 -> fxSystem_c::Render 0x4AA160,
//     which passes (0xA9AE00, 0, 0, 0) down to every prim render).
//   * Blend comes from the prim's own fxp bytes, read at 0x4A2D0B:
//     SRCBLENDID=4 (SRCALPHA) + DSTBLENDID=1 (ONE) for every water
//     splash prim. SRCALPHA/ONE is semi-additive (dst + src*src.a), so
//     overlapping sprites ACCUMULATE instead of blending.
//   * The swim-splash systems (water_splash_big/_splash/_sml/water_swim)
//     emit ~3 5x5 m "impact" sphere discs + ~100 0.5-2 m droplets per
//     burst, all at alpha 64; continuous swimming always keeps a fresh
//     burst alive, the overlaps saturate to a solid white dome over the
//     mod's brighter water (finalgate shots 7/8 - the small droplet
//     speckles at the dome crest were the sparse edge of the same cloud).
//   * See-through: FxManager_c::Render runs the whole particle batch
//     ZTEST on / ZWRITE off (0x4A92C2-0x4A92EA) and the translucent
//     water plane never writes Z, so from a submerged camera the discs
//     Z-test against the sea floor/sky and draw straight through the
//     surface (finalgate shot 7).
//
// Fix (system-level):
//   * Hook fxSystem_c::Render at its ONLY call site (0x4A930A, the
//     FxManager_c::Render loop - intercepted-call style, same as the
//     WaterDrops hooks below). A system counts as water splash iff any
//     of its prims binds one of the water-exclusive textures listed
//     below; the fxp audit shows no fire/explosion/spark system binds
//     any of them (their sphere/bullethitsmoke prims live in systems
//     that never bind a trigger texture).
//   * F4 (test2-0438) white smoke family: a SIBLING predicate matches the
//     saturating non-water white/grey smoke systems - vehicle exhaust and
//     tyre skid (prt_smoke_huge/prt_smokeII_3_expand/prt_collisionsmoke:
//     the wheel trio at 0x6AA86F/0x6AA87F/0x6AA8FA and the automobile
//     sites 0x6C942B..0x6CC0D4), muzzle/dust smoke (gunsmoke, heli_dust,
//     prt_wheeldirt, cigarette_smoke, riot_smoke) and the map smoke sheet
//     (WS_factorysmoke). NOTE: effects.fxp has NO system literally named
//     "exhaust"/"skid" - those producers are the prt_smoke_huge family
//     above (verified against all 82 fxp systems + all 59 named-create
//     sites in the exe). Fire-family textures are a HARD VETO so every
//     flame/spark system stays additive exactly as authored.
//   * Matched systems: every prim's dst blend byte is rewritten
//     ONE(1) -> INVSRCALPHA(5) around the original call (the bytes are
//     read inside the original at 0x4A2D25-0x4A2D50), giving proper
//     alpha blend - translucent streaks, no accumulation. Restore after
//     so a pooled/recycled prim object can never carry the rewrite into
//     a foreign blueprint. (Prims already authored INVSRCALPHA - the
//     prt_smoke_huge family - round-trip untouched: the rewrite only
//     flips byte==1.)
//   * Per-draw blend PIN (test2-0438 RCA-3a + F4): the game sets the
//     per-prim blend via RwRenderStateSet (0x4A2D0B) which NO-OPS when
//     the rw cache already holds the value - after the post-water/postfx
//     window leaves cache=(alpha pair) but device=(blend OFF/ONE), every
//     (5,6) request no-ops and the smoke/splash prims draw REPLACE
//     ("hard additive streaks", "solid white wedge"). Fire's (5,2) sets
//     transition and stay correct, which is why fire looked right. The
//     pin forces VERTEXALPHA=1 + SRCALPHA/INVSRCALPHA + BLENDOP=ADD on
//     all three layers around the matched draw and restores verbatim;
//     authored (5,2) prims inside a matched system still transition to
//     their own blend, so the pin can never de-addify a flame.
//   * Matched SPLASH systems are skipped entirely while the camera is
//     submerged (same 0.339731634 threshold as WaterDrops::NoDrops):
//     the surface foam stops drawing through the water plane. Only the
//     draw is gated - fxSystem_c::Render is draw-only (the particle
//     update runs elsewhere), so particles keep aging and dying. White
//     smoke is NOT underwater-gated (world smoke, not surface foam).
//   * WaterDrops (screen-space lens drops, drawn from
//     CMotionBlurStreaksRender) is a separate overlay - untouched here;
//     its own tail leak is paired in WaterDrops::Render below (cause #3).
//
// Crash hardening (RCA 0x4A8E49, post-screenshot systems-list crash):
//   * The fault was fxManager's list-REMOVE (0x4A8E49, node layout
//     +0=next/+4=prev, header 0xA9AE8C = fxManager 0xA9AE80+0xC = live
//     systems list) writing next->prev with a system whose ->next held
//     float-bits garbage (0x3F70ACE0 ~ 0.9688f) while ->prev stayed
//     valid. Systems-list links are written ONLY by INSERT/REMOVE/POPFN
//     with pointer values, so the header was overwritten by float data
//     from outside the list machinery - the particle-node data writes
//     (0x3C pool nodes, floats at +8..+0x27; a node at sys-8 covers
//     sys+0..0x1F) through a stale pool pointer, i.e. game-side FX
//     memory reuse. None of this hook's writes can reach a system
//     header (verified: single render-prim class, vtable 0x859B88 - the
//     only vtable in the image whose +0x14 slot is the renderer
//     0x4A2C40; prim+5/6/7 are standalone byte fields, packed from fxp
//     floats at 0x4A673F/0x4A6747/0x4A68CF).
//   * Residual hook-side hazard closed anyway: the rewrite/restore
//     dereferenced primList[i] with NO identity check, so a recycled or
//     foreign object in the array would have taken our byte write.
//     Every prim touch is now gated on the vtable signature
//     (IsFxRenderPrim below) - a freed/recycled prim fails the check
//     and is skipped, so the hook is structurally unable to write into
//     memory that is not a live render prim. Splash/smoke behaviour is
//     unchanged: every live prim passes the check.
// ============================================================

// Signature gate: the render-prim class vtable (0x859B88, sole +0x14 ->
// renderer 0x4A2C40 in the whole image). Anything else in primList[] is
// not a live render prim and must not be read as one or written to.

const void *const FX_RENDER_PRIM_VTABLE = (void *)0x859B88;

bool
IsFxRenderPrim(void *prim)
{
	return prim && *(void **)prim == FX_RENDER_PRIM_VTABLE;
}

// Per-draw FX draw-state pin (test2-0438 RCA-3a/F4 + font-safety).
//
// The pin frames EVERY fxSystem_c::Render call we wrap (matched or not) with
// a three-layer capture and a verbatim restore of BOTH:
//   (a) the stage-0 TEXTURERASTER block          -> fx_s0leak receipt
//   (b) the alpha / blend block                  -> fx_blendleak receipt
//
// Why stage 0 must be framed by the hook (not just by matched systems):
// the fx prim renderer (0x4A2C40) binds its sprite texture through raw
// RwD3D9SetTexture / device paths, so the rw cache keeps whatever raster it
// held on entry while the DEVICE ends the draw holding the last particle
// texture. The next FFP consumer (font/HUD quads) then calls
// RwRenderStateSet(rwRENDERSTATETEXTURERASTER, <that same entry raster>):
// the cache comparison in RwRenderStateSet sees a HIT, the set no-ops, and
// every glyph samples the last smoke/splash texture. That is the exact
// "fonts completely broken" class already fixed in postfx.cpp's mb_s0leak
// pass and chars.cpp's RestoreSSSStage0AndSampler.
//
// Matched systems additionally get the canonical alpha pair FORCED for the
// duration of the draw (forceAlpha). The game's per-prim RwRenderStateSet
// (0x4A2D0B) no-ops when the rw cache already holds the value, so after the
// post-water/postfx window leaves cache=(alpha pair) but device=(blend
// OFF/ONE) every (5,6) request no-ops and the smoke/splash prims draw
// REPLACE. Authored (5,2) prims still transition to their own pair, so the
// pin can never de-addify a flame. Non-matched systems are state-neutral:
// captured and restored, never forced.
//
// Exit restores every captured layer (rw cache / RW driver pending / raw
// device) verbatim AND runs unconditionally - the caller guards the original
// call so a fault cannot skip it. POD-only (C2712-safe: the hook body owns
// __try blocks).
struct FxDrawPin {
	// layer 1 - rw cache
	RwUInt32 vtxRw, srcRw, dstRw;
	RwRaster *rasRw;
	// layer 2 - driver pending
	DWORD vtxDrv, srcDrv, dstDrv, opDrv;
	// layer 3 - raw device
	DWORD vtxDev, srcDev, dstDev, opDev;
};

// Resolve an rw raster to its D3D texture. Borrowed pointer, NO AddRef
// (the proven 0xB4E9E0 extension extraction used by pedStage0Resync,
IDirect3DTexture9 *
fxRasterToTex(RwRaster *ras)
{
	if(!ras)
		return NULL;
	DWORD extOff = *(DWORD*)0xB4E9E0;
	if(extOff == 0 || extOff >= 0x100000)
		return NULL;
	IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((BYTE*)ras + extOff);
	if(!cand || (uintptr_t)cand < 0x10000u || ((uintptr_t)cand & 3u))
		return NULL;
	return cand;
}

void
FxDrawPinEnter(FxDrawPin *p, bool forceAlpha)
{
	p->vtxRw = TRUE;
	p->srcRw = rwBLENDSRCALPHA;
	p->dstRw = rwBLENDINVSRCALPHA;
	p->rasRw = NULL;
	p->vtxDrv = TRUE; p->srcDrv = D3DBLEND_SRCALPHA;
	p->dstDrv = D3DBLEND_INVSRCALPHA; p->opDrv = D3DBLENDOP_ADD;
	p->vtxDev = p->vtxDrv; p->srcDev = p->srcDrv;
	p->dstDev = p->dstDrv; p->opDev = p->opDrv;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &p->vtxRw);
	if(p->vtxRw == 0xFFFFFFFF)	// failed-Get sentinel (see pedStage0Resync)
		p->vtxRw = TRUE;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &p->srcRw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &p->dstRw);
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &p->rasRw);
	if((uintptr_t)p->rasRw == (uintptr_t)0xFFFFFFFF)	// failed-Get sentinel
		p->rasRw = NULL;
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &p->vtxDrv);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &p->srcDrv);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &p->dstDrv);
	RwD3D9GetRenderState(D3DRS_BLENDOP, &p->opDrv);
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &p->vtxDev);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &p->srcDev);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &p->dstDev);
		d3d9device->GetRenderState(D3DRS_BLENDOP, &p->opDev);
	}

	if(!forceAlpha)
		return;

	// Pin: canonical alpha on all three layers. L1 is what the game's own
	// per-prim RwRenderStateSet reads against (0x4A2D0B no-ops when the
	// cache already holds the value); L2/L3 make the DEVICE obey even when
	// L1 no-ops. Authored (5,2) prims still transition to their own pair.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	}
}

void
FxDrawPinExit(FxDrawPin *p)
{
	// --- blend block: rw cache -> driver pending -> raw device -------------
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)p->vtxRw);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)p->srcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)p->dstRw);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, p->vtxDrv);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, p->srcDrv);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, p->dstDrv);
	RwD3D9SetRenderState(D3DRS_BLENDOP, p->opDrv);

	// --- stage 0: force the rw cache transition, then raw-pin the device ---
	// A plain RwRenderStateSet(entry raster) no-ops whenever the cache still
	// holds that raster (the raw fx binds never moved it), leaving the device
	// on the last particle texture. Park on NULL first so the restore is a
	// guaranteed cache miss -> real device bind. When the entry raster is
	// NULL both L1 writes are no-ops, so the raw clear below is the ONLY
	// thing that can unbind a foreign particle texture - it must run.
	if(p->rasRw){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)p->rasRw);
		// L3 raw pin of that raster's resolved texture (authoritative layer).
		// When the extraction fails the park-and-set above has already forced
		// the bind, so there is nothing more to pin.
		if(d3d9device){
			IDirect3DTexture9 *tex = fxRasterToTex(p->rasRw);
			if(tex)
				d3d9device->SetTexture(0, tex);
		}
	}else if(d3d9device){
		d3d9device->SetTexture(0, NULL);
	}

	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, p->vtxDev);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, p->srcDev);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, p->dstDev);
		d3d9device->SetRenderState(D3DRS_BLENDOP, p->opDev);
	}

	// --- receipts ----------------------------------------------------------
	// Sampled at most once per DBGLOG_THROTTLE_MS: the stage-0 comparison
	// needs a device read (GetTexture = AddRef) and must not run per draw.
	// A mismatch means this hook left the frame-boundary state different
	// from what it found - the exact condition that makes the following
	// font/HUD quads sample a foreign texture or draw with blend OFF.
	if(d3d9device && g_diagLevel < 2){
		static DWORD s_fxPinReceiptTick = 0;
		DWORD now = GetTickCount();
		if(now - s_fxPinReceiptTick >= (DWORD)DBGLOG_THROTTLE_MS){
			s_fxPinReceiptTick = now;

			RwRaster *exitRas = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &exitRas);
			if((uintptr_t)exitRas == (uintptr_t)0xFFFFFFFF)
				exitRas = NULL;
			// Expected device bind = the entry raster's own texture (or NULL
			// when the entry had no raster). Resolved without AddRef via the
			// 0xB4E9E0 extraction; when it cannot be resolved only the rw
			// layer is compared (no false positives).
			IDirect3DTexture9 *expectTex = p->rasRw ? fxRasterToTex(p->rasRw) : NULL;
			bool s0Bad = (exitRas != p->rasRw);
			IDirect3DBaseTexture9 *exitDev = NULL;
			bool devRead = false;
			if(!s0Bad && (!p->rasRw || expectTex)){
				d3d9device->GetTexture(0, &exitDev);	// AddRef - released below
				devRead = true;
				if(exitDev != (IDirect3DBaseTexture9*)expectTex)
					s0Bad = true;
			}
			if(s0Bad){
				if(dbglog_throttle("fx_s0leak"))
					dbglog("[fx_s0leak] fx render left stage0 raster=%p devTex=%p%s but entry was raster=%p tex=%p "
					       "(incomplete restore -> following font/HUD quads may sample a foreign texture)",
						(void*)exitRas, (void*)exitDev, devRead ? "" : " (not sampled)",
						(void*)p->rasRw, (void*)expectTex);
			}
			if(exitDev)
				exitDev->Release();

			DWORD eAb = 0, eSrc = 0, eDst = 0, eOp = 0;
			RwUInt32 rVtx = 0, rSrc = 0, rDst = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &eAb);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &eSrc);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &eDst);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &eOp);
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &rVtx);
			RwRenderStateGet(rwRENDERSTATESRCBLEND, &rSrc);
			RwRenderStateGet(rwRENDERSTATEDESTBLEND, &rDst);
			if(eAb != p->vtxDev || eSrc != p->srcDev || eDst != p->dstDev || eOp != p->opDev ||
			   rVtx != p->vtxRw || rSrc != p->srcRw || rDst != p->dstRw){
				if(dbglog_throttle("fx_blendleak"))
					dbglog("[fx_blendleak] fx render left blend rw(vtx=%u src=%u dst=%u) dev(ab=%u src=%u dst=%u op=%u) "
					       "but entry was rw(vtx=%u src=%u dst=%u) dev(ab=%u src=%u dst=%u op=%u)",
						rVtx, rSrc, rDst, eAb, eSrc, eDst, eOp,
						p->vtxRw, p->srcRw, p->dstRw, p->vtxDev, p->srcDev, p->dstDev, p->opDev);
			}
		}
	}
}

static injector::hook_back<void(__fastcall*)(void* _this, int edx, int a1, int a2, int a3)>
	s_fxSysRender;

void __fastcall
FxSysRenderHook(void* _this, int edx, int a1, int a2, int a3)
{
	// POD locals only - SEH fence (C2712-safe), same doctrine as the
	// chars.cpp/postfx passes: a fault in the identification or the byte
	// rewrite must degrade to a vanilla pass-through, never skip the
	// restore of bytes already flipped.
	bool splash = false;
	bool smoke = false;
	bool submerged = false;
	bool pinAlpha = false;
	bool callFaulted = false;
	DWORD callFaultCode = 0;
	signed char primCount = 0;
	void **primList = NULL;
	unsigned rewriteMask = 0;
	int rewritten = 0;
	FxDrawPin pinState;

	__try{
		splash = IsWaterSplashSystem(_this);
		smoke = !splash && IsWhiteSmokeSystem(_this);
		if(splash || smoke){
			if(splash && CWeather__UnderWaterness > 0.339731634f){
				submerged = true;	// draw gate; nothing flipped
			}else{
				primCount = *(signed char*)((char*)_this + 0x1b);
				primList = *(void***)((char*)_this + 0x1c);
				if(primCount > 0 && primList){
					for(int i = 0; i < primCount && i < 32; i++){
						BYTE *prim = (BYTE*)primList[i];
						// vtable gate: only a live render prim's byte 6 is
						// the dst blend ID; anything else must not be
						// touched (recycled/foreign object protection).
						// Per-prim fire veto: a matched gunshot system also
						// carries sphere (muzzle flash) prims — those stay
						// additive (dst ONE), only the smoke prims flip.
						if(IsFxRenderPrim(prim) && !IsFirePrim(prim) && prim[6] == 1){
							prim[6] = 5;	// rwBLENDONE -> rwBLENDINVSRCALPHA (LUT 0x8A6230)
							rewriteMask |= (1u << i);
							rewritten++;
						}
					}
				}
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// Keep rewriteMask/primList/primCount exactly as they were: every
		// byte counted in the mask was already flipped and MUST be restored.
		// splash/smoke stay true so the restore path below still runs.
	}

	if(splash && submerged){
		// Log-hygiene (2026-11-02): recurring per-frame receipt ->
		// g_diagLevel>=1 only; the first hit is always logged (one-shot).
		if(dbglog_once("splashfoam_gate_first") ||
		   (g_diagLevel >= 1 && dbglog_throttle("splashfoam_gate")))
			dbglog("[splashfoam] underwater gate: splash system render skipped (uw=%.2f)",
				CWeather__UnderWaterness);
		return;
	}

	// Per-draw state pin: EVERY wrapped draw gets the three-layer stage-0 +
	// blend capture/restore (font-safety - the raw texture bind left behind
	// by the prim renderer makes the next font/HUD stage-0 set a cache hit
	// that no-ops, so the glyphs sample the last particle texture).
	// Matched draws ADDITIONALLY get the canonical alpha block forced, since
	// the game's per-prim RwRenderStateSet no-ops against a matching cache
	// and a split leaves the smoke/splash prims drawing REPLACE. The exit is
	// unconditional (also on the fault path below) - the pin must be
	// invisible to every later consumer.
	pinAlpha = (splash || smoke) && !submerged;
	FxDrawPinEnter(&pinState, pinAlpha);

	// Fault fence around the original: a fault inside the game's render must
	// not strand the pin (blend forced ON, stage-0 parked) for the rest of
	// the frame - that is the state that breaks every later FFP draw. Same
	// doctrine as RenderReflectionScene/WaterDrops::Render: degrade to a
	// pass-through, always run the restore.
	__try{
		s_fxSysRender.fun(_this, edx, a1, a2, a3);
	}__except(EXCEPTION_EXECUTE_HANDLER){
		callFaulted = true;
		callFaultCode = GetExceptionCode();
	}

	FxDrawPinExit(&pinState);

	if(rewriteMask){
		for(int i = 0; i < primCount && i < 32; i++){
			if(!(rewriteMask & (1u << i)))
				continue;
			// Per-prim fence: one bad/unmapped entry must never strand the
			// remaining flipped bytes (a half-restored primList would leave
			// dst=INVSRCALPHA on prims whose blueprint authored ONE). The
			// same __try also covers the primList[i] read: if the array was
			// freed during the draw, the next iteration re-faults and is
			// skipped, instead of abandoning the whole loop.
			__try{
				BYTE *prim = (BYTE*)primList[i];
				// Re-verify the signature AFTER the original call: if the
				// prim was freed/recycled during the draw, its first dword
				// no longer holds the render-prim vtable and writing our
				// byte back would corrupt the new occupant. Skipping the
				// restore in that case is correct - the object is no
				// longer the prim we flipped.
				if(IsFxRenderPrim(prim) && prim[6] == 5)
					prim[6] = 1;
			}__except(EXCEPTION_EXECUTE_HANDLER){
				if(dbglog_throttle("fx_primrestore"))
					dbglog("[splashfoam] prim[%d] restore faulted code=0x%08X - byte left at 5, "
					       "system re-seeds from its blueprint on next init",
						i, GetExceptionCode());
			}
		}
	}

	if(callFaulted && dbglog_throttle("fx_renderfault"))
		dbglog("[splashfoam] fxSystem_c::Render FAULT code=0x%08X - draw aborted, "
		       "pin restored (stage0 + blend), %d prim byte(s) restored",
			callFaultCode, rewritten);

	// Log-hygiene (2026-11-02): recurring per-draw receipt (splash +
	// white-smoke pin lines) -> g_diagLevel>=1 only; the first hit is
	// always logged (one-shot).
	if(pinAlpha &&
	   (dbglog_once("splashfoam_fix_first") ||
	    (g_diagLevel >= 1 && dbglog_throttle("splashfoam_fix"))))
		dbglog("[splashfoam] %s render: %d prim(s) dst ONE->INVSRCALPHA, blend pin ON for this draw",
			smoke ? "white-smoke system" : "splash system", rewritten);
}


void
hookSplashFoamFix(void)
{
	s_fxSysRender.fun = injector::MakeCALL(0x4A930A,
		static_cast<void(__fastcall*)(void*, int, int, int, int)>(FxSysRenderHook), true).get();
	dbglog("[splashfoam] fxSystem_c::Render hooked at call site 0x4A930A (orig=%p) - "
	       "water-splash + white-smoke dst blend ONE->INVSRCALPHA, per-draw blend pin, "
	       "per-draw stage-0 + blend capture/restore on every wrapped draw, "
	       "underwater splash draw gate",
		(void*)s_fxSysRender.fun);
}
