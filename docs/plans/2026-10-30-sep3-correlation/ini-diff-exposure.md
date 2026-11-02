# INI Surface Diff (Sep 3 → now) + Exposure-Gap Inventory

- **Date:** 2026-10-30
- **Lane:** READ-ONLY research (§4.3 bisect support)
- **Baseline anchor:** repo has **no commits on 2026-09-03**. Working tree state during Sep 3 = the tree as of **`c270294`** ("config reader consolidation, INI read fixes, safety hardening, deploy ritual", 2026-08-30). The next commit is `18f5303` (2026-09-07, checkpoint "PBR restore + dismemberment/ragdoll/depthhook/SMAA + tonemap tuning knobs"). No commit exists between Aug 30 and Sep 7, so `c270294` **is** the stable state of 2026-09-03.
- **HEAD anchor:** `898e894` (2026-10-25).

---

## 1. Surface-size summary

| Surface | Sep 3 (`c270294`) | now (`898e894` + working tree) | Δ |
|---|---|---|---|
| `cfg.get()` keys in `readIni` (src/core/main.cpp) | 110 SkyGfx keys (+0 other sections) | 259 SkyGfx + 1 `[General]` + 1 `[Modes]` | **+151** |
| Keys written by `saveConfig` | present | present | — |
| Write-only keys (written but never read back) | **44** | **0** | 44 dead switches became live bisect levers |
| INI sections | `[General]` (NoiseQuality not read), `[SkyGfx]` | `[General]` NowQuality-read, `[SkyGfx]`, **`[Modes]` active= (M1a mode persistence)** | new |
| Read sites outside main.cpp | `normalmap.cpp` (`enableNormalMaps`), `ragdoll.ini`/`ragdoll_death.cpp` | same + `normalmap_byDK.ini` plugin file | stable |
| `skygfx.default.ini` (repo) | **untracked / did not exist** | generated reference (Oct 26, commits 09ff0c7/83047b4/898e894), untracked | new artifact |
| `presets.cpp` | unchanged since baseline | unchanged | 0 |

Extraction shape notes (so the table is reproducible): keys captured from `cfg.get("<sec>", "<key>", <default>)`; keys read via `parseShadowToggle("pedShadows"...)` (main.cpp:6493-6513) and the `StrAssoc` path count as retained.

## 2. Shared keys: default changes (Sep 3 → now)

Only two, both cosmetic:

| Key | Sep 3 read default | now | note |
|---|---|---|---|
| `keySwitch` | `""`→fallback `0x0` (arg shape) | literal `""` fallback+clamp | enum-parse hardening (commits around c270294), no behavior change |
| `keyReload` | same | same | |

`pedShadows` / `stencilShadows` are **not removed** — they moved to `parseShadowToggle()` (main.cpp:6493) and keep the identical `-1/0/1` semantics (boolMap with `-1` default both eras). `stochasticTexturing` similarly survived as a legacy alias of the new `noiseDetail` (main.cpp:6363-6371): `stochastic`/`stochasticTexturing` now both map to `Config::noiseDetail`.

## 3. New keys since Sep 3 (all read at main.cpp unless noted)

Grouped by domain. **These are the effective bisect levers that did not exist on Sep 3** — the "corruption" build differs from Sep-3 only through these + their defaults:

**State-integrity gates (the §4.3 core set):**
`defaultCbShaderHygiene` (1), `stateBaselineEnable` (1), `texAlphaClassify` (1), `spriteStateShield` (1), `spriteFlushShield` (1), `coronaBlendShield` (1), `effectsAlphaBlock` (1), `camRasterRepair` (1), `skidmarkAlphaFix` (1), `moonMaskAlpha` (1), `pedTexResync` (1), `shadowContractEnable` (0), `pedOpaqueContract` (1), `pedCullMode` (-1), `pedAlphaTestEnable` (1), `ambientFloor` (0.03), `pbrAmbientFloor` (0.0 repo / 0.05 live), `pedAmbientFloor` (0.20), `ambientMult` (1.0), `depthHookEnable` (1), `pipeChainReflEnable` (0), `cloudRenderFix` (1), `mirrorDistortionFix` (0), `reflMsaa` (`reflectionMsaaLevel`), `postfxDumpDebug`, `profilerEnable`, `profilerGpuTimers`, `ragdollEnable`, `altTimecycEnable` (alt-timecyc gate now inline, main.cpp:6819+).

**Watstyle/water (whole subsystem new):**
`waterStyle` (Xbox/IV/V routing — **new since Sep 3**; late-commits enablement per `waterPipe.cpp`), `waterQuality`, `waterFresnelSplit`, `waterParallaxEnable`/`waterParallaxScale`, `waterBodyClassify`, `waterUseTimecycle`, `waterShoreFoam`, `waterShallowR/G/B`, `waterDeepR/G/B`, `waterTileScale`, `waterShoreFade`, `waterTranslucency`, `waterReflectionStrength`, `waterSpecularPower`, `waterSpecularIntensity`, `waterFresnelPower`, `waterNormalStrength`, `waterUnderwaterFog`, `waterFoamThreshold`, `waterFoamSoftness`.

**Tonemap/exposure:** `tonemapAutoExposure`, `tonemapAdaptSpeed`, `tonemapKeyStrength`, `tonemapMinExposure`, `tonemapMaxExposure`, `tonemapCurveLows/Mid/Highs`, `tonemapBlackLift`, `tonemapOperator`.

**Post effects (all defaults 0 except noted):** `bloomEnable/Intensity/Quality/Threshold`, `fxaaEnable`, `dofEnable/FocusDistance/FocusRange/BokehRadius/MaxBlur`, `sunShaftsEnable/Density/Decay`, `grainIntensity`, `caIntensity`, `vignetteIntensity`, `sunCoronaIntensity`/`sunCoreIntensity`/`sunStreakIntensity`/`sunStreakSize`, `ssaoTemporalEnable`(1)/`ssaoTemporalBlend`/`ssaoBlurPasses`/`ssaoBlurRadius`/`ssaoDepthThreshold`, `sssEnable`(1)/`sssIntensity`/`sssSkin/Cloth/VegIntensity`/`sssAmbientBoost`/`sssPostProcessEnable`/`sssPostProcessStrength/Radius/Threshold`, `heightFogEnable`+7 params, `godRaysEnable`+6 params.

**Vehicle PBR (Modern-layer reflect model):** `vehEnvFallback`(1), `vehEnvStrengthMode`(1), `vehEnvMask`(1), `vehWetEnv`(1), `vehEnvGlint`, `vehEnvIntensity`, `vehChromeEnvThreshold`, `vehAutoChrome`(1), `chromeF0/Gloss/Metallic/EnvBoost/Clearcoat`, `bldEnvReflect`(1), `bldSkyReflect`(0.15), `pbrIblAmbientWeight`, `vehPrelightFallback`(0.15), `vehPBRLayers` (255, Modern), `gamePreset` (preset display regen; `presets.cpp` unchanged ⇒ preset *presets* did not change).

**Skin/hair/vegetation SSS/uplift:** `skinEnhanceEnable`, `skinWrapFactor`, `skinSpecularPower`, `skinSpecularStrength`, `skinSSSStrength`, `hairEnhanceEnable`, `hairAnisotropicPower`, `hairAnisotropicStrength`, `hairSSSStrength`, `vegetationEnhanceEnable`, `vegetationSSSStrength`, `vegetationAmbientBoost`.

**Forward+/omni:** `forwardPlusEnable`(1), `forwardPlusDebug`(0).

**Mode persistence / misc:** `[Modes] active=` (M1a — new section; the live INI reads `active = Xbox`), `[General] NoiseQuality` (now actually read; was write-only via preset before), `treeFx`, `treeWind`, `grassInstancing`, `noiseDetail` (+ alias move), `ragdollEnable`, `dofUnused?` (see exposure gaps).

**Sep-3 keys that ARE now ini-external (same key, other TU):** unchanged — `forwardPlusFallback` (declared default.ini:334 "read by forwardplus.cpp" — **but see §5 dead-key finding**), `enableNormalMaps` (normalmap.cpp:304, still Win32-read), `[Modes]`-owned aliases managed by modeManager.cpp profiles (M1a, new).

## 4. Live game INI vs current parser (E:\games\gtasa_skygfx_plus\skygfx.ini)

| Class | keys | notes |
|---|---|---|
| Set + consumed | full current parser set present incl. all §4.3 levers | live values: `defaultCbShaderHygiene=1`, `stateBaselineEnable=1`, `texAlphaClassify=1`, `dualPassPed=1`, `ambientFloor=0.03`, `pipeChainReflEnable=0`, `waterStyle=Xbox`, `shadowContractEnable=0` ✓ |
| Stale but alias-handled | `stochasticTexturing` | alias of `noiseDetail`, handled (main.cpp:6371) |
| Read externally | `forwardPlusFallback=1` (documented reader **doesn't exist in src/** — see §5), `enableNormalMaps` absent from live INI (external read OK) | |
| Dead (kept for compat) | — none beyond the above | `pedShadows=-1`, `stencilShadows=-1` live-writable ✓ |

## 5. Exposure gaps (active in code, no INI key, no ImGui menu entry)

The INTENT of this section is §4.3: lever candidates to bisect the renderer-wide corruption. Everything the §4.3 doc already lists (`defaultCbShaderHygiene`, `stateBaselineEnable`, `texAlphaClassify`, `dualPassPed`, `ambientFloor`, `pipeChainReflEnable`) **is already INI-exposed**. The gaps below are the ones that are still *hard-wired*.

### 5.1 Tier-1 gaps (hardcoded behavior on the affected paths)

| # | Feature | Where | Config member | State |
|---|---|---|---|---|
| G1 | **Cloud ground shadow strength (Xbox/cloud/ground path)** | vehicle c4 upload `vehEnvIblAndCloudShadow()` vehiclePipe.cpp:2364-2394 (`{0.0, cloudAnimTimer*0.01, 1.0, 0.0}`), postfx upload postfx.cpp:2802-2806 | `Config::cloudBaseStrength`, `Config::cloudShadowR/G/B`, `Config::cloudShadowStrength` (skygfx.h:501-504) | Fields exist but are **dead** — no reader, no INI, no menu. Strength/multiplier fully hardcoded (`cloudAnimTimer*0.01`). Zero-exposure bisect candidate for BOTH vehicle + sky paths. |
| G2 | **Water install-level hooks (W1/W5/SPLASH/pipe)** | main.cpp:8153-8173 (4-site `RenderAndEmptyRenderBuffer` intercept + `WaterDirectFlush1/2` 0x6EF8AC/0x6F0099 + `WakeDirectFlush1/2` 0x6EA2E8/0x6EDD7C), `hookSplashFoamFix()` main.cpp:8110-8114 | *(none)* | Install **unconditional by design** ("Independent of the neoWaterDrops lever on purpose", main.cpp:8112). Per-draw behavior is keyed (`waterStyle` etc.), but the *install/wrapper layer* has no kill switch. Water is a §4.3-suspect area — worth a `waterHooksEnable` master switch. |
| G3 | **`FX::GetFxQuality` redirects (7 sites)** | main.cpp:8062-8068 (0x5E675E/0x5E676D/0x706BC4/0x706BD3/0x7113B8/0x711D95/0x70F9B8) | *(none)* | Every ped + stencil FX-quality read decays to the PBR-era value; no lever. |
| G4 | **Core-correction hook bundle** | main.cpp:8276-8333: `_rwD3D9VSGetComposedTransformMatrix` 0x7646E0, `rxD3D9DefaultRenderCallback_Hook` 0x756DFE, `FrustumTestSphere/Box_Guard` 0x7FAD30/0x7FAD90, `PSSetCameraConstantD_Guard` 0x7618B0, `fixSeed` 0x5DADB7, `saveIntensity` 0x5DAE61, alpha-140-jump 0x553AD1, `0x4C88F0` vehicle-pipe-for-upgrade-parts | *(none)* | Byte-verified but semantically unconditional. Each is a candidate for a `cbPatchesEnable`-style enum key. |
| G5 | **`stateBaselineEnable` asymmetry** | 4 frame-boundary sites gated at main.cpp:3790/4322/4351/7878 ✓, but the `DeviceReset` call depthhook.cpp:421 is unconditional | `Config::stateBaselineEnable` | INI-levered; only the DeviceReset site bypasses the lever, meaning `stateBaselineEnable=0` does not fully suppress the baseline family. Explains the "feels like a no-op" report partially — it is 80% a lever, one site remains hard-wired. |
| G6 | **`pipeChainReflEnable` / pipe-chain toggles** | postfx.cpp:1657, 3397 on; `PFG_PIPECHAIN` bit | `Config::pipeChainEnable`/`pipeChainIntensity`/`pipeChainReflEnable` | **All INI-exposed already** ✓ (listing here to close the task's "pipe chain toggles" item — nothing missing). |

### 5.2 Tier-2 gaps (dead/intended-exposed-but-never-wired struct fields)

These Config members are neither read by render code nor exposed via INI/menu — a) they are traps if someone assumes they work, b) two of them are the natural home for G1/G2 exposure:

- `Config::sunDiscSize`, `Config::moonDiscSize` (sun/moon disc size — draw size hardcoded in the sky path)
- `Config::lightAmbDownWrap`, `Config::postfxIntensityBloom`, `Config::postfxDesaturation` (IV/postfx remembered params)
- `Config::cloudBaseStrength`, `Config::cloudShadowStrength`, `Config::cloudShadowR/G/B` (see G1)
- `Config::currentWeatherType`, `weatherTransition`, `timecycleOverrideHour` (runtime weather state, not config — exempt)
- `Config::unified*` family (26 fields) — **dead code**: `UploadUnifiedConstants()` (debugmenu_ui.cpp:140-199) has `return;` at the top, "never called from anywhere" per its own comment. Not candidates for exposure.

### 5.3 Menu-only (INI refused by design — separate note, not a defect)

- `Config::buildingPipe`/`Config::vehiclePipe` — mode-owned; absent key = "no explicit override" (documented in skygfx.default.ini header lines 20-26). ImGui menu has combos for them, so *menu* exposure exists but *INI* exposure deliberately does not; user can still bisect via the menu or `[Modes]`+`explicitBuildingPipe`.
- `doRadiosity` special value handling, `colorFilterEnable` set by mode profiles only.

## 6. Notes for the bisect (§4.3 correlation)

1. Every state-integrity gate the user listed is already a live comma-set of INI keys (`defaultCbShaderHygiene`, `stateBaselineEnable`, `texAlphaClassify`, `dualPassPed`, `ambientFloor`, `pipeChainReflEnable`). The corruption-causing set is most plausibly **inside the new-since-Sep3 keys of §3**, not in Sep-3 features that disappeared — none disappeared.
2. The 44 write-only keys of Sep 3 became fully read-write — this is the largest single behavioral difference vs Sep 3: on Sep 3, `readIni` simply *ignored* ~44 keys that today do take effect.
3. `[Modes] active=` (M1a) and mode-profile overrides (modeManager.cpp, `MV_I`/mode-owned defaults) are new since Sep 3; the live INI is `active=Xbox` ⇒ several of the §3 "new keys" (e.g. `waterStyle`, `render state` shields) are *engaged by profile*, not by top-level values.
4. `forwardPlusFallback` (live INI:67) has **no reader in current src/** despite being documented as "read by forwardplus.cpp" (default.ini:334). Matches the 898e894 "UNRESOLVED defaults" hygiene commit — treat as dead key.

---

### Appendix A — complete ADDED-key list (153 keys, sorted)

NoiseQuality, active([Modes]), altTimecycEnable, ambientFloor, ambientMult, bldEnvReflect, bldSkyReflect, bloomEnable, bloomIntensity, bloomQuality, bloomThreshold, caIntensity, camRasterRepair, chromeClearcoat, chromeEnvBoost, chromeF0, chromeGloss, chromeMetallic, cloudRenderFix, coronaBlendShield, defaultCbShaderHygiene, depthHookEnable, dofBokehRadius, dofEnable, dofFocusDistance, dofFocusRange, dofMaxBlur, effectsAlphaBlock, forwardPlusDebug, forwardPlusEnable, fxaaEnable, gamePreset, godRaysDecay, godRaysDensity, godRaysEnable, godRaysExposure, godRaysNumSamples, godRaysWeight, grainIntensity, grassInstancing, hairAnisotropicPower, hairAnisotropicStrength, hairEnhanceEnable, hairSSSStrength, heightFogB, heightFogDensity, heightFogEnable, heightFogG, heightFogHeightFalloff, heightFogR, heightFogStartHeight, heightFogTimecycleScale, mirrorDistortionFix, moonMaskAlpha, noiseDetail, pbrAmbientFloor, pbrIblAmbientWeight, pedAlphaTestEnable, pedAmbientFloor, pedCullMode, pedOpaqueContract, pedTexResync, pipeChainReflEnable, postfxDumpDebug, profilerEnable, profilerGpuTimers, ragdollEnable, reflectionMsaaLevel, shadowContractEnable, skidmarkAlphaFix, skinEnhanceEnable, skinSSSStrength, skinSpecularPower, skinSpecularStrength, skinWrapFactor, spriteFlushShield, spriteStateShield, ssaoBlurPasses, ssaoBlurRadius, ssaoDepthThreshold, ssaoTemporalBlend, ssaoTemporalEnable, sssAmbientBoost, sssClothIntensity, sssEnable, sssIntensity, sssPostProcessEnable, sssPostProcessRadius, sssPostProcessStrength, sssPostProcessThreshold, sssSkinIntensity, sssVegIntensity, stateBaselineEnable, stochastic(alias, alongside stochasticTexturing), sunCoreIntensity, sunCoronaIntensity, sunShaftsDecay, sunShaftsDensity, sunShaftsEnable, sunStreakIntensity, sunStreakSize, texAlphaClassify, tonemapAdaptSpeed, tonemapAutoExposure, tonemapBlackLift, tonemapCurveHighs, tonemapCurveLows, tonemapCurveMid, tonemapKeyStrength, tonemapMaxExposure, tonemapMinExposure, tonemapOperator, treeFx, treeWind, vegetationAmbientBoost, vegetationEnhanceEnable, vegetationSSSStrength, vehAutoChrome, vehChromeEnvThreshold, vehEnvFallback, vehEnvGlint, vehEnvIntensity, vehEnvMask, vehEnvStrengthMode, vehPBRLayers, vehPrelightFallback, vehWetEnv, vignetteIntensity, waterBodyClassify, waterDeepB, waterDeepG, waterDeepR, waterFoamSoftness, waterFoamThreshold, waterFresnelPower, waterFresnelSplit, waterNormalStrength, waterParallaxEnable, waterParallaxScale, waterQuality, waterReflectionStrength, waterShallowB, waterShallowG, waterShallowR, waterShoreFade, waterShoreFoam, waterSpecularIntensity, waterSpecularPower, waterStyle, waterTileScale, waterTranslucency, waterUnderwaterFog, waterUseTimecycle.

### Appendix B — method

- Baseline extraction: `git show c270294:src/core/main.cpp` piped through regex per-line capture of `cfg.get(section,key,default)` + `cfg.set` + `ADD_IF_MISSING`; verified `parseShadowToggle`/`StrAssoc` blocks manually.
- Live extraction: same regex over working-tree `src/core/main.cpp` (261 keys) + `cuda`?-none.
- Consumer check: `grep` for `config->FIELD` outside readIni/saveConfig/debugmenu/modeManager.
- Menu surface: identifier scan over src/core/debugmenu_ui.cpp (legacy `DebugMenuAddVar` + new `rEn/rWn` tables + ImGui section).
- Dead-field check: `grep` for each struct-field name across src/; zero consumer ⇒ dead (unless documented as runtime state).
