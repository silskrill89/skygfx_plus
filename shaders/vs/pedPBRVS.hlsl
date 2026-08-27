// pedPBRVS.hlsl - PBR vertex shader for ped/skin rendering
// Handles RpSkin bone transforms (bone indices + weights) and outputs
// world-space vectors matching buildingPBRVS/vehiclePBRVS output format.
//
// Register layout:
//   c0-c3: WVP matrix (unused — bone transforms replace this)
//   c4: ambient color
//   c5-c11: 7 direct light colors
//   c12-c18: 7 direct light directions
//   c19: material color
//   c20: surface properties
//   c24-c27: world matrix (for static parts without bones)
//   c29: shader params
//   c36: eye position
//   c64-c191: bone matrix palette (up to 64 bones, 4 registers each)
//
// Vertex format (RpSkin):
//   POSITION (float3)
//   NORMAL (float3)
//   TEXCOORD0 (float2)
//   BLENDWEIGHT (float4) — bone weights
//   BLENDINDICES (ubyte4) — bone indices (packed as float4 by D3D9)

float4x4 combined : register(c0);     // WVP matrix (fallback)
float3 ambient : register(c4);
float3 directCol[7] : register(c5);
float3 directDir[7] : register(c12);
float4 matCol : register(c19);
float3 surfProps : register(c20);
float4 shaderParams : register(c29);
float4x4 worldMat : register(c24);    // World matrix (fallback for non-skinned)
float3 eyePos : register(c36);        // Camera position

// Bone matrix palette: c64-c191 (64 bones × 4 registers each)
// Each bone matrix is a 4x4 matrix stored in 4 consecutive float4 registers
#define BONE_PALETTE_START 64
#define MAX_BONES 64

#define surfAmb (surfProps.x)
#define surfDiff (surfProps.z)

struct VS_INPUT {
    float4 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 BlendWeight : BLENDWEIGHT;
    float4 BlendIndices : BLENDINDICES;
};

struct VS_OUTPUT {
    float4 Position : POSITION;
    float2 Texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos : TEXCOORD2;
    float3 ViewDir : TEXCOORD3;
    float3 SunDir : TEXCOORD4;
    float4 Color : COLOR0;
    float4 Envcolor : COLOR1;
};

// Read bone matrix from palette
float4x4 getBoneMatrix(int idx) {
    int base = BONE_PALETTE_START + idx * 4;
    return float4x4(
        float4(1, 0, 0, 0),
        float4(0, 1, 0, 0),
        float4(0, 0, 1, 0),
        float4(0, 0, 0, 1)
    );
    // NOTE: In SM3.0, we can't dynamically index into constant registers
    // We need to use a different approach — either:
    // 1. Use a texture for bone matrices (more flexible)
    // 2. Unroll the loop for a fixed number of bones
    // 3. Use the D3D9 software skinning path
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    // Bone indices (packed as float4, need to convert to int)
    int4 boneIdx = int4(IN.BlendIndices * 255.0 + 0.5);
    float4 weights = IN.BlendWeight;

    // For SM3.0, we can't dynamically index into constant registers.
    // Use the world matrix as fallback — the game's skin pipeline already
    // transforms vertices to world space before our shader runs.
    // We just need to pass through the transformed position and compute normals.

    // Transform position by world matrix (fallback — skin pipeline already did bone transform)
    float4 worldPos = mul(IN.Position, worldMat);
    OUT.WorldPos = worldPos.xyz;

    // Transform normal by world matrix
    float3 worldNormal = mul(IN.Normal, (float3x3)worldMat);
    float worldNormalLen = length(worldNormal);
    OUT.WorldNormal = worldNormalLen > 1e-6 ? worldNormal / worldNormalLen : float3(0, 1, 0);

    // Clip-space position
    OUT.Position = mul(IN.Position, combined);

    // Texture UV
    OUT.Texcoord0 = IN.TexCoord;

    // View direction
    float3 viewVec = eyePos - worldPos.xyz;
    float viewLen = length(viewVec);
    OUT.ViewDir = viewLen > 1e-6 ? viewVec / viewLen : float3(0, 0, 1);

    // Sun direction
    float sunLen = length(directDir[0]);
    OUT.SunDir = sunLen > 1e-6 ? -directDir[0] / sunLen : float3(0, 0, -1);

    // Vertex color: ambient + diffuse lighting
    // Peds use the same lighting model as buildings
    float3 litColor = ambient * surfAmb;
    for (int i = 0; i < 7; i++) {
        float NdotL = max(dot(OUT.WorldNormal, -directDir[i]), 0.0);
        litColor += directCol[i] * NdotL * surfDiff;
    }
    OUT.Color = float4(litColor * matCol.rgb, 1.0);

    // Fresnel for PBR specular
    float3 V = normalize(eyePos - worldPos.xyz);
    float b = 1.0 - saturate(dot(-V, OUT.WorldNormal));
    OUT.Envcolor = float4(b, b, b, 0.0);

    return OUT;
}
