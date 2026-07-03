# Unified Pipeline

#pipeline #unified #experimental

## Overview
An alternative 4-pass post-processing chain that combines SSAO, SMAA, IBL, and grading into a single pipeline. Separate from the standard postfx pipeline.

**Shader**: `shaders/unifiedPipe.hlsl`
**Config**: `unifiedEnable=1`

## Config Fields
```ini
unifiedEnable=0
unifiedVersion=1
unifiedSatBoost, unifiedIblTintStrength
unifiedSsaoNoiseScale
unifiedShadowSoftness
unifiedCloudShadowStr, unifiedSunShadowStr
unifiedVertexAOBoost, unifiedDayReduction, unifiedPointLightOverride
unifiedSmaaThreshold, unifiedSmaaCornerRounding, unifiedSmaaMaxSearchSteps
unifiedShowMenu, unifiedShowOverlay, unifiedDebugOcclusion
unifiedEnablePrePass, unifiedEnableEdgeDetect, unifiedEnableOcclusion
unifiedEnableStoredShadows, unifiedEnableCloudShadows, unifiedEnableSunShadows
unifiedEnableTimeOfDay, unifiedEnableVertexAO, unifiedEnablePointLightOverride
unifiedEnablePostPass, unifiedEnableIBL, unifiedEnableIBLTint
unifiedEnableSurfaceWeights, unifiedEnableGrading, unifiedEnableGamma
```

## Passes
1. **Pre-pass** — Depth/normal preparation
2. **Edge detect** — SMAA edge detection
3. **Occlusion** — SSAO computation
4. **Post-pass** — IBL, grading, gamma, tonemapping

## Status
Experimental — not enabled by default. Requires `unifiedEnable=1`.

## See Also
- [[PostFX Pipeline]] — Standard postfx pipeline
- [[Build System]] — How unifiedPipe.hlsl compiles
