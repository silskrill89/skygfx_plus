// Xbox building VS — register layout (vs_3_0):
//   c0      combined WVP matrix (4 regs)
//   c4      ambient color
//   c5      direct (sun) color — NEW: Force-uploaded from pDirect every cb
//           (buildingPipe.cpp Xbox cb); near-black when the timecycle
//           deactivates the sun, so the term below is naturally zero then.
//   c6      direct (sun) direction — NEW: pDirect frame LTM 'at'
//           (light-travel direction, pre-normalized by RW orthonormal
//           frames; NOT re-normalized here — SM3.0 division safety).
//   c19     material color
//   c20     surface props {ambient, -, diffuse, -} (packed float4)
//   c21-c23 wind (xboxBuildingWindVS only; unused here)
//   c29     shader params
//   c30/c31 day/night prelight balance params
//   c32     texture matrix (4 regs)
//   c36     fx params, c37 env xform, c38 env matrix (3 regs)
// c5/c6 are free in the Xbox VS family: c5-c11/c12-c18 are the
// PS2/GTAIV/PBR VS light-array space and no Xbox-family VS declared them.
float4x4	combined	: register(c0);
float3		ambient		: register(c4);
float4		directCol	: register(c5);
float3		directDir	: register(c6);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);

float4		shaderParams	: register(c29);
float4		dayparam	: register(c30);
float4		nightparam	: register(c31);
float4x4	texmat		: register(c32);
float4		fxParams	: register(c36);
float4		envXform	: register(c37);
float3x3	envmat		: register(c38);

#define surfAmb (surfProps.x)
#define surfDiff (surfProps.z)
#define shininess (fxParams.x)
#define lightmult (fxParams.y)

// aap/junior have NO direct-sun term in the Xbox building VS: the baked
// DayColor/NightColor prelight already contains the sunlight, so an unscaled
// direct term double-counts it. Keep the term (dawn/dusk direction gradient)
// but at the same small scale as buildingPBRVS.hlsl's kDirectScale.
static const float kXboxDirectScale = 1.0;

// Env (reflection) weight. The env addend used to be implicitly scaled by
// the removed *10.0 diffuse multiplier; without it a baked sphere map
// dominates albedo and reads as a flat "lightmap" patch on floors.
static const float kXboxEnvScale = 0.5;

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
	float2 Texcoord1	: TEXCOORD1;
	float4 Color		: COLOR0;
	float4 Envcolor		: COLOR1;
};

VS_OUTPUT main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	OUT.Position = mul(IN.Position, combined);
	OUT.Texcoord0 = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;
	OUT.Texcoord1 = mul(envmat, IN.Normal).xy * envXform.zw;

	float4 prelight = IN.DayColor * dayparam + IN.NightColor * nightparam;

	// Direct (sun) term: IN.Normal is world-space for static building
	// geometry (SA bakes building atomics with identity object matrices —
	// the composed transform c0-c3 is WVP, not world, so it must NOT touch
	// the normal). directDir is the light-travel direction, so the lit side
	// is dot(N, -directDir); max(...,0) guards the back side. No normalize
	// of directDir (uploaded pre-normalized from C++).
	float ndl = max(dot(IN.Normal, -directDir), 0.0);
	float4 directContrib = directCol * ndl * kXboxDirectScale;

	OUT.Color = (prelight * surfDiff + float4(ambient, 0.0) * surfAmb + directContrib) * matCol;

	// saturate: the source is an int8 shininess (>=128 wraps negative and
	// would make the reflection subtractive). Weight keeps albedo dominant.
	OUT.Envcolor = float4(1.0, 1.0, 1.0, 0.0) * saturate(shininess) * lightmult * kXboxEnvScale;

	return OUT;
}