sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

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
	float4 diff = tex2D(tex0, IN.texcoord0) * IN.color;
	float4 env = tex2D(tex1, IN.texcoord0);
	float4 color = lerp(diff, env * IN.envColor, IN.envColor.a) + diff;
	color.a = diff.a;
	return color;
}
