# Log Forensics — Test 2 (04:38–04:42, Oct 30 2026)

READ-ONLY analysis of `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (2,8771 lines, 2.9 MB).
The log was truncated at DLL load: **the entire file is this run**. No earlier runs present.

---

## 1. Run window & build identity

| Item | Value |
|---|---|
| Run start | `04:38:17.331` — `VEH handler + watchdog installed` |
| Run end | `04:42:20.070` — `DLL_DETACH complete` (+ `[Softbody] shutdown`, `[water_rt] release` at 04:42:20.062-069) |
| Build | `skygfx.asi size=2486784 mtime=2026-10-30 04:37:37 built=Oct 30 2026 04:36:47` |
| Build name | `corona-flush-shield` (CoronaBlend entry hook + flush-window close at SpriteFlushIndexedDraw_hook + corona_install verification) |
| Config at load | pipeline=2 (Xbox) from INI → `[Mode] engage Xbox pipes=2/1/2 cf=2` → `active=Xbox (from INI)` |
| Shader init | `[INIT] shaders: compiled OK (ps=60 vs=17), FAILED (ps=0 vs=0)` ×2 (second batch after device reset) |
| Device Reset | `04:38:35.593` — `[RENDER] camera-raster cache invalidated (04C7EFC0 1920x1080) — device reset / resolution change` → `DepthHook: lazy INTZ recreate 1920x1080 after Reset`; full CreateShaders re-run 04:38:35.6→40 |

### Pipeline flip (major event)

```
04:38:48.428  [Config] pipeline re-derived 2 -> 0 (buildingPipe=3 vehiclePipe=2)   ← Xbox → PBR
04:39:03.989  [Config] pipeline re-derived 0 -> 2 (buildingPipe=1 vehiclePipe=2)   ← back to Xbox
```

- **≈15.5 s / ~500 frames ran under PBR building pipe** (PBRBuilding cb receipts, `VS=IDR_BUILDINGPBRVS PS=IDR_BUILDINGPBRPS`, pbrUpload first-upload receipt OK via `pipeUploadPBR`: c22={0.900,0.560,0,0} c23={1,1,1,0}).
- During the PBR window: `ColourFilter_Modern ENTER … grading r=0.872 g=0.872 b=0.872 luma=0.218 tint=0.3` (16 receipts — MODERN CF ran).
- **Vehicles stayed Xbox-piped throughout** (`[VPIPE] cb=Xbox pipe=XBOX(2) VS=IDR_XBOXCARVS` inside the window) — mixed pipeline, legal for this build's mapping.
- **The FIRST `[water] flatpath` hit (04:38:55.897) fell inside the PBR window**, while the `[water]` shader chain only exists from 04:39:04 (after revert). Any water shot taken 04:38:48–04:39:03 shows pure-FFP water only.
- Cause of the flip not logged (no override/INI-change receipt); treat as user/hotkey re-derive.

Screenshot-relevant windows:
- **04:38:35.3–35.6** — one malformed pre-Reset frame (§ anomaly A1).
- **04:38:48–04:39:03** — PBR buildings + FFP-only water.
- **04:39:04 onward** — Xbox buildings + full dual water path (chain + FFP flatpath).
- **04:39:31–04:39:59** — vehicle texture-null corruption window (§ A2).
- **04:40:24–04:42:00** — water flatpath site=2 fires 6× with **s0=NULL**.
- **04:41:33.169** — splash underwater gate skip (uw=0.34), swim-level moment.

---

## 2. Per-diagnostic findings

### 2.1 `[water] flatpath` — FIRED, 97 receipts (2 s throttle)

- First hit: `04:38:55.897  site=1 v=80 i=120 staleVS=00000000 stalePS=00000000 s0=114C1B08 s3=00000000 -> FFP+vanilla raster`
- Site split: **site=1 ×91, site=2 ×6**.
- `staleVS`: 22C2F6E8 ×91 persistent; NULL ×6 (first hit + all-zero site=2 samples). `stalePS`: 22C2F940 ×69, 22DE2C58 ×22, NULL ×6. Stale handles are non-FFP leftovers only (FFP ignores them) — not a functional finding.
- **site=1 always binds s0=114C1B08** (vanilla water raster, deliberate rebind OK). **site=2 always has s0=00000000** — every site-2 flush drew **untextured FFP geometry** (renders white/grey depending on material/vertex color). s3=00000000 always.
- Site-2 first appearance `04:40:24.051 v=460 i=690`; biggest draw `04:41:32.082 v=2036 i=3054 staleVS=0 stalePS=0 s0=0` (all handles + tex NULL), fired right after `[Cull] exit restored=1`.
- Traffic counter monotonic: 1422 → **35397** (last receipt 04:42:18.256). Direct-flush hook is hot all session.

### 2.2 `wake_flatpath` — **NOT FIRED (0 receipts)**

- Only trace is the install line `04:38:17.395 — HOOK: WaterLevel ::RenderWakeSegment/::RenderBoatWakes inline flushes (2 sites)`.
- No first-hit, no per-flush receipts all session. Either the wake path never executed (no boats/wakes in view) or the hook sites are not reached. **Blind spot: any wake visuals are unevidenced** — including the white-wedge-at-swim-level symptom.

### 2.3 splashfoam — FIRED

- Install receipt: `04:38:17.392 — [splashfoam] fxSystem_c::Render hooked at call site 0x4A930A (orig=004AA160) - water-splash dst blend ONE->INVSRCALPHA + underwater draw gate`.
- Per-draw receipts: **113×** `splash system render: 1 prim(s) dst ONE->INVSRCALPHA for this draw` at ~2 s cadence from 04:38:35 to 04:42:18 — the blend fix applied on every sampled draw.
- Underwater draw gate fired **once**: `04:41:33.169 — underwater gate: splash system render skipped (uw=0.34)`.
- (No literal `splashfoam_fix`/`splashfoam_gate` tags exist in this build's log format; the receipts above are the equivalents.)

### 2.4 corona family — hooks verified; feed pipeline DEAD; shield working; exit restore silent

- Install: `corona_entry_install 0x006FAEC0 bytes match OK (tramp=03720000)`; `corona_install flush draw sites: 0x70CF62 OK | 0x70CF9B OK`; `CoronaBlend probe: entry hooked`.
- `[CoronaDraw]` 103 receipts at `SpriteFlush_70CF20`: pre-force dev **blend=1 ×93, blend=0 (REPLACE) ×10**.
- `[CoronaShield]` 112 receipts: `forced additive ONE/ONE` — 81 entered with blend=1 already on, **31 entered with blend=0** (shield repaired REPLACE windows).
- `[FlushShield]` restore receipt: **only 1 occurrence** (04:38:43.361, `restore path=SpriteFlush_70CF20/corona states=9 … captured REPLACE -> canonical normal-alpha`) — all other repair windows left no restore receipt (throttle or swallowed path).
- `[CoronaBlend]` 120 samples (2 s): 60× canonical shielded state `dev blend=1 src=2 dst=2 op=1 atest=1 func=7 ref=0`; **53/120 samples caught the device in `blend=0 src=5 dst=6` (REPLACE)** at sampled im2D draws (mostly atest=1 fn=5 ref=0). drvLTE/dev splits frequent: `drv blend=1 src=10 dst=1` ×36, `drv blend=1 src=5 dst=6` ×19, `drv blend=0` ×5 — the 3-layer state divergence is visible in the sampler throughout.
- **`[CoronaFeed]` 82 samples: `fed=0 regFed=0` in ALL.** rej constant `rej(id0=64 sz=0 rgb0=0 int0=0)`. **Pool counter decays monotonically 0 → −80730** (leak-shaped). The pipe-side corona feed never shipped a single corona this run.
- `[CoronaExit]`: only 2 receipts (04:38:35.446/.564) then **absent for the rest of the run** — the corona exit 3-layer restore path stopped emitting after the first two uses.

### 2.5 `grass_zfix` — **NOT FIRED (0 receipts)**

Case-insensitive `grass` → **zero matching lines in the entire log**. Not even a grass install receipt. Either the grass path never ran in this session (plausible: dark 10:36 in-game clock, gym interior area=3, IBL suppressed for interior) or the repair never logged. **Grass Z-contract repair is unevidenced.**

### 2.6 `[BldTex]` — 24 samples, no decal coverage

Texture census (one sample per ~2 s window): lw_seat2 ×8, sjmhoodlawn41 ×5, CJ_CHROME2 ×2, two ×1 each for AH_barpole1 / Metal1_128 / board64_law / fourlanefreeway / lampost_16clr / macbrij2_LAe / planta256 / sw_musicstore01_LOD / wallwashv128LOD.
- All samples: `null=0 blend=0 zw=1` except **sw_musicstore01_LOD: `blend=1 zw=0`** (04:39:01, glass/LOD — correct transparent+no-zwrite pattern).
- **`cleargraf02_LA` never sampled in BldTex** (appears only in texalpha receipts, §2.7); **`trafficlight_64`: zero appearances anywhere.** Decal swatch never caught — ZWRITE-off policy on decals is **unverified by log**.

### 2.7 `texalpha` — counters moved, classes sane, player parts misclassified

- Class receipts: CUTOUT ×10, FADED ×11, OPAQUE ×16 (throttled; samplerwise totals).
- Summary counter moved 0 → final: **`44257 opaque, 1149 cutout (281 aa), 2`** — cutout/aa counter climbed 0→281 ✔ (TAC is classifying on real histograms).
- CUTOUT examples: CJ_PLANT (zero 15459, mid 3522/10%), newtreeleavesb128 (19%).
- **FADED: `cleargraf02_LA zero=4733 full=0 mid=28035(85%) total=32768 blendOn=1 legacy=0`** (×2, 04:39:05 + 04:40:34) — decal classified FADED (85% mid-alpha) → blend-on path, no histogram cutout.
- ⚠ **Player skin parts at first ext registration (04:38:35.342-350): `torso, head, legs, feet, glasses` all `OPAQUE hist=none blendOn=0 legacy=0`** (`extOff=0x34 from 0xB4E9E0 tex='torso' raster=1DA986C8`). `hist=none` means no alpha histogram was collected for the player's body textures → they can never be CUTOUT → alpha-test/cutout shader decisions on CJ fall back to OPAQUE. Ties into CJ mannequin-type appearance (see §3.1).
- `hist=none` total = 5 lines — exactly the player parts; every other classified texture got a histogram.

### 2.8 `[Softbody]` probe — **absent (expected, INI says backend=off)**

- Load-time: `[PhysicsCfg] created E:\...\sky_physics.ini with defaults` then `[PhysicsCfg] loaded backend=off … budget=8` (04:38:17.356). 
- `softbodyBackend` was **off** in the INI this run read → frame-layout probe had no reason to run.
- Only receipt: `[Softbody] shutdown` at 04:42:20.069. **The native-backend probe receipt cannot exist for this run.** If the INI edit to native was applied before deploy, something overwrote/recreated the INI at load (see "created with defaults" line) — verify edit timing.

### 2.9 MotionBlurDtScale + camera CUT — **NOT FIRED, feature disabled**

- `MotionBlurDtScale` → 0 hits.
- `motionBlur=0` in **both** `[PF] pi_gate: … motionBlur=0` (every frame, ×112) and `[PostFX] ColourFilter_switch: filter=2 pipeline=… motionBlur=0` — the motion-blur switch is off in-session, so neither the dt fix nor its breadcrumb can execute.
- Camera CUT: `[BLIT] … cut=0` ×104, no `cut=1` anywhere, zero CUT-transition/reset receipts. Unused code path this session.

### 2.10 `[UW]` / `[water]` chain — healthy Xbox water pipe

- `[UW] water.dat: 307 planes (ok=1 bbox -3000..3000)` ✔; `[UW] Water_Underwater PS res=276 sh=…` twice (initial + post-Reset recreate). No further UW receipts (underwater pass running blind).
- `[water] entry` ×72 + `[water] chain` ×72, 2 s cadence from 04:39:04. First entry printed `sceneRT=00000000` (RT created 2 ms later: `[water_rt] create scene 1920x1080 pool=DEFAULT` + `create depth 1920x1080 pool=DEFAULT (R32F)`), delta only.
- Every chain: `style=Xbox waterStyle=0 tier=2 needRT=1 sceneRT=ok depthRT=ok **reflRT=none reflFlag=0** fresnelSplit=1 body=1590066272 VS c10.gate=1.0000 … flushVerts=(matches entry verts) ocean/shallow/river/lake split … nearClip=0.17→0.30 farClip=1652.4→1800 PS=22C2DFD8(IDR_WATER_PARALLAX) VS=22C2E0A0(IDR_WATER_VS) cullPre=0x01 cullSet=NONE`.
- Router stable (**IDR_WATER_PARALLAX**), VS/PS handles stable. **`reflRT=none reflFlag=0` everywhere — reflection pass never enabled; no [water] reflection receipts.**
- `[water] restore:` ×72 — every chain entry paired with the full unbind/restore snapshot (`VS/PS unbound, tex stages 0-3 cleared, samplers 0-3→linear/wrap, blend→SRCALPHA/INVSRCALPHA, vertexAlpha off, cull restore, fog on, zstate entry snapshot`).
- `[water_sun] style=0 tier=2 specPow=512.0 specInt=1.0 reflStr=0.550` (04:39:04.721). Reflection strength configured but reflector RT off.
- Dual-pass reality: **shader chain (72×) and FFP flatpath (97×) ran concurrently from 04:39:04** — two water renders per frame, one padded to material texture 114C1B08, the other vs RT sampling. Downstream identical geometry: this duplication is the top structural suspect for the seam/wash symptoms (§3).

### 2.11 Errors / one-shots / the odd receipts

- **A1 — Non-screen raster live during building cb (once):** `04:38:35.368 — [BUILDING] cb camRas=1FA03650 2048x1024 != screen 1920x1080 — non-screen raster LIVE during building cb`. One frame before the 04:38:35.593 device reset. A building pass rendered bound to a 2048×1024 foreign raster — exactly the shape that leaves a half/dirty-black screen region for that frame.
- **A2 — Vehicle texture NULL (×5):** `04:39:31.491 / 04:39:39.496 / 04:39:41.494 / 04:39:51.500 / 04:39:59.503 — [VehTex] tex='' null=1 bound=04C84B28 env=00 spec=0 matA=255` (same bound pointer each time = the screen-family raster space, cf. camera rasters 04C7EFC0/04C80A38). Before 04:39:31 all VehTex samples are valid textures; after 04:40:05 never null again. Vehicle material resolved to a **null/empty texture bound to a screen-raster pointer** for the 31 s window.
- Rest of error surface clean: 0 compile FAILs, patch_verify all OK, `[shadow_install]` ×5 OK, `[pointlights_install]` ×2 OK, `[skydome_install]` deliberately no-hook (callee self-sets states), MirrorPass gate off (`mirrorDistortionFix off — no composite overdraw`), no watchdog/crash lines, no NaN lines, DLL detach clean.
- `[BldElide] writes skipped: Xbox=334737` cumulative (elision path active).
- `[PipeAlpha]/[PedOpaque]/[PedRS]` parity receipts all OK (`contract repaired divergent entry` ×114 = entry-state reconciliation, `PedRS ok cull/blend/src/dst/zw/afunc/aref rw=drv=dev`),
- `[FxAlpha] PostFx.repaired` ×111 (postfx alpha repair engaged).
- `[PedDual] legacy dual pass restored (zwriteThresholdPed=128)`, one `dual pass skipped: zwrite=1 hasAlpha=0`, `[PedTex] stage0 resync cached=00000000 dev=00000000` (ped sampler-0 cached pointer 0 — another spot texture state resolved to NULL at least once).

---

## 3. Symptom ↔ receipt correlation

| # | Symptom | Correlated receipts | Verdict (log) |
|---|---|---|---|
| S1 | **CJ grey mannequin (no skin texture)** | (a) `texalpha OPAQUE hist=none` for `torso/head/legs/feet/glasses` at 04:38:35.34 — player cutout textures classed opaque, no histogram ever collected for them; (b) `[Ped] pAmbient override: (0.000,0.000,0.000)` ×11 throughout 04:38:35–04:38:55, then normal small ambient; (c) `[PedTex] stage0 resync cached=0 dev=0` | **Incriminated:** player-text classification (histogram never built for player exts → no cutout) + zero-ambient window covering first 20 s; mannequin look consistent with unlit/unclassified body. Fix candidates: player ext histogram path + guard pAmbient=0. |
| S2 | **Giant black quadrant (right half, diagonal edge)** | A1 `[BUILDING] cb camRas=1FA03650 2048x1024 != screen` + device Reset 2 steps later. Single corrupted frame around 04:38:35.3–35.6 | **Incriminated (if shot ≈04:38:35):** building cb live viewport bound to a 2048×1024 non-screen raster pre-Reset; diagonal-edge artifact typical of that frame. Otherwise: no other receipt matches a screen-space half-black. |
| S3 | **Hard vertical seam in water (left flat pale vs right textured)** | (a) dual water pass: FFP flatpath site=1 (`s0=114C1B08`, vanilla raster, pale flat) + `[water] chain` (parallax/RT, textured) both rendering from 04:39:04; (b) 04:38:48–04:39:03 PBR window had ONLY flatpath → some shots contain flat-pale water; (c) site=2 flushes with s0=00000000 = untextured FFP regions | **Incriminated:** two independent water renders with different filters/bases and per-site texture state. A vertical split of the same plane between an FFP pale pass and a shader textured pass or s0=NULL region reproduces the seam. |
| S4 | **White polygon blob** | site=2 flatpath `s0=00000000` (untextured FFP polys), e.g. `v=460 i=690` (04:40:24) | **Incriminated:** white = FFP default color with NULL s0 — small/medium white polygons from site-2 flushes. |
| S5 | **Giant white wedge at swim level** | site=2 big draw `v=2036 i=3054` all-NULL handles 04:41:32 (right after `[Cull] exit restored=1`); wake hooks installed but **zero** wake diagnostics (§2.2) → any wake geometry is FFP-untextured-blind; splash gate receipt `uw=0.34` at 04:41:33 confirms swim-level scene | **Incriminated (structural):** large untextured FFP site=2 flush in a post-cull window with all state NULL. Also flag wake path as unevidenced — if the wedge is wake geometry, no log evidence can exonerate it. |
| S6 | **White/grey wash over bottom third of water shots** | (a) site=1 FFP pale pass under the shader chain (S3 mechanism); (b) `reflRT=none reflFlag=0` — no reflection sampling → glancing/underwater regions rely on fresnelSplit weights alone; (c) Water_Underwater PS created but no underwater-pass receipts (running blind) | **Incriminated (secondary):** duplicate flat pass wash + disabled reflection can wash-out near-camera water. No terminal verdict from log alone. |
| S7 | **Black band along horizon** | `farClip=1800/1652` vs `[SKY] entry DEV ps=0 vs=0` FFP sky; `reflRT=none` for far water; `[Tonemap] … hour=10` dark ex exposure; no reflection/beyond-far band receipts | **Not directly incriminated by any receipt.** Weakest evidence link. Suggest: far plane / horizon-far-water edge probe next build (sky is FFP, water chain is shader — mismatch at horizon seam is plausible but unseen in log). |
| S8 | **Black unlit sedan in shallow water** | A2 `[VehTex] tex='' null=1 bound=04C84B28` ×5 during 04:39:31–04:39:59; `[PED] dual pass skipped … pipe=04C86410`; `[PedTex] stage0 resync cached=0 dev=0` nearby; windows overlap the shallow-water screenshots | **Incriminated:** sustained vehicle-texture-null window on a screen-raster pointer — exactly the "black unlit car" shape. Same root pattern as S4/S5: a shared texture registry (likely the rebind-not-restored raster pool) leaks state into unrelated draws. |

Also:
- **Corona black-quad risk**: `[CoronaBlend]` sampler caught REPLACE (blend=0) on 53/120 im2D draws; those are NOT necessarily corona draws (mixed sprite samples), and the shield repaired 31 windows ([CoronaShield] forced). Net: shield working, but REPLACE-state leaks remain frequent among im2D consumers.
- **Corona feed dead**: `pool 0→−80730 monotonic, fed=0, regFed=0` all 82 samples — every corona in the pipe-side path was rejected with `rej(id0=64 sz=0 rgb0=0 int0=0)`; installed-but-unfed receipt signature.

---

## 4. Ranked incrimination / exoneration per subsystem

1. **Water dual-pass (chain + FFP flatpath) — INCIMINATED (top).** Site-local differences in texture binding (s0 non-NULL on site=1, s0 NULL on site=2), duplicated geometry per frame (72 chain entries + 97 flatpath entries), and RT-vs-FFP filter mismatch explain S3/S4/S6 and likely S5. Site=2 needs a texture bind audit; the PBR-window double-template regime confirms pale-FFP-only water shots.
2. **Vehicle/player texture registry (VehTex null + PedTex stage0=0 + clamp offsets between screen rasters and materials) — INCHEMINATED.** Direct, five-receipt evidence for the black sedan (S8), plus ped stage0 null (S1 secondary).
3. **Player ext texalpha (hist=none on torso/head/legs/feet/glasses) — INCHEMINATED.** Ties to S1 no-cutout/mann­equin look; must read skin pixel data (histogram) before classify, or hard-code CUTOUT for player parts.
4. **pAmbient=0 window (04:38:35–55) — INCHEMINATED (secondary for S1).** Black-ambient peds in the exact first 20 s; ped-ambient revert pushed 0.000 overrides.
5. **Building-cb viewport on foreign raster (A1) + device-Reset timing — INCHEMINATED for S2 only.** Single-frame black quadrant.
6. **Corona feed pipeline — INCHEMINATED as broken** (fed=0 + monotonic negative pool + constant rej). Not yet linked to a listed symptom, but a dead feed + REPLACE leaks among im2D sprites means any corona-related chip in shots cannot be exonerated by this code path.
7. **Wake hooks — UNVERIFIABLE (CONCERN).** Installed with zero receipts all session. If any wake/wedge visuals were in shots, that geometry ran unlogged and untextured; add a first-hit receipt + texture logging for wake sites next build.
8. **Splashfoam fix — EXONERATED.** 113 correct dst-blend receipts + gate receipt. Not a symptom source.
9. **Corona shield force — EXONERATED (mostly).** 31 REPLACE windows repaired in-flight; [FlushShield] restore logged once; [CoronaBlend] canonical state majority (60/120) = shielded draws sampled.
10. **pipeUploadPBR — EXONERATED.** First-upload receipt correct (c22/c23 via proper path).
11. **Xbox water chain bookkeeping — EXONERATED.** 72/72 paired restores, stable handles, cull pre/set sane, RT create/release paired; no chain-side receipt anomalies.
12. **Motion-blur dt fix — UNTESTED (gated off)** (`motionBlur=0` everywhere; no MotionBlurDtScale, no CUT reset). Exonerate only trivially — nothing exercised.
13. **Grass Z-contract repair — UNTESTED (0 receipts, 0 'grass' strings).** Add a first-hit receipt; this buildabiliy's path never surfaced.
14. **Decal ZWRITE-off — UNVERIFIED.** Decal textures never sampled; cleargraf02_LA exists (texalpha FADED 85% mid-alpha), trafficlight_64 never seen. Extend BldTex sampler to force decal sampling next build.
15. **Softbody probe — NOT ACTIVE this run** (INI backend=off). Note PhysicsCfg may have recreated its INI at load — confirm the native edit survived deployment next run.

---

## 5. Receipt ↔ symptom quick reference

- 04:38:35.34–.36 — player parts OPAQUE hist=none; pAmbient=0 window begins; A1 malformed building-cb frame.
- 04:38:35.59 — device Reset, shader re-init #2.
- 04:38:48.43–04:39:03.99 — **PBR window** (buildings PBR, cars Xbox, water FFP-only, CF Modern).
- 04:38:55.90 — water flatpath **first hit** (site=1) — inside PBR window.
- 04:39:04.70— water chain active (sceneRT ok two ms earlier), waterStyle=0.
- 04:39:31–04:39:59 — **vehicle texture null** window ×5 (S8).
- 04:40:24 — flatpath site=2 begins (s0=NULL series) (S4/S5).
- 04:41:32.08 — site=2 v=2036 flush with all-NULL state (S5 giant wedge candidate).
- 04:41:33.17 — splash underwater gate skip, uw=0.34 (swim level).
- 04:42:18 — final water flatpath count 35397; clean shutdown 04:42:20.

## 6. Next-build diagnostic requests

- Wake: first-hit + per-flush receipts (v/i, s0/stale handles) — mirror water flatpath format.
- Grass: first-hit receipt for the Z-contract repair cadence.
- Decals: pin BldTex sampler to always sample cleargraf02_LA / trafficlight_64 draws.
- Corona feed: log reject cause evolution + pool-decay rate (it is a leak-shaped counter).
- Player exts: log why `hist=none` (raster lock failure? non-lockable format?).
- Site=2: log the intended texture (expected raster) to compare against s0=NULL.
- Vehicle tex registry: log the object pointer alongside VehTex to finger the car/driver of the null window.
