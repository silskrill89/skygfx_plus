// SSAO.hlsl - Screen-space ambient occlusion (ps_3_0)
// Compiled via /E main -> SSAO.cso
// Uses depth buffer + normal buffer for hemisphere-oriented ambient occlusion
// Based on Alex Tardif's SSAO (https://alextardif.com/SSAO.html)
//
// Output contract (black-SSAO fixes):
//   Greyscale AO, WHITE = no occlusion. The C++ composite multiplies the
//   scene by this texture (Photoshop multiply: final = scene * ao). Every
//   invalid input fails OPEN to white — SSAO must degrade to "off", never
//   black:
//     - depth >= 1.0 (sky) or <= 1e-6 (never-written INTZ) -> white
//     - garbage/missing normal buffer -> depth-derivative fallback
//     - degenerate fallback (zero-length cross) -> white
//     - occlusion saturate()d before pow(): pow(negative) = NaN in SM3.0,
//       NaN renders as black through the multiply composite (was the
//       primary cause of the fully-black SSAO buffer)

uniform sampler2D depthTexture : register(s0);
uniform sampler2D randomTexture : register(s1);
uniform sampler2D normalTexture : register(s2);

uniform float4 ssaoParams : register(c0); // x=radius, y=power, z=noiseScale, w=kernelSize
uniform float4 screenSize : register(c1); // x=width, y=height, z=1/width, w=1/height
uniform float4 projInfo : register(c2); // projection matrix info

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float3 GetViewPos(float2 texCoord, float depth)
{
    float2 ndc = texCoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

// Exact inverse of GetViewPos: view-space position -> screen UV.
// GetViewPos does: ndc = uv*2-1; ndc.y = -ndc.y; viewXY = ndc * viewZ * projInfo.xy
// so the reverse must divide by projInfo.xy AND un-flip y. The old code did
// neither (plain samplePos.xy / samplePos.z), so every occlusion sample was
// fetched from a mirrored, projection-scaled wrong texel — garbage occlusion
// everywhere (the other primary cause of the black AO buffer).
float2 ProjectToUV(float3 viewPos)
{
    float invZ = 1.0 / max(viewPos.z, 1e-7);
    float nx = viewPos.x * invZ / max(projInfo.x, 1e-7);
    float ny = viewPos.y * invZ / max(projInfo.y, 1e-7);
    return float2(nx * 0.5 + 0.5, 0.5 - ny * 0.5);
}

float3x3 BuildTBN(float3 normal, float2 texCoord, float2 noiseScale)
{
    float3 randVec = tex2D(randomTexture, texCoord * noiseScale).rgb * 2.0 - 1.0;
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

float4 main(PS_INPUT IN) : COLOR
{
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;

    // Fail-open: sky (>=1.0) and never-written depth (<=1e-6, garbage INTZ
    // rows below/around the real Z) both return white instead of feeding
    // GetViewPos bad values that produced wild occlusion.
    if (centerDepth >= 1.0 || centerDepth <= 1e-6)
        return float4(1, 1, 1, 1);

    float3 centerPos = GetViewPos(IN.texCoord, centerDepth);

    // Normal from buffer, decoded from [0,1] to [-1,1]. Valid data is
    // unit-length (NormalBuffer.hlsl writes n*0.5+0.5 of a normalized
    // view-space normal; sky = (0,0,1)). A NULL/unbound texture samples as
    // black -> decodes to (-1,-1,-1), length 1.73 — the old `len < 0.5`
    // check PASSED that garbage, and the un-normalized dot product let
    // occlusion go negative -> pow(negative) = NaN -> black frame. Accept
    // only a sane unit-length window (normalize), else reconstruct from
    // depth derivatives.
    float3 normal = tex2D(normalTexture, IN.texCoord).rgb * 2.0 - 1.0;
    float normalLen = length(normal);
    if (normalLen >= 0.5 && normalLen <= 1.5)
    {
        normal /= normalLen;
    }
    else
    {
        // Normal buffer not available — reconstruct from depth (5-tap cross,
        // same bilateral pair selection as NormalBuffer.hlsl).
        float2 texel = screenSize.zw;
        float dc = centerDepth;
        float dl = tex2D(depthTexture, IN.texCoord - float2(texel.x, 0)).r;
        float dr = tex2D(depthTexture, IN.texCoord + float2(texel.x, 0)).r;
        float du = tex2D(depthTexture, IN.texCoord - float2(0, texel.y)).r;
        float dd = tex2D(depthTexture, IN.texCoord + float2(0, texel.y)).r;
        float3 pc = GetViewPos(IN.texCoord, dc);
        float3 pl = GetViewPos(IN.texCoord - float2(texel.x, 0), dl);
        float3 pr = GetViewPos(IN.texCoord + float2(texel.x, 0), dr);
        float3 pu = GetViewPos(IN.texCoord - float2(0, texel.y), du);
        float3 pd = GetViewPos(IN.texCoord + float2(0, texel.y), dd);
        float3 dx1 = pr - pc;
        float3 dx2 = pc - pl;
        float3 dy1 = pd - pc;
        float3 dy2 = pc - pu;
        float3 dx = (abs(dx1.z) < abs(dx2.z)) ? dx1 : dx2;
        float3 dy = (abs(dy1.z) < abs(dy2.z)) ? dy1 : dy2;
        float3 fn = cross(dx, dy);
        float flen = length(fn);
        // Degenerate (flat surface / equal taps) — fail open white
        if (flen < 1e-7)
            return float4(1, 1, 1, 1);
        normal = fn / flen;
    }

    float2 noiseScale = ssaoParams.z * screenSize.xy;
    float3x3 TBN = BuildTBN(normal, IN.texCoord, noiseScale);

    float occlusion = 0.0;
    float radius = ssaoParams.x;
    float power = ssaoParams.y;

    // Generate 16 hemisphere kernel samples oriented by surface normal
    float3 kernel[16];
    float3 rand = tex2D(randomTexture, IN.texCoord * noiseScale).rgb * 2.0 - 1.0;
    for (int i = 0; i < 16; ++i)
    {
        float3 sample = float3(
            rand.x * 2.0 - 1.0,
            rand.y * 2.0 - 1.0,
            abs(rand.z) * 2.0 - 0.5  // bias towards hemisphere
        );
        sample = normalize(sample);
        sample *= abs(rand.x) * radius;
        kernel[i] = mul(sample, TBN);  // orient by surface normal
        rand = tex2D(randomTexture, float2(i * 0.1, 0.0)).rgb * 2.0 - 1.0;
    }

    // Calculate occlusion using hemisphere-oriented kernel samples
    for (int i = 0; i < 16; ++i)
    {
        float3 samplePos = centerPos + kernel[i];
        if (samplePos.z <= 1e-7)
            continue;

        // Project the kernel-lifted sample to screen space (exact GetViewPos
        // inverse — see ProjectToUV)
        float2 sampleCoord = ProjectToUV(samplePos);

        if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
            sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTexture, sampleCoord).r;
            // Invalid/sky depth is not an occluder — skip it (old code let
            // depth=0 garbage occlude whole screen regions)
            if (sampleDepth <= 1e-6 || sampleDepth >= 1.0)
                continue;
            float3 sampleViewPos = GetViewPos(sampleCoord, sampleDepth);

            // Range check and angle-aware occlusion
            float diff = length(sampleViewPos - centerPos);
            float rangeCheck = smoothstep(radius, 0.0, diff);
            float3 sdir = samplePos - centerPos;
            float slen = length(sdir);
            // Guard normalize(): a zero-length kernel direction is NaN in SM3.0
            float nDotS = slen > 1e-7 ? max(dot(normal, sdir / slen), 0.0) : 0.0;
            // Compare against the kernel-LIFTED point (hemisphere test), not the
            // surface point: kernel[i] lifts the sample along the normal, so
            // using centerPos.z over-occluded on tilted surfaces.
            occlusion += rangeCheck * step(sampleViewPos.z, samplePos.z) * nDotS;
        }
    }

    // Each term is in [0,1] (rangeCheck, step, nDotS), but saturate anyway —
    // a negative base makes pow() NaN in SM3.0 and NaN reads black through
    // the multiply composite.
    occlusion = saturate(1.0 - (occlusion / 16.0));
    occlusion = pow(occlusion, max(power, 1e-3));

    return float4(occlusion, occlusion, occlusion, 1.0);
}
