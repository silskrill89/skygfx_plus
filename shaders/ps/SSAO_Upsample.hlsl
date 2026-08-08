// SSAO_Upsample.hlsl - Bilateral upsample quarter-res SSAO to full-res (ps_3_0)
// Compiled via /E main -> SSAO_Upsample.cso
// Reads the 4 nearest quarter-res SSAO texels and blends them with
// depth-weighted bilateral filtering to produce full-res SSAO output.
//
// Registers:
//   s0: blurred SSAO (quarter-res RGBA8, R=occlusion, G=confidence)
//   s1: depth texture (full-res INTZ/D24S8)
//
// Constants:
//   c0: upsampleParams = {depthThreshold, 0, 0, 0}
//   c1: screenSize = {fullW, fullH, 1/fullW, 1/fullH}
//   c2: quarterSize = {quarterW, quarterH, 1/quarterW, 1/quarterH}

uniform sampler2D ssaoTexture   : register(s0);
uniform sampler2D depthTexture   : register(s1);

uniform float4 upsampleParams : register(c0);  // x=depthThreshold
uniform float4 screenSize     : register(c1);  // x=fullW, y=fullH, z=1/fullW, w=1/fullH
uniform float4 quarterSize    : register(c2);  // x=quarterW, y=quarterH, z=1/quarterW, w=1/quarterH

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
float4 main(PS_INPUT IN) : COLOR
{
    // Compute quarter-res UV from full-res UV
    float2 quarterScale = quarterSize.xy / screenSize.xy;
    float2 quarterUV = IN.texCoord * quarterScale;

    // Full-res center depth
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;

    // Quarter-res texel size
    float2 quarterTexel = quarterSize.zw;

    // Find the 4 nearest quarter-res texel centers
    // texelCenter = floor(quarterUV * quarterSize.xy) + 0.5
    float2 texelPos = quarterUV * quarterSize.xy;
    float2 floorPos = floor(texelPos);
    float2 fracPos = texelPos - floorPos;  // offset within texel [0,1]

    float depthThreshold = upsampleParams.x;

    float totalOcclusion = 0.0;
    float totalWeight = 0.0;
    float totalConfidence = 0.0;

    // Iterate over 4 nearest quarter-res texels (bilinear grid)
    for (int j = 0; j < 2; ++j)
    {
        for (int i = 0; i < 2; ++i)
        {
            // Quarter-res UV of this texel
            float2 sampleQuarterUV = (floorPos + float2(i, j) + 0.5) * quarterTexel;
            sampleQuarterUV = clamp(sampleQuarterUV, float2(0, 0), float2(1, 1));

            // Read quarter-res SSAO
            float4 sampleSSAO = tex2D(ssaoTexture, sampleQuarterUV);
            float sampleOcclusion = sampleSSAO.r;
            float sampleConfidence = sampleSSAO.g;

            // Compute the full-res UV of this quarter texel for depth lookup
            float2 sampleFullUV = sampleQuarterUV / quarterScale;
            float sampleDepth = tex2D(depthTexture, sampleFullUV).r;

            // Bilinear weight: distance from full-res pixel to this quarter texel center
            float2 bilinearOffset = fracPos - float2(i, j);
            float bilinearWeight = (1.0 - abs(bilinearOffset.x)) * (1.0 - abs(bilinearOffset.y));

            // Bilateral (depth) weight
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * depthDiff / (depthThreshold * depthThreshold));

            float weight = bilinearWeight * depthWeight;
            totalOcclusion += sampleOcclusion * weight;
            totalConfidence += sampleConfidence * weight;
            totalWeight += weight;
        }
    }

    // Normalize
    float upsampledOcclusion = totalOcclusion / max(totalWeight, 1e-7);
    float upsampledConfidence = totalConfidence / max(totalWeight, 1e-7);

    return float4(upsampledOcclusion, upsampledConfidence, 0.0, 1.0);
}
