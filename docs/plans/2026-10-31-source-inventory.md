# Source Inventory — src/ (READ-ONLY audit, 2026-10-31)

Purpose: consolidate all pending fixes in one spot; identify refactor/merge targets; make
config changes lane-scoped instead of global. No source was modified.

Scope: 86 files under `src/`, 84,609 LOC total. Fan-in = # project files that `#include` it
(forwarding-header chains counted at the final target). Fan-out = # project-internal
`#include`s it emits. TUs (`.cpp`) have fan-in "—" (they are compiled, not included); their
*wiring* (who calls their entry points) is noted where it matters.

---

## 1. FILE TABLE

### Top level

| File | LOC | Purpose | Subsystem | Fan-in | Fan-out |
|---|---|---|---|---|---|
| src/skygfx.h | 1610 | Config struct, enums, all externs, debug macros | core | 48 | 5 |
| src/texdefs.cpp | 1180 | Unified texture-definition layer (normal-map extraction, TexDefsGetD3DTexture) | render | — | 1 |
| src/texdefs.h | 53 | texdefs public API | render | 8 | 1 |

### core/

| File | LOC | Purpose | Fan-in | Fan-out |
|---|---|---|---|---|
| core/main.cpp | 9142 | DLL entry, INI parse/readIni, hook install (63 InjectHook sites), ped/default callbacks, corona/sprite/skidmark/moon hooks, shadow contract | — | 19 |
| core/main_exports.h | 23 | struct FX export for out-of-TU hook sites | 1 | 1 |
| core/modeManager.cpp | 1635 | Runtime mode toggles: listener registry, latch, disengage/restore | — | 2 |
| core/modeManager.h | 125 | ModeManager API + ModeId enum | 4 | 1 |
| core/debugmenu_ui.cpp | 4384 | ImGui menu (DrawUnifiedDebugMenu, postfx.cpp:2535), named configs, dead unified block | — | 7 |
| core/debugmenu_theme.cpp | 619 | ImGui theme/colours | — | 1 |
| core/debugmenu_theme.h | 132 | Theme API | 2 | 0 |
| core/debug_handl.cpp | 770 | dbglog/diag ring buffer, scope registry, heartbeat (merged §9) | — | 2 |
| core/debug_handl.h | 136 | dbglog API (merged §9) | 13 | 0 |
| core/physicscfg.cpp | 418 | physics.ini store (softbody backend, budget) — lazy-loaded | — | 2 |
| core/physicscfg.h | 119 | PhysicsCfg API | 4 | 0 |
| core/presets.cpp | 157 | ApplyPreset() style presets (PS2/Xbox/PC/IV) | — | 2 |
| core/weather.cpp | 466 | Alt weathers2 timecycle blending, Weather_Update/ApplySunConfig | — | 2 |
| core/weather.h | 31 | Weather API | 2 | 0 |

### render/

| File | LOC | Purpose | Fan-in | Fan-out |
|---|---|---|---|---|
| render/pipelinecommon.cpp | 2250 | Shared pipe helpers: matrix/light upload, CreateShaders, pipeUploadPBR, pipeEnter/ExitCullMode, pipeEnforceDepthContract, alpha block | — | 4 |
| render/buildingPipe.cpp | 3732 | Building render callbacks (PS2/Xbox/PBR/IV) + bldBindDiffuse3L | — | 7 |
| render/vehiclePipe.cpp | 4010 | Vehicle render callbacks (all pipe styles) + vehBindDiffuse3L | — | 5 |
| render/waterPipe.cpp | 2265 | Water rendering (parallax/IV/V styles) + waterPipe_BindVanillaRaster3L | — | 4 |
| render/postfx.cpp | 5022 | PostFX chain orchestrator, ColourFilter_switch hook, DrawUnifiedDebugMenu call | — | 7 |
| render/postfx.h | 199 | PostFX API | 13 | 0 |
| render/postfx_colourfilter.cpp | 1883 | Colour-filter passes (incl. auto-exposure consumer :307) | — | 5 |
| render/postfx_luminance.cpp | 290 | Luminance reduce/adapt — RunFrameExposure/ReleaseLuminanceTargets | — | 4 |
| render/postfx_motionblur.cpp | 407 | Motion blur / burnout pass | — | 5 |
| render/postfx_newfx.cpp | 888 | "New" effects passes (DOF/bloom family support) | — | 4 |
| render/postfx_ssao.cpp | 1318 | SSAO chain (incl. temporal/bilateral/upsample) | — | 4 |
| render/postfx_smaa.cpp | 1434 | SMAA chain + embedded AreaTex/SearchTex data | — | 6 |
| render/postfx_underwater.cpp | 563 | Underwater volume fog + SSS (W7-D), PostFX_UnderwaterTick | — | 4 |
| render/postfx_underwater.h | 57 | Underwater API | 3 | 0 |
| render/postfx_utils.cpp | 338 | Shared quad/raster/state save-restore helpers | — | 4 |
| render/envmap.cpp | 1246 | Screen envmap capture/render (envmaphooks, ShutdownEnvMap) | — | 3 |
| render/mirrorPass.cpp | 722 | CMirrors::RenderMirrorBuffer intercept + composite (mirrorPass_init) | — | 4 |
| render/mirrorPass.h | 14 | mirrorPass API — **not included by main.cpp** (local extern decl instead) | 1 | 0 |
| render/depthhook.cpp | 667 | Depth-buffer hook (DepthHook_Install), device-reset relay | — | 4 |
| render/depthhook.h | 35 | DepthHook API | 7 | 0 |
| render/forwardplus.cpp | 1946 | Tiled Forward+ light cull/upload/constants | — | 4 |
| render/forwardplus.h | 101 | ForwardPlus API | 2 | 0 |
| render/neopipe.cpp | 2199 | Neo vehicle pipe + WaterDrops rain/blood drops + FX-splash fix | — | 3 |
| render/neopipe.h | 220 | Neopipe/WaterDrops API | 2 (+neo.h chain) | 0 |
| render/neo.h | 3 | **Forwarding header → neopipe.h** (delete candidate) | 4 | 1 |
| render/grasssys.cpp | 1405 | Plant island TU: legacy renderer + instancer + depth contract — **draw path dead** | — | 4 |
| render/grasssys.h | 699 | Plant island API (CGrassRenderer, CGrassInstancer, CPPTriPlantBuffer) | 2 | 3 |
| render/PC_GrassInstancer.h | 3 | **Forwarding header → grasssys.h** (delete candidate) | 1 | 1 |
| render/treeWind.cpp | 476 | CPU tree-wind tilt (TreeWind_ApplyToTransform) | — | 3 |
| render/treeWind.h | 140 | TreeWind API | 4 | 1 |
| render/texalpha.cpp | 1364 | Texture alpha classification (TexAlpha_Classify, OnDeviceReset) | — | 3 |
| render/texalpha.h | 96 | TexAlpha API | 3 | 0 |
| render/noise.cpp | 959 | Noise texture generation/utilities | — | 2 |
| render/noise.h | 95 | Noise API | 4 | 0 |
| render/profiler.cpp | 662 | Pass/frame profiler (PfFrame, PF_SCOPE) | — | 3 |
| render/profiler.h | 36 | Profiler API | 16 | 0 |
| render/renderstate_monitor.cpp | 488 | 3-layer (rw/drv/dev) render-state monitor | — | 2 |
| render/renderstate_monitor.h | 45 | RSMon API | 2 | 0 |
| render/AreaTex.h | 14980 | SMAA area-texture data blob | 1 | 0 |
| render/SearchTex.h | 132 | SMAA search-texture data blob | 1 | 0 |

### entities/

| File | LOC | Purpose | Fan-in | Fan-out |
|---|---|---|---|---|
| entities/chars.cpp | 647 | Ped head-part receipt (chars_logHeadPartReceipt) + chars_drawSSSBlur post-pass | — | 2 |
| entities/chars.h | 55 | chars API + PartSSSParams | 3 | 0 |
| entities/softbody.cpp | 1267 | Softbody ragdoll backend (SoftRagdoll_c, Bullet) | — | 6 |
| entities/softbody.h | 99 | Softbody API | 2 | 2 |
| entities/sftbdyphsx.cpp | 1184 | Legacy Ragdoll_c/RagdollManager_c (g_ragdollMan, merged §9) — **alive** (main.cpp:5705/8359) | — | 5 |
| entities/sftbdyphsx.h | 249 | Ragdoll API (merged §9) | 4 | 3 |
| entities/PlantSurfPropMgr.h | 32 | Plant surface-prop decl (consumed via grasssys.h only) | 1 | 1 |

### rw/

| File | LOC | Purpose | Fan-in | Fan-out |
|---|---|---|---|---|
| rw/gta.cpp | 232 | RW SDK WRAPPER/EAXJMP definitions + game globals (CWeather__* etc.) | — | 5 |
| rw/gta.h | 388 | RW SDK globals (pDirect, pAmbient, Scene, d3d9device, skinPipe) | 8 | 0 |
| rw/normmap_stubs.cpp | 87 | Link stubs for rpnormmap.lib (linked at premake5.lua:348) — **alive** | — | 0 |
| rw/pipeplg.cpp | 17 | GetPipelineID/SetPipelineID plugin (used buildingPipe.cpp:3459/3688, main.cpp:2474/3718) | — | 1 |
| rw/rtquat.h | 646 | RW SDK quaternion | 5 | 0 |
| rw/rtanim.h | 654 | RW SDK anim | 1 | 0 |
| rw/rphanim.h | 673 | RW SDK HAnim | 4 | 0 |
| rw/rtslerp.h | 262 | RW SDK slerp | 3 | 1 |
| rw/MemoryMgr.h | 98 | RW SDK memory | 2 | 0 |
| rw/Pools.h | 161 | RW SDK pools | 1 | 0 |
| rw/LinkList.h | 148 | RW SDK list | 1 | 0 |
| rw/ColData.h | 17 | Grass colour data decl (via grasssys.h only) | 1 | 1 |

### extras/

| File | LOC | Purpose | Fan-in | Fan-out |
|---|---|---|---|---|
| extras/veh_shaders.cpp | 539 | Per-vehicle paint/BRDF/light-tint params (VehShaders_*) | — | 2 |
| extras/vehicles.cpp | 378 | vehicles.ide/handling.cfg parser DB (Vehicles_Init + getters) | — | 1 |
| extras/wheels.cpp | 169 | wheels_meta.json loader — **DEAD** (see §2) | — | 2 |
| extras/wheels.h | 44 | Wheels API — zero external consumers | 1 | 0 |
| extras/ragdoll_death.cpp | 1387 | Bullet death ragdolls (Ragdoll_Init/Simulate/ApplyPose) | — | 7 |
| extras/ragdoll_death.h | 143 | Ragdoll API | 2 | 1 |
| extras/brdfLibrary.h | 627 | Header-only BRDF material database | 3 | 0 |

---

## 2. DEAD CODE LIST (each claim verified)

| # | Item | Verdict | Evidence |
|---|---|---|---|
| D1 | **grasssys renderer + instancer draw path** | **CONFIRMED DEAD (draw path)** | `CGrassRenderer::*` (grasssys.cpp:101-532) and `CGrassInstancer::DrawTriPlant/BeginBatch/AddInstance/EndBatch` (grasssys.cpp:1153-1402) have **zero external callers** — only definitions and internal calls. Wired surface is only: `CGrassInstancer::SetEnabled` (main.cpp:800, 7279, 8304 — gated by `grassInstancing`, default 0), `GrassEnforceDepthContract` (main.cpp:815), `GrassResetShaderLatches` (depthhook.cpp:417). The instancer is *enabled* but nothing ever feeds it: no hook calls DrawTriPlant. ~1,000 of 1,405 LOC in grasssys.cpp + ~350 of 699 in grasssys.h (CGrassRenderer class + CGrassInstancer draw API) are dead. Plant rendering runs vanilla. |
| D2 | **IBL_SkyCloud.hlsl** | **CONFIRMED not embedded** | `shaders/ps/IBL_SkyCloud.hlsl` exists; **no `IDR_*` in resources/resource.rc**; superseded by DynamicSky.hlsl (its header line 3: "Replaces IBL_SkyCloud"). CSO exists on disk but is unreachable. Delete or wire. |
| D3 | **unified\* config family (32 fields)** | **CONFIRMED DEAD** | skygfx.h:615-628 (32 fields). Sole consumer `UploadUnifiedConstants` (debugmenu_ui.cpp:145) begins with `return;` at :147 and is **never called** (comment :141-144 "DEAD CODE... never called"; :1461-1462 "dead JuniorDjjr unified-pipeline block"). Keys are not even parsed in readIni (absent from the 259-key list). Delete fields + function + the debugmenu references (:148-208, :1461). |
| D4 | **pedAmbientFloor** | **CONFIRMED dead-but-parsed** | Parsed main.cpp:7102-7104 (clamped), EMIT :7429, saved :8054, debugmenu slider debugmenu_ui.cpp:817, default table :1571 — but **zero render consumers**. The W3 revert removed the lift (comment main.cpp:2189-2191: "no pedAmbientFloor hue-preserving lift"). Either re-wire or delete key+field+slider. |
| D5 | **forwardPlusFallback "no reader"** | **CLAIM WRONG — ALIVE** | Read by forwardplus.cpp:257 (`FpFallbackEnabled`, called :1444), EMIT main.cpp:7405, debugmenu read-only display debugmenu_ui.cpp:3375. **Caveat**: it is read via direct `GetPrivateProfileIntA` *outside* the main readIni parser (forwardplus.cpp:141 documents this) — an inconsistency worth fixing, not a dead key. |
| D6 | **FLAT helper 0x4A1940 gate** | **CONFIRMED dead (game-exe code, documented)** | neopipe.cpp:1156-1160: gate flag `[prim+0x3d]` has no writer anywhere in the FX region; only call site 0x4A2CA2 always receives 0 on the live chain. This is *game exe* dead code documented in a comment — no action possible in our source. |
| D7 | **ps2grassFiles key** | **CONFIRMED vestigial** | Decl commented (main.cpp:51), parse commented (:6770), not emitted, no consumer. Delete the two comment lines. |
| D8 | **4 embedded-but-never-loaded CSOs** | **CONFIRMED** | IDR_NORMMAPBUILDINGPS, IDR_NORMMAPVEHICLEPS, IDR_PEDPBRVS, IDR_VECTORMOTIONBLUR are in resource.rc but **zero src references**. Notably **IDR_PEDPBRVS (pedPBRVS.cso, res 275)** — a ped PBR vertex shader is compiled+embedded but never created/loaded; relevant to the peds_pipe plan (§6). |
| D9 | **11 HLSL files with no IDR (not embedded)** | **CONFIRMED** | GTA_SA_ModernColor, GrassInstanced, HairEnhance, IBL_SkyCloud, SSAO_ps20_depthonly, SSAO_ps20_simple, SkinEnhance, iiiTrailsPS (ps/); mobileBuildingVS, postfxVS, ps2BuildingSSSVS (vs/). Matches docs/plans/2026-10-25-deadcode-manifest.md. Delete or wire. |
| D10 | **wheels.cpp / wheels.h** | **CONFIRMED DEAD module** | `Wheels_Init` is wired (vehiclePipe.cpp:216) but its entire output is unread: `Wheels_SelectForVehicle/GetEntry/GetClassForModel/GetCount` have **zero callers** outside wheels.cpp. Loads wheels_meta.json nobody consumes. Delete module + the Init call. |
| D11 | **coronaZtest comment/key mismatch** | **FLAG — reconcile** | main.cpp:374-380 says "PARSE-ONLY... SilentPatchSA owns the corona z-test... no bytes written", but main.cpp:4755 and :4829 actively read `config->coronaZtest` in a corona z-test force block. One of the two blocks/comments is stale. Verify which generation is live before touching. |
| D12 | **Duplicate normal-map INI keys** | **FLAG** | `enableNormalMaps` read directly by texdefs.cpp:1038 (GetPrivateProfileStringA) vs `normalMapEnable` parsed in readIni (main.cpp:6757). Two keys, similar intent, different readers — consolidate. |
| D13 | **Keys read outside readIni** | **FLAG** | `debugOverlay` (debugmenu_ui.cpp:4277), `forwardPlusDebug` (forwardplus.cpp:168), `forwardPlusFallback` (:257), `enableNormalMaps` (texdefs.cpp:1038), `softbodyNative` (physicscfg.cpp:276) — none appear in the readIni key list or EMIT template; invisible to INI save/reload and the debugmenu default table. |
| D14 | **Stale comments** | Minor | ragdoll_death.cpp:858 references "src/entities/ragdoll.cpp" (merged away); debugmenu_ui.cpp:1278 references staged PC_GrassRenderer diff. |

**Checked and ALIVE (not dead):** `g_ragdollMan`/RagdollManager_c (main.cpp:5705/5706/8359/9124),
`GetPipelineID`/`SetPipelineID`, normmap_stubs.cpp (rpnormmap.lib linked), pipeplg.cpp,
`UploadUnifiedConstants` is the only dead function found in the debugmenu; `ApplyPreset`
(debugmenu_ui.cpp:2620-2658), `Weather_Update` (main.cpp:5713), `Weather_Init`
(vehiclePipe.cpp:212 — odd call site, see §3), `mirrorPass_init` (main.cpp:5786),
`PostFX_UnderwaterTick` (postfx.cpp:2019), `RunFrameExposure` (postfx_colourfilter.cpp:307),
`envmaphooks` (main.cpp:5779), `chars_drawSSSBlur` (postfx.cpp chain), `TexAlpha_Classify`
(4 consumers), `VehShaders_*` (pipelinecommon.cpp:918, vehiclePipe), `Vehicles_*` getters
(veh_shaders.cpp), ForwardPlus_* (main.cpp:3997/4415/9116), `pipeEnforceDepthContract`
(pipelinecommon.cpp:1315).

---

## 3. MERGE CANDIDATES (remaining; §9 already merged texdefs, debug_handl, sftbdyphsx)

| # | Merge | Effort | Risk | Notes |
|---|---|---|---|---|
| M1 | **Delete dead grass draw path** (grasssys.cpp CGrassRenderer + CGrassInstancer draw API, grasssys.h classes) — not a merge but the biggest single reduction (~1,350 LOC) | Med | LOW | Keep SetEnabled/GrassEnforceDepthContract/GrassResetShaderLatches + the plant-island support the live code touches. Re-add later from git if instancing is revived. |
| M2 | **postfx_luminance.cpp (290) → postfx.cpp** or postfx_colourfilter.cpp | Small | LOW | Only consumers: RunFrameExposure (postfx_colourfilter.cpp:307), ReleaseLuminanceTargets (postfx.cpp:2738), decl in postfx.h:34. Single-caller module; fold into colourfilter (its only runtime consumer) and drop from postfx.h or keep decl. |
| M3 | **presets.cpp (157) → debugmenu_ui.cpp** | Trivial | LOW | Sole caller is debugmenu_ui.cpp:2620-2658. (Alternative: modeManager.cpp, since it already calls ModeManager_ClearLatch.) |
| M4 | **vehicles.cpp + veh_shaders.cpp → one extras/vehdata.cpp** (~917 LOC) | Small | LOW | veh_shaders already calls Vehicles_Init (veh_shaders.cpp:133) and consumes all vehicles.cpp getters. One TU, one init. |
| M5 | **pipeplg.cpp (17) + normmap_stubs.cpp (87) → rw/gta.cpp** | Trivial | LOW | Both are RW glue; gta.cpp is the natural home. normmap_stubs must keep `extern "C"` + selectany semantics for rpnormmap.lib. |
| M6 | **Delete forwarding headers neo.h and PC_GrassInstancer.h**; include neopipe.h / grasssys.h directly (4 + 1 include sites) | Trivial | LOW | Pure indirection. |
| M7 | **chars.cpp split**: chars_drawSSSBlur (~420 LOC, chars.cpp:150-640) → **postfx_sss.cpp** (or into postfx.cpp); chars_logHeadPartReceipt (:60-150) → future **peds_pipe.cpp** (§6) | Med | MED | SSS blur is a postfx pass living in entities/; it already includes postfx machinery via postfx.cpp's call. Head-part receipt is ped-lane. Do together with the peds_pipe extraction. |
| M8 | **weather.cpp init call site**: Weather_Init is called from vehiclePipe.cpp:212 (buried in a vehicle TU) — move to main.cpp init burst next to Weather_Update (:5713) | Trivial | LOW | Not a merge; a wiring fix. |
| M9 | **main.cpp local extern decls → headers**: main.cpp:5778 (`envmaphooks`), :5785 (`mirrorPass_init`) declare functions locally instead of including envmap/mirrorPass headers. Add decls to a header (or include mirrorPass.h) | Trivial | LOW | mirrorPass.h currently has fan-in 1 (its own TU) purely because of this. |
| M10 | **debugmenu_theme.cpp → debugmenu_ui.cpp** | Small | MED | Would create a ~5,000 LOC TU; recommend **not** merging — keep as-is (theme is a clean seam). |
| M11 | **texalpha.cpp → texdefs.cpp** | Med | MED | Both texture layers, texalpha already includes texdefs.h. Combined 2,544 LOC — only if main.cpp split (planned) frees review budget. Defer. |
| M12 | **postfx_underwater.cpp → postfx.cpp** | Med | MED | Self-contained, single call site (postfx.cpp:2019). Viable (matches the earlier "postfx_support merge assessed viable") but the split postfx_*.cpp pattern is working well; low priority. |

**Already-assessed / planned (not re-listed as new work):** postfx_support merge (viable, not
executed), main.cpp split (planned — see §6 for the ped slice).

---

## 4. CONFIG ROUTING MAP

All `[SkyGfx]` keys are parsed in `readIni` (main.cpp:6494-7355) unless noted. Gate site =
first consumption point. 259 distinct keys total.

### Ped lane (already lane-scoped)
| Key | readIni | Consumer / gate |
|---|---|---|
| pedOpaqueContract | main.cpp:7027 | main.cpp:2009, 2113-2131 (myDefaultCallback) |
| pedCullMode | main.cpp:7033 | main.cpp:2041-2056 |
| pedAlphaTestEnable | main.cpp:7037 | main.cpp:2010, 2131 |
| pedTexResync | main.cpp:7011 | main.cpp:2138-2139 |
| zwriteThresholdPed | main.cpp:6906 | main.cpp:1955, 2272-2273 |
| dualPassPed | main.cpp:6782 | main.cpp:1955 |
| pedAmbientFloor | main.cpp:7102 | **DEAD** (D4) |

### Grass / vegetation lane
| Key | readIni | Consumer / gate |
|---|---|---|
| ps2ModulateGrass | main.cpp:6767 | grasssys/building grass path |
| dualPassGrass | main.cpp:6768 | grass draw path |
| grassAddAmbient | main.cpp:6769 | grasssys |
| grassFixPlacement | main.cpp:6771 | grass placement fix |
| grassBackfaceCull | main.cpp:6772 | grass draw |
| grassInstancing | main.cpp:7276 | main.cpp:800/7279/8304 → CGrassInstancer::SetEnabled (**draw path dead**, D1) |
| zwriteThresholdGrass | main.cpp:6902 | grass dual-pass cut |
| vegetationSSSStrength / vegetationAmbientBoost / vegetationEnhanceEnable | main.cpp:7181-7189 | SSS/veg enhance chain |

### Building lane
| Key | readIni | Consumer / gate |
|---|---|---|
| buildingPipe / explicitBuildingPipe | main.cpp:6621 / 6894 | pipeline switch (main.cpp:2474, 3718 SetPipelineID) |
| ps2ModulateBuilding / dualPassBuilding | main.cpp:6672-6673 | buildingPipe callbacks |
| bldEnvReflect / bldSkyReflect | main.cpp:6680-6683 | buildingPipe env reflect |
| treeFx / treeWind / forceWindShader | main.cpp:7260 / 7271 / 7086 | buildingPipe.cpp:1397, 2581 (TreeWind_ApplyToTransform) |

### Vehicle lane
| Key | readIni | Consumer / gate |
|---|---|---|
| vehiclePipe | main.cpp:6639 | pipeline switch |
| dualPassVehicle | main.cpp:6687 | vehicle dual pass |
| leedsShininessMult / neoShininessMult / neoSpecularityMult | main.cpp:6688-6690 | vehiclePipe |
| envShininessMult / envSpecularityMult / envPower / envFresnel | main.cpp:6691-6708 | envmap/vehiclePipe |
| vehPBRLayers / vehEnvIntensity / vehChromeEnvThreshold | main.cpp:6710-6716 | vehiclePipe PBR |
| vehAutoChrome + chrome* (5) | main.cpp:6719-6724 | vehiclePipe chrome |
| vehPrelightFallback / vehEnvFallback / vehEnvStrengthMode / vehEnvMask / vehWetEnv / vehEnvGlint | main.cpp:7115-7126 | vehiclePipe |
| normalMapEnable / normalMapIntensity / normalMapPlayerOnly | main.cpp:6757-6759 | texdefs/vehiclePipe (**plus orphan `enableNormalMaps`**, D12) |

### Water lane
| Key | readIni | Consumer / gate |
|---|---|---|
| waterStyle / waterQuality / waterFresnelSplit | main.cpp:7303-7311 | waterPipe |
| waterParallax* / waterNormalStrength / waterFresnelPower / waterSpecular* | main.cpp:7228-7233 | waterPipe |
| waterUnderwaterFog / waterFoam* / waterShoreFoam / waterBodyClassify | main.cpp:7234-7240 | waterPipe + postfx_underwater |
| waterShallow*/Deep* (6) / waterTileScale / waterShoreFade / waterTranslucency / waterReflectionStrength / waterUseTimecycle | main.cpp:7243-7256 | waterPipe |
| neoWaterDrops / neoBloodDrops | main.cpp:6887-6892 | neopipe.cpp:1499-1785 (gated iCanHasNeoDrops, main.cpp:8416) |

### PostFX lane (screen-space; inherently global — OK)
ssao* (10 keys, main.cpp:6931-6935, 7154-7158), smaaEnable (:6940), motionBlur* (5,
:6943-6947), velocityBufferEnable (:6950), dof* (5, :7318-7325), bloom* (4, :7331-7337),
sunShafts* (3, :7339-7343), fxaaEnable (:7346), grain* (:6856, 7347), caIntensity (:7350),
vignetteIntensity (:7355), tonemap* (11, :6726-6736, 7111, 7328), radiosity* (7,
:6868-6885, 7065), trails* (3, :6878-6881), heightFog* (7, :7132-7139), godRays* (6,
:7142-7147), iv* (11, :7073-7083), colorFilter/colorFilterEnable/rgb1Mult/rgb2Mult
(:6836-6852), YCbCr* (7, :6922-6928), blur* rect (4, :6859-6865), usePCTimecyc (:6857),
infraredVision/nightVision (:6854-6855), postfxDumpDebug (:7067), grainEnable (:7066).

### Effects / corona / sprite lane (main.cpp hook sites)
coronaZtest (:6910 → main.cpp:4755/4829; **stale "retired" comment** :374, D11),
coronaBlendShield (:6993 → main.cpp:3084, 3139), spriteStateShield (:6984 → :3125),
spriteFlushShield (:6996 → :3462, 3546), moonMaskAlpha (:7005 → :3778, 3803),
skidmarkAlphaFix (:7008 → :3856-3863), transparentLockon (:6814), sunGlare (:6761),
sunCorona/Core/Streak* (4, :7209-7212), disableClouds/cloudRenderFix (:6808-6812),
fixShadows (:6815), lightningIlluminatesWorld (:6816 → buildingPipe.cpp:185),
fixPcCarLight (:6893).

### ⚠ GLOBAL keys whose effect spans ALL lanes (the "config changes fix only one lane" violations)

| Key | readIni | Gate site(s) | Why global |
|---|---|---|---|
| **stateBaselineEnable** | main.cpp:6981 | main.cpp:3935, 4497, 4526 | Render-window baseline forced at *every* pipe entry + sprite windows — touches buildings, vehicles, peds, effects alike. |
| **zwriteThreshold** | main.cpp:6898 | main.cpp:275, 5266 | Dual-pass cut shared by default/building/vehicle spans (ped has its own key; grass has its own; the base one is the cross-lane one). |
| **dualPass** / **dualPassDefault** | main.cpp:6611 / 6781 | main.cpp:1904, 2241 | Global dual-pass master + default-pipe variant. |
| **ps2Modulate** | main.cpp:6610 | pipe callbacks | Global modulate master overriding the per-lane Building/Grass keys. |
| **ambientFloor / ambientMult / pbrAmbientFloor / pbrIblAmbientWeight** | main.cpp:7043 / 7107 / 7093 / 7110 | pipelinecommon.cpp:167, 196, 214-236 | Light-upload math applied to **every** pipe's ambient/direct upload — a ped-only tweak here shifts buildings and vehicles too. |
| **texAlphaClassify** | main.cpp:6978 | buildingPipe.cpp:2035, 2849; vehiclePipe.cpp:2884; main.cpp:931; chars.cpp:90 | Classification gate consumed by 3 lanes. |
| **defaultCbShaderHygiene** | main.cpp:7022 | main.cpp:2152 | Runs for every default-callback atomic (peds AND props/pickups). |
| **shadowContractEnable** | main.cpp:7019 | main.cpp:1616 | All four shadow hooks. |
| **depthHookEnable / normalBuffer\* / pipeChain\*** | main.cpp:6953-6963 | main.cpp:3964; postfx chain | Device-level buffers consumed by multiple lanes. |
| **camRasterRepair / effectsAlphaBlock** | main.cpp:7002 / 6999 | main.cpp:4078 / 4170 | Camera/effects windows, cross-lane. |
| **pipeline / pipelineOverride / qualityPreset / gamePreset** | main.cpp:6550-6553, 6599, 7202 | pipeline switch | Intentionally global. |
| **profiler\* / stateBaseline / debug keys** | main.cpp:7285-7286 | main.cpp:794-795 | Intentionally global. |

**Proposed key-namespacing convention** (drives the "one lane per change" rule):

```ini
[Ped]        pedOpaqueContract, pedCullMode, pedAlphaTestEnable, pedTexResync,
             zwriteThresholdPed, dualPassPed, pedAmbientFloor, skin*/hair*/sssSkin*
[Building]   buildingPipe, ps2ModulateBuilding, dualPassBuilding, bldEnvReflect,
             bldSkyReflect, treeFx, treeWind, zwriteThresholdBuilding(new)
[Vehicle]    vehiclePipe, dualPassVehicle, veh*, chrome*, leeds/neo/env mults
[Grass]      grass*, ps2ModulateGrass, dualPassGrass, zwriteThresholdGrass,
             vegetation*
[Water]      water*, neoWaterDrops, neoBloodDrops
[PostFX]     ssao*, smaa*, motionBlur*, dof*, bloom*, tonemap*, ... (screen-space)
[Effects]    corona*, sprite*, moonMask*, skidmark*, sunGlare...
[Core]       pipeline, qualityPreset, profiler*, debug keys, stateBaselineEnable
```

Migration mechanics: keep old top-level keys as **fallback reads** (read section first, then
top-level) for one release; readIni gains a `ReadKey(section, key, fallbackTopLevel)` helper.
The four light keys (ambientFloor/ambientMult/pbrAmbientFloor/pbrIblAmbientWeight) are the
hard case: they live in pipelinecommon's shared upload — lane-scoping them requires either
per-lane upload variants or a lane tag parameter on the light-upload call (see §5 helper).

---

## 5. FIX-CONSOLIDATION ASSESSMENT — the 3-layer bind/contract idiom (5 copies)

The waves added the same "three-layer (L1 rw cache / L2 RW driver cache / L3 raw device)
park-and-set bind" idiom in five places:

| Helper | Site | Call sites |
|---|---|---|
| `vehBindDiffuse3L(RwTexture*, int stage)` | vehiclePipe.cpp:767-821 (def :795) | vehiclePipe.cpp:836, 1662, 1738, 1775, 1811, 1824, 2242, 2244, 2421, 2581 — **10 sites** |
| `bldBindDiffuse3L(RwTexture*, int stage)` | buildingPipe.cpp:1152-1204 (def :1177) | buildingPipe.cpp:1214, 1251 — 2 sites |
| `waterPipe_BindVanillaRaster3L(void)` | waterPipe.cpp:2008-2030 (def :2030) | waterPipe.cpp:1216, 1256, 1262, 1276, 1294, 2159, 2163, 2168, 2172 — **9 sites** |
| `pedStage0Resync(RpAtomic*)` | main.cpp:1760-1872 (def :1782) | main.cpp:2139 — 1 site (+ NULL-cache recovery branch) |
| grasssys stage-0 contract | grasssys.cpp:~1003-1015 (bind-only mirror of vehBindDiffuse3L) + GrassResetShaderLatches (:761) + GrassEnforceDepthContract (:876) | internal + depthhook.cpp:417 |

**Plus a sixth partial instance already shared:** `pipeEnforceDepthContract`
(pipelinecommon.cpp:1314-1356) — the depth-state half of the idiom is ALREADY consolidated in
pipelinecommon (it explicitly cites grasssys.cpp:823 as the reference implementation), and
`pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set` (pipelinecommon.cpp:~1240-1284) are the shared
park-and-set primitives the bind helpers re-implement by hand.

**Proposal: one shared helper in pipelinecommon.cpp**

```cpp
// pipelinecommon.cpp — the ONE bind helper
void pipeBindDiffuse3L(RwTexture *tex, int stage, int flags);
// flags: BIND_PARK_SET (default) | BIND_RESOLVE_NULL_TO_WHITE | BIND_RAW_ONLY
// Behaviour = today's vehBindDiffuse3L: L1 park-and-set raster, L2 RwD3D9SetTexture,
// L3 d3d9device->SetTexture(0..n, TexDefsGetD3DTexture(tex)) with guarded extraction,
// gpWhiteTexture fallback on NULL (the pedStage0Resync NULL-cache recovery folded in).
```

`waterPipe_BindVanillaRaster3L` is the raster-only (no RwTexture) variant → either a second
entry `pipeBindRaster3L(RwRaster*, int stage)` or a flag. `pedStage0Resync` adds the
atomic-material walk + dbglog receipts → keep the walk in the ped module (§6) but have it
end in `pipeBindDiffuse3L(resolved, 0, BIND_PARK_SET)`.

**Migration plan (per call site):**

1. Add `pipeBindDiffuse3L` + `pipeBindRaster3L` to pipelinecommon.cpp; declare in skygfx.h
   (or a new pipelinecommon.h — currently pipelinecommon has **no header**, all consumers
   reach it via skygfx.h externs).
2. vehiclePipe.cpp: replace 10 `vehBindDiffuse3L` calls; delete local def :767-821. The
   elision tracker at :2400-2421 wraps the helper — keep the wrapper, point it at the shared
   helper.
3. buildingPipe.cpp: replace 2 calls (:1214, :1251); delete def :1152-1204.
4. waterPipe.cpp: replace 9 calls with `pipeBindRaster3L()`; delete def :2008-2030 and the
   forward decl :88.
5. main.cpp: `pedStage0Resync` (:1782-1872) keeps its atomic walk + receipts but delegates
   the actual bind to `pipeBindDiffuse3L`; the park/set pairs at :1820-1821, :1851-1852 and
   the L3 pin at :1869 collapse into the helper.
6. grasssys.cpp: the stage-0 contract at :1003-1015 delegates to the same helper;
   GrassEnforceDepthContract (:876) can delegate its repair branch to
   `pipeEnforceDepthContract` (already shared) or remain the reference implementation —
   pick one and comment the other as alias.
7. Verify: `--fastest` build + ped/water/building/vehicle smoke screenshots; the dbglog
   receipts (`[PedTex]`, `[zfix]`) must still fire.

Effort: ~1-2 h. Risk: LOW-MED — the idiom is byte-identical in intent across copies; the
risk is the water raster-only variant and the ped NULL-recovery branch, both covered by
flags. Payoff: 5 copies → 1; future lane fixes land once.

---

## 6. PED PIPE HOOK-POINT MAP (input for the peds_pipe design doc)

All sites in **src/core/main.cpp** unless noted. Hook base: `InjectHook(0x7491C0,
myDefaultCallback, PATCH_JUMP)` — **main.cpp:8611**.

| # | Component | Location | Notes |
|---|---|---|---|
| P1 | **Callback hook install** | main.cpp:8611 | `0x7491C0 → myDefaultCallback`, PATCH_JUMP. RW side: gta.cpp:159 `AtomicDefaultRenderCallBack` WRAPPER; `skinPipe` global gta.cpp:66 (`*(RxPipeline**)0xC978C4`), decl skygfx.h:1327. |
| P2 | **myDefaultCallback** | main.cpp:1881-~2440 (def :1882) | Entry: pipe capture :1895, `isPedPipe = (pipe == skinPipe)` :1900. |
| P3 | **pedOpaqueEnforce / pedOpaqueRestore / pedOpaqueCapture / pedOpaqueDiverged** | main.cpp:1101-1161 (enforce), :1163-1191 (restore); capture/diverged in the 1000-1099 block | Ped opaque contract primitives; POD `PedOpaqueState`. |
| P4 | **Ped entry contract** | main.cpp:1958-2140 | cull save :1983, `pipeEnterCullMode` :2033, pedCullMode A/B :2041-2064, `pipeEnterAlphaMode` :2066, ZWRITE 3-layer guard :2068-2084, `pipeForceAlphaBlock` :2089, saved alpha-func/ref restore :2098-2105. |
| P5 | **Ped opaque contract apply** | main.cpp:2107-2132 | `pedContractOn` :2009, diverged receipt :2119-2130, `pedOpaqueEnforce(pedAlphaTestOn)` :2131. |
| P6 | **pedStage0Resync** | main.cpp:1760-1872 (def :1782), called :2138-2139 | Park-and-set + NULL-cache recovery (atomic material walk :1798-1812, gpWhiteTexture fallback :1813, L1/L2/L3 bind :1814-1832, receipts :1836-1844, :1870-1871). Gated by `pedTexResync`. |
| P7 | **pAmbient override** | main.cpp:2185-2225 (save :2192, gate :2194, black-guard :2197-2219, apply :2220), restore :2340-2343 | Sep-3 semantics: plain `GetTimecycleAmbient()`; black-override block (04:38 forensics). `pedAmbientFloor` intentionally NOT used here (D4). |
| P8 | **Dual-pass legs (ped)** | main.cpp:2227-2337 | Gate :1955 (`dualPassPed && zwriteThresholdPed>0`), legacy-skip comment :2227-2240, ped legs :2281-2327 (ZWRITE TRUE → leg1 GREATEREQUAL@zt :2290-2296 → ZWRITE FALSE + blend-on → leg2 LESS@zt :2297-2308 → restore :2309-2310), non-ped branch :2328-2336. `setZWriteTwoLayer`/`setAlphaTestTwoLayer` = pipelinecommon shared helpers. |
| P9 | **Head-part receipt** | main.cpp:2253-2258 → `chars_logHeadPartReceipt` (chars.cpp:~60-150) | S5 diagnostic; uses TexAlpha_Classify (texalpha.cpp:1172). |
| P10 | **Ped exit contract** | main.cpp:2345-2379 | `pipeExitAlphaMode` :2352, sticky ZWRITE repair :2359-2371, `pipeExitCullMode` :2372, `pedOpaqueRestore` :2378-2379. |
| P11 | **[PedRS] exit monitor** | main.cpp:2381-~2440 | 21-state 3-layer readout, throttled "ped_rs". |
| P12 | **Default-cb shader hygiene** | main.cpp:2142-2183 | Shared with props/pickups (not ped-only); gate `defaultCbShaderHygiene` :2152. |
| P13 | **SSS blur pass** | chars.cpp:150-640 (`chars_drawSSSBlur` def :231); called from the postfx chain (postfx.cpp) | Gates: sssEnable :242, sssPostProcessEnable :247, depth-valid :262-268; rasters :166-179; exit contract :182-229; passes 0/1/2 :438-640. Consumes skin*/sss* keys. |
| P14 | **Ped VS shader (unwired)** | resources/resource.rc:129 `IDR_PEDPBRVS` (pedPBRVS.cso, shaders/vs/pedPBRVS.hlsl) | Embedded but **never loaded** (D8) — the natural shader for a future peds_pipe PBR path. |
| P15 | **Ped-related INI keys** | main.cpp:7011-7104 (pedTexResync, pedOpaqueContract, pedCullMode, pedAlphaTestEnable, zwriteThresholdPed :6906, dualPassPed :6782, pedAmbientFloor :7102) + skin*/hair*/sssSkin* :7161-7199 | See §4 Ped lane. |

**Extraction plan sketch (for the design doc):** new `src/entities/peds_pipe.{cpp,h}` (or
`src/render/peds_pipe.cpp`) absorbs P3-P11 + P9 (≈900 LOC from main.cpp) + P13 from
chars.cpp (M7). Exposes `pedsPipe_init()` (moves the :8611 hook install), keeps
`pipeEnter/ExitCullMode`, `pipeEnter/ExitAlphaMode`, `pipeForceAlphaBlock`,
`setZWrite/AlphaTestTwoLayer`, `pipeBindDiffuse3L` (§5) as pipelinecommon imports. P12 stays
in main.cpp (shared with non-ped default atomics). Config keys move to the `[Ped]` section
(§4). main.cpp drops to ~8,200 LOC as the first slice of the planned split.

---

## Appendix — quick stats

- Total: 86 files, 84,609 LOC (AreaTex.h alone is 14,980 — 18% of the tree is SMAA data).
- Largest TUs: main.cpp 9,142; postfx.cpp 5,022; debugmenu_ui.cpp 4,384; vehiclePipe.cpp 4,010; buildingPipe.cpp 3,732.
- Dead/duplicate mass identified: ~1,350 LOC grass draw path + 169 wheels + 32 config fields + 15 shader files + 4 orphan CSOs ≈ **~2,000 LOC removable** before any merge.
- Highest fan-in headers: skygfx.h (48), profiler.h (16), postfx.h (13), debug_handl.h (13), texdefs.h (8), depthhook.h (7).
