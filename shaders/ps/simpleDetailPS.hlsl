// simpleDetailPS.hlsl - Building with detail map (ps_3_0)
// Compiled via /E main -> simpleDetailPS.cso
// Used by PS2/Xbox building pipes when a detail texture is present.
uniform sampler2D tex0_detail : register(s0);
uniform sampler2D tex1_detail : register(s1);
uniform sampler2D tex2_detail : register(s2);
uniform float4 colorscale_detail : register(c0);
uniform float detailtile : register(c1);

struct PS_INPUT_DETAIL {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
};

float4 main(PS_INPUT_DETAIL IN) : COLOR
{
    float4 final = tex2D(tex0_detail, IN.texcoord0) * IN.color * colorscale_detail.x;
    final.rgb *= tex2D(tex2_detail, IN.texcoord0 * detailtile).rgb * 2.0;
    return final;
}
