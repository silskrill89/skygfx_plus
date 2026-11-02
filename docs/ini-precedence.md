# INI Precedence

How a `skygfx.ini` value becomes a live render setting, and which of the
several config writers wins. Everything here is read from the current tree;
every rule carries a `file:line` citation. If a rule could not be proven from
source it is marked **UNVERIFIED**.

Related documents:

- [`modes.md`](modes.md) — per-mode catalogue (fields, pipes, known gaps).
- [`ini-reference.md`](ini-reference.md) — generated key/type/default/clamp table.
- [`../skygfx.default.ini`](../skygfx.default.ini) — canonical code defaults.

---

## 0. TL;DR

```
LOAD (readIniFile, main.cpp:5294)
  1. per-slot base snapshot  <- the post-INI user state
  2. top-level keys          -> Config, starting from qualityPreset defaults
  3. [Modes] profile         -> owned fields only, applied LAST
  4. base re-snapshot        -> ModeManager_RefreshBase()

RUNTIME
  menu edit                  -> writes Config directly (highest, transient)
  next ApplyMode / INI load  -> overwrites mode-owned fields; non-owned survive

DISENGAGE
  mode-owned fields          <- per-slot base (the user's INI values)
  non-owned fields           -> never written, keep their live value
```

"Mode-owned" means a field listed in that mode's `kValues*` table in
`src/core/modeManager.cpp`. `None` and `Custom` own nothing
(`src/core/modeManager.cpp:735-745`).

---

## 1. The preset systems

There are **three live preset systems plus one retired set**. Only one of the
live three is authoritative at the field level: `ModeManager`.

| System | Driven by | When it runs | Authority |
|---|---|---|---|
| `qualityPreset` | `[SkyGfx] qualityPreset` | every `readIniFile()` | seeds feature defaults (and pipes for tiers 0-2) |
| `gamePreset` / `ApplyPreset()` | menu Style combo / preset buttons | menu action only | whole-Config write, clears the mode latch |
| **`[Modes]` / `ModeManager`** | `[Modes] active` or a mode chip | end of every `readIniFile()`, and on every chip click | **authoritative for owned fields** (runs last) |
| Retired per-mode writers (5 `*_mode.cpp` + `mobile.cpp`) | nothing | never | dead stubs |

### 1.1 `qualityPreset` — load-time feature preset

- Read once per load: `int preset = readint(cfg.get("SkyGfx", "qualityPreset", ""), 3)`
  (`src/core/main.cpp:5408`). Default is `3` (ULTRA). It is kept in a
  file-static `g_qualityPreset` because it is not a `Config` field
  (`src/core/main.cpp:5285-5288`), and written back on save
  (`src/core/main.cpp:6973`).
- The switch (`src/core/main.cpp:5415-5566`) sets feature defaults. Every later
  read uses `c-><field>` as its default, so an explicit INI key overrides the
  preset (`src/core/main.cpp:5411-5412`, e.g. `src/core/main.cpp:5887`).
- Tiers 0, 1 and 2 also rewrite `buildingPipe` / `vehiclePipe` / `colorFilter`
  (`src/core/main.cpp:5417-5421`, `:5444-5448`, `:5470-5474`) — but only when
  no pipe-owning mode is active (the `!modePipesOwned` guard on each).
- Tier 3 deliberately does **not** touch the pipes (`src/core/main.cpp:5505-5509`).
- Written for a brand-new INI at `src/core/main.cpp:6270` and for an existing
  INI missing the key at `src/core/main.cpp:6404`.

### 1.2 `gamePreset` / `ApplyPreset()` — menu-only whole-config preset

- `[SkyGfx] gamePreset` is read into `c->preset`, but it is **display only**:
  "Style combo display only — the values themselves load via their own keys
  above" (`src/core/main.cpp:6098-6099`). It does not apply anything at load.
- The preset action is `ApplyPreset(Config*, int)` (`src/core/presets.cpp:108-157`),
  called by the Style combo and the preset buttons
  (`src/core/debugmenu_ui.cpp:2874`, `:2896`, `:2906-2912`).
- `ApplyPreset()` is a direct whole-`Config` writer that **bypasses the mode
  manager**. It first drops any active mode latch
  (`src/core/presets.cpp:115-121`, `ModeManager_ClearLatch` at
  `src/core/modeManager.cpp:1018-1033`) and then writes the preset fields
  (`src/core/presets.cpp:123-156`).
- It does **not** write `buildingPipe` / `vehiclePipe`
  (`src/core/presets.cpp:126-129`); it does write `colorFilter` and the
  feature/global gates (`src/core/presets.cpp:130-150`).

### 1.3 `[Modes]` / `ModeManager` — authoritative

- Exactly one mode can be latched (`src/core/modeManager.h:30-44`,
  `src/core/modeManager.cpp:39`).
- Each mode is a sparse table of owned `Config` fields:
  `kValuesPS2` (`src/core/modeManager.cpp:95`), `kValuesXBOX` (`:200`),
  `kValuesPCVintage` (`:306`), `kValuesGTAIV` (`:399`), `kValuesGTAV` (`:504`),
  `kValuesModern` (`:514`), `kValuesMobile` (`:640`). `MODE_NONE` and
  `MODE_CUSTOM` have `nullptr, 0` (`src/core/modeManager.cpp:736`, `:743`).
- The only writer of a profile's owned fields is `ApplyMode()` /
  `applyProfile()` (`src/core/modeManager.cpp:1077-1164`, `:813-826`).
  Direct writers (`ApplyPreset`, menu rows) can still write those fields, but
  they do not apply a profile (`src/core/presets.cpp:115-121`).
- It runs **after** every top-level key in `readIniFile()`
  (`src/core/main.cpp:6535-6542`), which is why a profile wins over the INI
  value for a field it owns.
- `ModeManager_SetMode()` is a thin alias; passing the currently active id is
  the toggle-off path (`src/core/modeManager.cpp:1271-1285`).

### 1.4 The pipeline/pipe keys are a selector, not a preset

`[SkyGfx] pipeline`, `pipelineOverride`, `buildingPipe`, `vehiclePipe` and
`colorFilter` form a fourth value source, but it is a pipe selector with a
derived mirror, not a mode preset:

- `pipeline` is read via a keyword map, default PBR
  (`src/core/main.cpp:5351-5360`).
- `pipelineOverride` (0..4, `-1` = disabled) pins `pipeline` directly
  (`src/core/main.cpp:5363-5365`).
- The pipeline is expanded to a pipe/filter triple unless a pipe-owning mode
  is active (`src/core/main.cpp:5342`, `:5373-5401`).
- Explicit `buildingPipe` / `vehiclePipe` keywords override that mapping,
  again only when no pipe-owning mode is active
  (`src/core/main.cpp:5572-5599`).
- `derivePipelineFromPipes()` then re-derives `pipeline` from the actual pipes
  (`src/core/main.cpp:5606`, definition `:648-729`). See §6.2.

### 1.5 RETIRED — the `*_mode.cpp` preset writers

All six per-mode writers are **empty stubs with zero call sites**. ModeManager
is the single source of mode values.

| File | Function | Status |
|---|---|---|
| `src/render/ps2_mode.cpp:39-51` | `PS2Mode_ApplyDefaults` | RETIRED, empty |
| `src/render/xbox_mode.cpp:39-51` | `XboxMode_ApplyDefaults` | RETIRED, empty |
| `src/render/iv_mode.cpp:33-46` | `IVMode_ApplyDefaults` | RETIRED, empty |
| `src/render/pc_patched_mode.cpp:34-47` | `PCPatchedMode_ApplyDefaults` | RETIRED, empty |
| `src/render/custom_mode.cpp:39-52` | `CustomMode_ApplyDefaults` | RETIRED, empty |
| `src/render/mobile.cpp:33-46` | `MobileMode_ApplyDefaults` | RETIRED, empty |

The declarations are marked `// RETIRED - no call sites; ModeManager is
authoritative` in `src/skygfx.h:1534-1556`. The files are still compiled —
`src/render/*.cpp` is globbed (`premake5.lua:204`) — so the stubs exist only to
keep the header declarations valid (`src/render/ps2_mode.cpp:39`). The only
remaining call-site-shaped mentions in `src/` are stale comments
(`src/core/main.cpp:614`, `src/core/debugmenu_ui.cpp:2830`).

---

## 2. Exact load order

`readIniFile(const char*, Config*)` (`src/core/main.cpp:5294-6552`) is shared by
`readIni` (`:6555`), `readInis` (`:6586`), `reloadAllInis` (`:6607`) and
`loadConfigFile` (`:7018`). One pass does the following, in this order:

| # | Step | Line |
|---|---|---|
| 1 | `cfg.load_file(modulePath)`; `iniExisted` records whether the file was there | `main.cpp:5298` |
| 2 | `[SkyGfx] keySwitch` / `keyReload`; an empty/missing value keeps the current binding | `main.cpp:5303-5310` |
| 3 | `[Modes] active` is **parsed but not applied** (only when `c == config`); sets `s_activeModeIni` / `s_activeModeIniSeen` | `main.cpp:5312-5341` |
| 4 | `modePipesOwned = (c == config && modeOwnsPipes())` | `main.cpp:5342` |
| 5 | `[General] NoiseQuality` (global, not a `Config` field) | `main.cpp:5347` |
| 6 | `pipeline` + `pipelineOverride` reads, then the pipeline→pipe mapping (the mapping is skipped if a pipe-owning mode is active) | `main.cpp:5349-5401` |
| 7 | `qualityPreset` + preset switch | `main.cpp:5406-5566` |
| 8 | `ps2Modulate` / `dualPass` globals (these are what the per-surface reads use as defaults) | `main.cpp:5568-5569` |
| 9 | explicit `buildingPipe` / `vehiclePipe` overrides (skipped if pipes are mode-owned) | `main.cpp:5571-5599` |
| 10 | `derivePipelineFromPipes(c)` | `main.cpp:5606` |
| 11 | every remaining `[SkyGfx]` key | `main.cpp:5608-6254` |
| 12 | if the file did not exist: write the full default INI; else `ADD_IF_MISSING` for absent keys only | `main.cpp:6259-6392`, `:6393-6533` |
| 13 | `[Modes] active` is **applied** (only if a known alias was parsed and differs from the current latch) | `main.cpp:6535-6542` |
| 14 | `ModeManager_RefreshBase()` (only when `c == config`) | `main.cpp:6544-6547` |
| 15 | `cfg.write_file(modulePath)` — unconditional rewrite | `main.cpp:6549-6551` |

`readInis()` (`src/core/main.cpp:6594`), `reloadAllInis()` (`:6615`) and
`loadConfigFile()` (`:7021`) then call `refreshIni()`
(`src/core/main.cpp:750-786`), which re-applies the clamps, re-derives the
pipeline (`:773`) and copies values into `CPostEffects` via `resetValues()`
(`:784`, definition `:340`). The bare `readIni()` used by the early startup
path (`:7190-7193`) does not, but the later `afterStreamIni` hook
(`:7029-7035`, installed at `:7514`) re-runs `readInis()` and therefore
`refreshIni()`.

### 2.1 Effective precedence

Read as layers, lowest to highest:

| Layer | Written by | Wins over |
|---|---|---|
| 1. per-slot base (post-INI user state) | `ApplyMode` snapshot / `RefreshBase` | nothing — it is the fallback |
| 2. top-level INI keys | `readIniFile` | the base (it *is* the base source) |
| 3. active mode profile (owned fields only) | `ApplyMode` / `applyProfile` | the INI value for owned fields |
| 4. live menu edits | widgets write `config` directly | everything, until the next `ApplyMode` / reload |

Chronologically the INI keys are parsed **before** the profile is applied
(step 11 vs step 13), so "profile above top-level keys" is the correct
field-level order even though the request order reads base → profile → keys →
menu. The profile only outranks the keys for the fields it owns; for everything
else the top-level key is the final value.

---

## 3. Base snapshots — when captured, when refreshed

The base is **per config slot**, not process-global:
`Config s_baseConfig[MODE_CONFIG_SLOTS]` + `bool s_haveBase[]`
(`src/core/modeManager.cpp:34-37`, slot lookup `:948-956`). `setConfig()` can
repoint `config` to another slot (`src/core/main.cpp:6597-6605`), so each slot
keeps its own pre-mode state.

| Event | What happens | Citation |
|---|---|---|
| First engage `NONE -> A` | Snapshot the live Config **before** the profile runs, only if the slot has no base yet | `modeManager.cpp:1124-1132` (snapshot), `:1134-1137` (apply) |
| Switch `A -> B` | `adoptLiveEdits()` folds user edits into the base, then revert A's owned fields, then apply B | `modeManager.cpp:1117-1122`, `:1136` |
| Disengage `A -> NONE` | Revert A's owned fields from the base; adoption is deliberately skipped | `modeManager.cpp:1117-1120`, `:1191-1193` |
| `ApplyMode` during load, different id | `ApplyMode` captures the base from the just-parsed INI before applying the profile (`modeManager.cpp:1124-1137`) | `main.cpp:6541-6542` |
| `ApplyMode` during load, same id | Skipped (keeps the latch); `RefreshBase` handles it | `main.cpp:6541`, `modeManager.cpp:1246-1252` |
| INI reload, mode active, profile no longer matches Config | Snapshot the reloaded raw INI values as the **new** base, then re-layer the active profile | `modeManager.cpp:1254-1267` |
| INI reload, no mode active | Snapshot the reloaded Config as the base | `modeManager.cpp:1235-1243` |
| `ModeManager_ClearLatch()` | Drops the latch but **keeps** the base (direct writers already wrote the Config they want) | `modeManager.cpp:1018-1033` |
| `ModeManager_Init()` | Resets the latch and clears every `s_haveBase` | `modeManager.cpp:970-979` |

`RefreshBase()` detects "ApplyMode already ran this pass" by comparing every
owned field with the profile value (`configMatchesProfile`,
`modeManager.cpp:927-942`); a mismatch means the INI overwrote Config, so the
raw values become the new base and the profile is re-applied
(`modeManager.cpp:1254-1267`).

Important consequence: an INI reload **never** permanently replaces the active
profile with the file's values. Owned fields are still overwritten by the
profile at the end; the reloaded values only become what disengage restores.

---

## 4. What "engage" and "disengage" actually do

### 4.1 Engage — field-level only

Entry into a mode is:

1. capture the per-slot base if this is the first engage
   (`modeManager.cpp:1124-1132`);
2. write **only** the rows in the profile table (`applyProfile`,
   `modeManager.cpp:813-826`);
3. `resetValues()` to push the new Config into the game's `CPostEffects`
   fields (`modeManager.cpp:1152`, `src/core/main.cpp:340`), then notify
   listeners (`modeManager.cpp:1158`, listener at `src/core/main.cpp:7155-7173`).

No other field is written. `applyProfile()` iterates the table and nothing
else, so a field the mode does not own keeps its current value
(`modeManager.cpp:810-826`, contract in `modeManager.h:6-20`).

### 4.2 Disengage — restore owned fields, leave the rest alone

`ModeManager_Disengage()` (`src/core/modeManager.cpp:1167-1208`):

- iterates the **same** profile table and copies the base value back
  (`restoreProfile`, `modeManager.cpp:833-846`);
- never touches a non-owned field (`modeManager.cpp:1186-1193`);
- clears `s_haveBase[slot]` and latches `MODE_NONE`
  (`modeManager.cpp:1198-1201`);
- calls `resetValues()` and notifies listeners (`:1202-1207`);
- if there is no base (non-slot Config or a base dropped by `RefreshBase`),
  the Config is left untouched rather than half-restored
  (`modeManager.cpp:1194-1197`).

This is the behaviour requested: the user's INI config comes back because the
base was captured from the post-INI Config before the profile ran
(`modeManager.cpp:12-17`, `:1129-1132`).

Two caveats, both real:

1. **A menu edit to a mode-owned field is discarded on disengage.** The
   `A -> NONE` path deliberately skips `adoptLiveEdits()` so the restore is
   exact (`modeManager.cpp:1114-1120`). Editing a non-owned field survives.
2. **Saving the INI while a mode is active bakes the profile into the file.**
   `saveConfigTo()` writes the live `Config` with no mode guard
   (`src/core/main.cpp:6619-6669`, e.g. `buildingPipe` at `:6647`), and
   the Save button calls it (`src/core/debugmenu_ui.cpp:4227-4229`,
   `src/core/main.cpp:6999-7015`). After the next load those values are the
   new base, so disengage returns to the saved (profile) values rather than
   the pre-mode ones.

### 4.3 Menu edits

Menu rows write the field in `config` directly; the row-edit helper then calls
`resetValues()` to push the value into the game state
(`src/core/debugmenu_ui.cpp:2449-2458`). A subsequent `ApplyMode` /
`RefreshBase` overwrites owned fields again
(`modeManager.cpp:1254-1267`). An edit to an owned field is only folded into
the base on a **switch to another mode**
(`modeManager.cpp:1108-1122`, `adoptLiveEdits` at `:900-925`), not on a
disengage.

---

## 5. `[Modes] active = <alias>`

### 5.1 Aliases

| Alias | ModeId | Owned fields |
|---|---|---|
| `None` | `MODE_NONE` | 0 |
| `PS2` | `MODE_PS2` | 79 |
| `Xbox` | `MODE_XBOX` | 79 |
| `PC` | `MODE_PC_VINTAGE` | 48 |
| `GTAIV` | `MODE_GTAIV` | 63 |
| `GTAV` | `MODE_GTAV` | 2 |
| `Modern` | `MODE_MODERN` | 92 |
| `Custom` | `MODE_CUSTOM` | 0 |
| `Mobile` | `MODE_MOBILE` | 68 |

Source: alias map `src/core/main.cpp:5322-5333`; counts are the number of
`MV_*` rows in the tables (`src/core/modeManager.cpp:95`, `:200`, `:306`,
`:399`, `:504`, `:514`, `:640`). `docs/modes.md` still lists
77/77/42/63/2/80/59; those numbers do not match the current tables - the
counts above are the recounted rows.

Alias matching is case-insensitive (`strcmpi` in `StrAssoc::get`,
`src/core/main.cpp:5224-5229`). Whitespace around `=` is trimmed by the INI
reader (`external/ini_parser.hpp:200-235`). INI **key** names are
case-sensitive (plain `std::map`, `external/ini_parser.hpp:42`, `:307`).

### 5.2 Parsing rules

- `[Modes]` is read only while parsing the **active** config object
  (`if(c == config)`, `src/core/main.cpp:5321`). In the default single-file
  setup that is `skygfx.ini`; in a multi-config install the active slot can be
  `skygfx1.ini` .. `skygfx9.ini` instead (`readIni` mapping,
  `src/core/main.cpp:6555-6583`, `setConfig` at `:6597-6605`). Non-active
  slots ignore `[Modes]`.
- A known alias sets `s_activeModeIni`, `s_activeModeIniSeen` and marks the
  pass as mode-bearing (`src/core/main.cpp:5334-5340`).
- `None` is a real alias: it sets `MODE_NONE` and will disengage a latched
  mode at the end of the pass (`src/core/main.cpp:6541-6542`).
- **Unknown alias** (e.g. `active = GTA3`) falls through to the `{"", -1}`
  sentinel (`src/core/main.cpp:5332`, `:5224-5229`). Then
  `iniModeRaw >= 0` is false, so: no `ApplyMode`, no log, no change to
  `s_activeModeIni`, no `s_activeModeIniSeen`
  (`src/core/main.cpp:5317-5318`, `:5334-5340`). An already-latched mode stays
  latched; a fresh process stays `None`.
- **Absent key** behaves exactly like an unknown alias (same lines).
- The raw `active = ...` line is preserved by the load/write round-trip
  (`cfg.load_file` / `cfg.write_file`, `src/core/main.cpp:5298`, `:6550`), but
  `saveConfigTo()` only rewrites it once `s_activeModeIniSeen` is true
  (`src/core/main.cpp:6638-6642`).

### 5.3 INI reload while a mode is active

`reloadAllInis()` -> `readIni` -> `readIniFile` (`src/core/main.cpp:6607-6616`,
`:6555-6583`). What happens at the end of the pass:

| `[Modes]` in the reloaded file | Result |
|---|---|
| same alias as the latch | `ApplyMode` is skipped (`src/core/main.cpp:6541`), then `RefreshBase` spots that the INI values no longer match the profile, snapshots them as the new base and re-applies the profile (`modeManager.cpp:1246-1267`). The mode stays on; the reloaded values become the disengage target. |
| a different known alias | `ApplyMode(new)` runs (`src/core/main.cpp:6542`): outgoing owned fields revert to the old base, incoming profile applies. `RefreshBase` then sees a match and no-ops (`modeManager.cpp:1247-1252`). |
| `None` | `ApplyMode(MODE_NONE)` runs (`src/core/main.cpp:6542`): the active mode's owned fields are restored from the base. |
| absent or unknown | No `ApplyMode` (`src/core/main.cpp:5334-5340`). `RefreshBase` snapshots the reloaded Config and re-layers the still-active profile (`modeManager.cpp:1254-1267`). A menu-latched mode therefore **survives** an F11 reload even if the INI never mentions it. |

While the reloaded alias (or a still-latched menu mode) is pipe-owning,
`modeOwnsPipes()` (`src/core/main.cpp:110-120`) makes the pass ignore
`pipeline`, `buildingPipe`, `vehiclePipe` and `colorFilter`
(`src/core/main.cpp:5342`, `:5373`, `:5580`, `:5598`, `:5791`). If the reloaded
alias is `None`, `s_activeModeIni` is set to `MODE_NONE` while parsing
(`src/core/main.cpp:5338`), so the pipe keys are parsed — but the disengage at
`:6542` then restores those same fields from the base, so the final pipes are
still the pre-mode ones, not the file's.

---

## 6. Hazards

### 6.1 `qualityPreset` vs the pipes

The pipeline is mapped to pipes **before** `qualityPreset` is read
(`src/core/main.cpp:5373-5401` vs `:5408`). Tiers 0-2 then overwrite the
pipes (`:5417-5421`, `:5444-5448`, `:5470-5474`); tier 3 does not
(`:5505-5509`). Explicit `buildingPipe` / `vehiclePipe` keys override after
that (`:5580`, `:5598`), and `derivePipelineFromPipes()` re-derives
`pipeline` from whatever pipes survived (`:5606`).

Consequence: `pipeline = GTAIV` + `qualityPreset = 0` ends up with PS2 pipes
and `pipeline` re-derived to `PIPELINE_PS2`. The `pipeline` key is not the
final authority. While a pipe-owning mode is active, all of these preset pipe
writes are suppressed (`!modePipesOwned` guards on the same lines).

### 6.2 `pipeline` is a derived mirror

`pipeline` is read from its own key (`src/core/main.cpp:5360`) but is
overwritten from the actual `buildingPipe` / `vehiclePipe` pair by
`derivePipelineFromPipes()` (`src/core/main.cpp:648-729`), which runs:

- at the end of every load (`src/core/main.cpp:5606`);
- from `refreshIni()` on every config switch/reload (`src/core/main.cpp:773`);
- **every frame** from `ColourFilter_switch()` (`src/render/postfx.cpp:1424`,
  with the comment "rewrites EVERY FRAME" at `:1431-1437`).

The rules are ordered (first match wins): GTAIV building, GTAIV vehicle with a
PS2-ish partner, PBR building, Xbox building + Mobile vehicle, Xbox building,
PS2 building, fallback PBR (`src/core/main.cpp:660-686`).

`pipelineOverride` (`src/core/main.cpp:5363-5365`) writes `pipeline` directly
*before* the pipe mapping; at load the mapping then makes the pipes follow it,
so the derive confirms the value. While a pipe-owning mode suppresses the
mapping (`src/core/main.cpp:5373`), only the mirror changes and the next derive
reverts it. The menu override (`src/core/debugmenu_ui.cpp:795-802`) also calls
`ApplyPipelineFromMenu()`, so its pipes follow. Either way, the next derive
recomputes `pipeline` from the pipes. `ivMode` is explicitly **not** re-derived
(`src/core/main.cpp:726-728`).

### 6.3 `colorFilter` is forced to Modern under PBR

- On load: if `pipeline == PIPELINE_PBR` and the parsed filter is not
  `COLORFILTER_MODERN`, the loader warns once and forces Modern
  (`src/core/main.cpp:5798-5806`). The guarded read is at `:5791-5792`.
- Every frame: `derivePipelineFromPipes()` re-asserts the same force whenever
  the derived pipeline is PBR and `buildingPipe == BUILDING_PBR`, unless a
  pipe-owning mode is active (`src/core/main.cpp:706-724`).
- `colorFilterEnable` is independently forced to `1` under PBR, and the user
  intent latch is set to `1` so the self-heal cannot restore a stale `0`
  (`src/core/main.cpp:5951-5965`).

So `colorFilter = PC` / `None` / `VCS` under `pipeline = PBR` is not honoured.

### 6.4 Mode-owned keys are ignored at the top level while a mode is active

- Pipes and filter are not even parsed into the live Config while a pipe-owning
  mode is active: `modePipesOwned` gates the pipeline mapping (`:5373`), the
  `buildingPipe` / `vehiclePipe` overrides (`:5580`, `:5598`) and the
  `colorFilter` read (`:5791`).
- Every other owned field is parsed normally (step 11), then overwritten by the
  profile at the end of the pass (`src/core/main.cpp:6541-6542`). The INI value
  survives only as the base.
- `modeOwnsPipes()` returns true for PS2, Xbox, Legacy PC, GTA IV and Modern.
  It returns **false** for `None`, `Custom` and `GTAV`, so top-level pipe keys
  still apply for those (`src/core/main.cpp:110-120`).

### 6.5 `ApplyPreset()` silently ends a mode

`ApplyPreset()` clears the mode latch before writing its fields
(`src/core/presets.cpp:115-121`). The base is kept but not restored
(`modeManager.cpp:1018-1033`), and because the latch is now `None`, clicking
the previously lit chip cannot restore anything — it simply re-engages on the
current Config. If you use preset buttons while a mode is on, the mode is gone.

### 6.6 Saving while a mode is active bakes the profile

`saveConfigTo()` has no mode-aware guard: it writes `pipeline`,
`pipelineOverride`, `buildingPipe`, `vehiclePipe`, `colorFilter` and every
other Config value from the **live** Config
(`src/core/main.cpp:6619-6996`). Because the live
Config currently holds the profile's values, the INI now contains the profile.
On the next load those values become the base and disengage no longer restores
the user's pre-mode settings. Save either while no mode is active, or with
`active = None` and the desired values already restored.

### 6.7 Every load rewrites the INI file

`readIniFile()` ends with an **unconditional** `cfg.write_file(modulePath)`
(`src/core/main.cpp:6549-6551`). `linb::ini::write_file` re-emits the parsed
map: comments are gone, sections and keys are sorted alphabetically, and
`key = value` formatting is normalised (`external/ini_parser.hpp:256-282`;
parser comments at `:202-203`, `:307`). Unknown keys are preserved because
they were parsed into the map, but comment text is not. Do not rely on hand
formatting or comments surviving a load.

---

## 7. Worked examples

Three keys, three ownership shapes. Base = the value parsed from the INI
before the profile runs. "Runtime" = the value after `ApplyMode` at the end of
the load.

### 7.1 Mode-owned: `ssaoEnable`

| Stage | `[Modes] active = Modern` | `[Modes] active = None` |
|---|---|---|
| INI value | `0` | `0` |
| Parse | `c->ssaoEnable = 0` (`main.cpp:5887`, default `c->ssaoEnable`) | same |
| Base snapshot | 0 (captured `modeManager.cpp:1129-1132`) | 0 (`modeManager.cpp:1235-1243`) |
| Profile | Modern owns it, value `1` (`modeManager.cpp:549`) | no profile |
| Runtime | **1** | **0** |
| Disengage | restored to **0** (`modeManager.cpp:1191-1193`) | n/a |
| Save | writes the live **1** (`main.cpp:6755`) | writes 0 |

### 7.2 Not owned: `chromeF0`

| Stage | Any mode |
|---|---|
| INI value | `0.56` |
| Parse | `c->chromeF0 = readfloat(..., 0.56f)` (`main.cpp:5676`) |
| Ownership | no `MV_*` row in any `kValues*` table (`modeManager.cpp:95-745`) |
| Runtime | **0.56** under every mode |
| Disengage | **0.56** (never written by engage or disengage) |

Non-owned keys are the reason two people on different base configs get
different results from the same mode.

### 7.3 Renamed key: `dualPass` -> `dualPassGlobal`

The INI key and the `Config` field differ; the mode table stores the **INI
key**, not the field name.

| Concept | Value / citation |
|---|---|
| INI key | `dualPass` |
| Config field | `dualPassGlobal` |
| Read | `config->dualPassGlobal = readint(cfg.get("SkyGfx", "dualPass", ""), 0)` (`main.cpp:5569`) |
| Mode row | `MV_IK(dualPassGlobal, "dualPass", 1)` in Modern (`modeManager.cpp:608`); PS2/Xbox/PC/GTAIV also own it (`:165`, `:270`, `:357`, `:484`) |
| Ownership lookup | compares the **INI key**, case-insensitively (`modeManager.cpp:774-782`, `:799-808`) — with Modern active, `ModeManager_IsOwnedKey("dualPass")` is true and `ModeManager_IsOwnedKey("dualPassGlobal")` is false |
| Runtime under Modern | **1** (INI 0 is overwritten by the profile) |
| Disengage | restored to **0** |
| Save | `cfg.set("SkyGfx", "dualPass", ...)` from `dualPassGlobal` (`main.cpp:6659`) |

The seven renamed keys are `gamePreset`, `sunGlare`, `ps2Modulate`,
`dualPass`, `grassBackfaceCull`, `grassFixPlacement` and `YCbCrCorrection`
(`modeManager.cpp:49-53`, rows at `:102`, `:122`, `:149`, `:163-171`, `:608`).
The per-surface flags (`dualPassBuilding`, `dualPassVehicle`, …) are separate
keys read with `config->dualPassGlobal` as their default (`main.cpp:5629`,
`:5643`, `:5724`, `:5738`); they are separately owned, and an explicit
per-surface key beats that default (`main.cpp:5411-5412`).

---

## 8. Verification receipts

- Load: one `[Mode] active=<alias> (from INI)` or `[Mode] active=none` per
  process (`src/core/main.cpp:7196-7203`).
- Every apply: `[Mode] engage <alias> owned=<n> pipes=...` plus the legacy
  `[Mode] apply <name> pipes=<pipeline>/<buildingPipe>/<vehiclePipe> cf=<filter> iv=<ivMode>`
  (`src/core/modeManager.cpp:1153-1157`).
- Disengage: `[Mode] disengage restored=<alias> owned=<n> pipes=...`
  (`src/core/modeManager.cpp:1203-1206`).
- Reload while active: `[mode] base refreshed ... active=<alias> ...`
  (`src/core/modeManager.cpp:1262-1266`); a same-id reload that ends up
  identical logs `base refresh no-op ... (already applied this pass)`
  (`:1248-1250`).
- Unknown alias / absent key: no `[Mode]` receipt at all, because no apply is
  attempted (`src/core/main.cpp:5334-5340`).

## 9. Unverified / corrections

Nothing in this document required an **UNVERIFIED** marker: every rule above is
a direct read of the current tree.

Corrections to statements elsewhere:

- `docs/ini-reference.md:31` and `docs/modes.md:47` describe unknown/absent
  `[Modes] active` as `None`. The *displayed id* is None, but the runtime
  behaviour is a **no-op**: no `ApplyMode`, no latch change
  (`src/core/main.cpp:5334-5340`). On a fresh process that is indistinguishable
  from None; on an F11 reload with a mode latched it keeps the mode.
- The `gamePreset` key does **not** apply a preset at load. It only seeds the
  style-combo display (`src/core/main.cpp:6098-6099`); only the menu's
  `ApplyPreset()` call changes fields (`src/core/presets.cpp:108`).
