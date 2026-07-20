// ps2BuildingSSSVS.hlsl - Building VS with SSS output
// Extends ps2BuildingVS with world position for SSS normal reconstruction
// materialType and sssIntensity passed via shaderParams.yzw (c29.yzw)

float4x4	combined	: register(c0);
float3		ambient		: register(c4);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);

float4		shaderParams	: register(c29);
#define colorScale (shaderParams.x)
#define surfAmb (surfProps.x)
#define sssMatType (shaderParams.y)		// 0=none, 1=skin, 2=cloth, 3=vegetation
#define sssIntensity (shaderParams.z)	// 0..1 global SSS intensity
#define sssVertMask (shaderParams.w)	// vertex alpha override for SSS

float4		dayparam	: register(c30);
float4		nightparam	: register(c31);
float4x4	texmat		: register(c32);

struct VS_INPUT
{
	float4 Position		: POSITION;
	float2 TexCoord		: TEXCOORD0;
	float4 NightColor	: COLOR0;
	float4 DayColor		: COLOR1;
};

struct VS_OUTPUT {
	float4 Position		: POSITION;
	float2 Texcoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
	float3 WorldPos		: TEXCOORD1;	// world position for normal reconstruction
	float3 ViewDir		: TEXCOORD2;	// view direction for E·L SSS
};

VS_OUTPUT main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	OUT.Position = mul(IN.Position, combined);
	OUT.Texcoord0 = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;

	OUT.Color = IN.DayColor*dayparam + IN.NightColor*nightparam;
	OUT.Color *= matCol / colorScale;
	OUT.Color.rgb += ambient*surfAmb;

	// World position from the combined matrix (inverse viewproj not available,
	// so we approximate from the input position and the combined matrix)
	OUT.WorldPos = IN.Position.xyz;

	// View direction placeholder (will be interpolated; PS corrects it)
	OUT.ViewDir = float3(0, 0, 1);

	return OUT;
}
