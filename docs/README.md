# SkyGFX Plus

GTA San Andreas ASI plugin that replaces the game's RenderWare 3.6 D3D9 building, vehicle, water and post-process paths to reproduce the PS2, Xbox, Fixed PC (Legacy PC), GTA IV, GTA V and Modern (PBR) looks. RW 3.6 is baked into `gta_sa.exe`; every RenderWare call goes through wrapper/EAXJMP thunks to hardcoded addresses (`src/rw/gta.cpp`), never through an external RW SDK.

Current state: **experimental build, not a release**. Treat every mode profile as a probe until verified on your install (see [state of the build](#state-of-the-build)).

## What it is

| Fact | Value |
|---|---|
| Artifact | `skygfx.asi` (built as `build\Release\skygfx.dll`) |
| Host | GTA SA v1.0 US, D3D9, shader model **3.0 only** (`ps_3_0`/`vs_3_0`) |
| Config | `skygfx.ini` next to the exe — sections `[SkyGfx]`, `[Modes]`, `[General]` (`src/core/main.cpp:5106`, `:5146`, `:5157`, `:5168`) |
| Render hooks | RW 3.6 pipeline callbacks + game functions (`0x5D7100` building pipe, `0x5D9FE9` vehicle pipe, `0x704D1E` colour filter, `0x53EBE9` DrawFinalEffects) |
| Log | `skygfx_dbg.log` beside the ASI (path derived from `GetModuleFileNameA`, `src/core/main.cpp:7242-7244`) |

## Build and deploy

```bash
python tools/fast_build.py            # default: detect + compile changed shaders + MSBuild + deploy
python tools/fast_build.py --shaders  # HLSL -> CSO only
python tools/fast_build.py --fastest  # skip detection: compile + build + deploy
python tools/fast_build.py --rebuild  # clean full rebuild + deploy
python tools/fast_build.py --launch   # build then start the game
```

Pipeline: `shaders/ps|vs/*.hlsl` → FXC (DirectX SDK June 2010) → `resources/cso/*.cso` → embedded via `resources/resource.rc` → MSBuild `OutDir` `E:\games\gtasa_skygfx_plus\` (the post-build copies the DLL to `skygfx.asi` and deletes the DLL, `build/skygfx.vcxproj:46,91-93`; `tools/fast_build.py:58` sets `GAME_DIR`, override with `SKYGFX_GAME`).

Do not hand-copy a DLL built elsewhere: `[BUILD] asipath= size= mtime= built=` in the log is the only proof of which binary is running (`src/core/main.cpp:7054`).

## Install

1. ASI loader installed in the game directory.
2. `skygfx.asi` in the game directory.
3. `skygfx.ini` in the game directory (optional; defaults are compiled in).
4. No `presets/` folder is shipped or read. Mode selection is `[Modes] active=<alias>` (or the in-menu chip row), not a preset file.

Three INI sections, three purposes:

- `[SkyGfx]` — legacy per-feature keys (pipes, postFX toggles, tuning).
- `[Modes]` — exactly one key, `active=PS2|Xbox|PC|GTAIV|GTAV|Modern|Custom|Mobile|None`.
- `[General]` — global knobs that are not per-config, currently `NoiseQuality` (`src/core/main.cpp:5157`).

## Modes

Exactly one mode is active at a time. A mode is a **delta profile** over a snapshot of your pre-mode config, not a complete preset — see [modes.md](modes.md) for owned vs inherited fields, and [feature-matrix.md](feature-matrix.md) for the effect grid.

| Mode | `[Modes] active=` | Building pipe | Vehicle pipe | Colour filter | Purpose | Status |
|---|---|---|---|---|---|---|
| None | `None` | top-level keys | top-level keys | top-level keys | legacy/pre-mode path | shipped |
| PS2 | `PS2` | `BUILDING_PS2` | `CAR_PS2` | `COLORFILTER_PS2` | PS2 hardware look | shipped, 81 owned fields |
| Xbox | `Xbox` | `BUILDING_XBOX` | `CAR_XBOX` | `COLORFILTER_PC` | Xbox hardware look | shipped, 81 owned fields |
| Fixed PC (Legacy PC) | `PC` | `BUILDING_XBOX` | `CAR_PC` | `COLORFILTER_PC` | original PC renderer; **no PC building pipe exists** | partial, 50 owned fields |
| GTA IV | `GTAIV` | `BUILDING_GTAIV` | `CAR_GTAIV` | `COLORFILTER_GTAIV` | IV grade + water + SM3 postFX | partial (probe values), 70 owned fields |
| GTA V | `GTAV` | inherited | inherited | inherited | **water only** | partial (3 owned fields) |
| Modern | `Modern` | `BUILDING_PBR` | `CAR_MODERN` | `COLORFILTER_MODERN` | PBR everything | shipped, 104 owned fields |
| Custom | `Custom` | top-level keys | top-level keys | top-level keys | snapshot/restore only | shipped (0 owned fields) |
| Mobile | `Mobile` | `BUILDING_XBOX` | `CAR_MOBILE` | `COLORFILTER_MOBILE` | Mobile/PSP-era pipe mapping | first-class, 70 owned fields |

Field counts measured from `kValues*` in `src/core/modeManager.cpp` (PS2 81, Xbox 81, Legacy PC 50, GTA IV 70, GTA V 3, Modern 104, Mobile 70, None/Custom 0).

Aliases are case-insensitive via `StrAssoc` (`src/core/main.cpp:5315-5327`). The in-menu picker shows **eight chips** in this order: PS2 · Xbox · PC · Mobile · GTA IV · GTA V · Modern · Custom. Mobile is a first-class mode, not a hidden stub. The six per-mode preset writers (`ps2_mode.cpp`, `xbox_mode.cpp`, `pc_patched_mode.cpp`, `iv_mode.cpp`, `custom_mode.cpp`, `mobile.cpp`) are retired no-ops; `ModeManager` is the only mode authority.

## Requirements

- GTA San Andreas v1.0 US (the RW 3.6 addresses are v1.0-specific).
- ASI loader (any 1.x).
- DirectX 9 runtime; d3d9.dll from the game install (DXVK works — see the DXVK notes in `docs/DXVK Compatibility.md`, unverified for this build).
- 32-bit process; the DLL is Win32.
- No external RenderWare SDK, no shader compiler at runtime (CSOs are embedded).

## Minimal skygfx.ini

```ini
[SkyGfx]
pipeline=PBR
qualityPreset=3

[Modes]
active=Modern

[General]
NoiseQuality=2
```

Everything else has a compiled default. `pipeline` and the pipe keys are ignored while a pipe-owning mode is active; `qualityPreset` only changes a feature base, not the mode.

## Verification quickstart

1. Start the game, open `skygfx_dbg.log`.
2. Confirm `[BUILD] asipath=... size=... mtime=... built=...` matches the DLL you just built.
3. Confirm `[Mode] active=<alias> (from INI)` matches `[Modes] active=`.
4. Confirm one `[Mode] apply <name> pipes=<b>/<v>/<c> iv=<n>` with the pipe numbers from [modes.md](modes.md).
5. Only then judge the picture. Full loop: [debugging.md](debugging.md).

## Project layout

```
src/core/        entry, INI, mode manager, presets, menu, weather, crash handler
src/render/      pipes, postFX, Forward+, water, env map, mirror, shadows, state monitor
src/rw/          RW 3.6 globals + WRAPPER/EAXJMP thunks
src/entities/    entity-level helpers
shaders/         HLSL sources (ps/, vs/, include/)
resources/cso/   compiled shaders embedded by resource.rc
tools/           fast_build.py and support scripts
docs/            this documentation set + plans/research archives
```

## Non-goals

- No RenderWare 3.7 upgrade, no SM4/SM5, no Vulkan/D3D11 path.
- No replacement of the game's RW 3.6 SDK; hooks only.
- No redistributed Rockstar assets.
- No runtime shader compilation.

## Documentation index

| Doc | Contents |
|---|---|
| [modes.md](modes.md) | per-mode owns/inherits/switch/log receipt/known gaps |
| [architecture.md](architecture.md) | subsystem map, per-frame hook order, L1/L2/L3 state doctrine, pipe contracts, invariants |
| [feature-matrix.md](feature-matrix.md) | effect × mode grid, INI gate per effect, log tag per effect |
| [debugging.md](debugging.md) | log tags, build identity, INI bisect, crash recovery, evidence loop |

### Reading a receipt

A healthy change looks like this, in order, once per process/apply:

```
[BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=... mtime=... built=...
[mode] manager init
[Mode] active=Modern (from INI)
[Mode] apply Modern pipes=0/3/10 cf=8 iv=0
```

`[Mode] apply` prints the numeric enums, not names: `pipes=<pipeline>/<buildingPipe>/<vehiclePipe> cf=<colorFilter> iv=<ivMode>` (`src/core/modeManager.cpp:873-878`). Compare against the enum tables in [modes.md](modes.md). If the numbers are right but the image is wrong, the bug is downstream of mode selection — go to [debugging.md](debugging.md).

Reserved (not yet written): `docs/reference/` subsystem refs, `docs/history/TIMELINE.md`, `docs/ROADMAP.md`.

## State of the build

> **WARNING — this is a probe build.** Mode profiles are being reconciled against the source of truth (`src/core/modeManager.cpp`). GTA V is water-only today. GTA IV owns probe values, not verified ones. Fixed PC has no PC building pipe. Several legacy INI keys are dead (`smaaPreset`, `smaaPredication`, `smaaTemporal`, `normalMapBuilding`, `normalMapVehicle`, `tagsBuildingPipe`, `presetFile`). Do not treat any doc claim as a guarantee without the log receipt in [debugging.md](debugging.md).

Known factual baselines that older docs get wrong:

- SM3.0 only — no `ps_2_0` entry points exist in the build (`tools/fast_build.py` has 30 `ps_3_0`/`vs_3_0` compile entries and zero `ps_2_0` entries).
- Default `pipeline=PBR` ⇒ fresh install runs `COLORFILTER_MODERN(8)`, not `PC`.
- Default `envFresnel=0.4`, clamped to `[0.0, 0.4]`.
- Default `envMapSize=512` (ULTRA base); PS2/Xbox/PC/IV profiles set 256.
- `tagsBuildingPipe` is derived (`src/core/main.cpp:5664`), never read from the INI.
- GTA IV profile sets `CAR_GTAIV`, not `CAR_NEO`.

## Credits

SkyGFX Plus is a fork/continuation of **SkyGFX** by **The_GTA** and contributors, itself derived from the PS2/Xbox rendering restorations for GTA San Andreas. PBR pipeline, modern postFX, Forward+ and mode system are additions of this project.

Third-party references used: RenderWare 3.6 SDK headers/symbol maps, GTA SA reverse-engineering community documentation, DirectX SDK (June 2010) FXC, Dear ImGui (menu), and the original Rockstar assets the game already ships. All trademarks belong to their owners; no Rockstar assets are redistributed.
