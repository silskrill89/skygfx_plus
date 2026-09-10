// LuminanceAdapt.hlsl - temporal eye adaptation (ps_3_0)
// Reduces the 8x8 measured luminance to a 1x1 adapted value and smooths it
// across frames (CryEngine-style eye adaptation: prev + speed*(measured-prev)).
//
// s0 = measured luminance (8x8, .r)
// s1 = previous adapted luminance (1x1, .r)
// c0 = { adaptSpeed (0-1), 0, 0, 0 }
// Output: 1x1, .r = adapted luminance (linear)

uniform sampler2D measTex : register(s0);
uniform sampler2D prevTex : register(s1);
uniform float4 adaptParams : register(c0);

struct PS_INPUT
{
	float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
	// Average the 8x8 measured texture with a 4x4 tap grid.
	float acc = 0.0;
	[unroll]
	for (int j = 0; j < 4; j++)
	{
		[unroll]
		for (int i = 0; i < 4; i++)
		{
			float2 uv = float2((i + 0.5) / 4.0, (j + 0.5) / 4.0);
			acc += tex2D(measTex, uv).r;
		}
	}

	float measured = max(acc * (1.0 / 16.0), 1e-4);
	float prev = tex2D(prevTex, float2(0.5, 0.5)).r;

	// First frame (or reset): adopt the measurement immediately.
	if (prev <= 0.0)
		prev = measured;

	float s = saturate(adaptParams.x);
	float adapted = prev + s * (measured - prev);
	return float4(adapted, adapted, adapted, 1.0);
}
