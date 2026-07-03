# SkyGFX Plus — Decided Architecture

## Quiz Answers (Final)

| # | Topic | Decision |
|---|-------|----------|
| Q1 | Build & Deploy | **A** — Always deploy after every build |
| Q2 | Shaders | **B** — Auto-compile via fix_build.py |
| Q3 | Timecycle | **B** — Use PC timecyc values (gamma-corrected) |
| Q4 | Sky | **B** — Native SA sky system + timecycle blending |
| Q5 | PBR | **A** — Full GGX/Smith/Schlick + baked vertex as AO |
| Q6+7 | Buildings/Vehicles | **Unified** — One PBR group for buildings, vegetation, vehicles. Keep PS2/Xbox as separate legacy pipes |
| Q8 | Water | **Both** — Procedural Gerstner + screen-space reflections (will explain later) |
| Q9 | Weather | **A** — Hash-based from multiple timecycs |
| Q10 | Wheels | **A** — Shared wheel DFF pool + hash-based class selection |
| Q11 | INI | **A** — Fresh INI every build, all features ON |
| Q12 | Debug | **B** — CLEO-based + Lua option |
| Q13 | Memory | **A** — 64-bit bridge for scripts/AI (4GB dedicated) |
| Q14 | Textures | **B** — Support all D3D9 formats |
| Q15 | Normal Map | **A** — POC + silhouette + regular normal mapping combo |
| Q16 | PostFX | **A** — SMAA → SSAO → SSS → Grading → Tonemapping |
| Q17 | Audio | **A** — Keep original SA audio (for now) |
| Q18 | Streaming | **B** — Custom streaming with memory pool optimization |
| Q19 | CLEO/Scripting | **A** — Keep CLEO + add Lua scripting |
| Q20 | Release | **B** — Modular ASIs + monolith option |

## Key Architecture Decisions

### Unified PBR Group (Q6+7)
One unified PBR shader for:
- Vehicles
- Buildings
- Vegetation
- All share BRDF table from brdfLibrary.h

Keep PS2/Xbox/Neo as separate legacy pipes.

### Water = Both (Q8)
- Procedural Gerstner waves for surface displacement
- Screen-space reflections via depth buffer ray marching
- Both active simultaneously

### Weather = Hash-based multi-timecyc (Q9)
- Multiple timecyc files
- Hash-based selection per weather type + time slot
- Blend between sets over time

### Wheels = Shared pool + hash (Q10)
- Combined wheels.dff (SA + VC + custom)
- Hash-based class selection (sport, muscle, etc.)
- TransFender grid UI

### 64-bit Bridge (Q13)
- SkyGFX ASI (32-bit, rendering)
- Bridge DLL (64-bit, scripts/AI/assets)
- Shared memory communication
- 4GB dedicated pool for scripts

### Modular + Monolith (Q20)
- Separate ASIs per feature
- Single monolith installer option
