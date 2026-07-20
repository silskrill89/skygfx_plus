# SkyGFX Plus — 64-bit Bridge Architecture

## Status: Concept / Partially Implemented

- ✅ `skygfx_bridge.cpp` — shared memory ring buffer (32-bit only)
- ❌ 64-bit Bridge DLL not implemented
- ❌ WoW64 context switching not implemented
- ❌ 4GB dedicated pool not implemented

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

### How It Would Work (Planned)

1. **SkyGFX ASI** loads normally, handles rendering hooks
2. **Bridge DLL** loads via `LoadLibrary` — gets its own code segment
3. **Shared memory** via `CreateFileMapping` or `VirtualAlloc` with `MEM_COMMIT`
4. **Function pointers** passed through the bridge as serialized addresses
5. **64-bit mode** via WoW64 — the bridge DLL runs in 64-bit context
6. **Dedicated 4GB** — bridge allocates its own memory pool for scripts/AI

### What's Actually Implemented

`skygfx_bridge.cpp` provides a 32-bit shared memory ring buffer:
- `Bridge_Init()` — creates shared memory via `CreateFileMappingA`
- `Bridge_SendCommand()` — writes commands to ring buffer
- `Bridge_ReceiveCommand()` — reads commands from ring buffer
- 256-entry command ring buffer
- Mutex synchronization

This is a foundation for inter-process communication, but does not implement:
- 64-bit context switching
- Script execution routing
- Physics/AI isolation
- 4GB dedicated memory pool

### Benefits (Planned)
- Scripts get dedicated 4GB RAM (no memory pressure from rendering)
- Asset loading/unloading isolated from main game loop
- Physics/AI can run heavy computations without frame drops
- Water simulation runs independently

### Challenges
- WoW64 bridge overhead (context switching)
- Synchronization between 32-bit and 64-bit contexts
- Memory layout alignment (pointers between address spaces)

## Related
- [[Signal Splitter Architecture]] — bridge concept
- [[Decided Architecture]] — Q13 decision
