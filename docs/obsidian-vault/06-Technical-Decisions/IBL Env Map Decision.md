---
tags: [technical-decision, ibl, env-map, reflections]
created: 2025-01-02
updated: 2026-07-15
---

# IBL Env Map Decision

## Decision
**IBL (Image-Based Lighting) should tint/boost the env map reflection, NOT replace it.**

## Context
Initially, we replaced the scene env map (reflectionTex) with the IBL cubemap (g_iblTex) for car reflections. This eliminated "melting" artifacts caused by the low-res 128x128 scene capture lagging behind camera movement.

However, the user clarified: *"ibl should affect the tinting of a mesh not replace its environmental map, it can boost it but that's it"*

## Problem with IBL-Only Approach
- IBL is generated from sky parameters (timecycle colors)
- It provides ambient sky color but lacks actual scene reflections
- Cars looked flat/grey because they weren't reflecting the environment
- Lost the visual richness of actual scene reflections (buildings, road, etc.)

## Solution
Use **both** env map and IBL:
1. **Env map (s1)**: Primary reflection source — actual scene capture
2. **IBL (s3)**: Tint/boost — adds sky color variation to the reflection

### Implementation
```hlsl
// Sample scene env map (s1) — actual reflection of surroundings
float2 envReflUV = SphereEnvMapUV(R, V);
float3 envRefl = tex2D(envMapTex, envReflUV).rgb;

// Sample IBL (s3) — sky ambient color, tints/boosts the env map
float3 iblTint = tex2D(iblTex, envReflUV).rgb;

// Layer 2 = env map (primary) tinted by IBL sky color, Fresnel-modulated
float3 layer2 = envRefl * iblTint * paintTint * clearCoatF * envFresnel;
```

## Benefits
- **Preserves scene reflections**: Cars reflect actual environment
- **Adds sky color variation**: Reflections tinted by current sky color (sunset = orange tint, cloudy = grey tint)
- **Physically plausible**: Real-world reflections are influenced by ambient lighting
- **No melting artifacts**: IBL is stable frame-to-frame, provides consistent tinting

## Trade-offs
- **Melting artifacts may return**: The env map is still low-res and can lag
- **Mitigation**: IBL tinting helps mask the artifacts by adding stable sky color
- **Future improvement**: Could increase env map resolution or use temporal smoothing

## Related Decisions
- [[Rubber Shader Merge]] — Merged rubber shader into VehiclePBR_Modern
- [[VehiclePBR Modern]] — Implementation details

## Code References
- `shaders/ps/VehiclePBR_Modern.hlsl:154-175` — Layer 2 env reflection code
- `src/render/vehiclePipe.cpp` — Env map texture binding (s1) and IBL texture binding (s3)
