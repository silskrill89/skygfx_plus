# Crash Analysis: 0x4C9FB5 and 0x40890A

> **Related**: This crash is analyzed from the register/call-stack perspective here, and from the symbol-conflict root-cause perspective in [[Crash Analysis - normmap_stubs symbol conflicts]].

## Summary
Two sequential crashes during `CGame::Initialise` when skygfx.asi is loaded. The first crash is caught by skygfx's SEH handler, but the second crash (at 0x40890A) is fatal and kills the process.

## Crash 1: 0x4C9FB5 (Caught by SEH)

### Registers
```
EAX=0x00000034  EBX=0x13DAF3C4  ECX=0x64616F72  EDX=0x0177F688
ESI=0x13DAF390  EDI=0x0177F760  EBP=0x64616F72  ESP=0x0177F61C
```

### Analysis
- **ECX = EBP = 0x64616F72** = ASCII "road" — a corrupted pointer being treated as a struct base
- **vehicleStoreCount = 0** — no vehicles loaded yet, crash during CGame::Initialise vehicle model loading
- **EAX = 0x34 (52)** — likely a struct size or field offset
- The address 0x4C9FB5 is in the vehicle model loading/processing code
- The "road" string in registers suggests a model name pointer was corrupted into a struct base pointer

### Root Cause Chain
1. skygfx hooks `0x5BCF14` → `afterStreamIni` (post-RwEngine init)
2. skygfx hooks `0x7491C0` → `myDefaultCallback` (render callback)
3. skygfx hooks `0x5BF8EA` → `CPlantMgr_Initialise` (vegetation)
4. skygfx hooks `0x756DFE` → `rxD3D9DefaultRenderCallback_Hook` (D3D9 render)
5. skygfx hooks `0x5DADB7` → `fixSeed` (RNG)
6. **One or more of these hooks corrupts state** that manifests during vehicle loading

### Config Struct Shift (Initially Suspected, Later Ruled Out)
The Normal Mapping fields were inserted between `ivExposure` and the weather section in the Config struct, shifting all subsequent fields by 16 bytes. This was fixed by moving them to the end of the struct. However, the crash persisted after this fix, indicating the Config struct shift was NOT the primary cause.

## Crash 2: 0x40890A (Fatal)

### Registers
```
EAX=0x00000100  EBX=0x00000002  ECX=0x00000001  EDX=0x00000502
ESI=0x008E62DC  EDI=0x0000161C  EBP=0x00000000  ESP=0x0177F714
```

### Analysis
- **Reading from 0x000000000000000A** = null pointer + offset 0xA
- **EBP = 0** — base pointer is null, suggesting a failed struct lookup
- **ESI = 0x008E62DC** — in .data section, possibly a global config/handling data
- This crash happens AFTER the first crash is caught by SEH
- The game tries to continue but encounters corrupted state from the first crash

## What Was Tried

### Attempt 1: Disable 0x4C88F0→0x5DA610 hook
- **Result**: Same crash — this hook was NOT the cause

### Attempt 2: Move Normal Mapping fields to END of Config struct
- **Result**: Same crash — Config struct shift was NOT the primary cause

### Attempt 3: Restore backup_original source entirely
- **Result**: Pending testing — this is the most complete revert

## Key Findings

### 1. The backup_original Also Has Issues
The backup_original source references symbols that don't exist without normmap_stubs.cpp:
- `RpNormMapPluginAttach` — not in game exe, was provided by normmap_stubs.cpp
- `RpNormMapAtomicIsInitialized` — not in game exe
- `RpNormMapMaterialGetNormMapTexture` — not in game exe
- `myPluginAttach` — undefined anywhere
- `DrawUnifiedDebugMenu` — in debugmenu_ui.cpp (commented out of vcxproj)

### 2. normmap_stubs.cpp Was the Original Problem
The original normmap_stubs.cpp exported `extern "C"` functions with the same names as the game's internal RW plugin functions. Windows DLL loader treats exports as process-wide, so SilentPatch (which hooks the same addresses) resolved to our stubs instead of the game's code, corrupting the stack.

### 3. The Crash May Be Pre-Existing
If the backup_original source ALSO crashes (which is possible given the undefined symbols), the crash may have been present before any of our changes. The original aap's skygfx may need specific build configurations that the backup_original doesn't match.

## Current Approach
Restored backup_original source files (skygfx.h, main.cpp, gta.cpp, postfx.cpp) with minimal compile fixes. This should match aap's skygfx as closely as possible.

## Lessons Learned

### For GTA SA Graphics Modding
1. **Never add `extern "C"` exports to an ASI/DLL that share names with host game functions** — Windows DLL loader treats all exports as process-wide symbols
2. **Config structs with fixed memory offsets are fragile** — any field insertion shifts all subsequent fields, causing silent memory corruption
3. **RW plugin functions (RpNormMap*) are in separate DLLs** — they're not in gta_sa.exe, can't be called directly without finding their runtime addresses
4. **SilentPatch hooks many of the same addresses** — any hooking must account for SP's hooks to avoid conflicts
5. **The SEH crash handler allows partial game state** — but corrupted state from crash 1 causes crash 2 (fatal)

### For Future Normal Map Integration
1. Find rpnormmap function addresses at runtime (Cheat Engine / IDA on running process)
2. Use MinHook for proper trampolines (instruction-length-aware)
3. Never hook 0x5DA610 without a proper trampoline — the backup's EAXJMP(0x5DA610) creates infinite loop when hooked
4. Normal map fields in Config struct must be at the END to avoid shifting existing fields
