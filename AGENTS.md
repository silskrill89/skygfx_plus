# AGENTS.md — skygfx_plus_expIV

## Project

GTA San Andreas graphics mod (ASI plugin). Replaces building/vehicle rendering pipelines with PBR shaders. DLL loaded via ASI loader. Hooks into RW 3.6 D3D9 pipeline baked into the game exe.

## Build

```bash
python tools/fast_build.py              # Smart incremental (default)
python tools/fast_build.py --fastest    # Skip detection, always build+deploy
python tools/fast_build.py --rebuild    # Clean full rebuild + deploy
python tools/fast_build.py --shaders    # Only compile HLSL → CSO
python tools/fast_build.py --launch     # Build + launch game
```

- **FXC**: `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe`
- **MSBuild**: VS2022 Enterprise at `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe`
- **Deploy target**: `E:\games\gtasa_skygfx_plus\skygfx.asi`
- **Game dir**: `E:\games\gtasa_skygfx_plus`
- **Build output**: `build\Release\skygfx.dll` → renamed to `skygfx.asi`
- **Build log**: `premake_log.txt`
- **Diag log**: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`

## Shader Pipeline

HLSL → FXC (SM3.0: ps_3_0 / vs_3_0) → `.cso` → embedded in DLL via `Resource.rc`

- **Shader source**: `shaders/ps/`, `shaders/vs/`, `shaders/include/`
- **Compiled CSOs**: `resources/cso/`
- **Resource definitions**: `resources/resource.h`, `resources/resource.rc`
- **Multi-entry compilation**: `VehiclePBR_Modern.hlsl` compiles 7 entry points via `tools/fast_build.py` (multi_entry list): `main`, `main_specCarFx`, `main_mobileVehicle`, `main_building`, `main_rubber`, `main_normMapVehicle`, `main_ps2EnvSpecFx`
- **Include path**: shaders include via relative `../include/` from entry HLSL files
- **Critical include**: `PBR_Common.hlsl` (GGX/Smith/Schlick/Cloud shadow)

## Architecture

### Render Pipeline Mapping

```
PIPELINE_PBR(0)  → buildingPipe=BUILDING_PBR(3), vehiclePipe=CAR_MODERN(10), colorFilter=MODERN(8)
PIPELINE_PS2(1)  → buildingPipe=BUILDING_PS2(0),  vehiclePipe=CAR_PS2(0)
PIPELINE_XBOX(2) → buildingPipe=BUILDING_XBOX(1), vehiclePipe=CAR_XBOX(2)
PIPELINE_MOBILE(3) → vehiclePipe=CAR_MOBILE(4)
PIPELINE_GTAIV(4) → buildingPipe=BUILDING_GTAIV(2), vehiclePipe=CAR_GTAIV(9)
```

### Key Source Files

| File | Purpose |
|------|---------|
| `src/core/main.cpp` | DLL entry, INI config, pipeline switch, hooks |
| `src/render/buildingPipe.cpp` | Building render callbacks (PS2/Xbox/PBR) |
| `src/render/vehiclePipe.cpp` | Vehicle render callbacks (PS2/Xbox/PBR/modern) |
| `src/render/pipelinecommon.cpp` | Shared: matrix math, light uploads, shader loading (`CreateShaders`) |
| `src/render/postfx.cpp` | PostFX chain: SSAO, colour filter, SSS blur, SMAA, radiosity |
| `src/skygfx.h` | Enums, config struct, all externs, debug macros |
| `src/rw/gta.h` | RW SDK globals (`pDirect`, `pAmbient`, `Scene`, `d3d9device`) |
| `src/rw/gta.cpp` | WRAPPER/EAXJMP declarations for RW SDK functions |

### PBR Shader Register Layout

**VS (buildingPBRVS / vehiclePBRVS):**
- c0-c3: WVP matrix
- c4: ambient color
- c5-c11: 7 direct light colors (VS)
- c12-c18: 7 direct light directions (VS)
- c19: material color
- c20: surface properties
- c24-c27: world matrix
- c29: shader params (color scale)
- c30-c31: day/night params
- c32-c35: texture transform
- c36: eye position

**PS (VehiclePBR_Modern):**
- c0: surfProps {ambient, 0, diffuse, prelit_flag}
- c1: fxParams {fresnel, shininess, specularity, lightmult}
- c2: eyePos
- c3: iblParams
- c4: cloudShadow
- c5-c11: directCol/lightCol (PS)
- c12-c18: directDir/lightDir (PS)
- c19: matCol
- c22: pbrParams — **MUST use `pipeUploadPBR()`** — NEVER upload manually
- c23: paintNoise — **MUST use `pipeUploadPBR()`**
- c24: ambientColor — used by BOTH vehicle `main()` AND building `main_building()` (uploaded by both callbacks)

### Texture Registers

- s0: diffuse texture
- s1: environment/reflection map
- s2: mask texture
- s3: IBL cubemap (set as `g_iblTex` raw D3D9 pointer)
- s4: screen-space normal buffer (optional)

### Resource IDs (PBR)

| ID | Name | Value |
|----|------|-------|
| IDR_VEHICLEPBR_MODERN | VehiclePBR_Modern | 203 |
| IDR_VEHICLEPBRVS | vehiclePBRVS | 211 |
| IDR_BUILDINGPBRVS | buildingPBRVS | 238 |
| IDR_BUILDINGPBRPS | buildingPBRPS | 239 |
| IDR_RUBBER_VEHICLE_MODERN | Rubber_Vehicle_Modern | 240 |

## Rules

### Critical Shader Rules
- **c22/c23 upload**: ALWAYS use `pipeUploadPBR()` from `pipelinecommon.cpp`. Order: `{glossiness, specular, ...}`. Manual upload produces flat/dark output.
- **Color filter for PBR**: MUST be `COLORFILTER_MODERN(8)`. Runtime override in `postfx.cpp ColourFilter_switch`.
- **SM3.0 only**: All shaders compile as ps_3_0 / vs_3_0. No SM4/5 features.
- **Division safety**: Guard all `normalize()` calls against zero-length vectors. Guard `D_GGX` denominator. Use `1e-7` epsilon minimum. NaN propagates as black in SM3.0.
- **Debug logging**: Use `dbglog_throttle("tag")` (global 2s interval) for per-frame logs. Use `dbglog()` for init/error logs. Log file: `skygfx_dbg.log`.

### Token Efficiency Rules
- **AGGRESSIVE COMPRESSION**: This project hits context limits extremely fast. Compress early, compress often. Every 5-8 tool calls, evaluate whether older ranges are stale and compress them. Do NOT wait for the max context warning. When the user asks to compress, do it IMMEDIATELY — compress the widest stale range possible in one pass.
- **LSAI First**: Use LSAI for all symbol lookup (saves 90%+ tokens). Never grep/glob for symbol search. Use lsai_source instead of Read for method bodies. Use lsai_outline instead of reading entire files.
- **Specialist Routing**: Single-file mechanical fix → @fixer (1/2 cost). Symbol lookup → @explorer (2x faster, 1/2 cost). External research → @librarian (2x faster, 1/2 cost). Architecture decisions → @oracle (5x better decisions).
- **Parallel Execution**: Independent tasks → Parallel background specialists. Dependent tasks → Sequential with dependency tracking. Write conflicts → Never parallelize overlapping writes.
- **Session Reuse**: Reuse available sessions when context fits. Fresh sessions when too much unrelated context. Track session IDs for background tasks.
- **Minimal Context**: Reference paths/lines instead of pasting files. Brief delegation notices instead of verbose explanations. Concise status updates instead of narrating work.

### Safety Rules
- **Null Pointer Guards**: Always check RwFrame/RwCamera/RwRaster pointers before use. Check return values of RwV3dNormalize.
- **Division Safety**: Guard all 1.0f/x operations with epsilon checks. Use max(x, 1e-7f) pattern.
- **Buffer Safety**: Use strncpy/strncat instead of strcpy/strcat. Use snprintf instead of sprintf.
- **Memory Safety**: Prefer stack allocation over heap. If heap required, ensure matching delete/free.
- **Render State**: Save and restore all D3D9 render states modified in callbacks.

## Hooks

Key game function hooks (addresses are for GTA SA v1.0 US):
- `0x704D1E` → `ColourFilter_switch` (PostFX chain)
- `0x53EBE9` → `DrawFinalEffects`
- `0x5D7100` → Building pipe creation
- `0x5D9FE9` → Vehicle pipe creation

## Gotchas

- RW 3.6 SDK is baked into the game exe — WRAPPER/EAXJMP calls real RW functions at hardcoded addresses. Do NOT replace with RW 3.7 API calls.
- Render state is persistent/shared across all pipelines. PostFX must restore states it modifies.
- `pRasterFrontBuffer` may be 2048x2048 while camera is 1920x1080 — UpdateFrontBuffer handles this via RwRasterRenderFast.
- `g_iblTex` is a raw `IDirect3DTexture9*`, not an `RwTexture*` — set via `dev->SetTexture(3, g_iblTex)`.
