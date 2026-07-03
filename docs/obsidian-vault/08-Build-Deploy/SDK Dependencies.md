---
tags: [sdk, dependencies, paths, toolchain]
created: 2025-01-02
updated: 2025-01-02
---

# SDK Dependencies

## Core SDKs

| SDK | Path | Version | Purpose |
|-----|------|---------|---------|
| **RenderWare 3.7** | `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37` | 3.7 | RenderWare graphics API (D3D9 backend) |
| **Plugin SDK** | `E:\SDKs\plugin-sdk-master\plugin_sa` | master | GTA SA plugin SDK (structs, hooks, memory) |
| **DirectX SDK** | `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)` | June 2010 | D3D9, fxc.exe (shader compiler), d3dx9 |
| **Windows SDK** | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0` | 10.0.26100.0 | Win32 API, um/d3d9helper.h |
| **MSVC** | `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC` | 2022 Enterprise | C++ compiler (x86) |
| **MSBuild** | `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe` | Current | Build system |
| **GTA SA Source** | `E:\dev(dave)\Sa_dev\GTASource\gta-reversed-master` | master | Reverse-engineered GTA SA source (reference only) |
| **NormalMap by DK** | `E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources` | 1.01 | Normal map plugin for RW |

## Bundled Libraries (in repo)

| Library | Path | Purpose |
|---------|------|---------|
| **imgui** | `external/imgui/` | Debug menu, UI overlay |
| **injector** | `external/injector/` | Hook injection (InjectHook, InterceptCall) |
| **d3d9 headers** | `external/d3d9/` | D3D9 type definitions |

## Linked Libraries (from vcxproj)

```
d3d9.lib  d3dx9.lib  winmm.lib  rpnormmap.lib
kernel32.lib  user32.lib  gdi32.lib  winspool.lib  comdlg32.lib
advapi32.lib  shell32.lib  ole32.lib  oleaut32.lib  uuid.lib
odbc32.lib  odbccp32.lib
```

## Shader Compiler

- **fxc.exe**: `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe`
- **Profiles**: ps_3_0 (pixel), vs_3_0 (vertex)
- **CSO output**: `resources/cso/`

## Target Platform

| Property | Value |
|----------|-------|
| **Game** | Grand Theft Auto San Andreas v1.0 US |
| **Game dir** | `E:\games\gtasa_skygfx_plus` |
| **ASI target** | `skygfx.asi` (x86, MSVC 2022) |
| **Deploy** | `fast_build.py` copies ASI + INI + DLL to game dir |

## Hook Compatibility Rules

All hooks target GTA SA v1.0 US (steam/retail executable). Hook methods:
- `InjectHook(addr, func, PATCH_JUMP)` — replaces function at addr with JMP to func
- `InterceptCall(&orig, func, addr)` — saves original CALL target, replaces with func
- `WRAPPER` macros — for RW/game functions at known addresses (gta.cpp)

**Critical rules**:
- Only hook addresses verified against SA 1.0 US executable
- Never hook addresses already used by other ASI plugins (CLEO, modloader, etc.)
- Use `InterceptCall` over `InjectHook` where possible (safer, chains properly)
- All hooks installed in `DllMain` → `DelayedInit` → specific `hook*()` functions

## MoonLoader / Lua (Planned)

- **MoonLoader**: Lua scripting runtime for GTA SA
- **Purpose**: TransFender replacement script (extra wheels, spoilers via clump manipulation)
- **Packaging**: Shipped as optional .lua file; game runs normally without it
- **Scope**: Clump geometry scaling/stretching for universal spoiler fitment
- **Mounting**: Mark mounting spots on vehicles, scale/rotate spoiler geometry to fit

## See Also
- [[08-Build-Deploy/Build System]] — Build automation
- [[09-References/RW SDK Reference]] — RenderWare API details
- [[01-Architecture/Project Paths]] — All project paths
