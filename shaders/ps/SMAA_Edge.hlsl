// SMAA Pass 0: Luma Edge Detection (ps_3_0)
// Detects edges using luminance with local contrast adaptation
// Outputs edge map (RG = horizontal,vertical edges)
// Reusable for SSAO edge-aware blur passes
//
// Constants:
//   c0.x = threshold (default 0.1)
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)

sampler2D colorTex : register(s0);

uniform float4 edgeParams : register(c0);
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

    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;

    float2 edges = SMAALumaEdgeDetectionPS(texcoord, offset, colorTex);

    return float4(edges, 0.0, 1.0);
}
