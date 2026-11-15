# 2026-11-10 Fix Architecture Refactoring Plan

## Executive Summary
Our fix architecture is over-engineered and creating state thrashing. AAP's approach is much simpler: direct state setting per pass without save/restore. The solution is to simplify to AAP's direct-state approach.

## Current State Analysis

### What AAP Does (Simple)
- **Direct state setting**: Each pass sets its own blend states via `RwRenderStateSet`
- **No save/restore**: States set by one pass are left for the next pass to set again
- **No producer gates**: No `g_inCoronasRender`, `g_inCloudsRender`, `s_spriteForceValid` flags
- **No layer-drift detection**: No 3-layer (rw cache / driver cache / raw device) comparison
- **No SEH guards**: No `__try/__except` around render hooks
- **No corona feed to Forward+**: Coronalights stay in the game's own pipeline

### What We Built (Over-Engineered)
1. **`effectsForceAlphaBlock(tag)`** — ~15 call sites, each doing:
   - 3-layer evidence sampling (rw cache, driver cache, raw device)
   - Drift detection
   - Additive-leak substitution (2/2 → 5/6)
   - `pipeForceAlphaBlock()` which does:
     - rw cache read (5 states)
     - rw→d3d validation/conversion
     - rw cache write (if changed)
     - RwD3D9SetRenderState for 7 states (L1→L2)
     - Raw device write for 7 states (L2→L3)

2. **`CClouds__Render_hygiene`** — Massive function with:
   - SEH __try/__except
   - 3-layer state capture (12+ states across rw cache, driver cache, raw device)
   - Producer gates (skip entire function when producer active)
   - Split-entry detection (layer disagreement at entry)
   - Stage-0 texture raster save/restore
   - Z-test/Z-func save/restore

3. **`pipeDeviceStateSet`** — Chokepoint that all render state writes route through

### The Vicious Cycle
1. Each `effectsForceAlphaBlock` call does 3-layer comparison
2. The comparison can **miss** states only in raw device but not in caches
3. The producer gates can **skip** repairs when most needed
4. The SEH guards can **swallow** exceptions leaving state unknown
5. Multiple call sites can **conflict** with each other's assumptions
6. This **creates** the desyncs it's supposed to fix

## Refactoring Plan

### Phase 1: Backup and Baseline
- [x] Backup committed (7df5a48)
- [x] Force pushed to GitHub (33cc954)
- [ ] Build baseline W0.7 (current over-engineered)
- [ ] User retest baseline → record visual state

### Phase 2: Remove `effectsForceAlphaBlock` Entirely
**Goal**: Replace ~15 calls with direct `RwRenderStateSet` at each site

**Sites to fix** (from main.cpp):
- `RenderEffects.enter` (line ~2182)
- `RenderEffects.exit` (line ~2187)
- `PostFx.exit` (line ~2209)
- `Coronas.Reflections` (line ~2292)
- `Coronas.SunReflection` (line ~2302)
- `Clouds` (line ~2691)
- `SceneStart` (line ~1610)
- `EffectsExit` (line ~2191)
- `FinalExit` (line ~2212)
- `CSkidmarks__Render` (standalone)

**Replacement pattern** (AAP-style):
```cpp
// Before (over-engineered):
effectsForceAlphaBlock("RenderEffects.enter");

// After (AAP-style direct state):
RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
```

**Risk**: HIGH — test each site individually
**Files**: `src/core/main.cpp` (primary), `src/render/buildingPipe.cpp` (pipeForceAlphaBlock)

### Phase 3: Simplify `CClouds__Render_hygiene`
**Goal**: Remove over-engineering, keep only essential repairs

**Remove**:
- SEH __try/__except (lines 2934-2945)
- 3-layer state capture (lines 2785-2849)
- Producer gates (lines 2867-2882)
- Split-entry detection (lines 2851-2880)
- Most of the 12+ state save/restore

**Keep** (essential repairs AAP lacks):
- Stage-0 texture raster save/restore (lines 2740-2761, 3026-3036)
- PS/VS neutralize on entry/exit (lines 2896-2901, 3005-3011)
- Cloud noise parameter upload (lines 2661-2681)

**Simplified structure**:
```cpp
static void CClouds__Render_hygiene(void)
{
    // Cloud noise upload (keep)
    if(cloudsPS && d3d9device) { /* ... */ }
    
    // Stage-0 texture save (keep)
    void *savedTexRasterRaw = NULL;
    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedTexRasterRaw);
    // ...
    
    // PS/VS neutralize (keep)
    RwD3D9SetPixelShader(NULL);
    RwD3D9SetVertexShader(NULL);
    
    // Call original
    if(CClouds__Render_orig) {
        g_inCloudsRender = true;
        CClouds__Render_orig();
        g_inCloudsRender = false;
    }
    
    // Restore stage-0 texture (keep)
    restoreStage0Tex(savedTexRaster, savedDevTex0, "cloud_texraster_simple");
    // ...
}
```

**Risk**: MEDIUM — keep stage-0 restore, remove the rest
**Files**: `src/core/main.cpp`

### Phase 4: Remove `pipeDeviceStateSet` Chokepoint
**Goal**: Replace with direct `RwD3D9SetRenderState` calls

**Current**: All render state writes route through `pipeDeviceStateSet`
**Replace with**: Direct `RwD3D9SetRenderState` at each call site

**Sites** (from grep):
- `postfx.cpp:726` (SDV state sync)
- `postfx.cpp:3928-3930` (classify pass)
- `postfx.cpp:4011-4017` (classify restore)
- `postfx.cpp:4331-4349` (blend override)
- `main.cpp:2993-2998` (CClouds restore)

**Risk**: HIGH — state sync logic may break
**Files**: `src/render/postfx.cpp`, `src/core/main.cpp`

### Phase 5: Remove `pipeForceAlphaBlock`
**Goal**: Replace with direct state setting

**Current**: 5-state rw read + validation + conversion + 7-state push
**Replace with**: Direct `RwD3D9SetRenderState` + raw device writes

**Risk**: MEDIUM — simpler but may miss edge cases
**Files**: `src/render/buildingPipe.cpp`

### Phase 6: Minimal Safety Net
**Goal**: Keep only essential repairs AAP lacks

**Keep**:
1. **Additive-leak cleanup**: At known corona/cloud exit sites only
2. **Producer window protection**: Only during corona/cloud body
3. **Stage-0 texture restore**: For CClouds and similar passes

**Remove**:
- Global `effectsForceAlphaBlock` sweep
- 3-layer drift detection
- SEH guards on render hooks
- `pipeDeviceStateSet` chokepoint

## Implementation Order

### Step 1: Baseline Build (W0.7)
```bash
cd "E:\dev(dave)\skygfx_plus_expIV"
python tools/fast_build.py --no-deploy
# Verify tokens present
python tools/verify_tokens.py
# Deploy
python tools/fast_build.py --deploy
```
**Gate**: User retest → record visual state vs September reference

### Step 2: Remove `effectsForceAlphaBlock` (HIGH RISK)
- Replace each call site with direct state setting
- Test each site individually
- Build W0.8

### Step 3: Simplify `CClouds__Render_hygiene` (MEDIUM RISK)
- Remove SEH, 3-layer capture, producer gates
- Keep stage-0 texture restore, PS/VS neutralize
- Build W0.9

### Step 4: Remove `pipeDeviceStateSet` (HIGH RISK)
- Replace with direct `RwD3D9SetRenderState`
- Build W0.10

### Step 5: Remove `pipeForceAlphaBlock` (MEDIUM RISK)
- Replace with direct state setting
- Build W0.11

### Step 6: Final Cleanup
- Remove dead code
- Update documentation
- Build W1.0 (simplified)

## Risk Mitigation

### Testing Strategy
1. **Baseline first**: Build W0.7 (current) and record visual state
2. **Incremental**: One phase at a time, build after each
3. **Visual comparison**: Compare each build vs September reference
4. **Rollback**: Each phase is a separate commit for easy rollback

### Rollback Plan
```bash
# If a phase breaks:
git reset --hard <previous-phase-commit>
python tools/fast_build.py --deploy
# User retest
```

### Success Criteria
- Visual quality matches or exceeds W0.6
- No new glitches introduced
- Code is simpler and more maintainable
- AAP-aligned architecture

## Timeline Estimate

| Phase | Effort | Risk | Build |
|---|---|---|---|
| 1. Baseline | 1 hour | LOW | W0.7 |
| 2. Remove effectsForceAlphaBlock | 4-6 hours | HIGH | W0.8 |
| 3. Simplify CClouds__Render_hygiene | 2-3 hours | MEDIUM | W0.9 |
| 4. Remove pipeDeviceStateSet | 3-4 hours | HIGH | W0.10 |
| 5. Remove pipeForceAlphaBlock | 2-3 hours | MEDIUM | W0.11 |
| 6. Final cleanup | 1-2 hours | LOW | W1.0 |

**Total**: ~15-20 hours of focused work

## Decision Points

### After Phase 2 (W0.8)
- If visual quality improves → continue
- If visual quality degrades → rollback, investigate
- If no change → continue (architecture still simpler)

### After Phase 3 (W0.9)
- If CClouds issues resolved → continue
- If CClouds issues persist → investigate further

### After Phase 4 (W0.10)
- If state sync issues resolved → continue
- If state sync issues persist → rollback pipeDeviceStateSet removal

## Conclusion

This refactoring will:
1. **Simplify** the fix architecture to AAP's direct-state approach
2. **Reduce** state thrashing and layer drift
3. **Improve** maintainability and debuggability
4. **Align** with AAP's proven architecture

**Recommendation**: Proceed with Phase 1 (baseline) immediately, then Phase 2 (remove effectsForceAlphaBlock) as the highest-impact change.

---

**Plan created**: 2026-11-10
**Status**: Ready for execution
**Next**: Build W0.7 baseline
---

## Pass 2 evaluation verdicts (2026-11-10, post-pass-1)

| Candidate | Verdict | Reason |
|---|---|---|
| pipeDeviceStateSet chokepoint | **KEEP** | pipelinecommon.cpp:1199-1330 — it IS the N1 master-fault kill (2026-11-08): coherent L1 rw + L2 driver + L3 raw write. Junior's L2-only RwD3D9SetRenderState was the rainbow class; removing this reintroduces the [zfix2] divergence (ALPHABLEND/ZWRITE/ZTEST disagree, flush skips). |
| pipeForceAlphaBlock | **KEEP** | Same coherent-write doctrine for the canonical alpha block (12 states x 3 layers, lag-aware drift log). |
| effectsForceAlphaBlock("Clouds") (main.cpp:2596) | **KEEP** | Comment documents the observed black-square sun-glow failure it fixes: CClouds::Render sets blend only via rw cache; a device parked behind an unchanged cache turns the additive glow into a black square. Load-bearing, not redundant. |
| CClouds 3-layer save/restore | **KEEP** | Behavior (stage-0 texraster + full state restore). |
| renderstate_monitor.cpp (812 lines) | **DISABLED BY DEFAULT** | Measurement-only ("no repair, no behavior"); s_enabled 1→0 (renderstate_monitor.cpp:144). Runtime kill switch RSMon_SetEnabled(1) remains for future divergence hunts. ~186 GetRenderState calls/frame + log noise removed. |

**Refactor conclusion**: pass 1 removed the thrash layers (evidence nets, ONE/ONE substitution net, split-entry detect, SEH). Everything remaining is the coherent-write doctrine that FIXES the observed failure classes (rainbow, black square, zfix2). The architecture is now minimal: coherent writes at the boundaries, direct game state inside passes, no compare/repair cycles.
