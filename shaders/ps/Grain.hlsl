// ============================================================================
// Grain.hlsl - animated film grain (ps_3_0)
// ============================================================================
// LEGACY IGN PATH ONLY.
//
// MODE 2 REMOVED (was: GTA IV / TLAD authored texture grain, selected by
// c0.w >= 1.5). It read s9 (universal packed noise tile) plus the c49..c52
// noise block, but its only caller -- DrawFilmGrain in
// src/render/postfx_newfx.cpp -- never calls pipeBindNoise(9) and uploads only
// c0. The branch was dormant solely because c0.w is hard-zero, so the shader
// carried a latent undefined-sampler read. Mode 2 must not come back until the
// CPU side actually binds a noise tile (and sets POINT filtering) for this
// draw; see docs/plans/2026-10-25-gtaiv-shader-overrides.md section 4 for the
// recovered Rockstar formula.
//
// ACTIVE PATH: Jorge Jimenez Interleaved Gradient Noise (IGN) film-grain port.
//   Ported from:
//     - LibertyRecomp gpu/shader/hlsl/film_grain_ps.hlsl, which itself cites
//       Jorge Jimenez, "Next Generation Post Processing in Call of Duty:
//       Advanced Warfare" (SIGGRAPH 2014). Preserves:
//         * IGN low-discrepancy noise (TAA 3x3-friendly, no temporal crawl),
//         * the 5.588238 px/frame animated scroll with a 64-frame fmod reset,
//         * 3-octave multi-scale grain (crystal-size layers),
//         * the S-curve luminance response (grain hidden in toe + shoulder).
//   Post-tonemap, screen-pixel space via VPOS. 1 fetch (the scene only).
//
// REGISTER MAP (c0 matches DrawFilmGrain, src/render/postfx_newfx.cpp:737 --
// that upload is the source of truth; an earlier revision of this file
// declared c0 = frameIndex / c1 = params, but DrawFilmGrain never uploads c1,
// so the legacy path was reading a stale constant register):
//   s0     : color input (scene / current pass).
//   c0     : (intensity, frameIndex, luminanceScale, coloredGrain)
//              intensity      -- grain amplitude (0..1), config grainIntensity
//              frameIndex     -- integer frame counter (drives animated scroll)
//              luminanceScale -- 0..1 how strongly the S-curve shapes grain
//              coloredGrain   -- 0 = mono, 0..1 = mono/colour blend, 1 = colour.
//                                Values are saturated, so anything >= 1 is
//                                full colour (the old mode selector value 2.0
//                                no longer selects a second path).
//
// COST: 1 fetch, no loops, no bitwise ops.
// ============================================================================

sampler2D sceneTex : register(s0);
// (intensity, frameIndex, luminanceScale, modeOrColored) -- see register map.
uniform float4 grainParam : register(c0);

// --- Interleaved Gradient Noise (IGN), Jorge Jimenez / Activision ---
// Magic constants tuned for low discrepancy inside every 3x3 pixel block.
inline float IGN(float2 pixelCoord)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixelCoord, magic.xy)));
}

// Animated IGN: scroll 5.588238 px/frame; reset every 64 frames for precision.
inline float IGNAnimated(float2 pixelCoord, float frameIndex)
{
    float fi = fmod(frameIndex, 64.0);
    float2 animatedCoord = pixelCoord + 5.588238 * fi;
    return IGN(animatedCoord);
}

// 3-octave multi-scale grain, normalized to ~[-1, 1].
inline float GrainNoise(float2 pixelCoord, float frameIndex)
{
    // Base crystal layer.
    float grain = IGNAnimated(pixelCoord, frameIndex) * 2.0 - 1.0;

    // Finer detail layers, decorrelated by coordinate offsets.
    float grain2 = IGNAnimated(pixelCoord * 2.0 + float2(127.1, 311.7), frameIndex);
    float grain3 = IGNAnimated(pixelCoord * 4.0 + float2(269.5, 183.3), frameIndex);

    grain += (grain2 * 2.0 - 1.0) * 0.5;
    grain += (grain3 * 2.0 - 1.0) * 0.25;

    // Normalize by (1 + 0.5 + 0.25) with a 1e-7 guarded divide.
    return grain / max(1.0 + 0.5 + 0.25, 1e-7);
}

// Per-channel colored grain (independent dye layers).
inline float3 GrainNoiseColored(float2 pixelCoord, float frameIndex)
{
    float3 g;
    g.r = GrainNoise(pixelCoord, frameIndex);
    g.g = GrainNoise(pixelCoord + float2(97.0, 43.0), frameIndex + 17.0);
    g.b = GrainNoise(pixelCoord + float2(43.0, 97.0), frameIndex + 31.0);
    return g;
}

// S-curve luminance response: grain hidden in deep shadows + blown highlights,
// maximum in midtones (photographic film characteristic). Legacy modes only.
inline float GrainResponse(float luminance, float luminanceScale)
{
    float shadowFalloff = smoothstep(0.0, 0.3, luminance);
    float highlightFalloff = smoothstep(1.0, 0.7, luminance);
    float response = shadowFalloff * highlightFalloff;
    // Blend the shaped response against a flat 1.0 by luminanceScale.
    return lerp(1.0, response, saturate(luminanceScale));
}

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN, float4 vpos : VPOS) : COLOR
{
    float2 texCoord = IN.texCoord;

    float4 color = tex2D(sceneTex, texCoord);   // fetch 1 of max 2

    // --- Unpack parameters (layout matches DrawFilmGrain) ---
    float intensity      = max(grainParam.x, 0.0);
    float frameIndex     = grainParam.y;
    float luminanceScale = grainParam.z;
    float coloredGrain   = saturate(grainParam.w);

    if (intensity <= 0.0)
        return color;

    float3 grain;

    // Rec.709 luminance for the film response curve.
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float grainResponse = GrainResponse(luminance, luminanceScale);

    // Screen-pixel coordinate (VPOS) -> resolution-independent grain.
    float2 pixelCoord = vpos.xy;

    float scaledIntensity = intensity * grainResponse;

    // Grain mode: 0 = mono, (0..1) = blend, 1 = colored.
    if (coloredGrain > 0.5)
    {
        grain = GrainNoiseColored(pixelCoord, frameIndex) * scaledIntensity;
    }
    else if (coloredGrain > 0.0)
    {
        float3 colored = GrainNoiseColored(pixelCoord, frameIndex);
        float mono = GrainNoise(pixelCoord, frameIndex);
        grain = lerp(float3(mono, mono, mono), colored, coloredGrain) * scaledIntensity;
    }
    else
    {
        float mono = GrainNoise(pixelCoord, frameIndex);
        grain = float3(mono, mono, mono) * scaledIntensity;
    }

    // Additive grain, clamped to valid range (no negative reconstruction).
    color.rgb = max(color.rgb + grain, 0.0);

    return color;
}
