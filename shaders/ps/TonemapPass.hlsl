// TonemapPass.hlsl - Adaptive Hable/Uncharted 2 filmic tonemapping + exact sRGB gamma (ps_3_0)
// Runs as a fullscreen pass AFTER color grading.
//
// Uses CryEngine's FilmMapping() base parameters (HDRPostProcess.cfx lines 282-311).
// Toe strength is now timecycle-driven: night/dawn lifts shadows more aggressively,
// midday keeps tighter contrast. Exposure adapts to scene luminance.
//
// Reads graded linear HDR from pRasterFrontBuffer, outputs gamma-corrected LDR.
//
// c5 = tonemapParams (timecycle-driven):
//   x = exposure (brightness slider × sceneLuma × carcols × sun dampening)
//   y = toeStrength (0.05-0.25, driven by sceneLuma + timecycle shadowStrength)
//   z = sceneLuma (ambient + directional luminance from timecycle, 0-1)
//   w = flags (bit0=isInterior, bit1=isCutscene)
//
// c6 = gradeParams (fully timecycle-driven from CColourSet):
//   x = brightness (0.03-0.07, from timecycle lightsOnGroundBrightness)
//   y = contrast (1.15-1.40, from timecycle shadowStrength × fogStart)
//   z = lift (0.00-0.025, from timecycle cloudAlpha + fogStart)
//   w = curves blend (0.25-0.50, from timecycle sceneLuma)

uniform sampler2D tex : register(s0);
uniform float4 tonemapParams : register(c5);
uniform float4 gradeParams : register(c6);

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

float4 main(PS_INPUT IN) : COLOR
{
	float3 c = tex2D(tex, IN.texcoord0.xy);

	// Adaptive exposure from timecycle scene luminance
	c *= tonemapParams.x;

	// Pre-compress extreme highlights so filmic tonemap can spread them
	c = SoftKnee(c);

	// Adaptive Hable/Uncharted 2 filmic tonemap (time-of-day toe strength)
	c = FilmicTonemap(c, tonemapParams.y);

	// Exact sRGB gamma encode
	c = saturate(LinearToSRGB(c));

	// Post-gamma grading: adaptive curves + brightness/contrast
	// Intensity scales with scene luminance — dark scenes get gentler grading
	c = PostGrade(c, tonemapParams.z);

	// Filmic color grading: warm shadows, warm highlights, mid-tone saturation,
	// highlight desaturation — matches Xbox SA press kit rendering intent
	c = FilmicGrading(c, tonemapParams.z);

	return float4(c, 1.0f);
}
