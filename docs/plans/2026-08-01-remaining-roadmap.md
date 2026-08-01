# Remaining Roadmap — Implementation Plan

> Created: 2026-08-01
> Branch: `experimental` (HEAD = 3999f8c)
> Scope: Everything still TODO across all planning docs

---

## Status Summary

| Phase | Description | Status |
|-------|-------------|--------|
| Safety (WORKFLOW_PLAN 1-2) | Normalize guards, div-by-zero, strcat, sprintf | ❌ NOT STARTED |
| Architecture (WORKFLOW_PLAN 3) | Config refactor, code dedup, render state | ❌ NOT STARTED |
| D.4 | CJ character material depth | ❌ NOT STARTED |
| D.5 | Glass shader transparency | ❌ NOT STARTED |
| Phase 1 (Roadmap) | Restore full compatibility — verify all pipes, dead stubs, YCbCr | ❌ NOT STARTED |
| Phase 2 (Roadmap) | Platform/game style selection UI | ❌ NOT STARTED |
| Phase 3 (Roadmap) | GTA V/IV-style settings menu | ❌ NOT STARTED |
| Phase 4 (Roadmap) | Forward+ unified pipeline | ❌ NOT STARTED |
| Phase 5 (ROADMAP) | SM3.0 feature fit — skin/hair/veg, normal buffer AO | ❌ NOT STARTED |
| C (Deferred) | Normal map integration (DK22Pac) — LAST per user rule | 🔒 DEFERRED |

---

## Phase 0: Safety Hardening (DO FIRST)

Fix all 25 issues from WORKFLOW_PLAN.md. These are crash-risk and correctness bugs.

### 0.1 — Unguarded Normalize (3 sites)

**File:** `src/render/vehiclePipe.cpp`

| Line | Issue | Fix |
|------|-------|-----|
| 193 | `RwV3dNormalize(&lightDir)` — no zero-check | Guard: check return value, skip light if zero-length |
| 268 | Same pattern | Same fix |
| 345 | Same pattern | Same fix |

**Pattern:**
```cpp
// BEFORE (crashes on zero-length vector):
RwV3dNormalize(&lightDir);

// AFTER:
if(RwV3dNormalize(&lightDir) == 0.0f)
    continue;  // skip degenerate light
```

### 0.2 — Division Safety (3 sites)

**File:** `src/render/postfx.cpp`

| Line | Issue | Fix |
|------|-------|-----|
| 638 | `1.0f / (d * d + 1.0)` — d could be 0 | Add epsilon: `max(d*d + 1.0f, 1e-7f)` |
| 643 | Same pattern | Same fix |
| 1065 | `1.0f / (0.70f + sceneLuma * 2.0f)` — sceneLuma could be negative | Already clamped [0.80, 1.30], but guard denominator: `max(denom, 1e-7f)` |

### 0.3 — Uninitialized fxParams.fxSwitch (2 sites)

**File:** `src/render/vehiclePipe.cpp`

| Line | Issue | Fix |
|------|-------|-----|
| 458 | `fxParams.fxSwitch` used without init | Zero-init: `memset(&fxParams, 0, sizeof(fxParams))` before use |
| 649 | Same pattern | Same fix |

### 0.4 — Unsafe strcat (1 site)

**File:** `src/core/main.cpp:105`

```cpp
// BEFORE (buffer overflow risk):
strcat(path, filename);

// AFTER:
strncat(path, filename, sizeof(path) - strlen(path) - 1);
```

### 0.5 — Raw new without delete (3 sites)

**Files:**
- `src/rw/normalmap_plugin.cpp` — new without delete
- `src/extras/texdb.cpp:822,1146,103,167` — raw new, no RAII

**Fix:** Replace with `std::unique_ptr` or add matching delete in destructor/cleanup.

### 0.6 — Unused variable (1 site)

**File:** `src/render/vehiclePipe.cpp:249,323`

`float sub = ...` computed but never used. Remove or prefix with `(void)sub;`.

### 0.7 — sprintf without bounds (1 site)

**File:** `src/extras/PC_PlantsMgr.cpp:428-451`

Replace `sprintf` with `snprintf(buf, sizeof(buf), ...)`.

---

## Phase 1: Restore Full Compatibility

From Roadmap Phase 1. Verify all 9 car pipes + 2 building pipes work correctly.

### 1.1 — Verify All Vehicle Pipes

Each pipe must render without crash and produce correct output:

| Pipe | Enum | Entry | Status |
|------|------|-------|--------|
| PS2 | `CAR_PS2` | `CustomPipeRenderCB_PS2()` | Needs test |
| PC (exe) | `CAR_PC` | `CustomPipeRenderCB_exe()` | Needs test |
| Xbox | `CAR_XBOX` | `CustomPipeRenderCB_Xbox()` | Needs test |
| Specular | `CAR_SPEC` | `CustomPipeRenderCB_Specular()` | Needs test |
| Neo | `CAR_NEO` | `CarPipe::RenderCallback()` | Needs test |
| Leeds | `CAR_LCS` | `CustomPipeRenderCB_leeds()` | Needs test |
| VCS | `CAR_VCS` | `CustomPipeRenderCB_leeds()` | Needs test |
| Mobile | `CAR_MOBILE` | `CustomPipeRenderCB_mobile()` | Needs test |
| Env/Modern | `CAR_ENV/CAR_MODERN` | `vehiclePBR_RenderCallback()` | ✅ Working |
| GTA IV | `CAR_GTAIV` | `CarPipe::RenderCallback()` | Needs test |

**Test method:** Set `vehiclePipe` INI to each enum, launch game, verify vehicle rendering.

### 1.2 — Verify Building Pipes

| Pipe | Enum | Status |
|------|------|--------|
| PS2 | `BUILDING_PS2` | Needs test |
| Xbox/PC | `BUILDING_XBOX` | Needs test |
| GTA IV | `BUILDING_GTAIV` | Needs test |
| PBR | `BUILDING_PBR` | ✅ Working |

### 1.3 — Fix Dead Code Stubs

| Stub | Location | Issue | Fix |
|------|----------|-------|-----|
| `UploadUnifiedConstants` | `debugmenu_ui.cpp:307` | Returns immediately, full impl below never runs | Remove early return or delete dead code |
| Wheel extender swap | `wheels_extender.cpp:330-349` | Geometry swap disabled, Install empty | Implement or remove |
| Env map normals | `envmap.cpp:622-636` | Normal buffer cleared flat, re-render disabled (crash 0x7F98DF) | Fix crash or remove dead path |
| Mobile light mult | `vehiclePipe.cpp` | Unreachable mobile branch | Remove or fix |

### 1.4 — Restore YCbCr Filter

Verify `COLORFILTER_YCBCR` works in ColourFilter_switch. Check if shader loads and renders.

### 1.5 — Junior Feature Verification

Verify all features from skygfx_junior still work:
- SMAA (4 quality levels)
- SSAO
- Motion blur
- SSS post-process
- Cloud shadows
- Wind animation
- Stochastic texturing

---

## Phase 2: Visual Quality Remaining

### 2.1 — CJ Character Material Depth

**Problem:** CJ and pedestrians lack material variation — skin, clothing, accessories all look flat.

**Approach:**
1. Identify which shader entry handles ped rendering (likely `main()` or `main_mobileVehicle()`)
2. Add skin-tuned PBR parameters: lower roughness (0.4-0.6), subsurface tint, rim lighting
3. Use `RpSkin` pipeline data to detect skin vs clothing materials
4. Feed material type to shader via unused constant register channel

**Files:**
- `src/render/vehiclePipe.cpp` — ped render callback
- `shaders/ps/VehiclePBR_Modern.hlsl` — add skin branch
- `src/skygfx.h` — skin PBR config params

### 2.2 — Glass Shader Transparency

**Problem:** Vehicle windows too dark/opaque. Current env intensity too low.

**Approach:**
1. Review `Glass_Vehicle.hlsl` — current envIntensity=0.08, alpha=opacity*0.7+fresnel*0.20
2. Increase envIntensity to 0.15-0.20 for transparency
3. Add view-angle-dependent opacity (more transparent at normal incidence)
4. Consider separate glass PBR params in c22 channel

**Files:**
- `shaders/ps/Glass_Vehicle.hlsl` — transparency tuning
- `shaders/ps/VehiclePBR_Modern.hlsl` — glass entry point

### 2.3 — Sun Streak Controls (FIX)

**Problem:** `sunStreakIntensity` and `sunStreakSize` added but not working.

**Root cause:** Timecycle interpolation in `weather.cpp` may overwrite scaled values.

**Fix:**
1. Move scaling AFTER final timecycle interpolation (not between weather blend steps)
2. Or: hook `CCoronas::DoSunCorona` (0x6FB2A0) and scale spriteBrightness directly

---

## Phase 3: Architecture Cleanup

### 3.1 — Config Struct Refactor

**Problem:** `Config` struct has ~250 fields (skygfx.h:202-478).

**Approach:**
1. Group into sub-structs: `ConfigVideo`, `ConfigPipeline`, `ConfigPostFX`, `ConfigDebug`
2. Keep top-level `Config` as aggregate with sub-struct members
3. Update INI parsing to use dot-notation: `[SkyGfx.Video]`, `[SkyGfx.PostFX]`

### 3.2 — Vehicle Callback Deduplication

**Problem:** ~1500 lines duplicated across 7 vehicle callbacks in vehiclePipe.cpp.

**Approach:**
1. Extract shared setup (texture binding, light upload, matrix upload) into helper functions
2. Keep per-pipe differences as parameterized calls
3. Target: reduce vehiclePipe.cpp by 60-70%

### 3.3 — Render State Leaks

**Problem:** PostFX effects leave D3D9 render states dirty.

**Approach:**
1. Audit all `SetRenderState`/`SetTexture`/`SetSamplerState` in postfx.cpp
2. Add save/restore wrapper: `ScopedRenderState` RAII class
3. Verify SMAA, SSAO, motion blur all restore states

### 3.4 — Static Locals Non-Reentrant

**Problem:** `static` locals in render callbacks prevent re-entry (e.g., if game re-enters render during cutscene).

**Approach:**
1. Audit all `static` locals in render paths
2. Replace with per-frame state (passed via struct or thread-local)

---

## Phase 4: Platform Selection UI (Roadmap Phase 2)

### 4.1 — Style Selection INI

```ini
[SkyGfx]
vehicleStyle = modern    ; ps2, pc, xbox, mobile, neo, leeds, gtaiv, modern
buildingStyle = modern   ; ps2, xbox, gtaiv, modern
colorStyle = modern      ; ps2, pc, mobile, iii, vc, vcs, modern, gtaiv
```

### 4.2 — Debug Menu Integration

Add style dropdowns to ImGui debug menu. Hot-reload on change (recompile shaders, rebind pipes).

### 4.3 — In-Game Menu (ImGui Overlay)

Deferred — needs ImGui SDK integration. Low priority.

---

## Phase 5: Advanced Features (Roadmap Phases 3-6)

These are long-term goals. Not detailed here — will get separate plans when reached.

- **Phase 3:** GTA V/IV settings menu (quality presets, feature toggles)
- **Phase 4:** Forward+ unified pipeline (G-buffer, tiled lighting)
- **Phase 5:** SM3.0 feature fit (skin/hair/vegetation passes, normal buffer AO)
- **Phase 6:** Multi-agent development plan

---

## Execution Order

```
Phase 0 (Safety)          ← DO FIRST, no visual change, prevents crashes
  ↓
Phase 1 (Compatibility)   ← Verify all pipes work, fix dead stubs
  ↓
Phase 2 (Visual)          ← CJ depth, glass transparency, sun streaks
  ↓
Phase 3 (Architecture)    ← Config refactor, dedup, render state
  ↓
Phase 4 (Platform UI)     ← Style selection, INI + debug menu
  ↓
Phase 5 (Advanced)        ← Forward+, skin/hair/veg, multi-agent
```

---

## Safety Issues Quick Reference

| ID | Phase | File:Line | Issue | Severity |
|----|-------|-----------|-------|----------|
| SAF-1 | 0.1 | vehiclePipe.cpp:193,268,345 | Unguarded RwV3dNormalize | CRASH |
| SAF-2 | 0.2 | postfx.cpp:638,643,1065 | Div-by-zero | BLACK |
| SAF-3 | 0.3 | vehiclePipe.cpp:458,649 | Uninitialized fxParams.fxSwitch | CORRUPT |
| SAF-4 | 0.4 | main.cpp:105 | Unsafe strcat | OVERFLOW |
| MEM-1 | 0.5 | normalmap_plugin.cpp, texdb.cpp | Raw new without delete | LEAK |
| MEM-2 | 0.6 | vehiclePipe.cpp:249,323 | Unused variable | WARNING |
| MEM-3 | 0.7 | PC_PlantsMgr.cpp:428-451 | sprintf without bounds | OVERFLOW |
| ARCH-1 | 3.1 | skygfx.h:202-478 | Config struct 250 fields | MAINTAIN |
| ARCH-2 | 3.2 | vehiclePipe.cpp | 1500 lines duplication | MAINTAIN |
| ARCH-3 | 3.3 | postfx.cpp | Render state leaks | VISUAL |
| ARCH-4 | 3.4 | render callbacks | Static locals non-reentrant | CORRUPT |
