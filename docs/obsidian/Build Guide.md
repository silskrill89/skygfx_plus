# Build Guide

## Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| Visual Studio | 2022 (v143) | Community or higher |
| DirectX SDK | June 2010 | `fxc.exe` for shader compilation |
| RenderWare SDK | 3.7 | `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\` |
| Premake5 | — | Included as `premake5.exe` |
| ImGui | — | Included at `E:/SDKs/imgui-master` (or adjust premake5.lua) |
| plugin-sdk | — | Included at `E:/SDKs/plugin-sdk-master` (or adjust premake5.lua) |

## Build Steps

### 1. Generate VS Project

```bat
cd E:\dev(dave)\skygfx_plus_expIV
premake5.exe vs2015 --file=premake5.lua
```

> [!note]
> Use `vs2015` even with VS2022 — the generated project will be auto-upgraded.

### 2. Update Platform Toolset

After generation, edit `build/skygfx.vcxproj` and change:
```xml
<PlatformToolset>v140</PlatformToolset>
```
to:
```xml
<PlatformToolset>v143</PlatformToolset>
```

### 3. Set Environment Variables

```bat
set RWSDK37=E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9
set DXSDK_DIR=E:\SDKs\Microsoft DirectX SDK (June 2010)
```

### 4. Build

```bat
msbuild build\skygfx.sln /p:Configuration=Release /p:Platform=Win32
```

Or use `build_release.bat` (adjust paths first).

### 5. Output

The compiled DLL is at `bin\Release\skygfx.dll`. Rename to `skygfx.asi` for ASI loader compatibility.

## Shader Compilation

Shaders compile automatically via prebuild commands. To compile manually:

```bat
cd shaders
shaders.bat
```

Requires `%DXSDK_DIR%\Utilities\bin\x86\fxc.exe`.

## SDK Paths

The premake5.lua references these paths — adjust if your SDKs are elsewhere:

| Path | Purpose |
|------|---------|
| `E:/SDKs/imgui-master` | ImGui for debug menu |
| `E:/SDKs/plugin-sdk-master` | GTA SA class definitions |
| `E:/SDKs/Microsoft DirectX SDK (June 2010)` | D3D9 headers and `fxc.exe` |
| `%RWSDK37%` | RenderWare 3.7 headers |

## Troubleshooting

| Error | Fix |
|-------|-----|
| `Windows SDK version not found` | Install Windows 10 SDK or retarget in VS |
| `PlatformToolset v140 not found` | Change to v143 in vcxproj |
| `fxc.exe not found` | Set `DXSDK_DIR` environment variable |
| `RWSDK37 not found` | Set `RWSDK37` environment variable |
| Link errors for `d3dx9.lib` | Ensure DirectX SDK June 2010 is installed |

## See Also

- [[Shader Architecture]]
