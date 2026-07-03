// shaders.h
// Central shader hub — common types, edge detection, noise, shader loading.
// All shader-related files include this.
//
// Architecture:
//   shaders.h/cpp      — core (edge detect, noise, common math)
//   veh_shaders.cpp    — vehicle shader data bridge
//   buildingPipe.cpp   — building rendering (connects to shaders.h)
//   postfx.cpp         — post-effects (connects to shaders.h)

#pragma once

// ============================================================
// Common includes
// ============================================================

#include "skygfx.h"
#include <math.h>
#include <string.h>

// ============================================================
// Shader types and constants
// ============================================================

#define SHADER_MAX_PASSES 4
#define SHADER_MAX_LIGHTS 6
#define SHADER_MAX_TEXTURES 8

// Edge detection thresholds
#define EDGE_DEPTH_THRESHOLD    0.01f
#define EDGE_NORMAL_THRESHOLD  0.4f
#define EDGE_LUMA_THRESHOLD    0.1f

// ============================================================
// Edge detection — base functions for SSAO, SMAA, outlines
// ============================================================

struct EdgeData {
    float depth;        // scene depth
    float3 normal;      // world-space normal
    float luma;         // luminance
    float edgeStrength; // combined edge factor
};

// Compute edge strength from depth/normal/luminance derivatives
inline float ComputeEdgeStrength(float depthDX, float depthDY,
                                  float3 normalDX, float3 normalDY,
                                  float lumaDX, float lumaDY)
{
    float depthEdge = saturate(abs(depthDX) + abs(depthDY)) / EDGE_DEPTH_THRESHOLD;
    float normalEdge = length(normalDX) + length(normalDY);
    normalEdge = saturate(normalEdge / EDGE_NORMAL_THRESHOLD);
    float lumaEdge = saturate(abs(lumaDX) + abs(lumaDY)) / EDGE_LUMA_THRESHOLD;

    return max(max(depthEdge, normalEdge), lumaEdge);
}

// ============================================================
// Procedural noise — common functions for all shaders
// ============================================================

inline float Hash11(float p){
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

inline float Hash21(float2 p){
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

inline float2 Hash22(float2 p){
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

inline float ValueNoise(float2 p){
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash21(i + float2(0,0)), Hash21(i + float2(1,0)), u.x),
                lerp(Hash21(i + float2(0,1)), Hash21(i + float2(1,1)), u.x), u.y);
}

inline float FBMNoise(float2 p, int octaves){
    float v = 0.0;
    float a = 0.5;
    float2 shift = float2(100.0, 100.0);
    for(int i = 0; i < octaves; i++){
        v += a * ValueNoise(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// ============================================================
// Fresnel helpers (shared across vehicle/building shaders)
// ============================================================

inline float SchlickFresnel(float cosTheta, float F0){
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

inline float3 SchlickFresnelRGB(float cosTheta, float3 F0){
    float f = pow(saturate(1.0 - cosTheta), 5.0);
    return F0 + (1.0 - F0) * f;
}

// ============================================================
// Shader loading helpers (called from pipelinecommon.cpp)
// ============================================================

// All shader pointers — declared extern, defined in pipelinecommon.cpp
// Vehicle shaders
extern void *VehiclePBR_Modern;
extern void *Glass_Vehicle;
extern void *Rubber_Vehicle;

// Building shaders
extern void *Building_PBR;
extern void *Building_Reflection;

// PostFX shaders
extern void *SSAO;
extern void *SMAA;
extern void *NormalBufferShader;
extern void *PipeChainShader;

// Common shaders
extern void *vehiclePBRVS;
extern void *simplePS;

// ============================================================
// Texture binding helpers
// ============================================================

inline void BindTexture(int stage, IDirect3DTexture9 *tex){
    extern IDirect3DDevice9 *d3d9device;
    if(d3d9device)
        d3d9device->SetTexture(stage, tex);
}

inline void SetSamplerLinear(int stage){
    extern IDirect3DDevice9 *d3d9device;
    if(d3d9device){
        d3d9device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3d9device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
}

inline void SetSamplerClamp(int stage){
    extern IDirect3DDevice9 *d3d9device;
    if(d3d9device){
        d3d9device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d3d9device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
}
