// SMAA Pass 0: Combined Luma + Depth Edge Detection (ps_3_0)
// Detects edges using both luminance and depth buffer
// Catches color edges AND geometric edges for better coverage
//
// Constants:
//   c0   = (lumaThreshold, depthThreshold, 0, 0)
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = color texture (front buffer)
//   s1 = depth texture (INTZ or packed depth)

sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 edgeParams : register(c0);
uniform float4 screenSize : register(c1);

#define SMAA_RT_METRICS float4(screenSize.zw, screenSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#define SMAA_THRESHOLD edgeParams.x
#define SMAA_DEPTH_THRESHOLD edgeParams.y

#include "SMAA.hlsli"

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;

    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;

    // Luma edge detection
    float2 lumaEdges = SMAALumaEdgeDetectionPS(texcoord, offset, colorTex);

    // Depth edge detection
    float2 depthEdges = SMAADepthEdgeDetectionPS(texcoord, offset, depthTex);

    // Combine: take the maximum of both edge maps
    float2 edges = max(lumaEdges, depthEdges);

    return float4(edges, 0.0, 1.0);
}
