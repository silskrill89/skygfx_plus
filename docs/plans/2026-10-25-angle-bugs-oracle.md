# 2026-10-25 — Angle-dependent black screens / angle-dependent ped visibility

**Oracle strategy review. Read-only: this file is the only artifact written.**

Inputs: `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` (waves 1–8),
`docs/plans/2026-10-21-alpha-reversal-findings.md`, `2026-10-24-water-veil-diagnosis.md`,
`2026-10-25-reset-blackout-diagnosis.md`, `2026-10-25-ivgrade-audit.md`,
`docs/baseline/2026-10-25-screenshots-2200/README.md`, the live log
`E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (6,152,664 B, 22:35:22 → 22:42:46),
the live `skygfx.ini`, and the working tree at 00:15 on 2026-10-26.

> **Line-number caveat.** `src/core/main.cpp` and `src/render/pipelinecommon.cpp` were being
> edited while this review ran (mtime 00:06 / 00:11). Line numbers below are from the 00:15
> tree; the function names are authoritative. The **deployed** ASI is 21:57:27,
> 2,360,832 B — the tree is newer and **not deployed**.

---

## 0. TL;DR verdict

- **Two symptoms, two roots — 60% confidence.**
  1. **Interior/world black:** the ambient-only PS2/Xbox pipes run on a 0.03–0.05
     ambient budget while peds get 0.20. The new `ClampAmbientToSky` indoor early-return
     fixes the crush but **returns before the absolute floor**, so interiors can still
     sit at 0.05. `[PBR-Ambient] final=(0.050,0.050,0.050)` is in the live log.
  2. **Peds at specific angles:** a per-pass state leak/overlay, not the ped alpha/Z
     contract (that contract is proven to repair the entry state). The two concrete
     leaks are the **unhooked stored-shadow pass** (pure-black quads; R2-03/R2-08) and
     the **missing `rwRENDERSTATETEXTURERASTER` save/restore in the cloud hook**
     (R2-01: peds sample the cloud atlas; alpha test then discards much of the body).
- **One shared root — 30%.** The stored-shadow pass alone can produce both the
  interior black quads and a ped-shaped occluder over CJ. If the pass-ID experiment
  paints the black region blue, this becomes the answer.
- **One ambient root — 10%.**
- **PBR grazing Fresnel is NOT the default cause.** `bldSkyReflect = 0.15` caps the
  darkening at 15%, and the current tree already CPU-gates the sky term indoors
  (`buildingPipe.cpp:2905-2908`) and suppresses the indoor IBL capture
  (`pipelinecommon.cpp:1067-1076`). It matters only if the user raised the key or is
  running an older PBR CSO.
- **Tonemap/exposure collapse is ruled out.** The live log says
  `[PF] TonemapPass: … identGrade=1 autoExp=0 lumaTex=00000000 operator=0`.
  There is no auto-exposure to collapse.
- **Cull is not the primary cause.** Sampled cull entries are CULLNONE/CULLBACK only,
  never CULLFRONT, and every pipe entry re-asserts the mode through all three layers.
  CULLNONE is double-sided, not black. There is, however, a real **NONE leak from the
  mirror pass** (§6) that should be fixed because it is the one recent change most
  capable of creating a view-angle dependency.

---

## 1. Evidence corrections — read these before any more triage

### 1.1 The 22:00 baseline is not an Xbox run

`docs/baseline/2026-10-25-screenshots-2200/README.md:35` says
`pipeline=Xbox (2), buildingPipe=Xbox (1), vehiclePipe=Xbox (2)`, and §1 caveat 1
(line 80) says “This run is pure Xbox”. **Both are false for the screenshot window.**

The live log shows a mode cycle immediately before the screenshots:

| Time | Event | Log line |
|---|---|---|
| 22:35:22 | `[Mode] apply Xbox` (startup) | 21 |
| 22:37:04 | `[Mode] apply PS2` | 16753 |
| 22:37:07 | `[Mode] apply Xbox` | 17426 |
| 22:37:09 | `[Mode] apply Legacy PC` | 17596 |
| 22:37:10 | `[Mode] apply GTA IV` (iv=1) | 17810 |
| 22:37:14 | `[Mode] apply GTA V` | 18685 |
| 22:37:22 | `[Mode] apply Modern` | 20654 |
| 22:37:26 | `[Mode] apply Custom` | 21433 |
| **22:37:29** | **`[Mode] apply PS2`** | **21997** |
| 22:37:39 → 22:41:20 | **shots 01–21** | `[BPIPE] cb=PS2 pipe=PS2(0)`, `[VPIPE] cb=PS2 pipe=PS2(0)` |
| 22:41:42 | `[Mode] apply None` | 55255 |
| **22:41:45** | **`[Mode] apply Xbox`** | **55989** |
| 22:42:34 / 22:42:45 | **shots 22–23** | Xbox |

So **21 of 23 screenshots are PS2 mode**, not Xbox. Consequences:

- R2-04’s root-cause lead (`vehiclePipe.cpp:1620-1635`, the Xbox vehicle callback)
  is aimed at code that **did not run** during shots 04–11. The PS2 callback is
  `CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2` (`vehiclePipe.cpp:917-1195`).
- R2-01/R2-03 are mode-independent (default ped pipe / CShadows), so they survive.
- R2-06’s water verdict is still valid only because `waterParallaxEnable=0`
  (vanilla water), not because of the mode.
- The README’s “PBR previous baseline vs Xbox this run” comparison is
  mode-confounded in the **opposite** direction from what it claims: the run is
  mostly PS2, with a 4-second GTA IV window and two final Xbox shots.

### 1.2 `colorFilterEnable=0` is not what ran

The INI says `colorFilterEnable=0`, but the log has:

```
[22:35:33.743] [Config] colorFilterEnable=1 diverged from user intent 0 — silent write repaired (slot 0)
[22:35:41.053] [PostFX] ColourFilter_switch: filter=2 pipeline=2 smaa=0 ssao=0 motionBlur=0
[22:37:40.865] [PostFX] CF-SRC: effective pipeline=1 … colorFilter=1 ivMode=0   ← PS2 window
```

The PC colour filter ran during the screenshots. The IV grade did **not**:
`[PostFX] DrawIVGrade … exposure=2.50` appears only at 22:37:10–14, inside the
4-second GTA IV window, and `[PostFX] IVGrade GATE: ivMode=0 … SKIPPED` is the
line for the rest of the run. Do not attribute the 22:00 washout to the IV grade;
do not attribute the black-at-angle frames to it either.

### 1.3 The deployed binary lacks most of the fixes now in the tree

Binary string check on the deployed `skygfx.asi` (2,360,832 B, 21:57:27):

| String / receipt | In 21:57 ASI? | In current tree? |
|---|---|---|
| `0x708300 entry bytes` / `entry hook installed` | **No** | Yes, `main.cpp:7672-7723` |
| PBR grazing guard (`VehiclePBR_Modern.hlsl:1150-1185`) | **Not provable from strings** (compiled CSO); source diff shows it added in the working tree, and the CSO/build predates the 00:06 shader edit | Yes |
| `PedCull` / `PedAlphaTest` / `pedCullMode` | **No** | Yes, `main.cpp:1710-1766` |
| `bld_ibl_indoor` (indoor neutral IBL bind) | **No** | Yes, `buildingPipe.cpp:2451-2465` |
| `ibl_indoor` (indoor capture suppression) | Yes | Yes |
| `shadow_install` (call-site form) | Yes (only 2 sites + skip) | Yes + entry hook |

The user’s report is therefore against a binary that still has **R2-03 unhooked**,
**no ped cull/alpha levers**, and **no indoor neutral IBL bind**; the PBR grazing
guard may or may not be in its CSO (unprovable from strings). The 00:06 tree is the
response batch, not a tested build.

### 1.4 The stored-shadow pass was never hooked in the tested build

```
[22:35:22.734] [shadow_install] 0x53E0BE op=E8 orig=00707F40 OK zeroarg=1 site=CShadows::RenderStaticShadows
[22:35:22.735] [shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows (expect 00708300)
[22:35:22.735] [shadow_install] 0x53E0C3 SKIPPED — vanilla path kept
[22:35:22.736] [shadow_install] 0x53E0C8 op=E8 orig=0070A960 OK zeroarg=1 site=CShadows::RenderExtraPlayerShadows
```

There is **no** `0x708300 entry bytes` line, and the string is absent from the
binary. The call site is already rewritten to `0x68E8DD00` by another module
(`dllModule=0x68A90000`; `0x68E8DD00` is outside the 2.36 MB ASI image, so it is a
different ASI/plugin). Chasing the call-site signature is the wrong lane; the
function-entry hook at `0x708300` is correct. It must be verified in the next run.

---

## 2. Unified diagnosis

### 2.1 Interior black at random angles

| # | Hypothesis | Confidence | Evidence / why |
|---|---|---:|---|
| c | **Overlay pass occludes at certain depths** (shadow/decal, cloud) | **40%** | Stored-shadow pass provably unhooked in the tested build (§1.4). R2-03 black quads are pure `(2,2,2)` with hard geometric edges, 22–41% of frame; R2-08 is a ped-shaped flat fill in the sky. A wall shader would preserve texture/gradient; this does not. The new 0x708300 entry hook is the direct fix. |
| e | **Ambient collapse / incomplete interior floor** | **35%** | `[PBR-Ambient] final=(0.050,0.050,0.050) floor=0.050 raw=(0.000,0.000,0.000)` is the live budget. PS2 building VS is ambient-only: `OUT.Color = day/night*matCol + ambient*surfAmb` (`shaders/vs/ps2/ps2BuildingVS.hlsl:35-37`). `buildingAmbient` is refreshed from `GetTimecycleAmbientPBR()` (`buildingPipe.cpp:183`). The current tree's indoor return (`pipelinecommon.cpp:92`) removes the night-law crush, but it returns **before** the absolute floor (`:158-168`), leaving only the 0.05 PBR floor; whether the 21:57 binary contains that return is unproven (no interior frame in the log), and either way the clamp/floor combination can only be ≤0.05 indoors. Peds get 0.20 (`[Ped] ped_amb_cmp … post=(0.200,…)`). A wall with `surfAmb≈0.3` and texel≈0.5 is 0.0075 → black. |
| b | **Grazing Fresnel toward a dark IBL/sky capture** | **10%** | The path exists (`VehiclePBR_Modern.hlsl:1144-1185`) and the comment describes exactly this symptom, but the default `bldSkyReflect=0.15` caps the replacement at 15%; indoor `bldEnv.z` is CPU-zeroed (`buildingPipe.cpp:2905-2908`) and the indoor IBL capture is suppressed (`pipelinecommon.cpp:1067-1076`). Only live if the user raised `bldSkyReflect` or runs a pre-gate build. |
| a | **Cull/winding** | **10%** | `[Cull] enter saved=1` ×19, `saved=2` ×125; `[PedRS] … cull rw=2 drv=2 dev=2`. No CULLFRONT observed. CULLNONE is double-sided, not black. The mirror NONE leak (§6) is real but cannot make a wall black. |
| d | **Tonemap/exposure collapse when a dark wall fills the frame** | **5%** | `autoExp=0` in the live tonemap receipt; the reset-blackout doc’s black was one frame **pre-tonemap**. The 22:00 black quads are hard-edged, not a global curve collapse. |

**Why “random angles” fits (e):** the clamp/floor is global, but each wall’s visible
brightness is `texture × day/night prelight × matCol + ambient × surfAmb`. Different
materials have different `surfAmb` and prelight, so rotating the camera changes which
material fills the frame. The user sees “some angles black, some fine” while the
ambient budget is uniformly low. This is an important distinction from a true
per-angle shader term.

### 2.2 Peds only visible at specific angles

The ped alpha/Z/blend contract is **not** the surviving problem. The log proves the
entry device is polluted and the contract repairs it before the skin draw:

```
[PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0
[PedOpaque] contract repaired divergent entry: dev(ref=100 fn=5 blend=1 zw=0 …)
[PedRS] ok cull rw=2 drv=2 dev=2 | blend rw=0 drv=0 dev=0 | … | aref rw=1 drv=1 dev=1
```

`pedOpaqueEnforce` (`main.cpp:1076-1119`) runs last before the skin pipeline; the
cull is forced through all three layers by `pipeEnterCullMode` (`pipelinecommon.cpp:1239-1294`).
So the candidates that survive are the ones the contract does **not** cover:

| # | Hypothesis | Confidence | Evidence / why |
|---|---|---:|---|
| e | **Texture-raster / alpha state leak** (cloud atlas) | **40%** | `CClouds__Render_hygiene` saves filter/alpha/fog/Z/blend/stage ops (`main.cpp:4673-4737`) but **never saves `rwRENDERSTATETEXTURERASTER`**, and the restore (`:4810+`) has no texture-raster resync. R2-01 is 8/23. If the ped samples the cloud atlas (white RGB, alpha 0), alpha test `GREATEREQUAL@1` discards the transparent regions → the body disappears; angle correlation comes from whether the cloud/sky pass bound the atlas that frame. `[texalpha] unknown_texnull=129276` shows texture resolution is already unreliable. |
| c | **Shadow/decal overlay covers the ped** | **30%** | R2-03/R2-08 + stored pass unhooked. A ped shadow or a black quad drawn at the ped’s depth can occlude CJ only when the projection overlaps him; as the camera orbits, the overlap changes. The 0x708300 entry hook is the direct test. |
| e2 | **VS/declaration cache desync on the skin pipeline** | **15%** | `vs_probe` is in the tree (`main.cpp:1548-1575`, called at `:2177`) but absent from the tested binary. RW’s value-compare caches are documented to skip a `SetVertexShader`/`SetVertexDeclaration` when the cache already holds the pointer; a stale declaration misparses the skinned stream, producing a mesh that is only coherent at some projections. The first `[DefCB]` receipt already shows `cacheVS=FFFFFFFF`. |
| a | **Cull/winding on skinned meshes** | **10%** | No FRONT observed; ped span preserves the saved mode. `pedCullMode` is a valid A/B, not a proven cause. CULLNONE does not hide a closed mesh. |
| b/d | Shader/exposure | **5%** | Peds bind no skygfx PS (`SkinSSS` is an orphan); `autoExp=0`. |

### 2.3 One root or two?

**Two roots, 60%.** The interior black is a budget problem (ambient-only pipes at
0.05) with an overlay alternative; the ped issue is a pass/state problem (shadow
overlay or cloud texture raster). They share an **enabler**, not a line of code:
skygfx brackets its own pipe callbacks extremely carefully, but the passes *between*
them (mirror, envmap, clouds, shadows, postfx) are only partially bracketed. The new
“preserve the saved cull/state” doctrine then carries the last pass’s pollution into
the next draw.

**One root (stored shadow), 30%.** The unhooked stored-shadow pass draws black
quads; in a tight interior a quad fills the screen; outdoors a ped-shaped shadow can
cover CJ at angles. This is the cleanest single explanation of both symptoms and the
cheapest to test (the entry hook is already written).

**One root (ambient), 10%.** Only if the user’s “peds disappear” really means “peds
are dark and merge into a dark world,” not “peds are geometrically absent.”

---

## 3. Highest-information experiment

### 3.0 Five-minute pre-test (no build, existing 21:57 ASI)

Change **one** INI key and reproduce the interior black:

```ini
[SkyGfx]
pbrAmbientFloor = 0.200000
```

Keep everything else as-is (`pedAmbientFloor` is already 0.20). This is the only
live key that lifts the ambient **after** `GetTimecycleAmbient` and therefore
indoors too.

- Read `[PBR-Ambient] final=(…)` — it must print `0.200`.
- **Interior black disappears / walls brighten materially** → root (e), ambient
  starvation. The fix is the interior floor, not the shader and not the shadow pass.
- **Interior black unchanged** → ambient is not the cause; go to §3.1.

This test is one variable, zero build, and it costs one launch. Do it before any
further code lane. It also tells us whether the ped “dark side” visibility improves,
which would move peds from (c/e) toward (e).

### 3.1 Definitive build: pass-ID flat-colour view (`angleDiag=1`)

Build the current tree plus one **diagnostic-only** switch:

- New INI key `angleDiag` (default 0), parsed/written like `defaultCbShaderHygiene`
  (`main.cpp` readIniFile ~5980–6010, `ADD_IF_MISSING` ~6500, save ~6940).
- New flat PS (`ps_3_0`, `return c0;`) or, for FFP, a 1×1 solid texture + stage-op
  override. Do **not** change Z, cull, alpha test, blend, or depth.
- Paint by pass:
  - buildings **red**, peds **green**, shadow/decal/extra passes **blue**,
  - clouds/sky **yellow**, water **cyan**, everything else grey.
- Emit one `[PassColor] active bld=R ped=G shadow=B cloud=Y` receipt plus throttled
  per-pass counts.

Run configuration (one build, one INI):

```ini
[Modes]
active = PS2              ; or Xbox / Modern — the mode that reproduces the bug
[SkyGfx]
angleDiag = 1
colorFilterEnable = 0
pipeChainEnable = 0
ssaoEnable = 0
smaaEnable = 0
motionBlurEnable = 0
heightFogEnable = 0
radiosityEnable = 0
waterParallaxEnable = 0
bldSkyReflect = 0.0
pedOpaqueContract = 1     ; preserve the failure state
pedCullMode = -1
pedAlphaTestEnable = 1
texAlphaClassify = 1
```

**What the user looks at (two screenshots):**

1. **The interior angle that goes black.** Read the colour of the black region:
   - **blue** → the stored/extra shadow pass owns the pixels → root (c). Ship/verify
     the 0x708300 entry hook.
   - **red** → building geometry is present; the black is shading/ambient → root (e)/(b).
   - **unfilled / black** → no geometry drew → cull/Z/VS → root (a)/(e2).
   - **yellow** → cloud/sky overlay → R2-02/R2-01 lane.
2. **CJ at a good angle and at a bad angle.**
   - **green at both** → geometry/cull is fine; the failure is texture/alpha → root (e).
   - **green only at the good angle** → cull/Z/VS → root (a)/(e2).
   - **blue over CJ** → shadow overlay → root (c).
   - **green but the texture is still the cloud/mannequin** → the pass-ID build has
     isolated the texture lane; the raster restore is the fix.

Also record whether the **HUD/radar is visible in the black frame**. HUD visible
means the black is in the 3D pass (overlay/geometry); HUD black means a postfx/RT
problem (back to the reset-blackout class). This one observation would have saved
the whole reset-blackout lane.

**Log tags to read:** `[PassColor]` (new), `[BPIPE] cb=…` (confirms which building
callback actually ran), `[VPIPE] cb=…`, `[shadow_install] 0x708300 entry bytes=…` and
`entry hook installed`, `[PedRS]`, `[PedOpaque]`, `[Cull]` / `[CullResync]`,
`[PBR-Ambient] final=…`.

Do **not** ship `angleDiag` in a release build; it is a one-run instrument.

---

## 4. Backlog review — what is now wasted or mis-prioritised

| Lane / file | Status | Verdict |
|---|---|---|
| **R2-04 Xbox vehicle material corruption** — `docs/baseline/2026-10-25-screenshots-2200/README.md` §R2-04, `vehiclePipe.cpp:1620-1635` | Wrong pipe. The screenshot window ran `cb=PS2 pipe=PS2(0)` (`[VPIPE]` at 22:37:40+), not Xbox. The PS2 callback already resolves NULL paint textures through `pipeSetTexture` → `gpWhiteTexture` (`vehiclePipe.cpp:772-775`). | **Re-target to PS2** (`vehiclePipe.cpp:917-1195`). The residual flat-black body is more likely black paint at a 0.05 ambient budget than a texture bug. Kill the Xbox-callback lane. |
| **R2-01 cloud texture leak** — `main.cpp:4673-4737` save block, `:4810+` restore block | Valid and stalled. The hook saves filter/alpha/fog/Z/blend/stage ops but **not** `rwRENDERSTATETEXTURERASTER`, and the restore has no texture resync. | **Keep; this is now the top ped fix.** Add the raster save/restore and a ped-entry texture resync. |
| **R2-03 stored-shadow** — `main.cpp:7617-7723` | Valid. Call-site hook correctly skipped (the site resolves to `0x68E8DD00`, another module); the function-entry hook at `0x708300` is written but not built. | **Build/verify the entry hook.** Do not spend more time on the `0x53E0C3` signature. |
| **R2-06 water / water veil** — `waterPipe.cpp`, `2026-10-24-water-veil-diagnosis.md` | Not running (`waterParallaxEnable=0`); the white cone/band is vanilla water + `waterUnderwaterFog`, not the custom WVP. | **Defer.** Do not mix the WVP fix into this bug. |
| **Reset-blackout F3/oldDS guards** — `2026-10-25-reset-blackout-diagnosis.md` | Different class: one frame after Reset, not persistent at an angle. | **Defer as hygiene.** Keep the F3 guards, but they are not the current fix. |
| **IV grade F1–F9** — `postfx_colourfilter.cpp:1354-1547`, `2026-10-25-ivgrade-audit.md` | Only ran for the 4-second GTA IV window at 22:37:10–14; `ivMode=0` for the screenshots. | **Defer.** It explains washout in that window, not black/peds. |
| **PBR grazing env guard** — `VehiclePBR_Modern.hlsl:1144-1185` | Default `bldSkyReflect=0.15` caps the darkening at 15%; indoor CPU gates already exist. | **Keep as hardening, but do not fund a lane.** It is not the default interior fix. |
| **`stateBaselineEnable`** | Parsed/saved/menu-exposed but **no consumer** (checkpoint). | **Do not count it as protection.** Any lane relying on it is inert. |
| **Baseline README “pure Xbox”** | Factually wrong for 21/23 shots. | **Fix the baseline before dispatching more lanes.** Add a per-screenshot mode stamp. |

---

## 5. Next 10 fixes ranked by expected visual gain per hour

| Rank | Fix | File(s) | Gain | Est. | Risk |
|---:|---|---|---|---|---|
| 1 | Ship/verify the stored-shadow function-entry hook (`0x708300`) | `main.cpp:7672-7723` | High (R2-03/R2-08, possible ped occluder) | 0.5 h | Low–Med |
| 2 | Save/restore `rwRENDERSTATETEXTURERASTER` in the cloud hook + ped-entry texture resync | `main.cpp:4673-4737`, `:4810+`, `myDefaultCallback` ~1837 | High (R2-01 white/ghost/disappearing peds) | 1 h | Low |
| 3 | Apply the ambient floor after the indoor early-return; add `interiorAmbientFloor` (0.15) and raise `pbrAmbientFloor` for buildings indoors | `pipelinecommon.cpp:81-168`, `buildingPipe.cpp:183` | High (interior black, black cars/walls) | 1 h | Med |
| 4 | Restore the **pre-mirror** cull in `MirrorPass_ResyncCull` (save at hook entry, restore at exit; do not re-assert the post-mirror rw cache) | `mirrorPass.cpp:364-383`, `:390-492` | Med–High (kills the NONE leak; stabilises all pipes) | 0.5 h | Med |
| 5 | Gate `defaultCbShaderHygiene` to `!isPedPipe`; ship `vs_probe` | `main.cpp:1837-1878`, `1548-1575`, `2177` | Med–High (peds, if skin VS is being unbound) | 1.5 h | Med |
| 6 | PBR building env: never darken (`envW=0` when `skyLum < bodyLum`) and cap the lerp | `VehiclePBR_Modern.hlsl:1144-1185` | Med (Modern-only black walls) | 1 h | Low |
| 7 | PS2 vehicle body paint/ambient audit (not NULL texture) | `vehiclePipe.cpp:917-1195`, `:540-556` (PS2 `uploadLights` ambient; the Xbox path uses `:1509`) | Med (R2-04 residual black body) | 2 h | Med |
| 8 | Cloud blend/content fix (R2-02 pink quads) | `CloudsPS.hlsl`, `main.cpp:4605-4840` | Med (outdoor sky) | 3 h | Med |
| 9 | IV grade F1/F2 (display-space exposure, remove dead cap) | `postfx_colourfilter.cpp:1478-1517` | Med (IV mode only) | 0.5 h | Low |
| 10 | Baseline/mode provenance correction + per-run mode stamp | `docs/baseline/2026-10-25-screenshots-2200/README.md`, mode listener | Process (prevents repeat waste) | 0.5 h | None |

**Highest regression risk of the ten:**

1. **Any cull override / forcing BACK for peds** (`pedCullMode=2`, or changing
   `pipeEnterCullMode`). The code itself warns: the game legitimately uses CULLNONE
   for the mirror and CULLFRONT for stencil shadow volumes; forcing BACK draws the
   mirrored negative-determinant CJ inside-out and destroys shadow volumes
   (`pipelinecommon.cpp:1183-1191`). Keep `pedCullMode=-1` until the A/B says otherwise.
2. **Raising the ambient floor globally.** It touches every pipe and every time of
   day; the project already has a daylight-darkness/sepia-veil history. Gate it
   indoors and cap it; do not remove the outdoor night law.
3. **Forcing a ped VS/declaration.** A wrong declaration breaks all skinning. Only
   repair L3 from the L1 truth (never invent a declaration), and only after
   `vs_probe` shows a real cache/device mismatch.

Honourable mention: `pedAlphaTestEnable=0` is a **diagnostic**, not a fix. Shipping
it would reintroduce the wave-5 black/ghost-ped bug.

---

## 6. Recent waves that could have CAUSED a view-angle dependency

| Change | file:line | Why it can create angle dependence | Verdict |
|---|---|---|---|
| `pipeEnterCullMode` now preserves the **sanitized saved** cull instead of forcing BACK | `pipelinecommon.cpp:1193-1199`, `:1239-1274` | Every pipe span inherits whatever the last pass left. If a pass leaves CULLNONE (mirror) or CULLFRONT (shadow volume), buildings/vehicles/peds draw with it. | **Gate/fix.** Keep the 3-layer repair, but capture the cull at a stable frame boundary (or per-pipe canonical BACK) rather than the post-pass value. |
| `MirrorPass_ResyncCull` re-asserts the **post-mirror rw cache** | `mirrorPass.cpp:364-383`, calls at `:423-424`, `:433-434`, `:445-446`, `:461-462`, `:490-491` | The log proves it: `[CullResync] mirror exit rw=1` **×212** — it institutionalises the mirror’s NONE for the rest of the frame. Combined with the row above, the next pipe span is double-sided. | **Fix.** Save the pre-mirror cull at hook entry and restore that; do not read the cache after the original ran. |
| `defaultCbShaderHygiene` unbinds VS/PS at every default atomic, **including peds** | `main.cpp:1837-1878` | If the skin pipeline’s VS/declaration is set before the callback on any path, this is a direct ped-invisibility/garble cause. The log’s first `[DefCB]` shows `devVS=0`, so it is not proven — but it is the one unconditional shader write on the ped path. | **Gate to `!isPedPipe`** until `vs_probe` proves the skin VS is set after the callback. Props/pickups keep the hygiene. |
| `CClouds__Render_hygiene` entry force `ALPHAFUNC=ALWAYS` / `ALPHAREF=0` | `main.cpp:4739-4781`; fault path `:4783-4802`; restore `:4810+` | A skipped or faulted restore leaves every later alpha-cutout mesh accepting all texels → black sprites/decals covering the screen. The code’s own fault comment (`:4783-4788`) names that exact “whole screen covered outdoors” signature. | **Keep the SEH guard and verify the restore runs.** Do not set `cloudRenderFix=0` while diagnosing; that path skips the save/restore entirely. |
| `pedOpaqueContract` forces `ALPHATESTENABLE=TRUE` + `ALPHAREF=1` | `main.cpp:1076-1119`, `:1809-1834` | With the cloud-atlas leak (alpha 0 in large regions), this discards most of the ped. The contract itself is correct for solid skin; the **texture** is the variable. | **Keep the contract; fix the texture raster first.** Do not relax the alpha test globally. |
| `ClampAmbientToSky` indoor early-return | `pipelinecommon.cpp:81-93` | Correctly removes the night-law crush indoors, but `return`s before the absolute floor (`:158-168`), so the final indoor ambient is only the 0.05 PBR floor. | **Keep the return, move the floor above it** (or apply it on both paths). |
| `main_building` grazing-env guard | `VehiclePBR_Modern.hlsl:1150-1185` | New guard reduces the old “dark capture replaces the body” at grazing. The `lerp(color, skyEnv, envW)` still permits an 85% replacement; with `bldSkyReflect=0.15` the current INI cannot black out, but a user-raised value can. | **Keep, strengthen.** Make it a never-darken additive term, and gate `bldEnv.z` outdoors at the CPU (already done at `buildingPipe.cpp:2905-2908`). |
| `bldEnv.z` indoor CPU gate + indoor IBL suppression | `buildingPipe.cpp:2905-2908`, `pipelinecommon.cpp:1067-1076` | Correct: the sky capture is meaningless indoors. | **Keep.** |
| `pedCullMode` / `pedAlphaTestEnable` levers | `main.cpp:1710-1766`, `1076-1119` | Diagnostics added for this exact report. Defaults are safe (`-1` / `1`), but they are not fixes. | **Keep as diagnostics only.** Do not ship a non-default cull/alpha value without the A/B. |
| `stateBaselineEnable` | parsed/saved/menu | No consumer; a lane that cites it as protection is false comfort. | **Do not count it.** |

---

## 7. What I do not know

- **Which mode the user was actually in.** The INI says Xbox; the 22:00 screenshot
  window is PS2; the report may be from either, or from a later Modern test. The
  experiment must be run in the mode that reproduces, and the mode must be stamped
  into the log per frame.
- **Whether the user has built/deployed the 00:06 tree.** The deployed ASI is 21:57
  and lacks the new hooks (string-verified). The current report is against the old
  binary. Do not claim any 00:06 fix is “tested” until the ASI size/stamp changes.
- **What the black frame looks like.** No screenshot or log slice of the current
  report was provided. “Screen black” could be an overlay (hard edges, HUD visible),
  a black wall (texture/gradient preserved), or a lost RT (HUD also black). The
  pass-ID experiment answers this.
- **Whether the cloud raster leak actually reaches the ped draw in this RW build.**
  The save-list omission is real; the cache-hit mechanism needs the probe to confirm
  it is the *ped* that samples the atlas.
- **Whether the `0x708300` prologue bytes match on this exe.** The new log line
  (`entry bytes=… OK/MISMATCH`) will tell us; if another plugin hooked that entry,
  the hook will be skipped.
- **Whether the user’s ped model is vanilla.** A modded CJ/ped with mixed or
  one-sided winding can make cull evidence misleading even when the engine state is
  correct.
- **Whether other ASIs (VehDeform / VehIK) are affecting peds or vehicle panels.**
  The baseline caveat already flags them for the vehicle hover; they could also
  affect the ped skeleton.
- **The exact `CGame__currArea` behavior in tunnels/shelters.** The interior early
  return keys on `currArea != 0`; sheltered-but-outdoor areas still take the night
  law. That may be a separate dark-area class.

---

## Appendix — log evidence quick reference (live 22:35 run)

| Line | Evidence |
|---|---|
| 27 | `[BUILD] size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44` — binary under test |
| 47–48 | `[shadow_install] 0x53E0C3 … MISMATCH … SKIPPED` — stored shadow unhooked |
| 215 | `[PBR-Ambient] final=(0.050,0.050,0.050) floor=0.050 raw=(0.000,0.000,0.000)` |
| 225 | `[Ped] ped_amb_cmp … post=(0.200,0.200,0.200) floor=0.200` |
| 216–224 | `[PedOpaque] pre-entry dev ALPHAREF=2 … ZENABLE=0 … ZFUNC=8` → contract repairs before skin draw |
| 19/125 | `[Cull] enter saved=1` ×19, `saved=2` ×125 — no FRONT sampled |
| 212 | `[CullResync] mirror exit rw=1` ×212 — NONE leak |
| 20718 | `[PF] TonemapPass: … autoExp=0` — no exposure adaptation |
| 21997 | `[Mode] apply PS2` at 22:37:29 — screenshot window is PS2 |
| 55989 | `[Mode] apply Xbox` at 22:41:45 — only the last two shots are Xbox |
| 24293 | `[PostFX] CF-SRC … colorFilter=1 ivMode=0` — PC colour filter, not IV |
| 17825 | `[PostFX] DrawIVGrade … exposure=2.50` — 4-second GTA IV window only |
