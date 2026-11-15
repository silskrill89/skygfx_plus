# SkyGFX Plus Dependencies

> **Reconciled 2026-11-07** against the disk at `E:\dev(dave)\skygfx_plus_expIV`.
> **Build truth:** `build/skygfx.vcxproj` is the authoritative, hand-maintained build input; `premake5.lua` is a reference mirror only (see its header).
> **Tags:** **VENDORED** = lives in this repo · **EXTERNAL-TOOL** = installed tool/SDK outside the repo · **REFERENCE-ONLY** = studied, never compiled or shipped.
> Every path below was existence-checked during reconciliation (✅ = exists on disk; ⚠ = stale/missing).

## Toolchain — EXTERNAL-TOOL

| Dependency | Version (on disk) | Path | Status |
|---|---|---|---|
| MSVC (VS 2022 Enterprise) | **toolset v143** (`PlatformToolset` in `build/skygfx.vcxproj`); installed toolsets: 14.44.35207, 14.29.30133, 14.16.27023 | `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC` | ✅ |
| MSBuild | VS2022 `Current` (invoked by `tools/fast_build.py`) | `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe` | ✅ |
| Windows SDK | **10.0.19041.0** pinned (`WindowsTargetPlatformVersion`); 10.0.10240.0 / 10.0.22621.0 / 10.0.26100.0 also installed | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0` | ✅ |
| premake5 | **5.0.0-alpha10** (`premake5.exe --version`) | `premake5.exe`, `premake5.cmd`, `premake5.lua` — checked in | ✅ **VENDORED** — reference generator only, **not** part of the normal build |
| Python | 3.10-32 (stated in `requirements.txt` header) | `C:\Program Files (x86)\Python310-32` | ✅ |

## DirectX SDK & shader compiler — EXTERNAL-TOOL

| Dependency | Version | Path | Status |
|---|---|---|---|
| DirectX SDK (June 2010) | June 2010 | `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)` — `Include` (on include path), `Lib\x86\d3dx9.lib` (linked), `Utilities\bin\x86\fxc.exe` (shader compiler) | ✅ all three checked |
| Alternate DX SDK copy | June 2010 | `E:\SDKs\Microsoft DirectX SDK (June 2010)` (present; **not** used by this project's build) | ✅ |
| **microsoft.dxsdk.d3dx NuGet** | **9.29.952.8** (nuspec) — includes its own `build/native/release/lib/x86/d3dx9.lib` | `external/microsoft.dxsdk.d3dx.9.29.952.8/` | ✅ **PRESENT but NOT referenced** by `build/skygfx.vcxproj` (0 hits) — `d3dx9.lib` comes from the June 2010 SDK; the package is kept as an alternate redistributable D3DX source |

### Shader compilation (FXC)
- `tools/fast_build.py` drives `fxc.exe` for every HLSL entry point → `resources/cso/*.cso` (124 `.cso` on disk; 110 outputs per `premake5.lua` header). Profiles: `ps_3_0` / `vs_3_0` (SM3.0).
- MSBuild's PreBuildEvent is literally `rem Shaders precompiled manually` — shaders are **not** built by the project file (see `premake5.lua` header §6).

## Python tooling — EXTERNAL-TOOL (pinned in `requirements.txt`)

| Package | Used by (verified imports) |
|---|---|
| `watchdog` | `tools/fast_build.py`, `tools/doctor.py` |
| `numpy` | `tools/assets.py`, `tools/pack_helper_tex.py` |
| `Pillow` | `tools/assets.py`, `tools/pack_helper_tex.py` |
| `yt-dlp` | `tools/research.py`, `tools/doctor.py` |
| `PyPDF2` | `tools/extract_pdf_info.py`, `tools/doctor.py` |

- Installer runtime deps are imported by `installer/` but **not pinned** in `requirements.txt`: PyQt5 (`installer/ui/`), requests, beautifulsoup4, py7zr, rarfile (`installer/*.py`). See `CREDITS.md` → Installer Development Credits.

## C++ third-party in the repo

| Library | Version | Where | Tag / notes |
|---|---|---|---|
| Bullet Physics | **2.87** (per `build/skygfx.vcxproj` comment + `third_party/bullet/reference/Ragdoll_reference.txt`) | `third_party/bullet/src` — 115 TUs compiled | **VENDORED** — zlib, `third_party/bullet/LICENSE.txt` vendored verbatim from upstream (https://github.com/bulletphysics/bullet3), tracked via gitignore carve-out |
| Dear ImGui | **1.92.9 WIP** (`IMGUI_VERSION` in `imgui.h`) | Build compiles `E:\SDKs\imgui-master` (4 core + 2 backend TUs); vendored mirror `external/imgui/` (with `LICENSE.txt`) is **not** in the build | **EXTERNAL** (build input) + **VENDORED** mirror present |
| injector | thelink2012/injector (LINK/2012; zlib-style `LICENSE`) | `external/injector/` — git submodule (`.gitmodules`); included by `src/core/main.cpp` | **VENDORED** (submodule) |
| RenderWare 3.7 D3D9 headers | RW SDK 3.7 snapshot (RenderWare copyright headers) | `external/d3d9/` — 121 files | **VENDORED** |
| ini_parser (linb) | — | `external/ini_parser.hpp` (used by `src/core/main.cpp`, `src/peds/peds_pipe.h`) | **VENDORED** |
| SMAA | iryoku 2013, **BSD** (`SMAA_reference/LICENSE.txt`) | `SMAA_reference/` (SMAA.hlsl, Textures, Scripts, Demo) | **REFERENCE-ONLY** — algorithm ported into `src/render/postfx_smaa.cpp`; area/search textures embedded in the binary |
| DK22Pac helper sources | Normal Map Plugin samples / stubs | `external/patch/CPatch.h`, `external/plugin/plugin.h`, `external/normalmap/`, `external/nm_matbuild/` | **VENDORED** reference helpers — **not compiled** (no `ClCompile` entries in `build/skygfx.vcxproj`) |

## External SDKs on include/lib paths — EXTERNAL

| SDK | Path | Role | Status |
|---|---|---|---|
| RenderWare 3.7 SDK | `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9` + `...\lib\d3d9\release` (66 libs incl. `rpnormmap.lib`) | headers/types + linked `rpnormmap.lib`. **The game itself runs RW 3.6 baked into the exe** — do not use RW 3.7 API calls (`.serena/memories/project.md`) | ✅ |
| Plugin SDK (DK22Pac) | `E:\SDKs\plugin-sdk-master\plugin_sa` | include dir in `build/skygfx.vcxproj`; used in source comments for struct/offset validation | ✅ |
| Normal Map Plugin (DK22Pac) | `E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources` | include + libdir; `rpnormmap.lib` actually resolves from the RWSDK37 lib dir above (this sources dir contains no `.lib`) | ✅ |
| DirectX SDK (June 2010) | see DirectX section | include + lib + FXC | ✅ |
| GTASource RE headers | vcxproj/premake cite `E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source` | ⚠ **STALE — path does not exist** (tree relocated). Live copy: `E:\dev(dave)\releases\GTASource\GTA SA RE\source` | ⚠ stale include entry |

## Reference-only (studied — never compiled, never shipped)

| Source | Path | Status |
|---|---|---|
| Rockstar **GTASource** collection (incl. `GTA SA RE`, `gtasa_src-main`, `OpenSA`, `gta-reversed-master`) | `E:\dev(dave)\releases\GTASource` | ✅ |
| **gta-reversed** | `E:\SDKs\gta-reversed` | ✅ |
| **librw** | `E:\SDKs\librw` (+ `librw.7z`) | ✅ |
| **SMAA** upstream (iryoku, BSD) | `E:\SDKs\smaa` (`LICENSE.txt`) | ✅ |
| **SADestruction** | `E:\SDKs\SADestruction` | ✅ path exists — ⚠ **NO LICENSE FILE** → **REFERENCE-ONLY; their code must not ship** (`docs/plans/2026-10-29-destruction-mod-inventory.md`) |
| CryEngine / LibertyRecomp material | (no local checkout required) | per `docs/Credits.md` — reimplemented, not shipped |

## Legacy env-var block (corrected values from the old Jul-2026 file)

```
# RenderWare SDK 3.7 (headers + libs)
RW_SDK=E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9

# DirectX SDK (June 2010) — include/lib/fxc
DX_SDK=C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)

# Windows SDK (10.0.19041.0 pinned in build/skygfx.vcxproj)
WINDOWS_SDK=C:\Program Files (x86)\Windows Kits\10

# MSVC root — build uses toolset v143 (installed: 14.44.35207, 14.29.30133, 14.16.27023)
MSVC=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC

# GTASource reference (relocated; old E:\dev(dave)\Sa_dev\... path is dead)
GTA_SA_RE=E:\dev(dave)\releases\GTASource\GTA SA RE\source
```

## Dropped / corrected from the previous (stale, Jul 2026) file

- `GTA_SA_RE=E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source` — **path does not exist** (`E:\dev(dave)\Sa_dev` is gone); replaced with the relocated `E:\dev(dave)\releases\...` copy. The same dead path still sits in `build/skygfx.vcxproj`'s include list (⚠ flagged above; fixing the vcxproj is outside this file's scope).
- `IMGUI` / `PLUGIN_SDK` / `NORMALMAP_SDK` marked "excluded for now" — **stale**: all three are active include paths in `build/skygfx.vcxproj` (ImGui is also compiled from `E:\SDKs\imgui-master`).
- `MSVC=...\14.16.27023` presented as *the* compiler — **stale**: the project builds with toolset **v143**; 14.16.27023 is only one of three installed toolsets.
- Previously unlisted, now documented above: **premake5** (checked in), **Python tooling** (`requirements.txt`), **FXC** shader compilation, **Bullet** (`third_party/bullet/`), **SMAA** (`SMAA_reference/`), **injector**, **RW 3.7 headers** (`external/d3d9/`), **microsoft.dxsdk.d3dx NuGet** (present, unused), **SADestruction / gta-reversed / librw** reference trees.
