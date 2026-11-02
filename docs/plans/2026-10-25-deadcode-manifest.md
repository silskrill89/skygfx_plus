# Dead-Code Removal Manifest — 2026-10-25

**Repo:** `E:\dev(dave)\skygfx_plus_expIV`
**Purpose:** Executable, re-verified dead-code removal plan ordered by safety (apply top-down).
**Status:** VERIFIED AGAINST CURRENT TREE — no source or build file was modified by this audit; this document is the only file changed by this session.

> **Method:** Every candidate was re-checked against the current working tree with an explicit grep. The 2026-10-24 inventory was used only as a lead. Current line numbers are from this tree; apply build-file edits bottom-up because earlier edits shift later line numbers.
> **Rule:** No deletions performed. No source or build files edited.

## Summary

| # | Candidate | Current verdict | Safety | Size | Action |
|---|---|---|---|---|---|
| 1 | Six `src/render/*_mode.cpp` presets | DEAD: compiled, zero call sites | SAFE | 675 LOC / 21,691 B | Delete 6 TUs + 23-line `skygfx.h` block |
| 2 | `src/render/PC_PlantsMgr*.cpp` | DEAD TUs (excluded); **headers LIVE** | SAFE | 3,210 LOC / 90,566 B | Delete `.cpp` only; keep headers |
| 3 | `src/rw/normalmap_plugin.cpp` | DEAD: not compiled | SAFE | 1,314 LOC / 40,016 B | Delete file |
| 4 | `src/render/vegImposter.{cpp,h}` | DEAD: zero external refs/includes | SAFE | 1,070 LOC / 43,362 B | Delete both |
| 5 | `SMAA.cpp` / `SMAA.h` | DEAD: `.cpp` not compiled; `.h` only `.cpp` | SAFE | 551 LOC / 21,984 B | Delete both; keep `postfx_smaa.cpp` |
| 6 | `EdgeDetection.h`, `PostFXPipeline.h`, `GrassSystem.h`, `hooks.h` | DEAD: zero includes; not in build | SAFE | 377 LOC / 11,516 B | Delete 4 headers |
| 7 | `#if 0` blocks | DEAD: preprocessor-excluded | SAFE | 115 LOC / 4,093 B | Delete 3 blocks |
| 8 | Five `Pipeline_Get*` lazy getters | DEAD: zero call sites | SAFE (code) | 76 LOC / 2,314 B + decls | Delete getters/decls; resource purge is separate |
| 9 | Dead ped helpers in `chars.cpp` | DEAD: classification/init/shutdown have zero call sites | SAFE | 263 LOC / 9,631 B | Delete dead classification + init/shutdown |
| 10 | `src/extras/skygfx_bridge.cpp` | DEAD: not in project; zero refs | SAFE | 134 LOC / 2,720 B | Delete file |
| 11 | `src/core/test.cpp` + screens/dumpMenu | DEAD: compiled, zero refs | SAFE | 47 LOC / 922 B | Delete file + row |
| 12 | `HLSL_hook.{cpp,h}` | DEAD: compiled stubs, zero refs | SAFE | 45 LOC / 1,328 B | Delete both |
| 13 | Dead/never-bound `IDR_*` + orphan CSOs | PARTIAL: 16 orphan IDs; 18 unembedded outputs | NEEDS-BUILD-CHECK | 16 IDs / 16 CSOs (39,240 B) | Purge only after shader audit |
| 14 | `buildingPipe` ↔ `defaultFuncs` duplication | `defaultFuncs.cpp` appears wholly unreferenced; `DNInstance_PS2` is the live superset | NEEDS-BUILD-CHECK | 466 LOC / 19,786 B | Do not spot-dedupe; remove whole TU after build proof |
| 15 | Duplicated `MENUSETTINGS` macros | `main.cpp` copy DEAD; `debugmenu_ui.cpp` copy LIVE | SAFE | 74 LOC / 1,652 B | Delete `main.cpp:6844-6917` only |
| 16 | Orphan-shader audit | 18 CSOs no `IDR_*`; only `IDR_PEDPBRVS` has no loader; `CloudsPS` pending | NEEDS-BUILD-CHECK | 1,210 HLSL LOC / 43,978 B | Triage by category, do not bulk delete |

**No candidate became live.** The only live sub-parts are the `PC_PlantsMgr.h` header (included by live grass TUs), `IDR_RUBBER_VEHICLE` (used as a boolean gate), the `debugmenu_ui.cpp` `MENUSETTINGS` macro, and the live SMAA implementation in `postfx_smaa.cpp`/`AreaTex.h`/`SearchTex.h`.

---

## Safety classes

- **SAFE** — no runtime references; only direct file/row deletion (plus a header declaration cleanup where noted). Independently verifiable; build not strictly required to diagnose, but always run one.
- **NEEDS-BUILD-CHECK** — resource/build edits or structural removal that must be compiled/link-checked (and ideally launched) before being accepted.
- **RISKY** — touches active render paths or may be intentionally staged. None of the recommended top-down actions are in this class; do not invent risky deletions from this document.

---

## Tier 0 — not compiled / not in the project (SAFE)

### C3. `src/rw/normalmap_plugin.cpp` — DEAD

**Evidence**

```text
$ grep -n -E 'normalmap_plugin' build/skygfx.vcxproj build/skygfx.vcxproj.filters premake5.lua
build/skygfx.vcxproj:259:    <!-- <ClCompile Include="..\src\rw\normalmap_plugin.cpp" /> -->
premake5.lua:32:--       src/rw/normalmap_plugin.cpp   — 1,314 LOC vendored DK22Pac copy, dead
premake5.lua:92:--     * no ClCompile for normalmap_plugin.cpp / SMAA.cpp / skygfx_bridge.cpp
premake5.lua:173:	-- src/rw: glob, minus normalmap_plugin.cpp (dead DK copy)
premake5.lua:185:		"src/rw/normalmap_plugin.cpp",  -- 1,314 LOC vendored DK22Pac copy, dead
```

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\b(BuildPlayerNormalMapTextures|SetupAtomic)\b' src | grep -v '^src/rw/normalmap_plugin.cpp:' || echo 'NO_REFS'
NO_REFS
```

`nmPluginAttach` is a different, live API in `src/render/normalmap.cpp`; it is not part of the vendored file's build. The vendored file also defines its own `DllMain` at line 1280, so compiling it would create a second entry point.

**Size:** 1,314 LOC / 40,016 B.
**What breaks:** Nothing. It is commented out of the vcxproj and explicitly `removefiles()`d by premake. `src/rw/normalmap.cpp` is a different, live TU.
**Removal steps:**
1. Delete `src/rw/normalmap_plugin.cpp`.
2. Delete `build/skygfx.vcxproj:259` (commented-out row).
3. Delete `premake5.lua:185` from `removefiles`; optionally clean stale comments at `premake5.lua:32`, `:92`.
4. No `.filters` row exists.

**Safety: SAFE.**

---

### C5. `src/render/SMAA.cpp` / `SMAA.h` — DEAD

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '#include.*SMAA\.h' src
src/render/SMAA.cpp:44:#include "SMAA.h"
```

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\b(loadAreaTex|edgesDetectionPass|neighborhoodBlendingPass)\b' src | grep -v '^src/render/SMAA.cpp:'
src/render/EdgeDetection.h:97:    void loadAreaTex();
src/render/EdgeDetection.h:103:    void neighborhoodBlendingPass(IDirect3DTexture9* src, IDirect3DSurface9* dst);
src/render/SMAA.h:139:        void loadAreaTex();
src/render/SMAA.h:141:        void edgesDetectionPass(IDirect3DTexture9 *edges, Input input);
src/render/SMAA.h:143:        void neighborhoodBlendingPass(IDirect3DTexture9 *src, IDirect3DSurface9 *dst);
```

`EdgeDetection.h` is itself an unused duplicate (C6); the only other hits are inside the two legacy SMAA files, which are not built.

```text
$ grep -n -E 'ClInclude.*SMAA|ClCompile.*SMAA|"[^"]*SMAA\.(cpp|h)"' build/skygfx.vcxproj premake5.lua | grep -vE ':[0-9]+:[[:space:]]*--'
build/skygfx.vcxproj:136:    <ClInclude Include="..\src\render\SMAA.h" />
build/skygfx.vcxproj:260:    <!-- <ClCompile Include="..\src\render\SMAA.cpp" /> -->
premake5.lua:186:        "src/render/SMAA.cpp",          -- replaced by src/render/postfx_smaa.cpp
premake5.lua:229:        "src/render/SMAA.h",
```

**Size:** `SMAA.cpp` 374 LOC / 14,407 B; `SMAA.h` 177 LOC / 7,577 B.
**What breaks:** Nothing. The live path is `postfx_smaa.cpp` + `AreaTex.h` + `SearchTex.h`; do **not** remove those. `SMAA.cso` is a separate prebuilt resource (orphan, see C13).
**Removal steps:**
1. Delete both files.
2. `build/skygfx.vcxproj`: delete header row `136`; delete commented row `260`.
3. `build/skygfx.vcxproj.filters`: delete `42-44` (`SMAA.h`) and `93-95` (`SMAA.cpp`).
4. `premake5.lua`: delete header row `229`; delete `removefiles` row `186`.

**Safety: SAFE.**

---

### C10. `src/extras/skygfx_bridge.cpp` — DEAD

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\b(Bridge_Init|Bridge_Shutdown|Bridge_SendCommand|Bridge_ReceiveCommand|Bridge_GetPendingCount)\b' src || echo 'NO_REFS'
NO_REFS
```

```text
$ grep -n -E 'skygfx_bridge' build/skygfx.vcxproj build/skygfx.vcxproj.filters premake5.lua
premake5.lua:34:--       src/extras/skygfx_bridge.cpp  — not part of this build
premake5.lua:92:--     * no ClCompile for normalmap_plugin.cpp / SMAA.cpp / skygfx_bridge.cpp
premake5.lua:176:	-- src/extras: glob, minus skygfx_bridge.cpp (not part of this build)
premake5.lua:187:		"src/extras/skygfx_bridge.cpp", -- not part of this build
```

The file exports a second `DllMain` (`:124`) and a shared-memory bridge that no current client references.

**Size:** 134 LOC / 2,720 B.
**What breaks:** Nothing in this DLL. There is no vcxproj or filters row.
**Removal steps:**
1. Delete `src/extras/skygfx_bridge.cpp`.
2. Delete `premake5.lua:187` from `removefiles`; optionally clean comments at `:34`, `:92`, `:176`.

**Safety: SAFE.**

---

### C6. Unused headers (`EdgeDetection.h`, `PostFXPipeline.h`, `GrassSystem.h`, `hooks.h`) — DEAD

**Evidence**

```text
$ for h in EdgeDetection PostFXPipeline GrassSystem hooks; do printf '%s: ' "$h.h"; grep -rn --include='*.cpp' --include='*.h' -E "#include[[:space:]]*[\"<].*${h}\.h" src | wc -l; done
EdgeDetection.h: 0
PostFXPipeline.h: 0
GrassSystem.h: 0
hooks.h: 0
```

```text
$ grep -n -E 'EdgeDetection|PostFXPipeline|GrassSystem|hooks\.h' build/skygfx.vcxproj build/skygfx.vcxproj.filters premake5.lua || echo 'NOT_IN_BUILD_FILES'
NOT_IN_BUILD_FILES
```

**Size:** 377 LOC / 11,516 B (`146+103+101+27` LOC).
**What breaks:** Nothing. `hooks.h` only declares `InstallAllHooks`/`InjectDelayedPatches`; the live implementation is in `main.cpp`, and nothing includes this declaration file.
**Removal steps:** Delete the four headers. No build-file edits.

**Safety: SAFE.**

---

## Tier 1 — compiled but zero live references (SAFE)

### C2. `src/render/PC_PlantsMgr*.cpp` — DEAD TUs, LIVE headers

The candidate path in the inventory is stale: the files are under `src/render/`, not `src/extras/`.

**Evidence**

```text
$ grep -n -E 'PC_PlantsMgr' build/skygfx.vcxproj
142:    <ClInclude Include="..\src\render\PC_PlantsMgr.h" />
207:    <ClCompile Include="..\src\render\PC_PlantsMgr.cpp">
210:    <ClCompile Include="..\src\render\PC_PlantsMgr_overlay.cpp">
```

```text
$ sed -n '207,212p' build/skygfx.vcxproj
    <ClCompile Include="..\src\render\PC_PlantsMgr.cpp">
      <ExcludedFromBuild>true</ExcludedFromBuild>
    </ClCompile>
    <ClCompile Include="..\src\render\PC_PlantsMgr_overlay.cpp">
      <ExcludedFromBuild>true</ExcludedFromBuild>
    </ClCompile>
```

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '#include\s+["<].*PC_PlantsMgr\.h[" >]' src
src/render/PC_GrassInstancer.cpp:21:#include "PC_PlantsMgr.h"
src/render/PC_GrassInstancer.h:26:#include "PC_PlantsMgr.h"		// PPTriPlant
src/render/PC_GrassRenderer.h:14:#include "PC_PlantsMgr.h"
src/render/PC_PlantsMgr.cpp:32:#include "PC_PlantsMgr.h"
src/render/PC_PlantsMgr_overlay.cpp:33:#include "PC_PlantsMgr.h"
```

**Size:** 3,210 LOC / 90,566 B (`PC_PlantsMgr.cpp` 2,175/62,156; overlay 1,035/28,410).
**What breaks:** Nothing today — both TUs are `ExcludedFromBuild`. **Do not delete `PC_PlantsMgr.h`**: live `PC_GrassInstancer.cpp`/`.h` and `PC_GrassRenderer.h` include it for `PPTriPlant` and related types. Deleting the header breaks the build.
**Removal steps:**
1. Delete only `src/render/PC_PlantsMgr.cpp` and `src/render/PC_PlantsMgr_overlay.cpp`.
2. `build/skygfx.vcxproj`: delete rows `207-212`; optionally also the stale blocker comment `201-206`.
3. `build/skygfx.vcxproj.filters`: delete rows `203-208`; keep `70-72` (header).
4. `premake5.lua`: delete the four `filter` blocks at `287-294` (not the closing `filter {}` at `295`); keep header row `233`.
5. Keep `PC_GrassRenderer.cpp`/`PC_GrassInstancer.cpp` untouched.

**Safety: SAFE** (build-wise). Caveat: this removes the only implementation of the legacy plant island; if W8-3 grass revival is still planned, archive the files instead.

---

### C1. Six `src/render/*_mode.cpp` presets — DEAD

`modeManager.cpp` now owns a real `g_modeProfiles[]` table, and none of the six preset TUs is called.

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '(PS2Mode|XboxMode|PCPatchedMode|MobileMode|IVMode|CustomMode)_(ApplyDefaults|GetPreset)\(' src
src/render/custom_mode.cpp:68:CustomMode_ApplyDefaults(Config *c)
src/render/custom_mode.cpp:168:CustomMode_GetPreset(void)
src/render/iv_mode.cpp:63:IVMode_ApplyDefaults(Config *c)
src/render/iv_mode.cpp:76:IVMode_GetPreset(void)
src/render/mobile.cpp:61:MobileMode_ApplyDefaults(Config *c)
src/render/mobile.cpp:102:MobileMode_GetPreset(void)
src/render/pc_patched_mode.cpp:62:PCPatchedMode_ApplyDefaults(Config *c)
src/render/pc_patched_mode.cpp:103:PCPatchedMode_GetPreset(void)
src/render/ps2_mode.cpp:65:PS2Mode_ApplyDefaults(Config *c)
src/render/ps2_mode.cpp:106:PS2Mode_GetPreset(void)
src/render/xbox_mode.cpp:61:XboxMode_ApplyDefaults(Config *c)
src/render/xbox_mode.cpp:102:XboxMode_GetPreset(void)
src/skygfx.h:1488:void PS2Mode_ApplyDefaults(Config *c);
src/skygfx.h:1489:const PresetConfig* PS2Mode_GetPreset(void);
src/skygfx.h:1491:void XboxMode_ApplyDefaults(Config *c);
src/skygfx.h:1492:const PresetConfig* XboxMode_GetPreset(void);
src/skygfx.h:1494:void IVMode_ApplyDefaults(Config *c);
src/skygfx.h:1495:const PresetConfig* IVMode_GetPreset(void);
src/skygfx.h:1497:void MobileMode_ApplyDefaults(Config *c);
src/skygfx.h:1498:const PresetConfig* MobileMode_GetPreset(void);
src/skygfx.h:1500:void PCPatchedMode_ApplyDefaults(Config *c);
src/skygfx.h:1501:const PresetConfig* PCPatchedMode_GetPreset(void);
src/skygfx.h:1503:void CustomMode_ApplyDefaults(Config *c);
src/skygfx.h:1504:const PresetConfig* CustomMode_GetPreset(void);
```

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '#include.*(ps2_mode|xbox_mode|pc_patched_mode|mobile|iv_mode|custom_mode)\.h' src || echo 'NO_INCLUDES'
NO_INCLUDES
```

```text
$ grep -n -E 'static const ModeProfile g_modeProfiles' src/core/modeManager.cpp
608:static const ModeProfile g_modeProfiles[] = {
```

The current profiles (`kValuesPS2`, `kValuesXBOX`, `kValuesPCVintage`, `kValuesGTAIV`, `kValuesGTAV`, `kValuesModern`, `kValuesMobile`, lines 89-617) are applied by the manager directly. `presets.cpp` has its own `presetConfigs[]` style ladder.

**Size:** 675 LOC / 21,691 B total:

| File | LOC | Bytes |
|---|---:|---:|
| `ps2_mode.cpp` | 109 | 3,563 |
| `xbox_mode.cpp` | 105 | 3,055 |
| `pc_patched_mode.cpp` | 106 | 3,325 |
| `mobile.cpp` | 105 | 3,125 |
| `iv_mode.cpp` | 79 | 3,286 |
| `custom_mode.cpp` | 171 | 5,337 |

**What breaks:** Nothing. Delete the declarations in `skygfx.h:1482-1504` too (the block comment plus six forward-declaration pairs). `PresetConfig` is already defined at `skygfx.h:255`; remove the redundant forward at `:1486` with the block. Update stale comments in `main.cpp:611`, `debugmenu_ui.cpp:2694` only if documentation hygiene is desired.
**Removal steps:**
1. Delete all six `.cpp` files.
2. `build/skygfx.vcxproj`: delete rows `171` (`custom_mode`), `180` (`iv_mode`), `183` (`mobile`), `213` (`pc_patched_mode`), `229` (`ps2_mode`), `249` (`xbox_mode`).
3. No `.filters` rows exist for these files.
4. Delete `src/skygfx.h:1482-1504`.
5. Premake globs `src/render/*.cpp`; no premake edit needed.

**Safety: SAFE.**

---

### C4. `src/render/vegImposter.{cpp,h}` — DEAD

Header comment says “Nothing here is called by anything today”, and the tree confirms it.

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\b(VegImposter_[A-Za-z]+|CVegImposter|VegImposterConfig)\b' src | grep -v -E '^src/render/vegImposter\.(cpp|h):' || echo 'NO_EXTERNAL_REFS'
NO_EXTERNAL_REFS
```

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '#include.*vegImposter' src
src/render/vegImposter.cpp:28:#include "vegImposter.h"
```

`forwardplus.cpp:1645` and other matches are comments only. The only live mention is a comment at `forwardplus.cpp:1645`.

**Size:** 1,070 LOC / 43,362 B (`vegImposter.cpp` 704/24,844; `.h` 366/18,518).
**What breaks:** Nothing. Build rows exist; remove them. The module is inert by design, but its header is a substantial design contract — archive if the future imposter lane is still planned.
**Removal steps:**
1. Delete both files.
2. `build/skygfx.vcxproj`: delete `.cpp` row `199`; delete header row `144`.
3. `build/skygfx.vcxproj.filters`: delete `.cpp` rows `200-202`; delete header rows `74-76`.
4. `premake5.lua`: delete explicit `.cpp` row `198`; delete header row `234`.

**Safety: SAFE.**

---

### C11. `src/core/test.cpp` + screens/dumpMenu — DEAD

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -w 'dumpMenu' src
src/core/test.cpp:25:dumpMenu(void)
```

```text
$ grep -rn --include='*.cpp' -E '\bscreens\b' src/core | grep -v test.cpp | grep -v ':[0-9]*:[[:space:]]*//' || echo 'NO_CODE_REFS'
NO_CODE_REFS
```

The only `screens` hits elsewhere are comments in `menu_inject.cpp` (`:186`, `:412`). The global `screens` is defined only in `test.cpp:22`.

**Size:** 47 LOC / 922 B.
**What breaks:** Nothing.
**Removal steps:**
1. Delete `src/core/test.cpp`.
2. `build/skygfx.vcxproj`: delete row `230`.
3. `build/skygfx.vcxproj.filters`: delete rows `169-171` (stale path `..\src\test.cpp`).
4. Premake glob; no edit.

**Safety: SAFE.**

---

### C12. `HLSL_hook.{cpp,h}` — DEAD

**Evidence**

```text
$ grep -rl --include='*.cpp' --include='*.h' -E '\b(HLSL::)?(CompileVertexShader|CompilePixelShader|LoadShaderFromFile)\b' src
src/rw/HLSL_hook.cpp
src/rw/HLSL_hook.h
```

All hits are definitions/declarations in the pair itself. Both compile helpers return `nullptr`; `LoadShaderFromFile` is the only nontrivial code and has no caller.

**Size:** 45 LOC / 1,328 B.
**What breaks:** Nothing; no runtime shader loader uses this namespace.
**Removal steps:**
1. Delete both files.
2. `build/skygfx.vcxproj`: delete header row `130`; delete `.cpp` row `161`.
3. `build/skygfx.vcxproj.filters`: delete header rows `27-29`; delete `.cpp` rows `90-92`.
4. `premake5.lua`: delete header row `223`. The `.cpp` is covered by the `src/rw/*.cpp` glob and disappears with the file.

**Safety: SAFE.**

---

### C15. Duplicated `MENUSETTINGS` macros — only `main.cpp` copy is dead

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\bMENUSETTINGS\b' src/core/main.cpp src/core/debugmenu_ui.cpp
src/core/main.cpp:6844:#define MENUSETTINGS \
src/core/debugmenu_ui.cpp:77:#define MENUSETTINGS \
src/core/debugmenu_ui.cpp:161:MENUSETTINGS
src/core/debugmenu_ui.cpp:172:MENUSETTINGS
```

The `debugmenu_ui.cpp` macro is expanded twice and is live. The `main.cpp` macro is never expanded. It spans `6844-6917` (74 LOC, 1,652 B, ending at the blank line before the next function).

**What breaks:** Nothing. Remove exactly `src/core/main.cpp:6844-6917`; keep both `debugmenu_ui.cpp` expansions. No build-file edits.

**Safety: SAFE.**

---

### C7. `#if 0` blocks in `neo.cpp`, `postfx_colourfilter.cpp`, `vehiclePipe.cpp` — DEAD

**Evidence**

```text
$ grep -n -E '^#if 0|^#endif' src/render/neo.cpp src/render/postfx_colourfilter.cpp src/render/vehiclePipe.cpp
src/render/neo.cpp:143:#if 0
src/render/neo.cpp:209:#endif
src/render/postfx_colourfilter.cpp:853:#if 0
src/render/postfx_colourfilter.cpp:893:#endif
src/render/vehiclePipe.cpp:3401:#if 0
src/render/vehiclePipe.cpp:3407:#endif
```

**Size:** 115 LOC / 4,093 B:

| Block | Current range | LOC | Bytes |
|---|---|---|---:|---:|
| `neo.cpp` legacy `neoCreatePipe`/`CustomPipe` | `143-209` | 67 | 1,774 |
| `postfx_colourfilter.cpp` reference `ColourFilter_PC` | `853-893` | 41 | 2,021 |
| `vehiclePipe.cpp` temp car-pipe disable | `3401-3407` | 7 | 298 |

**What breaks:** Nothing; all code is preprocessor-excluded. Do not remove the two live blocks’ surrounding code.
**Removal steps:** Delete the ranges above. No build-file edits. Do not touch the `#if 0` in `PC_GrassRenderer.cpp:509-532` or `PC_PlantsMgr_overlay.cpp:168-189` unless those excluded TUs are handled under C2.

**Safety: SAFE.**

---

### C9. Dead ped helpers in `chars.cpp` — DEAD

The entire classification/init/shutdown lane is unreferenced; the live SSS composite only needs `g_charsClassifyTex`, its surface/raster, `EnsureClassifyBuffer`, and the `s_charsClassifyFresh` gate.

**Evidence**

```text
$ for id in chars_classifyByFrameName chars_classifyByPosition chars_classifyAtomic chars_classifyByTexture chars_getPartParams chars_buildClassificationBuffer chars_shutdown chars_init; do echo "--- $id"; grep -rn --include='*.cpp' --include='*.h' -w "$id" src | grep -v '^src/entities/chars.cpp:'; done
--- chars_classifyByFrameName
src/entities/chars.h:55:int chars_classifyByFrameName(const char *frameName);
--- chars_classifyByPosition
src/entities/chars.h:57:int chars_classifyByPosition(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics);
--- chars_classifyAtomic
src/entities/chars.h:54:int chars_classifyAtomic(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics);
--- chars_classifyByTexture
src/entities/chars.h:56:int chars_classifyByTexture(RwTexture *tex);
--- chars_getPartParams
src/entities/chars.h:59:PartSSSParams chars_getPartParams(int partType);
--- chars_buildClassificationBuffer
src/entities/chars.h:62:void chars_buildClassificationBuffer(void);
--- chars_shutdown
src/entities/chars.h:49:void chars_shutdown(void);
--- chars_init
src/entities/chars.h:48:void chars_init(void);
```

Every external hit is a declaration. `ByFrameName`, `ByPosition`, and `Atomic` only feed `classifyClump`; `classifyCB` and the 256-entry arrays only feed that; `classifyClump` has no caller (see the TODO at `chars.cpp:671`). `ByTexture` is standalone. `chars_init`/`chars_shutdown`/`getPartParams`/`buildClassificationBuffer` also have zero callers.

**Size:** 263 LOC / 9,631 B including the function-pointer typedefs, `ClassifyData`, dead globals, and the dead init/shutdown functions.
**What breaks:** Nothing if the live classification-resource globals are preserved. **Do not delete** `g_charsClassifyTex`, `g_charsClassifySurf`, `g_charsClassifyRaster`, `EnsureClassifyBuffer`, `s_charsClassifyFresh`, `chars_drawSSSBlur`, or `chars_OnDeviceReset`. `chars_OnDeviceReset` is live (`postfx.cpp:2299`).
**Removal steps:** Delete current ranges `25-35` (RW function-pointer typedefs), `42-221` (ByFrameName/ByTexture/ByPosition/Atomic), `257-285` (`ClassifyData` + classify arrays + `classifyCB` + `classifyClump`), and `648-690` (`chars_init`/`getPartParams`/`buildClassificationBuffer`/`chars_shutdown`). Remove the matching declarations in `chars.h:48-49` and `:54-62`; keep `:50-51` and `:64-67`. Rebuild to ensure no static-unused warning is treated as an error.

**Safety: SAFE** for the build; this is feature-dead, not a live render path.

---

### C8. Five dead lazy getters in `pipelinecommon.cpp` — DEAD

**Evidence**

```text
$ grep -rn --include='*.cpp' --include='*.h' -E '\b(Pipeline_GetCustomBuildingVS|Pipeline_GetNormMapBuildingPS|Pipeline_GetNormMapVehiclePS|Pipeline_GetVectorMotionBlurPS|Pipeline_GetSkinSSSPS)\b' src | grep -v pipelinecommon.cpp || echo 'NO_CALLERS'
NO_CALLERS
```

Each name occurs only twice in `pipelinecommon.cpp` (prototype + definition); `Pipeline_GetSkinSSSPS` occurs three times (an extra comment). The five bodies span `737-802` (66 LOC / 1,773 B); with the section comment the removable block is `727-802` (76 LOC / 2,314 B). Prototypes are `74-78`; static handles are `66-69`.

**What breaks:** Nothing at runtime. Removing the getters stops those five resources from being loaded, but they are already never bound. The actual resource/CSO cleanup is C13 and requires a build check.
**Removal steps:** Delete `727-802` (or keep the section banner and delete `734-802`); delete prototypes `74-78`; delete static handles `66-69`; delete the `SkinPBR` handoff comment if removing `SkinPBR`. Keep `SkinPBR` itself if C13 is deferred. No build-file edits.
**Optional paired cleanup:** after deleting getters, `IDR_CUSTOMBUILDINGVS`, `IDR_NORMMAPBUILDINGPS`, `IDR_NORMMAPVEHICLEPS`, `IDR_VECTORMOTIONBLUR`, and `IDR_SKINPBR` become loader-less; see C13.

**Safety: SAFE** (getter removal alone). Pairing with C13 makes it NEEDS-BUILD-CHECK.

---

## Tier 2 — requires build/runtime check

### C13. Dead/never-bound `IDR_*` shader resources + orphan CSOs

**Current shader registry facts**

- `resources/resource.h` defines **100** `IDR_*` IDs.
- `resources/Resource.rc` embeds exactly those **100** IDs (no mismatch either way).
- **99** are loaded by `makePS`/`makeVS`, plus `IDR_GRASSPS` loaded manually in `main.cpp:2338`.
- **1** ID has no loader at all: `IDR_PEDPBRVS` (275).
- **16** IDs are orphaned (loaded but never bound, lazy-but-never-called, or never loaded).

**Evidence — registry count and no-loader ID**

```text
$ python -c "import re,pathlib; h=pathlib.Path('resources/resource.h').read_text(); rc=pathlib.Path('resources/Resource.rc').read_text(); ids={n for n,v in re.findall(r'^#define\s+(IDR_\w+)\s+(\d+)',h,re.M)}; used=set('IDR_'+n for n in re.findall(r'make(?:PS|VS)\(IDR_(\w+)', ''.join(p.read_text(errors='ignore') for p in pathlib.Path('src').rglob('*') if p.suffix in ('.cpp','.h'))))|{'IDR_GRASSPS'}; print('DEFINED',len(ids),'LOADED',len(ids&used)); print('NEVER_LOADED:',sorted(ids-used))"
DEFINED 100 LOADED 99
NEVER_LOADED: ['IDR_PEDPBRVS']
```

**Evidence — loaded-but-never-bound draw/load symbols**

```text
$ grep -rn --include='*.cpp' -E 'RwD3D9SetPixelShader\([[:space:]]*(GTAIV_PS|SMAA_EdgeNormal|SMAA_EdgeDepth|SMAA_EdgeCombined|SSAO_VertexDepth|ColorFilter_CrossMix|VehiclePaint_GTAIV|CarPaint_Reflections|PBR_Lighting|SkinPBR)[[:space:]]*\)|PfBindPS\([[:space:]]*(GTAIV_PS|SMAA_EdgeNormal|SMAA_EdgeDepth|SMAA_EdgeCombined|SSAO_VertexDepth|ColorFilter_CrossMix|VehiclePaint_GTAIV|CarPaint_Reflections|PBR_Lighting|SkinPBR)' src || echo 'NO_BIND_SITES'
NO_BIND_SITES
```

`SMAA` (the old monolithic handle) has no assignment and no bind site. The five lazy getters are proven under C8 (`NO_CALLERS`).

**Orphan ID table**

| `IDR_*` | # | CSO | Loader | Why orphan |
|---|---:|---|---|---|
| `IDR_GTAIVPS` | 110 | `GTAIV_ps20.cso` | `pipelinecommon.cpp:867` | loaded, never bound |
| `IDR_SMAAPS` | 121 | `SMAA.cso` | `pipelinecommon.cpp:847` | old mono pass, never bound |
| `IDR_SMAAEDGENORMALPS` | 125 | `SMAA_EdgeNormal.cso` | `pipelinecommon.cpp:849` | loaded, never bound |
| `IDR_SMAAEDGEDEPTHPS` | 126 | `SMAA_EdgeDepth.cso` | `pipelinecommon.cpp:850` | loaded, never bound |
| `IDR_SMAAEDGECOMBINEDPS` | 128 | `SMAA_EdgeCombined.cso` | `pipelinecommon.cpp:851` | loaded, never bound |
| `IDR_SSAO_VERTEXDEPTH` | 130 | `SSAO_VertexDepth.cso` | `pipelinecommon.cpp:854` | loaded, never bound |
| `IDR_COLORFILTER_CROSSMIX` | 141 | `ColorFilter_CrossMix.cso` | `pipelinecommon.cpp:861` | loaded, never bound |
| `IDR_VEHICLEPAINT_GTAIV` | 200 | `VehiclePaint_GTAIV.cso` | `pipelinecommon.cpp:874` | loaded, never bound |
| `IDR_CARPAINT_REFL` | 206 | `CarPaint_Reflections.cso` | `pipelinecommon.cpp:893` | loaded, never bound |
| `IDR_PBR_LIGHTING` | 207 | `PBR_Lighting.cso` | `pipelinecommon.cpp:894` | loaded, never bound |
| `IDR_SKINPBR` | 210 | `SkinSSS.cso` | `Pipeline_GetSkinSSSPS` | getter never called |
| `IDR_CUSTOMBUILDINGVS` | 235 | `customBuildingVS.cso` | `Pipeline_GetCustomBuildingVS` | getter never called |
| `IDR_NORMMAPBUILDINGPS` | 236 | `normMapBuildingPS.cso` | `Pipeline_GetNormMapBuildingPS` | getter never called |
| `IDR_NORMMAPVEHICLEPS` | 237 | `normMapVehiclePS.cso` | `Pipeline_GetNormMapVehiclePS` | getter never called |
| `IDR_VECTORMOTIONBLUR` | 264 | `vectorMotionBlur.cso` | `Pipeline_GetVectorMotionBlurPS` | getter never called |
| `IDR_PEDPBRVS` | 275 | `pedPBRVS.cso` | **none** | never loaded |

`IDR_RUBBER_VEHICLE` (205) is deliberately excluded from this table: it is loaded and used as a **boolean gate** at `vehiclePipe.cpp:2458` (`Rubber_Vehicle && VehShaders_IsTireTexture`), even though the pixel shader is not bound directly.

**Size:** 16 orphan IDs, 16 unique CSOs, **39,240 B** total; all 16 `<IDR,RC-row>` pairs are present.

**What breaks if removed**
- Deleting a header `#define` without deleting its `makePS`/`makeVS` or `Pipeline_Get*` call is a compile error.
- Deleting a `Resource.rc` row before the CSO file is removed is a missing-file RC failure; deleting the CSO while the row remains is also an RC failure.
- Do **not** remove `IDR_RUBBER_VEHICLE`, `IDR_SMAAEDGEPS`, `IDR_SMAABLENDWEIGHTPS`, `IDR_SMAABLENDNEIGHBORPS`, `IDR_SMAAEDGEMOTIONDEPTHPS`, `IDR_SMAATEMPPS`, the live SSAO IDs, `IDR_GRASSPS`, or any live postfx ID.
- `IDR_SMAAEDGENORMALPS`/`DEPTH`/`COMBINED` are loaded but never drawn; removing them cannot blank the current SMAA chain.
- `SMAA.cso` and `customBuildingVS.cso` are prebuilt binaries with no current HLSL producer, so they can be deleted once their IDs/loaders are gone. The other 14 orphan CSOs still have current producers in `tools/fast_build.py`; unembedding them shrinks the DLL but does **not** remove the `.cso` file unless the HLSL producer/compile row is retired too.

**Removal steps (single build-checked change set; delete descending so line numbers do not shift)**
1. Delete the 16 IDs from `resources/resource.h` at lines `9, 15, 19, 20, 22, 24, 31, 46, 52, 53, 56, 92, 93, 94, 114, 125`.
2. Delete the matching `Resource.rc` rows at lines `12, 27, 31, 32, 34, 36, 43, 58, 67, 68, 71, 96, 97, 98, 118, 129`.
3. Remove the 10 loaded-unbound globals/loaders and externs:
   - globals/loaders: `pipelinecommon.cpp:21, 23, 24, 25, 31, 37, 39, 48, 49, 57`; loader calls at `847, 849, 850, 851, 854, 861, 867, 874, 893, 894`.
   - externs: `skygfx.h:1171, 1173, 1174, 1175, 1181, 1186, 1187, 1195, 1196, 1200`.
   - remove dead debug-log references at `pipelinecommon.cpp:962-975`.
4. Remove the five lazy getters per C8 plus their static handles (`pipelinecommon.cpp:66-69`) and `SkinPBR` (`pipelinecommon.cpp:52`, `skygfx.h:1199`, `chars.h:64`); `Pipeline_GetSkinSSSPS` loader is at `:799`.
5. `IDR_PEDPBRVS` has no code references; remove `resource.h:125` + `Resource.rc:129`. If the ped PBR lane is abandoned, also remove `shaders/vs/pedPBRVS.hlsl` and `pedPBRVS.cso`.
6. Delete `SMAA.cso` and `customBuildingVS.cso` only after steps 1-3. Do not delete the other generated CSOs until their producers are retired.
7. Build (resource compile + link), then launch and check the live shader receipt for new `FindResource FAILED` lines. The receipt should show no missing ID beyond pre-existing expected gaps.

**Safety: NEEDS-BUILD-CHECK.** The binds are proven absent, but this edits resource IDs, the RC, loader globals, and DLL size; compile and launch are mandatory.

---

## Orphan shaders

### C16. HLSL files whose CSO has no `IDR_*`, and IDs with no loader

**Cross-check result**

- Current `tools/fast_build.py::get_shader_list()` produces **111** outputs.
- `resources/Resource.rc` embeds **100** IDs.
- `resources/cso/` contains **122** `.cso` files.
- **18** current producer outputs are not embedded (listed below).
- **22** CSO files total are not embedded: those 18 plus four stale no-producer files (`buildingPipePS`, `neoVehiclePass1VS`, `neoVehiclePass2VS`, `vehiclePipeVS`).
- **11** CSO files on disk have no current producer; five are intentional GTAIV prebuilts, six are stale (the four above plus `SMAA` and `customBuildingVS`, which are embedded).
- **1** `IDR_*` is never `makePS`/`makeVS`'d: `IDR_PEDPBRVS` (C13).
- **`shaders/ps/CloudsPS.hlsl` is not dead and must not be deleted.** It has no resource entry yet; it is a **pending integration** (explicitly noted in the source header as producing `CloudsPS.cso` without a build-script edit).

**Evidence — producer outputs vs RC**

```text
$ python -c "import sys,pathlib,re; sys.path.insert(0,'tools'); import fast_build as fb; sh=fb.get_shader_list(); rc=pathlib.Path('resources/Resource.rc').read_text(); emb=set(re.findall(r'RCDATA\s+\"cso/([^\"]+)\"',rc)); print('shader entries',len(sh)); print('NOT_EMBEDDED:'); [print(pathlib.Path(c).name,'<-',pathlib.Path(s).name+':'+e) for _,s,c,_,e in sh if pathlib.Path(c).name not in emb]"
shader entries 111
NOT_EMBEDDED:
CloudsPS.cso <- CloudsPS.hlsl:main
GrassInstanced.cso <- GrassInstanced.hlsl:main
GTA_SA_ModernColor.cso <- GTA_SA_ModernColor.hlsl:main
HairEnhance.cso <- HairEnhance.hlsl:main
IBL_SkyCloud.cso <- IBL_SkyCloud.hlsl:main
iiiTrailsPS.cso <- iiiTrailsPS.hlsl:main
SkinEnhance.cso <- SkinEnhance.hlsl:main
SSAO_ps20.cso <- SSAO_ps20.hlsl:main
SSAO_ps20_depthonly.cso <- SSAO_ps20_depthonly.hlsl:main
SSAO_ps20_simple.cso <- SSAO_ps20_simple.hlsl:main
mobileBuildingVS.cso <- mobileBuildingVS.hlsl:main
postfxVS.cso <- postfxVS.hlsl:main
ps2BuildingSSSVS.cso <- ps2BuildingSSSVS.hlsl:main
unifiedPipe.cso <- unifiedPipe.hlsl:main
unifiedPipePre.cso <- unifiedPipe.hlsl:main_pre
unifiedPipeOcclusion.cso <- unifiedPipe.hlsl:main_occlusion
unifiedPipePost.cso <- unifiedPipe.hlsl:main_post
unifiedPipeIBL.cso <- unifiedPipe.hlsl:main_ibl
```

**Evidence — stale CSOs with no current producer**

```text
$ python -c "import sys,pathlib,re; sys.path.insert(0,'tools'); import fast_build as fb; sh=fb.get_shader_list(); outs={pathlib.Path(c).name for _,_,c,_,_ in sh}; disk={p.name for p in pathlib.Path('resources/cso').glob('*.cso')}; print('COMPILED_OUTPUTS',len(outs),'DISK',len(disk)); print('ON_DISK_NO_CURRENT_PRODUCER:'); print('\n'.join(sorted(disk-outs)))"
COMPILED_OUTPUTS 111 DISK 122
ON_DISK_NO_CURRENT_PRODUCER:
GTAIVBuilding_ps.cso
GTAIVForwardPlus_ps.cso
GTAIVForwardPlus_vs.cso
GTAIVVehicle_ps.cso
GTAIVVehicle_vs.cso
SMAA.cso
buildingPipePS.cso
customBuildingVS.cso
neoVehiclePass1VS.cso
neoVehiclePass2VS.cso
vehiclePipeVS.cso
```

**Evidence — CloudPS has no RC entry**

```text
$ grep -n 'CloudsPS' resources/resource.h resources/Resource.rc || echo 'NO_RC_ENTRY'
NO_RC_ENTRY
$ grep -n 'CloudsPS' tools/fast_build.py || echo 'NOT_MULTI_ENTRY'
NOT_MULTI_ENTRY
```

#### Category A — pending integration (do not delete)

| HLSL | CSO | Status |
|---|---|---|
| `shaders/ps/CloudsPS.hlsl` | `CloudsPS.cso` | Pending RC/resource ID; explicitly staged |
| `shaders/ps/GrassInstanced.hlsl` | `GrassInstanced.cso` | Pending `IDR_GRASSINSTANCED`; code `#ifdef` fallback exists |

`GrassInstanced` is additionally risky: `PC_GrassInstancer.cpp:106-123` guards loading on `IDR_GRASSINSTANCED`, which is undefined, so the current path latches failure and falls back. Do not classify the shader as removable until the instancer lane decides its fate.

#### Category B — WIP Forward+ chain (not dead by design)

`shaders/unifiedPipe.hlsl` produces `unifiedPipe.cso`, `unifiedPipePre.cso`, `unifiedPipeOcclusion.cso`, `unifiedPipePost.cso`, `unifiedPipeIBL.cso` (5 outputs). There is no RC wiring and no C++ consumer yet. These are WIP, not quick-win deletions.

#### Category C — dead/unreachable HLSL outputs

| HLSL | CSO | Evidence / note |
|---|---|---|
| `ps/GTA_SA_ModernColor.hlsl` | `GTA_SA_ModernColor.cso` | no C++ refs |
| `ps/HairEnhance.hlsl` | `HairEnhance.cso` | no C++ refs |
| `ps/IBL_SkyCloud.hlsl` | `IBL_SkyCloud.cso` | no C++ refs |
| `ps/SkinEnhance.hlsl` | `SkinEnhance.cso` | no C++ refs |
| `ps/iiiTrailsPS.hlsl` | `iiiTrailsPS.cso` | C++ pointer `iiiTrailsPS` exists but is **never assigned**; `postfx.cpp:1828` passes NULL. Treat as an unfinished/buggy path, not a safe delete. |
| `vs/mobileBuildingVS.hlsl` | `mobileBuildingVS.cso` | only a comment in `vehiclePipe.cpp`; no loader |
| `vs/postfxVS.hlsl` | `postfxVS.cso` | no C++ refs; Im2D uses FFVS |
| `vs/ps2BuildingSSSVS.hlsl` | `ps2BuildingSSSVS.cso` | no C++ refs |
| `ps/SSAO_ps20.hlsl` | `SSAO_ps20.cso` | legacy ps_2_0, no refs |
| `ps/SSAO_ps20_depthonly.hlsl` | `SSAO_ps20_depthonly.cso` | legacy ps_2_0, no refs |
| `ps/SSAO_ps20_simple.hlsl` | `SSAO_ps20_simple.cso` | legacy ps_2_0, no refs |

```text
$ for n in GTA_SA_ModernColor HairEnhance IBL_SkyCloud SkinEnhance mobileBuildingVS postfxVS ps2BuildingSSSVS SSAO_ps20 SSAO_ps20_depthonly SSAO_ps20_simple; do c=$(grep -rn --include='*.cpp' --include='*.h' -w "$n" src | wc -l); printf '%-24s refs=%s\n' "$n" "$c"; done
GTA_SA_ModernColor       refs=0
HairEnhance              refs=0
IBL_SkyCloud             refs=0
SkinEnhance              refs=0
mobileBuildingVS         refs=1
postfxVS                 refs=0
ps2BuildingSSSVS         refs=0
SSAO_ps20                refs=0
SSAO_ps20_depthonly      refs=0
SSAO_ps20_simple         refs=0
```

The single `mobileBuildingVS` hit is a comment. `iiiTrailsPS` is different: it has a live-looking C++ consumer but no producer:

```text
$ grep -rn --include='*.cpp' --include='*.h' -w 'iiiTrailsPS' src
src/render/postfx.cpp:481:void *iiiTrailsPS, *vcTrailsPS, *modernColorFilterPS;
src/render/postfx.cpp:1541:	// indoors). iiiTrailsPS (no doubling), Mobile (direction-only math) and
src/render/postfx.cpp:1828:		CPostEffects::ColourFilter_Generic(rgb1pc, rgb2pc, iiiTrailsPS);
```

There is no `iiiTrailsPS = ...` assignment, so line 1828 always passes NULL today.

**Size:** 11 dead sources: 774 LOC / 25,965 B; 11 CSOs 27,028 B. All 18 unembedded outputs: 1,210 HLSL LOC / 43,978 B; 37,660 B of CSOs.
**What breaks:** Nothing for Category C if its CSO is not referenced. Category A/B must stay. `iiiTrailsPS` needs a code decision first (bind it or retire the `COLORFILTER_III` branch); deleting only the CSO would leave the NULL branch.
**Removal steps:** For a Category C row, delete the HLSL source, its `.cso`, and the `fast_build.py` producer row if it is a multi-entry row. For single-entry `.hlsl` files, deleting the file is enough. Do **not** delete `CloudsPS`, `GrassInstanced`, `unifiedPipe*`, `AreaTex.h`, or `SearchTex.h`.

**Safety: NEEDS-BUILD-CHECK.** Some outputs are intentional future work even though no current `IDR_*` exists.

#### Stale `.cso` cleanup

- Safe to delete after confirming no external consumer: `buildingPipePS.cso`, `neoVehiclePass1VS.cso`, `neoVehiclePass2VS.cso`, `vehiclePipeVS.cso` (all have no current producer and are not embedded).
- `SMAA.cso` and `customBuildingVS.cso` become deletable only after C13 removes their orphan IDs/loaders. Until then they are embedded.
- Keep the five GTAIV prebuilt CSOs (`GTAIVBuilding_ps`, `GTAIVForwardPlus_ps/vs`, `GTAIVVehicle_ps/vs`); they are intentional binaries with no HLSL source.

---

## Tier 2 — continued

### C14. `buildingPipe.cpp` ↔ `src/rw/defaultFuncs.cpp` duplication

The file does not contain an exact duplicate of the active building callback. It is the **legacy/original default-callback implementation**, while `buildingPipe.cpp` now has its own `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` and `DNInstance_PS2`. `defaultFuncs.cpp` has zero inbound references.

**Evidence — no refs outside the file except declarations**

```text
$ grep -rl --include='*.cpp' --include='*.h' -E '\b(CCustomBuildingDNPipeline__CustomPipeRenderCB|DNInstance_default|CCustomBuildingDNPipeline__CreateCustomObjPipe|CCustomCarEnvMapPipeline__CustomPipeRenderCB|D3D9RenderNotLit|D3D9RenderPreLit)\b' src | grep -v -E '/defaultFuncs\.cpp$'
src/skygfx.h
```

The only remaining references are the four declarations at `src/skygfx.h:1266-1269` (`D3D9RenderNotLit`, `D3D9RenderPreLit`, `DNInstance_default`, `CCustomCarEnvMapPipeline__CustomPipeRenderCB`).

```text
$ python -c "import pathlib,difflib; p=pathlib.Path('src/render/buildingPipe.cpp').read_text().splitlines(); a=pathlib.Path('src/rw/defaultFuncs.cpp').read_text().splitlines()[125:260]; b=p[2806:2988]; print('DNInstance_default 126-260 vs DNInstance_PS2 2807-2988'); print('similarity_ratio',round(difflib.SequenceMatcher(None,a,b).ratio(),3))"
DNInstance_default 126-260 vs DNInstance_PS2 2807-2988
similarity_ratio 0.789
```

`DNInstance_PS2` is the live superset (182 LOC vs 135; adds pipeline-ID handling, DN/extra colours, and `instWhite` paths). A partial “dedupe” is not worthwhile: either keep `defaultFuncs.cpp` as reference or remove the whole TU.

**Size:** 466 LOC / 19,786 B.
**What breaks:** Nothing identified — no inbound code reference. The only declarations are `skygfx.h:1266-1269`; remove them with the file. Do not remove `DNInstance_PS2`, `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch`, or the building callbacks.
**Removal steps:**
1. Delete `src/rw/defaultFuncs.cpp`.
2. `build/skygfx.vcxproj`: delete row `172`.
3. `build/skygfx.vcxproj.filters`: delete rows `102-104`.
4. Delete `skygfx.h:1266-1269`.
5. Premake glob `src/rw/*.cpp`; no row edit.

**Safety: NEEDS-BUILD-CHECK.** The symbol evidence is clean, but this is a whole compiled render helper TU; link and run the building pipe before accepting. If any hidden address/table use exists outside the indexed tree, keep the file.

---

## Application order (top-down)

Apply in this order; remove build rows **bottom-up** within each file.

1. **Baseline:** build and launch current tree; save `skygfx_dbg.log`. Confirm no pre-existing shader `FindResource FAILED`.
2. **Tier 0:** C3 `normalmap_plugin` → C5 legacy SMAA → C10 bridge → C6 unused headers. No runtime paths.
3. **C2:** delete the two excluded `PC_PlantsMgr*.cpp` TUs; keep `PC_PlantsMgr.h` and the live grass TUs.
4. **Tier 1 file TUs:** C1 six mode presets → C4 `vegImposter` → C11 `test.cpp` → C12 `HLSL_hook`.
5. **Text-only dead code:** C15 `main.cpp` `MENUSETTINGS` → C7 three `#if 0` blocks.
6. **Dead APIs:** C9 `chars.cpp` ped classification/init/shutdown → C8 lazy getters.
7. **Checkpoint:** build + launch. Expect no new missing-symbol/missing-resource logs. Do not proceed to shader IDs until this is green.
8. **C13:** unembed the 16 orphan IDs and delete their loaders/externs in one change set; build + launch; inspect shader receipt.
9. **C16:** retire Category C shader sources/CSOs only after the `iiiTrailsPS` and pending-integration decisions; then delete the four non-embedded stale CSOs.
10. **C14:** remove `defaultFuncs.cpp` last, after a link check, because it is the only whole-TU “structural” deletion with no direct runtime proof beyond zero references.

## Open questions / pending integrations

- **`CloudsPS.hlsl`**: pending, not dead. Needs an `IDR_*`/RC entry and a caller before its CSO can be embedded. Do not delete.
- **`GrassInstanced.cso`**: pending `IDR_GRASSINSTANCED`; the instancer path currently falls back. Do not delete until that lane is resolved.
- **`unifiedPipe*.cso` (5)**: WIP Forward+ chain, no RC or C++ consumer. Archive/keep, do not treat as a quick win.
- **`iiiTrailsPS`**: C++ path passes a permanently NULL pointer at `postfx.cpp:1828`. Decide whether to bind the existing shader or retire the `COLORFILTER_III` branch; do not delete only the CSO.
- **C13 staged resources**: `IDR_PBR_LIGHTING`, `IDR_CARPAINT_REFL`, the norm-map IDs, and SMAA edge variants may be intentionally reserved for future lanes. If so, mark them as staged and leave the IDs/loaders; the current tree proves only that they are not bound **today**.
- **`PC_PlantsMgr`**: deleting the excluded `.cpp` files removes the only legacy plant-island implementation. If W8-3 revival remains planned, archive rather than delete; the headers must stay either way.
- **`defaultFuncs.cpp`**: zero indexed references, but it is a compiled render TU. Prefer a branch build with map/link inspection before permanent deletion.
- **`IDR_RUBBER_VEHICLE`**: keep — it is loaded and used as a tire-mesh boolean gate even though it is never bound as a PS.

