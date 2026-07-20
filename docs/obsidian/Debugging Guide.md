# Debugging Guide

> Complete debugging reference for skygfx_plus: logs, crash analysis, hooks, and common issues.
> See also: [[Crash Analysis Reference]] for detailed crash patterns and [[Hook Architecture]] for hook details.
>
> **Note**: GTA SA uses RW 3.6.0.3. skygfx_plus uses RW 3.7 SDK headers and backports 3.7 features.

## Log File

SkyGFX writes a timestamped log to `skygfx_dbg.log` in the game directory. Every hook, shader load, crash, and config change is logged.

### Log Format
```
[YYYY-MM-DD HH:MM:SS.mmm] Message
```

### Key Log Entries

#### Startup Sequence
```
[2024-01-01 12:00:00.000] === skygfx loading ===
[2024-01-01 12:00:00.001] dllModule=0x10000000, g_logPath=...
[2024-01-01 12:00:00.002] DynBaseAddr=0x400000
[2024-01-01 12:00:00.003] ver check: 0x82457C=94BF, 0x8245BC=94BF (expect 0x94BF)
[2024-01-01 12:00:00.004] ver check OK
[2024-01-01 12:00:00.005] applying hooks...
[2024-01-01 12:00:00.010] RpNormMapPluginAttach: success
[2024-01-01 12:00:00.015] === InjectDelayedPatches complete ===
```

#### Shader Loading
```
[2024-01-01 12:00:01.000] CreateShaders started
[2024-01-01 12:00:01.001]   makePS(111): creating pixel shader...
[2024-01-01 12:00:01.002]   makePS(111): OK sh=0x12345678
[2024-01-01 12:00:01.003]   loading SSAO shader...
[2024-01-01 12:00:01.004]   SSAO=0x12345678
```

#### Crashes
```
[2024-01-01 12:00:02.000] CRASH: code=0xC0000005 at=0x7FACC0 EAX=... EBX=...
[2024-01-01 12:00:02.001] AUTO-FIX: skipping NULL CColModel store at 0x5B5192
```

## Crash Handler

### VEH Handler (`src/core/diagnostics.cpp`)

1. **Logs all registers** at crash time
2. **Writes minidump** (`skygfx_dbg.dmp`)
3. **Shows MessageBox** with crash details (forced to front)
4. **Diagnostic-only** — returns `EXCEPTION_CONTINUE_SEARCH`

### Freeze Watchdog

- Heartbeat counter updated every frame
- 30-second timeout triggers warning
- ESC key for force-kill
- Previous crash marker shown on next launch

### Reading Crash Info
```
CRASH: code=0xC0000005 at=0x7FACC0
  EAX=00000000 EBX=12345678 ECX=00000000 EDX=00000000
  ESI=12345678 EDI=00000000 EBP=12345678 ESP=12345678
  Access: READ at 0x00000008
```

| Field | Meaning |
|-------|---------|
| `code` | Exception code (0xC0000005 = access violation) |
| `at` | Instruction address that crashed |
| `EAX-EDI` | CPU registers at crash |
| `Access` | READ/WRITE/EXECUTE + faulting address |

### Common Crash Codes

| Code | Meaning | Common Cause |
|------|---------|--------------|
| `0xC0000005` | Access violation | NULL pointer, freed memory |
| `0xC000001D` | Illegal instruction | Corrupted code, wrong RW version |
| `0xE06D7363` | C++ exception | Thrown by readint/readfloat (safe) |
| `0x40010006` | Debug break | INT3 in code |

## Known Crash Patterns

See [[Crash Analysis Reference]] for detailed analysis of:
- **0x7F39FB**: NULL frame in pool iteration (RW SDK)
- **0x5A5735**: CopyTexture NULL dereference
- **Game binary obfuscation**: On-disk bytes ≠ runtime code

## Debug Menu (F4)

Press **F4** to toggle the debug menu. All settings can be changed at runtime.

### Menu Structure
```
SkyGFX/
├── Config selector (cycle INI configs)
├── Reload Inis
├── Dual-pass Global
├── PS2-modulate Global
├── Building Pipeline [PS2/Xbox/GTAIV]
├── Detail Maps
├── Vehicle Pipeline [PS2/PC/Xbox/Spec/Mobile/Neo/LCS/VCS/Env/GTAIV]
├── Vehicle Env Map Size
├── Misc/
│   ├── Add Ambient to Grass
│   ├── Grass Backface Culling
│   ├── Ped Shadows [Default/PS2/PC]
│   ├── Stencil Shadows [Default/PS2/PC]
│   ├── Colour filter [None/PS2/PC/Mobile/III/VC/VCS/GTAIV]
│   ├── Radiosity
│   ├── Radiosity type [PS2/Shader]
│   ├── Blur PS2 Colour Filter
│   ├── Sun Glare
│   └── [Shininess/Specularity/Power/Fresnel sliders]
├── Advanced/
│   ├── Dual-pass [Default/Buildings/Vehicles/Peds/Grass]
│   ├── PS2-modulate [Buildings/Grass]
│   ├── Infrared/Night vision [PS2/PC]
│   ├── Grain Filter [PS2/PC]
│   └── Z-write Alpha Threshold
├── ScreenFX/
│   ├── Enable YCbCr tweak
│   └── [Y/Cb/Cr scale/offset sliders]
├── SSAO/
│   ├── Enable SSAO
│   └── [Radius/Power/Kernel/Samples]
├── SMAA/
│   ├── Enable SMAA
│   ├── SMAA Preset [LOW/MEDIUM/HIGH/ULTRA]
│   └── [Predication/Temporal]
└── GTA IV/
    ├── Enable GTA IV Mode
    └── [Desaturation/Gamma/Vignette/Bloom/Exposure]
```

## Verifying Hooks

Check the log for these entries to verify hooks installed correctly:

### Building Pipeline
```
[...] hookBuildingPipe: OK
[...] CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2: OK
```

### Vehicle Pipeline
```
[...] hookVehiclePipe: OK
```

### Post Effects
```
[...] ColourFilter_switch: start (ssao=1 smaa=1)
[...] Radiosity: start (do=1 rad=1 vcs=0)
```

### Normal Mapping
```
[...] RpNormMapPluginAttach: success
[...] Normal map found on vehicle atomic: ...
```

## Memory Addresses

### Key skygfx Addresses

| Address | Purpose |
|---------|---------|
| `0xC02C68` | Building Pipeline (Obj) |
| `0xC02C1C` | Building DN Pipeline |
| `0xC02D24` | Default vehicle pipeline |
| `0xC02D2C` | EnvMap pool |
| `0x8E2458` | Projection matrix |
| `0xC9BC80` | View matrix |
| `0xC94C30` | ViewProj matrix |

### RW SDK Functions

| Address | Function | SDK Source |
|---------|----------|-----------|
| `0x7F0410` | `RwFrameCreate` | rwcore |
| `0x7F0990` | `RwFrameGetLTM` | rwcore |
| `0x804EF0` | `RwObjectHasFrameSetFrame` | rwcore |
| `0x74C790` | `RpGeometryForAllMaterials` | rpworld |
| `0x72FC40` | `GetPipelineID` | rpworld |
| `0x72FC50` | `SetPipelineID` | rpworld |
| `0x811C30` | `RpMatFXAtomicQueryEffects` | rpmatfx |

## Common Issues

### 1. Shader Not Loading
```
makePS(111): FindResource FAILED
```
**Fix**: Check `resources/cso/` directory exists and CSO files are present.

### 2. Wrong RW Version
```
ver check WARNING - v1 mismatch (0x1234 != 0x94BF)
```
**Fix**: Ensure GTA SA 1.0 US (no Steam/Rockstar Games launcher).

### 3. Crash on Startup
```
CRASH: code=0xC0000005 at=0x5B5192
```
**Fix**: Usually auto-fixed by crash handler. If persistent, check COL files.

### 4. Black Screen
- Check `skygfx_dbg.log` for shader compilation errors
- Verify `DXSDK_DIR` environment variable
- Try disabling SSAO/SMAA in INI

### 5. Performance Issues
- Reduce `envMapSize` (256 → 128)
- Disable SSAO (`ssaoEnable=0`)
- Disable SMAA (`smaaEnable=0`)
- Set SMAA preset to LOW (`smaaPreset=0`)

### 6. Normal Map Crash
```
normalmap: RpNormMapPluginAttach FAILED
```
**Cause**: Plugin already attached from DllMain. `normalmap_init()` returns early.
**Fix**: Remove `RpNormMapPluginAttach()` call from DllMain, let `normalmap_init()` handle it.

### 7. NULL Frame Crash
```
CRASH: code=0xC0000005 at=0x007F39FB READ at 0x00000008
```
**Cause**: Pool entry has NULL frame pointer. RW SDK reads frame+8 → crash.
**Fix**: Guard with `if(!RpAtomicGetFrame(atomic)) return FALSE;` in pipeline checks.

## Diagnostic Commands

### Check DLL Module Base
```cpp
HMODULE dllMod = GetModuleHandle("skygfx.asi");
dbglog("skygfx base: %p", dllMod);
```

### Check RW Engine State
```cpp
// From rwplcore.h
RwEngine* engine = &RWSRCGLOBAL(engine);
dbglog("dirtyFrameList: %p", &engine->frameDirtyList);
```

### Verify Hook Installation
```cpp
// Check if hook is installed by reading the first byte
BYTE* hookAddr = (BYTE*)0x5D7F40;
if(*hookAddr == 0xE9)  // JMP opcode
    dbglog("Hook at 0x5D7F40: INSTALLED");
else
    dbglog("Hook at 0x5D7F40: NOT installed (first byte=0x%02X)", *hookAddr);
```

## See Also

- [[Crash Analysis Reference]]
- [[Hook Architecture]]
- [[RenderWare SDK Architecture]]
- [[Configuration Reference]]
- [[Build Guide]]
