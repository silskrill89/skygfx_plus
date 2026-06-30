// Edge Tessellation Vertex Shader (vs_3_0)
// Smooths sharp edges by displacing vertices along normals
// Reads from the SMAA edge buffer to detect sharp edges
// Mimics 2x subdivision smoothing on edges only
//
// Constants:
//   c0-c3   = World*View*Projection matrix
//   c4      = (edgeStrength, normalThreshold, smoothingFactor, 0)
//   c5      = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = edge buffer from SMAA (RG=luma edges, B=motion, A=depth)

sampler2D edgeTex : register(s0);

// Vertex shader input
struct VS_INPUT
{
    float4 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

// Vertex shader output
struct VS_OUTPUT
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
    float3 worldPos : TEXCOORD1;
    float3 worldNrm : TEXCOORD2;
};

uniform float4x4 WorldViewProj : register(c0);
uniform float4 edgeParams      : register(c4); // x=strength, y=normalThreshold, z=smoothingFactor
uniform float4 screenSize      : register(c5); // xy=resolution, zw=1/resolution

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;
    
    // Transform position to clip space
    float4 clipPos = mul(IN.position, WorldViewProj);
    
    // Calculate screen-space UV for edge buffer sampling
    float2 screenUV = clipPos.xy / clipPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y; // Flip Y for D3D
    
    // Sample edge buffer at vertex position
    float4 edgeSample = tex2Dlod(edgeTex, float4(screenUV, 0, 0));
    float edgeStrength = max(edgeSample.r, edgeSample.g); // Use luma edges
    
    // Calculate view-space normal for smoothing direction
    float3 worldNormal = normalize(IN.normal);
    
    // Displace vertex along normal based on edge strength
    // This mimics 2x subdivision by pushing vertices outward at edges
    float displacement = edgeStrength * edgeParams.x * edgeParams.z;
    
    // Apply displacement along normal
    float4 displacedPos = IN.position;
    displacedPos.xyz += worldNormal * displacement;
    
    // Recalculate position with displacement
    OUT.position = mul(displacedPos, WorldViewProj);
    OUT.texcoord = IN.texcoord;
    OUT.color = IN.color;
    OUT.worldPos = IN.position.xyz;
    OUT.worldNrm = worldNormal;
    
    return OUT;
}
