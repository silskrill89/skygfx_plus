// GodRays.hlsl - Screen-space god rays / crepuscular rays (ps_3_0)
// Radial blur toward sun position for volumetric light ray effect

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
    float exposure = rayParams.x;     // brightness per sample (default 0.0034)
    float decay = rayParams.y;        // falloff per sample (default 1.0)
    float density = rayParams.z;      // ray density (default 0.84)
    float weight = rayParams.w;       // ray weight (default 1.0)
    int numSamples = (int)numSamplesP.x; // number of samples (default 20)

    // Guard against degenerate cases
    numSamples = max(numSamples, 1);
    density = max(density, 0.0);

    // Direction from current pixel to sun
    float2 deltaTexCoord = (texCoord - sunScreenPos) * density / (float)numSamples;

    // Reduce effect when sun is far outside [0,1] (behind camera)
    float sunVis = 1.0 - saturate(max(
        abs(sunScreenPos.x - 0.5) - 0.5,
        abs(sunScreenPos.y - 0.5) - 0.5) * 2.0);

    // Original scene color
    float3 color = tex2D(sceneTex, texCoord).rgb;

    // Radial blur accumulation
    float3 result = float3(0, 0, 0);
    float illuminationDecay = 1.0;
    float2 sampleCoord = texCoord;

    // SM3.0 requires compile-time loop bound; runtime break for variable count
    for(int i = 0; i < 20; i++)
    {
        if(i >= numSamples) break;

        sampleCoord -= deltaTexCoord;

        // Clamp sample coord to valid UV range
        float2 clampedCoord = clamp(sampleCoord, 0.0, 1.0);

        float3 samp = tex2D(sceneTex, clampedCoord).rgb;
        samp *= illuminationDecay * weight;
        result += samp;
        illuminationDecay *= decay;
    }

    // Scale by exposure and sun visibility
    result *= exposure * sunVis;

    // Additive blend with original scene
    float3 finalColor = color + result;

    return float4(finalColor, 1.0);
}
