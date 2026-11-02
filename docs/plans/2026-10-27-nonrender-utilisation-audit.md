# Non-render utilisation audit — 2026-10-27

**Scope:** every file under `src/` **outside** `src/render/`, plus the non-render build metadata
(`build/skygfx.vcxproj`, `.filters`, `premake5.lua`, `tools/fix_build.py`, `tools/gen_ini_ref.py`).
`src/render/` consolidation is treated as landed; the six retired `src/render/*_mode.cpp` are the
reference case for "concept moved to `src/core/modeManager.cpp` → old file deleted".

**Method:** read-only. All counts are textual occurrences outside the defining TU/header
(comments included unless stated), not link-time counts. Line numbers are this working tree.
`grep -rn` evidence is quoted for every deletion. No file was modified or built.

**Hard facts used below**

- `build/skygfx.vcxproj` has **165** `ClCompile` rows; **18** are non-render project TUs:
  `core/{debug_handl,debugmenu_theme,debugmenu_ui,main,modeManager,presets,weather}.cpp`,
  `entities/{chars,sftbdyphsx}.cpp`, `extras/{ragdoll_death,veh_shaders,vehicles,wheels}.cpp`,
  `rw/{defaultFuncs,gta,normmap_stubs,pipeplg}.cpp`, `src/texdefs.cpp`.
- `src/core/config.cpp` is only a commented row (`vcxproj:170`) and does not exist.
- 42 source/header files outside `src/render/`, **29,860 LOC**.
- No `src/vehicles/` directory exists. Vehicle code is `src/extras/vehicles.cpp`,
  `src/extras/veh_shaders.cpp`, `src/extras/wheels.cpp` (plus `src/render/vehiclePipe.cpp`).
- Line endings were measured per file. Exceptions to the stated map:
  `src/rw/gta.cpp` is **mixed CRLF/LF**; the RW anim headers
  (`rphanim/rtanim/rtquat/rtslerp.h`) are **LF**; `src/core/modeManager.*` and
  `src/extras/ragdoll_death.*` are **LF**. `src/extras/{brdfLibrary.h,debugmenu_public.h,vehicles.cpp,veh_shaders.cpp,wheels.cpp,wheels.h}`
  are **CRLF**, not LF as the general `src/extras/*` rule suggests. Everything else audited is
  CRLF.

---

## 1. Inventory of every file outside `src/render/`

Build key: **C** = `ClCompile`; **H** = `ClInclude`; **—** = neither (included by a compiled TU,
or not referenced at all).

| File | LOC | Build | Exported symbols (cross-TU refs) | Last mtime | Verdict |
|---|---:|---|---|---|---|
| `src/core/main.cpp` | 8701 | C (178) | `config` (1,067), `configs` (17), `GetConfig` (ABI), `D3D9Render*`, `resetValues`, `refreshIni`, `setConfig`, `saveConfig` (~40 exported fns) | 2026-10-27 | **KEEP/entry** — split plan target |
| `src/core/modeManager.cpp` | 1357 | C (179) | 12 API fns; ~60 refs outside (`GetMode` 21, `SetMode` 11, `ApplyMode` 9) | 2026-10-26 | **OWNER** — authoritative mode layer |
| `src/core/presets.cpp` | 157 | C (205) | `ApplyPreset` (6 calls; 14 textual), `presetNames` (5 textual), `presetConfigs` (1 decl, 0 use) | 2026-10-25 | **ABSORB** — legacy preset ladder |
| `src/core/weather.cpp` | 466 | C (219) | `Weather_Init` (1), `Weather_Update` (1), `Weather_ApplySunConfig` (1); `SelectSet/GetBlendFactor` internal-only | 2026-10-23 | **KEEP** — prune 2 exports |
| `src/core/debug_handl.cpp` | 1207 | C (169) | `dbglog` (902), `diag_registerScope` (30), `diag_installVEH` (1), `menu_inject_init` (**0**) | 2026-10-27 | **KEEP log/crash; PRUNE 781-1207 + dead diag** |
| `src/core/debugmenu_ui.cpp` | 4461 | C (221) | `DrawUnifiedDebugMenu` (1, from `postfx.cpp:2400`), `refreshMenu` (1), `installMenu` (**0**), `hasMenu` (**0**) | 2026-10-27 | **KEEP ImGui; DELETE legacy 90-381/852-860** |
| `src/core/debugmenu_theme.cpp` | 619 | C (223) | 12 helpers; ~50 refs (`DrawGlassHeader` 17, `DrawAccentSeparator` 19) | 2026-10-26 | **KEEP** — `UI_ImGuiIsInitialized` dead |
| `src/core/main_exports.h` | 87 | — | **0** cross-TU (only `main.cpp:2` includes it; other hit is stale `graphify-out/graph.json`) | 2026-07-30 | **DELETE** — stale `hooks.cpp` bridge |
| `src/core/modeManager.h` | 125 | H (156) | 12 function decls; 5 includers | 2026-10-25 | **KEEP** |
| `src/core/debug_handl.h` | 118 | H (155) | logging/crash API; 12 includers; menu-inject block dead | 2026-10-27 | **KEEP** — prune 97-118 |
| `src/core/debugmenu_theme.h` | 132 | H (158) | 12 decls; 2 includers (`debugmenu_ui`, self) | 2026-10-26 | **KEEP** |
| `src/core/weather.h` | 31 | — | 5 decls; 2 includers; `Weather_SelectSet/GetBlendFactor` only internal callers | 2026-08-03 | **KEEP** — make 2 static |
| `src/entities/chars.cpp` | 817 | C (165) | **2 live** (`chars_drawSSSBlur` → `postfx.cpp:2021/2287`; `chars_OnDeviceReset` → `postfx.cpp:2651`); 8 dead exports | 2026-10-27 | **KEEP draw pass; PRUNE ~270 LOC** |
| `src/entities/chars.h` | 67 | — | 10 dead decls + duplicate `SkinPBR/SSS_Blur` externs; 3 includers | 2026-10-23 | **PRUNE dead decls** |
| `src/entities/sftbdyphsx.cpp` | 1184 | C (168) | `g_ragdollMan` (4, `main.cpp:5466/5467/7950/8687`); `Dismember_*` (5-6, `ragdoll_death.cpp:381/402/1033/1141/1205/1206`) | 2026-10-27 | **KEEP** — merge-later with ragdoll |
| `src/entities/sftbdyphsx.h` | 206 | H (154) | `Ragdoll_c`/`RagdollManager_c`/`DismemberState`; 3 includers | 2026-10-27 | **KEEP** |
| `src/entities/PlantSurfPropMgr.h` | 32 | — | 1 includer (`render/grasssys.h:19`) | 2026-07-29 | **ABSORB** — grass-private stub |
| `src/extras/ragdoll_death.cpp` | 1227 | C (206) | 6 `Ragdoll_*` fns; `g_bulletWorld` (32), `g_ragdollDeathEnable` (9) | 2026-10-27 | **KEEP** — death ragdoll / Bullet owner |
| `src/extras/ragdoll_death.h` | 143 | — | globals + instance struct; 3 includers | 2026-10-27 | **KEEP** |
| `src/extras/veh_shaders.cpp` | 539 | C (217) | 14 live `VehShaders_*` (paint/tint/glass/tire/surface); **6 dead** | 2026-09-23 | **KEEP** — prune dead exports |
| `src/extras/vehicles.cpp` | 378 | C (216) | `GetVehicleGroup/Era/Cop/Taxi/FWD` live; `GetVehicleDrive/DBCount/DB` **0 external** | 2026-07-29 | **KEEP** — prune 3 exports; merge-later |
| `src/extras/wheels.cpp` | 169 | C (220) | `Wheels_Init` (1 call); `SelectForVehicle/GetEntry/GetClassForModel/GetCount` **0** | 2026-07-29 | **DELETE** — selection never wired |
| `src/extras/wheels.h` | 44 | — | only `wheels.cpp` includes it | 2026-07-29 | **DELETE with .cpp** |
| `src/extras/brdfLibrary.h` | 627 | — | data/enums; 3 includers (`buildingPipe`, `vehiclePipe`, `veh_shaders`) | 2026-08-27 | **RELOCATE** into render ownership |
| `src/extras/debugmenu_public.h` | 152 | H (137) | `gDebugMenuAPI` (42 inlines) — all inside the dead legacy menu | 2026-07-29 | **DELETE after menu removal** |
| `src/rw/gta.cpp` | 232 | C (177) | 136 `WRAPPER/EAXJMP` + ~45 game globals (`d3d9device`, `Scene`, `pAmbient`, `CTimeCycle__m_CurrentColours`) | 2026-09-22 | **KEEP** — RW 3.6 glue |
| `src/rw/gta.h` | 388 | H (136) | 45 declarations; included by `skygfx.h:165` → all 47 TUs | 2026-10-27 | **KEEP** |
| `src/rw/defaultFuncs.cpp` | 466 | C (171) | 7 defs; **0 cross-TU refs** | 2026-07-29 | **DELETE** |
| `src/rw/normmap_stubs.cpp` | 87 | C (187) | ~30 link stubs for `rpnormmap.lib`; 0 source refs (deliberate) | 2026-07-29 | **KEEP** — link glue |
| `src/rw/pipeplg.cpp` | 17 | C (194) | `GetPipelineID/SetPipelineID` (6 refs) | 2026-10-27 | **MERGE into gta.cpp** |
| `src/rw/ColData.h` | 17 | — | 1 includer (`render/grasssys.h:18`) | 2026-07-29 | **ABSORB** — grass-private stub |
| `src/rw/LinkList.h` | 148 | H (132) | `CLinkList` used by `gta.h:159-163` / `envmap.cpp:304-308` | 2026-07-29 | **KEEP** |
| `src/rw/MemoryMgr.h` | 98 | H (133) | `InjectHook/InterceptCall/Patch/Nop` (120 refs) | 2026-07-29 | **KEEP** |
| `src/rw/Pools.h` | 161 | H (135) | `CPool<>` used by `vehiclePipe.cpp:120` | 2026-07-29 | **KEEP** |
| `src/rw/ModuleList.hpp` | 211 | H (134) | `ModuleList` (3 includers; main:7898-7899, postfx×2) | 2026-07-29 | **KEEP** |
| `src/rw/rphanim.h` | 673 | — | RW anim hierarchy; 3 includers | 2026-07-30 | **KEEP** — vendored RW 3.6 |
| `src/rw/rtanim.h` | 654 | — | RW anim; 2 includers | 2026-07-30 | **KEEP** |
| `src/rw/rtquat.h` | 646 | — | RW quaternion; 5 includers | 2026-07-30 | **KEEP** |
| `src/rw/rtslerp.h` | 262 | — | RW slerp; 3 includers | 2026-07-30 | **KEEP** |
| `src/texdefs.cpp` | 1123 | C (186) | 14 exports; `TexDefsGetInfo` (6), `GetNormalMap` (5), `GetDefinedShader` (7); 2 dead | 2026-10-27 | **KEEP** — unified texture owner |
| `src/texdefs.h` | 31 | H (153) | 14 decls; 6 includers | 2026-10-27 | **KEEP** — prune 2 decls |
| `src/skygfx.h` | 1600 | H (152) | master header; 47 includers; `Config` ABI, retired-mode decls 1570-1592 | 2026-10-27 | **KEEP** — prune retired decls |
| `src/.lsai/mjsf.json` | 43 | — | untracked generated metadata | 2026-07-18 | **STALE ARTIFACT** |
| `src/graphify-out/graph.json` | 15364 | — | untracked generated graph | 2026-08-03 | **STALE ARTIFACT** |

**Non-source leftovers elsewhere:** `build/obj/Release/` still contains objects for deleted TUs
(`custom_mode`, `iv_mode`, `pc_patched_mode`, `ps2_mode`, `xbox_mode`, `vegImposter`,
`HLSL_hook`, `normalmap`, `normalmap_rw`, `test`). They are not inputs to the current vcxproj and
are safe to purge with the next `--rebuild`; do not treat them as evidence a source file exists.

---

## 2. Concept-ownership conflicts

The rule applied here is the `*_mode.cpp` precedent: if one TU is authoritative for a concept,
any other TU that only carries a stale copy of that concept is either deleted, demoted to data,
or explicitly marked as a compatibility shim.

### 2.1 Modes / presets — **three owners, one of them already obsolete**

| Old/other writer | New owner | What remains in the old file | Verdict |
|---|---|---|---|
| `src/core/presets.cpp:25-105` `presetConfigs[]` | `src/core/modeManager.cpp:95-617` `kValues*` | 15 positional `PresetConfig` rows; `ApplyPreset` writes 13 fields + 6 cascades + 2 shadow toggles (`presets.cpp:123-156`) | **duplicated concept, still load-bearing via UI** |
| `src/core/main.cpp:6019-6177` `qualityPreset` switch | same `kValues*` profiles | 4 tiers (LOW/MEDIUM/HIGH/ULTRA) writing pipes/features when `[Modes] active` is absent (`main.cpp:6025-6177`) | **duplicated fallback, still load-bearing** |
| `src/core/main.cpp:6742` reads `gamePreset`; `:7594` writes it | `presets.cpp:124` and `modeManager.cpp:102/563` also write `c->preset` | `preset` is display-only (`debugmenu_ui.cpp:977-978,3424-3426`) but has three writers | **metadata owner unclear** |

Evidence of duplication rather than delegation:

- `PresetConfig` (`skygfx.h:255-275`) has 18 fields, but `ApplyPreset` never reads
  `p->buildingPipe`, `p->vehiclePipe`, or `p->smaaPreset` (`grep -n "p->buildingPipe\|p->vehiclePipe\|p->smaaPreset" src/core/presets.cpp` → no output). The **pipes and SMAA preset columns are dead data**. The doc `docs/plans/2026-10-24-mode-preset-architecture.md:243` already decided to demote `ApplyPreset` to a colour-grade-only preset.
- `ApplyPreset` clears the mode latch before writing (`presets.cpp:115-121`) precisely because
  it bypasses `ApplyMode`. This is the same coupling the retired mode files had.
- `qualityPreset` is still authoritative when no mode is active. `modeManager.cpp:553-554`
  explicitly notes that `qualityPreset` is a `main.cpp` file-static; the Modern profile
  re-states its values rather than calling it.

**Verdict:** `modeManager.cpp` owns modes; `presets.cpp` owns a *legacy game-flavour* ladder that
must either be absorbed into `modeManager.cpp` (same file, same latch logic) or formally reduced
to colour-grade fields. It is not dead today (6 call sites, `debugmenu_ui.cpp:2874,2896,2906-2912`), but
its pipe/toggle writes duplicate ownership. The `qualityPreset` block is the second copy of the
same fallback; it must be resolved before the main split's config-I/O extraction (§5).

### 2.2 Menu — unified ImGui is authoritative; the plugin-sdk tree is dead

- Legacy tree: `debugmenu_ui.cpp:90-169` (`MENUSETTINGS` macro), `171-178` (`SkyGfxMenu menu;
  bool hasMenu`), `181-190` (`refreshMenu`), `191-224` (row callbacks), `225-381` (`installMenu`),
  `852-860` (`ivModeLegacyRowEdit`). **~301 LOC.**
- `installMenu` is **never called** — the only call is commented at `main.cpp:8068`
  (`menu_inject_init`, a different dead entry point, is disabled at `main.cpp:8037`);
  `refreshMenu` is called at `main.cpp:775` but is a guaranteed no-op because `hasMenu` is only
  set inside `installMenu` (`debugmenu_ui.cpp:377`). `hasMenu` has no other writer.
- Unified owner: `DrawUnifiedDebugMenu` (`debugmenu_ui.cpp:4161`), called from
  `postfx.cpp:2400`; theme helpers in `debugmenu_theme.cpp`.
- The legacy API header `src/extras/debugmenu_public.h` (152 lines) is pulled in only by
  `debugmenu_ui.cpp:5` and `main.cpp:10`; `gDebugMenuAPI` is defined at `main.cpp:46` and used
  only by the inlines inside `debugmenu_public.h`, all reached from the dead `installMenu`.
  Proof: `grep -rn "\bgDebugMenuAPI\b" src` → 43 hits, all `debugmenu_public.h`, `main.cpp:46`,
  and the dead block.

**Verdict:** the entire legacy menu is **dead**, not "stale but active". Deleting it also
deletes `debugmenu_public.h`, `gDebugMenuAPI`, both `MENUSETTINGS` macros, `installMenu`/
`refreshMenu` declarations (`skygfx.h:936,941`, `main_exports.h:68`) and the `refreshMenu()` call
at `main.cpp:775`. The ImGui menu keeps `DrawUnifiedDebugMenu` and `debugmenu_theme.*` untouched.

### 2.3 Crash / log — one owner, but two minidump writers and a dead watchdog

- `debug_handl.cpp` is the only TU that defines `dbglog` (`:126-181`); `skygfx.h:48` declares it,
  and `skygfx.h:89-128` supplies the `dbglog_throttle/once/first` inline guards. No duplicate
  `dbglog` family remains in `main.cpp` — the "DIAGNOSTICS — inline" comment at `main.cpp:8077`
  is historical; no definitions follow it.
- **Real duplication:** `diag_writeMinidump` (`debug_handl.cpp:443-479`) is a second minidump
  writer alongside the live `crashWriteDump` (`:647-694`). When the first one is compiled as a
  `static` at `:47` but never called, it still carries a `MiniDumpWriteDump` path and its own
  `dbghelp.dll` load. Remove the dead copy.
- **Dead diagnostics API:** `diag_heartbeat` (`:303-306`), `diag_stopWatchdog` (`:372-375`),
  `diag_removeVEH` (`:431-437`), `diag_checkCrashMarker` (declared `debug_handl.h:78`, **never
  defined**) and the whole watchdog thread (`:308-375`) have zero callers. `diag_startWatchdog()`
  is commented out at `main.cpp:8123` ("heartbeat() is never called, causes false freeze dialog").
  Keep `diag_init`, `diag_installVEH`, `diag_getLogPath`, `diag_registerScope`, `diag_ringDump`.
- **Menu-injection section:** `debug_handl.cpp:781-1207` (427 LOC) is the old `menu_inject.cpp`.
  Its only entry point `menu_inject_init` (`:1185`) is disabled at `main.cpp:8037` and
  commented at `:8068`. It also pulls `modeManager.h` into the logging TU only for an IV-mode
  desync guard at `:1147`; after deletion that include can go.

**Verdict:** `debug_handl.cpp` remains the crash/log owner. It should shrink from 1,207 to
~670 lines: delete 781-1207 and the dead diagnostic API. No `dbglog` duplication needs fixing.

### 2.4 Ragdoll — two live systems, intentionally split by lifecycle

- `src/extras/ragdoll_death.cpp` owns the **Bullet world** (`g_bulletWorld`, `:52`, created
  `:347-365`), death activation (`Ragdoll_Activate`, `:411`), simulation (`:963`), pose writeback
  (`:1150`), and drives the dismemberment API (`Dismember_Init` at `:381`, `Dismember_ProcessPed`
  at `:1033`, `Dismember_Update/Render` at `:1141-1142`).
- `src/entities/sftbdyphsx.cpp` owns the **living-ped secondary-motion overlay**
  (`RagdollBone`/`Ragdoll_c`/`RagdollManager_c`, `:78-560`), the global `g_ragdollMan` (`:561`),
  and the **implementation** of `Dismember_*` (`:676-1184`). It consumes `g_bulletWorld` and
  `g_ragdollDeathEnable` from `ragdoll_death.h` (`sftbdyphsx.cpp:17`).
- Both are live: `main.cpp:7950` (`g_ragdollMan.Init`), `:5466-5467` (process/update),
  `:5422` (`Ragdoll_Simulate`), `:3936` (`Ragdoll_ApplyPose`), `:8692` (`Ragdoll_Shutdown`).

**Verdict:** not dead, and not a simple duplicate. The two TUs are mutually dependent:
`ragdoll_death` needs `Dismember_*`, `sftbdyphsx` needs `g_bulletWorld`. That mutual dependency is
the only real argument for a merge (§4, candidate D); the split itself is coherent (death vs
living-ped overlay). Do **not** move either without a plan for the Bullet world lifetime.

### 2.5 Texture metadata — no extras remnant; one stale struct location

- Owner: `src/texdefs.cpp` + `src/texdefs.h`. It already merged `src/extras/texdb.cpp`,
  `src/render/normalmap.cpp`, `src/rw/normalmap.cpp` (`texdefs.cpp:1-30`). There is no
  `texdb.cpp`/`normalmap*.cpp` left; `grep -rln "texdb" src` hits only comments/main.
- `TexInfo` is still defined in the master header (`skygfx.h:1117-1130`), not `texdefs.h`. That
  is a layering leak but not a conflict: `texdefs.cpp` is the only producer/consumer of the map.
- Dead API in the owner: `TexDefsExternalShutdown` (`texdefs.h:20`, definition
  `texdefs.cpp:1087`) and `TexDefsSetDefinedShader` (`texdefs.h:31`, definition `:90`) have **0
  call sites** (`grep -rn` outside `texdefs.*` → no output). `TexDefsFindInfo/AssignInfo` are live
  (`main.cpp:3564`).
- `chars.cpp:130` uses `TexDefsGetInfo()` for `TexInfo::materialType`; that consumer is itself in
  the dead classification lane (§2.9/§3), so it does not extend the live texture contract.

**Verdict:** TexDefs is the sole owner. The only cleanup is moving `TexInfo` next to its owner and
pruning the two uncalled entry points.

### 2.6 Vehicles — no `src/vehicles/`; three extras providers, one render consumer

| Layer | Owner | Responsibility | Evidence |
|---|---|---|---|
| DB/classification | `src/extras/vehicles.cpp` | parses `vehicles.ide`/`handling.cfg`, `VehicleGroup`/`VehicleEra`/`VehicleDrive`, lookup | `:1-6`, enums `:11-38`, `Vehicles_Init :312` |
| Material tables | `src/extras/veh_shaders.cpp` | paint types/weights, light/glass/tire tints, texture-name classification, PBR property queries | header `:1-8`, tables `:37-130`, `VehShaders_Init :130` |
| Wheel metadata | `src/extras/wheels.cpp` | parses `models/wheels/wheels_meta.json`, weight-based wheel selection | `:47-105` |
| Render consumer | `src/render/vehiclePipe.cpp` | hashes paint, calls table APIs, uploads PBR | init `:203-216`; paint `:3240-3245`; tires `:3064`; chrome `:3297` |
| Separate pipe | `src/render/neopipe.cpp` | Neo/CarPipe/WaterDrops; **does not use** `VehShaders_*`/`Vehicles_*` | `grep -n "VehShaders\|GetVehicle\|Wheels" src/render/neopipe.cpp` → no output |

Live calls: `VehShaders_Init` (vehiclePipe:208), `Weather_Init` (:212), `Wheels_Init` (:216) are
the single init chain. `VehShaders_GetPaintPBR` clamps `paintType > 3` to 0 even though
`VehShaders_SelectPaintType` can return 4 (Clearcoat, table `:52-58`); the fifth paint type is
unreachable — an ownership drift, not just dead code.

**Verdict:** `vehiclePipe.cpp` owns rendering; `extras` owns data. `wheels.cpp` is dead beyond its
init side effect. `vehicles.cpp`/`veh_shaders.cpp` should share a private header or merge (§4).

### 2.7 Grass / vegetation — two live families, no `vegImposter` source remnant

- `src/render/grasssys.cpp` owns both `CGrassRenderer` (`:99-480`) and `CGrassInstancer`
  (`:832-...`) — the in-flight merge is landed. `grasssys.h:18-19` includes the two private
  stubs `ColData.h` and `PlantSurfPropMgr.h`.
- `src/render/treeWind.cpp` owns tree-wind matrices + tree alpha profiles
  (`TreeAlpha_profile :51`, `TreeWind_ModifyMatrixForTreeInWind :258`, `TreeAlpha_SetProfile :369`).
  It is a distinct consumer of `CWeather__Wind` (`:149-169`), not a grass duplicate.
- `vegImposter` is **gone from source**: `grep -rn "vegImposter" src` returns only the historical
  comment `forwardplus.cpp:1858`. Stale `build/obj/Release/vegImposter.obj` and clangd index
  entries remain (non-source).
- `main.cpp` includes `PC_GrassInstancer.h:19` (inside render) and calls
  `CGrassInstancer::SetEnabled` at `:773`.

**Verdict:** no grass/veg merge is justified. The two outside-render stubs (`ColData.h`,
`PlantSurfPropMgr.h`) belong to `grasssys.h` and have exactly one includer; they are absorb
candidates (§4, candidate E), not independent files.

### 2.8 Weather / timecycle — write-owner vs read-owner is clean

- Writer: `src/core/weather.cpp` loads `data/weathers2.dat` (`Weather_Init :229-239`) and
  overwrites `CTimeCycle::m_CurrentColours` (`Weather_Update :285-466`), then applies sun
  multipliers (`Weather_ApplySunConfig :249-283`). Order is enforced at
  `main.cpp:5473`/`:5478` inside the block the main split plan moves to `sceneHooks`.
- Readers: `src/render/pipelinecommon.cpp:175-196` (`CTimeCycle_GetAmbient*` → `GetTimecycleAmbient`)
  and `src/render/envmap.cpp:510` (`SetAmbientColours()` for fading buildings). Both are
  consumers, not owners. `pipelinecommon.cpp:183` explicitly says `Weather_Update()` populates
  the values.
- Dead surface: `Weather_SelectSet` always returns `WEATHER_SET_2` (`weather.cpp:61-64`), and
  `Weather_GetBlendFactor` is its only caller (`:67-70`, `:330`). Both are exported in
  `weather.h:22/25` but have no cross-TU caller. `WeatherSet`/`NUM_WEATHER_SETS` are only used by
  this always-2 abstraction.

**Verdict:** keep `weather.cpp` as the write owner. Make the two helpers `static` and shrink
`weather.h` to the three live entry points. Do not fold weather into `pipelinecommon.cpp`; that
would move the writer into the renderer and invert the layering.

### 2.9 INI / tools — generator is pinned to `main.cpp`'s current layout

- `tools/gen_ini_ref.py:37` hard-codes `MAIN_CPP = src/core/main.cpp`, and `:742` searches for
  the literal `readIniFile(const char *modulePath, Config *c)`. The main split plan's Stage 6
  moves that function to `configIo.cpp`; the generator then raises `ValueError` (signature not
  found) or silently reads a stale file.
- `:254` special-cases `qualityPreset → g_qualityPreset` as the file-static seed. If §2.1
  removes that block, the special case must go too.
- `:269-272` declares `EXTERNAL_INI_SOURCES` as `src/render/forwardplus.cpp` and
  `src/texdefs.cpp`; a move of `texdefs.cpp` (not proposed) would silently drop those keys
  because the tool skips missing paths.
- `:279` parses `ADD_IF_MISSING` in `main.cpp`; those macros are inside `readIniFile`
  (`main.cpp:6909-6913`), so they move with it in Stage 6 and the cross-check silently reports 0.
- `tools/fix_build.py:262-264` requires `extras/brdfLibrary.h`, `extras/veh_shaders.cpp`,
  `render/vehiclePipe.cpp`, `render/buildingPipe.cpp`, `core/main.cpp`; §4's vehicle merge must
  update that list. `:237-238` requires `entities/sftbdyphsx.cpp`; the ragdoll merge must update it.
- `premake5.lua:180/183/190/193` are directory globs, so deletions/merges need no premake TU
  edit, but the "glob is complete today" comment (`:176-179`) becomes false after `src/rw`
  loses `defaultFuncs.cpp`.

**Verdict:** the parser and generator currently agree. Any ownership move in §2.1 or any main
split Stage 6 move breaks the generator unless it is updated in the same commit. This is a real
blocker, not documentation drift.

---

## 3. Dead / unreferenced files

### 3.1 Whole files

| File | Proof | Risk of deleting | Backup? |
|---|---|---|---|
| `src/rw/defaultFuncs.cpp` (466) | `grep -rn "\b(D3D9RenderNotLit\|D3D9RenderPreLit\|DNInstance_default\|CCustomBuildingDNPipeline__CustomPipeRenderCB\|CCustomCarEnvMapPipeline__CustomPipeRenderCB\|CCustomBuildingDNPipeline__CreateCustomObjPipe)\b" src --include=*.cpp --include=*.h \| grep -v /defaultFuncs.cpp` → only `skygfx.h:1290-1293` declarations and `defaultFuncs.cpp` self-references. `CreateCustomObjPipe` is only called by its own file (`:292`). | **LOW**; no DLL export (`build/skygfx.def` has an empty EXPORTS list). Prior manifest C14 classes it NEEDS-BUILD-CHECK; the build is the proof. | **YES** (`_backup/nonrender_s1_2026-10-27/`) |
| `src/extras/wheels.cpp` (169) + `wheels.h` (44) | `grep -rn "Wheels_" src \| grep -v src/extras/wheels` → only `vehiclePipe.cpp:215-216` (`Wheels_Init`). Selection/getters have no call sites. `wheels_meta.json` is the only input; no render path reads `g_wheels`. | **LOW**; removes one log line (`Wheels_Init:`), no visual change. | **YES** |
| `src/extras/debugmenu_public.h` (152) | `grep -rln "debugmenu_public.h" src` → `main.cpp`, `debugmenu_ui.cpp`. All `DebugMenu*` calls are inside `installMenu` (`debugmenu_ui.cpp:228-377`); `gDebugMenuAPI` is defined `main.cpp:46` and used only by the header's inlines. | **LOW after §2.2 lands**; before that, deleting breaks `installMenu` compile. | **YES** |
| `src/core/main_exports.h` (87) | `grep -rln "main_exports.h" src` → only `src/core/main.cpp:2`. Header comment says "bridge between the two translation units" (`hooks.cpp`) but no `hooks.cpp` exists. All declarations are duplicated in `skygfx.h` or defined in `main.cpp`; the one load-bearing item is `struct FX` (`main_exports.h:80-85`). | **MEDIUM** — move `struct FX` to `main.cpp` before deleting; no link impact. | **YES** |
| `src/core/presets.cpp` | Not dead (6 `ApplyPreset` calls in `debugmenu_ui.cpp`); see §2.1. | **DO NOT DELETE** without UI switch. | n/a |

### 3.2 Dead code inside live files

| Location | What | Proof | Risk |
|---|---|---|---|
| `debugmenu_ui.cpp:90-169,171-178,181-190,191-224,225-381,852-860` | legacy menu + `MENUSETTINGS` + row callbacks + `ivModeLegacyRowEdit` | `installMenu` 0 call sites; `hasMenu` only written at `:377`; `refreshMenu` only called at `main.cpp:775` (no-op) | **LOW**; keep ImGui code |
| `main.cpp:7673-7746` | duplicate `MENUSETTINGS` macro | only defined; macro body never expanded in `main.cpp`. The symbol map's claim that `debugmenu_ui.cpp:90/174/185` uses it is wrong — that is a *different* macro with the same name in another TU. | **LOW** |
| `main.cpp:46` + `debugmenu_public.h` | `gDebugMenuAPI` + SDK menu API | only the dead `installMenu` path | **LOW** |
| `debug_handl.cpp:781-1207` | native pause-menu injection | `menu_inject_init` only reference is `main.cpp:8037` **commented**; `patch_screens_table` only called by it (`:1190`) | **LOW**; disabled since before this tree |
| `debug_handl.cpp:303-306,308-375,431-437,443-479` | watchdog + `diag_removeVEH` + duplicate minidump | `diag_startWatchdog` commented at `main.cpp:8123`; `diag_writeMinidump` is `static` and never called; `crashWriteDump` (`:647-694`) is the live writer; `diag_checkCrashMarker` is declared but **not defined** (`debug_handl.h:78`) | **LOW**; keep `diag_init/installVEH/getLogPath/registerScope/ringDump` |
| `chars.cpp:53-241,257-303,745-799` (~270) | ped classification, `chars_classifyClump`, `chars_init/shutdown`, `chars_getPartParams`, `chars_buildClassificationBuffer` | `grep -rn` outside `chars.*` → declarations only. `s_charsClassifyFresh` has no producer (`chars.cpp:423-424`, `:764-770`). Keep `g_charsClassifyTex/Surf/Raster`, `EnsureClassifyBuffer`, `chars_drawSSSBlur`, `chars_OnDeviceReset`. | **LOW-MEDIUM** — this is the documented "mask producer TODO"; deleting removes the stub, not a working feature. Prior manifest C9 already verified 263 LOC. |
| `weather.cpp:61-70` + `weather.h:22,25` | `Weather_SelectSet`/`GetBlendFactor` exports | only internal call at `weather.cpp:68/330`; no cross-TU refs | **LOW**; make `static` |
| `texdefs.cpp:90-95,1087` + `texdefs.h:20,31` | `TexDefsSetDefinedShader`, `TexDefsExternalShutdown` | 0 call sites outside `texdefs.*` | **LOW** |
| `veh_shaders.cpp:294-312,419-445,449-539` (~140) | dirt API, colour-area tables, saturation/colour generation, `GetColorArea/GetMinSaturation` | only definitions + `skygfx.h:1163-1171` declarations; `GetColorArea` only called by dead `GetMinSaturation` (`:538`) | **LOW**; paint/tint live functions untouched |
| `vehicles.cpp:342,359-362,377-378` | `GetVehicleEntry`/`GetVehicleDrive` external linkage (internal-only use), `GetVehicleDBCount`, `GetVehicleDB` | `GetVehicleDrive` has no external caller; DB getters 0 refs. `GetVehicleEntry` is internal-only (used `:350/355/360`) and can be `static`. | **LOW** |
| `debugmenu_theme.cpp:260-264` | `UI_ImGuiIsInitialized` | 0 call sites | **LOW** |
| `skygfx.h:1568-1592` | retired `*Mode_ApplyDefaults/GetPreset` decls | 0 call sites; the six `.cpp` were deleted. `struct PresetConfig;` forward at `:1568` is redundant (definition `:255`). | **LOW** |
| `skygfx.h:1596` + `ragdoll_death.h:136` | `Ragdoll_Update` declared twice | header already provides an inline (`ragdoll_death.h:136`); the `skygfx.h` duplicate is harmless but misleading | **LOW** |
| `main.cpp` `fixingSAMP` + `main_exports.h:15` | declaration-only surface | used only in `main.cpp:184/7898`; never cross-TU | **LOW** with `main_exports.h` deletion |

### 3.3 Non-source stale artifacts

- `src/.lsai/mjsf.json` (43 lines, untracked), `src/graphify-out/graph.json` (15,364 lines,
  untracked) — generated by tooling that no longer participates in the build. Not referenced by
  `premake5.lua`, the vcxproj, or `fix_build.py`. Delete or `.gitignore`; no backup needed.
- `build/obj/Release/{custom_mode,iv_mode,pc_patched_mode,ps2_mode,xbox_mode,vegImposter,HLSL_hook,normalmap,normalmap_rw,test}.obj` — orphan objects from deleted TUs. Delete with the next
  `--rebuild`; they are not a source of truth.
- `.cache/clangd/index/vegImposter.*` — same, stale index; harmless.

---

## 4. Match-together candidates

Strict criterion: shared **struct**, shared **lifecycle**, or a **private header**. A shared
`#include` alone is not enough. No candidate below contains `__finally`. Among the candidates,
none contains naked asm. Two other TUs in the audited set do contain naked asm:
`main.cpp` (13 tokens, handled exclusively by the main split plan) and `texdefs.cpp` (two hooks at
`texdefs.cpp:341/351`, **not** a merge candidate). Any future move of either must say so
explicitly and preserve the verbatim blocks.

| # | Files | Combined LOC | Why together | Why NOT / blockers | Build delta |
|---|---|---:|---|---|---|
| A | `presets.cpp` + `modeManager.cpp` | 1,514 | Both write the same `Config`; `ApplyPreset` already calls `ModeManager_ClearLatch` (`presets.cpp:120-121`); UI uses both on one tab (`debugmenu_ui.cpp:2855-2912`); both own `preset`. | `PresetConfig` is a positional whole-Config writer with 3 dead columns; merging without demoting it creates a bigger two-headed owner. `presets.cpp` CRLF vs `modeManager.cpp` LF. Decide §2.1 policy first. | `vcxproj:205` removed; no filters row today; `premake5.lua:180` glob auto; `fix_build.py` no row. |
| B | `vehicles.cpp` + `veh_shaders.cpp` (after wheels deletion) | 917 | `VehShaders_Init` calls `Vehicles_Init` (`veh_shaders.cpp:133`); both share `VehicleGroup/VehicleEra`; three files hand-redeclare the same externs (`veh_shaders.cpp:19-26`, `wheels.cpp:41`, `vehiclePipe.cpp:11`). One init lifecycle from `vehiclePipe.cpp:203-216`. | Different cadence: `vehicles` is a file parser, `veh_shaders` is static material data. `veh_shaders.cpp` is required by `fix_build.py:263`. No header exists today, so the merged TU still needs a private header for `vehiclePipe`. | `vcxproj:216-217` → one `vehicleData.cpp`; filters add one row; `premake5.lua:193` glob auto; `fix_build.py:263` rename. |
| C | `rw/pipeplg.cpp` + `rw/gta.cpp` | 249 | `GetPipelineID/SetPipelineID` operate on `gtaPipeOffset` (`pipeplg.cpp:5`) and are declared in `gta.h:387-388`; `gta.cpp` is the game-global layer. | `gta.cpp` is mixed CRLF/LF; 232 lines of WRAPPERs. Value is one `ClCompile` row and fewer stray files. | `vcxproj:194` removed; filters:126 stale row removed; `premake5.lua:190` glob auto. |
| D | `sftbdyphsx.cpp` + `ragdoll_death.cpp` | 2,411 | Mutual dependency: `ragdoll_death` calls `Dismember_*` (`:381/1033/1141/1205`), `sftbdyphsx` reads `g_bulletWorld`/`g_ragdollDeathEnable` (`sftbdyphsx.cpp:17`). One Bullet world lifetime. | **Large**; LF vs CRLF; two different lifecycles (living-ped overlay vs death ragdoll); the single `__try` in `sftbdyphsx.cpp:1182` must remain verbatim; `fix_build.py:237` requires `sftbdyphsx.cpp`. | `vcxproj:168,206` → one TU; filters:160 removed; `fix_build.py:237` rename. |
| E | `rw/ColData.h` + `entities/PlantSurfPropMgr.h` → `render/grasssys.h` | 49 | Single includer (`grasssys.h:18-19`); both are private game-type stubs used only by grass. `CPLANT_SURF_PROP_PLANTDATA_NUM` is used at `grasssys.h:245-246`; `CColTriangle` at `:248-249`. | They model real game structs (`CColTriangle`, plant props) that might be reused; `grasssys.h` is already 300+ lines. Moving is cheap; merging into `grasssys.h` is optional. | No vcxproj/premake rows today; only `grasssys.h` include edits. If renamed, add to `premake5.lua:234-253`. |
| F | `extras/brdfLibrary.h` → `render/brdfLibrary.h` | 627 | Data/enums consumed only by render material code (`buildingPipe.cpp:3`, `vehiclePipe.cpp:3`) and the vehicle data bridge (`veh_shaders.cpp:12`); concept is material/rendering, not "extras". | `veh_shaders.cpp` is in extras; moving the header makes extras depend on render (already true through other headers). | No vcxproj row; `fix_build.py:262` path update; 3 `#include` edits. |
| G | `core/main_exports.h` → `main.cpp` | 87 | Only one includer (`main.cpp:2`); the original second TU (`hooks.cpp`) is gone; all declarations duplicate `skygfx.h`/definitions except `struct FX`. | If any out-of-tree consumer includes it, deletion breaks it; no in-tree consumer. | No build row. Move `struct FX` next to `main.cpp:2688`. |

**Rejected:** `weather.cpp` + `pipelinecommon.cpp` (write owner vs render reader, different
layers); `treeWind.cpp` + `grasssys.cpp` (distinct shader families and lifecycles);
`debug_handl.cpp` + `debugmenu_ui.cpp` (logging vs UI); `presets.cpp` + `main.cpp` qualityPreset
that would keep two fallback owners in the entry TU.

---

## 5. Deltas to `2026-10-27-main-split-plan.md` / `main-symbol-map.md`

Do not rewrite those plans; these are the only deltas this audit adds.

| # | Delta | Plan reference | Effect |
|---|---|---|---|
| D1 | `MENUSETTINGS` in `main.cpp:7673-7746` is **not** consumed by `debugmenu_ui.cpp`; that file defines its own macro at `:90`. The symbol map's 1A/1D rows are wrong. | `main-split-plan.md:161,347`; `main-symbol-map.md:82,161` | Change "KEEP — dead macro" to **delete now**. No extraction needed. |
| D2 | The legacy menu is fully dead, so `installMenu`/`refreshMenu`/`hasMenu` need not survive Stage 1-6. Deleting it also removes `debugmenu_public.h` and `gDebugMenuAPI`. | `main-symbol-map.md:25,209`; `main-split-plan.md` has no row for it | **Extra extraction unlocked** (a 301-line UI prune + 152-line header) before any main.cpp stage. |
| D3 | `main_exports.h` is not a cross-TU interface: only `main.cpp` includes it. Plan repoints `main_exports.h:37/38/49/55-56/60/63/70/74` are vacuous. Only `struct FX` (`:80-85`) is load-bearing. | `main-split-plan.md:100,115,224,229,256-257,270,306,343` | **Extra unblocker**: Stage 2/4/6/7 can delete the header instead of preserving declarations; move `FX` once. |
| D4 | `qualityPreset` (`main.cpp:6019-6177`) duplicates `modeManager` profiles and is still load-bearing when `[Modes] active` is absent. Stage 6 would move it verbatim into `configIo`, freezing two owners. | `main-split-plan.md:335-345` | **Extra blocker for Stage 6**: decide the owner first (§2.1). If `modeManager` wins, `configIo` shrinks by ~159 lines and `g_qualityPreset`/`main.cpp:7614` must be retired. |
| D5 | `gen_ini_ref.py` hard-codes `main.cpp` (`:37`) and `readIniFile(const char *modulePath, Config *c)` (`:742`). Stage 6 moves that function. | `main-split-plan.md:338-350` | **Extra blocker**: update the generator path/signature in the same commit, or `--check` fails. |
| D6 | `debug_handl.cpp`'s menu section is dead (`main.cpp:8037` commented), including its only `modeManager.h` use. | `main-split-plan.md:143-165` (InjectDelayedPatches KEEP) | No extraction needed; deleting 781-1207 makes the diagnostics TU independent of mode state. |
| D7 | `sceneHooks` does not call any `chars_*` function. `chars_drawSSSBlur`/`chars_OnDeviceReset` are called from `postfx.cpp:2021/2287/2651`. | `main-split-plan.md:299` lists `chars.h` in the sceneHooks include union | Drop `chars.h` from that union; no functional change. |
| D8 | `presets.cpp` is live and not mentioned by the split plan. Any `configRuntime` mode accessors must not absorb `ApplyPreset` implicitly. | `main-split-plan.md:327-337` | Add an explicit exception until `debugmenu_ui` switches to `ApplyMode`; target LOC unchanged. |
| D9 | `refreshMenu()` at `main.cpp:775` becomes removable with the legacy menu; `configRuntime` can drop it from `refreshIni`. | `main-split-plan.md` `refreshIni` row (Stage 6) | One fewer no-op call/declaration. |
| D10 | `defaultFuncs.cpp` / `wheels.cpp` deletions and the vehicle `fix_build.py` requirement collide with the plan's vcxproj/premake edits. | `main-split-plan.md:417-467` build-delta template | **Sequence first** or re-anchor the vcxproj line numbers before Stage 1. |
| D11 | `fix_build.py` requires `extras/veh_shaders.cpp` (`:263`) and `entities/sftbdyphsx.cpp` (`:237`). Merges in §4 must update these paths. | `main-split-plan.md:461-466` fix_build step | Extra edit in the vehicle/ragdoll merge stages. |

---

## 6. Ordered stage plan (smallest risk first)

Every stage is independently compilable. Backup the stage's write set to
`_backup/nonrender_s<N>_2026-10-27/` **before** editing; never `git checkout` this dirty tree.
Global gate after every stage:

```text
grep -o 'ClCompile Include="[^"]*"' build/skygfx.vcxproj | sed 's/.*\\//;s/"//' | sort | uniq -d
# must print nothing (LNK4042 basename gate)
```

### Stage 0 — metadata reconciliation (no code risk)

- Fix stale `.filters` rows: `..\src\defaultFuncs.cpp:98`, `..\src\pipeplg.cpp:126`,
  `..\src\debugmenu_public.h:39`, `..\src\gta.cpp:107`, `..\src\main.cpp:110` (live paths are
  `rw\defaultFuncs.cpp`, `rw\pipeplg.cpp`, `extras\debugmenu_public.h`, `rw\gta.cpp`,
  `core\main.cpp`). `.filters` does not affect the build but misleads every future audit.
- Delete `src/.lsai/`, `src/graphify-out/` and stale `build/obj/Release` objects.
- **Verify:** `git status --short` shows only intended files; `python tools/fast_build.py --fastest`
  still succeeds.

### Stage 1 — delete `src/rw/defaultFuncs.cpp` (466 LOC)

- Write set: `src/rw/defaultFuncs.cpp`, `build/skygfx.vcxproj:171`, `build/skygfx.vcxproj.filters:98`,
  `src/skygfx.h:1290-1293`.
- Why first: whole-TU zero-reference; no exports; no SEH/naked.
- **Build delta:** remove 1 `ClCompile`; filter row; remove 4 declarations.
- **Verify:**
  `grep -rn "D3D9RenderNotLit\|D3D9RenderPreLit\|DNInstance_default\|CCustomBuildingDNPipeline__CreateCustomObjPipe\|CCustomCarEnvMapPipeline__CustomPipeRenderCB" src` → only `skygfx.h` (if any) and render `_PS2/_Switch` names; then build+launch, confirm no new log tags and `[PIPE]` init unchanged. Prior manifest C14 says this is the only whole-TU deletion needing a link check.

### Stage 2 — delete `src/extras/wheels.cpp/.h` (213 LOC)

- Write set: both files, `build/skygfx.vcxproj:220`, `vehiclePipe.cpp:215-216`.
- **Build delta:** remove 1 `ClCompile`; no filters/premake/fix_build row exists.
- **Verify:** `grep -rn "Wheels_\|WheelEntry\|WheelClass" src` → no output; build; launch log must
  not contain `Wheels_Init:` (expected removal), all other init lines unchanged.

### Stage 3 — prune dead API in live TUs (~350 LOC)

- `veh_shaders.cpp:294-312,419-445,449-539` + `skygfx.h:1163-1171`; `vehicles.cpp:342,359-362,377-378`
  (make `GetVehicleEntry`/`GetVehicleDrive` `static`; delete the two DB getters);
  `weather.cpp:61-70` → `static` + `weather.h:22,25`; `texdefs.cpp:90-95,1087` + `texdefs.h:20,31`;
  `debugmenu_theme.cpp:260-264` + `debugmenu_theme.h:84`.
- Leave `VehShaders_GetPaintPBR` clamp bug as-is or fix separately (it is a behaviour change).
- **Build delta:** none (no rows).
- **Verify:** for each symbol `grep -rn "\bSYMBOL\b" src` shows only its definition/header;
  `python tools/fast_build.py --fastest`; launch and confirm identical log lines except the
  removed ones.

### Stage 4 — remove legacy DebugMenu UI (~301 LOC + 152 header)

- Write set: `debugmenu_ui.cpp:5,90-169,171-178,181-190,191-224,225-381,852-860`;
  `main.cpp:10,46,775,7673-7746,7754,8037,8064-8068`; `skygfx.h:936,941`;
  `main_exports.h:68`; delete `src/extras/debugmenu_public.h`; `build/skygfx.vcxproj:137`;
  `build/skygfx.vcxproj.filters:39`; `premake5.lua:253`.
- Keep `DrawUnifiedDebugMenu` (`debugmenu_ui.cpp:4161`) and all of `debugmenu_theme.*`.
- **Build delta:** −1 `ClInclude`, −1 header row in premake/filters.
- **Verify:** `grep -rn "DebugMenuLoad\|DebugMenuAddVar\|gDebugMenuAPI\|installMenu\|refreshMenu\|hasMenu" src` → no output; build; launch, F11 opens the ImGui window; no `[MenuInject]` lines (none existed).

### Stage 5 — prune `debug_handl.cpp` (~550 LOC)

- Delete `:781-1207` (native menu injection) and `:303-306,308-375,431-437,443-479` (dead
  watchdog + duplicate minidump). Remove `#include "modeManager.h"` (`:17`); delete
  `debug_handl.h:97-118`; keep `diag_init/installVEH/getLogPath/registerScope/ringDump`.
- **Build delta:** none.
- **Verify:** `grep -rn "menu_inject_init\|diag_writeMinidump\|diag_heartbeat\|diag_stopWatchdog\|diag_removeVEH" src` → only `main.cpp:8037` comment removed; build; launch, crash log still writes
  `skygfx_dbg.log` and the VEH install line from `main.cpp:8122` is unchanged.

### Stage 6 — prune dead `chars.cpp` classification (~270 LOC)

- Delete `:25-35` RW function pointers used only by classification, `:53-241`, `:257-303`,
  `:745-799`; keep `g_charsClassifyTex/Surf/Raster`, `EnsureClassifyBuffer`,
  `s_charsClassifyFresh`, `chars_drawSSSBlur`, `chars_OnDeviceReset`. Remove dead decls in
  `chars.h:48-49,54-62`.
- **Build delta:** none.
- **Verify:** `grep -rn "chars_classify\|chars_init\|chars_shutdown\|chars_getPartParams\|chars_buildClassificationBuffer" src` → no output; build; launch, `[PostFX] chars_drawSSSBlur ENTER` still appears and the pass-2 skip message is unchanged.

### Stage 7 — merge `rw/pipeplg.cpp` into `rw/gta.cpp` (249 LOC)

- Append the 17-line body verbatim; delete `pipeplg.cpp`; remove `vcxproj:194` and
  `filters:126`. Do not normalise `gta.cpp` EOL (it is mixed today).
- **Verify:** `grep -rn "GetPipelineID\|SetPipelineID" src` resolves to `rw/gta.cpp`; build;
  launch and confirm building Ps2/DN pipe selection log lines unchanged.

### Stage 8 — absorb grass stubs + move BRDF header + delete `main_exports.h`

- Inline/forward `ColData.h` + `PlantSurfPropMgr.h` into `render/grasssys.h:18-19`; delete the
  two stand-alone headers only after verifying no other include. Move `extras/brdfLibrary.h` →
  `render/brdfLibrary.h`; update `buildingPipe.cpp:3`, `vehiclePipe.cpp:3`,
  `veh_shaders.cpp:12`; update `fix_build.py:262`.
- Delete `main_exports.h`; move `struct FX` to `main.cpp` just above `main.cpp:2688`;
  `main.cpp:2` include removed. No vcxproj row.
- **Verify:** `grep -rln "ColData.h\|PlantSurfPropMgr.h\|brdfLibrary.h\|main_exports.h" src` →
  only the new paths; build; launch materials unchanged (BRDF is a pure header move).

### Stage 9 — absorb `presets.cpp` into `modeManager.cpp` (1,514 LOC)

- First pass: pure move of `presetNames`/`presetConfigs`/`ApplyPreset` to the end of
  `modeManager.cpp`; delete `vcxproj:205`; keep every exported name (`skygfx.h:277-278,943`).
  `modeManager.cpp` is LF; append as LF.
- Second pass (separate commit): demote `ApplyPreset` per
  `docs/plans/2026-10-24-mode-preset-architecture.md:243` — drop `p->buildingPipe/vehiclePipe/
  smaaPreset`, stop writing pipes, or replace the UI path with `ApplyMode`.
- **Verify (pass 1):** build; open Presets tab, apply all 15 entries, compare `config` field
  values before/after against a fixed INI; no new log tags. **Pass 2** is behaviour-changing and
  needs the doc's acceptance table.

### Stage 10 — merge `vehicles.cpp` + `veh_shaders.cpp` (917 LOC)

- Create `src/extras/vehicleData.cpp` (or append to `veh_shaders.cpp`), keep all public names and
  the init order `Vehicles_Init` → `VehShaders_Init`. Delete `vcxproj:216-217`, add one row.
  Update `fix_build.py:263`.
- **Verify:** `grep -rn "VehShaders_Init\|Vehicles_Init" src` → one definition/one call chain;
  build; launch, `VehShaders_Init: bridge initialized` still appears once; paint/tint/glass values
  unchanged on a fixed vehicle.

### Stage 11 — optional ragdoll merge (2,411 LOC, last)

- Only if the team wants one Bullet lifecycle TU. Append `ragdoll_death.cpp` to
  `sftbdyphsx.cpp` (or the reverse) verbatim; adopt **LF** (or CRLF — pick one and note it);
  do not alter the `sftbdyphsx.cpp:1182` `__try` body. Update `vcxproj:168,206`, `filters:160`, `fix_build.py:237`.
- **Verify:** `grep -rn "Dismember_Init\|g_bulletWorld\|Ragdoll_Simulate" src` resolves in one
  TU; build; launch and confirm `Ragdoll_Init`, `Dismember_Init`, ragdoll activation and
  `Dismember: gib freed` lines are unchanged. If context budget is the constraint, **do not do
  this merge** — it is the only candidate that creates a >2k-line TU.

### Stage 12 — pre-main-split decisions (must precede main split Stage 6)

- Decide §2.1: delete the `qualityPreset` block or keep it as a read-only mirror. If deleted,
  remove `g_qualityPreset` (`main.cpp:5899`), `saveConfigTo:7614`, and
  `gen_ini_ref.py:254`; update `docs/ini-reference.md`.
- Update `tools/gen_ini_ref.py` to read `configIo.cpp` (or a configurable list) and to find
  `readIniFile` in the new TU; update `ADD_IF_MISSING` parsing to the new file.
- Re-run `python tools/gen_ini_ref.py --check`, then `python tools/fast_build.py --fastest`.
- Only then apply main-split Stage 6 (`configIo`), which moves `readIniFile` verbatim.

---

## 7. Bottom line

- **Delete now:** `defaultFuncs.cpp` (466), `wheels.cpp/.h` (213), `debugmenu_public.h` (152),
  `main_exports.h` (87), and `debug_handl.cpp:781-1207` (427) + dead diag (~120).
- **Prune inside live TUs:** legacy menu (301), chars classification (~270), veh_shaders dead
  API (~140), texdefs/weather/debugmenu_theme dead entry points.
- **Absorb:** `presets.cpp` → `modeManager.cpp`; `pipeplg.cpp` → `gta.cpp`; grass stubs →
  `grasssys.h`; `brdfLibrary.h` → render ownership; `main_exports.h` → `main.cpp`.
- **Do not merge:** weather + pipelinecommon; grass + treeWind; debug_handl + debugmenu_ui.
- **Defer:** `sftbdyphsx.cpp` + `ragdoll_death.cpp` (mutual dependency but 2,411 LOC and mixed EOL).
- **Main-split deltas:** D1-D11 above; the two hard blockers are the `qualityPreset`/`ApplyPreset`
  ownership decision and `gen_ini_ref.py`'s hard-coded `main.cpp` path.
