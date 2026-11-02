# Device-Reset blackout — diagnosis

Date: 2026-10-25
Repo: `E:\dev(dave)\skygfx_plus_expIV`
Evidence session: `skygfx_dbg.prev.log` (15:06:07 → 15:07:34), screenshot
`gta_sa_IKKTOtzaLs.png` (taken 15:06:45.020)
Scope: read-only diagnosis. **No source file was modified.**

---

## 0. TL;DR

1. The blackout is **one frame**, not a persistent state. The screenshot at
   15:06:45 is the only black frame in ~700 frames of that session; 15:06:41.8
   (2.3 s earlier) and 15:06:50.9 (5.9 s later) both render normally
   (frame means 61,64,42 and 66,74,63).
2. **The world RT was uniform black** — 97.5% of the frame is exactly `(2,2,2)`,
   which is *post-tonemap black* (`[Tonemap] VAL … lift=0.012` ⇒ 0.012·255 ≈ 3).
   So the black entered the chain **before** the tonemap, i.e. the camera raster
   was black at the end of the geometry phase.
3. **The four lazy loaders are provably stale in this session**, but they are
   *not* this blackout: all 15 of their shader handles were created on the frame
   immediately **before** the Reset (`15:06:31.881`, `15:06:31.889`), the Reset
   ran at `15:06:31.94`, and they were never recreated — yet every consumer of
   those handles was gated **off** in the failing frame (`gates: 0/0/0`,
   `ivMode=0`, `mirrorDistortionFix=0`, `ssao=0`, `smaa=0`, `mb=0`, `hf=0`).
   They are a live landmine, not the trigger.
4. **The black radar is not part of this failure.** It is already black in the
   *normal* frame at 15:06:41 (2.3 s after the Reset) and it is normal at
   15:06:50 outdoors. It correlates with "player is indoors", not with the Reset.
   See §4 — the requested INTZ / SetDepthStencilSurface / RT-leak mechanisms are
   all **ruled out** for it.
5. The two highest-value structural defects surfaced by this dig are
   (a) the **unguarded `SetDepthStencilSurface(oldDS)` restore after
   `DepthHook_Restore()`** (can write NULL over a good depth bind → exactly this
   symptom, one frame) and (b) the **15 dead lazy shader handles with latched
   one-shot flags** (a whole class of post-Reset dead binds). Both are cheap to
   fix; see §6.

---

## 1. Evidence read-back (and three corrections)

### 1.1 Timeline recovered from the log

| time | event |
|---|---|
| 15:06:07.05 | DLL loaded, hooks installed |
| 15:06:08.78 | `CreateShaders` #1 (startup, 57 PS + 17 VS) |
| 15:06:31.76 | `DepthHook_Install` (first install; vtable hooks on device `0E221E00`) |
| 15:06:31.79 | **Frame 1** postfx: `screen size cache: 0x0 -> 1920x1080`, `[FX] shaders:` (14 handles), `[UW] Water_Underwater PS sh=2174DCC0`, PipeChain, CF, tonemap |
| 15:06:31.939 | `ReleaseDefaultPoolResources: releasing...` (pre-Reset) |
| 15:06:31.95 | `orig_Reset` runs (startup Reset; no failure line ⇒ **SUCCEEDED**) |
| 15:06:31.978 | `CreateShaders` #2 (`ps=57 vs=17 FAILED 0`) |
| 15:06:32.254 | `DepthHook: lazy INTZ recreate 1920x1080 after Reset` |
| 15:06:33.740 | `InitSSAOResources: re-aliased depthTex from depthhook after Reset` |
| 15:06:41.777 | screenshot `gta_sa_FDRceenY9f.png` — world normal, **radar map already black** |
| **15:06:45.020** | **screenshot `gta_sa_IKKTOtzaLs.png` — world black** |
| 15:06:50.896 | screenshot `gta_sa_QnH8dwMRg6.png` — world normal, radar map normal |
| 15:07:34.17 | DLL detach |

The Reset is a **startup** Reset (frame 1, right after `InitialiseGame_hook`
returns), not a user resolution change — the 16:41 session shows the identical
Reset at `16:41:45.78`, so this is the *normal* launch path. Any defect on this
path is hit on every launch; only the timing of its visible consequence varies.

### 1.2 Correction 1 — the 1 px bright L border is a capture artifact

The black frame has scene content in row `y=0` (1920 px wide, 240 distinct
colours) and in column `x=0` (781 non-black rows). That looks like a "1 px inset
fullscreen quad" — it is not. The **same** double-exposure border exists in every
normal screenshot:

| shot | row 0 | row 1 | col 0 | col 1 |
|---|---|---|---|---|
| `NrOxfdYkUy` (normal) | `(70,120,208)` uniform | `(57,106,179)` | `(70,120,208)` | `(57,106,179)` |
| `IKKTOtzaLs` (black) | scene | black | scene | black |

i.e. the outermost 1 px belongs to a *different* presented frame in every shot tested
(five files: the black frame plus four neighbour/other-session frames). Do not chase an off-by-one quad; the 1 px L is ShareX/present
behaviour and must be excluded from any pixel statistic (the (2,2,2) mean above
already excludes it).

### 1.3 Correction 2 — the radar map is black *before* the blackout

`gta_sa_FDRceenY9f.png` (15:06:41.777, world perfectly normal, in-game 10:17)
shows the same black radar: black disc, visible rim, visible `N`, visible player
blip. So the radar defect is present **while the world is fine**, 3.2 s *before*
the black frame, and it is gone at 15:06:50 (in-game 10:26, player outdoors,
radar drawn normally). It tracks the **interior**, not the Reset and not the
blackout. Details and mechanism verdicts in §4.

### 1.4 Correction 3 — the `[PF] HeightFog done: DepthHook_Restore` line does not
mean Restore ran

`DrawHeightFog` returns at `postfx.cpp:3893-3894` when `heightFogEnable` is 0 —
*before* `DepthHook_Suspend` (`:3972`). The `done` line at `postfx.cpp:1932` is
the caller's unconditionally-logged banner. In this session `heightFogEnable=0`,
so the two per-frame Suspend/Restore pairs are inside `DrawNormalBufferToTexture`
(`postfx.cpp:2798` / `:2838`) and `DrawPipeChain` (`:3336` / `:3553`). Both capture
`oldDS` **before** their Suspend and write it back **after** their Restore
(`:2755`→`:2858`, `:3322`→`:3569`) — the pattern behind cause C2 in §5.

### 1.5 What the black frame actually contained

Pixel analysis of `gta_sa_IKKTOtzaLs.png` (1920×1080):

* `(2,2,2)` — 2 021 446 px (97.5%) — whole world, sky, radar disc
* `(175,175,2)` — 14 191 px — the 3D mission marker cone (flat vertex colour)
* `(55,99,45)` — 6 505 px — money font
* `(200,200,200)` — 2 560 px — weapon HUD icon
* `(186,31,35)` — 1 720 px — health bar
* `(168,167,2)` / `(175,173,2)` — marker facet shading

Those HUD/marker colours are **identical** in the normal 15:06:41 shot
(marker `(175,175,2)`, health `(186,31,35)`), and the grade was identical in both
frames (`r=0.871 luma=0.207`), so the marker went through the same colour filter
and simply sat on top of a black world. Conclusion: the **entity geometry phase
produced nothing**, everything after it (marker sprite, postfx, HUD) worked.

---

## 2. Exhaustive D3D resource + shader inventory

### 2.1 The Reset call graph (what "the 12-subsystem list" actually is)

`hook_Reset` (`src/render/depthhook.cpp:336-431`):

| # | action | line |
|---|---|---|
| 1 | `OnDeviceReset()` → `ReleaseDefaultPoolResources()` | `depthhook.cpp:340` |
| 2 | release `g_gameDS` (game depth-stencil, cached) | `depthhook.cpp:343` |
| 3 | `UI_ImGuiInvalidateDeviceObjects()` | `depthhook.cpp:353` |
| 4 | `SkyNoise::ReleaseDeviceObjects()` | `depthhook.cpp:358` |
| 5 | `orig_Reset(dev, pParams)` | `depthhook.cpp:360` |
| 6 | `Pipeline_InvalidateShaders()` | `depthhook.cpp:387` |
| 7 | `CreateShaders()` | `depthhook.cpp:388` |
| 8 | `pipeApplyWorldBaseline("DeviceReset")` | `depthhook.cpp:395` |
| 9 | `CoronaWindow_Close()` | `depthhook.cpp:400` |
| 10 | `TexAlpha_OnDeviceReset()` | `depthhook.cpp:405` |
| 11 | `SkyNoise::OnDeviceReset()` | `depthhook.cpp:411` |
| 12 | `UI_ImGuiCreateDeviceObjects()` | `depthhook.cpp:415` |
| 13 | zero `_rwD3D9Last{Vertex,Pixel}ShaderUsed` + re-seed ZENABLE | `depthhook.cpp:421-426` |

`OnDeviceReset` (`postfx.cpp:2283-2317`) adds:
`ReleaseEnvMapResources` (`:2289`), `ReleaseVehiclePipeCaches` (`:2290`),
`ReleaseBuildingPipeCaches` (`:2291`), `ReleaseNeoCarPipeResources` (`:2292`),
`chars_OnDeviceReset` (`:2299`), `InvalidateCameraRasterCache` (`:2309`), and the
Im2D guard-state reset (`:2314-2316`).

`ReleaseDefaultPoolResources` (`postfx.cpp:2178-2274`) is the real payload; the
sub-releases are enumerated in table A below.

### 2.2 Table A — raw D3D9 render targets / textures we own

Legend: **pre** = released before `orig_Reset`; **post** = recreated lazily.
"yes" in *pre* means the *release call exists on the pre-Reset path*.

| resource | owner (file:line created) | pool | pre-Reset release | post-Reset recreate |
|---|---|---|---|---|
| `g_intzTex` / `g_intzSurf` (INTZ DS) | `depthhook.cpp:88-90`, `:246`, `:294` | DEFAULT | `DepthHook_ReleaseResources` `depthhook.cpp:599-600`, called from `postfx.cpp:2265` | lazy in `hook_SetDS` `depthhook.cpp:235-258` |
| `g_gameDS` (game DS, AddRef'd) | `depthhook.cpp:275-279` | (game) | `depthhook.cpp:343` | re-cached on next screen-sized `SetDS` |
| `g_ssaoDepthTex/Surf` (alias of INTZ) | `postfx_ssao.cpp:6-7`; alias `:48-50`, `:93-94`; own create `:112-114` | DEFAULT | `postfx.cpp:2185-2186` (only if not aliased) + `:2270-2271` | `InitSSAOResources` re-alias `postfx_ssao.cpp:46-52` |
| `g_prevDepthTex/Surf` (R32F) | `postfx_ssao.cpp:157-158` | DEFAULT | `postfx.cpp:2190-2191` | `CreatePrevDepthTexture` |
| `g_ssaoNoiseTex` | `postfx_ssao.cpp:67` | **MANAGED** | n/a (survives) | — |
| `g_ssaoOutputRaster` | `postfx_ssao.cpp:552` (RW) | DEFAULT-ish (CAMERATEXTURE) | `postfx.cpp:2196` | lazily recreated |
| `g_ssaoQuarterRaster[2]` + textures | `postfx_ssao.cpp:423-428` (RW) | CAMERATEXTURE | `ReleaseSSAOOverhaulResources` `postfx_ssao.cpp:397-408` → `postfx.cpp:2255` | `InitSSAOOverhaulResources` |
| `g_ssaoBlurTempRaster` + tex | `postfx_ssao.cpp:436` | CAMERATEXTURE | same `:397-408` | same |
| `g_normalBufferTex/Surf` | `postfx_ssao.cpp:29-30`, create `postfx.cpp:2571-2572` | DEFAULT | `postfx.cpp:2222-2223` | `GetNormalBufferTexture` |
| `g_prevNormalTex/Surf` | `postfx_ssao.cpp:40-41`, create `postfx.cpp:2597-2598` | DEFAULT | `postfx.cpp:2228-2229` | `GetNormalBufferTexture` |
| `g_iblTex/Surf` (quarter-res IBL) | `postfx.cpp:2155-2156`, create `:2362-2364` | DEFAULT | `postfx.cpp:2218-2219` | `GetIBLTexture` |
| `g_pipeChainTexA/SurfA`, `TexB/SurfB` | `postfx.cpp:2165-2168`, create `:2622-2623` | DEFAULT | `postfx.cpp:2233-2236` | `GetPipeChainTexture` |
| `g_pipeChainClassifyTex/Surf` | `postfx.cpp:2891-2892`, create `:2963-2964` | DEFAULT | `ReleasePipeChainClassifyResources` `:2920-2934` → `postfx.cpp:2241` | `GetPipeChainClassifySurf` (cleared on create `:2985-2992`) |
| `s_pipeChainFadeRas` (edge-fade 128²) | `postfx.cpp:2682-2701` (RW) | TEXTURE | `postfx.cpp:2926` | `GetPipeChainEdgeFadeRaster` |
| `GetPipeChainNoPackTex` 1×1 | `postfx.cpp:2642-2653` | **MANAGED** | n/a | — |
| `lastFrameBuffer` (VCS blur) | `postfx.cpp:809` (RW) | DEFAULT-ish | `postfx.cpp:2199` | `Blur_VCS` |
| `s_radiosityShaderWorkBuffer` | `postfx.cpp:986`, `:1023` (RW) | CAMERATEXTURE | `postfx.cpp:2207` | radiosity init |
| `s_radiosityWorkBuffer` | `postfx.cpp:987`, `:1212` (RW) | CAMERATEXTURE | `postfx.cpp:2208` | radiosity init |
| VCS radiosity RTs | `ReleaseVCSRadiosityResources` `postfx.cpp:540` | CAMERATEXTURE | `postfx.cpp:2215` | pass re-init |
| `g_smaaAreaTex`, `g_smaaSearchTex` | `postfx_smaa.cpp:119-120`, create `:140`, `:163` | DEFAULT (DYNAMIC) | `postfx.cpp:2211-2212` | `GenerateSMAAAreaTex/SearchTex` |
| SMAA edge/blend/prev/scene rasters + textures | `postfx_smaa.cpp:7-9`, `:22`, `:24-25` | CAMERATEXTURE | `ReleaseSMAAStaticResources` `postfx_smaa.cpp:90-118` → `postfx.cpp:2249` | `SMAATryInitRasters` |
| `g_velocityTex/Surf` | `postfx_motionblur.cpp:9-10`, create `:61-62` | DEFAULT | `ReleaseVelocityBufferResources` `postfx_motionblur.cpp:15-22` → `postfx.cpp:2252` | `DrawVelocityBuffer` |
| `g_lumaMeasTex/Surf` | `postfx_luminance.cpp:11-12`, create `:30` | DEFAULT | `ReleaseLuminanceTargets` `postfx_luminance.cpp:240-252` → `postfx.cpp:2244` | `EnsureLuminanceTargets` |
| `g_lumaAdaptTexA/B` + surfs | `postfx_luminance.cpp:13-16`, create `:47`, `:72` | DEFAULT | same `:240-252` | same |
| DOF CoC / Bokeh / Tent (tex+surf) | `postfx_newfx.cpp:174-176`, create `:191-199` | DEFAULT | `ReleaseFxScratchResources` `postfx_newfx.cpp:240-244` → `postfx.cpp:2259` | `FxEnsureDofRTs` |
| Bloom mips 0..5 (tex+surf) | `postfx_newfx.cpp:177-178`, create `:191-199` | DEFAULT | same `:240-244` | `FxEnsureBloomRTs` |
| Forward+ index A/B, pos/col atlases ×2 | `forwardplus.cpp:60-61`, `:73-74`, create `:298`, `:304`, `:325`, `:340` | DEFAULT | `ForwardPlus_ReleaseResources` `forwardplus.cpp:277-291` → `postfx.cpp:2262` | `EnsureFPResources` |
| Forward+ debug VB ×2 | `forwardplus.cpp:1404-1405`, create `:1505`, `:1511` | DEFAULT | `FpDbgReleaseVBs` `forwardplus.cpp:1448` | lazily on overlay draw |
| `g_charsClassifyTex/Surf` + classify raster | `src/entities/chars.cpp:247` | DEFAULT | `chars_OnDeviceReset` `chars.cpp:703-727` → `postfx.cpp:2299` | `EnsureClassifyBuffer` |
| SSS blur rasters A/B | `chars.cpp` (`sssBlurRasterA/B`) | CAMERATEXTURE | `chars.cpp:720-724` | `chars_OnDeviceReset` latch cleared |
| `s_ivGradeRas` | `postfx_colourfilter.cpp:1329`, create `:1440` | CAMERATEXTURE | `postfx.cpp:2203` | `DrawIVGrade` |
| `envFB_prev` | `envmap.cpp:21`, create `:124` | CAMERATEXTURE | `ReleaseEnvMapResources` `envmap.cpp:25-30` → `postfx.cpp:2289` | `BlendEnvMapTemporal` |
| `envFB`, `envZB`, `reflectionTex`, `reflectionCam` | `envmap.cpp` (`MakeEnvmapRasters`) | RW-owned | **RW restore** (not our list); `envFB_prev` only | `MakeEnvmapRasters` self-heals on size mismatch |
| `SkyNoise` universal tile | `noise.cpp:717-719` | DEFAULT | `depthhook.cpp:358` → `noise.cpp:830` | `ensureTexture` from cached CPU bytes; stats reset `noise.cpp:841-859` |
| ImGui DX9 VB/IB + font atlas | ImGui backend (vendored) | DEFAULT | `depthhook.cpp:353` → `debugmenu_theme.cpp:274` | `depthhook.cpp:415` → `debugmenu_theme.cpp:282` |

Transient surfaces (not Reset-scoped, safe): `postfx.cpp:258` and
`postfx_ssao.cpp:477` create `D3DPOOL_SYSTEMMEM` staging surfaces and release them
in-function.

### 2.3 Table B — **not** in the Reset list (the actual gaps)

| resource | file:line | why it matters |
|---|---|---|
| `g_sceneTexture` (water refraction RT) | create `waterPipe.cpp:622-623` | `D3DPOOL_DEFAULT`. Only freed by `waterPipe_shutdown` (`:569-579`, DLL detach) or `SyncDevice` (`:581-615`, only when `TestCooperativeLevel() != D3D_OK` **at flush time**). If it is alive when `Reset` is called, D3D9 returns `D3DERR_INVALIDCALL` → the documented RW-restore crash at `0x7F9A3C`. |
| `g_depthTexture` (water R32F) | create `waterPipe.cpp:635-636` | same as above |
| `g_reflTexture` (water reflection) | create `waterPipe.cpp:667-668` | same as above |
| 14 FX shaders (`s_fx.*`) | `postfx_newfx.cpp:92-105` | dead handles + `s_fxLoadTried` latch = never recreated |
| `s_uwPS` (underwater) | `postfx_underwater.cpp:283-289` | dead handle + `s_uwLoadTried` latch |
| `mirrorCompositePS` | `mirrorPass.cpp:115-118` | dead handle + `mirrorPSLoadTried` latch |
| `s_grassInstPS` | `PC_GrassInstancer.cpp:107-123` | dead handle + `s_grassInstPSTried` latch (compiled out today — `IDR_GRASSINSTANCED` is **not** defined in `resources/resource.h`, so it is always NULL; landmine only) |
| `s_ivGradePS` | `postfx_colourfilter.cpp:1332-1352` | raw `dev->CreatePixelShader` handle + `s_ivGradePSFailed` latch. Never created in this session (`ivMode=0`) |
| `grassPixelShader` | `main.cpp:2340` (only created in `CPlantMgr_Initialise`, `:2331-2351`) | dead after Reset; bound at `main.cpp:797` when `ps2ModulateGrass=1` (0 in this INI) |
| 5 pipelinecommon lazy accessor handles | `pipelinecommon.cpp:66-69`, `:736-802` | `s_customBuildingVS`, `s_normMapBuildingPS`, `s_normMapVehiclePS`, `s_vectorMotionBlurPS`, `SkinPBR` (`Pipeline_GetSkinSSSPS`) — all latched with `static bool attempted`. **Zero call sites today** (grep: only defined, never called), so not live yet |

### 2.4 Table C — shader handles

* **Batch** (`CreateShaders`, `pipelinecommon.cpp:824-984`): 57 PS + 17 VS = **74**
  handles. All recreated by `Pipeline_InvalidateShaders()` (`:817-822`) →
  `CreateShaders()` because `makePS`/`makeVS` (`:674-699`, `:701-724`) test
  `*sh == NULL || s_forceShaderRecreate` and set `*sh = NULL` first, so a failed
  recreate cannot leave a dead pointer behind. Verified in the log:
  `[INIT] shaders: compiled OK (ps=57 vs=17), FAILED (ps=0 vs=0)` after the Reset,
  with **all-new addresses** (`0E72D508` → `217890E8` for VehiclePBR_Modern).
  The dead pre-Reset handles are intentionally **not** Released
  (`pipelinecommon.cpp:816`).
* **Out-of-batch** (Table B): 14 + 1 + 1 + 1 + 1 + 1 + 5 = 24 handles, of which
  **15 were proven dead in this session** (14 FX + underwater).

### 2.5 RW-managed state that Reset also clears (for completeness)

`_rwD3D9LastVertexShaderUsed` `0x8E2448`, `_rwD3D9LastPixelShaderUsed` `0x8E244C`
are zeroed at `depthhook.cpp:421-422`; the related `0x8E2444`
(`_rwD3D9LastVertexDeclarationUsed`) and `0x8E2440` (FVF) are **not** zeroed
after Reset (they are zeroed in the water path at `main.cpp:4894-4895`). If a
post-Reset draw's declaration happens to equal the stale cached one, RW's
value-compare will skip `SetVertexDeclaration` on the first draw of a geometry.
Low risk (RW's restore normally re-creates declarations), but it belongs in the
same table.

---

## 3. Stale-handle list — ranked by how visible the failure is

Rank = "how bad does it look when this handle is used post-Reset".

| # | handle(s) | latch that blocks recreation | visible symptom when bound dead | live today? |
|---|---|---|---|---|
| 1 | `grassPixelShader` (`main.cpp:124/2340`) | none — `CPlantMgr_Initialise` runs once at game init | grass patches vanish or inherit the previous PS for every grass atomic (`grassRenderCallback` at `main.cpp:797`). Affects *world* geometry. | only if `ps2ModulateGrass=1` (INI now 0) |
| 2 | `s_ivGradePS` (`postfx_colourfilter.cpp:1319`) | `s_ivGradePSFailed` + non-NULL check `:1335` | the IV grade fullscreen pass runs with a dead PS: `PfBindPS` fails, the *previous* PS stays bound, and the pass paints the whole frame with the colourfilterVerts quad using the wrong shader → **full-screen wrong/black** (this is the only lazy handle that can black the *whole* frame). | only if `ivMode=1` (0 here) |
| 3 | 14 FX handles (`postfx_newfx.cpp:78-111`) | `s_fxLoadTried` `:85-91` | DOF/bloom/sun-shafts/FXAA3/grain/CA/vignette passes write garbage into the camera raster; **TonemapLUT** (used at `postfx_colourfilter.cpp:602-603` when `tonemapOperator>0`) would bind dead → tonemap pass reads a wrong PS → whole-frame wrong output. | gates were 0 in the failing frame; `tonemapOperator=0` |
| 4 | `s_uwPS` (`postfx_underwater.cpp:140`) | `s_uwLoadTried` `:285-289` | the underwater fog quad draws with `overrideIm2dPixelShader` = dead handle → every Im2D dispatch in that pass re-applies it (`Im2dSetPixelShader_hook`), full-screen garbage while submerged. | only below a water plane (`waterUnderwaterFog=1` in INI, but never drew in this session) |
| 5 | `mirrorCompositePS` (`mirrorPass.cpp:97`) | `mirrorPSLoadTried` `:115-118` | mirror composite skipped (fail-open, `:119-126`) → vanilla mirror blit stands. Benign. | only when `mirrorDistortionFix=1` (0 here) |
| 6 | `s_grassInstPS` (`PC_GrassInstancer.cpp:71`) | `s_grassInstPSTried` `:107-109` | grass instancing falls back to fixed-function (by design, `:106` `#ifdef`). | compiled out |
| 7 | `s_customBuildingVS` … `SkinPBR` (`pipelinecommon.cpp:66-69`, `:736-802`) | `attempted` statics | would bind dead handles in the norm-map building/vehicle and SSS paths. | **no call sites** today |

### 3.1 Why the latches defeat `Pipeline_InvalidateShaders`

`Pipeline_InvalidateShaders` (`pipelinecommon.cpp:817-822`) only sets
`shadersCreated = false` and `s_forceShaderRecreate = true`; it cannot see the
file-static handles above because they are filled by their *own* `makePS` call
sites behind a `static bool` one-shot. Once that flag is true the loader returns
the cached pointer forever, so a dead handle is returned on every subsequent
frame. The log proves it happened:

```
[15:06:31.881] [FX] shaders: dof=0E884140/2174E3C8/2174D298/2174DD88 lut=2174D5B8 bloom=...
[15:06:31.889] [UW] Water_Underwater PS res=276 sh=2174DCC0
[15:06:31.939] ReleaseDefaultPoolResources: releasing...      <- Reset
[15:06:31.978] CreateShaders started
[15:06:32.242] CreateShaders: done                            <- 74 batch handles rebuilt
```

`[FX] shaders:` and `[UW]` never appear again in the remaining 4 900 log lines.
Those 15 pointers are dead for the rest of the session.

---

## 4. Radar corruption — verdict: **not ours, not this bug**

### 4.1 Facts

* `gta_sa_FDRceenY9f.png` (15:06:41.777, **world normal**, 2.3 s after the Reset):
  radar = black disc, rim visible, `N` visible, blip visible. Region mean
  (37.5, 40.9, 8.9), σ ≈ 28.
* `gta_sa_QnH8dwMRg6.png` (15:06:50.896, outdoors): radar = fully drawn map,
  region mean (93.3, 100.0, 88.5), σ ≈ 82.
* `gta_sa_NrOxfdYkUy.png` (16:44:43, **different session**, Xbox pipe): radar
  normal, mean (76.2, 81.3, 68.1).
* The three 15:06 shots differ only in *where the player is* (indoors at 10:17
  and 10:21, outdoors at 10:26). The Reset is a constant across all three.

⇒ The radar map blackness **predates the blackout by ≥3 s and is not caused by
the Reset**.

### 4.2 Ruling out the three mechanisms in the brief

**(a) INTZ depth-stencil aliasing — ruled out.** The radar/HUD is drawn *after*
`RenderEffects`/`CPostEffects::Render`; the code itself states it
(`main.cpp:3566-3572`: "HUD/radar/dialog sprites draw after this"). At that point
`DepthHook_Restore()` has closed the window and the `N`/blip sprites are drawing
at their correct screen positions — so the depth-stencil state is, at minimum,
not blocking 2D. A depth-stencil can only black a 3D pass; the radar map contents
are 2D blits.

**(b) `SetDepthStencilSurface` juggling — ruled out** for the same reason. Also,
a wrong DS would have to survive from the postfx pass to the HUD dispatch, and the
HUD's other textured sprites (weapon icon, money, `N`, blip) all render.

**(c) Leaked render-target binding — ruled out by the sprites themselves.** The
radar map quad, the `N`, and the player blip are submitted by the *same* 2D
dispatcher with the same RT. If the RT were wrong (or the viewport/scissor
poisoned), the `N` and blip would be missing or displaced too. They are pixel
correct. What is black is specifically the **map texture content**, i.e. a
game-side radar-texture/tile-streaming artifact.

### 4.3 Most likely actual cause (for completeness)

The map tiles are streamed from `radar00..63.txd`. The map goes black exactly
while the player is inside an interior (where the streamed tile set around the
interior is not resident/refreshed) and recovers the moment the player is
outdoors and the tiles stream back in. That is a game/texture-streaming behaviour,
not a device-object lifetime bug of ours. Nothing in `skygfx_dbg.prev.log`
correlates with it (`[TEXDB]`/stream lines absent; no `[RasterInit]` skip for the
radar).

**Action: do not spend Reset-hygiene work on the radar.** If it must be chased
later, the cheap discriminator is a vanilla-install screenshot of the same
interior: if vanilla also shows a black map indoors, it is retail behaviour.

---

## 5. Ranked cause list for the world blackout

Method note: the frame is *uniformly* `(2,2,2)` **after** the tonemap's
`lift=0.012` (`postfx_colourfilter.cpp:441-501`, `[Tonemap] VAL exp=1.219 …
lift=0.012`). A stale/temporal blend cannot produce a flat field; a constant
black source can. So the black was already in the **camera raster before
`UpdateFrontBuffer`/grading/tonemap**, and it is **one frame**. Everything below
is ranked by how well it explains *that specific* signature. All of them are
one-frame-transient classes; none of them is a persistent state, which is
consistent with the 15:06:50 recovery.

### C1 — `UpdateFrontBuffer` copied/left a black `pRasterFrontBuffer` (the whole
frame's only scene source) — **30%**

`ColourFilter_Modern` reads **`pRasterFrontBuffer`** (not the camera raster) and
writes the camera raster (`postfx_colourfilter.cpp:196`, draw at `:285`, log at `:287`). The chain is
`camRas → UpdateFrontBuffer → pRasterFrontBuffer → grading → camRas → UpdateFrontBuffer
→ tonemap → camRas`. One failed/undefined copy of the world frame into
`pRasterFrontBuffer` turns the entire output black (the marker survives because it
was drawn into `camRas` before the first copy; the HUD is drawn after all of it).
This is exactly the class commit `ba8c997` ("copy front buffer before grading to
prevent black loop") patched once already.

Why after a Reset: `pRasterFrontBuffer` is an RW video-memory raster. RW
destroys/re-creates its D3D surface during the Reset; its content is undefined
until our first copy. `UpdateFrontBuffer` (`postfx.cpp:488-535`) can also **skip
the copy entirely**:

```c
if(GetScreenSize(&ufbScrW,&ufbScrH) &&
   (camRas->width != ufbScrW || camRas->height != ufbScrH)) { … return; }   // postfx.cpp:508-517
```

and the log proves transient non-screen camera rasters occur *inside* the frame
(`[BUILDING] cb camRas=04C511C8 512x512 != screen 1920x1080`, once per frame).
A skip leaves the previous (graded) frame, not black — but a *failed*
`RwRasterRenderFast` into the freshly re-created surface would leave undefined
(driver-black) content, and the next grading/tonemap pass paints it over the
world.

**Probe (one line, decisive):** after the `UpdateFrontBuffer` copy at
`postfx.cpp:523-525`, when the dump is armed, read back the mean of
`pRasterFrontBuffer` (`DumpCurrentRT("fb_after_copy")` already exists at
`postfx.cpp:531-534`). The black frame will show `fb_after_copy` mean ≈ 0 while
`[PF] PipeChain ran: … classifyBegins=128` proves the geometry drew. If
`fb_after_copy` is a normal frame, C1 dies.

### C2 — depth-stencil left NULL/foreign while `ZENABLE=TRUE` → all depth-tested
entity draws rejected — **25%**

Every postfx pass does the following dance:
`GetDepthStencilSurface(&oldDS)` … `DepthHook_Suspend()` (which unbinds the DS,
`depthhook.cpp:509`) … `DepthHook_Restore()` (rebinds `g_gameDS`/INTZ, `:550-581`)
… then **writes `oldDS` back over the restored bind**:
`postfx.cpp:3569`, `postfx.cpp:2540`, `postfx.cpp:2858`,
`postfx_ssao.cpp:262`, `:390`, `postfx_newfx.cpp:422`,
`postfx_luminance.cpp:66`, `:90`, `:161`.

If `oldDS` was NULL (or a foreign/small surface) at capture time, that last write
replaces a good INTZ bind with NULL. The device then holds
`DS = NULL, ZENABLE = TRUE` — the exact state the depthhook comments call out:

> `depthhook.cpp:111-116` — "leaving DS=NULL with ZENABLE=TRUE: every draw fails
> and the world goes blank."

The next frame's world pass draws nothing; the next successful `SetDS`
(e.g. `RwCameraBeginUpdate` of the envmap camera then the main camera, or the
next `DepthHook_Restore`) repairs it — one black frame. The marker caveat (it
survives) is the only reason this is not #1: on native D3D9 a NULL DS rejects the
marker too, unless the marker path runs with Z off. Under DXVK/Vulkan a
missing depth attachment disables the depth test rather than failing draws — in
that case C2 would *not* black the world and should be discarded.

**Probe:** log the DS at `RenderScene_before` and at the frame tail:

```c
IDirect3DSurface9 *ds=NULL; dev->GetDepthStencilSurface(&ds);
D3DSURFACE_DESC d; if(ds && SUCCEEDED(ds->GetDesc(&d))) … ;   // log ptr+WxH
```
plus the HRESULT of every `SetDepthStencilSurface`. The black frame shows
`ds=NULL` (or `ds=512x512`) where the normal frames show `ds=1920x1080 INTZ`.

### C3 — geometry phase drew into the wrong surface (classify bracket / envmap
camera-raster swap) — **20%**

`PipeChain_ClassifyBegin` swaps the device RT to the classify texture **for every
glossy mesh** (`postfx.cpp:3069`), forces blend/fog/ZWRITE off (`:3075-3083`), and restores it in
`PipeChain_ClassifyEnd` (`postfx.cpp:3178`). The building path closes it on every
exit (`buildingPipe.cpp:1928-1942`), and both Begin/End have fault belts
(`postfx.cpp:3020-3023`, `:3101-3106`, `:3214-3216`). A single missed End leaves
the classify RT bound for the rest of the geometry phase → every following entity
is drawn into a look-alike 1920×1080 surface → invisible; the marker (a separate
Im3D flush) is drawn after the surface is restored. The envmap reflection pass
(`RenderSphereReflections`, `[BUILDING] cb camRas=… 512x512`) repoints the camera
raster mid-frame every frame, which is a second way for Begin/End to capture the
"wrong" RT.

**Probe:** at `RenderScene_before`, before/after every `ClassifyBegin`, and at the
marker flush, log `GetRenderTarget(0)` and compare against
`RwRasterGetNativeSurface(RwCameraGetRaster(Scene.camera))`. The black frame shows
one mismatch (classify or envmap surface live past its bracket).

### C4 — dead lazy/raw shader handle bound in a world path — **15%**

Proven-dead handles exist (15 of them, §3), and two of them paint the *whole
frame* when they fire (`s_ivGradePS`, the FX tonemap LUT). They are ranked low for
*this* frame only because every consumer was gated off, as the log states:
`[PF] FX pre-tonemap group: … (gates: 0/0/0)`, `ivMode=0`, `mb=0`, `hf=0`,
`ssao=0`, `smaa=0`, `mirrorDistortionFix=0`, `ps2ModulateGrass=0`. It becomes #1
the moment any of those features is enabled, **or** after a Reset when
`grassPixelShader`/`s_ivGradePS` were created pre-Reset.

**Probe:** at every `PfBindPS`/`RwD3D9SetPixelShader`/`SetVertexShader`, log the
handle; then in the black frame look for a handle that is *not* in the post-Reset
`CreateShaders` address range. Kill it permanently by adding the invalidation
hooks in F1.

### C5 — a full-screen quad drawn with a black/unbound source (corona "black
quad", feedback lock) — **10%**

The build already carries a shield for one instance of this class
(`[CoronaShield] forced additive, was src=5 dst=6 blend=0` — a corona quad drawn
with blending disabled writes opaque black; log tag literally says
`black-quad=blend 0`). The same shape appears whenever a fullscreen pass samples a
texture that is simultaneously bound as the render target (feedback lock), e.g.
`pRasterFrontBuffer` left on s0 across `UpdateFrontBuffer`'s
`RwRasterRenderFast` into it (`postfx.cpp:523-525`, `:881-883`, `:920-922`), or the
INTZ/velocity/normal RT left on a stage across a pass boundary. The code has many
manual unbinds for exactly this reason (`postfx.cpp:3550`, `:3834`,
`postfx_ssao.cpp:633`, `postfx_smaa.cpp:331`, `buildingPipe.cpp:2594`).

**Probe:** at pass exit, `GetTexture(0..7)` and compare each against the current
RT/DS; log any equality (`[FEEDBACK] stage=%u %p == RT`). The black frame shows
the match.

### C6 — uninitialized D3DPOOL_DEFAULT target read before its first write after
Reset — **5%**

All the post-Reset RTs are recreated lazily and **not cleared** on create
(`GetPipeChainTexture` `postfx.cpp:2607-2627`, `GetIBLTexture` `:2362`,
`GetNormalBufferTexture` `:2571`, `postfx_newfx.cpp:191`; the classify RT and the
luminance/DOF/bloom targets are the exceptions). A frame that reads one before
writing it sees driver-defined content (commonly black). This requires an
unlucky gate combination and would normally self-heal in one frame.

**Probe:** temporarily clear every freshly created DEFAULT-pool RT to
`0xFF00FF` (magenta) in each `CreateTexture` wrapper. If the blackout turns
magenta, the source is an uninitialized RT; if it stays black, discard C6.

### 5.1 What is *not* the cause (evidence-based eliminations)

* **A failed Reset / RW restore.** No `DepthHook: orig_Reset FAILED` line; the
  Reset succeeded (`CreateShaders` re-ran with 0 failures, new handle addresses,
  `lazy INTZ recreate` at `15:06:32.254`).
* **`Pipeline_InvalidateShaders` forgetting a batch handle.** All 74 were
  recreated (`[INIT] shaders: compiled OK (ps=57 vs=17)`).
* **A stale RW VS/PS cache suppressing rebinds.** `depthhook.cpp:421-422` zeroes
  both, and the log's `[DefCB] shader hygiene` path proves the cache transitions
  are being observed.
* **INTZ not recreated.** `[DepthHook] lazy INTZ recreate 1920x1080 after Reset`
  + `InitSSAOResources: re-aliased depthTex from depthhook after Reset`.
* **The radar.** See §4.
* **Sustained over-darkening (ambient/grade).** The grade parameters are identical
  in the black and the neighbouring normal frames (`r=0.871 luma=0.207`,
  `[PBR-Ambient] final=(0.050,…)`), and the marker's graded colour is
  byte-identical — the postfx is not over-darkening, the input is black.

---

## 6. Fix list (ordered; exact change; risk)

> None of these are applied. Each cites the file:line to change.

### F1 — Make the lazy shader handles Reset-aware (highest value, lowest risk)

Add one invalidator per owner and call it from the post-Reset block
(`depthhook.cpp:386-415`, next to `Pipeline_InvalidateShaders`):

| file | add | call from |
|---|---|---|
| `postfx_newfx.cpp` after `:111` | `void FxInvalidateShaders(void){ s_fx = FxShaders{0}; s_fxLoadTried = false; }` | `depthhook.cpp:387` area |
| `postfx_underwater.cpp` after `:289` | `void PostFX_UnderwaterInvalidateShader(void){ s_uwPS = NULL; s_uwLoadTried = false; }` | same |
| `mirrorPass.cpp` after `:118` | `void MirrorPass_InvalidateShader(void){ mirrorCompositePS = NULL; mirrorPSLoadTried = false; }` | same |
| `PC_GrassInstancer.cpp` after `:123` | `void CGrassInstancer_InvalidateShader(void){ s_grassInstPS = NULL; s_grassInstPSTried = false; }` | same |
| `postfx_colourfilter.cpp` after `:1352` | `void PostFX_IVGradeInvalidateShader(void){ s_ivGradePS = NULL; s_ivGradePSFailed = false; }` | same |
| `main.cpp` (`grassPixelShader`) | factor the `RwD3D9CreatePixelShader` out of `CPlantMgr_Initialise` (`:2338-2341`) into `Grass_EnsurePixelShader()`; add `Grass_InvalidatePixelShader()` | same, plus call `Grass_EnsurePixelShader` from `grassRenderCallback` when `ps2ModulateGrass` |

Rules: **null the pointer, do not `Release()` it** — the D3D shader object is
already dead; `Release()` on it is the documented hazard the batch path avoids
(`pipelinecommon.cpp:816`). Reset the latch **after** the successful creation in
each loader (set `Tried` only once `makePS` produced a non-NULL handle), so a
missing resource can retry after a config change.
Risk: **low**. Failure mode if half-done: one extra log line per session.

### F2 — Release the water RTs before Reset (fixes a Reset-failure/crash class)

* `waterPipe.cpp`: add
  `void waterPipe_OnDeviceReset(void)` that releases `g_sceneTexture`,
  `g_depthTexture`, `g_reflTexture` and zeroes `g_sceneWidth/Height`,
  `g_depthWidth/Height`, `g_reflWidth/Height` (same body as the `SyncDevice`
  device-lost block, `waterPipe.cpp:604-614`). Keep `g_detailTex` (MANAGED).
* Call it from `OnDeviceReset()` (`postfx.cpp:2286`, before
  `ReleaseDefaultPoolResources` or right after) and declare it in `skygfx.h`
  next to `ReleaseDefaultPoolResources` (`skygfx.h:1062`).
Risk: **low**. Water already recreates every RT lazily in `EnsureSceneTexture`
(`:617`), `EnsureDepthTexture` (`:629`), `EnsureReflTexture` (`:661`).

### F3 — Never write a NULL/foreign `oldDS` back over a good bind

In each of the following, guard the restore with `if(oldDS)` and, when it is
NULL, re-bind the authoritative DS instead (call `DepthHook_Restore()` or
`orig_SetDS(g_gameDS)`), then `Release()` the ref:

* `postfx.cpp:3569` (DrawPipeChain tail) — highest impact, runs every frame
* `postfx.cpp:2540` (IBL), `postfx.cpp:2858` (normal buffer)
* `postfx_ssao.cpp:262`, `:390`
* `postfx_newfx.cpp:422`
* `postfx_luminance.cpp:66`, `:90`, `:161`

Also log the `HRESULT` of these calls (throttled): a failed
`SetDepthStencilSurface` in the middle of these tail restores is currently
invisible.
Risk: **low**, but it touches every postfx pass — verify by running the debug
dump (`F10`) for one frame per pass. This is the fix that closes C2 and the
whole "one-frame world blank" class described in `depthhook.cpp:111-116`.

### F4 — Do not force `ZENABLE=TRUE` at the end of `hook_Reset` without a DS

`depthhook.cpp:424-426`. Change to: query `GetDepthStencilSurface`; only write
ZENABLE=TRUE if a DS is bound, else write FALSE and let the first
`hook_SetDS`/`DepthHook_Restore` restore it (it already re-seeds both layers).
Risk: **low**; a wrong ZENABLE only affects the first frame's markers/HUD, and
the RW cache is re-seeded anyway.

### F5 — Convert shader invalidation from "one-shot flag" to a handle registry

`pipelinecommon.cpp:674-724`: have `makePS`/`makeVS` append `sh` to a fixed
`void **g_shaderHandles[192]` table (they already receive the address, so it is
one line each). Then `Pipeline_InvalidateShaders` (`:817-822`) can iterate the
table and call the right create function — or simply be trusted to cover the
batch — and the *lazy* loaders can register through the same API in F1 instead of
each exporting its own invalidator.
Risk: **medium** (fixed table size; keep the existing per-loader invalidators as
the near-term fix). This is the only change that prevents the *next* forgotten
loader.

### F6 — Black-frame tripwire (permanent instrumentation)

In `postfxReportSummary` (`postfx.cpp:448-470`, called at `:1773` / `:1889`) or at the tail
of `ColourFilter_switch`: when a new INI key (e.g. `SkyGfx.blackFrameProbe=1`)
is set, read back a 16×16 sparse grid of the camera raster and log
`[BLACKOUT] frame=%d mean=%.2f pRasterFrontBuffer=%p ds=%p rt=%p` when the mean
< 1/255 *and* `classifyBegins > 0` (geometry did draw). One line gives the
timestamp to align with a screenshot. Use `GetRenderTargetData` on a
`D3DPOOL_SYSTEMMEM` staging surface exactly like `DumpCurrentRT`
(`postfx.cpp:250-285`).
Risk: **low** (off by default; ~0 cost when off).

### F7 — Also zero the remaining RW caches after Reset

`depthhook.cpp:421-422` → add `*(void**)0x8E2444 = NULL;` (last vertex declaration)
and `*(void**)0x8E2440 = (void*)0;` (last FVF), matching what the water path
already does at `main.cpp:4894-4895`.
Risk: **low**; forces one extra `SetVertexDeclaration` on the first draw.

### F8 — `s_forceShaderRecreate` early-return safety

`pipelinecommon.cpp:827-830`: if `CreateShaders()` returns early with
`shadersCreated == true` while `s_forceShaderRecreate == true`, the flag stays set
and every later `makePS` recreates forever. Today `Pipeline_InvalidateShaders`
sets `shadersCreated = false` first, so it cannot happen — add
`s_forceShaderRecreate = false;` in the early-return branch as a tripwire.
Risk: **none**.

### F9 — Do NOT "fix" the radar here

No source change is justified by the evidence (§4). If a future report shows the
radar black *outdoors* or black *after a Reset while outdoors*, re-open with the
probe `GetTexture`/RT/DS dump at the HUD dispatch — but the two known-black shots
are both indoors.

---

## Appendix A — the exact log evidence for the stale handles

```
[15:06:31.838]   makePS(265): creating pixel shader...
[15:06:31.881] [FX] shaders: dof=0E884140/2174E3C8/2174D298/2174DD88 lut=2174D5B8
                bloom=2174E300/2174E878/2174E7B0/2174D810 shafts=2174D360
                fxaa=2174D4F0 grain=2174D8D8 ca=2174DE50 vig=2174E0A8
[15:06:31.889] [UW] Water_Underwater PS res=276 sh=2174DCC0
[15:06:31.939] ReleaseDefaultPoolResources: releasing...
[15:06:31.942] ReleaseSMAAStaticResources: done
[15:06:31.954] DepthHook: Release tex=1F06C8E0
[15:06:31.957] [RENDER] camera-raster cache invalidated (04C4EFC0 1920x1080) — device reset / resolution change
[15:06:31.978] CreateShaders started
[15:06:32.241] [INIT] shaders: compiled OK (ps=57 vs=17), FAILED (ps=0 vs=0)
[15:06:32.254] DepthHook: lazy INTZ recreate 1920x1080 after Reset
```

No `[FX] shaders:`, no `[UW]`, and no `makePS(260..276)` after `15:06:32.254`
(those IDs are the FX pack 260-274 and the underwater PS 276; the only 27x
ID in `CreateShaders` is 279/velocity, recreated at 15:06:32.191).

## Appendix B — how the screenshot numbers were produced

```python
from PIL import Image
im = Image.open(r"...\gta_sa_IKKTOtzaLs.png").convert("RGB")
# exclude the 1 px capture border (row 0 / col 0) — see §1.2
crop = im.crop((1, 1, 1920, 1080))
# 97.5% of crop == (2,2,2); marker == (175,175,2); health == (186,31,35)
```

Cross-checks used in this document:

* neighbour frames: `FDRceenY9f.png` (15:06:41.777), `QnH8dwMRg6.png`
  (15:06:50.896), `cZzsJi9Ca1.png` (15:06:56), plus `NrOxfdYkUy.png` (16:44:43,
  other session) for the radar baseline;
* in-game clock in the shots: 10:17 / **10:21** / 10:26 (1 real second = 1
  in-game minute), which proves the game was not paused/faded during the black
  frame.
