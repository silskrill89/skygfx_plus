// GTAIVBuilding_vs.hlsl - GTA IV building vertex shader.
// Entry: main, profile vs_3_0 -> resources/cso/GTAIVBuilding_vs.cso
// (Resource.rc IDR_GTAIVBUILDINGVS, loaded by pipelinecommon CreateShaders).
//
// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS
// ---------------------------------------------------------------------------
// Until now the only artifact in the tree was the prebuilt
// GTAIVBuilding_vs.cso (shaders/vs + resources/cso) with NO HLSL source, so
// nothing could be audited or recompiled. Disassembling it showed it was out
// of sync with what CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2
// (buildingPipe.cpp) actually uploads:
//
//  1. Diffuse UV: it never read the mesh texcoords. TEXCOORD0 was emitted as
//     `(normal . envmat).xy * envXform.zw` - i.e. the ENVMAP projection - and
//     envXform (VS c37) is only uploaded for env-mapped materials. For every
//     other material c37 was stale/zero, so the UV scale collapsed to 0 and
//     the whole atomic sampled a single texel of its diffuse map: flat,
//     posterised colour.
//  2. Day/night: it read only COLOR0. DNInstance_PS2 writes the NIGHT colour
//     to COLOR0 and the DAY colour to COLOR1, and setDnParams() uploads the
//     blend weights to c30/c31 - registers the old shader never used, so IV
//     buildings were drawn with night prelight at noon.
//  3. c30 was declared as fxParams while the callback fills c30 with
//     setDnParams()'s dayparam, and fxParams actually lives at c36
//     (REG_fxParams). The IV pixel shader reads COLOR1.a as its env-lerp
//     weight, so that weight was being driven by the time-of-day parameters.
//
// This rebuild keeps the exact output contract GTAIVBuilding_ps.cso consumes
//   TEXCOORD0 = diffuse UV          (ps: texld s0)
//   TEXCOORD1 = normal              (ps: uv = v1 * 0.5 + 0.5 -> texld s1)
//   COLOR0    = lit vertex colour   (ps: * tex, drives oC0.a)
//   COLOR1.a  = env lerp weight     (ps: mad oC0, v3.w, env-diffuse, diffuse)
// while taking the lighting/UV path from the proven xboxBuildingVS /
// ps2BuildingVS pair.
//
// Lighting is deliberately PS2/Xbox-equivalent: prelight + ambient only, no
// live 7-light sum. The callback packs c20 as {ambient, diffuse, diffuse, 0}
// (buildingPipe_uploadSurfProps) with a diffuse==0 -> 1.0 fallback, so
// surfDiff reaches the shader as a real, non-zero prelight scale.
// ---------------------------------------------------------------------------

float4x4	combined	: register(c0);     // WVP (c0-c3)
float3		ambient		: register(c4);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);    // x = ambient scale, z = surfDiff
float4		shaderParams	: register(c29);    // colorScale (x) - unused here
float4		dayparam	: register(c30);
float4		nightparam	: register(c31);
float4x4	texmat		: register(c32);    // c32-c35
float3x3	envmat		: register(c38);    // c38-c40 (object -> camera)

#define surfAmb  (surfProps.x)
#define surfDiff (surfProps.z)

struct VS_INPUT {
	float4 Position		: POSITION;
	float3 Normal		: NORMAL;
	float2 TexCoord		: TEXCOORD0;
	float4 NightColor	: COLOR0;
	float4 DayColor		: COLOR1;
};

struct VS_OUTPUT {
	float4 Position		: POSITION;
	float2 Texcoord0	: TEXCOORD0;    // diffuse UV
	float3 Texcoord1	: TEXCOORD1;    // normal (ps env UV)
	float3 Texcoord2	: TEXCOORD2;    // camera-space position
	float4 Color		: COLOR0;
	float4 Envcolor		: COLOR1;       // .a = env lerp weight
};

VS_OUTPUT
main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	OUT.Position = mul(IN.Position, combined);
	OUT.Texcoord0 = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;

	// Same camera-space basis the shipped shader used; the PS derives its
	// reflection UV from Texcoord1 with `uv = n.xy * 0.5 + 0.5`.
	OUT.Texcoord1 = mul(envmat, IN.Normal);
	OUT.Texcoord2 = mul(envmat, IN.Position.xyz);

	// prelight (day/night blended by setDnParams) * surfDiff + ambient*surfAmb,
	// modulated by the material colour, exactly like xboxBuildingVS.
	// Division guard: surfDiff is never 0 (the callback substitutes 1.0), and
	// nothing here divides.
	float4 prelight = IN.DayColor * dayparam + IN.NightColor * nightparam;
	float4 col = prelight;
	col.rgb *= surfDiff;
	col.rgb += ambient * surfAmb;
	OUT.Color = col * matCol;
	OUT.Color.rgb = saturate(OUT.Color.rgb);

	// Env lerp stays OFF: env-mapped materials are composited by the separate
	// ps2BuildingFxVS / ps2EnvSpecFxPS pass in the callback, and every other
	// material has no env texture bound on s1. With .a == 0 the IV pixel
	// shader collapses to `tex * color`, which is exactly what we want.
	OUT.Envcolor = float4(0.0, 0.0, 0.0, 0.0);

	return OUT;
}
