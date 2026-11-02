// LUTOps.hlsl - Analytic tonemap operators for TonemapLUT.hlsl (ps_3_0)
//
// BINDING CONTRACT (confirmed in src/render/postfx.cpp ColourFilter_Modern,
// the seam that draws this shader — NOT assumed from the lane brief):
//   postfx.cpp:1674 / :1773  rwRENDERSTATETEXTURERASTER = pRasterFrontBuffer
//                             -> s0 = graded linear-HDR front buffer
//   postfx.cpp:1985-1991     s1 = frameLumaTex ONLY when auto-exposure is on
//   postfx.cpp:1911/1915/1937/1980  c5..c8 uploaded for BOTH the Hable pass
//                             and this pass (TonemapPass contract)
//   postfx.cpp:2004-2007     c9 = {tonemapOperator, 0, 0, 0}  <- NOT c0;
//                             c0..c2 hold the grading tint matrices for
//                             gradingPS (postfx.cpp:1722-1724), so an
//                             operator at c0.x would read the red tint.
//
// NO LUT TEXTURE IS BOUND: s1 is the auto-exposure luminance readback, s2 is
// never touched, no LUT raster exists or is uploaded anywhere in the repo
// (FusionShaders' ACES_20/AgX DDS samples are NOT loaded — they are upstream
// assets of a different mod). The five operators are therefore evaluated
// ANALYTICALLY per pixel. Zero texture dependencies.
//
// CONTRACT for every function here:
//   in  : display-referred-ish LINEAR HDR RGB, exposure-scaled and clamped to
//         [0, LUT_MAX_HDR] by TonemapLUT.hlsl main()
//   out : DISPLAY-LINEAR [0, 1]. main() applies the exact sRGB OETF next,
//         exactly like the legacy Hable path in TonemapPass.hlsl.
//   Exceptions (documented per operator):
//     op3 AgX  - the sigmoid output is display-ENCODED (standard GPU port of
//                Sobotka's AgX), so LUT_AgX linearises it with the exact sRGB
//                EOTF before returning, keeping the contract above.
//
// SM3.0 rules (AGENTS.md): every runtime division denominator floored with
// max(x, 1e-7) (compile-time constant denominators are folded by fxc and are
// commented where deliberately negative); no bitwise ops; no SM4+ syntax;
// no value-dependent branches - selection uses step/lerp/(uniform if) only.
// The operator dispatch in LUT_ApplyOperator switches on c9.x, a per-draw
// UNIFORM float, so ps_3_0 control flow stays uniform.
//
// op -> curve map (menu labels owned by debugmenu_ui.cpp:595):
//   op1 ACES 1.3    Stephen Hill RRT+ODTFit, VERBATIM
//                    docs/research/aces-bakinglab.hlsl (BakingLab ACES.hlsl)
//                    = FusionShaders assets/luts/utils/ACES_StephenHill.h
//                    (byte-identical matrices/fit - both on disk)
//   op2 "ACES 2.0"  SEE REPORT: no ACES 2.0 analytic source exists anywhere on
//                    disk (only a baked ACES_20.dds produced by DaVinci Resolve
//                    - utils/Resolve.comp AcesVersion=ACES_VERSION_2_0_0).
//                    Slot filled with the OTHER on-disk ACES fit, Narkowicz's
//                    ACESFilmicTonemap from
//                    E:\SDKs\LibertyRecomp-main\LibertyRecomp\gpu\shader\hlsl\
//                    hdr_tonemap_ps.hlsl (a=2.51 b=0.03 c=2.43 d=0.59 e=0.14).
//                    FLAGGED to the orchestrator for relabel or replacement.
//   op3 AgX         Troy Sobotka log-sigmoid, standard GPU port
//                    (inset/outset matrices, -12.47393..4.026069 EV, 6th-order
//                    contrast polynomial). No AgX HLSL on disk either - only
//                    AgX_Base_Contrast.dds - so these constants are written
//                    from the published reference implementation. FLAGGED.
//   op4 Frostbite   E:\SDKs\GTAIV.EFLC.FusionShaders\assets\luts\utils\
//                    frostbite.hlsl applyHuePreservingShoulder (threshold 0.25,
//                    hue/achromatic mix 0.6, desat exponent 1.3, post-compress
//                    sat boost 0.3). ICtCp RGB round-trip replaced by a
//                    Rec.709 luminance formulation (budget: all five operators
//                    live in ONE ps_3_0 shader); the two scalar brightness
//                    thresholds keep their published meaning via LUT_PQI().
//   op5 GT7          E:\SDKs\GTAIV.EFLC.FusionShaders\assets\luts\utils\
//                    GT7_Tonemap.hlsl GTToneMappingCurveV2 + initializeAsSDR
//                    (alpha .25, mid .538, linearSection .444, toe 1.280,
//                    paper white 250 nits -> 2.5 fb, sdrCorrection 0.4,
//                    chroma fade .98/1.16, blend .6). ICtCp/Jzazbz UCS replaced
//                    by Rec.709 luminance + the same LUT_PQI() threshold
//                    surrogate; curve constants and blend math are verbatim.
//
// Entry point lives in ../ps/TonemapLUT.hlsl (fast_build compiles every
// shaders/ps/*.hlsl with /T ps_3_0 /E main).

#ifndef LUTOPS_INCLUDED
#define LUTOPS_INCLUDED

// Rec.709 luminance weights (GT7_Tonemap / frostbite perceptual weights).
static const float3 LUT_LUMA = float3(0.2126, 0.7152, 0.0722);

// HDR input ceiling enforced by the caller before dispatch (AGENTS: clamp
// before log). Re-asserted defensively inside each operator.
static const float LUT_MAX_HDR = 64.0;

// ---------------------------------------------------------------------------
// LUT_PQI - ICtCp "I" surrogate for scalar brightness thresholds.
//
// frostbite.hlsl and GT7_Tonemap.hlsl make their luma-dependent decisions
// (pre-desaturation ramp, post-compression saturation boost, chroma fade
// around paper white) on the ICtCp FIRST component, which for the neutral
// axis is exactly the ST.2084 PQ encode of luminance with 1.0 = 100 nits
// (both sources: linearToPQ(col, 100.0) / REFERENCE_LUMINANCE = 100 cd/m2).
// Feeding raw Rec.709 luma into those constants instead would move them by
// orders of magnitude (I = 0.3 is ~10 nits, I = 1.0 is ~10000 nits) and
// grey out bright surfaces much earlier than the reference implementations.
//
// Only the neutral-axis encode is kept: the full ICtCp round trip
// (RGB->XYZ->LMS->PQ->2 matrices->PQ^-1->LMS->XYZ->RGB, ~6 muls + 4 pows) is
// what the pre-crash lane dropped for the 512 arithmetic-slot budget; this
// costs 2 pows and is used by op4/op5 only.
// ---------------------------------------------------------------------------
float LUT_PQI(float luma)
{
    const float m1 = 0.1593017578125;   // 2610 / 4096 / 4
    const float m2 = 78.84375;          // 2523 / 4096 * 128
    const float c1 = 0.8359375;         // 3424 / 4096
    const float c2 = 18.8515625;        // 2413 / 4096 * 32
    const float c3 = 18.6875;           // 2392 / 4096 * 32

    float y = max(luma, 0.0) * 0.01;    // fb (1.0 = 100 nits) -> PQ (1.0 = 10000)
    float ym = pow(max(y, 1e-7), m1);   // y -> 0 gives PQ ~7e-7, i.e. black
    return pow((c1 + c2 * ym) / max(1.0 + c3 * ym, 1e-7), m2);
}

// ---------------------------------------------------------------------------
// op1 - ACES 1.3 filmic fit (Stephen Hill), VERBATIM from
// docs/research/aces-bakinglab.hlsl (Baking Lab / MJP + David Neubelt, which
// in turn is github.com/TheRealMJP/BakingLab ACES.hlsl). Identical to
// FusionShaders assets/luts/utils/ACES_StephenHill.h on disk.
// The only edit is the division guard: b >= 0.238081 for every v >= 0 (the
// caller clamps to [0, 64]), so max(b, 1e-7) never fires -> bit-exact.
// ---------------------------------------------------------------------------
// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / max(b, 1e-7);
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

float3 LUT_Aces13(float3 x)
{
    return ACESFitted(clamp(x, 0.0, LUT_MAX_HDR));
}

// ---------------------------------------------------------------------------
// op2 - "ACES 2.0" slot. NO ACES 2.0 analytic source exists on disk; this is
// the Narkowicz ACES fit shipped by LibertyRecomp (ACESFilmicTonemap, hdr_tonemap_ps.hlsl).
// FLAGGED in the lane report: this is an ACES 1.3-era per-channel fit, not the
// ACES 2.0 RRT - see the op->curve map in the header.
// ---------------------------------------------------------------------------
float3 LUT_AcesFilmic(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    x = clamp(x, 0.0, LUT_MAX_HDR);
    float3 num = x * (a * x + b);
    float3 den = max(x * (c * x + d) + e, 1e-7);
    return saturate(num / den);
}

// ---------------------------------------------------------------------------
// op3 - AgX (Troy Sobotka log-sigmoid), standard GPU port.
// inset (white-preserving, rows sum to 1) -> log2 -> EV normalise ->
// 6th-order contrast polynomial -> outset (rows sum to 1) -> sRGB EOTF.
// NOTE on matrix layout: the reference lists these column-major (GLSL mat3
// constructor); each row below is the TRANSPOSE of that listing so that
// mul(M, v) matches the reference's M * v. Verified: every row sums to 1.0,
// i.e. neutral grey round-trips exactly - the property an inset/outset pair
// must have.
// ---------------------------------------------------------------------------
static const float3x3 LUT_AgXInset =
{
    { 0.856627153315983,  0.0951212405381588, 0.0482516061458583},
    { 0.137318972929847,  0.761241990602591,  0.101439036467562},
    { 0.11189821299995,   0.0767994186031903, 0.811302368396859}
};

static const float3x3 LUT_AgXOutset =
{
    { 1.1271005818144368, -0.11060664309660323, -0.016493938717834573},
    {-0.1413297634984383,  1.157823702216272,   -0.016493938717834257},
    {-0.14132976349843826, -0.11060664309660294,  1.2519364065950405}
};

static const float LUT_AgXMinEV = -12.47393;
static const float LUT_AgXMaxEV = 4.026069;   // span = 16.5 stops

float3 LUT_AgXSigmoid(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  15.5    * x4 * x2
          - 40.14   * x4 * x
          + 31.96   * x4
          - 6.868   * x2 * x
          + 0.4298  * x2
          + 0.1191  * x
          - 0.00232;
}

float3 LUT_AgX(float3 x)
{
    x = clamp(x, 0.0, LUT_MAX_HDR);

    float3 v = mul(LUT_AgXInset, x);
    // clamp before log (AGENTS); min EV floor also keeps log2(0) finite
    v = clamp(log2(max(v, 1e-10)), LUT_AgXMinEV, LUT_AgXMaxEV);
    v = (v - LUT_AgXMinEV) / max(LUT_AgXMaxEV - LUT_AgXMinEV, 1e-7);
    v = LUT_AgXSigmoid(v);
    v = mul(LUT_AgXOutset, v);

    // Sigmoid output is display-ENCODED -> exact sRGB EOTF back to linear so
    // the shared sRGB OETF in main() sees real light (see header contract).
    v = max(v, 0.0);
    float3 lo = v / 12.92;
    float3 hi = pow(max((v + 0.055) / 1.055, 1e-7), 2.4);
    return saturate(lerp(hi, lo, 1.0 - step(0.04045, v)));
}

// ---------------------------------------------------------------------------
// op4 - Frostbite range compression (frostbite.hlsl applyHuePreservingShoulder),
// Rec.709-luma formulation of the ICtCp round trip (see LUT_PQI header).
// ---------------------------------------------------------------------------
// Photographic range compression with a linear segment below `threshold`.
// step()/lerp() instead of the source's `val < threshold ? v1 : v2` so ps_3_0
// control flow stays branch-free.
float LUT_RangeCompress(float val, float threshold)
{
    float span = max(1.0 - threshold, 1e-7);
    float over = max(val - threshold, 0.0);
    float compressed = threshold + span * (1.0 - exp(-over / span));
    return lerp(val, compressed, step(threshold, val));
}

float3 LUT_Frostbite(float3 x)
{
    x = clamp(x, 0.0, LUT_MAX_HDR);

    float lum = dot(x, LUT_LUMA);
    float I = LUT_PQI(lum);                          // ICtCp I surrogate

    // 1. luma-adaptive pre-desaturation: pow(smoothstep(1.0, 0.3, I), 1.3).
    //    Descending form written as 1 - ascending (identical, defined edges).
    float desat = 1.0 - smoothstep(0.3, 1.0, I);
    float satAmount = pow(saturate(desat), 1.3);
    float3 col = lum.xxx + (x - lum.xxx) * satAmount;

    // 2. hue-preserving vs per-channel range compression, mixed 0.6.
    float maxCol = max(col.x, max(col.y, col.z));
    float mappedMax = LUT_RangeCompress(maxCol, 0.25);
    float3 huePreserving = col * (mappedMax / max(maxCol, 1e-7));  // source has 0/0 here
    float3 perChannel = float3(LUT_RangeCompress(col.x, 0.25),
                               LUT_RangeCompress(col.y, 0.25),
                               LUT_RangeCompress(col.z, 0.25));
    col = lerp(perChannel, huePreserving, 0.6);

    // 3. re-instate chroma lost to the compression (source:
    //    postCompressionSaturationBoost = 0.3 * smoothstep(1.0, 0.5, I)).
    //    Luma-space form of ictcpMapped.yz = lerp(..., ictcp.yz * Iout/Iin).
    float lumOut = dot(col, LUT_LUMA);
    float boost = 0.3 * (1.0 - smoothstep(0.5, 1.0, I));
    float3 chromaTarget = (x - lum.xxx) * (lumOut / max(lum, 1e-7));
    float3 chroma = lerp(col - lumOut.xxx, chromaTarget, boost);

    return saturate(lumOut.xxx + chroma);
}

// ---------------------------------------------------------------------------
// op5 - GT7 (GT7_Tonemap.hlsl GTToneMappingCurveV2 + initializeAsSDR),
// Rec.709-luma formulation of the ICtCp UCS (see LUT_PQI header).
// Curve constants, shoulder algebra, chroma fade and 0.4 SDR correction are
// verbatim; only the UCS colour round trip is replaced.
// ---------------------------------------------------------------------------
static const float LUT_GT7_Peak   = 2.5;    // 250 nits paper white / 100 (REFERENCE_LUMINANCE)
static const float LUT_GT7_Alpha  = 0.25;
static const float LUT_GT7_Mid    = 0.538;
static const float LUT_GT7_LinSec = 0.444;
static const float LUT_GT7_Toe    = 1.280;
static const float LUT_GT7_Blend  = 0.6;
static const float LUT_GT7_Fade0  = 0.98;
static const float LUT_GT7_Fade1  = 1.16;
static const float LUT_GT7_SDR    = 0.4;     // 1 / 2.5, initializeAsSDR

// k = (linearSection - 1) / (alpha - 1) = 0.741333... — compile-time constant
// expression: the denominator is the non-zero constant -0.75, so no runtime
// guard applies (max(x,1e-7) would WRONGLY clamp this negative divisor).
static const float LUT_GT7_K  = (LUT_GT7_LinSec - 1.0) / (LUT_GT7_Alpha - 1.0);
static const float LUT_GT7_KA = LUT_GT7_Peak * LUT_GT7_LinSec + LUT_GT7_Peak * LUT_GT7_K;

float LUT_GT7Curve(float x, float kB, float kC)
{
    x = max(x, 0.0);

    float t = saturate(x / max(LUT_GT7_Mid, 1e-7));
    float wLinear = t * t * (3.0 - 2.0 * t);      // smoothStep(x, 0, midPoint)
    float wToe = 1.0 - wLinear;
    float toe = LUT_GT7_Mid * pow(max(x / max(LUT_GT7_Mid, 1e-7), 1e-7), LUT_GT7_Toe);
    float shoulder = LUT_GT7_KA + kB * exp(x * kC);

    // source: if (x < linearSection * peakIntensity) toe/linear else shoulder
    float belowLinear = 1.0 - step(LUT_GT7_LinSec * LUT_GT7_Peak, x);
    return lerp(shoulder, wToe * toe + wLinear * x, belowLinear);
}

float3 LUT_GT7(float3 x)
{
    x = clamp(x, 0.0, LUT_MAX_HDR);

    // initializeCurve: kB = -peak*k*exp(linearSection/k), kC = -1/(k*peak)
    float kB = -LUT_GT7_Peak * LUT_GT7_K * exp(LUT_GT7_LinSec / max(LUT_GT7_K, 1e-7));
    float kC = -1.0 / max(LUT_GT7_K * LUT_GT7_Peak, 1e-7);

    float3 skewed = float3(LUT_GT7Curve(x.r, kB, kC),
                           LUT_GT7Curve(x.g, kB, kC),
                           LUT_GT7Curve(x.b, kB, kC));

    // chromaCurve(ucs.x / framebufferLuminanceTargetUcs_, 0.98, 1.16):
    // fade input chroma out as perceived brightness approaches paper white.
    float lumIn = dot(x, LUT_LUMA);
    float ratio = LUT_PQI(lumIn) / max(LUT_PQI(LUT_GT7_Peak), 1e-7);
    float chromaScale = 1.0 - smoothstep(LUT_GT7_Fade0, LUT_GT7_Fade1, ratio);

    // scaledUcs = (skewed luminance, input chroma * fade) -> luma-space form
    float lumSkewed = dot(skewed, LUT_LUMA);
    float3 scaledRgb = lumSkewed.xxx + (x - lumIn.xxx) * chromaScale;

    // final blend between per-channel and UCS-scaled results + SDR correction
    float3 blended = (1.0 - LUT_GT7_Blend) * skewed + LUT_GT7_Blend * scaledRgb;
    return saturate(LUT_GT7_SDR * min(blended, LUT_GT7_Peak));
}

// ---------------------------------------------------------------------------
// Operator dispatch. c9.x is uploaded once per draw (postfx.cpp:2006), so the
// chain below is uniform control flow - legal in ps_3_0 and only ONE operator
// body is ever evaluated for a given frame. Gaps/defensive values map to the
// nearest operator: <=0 (never expected - operator 0 keeps the C++ Hable path)
// falls back to op1, >=5 to op5.
// ---------------------------------------------------------------------------
float3 LUT_ApplyOperator(float3 hdr, float op)
{
    if (op < 1.5)  return LUT_Aces13(hdr);
    if (op < 2.5)  return LUT_AcesFilmic(hdr);
    if (op < 3.5)  return LUT_AgX(hdr);
    if (op < 4.5)  return LUT_Frostbite(hdr);
    return LUT_GT7(hdr);
}

#endif // LUTOPS_INCLUDED
