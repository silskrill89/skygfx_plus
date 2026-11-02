// TonemapPass.hlsl - Adaptive Hable/Uncharted 2 filmic tonemapping + exact sRGB gamma (ps_3_0)
// Runs as a fullscreen pass AFTER color grading.
//
// Uses CryEngine's FilmMapping() base parameters (HDRPostProcess.cfx lines 282-311).
// Toe strength is now timecycle-driven: night/dawn lifts shadows more aggressively,
// midday keeps tighter contrast. Exposure adapts to scene luminance.
//
// Reads graded linear HDR from pRasterFrontBuffer, outputs gamma-corrected LDR.
//
// c5 = tonemapParams (timecycle COLOUR + exposure bias):
//   x = exposure bias (brightness slider × carcols × sun dampening × garage dampen)
//   y = toeStrength (0.06-0.30, night~0.10 for shadow lift, midday~0.17 solid blacks)
//   z = timecycle sceneLuma (fallback when auto-exposure is off)
//   w = flags (1.0 = auto-exposure ON -> brightness taken from the rendered frame)
//
// c6 = gradeParams (fully timecycle-driven from CColourSet):
//   x = brightness (0.03-0.07, from timecycle lightsOnGroundBrightness)
//   y = contrast (1.15-1.40, from timecycle shadowStrength × fogStart)
//   z = lift (0.00-0.025, from timecycle cloudAlpha + fogStart)
//   w = curves blend (0.25-0.50, from timecycle sceneLuma)
//
// c7 = blackLiftParams:
//   x = blackLift (0.0-0.05, sRGB black-level lift to preserve shadow detail)
//   y = minExposure (auto-exposure clamp low, [-0.5..0.5]; negative = darkening)
//   z = maxExposure (auto-exposure clamp high, [0.2..4.0] — menu-aligned 2026-10-21)
//   w = keyStrength (0 = pure timecycle exposure, 1 = full frame-adaptive)
//
// c8 = curveParams (user pivot curve, applied at the OUTPUT):
//   x = Curve Intensity Lows  [-1..1], default 0 (0 = identity)
//   y = Curve Intensity Highs [-1..1], default 0 (0 = identity)
//   z = Curve Mid Point       [0..1],  default 0.5 (pivot)
//   w = cf_gain soft-knee gate flag: 1 = DrawPipeChain pass-3 composited the
//       REAL classify pack this frame (measured-luma knee armed); 0 = identity
//       (knee lerp term forced to 0 => exact x1.0 no-op)

uniform sampler2D tex : register(s0);
uniform sampler2D lumTex : register(s1);
uniform float4 tonemapParams : register(c5);
uniform float4 gradeParams : register(c6);
uniform float4 blackLiftParams : register(c7);
uniform float4 curveParams : register(c8);

struct PS_INPUT
{
	float3 texcoord0 : TEXCOORD0;
};

// Hable/Uncharted 2 filmic tonemapping — CryEngine base + adaptive toe
// Reference: CryEngine HDRPostProcess.cfx FilmMapping() with default HDRFilmCurve=(1,1,1,1)
// ShoStren=0.22, LinStren=0.30, LinAngle=0.10, ToeNum=0.01, ToeDenom=0.30
// ToeStren is now a uniform (c5.y) driven by timecycle scene luminance.
//
// Output range: [0, ~0.8] for inputs [0, 5] — no white scaling needed.
// After sRGB gamma this maps to a natural, contrasty LDR image.
float3 FilmicTonemap(float3 x, float toeStrength)
{
	const float A = 0.22;          // Shoulder Strength
	const float B = 0.30;          // Linear Strength
	const float CB = 0.03;         // Linear Angle × Linear Strength (0.10 × 0.30)
	// D = toeStrength (adaptive): night=0.35 lifts shadows, midday=0.12 keeps contrast
	const float D = toeStrength;
	const float E = 0.01;          // Toe Numerator
	const float F = 0.30;          // Toe Denominator
	return ((x * (A * x + CB) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

#include "../include/TonemapCommon.hlsl"

// Soft-knee highlight compression: gracefully compress values above 1.0
// Prevents hard clamping of extreme highlights (sun disc, specular hot-spots).
// At x=1: y=1 (continuous). At x=5: y≈2.3. At x=50: y≈5.5.
// This spreads out extreme values so the filmic curve can distinguish them.
float3 SoftKnee(float3 x)
{
	float3 r = max(x - 1.0, 0.0);
	// Logarithmic compression above 1.0 — preserves gradient detail in highlights
	return min(x, 1.0 + log(1.0 + r) * 0.65);
}

float4 main(PS_INPUT IN) : COLOR
{
	float3 c = tex2D(tex, IN.texcoord0.xy);

	// === Unified exposure: timecycle supplies COLOUR, the frame supplies BRIGHTNESS ===
	// Base bias always applies (menu brightness, carcols adapt, sun/garage dampening).
	float exposure = tonemapParams.x;
	float sceneLuma = tonemapParams.z;
	if (tonemapParams.w >= 0.5)
	{
		// CryEngine-style auto-exposure (mode 2): scene key from measured luminance.
		float lum = max(tex2D(lumTex, float2(0.5, 0.5)).r, 1e-4);
		float key = 1.03 - 2.0 / (2.0 + log2(lum + 1.0));
		float autoExp = clamp(key / lum, blackLiftParams.y, blackLiftParams.z);
		exposure *= lerp(1.0, autoExp, saturate(blackLiftParams.w));
		// cf_gain soft-knee (2026-10-21 washout rework): DrawPipeChain pass-3
		// re-presents the scene ~2.01x when the REAL classify pack is bound on
		// s4. The knee runs on the MEASURED lum here (the CPU timecycle-proxy
		// sceneLuma reads near-black on vanilla alt-timecyc and never armed):
		// identity at lum<=0.05, ramping to x0.5 at lum>=0.25. Gate flag
		// c8.w=0 forces kneeK=0 => lerp(1.0, 0.5, 0)=1.0 exact no-op (the
		// identity-grade bypass and every non-composited frame stay bit-exact).
		// SM3.0-safe: saturate/lerp only, constant divisor (no new intrinsics).
		float kneeK = saturate((lum - 0.05) / 0.20) * saturate(curveParams.w);
		exposure *= lerp(1.0, 0.5, kneeK);
		// Grading intensity follows the measured frame, keeping day/night consistent.
		sceneLuma = saturate(lum * 2.0);
	}

	c *= exposure;

	// Pre-compress extreme highlights so filmic tonemap can spread them
	c = SoftKnee(c);

	// Division-safety guard for NEGATIVE exposure (min/max exposure ranges
	// now allow -0.5 for deliberate darkening): c*exposure < 0 would hit the
	// Hable denominator's real roots (x ≈ -0.21 / -1.15 for D in [0.18,0.55])
	// => Inf/NaN pixels. Clamping negatives to 0 maps them cleanly to black
	// (deliberate darkening). max(x,0) is bit-exact for all x >= 0, so the
	// normal (non-negative) path is provably unchanged.
	c = FilmicTonemap(max(c, 0.0), tonemapParams.y);

	// Exact sRGB gamma encode
	c = saturate(LinearToSRGB(c));

	// Post-gamma grading: adaptive curves + brightness/contrast
	// Intensity scales with scene luminance — dark scenes get gentler grading
	c = PostGrade(c, tonemapParams.z);

	// Filmic color grading: warm shadows, warm highlights, mid-tone saturation,
	// highlight desaturation — matches Xbox SA press kit rendering intent
	c = FilmicGrading(c, tonemapParams.z);

	// Black-level lift: preserve deep shadow detail without a gray veil.
	// Small constant (0.015-0.02) only affects near-black; midtones untouched.
	c = c * (1.0f - blackLiftParams.x) + blackLiftParams.x;

	// User pivot curve — final OUTPUT stage (after black lift). Identity
	// (bit-exact passthrough) when both intensities are 0 (the default).
	c = PivotCurve(c);

	return float4(c, 1.0f);
}
