# Documentation Reconciliation Plan — 2026-10-25

**Status:** proposal only. This is the only file written by this pass. No source, shader, INI, doc, or build file was edited, moved, or deleted.

**Directive served:** m0510 (reconcile every doc; compress past/fixed data; document every step of the software so SkyGFX Plus can switch cleanly between PS2 / Xbox / fixed PC / GTA IV / GTA V / Modern) and m0511 (finish the implementation list, remove what is deprecated, keep the code and docs clean).

**Authority rule:** source wins over docs. Every "correct value" below was checked against the current working tree (HEAD `b7396c4`, with pre-existing modifications present before this pass; `git status` shows no changes from this pass outside this report). Docs are evidence of intent, not authority.

**Survey method:** all 199 files were `stat`'ed and opened; every file's heading outline was read. Docs used for contradiction checks were read in full or in targeted sections. The inventory is exhaustive; the contradiction table is not a claim that unlisted prose is correct.

## Executive answer

- **There is no single documentation set today; there are three overlapping ones** (aap-era pipeline prose, the Obsidian wiki mirrors, and the 2026-10 plan/wave records). 199 files / 2.17 MB describe at least three different products.
- **The mode system is the least documented and most important subsystem.** `[Modes] active=` and `ModeManager` work, but no doc states per-mode owned vs inherited Config fields, precedence, toggle/A→B behavior, or the GTA V water-only limitation.
- **Six concrete mode hazards are undocumented:** GTA V changes only 2 fields; GTA IV inherits motion blur/SSS/normal map/Forward+ from the base; Fixed PC inherits normal maps; no profile owns the water knobs except style/quality; `pipelineOverride` bypasses mode ownership; `qualityPreset` still writes pipes for tiers 0-2.
- **The most dangerous stale facts are shader-model and register claims:** the old register tables are wrong, and the docs say ps_2_0 while the build is SM3.0-only (29 SM3.0 rows, 0 SM2.0 rows).
- **Proposal:** 20 canonical docs (mode guide + feature matrix + architecture + 10 subsystem refs + build/debug/INI/shader refs + timeline + roadmap), 67 pure archives, 126 absorbed-then-archived sources, 4 deletions, 2 in-place rewrites. Active surface drops from 2.17 MB to ~300-380 KB (83-87%).

---

## Scale (measured, not estimated)

| Location | Files | Bytes | KB | Share |
|---|---:|---:|---:|---:|
| `docs/**/*.md` | 152 | 1,896,300 | 1,851.9 | 83.3% |
| root `*.md` | 16 | 130,156 | 127.1 | 5.7% |
| `.serena/memories/*.md` | 25 | 205,055 | 200.3 | 9.0% |
| `.opencode/memory-bank/*.md` | 6 | 43,603 | 42.6 | 1.9% |
| **Total** | **199** | **2,275,114** | **2,221.8** | **100%** |

Integrity checks:

- **0 zero-byte files.** Smallest is `docs/obsidian/Welcome.md` at 46 B (a redirect stub).
- **0 unreadable files.**
- **No byte-for-byte duplicates inside `docs/`.** MD5 over all 152 files produced no duplicate pair. An apparent duplicate in the first pass was the same path reached twice through overlapping walk roots, not two files.
- Near-duplicates do exist by topic (`RW SDK Reference.md` vs `RW_SDK_REFERENCE.md`; `VehiclePBR Modern.md` vs `obsidian-vault/03-Shaders/VehiclePBR Modern.md`; `Build System.md` in three places); they are not identical and are handled as MERGE-INTO, not DELETE.

---

# Part 1 — Inventory of all 199 markdown files

**Legend**

- **CANONICAL** — authoritative for its topic; stays at this path. Only two existing files qualify: `README.md` (one of the 20 target docs, rewritten in place) and `AGENTS.md` (an additional agent-operations doc outside the 20). The other 19 target docs are new and therefore not in this inventory.
- **MERGE-INTO** — content is absorbed into the named canonical doc, then the source is archived. Specific carried sections are listed in §3.2 and the archive dependency order in §4.4.
- **ARCHIVE** — provenance only (past-fixed, executed, superseded, research, or reference material outside the target set); move to the path shown under `docs/archive/`.
- **DELETE** — zero unique product content; justification in §4.2. Only 4 files earn this.
- **SPLIT** — one file covers several topics; each target receives the named sections.

**Target paths used below**

| Short key | Canonical path |
|---|---|
| README | `README.md` |
| MODE-GUIDE | `docs/modes/MODE-GUIDE.md` |
| FEATURE-MATRIX | `docs/modes/FEATURE-MATRIX.md` |
| ARCHITECTURE | `docs/architecture/ARCHITECTURE.md` |
| BUILDING-PIPE | `docs/reference/BUILDING-PIPE.md` |
| VEHICLE-PIPE | `docs/reference/VEHICLE-PIPE.md` |
| WATER | `docs/reference/WATER.md` |
| POSTFX | `docs/reference/POSTFX.md` |
| LIGHTING | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| ALPHA-TEXALPHA | `docs/reference/ALPHA-TEXALPHA.md` |
| CORONA-LIGHTS | `docs/reference/CORONA-LIGHTS.md` |
| SHADOWS | `docs/reference/SHADOWS.md` |
| NOISE | `docs/reference/NOISE.md` |
| IMGUI-MENU | `docs/reference/IMGUI-MENU.md` |
| BUILD-DEPLOY | `docs/guides/BUILD-DEPLOY.md` |
| DEBUGGING | `docs/guides/DEBUGGING.md` |
| INI-REFERENCE | `docs/reference/INI-REFERENCE.md` |
| SHADERS-RESOURCES | `docs/reference/SHADERS-RESOURCES.md` |
| TIMELINE | `docs/history/TIMELINE.md` |
| ROADMAP | `docs/ROADMAP.md` |

**Counts:** 67 ARCHIVE · 111 MERGE-INTO · 15 SPLIT · 2 CANONICAL · 4 DELETE = 199.

| path | size (KB) | last modified | topic | verdict | target |
|---|---:|---|---|---|---|
| `docs/00 - Index.md` | 3.1 | 2026-07-30 | MOC / docs index | MERGE-INTO | `README.md` |
| `docs/BRDF Reference.md` | 7.1 | 2026-07-29 | BRDF theory + values | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/Backwards Compatibility.md` | 10.5 | 2026-09-14 | INI key provenance | MERGE-INTO | `docs/reference/INI-REFERENCE.md` |
| `docs/Build System.md` | 1.3 | 2026-07-29 | build system (stale) | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/Building Pipeline.md` | 3.2 | 2026-07-29 | building pipe modes | MERGE-INTO | `docs/reference/BUILDING-PIPE.md` |
| `docs/Color Grading.md` | 1.1 | 2026-07-29 | color grading | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/Credits.md` | 1.3 | 2026-07-29 | credits | MERGE-INTO | `README.md` |
| `docs/DXVK Compatibility.md` | 4.9 | 2026-08-30 | DXVK install/compat | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/Debug Tools.md` | 1.4 | 2026-07-29 | debug tools | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `docs/FIXLOG_PBR_NullFrame_Deref.md` | 3.3 | 2026-07-29 | PBR NULL-frame fix log | ARCHIVE | `docs/archive/docs/FIXLOG_PBR_NullFrame_Deref.md` |
| `docs/Feature Hook Pattern.md` | 3.2 | 2026-07-29 | how to add a feature/hook | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/File Inventory.md` | 12.0 | 2026-09-14 | source inventory (stale) | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/Future Features.md` | 3.8 | 2026-09-14 | future phases | MERGE-INTO | `docs/ROADMAP.md` |
| `docs/GTA IV Mode.md` | 2.1 | 2026-07-29 | GTA IV mode (stale) | MERGE-INTO | `docs/modes/MODE-GUIDE.md` |
| `docs/Glass Shader.md` | 2.0 | 2026-07-29 | vehicle glass | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/INI Configuration.md` | 14.4 | 2026-09-14 | INI reference (stale) | MERGE-INTO | `docs/reference/INI-REFERENCE.md` |
| `docs/Implemented Features.md` | 4.4 | 2026-08-04 | implemented features | MERGE-INTO | `docs/modes/FEATURE-MATRIX.md` |
| `docs/Leeds Vehicle Pipes.md` | 1.2 | 2026-07-29 | LCS/VCS vehicle pipes | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/Lens Effects.md` | 0.9 | 2026-07-29 | lens effects | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/Neo Vehicle Pipe.md` | 1.4 | 2026-07-29 | Neo vehicle pipe | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/Night and Infrared Vision.md` | 1.1 | 2026-07-29 | night/infrared vision | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/PBR Common.md` | 2.8 | 2026-07-29 | PBR common HLSL functions | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `docs/PostFX Pipeline.md` | 6.1 | 2026-08-04 | PostFX chain | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/Project Lineage.md` | 7.0 | 2026-07-29 | project lineage | MERGE-INTO | `docs/history/TIMELINE.md` |
| `docs/Project Paths.md` | 2.2 | 2026-07-29 | paths | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/RW SDK Integration Strategy.md` | 12.6 | 2026-07-30 | RW SDK integration strategy | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/RW SDK Reference.md` | 2.4 | 2026-07-29 | RW SDK quick ref | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/RW_SDK_REFERENCE.md` | 10.2 | 2026-07-30 | RW 3.7 struct ref | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/Radiosity.md` | 1.8 | 2026-07-29 | radiosity | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/Roadmap to Ultimate Mod.md` | 2.9 | 2026-07-29 | roadmap phases | MERGE-INTO | `docs/ROADMAP.md` |
| `docs/SDK Dependencies.md` | 4.5 | 2026-07-30 | SDK dependencies | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/Shader Architecture.md` | 17.5 | 2026-09-14 | shader architecture + ID map | SPLIT | `docs/reference/SHADERS-RESOURCES.md` + `docs/guides/BUILD-DEPLOY.md` |
| `docs/SkyGFX_Living_Systems_Database.md` | 46.3 | 2026-07-29 | living systems / vehicle DB | SPLIT | `docs/reference/VEHICLE-PIPE.md` + `docs/ROADMAP.md` |
| `docs/Three-Codebase Comparison.md` | 30.9 | 2026-09-14 | 3-codebase comparison | SPLIT | `docs/architecture/ARCHITECTURE.md` + `docs/history/TIMELINE.md` |
| `docs/Unified Pipeline.md` | 1.9 | 2026-07-29 | unified/Forward+ concept | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/Vehicle Classification.md` | 6.4 | 2026-07-29 | vehicle classification | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/Vehicle Pipeline.md` | 4.3 | 2026-07-29 | vehicle pipe modes | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/VehiclePBR Modern.md` | 3.6 | 2026-08-01 | VehiclePBR shader entry points | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `docs/Water Rendering.md` | 1.9 | 2026-07-29 | water rendering | MERGE-INTO | `docs/reference/WATER.md` |
| `docs/Weather Timecycle.md` | 2.5 | 2026-07-29 | weather/timecycle | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/compress-recovery.md` | 3.3 | 2026-10-09 | compress tool recovery | ARCHIVE | `docs/archive/docs/compress-recovery.md` |
| `docs/helper_tex.md` | 7.5 | 2026-10-09 | helper_tex packaging | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/test-plan.md` | 12.2 | 2026-08-27 | systematic test plan | SPLIT | `docs/modes/MODE-GUIDE.md` + `docs/modes/FEATURE-MATRIX.md` |
| `docs/wheel_lod_system.md` | 2.7 | 2026-07-29 | wheel LOD (planned) | ARCHIVE | `docs/archive/docs/wheel_lod_system.md` |
| `docs/wheel_naming_convention.md` | 3.1 | 2026-07-29 | wheel naming (planned) | ARCHIVE | `docs/archive/docs/wheel_naming_convention.md` |
| `docs/wheel_texture_atlas.md` | 2.9 | 2026-07-29 | wheel atlas (planned) | ARCHIVE | `docs/archive/docs/wheel_texture_atlas.md` |
| `docs/baseline/2026-10-24-screenshots/README.md` | 6.1 | 2026-10-24 | baseline run README | ARCHIVE | `docs/archive/docs/baseline/2026-10-24-screenshots/README.md` |
| `docs/baseline/2026-10-24-screenshots/images.md` | 16.8 | 2026-10-24 | baseline image notes | ARCHIVE | `docs/archive/docs/baseline/2026-10-24-screenshots/images.md` |
| `docs/baseline/2026-10-24-screenshots/issues.md` | 21.9 | 2026-10-24 | baseline issue registry | ARCHIVE | `docs/archive/docs/baseline/2026-10-24-screenshots/issues.md` |
| `docs/baseline/2026-10-24-screenshots/next-run-checklist.md` | 6.4 | 2026-10-24 | baseline next-run checklist | ARCHIVE | `docs/archive/docs/baseline/2026-10-24-screenshots/next-run-checklist.md` |
| `docs/compose/plans/2026-07-15-integrate-expiv-subdirs.md` | 16.5 | 2026-07-29 | expIV subdir integration plan | ARCHIVE | `docs/archive/docs/compose/plans/2026-07-15-integrate-expiv-subdirs.md` |
| `docs/compose/spec/ped-pbr-pipeline.md` | 3.9 | 2026-08-26 | ped PBR spec | ARCHIVE | `docs/archive/docs/compose/spec/ped-pbr-pipeline.md` |
| `docs/compose/spec/rendering-consistency-and-beta-restoration.md` | 5.1 | 2026-08-26 | rendering consistency spec | ARCHIVE | `docs/archive/docs/compose/spec/rendering-consistency-and-beta-restoration.md` |
| `docs/obsidian/Build Guide.md` | 2.4 | 2026-07-29 | build guide (stale) | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/obsidian/Configuration Reference.md` | 10.8 | 2026-07-29 | configuration reference (stale) | MERGE-INTO | `docs/reference/INI-REFERENCE.md` |
| `docs/obsidian/Crash Analysis - 0x4C9FB5 and 0x40890A.md` | 5.3 | 2026-07-29 | crash 0x4C9FB5/0x40890A | ARCHIVE | `docs/archive/docs/obsidian/Crash Analysis - 0x4C9FB5 and 0x40890A.md` |
| `docs/obsidian/Crash Analysis Reference.md` | 9.2 | 2026-07-29 | crash analysis reference | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `docs/obsidian/Credits.md` | 1.3 | 2026-07-29 | credits | MERGE-INTO | `README.md` |
| `docs/obsidian/D3D9 Shader Architecture.md` | 8.3 | 2026-07-29 | D3D9 shader architecture | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `docs/obsidian/Debugging Guide.md` | 7.9 | 2026-07-29 | debugging guide | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `docs/obsidian/Forward Plus Rendering.md` | 4.1 | 2026-07-29 | Forward+ rendering | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/obsidian/Hardware Differences.md` | 6.3 | 2026-07-29 | platform differences | SPLIT | `docs/reference/ALPHA-TEXALPHA.md` + `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian/Home.md` | 5.6 | 2026-07-29 | obsidian home | MERGE-INTO | `README.md` |
| `docs/obsidian/Hook Architecture.md` | 13.6 | 2026-07-29 | hook architecture | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian/Index.md` | 8.0 | 2026-07-29 | obsidian index | MERGE-INTO | `README.md` |
| `docs/obsidian/Normal Map Plugin.md` | 6.3 | 2026-07-29 | normal map plugin | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/obsidian/OpenSA Architecture.md` | 8.3 | 2026-07-29 | OpenSA architecture | ARCHIVE | `docs/archive/docs/obsidian/OpenSA Architecture.md` |
| `docs/obsidian/RenderWare SDK Architecture.md` | 13.9 | 2026-07-29 | RW SDK architecture | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian/Welcome.md` | 0.0 | 2026-07-29 | redirect stub | DELETE | — (D1) |
| `docs/obsidian/BRDF_References/PBR_BRDF_Reference.md` | 3.3 | 2026-07-29 | BRDF references | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/obsidian/Build/Build Scripts.md` | 3.5 | 2026-07-20 | build scripts | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/obsidian/Hardware/Early PC GPUs.md` | 6.7 | 2026-07-29 | early PC GPUs | ARCHIVE | `docs/archive/docs/obsidian/Hardware/Early PC GPUs.md` |
| `docs/obsidian/Hardware/Overview.md` | 2.7 | 2026-07-29 | hardware overview | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian/Hardware/PS2 GS.md` | 11.4 | 2026-07-29 | PS2 GS | ARCHIVE | `docs/archive/docs/obsidian/Hardware/PS2 GS.md` |
| `docs/obsidian/Hardware/Xbox NV2A.md` | 11.3 | 2026-07-29 | Xbox NV2A | ARCHIVE | `docs/archive/docs/obsidian/Hardware/Xbox NV2A.md` |
| `docs/obsidian/Modding/Wiki/README.md` | 2.1 | 2026-07-29 | modding wiki index | MERGE-INTO | `README.md` |
| `docs/obsidian/Pipelines/Building Pipelines.md` | 2.3 | 2026-07-29 | building pipelines | MERGE-INTO | `docs/reference/BUILDING-PIPE.md` |
| `docs/obsidian/Pipelines/Grass Rendering.md` | 1.2 | 2026-07-29 | grass rendering | MERGE-INTO | `docs/reference/BUILDING-PIPE.md` |
| `docs/obsidian/Pipelines/Overview.md` | 1.7 | 2026-07-29 | pipeline overview | MERGE-INTO | `docs/modes/FEATURE-MATRIX.md` |
| `docs/obsidian/Pipelines/Vehicle Pipelines.md` | 3.0 | 2026-07-29 | vehicle pipelines | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/obsidian/PostEffects/Colour Filter.md` | 1.4 | 2026-07-29 | colour filter | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/obsidian/PostEffects/GTA IV Mode.md` | 1.5 | 2026-07-29 | GTA IV postFX | MERGE-INTO | `docs/modes/MODE-GUIDE.md` |
| `docs/obsidian/PostEffects/Overview.md` | 1.6 | 2026-07-29 | postFX overview | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/obsidian/PostEffects/Radiosity.md` | 1.6 | 2026-07-29 | radiosity | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/obsidian/Shaders/Shader Reference.md` | 6.1 | 2026-07-29 | shader ref (stale registers) | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `docs/obsidian/Structures/Entity System.md` | 2.9 | 2026-07-29 | entity structs | ARCHIVE | `docs/archive/docs/obsidian/Structures/Entity System.md` |
| `docs/obsidian/Structures/Index.md` | 3.1 | 2026-07-29 | struct index | ARCHIVE | `docs/archive/docs/obsidian/Structures/Index.md` |
| `docs/obsidian-vault/INDEX.md` | 3.6 | 2026-07-29 | vault index | MERGE-INTO | `README.md` |
| `docs/obsidian-vault/00-Index/Home.md` | 6.5 | 2026-07-29 | vault home | MERGE-INTO | `README.md` |
| `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md` | 4.4 | 2026-07-29 | VehiclePBR duplicate | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `docs/obsidian-vault/06-Technical-Decisions/IBL Env Map Decision.md` | 2.5 | 2026-07-29 | IBL env decision | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/obsidian-vault/06-Technical-Decisions/Rubber Shader Merge.md` | 3.8 | 2026-07-29 | rubber shader merge | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `docs/obsidian-vault/08-Build-Deploy/Build System.md` | 4.6 | 2026-07-29 | build system | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/obsidian-vault/08-Build-Deploy/SDK Dependencies.md` | 3.6 | 2026-07-29 | SDK deps | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `docs/obsidian-vault/AntiAliasing/Conservative Morphological AA.md` | 1.4 | 2026-07-29 | CMAA research | ARCHIVE | `docs/archive/docs/obsidian-vault/AntiAliasing/Conservative Morphological AA.md` |
| `docs/obsidian-vault/AntiAliasing/Dynamic Temporal AA Call of Duty.md` | 1.7 | 2026-07-29 | temporal AA research | ARCHIVE | `docs/archive/docs/obsidian-vault/AntiAliasing/Dynamic Temporal AA Call of Duty.md` |
| `docs/obsidian-vault/AntiAliasing/SMAA Enhanced Subpixel Morphological AA.md` | 1.3 | 2026-07-29 | SMAA algorithm | MERGE-INTO | `docs/reference/POSTFX.md` |
| `docs/obsidian-vault/Architecture/32_to_64_Bit_Transition_Reference.md` | 6.6 | 2026-10-24 | 32-to-64-bit reference | ARCHIVE | `docs/archive/docs/obsidian-vault/Architecture/32_to_64_Bit_Transition_Reference.md` |
| `docs/obsidian-vault/BRDF/Non-Parametric Sparse BRDF.md` | 2.2 | 2026-07-29 | sparse BRDF research | ARCHIVE | `docs/archive/docs/obsidian-vault/BRDF/Non-Parametric Sparse BRDF.md` |
| `docs/obsidian-vault/EPK-Reference/EPK Visual Reference - Quick Look.md` | 2.3 | 2026-07-29 | EPK visual quick look | ARCHIVE | `docs/archive/docs/obsidian-vault/EPK-Reference/EPK Visual Reference - Quick Look.md` |
| `docs/obsidian-vault/EPK-Reference/GTA San Andreas EPK Reference.md` | 2.3 | 2026-07-29 | EPK reference | ARCHIVE | `docs/archive/docs/obsidian-vault/EPK-Reference/GTA San Andreas EPK Reference.md` |
| `docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Complete Reference.md` | 11.7 | 2026-07-29 | GTA IV RAGE complete ref | ARCHIVE | `docs/archive/docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Complete Reference.md` |
| `docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Format Reference.md` | 5.6 | 2026-07-29 | GTA IV RAGE format ref | ARCHIVE | `docs/archive/docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Format Reference.md` |
| `docs/obsidian-vault/RenderWare/RenderWare V2.1 API Reference.md` | 2.5 | 2026-07-29 | RW 2.1 API ref | ARCHIVE | `docs/archive/docs/obsidian-vault/RenderWare/RenderWare V2.1 API Reference.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/64-bit Bridge Architecture.md` | 3.3 | 2026-07-29 | 64-bit bridge concept | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/64-bit Bridge Architecture.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Decided Architecture.md` | 2.9 | 2026-07-29 | decided architecture (quiz) | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Decided Architecture.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md` | 2.7 | 2026-07-29 | roadmap (vault) | MERGE-INTO | `docs/ROADMAP.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Signal Splitter Architecture.md` | 3.4 | 2026-07-29 | signal splitter concept | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Signal Splitter Architecture.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/SkyGFX Pipeline Overview.md` | 3.4 | 2026-07-29 | pipeline overview (vault) | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Three Codebase Comparison.md` | 1.8 | 2026-07-29 | 3-codebase comparison (short) | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md` | 4.0 | 2026-07-29 | weather architecture | MERGE-INTO | `docs/reference/LIGHTING-FORWARD-PLUS.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Wheel Extender Technical Plan.md` | 4.8 | 2026-07-29 | wheel extender plan | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Wheel Extender Technical Plan.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Wheel System Architecture.md` | 6.2 | 2026-07-29 | wheel system architecture | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Wheel System Architecture.md` |
| `docs/obsidian-vault/SkyGFX-Pipeline/Workflow Quiz.md` | 2.7 | 2026-07-29 | workflow quiz | ARCHIVE | `docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Workflow Quiz.md` |
| `docs/plans/2026-07-30-ragdoll-restoration.md` | 16.5 | 2026-07-30 | ragdoll plan | ARCHIVE | `docs/archive/docs/plans/2026-07-30-ragdoll-restoration.md` |
| `docs/plans/2026-08-01-remaining-roadmap.md` | 11.7 | 2026-10-24 | remaining roadmap (superseded) | ARCHIVE | `docs/archive/docs/plans/2026-08-01-remaining-roadmap.md` |
| `docs/plans/2026-09-10-libertyrecomp-postfx-port.md` | 20.3 | 2026-09-10 | LibertyRecomp postFX port | ARCHIVE | `docs/archive/docs/plans/2026-09-10-libertyrecomp-postfx-port.md` |
| `docs/plans/2026-10-08-burst-waves-execution.md` | 4.7 | 2026-10-08 | burst waves schedule | ARCHIVE | `docs/archive/docs/plans/2026-10-08-burst-waves-execution.md` |
| `docs/plans/2026-10-08-iv-v-graphics-physics-port.md` | 10.4 | 2026-10-08 | IV/V graphics+physics master plan | ARCHIVE | `docs/archive/docs/plans/2026-10-08-iv-v-graphics-physics-port.md` |
| `docs/plans/2026-10-08-restructure-2026-10-08.md` | 6.7 | 2026-10-08 | modular restructure plan | ARCHIVE | `docs/archive/docs/plans/2026-10-08-restructure-2026-10-08.md` |
| `docs/plans/2026-10-17-systematic-dig.md` | 3.4 | 2026-10-17 | systematic dig process | ARCHIVE | `docs/archive/docs/plans/2026-10-17-systematic-dig.md` |
| `docs/plans/2026-10-20-video-evidence-wave3-plan.md` | 8.2 | 2026-10-24 | wave-3 video evidence plan | ARCHIVE | `docs/archive/docs/plans/2026-10-20-video-evidence-wave3-plan.md` |
| `docs/plans/2026-10-21-alpha-reversal-findings.md` | 14.9 | 2026-10-21 | alpha-reversal findings | SPLIT | `docs/reference/ALPHA-TEXALPHA.md` + `docs/reference/WATER.md` |
| `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` | 56.1 | 2026-10-25 | AAP PS2/Xbox parity audit | SPLIT | `docs/modes/MODE-GUIDE.md` + `docs/reference/VEHICLE-PIPE.md` |
| `docs/plans/2026-10-24-code-inventory.md` | 75.3 | 2026-10-24 | code inventory | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/plans/2026-10-24-corona-blackquad-diagnosis.md` | 24.3 | 2026-10-24 | corona black-quad diagnosis | MERGE-INTO | `docs/reference/CORONA-LIGHTS.md` |
| `docs/plans/2026-10-24-gtasource-pipes.md` | 59.9 | 2026-10-25 | GTASource pipes decode | SPLIT | `docs/reference/BUILDING-PIPE.md` + `docs/reference/VEHICLE-PIPE.md` |
| `docs/plans/2026-10-24-gtasource-render-core.md` | 77.3 | 2026-10-25 | GTASource render core audit | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `docs/plans/2026-10-24-imgui-aero-theme-research.md` | 93.4 | 2026-10-24 | ImGui Aero theme research | MERGE-INTO | `docs/reference/IMGUI-MENU.md` |
| `docs/plans/2026-10-24-imgui-settings-coverage.md` | 93.7 | 2026-10-24 | ImGui settings coverage | SPLIT | `docs/reference/IMGUI-MENU.md` + `docs/reference/INI-REFERENCE.md` |
| `docs/plans/2026-10-24-legacy-parity-matrix.md` | 50.0 | 2026-10-24 | legacy parity matrix | SPLIT | `docs/modes/FEATURE-MATRIX.md` + `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` | 55.1 | 2026-10-25 | legacy PC feature inventory | SPLIT | `docs/modes/MODE-GUIDE.md` + `docs/modes/FEATURE-MATRIX.md` |
| `docs/plans/2026-10-24-mode-preset-architecture.md` | 68.3 | 2026-10-25 | mode/preset architecture | SPLIT | `docs/modes/MODE-GUIDE.md` + `docs/architecture/ARCHITECTURE.md` + `docs/reference/INI-REFERENCE.md` |
| `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` | 11.4 | 2026-10-24 | ped lighting/shadow diagnosis | SPLIT | `docs/reference/LIGHTING-FORWARD-PLUS.md` + `docs/reference/SHADOWS.md` |
| `docs/plans/2026-10-24-recovered-lane-findings.md` | 9.6 | 2026-10-24 | recovered lane findings/opacity | MERGE-INTO | `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/2026-10-24-refactor-blueprint.md` | 82.0 | 2026-10-24 | refactor blueprint (state doctrine) | SPLIT | `docs/architecture/ARCHITECTURE.md` + `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/2026-10-24-screenshot-catalogue-2104.md` | 15.1 | 2026-10-24 | screenshot catalogue 2026-10-24 | ARCHIVE | `docs/archive/docs/plans/2026-10-24-screenshot-catalogue-2104.md` |
| `docs/plans/2026-10-24-skills-research.md` | 80.7 | 2026-10-24 | skills research | ARCHIVE | `docs/archive/docs/plans/2026-10-24-skills-research.md` |
| `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md` | 7.2 | 2026-10-24 | vehicle opacity diagnosis | MERGE-INTO | `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/2026-10-24-water-tier0-diagnosis.md` | 40.3 | 2026-10-24 | water tier-0 diagnosis | MERGE-INTO | `docs/reference/WATER.md` |
| `docs/plans/2026-10-24-water-veil-diagnosis.md` | 21.1 | 2026-10-24 | water veil diagnosis | MERGE-INTO | `docs/reference/WATER.md` |
| `docs/plans/2026-10-25-debug-strategy-parity.md` | 40.4 | 2026-10-25 | debug strategy + parity | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `docs/plans/2026-10-25-noise-system-audit.md` | 88.5 | 2026-10-25 | noise system audit | MERGE-INTO | `docs/reference/NOISE.md` |
| `docs/plans/README.md` | 5.1 | 2026-10-25 | plans index/crash resume | SPLIT | `docs/history/TIMELINE.md` + `docs/guides/DEBUGGING.md` |
| `docs/plans/effects-menu-overhaul.md` | 9.7 | 2026-09-29 | effects/menu overhaul | MERGE-INTO | `docs/reference/IMGUI-MENU.md` |
| `docs/plans/questionnaire.md` | 4.2 | 2026-10-01 | resolved questionnaire | ARCHIVE | `docs/archive/docs/plans/questionnaire.md` |
| `docs/plans/swarm-deploy-2026-10-01.md` | 3.5 | 2026-10-24 | fixer swarm deploy | ARCHIVE | `docs/archive/docs/plans/swarm-deploy-2026-10-01.md` |
| `docs/plans/water-refs.md` | 17.4 | 2026-09-30 | water reference recipes | MERGE-INTO | `docs/reference/WATER.md` |
| `docs/reports/2026-10-24-sadie-reindex.md` | 10.0 | 2026-10-24 | Sadie reindex report | ARCHIVE | `docs/archive/docs/reports/2026-10-24-sadie-reindex.md` |
| `docs/research/youtube/Exploring-a-New-Approach-to-Realistic-Lighting-Radiance-Casc.md` | 1.1 | 2026-08-07 | radiance cascades research | ARCHIVE | `docs/archive/docs/research/youtube/Exploring-a-New-Approach-to-Realistic-Lighting-Radiance-Casc.md` |
| `docs/superpowers/plans/2026-08-07-smaa-crash-fix.md` | 7.9 | 2026-08-07 | SMAA crash fix plan | ARCHIVE | `docs/archive/docs/superpowers/plans/2026-08-07-smaa-crash-fix.md` |
| `docs/superpowers/plans/2026-09-08-vehicle-reflection-fix.md` | 6.1 | 2026-09-08 | vehicle reflection fix plan | ARCHIVE | `docs/archive/docs/superpowers/plans/2026-09-08-vehicle-reflection-fix.md` |
| `AGENTS.md` | 11.3 | 2026-08-27 | agent instructions | CANONICAL | `AGENTS.md` |
| `BUILD_COMPLETE.md` | 1.8 | 2026-07-29 | build complete note | DELETE | — (D2) |
| `CODEBASE_DOCUMENTATION.md` | 7.7 | 2026-07-29 | codebase documentation | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `COMMIT_MESSAGES.md` | 4.1 | 2026-07-29 | commit lyric pool | DELETE | — (D3) |
| `CREDITS.md` | 8.2 | 2026-07-29 | credits/attribution | MERGE-INTO | `README.md` |
| `DEPENDENCIES.md` | 0.8 | 2026-07-29 | dependencies | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `DRIFT_RECONCILIATION_REPORT_2026-09-14.md` | 9.8 | 2026-09-14 | drift reconciliation report | ARCHIVE | `docs/archive/DRIFT_RECONCILIATION_REPORT_2026-09-14.md` |
| `FUTURE_FEATURES.md` | 0.9 | 2026-09-14 | future features (short) | MERGE-INTO | `docs/ROADMAP.md` |
| `IMPLEMENTATION_COMPLETE.md` | 2.2 | 2026-07-29 | implementation complete note | DELETE | — (D4) |
| `OBSIDIAN_CODEBASE_DOCUMENTATION.md` | 14.8 | 2026-07-29 | obsidian codebase doc | MERGE-INTO | `docs/history/TIMELINE.md` |
| `PLAN.md` | 6.3 | 2026-07-30 | recursive priority plan | ARCHIVE | `docs/archive/PLAN.md` |
| `TODO.md` | 7.2 | 2026-09-19 | project TODO | MERGE-INTO | `docs/ROADMAP.md` |
| `TODO_PBR_NullFrame_Crash_2026-07-15.md` | 6.8 | 2026-07-15 | PBR null-frame TODO | ARCHIVE | `docs/archive/TODO_PBR_NullFrame_Crash_2026-07-15.md` |
| `WORKFLOW_PLAN.md` | 14.8 | 2026-09-19 | workflow plan/issues | ARCHIVE | `docs/archive/WORKFLOW_PLAN.md` |
| `gta_sa_vehicle_paint_classification.md` | 23.6 | 2026-07-29 | vehicle paint classification | MERGE-INTO | `docs/reference/VEHICLE-PIPE.md` |
| `readme.md` | 6.8 | 2026-09-14 | readme | CANONICAL | `README.md` |
| `.serena/memories/agent-skills.md` | 6.9 | 2026-09-14 | agent skills | MERGE-INTO | `AGENTS.md` |
| `.serena/memories/agent-tiers.md` | 1.3 | 2026-09-15 | agent tiers | MERGE-INTO | `AGENTS.md` |
| `.serena/memories/build-process.md` | 2.1 | 2026-08-30 | build process | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |
| `.serena/memories/crash-0x7618F5.md` | 1.8 | 2026-09-14 | crash 0x7618F5 | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `.serena/memories/crash-0x7FAD4D.md` | 2.5 | 2026-09-14 | crash 0x7FAD4D | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `.serena/memories/crash-context.md` | 1.0 | 2026-10-24 | crash context | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` | 23.8 | 2026-10-24 | 2026-10-20 crash checkpoint | ARCHIVE | `docs/archive/.serena/memories/crash-recovery-checkpoint-2026-10-20.md` |
| `.serena/memories/debugging.md` | 6.5 | 2026-08-31 | debugging memory | MERGE-INTO | `docs/guides/DEBUGGING.md` |
| `.serena/memories/free-agent-swarm.md` | 1.7 | 2026-10-24 | free agent swarm | MERGE-INTO | `AGENTS.md` |
| `.serena/memories/occluder-round-2026-10-01.md` | 4.3 | 2026-10-01 | occluder round | ARCHIVE | `docs/archive/.serena/memories/occluder-round-2026-10-01.md` |
| `.serena/memories/project-status.md` | 2.0 | 2026-10-24 | project status (2026-10-01) | ARCHIVE | `docs/archive/.serena/memories/project-status.md` |
| `.serena/memories/project.md` | 3.3 | 2026-08-27 | project memory | MERGE-INTO | `AGENTS.md` |
| `.serena/memories/render-pipes.md` | 4.6 | 2026-09-14 | render pipes memory | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `.serena/memories/round5-bugs-2026-10-08.md` | 4.2 | 2026-10-24 | round-5 bugs | ARCHIVE | `docs/archive/.serena/memories/round5-bugs-2026-10-08.md` |
| `.serena/memories/round5-consolidated-2026-10-08.md` | 14.4 | 2026-10-08 | round-5 consolidated | ARCHIVE | `docs/archive/.serena/memories/round5-consolidated-2026-10-08.md` |
| `.serena/memories/round6-directives-2026-10-08.md` | 85.7 | 2026-10-20 | round-6 directives | ARCHIVE | `docs/archive/.serena/memories/round6-directives-2026-10-08.md` |
| `.serena/memories/session-rules.md` | 1.1 | 2026-10-15 | session rules | MERGE-INTO | `AGENTS.md` |
| `.serena/memories/shader-pipeline.md` | 3.7 | 2026-08-27 | shader pipeline memory | MERGE-INTO | `docs/reference/SHADERS-RESOURCES.md` |
| `.serena/memories/swarm-round1-2026-10-01.md` | 4.3 | 2026-10-01 | swarm round 1 | ARCHIVE | `docs/archive/.serena/memories/swarm-round1-2026-10-01.md` |
| `.serena/memories/swarm-round2-2026-10-01.md` | 4.5 | 2026-10-02 | swarm round 2 | ARCHIVE | `docs/archive/.serena/memories/swarm-round2-2026-10-01.md` |
| `.serena/memories/swarm-round3-2026-10-01.md` | 11.1 | 2026-10-07 | swarm round 3 | ARCHIVE | `docs/archive/.serena/memories/swarm-round3-2026-10-01.md` |
| `.serena/memories/tool-recovery-2026-10-07.md` | 2.2 | 2026-10-07 | tool recovery | ARCHIVE | `docs/archive/.serena/memories/tool-recovery-2026-10-07.md` |
| `.serena/memories/vault-refs.md` | 1.3 | 2026-08-26 | vault refs | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `.serena/memories/wave-12lane.md` | 2.1 | 2026-09-14 | wave-12lane log | ARCHIVE | `docs/archive/.serena/memories/wave-12lane.md` |
| `.serena/memories/wave3-fixes-and-effects-roadmap.md` | 3.7 | 2026-09-29 | wave-3 fixes/effects roadmap | ARCHIVE | `docs/archive/.serena/memories/wave3-fixes-and-effects-roadmap.md` |
| `.opencode/memory-bank/activeContext.md` | 10.3 | 2026-10-24 | active context | ARCHIVE | `docs/archive/.opencode/memory-bank/activeContext.md` |
| `.opencode/memory-bank/history.md` | 4.5 | 2026-08-27 | history | MERGE-INTO | `docs/history/TIMELINE.md` |
| `.opencode/memory-bank/productContext.md` | 2.0 | 2026-08-26 | product context | MERGE-INTO | `README.md` |
| `.opencode/memory-bank/progress.md` | 18.1 | 2026-10-24 | progress | MERGE-INTO | `docs/history/TIMELINE.md` |
| `.opencode/memory-bank/systemPatterns.md` | 4.8 | 2026-08-26 | system patterns | MERGE-INTO | `docs/architecture/ARCHITECTURE.md` |
| `.opencode/memory-bank/techContext.md` | 2.9 | 2026-08-26 | tech context | MERGE-INTO | `docs/guides/BUILD-DEPLOY.md` |

---

# Part 2 — Contradictions between docs (and docs vs source)

Source wins in every row. "Doc B" is either a second doc that disagrees or the source authority when the docs agree with each other but not with the code.

| fact | doc A says | doc B says | correct value | evidence (file:line) |
|---|---|---|---|---|
| Where INI keys are parsed | `config.cpp` — base config; `main.cpp::refreshIni()` — unified keys (`docs/INI Configuration.md:8-9`) | No `src/config.cpp` exists in the tree; all reads are in `readIniFile()` | All INI parsing is in `src/core/main.cpp::readIniFile` (entry at `:5106`); there is no `config.cpp` to amend | `docs/INI Configuration.md:8-9`; `src/core/main.cpp:5106`; `ls src/config.cpp` → not found |
| INI section set | "All keys live under the `[SkyGfx]` section" (`docs/INI Configuration.md:6`) | Source reads `[Modes] active` and `[General] NoiseQuality` | Three sections: `[SkyGfx]`, `[Modes]`, `[General]` | `docs/INI Configuration.md:6`; `src/core/main.cpp:5146`; `src/core/main.cpp:5157` |
| SMAA quality/predication/temporal keys | Active keys `smaaPreset`, `smaaPredication`, `smaaTemporal` (`docs/INI Configuration.md:173-175`; `docs/Backwards Compatibility.md:83-85`; `docs/PostFX Pipeline.md:24`; `readme.md:71`) | Source removed them; only `smaaEnable` remains; the old slots are padding | `smaaPreset`/`smaaPredication`/`smaaTemporal` are **dead**. Internal constants are preset=HIGH, temporal=true | `src/skygfx.h:411-415`; `src/core/main.cpp` has zero `cfg.get` reads for them; `.opencode/memory-bank/systemPatterns.md:80-81` agrees with source |
| Default `colorFilter` | Default `PC` (`docs/INI Configuration.md:20`; `docs/obsidian/Configuration Reference.md:84`) | Default `pipeline=PBR`, and PBR force-rewrites the filter to `MODERN` | Fresh install (no `[Modes]`) = `COLORFILTER_MODERN(8)`. `PC` only applies if `pipeline` is changed away from PBR | `docs/INI Configuration.md:20`; `docs/obsidian/Configuration Reference.md:84`; `src/core/main.cpp:5170` (default pipeline), `:5578` (map default PC), `:5593-5601` (PBR force) |
| Default `envFresnel` | `0.7` (`docs/INI Configuration.md:81`; `docs/obsidian/Configuration Reference.md:39`; `docs/obsidian/Pipelines/Vehicle Pipelines.md:74`) | Source default is `0.4`, and every load/save clamps to `[0.0, 0.4]` | `0.4` | `docs/INI Configuration.md:81`; `src/core/main.cpp:5459`; clamp at `:403-407` |
| Default `envMapSize` | `256` (`docs/INI Configuration.md:75`; `docs/obsidian/Configuration Reference.md:37`) | Default `qualityPreset=3` (ULTRA) sets `512` before keys are read | Fresh install = `512`; PS2/Xbox/PC/IV modes override to `256`; Modern = `512` | `docs/INI Configuration.md:75`; `src/core/main.cpp:5220` (default preset), `:5366` (ULTRA 512); `src/core/modeManager.cpp:94,196,299,363,431` |
| ULTRA preset building pipe | `buildingPipe=Xbox` for ULTRA (`docs/INI Configuration.md:39`) | ULTRA does not write pipes; default `pipeline=PBR` maps to `BUILDING_PBR` | `BUILDING_PBR(3)` for ULTRA; only tiers 0-2 write pipes | `docs/INI Configuration.md:39`; `src/core/main.cpp:5317-5320` (do not override pipes), `:5187-5191` (PBR map) |
| `tagsBuildingPipe` status | Active INI key and example `tagsBuildingPipe=PS2` (`docs/obsidian/Configuration Reference.md:50`; `docs/obsidian/Pipelines/Building Pipelines.md:49`) | "Deprecated — now follows `buildingPipe` automatically" (`docs/INI Configuration.md:291`; `docs/Backwards Compatibility.md:192`) | Derived, not configurable. `c->tagsBuildingPipe = c->buildingPipe` unconditionally; no `cfg.get("tagsBuildingPipe")` exists | `src/core/main.cpp:5647`; zero key reads; mode profiles also mirror it (`src/core/modeManager.cpp:81,183,286`) |
| Accepted `buildingPipe=` strings | `PS2, PC/Xbox, GTAIV` (`docs/obsidian/Pipelines/Building Pipelines.md:48`) | Source map: `PS2`, `Xbox`, `GTAIV`, `PBR` | `PS2`, `Xbox`, `GTAIV`, `PBR`. `PC` is **not** accepted; `PBR` was missing from the doc | `src/core/main.cpp:5384-5390`; enum `src/skygfx.h:185-193` |
| Accepted `vehiclePipe=` strings | `Spec` (`docs/obsidian/Configuration Reference.md:31`; `docs/obsidian/Pipelines/Vehicle Pipelines.md:66`) | Source key is `Specular`; `Modern` is also valid and omitted by both docs | `PS2, PC, Xbox, Specular, Mobile, Neo, Leeds, VCS, Env, GTAIV, Modern` | `src/core/main.cpp:5395-5408`; enum `src/skygfx.h:168-183` |
| `colorFilter=` value list | No `Modern` (`docs/obsidian/Configuration Reference.md:84`; `docs/obsidian/PostEffects/Colour Filter.md:32`) | Source accepts `Modern` | `None, PS2, PC, Mobile, III, VC, VCS, GTAIV, Modern` | `src/core/main.cpp:5569-5579`; enum `src/skygfx.h:944-952` |
| VS constant registers | c4-c7 world; c8-c11 view; c12-c15 projection; c16 UV; c17 time; c18 fog; c19 shininess; c20-c23 light0; c24-c27 light1 (`docs/obsidian/Shaders/Shader Reference.md:9-24`) | HLSL declares c4 ambient, c5-c11 direct colours, c12-c18 directions, c19 matCol, c20 surfProps, c24-c27 world, c29 shader params, c30-c31 day/night, c32-c35 texmat, c36 eye | Use the HLSL. The doc table is wholesale wrong and must be replaced, not carried | `shaders/vs/buildingPBRVS.hlsl:17-33`; `shaders/vehiclePipeVS.hlsl:10-16` |
| PS constant registers | c4 ambient; c5-c7 3 light colours; c8-c10 directions; c11-c13 extra; c14 matCol; c15 UV; c16 fog; c18 shininess; c20-c23 SSAO; c24 SMAA; c25-c31 IV postFX (`docs/obsidian/Shaders/Shader Reference.md:28-42`) | `VehiclePBR_Modern` declares c0 surfProps, c1 fxParams, c2 eye, c3 IBL, c4 cloudShadow, c5 directCol, c6-c11 lightCol[6], c12 directDir, c13-c18 lightDir[6], c19 matCol, c22 pbrParams, c23 paintNoise, c24 ambientColor, c25-c27 view basis, c28 sky, c29 screenSize, c44 chrome, c45 cluster, c46 layer, c47 bldEnv, c48 nmParams | Use the HLSL. Per-shader layouts differ; a single universal PS table is invalid | `shaders/ps/VehiclePBR_Modern.hlsl:29-46,55-83`; `docs/obsidian/Shaders/Shader Reference.md:28-42` |
| Shader model target | "Fallback to ps_2_0 for SSAO and SMAA" (`docs/obsidian/Shaders/Shader Reference.md:50`); "legacy entry points compiled as ps_2_0" (`docs/VehiclePBR Modern.md:6`); "Building shaders: ps_2_0/vs_2_0" (`docs/Shader Architecture.md:110`); `main_ps2EnvSpecFx`/`main_specCarFx`/`main_mobileVehicle` as ps_2_0 (`docs/Shader Architecture.md:221-223,230-232`) | Build compiles **every** entry as SM3.0; 29 `ps_3_0`/`vs_3_0` entries, 0 `ps_2_0` entries | All shipped CSOs are `ps_3_0`/`vs_3_0`. SM3.0 only | `tools/fast_build.py:407,419-425`; `grep -c "('ps_3_0'\|('vs_3_0'" tools/fast_build.py` = 29, `ps_2_0` = 0; AGENTS.md:27,112 |
| `c22` (pbrParams) meaning | `{glossiness, specularF0, specTint, envFresnel}` (`docs/VehiclePBR Modern.md:49`) | `{glossiness, reflectance, clearcoat, subsurface}` (`docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md:38`) | HLSL declares `{glossiness, specular, specTint, metallicness}`; the four components are **overloaded per entry point** (`main` vs `main_building` vs `main_rubber`), so one row per entry is required | `shaders/ps/VehiclePBR_Modern.hlsl:39`; `docs/VehiclePBR Modern.md:49`; `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md:38` |
| Who consumes PS `c24` | "`c24: ambientColor` (vehicle PBR only)" (`.opencode/memory-bank/systemPatterns.md:21`) | Both vehicle `main()` and building `main_building()` use c24 | Both consume c24. Document both | `shaders/ps/VehiclePBR_Modern.hlsl:41`; `AGENTS.md:87`; `.opencode/memory-bank/systemPatterns.md:21` |
| Vehicle pipe creation hook | `0x5D9020` = vehicle pipe creation (`docs/obsidian/Hook Architecture.md:215`) | Actual install site is `0x5D9FE9` | `InjectHook(0x5D9FE9, setVehiclePipeCB)`; `0x5D9020` is not the installed hook site | `src/render/vehiclePipe.cpp:3375`; `docs/obsidian/Hook Architecture.md:215`; AGENTS.md:137 |
| GTA IV mode vehicle pipe | `vehiclePipe=CAR_NEO` (`docs/GTA IV Mode.md:14`) | Mode profile sets `CAR_GTAIV` | `CAR_GTAIV(9)`, not `CAR_NEO(5)` | `src/core/modeManager.cpp:350`; `docs/GTA IV Mode.md:14`; enum `src/skygfx.h:168-183` |
| GTA IV grade defaults / semantics | `ivDesaturation` default `1.0` and "1.0 = no desaturation"; vignette `0.75`; bloom `0.0`; exposure `1.0` (`docs/GTA IV Mode.md:26-34,42`) | `ivDesaturation` 0.3, "0=none, 1=full"; vignette 0.5; bloom 0.15; exposure 1.0 (`docs/obsidian/PostEffects/GTA IV Mode.md:28-34`) | Neither table is the IV profile. IV owns only `ivSaturation=0.0` and `ivCurves=0.0`; every other grade value is inherited from the pre-mode base. The fresh base (ULTRA) is 0.15 / 1.0 / 0.3 / 1.0 / 0.15 / 0.70 / 1.5 / 0.05 / 2.5; IV then overrides saturation and curves to 0.0, so the effective IV state is 0.15 / 1.0 / 0 / 0 / 0.15 / 0.70 / 1.5 / 0.05 / 2.5. The `1.0 = no desaturation` claim is false; the source comment says 1.0 was a 50% grey wash | `src/core/modeManager.cpp:377-378` (IV owns only these two); `src/core/main.cpp:5357-5365` (ULTRA base values); `docs/GTA IV Mode.md:26-34`; `docs/obsidian/PostEffects/GTA IV Mode.md:28-34` |
| Normal-map INI key | `enableNormalMaps` (`docs/INI Configuration.md:237`; `docs/Backwards Compatibility.md:139`) | Source key is `normalMapEnable`; `enableNormalMaps` is never read | `normalMapEnable` (int, default 0). `normalMapBuilding`, `normalMapVehicle`, `normalMapDebug`, `normalMapDebugMode` are also dead keys | `src/core/main.cpp:5508`; zero reads for `enableNormalMaps` and the four dead keys |
| Preset selection | `presetFile=custom`; shipped `gtaiv_modern.ini`, `gtav_style.ini`, `classic_sa.ini`, `balanced.ini` (`readme.md:49-52,67`) | No `presets/` directory; no `presetFile` read; mode selection is `[Modes] active=` | No `presetFile` and no shipped per-mode INIs. Selection is `[Modes] active=PS2\|Xbox\|PC\|GTAIV\|GTAV\|Modern\|Custom\|Mobile\|None`, plus the 15 built-in `GamePreset` entries in `presets.cpp` | `readme.md:49-52,67`; `ls presets` → absent; zero `presetFile` reads; `src/core/main.cpp:5124-5151`; `src/skygfx.h:214-252`; `src/core/presets.cpp:25-106` |
| Mobile pipeline mapping | `PIPELINE_MOBILE → vehiclePipe=CAR_MOBILE` only (`AGENTS.md:44`; `.serena/memories/project.md:11`; `.opencode/memory-bank/systemPatterns.md:11`) | Source also sets `buildingPipe=BUILDING_XBOX`; the mode profile mirrors it | Mobile = `BUILDING_XBOX(1)` + `CAR_MOBILE(4)`; colorFilter is not forced (inherits) | `src/core/main.cpp:5200-5203`; `src/core/modeManager.cpp:520-522`; `AGENTS.md:44` |
| Source-file counts | 67 src files; `main.cpp` 1996 lines; `config.cpp` 483 lines; `presets.cpp` 133 lines/16 presets (`docs/File Inventory.md:3,13,14,22`) | Actual: 116 files under `src/` (65 cpp + 48 h + others), `main.cpp` 7635 lines, no `config.cpp`, `presets.cpp` 157 lines, `NUM_PRESETS=15` | Use the measured counts; the file table's taxonomy is usable, its numbers and two file entries are not | `docs/File Inventory.md:3,13,14,22`; `find src -type f \| wc -l` = 116; `wc -l src/core/main.cpp` = 7635; `wc -l src/core/presets.cpp` = 157; `src/skygfx.h:251` |
| Build command / deploy target | `fix_build.py` is the master; deploy to `H:\games\clean_sa` and `E:\dev(dave)\binaries` (`docs/Build System.md:6,16`; `docs/obsidian/Build/Build Scripts.md:70-71`; `docs/obsidian-vault/08-Build-Deploy/Build System.md:14,26,97`) | Default is `fast_build.py`; deploy target is `E:\games\gtasa_skygfx_plus\skygfx.asi` from `build\Release\skygfx.dll` | `python tools/fast_build.py` is the default; `fix_build.py` is legacy; game dir is `E:\games\gtasa_skygfx_plus` | `AGENTS.md:10-14`; `tools/fast_build.py:56,669-686`; `docs/Build System.md:6,16`; `docs/obsidian/Build/Build Scripts.md:70-71` |
| `qualityPreset` writes pipes | "Remove the pipe writes in cases 0/1/2" and `qualityPresetPipes` back-compat gate (proposed) (`docs/plans/2026-10-24-mode-preset-architecture.md:243,256`) | Source still writes pipes for tiers 0-2 when no mode is active; ULTRA does not | Tiers 0/1/2 still write pipes today; only ULTRA is feature-only. The plan is **not implemented** | `docs/plans/2026-10-24-mode-preset-architecture.md:243,256`; `src/core/main.cpp:5230-5233` (LOW), `:5261-5263` (MEDIUM), `:5283-5285` (HIGH), `:5317-5320` (ULTRA) |
| Pipeline table vs mode profiles (colorFilter) | Legacy table shows `—` for colorFilter on PS2/Xbox/Mobile/GTAIV (`docs/INI Configuration.md:24-31`) | Mode profiles set PS2→`COLORFILTER_PS2(1)`, Xbox→`COLORFILTER_PC(2)`, GTAIV→`COLORFILTER_GTAIV(7)`; GTAV/Custom own nothing | The legacy table is only true when no mode is active. With `[Modes] active=`, the profile owns the filter | `docs/INI Configuration.md:24-31`; `src/core/modeManager.cpp:80,182,351`; `src/core/main.cpp:110-120` |
| `pipeline=` aliases | Options `PBR, PS2, Xbox, Mobile, GTAIV` (`docs/INI Configuration.md:16`) | Source also accepts `Modern` as an alias for PBR | Six accepted strings: `PBR, Modern, PS2, Xbox, Mobile, GTAIV` | `src/core/main.cpp:5163-5172`; `docs/INI Configuration.md:16` |

**Resource IDs:** no value contradiction was found. `docs/Shader Architecture.md:120-165` was compared against `resources/resource.h` and `resources/Resource.rc`: every ID it lists has the correct number. The doc omits **34 of the 100 IDs** in `resource.h` (notably `IDR_BUILDINGPBRVS/PS 238/239`, `IDR_PS2ENVSPECFXPS 212`, `IDR_WATER_IV/V 241/242`, `IDR_WATER_UNDERWATER 276`, `IDR_MIRROR_COMPOSITE 277`, `IDR_SIMPLESTOCHASTICPS 278`, `IDR_TREEALPHA 279`, and the 250-275 postFX block). This is a coverage gap, not a wrong value; the replacement doc must be generated from `resource.h`/`Resource.rc`.

**Enum values verified (no contradiction):** `PIPELINE_PBR=0…GTAIV=4` (`src/skygfx.h:196-205`); `BUILDING_PS2=0…PBR=3` (`:185-193`); `CAR_PS2=0…CAR_MODERN=10` (`:168-183`); `COLORFILTER_NONE=0…MODERN=8` (`:944-952`). AGENTS.md:41-45 and `.serena/memories/project.md:8-12` state these correctly except for the Mobile building/filter omission listed above.

---

# Part 3 — Target document set (20 canonical docs)

The set is organised around the user's goal: **one understandable switch between PS2 / Xbox / Fixed PC / GTA IV / GTA V / Modern**, with Custom and the hidden Mobile alias documented but not advertised as peers.

Type key: **(a) reference**, **(b) how-to**, **(c) architecture**, **(d) history**.

| # | path | type | one-line purpose | primary sources absorbed |
|---:|---|---|---|---|
| 1 | `README.md` | b | What SkyGFX Plus is, the one-line build+deploy, install, mode table, docs map, credits | `readme.md`, `docs/00 - Index.md`, `docs/Credits.md`, `CREDITS.md`, `docs/obsidian/{Home,Index,Credits}.md`, `docs/obsidian-vault/{INDEX.md,00-Index/Home.md}`, `.opencode/memory-bank/productContext.md` |
| 2 | `docs/modes/MODE-GUIDE.md` | a+b | One section per mode: owns / changes / does not change / how to switch / known gaps / verification | `docs/GTA IV Mode.md`, `docs/obsidian/PostEffects/GTA IV Mode.md`, `docs/plans/2026-10-24-mode-preset-architecture.md`, `docs/plans/2026-10-24-aap-xbox-ps2-parity.md`, `docs/plans/2026-10-24-legacy-pc-feature-inventory.md`, `docs/test-plan.md` |
| 3 | `docs/modes/FEATURE-MATRIX.md` | a | Every effect × every mode with status and evidence | `docs/Implemented Features.md`, `docs/obsidian/Pipelines/Overview.md`, `docs/plans/2026-10-24-legacy-parity-matrix.md`, `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` |
| 4 | `docs/architecture/ARCHITECTURE.md` | c | Subsystems, hook order, L1/L2/L3 state doctrine, Config ownership/precedence, module contracts, render graph | `docs/obsidian/Hook Architecture.md`, `docs/plans/2026-10-24-refactor-blueprint.md`, `docs/plans/2026-10-24-gtasource-render-core.md`, `docs/plans/2026-10-24-code-inventory.md`, `docs/File Inventory.md`, `docs/Feature Hook Pattern.md`, `docs/RW SDK Integration Strategy.md`, `docs/RW SDK Reference.md`, `docs/RW_SDK_REFERENCE.md`, `docs/obsidian/RenderWare SDK Architecture.md`, `docs/obsidian/Hardware/Overview.md`, `CODEBASE_DOCUMENTATION.md`, `.serena/memories/{render-pipes,vault-refs}.md`, `.opencode/memory-bank/systemPatterns.md` |
| 5 | `docs/reference/BUILDING-PIPE.md` | a | Building pipe variants, callbacks, day/night, detail/stochastic, grass, tagged objects | `docs/Building Pipeline.md`, `docs/obsidian/Pipelines/{Building Pipelines,Grass Rendering}.md`, `docs/plans/2026-10-24-gtasource-pipes.md` (building half) |
| 6 | `docs/reference/VEHICLE-PIPE.md` | a | Vehicle pipe variants, callbacks, shader selection, glass, rubber, paint/vehicle classification | `docs/Vehicle Pipeline.md`, `docs/Glass Shader.md`, `docs/Leeds Vehicle Pipes.md`, `docs/Neo Vehicle Pipe.md`, `docs/Vehicle Classification.md`, `docs/obsidian/Pipelines/Vehicle Pipelines.md`, `docs/obsidian-vault/06-Technical-Decisions/Rubber Shader Merge.md`, `gta_sa_vehicle_paint_classification.md`, `docs/plans/2026-10-24-gtasource-pipes.md` (vehicle half) |
| 7 | `docs/reference/WATER.md` | a | Water styles (Xbox / IV / V), parallax, shore/foam, underwater, tiers | `docs/Water Rendering.md`, `docs/plans/2026-10-24-water-tier0-diagnosis.md`, `docs/plans/2026-10-24-water-veil-diagnosis.md`, `docs/plans/water-refs.md`, `docs/plans/2026-10-21-alpha-reversal-findings.md` (water half) |
| 8 | `docs/reference/POSTFX.md` | a | PostFX chain order, tonemap, colour filters, SSAO, SMAA, motion blur, SSS blur, radiosity, lens/vision | `docs/PostFX Pipeline.md`, `docs/Color Grading.md`, `docs/Lens Effects.md`, `docs/Night and Infrared Vision.md`, `docs/Radiosity.md`, `docs/obsidian/PostEffects/{Overview,Colour Filter,Radiosity}.md`, `docs/obsidian-vault/AntiAliasing/SMAA Enhanced Subpixel Morphological AA.md` |
| 9 | `docs/reference/LIGHTING-FORWARD-PLUS.md` | a | BRDF, IBL, Forward+ tiles, weather/timecycle, normal maps, ped lighting, sky | `docs/BRDF Reference.md`, `docs/Unified Pipeline.md`, `docs/Weather Timecycle.md`, `docs/obsidian/Forward Plus Rendering.md`, `docs/obsidian/Normal Map Plugin.md`, `docs/obsidian/BRDF_References/PBR_BRDF_Reference.md`, `docs/obsidian-vault/06-Technical-Decisions/IBL Env Map Decision.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md`, `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (lighting half) |
| 10 | `docs/reference/ALPHA-TEXALPHA.md` | a | PS2 modulate, dual-pass alpha, texalpha classification, ped/vehicle opacity contracts, L1/L2/L3 alpha state | `docs/obsidian/Hardware Differences.md` (alpha half), `docs/plans/2026-10-21-alpha-reversal-findings.md`, `docs/plans/2026-10-24-recovered-lane-findings.md`, `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md`, `docs/plans/2026-10-24-refactor-blueprint.md` (alpha half), `docs/plans/2026-10-24-legacy-parity-matrix.md` (alpha/state tables) |
| 11 | `docs/reference/CORONA-LIGHTS.md` | a | Corona sprite window, z-test, sun glare, point lights, sprite-flush contract | `docs/plans/2026-10-24-corona-blackquad-diagnosis.md`; corona sections of `docs/obsidian/Hook Architecture.md`; sun/ corona fields in `src/core/modeManager.cpp` |
| 12 | `docs/reference/SHADOWS.md` | a | Ped/stencil shadows, shadow blobs, shadow-state ownership | `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (shadow half); shadow rows of `docs/plans/2026-10-24-legacy-parity-matrix.md` |
| 13 | `docs/reference/NOISE.md` | a | Every noise/stochastic producer, textures, quality tiers, consumers | `docs/plans/2026-10-25-noise-system-audit.md` |
| 14 | `docs/reference/IMGUI-MENU.md` | a | Menu structure, Modes rows, badges, theme metrics, settings coverage, never-revert UX rules | `docs/plans/2026-10-24-imgui-settings-coverage.md`, `docs/plans/2026-10-24-imgui-aero-theme-research.md`, `docs/plans/effects-menu-overhaul.md` (cross-reference DEBUGGING for hotkeys/menu tabs) |
| 15 | `docs/guides/BUILD-DEPLOY.md` | b | Premake/MSBuild/FXC, fast_build.py, shader compile, deploy, SDKs, DXVK, helper_tex, paths | `docs/Build System.md`, `docs/Project Paths.md`, `docs/SDK Dependencies.md`, `docs/DXVK Compatibility.md`, `docs/helper_tex.md`, `docs/obsidian/Build Guide.md`, `docs/obsidian/Build/Build Scripts.md`, `docs/obsidian-vault/08-Build-Deploy/{Build System,SDK Dependencies}.md`, `DEPENDENCIES.md`, `.serena/memories/build-process.md`, `.opencode/memory-bank/techContext.md` |
| 16 | `docs/guides/DEBUGGING.md` | b | Log format, log-tag dictionary, crash classes, VEH/watchdog, three-layer state receipts, crash-resume procedure | `docs/Debug Tools.md`, `docs/obsidian/Debugging Guide.md`, `docs/obsidian/Crash Analysis Reference.md`, `.serena/memories/{debugging,crash-0x7618F5,crash-0x7FAD4D,crash-context}.md`, `docs/plans/2026-10-25-debug-strategy-parity.md`, `docs/plans/README.md` (crash resume) |
| 17 | `docs/reference/INI-REFERENCE.md` | a | Authoritative key list generated from source: section, type, default, read line, save line, status | `docs/INI Configuration.md`, `docs/obsidian/Configuration Reference.md`, `docs/Backwards Compatibility.md`, `docs/plans/2026-10-24-imgui-settings-coverage.md` (Config inventory), `docs/plans/2026-10-24-mode-preset-architecture.md` (mode schema, future-marked) |
| 18 | `docs/reference/SHADERS-RESOURCES.md` | a | HLSL files, entry points, profiles, VS/PS register maps, texture stages, `IDR_*` resource map | `docs/Shader Architecture.md`, `docs/VehiclePBR Modern.md`, `docs/PBR Common.md`, `docs/obsidian/D3D9 Shader Architecture.md`, `docs/obsidian/Shaders/Shader Reference.md` (compilation table only), `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md`, `.serena/memories/shader-pipeline.md` |
| 19 | `docs/history/TIMELINE.md` | d | One compressed chronology: versions, waves, every fixed defect, per-wave records, links to archive | `docs/Project Lineage.md`, `docs/Three-Codebase Comparison.md`, `OBSIDIAN_CODEBASE_DOCUMENTATION.md`, `.opencode/memory-bank/{history,progress}.md`, `.serena/memories/{round5-bugs,round5-consolidated,round6-directives,swarm-round1,swarm-round2,swarm-round3,occluder-round,wave-12lane,wave3-fixes-and-effects-roadmap,crash-recovery-checkpoint-2026-10-20}.md`, `docs/plans/*` (status rows) |
| 20 | `docs/ROADMAP.md` | d | Reserved backlog lane: shipped/next/deferred; no duplicated design prose | `docs/Future Features.md`, `docs/Roadmap to Ultimate Mod.md`, `TODO.md` (open items), `FUTURE_FEATURES.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md`, `docs/plans/2026-10-08-iv-v-graphics-physics-port.md` (unscheduled matrix), wheel planned docs |

## 3.1 Required content spec for the mode-critical docs

### `README.md` (entry)

Must contain, in this order:

1. One paragraph: SkyGFX Plus is an ASI plugin that replaces GTA SA's RW 3.6 D3D9 building/vehicle/water/postFX paths to reproduce PS2, Xbox, Fixed PC, GTA IV, GTA V and Modern looks.
2. One-line build+deploy: `python tools/fast_build.py` → `build\Release\skygfx.dll` → `E:\games\gtasa_skygfx_plus\skygfx.asi`.
3. **Mode table** with columns: mode, `[Modes] active=` alias, building pipe, vehicle pipe, color filter, what it is for, status. Values from Part 5.1.
4. Install (ASI loader, `skygfx.ini`, no `presets/` folder) and the 3 INI sections.
5. Link map to the other 19 docs.
6. Credits/attribution, compressed from the three credits files.

Do **not** repeat: source counts, full INI tables, shader registers, or per-wave history.

### `docs/modes/MODE-GUIDE.md` (the user's primary doc)

Structure:

1. **Mental model** — exactly one active mode; a mode is a named profile layered on the live Config. Entering snapshots the pre-mode Config; A→B rebuilds from that same snapshot; toggling the active mode off restores it verbatim. Cite `src/core/modeManager.h:9-16` and `src/core/modeManager.cpp:695-708`.
2. **How to switch** — `[Modes] active=PS2|Xbox|PC|GTAIV|GTAV|Modern|Custom|Mobile|None`; F4 → Modes rows (`debugmenu_ui.cpp:2683-2703`); `ModeManager_SetMode` is a thin alias of `ApplyMode`. Reload with F11 re-applies the INI mode once (`main.cpp:6329-6336`).
3. **Precedence** — a boxed chain and an explicit "who wins" table:
   - per-config-slot base snapshot (lowest)
   - active mode profile (owned fields)
   - top-level INI keys for **unowned** fields
   - live menu edits (highest; may create a future per-mode override, not implemented)
   - while a pipe-owning mode is active, top-level `pipeline`, `buildingPipe`, `vehiclePipe`, `colorFilter` are ignored with a log (`main.cpp:110-120,5392,5410,5586`)
   - `qualityPreset` still writes pipes for tiers 0-2 when no mode is active (`main.cpp:5230-5233,5261-5263,5283-5285`)
   - `pipelineOverride` still rewrites the `pipeline` mirror even during a mode (`main.cpp:5175-5177`) — flag as a known hazard
   - the Style combo / four preset buttons still call `ApplyPreset`, which writes `colorFilter` and feature toggles and clears the mode latch (`presets.cpp:121-137`; `debugmenu_ui.cpp:2714-2775`)
4. **One section per mode** (PS2, Xbox, Fixed PC/Legacy PC, GTA IV, GTA V, Modern, Custom, None, Mobile-legacy). Each section must have:
   - identity: `ModeId`, INI alias, display name, profile source line range
   - owned-field count and a link to the generated full ownership table
   - pipes/filter/preset/ivMode table
   - **what it changes** (grouped: pipeline, postFX, alpha/state, env/water, perf)
   - **what it does NOT change** (the inherited set, explicitly; this is the missing half today)
   - how to switch in and out
   - known gaps (from Part 5)
   - verification steps (from `docs/test-plan.md` Groups 1-3 plus the mode receipts below)
5. **Mode switching receipts** — expected log line `[Mode] apply <name> pipes=<b>/<v>/<cf> iv=<n>` (`modeManager.cpp:710-718`) plus `[Mode] active=<alias> (from INI)` (`main.cpp:6985-6989`); what to check in `skygfx_dbg.log`.
6. **Multi-config caveat** — the base snapshot is per `configs[]` slot, not process-global; `setConfig()` can repoint `config` (`modeManager.cpp:28-30,582-591`).
7. **Generated appendix: full ownership matrix.** Must be generated from `kValues*` (`modeManager.cpp:75-523`), not hand-written. One row per Config field, one column per mode, cell = value or `inherit`.

### `docs/modes/FEATURE-MATRIX.md`

Rows = effects/subsystems; columns = `None | PS2 | Xbox | Fixed PC | GTA IV | GTA V | Modern | Custom | Mobile`. Status vocabulary exactly: **shipped**, **partial**, **missing**, **N/A**. Every cell must cite either a profile value, a source symbol, or a verification run. Minimum rows: building PBR, vehicle PBR, PS2 modulate, dual-pass, detail maps, stochastic, env map, water (Xbox/IV/V), radiosity, colour filters, SSAO, SMAA, motion blur, SSS post, skin/hair/vegetation, normal map, bloom, DOF, sun shafts, FXAA, grain, CA, tonemap, Forward+, pipe chain, normal buffer, velocity buffer, depth hook, grass instancing, tree wind, coronas, ped shadows, stencil shadows, ImGui menu.

The matrix must show **GTA V as water-only** and **Mobile as hidden/legacy**, not as full peers.

### `docs/architecture/ARCHITECTURE.md`

Must contain:

1. Subsystem map (files and responsibilities) — verified against the tree, not the stale 67-file inventory.
2. Hook installation order — `DllMain` immediate hooks, `InjectDelayedPatches`, `InitialiseGame_hook`; built from `main.cpp:7068-7530`, `buildingPipe.cpp:3046-3056`, `vehiclePipe.cpp:3373-3377`.
3. **L1/L2/L3 state doctrine** — L1 RW cache (`RwRenderStateSet/Get`), L2 RW D3D9 driver cache (`RwD3D9SetRenderState`, flush `0x7FC200`), L3 raw device (`d3d9device->SetRenderState` that the caches cannot see). Cite `docs/plans/2026-10-24-refactor-blueprint.md:39-43`. State the rule: modules write through `pipelinecommon` primitives; no ad-hoc L3 writes.
4. Config ownership/precedence — one diagram covering `ModeManager`, `ApplyPreset`, `qualityPreset`, `pipeline`, `pipelineOverride`, `[Modes] active`, and the menu writers.
5. Module contracts — init/shutdown/device-reset/frame/scene/config; one log-tag prefix per module.
6. Render graph — frame order from `gtasource-render-core` (building, vehicle, water, postFX, corona, sprites, final effects), with hook addresses.
7. RW 3.6 pinning — 3.7 headers are used for types/backports only; no 3.7 runtime API calls (AGENTS.md:139-141).
8. Dead-code register — `ps2_mode.cpp`, `xbox_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`, `custom_mode.cpp` are unreachable preset bodies; `test.cpp`/bridge etc. as verified.

### Subsystem refs (5-14)

Each must follow the same skeleton: purpose → owned Config fields → owned INI keys → callbacks/hooks → shader entry points → state contract (L1/L2/L3) → mode interaction → verification → known gaps → archive links. The mode interaction section is mandatory: state whether the subsystem is mode-owned, mode-sensitive, or mode-neutral.

### `docs/guides/BUILD-DEPLOY.md`

Must replace the `fix_build.py`-only instructions. `fast_build.py` is the default; `fix_build.py` is legacy. Include: prerequisites, `--fastest/--rebuild/--shaders/--launch`, FXC path, premake/project generation, shader multi-entry list, `Resource.rc` embedding, deploy to `E:\games\gtasa_skygfx_plus\skygfx.asi`, log path, DXVK layout, `helper_tex` staging. Do not carry `H:\games\clean_sa` or `E:\dev(dave)\binaries`.

### `docs/guides/DEBUGGING.md`

Must include a **log-tag dictionary** generated from `dbglog`, `dbglog_throttle`, `dbglog_once`, `dbglog_first` call sites: tag → owner module → meaning → expected frequency. Include the three-layer receipt pattern, crash classes with addresses (`0x7618F5`, `0x7FAD4D`, `0x7F98DF`, `0x7F9ECB`), VEH/watchdog, and the crash-resume procedure. Exclude raw 80 KB round directives; compress them into timeline rows.

### `docs/reference/INI-REFERENCE.md`

Generated, never hand-edited. For each key: section, name, type, default, `readIniFile` line, `saveConfigTo` line, menu row, status (`active`, `deprecated`, `dead`, `mode-owned`). Sources of truth: `main.cpp` `cfg.get`/`cfg.set`, `skygfx.h` `Config`, `modeManager.cpp` `MV_*` tables. Must explicitly list the dead keys (`smaaPreset`, `smaaPredication`, `smaaTemporal`, `enableNormalMaps`, `normalMapBuilding`, `normalMapVehicle`, `normalMapDebug`, `normalMapDebugMode`, `presetFile`, `tagsBuildingPipe` as read) and the mode-owned keys.

### `docs/reference/SHADERS-RESOURCES.md`

Generated from HLSL and resources. Must contain per-entry-point tables (not one global PS table): file, entry, profile, CSO, `IDR_*`, VS registers, PS registers, samplers. The old `docs/obsidian/Shaders/Shader Reference.md` register tables are **not** carried; only its compilation-target table is reused as a starting point, and even that must be corrected to SM3.0-only. Include the 100 `IDR_*` entries from `resource.h` and the RC line for each.

### `docs/history/TIMELINE.md`

One chronological table: date → wave/version → change → status → evidence/commit → archive link. Compress each 2026-10 round/wave to 1-5 rows. Keep every fixed defect searchable by symptom. Do not duplicate design prose; link to the subsystem refs.

### `docs/ROADMAP.md`

Reserved for a separate lane. Only the schema and the "shipped vs next vs deferred" split are defined here. Do not move implementation prose into it.

## 3.2 Carry map — exactly what each canonical doc takes

A source may appear under multiple targets only where it genuinely splits; the listed sections are the only content worth carrying. Everything else in that source is archive-only.

| target | sources | sections carried (and corrected) |
|---|---|---|
| `README.md` | `docs/00 - Index.md`; `docs/Credits.md`; `CREDITS.md`; `docs/obsidian/Home.md`; `docs/obsidian/Index.md`; `docs/obsidian/Credits.md`; `docs/obsidian/Modding/Wiki/README.md`; `docs/obsidian-vault/INDEX.md`; `docs/obsidian-vault/00-Index/Home.md`; `.opencode/memory-bank/productContext.md`; `readme.md` | §Architecture/§Rendering Pipeline/§Status nav; §Core Contributors/§Shader Credits/§Academic Research; attribution for GTA SA Stories 1987 and supporting mods; §What This Mod Does/§Key Features; §Overview goals; §Main Topics links; §Vault Structure/§Map of Content; §Project/§Goals/§Scope. Replace all preset/build claims with the Part 2 corrections. |
| `AGENTS.md` | `.serena/memories/agent-skills.md`; `.serena/memories/agent-tiers.md`; `.serena/memories/free-agent-swarm.md`; `.serena/memories/project.md`; `.serena/memories/session-rules.md` | Routing/tier table; free-model dispatch; project pipeline map (correct the Mobile row); hook list (correct 0x5D9FE9); gotchas; session-startup/token rules. Keep agent operations only; product facts live in the canonical docs. |
| `docs/modes/MODE-GUIDE.md` | `docs/GTA IV Mode.md`; `docs/obsidian/PostEffects/GTA IV Mode.md`; `docs/plans/2026-10-24-mode-preset-architecture.md`; `docs/plans/2026-10-24-aap-xbox-ps2-parity.md`; `docs/plans/2026-10-24-legacy-pc-feature-inventory.md`; `docs/test-plan.md` | GTA IV §Rendering Configuration (correct `CAR_GTAIV`) and §Key Design Decisions (correct grade semantics); IV postFX features/tonemap curve; mode plan §2 mode definitions, §3.1-3.6 owned-field tables (cross-check every row against `kValues*`; the plan is design-stage), §5.3 persistence (mark `[Mode.*]` as **not implemented**); AAP §Look baseline PS2/Xbox + config defaults; Legacy PC §1.1 mode plumbing + §1.3 PC deltas; test plan Group 1.1-1.3 verification recipes. |
| `docs/modes/FEATURE-MATRIX.md` | `docs/Implemented Features.md`; `docs/obsidian/Pipelines/Overview.md`; `docs/plans/2026-10-24-legacy-parity-matrix.md`; `docs/plans/2026-10-24-legacy-pc-feature-inventory.md`; `docs/test-plan.md` | Implemented-features list (status only); §Supported Pipelines and §Effects Matrix; §2.1-2.x behaviour tables; §1.2 feature register; coverage map. |
| `docs/architecture/ARCHITECTURE.md` | `docs/obsidian/Hook Architecture.md`; `docs/plans/2026-10-24-refactor-blueprint.md`; `docs/plans/2026-10-24-mode-preset-architecture.md` (architecture half); `docs/plans/2026-10-24-gtasource-render-core.md`; `docs/plans/2026-10-24-code-inventory.md`; `docs/File Inventory.md`; `docs/Feature Hook Pattern.md`; `docs/RW SDK Integration Strategy.md`; `docs/RW SDK Reference.md`; `docs/RW_SDK_REFERENCE.md`; `docs/obsidian/RenderWare SDK Architecture.md`; `docs/obsidian/Hardware/Overview.md`; `docs/obsidian/Hardware Differences.md` (platform half); `docs/obsidian-vault/SkyGFX-Pipeline/SkyGFX Pipeline Overview.md`; `docs/obsidian-vault/SkyGFX-Pipeline/Three Codebase Comparison.md`; `docs/Three-Codebase Comparison.md`; `CODEBASE_DOCUMENTATION.md`; `.serena/memories/render-pipes.md`; `.serena/memories/vault-refs.md`; `.opencode/memory-bank/systemPatterns.md` | Hook timeline/§Key Hook Functions/§Hook Conflict 0x5D7F40/§SilentPatch (correct vehicle address); refactor §1.1 L1/L2/L3 table/§1.2-1.3 pain map/§7 target contracts (doctrine only); render-core §1 frame graph/§1.2 RenderScene order; code inventory §1 file table (re-measure); File Inventory directory taxonomy only (discard counts and `config.cpp`/`hooks.cpp`); Feature Hook Pattern (as module-contract template); RW strategy §Compatibility Premise/§Tier list; RW refs §Key Structures/§Crash at offset 8; RW architecture §SDK version/backport; hardware matrix; 3-codebase §1 file mapping/§2 shader comparison; CODEBASE §Source Code Structure; render-pipes §Building/Vehicle/PostFX/PipelineCommon; vault refs; systemPatterns §Architecture Overview/§Key Patterns (correct c24). |
| `docs/reference/BUILDING-PIPE.md` | `docs/Building Pipeline.md`; `docs/obsidian/Pipelines/Building Pipelines.md`; `docs/obsidian/Pipelines/Grass Rendering.md`; `docs/plans/2026-10-24-gtasource-pipes.md` (building) | §Pipeline Modes/§Merged Building Shaders/§Render Flow; §Available Pipelines/§Effects Matrix/§PC Issues (discard the INI block with `tagsBuildingPipe`); grass differences/config; GTASource building pipeline declarations/callbacks. |
| `docs/reference/VEHICLE-PIPE.md` | `docs/Vehicle Pipeline.md`; `docs/Glass Shader.md`; `docs/Leeds Vehicle Pipes.md`; `docs/Neo Vehicle Pipe.md`; `docs/Vehicle Classification.md`; `docs/obsidian/Pipelines/Vehicle Pipelines.md`; `docs/obsidian-vault/06-Technical-Decisions/Rubber Shader Merge.md`; `gta_sa_vehicle_paint_classification.md`; `docs/plans/2026-10-24-gtasource-pipes.md` (vehicle); `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` (vehicle deltas); `docs/SkyGFX_Living_Systems_Database.md` (vehicle/paint DB, summary) | §Pipeline Modes/§Render Callback Summary/§Shader Consolidation; glass Fresnel/tint profiles; LCS/VCS differences; Neo features/tweaking; paint/era/drive-type classification; available pipelines/env-map textures (add Modern); rubber merge decision; paint probability tables (summary, not the 24 KB dump); GTASource vehicle callbacks. |
| `docs/reference/WATER.md` | `docs/Water Rendering.md`; `docs/plans/2026-10-24-water-tier0-diagnosis.md`; `docs/plans/2026-10-24-water-veil-diagnosis.md`; `docs/plans/water-refs.md`; `docs/plans/2026-10-21-alpha-reversal-findings.md` (water) | Water parallax/Gerstner/config; tier-0 §0 verdict/§2 root causes/§4 fix spec; veil §Top finding/§2 `[water]` evidence; §Hybrid recipe/§Quality tiers; water/washout schematic. |
| `docs/reference/POSTFX.md` | `docs/PostFX Pipeline.md`; `docs/Color Grading.md`; `docs/Lens Effects.md`; `docs/Night and Infrared Vision.md`; `docs/Radiosity.md`; `docs/obsidian/PostEffects/Overview.md`; `docs/obsidian/PostEffects/Colour Filter.md`; `docs/obsidian/PostEffects/Radiosity.md`; `docs/obsidian-vault/AntiAliasing/SMAA Enhanced Subpixel Morphological AA.md` | §Effects chain/SSAO/SMAA/Motion Blur/filters/tonemap (remove `smaaPreset`/`smaaPredication`/`smaaTemporal`); color grading controls; lens effects (mark minimal); night/infrared; radiosity variants; effects summary; filter types (add Modern); SMAA implementation/params. |
| `docs/reference/LIGHTING-FORWARD-PLUS.md` | `docs/BRDF Reference.md`; `docs/Unified Pipeline.md`; `docs/Weather Timecycle.md`; `docs/obsidian/Forward Plus Rendering.md`; `docs/obsidian/Normal Map Plugin.md`; `docs/obsidian/BRDF_References/PBR_BRDF_Reference.md`; `docs/obsidian-vault/06-Technical-Decisions/IBL Env Map Decision.md`; `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md`; `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (lighting) | BRDF theory/material values; PBR functions (cross-reference only; owned by SHADERS-RESOURCES); Forward+ passes/status; weather/timecycle + expanded config; normal-map plugin integration/addresses; BRDF sources; IBL decision/implementation; weather multi-timecyc/hash selection; near-camera black-ped root cause/fix. |
| `docs/reference/ALPHA-TEXALPHA.md` | `docs/obsidian/Hardware Differences.md` (alpha); `docs/plans/2026-10-21-alpha-reversal-findings.md`; `docs/plans/2026-10-24-recovered-lane-findings.md`; `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md`; `docs/plans/2026-10-24-refactor-blueprint.md` (alpha); `docs/plans/2026-10-24-legacy-parity-matrix.md` (alpha/state) | PS2 modulate/blend/alpha-test/dual-pass theory; verified state map/raw-vs-rw evidence; opacity decision points; vehicle opacity causes 1-2; alpha contract pain map; alpha/state parity tables. |
| `docs/reference/CORONA-LIGHTS.md` | `docs/plans/2026-10-24-corona-blackquad-diagnosis.md`; `docs/obsidian/Hook Architecture.md` (corona hooks only) | §0 verdict/§1 binary facts/§2 producers/§fix spec/verification; sprite-flush hooks 0x70CF62/0x70CF9B, z-test behavior, point-light hooks. |
| `docs/reference/SHADOWS.md` | `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (shadow); `docs/plans/2026-10-24-legacy-parity-matrix.md` (shadow) | §B black shadow blobs/root cause/fix; ped/stencil shadow parity rows. |
| `docs/reference/NOISE.md` | `docs/plans/2026-10-25-noise-system-audit.md` | §1 producer inventory/§2 texture files/§3 consumers/§4 quality verdict/§5 fix list. |
| `docs/reference/IMGUI-MENU.md` | `docs/plans/2026-10-24-imgui-settings-coverage.md`; `docs/plans/2026-10-24-imgui-aero-theme-research.md`; `docs/plans/effects-menu-overhaul.md` | §1 settings inventory/§2 menu plan/§3 defects; Aero metrics/theme rules; §0 never-revert/§1-2 menu/effects plan. Cross-reference DEBUGGING for hotkeys/log tabs. |
| `docs/guides/BUILD-DEPLOY.md` | `docs/Build System.md`; `docs/Project Paths.md`; `docs/SDK Dependencies.md`; `docs/DXVK Compatibility.md`; `docs/helper_tex.md`; `docs/obsidian/Build Guide.md`; `docs/obsidian/Build/Build Scripts.md`; `docs/obsidian-vault/08-Build-Deploy/Build System.md`; `docs/obsidian-vault/08-Build-Deploy/SDK Dependencies.md`; `DEPENDENCIES.md`; `.serena/memories/build-process.md`; `.opencode/memory-bank/techContext.md` | Build command (switch to fast_build), requirements, shader compilation, deploy; paths/SDKs; bundled/linked libs; DXVK install/layout/settings; helper_tex TL;DR/manifest; premake/build scripts; subdirectory structure; dependency paths; stack/paths. Discard H: paths and `bin\Release` deploy. |
| `docs/guides/DEBUGGING.md` | `docs/Debug Tools.md`; `docs/obsidian/Debugging Guide.md`; `docs/obsidian/Crash Analysis Reference.md`; `.serena/memories/debugging.md`; `.serena/memories/crash-0x7618F5.md`; `.serena/memories/crash-0x7FAD4D.md`; `.serena/memories/crash-context.md`; `docs/plans/2026-10-25-debug-strategy-parity.md`; `docs/plans/README.md` | Hotkeys/log/crash handler; log format/key entries/VEH/watchdog; crash handler architecture/log reading; crash patterns; 0x7618F5 mechanism/fix; 0x7FAD4D mechanism/fix; crash context summary; debug strategy/three-layer receipts/parity checklist; crash-resume procedure. |
| `docs/reference/INI-REFERENCE.md` | `docs/INI Configuration.md`; `docs/obsidian/Configuration Reference.md`; `docs/Backwards Compatibility.md`; `docs/plans/2026-10-24-imgui-settings-coverage.md` (inventory); `docs/plans/2026-10-24-mode-preset-architecture.md` (schema) | All key tables, but regenerate from source; key-origin matrix with `tagsBuildingPipe` correctly deprecated and `smaa*`/`enableNormalMaps` dead; Config inventory; `[Modes] active` (implemented) and `[Mode.*]` (future, not implemented). |
| `docs/reference/SHADERS-RESOURCES.md` | `docs/Shader Architecture.md`; `docs/VehiclePBR Modern.md`; `docs/PBR Common.md`; `docs/obsidian/D3D9 Shader Architecture.md`; `docs/obsidian/Shaders/Shader Reference.md` (compilation table only); `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md`; `.serena/memories/shader-pipeline.md` | Directory/consolidation/ID map (complete 100 IDs); entry points/register tables (per entry, corrected); PBR function contracts; D3D9/RW background; compilation targets (SM3.0 only); duplicate entry-point table (dedupe); paths/multi-entry/register map (correct c20/c24). Do **not** carry the old register tables. |
| `docs/history/TIMELINE.md` | `docs/Project Lineage.md`; `docs/Three-Codebase Comparison.md` (lineage); `OBSIDIAN_CODEBASE_DOCUMENTATION.md`; `.opencode/memory-bank/history.md`; `.opencode/memory-bank/progress.md`; `.serena/memories/round5-bugs-2026-10-08.md`; `.serena/memories/round5-consolidated-2026-10-08.md`; `.serena/memories/round6-directives-2026-10-08.md`; `.serena/memories/swarm-round1-2026-10-01.md`; `.serena/memories/swarm-round2-2026-10-01.md`; `.serena/memories/swarm-round3-2026-10-01.md`; `.serena/memories/occluder-round-2026-10-01.md`; `.serena/memories/wave-12lane.md`; `.serena/memories/wave3-fixes-and-effects-roadmap.md`; `.serena/memories/crash-recovery-checkpoint-2026-10-20.md`; `docs/plans/README.md` | Version evolution/lineage; size/diff summary; evolution analysis tables; session history; completed log (dedupe); bug/wave records compressed to dated rows; crash checkpoint compressed; plan status index. |
| `docs/ROADMAP.md` | `docs/Future Features.md`; `docs/Roadmap to Ultimate Mod.md`; `TODO.md` (open only); `FUTURE_FEATURES.md`; `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md`; `docs/plans/2026-10-08-iv-v-graphics-physics-port.md`; `docs/SkyGFX_Living_Systems_Database.md` (roadmap half); `docs/wheel_lod_system.md`; `docs/wheel_naming_convention.md`; `docs/wheel_texture_atlas.md` | Phases 1-5; open TODO items (done items go to TIMELINE); deferred feature list; vault roadmap; unscheduled feature matrix; wheel LOD/atlas/naming backlog items. |

## 3.3 Content that must NOT be carried into canonical docs

- The old PS/VS register tables in `docs/obsidian/Shaders/Shader Reference.md` (wrong registers; replaced by HLSL-generated tables).
- Any `ps_2_0` fallback claim; the build is SM3.0-only.
- `smaaPreset`, `smaaPredication`, `smaaTemporal`, `enableNormalMaps`, `normalMapBuilding`, `normalMapVehicle`, `normalMapDebug`, `normalMapDebugMode`, `presetFile` as live keys.
- `tagsBuildingPipe` as an INI key (it is derived).
- `vehiclePipe=CAR_NEO` for GTA IV; `ivDesaturation=1.0 means no desaturation`.
- The `docs/File Inventory.md` counts (67 files, 1996-line main.cpp, `config.cpp`, 16 presets).
- `fix_build.py` as the only build path; `H:\games\clean_sa` / `E:\dev(dave)\binaries` deploy targets.
- The claim that `ps2_mode.cpp`/`xbox_mode.cpp` are active preset implementations (they are dead bodies with no call sites).
- Any statement that `[Mode.*]` overrides or `qualityPresetPipes` exist; both are proposed only.

---

# Part 4 — Deletion / archive manifest (executable checklist)

Do not run this in the survey pass. Run it only after the destination doc for each source has been written and link-checked. Preserve the original relative path under `docs/archive/` so every citation in this report remains resolvable.

## 4.1 Archive — 67 files (636,612 B / 621.7 KB)

```bash
# --- docs root -------------------------------------------------------------
mkdir -p docs/archive/docs
mv "docs/FIXLOG_PBR_NullFrame_Deref.md"        "docs/archive/docs/FIXLOG_PBR_NullFrame_Deref.md"
mv "docs/compress-recovery.md"                 "docs/archive/docs/compress-recovery.md"
mv "docs/wheel_lod_system.md"                  "docs/archive/docs/wheel_lod_system.md"
mv "docs/wheel_naming_convention.md"           "docs/archive/docs/wheel_naming_convention.md"
mv "docs/wheel_texture_atlas.md"               "docs/archive/docs/wheel_texture_atlas.md"

# --- baseline (4) ----------------------------------------------------------
mkdir -p "docs/archive/docs/baseline/2026-10-24-screenshots"
mv "docs/baseline/2026-10-24-screenshots/README.md"             "docs/archive/docs/baseline/2026-10-24-screenshots/README.md"
mv "docs/baseline/2026-10-24-screenshots/images.md"             "docs/archive/docs/baseline/2026-10-24-screenshots/images.md"
mv "docs/baseline/2026-10-24-screenshots/issues.md"             "docs/archive/docs/baseline/2026-10-24-screenshots/issues.md"
mv "docs/baseline/2026-10-24-screenshots/next-run-checklist.md" "docs/archive/docs/baseline/2026-10-24-screenshots/next-run-checklist.md"

# --- compose (3) -----------------------------------------------------------
mkdir -p docs/archive/docs/compose/plans docs/archive/docs/compose/spec
mv "docs/compose/plans/2026-07-15-integrate-expiv-subdirs.md" "docs/archive/docs/compose/plans/2026-07-15-integrate-expiv-subdirs.md"
mv "docs/compose/spec/ped-pbr-pipeline.md"                    "docs/archive/docs/compose/spec/ped-pbr-pipeline.md"
mv "docs/compose/spec/rendering-consistency-and-beta-restoration.md" "docs/archive/docs/compose/spec/rendering-consistency-and-beta-restoration.md"

# --- obsidian (7) ----------------------------------------------------------
mkdir -p "docs/archive/docs/obsidian/Hardware" "docs/archive/docs/obsidian/Structures"
mv "docs/obsidian/Crash Analysis - 0x4C9FB5 and 0x40890A.md" "docs/archive/docs/obsidian/Crash Analysis - 0x4C9FB5 and 0x40890A.md"
mv "docs/obsidian/OpenSA Architecture.md"                    "docs/archive/docs/obsidian/OpenSA Architecture.md"
mv "docs/obsidian/Hardware/Early PC GPUs.md"                 "docs/archive/docs/obsidian/Hardware/Early PC GPUs.md"
mv "docs/obsidian/Hardware/PS2 GS.md"                        "docs/archive/docs/obsidian/Hardware/PS2 GS.md"
mv "docs/obsidian/Hardware/Xbox NV2A.md"                     "docs/archive/docs/obsidian/Hardware/Xbox NV2A.md"
mv "docs/obsidian/Structures/Entity System.md"               "docs/archive/docs/obsidian/Structures/Entity System.md"
mv "docs/obsidian/Structures/Index.md"                       "docs/archive/docs/obsidian/Structures/Index.md"

# --- obsidian-vault (15) ---------------------------------------------------
mkdir -p "docs/archive/docs/obsidian-vault/AntiAliasing" \
         "docs/archive/docs/obsidian-vault/Architecture" \
         "docs/archive/docs/obsidian-vault/BRDF" \
         "docs/archive/docs/obsidian-vault/EPK-Reference" \
         "docs/archive/docs/obsidian-vault/RenderWare" \
         "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline"
mv "docs/obsidian-vault/AntiAliasing/Conservative Morphological AA.md"       "docs/archive/docs/obsidian-vault/AntiAliasing/Conservative Morphological AA.md"
mv "docs/obsidian-vault/AntiAliasing/Dynamic Temporal AA Call of Duty.md"    "docs/archive/docs/obsidian-vault/AntiAliasing/Dynamic Temporal AA Call of Duty.md"
mv "docs/obsidian-vault/Architecture/32_to_64_Bit_Transition_Reference.md"   "docs/archive/docs/obsidian-vault/Architecture/32_to_64_Bit_Transition_Reference.md"
mv "docs/obsidian-vault/BRDF/Non-Parametric Sparse BRDF.md"                  "docs/archive/docs/obsidian-vault/BRDF/Non-Parametric Sparse BRDF.md"
mv "docs/obsidian-vault/EPK-Reference/EPK Visual Reference - Quick Look.md"  "docs/archive/docs/obsidian-vault/EPK-Reference/EPK Visual Reference - Quick Look.md"
mv "docs/obsidian-vault/EPK-Reference/GTA San Andreas EPK Reference.md"      "docs/archive/docs/obsidian-vault/EPK-Reference/GTA San Andreas EPK Reference.md"
mv "docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Complete Reference.md" "docs/archive/docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Complete Reference.md"
mv "docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Format Reference.md"   "docs/archive/docs/obsidian-vault/RenderWare/GTA IV RAGE Engine Format Reference.md"
mv "docs/obsidian-vault/RenderWare/RenderWare V2.1 API Reference.md"         "docs/archive/docs/obsidian-vault/RenderWare/RenderWare V2.1 API Reference.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/64-bit Bridge Architecture.md"       "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/64-bit Bridge Architecture.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/Decided Architecture.md"             "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Decided Architecture.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/Signal Splitter Architecture.md"     "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Signal Splitter Architecture.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/Wheel Extender Technical Plan.md"    "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Wheel Extender Technical Plan.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/Wheel System Architecture.md"        "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Wheel System Architecture.md"
mv "docs/obsidian-vault/SkyGFX-Pipeline/Workflow Quiz.md"                    "docs/archive/docs/obsidian-vault/SkyGFX-Pipeline/Workflow Quiz.md"

# --- plans (12) ------------------------------------------------------------
mkdir -p docs/archive/docs/plans
mv "docs/plans/2026-07-30-ragdoll-restoration.md"        "docs/archive/docs/plans/2026-07-30-ragdoll-restoration.md"
mv "docs/plans/2026-08-01-remaining-roadmap.md"          "docs/archive/docs/plans/2026-08-01-remaining-roadmap.md"
mv "docs/plans/2026-09-10-libertyrecomp-postfx-port.md"  "docs/archive/docs/plans/2026-09-10-libertyrecomp-postfx-port.md"
mv "docs/plans/2026-10-08-burst-waves-execution.md"      "docs/archive/docs/plans/2026-10-08-burst-waves-execution.md"
mv "docs/plans/2026-10-08-iv-v-graphics-physics-port.md" "docs/archive/docs/plans/2026-10-08-iv-v-graphics-physics-port.md"
mv "docs/plans/2026-10-08-restructure-2026-10-08.md"     "docs/archive/docs/plans/2026-10-08-restructure-2026-10-08.md"
mv "docs/plans/2026-10-17-systematic-dig.md"             "docs/archive/docs/plans/2026-10-17-systematic-dig.md"
mv "docs/plans/2026-10-20-video-evidence-wave3-plan.md"  "docs/archive/docs/plans/2026-10-20-video-evidence-wave3-plan.md"
mv "docs/plans/2026-10-24-screenshot-catalogue-2104.md"  "docs/archive/docs/plans/2026-10-24-screenshot-catalogue-2104.md"
mv "docs/plans/2026-10-24-skills-research.md"            "docs/archive/docs/plans/2026-10-24-skills-research.md"
mv "docs/plans/questionnaire.md"                         "docs/archive/docs/plans/questionnaire.md"
mv "docs/plans/swarm-deploy-2026-10-01.md"               "docs/archive/docs/plans/swarm-deploy-2026-10-01.md"

# --- reports / research / superpowers (4) ---------------------------------
mkdir -p docs/archive/docs/reports docs/archive/docs/research/youtube docs/archive/docs/superpowers/plans
mv "docs/reports/2026-10-24-sadie-reindex.md" "docs/archive/docs/reports/2026-10-24-sadie-reindex.md"
mv "docs/research/youtube/Exploring-a-New-Approach-to-Realistic-Lighting-Radiance-Casc.md" "docs/archive/docs/research/youtube/Exploring-a-New-Approach-to-Realistic-Lighting-Radiance-Casc.md"
mv "docs/superpowers/plans/2026-08-07-smaa-crash-fix.md" "docs/archive/docs/superpowers/plans/2026-08-07-smaa-crash-fix.md"
mv "docs/superpowers/plans/2026-09-08-vehicle-reflection-fix.md" "docs/archive/docs/superpowers/plans/2026-09-08-vehicle-reflection-fix.md"

# --- root (4) --------------------------------------------------------------
mv "DRIFT_RECONCILIATION_REPORT_2026-09-14.md" "docs/archive/DRIFT_RECONCILIATION_REPORT_2026-09-14.md"
mv "PLAN.md"                                   "docs/archive/PLAN.md"
mv "TODO_PBR_NullFrame_Crash_2026-07-15.md"    "docs/archive/TODO_PBR_NullFrame_Crash_2026-07-15.md"
mv "WORKFLOW_PLAN.md"                          "docs/archive/WORKFLOW_PLAN.md"

# --- .serena (12) ----------------------------------------------------------
mkdir -p docs/archive/.serena/memories
mv ".serena/memories/crash-recovery-checkpoint-2026-10-20.md" "docs/archive/.serena/memories/crash-recovery-checkpoint-2026-10-20.md"
mv ".serena/memories/occluder-round-2026-10-01.md"            "docs/archive/.serena/memories/occluder-round-2026-10-01.md"
mv ".serena/memories/project-status.md"                       "docs/archive/.serena/memories/project-status.md"
mv ".serena/memories/round5-bugs-2026-10-08.md"               "docs/archive/.serena/memories/round5-bugs-2026-10-08.md"
mv ".serena/memories/round5-consolidated-2026-10-08.md"       "docs/archive/.serena/memories/round5-consolidated-2026-10-08.md"
mv ".serena/memories/round6-directives-2026-10-08.md"         "docs/archive/.serena/memories/round6-directives-2026-10-08.md"
mv ".serena/memories/swarm-round1-2026-10-01.md"              "docs/archive/.serena/memories/swarm-round1-2026-10-01.md"
mv ".serena/memories/swarm-round2-2026-10-01.md"              "docs/archive/.serena/memories/swarm-round2-2026-10-01.md"
mv ".serena/memories/swarm-round3-2026-10-01.md"              "docs/archive/.serena/memories/swarm-round3-2026-10-01.md"
mv ".serena/memories/tool-recovery-2026-10-07.md"             "docs/archive/.serena/memories/tool-recovery-2026-10-07.md"
mv ".serena/memories/wave-12lane.md"                          "docs/archive/.serena/memories/wave-12lane.md"
mv ".serena/memories/wave3-fixes-and-effects-roadmap.md"      "docs/archive/.serena/memories/wave3-fixes-and-effects-roadmap.md"

# --- .opencode memory bank (1) --------------------------------------------
mkdir -p docs/archive/.opencode/memory-bank
mv ".opencode/memory-bank/activeContext.md" "docs/archive/.opencode/memory-bank/activeContext.md"
```

## 4.1b Absorbed sources — 126 files (1,611,664 B / 1,574.0 KB)

These are the MERGE-INTO (111) and SPLIT (15) files. They are **not** archived at the same time as §4.1: each moves only after its destination is signed off (§4.4). Destination for every row is the same relative path under `docs/archive/` (e.g. `docs/INI Configuration.md` → `docs/archive/docs/INI Configuration.md`; `.serena/memories/project.md` → `docs/archive/.serena/memories/project.md`).

| archive after | source files (each → `docs/archive/<same path>`) |
|---|---|
| `AGENTS.md` | `.serena/memories/agent-skills.md` ; `.serena/memories/agent-tiers.md` ; `.serena/memories/free-agent-swarm.md` ; `.serena/memories/project.md` ; `.serena/memories/session-rules.md` |
| `README.md` | `docs/00 - Index.md` ; `docs/Credits.md` ; `docs/obsidian/Credits.md` ; `docs/obsidian/Home.md` ; `docs/obsidian/Index.md` ; `docs/obsidian/Modding/Wiki/README.md` ; `docs/obsidian-vault/INDEX.md` ; `docs/obsidian-vault/00-Index/Home.md` ; `CREDITS.md` ; `.opencode/memory-bank/productContext.md` |
| `docs/ROADMAP.md` | `docs/Future Features.md` ; `docs/Roadmap to Ultimate Mod.md` ; `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md` ; `FUTURE_FEATURES.md` ; `TODO.md` |
| `docs/architecture/ARCHITECTURE.md` | `docs/Feature Hook Pattern.md` ; `docs/File Inventory.md` ; `docs/RW SDK Integration Strategy.md` ; `docs/RW SDK Reference.md` ; `docs/RW_SDK_REFERENCE.md` ; `docs/obsidian/Hook Architecture.md` ; `docs/obsidian/RenderWare SDK Architecture.md` ; `docs/obsidian/Hardware/Overview.md` ; `docs/obsidian-vault/SkyGFX-Pipeline/SkyGFX Pipeline Overview.md` ; `docs/obsidian-vault/SkyGFX-Pipeline/Three Codebase Comparison.md` ; `docs/plans/2026-10-24-code-inventory.md` ; `docs/plans/2026-10-24-gtasource-render-core.md` ; `CODEBASE_DOCUMENTATION.md` ; `.serena/memories/render-pipes.md` ; `.serena/memories/vault-refs.md` ; `.opencode/memory-bank/systemPatterns.md` |
| `docs/guides/BUILD-DEPLOY.md` | `docs/Build System.md` ; `docs/DXVK Compatibility.md` ; `docs/Project Paths.md` ; `docs/SDK Dependencies.md` ; `docs/helper_tex.md` ; `docs/obsidian/Build Guide.md` ; `docs/obsidian/Build/Build Scripts.md` ; `docs/obsidian-vault/08-Build-Deploy/Build System.md` ; `docs/obsidian-vault/08-Build-Deploy/SDK Dependencies.md` ; `DEPENDENCIES.md` ; `.serena/memories/build-process.md` ; `.opencode/memory-bank/techContext.md` |
| `docs/guides/DEBUGGING.md` | `docs/Debug Tools.md` ; `docs/obsidian/Crash Analysis Reference.md` ; `docs/obsidian/Debugging Guide.md` ; `docs/plans/2026-10-25-debug-strategy-parity.md` ; `.serena/memories/crash-0x7618F5.md` ; `.serena/memories/crash-0x7FAD4D.md` ; `.serena/memories/crash-context.md` ; `.serena/memories/debugging.md` |
| `docs/history/TIMELINE.md` | `docs/Project Lineage.md` ; `OBSIDIAN_CODEBASE_DOCUMENTATION.md` ; `.opencode/memory-bank/history.md` ; `.opencode/memory-bank/progress.md` |
| `docs/modes/FEATURE-MATRIX.md` | `docs/Implemented Features.md` ; `docs/obsidian/Pipelines/Overview.md` |
| `docs/modes/MODE-GUIDE.md` | `docs/GTA IV Mode.md` ; `docs/obsidian/PostEffects/GTA IV Mode.md` |
| `docs/reference/ALPHA-TEXALPHA.md` | `docs/plans/2026-10-24-recovered-lane-findings.md` ; `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md` |
| `docs/reference/BUILDING-PIPE.md` | `docs/Building Pipeline.md` ; `docs/obsidian/Pipelines/Building Pipelines.md` ; `docs/obsidian/Pipelines/Grass Rendering.md` |
| `docs/reference/CORONA-LIGHTS.md` | `docs/plans/2026-10-24-corona-blackquad-diagnosis.md` |
| `docs/reference/IMGUI-MENU.md` | `docs/plans/2026-10-24-imgui-aero-theme-research.md` ; `docs/plans/effects-menu-overhaul.md` |
| `docs/reference/INI-REFERENCE.md` | `docs/Backwards Compatibility.md` ; `docs/INI Configuration.md` ; `docs/obsidian/Configuration Reference.md` |
| `docs/reference/LIGHTING-FORWARD-PLUS.md` | `docs/BRDF Reference.md` ; `docs/Unified Pipeline.md` ; `docs/Weather Timecycle.md` ; `docs/obsidian/Forward Plus Rendering.md` ; `docs/obsidian/Normal Map Plugin.md` ; `docs/obsidian/BRDF_References/PBR_BRDF_Reference.md` ; `docs/obsidian-vault/06-Technical-Decisions/IBL Env Map Decision.md` ; `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md` |
| `docs/reference/NOISE.md` | `docs/plans/2026-10-25-noise-system-audit.md` |
| `docs/reference/POSTFX.md` | `docs/Color Grading.md` ; `docs/Lens Effects.md` ; `docs/Night and Infrared Vision.md` ; `docs/PostFX Pipeline.md` ; `docs/Radiosity.md` ; `docs/obsidian/PostEffects/Colour Filter.md` ; `docs/obsidian/PostEffects/Overview.md` ; `docs/obsidian/PostEffects/Radiosity.md` ; `docs/obsidian-vault/AntiAliasing/SMAA Enhanced Subpixel Morphological AA.md` |
| `docs/reference/SHADERS-RESOURCES.md` | `docs/PBR Common.md` ; `docs/VehiclePBR Modern.md` ; `docs/obsidian/D3D9 Shader Architecture.md` ; `docs/obsidian/Shaders/Shader Reference.md` ; `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md` ; `.serena/memories/shader-pipeline.md` |
| `docs/reference/VEHICLE-PIPE.md` | `docs/Glass Shader.md` ; `docs/Leeds Vehicle Pipes.md` ; `docs/Neo Vehicle Pipe.md` ; `docs/Vehicle Classification.md` ; `docs/Vehicle Pipeline.md` ; `docs/obsidian/Pipelines/Vehicle Pipelines.md` ; `docs/obsidian-vault/06-Technical-Decisions/Rubber Shader Merge.md` ; `gta_sa_vehicle_paint_classification.md` |
| `docs/reference/WATER.md` | `docs/Water Rendering.md` ; `docs/plans/2026-10-24-water-tier0-diagnosis.md` ; `docs/plans/2026-10-24-water-veil-diagnosis.md` ; `docs/plans/water-refs.md` |

**SPLIT sources (15) — archive only after every target is signed off**

| source | targets |
|---|---|
| `docs/Shader Architecture.md` | `docs/reference/SHADERS-RESOURCES.md` + `docs/guides/BUILD-DEPLOY.md` |
| `docs/SkyGFX_Living_Systems_Database.md` | `docs/reference/VEHICLE-PIPE.md` + `docs/ROADMAP.md` |
| `docs/Three-Codebase Comparison.md` | `docs/architecture/ARCHITECTURE.md` + `docs/history/TIMELINE.md` |
| `docs/test-plan.md` | `docs/modes/MODE-GUIDE.md` + `docs/modes/FEATURE-MATRIX.md` |
| `docs/obsidian/Hardware Differences.md` | `docs/reference/ALPHA-TEXALPHA.md` + `docs/architecture/ARCHITECTURE.md` |
| `docs/plans/2026-10-21-alpha-reversal-findings.md` | `docs/reference/ALPHA-TEXALPHA.md` + `docs/reference/WATER.md` |
| `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` | `docs/modes/MODE-GUIDE.md` + `docs/reference/VEHICLE-PIPE.md` |
| `docs/plans/2026-10-24-gtasource-pipes.md` | `docs/reference/BUILDING-PIPE.md` + `docs/reference/VEHICLE-PIPE.md` |
| `docs/plans/2026-10-24-imgui-settings-coverage.md` | `docs/reference/IMGUI-MENU.md` + `docs/reference/INI-REFERENCE.md` |
| `docs/plans/2026-10-24-legacy-parity-matrix.md` | `docs/modes/FEATURE-MATRIX.md` + `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` | `docs/modes/MODE-GUIDE.md` + `docs/modes/FEATURE-MATRIX.md` |
| `docs/plans/2026-10-24-mode-preset-architecture.md` | `docs/modes/MODE-GUIDE.md` + `docs/architecture/ARCHITECTURE.md` + `docs/reference/INI-REFERENCE.md` |
| `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` | `docs/reference/LIGHTING-FORWARD-PLUS.md` + `docs/reference/SHADOWS.md` |
| `docs/plans/2026-10-24-refactor-blueprint.md` | `docs/architecture/ARCHITECTURE.md` + `docs/reference/ALPHA-TEXALPHA.md` |
| `docs/plans/README.md` | `docs/history/TIMELINE.md` + `docs/guides/DEBUGGING.md` |

## 4.2 Delete — 4 files (8,378 B / 8.2 KB)

| ID | file | bytes | justification |
|---|---|---:|---|
| D1 | `docs/obsidian/Welcome.md` | 46 | Single line: "See [[Home]] for the main documentation index." Zero unique content; the Home/Index nav is absorbed into `README.md`. |
| D2 | `BUILD_COMPLETE.md` | 1,845 | Transient build-ready note. It claims `src/normmap_stubs.cpp`, `src/dk_normal_fix.cpp`, `src/normalmap_plugin.cpp`, and `bin/Release/skygfx.dll` exist; all four are absent. Superseded by `docs/guides/BUILD-DEPLOY.md`. |
| D3 | `COMMIT_MESSAGES.md` | 4,224 | A pool of Skrillex lyrics for commit messages. Not documentation; contains zero product facts. |
| D4 | `IMPLEMENTATION_COMPLETE.md` | 2,263 | Transient "normal map integration complete" note. References the same three missing source files and the missing `bin/Release` path, and claims completion of work whose files do not exist. Superseded by `docs/guides/BUILD-DEPLOY.md`. |

Everything else earns ARCHIVE because it contains at least one unique measured fact, address, field value, or dated decision. Archive is not deletion; the provenance stays searchable.

## 4.3 Rewrite in place — 2 files

| file | operation | scope |
|---|---|---|
| `readme.md` → `README.md` | full rewrite + case-only rename (same file on NTFS) | Remove `presetFile`, the four non-existent preset INIs, `smaaPreset`, the VS2015/v140/`build\skygfx.vcxproj` build recipe, and the `bin\Release` deploy path. Add the mode table, `fast_build.py`, `build\Release\skygfx.dll` → `E:\games\gtasa_skygfx_plus\skygfx.asi`, and links to the 19 canonical docs. |
| `AGENTS.md` | surgical amendment | Fix the Mobile pipeline row (add `buildingPipe=BUILDING_XBOX`), the vehicle hook address (0x5D9FE9), the resource-ID table (confirm 238/239/240), add the mode model and `[Modes] active`, refresh the stale Sadie/Serena counts, and point at the new docs. Keep it as the agent-operations doc, not a product manual. |

No other file stays at its current path.

## 4.4 Merge operations in dependency order

Each stage must be signed off before the next begins. A SPLIT source is unlocked only when **all** of its target docs are signed off.

| stage | write first | why it must precede | sources unlocked for archive |
|---:|---|---|---|
| 0 | Freeze a copy of the current tree (`git tag docs-pre-reconcile-2026-10-25` + the archive tree) | Every later move must be reversible | none |
| 1a | `docs/reference/INI-REFERENCE.md` | Generated from `readIniFile`/`saveConfigTo`; every other doc links to it | `docs/INI Configuration.md`, `docs/obsidian/Configuration Reference.md`, `docs/Backwards Compatibility.md`, `docs/plans/2026-10-24-imgui-settings-coverage.md` (INI half), `docs/plans/2026-10-24-mode-preset-architecture.md` (INI half) |
| 1b | `docs/reference/SHADERS-RESOURCES.md` | Generated from HLSL + `resource.h`; no other doc can state registers/IDs | `docs/Shader Architecture.md`, `docs/VehiclePBR Modern.md`, `docs/PBR Common.md`, `docs/obsidian/D3D9 Shader Architecture.md`, `docs/obsidian/Shaders/Shader Reference.md`, `docs/obsidian-vault/03-Shaders/VehiclePBR Modern.md`, `.serena/memories/shader-pipeline.md` |
| 1c | `docs/modes/MODE-GUIDE.md` | Owns the mode model, precedence, and generated ownership appendix; blocks every mode claim | `docs/GTA IV Mode.md`, `docs/obsidian/PostEffects/GTA IV Mode.md`, `docs/plans/2026-10-24-mode-preset-architecture.md` (mode half), `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` (mode half), `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` (mode half), `docs/test-plan.md` (verification half) |
| 1d | `docs/modes/FEATURE-MATRIX.md` | Needs 1c's mode names/status vocabulary; blocks feature-status claims | `docs/Implemented Features.md`, `docs/obsidian/Pipelines/Overview.md`, `docs/plans/2026-10-24-legacy-parity-matrix.md` (feature half), `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` (feature half), `docs/test-plan.md` (coverage half) |
| 2 | `docs/architecture/ARCHITECTURE.md` | Needs 1a-1c for Config/hook/mode contracts; blocks all subsystem refs | `docs/obsidian/Hook Architecture.md`, `docs/plans/2026-10-24-refactor-blueprint.md` (arch half), `docs/plans/2026-10-24-mode-preset-architecture.md` (arch half), `docs/plans/2026-10-24-gtasource-render-core.md`, `docs/plans/2026-10-24-code-inventory.md`, `docs/File Inventory.md`, `docs/Feature Hook Pattern.md`, `docs/RW SDK Integration Strategy.md`, `docs/RW SDK Reference.md`, `docs/RW_SDK_REFERENCE.md`, `docs/obsidian/RenderWare SDK Architecture.md`, `docs/obsidian/Hardware/Overview.md`, `docs/obsidian/Hardware Differences.md` (platform half), `docs/obsidian-vault/SkyGFX-Pipeline/SkyGFX Pipeline Overview.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Three Codebase Comparison.md`, `docs/Three-Codebase Comparison.md` (arch half), `CODEBASE_DOCUMENTATION.md`, `.serena/memories/render-pipes.md`, `.serena/memories/vault-refs.md`, `.opencode/memory-bank/systemPatterns.md` |
| 3a | `docs/reference/BUILDING-PIPE.md` | Needs 2's hook/state contract | `docs/Building Pipeline.md`, `docs/obsidian/Pipelines/Building Pipelines.md`, `docs/obsidian/Pipelines/Grass Rendering.md`, `docs/plans/2026-10-24-gtasource-pipes.md` (building half) |
| 3b | `docs/reference/VEHICLE-PIPE.md` | Needs 2; absorbs glass/rubber/classification | `docs/Vehicle Pipeline.md`, `docs/Glass Shader.md`, `docs/Leeds Vehicle Pipes.md`, `docs/Neo Vehicle Pipe.md`, `docs/Vehicle Classification.md`, `docs/obsidian/Pipelines/Vehicle Pipelines.md`, `docs/obsidian-vault/06-Technical-Decisions/Rubber Shader Merge.md`, `gta_sa_vehicle_paint_classification.md`, `docs/plans/2026-10-24-gtasource-pipes.md` (vehicle half), `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` (vehicle half), `docs/SkyGFX_Living_Systems_Database.md` (vehicle half) |
| 3c | `docs/reference/WATER.md` | Needs 2 + mode guide (waterStyle is a mode field) | `docs/Water Rendering.md`, `docs/plans/2026-10-24-water-tier0-diagnosis.md`, `docs/plans/2026-10-24-water-veil-diagnosis.md`, `docs/plans/water-refs.md`, `docs/plans/2026-10-21-alpha-reversal-findings.md` (water half) |
| 3d | `docs/reference/POSTFX.md` | Needs 2 + 1b (SMAA/tonemap shader entries) | `docs/PostFX Pipeline.md`, `docs/Color Grading.md`, `docs/Lens Effects.md`, `docs/Night and Infrared Vision.md`, `docs/Radiosity.md`, `docs/obsidian/PostEffects/Overview.md`, `docs/obsidian/PostEffects/Colour Filter.md`, `docs/obsidian/PostEffects/Radiosity.md`, `docs/obsidian-vault/AntiAliasing/SMAA Enhanced Subpixel Morphological AA.md` |
| 3e | `docs/reference/LIGHTING-FORWARD-PLUS.md` | Needs 1b + 2 | `docs/BRDF Reference.md`, `docs/Unified Pipeline.md`, `docs/Weather Timecycle.md`, `docs/obsidian/Forward Plus Rendering.md`, `docs/obsidian/Normal Map Plugin.md`, `docs/obsidian/BRDF_References/PBR_BRDF_Reference.md`, `docs/obsidian-vault/06-Technical-Decisions/IBL Env Map Decision.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md`, `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (lighting half) |
| 3f | `docs/reference/ALPHA-TEXALPHA.md` | Needs 2 (L1/L2/L3 doctrine) | `docs/obsidian/Hardware Differences.md` (alpha half), `docs/plans/2026-10-21-alpha-reversal-findings.md` (alpha half), `docs/plans/2026-10-24-recovered-lane-findings.md`, `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md`, `docs/plans/2026-10-24-refactor-blueprint.md` (alpha half), `docs/plans/2026-10-24-legacy-parity-matrix.md` (alpha half) |
| 3g | `docs/reference/CORONA-LIGHTS.md` | Needs 2 (sprite window) | `docs/plans/2026-10-24-corona-blackquad-diagnosis.md` |
| 3h | `docs/reference/SHADOWS.md` | Needs 2 + 3e/3f | `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md` (shadow half), `docs/plans/2026-10-24-legacy-parity-matrix.md` (shadow half) |
| 3i | `docs/reference/NOISE.md` | Needs 2 | `docs/plans/2026-10-25-noise-system-audit.md` |
| 3j | `docs/reference/IMGUI-MENU.md` | Needs 1a (key list) + 1c (mode rows) | `docs/plans/2026-10-24-imgui-settings-coverage.md` (UI half), `docs/plans/2026-10-24-imgui-aero-theme-research.md`, `docs/plans/effects-menu-overhaul.md` |
| 4a | `docs/guides/BUILD-DEPLOY.md` | Needs 1b (shader build) + 1a (INI deploy) | `docs/Build System.md`, `docs/Project Paths.md`, `docs/SDK Dependencies.md`, `docs/DXVK Compatibility.md`, `docs/helper_tex.md`, `docs/obsidian/Build Guide.md`, `docs/obsidian/Build/Build Scripts.md`, `docs/obsidian-vault/08-Build-Deploy/Build System.md`, `docs/obsidian-vault/08-Build-Deploy/SDK Dependencies.md`, `DEPENDENCIES.md`, `.serena/memories/build-process.md`, `.opencode/memory-bank/techContext.md` |
| 4b | `docs/guides/DEBUGGING.md` | Needs 2 (hooks) + 3g/3h (corona/shadow) | `docs/Debug Tools.md` (log half), `docs/obsidian/Debugging Guide.md`, `docs/obsidian/Crash Analysis Reference.md`, `.serena/memories/debugging.md`, `.serena/memories/crash-0x7618F5.md`, `.serena/memories/crash-0x7FAD4D.md`, `.serena/memories/crash-context.md`, `docs/plans/2026-10-25-debug-strategy-parity.md`, `docs/plans/README.md` (resume half) |
| 5a | `docs/history/TIMELINE.md` | Needs every status claim final | `docs/Project Lineage.md`, `docs/Three-Codebase Comparison.md` (history half), `OBSIDIAN_CODEBASE_DOCUMENTATION.md`, `.opencode/memory-bank/history.md`, `.opencode/memory-bank/progress.md`, all 12 archived Serena round/wave/crash records, `docs/plans/README.md` (index half) |
| 5b | `docs/ROADMAP.md` | Reserved lane; needs 1c/1d status | `docs/Future Features.md`, `docs/Roadmap to Ultimate Mod.md`, `TODO.md`, `FUTURE_FEATURES.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md`, `docs/plans/2026-10-08-iv-v-graphics-physics-port.md`, `docs/SkyGFX_Living_Systems_Database.md` (roadmap half), `docs/wheel_*.md` |
| 6 | `README.md` + amended `AGENTS.md` | Needs all links and the final mode table | index/credits/product/agent memories: `docs/00 - Index.md`, `docs/Credits.md`, `CREDITS.md`, `docs/obsidian/{Home,Index,Credits}.md`, `docs/obsidian/Modding/Wiki/README.md`, `docs/obsidian-vault/{INDEX.md,00-Index/Home.md}`, `.opencode/memory-bank/productContext.md`, `.serena/memories/{agent-skills,agent-tiers,free-agent-swarm,project,session-rules}.md` |
| 7 | Link-check + `git status` + archive README | Prevents dangling `[[wikilinks]]` | delete D1-D4 |

**Ordering rule for SPLIT files:** archive only after the last target is signed off. Example: `docs/plans/2026-10-24-mode-preset-architecture.md` stays live until MODE-GUIDE, ARCHITECTURE, and INI-REFERENCE are all written; `docs/plans/2026-10-24-gtasource-pipes.md` stays until BUILDING-PIPE and VEHICLE-PIPE are both written.

## 4.5 Byte reduction

| bucket | files | bytes | KB | note |
|---|---:|---:|---:|---|
| Active before | 199 | 2,275,114 | 2,221.8 | measured |
| DELETE | 4 | 8,378 | 8.2 | only immediate physical reduction |
| ARCHIVE (pure) | 67 | 636,612 | 621.7 | provenance retained |
| MERGE-INTO | 111 | 987,369 | 964.2 | absorbed into canonical docs, original archived |
| SPLIT | 15 | 624,295 | 609.7 | absorbed into 2-3 docs each, original archived |
| CANONICAL kept | 2 | 18,460 | 18.0 | `README.md` + `AGENTS.md` |
| **Target canonical docs (20, incl. README rewrite)** | **20** | **280,000-360,000** | **273-352** | budget, not measured yet |
| **Active after (estimated)** | **21** | **298,460-378,460** | **291-370** | 84-87% smaller active surface |
| Archive retained | 193 | 2,248,276 | 2,195.6 | archive + merged/split originals |

- **Active-surface reduction:** 2,275,114 → ~300-380 KB = **83-87%**. This is the number that matters for daily reading/search/agent context.
- **Immediate physical disk reduction:** 8,378 B (0.37%) if the archive is kept.
- **Physical reduction if the archive is purged after one release:** 2,275,114 → ~300-380 KB = **83-87%** (the same range). Do not purge until the canonical docs have survived one verification cycle.
- Archiving is not free disk-wise: retained archive + new canonical docs makes total disk ~2.55 MB (+12%) until the archive is purged. The benefit is a 6-7× smaller live surface and one authoritative location per fact.

---

# Part 5 — Mode-oriented gap list

This is the answer to the user's actual goal. The current docs explain pipelines, but they do not explain **modes** as a switching system. The source already has a working single-active-mode manager; the documentation has not caught up.

## 5.1 What each mode actually owns (source snapshot)

Authority: `g_modeProfiles` + `kValues*` in `src/core/modeManager.cpp:75-537`. `inherit` means the field is **not** written by that profile; the value comes from the per-config-slot pre-mode base snapshot (`ApplyMode` at `:657-718`).

| Config field | PS2 | Xbox | Fixed PC | GTA IV | GTA V | Modern | Mobile |
|---|---|---|---|---|---|---|---|
| `pipeline` | PS2 | Xbox | Xbox | GTAIV | inherit | PBR | Mobile |
| `buildingPipe` | PS2 | Xbox | Xbox | GTAIV | inherit | PBR | Xbox |
| `vehiclePipe` | PS2 | Xbox | PC | GTAIV | inherit | Modern | Mobile |
| `colorFilter` | PS2 | PC | PC | GTAIV | inherit | Modern | inherit |
| `preset` | SA_PS2 | SA_Xbox | SA_PC | IV_PC | inherit | ULTRAMAX | inherit |
| `ivMode` | 0 | 0 | 0 | 1 | inherit | 0 | inherit |
| `ps2ModulateBuilding` | 1 | 0 | 0 | 0 | inherit | 0 | inherit |
| `detailMaps` | 0 | 1 | 1 | 0 | inherit | 1 | inherit |
| `stochastic` | 0 | 1 | 1 | 0 | inherit | 1 | inherit |
| `dualPassBuilding` | 0 | 1 | 1 | 0 | inherit | 1 | inherit |
| `usePCTimecyc` | 0 | 1 | 1 | inherit | inherit | 1 | inherit |
| `dualPassVehicle` | 0 | 1 | 1 | 0 | inherit | 1 | inherit |
| `envMapSize` | 256 | 256 | 256 | 256 | inherit | 512 | inherit |
| `envMapFarClipMult` | 1.0 | 1.0 | 1.0 | 1.0 | inherit | 2.0 | inherit |
| `waterStyle` | 0 (Xbox) | 0 (Xbox) | inherit | inherit | 2 (V) | inherit | inherit |
| `waterQuality` | inherit | inherit | inherit | inherit | 3 (Ultra) | inherit | inherit |
| `radiosity` | 0 | 1 | 1 | 0 | inherit | 1 | inherit |
| `doRadiosity` | 1 | 1 | 1 | 0 | inherit | 1 | inherit |
| `grainFilter` | 0 | 0 | 1 | 0 | inherit | 1 | inherit |
| `ssaoEnable` | 0 | 0 | 0 | 0 | inherit | 1 | inherit |
| `smaaEnable` | 0 | 0 | 0 | 0 | inherit | 1 | inherit |
| `motionBlurEnable` | 0 | 0 | 0 | **inherit** | inherit | 1 | inherit |
| `sssPostProcessEnable` | 0 | 0 | 0 | **inherit** | inherit | 1 | inherit |
| `normalMapEnable` | 0 | 0 | **inherit** | **inherit** | inherit | **inherit** | inherit |
| `forwardPlusEnable` | 0 | 0 | 0 | **inherit** | inherit | 1 | inherit |
| `normalBufferEnable` | 0 | 0 | inherit | inherit | inherit | 1 | inherit |
| `depthHookEnable` | 1 | 1 | 1 | inherit | inherit | 1 | inherit |
| `colorFilterEnable` | 1 | 1 | **inherit** | **inherit** | inherit | **inherit** | inherit |
| `rgb1Mult` / `rgb2Mult` / `bYCbCrFilter` | 1.0/1.0/false | 1.0/1.0/false | inherit | inherit | inherit | inherit | inherit |
| `sunCoronaIntensity` … `sunStreakSize` | 1.0 each | 1.0 each | inherit | inherit | inherit | inherit | inherit |
| `pedShadows` / `stencilShadows` | -1/-1 | -1/-1 | -1/-1 | inherit | inherit | inherit | inherit |
| safety four (`stateBaselineEnable`, `texAlphaClassify`, `pedOpaqueContract`, `defaultCbShaderHygiene`) | 1 | 1 | 1 | 1 | inherit | 1 | inherit |

Field counts: **PS2 77, Xbox 77, Fixed PC 42, GTA IV 35, GTA V 2, Modern 80, Mobile 3, Custom 0, None 0** (`kValues*` at `modeManager.cpp:75-523`; `g_modeProfiles` at `:527-537`).

**The important consequence:** a mode is not a complete preset. Unowned fields survive from the base. Entering GTA V from a Modern base leaves PBR pipes and every Modern postFX on; only the water style changes. Entering GTA IV from Modern leaves motion blur, SSS, skin/hair/vegetation enhancement, normal maps, Forward+, and the normal buffer on unless another writer turns them off. Entering Fixed PC from Modern leaves `normalMapEnable` and `colorFilterEnable` at the Modern values. None of this is stated in any current doc.

## 5.2 Modes with no documentation at all

| mode | current documentation | gap |
|---|---|---|
| **GTA V** | **None.** The "GTA V" hits in `docs/` are unrelated "GTA V-style" sky/weather/menu prose (`docs/Weather Timecycle.md`, `docs/obsidian-vault/SkyGFX-Pipeline/Weather System Architecture.md`, `docs/Implemented Features.md:62`, `docs/Roadmap to Ultimate Mod.md:4,28-29`), not the mode. | No file states that the GTA V mode is **water-only** (2 owned fields), that it does not change pipes, or what it inherits. |
| **Fixed PC / Legacy PC** | No standalone doc. Pre-mode prose treats "PC" as a vehicle pipe and "PC/Xbox" as one building pipe (`docs/obsidian/Pipelines/Building Pipelines.md:13-20`; `docs/obsidian/Pipelines/Vehicle Pipelines.md:12-18`). | No statement that Fixed PC = `BUILDING_XBOX` + `CAR_PC` + `COLORFILTER_PC`, 42 owned fields, or which 42. |
| **Modern** | No user-facing mode doc. Shader docs describe PBR; `docs/plans/2026-10-24-mode-preset-architecture.md` §3.6 is a design table, not a user guide. | No "what Modern changes / does not change / how to switch / verify" doc. |
| **Custom** | None. | No explanation that Custom owns zero fields and only snapshots/restores the per-slot base. |
| **None** | None. | No explanation that `None` is the legacy path, that top-level keys are authoritative, and that the mode latch is cleared. |
| **Mobile** | Only legacy pipeline tables (`AGENTS.md:44`, `docs/obsidian/Pipelines/*`, `docs/File Inventory.md:76-77`). | No statement that Mobile is a hidden legacy alias (`MODE_MOBILE`, `modeManager.h:35`) with 3 owned fields. |
| **PS2** | Pipeline prose only (`docs/Building Pipeline.md`, `docs/Vehicle Pipeline.md`, `docs/obsidian/Pipelines/Overview.md`). | No mode doc with owned/inherited fields, switch, gaps, or verification. |
| **Xbox** | Pipeline prose only, same files. | Same gap. |
| **GTA IV** | Two stale docs: `docs/GTA IV Mode.md` and `docs/obsidian/PostEffects/GTA IV Mode.md`. | They contradict each other on vehicle pipe and grade defaults (Part 2 rows 18-19), and describe `ivMode` as the selector, not `[Modes] active=GTAIV`. |

## 5.3 Docs that still describe the pre-mode architecture

These are not "wrong history"; they are wrong **as current guidance**:

| doc | pre-mode statement | line |
|---|---|---|
| `readme.md` | `presetFile=custom`; four shipped style INIs; no modes | `:49-52,67` |
| `docs/INI Configuration.md` | Selection = `pipeline` + `qualityPreset` + per-pipe overrides; no `[Modes]` | `:15-39` |
| `docs/obsidian/Configuration Reference.md` | Same pipeline-only model; no modes | `:15-90` |
| `docs/Backwards Compatibility.md` | Key matrix with no `[Modes]` section; `smaa*` listed active | `:60-105` |
| `docs/GTA IV Mode.md` | `ivMode=1` is the selector; `vehiclePipe=CAR_NEO` | `:7-14` |
| `docs/obsidian/PostEffects/GTA IV Mode.md` | `ivMode=1` only; no mode manager | `:3` |
| `docs/Implemented Features.md` | Global feature list, no per-mode status | whole file |
| `docs/obsidian/Pipelines/Overview.md` | Pipeline matrix, no modes | `:5-20` |
| `docs/obsidian/Pipelines/Building Pipelines.md` | "PC/Xbox" as one pipeline; `tagsBuildingPipe` as a key | `:13-20,48-49` |
| `docs/obsidian/Pipelines/Vehicle Pipelines.md` | No Modern; `Spec` alias | `:12-66` |
| `docs/File Inventory.md` | `ps2_mode.cpp` / `xbox_mode.cpp` listed as live preset implementations | `:76-77` |
| `docs/Three-Codebase Comparison.md` | Same dead preset files listed as live | `:91-92` |
| `docs/plans/README.md` | Calls the 2026-10-08 IV/V master plan **ACTIVE**, and lists the superseded roadmap as the successor chain | `:11-15` |
| `TODO.md` | Status list predates the mode manager and the Oct-24 waves | whole file |
| `.serena/memories/project.md` | Pipeline map only; Mobile building/filter omitted | `:8-12` |
| `.opencode/memory-bank/systemPatterns.md` | Pipeline map only; no modes; c24 claim wrong | `:8-12,21` |

**The three competing preset systems are still live in source**, which is why these docs cannot simply be patched:

1. `ApplyPreset` / `PresetConfig` — 15 presets, still called by the Style combo and four buttons; writes `colorFilter`, feature toggles, `ivMode`, and calls `ModeManager_ClearLatch()` (`src/core/presets.cpp:25-158`; `debugmenu_ui.cpp:2714-2775`).
2. `ModeManager` — the six-mode + Custom profile system (`src/core/modeManager.cpp:527-537`; `ApplyMode` `:657-718`).
3. Load-time `qualityPreset` + `pipeline` block — still writes pipes for tiers 0-2 (`src/core/main.cpp:5220-5379`; writes at `:5230-5233,5261-5263,5283-5285`).

Plus the dead fourth system: `src/render/ps2_mode.cpp`, `xbox_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`, `custom_mode.cpp` have **no call sites** (grep over `src/` finds only their definitions and the declarations at `src/skygfx.h:1463`). `docs/File Inventory.md:76-77` and `docs/Three-Codebase Comparison.md:91-92` still present them as implemented preset bodies.

## 5.4 Source-level mode hazards that no doc mentions

1. **GTA V is water-only.** `kValuesGTAV` has exactly two rows: `waterStyle=2`, `waterQuality=3` (`modeManager.cpp:403-406`). The file comment says "probe-gated … owns ONLY the water knobs" (`:398-402`). No user doc says this.
2. **GTA IV does not own its grade knobs.** It owns only `ivSaturation` and `ivCurves` (`:377-378`), and deliberately does **not** own `ivDesaturation`, `ivGamma`, `ivVignette*`, `ivBloomIntensity`, `ivExposure` (comment `:341-345`). A "GTA IV look" therefore depends on whatever base was captured. The old doc's claim that 1.0 = no desaturation is false (`main.cpp:5357`).
3. **GTA IV does not own motion blur, SSS, skin/hair/vegetation, normal map, Forward+, normal buffer, depth hook.** Those are absent from `kValuesGTAIV` (`:346-395`), so IV inherits them. Entering IV from Modern leaves them ON.
4. **Fixed PC does not own `normalMapEnable`, `colorFilterEnable`, `sunCorona*`, `rgb1/2`, `bYCbCrFilter`.** Those are absent from `kValuesPCVintage` (`:280-338`), so Fixed PC inherits them from the base. If the base has `normalMapEnable=1` (e.g. the user enabled it while in Modern/None), Fixed PC keeps it on; the profile never forces it off.
5. **No profile owns the water knobs except `waterStyle`/`waterQuality`.** `waterParallaxEnable`, `waterParallaxScale`, `waterNormalStrength`, `waterFresnelPower`, `waterSpecularPower`, `waterSpecularIntensity`, `waterUnderwaterFog`, `waterFoam*`, `waterShallow*`, `waterDeep*`, `waterTileScale`, `waterShoreFade`, `waterTranslucency`, `waterReflectionStrength`, `waterUseTimecycle`, `waterShoreFoam`, `waterBodyClassify` are inherited by **every** mode, including GTA V. Switching to GTA V changes the shader style but not the old water tuning.
6. **`pipelineOverride` is unguarded.** It rewrites `c->pipeline` before the `modePipesOwned` check (`main.cpp:5175-5177`), so it can desync the mirror even while a mode owns the pipes. The debug-menu pipeline apply paths are also still unguarded, as the source comment admits (`:5181-5184`, "pending M2").
7. **`qualityPreset` 0-2 still writes pipes when no mode is active.** The mode plan proposed removing this (E2); it is not done. Tier 0/1/2 writes at `main.cpp:5230-5233,5261-5263,5283-5285` (guarded by `!modePipesOwned`); only ULTRA is feature-only (`:5317-5320`).
8. **`ApplyPreset` clears the mode latch.** A Style combo selection while a mode is active drops `s_mode` to `MODE_NONE` without restoring the base. The preset's written fields (colorFilter, feature toggles, ivMode) win; the mode's pipe values remain but the latch is gone, so a later same-id toggle-off cannot restore the base (`presets.cpp:118-137`; `modeManager.cpp:622-637`). No doc explains this escape hatch.
9. **`[Mode.<alias>]` overrides are not implemented.** Only `[Modes] active` is read and saved (`main.cpp:5124-5151,6329-6336,6427-6431`). The plan's `[Mode.PS2]`/`schema`/`qualityPresetPipes` examples are future design (`docs/plans/2026-10-24-mode-preset-architecture.md:600-640`; source TODOs at `main.cpp:6333,6429`). Do not document them as shipped.
10. **Toggle-off vs A→B semantics.** Passing the current id (or `MODE_NONE`) restores the per-slot base; A→B rebuilds from the original base, never from A (`modeManager.h:9-16`; `modeManager.cpp:673-708`). No doc states this.
11. **Per-config-slot base.** The snapshot is keyed by the `configs[]` slot, not process-global; `setConfig()` can repoint `config` (`modeManager.cpp:28-30,582-591`). No doc states this.
12. **`tagsBuildingPipe` is written by every profile but derived anyway.** `modeManager.cpp:81,183,286` writes it; `main.cpp:5647` also derives it from `buildingPipe`. Harmless redundancy, but it misleads readers into thinking the key is configurable.
13. **Mode apply receipt.** `[Mode] apply <name> pipes=<b>/<v>/<cf> iv=<n>` is the one line that proves which profile ran (`modeManager.cpp:710-718`); `[Mode] active=<alias> (from INI)` is the load receipt (`main.cpp:6985-6989`). Neither is documented in any user doc.

## 5.5 Missing documentation required to make mode switching first-class

| # | missing doc/section | why it is required | acceptance test |
|---:|---|---|---|
| 1 | Per-mode **owned** field table, generated from `kValues*` | The only complete authority today is C++ member-pointer tables (`modeManager.cpp:75-523`); the plan §3 tables are design-stage and must be cross-checked row-by-row; the code is authoritative | 100% of `MV_*` rows appear; field count matches 77/77/42/35/2/80/3 |
| 2 | Per-mode **inherited** field table (the complement) | This is the actual source of "mode looks wrong when switching from X"; no doc lists it | Every Config field is either owned or explicitly inherited |
| 3 | Precedence spec for all Config writers (mode profile, `[Modes] active`, top-level INI, `ApplyPreset`, `qualityPreset`, `pipeline`, `pipelineOverride`, live menu) | Six writers exist; only the mode plan discusses them, and it is PROPOSED | A single ordered list + a "who wins" example for each pair |
| 4 | `[Modes] active` persistence + reload + multi-config behavior | Implemented (`main.cpp:5124-5151,6329-6336,6427-6431`) and undocumented | Save/reload/F11/switch-slot round-trip |
| 5 | Toggle-off, A→B, A→B→A, and Custom/None semantics | Implemented (`modeManager.h:9-16`; `modeManager.cpp:673-708`) and undocumented | Base restored byte-for-byte; user tweaks survive the run |
| 6 | GTA V mode doc (water-only + inheritance) | Zero docs; mode is 2 fields | A doc states "only water changes; pipes/postFX inherited" |
| 7 | Fixed PC mode doc (42 fields + `BUILDING_XBOX`+`CAR_PC`) | Zero standalone docs; pre-mode "PC/Xbox" prose is misleading | Doc matches `kValuesPCVintage` |
| 8 | Modern mode doc (80 fields + PBR/Forward+) | Zero user-facing docs | Doc matches `kValuesModern` |
| 9 | Custom/None doc | Zero docs; users cannot tell what "Custom" preserves | Doc explains per-slot base and latch |
| 10 | Mobile legacy alias policy | Hidden mode + 3 fields; old docs present it as a pipeline | Doc marks hidden/legacy, not a 7th peer |
| 11 | Per-mode **known gaps** | E.g. GTA IV inherits MB/SSS; GTA V inherits PBR; Fixed PC inherits normal map | Each mode section has a gap list |
| 12 | Per-mode **verification protocol** | Current test plan is global and predates modes | Log receipt + 5 owned + 5 inherited + toggle + reload + slot switch |
| 13 | Feature matrix per mode with status | `Implemented Features` is global | Matrix has all modes × all effects with evidence |
| 14 | Mode change receipts and log-tag dictionary | `[Mode] apply` is the only proof; tags are scattered | Every mode log line documented with expected values |
| 15 | Deprecation register | Dead keys/files/pipes are still documented as live | One table: `smaa*`, `enableNormalMaps`, `normalMap*` dead keys, `ps2_mode.cpp`, `xbox_mode.cpp`, `presetFile` |
| 16 | "Editing INI while a mode is active" guide | Owned keys are silently ignored (`main.cpp:110-120,5392,5410,5586`) | Doc states which keys need `active=None` first |
| 17 | Mode UI doc (F4 Modes rows, badges, reset) | UI exists (`debugmenu_ui.cpp:2683-2703`) but is undocumented | Screenshot-level walkthrough |

## 5.6 Verification protocol (per mode, to ship the guide)

Use a fixed save, time, weather, and camera position; capture the log and one screenshot per step.

1. **Base capture** — set `[Modes] active=None`; record `skygfx.ini` and `[Mode]` log; confirm the Config is the known base.
2. **Enter mode** — set `active=<mode>`; relaunch or F11; assert `[Mode] apply <name> pipes=<b>/<v>/<cf> iv=<n>` and `[Mode] active=<alias> (from INI)`.
3. **Owned check** — assert each owned field from the generated table equals the profile value. Start with pipes/filter/iv; then the mode-specific values.
4. **Inherited check** — assert 5 deliberately chosen unowned fields keep the base value (the current gaps: GTA V water knobs, IV motion blur/SSS/normal map, Fixed PC normal map/filter-enable, IV grade).
5. **Visual check** — screenshot at the same spot; compare against the archive baseline for that mode if one exists.
6. **Toggle off** — click the active mode row (or set `active=None`); assert Config is byte-identical to step 1 and the mode log says `None`.
7. **A→B** — switch PS2→Xbox→PS2 without returning to None; assert B does not stack on A and A→B→A returns the step-1 base plus A's profile.
8. **Reload** — F11; assert the mode re-applies once and no owned top-level key overwrites it.
9. **Config slot** — switch `skygfx.N.ini`; assert the base is per-slot, not global.
10. **Save/reload** — save from the menu; assert `[Modes] active` is written only when a mode or the INI named one, and re-reads to the same mode.

Any failure in steps 3-7 is a docs-vs-code contradiction that must be fixed in the guide **or** in the profile, not explained away.

## 5.7 What this means for the cleanup order

Do not start by rewriting the pipeline docs. The mode guide is the keystone:

1. Generate the ownership/inheritance appendix from `modeManager.cpp`.
2. Write MODE-GUIDE (including the precedence and hazards above).
3. Write FEATURE-MATRIX from the same tables.
4. Then the subsystem refs can say "mode-owned / mode-sensitive / mode-neutral" without guessing.
5. Only then archive the pre-mode docs. Otherwise the archive will preserve contradictions as if they were history.

---

# Appendix A — Source authority index

Use these lines when writing the canonical docs; do not re-derive from older prose.

| claim | authority |
|---|---|
| `Config` struct and field defaults | `src/skygfx.h:280-903`; `src/core/main.cpp:5106-6440` |
| `CarPipeline` / `BuildingPipeline` / `Pipeline` enums | `src/skygfx.h:168-205` |
| `COLORFILTER_*` enum | `src/skygfx.h:944-952` |
| INI read entry | `src/core/main.cpp:5106` |
| `[Modes] active` parse | `src/core/main.cpp:5124-5151` |
| `pipeline` map + `pipelineOverride` | `src/core/main.cpp:5163-5177` |
| pipeline → pipes/filter switch | `src/core/main.cpp:5185-5211` |
| `qualityPreset` defaults + pipe writes (0-2) | `src/core/main.cpp:5220-5379`; writes `:5230-5233,5261-5263,5283-5285`; ULTRA no-pipe comment `:5317-5320` |
| `buildingPipe` / `vehiclePipe` maps | `src/core/main.cpp:5384-5408` |
| `colorFilter` map + PBR force | `src/core/main.cpp:5569-5603` |
| `envFresnel` default + clamp | `src/core/main.cpp:5459`; `:403-407` |
| `tagsBuildingPipe` derivation | `src/core/main.cpp:5647` |
| `normalMapEnable` read/save | `src/core/main.cpp:5508-5510,6113-6115,6517-6519` |
| `[Modes] active` apply + save | `src/core/main.cpp:6329-6336,6427-6431` |
| mode receipts | `src/core/main.cpp:6985-6989`; `src/core/modeManager.cpp:710-718` |
| mode model / toggle / A→B | `src/core/modeManager.h:9-16`; `src/core/modeManager.cpp:657-718` |
| per-slot base | `src/core/modeManager.cpp:28-30,582-591,695-708` |
| mode profile tables | `src/core/modeManager.cpp:75-523` |
| `g_modeProfiles` | `src/core/modeManager.cpp:527-537` |
| `PresetConfig` / `ApplyPreset` | `src/core/presets.cpp:25-158` |
| Style combo / preset buttons | `src/core/debugmenu_ui.cpp:2714-2775` |
| Modes rows | `src/core/debugmenu_ui.cpp:2683-2703` |
| hook: ColourFilter | `src/core/main.cpp:7493` |
| hook: DrawFinalEffects | `src/core/main.cpp:7509` |
| hook: building pipe | `src/render/buildingPipe.cpp:3048-3054` |
| hook: vehicle pipe | `src/render/vehiclePipe.cpp:3375-3376` |
| render-state doctrine L1/L2/L3 | `docs/plans/2026-10-24-refactor-blueprint.md:39-43` |
| VS registers (building) | `shaders/vs/buildingPBRVS.hlsl:17-33` |
| VS registers (vehicle PBR) | `shaders/vehiclePipeVS.hlsl:10-16,56-58` |
| PS registers (VehiclePBR_Modern) | `shaders/ps/VehiclePBR_Modern.hlsl:18-83` |
| shader profiles / multi-entry | `tools/fast_build.py:407,419-425`; 29 `ps_3_0`/`vs_3_0`, 0 `ps_2_0` |
| resource IDs | `resources/resource.h:4-138`; `resources/resource.rc:63-100` |
| build/deploy default | `tools/fast_build.py:56,669-686`; `AGENTS.md:10-14` |

# Appendix B — Verification ledger

**Verified against source during this pass**

- All 199 files exist, sizes/mtimes measured, all opened, all H1/H2 outlines read.
- MD5 over all 152 `docs/**/*.md` files: no duplicate pairs.
- Enum values, `Config` field names used in the contradiction table, `COLORFILTER_*` values.
- INI read/save sites for the disputed keys; zero reads for `smaaPreset`, `smaaPredication`, `smaaTemporal`, `enableNormalMaps`, `normalMapBuilding`, `normalMapVehicle`, `normalMapDebug`, `normalMapDebugMode`, `presetFile`, `tagsBuildingPipe`.
- Pipeline/quality-preset/pipe-override/colorFilter/PBR-force control flow.
- Mode profile tables and counts (77/77/42/35/2/80/3).
- Hook install sites (0x704D1E, 0x53EBE9, 0x5D7100, 0x5D7D90, 0x5D7F40, 0x5D9FE9, 0x53C15E, 0x5D5B10).
- Shader profile list (29 SM3.0 entries, 0 SM2.0 entries).
- Resource IDs: 100 definitions, 0 value mismatches against the doc table, 36 doc omissions.
- Dead preset files: no call sites for `ps2_mode.cpp`, `xbox_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`, `custom_mode.cpp` outside their own definitions/declarations.
- Build/deploy target in both build scripts.

**Not verified (do not treat as settled)**

- Runtime visual correctness of any mode (no game launch in this pass).
- The live `skygfx.ini` in `E:\games\gtasa_skygfx_plus` (not read; the user's current settings are not assumed).
- Every sentence of all 152 docs; the inventory used heading outlines plus targeted reads. A file's unlisted prose may still be stale.
- Cross-vault wikilinks (`[[...]]`) and Obsidian embedding integrity.
- Whether the `docs/plans` status labels match the true implementation state beyond the source checks above; the mode plan and `qualityPresetPipes` are explicitly proposed, not shipped.

# Appendix C — "Done" checklist for the execution lane

The documentation cleanup is done when all of these are true:

1. The 20 canonical docs exist at the Part 3 paths, and every one has a one-line purpose in `README.md`.
2. `MODE-GUIDE` contains a generated ownership table whose row count equals the sum of `MV_*` entries, and whose values match `kValues*` at the current HEAD.
3. `MODE-GUIDE` states the six+Custom modes, the hidden Mobile alias, the precedence chain, the toggle/A→B semantics, and the GTA V water-only fact.
4. `FEATURE-MATRIX` uses exactly `shipped / partial / missing / N/A` and has one column per mode.
5. `INI-REFERENCE` is generated; a grep for `smaaPreset`, `enableNormalMaps`, `presetFile` returns only "dead/deprecated" rows, never an active row.
6. `SHADERS-RESOURCES` lists 100/100 `IDR_*` entries and per-entry register tables; a grep for `ps_2_0` returns only historical notes, never a current profile claim.
7. `ARCHITECTURE` contains the L1/L2/L3 table and a Config-writer precedence diagram that names `ModeManager`, `ApplyPreset`, `qualityPreset`, `pipeline`, `pipelineOverride`, and `[Modes] active`.
8. `BUILD-DEPLOY` names `fast_build.py` as default and `E:\games\gtasa_skygfx_plus\skygfx.asi` as the deploy target; no `H:\games\clean_sa`/`E:\dev(dave)\binaries` remains.
9. 67 files are under `docs/archive/` with their original relative paths; 4 files are deleted (D1-D4); `README.md` and `AGENTS.md` are the only rewritten in place.
10. `grep -rn "config.cpp\|CAR_NEO.*GTA IV\|1.0 = no desaturation\|ps2_mode.cpp.*live\|match 67 files" docs/ README.md AGENTS.md` returns no current-guidance hit.
11. `AGENTS.md` Mobile row reads `buildingPipe=BUILDING_XBOX, vehiclePipe=CAR_MOBILE`; vehicle hook reads `0x5D9FE9`.
12. A fresh `[Modes] active=PS2`, `Xbox`, `PC`, `GTAIV`, `GTAV`, `Modern` run each produces the expected `[Mode] apply` receipt and a screenshot that matches the mode's reference, with the inherited-field hazards of §5.4 either fixed in profiles or documented as known gaps.

---

**Bottom line:** the docs are not merely stale; they describe three different products (aap's pipeline selector, the abandoned per-mode preset files, and the new mode manager) as if they were one. The fastest trustworthy path is: generate `INI-REFERENCE` and `SHADERS-RESOURCES` from source, generate the mode ownership table from `kValues*`, write `MODE-GUIDE` and `FEATURE-MATRIX` around that table, then write `ARCHITECTURE` and the subsystem refs, then archive the old tree. Do not hand-merge the old prose into new docs; regenerate the facts and carry only the prose that explains *why*.

