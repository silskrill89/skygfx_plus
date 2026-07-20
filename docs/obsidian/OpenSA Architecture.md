# OpenSA Architecture

## Project Overview
**OpenSA** is an open-source San Andreas experience built on top of GTA:SA. It consists of two core components:

### MTA64 (Core)
- **Location**: `H:\TEST\slop\mta_64`
- **Technology**: .NET 9.0, x64, C# + C++ native DLL
- **Purpose**: Multiplayer framework, memory bridging, script execution, VFS
- **Solution**: `MtaBridge.Core.sln` (3 projects)

### SKYGFXPlus (Rendering)
- **Location**: `E:\dev(dave)\skygfx_plus_expIV`
- **Technology**: C++ DLL (ASI), RenderWare SDK, HLSL shaders
- **Purpose**: PS2/Xbox/PC rendering pipeline emulation, post-FX, normal mapping
- **Output**: `skygfx.asi` injected into game process

## MTA64 Architecture

```
┌─────────────────────────────────────────────┐
│  MtaBridge.Desktop (WPF + WebView2)         │
│  - UI Dashboard                             │
│  - Game Directory Validation                │
│  - VFS Browser                              │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│  MtaBridge.Core (.NET 9.0, x64)             │
│  - Launcher64: CreateProcess + Inject       │
│  - PluginSdkBridge: CEntity/CPed/CVehicle   │
│  - OpcodeBridge: SCM opcode interception    │
│  - MemoryHook: x86/x64 JMP injection        │
│  - ModScriptBridge: Lua/JS/C# execution     │
│  - LooseFileLoader: Server-delivered VFS    │
│  - IvNetworkBridge: GTA IV multiplayer      │
│  - RdrBridge: RDR1/RDR2 native hooks        │
└──────────────────┬──────────────────────────┘
                   │ P/Invoke
┌──────────────────▼──────────────────────────┐
│  MtaBridgeNative (C++ DLL)                  │
│  - ILuaModuleManager interface              │
│  - Lua function registration                │
│  - Direct game memory access                │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│  Game Process (GTA:SA / GTA IV / RDR)       │
│  - RenderWare 3.6/3.7 (SA)                  │
│  - RAGE (IV/RDR)                            │
│  - SCM Script Engine                        │
│  - D3D9 Rendering                           │
└─────────────────────────────────────────────┘
```

### Supported Games
| Game | Executable | Engine | Status |
|------|-----------|--------|--------|
| GTA III | gta3.exe | RenderWare 3.3 | Opcode table mapped |
| GTA Vice City | gta-vc.exe | RenderWare 3.4 | Opcode table mapped |
| GTA San Andreas | gta_sa.exe | RenderWare 3.6 | Full struct layouts |
| GTA IV | GTAIV.exe | RAGE | Native hash hooks |
| RDR1 | RDR.exe | RAGE | Native hash hooks |
| RDR2 | RDR2.exe | RAGE | Native hash hooks |

### Key Addresses (GTA:SA v1.0 US)
- SCM execution table: `0x8A5A00`
- CEntity size: `0x50` bytes
- CPed size: `0x7C0` bytes, health at `0x47C`
- CVehicle size: `0xA18` bytes, engineHealth at `0x490`
- RenderWare engine: `*(void**)0x58FFC0`
- D3D9 device: `*(IDirect3DDevice9**)0xC97C28`

## SKYGFXPlus Architecture

```
┌─────────────────────────────────────────────┐
│  skygfx.asi (DLL injected via ASI loader)   │
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │  DllMain                            │    │
│  │  - Version check (0x94BF)           │    │
│  │  - Apply all hooks via InjectHook   │    │
│  │  - Load INI config                  │    │
│  └──────────────┬──────────────────────┘    │
│                 │                           │
│  ┌──────────────▼──────────────────────┐    │
│  │  Initialise_skygfx                  │    │
│  │  - Create all shaders (HLSL)        │    │
│  │  - Init SMAA, SSAO, GTAIV pipes     │    │
│  │  - Register render callbacks        │    │
│  └──────────────┬──────────────────────┘    │
│                 │                           │
│  ┌──────────────▼──────────────────────┐    │
│  │  Rendering Pipelines                │    │
│  │  - PC Pipeline (default)            │    │
│  │  - PS2 Pipeline (emulated)          │    │
│  │  - Xbox Pipeline (emulated)         │    │
│  │  - GTA IV Pipeline (new)            │    │
│  │  - Normal Map Pipeline (new)        │    │
│  │  - Mobile Pipeline                  │    │
│  │  - Neo Pipeline                     │    │
│  └──────────────┬──────────────────────┘    │
│                 │                           │
│  ┌──────────────▼──────────────────────┐    │
│  │  Post-FX Chain                      │    │
│  │  - Color filter (PS2/PC/Mobile)     │    │
│  │  - Radiosity                        │    │
│  │  - Infrared/Night vision            │    │
│  │  - SSAO                             │    │
│  │  - SMAA                             │    │
│  │  - GTA IV effects                   │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

### Shader Register Layout (All Pipelines)
| Register | Content |
|----------|---------|
| c0 | Combined world-view-projection matrix |
| c4 | Ambient color |
| c5-c11 | Direct light colors (7 lights) |
| c12-c18 | Direct light directions (7 lights) |
| c19 | Material color |
| c20 | Surface properties |

### Hook Map (GTA:SA v1.0 US)
| Address | Hook Target | Purpose |
|---------|-------------|---------|
| 0x5BCF14 | afterStreamIni | Post-RwEngine init |
| 0x7491C0 | myDefaultCallback | Default render callback |
| 0x5BF8EA | CPlantMgr_Initialise | Vegetation init |
| 0x756DFE | rxD3D9DefaultRenderCallback_Hook | D3D9 render |
| 0x5DADB7 | fixSeed | RNG fix |
| 0x4C88F0 | → 0x5DA610 | Vehicle pipe upgrade parts |
| 0x5DA610 | CustomPipeAtomicSetup | Vehicle atomic setup |
| 0x5D7F40 | IsCBPCPipelineAttached | Building pipe check |
| 0x5D5B80 | IsCCPCPipelineAttached | Car pipe check |

## Integration Points

### MTA64 ↔ SKYGFXPlus
- MTA64 provides the multiplayer framework and mod loading
- SKYGFXPlus provides the rendering enhancements
- Both inject into the same game process
- Must not hook conflicting addresses
- SKYGFXPlus reads INI config; MTA64 uses Lua/C# scripts

### Shared Game Memory
Both projects read/write to the same game memory:
- `RwEngineInstance` at `*(void**)0x58FFC0`
- `D3D9 device` at `*(IDirect3DDevice9**)0xC97C28`
- Camera, weather, timecycle globals
- Vehicle/ped pools

## Build Configuration
- **Target**: Win32 (x86) DLL, renamed to .asi
- **Dependencies**: RWSDK 3.6/3.7, DirectX 9 SDK (June 2010)
- **Output**: `bin/Release/skygfx.dll` → `skygfx.asi`
- **Install**: `E:\games\gtasa_skygfx_plus\`
