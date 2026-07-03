// DynamicSky.hlsl - Dynamic sky with volumetric cloud clumps (ps_3_0)
// Unified sky + clouds system tied to weather/timecycle.
// Replaces IBL_SkyCloud with multi-layer sky and spawnable cloud clumps.
//
// Based on Ren712 dynamic sky concept, CloudWorks noise, Disney BRDF.
//
// c0 = screenParams (w, h, 1/w, 1/h)
// c1 = skyColors (topR, topG, topB, fogPresent)
// c2 = cloudParams (time, cloudCoverage, cloudAlpha, cloudSpeed)
// c3 = sunData (sunDirX, sunDirY, sunDirZ, sunBrightness)
// c4 = horizonColors (horR, horG, horB, horizonBlend)
// c5 = moonData (moonDirX, moonDirY, moonDirZ, moonPhase)
// c6 = cloudClumpParams (clumpScale, clumpDensity, clumpSoftness, clumpCount)
// c7 = weatherFog (fogR, fogG, fogB, fogDensity)
// c8 = weatherType (currentWeather, oldWeather, weatherInterp, smogBoost)

sampler2D skyboxTex    : register(s0);
sampler2D cloudTex     : register(s1);
sampler2D cloudNormTex : register(s2);

float4 screenParams    : register(c0);
float4 skyColors       : register(c1);
float4 cloudParams     : register(c2);
float4 sunData         : register(c3);
float4 horizonColors   : register(c4);
float4 moonData        : register(c5);
float4 cloudClump      : register(c6);
float4 weatherFog      : register(c7);
float4 weatherType     : register(c8);

static float randomSeed = 1618.03398875;

float hash(float n) {
    return frac(sin(n / 1873.1873) * randomSeed);
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

// Volumetric cloud clump - simple sphere with noise displacement
float cloudClumpSphere(float3 pos, float3 center, float radius, float softness)
{
    float dist = length(pos - center);
    float sphere = saturate(1.0 - dist / radius);
    sphere = pow(sphere, softness);
    // Noise displacement on surface
    float noise = fbm(pos * cloudClump.x + center * 0.5);
    sphere *= smoothstep(0.3, 0.7, noise);
    return sphere;
}

// Multi-clump cloud field
float cloudField(float3 pos, float time, float coverage)
{
    float result = 0.0;
    float scale = cloudClump.x;
    float density = cloudClump.y * coverage;
    float softness = cloudClump.z;
    int count = (int)cloudClump.w;

    // Generate clump positions from hash
    for(int i = 0; i < 8; i++)
    {
        if(i >= count) break;
        float fi = (float)i;
        float3 center = float3(
            hash(fi * 17.0 + 1.0) * 2.0 - 1.0,
            hash(fi * 31.0 + 2.0) * 0.3 + 0.5,  // upper sky
            hash(fi * 43.0 + 3.0) * 2.0 - 1.0
        );
        center.xz += time * 0.02 * float2(hash(fi * 7.0), hash(fi * 11.0));
        float radius = 0.15 + hash(fi * 53.0) * 0.2;
        result += cloudClumpSphere(pos, center, radius, softness);
    }

    return saturate(result * density);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float time = cloudParams.x;
    float coverage = cloudParams.y;
    float cloudAlpha = cloudParams.z;
    float cloudSpeed = cloudParams.w;

    // ---- Range validation: clamp all incoming constants to valid ranges ----
    // This prevents corrupted timecycle values from destroying the sky
    float3 zenith = saturate(skyColors.rgb);
    float3 horizon = saturate(horizonColors.rgb);
    float horizonBlend = saturate(horizonColors.w);
    float3 fogColor = saturate(weatherFog.rgb);
    float fogDensity = saturate(weatherFog.w);
    float sunBright = saturate(sunData.w);
    float3 sunDir = normalize(sunData.xyz);
    float3 moonDir = normalize(moonData.xyz);
    float cloudA = saturate(cloudAlpha);
    float cov = saturate(coverage);
    float smogBoost = saturate(weatherType.w);

    // ---- Sky gradient ----
    float height = uv.y;

    // Smooth sky gradient with horizon blending
    float skyGrad = saturate(height * height * 1.5);
    float3 skyColor = lerp(horizon, zenith, skyGrad);

    // Fog blend at horizon
    float fogBlend = saturate(1.0 - height * 3.0) * fogDensity;
    skyColor = lerp(skyColor, fogColor, fogBlend);

    // ---- Sun glow ----
    float2 sunUV = sunDir.xy * 0.5 + 0.5;
    float sunDist = length(uv - sunUV);
    float sunGlow = exp(-sunDist * 6.0) * 0.3 * sunBright;
    float sunDisc = smoothstep(0.02, 0.015, sunDist) * sunBright;
    skyColor += float3(1.0, 0.95, 0.8) * sunGlow;
    skyColor += float3(1.0, 0.98, 0.9) * sunDisc * 2.0;

    // ---- Moon ----
    float2 moonUV = moonDir.xy * 0.5 + 0.5;
    float moonDist = length(uv - moonUV);
    float moonDisc = smoothstep(0.025, 0.02, moonDist);
    float moonPhase = moonData.w; // 0-1 phase
    float moonBright = moonDisc * 0.8 * saturate(1.0 - abs(moonPhase - 0.5) * 2.0);
    skyColor += float3(0.8, 0.85, 1.0) * moonBright;

    // Stars (only at night)
    float nightFactor = saturate(1.0 - sunBright);
    float stars = noise3d(float3(uv * 200.0, 0.0));
    stars = smoothstep(0.97, 1.0, stars) * nightFactor * 0.5;
    skyColor += float3(1.0, 1.0, 0.9) * stars;

    // ---- Volumetric cloud clumps ----
    // Smog weather (type 4): boost coverage, add yellow-brown tint
    float adjustedCoverage = saturate(cov + smogBoost * 0.4);

    float3 cloudPos = float3(uv, time * cloudSpeed);
    float clumps = cloudField(cloudPos, time, adjustedCoverage);

    // Traditional layered clouds on top
    float3 cloudPos2 = float3(uv * 6.0, time * 0.3);
    float layeredClouds = fbm(cloudPos2);
    float density = lerp(0.55, 0.25, adjustedCoverage);
    layeredClouds = smoothstep(density - 0.1, density + 0.2, layeredClouds);

    // Combine clumps + layered
    float totalClouds = saturate(clumps + layeredClouds * 0.5);
    totalClouds *= saturate(cloudA);

    // Smog: add low-altitude haze layer
    float haze = saturate(1.0 - height * 2.0) * smogBoost * 0.5;
    totalClouds = saturate(totalClouds + haze);

    // Cloud lighting (no normal map — use procedural normal from noise)
    float3 cloudNorm = normalize(float3(
        noise3d(float3(uv * 4.0 + time * 0.01, 0.0)) - 0.5,
        noise3d(float3(uv * 4.0 + time * 0.01, 1.0)) - 0.5,
        1.0
    ));
    float sunFacing = saturate(sunDir.z);
    float cloudLight = saturate(dot(cloudNorm, sunDir));
    cloudLight = cloudLight * 0.5 + 0.5; // wrap

    // Cloud color (lit by sun, shaded by phase)
    float3 cloudColor = lerp(float3(0.95, 0.97, 1.0), float3(1.0, 0.9, 0.7), sunFacing * 0.3);
    cloudColor *= cloudLight;

    // Smog tint: yellowish-brown for low clouds
    float3 smogTint = float3(0.85, 0.75, 0.55);
    cloudColor = lerp(cloudColor, smogTint, smogBoost * 0.6);

    // Apply clouds to sky
    skyColor = lerp(skyColor, cloudColor, totalClouds * 0.8);

    // Smog: haze the sky color itself
    float3 smogFogColor = float3(0.7, 0.65, 0.5);
    skyColor = lerp(skyColor, smogFogColor, smogBoost * 0.3 * saturate(1.0 - height * 2.0));

    // Cloud shadow factor (for PBR pipeline)
    float cloudShadow = lerp(0.5, 1.0, 1.0 - totalClouds * 0.5);

    return float4(skyColor, cloudShadow);
}
