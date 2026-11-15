# Relay Unify Spec — collapsed state-relay audit + one-shared-machine plan

**Date:** 2026-11-09
**Author:** Oracle (strategic audit, READ-ONLY — no source edits)
**Wave ref:** m0148 — "tyr to see if there is any reducnat code duplicated which due to the way its relayring is causing majopr issues in pipeline that cuase it all"
**Root audited:** `E:\dev(dave)\skygfx_plus_expIV` (real tree; `C:\Dev\shaisse_hub\skygfx_plus` is a stale mirror and was NOT used)

---

## Objective

Prove/disprove the hypothesis that **duplicated state-relay implementations** (many independent capture/restore machines, each restoring a *different* gap set at its *own* exit, none asserting the canonical contract at the *next* machine's entry) are the root cause of the persistent pipeline failures — then produce an implementation-ready unify spec that collapses those machines onto **one** shared `StateBlock` capture/restore.

---

## Findings

### A. MAP — every capture/restore state machine in `src/`

The user's 8 machines are real. The audit found **14** (the 8 + 6 more). Every one is a hand-rolled "capture → force → restore" triple with a *different* state subset and a *different* restore semantics. Line numbers below are the **live** file positions (the wave record's numbers drift by a few lines; I trust the code, not the record).

| # | Machine | Location | Layers covered | Restore semantics | Known gaps | Receipt(s) |
|---|---|---|---|---|---|---|
| 1 | **SpriteFlushState** | `main.cpp:3577` (struct), `:3593` capture, `:3716` close | L1+L2+L3, 11 states + 3-layer depth pair | park-and-set replay; captured-REPLACE→canonical rewrite | Depth pair (ZENABLE/ZFUNC) was added piecemeal AFTER the 9-state core — the "now 3-layer depth fields" note in the record | `[FlushShield]`, `[SpriteDepthFix]`, `[CloudShield]`, `[probe] sprite_flush` |
| 2 | **Font flush** `CloudRegionFlush_7273D0_hook` | `main.cpp:4455` (hook), `:4455-4606` | OWN inline capture: L1 (src/dst/vtx) + L2 (blend/src/dst) + L3 (blend/src/dst) | verbatim per-layer; fast path skips capture; enforce on both paths | Fast path used to leak zen/zfunc (band-aid `spriteEnforceDepthContract` at `:4539`/`:4600`); **ZWRITE never restored** (font draws own ZWRITE off) | `[font_flush]`, `[SpriteDepthFix]` |
| 3 | **Corona blend shield** `coronaDrawRawShield` + `CoronaWindow_Close` + entry/exit | `main.cpp:3482` (shield), `:4620` (frame close), `:5734-6024` (entry/exit) | blend/ZWRITE park-and-set L1+L2+L3; alpha-test/colorwrite per-draw | **exit normal-alpha block is L1+L2-ONLY by design** (`:5944-5969`); frame-close pins blend TRUE | **Prime suspect** (see chain below): L3 blend/src/dst/BLENDOP never pinned at exit → device left additive under a cache claiming normal-alpha | `[CoronaDraw]`, `[CoronaShield]`, `[CoronaExit]`, `[CoronaResync]`, `[CoronaZ]`, `[SpriteDepthFix]` |
| 4 | **PedOpaqueState** | `main.cpp:1080` (struct), `:1090` capture, `:1164` enforce, `:1228` restore | L1+L2+L3 | verbatim per-layer; **Z/alpha stickies** in `myDefaultCallback` (`:2696-2769`) | Blend half dead (fields deleted 2026-11-09). **FULLY DUPLICATED** in `skin.cpp` (see #4b) | `[PedOpaque]`, `[PedZW]`, `[PED]`, `[PedRS]` |
| 4b | **SkinOpaqueState + SkinEntry/ExitContract** | `skin.cpp:80-260` (opaque), `:415` (entry), `:693` (exit), `:795` setAlpha/ZWrite, `:1018` dual | L1+L2+L3 | identical verbatim + sticky | **This is a verbatim copy** of main.cpp's ped machine (pedsPipeEnable=1 path); main.cpp keeps the byte-identical pedsPipeEnable=0 path. Two live copies of the same logic. | same as #4 |
| 5 | **ShadowContractState** | `main.cpp:1360` (struct), `:1374` capture, `:1479` force, `:1520` restore, `:1676` run | L1+L2+L3 + stage-0 tex | verbatim; heal L2+L3 from L1 on drift | Marked for extraction to `shadows.cpp` (does **not exist yet**) | `[shadow_contract]`, `[ShadowHeal]`, `[ShadowTex]` |
| 6 | **PostFxFlush_701FE9_hook** + `flushBatchIdentity` | `main.cpp:4381` (hook), `:4303` (identity) | reuses #1's SpriteFlushState | reuse capture/force/close | Producer discrimination is L1-only (correct post-fix) | `[postfx_flush]`, `[probe] postfx_flush` |
| 7 | **pipeEnter/ExitAlphaMode** + `pipeApplyWorldBaseline` + `pipeForceAlphaBlock` | `pipelinecommon.cpp:2035`/`:2259`, `:2530`; `buildingPipe.cpp:561` | L1+L2+L3 | canonical entry + faithful/sticky exit | The *correct* shared pipe-level machine — but only covers the alpha block, and is bypassed by every effects/2D writer | `[PipeAlpha]`, `[Pipeline]` |
| 8 | **GrassEnforceDepthContract** | `grasssys.cpp:189-395` | L1+L2+L3 (Z) + stage-0 tex | enforce-not-park (reference impl) | Once-per-frame gate | `[grass_zfix]`, `[veg]` |
| 9 | **buildingPipe stage-0 contract** `bldStage0DevContract` + `bldBindDiffuse3L` | `buildingPipe.cpp:1189-1490` | L1+L2+L3 stage-0 texture | park-and-set | per-system bind contract | `[BldBind]` |
| 10 | **postfx raw-geom machine** `SaveRawGeomStates`/`RestoreRawGeomStates`/`RestoreOneRawGeomState` | `postfx_utils.cpp:189-338` | L1+L2+L3 | park-and-set (the *fixed* version — was the generator) | Correct now; only 9 states | `[statesync]` (consumer) |
| 11 | **PostfxStateSyncCheck** | `postfx.cpp:1627-1672` | L2+L3 only | heal L2 := L3 | Heals the *symptom*, not the creator | `[statesync]` |
| 12 | **fade-multiply bracket** | `postfx.cpp:4178-4198` | L1+L2+L3 | **already routed through `pipeDeviceStateSet`** | Correct | `[PipeChain]` |
| 13 | **mirrorPass MirrorRestoreState3L** | `mirrorPass.cpp:125-290` | L1+L2+L3 | private park-and-set copy | Another duplicate of the same idiom | — |
| 14 | **small writers** `setAlphaTestTwoLayer`/`setZWriteTwoLayer` (+skin copies, `setMoonAlphaBlendStates`) | `main.cpp:230-249`, `:4745-4755`; `skin.cpp:795-815` | L1+L2+L3 | direct set (no park) | No park → cache-hit no-op window | `[PedDual]` etc. |

**Duplicate-idiom inventory (the mechanical proof of the hypothesis):** the same "park-and-set through L1/L2/L3" idiom is re-derived in at least **6 separate places** — `pipeRwParkSet/pipeL2ParkSet/pipeL3Set` (pipelinecommon), `pipeForceCullMode`, `pipeEnter/ExitAlphaMode`, `pipeApplyWorldBaseline`, `spriteFlushShieldClose`, `CoronaWindow_Close`, `coronaDrawRawShield`, `RestoreOneRawGeomState`, `MirrorRestoreState3L`, `shadowContractRestore`, `bldStage0DevContract`. The **ped state machine is duplicated wholesale** across `main.cpp` and `skin.cpp`. The **depth-contract enforce** is duplicated 4× (`pipeEnforceDepthContract`, `spriteEnforceDepthContract`, `GrassEnforceDepthContract`, the font-flush fast path). This is not "one fix per constant" — it is the same fix, hand-typed, with different gap sets each time.

### B. THE DIVERGENCE COMPOUNDING CHAIN (one concrete end-to-end cycle)

The `scene_start` DIVERGE signature the record cites is `ALPHABLEND rw=0 dev=1 | ZWRITE rw=1 dev=0 | ZTEST rw=1 dev=0 | ZFUNC drv=4 dev=8`. Here is the exact writer sequence that re-creates it every ~2s. Frame boundary order in `RenderScene_before` (`main.cpp:4965`) is:

1. `pipeApplyWorldBaseline` (`:4975`) — asserts canonical L1+L2+L3 (blend OFF, ZWRITE ON, ZTEST ON, ZFUNC LESSEQUAL).
2. `CoronaWindow_Close` (`:4982`) — **overwrites** the baseline's blend-OFF with blend-ON (`:4639 canonBlend=TRUE`).
3. `RSMon_Sample("scene_start")` (`:4986`) — **samples the residue of the previous frame** (the divergence).
4. `pipeRepairSceneStartSync` (`:4997`) — repairs it (this is the `[scene_repair]` receipt).

The divergence is **already present at step 3**, i.e. it was manufactured at the *end of the previous frame* by the 2D/effects tail. The concrete writer sequence for the `ALPHABLEND rw=0 dev=1` leg:

1. **Corona body** draws additive (ONE/ONE, blend TRUE) — through L1+L2+L3 (`CCoronas__Render`, L1-only in vanilla, but the shield `:5836-5858` forces all three).
2. **Corona exit restore** (`main.cpp:5944-5969`) — the normal-alpha block writes L1 (rw) + L2 (driver) to `SRCALPHA/INVSRCALPHA`, blend=`preBlendDev` (captured pre-window = **FALSE**, the world baseline value), but **deliberately does NOT pin L3** for ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP (comment at `:5922-5928` argues the L2 dirty-mark will flush it).
   - **The flaw:** the L2 `applied[]` cache already holds `SRCALPHA/INVSRCALPHA/FALSE` from the world baseline, so `pending[] == applied[]` and the pre-draw flush **value-compares into a no-op**. L3 stays additive (blend=1). This is *exactly* the N1 master fault `pipeDeviceStateSet` documents (`pipelinecommon.cpp:1198-1232`), and this exit does not use it.
3. **Font/2D/HUD tail** (glyph flush `:4455` + Im2D): parks ZTEST=0 + ZFUNC=ALWAYS(8) + ZWRITE=0 on L3; `spriteEnforceDepthContract` (`:3687`) re-asserts ZTEST=1/ZFUNC=LESSEQUAL on L3 but **never touches ZWRITE**.
4. **No machine re-pins L3 blend or L3 ZWRITE after this point.** The residue → `scene_start` samples `ALPHABLEND rw=0 dev=1` (L1 world-baseline blend=0, L3 additive blend=1) and `ZWRITE rw=1 dev=0` (L1 baseline TRUE, L3 2D-phase FALSE) and `ZFUNC drv=4 dev=8` (L2 baseline LESSEQUAL, L3 2D-phase ALWAYS).

The `PedZW` Mode A/B receipts are the *same residue* seen one consumer later: ped entry (`main.cpp:2282-2287`) finds `dev zfunc=8` (Mode A) or `zw=0 ztest=0` (Mode B) because the 2D/effects tail left it and the ped entry contract is the *only* thing that repairs it — so peds drew over vehicles/buildings until the ped entry guard fired.

**Verdict on the writer: the user's named prime suspect is correct.** The corona exit restore's normal-alpha block (`main.cpp:5944-5969`) is the machine that leaves `L1/L2 = normal-alpha (blend off)` while `L3 = additive (blend on)`, because it is deliberately L1+L2-only for the blend family. The secondary gap is `spriteEnforceDepthContract` omitting ZWRITE, which leaves `ZWRITE rw=1 dev=0`.

### C. VERDICT on the hypothesis

**The hypothesis is CONFIRMED as the root cause of (a) and (b), and a strong co-factor in (c).** Ranked by evidence:

1. **(a) H1 `scene_start` DIVERGE — CONFIRMED root cause.** The divergence is manufactured at frame-end by machines (#3 corona exit + #2 font) that restore *their* subset (L1+L2 blend, ZTEST/ZFUNC) but never assert the canonical contract on L3 for blend-enable and ZWRITE. `pipeRepairSceneStartSync` heals it at scene_start, but the *creator* fires again at the next frame tail → the "re-created every ~2s" pattern is **structural**, not random. This is precisely "machine A's restore gap becomes machine B's dirty entry."

2. **(b) vehicle-over-peds (`PedZW` Mode A/B) — CONFIRMED root cause.** Mode A (`dev zfunc=8`) and Mode B (`zw=0 ztest=0`) are the same frame-end residue inherited at ped entry. The ped machine (#4/#4b) is *correct* — it detects and repairs — but it is the *last line of defense* against a leak that three upstream machines should have stopped. The "vehicles draw over peds" symptom is the window between the leak and the ped-entry repair.

3. **(c) crash-adjacent instability — PARTIALLY explained / NOT proven root cause.** The duplicated machines increase the *surface area* for state divergence (and the SEH/`__finally` latch complexity in `spriteFlushShieldClose`/`PostFxFlush_701FE9_hook` is a real fault-latch risk), but the crash-adjacent instability the record ties to it (camera-raster swap, 0x7FBD4A null derefs) is a **separate defect** tracked by `sky_raster`/`fbWp` in `RenderScene_before` (`:5059-5150`). The relay duplication does **not** explain the raster swap.

**What the hypothesis does NOT explain:** the camera-raster swap (`s_camRasGame` repair), the dead luminance chain (`[tonemap] lum fallback`), and the shader-handle reset hazards. Those are orthogonal and should not be folded into this unify.

---

## Changes (DOC ONLY — this spec)

### The shared design: one `StateBlock`, one `capture()`, one `restore(mode)`

Collapse the 14 machines onto a single `StateBlock` living in **`pipelinecommon.cpp`** (exported via `skygfx.h`), covering the **union** of every state set above:

```
StateBlock {                       // union of all captured sets (L1/L2/L3)
  // L1 rw twin set (9):  VERTEXALPHAENABLE, SRCBLEND, DESTBLEND, ZWRITEENABLE,
  //                      ZTESTENABLE, ALPHATESTFUNCTION, ALPHATESTFUNCTIONREF,
  //                      CULLMODE, FOGENABLE, TEXTURERASTER
  // L2 driver set (14):  ALPHABLENDENABLE, SRCBLEND, DESTBLEND, ZWRITEENABLE,
  //                      ZENABLE, ZFUNC, ALPHATESTENABLE, ALPHAFUNC, ALPHAREF,
  //                      FOGENABLE, BLENDOP, COLORWRITEENABLE,
  //                      SEPARATEALPHABLENDENABLE, CULLMODE (+ BLENDFACTOR for
  //                      the postfx raw-geom set)
  // L3 device set (14 + stage-0 texture ptr)
};
StateBlockCapture(StateBlock*)   // one read of all 3 layers, pre-seeded
                                 // canonical, sanitized (rsmonCapture doctrine)
StateBlockRestore(const StateBlock*, mode)   // mode ∈ {VERBATIM, PARK_SET, ENFORCE}
StateBlockEnforceCanonical(void)             // assert canonical world contract on
                                             // ALL 3 layers (blend OFF, SRCALPHA/
                                             // INVSRCALPHA, ZWRITE ON, ZTEST ON,
                                             // ZFUNC LESSEQUAL, CULL BACK, CWR ALL,
                                             // SEPALPHA OFF, ALPHATEST ON)
```

The `ENFORCE` mode **always writes L3** (the single property that kills the residue). `VERBATIM`/`PARK_SET` keep byte-parity with the current per-layer restores where required. `pipeDeviceStateSet` becomes the only L3 writer inside the block.

### Phase plan (file-disjoint, respecting standing directive #2 + per-system `.cpp` + main.cpp slim-down)

> **Standing constraints honored:** `main.cpp` phases are marked **post-extraction** (the parallel `shadows.cpp` extraction must land first). One shared impl per fix/constant. Per-system end state in each subsystem's own `.cpp`.

| Phase | Files | Delete / replace | Byte-parity req | Receipts that survive | Build gate |
|---|---|---|---|---|---|
| **P0 — shared block (no behavior change)** | `pipelinecommon.cpp` (+`skygfx.h` decl) | ADD `StateBlock` + `capture`/`restore`/`enforce`. `pipeDeviceStateSet`/`pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set`/`pipeEnter/ExitAlphaMode` stay. | New code only; no existing call sites touched. | all existing receipts | compiles; `rs_mon` counters unchanged in a soak |
| **P1 — postfx raw-geom** | `postfx_utils.cpp` | Replace `RestoreOneRawGeomState`+`RestoreRawGeomStates` internals with `StateBlockRestore(PARK_SET)` on the raw-geom subset. | Per-state park/target identical; `BLENDFACTOR`/`BLENDOP`/`ALPHATESTENABLE` L1-leg-skip preserved. | `[statesync]` should drop toward 0 | `rs_mon` `cache=` → 0; no `[statesync]` in soak |
| **P2 — corona/sprite/font exit (the killer)** | `main.cpp` (post-extraction) | Replace `coronaDrawRawShield`'s manual park-set + `CoronaWindow_Close` + `CCoronas__Render_blendprobe_hook` exit restore + `spriteFlushShieldClose` + font-flush inline capture/restore with `StateBlock` calls. **The corona exit normal-alpha block becomes `StateBlockEnforceCanonical()` (L3 included).** | Blend family L3 pinned; ZWRITE L3 pinned; alpha-test pair verbatim. | `[SpriteDepthFix]`, `[scene_repair]`, `[PedZW]` Mode A/B → ~0; `[CoronaExit]`/`[FlushShield]` retained | `[scene_repair]` count ≈ 0 over a 90s retest |
| **P3 — ped machine dedupe** | `skin.cpp` + `peds_pipe.h` | Fold `SkinOpaqueState` into `StateBlock`; `SkinEntryContract`/`SkinExitContract` call `StateBlockCapture`/`Restore`. Delete the `main.cpp` legacy inline ped path **only after** `pedsPipeEnable=0` rollback is retired (directive: one impl). | Sticky ZWRITE + sticky-depth rules preserved verbatim (they are `mode`-level policy, not removed). | `[PedZW]`, `[PedOpaque]`, `[PedRS]` retained | `[PedZW]` → 0 with `pedsPipeEnable` in both 0 and 1 |
| **P4 — shadow + building + mirror** | `shadows.cpp` (new, post-extraction), `buildingPipe.cpp`, `mirrorPass.cpp` | `ShadowContractState` → `StateBlock` (on extraction); `MirrorRestoreState3L` → `StateBlockRestore(PARK_SET)`; `bldStage0DevContract` keeps its stage-0 texture niche but delegates the scalar set to the block. | heal-on-drift policy preserved. | `[ShadowHeal]`, `[BldBind]` retained | `[ShadowHeal]` fires only on genuine drift |

**File-disjoint guarantee:** each phase touches a disjoint set of files; P0 is additive; P1/P2/P3/P4 can be reviewed and landed independently without cross-phase merge conflict.

### The single change that drops `[scene_repair]`/`[SpriteDepthFix]`/`[PedZW]` to ~0 (acceptance metric)

> **Call `StateBlockEnforceCanonical()` at frame END** — one shared 3-layer canonical assert inserted in `RenderScene_after` (`main.cpp:6719`) **after** the HUD/2D/font phase and **before** the `frame_end` RSMon sample. This is the *frame-end twin* of the scene-start repair already present in `RenderScene_before`. It does what none of the 14 machines do today: assert the canonical contract on **L3** after the last 2D write, so the residue the 2D/effects tail manufactures can never survive to the next `scene_start`.

- `[scene_repair]` → ~0: the divergence is healed *before* it is sampled (the repair at scene_start becomes a no-op).
- `[SpriteDepthFix]` → ~0: `ZFUNC=8`/`ZENABLE=0` cannot survive the frame boundary.
- `[PedZW]` Mode A/B → ~0: peds no longer inherit `zw=0 ztest=0 zfunc=8` because it was cleared at frame_end.

This is **one function call**, not a rewrite — it is the minimal acceptance-metric lever. P1/P2 (the `pipeDeviceStateSet`/`StateBlock` routing) are the *preventative* fix that removes the creators; the frame-end assert is the *curative* fix that makes the retest pass immediately. Ship both; the frame-end assert is what moves the counters this retest.

---

## Verification (receipt behavior per phase)

| Receipt | Pre-unify (baseline) | After frame-end assert (P0+curative) | After full unify (P1-P4) |
|---|---|---|---|
| `[scene_repair]` | every ~2s | ~0 (no-op) | ~0 (creator gone) |
| `[SpriteDepthFix]` | 29×/1.5min | ~0 | ~0 |
| `[PedZW]` Mode A/B | 13×/1.5min | ~0 | ~0 |
| `[zfix2]` CONTRACT | 27× | ~0 at `depthct_out` | ~0 everywhere |
| `[statesync]` | recurring ~1s | unchanged | ~0 |
| `rs_mon_report` `diverge=%` | >0 | → 0 | → 0 |
| `[CoronaExit]`/`[FlushShield]`/`[PedOpaque]` | present | retained (proof of liveness) | retained |

**Byte-parity check:** P0 must be provably no-behavior-change (soak `rs_mon` unchanged). P2's `StateBlockEnforceCanonical` at the corona exit is the *intentional* behavior change that pins L3 — verify by `rs_mon` at `scene_start` going clean, not by bit-diffing.

---

## Status + flags

- **Status:** AUDIT COMPLETE, SPEC READY. No source edits, no builds, no commits (per ROOT directive).
- **FLAG 1 — stale mirror:** the workspace CWD (`C:\Dev\shaisse_hub\skygfx_plus`) is NOT the live tree. The audit ran against `E:\dev(dave)\skygfx_plus_expIV`. Any downstream implementer must use the `E:` root.
- **FLAG 2 — `shadows.cpp` does not exist yet.** The record says ShadowContractState is "moving to shadows.cpp"; the file is absent from the tree. P4 must wait for that extraction.
- **FLAG 3 — dual ped path is a live fork, not dead code.** `pedsPipeEnable=0` (legacy inline in `main.cpp`) and `=1` (module in `skin.cpp`) are both reachable. P3 cannot delete the `main.cpp` copy until the rollback lever is retired; collapsing it while both levers exist risks a half-migrated fork. Sequence P3 *after* `pedsPipeEnable` is default-1 and the `=0` path is confirmed dead.
- **FLAG 4 — `pipeEnter/ExitAlphaMode` faithful/sticky restore (parity-6) is already the correct "one impl".** Do not re-derive it inside `StateBlock`; it is the reference the block's `ENFORCE`/`VERBATIM` modes must reproduce. The block should *call* `pipeEnter/ExitAlphaMode`, not replace it.
- **FLAG 5 — the record's line numbers drift ±20 lines** from the live file (e.g. `main.cpp:3581`→`3577`, `:4648`→`4620`, `:5900-5930`→`5944-5969`). The spec cites live positions; treat the record's numbers as approximate.
