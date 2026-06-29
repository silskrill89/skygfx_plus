// unifiedPipe.hlsl - Consolidated pipeline shader
// Entry points: main (defaults to PS_Pre), main_pre, main_occlusion, main_post, main_ibl
// Compile with /E main for pre-pass, or /E main_occlusion, /E main_post, /E main_ibl for other passes
// NOTE: This shader is NOT referenced in Resource.rc. Compile each pass separately:
//   fxc /T ps_3_0 /E main /Fo pre.cso unifiedPipe.hlsl
//   fxc /T ps_3_0 /E main_occlusion /Fo occl.cso unifiedPipe.hlsl
//   fxc /T ps_3_0 /E main_post /Fo post.cso unifiedPipe.hlsl
//   fxc /T ps_3_0 /E main_ibl /Fo ibl.cso unifiedPipe.hlsl
#include "include/colorSpace.hlsl"

// --- Shared samplers ---
sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);
sampler2D normalTex : register(s2);
sampler2D noiseTex : register(s3);

// --- Pre-pass constants ---
float4 projInfo : register(c2);

struct PS_INPUT_PRE {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT_PRE {
    float4 Color : COLOR0;
};

// PS_Pre: gamma decode + edge detect (edge stored in alpha)
PS_OUTPUT_PRE main_pre(PS_INPUT_PRE IN)
{
    PS_OUTPUT_PRE OUT;

    if (!ENABLE_PRE)
    {
        OUT.Color = float4(tex2D(colorTex, IN.TexCoord).rgb, 0);
        return OUT;
    }

    float3 color = SRGBToLinear(tex2D(colorTex, IN.TexCoord).rgb);
    float2 pixelSize = float2(SCREEN_INV_W, SCREEN_INV_H);

    float2 edge = DetectEdge(IN.TexCoord, depthTex, colorTex,
                             projInfo, pixelSize, 0.1);

    OUT.Color = float4(color, edge.x);
    return OUT;
}

// main: alias for main_pre (default entry point for /E main)
PS_OUTPUT_PRE main(PS_INPUT_PRE IN) { return main_pre(IN); }

// --- Occlusion constants ---
float4 ssaoParams : register(c4);  // radius, power, noiseScale, kernelSize
float4 shadowParams : register(c5); // shadowSoftness, cloudStr, sunStr, unused

struct PS_INPUT_OCCL {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT_OCCL {
    float4 Color : COLOR0;
};

// PS_Occlusion: SSAO + shadow merge + ToD modulation
PS_OUTPUT_OCCL main_occlusion(PS_INPUT_OCCL IN)
{
    PS_OUTPUT_OCCL OUT;

    if (!ENABLE_OCCLUSION)
    {
        OUT.Color = float4(1, 1, 1, 1);
        return OUT;
    }

    float2 pixelSize = float2(SCREEN_INV_W, SCREEN_INV_H);
    float3 normal = ReconstructNormal(IN.TexCoord, depthTex, projInfo, pixelSize);
    float2 worldUV = IN.TexCoord * 10.0;

    float vertBright = 0.5;
    float occ = ComputeOcclusion(IN.TexCoord, depthTex, projInfo, pixelSize,
                                 worldUV, normal, vertBright,
                                 ssaoParams.x, ssaoParams.y);

    OUT.Color = float4(occ, occ, occ, 1.0);
    return OUT;
}

// --- Post constants ---
float4 gradeParams : register(c6); // version, exposure, unused, unused

struct PS_INPUT_POST {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT_POST {
    float4 Color : COLOR0;
};

// PS_Post: occlusion apply + grading + gamma encode
PS_OUTPUT_POST main_post(PS_INPUT_POST IN)
{
    PS_OUTPUT_POST OUT;

    float3 color = tex2D(colorTex, IN.TexCoord).rgb;

    if (ENABLE_OCCLUSION)
    {
        float occ = tex2D(depthTex, IN.TexCoord).r;
        color *= occ;
    }

    color = ApplySatBoost(color);
    color = ApplyGrading(color);

    int version = (int)gradeParams.x;
    color = VersionGamma(color, version);

    OUT.Color = float4(color, 1.0);
    return OUT;
}

// --- IBL constants ---
float4 iblParams : register(c7); // iblStrength, envMapSize, unused, unused
float4 fxParams : register(c8);  // shininess, specularity, lightCount, wetness

struct PS_INPUT_IBL {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 ViewDir : TEXCOORD2;
    float2 EnvTexcoord : TEXCOORD3;
};

struct PS_OUTPUT_IBL {
    float4 Color : COLOR0;
};

// PS_IBL: vehicle IBL with car paint tint
PS_OUTPUT_IBL main_ibl(PS_INPUT_IBL IN)
{
    PS_OUTPUT_IBL OUT;

    if (!ENABLE_IBL)
    {
        OUT.Color = float4(0, 0, 0, 0);
        return OUT;
    }

    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float NdotV = saturate(dot(N, V));

    float fresnel = pow(1.0 - NdotV, 5.0);
    float3 envColor = tex2D(colorTex, IN.EnvTexcoord).rgb;

    envColor = SRGBToLinear(envColor);

    float iblWeight = ComputeIBLWeight();
    float3 ibl = envColor * iblWeight * iblParams.x;

    float3 carColour = VEHICLE_COLOUR.rgb;
    ibl = ApplyIBLTint(ibl, carColour);

    ibl *= fresnel;

    OUT.Color = float4(ibl, fresnel);
    return OUT;
}
