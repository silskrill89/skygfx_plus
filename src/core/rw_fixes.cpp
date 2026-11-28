// rw_fixes.cpp — RenderWare-level FIX machinery (render-state / byte-code
// shields) extracted from main.cpp (2026-11-09). Bodies byte-identical to the
// pre-extraction main.cpp; the split lets runtime attribution separate "a
// fixer broke it" from "a feature broke it". Owns:
//   * the corona/sprite flush shield: coronaDrawRawShield, SpriteFlushState,
//     spriteFlushShieldCapture/Close, spriteEnforceDepthContract,
//     SpriteFlushIndexedDraw_hook, CPointLights__Render_shield_hook,
//     coronaFamilyTexture / flushBatchIdentity, PostFxFlush_701FE9_hook,
//     CloudRegionFlush_7273D0_hook, CoronaWindow_Close
//   * CCoronas::Render entry blend shield (CCoronas__Render_blendprobe_hook)
//   * rwFixes_installHooks(void) — byte-verified installs, gated by
//     Config.rwFixesEnable (default 1). 0 = install none of the above.
// Called once from main.cpp DllMain at the original install position.
#include "skygfx.h"
#include "transparency.h"
#include <injector\hooking.hpp>
#include <excpt.h>
#include <string.h>

// ============================================================================
// SUBSYSTEM: CORONA / SPRITE-STATE WINDOW   (extraction: weather/corona.cpp)
// ============================================================================
// Scope-isolated for a mechanical later extraction. Everything below through
// CoronaWindow_Close, plus CCoronas__Render_blendprobe_hook and the install
// sites named at the bottom, belongs to this subsystem. Do not add unrelated
// code inside this region.
//
// GLOBALS (file-scope POD, render thread only):
//   pointLights             live CPointLights entry base. Read through the
//                           immediate of the game's own `lea eax,[ecx+0xC3F0E0]`
//                           at 0x7001FD (bytes 8D 81 E0 F0 C3 00, 1.0 US), so
//                           an array-moving limit adjuster is followed.
//   g_inCoronasRender       window open: producer entry .. first post-body flush
//   g_coronaBodyDone        body finished; the next buffered flush closes it
//   g_inCloudsRender        producer flag: flush picks ONE/ONE for CClouds
//   g_inPointLightsRender   body-scoped CPointLights gate leg: hook entry ..
//                           first post-body flush (covers the mid-body
//                           overflow flushes the g_coronaBodyDone close would
//                           otherwise drop, PLUS a SilentPatch-deferred batch
//                           that rasterises after orig returned — FIX A2)
//   g_pointLightBodyDone    FIX A2 hand-off: set at CPointLights body exit,
//                           consumed by the first non-empty flush after it
//                           (or by CoronaWindow_Close at the frame boundary)
//   g_coronaPreValid        pre-window capture completed this window
//   g_coronaPreFuncRw/RefRw, g_coronaPreFuncDev/RefDev
//                           pre-window ALPHAFUNC/ALPHAREF (L1 + L3 snapshots)
//   g_coronaPreSepAlpha, g_coronaPreColorWrite
//                           pre-window raw-only state snapshots
//   g_coronaPreBlend/SrcBlend/DstBlend/BlendOp/ZWrite
//                           pre-window blend block (device snapshot)
//   currentLight, stkp      asm ABI carriers for the 0x700B6B wrapper
//
// ENTRY POINTS:
//   coronaRwBlendFromD3D()                 D3DBLEND -> rwBlend mapping
//   coronaDrawRawShield()                  per-draw park-and-set, L1+L2+L3
//   SpriteFlushIndexedDraw_hook()          flush draw entries 0x70CF62/0x70CF9B;
//                                          owns force-shield + window close
//   CPointLights__Render_shield_hook()     re-arms at 0x53E21D and 0x53DE6B
//   CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect()
//                                          0x700B6B point-light colour fix
//   CCoronas__Render_blendprobe_hook()     0x6FAEC0 entry/exit shield (defined
//                                          later in the "CCoronas::Render
//                                          entry blend shield" region)
//   CoronaWindow_Close()                   frame-boundary safety close
//
// INVARIANTS:
//   * Window opens at producer entry, closes at the FIRST post-body flush
//     (g_coronaBodyDone), never at producer exit. SilentPatchSA defers the
//     corona batch past CCoronas::Render, so the window must survive the return.
//   * Every draw inside the window is repaired by coronaDrawRawShield
//     immediately before rasterisation; the buffered flush forces the block
//     for a non-empty batch owned by a corona/pointlight or cloud producer,
//     and spriteFlushShieldClose() replays the captured pre-force state
//     right after that draw.
//   * The close runs the canonical world resync (blend ON, SRCALPHA/
//     INVSRCALPHA, BLENDOP ADD, ZWRITE/ZTEST ON) through L1+L2+L3.
//   * Flags are POD and unconditionally re-armed at each producer entry; a
//     fault path therefore leaves a bounded state that the next flush or
//     CoronaWindow_Close clears.
//
// FUTURE weather/corona.cpp PUBLIC API (exact exports):
//   void CoronaWindow_Close(void);
//   void CoronaWindow_InstallHooks(void);               // NEW: owns installs
//   void CoronaWindow_SetCloudProducer(bool inClouds);  // NEW: CClouds hygiene
//   void CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect(...);
//
// MOVES TOGETHER (copy-paste set):
//   src/core/main.cpp   this region (PointLight .. CoronaWindow_Close)
//   src/core/main.cpp   CCoronas__Render_blendprobe_hook + trampoline/orig
//   src/core/main.cpp   installs: 0x6FAEC0 entry, 0x70CF62/0x70CF9B flush,
//                       0x53E21D/0x53DE6B CPointLights, 0x700B6B CSprite
//   src/skygfx.h        CoronaWindow_Close declaration
//   src/core/main_exports.h  CSprite wrapper declaration
//   Consumers stay put: depthhook.cpp CoronaWindow_Close call; main.cpp
//   RenderScene_before / effects-exit CoronaWindow_Close calls.
// ============================================================================
struct PointLight
{
	RwV3d pos;
	RwV3d dir;
	float radius;
	float color[3];
	void *attachedTo;
	char type;
	char fogType;
	char generateExtraShadows;
	char pad;
};

// Point-light base is read from the game's own instruction immediate, not a
// hardcoded address: 0x7001FD is `lea eax,[ecx+0xC3F0E0]` (bytes
// 8D 81 E0 F0 C3 00 in gta_sa.exe 1.0 US); the imm32 lives at 0x7001FF. A
// limit adjuster that moves the array patches that immediate, so binding the
// reference to the operand follows the moved base automatically.
PointLight *&pointLights = *(PointLight**)(0x7001FD + 2);

// ---- Corona draw-time raw shield --------------------------------------
// Scope flag: TRUE from the top of CCoronas__Render_blendprobe_hook until
// the buffered-flush that rasterises the corona batch. With vanilla RW the
// flush runs as CCoronas::Render's tail (0x6FB621 tail-jmp -> 0x70CF20),
// i.e. inside this window; with SilentPatchSA loaded that tail-jmp is
// word-patched to a RET (SP init: mov word [0x6FB621],0xE8C3) so the batch
// persists and is drawn by the NEXT sprite consumer's flush — AFTER this
// hook returned. The window therefore CLOSES in SpriteFlushIndexedDraw_hook
// (first flush after the body — g_coronaBodyDone), NOT at Render exit.
// Draw entries below raw-force the corona additive block (blend enable /
// ONE/ONE / ZWRITE off) plus ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/
// COLORWRITEENABLE when the flag is set. The buffered-sprite flush forces
// the ONE/ONE additive block for EVERY non-empty batch (force=true) so
// sprites produced outside the window — the early CPointLights::Render call
// 0x53DE6B — are covered, including the SilentPatchSA-deferred batch whose
// shield window is already closed when the real rasterisation happens.
// Every forced batch is closed immediately after its draw by
// spriteFlushShieldClose(), so the force never survives its own raster.
bool g_inCoronasRender = false;	// SHARED: read by main.cpp effectsForceAlphaBlock (extern in main.cpp)
// TRUE from the END of the CCoronas::Render trampoline body (or from the
// START of the CPointLights body — its tail flush runs inside its own orig
// call) until the first buffered-flush afterwards; the flush hook uses it to
// close the window. Remains FALSE if the trampoline faults — see the flush
// hook.
static bool g_coronaBodyDone = false;
// Producer flag for the flush-boundary shield: TRUE only while the
// CClouds::Render original is on the stack. CClouds natively draws its
// cloud sprites ADDITIVELY (ONE/ONE) through the SAME RW TempBuffer flush
// the corona/pointlight path uses. Both producers now force the same
// ONE/ONE pair; this flag only selects the close-receipt label
// ("/clouds" vs "/corona") and documents which producer owned the batch.
// POD, render-thread only; armed/cleared in CClouds__Render_hygiene.
bool g_inCloudsRender = false;	// SHARED: set by main.cpp CClouds__Render_hygiene (extern in main.cpp)
// ---- FIX A: body-scoped CPointLights gate leg (street-lamp fog cones) ----
// DEFECT (Nov-25 01:04 street batch, build 4eb4361): the fog-cone batch
// fails the reconcile gate (family=0 window=0, ras=05189E70 — the cone
// texture is dynamic, NOT one of the four coronaFamilyTexture statics) and
// keeps the world's normal-alpha -> the additive-authored glow texture
// (black RGB x a~0.8) REPLACES the background -> black pyramids.
// WHY window=0 inside the producer: CPointLights__Render_shield_hook arms
// g_inCoronasRender/g_coronaBodyDone BEFORE orig (see below), so the FIRST
// flush of the body — an appender-overflow flush; RenderFogEffect emits
// hundreds of cone/dot quads into the 384-sprite batch already holding
// SilentPatch's deferred corona quads — consumes g_coronaBodyDone and runs
// the canonical 5/6 resync MID-BODY (docs/plans/2026-10-26-sdk-
// compare-sprites.md:141/274). Every cone appended after that flush
// rasterises at a later body flush with window=0 -> fail open.
// This leg spans the WHOLE orig call instead: set at hook entry, cleared
// right after orig returns. Safe because every flush during the body drains
// the shared sprite batch (0xC6A158) whose only producers are CCoronas/
// CPointLights/CClouds — all additive-authored; the font/HUD lanes flush
// through 0x7273D0/0x701FE9, never through 0x70CF20. Cones cannot outlive
// the body: the CPointLights tail is an unconditional jmp into the shared
// flush (0x700BCD, see the shield-hook block below), so every cone quad
// drains inside orig.
// consulted ONLY by spriteBlendContract() on the spriteFlushShield=0 path
// — the shield=1 force/close code and effectsForceAlphaBlock (reads
// g_inCoronasRender only) are untouched. POD, render-thread only.
static bool g_inPointLightsRender = false;
// ---- FIX A2: DEFERRED close for the body-scoped CPointLights leg --------
// DEFECT (Nov-26 field log, both sessions): the leg above is cleared at
// CPointLights__Render_shield_hook BODY EXIT, while its sibling
// g_inCoronasRender/g_coronaBodyDone are cleared by the FIRST FLUSH that
// rasterises the batch (SpriteFlushIndexedDraw_hook, "Window close" below).
// That asymmetry is the deferral hole SilentPatchSA creates: the producer
// queues the shared TempBuffer batch and returns, so a cone/dot batch can
// reach 0x70CF20 only AFTER orig has already run the post-call clear. The
// flush then evaluates family=0 (the cone/dot texture is dynamic, not one
// of the four coronaFamilyTexture statics) AND window=0 -> skip-non-glow,
// fail open, authored normal-alpha kept -> the additive-authored glow
// texture (opaque alpha, non-white RGB surround) REPLACES the background as
// an opaque hard-edged quad. Field receipts for exactly that state:
//   `[PtLightGlow] root-cause skip-non-glow: flush entry blend=1 src=5
//    dst=6 op=1 family=0 window=0 ras=…` (skygfx_dbg.log / .prev.log).
// FIX: hand the leg over to the FLUSH instead of dropping it at body exit —
// set at hook entry (with the re-arm), flagged done at body exit, and
// cleared by the FIRST flush after that (or by CoronaWindow_Close at the
// frame boundary). Mid-body flushes are unaffected because g_pointLightBodyDone
// is still false there, so FIX A's whole-body coverage is preserved.
// Blast radius is exactly one flush carrying a NON-EMPTY shared batch
// (glowContractPending already requires spriteBatch>0), whose only producers
// this file documents as CCoronas/CPointLights/CClouds — all additive.
// The texture-family list is NOT widened, so glyph/font/HUD/screen-blit
// lanes keep their authored normal-alpha. POD, render-thread only.
static bool g_pointLightBodyDone = false;

// ---- Pre-window state captured at the CCoronas hook entry ---------------
// The entry "normalize" block used to force ALPHAFUNC=GREATEREQUAL /
// ALPHAREF=0 / SEPARATEALPHABLENDENABLE=0 / COLORWRITEENABLE=0xF ONCE per
// window and never restore them, leaking GREATER-EQUAL@0, a blanking colour
// mask and separate-alpha blend into the rest of the frame. Those writes now
// run per-draw inside coronaDrawRawShield (immediately before each
// rasterisation), and the PRE-WINDOW values are captured here at the
// CCoronas::Render entry, then put back at the hook exit and by
// CoronaWindow_Close (frame-boundary safety net). func/ref are captured on
// both layers: rw cache (L1 source) and device (L2/L3 source); the two
// raw-only states have no rw render state at all, so the device value is
// their only snapshot. All POD file-scope statics — C2712-safe.
static bool  g_coronaPreValid = false;	// capture completed this window
static int   g_coronaPreFuncRw = rwALPHATESTFUNCTIONGREATEREQUAL;
static int   g_coronaPreRefRw = 0;
static DWORD g_coronaPreFuncDev = (DWORD)D3DCMP_GREATEREQUAL;
static DWORD g_coronaPreRefDev = 0;
static DWORD g_coronaPreSepAlpha = 0;
static DWORD g_coronaPreColorWrite = 0xF;
// Pre-window BLEND block (device values captured at the CCoronas::Render
// entry, before the entry shield / per-draw shield overwrite them). The
// exit restore used to hard-set ALPHABLENDENABLE=FALSE (REPLACE), which is
// what manufactured the next frame's blend-off black quads; it now puts
// back the value the device actually held when the window opened. SRC/
// DEST/BLENDOP/ZWRITE are captured alongside (already fetched by the same
// entry probe) so the exit log reports the true pre-window state.
// File-scope PODs — C2712-safe.
static DWORD g_coronaPreBlend = TRUE;	// D3DRS_ALPHABLENDENABLE
static DWORD g_coronaPreSrcBlend = (DWORD)D3DBLEND_SRCALPHA;
static DWORD g_coronaPreDstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
static DWORD g_coronaPreBlendOp = (DWORD)D3DBLENDOP_ADD;
static DWORD g_coronaPreZWrite = TRUE;

// Force an additive block through ALL THREE layers at the draw boundary
// (PARK-AND-SET — pipeForceCullMode idiom, same as the corona entry shield):
// for ALPHABLENDENABLE / SRCBLEND / DESTBLEND / ZWRITEENABLE the OPPOSITE
// value goes through L1 (rw cache) + L2 (RwD3D9 driver cache) FIRST — a
// guaranteed cache TRANSITION even when a cache already holds the target
// (the value-compare double-no-op that let vanilla body-internal transitions
// flush blend=0 (REPLACE) to the device while the caches still claimed
// additive) — then the TARGET through L1+L2, then raw L3 (repairs device
// drift the caches can't see). No draw happens between park and set, so the
// transient never reaches a rasteriser. BLENDOP has no rw render state (same
// rule as the entry shield) — it rides L2+L3 only. This is the per-draw
// boundary repair: it runs immediately before each rasterisation inside the
// shield window, so vanilla mid-body state transitions can no longer leave
// the following quads blend-off/black. Seeding the rw cache with the
// additive block also makes any consumer whose entry contract DIFFERS
// transition instead of no-op'ing (the cache<->device split fix).
// ADDITIVE PAIR: the default is ONE/ONE — the vanilla CCoronas /
// CPointLights / CClouds contract (all three set rwBLENDONE for both
// factors). Their glow textures carry a black RGB surround with opaque
// alpha; SRCALPHA would write that surround (the night-lamp black
// rectangle). SpriteFlushIndexedDraw_hook passes the same ONE/ONE pair
// explicitly for clouds.
// The park values are derived from the targets (src parks INVSRCALPHA,
// dst parks ZERO for a ONE target), so a cache-hit can never skip the
// transition for this pair.
static RwUInt32
coronaRwBlendFromD3D(DWORD d3db)
{
	switch(d3db){
	case D3DBLEND_ZERO:        return rwBLENDZERO;
	case D3DBLEND_ONE:         return rwBLENDONE;
	case D3DBLEND_INVSRCALPHA: return rwBLENDINVSRCALPHA;
	case D3DBLEND_SRCALPHA:
	default:                   return rwBLENDSRCALPHA;
	}
}

static void coronaDrawRawShield(const char *where, bool force = false,
                                DWORD srcBlend = (DWORD)D3DBLEND_ONE,
                                DWORD dstBlend = (DWORD)D3DBLEND_ONE)
{
	if((!g_inCoronasRender && !force) || !d3d9device)
		return;
	// DIAG (first-hit one-shot + throttled bucket "corona_draw"): read the
	// device BEFORE the forced write — the field log proves the mid-body
	// inversion (device sitting on REPLACE: blend=0, or src/dst wrong) at
	// the draw boundary. dbglog_once is not time-based, so the run's first
	// [CoronaDraw] can never be starved by a throttle-bucket collision.
	if(dbglog_once("corona_draw_first") || dbglog_throttle("corona_draw")){
		DWORD blendEn = 0, srcB = 0, dstB = 0;
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
		dbglog("[CoronaDraw] pre-force dev blend=%d src=%d dst=%d at %s "
		       "(D3DBLEND: 2=ONE 5=SRCALPHA 6=INVSRCALPHA; black-quad=blend 0)",
		       (int)blendEn, (int)srcB, (int)dstB, where);
	}
	// INI coronaBlendShield (default 1 = ON) gates ONLY the blend/ZWRITE
	// force below — the black-quad repair. 0 = leave the authored additive
	// pair (and ZWRITE) untouched. The spriteStateShield four-state block
	// further down has its own independent key.
	if(!config || config->coronaBlendShield){
		// ALPHABLENDENABLE: park FALSE -> TRUE
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		// SRCBLEND: park INVSRCALPHA (ZERO if that is the target) -> target
		DWORD parkSrc = (srcBlend == (DWORD)D3DBLEND_INVSRCALPHA)
			? (DWORD)D3DBLEND_ZERO : (DWORD)D3DBLEND_INVSRCALPHA;
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(parkSrc));
		RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(srcBlend));
		RwD3D9SetRenderState(D3DRS_SRCBLEND, srcBlend);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, srcBlend);
		// DESTBLEND: park ZERO (ONE if that is the target) -> target
		DWORD parkDst = (dstBlend == (DWORD)D3DBLEND_ZERO)
			? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(parkDst));
		RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(dstBlend));
		RwD3D9SetRenderState(D3DRS_DESTBLEND, dstBlend);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, dstBlend);
		// ZWRITEENABLE: park TRUE -> FALSE
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	}
	// ALPHAFUNC/ALPHAREF + the two raw-only states (SEPARATEALPHABLENDENABLE,
	// COLORWRITEENABLE): applied PER DRAW here, immediately before the
	// rasterisation, instead of once per window at the CCoronas entry. The
	// old entry normalize left them live for the rest of the frame
	// (GREATER-EQUAL@0 kills the soft falloff of every later alpha-tested
	// draw, a blanking colour mask drops RGB, separate-alpha rewrites
	// destination alpha). Same layers as the old entry block: L1 (rw cache,
	// where an rw state exists) + L2 + raw L3. Canonical corona values only —
	// blend FACTORS are never touched here (additive pair stays as-is).
	// INI spriteStateShield=0 skips ONLY this four-state normalisation; the
	// blend/ZWRITE force above always runs (it is the black-quad repair).
	if(!config || config->spriteStateShield){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)0);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, (DWORD)D3DCMP_GREATEREQUAL);
		RwD3D9SetRenderState(D3DRS_ALPHAREF, 0);
		RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
		RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, (DWORD)D3DCMP_GREATEREQUAL);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, 0);
		d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
		d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	}
	// BLENDOP: no rw twin — L2+L3 only (same rule as the entry shield).
	// Part of the same coronaBlendShield force (default 1 = ON).
	if(!config || config->coronaBlendShield){
		RwD3D9SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
		d3d9device->SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
	}
}

// ---- Flush-shield pre-force snapshot (file-scope POD, render thread) ----
// The flush-boundary force writes ELEVEN states through L1 (rw cache) + L2
// (driver cache) + raw L3, so the rw cache itself ends up holding the
// corona block (blend ON, ONE/ONE, ZWRITE off). The undo used to sit
// inside the g_coronaBodyDone branch, which is armed only by the
// CCoronas/CPointLights bodies — cloud / sun-reflection / water-reflection
// / HUD / deferred mid-world batches were force-written and never closed.
// Every layer is captured here BEFORE the force and replayed by
// spriteFlushShieldClose() immediately after the draw. Pre-seeded with the
// world-canonical signature so a failed Get can never replay garbage.
struct SpriteFlushState
{
	DWORD blendEn, srcBlend, dstBlend, zwrite, alphaFunc, alphaRef;
	DWORD colorWrite, sepAlpha, blendOp;					// L3 raw device
	DWORD zenable, zfunc;								// L3 raw device (depth pair)
	DWORD drvBlendEn, drvSrcBlend, drvDstBlend, drvZwrite;	// L2 driver cache
	DWORD drvAlphaFunc, drvAlphaRef, drvColorWrite, drvSepAlpha, drvBlendOp;
	DWORD drvZenable, drvZfunc;							// L2 driver cache (depth pair)
	RwUInt32 rwBlendEn, rwSrcBlend, rwDstBlend, rwZwrite;	// L1 rw cache
	RwUInt32 rwAlphaFunc, rwAlphaRef;
	RwUInt32 rwZtest;									// L1 rw cache (ZENABLE's rw twin)
};
static SpriteFlushState s_spriteFlushState;
bool s_spriteForceValid = false;	// SHARED: read by main.cpp effectsForceAlphaBlock (extern in main.cpp)

static void
spriteFlushShieldCapture(void)
{
	SpriteFlushState &s = s_spriteFlushState;
	// World-canonical pre-seed: blend ON, SRCALPHA/INVSRCALPHA, ZWRITE TRUE,
	// ALPHAFUNC GREATEREQUAL, ALPHAREF 2, CWR 0xF, sepA FALSE, BLENDOP ADD.
	s.blendEn = TRUE;
	s.srcBlend = (DWORD)D3DBLEND_SRCALPHA;
	s.dstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
	s.zwrite = TRUE;
	s.alphaFunc = (DWORD)D3DCMP_GREATEREQUAL;
	s.alphaRef = 2;
	s.colorWrite = 0xF;
	s.sepAlpha = FALSE;
	s.blendOp = (DWORD)D3DBLENDOP_ADD;
	s.zenable = TRUE;
	s.zfunc = (DWORD)D3DCMP_LESSEQUAL;
	s.drvBlendEn = s.blendEn;
	s.drvSrcBlend = s.srcBlend;
	s.drvDstBlend = s.dstBlend;
	s.drvZwrite = s.zwrite;
	s.drvAlphaFunc = s.alphaFunc;
	s.drvAlphaRef = s.alphaRef;
	s.drvColorWrite = s.colorWrite;
	s.drvSepAlpha = s.sepAlpha;
	s.drvBlendOp = s.blendOp;
	s.drvZenable = s.zenable;
	s.drvZfunc = s.zfunc;
	s.rwBlendEn = (RwUInt32)TRUE;
	s.rwSrcBlend = (RwUInt32)rwBLENDSRCALPHA;
	s.rwDstBlend = (RwUInt32)rwBLENDINVSRCALPHA;
	s.rwZwrite = (RwUInt32)TRUE;
	s.rwAlphaFunc = (RwUInt32)rwALPHATESTFUNCTIONGREATEREQUAL;
	s.rwAlphaRef = (RwUInt32)2;
	s.rwZtest = (RwUInt32)TRUE;
	// L1 (rw cache) — only the seven states that have an rw twin. COLORWRITE-
	// ENABLE, SEPARATEALPHABLENDENABLE and BLENDOP have none.
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s.rwBlendEn);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &s.rwSrcBlend);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s.rwDstBlend);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s.rwZwrite);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &s.rwAlphaFunc);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &s.rwAlphaRef);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &s.rwZtest);
	// L2 (RwD3D9 driver cache). A state the driver cannot report keeps the
	// pre-seeded canonical value (the Get is a no-op or device fallback).
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s.drvBlendEn);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &s.drvSrcBlend);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &s.drvDstBlend);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &s.drvZwrite);
	RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &s.drvAlphaFunc);
	RwD3D9GetRenderState(D3DRS_ALPHAREF, &s.drvAlphaRef);
	RwD3D9GetRenderState(D3DRS_COLORWRITEENABLE, &s.drvColorWrite);
	RwD3D9GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &s.drvSepAlpha);
	RwD3D9GetRenderState(D3DRS_BLENDOP, &s.drvBlendOp);
	// Force depth pair to canonical — never capture corrupted values
	// (ZENABLE=0/ZFUNC=ALWAYS) from the device. This prevents the sprite
	// flush shield from propagating depth corruption.
	s.drvZenable = TRUE;
	s.drvZfunc = (DWORD)D3DCMP_LESSEQUAL;
	// Raw L3 (the value the rasteriser actually used).
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.blendEn);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &s.srcBlend);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &s.dstBlend);
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &s.zwrite);
		d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &s.alphaFunc);
		d3d9device->GetRenderState(D3DRS_ALPHAREF, &s.alphaRef);
		d3d9device->GetRenderState(D3DRS_COLORWRITEENABLE, &s.colorWrite);
		d3d9device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &s.sepAlpha);
		d3d9device->GetRenderState(D3DRS_BLENDOP, &s.blendOp);
		// Force depth pair to canonical — never capture corrupted values.
		s.zenable = TRUE;
		s.zfunc = (DWORD)D3DCMP_LESSEQUAL;
	}
	s_spriteForceValid = true;
}

// ---- Canonical depth-contract enforcement (sprite/font/HUD exit) --------
// N2 dead-depth repair (zfunc=8): the 2D Im2D/font/HUD draws park ZTEST off
// + ZFUNC ALWAYS (zfunc=8) through L1+L2+L3 and never restore it, so every
// later draw runs with dead depth testing until a pipe-entry [zfix] catches
// it. The sprite flush shield now restores its ELEVEN captured states
// (including the ZENABLE/ZFUNC depth pair) and the font fast path enforces
// the contract on exit, so zfunc=8 can no longer survive a flush boundary.
// This helper is the belt-and-braces net for the residual cases (a first
// capture taken while the device was already leaked). Enforce the canonical
// depth contract (renderstate_monitor.h:
// ZTEST=1, ZFUNC=LESSEQUAL). ZWRITE is deliberately NOT touched
// (per-situation — the caller owns the ZWRITE restore). ZTEST has an rw twin
// (L1+L2+L3); ZENABLE/ZFUNC are L2+L3 only (no rw zfunc twin). Cheap
// early-out: two device-layer reads; when both already hold the contract the
// writes below are a semantic no-op and are skipped entirely.
// The enforcement writes route through pipeDeviceStateSet (pipelinecommon.cpp)
// — the shared cache-coherent 3-layer chokepoint — so ZENABLE (rw twin
// ZTESTENABLE) and ZFUNC (L2+L3 only, no rw twin) can never leave the layers
// disagreeing.
extern void pipeDeviceStateSet(D3DRENDERSTATETYPE state, DWORD value);
static void
spriteEnforceDepthContract(const char *path)
{
	if(!d3d9device)
		return;
	DWORD devZen = 0, devZFunc = 0;
	if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZENABLE, &devZen)) &&
	   SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZFUNC, &devZFunc)) &&
	   devZen != 0 && devZFunc == D3DCMP_LESSEQUAL)
		return;
	if(dbglog_throttle("sprite_depthfix"))
		dbglog("[SpriteDepthFix] %s depth drift (dev zen=%u zfunc=%u) — "
		       "enforcing ZTEST=1 ZFUNC=LESSEQUAL (3L, no rw zfunc twin); "
		       "victims if leaked: dead depth testing",
		       path ? path : "?", (unsigned)devZen, (unsigned)devZFunc);
	pipeDeviceStateSet(D3DRS_ZENABLE, TRUE);
	pipeDeviceStateSet(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

// Replay a captured ZWRITEENABLE through L1 + L2 + raw L3. Mirrors the font
// hook's own blend restore: the rw/driver caches may already hold the target
// (a value-compare no-op), so the unconditional raw L3 leg is what repairs
// the device state the Im2D glyph draw parks OFF in between. The captured
// entry value is replayed verbatim — the font contract is "ZWRITE OFF while
// the glyphs rasterise, the entry value after", never a forced TRUE.
static void
spriteRestoreZWrite(DWORD devZw, DWORD drvZw, RwUInt32 rwZw)
{
	if(!d3d9device)
		return;
	// Cross-layer disagreement guard (same rule as spriteFlushShieldClose's
	// ZWRITE replay): when BOTH caches agree the device leg is the drifted
	// one — the [zfix] pipe-entry signature rw zwrite=1 drv zwrite=1 dev
	// zwrite=0, left behind by the glyph draw's device-side ZWRITE park
	// (a raw L3 write no cache sees). Replaying device-0 verbatim publishes
	// it under pending[]==applied[]==1, the one split RW's applied[]-gated
	// pre-draw flush can NEVER repair — the shield re-cemented the drift the
	// pipe entry had just healed (65x/session in the 02:17 and 06:25 logs).
	// Caches agree ON + device says OFF -> heal the device leg to the cache
	// consensus; every other combination (incl. a coherent capture whose
	// layers agree) replays verbatim, so the font contract documented above
	// ("OFF while the glyphs rasterise, the entry value after") is unchanged.
	if(rwZw && drvZw && !devZw)
		devZw = TRUE;
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)rwZw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, drvZw ? TRUE : FALSE);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, devZw ? TRUE : FALSE);
}

// Replay the eleven captured states through L1 + L2 + raw L3. PARK-AND-SET on
// every cached layer: the opposite value goes through the cache first, so a
// cache-hit skip cannot swallow the target (pipeForceCullMode idiom). A
// captured blend-OFF (REPLACE) is NEVER replayed verbatim — that state is
// the black-quad signature a deferred batch can leave behind, so it is
// replaced by the canonical world normal-alpha block (blend TRUE,
// SRCALPHA/INVSRCALPHA, BLENDOP ADD); ZWRITE keeps its captured value.
// Called after EVERY forced batch draw — NOT gated on g_coronaBodyDone. The
// window-close resync in SpriteFlushIndexedDraw_hook is a separate canonical
// push and still runs for its own producers.
static void
spriteFlushShieldClose(const char *path)
{
	if(!s_spriteForceValid)
		return;
	s_spriteForceValid = false;
	if(!d3d9device)
		return;
	SpriteFlushState &s = s_spriteFlushState;
	// Captured REPLACE -> canonical normal-alpha on all three layers (see
	// header comment). The snapshot fields are rewritten so the existing
	// park-and-set replay below targets the canonical block; ZWRITE is left
	// on its captured value.
	bool capturedReplace = (s.blendEn == FALSE);
	if(capturedReplace){
		s.blendEn = s.drvBlendEn = (DWORD)TRUE;
		s.rwBlendEn = (RwUInt32)TRUE;
		s.srcBlend = s.drvSrcBlend = (DWORD)D3DBLEND_SRCALPHA;
		s.rwSrcBlend = (RwUInt32)rwBLENDSRCALPHA;
		s.dstBlend = s.drvDstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
		s.rwDstBlend = (RwUInt32)rwBLENDINVSRCALPHA;
		s.blendOp = s.drvBlendOp = (DWORD)D3DBLENDOP_ADD;
	}
	// ALPHABLENDENABLE: park the boolean inverse -> captured target.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE,
		(void*)(s.rwBlendEn ? (RwUInt32)FALSE : (RwUInt32)TRUE));
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s.drvBlendEn ? FALSE : TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)s.rwBlendEn);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s.drvBlendEn);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s.blendEn);
	// SRCBLEND: park a guaranteed-different rw / D3D value -> target.
	RwUInt32 parkRwSrc = (s.rwSrcBlend == (RwUInt32)rwBLENDSRCALPHA)
		? (RwUInt32)rwBLENDINVSRCALPHA : (RwUInt32)rwBLENDSRCALPHA;
	DWORD parkSrc = (s.drvSrcBlend == (DWORD)D3DBLEND_ZERO)
		? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)parkRwSrc);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)s.rwSrcBlend);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, s.drvSrcBlend);
	d3d9device->SetRenderState(D3DRS_SRCBLEND, s.srcBlend);
	// DESTBLEND: same park-and-set pair.
	RwUInt32 parkRwDst = (s.rwDstBlend == (RwUInt32)rwBLENDONE)
		? (RwUInt32)rwBLENDZERO : (RwUInt32)rwBLENDONE;
	DWORD parkDst = (s.drvDstBlend == (DWORD)D3DBLEND_ZERO)
		? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)parkRwDst);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)s.rwDstBlend);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, s.drvDstBlend);
	d3d9device->SetRenderState(D3DRS_DESTBLEND, s.dstBlend);
	// Cross-layer disagreement guard for the ZWRITE replay (same rule as
	// spriteRestoreZWrite): the three captured legs can disagree when the
	// device was already drifted at capture time (rw zwrite=1 drv zwrite=1
	// dev zwrite=0 — the [zfix] pipe-entry signature). Replaying that
	// verbatim re-publishes device-0 while L1/L2 claim 1, and because the
	// raw leg moves the device WITHOUT touching pending[]/applied[], the
	// applied[]-gated pre-draw flush value-compares into a no-op and can
	// never repair it — the close re-cements the split every cycle (the
	// doctrine main.cpp's CClouds window close documents: when the captures
	// disagree, the caches are the single source; never re-publish the
	// split). Caches agree ON + device says OFF -> device leg heals to the
	// cache consensus; a capture whose three legs already agree (the normal
	// case, and the legitimate font contract OFF-during-glyphs) replays
	// verbatim, byte-identical to the old behaviour.
	if(s.rwZwrite && s.drvZwrite && !s.zwrite)
		s.zwrite = TRUE;
	// ZWRITEENABLE: park the boolean inverse -> captured target.
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,
		(void*)(s.rwZwrite ? (RwUInt32)FALSE : (RwUInt32)TRUE));
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s.drvZwrite ? FALSE : TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)s.rwZwrite);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s.drvZwrite);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, s.zwrite);
	// ALPHAFUNC: park GREATEREQUAL<->ALWAYS (always a transition).
	RwUInt32 parkRwFn = (s.rwAlphaFunc == (RwUInt32)rwALPHATESTFUNCTIONALWAYS)
		? (RwUInt32)rwALPHATESTFUNCTIONGREATEREQUAL
		: (RwUInt32)rwALPHATESTFUNCTIONALWAYS;
	DWORD parkFn = (s.drvAlphaFunc == (DWORD)D3DCMP_ALWAYS)
		? (DWORD)D3DCMP_GREATEREQUAL : (DWORD)D3DCMP_ALWAYS;
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)parkRwFn);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, parkFn);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)s.rwAlphaFunc);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, s.drvAlphaFunc);
	d3d9device->SetRenderState(D3DRS_ALPHAFUNC, s.alphaFunc);
	// ALPHAREF: park 3 when the captured value is the canonical 2.
	RwUInt32 parkRef = (s.rwAlphaRef == (RwUInt32)2) ? (RwUInt32)3 : (RwUInt32)2;
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)parkRef);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, parkRef);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)s.rwAlphaRef);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, s.drvAlphaRef);
	d3d9device->SetRenderState(D3DRS_ALPHAREF, s.alphaRef);
	// COLORWRITEENABLE: raw-only twin pair — park the bitwise inverse.
	DWORD parkCwr = s.drvColorWrite ^ 0xF;
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, parkCwr);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, s.drvColorWrite);
	d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, s.colorWrite);
	// SEPARATEALPHABLENDENABLE: raw-only twin pair — park the inverse.
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,
		s.drvSepAlpha ? FALSE : TRUE);
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, s.drvSepAlpha);
	d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, s.sepAlpha);
	// BLENDOP: no rw twin — L2+L3 only; park ADD<->REVSUBTRACT.
	DWORD parkOp = (s.drvBlendOp == (DWORD)D3DBLENDOP_ADD)
		? (DWORD)D3DBLENDOP_REVSUBTRACT : (DWORD)D3DBLENDOP_ADD;
	RwD3D9SetRenderState(D3DRS_BLENDOP, parkOp);
	RwD3D9SetRenderState(D3DRS_BLENDOP, s.drvBlendOp);
	d3d9device->SetRenderState(D3DRS_BLENDOP, s.blendOp);
	// ZENABLE: park the boolean inverse -> the FORCED-CANONICAL capture
	// (s.zenable, forced TRUE by spriteFlushShieldCapture's "never capture
	// corrupted values" rule — the same source the ZFUNC restore below
	// already uses via s.drvZfunc/s.zfunc). This restore used to drive all
	// three layers from s.rwZtest, the RAW L1 read: a leaked font/Im2D
	// ZTEST-off park leaves it 0, so every close replayed device
	// ZENABLE=0 and the trailing enforce below had to repair it — the
	// [SpriteDepthFix] "dev zen=0" churn (postfx-white x22 + the
	// SpriteFlush_70CF20/clouds hits in the 02:17 log). End state is
	// unchanged: enforce would force exactly this canonical value anyway;
	// this only removes the transient device-0 and the receipt churn it
	// caused. s.rwZtest stays captured (diagnostic record of the raw read).
	Transparency_SetZEnabled(!(s.zenable ? true : false));
	Transparency_SetZEnabled(s.zenable ? true : false);
	// ZFUNC: no rw twin — L2+L3 only; park LESSEQUAL<->ALWAYS.
	DWORD parkZFunc = (s.drvZfunc == (DWORD)D3DCMP_LESSEQUAL)
		? (DWORD)D3DCMP_ALWAYS : (DWORD)D3DCMP_LESSEQUAL;
	RwD3D9SetRenderState(D3DRS_ZFUNC, parkZFunc);
	RwD3D9SetRenderState(D3DRS_ZFUNC, s.drvZfunc);
	d3d9device->SetRenderState(D3DRS_ZFUNC, s.zfunc);
	// N2 dead-depth repair (belt-and-braces): the Im2D/font/HUD draws inside
	// this flush window park ZTEST off + ZFUNC ALWAYS (zfunc=8); the
	// eleven-state restore above now replays the captured depth pair, and
	// this enforce is the cheap early-out net that catches residual drift
	// (e.g. a first capture taken while the device was already leaked) so
	// sprite/font/HUD draws never leave dead depth behind.
	spriteEnforceDepthContract(path);
	if(dbglog_once("flushshield_restore"))
		dbglog("[FlushShield] restore path=%s states=11 blend=%d src=%d dst=%d "
		       "zw=%d ref=%d%s", path ? path : "?", (int)s.blendEn,
		       (int)s.srcBlend, (int)s.dstBlend, (int)s.zwrite,
		       (int)s.alphaRef,
		       capturedReplace
		           ? " (captured REPLACE -> canonical normal-alpha)"
		           : "");
}

WRAPPER void
CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect_orig(float x, float y, float z, float a4, float a5, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwInt16 f, int a10, float a11, RwUInt8 alpha) { EAXJMP(0x70E780); }

int currentLight;
char *stkp;

void
CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect(float x, float y, float z, float a4, float a5, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwInt16 f, int a10, float a11, RwUInt8 alpha)
{
	_asm mov [currentLight], esi
	_asm mov [stkp], ebp
	float mult = *(float*)(stkp + 0x48);
	currentLight /= sizeof(PointLight);

	// One-shot anchor receipt (unthrottled static, not a hash-bucket tag):
	// resolved base + live registry count (0xC3F0D0, the count the game's
	// Add/Render code reads). A mis-anchor or a moved array with an
	// unpatched immediate is visible here on the first sprite.
	static bool pointLightsAnchorLogged = false;
	if(!pointLightsAnchorLogged){
		pointLightsAnchorLogged = true;
		dbglog("[PointLights] base=%p count=%d (base from lea imm @0x7001FF)",
		       (void*)pointLights, *(int*)0xC3F0D0);
	}

	float add = pointLights[currentLight].fogType == 1 ? 0.0f : 16.0f;
	r = mult*pointLights[currentLight].color[0]+add;
	g = mult*pointLights[currentLight].color[1]+add;
	b = mult*pointLights[currentLight].color[2]+add;
	f = 0xFF;
	// Corona draw-time raw shield — raw-force immediately before the draw.
	// Uses the ONE/ONE default (no explicit pair override). The window gate
	// inside coronaDrawRawShield passes here because the 0x7002D0 entry hook
	// (CPointLights__Render_shield_hook) re-arms g_inCoronasRender before the
	// body issues this 0x700B6B call; the batch's actual raster is also
	// force-covered by SpriteFlushIndexedDraw_hook on every non-empty flush.
	coronaDrawRawShield("CSprite_70E780");
	CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect_orig(x, y, z, a4, a5, r, g, b, f, a10, a11, alpha);
}

// ---- Buffered-sprite flush: the innermost corona draw entry -------------
// Vanilla CCoronas::Render (0x6FAEC0) does NOT rasterise its quads through
// 0x70E780 — the corona body APPENDS quads to the RW TempBuffer sprite batch
// (0x70F440-family appender, called 3x from the body at 0x6FB461/0x6FB561/
// 0x6FB5EA; verts 0xC5F958, indices 0xC4B958, count 0xC6A158) and the batch
// is drawn in one indexed primitive by the buffered-flush (0x70CF20).
// VANILLA timing: the body's ONLY exit is the tail-jmp at 0x6FB621 ->
// 0x70CF20 (no ret anywhere in 0x6FAEC0..0x6FB630), and the 0x70F440-family
// appenders tail-jmp to the same flush (0x70F531 etc.) when the 384-sprite
// batch fills — every rasterisation happens INSIDE CCoronas::Render.
// SILENTPATCH OVERRIDE (SilentPatchSA.asi init, disassembled): it rewrites
// the tail-jmp — `mov dword [0x70F45A],0` + `mov word [0x6FB621],0xE8C3` +
// call-rel32 helper with edx=0x70CF20 — turning 0x6FB621 into a plain RET.
// The corona batch then persists and is drawn by the NEXT consumer's flush
// (CPointLights tail-jmp 0x700BCD, CClouds calls 0x714019/0x71422A/0x714387/
// 0x715EFB/0x719B26, RenderReflections call 0x6FB9C2) — i.e. AFTER
// CCoronas::Render returns. The flush's two draw calls (0x70CF62 / 0x70CF9B
// -> 0x734EA0, cdecl (primType, verts, nverts, indices, nindices)) are still
// the innermost draw entries for EVERY corona quad — but the window must
// span them: g_inCoronasRender stays TRUE past the Render exit and closes
// here on the first post-body flush (g_coronaBodyDone). Mid-body
// full-batch flushes keep the window open so remaining appends stay
// covered. Since 2026-10-24 the additive shield is ALSO forced at this flush
// boundary (force=true, non-empty batch only); since 2026-10-26 that force
// runs for EVERY non-empty batch — the silent-patch-deferred corona/pointlight
// raster, the early CPointLights::Render glow sprites at 0x53DE6B, and any
// other shared-flush producer — and is closed again right after its draw.
// Since 2026-11-01 (perf fix) the force is PRODUCER-GATED: only a flush that
// carries an undrained shared batch (0xC6A158 > 0 at hook entry — the flush
// drains the count at 0x70CF76, between the two hooked call sites) runs the
// capture/force/close; a drained-batch draw site passes through with ZERO
// added state ops. Capture/close stay paired exactly as before on the gated
// path.
typedef void (__cdecl *SpriteFlushIndexedDraw_t)(int primType, void *verts, int nverts, void *indices, int nindices);
static SpriteFlushIndexedDraw_t SpriteFlushIndexedDraw_orig;

// ---- Root-cause blend-contract reconcile gate (spriteFlushShield=0 path) --
// DEFECT (screenshots gta_sa_qUIPC6eVMq.jpg / gta_sa_JZOcnBTZrU.jpg, log
// 07:46-07:48): giant organic black blotches over roads/ground, 42x
// `[PtLightGlow] flush batch=N drew UNSHIELDED dev blend=0|1 src=5 dst=6`.
// ROOT CAUSE — verified against the field log, NOT a cache desync: every
// layer agrees on the same wrong pair. The glow batch is QUEUED, not
// rasterised, inside its producer (SilentPatchSA rewrites CCoronas::Render's
// tail-jmp at 0x6FB621 into a RET), so the game's own additive contract
// (rwBLENDONE / rwBLENDONE + VERTEXALPHA — gta-reversed Coronas.cpp:195-196;
// our `[CoronaBlend]` probe right after the body reads dev blend=1 src=2
// dst=2) is consumed BEFORE the quads reach a rasteriser:
//   * CCoronas__Render_blendprobe_hook's EXIT RESTORE pushes the canonical
//     normal-alpha block at Render exit (trail immediately upstream of the
//     first flush: `[CoronaExit]` then `[FxAlpha] PostFx.repaired: rw(vtx=0
//     src=5 dst=6 zw=1) dev(vtx=0 src=5 dst=6 zw=1)`);
//   * the world's own passes ([Pipeline] pipeApplyWorldBaseline, veg /
//     building draws) then re-write blend=1 src=5 dst=6.
// Either way the device reaches 0x70CF20 on SRCALPHA/INVSRCALPHA or blend
// OFF while the glow texture is authored black-RGB + opaque alpha for
// ONE/ONE: the surround REPLACES the background instead of adding nothing
// -> the black blotch. spriteFlushShield=1 hides this by forcing the pair
// at the flush boundary; =0 (the user's INI, "vanilla settings should
// work") leaves the raw state, so the flush must reconcile the blend
// contract ITSELF for glow-family batches.
//
// GATE — positive identification only, everything else FAILS OPEN: a batch
// is glow-family when its L1 stage-0 raster is one of the corona/pointlight/
// cloud glow rasters (flushBatchIdentity — the same verdict
// PostFxFlush_701FE9_hook already uses) OR a producer window is open
// (g_inCoronasRender / g_inCloudsRender / g_inPointLightsRender — the
// first two are the exact triple effectsForceAlphaBlock's producer gate
// trusts; the third is the FIX A body-scoped CPointLights leg that keeps
// the street-lamp fog-cone batch covered after a mid-body overflow flush
// consumes g_coronaBodyDone — see the flag's block comment). Glyph/font/
// HUD/screen-blit batches therefore keep their authored normal-alpha (the
// 2026-11-01 washed-out-font class), and the blend!=0 fail-open elsewhere
// is untouched.
// The writes run INSIDE the SEH fence around the orig draw so the pre-flush
// block is always put back; the spriteFlushShield=1 branch keeps its own
// eleven-state capture/force/close byte-for-byte unchanged (this block is
// unreachable there — it lives in the lever-OFF else-branch).
struct SpriteBlendContract
{
	bool pass;	// glow-family -> reconcile the contract for this draw
	bool family;	// flushBatchIdentity(): L1 stage-0 = glow raster
	bool window;	// g_inCoronasRender || g_inCloudsRender || g_inPointLightsRender
	RwRaster *ras;	// L1 stage-0 raster (receipt only)
};
// Defined beside flushBatchIdentity()/isFrontOrCameraRaster() further down
// (those helpers are declared AFTER this hook in the file), so the call
// site below needs this forward declaration.
static SpriteBlendContract spriteBlendContract(void);
static bool isFrontOrCameraRaster(RwRaster *ras);	// FIX B screenblit gate

static void __cdecl
SpriteFlushIndexedDraw_hook(int primType, void *verts, int nverts, void *indices, int nindices)
{
	// ---- Producer-ownership gate input (read at hook entry) ------------
	// The shared TempBuffer population counter is still live HERE: the flush
	// drains it at 0x70CF76, BETWEEN the two hooked call sites (0x70CF62 /
	// 0x70CF9B), and orig (0x734EA0) is the draw primitive itself. So
	// spriteBatch > 0 at entry == this draw carries an undrained
	// corona/pointlight-family batch — the only producers that need the
	// additive shield (the black-squares defect is their glow quads
	// rasterising with leaked REPLACE state). spriteBatch == 0 (the flush's
	// second draw site after its own 0x70CF76 reset, or any flush whose
	// batch an earlier flush already drained) passes through with ZERO
	// added state ops.
	int spriteBatch = *(int*)0xC6A158;
	// First-hit receipts BEFORE the shield writes: absence of these lines
	// means neither shield path engaged. blend/src/dst are the device
	// values the shield is about to overwrite.
	if(g_inCoronasRender){
		static bool s_probeSpriteFlushShield = false;
		if(!s_probeSpriteFlushShield){
			s_probeSpriteFlushShield = true;
			int blend = -1, src = -1, dst = -1;
			if(d3d9device){
				DWORD v = 0;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
			}
			dbglog("[probe] sprite_flush shield active pre-dev blend=%d src=%d dst=%d",
				blend, src, dst);
		}
	}
	// ---- Stage-0 identity receipt (H3, 2026-11-16 particle-alpha cleanup) --
	// MEASUREMENT ONLY — reads, never writes: rw-cache texture raster (L1)
	// via RwRenderStateGet + raw device stage-0 texture (L3) via GetTexture
	// (AddRef/Release). No render state is touched. Tests H3: water/wake
	// flushes deliberately leave their world raster bound at stage 0
	// (main.cpp water/wake sites — the bind is NOT restored), so the next
	// cache-gated sprite bind can no-op and the batch samples the LAST
	// WORLD TEXTURE (teal over sea / brown over land) instead of the glow
	// raster. Gated on spriteBatch > 0: an empty batch early-outs at
	// 0x70CF27 before any sprite samples stage 0, so its identity is
	// irrelevant there. Throttled 2s (tag "sprite_stage0"); first call of
	// any 2s window is unstarvable, so a short run still yields evidence.
	if(spriteBatch > 0 && dbglog_throttle("sprite_stage0")){
		void *rawRas = NULL;
		IDirect3DBaseTexture9 *devTex = NULL;
		RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rawRas);
		if((void*)rawRas == (void*)0xFFFFFFFF)
			rawRas = NULL;	// failed-Get sentinel (flushBatchIdentity convention)
		if(d3d9device)
			d3d9device->GetTexture(0, &devTex);	// AddRef, released below
		dbglog("[sprite_stage0] batch=%d nverts=%d nidx=%d s0Raster=%p s0DevTex=%p "
		       "(H3: non-glow s0 here = sprite samples the last world texture)",
		       spriteBatch, nverts, nindices, rawRas, (void*)devTex);
		if(devTex)
			devTex->Release();
	}
	// ---- Force the additive block on the corona-family carrier flushes --
	// PRODUCER GATING (perf fix 2026-11-01): the force below used to run on
	// EVERY hook invocation that reached the draw sites (~100 D3D9 ops each,
	// 10-50 flushes/frame). It is now gated on the producer-ownership test:
	// spriteBatch > 0 at hook entry (read above, BEFORE orig drains it).
	// 2026-10-24 diagnosis: the black squares with the tiny coloured core
	// are CPointLights glow sprites (0x700B6B -> 0x70E780) rasterised by
	// THIS shared flush at the EARLY CPointLights::Render call site
	// 0x53DE6B (sky/reflection phase). SilentPatchSA ALSO defers the
	// corona/pointlight batch past CCoronas::Render's return, so the shield
	// window is already CLOSED when the real rasterisation happens (field
	// log 2026-10-26: 230/232 flushes unforced with pre-force src=5 dst=6 —
	// that 5/6 pair writes the black rectangle around the coloured core).
	// A window tied to one body cannot cover a shared deferred queue: force
	// the vanilla additive block at the flush boundary itself for every
	// carrier batch (ALPHABLENDENABLE=TRUE, SRCBLEND=DESTBLEND=ONE,
	// ZWRITE=FALSE) through the park-and-set L1+L2+raw-L3 path.
	// SAFE because spriteFlushShieldCapture() snapshots all eleven states
	// before the force and spriteFlushShieldClose() replays them right after
	// SpriteFlushIndexedDraw_orig on every forced path — a non-corona batch
	// sees the additive pair only for its own raster; the surrounding frame
	// is restored exactly.
	// SAFETY: 0x70CF27 returns before both draw calls when the batch count
	// is <= 0, so mirror that guard here — the force (and its one-shot
	// probe) must never run for an empty batch (orig would no-op anyway).
	// Count = buffered-sprite count at 0xC6A158, read as a dword.
	const char *spriteForcePath = NULL;	// close-receipt label, force-only
	bool spriteForced = false;
	// ---- Root-cause blend-contract reconcile PODs (lever-OFF path only) --
	// Armed by the spriteFlushShield=0 else-branch below; the capture/force
	// writes run inside the SEH fence around the orig draw and the __finally
	// puts the pre-flush block back on every path. Constant-init PODs only —
	// no dynamic initialisation, C2712-safe inside __try/__finally.
	bool glowContractPending = false;	// lever OFF + non-empty batch -> evaluate the gate
	bool glowContractForced = false;	// snapshot taken + producer contract written
	DWORD glowPreBlendEn = (DWORD)TRUE;		// pre-flush device block (verbatim undo)
	DWORD glowPreSrcBlend = (DWORD)D3DBLEND_SRCALPHA;
	DWORD glowPreDstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
	DWORD glowPreBlendOp = (DWORD)D3DBLENDOP_ADD;
	{
		// One-shot gate receipt + throttled skipped-vs-shielded counter
		// (perf fix 2026-11-01). POD statics only — constant init, no
		// dynamic initialisation (DLL context).
		static bool s_flGateReceipt = false;
		static unsigned s_flShielded = 0, s_flSkipped = 0;
		if(!s_flGateReceipt){
			s_flGateReceipt = true;
			dbglog("[sprite_flush] producer gating active: shield only on spriteBatch>0");
		}
		// INI spriteFlushShield (default 1 = ON): 0 = no capture / force /
		// close at the flush boundary at all (the vanilla batch state is
		// drawn untouched — bisect lever for the producer-gated force).
		// W2-1: ONE family-identity predicate for both lever states. Hoisted so
	// lever-ON uses the same gate as lever-OFF (family || window, screenblit
	// excluded). See docs/plans/2026-11-27-w2-specs.md SPEC 9 W2-1.
	SpriteBlendContract gate = spriteBlendContract();
	if(spriteBatch > 0 && d3d9device && (!config || config->spriteFlushShield)){
			if(dbglog_once("sprite_flush_force")){
				int blend = -1, src = -1, dst = -1, zw = -1;
				if(d3d9device){
					DWORD v = 0;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &v)))     zw    = (int)v;
				}
				dbglog("[probe] sprite_flush force=1 pre blend=%d src=%d dst=%d zw=%d",
					blend, src, dst, zw);
			}
			// W2-1: ONE family-identity predicate for both lever states.
			// gate.pass is (family || window) && !screenblit.
			if(gate.pass){
				// Capture the PRE-force eleven-state block on every layer BEFORE
				// the first shield write; spriteFlushShieldClose replays it
				// immediately after the draw.
				spriteFlushShieldCapture();
				// Vanilla CCoronas / CPointLights / CClouds all draw ONE/ONE
				// additive (their glow textures have a black RGB surround with
				// opaque alpha). Both producers now force the SAME pair; the
				// g_inCloudsRender flag only selects the close-path label below.
				if(g_inCloudsRender){
					coronaDrawRawShield("SpriteFlush_70CF20", true,
						(DWORD)D3DBLEND_ONE, (DWORD)D3DBLEND_ONE);
					spriteForcePath = "SpriteFlush_70CF20/clouds";
				}else{
					coronaDrawRawShield("SpriteFlush_70CF20", true);
					spriteForcePath = "SpriteFlush_70CF20/corona";
				}
				spriteForced = true;
				// One-shot receipt per producer: the first forced flush of each
				// kind names the pair. dbglog_once is not time-based.
				if(dbglog_once(g_inCloudsRender ? "cloud_shield_clouds"
				                                : "cloud_shield_sprites"))
					dbglog(g_inCloudsRender
						? "[CloudShield] flush batch=clouds forced ONE/ONE"
						: "[CloudShield] flush batch=sprites forced ONE/ONE");
			}else{
				// Non-glow batch: delegate to the SHARED in-fence arm
				// (same fail-closed/skip decision as lever-OFF).
				glowContractPending = true;
			}
		}else if(spriteBatch > 0 && d3d9device){
			// [PtLightGlow] UNSHIELDED-FLUSH diagnostic (03:01-run black
			// squares): the batch IS about to rasterise but the flush-
			// boundary force is disabled — INI spriteFlushShield=0 (the
			// 03:01 run had coronaBlendShield/spriteFlushShield/
			// spriteStateShield all =0, so the verified flush installs
			// above never forced and the batch drew with whatever the
			// device held: 181 samples SRCALPHA/INVSRCALPHA + 23 REPLACE,
			// the glow texture's opaque black surround -> black squares
			// with bright centers). Throttled: an every-frame batch must
			// not flood the log; dbglog_once makes the run's FIRST hit
			// unstarvable. Consistency under producer gating: this branch
			// requires spriteBatch>0 AND the shield lever OFF, so with
			// spriteFlushShield=1 (default) it can never fire — every
			// carrier batch is shielded by the branch above.
			if(dbglog_once("ptlightglow_unshielded") || dbglog_throttle("ptlightglow")){
				DWORD ub = 0, us = 0, ud = 0;
				d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &ub);
				d3d9device->GetRenderState(D3DRS_SRCBLEND, &us);
				d3d9device->GetRenderState(D3DRS_DESTBLEND, &ud);
				dbglog("[PtLightGlow] flush batch=%d drew UNSHIELDED dev blend=%d src=%d dst=%d "
				       "(spriteFlushShield=%d — 0 disables the additive force; "
				       "black-square producer whenever this is not blend=1 src=2 dst=2)",
				       spriteBatch, (int)ub, (int)us, (int)ud,
				       config ? config->spriteFlushShield : -1);
			}
			// Root-cause reconcile gate input (read-only; the writes run
			// inside the SEH fence below, immediately before the orig draw).
			// The receipt above keeps naming the RAW entry state — that is
			// the root-cause evidence — and [PtLightGlow] root-cause ... (see
			// the fence) then names the reconcile decision taken for it.
			glowContractPending = true;
		}
		// Throttled skipped-vs-shielded receipt for the producer gate:
		// "skipped" = this draw site reached the hook with an already-drained
		// batch (spriteBatch<=0 — the flush's second draw site after its own
		// 0x70CF76 reset, or a batch a previous flush drained) or with the
		// bisect lever off. Those sites add ZERO state ops.
		if(spriteForced)
			s_flShielded++;
		else
			s_flSkipped++;
		if(dbglog_throttle("sprite_flush_gate"))
			dbglog("[sprite_flush] gate shielded=%u skipped=%u "
			       "(skipped = spriteBatch<=0 at entry or spriteFlushShield=0)",
			       s_flShielded, s_flSkipped);
	}
	// Xbox-clouds lane hunk B: the cloud sprite flush must rasterise with the
	// clouds pixel shader armed. Bind through BOTH layers (RW cache + raw
	// device) before the orig call and unbind right after it under the same
	// guard — g_inCloudsRender is only true inside CClouds__Render_hygiene's
	// orig window, so the pair can never leak into corona/HUD/deferred flushes.
	if(g_inCloudsRender && cloudsPS && d3d9device){
		RwD3D9SetPixelShader(cloudsPS);
		d3d9device->SetPixelShader((IDirect3DPixelShader9*)cloudsPS);
	}
	// SEH latch guard: the orig draw sits between spriteFlushShieldCapture
	// (sets s_spriteForceValid=true) and spriteFlushShieldClose (clears it).
	// A fault here that an outer handler swallows latched the flag true for
	// the rest of the session -> effectsForceAlphaBlock producer gate
	// skipped forever. __finally runs the unbind + close on BOTH the normal
	// and fault paths, so the latch can never survive a swallowed fault.
	__try{
		// ---- ROOT-CAUSE BLEND RECONCILE (spriteFlushShield=0 only) -------
		// See SpriteBlendContract above: by the time this deferred batch
		// rasterises the producer's ONE/ONE has already been overwritten by
		// the world's normal-alpha, so push the producer's own contract back
		// through the shared 3-layer chokepoint (pipeDeviceStateSet: L1 rw
		// cache + L2 driver cache + raw L3 — never a single-layer write) for
		// THIS draw only. Order is snapshot -> ARM -> write, so a fault
		// between the arm and the draws still reaches the __finally undo
		// below with a valid pre-flush snapshot. All PODs (C2712-safe).
		if(glowContractPending && d3d9device){
			SpriteBlendContract gate = spriteBlendContract();
			DWORD devBlend = (DWORD)TRUE;
			DWORD devSrc = (DWORD)D3DBLEND_SRCALPHA;
			DWORD devDst = (DWORD)D3DBLEND_INVSRCALPHA;
			DWORD devOp = (DWORD)D3DBLENDOP_ADD;
			DWORD v = 0;
			if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) devBlend = v;
			if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         devSrc   = v;
			if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        devDst   = v;
			if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_BLENDOP, &v)))          devOp    = v;
			// Already on the producer pair -> nothing to write and nothing to
			// restore; the receipt still names the entry state.
			bool alreadyAdditive = (devBlend != 0 &&
				devSrc == (DWORD)D3DBLEND_ONE &&
				devDst == (DWORD)D3DBLEND_ONE &&
				devOp  == (DWORD)D3DBLENDOP_ADD);
			bool reconcile = (gate.pass && !alreadyAdditive);
			if(reconcile){
				glowPreBlendEn = devBlend;
				glowPreSrcBlend = devSrc;
				glowPreDstBlend = devDst;
				glowPreBlendOp = devOp;
				glowContractForced = true;	// ARM before the first write
				pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE, TRUE);
				pipeDeviceStateSet(D3DRS_SRCBLEND, (DWORD)D3DBLEND_ONE);
				pipeDeviceStateSet(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ONE);
				pipeDeviceStateSet(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			}
			// ---- FIX B: fail-CLOSED normal-alpha for non-glow blend-0 -----
			// DEFECT (Nov-25 street batch): crash/damage smoke puff quads
			// (and the grey-box cars they occlude) rasterise with device
			// ALPHABLENDENABLE=0 at flush entry — the DefPipe entry contract
			// leaves blend OFF (render_callbacks.cpp:359-360 'leaving them
			// behind is the repair'; pipelinecommon.cpp:2416-2434
			// canonicalises the L2=1/L3=0 split to OFF) — so the puff's
			// authored alpha is never applied and the quad REPLACES the
			// background as an opaque grey square. The gate above correctly
			// fails OPEN for it (family=0, window=0) — that protects
			// glyph/HUD lanes whose authored blend is already right — but
			// blend==0 is never an authored *alpha* choice for a sprite
			// batch: it means 'alpha was never turned on'. Mirror the
			// fail-closed arm the postfx white path has (blend==0 ->
			// repair) for the general non-glow case: force the CANONICAL
			// NORMAL-ALPHA block (ALPHABLENDENABLE=TRUE, SRCBLEND=SRCALPHA,
			// DESTBLEND=INVSRCALPHA, BLENDOP=ADD) through the same 3-layer
			// chokepoint for THIS draw only.
			// GATES (all required): glowContractPending (so spriteBatch>0 =
			// non-empty batch, AND the lever-OFF else-branch — which is only
			// reachable when config!=NULL && spriteFlushShield==0, so the
			// config==NULL escape hatch keeps failing open by construction),
			// entry devBlend==0 (blend!=0 non-glow stays authored: glyphs/
			// HUD untouched), !gate.pass (entry blend==0 + glow family is
			// FIX A's reconcile above — mutually exclusive), and NOT
			// front/camera (isFrontOrCameraRaster: a screenblit's blend-0
			// output is an opaque screen copy — never repaired, never
			// white-shielded, same verdict PostFxFlush_701FE9_hook uses).
			// Snapshot -> ARM -> force reuses the glowPre* PODs and
			// glowContractForced, so the existing __finally replays the
			// pre-flush block verbatim on every path — no new SEH fence,
			// no new locals outside POD (C2712-safe).
			bool failClosed = false;
			if(!reconcile && !gate.pass && devBlend == 0 &&
			   !isFrontOrCameraRaster(gate.ras)){
				failClosed = true;
				glowPreBlendEn = devBlend;
				glowPreSrcBlend = devSrc;
				glowPreDstBlend = devDst;
				glowPreBlendOp = devOp;
				glowContractForced = true;	// ARM before the first write
				pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE, TRUE);
				pipeDeviceStateSet(D3DRS_SRCBLEND, (DWORD)D3DBLEND_SRCALPHA);
				pipeDeviceStateSet(D3DRS_DESTBLEND, (DWORD)D3DBLEND_INVSRCALPHA);
				pipeDeviceStateSet(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			}
			// ONE new receipt for FIX B: names the entry state the opaque
			// square inherited and the repair taken. First hit unstarvable
			// (dbglog_once), then the 2s bucket; FNV-1a 1024-clean vs the
			// project's 473 tags (checked at skygfx.h:122 before adding).
			// Supersedes the 'fail open' wording of the root-cause receipt
			// above for THIS class: the gate skipped the glow reconcile, but
			// the fail-closed arm then repaired the blend.
			if(failClosed &&
			   (dbglog_once("ptlightglow_fc1") || dbglog_throttle("ptlightglow_fc")))
				dbglog("[PtLightGlow] non-glow blend0 fail-closed: flush entry "
				       "blend=0 src=%d dst=%d op=%d ras=%p nverts=%d family=%d "
				       "window=%d — canonical normal-alpha (TRUE/SRCALPHA/"
				       "INVSRCALPHA/ADD) pushed L1+L2+L3 for this draw only, "
				       "pre-flush block restored after (screenblit excluded, "
				       "blend!=0 non-glow untouched)",
				       (int)devSrc, (int)devDst, (int)devOp, (void*)gate.ras,
				       nverts, gate.family ? 1 : 0, gate.window ? 1 : 0);
			// ONE new receipt for this fix: names the root cause (the entry
			// state the deferred batch inherited) and which gate leg matched.
			// First hit unstarvable (dbglog_once), then the 2s bucket.
			if(dbglog_once("ptlightglow_rc1") || dbglog_throttle("ptlightglow_rc"))
				dbglog("[PtLightGlow] root-cause %s: flush entry blend=%d src=%d dst=%d op=%d "
				       "family=%d window=%d ras=%p — deferred glow batch rasterises after "
				       "the producer's ONE/ONE was overwritten by the world normal-alpha "
				       "(spriteFlushShield=0 path); %s",
				       reconcile ? "reconciled"
				         : (gate.pass ? "already-additive" : "skip-non-glow"),
				       (int)devBlend, (int)devSrc, (int)devDst, (int)devOp,
				       gate.family ? 1 : 0, gate.window ? 1 : 0, (void*)gate.ras,
				       reconcile ? "producer ONE/ONE pushed L1+L2+L3, pre-flush block restored after the draw"
				         : (gate.pass ? "already on the producer pair — no write"
				                      : "not a glow-family batch — authored blend kept (fail open)"));
		}
		SpriteFlushIndexedDraw_orig(primType, verts, nverts, indices, nindices);
	}__finally{
		if(g_inCloudsRender && cloudsPS && d3d9device){
			RwD3D9SetPixelShader(NULL);
			d3d9device->SetPixelShader(NULL);
		}
		// Root-cause reconcile undo (spriteFlushShield=0 path): replay the
		// EXACT pre-flush blend block on all three layers so the forced
		// ONE/ONE can never leak past its own draw. Deliberately verbatim —
		// the entry state belonged to whoever wrote it last, and this hook
		// must change nothing outside its own rasterisation (that is what
		// keeps the lever a real bisect switch instead of a hidden shield).
		// Runs on the faulted path too (same __finally contract as the close).
		if(glowContractForced){
			glowContractForced = false;
			pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE,
			                   glowPreBlendEn ? TRUE : FALSE);
			pipeDeviceStateSet(D3DRS_SRCBLEND, glowPreSrcBlend);
			pipeDeviceStateSet(D3DRS_DESTBLEND, glowPreDstBlend);
			pipeDeviceStateSet(D3DRS_BLENDOP, glowPreBlendOp);
		}
		// Close the force on EVERY path that ran it — NOT gated on
		// g_coronaBodyDone. This is what keeps cloud/sun/water/HUD/deferred
		// flushes from leaving the corona block in the rw cache for the rest
		// of the frame. The window-close resync below is a separate
		// canonical push and still runs for its own producers.
		if(spriteForced)
			spriteFlushShieldClose(spriteForcePath);
	}
	// Window close: the body owner arms g_coronaBodyDone (CCoronas::Render
	// after its trampoline, CPointLights BEFORE its orig call so its own tail
	// flush closes the window). The flush that rasterises the SP-deferred
	// corona batch — or the street-lamp cone/dot batch — closes the window
	// here. POD writes; runs for every flush user.
	if(g_coronaBodyDone){
		g_inCoronasRender = false;
		g_coronaBodyDone = false;
		// ---- CANONICAL RE-SYNC at window-close (L1+L2+L3) ----
		// The park-and-set shield above leaves every layer holding the
		// corona additive block (blend on, ONE/ONE, ZWRITE FALSE).
		// Any later consumer whose entry contract EQUALS that cached
		// value would no-op and inherit it, and any raw writer in
		// between re-creates the cache<->device split that produced the
		// black cones. Push the canonical world signature — normal-alpha
		// blending: blend ON + SRCALPHA(5)/INVSRCALPHA(6) + ZWRITE TRUE,
		// VERTEXALPHA matching the pushed blend state, BLENDOP ADD,
		// ZTEST on (the same push the corona exit-restore performs for
		// L1+L2) — through L1 + L2 + raw L3, so every later consumer
		// starts cache-consistent: its own differing sets TRANSITION
		// instead of no-op'ing against a stale cache. Throttled tag
		// "corona_resync" (fires at most once per 2s).
		// INI spriteFlushShield (default 1 = ON): 0 = clear the window flags
		// only and skip this canonical push (bisect lever for the re-sync).
		if(!config || config->spriteFlushShield){
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			Transparency_SetZEnabled(true);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			Transparency_SetZEnabled(true);
			if(d3d9device){
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
				d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				Transparency_SetZEnabled(true);
			}
			// N2 dead-depth repair: this canonical re-sync pushes ZTEST=1 /
			// ZWRITE=1 but historically omitted ZFUNC, leaving zfunc=8 live —
			// complete the depth contract (cheap early-out when already held).
			spriteEnforceDepthContract("corona_resync");
			if(dbglog_throttle("corona_resync"))
				dbglog("[CoronaResync] window closed — canonical world "
				       "signature pushed L1+L2+L3 (blend=1 src=SRCALPHA "
				       "dst=INVSRCALPHA op=ADD zwrite=1 vtxalpha=1 ztest=1)");
		}
	}
	// ---- FIX A2: deferred CPointLights window close ---------------------
	// Sibling of the g_coronaBodyDone block above: the body-scoped
	// g_inPointLightsRender leg survives its producer's return (see the
	// flag's block comment) so the flush that finally rasterises a
	// SilentPatch-deferred cone/dot batch still evaluates gate.window=1 and
	// reconciles the ONE/ONE contract. This is that flush — consume the
	// hand-off here, AFTER the draw, so the extended window covers exactly
	// one rasterisation and can never shield the next producer's sprites.
	// Mid-body flushes do not reach this branch (g_pointLightBodyDone is
	// still false until body exit), which is what preserves FIX A.
	// POD writes; runs for every flush user on both normal and fault paths
	// only insofar as the close sits after the SEH fence.
	// spriteBatch > 0 is the file's own producer-ownership test (read at hook
	// entry, before orig drains it): consuming the hand-off only on a flush
	// that actually carries an undrained shared batch means an EMPTY flush
	// (the flush's own second draw site after 0x70CF76, or any already-
	// drained site) can never spend the window before the deferred batch
	// reaches a rasteriser.
	if(g_pointLightBodyDone && spriteBatch > 0){
		g_pointLightBodyDone = false;
		g_inPointLightsRender = false;
		if(dbglog_throttle("ptlightdefer"))
			dbglog("[PtLightGlow] deferred window closed at flush "
			       "batch=%d nverts=%d — gate leg released after the "
			       "rasterisation it was kept open for",
			       spriteBatch, nverts);
	}
}

// ---- CPointLights::Render (0x7002D0) entry — shield-window re-arm -------
// CPointLights::Render draws the street-lamp FOG CONES (call 0x700817,
// fogType 1-2) + warm glow dots (call 0x700B6B) through the SAME buffered
// sprite flush 0x70CF20 the corona batch rides (draw sites 0x70CF62/
// 0x70CF9B), and its own entry contract (decoded 0x7002E7-0x700346) is
// byte-identical to CCoronas::Render's: FOGENABLE=0 ZWRITE=0 ZTEST=1
// VERTEXALPHA=1 SRCBLEND=ONE DESTBLEND=ONE. With SilentPatchSA deferring
// the corona flush, the shield window closed at the FIRST post-corona-body
// flush — so the later CPointLights cone batch drew UNSHIELDED, inheriting
// whatever stale device state the last raw writer left (field log:
// [CoronaDraw] pre-force blend=0 src=5 dst=6 = REPLACE -> black cones).
// Re-arm the SAME window here (per-window flags; the re-arm overwrites
// whatever the corona window left — no interaction with the corona hook's
// own lifecycle, which always re-arms at its own entry): every CPointLights
// flush is then inside the shield, and the EXISTING flush-close logic in
// SpriteFlushIndexedDraw_hook closes the window + runs the canonical
// re-sync on that flush. CPointLights' tail flush
// (0x700BCD routes through 0x70CF20) fires DURING the orig call, so
// g_coronaBodyDone is armed BEFORE the call — the tail flush itself now
// closes the window, closing the body's own draw path instead of leaving
// the window open until the NEXT frame's flush. Plain call through the
// saved pointer: Render is a normal function (reads the global registry
// 0xC3F0E0, no this-use).
static void (*CPointLights__Render_orig)(void);
static void
CPointLights__Render_shield_hook(void)
{
	// SAFETY-CLEAR (stuck-flag guard) + RE-ARM — same pattern as the
	// corona hook entry. Plain POD writes on the render thread; no flush
	// draw can interleave here.
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	g_inCoronasRender = true;
	// Body START, not body end: the tail flush runs INSIDE the orig call, so
	// a post-call assignment left that flush (and every draw it rasterised)
	// inside the shield while the window stayed open past the body. POD
	// write; left set even if orig faults, so the next flush still closes.
	g_coronaBodyDone = true;
	// FIX A: body-scoped gate leg for the fog-cone batch (see the flag's
	// block comment). Spans the WHOLE orig call — unlike g_coronaBodyDone,
	// which the first mid-body overflow flush consumes — so every flush that
	// can rasterise cone quads evaluates gate.window=1. Set here (entry),
	// handed to the flush at body exit below (FIX A2). POD write.
	g_inPointLightsRender = true;
	g_pointLightBodyDone = false;	// FIX A2: no deferred flush pending yet
	// ---- [PtLightColor] wave-6b diagnostic receipt (test-7, 2026-11-01) ----
	// Discriminates "lights REGISTERED black" from "blend broken at draw":
	// the glow-sprite colour is read straight from this registry by the
	// 0x70E780 wrapper (r/g/b = mult*color[i]+add, add=16 when fogType!=1),
	// so a walk of the registry BEFORE the orig body is the authoritative
	// pre-draw colour record. If the registry shows saturated warm colours
	// while the screen still shows black quads, the defect is device blend
	// state at rasterisation (the flush shields above are the fix); if the
	// registry itself holds zeros, the producer (CPointLights::Add) is the
	// bug. Cheap: 2s throttle + run's first hit unstarvable (dbglog_once),
	// walk capped at the registry capacity (32) and only the first 8
	// entries logged individually. Pure reads — no render state touched.
	{
		static bool s_plColorFirst = true;
		if(s_plColorFirst || dbglog_throttle("ptlight_color")){
			s_plColorFirst = false;
			int n = *(int*)0xC3F0D0;
			if(n < 0) n = 0;
			if(n > 32) n = 32;	// registry capacity (see Forward+ layout note)
			if(n == 0){
				dbglog("[PtLightColor] registry EMPTY count=0 "
				       "(no glow sprites can draw this frame)");
			}else{
				int black = 0;
				for(int i = 0; i < n; i++){
					const PointLight &pl = pointLights[i];
					if(pl.color[0] <= 0.01f && pl.color[1] <= 0.01f &&
					   pl.color[2] <= 0.01f)
						black++;
					if(i < 8)
						dbglog("[PtLightColor] [%d] type=%d fog=%d "
						       "color=(%.2f,%.2f,%.2f) radius=%.1f",
						       i, (int)pl.type, (int)pl.fogType,
						       pl.color[0], pl.color[1], pl.color[2],
						       pl.radius);
				}
				dbglog("[PtLightColor] registry count=%d black=%d "
				       "(black==count => lights registered black = "
				       "producer bug; saturated colors + black quads "
				       "on screen => blend broken at draw)",
				       n, black);
			}
		}
	}
	if(CPointLights__Render_orig)
		CPointLights__Render_orig();
	// FIX A2: hand the body-scoped gate leg over to the FLUSH instead of
	// dropping it here. The tail flush (unconditional jmp 0x700BCD) drains
	// every cone/dot quad inside orig on the vanilla path, but SilentPatchSA
	// defers the shared batch, so a batch can still be undrained when orig
	// returns — clearing the leg here is what produced window=0 at the
	// rasterising flush. Keep g_inPointLightsRender set and flag the body
	// done; SpriteFlushIndexedDraw_hook's window-close block consumes both
	// on the first flush after this point (or CoronaWindow_Close does at the
	// frame boundary). POD write. If orig faulted, the leg stays set until
	// the next entry re-arm or CoronaWindow_Close — same bounded stuck-flag
	// contract as g_inCoronasRender.
	g_pointLightBodyDone = true;
	if(dbglog_once("ptlightdefer"))
		dbglog("[PtLightGlow] body exit hands the gate leg to the flush "
		       "(deferred-batch window): window stays 1 until the next "
		       "0x70CF20 flush with a non-empty shared batch");
}

// ---- Producer discrimination for the two wave-6b TempBuffer flush sites ---
// 0x701FE9 (postfx TempBuffer writer) and 0x7273D0 (call site 0x719B2B) draw
// whatever their caller accumulated into the shared 2D TempBuffer
// (0xC5F958 verts / 0xC4B958 indices). Those are NOT corona-only lanes: the
// game's CSprite2d / HUD / font / 2D-text batch drains through the same quad
// buffer, and the field log proves it — cloud_flush hit 1407x with a
// non-empty batch in 2 s while the game sat at the menu, far too hot for
// cloud rendering; that is the 2D text lane. Forcing the ONE/ONE additive
// block on such a batch rasterises glyph quads additively = washed-out
// "fonts completely broken". The force is therefore applied ONLY when the
// batch's stage-0 texture identity is one of the game's corona / pointlight
// / cloud glow rasters; every unknown producer FAILS OPEN (skip, no state
// write) instead of being corrupted.
// Family globals (clouds-ffp-audit §2.3 / corona diagnosis §1.2):
//   0xC3E000 gpCoronaTexture[0] / cloud glow A
//   0xC3E008 multiplicative pass glow texture
//   0xC6AA74 sun corona texture
//   0xC6AA78 shared 2D-sprite glow (CPointLights, cloud batch #2)
static RwTexture *
coronaFamilyTexture(int i)
{
	switch(i){
	case 0: return *(RwTexture**)0xC3E000;
	case 1: return *(RwTexture**)0xC3E008;
	case 2: return *(RwTexture**)0xC6AA74;
	case 3: return *(RwTexture**)0xC6AA78;
	default: return NULL;
	}
}
static const int CORONA_FAMILY_TEXTURE_COUNT = 4;

static bool
sameRasterFamily(RwRaster *a, RwRaster *b)
{
	if(!a || !b)
		return false;
	if(a == b)
		return true;
	// Glow textures can be bound through a parent or a sub-raster depending
	// on the producer path.
	if(a->parent && a->parent == b)
		return true;
	if(b->parent && b->parent == a)
		return true;
	return false;
}

// Capture the batch's stage-0 identity:
//   L1 rw cache -> RwRenderStateGet(rwRENDERSTATETEXTURERASTER)  [VERDICT]
//   L3 device   -> GetTexture(0) (AddRef; caller releases devTex) [diag only]
// The verdict is L1-ONLY. ORing the raw device layer into it was the font-
// regression false positive of 2026-11-01: the sprite flush runs 5
// instructions before the font flush and leaves the glow texture on device
// stage 0 while the rw cache has already moved to the font atlas, so every
// glyph batch tested as "corona family" — log proof: `[cloud_flush] gate
// shielded=23116 skipped=0`, zero rejections in 23,116 invocations, and every
// menu glyph rasterised as a solid rectangle (docs/baseline/2026-11-01-fonts/
// findings.md). The device read stays for receipts only.
struct FlushBatchId
{
	RwRaster *ras;
	IDirect3DBaseTexture9 *devTex;
	bool family;
};

static FlushBatchId
flushBatchIdentity(void)
{
	FlushBatchId id;
	id.ras = NULL;
	id.devTex = NULL;
	id.family = false;
	void *rawRas = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rawRas);
	if((void*)rawRas == (void*)0xFFFFFFFF)
		rawRas = NULL;	// failed-Get sentinel (same convention as postfx.cpp)
	id.ras = (RwRaster*)rawRas;
	if(d3d9device)
		d3d9device->GetTexture(0, &id.devTex);	// AddRef
	for(int i = 0; i < CORONA_FAMILY_TEXTURE_COUNT && !id.family; i++){
		RwTexture *tex = coronaFamilyTexture(i);
		// Defensive: the family globals are .data slots that are NULL before
		// the TXDs load; never follow a non-pointer. A bad slot is skipped
		// (fail open) rather than faulting the render thread.
		if(!tex || (uintptr_t)tex < 0x10000u || ((uintptr_t)tex & 3u))
			continue;
		if(sameRasterFamily(id.ras, tex->raster)){
			id.family = true;
			break;
		}
		// Device-layer OR test REMOVED (2026-11-02 audit): the verdict is
		// strictly L1-only. ORing the raw device texture into it was the
		// font-regression false positive of 2026-11-01 — the sprite flush
		// runs 5 instructions before the font flush and leaves the glow
		// texture on device stage 0 while the rw cache has already moved to
		// the font atlas, so every glyph batch tested as "corona family"
		// (23,116/23,116 shielded, zero rejections — see the block comment
		// above). id.devTex stays a diagnostics-only receipt field; the
		// caller Releases it after logging.
	}
	return id;
}

// ---- Screen-copy discrimination (white-quad fix, 2026-11-17) -------------
// The unknown-producer white branch of PostFxFlush_701FE9_hook used to fire
// on ANY blend-0 batch whose stage-0 raster was not corona-family -- including
// the postfx writer's own SCREEN BLIT: family=0 with the FRONT BUFFER bound at
// stage 0 (field log: `[texflush] white family=0 ras=075D0A38 nverts=720
// nidx=1080`, 46x per run after a mode engage; ras=075D0A38 = pRasterFrontBuffer,
// the 2048x2048 pRasFB of the [PostFX-REPORT] receipt). That batch samples the
// FRAMEBUFFER ITSELF, so its blend-0 output is an opaque screen copy -- never
// the black slab of the black-quad class (a screen raster cannot shade to
// black) -- and the white bind + ONE/ONE shield painted it WHITE: giant white
// quads, plus a per-draw [SpriteDepthFix] as the close healed the blit's
// depth-off entry state. Such batches now take the blend1 skip (left alone,
// zero state touched; receipt below).
// The genuine black-quad case is UNAFFECTED: a glow raster is never the front/
// camera raster, so family!=0 keeps its shield and family=0 + glow/unknown
// raster keeps the white+shield path exactly as-is.
// TRUE when ras IS the front buffer or the camera raster, through the named
// layer only (CPostEffects::pRasterFrontBuffer = pRasFB; Scene.camera's raster
// = camRas -- same read postfx.cpp:414 uses). No raw address sites.
// Verdict is L1-only, the single-source discipline flushBatchIdentity uses:
// ORing the device layer in is the documented font false-positive of
// 2026-11-01. sameRasterFamily also matches a parent/sub-raster bind.
static bool
isFrontOrCameraRaster(RwRaster *ras)
{
	if(!ras)
		return false;
	RwRaster *fb = CPostEffects::pRasterFrontBuffer;
	if(fb && sameRasterFamily(ras, fb))
		return true;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(camRas && sameRasterFamily(ras, camRas))
		return true;
	return false;
}

// ---- Glow-family verdict for the flush-entry blend reconcile -------------
// Definition sits here (beside flushBatchIdentity / isFrontOrCameraRaster)
// because those helpers are declared AFTER SpriteFlushIndexedDraw_hook in
// this file; the hook reaches them through the forward declaration at the
// top of the flush block. Verdict rule is the producer-ownership triple the
// rest of the tree already trusts — a POSITIVE stage-0 glow-raster match
// (strictly L1: the device-layer OR is the documented 2026-11-01 font false
// positive), or a live producer window (CCoronas/CPointLights =
// g_inCoronasRender, CClouds = g_inCloudsRender, plus the FIX A
// body-scoped CPointLights leg g_inPointLightsRender that covers the
// fog-cone batch for the WHOLE RenderFogEffect body — the first two are
// the same triple effectsForceAlphaBlock uses to keep its canonical
// normal-alpha sweep off glow batches). Anything else returns pass=false
// and the caller leaves the batch's authored blend untouched — glyph/font/
// HUD/screen-blit lanes stay on SRCALPHA/INVSRCALPHA, and the blend!=0
// fail-open in PostFxFlush_701FE9_hook is untouched. id.devTex is
// diagnostics-only (GetTexture AddRef) and is released here, exactly as the
// other caller releases it after logging.
static SpriteBlendContract
spriteBlendContract(void)
{
	SpriteBlendContract gate;
	FlushBatchId id = flushBatchIdentity();
	gate.family = id.family;
	gate.window = (g_inCoronasRender || g_inCloudsRender ||
	               g_inPointLightsRender);
	gate.ras = id.ras;
	gate.pass = (id.family || gate.window);
	if(id.devTex)
		id.devTex->Release();
	return gate;
}

// ---- Post-draw stage-0 restore for the white-shield bind ----------------
// The unknown-producer white path in PostFxFlush_701FE9_hook rebinds stage 0
// through all three layers (L1 park/set, L2 RwD3D9SetTexture, L3 raw device
// pin) for its own rasterisation. spriteFlushShieldClose only replays RENDER
// STATES -- it has no texture field -- so without this the white bind leaked
// into every later draw (the next consumer that cache-gates its bind inherits
// gpWhiteTexture = white silhouettes). Same park-and-set L1 + raw L3 sequence
// as main.cpp restoreStage0Tex (CloudShield R5, 2026-11-09), whose comment
// documents why the RwTexture layer cannot be replayed here: only the raster
// (L1) and the captured device texture (L3, the identity GetTexture AddRef
// held alive by the caller) are available. Park on a guaranteed-different
// raster first so the set is always a transition, never a cache-hit no-op.
// Silent by design: the ONE new receipt for this fix is the [texflush]
// skip-screenblit line in the hook.
static void
postfxRestoreStage0Tex(RwRaster *savedTexRaster, IDirect3DBaseTexture9 *savedDevTex0)
{
	if(!d3d9device)
		return;
	RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
		? gpWhiteTexture->raster : NULL;
	RwRaster *texPark = (savedTexRaster == whiteRas) ? (RwRaster*)NULL : whiteRas;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)texPark);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)savedTexRaster);
	// Raw L3 pin only with a captured texture: a failed identity Get leaves
	// the pointer NULL and SetTexture(0, NULL) would clear a valid bind we
	// cannot see. The L1 set above still re-anchors the rw cache either way.
	if(savedDevTex0)
		d3d9device->SetTexture(0, savedDevTex0);
}

// ---- Wave-6b: the TWO unshielded inline TempBuffer flush sites ----------
// Test-7 (2026-11-01 18:49, Xbox mode C-nibble 0x2): giant black wedge with
// hard diagonals, near-total world blackout, while the 0x70CF20 flush shield
// was ACTIVE (sprite_flush gating shielded=44 skipped=0) and the statesync
// blend heal kept recurring — the black quads persist anyway. Root cause:
// TWO MORE draw sites rasterise TempBuffer batches OUTSIDE the 0x70CF20
// flush hook, with a broken device blend, and were left vanilla in wave-6:
//
//   (a) 0x701FE9 — inside the postfx TempBuffer writer (function 0x701780,
//       called from CPostEffects::Render 0x7046E0 at 0x705116). Byte-
//       verified (exe 1.0 US): E8 B2 2E 03 00 -> 0x734EA0, cdecl 5-arg
//       (primType 3 TRISTRIP, verts 0xC5F958, nverts [0xC4B950], indices
//       0xC4B958, nindices [0xC4B954]) — the SAME draw primitive the shared
//       flush sites call, but with the writer's OWN count globals
//       0xC4B950/0xC4B954 (TempBufferVerticesStored/IndicesStored — NOT the
//       shared 0xC6A158). The caller gates the call on 0xC4B950 > 0
//       (test eax,eax; jz @ 0x701FC5..) and resets the count AFTER the call
//       (0x701FF9 mov dword [0xC4B950],0), so the count is still live at
//       hook entry — producer-ownership test identical to the 0x70CF20 gate.
//
//   (b) 0x7273D0 — cloud-region companion flush, called at 0x719B2B right
//       after the cloud-region 0x70CF20 flush (0x719B26). Byte-verified:
//       0x7273D0 is a FUNCTION ENTRY (not a call site): reads its own
//       indices count [0xC8045C], returns when <= 0 (A1 5C 04 C8 00 / 85 C0
//       / 7E 42), then draws 0x734EA0 with verts 0xC5F958 / nverts
//       [0xC80458] / indices 0xC4B958 / nindices [0xC8045C] and zeroes both
//       counts before ret. Hooked via its CALL SITE 0x719B2B (E8 A0 D8 00
//       00 -> 0x7273D0, verified) with a void(void) hook — the count gate
//       at entry mirrors the function's own early-out exactly.
//
// Both sites draw unshielded with a broken device blend = the black light
// quads covering the scene. Fix: the SAME capture -> force -> close shield
// pattern as the 0x70CF20 forced path (spriteFlushShieldCapture /
// coronaDrawRawShield(force=true, ONE/ONE) / spriteFlushShieldClose), gated
// per site on ITS OWN count global > 0. Non-nesting with the shared-flush
// shield: each hook closes its force before returning, and neither orig
// draw re-enters another hooked flush (0x734EA0 is the raw primitive).
// INI spriteFlushShield=0 disables both forces (bisect lever, same as the
// 0x70CF20 gate).
static SpriteFlushIndexedDraw_t PostFxFlush_701FE9_orig;
static void __cdecl
PostFxFlush_701FE9_hook(int primType, void *verts, int nverts, void *indices, int nindices)
{
	// Producer-ownership gate input: the writer's own vert count, read at
	// hook entry BEFORE the caller's post-call reset (the reset is at
	// 0x701FF9, after this call returns).
	int batch = *(int*)0xC4B950;
	static bool s_gateReceipt = false;
	static unsigned s_shielded = 0, s_skipped = 0;
	if(!s_gateReceipt){
		s_gateReceipt = true;
		dbglog("[postfx_flush] producer gating active: shield only on "
		       "0xC4B950>0 AND stage-0 = corona-family glow raster "
		       "(unknown producer -> blend0=white-shield / blend1=skip)");
	}
	const char *forcePath = NULL;
	bool forced = false;
	// White-path stage-0 restore state (postfxRestoreStage0Tex). Armed ONLY by
	// the unknown-producer white bind below: it captures the pre-bind L1 raster
	// and holds its OWN AddRef on the identity GetTexture pointer (so the
	// receipt-time Release of id.devTex never frees the texture the __finally
	// restore still needs, and [postfx_flush_id] keeps printing the real
	// s0DevTex value), then restores stage-0 right after the draw. PODs --
	// SEH-safe inside __try/__finally.
	bool stage0WhiteBound = false;
	RwRaster *savedStage0Ras = NULL;
	IDirect3DBaseTexture9 *savedStage0DevTex = NULL;
	// SEH latch guard (same as SpriteFlushIndexedDraw_hook), armed BEFORE
	// the first state mutation: this fence used to start only around the
	// orig draw at the bottom, but the gate/white-bind block below mutates
	// render state (white stage-0 bind, spriteFlushShieldCapture,
	// coronaDrawRawShield force) and runs receipts / the identity Release
	// BEFORE that old fence — a fault there propagated out of this hook
	// with the force parked (ZWRITE=0 + ONE/ONE on all three layers) and
	// s_spriteForceValid latched true forever (the effectsForceAlphaBlock
	// producer gate then never runs again), and the white stage-0 bind
	// stayed bound: neither restore in the __finally below would ever run.
	// One fence now covers the mutation span AND the orig draw; the
	// __finally closes the force and hands stage-0 back on BOTH the normal
	// and the faulted path, exactly as before. All locals in scope here are
	// POD (no unwinding) — C2712-safe.
	__try{
	if(batch > 0 && d3d9device && (!config || config->spriteFlushShield)){
		// Producer discrimination (fail-open gate): the shared TempBuffer is
		// ALSO the game's 2D text/HUD/font lane. Only a corona/pointlight/
		// cloud glow batch may receive the ONE/ONE additive force; a glyph
		// batch keeps its authored blend (additive = washed-out text).
		FlushBatchId id = flushBatchIdentity();
		const char *texAction = "skip";	// [texflush] receipt action (unknown producer)
		if(id.family){
			if(dbglog_once("postfx_flush_force")){
				int blend = -1, src = -1, dst = -1;
				if(d3d9device){
					DWORD v = 0;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
				}
				dbglog("[probe] postfx_flush 0x701FE9 force=1 pre blend=%d src=%d dst=%d "
				       "(black-wedge producer when not blend=1)",
				       blend, src, dst);
			}
			spriteFlushShieldCapture();
			coronaDrawRawShield("PostFxFlush_701FE9", true);
			forcePath = "PostFxFlush_701FE9/postfx";
			forced = true;
		}else{
			// P0-2 black-quad fail-open fix (docs/plans/2026-11-09-visual-
			// triage.md #4): an UNKNOWN producer must not draw un-shielded.
			// The black-quad class leaves the device blend DISABLED (blend=0
			// = opaque black slabs) and rasterises whatever broken stage-0
			// texture is live. Discriminate on the device blend: blend==0 is
			// the black-quad class -> bind neutral white + the SAME ONE/ONE
			// shield force the known family gets (turns the opaque slab into
			// a neutral shielded sprite). blend!=0 is the font/HUD glyph lane
			// (SRCALPHA/INVSRCALPHA) which MUST NOT receive the ONE/ONE force
			// (the 2026-11-01 font-regression class) -> it keeps its authored
			// blend (fail open, no force).
			DWORD devBlend = 1;	// pre-seed blend-ON (fail open = no force)
			if(d3d9device)
				d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &devBlend);
			// ---- SCREEN-COPY GATE (white-quad fix, 2026-11-17) ----
			// family=0 with the front/camera raster bound at stage 0 is a
			// screen blit, not a glow sprite (see isFrontOrCameraRaster): it
			// must NOT be white-shielded. Take the same path the blend1 lane
			// takes -- leave the draw completely alone (no white bind, no
			// capture/force/close, so no [SpriteDepthFix] churn either) --
			// and name the decision in one throttled receipt. The blend-0
			// black-quad repair below is untouched for every other raster.
			if(isFrontOrCameraRaster(id.ras)){
				if(dbglog_once("texflush_screenblit_first") ||
				   dbglog_throttle("texflush_screenblit"))
					dbglog("[texflush] skip-screenblit family=0 ras=%p nverts=%d",
					       (void*)id.ras, nverts);
			}else if(devBlend == 0){
				// Arm the post-draw stage-0 restore BEFORE the first bind
				// write: the 3L white bind below is NOT covered by
				// spriteFlushShieldClose (states only), so a fault mid-bind
				// must still hand the pre-bind raster/texture back — the arm
				// used to run AFTER the bind, leaving that window unguarded.
				// id.ras is the pre-bind L1 raster (flushBatchIdentity read it
				// before any shield write). savedStage0DevTex takes its OWN
				// AddRef so the identity ref stays on id.devTex's normal
				// Release line below ([postfx_flush_id] keeps printing the
				// real s0DevTex); the __finally restore Releases this ref.
				savedStage0Ras = id.ras;
				if(id.devTex){
					id.devTex->AddRef();
					savedStage0DevTex = id.devTex;
				}
				stage0WhiteBound = true;
				// (a) bind neutral white through all three layers (park-and-
				// set L1, L2 driver, raw L3) so the batch samples a defined
				// white instead of the leaked black raster, then run the SAME
				// capture -> force -> close shield as the known family. The
				// close sees the captured blend-0 and rewrites it to the
				// canonical normal-alpha block (spriteFlushShieldClose's
				// REPLACE path), healing the leak for the next consumer.
				if(gpWhiteTexture){
					RwRaster *whiteRas = gpWhiteTexture->raster;
					if(whiteRas){
						RwRaster *parkRas =
							(id.ras == whiteRas) ? (RwRaster*)NULL : whiteRas;
						RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
						RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)whiteRas);
					}
					RwD3D9SetTexture(gpWhiteTexture, 0);
					// Raw L3 pin (render_callbacks.cpp:514-528 idiom): the
					// black-quad leak is a cache<->device split, so bypass
					// the driver cache or a cache hit can skip the write.
					if(whiteRas && d3d9device){
						DWORD extOff = RwD3D9RasterExtOffset;
						if(extOff > 0 && extOff < 0x100000){
							IDirect3DTexture9 *cand =
								*(IDirect3DTexture9**)((uint8*)whiteRas + extOff);
							if(cand && (uintptr_t)cand >= 0x10000u &&
							   (((uintptr_t)cand & 3u) == 0))
								d3d9device->SetTexture(0, cand);
						}
					}
				}
				spriteFlushShieldCapture();
				coronaDrawRawShield("PostFxFlush_701FE9", true);
				forcePath = "PostFxFlush_701FE9/postfx-white";
				forced = true;
				texAction = "white";
			}
		}
		// Per-site receipt: the batch's stage-0 identity + whether the force
		// ran. First hit of the run is unstarvable so a short log still
		// names the producer; then throttled.
		if(dbglog_once("postfx_flush_id_first") || dbglog_throttle("postfx_flush_id"))
			dbglog("[postfx_flush_id] 0x701FE9 batch=%d s0Raster=%p s0DevTex=%p "
			       "family=%d force=%d (family->shield / unknown blend0->white / unknown blend1->skip)",
			       batch, (void*)id.ras, (void*)id.devTex,
			       id.family ? 1 : 0, forced ? 1 : 0);
		// [texflush] receipt: names the UNKNOWN producer's raster + counts so
		// the next log identifies it. white = shielded black-quad (a), skip =
		// font/HUD kept authored (fail open). family=0 = !id.family only.
		if(!id.family &&
		   (dbglog_once("texflush_first") || dbglog_throttle("texflush")))
			dbglog("[texflush] %s family=0 ras=%p nverts=%d nidx=%d",
			       texAction, (void*)id.ras, nverts, nindices);
		if(id.devTex)
			id.devTex->Release();	// GetTexture AddRef
	}
	if(forced)
		s_shielded++;
	else
		s_skipped++;
	if(dbglog_throttle("postfx_flush_gate"))
		dbglog("[postfx_flush] gate shielded=%u skipped=%u "
		       "(skipped = 0xC4B950<=0 at entry, spriteFlushShield=0, "
		       "or unknown stage-0 producer)",
		       s_shielded, s_skipped);
		// Orig draw — inside the mutation-span fence armed above (the
		// capture/close pair brackets it; a swallowed fault still runs the
		// __finally restores below).
		if(PostFxFlush_701FE9_orig)
			PostFxFlush_701FE9_orig(primType, verts, nverts, indices, nindices);
	}__finally{
		if(forced)
			spriteFlushShieldClose(forcePath);
		// White-path stage-0 handback (runs on the faulted path too, like the
		// close above): the white bind lasted exactly this draw's raster, then
		// the pre-bind raster/device texture go back so the leak class the
		// 3L white bind would otherwise create never reaches the next draw.
		if(stage0WhiteBound){
			postfxRestoreStage0Tex(savedStage0Ras, savedStage0DevTex);
			if(savedStage0DevTex)
				savedStage0DevTex->Release();	// this path's own AddRef
		}
	}
}

static void (*CloudRegionFlush_7273D0_orig)(void);
static void
CloudRegionFlush_7273D0_hook(void)
{
	// ==================================================================
	// 0x7273D0 IS THE FONT GLYPH FLUSH — NOT A CLOUD/REGION FLUSH.
	// Proven twice independently (2026-11-01):
	//   * 0x7273D0's ONLY caller is CFont::RenderFontBuffer (0x719840);
	//     its counts 0xC80458/0xC8045C are written only by the glyph
	//     appender 0x728200 (docs/plans/2026-11-01-sa-font-path.md).
	//   * Screenshot forensics: every menu glyph drew as a SOLID FILLED
	//     RECTANGLE at correct advance width, zero AA shades, 101px tall
	//     (docs/baseline/2026-11-01-fonts/findings.md).
	// The corona additive block (ONE/ONE, ZWRITE off, ALPHATEST GE@0) MUST
	// NEVER be forced here: it discards glyph coverage alpha. This lane is
	// pure FFP (no pixel shader) and needs SRCALPHA/INVSRCALPHA.
	//
	// Positive duty instead: the preceding 0x70CF20 sprite-flush close
	// replays SRC/DEST blend a few instructions before this call, and
	// CFont::RenderBuffer only sets VERTEXALPHAENABLE — so glyphs INHERIT
	// the corona/REPLACE pair. Pin the canonical glyph blend through all
	// three layers for the draw and hand the entry block back afterwards.
	// That makes text correct regardless of what leaked, and it is the
	// reason this hook stays installed.
	// ==================================================================
	int batch = *(int*)0xC8045C;
	static bool s_gateReceipt = false;
	static unsigned s_pinned = 0, s_empty = 0, s_fast = 0;
	if(!s_gateReceipt){
		s_gateReceipt = true;
		dbglog("[font_flush] 0x7273D0 = CFont::RenderFontBuffer glyph flush — "
		       "corona force DISABLED here; pinning SRCALPHA/INVSRCALPHA "
		       "(glyph coverage preserved)");
	}

	if(batch <= 0 || !d3d9device){
		s_empty++;
		if(dbglog_throttle("font_flush_gate"))
			dbglog("[font_flush] gate pinned=%u empty=%u (empty = batch<=0 "
			       "or no device — no state touched)", s_pinned, s_empty);
		if(CloudRegionFlush_7273D0_orig)
			CloudRegionFlush_7273D0_orig();
		return;
	}

	// ---- PERF R5: entry==canonical fast path ---------------------------
	// Read the three device-layer blend states ONCE. When the entry block
	// already IS the canonical glyph blend (ALPHABLENDENABLE on +
	// SRCALPHA/INVSRCALPHA — e.g. the preceding 0x70CF20 sprite flush left
	// exactly the pair the glyphs inherit), the park-and-set + verbatim
	// restore below is a state no-op: skip it entirely (3 reads + compare
	// instead of ~30 L1/L2/L3 ops per glyph flush) and just run the orig.
	// Any other entry state takes the full capture + pin + restore path,
	// unchanged. Any nonzero ALPHABLENDENABLE is TRUE to D3D9.
	DWORD eDevBlend = 1, eDevSrc = D3DBLEND_SRCALPHA, eDevDst = D3DBLEND_INVSRCALPHA;
	// N2: the fast path must ALSO hold the depth contract (ZTEST=1, ZFUNC=
	// LESSEQUAL) — a glyph flush inheriting zfunc=8 would otherwise skip the
	// repair and exit with dead depth live.
	// ZWRITE is captured (L1+L2+L3) but deliberately NOT part of the fast-
	// path test: the glyph Im2D draw legitimately parks ZWRITE OFF while it
	// rasterises, and the exit replay hands the captured entry value back on
	// both paths (the whole-flush contract: OFF during the draw, restored).
	DWORD eDevZen = 1, eDevZFunc = D3DCMP_LESSEQUAL;
	DWORD eDevZWrite = TRUE, eDrvZWrite = TRUE;
	RwUInt32 eRwZWrite = (RwUInt32)TRUE;
	bool devReadOk = false;
	if(d3d9device){
		devReadOk =
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &eDevBlend)) &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND,         &eDevSrc))  &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND,        &eDevDst)) &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZENABLE,          &eDevZen))  &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZFUNC,            &eDevZFunc));
		// ZWRITE entry snapshot for the exit replay. A failed Get keeps the
		// pre-seeded TRUE (the world-canonical value).
		(void)d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &eDevZWrite);
		(void)RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &eDrvZWrite);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &eRwZWrite);
	}
	if(devReadOk && eDevBlend != 0 &&
	   eDevSrc == D3DBLEND_SRCALPHA && eDevDst == D3DBLEND_INVSRCALPHA &&
	   eDevZen != 0 && eDevZFunc == D3DCMP_LESSEQUAL){
		s_fast++;
		if(dbglog_throttle("font_flush_gate"))
			dbglog("[font_flush] fast path: entry already canonical "
			       "(pinned=%u fast=%u empty=%u) — no state touched",
			       s_pinned, s_fast, s_empty);
		if(CloudRegionFlush_7273D0_orig)
			CloudRegionFlush_7273D0_orig();
		// ZWRITE replay: the glyph draw parked it OFF device-side; restore
		// the captured entry value so the next consumer inherits it. The
		// fast path skipped the pin/restore, so this leg cannot be skipped.
		spriteRestoreZWrite(eDevZWrite, eDrvZWrite, eRwZWrite);
		// N2 dead-depth repair: the glyph Im2D draw inside orig re-asserts
		// ZTEST off + ZFUNC ALWAYS (zfunc=8) even when the ENTRY was already
		// canonical (which is exactly why the fast path was taken). The
		// fast path skipped the pin/restore, so it must still enforce the
		// depth contract on exit or the next consumer inherits dead depth.
		// Cheap early-out: two device reads; a no-op once the device holds
		// the contract.
		spriteEnforceDepthContract("CloudRegionFlush_7273D0");
		return;
	}

	// ---- entry capture (all three layers) ----
	// (device-layer values already read above for the fast-path probe)
	DWORD eDrvBlend = eDevBlend, eDrvSrc = eDevSrc, eDrvDst = eDevDst;
	(void)RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &eDrvBlend);
	(void)RwD3D9GetRenderState(D3DRS_SRCBLEND,         &eDrvSrc);
	(void)RwD3D9GetRenderState(D3DRS_DESTBLEND,        &eDrvDst);
	void *eRwSrc = (void*)rwBLENDSRCALPHA, *eRwDst = (void*)rwBLENDINVSRCALPHA;
	// N1 L1 vtxAlpha leak: the slow path pins L1 VERTEXALPHAENABLE TRUE
	// (below) but the restore never handed it back — L1 stayed TRUE while
	// L2/L3 were restored, the [PipeAlpha]/rs_mon DIVERGE split. Capture it
	// alongside src/dst (pre-seeded canonical so a failed Get is sane).
	void *eRwVtx = (void*)TRUE;
	RwRenderStateGet(rwRENDERSTATESRCBLEND,  &eRwSrc);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &eRwDst);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &eRwVtx);
	if((void*)eRwSrc == (void*)0xFFFFFFFF) eRwSrc = (void*)rwBLENDSRCALPHA;
	if((void*)eRwDst == (void*)0xFFFFFFFF) eRwDst = (void*)rwBLENDINVSRCALPHA;
	if((void*)eRwVtx == (void*)0xFFFFFFFF) eRwVtx = (void*)TRUE;

	// ---- pin canonical glyph blend: park-and-set so every L1 write is a
	//      real transition (never a value-compare no-op), then L2+L3 ----
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDONE);          // park
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDSRCALPHA);     // target
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);         // park
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);  // target
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	d3d9device->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
	d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	if(dbglog_once("font_flush_first"))
		dbglog("[font_flush] first glyph batch=%d entryDev(blend=%lu src=%lu dst=%lu) "
		       "entryDrv(blend=%lu src=%lu dst=%lu) -> pinned SRCALPHA/INVSRCALPHA",
		       batch, (unsigned long)eDevBlend, (unsigned long)eDevSrc, (unsigned long)eDevDst,
		       (unsigned long)eDrvBlend, (unsigned long)eDrvSrc, (unsigned long)eDrvDst);

	if(CloudRegionFlush_7273D0_orig)
		CloudRegionFlush_7273D0_orig();

	// ---- hand the entry block back verbatim, all three layers ----
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, eDrvBlend ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND,         eDrvSrc);
	RwD3D9SetRenderState(D3DRS_DESTBLEND,        eDrvDst);
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  eRwSrc);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, eRwDst);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, eRwVtx);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, eDevBlend ? TRUE : FALSE);
	d3d9device->SetRenderState(D3DRS_SRCBLEND,         eDevSrc);
	d3d9device->SetRenderState(D3DRS_DESTBLEND,        eDevDst);

	// ZWRITE: replay the captured entry value — the glyph draw parked it OFF
	// device-side and the blend restore above never covered it, so that leak
	// used to survive the flush.
	spriteRestoreZWrite(eDevZWrite, eDrvZWrite, eRwZWrite);

	// N2 dead-depth repair: the glyph Im2D draw re-asserts ZTEST off + ZFUNC
	// ALWAYS (zfunc=8) and the blend restore above never covers the depth
	// trio — enforce the canonical depth contract on exit (cheap early-out
	// when the device already holds it).
	spriteEnforceDepthContract("CloudRegionFlush_7273D0");

	s_pinned++;
	if(dbglog_throttle("font_flush_gate"))
		dbglog("[font_flush] gate pinned=%u empty=%u (glyph blend pinned, "
		       "entry block restored)", s_pinned, s_empty);
}

// ---- CFont::RenderFontBuffer (0x719840) exit — depth-contract enforce ----
// CFont::RenderFontBuffer IS the game caller that owns the whole font/HUD
// 2D window: inside its body it runs the 0x70CF20 sprite flush (0x719B26)
// and THEN our hooked 0x7273D0 glyph flush (0x719B2B —
// CloudRegionFlush_7273D0_hook, which enforces the depth contract at ITS
// exit). The glyph flush's empty path (batch <= 0) returns WITHOUT that
// enforce, and anything the font/HUD 2D path parks AFTER our glyph-flush
// enforce (the Im2D depth pair: ZWRITE=0 / ZENABLE=0 / ZFUNC=ALWAYS — the
// N2 dead-depth repair this file documents at spriteEnforceDepthContract)
// escapes RenderFontBuffer to the next pipe entry as dead depth. Field
// evidence (log 2026-11-17 08:21, the pre-9b1256a build): 234x
// `[zfix] pipe entry depth drift repaired ... dev zwrite=0 zen=0 zfunc=8`
// out of 490 [zfix] receipts, recurring ~2s while the same frames' glyph
// flushes report their own repairs ([SpriteDepthFix] CloudRegionFlush_7273D0
// ... enforcing) — i.e. the drift re-appears INSIDE the window between our
// glyph-flush enforce and RenderFontBuffer's return (or on a path where
// that enforce never ran). Wrapping the game's call sites so the enforce
// ALSO runs when RenderFontBuffer returns closes the window no matter
// which internal path ran — belt-and-braces, same doctrine as
// spriteEnforceDepthContract's own header comment.
// Signature: void(void) — disassembled from the 1.0 US exe: function entry
// 0x719840 (mov eax,[guard]; sub esp,0x18; cmp; je) through plain `ret` at
// 0x719B3E, no call site pushes an argument, and the body overwrites ecx
// immediately (not thiscall), so the wrapper forwards nothing.
static void (*CFont_RenderFontBuffer_orig)(void);
static void
CFont_RenderFontBuffer_hook(void)
{
	// Plain call-through: orig runs the full body — sprite flush, glyph
	// flush, guard store, epilogue — and the enforce below then covers
	// every exit path, including the glyph flush's empty path that skips
	// its own enforce. Cheap: spriteEnforceDepthContract early-outs on two
	// device reads once the device already holds the contract.
	DWORD eDevZw = TRUE, eDrvZw = TRUE;  RwUInt32 eRwZw = (RwUInt32)TRUE;
	if(d3d9device){
		(void)d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &eDevZw);
		(void)RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &eDrvZw);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &eRwZw);
	}
	if(CFont_RenderFontBuffer_orig)
		CFont_RenderFontBuffer_orig();
	bool carried = !(eRwZw && eDrvZw && eDevZw);
	if(carried){ eDevZw = eDrvZw = TRUE; eRwZw = (RwUInt32)TRUE; }
	spriteRestoreZWrite(eDevZw, eDrvZw, eRwZw);
	spriteEnforceDepthContract("CFont_RenderFontBuffer");
	if(carried && (dbglog_once("font_zw1") || dbglog_throttle("font_zw")))
		dbglog("[FontZ] RenderFontBuffer entry ZWRITE not canonical "
		       "rw=%u drv=%u dev=%u -> canonical TRUE pushed at exit",
		       (unsigned)eRwZw, (unsigned)eDrvZw, (unsigned)eDevZw);
}

// ---- Frame-boundary safety close for the corona/cone shield window ------
// SilentPatchSA defers the corona batch past CCoronas::Render's return, so a
// window can legitimately outlive the hook. Any window still open when a
// frame boundary is reached is stale (the deferred flush never came) and
// would shield unrelated sprites next frame. Hard-clear the flags and, when
// the entry capture ran, put the pre-window ALPHAFUNC/ALPHAREF back through
// L1+L2+L3 (the CCoronas exit restores all four states; this is the fault-
// path / frame-boundary net for the alpha-test pair). The BLEND block is NOT
// replayed from the capture: it is pinned to the canonical world normal-alpha
// state, because the entry capture can itself be a REPLACE (blend=0)
// snapshot and replaying it re-installs the black-quad signature.
void
CoronaWindow_Close(void)
{
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	// FIX A stuck-flag net: if CPointLights orig faulted before its
	// post-call clear, the body-scoped gate leg survives to the frame
	// boundary — clear it here with the other window flags (POD write).
	// FIX A2: the deferred hand-off flag rides along so a stale leg can
	// never be released by a flush next frame.
	g_inPointLightsRender = false;
	g_pointLightBodyDone = false;
	// R3 (H1 residual): complete the depth contract on EVERY close, capture
	// or not. The effects-exit close consumes the snapshot, so the LAST
	// close before the 2D/HUD tail (and the next frame's scene_start) runs
	// with g_coronaPreValid already FALSE — it used to return before any
	// repair, letting the Im2D tail's device-only ZTEST=0 / ZFUNC=ALWAYS
	// leak survive into scene_start. spriteEnforceDepthContract is the one
	// canonical depth-contract definition (ZENABLE/ZFUNC through the shared
	// pipeDeviceStateSet chokepoint). ZWRITE is NOT forced here: the
	// captured branch below replays its captured value (the genuine corona
	// contract), and a no-capture close is not a shield close — the world
	// baseline owns ZWRITE on that path. The restore below never writes the
	// depth pair, so running the net first is equivalent to running it last
	// and also covers the early return. Cheap two-read early-out when the
	// device already holds the contract.
	spriteEnforceDepthContract("CoronaWindow_Close");
	if(!g_coronaPreValid)
		return;	// no capture (window never entered) — depth net already ran
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)g_coronaPreFuncRw);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)g_coronaPreRefRw);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
	// ALPHABLENDENABLE / SRCBLEND / DESTBLEND / BLENDOP are pinned to the
	// CANONICAL world normal-alpha block — never replayed from the capture:
	// the field log proves the device can be on REPLACE (blend=0) when the
	// corona window opens, and replaying that snapshot re-installed the
	// black-quad signature at every frame boundary. Canonical: blend TRUE,
	// SRCALPHA(5)/INVSRCALPHA(6), BLENDOP ADD. ZWRITEENABLE keeps its
	// captured value (the genuine corona contract — captured, never forced
	// to 1); the ZENABLE/ZFUNC pair is completed canonically by the depth
	// net at the top of this close (it runs before the capture check, so
	// the no-snapshot frame boundary is covered too), so the depth contract
	// cannot drift out of the frame boundary either. Every state below is
	// park-and-set (a guaranteed cache transition) then the canonical
	// target, with raw L3 last to repair device drift the caches cannot see.
	DWORD canonBlend = TRUE;
	DWORD canonSrc = (DWORD)D3DBLEND_SRCALPHA;
	DWORD canonDst = (DWORD)D3DBLEND_INVSRCALPHA;
	DWORD canonOp  = (DWORD)D3DBLENDOP_ADD;
	DWORD preZw  = g_coronaPreZWrite ? (DWORD)TRUE : (DWORD)FALSE;
	// ALPHABLENDENABLE: park FALSE -> canonical TRUE.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	// SRCBLEND / DESTBLEND: park ZERO (guaranteed different) -> canonical.
	DWORD parkSrc = (DWORD)D3DBLEND_ZERO;	// canonSrc is SRCALPHA
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(parkSrc));
	RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(canonSrc));
	RwD3D9SetRenderState(D3DRS_SRCBLEND, canonSrc);
	DWORD parkDst = (DWORD)D3DBLEND_ZERO;	// canonDst is INVSRCALPHA
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(parkDst));
	RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(canonDst));
	RwD3D9SetRenderState(D3DRS_DESTBLEND, canonDst);
	// ZWRITEENABLE: park the boolean inverse -> captured target.
	DWORD parkZw = preZw ? (DWORD)FALSE : (DWORD)TRUE;
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)parkZw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, parkZw);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)preZw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, preZw);
	// BLENDOP: no rw twin — L2 + raw L3 only. Park REVSUBTRACT -> ADD.
	DWORD parkOp = (DWORD)D3DBLENDOP_REVSUBTRACT;
	RwD3D9SetRenderState(D3DRS_BLENDOP, parkOp);
	RwD3D9SetRenderState(D3DRS_BLENDOP, canonOp);
	// SEPARATEALPHABLENDENABLE / COLORWRITEENABLE (raw-only, no rw twin):
	// coronaDrawRawShield parks SEPARATEALPHABLENDENABLE=0 and
	// COLORWRITEENABLE=0xF per draw. The CCoronas exit restore consumes the
	// captured values, but that exit runs BEFORE the SilentPatch-deferred
	// draws, so this frame-boundary close must also replay them or the
	// parked values leak into the next frame. L2 + raw L3 only.
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, canonBlend);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, canonSrc);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, canonDst);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, preZw);
		d3d9device->SetRenderState(D3DRS_BLENDOP, canonOp);
		d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
		d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
	}
	if(dbglog_once("corona_window_close"))
		dbglog("[CoronaExit] frame-boundary close: canonical-blend restore "
		       "(captured blend=%d src=%d dst=%d op=%d -> forced blend=1 "
		       "src=SRCALPHA(5) dst=INVSRCALPHA(6) op=ADD; zwrite=%d captured)",
		       (int)g_coronaPreBlend, (int)g_coronaPreSrcBlend,
		       (int)g_coronaPreDstBlend, (int)g_coronaPreBlendOp,
		       (int)preZw);
	// Snapshot consumed: the next corona window captures afresh at its entry
	// hook. Without this the FIRST-EVER snapshot was replayed at every frame
	// boundary, overwriting pipeApplyWorldBaseline.
	g_coronaPreValid = false;
}

// ---- CCoronas::Render (0x6FAEC0) entry blend shield + exit probe ----
// Black-quad symptom (user-confirmed): every light sprite/corona draws as
// a BLACK QUAD + dark overlay — the signature of coronas blending with
// normal alpha (SRCALPHA/INVSRCALPHA) instead of vanilla additive
// (ONE/ONE): the glow texture's RGB=0 background must add nothing, while
// a 5/6 pair writes the opaque-alpha black surround.
// H3 rework: the old probe intercepted the single vanilla call site
// (RenderEffects 0x53E18E) and never logged once in a 17k-line session.
// Now hooked at the CCoronas::Render ENTRY POINT so it fires regardless
// of caller (cutscene + gameplay paths alike), via a 20-byte trampoline:
// the stolen prologue (sub esp,0xB8 / mov eax,[0xC1703C] / mov
// ecx,[eax+0x60] / mov eax,[ecx+0xC] / mov ecx,[ecx+0x10] — all
// position-independent, verified against the 1.0 US exe) runs from an RWX
// copy and jmps back to 0x6FAED4, so the original body's ret lands back
// in this hook.
//
// SHIELD (entry, BEFORE the renderer): read the live device blend state
// (GetRenderState is ground truth — the RwD3D9 wrapper cache can
// disagree). If it looks like the broken normal-alpha case — blend ON
// with DESTBLEND=INVSRCALPHA, or blend OFF entirely — i.e. NOT additive —
// force vanilla corona additive (ALPHABLENDENABLE=TRUE, SRCBLEND=ONE,
// DESTBLEND=ONE, BLENDOP=ADD, ZWRITEENABLE=FALSE) through all three
// layers (rw cache / RwD3D9 driver cache / raw device). Seeding the rw
// cache with ONE/ONE matches the renderer's own rwBLENDONE/rwBLENDONE
// sets; those may now no-op against an unchanged cache, but the raw L3
// pin has already delivered the same pair, so the device can never stay
// on the 5/6 split — the cache-noop/device-drift mechanism behind the
// black quads is still closed by the raw mirror.
// Deliberately non-additive corona types (modulate-blended shadow /
// night-light passes) set their own blend INSIDE CCoronas::Render after
// this point, so an entry-state force cannot clobber them; an entry
// state that is already additive (dst=ONE) is left untouched — the
// shield only fires on the broken normal-alpha signature.
//
// ZTEST ENFORCEMENT (always-on-top round): the vanilla body sets ZTEST
// itself at 0x6FB17C (the coronaZtest patch site: 1=PC `call [ecx+0x20]`
// re-patched, 0=PS2 NOP) through the rw cache ONLY — a raw device
// ZENABLE=FALSE left by any upstream writer (DepthHook Suspend, postfx
// raw geom states, foreign mods) no-ops that set against an
// already-TRUE rw cache, and coronas draw through geometry. The shield
// now reads D3DRS_ZENABLE as a 7th probe state and repairs the device
// to the coronaZtest target (0=PS2 off, 1/-1=PC vanilla on) through all
// three layers — inside the broken-blend force AND standalone when the
// blend is healthy but ZTEST drifted (the always-on-top symptom can
// occur without the blend signature). Body-internal ZTEST sets still
// transition normally afterwards (entry force only syncs the layers).
// Throttle tag "corona_shield" (new bucket — the shared per-frame
// buckets kept the old probe's window permanently shut).
//
// PROBE (after the original): this is the state the corona quads
// actually drew with (vanilla sets its blend right before drawing).
// Kept from the breadcrumb rounds; read-only.
static void (*CCoronas__Render_trampoline)(void);
static void
CCoronas__Render_blendprobe_hook(void)
{
	// POD locals only; the shared pre-hook alpha-test capture is hoisted to
	// function scope (outside BOTH __try blocks — C2712-safe) so the exit
	// restore fence below can restore it. Two SEH fences: the first wraps
	// shield + trampoline + probe, the second wraps ONLY the exit restore —
	// a fault inside the first (e.g. the trampoline) is swallowed there and
	// can no longer skip the restore.
	DWORD preAtest = TRUE;	// contract default (alpha test on) if never captured
	// SAFETY-CLEAR (stuck-flag guard): expire ANY window that never reached
	// a flush — e.g. the previous frame's trampoline faulted (bodyDone
	// unset) or no sprite consumer flushed since. Plain POD writes on the
	// render thread; no flush draw can interleave here. If the previous
	// window's deferred batch is still pending, its first flush after this
	// entry re-arm is simply unshielded (accepted defensive loss).
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	// Draw-time raw shield scope ON from this point. With SilentPatchSA
	// loaded the vanilla tail-flush is gone (0x6FB621 word-patched to RET),
	// so the flag SURVIVES this function's exit; SpriteFlushIndexedDraw_hook
	// closes the window at the first flush after the body (g_coronaBodyDone).
	g_inCoronasRender = true;
	__try{
		// ---- CORONA BLEND SHIELD (entry, BEFORE the renderer) ----
		if(d3d9device){
			DWORD blendEn = 0, srcB = 0, dstB = 0, bop = 0, zw = 0, atest = 0;
			DWORD ztest = 0;
			DWORD afn = 0, aref = 0, sepA = 0, cwr = 0;
			int rwFn = rwALPHATESTFUNCTIONGREATEREQUAL;
			int rwRef = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &bop);
			d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
			d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &atest);
			d3d9device->GetRenderState(D3DRS_ZENABLE, &ztest);
			// Pre-window alpha-test + raw-only states: the ones the per-draw
			// shield (coronaDrawRawShield) now overwrites inside the window.
			// Captured here — both layers for func/ref, device only for the
			// two states that have no rw twin — and put back at the exit and
			// by CoronaWindow_Close.
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &afn);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &aref);
			d3d9device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &sepA);
			d3d9device->GetRenderState(D3DRS_COLORWRITEENABLE, &cwr);
			RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &rwFn);
			RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &rwRef);
			g_coronaPreFuncRw = rwFn;
			g_coronaPreRefRw = rwRef;
			g_coronaPreFuncDev = afn;
			g_coronaPreRefDev = aref;
			g_coronaPreSepAlpha = sepA;
			g_coronaPreColorWrite = cwr;
			// Pre-window BLEND block: the exit restore / CoronaWindow_Close
			// put ALPHABLENDENABLE back to what the device held here instead
			// of hard-setting FALSE (the frame-long REPLACE leak). The other
			// four are already probed above; capturing them costs nothing and
			// makes the exit log report the true pre-window state.
			g_coronaPreBlend = blendEn;
			g_coronaPreSrcBlend = srcB;
			g_coronaPreDstBlend = dstB;
			g_coronaPreBlendOp = bop;
			g_coronaPreZWrite = zw;
			g_coronaPreValid = true;
			preAtest = atest;	// pre-hook value — restored at exit (see below)
			// Broken normal-alpha signature: blend live with
			// DESTBLEND=INVSRCALPHA, or blend disabled entirely.
			// Already-additive (dst=ONE) is left untouched.
			DWORD broken = blendEn ?
				(dstB == (DWORD)D3DBLEND_INVSRCALPHA) : TRUE;
			// ZTEST target from the coronaZtest config (clamped [-1..1]
			// at INI load, main.cpp readIniFile): 0 = PS2 (z-test off),
			// 1/-1 = PC vanilla (z-test on). Drift = device disagrees.
			int ztTarget = (config && config->coronaZtest == 0) ? FALSE : TRUE;
			int ztestDrift = ((int)ztest != ztTarget);
			// ---- NORMALIZE (always — additive or not): blend-op only ----
			// ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/COLORWRITEENABLE
			// moved to coronaDrawRawShield: they are applied PER DRAW,
			// immediately before each rasterisation inside the window, and
			// the pre-window values captured above are restored at the exit.
			// The old once-per-window force here never restored them, leaking
			// GREATEREQUAL@0 / a blanking colour mask / separate-alpha into
			// the rest of the frame. BLENDOP has no rw render state, so it
			// rides L2+L3; the additive blend FACTORS are not touched here.
			RwD3D9SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			d3d9device->SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			if(broken){
				// ---- PARK-AND-SET (pipeForceCullMode idiom,
				// pipelinecommon.cpp:1114): for ALPHABLENDENABLE /
				// SRCBLEND / DESTBLEND / ZWRITEENABLE write the OPPOSITE
				// value through L1 (rw cache) + L2 (driver cache) FIRST —
				// a guaranteed cache TRANSITION even when a cache already
				// holds the target (the double cache-hit no-op that left
				// L3 stale at blend=0) — then the ONE/ONE target through
				// L1+L2, then raw L3 (repairs drift the caches can't see).
				// No draw happens between park and set, so the transient
				// never reaches a rasteriser. Seeding the rw cache with
				// ONE/ONE matches the renderer's own additive sets; their
				// cache-hit no-op is harmless because L3 is already ONE/ONE.
				//
				// ALPHABLENDENABLE: park FALSE -> TRUE
				RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				// SRCBLEND: park INVSRCALPHA -> ONE (vanilla additive)
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVSRCALPHA);
				RwD3D9SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_INVSRCALPHA);
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
				RwD3D9SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_ONE);
				// DESTBLEND: park ZERO -> ONE (additive)
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
				RwD3D9SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ZERO);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
				RwD3D9SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ONE);
				// ZWRITEENABLE: park TRUE -> FALSE
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				// ZTEST alongside the additive force (coronaZtest
				// wiring) — same three layers, only on drift.
				if(ztestDrift){
					Transparency_SetZEnabled(ztTarget ? true : false);
				}
				// First-hit one-shot (unthrottled) + the 2s bucket line, so
				// the run's first [CoronaShield] force can never be starved.
				if(dbglog_once("corona_shield_first") || dbglog_throttle("corona_shield"))
					dbglog("[CoronaShield] forced additive ONE/ONE, was src=%d dst=%d blend=%d op=%d zw=%d atest=%d ztest=%d->%d",
					       (int)srcB, (int)dstB, (int)blendEn, (int)bop,
					       (int)zw, (int)atest, (int)ztest, ztTarget);
			}else if(ztestDrift){
				// Blend healthy but ZTEST drifted — standalone
				// repair (same three layers), so the always-on-top
				// symptom cannot hide behind a correct blend state.
				Transparency_SetZEnabled(ztTarget ? true : false);
				if(dbglog_throttle("corona_ztest"))
					dbglog("[CoronaZ] forced ztest=%d, was=%d (coronaZtest=%d)",
					       ztTarget, (int)ztest,
					       config ? config->coronaZtest : -99);
			}
		}
		if(CCoronas__Render_trampoline)
			CCoronas__Render_trampoline();
		// Body complete — arm the flush hook's window-close: the next
		// buffered flush (SilentPatch defers the corona flush past this
		// return) draws the pending corona batch and then closes the
		// window. Left FALSE if the trampoline faulted, so a fault path
		// still closes the window at the next flush (POD write, SEH-safe).
		g_coronaBodyDone = true;
		// Probe AFTER orig: this is the state the corona quads actually
		// drew with (vanilla sets its blend right before drawing).
		if(d3d9device){
			DWORD blendEn = 0, srcB = 0, dstB = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
			if(dbglog_throttle("corona_probe2"))
				dbglog("[CoronaBlend] dev blend=%d src=%d dst=%d (D3DBLEND: 1=ZERO 2=ONE 4=INVSRCALPHA 5=SRCALPHA 6=INVSRCALPHA; REPLACE = src=ONE dst=ZERO blend=0)",
					(int)blendEn, (int)srcB, (int)dstB);
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// shield + trampoline + read-only probe diagnostics — never
		// propagate a fault into the corona render path. The exit restore
		// below sits in its OWN fence, so a fault here still runs it.
	}
	__try{
		// ---- EXIT RESTORE (corona-exit leak fix) ----
		// Vanilla CCoronas::Render (0x6FAEC0) exits restoring NOTHING
		// (gta-reversed: "NOTE/BUG: Renderstates not restored?") — it leaves
		// the corona additive block (ONE/ONE + VERTEXALPHA TRUE + ZWRITE
		// FALSE) live in every layer, and the next consumer without an
		// entry contract of its own (default-pipe atomics: props/pickups/
		// markers) drew with it. Mirror the restore vanilla
		// CCoronas::RenderReflections (0x6FB630) performs: canonical
		// normal-alpha + depth-on + blend-op ADD + alpha-test back to the
		// pre-hook value, through L1 (rw cache) + L2 (RW driver cache).
		// The blend family (ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP) is
		// ALSO pinned raw on L3 below: relying on the L2 set alone to mark
		// pending[] dirty and let RW's pre-draw flush deliver the canonical
		// state fails when applied[] ALREADY holds the canonical values —
		// pending[] == applied[], the flush value-compares into a no-op, and
		// the additive ONE/ONE the corona body forced survives on L3 under
		// caches claiming blend-off (the scene_start DIVERGE signature
		// ALPHABLEND rw=0 dev=1). The unconditional raw L3 pin is the only
		// layer that applied[]-gated flush can never repair.
		// (The captured-state restore below also re-pins the four shield-
		// overwritten states raw.)
		// BLENDOP has no rw render state and the shield forced ADD through
		// L2/L3 (entry ~2354); ALPHATESTENABLE likewise has none and the
		// shield only reads it — restore the captured pre-hook value
		// (preAtest, TRUE default) so the next consumer inherits exactly
		// what CCoronas::Render found. One-shot UNTHROTTLED log (tag
		// "corona_exit" via a plain static bool — a throttled bucket starved
		// this line for ~20 s in the 19:06 run).
		{
			static bool coronaExitLogged = false;	// zero-init POD, C2712-safe
			// ALPHABLENDENABLE / VERTEXALPHAENABLE: restore the PRE-WINDOW
			// device value captured at this hook's entry. The old hard FALSE
			// (REPLACE) is exactly what leaked blend-off into the rest of the
			// frame and painted the next flush's sprites as black quads; the
			// fallback is TRUE when no capture ran (g_coronaPreBlend init).
			DWORD preBlendDev = g_coronaPreBlend ? (DWORD)TRUE : (DWORD)FALSE;
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)preBlendDev);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			Transparency_SetZEnabled(true);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, preBlendDev);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, preAtest);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			// Raw L3 completion of the normal-alpha block (blend family +
			// ZWRITE): the driver cache can already claim these canonical
			// values while the device still holds the additive ONE/ONE/TRUE
			// block the corona body forced — the L2 sets above value-compare
			// into a no-op (pending[] == applied[]), so RW's pre-draw flush
			// never delivers them and the device survives additive (the
			// scene_start DIVERGE signature ALPHABLEND rw=0 dev=1). Pin the
			// device explicitly on every state the caches above claim.
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, preBlendDev);
			d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}
		Transparency_SetZEnabled(true);
		// N2 residual: ZENABLE is pushed TRUE above but ZFUNC is never
		// re-asserted — a zfunc=8 (ALWAYS) device survives the corona exit
		// and dead-depth-tests the next consumer. UNIFY: reuse the single
		// depth-contract helper (spriteEnforceDepthContract, main.cpp:3681)
		// instead of writing a second ZFUNC block; it cheaply early-outs
		// when the device already holds ZTEST=1 / ZFUNC=LESSEQUAL.
		spriteEnforceDepthContract("CCoronas_exit");
		// ---- pre-window ALPHAFUNC/ALPHAREF + raw-only states ----
			// The four states the per-draw shield overwrites. Restored to the
			// values captured at this hook's entry: func/ref through L1 (rw
			// cache) + L2 + raw L3; SEPARATEALPHABLENDENABLE and
			// COLORWRITEENABLE have no rw twin, so L2 + L3 only.
			// ALPHABLENDENABLE is instead restored to the captured pre-window
			// device value above; SRCBLEND/DESTBLEND keep the canonical
			// normal-alpha pair (the normal-alpha restore above owns them).
			if(g_coronaPreValid){
				RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)g_coronaPreFuncRw);
				RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)g_coronaPreRefRw);
				RwD3D9SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
				RwD3D9SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
				RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
				RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
				if(d3d9device){
					d3d9device->SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
					d3d9device->SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
					d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
					d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
				}
			}
			// One-shot, UNTHROTTLED (dbglog_throttle starved this line for
			// ~20 s in the 19:06 run): the first [CoronaExit] of the run
			// must always be visible.
			if(!coronaExitLogged){
				coronaExitLogged = true;
				dbglog("[CoronaExit] restored normal-alpha "
				       "(src=SRCALPHA dst=INVSRCALPHA op=ADD zwrite=1 "
				       "ztest=1) + pre-window blend=%d vtxalpha=%d src=%d "
				       "dst=%d op=%d zw=%d atest=%d fn=%d ref=%d "
				       "sepA=%u cwr=0x%X after CCoronas::Render",
				       (int)g_coronaPreBlend, (int)preBlendDev,
				       (int)g_coronaPreSrcBlend, (int)g_coronaPreDstBlend,
				       (int)g_coronaPreBlendOp, (int)g_coronaPreZWrite,
				       (int)preAtest, (int)g_coronaPreFuncRw,
				       (int)g_coronaPreRefRw,
				       (unsigned)g_coronaPreSepAlpha,
				       (unsigned)g_coronaPreColorWrite);
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// exit-restore containment — the restore is best-effort; a fault
		// inside it must not propagate into the corona render path either
	}
	// NOTE: g_inCoronasRender is deliberately NOT cleared at this exit.
	// With SilentPatchSA loaded the corona batch is flushed AFTER this
	// function returns (SP word-patches the vanilla tail-jmp 0x6FB621 ->
	// RET; vanilla would tail-jmp into the flush inside this window), so
	// the window must survive the return. SpriteFlushIndexedDraw_hook
	// closes it at the first post-body flush (g_coronaBodyDone); the
	// safety-clear at this hook's entry expires any window that never
	// reaches a flush. Both flags are POD and unconditionally re-armed
	// on the next entry, so both SEH fault paths leave a bounded state.
}

// ---- rwFixes_installHooks: the byte-verified installs for all six fix
// machines above, gated on Config.rwFixesEnable (default 1). 0 = skip every
// install (the vanilla path remains; nothing is patched). One-shot receipts:
// "[rwfix] install SKIPPED" / "[rwfix] install OK (N machines)".
// Machine count: (1) corona entry blend shield, (2) sprite flush shield,
// (3) postfx 0x701FE9 flush, (4) cloud/font 0x7273D0 flush, (5) CPointLights
// shield (two call sites), (6) CSprite 0x700B6B pointlight colour fix.
void
rwFixes_installHooks(void)
{
	static bool s_rwfixLogged = false;
	if(!config || !config->rwFixesEnable){
		if(!s_rwfixLogged){
			s_rwfixLogged = true;
			dbglog("[rwfix] install SKIPPED (rwFixesEnable=0)");
		}
		return;
	}
		// 0x6FAEC0  CCoronas::Render ENTRY — corona blend shield + probe
		//           ([CoronaShield] / [CoronaBlend] tags). H3: the old
		//           0x53E18E call-site intercept never logged; hook the
		//           entry point so it fires regardless of caller
		//           (cutscene + gameplay). Entry: forces vanilla additive
		//           blend through all 3 layers when the device sits on the
		//           broken normal-alpha signature (blend on + dst=
		//           INVSRCALPHA, or blend off). After: read-only probe.
		{
			const DWORD coronaEntry = 0x6FAEC0;
			const DWORD coronaStolen = 20;	// 6+5+3+3+3 prologue bytes
			// Pre-install verification (tag corona_entry_install): the RWX
			// trampoline copies these 20 prologue bytes VERBATIM and jumps
			// back to entry+20 (0x6FAED4), so a drifted exe would execute
			// foreign bytes from the copy and/or land the jump-back
			// mid-instruction. Bytes below are read from gta_sa.exe 1.0 US:
			//   81 EC B8 00 00 00   sub  esp, 0xB8
			//   A1 3C 70 C1 00      mov  eax, ds:[0xC1703C]
			//   8B 48 60            mov  ecx, [eax+0x60]
			//   8B 41 0C            mov  eax, [ecx+0x0C]
			//   8B 49 10            mov  ecx, [ecx+0x10]
			// MISMATCH => SKIP the entry hook only. The trampoline pointer is
			// consumed solely by CCoronas__Render_blendprobe_hook (NULL-
			// guarded) and the coronaZtest code patch is a separate site, so
			// the mod still runs — no other subsystem depends on this hook.
			static const BYTE s_coronaEntryExpect[20] = {
				0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00,
				0xA1, 0x3C, 0x70, 0xC1, 0x00,
				0x8B, 0x48, 0x60,
				0x8B, 0x41, 0x0C,
				0x8B, 0x49, 0x10
			};
			BYTE s_coronaEntryActual[20];
			memcpy(s_coronaEntryActual, (const void*)coronaEntry,
				sizeof(s_coronaEntryActual));
			const bool s_coronaEntryOk = (memcmp(s_coronaEntryActual,
				s_coronaEntryExpect, sizeof(s_coronaEntryActual)) == 0);
			char s_coronaEntryHex[sizeof(s_coronaEntryActual) * 3 + 1];
			s_coronaEntryHex[sizeof(s_coronaEntryHex) - 1] = 0;
			for(size_t i = 0; i < sizeof(s_coronaEntryActual); i++)
				snprintf(s_coronaEntryHex + i * 3, 4, "%02X ",
					(unsigned)s_coronaEntryActual[i]);
			dbglog("[corona_entry_install] 0x%08X bytes=%s %s "
			       "(expect 81 EC B8 00 00 00 A1 3C 70 C1 00 8B 48 60 "
			       "8B 41 0C 8B 49 10)",
				(unsigned)coronaEntry, s_coronaEntryHex,
				s_coronaEntryOk ? "OK" : "MISMATCH");
			if(!s_coronaEntryOk){
				dbglog("[corona_entry_install] prologue verify FAILED — "
				       "CCoronas::Render ENTRY hook SKIPPED, vanilla entry "
				       "kept (coronaZtest code patch and all other "
				       "subsystems unaffected)");
			}else{
				BYTE *coronaTramp = (BYTE*)VirtualAlloc(NULL, 32,
					MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if(coronaTramp){
					memcpy(coronaTramp, (void*)coronaEntry, coronaStolen);
					coronaTramp[coronaStolen] = 0xE9;	// jmp entry+stolen
					*(DWORD*)(coronaTramp + coronaStolen + 1) =
						(coronaEntry + coronaStolen) -
						(DWORD)(coronaTramp + coronaStolen + 5);
					CCoronas__Render_trampoline = (void (*)(void))coronaTramp;
					InjectHook(coronaEntry, CCoronas__Render_blendprobe_hook, PATCH_JUMP);
					dbglog("[INIT] CoronaBlend probe: entry 0x6FAEC0 hooked, tramp=%p",
						(void*)coronaTramp);
				}else
					dbglog("[INIT] CoronaBlend probe: VirtualAlloc failed — probe NOT installed");
			}
		}
		// Corona draw-time raw shield: wrap the buffered-sprite flush's two
		// innermost draw entries (0x70CF20 -> 0x734EA0 call sites 0x70CF62 /
		// 0x70CF9B — see SpriteFlushIndexedDraw_hook). These are the draws
		// that rasterise the corona quad batch AND the CPointLights glow
		// batch; since 2026-10-24 the additive shield is forced at the
		// flush boundary (force=true, non-empty batch only, corona/cloud
		// producers only) so the early CPointLights call at 0x53DE6B —
		// which runs outside the CCoronas window — is covered.
		// One-shot install verification (tag corona_install): InterceptCall
		// (MemoryMgr.h) is SILENT on a bad site — InjectHook PATCH_NOTHING
		// just rewrites the rel32 of whatever instruction sits at the
		// address. Both sites must be E8 with extracted target 0x734EA0.
		{
			BYTE op62 = *(BYTE*)0x70CF62, op9B = *(BYTE*)0x70CF9B;
			DWORD t62 = 0, t9B = 0;
			if(op62 == 0xE8) t62 = *(DWORD*)(0x70CF63) + 0x70CF62 + 5;
			if(op9B == 0xE8) t9B = *(DWORD*)(0x70CF9C) + 0x70CF9B + 5;
			bool ok62 = (op62 == 0xE8 && t62 == 0x734EA0);
			bool ok9B = (op9B == 0xE8 && t9B == 0x734EA0);
			dbglog("[corona_install] flush draw sites: 0x70CF62 op=%02X orig=%08X%s | "
			       "0x70CF9B op=%02X orig=%08X%s (expect op=E8 orig=734EA0; "
			       "MISMATCH = flush hook never fires, corona quads unshielded)",
				(unsigned)op62, (unsigned)t62,
				ok62 ? "OK" : "MISMATCH",
				(unsigned)op9B, (unsigned)t9B,
				ok9B ? "OK" : "MISMATCH");
			// GATE the installs on the verification: InterceptCall is SILENT on
			// a bad site and InjectHook PATCH_NOTHING would rewrite the rel32
			// of whatever instruction sits there — on a non-1.0 exe that
			// corrupts unrelated code. On MISMATCH skip BOTH installs; the
			// vanilla flush path remains (coronas may stay unshielded, but
			// nothing gets corrupted).
			if(ok62 && ok9B){
				InterceptCall(&SpriteFlushIndexedDraw_orig, SpriteFlushIndexedDraw_hook, 0x70CF62);
				InterceptCall(&SpriteFlushIndexedDraw_orig, SpriteFlushIndexedDraw_hook, 0x70CF9B);
			}else
				dbglog("[corona_install] site verify FAILED — both flush intercepts SKIPPED, vanilla flush path kept");
		}
		// Wave-6b installs: the TWO unshielded inline TempBuffer flush sites
		// (see PostFxFlush_701FE9_hook / CloudRegionFlush_7273D0_hook above).
		// Test-7 (2026-11-01 18:49): statesync cf_enter blend L2=1 L3=0 heals
		// recur every ~1s (persistent creator), sprite_flush gating
		// shielded=44 skipped=0 (0x70CF20 shield works) — yet black quads
		// persist: these two sites rasterise TempBuffer batches OUTSIDE the
		// 0x70CF20 flush hook. Same byte-verification discipline as
		// corona_install: each site must be E8 with the expected extracted
		// target; on MISMATCH skip that install, the vanilla path remains
		// (site may stay unshielded, but nothing gets corrupted).
		{
			// (a) 0x701FE9 — postfx TempBuffer writer flush (func 0x701780,
			//     called from CPostEffects::Render 0x7046E0 at 0x705116).
			//     Expect E8 B2 2E 03 00 -> 0x734EA0. 5-arg cdecl, same
			//     signature as the shared flush (SpriteFlushIndexedDraw_t).
			BYTE opPF = *(BYTE*)0x701FE9;
			DWORD tPF = 0;
			if(opPF == 0xE8) tPF = *(DWORD*)(0x701FEA) + 0x701FE9 + 5;
			bool okPF = (opPF == 0xE8 && tPF == 0x734EA0);
			dbglog("[postfx_flush_install] 0x701FE9 op=%02X orig=%08X%s "
			       "(expect op=E8 orig=734EA0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, postfx flush unshielded)",
			       (unsigned)opPF, (unsigned)tPF,
			       okPF ? "OK" : "MISMATCH");
			if(okPF)
				InterceptCall(&PostFxFlush_701FE9_orig,
					PostFxFlush_701FE9_hook, 0x701FE9);
			// (b) 0x719B2B — call site of the cloud-region companion
			//     flush 0x7273D0, right after the cloud-region 0x70CF20
			//     flush (0x719B26). Expect E8 A0 D8 00 00 -> 0x7273D0.
			//     void(void) hook — the count gate at hook entry mirrors
			//     the function's own prologue early-out.
			BYTE opCR = *(BYTE*)0x719B2B;
			DWORD tCR = 0;
			if(opCR == 0xE8) tCR = *(DWORD*)(0x719B2C) + 0x719B2B + 5;
			bool okCR = (opCR == 0xE8 && tCR == 0x7273D0);
			dbglog("[cloud_flush_install] 0x719B2B op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7273D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, cloud-region flush unshielded)",
			       (unsigned)opCR, (unsigned)tCR,
			       okCR ? "OK" : "MISMATCH");
			if(okCR)
				InterceptCall(&CloudRegionFlush_7273D0_orig,
					CloudRegionFlush_7273D0_hook, 0x719B2B);
		}
		// CFont::RenderFontBuffer (0x719840) EXIT depth-contract enforce:
		// wrap the game's THREE call sites — 0x57BF70 (HUD/menu caller),
		// 0x719B5D (the sibling CFont entry that begins right after
		// RenderFontBuffer's ret) and 0x719F43 (buffer-drain caller) — so
		// spriteEnforceDepthContract("CFont_RenderFontBuffer") also runs
		// when RenderFontBuffer returns; see CFont_RenderFontBuffer_hook
		// above for the defect this closes. Byte-verification discipline is
		// cloud_flush_install's exactly: each site must be E8 with the
		// extracted target 0x719840; on MISMATCH skip THAT site only (the
		// vanilla call remains — nothing gets corrupted). All three sites
		// extract the same target, so the shared orig pointer is the
		// CPointLights two-site idiom, not a double wrap.
		{
			static const DWORD s_fontBufSites[] = { 0x57BF70, 0x719B5D, 0x719F43 };
			for(size_t i = 0; i < sizeof(s_fontBufSites)/sizeof(s_fontBufSites[0]); i++){
				const DWORD site = s_fontBufSites[i];
				BYTE opFB = *(BYTE*)site;
				DWORD tFB = 0;
				if(opFB == 0xE8) tFB = *(DWORD*)(site + 1) + site + 5;
				const bool okFB = (opFB == 0xE8 && tFB == 0x719840);
				dbglog("[fontbuf_install] 0x%08X op=%02X orig=%08X%s "
				       "(expect op=E8 orig=719840; MISMATCH = intercept "
				       "SKIPPED, vanilla path kept, RenderFontBuffer exit "
				       "unenforced at this site)",
				       (unsigned)site, (unsigned)opFB, (unsigned)tFB,
				       okFB ? "OK" : "MISMATCH");
				if(okFB)
					InterceptCall(&CFont_RenderFontBuffer_orig,
						CFont_RenderFontBuffer_hook, site);
			}
		}
		// [PtLightGlow] one-shot shield-lever receipt (03:01-run black squares):
		// the flush installs above verified OK, yet the force NEVER ran —
		// skygfx.ini had coronaBlendShield=0 / spriteFlushShield=0 /
		// spriteStateShield=0, so the buffered-sprite flush draws
		// (spriteBatch 2-20) rasterised with SRCALPHA/INVSRCALPHA (181
		// [corona_blend] samples) and REPLACE (23 samples) — the glow
		// texture's opaque black surround -> black squares with bright
		// centers. Print the lever state at EVERY boot so a
		// disabled-shield run is identifiable from the log alone.
		dbglog("[PtLightGlow] shield levers: coronaBlendShield=%d spriteFlushShield=%d spriteStateShield=%d "
		       "(0 = flush draws unshielded -> black-square producer; parser default 1)",
		       config ? config->coronaBlendShield : -1,
		       config ? config->spriteFlushShield : -1,
		       config ? config->spriteStateShield : -1);
		// 0x53E21D  call CPointLights::Render (0x7002D0) — shield-window
		//           re-arm for the street-lamp fog-cone/glow-dot batch
		//           (see CPointLights__Render_shield_hook). Same
		//           byte-verification discipline as corona_install: the
		//           site must be E8 with extracted target 0x7002D0 — on
		//           MISMATCH skip the install, the vanilla path remains
		//           (cones may stay unshielded, but nothing gets
		//           corrupted).
		{
			BYTE opPL = *(BYTE*)0x53E21D;
			DWORD tPL = 0;
			if(opPL == 0xE8) tPL = *(DWORD*)(0x53E21E) + 0x53E21D + 5;
			bool okPL = (opPL == 0xE8 && tPL == 0x7002D0);
			dbglog("[pointlights_install] 0x53E21D op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7002D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, cone batch unshielded)",
				(unsigned)opPL, (unsigned)tPL,
				okPL ? "OK" : "MISMATCH");
			if(okPL)
				InterceptCall(&CPointLights__Render_orig,
					CPointLights__Render_shield_hook, 0x53E21D);
		}
		// 0x53DE6B  the EARLY call CPointLights::Render (0x7002D0) — sky/
		//           reflection phase, BEFORE RenderScene. Un-intercepted
		//           until now, which is why the glow-sprite batch it flushes
		//           was rasterised outside the shield window (the black
		//           squares of the 2026-10-24 diagnosis). Same byte-verified
		//           discipline, same hook function and the SAME orig pointer
		//           as 0x53E21D: both sites call 0x7002D0, so the extracted
		//           target is identical — re-assigning it is not a double
		//           wrap (the hook calls the real function directly).
		{
			BYTE opPL = *(BYTE*)0x53DE6B;
			DWORD tPL = 0;
			if(opPL == 0xE8) tPL = *(DWORD*)(0x53DE6C) + 0x53DE6B + 5;
			bool okPL = (opPL == 0xE8 && tPL == 0x7002D0);
			dbglog("[pointlights_install] 0x53DE6B op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7002D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, early glow batch "
			       "unshielded)",
				(unsigned)opPL, (unsigned)tPL,
				okPL ? "OK" : "MISMATCH");
			if(okPL)
				InterceptCall(&CPointLights__Render_orig,
					CPointLights__Render_shield_hook, 0x53DE6B);
		}
		// fix pointlight fog
		InjectHook(0x700B6B, CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect);
	if(!s_rwfixLogged){
		s_rwfixLogged = true;
		dbglog("[rwfix] install OK (6 machines)");
	}
}

// W2-2 (partial): exported predicate for cross-TU gate (main.cpp effectsForceAlphaBlock).
// Full struct refactor pending; for now ORs the existing producer flags.
bool spriteProducerWindowActive(void)
{
	return g_inCoronasRender || g_inCloudsRender || g_inPointLightsRender || s_spriteForceValid;
}
void spriteProducerWindowClouds(bool on)
{
	g_inCloudsRender = on;
}
