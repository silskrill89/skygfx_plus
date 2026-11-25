// LuminanceReduce.hlsl - GPU luminance measure (ps_3_0)
// Renders the full-res scene into a tiny luminance RT (8x8).
// Each output pixel averages a 1/8 x 1/8 tile of the screen using 4x4 = 16 taps.
// No CPU readback: the small RT is consumed by LuminanceAdapt.hlsl (eye adaptation)
// and then by TonemapPass.hlsl for frame-adaptive exposure.
//
// s0 = scene (pRasterFrontBuffer) - ONLY its content sub-rect. The CPU quad
//      (postfx_luminance.cpp RunFrameExposure lumaQuad) spans UV
//      (screenW/fbW, screenH/fbH) of the padded front buffer across the full
//      8x8 viewport, so texCoord is the source-UV centre of this destination
//      pixel's 1/8-screen tile. Do NOT swap that quad for a 0..1 quad - the
//      tile size below is derived from its UV gradient.
// Registers: s0 only (no cN) - ps_3_0 budget unchanged.
// Output: .r = tile luminance (linear)

uniform sampler2D tex : register(s0);

struct PS_INPUT
{
	float2 texCoord : TEXCOORD0;
};

#include "../include/BloomCommon.hlsl"

float4 main(PS_INPUT IN, float4 vpos : VPOS) : COLOR
{
	// UV span of ONE destination pixel = one 1/8-screen tile of the content
	// region in front-buffer UV units: (screenW/fbW / 8, screenH/fbH / 8).
	// Taken from the quad's own gradient (ddx = x-slope, ddy = y-slope) so it
	// tracks any screen/front-buffer padding with no extra constant register.
	// Under-sample fix (bottom-strip class): the old code used a fixed
	// FB-space 1/8 grid (floor(texCoord*8) * 0.125), which only equals the
	// screen tile grid when the front buffer is unpadded; on a padded buffer
	// (1920x1080 content in 2048x2048) the grid lagged the destination rows,
	// leaving the bottom content strip outside every tap's footprint.
	float2 du = ddx(IN.texCoord);
	float2 dv = ddy(IN.texCoord);
	float2 tileUV = float2(du.x, dv.y);

	// Destination pixel -> tile index. VPOS is the rasteriser pixel centre
	// (0.5..7.5 inside the 8x8 viewport), so floor() is exact and independent
	// of any Im2D half-pixel convention; the clamp pins every fetch inside the
	// written content region (the never-written pad border is never read).
	float2 tileIdx = clamp(floor(vpos.xy), 0.0, 7.0);
	float2 tileCentre = (tileIdx + 0.5) * tileUV;

	// 4x4 taps at the sub-cell centres of the tile: offsets +-{1.5,0.5} of a
	// tile/4 sub-cell, so the 16 taps REPRESENT the whole tile. The union over
	// the 8x8 grid therefore covers the entire source content rect (100% of
	// pixels, no pad reads) while staying a 16-tap reduction - the perf
	// intent (few taps, full coverage) is unchanged.
	float acc = 0.0;
	[unroll]
	for (int j = 0; j < 4; j++)
	{
		[unroll]
		for (int i = 0; i < 4; i++)
		{
			float2 off = ((float2(i, j) - 1.5) * 0.25) * tileUV;
			acc += Luminance(tex2D(tex, tileCentre + off).rgb);
		}
	}

	float l = acc * (1.0 / 16.0);
	return float4(l, l, l, 1.0);
}
