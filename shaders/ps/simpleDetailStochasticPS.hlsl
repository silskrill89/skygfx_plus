// simpleDetailStochasticPS.hlsl - Stochastic building with detail map (ps_3_0)
// Compiled via /E main -> simpleDetailStochasticPS.cso
// Used by PS2/Xbox building pipes when detail + stochastic sampling are enabled.
float2 hash2D2D_stoch(float2 s)
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

float4 tex2DStochastic(sampler2D tex, float2 UV)
{
    float2 jitter = hash2D2D_stoch(UV * 100.0) * 0.01;
    return tex2D(tex, UV + jitter);
}

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
    float4 final = tex2DStochastic(tex0_detail, IN.texcoord0 * 1.2) * IN.color * colorscale_detail.x;
    final.rgb *= tex2DStochastic(tex2_detail, IN.texcoord0 * detailtile).rgb * 2.0;
    return final;
}
