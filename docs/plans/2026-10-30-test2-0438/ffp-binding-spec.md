# FFP stage-0 binding diagnosis — build 2026-10-30 04:36:47 (test run 04:38–04:43)

READ-ONLY document (no hunks applied). Evidence: `skygfx_dbg.log` (28771 lines,
single run, Windows 1920x1080, cfg `pipeline=2→Xbox, building=1/vehicle=2/cf=2`,
water Tier-2 Xbox style), source at the revision deployed in `skygfx.asi` 04:37:37.

Log-access note: the two Obsidian RE-vault MCP servers returned schema errors for
both text and JSONLogic search this session; render-order claims below are
marked [ORDER?] where they rest on code comments instead of a vault receipt.

---

## 0. Run-level facts (ground truth from the log)

| Receipt | Meaning |
|---|---|
| `[SKY] entry ... DEV ps=0 vs=0` (every 2s, first at 04:38:57.960) | Sky pass **not** shaded by a leftover object-pipe shader; CSS-shutdown of VS/PS next frame is NOT the white/black-sky culprit |
| `[water] chain ... tier=2 needRT=1 sceneRT=ok depthRT=ok reflRT=none reflFlag=0` | Water pipe runs every flush; **reflection copy RT fails to create** (g_reflTexture=0). Body flag line is corrupt (`body=%d` printed with a string arg → `body=1590066272`) — logging bug, use c14 instead |
| `[water] restore: VS/PS unbound, tex stages 0-3 cleared, ... vertexAlpha off ... fog on` | waterPipe_restoreRenderState (waterPipe.cpp:1719+) runs paired — but its output is a **stage-0-null + blend-off + fog-on** world |
| `[water] flatpath *` / `[wake] flatpath *` — **ZERO lines** | W1 (RenderWater inline, 2 sites) and W5 (wake, 2 sites) hooks were installed (init line 51–52) but **never fired this run** → the waterclear256/wake raster leak they describe cannot be the live producer of the new symptoms on 04:38–04:43. Still listed in the fix plan because the sites fire whenever vanilla's inline flush preempts RenderAndEmptyRenderBuffer |
| `[corona_blend]` (120 samples) | Healthy game Im2D draws: `dev blend=1 src=2 dst=2, rw raster=17BA0B18/0E47E098 dev s0=0E47F8B8` (additive + own raster). **Pathological samples** (~40%): `dev blend=0 src=5 dst=6` with `drv blend=1 src=10 dst=1` and `rw raster=00000000 dev s0=00000000` |
| `[CoronaResync]` — **ZERO lines** | canonical window-close resync (main.cpp:3338+) produced no output in the whole run → either `g_coronaBodyDone` never armed while a flush was sampled, or INI `spriteFlushShield=0`. Verification item |
| `[PedOpaque] pre-entry dev(ref=0/2/100/140 ...)` + `[PedRS] ok/DIVERGED` | ped entry is repaired to GREATEREQUAL@1 opaque every frame; pre-entry ref is constantly polluted (0,2,100,140) by upstream passes — pollute-and-repair churn is the frame's normal mode |
| `[PedTex] stage0 resync` — **ZERO lines** | `pedStamp0Resync` (main.cpp:1694) one-shot never emitted → INI `pedTexResync` likely 0 (bisect lever OFF), i.e. the ped stage-0 resync was not exercised in this run |
| `texalpha: 'CJ_PLANT' -> CUTOUT`, `[BldTex] tex='CJ_CHROME2'/lw_seat2` | texalpha classification + building bind audit are running; no tampering with ped skin textures found (texalpha is classify-only, texdefs never re-binds stage 0) |

Time correlation: in 120/120 2s samples, pathological `corona_blend` lines appear
0–1.4 s **after** a `[water] restore` line and never before one — the device/RW
blend split and raster-NULL window is downstream of the water restore.

---

## 1. Symptom → root cause analysis

### S1 — CJ renders as a grey featureless mannequin (black diamond on face, black facets on arms)

Peds are vanilla FFP skinPipe via hooked `myDefaultCallback` (main.cpp:1737,
hook 0x7491C0). The vertex/blend path is provably repaired ([PedOpaque]/
[PedRS]/[PedDual] receipts). What remains wrong is the **skin raster content at
stage 0 at skin-draw time**. Mechanism chain, ranked:

**RCA-1a (primary): stage-0 raster leak vendors feeding the L1↔device texture split (prime-hypothesis (a), confirmed as a real, recurring state on this build).** Three leak vendors were audited; all feed the same failure signature the log already shows (`rw raster=00000000 dev s0=00000000`):

| # | Vendor | Code | What persists |
|---|---|---|---|
| L1 | `WaterDrops::Render` tail | neopipe.cpp:1907–1908 | `RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL)` + `VERTEXALPHAENABLE=0` set through the **L1 rw cache only, never restored, crosses the frame boundary** into frame N+1. WaterDrops runs every frame the player nears splashing water (the whole test session) |
| L2 | `waterPipe_restoreRenderState` | waterPipe.cpp:1740 (raw s0 null), 1744 (L1 raster NULL) | deliberate `s0=NULL`+raster=NULL; covers downstream consumers who bind (fine) but *widens the NULL window documented by corona_blend* |
| L3 | W1/W5 direct-flush hooks (designed leak) | main.cpp:5706–5731 (water), 5806–5831 (wake) | binds waterclear256/wake raster and *deliberately* does not restore. **Cold this run** (no receipts) but live whenever the inline flushes preempt the buffer hook |

The "black diamond on face / black facets" is the fingerprint of a **foreign
nearest-uniform raster sampled through the skin atlas UVs**: grey-blue plate
colour over the whole body = waterclear256-class texture; black diamonds/tris =
the UV-atlas gutters (opaque-black fill between islands). Both are classic
symptoms of the ped sampling the **wrong raster at stage 0**, not of lighting.

Why the skin pipe's own bind can no-op: RW's raster→device path
(`RwD3D9SetTexture` internals, per texdefs.cpp:23/594) value-compares the
**resolved D3D texture pointer**, not the RwRaster pointer. A raw device
`SetTexture(0, X)` that does not bump the RW cache (WaterDrops' `RwD3D9SetTexture`
writes go through L2 — main.cpp comment 5884–5899 acknowledges the class; the
water pipe's raw s1-s3 unbinds are device-only) leaves cache=X`, device=y;
whenever the *next* consumer binds the raster cached under the stale name, it
no-ops. peds are the first per-material consumer after water+WaterDrops in the
frame; `pedStage0Resync` was written exactly for this but the log shows the
INI lever was off, so it never ran.

Receipts that would confirm: `[PedTex] stage0 resync cached=R dev=T` with
`R != skinRaster` on a bad frame; a per-ped-atomic receipt binding the skin
raster and then pre-draw `GetTexture(0)` mismatch.

**RCA-1b (secondary, cheap to exclude): **texdefs/texalpha do NOT touch the ped
skin path** (grep audit: texalpha only classifies textures, texdefs only reads
rasters, never calls RwRenderStateSet — src/texdefs.cpp has zero render-state
writes). Only hypothesis (d) is thereby *refuted as a direct cause*; it remains
mentioned for completeness.

### S2 — Giant black quadrant covering right half of screen, hard diagonal edge following terrain slope

The cutting edge = **a near-horizontal plane intersecting sloped terrain**
(the plane is behind terrain where terrain rises above it; where the ground
slopes down, the plane rasterises — the diagonal is the "shoreline" of two
height fields). The plane at water height spanning a huge area = **a water
sheet body**. Ranked candidates:

**RCA-2a (primary): the W1/W7 class — a vanilla Im3D water flush (site-1
outside-world sheet, main.cpp:5629/5706 — "before the water raster bind") is
executed FFP with a raster/black-garbage state left by the water pipe's restore
window.** The outside sheet (±3000) is not inside the TempBuffer that the
RenderAndEmptyRenderBuffer hook owns; when RenderWater's inline flush draws it,
it inherits from the last restore. After a water-tier≥2 flush the world state is:
s0=NULL (both caches), blend device=OFF/lite, sampler0=W U +CLAMP leak
(until restore resets it), FOG=TRUE. A giant FFP sheet drawn REPLACE with
NULL stage-0 + vertex colour black/dark navy + fogged is the black quadrant.
[ORDER?] Evidence angle: samples immediately after `[water] restore` show exactly
this window. **The run-04:38 counters missing for W1 mean the hook never logged —
which under alarms means the hook-site report was aimed at the wrong address or
the site is excluded by the alive gate: fix must add a per-2s water site hit
counter to prove order; treat the RANK as provisional until that receipt.**

**RCA-2b (rule-out candidate): reflection-corona pass renders into the
2048x1024 reflection raster** (`[FxAlpha] Coronas.Reflections camRas=1FA03650
2048x1024 != screen 1920x1080`) while our own water reflection RT failed
(`reflRT=none`). The corona-reflection viewport (2048x1024) draws over a camera
whose viewport (1920x1080) mismatch cannot stretch sample space rectilinearly —
this yields big rectangles anchored by scene depth (a "hard diagonal edge that
follows terrain"). Only build-visible when the player looks down a beach slope,
which matches "right half of screen". Requires a receipt to move from rule-out
to confirmed: `corona_reflect_dims` line printing the reflection camera viewport
at draw time.

### S3 — White polygon blob / giant white wedge near swim level; white/grey wash over bottom third

Two stacked candidates, both receiving:

**RCA-3a (primary): splash-foam system runs ALWAYS after a swim; the hooked
`fxSystem_c::Render` (neopipe.cpp:1231+) rewrites prim dst `ONE→INVSRCALPHA`
only for splash-class systems, but the **impact-disc prims** (`water_splash*`
5×5 m sphere discs, ~alpha-64, ZTEST on / ZWRITE off per 0x4A92C2–EA) render at
`dev blend=0` when the consumer inherited blend OFF from the post-water window
— a large alpha-white disc drawn REPLACE = **solid white wedge** at swim level.
The log's every-2s `[splashfoam] splash system render: 1 prim(s)` + the
pathological blend=0 Im2D window in the same second is the current best
mechanism. The fix is not the splash hook (it's the blend inheritance), but a
short per-draw blend pin on the splash system.

**RCA-3b (secondary): WaterDrops screen-space overlay** (neopipe.cpp:1861+,
hooked after CMotionBlurStreaksRender 0x726AD0) draws with
`DefinedState()+FOG=0+ZTEST=0+blend on`, samples `pRasterFrontBuffer`
(**2048x2048, the known Not-fullscreen raster** — [PostFX-REPORT] pRasFB
2048x2048 vs camRas 1920x1080). If the front buffer is a stale frame (e.g. the
sample is taken from a water-underwater pass), the drop mask rasterises as a
**white-grey slab in the lower third**. Receipt to confirm: `[WATERDROPS]
overlay drew N quads` — **absent this run** → cold; keep as secondary.

### S4 — Black band along the ocean horizon above the water line

**RCA-4a (primary): FOG TRUE + fog-colour black inherited after the water
restore window.** `waterPipe_restoreRenderState` (waterPipe.cpp:1749) unilaterally
pushes `RwRenderStateSet(rwRENDERSTATEFOGENABLE, TRUE)` through L1 **without
touching the D3D9 fog colour at L2/L3**. After a tier-2 flush which set
FOGENABLE=FALSE for its own opaque draw, every FFP consumer that does not push
fog colour (raw SetRenderState FOGCOLOR is *not* tracked by RW's rw state cache,
c.f. shadowContract per-layer capture table documenting "no rw twin" states)
rasterises its distant geometry inside blend-TRUE-with-black fog → **a black
band exactly at the horizon** where distance-fog saturates. This matches the
"black band above the water line": water recedes into max-distance and the
next frame's sky/ground horizon pixels behind it render fog-black.

Receipt: log a single frame snapshot at the horizon band moment: `FOGCOLOR`
device value + `FOGENABLE` ring per layer next to a `[water] restore` line.

**RCA-4b (rule-out): skydome/明 additive ghost** — `[skydome_install] NO hook
installed — callee self-sets ztest/zwrite/blend/raster` (init log 04:38:17.388):
no additivity guarantee there; secondary/defensive bisect lever.

### S5 — Black sedan in shallow water renders black/unlit silhouette

Same mechanism family as S2/S4, that is the water sheethb; two lenses:

**RCA-5a (primary): the vehicle in water is drawn *before* the water
pipe's restored state reaches the vehicle pipe. Vehicle pipe X-car (CAR_XBOX)
requires a raster at stage 0; with cache=raster(=whatever), device s0=leak X'',
the vehicle material no-ops into a "changed" transition — normally black.
However this contradicts [PedOpaque]-adjacent expectations. Because the
symptom persists **regardless of vehicle pipe chosen** (user-confirmed), the
shared-path producer is required → essentially the same stage-0-vs-blend
window as RCA-2a applied to vehicles + the FOG-black accession of the first
FFP pass over shallow water. Ranking same as S4: singletons share the
water-restore-window defect.**

Receipt: a one-shot per‑vehicle bind receipt (already exists as
`vehEnvBindTexture`'s resolved-key log; extend to print `dev s0` pre-draw).

---

## 2. Ranked fix spec (DO NOT APPLY — spec only)

All hunks reference the tree as it stood at build 04:36:47.

### FIX 1 (top) — No-verbatim-unpark for direct-flush water/wake hooks (kills the "FFP consumer inherits object-pipe shaders" class) + restore the leaked raster policy

Files: `src/core/main.cpp`

- **1a. WaterDirectFlush_Draw unpark (main.cpp:5713–5731):** today it restores
  `prevVS/prevPS` raw *and* caches them into `_rwD3D9Last*Used`. Every
  fixed-function consumer that draws next before a shader-owner rebinding will
  process through the parked VS/PS. Since the wattage site binds its own raster,
  the *restore of the raster is a policy decision; the restore of the SHADERS
  is a bug* under D3D9 rule (SHD-bound VSO bids). Correct hunk:

  ```c
  // main.cpp WaterDirectFlush_Draw, unpark block (currently ~5713–5731 and 5813–5831)
  // BEFORE:  d3d9device->SetVertexShader(prevVS); d3d9device->SetPixelShader(prevPS);
  //          *(void**)0x8E2448 = prevVS; *(void**)0x8E244C = prevPS;
  //          prevVS->Release(); prevPS->Release();
  // AFTER:   // Do not restore prevVS/prevPS: RW fixed-function Im3D/Im2D
  //          // consumers (shadows, particles, wake/splash pathes) do not
  //          // unbind shaders. Leave FFP NULL on both layers and seed the
  //          // RW caches with NULL so the next shader-owning pipeline's
  //          // bind is a real transition; a shader-owning consumer (any
  //          // skygfx object pipe, postfx pass) rebinding overwrites this,
  //          // but an FFP consumer now sees FFP instead of the parked shader.
  //          *(void**)0x8E2448 = NULL; *(void**)0x8E244C = NULL;
  //          // RA-style release of the parked handles only.
  //          prevVS->Release(); prevPS->Release();
  ```

- **1b. Raster restore policy:** the deliberate non-restore of
  waterclear256/wake (main.cpp:5706–5707, 5806–5807) is acceptable only while
  the L1 cache records it; it currently *does* (set via RwRenderStateSet).
  Keep it, but append the missing `[wake] flatpath count-noted` lines with a
  site-hit counter so cold/warm state is provable per run:

  ```c
  // main.cpp, after 5711 / 5811 (both WaterDirectFlush_Draw and WakeDirectFlush_Draw):
  //   static unsigned s_siteHit[3] = {0,0,0};
  //   s_siteHit[site]++;
  //   if(dbglog_throttle(site==1 ? "w1_hitratio" : "w5_hitratio"))
  //       dbglog("[water] site%d hits=%u (count=%u)", site, s_siteHit[site], x);
  ```

  Rationale: the fix spec must distinguish "the hook fired but the flatpath
  receipt was starved" (run policy true) from "hot-cold site" (install-then-
  never-armed). Currently the silence INCORRECTLY reads as cold.

### FIX 2 (rank 2) — waterDeviceRestoreStage0 white-texture + L2 re-push (kills the stage-0-NULL Z-class proven by corona_blend)

Files: `src/render/waterPipe.cpp`

- **2a. Hunk (waterPipe.cpp:1740–1744):** replace the triple-null with a
  defined bind:

  ```c
  // waterPipe.cpp waterPipe_restoreRenderState (lines 1739–1745)
  // BEFORE:
  //     g_d3dDevice->SetTexture(0, nullptr);   (1740)
  //     ...
  //     RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);  (1744)
  // AFTER:
  //     RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
  //     RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)whiteRas); // L1+L2+L3 consistent, non-NULL
  //     g_d3dDevice->SetTexture(0, nullptr);   // leave raw level consistent: RW setter re-binds it on the next material bind
  ```

  The white texture is 1×1 opaque — any consumer that *misses* a rebind due to a
  cache split renders a defined near-white texel (clickable, diagnostic) instead
  of device-default garbage.

- **2b. Hunk (waterPipe.cpp:1747):** the `RwRenderStateSet(FOGENABLE, TRUE)`
  restore is incomplete — D3D9 fog colour has no rw twin (documented at
  main.cpp:1290 and shadow contract tables: "ZFUNC/ALPHATESTENABLE have no rw
  twin"). Add raw-layer re-push of the *pre-water* device FOGCOLOR, or (safer)
  propagate the render-state monitor's fog contract:

  ```c
  // waterPipe.cpp waterPipe_setRenderState() — new local, before the force block:
  //     DWORD sSavedFogColorDev = 0;
  //     g_d3dDevice->GetRenderState(D3DRS_FOGCOLOR, &sSavedFogColorDev);
  // waterPipe.cpp waterPipe_restoreRenderState() — after 1749:
  //     g_d3dDevice->SetRenderState(D3DRS_FOGCOLOR, sSavedFogColorDev);
  //     (expose via two static DWORDs alongside s_savedZWriteDrv etc.)
  ```

### FIX 3 (rank 3) — WaterDrops tail must not publish L1 raster/NULL + blend-off across the frame boundary

Files: `src/render/neopipe.cpp`

- **Hunk (neopipe.cpp:1907–1908):**

  ```c
  // BEFORE:
  //     RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
  //     RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)0);
  // AFTER:
  //     // Park with the white texture like shadowContractRestore:
  //     // a NULL L1 raster + VERTEXALPHA=0 + fog=0 combo is exactly the
  //     // corona_blend pathology window; pin the cache to a defined raster.
  //     RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
  //     RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)whiteRas);
  //     RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)0);
  //     // (blend/side restores for SRC/DEST already exist at 1896–1906 via the
  //     // captured savedSrc/savedDst per-layer values — keep those.)
  ```

  Additionally the L2/L3 SRC/DST restore at neopipe.cpp:1895–1904 currently
  reads a capture that is **only valid if DefinedState() didn't rewrite them**;
  the pass re-captured at 1848–1856 (before DefinedState) — good; leave as is.

### FIX 4 (rank 4) — canonical resync never fired: arm + log

Files: `src/core/main.cpp` (spriteFlush index) and INI.

- Confirm at runtime whether `g_coronaBodyDone` ever arms while a flush runs:
  log currently has **zero** `[CoronaResync]` lines even though
  `dbglog_throttle("corona_resync")` runs once per 2 s at most, so next-run
  bisect: if the canonical block at main.cpp:3338+ still never logs, the branch
  is being skipped because `config->spriteFlushShield == 0` in the INI.
  Spec step: add a one-shot INI value receipt at install:

  ```c
  // main.cpp install block (8218–8240)
  //   dbglog("INI spriteFlushShield=%d, pedTexResync=%d, shadowContractEnable=%d", ...);
  ```

### FIX 5 (rank 5, optional probe before next test run)

One-line probes that would settle RCA-1a/2a/3a in a single 2-minute run; spec
only, NOT to apply without review:

1. `[water] restore` line gains: `dev s0=%p drv-s0-n/a fog-col=0x%08X` (waterPipe.cpp:1779).
2. `Im2dSetPixelShader_hook` additionally prints `GetVertexShader + FVF + decl-cached` (postfx.cpp:481).
3. `myDefaultCallback` ped branch: pre-pipeline `GetTexture(0)` receipt tagged
   `ped_s0predraw` (throttled), gated by INI `pedTexProbe=1`.
4. Water site counters (FIX 1b) to settle W1/W5 cold-vs-starved.

### Explicitly NOT the cause (with reason)

- texdefs / texalpha (RCA-1b): classification only; zero stage-0 writes; no
  evidence of interplay with peds.
- Clouds PS bind/unbind (hypothesis (c)): the `RwD3D9SetPixelShader` pair at
  main.cpp:3314–3321 is paired inside one `__try`-guarded owner flag and resets
  both layers; no FVF/decl touch, no texture touch; `[SKY] entry ps=0` proves
  the exit happens before the sky pass in every frame.
- Ped skin path own texture bind (hypothesis (d)): bind is always a real
  transition because skin rasters differ from leak rasters; [PedOpaque]/
  [PedRS] receipts show the *render-state* contract is in effect.
- Splash-foam hook itself (neopipe.cpp:1231–1319): matched/restore is per-system
  and log-verified byte-level (`prim[6]`), no cross-system spill possible
  because restore sees the same prim objects.

---

## 3. Verification matrix (receipts → hypothesis)

| Receipt you should see | Confirms | Refutes |
|---|---|---|
| `[PedTex] stage0 resync cached=<water or wake raster>` on a bad-CJ frame | RCA-1a | — |
| `corona_blend` pathological window no longer repeats after [`water] restore`+FIX 2 | RCA-2a / FIX 2 | RCA-3b |
| W1/W5 `siteN hit` counters >0 on a grep of a DEV build | FIX 1b / RCA-2a | "W1/W5 were annot-cold" |
| Black quadrant disappears with `waterPipeDisable=1` (pre-existing INI) but **grey CJ still present** | RCA-1a | RCA-2a as CJ-cause |
| Black quadrant appears with `waterPipeDisable=1` (vanilla water, no skygfx touch) | WaterPipe-not-yours; RCA-2b | RCA-2a |
| `[WATERDROPS] overlay drew N` near symptoms | RCA-3b | — |
| FOGCOLOR receipt black at horizon-band moment | RCA-4a | — |

Bisect levers (INI): `spriteFlushShield`, `pedTexResync`, `shadowContractEnable`,
`waterParallaxEnable`, `cloudRenderFix`, `pedOpaqueContract`. Setting each to 0
in one session each maps every remaining symptom to the shared потребитель.

---

## 4. Top 3 ranked root causes (summary)

1. **W1/W5 direct-flush hook unpark bug + un-restored raster policy** —
   water/wake hooks restore prev VS/PS after forcing FFP, leaving bind-counted
   object shader state and a water/wake raster live for the next FFP consumer
   (main.cpp:5713/5813). Symptom set affected: S2 (black quadrant), S5, plus
   black-horizon falls through it.
2. **Water-pipe restore publishes stage-0 NULL + blend-dev-0 + FOG TRUE** —
   waterPipe_restoreRenderState (waterPipe.cpp:1740/1744/1747/1749) puts every
   consumer into the `rw raster=NULL dev s0=NULL, dev blend=0, drv blend=1`
   window the corona_blend receipts actually filmed; fixes in waterPipe.cpp +
   WaterDrops tail. Symptom set: S1 (grey CJ via foreign-raster sample),
   S3/S4.
3. **WaterDrops::Render tail publishes L1 raster=NULL + VERTEXALPHA=0 across
   the frame boundary** (neopipe.cpp:1907–1908) — drip feed into every frame
   that has dispenser/splash FX, strongest co-correlate of the
   post-water/стage-0-NULL window in the log.
