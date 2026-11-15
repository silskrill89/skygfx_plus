# H1 Residual Attribution — W0.3 session (2026-11-09 15:54:27–16:00:24)

Status: read-only forensics + code read. No source edits, no builds.
Scope: name the remaining re-creator(s) of the device render-state divergence that
survives the W0.2 `StateBlockEnforceCanonical()` frame-end assert, from
`E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (7.4 MB, 60,604 lines, draw counter
#1 → #462753, ~3,300 rendered frames) and the live tree `E:\dev(dave)\skygfx_plus_expIV`.

---

## 0. TL;DR (ranked verdict)

The residual is **not one new skygfx raw writer**. It is a *placement + repair-coverage*
problem:

1. **The "frame-end" assert is not at frame end.** `StateBlockEnforceCanonical()` runs
   inside `DrawFinalEffects_pf` (`src/core/main.cpp:7178`), but GTA SA draws the HUD /
   radar / dialog / menu **2D (Im2D) tail after `CPostEffects::DrawFinalEffects`
   returns**. Those Im2D draws raw-write `ZENABLE=0 / ZWRITE=0 / ZFUNC=ALWAYS` (and
   sometimes `ALPHABLEND=1`) to the device **without touching the rw (L1) or RW-driver
   (L2) caches**. That is exactly the observed `scene_start` signature
   `{ZWRITE rw=1 dev=0 | ZTEST rw=1 dev=0 | ZFUNC drv=4 dev=8}` — device-only.
   Evidence: **46/46 `rs_mon: DIVERGE` are at `site=scene_start`, 0 at `frame_end`**;
   **145 `[zfix]` pipe-entry repairs** show the same device-only depth drift all frame.
   No skygfx source writes `ZFUNC=ALWAYS`/`ZENABLE=FALSE` raw (grep — only captured
   *restore* values). The writer is game-side (exe) → we own the nets, not the writer.

2. **The only complete 3-layer depth+blend canonicaliser is switched OFF by config.**
   `pipeApplyWorldBaseline("SceneStart")` (`src/core/main.cpp:3028`, `pipelinecommon.cpp:2592`)
   would re-pin all three layers immediately before the `scene_start` sample. It is gated
   on `config->stateBaselineEnable`, which is **`0` in the shipped `skygfx.ini`**
   (`skygfx.ini:199`; read at `main.cpp:5877`). During the one window it was ON
   (Xbox-mode apply, **15:55:15.658** → **15:58:52**) there were **zero DIVERGEs**
   (gap L3228 15:55:14.212 → L55551 15:58:53.291). When it is OFF the drift is
   re-created **every frame**.

3. **The per-flush nets deliberately leave part of the depth contract unrepaired.**
   `spriteEnforceDepthContract` (`src/core/rw_fixes.cpp:417-433`) repairs only
   **ZENABLE/ZFUNC** — *"ZWRITE is deliberately NOT touched"* (`rw_fixes.cpp:406`).
   `CoronaWindow_Close` (`rw_fixes.cpp:1350`) pins blend to `TRUE/5/6` and restores
   `ZWRITE=preZw` (captured) but **never touches ZENABLE/ZFUNC**
   (*"the close does not own depth"*, `rw_fixes.cpp:1366`). So a 2D pass whose close
   replays a drifted capture, or a 2D tail after all closes, keeps `ZWRITE=0`
   (and can keep `ZFUNC=8`).

4. **Corona-exit normal-alpha block — VERIFIED, no longer a gap.** It is now
   `src/core/rw_fixes.cpp:1696-1758` (the old `main.cpp:5434-5440` anchor is **stale** —
   that range is now INI/config parsing). It writes L1 (`RwRenderStateSet`, 1699-1704) +
   L2 (`RwD3D9SetRenderState`, 1705-1710) + raw L3 (1720-1724), then
   `spriteEnforceDepthContract("CCoronas_exit")` (1733). Class (b), coherent.

5. **ImGui / `DebugOverlayDraw` — RULED OUT.** It runs after the assert
   (`main.cpp:7189`) but the backend uses a `D3DSBT_ALL` state block
   (`Capture()` at `external/imgui/backends/imgui_impl_dx9.cpp:208`,
   `Apply()` at `:311`), so it restores every render state it changes
   (`ZWRITEENABLE/ZENABLE/ALPHABLEND` set at `:113/:116/:117`).

Net: the *re-creator* is the game's post-effects Im2D/HUD 2D path; the *reason W0.2
didn't reach ~0* is the assert's position + the config-disabled SceneStart baseline +
the ZWRITE/ZENABLE/ZFUNC holes in the flush nets.

---

## 1. Method + a hard caveat about receipt throttling

Both repair receipts are **throttled**, not per-occurrence:

- `[frame_repair]` — `dbglog_once("frame_repair_first") || dbglog_throttle("frame_repair")`
  (`pipelinecommon.cpp:1601`).
- `[scene_repair]` — `dbglog_once("scene_repair_first") || dbglog_throttle("scene_repair")`
  (`pipelinecommon.cpp:1747`).

So the *repair functions run every frame*; only the log line is time-bucketed (~2 s).
Consequence: **"which log line immediately precedes a `[scene_repair]`" is mostly where
the throttle bucket happened to land, not a causal phase.** I therefore report the raw
tag‑proximity table (task item 1) **and** the structurally meaningful regime split that
the timeline actually proves.

Counts from the log: `[scene_repair]`=46, `[frame_repair]`=40, `[SpriteDepthFix]`=157,
`[zfix]`=145, `rs_mon DIVERGE`=46 (all `scene_start`), `[font_flush]`=160,
`[sprite_flush]`=145, `[CoronaShield]`=152.

---

## 2. Timeline — the single most important finding

| Window | `stateBaselineEnable` | `scene_start` drift | Evidence |
|---|---|---|---|
| 15:54:27 → 15:55:15.658 | **0** (baseline skipped; `[probe] baseline scenestart` had not fired) | **present** | L374/L375 first DIVERGE+repair; L727/L728; … L3228 (15:55:14.212) |
| 15:55:15.658 → 15:58:52.057 | **1** (Xbox mode applied → `[Cull] baseline BACK`, `[probe] baseline scenestart called`) | **ABSENT — 0 DIVERGEs for 3m39s** | L3272–L3273 probes; L3355 `pipeApplyWorldBaseline: SceneStart`; last SceneStart receipt L55302 (15:58:52.057) |
| 15:58:53.264 → 16:00:24 | **0 again** (GTAV mode applied; SceneStart receipts stop) | **present every sampled frame** | L55551 DIVERGE 15:58:53.291; L55552 scene_repair; recurs at ~2 s to L60412 (16:00:04) |

Key receipts:
- `[15:55:15.658] [Mode] engage Xbox owned=82 pipes=2/1/2 cf=2` then
  `[Cull] baseline BACK` and `[probe] baseline scenestart called` (15:55:15.668).
- `[15:58:53.264] [Mode] engage GTAV owned=3 pipes=0/3/10 cf=8` — immediately followed by
  the first DIVERGE in 3m39s (`[15:58:53.291] rs_mon: DIVERGE site=scene_start …`).
- The debug menu was opened at 15:55:13.382 and 15:58:50.057 (`Debug menu toggled: 1`),
  so the toggling is consistent with the *"Frame state baseline"* menu row
  (`debugmenu_ui.cpp:820`) and/or the mode apply; either way the gate is
  `config->stateBaselineEnable`.

Interpretation: **the drift writer is running the whole session**. It is only *visible at
`scene_start`* when the SceneStart baseline does not run before the sample. W0.2's
frame-end assert leaves the device clean at `frame_end`, but the drift is re-created
between `frame_end` and the next `scene_start` (the game's 2D tail), and only the
SceneStart baseline (currently OFF) hides/fixes it before the sample.

---

## 3. Phase-precedence table (task item 1)

### 3a. Raw log-proximity table (last recognisable phase tag within 400 lines before each receipt)

> Caveat as §1: dominated by throttle placement and by the 1‑in‑30 `RSMon_Sample` gate;
> treat as *where the bucket landed*, not causal.

`[scene_repair]` (46):

| preceding tag | count |
|---|---|
| `[BPIPE]` (building pipe) | 33 |
| `[PF]` (postfx pass) | 3 |
| `[CullResync]`/mirror-cull | 3 |
| `[RENDER] frame=` | 2 |
| `[PostFX]` | 2 |
| `[Pipeline]` | 2 |
| `[SSAO]` | 1 |

`[frame_repair]` (40):

| preceding tag | count |
|---|---|
| `[PF]` postfx pass | 19 |
| `[PostFX]` | 12 |
| `[Pipeline]` | 4 |
| init / sprite-flush / fwdplus / PedOpaque / BPIPE | 1 each |

The `[frame_repair]` table is *structurally* correct: the assert lives at the end of the
PostFX/effects chain, so the preceding phase is the PostFX tail.

### 3b. Structurally-meaningful precedence (per operating regime)

| regime | immediately-preceding actor before the divergence is seen at `scene_start` | why |
|---|---|---|
| `stateBaselineEnable=0` (the residual) | **game 2D/HUD/Im2D tail** (post `CPostEffects::DrawFinalEffects`, post `CPostEffects::Render` exit) | last writer is outside every skygfx repair net; device-only depth/blend |
| `stateBaselineEnable=1` | `pipeApplyWorldBaseline("SceneStart")` | full 3-layer park-and-set + L3 pin immediately before the sample → no drift |

So: **PostFX tail is the correct phase for the `frame_repair`** (bend), and the
**2D/HUD/Im2D tail after the effects chain** is the phase that precedes the surviving
`scene_start` drift.

---

## 4. `[SpriteDepthFix]` — 157 fires: design or leak? (task item 2)

Site histogram (`grep -oE` over the log):

| site | fires |
|---|---|
| `CloudRegionFlush_7273D0` (== `CFont::RenderFontBuffer` glyph flush, 0x7273D0) | **102** |
| `SpriteFlush_70CF20` (sprite flush) | **53** |
| `CCoronas_exit` | **2** |

Semantics (`src/core/rw_fixes.cpp`):
- `spriteEnforceDepthContract(path)` — `rw_fixes.cpp:417-433`. Early-outs unless
  `devZen==0 || devZFunc!=LESSEQUAL`; otherwise logs the one-shot/throttled receipt
  `"[SpriteDepthFix] %s depth drift (dev zen=%u zfunc=%u)"` and calls
  `pipeDeviceStateSet(D3DRS_ZENABLE,TRUE)` + `pipeDeviceStateSet(D3DRS_ZFUNC,LESSEQUAL)`.
  **It never repairs ZWRITE** (`rw_fixes.cpp:406`).
- Font flush `CloudRegionFlush_7273D0_hook` (`rw_fixes.cpp:1185-1335`) calls it in **both**
  the fast path (`:1269`) and the slow path (`:1330`) on exit. The slow path pins the glyph
  blend `ALPHABLEND=TRUE / SRCALPHA(5) / INVSRCALPHA(6)` on L1+L2+L3 (`:1294-1303`) and hands
  the *entry* block back verbatim (`:1311-1320`). Receipts: `[font_flush] gate pinned=…`
  152×; `[font_flush] fast path …` when entry was already canonical.
- Sprite flush close `SpriteFlushIndexedDraw_hook` (`rw_fixes.cpp:644-895`) canonical-pushes
  `blend=1 / 5 / 6 / ADD / zwrite=1 / ztest=1` L1+L2+L3 then
  `spriteEnforceDepthContract("corona_resync")` (`:872`). Receipt `[CoronaResync]` /
  `[sprite_flush] gate shielded=…` 144×.

**Verdict:** these are **per-flush detection by design**, not a per-draw leak. Each Im2D
glyph/sprite flush raw-writes `ZTEST=0 / ZFUNC=8` on the device (the vanilla Im2D default);
the close's net detects and repairs **ZENABLE/ZFUNC** at the boundary. The count
(102 font + 53 sprite + 2 corona ≈ 157) tracks the number of flush closes where the entry
was already depth-off — i.e. ~one per flush on many flushes. It is *evidence that the Im2D
path raw-writes depth every flush*, and it leaves **ZWRITE untouched**, so it is not the
final repair for the frame.

Note the slow-path font flush also carries the known **L1 vtxAlpha family** fix
(`rw_fixes.cpp:1281-1289` captures/restores `rwRENDERSTATEVERTEXALPHAENABLE`
*parked* on TRUE) — verified present; not a gap-class writer.

---

## 5. Raw-writer census (task item 3)

Method: staged the live tree to `%TEMP%` (read-only copy) and scanned every
`dev->SetRenderState` / `d3d9device->SetRenderState` / `RwD3D9SetRenderState` /
`RwRenderStateSet` / `pipeDeviceStateSet` / `pipeL3Set` / `pipeL2ParkSet` in the named
files. Class (a) `pipeDeviceStateSet`; class (b) park-and-set (`pipeRwParkSet` +
`pipeL2ParkSet` + `pipeL3Set`); class (c) L1+L2-only or L3-only (the gap class).

Per-file totals (raw device = `d3d9device->SetRenderState`):

| file | raw L3 | RwD3D9 (L2) | RwRenderState (L1) | pipeDeviceStateSet | class-b helpers |
|---|---|---|---|---|---|
| src/core/main.cpp | 57 | 64 | 80 | 0 | 0 |
| src/core/rw_fixes.cpp | 57 | 83 | 64 | 4 | 0 (uses inline park-and-set) |
| src/render/pipelinecommon.cpp | 6 | 27 | 23 | 2 | 73 |
| src/render/postfx.cpp | 0 | 44 | 164 | 14 | 6 |
| src/render/postfx_ssao.cpp | 0 | 30 | 43 | 0 | 0 |
| src/render/postfx_newfx.cpp | 0 | 11 | 34 | 0 | 0 |
| src/render/postfx_smaa.cpp | 1 | 8 | 22 | 0 | 0 |
| src/render/postfx_utils.cpp | 1 | 2 | 2 | 0 | 3 |
| src/core/debugmenu_ui.cpp | 0 | 0 | 0 | 0 | 0 |

### 5a. Every raw writer that touches the four drift states, classified

The four drift states are `ALPHABLENDENABLE`, `SRCBLEND/DESTBLEND`, `ZWRITEENABLE`,
`ZENABLE`, `ZFUNC`. All *skygfx* raw writes of these are either coherent or captured
restores — **none emit the drift values** (`ZFUNC=ALWAYS`, `ZENABLE=FALSE`, `ZWRITE=FALSE`)
except as captured snapshots:

| file:line | states | class | phase | note |
|---|---|---|---|---|
| `main.cpp:1189-1223` (`pedOpaqueEnforce`) | blend/z/ate | (b) L1→L2→L3 | ped opaque entry | canonical depth=LESSEQUAL; coherent |
| `main.cpp:1248-1253` (`pedOpaqueRestore`) | blend/z/ate | (b) L1→L2→L3 | ped opaque exit | restores **captured** values (not drift by itself) |
| `main.cpp:1531-1558` (`pipeForceAlphaBlock`) | blend/at/blendop/z | (b) L1→L2→L3 | pipe entry / effects sweep | does **not** write ZENABLE/ZFUNC |
| `main.cpp:4347-4400` (`CClouds::Render` restore) | blend/z/ate/fog/stage | (b) L1→L2→L3 | clouds exit | restores captured; coherent |
| `rw_fixes.cpp:212-299` (`coronaDrawRawShield`) | blend/z/ate/cwr/sepA | (b) inline park-and-set L1+L2+L3 | corona per-draw | force `ONE/ONE, ZWRITE=0`; coherent |
| `rw_fixes.cpp:446-556` (`spriteFlushShieldClose`) | 11-state replay | (b) L1+L2+L3 | sprite flush close | replays **captured** snapshot; then `spriteEnforceDepthContract` (`:556`) |
| `rw_fixes.cpp:1294-1335` (`CloudRegionFlush_7273D0_hook`) | blend L1+L2+L3; depth via net | (b)+(net) | font glyph flush | entry block handed back verbatim; depth net repairs ZENABLE/ZFUNC only |
| `rw_fixes.cpp:1350-1428` (`CoronaWindow_Close`) | blend L1+L2+L3; zwrite=captured | (b) | frame boundary | **does not touch ZENABLE/ZFUNC** |
| `rw_fixes.cpp:1696-1758` (CCoronas exit normal-alpha) | blend/zwrite L1+L2+L3; depth via net | (b)+(net) | corona exit | verified pin at `:1720-1724`; `spriteEnforceDepthContract("CCoronas_exit")` `:1733` |
| `imgui_impl_dx9.cpp:113-130` (setup) | z/zw/blend/srcdst | (b by restore) | debug overlay draw | restored by `D3DSBT_ALL` block (`:206-208`, `:311`) — **not a gap** |
| `postfx.cpp` (44 RwD3D9-only) | pass-local alpha/cull | (c) for those states | postfx passes | none touch ZENABLE/ZFUNC/ZWRITE to drift values |
| `postfx_ssao.cpp` (30 RwD3D9-only) | pass-local | (c) | SSAO | none write depth-to-drift |

**Conclusion:** there is **no skygfx (c)-writer that creates the observed
`{ZWRITE=0,ZTEST=0,ZFUNC=8}` device-only state**. The genuine gap is:
(i) `spriteEnforceDepthContract` not repairing `ZWRITE` (repair coverage, `rw_fixes.cpp:406`);
(ii) `CoronaWindow_Close` not repairing `ZENABLE/ZFUNC` (`rw_fixes.cpp:1366`);
(iii) the *game's* Im2D/HUD tail writing device-only state outside all nets.

---

## 6. Named re-creators, ranked with evidence

**R1 — Vanilla Im2D / 2D (HUD/radar/dialog/menu) tail after `DrawFinalEffects`.**
- Drift is device-only: `rs_mon DIVERGE site=scene_start` shows `ZWRITE rw=1 drv=1 dev=0`,
  `ZTEST rw=1 drv=1 dev=0`, `ZFUNC rw=- drv=4 dev=8` (L55551, L55834, … all 46 lines).
- 0/46 DIVERGE at `frame_end` → the device is clean right after the assert.
- 145 `[zfix]` show the same device-only depth drift at pipe entries all frame → it is
  created by 2D passes interleaved with 3D, then repaired at 3D entries.
- No skygfx raw writer emits those values (§5a).
- Fix owner: net placement / repair coverage (below), not the writer.

**R2 — Config-disabled SceneStart baseline (`stateBaselineEnable=0`).**
- `skygfx.ini:199` = 0; reader `main.cpp:5877`; gate `main.cpp:3028`.
- ON window = 0 DIVERGEs (3m39s); OFF = DIVERGE every sampled frame (§2).
- This is the single highest-leverage lever and the reason W0.2's assert alone is not ~0.

**R3 — Repair-coverage holes in the flush nets (ZWRITE / ZENABLE+ZFUNC split).**
- `spriteEnforceDepthContract` fixes only `ZENABLE/ZFUNC` (`rw_fixes.cpp:417-433`, comment
  `:406`); `CoronaWindow_Close` fixes blend+`ZWRITE` but not `ZENABLE/ZFUNC`
  (`rw_fixes.cpp:1366`, `:1369`, `:1373`). So whichever 2D pass runs last can leave any of
  the three depth bits set wrong with no net between it and `scene_start`.

**R4 — `pipeEnforceDepthContract` does not own `ALPHABLEND`** (it is blend-blind; the blend
half lives only in `StateBlockEnforceCanonical`/`pipeApplyWorldBaseline`). Minor: explains
why `[frame_repair]` (blend) and `[zfix]` (depth) fire as separate receipts.

Ruled out: ImGui overlay (`D3DSBT_ALL` restore), corona-exit block (coherent b),
ped-opaque/clouds restore (coherent b).

---

## 7. Minimal fix sketch per suspect

1. **R1/R2 — move the canonicaliser to the true frame end and/or enable the baseline.**
   - Owner: `src/core/main.cpp` `DrawFinalEffects_pf` (7156-7192).
   - Option A (minimal, robust): call `StateBlockEnforceCanonical()` **again** at the
     *last* own-code point of the frame — i.e. at the tail of `CPostEffects__Render_hook`
     (`main.cpp:3616-3641`) and `RenderEffects_hook` (`main.cpp:3595-3612`) *after*
     `effectsForceAlphaBlock`/`CoronaWindow_Close`, or better, hang a tiny hook on the
     game's `Idle`/present after the HUD 2D pass. Cheapest correct point: immediately
     before `RSMon_Sample("scene_start")` in `RenderScene_before`
     (`main.cpp:3036-3040`) — but that is just re-enabling the baseline.
   - Option B (config): ship `skygfx.ini` with `stateBaselineEnable = 1`
     (`skygfx.ini:199`), or make `pipeApplyWorldBaseline("SceneStart")` unconditional
     (drop the `config->stateBaselineEnable` gate at `main.cpp:3028`). This is the
     proven-zero-drift configuration (Xbox window).
   - Preferred: **both** — keep the assert where it is, and make the SceneStart baseline
     unconditional (it is already ~free: SILENT on the healthy path).

2. **R3 — close the repair-coverage holes.**
   - `src/core/rw_fixes.cpp:417-433`: extend `spriteEnforceDepthContract` to also repair
     `D3DRS_ZWRITEENABLE` (park-and-set via `pipeDeviceStateSet`, target `TRUE`), or split
     out a `spriteEnforceFullDepthContract`. Update the `:406` comment accordingly.
   - `src/core/rw_fixes.cpp:1350-1428` (`CoronaWindow_Close`): add
     `ZENABLE=TRUE`/`ZFUNC=LESSEQUAL` to the close (it already owns `ZWRITE=preZw`), or
     call `spriteEnforceDepthContract("CoronaWindow_Close")` at the end. Today
     *"the close does not own depth"* (`:1366`) is the hole.

3. **R1 belt-and-braces — a 2D-tail guard.** If a hook on the game's final 2D/HUD draw is
   available, invoke `StateBlockEnforceCanonical()` there. Otherwise the unconditional
   SceneStart baseline (fix 1) is the correct, already-implemented net.

---

## 8. Quick-check answers

### 8(a) Why did `[water] temp-buffer re-render disabled` fire 0 times?

- The receipt is `dbglog("[water] temp-buffer re-render disabled (waterTempBufferHook=0,
  S1 kill-switch) -> vanilla draw")` at `src/core/main.cpp:4980`, emitted **once**
  (`dbglog_once("water_tbuf_off")`) and **only if `TempBufferVerticesStored != 0`** in the
  `else` branch of `canUseWaterPipe` (`main.cpp:4974-4980`).
- `waterTempBufferHook` is a **silent INI kill-switch, default 0**
  (`main.cpp:6155`: `readint(g_cfg…, 0) != 0`; `g_waterTempBufferHook=false`, `main.cpp:4964`).
  It is **not printed by any `[Config]` receipt** — the log has **0** occurrences of the
  string `waterTempBufferHook` and 0 `[Config]` lines for it. The only writer is the INI
  round-trip (`main.cpp:6404`, `:6978`).
- The hook **is installed**: `[15:54:27.266] HOOK: WaterLevel ::RenderAndEmptyRenderBuffer
  (4 sites)`.
- Water **is** rendering through the pipe: `[water] flatpath` 148×, `[water_rt]` 1320×.
- Therefore: the game **did** call `CWaterLevel::RenderAndEmptyRenderBuffer`, but the
  `TempBufferVerticesStored` branch that arms the once-receipt never fired with a nonzero
  store (or the once-receipt was consumed outside this window). The kill-switch is ON
  (default 0) — it simply had nothing to disable this session. No config dump proves the
  value; verify by adding `waterTempBufferHook` to the `[Config]` line if a definitive
  receipt is wanted.

### 8(b) `[texdefs] dictfallback` = 2 — which 2, and is the sweep running for LOD-* misses?

The two actual lines:

```
[15:54:46.966] [texdefs] dictfallback 'gras07Si' txd='grass_pc' found-in='grass_pc' (cross-dict recovery — broken LOD parent)
[15:54:50.692] [texdefs] dictfallback 'sm_des_bush1' txd='lod_countn2' found-in='badlands' (cross-dict recovery — broken LOD parent)
```

Miss-list tail (32 distinct, all logged at Init): `#30 'ws_trans_concr' lod_countn2`,
`#31 'vegparking2_256' lod_sfs4`, `#32 'ws_alley_conc3' lod_sfs4`; the LOD families
`lod2_las`, `lod2lae1`, `lodhuge_lan2`, `lod_countn2`, `LODjetty_SFX`, `LOD_dem_SFXRF`,
`lod_boxhses_SFSX`, `LODtnsfn`, `lod_sfs4` are all in the miss list.

Sweep status: `TexDefsFindCB` (`src/texdefs.cpp:533-598`) runs the **full cross-dict sweep
once per name**, then pins either the hit slot (`TexDefsCacheUpsert(name, hitSlot)`) or
`-1` known-miss (`:590`). The miss-total receipts grow while distinct stays 32:
`total=177 distinct=32` (15:55:23), `total=179 distinct=32` (15:56:14),
`total=181 distinct=32` (15:58:21) — i.e. **repeated lookups of the same 32 names, no new
recoveries**. So the sweep **is** running for the LOD-* misses; only **2 of the ~34
attempted names exist in any loaded TXD's pool**. The "expected ~31/32 recovered"
assumption is not supported by this log — 30 LOD textures are genuinely absent from the
loaded dicts (the `RwTexDictionaryFindNamedTexture` (0x7F39F0) pool sweep found nothing).

If more recovery is expected, the next lever is the **dict load order / parent link**
(N8's premise that "the textures do exist in already-loaded TXDs" fails for these names
this session), not the sweep's coverage.

---

## 9. Evidence index (file:line / log:line)

Code:
- `src/core/main.cpp:3028` baseline gate; `:3036` `CoronaWindow_Close`; `:3040`
  `RSMon_Sample("scene_start")`; `:3051` `pipeRepairSceneStartSync`
- `src/core/main.cpp:3595-3612` `RenderEffects_hook`; `:3616-3641` `CPostEffects__Render_hook`
- `src/core/main.cpp:3273-3407` `effectsForceAlphaBlock` (alpha-only; `pipeForceAlphaBlock`)
- `src/core/main.cpp:4959-4981` water temp-buffer kill-switch; `:6155` INI read
- `src/core/main.cpp:5877` `stateBaselineEnable` read (`iniGet2` falls back to `[SkyGfx]`)
- `src/core/main.cpp:7156-7192` `DrawFinalEffects_pf`; `:7162` `CoronaWindow_Close`;
  `:7164` baseline; `:7178` `StateBlockEnforceCanonical`; `:7189` `DebugOverlayDraw`
- `src/render/pipelinecommon.cpp:1234-1327` `pipeDeviceStateSet`; `:1486-1544`
  `pipeEnforceDepthContract`; `:1571-1606` `StateBlockEnforceCanonical`; `:1647-1760`
  `pipeRepairSceneStartSync`; `:2592-2651` `pipeApplyWorldBaseline`
- `src/core/rw_fixes.cpp:406` "ZWRITE deliberately NOT touched"; `:417-433`
  `spriteEnforceDepthContract`; `:446-556` `spriteFlushShieldClose`; `:644-895`
  `SpriteFlushIndexedDraw_hook` (`:872` resync); `:1185-1335` font-flush hook
  (`:1269`,`:1330` net; `:1294-1303` L3 pin); `:1350-1428` `CoronaWindow_Close`
  (`:1366` no-depth, `:1369` `canonBlend=TRUE`, `:1373` `preZw`); `:1696-1758`
  CCoronas exit (`:1720-1724` L3 pin, `:1733` net)
- `src/core/debugmenu_ui.cpp:820` "Frame state baseline" row; `:4789` `DebugOverlayDraw`
- `external/imgui/backends/imgui_impl_dx9.cpp:113-130` setup;
  `:206/:208` `CreateStateBlock(D3DSBT_ALL)`/`Capture`; `:311` `Apply`
- `src/texdefs.cpp:326-360` N8 rationale; `:533-598` `TexDefsFindCB` sweep/pin; `:320`
  miss-total receipt
- `skygfx.ini:199` `stateBaselineEnable = 0`

Log (`E:\games\gtasa_skygfx_plus\skygfx_dbg.log`):
- L374/L375 first DIVERGE+repair; L723 `[frame_repair]`; L727/L728; L2567/L2568
- L3228 last DIVERGE before the baseline-on window (15:55:14.212)
- L3272/L3273 `[probe] baseline …` (15:55:15.66x); L3355 `pipeApplyWorldBaseline: SceneStart`
- L55551/L55552 first post-gap DIVERGE+repair (15:58:53.29x); L55530 `[frame_repair]`
- L240, L275 dictfallback; L242/L4697/L18474/L51384 miss totals
- `HOOK: WaterLevel ::RenderAndEmptyRenderBuffer (4 sites)` (15:54:27.266)
- `[FlushShield] restore path=SpriteFlush_70CF20/corona states=11 blend=1 src=5 dst=6 zw=1
  ref=1 (captured REPLACE -> canonical normal-alpha)` (15:54:58.490)
