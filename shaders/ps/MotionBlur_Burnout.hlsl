// Burnout Paradise-Style Motion Blur (ps_3_0)
// Inspired by GTA V's directional motion blur with jitter and velocity clamping
//
// Constants:
//   c0 = (blurStrength, radialStrength, maxSamples, speedFactor)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (cameraVelocity, cameraRotation, deltaTime, 0)
//
// Textures:
//   s0 = current frame
//   s1 = motion buffer (RG=motion XY, B=motion magnitude, A=depth)

sampler2D currentTex : register(s0);
sampler2D motionTex  : register(s1);

uniform float4 blurParams    : register(c0);
uniform float4 screenSize    : register(c1);
uniform float4 cameraParams  : register(c2);

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Simple noise for jitter (reduces banding)
float2 Jitter(float2 tex, float2 color)
{
    // Magic offset from GTA V's jitter formula
    const float2 JitterOffset = { 58.164f, 47.13f };
    float2 jitterLookup = tex * JitterOffset + (color * 8.0f);
    // Simple hash-based noise
    float noise = frac(sin(dot(jitterLookup, float2(12.9898, 78.233))) * 43758.5453);
    return float2(noise - 0.5, noise - 0.5);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    float2 center = float2(0.5, 0.5);
    
    // Sample current pixel
    float3 color = tex2D(currentTex, tex).rgb;
    
    // Sample motion buffer
    float4 motionData = tex2D(motionTex, tex);
    float2 motionVector = motionData.rg * 2.0 - 1.0; // Unpack from [0,1] to [-1,1]
    float motionMagnitude = motionData.b;
    
    // Calculate radial vector from screen center (vanishing point)
    float2 radialVec = tex - center;
    float radialDist = length(radialVec);
    float2 radialDir = radialVec / max(radialDist, 0.001);
    
    // Combine motion vector with radial blur (Burnout style)
    // At screen center, use mostly motion vector
    // At edges, blend in radial component
    float radialBlend = saturate(radialDist * 2.0);
    float2 blurDir = lerp(motionVector, radialDir, radialBlend * blurParams.y);
    
    // Calculate blur amount based on:
    // 1. Motion magnitude from buffer
    // 2. Camera velocity (global movement)
    // 3. Speed factor from config
    float blurAmount = motionMagnitude * blurParams.x;
    blurAmount += cameraParams.x * blurParams.w; // Camera velocity contribution
    blurAmount = min(blurAmount, blurParams.z * 0.01); // Clamp to max blur
    
    // Early exit if no blur needed (GTA V technique: discard when velocity near zero)
    if(blurAmount < 0.001)
        return float4(color, 1.0);
    
    // Normalize blur direction
    float2 blurStep = normalize(blurDir) * blurAmount * pixel;
    
    // Apply jitter to reduce banding (GTA V technique)
    float2 jitterOffset = Jitter(tex, color.rg);
    tex += blurStep * jitterOffset * 0.5;
    
    // Multi-sample blur with chromatic aberration (8 samples unrolled)
    float3 result = float3(0, 0, 0);
    float totalWeight = 0;
    
    // Center sample (highest weight)
    float centerWeight = 1.0;
    result += color * centerWeight;
    totalWeight += centerWeight;
    
    // Directional samples with chromatic aberration
    for(int i = 1; i <= 8; i++)
    {
        float t = (float)i / 8.0;
        float2 offset = blurStep * t;
        
        // Sample with slight RGB separation for chromatic aberration
        float2 sampleCoordR = tex + offset * 1.0;
        float2 sampleCoordG = tex + offset * 0.98;
        float2 sampleCoordB = tex + offset * 0.96;
        
        float3 sampleR = tex2D(currentTex, sampleCoordR).rgb;
        float3 sampleG = tex2D(currentTex, sampleCoordG).rgb;
        float3 sampleB = tex2D(currentTex, sampleCoordB).rgb;
        
        // Combine with per-channel separation
        float3 sample = float3(sampleR.r, sampleG.g, sampleB.b);
        
        // Weight falloff (sharper center, softer edges)
        float weight = 1.0 - t;
        weight *= weight; // Quadratic falloff
        
        result += sample * weight;
        totalWeight += weight;
    }
    
    // Normalize
    result /= totalWeight;
    
    // Add slight brightness boost for high-speed (Burnout style)
    float speedBoost = saturate(blurAmount * 10.0);
    result *= (1.0 + speedBoost * 0.1);
    
    return float4(result, 1.0);
}
