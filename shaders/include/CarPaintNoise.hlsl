// Perlin Noise for Car Paint Sparkle (hlsl include)
// Creates metallic flake pattern like Need for Speed car paint
// Optimized for ps_3_0

// Hash function for noise
float2 hash22(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

// 2D Perlin noise
float perlinNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f); // Smoothstep
    
    float a = dot(hash22(i + float2(0.0, 0.0)), f - float2(0.0, 0.0));
    float b = dot(hash22(i + float2(1.0, 0.0)), f - float2(1.0, 0.0));
    float c = dot(hash22(i + float2(0.0, 1.0)), f - float2(0.0, 1.0));
    float d = dot(hash22(i + float2(1.0, 1.0)), f - float2(1.0, 1.0));
    
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Fractal Brownian Motion (multiple octaves of noise)
float fbm(float2 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for(int i = 0; i < octaves; i++)
    {
        value += amplitude * perlinNoise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Metallic flake noise (high frequency, sparkle pattern)
float metallicFlake(float2 p, float scale)
{
    // Multiple layers of high-frequency noise
    float n1 = perlinNoise(p * scale);
    float n2 = perlinNoise(p * scale * 2.3 + 17.5);
    float n3 = perlinNoise(p * scale * 4.7 + 33.2);
    
    // Combine for sparkle effect
    float sparkle = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
    
    // Threshold for flake visibility
    return smoothstep(0.3, 0.7, sparkle);
}

// Car paint 3-layer model
float3 carPaint(float3 baseColor, float2 uv, float3 normal, float3 viewDir, 
                float metallic, float roughness, float flakeScale)
{
    // Layer 1: Base coat (dark, metallic)
    float3 base = baseColor * 0.3;
    
    // Layer 2: Color coat with metallic flakes
    float flake = metallicFlake(uv, flakeScale);
    float3 colorLayer = baseColor * (0.7 + flake * 0.3);
    
    // Layer 3: Clear coat (shiny, reflective)
    float NdotV = saturate(dot(normal, viewDir));
    float fresnel = pow(1.0 - NdotV, 5.0);
    float3 clearCoat = float3(1.0, 1.0, 1.0) * fresnel * 0.3;
    
    // Combine layers
    float3 paint = lerp(base, colorLayer, metallic);
    paint += clearCoat;
    
    return paint;
}
