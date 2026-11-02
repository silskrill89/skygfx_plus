> SUPERSEDED by docs/modes.md (2026-10-25). Profile counts and "Mobile hidden/3 fields" statements below are historical.

# Mode & preset architecture — consolidation design (2026-10-24)

**Status:** PROPOSED (design only, no source edited)
**Scope:** `src/core/presets.cpp`, `src/core/modeManager.cpp`, `src/render/*_mode.cpp`, `src/skygfx.h`, `src/core/main.cpp`, `src/core/debugmenu_ui.cpp`
**Requirement:** 3 vintage modes (PS2, Xbox, Legacy PC) + 3 modern modes (GTA IV, GTA V, Modern) + full user customisation; legacy PC must expose every function of the original PC code, including beta/unshipped paths.
**Authority:** `docs/plans/2026-10-24-gtasource-pipes.md` (P), `docs/plans/2026-10-24-gtasource-render-core.md` (R), `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` (A), `docs/plans/2026-10-24-legacy-parity-matrix.md` (L). Line numbers are from the tree as read during this audit; re-verify with `grep -n` before patching.

**Definitions used below:**
*Base config* = the live `*config` before a mode is applied (today: the pre-mode snapshot). *Owned field* = a field a mode profile writes. *Derived field* = computed, never user-authored. *Override* = a per-mode user edit stored against that mode.

---

## 0. Verdict in five lines

1. Three preset systems exist and none is complete for PS2/Xbox/PC-vintage: `ApplyPreset` (`PresetConfig`), `ModeManager` profiles, and the load-time `qualityPreset`/`pipeline` block.
2. The per-mode bodies in `src/render/{ps2,xbox,mobile,pc_patched,custom}_mode.cpp` and `iv_mode.cpp` are **dead code** — no call site outside their own definitions and the `skygfx.h` declarations.
3. Selecting PS2 or Xbox changes only 3 fields (`pipeline`, `buildingPipe`, `vehiclePipe`) and leaves the tone-map, dual-pass, detail/stochastic, PS2-modulate, env-map size and corona Z-test on the previous values. GTA V changes nothing at all.
4. There is no persistence of the active mode and no listener that pushes a mode change into the renderer, so the mode label lies after any reload, config switch or programmatic change.
5. The fix is one `ModeProfile` + one `ApplyMode()` + an explicit ownership/precedence contract, then moving the legacy PC feature set into `Config` (append-only) with real INI keys.

---

## 1. Current-state map

### 1.1 The three preset systems

| # | System | Enums / data | Entry points | Fields it can write |
|---|---|---|---|---|
| **A** | **Game presets** ("Style") | `GamePreset` `skygfx.h:216-252`; `PresetConfig` `skygfx.h:255-275`; `presetConfigs[15]` `presets.cpp:25-105`; `presetNames` `presets.cpp:5-21` | `ApplyPreset()` `presets.cpp:108-155`; declared `skygfx.h:899`; called from `debugmenu_ui.cpp:2737, 2759, 2769, 2771, 2773, 2775` | 13 direct fields + 6 cascades + 2 shadow toggles. **No** `detailMaps`, `stochastic`, `envMapSize`, `envMapFarClipMult`, `doglare`, `neoWaterDrops`, `coronaZtest`, `pedAmbient*`, `ambient*`. |
| **B** | **Mode manager** | `ModeId` `modeManager.h:23-33` (`NONE, PS2, XBOX, MOBILE, GTAIV, GTAV`); profiles `modeManager.cpp:61-177`; snapshot `modeManager.cpp:23` | `ModeManager_SetMode()` `modeManager.cpp:241-294`; UI `debugmenu_ui.cpp:2686-2711`; IV row `debugmenu_ui.cpp:802-829` | `profileGTAIV` 20 fields (`modeManager.cpp:64-116`); `profilePS2` **3** (`:129-131`); `profileXbox` **3** (`:138-140`); `profileMobile` **3** (`:147-149`); `profileGtav` **0** (`:152-162`). |
| **C** | **Load-time `qualityPreset` + pipeline block** | local `int preset` (`main.cpp:4921`), `g_qualityPreset` (`main.cpp:4840`); `pipelineMap` (`main.cpp:4871-4879`); `buildingPipeMap` (`:5078-5084`); `vehiclePipeMap` (`:5089-5102`) | `readIniFile()` `main.cpp:4846-…`; `refreshIni()` `:700-737` | `qualityPreset` 0/1/2 **writes pipes** (`main.cpp:4930-4931, 4955-4956, 4979-4980`); `pipeline` writes pipes (`:4888-4914`); explicit overrides (`:5085-5105`); `pipelineOverride` rewrites `pipeline` (`:4883-4885`). |
| **D** | *(dead)* per-mode preset bodies | `PS2Mode_*`, `XboxMode_*`, `IVMode_*`, `MobileMode_*`, `PCPatchedMode_*`, `CustomMode_*` declared `skygfx.h:1426-1444` | **no call sites** (verified: only definitions + the declarations) | n/a — dead |

### 1.2 Who writes `Config`

| Writer | Location | Notes |
|---|---|---|
| `readIniFile()` | `main.cpp:4846-…` | pipeline→pipes lock, qualityPreset block, explicit pipe overrides, then ~300 key reads |
| `derivePipelineFromPipes()` | `main.cpp:605-680` | writes `pipeline`; **also rewrites the pipes + `colorFilter`** under PBR rule 3 (`:663-675`) |
| `refreshIni()` | `main.cpp:700-737` | calls `derivePipelineFromPipes` + `resetValues` |
| `ColourFilter_switch()` | `postfx.cpp:1368` | calls `derivePipelineFromPipes(config)` **every frame** |
| `ApplyPreset()` | `presets.cpp:108-155` | drops the mode latch first (`:120-121`) |
| `ModeManager_SetMode()` | `modeManager.cpp:241-294` | whole-Config snapshot write `*config = g_preModeConfig` (`:261`), `*config = next` (`:286`) |
| `ApplyPipelineFromMenu()` | `debugmenu_ui.cpp:738-770` | pipeline combo writes pipes |
| `ApplyPipelineOverrideFromMenu()` | `debugmenu_ui.cpp:781-790` | calls the above |
| `ivModeRowEdit()` / `ivModeLegacyRowEdit()` | `debugmenu_ui.cpp:802-846` | routes through the mode manager |
| debug-menu pixel rows | `debugmenu_ui.cpp:2281-2420` | every `RK_*` control writes `config->field` directly |
| `menu_inject.cpp` | `menu_inject.cpp:370-371` | IV toggle latch clear only |

### 1.3 Who reads `Config` (mode-relevant)

| Consumer | Location | Fields |
|---|---|---|
| Building pipe switch | `buildingPipe.cpp:2561-2575` | `buildingPipe` (+ `gRenderingSpheremap` `:2560`) |
| Vehicle pipe switch | `vehiclePipe.cpp:3202-3266` | `vehiclePipe` |
| Colour filter + timecyc | `postfx.cpp:1525-1526` (`PIPELINE_PBR`→MODERN), `:1578` (`usePCTimecyc`), `:1696-1715` | `pipeline`, `colorFilter`, `usePCTimecyc` |
| Ambient | `pipelinecommon.cpp:190-213` + `ClampAmbientToSky` `:97-188` | `ambientMult`, `ambientFloor`, `pbrAmbientFloor` |
| Default pipe + ped contract | `main.cpp:1328-1798` | `dualPassDefault/Ped`, `pedOpaqueContract`, `defaultCbShaderHygiene`, `texAlphaClassify`, `stateBaselineEnable` |
| Corona Z-test patch | `resetValues()` `main.cpp:334-349` | `coronaZtest` |
| PS building selection | `buildingPipe_selectPS()` `buildingPipe.cpp:940-959` | `detailMaps`, `stochastic` |

### 1.4 Why selecting a mode currently does nothing (exact chain)

1. The Modes radio rows call `ModeManager_SetMode(id)` and then `resetValues()` — **only** from the UI (`debugmenu_ui.cpp:2700-2706`). There is **no registered listener** anywhere (`ModeManager_RegisterListener` has no call site outside `modeManager.cpp:224-239`), so any non-UI caller gets no renderer push.
2. `profilePS2` writes exactly `pipeline`, `buildingPipe`, `vehiclePipe` (`modeManager.cpp:129-131`). `profileXbox` the same (`:138-140`). The comment says `// expand later` (`:128`, `:137`). `profileGtav` writes **nothing** (`:152-162`).
3. Therefore after clicking PS2:
   * `colorFilter` keeps its previous value (e.g. `COLORFILTER_MODERN` under PBR). The PBR→MODERN force only fires while `pipeline == PIPELINE_PBR` (`postfx.cpp:1525-1526`), so a PS2 pipe pair is now composited through the Hable filmic tone-map.
   * `ps2ModulateGlobal/Building/Grass` stay 0 (they are 0 on a fresh PBR install, `main.cpp:5074-5075`, `:5117`, `:5212`) → PS2 modulation never enables.
   * `dualPassGlobal` and all five sub-toggles keep their previous values (fresh install: 0 through the cascade `main.cpp:5118, 5132, 5213, 5226-5227`).
   * `detailMaps` / `stochastic` stay 1/1 (ULTRA `main.cpp:5042-5043`) → PBR material behaviour leaks into PS2.
   * `grainFilter`, `infraredVision`, `nightVision` stay at their PC-variant defaults (`main.cpp:5298-5300`, `ps2pcMap` `:5275-5279`).
   * `coronaZtest` stays -1 → the PS2 no-Z-test coronas never happen (`main.cpp:335-349`).
   * `envMapSize` / `envMapFarClipMult` stay 512/2.0 (ULTRA `main.cpp:5060-5061`) instead of the aap 256/1.0 (A §"Look baseline", `backup_original/src/main.cpp:1315`).
   * `tagsBuildingPipe` is not touched; it is only assigned in `readIniFile` (`main.cpp:5340`) and read by nothing (`buildingPipe.cpp:325` uses `buildingPipe`).
   * `preset` (Style combo) is not updated by `profilePS2`/`profileXbox`, unlike `profileGTAIV` which sets `PRESET_IV_PC` (`modeManager.cpp:73`). The Style combo therefore keeps displaying the old entry (`debugmenu_ui.cpp:2726-2728`).
4. Everything in `ps2_mode.cpp:42-103`, `xbox_mode.cpp:39-99`, `mobile.cpp:39-99`, `pc_patched_mode.cpp:40-100`, `custom_mode.cpp:46-165` is unreachable. Their doc comments are also stale: `ps2_mode.cpp:28-31` cites `buildingPipe.cpp:188-386` but the aap callback starts at `:166`; `xbox_mode.cpp:28-31` cites `:390-533` but it is `:368-511` (A D15).
5. Mode identity is **not persisted**: `saveConfigTo()` writes `pipeline`, `pipelineOverride`, `buildingPipe`, `vehiclePipe`, `colorFilter` (`main.cpp:6106-6122`) and `gamePreset` (`:6420`), but no mode key. There is no `mode`/`activeMode` string anywhere in the reader or writer.
6. `readIniFile()` re-applies `qualityPreset` **after** the pipeline lock and **writes pipes for tiers 0-2** (`main.cpp:4930-4931, 4955-4956, 4979-4980`). A reload (`F11`, `reloadAllInis`) therefore silently overwrites mode pipes with the tier's pipes.
7. `ApplyPreset()` clears the mode latch but does **not** restore the snapshot (`presets.cpp:120-121`; `ModeManager_ClearLatch` deliberately does not restore, `modeManager.cpp:214-219`) → pressing a Style preset while a mode is lit leaves a hybrid config with no owner.
8. The single snapshot is **not keyed by config slot**: `g_preModeConfig` is one static `Config` (`modeManager.cpp:23`) while `config` can be repointed by `setConfig()` to another `configs[]` slot (`main.cpp:6071-6078`, no mode interaction). Toggle-off then restores slot A's pre-mode values into slot B (`modeManager.cpp:260-262`).
9. `derivePipelineFromPipes()` still rewrites `buildingPipe`/`vehiclePipe`/`colorFilter` when PBR wins rule 3 (`main.cpp:663-675`). That is the only re-derivation that can *change* a user/mode pipe choice, and it fires every frame from `postfx.cpp:1368`.
10. `pipelineOverride` is honoured at load (`main.cpp:4883-4885`) and re-applied from the menu by rewriting the pipes (`debugmenu_ui.cpp:781-790`), so a debug pin can defeat a mode.

**Net:** PS2/Xbox clicking changes the world/vehicle pipe pair; everything that makes the look *look* like that platform is left on the base config, GTA V writes nothing, and nothing survives a reload.

### 1.5 Additional defects found (relevant to the overhaul)

* `simpleStochasticPS` is not bound anywhere: no global, no `IDR_SIMPLESTOCHASTICPS`, no `makePS` (`pipelinecommon.cpp` shader list), and `buildingPipe_selectPS` returns `simplePS` for stochastic-without-detail (`buildingPipe.cpp:949-958`). The CSO still exists and still builds (`resources/cso/simpleStochasticPS.cso`). → stochastic buildings without a detail map render flat in both vintage modes (A D2).
* Xbox would disable stochastic if its dead preset were wired as-is: `xbox_mode.cpp:94` sets `stochastic = 0`, contradicting the aap first-run template `stochastic=1` (`backup_original/src/main.cpp:1477`) (A D8).
* PS2 would lower the reflection resolution: `ps2_mode.cpp:101` proposes `envMapSize = 128`; aap's default is 256 (`backup_original/src/main.cpp:1315`) (A D10).
* `forceWindShader`, `privateHooks`, `fixPcCarLight`, `disableClouds`, `disableGamma`, `transparentLockon`, `fixShadows`, `explicitBuildingPipe` are **globals, not `Config` fields** (`main.cpp:55-71`, parsed `:5253-5260, 5337-5338, 5474-5475`, saved `:6441-6453`). They cannot be per-config or per-mode.
* `tagsBuildingPipe` is a `Config` field that is both dead (`buildingPipe.cpp:325`) and not written by `saveConfigTo` — only by the legacy `MENUSETTINGS` macro (`main.cpp:6515`).
* `vignetteIntensity/Radius/Contrast` are shared between the legacy Vignette block and the new `FxVignette` pass (`skygfx.h:531-533` + comment `:843-852`).

---

## 2. Canonical model

### 2.1 One enum, six modes + Custom

```c
// skygfx.h — append-only additions (see §6 invariants)
enum ModeId {
    MODE_NONE        = 0,   // no mode: the live Config is the user's (legacy behaviour)
    MODE_PS2         = 1,   // vintage: PS2
    MODE_XBOX        = 2,   // vintage: Xbox
    MODE_PC_VINTAGE  = 3,   // vintage: original PC (Rockstar PC pipes + aap PC set)
    MODE_GTAIV       = 4,   // modern: GTA IV
    MODE_GTAV        = 5,   // modern: GTA V (probe-gated, see §3.5)
    MODE_MODERN      = 6,   // modern: PBR / "Custom Maxed"
    MODE_CUSTOM      = 7,   // user-defined, no profile
    MODE_MOBILE      = 8,   // LEGACY alias, hidden from the 6+Custom picker (kept so the
                            // existing mobile profile keeps working; see note below)
    MODE_COUNT
};
```

* Numeric values are **new**; today's `ModeId` order is `NONE, PS2, XBOX, MOBILE, GTAIV, GTAV` (`modeManager.h:25-32`). Nothing persists the mode id today (no INI key, `modeManager.cpp` only logs names), so renumbering is safe. `MODE_MOBILE` is retained at the end only so the existing `CAR_MOBILE` renderer choice is not deleted; it is **not** one of the required six, is equally incomplete as the other stubs (3 fields, `modeManager.cpp:147-149`), and is hidden from the picker until someone writes its profile.
* `MODE_NONE` and `MODE_CUSTOM` are not the same thing and must both exist:
  * `MODE_NONE` = "no profile applied; `Config` is exactly what the INI/menu says" (today's `ClearLatch`/toggle-off state).
  * `MODE_CUSTOM` = "a profile base plus the user's explicit overrides for every field" — the editable mode.
  The old `MODE_NONE` toggle-off restore semantics remain the behaviour of **leaving a mode to `MODE_NONE`**.

### 2.2 One profile struct with an ownership mask

The current `PresetConfig` cannot carry the mode because it has 18 positional ints and no room for `detailMaps`/`stochastic`/`envMapSize`/`coronaZtest`/`legacyAmbient`/… (A D1). The replacement is **data + a mask**, so a profile states exactly what it owns and nothing else is touched:

```c
// One entry per owned field, so ownership is compile-time (no string table).
enum ModeField {
    MF_PIPELINE, MF_BUILDING_PIPE, MF_VEHICLE_PIPE, MF_COLOR_FILTER,
    MF_TAGS_PIPE, MF_PRESET, MF_IV_MODE,
    MF_PS2MOD_GLOBAL, MF_PS2MOD_BUILDING, MF_PS2MOD_GRASS,
    MF_DUALPASS_GLOBAL, MF_DUALPASS_BUILDING, MF_DUALPASS_VEHICLE,
    MF_DUALPASS_PED, MF_DUALPASS_GRASS, MF_DUALPASS_DEFAULT,
    MF_DETAIL_MAPS, MF_STOCHASTIC, MF_USE_PC_TIMECYC,
    MF_RADIOSITY, MF_DO_RADIOSITY, MF_GRAIN, MF_IR, MF_NV,
    MF_VCS_TRAILS, MF_SSAO, MF_SMAA, MF_MOTION_BLUR,
    MF_SSS_PP, MF_SKIN_ENH, MF_HAIR_ENH, MF_VEG_ENH,
    MF_NORMAL_MAP, MF_BLOOM, MF_DOF, MF_SUNSHAFTS, MF_FXAA,
    MF_IV_GRADE, MF_TONEMAP,
    MF_ENVMAP_SIZE, MF_ENVMAP_FARCLIP, MF_ENVMAP_LODS, MF_REFL_MSAA,
    MF_CORONA_ZTEST, MF_SUN_CORONA_GAIN,
    MF_WATER_STYLE, MF_WATER_QUALITY,
    MF_LEGACY_AMBIENT, MF_PED_AMBIENT_OVERRIDE, MF_LEGACY_STATE_MODEL,
    MF_ALPHA_TEST_LEGACY, MF_PIPE_ALPHA_EXACT, MF_CORONA_SHIELD,
    MF_LEGACY_PC_PIPES,               // the L4 feature bundle (§4)
    MF_FORWARD_PLUS, MF_GRASS_INSTANCING, MF_TREE_WIND, MF_TREE_FX,
    MF_DEPTH_HOOK, MF_NORMAL_BUFFER, MF_PIPE_CHAIN, MF_PROFILER,
    MF_COUNT
};

struct ModeValues {
    int   i[MF_COUNT];      // every field stored as int (enums, bools, ints)
    float f[MF_COUNT];      // floats for the float-typed entries
};

struct ModeProfile {
    ModeId          id;
    const char     *name;          // "PS2", "Xbox", "Legacy PC", "GTA IV", "GTA V", "Modern"
    const char     *iniAlias;      // "PS2", "Xbox", "PC", "GTAIV", "GTAV", "Modern"
    unsigned __int64 owns[2];      // 128-bit mask over ModeField
    ModeValues      v;
};
extern const ModeProfile g_modeProfiles[6];   // MODE_PS2..MODE_MODERN, index = id - 1
```

One generic applier, one field setter table (`switch(field)` → `Config` member), so adding a field is one enum + one case and the mask cannot drift from the values.

### 2.3 Ownership: authoritative vs derived

| Field(s) | Class | Rule |
|---|---|---|
| `buildingPipe`, `vehiclePipe` | **authoritative** | Written only by `ApplyMode()` and explicit user edits. Never re-derived from `pipeline`. |
| `pipeline` | **derived (compat mirror)** | Recomputed from the pipes by `derivePipelineFromPipes()` (rules 1-7, `main.cpp:624-638`). It exists only because postfx gates on it. Never user-authoritative when pipes exist; the UI combo becomes a shortcut that calls `ApplyMode`, not a writer. |
| `colorFilter` | **authoritative, except one hard lock** | User/mode choice for every pipe family. `BUILDING_PBR` ⇒ forced `COLORFILTER_MODERN` (existing contract `main.cpp:5286-5294`, `postfx.cpp:1525-1526`); the UI already has the OVERRIDDEN badge for it (`debugmenu_ui.cpp:1843-1866`). |
| `tagsBuildingPipe` | **derived by default** | `= buildingPipe` unless the new `tagsBuildingPipeExplicit=1`. Keep the field; honour it again in `TagRenderCB` (L D5 / parity item 1). |
| `preset` (`gamePreset`) | **derived display** | Set by the mode table so the Style combo reflects the active mode; never used to drive pipes. |
| `ivMode` | **authoritative** | Profiles set it; `SyncIvModeFromPipeFamily` still repairs a family crossing (`debugmenu_ui.cpp:718-732`). |
| everything else | **authoritative** | Mode-owned or user-owned; pass-through otherwise. |

**Precedence (lowest → highest), and it is a total order:**

1. `ModeProfile(activeMode)` values — the mode's factory defaults.
2. Top-level INI keys — **only for fields the active mode does not own**.
3. `[Mode.<name>]` override keys — per-mode user edits (sparse diff vs the profile).
4. Live menu edit — writes the live `Config` *and* records the override for the active mode (and, on save, the top-level key so dropping to `MODE_NONE` keeps the look).

**No silent re-derivation rule:** when a mode is active, the top-level keys for owned fields are **ignored** (one log line, not an error). Only `derivePipelineFromPipes()` may write, and only `pipeline` + the PBR `colorFilter` lock. It may never write pipes except under the existing rule-3 PBR lock, and that lock must also set the colour-filter override badge.

### 2.4 One apply path

```c
// modeManager.cpp — the ONLY writer of mode-owned fields.
void ModeManager_SetMode(ModeId id)
{
    if(!config) return;
    ModeId old = s_mode;

    if(id == old){                       // toggle off (kept for parity with today's UI)
        if(old == MODE_NONE) return;
        SaveModeOverrides(old);          // persist the user's edits for `old` first
        RestoreBase();                   // *config = base snapshot (per config slot, see below)
        s_mode = MODE_NONE;
        ApplyDerived(config);
        PushToRenderer(old, MODE_NONE);
        return;
    }

    if(old == MODE_NONE){
        s_baseConfig[ConfigSlot(config)] = *config;   // keyed by slot, not one global
        s_haveBase[ConfigSlot(config)]   = true;
    }
    SaveModeOverrides(old);              // never lose edits made while `old` was active
    s_mode = id;

    Config next = s_haveBase[ConfigSlot(config)] ? s_baseConfig[ConfigSlot(config)] : *config;
    if(id != MODE_CUSTOM && id != MODE_NONE)
        ApplyProfile(&next, &g_modeProfiles[id - 1]);   // owned fields only
    ApplyModeOverrides(&next, id);                      // [Mode.<name>] sparse diff
    ApplyDerived(&next);                                // pipeline + tags (+ PBR filter lock)
    *config = next;

    PushToRenderer(old, id);             // registered listener, exactly one
}
```

`PushToRenderer` (single listener, registered in `InjectDelayedPatches` next to `ModeManager_Init` `main.cpp:6756`) does exactly four things and nothing else:

1. `resetValues()` — `doRadiosity`, `grainFilter`, `coronaZtest` (`main.cpp:296-351`).
2. `config->tagsBuildingPipe = config->buildingPipe` when not explicit.
3. `MakeEnvmapRasters()` only when `envMapSize`/`reflectionMsaaLevel` actually changed (`envmap.cpp:39-67` recreates on mismatch; per-pass call `:561`).
4. One structured log line: `[mode] old -> new pipes=B%d/V%d filter=%d ps2mod=%d dual=%d detail=%d stoch=%d env=%d corona=%d`.

`ApplyDerived` = `derivePipelineFromPipes()` verbatim, with the rule-3 pipe rewrite removed (see §2.6).

### 2.5 What happens to the existing systems

| Existing system | Fate |
|---|---|
| `PresetConfig` / `presetConfigs[]` | **Kept, demoted to "game flavour".** `ApplyPreset()` may set only `colorFilter` + grade fields + `preset`; it may **never** write pipes, dual-pass, detail or env-map. Rename its UI to "Colour grade preset". `GamePreset` values stay stable because `gamePreset` is persisted as an int (`main.cpp:5587, 6420`). |
| `ApplyPreset()` | Calls `ModeManager_ClearLatch()` only if the target preset is incompatible with the active mode's pipe family; otherwise keeps the mode. Must not clear the latch silently (today `presets.cpp:120-121`). |
| `qualityPreset` | **Feature tier only.** Delete the pipe writes in cases 0/1/2 (`main.cpp:4930-4931, 4955-4956, 4979-4980`); ULTRA already does not write pipes (comment `:5013-5014`). Old INIs that relied on tier=0 getting PS2 pipes keep working through the back-compat key `qualityPresetPipes=1` (default 0) which runs the old writes **once at load** with a log line. |
| `pipeline` combo | Becomes an alias for `ApplyMode(MODE_MODERN/PS2/XBOX/GTAIV)` — it must not write pipes itself. `pipeline` remains a derived field. |
| `pipelineOverride` | **Debug pin of the derived `pipeline` only.** Remove the pipe rewrite in `ApplyPipelineOverrideFromMenu` (`debugmenu_ui.cpp:781-790`). |
| `profileGTAIV` | Becomes `g_modeProfiles[MODE_GTAIV-1]` (data, not code). Same values. |
| `profilePS2` / `profileXbox` / `profileMobile` / `profileGtav` | Deleted; replaced by the tables in §3. |
| `src/render/{ps2,xbox,mobile,pc_patched,custom}_mode.cpp`, `iv_mode.cpp` | Deleted except `iv_mode.cpp`'s `ivMode` flag note. Their presets are superseded by §3. This removes the "two sources of truth" drift (A A1/D15). |
| `ModeManager_ClearLatch` | Kept for the legacy direct writers (`presets.cpp`, `menu_inject.cpp:371`, `debugmenu_ui.cpp:842`). |

### 2.6 Exact edits required to stop the fighting

| # | Change | File:line |
|---|---|---|
| E1 | Remove pipe writes from `derivePipelineFromPipes` rule 3; keep setting `pipeline` + the documented `colorFilter` lock. | `main.cpp:663-675` |
| E2 | Remove `qualityPreset` pipe writes (0/1/2); add `qualityPresetPipes` back-compat gate. | `main.cpp:4930-4931, 4955-4956, 4979-4980` |
| E3 | Make `ApplyPipelineOverrideFromMenu` not rewrite pipes. | `debugmenu_ui.cpp:781-790` |
| E4 | Make `ApplyPipelineFromMenu` route to `ModeManager_SetMode` (or delete the combo). | `debugmenu_ui.cpp:738-770` |
| E5 | Key the pre-mode snapshot by config slot. | `modeManager.cpp:23, 276-279` |
| E6 | Register the renderer-push listener. | `main.cpp:6756` (next to `ModeManager_Init`) |
| E7 | Persist/read `[Modes] active` + `[Mode.<name>]` overrides. | `main.cpp:readIniFile` / `saveConfigTo` |
| E8 | Reactivate `tagsBuildingPipe` in `TagRenderCB`. | `buildingPipe.cpp:320-338` |
| E9 | Wire `IDR_SIMPLESTOCHASTICPS` + the no-detail stochastic fallback. | `pipelinecommon.cpp` shader list; `buildingPipe.cpp:949-958`; `resources/resource.h/.rc` |

---

## 3. Per-mode field table

**Reading the tables.** Each table *is* the profile's `owns` set: every row is a field that differs from the base config, with the exact value. Fields not listed are **not owned** and always pass through (including all user-tuning fields). The "default" column is the fresh-install value (`pipeline=PBR` + `qualityPreset=3`, template `main.cpp:5750-5877`) so the delta is visible. `NEW` = field does not exist in `Config` yet (§4). `TBD-probe` = the source docs do not pin a value; must be measured, not invented.

Fresh-install baseline used as the "default" column: `radiosity=1`, `doRadiosity=<stream.ini>`, `grainFilter=1`, `infraredVision=1`, `nightVision=1`, `ssaoEnable=0`, `smaaEnable=0`, `motionBlurEnable=0`, `envMapSize=512`, `envMapFarClipMult=2.0`, `detailMaps=1`, `stochastic=1`, `dualPass*=0`, `ps2Modulate*=0`, `coronaZtest=-1`, `envPower=128`, `envFresnel=0.4`, `pedOpaqueContract=1`, `stateBaselineEnable=1`, `texAlphaClassify=1`.

### 3.1 `MODE_PS2` (vintage)

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_PS2` | PBR | `modeManager.cpp:129` |
| | `buildingPipe` | `BUILDING_PS2` | PBR | `modeManager.cpp:130` |
| | `vehiclePipe` | `CAR_PS2` | Modern | `modeManager.cpp:131` |
| | `colorFilter` | `COLORFILTER_PS2` | Modern | `ps2_mode.cpp:69`, `presets.cpp:44`, A §"Look baseline" |
| | `tagsBuildingPipe` | `BUILDING_PS2` | PBR | `backup_original/src/buildingPipe.cpp:151` (A D7) |
| | `preset` | `PRESET_SA_PS2` (6) | custom | `presets.cpp:43` (missing today — A D1) |
| | `ivMode` | `0` | 0 | `ps2_mode.cpp:91` |
| Building | `ps2ModulateBuilding` | `1` | 0 | `ps2_mode.cpp:72` |
| | `detailMaps` | `0` | 1 | `ps2_mode.cpp:97` |
| | `stochastic` | `0` | 1 | `ps2_mode.cpp:98` |
| | `dualPassBuilding` | `0` | 0 | `ps2_mode.cpp:75` |
| | `usePCTimecyc` | `0` | 0 | PS2 timecyc path; `postfx.cpp:1578` |
| | `NEW legacyAmbient` | `1` | 0 | A D3, L §2.1 P9 / §3 #9 |
| Vehicle | `dualPassVehicle` | `0` | 0 | `ps2_mode.cpp:76` |
| | `envMapSize` | `256` | 512 | aap `backup_original/src/main.cpp:1315` (A D10; `ps2_mode.cpp:101` says 128 — **do not copy**) |
| | `envMapFarClipMult` | `1.0` | 2.0 | `ps2_mode.cpp:102`, aap `:1319` |
| | `envMapUseLODs` | `0` | 0 | aap `:1320` |
| | `reflectionMsaaLevel` | `0` | 0 | perf: RW 3.6 rasters have no MSAA API (`skygfx.h:353-357`) |
| Water | `waterStyle` | `0` (Xbox core) | 0 | only implemented base style (`main.cpp:5682-5687`); PS2 `CVuWaterRenderer` is absent from the tree (R §3.5) → **TBD-probe** |
| PostFX | `radiosity` | `0` (PS2 hardware) | 1 | `ps2_mode.cpp:81` |
| | `doRadiosity` | `1` | * | `ps2_mode.cpp:82` |
| | `grainFilter` | `0` (PS2 grain) | 1 | `ps2_mode.cpp:84` |
| | `infraredVision` | `0` | 1 | `ps2_mode.cpp:85` |
| | `nightVision` | `0` | 1 | `ps2_mode.cpp:86` |
| | `ssaoEnable` | `0` | 0 | `ps2_mode.cpp:88` |
| | `smaaEnable` | `0` | 0 | `ps2_mode.cpp:89` |
| | `motionBlurEnable` | `0` | 0 | feature did not exist in the era; LOW tier reference `main.cpp:4935` |
| | `sssPostProcessEnable` | `0` | 1 | `main.cpp:4936` |
| | `skinEnhanceEnable` | `0` | 1 | `main.cpp:4937` |
| | `hairEnhanceEnable` | `0` | 1 | `main.cpp:4938` |
| | `vegetationEnhanceEnable` | `0` | 1 | `main.cpp:4939` |
| | `normalMapEnable` | `0` | 0 | not in legacy |
| | `bloomEnable` | `0` | 0 | not in legacy |
| | `dofEnable` | `0` | 0 | not in legacy |
| | `sunShaftsEnable` | `0` | 0 | not in legacy |
| | `fxaaEnable` | `0` | 0 | not in legacy |
| | `grainIntensity` | `0.0` | 0 | `main.cpp:4939` pattern |
| | `caIntensity` | `0.0` | 0 | not in legacy |
| | `vcsTrails` | `0` | 0 | `presets.cpp:44` (field 9) |
| | `colorFilterEnable` | `1` | 1 | legacy filter always on |
| | `rgb1Mult` / `rgb2Mult` | `1.0` / `1.0` | 1.0 | legacy values (`main.cpp:5296-5297`) |
| | `bYCbCrFilter` | `0` | 0 | aap PS2 filter is not YCbCr (`main.cpp:5366`) |
| | `ivDesaturation`/`ivGamma`/`ivVignetteIntensity`/`ivBloomIntensity` | `0.0/1.0/0.0/0.0` | 0.15/1/0.15/0.05 | `ps2_mode.cpp:92-95` |
| Coronas | `coronaZtest` | `0` | -1 | A §3 A3; aap patch `backup_original/src/main.cpp:330-346` |
| | `NEW sunCoronaGain` bundle (`sunCoronaIntensity`, `sunCoreIntensity`, `sunStreakIntensity`, `sunStreakSize`) | `1.0,1.0,1.0,1.0` | 0.4/0.6/0.5/0.6 | **TBD-probe**: the 0.4/0.6 defaults are PBR tonemap compensations (`skygfx.h:366-373`); identity is the PS2-correct start |
| Alpha/state | `ps2ModulateGlobal` / `ps2ModulateGrass` | `1` / `1` | 0 | `ps2_mode.cpp:71,73` |
| | `dualPassGlobal` / `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `0` | 0 | `ps2_mode.cpp:74,77-79` |
| | `grassAddAmbient` / `backfaceCull` / `fixGrassPlacement` | `0` / `1` / `0` | 0/1/0 | parser defaults `main.cpp:5214-5217` |
| | `pedShadows` / `stencilShadows` | `-1` / `-1` | -1 | `ps2_mode.cpp` preset 52-53; `ApplyPreset` semantics `presets.cpp:150-154` |
| | `zwriteThreshold` / `…Grass` / `…Ped` | `128` | 128 | `main.cpp:5342-5352` |
| | `stateBaselineEnable` | `0` | 1 | legacy has no frame baseline (L §4 row 2) |
| | `pedOpaqueContract` | `0` | 1 | L P4 |
| | `texAlphaClassify` | `0` | 1 | classifier is PBR-only (L A5) |
| | `defaultCbShaderHygiene` | `1` | 1 | keep the repair (`main.cpp:1539-1567`) |
| | `NEW pipeAlphaExactRestore` | `1` | 1 | L A1-A3 (capture blend/atest/BLENDOP) |
| | `NEW coronaStateShield` | `0` | 0 | L X3 |
| | `NEW alphaTestLegacy` | `1` | 1 | L A3 + `DefinedState` `handy.cpp:116-138` (GREATER@2) |
| FPS/perf | `grassInstancing` / `treeWind` / `treeFx` | `0` / `0` / `0` | 0 | `main.cpp:5645-5662` |
| | `forwardPlusEnable` | `0` | 1 | legacy 7-light VS path (R §6) |
| | `pipeChainEnable` / `normalBufferEnable` / `depthHookEnable` | `0` / `0` / `1` | 0/1/1 | PBR-only features |
| | `profilerEnable` | `1` | 1 | leave instrumentation on |

### 3.2 `MODE_XBOX` (vintage)

Same shape as 3.1; only the deltas are listed, everything else identical to 3.1 unless noted.

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_XBOX` | PBR | `modeManager.cpp:138` |
| | `buildingPipe` | `BUILDING_XBOX` | PBR | `modeManager.cpp:139` |
| | `vehiclePipe` | `CAR_XBOX` | Modern | `modeManager.cpp:140` |
| | `colorFilter` | `COLORFILTER_PC` | Modern | `xbox_mode.cpp:65`, `presets.cpp:46` |
| | `tagsBuildingPipe` | `BUILDING_XBOX` | PBR | A D7 |
| | `preset` | `PRESET_SA_XBOX` (7) | custom | `presets.cpp:45` |
| | `ivMode` | `0` | 0 | `xbox_mode.cpp:87` |
| Building | `ps2ModulateBuilding` | `0` | 0 | `xbox_mode.cpp:68` |
| | `detailMaps` | `1` | 1 | `xbox_mode.cpp:93`; aap `:1282-1284` |
| | `stochastic` | **`1`** | 1 | aap first-run `:1477` — **overrides** `xbox_mode.cpp:94` (A D8) |
| | `dualPassBuilding` | `1` | 0 | `xbox_mode.cpp:71` |
| | `usePCTimecyc` | `1` | 0 | Xbox is the PC/Xbox timecycle family (R §2.6) |
| | `NEW legacyAmbient` | `1` | 0 | A D3 |
| Vehicle | `dualPassVehicle` | `1` | 0 | `xbox_mode.cpp:72` |
| | `envMapSize` | `256` | 512 | `xbox_mode.cpp:97`; aap `:1315` |
| | `envMapFarClipMult` | `1.0` | 2.0 | `xbox_mode.cpp:98` |
| PostFX | `radiosity` / `doRadiosity` | `1` / `1` | 1/* | `xbox_mode.cpp:77-78` |
| | `grainFilter` / `infraredVision` / `nightVision` | `0` / `0` / `0` | 1/1/1 | `xbox_mode.cpp:80-82` |
| | `ssaoEnable` / `smaaEnable` | `0` / `0` | 0/0 | `xbox_mode.cpp:84-85` |
| | `motionBlurEnable` | `0` | 0 | `main.cpp:4935` |
| | `sssPostProcessEnable`/`skin`/`hair`/`veg` | `0` | 1 | `main.cpp:4936-4939` |
| | `vcsTrails` | `0` | 0 | `xbox_mode.cpp` preset 48 |
| Coronas | `coronaZtest` | `1` | -1 | A §3 A3 |
| Alpha/state | `ps2ModulateGlobal` / `ps2ModulateGrass` | `0` | 0 | `xbox_mode.cpp:67-69` |
| | `dualPassGlobal` / `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `1` | 0 | `xbox_mode.cpp:70,73-75` |
| | `pedShadows` / `stencilShadows` | `-1` | -1 | preset 49-50 |
| | `NEW pipeAlphaExactRestore` | `1` | 1 | L A1-A3 |
| | `NEW alphaTestLegacy` | `1` | 1 | L A3 |
| | `NEW coronaStateShield` | `0` | 0 | L X3 |
| FPS/perf | `forwardPlusEnable` | `0` | 1 | legacy VS path |

### 3.3 `MODE_PC_VINTAGE` (vintage, the new mode)

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_XBOX` (derived; see note) | PBR | `derivePipelineFromPipes` rule 5 `main.cpp:632-633` — the legacy PC pipe pair has no `PIPELINE_*` value. **Do not add one**; `mode` is authoritative. |
| | `buildingPipe` | `BUILDING_XBOX` | PBR | aap maps `PC`→`BUILDING_XBOX` (`backup_original/src/main.cpp:1272-1276`); `presets.cpp:48` (SA PC) |
| | `vehiclePipe` | `CAR_PC` | Modern | `presets.cpp:48`; the callback is the exe's own PC pipe (`src/rw/gta.cpp:163` → `0x5D9900`) |
| | `colorFilter` | `COLORFILTER_PC` | Modern | `presets.cpp:48` |
| | `tagsBuildingPipe` | `BUILDING_XBOX` | PBR | A D7 |
| | `preset` | `PRESET_SA_PC` (8) | custom | `presets.cpp:47` |
| | `ivMode` | `0` | 0 | `presets.cpp:48` |
| Building | `ps2ModulateBuilding` | `0` | 0 | `presets.cpp:48` |
| | `detailMaps` | `1` | 1 | aap `:1282-1284`, template `:1476` |
| | `stochastic` | `1` | 1 | aap template `:1477` |
| | `dualPassBuilding` | `1` | 0 | `presets.cpp:48` (field 6) |
| | `usePCTimecyc` | `1` | 0 | PC build runs the PC timecyc (`postfx.cpp:1578`); aap key default 0 (`backup_original/src/main.cpp:1373`) → **TBD-probe** for the exact aap parity value |
| | `NEW legacyAmbient` | `1` | 0 | L §2.1 P9 / §3 #9 |
| Vehicle | `dualPassVehicle` | `1` | 0 | `presets.cpp:48` |
| | `envMapSize` | `256` | 512 | aap `:1315` |
| | `envMapFarClipMult` | `1.0` | 2.0 | aap `:1319` |
| PostFX | `radiosity` / `doRadiosity` | `1` / `1` | 1/* | `presets.cpp:48` (fields 7-8) |
| | `grainFilter` | `1` (PC grain) | 1 | `presets.cpp:48` (field 12) |
| | `infraredVision` / `nightVision` | `0` / `0` | 1/1 | `presets.cpp:48` |
| | `ssaoEnable` / `smaaEnable` | `0` / `0` | 0/0 | `presets.cpp:48` |
| | `motionBlurEnable` | `0` | 0 | feature absent from the original PC code |
| | `sssPostProcessEnable`/`skin`/`hair`/`veg` | `0` | 1 | `main.cpp:4936-4939` |
| | `vcsTrails` | `0` | 0 | `presets.cpp:48` |
| Coronas | `coronaZtest` | `1` | -1 | aap PC path forces the PC z-test (`backup_original/src/main.cpp:340-345`); A §3 A3 |
| Alpha/state | `ps2ModulateGlobal` / `ps2ModulateGrass` | `0` | 0 | `presets.cpp:48` |
| | `dualPassGlobal` / `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `1` | 0 | `presets.cpp:48` |
| | `pedShadows` / `stencilShadows` | `-1` | -1 | `presets.cpp:48` |
| | `pedOpaqueContract` | `0` | 1 | L P4 / §3 #1 |
| | `stateBaselineEnable` | `0` | 1 | L §4 row 2 |
| | `texAlphaClassify` | `0` | 1 | L A5 |
| | `NEW pipeAlphaExactRestore` | `1` | 1 | L A1-A3 / #6 |
| | `NEW alphaTestLegacy` | `1` | 1 | L A3 / `DefinedState` GREATER@2 |
| | `NEW coronaStateShield` | `0` | 0 | L X3 |
| | `NEW pedAmbientOverride` | `0` | 0 | L P5 / §3 #4 |
| | `NEW dualPassDefaultAlphaGate` | `0` (legacy: no hasAlpha gate) | 0 | L P8 / §3 #10 |
| | `NEW dualPassPedZWriteForce` | `0` (legacy: inside the legs only) | 0 | L P3 / §3 #5 |
| FPS/perf | `forwardPlusEnable` | `0` | 1 | legacy path |

> **Why `BUILDING_XBOX` and not a new `BUILDING_PC`:** aap's "PC" building look *is* its Xbox building shader (`backup_original/src/main.cpp:1272-1276`), and `BUILDING_XBOX` in our tree is the port of that shader (`buildingPipe.cpp:1452-1672`). The vehicle difference (`CAR_PC` vs `CAR_XBOX`) is what makes PC vintage distinct. Adding `BUILDING_PC` would require a duplicate callback with no reference implementation.

### 3.4 `MODE_GTAIV` (modern)

Straight lift of `profileGTAIV` (`modeManager.cpp:61-117`), now data. Fields:

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_GTAIV` | PBR | `modeManager.cpp:65` |
| | `buildingPipe` | `BUILDING_GTAIV` | PBR | `:68` |
| | `vehiclePipe` | `CAR_GTAIV` | Modern | `:69` |
| | `colorFilter` | `COLORFILTER_GTAIV` | Modern | `:70` |
| | `preset` | `PRESET_IV_PC` (12) | custom | `:73` |
| | `ivMode` | `1` | 0 | `:102` |
| Building | `ps2ModulateBuilding` | `0` | 0 | `:78` |
| | `detailMaps` / `stochastic` | `0` / `0` | 1/1 | `:109-110` |
| | `dualPassBuilding` | `0` | 0 | `:82` |
| | `NEW legacyAmbient` | `0` | 0 | IV has its own shading path; **TBD-probe** |
| Vehicle | `dualPassVehicle` | `0` | 0 | `:83` |
| | `envMapSize` | `256` | 512 | `:115` |
| | `envMapFarClipMult` | `1.0` | 2.0 | `:116` |
| PostFX | `radiosity` / `doRadiosity` | `0` / `0` | 1/* | `:89-90` |
| | `grainFilter` / `infraredVision` / `nightVision` | `0` / `0` / `0` | 1/1/1 | `:93-95` |
| | `ssaoEnable` / `smaaEnable` | `0` / `0` | 0/0 | `:98-99` |
| | `ivSaturation` / `ivCurves` | `0.0` / `0.0` | 0.3/1.0 | `:105-106` (non-INI experiments) |
| | `ivDesaturation`/`ivGamma`/`ivVignette*`/`ivBloom`/`ivExposure` | **not owned** | — | deliberately left to the user (`:56-60` comment) |
| | `vcsTrails` | `0` | 0 | `iv_mode.cpp:50` |
| Coronas | `coronaZtest` | **TBD-probe** | -1 | no IV source in the four docs; do not invent |
| Alpha/state | `ps2ModulateGlobal` / `ps2ModulateGrass` | `0` | 0 | `:76-78` |
| | `dualPassGlobal` / `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `0` | 0 | `:81-86` |
| | `doglare` / `neoWaterDrops` | `0` / `0` | 0/0 | `:113-114` |
| FPS/perf | `forwardPlusEnable` | **TBD-probe** | 1 | IV path predates Forward+ in this mod |

### 3.5 `MODE_GTAV` (modern, probe-gated)

There is **no GTA V pipe set in the tree**: `Pipeline` stops at `PIPELINE_GTAIV` (`skygfx.h:196-205`) and `profileGtav` writes nothing (`modeManager.cpp:152-162`). The only implemented V-specific renderer is the water shader (`waterStyle=2`, `main.cpp:5682-5687`, `Water_V`). The profile is therefore deliberately minimal and every unimplemented field is `TBD-probe`:

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_PBR` (derived) | PBR | only safe family until a V pipe exists |
| | `buildingPipe` | `TBD-probe` (candidate `BUILDING_PBR`) | PBR | no V building source |
| | `vehiclePipe` | `TBD-probe` (candidate `CAR_MODERN`) | Modern | no V vehicle source |
| | `colorFilter` | `TBD-probe` | Modern | no V filter source |
| | `preset` | `PRESET_CUSTOM` (-1) | custom | no V preset entry (`skygfx.h:216-252`) |
| | `ivMode` | `0` | 0 | V is not IV |
| Water | `waterStyle` | `2` (V) | 0 | `main.cpp:5682-5687`; `Water_V` |
| | `waterQuality` | `3` | 3 | `main.cpp:5689` |
| PostFX | `vcsTrails` / `radiosity` / `grainFilter` / `ssaoEnable` / `smaaEnable` | `TBD-probe` | — | no source |
| Alpha/state | `legacyAmbient` / `alphaTestLegacy` / `pipeAlphaExactRestore` | `0` / `0` / `1` | 0/1/1 | modern family |
| FPS/perf | `forwardPlusEnable` | `TBD-probe` | 1 | no source |

**Probe list for V (must be captured before the mode ships):** a V building shading reference, a V vehicle env/clearcoat reference, a V colour-grade reference, and a V timecycle/fog reference. Until then `MODE_GTAV` must show an INACTIVE-style badge in the UI ("probe-gated: water only") rather than pretending to be a full mode.

### 3.6 `MODE_MODERN` (modern, PBR)

| Subsystem | Field | Value | Default | Source |
|---|---|---|---|---|
| Pipeline | `pipeline` | `PIPELINE_PBR` | PBR | `main.cpp:4889-4893` |
| | `buildingPipe` | `BUILDING_PBR` | PBR | same |
| | `vehiclePipe` | `CAR_MODERN` | Modern | same |
| | `colorFilter` | `COLORFILTER_MODERN` | Modern | forced lock `main.cpp:5286-5294` |
| | `preset` | `PRESET_SKYGFXPLUSULTRAMAXDELUXE` (14) | custom | `presets.cpp:86` |
| | `ivMode` | `0` | 0 | `custom_mode.cpp:131` |
| Building | `ps2ModulateBuilding` | `0` | 0 | `custom_mode.cpp:75` |
| | `detailMaps` / `stochastic` | `1` / `1` | 1/1 | `custom_mode.cpp:142-143`; `main.cpp:5042-5043` |
| | `dualPassBuilding` | `1` | 0 | `main.cpp:5044` |
| | `usePCTimecyc` | `1` | 0 | PBR path (`postfx.cpp:1578` `PIPELINE_PBR`) |
| | `NEW legacyAmbient` | `0` | 0 | PBR keeps the night law (`pipelinecommon.cpp:97-188`) |
| Vehicle | `dualPassVehicle` | `1` | 0 | `main.cpp:5045` |
| | `envMapSize` | `512` | 512 | `main.cpp:5060` |
| | `envMapFarClipMult` | `2.0` | 2.0 | `main.cpp:5061` |
| | `envShininessMult` / `envSpecularityMult` / `envPower` / `envFresnel` | `1.0` / `1.0` / `128.0` / `0.4` | same | `main.cpp:5062-5068` |
| | `vehEnvIntensity` | `1.0` (clamped to 0.4) | 1.0 | `custom_mode.cpp:155`, clamp `main.cpp:5156-5160` |
| | `vehPBRLayers` | `255` | 255 | `main.cpp:5155` |
| PostFX | `radiosity` / `doRadiosity` | `1` / `1` | 1/* | `main.cpp` ULTRA + `custom_mode.cpp:84-85` |
| | `grainFilter` | `1` | 1 | `custom_mode.cpp:87` |
| | `infraredVision` / `nightVision` | `0` / `0` | 1/1 | `custom_mode.cpp:88-89` |
| | `ssaoEnable` | `1` (`ssaoRadius=1.0`, `ssaoPower=2.0`, `ssaoKernelSize=16`, `ssaoSampleCount=16`) | 0 | `main.cpp:5017-5021`; `custom_mode.cpp:91-95` |
| | `smaaEnable` | `1` | 0 | `main.cpp:5016` |
| | `motionBlurEnable` | `1` (`strength .4`, `radial .2`, `speedFactor .3`, `cameraAware 1`) | 0 | `main.cpp:5022-5026` |
| | `sssPostProcessEnable` | `1` (`strength .2`, `radius 3.0`) | 1 | `main.cpp:5027-5029` |
| | `skinEnhanceEnable` / `hairEnhanceEnable` / `vegetationEnhanceEnable` | `1` / `1` / `1` | 1 | `main.cpp:5030-5041` |
| | `ivDesaturation`/`ivGamma`/`ivSaturation`/`ivCurves`/`ivVignette*`/`ivBloom`/`ivExposure` | `0.15/1.0/0.3/1.0/0.15/0.70/1.5/0.05/2.5` | same | `main.cpp:5051-5059`; `custom_mode.cpp:132-140` |
| | `tonemapAutoExposure`/`AdaptSpeed`/`KeyStrength`/`Min`/`Max` | `1/0.12/1.0/0.5/1.0` | same | `custom_mode.cpp:158-164`; `main.cpp:5171-5177` |
| | `pbrIblAmbientWeight` | `0.5` | 0.5 | `custom_mode.cpp:156` |
| | `vehEnvGlint` / `vehChromeEnvThreshold` | `0.0` / `0.0` | 0/0 | `custom_mode.cpp:157`; `main.cpp:5161` |
| | `vcsTrails` | `0` | 0 | `custom_mode.cpp:55` |
| Coronas | `coronaZtest` | `1` | -1 | modern path uses the z-tested coronas |
| Alpha/state | `ps2ModulateGlobal` / `ps2ModulateGrass` | `0` | 0 | `custom_mode.cpp:74-76` |
| | `dualPassGlobal` / `dualPassPed` / `dualPassGrass` / `dualPassDefault` | `1` | 0 | `main.cpp:5044-5048` |
| | `stateBaselineEnable` / `texAlphaClassify` / `pedOpaqueContract` / `defaultCbShaderHygiene` | `1/1/1/1` | same | `main.cpp:5415-5424` |
| | `NEW legacyAmbient` / `alphaTestLegacy` / `coronaStateShield` | `0` / `0` / `0` | 0/1/0 | PBR keeps the modern state model |
| FPS/perf | `forwardPlusEnable` | `1` | 1 | `main.cpp:5590` |
| | `pipeChainEnable` / `normalBufferEnable` / `velocityBufferEnable` / `depthHookEnable` | `0`/`1`/`1`/`1` | same | `main.cpp:5394-5407, 5400` |
| | `grassInstancing` / `treeWind` / `treeFx` | `0`/`0`/`0` | same | `main.cpp:5645-5662` |

### 3.7 `MODE_CUSTOM`

No profile. `ApplyMode(MODE_CUSTOM)` applies zero owned fields; the base config plus `[Mode.Custom]` overrides is the live config. This is the mode that must feel exactly like today's `MODE_NONE`, but with persistence and per-field reset.

---

## 4. Legacy PC feature completeness

Everything below is present in `pipes/PC` and/or aap's legacy PC/Neo paths but **cannot be expressed** by today's `Config`. Fields are append-only (§6). Priority: **P0** = required for "legacy PC is a full restore"; **P1** = visible parity; **P2** = beta/experimental (unshipped but requested).

### 4.1 Pipe/render features → new fields

| # | P | Feature (evidence) | Today | New field / INI key | Type / default | Consumer |
|---|---|---|---|---|---|---|
| L1 | P0 | **Stochastic without detail map** — aap loads `simpleStochasticPS` (`backup_original/src/pipelinecommon.cpp:379`) and selects it at `buildingPipe.cpp:310-315` (PS2) / `:491-496` (Xbox); our `buildingPipe_selectPS` falls through to `simplePS` (`buildingPipe.cpp:949-958`) | CSO exists (`resources/cso/simpleStochasticPS.cso`), never bound | `int simpleStochasticFallback` / **`simpleStochasticPS`** | int 0/1, default **1** | `pipelinecommon.cpp` shader list (`makePS`), `resources/resource.h` (`IDR_SIMPLESTOCHASTICPS`, free id 278), `resource.rc`, `buildingPipe_selectPS` |
| L2 | P0 | **Detail maps** — `detailMaps` + per-tex `detailnum`/`detailtile` (`src/skygfx.h:1091-1094`) exist; the `tile = detailtile/10.0f` divisor is hardcoded (aap `buildingPipe.cpp:283`) | partially expressible | `float detailTileScale` / **`detailTileScale`** | float, default `1.0` (1.0 = aap's /10) | `buildingPipe.cpp` detail branches |
| L3 | P0 | **DN day/night vertex colours** — PC CPU path interpolates `preLitLum` over 16 frames with 0.01/0.3 thresholds (`pipes/PC/CustomBuildingDNPipeline.cpp:68, 275-318, 1497-1575`); aap uses the same via `setDnParams`/`DNInstance_PS2` (`backup_original/src/buildingPipe.cpp:103-130, 667-842`) | no gate at all | `int dnVertexColors` / **`dnVertexColors`** | int 0/1, default **1** (vintage) | `buildingPipe.cpp` `setDnParams`/DN instance |
| | | frame stride | hardcoded 16 | `int dnUpdateFrames` / **`dnUpdateFrames`** | int, default `16` | DN scheduler |
| | | update thresholds | hardcoded | `float dnUpdateEps` / **`dnUpdateEps`**, `float dnBigUpdateEps` / **`dnBigUpdateEps`** | float `0.01` / `0.3` | DN scheduler |
| | | dynamic prelit usage | unused | `int dnDynamicPrelit` / **`dnDynamicPrelit`** | int, default `1` (`rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT`, `pipes/PC/CustomBuildingDNPipeline.cpp:240-244`) | DN atomic setup |
| L4 | P1 | **DN implementation selector** — PC shipped CPU (`GTA_PC_USE_DN_VS` off, `pipes/PC/CustomBuildingDNPipeline.cpp:48-51`), PC beta VS (`DNshaderPC.vsh`), Xbox normal-packed VS (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112`) | ours always builds the two-stream COLOR0/COLOR1 path (`buildingPipe.cpp:2714-2725`) | `int dnMode` / **`dnMode`** | int 0=CPU,1=VS,2=Xbox-pack; default `0` | DN instance/VS |
| L5 | P1 | **Env-map 1 source** — original uses clump LTM if present else atomic LTM (`pipes/PC/CustomCarEnvMapPipeline.cpp:790-829`) | ours uses a fixed matrix | `int vehEnv1ClumpMatrix` / **`vehEnv1ClumpMatrix`** | int 0/1, default `1` | `vehiclePipe.cpp` `SetupEnv` |
| L6 | P1 | **Env2 scroll** — sector parity, U accumulation, V clamp `[0,0.1]`, flip when upside down (`pipes/PC/CustomCarEnvMapPipeline.cpp:930-1073`) | `Env2Xform_PC` approximates it (`vehiclePipe.cpp:424`) | `int vehEnv2Scroll` / **`vehEnv2Scroll`**, `float vehEnv2VClamp` / **`vehEnv2VClamp`** | int `1`; float `0.1` | `Env2Xform_PC` |
| L7 | P1 | **Env2 by texture name** — first char `'x'` selects env2 (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:367-371`) | name rule not gated | `int vehEnv2ByName` / **`vehEnv2ByName`** | int 0/1, default `1` | vehicle material setup |
| L8 | P1 | **Specular pass** — D3D light index 1, `LOCALVIEWER=FALSE`, `SPECULARMATERIALSOURCE=MATERIAL`, `min(2*spec*mult,1)`, power `100*spec` (`pipes/PC/CustomCarEnvMapPipeline.cpp:42, 1361-1375, 1567-1573`) | `envSpecularityMult` only; index/power law hardcoded | `int vehSpecularPass` / **`vehSpecularPass`**, `int vehSpecLightIndex` / **`vehSpecLightIndex`**, `float vehSpecPowerScale` / **`vehSpecPowerScale`**, `float vehSpecSpecularScale` / **`vehSpecSpecularScale`** | int `1`/`1`, float `100.0`/`2.0` | `SetupSpec` / all spec branches |
| L9 | P1 | **Vehicle remap-colour fix** — zero `MAT1..MAT4` and `LIGHT_*` before surface properties (`pipes/PC/CustomCarEnvMapPipeline.cpp:1513-1534`) | absent (`grep MAT1 src/render/vehiclePipe.cpp` = 0) | `int vehRemapFix` / **`vehRemapFix`** | int 0/1, default `1` | `SetupMaterial*` |
| L10 | P1 | **Blown-up vehicles** — detect `!(pDirect->flags & rpLIGHTLIGHTATOMICS)` + atomic id bit `1<<14`, suppress env/spec (`pipes/PC/CustomCarEnvMapPipeline.cpp:1253-1275, 1320-1337`) | partial (`vehiclePipe.cpp:1423-1425`) | `int vehBlownUpDetect` / **`vehBlownUpDetect`**, `int vehNoExtraPassGate` / **`vehNoExtraPassGate`** | int 0/1, default `1` | vehicle cbs |
| L11 | P2 | **Device caps gate** — multitexture / `BLENDFACTORALPHA` / `MULTIPLYADD` / specular shade (`pipes/PC/CustomCarEnvMapPipeline.cpp:1690-1724`) | no gate | `int vehDeviceCapsGate` / **`vehDeviceCapsGate`** | int 0/1, default `1` (startup) | `CCustomCarEnvMapPipeline__Init` (`vehiclePipe.cpp:182`) |
| L12 | P1 | **Building env stage via texgen** — `D3DTSS_TCI_CAMERASPACENORMAL`, `TEXTURETRANSFORMFLAGS=DISABLE`, matrix computed but never uploaded (`pipes/PC/CustomBuildingPipeline.cpp:828-861`) | our PS2/Xbox env branch uses a transform (`buildingPipe.cpp:1256-1290, 1624-1650`) | `int bldEnvTexGen` / **`bldEnvTexGen`** | int 0/1, default `1` | building PS2/Xbox cbs |
| L13 | P2 | **Building env texture matrix** (computed, never uploaded → beta) | absent | `int bldEnvTexMatrix` / **`bldEnvTexMatrix`** | int 0/1, default `0` | building env branch |
| L14 | P2 | **DN lighting callback** (`CustomPipeLightingCB` defined, never attached — `pipes/PC/CustomBuildingDNPipeline.cpp:990-1045, 1413`) | absent | `int bldLightingCB` / **`bldLightingCB`** | int 0/1, default `0` | pipe creation |
| L15 | P1 | **PS2 modulate strength** — `colorScale = 255/128` hardcoded (aap `buildingPipe.cpp:234`) | hardcoded | `float ps2ModulateStrength` / **`ps2ModulateStrength`** | float, default `1.9921875` | PS2 building cb |
| L16 | P2 | **Stochastic scale** | shader constant only | `float stochasticScale` / **`stochasticScale`** | float, default `1.0` — **TBD-probe** | building PS selection |
| L17 | P1 | **Neo two-pass** — `CarPipe` pass1/pass2 + `RenderEnvTex` (aap `neoCarpipe.cpp:206-223, 296-463`); our `CreateShaders` nulls both VS (`neoCarpipe.cpp:475-486`) and dispatch reroutes `CAR_NEO` (`vehiclePipe.cpp:3215-3232`) | cannot render Neo | `int neoTwoPass` / **`neoTwoPass`** (+ restore `IDR_NEOVEHICLEONEVS/TWOVS` + RC) | int 0/1, default `0` | `neoCarpipe.cpp`, `vehiclePipe` dispatch |
| L18 | P2 | **SMAA predication / temporal / preset** — aap keys (`backup_original/src/main.cpp:1443-1445`); our fields are reserved padding (`skygfx.h:411-415`) | not expressible | reuse `_reserved_smaaPreset`/`_reserved_smaaPredication`/`_reserved_smaaTemporal` under keys **`smaaPreset`/`smaaPredication`/`smaaTemporal`** | int, aap defaults `2`/`0`/`0` | SMAA passes |

### 4.2 Alpha-test / dual-pass / state behaviour

| # | P | Feature | Today | New field / INI key | Default | Consumer |
|---|---|---|---|---|---|---|
| S1 | P0 | **Legacy ambient source** — buildings `CTimeCycle * CCoronas::LightsMult`, vehicles/peds `pAmbient->color`; **no** sky clamp (`backup_original/src/buildingPipe.cpp:57-59`, `vehiclePipe.cpp:348, 816`; vanilla `app_light.cpp:128-134` per `pipelinecommon.cpp:145-148`) | `ClampAmbientToSky` unconditional (`pipelinecommon.cpp:97-188, 211`) | `int legacyAmbient` / **`legacyAmbient`** | int 0/1, default `0`, **1 in vintage** | `GetTimecycleAmbient`, both vehicle `uploadLights` |
| S2 | P0 | **Ped `pAmbient` override** — legacy uses the game's light (`backup_original/src/main.cpp:536-537` returns before the 1.5× block); ours replaces it with timecycle ambient + 0.20 floor (`main.cpp:1582-1662`) | unconditional | `int pedAmbientOverride` / **`pedAmbientOverride`** | int, default `0` (gate off) | `myDefaultCallback` |
| S3 | P0 | **Exact alpha/blend restore** — capture/restore `ALPHABLENDENABLE`, `ALPHATESTENABLE`, `BLENDOP`; legacy never wrote them (L A1-A3, §3 #6) | exit derives blend from `vtxAlpha`, forces test on, forces `BLENDOP=ADD` (`pipelinecommon.cpp:1643-1686`) | `int pipeAlphaExactRestore` / **`pipeAlphaExactRestore`** (+ 3 fields in `PipeAlphaState`, `skygfx.h:1339-1357`) | int 0/1, default `1` | `pipeEnter/ExitAlphaMode` |
| S4 | P0 | **Legacy alpha-test doctrine** — enable/func/ref owned by the exe/RW; `DefinedState` sets GREATER@2 (`handy.cpp:116-138`); no pipe writes (L A3) | entry forces test on and ref≠0; exit forces test on (`pipelinecommon.cpp:1545, 1654, 1686`) | `int alphaTestLegacy` / **`alphaTestLegacy`**, `int alphaTestRef` / **`alphaTestRef`**, `int alphaTestFunc` / **`alphaTestFunc`** | int `0/1` (`1` vintage), `2`, `GREATER` | pipe alpha contract |
| S5 | P1 | **Dual-pass default gate** — legacy gate is only `zwrite && dualPassDefault`, no `hasAlpha` (`backup_original/src/main.cpp:416-421`); ours adds an alpha probe (L P8) | extra gate `main.cpp:1678-1687` | `int dualPassDefaultAlphaGate` / **`dualPassDefaultAlphaGate`** | int, default `0` = legacy semantic | `myDefaultCallback` |
| S6 | P1 | **Ped ZWRITE force** — legacy forces ZWRITE only inside the skin dual pass (`backup_original/src/main.cpp:425`); ours forces it for every ped atomic (L P3) | `main.cpp:1475-1488, 1776-1788` | `int dualPassPedZWriteForce` / **`dualPassPedZWriteForce`** | int, default `0` = legacy | ped contract |
| S7 | P1 | **Default-pipe contract** — legacy never forced blend/alpha-test (`backup_original/src/main.cpp:224-225` read-only) | forces the full canonical block `main.cpp:1369-1391` | `int defaultPipeContract` / **`defaultPipeContract`** | int, default `0` = legacy (keep depth repair) | default branch |
| S8 | P1 | **Corona state shield** — legacy had only the z-test patch (`backup_original/src/main.cpp:330-346`); our shield forces 8 states on every sprite flush (`main.cpp:2368-2432, 2541`) | unconditional | `int coronaStateShield` / **`coronaStateShield`** | int, default `0` | `coronaDrawRawShield` |
| S9 | P1 | **`tagsBuildingPipe`** — aap branches on it (`backup_original/src/buildingPipe.cpp:151`); ours on `buildingPipe` (`buildingPipe.cpp:325`), field written once (`main.cpp:5340`) and never saved | dead | **`tagsBuildingPipe`** (existing key) + `int tagsBuildingPipeExplicit` / **`tagsBuildingPipeExplicit`** | int, default `0` (mirror `buildingPipe`) | `TagRenderCB`, `saveConfigTo` |
| S10 | P2 | **Startup-only aap switches as `Config` fields** — `explicitBuildingPipe`, `forceWindShader`, `fixPcCarLight`, `disableClouds`, `disableGamma`, `transparentLockon`, `fixShadows`, `privateHooks` (`backup_original/src/main.cpp:1457-1458`; ours globals `main.cpp:55-71`) | globals, not per-config | eight `int` fields with the same names (plus the existing globals become aliases) | current defaults | `InjectDelayedPatches` |

### 4.3 Beta / unshipped features to expose (explicitly requested)

| Feature | Evidence | Exposure |
|---|---|---|
| PC DN vertex shader (`GTA_PC_USE_DN_VS`) | compiled out at `pipes/PC/CustomBuildingDNPipeline.cpp:48-51`; shader source `PC/DNshaderPC.vsh` | `dnMode=1` (L4) |
| PC custom instance callback (building) | `#if 0`, `pipes/PC/CustomBuildingPipeline.cpp:164-384`; not attached at `:942` | `int bldCustomInstanceCB` (P2, default 0) |
| PC custom instance callback (car) | `#if 0`, `pipes/PC/CustomCarEnvMapPipeline.cpp:200-421`; not attached at `:1627` | `int vehCustomInstanceCB` (P2, default 0) |
| Building env texture matrix upload | computed `pipes/PC/CustomBuildingPipeline.cpp:830-834`, never uploaded | `bldEnvTexMatrix` (L13) |
| DN lighting callback | defined `pipes/PC/CustomBuildingDNPipeline.cpp:990-1045`, not attached | `bldLightingCB` (L14) |
| Xbox normal-packed DN | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:103-111` | `dnMode=2` (L4) |
| Neon/other dead paths | `#if 0` blocks in the tree | **not exposed** (explicit non-goal, P §5.1) |

---

## 5. Custom mode — how user tweaks survive

### 5.1 Save/restore model

```
s_baseConfig[slot]     // the config the user had before the first mode in this run
s_mode                 // active ModeId
g_modeOverrides[8]     // per-mode sparse diff: { field, value } list
```

* **Enter** `MODE_NONE → X`: snapshot `*config` into `s_baseConfig[slot]`; `*config = base + profile(X) + overrides(X)`.
* **Switch** `X → Y`: persist X's overrides; rebuild `*config` from the **same base** (never stack, today's invariant `modeManager.cpp:284-286` kept) + profile(Y) + overrides(Y).
* **Toggle off** `X → MODE_NONE`: restore the base verbatim (today's behaviour), keeping the overrides on disk so re-entering X restores the user's look.
* **Edit while X is active**: write the live `Config`, and insert/replace `(field, value)` in `overrides(X)` — a diff against `profile(X)`, so untouched fields keep following the profile if it is ever revised.
* **Edit while `MODE_NONE`/`MODE_CUSTOM`**: write live `Config` only; no override record.
* **Config-slot switch**: each slot has its own base + its own override set keyed by `(slot, mode)`. (Fixes the current single global snapshot, §1.4 #8.)

### 5.2 UI affordances

The badge machinery already exists: `DM_BADGE_OVERRIDDEN`, `DM_BADGE_INACTIVE`, `DM_BADGE_WARN`, `DM_BADGE_RESTART`, `DM_BADGE_NEW` (`debugmenu_ui.cpp:2530-2539, 2577-2587`). Add three behaviours, no new widget types:

| State | Badge | Row behaviour | Tooltip |
|---|---|---|---|
| Field is owned by the active mode, value == profile | *(none)*, plus a small "MODE" tag in the label | editable; an edit creates an override | "Set by mode **PS2**. Edit to override for this mode." |
| Field is owned by the active mode, value ≠ profile (override exists) | `OVERRIDDEN` (existing pill, new reason path) | editable; right-click → **"Reset to mode default (…)"** | "Overridden in mode **PS2** (profile value X)." |
| Field is not owned by the active mode | `INACTIVE` (existing pill) | editable; the value is stored but has no effect until a mode that owns it is selected | "Mode **PS2** does not use this field; the value is kept for other modes." |
| Hard lock (PBR→Modern filter) | `OVERRIDDEN` | control disabled (existing behaviour `debugmenu_ui.cpp:2517-2521`) | existing reason text `:1861-1862` |

Implementation points:
* Extend `RowHardOverride()` (`debugmenu_ui.cpp:1843-1866`) with a mode-ownership branch (the function already returns "effective value + reason").
* Extend `RowResetPopup()` (`:2026-2052`) with a second menu item "Reset to mode default" next to the existing "Reset to default".
* The Modes section (`:2686-2711`) shows the 6+Custom picker with a "Custom" row, the six mode rows, and the legacy Mobile row hidden behind a "show legacy" toggle. Each row's tooltip lists the profile's owned-field count and any override count.
* A "Reset all overrides for this mode" button next to the picker.

### 5.3 Persistence format

Same `skygfx.ini` file (the writer already merges unknown keys: `main.cpp:6095`), new sections only — **no existing key is renamed or removed**:

```ini
[Modes]
active            = PS2        ; none|PS2|Xbox|PC|GTAIV|GTAV|Modern|Custom
schema            = 1
qualityPresetPipes = 0         ; 1 = legacy tier-0/1/2 pipe writes at load

[Mode.PS2]
colorFilter       = VCS        ; sparse: only fields the user changed vs profile
dualPassGlobal    = 1
envMapSize        = 512

[Mode.Custom]
; empty by default; everything comes from top-level keys
```

Rules:
* Only the **diff vs the profile** is written (`Reset to mode default` deletes the line).
* `active=none` or a missing section reproduces today's behaviour bit-for-bit (top-level keys only).
* The reader runs **after** the top-level keys, so unowned fields always come from the user's normal config (precedence §2.3).
* Save writes the top-level key for every field **as well**, so dropping to `MODE_NONE` keeps the last look.

---

## 6. Implementation order, tests, invariants

### 6.1 Phases (each independently shippable)

| Phase | Work | Files | Risk |
|---|---|---|---|
| **P0 — instrumentation** | Log every mode apply (`owns` count, resulting pipes/filter/knobs); log the "top-level owned key ignored" event; log `simpleStochasticPS` bind. No behaviour change. | `modeManager.cpp`, `buildingPipe.cpp` | none |
| **P1 — canonical model** | `ModeField`/`ModeValues`/`ModeProfile` + `g_modeProfiles[6]`; `ApplyMode`; per-slot base snapshot; listener registered in `InjectDelayedPatches`; delete `profilePS2/Xbox/Mobile/Gtav` and the six `*_mode.cpp` files. | `skygfx.h`, `modeManager.{h,cpp}`, `main.cpp:6756`, `premake5.lua` (glob auto), `debugmenu_ui.cpp` | **medium** — pipe/filter changes are user-visible; validate with §6.2 |
| **P2 — stop the fighting** | E1-E4: remove rule-3 pipe rewrite, qualityPreset pipe writes (behind `qualityPresetPipes`), `pipelineOverride`/`ApplyPipelineFromMenu` pipe writes, silent latch clear in `ApplyPreset`. | `main.cpp:663-675, 4930-4980, 4883-4885`, `debugmenu_ui.cpp:738-790`, `presets.cpp:120-121` | **medium** — changes existing INI outcomes; keep the back-compat gate |
| **P3 — overrides + persistence** | `[Modes]`/`[Mode.*]` reader/writer; sparse diff; badges + reset-to-mode-default; cfg-slot keying. | `main.cpp:readIniFile/saveConfigTo`, `modeManager.cpp`, `debugmenu_ui.cpp` | **low-medium** |
| **P4 — vintage parity P0** | L1, L2, L3, S1, S2, S3, S4, S9; `legacyAmbient` wiring; alpha-state capture; `tagsBuildingPipe` real. | `pipelinecommon.cpp`, `buildingPipe.cpp`, `main.cpp`, `resources/resource.{h,rc}`, `vehiclePipe.cpp` | **high** — shared lighting + alpha contract; test at 4 times of day per A E4 |
| **P5 — PC vintage completeness P1** | L4-L10, L12, L15, S5-S8, L17 decision. | `vehiclePipe.cpp`, `buildingPipe.cpp`, `neoCarpipe.cpp`, `skygfx.h`, `main.cpp` | **high** — vehicle env/spec rewrite; mirror + garage scenes |
| **P6 — beta + V probe** | L11, L13, L14, L16, S10, L18; `MODE_GTAV` probe mode. | `skygfx.h`, `main.cpp`, `buildingPipe.cpp`, `vehiclePipe.cpp`, `postfx*` | **low-medium** (mostly default-off) |
| **P7 — cleanup** | Delete dead globals replaced by `Config` fields; delete stale doc citations; drop duplicate shader sources (A D1) | `main.cpp`, `shaders/vs/{xbox,ps2}/*` | **low** |

Rollback per phase: keep the old profile numbers in a `g_legacyPresetCompat` table so `active=none` reproduces today's exact behaviour.

### 6.2 Test matrix

Per mode, on a fixed save/time/weather, captured from the same spot:

| Test | PS2 | Xbox | PC vintage | GTA IV | GTA V | Modern |
|---|---|---|---|---|---|---|
| Pipes after apply (log + menu) | B0/V0 | B1/V2 | B1/V1 | B2/V9 | TBD | B3/V10 |
| Colour filter actually used | PS2 | PC | PC | GTAIV | TBD | Modern (locked) |
| Timecycle source | PS2 | PC | PC | PC | TBD | PC |
| Building: detail on/off | off | on | on | off | TBD | on |
| Building: stochastic w/o detail binds `simpleStochasticPS` | n/a (off) | **must** | **must** | off | TBD | n/a |
| Building: DN day/night at 01:28 / 06:30 / 12:00 / 20:30 | on | on | on | off | TBD | off |
| Vehicle: env2 scroll both directions + sector crossing | n/a | on | on | TBD | TBD | n/a |
| Vehicle: spec highlight in shade | on | on | on | TBD | TBD | on |
| Vehicle: remap-coloured paint | correct | correct | correct | TBD | TBD | correct |
| Dual pass: decals/foliage fringe | off | on | on | off | TBD | on |
| Alpha-test: cutout props after pipe exit | restored | restored | restored | restored | TBD | canonical |
| Ped alpha (fading/cutscene) | legacy | legacy | legacy | modern | TBD | modern |
| Interior lighting (garage, cutscene) | legacy ambient | legacy ambient | legacy ambient | modern | TBD | modern |
| Coronas (z-test, additive) at night | off | on | on | TBD | TBD | on |
| Reflection resolution | 256 | 256 | 256 | 256 | TBD | 512 |
| Mode survives restart | yes | yes | yes | yes | yes | yes |
| Tweak survives mode switch A→B→A | yes | yes | yes | yes | yes | yes |
| INI reload (F11) keeps mode | yes | yes | yes | yes | yes | yes |
| Config-slot switch keeps per-slot mode | yes | yes | yes | yes | yes | yes |
| No `[Config] pipeline re-derived` line after an apply | yes | yes | yes | yes | yes | yes |
| `[BPIPE] light`/`[VEHICLE]` state probes quiet for 10 min | yes | yes | yes | yes | yes | yes |

Scene list (from L appendix): daylight CJ close-up + fade, night street lights, docks/beach outdoor ref=140, mirror + stencil shadows, transparent building + road decals, vehicle glass/paint/grunge, prop/pickup/marker.

### 6.3 Must NOT change

1. **`Config` layout**: append-only. `version` at offset 0, `fixGrassPlacement` at 8, `doglare` at 12 are asm-read (`skygfx.h:881-883`; `main.cpp` `fixSeed`/`doglare` hooks). Do not bump `VERSION` (`skygfx.h:163`; external modules read it at offset 0).
2. **Existing INI keys**: names, spellings and acceptance of legacy aliases. Adding `PC`→`BUILDING_XBOX` and `Spec`→`CAR_SPEC` is a *restoration*, not a change (A D6, `main.cpp:5078-5102`).
3. **`GamePreset` numeric values** (`PRESET_III_PS2`=0 … `PRESET_SKYGFXPLUSULTRAMAXDELUXE`=14, `skygfx.h:216-252`): `gamePreset` is persisted as an int (`main.cpp:5587, 6420`).
4. **`IDR_*` numeric values and CSO file names** (`resources/resource.h`, `resources/resource.rc`). `IDR_SIMPLESTOCHASTICPS` takes 278 (documented free at `resource.h:134`).
5. **Hook addresses**: `0x704D1E`, `0x53EBE9`, `0x5D7100`, `0x5D9FE9` (AGENTS.md), plus every address in `main.cpp`'s hook block (`:6892-6950`).
6. **Shader register contracts**: VS c0-c3 WVP, c4 ambient, c5-c11 direct colour, c12-c18 direct dir, c19 matCol, c22/c23 **only via `pipeUploadPBR()`**, c24 ambient, c30/c31 day/night, c32-c35 texture transform, c36 eye, c40 envtexmat (AGENTS.md + `skygfx.h:1288-1301`). No mode may renumber registers.
7. **`pipeUploadPBR()` call sites/order** (`{glossiness, specular, …}`) — AGENTS.md hard rule.
8. **Pipe creation IDs and plugin offsets**: `PDSPC_*`, `rwCUSTENVMAP*`, extra-vert-colour offset (`0x8D12BC`, `src/rw/gta.cpp:62`).
9. **`ModeManager` public API semantics** for `SetMode` toggle-off (restores the pre-mode config) and `ClearLatch` (does not restore) — code outside this design depends on them (`presets.cpp:120-121`, `menu_inject.cpp:371`, `debugmenu_ui.cpp:823-828, 842`).
10. **`D3D9RenderDual` threshold clamp 254** and the two-layer alpha/Z helpers (`main.cpp:208-259`) — shared by building/vehicle/ped/default.
11. **Shader SM3.0 only** (`ps_3_0`/`vs_3_0`), `Resource.rc` embedding, `tools/fast_build.py` multi-entry list.

---

## 7. Condensed summary (12 bullets)

1. **Three overlapping preset systems exist**: `PresetConfig`/`ApplyPreset` (`presets.cpp:25-155`), `ModeManager` profiles (`modeManager.cpp:61-177`), and the load-time `qualityPreset`+`pipeline` block that writes pipes (`main.cpp:4888-4914, 4930-4980`). A fourth, `src/render/*_mode.cpp`, is **dead** (no call sites; declarations only at `skygfx.h:1426-1444`).
2. **Selecting PS2/Xbox today writes only `pipeline`/`buildingPipe`/`vehiclePipe`** (`modeManager.cpp:125-141`); `colorFilter`, `ps2Modulate`, `dualPass`, `detailMaps`, `stochastic`, `grainFilter`, `coronaZtest`, `envMapSize` all stay on the previous (usually PBR/ULTRA) values, and **GTA V writes nothing** (`:152-162`).
3. **No persistence and no listener**: no mode INI key exists, and `ModeManager_RegisterListener` has no call site, so only the ImGui rows push state (`debugmenu_ui.cpp:2700-2706`); a reload or `setConfig()` leaves the mode lying (`modeManager.cpp:23` is a single global snapshot, `setConfig` never consults it).
4. **Canonical model**: one `ModeId` (`PS2, XBOX, PC_VINTAGE, GTAIV, GTAV, MODERN, CUSTOM` + `NONE` + legacy `MOBILE`) and one `ModeProfile { id, name, iniAlias, owns[2], ModeValues }`; exactly **one** `ApplyMode()` path, with a single listener that runs `resetValues()`, the tags-pipe mirror, a conditional env-map raster rebuild and one log line.
5. **Ownership contract**: `buildingPipe`/`vehiclePipe` are authoritative (never re-derived); `pipeline` is a derived 5-value compat mirror; `colorFilter` is authoritative except the existing PBR→Modern lock; `tagsBuildingPipe` mirrors `buildingPipe` unless explicitly set. Everything else is mode-owned or user-owned, never silently rewritten.
6. **Precedence is a total order**: profile defaults < top-level INI keys (unowned fields only) < `[Mode.<name>]` overrides < live edit. While a mode is active, top-level keys for *owned* fields are ignored with one log line — this is the "no silent re-derivation" rule. `qualityPreset`, `pipelineOverride`, the pipeline combo and `ApplyPreset` must stop writing pipes (E1-E4).
7. **Per-mode tables** give every owned field with exact values and citations: PS2 (filter PS2, ps2Modulate 1, dualPass 0, detail/stochastic 0, grain 0, coronaZtest 0, envMapSize 256 — *not* the dead file's 128), Xbox (filter PC, ps2Modulate 0, dualPass 1, detail 1, **stochastic 1** — overriding `xbox_mode.cpp:94`, coronaZtest 1, envMapSize 256), Legacy PC (`BUILDING_XBOX` + `CAR_PC`, grain 1, detail 1, stochastic 1, coronaZtest 1), GTA IV (`profileGTAIV` lifted verbatim), Modern (ULTRA + `custom_mode.cpp`), GTA V (water-only, everything else **TBD-probe**).
8. **Legacy PC is not a full restore today**: `simpleStochasticPS` is unbound (`buildingPipe.cpp:949-958`), DN day/night has no gate or scheduler knobs, env2 scroll and the PC specular/remap rules are approximated, `tagsBuildingPipe` is dead, and the legacy ambient source, ped-ambient override, exact alpha/blend restore, legacy alpha-test doctrine, dual-pass gates and corona shield are all missing from `Config`.
9. **~35 new append-only `Config` fields + INI keys** are specified (L1-L18, S1-S10): `legacyAmbient`, `pedAmbientOverride`, `pipeAlphaExactRestore`, `alphaTestLegacy/Ref/Func`, `dnVertexColors`/`dnMode`/`dnUpdateFrames`/`dnUpdateEps`/`dnBigUpdateEps`/`dnDynamicPrelit`, `vehEnv2Scroll`/`vehEnv2VClamp`, `vehSpecularPass`/`vehSpecLightIndex`/`vehSpecPowerScale`, `vehRemapFix`, `bldEnvTexGen`, `simpleStochasticFallback`, `dualPassDefaultAlphaGate`, `dualPassPedZWriteForce`, `coronaStateShield`, `tagsBuildingPipeExplicit`, plus the beta toggles (`bldEnvTexMatrix`, `bldLightingCB`, `dnMode=1`, custom instance CBs) and the eight aap startup switches promoted from globals.
10. **Custom mode persists per-mode overrides** as a sparse diff in `[Mode.<name>]` with `[Modes] active=…`; entering a mode rebuilds from the per-config-slot base + profile + overrides, so A→B→A returns the user's tweaks, and `active=none` is bit-identical to today.
11. **UI reuses the existing badge system** (`OVERRIDDEN`/`INACTIVE`, `debugmenu_ui.cpp:2530-2539`): owned-vs-profile rows get a MODE tag, overridden rows an `OVERRIDDEN` pill with "Reset to mode default", unowned rows `INACTIVE`; `RowHardOverride`/`RowResetPopup` gain the mode branches.
12. **Ship in 8 phases** (instrumentation → canonical model → stop-the-fighting → overrides/persistence → vintage P0 → PC P1 → beta + V probe → cleanup); verify per-mode with the matrix in §6.2; invariants that must not change: `Config` layout (append-only, `VERSION` unchanged), existing INI keys, `GamePreset` numeric values, `IDR_*`/CSO names (new id 278), hook addresses, SM3.0 and the shader register/`pipeUploadPBR` contracts.
