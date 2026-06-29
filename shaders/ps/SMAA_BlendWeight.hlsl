// SMAA Pass 1: Blending Weight Calculation (ps_3_0)
// For each edge pixel, searches along the edge to find length,
// looks up area texture for optimal blending weights
//
// Constants:
//   c0   = (subsampleIndex, maxSearchSteps, 0, 0)
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = edge map from pass 0 (RG)
//   s1 = area texture (160x560, A8L8)
//   s2 = search texture (66x33, L8)

sampler2D edgesTex  : register(s0);
sampler2D areaTex   : register(s1);
sampler2D searchTex : register(s2);

uniform float4 passParams  : register(c0); // x=subsampleIndex, y=maxSearchSteps
uniform float4 screenSize  : register(c1);

#define SMAA_RT_METRICS float4(screenSize.zw, screenSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#define SMAA_MAX_SEARCH_STEPS int(passParams.y)

#include "SMAA.hlsli"

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;
    float2 pixcoord = texcoord * SMAA_RT_METRICS.zw;

    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-0.25, -0.125, 1.25, -0.125) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4(-0.125, -0.25, -0.125, 1.25) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xxyy * float4(-2.0, 2.0, -2.0, 2.0) * passParams.y
              + float4(offset[0].xz, offset[1].yw);

    float4 subsampleIndices = float4(passParams.x, 0.0, 0.0, 0.0);

    return SMAABlendingWeightCalculationPS(
        texcoord, pixcoord, offset,
        edgesTex, areaTex, searchTex,
        subsampleIndices);
}
