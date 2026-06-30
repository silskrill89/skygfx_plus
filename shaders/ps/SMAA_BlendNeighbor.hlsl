// SMAA Pass 2: Neighborhood Blending (ps_3_0)
// Blends pixels using the blending weights from pass 1
// Final anti-aliased output
//
// Constants:
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = original color texture (front buffer)
//   s1 = blend weights texture from pass 1 (RGBA)

sampler2D colorTex : register(s0);
sampler2D blendTex : register(s1);

uniform float4 screenSize : register(c1);

#define SMAA_RT_METRICS float4(screenSize.zw, screenSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1

#include "SMAA.hlsli"

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;

    float4 offset = SMAA_RT_METRICS.xyxy * float4(1.0, 0.0, 0.0, 1.0) + texcoord.xyxy;

    return SMAANeighborhoodBlendingPS(texcoord, offset, colorTex, blendTex);
}
