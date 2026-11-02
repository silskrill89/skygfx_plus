// Bloom_Composite.hlsl - Jimenez/COD bloom final composite (ps_3_0)
// SkyGFX Plus - PostFX bloom lane, pass 4 of 4
//
// Blends the fully upsampled bloom pyramid with the original scene. Bloom gets
// a saturation adjustment and intensity scale, then is combined additively
// (HDR-friendly) or via screen blend (1-(1-a)(1-b), softer, avoids hard clipping).
//
// Register contract (parallel postfx lane binds):
//   s0 = scene color (LINEAR/CLAMP sampler)
//   s1 = bloom result (fully upsampled chain output)
//   c0 = (bloomIntensity, saturation, blendMode, unused)
//        bloomIntensity : overall bloom strength (0.0 - 2.0)
//        saturation     : bloom color saturation (0.0 - 2.0, 1 = unchanged)
//        blendMode      : 0 = additive (scene + bloom), 1 = screen
//        unused
//
// Tap count: 2
//
// SM3.0: fixed tap count, no dynamic loops, 1e-7 guards, safe at half/quarter res.

sampler2D sceneTex : register(s0);
sampler2D bloomTex : register(s1);
uniform float4 bloomCompositeParams : register(c0); // (bloomIntensity, saturation, blendMode, unused)

#include "../include/BloomCommon.hlsl"

// Adjust bloom saturation (1 = unchanged, 0 = grayscale, >1 = boosted)
float3 AdjustSaturation(float3 color, float saturation)
{
    float luma = Luminance(color);
    return lerp(float3(luma, luma, luma), color, saturation);
}

// Screen blend: 1 - (1-a)(1-b). Lighter than additive, no hard over-brightening.
float3 ScreenBlend(float3 base, float3 blend)
{
    return 1.0 - (1.0 - base) * (1.0 - blend);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float bloomIntensity = bloomCompositeParams.x;
    float saturation     = bloomCompositeParams.y;
    float blendMode      = bloomCompositeParams.z;

    float3 scene = tex2Dlod(sceneTex, float4(uv, 0.0, 0.0)).rgb;
    float3 bloom = tex2Dlod(bloomTex, float4(uv, 0.0, 0.0)).rgb;

    bloom = AdjustSaturation(bloom, saturation);
    bloom *= bloomIntensity;

    float3 result;
    if (blendMode < 0.5)
    {
        // Additive (HDR-friendly)
        result = scene + bloom;
    }
    else
    {
        // Screen blend (softer)
        result = ScreenBlend(scene, bloom);
    }

    return float4(result, 1.0);
}
