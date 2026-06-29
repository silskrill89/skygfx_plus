// Simple stochastic sampling for ps_2_0 compatibility
// Uses a simple hash-based jitter

float2 hash2D2D(float2 s)
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

float4 tex2DStochastic(sampler2D tex, float2 UV)
{
    // Simple stochastic sampling - single sample with jitter
    // ps_2_0 doesn't support ddx/ddy or complex math
    float2 jitter = hash2D2D(UV * 100.0) * 0.01;
    return tex2D(tex, UV + jitter);
}