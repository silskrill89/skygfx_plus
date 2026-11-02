// Bloom_Downsample.hlsl - Jimenez/COD 13-tap energy-preserving bloom downsample (ps_3_0)
// SkyGFX Plus - PostFX bloom lane, pass 2 of 4 (repeated once per mip, up to 6 mips)
//
// 13-tap filter over a 4x4 source region (bilinear-friendly tap placement).
// mipLevel 0 uses 5 Karis-averaged quad groups (anti-firefly); deeper mips use
// the plain 4-2-1 weighted kernel (energy preserved: total weight 20).
// Single-mip shader: C++ binds the source mip and sets mipLevel/invSizePx per pass.
//
// Reference: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// (Jorge Jimenez).
//
// Register contract (parallel postfx lane binds):
//   s0 = source mip (LINEAR/CLAMP sampler)
//   c0 = (mipLevel, filterRadius, invSizePx, unused)
//        mipLevel    : 0 = first downsample (Karis path), >0 = weighted path
//        filterRadius: tap spacing scale in texels (1.0 = stock kernel, 0.5 - 2.0 usable)
//        invSizePx   : 1/srcWidth of the SOURCE mip (square texels assumed)
//        unused
//
// Tap layout (x = tap, spacing = 2 texels outer / 1 texel inner diagonal):
//   x . x . x
//   . x . x .
//   x . x . x
//   . x . x .
//   x . x . x
//
// Tap count: 13
//
// SM3.0: fixed tap count (unrolled), no dynamic loops, 1e-7 guards,
// safe at half/quarter res sizes.

sampler2D srcTex : register(s0);
uniform float4 bloomDownsampleParams : register(c0); // (mipLevel, filterRadius, invSizePx, unused)

#include "../include/BloomCommon.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float mipLevel     = bloomDownsampleParams.x;
    float filterRadius = max(bloomDownsampleParams.y, 1e-7);
    float invSizePx    = max(bloomDownsampleParams.z, 1e-7);

    float2 texel = float2(invSizePx, invSizePx) * filterRadius;

    // Center
    float3 center = tex2Dlod(srcTex, float4(uv, 0.0, 0.0)).rgb;

    // 4 outer corners (2 texels out)
    float3 tl = tex2Dlod(srcTex, float4(uv + float2(-2.0, -2.0) * texel, 0.0, 0.0)).rgb;
    float3 tr = tex2Dlod(srcTex, float4(uv + float2( 2.0, -2.0) * texel, 0.0, 0.0)).rgb;
    float3 bl = tex2Dlod(srcTex, float4(uv + float2(-2.0,  2.0) * texel, 0.0, 0.0)).rgb;
    float3 br = tex2Dlod(srcTex, float4(uv + float2( 2.0,  2.0) * texel, 0.0, 0.0)).rgb;

    // 4 outer sides (2 texels out, axis-centered)
    float3 top = tex2Dlod(srcTex, float4(uv + float2( 0.0, -2.0) * texel, 0.0, 0.0)).rgb;
    float3 bot = tex2Dlod(srcTex, float4(uv + float2( 0.0,  2.0) * texel, 0.0, 0.0)).rgb;
    float3 lft = tex2Dlod(srcTex, float4(uv + float2(-2.0,  0.0) * texel, 0.0, 0.0)).rgb;
    float3 rgt = tex2Dlod(srcTex, float4(uv + float2( 2.0,  0.0) * texel, 0.0, 0.0)).rgb;

    // 4 inner diagonals (1 texel out)
    float3 itl = tex2Dlod(srcTex, float4(uv + float2(-1.0, -1.0) * texel, 0.0, 0.0)).rgb;
    float3 itr = tex2Dlod(srcTex, float4(uv + float2( 1.0, -1.0) * texel, 0.0, 0.0)).rgb;
    float3 ibl = tex2Dlod(srcTex, float4(uv + float2(-1.0,  1.0) * texel, 0.0, 0.0)).rgb;
    float3 ibr = tex2Dlod(srcTex, float4(uv + float2( 1.0,  1.0) * texel, 0.0, 0.0)).rgb;

    float3 result;

    if (mipLevel < 0.5)
    {
        // First downsample: 5 Karis-averaged 2x2 groups (anti-firefly).
        // group1 = inner quad (weight 0.5), groups 2-5 = four overlapping
        // corner quads of the 4x4 region (weight 0.125 each). Total = 1.0.
        float3 group1 = KarisAverage(itl, itr, ibl, ibr);
        float3 group2 = KarisAverage(tl, top, lft, center);
        float3 group3 = KarisAverage(top, tr, center, rgt);
        float3 group4 = KarisAverage(lft, center, bl, bot);
        float3 group5 = KarisAverage(center, rgt, bot, br);

        result = group1 * 0.5 + (group2 + group3 + group4 + group5) * 0.125;
    }
    else
    {
        // Deeper mips: 4-2-1 weighted 13-tap kernel.
        // Total weight: 4 + 4*2 + 8*1 = 20 (energy preserving).
        result  = center * 4.0;
        result += (itl + itr + ibl + ibr) * 2.0;
        result += tl + tr + bl + br + top + bot + lft + rgt;
        result /= 20.0;
    }

    return float4(result, 1.0);
}
