/*===================================================================================
SkyGFX Plus - Vehicle PBR Modern Shader (ps_3_0)
Uses envCarVS vertex lighting for diffuse (matching original envCarPS approach).
Adds O3DE-style PBR specular (GGX/Smith/Schlick) on top.
Sphere map env projection matching envCarPS exactly.
Based on CloudWorks noise by Brian Tu (keroroxzz), CC BY-NC-SA 3.0
===================================================================================*/

sampler2D diffuseTex : register(s0);
sampler2D envMapTex  : register(s1);
sampler2D maskTex    : register(s2);
sampler2D iblTex     : register(s3);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);
float3 eyePos      : register(c2);
float4 iblParams   : register(c3);
float4 cloudShadow : register(c4);
float4 directCol   : register(c5);
float4 lightCol[6] : register(c6);
float3 directDir   : register(c12);
float3 lightDir[6] : register(c13);
float4 matCol      : register(c19);

// PBR params at c22 (avoid overwriting c0/c5)
float4 pbrParams   : register(c22); // {roughness, metalness, reflectance, envFresnel}

struct PS_INPUT
{
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

// ---- Cloud shadow noise (CloudWorks by Brian Tu) ----
static float cnoiseSeed = 1618.03398875;
float chash(float n) { return frac(sin(n / 1873.1873) * cnoiseSeed); }
float cnoise3d(float3 p) {
    float3 fr = floor(p); float3 ft = frac(p);
    float n = 1153.0 * fr.x + 2381.0 * fr.y + fr.z;
    float v  = lerp(chash(n), chash(n + 1.0), ft.z);
    float vr = lerp(chash(n + 1153.0), chash(n + 1154.0), ft.z);
    float vd = lerp(chash(n + 2381.0), chash(n + 2382.0), ft.z);
    float vo = lerp(chash(n + 3534.0), chash(n + 3535.0), ft.z);
    return lerp(lerp(v, vr, ft.x), lerp(vd, vo, ft.x), ft.y);
}
float cloudFBM(float3 p) {
    float f = 0.0;
    f += 0.5000 * cnoise3d(p); p *= 2.01;
    f += 0.2500 * cnoise3d(p); p *= 2.02;
    f += 0.1250 * cnoise3d(p); p *= 2.03;
    f += 0.0625 * cnoise3d(p);
    return f;
}

// ---- PBR BRDF (O3DE Enhanced / Disney Principled) ----

float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

float V_SmithCorrelated(float NdotV, float NdotL, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float GGXL = NdotL * NdotL * (1.0 - a2) + a2;
    float GGXV = NdotV * NdotV * (1.0 - a2) + a2;
    return 0.5 / (sqrt(GGXV) * sqrt(GGXL) + 1e-5);
}

float3 F_Schlick(float cosTheta, float3 F0)
{
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * fresnel;
}

float4 main(PS_INPUT IN) : COLOR
{
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.WorldPos - eyePos);

    // ---- Diffuse: use vertex lighting from envCarVS (game-correct) ----
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * IN.color.rgb;

    // ---- Cloud shadow on diffuse ----
    float3 noisePos = float3(IN.WorldPos.xy * 0.0008, IN.WorldPos.z * 0.0004);
    float cloudNoise = cloudFBM(noisePos);
    cloudNoise = smoothstep(0.3, 0.7, cloudNoise);
    float shadowFactor = lerp(0.6, 1.0, 1.0 - cloudNoise * 0.4);
    baseColor *= shadowFactor;

    // ---- PBR material properties ----
    float roughness   = pbrParams.x;
    float metalness   = pbrParams.y;
    float reflectance = pbrParams.z;
    float envFresnel  = pbrParams.w;

    float NdotV = max(dot(N, V), 0.0);
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseColor, metalness);

    // ---- PBR specular (GGX/Smith/Schlick) from main sun + extra lights ----
    float3 specTotal = float3(0, 0, 0);

    // Sun
    {
        float3 L = -directDir;
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        if(NdotL > 0.0){
            float D = D_GGX(NdotH, roughness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
            float3 F = F_Schlick(LdotH, F0);
            specTotal += D * F * Vis * NdotL * directCol.rgb;
        }
    }

    // Extra lights (up to 6)
    for(int i = 0; i < 6; i++){
        float3 L = -lightDir[i];
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        if(NdotL > 0.0){
            float D = D_GGX(NdotH, roughness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
            float3 F = F_Schlick(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb;
        }
    }

    // ---- Env map reflection (sphere map matching envCarPS) ----
    float3 envDir = -float3(reflect(-V, N)); // negate to match envCarPS ReflVector = V - 2*dot(V,N)*N
    float2 envXY = envDir.xy;
    float lenXY = length(envXY);
    float2 envUV;
    if(lenXY > 1e-6)
        envUV = (envXY / lenXY) * (envDir.z * 0.5 + 0.5);
    else
        envUV = float2(0.0, 0.0);
    envUV = envUV * float2(0.5, -0.5) + float2(0.5, 0.5);

    float4 env = tex2D(envMapTex, envUV) * fxParams.z;

    // Reflection mask + fresnel
    float4 mask = tex2D(maskTex, IN.texcoord0);
    float3 envF = F_Schlick(NdotV, F0);
    float reflStrength = mask.r * envF.x * envFresnel;
    float3 envpass = env.rgb * reflStrength;

    // ---- IBL ambient fill ----
    float2 iblUV = N.xy * 0.5 + 0.5;
    float4 iblSample = tex2D(iblTex, iblUV);
    float3 iblFill = iblSample.rgb * 0.06;

    // ---- Combine ----
    // Diffuse (vertex-lit) + PBR specular + env reflection + IBL ambient
    float3 finalColor = baseColor + specTotal + envpass + iblFill;

    return float4(finalColor, diff.a);
}
