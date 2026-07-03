// Modern Color Filter - Clean color grading without motion trails
// Based on timecyc values, no frame blending

float4 main(uniform sampler2D Diffuse : register(s0),
            uniform float4 RGB1 : register(c0),
            uniform float4 RGB2 : register(c1),
            in float2 Tex0 : TEXCOORD0) : COLOR0
{
    float4 color = tex2D(Diffuse, Tex0);
    
    // Apply RGB1 as color multiplier (timecyc-driven)
    // Use lerp to blend between original and graded to avoid over-darkening
    float3 graded = color.rgb * RGB1.rgb;
    color.rgb = lerp(color.rgb, graded, 0.7);
    
    // Apply RGB2 as subtle additive tint
    color.rgb += RGB2.rgb * 0.05;
    
    // Darken midtones by applying gamma correction (lower grey middle point)
    color.rgb = pow(color.rgb, 1.2);
    
    // Contrast boost
    color.rgb = saturate((color.rgb - 0.5) * 1.1 + 0.5);
    
    return color;
}
