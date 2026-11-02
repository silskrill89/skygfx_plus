// ============================================================================
// Noise.hlsl - universal packed noise fetch (SM3.0 only: ps_3_0 / vs_3_0)
// ----------------------------------------------------------------------------
// Backed by SkyNoise (src/render/noise.h|cpp), which bakes ONE tileable
// A8R8G8B8 tile bound as sampler s9 with WRAP / LINEAR / MIPFILTER NONE.
//
// CHANNEL PACKING (canonical contract - do not reorder channels):
//   .r = tileable Perlin fBm    -> macro shapes, cloud bodies
//   .g = tileable Worley F1     -> erosion, cauliflower cloud edges
//   .b = white noise hash       -> per-pixel hash (raymarch/PCF/SSAO jitter)
//   .a = blue noise (Mitchell)  -> ordered dither, TAA-friendly jitter
//
// REGISTER BUDGET
//   s9    the packed noise tile (WRAP, LINEAR, no mips)
//   c49   { noiseScale,  worleyScale, coverage,     erosion }
//   c50   { whiteScale,  blueScale,   timeSeconds,  frameIndex }
//   c51   { cloudScaleA, cloudScaleB, cloudScaleC,  cloudDensity }
//   c52   { jitterAmount, ditherAmount, detailStrength, tileTexelSize }
//   c49-c52 are the default knob block. Every helper below ALSO takes explicit
//   parameters, so a shader can be driven without uploading them. Registers
//   that were never uploaded read as 0: all scales/amplitudes collapse to 0,
//   which turns jitter/dither into no-ops rather than garbage.
//
//   c52.w (was "unused") = SkyNoise tile size in texels, uploaded by the CPU
//   (SkyNoise::UniversalSize(): 32/64/128; 0 = no signal from legacy uploads).
//   NoiseTileOK() composes it with a live probe of s9 so a consumer can degrade
//   gracefully when the tile is low-res or absent instead of going flat.
//
// SM3.0 RULES OBSERVED HERE
//   * tex2D only - the tile has no mips, so tex2Dlod/derivatives are not needed
//   * every fetch is unconditional; no tex2D inside a dynamic branch, so the
//     implicit gradient is always defined (SM3.0 gradient rule)
//   * no bitwise ops, no integer textures, no dynamic-bound loops
//   * every divide/normalise is guarded with 1e-7 (NaN writes black on SM3.0)
//
// INCLUDE PATH
//   shaders/ps/*.hlsl      ->  #include "../include/Noise.hlsl"
//   shaders/*.hlsl (root)  ->  #include "include/Noise.hlsl"
//   FXC resolves quoted includes relative to the including file.
// ============================================================================

#ifndef NOISE_UNIVERSAL_INCLUDED
#define NOISE_UNIVERSAL_INCLUDED

#define NOISE_EPS 1e-7

sampler2D uniNoiseTex     : register(s9);
float4    uniNoiseParams0 : register(c49);
float4    uniNoiseParams1 : register(c50);
float4    uniNoiseParams2 : register(c51);
float4    uniNoiseParams3 : register(c52);

// --- raw packed fetch -------------------------------------------------------
float4 NoiseUniversal(float2 uv)
{
    return tex2D(uniNoiseTex, uv);
}

// --- single channels --------------------------------------------------------
float NoisePerlin(float2 uv) { return NoiseUniversal(uv).r; }
float NoiseWorley(float2 uv) { return NoiseUniversal(uv).g; }
float NoiseWhite(float2 uv)  { return NoiseUniversal(uv).b; }
float NoiseBlue(float2 uv)   { return NoiseUniversal(uv).a; }

// --- 3-tap "infinite" fBm ---------------------------------------------------
// THE INFINITE CLOUD TRICK: three fetches of the same tile at 1x / 3.17x /
// 9.41x, each with a different non-integer offset. Their periods never line up,
// so the sum reads like a deep octave stack. A real 8-octave fBm costs 8+
// fetches and 100+ ALU here; this costs 3 fetches and ~6 ALU. The weights sum
// to 1.0, so the result is already normalised to [0,1], and because the source
// tile is seamless the sum still tiles. Do NOT "optimise" this into a loop.
float NoiseFbm3(float2 uv, float baseScale)
{
    float s  = max(baseScale, NOISE_EPS);
    float3 n = float3(NoisePerlin(uv * s),
                      NoisePerlin(uv * s * 3.17 + 0.37),
                      NoisePerlin(uv * s * 9.41 + 0.71));
    return saturate(dot(n, float3(0.55, 0.30, 0.15)));
}

// --- cloud density ----------------------------------------------------------
// Perlin builds the body, Worley carves the cauliflower edge. `coverage` is the
// soft-threshold centre; `erosion` is how far the cellular detail eats into the
// body. The Worley tap is frequency-offset from the Perlin taps so the two
// fields cannot align into visible banding. 4 fetches (3 + 1).
float NoiseCloudDensity(float2 uv, float scale, float coverage, float erosion)
{
    float s     = max(scale, NOISE_EPS);
    float body  = NoiseFbm3(uv, s);
    float cells = NoiseWorley(uv * s * 2.31 + 0.19);
    float x     = body - cells * erosion;
    return saturate(smoothstep(coverage, coverage + 0.35, x));
}

// --- zero-mean jitter / dither ----------------------------------------------
// (A - 0.5) is a zero-mean uniform sequence, and A is a rank permutation of the
// tile, so any threshold stays unbiased. Multiplying by `amplitude` makes an
// unuploaded c52.x (== 0) collapse to exactly 0 => no jitter => deterministic
// single-tap fallback with no branch needed.
float NoiseBlueJitter(float2 uv, float amplitude)
{
    return (NoiseBlue(uv) - 0.5) * amplitude;
}

float NoiseDither(float2 uv, float amplitude)
{
    // Swizzled + offset so a shader calling both at the same uv does not dither
    // with the exact sequence it jitters with (visible correlation otherwise).
    return (NoiseBlue(uv.yx * 1.13 + 0.53) - 0.5) * amplitude;
}

// --- legacy grain emulation (ALU-only, no fetch) ----------------------------
// Emulates the CPostEffects::Grain_PS2 legacy grain raster (postfx_colourfilter.cpp):
// a 64x64 VU-LFSR noise raster (grey = LFSR low byte, alpha = grey & strength)
// drawn full-screen WRAP+LINEAR with UVs 5x(640) x 7x(448) -> one grain texel
// covers ~3.2 screen pixels and the raster is regenerated (reseeded) every
// frame, so the grain crawls. This helper reproduces that LOOK with pure ALU:
// a bilinear-interpolated hash on a ~3.2px texel grid, animated by the same
// 5.588238 px/frame scroll the film-grain pass (Grain.hlsl IGNAnimated) uses —
// one shared grain family across the post chain and the cloud/sky consumers.
//
// WHY ALU-ONLY: the whole point of the legacy-grain connection is the degraded
// regime — when s9 is unbound/blank there is no tile to fetch, so the fallback
// grain must not depend on it. 4 hash evals + 2 lerps, no sampler, no branch.
//
// `pixelCoord` is screen-pixel space (VPOS.xy on ps_3_0). `frameIndex` is the
// c50.w frame counter (any monotonically growing counter works).
float NoiseLegacyGrain(float2 pixelCoord, float frameIndex)
{
    // Legacy raster geometry: 64x64 texels over ~5 screen-widths -> ~3.2 px.
    static const float LEGACY_TEXEL = 3.2;
    // Animated scroll (Grain.hlsl IGNAnimated constants): 5.588238 px/frame,
    // fmod 64 for float precision — matches the per-frame raster regen crawl.
    float2 texel = pixelCoord * (1.0 / LEGACY_TEXEL)
                 + 5.588238 * fmod(frameIndex, 64.0);
    float2 c = floor(texel);
    float2 f = texel - c;   // LINEAR weights (the legacy raster is LINEAR too)

    // IGN hash (Jorge Jimenez, same magic as Grain.hlsl) as the LFSR stand-in:
    // per-texel white-ish sequence, zero fetches, deterministic per frame.
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    float g00 = frac(magic.z * frac(dot(c, magic.xy)));
    float g10 = frac(magic.z * frac(dot(c + float2(1.0, 0.0), magic.xy)));
    float g01 = frac(magic.z * frac(dot(c + float2(0.0, 1.0), magic.xy)));
    float g11 = frac(magic.z * frac(dot(c + float2(1.0, 1.0), magic.xy)));

    return lerp(lerp(g00, g10, f.x), lerp(g01, g11, f.x), f.y);
}

// --- tile presence / resolution ---------------------------------------------
// NoiseTileLive(): is s9 actually serving the packed tile? A fixed texel is
// probed (uniform fetch, no gradient dependence): an unbound sampler reads 0,
// a blank/failed upload reads ~0, a healthy tile reads >= ~0.5 from the white
// + blue channels alone (both are mid-frequency hashes, never all-dark).
// Doubled and saturated so any healthy tile reads exactly 1.
float NoiseTileLive()
{
    float4 probe = tex2D(uniNoiseTex, float2(0.25, 0.75));
    return saturate(dot(probe, float4(1.0, 1.0, 1.0, 1.0)) * 2.0);
}

// NoiseTileOK(): the composed quality factor a consumer should blend by.
//   1        = full tile (probe live AND no lower resolution signalled)
//   (0,1)    = low-res tile (c52.w signal 32 < 64) — consumer blends its
//              legacy-grain fallback in by (1 - tileOK)
//   0        = tile absent/blank — consumer runs its legacy-grain fallback
//   The c52.w signal (SkyNoise::UniversalSize(), 0 = no signal) is authoritative
//   when present; the probe covers legacy uploads that never signal.
float NoiseTileOK()
{
    float res    = uniNoiseParams3.w;                       // c52.w, 0 = no signal
    float hasSig = step(0.5, res);                          // 0 or >= 32 by contract
    return lerp(NoiseTileLive(), saturate(res * (1.0 / 64.0)), hasSig);
}

#endif // NOISE_UNIVERSAL_INCLUDED
