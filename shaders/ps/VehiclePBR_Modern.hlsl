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
float3 viewRight    : register(c25); // view matrix row 0 (world→view rotation)
float3 viewUp       : register(c26); // view matrix row 1
float3 viewFwd      : register(c27); // view matrix row 2
float4 skyParams    : register(c28); // xyz=skyTop color (0-1), w=skyReflect strength

// Forward+ clustered lights
sampler2D clusterTex : register(s5);   // 128x128 RGBA8 tile light index texture
float4 clusterParams : register(c45);  // (gridOffsetX, gridOffsetZ, tileSize, lightCount)
float4 layerCfg   : register(c46);  // x = vehPBRLayers layer bitmask
float4 clusterLightPos[8] : register(c29);  // (pos.x, pos.y, pos.z, radius) per light
float4 clusterLightCol[8] : register(c37);  // (col.r*intensity, col.g*intensity, col.b*intensity, 0) per light

float whiteNoise(float2 p){
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// PBR layer toggle decode: 1 when the given bit is set in c46.x, else 0.
// bit0=base,1=env,2=spec,3=rim,4=ibl,5=sky,6=clearcoat,7=normalbuf
float LF(float bit){
    float v = floor(layerCfg.x / exp2(bit));
    return frac(v * 0.5) * 2.0;
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

    // ---- Base color: game lighting from VS (matches building pipe) ----
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    // IN.color.rgb = VS-baked game lighting (ambient + 7 directional × matCol).
    // Same approach as main_building: vertex color IS the diffuse lighting.
    // Our PBR specular/env/reflection layers sit on top of this.
    float3 baseColor = diff.rgb * IN.color.rgb;

    // Blend with screen-space normal (if available)
    // ambientColor.w > 0 means normal buffer is bound and valid
    if(LF(7) > 0.5 && ambientColor.w > 0.5){
        float3 ssNormal = tex2D(normalBufTex, IN.texcoord0).rgb * 2.0 - 1.0;
        if(dot(ssNormal, ssNormal) > 0.25)
            N = normalize(lerp(N, ssNormal, 0.3));
    }

    // ---- PBR material properties (KHR_materials_pbrSpecularGlossiness workflow) ----
    // Everything is dielectric. No metalness.
    // pbrParams.x = glossiness (1=smooth/shiny, 0=rough) — PRIMARY variable
    // pbrParams.y = specular reflectance (F0 at normal incidence, ~0.04 for dielectrics)
    // pbrParams.z = specular color tint (0=white, 1=tinted by paint)
    // pbrParams.w = envFresnel
    // glTF KHR_materials_pbrSpecularGlossiness:
    //   α = (1 - glossiness)^2  — our D_GGX/V_SmithCorrelated square internally,
    //   so we pass (1 - glossiness) as σ, giving α² correctly.
    //   c_diff = diffuse * (1 - max(specular.r, specular.g, specular.b))
    float glossiness  = pbrParams.x;
    float specularF0  = pbrParams.y;
    float specTint    = pbrParams.z;
    float envFresnel  = pbrParams.w;

    // Wheel roughness override: reduce glossiness for wheels (tires are rough)
    float isWheel = paintNoise.x;
    float wheelNoise = whiteNoise(IN.texcoord0 * 47.0);
    glossiness = lerp(glossiness, 0.85, isWheel * wheelNoise * 0.5);

    // (paintNoise.y/z = noiseScale/edgeBlend, reserved for future reflection breakup)

    // ---- Paint tint from carcols ----
    float3 paintTint = matCol.rgb;

    // ---- Core PBR vectors ----
    float NdotV = max(dot(N, V), 0.0);

    // F0: dielectric reflectance, optionally tinted by paint color
    float3 F0 = lerp(float3(specularF0, specularF0, specularF0), paintTint * specularF0, specTint);

    // Energy conservation for specular ONLY — diffuseScale is NOT applied to
    // baseColor because IN.color.rgb already has game lighting baked in.
    // Applying it to vertex-lit color would darken/wash out the paint.
    float metallicFactor = saturate((specularF0 - 0.5) * 2.0);

    // Fresnel for specular energy split (kD not used — diffuse comes from VS vertex color)
    float3 F_atNdotV = F_Schlick(NdotV, F0);

    // ---- Diffuse: VS vertex color already has full game lighting ----
    // Ambient + 7 directional lights baked into IN.color.rgb by the VS.
    // PBR specular and env reflection layer ON TOP of game-lit base.
    // Matches building pipe approach (main_building line 366).
    float3 layer1 = baseColor * LF(0);

    // Sun direction for specular (from PS c12, same as buildings)
    float3 sunContrib = directCol.rgb;
    float NdotL_sun = max(dot(N, L), 0.0);

    // ---- Environment Reflection: clearcoat Fresnel drives visibility ----
    // Car paint has a clearcoat — env reflections visible at all angles, stronger at grazing
    float3 R_world = reflect(-V, N);
    // The env map is a PERSPECTIVE render from the camera's viewpoint (60m clip),
    // so sample it by projecting the world-space reflection vector with the main
    // camera's view window (c3.zw = tanHalfFovX/Y). Upward faces now reflect the
    // sky (top half of the env render) and side faces reflect the world, instead
    // of the geometrically-arbitrary patches the sphere-map formula produced.
    float3 R_view = float3(dot(R_world, viewRight), dot(R_world, viewUp), dot(R_world, viewFwd));
    float rz = max(R_view.z, 0.05);
    float2 envReflUV = saturate(float2(
        0.5 + 0.5 * clamp(R_view.x / (rz * iblParams.z), -1.2, 1.2),
        0.5 - 0.5 * clamp(R_view.y / (rz * iblParams.w), -1.2, 1.2)));
    float3 envRefl = tex2D(envMapTex, envReflUV).rgb;
    float3 iblSample = tex2D(iblTex, envReflUV).rgb;
    // Blend env with subtle IBL tint for depth
    float3 iblBlend = lerp(envRefl, envRefl + iblSample * 0.08 * LF(4), 0.4);
    // Sky contribution: upward-facing surfaces reflect sky color from the top of the sphere map
    float skyBlend = saturate(N.y) * skyParams.w;
    iblBlend = lerp(iblBlend, skyParams.rgb, skyBlend * 0.3 * LF(5));
    // Clearcoat Fresnel: carcols shininess drives env gloss intensity
    // fxParams.w = envData->GetShininess() * 8 * envShininessMult — the same carcols
    // value the VS bakes into IN.envColor.a (which the glass shader reads).
    // fxParams.y is envPower (≈20) and saturates to 1.0 — it must NOT drive gloss.
    float clearcoatFresnel = SchlickFresnelScalar(NdotV, 0.04);
    float carcolsShine = saturate(fxParams.w);  // 0..1 normalized carcols shininess
    // Gloss strength: visible env reflection at all angles (0.45 base), stronger
    // at grazing. v3 (0.10 base × 1.5 boost × 0.15 grazing weight) put face-on
    // env at ~2% — invisible. v1 (0.25-0.8 × 2.0 × 0.5) white-washed dark paint.
    // This sits between: ~0.2 added face-on on a 0.3 paint, ~0.6 at grazing.
    // Paint tinting: reflection tinted by paint color at normal incidence, white at grazing.
    float3 reflTint = lerp(matCol.rgb, float3(1,1,1), clearcoatFresnel);
    float3 envTerm = iblBlend * reflTint * 1.2;
    // Energy-conserving clearcoat blend (carcols-shade safe):
    // paint dominates face-on (kr ~0.15), world mirrors at grazing (kr -> ~0.7+).
    // carcols shininess widens the reflection band for shiny paints.
    // Pure additive env (previous versions) always lifted/washed the paint.
    float kr = saturate(clearcoatFresnel * (1.0 + 2.0 * carcolsShine) + 0.05) * LF(1);
    kr = lerp(kr, saturate(kr * 1.5), metallicFactor);
    float3 layer2 = lerp(layer1, envTerm, kr);

    // ---- Specular: GGX/Smith for direct sun highlight ----
    // glTF KHR_materials_pbrSpecularGlossiness: D_GGX and V_SmithCorrelated
    // take σ = (1 - glossiness) and square internally to get α = σ²
    float3 specTotal = float3(0, 0, 0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        float D = D_GGX(NdotH, 1.0 - glossiness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, 1.0 - glossiness);
        float3 F = F_SchlickLH(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * sunContrib;
        // Clearcoat specular: white dielectric highlight on top of paint
        float D_cc = D_GGX(NdotH, 0.25);  // very smooth clearcoat
        float Vis_cc = V_SmithCorrelated(NdotV, NdotL_sun, 0.25);
        float3 F_cc = F_SchlickLH(LdotH, float3(0.04, 0.04, 0.04));  // clearcoat F0
        specTotal += D_cc * F_cc * Vis_cc * NdotL_sun * sunContrib * 0.6 * LF(6);
    }
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float3 H = normalize(V + Ll);
        float NdotL = max(dot(N, Ll), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(Ll, H), 0.0);
        if(NdotL > 0.0){
            float D = D_GGX(NdotH, 1.0 - glossiness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, 1.0 - glossiness);
            float3 F = F_SchlickLH(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb;
            // Clearcoat per-light
            float D_cc_l = D_GGX(NdotH, 0.25);
            float Vis_cc_l = V_SmithCorrelated(NdotV, NdotL, 0.25);
            float3 F_cc_l = F_SchlickLH(LdotH, float3(0.04, 0.04, 0.04));
            specTotal += D_cc_l * F_cc_l * Vis_cc_l * NdotL * lightCol[i].rgb * 0.6 * LF(6);
        }
    }

    // ---- Clustered Point Lights (Forward+) ----
    if(clusterParams.w > 0.0) {  // lightCount > 0
        float2 tileUV = (IN.WorldPos.xz - clusterParams.xy) / clusterParams.z;
        tileUV = clamp(tileUV, 0.0, 1.0);
        float4 clusterSample = tex2D(clusterTex, tileUV);
        
        int indices[4];
        indices[0] = (int)(clusterSample.r * 255.0 + 0.5);
        indices[1] = (int)(clusterSample.g * 255.0 + 0.5);
        indices[2] = (int)(clusterSample.b * 255.0 + 0.5);
        indices[3] = (int)(clusterSample.a * 255.0 + 0.5);
        
        [unroll] for(int ci = 0; ci < 4; ci++) {
            if(indices[ci] <= 0) continue;
            int li = indices[ci] - 1;  // 0-based index (0 in texture = no light)
            if(li < 0 || li >= 8) continue;
            
            float4 clPos = clusterLightPos[li];
            float4 clCol = clusterLightCol[li];
            float3 ToLight = clPos.xyz - IN.WorldPos.xyz;
            float dist = length(ToLight);
            float radius = clPos.w;
            
            if(dist < radius && dist > 1e-6) {
                float3 Lc = ToLight / dist;
                float atten = saturate(1.0 - dist / radius);
                atten *= atten;  // quadratic falloff
                float NdotLc = max(dot(N, Lc), 0.0);
                if(NdotLc > 0.0) {
                    float3 Hc = normalize(V + Lc);
                    float NdotHc = max(dot(N, Hc), 0.0);
                    float LdotHc = max(dot(Lc, Hc), 0.0);
                    float Dc = D_GGX(NdotHc, 1.0 - glossiness);
                    float Visc = V_SmithCorrelated(NdotV, NdotLc, 1.0 - glossiness);
                    float3 Fc = F_SchlickLH(LdotHc, F0);
                    specTotal += Dc * Fc * Visc * NdotLc * clCol.rgb * atten;
                    // Clearcoat per-cluster-light
                    float Dc_cc = D_GGX(NdotHc, 0.05);
                    float Visc_cc = V_SmithCorrelated(NdotV, NdotLc, 0.05);
                    float3 Fc_cc = F_SchlickLH(LdotHc, float3(0.04, 0.04, 0.04));
                    specTotal += Dc_cc * Fc_cc * Visc_cc * NdotLc * clCol.rgb * atten * 0.6;
                }
            }
        }
    }

    // Edge highlight: subtle Fresnel rim catches sun at grazing angles.
    // Reduced from 0.45 — game lighting already has edge detail from VS.
    float rimFresnel = pow(1.0 - saturate(NdotV), 2.0);
    float3 rimLight = sunContrib * rimFresnel * 0.18 * LF(3);

    specTotal *= 1.5 * LF(2);  // visible sun glints

    // ---- COMPOSITE ----
    // layer1 = VS game lighting (ambient + 7 directional × matCol, matches building pipe)
    // layer2 = layer1 + additive env gloss (glass-style: env × intensity, added on top)
    // specTotal and rimLight are additive on top
    float3 color = layer2;
    color += specTotal;                                // specular highlights (base + clearcoat)
    color += rimLight;                                 // Fresnel rim on top of clearcoat

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

    float2 ReflPos = (length(IN.texcoord1.xy) > 1e-6 ? normalize(IN.texcoord1.xy) : float2(0, 0)) * (IN.texcoord1.z * 0.5 + 0.5);
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
    // Compute view-space reflection vector for camera-rendered sphere map
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = float3(0, 0, 1);  // approximate view dir in view space
    float3 R_world = reflect(-V, N);
    float3 R_view = float3(dot(R_world, viewRight), dot(R_world, viewUp), dot(R_world, viewFwd));
    float2 envUV = SphereEnvMapUV(R_view, V);
    float4 env = tex2D(envMapTex, envUV);
    // Lerp-replace: env replaces diffuse at grazing angles (clearcoat behavior)
    float envBlend = saturate(IN.envColor.a * 0.8);
    float3 color = lerp(diff.rgb, env.rgb * IN.envColor.rgb, envBlend);
    color.rgb = saturate(color.rgb);
    return float4(color.rgb, diff.a);
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

    // Wet roads recovery: dayparam[3] = CWeather__WetRoads (0-1)
    // Passed via vertex color alpha from buildingPipe.cpp setDnParams()
    float wetRoads = IN.dayNight.a;

    // Wet surfaces: increase reflectance, decrease roughness, darken diffuse
    // This simulates water filling surface micro-structure (beta restoration)
    if(wetRoads > 0.01){
        // Wet surfaces are smoother (water fills gaps)
        roughness *= lerp(1.0, 0.3, wetRoads);
        // Wet surfaces are more reflective (higher F0)
        reflectance = lerp(reflectance, 0.04, wetRoads);  // water F0 = 0.04
        // Wet surfaces darken slightly (water absorption)
        baseColor *= lerp(1.0, 0.85, wetRoads);
        // Increase glossiness for wet surfaces
        glossiness = lerp(glossiness, 0.8, wetRoads);
    }

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

    // ---- Clustered Point Lights (Forward+) ----
    float3 clusterDiffuse = float3(0, 0, 0);
    if(clusterParams.w > 0.0) {
        float2 tileUV = (IN.WorldPos.xz - clusterParams.xy) / clusterParams.z;
        tileUV = clamp(tileUV, 0.0, 1.0);
        float4 clusterSample = tex2D(clusterTex, tileUV);
        
        int indices[4];
        indices[0] = (int)(clusterSample.r * 255.0 + 0.5);
        indices[1] = (int)(clusterSample.g * 255.0 + 0.5);
        indices[2] = (int)(clusterSample.b * 255.0 + 0.5);
        indices[3] = (int)(clusterSample.a * 255.0 + 0.5);
        
        [unroll] for(int ci = 0; ci < 4; ci++) {
            if(indices[ci] <= 0) continue;
            int li = indices[ci] - 1;
            if(li < 0 || li >= 8) continue;
            
            float4 clPos = clusterLightPos[li];
            float4 clCol = clusterLightCol[li];
            float3 ToLight = clPos.xyz - IN.WorldPos.xyz;
            float dist = length(ToLight);
            float radius = clPos.w;
            
            if(dist < radius && dist > 1e-6) {
                float3 Lc = ToLight / dist;
                float atten = saturate(1.0 - dist / radius);
                atten *= atten;
                float NdotLc = max(dot(N, Lc), 0.0);
                if(NdotLc > 0.0) {
                    float3 Hc = normalize(V + Lc);
                    float NdotHc = max(dot(N, Hc), 0.0);
                    float LdotHc = max(dot(Lc, Hc), 0.0);
                    float Dc = D_GGX(NdotHc, roughness);
                    float Visc = V_SmithCorrelated(NdotV, NdotLc, roughness);
                    float3 Fc = F_Schlick(LdotHc, F0);
                    specTotal += Dc * Fc * Visc * NdotLc * clCol.rgb * atten;
                    // Diffuse from cluster lights (Burley)
                    float VdotHc = max(dot(V, Hc), 0.0);
                    float diffc = BurleyDiffuse(NdotLc, NdotV, VdotHc, roughness);
                    clusterDiffuse += baseColor * diffc * clCol.rgb * atten * kD / 3.14159;
                }
            }
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
    color += clusterDiffuse;

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
    float3 baseColor = diff.rgb * matCol.rgb;

    float roughness  = pbrParams.x;
    float dirtLevel  = paintNoise.y;
    float wearFactor = paintNoise.z;

    // Wet mud DARKENS tires (not brightens). Dry dust is the only brightener.
    float3 wetMud = float3(0.15, 0.12, 0.10);  // dark wet mud
    float3 dryDust = float3(0.35, 0.30, 0.25);  // light dry dust
    baseColor = lerp(baseColor, wetMud, dirtLevel * 0.5);    // wet mud darkens
    baseColor = lerp(baseColor, dryDust * baseColor, wearFactor * 0.15);  // dry dust lightens slightly

    float NdotV = max(dot(N, V), 0.0);
    float NdotL_sun = max(dot(N, L), 0.0);

    // Ambient: rubber is dark and matte — reduce ambient to 35% of timecycle value
    // so tires don't glow brighter than hub caps or wheel wells
    float3 ambient = ambientColor.rgb * baseColor * 0.35;

    // Wrap diffuse — tighter wrap than before, rubber shouldn't scatter this much
    float subsurface = 0.08;
    float wrapDiffuse = saturate((NdotL_sun + subsurface) / (1.0 + subsurface));
    float3 color = baseColor * wrapDiffuse * directCol.rgb + ambient;

    // Point lights — reduced contribution, rubber should stay dark
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdL = max(dot(N, Ll), 0.0);
        float wrap = saturate((NdL + subsurface) / (1.0 + subsurface));
        color += baseColor * wrap * lightCol[i].rgb * 0.08;
    }

    return float4(color, diff.a);
}
