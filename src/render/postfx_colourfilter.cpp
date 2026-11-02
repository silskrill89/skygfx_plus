// postfx_colourfilter.cpp - colour-filter chain, IV grade, colour cycle (m0060 split).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "ModuleList.hpp"
#include "profiler.h"

// pipelinecommon.cpp shader chokepoint — same extern declaration the other
// postfx TUs use (postfx_newfx.cpp / mirrorPass.cpp / postfx_underwater.cpp).
// EnsureIVGradePS uses it for the optional IDR_IVGRADEPS CSO load.
void makePS(int res, void **sh);

// ---- PERF log spam gate (mirror of postfx.cpp:20-30; struct body kept
// token-identical for ODR) ------------------------------------------------
// skygfx.h's PerfTimer logs UNCONDITIONALLY at scope exit, and this TU's
// PERF_SCOPE("Tonemap") fired every frame -> "PERF [Tonemap] x ms" flood
// (12,373 lines in the 2026-10-21 run) despite profilerEnable=0 — the
// gated #undef/#define only existed in postfx.cpp, so this TU still used
// skygfx.h's macro. Same local gated variant: logs only when
// config->profilerEnable is set. No __try in this file (SEH/C2712 safe).
#undef PERF_SCOPE
struct PostfxPerfGate {
	const char *name;
	double startMs;
	PostfxPerfGate(const char *n) : name(n), startMs(perfNow()) {}
	~PostfxPerfGate(){
		if(config && config->profilerEnable)
			dbglog("PERF [%s] %.2f ms", name, perfNow() - startMs);
	}
};
#define PERF_SCOPE(name) PostfxPerfGate _perf##__LINE__(name)
/////
///// Im2D overrides
/////


int overrideColorMod = -1;
int overrideAlphaMod = -1;
void *overrideIm2dPixelShader;
RwRaster *grainRaster;
void *gradingPS, *contrastPS, *tonemapPassPS;

// colourFilterEnable=0 bypass ownership (single owner: ColourFilter_Modern's
// tonemap pass). The bypass must skip the COLOUR FILTER, never the sRGB gamma
// encode: the pipes emit linear HDR and TonemapPass is the ONLY LinearToSRGB
// in the chain. Returning before it put the raw linear frame on screen —
// mid-tones land ~2x under their display value, so daylight exteriors render
// near-black while the HUD (already display-referred) stays bright.
// Set for exactly one ColourFilter_Modern call from the bypass path.
bool s_tonemapIdentityGrade = false;

// Exposure-stack handshake (ColourFilter_Modern -> DrawIVGrade, this file):
// CF_Modern's tonemap/auto-exposure pass sets this when it runs; DrawIVGrade
// (later in the SAME ColourFilter_switch — postfx.cpp :1660/:1783 before
// :1702/:1845) consumes it once for its throttled diagnostic log. Consume-once
// keeps the signal frame-local if the active colour filter changes mid-session.
// The old 1.5 exposure CAP was deleted on 2026-10-25 (IVGrade audit F2):
// COLORFILTER_GTAIV bypasses ColourFilter_Modern entirely (postfx.cpp:1842-1850
// is a pure bypass), so this flag was always false on the shipped IV preset and
// the cap never fired. The dead `s_ivExpCap` knob (zero writers) went with it.
static bool  s_cfTonemapRanFrame  = false;

// fix-30 handshake reset — called at ColourFilter_switch ENTRY (postfx.cpp).
// s_cfTonemapRanFrame is consumed inside DrawIVGrade, but DrawIVGrade has
// several early-returns BEFORE that point (gate/raster-size/frame-copy
// failures), so the flag could survive into the next frame and mis-report the
// handshake. Clearing it at switch entry means each frame starts clean and
// only this frame's own CF_Modern tonemap (which runs later inside the same
// ColourFilter_switch) can set it.
// Declared locally in postfx.cpp too: postfx.h is outside this fix's edit set.
void PostfxResetTonemapHandshake(void)
{
	s_cfTonemapRanFrame = false;
}

// ============================================================
// Cutscene≠menu postfx gate (expIV gate fix) — duplicate of the helpers in
// postfx.cpp (static inline there; postfx.h is outside this fix's edit set).
// IsGameInMenuOrPaused() (postfx_utils.cpp) bundles CCutsceneMgr__ms_running
// into its skip bool — original rationale (545bc02) was a blanket "skip
// expensive PostFX during menus/loading/pause" performance guard with
// cutscene bundled in without a pass-specific reason. Cutscenes are rendered
// scenes: the grading chain (incl. the IV grade) must run on them.
// ============================================================
static inline bool PostfxMenuPauseOnly(void)
{
	return CMenuManager__m_bMenuActive || CPostEffects::m_bDisableAllPostEffect;
}

// One-shot per pass: first cutscene frame where the pass now runs (it was
// skipped before the cutscene≠menu fix). Fires once per session.
static inline void
CutscenePostfxEnabledLog(const char *pass, bool *fired)
{
	if(*fired) return;
	*fired = true;
	dbglog("[PF] cutscene postfx: ENABLED (was skipped) pass=%s", pass);
}

void
CPostEffects::DarknessFilter_fix(uint8 alpha)
{
	DarknessFilter(alpha);
	UpdateFrontBuffer();
}

void
CPostEffects::ColourFilter_Generic(RwRGBA rgb1, RwRGBA rgb2, void *ps)
{
	PF_SCOPE("ColourFilter_Generic");
	if(dbglog_throttle( "cf_generic"))
		dbglog("[PostFX] ColourFilter_Generic ps=%p pRasterFrontBuffer=%p rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d)",
			ps, CPostEffects::pRasterFrontBuffer, rgb1.red, rgb1.green, rgb1.blue, rgb1.alpha,
			rgb2.red, rgb2.green, rgb2.blue, rgb2.alpha);
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	RwRGBAReal color, color2;
	RwRGBARealFromRwRGBA(&color, &rgb1);
	RwRGBARealFromRwRGBA(&color2, &rgb2);
	RwD3D9SetPixelShaderConstant(0, &color, 1);
	RwD3D9SetPixelShaderConstant(1, &color2, 1);

	// SEH-guarded choke-point: resets overrideIm2dPixelShader on every exit
	// (incl. fault) so a faulting dispatch can't leave the override bound.
	PfBindPS(ps, "IDR_VCTRAILSPS(164)/trails");
	PfDraw(4, 2);
	guardedIm2DRender(ps, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_generic");

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

// --- White-blowout audit: NaN/Inf scrub + clamp -----------------------------
// windows.h is included without NOMINMAX, so min/max are the ternary macros
// ((a)<(b)?(a):(b)) — NaN falls through EVERY existing max/min clamp in this
// file (min(2,NaN)=NaN, max(0.5,NaN)=NaN) and reaches the shader constants:
//   NaN c5.y (toe)   -> Hable denominator NaN -> NaN pixels -> black frame
//   NaN c6 (grade)   -> PostGrade/FilmicGrading saturate(NaN)=0 -> black
//   NaN c7.y/z (exp) -> clamp(key/lum, min, NaN) = NaN -> exposure NaN
//   NaN c8 (curve)   -> PivotCurve gain NaN -> black
// (saturate(NaN)=0 in SM3.0 — the failure mode is black, but a NaN c7.z can
// also collapse the auto-exposure window to a constant). CfSanitize uses the
// `!(x >= lo)` NaN-catch idiom (same as the kCFExposureFloor guard): NaN,
// -inf and below-lo all map to `def`; +inf and overshoot clamp to `hi`.
// Bounds are set AT the existing clamp bounds (or wider where no clamp
// existed) so every valid value is bit-identical to today's output.
static inline float CfSanitize(float v, float lo, float hi, float def)
{
	if(!(v >= lo)) return def;   // NaN, <lo, -inf -> default
	if(v > hi)     return hi;    // +inf / overshoot -> clamp
	return v;
}

void
CPostEffects::ColourFilter_Modern(RwRGBA rgba1, RwRGBA rgba2)
{
	PF_SCOPE("ColourFilter_Modern");
	// NOTE (next dig): observed ENTER rgba1=(93,94,56,127) rgba2=(85,22,11,127)
	// — G≈R≫B at alpha 127 (50% blend) yields the dark greenish interior tint
	// reported on PBR; the tint traces to these multiply colors (grading
	// constants left untouched — possibly intentional vanilla-derived values).
	if(dbglog_throttle("cfmodern_enter"))
		dbglog("[PostFX] ColourFilter_Modern ENTER rgba1=(%d,%d,%d,%d) rgba2=(%d,%d,%d,%d) pRFB=%p gradingPS=%p camRas=%p",
			rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha,
			rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha,
			CPostEffects::pRasterFrontBuffer, gradingPS,
			Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL);

	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("[PostFX] WARNING: pRasterFrontBuffer is NULL in ColourFilter_Modern!");
		return;
	}

	// Blend-leak fix (§4.7 corona-black): snapshot the FULL raw geometry block
	// (SaveRawGeomStates — the 9 raw states postfx passes touch: blend
	// enable/factors, BLENDOP, BLENDFACTOR, ZWRITE, ZENABLE, ALPHATESTENABLE,
	// CULLMODE) BEFORE any state write, and restore it at the pass tail through
	// the verified three-layer park-and-set (RestoreRawGeomStates,
	// postfx_utils.cpp). The previous ad-hoc restore hard-seeded the L1 rw
	// cache with the canonical SRCALPHA/INVSRCALPHA block while replaying the
	// SAVED device values through L2+L3 only — whenever the saved block
	// differed from canonical (blend OFF / ONE-ZERO / BLENDFACTOR pairs, all
	// observed in field logs), the rw cache and the device permanently
	// disagreed: every later game RwRenderStateSet targeting the cached value
	// cache-hit no-op'ed, the device kept the leaked block, and the next
	// sky-phase sprite flush (0x53DE6B — the un-windowed flush that carries
	// the corona/pointlight batch) rasterised the glow quads' black RGB
	// surround REPLACE = opaque black coronas. Restoring the CAPTURED values
	// through all three layers ends the pass with cache == device (snapshot
	// semantics), so the next producer's entry contract always TRANSITIONS.
	// Raw stage-1 sampler capture for the frame-luma bind in the tonemap block
	// (function scope so the restore can run after the generic rw-state cleanup).
	DWORD cfS1U = D3DTADDRESS_WRAP, cfS1V = D3DTADDRESS_WRAP;
	DWORD cfS1Min = D3DTEXF_LINEAR, cfS1Mag = D3DTEXF_LINEAR;
	bool cfS1Saved = false;
	DWORD cfRawGeom[9];
	bool cfRawGeomSaved = SaveRawGeomStates(cfRawGeom);

	// Log render state before postfx
	if(dbglog_throttle("cf_modern_state"))
		dbglog("[PostFX] cf_modern PRE-RENDER pRasFB=%p camRas=%p overridePS=%p gradingPS=%p zTest=%d zWrite=%d",
			CPostEffects::pRasterFrontBuffer,
			Scene.camera ? RwCameraGetRaster(Scene.camera) : 0,
			overrideIm2dPixelShader, gradingPS, -1, -1);

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	if(!Colorcycle::initialised)
		Colorcycle::Initialise();

	GradeColorset cset;
	Colorcycle::Update(&cset);
	Grade red, green, blue;
	red = cset.red;
	green = cset.green;
	blue = cset.blue;

	// Modern grading for PBR: timecycle provides color tint, not brightness multiplier.
	// Vanilla two-pass pipeline (MODULATE2X + ADD) is calibrated for gamma-encoded [0,1] values.
	// PBR outputs linear HDR where values can be 5-20+ (sky, reflections).
	// So we extract the timecycle's COLOR HUE and apply it as a gentle tint around neutral (1.0).
	float a1 = rgba1.alpha/128.0f;
	float a2 = rgba2.alpha/128.0f;
	// Timecycle alpha doubling guard (aap skygfx v4.2a "Fixed timecycle alpha
	// range conversion" precedent): PC timecyc.dat PostFX alphas are already
	// standard-range 0-255, but the game doubles them AGAIN at load (CTimeCycle,
	// outside this file — rgba1/rgba2 arrive here already doubled as args). The
	// vanilla out = in + in*rgb1*alpha1 + in*rgb2*alpha2 then overdrives ->
	// washed white, worst in EXTRACOLOURS interiors. Runtime equivalent of aap's
	// load-time halve: saturate the blend factors at 1.0 (with the /128
	// normalize here, a stored alpha byte >128 can only be doubling overshoot).
	a1 = min(a1, 1.0f);
	a2 = min(a2, 1.0f);
	float raw_r = a1*rgba1.red/255.0f + a2*rgba2.red/255.0f;
	float raw_g = a1*rgba1.green/255.0f + a2*rgba2.green/255.0f;
	float raw_b = a1*rgba1.blue/255.0f + a2*rgba2.blue/255.0f;
	// ... and bound the additive tint term so out = in*(1 + raw) cannot exceed
	// ~2x input (the final grade clamp below already hard-bounds the per-channel
	// multiply to 2.0; this keeps raw itself vanilla-safe before hue extraction).
	raw_r = min(raw_r, 1.0f);
	raw_g = min(raw_g, 1.0f);
	raw_b = min(raw_b, 1.0f);
	// Luminance of the raw tint — encodes overall brightness level (night=dark, day=bright)
	float luma = 0.299f*raw_r + 0.587f*raw_g + 0.114f*raw_b;
	float inv = (luma > 0.001f) ? 1.0f/luma : 1.0f;
	// Extract color hue from raw, blend toward neutral with TINT_STRENGTH
	// 0.0 = no tint (neutral 1,1,1), 1.0 = full vanilla timecycle color shift
	float TINT_STRENGTH = 0.35f;
	red.r   = 1.0f + (raw_r*inv - 1.0f) * TINT_STRENGTH;
	green.g = 1.0f + (raw_g*inv - 1.0f) * TINT_STRENGTH;
	blue.b  = 1.0f + (raw_b*inv - 1.0f) * TINT_STRENGTH;
	// Subtle brightness from timecycle level (preserves day/night atmosphere)
	float brightness = max(0.75f, min(1.25f, 0.85f + luma * 0.1f));
	red.r   *= brightness;
	green.g *= brightness;
	blue.b  *= brightness;
	// Clamp to prevent extremes
	red.r   = max(0.5f, min(2.0f, red.r));
	green.g = max(0.5f, min(2.0f, green.g));
	blue.b  = max(0.5f, min(2.0f, blue.b));
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;

	if(dbglog_throttle("cf_modern_grading"))
		dbglog("[PostFX] ColourFilter_Modern grading: r=%.3f g=%.3f b=%.3f luma=%.3f bright=%.3f tint=%.1f",
			red.r, green.g, blue.b, luma, brightness, TINT_STRENGTH);

	RwD3D9SetPixelShaderConstant(0, &red, 1);
	RwD3D9SetPixelShaderConstant(1, &green, 1);
	RwD3D9SetPixelShaderConstant(2, &blue, 1);

	if(!gradingPS){
		dbglog("[PostFX] WARNING: gradingPS is NULL! ColourFilter_Modern will render with no shader");
	}

	// Filter-quad blend doctrine: the grading/tonemap quads must REPLACE the
	// frame, never blend with it. The rw cache can carry a stale blend block
	// from an earlier pass (e.g. BLENDFACTOR additive from VCS trails, or
	// HUD SRCALPHA with a quad whose vertex alpha is 0 — colorfilterVerts
	// carry emissiveColor=0) which darkens or no-ops the graded output.
	// Force ONE/ZERO (replace) on both layers for the filter draws; the
	// cleanup below restores SRCALPHA/INVSRCALPHA.
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// Pass 1: Color grading (diagonal matrix multiply). SEH-guarded so the
	// override is cleared on fault as well as success. SKIPPED on the
	// colorFilterEnable=0 bypass (s_tonemapIdentityGrade): that path only
	// wants the tonemap's gamma encode, not the timecycle tint.
	if(!s_tonemapIdentityGrade){
		PfBindPS(gradingPS, "IDR_GRADINGPS(160) pass1");
		PfDraw(4, 2);
		guardedIm2DRender(gradingPS, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_modern_p1");
		if(dbglog_throttle("pf_cfm_p1"))
			dbglog("[PF] CF_Modern p1 grading written to cameraRT from pRasterFrontBuffer, FB resync next");
	}

	// Copy graded linear output to pRasterFrontBuffer for tonemap input
	UpdateFrontBuffer();

	// --- Unified tonemap: measure the graded frame for adaptive exposure ---
	// timecyc supplies COLOUR (pass 1 above); the rendered frame supplies
	// BRIGHTNESS here, so exposure is frame-consistent across all pipes.
	// Only runs when the feature is enabled; otherwise the legacy timecyc-only
	// exposure path below is used unchanged. The identity-grade bypass forces
	// it OFF: TonemapPass re-derives sceneLuma (and therefore PostGrade's
	// adaptive intensity) from the measured frame whenever c5.w >= 0.5, which
	// would re-introduce grading the bypass asked us to skip.
	bool autoExposureOn = !s_tonemapIdentityGrade &&
		(config && config->tonemapAutoExposure && luminanceReducePS && luminanceAdaptPS);
	float adaptSpeed = config ? config->tonemapAdaptSpeed : 0.12f;
	IDirect3DTexture9 *frameLumaTex = autoExposureOn ? RunFrameExposure(adaptSpeed) : NULL;
	if(autoExposureOn && !frameLumaTex)
		autoExposureOn = false;

	// Pass 2: Hable/Uncharted 2 filmic tonemap + sRGB gamma encode
	if(tonemapPassPS){
		// Re-setup render states for tonemap pass (reads pRasterFrontBuffer).
		// Re-assert replace blending — UpdateFrontBuffer's blit ran in between.
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

		// Map GTA SA brightness slider (0-384, default 256) to tonemap exposure
		float brightness = (float)CMenuManager__m_PrefsBrightness;
		float baseExposure = max(0.3f, brightness / 256.0f);  // 256 → 1.0 (neutral)

		// === Timecycle-driven adaptive tonemap ===
		// The timecycle is the authority over tonemapping mood.
		// Every parameter derives from CColourSet fields that timecyc.dat authors control.
		CColourSet &tc = CTimeCycle__m_CurrentColours;
		bool isInterior = (*CGame__currArea != 0);
		bool isCutscene = CCutsceneMgr__ms_running;

		// --- Timecycle signals (normalized 0-1 where applicable) ---
		// NOTE: SA CColourSet stores ambient/directional as floats ALREADY in 0-1
		// range (game divides timecyc.dat values by 255; weather.cpp blends them
		// with altR/255.0f and clamps to [0,1] — same convention). Do NOT divide
		// by 255 here: doing so collapsed sceneLuma to ~0.003, which clamped
		// exposure to 1.5 + toe to 0.33 → the permanent gray veil, day and night.
		float tcAmbientLuma  = 0.299f*tc.ambientR + 0.587f*tc.ambientG + 0.114f*tc.ambientB;
		float tcDirLuma      = 0.299f*tc.directionalR + 0.587f*tc.directionalG + 0.114f*tc.directionalB;
		float sceneLuma      = tcAmbientLuma + tcDirLuma * 0.5f;
		// White-blowout audit: TC floats are game memory — a foreign stomp or
		// uninitialised CColourSet can hand us NaN here, which today survives
		// every downstream max/min (ternary macros) into c5.z/c6/c8. Scrub to
		// the plausible physical range (valid data is <= 1.5, untouched).
		sceneLuma = CfSanitize(sceneLuma, 0.0f, 2.0f, 0.0f);
		// Receipt copy of the TIMECYCLE proxy (cf_exp below). Kept separate
		// from `sceneLuma` because the identity-grade bypass zeroes the c5.z
		// value further down; the exposure path (sceneExposure/toeStrength)
		// runs on this real TC luma, and the receipt reports that input.
		float exposureSceneLuma = sceneLuma;
		float shadowNorm     = CfSanitize((float)tc.shadowStrength / 255.0f, 0.0f, 1.0f, 0.0f);
		float fogFactor      = CfSanitize(tc.fogStart / 500.0f, 0.0f, 1.0f, 0.0f);  // less fog = 1, heavy fog = 0
		float clouds         = CfSanitize(tc.cloudAlpha, 0.0f, 1.0f, 0.0f);
		float sunBright      = CfSanitize(tc.spriteBrightness, 0.0f, 2.0f, 0.0f);
		float streetLights   = CfSanitize(tc.lightsOnGroundBrightness, 0.0f, 1.0f, 0.0f);
		float envMult        = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
		// Dark-frame guard: envMult feeds the exposure product directly. A
		// garbage/negative value (pipe not initialised this frame, cutscene
		// reset, etc.) would scale exposure to <= 0 -> black frame. Clamp to
		// the plausible physical range before use. (CfSanitize replaces the
		// old max/min pair, whose ternary form let NaN through.)
		envMult = CfSanitize(envMult, 0.0f, 2.0f, 1.0f);

		float exposure, toeStrength;
		float gradeContrast, gradeBrightness, gradeLift, gradeCurve;

		// === Unified timecycle-driven adaptive tonemap (outdoor + cutscene + interior) ===
		// The timecycle is the authority — cutscene/interior timecyc.dat entries already
		// encode the correct mood. We use the same adaptive path for everything.
		// Cutscene/interior gets a gentle dampening factor where the sun is pointing at camera.

		// --- Garage/sheltered area detection ---
		// Garages are NOT interiors (currArea==0) but have partial occlusion.
		// CCullZones detect sheltered areas (no rain zones). In garages, there's
		// only skylight from the front opening — reduce exposure to match.
		extern bool CCullZones__PlayerNoRain(void);
		bool isSheltered = CCullZones__PlayerNoRain() && !isInterior;
		float garageDampen = isSheltered ? 0.65f : 1.0f;  // 35% darker in garages

		// --- Exposure: reciprocal of scene brightness + timecycle dampening ---
		// sceneLuma is now normalized 0-1 from timecycle ambient+directional
		// Night (sceneLuma≈0.05): exposure≈1.3 (brighter, lifts shadows)
		// Midday (sceneLuma≈0.35): exposure≈0.7 (darker, prevents blowout)
		float sceneExposure = 1.0f / max(0.50f + sceneLuma * 2.5f, 1e-7f);
		sceneExposure = max(0.50f, min(1.50f, sceneExposure));
		// Carcols env mult nudges ±5%
		float carcolsAdapt = 0.95f + envMult * 0.05f;
		// Bright sun → slightly less exposure (prevent highlight blowout)
		float sunDampen = 1.0f - max(0.0f, min(0.08f, sunBright * 0.05f));
		// Mood exposure (prefs brightness + carcols + sun + garage). When
		// auto-exposure is on, the frame-derived term inside TonemapPass supplies
		// brightness, so the timecyc reciprocal is dropped here (no double-apply).
		exposure = baseExposure * carcolsAdapt * sunDampen * garageDampen;
		if(!autoExposureOn)
			exposure *= sceneExposure;

		// fix-cf-washout (wave-2): soft-knee composite-gain gate on the Modern
		// filter's gain portion.
		// [2026-10-21 REWORK — this block is now LOG-ONLY; see the log-only note
		// above `if(pipeChainRanFrame)`. The CPU-side exposure multiply
		// described below was REMOVED: sceneLuma is the TIMECYCLE proxy
		// (:338-340, near-black 0.000-0.074 on vanilla alt-timecyc), so the
		// knee NEVER armed while DrawPipeChain pass-3 unconditionally
		// re-presented the scene ~2.01x => net 2.6-2.9x washout. The knee now
		// runs in TonemapPass.hlsl on the MEASURED frame lum, gated by c8.w.]
		// ROOT CAUSE (BMP-dump proven, wave-2 session): DrawPipeChain's pass-3
		// composite re-presents the scene at ~2.01x mean (cf_after_ssao
		// 70.5 -> after_pipechain 141.9/255, flat across luma bands, clip at
		// top), and this filter's filmic stack gains again on the doubled
		// input (~+1.31x mean, +1.46x mid-band) => net ~2.9x: a 70/255 scene
		// displays at 186/255 washed-out white-grey — the user video's day
		// haze, top-down-over-road near-white and beach blowout. The Hable
		// curve itself cannot cancel a content-level doubling (its mid-band
		// identity point sits near 0.45 and its shoulder asymptotes ~0.99),
		// so the exposure product must be scaled down CPU-side when the chain
		// composited this frame. Predicate mirrors the switch's own un-gated
		// DrawPipeChain call (postfx.cpp: pipeChainEnable && normalBufferEnable)
		// AND requires the REAL reflection path to have run this frame —
		// PostfxPipeChainReflRanFrame() (postfx.cpp) is only TRUE when pass 3
		// bound the real classify pack on s4, not the pack0 dummy (the ×0.5
		// knee was calibrated against a pre-L1 frame; on pack0-dummy frames
		// the chain is gain-neutral and the knee must not fire). The identity
		// grade (colorFilterEnable=0 bypass) is likewise excluded. Internal
		// chain bails (missing shader/RTs) only occur in failure states where
		// the frame was not doubled, bounded here by the knee.
		// Soft knee driven by the timecycle scene luma: interiors/night frames
		// (sceneLuma ~0 — the separate dim-ambient issue, another lane) are
		// untouched; the correction ramps to full for bright (day) content so
		// pipeChain's ~2x + this filter's gain lands near 1.0 display luma.
		// The greenish-interior tint constants (pass 1) are NOT touched — the
		// tint is not the washout cause (provably ~0.9-1.0 mean gain).
		extern bool PostfxPipeChainReflRanFrame(void); // postfx.cpp — real classify pack bound on s4 this frame
		bool pipeChainRanFrame = config->pipeChainEnable && config->normalBufferEnable &&
			PostfxPipeChainReflRanFrame() && !s_tonemapIdentityGrade;
		// CPU-side knee factor hoisted for the cf_exp receipt: 1.0 = gate
		// closed (or knee at 0), 0.5 = full bright-frame knee. Still LOG-ONLY
		// (the real knee runs on measured lum in TonemapPass.hlsl, c8.w gate).
		float cfGainScale = 1.0f;
		// LOG-ONLY since the 2026-10-21 cf_gain rework: the knee moved into
		// TonemapPass.hlsl where it runs on the MEASURED frame lum (c8.w gate).
		// cfGainScale is kept purely for the log — it no longer multiplies the
		// exposure product anywhere.
		if(pipeChainRanFrame){
			float cfGainKnee = max(0.0f, min(1.0f, (sceneLuma - 0.10f) / 0.20f));
			cfGainScale = 1.0f - 0.5f * cfGainKnee;   // 1.0 (dim) -> 0.5 (bright day)
			// Throttled log deliberately OUTSIDE the old `cfGainScale < 0.999`
			// gate: "gate open, knee zero" (scale=1.000, tc-proxy near-black)
			// now emits a line; silence means the gate itself was closed.
			if(dbglog_throttle("cf_gain"))
				dbglog("[CF] cf_gain: gate=1 scale=%.3f sceneLuma(tc-proxy)=%.3f (LOG-ONLY; real knee on measured lum in TonemapPass, c8.w gate)",
					cfGainScale, sceneLuma);
		}

		// --- Exposure floor (black-interior audit, 2026-10-26) --------------
		// The product above is provably POSITIVE with today's factors:
		//   baseExposure = max(0.3, PrefsBrightness/256)        >= 0.300
		//   carcolsAdapt = 0.95 + envMult*0.05, envMult [0,2]   >= 0.950
		//   sunDampen    = 1 - min(0.08, sunBright*0.05)        >= 0.920
		//   garageDampen = 0.65 (sheltered) / 1.0               >= 0.650
		//   sceneExposure = clamp(1/(0.5+sceneLuma*2.5), .5, 1.5) >= 0.500
		// => worst case 0.3*0.95*0.92*0.65*0.5 = 0.0852 (auto off, garage,
		// brightness at minimum). Nothing ENFORCES that, though: a future
		// writer of any factor, or a NaN that slips the min/max macros, would
		// reach the shader's unconditional `c *= exposure` and collapse the
		// WHOLE frame to black. Clamp to a documented minimum AFTER the knee
		// block, so a reinstated knee multiply could never push below it
		// either. `!(x >= floor)` (not max()) also catches NaN.
		// No-op on every current path: 0.0852 (auto off) / 0.1704 (auto on,
		// no sceneExposure term) are both above 0.05, so the
		// s_tonemapIdentityGrade bypass (autoExposureOn forced false, same
		// product) stays BIT-IDENTICAL — this comparison always takes the
		// "leave untouched" branch there.
		const float kCFExposureFloor = 0.05f;
		if(!(exposure >= kCFExposureFloor))
			exposure = kCFExposureFloor;

		// Interior: timecycle encodes the correct mood, no extra dampening

		// --- Toe: shadow lift driven by sceneLuma + timecycle shadow depth ---
		// sceneLuma is normalized 0-1, so toe responds properly to time-of-day.
		// NOTE: In the Hable/Uncharted2 filmic tonemap, LOWER D (toeStrength) values
		// produce MORE shadow lift, not less — the denominator term D*F dominates.
		// The minimum of 0.05 was causing a permanent "gray veil" shadow lift even
		// in bright scenes. Raised to 0.15 for midday to keep blacks solid.
		// Night scenes still get up to 0.35 for gentle shadow detail.
		// D RISES with daylight: in the Hable/Uncharted2 curve, HIGHER D = tighter toe =
		// solid blacks; LOWER D = more shadow lift. The old form drove D down to its
		// 0.15 floor at midday (max gray veil) and up to ~0.26 at night — inverted, so
		// daylight washed out and shadows vanished. Now night keeps the proven gentle
		// lift (~0.27) and midday clamps high (tight toe, blacks stay black).
		float toeStrengthVal = 0.22f + sceneLuma * 1.0f + shadowNorm * 0.08f;
		// NaN-catch clamp (CfSanitize): the old max/min pair let a NaN
		// toeStrengthVal through into c5.y -> Hable denominator NaN.
		toeStrength = CfSanitize(toeStrengthVal, 0.18f, 0.55f, 0.30f);

		// --- Grade params: fully timecycle-driven ---
		// Contrast: shadow strength × fog clearance (deep shadows + clear sky = max contrast)
		gradeContrast = CfSanitize(1.10f + shadowNorm * 0.30f * fogFactor, 1.0f, 1.5f, 1.15f);

		// Brightness: street lights provide fill in dark scenes
		gradeBrightness = CfSanitize(0.02f + streetLights * 0.05f, 0.0f, 0.2f, 0.02f);

		// Lift: overcast/cloudy raises blacks slightly; heavy fog also lifts
		gradeLift = CfSanitize(clouds * 0.020f + (1.0f - fogFactor) * 0.015f, 0.0f, 0.1f, 0.0f);

		// Curve blend: brighter scenes get more S-curve for depth
		gradeCurve = CfSanitize(0.20f + sceneLuma * 0.30f, 0.0f, 0.8f, 0.25f);

		// Throttled diagnostic
		static unsigned int tonemapLogCounter = 0;
		// NaN-catch clamp: blackLift feeds c7.x -> `c*(1-bl)+bl`; a NaN bl
		// makes that NaN for EVERY pixel (black frame).
		float blackLift = CfSanitize(config ? config->tonemapBlackLift : 0.015f,
		                             0.0f, 0.2f, 0.0f);

		// --- colorFilterEnable=0 bypass: IDENTITY grade -----------------------
		// The bypass path routes through this tonemap so the frame still gets
		// its sRGB gamma encode; everything downstream must be a no-op:
		//   sceneLuma 0        -> PostGrade adaptive scale 0 => brightness/
		//                         contrast/lift/curve all neutral
		//   blackLift 0        -> no black-level lift
		//   curveP x/y 0       -> PivotCurve bit-exact early-out (see below)
		// Exposure/toe stay on the normal timecycle path so overall brightness
		// matches the graded path — only the timecycle TINT (pass 1) is skipped.
		if(s_tonemapIdentityGrade){
			sceneLuma = 0.0f;
			gradeBrightness = 0.0f;
			gradeContrast = 1.0f;
			gradeLift = 0.0f;
			gradeCurve = 0.0f;
			blackLift = 0.0f;
		}

		if(tonemapLogCounter++ % 3600 == 0){
			extern int16 &CWeather__OldWeatherType;
			extern int16 &CWeather__NewWeatherType;
			extern float &CWeather__InterpolationValue;
			extern uint8 &CClock__ms_nGameClockHours;
			dbglog("[Tonemap] TC sceneLuma=%.3f amb=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) shadow=%d fog=%.0f cloud=%.2f sun=%.2f street=%.2f hour=%d wOld=%d wNew=%d wInterp=%.2f cutscene=%d interior=%d",
				sceneLuma, tc.ambientR, tc.ambientG, tc.ambientB, tc.directionalR, tc.directionalG, tc.directionalB,
				tc.shadowStrength, tc.fogStart, clouds, sunBright, streetLights,
				CClock__ms_nGameClockHours, CWeather__OldWeatherType, CWeather__NewWeatherType, CWeather__InterpolationValue,
				isCutscene, isInterior);
			dbglog("[Tonemap] VAL exp=%.3f toe=%.3f bl=%.4f ct=%.2f br=%.3f lift=%.3f curve=%.2f",
				exposure, toeStrength, blackLift, gradeContrast, gradeBrightness, gradeLift, gradeCurve);
		}

		// Pack into c5: {exposure, toeStrength, sceneLuma, autoExposureFlag}
		// c5.w is the frame-adaptive exposure flag read by TonemapPass.
		float tonemapP[4] = { exposure, toeStrength, sceneLuma, autoExposureOn ? 1.0f : 0.0f };
		RwD3D9SetPixelShaderConstant(5, tonemapP, 1);

		// Pack into c6: {brightness, contrast, lift, curveBlend} — all timecycle-driven
		float gradeP[4] = { gradeBrightness, gradeContrast, gradeLift, gradeCurve };
		RwD3D9SetPixelShaderConstant(6, gradeP, 1);

		// Pack into c7: {blackLift, minExposure, maxExposure, keyStrength}
		// The auto-exposure clamp + key blend are only read when c5.w >= 0.5.
		// Dark-frame guard (FIX-1, black-interior audit): TonemapPass computes
		// autoExp = key/lum and clamps to [c7.y, c7.z]. The previous guard
		// (`min = max(0.15, min)` then `max = max(max, min)`) PINNED BOTH ENDS
		// of the window to 0.15 for any degenerate config — e.g. the user's
		// tonemapMinExposure=-0.500 / tonemapMaxExposure=-0.284 became
		// clamp(key/lum, 0.15, 0.15) = constant 0.15 -> exposure 0.135 ->
		// interiors at ~4% display brightness. Reject a degenerate/invalid
		// pair outright (max <= min, or a non-positive max) and fall back to
		// the sane 0.5..2.0 window INSTEAD of collapsing it, then floor the
		// surviving window at 0.15 with a non-empty span:
		//   degenerate  -> 0.5 / 2.0
		//   min -0.5, max 2.0 -> 0.15 / 2.0  (floor only, span preserved)
		// White-blowout audit: CfSanitize FIRST — a NaN config float used to
		// slip past the degenerate check below (NaN <= x is false) and reach
		// c7.y/z, where clamp(key/lum, min, NaN) collapses to NaN -> exposure
		// NaN -> black frame. Bounds mirror main.cpp's INI clamps
		// ([-0.5..0.5] / [0.2..4.0]) so valid values are untouched.
		float minExpCfg = config ? config->tonemapMinExposure : 0.5f;
		float maxExpCfg = config ? config->tonemapMaxExposure : 2.0f;
		minExpCfg = CfSanitize(minExpCfg, -0.5f, 0.5f, 0.5f);
		maxExpCfg = CfSanitize(maxExpCfg, 0.2f, 4.0f, 2.0f);
		if(maxExpCfg <= minExpCfg || maxExpCfg <= 0.0f){ minExpCfg = 0.5f; maxExpCfg = 2.0f; }
		minExpCfg = max(0.15f, minExpCfg);
		maxExpCfg = max(maxExpCfg, minExpCfg + 1e-3f);
		float keyStrengthCfg = CfSanitize(
			autoExposureOn && config ? config->tonemapKeyStrength : 1.0f, 0.0f, 1.0f, 1.0f);
		float blackP[4] = {
			blackLift,
			autoExposureOn ? minExpCfg : 0.0f,
			autoExposureOn ? maxExpCfg : 0.0f,
			autoExposureOn ? keyStrengthCfg : 0.0f
		};
		RwD3D9SetPixelShaderConstant(7, blackP, 1);

		// --- cf_exp receipt (throttled ~2s) ------------------------------
		// Logs the exposure product that actually reaches c5.x (AFTER the
		// kCFExposureFloor clamp above), the TC input that drove sceneExposure,
		// and the effective auto-exposure window shipped in c7. If the frame
		// ever goes black "at random angles", this line shows whether exposure
		// collapsed (it cannot go below the floor) vs. some other pass.
		// knee = CPU-side cf_gain gate factor (1.0 = gate closed, 0.5 = full
		// bright-frame knee); it is log-only, the real knee runs on the
		// GPU-MEASURED lum inside TonemapPass. That measured HDR luminance is
		// GPU-only (8x8 -> 1x1 RTs, no CPU readback), so it cannot be included
		// without a pipeline stall; sceneLuma (TC proxy) is the CPU-side input.
		// minExp/maxExp are 0 when auto-exposure is off (c7.y/z unused then).
		//
		// [exposure] receipt extension (white-blowout audit): the ADAPTED
		// 1x1 texel is now sampled too — postfx_luminance.cpp does a 1x1
		// GetRenderTargetData into a SYSMEM staging tex at most once per 2s
		// (PollAdaptedLum) and mirrors TonemapPass's key/autoExp math, so
		// this line shows the full auto-exposure pipeline state:
		//   adapted       = the adapted luminance the tonemap reads on s1
		//                   (+1e30 = Inf, -1e30 = NaN sentinel from the FP16
		//                   decode — either means the adapt chain is broken)
		//   autoExpRaw    = key/lum BEFORE the c7 clamp
		//   autoExpClamped= clamp(autoExpRaw, c7.y, c7.z) — pinned at maxExp
		//                   persistently => the MEASURED frame is dark, not
		//                   an exposure runaway
		//   autoExpMul    = lerp(1, autoExpClamped, c7.w) — the multiplier
		//                   actually applied to c5.x inside TonemapPass
		//   adaptedAgeMs  = staleness of the readback (<= ~2000 when fresh;
		//                   -1 = no readback yet this session)
		// If adapted/autoExp* are sane but the frame is still white, the
		// blowout is NOT the exposure chain — look at the additive passes
		// (GodRays ONE/ONE, underwater composite) or the scene itself.
		extern bool PostfxGetAdaptedLum(float *adapted, float *autoExpRaw, int *ageMs); // postfx_luminance.cpp
		float adaptedLum = 0.0f, autoExpRaw = 0.0f;
		int adaptedAgeMs = -1;
		bool adaptedOk = autoExposureOn &&
			PostfxGetAdaptedLum(&adaptedLum, &autoExpRaw, &adaptedAgeMs);
		float autoExpClamped = 0.0f, autoExpMul = 1.0f;
		if(adaptedOk){
			autoExpClamped = max(minExpCfg, min(maxExpCfg, autoExpRaw));
			autoExpMul = 1.0f + (autoExpClamped - 1.0f) * keyStrengthCfg;
		}
		if(dbglog_throttle("cf_exp"))
			dbglog("[CF] exposure=%f sceneLuma=%f knee=%f minExp=%f maxExp=%f autoExp=%d"
				" bl=%f key=%f adapted=%f autoExpRaw=%f autoExpClamped=%f autoExpMul=%f adaptedAgeMs=%d",
				exposure, exposureSceneLuma, cfGainScale,
				autoExposureOn ? minExpCfg : 0.0f,
				autoExposureOn ? maxExpCfg : 0.0f,
				autoExposureOn ? 1 : 0,
				blackLift, autoExposureOn ? keyStrengthCfg : 0.0f,
				adaptedOk ? adaptedLum : 0.0f,
				adaptedOk ? autoExpRaw : 0.0f,
				adaptedOk ? autoExpClamped : 0.0f,
				adaptedOk ? autoExpMul : 1.0f,
				adaptedAgeMs);

		// Pack into c8: {curveLows, curveHighs, curveMid, kneeGate} — user pivot
		// curve applied at the TonemapPass OUTPUT (per-channel gain ramp
		// around the mid pivot; bit-exact identity when both intensities
		// are 0). c8.w is the cf_gain soft-knee gate flag (see kneeGate below).
		// Uploaded here for THIS draw only — other passes (DynamicSky
		// weatherP, screenParams) re-upload their own c8 before theirs.
		// Identity-grade bypass forces both intensities to exact 0.0 so
		// PivotCurve's early-out returns the pixel untouched.
		//
		// FIX-2 clamp (black-interior audit): PivotCurve is a gain ramp, so
		// highs = -1.0 maps pure white -> 0 and lows = -0.548 crushes the
		// shadows — an unconditional image-wide darkener stacked on top of
		// the exposure path (user INI shipped lows=-0.548/highs=-1.000).
		// Identity is 0/0/0.5. Intensities are only sane within +/-0.35:
		//   |lows| or |highs| > 0.35 -> force the curve OFF (send identity
		//   0/0/0.5) rather than pinning to a boundary value;
		// then hard-clamp what actually reaches c8 so NO runtime writer
		// (negative debug-menu slider ranges owned by another lane, foreign
		// stomp, hand-edited INI reload) can bypass this guard.
		float curveLows = config ? config->tonemapCurveLows : 0.0f;
		float curveHighs = config ? config->tonemapCurveHighs : 0.0f;
		float curveMid = config ? config->tonemapCurveMid : 0.5f;
		// White-blowout audit: scrub NaN BEFORE the ±0.35 gate — fabs(NaN)
		// is not > 0.35, so a NaN curve used to pass the gate AND the ternary
		// max/min clamps below, reaching c8 -> PivotCurve gain NaN -> black.
		curveLows  = CfSanitize(curveLows,  -1.0f, 1.0f, 0.0f);
		curveHighs = CfSanitize(curveHighs, -1.0f, 1.0f, 0.0f);
		curveMid   = CfSanitize(curveMid,    0.0f, 1.0f, 0.5f);
		if(s_tonemapIdentityGrade){
			curveLows = 0.0f;
			curveHighs = 0.0f;
		}else if(fabs(curveLows) > 0.35 || fabs(curveHighs) > 0.35){
			if(dbglog_throttle("curve"))
				dbglog("[Tonemap] PivotCurve (lows=%.3f highs=%.3f mid=%.3f) outside +/-0.35 — forcing identity 0/0/0.5",
					curveLows, curveHighs, curveMid);
			curveLows = 0.0f;
			curveHighs = 0.0f;
			curveMid = 0.5f;
		}
		curveLows = max(-0.35f, min(0.35f, curveLows));
		curveHighs = max(-0.35f, min(0.35f, curveHighs));
		curveMid = max(0.0f, min(1.0f, curveMid));
		float curveP[4] = { curveLows, curveHighs, curveMid,
			PostfxPipeChainReflRanFrame() && !s_tonemapIdentityGrade ? 1.0f : 0.0f };
		RwD3D9SetPixelShaderConstant(8, curveP, 1);

		// test-4 Symptom A neutral fallback: dead/unresolved luminance chain
		// (frameLumaTex NULL) must NOT leave s1 stale-bound. Force the
		// frame-adaptive term OFF — TonemapPass ignores s1 when c5.w < 0.5 —
		// and keep the timecycle exposure only.
		if(autoExposureOn && !frameLumaTex){
			tonemapP[3] = 0.0f;
			RwD3D9SetPixelShaderConstant(5, tonemapP, 1);
			if(dbglog_throttle("tonemap_lumfb"))
				dbglog("[tonemap] lum fallback: frameLumaTex NULL — auto-exposure off this draw, timecycle exposure only");
		}

		// Bind the frame luminance (s1) for frame-adaptive exposure.
		IDirect3DDevice9 *cfDev = d3d9device;
		// Raw sampler CLAMP below bypasses the rw cache (no L2 twin); the
		// pre-pass stage-1 state was captured for the restore after cleanup.
		if(autoExposureOn && frameLumaTex && cfDev){
			cfDev->GetSamplerState(1, D3DSAMP_ADDRESSU, &cfS1U);
			cfDev->GetSamplerState(1, D3DSAMP_ADDRESSV, &cfS1V);
			cfDev->GetSamplerState(1, D3DSAMP_MINFILTER, &cfS1Min);
			cfDev->GetSamplerState(1, D3DSAMP_MAGFILTER, &cfS1Mag);
			cfS1Saved = true;
			if((cfS1U != D3DTADDRESS_WRAP || cfS1V != D3DTADDRESS_WRAP ||
			    cfS1Min != D3DTEXF_LINEAR || cfS1Mag != D3DTEXF_LINEAR) &&
			   dbglog_throttle("pf_cf_samp"))
				dbglog("[PF] CF_Modern s1 sampler capture != WRAP/LINEAR: u=%lu v=%lu min=%lu mag=%lu (restored after tonemap)",
					cfS1U, cfS1V, cfS1Min, cfS1Mag);
			cfDev->SetTexture(1, frameLumaTex);
			cfDev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			cfDev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			cfDev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			cfDev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		}

		// --- TonemapLUT branch (W5 lane) ---
		// tonemapOperator > 0 swaps the Hable/Uncharted2 filmic curve for the
		// LUT operator shader (1-5 = ACES 1.3 / ACES 2.0 / AgX / Frostbite /
		// GT7 — the mapping is owned by TonemapLUT.hlsl); operator 0 keeps
		// this filmic path bit-for-bit. CONTRACT ASSUMPTION (TonemapLUT.hlsl
		// landed after this binding): same s0/s1 + c5-c8 contract as
		// TonemapPass — every constant uploaded above and the s1
		// auto-exposure bind stay valid for the LUT pass — plus
		// c9 = {operator, 0, 0, 0}. Falls back to the filmic path when the
		// LUT shader is missing.
		void *tmPS = tonemapPassPS;
		if(config->tonemapOperator > 0 && FxTonemapLutShader()){
			tmPS = FxTonemapLutShader();
			float opP[4] = { (float)config->tonemapOperator, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(9, opP, 1);
			if(dbglog_throttle("fx"))
				dbglog("[FX] TonemapLUT enable: operator=%d ps=%p (operator 0 = filmic)",
					config->tonemapOperator, tmPS);
		}
		{
			PERF_SCOPE("Tonemap");
			PF_SCOPE("TonemapPass");
			if(dbglog_throttle("pf_tonemap"))
				dbglog("[PF] TonemapPass: ps=%p identGrade=%d autoExp=%d lumaTex=%p operator=%d",
					tmPS, (int)s_tonemapIdentityGrade, (int)autoExposureOn,
					(void*)frameLumaTex, config ? config->tonemapOperator : -1);
			PfBindPS(tmPS, config->tonemapOperator > 0
				? "IDR_TONEMAPLUT(269) TonemapPass"
				: "IDR_TONEMAPPASS(166)");
			PfDraw(4, 2);
			// SEH-guarded choke-point (same as the pass-1 grading draw above):
			// resets overrideIm2dPixelShader on every exit incl. fault — a raw
			// fault here used to leave the tonemap PS bound, and the Im2D hook
			// would re-apply it to every later dispatch (coronas/HUD).
			guardedIm2DRender(tmPS, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_modern_tonemap");
			if(dbglog_throttle("pf_tonemap2"))
				dbglog("[PF] TonemapPass done: cameraRT = display frame, textures unbound below");
		}
		// Exposure-stack handshake: the tonemap/auto-exposure pass ran THIS
		// frame — DrawIVGrade (later in this same ColourFilter_switch) consumes
		// this once for its diagnostic log (see DrawIVGrade c0 upload).
		// No exposure cap any more: IVGrade audit F2 (2026-10-25).
		s_cfTonemapRanFrame = true;
		if(cfDev) cfDev->SetTexture(1, NULL);
	}

	// Restore all render states (match ColourFilter_PC pattern + FOG=TRUE).
	// The blend block / ZWRITE are deliberately NOT re-seeded here:
	// RestoreRawGeomStates below park-and-sets the CAPTURED pre-pass values
	// through all three layers (rw cache incl. the VERTEXALPHA twin, driver
	// cache, raw device) — that is the authoritative blend/ZWRITE tail.
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	// §4.7 corona-black fix: three-layer park-and-set of the CAPTURED pre-pass
	// raw block (postfx_utils.cpp RestoreOneRawGeomState idiom — "all 9 parks
	// verified correct", 2026-10-21 alpha-reversal audit). Ends the pass with
	// rw cache == driver cache == device on every blend/ZWRITE/blendop state,
	// so the next producer's entry contract (CCoronas/CPointLights ONE/ONE,
	// world SRCALPHA/INVSRCALPHA) always real-transitions instead of no-op'ing
	// against a cache that no longer matches the device. This is what stops
	// the sky-phase sprite flush (0x53DE6B) from rasterising the corona glow
	// quads REPLACE (black squares) when this pass inherited a drifted device.
	if(cfRawGeomSaved){
		RestoreRawGeomStates(cfRawGeom);
		cfRawGeomSaved = false;
	}

	// Full D3D9 pipeline cleanup after tonemap pass
	IDirect3DDevice9 *dev = d3d9device;
	if(dev){
		// Raw L3 sampler restore LAST — the rw state restores above re-emit
		// the cached texture filter and would clobber a raw min/mag restore.
		if(cfS1Saved){
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, cfS1U);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, cfS1V);
			dev->SetSamplerState(1, D3DSAMP_MINFILTER, cfS1Min);
			dev->SetSamplerState(1, D3DSAMP_MAGFILTER, cfS1Mag);
		}
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
	}
	if(dbglog_throttle("pf_cfm_done"))
		dbglog("[PF] CF_Modern done: s0-s2+PS/VS unbound, filter/z/fog restored to ONE-default");
}

void
CPostEffects::ColourFilter_Mobile(RwRGBA rgba1, RwRGBA rgba2)
{
	PF_SCOPE("ColourFilter_Mobile");
	if(dbglog_throttle("pf_cf_mobile"))
		dbglog("[PF] ColourFilter_Mobile: in=pRasterFrontBuffer out=cameraRT ps=%p",
			(void*)CPostEffects::pRasterFrontBuffer);
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	if(!Colorcycle::initialised)
		Colorcycle::Initialise();

	GradeColorset cset;
	Colorcycle::Update(&cset);
	Grade red, green, blue;
	red = cset.red;
	green = cset.green;
	blue = cset.blue;

	// Mobile colors
	float r = rgba1.red + rgba2.red;
	float g = rgba1.green + rgba2.green;
	float b = rgba1.blue + rgba2.blue;
	float invsqrt = 1.0f/max(sqrt(r*r + g*g + b*b), 1e-7f);
	r *= invsqrt;
	g *= invsqrt;
	b *= invsqrt;
	red.r = (1.5f + r*1.732f)*0.4f*red.r;
	green.g = (1.5f + g*1.732f)*0.4f*green.g;
	blue.b = (1.5f + b*1.732f)*0.4f*blue.b;

/*	// Fun trick: PS2 colour filter:
	float a = rgba2.alpha/128.0f;
	red.r = rgba1.red/128.0f + a*rgba2.red/128.0f;
	green.g = rgba1.green/128.0f + a*rgba2.green/128.0f;
	blue.b = rgba1.blue/128.0f + a*rgba2.blue/128.0f;
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;
*/
/*	// Also fun: PC colour filter:
	float a1 = rgba1.alpha/128.0f;
	float a2 = rgba2.alpha/128.0f;
	red.r = 1.0f + a1*rgba1.red/255.0f + a2*rgba2.red/255.0f;
	green.g = 1.0f + a1*rgba1.green/255.0f + a2*rgba2.green/255.0f;
	blue.b = 1.0f + a1*rgba1.blue/255.0f + a2*rgba2.blue/255.0f;
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;
*/


	RwD3D9SetPixelShaderConstant(0, &red, 1);
	RwD3D9SetPixelShaderConstant(1, &green, 1);
	RwD3D9SetPixelShaderConstant(2, &blue, 1);

	// contrast
	float mult[4];
	float add[4];
	mult[0] = red.r + red.g + red.b;
	mult[1] = green.r + green.g + green.b;
	mult[2] = blue.r + blue.g + blue.b;
	mult[3] = 1.0f;
	add[0] = red.a;
	add[1] = green.a;
	add[2] = blue.a;
	add[3] = 0.0f;

	RwD3D9SetPixelShaderConstant(3, mult, 1);
	RwD3D9SetPixelShaderConstant(4, add, 1);

	if(!(GetAsyncKeyState(VK_F5) & 0x8000))
		overrideIm2dPixelShader = gradingPS;
	else
		overrideIm2dPixelShader = contrastPS;
	// SEH-guarded choke-point: resets overrideIm2dPixelShader on every exit
	// incl. fault (same contract as ColourFilter_Generic / CF_Modern p1).
	guardedIm2DRender(overrideIm2dPixelShader, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_mobile");
	overrideIm2dPixelShader = nil;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

void
CPostEffects::ColourFilter_PS2(RwRGBA rgba1, RwRGBA rgba2)
{
	PF_SCOPE("ColourFilter_PS2");
	if(dbglog_throttle("pf_cf_ps2"))
		dbglog("[PF] ColourFilter_PS2: in=pRasterFrontBuffer out=cameraRT (fixed-function MODULATE2X+ADD)");
	if(dbglog_throttle( "cf_ps2"))
		dbglog("[PostFX] ColourFilter_PS2 ENTER rgba1=(%d,%d,%d,%d) rgba2=(%d,%d,%d,%d) pRasterFrontBuffer=%p",
			rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha,
			rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha,
			CPostEffects::pRasterFrontBuffer);

	RwIm2DVertex *verts;

	verts = colorfilterVerts;
	// Setup state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

	// Colors are already converted to PC space in ColourFilter_switch
	// Just use them directly with MODULATE2X
	overrideColorMod = D3DTOP_MODULATE2X;
	overrideAlphaMod = D3DTOP_MODULATE2X;

	// First color - replace
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DVertexSetIntRGBA(&verts[0], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[1], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[2], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[3], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	if(m_bBlurColourFilter){
		static RwIm2DVertex blurVerts[4];
		float rasterWidth = RwRasterGetWidth(CPostEffects::pRasterFrontBuffer);
		float rasterHeight = RwRasterGetHeight(CPostEffects::pRasterFrontBuffer);
		float scale = (Scene.camera && RwCameraGetRaster(Scene.camera))
			? RwRasterGetWidth(RwCameraGetRaster(Scene.camera))/640.0f : 1.0f;
		float leftOff   = m_colourLeftUOffset*scale   / 16.0f / rasterWidth;
		float rightOff  = m_colourRightUOffset*scale  / 16.0f / rasterWidth;
		float topOff    = m_colourTopVOffset*scale    / 16.0f / rasterHeight;
		float bottomOff = m_colourBottomVOffset*scale / 16.0f / rasterHeight;
		memcpy(blurVerts, verts, sizeof(blurVerts));
		RwIm2DVertexSetU(&blurVerts[0], RwIm2DVertexGetU(&blurVerts[0]) + leftOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[1], RwIm2DVertexGetU(&blurVerts[1]) + leftOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[2], RwIm2DVertexGetU(&blurVerts[2]) + rightOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[3], RwIm2DVertexGetU(&blurVerts[3]) + rightOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[0], RwIm2DVertexGetV(&blurVerts[0]) + topOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[3], RwIm2DVertexGetV(&blurVerts[3]) + topOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[1], RwIm2DVertexGetV(&blurVerts[1]) + bottomOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[2], RwIm2DVertexGetV(&blurVerts[2]) + bottomOff, 1.0f);
		verts = blurVerts;
	}

	// Second color - add
	// Colors are already converted to PC space in ColourFilter_switch
	uint8 r2 = rgba2.red;
	uint8 g2 = rgba2.green;
	uint8 b2 = rgba2.blue;
	uint8 a2 = rgba2.alpha;

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwIm2DVertexSetIntRGBA(&verts[0], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[1], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[2], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[3], r2, g2, b2, a2);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
 	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	// Restore state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	overrideColorMod = -1;
	overrideAlphaMod = -1;
}

void
CPostEffects::SetFilterMainColour_PS2(RwRaster *raster, RwRGBA color)
{
	PF_SCOPE("SetFilterMainColour_PS2");
	if(dbglog_throttle("pf_cf_setmain"))
		dbglog("[PF] SetFilterMainColour_PS2: ras=%p %dx%d blend=ONE/ONE",
			(void*)raster, raster ? RwRasterGetWidth(raster) : 0,
			raster ? RwRasterGetHeight(raster) : 0);
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[0], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[1], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[2], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[3], color.red, color.green, color.blue, color.alpha);
	overrideColorMod = D3DTOP_MODULATE2X;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideColorMod = -1;

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
}

void
CPostEffects::InfraredVision_PS2(RwRGBA c1, RwRGBA c2)
{
	PF_SCOPE("InfraredVision_PS2");
	if(dbglog_throttle("pf_ivps2"))
		dbglog("[PF] InfraredVision_PS2: rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d)",
			c1.red, c1.green, c1.blue, c1.alpha, c2.red, c2.green, c2.blue, c2.alpha);
	if(config->infraredVision != 0){
		InfraredVision(c1, c2);
		return;
	}

	CPostEffects::ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();

	float r = m_fInfraredVisionFilterRadius;
	// test-11 white-blowout lane audit: the 4-iteration ONE/ONE loop below is
	// STRUCTURALLY BOUNDED (fixed count; byte vertex colours cap each draw's
	// additive contribution at c1/255 <= 1.0 per channel) — no energy bound
	// needed. The unbounded input is this game-side radius float: NaN or a
	// hand-edited huge value would put NaN/huge UV offsets on all four
	// additive quads (undefined rasterizer output on an ONE/ONE pass).
	// Sanitize: NaN/negative -> 0 (offsets collapse, effect stays intact),
	// absurd magnitudes clamped to a generous 4.0 (vanilla is a fraction of
	// the screen; the ru/rv formula below scales it up).
	if(!(r >= 0.0f))
		r = 0.0f;
	if(r > 4.0f)
		r = 4.0f;
	// not sure this scales correctly, but it looks ok (need better brain)
	float ru = r * RsGlobal->MaximumWidth  / RwRasterGetWidth(ms_imf.frontBuffer)  * 1024.0f / 640.0f;
	float rv = r * RsGlobal->MaximumHeight / RwRasterGetHeight(ms_imf.frontBuffer) * 512.0f  / 448.0f;
	float uoff[4] = { -ru, ru, ru, -ru };
	float voff[4] = { -rv, -rv, rv, rv };

	// PS2 draws the filter triangle triangle...we draw the quad
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	for(int i = 0; i < 4; i++){
		DrawQuadSetUVs(ms_imf.tri_umin + uoff[i], ms_imf.tri_vmin + voff[i],
		               ms_imf.tri_umax + uoff[i], ms_imf.tri_vmin + voff[i],
		               ms_imf.tri_umax + uoff[i], ms_imf.tri_vmax + voff[i],
		               ms_imf.tri_umin + uoff[i], ms_imf.tri_vmax + voff[i]);
		DrawQuad(0, 0, RwRasterGetWidth(ms_imf.frontBuffer)*2, RwRasterGetHeight(ms_imf.frontBuffer)*2,
		                       c1.red, c1.green, c1.blue, 0xFFu, ms_imf.frontBuffer);

		UpdateFrontBuffer();
	}
	DrawQuadSetDefaultUVs();
	ImmediateModeRenderStatesReStore();

	SetFilterMainColour_PS2(ms_imf.frontBuffer, c2);
	UpdateFrontBuffer();
}

void
CPostEffects::NightVision_PS2(RwRGBA color)
{
	PF_SCOPE("NightVision_PS2");
	if(dbglog_throttle("pf_nvps2"))
		dbglog("[PF] NightVision_PS2: rgb=(%d,%d,%d,%d) blend=ONE/ONE",
			color.red, color.green, color.blue, color.alpha);
	if(config->nightVision != 0){
		CPostEffects::NightVision(color);
		return;
	}

	if(CPostEffects::m_fNightVisionSwitchOnFXCount > 0.0f){
		CPostEffects::m_fNightVisionSwitchOnFXCount -= CTimer__ms_fTimeStep;
		if(CPostEffects::m_fNightVisionSwitchOnFXCount <= 0.0f)
			CPostEffects::m_fNightVisionSwitchOnFXCount = 0.0f;
		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		// test-11 white-blowout lane: bound the ONE/ONE additive loop. n is a
		// raw game float (0xC40300) cast to int — a corrupted/stuck-large value
		// would stack n full-screen additive quads (each +8/255 ~= +0.031 per
		// channel) = guaranteed white blowout + multi-second frame stall. The
		// vanilla switch-on fade lives in the low tens of quads; the cap keeps
		// the fade ramp intact and only kills runaway values.
		int nRaw = (int)CPostEffects::m_fNightVisionSwitchOnFXCount;
		int n = nRaw;
		if(n < 0) n = 0;
		if(n > 64){
			n = 64;
			if(dbglog_throttle("nv_cap"))
				dbglog("[PF] NightVision_PS2: additive loop bound n=%d -> 64 "
					"(raw count float %.1f)", nRaw,
					CPostEffects::m_fNightVisionSwitchOnFXCount);
		}
		while(n--)
		        CPostEffects::DrawQuad(0.0f, 0.0f,
				RwRasterGetWidth(ms_imf.frontBuffer), RwRasterGetHeight(ms_imf.frontBuffer),
				8, 8, 8, 255, ms_imf.frontBuffer);
		CPostEffects::ImmediateModeRenderStatesReStore();
	}

	UpdateFrontBuffer();
	CPostEffects::SetFilterMainColour_PS2(ms_imf.frontBuffer, color);
	UpdateFrontBuffer();
}


// VU style random number generator -- taken from pcsx2
uint R;
void vrinit(uint x){ R = 0x3F800000 | x & 0x007FFFFF; }
void vradvance(void){
	int x = (R >> 4) & 1;
	int y = (R >> 22) & 1;
	R <<= 1;
	R ^= x ^ y;
	R = (R&0x7fffff)|0x3f800000;
}
inline uint vrget(void){ return R; }
inline uint vrnext(void){ vradvance(); return R; }

void
CPostEffects::Grain_PS2(int strength, bool generate)
{
	PF_SCOPE("Grain_PS2");
	if(dbglog_throttle("pf_grainps2"))
		dbglog("[PF] Grain_PS2: strength=%d generate=%d grainRas=%p",
			strength, (int)generate, (void*)grainRaster);
	if(!config->grainEnable)
		return;
	if(config->grainFilter != 0){
		CPostEffects::Grain(strength, generate);
		return;
	}

	// grainRaster is created in Initialise_skygfx without a failure check; a
	// NULL here (create failed) would fault inside RwRasterLock/DrawQuad.
	if(!grainRaster)
		return;

	if(generate){
		RwUInt8 *pixels = RwRasterLock(grainRaster, 0, 1);
		if(pixels){
			vrinit(rand());
			int x = vrget();
			for(int i = 0; i < 64*64; i++){
				*pixels++ = x;
				*pixels++ = x;
				*pixels++ = x;
				*pixels++ = x & strength;
				x = vrnext();
			}
			RwRasterUnlock(grainRaster);
		}
	}

	ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	float umin = 0.0f;
	float vmin = 0.0f;
	float umax = 5.0f * RsGlobal->MaximumWidth/640.0f;
	float vmax = 7.0f * RsGlobal->MaximumHeight/448.0f;

	DrawQuadSetUVs(umin, vmin,
		umax, vmin,
		umax, vmax,
		umin, vmax);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)D3DBLEND_DESTCOLOR);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)D3DBLEND_SRCALPHA);

	overrideColorMod = D3DTOP_SELECTARG2;	// ignore texture color
	overrideAlphaMod = D3DTOP_MODULATE2X;
	CPostEffects::DrawQuad(0.0, 0.0, RsGlobal->MaximumWidth, RsGlobal->MaximumHeight,
	                       0xFFu, 0xFFu, 0xFFu, 0xFF, grainRaster);
	overrideColorMod = -1;
	overrideAlphaMod = -1;

	DrawQuadSetDefaultUVs();
	CPostEffects::ImmediateModeRenderStatesReStore();
}

/////
///// IV mode grade — first real consumer of config->ivMode + iv* sliders
///// (previously dead config fields: read from INI, shown in the debug menu,
///// never read by any pass). Grade runs after the colour-filter switch on
///// the freshly graded/tonemapped frame; every slider maps to a live pixel
///// shader constant uploaded each frame.
/////

// ps_3_0 bytecode of IVGrade.hlsl (compiled with the project's FXC:
//   fxc /T ps_3_0 /E main IVGrade.hlsl)
// Registers:
//   c0 = {exposure, desaturation, gamma, 0}
//   c1 = {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
//   c2 = {1/screenW, 1/screenH, 0, 0}
//   s0 = scene (front buffer)
alignas(4) static const BYTE g_ivGradePSBytes[] =
{
      0,   3, 255, 255, 254, 255, 
     58,   0,  67,  84,  65,  66, 
     28,   0,   0,   0, 179,   0, 
      0,   0,   0,   3, 255, 255, 
      4,   0,   0,   0,  28,   0, 
      0,   0,   0,   1,   0,   0, 
    172,   0,   0,   0, 108,   0, 
      0,   0,   2,   0,   0,   0, 
      1,   0,   2,   0, 116,   0, 
      0,   0,   0,   0,   0,   0, 
    132,   0,   0,   0,   2,   0, 
      1,   0,   1,   0,   6,   0, 
    116,   0,   0,   0,   0,   0, 
      0,   0, 139,   0,   0,   0, 
      2,   0,   2,   0,   1,   0, 
     10,   0, 116,   0,   0,   0, 
      0,   0,   0,   0, 146,   0, 
      0,   0,   3,   0,   0,   0, 
      1,   0,   2,   0, 156,   0, 
      0,   0,   0,   0,   0,   0, 
    103, 114,  97, 100, 101,  48, 
      0, 171,   1,   0,   3,   0, 
      1,   0,   4,   0,   1,   0, 
      0,   0,   0,   0,   0,   0, 
    103, 114,  97, 100, 101,  49, 
      0, 103, 114,  97, 100, 101, 
     50,   0, 115,  99, 101, 110, 
    101,  84, 101, 120,   0, 171, 
      4,   0,  12,   0,   1,   0, 
      1,   0,   1,   0,   0,   0, 
      0,   0,   0,   0, 112, 115, 
     95,  51,  95,  48,   0,  77, 
    105,  99, 114, 111, 115, 111, 
    102, 116,  32,  40,  82,  41, 
     32,  72,  76,  83,  76,  32, 
     83, 104,  97, 100, 101, 114, 
     32,  67, 111, 109, 112, 105, 
    108, 101, 114,  32,  57,  46, 
     50,  57,  46,  57,  53,  50, 
     46,  51,  49,  49,  49,   0, 
     81,   0,   0,   5,   3,   0, 
     15, 160,  10, 215,  35,  60, 
      0,   0, 128,  63,   0,   0, 
      0,  63, 205, 204,  76,  61, 
     81,   0,   0,   5,   4,   0, 
     15, 160, 135,  22, 153,  62, 
    162,  69,  22,  63, 213, 120, 
    233,  61, 111,  18, 131,  58, 
     81,   0,   0,   5,   5,   0, 
     15, 160,   0,   0,   0,   0, 
      0,   0, 128,  64,   0,   0, 
      0, 128,   0,   0, 200,  66, 
     81,   0,   0,   5,   6,   0, 
     15, 160,   0,   0, 128,  62, 
      0,   0,  64, 191,   0,   0, 
      0, 192,   0,   0,  64,  64, 
     31,   0,   0,   2,   5,   0, 
      0, 128,   0,   0,   3, 144, 
     31,   0,   0,   2,   0,   0, 
      0, 144,   0,   8,  15, 160, 
      2,   0,   0,   3,   0,   0, 
      3, 128,   3,   0, 170, 161, 
      0,   0, 228, 144,  90,   0, 
      0,   4,   0,   0,   1, 128, 
      0,   0, 228, 128,   0,   0, 
    228, 128,   5,   0,   0, 160, 
      7,   0,   0,   2,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      6,   0,   0,   2,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      1,   0,   0,   2,   1,   0, 
     15, 128,   3,   0, 228, 160, 
      2,   0,   0,   3,   0,   0, 
      2, 128,   1,   0,   0, 129, 
      1,   0,  85, 160,   6,   0, 
      0,   2,   0,   0,   4, 128, 
      1,   0,  85, 160,  88,   0, 
      0,   4,   0,   0,   2, 128, 
      0,   0,  85, 128,   0,   0, 
    170, 128,   5,   0, 255, 160, 
      5,   0,   0,   3,   0,   0, 
     17, 128,   0,   0,  85, 128, 
      0,   0,   0, 128,   4,   0, 
      0,   4,   0,   0,   2, 128, 
      0,   0,   0, 128,   6,   0, 
    170, 160,   6,   0, 255, 160, 
      5,   0,   0,   3,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      0,   0,   0, 128,   4,   0, 
      0,   4,   0,   0,   1, 128, 
      0,   0,  85, 128,   0,   0, 
      0, 129,   3,   0,  85, 160, 
     11,   0,   0,   3,   2,   0, 
      1, 128,   0,   0,   0, 128, 
      5,   0,   0, 160,  11,   0, 
      0,   3,   0,   0,   1, 128, 
      1,   0, 170, 160,   1,   0, 
      0, 128,  32,   0,   0,   3, 
      3,   0,   1, 128,   2,   0, 
      0, 128,   0,   0,   0, 128, 
      2,   0,   0,   3,   0,   0, 
      1, 128,   3,   0,   0, 128, 
      3,   0,  85, 161,   4,   0, 
      0,   4,   0,   0,   1, 128, 
      1,   0,   0, 160,   0,   0, 
      0, 128,   1,   0,  85, 128, 
     11,   0,   0,   3,   2,   0, 
      1, 128,   0,   0,   0, 128, 
      5,   0,   0, 160,   1,   0, 
      0,   2,   0,   0,   2, 128, 
      5,   0,  85, 160,   5,   0, 
      0,   3,   0,   0,   5, 128, 
      0,   0,  85, 128,   2,   0, 
    212, 160,   1,   0,   0,   2, 
      3,   0,   9, 128,   0,   0, 
    164, 129,   1,   0,   0,   2, 
      3,   0,   6, 128,   5,   0, 
    170, 160,   2,   0,   0,   3, 
      3,   0,  15, 128,   3,   0, 
    228, 128,   0,   0,  68, 144, 
     66,   0,   0,   3,   4,   0, 
     15, 128,   3,   0, 228, 128, 
      0,   8, 228, 160,  66,   0, 
      0,   3,   3,   0,  15, 128, 
      3,   0, 238, 128,   0,   8, 
    228, 160,   4,   0,   0,   4, 
      0,   0,   9, 128,   2,   0, 
    100, 160,   0,   0,  85, 128, 
      0,   0, 100, 144,   1,   0, 
      0,   2,   0,   0,   6, 128, 
      0,   0, 196, 144,  66,   0, 
      0,   3,   5,   0,  15, 128, 
      0,   0, 228, 128,   0,   8, 
    228, 160,  66,   0,   0,   3, 
      0,   0,  15, 128,   0,   0, 
    238, 128,   0,   8, 228, 160, 
      2,   0,   0,   3,   2,   0, 
     14, 128,   4,   0, 144, 128, 
      5,   0, 144, 128,   2,   0, 
      0,   3,   0,   0,   7, 128, 
      0,   0, 228, 128,   2,   0, 
    249, 128,   2,   0,   0,   3, 
      0,   0,   7, 128,   3,   0, 
    228, 128,   0,   0, 228, 128, 
      4,   0,   0,   4,   0,   0, 
      7, 128,   0,   0, 228, 128, 
      6,   0,   0, 160,   6,   0, 
     85, 160,   5,   0,   0,   3, 
      2,   0,  14, 128,   0,   0, 
    144, 128,   1,   0, 255, 160, 
     88,   0,   0,   4,   0,   0, 
      7, 128,   0,   0, 228, 128, 
      2,   0, 249, 128,   5,   0, 
      0, 160,   2,   0,   0,   3, 
      0,   0,   8, 128,   1,   0, 
      0, 128,   0,   0,   0, 161, 
     88,   0,   0,   4,   0,   0, 
      8, 128,   0,   0, 255, 128, 
      1,   0,  85, 128,   0,   0, 
      0, 160,  66,   0,   0,   3, 
      3,   0,  15, 128,   0,   0, 
    228, 144,   0,   8, 228, 160, 
      5,   0,   0,   3,   2,   0, 
     14, 128,   0,   0, 255, 128, 
      3,   0, 144, 128,   8,   0, 
      0,   3,   1,   0,   1, 128, 
      2,   0, 249, 128,   4,   0, 
    228, 160,   4,   0,   0,   4, 
      3,   0,   7, 128,   3,   0, 
    228, 128,   0,   0, 255, 129, 
      1,   0,   0, 128,   5,   0, 
      0,   3,   0,   0,  24, 128, 
      1,   0, 170, 128,   0,   0, 
     85, 160,   4,   0,   0,   4, 
      1,   0,   7, 128,   0,   0, 
    255, 128,   3,   0, 228, 128, 
      2,   0, 249, 128,  88,   0, 
      0,   4,   1,   0,   7, 128, 
      0,   0, 255, 129,   2,   0, 
    249, 128,   1,   0, 228, 128, 
     11,   0,   0,   3,   2,   0, 
     14, 128,   1,   0, 144, 128, 
      5,   0,   0, 160,  15,   0, 
      0,   2,   3,   0,   1, 128, 
      2,   0,  85, 128,  15,   0, 
      0,   2,   3,   0,   2, 128, 
      2,   0, 170, 128,  15,   0, 
      0,   2,   3,   0,   4, 128, 
      2,   0, 255, 128,  11,   0, 
      0,   3,   0,   0,   8, 128, 
      0,   0, 170, 160,   1,   0, 
    255, 128,   6,   0,   0,   2, 
      1,   0,   8, 128,   0,   0, 
    255, 128,   2,   0,   0,   3, 
      0,   0,   8, 128,   0,   0, 
    255, 128,   3,   0,  85, 161, 
      2,   0,   0,   3,   0,   0, 
      8, 128,   0,   0, 255, 140, 
      4,   0, 255, 160,   5,   0, 
      0,   3,   2,   0,  14, 128, 
      3,   0, 144, 128,   1,   0, 
    255, 128,  14,   0,   0,   2, 
      3,   0,   1, 128,   2,   0, 
     85, 128,  14,   0,   0,   2, 
      3,   0,   2, 128,   2,   0, 
    170, 128,  14,   0,   0,   2, 
      3,   0,   4, 128,   2,   0, 
    255, 128,  88,   0,   0,   4, 
      1,   0,   7, 128,   0,   0, 
    255, 128,   1,   0, 228, 128, 
      3,   0, 228, 128,   2,   0, 
      0,   3,   0,   0,   7, 128, 
      0,   0, 228, 128,   1,   0, 
    228, 128,   1,   0,   0,   2, 
      3,   0,   9, 128,   1,   0, 
    228, 160,   2,   0,   0,   3, 
      2,   0,   6, 128,   3,   0, 
    204, 129,   4,   0, 255, 160, 
     88,   0,   0,   4,   0,   0, 
      7, 128,   2,   0,  85, 128, 
      1,   0, 228, 128,   0,   0, 
    228, 128,   5,   0,   0,   3, 
      1,   0,   7, 128,   2,   0, 
      0, 128,   0,   0, 228, 128, 
     88,   0,   0,   4,   0,   8, 
      7, 128,   2,   0, 170, 128, 
      0,   0, 228, 128,   1,   0, 
    228, 128,   1,   0,   0,   2, 
      0,   8,   8, 128,   3,   0, 
     85, 160, 255, 255,   0,   0
};


static void *s_ivGradePS = NULL;
// Device the shader was created on. d3d9device identity changes on a full
// device re-create (not on Reset — Reset keeps the same IDirect3DDevice9), so
// this only self-heals the re-create case. The Reset case is handled by
// PostFX_IVGradeInvalidateShader(), which the Reset lane must call (wiring
// outside this file — see the function comment).
static IDirect3DDevice9 *s_ivGradePSDevice = NULL;
static bool s_ivGradePSFailed = false;

// Camera-sized frame copy that feeds the IV grade (see DrawIVGrade): the
// grade's tex-space math (vignette centred at 0.5, c2 bloom taps =
// 1/screenW) assumes UV 0..1 spans a FULL frame of content, but
// pRasterFrontBuffer is a padded (e.g. 2048x2048) raster with the frame only
// in its top-left — sampling it with a UV 0..1 quad read black below
// y = h*h/fbH (the IV-mode black band). Private w x h copy = content fills
// UV 0..1. Managed like Blur_VCS's lastFrameBuffer (PushContext + RenderFast).
RwRaster *s_ivGradeRas = NULL;
int s_ivGradeW = 0, s_ivGradeH = 0;

static void*
EnsureIVGradePS(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(s_ivGradePS && s_ivGradePSDevice == dev)
		return s_ivGradePS;
	if(s_ivGradePS){
		// Device object was replaced — the shader died with the old device.
		// Forget the pointer (never Release() a dead device's shader object)
		// and clear the sticky failure latch so it can be retried.
		s_ivGradePS = NULL;
		s_ivGradePSDevice = NULL;
		s_ivGradePSFailed = false;
	}
	if(s_ivGradePSFailed)
		return NULL;
	if(!dev){
		s_ivGradePSFailed = true;
		return NULL;
	}

	// Shader-source policy for THIS build: the shipped byte array stays the
	// default. shaders/ps/IVGrade.hlsl (-> cso/IVGrade.cso, IDR_IVGRADEPS)
	// compiles to the same 67 ps_3_0 slots and the same stage algebra, but
	// it is NOT bit-identical to g_ivGradePSBytes: 70 ops / 285 vs 283
	// instruction-stream DWORDs, DEF immediates repacked across c3-c6, a
	// different 4-tap summation order (FP add order), and cmp_sat instead of
	// cmp on oC0. Do not silently swap the shipped grade look: flip this to
	// true only after a visual A/B. The byte array is also the automatic
	// fallback if the resource is missing/unusable.
	static const bool s_ivGradeUseCSO = false;
	static bool s_ivGradeSourceLogged = false;
	bool fromCSO = false;
	unsigned srcSize = 0;

	if(s_ivGradeUseCSO){
		// Standard postfx deferred-load path (same chokepoint as
		// postfx_newfx.cpp / mirrorPass.cpp / postfx_underwater.cpp).
		// makePS only creates when the handle is NULL — it is NULL here via
		// the device-identity invalidate above or InvalidateShader().
		makePS(IDR_IVGRADEPS, &s_ivGradePS);
		if(s_ivGradePS){
			HRSRC res = FindResource(dllModule, MAKEINTRESOURCE(IDR_IVGRADEPS), RT_RCDATA);
			srcSize = res ? SizeofResource(dllModule, res) : 0;
			fromCSO = true;
		}else{
			// Resource missing/rejected — fall through to the shipped bytes
			// so the grade never goes dark.
			dbglog("[IVGrade] CSO load failed (res=%d), falling back to byte array",
				(int)IDR_IVGRADEPS);
		}
	}

	if(!s_ivGradePS){
		HRESULT hr = dev->CreatePixelShader((const DWORD*)g_ivGradePSBytes,
			(IDirect3DPixelShader9**)&s_ivGradePS);
		if(FAILED(hr) || !s_ivGradePS){
			dbglog("[PostFX] IVGrade CreatePixelShader FAILED hr=0x%08X", (unsigned)hr);
			s_ivGradePS = NULL;
			s_ivGradePSFailed = true;
			return NULL;
		}
		srcSize = (unsigned)sizeof(g_ivGradePSBytes);
	}

	s_ivGradePSDevice = dev;
	if(!s_ivGradeSourceLogged){
		s_ivGradeSourceLogged = true;
		dbglog("[IVGrade] shader source=%s size=%u", fromCSO ? "cso" : "bytes", srcSize);
	}
	dbglog("[PostFX] IVGrade pixel shader created sh=%p dev=%p", s_ivGradePS, (void*)dev);
	return s_ivGradePS;
}

// Device-reset lifetime (IVGrade audit 2026-10-25 F5 / B7): the raw
// CreatePixelShader handle is dead after IDirect3DDevice9::Reset, but
// EnsureIVGradePS would otherwise return the cached pointer forever.
// Null the stale handle (NEVER Release() a post-Reset shader object — same
// rule pipelinecommon.cpp:816 follows) and clear the sticky failure latch so
// the next DrawIVGrade recreates it on the new device generation.
// WIRING PENDING (outside this file): call ONCE in the POST-Reset block next to
// Pipeline_InvalidateShaders() (depthhook.cpp:387, inside `if(SUCCEEDED(hr))`)
// — do NOT call it pre-Reset: nulling there would orphan a still-live shader
// object instead of letting Reset retire it. Calling it twice is harmless.
// DrawIVGrade also self-heals without wiring: ReleaseDefaultPoolResources
// destroys s_ivGradeRas and zeroes the dims, which the raster block below
// detects as a Reset and turns into an invalidate+recreate.
void PostFX_IVGradeInvalidateShader(void)
{
	if(s_ivGradePS)
		dbglog("[PostFX] IVGrade PS invalidated for device reset (sh=%p)", s_ivGradePS);
	s_ivGradePS = NULL;
	s_ivGradePSDevice = NULL;
	s_ivGradePSFailed = false;
}

void
DrawIVGrade(void)
{
	PF_SCOPE("DrawIVGrade");
	// Gate decision breadcrumb (lane-A, build-2 outdoor-occluder hunt):
	// the log line "[IVGrade] 0.00 ms" is PerfTimer's UNCONDITIONAL scope
	// exit line (skygfx.h:79 `~PerfTimer(){ dbglog(...) }`) — it fires even
	// when this function returns instantly at the gate below, so it is NOT
	// evidence the grade ran. This throttle logs the ACTUAL gate input
	// every ~2s so the next test proves either way: ivMode=0 + no
	// "DrawIVGrade:" lines => grade never ran, occluder is elsewhere.
	// ivMode writers to watch: the mode manager (ModeManager_SetMode /
	// ModeManager_ClearLatch), menu_inject "GTA IV Mode" toggle
	// (menu_inject.cpp:123), presets.cpp:130, debug-menu checkbox
	// (ivModeRowEdit / ivModeLegacyRowEdit). derivePipelineFromPipes no
	// longer writes ivMode (main.cpp:599-602 — explicit writes win).
	if(dbglog_throttle("ivgrade_gate"))
		dbglog("[PostFX] IVGrade GATE: ivMode=%d pipeline=%d buildingPipe=%d vehiclePipe=%d cfEnable=%d -> %s",
			config ? (int)config->ivMode : -1,
			config ? config->pipeline : -1,
			config ? config->buildingPipe : -1,
			config ? config->vehiclePipe : -1,
			config ? (int)config->colorFilterEnable : -1,
			(config && config->ivMode) ? "RUN" : "SKIP");
	if(!config || !config->ivMode)
		return;
	// True when the PS outlived the previous frame on THIS device (i.e. it may
	// be a pre-Reset handle). Used by the reset fallback at the raster block.
	bool psWasCached = (s_ivGradePS != NULL) && (s_ivGradePSDevice == d3d9device);
	void *ps = EnsureIVGradePS();
	if(!ps)
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	// Screen-size doctrine: size the private grade copy from the trusted
	// cache (live camRas only as the pre-capture fallback). If the camera
	// raster is temporarily pointed at a non-screen raster (envmap/
	// reflection), skip — copying it would composite at the wrong scale.
	int w = 0, h = 0;
	if(!GetScreenSize(&w, &h)){
		w = camRas->width;
		h = camRas->height;
	}
	if(w < 1 || h < 1)
		return;
	if(camRas->width != w || camRas->height != h){
		if(dbglog_throttle("ivgrade_skip"))
			dbglog("[PostFX] DrawIVGrade SKIP: camRas %dx%d != screen %dx%d (transient raster)",
				camRas->width, camRas->height, w, h);
		return;
	}
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	// Cutscene≠menu (expIV gate fix): the IV grade is part of the grading
	// chain — cutscene frames get it too. Only real menu/pause or the game's
	// disable flag skips here. (IsGameInMenuOrPaused in postfx_utils.cpp
	// still bundles cutscene for lanes outside this fix.) NOTE: with
	// ivMode=0 the grade still never runs — that is the config gate at the
	// top of this function, not this one.
	if(PostfxMenuPauseOnly())
		return;
	static bool s_csFired_iv = false;
	if(CCutsceneMgr__ms_running)
		CutscenePostfxEnabledLog("IVGrade", &s_csFired_iv);

	if(dbglog_throttle("ivgrade"))
		dbglog("[PostFX] DrawIVGrade: desat=%.2f gamma=%.2f exposure=%.2f vig=(%.2f,%.2f,%.2f) bloom=%.2f",
			config->ivDesaturation, config->ivGamma, config->ivExposure,
			config->ivVignetteIntensity, config->ivVignetteRadius,
			config->ivVignetteContrast, config->ivBloomIntensity);

	// Feed the grade a CAMERA-SIZED frame copy instead of pRasterFrontBuffer.
	// Root cause of the IV-mode black band: the front buffer is a padded
	// (2048x2048) raster holding the 1920x1080 frame 1:1 at its TOP-LEFT
	// (UpdateFrontBuffer = RwRasterRenderFast at 0,0), while s_ffQuad maps
	// UV 0..1 across the screen — screen rows below y = h*h/fbH (~570 on
	// 1080/2048) sampled past the content into black. A private w x h copy
	// has content filling UV 0..1, so the grade's authored tex-space math
	// (vignette centred at 0.5, c2 bloom taps = 1/screenW) stays exact.
	// The game's own colorfilterVerts avoid the band with 0..2048 positions
	// + UV 0..1 (visible window = content region) — same idea, different
	// mechanism (we can't move positions without breaking tex 0..1 math).
	// Mirrors Blur_VCS's lastFrameBuffer copy (PushContext + RenderFast).
	if(!s_ivGradeRas || s_ivGradeW != w || s_ivGradeH != h){
		// Reset fallback (audit F5): ReleaseDefaultPoolResources destroys this
		// raster on the pre-Reset path AND zeroes s_ivGradeW/H, but it cannot
		// see the raw CreatePixelShader handle. So "cached PS from an earlier
		// frame + raster dims zeroed" is exactly a device Reset — drop the
		// stale PS and rebuild it here. The explicit
		// PostFX_IVGradeInvalidateShader() wiring remains the primary path;
		// this keeps the pass self-healing when it is not wired. (A failed
		// raster create leaves W/H non-zero, so it does not retrigger this.)
		if(!s_ivGradeRas && s_ivGradeW == 0 && s_ivGradeH == 0 && psWasCached){
			PostFX_IVGradeInvalidateShader();
			ps = EnsureIVGradePS();
			if(!ps)
				return;
		}
		if(s_ivGradeRas){ RwRasterDestroy(s_ivGradeRas); s_ivGradeRas = NULL; }
		s_ivGradeRas = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
		s_ivGradeW = w;
		s_ivGradeH = h;
		if(!s_ivGradeRas){
			dbglog("[PostFX] DrawIVGrade: frame-copy raster create failed %dx%d", w, h);
			return;
		}
	}
	{
		// Copy the CURRENT post-colour-filter camera frame into the private
		// raster (same EndUpdate/PushContext/RenderFast/BeginUpdate dance as
		// UpdateFrontBuffer). Fail-open: if the copy fails, skip the grade
		// this frame rather than grading garbage.
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(s_ivGradeRas);
		RwRaster *copyResult = RwRasterRenderFast(camRas, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		if(!copyResult){
			dbglog("[PostFX] DrawIVGrade: frame copy failed (RenderFast NULL), skipping grade");
			return;
		}
	}

	SetupFullscreenQuad((float)w, (float)h);

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // no depth hook in this pass
	void *prevAddr = NULL;                          // TEXTUREADDRESS is outside
	RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &prevAddr); // the Im2D bracket
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	// Audit F6: the ±4 px bloom taps at the frame border must not wrap, and the
	// pass must not inherit an additive blend from a preceding pass. Blend is
	// restored by ImmediateModeRenderStatesReStore(); TEXTUREADDRESS by hand.
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s_ivGradeRas);

	// c0: {exposure, desaturation, gamma, 0} — live from the debug-menu sliders.
	// Audit 2026-10-25 F1: this pass runs on a DISPLAY-REFERRED frame (the GTAIV
	// colour filter is a pure bypass — no tonemap/LinearToSRGB runs before it in
	// that mode), while IV multiplies LINEAR HDR exposure before tone mapping.
	// pow(ex, 1/2.2) is the display-space equivalent: ivExposure=2.5 becomes
	// 1.52x on screen instead of 2.5x, removing the hard clip of everything
	// above sRGB 0.40 and the grey wash.
	// The old s_ivExpCap clamp is GONE (audit F2): it never fired on the shipped
	// IV preset because COLORFILTER_GTAIV never runs CF_Modern, so
	// s_cfTonemapRanFrame stayed false. The handshake is diagnostic only now.
	bool tonemapRanThisFrame = s_cfTonemapRanFrame;
	s_cfTonemapRanFrame = false;      // consume-once: only the IVGrade call
	                                  // right after CF_Modern sets it
	float ex = (config->ivExposure > 0.01f) ? config->ivExposure : 1.0f;
	float exDisplay = powf(ex, 1.0f / 2.2f);
	float gm = (config->ivGamma > 0.05f) ? config->ivGamma : 1.0f;
	float c0[4] = { exDisplay, config->ivDesaturation, gm, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);
	// c1: {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
	float c1[4] = {
		config->ivVignetteIntensity,
		config->ivVignetteRadius,
		config->ivVignetteContrast,
		config->ivBloomIntensity
	};
	RwD3D9SetPixelShaderConstant(1, c1, 1);
	// c2: {1/screenW, 1/screenH, 0, 0} — bloom tap offsets
	float c2[4] = { 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f), 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	overrideIm2dPixelShader = ps;
	if(dbglog_throttle("pf_iv_run"))
		dbglog("[PF] DrawIVGrade RUN: ps=%p src=s_ivGradeRas(%p %dx%d) out=cameraRT c0=(expDisp %.2f desat %.2f gamma %.2f tonemapRan=%d)",
			ps, (void*)s_ivGradeRas, w, h, exDisplay, config->ivDesaturation, gm,
			tonemapRanThisFrame ? 1 : 0);
	PfBindPS(ps, "IVGradePS (embedded g_ivGradePSBytes)");
	PfDraw(4, 2);
	// SEH-guarded choke-point: resets overrideIm2dPixelShader on every exit
	// incl. fault — a raw fault here would leave the IV-grade PS bound and
	// the Im2D hook would re-apply it to every later dispatch (coronas/HUD).
	guardedIm2DRender(ps, rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6, "ivgrade");

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, prevAddr);  // audit F6 restore
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	// Leave the front buffer in sync so motion blur / height fog / god rays
	// sample the IV-graded frame even if they skip their own start-sync.
	CPostEffects::UpdateFrontBuffer();
	if(dbglog_throttle("pf_iv_done"))
		dbglog("[PF] DrawIVGrade done: FB resynced, states restored (filter/addr/z/fog/alphaTest; blend via Im2D)");
}


// Colorcycle stuff, partly taken from NTAuthority...at least originally

class CFileMgr
{
public:
	static void* OpenFile(const char* filename, const char* mode);

	static void  CloseFile(void* file);
};

class CFileLoader
{
public:
	static char* LoadLine(void* file);
};

WRAPPER void* CFileMgr::OpenFile(const char* filename, const char* mode) { EAXJMP(0x538900); }
WRAPPER void  CFileMgr::CloseFile(void* file) { EAXJMP(0x5389D0); }
WRAPPER char* CFileLoader::LoadLine(void* file) { EAXJMP(0x536F80); }

static int &CTimeCycle__m_ExtraColourWeatherType = *(int*)0xB79E40;
static int &CTimeCycle__m_ExtraColour = *(int*)0xB79E44;
static int &CTimeCycle__m_bExtraColourOn = *(int*)0xB7C484;
static float &CTimeCycle__m_ExtraColourInter = *(float*)0xB79E3C;
static float &CWeather__UnderWaterness = *(float*)0xC8132C;
static float &CWeather__InTunnelness = *(float*)0xC81334;
static int &tunnelWeather = *(int*)0x8CDEE0;


// 24 instead of NUMHOURS because we might be using timecycle_24h with extended extra colour hours
Grade Colorcycle::redGrade[24][NUMWEATHERS];
Grade Colorcycle::greenGrade[24][NUMWEATHERS];
Grade Colorcycle::blueGrade[24][NUMWEATHERS];
bool Colorcycle::initialised;

GradeColorset::GradeColorset(int h, int w)
{
	this->red = Colorcycle::redGrade[h][w];
	this->green = Colorcycle::greenGrade[h][w];
	this->blue = Colorcycle::blueGrade[h][w];
}

void
GradeColorset::Interpolate(GradeColorset *a, GradeColorset *b, float fa, float fb)
{
	this->red.r = fa * a->red.r + fb * b->red.r;
	this->red.g = fa * a->red.g + fb * b->red.g;
	this->red.b = fa * a->red.b + fb * b->red.b;
	this->red.a = fa * a->red.a + fb * b->red.a;
	this->green.r = fa * a->green.r + fb * b->green.r;
	this->green.g = fa * a->green.g + fb * b->green.g;
	this->green.b = fa * a->green.b + fb * b->green.b;
	this->green.a = fa * a->green.a + fb * b->green.a;
	this->blue.r = fa * a->blue.r + fb * b->blue.r;
	this->blue.g = fa * a->blue.g + fb * b->blue.g;
	this->blue.b = fa * a->blue.b + fb * b->blue.b;
	this->blue.a = fa * a->blue.a + fb * b->blue.a;
}

static int timecycleHours[] = { 0, 5, 6, 7, 12, 19, 20, 22, 24 };

void
Colorcycle::Update(GradeColorset *colorset)
{
	float time;
	int curHourSel, nextHourSel;
	int curHour, nextHour;
	float timeInterp, invTimeInterp, weatherInterp, invWeatherInterp;

	time = CClock__ms_nGameClockMinutes / 60.0f
	     + CClock__ms_nGameClockSeconds / 3600.0f
	     + CClock__ms_nGameClockHours;
	if(time >= 23.999f)
		time = 23.999f;

	for(curHourSel = 0; time >= timecycleHours[curHourSel+1]; curHourSel++);
	nextHourSel = (curHourSel + 1) % NUMHOURS;
	curHour = timecycleHours[curHourSel];
	nextHour = timecycleHours[curHourSel+1];
	timeInterp = (time - curHour) / (float)(nextHour - curHour);
	invTimeInterp = 1.0f - timeInterp;
	weatherInterp = CWeather__InterpolationValue;
	invWeatherInterp = 1.0f - weatherInterp;
	GradeColorset curold(curHourSel, CWeather__OldWeatherType);
	GradeColorset nextold(nextHourSel, CWeather__OldWeatherType);
	GradeColorset curnew(curHourSel, CWeather__NewWeatherType);
	GradeColorset nextnew(nextHourSel, CWeather__NewWeatherType);

	// Skipping smog weather handling
	GradeColorset oldInterp, newInterp;
	oldInterp.Interpolate(&curold, &nextold, invTimeInterp, timeInterp);
	newInterp.Interpolate(&curnew, &nextnew, invTimeInterp, timeInterp);
	colorset->Interpolate(&oldInterp, &newInterp, invWeatherInterp, weatherInterp);

	float inc = CTimer__ms_fTimeStep/120.0f;
	if(CTimeCycle__m_bExtraColourOn){
		CTimeCycle__m_ExtraColourInter += inc;
		if(CTimeCycle__m_ExtraColourInter > 1.0f)
			CTimeCycle__m_ExtraColourInter = 1.0f;
	}else{
		CTimeCycle__m_ExtraColourInter -= inc;
		if(CTimeCycle__m_ExtraColourInter < 0.0f)
			CTimeCycle__m_ExtraColourInter = 0.0f;
	}
	if(CTimeCycle__m_ExtraColourInter > 0.0f){
		GradeColorset extraset(CTimeCycle__m_ExtraColour, CTimeCycle__m_ExtraColourWeatherType);
		colorset->Interpolate(colorset, &extraset, 1.0f-CTimeCycle__m_ExtraColourInter, CTimeCycle__m_ExtraColourInter);
	}

	if(CWeather__UnderWaterness > 0.0f){
		GradeColorset curuwset(curHourSel, 20);
		GradeColorset nextuwset(nextHourSel, 20);
		GradeColorset tmpset;
		tmpset.Interpolate(&curuwset, &nextuwset, invTimeInterp, timeInterp);
		colorset->Interpolate(colorset, &tmpset, 1.0f-CWeather__UnderWaterness, CWeather__UnderWaterness);
	}

	if(CWeather__InTunnelness > 0.0f){
		GradeColorset tunnelset(tunnelWeather % NUMHOURS, tunnelWeather / NUMHOURS + EXTRASTART);
		colorset->Interpolate(colorset, &tunnelset, 1.0f-CWeather__InTunnelness, CWeather__InTunnelness);
	}

}

void
Colorcycle::Initialise(void)
{
	int have24h = ModuleList().Get(L"timecycle24") != 0;
	for(int i = 0; i < 24; i++)
		for(int j = 0; j < NUMHOURS; j++){
			redGrade[j][i].r = 1.0f;
			redGrade[j][i].g = 0.0f;
			redGrade[j][i].b = 0.0f;
			redGrade[j][i].a = 0.0f;
			greenGrade[j][i].r = 0.0f;
			greenGrade[j][i].g = 1.0f;
			greenGrade[j][i].b = 0.0f;
			greenGrade[j][i].a = 0.0f;
			blueGrade[j][i].r = 0.0f;
			blueGrade[j][i].g = 0.0f;
			blueGrade[j][i].b = 1.0f;
			blueGrade[j][i].a = 0.0f;
		}
	void *f = CFileMgr::OpenFile("data/colorcycle.dat", "r");
	if(f){
		char *line;
		for(int i = 0; i < NUMWEATHERS; i++){
			for(int j = 0; j < NUMHOURS; j++){
				line = CFileLoader::LoadLine(f);
				// EOF/short file: LoadLine returns NULL — sscanf(NULL, ...)
				// faults. Keep the identity defaults already seeded above and
				// stop parsing instead.
				if(!line)
					break;
				sscanf(line, "%f %f %f %f %f %f %f %f %f %f %f %f",
				       &redGrade[j][i].r, &redGrade[j][i].g,
				       &redGrade[j][i].b, &redGrade[j][i].a,
				       &greenGrade[j][i].r, &greenGrade[j][i].g,
				       &greenGrade[j][i].b, &greenGrade[j][i].a,
				       &blueGrade[j][i].r, &blueGrade[j][i].g,
				       &blueGrade[j][i].b, &blueGrade[j][i].a);
				float sum;
				sum = redGrade[j][i].r + redGrade[j][i].g + redGrade[j][i].b;
				if(sum > 1.7f)
					redGrade[j][i].a -= (sum - 1.7f)*0.13f;
				sum = greenGrade[j][i].r + greenGrade[j][i].g + greenGrade[j][i].b;
				if(sum > 1.7f)
					greenGrade[j][i].a -= (sum - 1.7f)*0.13f;
				sum = blueGrade[j][i].r + blueGrade[j][i].g + blueGrade[j][i].b;
				if(sum > 1.7f)
					blueGrade[j][i].a -= (sum - 1.7f)*0.13f;


				redGrade[j][i].r /= 1.5f;
				redGrade[j][i].g /= 1.5f;
				redGrade[j][i].b /= 1.5f;
				redGrade[j][i].a /= 1.5f;
				greenGrade[j][i].r /= 1.5f;
				greenGrade[j][i].g /= 1.5f;
				greenGrade[j][i].b /= 1.5f;
				greenGrade[j][i].a /= 1.5f;
				blueGrade[j][i].r /= 1.5f;
				blueGrade[j][i].g /= 1.5f;
				blueGrade[j][i].b /= 1.5f;
				blueGrade[j][i].a /= 1.5f;
				//printf("%f %f %f %f X %f %f %f %f X %f %f %f %f\n",
				//	redGrade[j][i].r, redGrade[j][i].g, redGrade[j][i].b, redGrade[j][i].a,
				//	greenGrade[j][i].r, greenGrade[j][i].g, greenGrade[j][i].b, greenGrade[j][i].a,
				//	blueGrade[j][i].r, blueGrade[j][i].g, blueGrade[j][i].b, blueGrade[j][i].a);
			}
		}
		if(have24h)
			for(int j = 0; j < NUMHOURS; j++){
				redGrade[j+8][21] = redGrade[j][22];
				greenGrade[j+8][21] = greenGrade[j][22];
				blueGrade[j+8][21] = blueGrade[j][22];
			}
		CFileMgr::CloseFile(f);
	}
	initialised = true;
}
