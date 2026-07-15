---
tags: [technical-decision, rubber-shader, merge, tires]
created: 2025-01-02
updated: 2026-07-15
---

# Rubber Shader Merge

## Decision
**Merged standalone `Rubber_Vehicle.hlsl` into `VehiclePBR_Modern.hlsl` as `main_rubber` entry point.**

## Context
Originally, the rubber/tire shader was a separate file (`shaders/ps/Rubber_Vehicle.hlsl`) with its own CSO and resource ID. This created maintenance overhead and didn't align with the unified shader approach used for vehicles.

## Problem with Separate Shader
- **Duplication**: Same vertex shader, similar constants, overlapping logic
- **Maintenance**: Two files to update when PBR functions change
- **Resource overhead**: Separate CSO, separate resource ID
- **Inconsistent**: Other vehicle shaders (glass, paint) are entry points in VehiclePBR_Modern

## Solution
Merge rubber shader as `main_rubber` entry point in `VehiclePBR_Modern.hlsl`:

### Changes Made
1. **Added entry point** to `VehiclePBR_Modern.hlsl`:
   ```hlsl
   float4 main_rubber(PS_INPUT IN) : COLOR
   {
       // Parametric rubber with dirt/wear tinting
       float roughness = pbrParams.x;
       float rubberF0 = pbrParams.y;
       float tintR = pbrParams.z;
       float tintG = pbrParams.w;
       float tintB = paintNoise.x;
       float dirtLevel = paintNoise.y;
       float wearFactor = paintNoise.z;
       
       // Dirt/wear tinting
       float3 dirtTint = float3(0.35, 0.25, 0.15);
       float3 wearTint = float3(0.55, 0.55, 0.50);
       baseColor = lerp(baseColor, dirtTint * baseColor, dirtLevel * 0.4);
       baseColor = lerp(baseColor, wearTint * baseColor, wearFactor * 0.3);
       
       // Subsurface wrap, Fresnel sheen, etc.
   }
   ```

2. **Updated `fix_build.py`** to compile `main_rubber`:
   ```python
   ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso', False),
   ```

3. **Added resource ID** in `resource.h`:
   ```cpp
   #define IDR_RUBBER_VEHICLE_MODERN   240
   ```

4. **Added resource entry** in `Resource.rc`:
   ```rc
   IDR_RUBBER_VEHICLE_MODERN RCDATA "resources/cso/Rubber_Vehicle_Modern.cso"
   ```

5. **Added shader pointer** in `pipelinecommon.cpp`:
   ```cpp
   void *Rubber_Vehicle_Modern = nullptr;
   makePS(IDR_RUBBER_VEHICLE_MODERN, &Rubber_Vehicle_Modern);
   ```

6. **Updated tire render path** in `vehiclePipe.cpp`:
   ```cpp
   // Upload tire params to c22/c23
   float tireParams[4] = { tireRough, tireRefl, tireTR, tireTG };
   float tireParams2[4] = { tireTB, dirtLevel, dirtLevel * 0.5f, 0.0f };
   RwD3D9SetPixelShaderConstant(22, tireParams, 1);
   RwD3D9SetPixelShaderConstant(23, tireParams2, 1);
   
   RwD3D9SetPixelShader(Rubber_Vehicle_Modern);
   ```

## Benefits
- **Unified shader**: All vehicle rendering in one file
- **Easier maintenance**: One file to update for PBR changes
- **Consistent approach**: Matches glass, paint, and other vehicle shaders
- **Parametric**: Dirt/wear levels passed via constants, not hardcoded

## Trade-offs
- **Larger file**: VehiclePBR_Modern.hlsl is now ~500 lines
- **Compilation time**: Slightly longer (but negligible)
- **Complexity**: More entry points to understand

## Future Work
- **Delete standalone shader**: Remove `Rubber_Vehicle.hlsl` once merge is stable
- **Extend features**: Add more tire properties (wet/dry, tread pattern, etc.)
- **Universal dirt system**: Share dirt/wear logic across all shaders

## Related Decisions
- [[IBL Env Map Decision]] — IBL tints env map
- [[VehiclePBR Modern]] — Implementation details

## Code References
- `shaders/ps/VehiclePBR_Modern.hlsl:460-506` — main_rubber entry point
- `src/render/vehiclePipe.cpp` — Tire render path
- `src/render/pipelinecommon.cpp` — Shader loading
- `tools/fix_build.py:320-322` — Shader compilation
