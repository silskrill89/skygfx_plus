// GTA IV Style PostFX Shader
// Adapted from RAGE engine (GTA V source) postfx.fx filmic tonemapping
// and color grading system for GTA SA via skygfx
//
// Features:
//   - Hable/Uncharted2 filmic tonemapping (from RAGE fullFilmicTonemap)
//   - Luminance-dependent color correction (from RAGE ApplyColorCorrection)
//   - Desaturation for GTA IV muted look
//   - Bloom compositing
//   - Vignette darkening
//   - Blue shift for shadow tones (GTA IV signature)

sampler2D sceneSampler : register(s0);
sampler2D bloomSampler : register(s1);

// Filmic tonemap params (from RAGE BrightTonemapParams0/1, DarkTonemapParams0/1)
// .xy = A,B  .zw = C*B  and  .x = D*E, .y = D*F, .z = E/F
float4 filmicParams0 : register(c0);  // {A, B, 1/whitePoint, unused}
float4 filmicParams1 : register(c1);  // {C*B, D*E, D*F, E/F}
float4 colorCorrect : register(c2);   // {desaturate, gamma, unused, unused}
float4 bloomParams : register(c3);    // {bloomIntensity, unused, unused, unused}
float4 vignetteParams : register(c4); // {intensity, radius, contrast, unused}
float4 vignetteColor : register(c5);  // {r, g, b, unused}
float4 exposureScale : register(c6);  // {exposure, unused, unused, unused}

// Hable/Uncharted2 filmic tonemapping - from RAGE fullFilmicTonemap()
// ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E/F
float3 filmicTonemap(float3 x)
{
    float A = filmicParams0.x;
    float B = filmicParams0.y;
    float C_mul_B = filmicParams1.x;
    float D_mul_E = filmicParams1.y;
    float D_mul_F = filmicParams1.z;
    float E_div_F = filmicParams1.w;
    float ooWhitePoint = filmicParams0.z;

    float3 numerator = x * (A * x + C_mul_B) + D_mul_E;
    float3 denominator = x * (A * x + B) + D_mul_F;
    float3 result = (numerator / denominator) - E_div_F;

    return result * ooWhitePoint;
}

// Luminance-dependent color correction - from RAGE ApplyColorCorrection()
static const float3 LumFactors = float3(0.299, 0.587, 0.114);

float3 applyColorCorrection(float3 color)
{
    float desat = colorCorrect.x;
    float gamma = colorCorrect.y;

    float lum = dot(color, LumFactors);

    // Desaturate (GTA IV muted look)
    color = lerp(float3(lum, lum, lum), color, desat);

    // Gamma correction
    color = pow(max(color, 0.0), gamma);

    return color;
}

// Vignette - darkening at screen edges
float3 applyVignette(float3 color, float2 uv)
{
    float intensity = vignetteParams.x;
    float radius = vignetteParams.y;
    float contrast = vignetteParams.z;

    float2 center = uv - 0.5;
    float dist = length(center);
    float vig = smoothstep(radius, radius * 0.5, dist);
    vig = pow(vig, contrast);

    float3 vigColor = vignetteColor.rgb;
    color = lerp(vigColor, color, vig * intensity + (1.0 - intensity));
    return color;
}

// Main composite pass
struct PS_INPUT {
    float2 texcoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : COLOR0
{
    float2 uv = input.texcoord;

    // Sample scene
    float4 sceneColor = tex2D(sceneSampler, uv);
    float3 color = sceneColor.rgb;

    // Sample bloom and add
    float4 bloomColor = tex2D(bloomSampler, uv);
    color += bloomColor.rgb * bloomParams.x;

    // Exposure scale
    color *= exposureScale.x;

    // Filmic tonemapping (RAGE Hable curve)
    color = filmicTonemap(color);

    // Color correction + desaturation (GTA IV muted palette)
    color = applyColorCorrection(color);

    // Vignette (GTA IV dark edges)
    color = applyVignette(color, uv);

    return float4(color, 1.0);
}

technique GTAIV {
    pass P0 {
        PixelShader = compile ps_3_0 main();
    }
}
