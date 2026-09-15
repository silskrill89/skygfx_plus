// CommonMath.hlsl - Consolidated math helpers
//
// Audit of shaders/ found ~15 helper types duplicated 50+ times inline.
// This header is the single home for those helpers.
//
// SM3.0 constraint: all helpers are inline, no dynamic loops, no discard,
// no SM4+ intrinsics, no texture sampling, no dynamic branching.
//
// NOTE: This header is NOT yet included by any file. Migration of call sites
// is a separate task. This file is purely additive.

#ifndef COMMONMATH_INCLUDED
#define COMMONMATH_INCLUDED

// --- Safe Divide ---
// was duplicated across SMAA_Temporal.hlsl, ColorFilter_CrossMix.hlsl,
// MotionBlur_Burnout.hlsl, HairEnhance.hlsl, SkinEnhance.hlsl,
// SMAA_EdgeCombined.hlsl, SMAA_EdgeMotionDepth.hlsl, LuminanceReduce.hlsl
inline float SafeDivide(float num, float den, float eps = 1e-7)
{
    return num / max(den, eps);
}

// --- Safe Normalize (float3) ---
// Guard magnitude, not components. Adding epsilon inside the
// argument to the normalize intrinsic is WRONG — see SafeNormalize above.
// was duplicated across SMAA_Temporal.hlsl, PerlinNoise.hlsl,
// ColorFilter_CrossMix.hlsl, MotionBlur_Burnout.hlsl, HairEnhance.hlsl,
// SkinEnhance.hlsl, SMAA_EdgeCombined.hlsl, SMAA_EdgeMotionDepth.hlsl
inline float3 SafeNormalize(float3 v, float3 fallback = float3(0, 1, 0))
{
    float len = length(v);
    return len > 1e-6 ? v / len : fallback;
}

// --- Safe Normalize (float2) ---
// Guard magnitude, not components.
inline float2 SafeNormalize(float2 v, float2 fallback = float2(1, 0))
{
    float len = length(v);
    return len > 1e-6 ? v / len : fallback;
}

// --- Rec.709 Luma ---
// was duplicated in SMAA_Temporal.hlsl:34, ColorFilter_CrossMix.hlsl,
// MotionBlur_Burnout.hlsl, HairEnhance.hlsl, SkinEnhance.hlsl,
// SMAA_EdgeCombined.hlsl, SMAA_EdgeMotionDepth.hlsl, LuminanceReduce.hlsl:17
inline float LumaRec709(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// --- SMPTE-C Luma ---
// was duplicated inline 6 times across various shader files
inline float LumaSMPTE(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

// --- Unpack Normal (RGB -> XYZ) ---
// was duplicated ~25 times across the shader tree
inline float3 UnpackNormal(float3 encoded)
{
    return encoded * 2.0 - 1.0;
}

// --- Unpack RG (RG -> XY) ---
inline float2 UnpackRG(float2 encoded)
{
    return encoded * 2.0 - 1.0;
}

// --- UV to NDC ---
inline float2 UVToNDC(float2 uv)
{
    return uv * 2.0 - 1.0;
}

// --- 2D Hash (value noise) ---
inline float Hash2D(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// --- 2D Hash with seed ---
inline float Hash2DSeed(float2 p, float seed)
{
    return frac(sin(dot(p, float2(12.9898, 78.233)) + seed) * 43758.5453);
}

// --- Linear Depth (projInfo variant) ---
// projInfo = (rxw, ryw, -nf/(f-n), f/(f-n))
// used by SSAO / height-fog family
inline float LinearDepthProjInfo(float depth, float4 projInfo)
{
    return projInfo.z / SafeDivide(depth - projInfo.w);
}

// --- Linear Depth (near/far variant) ---
// used by NormalBuffer.hlsl and MotionBlur_Burnout.hlsl
inline float LinearDepthNearFar(float depth, float near, float far)
{
    return near * far / SafeDivide(far - depth * (far - near));
}

// --- Linear to sRGB ---
inline float3 LinearToSRGB(float3 c)
{
    return pow(saturate(c), 1.0 / 2.2);
}

#endif
