# Integrate expIV Changes & Adopt Subdirectory Structure

> **For agentic workers:** REQUIRED SUB-SKILL: Use compose:subagent (recommended) or compose:execute to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize `src/` into subdirectories matching expIV's layout, merge stable additions from expIV, keep hooks.cpp and diagnostics.cpp as separate files (do NOT inline), apply same structure to expIV.

**Architecture:** Move flat `src/` into `core/`, `entities/`, `extras/`, `render/`, `rw/` subdirectories. Keep hooks.cpp, diagnostics.cpp, debugmenu_ui.cpp separate in `src/core/`. Merge crash guard improvements from expIV into main.cpp. Copy restructured layout back to expIV. Only add stable, tested additions.

**Tech Stack:** C++, premake5, GTA SA modding (RenderWare SDK)

## Global Constraints

- Target platform: Windows x86 (GTA SA 1.0 US)
- Build system: premake5 + Visual Studio
- All `#include "..."` paths must resolve with `includedirs { "src" }` in premake5
- **Keep hooks.cpp and diagnostics.cpp as separate files** — do NOT inline into main.cpp
- Only merge **stable** additions from expIV (crash handling, config.cpp)
- Apply same restructured layout to expIV repo (`E:\Dev(dave)\skygfx_plus_expIV`)
- Don't overwrite experimental's crash guard improvements from recent commits

---

## File Mapping (expIV → experimental)

### `src/core/` (move from src/)
| experimental file | expIV location |
|---|---|
| `main.cpp` | `core/main.cpp` |
| `weather.cpp` | `core/weather.cpp` |
| `weather.h` | `core/weather.h` |
| `presets.cpp` | `core/presets.cpp` |
| `test.cpp` | `core/test.cpp` |
| *(new)* | `core/config.cpp` |

### `src/entities/` (move from src/)
| experimental file | expIV location |
|---|---|
| `chars.cpp` | `entities/chars.cpp` |
| `chars.h` | `entities/chars.h` |
| `PlantSurfPropMgr.h` | `entities/PlantSurfPropMgr.h` |

### `src/extras/` (move from src/)
| experimental file | expIV location |
|---|---|
| `wheels.cpp` | `extras/wheels.cpp` |
| `wheels.h` | `extras/wheels.h` |
| `wheels_extender.cpp` | `extras/wheels_extender.cpp` |
| `wheels_extender.h` | `extras/wheels_extender.h` |
| `veh_shaders.cpp` | `extras/veh_shaders.cpp` |
| `vehicles.cpp` | `extras/vehicles.cpp` |
| `texdb.cpp` | `extras/texdb.cpp` |
| `debugmenu_public.h` | `extras/debugmenu_public.h` |
| `brdfLibrary.h` | `extras/brdfLibrary.h` |

### `src/render/` (move from src/)
| experimental file | expIV location |
|---|---|
| `buildingPipe.cpp` | `render/buildingPipe.cpp` |
| `postfx.cpp` | `render/postfx.cpp` |
| `postfx.h` | `render/postfx.h` |
| `vehiclePipe.cpp` | `render/vehiclePipe.cpp` |
| `envmap.cpp` | `render/envmap.cpp` |
| `SMAA.cpp` | `render/SMAA.cpp` |
| `SMAA.h` | `render/SMAA.h` |
| `pipelinecommon.cpp` | `render/pipelinecommon.cpp` |
| `PC_PlantsMgr.cpp` | `render/PC_PlantsMgr.cpp` |
| `PC_PlantsMgr.h` | `render/PC_PlantsMgr.h` |
| `PC_PlantsMgr_overlay.cpp` | `render/PC_PlantsMgr_overlay.cpp` |
| `PC_GrassRenderer.cpp` | `render/PC_GrassRenderer.cpp` |
| `PC_GrassRenderer.h` | `render/PC_GrassRenderer.h` |
| `neoCarpipe.cpp` | `render/neoCarpipe.cpp` |
| `neoWaterdrops.cpp` | `render/neoWaterdrops.cpp` |
| `waterPipe.cpp` | `render/waterPipe.cpp` |
| `waterPipe.h` | `render/waterPipe.h` |
| `mobile.cpp` | `render/mobile.cpp` |
| `iv_mode.cpp` | `render/iv_mode.cpp` |
| `ps2_mode.cpp` | `render/ps2_mode.cpp` |
| `xbox_mode.cpp` | `render/xbox_mode.cpp` |
| `custom_mode.cpp` | `render/custom_mode.cpp` |
| `pc_patched_mode.cpp` | `render/pc_patched_mode.cpp` |
| `PostFXPipeline.h` | `render/PostFXPipeline.h` |
| `AreaTex.h` | `render/AreaTex.h` |
| `SearchTex.h` | `render/SearchTex.h` |
| `EdgeDetection.h` | `render/EdgeDetection.h` |
| `GrassSystem.h` | `render/GrassSystem.h` |

### `src/rw/` (move from src/)
| experimental file | expIV location |
|---|---|
| `gta.cpp` | `rw/gta.cpp` |
| `gta.h` | `rw/gta.h` |
| `extendedplg.cpp` | `rw/extendedplg.cpp` |
| `HLSL_hook.cpp` | `rw/HLSL_hook.cpp` |
| `HLSL_hook.h` | `rw/HLSL_hook.h` |
| `normalmap.cpp` | `rw/normalmap.cpp` |
| `normalmap_plugin.cpp` | `rw/normalmap_plugin.cpp` |
| `normmap_stubs.cpp` | `rw/normmap_stubs.cpp` |
| `pipeplg.cpp` | `rw/pipeplg.cpp` |
| `defaultFuncs.cpp` | `rw/defaultFuncs.cpp` |
| `ColData.h` | `rw/ColData.h` |
| `LinkList.h` | `rw/LinkList.h` |
| `MemoryMgr.h` | `rw/MemoryMgr.h` |
| `Pools.h` | `rw/Pools.h` |
| `ModuleList.hpp` | `rw/ModuleList.hpp` |

### Root `src/` (keep in place)
| file | notes |
|---|---|
| `skygfx.h` | stays at src/ root |
| `neo.cpp` | stays at src/ root |
| `neo.h` | stays at src/ root |
| `shaders.h` | stays at src/ root |
| `main_exports.h` | stays at src/ root |
| `skygfx_bridge.cpp` | stays at src/ root |
| `__init__.py` | stays |
| Python files | stay |

### Experimental-only (keep, not in expIV)
| file | notes |
|---|---|
| `hooks.cpp` | merged into main.cpp in expIV, but experimental has standalone version |
| `hooks.h` | same |
| `diagnostics.cpp` | inlined in expIV, keep as separate file |
| `diagnostics.h` | same |
| `debugmenu_ui.cpp` | excluded from build, keep for future ImGui integration |
| `Core.cpp` | replaced by config.cpp from expIV |
| `Core/` directory | check if it has anything |

---

## Tasks

### Task 1: Create subdirectory structure

**Files:**
- Create: `src/core/`, `src/entities/`, `src/extras/`, `src/render/`, `src/rw/`

- [ ] **Step 1: Create directories**

```powershell
New-Item -ItemType Directory -Path "src\core" -Force
New-Item -ItemType Directory -Path "src\entities" -Force
New-Item -ItemType Directory -Path "src\extras" -Force
New-Item -ItemType Directory -Path "src\render" -Force
New-Item -ItemType Directory -Path "src\rw" -Force
```

- [ ] **Step 2: Verify directories exist**

Run: `dir src\`
Expected: 5 new subdirectories visible

- [ ] **Step 3: Commit**

```bash
git add src/core src/entities src/extras src/render src/rw
git commit -m "chore: create subdirectory structure matching expIV layout"
```

---

### Task 2: Move files to subdirectories

**Files:** Move ~55 files from flat src/ into subdirectories per the mapping table above.

- [ ] **Step 1: Move core/ files**

```powershell
Move-Item "src\main.cpp" "src\core\main.cpp"
Move-Item "src\weather.cpp" "src\core\weather.cpp"
Move-Item "src\weather.h" "src\core\weather.h"
Move-Item "src\presets.cpp" "src\core\presets.cpp"
Move-Item "src\test.cpp" "src\core\test.cpp"
```

- [ ] **Step 2: Move entities/ files**

```powershell
Move-Item "src\chars.cpp" "src\entities\chars.cpp"
Move-Item "src\chars.h" "src\entities\chars.h"
Move-Item "src\PlantSurfPropMgr.h" "src\entities\PlantSurfPropMgr.h"
```

- [ ] **Step 3: Move extras/ files**

```powershell
Move-Item "src\wheels.cpp" "src\extras\wheels.cpp"
Move-Item "src\wheels.h" "src\extras\wheels.h"
Move-Item "src\wheels_extender.cpp" "src\extras\wheels_extender.cpp"
Move-Item "src\wheels_extender.h" "src\extras\wheels_extender.h"
Move-Item "src\veh_shaders.cpp" "src\extras\veh_shaders.cpp"
Move-Item "src\vehicles.cpp" "src\extras\vehicles.cpp"
Move-Item "src\texdb.cpp" "src\extras\texdb.cpp"
Move-Item "src\debugmenu_public.h" "src\extras\debugmenu_public.h"
Move-Item "src\brdfLibrary.h" "src\extras\brdfLibrary.h"
```

- [ ] **Step 4: Move render/ files**

```powershell
Move-Item "src\buildingPipe.cpp" "src\render\buildingPipe.cpp"
Move-Item "src\postfx.cpp" "src\render\postfx.cpp"
Move-Item "src\postfx.h" "src\render\postfx.h"
Move-Item "src\vehiclePipe.cpp" "src\render\vehiclePipe.cpp"
Move-Item "src\envmap.cpp" "src\render\envmap.cpp"
Move-Item "src\SMAA.cpp" "src\render\SMAA.cpp"
Move-Item "src\SMAA.h" "src\render\SMAA.h"
Move-Item "src\pipelinecommon.cpp" "src\render\pipelinecommon.cpp"
Move-Item "src\PC_PlantsMgr.cpp" "src\render\PC_PlantsMgr.cpp"
Move-Item "src\PC_PlantsMgr.h" "src\render\PC_PlantsMgr.h"
Move-Item "src\PC_PlantsMgr_overlay.cpp" "src\render\PC_PlantsMgr_overlay.cpp"
Move-Item "src\PC_GrassRenderer.cpp" "src\render\PC_GrassRenderer.cpp"
Move-Item "src\PC_GrassRenderer.h" "src\render\PC_GrassRenderer.h"
Move-Item "src\neoCarpipe.cpp" "src\render\neoCarpipe.cpp"
Move-Item "src\neoWaterdrops.cpp" "src\render\neoWaterdrops.cpp"
Move-Item "src\waterPipe.cpp" "src\render\waterPipe.cpp"
Move-Item "src\waterPipe.h" "src\render\waterPipe.h"
Move-Item "src\mobile.cpp" "src\render\mobile.cpp"
Move-Item "src\iv_mode.cpp" "src\render\iv_mode.cpp"
Move-Item "src\ps2_mode.cpp" "src\render\ps2_mode.cpp"
Move-Item "src\xbox_mode.cpp" "src\render\xbox_mode.cpp"
Move-Item "src\custom_mode.cpp" "src\render\custom_mode.cpp"
Move-Item "src\pc_patched_mode.cpp" "src\render\pc_patched_mode.cpp"
Move-Item "src\PostFXPipeline.h" "src\render\PostFXPipeline.h"
Move-Item "src\AreaTex.h" "src\render\AreaTex.h"
Move-Item "src\SearchTex.h" "src\render\SearchTex.h"
Move-Item "src\EdgeDetection.h" "src\render\EdgeDetection.h"
Move-Item "src\GrassSystem.h" "src\render\GrassSystem.h"
```

- [ ] **Step 5: Move rw/ files**

```powershell
Move-Item "src\gta.cpp" "src\rw\gta.cpp"
Move-Item "src\gta.h" "src\rw\gta.h"
Move-Item "src\extendedplg.cpp" "src\rw\extendedplg.cpp"
Move-Item "src\HLSL_hook.cpp" "src\rw\HLSL_hook.cpp"
Move-Item "src\HLSL_hook.h" "src\rw\HLSL_hook.h"
Move-Item "src\normalmap.cpp" "src\rw\normalmap.cpp"
Move-Item "src\normalmap_plugin.cpp" "src\rw\normalmap_plugin.cpp"
Move-Item "src\normmap_stubs.cpp" "src\rw\normmap_stubs.cpp"
Move-Item "src\pipeplg.cpp" "src\rw\pipeplg.cpp"
Move-Item "src\defaultFuncs.cpp" "src\rw\defaultFuncs.cpp"
Move-Item "src\ColData.h" "src\rw\ColData.h"
Move-Item "src\LinkList.h" "src\rw\LinkList.h"
Move-Item "src\MemoryMgr.h" "src\rw\MemoryMgr.h"
Move-Item "src\Pools.h" "src\rw\Pools.h"
Move-Item "src\ModuleList.hpp" "src\rw\ModuleList.hpp"
```

- [ ] **Step 6: Verify structure**

Run: `dir /s /b src\*.cpp src\*.h src\*.hpp`
Expected: all files now under subdirectories, only skygfx.h, neo.cpp, neo.h, shaders.h, main_exports.h, skygfx_bridge.cpp, and Python files remain at root

- [ ] **Step 7: Commit**

```bash
git add -A src/
git commit -m "refactor: move source files into subdirectory structure (core/entities/extras/render/rw)"
```

---

### Task 3: Update premake5.lua for subdirectories

**Files:**
- Modify: `premake5.lua`

The current `files { "src/*.*" }` only grabs top-level files. Need to recurse into subdirectories.

- [ ] **Step 1: Update files glob**

Replace line 15 in premake5.lua:
```lua
-- Old:
files { "src/*.*" }

-- New:
files { "src/*.*" }
files { "src/core/*.*" }
files { "src/entities/*.*" }
files { "src/extras/*.*" }
files { "src/render/*.*" }
files { "src/rw/*.*" }
```

- [ ] **Step 2: Commit**

```bash
git add premake5.lua
git commit -m "build: update premake5 to include src subdirectories"
```

---

### Task 4: Add config.cpp from expIV

**Files:**
- Create: `src/core/config.cpp` (copy from expIV)
- Remove: `src/Core.cpp` (replaced by config.cpp)

- [ ] **Step 1: Copy config.cpp from expIV**

Copy `E:\Dev(dave)\skygfx_plus_expIV\src\core\config.cpp` to `src/core/config.cpp`

- [ ] **Step 2: Compare Core.cpp vs config.cpp**

Read both files to understand what changed. The expIV version likely has:
- Same `readIni()` function with pipeline mapping
- Same `Config` struct usage
- May have additional crash handling

- [ ] **Step 3: Remove old Core.cpp**

```powershell
Remove-Item "src\Core.cpp"
```

- [ ] **Step 4: Commit**

```bash
git add src/core/config.cpp
git rm src/Core.cpp
git commit -m "refactor: replace Core.cpp with config.cpp from expIV"
```

---

### Task 5: Merge code changes into main.cpp

**Files:**
- Modify: `src/core/main.cpp`

Key changes from expIV to merge:
1. Add `g_allowCrashPassThrough` and `crashCount` globals
2. Add crash pass-through logic in `InitialiseGame_hook`
3. Add debug logging in `refreshIni`

- [ ] **Step 1: Add crash handling globals**

After line 59 (`int original_bRadiosity = 0;`), add:
```cpp
static volatile int g_allowCrashPassThrough = 0;
static volatile int crashCount = 0;
```

- [ ] **Step 2: Add crash pass-through in InitialiseGame_hook**

Find the `__try` block in `InitialiseGame_hook` and add `g_allowCrashPassThrough = 1;` before it, and `g_allowCrashPassThrough = 0;` after `InitialiseGame()` returns and in the `__except` block.

- [ ] **Step 3: Add debug logging in refreshIni**

In the `refreshIni` function, add `dbglog` calls around `resetValues()` and `refreshMenu()`.

- [ ] **Step 4: Commit**

```bash
git add src/core/main.cpp
git commit -m "feat: merge crash handling and debug logging from expIV"
```

---

### Task 6: Handle experimental-only files (keep separate)

**Files:**
- Move to `src/core/`: `hooks.cpp`, `hooks.h`, `diagnostics.cpp`, `diagnostics.h`, `debugmenu_ui.cpp`
- **Do NOT inline these into main.cpp** — they stay as separate compilation units

- [ ] **Step 1: Move hooks to core/**

hooks.cpp contains `InstallAllHooks()` and `InjectDelayedPatches()`. Keep separate from main.cpp.

```powershell
Move-Item "src\hooks.cpp" "src\core\hooks.cpp"
Move-Item "src\hooks.h" "src\core\hooks.h"
```

- [ ] **Step 2: Move diagnostics to core/**

```powershell
Move-Item "src\diagnostics.cpp" "src\core\diagnostics.cpp"
Move-Item "src\diagnostics.h" "src\core\diagnostics.h"
```

- [ ] **Step 3: Move debugmenu_ui to core/**

```powershell
Move-Item "src\debugmenu_ui.cpp" "src\core\debugmenu_ui.cpp"
```

- [ ] **Step 4: Commit**

```bash
git add src/core/hooks.cpp src/core/hooks.h src/core/diagnostics.cpp src/core/diagnostics.h src/core/debugmenu_ui.cpp
git commit -m "refactor: move experimental-only files to core/ (keep hooks/diagnostics separate)"
```

---

### Task 7: Verify no include breakage

**Files:** All .cpp and .h files with `#include "..."` directives

The premake5.lua has `includedirs { "src" }`, so `#include "skygfx.h"` resolves from `src/`. Files in subdirectories using `#include "gta.h"` need `includedirs { "src/rw" }` or the include must be `#include "rw/gta.h"`.

- [ ] **Step 1: Check all include directives**

Search for all `#include "..."` in the moved files. Verify each resolves correctly with the premake include dirs:
- `src/` (already in includedirs)
- Files at root: skygfx.h, neo.h, shaders.h, main_exports.h, skygfx_bridge.cpp
- Files in subdirs: need `includedirs` entries or relative paths

- [ ] **Step 2: Add missing include dirs if needed**

If any includes break, add to premake5.lua:
```lua
includedirs { "src/core" }
includedirs { "src/entities" }
includedirs { "src/extras" }
includedirs { "src/render" }
includedirs { "src/rw" }
```

- [ ] **Step 3: Commit if changes needed**

```bash
git add premake5.lua
git commit -m "build: add subdirectory include paths for correct header resolution"
```

---

### Task 8: Final verification

- [ ] **Step 1: Verify file count matches**

Run: `dir /s /b src\*.cpp src\*.h src\*.hpp | find /c /v ""`
Expected: should match total from both repos combined (minus removed Core.cpp)

- [ ] **Step 2: Verify no orphaned files at src/ root**

Run: `dir src\*.cpp src\*.h`
Expected: only skygfx.h, neo.cpp, neo.h, shaders.h, main_exports.h, skygfx_bridge.cpp, and Python files

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "chore: complete integration of expIV subdirectory structure"
```

---

### Task 9: Copy restructured layout to expIV

**Files:**
- Destination: `E:\Dev(dave)\skygfx_plus_expIV\src\`

Copy the restructured `src/` directory from experimental to expIV so both repos share the same subdirectory layout. This allows fixing building issues in expIV with the proper folder structure.

- [ ] **Step 1: Sync src/ to expIV**

```powershell
# Copy experimental's restructured src/ over expIV's src/
# This brings the organized layout + experimental-only files (hooks, diagnostics, debugmenu_ui)
robocopy "C:\Dev\shaisse_hub\skygfx_plus\src" "E:\Dev(dave)\skygfx_plus_expIV\src" /MIR /XD .git
```

- [ ] **Step 2: Copy updated premake5.lua to expIV**

```powershell
Copy-Item "C:\Dev\shaisse_hub\skygfx_plus\premake5.lua" "E:\Dev(dave)\skygfx_plus_expIV\premake5.lua" -Force
```

- [ ] **Step 3: Verify expIV structure**

Run: `dir /s /b "E:\Dev(dave)\skygfx_plus_expIV\src\*.cpp" | find /c /v ""`
Expected: should match experimental's count

- [ ] **Step 4: Commit in expIV**

```bash
cd "E:\Dev(dave)\skygfx_plus_expIV"
git add -A src/
git commit -m "refactor: adopt subdirectory structure from experimental branch"
```
