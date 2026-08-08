# SMAA Crash Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix SMAA anti-aliasing crash at `0x007FBD4A` (NULL vtable in `RwCameraEndUpdate`) and restore working 3-pass SMAA rendering.

**Architecture:** Rewrite `DrawSMAA()` to use D3D9 `SetRenderTarget()` instead of RW camera raster swapping (`RwCameraEndUpdate`/`SetRaster`/`BeginUpdate`). This bypasses the RW camera context stack entirely, eliminating the double-EndUpdate crash. Move SMAA call site from `ColourFilter_switch` to end of `DrawFinalEffects` (matching working commit `018a455`). Remove broken inline copy + `SetRenderTarget(NULL)` code from `ColourFilter_switch`.

**Tech Stack:** C++, RenderWare 3.6 D3D9, HLSL SM3.0, Win32

## Global Constraints

- **SM3.0 only**: All shaders compile as `ps_3_0` / `vs_3_0`. No SM4/5 features.
- **RW 3.6 SDK**: Use `WRAPPER`/`EAXJMP` for game functions. Do NOT replace with RW 3.7 API.
- **Division safety**: Guard all `normalize()` calls against zero-length vectors. Guard `1.0/x` with `max(x, 1e-7f)`.
- **Null guards**: Check all `RwFrame`/`RwCamera`/`RwRaster` pointers before use.
- **Render state**: Save/restore all D3D9 render states modified in callbacks.
- **Debug logging**: Use `dbglog_throttle("tag")` (2s interval) for per-frame logs. Use `dbglog()` for init/error logs.
- **No comments unless critical**: Only comment non-obvious RW/D3D9 interop.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/render/postfx.cpp` | Modify | Main PostFX chain — SMAA call site + DrawSMAA function |
| `src/render/postfx.h` | Verify | Extern declarations for SMAA shaders/textures |
| `src/skygfx.h` | Verify | Config fields (`smaaEnable`, `smaaPreset`, `smaaPredication`, `smaaTemporal`) |
| `src/pipelinecommon.cpp` | Verify | SMAA shader pointers (`SMAA_Edge`, `SMAA_BlendWeight`, `SMAA_BlendNeighbor`, `SMAA_EdgeMotionDepth`) |

### Key Functions in `postfx.cpp`

| Function | Lines | Role |
|----------|-------|------|
| `DrawFinalEffects` | 1854-1933 | Hook at `0x53EBE9`. End of postFX chain. SMAA call goes HERE. |
| `ColourFilter_switch` | ~1700-1840 | Hook at `0x704D1E`. Remove SMAA + inline copy from HERE. |
| `DrawSMAA` | ~3030-3383 | 3-pass SMAA. Rewrite to use D3D9 `SetRenderTarget`. |
| `UpdateFrontBuffer` | 241-262 | Copies camera raster → pRasterFrontBuffer. Keep as-is. |

---

## Task 1: Add D3D9 surface helper for RwRaster

**Files:**
- Modify: `src/render/postfx.cpp` (add helper function near line 1933)
- Test: Build + verify no crash with SMAA disabled

**Interfaces:**
- Consumes: `RwRaster` pointer
- Produces: `GetD3D9RasterSurface()` function — returns `IDirect3DSurface9*` from RW raster plugin extension

- [ ] **Step 1: Add the helper function**

Add after line 1933 (after the SMAA-moved comment):

```cpp
// Get D3D9 surface from RwRaster — uses RW 3.6 plugin extension
static IDirect3DSurface9* GetD3D9RasterSurface(RwRaster *ras)
{
    if(!ras) return NULL;
    // In RW 3.6 D3D9, the surface pointer is stored in the raster's plugin extension.
    // Offset 0x20 from RwRaster base is the D3D9 surface pointer for camera-texture rasters.
    // This matches the internal layout used by RwCameraGetRaster in D3D9 builds.
    return *(IDirect3DSurface9**)((DWORD_PTR)ras + 0x20);
}
```

- [ ] **Step 2: Build and verify no crash**

Run: `python tools/fast_build.py`
Expected: Build succeeds, ASI deployed, no crash when SMAA disabled

- [ ] **Step 3: Commit**

```bash
git add src/render/postfx.cpp
git commit -m "feat(smaa): add D3D9 surface helper for raster RT access"
```

---

## Task 2: Rewrite DrawSMAA to use D3D9 SetRenderTarget

**Files:**
- Modify: `src/render/postfx.cpp` (lines ~3070-3166, the raster setup + pass loop)
- Test: Build + enable SMAA + check log for `[SMAA-DIAG]` entries

**Interfaces:**
- Consumes: `GetD3D9RasterSurface()`, `overrideIm2dPixelShader`, `SMAA_EdgeMotionDepth`, `SMAA_BlendWeight`, `SMAA_BlendNeighbor`
- Produces: Rewritten `DrawSMAA()` that uses `dev->SetRenderTarget()` instead of `RwCameraEndUpdate`/`SetRaster`/`BeginUpdate`

- [ ] **Step 1: Replace camera raster swap with D3D9 SetRenderTarget in Pass 0**

Find the Pass 0 section in DrawSMAA (around line 3226):
```cpp
// ---- Pass 0: Edge + Motion + Depth Detection ----
RwCameraEndUpdate(Scene.camera);
RwCameraSetRaster(Scene.camera, g_smaaEdgeRaster);
RwCameraBeginUpdate(Scene.camera);
```

Replace with:
```cpp
// ---- Pass 0: Edge + Motion + Depth Detection ----
IDirect3DDevice9 *smaaDev = d3d9device;
IDirect3DSurface9 *pass0Surf = GetD3D9RasterSurface(g_smaaEdgeRaster);
IDirect3DSurface9 *oldRt0 = NULL;
if(smaaDev && pass0Surf){
    smaaDev->GetRenderTarget(0, &oldRt0);
    smaaDev->SetRenderTarget(0, pass0Surf);
}
```

- [ ] **Step 2: Replace camera raster swap in Pass 1**

Find Pass 1 (around line 3274):
```cpp
RwCameraEndUpdate(Scene.camera);
RwCameraSetRaster(Scene.camera, g_smaaBlendRaster);
RwCameraBeginUpdate(Scene.camera);
```

Replace with:
```cpp
// ---- Pass 1: Blend Weight Calculation ----
IDirect3DSurface9 *pass1Surf = GetD3D9RasterSurface(g_smaaBlendRaster);
if(smaaDev && pass1Surf){
    smaaDev->SetRenderTarget(0, pass1Surf);
}
```

- [ ] **Step 3: Replace camera raster swap in Pass 2**

Find Pass 2 (around line 3322):
```cpp
RwCameraEndUpdate(Scene.camera);
RwCameraSetRaster(Scene.camera, drawBuffer);
RwCameraBeginUpdate(Scene.camera);
```

Replace with:
```cpp
// ---- Pass 2: Neighborhood Blending ----
IDirect3DSurface9 *pass2Surf = GetD3D9RasterSurface(drawBuffer);
if(smaaDev && pass2Surf){
    smaaDev->SetRenderTarget(0, pass2Surf);
}
```

- [ ] **Step 4: Replace final EndUpdate + prevFrame copy**

Find the final section (around line 3358-3375):
```cpp
RwCameraEndUpdate(Scene.camera);
RwRasterPushContext(g_smaaPrevFrameRaster);
RwRasterRenderFast(RwCameraGetRaster(Scene.camera), 0, 0);
RwRasterPopContext();
RwCameraBeginUpdate(Scene.camera);
```

Replace with:
```cpp
// Save current frame for next frame's motion detection
IDirect3DSurface9 *prevSurf = GetD3D9RasterSurface(g_smaaPrevFrameRaster);
if(smaaDev && prevSurf){
    smaaDev->StretchRect(pass2Surf, NULL, prevSurf, NULL, D3DTEXF_NONE);
}
// Restore original RT
if(smaaDev && oldRt0){
    smaaDev->SetRenderTarget(0, oldRt0);
    oldRt0->Release();
}
```

- [ ] **Step 5: Build and test**

Run: `python tools/fast_build.py`
Expected: Build succeeds, ASI deployed

- [ ] **Step 6: Enable SMAA and check log**

Launch game, open debug menu (Ctrl+4), check SMAA checkbox.
Check `skygfx_dbg.log` for `[SMAA-DIAG]` entries.
Expected: No crash, SMAA logs appear every 2 seconds

- [ ] **Step 7: Commit**

```bash
git add src/render/postfx.cpp
git commit -m "fix(smaa): use D3D9 SetRenderTarget to bypass camera state crash"
```

---

## Task 3: Move SMAA call from ColourFilter_switch to DrawFinalEffects

**Files:**
- Modify: `src/render/postfx.cpp` (remove SMAA block from ColourFilter_switch, add to DrawFinalEffects)
- Test: Build + enable SMAA + verify no crash + check visual output

**Interfaces:**
- Consumes: `DrawSMAA()`, `SMAA_Edge`, `ImmediateModeRenderStatesStore/Set/ReStore`
- Produces: SMAA called from DrawFinalEffects instead of ColourFilter_switch

- [ ] **Step 1: Remove SMAA block from ColourFilter_switch**

In `ColourFilter_switch`, find and remove the SMAA block (around lines 1818-1823):
```cpp
if(config->smaaEnable && SMAA_Edge){
    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    DrawSMAA();
    ImmediateModeRenderStatesReStore();
}
```

Also remove the inline copy + RT=NULL block above it (around lines 1775-1815) — this was the "CRITICAL FIX" that caused the black screen.

The ColourFilter_switch should end with just:
```cpp
ColourFilter_Modern(rgb1, rgb2);
if(dbglog_throttle("cf_done"))
    dbglog("[ColourFilter] done");
```

- [ ] **Step 2: Add SMAA to end of DrawFinalEffects**

In `DrawFinalEffects`, before the closing brace (line
