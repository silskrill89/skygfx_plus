// NormalBuffer.hlsl - Stereo disparity -> normal map
// ps_3_0
// Takes two depth textures (main camera + offset camera) and reconstructs
// surface normals from the stereo disparity vector.
// Output: R=normalX (right), G=normalY (up), B=normalZ (away), A=depth

sampler2D depthMain   : register(s0);
sampler2D depthNormal : register(s1);

float4 stereoParams : register(c0); // (offset, scale, pixelSizeX, pixelSizeY)
float4 projInfo     : register(c1); // (projX, projY, projZ, projW) for depth reconstruction

float3 ReconstructViewPos(float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

struct PS_INPUT {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT {
    float4 Color : COLOR0;
};

PS_OUTPUT main(PS_INPUT IN)
{
    PS_OUTPUT OUT;

    float depthM = tex2D(depthMain, IN.TexCoord).r;
    float depthN = tex2D(depthNormal, IN.TexCoord).r;

    // Handle sky pixels (depth = 1.0)
    if(depthM >= 1.0 || depthN >= 1.0){
        OUT.Color = float4(0.5, 0.5, 1.0, 1.0); // flat normal pointing up
        return OUT;
    }

    // Reconstruct view-space positions from both cameras
    float3 posM = ReconstructViewPos(IN.TexCoord, depthM);
    float3 posN = ReconstructViewPos(IN.TexCoord, depthN);

    // Disparity vector = difference in view-space positions
    float3 disparity = posM - posN;

    // Normalize to get surface normal direction
    float3 normal = normalize(disparity + float3(0, 0, 0.0001)); // avoid div by zero

    // Remap from [-1,1] to [0,1] for storage in RGB
    float3 normalEncoded = normal * 0.5 + 0.5;

    // Scale intensity
    normalEncoded = lerp(float3(0.5, 0.5, 1.0), normalEncoded, stereoParams.y);

    OUT.Color = float4(normalEncoded, depthM);
    return OUT;
}
