// VehiclePBR_Modern.hlsl - Unified vehicle pixel shader (ps_3_0)
// ALL vehicle rendering paths in one file.
//
// Entry points:
//   main               - PBR vehicle (GGX/Smith/Schlick, IBL, cloud shadows, normal buf)
//   main_rubber        - PBR rubber/tire (parametric roughness, dirt/wear tint, subsurface)
//   main_ps2EnvSpecFx  - PS2 env + spec dual-layer
//   main_specCarFx     - Specular car FX (env + specular)
//   main_mobileVehicle - Mobile vehicle (env lerp + specular)
//   main_normMapVehicle - Normal-mapped vehicle (env blend)
//
// Compile with /E <entry> ps_3_0 (or ps_2_0 for legacy passes)
//
// Shared constants (c0-c13): surfProps, fxParams, eye, directCol, lightCol, directDir, lightDir

#include "../include/PBR_Common.hlsl"

sampler2D diffuseTex   : register(s0);
sampler2D envMapTex    : register(s1);
sampler2D maskTex      : register(s2);
sampler2D iblTex       : register(s3);
sampler2D normalBufTex : register(s4);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);  // .z = envIntensity
float3 eyePos      : register(c2);
float4 iblParams   : register(c3);
float4 cloudShadow : register(c4);
float4 directCol   : register(c5);
float4 lightCol[6] : register(c6);
float3 directDir   : register(c12);
float3 lightDir[6] : register(c13);
float4 matCol      : register(c19);
float4 pbrParams   : register(c22); // {glossiness, specular, specTint, envFresnel}
float4 paintNoise  : register(c23); // x=wheel, y=noiseScale, z=edgeBlend
float4 ambientColor : register(c24); // xyz=ambient rgb from timecycle, w=normalBuf enable (0/1)

float whiteNoise(float2 p){
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float smoothNoise(float3 pos){
    float n  = frac(sin(dot(pos, float3(12.9898, 78.233, 45.164))) * 43758.5453);
    float n2 = frac(sin(dot(pos + float3(1,0,0), float3(12.9898, 78.233, 45.164))) * 43758.5453);
    float n3 = frac(sin(dot(pos + float3(0,1,0), float3(12.9898, 78.233, 45.164))) * 43758.5453);
    float n4 = frac(sin(dot(pos + float3(0,0,1), float3(12.9898, 78.233, 45.164))) * 43758.5453);
    return (n + n2 + n3 + n4) * 0.25;
}

// Burley Diffuse (Disney/Crytek) - used for non-metals
float BurleyDiffuse(float NdotL, float NdotV, float VdotH, float roughness){
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float scatterL = lerp(1.0, fd90, pow(1.0 - NdotL, 5.0));
    float scatterV = lerp(1.0, fd90, pow(1.0 - NdotV, 5.0));
    return scatterL * scatterV * lerp(1.0, 1.0/1.51, roughness);
}

struct PS_INPUT{
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

float4 main(PS_INPUT IN) : COLOR
{
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float3 L = normalize(IN.SunDir);

    // ---- Base color ----
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * IN.color.rgb;

    // Blend with screen-space normal (if available)
    // ambientColor.w > 0 means normal buffer is bound and valid
    if(ambientColor.w > 0.5){
        float3 ssNormal = tex2D(normalBufTex, IN.texcoord0).rgb * 2.0 - 1.0;
        if(dot(ssNormal, ssNormal) > 0.25)
            N = normalize(lerp(N, ssNormal, 0.3));
    }

    // ---- PBR material properties (glossiness workflow) ----
    // Everything is dielectric. No metalness.
    // pbrParams.x = glossiness (1=smooth/shiny, 0=rough)
    // pbrParams.y = specular reflectance (F0 at normal incidence, ~0.04 for dielectrics)
    // pbrParams.z = specular color tint (0=white, 1=tinted by paint)
    // pbrParams.w = envFresnel
    float glossiness  = pbrParams.x;
    float roughness   = 1.0 - glossiness;
    float specularF0  = pbrParams.y;
    float specTint    = pbrParams.z;
    float envFresnel  = pbrParams.w;

    // Wheel roughness override
    float isWheel = paintNoise.x;
    float wheelNoise = whiteNoise(IN.texcoord0 * 47.0);
    roughness = lerp(roughness, 0.15, isWheel * wheelNoise * 0.5);

    // ---- Surface noise for reflection breakup ----
    float noiseScale = paintNoise.y;
    float edgeBlend  = paintNoise.z;
    float rawNoise = smoothNoise(IN.WorldPos * 0.5);

    // ---- Paint tint from carcols ----
    float3 paintTint = matCol.rgb;

    // ---- Core PBR vectors ----
    float NdotV = max(dot(N, V), 0.0);

    // F0: dielectric reflectance, optionally tinted by paint color
    float3 F0 = lerp(float3(specularF0, specularF0, specularF0), paintTint * specularF0, specTint);

    // Fresnel
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = 1.0 - kS;  // dielectric: diffuse = all non-reflected light

    // ---- Cloud shadow ----
    float3 noisePos = float3(IN.WorldPos.xy * 0.0008, IN.WorldPos.z * 0.0004);
    float cloudNoise = PBR_cloudFBM(noisePos);
    cloudNoise = smoothstep(0.3, 0.7, cloudNoise);
    float shadowFactor = lerp(0.6, 1.0, 1.0 - cloudNoise * 0.4);

    // ---- LAYER 1: Diffuse (Burley, energy-conserved) ----
    // kD = (1 - kS) * (1 - metalness) — metals have no diffuse
    // ambientColor.rgb = timecycle ambient light, surfProps.x = surface ambient
    float3 ambient = ambientColor.rgb * surfProps.x * baseColor;
    float3 layer1 = diff.rgb * IN.color.rgb * kD * shadowFactor + ambient;

    // ---- Multi-light Fresnel accumulation ----
    float fresnelAccum = 0.0;
    float NdotL_sun = max(dot(N, L), 0.0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float LdotH = max(dot(L, H), 0.0);
        float3 F = F_Schlick(LdotH, F0);
        fresnelAccum += F.r * NdotL_sun;
    }
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdotL = max(dot(N, Ll), 0.0);
        if(NdotL > 0.0){
            float3 H = normalize(V + Ll);
            float LdotH = max(dot(Ll, H), 0.0);
            float3 F = F_Schlick(LdotH, F0);
            fresnelAccum += F.r * NdotL;
        }
    }
    fresnelAccum = saturate(fresnelAccum);

    // ---- LAYER 2: Environment Reflection (env map primary, IBL tint/boost) ----
    float3 R = reflect(-V, N);
    float4 mask = tex2D(maskTex, IN.texcoord0);

    // Sample scene env map (s1) — actual reflection of surroundings
    float2 envReflUV = SphereEnvMapUV(R, V);
    float3 envRefl = tex2D(envMapTex, envReflUV).rgb;

    // Sample IBL (s3) — sky ambient color, tints/boosts the env map
    float3 iblTint = tex2D(iblTex, envReflUV).rgb;

    // Clear-coat Fresnel for car paint: strong at all angles
    float clearCoatF = SchlickFresnelScalar(NdotV, 0.04);
    clearCoatF = lerp(0.35, 1.0, clearCoatF);

    // Fresnel refraction clamp (30→30, 10→10 pattern)
    // Prevents env map melting at grazing angles:
    //   NdotV >= 0.30 → full reflection (100%)
    //   NdotV <= 0.10 → minimum reflection (10%, never zero)
    //   Between → smooth lerp
    float reflFresnel = saturate((NdotV - 0.10) / (0.30 - 0.10));
    reflFresnel = lerp(0.10, 1.0, reflFresnel);

    // Layer 2 = env map (primary) tinted by IBL sky color, Fresnel-modulated
    // IBL tints additively, not multiplicatively — prevents dark IBL areas killing reflections
    float3 iblBlend = lerp(envRefl, envRefl + iblTint * 0.15, 0.5);
    float3 layer2 = iblBlend * paintTint * clearCoatF * envFresnel * reflFresnel;

    // ---- Fresnel Rim ----
    float rimStrength = pow(1.0 - NdotV, 3.0) * envFresnel * 0.3;
    float3 fresnelRim = F_Schlick(NdotV, F0) * paintTint * rimStrength;

    // ---- PBR Specular (GGX/Smith) — direct lights only ----
    float3 specTotal = float3(0, 0, 0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        float D = D_GGX(NdotH, roughness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, roughness);
        float3 F = F_Schlick(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * directCol.rgb;
    }
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float3 H = normalize(V + Ll);
        float NdotL = max(dot(N, Ll), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(Ll, H), 0.0);
        if(NdotL > 0.0){
            float D = D_GGX(NdotH, roughness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
            float3 F = F_Schlick(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb;
        }
    }

    // ---- IBL fill ----
    float2 iblUV = N.xy * 0.5 + 0.5;
    float3 layer4 = tex2D(iblTex, iblUV).rgb * 0.06;

    // ---- COMPOSITE ----
    // Preserve base paint color from carcols (diff.rgb * IN.color.rgb)
    // The carcols color is the foundation - lighting and reflections are overlays
    
    // Base paint with minimal lighting modification
    float3 basePaint = diff.rgb * IN.color.rgb;
    
    // Apply shadow factor to darken base paint (not replace it)
    float3 litPaint = basePaint * shadowFactor;
    
    // Add ambient to lift shadows (preserves base color in dark areas)
    litPaint += ambient * 0.3;
    
    // Reflections as overlay (scaled down to preserve paint)
    float3 reflection = layer2 * 0.35;
    
    // Rim light (very subtle)
    float3 rim = fresnelRim * 0.25;
    
    // Specular highlights (halved for PS2 light intensity)
    float3 specular = specTotal * 0.5;
    
    // IBL fill (subtle ambient from environment)
    float3 iblFill = layer4 * 0.5;
    
    // Composite: start with lit paint, add overlays
    float3 color = litPaint;
    color += reflection * reflFresnel;  // reflections respect Fresnel
    color += rim;
    color += specular;
    color += iblFill;

    // Soft tonemap (Reinhard) instead of hard saturate() to avoid clamp banding
    color = color / (1.0 + color);
    // Slight contrast boost to recover midtones
    color = pow(color, 0.9);
    // Linear → sRGB encoding (display gamma)
    // Using approximate: pow(x, 1/2.2) matches sRGB closely
    color = pow(saturate(color), 1.0/2.2);

    return float4(color, diff.a);
}

// ============================================================
// LEGACY VEHICLE ENTRY POINTS
// Each compiled independently via /E <entry>
// ============================================================

// Shared legacy helper
float specTermLegacy(float3 reflVec, float3 light, float pwr)
{
    return pow(max(dot(reflVec, light), 0.0), pwr);
}

// ============================================================
// Pass: PS2 env + spec dual-layer
// Entry: main_ps2EnvSpecFx
// ============================================================
struct PS_INPUT_PS2SPEC {
    float4 position : POSITION;
    float3 texcoord1 : TEXCOORD0;
    float3 texcoord2 : TEXCOORD1;
    float4 envcolor  : COLOR0;
    float4 speccolor : COLOR1;
};

float4 main_ps2EnvSpecFx(PS_INPUT_PS2SPEC IN) : COLOR
{
    float4 color = tex2D(envMapTex, IN.texcoord1.xy) * IN.envcolor +
           tex2D(maskTex, IN.texcoord2.xy) * IN.speccolor;
    return saturate(color);
}

// ============================================================
// Pass: Specular car FX
// Entry: main_specCarFx
// ============================================================
struct PS_INPUT_SPECCAR {
    float4 position : POSITION;
    float3 texcoord1 : TEXCOORD0;
    float4 envcolor  : COLOR0;
    float4 speccolor : COLOR1;
};

float4 main_specCarFx(PS_INPUT_SPECCAR IN) : COLOR
{
    float4 color = tex2D(envMapTex, IN.texcoord1.xy) * IN.envcolor + IN.speccolor;
    return saturate(color);
}

// ============================================================
// Pass: Mobile vehicle
// Entry: main_mobileVehicle
// ============================================================
struct PS_INPUT_MOBILE {
    float2 texcoord0 : TEXCOORD0;
    float3 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float3 spec      : COLOR1;
};

float4 main_mobileVehicle(PS_INPUT_MOBILE IN) : COLOR
{
    float4 col = tex2D(diffuseTex, IN.texcoord0) * IN.color;

    float2 ReflPos = normalize(IN.texcoord1.xy) * (IN.texcoord1.z * 0.5 + 0.5);
    ReflPos = ReflPos * float2(0.5, -0.5) + float2(0.5, 0.5);
    float4 ReflCol = tex2D(envMapTex, ReflPos);
    col.rgb = lerp(col.rgb, ReflCol.rgb, fxParams.y);
    col.a += ReflCol.b * 0.125;

    col.rgb += IN.spec;
    col.rgb = saturate(col.rgb);
    return col;
}

// ============================================================
// Pass: Normal-mapped vehicle
// Entry: main_normMapVehicle
// ============================================================
struct PS_INPUT_NORMMAP {
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

float4 main_normMapVehicle(PS_INPUT_NORMMAP IN) : COLOR
{
    float4 diff = tex2D(diffuseTex, IN.texcoord0) * IN.color;
    float4 env = tex2D(envMapTex, IN.texcoord0);
    float4 color = lerp(diff, env * IN.envColor, IN.envColor.a) + diff;
    color.rgb = saturate(color.rgb);
    color.a = diff.a;
    return color;
}

// ============================================================
// Pass: Building (unified PBR - material-driven, not arch-type)
// Entry: main_building
//
// BRDF material properties uploaded via c22:
//   c22 = {roughness, reflectance, clearcoat, subsurface}
//   c23 = {specularInt, metalness, 0, 0}
//
// Uses vertex color as base color (buildings use vertex color).
// Supports day/night blending via vertex alpha.
// ============================================================
struct PS_INPUT_BUILDING {
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    float4 color       : COLOR0;  // vertex color (day/night blend in alpha)
    float4 dayNight    : COLOR1;  // day/night parameters
};

float4 main_building(PS_INPUT_BUILDING IN) : COLOR
{
    // Base color from texture * vertex color
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * IN.color.rgb;

    // BRDF material properties from library
    float glossiness   = pbrParams.x;  // from c22 (CryEngine glossiness workflow)
    float roughness    = 1.0 - glossiness;
    float reflectance  = pbrParams.y;
    float clearcoat    = pbrParams.z;
    float subsurface   = pbrParams.w;
    float specularInt  = paintNoise.x; // from c23.x
    float metalness    = paintNoise.y; // from c23.y

    // Normals
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float3 L = normalize(IN.SunDir);

    // Core PBR vectors
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    // F0: dielectric reflectance
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseColor, metalness);

    // Fresnel
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = (1.0 - kS) * (1.0 - metalness);

    // Day/night blending (vertex color alpha)
    float dayFactor = IN.color.a;

    // Diffuse lighting (Burley)
    float3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);
    float diffuse = BurleyDiffuse(NdotL, NdotV, VdotH, roughness);

    // Specular (GGX/Smith)
    float NdotH = max(dot(N, H), 0.0);
    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
    float3 F = F_Schlick(max(dot(L, H), 0.0), F0);

    float3 specTotal = D * F * Vis * NdotL * directCol.rgb * specularInt;

    // Multi-light accumulation
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdotL_l = max(dot(N, Ll), 0.0);
        if(NdotL_l > 0.0){
            float3 Hl = normalize(V + Ll);
            float NdotH_l = max(dot(N, Hl), 0.0);
            float Dl = D_GGX(NdotH_l, roughness);
            float Visl = V_SmithCorrelated(NdotV, NdotL_l, roughness);
            float3 Fl = F_Schlick(max(dot(Ll, Hl), 0.0), F0);
            specTotal += Dl * Fl * Visl * NdotL_l * lightCol[i].rgb * specularInt;
        }
    }

    // IBL (ambient)
    float2 iblUV = N.xy * 0.5 + 0.5;
    float3 ibl = tex2D(iblTex, iblUV).rgb * 0.06;

    // Composite
    float3 color = baseColor * kD * diffuse * directCol.rgb;
    color += specTotal;
    color += ibl;

    // Day/night blend (night = darker, more ambient)
    color = lerp(color * 0.3, color, dayFactor);

    // Scale down (PS2 lights are too strong for linear PBR)
    color *= 0.5;
    // Soft tonemap + gamma encode
    color = color / (1.0 + color);
    color = pow(saturate(color), 1.0/2.2);

    return float4(color, diff.a);
}

// ============================================================
// RUBBER/TIRE ENTRY POINT
// Entry: main_rubber
// Parametric rubber BRDF with dirt/wear tint and subsurface wrap
// c22 = {roughness, F0, tintR, tintG}  (tireParams)
// c23 = {tintB, dirtLevel, wearFactor, 0}  (tireParams2)
// ============================================================
float4 main_rubber(PS_INPUT IN) : COLOR
{
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float3 L = normalize(IN.SunDir);

    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * IN.color.rgb * matCol.rgb;

    float roughness  = pbrParams.x;
    float rubberF0   = pbrParams.y;
    float tintR      = pbrParams.z;
    float tintG      = pbrParams.w;
    float tintB      = paintNoise.x;
    float dirtLevel  = paintNoise.y;
    float wearFactor = paintNoise.z;

    float3 F0 = float3(rubberF0, rubberF0, rubberF0);

    float3 dirtTint = float3(0.35, 0.25, 0.15);
    float3 wearTint = float3(0.55, 0.55, 0.50);
    baseColor = lerp(baseColor, dirtTint * baseColor, dirtLevel * 0.4);
    baseColor = lerp(baseColor, wearTint * baseColor, wearFactor * 0.3);

    roughness = lerp(roughness, min(roughness + 0.1, 0.98), dirtLevel * 0.5);

    float NdotV = max(dot(N, V), 0.0);

    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = (1.0 - kS);

    float3 diffuse = baseColor * kD;

    float3 specTotal = float3(0, 0, 0);
    float NdotL_sun = max(dot(N, L), 0.0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        float D = D_GGX(NdotH, roughness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, roughness);
        float3 F = F_Schlick(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * directCol.rgb;

        float subsurface = 0.08;
        float wrapDiffuse = saturate((NdotL_sun + subsurface) / (1.0 + subsurface));
        diffuse += baseColor * wrapDiffuse * directCol.rgb * subsurface;
    }

    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdotL = max(dot(N, Ll), 0.0);
        if(NdotL > 0.0){
            float3 H = normalize(V + Ll);
            float NdotH = max(dot(N, H), 0.0);
            float LdotH = max(dot(Ll, H), 0.0);
            float D = D_GGX(NdotH, roughness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
            float3 F = F_Schlick(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb * 0.3;
        }
    }

    float3 fresnelSheen = F_atNdotV * baseColor * 0.08;

    float3 color = diffuse + specTotal + fresnelSheen;

    return float4(color, diff.a);
}
