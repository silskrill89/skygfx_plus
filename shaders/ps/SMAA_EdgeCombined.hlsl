// SMAA Combined Edge + Motion + Normal Detection (ps_3_0)
// Detects edges using luma, motion between frames, and normal discontinuities
// Outputs packed edge buffer: RG=luma edges, B=motion edge, A=normal edge
//
// Constants:
//   c0   = (lumaThreshold, motionThreshold, normalThreshold, motionScale)
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = current frame (front buffer)
//   s1 = previous frame
//   s2 = normal/depth buffer (optional, can be NULL)

sampler2D currentTex : register(s0);
sampler2D prevTex    : register(s1);
sampler2D normalTex  : register(s2);

uniform float4 edgeParams : register(c0); // x=lumaThresh, y=motionThresh, z=normalThresh, w=motionScale
uniform float4 screenSize : register(c1);

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

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // ===== Luma Edge Detection (RG channels) =====
    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;
    
    float2 lumaEdges = SMAALumaEdgeDetectionPS(texcoord, offset, currentTex);
    
    // ===== Motion Detection (B channel) =====
    float3 current = tex2D(currentTex, texcoord).rgb;
    float3 prev = tex2D(prevTex, texcoord).rgb;
    
    float lumaCurrent = Luma(current);
    float lumaPrev = Luma(prev);
    float motion = abs(lumaCurrent - lumaPrev) * edgeParams.w;
    
    // Check neighbors for motion gradient
    float3 prevN = tex2D(prevTex, texcoord + float2(0, -pixel.y)).rgb;
    float3 prevS = tex2D(prevTex, texcoord + float2(0, pixel.y)).rgb;
    float3 prevE = tex2D(prevTex, texcoord + float2(pixel.x, 0)).rgb;
    float3 prevW = tex2D(prevTex, texcoord + float2(-pixel.x, 0)).rgb;
    
    float motionN = abs(lumaCurrent - Luma(prevN)) * edgeParams.w;
    float motionS = abs(lumaCurrent - Luma(prevS)) * edgeParams.w;
    float motionE = abs(lumaCurrent - Luma(prevE)) * edgeParams.w;
    float motionW = abs(lumaCurrent - Luma(prevW)) * edgeParams.w;
    
    // Use max motion in neighborhood for better edge detection
    float maxMotion = max(motion, max(max(motionN, motionS), max(motionE, motionW)));
    float motionEdge = step(edgeParams.y, maxMotion);
    
    // ===== Normal/Depth Edge Detection (A channel) =====
    float normalEdge = 0.0;
    // Sample normal/depth buffer if available
    // Normal discontinuities create edges even on flat-colored surfaces
    float3 normalC = tex2D(normalTex, texcoord).rgb;
    float3 normalN = tex2D(normalTex, texcoord + float2(0, -pixel.y)).rgb;
    float3 normalE = tex2D(normalTex, texcoord + float2(pixel.x, 0)).rgb;
    
    // Calculate normal difference
    float3 normalDiff = abs(normalC - normalN) + abs(normalC - normalE);
    float normalDelta = dot(normalDiff, float3(1.0, 1.0, 1.0));
    normalEdge = step(edgeParams.z, normalDelta);
    
    // ===== Output packed edge buffer =====
    // RG = luma edges (for SMAA blend weight calculation)
    // B = motion edge (for temporal stabilization)
    // A = normal edge (for geometric edge detection)
    return float4(lumaEdges, motionEdge, normalEdge);
}
