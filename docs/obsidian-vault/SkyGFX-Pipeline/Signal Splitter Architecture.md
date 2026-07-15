# Signal Splitter Architecture — "Bigger Bus" for Heavy Operations

## Status: Concept / Partially Implemented

- ✅ `skygfx_bridge.cpp` — shared memory ring buffer with command protocol
- ❌ 64-bit Bus.dll not implemented
- ❌ Script execution routing not implemented
- ❌ Physics/AI isolation not implemented

## Concept
SkyGFX loads a second DLL that acts as a "bigger bus" — a dedicated memory pool for heavy operations (scripts, asset loading, physics). The two libraries communicate via a shared memory command ring buffer.

## Architecture
```
┌─────────────────────────────────────────────────────┐
│                   GTA SA (32-bit)                    │
│                                                     │
│  ┌──────────────┐    Bridge    ┌──────────────┐    │
│  │  SkyGFX.dll  │◄────────────►│  Bus.dll     │    │
│  │  (main ASI)  │  shared mem  │  (future)    │    │
│  │  ~2GB pool   │  ring buffer │  ~4GB pool   │    │
│  │  rendering   │              │  scripts     │    │
│  │  hooks       │              │  physics     │    │
│  │  menu        │              │  asset load  │    │
│  └──────────────┘              └──────────────┘    │
│                                                     │
│  Game.exe (32-bit, ~2GB address space)              │
└─────────────────────────────────────────────────────┘
```

## Implemented: skygfx_bridge.cpp

The bridge DLL provides a shared memory command ring buffer:

### Ring Buffer Structure
```cpp
struct BridgeCommand {
    uint32_t id;
    uint32_t type;
    uint64_t param1;
    uint64_t param2;
    uint64_t result;
};

struct SharedMemory {
    volatile uint32_t readIndex;
    volatile uint32_t writeIndex;
    BridgeCommand commands[256];
};
```

### API
| Function | Purpose |
|----------|---------|
| `Bridge_Init()` | Create shared memory via `CreateFileMappingA` |
| `Bridge_Shutdown()` | Release shared memory |
| `Bridge_SendCommand(type, p1, p2, *result)` | Write command to ring buffer |
| `Bridge_ReceiveCommand(*outCmd)` | Read command from ring buffer |
| `Bridge_GetPendingCount()` | Get number of pending commands |

### Shared Memory Name
`"SkyGFXBridge_SharedMemory"`

## Bridge Protocol
1. SkyGFX writes request to shared memory ring buffer
2. Bus.dll reads request, processes, writes response
3. SkyGFX reads response
4. Heavy operations (script execution, large asset loading) go to Bus.dll
5. Rendering stays in SkyGFX.dll (fast path)

## Benefits (Planned)
- Scripts get dedicated 4GB pool (no more script overflow)
- Heavy asset loading doesn't compete with rendering
- Physics/AI processing isolated from main loop
- Memory pressure reduced on game.exe

## Challenges
- WoW64 bridge overhead (context switching) — if running 64-bit Bus.dll
- Synchronization between 32-bit and 64-bit contexts
- Memory layout alignment (pointers between address spaces)

## See Also
- [[64-bit Bridge Architecture]] — 64-bit bridge concept
- [[Decided Architecture]] — Q13 decision
