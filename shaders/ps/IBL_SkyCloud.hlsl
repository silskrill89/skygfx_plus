/*===================================================================================
SkyGFX Plus - IBL Sky + Cloud Buffer (ps_3_0)
Based on noise functions from CloudWorks by Brian Tu (keroroxzz)
https://github.com/keroroxzz
License: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported

Renders quarter-res sky gradient + procedural clouds into IBL texture.
Uses actual timecycle sky/cloud colors and weather cloud coverage.
===================================================================================*/

float4 screenParams : register(c0); // w, h, 1/w, 1/h
float4 skyColors    : register(c1); // skyTopR, skyTopG, skyTopB, fogPresent
float4 cloudParams  : register(c2); // time, cloudCoverage, cloudAlpha, unused
float4 sunData      : register(c3); // sunDirX, sunDirY, sunDirZ, sunBrightness

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR0;
};

// ---- Noise functions (from CloudWorks by Brian Tu / keroroxzz) ----
static float randomSeed = 1618.03398875;

float hash(float n) {
    return frac(sin(n / 1873.1873) * randomSeed);
}

float noise2d(float3 p) {
    float3 fr = floor(p);
    float3 ft = frac(p);
    float n = 1153.0 * fr.x + 2381.0 * fr.y + p.z;
    float nr = n + 1153.0;
    float nd = n + 2381.0;
    float no = nr + 2381.0;
    float v  = hash(n);
    float vr = hash(nr);
    float vd = hash(nd);
    float vo = hash(no);
    return ((v * (1.0 - ft.x) + vr * ft.x) * (1.0 - ft.y) +
            (vd * (1.0 - ft.x) + vo * ft.x) * ft.y);
}

float noise3d(float3 p) {
    float3 fr = floor(p);
    float3 ft = frac(p);
    float n = 1153.0 * fr.x + 2381.0 * fr.y + fr.z;
    float nr = n + 1153.0;
    float nd = n + 2381.0;
    float no = nr + 2381.0;
    float v  = lerp(hash(n), hash(n + 1.0), ft.z);
    float vr = lerp(hash(nr), hash(nr + 1.0), ft.z);
    float vd = lerp(hash(nd), hash(nd + 1.0), ft.z);
    float vo = lerp(hash(no), hash(no + 1.0), ft.z);
    return lerp(lerp(v, vr, ft.x), lerp(vd, vo, ft.x), ft.y);
}

float fbm(float3 p) {
    float f = 0.0;
    f += 0.5000 * noise3d(p); p *= 2.01;
    f += 0.2500 * noise3d(p); p *= 2.02;
    f += 0.1250 * noise3d(p); p *= 2.03;
    f += 0.0625 * noise3d(p);
    return f;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 uv = IN.texCoord;
    float time = cloudParams.x;
    float coverage = cloudParams.y;  // from CWeather::CloudCoverage (0-1)
    float cloudAlpha = cloudParams.z; // from timecycle m_fCloudAlpha

    // ---- Sky gradient from timecycle ----
    float height = uv.y;
    float3 zenithColor = skyColors.rgb;  // sky top from timecycle
    float3 horizonColor = zenithColor * 0.7 + float3(0.15, 0.12, 0.08); // warm shift at horizon
    float3 skyColor = lerp(horizonColor, zenithColor, saturate(height * height * 1.5));

    // Sun glow
    float2 sunUV = sunData.xy * 0.5 + 0.5;
    float sunDist = length(uv - sunUV);
    float sunGlow = exp(-sunDist * 6.0) * 0.3 * sunData.w;

    // ---- Procedural clouds (weather-driven density) ----
    // Coverage scales the cloud density threshold
    float densityThreshold = lerp(0.55, 0.25, coverage); // high coverage = lower threshold = more clouds

    float3 cloudPos = float3(uv * 6.0, time * 0.3);
    float clouds = fbm(cloudPos);
    clouds = smoothstep(densityThreshold - 0.1, densityThreshold + 0.2, clouds);

    // Second cloud layer (offset, larger scale)
    float3 cloudPos2 = float3(uv * 3.0 + float2(time * 0.05, 0.3), time * 0.2);
    float clouds2 = fbm(cloudPos2);
    float density2 = lerp(0.6, 0.35, coverage);
    clouds2 = smoothstep(density2 - 0.1, density2 + 0.15, clouds2);

    float combinedClouds = saturate(clouds + clouds2 * 0.4);

    // Scale by cloud alpha from timecycle
    combinedClouds *= saturate(cloudAlpha);

    // ---- Cloud lighting ----
    float3 cloudLit = float3(0.95, 0.97, 1.0) * combinedClouds;
    float sunFacing = saturate(sunData.z); // sun height factor
    cloudLit += float3(1.0, 0.85, 0.6) * combinedClouds * sunFacing * 0.2;

    // ---- Combine ----
    float3 result = skyColor + cloudLit * 0.3 + sunGlow * float3(1.0, 0.9, 0.7);

    // Alpha = cloud shadow factor (1.0 = lit, lower = shadowed)
    float cloudShadow = lerp(0.5, 1.0, 1.0 - combinedClouds * 0.5);

    return float4(result, cloudShadow);
}
