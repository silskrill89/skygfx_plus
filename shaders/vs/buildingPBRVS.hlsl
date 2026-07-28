// buildingPBRVS.hlsl - PBR vertex shader for buildings
// Outputs world-space vectors matching VehiclePBR_Modern PS_INPUT:
//   TEXCOORD0 = texture UV
//   TEXCOORD1 = WorldNormal (float3)
//   TEXCOORD2 = WorldPos (float3)
//   TEXCOORD3 = ViewDir (float3)
//   TEXCOORD4 = SunDir (float3)
//   COLOR0 = vertex color (day/night blended, lit)
//   COLOR1 = envColor (Fresnel + shininess)
//
// Register layout matches buildingPBR callback uploads in buildingPipe.cpp

float4x4	combined	: register(c0);     // WVP matrix
float3		ambient		: register(c4);
float3		directCol[7]: register(c5);
float3		directDir[7]: register(c12);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);
float4		shaderParams	: register(c29);
float4		dayparam	: register(c30);
float4		nightparam	: register(c31);
float4x4	texmat		: register(c32);    // c32-c35
float4x4	worldMat		: register(c24);    // World matrix (c24-c27)
float3		eyePos		: register(c36);    // Camera position (c36, free — avoids texmat conflict)

#define surfAmb  (surfProps.x)
#define surfDiff (surfProps.z)

struct VS_INPUT
{
	float4 Position		: POSITION;
	float3 Normal		: NORMAL;
	float2 TexCoord		: TEXCOORD0;
	float4 NightColor	: COLOR0;
	float4 DayColor		: COLOR1;
};

struct VS_OUTPUT {
	float4 Position		: POSITION;
	float2 Texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
	float3 ViewDir		: TEXCOORD3;
	float3 SunDir		: TEXCOORD4;
	float4 Color		: COLOR0;
	float4 Envcolor		: COLOR1;
};

VS_OUTPUT main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	// Clip-space position
	OUT.Position = mul(IN.Position, combined);

	// Texture UV (from texmat, same as xboxBuildingVS)
	OUT.Texcoord0 = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;

	// World-space position and normal
	float4 worldPos = mul(IN.Position, worldMat);
	float3 worldNormal = mul(IN.Normal, (float3x3)worldMat);
	float worldNormalLen = length(worldNormal);
	worldNormal = worldNormalLen > 1e-6 ? worldNormal / worldNormalLen : float3(0, 1, 0);

	OUT.WorldPos = worldPos.xyz;
	OUT.WorldNormal = worldNormal;

	// View direction (from surface to camera)
	float3 viewVec = eyePos - worldPos.xyz;
	float viewLen = length(viewVec);
	OUT.ViewDir = viewLen > 1e-6 ? viewVec / viewLen : float3(0, 0, 1);

	// Sun direction (main directional light, from c12)
	float sunLen = length(directDir[0]);
	OUT.SunDir = sunLen > 1e-6 ? -directDir[0] / sunLen : float3(0, 0, -1);

	// Vertex color: day/night blend + lighting (same as xboxBuildingVS)
	float4 prelight = IN.DayColor * dayparam + IN.NightColor * nightparam;
	OUT.Color = (prelight * surfDiff + float4(ambient, 0.0) * surfAmb) * matCol;

	// Env/Fresnel params for PBR specular
	float3 V = normalize(eyePos - worldPos.xyz);
	float b = 1.0 - saturate(dot(-V, worldNormal));
	OUT.Envcolor = float4(b, b, b, 1.0);

	return OUT;
}
