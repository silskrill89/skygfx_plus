# Vehicle Reflection Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix invisible vehicle reflections by deriving `fxParams.shininess` from paint glossiness instead of the MatFX env-map flag.

**Architecture:** The PBR vehicle shader uses `fxParams.y` (shininess) as `carcolsShine` to drive reflection intensity via `roughnessResponse`/`envMask`. Currently `fxParams.shininess` is only set non-zero when the material has a MatFX env-map flag (`rpMATFXEFFECTENVMAP`), which is FALSE for most GTA SA vehicle paint. The fix derives shininess from the paint system's glossiness (already computed via `VehShaders_GetPaintPBR`), ensuring reflections appear on all paint types.

**Tech Stack:** C++ (MSVC), HLSL SM3.0 (ps_3_0/vs_3_0), D3D9, RW 3.6 SDK

## Global Constraints

- RW 3.6 SDK baked into game exe — WRAPPER/EAXJMP calls real RW functions at hardcoded addresses
- SM3.0 only — all shaders compile as ps_3_0 / vs_3_0
- `c22/c23` upload MUST use `pipeUploadPBR()` — NEVER upload manually
- Division safety: guard all `normalize()` and `1/x` with epsilon checks
- Render state must be saved/restored for all D3D9 state changes
- fxParams struct layout: `{fxSwitch, shininess, specularity, lightmult}` — .y=shininess
- PS reads fxParams from c1, VS reads from c21

---

### Task 1: Fix fxParams.shininess derivation in vehiclePipe.cpp

**Files:**
- Modify: `src/render/vehiclePipe.cpp:1767-1773`

**Interfaces:**
- Consumes: `glossiness` from `VehShaders_GetPaintPBR()` (line 1767)
- Consumes: `fxParams.shininess` from MatFX block (lines 1712-1736)
- Produces: Correct `fxParams.shininess` for PS `carcolsShine` (c1.y)

**Analysis of the bug:**
- Line 1712: `fxParams.shininess = 0.0f;`
- Lines 1719-1722: If NOT `rpMATFXEFFECTENVMAP`, `hasEnv1/hasEnv2/hasSpec` all set false
- Lines 1725-1729: Only if `hasEnv1||hasEnv2`, shininess set from `envData->GetShininess() * 8 * envShininessMult`
- Result: Most vehicle paint materials get `fxParams.shininess = 0.0f`
- PS line 166: `carcolsShine = saturate(fxParams.y) = 0` → reflections dimmed

**Fix:** After `VehShaders_GetPaintPBR` (line 1767), if `fxParams.shininess` is still near zero (no MatFX env-map), derive it from the paint glossiness. Then re-upload fxParams to both VS (c21) and PS (c1).

- [ ] **Step 1: Read the exact code block to modify**

Read `src/render/vehiclePipe.cpp` lines 1765-1775 to confirm exact insertion point.

- [ ] **Step 2: Add glossiness-derived shininess fallback**

After line 1767 (`VehShaders_GetPaintPBR` call), insert:

```cpp
// Derive shininess from paint glossiness when no MatFX env-map provides it.
// PS uses fxParams.y as carcolsShine to drive reflection intensity.
// Without this, most vehicle paint gets shininess=0 → invisible reflections.
if(fxParams.shininess < 0.01f){
    fxParams.shininess = glossiness;
}
```

- [ ] **Step 3: Re-upload fxParams after the glossiness boost block**

After the existing block at lines 1770-1773 (which boosts glossiness/specular when shininess > 0.2), add re-upload:

```cpp
// Re-upload fxParams with corrected shininess (was uploaded at line 1756-1758 with stale value)
RwD3D9SetVertexShaderConstant(21, &fxParams, 1);
RwD3D9SetPixelShaderConstant(1, &fxParams, 1);
```

- [ ] **Step 4: Build**

Run: `python tools/fast_build.py`
Expected: Build succeeds, ASI deployed to `E:\games\gtasa_skygfx_plus\skygfx.asi`

- [ ] **Step 5: Launch game and verify**

Run: PowerShell `Start-Process "E:\games\gtasa_skygfx_plus\gta_sa.exe"` from game directory
Check: `skygfx_dbg.log` shows VehiclePBR logs with non-zero reflection values

- [ ] **Step 6: Commit**

```bash
git add src/render/vehiclePipe.cpp
git commit -m "fix(vehicle): derive fxParams.shininess from paint glossiness for PBR reflections

Most GTA SA vehicle paint materials lack MatFX env-map flag, leaving
fxParams.shininess at 0.0f. PS carcolsShine=0 collapses roughnessResponse
and envMask, making reflections invisible.

Fix: when no MatFX env-map provides shininess, derive it from the paint
system's glossiness (VehShaders_GetPaintPBR). Re-upload fxParams to VS/PS
after the glossiness boost block so the corrected value reaches the shader."
```

---

### Task 2: Verify reflection values in log

**Files:**
- Read: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`

**Interfaces:**
- Consumes: skygfx_dbg.log from game runtime
- Produces: Confirmation that fxParams.y is non-zero for vehicle paint

- [ ] **Step 1: Check log for VehiclePBR entries**

Search log for `[VehiclePBR]` lines. Verify:
- `fxParams.y` (shininess) is non-zero for paint materials (expected: 0.30-0.95 range)
- No shader null warnings
- PBRBuilding/PBRVehicle frame logs present

- [ ] **Step 2: Visual verification in-game**

Spawn a vehicle (use trainer or drive to parking lot). Verify:
- Paint reflections visible at grazing angles
- Glossy paint (sports cars) shows stronger reflections than matte
- No black/missing reflection patches on paint surfaces

- [ ] **Step 3: Commit verification result**

If fix works: no additional commit needed (Task 1 commit covers it)
If fix needs tuning: adjust the shininess derivation formula and commit

---

### Task 3: (Conditional) Tune reflection intensity if needed

**Files:**
- Modify: `src/render/vehiclePipe.cpp` (shininess derivation)
- Modify: `shaders/ps/VehiclePBR_Modern.hlsl` (envMask/ENV_BOOST if needed)

**Interfaces:**
- Consumes: User visual feedback from Task 2
- Produces: Tuned reflection appearance

- [ ] **Step 1: If reflections too strong, scale shininess down**

```cpp
if(fxParams.shininess < 0.01f){
    fxParams.shininess = glossiness * 0.8f;  // scale down if too bright
}
```

- [ ] **Step 2: If reflections too weak, increase ENV_BOOST**

In `VehiclePBR_Modern.hlsl` line 176: `const float ENV_BOOST = 2.0;` → increase to 3.0

- [ ] **Step 3: Build, deploy, verify**

Same as Task 1 Steps 4-5

- [ ] **Step 4: Commit tuning**

```bash
git add src/render/vehiclePipe.cpp shaders/ps/VehiclePBR_Modern.hlsl
git commit -m "tune(vehicle): adjust PBR reflection intensity"
```
