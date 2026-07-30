// TonemapPass.hlsl - Reinhard tonemapping + sRGB gamma encode (ps_3_0)
// Runs as a fullscreen pass AFTER color grading.
//
// Reads graded linear HDR from pRasterFrontBuffer, outputs gamma-corrected LDR.
//
// c5 = tonemapParams: x=exposure, y=whitePoint (unused for basic Reinhard), z=0, w=0

uniform sampler2D tex : register(s0);
uniform float4 tonemapParams : register(c5);

struct PS_INPUT
{
	float3 texcoord0 : TEXCOORD0;
};

float3 ReinhardTonemap(float3 c, float exposure)
{
	c *= exposure;
	return c / (1.0 + c);
}

float4 main(PS_INPUT IN) : COLOR
{
	float3 c = tex2D(tex, IN.texcoord0.xy);

	// Reinhard tonemap
	float exposure = tonemapParams.x;
	c = ReinhardTonemap(c, exposure);

	// Linear -> sRGB gamma encode
	c = pow(saturate(c), 1.0 / 2.2);

	return float4(c, 1.0f);
}
