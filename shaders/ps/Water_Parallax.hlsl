// Water_Parallax.hlsl - Procedural PBR water (ps_3_0)
// Fully procedural — no texture normal maps needed.
// Noise-based normals, depth refraction, PBR specular.
// Tied to sky system via constants.
//
// c0 = (time, waveSpeed, foamThreshold, foamSoftness)
// c1 = (normalStrength, parallaxScale, fresnelPower, 0)
// c2 = (specularPower, specularIntensity, absorptionDensity, 0)
// c3 = (shallowR, shallowG, shallowB, 0)
// c4 = (deepR, deepG, deepB, 0)
// c5 = (screenW, screenH, 1/screenW, 1/screenH)
// c6 = (sunDirX, sunDirY, sunDirZ, sunBrightness)
// c7 = (camPosX, camPosY, camPosZ, 0)
// c8 = (nearClip, farClip, 0, 0)
//
// s0 = scene color (refraction capture)

sampler2D sceneTex : register(s0);

uniform float4 timeParams     : register(c0);
uniform float4 waterParams    : register(c1);
uniform float4 specParams     : register(c2);
uniform float4 shallowColor   : register(c3);
uniform float4 deepColor      : register(c4);
uniform float4 screenSize     : register(c5);
uniform float4 sunDir         : register(c6);
uniform float4 camPos         : register(c7);
uniform float4 clipPlanes     : register(c8);

static const float PI = 3.14159265;

struct VS_OUTPUT {
    float4 position  : POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 worldPos  : TEXCOORD1;
    float3 viewDir   : TEXCOORD2;
    float3 lightDir  : TEXCOORD3;
    float4 screenPos : TEXCOORD4;
    float4 waveParams: TEXCOORD5;
};

// Hash-based noise — no texture dependency
float hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float noise2d(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + float2(1.0, 0.0));
    float c = hash(i + float2(0.0, 1.0));
    float d = hash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p)
{
    float f = 0.0;
    f += 0.5000 * noise2d(p); p *= 2.03;
    f += 0.2500 * noise2d(p); p *= 2.01;
    f += 0.1250 * noise2d(p); p *= 2.02;
    f += 0.0625 * noise2d(p);
    return f;
}

// Compute normal from procedural heightfield
float3 computeWaterNormal(float2 uv, float time, float strength)
{
    float eps = 0.005;
    float t0 = time * 0.4;
    float t1 = time * 0.25;

    float h0 = fbm(uv * 8.0 + float2(t0, t1));
    float hx = fbm((uv + float2(eps, 0)) * 8.0 + float2(t0, t1));
    float hy = fbm((uv + float2(0, eps)) * 8.0 + float2(t0, t1));

    float3 n = float3((h0 - hx) / eps, (h0 - hy) / eps, 1.0);
    n.xy *= strength;
    return normalize(n);
}

// Second noise layer at different scale
float3 computeWaterNormal2(float2 uv, float time, float strength)
{
    float eps = 0.008;
    float t0 = time * -0.3;
    float t1 = time * 0.18;

    float h0 = fbm(uv * 4.0 + float2(t0, t1));
    float hx = fbm((uv + float2(eps, 0)) * 4.0 + float2(t0, t1));
    float hy = fbm((uv + float2(0, eps)) * 4.0 + float2(t0, t1));

    float3 n = float3((h0 - hx) / eps, (h0 - hy) / eps, 1.0);
    n.xy *= strength * 0.5;
    return normalize(n);
}

float LinearizeDepth(float depth, float near, float far)
{
    return (2.0 * near) / (far + near - depth * (far - near));
}

float4 main(VS_OUTPUT IN) : COLOR
{
    float time = timeParams.x * timeParams.y;
    float2 worldUV = IN.worldPos.xy * 0.015;

    // ---- Procedural normals (dual layer) ----
    float3 N1 = computeWaterNormal(worldUV, time, waterParams.x);
    float3 N2 = computeWaterNormal2(worldUV, time, waterParams.x);
    float3 N = normalize(float3(
        N1.xy + N2.xy,
        N1.z * N2.z
    ));

    // ---- Vectors ----
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // ---- Screen UV ----
    float2 screenUV = IN.screenPos.xy / IN.screenPos.w;
    screenUV.y = 1.0 - screenUV.y;

    // ---- Refraction ----
    float2 refractOffset = N.xy * 0.02;
    float2 refractUV = screenUV + refractOffset;
    refractUV = clamp(refractUV, float2(0.001, 0.001), float2(0.999, 0.999));
    float3 sceneColor = tex2D(sceneTex, refractUV).rgb;

    // ---- Water color blend ----
    // Approximate depth from screen position (brighter = deeper)
    float approxDepth = saturate(1.0 - screenUV.y * 0.3);
    float3 waterColor = lerp(shallowColor.rgb, deepColor.rgb, approxDepth);

    // Mix scene refraction with water color
    float3 refracted = lerp(sceneColor, waterColor, 0.5);

    // ---- Fresnel (Schlick, IOR 1.333, F0=0.02) ----
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // ---- Sky reflection ----
    float3 R = reflect(-V, N);
    float skyBlend = saturate(R.y * 0.5 + 0.5);
    float3 skyReflect = lerp(float3(0.5, 0.6, 0.8), float3(0.2, 0.4, 0.7), skyBlend);

    // ---- Specular (GGX) ----
    float roughness = 0.02;
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float Vis = NdotL * 0.25;
    float3 specular = D * Vis * specParams.y * float3(1, 1, 1);

    // ---- Sun hotspot ----
    float sunSpec = pow(max(dot(reflect(-V, N), L), 0.0), 512.0) * 2.0;

    // ---- Composite ----
    float3 output = lerp(refracted, skyReflect, fresnel * 0.7);
    output += specular;
    output += float3(1.0, 0.95, 0.9) * sunSpec;

    // Foam at wave peaks
    float waveFoam = saturate(IN.waveParams.w);
    float foamNoise = fbm(worldUV * 12.0 + time * 0.5);
    float foam = smoothstep(0.6, 0.8, foamNoise) * waveFoam * 0.3;
    output += float3(0.9, 0.95, 1.0) * foam;

    output = saturate(output);

    return float4(output, 0.8);
}
