# Debug Tools

#debug #tools

## Overview
Runtime debug tools for development. The debug menu provides real-time configuration of all rendering features.

## Hotkeys
| Key | Action |
|-----|--------|
| F4 | Debug menu toggle (ImGui-based overlay with all config options) |
| F8 | Debug console allocation (opens Windows console for stdout/stderr) |

## Debug Menu
The debug menu (toggled via F4) provides access to all rendering configuration:
- **Pipeline selection** — Building pipe, vehicle pipe modes
- **Feature toggles** — Detail maps, stochastic texturing, grass, shadows, radiosity
- **Environment** — Env map size, far clip multiplier
- **Shininess/specularity** — Per-pipe multipliers (Neo, Leeds, Env)
- **Advanced** — Dual-pass settings, alpha threshold, PS2-modulate, infrared/night vision
- **Config switching** — Cycle between INI configs, reload INIs at runtime

The menu is implemented in `src/Core/debugmenu_ui.cpp` using the debugmenu_public.h API.

## Profiling
- `PerfTimer` — Frame profiling (internal, disabled by default)

## Crash Handler
- Smart crash recovery with auto-fixes
- NULL CColModel detection
- Model info refcount fixes

## Logging
- `dbglog()` — Runtime logging to debug output (visible in debug console when F8 is held at startup)

## See Also
- [[INI Configuration]] — All config fields
- [[Shader Architecture]] — Shader compilation and loading
