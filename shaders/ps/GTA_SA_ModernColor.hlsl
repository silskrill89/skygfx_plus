// GTA SA Modern Color Pipeline (ps_3_0)
// Combines: SA time-of-day colors + VCS grading + GTA IV filmic tonemap
// Ensures correct ambient lighting while maintaining modern look
//
// Constants:
//   c0 = (filmicA, filmicB, 1/whitePoint, desaturation)
//   c1 = (filmicC*B, filmicD*E, filmicD*F, filmicE/F)
//   c2 = (gamma, vcsBlend, saBlend, ambientStrength)
//   c3 = (bloomIntensity, exposure, vignetteIntensity, vignetteRadius)
//   c4 = (vignetteContrast, shadowR, shadowG, shadowB)
//   c5 = (ambientR, ambientG, ambientB, timeOfDay)
//   c6 = (tintR, tintG, tintB, tintStrength)
//
// Textures:
//   s0 = scene color (front buffer)
//   s1 = bloom texture (if available)

sampler2D sceneTex : register(s0);
sampler2D bloomTex : register(s1);

uniform float4 filmic0      : register(c0); // tonemap params
uniform float4 filmic1      : register(c1); // tonemap params
uniform float4 colorControl  : register(c2); // gamma, blend factors
uniform float4 postParams   : register(c3); // bloom, exposure, vignette
uniform float4 vignetteData : register(c4); // vignette contrast + shadow color
uniform float4 ambientData  : register(c5); // ambient color + time of day
uniform float4 tintData     : register(c6); // color tint

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

// Hable/Uncharted2 filmic tonemapping (from RAGE engine)
float3 filmicTonemap(float3 x)
{
    float A = filmic0.x;
    float B = filmic0.y;
    float C_mul_B = filmic1.x;
    float D_mul_E = filmic1.y;
    float D_mul_F = filmic1.z;
    float E_div_F = filmic1.w;
    float ooWhitePoint = filmic0.z;

    float3 numerator = x * (A * x + C_mul_B) + D_mul_E;
    float3 denominator = x * (A * x + B) + D_mul_F;
    float3 result = (numerator / denominator) - E_div_F;

    return result * ooWhitePoint;
}

// VCS-style color grading (warm shadows, cool highlights)
float3 vcsGrading(float3 color)
{
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    
    // VCS uses warm shadows and cool highlights
    float3 warmShadow = float3(1.1, 0.95, 0.85);
    float3 coolHighlight = float3(0.9, 0.95, 1.1);
    
    float shadowFactor = 1.0 - luma;
    float highlightFactor = luma;
    
    float3 graded = color;
    graded *= lerp(float3(1.0, 1.0, 1.0), warmShadow, shadowFactor * 0.3);
    graded *= lerp(float3(1.0, 1.0, 1.0), coolHighlight, highlightFactor * 0.2);
    
    return graded;
}

// SA time-of-day ambient integration
float3 applyAmbient(float3 color, float ambientStrength)
{
    float3 ambient = ambientData.rgb;
    float timeOfDay = ambientData.w; // 0=night, 0.5=dawn/dusk, 1=day
    
    // Modulate ambient by time of day
    ambient *= lerp(0.3, 1.0, timeOfDay);
    
    // Add ambient to dark areas (simulates GI bounce)
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    float shadowMask = 1.0 - smoothstep(0.0, 0.3, luma);
    
    color += ambient * shadowMask * ambientStrength;
    
    return color;
}

// Vignette effect (GTA IV dark edges)
float3 applyVignette(float3 color, float2 uv)
{
    float intensity = postParams.z;
    float radius = postParams.w;
    float contrast = vignetteData.x;

    float2 center = uv - 0.5;
    float dist = length(center);
    float vig = smoothstep(radius, radius * 0.5, dist);
    vig = pow(vig, contrast);

    // Dark blue vignette (GTA IV signature)
    float3 vigColor = vignetteData.yzw;
    color = lerp(vigColor, color, vig * intensity + (1.0 - intensity));
    return color;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 uv = IN.texCoord;
    float3 color = tex2D(sceneTex, uv).rgb;
    
    // Step 1: Apply SA ambient lighting (preserve time-of-day)
    float ambientStrength = colorControl.w;
    color = applyAmbient(color, ambientStrength);
    
    // Step 2: Add bloom if available
    float bloomIntensity = postParams.x;
    if(bloomIntensity > 0.001)
    {
        float3 bloom = tex2D(bloomTex, uv).rgb;
        color += bloom * bloomIntensity;
    }
    
    // Step 3: Apply exposure
    color *= postParams.y;
    
    // Step 4: Apply VCS-style color grading
    float vcsBlend = colorControl.y;
    if(vcsBlend > 0.001)
    {
        float3 vcsColor = vcsGrading(color);
        color = lerp(color, vcsColor, vcsBlend);
    }
    
    // Step 5: Apply GTA IV filmic tonemapping
    color = filmicTonemap(color);
    
    // Step 6: Desaturation (GTA IV muted look)
    float desat = filmic0.w;
    if(desat > 0.001)
    {
        float luma = dot(color, float3(0.299, 0.587, 0.114));
        color = lerp(float3(luma, luma, luma), color, 1.0 - desat);
    }
    
    // Step 7: Gamma correction
    float gamma = colorControl.x;
    color = pow(max(color, 0.0), 1.0 / gamma);
    
    // Step 8: Apply SA color tint (preserves game's color identity)
    float saBlend = colorControl.z;
    if(saBlend > 0.001)
    {
        float3 saTint = tintData.rgb;
        float tintStrength = tintData.w;
        color = lerp(color, color * saTint, saBlend * tintStrength);
    }
    
    // Step 9: Apply vignette
    color = applyVignette(color, uv);
    
    // Step 10: Shadow color tinting (blue shadows like GTA IV)
    float3 shadowColor = vignetteData.yzw;
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    float shadowMask = 1.0 - smoothstep(0.0, 0.15, luma);
    color = lerp(color, color * shadowColor, shadowMask * 0.2);
    
    return float4(color, 1.0);
}
