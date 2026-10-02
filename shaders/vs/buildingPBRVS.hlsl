// buildingPBRVS.hlsl - PBR vertex shader for buildings
// Outputs world-space vectors matching VehiclePBR_Modern PS_INPUT_BUILDING:
//   TEXCOORD0 = texture UV
//   TEXCOORD1 = WorldNormal (float3)
//   TEXCOORD2 = WorldPos (float3)
//   TEXCOORD3 = ViewDir (float3)
//   TEXCOORD4 = SunDir (float3)
//   TEXCOORD5 = camera-space normal (float3) — env UV basis, PORTED FROM
//               GTAIVBuilding_vs (see below). MUST stay in sync with
//               PS_INPUT_BUILDING in VehiclePBR_Modern.hlsl or the two halves
//               of the PBR building path read different interpolators.
//   COLOR0 = vertex color (day/night blended, lit)
//   COLOR1 = envColor (Fresnel + shininess)
//
// Register layout matches buildingPipe.cpp's PBR callback uploads.

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
float4x4	worldMat	: register(c24);    // World matrix (c24-c27)
float3		eyePos		: register(c36);    // Camera position (c36, free — avoids texmat conflict)
// object -> camera 3x3, uploaded by EVERY building cb (PS2/Xbox/PBR) as
// REG_envmat = c38-c40 from CustomBuildingEnvMapPipeline__SetupEnv. The PS2
// and Xbox cbs already feed it to their FX vertex shaders; the PBR cb
// uploaded it too but until now nothing read it.
float3x3	envmat		: register(c38);

#define surfAmb  (surfProps.x)
// c20 packed by buildingPipe.cpp (PBR cb): x = ambient scale, y = dynamic
// light scale, z = prelight scale. SA road/terrain/vegetation materials ship
// diffuse=0 with lighting baked into vertex prelight; the CPU fallback boosts
// ONLY z (prelight passes at full) and leaves y at the raw diffuse (~0) so the
// live 7-light sum cannot stack on top of already-baked lighting (2x energy =
// white blown-out ground).
#define surfLightScale (surfProps.y)
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
	float3 CamNormal	: TEXCOORD5;
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

	// Camera-space normal — PORTED FROM GTAIVBuilding_vs:83
	// (`OUT.Texcoord1 = mul(envmat, IN.Normal)`; its PS then builds the env
	// UV as `uv = n.xy * 0.5 + 0.5`). envmat is object->camera and IN.Normal
	// is object-space, so this is a single mul. Guarded against a degenerate
	// matrix (identity/zero rows when there is no camera) so the UV can never
	// come out of a NaN basis.
	float3 camNormal = mul(envmat, IN.Normal);
	float camNormLen = length(camNormal);
	OUT.CamNormal = camNormLen > 1e-6 ? camNormal / camNormLen : float3(0, 0, 1);

	// Vertex color: baked day/night prelight + LIVE timecycle sun (mirrors vehicleVS).
	// directDir is WORLD-space here (buildingPipe.cpp pipeUploadLightDirectionForce),
	// so NdotL uses worldNormal, not the object-space IN.Normal. saturate() is required
	// now that sun is added (sunlit faces can otherwise exceed 1.0).
	float4 prelight = IN.DayColor * dayparam + IN.NightColor * nightparam;
	OUT.Color = prelight * surfDiff;
	OUT.Color.xyz += ambient * surfAmb;
	for (int i = 0; i < 7; i++){
		float l = max(0.0, dot(worldNormal, -directDir[i]));
		// Dynamic lights scale by surfLightScale (y), NOT surfDiff: for
		// baked-lighting materials (diffuse=0) the CPU sends y~=0, so the
		// live light sum stays off while prelight passes at full strength.
		OUT.Color.xyz += l * directCol[i] * surfLightScale;
	}
	OUT.Color = saturate(OUT.Color) * matCol;

	// Env/Fresnel params for PBR specular
	float3 V = normalize(eyePos - worldPos.xyz);
	float b = 1.0 - saturate(dot(-V, worldNormal));
	// Pass wet roads value (dayparam.w = CWeather__WetRoads) to pixel shader
	// via Envcolor alpha. Pixel shader reads IN.dayNight.a for wet surface effects.
	OUT.Envcolor = float4(b, b, b, dayparam.w);

	return OUT;
}
