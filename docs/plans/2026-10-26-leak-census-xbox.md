# 2026-10-26 — World-render leak census (Xbox config)

**Build:** 2,395,136 bytes, `skygfx.asi` mtime 2026-10-26 01:11:54, built 01:11:06
**Run:** `skygfx_dbg.log` `[01:12:26.118]` → `[01:19:31.145]`, lines 1–30,661
**Config:** `pipeline=2` (Xbox), `buildingPipe=Xbox(1)`, `vehiclePipe=CAR_XBOX(2)`, `colorFilterEnable=0`
**Scope:** world render path only. PostFX is disabled in this run (`SSAO=0`, `SMAA=0`, `PipeChain=0`, `SSS_Blend` bails, `Radiosity=0`), so postfx rows are receipts only.

---

## 1. Log census (normalised, exact-tag counts)

`ZWRITE` substring count is **91**; **90** are `ZWRITE off/desynced on entry` receipts and **1** is the `[PedOpaque] pre-entry` diagnostic that happens to contain `ZWRITE=`.

| Tag | Count | Normalised receipt / triplet(s) | Entry point named | Layer that disagrees | Verdict |
|---|---:|---|---|---|---|
| `[BUILDING]` ZWRITE | **47** | 46× `(rw=0 drv=0 dev=0)`; 1× `(rw=1 drv=1 dev=0)` | `buildingPipe` Switch entry (`CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch`) | 46× no layer differs, but cache truth itself is OFF; 1× L3 only | **Dominant class.** The Switch restores the pre-entry state faithfully, so all-zero is re-created every callback. |
| `[PED]` ZWRITE | **26** | 25× `(rw=1 dev=0)`; 1× `(rw=0 dev=0)` at first ped 01:13:02.920 | `myDefaultCallback` ped entry | L3 raw device vs L1 cache (25×); all-off init (1×) | Device-only drift every frame; sticky repair writes all three TRUE at exit, but the next frame re-detects it. |
| `[VEHICLE]` ZWRITE | **17** | 17× `(rw=1 cache=1 dev=0)` | `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch` entry | L3 raw device vs L1+L2 | Device-only drift; the Switch restores each layer to its own pre-entry value, so it propagates the drift. |
| `[PED]` dual pass skipped | **51** | 51× `zwrite=1 hasAlpha=0 pipe=04C26410` | default-pipe branch of `myDefaultCallback` | none | Informational: non-ped default atomics skip the dual pass. Not a leak. |
| `[BUILDING]` cb camRas | **18** | 18× `camRas=1F361870 2048x1024 != screen 1920x1080` | Xbox building cb draw boundary | camera raster, not a render state | Same pointer as `[MirrorPass] pBuffer=1F361870`; this is the mirror sub-pass rendering the world, not a stage-0 texture leak. |
| `[BUILDING]` alpha/blend drift | **1** | `mask=0x004` (ALPHAFUNC); `rw fn=7 -> d3d want=7 dev d3d=8` | building cb exit repair | L3 ALPHAFUNC (`ALWAYS` vs `GREATEREQUAL`) | Real L3 repair, but not ZWRITE. |
| `[FxAlpha]` PostFx.repaired | **105** | `rw(vtx=0 src=5 dst=6 zw=1) dev(vtx=0 src=5 dst=6 zw=1)` | postfx exit sweep | none | Postfx excluded; its ZWRITE is consistently 1. |
| `[FxAlpha]` PostFx.exit | **25** | `additive ONE/ONE leak substituted with canonical normal-alpha — rw(src=2 dst=2) d3d(src=2 dst=2)` | postfx exit | blend only | Postfx excluded. |
| `[FxAlpha]` Coronas.Reflections | **4** | `rw/d3d vtx=1`, `dev vtx=0`; one sample `dev src=10 dst=1` | `effectsForceAlphaBlock("Coronas.Reflections")` | L3 blend state | Same device-only pattern as the pipe entries, but on blend/vtxAlpha, not ZWRITE. |
| `[FxAlpha]` Coronas.SunReflection | **1** | `rw/d3d vtx=0`, `dev vtx=1` | `effectsForceAlphaBlock("Coronas.SunReflection")` | L3 blend state (opposite direction) | Effects-phase; excluded from world verdict. |
| `[FxAlpha]` camRas | **2** | `camRas=1F361870 2048x1024 != 1920x1080` | Coronas reflection probe | camera raster | Mirror pBuffer again. |
| `[Cull]` enter | **71** | 59× `saved=2 forced=2`; 12× `saved=1 forced=1` | `pipeEnterCullMode` call sites | none | Instrumentation. `[Pipeline]` enter/exit pairs are 104/104, all `0x1`/`0x2`; no cull leak. |
| `[Cull]` exit | **87** | 87× `restored=1` | `pipeExitCullMode` call sites | none observed | Count is throttle-sampled, not a pair count; no out-of-domain value (`0`/`NONE`) appears. |
| `[Cull]` scope/baseline | **2** | `scope end re-assert=2 depth=0`; `baseline BACK` | mirror scope / baseline | none | Instrumentation. |
| `rs_mon` reports | **44** (+1 init) | last: `samples=59390 agree=59390 diverge=0 pct=0 bad=0 sites=0 top=(none)` | `RSMon_Sample("scene_start")` / `("frame_end")` | **none** | Monitor is blind to the entry-point drift: it samples only at scene start and frame end, after `CoronaWindow_Close()`/`pipeApplyWorldBaseline`. `sites=0` confirms it never saw a divergence. |
| `[CloudShield]` | **2** | `flush batch=sprites forced SRCALPHA/ONE` (01:13:15.246); `flush batch=clouds forced ONE/ONE` (01:13:40.044) | sprite-flush shield | blend pair | These are **blend-force receipts**, not stage-0 restores. |
| `[PedTex]` | **1** | `stage0 resync cached=00000000 dev=00000000` | `myDefaultCallback` ped block | none | Cache and device both NULL at the one sample; the new `pedStage0Resync` had nothing to resync. |
| `[CoronaExit]` | **2** | `restored normal-alpha ... zwrite=1 ... pre-window ... zw=1`; `frame-boundary close ... zwrite=1` | `CCoronas__Render` exit / `CoronaWindow_Close` | none | Both show `zwrite=1`; **no `zwrite=0` corona exit** in this run. |

### Direct answers

- **Are ZWRITE desyncs still `rw=0 drv=0 dev=0`, or did they become `rw=1 dev=0`?**
  **Both, split by pipe class.** 46/47 `[BUILDING]` receipts are still cache-truth-off (`rw=0 drv=0 dev=0`); 42/43 `[PED]`/`[VEHICLE]` receipts are device-only (`rw=1 dev=0`, with L2 also 1 for vehicles). One `[BUILDING]` entry at 01:14:04.168 is `rw=1 drv=1 dev=0`. The first ped at 01:13:02.920 is the initial all-off state (`rw=0 dev=0`).
- **Is `[BUILDING]` still the dominant class, and at what point in the frame?**
  Yes: 47/90 ZWRITE receipts (52%). The first all-zero receipt is 01:13:40.061, immediately after `[CloudShield] flush batch=clouds forced ONE/ONE` at 01:13:40.044, during the mid-frame world-geometry span (Xbox building Switch entry); the `EffectsExit` baseline at 01:13:40.998 later heals it, and it recurs. `[CoronaDraw]` receipts are also interleaved between XboxBuilding draws (e.g. 01:14:04.049), so the sprite-flush shield fires inside the world pass, not only in the effects phase.
- **Did `[CloudShield] restored stage-0` fire (2 occurrences) and with what values?**
  **No.** The two `[CloudShield]` lines are `flush batch=sprites forced SRCALPHA/ONE` and `flush batch=clouds forced ONE/ONE`; they are blend-force receipts. The stage-0 restore receipt (`dbglog_once("cloud_texraster")`, `[CloudShield] restored stage-0 raster=%p dev=%p`) has **0 hits** because the captured `savedTexRaster` was NULL or `gpWhiteTexture` (the log is gated on `savedTexRaster && savedTexRaster != whiteRas`). No values to report, and no evidence of a non-white cloud atlas left on stage 0.
- **Did `[PedTex]` fire and with what cached/dev pointers?**
  Once: `cached=00000000 dev=00000000`. Both NULL; no cache/device split at that sample.
- **Any `[CoronaExit]` with `zwrite=0`?**
  No. Both receipts report `zwrite=1`; the frame-boundary close restores `zw=1`.

---

## 2. ZWRITE writer audit

Legend: L1 = rw cache, L2 = `RwD3D9SetRenderState` driver cache, L3 = raw `IDirect3DDevice9::SetRenderState`.

| Writer | File:line | Sets ZWRITE to | Restores L1? | Restores L2? | Restores L3? | Where / notes |
|---|---|---|---|---|---|---|
| Corona per-draw shield `coronaDrawRawShield` | `src/core/main.cpp:2811`, writes 2854–2858 | park TRUE → FALSE | no | no | no | Deliberately no save; ownership is with the exit/close. Called before every corona/pointlight/cloud sprite draw. |
| `CoronaWindow_Close` | `src/core/main.cpp:3122`, capture 3166–3181 | park inverse → `preZw` | yes | yes | yes | **All three written, but from one device-only `g_coronaPreZWrite` capture (`main.cpp:4160`).** If the device was already 0 at window entry, this writes 0 to L1+L2+L3 → cache-truth-off. Clears `g_coronaPreValid` at 3192. |
| CCoronas normal-alpha exit | `src/core/main.cpp:4275–4330`, writes 4310/4317 | TRUE (canonical) | yes | yes | **NO** | The comment says raw L3 is deliberately omitted. This is the only active writer that leaves L1/L2 TRUE and L3 untouched → the `[PED] rw=1 dev=0` / `[VEHICLE] rw=1 cache=1 dev=0` signature. |
| `shadowContractForce` / `Restore` | `src/core/main.cpp:1345` / `1386` | FALSE at force; per-layer captured at restore | yes (1393) | yes (1404) | yes (1425; heal 1447) | Fully per-layer. No ZWRITE receipt in this log; drift lines are blend/alpha only. |
| `myDefaultCallback` ped entry/exit | `src/core/main.cpp:1811–1830`, exit 2113–2131 | TRUE at entry | yes | yes | yes | Sticky exit writes TRUE to all three when entry was `rw=0` or `dev=0`; otherwise per-layer. Note: `zwriteWasDesync` ignores L2, a latent gap. |
| `buildingPipe` Switch | `src/render/buildingPipe.cpp:3023–3055`, restore 3123–3126 | TRUE at entry | yes | yes | yes | **Faithful per-layer restore of the pre-entry trio.** With `rw=0 drv=0 dev=0` at entry it restores 0/0/0, so the all-zero state survives every callback. No sticky/heal. |
| `vehiclePipe` Switch | `src/render/vehiclePipe.cpp:3601–3625`, restore 3744–3746 | TRUE at entry | yes | yes | yes | Faithful per-layer restore; propagates `rw=1 cache=1 dev=0`. |
| Xbox vehicle cb (inner) | `src/render/vehiclePipe.cpp:1472`, restore 1692 | captured L1 only | yes | no | no | Restores only the rw cache; relies on the outer Switch for L2/L3. |
| `D3D9RenderDual` / `setZWriteTwoLayer` | `src/core/main.cpp:243–249`, call 252–300 | TRUE, FALSE, saved `zwrite` | yes | yes | yes | `setZWriteTwoLayer` writes all three; the final restore uses the **L1** `zwrite` value for all three, not a per-layer capture. |
| `PipeChain_ClassifyBegin/End` | `src/render/postfx.cpp:3347` / `3495`, writes 3435–3441 / 3512–3526 | FALSE at Begin | yes (3512) | yes (3515) | yes (3526) | Restores L1 from `s_classifyZWrite`, L2/L3 from `s_classifyZWriteDev`; L2 is not captured separately. **Config-disabled (`PipeChain=0`).** |
| `mirrorPass` composite | `src/render/mirrorPass.cpp:210`, 259, restore 307 | FALSE | yes | yes | yes | `MirrorRestoreState3L` is the reference three-layer park-and-set. Capture is L1-only. **Composite disabled (`mirrorDistortionFix=0`).** |
| `waterPipe` | `src/render/waterPipe.cpp:1080–1087`, set 1582–1588, restore 1742–1748 | FALSE | yes | yes | yes | Per-layer capture and restore. **Disabled (`waterParallaxEnable=0`).** |
| `CSkidmarks__Render` | `src/core/main.cpp:3311–3345` | no ZWRITE write | — | — | — | Only ALPHAFUNC/pipeForceAlphaBlock; ZWRITE is not touched. No log hits. |
| `effectsForceAlphaBlock` | `src/core/main.cpp:3630` | no ZWRITE write | — | — | — | Alpha/blend/fog repair only; no ZWRITE. |
| `pipeApplyWorldBaseline` | `src/render/pipelinecommon.cpp:1975–2048` | TRUE | yes (1997–1998) | yes (2015–2016) | yes (2042) | Canonical baseline, no restore by design; runs at frame boundaries. |
| `pipeEnterAlphaMode` / `pipeExitAlphaMode` | `src/render/pipelinecommon.cpp:1523` / `1747` | no ZWRITE write | — | — | — | Alpha/blend only; ZWRITE is outside this pair. |
| `vehiclePipe_fxAdditiveBlend` / leeds | `src/render/vehiclePipe.cpp:705–750` / `1847–1888` | FALSE, then **hardcoded TRUE** | yes | yes | yes | Restores TRUE even if pre-entry was FALSE; does not capture the original ZWRITE. Not the 1/1/0 source, but a fidelity gap. |
| Building per-mesh decal (Xbox/PBR) | `src/render/buildingPipe.cpp:1926–1941`, `2686–2701` | FALSE then TRUE | yes | yes | yes | Balanced inside the mesh loop; the final cb `buildingPipe_restoreRenderState` at 450 restores **L1 only**, then the Switch restores all three. |

### Net ZWRITE map

- **Cache-truth-off (`0/0/0`) producer:** the sprite-flush shield leaves all three layers at FALSE for non-body batches; `buildingPipe` Switch then faithfully restores `0/0/0`. `CoronaWindow_Close` can also convert a device-only 0 into `0/0/0` because it uses a single device capture for all three layers.
- **Device-only (`1/1/0`) producer:** the CCoronas normal-alpha exit writes ZWRITE only L1+L2 (raw L3 omitted), and the Xbox vehicle inner cb restores only L1. The building/vehicle Switches then faithfully restore the split instead of healing it (the ped path is the only sticky one).

---

## 3. Texture-raster (`rwRENDERSTATETEXTURERASTER` / raw `SetTexture(0)`) audit

| Writer | File:line | What it binds | Restores previous? | Notes |
|---|---|---|---|---|
| Building stage-0 bind | `src/skygfx.h:1272` (`pipeSetTexture`), `src/render/buildingPipe.cpp:1162–1173`, called 1844/2586 | material texture via `RwD3D9SetTexture` | **No** | `buildingPipe_cleanup` (`buildingPipe.cpp:371–388`) resets stage 1/2 to white and disables stage ops, but **never restores stage 0**. The last building material texture is handed to the next consumer. |
| Vehicle stage-0 bind | `src/render/vehiclePipe.cpp:2261` (`vehEnvBindTexture` → `pipeSetTexture`), called per material | diffuse/reflection | **No** | `vehiclePipe_cleanup` (`vehiclePipe.cpp:632–660`) unbinds stages 1–8, but **not stage 0**. The last vehicle texture is left bound. |
| Ped stage-0 resync | `src/core/main.cpp:1582–1616` | cache raster (park → target) + raw D3D texture | resyncs cache/device, not a pre-cb capture | At the one sample: `cached=00000000 dev=00000000`; no texture leak. |
| Cloud stage-0 save/restore | `src/core/main.cpp:4762–4770` save, `4785–4794` pass-through restore, `5005–5023` full restore | cloud/sprite atlas, then entry raster | **Yes (L1 + raw L3)** | No L2 texture-cache capture. The `[CloudShield] restored stage-0` receipt did not fire because the captured raster was NULL/white. |
| Mirror composite stage 0 | `src/render/mirrorPass.cpp:210`, 259, 290 | `CMirrors::pBuffer` raster | **Yes (L1)** | Park/set, `__finally` restore. Raw texture follows RW. **Disabled composite.** |
| Mirror pass camera raster | `mirrorPass.cpp` game original; logs `pBuffer=1F361870` | `Scene.camera` raster = 2048×1024 pBuffer | restored later (UpdateFrontBuffer shows 1920×1080) | This is the 18 `[BUILDING] camRas` hits. It is a **camera-raster** leak, not `rwRENDERSTATETEXTURERASTER`. |
| Water raw stage 0 | `src/render/waterPipe.cpp:1598–1599`, restore 1706–1710 | `g_sceneTexture` / NULL | **No** — unbinds to NULL | Does not restore the previous s0-s3; leaves NULL. **Disabled (`waterParallaxEnable=0`).** |
| Grass stage 0 | `src/render/PC_GrassInstancer.cpp:183` | batch texture | **No** | Disabled (`grassInstancing=0`). |
| Envmap corona bind | `src/render/envmap.cpp:683`, 725, 760 | corona texture | **Yes (L1)** | Save/set/restore around the reflection pass. |
| PostFX SSAO/SMAA/PipeChain/SSS | `postfx_ssao.cpp`, `postfx_smaa.cpp`, `postfx.cpp`, `chars.cpp` | RT/edge/scene buffers | Partial (known) | **Disabled in this config** (`SSAO=0`, `SMAA=0`, `PipeChain=0`, `SSS` bails). Not the source here. |
| Forward+ debug overlay | `src/render/forwardplus.cpp:1879` | `SetTexture(0,NULL)` | No capture | Debug-gated; `forwardPlusDebug=0`. |
| CSkidmarks / effectsForceAlphaBlock / pipeApplyWorldBaseline | — | no texture binds | — | Not texture writers. |

### Net texture map

- No stage-0 **render-target** leak is evidenced in this run: `[PedTex]` is `NULL/NULL`, the cloud restore receipt did not fire (captured raster was NULL/white), and the known SSAO/SMAA/PipeChain/SSS leakers are disabled.
- The surviving stage-0 issue is a **handoff**, not a desync: building and vehicle callbacks leave their last material texture bound (no pre-cb save/restore of stage 0). This is low-impact while every following world consumer rebinds, but it is the only live unrestored `rwRENDERSTATETEXTURERASTER`/`SetTexture(0)` path in this config.
- The 2048×1024 raster seen 18 times during building callbacks is the **mirror pass pBuffer as `Scene.camera` raster**, not a texture-raster leak. It is the same pointer as `[MirrorPass] pBuffer=1F361870`.

---

## 4. Verdict

### Highest-volume leak — sprite-flush shield leaves ZWRITE FALSE

**Evidence:** 46 of 47 `[BUILDING]` ZWRITE receipts are `(rw=0 drv=0 dev=0)`, and the first occurs at **01:13:40.061**, immediately after `[CloudShield] flush batch=clouds forced ONE/ONE` at **01:13:40.044**. `[CoronaDraw]` is interleaved with XboxBuilding draws (e.g. **01:14:04.049**), proving the sprite-flush shield fires inside the world pass. `[CoronaResync]` has **0 hits**, so the canonical post-flush close never ran.

**Exact patch:** `src/core/main.cpp:3028` — `if(g_coronaBodyDone){` gates the only all-three-layer ZWRITE resync. The shield at 3009/3012 runs for **every** non-empty sprite batch (corona, pointlight, clouds), but the close only runs when a corona/pointlight body armed `g_coronaBodyDone`. Clouds and mid-world deferred batches therefore leave all three layers at FALSE.

**Fix shape:** capture the ZWRITE trio immediately before the force at 3009/3012 and restore it from capture after `SpriteFlushIndexedDraw_orig` on every path (park-and-set for L1/L2, raw L3 last). If a capture is unwanted, run the existing 3048–3061 canonical resync for every forced batch instead of only `g_coronaBodyDone`; do not use the device-only `g_coronaPreZWrite` for this.

### Second leak — corona exit writes L1/L2 ZWRITE but not L3

**Evidence:** 25 `[PED]` `(rw=1 dev=0)` + 17 `[VEHICLE]` `(rw=1 cache=1 dev=0)` = 42 device-only receipts. The CCoronas normal-alpha exit at **`src/core/main.cpp:4310` / `4317`** writes `TRUE` through `RwRenderStateSet` (L1) and `RwD3D9SetRenderState` (L2), but deliberately has **no raw L3 write**. The `1/1/0` split is then faithfully restored by the building/vehicle Switches.

**Exact patch:** `src/core/main.cpp:4317` — add
`if(d3d9device) d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, g_coronaPreZWrite ? TRUE : FALSE);`
(or `TRUE` if the exit is meant to be canonical). Also make `CoronaWindow_Close` (`3166–3181`) capture ZWRITE **per layer** rather than reusing the single device-only `g_coronaPreZWrite`; otherwise a device-only 0 at window entry is promoted to cache truth off.

**Fix shape:** restore from a per-layer capture (L1/L2/L3), not a single device value; use park-and-set so a cache-hit cannot skip the transition.

### What not to chase in this config

- `rs_mon` is clean (`59,390/59,390`, `diverge=0`) because it samples only at `scene_start` and `frame_end`, after the baseline/close repairs. It does not contradict the entry receipts.
- The 18 `[BUILDING] camRas=2048x1024` hits are the mirror pass pBuffer, not a texture-raster leak.
- `[CloudShield]` has two hits, both blend-force receipts; the stage-0 restore receipt is absent because the captured raster was NULL/white.
