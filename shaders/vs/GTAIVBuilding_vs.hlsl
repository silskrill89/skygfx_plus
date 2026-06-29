float4x4	combined	: register(c0);
float3		ambient		: register(c4);
float3		directCol[7]	: register(c5);
float3		directDir[7]	: register(c12);
float4		matCol		: register(c19);
float4		surfProps	: register(c20);
float4		fxParams	: register(c30);
float4		envXform	: register(c37);
float3x3	envmat		: register(c38);
float4		eye		: register(c34);

#define surfAmb		(surfProps.x)
#define surfDiff	(surfProps.z)
#define shininess	(fxParams.x)
#define fresnel		(fxParams.y)

struct VS_INPUT {
	float4 Position	: POSITION;
	float3 Normal	: NORMAL;
	float2 Texcoord0: TEXCOORD0;
	float4 Color	: COLOR0;
};

struct VS_OUTPUT {
	float4 Position		: POSITION;
	float2 Texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
	float4 Color		: COLOR0;
	float4 EnvColor		: COLOR1;
};

VS_OUTPUT main(VS_INPUT IN)
{
	VS_OUTPUT OUT;

	OUT.Position = mul(IN.Position, combined);
	OUT.Texcoord0 = IN.Texcoord0;

	OUT.Color = float4(IN.Color.rgb * surfDiff, 1.0);
	OUT.Color.xyz += ambient * surfAmb;
	for(int i = 0; i < 7; i++){
		float l = max(0.0, dot(IN.Normal, -directDir[i]));
		OUT.Color.xyz += l * directCol[i] * surfDiff;
	}
	OUT.Color = clamp(OUT.Color, 0.0, 1.0);
	OUT.Color *= matCol;

	float3 WorldNormal = normalize(mul(envmat, IN.Normal));
	float3 WorldPos = mul(envmat, IN.Position.xyz);
	float3 ViewVector = normalize(eye.xyz - WorldPos);

	OUT.WorldPos = WorldPos;
	OUT.WorldNormal = WorldNormal;

	float3 envNormal = mul(IN.Normal, (float3x3)envmat);
	OUT.Texcoord0 = envNormal.xy * envXform.zw;

	float b = 1.0 - saturate(dot(-ViewVector, WorldNormal));
	OUT.EnvColor = lerp(1.0, b*b*b*b*b, fresnel) * shininess;

	return OUT;
}
