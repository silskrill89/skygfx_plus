// Clamp.hlsl - Final output clamp (ps_3_0)
// Ensures all pixels are in [0,1] range.
// Prevents overbright, banding, and gamma issues.
// Optional: can do soft tonemap if enabled.
//
// c0 = {clampMin, clampMax, tonemapStrength, 0}

sampler2D sceneTex : register(s0);
float4 params      : register(c0); // x=min, y=max, z=tonemap

float3 ReinhardTonemap(float3 color, float white2)
{
    float lum = dot(color, float3(0.299, 0.587, 0.114));
    float scaledLum = lum * (1.0 + lum / white2) / (1.0 + lum);
    if(lum > 0.001)
        color *= scaledLum / lum;
    return color;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 color = tex2D(sceneTex, uv);

    // Soft tonemap (very subtle, only affects values > 1)
    if(params.z > 0.0)
    {
        color.rgb = ReinhardTonemap(color.rgb, 4.0);
    }

    // Hard clamp
    color.rgb = clamp(color.rgb, params.x, params.y);

    return color;
}
