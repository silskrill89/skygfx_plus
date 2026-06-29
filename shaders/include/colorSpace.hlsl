// colorSpace.hlsl - Shared math for unified pipeline
// Gamma, grading, depth reconstruction, edge detect, shadow merge, surface weights, IBL

#ifndef COLORSPACE_INCLUDED
#define COLORSPACE_INCLUDED

// --- Constants ---
// c0-c3: reserved per-pass
// c28: satBoost, iblTintStrength, 0, 0
// c29-c31: grading matrix (3x4, rows packed as float4)
// c32: vehicleColour (r,g,b, 0)
// c33: avgCarColour (r,g,b, 0) for edge detection
// c34: sunDir (x,y,z), cloudCoverage
// c35: shadowSoftness, cloudShadowStr, sunShadowStr, ToD dayReduction
// c36: vertexAOBoost, pointLightOverride, 0, 0
// c37: screenParams (width, height, 1/width, 1/height)
// c38: surfaceWeights (metal, wood, grass, roughness)
// c40: enablePre, enableEdge, enableOcclusion, enableSSAO
// c41: enableStored, enableCloud, enableSun, enableToD
// c42: enableVertexAO, enablePointLight, enableIBL, enableIBLTint
// c43: enableSurfaceWeights, enableGrading, enableGamma, enableSMAA

#define SAT_BOOST          c28.x
#define IBL_TINT_STR       c28.y

#define GRADING_ROW0       c29
#define GRADING_ROW1       c30
#define GRADING_ROW2       c31

#define VEHICLE_COLOUR     c32
#define AVG_CAR_COLOUR     c33

#define SUN_DIR            float3(c34.xyz)
#define CLOUD_COVERAGE     c34.w

#define SHADOW_SOFTNESS    c35.x
#define CLOUD_SHADOW_STR   c35.y
#define SUN_SHADOW_STR     c35.z
#define TOD_DAY_REDUCTION  c35.w

#define VERTEX_AO_BOOST    c36.x
#define POINT_LIGHT_OVR    c36.y

#define SCREEN_W           c37.x
#define SCREEN_H           c37.y
#define SCREEN_INV_W       c37.z
#define SCREEN_INV_H       c37.w

#define SURF_METAL         c38.x
#define SURF_WOOD          c38.y
#define SURF_GRASS         c38.z
#define SURF_ROUGHNESS     c38.w

#define ENABLE_PRE         (c40.x != 0)
#define ENABLE_EDGE        (c40.y != 0)
#define ENABLE_OCCLUSION   (c40.z != 0)
#define ENABLE_SSAO        (c40.w != 0)
#define ENABLE_STORED      (c41.x != 0)
#define ENABLE_CLOUD       (c41.y != 0)
#define ENABLE_SUN         (c41.z != 0)
#define ENABLE_TOD         (c41.w != 0)
#define ENABLE_VERTEX_AO   (c42.x != 0)
#define ENABLE_POINT_LIGHT (c42.y != 0)
#define ENABLE_IBL         (c42.z != 0)
#define ENABLE_IBL_TINT    (c42.w != 0)
#define ENABLE_SURF_W      (c43.x != 0)
#define ENABLE_GRADING     (c43.y != 0)
#define ENABLE_GAMMA       (c43.z != 0)
#define ENABLE_SMAA_EN     (c43.w != 0)

// --- Gamma ---
float3 LinearToSRGB(float3 c)
{
    if (ENABLE_GAMMA)
        return pow(saturate(c), 1.0 / 2.2);
    return c;
}

float3 SRGBToLinear(float3 c)
{
    if (ENABLE_GAMMA)
        return pow(saturate(c), 2.2);
    return c;
}

// Per-version gamma (SA PS2/PC10/Steam/Mobile)
float3 VersionGamma(float3 c, int version)
{
    if (!ENABLE_GAMMA) return c;
    float gamma;
    if (version == 0)      gamma = 2.2;   // PS2
    else if (version == 1) gamma = 2.0;   // PC 1.0
    else if (version == 2) gamma = 1.9;   // Steam
    else                   gamma = 2.1;   // Mobile
    return pow(saturate(c), 1.0 / gamma);
}

// --- Colour Grading ---
float3 ApplyGrading(float3 c)
{
    if (!ENABLE_GRADING) return c;
    float3 result;
    result.r = dot(c, GRADING_ROW0.rgb) + GRADING_ROW0.a;
    result.g = dot(c, GRADING_ROW1.rgb) + GRADING_ROW1.a;
    result.b = dot(c, GRADING_ROW2.rgb) + GRADING_ROW2.a;
    return saturate(result);
}

float3 ApplySatBoost(float3 c)
{
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(luma, luma, luma), c, 1.0 + SAT_BOOST);
}

// --- Depth Reconstruction ---
float3 ReconstructViewPos(float2 uv, float depth, float4 projInfo)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

float3 ReconstructNormal(float2 uv, sampler2D depthTex, float4 projInfo, float2 pixelSize)
{
    float c = tex2D(depthTex, uv).r;
    float r = tex2D(depthTex, uv + float2(pixelSize.x, 0)).r;
    float l = tex2D(depthTex, uv - float2(pixelSize.x, 0)).r;
    float d = tex2D(depthTex, uv + float2(0, pixelSize.y)).r;
    float u = tex2D(depthTex, uv - float2(0, pixelSize.y)).r;

    float3 cPos = ReconstructViewPos(uv, c, projInfo);
    float3 rPos = ReconstructViewPos(uv + float2(pixelSize.x, 0), r, projInfo);
    float3 lPos = ReconstructViewPos(uv - float2(pixelSize.x, 0), l, projInfo);
    float3 dPos = ReconstructViewPos(uv + float2(0, pixelSize.y), d, projInfo);
    float3 uPos = ReconstructViewPos(uv - float2(0, pixelSize.y), u, projInfo);

    float3 ddxV = rPos - lPos;
    float3 ddyV = dPos - uPos;
    return normalize(cross(ddxV, ddyV));
}

// --- Edge Detection ---
float2 DetectEdge(float2 uv, sampler2D depthTex, sampler2D colorTex,
                  float4 projInfo, float2 pixelSize, float threshold)
{
    if (!ENABLE_EDGE) return float2(0, 0);

    float cDepth = tex2D(depthTex, uv).r;
    float rDepth = tex2D(depthTex, uv + float2(pixelSize.x, 0)).r;
    float lDepth = tex2D(depthTex, uv - float2(pixelSize.x, 0)).r;
    float dDepth = tex2D(depthTex, uv + float2(0, pixelSize.y)).r;
    float uDepth = tex2D(depthTex, uv - float2(0, pixelSize.y)).r;

    float depthDiff = abs(rDepth - lDepth) + abs(dDepth - uDepth);
    float depthEdge = step(threshold * cDepth, depthDiff);

    float3 cColor = tex2D(colorTex, uv).rgb;
    float3 rColor = tex2D(colorTex, uv + float2(pixelSize.x, 0)).rgb;
    float3 lColor = tex2D(colorTex, uv - float2(pixelSize.x, 0)).rgb;
    float3 dColor = tex2D(colorTex, uv + float2(0, pixelSize.y)).rgb;
    float3 uColor = tex2D(colorTex, uv - float2(0, pixelSize.y)).rgb;

    float3 colorDiff = abs(rColor - lColor) + abs(dColor - uColor);
    float colorLuma = dot(colorDiff, float3(0.2126, 0.7152, 0.0722));

    float3 avgCar = AVG_CAR_COLOUR;
    float carSat = length(cColor - float3(dot(cColor, float3(0.333)), dot(cColor, float3(0.333)), dot(cColor, float3(0.333))));
    float avgCarSat = length(avgCar - float3(0.5));
    float carMask = smoothstep(0.1, 0.4, carSat) * smoothstep(0.1, 0.4, avgCarSat);
    float colorEdge = step(threshold, colorLuma) * (1.0 - carMask * 0.5);

    float edge = max(depthEdge, colorEdge);
    float2 edgeDir = float2(abs(rDepth - lDepth), abs(dDepth - uDepth));
    return float2(edge, atan2(edgeDir.y, edgeDir.x));
}

// --- SSAO ---
float ComputeSSAO(float2 uv, sampler2D depthTex, float4 projInfo, float2 pixelSize,
                  float radius, float power)
{
    if (!ENABLE_SSAO) return 1.0;

    float centerDepth = tex2D(depthTex, uv).r;
    if (centerDepth >= 1.0) return 1.0;

    float3 centerPos = ReconstructViewPos(uv, centerDepth, projInfo);
    float occ = 0.0;

    float2 offsets[4] = {
        float2(1, 0), float2(-1, 0),
        float2(0, 1), float2(0, -1)
    };

    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * radius * pixelSize;
        float sd = tex2D(depthTex, sampleUV).r;
        float3 sv = ReconstructViewPos(sampleUV, sd, projInfo);
        float diff = length(sv - centerPos);
        occ += step(sd, centerDepth) * (1.0 - saturate(diff / radius));
    }

    occ = 1.0 - occ * 0.25;
    return pow(occ, power);
}

// --- Shadow Merge ---
float HashNoise(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float ComputeCloudShadow(float2 worldUV, float sunAzimuth)
{
    float2 rotUV;
    rotUV.x = worldUV.x * cos(sunAzimuth) - worldUV.y * sin(sunAzimuth);
    rotUV.y = worldUV.x * sin(sunAzimuth) + worldUV.y * cos(sunAzimuth);

    float noise = HashNoise(rotUV * 8.0);
    float cloud = smoothstep(0.3, 0.7, noise);
    return lerp(1.0, cloud, CLOUD_COVERAGE * CLOUD_SHADOW_STR);
}

float ComputeSunShadow(float3 normal, float3 sunDir)
{
    float NdotL = dot(normalize(normal), normalize(sunDir));
    return smoothstep(-0.1, 0.3, NdotL);
}

float ComputeShadowMerge(float2 uv, float3 normal, float2 worldUV,
                          sampler2D depthTex, float4 projInfo, float2 pixelSize)
{
    if (!ENABLE_STORED && !ENABLE_CLOUD && !ENABLE_SUN) return 1.0;

    float storedShadow = 1.0;
    float cloudShadow = 1.0;
    float sunShadow = 1.0;

    if (ENABLE_CLOUD)
        cloudShadow = ComputeCloudShadow(worldUV, atan2(SUN_DIR.y, SUN_DIR.x));

    if (ENABLE_SUN)
        sunShadow = ComputeSunShadow(normal, SUN_DIR);

    float merged = storedShadow * cloudShadow * sunShadow;
    merged = lerp(merged, smoothstep(0.0, 1.0, merged), SHADOW_SOFTNESS);
    return merged;
}

// --- Time of Day Modulation ---
float ComputeTimeOfDayModulation(float vertBright)
{
    if (!ENABLE_TOD) return 1.0;

    float dayFactor = 1.0 - TOD_DAY_REDUCTION;

    float vertexBoost = 1.0;
    if (ENABLE_VERTEX_AO)
        vertexBoost = lerp(1.0, VERTEX_AO_BOOST, 1.0 - vertBright);

    float pointLight = 0.0;
    if (ENABLE_POINT_LIGHT)
        pointLight = POINT_LIGHT_OVR;

    float mod = dayFactor * vertexBoost * (1.0 - pointLight * 0.5);
    return saturate(mod);
}

// --- Surface Weights ---
float ComputeIBLWeight()
{
    if (!ENABLE_SURF_W) return 1.0;

    float metal = SURF_METAL;
    float rough = SURF_ROUGHNESS;

    float iblWeight = lerp(0.2, 1.0, metal);
    iblWeight *= lerp(1.0, 0.3, rough);
    return iblWeight;
}

// --- Unified Occlusion ---
float ComputeOcclusion(float2 uv, sampler2D depthTex, float4 projInfo, float2 pixelSize,
                       float2 worldUV, float3 normal, float vertBright,
                       float ssaoRadius, float ssaoPower)
{
    if (!ENABLE_OCCLUSION) return 1.0;

    float ssao = ComputeSSAO(uv, depthTex, projInfo, pixelSize, ssaoRadius, ssaoPower);
    float shadow = ComputeShadowMerge(uv, normal, worldUV, depthTex, projInfo, pixelSize);
    float tod = ComputeTimeOfDayModulation(vertBright);

    return ssao * shadow * tod;
}

// --- IBL Tint ---
float3 ApplyIBLTint(float3 envColor, float3 carColour)
{
    if (!ENABLE_IBL_TINT) return envColor;
    return lerp(envColor, envColor * carColour, IBL_TINT_STR);
}

// --- Combined Apply ---
float3 ApplyUnifiedColor(float3 color)
{
    float3 c = color;
    c = ApplySatBoost(c);
    c = ApplyGrading(c);
    c = LinearToSRGB(c);
    return c;
}

#endif
