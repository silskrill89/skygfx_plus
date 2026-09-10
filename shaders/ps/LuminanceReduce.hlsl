// LuminanceReduce.hlsl - GPU luminance measure (ps_3_0)
// Renders the full-res scene into a tiny luminance RT (8x8).
// Each output pixel averages a 1/8 x 1/8 tile of the screen using 4x4 = 16 taps.
// No CPU readback: the small RT is consumed by LuminanceAdapt.hlsl (eye adaptation)
// and then by TonemapPass.hlsl for frame-adaptive exposure.
//
// s0 = scene (pRasterFrontBuffer)
// Output: .r = tile luminance (linear)

uniform sampler2D tex : register(s0);

struct PS_INPUT
{
	float2 texCoord : TEXCOORD0;
};

float Lum(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(PS_INPUT IN) : COLOR
{
	// Luminance RT is 8x8 -> each output texel owns a 1/8 screen tile.
	const float TILE = 0.125;
	const float STEP = TILE / 4.0;   // 4 samples across the tile

	// Tile centre for this output pixel (quantise incoming uv to the 1/8 grid).
	float2 tileCentre = (floor(IN.texCoord * 8.0) + 0.5) * TILE;

	float acc = 0.0;
	[unroll]
	for (int j = 0; j < 4; j++)
	{
		[unroll]
		for (int i = 0; i < 4; i++)
		{
			float2 off = float2((i - 1.5) * STEP, (j - 1.5) * STEP);
			acc += Lum(tex2D(tex, tileCentre + off).rgb);
		}
	}

	float l = acc * (1.0 / 16.0);
	return float4(l, l, l, 1.0);
}
