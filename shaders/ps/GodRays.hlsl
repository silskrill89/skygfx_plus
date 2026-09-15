// GodRays.hlsl - Screen-space god rays / crepuscular rays (ps_3_0)
// Radial blur toward sun position for volumetric light ray effect.
//
// IMPORTANT: this pass is drawn with ADDITIVE blending (src=ONE, dst=ONE).
// It must therefore output ONLY the ray contribution. The scene colour is
// already preserved by the blend (dst += src); returning scene + rays here
// would add the whole frame onto itself and blow out every scene.

sampler2D sceneTex : register(s0);

uniform float4 sunPos      : register(c0); // (sunScreenX, sunScreenY, 0, 0)
uniform float4 rayParams   : register(c1); // (exposure, decay, density, weight)
uniform float4 numSamplesP : register(c2); // (numSamples, 0, 0, 0)

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texCoord = IN.texCoord;
    float2 sunScreenPos = sunPos.xy;

    // Parameters
    float exposure = rayParams.x;     // brightness per sample (e.g. 0.0034)
    float decay = rayParams.y;        // falloff per sample (default 1.0)
    float density = rayParams.z;      // ray density (default 0.84)
    float weight = rayParams.w;       // ray weight (default 1.0)
    int numSamples = (int)numSamplesP.x; // number of samples (default 20)

    // Guard against degenerate cases
    numSamples = max(numSamples, 1);
    density = max(density, 0.0);

    // No exposure => no contribution (do not emit scene colour: additive pass).
    if(exposure <= 0.0)
        return float4(0, 0, 0, 0);

    // Direction from current pixel to sun
    float2 deltaTexCoord = (texCoord - sunScreenPos) * density / (float)numSamples;

    // Reduce effect when sun is far outside the viewport
    float sunVis = 1.0 - saturate(max(
        abs(sunScreenPos.x - 0.5) - 0.5,
        abs(sunScreenPos.y - 0.5) - 0.5) * 2.0);
    if(sunVis <= 0.0)
        return float4(0, 0, 0, 0);

    // Radial blur accumulation (rays only)
    float3 result = float3(0, 0, 0);
    float illuminationDecay = 1.0;
    float2 sampleCoord = texCoord;

    // SM3.0 requires compile-time loop bound; runtime break for variable count
    for(int i = 0; i < 20; i++)
    {
        if(i >= numSamples) break;

        sampleCoord -= deltaTexCoord;

        // Fade toward screen edges instead of clamping. Hard-clamping the
        // sample coord smeared the border texel into long bright streaks.
        float2 edge = saturate(min(sampleCoord, 1.0 - sampleCoord) * 12.0);
        float edgeFade = edge.x * edge.y;

        float2 clampedCoord = clamp(sampleCoord, 0.0, 1.0);
        float3 samp = tex2D(sceneTex, clampedCoord).rgb;
        result += samp * (illuminationDecay * weight * edgeFade);
        illuminationDecay *= decay;
    }

    result *= exposure * sunVis;

    // Additive blend: emit the ray contribution only (no scene colour).
    return float4(result, 1.0);
}
