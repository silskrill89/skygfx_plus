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


// Burley Diffuse (Disney/Crytek) — returns BRDF × NdotL (energy-conserved cosine weighting)
// Matches CryEngine shadeLib.cfi BurleyBRDF exactly.
float BurleyDiffuse(float NdotL, float NdotV, float VdotH, float roughness){
    NdotV = max(NdotV, 0.1);  // Prevent overly dark edges (CryEngine convention)
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float scatterL = lerp(1.0, fd90, pow(1.0 - NdotL, 5.0));
    float scatterV = lerp(1.0, fd90, pow(1.0 - NdotV, 5.0));
    return scatterL * scatterV * lerp(1.0, 1.0/1.51, roughness) * NdotL;
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
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    // Use PS c12 (directDir) — world-space sun direction.
    // IN.SunDir from VS is LOCAL-space (wrong for rotated vehicles).
    float3 L = length(directDir) > 1e-6 ? -normalize(directDir) : float3(0, 0, -1);

    // ---- Base color ----
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    // Albedo = texture × paint color ONLY. Vertex color (IN.color) contains
    // VS-baked lighting × matCol — using it doubles lighting and matCol.
    float3 baseColor = diff.rgb * matCol.rgb;

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

    // (paintNoise.y/z = noiseScale/edgeBlend, reserved for future reflection breakup)

    // ---- Paint tint from carcols ----
    float3 paintTint = matCol.rgb;

    // ---- Core PBR vectors ----
    float NdotV = max(dot(N, V), 0.0);

    // F0: dielectric reflectance, optionally tinted by paint color
    float3 F0 = lerp(float3(specularF0, specularF0, specularF0), paintTint * specularF0, specTint);

    // Energy conservation: metallic paints have near-zero diffuse
    // metallicFactor: 0 for dielectric (specF0 < 0.5), 1 for metal (specF0 > 0.5)
    float metallicFactor = saturate((specularF0 - 0.5) * 2.0);
    float diffuseScale = lerp(saturate(1.0 - dot(F0, float3(0.2126, 0.7152, 0.0722))), 0.05, metallicFactor);
    baseColor *= diffuseScale;

    // Fresnel
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = (1.0 - kS) * (1.0 - metallicFactor * 0.95);  // metallic: near-zero diffuse

    // ---- Cloud shadow ----
    float3 noisePos = float3(IN.WorldPos.xy * 0.0008, IN.WorldPos.z * 0.0004);
    float cloudNoise = PBR_cloudFBM(noisePos);
    cloudNoise = smoothstep(0.3, 0.7, cloudNoise);
    float shadowFactor = lerp(0.6, 1.0, 1.0 - cloudNoise * 0.4);

    // ---- Direct light — NO floor, use raw timecycle values ----
    // Buildings use directCol.rgb directly and look correct at all times of day.
    // Previous 0.20 floor made vehicles artificially bright at dawn/night.
    float3 sunContrib = directCol.rgb;

    float NdotL_sun = max(dot(N, L), 0.0);
    float3 H_sun = normalize(V + L);
    float VdotH_sun = max(dot(V, H_sun), 0.0);
    float burleyDiff = BurleyDiffuse(NdotL_sun, NdotV, VdotH_sun, roughness);

    // ---- Ambient: timecycle object ambient illuminating surfaces ----
    // Uses ambientObj from timecycle (via PS c24), which is the game's own
    // ambient for objects — properly colored, not desaturated.
    float3 ambient = ambientColor.rgb * baseColor;

    // ---- Diffuse: paint color × lighting (paint MUST dominate) ----
    float3 layer1 = baseColor * kD * burleyDiff * sunContrib * shadowFactor + ambient;

    // ---- Environment Reflection: clearcoat Fresnel drives visibility ----
    // Car paint has a clearcoat — env reflections visible at all angles, stronger at grazing
    float3 R = reflect(-V, N);
    float2 envReflUV = SphereEnvMapUV(R, V);
    float3 envRefl = tex2D(envMapTex, envReflUV).rgb;
    float3 iblSample = tex2D(iblTex, envReflUV).rgb;
    // Blend env with subtle IBL tint for depth
    float3 iblBlend = lerp(envRefl, envRefl + iblSample * 0.08, 0.4);
    // Clearcoat Fresnel: F0=0.04 (dielectric), matches Glass shader approach
    float clearcoatFresnel = SchlickFresnelScalar(NdotV, 0.04);
    // Env reflection: 5% base (consistent with Glass) up to 20% at grazing
    float envMask = lerp(0.05, 0.20, clearcoatFresnel);
    // Metallic paints boost env reflection — metals are inherently reflective
    envMask = lerp(envMask, envMask * 1.5, metallicFactor);
    // Paint tinting: reflection tinted by paint color at normal incidence, white at grazing.
    // Real paint: light passes through clearcoat, reflects off base paint, gets tinted on exit.
    // At grazing angles Fresnel dominates and reflection becomes white (like a mirror).
    float3 reflTint = lerp(matCol.rgb, float3(1,1,1), clearcoatFresnel * 0.6);
    // Texture luminance mask: dark areas (tire rubber, dirt, holes, black textures) reflect less.
    // Physically correct — dark paint absorbs more light, so less is reflected back through clearcoat.
    float texReflMask = saturate(dot(diff.rgb, float3(0.2126, 0.7152, 0.0722)));
    texReflMask = max(texReflMask, 0.05);  // tiny floor to prevent zero
    float3 layer2 = iblBlend * envMask * reflTint * texReflMask;

    // ---- Specular: GGX/Smith for direct sun highlight ----
    float3 specTotal = float3(0, 0, 0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        float D = D_GGX(NdotH, roughness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, roughness);
        float3 F = F_Schlick(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * sunContrib;
        // Clearcoat specular: white dielectric highlight on top of paint
        float D_cc = D_GGX(NdotH, 0.05);  // very smooth clearcoat
        float Vis_cc = V_SmithCorrelated(NdotV, NdotL_sun, 0.05);
        float3 F_cc = F_Schlick(LdotH, float3(0.04, 0.04, 0.04));  // clearcoat F0
        specTotal += D_cc * F_cc * Vis_cc * NdotL_sun * sunContrib * 0.6;
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
            // Clearcoat per-light
            float D_cc_l = D_GGX(NdotH, 0.05);
            float Vis_cc_l = V_SmithCorrelated(NdotV, NdotL, 0.05);
            float3 F_cc_l = F_Schlick(LdotH, float3(0.04, 0.04, 0.04));
            specTotal += D_cc_l * F_cc_l * Vis_cc_l * NdotL * lightCol[i].rgb * 0.6;
        }
    }

    // ---- COMPOSITE ----
    // Paint color (baseColor) is the DOMINANT term. Everything else is additive tint.
    float3 color = layer1;                           // diffuse (paint × lighting) + ambient fill
    color += specTotal;                                // specular highlights (base + clearcoat)
    color += layer2;                                  // env reflection (clearcoat Fresnel-driven)

    // Output linear HDR — PostFX TonemapPass handles everything
    return float4(max(color, 0.0), diff.a);
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
//   c22 = {glossiness, reflectance, clearcoat, subsurface}
//   c23 = {specularTintR, specularTintG, specularTintB, 0}
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
    float3 specularTint = paintNoise.xyz; // c23 = {specTintR, specTintG, specTintB}

    // Normals
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    float3 L = length(IN.SunDir) > 1e-6 ? IN.SunDir / length(IN.SunDir) : float3(0, 0, -1);

    // Core PBR vectors
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    // F0: dielectric reflectance, optionally tinted by specular tint
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseColor * reflectance, specularTint.r);

    // Fresnel
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = 1.0 - kS;  // dielectric: all non-reflected energy is diffuse

    // NOTE: Ambient is already baked into IN.color.rgb by the VS
    // (OUT.Color = (prelight*surfDiff + ambient*surfAmb) * matCol).
    // baseColor = tex * IN.color already contains ambient.
    // DO NOT add ambientColor again — that creates ambient² and washes out blacks.
    // Industry precedent: Skyrim Community Shaders fixed identical double-ambient bug
    // by setting vertexColor=1 for PBR paths.

    // Direct light from timecycle (PS c5) — 1:1 match with ped pipeline
    float3 sunContrib = directCol.rgb;

    // Diffuse lighting (Burley)
    float3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);
    float diffuse = BurleyDiffuse(NdotL, NdotV, VdotH, roughness);

    // Specular (GGX/Smith)
    float NdotH = max(dot(N, H), 0.0);
    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
    float3 F = F_Schlick(max(dot(L, H), 0.0), F0);

    float3 specTotal = D * F * Vis * NdotL * sunContrib;

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
            specTotal += Dl * Fl * Visl * NdotL_l * lightCol[i].rgb;
        }
    }

    // IBL (ambient) — fallback to flat ambient when iblTex not bound
    float2 iblUV = N.xy * 0.5 + 0.5;
    float3 iblSample = tex2D(iblTex, iblUV).rgb;
    // SM3.0 returns black for unbound textures — use ambient as fill
    float3 ibl = (dot(iblSample, iblSample) > 1e-6) ? iblSample * 0.15 : baseColor * surfProps.x * 0.5;

    // Composite: vertex color AS diffuse (VS already baked ambient + directional),
    // plus PBR specular and IBL for per-pixel detail.
    // Xbox pipe: tex * vertexColor * 10.0 — vertex color IS the lighting.
    // Same approach: baseColor contains full lit result from VS, don't attenuate it.
    float3 color = baseColor;
    color += specTotal;
    color += ibl;

    // Output linear HDR — PostFX TonemapPass handles tonemapping uniformly
    return float4(max(color, 0.0), diff.a);
}

// ============================================================
// RUBBER/TIRE ENTRY POINT
// Entry: main_rubber
// Pure diffuse rubber — no specular, no Fresnel sheen (tires are matte)
// c22 = {roughness, F0, tintR, tintG}  (tireParams)
// c23 = {tintB, dirtLevel, wearFactor, 0}  (tireParams2)
// ============================================================
float4 main_rubber(PS_INPUT IN) : COLOR
{
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    float3 L = length(IN.SunDir) > 1e-6 ? IN.SunDir / length(IN.SunDir) : float3(0, 0, -1);

    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    // Same pattern as vehicle main: texture × paint color ONLY.
    // IN.color contains VS-baked ambient × lighting — using it doubles lighting (building pipe bug).
    float3 baseColor = diff.rgb * matCol.rgb;

    float roughness  = pbrParams.x;
    float dirtLevel  = paintNoise.y;
    float wearFactor = paintNoise.z;

    float3 dirtTint = float3(0.35, 0.25, 0.15);
    float3 wearTint = float3(0.55, 0.55, 0.50);
    baseColor = lerp(baseColor, dirtTint * baseColor, dirtLevel * 0.4);
    baseColor = lerp(baseColor, wearTint * baseColor, wearFactor * 0.3);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL_sun = max(dot(N, L), 0.0);

    // Ambient from timecycle — rubber needs fill light to be visible in shadows
    float3 ambient = ambientColor.rgb * baseColor;

    // Wrap diffuse — rubber scatters light broadly (subsurface wrap)
    float subsurface = 0.15;
    float wrapDiffuse = saturate((NdotL_sun + subsurface) / (1.0 + subsurface));
    float3 color = baseColor * wrapDiffuse * directCol.rgb + ambient;

    // Point lights — diffuse only, no specular
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdL = max(dot(N, Ll), 0.0);
        float wrap = saturate((NdL + subsurface) / (1.0 + subsurface));
        color += baseColor * wrap * lightCol[i].rgb * 0.15;
    }

    // No specular, no Fresnel sheen — rubber is matte
    return float4(color, diff.a);
}
