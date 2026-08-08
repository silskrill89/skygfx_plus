// VelocityReconstruct.hlsl - Per-pixel motion vectors via depth reconstruction (ps_3_0)
// GPU Gems 3 Ch. 27: reconstruct world-space position from depth, then project
// into previous frame's clip space to derive screen-space motion vectors.

sampler2D depthTex : register(s0);

uniform float4x4 invCurrentVP : register(c0); // inverse(currentView * currentProj)
uniform float4x4 prevVP       : register(c4); // previousView * previousProj
uniform float4   screenParams : register(c8);  // (W, H, 1/W, 1/H)

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    // Sample current frame depth (INTZ format, depth in .r channel)
    float currentDepth = tex2D(depthTex, IN.texCoord).r;

    // Reconstruct NDC clip-space position from UV + depth
    // UV (0,0)=top-left → NDC (-1,+1)=top-left in D3D9
    float clipX = IN.texCoord.x * 2.0 - 1.0;
    float clipY = (1.0 - IN.texCoord.y) * 2.0 - 1.0;
    float4 clipPos = float4(clipX, clipY, currentDepth, 1.0);

    // World position = clipPos * inverseVP
    float4 worldPos = mul(clipPos, invCurrentVP);
    // Guard division: w can be zero for degenerate geometry
    worldPos.xyz /= max(abs(worldPos.w), 1e-7);

    // Project into previous frame's clip space
    float4 prevClipPos = mul(float4(worldPos.xyz, 1.0), prevVP);
    float prevW = max(abs(prevClipPos.w), 1e-7);

    // Previous UV from clip space
    float2 prevUV;
    prevUV.x = prevClipPos.x / prevW;
    prevUV.y = prevClipPos.y / prevW;
    prevUV = prevUV * float2(0.5, -0.5) + 0.5; // NDC → [0,1]

    // Velocity = current screen position - previous screen position
    float2 velocity = IN.texCoord - prevUV;

    // Pack velocity into [0,1] range for storage in A8R8G8B8
    float2 packedVelocity = velocity * 0.5 + 0.5;
    float motionMagnitude = length(velocity);

    return float4(packedVelocity.x, packedVelocity.y, motionMagnitude, 1.0);
}
