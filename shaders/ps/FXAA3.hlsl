// FXAA3.hlsl - Fast Approximate Anti-Aliasing, FXAA 3.11 console-lite (ps_3_0)
// Cheap edge-luma-search + blend variant, valid on Shader Model 3.0.
//
// Ported from:
//   - GTAIV.EFLC.FusionFix source/resources/FXAA3_11.h -- the "FXAA3 CONSOLE -
//     PC VERSION" block (#if FXAA_PC_CONSOLE == 1). This is the cheap 9-tap
//     console variant (4 corner luma taps + center + two blend rings), NOT the
//     full FXAA_QUALITY multi-iteration version. The quality version needs more
//     dependent fetches than we want on ps_3_0; the console variant is the
//     right cost/quality trade-off here.
//
// Tuning constants below mirror the FXAA 3.11 console / PS3 knobs:
//   FXAA_CONSOLE__PS3_EDGE_SHARPNESS (8.0 sharper / 4.0 softer / 2.0 very soft)
//   FXAA_CONSOLE__PS3_EDGE_THRESHOLD (0.25 sharper / 0.125 softer)
//   FXAA_CONSOLE__PS3_EDGE_THRESHOLD_MIN (absolute floor)
//
// Register map:
//   s0     : color input (scene / current pass).
//   c0.xy  : invSizePx = (1/width, 1/height) of s0. c0.zw reserved (0).
//   c1     : reserved (unused).
//   c2     : reserved (unused).

sampler2D colorTex : register(s0);

uniform float4 invSizePx : register(c0); // (1/width, 1/height, 0, 0)

// --- FXAA 3.11 console tuning knobs (see FXAA3_11.h) ---
#define FXAA_EDGE_SHARPNESS      8.0     // 8 = sharp, 4 = soft, 2 = very soft
#define FXAA_EDGE_THRESHOLD      0.125   // relative luma range gate (0.25 sharp)
#define FXAA_EDGE_THRESHOLD_MIN  0.0312  // absolute luma floor
// Small tie-break bias added to the NE luma, as in the reference console path.
#define FXAA_LUMA_BIAS (1.0 / 384.0)

// Rec.709 luma (FXAA_GREEN_AS_LUMA == 0 semantics: full luma, not just green).
inline float FxaaLuma(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

// Guarded 2D normalize -- zero-length dir must not produce NaN.
inline float2 FxaaNormalize(float2 v)
{
    float len = length(v);
    return len > 1e-6 ? v / len : float2(1.0, 0.0);
}

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 pos = IN.texCoord;
    float2 rcp = max(invSizePx.xy, float2(1e-7, 1e-7)); // 1/width, 1/height

    // Pixel-corner positions for the 4 diagonal luma taps (NW/SW/NE/SE).
    //   posPos.xy = NW (pos - 0.5 texel)
    //   posPos.zw = SE (pos + 0.5 texel)
    // NW=xy, SW=xw, NE=zy, SE=zw below.
    float2 posMin = pos - 0.5 * rcp;
    float2 posMax = pos + 0.5 * rcp;

    // --- Four corner luma taps + centre ---
    float lumaNW = FxaaLuma(tex2D(colorTex, float2(posMin.x, posMin.y)).rgb);
    float lumaSW = FxaaLuma(tex2D(colorTex, float2(posMin.x, posMax.y)).rgb);
    float lumaNE = FxaaLuma(tex2D(colorTex, float2(posMax.x, posMin.y)).rgb);
    float lumaSE = FxaaLuma(tex2D(colorTex, float2(posMax.x, posMax.y)).rgb);

    float4 rgbyM = tex2D(colorTex, pos);
    float  lumaM = FxaaLuma(rgbyM.rgb);

    // --- Local contrast / edge test (console mapping) ---
    float lumaMaxNwSw = max(lumaNW, lumaSW);
    lumaNE += FXAA_LUMA_BIAS;
    float lumaMinNwSw = min(lumaNW, lumaSW);

    float lumaMaxNeSe = max(lumaNE, lumaSE);
    float lumaMinNeSe = min(lumaNE, lumaSE);

    float lumaMax = max(lumaMaxNeSe, lumaMaxNwSw);
    float lumaMin = min(lumaMinNeSe, lumaMinNwSw);

    float lumaMaxScaled = lumaMax * FXAA_EDGE_THRESHOLD;
    float lumaMinM = min(lumaMin, lumaM);
    float lumaMaxScaledClamped = max(FXAA_EDGE_THRESHOLD_MIN, lumaMaxScaled);
    float lumaMaxM = max(lumaMax, lumaM);

    float dirSwMinusNe = lumaSW - lumaNE;
    float lumaMaxSubMinM = lumaMaxM - lumaMinM;
    float dirSeMinusNw = lumaSE - lumaNW;

    // Not enough local contrast -> leave the pixel untouched.
    if (lumaMaxSubMinM < lumaMaxScaledClamped)
        return rgbyM;

    // --- Edge direction (diagonal gradient) ---
    float2 dir;
    dir.x = dirSwMinusNe + dirSeMinusNw;
    dir.y = dirSwMinusNe - dirSeMinusNw;

    float2 dir1 = FxaaNormalize(dir);

    // Ring 1: half-texel taps along the edge normal.
    float2 step1 = 0.5 * rcp;
    float4 rgbyN1 = tex2D(colorTex, pos - dir1 * step1);
    float4 rgbyP1 = tex2D(colorTex, pos + dir1 * step1);

    // Ring 2: stretch dir along the dominant axis (edge sharpness) for a wider
    // blur, clamped to +/-2 texels -- the classic console "cheap" second tap.
    float dirAbsMinTimesC = min(abs(dir1.x), abs(dir1.y)) * FXAA_EDGE_SHARPNESS;
    float2 dir2 = clamp(dir1 / max(dirAbsMinTimesC, 1e-7), -2.0, 2.0);

    float2 step2 = 2.0 * rcp;
    float4 rgbyN2 = tex2D(colorTex, pos - dir2 * step2);
    float4 rgbyP2 = tex2D(colorTex, pos + dir2 * step2);

    // --- Blend the two rings ---
    float4 rgbyA = rgbyN1 + rgbyP1;
    float4 rgbyB = ((rgbyN2 + rgbyP2) * 0.25) + (rgbyA * 0.25);

    // If the wide blend overshoots the local luma range, fall back to ring 1.
    float lumaB = FxaaLuma(rgbyB.rgb);
    bool twoTap = (lumaB < lumaMin) || (lumaB > lumaMax);
    if (twoTap)
        rgbyB.rgb = rgbyA.rgb * 0.5;

    // Preserve source alpha.
    rgbyB.a = rgbyM.a;

    return rgbyB;
}
