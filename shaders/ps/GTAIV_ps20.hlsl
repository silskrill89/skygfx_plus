// GTA IV Style PostFX Shader
sampler2D sceneSampler : register(s0);
sampler2D bloomSampler : register(s1);
float4 filmic0   : register(c7);
float4 filmic1   : register(c8);
float4 colorCorr : register(c9);  // {desat, gamma, sat, curves}
float4 bloomP    : register(c10);
float4 vigP      : register(c11);
float4 vigE      : register(c12); // {exposure, 0, 0, 0}
float4 rgb1      : register(c0);  // timecycle tint (set by ColourFilter_Generic)
float4 rgb2      : register(c1);  // timecycle tint (set by ColourFilter_Generic)

float filmicScalar(float x)
{
    float A = filmic0.x, B = filmic0.y, wp = filmic0.z, Cb = filmic0.w;
    float DE = filmic1.x, DF = filmic1.y, Ef = filmic1.z;
    float num = x * (A * x + Cb) + DE;
    float den = x * (A * x + B) + DF;
    return max(((num / den) - Ef) * wp, 0.0);
}

static const float3 LUM = float3(0.299, 0.587, 0.114);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 color = tex2D(sceneSampler, uv).rgb;
    color += tex2D(bloomSampler, uv).rgb * bloomP.x;
    color *= vigE.x;

    // Tonemap luminance only
    float lum = dot(color, LUM);
    float mappedLum = filmicScalar(lum);
    if(lum > 0.001) color *= (mappedLum / lum);

    // Saturation boost
    float sat = colorCorr.z;
    if(sat != 0.0) {
        float cl = dot(color, LUM);
        color += (color - float3(cl,cl,cl)) * sat;
    }

    // Desaturation
    float desat = colorCorr.x;
    if(desat < 1.0) {
        float dl = dot(color, LUM);
        color = lerp(float3(dl,dl,dl), color, desat);
    }

    // Gamma
    color = pow(max(color, 0.0), colorCorr.y);

    // Curves
    float curve = colorCorr.w;
    if(curve != 1.0) {
        float cl = dot(color, LUM);
        float sl = cl * cl * (3.0 - 2.0 * cl);
        float nl = lerp(cl, sl, saturate(curve - 1.0));
        if(cl > 0.001) color *= nl / cl;
    }

    // Vignette
    float dist = length(uv - 0.5);
    float vig = 1.0 - smoothstep(0.0, vigP.y, dist);
    vig = pow(max(vig, 0.0), vigP.z);
    color = lerp(color, color * vig, vigP.x);

    return float4(max(color, 0.0), 1.0);
}

technique GTAIV {
    pass P0 {
        PixelShader = compile ps_3_0 main();
    }
}
