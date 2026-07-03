// PBR_Lighting.hlsl - Forward+ PBR Lighting Pass (ps_3_0)
// Reads G-buffer (albedo, normals, material ID) and applies BRDF lighting.
// Material properties come from BRDF library uploaded via constants.
// All output is clamped to [0,1] to prevent overbright.
//
// Constants:
//   c0 = {screenW, screenH, 1/screenW, 1/screenH}
//   c1 = (sunDirX, sunDirY, sunDirZ, sunIntensity)
//   c2 = (camPosX, camPosY, camPosZ, 0)
//   c3 = (roughness, reflectance, clearcoat, subsurface)
//   c4 = (specularInt, metalness, 0, 0)
//   c5 = (ambientR, ambientG, ambientB, ambientInt)
//   c6 = (nearClip, farClip, 0, 0)
//   c7 = (time, 0, 0, 0)
//
// Textures:
//   s0 = albedo (RGBA)
//   s1 = normals (RGB = world normal, A = depth or material ID)
//   s2 = depth buffer
//   s3 = environment map (for reflections)

sampler2D albedoTex   : register(s0);
sampler2D normalTex   : register(s1);
sampler2D depthTex    : register(s2);
sampler2D envMapTex   : register(s3);

float4 screenParams  : register(c0); // xy=res, zw=1/res
float4 sunDirection  : register(c1); // xyz=dir, w=intensity
float4 cameraPos     : register(c2); // xyz=cam pos
float4 brdfProps     : register(c3); // x=roughness, y=reflectance, z=clearcoat, w=subsurface
float4 brdfProps2    : register(c4); // x=specularInt, y=metalness
float4 ambientColor  : register(c5); // xyz=ambient, w=intensity
float4 clipPlanes    : register(c6); // xy=near, far
float4 timeParam     : register(c7); // x=time

static const float3 LUM = float3(0.299, 0.587, 0.114);
static const float PI = 3.14159265;

// GGX Normal Distribution
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith Correlated Visibility
float V_SmithCorrelated(float NdotV, float NdotL, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float GGXL = NdotL * NdotL * (1.0 - a2) + a2;
    float GGXV = NdotV * NdotV * (1.0 - a2) + a2;
    return 0.5 / (sqrt(GGXV) * sqrt(GGXL) + 1e-5);
}

// Schlick Fresnel
float3 F_Schlick(float cosTheta, float3 F0)
{
    float f = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * f;
}

// Linearize depth
float LinearizeDepth(float depth, float near, float far)
{
    return (2.0 * near) / (far + near - depth * (far - near));
}

// Simple hash for noise
float hash(float n) { return frac(sin(n) * 43758.5453); }
float noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = i.x + i.y * 57.0;
    return lerp(lerp(hash(n), hash(n + 1.0), f.x),
                lerp(hash(n + 57.0), hash(n + 58.0), f.x), f.y);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Sample G-buffer
    float4 albedo = tex2D(albedoTex, uv);
    float3 normal = tex2D(normalTex, uv).rgb * 2.0 - 1.0;
    float depth = tex2D(depthTex, uv).r;

    // Skip sky pixels
    if(depth >= 0.999)
        return albedo;

    // Reconstruct position from depth
    float linearDepth = LinearizeDepth(depth, clipPlanes.x, clipPlanes.y);

    // Vectors
    float3 N = normalize(normal);
    float3 V = normalize(cameraPos.xyz - float3(uv * screenParams.xy, linearDepth));
    float3 L = normalize(-sunDirection.xyz);

    // BRDF properties
    float roughness   = brdfProps.x;
    float reflectance = brdfProps.y;
    float clearcoat   = brdfProps.z;
    float subsurface  = brdfProps.w;
    float specularInt = brdfProps2.x;
    float metalness   = brdfProps2.y;

    // F0
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), albedo.rgb, metalness);

    // Core PBR
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);

    // Diffuse (Lambert)
    float3 kS = F_Schlick(LdotH, F0);
    float3 kD = (1.0 - kS) * (1.0 - metalness);
    float3 diffuse = kD * albedo.rgb / PI;

    // Specular (GGX)
    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
    float3 F = F_Schlick(LdotH, F0);
    float3 specular = D * F * Vis * specularInt;

    // Direct lighting
    float3 directLight = (diffuse + specular) * sunDirection.w * NdotL;

    // Ambient
    float3 ambient = albedo.rgb * ambientColor.rgb * ambientColor.w;

    // Subsurface approximation (wrap lighting)
    float3 subsurfaceLight = float3(0, 0, 0);
    if(subsurface > 0.0)
    {
        float wrapDiffuse = saturate((NdotL + subsurface) / (1.0 + subsurface));
        subsurfaceLight = albedo.rgb * wrapDiffuse * 0.3 * subsurface;
    }

    // Environment reflection (approximate)
    float3 R = reflect(-V, N);
    float2 envUV = R.xy * 0.5 + 0.5;
    float3 envColor = tex2D(envMapTex, envUV).rgb * 0.1;
    float3 envReflection = envColor * F_Schlick(NdotV, F0) * (1.0 - roughness);

    // Composite
    float3 color = directLight + ambient + subsurfaceLight + envReflection;

    // Final clamp - prevents overbright
    color = saturate(color);

    return float4(color, albedo.a);
}
