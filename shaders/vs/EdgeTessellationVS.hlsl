// Edge Smoothing Vertex Shader (vs_3_0)
// Smooths jagged edges by averaging vertex positions
// Reads from SMAA edge buffer to detect sharp edges
// Creates smoother silhouettes without adding geometry
//
// Constants:
//   c0-c3   = World*View*Projection matrix
//   c4      = (smoothStrength, edgeThreshold, normalBlend, 0)
//   c5      = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = edge buffer from SMAA (RG=luma edges)

sampler2D edgeTex : register(s0);

uniform float4x4 WorldViewProj : register(c0);
uniform float4 smoothParams    : register(c4); // x=strength, y=threshold, z=normalBlend
uniform float4 screenSize      : register(c5);

struct VS_INPUT
{
    float4 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
    float3 worldPos : TEXCOORD1;
    float3 worldNrm : TEXCOORD2;
};

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;
    
    // Transform position to clip space
    float4 clipPos = mul(IN.position, WorldViewProj);
    
    // Calculate screen-space UV for edge buffer sampling
    float2 screenUV = clipPos.xy / clipPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;
    
    // Sample edge buffer at vertex position
    float4 edgeSample = tex2Dlod(edgeTex, float4(screenUV, 0, 0));
    float edgeStrength = max(edgeSample.r, edgeSample.g);
    
    // Calculate view-space normal for smoothing direction
    float3 worldNormal = normalize(IN.normal);
    
    // Edge-based smoothing: blend vertex toward average
    // This creates a smoother appearance by pulling vertices inward at edges
    float smoothFactor = edgeStrength * smoothParams.x;
    
    // Average position calculation (fake tessellation effect)
    // Move vertex slightly toward center of face along normal
    float3 smoothedPos = IN.position.xyz;
    
    // Pull vertex inward along normal (reverse of extrusion)
    // This smooths sharp edges by averaging the surface
    smoothedPos -= worldNormal * smoothFactor * smoothParams.z;
    
    // Apply smoothed position
    float4 finalPos = float4(smoothedPos, IN.position.w);
    OUT.position = mul(finalPos, WorldViewProj);
    OUT.texcoord = IN.texcoord;
    OUT.color = IN.color;
    OUT.worldPos = IN.position.xyz;
    OUT.worldNrm = worldNormal;
    
    return OUT;
}
