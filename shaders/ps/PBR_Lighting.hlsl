// PBR_Lighting.hlsl - Forward+ PBR Lighting Pass (ps_3_0)
// Reads G-buffer (albedo, normals, material ID) and applies BRDF lighting.
// Material properties come from BRDF library uploaded via constants.
// All output is clamped to [0,1] to prevent overbright.
//
// Constants:
//   c0 = {screenW, screenH, 1/screenW, 1/screenH}
//   c1 = (sunDirX, sunDirY, sunDirZ, sunIntensity)
//   c2 = (camPosX, camPosY, camPosZ, 0)
//   c3 = (glossiness, reflectance, clearcoat, subsurface)
//   c4 = (specularInt, 0, 0, 0)
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
float4 brdfProps     : register(c3); // x=glossiness, y=reflectance, z=clearcoat, w=subsurface
float4 brdfProps2    : register(c4); // x=specularInt
float4 ambientColor  : register(c5); // xyz=ambient, w=intensity
float4 clipPlanes    : register(c6); // xy=near, far
float4 timeParam     : register(c7); // x=time

static const float3 LUM = float3(0.299, 0.587, 0.114);
static const float PI = 3.14159265;

// Burley (Disney) Diffuse
float BurleyDiffuse(float NdotL, float NdotV, float VdotH, float roughness)
{
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float scatterL = lerp(1.0, fd90, pow(1.0 - NdotL, 5.0));
    float scatterV = lerp(1.0, fd90, pow(1.0 - NdotV, 5.0));
    return scatterL * scatterV * lerp(1.0, 1.0/1.51, roughness);
}

// Linearize depth
float LinearizeDepth(float depth, float near, float far)
{
    return (2.0 * near) / (far + near - depth * (far - near));
}

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
    float glossiness  = brdfProps.x;
    float roughness   = 1.0 - glossiness;  // convert glossiness → roughness internally
    float reflectance = brdfProps.y;
    float clearcoat   = brdfProps.z;
    float subsurface  = brdfProps.w;
    float specularInt = brdfProps2.x;

    // F0: explicit dielectric reflectance (no metalness)
    float3 F0 = float3(reflectance, reflectance, reflectance);

    // Core PBR
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Diffuse (Burley/Disney)
    float3 kS = F_Schlick(LdotH, F0);
    float3 kD = 1.0 - kS;  // dielectric: all non-reflected energy is diffuse
    float diffuseBRDF = BurleyDiffuse(NdotL, NdotV, VdotH, roughness);
    float3 diffuse = kD * albedo.rgb * diffuseBRDF;

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
