# Modes

One active mode at a time. A mode is a **named delta profile layered on a snapshot of your config**, not a self-contained preset. Read this before switching: unowned fields survive from whatever config was live when you entered the mode.

## Mental model

```
enter  (NONE -> A) : snapshot live Config into per-config-slot base, THEN apply profile(A)
switch (A -> B)    : apply profile(B) on the SAME base - profiles never stack on each other
toggle off (A -> A): restore Config verbatim from the slot base; mode becomes None
```

Authority: `src/core/modeManager.h:9-16`, `ApplyMode()` at `src/core/modeManager.cpp`. `ModeManager_SetMode()` is a thin alias of `ApplyMode()`. `ApplyMode()` is the one writer of mode-owned fields; it also calls `resetValues()` and notifies listeners.

The six legacy per-mode preset writers (`PS2Mode_ApplyDefaults`, `XboxMode_ApplyDefaults`, `IVMode_ApplyDefaults`, `MobileMode_ApplyDefaults`, `PCPatchedMode_ApplyDefaults`, `CustomMode_ApplyDefaults`) are **retired no-ops** (`src/skygfx.h:1488-1508`; each `*_mode.cpp` says `RETIRED`). They have no call sites. Any doc that credits them with applying a mode is wrong.

## Owned-field counts

| Mode | Alias | Owned fields |
|---|---|---|
| None | `None` | 0 |
| PS2 | `PS2` | 79 |
| Xbox | `Xbox` | 79 |
| Fixed PC (Legacy PC) | `PC` | 48 |
| GTA IV | `GTAIV` | 63 |
| GTA V | `GTAV` | 2 |
| Modern | `Modern` | 92 |
| Custom | `Custom` | 0 |
| Mobile | `Mobile` | **68** |

Counts are `MV_COUNT(kValues*)` in `src/core/modeManager.cpp` (PS2 `:95`, Xbox `:200`, Legacy PC `:306`, GTA IV `:399`, GTA V `:504`, Modern `:514`, Mobile `:640`).

Two consequences that surprise people:

1. **A→B does not build on A.** Entering PlayStation 2 from Modern and then GTA IV gives you IV on the *pre-mode* base, not IV on top of the PS2 profile.
2. **The snapshot is per config slot**, not process-global (`s_baseConfig[]`, `s_baseConfig` keyed by `configs[]`; `MODE_CONFIG_SLOTS` at `src/core/modeManager.cpp:31-34`). A `setConfig()` that repoints `config` selects a different base.

## How to switch

**INI** — `skygfx.ini`:

```ini
[Modes]
active=Modern
```

Accepted aliases (case-insensitive `StrAssoc`): `None`, `PS2`, `Xbox`, `PC`, `GTAIV`, `GTAV`, `Modern`, `Custom`, `Mobile` (`src/core/main.cpp:5315-5327`). Unknown or absent = `None`. Read at INI load (`src/core/main.cpp:5327`), saved back on save (`src/core/main.cpp:6630`). F11 reload re-applies the INI mode once (`src/core/main.cpp:6528-6534`).

**Menu** — F4 opens the debug menu. The **mode chip row sits above the tab strip and is always visible**: eight chips in order PS2 · Xbox · PC · Mobile · GTA IV · GTA V · Modern · Custom (`src/core/debugmenu_ui.cpp:3846-3851`, drawn at `:4040`). Clicking a lit chip toggles the mode off. Every settings row owned by the active profile gets a **MODE** pill (`ModeManager_IsOwnedKey`, `src/core/debugmenu_ui.cpp:2560`), and its context menu offers **Reset to mode default** (`ModeManager_GetOwnedValue`, `:2099-2101`).

**Pipes while a mode is active**: `modeOwnsPipes()` (`src/core/main.cpp:110-120`) makes a pipe-owning mode ignore top-level `pipeline`, `buildingPipe`, `vehiclePipe`, `colorFilter`. `None`, `Custom` and `GTAV` do **not** own pipes, so top-level keys apply for them.

## Precedence

```
lowest   1. per-config-slot base snapshot (your config before the mode run)
         2. active mode profile (owned fields only)
         3. top-level INI keys for fields the profile does NOT own
         4. live menu edits (highest, may be overwritten by the next ApplyMode)
```

Hazards, all real in the current tree:

- `pipelineOverride` rewrites `pipeline` even during a mode, desyncing the mirror (`src/core/main.cpp:5174-5177`).
- `qualityPreset` tiers 0-2 still write pipes when **no** mode is active; tier 3 (ULTRA) is feature-only (`src/core/main.cpp:5230-5233, 5261-5263, 5283-5285, 5317-5320`).
- `ApplyPreset()` (Style combo / preset buttons) clears the mode latch without restoring the base (`src/core/presets.cpp:118-137`, `ModeManager_ClearLatch` at `src/core/modeManager.cpp:687-704`).
- `[Mode.<alias>]` override sections are **not implemented**; only `[Modes] active` is read.

## None

- Identity: `MODE_NONE`, alias `None`, no profile (`src/core/modeManager.cpp:609`).
- Owns: nothing. Owned fields: 0.
- Inherits: everything — top-level `[SkyGfx]` keys and `pipeline` mapping are authoritative.
- Switch: `active=None`, or toggle off the active mode. Toggling off restores the slot base verbatim.
- Log: `[Mode] active=none` (`src/core/main.cpp:7009`) and `[Mode] apply None pipes=...` (`src/core/modeManager.cpp:873-878`).
- Gaps: with a fresh INI, `pipeline=PBR` still maps to PBR pipes and `COLORFILTER_MODERN`.

## PS2

- Identity: `MODE_PS2`, alias `PS2`, profile `kValuesPS2` (`src/core/modeManager.cpp:95`), 81 fields.
- Pipes/filter: `BUILDING_PS2` / `CAR_PS2` / `COLORFILTER_PS2`, `preset=SA_PS2`, `ivMode=0`.
- Changes: PS2 modulate on buildings; no detail maps/noise detail; no dual pass building or vehicle; `usePCTimecyc=0`; 256 env map; water style 0 (Xbox style shader); radiosity off; grain off; SSAO/SMAA/motion blur/SSS off; alpha contracts on.
- Does not change: env-map far clip tuning beyond 1.0, water tuning knobs other than style, `coronaZtest` is owned (PS2 z-test disabled), sun-corona gains not owned, ped/stencil shadows not owned, normal maps not owned.
- Switch: `active=PS2`. Receipt: `[Mode] apply PS2 pipes=1/0/0 cf=1 iv=0` shape (`modeManager.cpp:873-878`).
- Known gaps: PS2 vector water is absent — comment at `src/core/modeManager.cpp:118` ("PS2 vector water is absent"); the PS2 pipe is the base-style water path. Dual-pass is intentionally 0 for PS2.

## Xbox

- Identity: `MODE_XBOX`, alias `Xbox`, `kValuesXBOX` (`src/core/modeManager.cpp:206`), 81 fields.
- Pipes/filter: `BUILDING_XBOX` / `CAR_XBOX` / `COLORFILTER_PC`, `preset=SA_Xbox`.
- Changes: detail maps, noise detail, dual-pass building and vehicle on; PC timecycle on; radiosity on; 256 env map; water style 0 (Xbox); no SSAO/SMAA/MB/SSS; grain off.
- Does not change: normal maps, Forward+, buffers, IV grade values, corona gains, shadow knobs (not owned).
- Switch: `active=Xbox`. Log: `[Mode] apply Xbox pipes=2/1/2 cf=2 iv=0` shape.
- Known gaps: Xbox building pipe is the same pipe Fixed PC uses, so "Fixed PC" differs mainly by vehicle pipe and owned-field count.

## Fixed PC (Legacy PC)

- Identity: `MODE_PC_VINTAGE`, alias `PC`, display "Legacy PC", `kValuesPCVintage` (`src/core/modeManager.cpp:316`), 50 fields.
- Pipes/filter: `BUILDING_XBOX` / `CAR_PC` / `COLORFILTER_PC`, `preset=SA_PC`.
- Changes: detail maps/noise detail/dual-pass, PC timecycle, radiosity, grain (legacy SA grain path), 256 env map, alpha contracts.
- Does not change: `normalMapEnable`, `colorFilterEnable`, `sunCorona*`, `rgb1/rgb2`, `bYCbCrFilter` are **not owned** (`src/core/modeManager.cpp:293-349`). If the snapshot had normal maps on, Fixed PC keeps them on.
- Switch: `active=PC`.
- Known gaps: **there is no PC building pipe**. The legacy PC renderer reused the Xbox building path; the source comment at `src/core/modeManager.cpp:288-291` says aap maps "PC" to `BUILDING_XBOX`. Do not expect a distinct PC building look.

## GTA IV

- Identity: `MODE_GTAIV`, alias `GTAIV`, `kValuesGTAIV` (`src/core/modeManager.cpp:418`), **70 fields** (expanded by the 2026-10-25 IV parity audit and the 2026-10-27 fix-94 wave; older docs say 35/63).
- Pipes/filter: `BUILDING_GTAIV` / `CAR_GTAIV` (not `CAR_NEO`) / `COLORFILTER_GTAIV`, `preset=IV_PC`, `ivMode=1`.
- Changes: IV water style + parallax + water quality 3; bloom pyramid (threshold 0.90, intensity 0.35, quality 2); DoF (focus 20, range 15, bokeh 4, max blur 0.5); sun shafts (density 0.84, decay 0.97); height fog on; god rays off; film grain 0.03; CA 0.12; standalone vignette pinned to 0; motion blur 0.35; SSAO 1.0/2.0; SMAA on, FXAA off; normal and velocity buffers on; tonemap ACES; radiosity/dual-pass/PS2 modulate off; safety four on.
- Does not change (deliberately inherited): `ivExposure`, `ivDesaturation`, `ivGamma`, `ivSaturation`, `ivCurves`, `ivVignette*`, `ivBloomIntensity`, `coronaZtest` (`src/core/modeManager.cpp:364-376`). The IV look comes from `DrawIVGrade` (`src/render/postfx_colourfilter.cpp:1354-1547`), not from `COLORFILTER_GTAIV`, which is a bypass (`src/render/postfx.cpp:1842-1850`).
- Switch: `active=GTAIV`. Log: `[Mode] apply GTA IV pipes=4/2/9 cf=7 iv=1`.
- Known gaps: IV owns no `ivDesaturation`/vignette/bloom-depth cleanup; the audit's P1 cleanup (zeroing the iv* knobs) is not implemented. The standalone vignette is pinned off so it does not double with `DrawIVGrade`. All "probe" values in the profile are unverified heuristics; see source comments `// IV probe`.

## GTA V

- Identity: `MODE_GTAV`, alias `GTAV`, `kValuesGTAV` (`src/core/modeManager.cpp:545`), **3 fields**.
- Changes: `waterStyle=2` (Water_V / GTA V), `waterQuality=3` (Ultra), `waterParallaxEnable=1` (the V water pipe master gate). That is the entire profile.
- Does not change: pipes, colour filter, postFX, alpha, env maps, shadows. Entering GTA V from Modern leaves PBR pipes and every Modern postFX running; only water changes (`src/core/modeManager.cpp:545-550`).
- Switch: `active=GTAV`. `modeOwnsPipes()` returns false for GTAV, so top-level pipe keys still apply.
- Known gaps: there is no GTA V pipe set in the tree; `PIPELINE` stops at `GTAIV`. No V vehicle/building shader exists.

## Modern

- Identity: `MODE_MODERN`, alias `Modern`, `kValuesModern` (`src/core/modeManager.cpp:557`), 104 fields.
- Pipes/filter: `BUILDING_PBR` / `CAR_MODERN` / `COLORFILTER_MODERN`, `preset=SKYGFXPLUSULTRAMAXDELUXE`.
- Changes: PBR building + vehicle; detail maps, noise detail, dual-pass; 512 env map, far clip 2.0; radiosity; SSAO; SMAA; motion blur; SSS post; skin/hair/vegetation enhancement; IV grade base; auto exposure; IBL weight 0.5; Forward+; normal buffer; velocity buffer; depth hook; profiler; alpha contracts.
- Does not change: water style/tuning (inherited), `pipeChainEnable` is owned = 0, grass instancing/tree wind/tree FX owned = 0, `vcsTrails=0`.
- Switch: `active=Modern`.
- Known gaps: `pipeChainEnable` is off in the profile; PipeChain reflections are implemented but not part of the Modern preset.

## Custom

- Identity: `MODE_CUSTOM`, alias `Custom`, **no value table** (`nullptr, 0` at `src/core/modeManager.cpp:701`).
- Changes: nothing. Entering Custom snapshots the slot base; leaving restores it.
- Does not change: everything. It is a bookmark for a hand-tuned config, not a preset.
- Switch: `active=Custom` or the Custom row. Pipes are not owned, so top-level keys apply.
- Known gaps: no persistence of the "custom" fields beyond the normal INI save; no per-mode overrides exist.

## Mobile (first-class, 70 fields)

- Identity: `MODE_MOBILE`, alias `Mobile`, `kValuesMobile` (`src/core/modeManager.cpp:708`), **70 owned fields**. It is the Mobile chip in the eight-chip picker (`src/core/debugmenu_ui.cpp:3850`), not a hidden alias. The source comment at `modeManager.cpp:697-707` also calls it a first-class 8-chip picker mode; its owned-field count is stale (the table has 70).
- Pipes/filter: `PIPELINE_MOBILE` mirror, `BUILDING_XBOX`, `CAR_MOBILE`, `COLORFILTER_MOBILE`, `ivMode=0`. `preset` is deliberately not owned (there is no Mobile `GamePreset`).
- Profile: detail maps on, dual-pass building/vehicle on, PC timecycle on, `noiseDetail=0` (the Mobile vehicle shader does its own diffuse detail), 256 env map, water style 0, coronas z-test on; radiosity, grain, SSAO, SMAA, motion blur, SSS, skin/hair/vegetation enhancement, normal maps, bloom, DoF, sun shafts, FXAA, VCS trails, Forward+, PipeChain and the normal buffer all off. `depthHookEnable=1`; the four safety contracts stay on.
- Switch: `active=Mobile` or the Mobile chip.
- Known gaps: the Mobile building path reuses the Xbox building pipe; only the vehicle pipe and colour filter are Mobile-specific.


## Multi-config caveat

The pre-mode base is stored per `configs[]` slot (`s_baseConfig[MODE_CONFIG_SLOTS]`, `src/core/modeManager.cpp:31-34`). `setConfig()` can repoint the live `config` to another slot, and a mode applied there uses that slot's own base. Consequences:

- Switching config slots while a mode is active does not transfer the snapshot; the new slot has its own (possibly empty) base.
- `configSlot()` returns `-1` for a Config that is not one of the slots; `ApplyMode` then works on the live Config without a restore base (`src/core/modeManager.cpp:744-752`).
- INI reload (F11) re-reads keys into the active config and re-applies the INI mode once (`src/core/main.cpp:6350-6354`). It does not re-capture a base for other slots.

## Common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| Switching to GTA IV leaves bloom/SSAO/SMAA off | Base snapshot had them off; IV owns them and sets them on — but only if the profile actually ran | check `[Mode] apply` line and `iv=1` |
| Switching to GTA V changes nothing visible | GTA V owns only water; pipes/postFX inherit | look at water, not buildings |
| Leaving a mode does not restore your settings | You changed the config via menu while in the mode (live edit overwrote the base), or `ApplyPreset` ran and cleared the latch | re-enter `None`, re-apply your INI, avoid preset buttons while in a mode |
| `pipeline` shows a different number than the pipes | `pipelineOverride` or a menu pipeline apply ran after the profile | set `pipelineOverride=-1`, reload |
| Custom behaves like None | Custom has no field table; it only snapshots/restores | expected |

## Verification receipts

One apply line per change (`src/core/modeManager.cpp:873-878`):

```
[Mode] apply <name> pipes=<pipeline>/<buildingPipe>/<vehiclePipe> cf=<colorFilter> iv=<ivMode>
```

The first number is the derived `pipeline` **mirror**, not a building pipe. Example: Modern = pipeline 0 / building 3 / vehicle 10, `cf=8`. IV = pipeline 4 / building 2 / vehicle 9, `cf=7`.

One load receipt per process (`src/core/main.cpp:7007-7009`):

```
[Mode] active=<alias> (from INI)
[Mode] active=none
```

Healthy switch sequence: `[Mode] active=GTAIV (from INI)` once, then one `[Mode] apply ... iv=1` per apply. A missing apply line means the menu path never called `ApplyMode` (or the id was out of range — `[mode] ApplyMode(%d) out of range - ignored`). A mismatch between the four numbers after `pipes=` and the mode table means a later writer (pipelineOverride, qualityPreset 0-2, ApplyPreset) ran after the profile.

Full log workflow: [debugging.md](debugging.md).
