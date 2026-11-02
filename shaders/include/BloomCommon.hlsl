// BloomCommon.hlsl - shared bloom + luminance helpers (ps_3_0)
// Verbatim extraction shared by shaders/ps/Bloom_Extract.hlsl,
// shaders/ps/Bloom_Downsample.hlsl, shaders/ps/Bloom_Composite.hlsl and
// shaders/ps/LuminanceReduce.hlsl (stage 0-3 shader dedupe, lane L13,
// 2026-10-27). Function bodies below are byte-identical to the
// Bloom_Extract originals. Do not edit without re-checking all consumers.
//
// No register declarations here (consumers own their sN/cN contracts).

// Rec. 709 luminance (sRGB primaries)
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Karis average: weight each tap inversely by luminance to kill fireflies
float3 KarisAverage(float3 c1, float3 c2, float3 c3, float3 c4)
{
    float w1 = 1.0 / (1.0 + Luminance(c1));
    float w2 = 1.0 / (1.0 + Luminance(c2));
    float w3 = 1.0 / (1.0 + Luminance(c3));
    float w4 = 1.0 / (1.0 + Luminance(c4));

    float wSum = max(w1 + w2 + w3 + w4, 1e-7);

    return (c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4) / wSum;
}

