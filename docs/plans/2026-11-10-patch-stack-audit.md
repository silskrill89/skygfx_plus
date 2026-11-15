# 2026-11-10 Patch-Stack Strip-Back / Universal-Fix Audit

## Method
- Source: `.serena/memories/wave-15..wave-20-record.md`, `docs/plans/*.md`, and receipt token scan of `E:\games\gtasa_skygfx_plus\skygfx.asi` (2,625,536 bytes).
- Classification: (i) genuine bug fix, (ii) patch-on-patch shim, (iii) temporary band-aid, (iv) diagnostic/telemetry.

## Fix Inventory (by receipt token)

| Receipt | File:Line | Classification | Strip-Back Candidate? |
|---------|-----------|----------------|----------------------|
| `bld_zw_guard` | buildingPipe.cpp:2121-2148, :2312, :3198 | (i) genuine bug fix — Z-write park guard | KEEP |
| `bld_opaque_nopark` | buildingPipe.cpp:2420, :3382 | (i) genuine bug fix — narrowed park predicate | KEEP |
| `texflush` | rw_fixes.cpp:1228-1234 | (i) genuine bug fix — black-quad fail-open | KEEP |
| `cloud_split_restore` | main.cpp:2851-2880, :2936-2999 | (ii) patch-on-patch shim — 12-state restore | **STRIP-BACK CANDIDATE** |
| `AUTOEXP_DEGENERATE` | postfx_luminance.cpp:125-137, :273-308 | (i) genuine bug fix — RW cache desync | KEEP (universal fix) |
| `radvcs_srcmismatch` | postfx.cpp:914-936 | (i) genuine bug fix — Radiosity VCS screen-size guard | KEEP |
| `blurvcs_srcmismatch` | postfx.cpp:1130-1153 | (i) genuine bug fix — Blur VCS content-rect | KEEP |
| `stale-write skip` | softbody.cpp:250-290, :582-604, :1038-1067 | (i) genuine bug fix — family-C TOCTOU | KEEP |
| `gib retire` | ragdoll_death.cpp:1138-1156 | (i) genuine bug fix — gib lifetime | KEEP |
| `n9_ped_remap` | buildingPipe.cpp:1373-1381 | (i) genuine bug fix — sentinel honor | KEEP |
| `n9_pedcb_remap` | skin.cpp:927-948, render_callbacks.cpp:458-466 | (i) genuine bug fix — sentinel honor | KEEP |
| `far_fog` | main.cpp:1664-1682 | (i) genuine bug fix — far/fog re-update | KEEP |
| `CAPass` | postfx_newfx.cpp:783, :799 | (i) genuine bug fix — CA scope gate | KEEP |
| `fx_ca` | postfx_newfx.cpp:775-786, :825 | (i) genuine bug fix — CA contract | KEEP |
| `FXAA3Pass` | postfx_newfx.cpp:864-888 | (i) genuine bug fix — FXAA scope gate | KEEP |
| `PF_SCOPE` | (various) | (iv) diagnostic/telemetry | KEEP (low overhead) |
| `VCS RT parity` | postfx.cpp:1000-1025 | (iv) diagnostic/telemetry | KEEP (low overhead) |
| `[FxAlpha]` | main.cpp:2546-2552 | (ii) patch-on-patch shim — alpha restore | **STRIP-BACK CANDIDATE** |
| `[texdefs]` | texdefs.cpp:604-630, :752-773, :1040-1060 | (i) genuine bug fix — sentinel consumer honors | KEEP |
| `[nm_install]` | texdefs.cpp:1684-1731 | (i) genuine bug fix — pipeline install | KEEP |
| `[Softbody]` | softbody.cpp (various) | (i) genuine bug fix — TOCTOU gates | KEEP |
| `[ragdoll]` | ragdoll_death.cpp (various) | (i) genuine bug fix — gib retire | KEEP |
| `[N5]` | treeWind.cpp | (i) genuine bug fix — TreeAlpha | KEEP |
| `[FxAlpha]` | main.cpp:2546-2552 | (ii) patch-on-patch shim | **STRIP-BACK CANDIDATE** |

## Strip-Back Candidates

### 1. CClouds split restore (main.cpp:2936-2999) — HIGH IMPACT
**Reason**: 12-state restore via `pipeDeviceStateSet` is complex and may be introducing its own issues. The SEH `__try/__except` may be masking real CClouds faults.
**Revert**: Remove the entire `CClouds__Render_hygiene` hook + restore block. Let CClouds run natively.
**Universal Fix**: Fix the underlying CClouds texture/state issue (if any) at the source, or accept native CClouds behavior.
**Risk**: MEDIUM — may re-introduce the original CClouds state clobbering.

### 2. `[FxAlpha]` restore (main.cpp:2546-2552) — MEDIUM IMPACT
**Reason**: This is a comment block describing the alpha restore; the actual restore is in the CClouds split. If we strip CClouds, this becomes dead code.
**Revert**: Remove the comment block + any associated code.
**Universal Fix**: N/A — this is a subset of the CClouds strip-back.

### 3. Depth-bias dual-layer (main.cpp:219-268) — LOW IMPACT
**Reason**: The `D3D9RenderDual_WithDepthBias` may be over-applying depth bias. However, this is a genuine bug fix for the raw-SetTexture desync class.
**Revert**: N/A — this is the universal fix for the desync class. KEEP.

## Universal Fixes (should apply to ALL paths)

1. **RW cache desync fix** (`AUTOEXP_DEGENERATE` + `_rwD3D9RenderStateFlushCache`) — universal, keep.
2. **CA scope gate** (`CAPass`, `FXAA3Pass`) — universal, keep.
3. **Sentinel consumer honors** (`n9_ped_remap`, `n9_pedcb_remap`, `[texdefs]`) — universal, keep.
4. **Family-C TOCTOU** (`stale-write skip`, `gib retire`) — universal, keep.

## Recommended Clean Rebuild Sequence

1. Strip CClouds split restore (main.cpp:2936-2999) + `[FxAlpha]` comment.
2. Rebuild W0.7.
3. Retest vs September reference.
4. If CClouds native behavior is acceptable, keep stripped. If not, restore and investigate the underlying CClouds issue.

## No Source Edits, No Builds, No Commits
This is a read-only analysis.