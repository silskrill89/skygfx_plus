# AGENTS.md — skygfx_plus_expIV

## Project

GTA San Andreas graphics mod (ASI plugin). Replaces building/vehicle rendering pipelines with PBR shaders. DLL loaded via ASI loader. Hooks into RW 3.6 D3D9 pipeline baked into the game exe.

## Agents (8-agent roster)

8 specialized agents for swarm-style orchestration. Definitions: `.mimocode/agents/*.md` (all 8 verified on disk).

| Agent | Tier | Purpose |
|-------|------|---------|
| shader-expert | ultra | HLSL/SM3.0, FXC compilation, register layout |
| pbr-specialist | ultra | PBR pipeline (building/vehicle/ped), BRDF, materials |
| postfx-specialist | standard | PostFX chain (tonemap, SMAA, SSAO, motion blur) |
| crash-debugger | standard | Diagnostic log analysis, crash zone detection |
| knowledge-manager | lite | Sadie/serena/harness-memory orchestration |
| build-deploy | lite | fast_build.py, shader compilation, deployment |
| researcher | standard | Web search, paper analysis, reference gathering |
| render-auditor | ultra | Cross-pipeline consistency audit, state leak detection |

Routing (verified: `tools/agent_coordinator.py`):

```bash
python tools/agent_coordinator.py route "<task>" <scope>   # scopes: shader, pipeline, postfx, audit, research, crash, build, full
python tools/agent_coordinator.py status                   # agent + memory-layer health
```

Swarm dispatch: `.mimocode/workflows/swarm-dispatch.js` → `workflow({ name: "swarm-dispatch", args: { task: "...", scope: "pipeline" } })`.

Routing scopes → agents: shader → shader-expert, build-deploy · pipeline → pbr-specialist, render-auditor · postfx → postfx-specialist, crash-debugger · audit → render-auditor, pbr-specialist, postfx-specialist · research → researcher, knowledge-manager · crash → crash-debugger, knowledge-manager · build → build-deploy, crash-debugger · full → shader-expert, pbr-specialist, postfx-specialist, build-deploy.

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
- **Build output**: `build\Release\skygfx.dll` (searched by `fast_build.py`; also `bin\Release\skygfx.dll`) → renamed to `skygfx.asi`
- **Build log**: `.cache/msbuild_last.log` (written by `fast_build.py`; `premake_log.txt` only exists after a premake run via `tools/doctor.py`)
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
- **Specialist Routing**: Project work routes through the 8-agent roster — `python tools/agent_coordinator.py route "<task>" <scope>` (see Agents section). Built-in task-tool types (@fixer, @explorer, @librarian, @oracle) still work for quick dispatch; cluster edits by subsystem per directive §0.3 (no single-file lane slicing).
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

## Knowledge System

Unified CLI for managing project knowledge, research, and documentation.

### Quick Commands

```bash
python tools/skytool.py help                    # Show all commands
python tools/skytool.py status                  # System health overview
python tools/skytool.py sync                    # Full sync: ingest + xref + graph
python tools/skytool.py ingest --force          # Sync wiki/research → Obsidian
python tools/skytool.py xref --query GGX        # Find GGX in all sources
python tools/skytool.py graph --query PBR       # Concept dependency tree
python tools/skytool.py gaps --query "SSR"      # Gap analysis
python tools/skytool.py mem                     # Compact reference (<500 tokens)
python tools/skytool.py dream                   # Dream consolidation context
python tools/skytool.py moon                    # Moon phase status
python tools/skytool.py yt --search "PBR"       # YouTube transcript search
python tools/skytool.py research <url>          # Ingest URL to research docs
```

### Components

1. **Ingestion Pipeline** — Syncs GTAMods wiki (130 articles), research docs, science pillars → Obsidian vault
2. **Cross-Reference System** — 43 concepts, 369 files, 1996 cross-links
3. **Knowledge Graph** — 44 concepts, 9 implementations, 16 concept chains
4. **Gap Detector** — 12 features tracked, 6 implemented, 50% coverage
5. **Memory System** — 15 logarithmic tiers + lunar phase dual-track

### Data Sources

| Source | Location | Count |
|--------|----------|-------|
| GTAMods Wiki | `H:/wikis/gtamods/rendering/` | 130 articles |
| Obsidian Vault (SkyGFX) | `E:/dev(dave)/SKYGFXPLUS_DOCS/` | 195 docs |
| Obsidian Vault (RE) | `E:/dev(dave)/GTA SA Reverse Engineering Documentation/` | 128 docs |
| Science Pillars | `memory-bank/.cache/science-pillars/` | 3 docs |
| Research | `docs/research/youtube/` | 6 transcripts |

### Memory Layers (8 total — per `.serena/memories/project.md`, paths verified on disk 2026-11-07)

| # | Layer | Location | Purpose |
|---|-------|----------|---------|
| 1 | Serena | `.serena/memories/*.md` | Curated knowledge notes + wave records |
| 2 | Sadie | `.sadie/sadie.db` | Queryable SQLite index of all sources |
| 3 | Harness-Memory | `.harness-memory/memory.sqlite` | Dream-consolidated memories |
| 4 | MiMoCode Checkpoint | `~/.local/share/mimocode/memory/sessions/*/checkpoint.md` | Session state (project-root `memory/` does not exist) |
| 5 | MiMoCode Memory | `~/.local/share/mimocode/memory/projects/*/MEMORY.md` | Project memory |
| 6 | Obsidian Vault | `E:/dev(dave)/SKYGFXPLUS_DOCS/` | SkyGFX documentation vault |
| 7 | Git History | `git log` | Commit messages as knowledge |
| 8 | Agent Outputs | `.serena/agent-reports/*.md` | Per-task findings (`tasks/*/progress.md` does not exist — reports live here) |

Working memory on top of these: `.opencode/memory-bank/*.md` (6 files — `activeContext.md` is top = newest). Standing directive §0.5: every lane/wave writes its summary to **memory-bank + serena**, never chat-only.

### Sadie Knowledge Librarian

```bash
python .sadie/sadie.py init                     # Build/rebuild database from all sources
python .sadie/sadie_vault_sync.py               # Sync BOTH Obsidian vaults into sadie.db
python .sadie/sadie.py search "<term>"          # Search items by title/content/tags
python .sadie/sadie.py query "<sql>"            # Run SQL query
python .sadie/sadie.py stats                    # Show database statistics
python .sadie/sadie.py locations                # List file locations
python .sadie/sadie.py gotchas                  # Show critical items
python tools/memory-startup.py                  # Bootstrap all 5 file-based layers
python tools/memory-startup.py --check          # Check freshness of all layers
```

### MCP Servers (Obsidian)

Two Obsidian vault MCP servers configured in `~/.config/mimocode/mimocode.jsonc`:
- `obsidian-re-vault` — GTA SA Reverse Engineering Documentation vault
- `obsidian-skygfx-docs` — SKYGFXPLUS_DOCS vault

Both use `obsidian-mcp-server.exe` via Obsidian Local REST API plugin.

## Standing directives 2026-11-02

User order (§0), source: `.serena/memories/standing-directives-2026-11-02.md`. These EXTEND, not replace, older standing orders (lanes never build; one integrated build by orchestrator; NEVER-REVERT wave records; stable-config acceptance).

1. **ASAP pacing** — work the remaining list (peds_pipe Phase 3 skin absorb, water M4-M9, gibs render fix, address migration, menu overhaul, RpAtomic layout resolution, RSMon metric bug) without idle gaps. No deadline active; goal line = stable-config baseline (`docs/baseline/2026-11-02-stable-config/README.md`).
2. **Unify** — prefer one shared implementation over per-call-site copies: one upload path, one noise system, registry-driven addresses. If the same constant/fix exists twice, it is wrong.
3. **File limit lifted** — multi-file clustered passes are allowed; stop slicing lanes to single-file edits. Cluster by subsystem instead. Never parallelize overlapping writes.
4. **Per-system .cpp targets** — end state is one file per system: a skin system .cpp owning ped/skin absorption from peds_pipe; ONE noise.cpp feeding wind + water + trees; physics engines connecting systems (softbody/ragdoll linking into tree/water/vehicle work).
5. **Write summaries to memory** — every lane/wave writes its summary into the memory layers (memory-bank + serena), never chat-only.

## Hard rules (2026-11-10)

Source: `.serena/memories/standing-directives-2026-11-10.md` (EXTENDS the 2026-11-02 directives; dated-addendum family is NEVER-REVERT).

- **Named variables, no raw address call sites.** Always use proper variable names. Game/SDK addresses live ONLY in the named declaration layer — `src/rw/gta.h` named externs/constants, precedent the `gta.h:391-527` layout contract. Call sites use the name; raw hex at a call site is a violation unless it is a disassembly-only constant documented inline as `// no symbol; verified <date>`.
- **Address-hygiene sweep** (targets: `.serena/memories/wave-20-record.md` §6): `main.cpp:1677` `0xC9BCC0` first, then `rw_fixes.cpp` `0xB4E9E0`, begun-camera sites (`postfx_smaa.cpp:237-241` / `postfx.cpp:3197` / `pipelinecommon.cpp:1041`), ped-pool `0xB74490`/`0x7C4`/`0x800`, `0x804EF0`.
- **Workflow**: deploy agents as needed to keep moving; update all documentation layers every wave (Obsidian priority → Serena → Sadie); keep fixing until the game matches the September references (`C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-09`, ~Sep-23 07:09 batch) with zero on-screen glitches.
