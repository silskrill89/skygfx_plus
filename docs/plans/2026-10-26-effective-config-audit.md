# Effective Config Audit — 2026-10-26

Scope: the live INI at `E:\games\gtasa_skygfx_plus\skygfx.ini` versus the current
working tree (HEAD `898e894`, source files have uncommitted changes). Read-only:
no source was modified. The audit follows the actual load order — every `[SkyGfx]`
key is parsed first, then `[Modes] active` is applied at the very end
(`src/core/main.cpp:6768-6770`), then per-frame re-derivation/self-healing runs.

One-line verdict: the live INI says `Xbox`, and Xbox owns 79 fields. The user's
`dualPass=0`, `dualPassVehicle=0`, `forwardPlusEnable=1` and `colorFilterEnable=0`
do **not** survive as written: the first three are overwritten by the profile; the
fourth is written to 1 by the profile and then healed back to 0 every frame by the
user-intent latch. The runtime therefore has dual-pass ON, Forward+ OFF, and the
colour filter bypassed, not the combination written in the file.

---

## 1. Which mode is active, and what does it own?

`[Modes] active = Xbox` is present in the live INI (`skygfx.ini:5`). It is parsed
at `src/core/main.cpp:5535-5556`; `modeOwnsPipes()` is true for Xbox
(`main.cpp:111-120`), so the INI pipe keys are treated as profile-owned
(`main.cpp:5557`, `6004-6005`). The profile is applied at the end of
`readIniFile()` (`main.cpp:6768-6770`) through `ApplyMode()`
(`src/core/modeManager.cpp:1077-1160`, `applyProfile()` at `:812-827`). The live
log confirms `[Mode] engage Xbox owned=79 pipes=2/1/2 cf=2`.

The Xbox table is `kValuesXBOX` (`src/core/modeManager.cpp:196-298`), registered
at `:738`. Fields relevant to transparency/ordering:

| Field | Xbox value | Line |
|---|---|---|
| `pipeline` / `buildingPipe` / `vehiclePipe` | `PIPELINE_XBOX` / `BUILDING_XBOX` / `CAR_XBOX` (2/1/2) | `modeManager.cpp:202-204` |
| `colorFilter` / `ivMode` | `COLORFILTER_PC` (2) / `0` | `:205`, `:208` |
| `gamePreset` (alias of `preset`) | `PRESET_SA_XBOX` | `:207` |
| `dualPassBuilding` / `dualPassVehicle` | `1` / `1` | `:214`, `:218` |
| `waterStyle` | `0` (Xbox) | `:225` |
| `radiosity` / `doRadiosity` / `grainFilter` | `1` / `1` / `0` | `:231-233` |
| `colorFilterEnable` | `1` | `:251` |
| `coronaZtest` | `1` | `:261` |
| `dualPass` (field `dualPassGlobal`) | `1` | `:270` |
| `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `1` / `1` / `1` | `:271-273` |
| `grassBackfaceCull` (field `backfaceCull`) | `1` | `:275` |
| `pedShadows` / `stencilShadows` | `-1` / `-1` | `:277-278` |
| `stateBaselineEnable` / `pedOpaqueContract` / `texAlphaClassify` / `defaultCbShaderHygiene` | `1` / `1` / `1` / `1` | `:284-287` |
| `forwardPlusEnable` / `pipeChainEnable` / `normalBufferEnable` | `0` / `0` / `0` | `:293-295` |
| `depthHookEnable` / `profilerEnable` | `1` / `0` | `:296-297` |

`fixShadows` is **not** a `Config` field and is **not** owned by the Xbox profile.
It is a global read at `main.cpp:5986` and only gates the shadow-offset patch at
`main.cpp:7605-7620`. `radiosityEnable` is also not owned; it is read at
`main.cpp:6191` and gates `Radiosity` at `src/render/postfx.cpp:1243-1244`.
`pedShadows`/`stencilShadows` *are* mode-owned in the current source (contrary to
the stale statement in `docs/feature-matrix.md:70`).

---

## 2. Effective config table

Load order is: preset seeds (`qualityPreset`, `main.cpp:5623-5790`) -> explicit
`[SkyGfx]` reads -> `[Modes] active` apply (`main.cpp:6768-6770`). For owned
fields the profile wins. The exception is `colorFilterEnable`: the profile writes
1, but the per-frame `cfEnableHeal()` restores the recorded user intent (0).

| Key | INI | Effective | Who wins (source) |
|---|---|---|---|
| `pipeline` | Xbox | `PIPELINE_XBOX` (2) | Xbox profile, `modeManager.cpp:202`; INI also 2 (`main.cpp:5575`) |
| `buildingPipe` | Xbox | `BUILDING_XBOX` (1) | profile `:203`; INI override ignored while owned (`main.cpp:5810-5814`) |
| `vehiclePipe` | Xbox | `CAR_XBOX` (2) | profile `:204`; INI override ignored (`main.cpp:5825-5829`) |
| `colorFilter` | PC | `COLORFILTER_PC` (2) | profile `:205`; INI read skipped while owned (`main.cpp:6004-6005`) |
| `colorFilterEnable` | 0 | **0** | profile writes 1 (`modeManager.cpp:251`), then `cfEnableHeal()` restores user intent 0 every frame (`main.cpp:596-610`, called from `derivePipelineFromPipes`, `:648-654`; `postfx.cpp:1524`) |
| `ivMode` | 0 | 0 | profile `:208` |
| `gamePreset` | -1 | `PRESET_SA_XBOX` | profile `:207`; display/style field only (`debugmenu_ui.cpp:2863-2864`) |
| `qualityPreset` | 3 | 3 | INI `main.cpp:5623-5624`; but its `smaa/ssao/motionBlur/sssPostProcess` seeds (`:5725-5736`) are overridden to 0 by the profile (`modeManager.cpp:236-239`) |
| `ssaoEnable` | 0 | 0 | INI `main.cpp:6102`; profile `:236` also 0 |
| `ssaoTemporalEnable` | 0 | 0 | INI `main.cpp:6276`; not owned (SSAO off anyway) |
| `smaaEnable` | 0 | 0 | INI `main.cpp:6111`; profile `:237` also 0 |
| `sssEnable` | 0 | 0 | INI `main.cpp:6317`; not owned |
| `sssPostProcessEnable` | 0 | 0 | INI `main.cpp:6308`; profile `:239` also 0 |
| `pipeChainEnable` | 0 | 0 | profile `:294` |
| `pipeChainReflEnable` | 0 | 0 | INI `main.cpp:6134`; not owned |
| `texAlphaClassify` | 1 | 1 | profile `:286` |
| `stateBaselineEnable` | 1 | 1 | profile `:284` |
| `defaultCbShaderHygiene` | 1 | 1 | profile `:287` |
| `pedOpaqueContract` | 1 | 1 | profile `:285` |
| `pedCullMode` | -1 | -1 | INI `main.cpp:6159-6161`; not owned (−1 = preserve entry cull) |
| `pedAlphaTestEnable` | 1 | 1 | INI `main.cpp:6163-6165`; not owned |
| `pedAmbientFloor` | 0.20 | 0.20 | INI `main.cpp:6224-6226`; not owned |
| `ambientFloor` | 0.03 | 0.03 | INI `main.cpp:6169-6171`; not owned |
| `pbrAmbientFloor` | 0.05 | 0.05 | INI `main.cpp:6219`; not owned |
| `bldEnvReflect` | 1 | 1 | INI `main.cpp:5851-5853`; not owned |
| `bldSkyReflect` | 0.15 | 0.15 | INI `main.cpp:5854-5856`; not owned |
| `pbrIblAmbientWeight` | 0.8 | 0.8 | INI `main.cpp:6232`; not owned by Xbox (Modern owns 0.5, `modeManager.cpp:593`) |
| `coronaZtest` | 1 | 1 | profile `:261` |
| `cloudRenderFix` | 1 | 1 | INI `main.cpp:5984`; not owned |
| `depthHookEnable` | 1 | 1 | profile `:296` |
| `waterStyle` | Xbox | `0` (Xbox) | profile `:225`; INI also 0 (`main.cpp:6425`) |
| `waterQuality` | 0 | 0 | INI `main.cpp:6426-6430`; not owned by Xbox |
| `waterParallaxEnable` | 0 | 0 | INI `main.cpp:6350`; not owned |
| `forwardPlusEnable` | 1 | **0** | Xbox profile `:293` overrides INI `main.cpp:6327` |
| `forwardPlusFallback` | 1 | 1 | direct INI read `forwardplus.cpp:257`; inert while FP off |
| `profilerEnable` | 0 | 0 | profile `:297` |
| `normalMapEnable` | 0 | 0 | profile `:243` |
| `dualPassPed` | 1 | 1 | profile `:271` (same as INI) |
| `dualPassVehicle` | 0 | **1** | profile `:218` overrides INI `main.cpp:5858` |
| `dualPass` | 0 | **1** (field `dualPassGlobal`) | profile `:270` overrides INI `main.cpp:5784` |
| `fixShadows` | 0 | 0 | global INI `main.cpp:5986`; not owned |
| `pedShadows` | -1 | -1 | profile `:277`; same as INI `main.cpp:5977` |
| `stencilShadows` | -1 | -1 | profile `:278`; same as INI `main.cpp:5978` |
| `tonemapAutoExposure` | 0 | 0 | INI `main.cpp:5897`; not owned |
| `tonemapMaxExposure` | 2.0 | 2.0 | INI `main.cpp:5902`, clamped `[0.2..4.0]` |
| `tonemapMinExposure` | 0.5 | 0.5 | INI `main.cpp:5900`, clamped `[-0.5..0.5]` |
| `motionBlurEnable` | 0 | 0 | profile `:238` |
| `heightFogEnable` | 0 | 0 | INI `main.cpp:6254`; not owned |
| `godRaysEnable` | 0 | 0 | INI `main.cpp:6264`; not owned |
| `sunShaftsEnable` | 0 | 0 | profile `:246` |
| `bloomEnable` | 0 | 0 | profile `:244` |
| `dofEnable` | 0 | 0 | profile `:245` |
| `grainEnable` | 1 | 1 | INI `main.cpp:6192`; not owned, **but** see next row |
| `grainIntensity` (not in prompt) | 0.284 | **0.0** | profile `:248`; `DrawFilmGrain` self-gates on `grainIntensity <= 0` (`postfx_newfx.cpp:748`), so grain is effectively off |
| `caIntensity` | 0 | 0 | profile `:249` |
| `radiosityEnable` | 0 | 0 | INI `main.cpp:6191`; not owned; gate at `postfx.cpp:1243` |
| `radiosity` | Shader | `1` (Shader) | profile `:231`; same value |
| `grassBackfaceCull` (field `backfaceCull`) | 1 | 1 | profile `:275`; INI `main.cpp:5943` |
| `zwriteThreshold` | 128 | 128 | profile `:279` |
| `vehPrelightFallback` | 0.15 | 0.15 | INI `main.cpp:6237-6239`; not owned |
| `envPower` | 128 | 128 | INI `main.cpp:5864-5874`; not owned |
| `ivExposure` | 2.5 | 2.5 | INI `main.cpp:6209`; not owned by Xbox (qualityPreset 3 also seeds 2.5, `:5768`) |

Notes:
- `qualityPreset=3` is not a complete preset. It seeds SMAA/SSAO/MB/SSS on, but
  explicit INI keys and the Xbox profile turn them back off. The profile wins for
  `smaaEnable`, `ssaoEnable`, `motionBlurEnable`, `sssPostProcessEnable`
  (`modeManager.cpp:236-239`), so the ULTRA preset does not survive.
- `grainEnable=1` is technically honoured, but the profile's `grainIntensity=0.0`
  neutralises it. This is an effective override that is easy to miss because the
  master switch reads 1.
- `radiosity=Shader` and `doRadiosity=1` are profile values, but `radiosityEnable=0`
  is not owned and gates the whole pass off (`postfx.cpp:1243-1244`). The profile's
  radiosity look is therefore inert.

---

## 3. Colour filter: `colorFilter=PC` with `colorFilterEnable=0`

Trace, in execution order:

1. INI parse records **user intent 0**: `cfEnableSetUserIntent(c, 0)`
   (`main.cpp:6176`; implementation `:561-571`).
2. End-of-INI `ApplyMode(Xbox)` writes `colorFilterEnable=1`
   (`modeManager.cpp:251`, `applyProfile` `:812-827`; apply call `main.cpp:6769`).
3. On the first `ColourFilter_switch`, `derivePipelineFromPipes(config)` runs
   (`postfx.cpp:1524`) and calls `cfEnableHeal(c)` (`main.cpp:648-654`). The live
   value (1) differs from the recorded intent (0), so it is repaired back to 0
   (`main.cpp:596-610`). The log shows exactly this:
   `[Config] colorFilterEnable=1 diverged from user intent 0 — silent write repaired`.
4. `ColourFilter_switch` reaches `if(!config->colorFilterEnable)` at
   `postfx.cpp:1906` and takes the **bypass** early-return.

Result:

- The PC filter is **not applied**. The whole `switch(colorFilter)` is skipped;
  the `COLORFILTER_PC` case (`CPostEffects::ColourFilter(rgb1pc, rgb2pc)`) at
  `postfx.cpp:2080-2082` never runs.
- `UpdateFrontBuffer()` **does still run**: first at `postfx.cpp:1917` (before the
  bypass's lens group), again at `:1989` (after CA/grain/vignette), and inside
  `DrawFilmGrain` if it were active. So the front-buffer sync is not the thing that
  is skipped.
- The tonemap does **not** run. The bypass's identity-tonemap hook is conditional
  on `cfGate == COLORFILTER_MODERN` (`postfx.cpp:1932-1934`); with
  `colorFilter=PC` and `pipeline=XBOX`, `cfGate` is PC, so `ColourFilter_Modern`
  and `s_tonemapIdentityGrade` are not used. PC has no tonemap anyway.
- The vanilla final full-screen composite is skipped. In the normal PC path,
  `CPostEffects::ColourFilter` draws `pRasterFrontBuffer` back over the camera
  raster (`postfx.cpp:2049-2078` comment; case at `:2080-2082`). The bypass never
  calls it. If the ordering symptom is a missing final re-present/composite, this
  is the candidate.
- The bypass still calls CA, film grain, vignette, IV grade and `chars_drawSSSBlur`
  (`postfx.cpp:1938-2025`), but with this config they are all no-ops: CA intensity
  0, grain intensity 0 (profile), `ivMode=0`, SSS off. So the bypass is effectively
  "sync front buffer, return".

Correction to the earlier audit: `docs/plans/2026-10-25-angle-bugs-oracle.md` §1.2
correctly identifies the heal conflict, but its conclusion that "the PC colour
filter ran" is not supported by the quoted `ColourFilter_switch: filter=2` line.
That line is the entry log (`postfx.cpp:1547`), which is emitted **before** the
bypass check at `:1906`. The discriminating lines are `BYPASSED` (`:1908`) or
`CF switch branch executed` (`:2121`); both share the `cf_switch` throttle tag, so
the entry line can starve them. Source order is unambiguous: effective 0 -> bypass.

---

## 4. Water: `waterParallaxEnable = 0`

Confirmed. `main.cpp:6350` reads it; it is not mode-owned. The four
`CWaterLevel::RenderAndEmptyRenderBuffer` call sites are still intercepted at
`main.cpp:7637-7640`, but the hook gate at `main.cpp:5223-5231` is false, so the
hook falls through to the original game function at `:5236-5238` **before**
`_rwD3D9RenderStateFlushCache()`, the state park, `waterPipe_setRenderState()` or
any raw draw. Therefore the water hooks still cost a gate check and a vanilla call,
but they do **not** touch render state in this path.

- What draws water instead: the game's vanilla `CWaterLevel::RenderAndEmptyRenderBuffer`
  (the original function called by the hook).
- `waterStyle=Xbox` / `waterQuality=0` are irrelevant while the custom pipe is off.
- `waterPipe_init()` still runs at startup and loads the CSOs
  (`main.cpp:7634-7640`), but `g_waterParallaxActive` is reset false
  (`waterPipe.cpp:1047-1054`) and `waterPipe_restoreRenderState()` early-outs
  (`waterPipe.cpp:1702`).
- The live log confirms `[water] gate failed (enable=0 vs=... ps=...) -> original draw`.
- Caveat: `PostFX_UnderwaterTick()` still runs from `ColourFilter_switch`
  (`postfx.cpp:1890`) with `waterUnderwaterFog=1`; that is the underwater fog/SSS
  post pass, independent of the surface water pipe.

---

## 5. Forward+: `forwardPlusEnable=1` but `buildingPipe=Xbox`

Effective Forward+ is **0**, not 1: the Xbox profile writes `forwardPlusEnable=0`
(`modeManager.cpp:293`) after the INI read (`main.cpp:6327`). The live log confirms
`fpCfg=0` on both `[BPIPE] cb=Xbox` and `[VPIPE] cb=Xbox` lines.

- Xbox building callback: **does not consume Forward+**. `buildingPipe_consumesForwardPlus()`
  returns true only for `BUILDING_PBR` (`buildingPipe.cpp:1596-1601`). The Xbox cb's
  `if(buildingPipe_consumesForwardPlus())` is false; with `forwardPlusEnable=0` the
  `else if(config && config->forwardPlusEnable)` branch is also false, so it calls
  **no** `ForwardPlus_SetConstants()` at all (`buildingPipe.cpp:1727-1733`). If FP
  were 1, it would still skip the call and log a breadcrumb (`:1745-1749`), because
  the Xbox simple-PS family reads s0/s1/s2 only.
- Xbox vehicle callback (`vehiclePipe.cpp:1441-1713`): **no** `ForwardPlus_SetConstants()`
  call in that range. It binds `xboxCarVS` and the legacy 7-light constants. The
  only FP calls in that file are the mobile cb (`:1959`) and the env/PBR paths
  (`:3092`, `:3406`), which are not the Xbox car callback.
- Per-frame cost when off: `ForwardPlus_CullAndUpload()` is still called from
  `RenderScene_before` (`main.cpp:3477`), but it early-outs after
  `g_fpAddCount = 0` (`forwardplus.cpp:1251-1263`). `ForwardPlus_FeedGameLights()`
  early-outs (`forwardplus.cpp:674-678`) and `ForwardPlus_AddLight()` early-outs
  (`:607-609`), so nothing is pushed. The corona walk still loops 64 entries
  (`main.cpp:3868-3882`), but each `AddLight` is a no-op. No GPU constant upload,
  no texture bind, no sampler write.
- If FP were forced on under Xbox, there would be real per-frame cost
  (`CullAndUpload` at `main.cpp:3477`) but no Xbox consumer. Worse, any non-consumer
  callback that called `ForwardPlus_SetConstants()` would bind s5-s7 and set
  POINT/CLAMP on samplers 5-7 without restoring them; the code documents this leak
  contract at `buildingPipe.cpp:1290-1318` and gates the PS2/GTAIV cb at
  `:1315-1318`. The Xbox profile forcing FP off is protective here.

---

## 6. Known-bad combinations and the `-1` shadow sentinel

### 6.1 What `-1` actually resolves to

`parseShadowToggle()` (`main.cpp:5954-5978`) maps absent/empty to `-1` via
`boolMap`; a numeric `2` is not a valid shadow toggle and is clamped to `1` with a
one-time log (`:5968-5975`). So `-1` is **not** a hidden "2".

At runtime, `FX::GetFxQuality_ped()` / `_stencil()` return `this->fxQuality` when
the config value is negative (`main.cpp:2558-2570`; field at
`main_exports.h:82`). Therefore:

- `pedShadows=-1` and `stencilShadows=-1` mean "do not override; use the game's
  current FX quality".
- If the game's FX quality is 0/1, shadow detail is reduced/off; if 2/3, the game's
  stencil/ped shadow paths run at that quality. There is no hidden "2 = hidden".
- `fixShadows=0` is independent: it only leaves the shadow offsets vanilla
  (`main.cpp:5986`, patch `:7605-7620`); the shadow state contract still installs.

### 6.2 Known-bad in this list

1. **Mode-owned keys silently overwritten.** `docs/baseline/2026-10-25-angle-series/README.md:426`
   already flags this: `buildingPipe`, `vehiclePipe`, `colorFilterEnable`,
   `dualPassPed`, `pedOpaqueContract`, `pedShadows`, `stencilShadows` are written by
   `ApplyMode()`. Current source extends the same trap to `dualPass`,
   `dualPassVehicle`, `dualPassBuilding`, `dualPassGrass`, `dualPassDefault`,
   `forwardPlusEnable`, `texAlphaClassify`, `stateBaselineEnable`,
   `defaultCbShaderHygiene`, `coronaZtest`, `grassBackfaceCull` (`modeManager.cpp:214-297`).
   In this INI, `dualPass=0`, `dualPassVehicle=0`, `forwardPlusEnable=1` and
   `colorFilterEnable=0` are all mode-owned and do not mean what they say.
2. **`colorFilterEnable=0` + `[Modes] active=Xbox` is an active fight.** The profile
   writes 1 (`modeManager.cpp:251`); the latch heals it to 0 every frame
   (`main.cpp:604`, called from `postfx.cpp:1524`). The profile's intended CF gate
   is unreachable while the INI says 0. This is the most acute config inconsistency
   in the list.
3. **`colorFilter=PC` + `colorFilterEnable=0`** makes the filter selection dead
   config: PC is selected, but the bypass at `postfx.cpp:1906` prevents the PC
   switch from running. Not harmful, but misleading.
4. **`forwardPlusEnable=1` + Xbox** is ignored (profile 0). If the profile were
   disengaged, the INI 1 would turn on full tile culling with no Xbox consumer;
   the code's leak contract (`buildingPipe.cpp:1290-1318`) explains why that is
   undesirable. As configured, the profile's 0 avoids it.
5. **`stencilShadows=-1` / `pedShadows=-1` are not "known-bad"**, but they leave
   the shadow-volume pass (the only known CULLFRONT source) and ped stored shadows
   enabled. Earlier audits call the stencil-volume FRONT cull a leading
   angle-dependent suspect (`docs/plans/2026-10-25-ped-angle-disappearance.md:136`,
   `:291`, `:424-429`). The correct A/B is `stencilShadows=0` (and optionally
   `pedShadows=0`), not a reinterpretation of `-1`.
6. **Documentation drift:** `docs/feature-matrix.md:70` says `pedShadows` /
   `stencilShadows` are not mode-owned. The current source owns both in the PS2,
   Xbox and PC profiles (`modeManager.cpp:172-173`, `277-278`, `361-362`, `490-493`).
   Trust the source, not that table, when predicting effective values.

---

## 7. Ranked single-change isolation tests

"Single change" means one INI line. Mode-owned keys are marked; while Xbox is
active, editing them is a no-op because `ApplyMode()` rewrites them at the end of
`readIniFile()`.

| Rank | Change | Expected observable result |
|---|---|---|
| 1 | `[Modes] active = None` | Drops the whole Xbox profile: `dualPass`/`dualPassVehicle` go back to 0, `forwardPlusEnable` to 1, `colorFilterEnable` stays 0, and `grainIntensity` returns to 0.284. If the transparency/ordering artifact changes or disappears, the Xbox profile is causal; if it is identical, the profile is exonerated and the bug is in shared/base rendering. Log should show `[Mode] disengage restored=...` and `dualPass=0`, `fpCfg=1`. |
| 2 | `colorFilterEnable = 1` | Keeps the Xbox profile, but the latch now records 1, so the bypass at `postfx.cpp:1906` is skipped and the PC case (`CPostEffects::ColourFilter`, `:2080-2082`) runs the final full-screen composite. If the ordering artifact changes, the missing final composite/PC path is implicated; if only colour changes, ordering is independent. Log should show `CF switch branch executed: colorFilter=2` instead of the bypass. |
| 3 | `pedAlphaTestEnable = 0` | Not mode-owned (`main.cpp:6163-6165`), so it takes effect immediately. If ped/foliage cutouts or transparent ordering change, alpha-test classification is involved; if identical, alpha test is exonerated. Log `[PedAlphaTest] enabled=0`. |
| 4 | `pedCullMode = 1` | Not mode-owned (`main.cpp:6159-6161`). 1 = CULLNONE, 2 = CULLBACK, 3 = CULLFRONT (`main.cpp:1784-1800`). CULLNONE makes the ped span two-sided. If the artifact disappears, cull/winding is confirmed; if identical, cull is exonerated. Follow with 2/3 if 1 changes anything. Log `[PedCull] mode=1 ...`. |
| 5 | `stencilShadows = 0` | Mode-owned (`modeManager.cpp:278`): with Xbox active the profile rewrites it to -1, so this test requires test 1 first (or the debug menu). It removes the only known CULLFRONT source. If the artifact disappears, the shadow-volume state/cull is implicated; if unchanged, shadow volumes are exonerated. |

Next tests if #1 changes the symptom: bisect the mode-owned transparency flags one
at a time with the debug menu (which writes after `ApplyMode()`):
`dualPass=0`, `texAlphaClassify=0`, `pedOpaqueContract=0`, `stateBaselineEnable=0`,
`defaultCbShaderHygiene=0`. Also worth a cheap A/B: `bldEnvReflect=0` (Xbox building
env branch) and `fixShadows=1` (vanilla offset patch).

---

## Appendix: live-log corroboration

From `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`:

- `[Mode] engage Xbox owned=79 pipes=2/1/2 cf=2`
- `[Mode] apply Xbox pipes=2/1/2 cf=2 iv=0`
- `[Config] colorFilterEnable=1 diverged from user intent 0 — silent write repaired (slot 0)`
- `[water] gate failed (enable=0 vs=... ps=...) -> original draw`
- `[BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 ... fp=00000000/00000000`
- `[VPIPE] cb=Xbox pipe=XBOX(2) ivMode=0 fpCfg=0 fpUp=0 VS=IDR_XBOXCARVS`

These match the source-level effective values: Xbox mode, CF bypass (after heal),
vanilla water, no Forward+ upload/bind.
