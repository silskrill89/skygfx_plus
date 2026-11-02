# Premake Verification — 2026-10-25

**Status: PASS** — real regeneration executed in a scratch git worktree, 24/24 gates pass.
No build was run (per lane instructions). The main working tree never had premake executed against it.

## 1. Why this lane existed

- `build/skygfx.vcxproj` is the authoritative build input. `tools/fast_build.py` MSBuilds it
  directly and never invokes premake.
- `premake5.lua` is a **reference mirror** of that vcxproj, used only if someone needs to
  regenerate the project (new machine, VS upgrade, mass file add).
- The 2026-10-25 dead-code sweep deleted 15 files from `src/` and from the live vcxproj.
  The mirror still referenced them, so a regeneration would have failed (or, worse, silently
  re-added deleted TUs through the directory globs once `removefiles()` was out of sync).

## 2. What was de-staled in `premake5.lua`

| File removed from disk | Old reference in the script |
|---|---|
| `src/render/vegImposter.cpp` | `removefiles` |
| `src/render/vegImposter.h` | header comment / ClInclude audit |
| `src/rw/HLSL_hook.cpp` | `removefiles` |
| `src/rw/HLSL_hook.h` | header comment / ClInclude audit |
| `src/render/SMAA.cpp` | `removefiles` |
| `src/render/SMAA.h` | header comment / ClInclude audit |
| `src/extras/skygfx_bridge.cpp` | `removefiles` |
| `src/rw/normalmap_plugin.cpp` | `removefiles` |
| `src/core/test.cpp` | `removefiles` |
| `src/render/EdgeDetection.h` | already absent from this script revision (re-audited) |
| `src/render/PostFXPipeline.h` | already absent from this script revision (re-audited) |
| `src/render/GrassSystem.h` | already absent from this script revision (re-audited) |
| `src/core/hooks.h` | already absent from this script revision (re-audited) |
| `src/render/PC_PlantsMgr.cpp` | `removefiles` + comment |
| `src/render/PC_PlantsMgr_overlay.cpp` | `removefiles` + comment |
| `src/core/config.cpp` (pre-existing stale) | `removefiles` |

Changes made:

0. Note on the inventory: the four headers `EdgeDetection.h`, `PostFXPipeline.h`,
   `GrassSystem.h` and `hooks.h` were already absent from the 420-line script when this lane
   started (the stale references to them belonged to an earlier revision). They were re-checked
   by the reference audit and do not appear in the generated project either.
1. **`removefiles { ... }` block deleted entirely.** All 9 entries were already gone from disk,
   so no glob can see them. The header now states that the dead code is deleted, and that any
   future restoration of one of those files must re-add an explicit `removefiles()` entry in the
   same commit.
2. **Glob comments corrected.** `src/core`, `src/render`, `src/rw`, `src/extras` are now described
   as plain globs (no "minus X" clauses for files that no longer exist).
3. **`src/render/PC_PlantsMgr.h` kept** in the ClInclude list. It is still included by
   `PC_GrassInstancer.h`, `PC_GrassInstancer.cpp` and `PC_GrassRenderer.h`; only the two `.cpp`
   files were deleted.
4. **Header documentation rewritten**: deleted-file inventory, `UseEnv`/`WindowsSdkDir` caveats,
   functional gates, and the regeneration procedure (now includes the worktree mirror step).
5. **CRLF preserved**: the file was 420 lines / 420 CRLF before, and is 419 lines / 419 CRLF after
   (uniform CRLF, zero bare LF).

**Reference audit:** all 61 quoted file references in the script were checked. The only
non-resolving strings are the 7 intended glob patterns, which expand to:

| Glob | Matches |
|---|---|
| `src/core/*.cpp` | 9 |
| `src/entities/*.cpp` | 2 |
| `src/render/*.cpp` | 35 |
| `src/rw/*.cpp` | 6 |
| `src/extras/*.cpp` | 6 |
| `src/*.cpp` | 0 |
| `third_party/bullet/src/**.cpp` | 115 |
| **project TUs** | **58** |

Every explicit path (`resources/*.rc`, `resources/Resource.aps`, `shaders/shaders.bat`,
`external/ini_parser.hpp`, all 23 headers, all 6 imgui absolute paths) exists on disk.

## 3. Verification method actually used

**Real regeneration** — not a static simulation.

- Generator: `premake5.exe` **5.0.0-alpha10**, action **vs2015** (the installed generator; it
  does not support `--to=`).
- Scratch worktree: `E:/dev/dave/skygfx_premake_verify` (detached at `b7396c4`), removed afterwards.
- The worktree was **not** used as-is. HEAD still contains the 15 files that are deleted only in
  the working tree, and it lacks the new untracked TUs plus the gitignored
  `third_party/bullet/`. Generating from a raw HEAD checkout would therefore be an environment
  artefact, not a valid parity test. The live input trees were copied in first:

```bash
git -C "E:/dev/dave/skygfx_plus_expIV" worktree add --detach ../skygfx_premake_verify HEAD
cd ../skygfx_premake_verify

rm -rf src resources external shaders third_party/bullet
cp -r "E:/dev/dave/skygfx_plus_expIV/src" src
cp -r "E:/dev/dave/skygfx_plus_expIV/resources" resources
cp -r "E:/dev/dave/skygfx_plus_expIV/external" external
cp -r "E:/dev/dave/skygfx_plus_expIV/shaders" shaders
cp -r "E:/dev/dave/skygfx_plus_expIV/third_party/bullet" third_party/bullet
cp "E:/dev/dave/skygfx_plus_expIV/premake5.lua" .

../skygfx_plus_expIV/premake5.exe --file=premake5.lua vs2015
# -> Generated build/skygfx.vcxproj, .vcxproj.filters, .sln; exit 0 (122 ms)

diff -u "../skygfx_plus_expIV/build/skygfx.vcxproj" build/skygfx.vcxproj

cd "E:/dev/dave/skygfx_plus_expIV"
git worktree remove --force ../skygfx_premake_verify
```

The generated vcxproj and the unified diff were kept in the agent temp dir for inspection:
`.../Temp/opencode/premake_verify/generated_skygfx.vcxproj`, `.../vcxproj.diff`.

Secondary static cross-check: the glob expansion table above was computed directly against the
live tree and matches the live project's 58 project TUs exactly.

## 4. Assertion table

All checks were run against the **generated** `build/skygfx.vcxproj` and compared with the live
`E:/dev/dave/skygfx_plus_expIV/build/skygfx.vcxproj`.

| # | Assertion | Result | Evidence |
|---|---|---|---|
| 1 | Generated ClCompile count = 179 | PASS | 179 |
| 2 | Bullet TUs = 115 | PASS | 115 |
| 3 | ImGui TUs = 6 | PASS | 6 |
| 4 | Generated ClCompile set == live set | PASS | only-live `[]`, only-gen `[]` |
| 5 | Generated ClInclude count = 23 | PASS | 23 |
| 6 | Generated ClInclude set == live set | PASS | only-live `[]`, only-gen `[]` |
| 7 | No deleted TU/header in generated project | PASS | searched all 15 names: none found |
| 8 | `src/render/PC_PlantsMgr.h` present | PASS | `..\src\render\PC_PlantsMgr.h` |
| 9 | Both `normalmap.cpp` TUs present | PASS | `..\src\rw\normalmap.cpp`, `..\src\render\normalmap.cpp` |
| 10 | Distinct object names (no LNK4042) | PASS | rw = `$(IntDir)normalmap_rw.obj`; render = default `normalmap.obj` |
| 11 | `normalmap.cpp` is the only duplicate basename | PASS | duplicate-basename scan over all 179 TUs = `{'normalmap.cpp': 2}` only |
| 12 | Required new TUs present | PASS | texalpha, noise, renderstate_monitor, profiler, treeWind, mirrorPass, forwardplus, postfx, all 9 `postfx_*`, debugmenu_theme, debugmenu_ui |
| 13 | Both `.rc` files are ResourceCompile inputs | PASS | `Resource.rc`, `VersionInfo.rc` |
| 14 | PlatformToolset v143 | PASS | 2 configs |
| 15 | CharacterSet MBCS (MultiByte) | PASS | 2 configs |
| 16 | WindowsTargetPlatformVersion 10.0.19041.0 | PASS | present in Globals |
| 17 | OutDir = `E:\games\gtasa_skygfx_plus\` | PASS | 2 configs |
| 18 | TargetName = `skygfx` | PASS | 2 configs |
| 19 | `rpnormmap.lib` linked | PASS | 2 AdditionalDependencies lists |
| 20 | Defines preserved | PASS | ClCompile define sets exactly equal (order differs); RC also adds the two `_CRT_*` defines |
| 21 | LibDirs exactly preserved | PASS | identical string in both configs |
| 22 | IncludeDirs preserved | PASS | equal after normalizing the one relativized path (`E:\dev(dave)\Sa_dev\...` → `..\..\Sa_dev\...`, same resolved dir from `build/`); RC adds bullet + WinKit `um` (ignored by rc.exe) |
| 23 | PreBuildEvent = `rem Shaders precompiled manually` | PASS | 2 configs; no fxc loop reintroduced |
| 24 | PostBuildEvent copies to `skygfx.asi` and deletes the dll | PASS | `copy /y "$(TargetPath)" "$(OutDir)skygfx.asi" && del /f "$(TargetPath)"` in 2 configs |

**Summary: 24/24 PASS.**

## 5. Residual diff (generated vs live)

`diff -u` produced 281 lines, but **no file-set difference at all**: 179/179 ClCompile and
23/23 ClInclude match as sets, and both ResourceCompile entries match.

### Cosmetic / behaviourally equivalent

- UTF-8 BOM added; `ToolsVersion="14.0"` added to `<Project>`.
- ClCompile / ClInclude entries reordered (premake sorts by vpath).
- Hand-written XML comments in the live project are lost (e.g. W7-C, W8-3, profiler notes).
  Comments have no effect on MSBuild.
- Indentation / whitespace changes (e.g. `<PlatformToolset>` line).
- `PreprocessorDefinitions` order: `NDEBUG`/`DEBUG` moved after the `_CRT_*` defines.
  Verified set-equal per config.
- Release `<DebugInformationFormat>None</DebugInformationFormat>` emitted explicitly (default).
- Release Link `<GenerateDebugInformation>false</GenerateDebugInformation>` emitted explicitly (default).
- `<ExternalWarningLevel>Level3</ExternalWarningLevel>` dropped (MSBuild default is Level3).
- `<ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>` dropped (default false).
- `<MapFile>E:\games\...\skygfx.map</MapFile>` dropped; `<GenerateMapFile>true</GenerateMapFile>`
  retained, so MSBuild writes `$(TargetDir)$(TargetName).map` — the same file.
- `normalmap_rw.obj` override now carries per-config `Condition` attributes; the value is
  identical in Release and Debug.
- `E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source` is emitted as
  `..\..\Sa_dev\GTASource\GTA SA RE\source`. Resolved against `build/`, this is the same
  directory; include-dir sets compare equal after normalization.
- `.vcxproj.filters` is regenerated (premake ordering). The live filters file is not used for
  compilation.

### Non-expressible in premake 5.0.0-alpha10 (accepted)

- `<UseEnv>true</UseEnv>` — no premake API; MSBuild default is false. Low risk: the project
  lists every required include/lib dir explicitly, including the Windows Kit `um` dir and the
  DirectX SDK. This is the only property difference that is not strictly cosmetic.
- `<WindowsSdkDir>` PropertyGroup at EOF — legacy hand-added property; the SDK is resolved from
  `WindowsTargetPlatformVersion` by `Microsoft.Cpp.props`.
- ResourceCompile's `/I` list also contains `third_party/bullet/src` and the Windows Kit `um`
  directory, and its defines also carry `_CRT_NONSTDC_NO_WARNINGS` / `_CRT_SECURE_NO_WARNINGS`.
  Both are ignored by `rc.exe`; they only show up in the XML diff.

**Functional regressions found: none.**

## 6. Regeneration procedure (for future agents)

**Never run premake in the main working tree.** It overwrites the authoritative
`build/skygfx.vcxproj` and `build/skygfx.vcxproj.filters`. Always use a scratch worktree:

```bash
git -C "E:/dev/dave/skygfx_plus_expIV" worktree add --detach ../skygfx_premake_verify HEAD
cd ../skygfx_premake_verify

# HEAD is not a snapshot of the working tree: it still has the deleted dead files,
# and it lacks untracked TUs + gitignored third_party/bullet. Mirror the live inputs.
rm -rf src resources external shaders third_party/bullet
cp -r "E:/dev/dave/skygfx_plus_expIV/src" src
cp -r "E:/dev/dave/skygfx_plus_expIV/resources" resources
cp -r "E:/dev/dave/skygfx_plus_expIV/external" external
cp -r "E:/dev/dave/skygfx_plus_expIV/shaders" shaders
cp -r "E:/dev/dave/skygfx_plus_expIV/third_party/bullet" third_party/bullet
cp "E:/dev/dave/skygfx_plus_expIV/premake5.lua" .

../skygfx_plus_expIV/premake5.exe --file=premake5.lua vs2015

diff -u "../skygfx_plus_expIV/build/skygfx.vcxproj" build/skygfx.vcxproj

cd "E:/dev/dave/skygfx_plus_expIV"
git worktree remove --force ../skygfx_premake_verify
```

Re-run the gates in section 4 against the generated file before trusting it. If the generated
project is ever promoted, `build/skygfx.vcxproj` must be reviewed by hand for the
`UseEnv`/`WindowsSdkDir` differences first.

## 7. Environment notes / residual artefacts

- The agent's Git Bash sandbox could not resolve the absolute `E:/dev/dave/skygfx_plus_expIV/...`
  path from inside the worktree; the equivalent relative path
  `../skygfx_plus_expIV/premake5.exe` worked. This is a shell-sandbox artefact, not a premake or
  path problem.
- A pre-existing detached worktree from an earlier (dead) lane remains at
  `C:/Users/aaaaaaaaa/AppData/Local/Temp/opencode/premake_verify`. It was not used for this
  verification and was left untouched.
- No MSBuild / `fast_build.py` invocation was made in either tree.

## 8. Blockers

None. `premake5.lua` is de-staled, real regeneration succeeds, and all functional gates pass.
