// GTA SA Color Correction Shader (ps_3_0)
// When all params are 0/1, this is a pure passthrough.
// Only applies when user explicitly enables via INI.
//
// c0 = timecycle color 1, c1 = timecycle color 2
// c7 = {strength, contrast, vignette, vignetteRadius}

sampler2D sceneSampler : register(s0);
float4 tc1       : register(c0);
float4 tc2       : register(c1);
float4 params    : register(c7); // x=strength, y=contrast, z=vignette, w=vRadius

static const float3 LUM = float3(0.299, 0.587, 0.114);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 color = tex2D(sceneSampler, uv).rgb;

    // Strength = 0 means pure passthrough (no filter)
    if(params.x < 0.001)
        return float4(color, 1.0);

    // Subtle warm tint from timecycle
    float3 tint = lerp(tc1.rgb, tc2.rgb, 0.5);
    color = lerp(color, color * tint, params.x * 0.3);

    // Gentle S-curve contrast
    float lum = dot(color, LUM);
    float s = lum * lum * (3.0 - 2.0 * lum);
    float cf = lerp(lum, s, params.y * 0.15);
    if(lum > 0.001) color *= cf / lum;

    // Vignette
    float dist = length(uv - 0.5);
    float vig = 1.0 - smoothstep(0.0, params.w, dist);
    color *= lerp(1.0, vig, params.z);

    return float4(color, 1.0);
}
