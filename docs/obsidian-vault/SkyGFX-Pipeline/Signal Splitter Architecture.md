# Signal Splitter Architecture — "Bigger Bus" for Heavy Operations

## Concept
SkyGFX loads a second DLL that acts as a "bigger bus" — a dedicated memory pool for heavy operations (scripts, asset loading, physics). The two libraries communicate via a "fake 64-bit bridge" (shared memory + function pointers).

## Architecture
```
┌─────────────────────────────────────────────────────┐
│                   GTA SA (32-bit)                    │
│                                                     │
│  ┌──────────────┐    Bridge    ┌──────────────┐    │
│  │  SkyGFX.dll  │◄────────────►│  Bus.dll     │    │
│  │  (main ASI)  │  shared mem  │  (64-bit)    │    │
│  │  ~2GB pool   │  + RPC       │  ~4GB pool   │    │
│  │  rendering   │              │  scripts     │    │
│  │  hooks       │              │  physics     │    │
│  │  menu        │              │  asset load  │    │
│  └──────────────┘              └──────────────┘    │
│                                                     │
│  Game.exe (32-bit, ~2GB address space)              │
└─────────────────────────────────────────────────────┘
```

## Bridge Protocol
1. SkyGFX writes request to shared memory
2. Bus.dll reads request, processes, writes response
3. SkyGFX reads response
4. Heavy operations (script execution, large asset loading) go to Bus.dll
5. Rendering stays in SkyGFX.dll (fast path)

## Memory Layout
- SkyGFX.dll: 0x10000000 - 0x8FFFFFFF (~2GB)
- Bus.dll: 0x90000000 - 0xFFFFFFFF (~1.75GB)
- Shared memory: 0xF0000000 - 0xFFFFFFFF (64MB ring buffer)

## Benefits
- Scripts get dedicated 4GB pool (no more script overflow)
- Heavy asset loading doesn't compete with rendering
- Physics/AI processing isolated from main loop
- Memory pressure reduced on game.exe

## Implementation
1. SkyGFX loads Bus.dll via LoadLibrary
2. Bus.dll exports: `Bus_Init()`, `Bus_ExecuteScript()`, `Bus_LoadAsset()`
3. Communication: shared memory ring buffer + InterlockedExchange
4. Signal splitter: routes requests based on type (render vs compute)
