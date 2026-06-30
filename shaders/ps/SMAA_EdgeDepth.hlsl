// SMAA Enhanced Edge Detection (ps_3_0)
// Combines luma edges with depth discontinuities
// Catches geometric edges on flat-colored surfaces
//
// Constants:
//   c0 = (lumaThreshold, depthThreshold, 0, 0)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = color texture (front buffer)
//   s1 = depth texture (INTZ or packed)

sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 edgeParams : register(c0); // x=lumaThreshold, y=depthThreshold
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

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Luma edge detection (from SMAA.hlsli)
    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;
    float2 lumaEdges = SMAALumaEdgeDetectionPS(texcoord, offset, colorTex);
    
    // Depth edge detection (geometric edges)
    float depthC = tex2D(depthTex, texcoord).r;
    float depthL = tex2D(depthTex, texcoord + float2(-pixel.x, 0)).r;
    float depthR = tex2D(depthTex, texcoord + float2( pixel.x, 0)).r;
    float depthU = tex2D(depthTex, texcoord + float2(0, -pixel.y)).r;
    float depthD = tex2D(depthTex, texcoord + float2(0,  pixel.y)).r;
    
    // Calculate depth gradients
    float depthDX = abs(depthR - depthL);
    float depthDY = abs(depthD - depthU);
    
    // Depth edge detection with threshold
    float depthThreshold = edgeParams.y;
    float2 depthEdges = float2(
        step(depthThreshold, depthDX),
        step(depthThreshold, depthDY)
    );
    
    // Combine: take maximum of luma and depth edges
    // This catches both color and geometric edges
    float2 edges = max(lumaEdges, depthEdges);
    
    return float4(edges, 0.0, 1.0);
}
