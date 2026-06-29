uniform sampler2D tex0 : register(s0);
uniform sampler2D tex1 : register(s1);
uniform sampler2D tex2 : register(s2);

float4		surfProps	: register(c0);
float4		fxParams	: register(c1);
float3		eye		: register(c2);
float4		directCol	: register(c5);
float4		lightCol[6]	: register(c6);
float3		directDir	: register(c12);
float3		lightDir[6]	: register(c13);

#define power		(fxParams.y)
#define lightmult	(fxParams.z)
#define surfSpec	(surfProps.z)

struct PS_INPUT
{
	float3 texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
	float4 color		: COLOR0;
	float4 envColor		: COLOR1;
};

float specTerm(float3 reflVec, float3 light, float pwr)
{
    float dp = max(dot(reflVec, light), 0.0);
    // Simplified pow for ps_2_0
    return dp * dp * dp * dp * dp * dp * dp * dp; // approx power of 8
}

float4 main(PS_INPUT IN) : COLOR
{
	float3 ViewVector = normalize(IN.WorldPos - eye);
	float3 ReflVector = normalize(ViewVector - 2.0*dot(ViewVector, IN.WorldNormal)*IN.WorldNormal);
	
	float4 spec = directCol * specTerm(ReflVector, -directDir, power);
	
	// Combine lights to reduce instruction count
	float3 avgLightDir = (lightDir[0] + lightDir[1] + lightDir[2] + lightDir[3] + lightDir[4] + lightDir[5]) * (1.0/6.0);
	float4 avgLightCol = (lightCol[0] + lightCol[1] + lightCol[2] + lightCol[3] + lightCol[4] + lightCol[5]) * (1.0/6.0);
	spec += avgLightCol * specTerm(ReflVector, -avgLightDir, power*2) * 4.8;
	
	spec *= surfSpec;

	float4 diff = tex2D(tex0, IN.texcoord0.xy);
	float4 mask = tex2D(tex2, ReflVector.xy*0.5 + 0.5);
	float2 ReflPos = normalize(ReflVector.xy) * (ReflVector.z*0.5 + 0.5);
	ReflPos = ReflPos*float2(0.5, -0.5) + float2(0.5, 0.5);
	float4 env = tex2D(tex1, ReflPos)*lightmult*2;

	float4 diffpass = diff*IN.color;
	float4 envpass = env*mask;
	float4 specpass = spec;
	float4 final = lerp(diffpass, envpass, IN.envColor) + specpass;
	final.a = diffpass.a;
	return final;
}