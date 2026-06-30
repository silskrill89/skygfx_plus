// Subsurface Scattering Blur (ps_3_0)
// Separable Gaussian bilateral blur for skin SSS
// Inspired by CryEngine's SSSSS implementation
//
// Constants:
//   c0 = (blurDirection.x, blurDirection.y, sssRadius, sssStrength)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (depthThreshold, nearPlane, farPlane, 0)
//
// Textures:
//   s0 = scene color (HDR)
//   s1 = depth buffer
//   s2 = normal buffer (from GBuffer if available)

sampler2D colorTex  : register(s0);
sampler2D depthTex  : register(s1);
sampler2D normalTex : register(s2);

uniform float4 blurParams  : register(c0); // xy=direction, z=radius, w=strength
uniform float4 screenSize  : register(c1);
uniform float4 depthParams : register(c2); // x=threshold, y=near, z=far

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float GetLinearDepth(float2 uv)
{
    float depth = tex2D(depthTex, uv).r;
    // Convert from [0,1] to linear depth
    float near = depthParams.y;
    float far = depthParams.z;
    return near * far / (far - depth * (far - near));
}

float3 GetViewPos(float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc * depth, depth);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Center sample
    float3 centerColor = tex2D(colorTex, tex).rgb;
    float centerDepth = GetLinearDepth(tex);
    
    // Skin profile: red channel scatters more (variance 3.3), green less (1.4), blue least (1.1)
    // CryEngine uses: float3(3.3, 1.4, 1.1) for skin
    float3 blurFalloff = -1.0 / (2.0 * float3(3.3, 1.4, 1.1));
    
    // CDF-optimal offsets for 7 taps
    float offsets[6] = { 0.352, 0.719, 1.117, 1.579, 2.177, 3.213 };
    
    float3 totalWeight = 1.0;
    float3 colorSum = centerColor;
    
    float radius = blurParams.z;
    float2 blurDir = blurParams.xy * radius * pixel;
    
    // Two-sided 6-tap Gaussian bilateral filter
    [unroll]
    for(int i = 0; i < 2; i++)
    {
        [unroll]
        for(int j = 0; j < 6; j++)
        {
            float2 uv = tex + offsets[j] / 5.5 * blurDir;
            
            float sampleDepth = GetLinearDepth(uv);
            float3 sampleColor = tex2D(colorTex, uv).rgb;
            
            // Depth-aware weight (bilateral)
            float depthDiff = abs(sampleDepth - centerDepth) * 1000.0; // mm
            float depthWeight = exp(-depthDiff * depthDiff / (2.0 * depthParams.x * depthParams.x));
            
            // Spatial weight (Gaussian)
            float spatialWeight = exp(-(offsets[j] * offsets[j]) * blurFalloff.x);
            
            float3 weight = depthWeight * spatialWeight;
            totalWeight += weight;
            colorSum += weight * sampleColor;
        }
        blurDir = -blurDir; // Flip direction for other side
    }
    
    float3 result = colorSum / totalWeight;
    
    // Blend with original based on strength
    result = lerp(centerColor, result, blurParams.w);
    
    return float4(result, 1.0);
}
