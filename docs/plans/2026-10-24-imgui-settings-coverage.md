# ImGui Settings Coverage and Debug-Menu Plan

**Date:** 2026-10-24
**Scope:** `src/skygfx.h` (`struct Config`), `src/core/main.cpp` (`readIniFile` / `saveConfigTo`), `src/core/debugmenu_ui.cpp`, `src/core/presets.cpp`, `src/core/modeManager.cpp`, `src/core/menu_inject.cpp`.
**Goal:** make the in-game debug menu display and edit every setting that matters for diagnosing the current rendering bugs (corona/alpha/blend, water, lighting, pipe chain/SSR, Forward+, normal maps, logging).

> **SNAPSHOT WARNING - READ FIRST.**
> `debugmenu_ui.cpp` was being edited while this audit ran. Its size changed from 125,453 bytes / 2,558 lines to 154,852 bytes / 3,220 lines between the first and last read (18:34-18:38 local). The matrix below is a snapshot of the **18:38** file plus `main.cpp` (18:18) and `skygfx.h` (18:13). Several items from the original gap list were fixed mid-audit (the five new keys and the other missing rows were added; `PBR / Modern` and the `Mobile` pipe label were corrected). Re-verify line numbers before editing, and treat the P1/P2 list as "remaining at snapshot", not "still broken forever".

---

## Executive summary

| Metric | Value |
|---|---|
| `Config` fields total | **436** |
| Fields parsed by `readIniFile` | **233** (incl. `keys[2]` via `keySwitch`/`keyReload`) |
| Fields with an ImGui row at snapshot | **235** (233 live + `vignetteRadius`/`vignetteContrast`, which have no parser and no consumer) |
| Live parsed fields with **no** menu row | **0** at 18:38 snapshot (was 7 earlier the same day) |
| Unparsed `Config` fields (intentionally hidden / dead) | **203** = 160 legacy weather/cloud/light/postFX + 5 weather-cycle + 32 unified-fork + 3 reserved + 3 internal |
| ImGui rows (incl. cross-tab mirrors) | **~315** unique rows, many keys mirrored on 2-3 tabs |
| Deployed INI keys | **233**; 5 have no `Config` field: `NoiseQuality`, `qualityPreset`, `cloudRenderFix`, `waterFresnelSplit`, `forwardPlusFallback` |
| Parsed keys **not written back** by `saveConfigTo` | **16** (4 live + 2 hotkeys + 10 startup/load-time; see 1.3) |

**Biggest remaining risks (details in Part 4):**

1. `stateBaselineEnable` is now a live-looking checkbox with **no consumer** - the row claims it gates `pipeApplyWorldBaseline`, but grep finds zero reads of the field outside parse/save. The UI currently lies about a state-integrity control that debuggers will reach for first.
2. **Save INI loses menu edits** for `waterStyle`, `waterQuality`, `waterFresnelSplit`, `cloudRenderFix`, `forwardPlusFallback`, `keySwitch`, `keyReload`, `NoiseQuality`. A referenced `MenuWriteMissingIniKeys()` does not exist yet (only a comment at `debugmenu_ui.cpp:25`).
3. **Range mismatches**: `tonemapMinExposure` menu `[0.2..4.0]` vs parser `[-0.5..0.5]`; `envPower` menu `[0..2000]` vs parser `[1..128]`; `envMapSize` slider does not snap to a power of two; `reflectionMsaaLevel` slider accepts values the parser discards.
4. **No device-reset handling for ImGui** (no `ImGui_ImplDX9_InvalidateDeviceObjects()` / `CreateDeviceObjects()` on `IDirect3DDevice9::Reset`). The menu can use a dead font texture / vertex buffers after Alt-Tab or a resolution change.
5. Two **dead-but-editable** knobs (`vignetteRadius`, `vignetteContrast`) and two no-op knobs (`ssaoKernelSize`, `ssaoSampleCount`) look live; no stored-but-inactive marker is shown for them.
6. The legacy `debugmenu.asi` menu (`installMenu`) remains a **second, divergent UI** with different ranges for the same fields (e.g. `ivGamma` 0.1-3.0 vs ImGui 0.5-2.0).

---

# PART 1 - Settings inventory (code is the source of truth)

## 1.1 How the inventory was produced

- Field list: `struct Config` at `src/skygfx.h:260-846` (436 fields; multi-declarator lines expanded).
- Parser: `readIniFile()` at `src/core/main.cpp:3945`; the parse region ends at `main.cpp:4819` (`if(!iniExisted)`).
- Write-back: `saveConfigTo()` at `src/core/main.cpp:5162-5494`.
- Deployed INI: `E:/games/gtasa_skygfx_plus/skygfx.ini` (233 keys, read 2026-10-24 19:44).
- Menu rows: `BuildSettingRows()` at `debugmenu_ui.cpp:890` (snapshot 18:38).
- Aliases are real: several Config fields are parsed from differently named INI keys (`doglare` from `sunGlare`, `offLeft` from `blurLeft`, `stochastic` from `stochasticTexturing`, `backfaceCull` from `grassBackfaceCull`, `bYCbCrFilter` from `YCbCrCorrection`, etc.). Appendix A shows the exact key for every field.

## 1.2 The five recently added keys

All five **exist in `Config`**, all five are **parsed**, and at the 18:38 snapshot all five are **exposed** (the last one, `pipeChainReflEnable`, was added during the audit):

| Key | `Config` field | Parse | Default | Clamp | Consumer | Menu row |
|---|---|---|---|---|---|---|
| `texAlphaClassify` | `skygfx.h:614` | `main.cpp:4513` | 1 | 0..1 | `buildingPipe.cpp:2111`, `vehiclePipe.cpp:2246` | `debugmenu_ui.cpp:1010` (Main / State integrity) |
| `stateBaselineEnable` | `skygfx.h:615` | `main.cpp:4516` | 1 | 0..1 | **NONE** (grep finds only parse/save/comments) | `debugmenu_ui.cpp:1014` |
| `defaultCbShaderHygiene` | `skygfx.h:616` | `main.cpp:4519` | 1 | 0..1 | `main.cpp:1067` (`myDefaultCallback`) | `debugmenu_ui.cpp:1018` |
| `ambientFloor` | `skygfx.h:620` | `main.cpp:4525` | 0.03 | 0..0.2 | `pipelinecommon.cpp:177` (`ClampAmbientToSky` tail) | `debugmenu_ui.cpp:1041` |
| `pipeChainReflEnable` | `skygfx.h:597` | `main.cpp:4505` | 0 | 0/1 by convention (no explicit clamp) | `postfx.cpp:3494` (reflection kill-switch) | `debugmenu_ui.cpp:1164` (Pipe chain tab) |

**Critical caveat:** the `stateBaselineEnable` row (`debugmenu_ui.cpp:1014-1017`) says "1 = pipeApplyWorldBaseline at the four frame boundaries ... 0 = off". The four `pipeApplyWorldBaseline()` calls (`main.cpp:2144`, `2636`, `2658`, `5639`) are **unconditional** and never consult `config->stateBaselineEnable`. The toggle is a no-op. This is the #1 "menu lies" defect at snapshot.

## 1.3 Config fields written by `saveConfigTo` vs keys with no Config field

`saveConfigTo()` writes **230** `[SkyGfx]` keys. It merges into the existing file, so an unwritten key keeps its old INI value - which means **menu edits to that key silently vanish from the next session**.

**Parsed keys that `saveConfigTo` does NOT write (16):**

| Class | Keys | Consequence |
|---|---|---|
| Live and menu-editable | `waterStyle`, `waterQuality`, `waterFresnelSplit`, `cloudRenderFix` | Menu edit appears to work, Save INI leaves the old value, next load reverts |
| Hotkeys | `keySwitch`, `keyReload` | Hotkey edits are not persisted by Save INI (only by hand-editing) |
| Load-time / startup-only | `qualityPreset`, `disableClouds`, `disableGamma`, `explicitBuildingPipe`, `fixPcCarLight`, `fixShadows`, `forceWindShader`, `privateHooks`, `ps2grassFiles`, `transparentLockon` | Greyed rows; INI keeps whatever was already there |

`forwardPlusFallback` is not parsed by `main.cpp` at all (it is read directly by `forwardplus.cpp:256`), so it is not in the 16; it is listed in the non-Config-key table below. `pedShadows`/`stencilShadows` are saved despite the script's regex miss (they are parsed through the `parseShadowToggle` lambda).

**Deployed INI keys with NO `Config` field (5):**

| Key | Owner | Menu handling at snapshot |
|---|---|---|
| `NoiseQuality` | `[General]`, TU-static `g_noiseQuality` (`main.cpp:688`) | Live via `menuGet/menuSetNoiseQuality` (`debugmenu_ui.cpp:1577`). Not written by Save INI. |
| `qualityPreset` | local in `readIniFile` (`main.cpp:4020`) | Greyed `rN` row (`debugmenu_ui.cpp:928`). Correctly load-time-only. |
| `cloudRenderFix` | global `bool` (`main.cpp:3377`) | Live row (`debugmenu_ui.cpp:1022`) but not saved. |
| `waterFresnelSplit` | global `int g_waterFresnelSplit` (`main.cpp:4768`) | Live row (`debugmenu_ui.cpp:1322`) but not saved. |
| `forwardPlusFallback` | read directly by `forwardplus.cpp:256` (`GetPrivateProfileIntA`), cached in `g_fpFallbackCfg` | Read-only `RK_INIINT` row (`debugmenu_ui.cpp:1599`); correct value read from active INI, restart required. |

No truly dead key remains in the deployed INI (`waterReflectionFarClip` was removed; stale entries are ignored).

## 1.4 Boot-time overrides - the INI does not always win

These are the "why does my INI value do nothing?" paths. A UI designer should surface the **effective** value, not only the raw field, for all of them.

| Precedence rule | Where | Effect |
|---|---|---|
| `pipeline` -> locked pipe/filter combo | `main.cpp:3970-4013` | `PBR` forces building=PBR, vehicle=Modern, filter=Modern |
| `buildingPipe` / `vehiclePipe` explicit keys override the combo | `main.cpp:4176-4203` | INI override wins over `pipeline` |
| `derivePipelineFromPipes()` re-derives `pipeline` from pipes | `main.cpp:603-679`, called from `main.cpp:4210`, `refreshIni` 723, menu every frame `debugmenu_ui.cpp:3068` | A mixed pipe state can change `pipeline` at runtime; PBR rule 3 **forces** building=PBR, vehicle=Modern, filter=Modern. |
| PBR forces `colorFilter=Modern` at load | `main.cpp:4384-4392` | `colorFilter=None` in INI is silently overridden |
| `ivMode` default follows pipeline when key absent | `main.cpp:4541-4542` | GTAIV pipes mean IV grade on |
| `qualityPreset` seeds features before keys | `main.cpp:4020-4170` | Preset can be stomped by missing-key defaults (`sunGlare`, `neoWaterDrops` use sentinel `-1` and force 0/off) |
| `sunGlare` default `-1` -> 0 + `iCanHasSunGlare=false` | `main.cpp:4304-4308` | Absent key disables glare even if ULTRA preset set `doglare=1` |
| `neoWaterDrops` default `-1` -> 0 + `iCanHasNeoDrops=false` | `main.cpp:4429-4433` | Same preset stomp |
| `ssaoPower` clamp `[0.5..4.0]` | `main.cpp:447-457` | Any value is rewritten at load; menu/legacy differ historically |
| `envFresnel` / `vehEnvIntensity` clamp `[0..0.4]` | `main.cpp:357-372` | Hard ceiling not visible in the INI docs |
| `tonemapMinExposure` clamp `[-0.5..0.5]`, max `[0.2..4.0]`, min<=max | `main.cpp:381-403` | Menu asks for `[0.2..4.0]` - see P2-1 |
| `envPower` clamp `[1..128]` | `main.cpp:4240-4247` | Menu asks for `[0..2000]` - see P2-2 |
| `envMapSize` rounded **up** to next power of two | `main.cpp:4283-4286` | Menu slider writes arbitrary ints |
| `reflectionMsaaLevel` snapped to `{0,2,4,8}`, anything else = 0 | `main.cpp:4292-4294` | Menu slider writes 1..8 |
| `waterQuality` clamp `[0..3]` | `main.cpp:4761-4763` | Combo matches |
| `treeFx` clamp `[0..3]`; `treeWind` pinned 0/1 | `main.cpp:4717-4729` | Menu matches `treeFx`; `treeWind` comment in `skygfx.h:558` says default 1 but parse default is 0 |
| `texAlphaClassify`, `stateBaselineEnable`, `defaultCbShaderHygiene` clamp 0/1 | `main.cpp:4513-4521` | Menu checkbox matches |
| `ambientFloor` clamp `[0..0.2]`, `ambientMult` `[0..2]` | `main.cpp:4525-4527`, `4564-4566` | Menu matches |
| `pbrAmbientFloor` default `0.0` | `main.cpp:4561` | `skygfx.h:679` comment says default 0.12 - stale doc |
| `stateBaselineEnable` no consumer | - | UI toggle is a no-op |

## 1.5 Parser defects worth fixing while the UI is being touched

1. **`ps2ModulateGlobal` and `dualPassGlobal` are read into `config`, not `c`** (`main.cpp:4172-4173`). Loading `configs[N]` mutates the active config. The menu's config selector makes this reachable.
2. **Zero-init self-fallback** (`Config configs[10]` is zero-initialized). Fields parsed with `c->field` as their default inherit 0 on the first load when the key is absent, not the documented default. The SSAO block was already fixed with literals (`main.cpp:4606-4615`); the same pattern remains on e.g. `sssPostProcessStrength`/`Radius`/`Threshold`, `motionBlur*`, `heightFog*`, `godRays*`, `ssaoRadius/Power`, `envMapFarClipMult`, `detailMaps`/`stochastic` (though presets usually seed them).
3. **`clampTonemapExposureRanges` comment is stale** (`main.cpp:374-380` says menu Min is `[-0.5..0.5]` and Max ends at 1.0; the current menu is `[0.2..4.0]`). The code clamps Min `[-0.5..0.5]`, the menu clamps Min `[0.2..4.0]` per frame (`debugmenu_ui.cpp:2284`). Three different stories.
4. **`skygfx.h` doc drift**: `treeWind` default 1 vs parse 0 (`skygfx.h:558`); `pbrAmbientFloor` default 0.12 vs parse 0.0 (`skygfx.h:679`); `tonemapMaxExposure` "2.0-at-read-then-clamped" vs parse 1.0 (`skygfx.h:788-790`).
5. **`tagsBuildingPipe`** is assigned `c->buildingPipe` (`main.cpp:4438`) but has no INI key and no consumer; keep hidden.

## Appendix A - every parsed field (233 rows)

Generated from `readIniFile` (parse line, literal or self-fallback default, clamp) and `BuildSettingRows` (widget macro + first row line at 18:38). "self" = `c->field` used as the fallback, i.e. zero-init on the first load.

| # | Config field | Type | INI key | Parse | Default | Parse clamp | Menu widget @ line | Group |
|---|---|---|---|---|---|---|---|---|
| 1 | `preset` | int | `gamePreset` | :4659 | PRESET_CUSTOM (-1) | - | `rSn` @ :930 | Main / Unified pipeline |
| 2 | `fixGrassPlacement` | RwBool | `grassFixPlacement` | :4314 | 0 | - | `rW` @ :1053 | Main / Grass |
| 3 | `doglare` | RwBool | `sunGlare` | :4304 | -1 -> 0 + `iCanHasSunGlare=false` | - | `rWn` @ :974 | Main / Effects |
| 4 | `pipeline` | int | `pipeline` | :3979 | map default (PBR) | - | `rEo` @ :924 | Main / Unified pipeline |
| 5 | `buildingPipe` | int | `buildingPipe` | :4183 | map default (-1 = keep) | - | `rEo` @ :934 | Main / Rendering pipes |
| 6 | `ps2ModulateBuilding` | RwBool | `ps2ModulateBuilding` | :4215 | global (self) | - | `rW` @ :1086 | Main / PS2 modulate detail |
| 7 | `dualPassBuilding` | RwBool | `dualPassBuilding` | :4216 | global (self) | - | `rW` @ :1077 | Main / Dual-pass detail |
| 8 | `bldEnvReflect` | RwBool | `bldEnvReflect` | :4223 | 1 | 0/1 | `rWn` @ :1099 | Main / Building reflection |
| 9 | `bldSkyReflect` | float | `bldSkyReflect` | :4226 | 0.15 | 0..1 | `rFn` @ :1104 | Main / Building reflection |
| 10 | `usePCTimecyc` | RwBool | `usePCTimecyc` | :4399 | 0 | - | `rW` @ :1004 | Main / Rendering features |
| 11 | `ps2ModulateGrass` | RwBool | `ps2ModulateGrass` | :4310 | global (self) | - | `rW` @ :1087 | Main / PS2 modulate detail |
| 12 | `grassAddAmbient` | RwBool | `grassAddAmbient` | :4312 | 0 | - | `rW` @ :1051 | Main / Grass |
| 13 | `backfaceCull` | RwBool | `grassBackfaceCull` | :4315 | 1 | - | `rW` @ :1052 | Main / Grass |
| 14 | `dualPassDefault` | RwBool | `dualPassDefault` | :4324 | global (self) | - | `rW` @ :1076 | Main / Dual-pass detail |
| 15 | `dualPassGrass` | RwBool | `dualPassGrass` | :4311 | global (self) | - | `rW` @ :1080 | Main / Dual-pass detail |
| 16 | `dualPassVehicle` | RwBool | `dualPassVehicle` | :4230 | global (self) | - | `rW` @ :1078 | Main / Dual-pass detail |
| 17 | `dualPassPed` | RwBool | `dualPassPed` | :4325 | global (self) | - | `rW` @ :1079 | Main / Dual-pass detail |
| 18 | `vehiclePipe` | int | `vehiclePipe` | :4201 | map default (-1 = keep) | - | `rEo` @ :936 | Main / Rendering pipes |
| 19 | `neoShininessMult` | float | `neoShininessMult` | :4232 | 1.0 | - | `rF` @ :1557 | Vehicle / Legacy car-pipe shading |
| 20 | `neoSpecularityMult` | float | `neoSpecularityMult` | :4233 | 1.0 | - | `rF` @ :1558 | Vehicle / Legacy car-pipe shading |
| 21 | `colorFilter` | int | `colorFilter` | :4378 | map default (PC) | PBR->Modern force | `rE` @ :939 | Main / Rendering pipes |
| 22 | `infraredVision` | int | `infraredVision` | :4396 | PS2/PC map default 1 | - | `rE` @ :976 | Main / Effects |
| 23 | `nightVision` | int | `nightVision` | :4397 | PS2/PC map default 1 | - | `rE` @ :977 | Main / Effects |
| 24 | `grainFilter` | int | `grainFilter` | :4398 | PS2/PC map default 1 | - | `rE` @ :1195 | Colour filter / Film grain |
| 25 | `doRadiosity` | RwBool | `doRadiosity` | :4410 | 4000 -> `original_bRadiosity` | - | `rW` @ :952 | Main / Effects |
| 26 | `radiosityFilterPasses` | int | `radiosityFilterPasses` | :4425 | 2 | - | `rI` @ :1172 | Radiosity / Radiosity |
| 27 | `radiosityRenderPasses` | int | `radiosityRenderPasses` | :4426 | 1 | - | `rI` @ :1173 | Radiosity / Radiosity |
| 28 | `radiosityIntensity` | int | `radiosityIntensity` | :4427 | 0x23 (35) | - | `rI` @ :1174 | Radiosity / Radiosity |
| 29 | `offLeft` | int | `blurLeft` | :4401 | 4000 -> default offset | - | `rIn` @ :1189 | Colour filter / crop |
| 30 | `offRight` | int | `blurRight` | :4405 | 4000 -> default offset | - | `rIn` @ :1191 | Colour filter / crop |
| 31 | `offTop` | int | `blurTop` | :4403 | 4000 -> default offset | - | `rIn` @ :1190 | Colour filter / crop |
| 32 | `offBottom` | int | `blurBottom` | :4407 | 4000 -> default offset | - | `rIn` @ :1192 | Colour filter / crop |
| 33 | `vcsTrails` | RwBool | `vcsTrails` | :4420 | 0 | - | `rW` @ :969 | Main / Effects |
| 34 | `trailsLimit` | int | `trailsLimit` | :4421 | 80 | - | `rI` @ :1267 | VCS trails |
| 35 | `trailsIntensity` | int | `trailsIntensity` | :4422 | 38 | - | `rI` @ :1268 | VCS trails |
| 36 | `trailsResolution` | int | `trailsResolution` | :4423 | 1 | - | `rI` @ :1269 | VCS trails |
| 37 | `pedShadows` | int | `pedShadows` | :4349 | -1 (sentinel) | -1..1 | `rEn` @ :1031 | Main / Shadows |
| 38 | `stencilShadows` | int | `stencilShadows` | :4350 | -1 (sentinel) | -1..1 | `rEn` @ :1034 | Main / Shadows |
| 39 | `lightningIlluminatesWorld` | int | `lightningIlluminatesWorld` | :4359 | 0 | - | `rE` @ :1066 | Main / Weather & timecycle |
| 40 | `neoWaterDrops` | RwBool | `neoWaterDrops` | :4429 | -1 -> 0 + `iCanHasNeoDrops=false` | - | `rW` @ :978 | Main / Effects |
| 41 | `neoBloodDrops` | RwBool | `neoBloodDrops` | :4434 | 0 | - | `rW` @ :979 | Main / Effects |
| 42 | `ps2ModulateGlobal` | RwBool | `ps2Modulate` | :4172 | 0 (written to `config`, not `c`!) | - | `rWn` @ :1002 | Main / Rendering features |
| 43 | `dualPassGlobal` | RwBool | `dualPass` | :4173 | 0 (written to `config`, not `c`!) | - | `rWn` @ :1000 | Main / Rendering features |
| 44 | `keys` | int[2] | `keySwitch` / `keyReload` | :3957 | keep current | - | `rIn` @ :1581 | Utility / Hotkeys |
| 45 | `bYCbCrFilter` | bool | `YCbCrCorrection` | :4464 | 0 | - | `rW8` @ :1178 | Colour filter / YCbCr |
| 46 | `lumaScale` | float | `lumaScale` | :4465 | 219/255 | - | `rF` @ :1179 | Colour filter / YCbCr |
| 47 | `lumaOffset` | float | `lumaOffset` | :4466 | 16/255 | - | `rF` @ :1180 | Colour filter / YCbCr |
| 48 | `cbScale` | float | `CbScale` | :4467 | 1.23 | - | `rF` @ :1181 | Colour filter / YCbCr |
| 49 | `cbOffset` | float | `CbOffset` | :4468 | 0.0 | - | `rF` @ :1182 | Colour filter / YCbCr |
| 50 | `crScale` | float | `CrScale` | :4469 | 1.23 | - | `rF` @ :1183 | Colour filter / YCbCr |
| 51 | `crOffset` | float | `CrOffset` | :4470 | 0.0 | - | `rF` @ :1184 | Colour filter / YCbCr |
| 52 | `rgb1Mult` | float | `rgb1Mult` | :4394 | 1.0 | - | `rF` @ :1185 | Colour filter / YCbCr |
| 53 | `rgb2Mult` | float | `rgb2Mult` | :4395 | 1.0 | - | `rF` @ :1186 | Colour filter / YCbCr |
| 54 | `zwriteThreshold` | int | `zwriteThreshold` | :4440 | 128 | 0..255 | `rI` @ :1081 | Main / Dual-pass detail |
| 55 | `zwriteThresholdGrass` | int | `zwriteThresholdGrass` | :4444 | 128 | 0..255 | `rI` @ :1082 | Main / Dual-pass detail |
| 56 | `zwriteThresholdPed` | int | `zwriteThresholdPed` | :4448 | 128 | 0..255 | `rI` @ :1083 | Main / Dual-pass detail |
| 57 | `leedsShininessMult` | float | `leedsShininessMult` | :4231 | 1.0 | - | `rF` @ :1556 | Vehicle / Legacy car-pipe shading |
| 58 | `detailMaps` | RwBool | `detailMaps` | :4212 | self | - | `rW` @ :995 | Main / Rendering features |
| 59 | `stochastic` | RwBool | `stochasticTexturing` | :4213 | self (zero on 1st load) | - | `rWn` @ :996 | Main / Rendering features |
| 60 | `envMapUseLODs` | RwBool | `envMapUseLODs` | :4288 | 0 | - | `rW` @ :1521 | Vehicle / Env map |
| 61 | `envMapSize` | int | `envMapSize` | :4283 | self | round **up** pow2 | `rI` @ :1520 | Vehicle / Env map |
| 62 | `envMapFarClipMult` | float | `envMapFarClipMult` | :4287 | self | - | `rF` @ :1522 | Vehicle / Env map |
| 63 | `reflectionMsaaLevel` | int | `reflectionMsaaLevel` | :4292 | 0 | snap {0,2,4,8} | `rIn` @ :1306 | Water / feature switches |
| 64 | `mirrorDistortionFix` | RwBool | `mirrorDistortionFix` | :4297 | 0 | - | `rWn` @ :1302 | Water / feature switches |
| 65 | `radiosity` | int | `radiosity` | :4418 | map default (Shader=1) | - | `rE` @ :1171 | Radiosity |
| 66 | `coronaZtest` | int | `coronaZtest` | :4452 | -1 | -1..1 | `rEn` @ :1067 | Main / Weather & timecycle |
| 67 | `sunCoronaIntensity` | float | `sunCoronaIntensity` | :4666 | self | - | `rFn` @ :1056 | Main / Sun & lens |
| 68 | `sunCoreIntensity` | float | `sunCoreIntensity` | :4667 | self | - | `rFn` @ :1058 | Main / Sun & lens |
| 69 | `sunStreakIntensity` | float | `sunStreakIntensity` | :4668 | self | - | `rFn` @ :1060 | Main / Sun & lens |
| 70 | `sunStreakSize` | float | `sunStreakSize` | :4669 | self | - | `rFn` @ :1062 | Main / Sun & lens |
| 71 | `altTimecycEnable` | int | `altTimecycEnable` | :4672 | 0 | - | `rWn` @ :1071 | Main / Weather & timecycle |
| 72 | `envShininessMult` | float | `envShininessMult` | :4234 | 1.0 | - | `rF` @ :1529 | Vehicle / Env map |
| 73 | `envSpecularityMult` | float | `envSpecularityMult` | :4235 | 1.0 | - | `rF` @ :1530 | Vehicle / Env map |
| 74 | `envPower` | float | `envPower` | :4236 | 20.0 | 1..128 | `rF` @ :1527 | Vehicle / Env map |
| 75 | `envFresnel` | float | `envFresnel` | :4251 | 0.4 | 0..0.4 | `rF` @ :1528 | Vehicle / Env map |
| 76 | `normalMapEnable` | RwBool | `normalMapEnable` | :4300 | 0 | - | `rWn` @ :990 | Main / Rendering features |
| 77 | `normalMapIntensity` | float | `normalMapIntensity` | :4301 | 1.0 | - | `rFn` @ :1090 | Main / Normal mapping detail |
| 78 | `normalMapPlayerOnly` | RwBool | `normalMapPlayerOnly` | :4302 | 1 | - | `rW` @ :1092 | Main / Normal mapping detail |
| 79 | `sssEnable` | RwBool | `sssEnable` | :4652 | 1 | - | `rW` @ :967 | Main / Effects |
| 80 | `sssIntensity` | float | `sssIntensity` | :4653 | self | - | `rF` @ :1241 | SSS |
| 81 | `sssVegIntensity` | float | `sssVegIntensity` | :4654 | self | - | `rF` @ :1242 | SSS |
| 82 | `sssSkinIntensity` | float | `sssSkinIntensity` | :4655 | self | - | `rF` @ :1243 | SSS |
| 83 | `sssClothIntensity` | float | `sssClothIntensity` | :4656 | self | - | `rF` @ :1244 | SSS |
| 84 | `ssaoEnable` | RwBool | `ssaoEnable` | :4473 | self | - | `rW` @ :947 | Main / Effects |
| 85 | `ssaoRadius` | float | `ssaoRadius` | :4474 | self | - | `rF` @ :1115 | SSAO |
| 86 | `ssaoPower` | float | `ssaoPower` | :4475 | self | 0.5..4.0 | `rF` @ :1116 | SSAO |
| 87 | `ssaoKernelSize` | float | `ssaoKernelSize` | :4476 | self | - | `rFn` @ :1117 | SSAO |
| 88 | `ssaoSampleCount` | int | `ssaoSampleCount` | :4477 | self | - | `rIn` @ :1119 | SSAO |
| 89 | `ssaoTemporalEnable` | RwBool | `ssaoTemporalEnable` | :4611 | 1 | - | `rW` @ :1121 | SSAO |
| 90 | `ssaoTemporalBlend` | float | `ssaoTemporalBlend` | :4612 | 0.1 | - | `rF` @ :1122 | SSAO |
| 91 | `ssaoBlurPasses` | int | `ssaoBlurPasses` | :4613 | 2 | - | `rI` @ :1123 | SSAO |
| 92 | `ssaoBlurRadius` | float | `ssaoBlurRadius` | :4614 | 3.0 | - | `rF` @ :1124 | SSAO |
| 93 | `ssaoDepthThreshold` | float | `ssaoDepthThreshold` | :4615 | 0.01 | - | `rF` @ :1125 | SSAO |
| 94 | `smaaEnable` | RwBool | `smaaEnable` | :4482 | self | - | `rW` @ :948 | Main / Effects |
| 95 | `motionBlurEnable` | RwBool | `motionBlurEnable` | :4485 | self | - | `rW` @ :951 | Main / Effects |
| 96 | `motionBlurStrength` | float | `motionBlurStrength` | :4486 | self | - | `rF` @ :1129 | Motion blur |
| 97 | `motionBlurRadial` | float | `motionBlurRadial` | :4487 | self | - | `rF` @ :1130 | Motion blur |
| 98 | `motionBlurSpeedFactor` | float | `motionBlurSpeedFactor` | :4488 | self | - | `rF` @ :1131 | Motion blur |
| 99 | `motionBlurCameraAware` | RwBool | `motionBlurCameraAware` | :4489 | self | - | `rW` @ :1132 | Motion blur |
| 100 | `sssPostProcessEnable` | RwBool | `sssPostProcessEnable` | :4643 | self | - | `rW` @ :968 | Main / Effects |
| 101 | `sssPostProcessStrength` | float | `sssPostProcessStrength` | :4618 | self | - | `rF` @ :1246 | SSS |
| 102 | `sssPostProcessRadius` | float | `sssPostProcessRadius` | :4619 | self | - | `rF` @ :1247 | SSS |
| 103 | `sssPostProcessThreshold` | float | `sssPostProcessThreshold` | :4620 | self | - | `rF` @ :1248 | SSS |
| 104 | `sssAmbientBoost` | float | `sssAmbientBoost` | :4624 | 1.0 | - | `rF` @ :1249 | SSS |
| 105 | `skinEnhanceEnable` | RwBool | `skinEnhanceEnable` | :4644 | self | - | `rW` @ :1252 | SSS / Material enhancement |
| 106 | `skinWrapFactor` | float | `skinWrapFactor` | :4627 | self | - | `rF` @ :1253 | SSS / Material enhancement |
| 107 | `skinSpecularPower` | float | `skinSpecularPower` | :4628 | self | - | `rF` @ :1254 | SSS / Material enhancement |
| 108 | `skinSpecularStrength` | float | `skinSpecularStrength` | :4629 | self | - | `rF` @ :1255 | SSS / Material enhancement |
| 109 | `skinSSSStrength` | float | `skinSSSStrength` | :4630 | self | - | `rF` @ :1256 | SSS / Material enhancement |
| 110 | `hairEnhanceEnable` | RwBool | `hairEnhanceEnable` | :4645 | self | - | `rW` @ :1257 | SSS / Material enhancement |
| 111 | `hairAnisotropicPower` | float | `hairAnisotropicPower` | :4633 | self | - | `rF` @ :1258 | SSS / Material enhancement |
| 112 | `hairAnisotropicStrength` | float | `hairAnisotropicStrength` | :4634 | self | - | `rF` @ :1259 | SSS / Material enhancement |
| 113 | `hairSSSStrength` | float | `hairSSSStrength` | :4635 | self | - | `rF` @ :1260 | SSS / Material enhancement |
| 114 | `vegetationEnhanceEnable` | RwBool | `vegetationEnhanceEnable` | :4646 | self | - | `rW` @ :1261 | SSS / Material enhancement |
| 115 | `vegetationSSSStrength` | float | `vegetationSSSStrength` | :4638 | self | - | `rF` @ :1262 | SSS / Material enhancement |
| 116 | `vegetationAmbientBoost` | float | `vegetationAmbientBoost` | :4639 | self | - | `rF` @ :1263 | SSS / Material enhancement |
| 117 | `ivMode` | RwBool | `ivMode` | :4541 | pipeline==GTAIV ? 1 : 0 | - | `A` @ :960 | Main / Effects |
| 118 | `ivDesaturation` | float | `ivDesaturation` | :4543 | self | - | `rFn` @ :1221 | IV grade |
| 119 | `ivGamma` | float | `ivGamma` | :4544 | self | - | `rFn` @ :1223 | IV grade |
| 120 | `ivSaturation` | float | `ivSaturation` | :4545 | self | - | `rF` @ :1225 | IV grade |
| 121 | `ivCurves` | float | `ivCurves` | :4546 | self | - | `rF` @ :1226 | IV grade |
| 122 | `ivVignetteIntensity` | float | `ivVignetteIntensity` | :4547 | self | - | `rFn` @ :1227 | IV grade |
| 123 | `ivVignetteRadius` | float | `ivVignetteRadius` | :4548 | self | - | `rFn` @ :1229 | IV grade |
| 124 | `ivVignetteContrast` | float | `ivVignetteContrast` | :4549 | self | - | `rFn` @ :1231 | IV grade |
| 125 | `ivBloomIntensity` | float | `ivBloomIntensity` | :4550 | self | - | `rFn` @ :1233 | IV grade |
| 126 | `ivExposure` | float | `ivExposure` | :4551 | self | - | `rFn` @ :1235 | IV grade |
| 127 | `vignetteIntensity` | float | `vignetteIntensity` | :4812 | 0.0 | 0..1 | `rFn` @ :1440 | PostFX / Vignette |
| 128 | `waterParallaxEnable` | RwBool | `waterParallaxEnable` | :4685 | 1 | - | `rWn` @ :972 | Main / Effects |
| 129 | `waterParallaxScale` | float | `waterParallaxScale` | :4686 | 1.0 | - | `rF` @ :1280 | Water / appearance |
| 130 | `waterNormalStrength` | float | `waterNormalStrength` | :4687 | 1.0 | - | `rF` @ :1281 | Water / appearance |
| 131 | `waterFresnelPower` | float | `waterFresnelPower` | :4688 | 5.0 | - | `rF` @ :1282 | Water / appearance |
| 132 | `waterSpecularPower` | float | `waterSpecularPower` | :4689 | 512.0 | - | `rF` @ :1283 | Water / appearance |
| 133 | `waterSpecularIntensity` | float | `waterSpecularIntensity` | :4690 | 1.0 | - | `rF` @ :1284 | Water / appearance |
| 134 | `waterUnderwaterFog` | float | `waterUnderwaterFog` | :4691 | 1.0 | - | `rF` @ :1285 | Water / appearance |
| 135 | `grassInstancing` | RwBool | `grassInstancing` | :4733 | 0 | pin 0/1 | `rWn` @ :1470 | Vegetation |
| 136 | `waterFoamThreshold` | float | `waterFoamThreshold` | :4692 | 0.6 | - | `rF` @ :1286 | Water / appearance |
| 137 | `waterFoamSoftness` | float | `waterFoamSoftness` | :4693 | 0.2 | - | `rF` @ :1287 | Water / appearance |
| 138 | `waterShallowR` | float | `waterShallowR` | :4700 | 0.13 | - | `rF` @ :1288 | Water / appearance |
| 139 | `waterShallowG` | float | `waterShallowG` | :4701 | 0.45 | - | `rF` @ :1289 | Water / appearance |
| 140 | `waterShallowB` | float | `waterShallowB` | :4702 | 0.55 | - | `rF` @ :1290 | Water / appearance |
| 141 | `waterDeepR` | float | `waterDeepR` | :4703 | 0.01 | - | `rF` @ :1291 | Water / appearance |
| 142 | `waterDeepG` | float | `waterDeepG` | :4704 | 0.03 | - | `rF` @ :1292 | Water / appearance |
| 143 | `waterDeepB` | float | `waterDeepB` | :4705 | 0.08 | - | `rF` @ :1293 | Water / appearance |
| 144 | `waterTileScale` | float | `waterTileScale` | :4709 | 1.0 | - | `rF` @ :1332 | Water / feature switches |
| 145 | `waterShoreFade` | float | `waterShoreFade` | :4710 | 2.0 | - | `rF` @ :1333 | Water / feature switches |
| 146 | `waterTranslucency` | float | `waterTranslucency` | :4711 | 1.0 | - | `rF` @ :1334 | Water / feature switches |
| 147 | `waterReflectionStrength` | float | `waterReflectionStrength` | :4712 | 0.55 | - | `rF` @ :1331 | Water / feature switches |
| 148 | `waterUseTimecycle` | int | `waterUseTimecycle` | :4713 | 1 | - | `rWn` @ :1275 | Water / style & quality |
| 149 | `waterShoreFoam` | RwBool | `waterShoreFoam` | :4696 | 1 | pin 0/1 | `rWn` @ :1314 | Water / feature switches |
| 150 | `waterBodyClassify` | RwBool | `waterBodyClassify` | :4697 | 1 | pin 0/1 | `rWn` @ :1318 | Water / feature switches |
| 151 | `treeWind` | RwBool | `treeWind` | :4728 | 0 | pin 0/1 | `rWn` @ :1474 | Vegetation |
| 152 | `treeFx` | int | `treeFx` | :4717 | 0 | 0..3 | `rEo` @ :1480 | Vegetation |
| 153 | `waterStyle` | int | `waterStyle` | :4760 | map default (Xbox=0) | - | `rE` @ :938 | Main / Rendering pipes |
| 154 | `waterQuality` | int | `waterQuality` | :4761 | 3 | 0..3 | `rE` @ :1274 | Water / style & quality |
| 155 | `normalBufferEnable` | RwBool | `normalBufferEnable` | :4498 | 1 | - | `rWn` @ :992 | Main / Rendering features |
| 156 | `normalBufferOffset` | float | `normalBufferOffset` | :4499 | 0.5 | - | `rF` @ :1095 | Main / Normal buffer detail |
| 157 | `normalBufferScale` | float | `normalBufferScale` | :4500 | 1.0 | - | `rF` @ :1096 | Main / Normal buffer detail |
| 158 | `pipeChainEnable` | RwBool | `pipeChainEnable` | :4503 | 0 | - | `rWn` @ :970 | Main / Effects |
| 159 | `pipeChainIntensity` | float | `pipeChainIntensity` | :4504 | 0.3 | - | `rFn` @ :1162 | Pipe chain |
| 160 | `pipeChainReflEnable` | int | `pipeChainReflEnable` | :4505 | 0 | - | `rWn` @ :1164 | Pipe chain |
| 161 | `texAlphaClassify` | int | `texAlphaClassify` | :4513 | 1 | 0..1 | `rWn` @ :1010 | Main / State integrity |
| 162 | `stateBaselineEnable` | int | `stateBaselineEnable` | :4516 | 1 | 0..1 | `rWn` @ :1014 | Main / State integrity |
| 163 | `defaultCbShaderHygiene` | int | `defaultCbShaderHygiene` | :4519 | 1 | 0..1 | `rWn` @ :1018 | Main / State integrity |
| 164 | `ambientFloor` | float | `ambientFloor` | :4525 | 0.03 | 0..0.2 | `rFn` @ :1041 | Main / Lighting & ambient |
| 165 | `colorFilterEnable` | RwBool | `colorFilterEnable` | :4532 | 1 | - | `A` @ :954 | Main / Effects |
| 166 | `radiosityEnable` | RwBool | `radiosityEnable` | :4533 | 1 | - | `rW` @ :953 | Main / Effects |
| 167 | `grainEnable` | RwBool | `grainEnable` | :4534 | 1 | - | `rW` @ :966 | Main / Effects |
| 168 | `pipelineOverride` | int | `pipelineOverride` | :3982 | -1 | -1..4 (gate) | `rIo` @ :926 | Main / Unified pipeline |
| 169 | `postfxDumpDebug` | RwBool | `postfxDumpDebug` | :4535 | 0 | - | `rW` @ :1110 | Main / Debug switches |
| 170 | `profilerEnable` | int | `profilerEnable` | :4742 | 1 | pin 0/1 | `A` @ :1564 | Profiler |
| 171 | `profilerGpuTimers` | int | `profilerGpuTimers` | :4743 | 0 | pin 0/1 | `A` @ :1569 | Profiler |
| 172 | `heightFogEnable` | RwBool | `heightFogEnable` | :4589 | self | - | `rW` @ :950 | Main / Effects |
| 173 | `heightFogDensity` | float | `heightFogDensity` | :4590 | self | - | `rFn` @ :1144 | Height fog |
| 174 | `heightFogHeightFalloff` | float | `heightFogHeightFalloff` | :4591 | self | - | `rFn` @ :1146 | Height fog |
| 175 | `heightFogStartHeight` | float | `heightFogStartHeight` | :4592 | self | - | `rF` @ :1148 | Height fog |
| 176 | `heightFogR` | float | `heightFogR` | :4593 | self | - | `rF` @ :1149 | Height fog |
| 177 | `heightFogG` | float | `heightFogG` | :4594 | self | - | `rF` @ :1150 | Height fog |
| 178 | `heightFogB` | float | `heightFogB` | :4595 | self | - | `rF` @ :1151 | Height fog |
| 179 | `heightFogTimecycleScale` | float | `heightFogTimecycleScale` | :4596 | self | - | `rFn` @ :1152 | Height fog |
| 180 | `godRaysEnable` | RwBool | `godRaysEnable` | :4599 | self | - | `rW` @ :949 | Main / Effects |
| 181 | `godRaysExposure` | float | `godRaysExposure` | :4600 | self | - | `rF` @ :1136 | God rays |
| 182 | `godRaysDecay` | float | `godRaysDecay` | :4601 | self | - | `rF` @ :1137 | God rays |
| 183 | `godRaysDensity` | float | `godRaysDensity` | :4602 | self | - | `rF` @ :1138 | God rays |
| 184 | `godRaysWeight` | float | `godRaysWeight` | :4603 | self | - | `rF` @ :1139 | God rays |
| 185 | `godRaysNumSamples` | int | `godRaysNumSamples` | :4604 | self | - | `rI` @ :1140 | God rays |
| 186 | `velocityBufferEnable` | RwBool | `velocityBufferEnable` | :4492 | 1 | - | `rW` @ :994 | Main / Rendering features |
| 187 | `forwardPlusEnable` | RwBool | `forwardPlusEnable` | :4662 | 1 | - | `rW` @ :982 | Main / Rendering features |
| 188 | `forwardPlusDebug` | RwBool | `forwardPlusDebug` | :4663 | 0 | - | `rIn` @ :987 | Main / Rendering features |
| 189 | `depthHookEnable` | RwBool | `depthHookEnable` | :4495 | 1 | - | `rW` @ :998 | Main / Rendering features |
| 190 | `ragdollEnable` | RwBool | `ragdollEnable` | :4557 | 1 | - | `rW` @ :999 | Main / Rendering features |
| 191 | `pbrAmbientFloor` | float | `pbrAmbientFloor` | :4561 | 0.0 | - | `rFn` @ :1044 | Main / Lighting & ambient |
| 192 | `ambientMult` | float | `ambientMult` | :4564 | 1.0 | 0..2 | `rFn` @ :1038 | Main / Lighting & ambient |
| 193 | `tonemapBlackLift` | float | `tonemapBlackLift` | :4568 | 0.0 | - | `rF` @ :1216 | Tonemap |
| 194 | `vehPBRLayers` | int | `vehPBRLayers` | :4253 | 255 | - | `rIn` @ :1549 | Vehicle / PBR vehicle |
| 195 | `vehPrelightFallback` | float | `vehPrelightFallback` | :4572 | 0.15 | 0..1 | `rF` @ :1553 | Vehicle / PBR vehicle |
| 196 | `vehEnvFallback` | RwBool | `vehEnvFallback` | :4579 | 1 | - | `rW` @ :1539 | Vehicle / Env map |
| 197 | `vehEnvStrengthMode` | RwBool | `vehEnvStrengthMode` | :4580 | 1 | - | `rW` @ :1540 | Vehicle / Env map |
| 198 | `vehEnvMask` | RwBool | `vehEnvMask` | :4581 | 1 | - | `rW` @ :1541 | Vehicle / Env map |
| 199 | `vehWetEnv` | RwBool | `vehWetEnv` | :4582 | 1 | - | `rW` @ :1542 | Vehicle / Env map |
| 200 | `vehEnvGlint` | float | `vehEnvGlint` | :4583 | 0.0 | 0..1 | `rFn` @ :1543 | Vehicle / Env map |
| 201 | `vehEnvIntensity` | float | `vehEnvIntensity` | :4254 | 1.0 | 0..0.4 | `rF` @ :1531 | Vehicle / Env map |
| 202 | `vehChromeEnvThreshold` | float | `vehChromeEnvThreshold` | :4259 | 0.0 | >=0 | `rFn` @ :1516 | Vehicle / Chrome |
| 203 | `vehAutoChrome` | RwBool | `vehAutoChrome` | :4262 | 1 | pin 0/1 | `rW` @ :1515 | Vehicle / Chrome |
| 204 | `chromeF0` | float | `chromeF0` | :4263 | 0.56 | 0..1 | `rF` @ :1510 | Vehicle / Chrome |
| 205 | `chromeGloss` | float | `chromeGloss` | :4264 | 0.90 | 0..1 | `rF` @ :1511 | Vehicle / Chrome |
| 206 | `chromeMetallic` | float | `chromeMetallic` | :4265 | 1.0 | 0..1 | `rF` @ :1512 | Vehicle / Chrome |
| 207 | `chromeEnvBoost` | float | `chromeEnvBoost` | :4266 | 1.0 | 0..4 | `rF` @ :1513 | Vehicle / Chrome |
| 208 | `chromeClearcoat` | float | `chromeClearcoat` | :4267 | 0.6 | 0..1 | `rF` @ :1514 | Vehicle / Chrome |
| 209 | `tonemapAutoExposure` | int | `tonemapAutoExposure` | :4269 | 1 | - | `rW` @ :1203 | Tonemap |
| 210 | `tonemapAdaptSpeed` | float | `tonemapAdaptSpeed` | :4270 | 0.12 | 0.01..1 | `rF` @ :1204 | Tonemap |
| 211 | `tonemapKeyStrength` | float | `tonemapKeyStrength` | :4271 | 1.0 | 0..1 | `rF` @ :1205 | Tonemap |
| 212 | `tonemapMinExposure` | float | `tonemapMinExposure` | :4272 | 0.5 | **-0.5..0.5** | `rFn` @ :1206 | Tonemap |
| 213 | `tonemapMaxExposure` | float | `tonemapMaxExposure` | :4275 | 1.0 | 0.2..4.0 | `rFn` @ :1208 | Tonemap |
| 214 | `tonemapCurveLows` | float | `tonemapCurveLows` | :4277 | 0.0 | -0.35..0.35 | `rFn` @ :1210 | Tonemap |
| 215 | `tonemapCurveHighs` | float | `tonemapCurveHighs` | :4278 | 0.0 | -0.35..0.35 | `rFn` @ :1212 | Tonemap |
| 216 | `tonemapCurveMid` | float | `tonemapCurveMid` | :4279 | 0.5 | 0..1 | `rFn` @ :1214 | Tonemap |
| 217 | `pbrIblAmbientWeight` | float | `pbrIblAmbientWeight` | :4567 | 0.5 | - | `rFn` @ :1047 | Main / Lighting & ambient |
| 218 | `dofEnable` | int | `dofEnable` | :4775 | 0 | - | `rW` @ :1355 | PostFX |
| 219 | `dofFocusDistance` | float | `dofFocusDistance` | :4776 | 20.0 | >=0 | `rF` @ :1424 | PostFX / DOF |
| 220 | `dofFocusRange` | float | `dofFocusRange` | :4778 | 15.0 | >=0 | `rF` @ :1425 | PostFX / DOF |
| 221 | `dofBokehRadius` | float | `dofBokehRadius` | :4780 | 4.0 | >=0 | `rF` @ :1426 | PostFX / DOF |
| 222 | `dofMaxBlur` | float | `dofMaxBlur` | :4782 | 0.5 | 0..1 | `rF` @ :1427 | PostFX / DOF |
| 223 | `tonemapOperator` | int | `tonemapOperator` | :4785 | 0 | 0..5 | `rEn` @ :1398 | PostFX / Tonemap LUT |
| 224 | `bloomEnable` | int | `bloomEnable` | :4788 | 0 | - | `rW` @ :1353 | PostFX |
| 225 | `bloomQuality` | int | `bloomQuality` | :4789 | 2 | 0..3 | `rEn` @ :1412 | PostFX / Bloom |
| 226 | `bloomThreshold` | float | `bloomThreshold` | :4792 | 1.0 | >=0 | `rF` @ :1414 | PostFX / Bloom |
| 227 | `bloomIntensity` | float | `bloomIntensity` | :4794 | 0.8 | >=0 | `rF` @ :1415 | PostFX / Bloom |
| 228 | `sunShaftsEnable` | int | `sunShaftsEnable` | :4796 | 0 | - | `rW` @ :1354 | PostFX |
| 229 | `sunShaftsDensity` | float | `sunShaftsDensity` | :4797 | 0.84 | 0..1 | `rF` @ :1419 | PostFX / Sun shafts |
| 230 | `sunShaftsDecay` | float | `sunShaftsDecay` | :4800 | 1.0 | 0..1 | `rF` @ :1420 | PostFX / Sun shafts |
| 231 | `fxaaEnable` | int | `fxaaEnable` | :4803 | 0 | - | `rWn` @ :1349 | PostFX |
| 232 | `grainIntensity` | float | `grainIntensity` | :4804 | 0.0 | 0..1 | `rFn` @ :1431 | PostFX / Film grain |
| 233 | `caIntensity` | float | `caIntensity` | :4807 | 0.0 | 0..1 | `rFn` @ :1436 | PostFX / Chromatic aberration |

## Appendix B - Config fields with no INI key / no consumer (203)

These are exhaustive. All have **no `cfg.get` read** and (spot-checked plus grep-verified) **no reader outside `skygfx.h`**. Keep them hidden; if one gains a consumer, move it into `BuildSettingRows` rather than leaving it invisible.

**Legacy Expanded Weather/Timecycle + clouds + lights + postFX/grading/lens block (160 fields, `skygfx.h:454-524`):**
`skyZenithR`, `skyZenithG`, `skyZenithB`, `skyZenithInten`, `skyZenithTransR`, `skyZenithTransG`, `skyZenithTransB`, `skyZenithTransInten`, `skyAzimuthEastR`, `skyAzimuthEastG`, `skyAzimuthEastB`, `skyAzimuthEastInten`, `skyAzimuthTransR`, `skyAzimuthTransG`, `skyAzimuthTransB`, `skyAzimuthTransInten`, `skyAzimuthWestR`, `skyAzimuthWestG`, `skyAzimuthWestB`, `skyAzimuthWestInten`, `skyPlaneR`, `skyPlaneG`, `skyPlaneB`, `skyPlaneInten`, `sunR`, `sunG`, `sunB`, `sunDiscR`, `sunDiscG`, `sunDiscB`, `sunDiscSize`, `sunMiePhase`, `sunMieScatter`, `sunMieIntenMult`, `sunInfluenceRadius`, `sunScatterInten`, `moonR`, `moonG`, `moonB`, `moonDiscSize`, `moonInten`, `starsInten`, `moonInfluenceRadius`, `moonScatterInten`, `cloudGenFreq`, `cloudGenScale`, `cloudGenThresh`, `cloudGenSoftness`, `cloudDensityMult`, `cloudDensityBias`, `cloudMidR`, `cloudMidG`, `cloudMidB`, `cloudBaseR`, `cloudBaseG`, `cloudBaseB`, `cloudBaseStrength`, `cloudShadowR`, `cloudShadowG`, `cloudShadowB`, `cloudShadowStrength`, `cloudGenDensityOffset`, `cloudOffset`, `cloudOverallStrength`, `cloudOverallColor`, `cloudEdgeStrength`, `cloudFadeout`, `cloudHDR`, `cloudDitherStrength`, `smallCloudR`, `smallCloudG`, `smallCloudB`, `smallCloudDetailStrength`, `smallCloudDetailScale`, `smallCloudDensityMult`, `smallCloudDensityBias`, `lightDirR`, `lightDirG`, `lightDirB`, `lightDirMult`, `lightDirAmbR`, `lightDirAmbG`, `lightDirAmbB`, `lightDirAmbInten`, `lightDirAmbIntenMult`, `lightDirAmbBounce`, `lightAmbDownWrap`, `lightNatAmbDownR`, `lightNatAmbDownG`, `lightNatAmbDownB`, `lightNatAmbDownInten`, `lightNatAmbBaseR`, `lightNatAmbBaseG`, `lightNatAmbBaseB`, `lightNatAmbBaseInten`, `lightNatAmbBaseIntenMult`, `lightArtifIntAmbDownR`, `lightArtifIntAmbDownG`, `lightArtifIntAmbDownB`, `lightArtifIntAmbDownInten`, `lightArtifIntAmbBaseR`, `lightArtifIntAmbBaseG`, `lightArtifIntAmbBaseB`, `lightArtifIntAmbBaseInten`, `lightArtifExtAmbDownR`, `lightArtifExtAmbDownG`, `lightArtifExtAmbDownB`, `lightArtifExtAmbDownInten`, `lightArtifExtAmbBaseR`, `lightArtifExtAmbBaseG`, `lightArtifExtAmbBaseB`, `lightArtifExtAmbBaseInten`, `pedLightR`, `pedLightG`, `pedLightB`, `pedLightMult`, `pedLightDirX`, `pedLightDirY`, `pedLightDirZ`, `postfxExposure`, `postfxExposureMin`, `postfxExposureMax`, `postfxBrightPassThreshWidth`, `postfxBrightPassThresh`, `postfxIntensityBloom`, `postfxCorrectR`, `postfxCorrectG`, `postfxCorrectB`, `postfxCorrectCutoff`, `postfxShiftR`, `postfxShiftG`, `postfxShiftB`, `postfxShiftCutoff`, `postfxDesaturation`, `postfxNoise`, `postfxNoiseSize`, `vignetteRadius`, `vignetteContrast`, `vignetteR`, `vignetteG`, `vignetteB`, `gradTopR`, `gradTopG`, `gradTopB`, `gradMidR`, `gradMidG`, `gradMidB`, `gradBotR`, `gradBotG`, `gradBotB`, `gradMidpoint`, `gradTopMidMidpoint`, `gradMidBotMidpoint`, `lensDistortionCoeff`, `lensDistortionCubeCoeff`, `lensChromaticAberrationCoeff`, `lensChromaticAberrationCubeCoeff`, `lensArtefactsInten`, `lensArtefactsIntenMinExp`, `lensArtefactsIntenMaxExp` (all float).
**Exception:** `vignetteRadius` / `vignetteContrast` currently have editable rows (`debugmenu_ui.cpp:1442`, `:1444`) despite having no parser and no consumer - see P2-2.

**Legacy weather-cycle block (5, `skygfx.h:564-568`):** `currentWeatherType` (int), `weatherTransition` (float), `weatherCycleEnabled` (RwBool), `timecycleOverrideHour` (int), `timecycleOverrideEnabled` (RwBool).

**Dead JuniorDjjr unified-pipeline block (32, `skygfx.h:574-587`):** `unifiedEnable`, `unifiedVersion`, `unifiedSatBoost`, `unifiedIblTintStrength`, `unifiedSsaoNoiseScale`, `unifiedShadowSoftness`, `unifiedCloudShadowStr`, `unifiedSunShadowStr`, `unifiedVertexAOBoost`, `unifiedDayReduction`, `unifiedPointLightOverride`, `unifiedSmaaThreshold`, `unifiedSmaaCornerRounding`, `unifiedSmaaMaxSearchSteps`, `unifiedShowMenu`, `unifiedShowOverlay`, `unifiedDebugOcclusion`, `unifiedEnablePrePass`, `unifiedEnableEdgeDetect`, `unifiedEnableOcclusion`, `unifiedEnableStoredShadows`, `unifiedEnableCloudShadows`, `unifiedEnableSunShadows`, `unifiedEnableTimeOfDay`, `unifiedEnableVertexAO`, `unifiedEnablePointLightOverride`, `unifiedEnablePostPass`, `unifiedEnableIBL`, `unifiedEnableIBLTint`, `unifiedEnableSurfaceWeights`, `unifiedEnableGrading`, `unifiedEnableGamma`. Only referenced by `UploadUnifiedConstants()`, which returns immediately (`debugmenu_ui.cpp:411-413`).

**Reserved/padding (3, `skygfx.h:393-395`):** `_reserved_smaaPreset` (int), `_reserved_smaaPredication` (RwBool), `_reserved_smaaTemporal` (RwBool).

**Internal (3):** `version` (int, module ABI), `tagsBuildingPipe` (int, mirror of `buildingPipe`), `debugMenuOpen` (RwBool, runtime UI flag).

---

# PART 2 - Menu coverage and correctness

## 2.1 Architecture

There are **two independent UIs** in `debugmenu_ui.cpp`:

1. **Legacy `debugmenu.asi` menu** - `installMenu()` at `debugmenu_ui.cpp:208`. It registers `DebugMenuEntry*` variables with string combo arrays and per-entry min/max/step. It is a second source of ranges for ~90 fields. `refreshMenu()` (`:163`) re-points entries when the Config selector moves.
2. **The Dear ImGui menu** - a data-driven table of `SettingRow` (`debugmenu_ui.cpp:614-635`) built by `BuildSettingRows()` (`:890`). Each row stores: tab, group, INI key, label, `RowKind`, `void*` into the live variable, `lo/hi`, enum names/base, `onEdit` hook, tooltip note. `DrawRowsForTab()` (`:2444` region) emits `CollapsingHeader` groups; `DrawRowControl()` (`:2169`) maps `RowKind` to `Checkbox` / `SliderInt` / `SliderFloat` / `BeginCombo` / grey `TextDisabled`. Rows are rebuilt when the `config` pointer changes (`GetSettingRows`, `:1634`).
   - New at snapshot: per-row right-click **`RowResetPopup`** (`:1922`), **`RowHardOverride`** effective-value disclosure (`:1786`), **`RowInactiveReason`** stored-but-not-in-effect notes (`:1810`), and a `kKeyDefaults[]` table (`:1680-1765`) with parser defaults. `RowHardOverride`/`RowInactiveReason`/`RowResetPopup` are wired into the row draw at `:2337-2360` and `:2381-2406`.
   - `GetCoverageRows()` (`:2060`) builds a deduplicated "All Settings" list grouped by subsystem, but **no `BeginTabItem("All Settings")` exists yet** (`:3038-3130` lists only Main/Vehicle/Vegetation/PostFX/Utility/Profiler/Presets/INI Keys).
3. **Native pause-menu injection** - `menu_inject.cpp`, compiled but disabled (`main.cpp:5804`: `// menu_inject_init();`). It would add a SkyGFX page to `CMenuManager`; its 10 options bind `bool*` to `RwBool` fields and cycle `buildingPipe` with a 4-name table. Treat it as dead unless explicitly revived.

**Where values are written back:** row edits write **directly into the live `config` field** (or a global), then `FinishRowEdit()` (`:2266`) calls the row's `onEdit` and `resetValues()`. `resetValues()` (`main.cpp:296-350`) only copies config into the legacy `CPostEffects` mirrors and applies the corona z-test patch; it is **not** a save-to-disk and **not** a reset-to-default. Disk persistence happens only when the user presses **Save INI** (`saveConfig` -> `saveConfigTo`, `main.cpp:5496`), **Save as New Config** (`saveConfigTo(path)`), or the legacy native menu's toggle/cycle handler (`menu_inject.cpp:360-388`, if enabled).

**Raw vs effective:** all `SettingRow`s bind the raw `config` field. `RowHardOverride` is the only effective-value display (currently only `colorFilter` under `pipeline==PBR`). `RowInactiveReason` is a stored-but-inactive note, not a value. `DrawMainPane` shows two live Forward+ counters (`g_fpGpuLightCount`, `g_fpNumLights`) and `ForwardPlus_StatsText`, and the Profiler tab shows live snapshots; no other tab reads derived/runtime values.

**Presets and modes can overwrite explicit settings.** `ApplyPreset()` (`presets.cpp:108-155`) writes pipes, filter, ps2Modulate/dualPass globals + per-type cascades, radiosity, `doRadiosity`, `vcsTrails`, `grainFilter`, IR/NV, SSAO, SMAA, `ivMode`, ped/stencil shadows, and clears any mode latch. ModeManager (`modeManager.cpp:61-117`) takes a **whole-Config snapshot** and layers a profile; `MODE_GTAIV` zeroes ps2Modulate, dualPass, radiosity, grain, IR/NV, SSAO, SMAA, detailMaps, stochastic, `doglare`, `neoWaterDrops`, and sets `envMapSize=256`. The Presets tab calls both (`debugmenu_ui.cpp:2464-2500`), so a preset click silently discards unrelated explicit settings; there is no confirmation or diff preview.

## 2.2 Coverage summary (snapshot)

| Bucket | Count | Notes |
|---|---|---|
| `Config` fields | 436 | |
| Live/parsed | 233 | 100% have a row at 18:38 |
| Live + row | 233 | Some rows are duplicated across tabs (intentional mirrors) |
| Unparsed + no row | 201 | Appendix B |
| Unparsed but row (dead knobs) | 2 | `vignetteRadius`, `vignetteContrast` |
| Non-Config keys with rows | 3 live (`cloudRenderFix`, `waterFresnelSplit`, `forwardPlusFallback`) + 1 global (`NoiseQuality`) | `qualityPreset` greyed; hotkeys map to `keys[]` |
| Rows total | ~315 | `GetSettingRows().size()` is shown as "N rows" on the INI Keys tab |

**At snapshot the only meaningful "missing Config fields" are historical**: the five new keys and the earlier missing `ambientMult` / `altTimecycEnable` / `forwardPlusDebug` are now rows. Do not re-add them; verify they work instead (P1-1/P1-2).

## 2.3 Missing / dead controls and persistence gaps

| Item | File:line | State |
|---|---|---|
| `stateBaselineEnable` control | `debugmenu_ui.cpp:1014` | Row exists, **no consumer** - dead toggle |
| `waterStyle`, `waterQuality` persistence | `main.cpp:5162-5494` | Rows live (`:938`, `:1274`); **not written by Save INI** |
| `cloudRenderFix`, `waterFresnelSplit` persistence | `main.cpp:5162-5494` | Rows live (`:1022`, `:1322`); not written. Comment claims `MenuWriteMissingIniKeys()` exists (`:25`) - **function is not defined** |
| `forwardPlusFallback` | `debugmenu_ui.cpp:1599` | Read-only `RK_INIINT`; correct (restart-only), but not saveable by design |
| `NoiseQuality` persistence | `main.cpp:5496-5513` | Menu accessor changes the live tile; Save INI writes no `[General]` keys |
| Hotkey persistence | `main.cpp:5162-5494` | `keySwitch`/`keyReload` not written; menu edits lost on Save INI |
| "All Settings" tab | `debugmenu_ui.cpp:2060` vs `:3038` | Data built, tab not created |
| Global settings search | `debugmenu_ui.cpp:2351` | Only the **INI Keys** tab has a filter (`:2523`); Main/Vehicle/PostFX/Utility have none |

## 2.4 MIS-DISPLAY bugs (range / label / effective value)

| # | Setting | Menu (file:line) | Parser / consumer truth | Bug |
|---|---|---|---|---|
| M1 | `tonemapMinExposure` | `rFn [0.2..4.0]` @ `debugmenu_ui.cpp:1206` and `:1392`; per-frame clamp `[0.2..4.0]` @ `:2284` | `main.cpp:386-390` clamps **`[-0.5..0.5]`**; comment `:374-380` claims menu is `[-0.5..0.5]` | Menu cannot set deliberate negative min; an INI `-0.4` is silently rewritten to 0.2 on the first menu frame. Three inconsistent sources. |
| M2 | `envPower` | `rF [0..2000]` @ `:1527` (legacy now 1..128 @ `:276`) | `main.cpp:4240-4247` clamps `[1..128]`; Save does not clamp `envPower` | Menu can write 2000, it persists to disk, then reload clamps to 128. Shader sees a pinprick/NaN-prone exponent until reload. |
| M3 | `envMapSize` | `rI [4..2048]` @ `:1520` | `main.cpp:4284-4286` rounds **up** to a power of two; legacy entry snaps (`changeEnvMapSize`, `:195`) | ImGui writes 1000; consumer may get a non-POT size until reload, then it changes to 1024. |
| M4 | `reflectionMsaaLevel` | `rIn [0..8]` @ `:1306` and `:1523` | `main.cpp:4292-4294` snaps to `{0,2,4,8}`, anything else = 0 | Slider offers 1,3,5,6,7 which silently mean "off"; reload changes the displayed value. |
| M5 | `colorFilter` under PBR | `rE` @ `:939`; effective-value helper exists at `:1786` | `main.cpp:4384-4392` and `:662-674` force Modern every frame | Combo appears editable, silently reverts next frame. `RowHardOverride` shows why only if `DrawSettingRow` calls it (it does at snapshot), but the combo itself is still enabled. |
| M6 | `vignetteRadius` / `vignetteContrast` | `rFn` @ `:1442` / `:1444` | No `cfg.get`, no save, no consumer; `postfx_newfx.cpp:781` hardcodes `0.85/0.45` | Dead knobs look live. Note text says "Not wired" but the slider still accepts edits that do nothing. |
| M7 | `ssaoKernelSize` / `ssaoSampleCount` | `rFn`/`rIn` @ `:1117` / `:1119` (and `:1450`/`:1452`) | Self-described "not wired to the shader"; grep finds 0 consumers | Same class as M6: editable but no effect. Should be `RK_STATIC` or hidden. |
| M8 | `ivGamma` / `ivDesaturation` | ImGui `[0.5..2.0]` @ `:1223`; `[0..2]` @ `:1221` | Legacy menu `[0.1..3.0]` / `[0..1]` (`:334-335`); parser unclamped; presets set 0.15 / 1.0 | Two UIs disagree; values outside the ImGui range display but snap when dragged. |
| M9 | `Reset Values` button | `debugmenu_ui.cpp:2617-2620` | Calls `resetValues()` = push config into legacy mirrors | Label implies "reset settings"; it does not reset anything. Users will click it expecting defaults. |
| M10 | `forwardPlusDebug` | `rIn [0..2]` @ `:987` plus a 0/1 checkbox in `DrawMainPane` @ `:2190` | Consumer reads 0/1/2 (`forwardplus.cpp:179`) | Two controls, one of which cannot reach mode 2. Slider is correct; checkbox should be a combo or be removed. |
| M11 | `pbrAmbientFloor` tooltip | `:1044` "default 0.0" | `skygfx.h:679` says "default 0.12" | Doc/comment drift; the tooltip is right, the header is wrong. |
| M12 | `treeWind` tooltip | `:1474` "Default 0" | `skygfx.h:558` says "1 (default)" | Header wrong; parse 0 is right. |
| M13 | `tonemapMinExposure`/`Max` header docs | - | `skygfx.h:788-790` says max "2.0-at-read-then-clamped" | Header wrong; parse max default is 1.0. |
| M14 | `sunCoronaIntensity` etc. | ImGui `[0..2]` @ `:1056-1062` | Legacy menu `[0..10]` @ `:266-269`; parser unclamped; documented 0.0-2.0 | ImGui matches docs; legacy allows 10x blowout. Remove the legacy entries. |
| M15 | `envMapSize` legacy row label | `debugmenu_ui.cpp:240` | Range 4..2048 with snap callback | Fine; keep but ensure it matches M3's fix. |
| M16 | `pedShadows` / `stencilShadows` enum | `s_shadow` base -1, names Default/0/1 | `parseShadowToggle` clamps numeric >1 to 1 and maps `false/true`; legacy `shadStr` has no "2" note | Menu says "Default (key absent)" for -1, but a parsed `2` also becomes 1 with a one-time log. Add a note. |
| M17 | `waterStyle`/`waterQuality` not persisted | rows `:938`, `:1274` | `saveConfigTo` omits both | Menu "Save INI" appears to work but next load reverts. P1. |
| M18 | `cloudRenderFix`/`waterFresnelSplit` not persisted | rows `:1022`, `:1322` | globals, no Config field, no save writer | Same as M17. |
| M19 | `qualityPreset` row | grey `rN` @ `:928` | Load-time local | Correct, but the Presets tab should state that changing it needs a reload (it does not). |

## 2.5 Enum string audit (combo display vs parser token)

At snapshot the ImGui tables were reconciled with `readIniFile`'s `StrAssoc` maps (`debugmenu_ui.cpp:638-649`):

| Enum | ImGui strings (`debugmenu_ui.cpp`) | Parser accepted tokens (`main.cpp`) | Status |
|---|---|---|---|
| `pipeline` | `PBR, PS2, Xbox, Mobile, GTAIV` (`s_pipeNames:639`) | `PBR`, `Modern` (alias), `PS2`, `Xbox`, `Mobile`, `GTAIV`, empty->PBR (`3970-3977`) | OK; `Modern` is an accepted alias not shown. Save writes `PBR`. |
| `buildingPipe` | `PS2, Xbox, GTAIV, PBR` (`:640`) | same + empty->keep (`4176-4182`) | OK. Legacy table now matches (`:214`). |
| `vehiclePipe` | `PS2, PC, Xbox, Specular, Mobile, Neo, Leeds, VCS, Env, GTAIV, Modern` (`:641`) | same + empty->keep (`4187-4200`) | OK; legacy table now 11 entries (`:215`). |
| `colorFilter` | `None, PS2, PC, Mobile, III, VC, VCS, GTAIV, Modern` (`:642`) | same + empty->PC (`4361-4372`) | OK. |
| `waterStyle` | `Xbox, IV, V` (`:643`) | `Xbox/0`, `IV/GTAIV/1`, `V/GTAV/2`, empty->0 (`4754-4759`) | Display is shorter than accepted aliases; harmless, but tooltip should list them. |
| `waterQuality` | `Low, Medium, High, Ultra` (`:644`) | numeric 0..3 (`4761`) | OK. |
| `radiosity` | `PS2, Shader` (`:648`) | `PS2/0`, `Shader/1`, empty->1 (`4413-4417`) | OK. |
| `pedShadows`/`stencilShadows` | `Default (key absent), 0 / PS2, 1 / PC` base -1 (`:646`) | `boolMap` 0/false/1/true + numeric clamp -1..1 (`4317-4350`) | OK but see M16. |
| `coronaZtest` | `- (leave alone), PS2 (disable z-test), PC (force z-test)` base -1 (`:647`) | `-1, 0, 1` (`4452-4462`) | OK. |
| `tonemapOperator` | `Filmic, ACES 1.3, ACES 2.0, AgX, Frostbite, GT7` (`:652`) | 0..5 (`4785-4787`) | OK. |
| `treeFx` | `Off (0), PC (1), PC+ (2), Console (3)` (`:655`) | 0..3 (`4717-4719`) | OK. |
| `NoiseQuality` | `32/128/512/1024 px` (`:650`) | 0/1/2/>=3 (`3966`, `main.cpp:690-697`) | OK; the `>=3 -> 1024` case is only shown when q>3. |

## 2.6 Debug-critical settings exposure (the bug classes in the current round)

| Setting | Bug class | Exposed? (snapshot) | Where / caveat |
|---|---|---|---|
| `coronaZtest` | corona / z-test | YES | Main / Weather & timecycle `:1067`; applies via `resetValues()` -> patch at `main.cpp:334-348` |
| `pipeChainReflEnable` | pipe chain / SSR | YES (new) | Pipe chain tab `:1164`; consumer `postfx.cpp:3494` |
| `pipeChainIntensity` | pipe chain / SSR | YES | Pipe chain tab `:1162` |
| `texAlphaClassify` | alpha / blend / cutouts | YES (new) | Main / State integrity `:1010`; consumers building/vehicle pipe |
| `stateBaselineEnable` | blend / state bleed | YES but **dead** | `:1014`; no consumer - P1-1 |
| `defaultCbShaderHygiene` | stale shader / black geometry | YES (new) | `:1018`; consumer `main.cpp:1067` |
| `ambientFloor` | dark world / ambient collapse | YES (new) | `:1041`; consumer `pipelinecommon.cpp:177` |
| `pbrAmbientFloor` | peds/vehicles black at night | YES | `:1044`; consumer PBR paths |
| `forwardPlusEnable` | lighting / tiles | YES | `:982` |
| `forwardPlusFallback` | lighting fallback | READ-ONLY | Utility `:1599`; restart-only; correct |
| `forwardPlusDebug` | tiles debug view | YES | `:987` slider (0/1/2) + `:2190` checkbox (0/1) - M10 |
| `normalMapEnable` | normal maps | YES | `:990` |
| `normalMapIntensity` | normal maps | YES | `:1090` |
| `normalMapPlayerOnly` | normal maps | YES | `:1092` |
| `profilerEnable` | logging / perf | YES | Profiler `:1564`; default 1 in parser, deployed INI 0 |
| `profilerGpuTimers` | logging / perf | YES | `:1569` |
| `postfxDumpDebug` | dump / black-screen triage | YES | `:1110` |
| `waterStyle` | water | YES but not saved | `:938` / `:1274` - P1-2 |
| `waterQuality` | water | YES but not saved | `:1274` - P1-2 |
| `waterFresnelSplit` | water composite | YES but not saved | `:1322` - P1-2 |
| `ssaoEnable` / `ssaoTemporalEnable` | SSAO / black silhouettes | YES | `:947` / `:1121` and PostFX mirrors |
| `colorFilter` | colour / tonemap | YES, but hard-overridden under PBR | `:939` + `RowHardOverride` |
| `pipeline`, `buildingPipe`, `vehiclePipe` | pipeline mismatch | YES | `:924`, `:934`, `:936` |
| `pipelineOverride` | force pipeline | YES | `:926` |
| `ivMode` | IV grade | YES | `:960` (+ PostFX mirror) |
| `cloudRenderFix` | cloud/corona state bleed | YES but not saved | `:1022` - P1-2 |
| logging | logging | N/A (console/INI; no menu toggle) | `skygfx_dbg.log` always written; no verbosity UI |

## 2.7 Presets vs explicit settings (what the UI must disclose)

- `ApplyPreset` (`presets.cpp:108`) writes 17+ fields and the cascades (`ps2ModulateBuilding/Grass`, `dualPass*`). It also calls `ModeManager_ClearLatch()` when a mode is active. It never touches tonemap, water, normal maps, pipe-chain, ambient, or the new state-integrity keys - good.
- `ModeManager_SetMode(MODE_GTAIV)` (`modeManager.cpp:61`) overwrites 25+ fields (pipes/filter/preset/ps2Modulate/dualPass/radiosity/grain/IR/NV/SSAO/SMAA/ivMode/ivSaturation/ivCurves/detailMaps/stochastic/doglare/neoWaterDrops/envMapSize/envMapFarClipMult). Toggling off restores the pre-mode snapshot **verbatim**, so edits made inside a mode are lost when it is toggled off. The UI never warns.
- The "Style (gamePreset)" combo is gated on change (`debugmenu_ui.cpp:2455`), so it no longer stomps every frame; but each click is still a whole preset application with no confirmation.
- The `Modern` button writes pipes directly and calls `SyncIvModeFromPipeFamily` (`:2479-2496`); it does not clear a mode latch (unlike `ApplyPreset`), so a latched mode's snapshot can later restore over it. P2.

## 2.8 Legacy menu / native injection defects (fix or delete)

- `installMenu` `colFilterStr` now has 9 entries and range to `COLORFILTER_MODERN` (`:250`), fixed mid-audit.
- `buildPipeStr` now matches the enum (`:214`), fixed.
- `vehPipeStr` now 11 entries (`:215`), fixed.
- Still divergent: `ivGamma` legacy `0.1..3.0` vs ImGui `0.5..2.0`; `ivDesaturation` legacy `0..1` vs ImGui `0..2`; `sunCoronaIntensity` legacy `0..10` vs ImGui `0..2`; `ssaoPower` legacy `0.5..4.0` (matches); `envPower` legacy now 1..128 (matches parser, ImGui still 0..2000); `envMapSize` legacy snaps (correct), ImGui does not.
- `menu_inject.cpp` is inactive (`main.cpp:5804`) and would be unsafe if enabled: it stores `bool*` to `RwBool` (4-byte) fields (`s_options`, `:118-130`), calls `saveConfig()` on every toggle (`:372`), and `menu_inject_init` is a stub that only patches the screen table (`:408-429`). Either delete it or migrate the option list to `SettingRow` and fix the pointer types.

---

# PART 3 - External research: current best practices (2024-2026)

## 3.1 Widget choice per type

| Type | Recommended | Why / rule |
|---|---|---|
| `bool` / `RwBool` / `int 0-1` | `Checkbox` with a staged `bool` (current `DrawRowControl` `RK_BOOL`/`RK_BOOL8`) | ImGui has no bool-sized int; the staged write is correct. Always use a stable `##id` (current `##ini_<key>` is good; two rows with the same key in the same ID scope would collide). |
| Bounded float with a meaningful min/max | `SliderFloat` when the range is part of the concept (0..1, 0..4); `DragFloat` when the range is wide/open and precision matters (distance, radius) | Slider communicates the legal band and prevents most invalid input; Drag is better for large/open ranges. ImGui's own docs: sliders are for bounded values, drags for tweaking. |
| Unbounded or very wide float (e.g. `envPower` 1..128 in log space, distances) | `DragFloat` with `v_speed`, or `InputFloat` + explicit clamp | `SliderFloat` over 0..2000 wastes 99% of the track and hides the useful 1..128 band. For exponents use `SliderFloat(..., ImGuiSliderFlags_Logarithmic)`. |
| Exact numeric entry | `InputFloat`/`InputInt` with `ImGuiInputTextFlags_EnterReturnsTrue` + clamp on deactivate | Slider/drag Ctrl+Click already opens an input box, but a separate input is better for long lists. |
| Enum | `BeginCombo` with the **exact parser token** as the string (current `s_*` arrays) | Combo avoids out-of-range ints; use `base` for -1-based enums (current pattern). Add the INI token to the tooltip when the display label differs from the token. |
| Colours | `ColorEdit3` / `ColorEdit4` with `ImGuiColorEditFlags_Float` + `NoInputs` where the raw floats are the contract | This project has many RGB triples (`waterShallowR/G/B`, `heightFogR/G/B`, `vignetteR/G/B`) still hidden/dead. If exposed, use one `ColorEdit3` per triple, not three sliders. |
| Bitmask | Checkbox list (one per bit) + a read-only hex display, not a 0..255 slider | `vehPBRLayers` is currently a slider (`:1549`); a debugger cannot tell which bit is set. ImGui has no native bitmask widget. |
| Read-only/startup-only | `TextDisabled` value + "restart required" tooltip (current `RK_STATIC`/`RK_INIINT`); optionally `BeginDisabled` around the row | Do not present a live control for a value read once at boot. |

**Effective vs raw:** the UI should show both when a writer can override the config (this project: `colorFilter` under PBR). The current `RowHardOverride` (`:1786`) is the right pattern; extend it to `pipeline`/`buildingPipe`/`vehiclePipe` family locks and to `qualityPreset`-seeded fields. Godot's inspector shows a "revert" icon on any value different from default, and Blender's Properties search can auto-open the tab containing the first match - both are good models for "the value you see is the value in effect" (sources 10-12).

## 3.2 Clamps and constraints

- **Do not rely on the widget to enforce the contract.** ImGui's own `imgui.h` documentation states that Ctrl+Click manual input is **not clamped by default** and that `ImGuiSliderFlags_AlwaysClamp` is required to clamp manual input (source 2). The project's `SliderInt`/`SliderFloat` calls pass no flags, so Ctrl+Click can write out of range into `config`. The code compensates with per-frame `ClampMenuRanges()` (`:2235`) for only SSAO/env/tonemap/chrome - not for `envPower`, `envMapSize`, `reflectionMsaaLevel`, `ivGamma`, etc.
- **Single source of truth for ranges.** Put min/max/clamp in one table used by (a) the INI parser, (b) the ImGui row, (c) the legacy menu, (d) `saveConfigTo`. Today the same field has up to four values (`envPower`, `tonemapMinExposure`, `ivGamma`, `sunCoronaIntensity`).
- **Clamp on every writer.** ImGui only writes when the user interacts; the parser clamps on load; `saveConfigTo` clamps only 5 helpers (`main.cpp:5169-5173`). A value set by a preset, mode, or another module bypasses all of them. The code already has the right pattern (`clampEnvSliderRanges`, `clampTonemapExposureRanges`, `clampSsaoPower`, `clampChromeParams`); extend it to the mismatch list and call it from `refreshIni` + `saveConfigTo` + after each row edit.
- **Use `ImGuiSliderFlags_AlwaysClamp`** for every slider whose value reaches a shader constant; add `ImGuiSliderFlags_Logarithmic` for exponents/perceptual ranges (`envPower`, `godRaysExposure`, `heightFogDensity`).
- **Discrete sets need a combo, not a slider.** `reflectionMsaaLevel in {0,2,4,8}` and power-of-two `envMapSize` are classic combo cases; a slider writes illegal intermediate values that the parser silently normalises on the next load.
- **Min <= max invariants**: `tonemapMinExposure <= tonemapMaxExposure` is enforced by pinning (`main.cpp:396-403`) but the UI can still display a transiently inverted pair. Use `DragFloatRange2`/two sliders with `AlwaysClamp`, or clamp the sibling immediately in `onEdit`.

## 3.3 Reset to default and persisting

- **Per-field reset is expected in engine inspectors.** Unreal exposes `IPropertyHandle::ResetToDefault()` and renders a yellow reset arrow per property (`SResetToDefaultMenu`), plus `NoResetToDefault` for fields where a reset would be destructive (sources 9, 10). Godot's Project Settings has a circular-arrow reset next to every setting (source 11); Blender has "Load Factory Preferences" / "Revert to Saved" at the panel level and a per-property revert in the Inspector (source 12).
- **This project now has the data for it**: `kKeyDefaults[]` (`debugmenu_ui.cpp:1680-1765`) + `RowResetPopup` (`:1922`) + `ResetRowToDefault` (`:1895`). At snapshot it is wired into the row draw, but the **global "Reset Values" button still calls `resetValues()`** (`:2617`), which is a push-to-engine, not a reset. Rename it or replace it with a real "Reset current tab to defaults" action that iterates `kKeyDefaults` and asks for confirmation.
- **Defaults must be per-key, not per-field.** `waterStyle` default 0, `colorFilter` default 8, `envMapSize` 512, `ivGamma` 1.0, etc. `kKeyDefaults` already does this; keep it in sync with `readIniFile` literals. Add an assert/log that every parsed key has a default entry (a missing entry silently disables the reset menu item via `RowDefaultValue` returning false).
- **Reset must run the same write path as an edit.** `ResetRowToDefault` calls `FinishRowEdit(r, true)` (`:1916`), which runs `onEdit` and `resetValues()` - correct. Do not write the field directly from the reset menu.
- **Persist on explicit action, not per-frame.** This menu already uses explicit Save/Reload (`saveConfigTo`, `reloadAllInis`). Keep that. Do not write the INI every slider move; the legacy `menu_inject.cpp` calls `saveConfig()` on every toggle (`:372`) and is a good example of what to avoid.
- **ImGui's own window/table state is separate.** `io.IniFilename` defaults to `imgui.ini` relative to the process CWD (source 4); this project never sets it, so window size/pos and collapsing headers persist next to the game exe. Set an absolute path (next to `skygfx.ini`) or `NULL` + `LoadIniSettingsFromDisk`/`SaveIniSettingsToDisk` if the mod should not litter the game dir. Never use ImGui's ini as the settings store - it is UI state only.

## 3.4 Search, filtering, grouping for 200+ settings

- **Search is the primary navigation at this scale.** Blender added a Preferences search that filters categories/panels and highlights matching headers, with Ctrl+F to focus and Alt+F to clear (source 12; PR #119072). Godot Project Settings has "Filter Settings" and an "Advanced Settings" toggle to hide internal options (source 11). The current menu has a filter only on the INI Keys tab (`:2523`); Main/Vehicle/PostFX/Utility have none, and `GetCoverageRows()` has no tab. Move the filter to the window header and apply it to every tab via `DrawRowsForTab(tab, filter)`.
- **Group by subsystem, not by file.** The current groups are good (Pipelines, Lighting & Ambient, State integrity, Water, PostFX, Pipe chain, Normal maps, Forward+, Vehicle, Vegetation, Weather, Debug). Keep one **"All Settings"** view that deduplicates by key and groups by subsystem (`GetCoverageRows` + `kCoverageGroupOrder`), because the per-effect tabs intentionally mirror keys and a debugger needs one place to search. Add "modified only" and "non-default only" filters (Godot/Blender patterns) - with 315 rows this is what makes the menu usable.
- **Use tables for dense inventories.** `DrawReferenceTab` currently hand-aligns with `SameLine(240/410)`; `ImGui::BeginTable` with `ImGuiTableFlags_Resizable | RowBg | Borders` gives sortable/resizable columns and clips correctly. The Profiler tab already demonstrates the table pattern (`:2929`). For 300+ rows wrap the body in `ImGuiListClipper` (source 13) so rendering cost tracks visible rows.
- **Tabs vs tree:** top-level tabs for workflows (Main, Vehicle, Vegetation, PostFX, Utility, Profiler, Presets, INI Keys) + `CollapsingHeader` per subsystem + "All Settings" is the right 3-level shape. Avoid a single 300-row flat list; avoid a tree deeper than 3 levels. ImGui's `BeginTabBar(..., ImGuiTabBarFlags_FittingPolicyScroll)` is already used (`:2940`) and handles overflow.
- **Show counts and empty states.** "N rows" (`DrawReferenceTab`) and the dimmed effect tabs are good. Add per-group match counts during search and a "no settings match" state so a typo does not look like an empty mod.

## 3.5 Safe ImGui inside a D3D9 game hook

**What the official backend already does (and this project relies on):**

- `ImGui_ImplDX9_RenderDrawData` creates a `D3DSBT_ALL` state block, calls `Capture()`, backs up the three transforms manually (the DX9 state block does not reliably include them), draws, restores the transforms, then `state_block->Apply()` (`external/imgui/backends/imgui_impl_dx9.cpp:204-212`, `:305-312`). The local copy is 1.92.9 WIP and includes the 2021 `Capture()` workaround (source 3, issue #3857). Do **not** replace this with a hand-rolled partial state save; the ImGui maintainer's guidance for custom-engine DX9 integration is to compare the full `_D3DRENDERSTATETYPE` list against `SetupRenderState` and let the state block restore everything (source 6).
- **Device reset is the caller's job.** The example's `ResetDevice()` explicitly calls `ImGui_ImplDX9_InvalidateDeviceObjects()` before `IDirect3DDevice9::Reset()` and `ImGui_ImplDX9_CreateDeviceObjects()` after (source 5). Microsoft's lost-device documentation requires releasing all `D3DPOOL_DEFAULT` resources before `Reset`; ImGui's font texture and vertex/index buffers are `D3DPOOL_DEFAULT` (`imgui_impl_dx9.cpp:193-201`, texture creation `D3DPOOL_DEFAULT`). This project has **no** such call in `OnDeviceReset` (`postfx.cpp:2264`) or `hook_Reset` (`depthhook.cpp:329-344`). Add it (P1-6).
- **Hook at `Present`/`EndScene`, not mid-scene if you can avoid it.** This project draws inside `DrawFinalEffects` (`postfx.cpp:2030-2031`), which is after the world but still inside the frame's render sequence. The backend restores D3D state, so the immediate corruption risk is low, but the menu is subject to whatever render target/viewport is current and to the game's own state-cache assumptions. A `Present` hook (or an `EndScene` hook after the game's last draw) is the conventional, safest integration point; the Universal Dear ImGui Hook project hooks `Present`/`PresentEx`/`Reset`/`ResetEx` for exactly this reason (source 8).
- **RW 3.6 cache hazard:** RenderWare keeps its own render-state cache and only calls D3D when its cached value changes (`librw`'s `RwStateCache`/`flushCache` is the canonical example, source 15). A raw `device->SetRenderState` from ImGui followed by `state_block->Apply()` restores D3D exactly, so RW's cache stays consistent *if* the state block captured the pre-ImGui D3D state. The danger is a **capture while D3D already diverges from RW's cache** (caused by earlier raw D3D writes in this mod). Fixing the menu will not fix that; the project already has `pipeApplyWorldBaseline` for the world baseline. Rule: any code that bypasses `RwRenderStateSet` must either restore the exact D3D state or force `_rwD3D9RenderStateFlushCache()` before returning to RW.
- **Input:** the current `ImGuiWndProc` (`:376-386`) always forwards to ImGui and consumes handled messages only while `config->debugMenuOpen`. GTA SA polls input outside WndProc in places, so the player can still move while typing in the menu. Gate game input on `io.WantCaptureKeyboard || io.WantCaptureMouse` (the Hazel `ImGuiLayer` pattern, source 14) or hook the game's input poll. Also restore the original WndProc on unload; the current subclass (`:404`) is never removed.
- **`io.DisplaySize`:** never set explicitly; `ImGui_ImplWin32_NewFrame` derives it from the client rect. If the backbuffer and client rect differ (widescreen fixes, borderless, `pRasterFrontBuffer` 2048x2048 vs camera 1920x1080), the menu is scaled/clipped and the mouse is offset. Set `io.DisplaySize` from the current viewport/backbuffer each frame before `NewFrame` (and `io.DisplayFramebufferScale` if needed).
- **Crash safety:** an exception inside the menu draw should not take down the game. The Universal Dear ImGui Hook wraps frame/render callbacks in SEH and removes the overlay on fault (source 8). At minimum, guard `Begin/End` pairs and never call ImGui from a device-lost state.

## 3.6 Source list (used and recommended)

1. Dear ImGui FAQ - widget/ID/immediate-mode rules: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md
2. `imgui.h` slider/drag documentation (Ctrl+Click not clamped; `ImGuiSliderFlags_AlwaysClamp`): https://github.com/ocornut/imgui/blob/master/imgui.h
3. Official DX9 backend (D3DSBT_ALL capture/apply, transform backup, `InvalidateDeviceObjects`/`CreateDeviceObjects`): https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx9.cpp
4. ImGui settings/ini persistence (`io.IniFilename`, `LoadIniSettingsFromDisk`, `SaveIniSettingsToMemory`): https://github.com/ocornut/imgui/issues/4294 and https://github.com/ocornut/imgui/releases/tag/v1.61
5. Official Win32 + DX9 example, `ResetDevice()`: https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx9/main.cpp
6. Microsoft "Lost Devices (Direct3D 9)": https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices
7. ImGui DX9 state-block capture bug/issue: https://github.com/ocornut/imgui/issues/3857 ; custom-engine state guidance: https://github.com/ocornut/imgui/issues/4063 ; state restore pitfalls: https://github.com/ocornut/imgui/issues/6290
8. Universal Dear ImGui Hook (Present/Reset hooks, SEH isolation): https://github.com/Sh0ckFR/Universal-Dear-ImGui-Hook
9. Unreal `IPropertyHandle::ResetToDefault` + details customization: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/PropertyEditor/IPropertyHandle and https://dev.epicgames.com/documentation/unreal-engine/details-panel-customization?application_version=4.27
10. Unreal property metadata (`ClampMin`/`ClampMax`, `UIMin`/`UIMax`, `EditCondition`, `NoResetToDefault`): https://unreal-garden.com/docs/uproperty/
11. Godot Project Settings (filter field, reset arrow, advanced toggle, INI storage): https://docs.godotengine.org/en/stable/tutorials/editor/project_settings.html
12. Blender Preferences search (Ctrl+F/Alt+F, matching headers) and Properties search/revert: https://docs.blender.org/manual/en/latest/editors/preferences/introduction.html and https://projects.blender.org/blender/blender/pulls/119072
13. ImGui tables/list clipping (large lists): https://deepwiki.com/ocornut/imgui/2.5-tables-system and https://deepwiki.com/ocornut/imgui/9-advanced-topics
14. Hazel `ImGuiLayer` input capture (`io.WantCaptureMouse/Keyboard`): https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/ImGui/ImGuiLayer.cpp
15. RenderWare state cache (`RwStateCache`, `flushCache`, `restoreD3d9Device` after reset): https://github.com/nosro1/librw/blob/b34f2947/src/d3d/d3ddevice.cpp
16. NVIDIA RTX Remix debug view/search menu (renderer debug UI with a searchable debug-view list): https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-dev/docs/runtimeinterface/renderingtab/remix-runtimeinterface-rendering-debug.html

**Actionable rules distilled:**

1. One range table per setting; every UI, parser, save path and preset uses it.
2. Sliders are for bounded values; combos for discrete sets; inputs for exact entry; bitmask fields get bit checkboxes.
3. Always `AlwaysClamp`; clamp again in the parser and in `saveConfigTo`; document the effective range in the tooltip.
4. Every setting gets a per-key default and a per-row "Reset to default"; a global reset is a separate, confirmed action.
5. Persist only on explicit Save; never during a frame; keep ImGui's UI-state ini separate from the mod's INI.
6. At 200+ settings, search + "modified only" + subsystem grouping + one deduplicated "All Settings" view are mandatory.
7. In D3D9, rely on the official backend's `D3DSBT_ALL` capture/apply; add `Invalidate/CreateDeviceObjects` on reset; set `io.DisplaySize`; gate game input on `WantCapture*`.

---

# PART 4 - Prioritized, implementation-ready gap list

Line numbers are from the 18:38 snapshot. Acceptance checks are written so a UI designer can verify without reading shader code.

## P1 - critical for debugging (do first)

**P1-1. Make `stateBaselineEnable` real or remove the control.**
`debugmenu_ui.cpp:1014-1017`; `main.cpp:4516-4521`; call sites `main.cpp:2144`, `2636`, `2658`, `5639`.
*Change:* wrap each `pipeApplyWorldBaseline()` call in `if(config && config->stateBaselineEnable)` (or gate inside `pipeApplyWorldBaseline` itself using the global config). If the feature is intentionally always-on, change the row to `RK_STATIC` and the tooltip to "always on; no per-frame toggle".
*Acceptance:* toggling the checkbox produces a visible `[Pipeline] pipeApplyWorldBaseline: ...` log difference (or the row is greyed and cannot be toggled); grep shows at least one `stateBaselineEnable` read outside parse/save.

**P1-2. Persist every menu-editable key.**
`main.cpp:5162-5494`; missing: `waterStyle`, `waterQuality`, `waterFresnelSplit`, `cloudRenderFix`, `forwardPlusFallback`, `keySwitch`, `keyReload`, `NoiseQuality`.
*Change:* add `cfg.set` lines for the first four (Config/global-backed) in `saveConfigTo`; write `qualityPreset` only if it is meant to round-trip (it is a load-time seed, so leave it and say so); add a `[General] NoiseQuality` write; persist hotkeys. If the design is "menu writes non-Config globals", implement the referenced `MenuWriteMissingIniKeys()` for `cloudRenderFix`/`waterFresnelSplit` and call it from the Save button.
*Acceptance:* edit each value in the menu -> Save INI -> close/reopen the game -> Reload INI -> value is identical; the key appears exactly once in `skygfx.ini`.

**P1-3. Reconcile `tonemapMinExposure` (three sources).**
`debugmenu_ui.cpp:1206`, `:1392`, `:2284`; `main.cpp:386-390`; comment `main.cpp:374-380`.
*Change:* decide the contract. Recommended: parser clamps `[-0.5..0.5]` (as today), menu slider `[-0.5..0.5]` with a tooltip "negative darkens deliberately", per-frame clamp to the same range, and update the stale comment. If negative is no longer wanted, change the parser and `skygfx.h` docs to `[0.2..4.0]` and remove the old comment.
*Acceptance:* a single grep shows one min/max pair; setting `-0.4` in the INI survives a menu frame and displays as `-0.400`; Min <= Max invariant holds.

**P1-4. Fix `envPower` range and persistence.**
`debugmenu_ui.cpp:1527` (`[0..2000]`), `main.cpp:4240-4247` (`[1..128]`), `main.cpp:5162` (no clamp).
*Change:* slider `[1..128]` with `ImGuiSliderFlags_Logarithmic` (or `DragFloat` speed 0.1), add `envPower` to a clamp helper called from `readIniFile`/`refreshIni`/`saveConfigTo`/menu.
*Acceptance:* the slider cannot produce >128; an INI `envPower=380` logs one clamp and displays 128; Save writes <=128.

**P1-5. Make `envMapSize` and `reflectionMsaaLevel` discrete.**
`debugmenu_ui.cpp:1520`, `:1306`, `:1523`; `main.cpp:4283-4294`.
*Change:* `envMapSize` -> combo of `{128,256,512,1024,2048}` or a slider with an onEdit snap (`changeEnvMapSize` already exists at `:195`); `reflectionMsaaLevel` -> combo `{Off(0), 2x, 4x, 8x}`.
*Acceptance:* the UI can never store a value outside the accepted set; reload does not change the displayed value.

**P1-6. Handle D3D9 device reset for ImGui.**
`debugmenu_ui.cpp:389-408` (init only); `postfx.cpp:2264` (`OnDeviceReset`); `depthhook.cpp:329-344` (`hook_Reset`).
*Change:* call `ImGui_ImplDX9_InvalidateDeviceObjects()` **before** `Reset` and `ImGui_ImplDX9_CreateDeviceObjects()` **after**; guard with `unifiedImGuiInited`. Optionally shutdown/recreate the Win32 backend if the window handle changes.
*Acceptance:* Alt-Tab / change resolution 10x with the menu open: no missing glyphs, no crash, menu still draws; `skygfx_dbg.log` records the reset without ImGui errors.

**P1-7. Wire or remove the "All Settings" coverage view.**
`debugmenu_ui.cpp:2060-2092` (builder) vs `:3038-3130` (tabs).
*Change:* add `if(ImGui::BeginTabItem("All Settings"))` and draw `GetCoverageRows()` with the existing group order; put the search box at the top of the main window (not only INI Keys).
*Acceptance:* the tab lists every unique key once (count equals `GetSettingRows()` dedup count); typing a key filters all groups; no duplicate key appears twice in the tab.

**P1-8. Stop claiming PBR-locked values are editable.**
`debugmenu_ui.cpp:939` (combo), `:1786-1803` (helper).
*Change:* when `pipeline==PBR` and key `colorFilter`, either `BeginDisabled()` the combo (with the `RowHardOverride` reason as tooltip) or keep it enabled but show the effective value next to it; do the same for `buildingPipe`/`vehiclePipe` when `pipelineOverride` is active.
*Acceptance:* selecting `None` under PBR visibly reverts within one frame **and** the row shows "effective: Modern (8) - PBR forces it"; no silent flicker.

**P1-9. Persist/repair `cloudRenderFix` and `waterFresnelSplit`.**
`debugmenu_ui.cpp:1022`, `:1322`; `main.cpp:4355`, `:4768`; `saveConfigTo` absence.
*Change:* implement `MenuWriteMissingIniKeys()` (or add to `saveConfigTo`) and call it from Save/Save-as; the menu comment at `:25` already promises it.
*Acceptance:* with the menu rows toggled 0/1, Save INI updates the key; reload shows the same value; grep in `skygfx.ini` shows one line per key.

**P1-10. Add a per-row "effective vs raw" display for all hard overrides, not just `colorFilter`.**
`RowHardOverride` (`:1786`), `RowInactiveReason` (`:1810`).
*Change:* extend `RowHardOverride` with `pipeline`->pipes/filter, `pipelineOverride`, `qualityPreset` seeds, and `waterUseTimecycle`->`waterShallow*/Deep*` (already in `RowInactiveReason`). Show a compact "raw -> effective" string on the row when they differ.
*Acceptance:* every hard-override path has a row that shows both values; changing the raw value under an override gives immediate visual feedback instead of a silent revert.

## P2 - mis-displays, range mismatches, dead controls

**P2-1. `ivGamma` / `ivDesaturation` ranges disagree between the two menus.**
`debugmenu_ui.cpp:1221-1223` vs legacy `:334-335`; parser unclamped; presets `main.cpp:4149-4157`.
*Change:* pick `[0..2]` for desaturation and `[0.5..2.0]` for gamma (current ImGui) and update the legacy `DebugMenuAddVar` bounds; or delete the legacy entries. Add parser clamps if the shader expects them.
*Acceptance:* both UIs show the same range; a preset value (0.15 / 1.0) is inside it; manual entry cannot exceed it.

**P2-2. `vignetteRadius` / `vignetteContrast` are dead editable rows.**
`debugmenu_ui.cpp:1442`, `:1444`; consumer `postfx_newfx.cpp:781` hardcodes 0.85/0.45; no parse/save.
*Change:* convert both to `RK_STATIC` with "not wired - FxVignette hard-codes 0.85/0.45", or remove them and rely on `ivVignetteRadius/Contrast` (which are wired).
*Acceptance:* the control either changes pixels or is visibly read-only; Save INI does not claim to persist it.

**P2-3. `ssaoKernelSize` / `ssaoSampleCount` are no-ops.**
`debugmenu_ui.cpp:1117`, `:1119`, `:1450`, `:1452`; consumer grep = 0.
*Change:* make them `RK_STATIC` with the existing tooltips ("fixed at 16 samples in the shader").
*Acceptance:* no editable control remains for a key with zero consumers.

**P2-4. `Reset Values` is mislabeled.**
`debugmenu_ui.cpp:2617-2620`; `resetValues()` at `main.cpp:296`.
*Change:* rename to "Apply to engine" (it pushes config into `CPostEffects`) and add a separate "Reset tab to defaults" button using `kKeyDefaults`/`ResetRowToDefault`, with a confirmation modal.
*Acceptance:* clicking "Reset tab to defaults" restores every visible control in the tab to its parser default and leaves other tabs untouched.

**P2-5. `forwardPlusDebug` has two controls, one cannot reach mode 2.**
`debugmenu_ui.cpp:987` (slider 0..2), `:2190` (checkbox 0/1); consumer `forwardplus.cpp:179`.
*Change:* keep one combo `{Off, Tiles, Tiles+Legend}` in the Forward+ group; remove the checkbox or make it a shortcut that opens the combo. The slider is acceptable if labelled 0/1/2.
*Acceptance:* all three modes are selectable from one control; no second control writes a different value in the same frame.

**P2-6. `sunCoronaIntensity` etc. have legacy ranges 5x the documented range.**
Legacy `debugmenu_ui.cpp:266-269` `[0..10]`; ImGui `[0..2]` `:1056-1062`; `skygfx.h:350-353` says 0.0-2.0.
*Change:* set the legacy entries to `[0..2]` or delete them.
*Acceptance:* neither UI can set 10x and blow out the PBR tonemap.

**P2-7. `pbrAmbientFloor` / `treeWind` / tonemap header comments are stale.**
`skygfx.h:679`, `:558`, `:788-790`.
*Change:* update the comments to the parse defaults (0.0, 0, 1.0) and the actual clamps; or change the parse defaults if the comments are the intended contract.
*Acceptance:* header comment, parser default, deployed INI and menu tooltip agree for each.

**P2-8. `ps2ModulateGlobal` / `dualPassGlobal` write to `config`, not `c`.**
`main.cpp:4172-4173`.
*Change:* use `c->` (the function already takes `Config *c`).
*Acceptance:* loading a non-active config does not change the active config's globals; the config selector shows independent values.

**P2-9. `qualityPreset` is silently stomped by sentinel defaults.**
`main.cpp:4304-4308` (`sunGlare`), `:4429-4433` (`neoWaterDrops`).
*Change:* use `c->doglare`/`c->neoWaterDrops` as the fallback (the preset has already set them), or apply the sentinel only when the key is absent **and** no preset was applied. Add a one-time log when the default disables a preset feature.
*Acceptance:* ULTRA preset + missing `sunGlare`/`neoWaterDrops` keeps those features on (or logs exactly why not); menu shows the effective value.

**P2-10. Legacy DebugMenu and ImGui both edit the same fields with different ranges.**
`installMenu` `:208-355`; ImGui `:890-1608`.
*Change:* either delete `installMenu` (the ImGui menu is the supported one) or generate both from the same `SettingRow`/range table. At minimum, fix `ivGamma`, `ivDesaturation`, `sunCoronaIntensity`, `envPower`, `ssaoPower` to match.
*Acceptance:* `grep DebugMenuAddVar` ranges equal the corresponding `rF/rI` bounds; no field can be set to a value the other UI forbids.

**P2-11. `menu_inject.cpp` is stale and unsafe if revived.**
`menu_inject.cpp:118-130` (bool* to RwBool), `:372` (save on every toggle), `main.cpp:5804` (disabled).
*Change:* delete the file from the build or mark it `#if 0` with a note; if revived, use `int*`/`RwBool*` and call `saveConfig` only from an explicit action.
*Acceptance:* the build has one settings UI path; no dead file claims to be a menu.

**P2-12. `waterUseTimecycle` makes the water colour overrides inactive.**
`debugmenu_ui.cpp:1275` (toggle), `:1288-1293` (Deep/Shallow), `RowInactiveReason` `:1815-1819`.
*Change:* the row already returns a reason; make the drawer visually dim the Deep/Shallow rows when `waterUseTimecycle==1` (not just a tooltip).
*Acceptance:* with timecycle colour on, the six colour rows are visibly disabled/dimmed; toggling it off re-enables them.

**P2-13. `pedShadows`/`stencilShadows` `2` handling is not shown.**
`main.cpp:4326-4350` (numeric 2 -> 1 with a one-time log); menu `:1031-1035`.
*Change:* add to the tooltip: "2 is clamped to 1; -1 = leave the game default".
*Acceptance:* a user reading the row understands all three states.

**P2-14. `forwardPlusFallback` reads the active INI path correctly but not the loaded config path for `skygfxN.ini`.**
`debugmenu_ui.cpp:855-888` (`CurrentIniPath` uses `skygfxN.ini` for config > 0, falls back to `skygfx.ini`); `forwardplus.cpp:256`.
*Change:* align the fallback rule with `readIni`/`findInis`; log which path the value came from.
*Acceptance:* row value equals what `FpFallbackEnabled` will read on the next launch for configs 0..N.

**P2-15. `RowResetPopup`/`RowHardOverride`/`RowInactiveReason` are new and need coverage tests.**
`debugmenu_ui.cpp:1786-1950`, draw sites `:2337-2406`.
*Change:* add a debug-only assert/log when a row's key has no `kKeyDefaults` entry; walk the 233 live keys and ensure each has a default (the table looks complete but is hand-maintained).
*Acceptance:* no live row shows "No parser default" in the context menu; every row's reset produces the same value as a fresh INI load.

## P3 - structure, search, reset, polish

**P3-1. Global search + modified/non-default filters.**
Only INI Keys has `s_filter` (`:2519-2534`). Move the filter to a shared header above the tab bar; pass it to every `DrawRowsForTab`. Add checkboxes `Modified only` and `Non-default only` (computed from `kKeyDefaults`). Acceptance: typing `corona` in any tab shows the corona rows; "modified only" hides everything at default.

**P3-2. Replace hand-aligned rows with `ImGui::BeginTable`.**
`DrawSettingRow` uses `SameLine(240/410)` (`:2294-2319`). Use a 3-column resizable table (label, control, INI key) with `ImGuiTableFlags_RowBg | Borders | Resizable`; wrap long bodies in `ImGuiListClipper`. Acceptance: resizing the window keeps columns aligned; 315 rows scroll without frame-time growth.

**P3-3. Rename and split `Reset Values`.**
See P2-4. Add "Reset this tab" and "Reset entire config" with a confirm modal; show the number of changed keys. Acceptance: no accidental full reset; the button label matches the action.

**P3-4. Add "restart required" badges to startup-only rows.**
`disableClouds`, `disableGamma`, `privateHooks`, `forceWindShader`, `fixShadows`, `fixPcCarLight`, `explicitBuildingPipe`, `treeWind` (hook install), `forwardPlusFallback`, `qualityPreset`. Current `RK_STATIC` grey rows exist but read as "dead". Add a small `[restart]` prefix or an icon column. Acceptance: every startup-only row says why it is grey.

**P3-5. Add a "copy INI snippet" / "copy effective settings" action.**
Debuggers need to attach current values to bug reports. Add `Copy` buttons that emit `key = value` lines (from `SettingRow`/`kKeyDefaults`) to the clipboard, with a "only non-default" option. Acceptance: paste into a text editor produces a valid, complete `[SkyGfx]` fragment.

**P3-6. Add live diagnostic readouts next to the relevant groups.**
Useful and already available: Forward+ light counts (`:2178-2185`), profiler frame/draw counters, `g_CurrentNoiseSize` (`:2528`), normal-buffer/pipe-chain texture availability. Add one line per group: `normalBufferTex=... pipeChain pack=... coronas=...`. Acceptance: a black-screen report can tell whether the input textures exist before changing a setting.

**P3-7. Make the effect-tab dimming consistent.**
`EffectSwitchOn` (`:2590-2607`) dims tabs for 11 effects; `PostFX`/`Tonemap`/`Water` are never dimmed. Add `EF_TONEMAP` (gated on `tonemapAutoExposure`), `EF_FORWARD` (`forwardPlusEnable`), `EF_WATER` (`waterParallaxEnable`), and use the same reason string in the body. Acceptance: every tab whose master switch is off is dimmed with a one-line reason.

**P3-8. Persist window/table state explicitly (and not in the game dir).**
`EnsureImGuiInit` never sets `io.IniFilename` (`:389-408`). Set an absolute path (e.g. `<game>/skygfx_imgui.ini`) or `NULL` + manual load/save. Acceptance: no `imgui.ini` appears in the CWD; window size is restored between sessions.

**P3-9. Handle `io.DisplaySize` / DPI.**
Set `io.DisplaySize` from the current backbuffer/viewport before `NewFrame`; set `io.DisplayFramebufferScale` if the UI should scale. Acceptance: menu fits and mouse hits correctly at 1280x720, 1920x1080 and 2048x2048 front-buffer modes.

**P3-10. Restore WndProc and gate game input on `WantCapture*`.**
`ImGuiWndProc` `:376-386`; subclass `:404`. Save/restore the original WndProc on shutdown; consume keyboard/mouse only when `io.WantCaptureKeyboard/Mouse` is true. Acceptance: typing in the menu does not move the player; after unload, the game WndProc is the original one.

**P3-11. Document the matrix in the UI itself.**
Add a small "Coverage" footer: `436 Config fields / 233 live / 235 rows / 201 hidden (all unparsed)`. This prevents the next audit from re-discovering the same gaps. Acceptance: the footer numbers match `GetCoverageRows`/`BuildSettingRows` at runtime.

## Settings that should stay hidden (and why)

| Field / group | Reason |
|---|---|
| `version` | Module ABI; set programmatically (`DllMain`, `main.cpp:5892`) |
| `tagsBuildingPipe` | Internal mirror of `buildingPipe`; no INI key, no consumer |
| `debugMenuOpen` | Runtime UI flag; toggled by the hotkey (`main.cpp:3611`) |
| `_reserved_smaa*` | Padding; kept only for struct layout |
| Legacy Expanded Weather/Timecycle/cloud/light/postFX/grading/lens (160 fields) | No `cfg.get`, no reader outside `skygfx.h`; a full weather/cloud rewrite would add them back with keys |
| `currentWeatherType`, `weatherTransition`, `weatherCycleEnabled`, `timecycleOverrideHour`, `timecycleOverrideEnabled` | No reader; `weather.cpp` uses `m_CurrentColours` directly |
| `unified*` (32) | Fork never integrated; only referenced by the early-returning `UploadUnifiedConstants` |
| `vignetteRadius`, `vignetteContrast` | No parse, no save, no consumer (`postfx_newfx.cpp` hardcodes 0.85/0.45); use `ivVignette*` instead |
| `ssaoKernelSize`, `ssaoSampleCount` | Only kept for INI compat; shader fixes 16 samples; expose as read-only if at all |
| `qualityPreset` | Load-time seed, not a runtime setting; greyed with a tooltip is the right treatment |
| `forwardPlusFallback` | Read once at boot by `forwardplus.cpp`; show read-only, never a live control |
| `disableClouds`, `disableGamma`, `privateHooks`, `forceWindShader`, `fixShadows`, `fixPcCarLight`, `explicitBuildingPipe` | Startup-only hooks/patches; greyed is correct |
| `ps2grassFiles` | Parser disabled (`main.cpp:4313` comment); keep greyed so it is not invisible |
| `grassIntensityVar` (non-Config row) | Staged feature with no live variable; keep the greyed row as a gap marker |

## Bottom line for the UI designer

- The inventory is **complete** at snapshot: every parsed Config field has a row, and the five new keys are reachable. The remaining work is correctness, persistence, and structure - not coverage.
- Fix the **six lies** first: `stateBaselineEnable` (no consumer), Save INI omissions, `tonemapMinExposure`, `envPower`, `envMapSize`/`reflectionMsaaLevel`, and the reusable "stored but inactive" marker.
- Then make the menu **trustworthy under pressure**: device reset, `io.DisplaySize`, input capture, and a single range table shared by parser/menu/legacy/save.
- Then make it **navigable at 315 rows**: global search, "All Settings", tables + clipper, per-row reset, and a restart badge.
