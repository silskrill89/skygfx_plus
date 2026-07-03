// Water_VS.hlsl - Ocean water vertex shader with Gerstner wave displacement (vs_3_0)
//
// Gerstner waves: each wave has direction, steepness, wavelength.
// Vertex position is displaced along wave direction and upward.
// Tangent/bitangent computed from wave derivatives for normal mapping.
//
// Constants:
//   c0-c3 = WorldViewProjection
//   c4-c7 = World
//   c8 = (camPosX, camPosY, camPosZ, time)
//   c9 = (sunDirX, sunDirY, sunDirZ, numWaves)
//   c10 = waveDir1 + steepness1 (xy, z)
//   c11 = waveDir2 + steepness2 (xy, z)
//   c12 = waveDir3 + steepness3 (xy, z)
//   c13 = (wavelength1, wavelength2, wavelength3, amplitude)

uniform float4x4 worldViewProj : register(c0);
uniform float4x4 world         : register(c4);
uniform float4   camTime       : register(c8);  // xyz=camera pos, w=time
uniform float4   sunNumWaves   : register(c9);  // xyz=sun dir, w=num waves
uniform float4   wave1         : register(c10); // xy=dir, z=steepness
uniform float4   wave2         : register(c11); // xy=dir, z=steepness
uniform float4   wave3         : register(c12); // xy=dir, z=steepness
uniform float4   waveProps     : register(c13); // x=wavelength1, y=wavelength2, z=wavelength3, w=amplitude

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 position  : POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 worldPos  : TEXCOORD1;
    float3 viewDir   : TEXCOORD2;
    float3 lightDir  : TEXCOORD3;
    float4 screenPos : TEXCOORD4;
    float4 waveParams : TEXCOORD5; // foam factor in .w
};

// Gerstner wave displacement
// Returns displaced position and computes tangent for normal
float3 GerstnerWave(float2 pos, float2 dir, float steepness, float wavelength, float time,
                    inout float2 tangent, inout float2 bitangent)
{
    float k = 6.28318 / wavelength;
    float c = sqrt(9.81 / k);
    float2 d = normalize(dir);
    float f = k * (dot(d, pos) - c * time);
    float a = steepness / k;

    tangent += float2(-d.x * d.x * steepness * sin(f),
                      -d.x * d.y * steepness * sin(f));
    bitangent += float2(-d.x * d.y * steepness * sin(f),
                        -d.y * d.y * steepness * sin(f));

    return float3(d.x * a * cos(f),
                  d.y * a * cos(f),
                  a * sin(f));
}

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;

    float time = camTime.w;
    float2 pos = IN.position.xy;

    // Accumulate Gerstner waves
    float3 displacement = float3(0, 0, 0);
    float2 tangent = float2(1, 0);
    float2 bitangent = float2(0, 1);

    // Wave 1: large swell
    displacement += GerstnerWave(pos, wave1.xy, wave1.z, waveProps.x, time, tangent, bitangent);
    // Wave 2: medium chop
    displacement += GerstnerWave(pos, wave2.xy, wave2.z, waveProps.y, time, tangent, bitangent);
    // Wave 3: small detail
    displacement += GerstnerWave(pos, wave3.xy, wave3.z, waveProps.z, time, tangent, bitangent);

    // Apply displacement to vertex position
    float3 displacedPos = IN.position + displacement;

    // Transform
    OUT.position = mul(float4(displacedPos, 1.0), worldViewProj);
    OUT.screenPos = OUT.position;
    OUT.worldPos = mul(float4(displacedPos, 1.0), world);

    // UVs from displaced world position
    OUT.texCoord = displacedPos.xy * 0.02;

    // View direction
    OUT.viewDir = OUT.worldPos.xyz - camTime.xyz;

    // Light direction
    OUT.lightDir = -sunNumWaves.xyz;

    // Wave params for PS: foam factor from wave steepness
    float steepness = wave1.z + wave2.z + wave3.z;
    OUT.waveParams = float4(steepness, displacement.z, 0, saturate(steepness - 1.0));

    return OUT;
}
