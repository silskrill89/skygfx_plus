# Log divergence timeline — 2026-10-25 22:35:22 → 22:42:48

Source: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (6,152,664 bytes, 63,421 lines).
Read-only analysis. All quoted lines are verbatim from that log.

---

## 0. Run identity

```
[22:35:22.707] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

Expected size/build confirmed. The ASI mtime (21:57:27) is 43 s after the compile stamp — normal copy-to-game-dir lag.

`skygfx_dbg.prev.log` **exists** (3,099,010 bytes, 31,384 lines) and is a **different, older binary**:

```
[16:41:32.740] ... [BUILD] ... size=2337280 mtime=2026-10-25 16:32:02 built=Oct 25 2026 16:31:18
```

- Prev run: 16:41:32.719 → 16:45:10.917 (3 m 38 s), PBR mode (`Config: pipeline=0 buildingPipe=3 vehiclePipe=10 colorFilter=8`).
- Current run: 22:35:22.682 → 22:42:48.161 (7 m 26 s), **Xbox → mode-cycle → PS2 → Xbox**.
- The current binary is 23,552 bytes larger than the prev one, so prev cannot be used as a same-binary A/B.

**This is not a steady-state gameplay capture — it is a live mode-cycling regression session.** Eleven mode applications fire inside the run:

| time | event |
|---|---|
| 22:35:22.701 | `[Mode] apply Xbox pipes=2/1/2 cf=2 iv=0` (startup) |
| 22:36:49.424 | `[Menu] top tabs: avail=1260 required=648 pad=6.0 share=134.0 compact=0` |
| 22:37:04.730 | `[Mode] apply PS2 pipes=1/0/0 cf=1 iv=0` |
| 22:37:07.934 | `[Mode] apply Xbox pipes=2/1/2 cf=2 iv=0` |
| 22:37:09.054 | `[Mode] apply Legacy PC pipes=2/1/1 cf=2 iv=0` |
| 22:37:10.179 | `[Mode] apply GTA IV pipes=4/2/9 cf=7 iv=1` |
| 22:37:14.271 | `[Mode] apply GTA V pipes=1/0/0 cf=0 iv=0` |
| 22:37:22.900 | `[Mode] apply Modern pipes=0/3/10 cf=8 iv=0` |
| 22:37:26.634 | `[Mode] apply Custom pipes=1/0/0 cf=0 iv=0` |
| 22:37:29.442 | `[Mode] apply PS2 pipes=1/0/0 cf=1 iv=0` |
| 22:41:42.749 | `[Mode] apply None pipes=1/0/0 cf=0 iv=0` |
| 22:41:45.855 | `[Mode] apply Xbox pipes=2/1/2 cf=2 iv=0` |

Consequence: the long middle of the log (22:37:29 → 22:41:42, 4 m 13 s) is **PS2**, not Xbox and not PBR. Any claim about "the PBR path" must not be drawn from this run. The `[Pipeline]` receipts prove it: `pipes(bld=1 veh=2)` early, then `pipes(bld=0 veh=0)` for the PS2 middle.

In-game clock (BLIT tag) runs 10:08 → 17:09 across the 7 minutes; weather/time is being driven, so sky/tonemap values are not comparable across the run.

---

## 1. Read the counts as receipts, not occurrences

`dbglog_throttle()` is `skygfx.h:89`:

```c
#define DBGLOG_THROTTLE_MS 2000
// 256 buckets + FNV-1a
inline bool dbglog_throttle(const char *tag) {
    static DWORD lastTick[256] = {};
    ...
    int idx = (int)(h & 255u);
```

That is **one 2 s window per hash bucket**, shared by every tag that hashes to that bucket. Three consequences:

1. **A count is a sampling receipt, not a fault count.** `[PedOpaque] contract ...` 212 times over 424 s = it won almost every 2 s window; the underlying repair may run every ped atomic.
2. **Some tags are structurally starved.** Hashing every `dbglog_throttle`/`dbglog_once` tag in `src/` (316 tags) shows only 163 buckets used. The collisions that matter here:

| bucket | tags that share it |
|---|---|
| 248 | `cull_enter`, **`fx_alpha`**, `ras_unsafe`, `vel_no_depth` |
| 208 | **`corona_resync`**, `postfx_after`, `ssao_skipped` |
| 203 | **`PedZWrite`**, `fp`, `radiosity` |
| 206 | `blend_leak`, **`ped_opaque_pre`** |
| 117 | `pipeline_alpha_exit`, **`shadow_contract_exit`** |
| 119 | `VehEnvMask`, **`corona_shield`**, `hf_crash2`, `pf_vel` |
| 225 | `cf_modern_grading`, `cull_enter_first`, `sprite_flush_force` |
| 8 | `pf_lumapass`, `shadow_contract` |

   So `[FxAlpha] ... drift` is under-reported by colliding with `[Cull] enter`; `[Cull] enter` is under-reported for the same reason (144 enters vs 209 exits — impossible unless starved). `CoronaResync` never appears at all, which is consistent with starvation (or with the window-close path never running; the two cannot be separated from this log).

3. **`[FxAlpha] PostFx.repaired` is unconditional.** It is emitted by the throttle only, after the repair, at `src/core/main.cpp:3876`. All 212 lines are byte-identical:

```
[22:42:46.351] ... [FxAlpha] PostFx.repaired: rw(vtx=0 src=5 dst=6 zw=1) dev(vtx=0 src=5 dst=6 zw=1)
```

   This tag is a **heartbeat proving the repair converged**, not a fault. Do not count it as one.

`dbglog_once` tags (`[Cull] baseline BACK`, `[ShadowHeal]`, `[CoronaExit]`, `[PedOpaque] pre-entry`, `[PedDual] ...`) fire exactly once per process and are the highest-value lines in the log.

**Caveat:** the log's binary is from 21:56:44; `src/` may be newer. Example: the tree contains a `[PedCull] mode=...` receipt (`src/core/main.cpp`, ped entry) that never appears in the log, so at least that block post-dates the binary. All `file:line` below are from the current tree.

---

## 2. Tag inventory

Parsed 63,421 lines, 53,588 of which carry no leading `[TAG]`. First/last are in the log's own clock.

| tag | count | first | last |
|---|---:|---|---|
| `[PedOpaque]` | 213 | 22:35:40.941 | 22:42:46.329 |
| `[shadow_contract]` | 212 | 22:35:41.042 | 22:42:46.281 |
| `[FxAlpha]` | 240 | 22:35:40.973 | 22:42:46.351 |
| `[PipeAlpha]` | 296 | 22:35:40.945 | 22:42:46.240 |
| `[Cull]` | 354 | 22:35:40.943 | 22:42:46.249 |
| `[CullResync]` | 212 | 22:35:41.015 | 22:42:46.361 |
| `[PedRS]` | 157 | 22:35:40.970 | 22:42:42.281 |
| `[PED]` | 75 | 22:35:40.947 | 22:42:46.330 |
| `[VEHICLE]` | 74 | 22:35:53.509 | 22:42:44.380 |
| `[BUILDING]` | 120 | 22:35:40.975 | 22:42:46.391 |
| `[CoronaShield]` | 73 | 22:35:41.045 | 22:42:34.256 |
| `[CoronaDraw]` | 198 | 22:35:52.692 | 22:42:46.260 |
| `[CoronaBlend]` | 212 | 22:35:41.047 | 22:42:46.262 |
| `[CoronaFeed]` | 212 | 22:35:40.972 | 22:42:46.378 |
| `[water]` | 47 | 22:37:57.185 | 22:42:34.248 |
| `[Ped]` (ambient) | 167 | 22:35:40.951 | 22:42:46.248 |
| `[MirrorPass]` | 425 | 22:35:22.712 | 22:42:46.333 |
| `[Pipeline]` | 1067 | 22:35:40.944 | 22:42:46.298 |
| `[ShadowHeal]` | **1** | 22:35:41.044 | — |
| `[CoronaExit]` | **1** | 22:35:41.049 | — |
| `[PedDual]` | 2 | 22:35:40.955 | 22:35:40.964 |
| `rs_mon_report` | 86 | 22:35:41.216 | 22:42:47.637 |
| `[shadow_install]` | 4 | 22:35:22.734 | 22:35:22.736 |

### Expected tags that are ABSENT (absence is evidence)

| tag | occurrences | why it is absent / what it would have told us |
|---|---:|---|
| `pf_gate` | **0** | `PostfxGateSummary()` exists at `src/render/postfx.cpp:1377-1380` but never emits. Its bucket 59 is only lightly used (`smaa_pending`, `smaa_vel`, 12 lines total), so starvation does not explain a 424 s silence. The per-frame gate summary is **not being reached** on this path. All the `[PF] ... self-gates` lines are the only gate evidence. |
| `pipe_alpha_restore` | **0** | `src/render/pipelinecommon.cpp:1883-1884`. Logs only when the parity restore deviates from the derived value. Zero means the restore never logged a deviation — yet `[PipeAlpha] faithful restore` shows `L2=0 L3=0 (derived 1)` 179 times. The two receipts disagree about what "deviation" means; the restore path is not covered. |
| `VehElide` | **0** | `src/render/vehiclePipe.cpp:2373`. PBR/env elision counters never non-zero — expected in PS2/Xbox pipes, absent in Modern mode too (only 28 `VehiclePBR` lines, all at 22:37:22-24). |
| `bpipe_elide` | **0** | `src/render/buildingPipe.cpp:1103`. `s_bpipeElideSkip[]` stayed all-zero for the whole run: no building state write was elided. Good news, but it also means the "writes skipped" optimisation is dead in this configuration. |
| `water_state` | **0** | `src/core/main.cpp:5159`. Inside the water-pipe branch; the gate fails every frame, so the branch never executes. See `[water] gate failed` below. |
| `CoronaResync` | **0** | `src/core/main.cpp:3020-3023`. Either starved by bucket 208 (`postfx_after`) or the flush-window close never ran. This is the single most important absence: the canonical world resync after the corona window is **unverified**. |
| `[shadow_contract] stored` | **0** | The stored-shadow hook was **never installed**. Install receipt at 22:35:22.735: `0x53E0C3 op=E8 orig=68E8DD00 MISMATCH ... SKIPPED — vanilla path kept`. So `CShadows::RenderStoredShadows` (0x708300) runs **unwrapped**, with whatever state it inherits, and its exit is never healed. Only `static` (84) and `extra` (128) receipts exist. |
| `[CoronaDraw] src=2` | **0** | Every one of the 198 receipts has `src=5 dst=6`. The flush never arrives in the ONE/ONE additive state — it arrives in normal-alpha or REPLACE and is forcibly converted. |
| `[PED]` ALPHATEST/ZFUNC per-span | **0** | `[PedRS]` prints cull/blend/src/dst/zw/afunc/aref, but **not** ALPHATESTENABLE or ZFUNC. The two states that can actually erase geometry are unmonitored at ped exit. |
| `[Cull] enter saved=3` | **0** | CULLMODE=FRONT never observed entering a pipe span. |
| `[PED] dual pass` beyond 11 | 11 | Only 11 receipts; per-atomic decision is unthrottled in code, so this is the real count (Xbox#2 epoch only). |

---

## 3. Timeline

### 3.1 Phase table

| window | phase | dominant events | interpretation |
|---|---|---|---|
| 22:35:22 – 22:35:40.936 | init | build, hooks, `shadow_install` stored-shadow MISMATCH, shader compile, 5.4 s init gap | one 5.44 s gap at 22:35:33.747→22:35:39.184 (`InitialiseGame_hook: original returned successfully`). Not gameplay. |
| 22:35:40.93 – 22:35:52 | **first frames** | `[PedOpaque] pre-entry` (device Z off, ALPHATEST off), `[Cull] enter saved=1`, `[PipeAlpha] entry resync ... op=4294967295 test=0`, `[ShadowHeal]`, `[CoronaExit] pre-window blend=0`, 5× non-screen raster 201DD090 in building cb, 6× `TypeOfMirror=2` | entire first frame runs on **leftover 2D/loading state**: ZENABLE=0, ZFUNC=8 (ALWAYS), ZWRITE=0, ALPHATEST=0, BLENDOP=0xFFFFFFFF. Camera raster is the mirror buffer 2048×1024 during building geometry. |
| 22:35:52 – 22:36:45 | Xbox steady #1 | `[BUILDING] ZWRITE (rw=0 drv=0 dev=0)` every window; `[VEHICLE] ZWRITE (rw=1 cache=1 dev=0)`; `[CoronaDraw] pre-force ... blend=0`; `[PedRS] ok` only | state leak is already **persistent**: the rw cache itself is ZWRITE=0 at building entry. Ped exit is clean. |
| 22:36:45 – 22:37:04 | divergence onset | first `[PedRS] DIVERGED` (22:36:45.034), first `[PED] ZWRITE (rw=1 dev=0)` (22:36:53.115), `FxAlpha PostFx.exit` drift (22:36:48.996) | device starts losing ALPHABLENDENABLE/ALPHAREF relative to both caches. No mode change in this window — the drift precedes the menu. |
| 22:37:04 – 22:37:29 | **mode cycling** | 8 mode applications, `RasterInit`/SMAA init, `VehiclePBR`/`VehOpacity`/`Tonemap`, `[PED] dual pass skipped` first at 22:37:08 | pipeline churn; every mode apply reconfigures pipes and re-inits rasters. Fault receipts continue non-stop through the churn (the throttle does not pause). |
| 22:37:29 – 22:41:42 | **PS2 steady** | `[PedRS] DIVERGED` ×19, `[PED] ZWRITE` ×49, `[CoronaShield] ... ztest=1->0` ×37 (PS2 only), `[water] gate failed` ×39, `[FxAlpha]` drift ×16 | long, stable PS2 gameplay (4 m 13 s). This is where the bulk of the numbers come from. |
| 22:41:42 – 22:41:45 | "None" | `[Mode] apply None` | 1.25 s gap immediately after; no shader re-init log. |
| 22:41:45 – 22:42:48 | Xbox steady #2 | `[CoronaShield] ztest=1->1` (never 0), `[PED] dual pass skipped` ×10, `[PED] ZWRITE` ×10, `[water]` ×8 | ztest drift disappears when `coronaZtest` target is 1 again; ped cutout detection collapses (`hasAlpha=0`). |

### 3.2 Per-second detail (seconds that change the picture)

Format: `second | events | interpretation`.

| second | events (verbatim where quoted) | interpretation |
|---|---|---|
| 22:35:40 | `.941` `[PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 \| rw ref=2 fn=5 vtx=0 zw=0 ztest=1` · `.943` `[Cull] enter saved=1 forced=1` · `.945` `[PipeAlpha] entry resync: dev src=5 dst=6 vtx=0 op=4294967295 test=0 rw ref=2 fn=5` · `.947` `[PED] ZWRITE off/desynced on entry (rw=0 dev=0)` · `.975` `[BUILDING] cb camRas=201DD090 2048x1024 != screen 1768x992` | **worst single second of the run.** First scene frame inherits Im2D state: depth off + ALWAYS, zwrite off, alpha test off, BLENDOP garbage. Cull is NONE. Camera raster is the mirror buffer. Every guard fires and repairs. |
| 22:35:41 | `.013` first `TypeOfMirror=2 ... pBuffer=201DD090` · `.044` `[ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)` · `.049` `[CoronaExit] restored normal-alpha ... pre-window blend=0 vtxalpha=0` | shadow pass enters with **device FOG on, cache FOG off** → healed. Corona pass entered with **blending disabled** → exit restores normal alpha. |
| 22:35:42 / 44 / 46 / 48 / 50 | `[BUILDING] cb camRas=201DD090 2048x1024` on 5 of 6 consecutive windows; `[FxAlpha] Coronas.Reflections camRas=...` on 2 | the mirror raster is live during **world geometry** for ~10 s. This is the documented "scene rendered into a non-screen raster" class; it is the only concrete black-screen mechanism in the log. |
| 22:35:52 | `.692` first `[CoronaDraw] pre-force dev blend=0 src=5 dst=6 ... (black-quad=blend 0)` · `.717` first `[BUILDING] ZWRITE off/desynced on entry (rw=0 drv=0 dev=0)` · `.694/…` `[CoronaFeed] pool=0 fed=0 rej(id0=64 sz=0 rgb0=0 int0=0) regFed=7` | corona flush arrives in **REPLACE** (blend off) → shield forces additive. Immediately after, the building pipe enters with **all three layers ZWRITE off**. Corona feed sees an empty pool (64× id=0). |
| 22:35:53 | `.509` first `[VEHICLE] ZWRITE off/desynced on entry (rw=1 cache=1 dev=0)` · `.425` `[shadow_contract] static ... drift=2 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 ...) dev(blend=0 ... zw=0 ...)` | vehicle path: caches say zwrite on, **device off**; shadow pass: device blend/zw lost vs caches. First proof the split is direction `dev < caches`. |
| 22:36:00 | `.327` `[PedOpaque] contract repaired divergent entry: dev(ref=1 fn=7 blend=0 zw=1 zen=1 zfunc=8 atest=0) rw(ref=1 fn=7 vtx=1 zw=1 ztest=1) drv(ref=1 fn=7 blend=1 zw=1)` | device ALPHAFUNC=ALWAYS(8) + ALPHATEST off while cache says GREATEREQUAL. This is the "alpha test vanished" fingerprint, on the ped path, inside the Xbox epoch. |
| 22:36:38 | `[shadow_contract] extra ... drift=4 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=0 ... fog=0) dev(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)` | largest drift seen (4 states): device blend ON, ref 100 vs 0, fog ON vs OFF. Pre-shadow pollution is a **device-vs-cache** split, not a cache-vs-cache one. |
| 22:36:45 | `.034` first `[PedRS] DIVERGED cull rw=2 drv=2 dev=2 \| blend rw=1 drv=1 dev=0 \| ... \| zw rw=1 drv=1 dev=1 \| afunc rw=5 drv=5 dev=5 \| aref rw=100 drv=100 dev=100` | device lost **ALPHABLENDENABLE** only; everything else agrees. Cull/write/ref are all sane. |
| 22:36:53 | `.115` first `[PED] ZWRITE off/desynced on entry (rw=1 dev=0)` in gameplay (the earlier one was rw=0/rw=0) | device-only ZWRITE loss begins; persists for most of the rest of the run. |
| 22:37:04 – 22:37:29 | 8 `[Mode] apply ...`; `RasterInit` 22:37:10.273-277; `VehiclePBR` 22:37:22.911-24.930; `SMAA` 22:37:10.218-25.124 | mode-cycling: the pipeline is torn down/rebuilt. Fault receipts continue, so none of them are caused by mode switching alone. |
| 22:37:40 | `.480` `[PedOpaque] contract ... dev(ref=100 fn=5 blend=1 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` | **real layer-3 ZWRITE loss on the ped path** (dev 0 vs both caches 1). This is the leading candidate for the "peds look wrong at some angles" class (no depth written for the ped). |
| 22:37:44 | `.859` first `[CoronaShield] forced additive, was src=5 dst=6 blend=0 op=1 zw=1 atest=1 ztest=1->0` | PS2 epoch: the shield also disables ztest (config-driven 0), so every later world draw has to re-enable it. Fight loop starts. 37 receipts until 22:41:40. |
| 22:37:57 | `.185` first `[water] gate failed (enable=0 vs=23D5E3A8 ps=23D5E790) -> original draw` | water parallax is **disabled in config**; the water pipe never runs. `water_state` therefore never logs. |
| 22:38:14 – 22:39:12 | `[PED] ZWRITE (rw=1 dev=0)` every 2 s without a single miss (29 receipts in 58 s); `[VEHICLE]`/`[BUILDING]` in step; `[CoronaDraw] blend=1` mostly | **persistent, self-recreating** split: every pipe entry repairs all three layers, every exit restores the split per layer, the next entry re-detects it 2 s later. |
| 22:38:26 / 22:38:28 | `[PedRS] DIVERGED ... afunc rw=5 drv=5 dev=5 \| aref rw=100 drv=100 dev=1` | device ALPHAREF = **1** instead of 100. With ALPHAFUNC=GREATER this makes *more* texels pass (fat/opaque silhouettes), not fewer. |
| 22:39:12 → 22:41:05 | **113 s with zero `[Ped]`/`[PedRS]`/`[PED]` receipts**, but building/vehicle/corona receipts continue | either no peds on screen or the ped pipe was idle. The ped monitor is blind for 113 s; do not read this as "no divergence". |
| 22:40:08 – 22:41:02 | `[CoronaDraw] blend=0` on 10 of 12 windows; `[CoronaShield] ztest=1->0` continuous | worst ratio of "flush arrives in REPLACE" during PS2. Every one is repaired by the shield, but the shield is the only thing standing between this state and black quads. |
| 22:41:42 – 22:41:47 | mode → None → Xbox; 1.25 s log gap 22:41:45.905→22:41:47.155 | pipeline re-apply hitch. `[PED] ZWRITE` resumes immediately at 22:41:47.543. |
| 22:41:49 – 22:42:46 | `[PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=048C6410` ×10; `[CoronaShield] ztest=1->1`; no `ztest=1->0` | Xbox#2: the ped cutout classifier never sees alpha (`hasAlpha=0`), so the legacy dual pass is always skipped. |
| 22:42:46 | `.230` last `FxAlpha RenderEffects.enter` drift · `.281` last `shadow_contract` · `.329` last `PedOpaque contract` · `.339` `texalpha: 26372 opaque, 2505 cutout, 5 faded, 152749 unknown` · `.391` last `[BUILDING] ZWRITE (rw=0 drv=0 dev=0)` | run ends with the same leaks it started with. 152,749 of ~181k texalpha probes are **unknown** (85%) — the alpha classifier is effectively blind. |

### 3.3 What runs continuously (every ~2 s window)

These are not events; they are the steady background. Their persistence is the finding.

```
[PedOpaque] contract repaired divergent entry       212 (≈ every window)
[shadow_contract] <static|extra> pre-entry drift    212 (≈ every window)
[FxAlpha] PostFx.repaired (heartbeat, not a fault)  212
[CoronaBlend] dev blend=1 src=2|5 dst=2             212 (post-corona state always additive)
[CoronaFeed] pool negative, fed=0, rej id0=64       212 (no corona ever registered)
[CullResync] mirror exit rw=1                       212 (cull left at NONE after every mirror pass)
[PipeAlpha] entry resync                            111 (only when device differs)
[CoronaDraw] pre-force ...                          198 (198 of 212 windows)
```

`[CoronaFeed]` is worth calling out: every line is `pool=<negative> fed=0 rej(id0=64 sz=0 rgb0=0 int0=0)`, ending at `pool=-114480 ... regFed=0`. The 64-slot corona pool is entirely `id==0` for the whole run, so coronas are not the source of the sprite quads — `CPointLights` street-lamp/glow sprites are.

---

## 4. Transient or persistent, and which layer disagreed

| fault | transient / persistent | layer split (rw / drv / dev) | correlates with |
|---|---|---|---|
| First-frame Z/ALPHATEST/zwrite garbage | **transient** (one frame, self-healed by the entry guards) | all three layers wrong; `rw zw=0` and `dev zw=0` agree, `rw ztest=1` vs `dev zen=0` | startup only (22:35:40), mirror raster live |
| `[BUILDING] ZWRITE (rw=0 drv=0 dev=0)` | **persistent**, 111/115 receipts | **all three** off (rw cache included) | every window, all epochs; slightly worse in PS2 |
| `[VEHICLE] ZWRITE (rw=1 cache=1 dev=0)` | **persistent**, 74/74 receipts | dev only | every window; absent in the first 12 s only |
| `[PED] ZWRITE (rw=1 dev=0)` | **persistent in clusters** (22:37:18–22:39:12; 22:41:05–22:41:47; 22:42:08–22:42:44) | dev only (drv **not printed** — blind) | gameplay; density tracks ped presence, not mode |
| `[PedRS] DIVERGED` | **transient per exit** (17% of sampled exits) but recurring | dev only: `blend dev=0` 21×, `aref dev=1/0` 4×, `afunc` dev 0/1 once | ped exits; slightly worse PS2 |
| `[shadow_contract]` drift | **persistent**, 212/212 | dev vs rw: blend 161, fog 97, ref 94, zw 72; drv vs rw: blend 64 | strongest right before shadows; independent of epoch |
| `[CoronaDraw] pre-force blend=0` | **transient per flush** (shield repairs) but 45% of flushes | dev only (blend bit) | sprite flush, both epochs; worst 22:40:08–22:41:02 |
| `[CoronaShield] ztest=1->0` | **PS2-only**, 37 receipts | device ztest, forced to 0 then re-forced to 1 by baseline | `coronaZtest=0` in PS2 mode |
| `[FxAlpha] ... drift` | **transient**, 26 receipts (starved) | dev vs caches: `vtx dev=1` 10×; `src/dst dev` 7×; `d3d vtx` 2× | postfx exit / RenderEffects / SunReflection; follows corona+sky order |
| `[PipeAlpha] entry resync` | **transient per pipe entry**, 111 receipts | dev only: `vtx=1` 81×, `vtx=0` 30×, `op=0xFFFFFFFF`+`test=0` 1× | any pipe entry; 81/111 are device blend ON that the entry forces OFF |
| `[water] gate failed` | **configuration**, not a fault | n/a | PS2 + Xbox#2 only because that is when water geometry is drawn |
| `[PED] dual pass skipped` | **persistent in Xbox#2** | n/a (texture alpha probe) | Xbox#2 only, after the mode re-apply |

**Camera correlation.** The log contains **no camera position or angle** anywhere. What it does contain that correlates with a camera move is the **mirror pass**: `TypeOfMirror=0` 206× (no mirror in view) and `TypeOfMirror=2, pBuffer=201DD090` 6× at 22:35:41–51. The 5 `[BUILDING] cb camRas=201DD090` events fall inside exactly that window. If "screen goes black at random angles" is real, this is the only mechanism in the log that can produce it: world geometry is rendered while `Scene.camera`'s raster is the 2048×1024 mirror buffer instead of the 1768×992 screen. After 22:35:51 the mirror is never `TypeOfMirror=2` again, so this run cannot confirm or refute the symptom after startup.

**Object-class correlation.** Fault count per epoch (Xbox#1 / mode-cycle / PS2 / None / Xbox#2):

```
[PedOpaque] contract        41   13  126   1   31
[shadow_contract]           41   13  126   2   30
[BUILDING] ZWRITE           36    8   52   1   18
[VEHICLE] ZWRITE            14    1   42   1   16
[PED] ZWRITE                 2    3   49   0   10
[PedRS] DIVERGED             2    3   19   0    3
[CoronaShield] ztest 1->0    0    0   37   0    0
[CoronaDraw] blend=0        20    9   43   0   17
[PED] dual pass skipped      0    1    0   0   10
```

The vehicle/ped/building ZWRITE faults track *time on screen*, not mode. The corona ztest fight is PS2-only. The ped cutout skip is Xbox#2-only.

---

## 5. Top 5 recurring state faults

### #1 — World `ZWRITE` off in the **rw cache** at building entry (115 receipts; 111 with all three layers off)

Verbatim:

```
[22:35:52.717] [INFO ] :0 () - [BUILDING] ZWRITE off/desynced on entry (rw=0 drv=0 dev=0) — forcing TRUE for cb
[22:42:46.391] [INFO ] :0 () - [BUILDING] ZWRITE off/desynced on entry (rw=0 drv=0 dev=0) — forcing TRUE for cb
```

Only 4 of 115 are the device-only form `(rw=1 drv=1 dev=0)`. On 111 windows **the cache itself is wrong**, which means a writer called `RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, FALSE)` and never restored it.

Most likely raw writer: **`src/core/main.cpp:2812-2814`** (`coronaDrawRawShield`):

```c
RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);   // 2812  L1
RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);             // 2813  L2
d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);       // 2814  L3
```

This is the only per-draw site that writes ZWRITE=FALSE through **L1** and does not restore it on the same code path: the vehicle FX leg at `src/render/vehiclePipe.cpp:705-706` does the same L1/L2 write but restores it at `:729-730`, whereas the corona shield relies entirely on the later window close. The matching L1/L2/L3 write also exists in the corona *entry* shield at `main.cpp:4125-4127`.

Why it survives: the normal window-close resync does restore ZWRITE (`main.cpp:2984-3024`), but the **frame-boundary safety close does not**. `CoronaWindow_Close()` (`main.cpp:3078-3098`) restores only `ALPHAFUNC`, `ALPHAREF`, `ALPHABLENDENABLE`. Its own comment says it is the net for the case where "the deferred flush never came" — exactly the case that leaves ZWRITE=FALSE. The 111 cache-level receipts prove this path ran for the whole run; the missing `[CoronaResync]` is what leaves the alternative unverified.

Perpetuator, not generator: the building exit restores each layer to its own captured value (`buildingPipe.cpp:3067-3070`), so once the split exists it is faithfully re-created every callback.

### #2 — Shadow contract pre-entry drift (212 receipts; device vs rw)

Verbatim:

```
[22:35:53.425] ... [shadow_contract] static pre-entry layer drift: drift=2 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=0 fog=1)
[22:36:38.454] ... [shadow_contract] extra pre-entry layer drift: drift=4 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=0) dev(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
```

Composition over all 212 receipts: dev blend ≠ rw **161**, dev FOG ≠ rw **97**, dev ALPHAREF ≠ rw **94**, dev ZWRITE ≠ rw **72**, drv blend ≠ rw **64**. `drv` is clean for FOG/ref/ZWRITE, so the disagreement is almost always **device vs both caches**.

Most likely raw writer: the stack of L3-only writers enumerated in `src/render/buildingPipe.cpp:476` and `src/core/main.cpp:1662`: `postfx` raw render-state writes, `DepthHook` raw ZENABLE, Im2D passes, and the corona shield. The concrete propagation engine is **`src/render/postfx_utils.cpp:190` (`SaveRawGeomStates`) + `:246` (`RestoreRawGeomStates`)** — it captures the **device** and then park-and-sets that captured value into L1 and L2, so any device-only drift at save time is promoted into the caches. Also relevant: the stored-shadow pass is unwrapped (§2), so it can both consume and leave drift with no receipt.

### #3 — Device-only `ZWRITE` loss (VEHICLE 74/74 + PED 63/64 = 138 receipts)

Verbatim:

```
[22:35:53.509] ... [VEHICLE] ZWRITE off/desynced on entry (rw=1 cache=1 dev=0) — forcing TRUE for cb
[22:42:44.581] ... [PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb
```

Caches say ON, device says OFF. Note `[PED]` does not read/print the driver cache at all (`main.cpp:1775-1777` reads only rw + dev), so the middle layer is unmeasured for the ped path.

Most likely raw writers, in order:
1. `src/core/main.cpp:2814` — draw-time sprite shield, L3 `ZWRITE=FALSE` (as in #1).
2. `src/core/main.cpp:4127` — corona entry shield, L3 `ZWRITE=FALSE` (the `ztestDrift` branch at 4130-4136 also touches L3 ZENABLE).
3. `src/render/postfx.cpp:3235` — raw device `ZWRITE=FALSE` on the SSAO/depth path.
4. `src/entities/chars.cpp:469` — raw device `ZWRITE=FALSE` on the SSS blur; **not active in this run** (`chars_drawSSSBlur bailing: sssEnable=0`), so this is exonerated here but will reproduce the fault whenever SSS is enabled.

Perpetuator: `src/render/vehiclePipe.cpp:3742-3743` restores `savedZWriteCache` / `savedZWriteDev` per layer, and `main.cpp:2082-2085` does the same for peds when the entry was *not* flagged desynced. Once the device is one step behind, every clean exit re-applies "device = 0" while the cache says 1.

### #4 — Sprite flush arrives in REPLACE (89/198 receipts; `blend=0`)

Verbatim:

```
[22:35:52.719] ... [CoronaDraw] pre-force dev blend=0 src=5 dst=6 at SpriteFlush_70CF20 (D3DBLEND: 2=ONE 5=SRCALPHA 6=INVSRCALPHA; black-quad=blend 0)
[22:40:08.xxx] ... [CoronaDraw] pre-force dev blend=0 src=5 dst=6 at SpriteFlush_70CF20 ...
```

`D3DRS_ALPHABLENDENABLE=0` + `SRC=SRCALPHA/DST=INVSRCALPHA` is **REPLACE**: the quad's RGB is written straight to the target. For a glow sprite whose RGB is near zero at the border, that is the classic black square. 89 of 198 sampled flush boundaries (45%) were in this state before the shield; the other 109 were blend-on but still normal-alpha (never additive).

Corroborating state right after the corona body:

```
[22:35:41.047] ... [CoronaBlend] dev blend=1 src=2 dst=2 ...
[22:42:46.262] ... [CoronaBlend] dev blend=1 src=2 dst=2 ...
```

144× ONE/ONE, 68× SRCALPHA/ONE — the corona body always leaves an **additive** device state behind. `[CoronaExit]` shows what it inherited: `pre-window blend=0 vtxalpha=0` (blending was off when the pass started).

Most likely raw writer: whatever disables blending before the flush. The code path that most plausibly does this with a raw L3 write is `src/render/mirrorPass.cpp` (raw-geom save/restore + `RwIm2DRenderIndexedPrimitive` at `:272`), plus `postfx.cpp`'s Im2D blocks (`:734`, `:876`, `:3909`, `:4142`, `:4392` all raw-set `ALPHATESTENABLE=FALSE`, and `:748` sets `BLENDOP=REVSUBTRACT`). The exact writer is **not provable from this log** because `[CoronaDraw]` does not print the flush owner (`g_inCoronasRender` / `g_inCloudsRender` / CPointLights) or `batchCount`.

Mitigation in place: `SpriteFlushIndexedDraw_hook` forces `blend=TRUE, src=SRCALPHA, dst=ONE` for every non-empty batch (`main.cpp:2925-2977`), which is why there are no black-quad complaints in the state itself — only the pre-force evidence.

### #5 — `[PedRS] DIVERGED` (27/157 sampled ped exits) + `[PipeAlpha] entry resync` (111 receipts)

Verbatim:

```
[22:36:45.034] ... [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:38:26.xxx] ... [PedRS] DIVERGED ... afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=1
```

Breakdown of the 27: `blend dev=0` 21×, `aref dev=1` (instead of 100) 3×, `aref dev=0` 1×, `blend+afunc` combos 2×. **Cull, src, dst, zw are never wrong here** — the ped RS monitor says the only lost states are ALPHABLENDENABLE and ALPHAREF.

`[PipeAlpha] entry resync` shows the same direction, one layer earlier: 81/111 are `vtx=1` (device blend ON while the entry canonical is OFF), 30 are `vtx=0`, and one is the first-frame garbage `op=4294967295 test=0` at 22:35:40.945.

Most likely raw writer: the L3 blend writers whose exits are device-only or missing — `src/core/main.cpp:2786-2791` (`coronaDrawRawShield`), `main.cpp:4103-4108` (corona entry shield), `src/render/buildingPipe.cpp:929` (`bldBlendLeakProbe` repair, L1+L2+L3), `src/render/vehiclePipe.cpp:716/1817` (FX additive leg). The `aref dev=1` variant is a *leftover canonical corona value*: `main.cpp:2824-2831` raw-sets `ALPHAREF=0`/`ALPHAFUNC=GREATEREQUAL` per draw, and the ped contract's canonical is ref=1 — so a skipped/partial corona exit is the shape of the evidence.

---

## 6. Frame pacing

Derived from `[PostFX-REPORT] frame=N` (56 samples, 55 windows). The frame counter is the game's frame counter and is consistent with `rs_mon` sample counts (2 samples/frame ⇒ 96,547/2 ≈ 48.3k).

| metric | value |
|---|---|
| first / last | `frame=2550` @ 22:35:53.021 → `frame=46934` @ 22:42:46.296 |
| total frames / span | 44,384 / 413.28 s |
| mean | **107.4 fps** |
| median window | **75.6 fps** |
| min window | **32.5 fps** (22:36:13.021→22:36:17.020, 130 frames / 4.00 s) |
| max window | **231.3 fps** (22:42:22.220→22:42:24.222) |

Pacing is bimodal: ~45-75 fps during the first two minutes (loading/streaming, Xbox#1 + mode cycling) and 150-230 fps during the PS2 middle and the final Xbox epoch. The 22:36:13-17 dip is the worst sustained stutter and it coincides with a `[PedOpaque]` repair at 22:36:13.278 and a 1.12 s inter-line gap 22:36:13.278→22:36:14.402 — suggestive, but the log has no per-frame timings to prove causality.

**Multi-second stalls** (all gaps > 0.35 s between consecutive log lines):

| gap | window | context |
|---:|---|---|
| 5.44 s | 22:35:33.747→22:35:39.184 | `InitialiseGame_hook: original returned successfully` — startup, not gameplay |
| 4.75 s | 22:35:28.160→22:35:32.909 | first `InitialiseGame_hook` — startup |
| 2.41 s | 22:35:25.705→22:35:28.115 | env-map pipeline init — startup |
| 2.04 s | 22:35:23.180→22:35:25.217 | `Initialise_skygfx entered` — startup |
| 1.75 s | 22:35:39.186→22:35:40.936 | first frame after init (shader/tex upload) |
| 1.44 s | 22:35:41.493→22:35:42.933 | second frame (mirror/raster init) |
| **1.25 s** | **22:41:45.905→22:41:47.155** | **immediately after `[Mode] apply Xbox`** — the only in-gameplay stall at a mode switch |
| 1.24 s | 22:41:56.300→22:41:57.543 | gameplay (next-window unknown) |
| 1.21 s | 22:42:02.982→22:42:04.194 | gameplay |
| 1.16 s | 22:42:46.474→22:42:47.637 | shutdown (`rs_mon_report` then `texdb shutdown`) |

**No stall coincides with a state-fault onset.** Every state fault in §3 is already active before and after these gaps. The one stall that coincides with anything is the 1.25 s hitch at the final Xbox re-apply, and it immediately precedes a *disappearance* of the PS2-only ztest fight, not a worsening.

---

## 7. Ped path: could geometry be invisible depending on view?

Short answer: **not in any sampled state.** Every ped-path receipt shows cull=BACK, ZWRITE on in both caches, ALPHATEST on, ALPHAREF in 1..255. The faults that do exist would make peds *wrong*, not *absent*.

| state | observed on ped path | would it hide geometry? |
|---|---|---|
| CULLMODE | `rw=drv=dev=2` (BACK/CW) in 156/157 `[PedRS]` receipts; one `rw=drv=dev=1` (NONE, 22:35:40.970); `[Cull] enter saved=1/forced=1` 19 receipts | No. NONE is double-sided (more visible); FRONT never observed. |
| ZWRITE (device) | 0 at 22:35:40.947 (rw also 0) and at 27 `[PedOpaque]` receipts (rw=1); 63 `[PED] ZWRITE` receipts (`rw=1 dev=0`) | No. Missing depth write means the ped does not occlude later draws; it does not self-erase. |
| ALPHATESTENABLE (device) | **0** at 22:35:40.947 and 22:36:00.327 (17 `[PedOpaque]` receipts total); 1 everywhere else in `[PedRS]` (not printed — inferred from `atest=1` in the contract line) | No. Test off = more fragments pass. |
| ZFUNC (device) | **8 = D3DCMP_ALWAYS** at 22:35:40.947 and 22:36:00.327; **4 = LESSEQUAL** in all other contract receipts | No. ALWAYS disables depth rejection. |
| ALPHAREF | 1, 2, 100. Never 0 at the draw; 33 `[PipeAlpha] entry resync` receipts show `rw ref=0` at entry, all repaired to ≥1 | No. ref 0/1 with GREATEREQUAL accepts everything; ref 100 with GREATER is the normal ped cutout. |
| ALPHAFUNC | 5 (GREATER) or 7 (GREATEREQUAL) in `[PedRS]`; device 5 in all but the two `atest=0` frames; dev 0/1 never observed | No. NEVER(1) would erase everything; it never appears. |
| BLEND | device blend=0 while cache=1 (21 receipts) | No. Opaque, not invisible. |

The only state that could plausibly erase a ped is `ALPHATESTENABLE=1 + ALPHAFUNC=NEVER` or a ZFUNC that rejects everything — neither is present. **Therefore the "peds only visible at some angles" report is not explained by anything this log records.** The likely explanations are outside the tagged state set: per-atomic shader/texture bind, the ped opaque contract forcing blend off on alpha-blended ped parts, or a camera/view-dependent cull or z-fight that the current probes cannot see.

Two blind spots make this conclusion weaker than it sounds:

1. `[PedRS]` samples at most once per 2 s bucket (157 samples over 424 s) and has a 113 s hole (22:39:12→22:41:05).
2. `[PedRS]` does not print ALPHATESTENABLE or ZFUNC, and `[PED]` does not print the driver-cache ZWRITE.

Also relevant: the ped classifier is not seeing alpha at all in the final epoch — `[PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=048C6410` 10× between 22:41:49 and 22:42:46, and `texalpha: ... 152749 unknown` at 22:42:46.339. If a ped texture does have alpha cutouts, the dual pass is skipped and those polys are drawn once, opaque.

---

## 8. What to instrument next

1. **Fix the throttle buckets.** 316 tags → 163 buckets, and the important ones collide (`cull_enter`/`fx_alpha`, `corona_resync`/`postfx_after`, `PedZWrite`/`fp`/`radiosity`, `shadow_contract_exit`/`pipeline_alpha_exit`). Options: 1024 buckets, or a linear-probed per-tag map in `skygfx.h:89`. Until then, every count in this report is a lower bound and several expected lines are missing for hash reasons, not code reasons.
2. **Give `CoronaWindow_Close` the full restore.** `main.cpp:3078-3098` restores only ALPHAFUNC/ALPHAREF/ALPHABLENDENABLE. Add ZWRITE, ZTEST, SRCBLEND, DESTBLEND, BLENDOP, and emit an unthrottled (or `dbglog_once`) receipt on the frame-boundary path. That single line would confirm or kill fault #1 and #3.
3. **Log the flush owner.** `[CoronaDraw]` currently prints only `at SpriteFlush_70CF20`. Add `g_inCoronasRender`, `g_inCloudsRender`, `batchCount`, and the pre-force `zwrite`. Then a `blend=0` receipt names its producer instead of its call site.
4. **Move `rs_mon` inside the pipes.** It samples only at `scene_start` (`main.cpp:3329`) and `frame_end` (`main.cpp:7226`), which is why it reports `samples=96547 agree=96547 diverge=0 pct=0` while 27 `[PedRS] DIVERGED` and 111 `[PipeAlpha] entry resync` receipts exist. Sample at every pipe entry/exit and at the PostFX boundaries; the monitor is currently measuring the only two moments where the state is guaranteed clean.
5. **Add camera pose and frame number to every divergence line.** There is no camera position or angle in the log at all, so "random angles" cannot be correlated. A rotating `[CAM] pos=(...) yaw=... pitch=... frame=...` heartbeat at 2 s costs one line per window.
6. **Add `ALPHATESTENABLE` and `ZFUNC` to `[PedRS]`, and the driver-cache ZWRITE to `[PED]`.** These are the two states that can erase geometry and the one layer the ped path never reads.
7. **Instrument the stored-shadow pass or record its absence.** `CShadows::RenderStoredShadows` was skipped at install (`0x53E0C3 MISMATCH`). Emit a `dbglog_once("shadow_stored_unwrapped")` when the hook install fails so every log states that one of the three shadow passes is unguarded.
8. **Log `CoronaResync` unconditionally on the first close** (`dbglog_once`), in addition to the throttled tag. Right now the canonical world resync is the only repair that has zero receipts in a 7-minute run.
9. **Expose the alpha-classifier coverage.** `texalpha` ends at `152749 unknown` of ~181k probes (85%). Either raise `unknown_budget` for one session or log a histogram of the unknown reason; a classifier that is 85% blind cannot support the ped-opacity decisions it is feeding (`[PED] dual pass skipped: ... hasAlpha=0`).
10. **Log `pf_gate` at least once.** `PostfxGateSummary` (`postfx.cpp:1377`) never emitted. Add a `dbglog_once("pf_gate_first")` next to it so a silent gate summary is distinguishable from a non-invoked one.

---

## 9. Bottom line

- The build is the expected one (`size=2360832`, `built=Oct 25 2026 21:56:44`); `prev.log` is a different, older PBR binary and a different session.
- The run is a **mode-cycling regression session**, not PBR gameplay. 4 m 13 s of it is PS2.
- The dominant defect is a **device-vs-cache ZWRITE/ALPHABLEND split** that is re-created every frame. It is not transient: 111/115 building receipts see the **rw cache itself** with ZWRITE off, and 138 vehicle+ped receipts see the device one step behind the caches.
- The code path that can produce cache-level ZWRITE=FALSE is the corona sprite shield (`main.cpp:2812-2814`), and the path that fails to undo it is the frame-boundary close (`main.cpp:3078-3098`). That is a concrete, testable change: restore ZWRITE in `CoronaWindow_Close` and watch `[BUILDING] ZWRITE (rw=0 ...)` disappear.
- The only black-screen mechanism in the log is the **mirror raster (201DD090, 2048×1024) live during world geometry** at 22:35:40-50 while the screen is 1768×992. It is startup-only and does not repeat later.
- The ped path never shows a state that would make geometry invisible. Cull is BACK, ALPHATEST is on, ZFUNC is LESSEQUAL, ALPHAREF is 1..255 in every sampled window. The "peds only visible at some angles" symptom needs new probes (camera pose, per-atomic shader bind, ALPHATEST/ZFUNC in `[PedRS]`), not more of the current tags.
- `rs_mon` is not a usable witness: 96,547/96,547 samples "agree" because it only samples at scene start and frame end.
</content>
