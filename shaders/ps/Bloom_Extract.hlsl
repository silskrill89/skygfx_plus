// Bloom_Extract.hlsl - Jimenez/COD HDR bloom bright-pass extract (ps_3_0)
// SkyGFX Plus - PostFX bloom lane, pass 1 of 4 (extract -> downsample* -> upsample* -> composite)
//
// Soft-knee threshold + Karis average anti-firefly. First step of the bloom
// pyramid (writes mip0, typically half-res). Single-mip shader: the C++ lane
// runs repeated passes for up to 6 mips; per-pass state comes from c0.
//
// Reference: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// (Jorge Jimenez) / MiniEngine bright pass.
//
// Register contract (parallel postfx lane binds):
//   s0 = scene HDR color (LINEAR/CLAMP sampler)
//   c0 = (threshold, softThreshold, karisOn, invSizePx)
//        threshold     : luma bright-pass threshold (0.8 - 1.5 typical)
//        softThreshold : soft knee width as fraction of threshold (0.0 - 0.5)
//        karisOn       : 1 = Karis weighted average (anti-firefly), 0 = plain box average
//        invSizePx     : 1/srcWidth texels (square texels assumed; offsets scale by this)
//
// Tap count: 4 (2x2 box at +/-0.5 texel)
//
// SM3.0: fixed tap count, no loops, 1e-7 division guards, safe at half/quarter res.

sampler2D sceneTex : register(s0);
uniform float4 bloomExtractParams : register(c0); // (threshold, softThreshold, karisOn, invSizePx)

#include "../include/BloomCommon.hlsl"

// Soft thresholding with knee curve: gradual contribution near threshold
float3 SoftThreshold(float3 color, float threshold, float softKnee)
{
    float luma = Luminance(color);

    float knee = threshold * softKnee;
    float soft = luma - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-7);

    float contribution = max(soft, luma - threshold) / max(luma, 1e-7);

    return color * contribution;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float threshold  = bloomExtractParams.x;
    float softKnee   = bloomExtractParams.y;
    float karisOn    = bloomExtractParams.z;
    float invSizePx  = max(bloomExtractParams.w, 1e-7);

    // 2x2 pattern at +/-0.5 texel: initial box downsample of the pyramid
    float2 halfTexel = float2(invSizePx, invSizePx) * 0.5;

    float3 c1 = tex2Dlod(sceneTex, float4(uv + float2(-halfTexel.x, -halfTexel.y), 0.0, 0.0)).rgb;
    float3 c2 = tex2Dlod(sceneTex, float4(uv + float2( halfTexel.x, -halfTexel.y), 0.0, 0.0)).rgb;
    float3 c3 = tex2Dlod(sceneTex, float4(uv + float2(-halfTexel.x,  halfTexel.y), 0.0, 0.0)).rgb;
    float3 c4 = tex2Dlod(sceneTex, float4(uv + float2( halfTexel.x,  halfTexel.y), 0.0, 0.0)).rgb;

    c1 = SoftThreshold(c1, threshold, softKnee);
    c2 = SoftThreshold(c2, threshold, softKnee);
    c3 = SoftThreshold(c3, threshold, softKnee);
    c4 = SoftThreshold(c4, threshold, softKnee);

    float3 karis  = KarisAverage(c1, c2, c3, c4);
    float3 plain  = (c1 + c2 + c3 + c4) * 0.25;
    float3 bloom  = lerp(plain, karis, saturate(karisOn));

    return float4(bloom, 1.0);
}
