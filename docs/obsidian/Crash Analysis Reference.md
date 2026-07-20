# Crash Analysis & Debugging Reference

> Complete reference for crash analysis, debugging techniques, and known crash patterns in skygfx_plus.
> Connects RW SDK knowledge to actual game binary addresses and crash scenarios.
>
> **Note**: GTA SA uses RW 3.6.0.3. skygfx_plus uses RW 3.7 SDK headers and backports 3.7 features.

## Crash Handler Architecture

### VEH (Vectored Exception Handler)

skygfx_plus installs a VEH handler at DllMain time (`src/core/diagnostics.cpp`):

```
DllMain
  → diag_init()          — Initialize logging
  → diag_installVEH()    — Install VEH handler
  → diag_startWatchdog() — Start freeze detection thread
```

**VEH Handler** (`diag_crashHandler`):
1. Logs crash details (code, address, registers, stack)
2. Writes minidump (`skygfx_dbg.dmp`)
3. Shows MessageBox with crash info (forced to front)
4. Click OK to continue, Cancel to terminate

### Freeze Watchdog

A background thread monitors heartbeat counter:
- `diag_heartbeat()` called from `RenderScene_hook` and `InitialiseGame_hook`
- If no heartbeat for 30 seconds → warning MessageBox
- ESC key polls `GetAsyncKeyState(VK_ESCAPE)` for force-kill
- Previous crash marker file (`.crash`) shown on next launch

## Reading Crash Logs

### Log Format
```
[YYYY-MM-DD HH:MM:SS.mmm] Message
```

### Crash Entry Format
```
CRASH: code=0xC0000005 at=0x007F39FB EAX=00000008 EBX=0000000D ECX=00000000 EDX=047A4178
       ESI=00000000 EDI=00000000 EBP=0177F448 ESP=0177F31C  ExceptionInfo: READ addr=0x00000008
```

| Field | Meaning |
|-------|---------|
| `code` | Exception code (0xC0000005 = ACCESS_VIOLATION) |
| `at` | Instruction address that crashed |
| `EAX-EDI` | CPU registers at crash time |
| `ExceptionInfo` | READ/WRITE/EXECUTE + faulting address |

### Stack Walk
```
Stack trace:
    [0] EBP=0177F448 RET=00000000 (module=00000000)
    [1] EBP=0177F624 RET=6DA36D7F (module=6DA30000)
```

- `RET` = return address (saved on stack)
- `module` = module base address
- Frame chain can be unreliable due to FPO (Frame Pointer Omission)

### Stack Dump
```
Stack dump (ESP):
    [ESP+0x00] = 00000000
    [ESP+0x04] = 005D5B3A
```

Raw stack values — useful for identifying data vs code addresses.

## Known Crash Patterns

### Crash 0x7F39FB — NULL Frame in Pool Iteration

**Symptom**: `0xC0000005` at `0x007F39FB`, READ at `0x00000008`

**Root cause**: Function at `0x7F39F0` (`RwTexDictionaryFindNamedTexture` or similar) reads `frame+8` (inDirtyListLink). Called with NULL frame → reads from address `0x8`.

**Caller**: `0x5A6111` in gta_sa.exe iterates pool at `0xC8800C` (CTxdPool*), 12-byte elements, first DWORD = frame pointer. One entry has NULL frame.

**Pool structure** (from `game_sa/TxdStore.h:45`):
```cpp
static inline auto& ms_pTxdPool = StaticRef<CTxdPool*>(0xC8800C);
// CPool<TxdDef>: [items ptr, flags ptr, size, freeIndex, ownsArrays, dealWithNoMemory]
// Pool elements: 12 bytes, first DWORD = frame pointer
```

**Binary disassembly** (RUNTIME, not on-disk — game code is obfuscated):
```asm
; 0x7F39F0 — the crashing function
mov eax, [esp+4]      ; eax = frame arg
push ebx
add eax, 8            ; frame+8 = inDirtyListLink
push ebp
push esi
push edi
mov ebx, [eax]        ; CRASH when frame=NULL → reads from 0x8

; 0x5A6111 — caller
mov esi, [eax]         ; esi = pool element (NULL frame)
mov eax, [0x8D0A5C]    ; load RW SDK function ptr
push eax               ; push sync function
push esi               ; push frame (NULL)
call 0x7F39F0          ; CRASH
```

**Key addresses**:
- `0xC8800C` = CTxdPool* (TXD store pool)
- `0x8D0A5C`, `0x8D0A60`, `0x8D0A64` = RW SDK sync function pointers

### Crash 0x5A5735 — CopyTexture NULL Dereference

**Symptom**: `0xC0000005` at `0x005A5735`, READ at `0x3D4CCCCD` (float 0.1 used as pointer)

**Root cause**: Function at `0x5A5730` (`CopyTexture`) dereferences texture parameter without NULL check.

**Binary**:
```asm
; 0x5A5730 — CopyTexture
mov eax, [esp+4]      ; eax = tex arg
push ebx
mov ebx, [eax]        ; CRASH: reads from 0x3D4CCCCD
movzx eax, byte [ebx+0x23]
```

**MTA reference**: MTA source has identical crash — `HOOK_CClothesBuilder_CopyTexture` at `0x5A5730`. MTA comment: "SA CopyTexture at 0x5A5730 derefs the texture parameter without a null check."

### 0x7F39F0 Identity — CRITICAL

`normalmap_plugin.cpp:771` defines:
```cpp
#define RwTexDictionaryFindNamedTexture(dict, name) \
    ((RwTexture *(__cdecl *)(RwTexDictionary *, char *))0x7F39F0)(dict, name)
```

So `0x7F39F0` may be `RwTexDictionaryFindNamedTexture`, NOT `RwFrameSyncObject`.
**WARNING**: Hooking this address can corrupt texture dictionary lookups.

## Game Binary Obfuscation

**CRITICAL**: The gta_sa.exe binary at `E:\games\gtasa_skygfx_plus\gta_sa.exe` has **obfuscated/encrypted code**. On-disk bytes at runtime-decrypted addresses are junk:

```asm
; On-disk at 0x5A5730:
sub esp, 4
mov [esp], 0xcdae71
add [esp], 0x895c3
jmp 0x5a573d

; Runtime (decrypted) at 0x5A5730:
mov eax, [esp+4]      ; CopyTexture
push ebx
mov ebx, [eax]
```

**Consequence**: `InjectHook()` in DllMain writes JMP to on-disk (junk) code, but the game's code decryption **overwrites our JMP** during CGame::Initialise. The hook never fires.

**Workaround**: Only hook addresses in the RW SDK section (`.text` at VA 0x1000, not in the obfuscated area). Or use VEH handler instead.

## RW SDK Memory Layout (gta_sa.exe)

| Section | VA Range | Size | Content |
|---------|----------|------|---------|
| `.text` | 0x00001000 - 0x00457000 | 4.3MB | Main game code + RW SDK |
| `_rwcseg` | 0x00457000 - 0x00458000 | 4KB | RW code segment |
| `.rdata` | 0x00458000 - 0x004A4000 | 304KB | Read-only data |
| `.data` | 0x004A4000 - 0x0089E000 | 4MB | Global variables |
| `_TEXT_HA` | 0x0089E000 - 0x008AF000 | 64KB | HAnim text |
| `_rwdseg` | 0x008AF000 - 0x008B0000 | 4KB | RW data segment |
| `.text2` | 0x008B1000 - 0x00EFB000 | 6.4MB | Additional code (RW SDK embedded) |

**Important**: RW SDK sync function pointers (`0x8D0A5C/60/64`) are in `.text2` (second code section).

## Diagnostic Logging

### Heartbeat System

```cpp
// Called from RenderScene_hook (every frame) and InitialiseGame_hook
void diag_heartbeat() {
    InterlockedExchangeAdd(&s_heartbeat, 1);
}
```

Watchdog thread checks every 2 seconds:
- If no heartbeat for 30s → "FREEZE DETECTED" warning
- ESC key → force terminate

### Crash Marker Files

On crash: `diag_writeCrashMarker("CRASH (exception)", code, address)` writes a `.crash` file.
On next launch: `diag_checkCrashMarker()` shows previous crash info dialog.

## Common Debugging Patterns

### 1. Identify Module

```cpp
HMODULE exeMod = GetModuleHandle(NULL);  // gta_sa.exe base
HMODULE dllMod = GetModuleHandle("skygfx.asi");  // skygfx base
// DLL base changes with ASLR — use GetModuleHandle at runtime
```

### 2. Calculate Offset

```
actual_addr - module_base + preferred_va = offset_in_binary
```

For skygfx DLL: preferred VA = 0x10000000 (from map file)
For gta_sa.exe: base = 0x00400000 (fixed, no ASLR)

### 3. Resolve DLL Addresses

From `skygfx.map` (preferred load 0x10000000):
- `readIni` at 0x10006CB0 (40KB function due to STL inlining)
- `normalmap_init` at 0x1015F60
- `IsCCPCPipelineAttached_hook` at 0x1015EC0

### 4. Verify Hook Installation

Check log for:
```
hookBuildingPipe: OK
RwFrameSyncObject NULL-guard OK
normalmap: RpNormMapPluginAttach FAILED  (if already attached)
```

## SilentPatch Interaction

SilentPatch is a mandatory dependency — skygfx runs on top of it. Key interactions:

### Hook Load Order
1. `gta_sa.exe` loads → ASI loader fires DLL_PROCESS_ATTACH for all plugins
2. **SilentPatch loads FIRST** (alphabetical by ASI loader convention): hooks `0x74872D` (IsAlreadyRunning), applies immediate patches
3. **skygfx loads SECOND**: hooks addresses directly from DllMain, does NOT hook `0x74872D`
4. Game reaches `IsAlreadyRunning` → SilentPatch's `InjectDelayedPatches_10` fires → applies deferred patches
5. Game reaches `CGame::Initialise` → skygfx's `InitialiseGame_hook` fires → normalmap_init, render hooks

### VEH Handler Interaction
- SilentPatch does NOT install a VEH handler
- skygfx's VEH handler (`src/core/diagnostics.cpp`) handles all exceptions
- skygfx's VEH only shows blocking MessageBox for crashes INSIDE skygfx DLL module; game crashes are logged and allowed to propagate

### Known Crash 0x04CD4769 — Early Startup Crash
**Symptom**: `0xC0000005` at `0x04CD4769`, WRITE to `0x00000000`
**Module**: `0x04CC0000` (game's obfuscated/decrypted code section)
**When**: IMMEDIATELY after DllMain completes, before InitialiseGame_hook fires
**Stack**: Goes through `0x6EE80000` (SilentPatch) 3 frames, then calls game code that hits NULL write

**Analysis**: This is a game startup crash that occurs during early initialization. The stack shows it passes through SilentPatch code before reaching the NULL write. This is NOT a skygfx-vs-SilentPatch conflict — it's a game-level issue exposed by the plugin load order.

## See Also

- [[RenderWare SDK Architecture]]
- [[Debugging Guide]]
- [[Crash Analysis - 0x4C9FB5 and 0x40890A]]
