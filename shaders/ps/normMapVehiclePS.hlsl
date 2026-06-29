sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);
sampler2D tex2 : register(s2);

float4		surfProps	: register(c20);
float4		fxParams	: register(c30);

float4		directCol[7]	: register(c5);
float3		directDir[7]	: register(c12);

#define power		(fxParams.y)
#define lightmult	(fxParams.z)
#define surfSpec	(surfProps.z)
#define intensity	(fxParams.w)

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
