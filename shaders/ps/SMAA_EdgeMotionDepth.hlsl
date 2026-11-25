// Combined Edge + Motion + Depth Detection (ps_3_0)
// Single pass that outputs data for both SMAA and SSAO
// RG = luma edges (SMAA blend weights)
// B = motion edge (temporal stabilization)
// A = packed depth (SSAO occlusion input)
//
// Constants:
//   c0 = (lumaThreshold, motionThreshold, motionScale, depthScale)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (projInfo.x, projInfo.y, projInfo.z, projInfo.w) - for depth reconstruction
//
// Textures:
//   s0 = current frame (front buffer)
//   s1 = previous frame
//   s2 = depth buffer (INTZ or packed)
//   s3 = velocity buffer (RG=velocity XY, B=magnitude)
//   s4 = HALF-RES view-space normal buffer (RGB = n*0.5+0.5, A = depth/far)
//        — OPTIONAL (m2293 C2). Unbound/NULL when normalBufferEnable=0 or
//        the buffer has no completed write; NormalEdgeTerm then returns
//        (0,0) and this shader takes the original luma-only path, i.e.
//        bit-identical output to the pre-normal build.

sampler2D currentTex  : register(s0);
sampler2D prevTex     : register(s1);
sampler2D depthTex    : register(s2);
sampler2D velocityTex : register(s3);
sampler2D normalTex   : register(s4);

uniform float4 edgeParams : register(c0); // x=lumaThresh, y=motionThresh, z=motionScale, w=cameraMovement
uniform float4 screenSize : register(c1);
uniform float4 projInfo   : register(c2);

#define SMAA_RT_METRICS float4(screenSize.zw, screenSize.xy)
#define SMAA_HLSL_3 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#define SMAA_THRESHOLD edgeParams.x

#include "SMAA.hlsli"

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float PackDepth(float depth)
{
    // Pack depth into [0,1] range for storage in alpha channel
    return saturate(depth * edgeParams.w);
}

// ---------------------------------------------------------------------------
// m2293 C2 — normal-buffer edge assist.
//
// The stock SMAALumaEdgeDetectionPS (SMAA.hlsli) DISCARDS the pixel when it
// finds no luma edge, which would kill any term added after the call — and
// the target case for normals IS "geometric edge on a flat-coloured surface"
// (no luma edge). So the edge decision is restructured: the normal term is
// evaluated FIRST (1 tap when s4 is unbound), and the stock helper is only
// used on pixels where the normal term is inactive — preserving its original
// values AND its original discard exactly.
// ---------------------------------------------------------------------------

// Half-res view-space normal buffer -> RG edge weights, or (0,0) when the
// term must not run. Content gates (no constant register needed, so nothing
// to upload and nothing stale to leak into later passes):
//   * s4 unbound (normalBufferEnable=0 / no completed write): tex2D reads
//     (0,0,0,0) -> decode (-1,-1,-1), length 1.73 -> outside the window.
//   * invalid-normal marker 0x808080 (fresh D3DPOOL_DEFAULT RT, cleared by
//     GetNormalBufferTexture): decode (0,0,0), length 0 -> outside window.
//   * valid content: NormalBuffer.hlsl normalizes to unit length (8-bit
//     quantisation keeps |n| within ~[0.986, 1.014]) -> inside window.
float2 NormalEdgeTerm(float2 texcoord)
{
    float3 nC = tex2D(normalTex, texcoord).xyz * 2.0 - 1.0;
    float lC = length(nC);
    if(lC < 0.7 || lC > 1.3)
        return float2(0.0, 0.0);

    // The buffer is HALF-RES (DrawNormalBufferToTexture: camW/2 x camH/2)
    // while this pass runs FULL-RES: one half-res texel = 2 full-res pixels.
    // The C++ bind pins s4 LINEAR/CLAMP, so a tap at +/-2px lands on a
    // half-res texel centre for even full-res pixels and keeps the same
    // 0.75/0.25 blend for odd ones — always a true neighbour difference.
    float2 hp = screenSize.zw * 2.0;
    float3 nE = tex2D(normalTex, texcoord + float2(hp.x, 0.0)).xyz * 2.0 - 1.0;
    float3 nN = tex2D(normalTex, texcoord + float2(0.0, -hp.y)).xyz * 2.0 - 1.0;
    float lE = length(nE);
    float lN = length(nN);
    if(lE < 0.7 || lE > 1.3 || lN < 0.7 || lN > 1.3)
        return float2(0.0, 0.0);

    // Unit-vector dot = cos(theta). Dead-zone 0.0125 (= ~9 degrees) before
    // the x4 scale: 8-bit quantisation / reconstruction noise on a flat
    // surface yields 1-dot ~ 1e-4..1e-3 and must come out as EXACT zero —
    // BlendWeight's search loops test `edge == 0.0` to stop walking, so a
    // hairline non-zero would stretch AA along flat areas. Ramp after the
    // dead-zone: ~10deg->0.07, 20deg->0.19, 30deg->0.49, 45deg->1.0.
    // RG orientation matches SMAALumaEdgeDetectionPS: .x from an X-direction
    // (east) difference, .y from a Y-direction (north) difference.
    float eX = saturate(((1.0 - dot(nC, nE)) - 0.0125) * 4.0);
    float eY = saturate(((1.0 - dot(nC, nN)) - 0.0125) * 4.0);
    return float2(eX, eY);
}

// VERBATIM copy of SMAALumaEdgeDetectionPS (SMAA.hlsli) with ONLY the early
// `discard` removed. Op sequence is identical, so the luma values are
// bit-identical to the stock helper; keep in sync with SMAA.hlsli if the
// include is ever updated. Reached only when NormalEdgeTerm found an edge.
float2 LumaEdgesNoDiscard(float2 texcoord, float4 offset[3])
{
    float2 threshold = float2(SMAA_THRESHOLD, SMAA_THRESHOLD);

    float3 weights = float3(0.2126, 0.7152, 0.0722);
    float L = dot(SMAASamplePoint(currentTex, texcoord).rgb, weights);

    float Lleft = dot(SMAASamplePoint(currentTex, offset[0].xy).rgb, weights);
    float Ltop  = dot(SMAASamplePoint(currentTex, offset[0].zw).rgb, weights);

    float4 delta;
    delta.xy = abs(L - float2(Lleft, Ltop));
    float2 edges = step(threshold, delta.xy);

    float Lright = dot(SMAASamplePoint(currentTex, offset[1].xy).rgb, weights);
    float Lbottom = dot(SMAASamplePoint(currentTex, offset[1].zw).rgb, weights);
    delta.zw = abs(L - float2(Lright, Lbottom));

    float2 maxDelta = max(delta.xy, delta.zw);

    float Lleftleft = dot(SMAASamplePoint(currentTex, offset[2].xy).rgb, weights);
    float Ltoptop = dot(SMAASamplePoint(currentTex, offset[2].zw).rgb, weights);
    delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return edges;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 texcoord = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // ===== Luma Edge Detection (RG) + normal-edge assist =====
    float4 offset[3];
    offset[0] = SMAA_RT_METRICS.xyxy * float4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = SMAA_RT_METRICS.xyxy * float4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = SMAA_RT_METRICS.xyxy * float4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;

    // Normal term first (m2293 C2): 1 tap when s4 is unbound and it returns
    // (0,0) -> stock path below, including the helper's no-edge discard.
    float2 normalEdges = NormalEdgeTerm(texcoord);
    float2 edges;
    if(normalEdges.x + normalEdges.y == 0.0){
        // Original path, byte-for-byte: stock helper with its early discard.
        edges = SMAALumaEdgeDetectionPS(texcoord, offset, currentTex);
    } else {
        // Normal edge present: must NOT discard (that is the whole point —
        // flat colour, geometric discontinuity), so take the no-discard luma
        // copy and union the two edge maps.
        edges = max(LumaEdgesNoDiscard(texcoord, offset), normalEdges);
    }
    
    // ===== Motion Detection (B) =====
    // Camera movement increases threshold to reduce jitter
    float dynamicMotionThresh = edgeParams.y + (edgeParams.w * 0.5f); // base + camera movement
    
    // Signal 1: Luma diff (scene changes — explosions, spawning, lighting shifts)
    float3 current = tex2D(currentTex, texcoord).rgb;
    float3 prev = tex2D(prevTex, texcoord).rgb;
    float lumaMotion = abs(Luma(current) - Luma(prev)) * edgeParams.z;
    
    // Signal 2: Velocity buffer (actual per-pixel motion vectors from depth reconstruction)
    float velocityMag = tex2D(velocityTex, texcoord).b;
    float velocityMotion = velocityMag * edgeParams.z;
    
    // Combine: max of both signals catches all motion sources
    float combinedMotion = max(lumaMotion, velocityMotion);
    
    // Check neighbors for luma gradient (spatial enhancement)
    float3 prevN = tex2D(prevTex, texcoord + float2(0, -pixel.y)).rgb;
    float3 prevS = tex2D(prevTex, texcoord + float2(0, pixel.y)).rgb;
    float3 prevE = tex2D(prevTex, texcoord + float2(pixel.x, 0)).rgb;
    float3 prevW = tex2D(prevTex, texcoord + float2(-pixel.x, 0)).rgb;
    
    float maxSpatialMotion = max(combinedMotion, max(
        max(abs(Luma(current) - Luma(prevN)), abs(Luma(current) - Luma(prevS))),
        max(abs(Luma(current) - Luma(prevE)), abs(Luma(current) - Luma(prevW)))
    )) * edgeParams.z;
    
    // Apply dynamic threshold based on camera movement
    float motionEdge = saturate(maxSpatialMotion / max(0.001f, dynamicMotionThresh));
    
    // ===== Depth Sampling (A) =====
    float depth = tex2D(depthTex, texcoord).r;
    float packedDepth = PackDepth(depth);
    
    // Output: RG=luma(+normal) edges, B=motion, A=depth
    return float4(edges, motionEdge, packedDepth);
}
