/*===================================================================================
SkyGFX Plus - Vehicle PBR Modern Shader (ps_3_0)
Energy-conserving PBR with multi-light Fresnel and surface noise.

Energy Conservation:
  kS = Fresnel (portion of light that reflects)
  kD = 1 - kS (portion that goes to diffuse)
  For metals: kD = 0 (all energy to specular/reflection)
  diffuse = kD * baseColor * light
  specular = D * F * Vis * light
  reflection = envMap * F * envIntensity
  The sum never exceeds incoming light energy.

Surface noise: blends between clean and blurred reflection.
  Edges of panels are rougher, centers smoother.
  Noise controlled from C++ via c23.yz.

Based on CloudWorks noise by Brian Tu (keroroxzz), CC BY-NC-SA 3.0
===================================================================================*/

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
float4 pbrParams   : register(c22); // {roughness, metalness, reflectance, envFresnel}
float4 paintNoise  : register(c23); // x=wheel, y=noiseScale, z=edgeBlend

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

    // Blend with screen-space normal
    float3 ssNormal = tex2D(normalBufTex, IN.texcoord0).rgb * 2.0 - 1.0;
    if(length(ssNormal) > 0.5)
        N = normalize(lerp(N, ssNormal, 0.3));

    // ---- PBR material properties ----
    float roughness   = pbrParams.x;
    float metalness   = pbrParams.y;
    float reflectance = pbrParams.z;
    float envFresnel  = pbrParams.w;

    // Wheel metallic white noise
    float isWheel = paintNoise.x;
    float wheelNoise = whiteNoise(IN.texcoord0 * 47.0);
    metalness = saturate(metalness + isWheel * wheelNoise * 0.6);
    roughness = lerp(roughness, 0.2, isWheel * wheelNoise * 0.4);

    // ---- Surface noise for reflection breakup ----
    float noiseScale = paintNoise.y;
    float edgeBlend  = paintNoise.z;
    float rawNoise = smoothNoise(IN.WorldPos * 0.5);

    // ---- Paint tint from carcols ----
    float3 paintTint = matCol.rgb;
    float paintType = paintNoise.w;

    // ---- Core PBR vectors ----
    float NdotV = max(dot(N, V), 0.0);

    // ================================================================
    // CARCOLS = PAINT COLOR
    // The texture IS the paint color. Carcols IS the paint color.
    // Energy conservation handled in post-effects, not here.
    // ================================================================

    // Diffuse base = carcols color (the paint)
    // Texture modulates it for surface detail (scratches, dirt)
    float3 paintColor = paintTint;

    // F0 for Fresnel: dielectric uses grey, metallic uses carcols
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), paintColor, metalness);

    // Fresnel for reflection blending (no kD darkening — energy conserved in post)
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;

    // ---- Cloud shadow ----
    float3 noisePos = float3(IN.WorldPos.xy * 0.0008, IN.WorldPos.z * 0.0004);
    float cloudNoise = PBR_cloudFBM(noisePos);
    cloudNoise = smoothstep(0.3, 0.7, cloudNoise);
    float shadowFactor = lerp(0.6, 1.0, 1.0 - cloudNoise * 0.4);

    // ---- LAYER 1: Diffuse = texture × vertex color ----
    // Vertex color already includes matCol from envCarVS (OUT.Color * matCol)
    // No need to multiply by paintColor again — that would double-apply carcols
    // Energy conservation handled in post-effects
    float3 layer1 = diff.rgb * IN.color.rgb * shadowFactor;

    // ================================================================
    // MULTI-LIGHT FRESNEL ACCUMULATION (for env reflection blending)
    // ================================================================
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

    // ================================================================
    // LAYER 2: ENVIRONMENT REFLECTION (energy-conserved)
    // Reflection = envMap * F * envIntensity
    // F controls how much light reflects (edges reflect more)
    // ================================================================
    float3 R = reflect(-V, N);
    float4 mask = tex2D(maskTex, IN.texcoord0);

    float edgeFactor = pow(1.0 - NdotV, 2.0);
    float blendAmount = saturate(rawNoise * edgeFactor * noiseScale);

    float2 cleanUV = SphereEnvMapUV(R, V);
    float4 cleanEnv = tex2D(envMapTex, cleanUV) * fxParams.z;
    float2 noisyUV = SphereEnvMapUV(R, V) + (rawNoise - 0.5) * 0.02;
    float4 noisyEnv = tex2D(envMapTex, noisyUV) * fxParams.z;
    float4 env = lerp(cleanEnv, noisyEnv, blendAmount);

    // Energy-conserved reflection: env * Fresnel * mask
    float3 layer2 = env.rgb * paintColor * kS * mask.r * envFresnel;

    // ================================================================
    // FRESNEL RIM
    // ================================================================
    float rimStrength = pow(1.0 - NdotV, 3.0) * envFresnel * 0.3;
    float3 fresnelRim = F_Schlick(NdotV, F0) * paintColor * rimStrength;

    // ================================================================
    // LAYER 3: PBR SPECULAR (GGX/Smith/Schlick)
    // ================================================================
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

    float3 sunContrib = ComputeSunContribution(N, V, L, F0, NdotL_sun);

    // ---- IBL fill ----
    float2 iblUV = N.xy * 0.5 + 0.5;
    float3 layer4 = tex2D(iblTex, iblUV).rgb * 0.06;

    // ================================================================
    // COMPOSITE — energy-conserved layers
    // ================================================================
    float3 color = float3(0, 0, 0);
    color += layer1;                        // diffuse (kD applied — dark stays dark)
    color += layer2;                        // reflection (kS applied — energy-conserved)
    color += fresnelRim;                    // rim glow
    color += specTotal;                     // specular
    color += sunContrib;                    // sunspot
    color += layer4;                        // IBL

    return float4(color, diff.a);
}
