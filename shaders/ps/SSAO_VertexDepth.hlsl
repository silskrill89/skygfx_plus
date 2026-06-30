// Improved SSAO with Vertex AO + Depth Buffer (ps_3_0)
// Combines screen-space AO with San Andreas vertex-baked AO
// Uses the depth buffer from SMAA edge detection
//
// Constants:
//   c0 = (radius, power, noiseScale, vertexAOStrength)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (projInfo.x, projInfo.y, projInfo.z, projInfo.w)
//
// Textures:
//   s0 = depth buffer (from SMAA edge detection, A channel)
//   s1 = noise texture (random rotation)
//   s2 = vertex color texture (AO baked in alpha)

sampler2D depthTex    : register(s0);
sampler2D noiseTex    : register(s1);
sampler2D vertexAOTex : register(s2);

uniform float4 ssaoParams  : register(c0); // x=radius, y=power, z=noiseScale, w=vertexAOStrength
uniform float4 screenSize  : register(c1); // xy=resolution, zw=1/resolution
uniform float4 projInfo    : register(c2); // projection matrix info

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float3 GetViewPos(float2 texCoord, float depth)
{
    float2 ndc = texCoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Sample depth buffer
    float centerDepth = tex2D(depthTex, tex).r;
    
    // Early out for sky/background
    if(centerDepth >= 0.999)
        return float4(1, 1, 1, 1);
    
    // Reconstruct view-space position
    float3 centerPos = GetViewPos(tex, centerDepth);
    
    // Sample vertex AO (San Andreas bakes AO into vertex alpha)
    float vertexAO = tex2D(vertexAOTex, tex).a;
    vertexAO = lerp(1.0, vertexAO, ssaoParams.w); // Blend with vertex AO strength
    
    // Generate random rotation from noise texture
    float2 noiseScale = ssaoParams.z * screenSize.xy;
    float3 rand = tex2D(noiseTex, tex * noiseScale).rgb * 2.0 - 1.0;
    
    float occlusion = 0.0;
    float radius = ssaoParams.x;
    float power = ssaoParams.y;
    
    // 8-sample kernel (balanced quality/performance)
    const int numSamples = 8;
    float3 kernel[8];
    
    // Generate hemisphere kernel
    for(int i = 0; i < numSamples; i++)
    {
        float3 sample = float3(
            rand.x * 2.0 - 1.0,
            rand.y * 2.0 - 1.0,
            rand.z * 2.0 - 1.0
        );
        sample = normalize(sample);
        sample *= rand.x * radius;
        kernel[i] = sample;
        rand = tex2D(noiseTex, float2(i * 0.1, 0.0)).rgb * 2.0 - 1.0;
    }
    
    // Sample and accumulate occlusion
    for(int i = 0; i < numSamples; i++)
    {
        float3 samplePos = centerPos + kernel[i];
        
        // Project sample position to screen space
        float2 sampleCoord = samplePos.xy / samplePos.z;
        sampleCoord = (sampleCoord + 1.0) * 0.5;
        sampleCoord.y = 1.0 - sampleCoord.y; // Flip Y
        
        // Check bounds
        if(sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
           sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTex, sampleCoord).r;
            float3 sampleViewPos = GetViewPos(sampleCoord, sampleDepth);
            
            // Range check and occlusion test
            float diff = length(sampleViewPos - centerPos);
            float rangeCheck = smoothstep(0.0, radius, diff);
            occlusion += rangeCheck * step(sampleViewPos.z, centerPos.z);
        }
    }
    
    // Normalize and apply power
    occlusion = 1.0 - (occlusion / float(numSamples));
    occlusion = pow(occlusion, power);
    
    // Combine with vertex AO
    occlusion *= vertexAO;
    
    return float4(occlusion, occlusion, occlusion, 1.0);
}
