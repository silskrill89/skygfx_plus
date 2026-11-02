// TonemapCommon.hlsl - shared tonemap helpers (ps_3_0)
// Verbatim extraction shared by shaders/ps/TonemapPass.hlsl and
// shaders/ps/TonemapLUT.hlsl (stage 0-3 shader dedupe, lane L13, 2026-10-27).
// Function bodies below are byte-identical to the TonemapPass originals
// (the TonemapLUT copies matched at token level; comment drift resolved to
// the TonemapPass text). Do not edit without re-checking both consumers.
//
// No register declarations here. The includer must declare
// gradeParams : register(c6) and curveParams : register(c8) BEFORE this
// include (PostGrade reads c6; PivotCurve1/PivotCurve read c8).

// Exact sRGB OETF (IEC 61966-2-1)
// More accurate than simple pow(1/2.2) — handles the linear toe correctly
float3 LinearToSRGB(float3 c)
{
	return (c < 0.0031308)
		? 12.92 * c
		: 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

// Post-gamma color grading: Curves + Brightness/Contrast
// Matched from Photoshop adjustments on sunset screenshot:
//   Curves: S-curve with shadow lift (0→15/255), midtone boost, highlight compression
//   Brightness/Contrast: B=52, C=72 at 61% opacity → effective B≈0.124, C≈1.44
//
// ADAPTIVE: grading intensity scales with sceneLuma (from c5.z).
//   Dark scenes (dawn/dusk, sceneLuma < 0.15) → minimal grading to avoid crushing shadows
//   Bright scenes (midday, sceneLuma > 0.35) → full Photoshop-matched grading
float3 PostGrade(float3 c, float sceneLuma)
{
	float brightness = gradeParams.x;
	float contrast   = gradeParams.y;
	float lift       = gradeParams.z;
	float curveBlend = gradeParams.w;

	// Adaptive scale: ramps 0→1 as sceneLuma goes 0.04→0.30
	// Below 0.04 → no grading (identity). Above 0.30 → full grading.
	// Dawn (~0.10) gets ~25% grading — enough for color pop without crushing shadows
	float adaptive = saturate((sceneLuma - 0.04) / 0.26);

	// Lerp each param toward identity (no-op) for dark scenes
	float b = brightness * adaptive;           // identity: 0.0
	float ct = lerp(1.0, contrast, adaptive);  // identity: 1.0
	float lf = lift * adaptive;                // identity: 0.0
	float cb = curveBlend * adaptive;          // identity: 0.0

	// Shadow lift: raise blacks from 0 to ~lift value
	c = c + lf * (1.0 - c);

	// S-curve: smoothstep-based contrast enhancement
	float3 s = c * c * (3.0 - 2.0 * c);
	c = lerp(c, s, cb);

	// Brightness/Contrast (Photoshop non-legacy mode)
	c = (c - 0.5) * ct + 0.5;
	c += b;

	return saturate(c);
}

// Filmic color grading based on Xbox press kit analysis
// Matches the original SA rendering intent: warm shadows, warm highlights,
// mid-tone saturation peak, highlight desaturation
float3 FilmicGrading(float3 c, float sceneLuma)
{
	float luma = dot(c, float3(0.299, 0.587, 0.114));

	// 1. Warm shadow tinting (Xbox: +3.9% warmth in shadows)
	// Shadows are warm brown/orange, NOT blue
	float3 warmShadow = float3(1.15, 0.95, 0.85);
	float shadowFactor = 1.0 - smoothstep(0.0, 0.3, luma);
	c *= lerp(float3(1.0, 1.0, 1.0), warmShadow, shadowFactor * 0.3);

	// 2. Warm highlight tinting (Xbox: +11.8% warmth in highlights)
	// Highlights are warm golden, NOT cool blue
	float3 warmHighlight = float3(1.1, 1.02, 0.9);
	float highlightFactor = smoothstep(0.5, 1.0, luma);
	c *= lerp(float3(1.0, 1.0, 1.0), warmHighlight, highlightFactor * 0.2);

	// 3. Mid-tone saturation boost (Xbox: peaks at 0.484 in mids)
	// Saturation follows classic film curve: low in shadows, peak in mids, low in highlights
	float midToneFactor = smoothstep(0.1, 0.4, luma) * (1.0 - smoothstep(0.6, 0.9, luma));
	float3 desaturated = float3(luma, luma, luma);
	c = lerp(c, lerp(desaturated, c, 1.3), midToneFactor * 0.15);

	// 4. Highlight desaturation (Xbox: 0.244 in highlights vs 0.484 in mids)
	// Bright areas desaturate, simulating overexposure/haze
	float highlightDesat = smoothstep(0.7, 1.0, luma);
	c = lerp(c, float3(luma, luma, luma), highlightDesat * 0.2);

	return saturate(c);
}

// === User pivot curve (c8) — applied at the output, Photoshop-style ===
// Region below the mid pivot is driven by Curve Intensity Lows (negative
// darkens shadows, positive lifts them); region above mid by Curve Intensity
// Highs (negative pulls highlights down, positive boosts). The pivot itself
// anchors at gain 1.0 from BOTH sides (continuity: x/m == 1 and
// (x-m)/(x-m)==0 evaluate exactly at the pivot).
// Implemented as a per-channel gain ramp:  out = x * gain(x).
// Both intensities 0 => early-out in PivotCurve BEFORE any math on c, so
// the disabled path is bit-exact passthrough (existing tuning unaffected).
// Guarded at the mid extremes (m~0 / m~1) so the region divisions never
// divide by zero (AGENTS division-safety rule).
float PivotCurve1(float x)
{
	float lows  = curveParams.x;
	float highs = curveParams.y;
	float m     = saturate(curveParams.z);

	// Guarded pivots: mid <= 0 => empty low region (all high region);
	// mid >= 1 => empty high region (all low region). Division-safe forms:
	// m~0:  gain ramps 1 -> (1+highs) across [0,1]
	// m~1:  gain ramps (1+lows) -> 1 across [0,1]
	if (m <= 1e-5)
		return x * (1.0 + highs * x);
	if (m >= 1.0 - 1e-5)
		return x * (1.0 + lows * (1.0 - x));

	if (x <= m)
		return x * (1.0 + lows * (1.0 - x / m));       // gain: (1+lows)@0 -> 1@m
	return x * (1.0 + highs * (x - m) / (1.0 - m));    // gain: 1@m -> (1+highs)@1
}

float3 PivotCurve(float3 c)
{
	// CRITICAL bit-exact identity: both intensities exactly 0.0 (the CPU
	// clamps/defaults write exact 0.0f) => return c untouched, zero
	// arithmetic — output is bit-identical to the pre-curve shader.
	if (curveParams.x == 0.0 && curveParams.y == 0.0)
		return c;
	return float3(PivotCurve1(c.r), PivotCurve1(c.g), PivotCurve1(c.b));
}

