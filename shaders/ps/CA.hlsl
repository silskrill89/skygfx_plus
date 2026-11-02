// CA.hlsl - Lateral (transverse) chromatic aberration (ps_3_0)
// Per-channel radial RGB offset with a smoothstep soft knee and aspect correction.
//
// Ported from:
//   - LibertyRecomp gpu/shader/hlsl/chromatic_aberration_ps.hlsl. Preserves the
//     per-channel radial offset model, the smoothstep soft-knee falloff, aspect
//     correction (so the fringing is circular, not elliptical, on wide displays),
//     and the max-offset clamp that prevents extreme distortion. The green
//     sample is reused for alpha (saves a fetch), exactly as in the source.
//
// Register map:
//   s0     : color input (scene / current pass).
//   c0     : (intensity, radialFalloff, maxOffset, aspect)
//              intensity     -- overall strength (0..1 typical)
//              radialFalloff -- pow exponent: 1 = linear, 2 = quadratic toward edge
//              maxOffset     -- maximum radial offset in UV units (e.g. 0.01)
//              aspect        -- width/height (>=1), corrects the radial distance
//   c1     : reserved (unused).
//   c2     : reserved (unused).
//
// Center is fixed at (0.5, 0.5). Per-channel offset scales + soft knee are the
// tuning constants below (kept out of the register budget).

sampler2D colorTex : register(s0);

uniform float4 caParam : register(c0); // (intensity, radialFalloff, maxOffset, aspect)

// --- Lateral CA tuning knobs ---
#define CA_RED_SCALE   1.0   // red pushed outward (+)
#define CA_GREEN_SCALE 0.0   // green stays centred
#define CA_BLUE_SCALE  1.0   // blue pulled inward (-) in the sample below
#define CA_SOFT_KNEE   0.5   // 0 = sharp onset, 1 = fully smoothstepped onset

// Guarded 2D normalize (zero-length direction must not yield NaN).
inline float2 SafeNormalize2(float2 v)
{
    float len = length(v);
    return len > 1e-6 ? v / len : float2(1.0, 0.0);
}

// Clamp sample UV into the valid range (avoid border wrap / clamp artefacts).
inline float2 ClampUV(float2 uv)
{
    return clamp(uv, 0.0, 1.0);
}

// Scale a 2D offset so its magnitude never exceeds maxMag (guarded divide).
inline float2 ClampMagnitude(float2 v, float maxMag)
{
    float m = length(v);
    return (m > maxMag && m > 1e-7) ? v * (maxMag / m) : v;
}

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texCoord = IN.texCoord;

    // --- Unpack parameters ---
    float intensity     = caParam.x;
    float radialFalloff = caParam.y;
    float maxOffset     = max(caParam.z, 0.0);
    float aspect        = max(caParam.w, 1e-7);

    // Guards.
    intensity = max(intensity, 0.0);
    radialFalloff = max(radialFalloff, 1e-7);

    // Nothing to do.
    if (intensity <= 0.0 || maxOffset <= 0.0)
        return tex2D(colorTex, texCoord);

    // --- Radial geometry ---
    float2 center = float2(0.5, 0.5);
    float2 centerOffset = texCoord - center;

    // Aspect-correct the distance so the effect is circular on wide displays.
    float2 correctedOffset = centerOffset;
    correctedOffset.x *= aspect;
    float distFromCenter = length(correctedOffset);

    // Exact centre: no aberration.
    if (distFromCenter < 1e-4)
        return tex2D(colorTex, texCoord);

    // --- Soft-knee radial falloff ---
    float normalizedDist = saturate(distFromCenter * 2.0);
    float smoothDist = lerp(normalizedDist,
                            smoothstep(0.0, 1.0, normalizedDist),
                            saturate(CA_SOFT_KNEE));
    float falloff = pow(max(smoothDist, 0.0), radialFalloff);

    // Radial direction (uncorrected offset -> true radial pattern).
    float2 dir = SafeNormalize2(centerOffset);

    // Per-channel radial offset magnitude (UV units), clamped to maxOffset.
    float offsetScale = intensity * falloff;

    float2 redOffset   = dir * (CA_RED_SCALE   * maxOffset * offsetScale);
    float2 greenOffset = dir * (CA_GREEN_SCALE * maxOffset * offsetScale);
    float2 blueOffset  = dir * (CA_BLUE_SCALE  * maxOffset * offsetScale);

    redOffset   = ClampMagnitude(redOffset,   maxOffset);
    greenOffset = ClampMagnitude(greenOffset, maxOffset);
    blueOffset  = ClampMagnitude(blueOffset,  maxOffset);

    // --- Per-channel sample (red outward, blue inward) ---
    float r = tex2D(colorTex, ClampUV(texCoord + redOffset)).r;

    // Green (and alpha) sampled together at the near-centre offset.
    float4 greenSample = tex2D(colorTex, ClampUV(texCoord + greenOffset));
    float g = greenSample.g;
    float a = greenSample.a;

    float b = tex2D(colorTex, ClampUV(texCoord - blueOffset)).b;

    return float4(r, g, b, a);
}
