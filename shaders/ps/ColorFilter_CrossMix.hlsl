// Cross-Mixable Color Filter (ps_3_0)
// Blends between two different color grading styles
// Supports: PS2, PC, Mobile, III, VC, VCS, GTAIV tonemapping
//
// Constants:
//   c0 = (primaryFilter, secondaryFilter, mixFactor, 0)
//   c1 = (rgb1.r, rgb1.g, rgb1.b, rgb1.a)
//   c2 = (rgb2.r, rgb2.g, rgb2.b, rgb2.a)
//   c3 = (desaturation, gamma, vignetteIntensity, vignetteRadius)
//   c4 = (vignetteContrast, bloomIntensity, exposure, 0)
//
// Textures:
//   s0 = current frame (front buffer)

sampler2D colorTex : register(s0);

uniform float4 filterParams : register(c0); // x=primary, y=secondary, z=mixFactor
uniform float4 color1       : register(c1); // rgb1 from timecycle
uniform float4 color2       : register(c2); // rgb2 from timecycle
uniform float4 ivParams1    : register(c3); // IV desaturation, gamma, vignette
uniform float4 ivParams2    : register(c4); // IV bloom, exposure

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// PS2 color filter: additive with alpha modulation
float3 FilterPS2(float3 color, float4 rgb1, float4 rgb2)
{
    // PS2 uses 2x modulation and additive blending
    float3 tint1 = rgb1.rgb * (rgb1.a / 128.0);
    float3 tint2 = rgb2.rgb * (rgb2.a / 128.0);
    return color * (1.0 + tint1) + tint2;
}

// PC color filter: multiplicative with alpha blend
float3 FilterPC(float3 color, float4 rgb1, float4 rgb2)
{
    float a1 = rgb1.a / 128.0;
    float a2 = rgb2.a / 128.0;
    float3 mult = float3(1.0, 1.0, 1.0) + rgb1.rgb * a1 + rgb2.rgb * a2;
    return color * mult;
}

// Mobile color filter: color grading with normalization
float3 FilterMobile(float3 color, float4 rgb1, float4 rgb2)
{
    float3 tint = rgb1.rgb + rgb2.rgb;
    float len = length(tint);
    if(len > 0.001)
        tint /= len;
    
    // Create grading matrix
    float3 red = float3(1.5 + tint.r * 1.732, 0, 0) * 0.4;
    float3 green = float3(0, 1.5 + tint.g * 1.732, 0) * 0.4;
    float3 blue = float3(0, 0, 1.5 + tint.b * 1.732) * 0.4;
    
    return float3(dot(red, color), dot(green, color), dot(blue, color));
}

// GTA III trails: warm sepia tone
float3 FilterIII(float3 color, float4 rgb1, float4 rgb2)
{
    // III uses a warm color shift
    float luma = Luma(color);
    float3 warm = float3(1.1, 0.95, 0.8);
    float3 cool = float3(0.9, 0.95, 1.1);
    float3 tint = lerp(cool, warm, luma);
    return color * tint * (1.0 + rgb1.rgb * 0.3);
}

// GTA VC trails: cool blue/purple tint
float3 FilterVC(float3 color, float4 rgb1, float4 rgb2)
{
    // VC uses a cool color shift with saturation boost
    float luma = Luma(color);
    float3 cool = float3(0.85, 0.9, 1.15);
    float3 warm = float3(1.1, 1.0, 0.9);
    float3 tint = lerp(warm, cool, luma);
    return color * tint * (1.0 + rgb1.rgb * 0.2);
}

// GTA VCS trails: enhanced VC with more saturation
float3 FilterVCS(float3 color, float4 rgb1, float4 rgb2)
{
    // VCS is similar to VC but with more color pop
    float luma = Luma(color);
    float3 cool = float3(0.8, 0.85, 1.2);
    float3 warm = float3(1.15, 1.05, 0.85);
    float3 tint = lerp(warm, cool, luma);
    
    // Saturation boost
    float3 saturated = lerp(float3(luma, luma, luma), color, 1.3);
    return saturated * tint * (1.0 + rgb1.rgb * 0.25);
}

// GTA IV filmic tonemapping
float3 FilterGTAIV(float3 color, float4 rgb1, float4 rgb2, float2 uv)
{
    float desat = ivParams1.x;
    float gamma = ivParams1.y;
    float vignetteInten = ivParams1.z;
    float vignetteRadius = ivParams1.w;
    float vignetteContrast = ivParams2.x;
    float bloomInten = ivParams2.y;
    float exposure = ivParams2.z;
    
    // Apply exposure
    color *= exposure;
    
    // Filmic tonemapping (Uncharted 2 style)
    float A = 0.15; // Shoulder Strength
    float B = 0.50; // Linear Strength
    float C = 0.10; // Linear Angle
    float D = 0.20; // Toe Strength
    float E = 0.02; // Toe Numerator
    float F = 0.30; // Toe Denominator
    
    float3 x = color;
    color = ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
    
    // Desaturation in darks
    float luma = Luma(color);
    color = lerp(color, float3(luma, luma, luma), desat * (1.0 - luma));
    
    // Gamma correction
    color = pow(max(color, 0.0), 1.0 / gamma);
    
    // Vignette
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center);
    float vignette = smoothstep(vignetteRadius, vignetteRadius - 0.3, dist);
    vignette = pow(vignette, vignetteContrast);
    color *= lerp(1.0, vignette, vignetteInten);
    
    return color;
}

// Apply a specific filter by index
float3 ApplyFilter(float3 color, int filterIndex, float4 rgb1, float4 rgb2, float2 uv)
{
    if(filterIndex == 0) return FilterPS2(color, rgb1, rgb2);
    if(filterIndex == 1) return FilterPC(color, rgb1, rgb2);
    if(filterIndex == 2) return FilterMobile(color, rgb1, rgb2);
    if(filterIndex == 3) return FilterIII(color, rgb1, rgb2);
    if(filterIndex == 4) return FilterVC(color, rgb1, rgb2);
    if(filterIndex == 5) return FilterVCS(color, rgb1, rgb2);
    if(filterIndex == 6) return FilterGTAIV(color, rgb1, rgb2, uv);
    return color; // NONE
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float3 color = tex2D(colorTex, tex).rgb;
    
    int primaryFilter = (int)filterParams.x;
    int secondaryFilter = (int)filterParams.y;
    float mixFactor = filterParams.z;
    
    // Apply primary filter
    float3 result1 = ApplyFilter(color, primaryFilter, color1, color2, tex);
    
    // If mixing with a different filter
    if(mixFactor > 0.001 && primaryFilter != secondaryFilter)
    {
        // Apply secondary filter
        float3 result2 = ApplyFilter(color, secondaryFilter, color1, color2, tex);
        
        // Blend between the two results
        float3 result = lerp(result1, result2, mixFactor);
        return float4(result, 1.0);
    }
    
    return float4(result1, 1.0);
}
