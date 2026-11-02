# SkyGFX Plus - Future Features (Deferred)

Features NOT in the active roadmap. Statuses reconciled 2026-10-31 against docs/FEATURE_LIST.md
(authoritative), TODO.md and premake5.lua. Authoritative deferred list: docs/FEATURE_LIST.md §11.

## ImGui Debug Menu
- **Status:** LANDED (updated 2026-10-31)
- **Dependencies:** ImGui SDK (external\imgui, merged into repo tree)
- **Notes:** debugmenu_ui.cpp is an active compiled TU (~4,300 LOC; premake5.lua:233) with
  debugmenu_theme.cpp. Features live: All Settings tab (235 rows, search/reset/copy-as-INI),
  diagnostics, Atmospheric header (13 sliders),   mode chips + MODE pill rows (Wave 8).
  Pending defects tracked in docs/plans/2026-10-24-imgui-settings-coverage.md
  (parser/save/clamp fixes partially landed).

## Normal Map Plugin Integration (txd `_n`)
- **Status:** GROUNDWORK LANDED; final `_n` wiring is DEFERRED-LAST (see TODO.md golden rule §C)
- **Dependencies:** DK22Pac rwnormal SDK — `rpnormmap.lib` is LINKED (premake5.lua:353)
- **Notes:** src/texdefs.cpp (unified texture-def layer: texdb + normalmap + plugin registry,
  normalmap.cpp/plugin TUs merged away) and src/normmap_stubs.cpp landed in the Wave-8 merge.
  What remains deferred-LAST: the actual `_n` texture wiring into the render shaders.

## Multi-pass Vehicle Glass with Parallax
- **Status:** Partially implemented
- **Notes:** Glass shader exists, needs POM integration for lens details

## Collision-based Edge Detection
- **Status:** Not started
- **Notes:** Would improve SMAA edge detection using collision geometry
