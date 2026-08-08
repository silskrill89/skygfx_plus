// SSAO_BilateralBlur.hlsl - Quarter-res depth-aware bilateral blur (ps_3_0)
// Compiled via /E main -> SSAO_BilateralBlur.cso
// Single-pass 9-tap bilateral blur to smooth SSAO noise while preserving
// depth edges.  Budget: 9 taps x 2 tex reads = 18 reads, ~30 ALU.
//
// Registers:
//   s0: SSAO temporal output (quarter-res RGBA8, R=occlusion, G=confidence)
//   s1: depth texture (full-res INTZ/D24S8, for edge-aware weighting)
//
// Constants:
//   c0: blurParams = {blurRadius, depthThreshold, 0, 0}
//   c1: screenSize = {quarterW, quarterH, 1/quarterW, 1/quarterH}
//   c2: projInfo (same as SSAO_Temporal — unused here but kept for consistency)

uniform sampler2D ssaoTexture   : register(s0);
uniform sampler2D depthTexture   : register(s1);

uniform float4 blurParams  : register(c0);  // x=blurRadius, y=depthThreshold
uniform float4 screenSize  : register(c1);  // x=quarterW, y=quarterH, z=1/quarterW, w=1/quarterH
uniform float4 projInfo    : register(c2);  // unused, kept for register layout consistency

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// Gaussian weight approximation (linear falloff for SM3.0 efficiency)
// ---------------------------------------------------------------------------
float GaussianWeight(float x, float sigma)
{
    // Exact Gaussian: exp(-x^2 / (2*sigma^2))
    // sigma = blurRadius / 3 for ~95% coverage within the radius
    float s = sigma / 3.0;
    return exp(-0.5 * (x * x) / (s * s));
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
float4 main(PS_INPUT IN) : COLOR
{
    float2 texel = screenSize.zw;  // 1/quarterW, 1/quarterH

    // Center tap
    float4 centerSSAO = tex2D(ssaoTexture, IN.texCoord);
    float centerOcclusion = centerSSAO.r;
    float centerConfidence = centerSSAO.g;

    // Read center depth (quarter-res — same resolution as SSAO texture)
    // We use depth at quarter-res for the bilateral weight since both textures
    // are the same resolution.
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;

    float blurRadius = blurParams.x;
    float depthThreshold = blurParams.y;

    float totalOcclusion = 0.0;
    float totalWeight = 0.0;

    // 9-tap bilateral blur
    for (int i = -4; i <= 4; ++i)
    {
        float2 sampleUV = IN.texCoord + float2(i, 0) * texel;
        sampleUV = clamp(sampleUV, float2(0, 0), float2(1, 1));

        float4 sampleSSAO = tex2D(ssaoTexture, sampleUV);
        float sampleOcclusion = sampleSSAO.r;

        float sampleDepth = tex2D(depthTexture, sampleUV).r;

        // Spatial (Gaussian) weight
        float spatialWeight = GaussianWeight(float(i), blurRadius);

        // Depth (bilateral) weight — preserves edges
        float depthDiff = abs(centerDepth - sampleDepth);
        float depthWeight = exp(-depthDiff * depthDiff / (depthThreshold * depthThreshold));

        float weight = spatialWeight * depthWeight;
        totalOcclusion += sampleOcclusion * weight;
        totalWeight += weight;
    }

    // Normalize
    float blurredOcclusion = totalOcclusion / max(totalWeight, 1e-7);

    return float4(blurredOcclusion, centerConfidence, 0.0, 1.0);
}
