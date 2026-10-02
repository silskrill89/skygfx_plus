// Water_VS.hlsl - Flat water plane vertex shader (vs_3_0)
//
// REWRITE (shading-only waves): the water plane geometry is NEVER displaced.
// All wave motion lives in the pixel shader as animated normal perturbation /
// scrolling normal detail. The flat plane therefore always stays at exactly
// its geometry level and can no longer pop or intersect land/other water at
// the coast when waves animate.
//
// Constants:
//   c0-c3 = WorldViewProjection
//   c4-c7 = World
//   c8 = (camPosX, camPosY, camPosZ, time)
//   c9 = (sunDirX, sunDirY, sunDirZ, sunBrightness)

uniform float4x4 worldViewProj : register(c0);
uniform float4x4 world         : register(c4);
uniform float4   camTime       : register(c8);  // xyz=camera pos, w=time
uniform float4   sunDir        : register(c9);  // xyz=sun dir (negated), w=brightness

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
    float4 vtxColor  : COLOR0;
};

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;

    // Flat plane: position passes through completely untouched — no
    // Gerstner/vertex displacement of any kind (req: geometry level fixed).
    OUT.position  = mul(float4(IN.position, 1.0), worldViewProj);
    OUT.screenPos = OUT.position;
    OUT.worldPos  = mul(float4(IN.position, 1.0), world);

    // Base UV: the game fills the water render buffer with the original SA
    // water texture mapping, so passing it through keeps the tile scale
    // identical to the original SA water texture scale. Degenerate-UV guard:
    // fall back to a world-space mapping per vertex (SM3.0 allows branches
    // without dependent texture reads).
    float2 baseUV = IN.texCoord;
    if(abs(baseUV.x) + abs(baseUV.y) < 1e-6)
        baseUV = IN.position.xy * 0.0625;
    OUT.texCoord = baseUV;

    // Unnormalized camera->surface vector; the PS normalizes with a 1e-7 guard.
    OUT.viewDir = OUT.worldPos.xyz - camTime.xyz;

    OUT.lightDir = -sunDir.xyz;

    // Game vertex colour/alpha — SA modulates its water quads through COLOR0;
    // the PS uses it as a modulation so per-vertex shore fades (if present
    // in the buffer) survive.
    OUT.vtxColor = IN.color;

    return OUT;
}
