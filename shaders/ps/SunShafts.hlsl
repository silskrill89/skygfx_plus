// SunShafts.hlsl - Volumetric light scattering / god rays (ps_3_0)
// Single-pass GPU Gems 3 Chapter 13 radial-scattering march + tint composite.
//
// Ported from:
//   - GPU Gems 3, Ch.13 "Volumetric Light Scattering as a Post-Process"
//     (Kenny Mitchell, Electronic Arts) -- the radial summation (Equation 3).
//   - GTAIV.EFLC.FusionFix SunShafts_PS.hlsl  -- real constants:
//       NUM_SAMPLES = 24, the deltaTexCoord / illuminationDecay march, and the
//       EdgeFade screen-border guard.
//   - LibertyRecomp sunshafts_radial_ps.hlsl / sunshafts_composite_ps.hlsl
//     -- density/decay/exposure/weight layout and the additive tint composite.
//
// The input s0 is expected to already contain the SKY-MASK + SUN-DISK extracted
// bright pixels (the "prepass" result): dark everywhere occluders/geometry are,
// bright only in the sky toward the sun. This shader only marches that mask
// radially toward the sun and tints it.
//
// Register map:
//   s0      : sky-mask + sun-disk input (prepass / extraction result).
//   c0.xy   : sun screen-space position (UV, 0..1). c0.zw reserved (0).
//   c1      : (density, decay, exposure, weight)
//               density  -- ray length / sampling spread (e.g. 0.84)
//               decay    -- per-sample falloff multiplier (e.g. 0.95)
//               exposure -- final brightness scale (e.g. 0.0034..1.0)
//               weight   -- per-sample weight (e.g. 1.0)
//   c2.rgb  : composite tint (sun colour). c2.a = overall intensity (1.0).
//
// NOTE: intended for an ADDITIVE blend pass (src=ONE, dst=ONE), like GodRays.
// It emits ONLY the tinted shaft contribution. If instead you want it written
// straight into the frame, sample the scene in a companion pass and add this.

sampler2D shaftTex : register(s0);

uniform float4 sunPos    : register(c0); // (sunScreenX, sunScreenY, 0, 0)
uniform float4 shaftParam: register(c1); // (density, decay, exposure, weight)
uniform float4 shaftTint : register(c2); // (tintR, tintG, tintB, intensity)

// Fixed march length (FusionFix NUM_SAMPLES = 24). SM3.0 wants a compile-time
// loop bound; the count is intentionally static here.
#define SHAFT_SAMPLES 24
// Screen-border fade gain: keeps the border texel from smearing into streaks.
#define SHAFT_EDGE_FADE 12.0

// Rec.709 luma (used only for a cheap bright-pass gate on the input mask).
inline float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texCoord = IN.texCoord;
    float2 sunScreenPos = sunPos.xy;

    // --- Unpack parameters ---
    float density  = shaftParam.x;
    float decay    = shaftParam.y;
    float exposure = shaftParam.z;
    float weight   = shaftParam.w;
    float3 tint    = shaftTint.rgb;
    float tintStrength = shaftTint.a;

    // --- Guards (1e-7 / saturate chains) ---
    density  = max(density, 0.0);
    decay    = max(decay, 0.0);
    weight   = max(weight, 0.0);
    exposure = max(exposure, 0.0);
    tintStrength = max(tintStrength, 0.0);

    // No exposure / intensity => no contribution (emit black for additive).
    if (exposure <= 0.0 || tintStrength <= 0.0)
        return float4(0, 0, 0, 1);

    // Fade the effect as the sun leaves the viewport (keeps borders clean).
    float sunVis = 1.0 - saturate(max(
        abs(sunScreenPos.x - 0.5) - 0.5,
        abs(sunScreenPos.y - 0.5) - 0.5) * 2.0);
    if (sunVis <= 0.0)
        return float4(0, 0, 0, 1);

    // --- Radial march toward the sun (GPU Gems 3 Eq. 3) ---
    // Vector from this pixel toward the light source, scaled by density/samples.
    float2 deltaTexCoord = (texCoord - sunScreenPos) * (density / (float)SHAFT_SAMPLES);

    // Store initial sample at the current pixel.
    float3 color = tex2D(shaftTex, texCoord).rgb;

    // Illumination decay factor.
    float illuminationDecay = 1.0;
    float2 sampleCoord = texCoord;

    for (int i = 0; i < SHAFT_SAMPLES; i++)
    {
        // Step the sample location along the ray toward the sun.
        sampleCoord -= deltaTexCoord;

        // Fade toward screen edges instead of hard-clamping (prevents border
        // texel smearing -- same technique as GodRays.hlsl).
        float2 edge = saturate(min(sampleCoord, 1.0 - sampleCoord) * SHAFT_EDGE_FADE);
        float edgeFade = edge.x * edge.y;

        float2 clampedCoord = clamp(sampleCoord, 0.0, 1.0);
        float3 samp = tex2D(shaftTex, clampedCoord).rgb;

        // Attenuate by decay/weight and accumulate.
        color += samp * (illuminationDecay * weight * edgeFade);

        // Update exponential decay factor.
        illuminationDecay *= decay;
    }

    // --- Composite ---
    color *= exposure * sunVis;

    // Guard luminance so the tint multiply can't resurrect NaN/negative noise.
    color = max(color, 0.0);

    // Apply the sun tint + overall strength. Additive-friendly output.
    color *= tint * tintStrength;

    return float4(color, 1.0);
}
