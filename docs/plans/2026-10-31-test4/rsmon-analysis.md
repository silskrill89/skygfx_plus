# Render-State Monitor Analysis — test4 (rs_mon / corona_blend)

**Date:** 2026-10-31 · **Analyst:** @explorer (read-only log analysis)
**Log:** `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (+ `skygfx_dbg.prev.log`)

---

## 0. Important data-location correction

The task brief attributes 29 `FIRST_DIVERGE` hits to the 01:24:45→01:27+ run
(`skygfx_dbg.log`). **That run contains ZERO divergence lines.** Actual counts:

| File | Run window | FIRST_DIVERGE | DIVERGE | rs_mon_report w/ diverge>0 | corona_blend |
|---|---|---|---|---|---|
| `skygfx_dbg.prev.log` | 00:57:12 → 00:58:16 (frames #778→#57135) | **2** | **21** | **9** (pct 39→78) | 28 |
| `skygfx_dbg.log` | 01:24:40 → 01:27:21 (frames #2→#288614) | 0 | 0 | 0 (29/29 reports agree=100%) | 79 |

The "29 hits" figure matches neither file exactly; the divergence data lives
entirely in **prev.log** (the earlier 00:57 run, also pipeline=Xbox
`buildingPipe=1 vehiclePipe=2 colorFilter=2`, but **without**
`stateBaselineEnable` — no `[probe] baseline …` lines exist there). The 01:24
run has `stateBaselineEnable=1` (baseline probes at `scenestart`,
`effects_exit_postfx/renderfx/final`) and is completely clean at the sampled
sites. Both runs are analysed below; the contrast is itself the key finding.

---

## 1. Divergence table (prev.log, 00:57 run — the divergent run)

rs_mon samples 11 states at 3 layers: **L1 rw cache / L2 RW D3D9 driver / L3 raw device**.
Only 2 sites ever diverged: `scene_start` and `frame_end` (`sites=2` in every report).

### D1 — site=`scene_start` (pipe=-1), first hit frame **#778** @ 00:57:32.341

| State | rw (L1) | drv (L2) | dev (L3) | Meaning of dev value | Direction |
|---|---|---|---|---|---|
| SRCBLEND | 5 | 5 | **2** | ONE | dev≠(rw=drv) |
| DESTBLEND | 6 | 6 | **1** | ZERO → **REPLACE mode** | dev≠(rw=drv) |
| ZWRITE | 1 | 1 | **0** | off | dev≠(rw=drv) |
| ZTEST (ZENABLE) | 1 | 1 | **0** | off | dev≠(rw=drv) |
| ZFUNC | – | 4 | **8** | ALWAYS | dev≠(rw=drv) |

Context at first hit: immediately after `[MirrorPass] gate` + `[CullResync]
mirror exit rw=1`, right before `RenderScene_before: frame 1` and DepthHook
install. Signature = a **2D-quad/REPLACE flush with depth disabled** left the
raw device dirty before the first scene sample of the frame.

Recurrence (monotonic n, never re-synced): n = 1 (#778) → 2 (#1834) → 343
(#8654) → 1179 (#25374) → 1325 (#28276) → 1508 (#31753) → 1640 (#40213) → 1694
(#43333) → 1828 (#49858). Same 5-state signature every time.

### D2 — site=`frame_end` (pipe=-1), first hit frame **#1820** @ 00:57:36.196

| State | rw (L1) | drv (L2) | dev (L3) | Meaning | Direction |
|---|---|---|---|---|---|
| CULLMODE | 1 | 1 | **2** | CW (rw wants NONE) | dev≠(rw=drv) |
| ALPHABLEND | 0 | 0 | **1** | ON (rw wants off) | dev≠(rw=drv) |

Context at first hit: directly follows the PostFX chain for frame 2 —
`[PF] Lens group (bypass path) done: FB resynced` (#1820), and one tick earlier
`[CoronaShield] forced additive ONE/ONE` + `[CoronaBlend] dev blend=1 src=2
dst=2` (#1816). Next line repairs it: `[cullfix] pipe entry cull reconciled → 2`.

Recurrence: ALPHABLEND dev=1 vs rw=drv=0 at n = 790 (#17600) → 1048 → 1122 →
1194 → 1216 → 1241 → 1269 → 1319 → 1380 → 1404 → 2543 → 4496 → **6351** (#57135,
last sample of the run). Never re-synced at the sampled site.

### Aggregate reports (prev.log)

| Frame | samples | agree | diverge | pct | bad | top site |
|---|---|---|---|---|---|---|
| #784 | 2 | 1 | 1 | 50 | 0 | scene_start |
| #8440 | 1693 | 1 | 664 | 39 | 1028 | scene_start (332) |
| #27340 | 3583 | 249 | 2306 | 64 | 1028 | scene_start (1277) |
| #30885 | 3955 | 347 | 2580 | 65 | 1028 | scene_start (1462) |
| #33787 | 4119 | 347 | 2744 | 66 | 1028 | scene_start (1544) |
| #38209 | 4251 | 347 | 2876 | 67 | 1028 | scene_start (1610) |
| #42037 | 4371 | 347 | 2996 | 68 | 1028 | scene_start (1670) |
| #44778 | 4523 | 347 | 3148 | 69 | 1028 | scene_start (1746) |
| #53935 | 6394 | 349 | 5017 | **78** | 1028 | **frame_end (3151)** |

`bad=1028` is constant from the first full report to the end — the monitor is
observe-only in this run; nothing ever repairs the sampled drift.

---

## 2. Findings

### (1) TOP diverging state

**ALPHABLENDENABLE at `frame_end`** — dev=1 vs rw=drv=0, cumulative n→6351, and
it made `frame_end` the top site (top_n=3151, 78 % diverge) by the end of the
run. Runner-up: the scene_start blend/depth cluster (SRCBLEND/DESTBLEND
ONE/ZERO + ZWRITE/ZENABLE off + ZFUNC ALWAYS, n→1828). Both signatures are
"device left dirty by a raw-D3D9 writer that never told RW".

### (2) Sites that NEVER re-sync (persistent across frames)

**Both sites.** `scene_start` diverged on every sampled frame from frame 1 to
the end (n grows monotonically 1→1828, identical signature), and `frame_end`
ALPHABLEND diverged on every sampled frame from frame 2 to the end (n→6351).
`bad=1028` never moves. Why: in that run `stateBaselineEnable=0` (no baseline
probes logged) and rs_mon is observe-only; the per-pipe-entry repairs
(`[cullfix]`, `[zfix]`, `[PipeAlpha] entry resync`) run *after* the scene_start
sample, so the monitor always sees the stale pre-repair device.

### (3) Divergence direction vs subsystems

Every divergence is **dev≠(rw=drv)** — L1 and L2 always agree, only the raw
device (L3) is dirty. That means the dirtying code writes via the raw
`IDirect3DDevice9` (bypassing `RwRenderStateSet`), i.e. mod hooks / game direct
D3D code — never RW-tracked state. Subsystem correlation:

- **frame_end blend-ON/CULL-CW → corona flush + PostFX (postfx lane).** Direct
  causal chain in the log: `#1816 CoronaShield forced additive ONE/ONE` +
  `CoronaBlend dev blend=1 src=2 dst=2` → `#1820 FIRST_DIVERGE frame_end
  ALPHABLEND rw=0 drv=0 dev=1`. The end-of-frame PostFX/tonemap passes
  (`Lens group bypass`, `FxAlpha PostFx.repaired`) run right at the sample.
- **scene_start REPLACE/no-depth → 2D quad flushes + water FFP fallback
  (waterPipe lane + main).** `[water] flatpath … → FFP fallback (pipe
  unavailable)` lines interleave the scene_start divergence windows (water
  count 1109→1749 between samples #25374→#49858); the FFP fallback path and the
  corona/splash Im2D flushes both leave REPLACE-style blend + depth-off on the
  device. `[PedRS]` lines, by contrast, always show rw=drv=dev agreement — ped
  draws are **not** a divergence source (the PedOpaque entry repairs its own
  ZWRITE/ZFUNC drift before drawing).
- **buildingPipe is a victim, not a source:** `[BldZW] entry rw=1 drv=1 dev=1 →
  restore=verbatim` (clean), while `[BldElide] writes skipped … Xbox=1127→142566`
  shows the Xbox building lane absorbing the drift.

---

## 3. corona_blend correlation (79 hits in the 01:24 run)

Pattern distribution (`[corona_blend] Ns game Im2D draw: dev blend=X … | drv blend=Y`):

| dev blend | drv blend | prev.log (28) | current log (79) |
|---|---|---|---|
| 1 | 0 | 5 (incl. FIRST at #2) | 7 |
| 0 | 1 | 5 | **50** ← dominant |
| 1 | 1 (agree) | 15 | 15 |
| 0 | 0 (agree) | 3 | 7 |

Current-run extras: `rw raster=00000000` in **71/79** lines and
`spriteBatch=0` in **74/79** — the game's Im2D corona flush runs outside a
valid RW raster target with an empty sprite batch, drawing with device blend
OFF while the RW driver cache still says ON (the log's own annotation: *blend 0
= REPLACE → black quads*).

Correlation with rs_mon windows:
- **prev.log:** corona_blend lines bracket the FIRST_DIVERGE scene_start
  (#762/#785 around #778) and the FIRST_DIVERGE frame_end (#1861 right after
  #1820, showing `dev blend=0 | drv blend=1 src=10 dst=1`). The corona flush is
  in the same state-neighbourhood as both divergence signatures.
- **current log (stateBaselineEnable=1):** all 29 rs_mon reports are
  agree=100 %/sites=0, yet **57/79 corona_blend draws still disagree**. The
  baseline re-sync runs at scene boundaries (scenestart + effects_exit_*), so
  the mid-frame Im2D flush site is unsampled and unrepaired — the black-quad
  risk persists even though the monitor is clean.

Residual drift in the clean run (all repaired at pipe entry, never sampled by
rs_mon): 29× `[zfix] pipe entry depth drift repaired → ZENABLE=TRUE
ZFUNC=LESSEQUAL` (dev zfunc=8/ALWAYS drift), 30× `[PedRS]`, `[PedOpaque]
pre-entry dev ZWRITE=0 ZFUNC=8`.

---

## 4. Top-3 ranked conclusions

1. **ALPHABLENDENABLE @ frame_end is the #1 diverging state** (dev=1 vs
   rw=drv=0, n→6351, drove diverge-pct to 78 % and flipped top site to
   frame_end). Cause: corona flush / CoronaShield additive force + end-of-frame
   PostFX passes write blend via the raw device without updating the RW cache.
   **Lane: postfx** (with the corona flush in main).
2. **scene_start blend/depth cluster (SRCBLEND/DESTBLEND=ONE/ZERO REPLACE,
   ZWRITE/ZENABLE off, ZFUNC ALWAYS) diverged every sampled frame and never
   re-synced** (n→1828, bad=1028 constant). Cause: 2D quad flushes (corona
   Im2D / splash-foam) plus the **water FFP fallback** (pipe unavailable) leave
   the device dirty before the scene_start sample; pipe-entry repairs
   (cullfix/zfix/PipeAlpha) run after the sample. **Lane: waterPipe (FFP
   fallback) + main (2D flush)**.
3. **`stateBaselineEnable=1` fully fixed the sampled sites** — the 01:24 run
   shows 29/29 rs_mon reports agree=100 %, sites=0, bad=0 (vs 39–78 % diverge
   without it). Remaining gap: **57/79 corona_blend Im2D draws still disagree**
   (dominant pattern dev blend=0 | drv blend=1, rw raster NULL, spriteBatch=0)
   at mid-frame sites the monitor doesn't sample → black-quad corona risk
   persists. **Next step:** extend the baseline/rs_mon coverage to the Im2D
   flush site (or pin blend at the corona draw itself).

### Lane attribution summary

| Lane | Role in divergence |
|---|---|
| **postfx** | Primary source of D2 (frame_end blend-ON/CULL-CW after tonemap/lens passes + CoronaShield additive) |
| **waterPipe** | Contributor to D1 — FFP fallback draws (pipe unavailable) interleave scene_start divergence windows |
| **main** | 2D Im2D flush (corona/splash) leaves REPLACE/no-depth state → D1 signature; corona_blend mid-frame site still uncovered by baseline |
| **buildingPipe** | Victim only — entry state clean after repairs (`BldZW restore=verbatim`); Xbox elide counters absorb drift |
