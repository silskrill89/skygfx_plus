// SSAO_ps20_depthonly.hlsl - Depth-only pre-pass for SSAO
uniform sampler2D depthTexture : register(s0);

struct PS_INPUT { float2 texCoord : TEXCOORD0; };

float4 main(PS_INPUT IN) : COLOR
{
    float d = tex2D(depthTexture, IN.texCoord).r;
    return float4(d, d, d, 1);
}
