// TonemapPass.hlsl - Hable/Uncharted 2 filmic tonemapping + exact sRGB gamma (ps_3_0)
// Runs as a fullscreen pass AFTER color grading.
//
// Uses CryEngine's FilmMapping() parameters (HDRPostProcess.cfx lines 282-311).
// These are tuned to output in [0,1] range directly — NO white scaling needed.
// The old Reinhard desaturates highlights and crushes contrast; this preserves both.
//
// Reads graded linear HDR from pRasterFrontBuffer, outputs gamma-corrected LDR.
//
// c5 = tonemapParams: x=exposure, y=unused, z=0, w=0

uniform sampler2D tex : register(s0);
uniform float4 tonemapParams : register(c5);

struct PS_INPUT
{
	float3 texcoord0 : TEXCOORD0;
};

// Hable/Uncharted 2 filmic tonemapping — CryEngine parameters
// Reference: CryEngine HDRPostProcess.cfx FilmMapping() with default HDRFilmCurve=(1,1,1,1)
// ShoStren=0.22, LinStren=0.30, LinAngle=0.10, ToeStren=0.20, ToeNum=0.01, ToeDenom=0.30
//
// Output range: [0, ~0.8] for inputs [0, 5] — no white scaling needed.
// After sRGB gamma this maps to a natural, contrasty LDR image.
float3 FilmicTonemap(float3 x)
{
	const float A = 0.22;   // Shoulder Strength
	const float B = 0.30;   // Linear Strength
	const float CB = 0.03;  // Linear Angle × Linear Strength (0.10 × 0.30)
	const float D = 0.20;   // Toe Strength
	const float E = 0.01;   // Toe Numerator
	const float F = 0.30;   // Toe Denominator
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

float4 main(PS_INPUT IN) : COLOR
{
	float3 c = tex2D(tex, IN.texcoord0.xy);

	// Apply exposure (default 1.0 from C++)
	float exposure = tonemapParams.x;
	c *= exposure;

	// Hable/Uncharted 2 filmic tonemap (CryEngine params, no white scaling)
	c = FilmicTonemap(c);

	// Exact sRGB gamma encode
	c = saturate(LinearToSRGB(c));

	return float4(c, 1.0f);
}
