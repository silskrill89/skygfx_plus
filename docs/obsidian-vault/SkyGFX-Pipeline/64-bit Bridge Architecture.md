# SkyGFX Plus — 64-bit Bridge Architecture

## Concept: Signal Splitter / Bigger Bus

Instead of hooking limited functions directly, SkyGFX creates a second library
that opens as a separate "app" with its own memory pool. The two libraries
communicate via a bridge, passing limited game functions through to the bigger bus.

### Architecture

```
┌─────────────────────────────────────────────┐
│  GTA SA (32-bit process, ~2GB address space) │
│                                              │
│  ┌──────────────┐    ┌──────────────────────┐│
│  │  SkyGFX ASI  │◄──►│  Bridge DLL (64-bit) ││
│  │  (rendering)  │    │  (script/AI/assets)   ││
│  │  32-bit       │    │  4GB dedicated pool   ││
│  └──────────────┘    └──────────────────────┘│
│         │                      │              │
│         ▼                      ▼              │
│  Game functions           Script execution    │
│  RW rendering             Asset loading       │
│  D3D9 calls               Physics/AI          │
│  Vehicle rendering        Water simulation    │
└─────────────────────────────────────────────┘
```

### How It Works

1. **SkyGFX ASI** loads normally, handles rendering hooks
2. **Bridge DLL** loads via `LoadLibrary` — gets its own code segment
3. **Shared memory** via `CreateFileMapping` or `VirtualAlloc` with `MEM_COMMIT`
4. **Function pointers** passed through the bridge as serialized addresses
5. **64-bit mode** via WoW64 — the bridge DLL runs in 64-bit context
6. **Dedicated 4GB** — bridge allocates its own memory pool for scripts/AI

### Implementation

```cpp
// In SkyGFX ASI (main.dll)
HANDLE bridge = LoadLibrary("skygfx_bridge.dll");
// Pass game function pointers through bridge
void (*BridgeExec)(void* func, void* args) = GetProcAddress(bridge, "BridgeExec");
BridgeExec(GameFunction, &args);

// In Bridge DLL (skygfx_bridge.dll)
// 64-bit address space, dedicated memory pool
static char scriptMemory[4 * 1024 * 1024 * 1024]; // 4GB
void BridgeExec(void* gameFunc, void* args) {
    // Execute game function in 64-bit context
    // Returns results through shared memory
}
```

### Benefits
- Scripts get dedicated 4GB RAM (no memory pressure from rendering)
- Asset loading/unloading isolated from main game loop
- Physics/AI can run heavy computations without frame drops
- Water simulation runs independently

### Challenges
- WoW64 bridge overhead (context switching)
- Synchronization between 32-bit and 64-bit contexts
- Memory layout alignment (pointers between address spaces)

### TODO
- [ ] Create bridge DLL skeleton
- [ ] Implement shared memory allocation
- [ ] Hook game functions to route through bridge
- [ ] Benchmark WoW64 context switch overhead
- [ ] Test 4GB allocation for scripts
