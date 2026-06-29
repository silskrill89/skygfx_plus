// Combined Edge + Motion + Depth Detection (ps_3_0)
// Single pass that outputs data for both SMAA and SSAO
// RG = luma edges (SMAA blend weights)
// B = motion edge (temporal stabilization)
// A = packed depth (SSAO occlusion input)
//
// Constants:
//   c0 = (lumaThreshold, motionThreshold, motionScale, depthScale)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (projInfo.x, projInfo.y, projInfo.z, projInfo.w) - for depth reconstruction
//
// Textures:
//   s0 = current frame (front buffer)
//   s1 = previous frame
//   s2 = depth buffer (INTZ or packed)

sampler2D currentTex : register(s0);
sampler2D prevTex    : register(s1);
sampler2D depthTex   : register(s2);

uniform float4 edgeParams : register(c0); // x=lumaThresh, y=motionThresh, z=motionScale, w=cameraMovement
uniform float4 screenSize : register(c1);
uniform float4 projInfo   : register(c2);

#define SMAA_RT_METRICS float4(screenSize.zw, screenSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#define SMAA_THRESHOLD edgeParams.x

#include "SMAA.hlsli"

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float PackDepth(float depth)
{
    // Pack depth into [0,1] range for storage in alpha channel
    return saturate(depth * edgeParams.w);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // ===== Luma Edge Detection (RG) =====
    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;
    float2 lumaEdges = SMAALumaEdgeDetectionPS(texcoord, offset, currentTex);
    
    // ===== Motion Detection (B) =====
    // Camera movement increases threshold to reduce jitter
    float dynamicMotionThresh = edgeParams.y + (edgeParams.w * 0.5f); // base + camera movement
    
    float3 current = tex2D(currentTex, texcoord).rgb;
    float3 prev = tex2D(prevTex, texcoord).rgb;
    float motion = abs(Luma(current) - Luma(prev)) * edgeParams.z;
    
    // Check neighbors for motion gradient
    float3 prevN = tex2D(prevTex, texcoord + float2(0, -pixel.y)).rgb;
    float3 prevS = tex2D(prevTex, texcoord + float2(0, pixel.y)).rgb;
    float3 prevE = tex2D(prevTex, texcoord + float2(pixel.x, 0)).rgb;
    float3 prevW = tex2D(prevTex, texcoord + float2(-pixel.x, 0)).rgb;
    
    float maxMotion = max(motion, max(
        max(abs(Luma(current) - Luma(prevN)), abs(Luma(current) - Luma(prevS))),
        max(abs(Luma(current) - Luma(prevE)), abs(Luma(current) - Luma(prevW)))
    )) * edgeParams.z;
    
    // Apply dynamic threshold based on camera movement
    float motionEdge = saturate(maxMotion / max(0.001f, dynamicMotionThresh));
    
    // ===== Depth Sampling (A) =====
    float depth = tex2D(depthTex, texcoord).r;
    float packedDepth = PackDepth(depth);
    
    // Output: RG=luma edges, B=motion, A=depth
    return float4(lumaEdges, motionEdge, packedDepth);
}
