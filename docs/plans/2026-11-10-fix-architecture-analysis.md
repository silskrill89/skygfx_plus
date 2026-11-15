# 2026-11-10 Fix Architecture Analysis: Our Fixes vs AAP Implementation

## Executive Summary
**Critical Finding**: Our fix architecture is massively over-engineered compared to AAP's simple direct-state approach. The ~15 `effectsForceAlphaBlock` calls + CClouds 3-layer restore + `pipeDeviceStateSet` chokepoint may be **creating the state desyncs they're supposed to fix**.

AAP has **NONE** of our alpha state management. AAP's postfx.cpp directly sets blend states without save/restore. Our fix architecture introduces multiple layers of state management that can conflict with each other.

---

## Architecture Comparison

### Our Fix Architecture (main.cpp + buildingPipe.cpp + postfx.cpp)

| Component | Location | Purpose | AAP Equivalent |
|---|---|---|---|
| `effectsForceAlphaBlock(tag)` | main.cpp:1858 | Force canonical alpha block at ~15 call sites | **NONE** |
| `CClouds__Render_hygiene()` | main.cpp:2651 | Massive state save/restore with SEH, 3-layer capture, split-entry detection, producer gates | **NONE** |
| `frameBoundaryCloseAndBaseline()` | main.cpp:1585 | CoronaWindow_Close + baseline push at SceneStart/EffectsExit/FinalExit | **NONE** |
| `coronaFeedForwardPlus()` | main.cpp:2093 | Corona/point-light → Forward+ feed with counter repair | **NONE** |
| `pipeForceAlphaBlock()` | buildingPipe.cpp:562 | Canonical alpha block push via pipeDeviceStateSet (L1/L2/L3) | **NONE** |
| `pipeDeviceStateSet()` | buildingPipe.cpp | Derives L2/L3 from same rw value (our invention) | **NONE** |
| `pipeEnterAlphaMode/pipeExitAlphaMode` | pipelinecommon.cpp | Lag-aware state capture/restore | **NONE** |

### AAP Approach (postfx.cpp, main.cpp)
- **Direct state setting**: `RwRenderStateSet(rwRENDERSTATESRCBLEND, ...)` then draw
- **No save/restore**: States set by one pass are left for the next pass to set again
- **No producer gates**: No `g_inCoronasRender`, `g_inCloudsRender`, `s_spriteForceValid` flags
- **No layer-drift detection**: No 3-layer (rw cache / driver cache / raw device) comparison
- **No SEH guards**: No `__try/__except` around render hooks
- **No corona feed to Forward+**: Coronalights stay in the game's own pipeline

---

## Root Cause Hypothesis: State Thrashing

### The Vicious Cycle
1. `effectsForceAlphaBlock` is called at ~15 sites (RenderEffects.enter/exit, PostFx.exit, CClouds, CCoronas.Reflections, CCoronas.SunReflection, SceneStart, EffectsExit, FinalExit, etc.)
2. Each call does:
   - 3-layer evidence sampling (rw cache, driver cache, raw device)
   - Drift detection
   - Additive-leak substitution (2/2 → 5/6)
   - `pipeForceAlphaBlock()` which does:
     - rw cache read (5 states)
     - rw→d3d validation/conversion
     - rw cache write (if changed)
     - RwD3D9SetRenderState for 7 states (L1→L2)
     - Raw device write for 7 states (L2→L3)
3. `CClouds__Render_hygiene` additionally does:
   - 12+ state saves across 3 layers
   - SEH __try/__except
   - Split-entry detection (g_inCloudsRender, g_inCoronasRender, s_spriteForceValid)
   - Producer gates that skip the entire function
   - Stage-0 texture raster save/restore
   - Z-test/Z-func save/restore

### The Problem
**AAP's simpler approach works because each pass sets its own state directly.** Our approach tries to "fix" state drift by constantly comparing and repairing 3 layers, but this very comparison/repair cycle **creates** the desyncs:

- The 3-layer comparison can **miss** states that are only in the raw device but not in either cache
- The producer gates can **skip** repairs when they're most needed
- The SEH guards can **swallow** exceptions that leave state in an unknown condition
- The multiple call sites can **conflict** with each other's assumptions about what state should be

---

## Fix Correlation Analysis

### Visual Issues Still Present After W0.6

| Issue | AAP Behavior | Our Behavior | Likely Cause |
|---|---|---|---|
| Rainbow RGB channel-shred | AAP: direct blend state per pass | Our: 3-layer sync + producer gates | Layer drift detection missing states; producer gate skips repair |
| Black streak-bars / black quads | AAP: direct blend state per pass | Our: additive-leak substitution + pipeForceAlphaBlock | 2/2→5/6 substitution may be too aggressive; producer gate skips repair |
| Grey box head / marker cone | AAP: direct state | Our: softbody TOCTOU gates + gib lifetime gates | Gates may be too narrow; race conditions between gates |
| Black floor blotches / grey slabs | AAP: direct state | Our: sentinel consumer honors (N9) | Sentinel remap may be incomplete; policy bypasses |
| Olive/milky scenes | AAP: direct state | Our: [FxAlpha] split restore + cloud_split_restore | Split restore may be incomplete; 12-state restore missing states |
| Exposure pin | AAP: direct state | Our: RW cache desync fix (NULL→flush→sceneRaster) | Fix may be incomplete; pending NULL commit may fail |

### "Broke Almost At Once" Analysis
User reports issues "all broke almost at once" and "have been broken for a while since." This suggests a **single architectural change** broke everything. The most likely candidate:

**The introduction of `pipeDeviceStateSet` as a chokepoint** — all render state writes now route through this function, which derives L2/L3 from L1. If this derivation is wrong for even one state, it creates a cascade of desyncs across all passes.

---

## Refactoring Proposal: Simplify to AAP Approach

### Phase 1: Remove Over-Engineering
1. **Delete `effectsForceAlphaBlock` entirely** — replace with direct `RwRenderStateSet` calls at each site
2. **Simplify `CClouds__Render_hygiene`** — remove SEH, 3-layer capture, producer gates, split-entry detection
3. **Remove `pipeDeviceStateSet` chokepoint** — let each pass set its own state directly
4. **Remove `pipeForceAlphaBlock`** — replace with direct `RwD3D9SetRenderState` + raw device writes

### Phase 2: AAP-Style Direct State Management
For each render pass that needs specific blend states:
```cpp
// AAP-style direct state setting (no save/restore)
RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
// ... draw ...
// No restore needed — next pass sets its own state
```

### Phase 3: Minimal Safety Net
Only add back the **essential** repairs that AAP lacks:
1. **Additive-leak cleanup**: If ONE/ONE is detected, substitute with 5/6 (but only at specific known leak sites, not globally)
2. **Producer window protection**: Only skip alpha block during corona/cloud body (not at every call site)

---

## Implementation Plan

### Step 1: Backup Current State
```bash
cd "E:\dev(dave)\skygfx_plus_expIV"
git add -A
git commit -m "WIP: before fix architecture simplification"
```

### Step 2: Remove `effectsForceAlphaBlock` Calls
Replace all ~15 calls with direct `RwRenderStateSet` at each site:
- `RenderEffects.enter` → direct state set
- `RenderEffects.exit` → direct state set
- `PostFx.exit` → direct state set
- `CClouds` → direct state set
- `CCoronas.Reflections` → direct state set
- `CCoronas.SunReflection` → direct state set
- `SceneStart` → direct state set
- `EffectsExit` → direct state set
- `FinalExit` → direct state set

### Step 3: Simplify `CClouds__Render_hygiene`
Remove:
- SEH __try/__except
- 3-layer state capture (rw cache, driver cache, raw device)
- Producer gates (g_inCoronasRender, g_inCloudsRender, s_spriteForceValid)
- Split-entry detection
- Stage-0 texture raster save/restore
- Z-test/Z-func save/restore

Keep only:
- Cloud noise parameter upload (c49-c52)
- Stage-0 texture raster save/restore (minimal)

### Step 4: Remove `pipeDeviceStateSet` Chokepoint
Replace with direct `RwD3D9SetRenderState` calls at each site. Remove the L1→L2→L3 derivation logic.

### Step 5: Remove `pipeForceAlphaBlock`
Replace with direct `RwD3D9SetRenderState` + raw device writes at each call site.

### Step 6: Add Minimal Safety Net
Only add back:
1. Additive-leak cleanup at known leak sites (corona exit, cloud exit)
2. Producer window protection only during corona/cloud body

---

## Risk Assessment

| Change | Risk | Mitigation |
|---|---|---|
| Remove `effectsForceAlphaBlock` | HIGH | Test each site individually; keep direct state sets |
| Simplify `CClouds__Render_hygiene` | MEDIUM | Keep stage-0 texture restore; remove SEH/layer capture |
| Remove `pipeDeviceStateSet` | HIGH | Replace with direct `RwD3D9SetRenderState` + raw writes |
| Remove `pipeForceAlphaBlock` | MEDIUM | Replace with direct `RwD3D9SetRenderState` + raw writes |

---

## Expected Outcome

After simplification:
1. **Fewer state thrashing** — each pass sets its own state directly
2. **No layer drift** — no 3-layer comparison to miss states
3. **No producer gate skips** — state is always set correctly
4. **No SEH swallows** — exceptions are visible and fixable
5. **AAP-aligned behavior** — simpler, more predictable

---

## Conclusion

Our fix architecture is **over-engineered** and likely **creating the bugs it's trying to fix**. The solution is to simplify to AAP's direct-state approach, keeping only the essential repairs that AAP lacks.

**Recommendation**: Proceed with Phase 1 (remove over-engineering) immediately, then test against September references.