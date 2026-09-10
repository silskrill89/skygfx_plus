// vehiclePipeVS.hlsl - Consolidated vehicle vertex shaders
// Entry points: main_vehicle, main_vehiclePBR, main_ps2CarFx, main_specCarFx,
//               main_xboxCar, main_leedsCarFx, main_mobileVehicle,
//               main_neoPass1, main_neoPass2, main_edgeTessellation
// Compile with /E <entry> for each pass

// ============================================================
// Shared constants
// ============================================================
float4x4 combined     : register(c0);
float3   ambient      : register(c4);
float3   directCol[7] : register(c5);
float3   directDir[7] : register(c12);
float4   matCol       : register(c19);
float4   surfProps    : register(c20);

#define surfAmb      (surfProps.x)
#define surfDiff     (surfProps.z)
#define isPrelit     (surfProps.w)

// ============================================================
// Pass 0: Basic vehicle VS (PS2/PC default)
// ============================================================
struct VS_INPUT_VEH {
    float4 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 Texcoord0: TEXCOORD0;
    float2 Texcoord1: TEXCOORD1;
};

struct VS_OUTPUT_VEH {
    float4 Position  : POSITION;
    float2 Texcoord0 : TEXCOORD0;
    float4 Color     : COLOR0;
};

VS_OUTPUT_VEH main_vehicle(VS_INPUT_VEH IN)
{
    VS_OUTPUT_VEH OUT;
    OUT.Position = mul(IN.Position, combined);
    OUT.Texcoord0 = IN.Texcoord0;
    OUT.Color = float4(IN.Color.rgb * isPrelit, 1.0);
    OUT.Color.xyz += ambient * surfAmb;
    for(int i = 0; i < 7; i++){
        float l = max(0.0, dot(IN.Normal, -directDir[i]));
        OUT.Color.xyz += l * directCol[i] * surfDiff;
    }
    OUT.Color = saturate(OUT.Color) * matCol;
    return OUT;
}

// ============================================================
// Pass 1: Vehicle PBR VS (world-space vectors for PBR lighting)
// ============================================================
float4   fxParams    : register(c21);
float4x4 world       : register(c30);
float3   eyePos      : register(c34);

#define fresnel   (fxParams.x)
#define shininess (fxParams.w)
#define surfPrelight (surfProps.w)

struct VS_OUTPUT_VPBR {
    float4 Position   : POSITION;
    float2 Texcoord0  : TEXCOORD0;
    float3 WorldNormal: TEXCOORD1;
    float3 WorldPos   : TEXCOORD2;
    float3 ViewDir    : TEXCOORD3;
    float3 SunDir     : TEXCOORD4;
    float4 Color      : COLOR0;
    float4 EnvColor   : COLOR1;
};

VS_OUTPUT_VPBR main_vehiclePBR(VS_INPUT_VEH IN)
{
    VS_OUTPUT_VPBR OUT;
    OUT.Position = mul(IN.Position, combined);
    OUT.Texcoord0 = IN.Texcoord0;

    OUT.Color = float4(IN.Color.rgb * surfPrelight, 1.0);
    OUT.Color.xyz += ambient * surfAmb;
    for(int i = 0; i < 7; i++){
        float l = max(0.0, dot(IN.Normal, -directDir[i]));
        OUT.Color.xyz += l * directCol[i] * surfDiff;
    }
    OUT.Color = saturate(OUT.Color) * matCol;

    float4 WorldPos = mul(IN.Position, world);
    float3 wn = mul(IN.Normal, (float3x3)world);
    float3 WorldNormal = length(wn) > 1e-6 ? normalize(wn) : float3(0, 1, 0);
    // View direction: surface -> camera (matches buildingPBRVS convention).
    // Previously WorldPos - eyePos (camera->surface) made NdotV = max(dot(N,V),0)
    // evaluate to 0 in the PS: clearcoatFresnel clamped to 1, kr collapsed to 1,
    // and layer2 = lerp(layer1, envTerm, 1) replaced the base texture with the env
    // reflection — the base paint vanished. Also corrupted reflect(-V, N).
    float3 vv = eyePos - WorldPos.xyz;
    float3 ViewVector = length(vv) > 1e-6 ? normalize(vv) : float3(0, 0, 1);
    OUT.WorldPos = WorldPos;
    OUT.WorldNormal = WorldNormal;
    OUT.ViewDir = ViewVector;
    OUT.SunDir = -directDir[0];

    // Grazing rim for glass env intensity: b = 1 - NdotV (ViewVector is now
    // surface->camera, so dot(ViewVector, WorldNormal) is the positive NdotV).
    float b = 1.0 - saturate(dot(ViewVector, WorldNormal));
    OUT.EnvColor = lerp(1.0f, b * b * b * b * b, fresnel) * shininess;

    return OUT;
}

// ============================================================
// Pass 2: PS2 car FX VS (env1/env2 maps + specular)
// ============================================================
float4   fxParams_ps2   : register(c30);
float4   envXform       : register(c31);
float3x3 envmat         : register(c32);
float3x3 specmat        : register(c35);
float3   lightdir       : register(c38);

#define fxSwitch_ps2   (fxParams_ps2.x)
#define shininess_ps2  (fxParams_ps2.y)
#define specularity_ps2 (fxParams_ps2.z)
#define lightmult_ps2  (fxParams_ps2.w)

struct VS_OUTPUT_PS2 {
    float4 Position : POSITION;
    float2 Texcoord0: TEXCOORD0;
    float3 Texcoord1: TEXCOORD1;
    float4 Envcolor : COLOR0;
    float4 Speccolor: COLOR1;
};

VS_OUTPUT_PS2 main_ps2CarFx(VS_INPUT_VEH IN)
{
    VS_OUTPUT_PS2 OUT;
    OUT.Position = mul(IN.Position, combined);

    float3 envNormal = mul(envmat, IN.Normal);
    if(fxSwitch_ps2 == 1.0f){
        OUT.Texcoord0.xy = envNormal.xy - envXform.xy;
        OUT.Texcoord0.xy *= -envXform.zw;
    }else if(fxSwitch_ps2 == 2.0f){
        OUT.Texcoord0 = envNormal.xy - envXform.xy;
        OUT.Texcoord0.y *= envXform.y;
        OUT.Texcoord0.xy += IN.Texcoord1;
        OUT.Texcoord0.xy *= -envXform.zw;
    }
    OUT.Envcolor = float4(192.0, 192.0, 192.0, 0.0) / 128.0 * shininess_ps2 * lightmult_ps2;

    float3 N = mul(specmat, IN.Normal);
    float3 V = lightdir - 2.0 * N * dot(N, lightdir);
    OUT.Texcoord1.xyz = (V + float3(1.0, 1.0, 0.0)) / 2.0;

    if(OUT.Texcoord1.z < 0.0)
        OUT.Speccolor = float4(96.0, 96.0, 96.0, 0.0) / 128.0 * specularity_ps2 * lightmult_ps2;
    else
        OUT.Speccolor = float4(0.0, 0.0, 0.0, 0.0);

    return OUT;
}

// ============================================================
// Pass 3: Specular car FX VS
// ============================================================
float4   fxParams_spec  : register(c30);
float4   envXform_spec  : register(c31);
float3x3 envmat_spec    : register(c32);
float3   eyePos_spec    : register(c36);

#define shininess_spec (fxParams_spec.y)
#define specularity_spec (fxParams_spec.z)
#define lightmult_spec (fxParams_spec.w)

struct VS_OUTPUT_SPEC {
    float4 Position : POSITION;
    float2 Texcoord0: TEXCOORD0;
    float4 Envcolor : COLOR0;
    float4 Speccolor: COLOR1;
};

VS_OUTPUT_SPEC main_specCarFx(VS_INPUT_VEH IN)
{
    VS_OUTPUT_SPEC OUT;
    float3 envNormal = mul((float3x3)envmat_spec, IN.Normal);
    OUT.Position = mul(IN.Position, combined);

    float4 fxS = fxParams_spec;
    if(fxS.x == 1.0f){
        OUT.Texcoord0.xy = envNormal.xy - envXform_spec.xy;
        OUT.Texcoord0.xy *= -envXform_spec.zw;
    }else if(fxS.x == 2.0f){
        OUT.Texcoord0 = envNormal.xy - envXform_spec.xy;
        OUT.Texcoord0.y *= envXform_spec.y;
        OUT.Texcoord0.xy += IN.Texcoord1;
        OUT.Texcoord0.xy *= -envXform_spec.zw;
    }
    OUT.Envcolor = float4(192.0, 192.0, 192.0, 0.0) / 128.0 * shininess_spec * lightmult_spec;

    float3 ve = eyePos_spec - IN.Position.xyz;
    float3 V = length(ve) > 1e-6 ? normalize(ve) : float3(0, 0, 1);
    float spec = pow(saturate(dot(IN.Normal, normalize(V + -directDir[0]))), 16);
    OUT.Speccolor.rgb = spec * 3 * float3(0.75, 0.75, 0.75) * specularity_spec * lightmult_spec;
    OUT.Speccolor.a = 1.0;

    return OUT;
}

// ============================================================
// Pass 4: Xbox car VS
// ============================================================
float4x4 World_xb  : register(c0);
float4x4 View_xb   : register(c4);
float4x4 Proj_xb   : register(c8);
float4x4 WorldIT_xb: register(c12);
float4x4 Texture_xb: register(c16);
float4 matCol_xb    : register(c20);
float3 sunDir_xb    : register(c23);
float3 sunDiff_xb   : register(c24);
float3 sunAmb_xb    : register(c25);
float4 surfProps_xb : register(c26);
float3 eye_xb       : register(c40);
float envSwitch     : register(c39);

struct VS_INPUT_XB {
    float3 Position : POSITION;
    float4 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 Texcoord0: TEXCOORD0;
    float2 Texcoord1: TEXCOORD1;
};

struct VS_OUTPUT_XB {
    float4 Position : POSITION;
    float2 Texcoord0: TEXCOORD0;
    float2 Texcoord1: TEXCOORD1;
    float4 Color    : COLOR0;
};

VS_OUTPUT_XB main_xboxCar(VS_INPUT_XB IN)
{
    VS_OUTPUT_XB OUT;
    float3 N = normalize(mul(IN.Normal, WorldIT_xb).xyz);
    OUT.Position = mul(Proj_xb, mul(View_xb, mul(World_xb, float4(IN.Position, 1.0))));
    float3 V = normalize(eye_xb - mul(World_xb, float4(IN.Position, 1.0)).xyz);

    OUT.Texcoord0 = IN.Texcoord0;
    OUT.Texcoord1 = IN.Texcoord1;

    float3 specCol = float3(0.7, 0.7, 0.7);
    float3 diff = saturate(dot(N, -sunDir_xb)) * sunDiff_xb * surfProps_xb.z * matCol_xb.xyz;
    float3 spec = pow(saturate(dot(N, normalize(V + -sunDir_xb))), 16) * specCol * surfProps_xb.y;
    float3 amb = sunAmb_xb * surfProps_xb.x * matCol_xb.xyz;

    float3 eyeN = normalize(mul((float3x3)View_xb, N));
    float2 tex1 = mul(Texture_xb, float4(eyeN.x, eyeN.y, 1.0, 0.0)).xy;
    float2 tex2 = mul(Texture_xb, float4(IN.Texcoord1.x, IN.Texcoord1.y, 1.0, 0.0)).xy;

    OUT.Color = float4(0.0, 0.0, 0.0, matCol_xb.a);
    if(envSwitch == 1.0f){
        OUT.Color.rgb = amb + diff;
    }else if(envSwitch == 2.0f){
        OUT.Color.rgb = amb + diff;
        OUT.Texcoord1 = tex1;
    }else if(envSwitch == 3.0f){
        OUT.Color.rgb = amb + diff;
        OUT.Texcoord1 = tex2;
    }else if(envSwitch == 4.0f){
        OUT.Color.rgb = amb + (diff + spec) * 0.5;
    }else if(envSwitch == 5.0f){
        OUT.Color.rgb = amb + (diff + spec) * 0.5;
        OUT.Texcoord1 = tex1;
    }else if(envSwitch == 6.0f){
        OUT.Color.rgb = amb + (diff + spec) * 0.5;
        OUT.Texcoord1 = tex2;
    }

    return OUT;
}

// ============================================================
// Pass 5: Leeds car FX VS
// ============================================================
float4    fxParams_leeds : register(c30);
float3x3  envmat_leeds   : register(c32);
float4x4  texmat_leeds   : register(c40);

#define shininess_leeds (fxParams_leeds.y)
#define lightmult_leeds (fxParams_leeds.w)

struct VS_OUTPUT_LEEDS {
    float4 Position : POSITION;
    float2 Texcoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VS_OUTPUT_LEEDS main_leedsCarFx(VS_INPUT_VEH IN)
{
    VS_OUTPUT_LEEDS OUT;
    OUT.Position = mul(IN.Position, combined);

    float3 envNormal = mul(envmat_leeds, IN.Normal);
    OUT.Texcoord = mul(texmat_leeds, float4(envNormal.xy, 0.0, 1.0)).xy;
    OUT.Color = float4(1.0, 1.0, 1.0, shininess_leeds * lightmult_leeds);
    return OUT;
}

// ============================================================
// Pass 6: Mobile vehicle VS
// ============================================================
float4   fxParams_mob   : register(c30);
float4x4 worldmat       : register(c31);
float3   campos          : register(c36);

#define surfAmb_mob    (surfProps.x)
#define surfDiff_mob   (surfProps.z)
#define isPrelit_mob   (surfProps.w)
#define shininess_mob  (fxParams_mob.y)
#define specularity_mob (fxParams_mob.z)
#define lightmult_mob  (fxParams_mob.w)

struct VS_OUTPUT_MOB {
    float4 Position : POSITION;
    float2 Texcoord0: TEXCOORD0;
    float3 Texcoord1: TEXCOORD1;
    float4 Color    : COLOR0;
    float3 Spec     : COLOR1;
};

VS_OUTPUT_MOB main_mobileVehicle(VS_INPUT_VEH IN)
{
    VS_OUTPUT_MOB OUT;
    OUT.Position = mul(IN.Position, combined);
    OUT.Texcoord0 = IN.Texcoord0;

    float4 WorldPos = mul(IN.Position, worldmat);
    float3 WorldNormal = mul(IN.Normal, (float3x3)worldmat);
    float3 rv = WorldPos.xyz - campos;
    float3 ReflVector = length(rv) > 1e-6 ? normalize(rv) : float3(0, 0, 1);
    ReflVector = ReflVector - 2.0 * dot(ReflVector, WorldNormal) * WorldNormal;
    OUT.Texcoord1 = ReflVector;

    float specAmt = pow(max(dot(ReflVector, mul(-directDir[0], (float3x3)worldmat)), 0.0), 10.0f) * specularity_mob * 2.0;
    OUT.Spec = specAmt * directCol[0];

    OUT.Color = float4(IN.Color.rgb * isPrelit_mob, 1.0);
    OUT.Color.xyz += ambient * surfAmb_mob;
    for(int i = 0; i < 7; i++){
        float l = max(0.0, dot(IN.Normal, -directDir[i]));
        OUT.Color.xyz += l * directCol[i] * surfDiff_mob;
    }
    OUT.Color = saturate(OUT.Color) * matCol;

    return OUT;
}

// ============================================================
// Pass 7: Neo vehicle pass 1 VS
// ============================================================
float4x4 world_neo      : register(c4);
float4x4 tex_neo        : register(c8);
float3   eye_neo        : register(c12);
float3   directDir_neo  : register(c13);
float3   ambient_neo    : register(c15);
float4   matCol_neo     : register(c16);
float3   directCol_neo  : register(c17);
float3   lightDir_neo[6]: register(c18);
float3   lightCol_neo[6]: register(c24);

float4   directSpec_neo : register(c30);
float4   reflProps_neo  : register(c31);
float3   surfProps_neo  : register(c32);

#define shininess_neo (reflProps_neo.x)
#define fresnel_neo   (reflProps_neo.y)
#define surfAmb_neo   (surfProps_neo.x)
#define surfDiff_neo  (surfProps_neo.z)

struct VS_OUTPUT_NEO1 {
    float4 position   : POSITION;
    float2 texcoord0  : TEXCOORD0;
    float2 texcoord1  : TEXCOORD1;
    float4 color      : COLOR0;
    float4 reflcolor  : COLOR1;
};

VS_OUTPUT_NEO1 main_neoPass1(VS_INPUT_VEH IN)
{
    VS_OUTPUT_NEO1 Out;
    Out.position = mul(IN.Position, combined);
    Out.texcoord0 = IN.Texcoord0;
    float3 wn2 = mul(IN.Normal, (float3x3)world_neo).xyz;
    float3 N = length(wn2) > 1e-6 ? normalize(wn2) : float3(0, 1, 0);
    float3 vn = eye_neo - mul(IN.Position, world_neo).xyz;
    float3 V = length(vn) > 1e-6 ? normalize(vn) : float3(0, 0, 1);

    float3 c = saturate(dot(N, -directDir_neo)) * surfDiff_neo;
    c += ambient_neo * surfAmb_neo;
    for(int i = 0; i < 6; i++)
        c += lightCol_neo[i] * saturate(dot(N, -lightDir_neo[i])) * surfDiff_neo;
    Out.color = float4(saturate(c), 1.0f) * matCol_neo;

    float a = dot(V, N) * 2.0;
    float3 uv2 = N * a - V;
    uv2 = mul(tex_neo, uv2);
    Out.texcoord1.xy = uv2.xy * 0.5 + 0.5;
    float b = 1.0 - saturate(dot(V, N));
    Out.reflcolor = lerp(b * b * b * b * b, 1.0f, fresnel_neo) * shininess_neo;

    return Out;
}

// ============================================================
// Pass 8: Neo vehicle pass 2 VS (specular)
// ============================================================
float4   directSpec_neo2 : register(c30);
float4   reflProps_neo2  : register(c31);
float3   surfProps_neo2  : register(c32);

#define power_neo (reflProps_neo2.w)
#define surfSpec_neo (surfProps_neo2.y)

struct VS_OUTPUT_NEO2 {
    float4 position : POSITION;
    float4 color    : COLOR0;
};

float specTerm2(float3 N, float3 L, float3 V, float pwr)
{
    return pow(saturate(dot(N, normalize(V + L))), pwr);
}

VS_OUTPUT_NEO2 main_neoPass2(VS_INPUT_VEH IN)
{
    VS_OUTPUT_NEO2 Out;
    Out.position = mul(IN.Position, combined);
    float3 vn2 = eye_neo - mul(IN.Position, world_neo).xyz;
    float3 V = length(vn2) > 1e-6 ? normalize(vn2) : float3(0, 0, 1);
    float3 N = mul(IN.Normal, (float3x3)world_neo).xyz;

    Out.color = float4((directSpec_neo2 * specTerm2(N, -directDir_neo, V, power_neo)).rgb, 1.0);
    for(int i = 0; i < 6; i++)
        Out.color.rgb += lightCol_neo[i] * specTerm2(N, -lightDir_neo[i], V, power_neo * 2);
    Out.color = saturate(Out.color * surfSpec_neo);

    return Out;
}

// ============================================================
// Pass 9: Edge tessellation VS (optional, smooths jagged edges)
// ============================================================
// This is a modern feature - kept for compatibility but not actively used
// when edgeTessEnable is false in config
