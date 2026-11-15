# 2026-11-10 main.cpp vs AAP Divergence Audit

## Method
- AAP baseline: `/E/SDKs/skygfx_original/src/main.cpp` (1,733 lines, aap@926616a)
- Live: `src/core/main.cpp` (4,476 lines, as of W0.6 build 2026-11-10)
- Compared render-state/device-state calls, globals, and known wave-fix regions.

## Key Divergences (render-state relevant)

### 1. Far/fog re-update — main.cpp:1664-1682 (LIVE) vs AAP:738-740
**LIVE**: Guarded `RwCameraEndUpdate`+`RwCameraBeginUpdate` behind `pCameraBeingUpdated == (void*)Scene.camera`.
**AAP**: Unguarded (no begun-camera check).
**Classification**: Wave fix (aap parity #5). Genuine bug fix — prevents 2026-09-14 crash when called with camera not begun. KEEP.

### 2. CClouds split restore — main.cpp:2936-2999
**LIVE**: Full SEH-protected `CClouds__Render_orig` + 12-state restore via `pipeDeviceStateSet` (rw cache + driver cache + raw device).
**AAP**: No CClouds hook at all (only commented Nops at :1723-1725).
**Classification**: Wave fix (lane 10). Patch-on-patch shim — restores state that CClouds clobbered. Candidate for strip-back IF the underlying CClouds texture/state issue is fixed properly elsewhere.

### 3. Render-state dual-layer handling — main.cpp:219-268
**LIVE**: New `pipeAlphaFuncToD3D` helper + dual-layer (rw cache + raw D3D9) alpha/zwrite handling in `D3D9RenderDual` and new `D3D9RenderDual_WithDepthBias`.
**AAP**: Inline `RwRenderStateSet` only (no dual-layer).
**Classification**: Wave fix (depth-bias + cache-coherent fix). KEEP — this is the universal fix for the raw-SetTexture desync class.

### 4. SEPARATEALPHABLENDENABLE block — main.cpp:1352-1360
**LIVE**: New block that sets/restores SEPARATEALPHABLENDENABLE + BLENDOPALPHA + SRCBLENDALPHA + DESTBLENDALPHA.
**AAP**: At :696-700 (same block, but without rw cache layer).
**Classification**: Wave fix (alpha-blend restore). KEEP.

### 5. Alpha test always/restore — main.cpp:1474-1481
**LIVE**: New block that forces ALPHATESTFUNCTIONALWAYS then restores.
**AAP**: At :281-292 (similar but simpler).
**Classification**: Wave fix (alpha-test restore). KEEP.

### 6. New globals/externs (not in AAP)
- `perfFreqInv` (:45)
- `explicitBuildingPipe_tmp` (:56)
- `myDefaultCallback` (:58)
- `vs_probe` (:60)
- `gHasExternalNormalMapPlugin` (:66)
- `g_hasMoonLoader` (:67)
- `forceWindShader` (:72)
- `s_activeModeIniSeen` (:88)
- `gpCurrentPixelShaderForDefaultCallbacks` (:127)
- `gpCurrentVertexShaderForDefaultCallbacks` (:128)
- `TempBufferRenderIndexList` (:139)
- `TempVertexBuffer` (:140)
- `windPos` (:142)
- `SkyGfx_FollowsMovedArrays` (:153)
- `pipeAlphaFuncToD3D` (:228)
- `s_cfEnableUser` (:558)
- `s_cfEnableSeen` (:559)
- `g_noiseQuality` (:790)
- `AmbientLightColourForFrame_PedsCarsAndObjects` (:981)
- `DirectionalLightColourForFrame` (:982)
- `tmpintensity` (:1024)

## Likely Candidates for "Still Looks Fucked" Visual Issues

### P0: CClouds split restore (main.cpp:2936-2999)
- The 12-state restore is complex and may be introducing its own issues.
- The SEH `__try/__except` may be masking real CClouds faults.
- The `pipeDeviceStateSet` calls may be interacting badly with other render states.

### P1: Render-state dual-layer handling (main.cpp:219-268)
- The new `D3D9RenderDual_WithDepthBias` may be over-applying depth bias.
- The `pipeAlphaFuncToD3D` mapping may be wrong for some cases.

### P2: SEPARATEALPHABLENDENABLE block (main.cpp:1352-1360)
- May be leaving SEPARATEALPHABLENDENABLE in a bad state if the restore is incomplete.

### P3: New globals/externs
- `g_hasMoonLoader` — may be affecting hook installation order.
- `forceWindShader` — may be forcing wind shaders where they shouldn't be.
- `g_noiseQuality` — may be affecting noise generation.

## No Source Edits, No Builds, No Commits
This is a read-only analysis.