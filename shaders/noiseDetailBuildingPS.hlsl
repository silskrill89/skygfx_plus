// noiseDetailBuildingPS.hlsl - Noise-detail building pixel shaders
// Entry points: main_simpleNoiseDetail, main_simpleDetailNoiseDetail, main_xboxBuildingNoiseDetail
// Compiled as ps_3_0 by tools/fast_build.py (multi_entry list).
//
// NOISE DETAIL SYSTEM
//   Blue-noise UV jitter for tiled building textures. Noise.hlsl packs the
//   blue-noise sequence in s9.a; the per-pixel offset is derived from surface
//   UV through NoiseBlue(), scaled by the jitter amplitude in c52.x.
//
//   Blue noise pushes the sampling error to high frequencies, so mips/TAA
//   average it away instead of leaving visible structure. The sequence is
//   derived from surface UV, so it is stable per pixel across frames (no
//   temporal shimmer) and needs no derivatives.
//   FALLBACK: if s9 is not bound the fetch returns the D3D9 unbound-sampler
//   value, and if the lane that uploads c52 is inactive c52.x reads 0.
//   (NoiseBlue(...) - 0.5) * 0.0 == 0 in either case => zero jitter =>
//   deterministic single tap, i.e. byte-identical to the non-noise-detail
//   shader. Safe to ship before the sampler is bound.
//   COST: 2 extra fetches per jittered tap (or per entry when shared).
//
//   Removed: the old sin/fmod white-noise hash jitter path (constant positive
//   UV push, driver-dependent), superseded by the blue-noise jitter above.
//
// Sampler/constant registers below are unchanged from the original file.

#include "include/Noise.hlsl"

// Jitter frequency in UV space. Matches the old hash(UV*100) density, but the
// second axis is swizzled and offset by 0.37 so X and Y are decorrelated.
static const float kNoiseDetailJitterScale = 100.0;

// Per-pixel blue-noise UV offset. Zero-mean, amplitude = c52.x.
float2 NoiseDetailJitter(float2 uv)
{
    float2 n = float2(NoiseBlue(uv * kNoiseDetailJitterScale),
                      NoiseBlue(uv.yx * kNoiseDetailJitterScale + 0.37));
    return (n - 0.5) * uniNoiseParams3.x;   // c52.x = jitterAmount
}

// Noise-detail tap: single fetch at a blue-noise offset UV.
float4 tex2DNoiseDetail(sampler2D tex, float2 uv)
{
    return tex2D(tex, uv + NoiseDetailJitter(uv));
}

// Same, but reuses a jitter already computed for this pixel (keeps multi-tap
// entries down to one jitter -> the detail map cannot slide against the base).
float4 tex2DNoiseDetailJ(sampler2D tex, float2 uv, float2 jitter)
{
    return tex2D(tex, uv + jitter);
}

// ============================================================
// Pass 0: Noise-detail simple building
// Entry: main_simpleNoiseDetail
// ============================================================
uniform sampler2D tex_simple : register(s0);
uniform float4 colorscale_simple : register(c0);

struct PS_INPUT_SIMPLE {
    float3 texcoord0 : TEXCOORD0;
    float4 color     : COLOR0;
};

float4 main_simpleNoiseDetail(PS_INPUT_SIMPLE IN) : COLOR
{
    return tex2DNoiseDetail(tex_simple, IN.texcoord0.xy * 1.2) * IN.color * colorscale_simple.x;
}

// ============================================================
// Pass 1: Noise-detail building with detail map
// Entry: main_simpleDetailNoiseDetail
// ============================================================
uniform sampler2D tex0_detail : register(s0);
uniform sampler2D tex1_detail : register(s1);
uniform sampler2D tex2_detail : register(s2);
uniform float4 colorscale_detail : register(c0);
uniform float detailtile : register(c1);

struct PS_INPUT_DETAIL {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
};

float4 main_simpleDetailNoiseDetail(PS_INPUT_DETAIL IN) : COLOR
{
    float2 jitter = NoiseDetailJitter(IN.texcoord0);
    float4 final = tex2DNoiseDetailJ(tex0_detail, IN.texcoord0 * 1.2, jitter) * IN.color * colorscale_detail.x;
    final.rgb *= tex2DNoiseDetailJ(tex2_detail, IN.texcoord0 * detailtile, jitter).rgb * 2.0;
    return final;
}

// ============================================================
// Pass 2: Noise-detail Xbox building (env map + diffuse)
// Entry: main_xboxBuildingNoiseDetail
// ============================================================
uniform sampler2D tex0_xbox : register(s0);
uniform sampler2D tex1_xbox : register(s1);

struct PS_INPUT_XBOX {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float4 envcolor  : COLOR1;
};

float4 main_xboxBuildingNoiseDetail(PS_INPUT_XBOX IN) : COLOR
{
    // Only the diffuse tap is jittered - the env tap is a smooth reflection and
    // an offset there just makes it boil.
    return tex2DNoiseDetail(tex0_xbox, IN.texcoord0 * 1.2) * IN.color +
           tex2D(tex1_xbox, IN.texcoord1) * IN.envcolor;
}
