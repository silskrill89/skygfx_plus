float4x4	combined	: register(c0);
float3		ambient		: register(c4);
// Xbox sun term — same registers as xboxBuildingVS (c5/c6), Force-uploaded
// once per building callback BEFORE either Xbox VS is bound
// (buildingPipe.cpp pipeUploadLightColorForce/pDirect). c5/c6 sit outside this
// family's own block (c0/c4/c19-c23/c29-c32/c36-c38) and previously held
// FXC's auto-allocated literal constants, so declaring them only retires
// those internal slots — no C++ change required. directDir is the light
// TRAVEL direction (unit-length from RW orthonormal frames), negated in the
// dot and never re-normalized (SM3.0 division safety).
float4		directCol	: register(c5);
float3		directDir	: register(c6);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);
float2		windPos		: register(c21);
float		windIntensity : register(c22);
// W8-2 tree sway extension (VS c23 — TreeWind_UploadWindExt, called from
// buildingPipe setWindParams BEFORE every wind-VS bind; VS constant space,
// unrelated to PS c22/c23 owned by pipeUploadPBR):
//   .x heightScale  1/treeHeight: saturate(z*.x) anchors the sway at the
//                   trunk (0 at the model origin, 1 at the canopy)
//   .y gustPhase    radians, slow global gust (windPos-derived)
//   .z gustAmp      gust amplitude in the same units as `dist`
//   .w enable       0 => legacy behaviour, byte-for-byte (a stale or
//                   never-uploaded c23 can never kill/change sway)
float4		windExt		: register(c23);

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

float hash(float n)
{
	return frac(sin(n) * 43758.5453);
}

float noise(float2 x)
{
	float2 p = floor(x);
	float2 f = frac(x);

	f = f * f * (3.0 - 2.0 * f);
	float n = p.x + p.y * 57.0;

	return lerp(lerp(hash(n + 0.0), hash(n + 1.0), f.x),
				lerp(hash(n + 57.0), hash(n + 58.0), f.x), f.y);
}

VS_OUTPUT main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	float4 dayColor = IN.DayColor;
	// The day vertex alpha is the wind weight (consumed by intensityMask
	// below) and is deliberately NOT used as the output alpha: OUT.Color.a is
	// forced to 1.0 further down so buildingPipePS.hlsl main_xboxBuilding
	// (tex * IN.color) leaves the texture alpha as the sole cutout source.
	float intensityMask = (-dayColor.a + 1.0f);

	float4 PositionEdited = IN.Position;

	float dist = ((noise(windPos + PositionEdited) - 0.5) * windIntensity * intensityMask);

	// W8-2: height mask + global gust, gated by windExt.w (see c23 above).
	// w==0 -> hmask==1 and gust==0 -> exactly the legacy `dist` below.
	float hmask = lerp(1.0f, saturate(PositionEdited.z * windExt.x), windExt.w);
	float gust = windExt.w * windExt.z * sin(windExt.y);
	dist = (dist + gust) * hmask;

	PositionEdited.x = (PositionEdited.x + dist);
	PositionEdited.y = (PositionEdited.y + dist);
	PositionEdited.z = (PositionEdited.z + (dist * 0.3));

	OUT.Position = mul(PositionEdited, combined);

	OUT.Texcoord0 = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;
	OUT.Texcoord1 = mul(envmat, IN.Normal).xy * envXform.zw;

	float4 prelight = dayColor * dayparam + IN.NightColor * nightparam;

	// Direct (sun) term — verbatim from xboxBuildingVS. IN.Normal is
	// world-space (the composed c0-c3 block is WVP, not world, so it must NOT
	// touch the normal; wind displacement moves positions only). directDir is
	// the light-travel direction, so the lit side is dot(N, -directDir);
	// max(...,0) guards the back side. No normalize of directDir (uploaded
	// pre-normalized from C++).
	float ndl = max(dot(IN.Normal, -directDir), 0.0);
	float4 directContrib = directCol * ndl * kXboxDirectScale;

	OUT.Color = (prelight * surfDiff + float4(ambient, 0.0) * surfAmb + directContrib) * matCol;
	// The PS (main_xboxBuilding) multiplies OUT.Color.a into the diffuse
	// texture alpha, which must be the sole cutout source. Force alpha to 1.0
	// so neither the wind weight nor directContrib.a can punch holes / make
	// foliage translucent. RGB (the day/night prelight + sun blend) is
	// untouched.
	OUT.Color.a = 1.0;

	// saturate: the source is an int8 shininess (>=128 wraps negative and
	// would make the reflection subtractive). Weight keeps albedo dominant.
	OUT.Envcolor = float4(1.0, 1.0, 1.0, 0.0) * saturate(shininess) * lightmult * kXboxEnvScale;

	return OUT;
}
