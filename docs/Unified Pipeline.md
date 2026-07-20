# Unified Pipeline

#pipeline #unified #experimental

## Overview

An alternative forward+ rendering pipeline that combines SSAO, SMAA, IBL, and grading into a single configurable system. Separate from the standard postfx pipeline.

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

1. **Pre-pass** — Depth/normal preparation for downstream effects
2. **Edge detect** — SMAA edge detection (shared with standard pipeline)
3. **Occlusion** — SSAO computation (shared with standard pipeline)
4. **Post-pass** — IBL (image-based lighting), color grading, gamma correction, tonemapping

Each pass can be independently enabled/disabled via the `unifiedEnable*` config fields.

## Debug Features

- `unifiedShowMenu` — In-game debug menu for real-time parameter adjustment
- `unifiedShowOverlay` — Visual overlay showing pipeline state
- `unifiedDebugOcclusion` — Debug visualization of SSAO occlusion

## Status

Experimental — not enabled by default. Requires `unifiedEnable=1`.

## See Also

- [[PostFX Pipeline]] — Standard postfx pipeline
- [[Color Grading]] — Grading system details
