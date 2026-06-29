sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

float4		fogColor : register(c0);

struct PS_INPUT
{
	float2 texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
	float4 color		: COLOR0;
	float4 envColor		: COLOR1;
};

float4 main(PS_INPUT IN) : COLOR
{
	float4 baseColor = tex2D(tex0, IN.texcoord0) * IN.color;

	float3 envColor = tex2D(tex1, IN.WorldNormal.xy * 0.5 + 0.5).rgb;
	baseColor.rgb = lerp(baseColor.rgb, envColor, IN.envColor.a);

	return baseColor;
}
