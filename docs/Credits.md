# Credits

## Original SkyGFX
- **aap** — Author of SkyGfx ([github.com/aap/skygfx](https://github.com/aap/skygfx)). SkyGfx is aap's project; it is not a DK22Pac project.

## Core Contributors
- **Junior (JuniorDjjr)** — JuniorDjjr fork: stochastic texturing (later superseded by `noiseDetail`), wind animation, EDED plugin, texdb, extended colour filters
- **GTASAS 1987 Team** — skygfx_plus extension work recorded in project docs (lead: babamohammed2022 — UNVERIFIED)
- **DK22Pac** — Normal Map Plugin (rpNORMMAP pipeline reference)
- **Silent** — SilentPatch / SilentPatchSA compatibility
- **_AG** — RenderWare documentation

## Reference sources actually used

| Source | What was used |
|--------|---------------|
| **aap SkyGfx** | Original architecture, legacy vehicle/building pipelines, configuration model, and shader set; this fork starts from it. |
| **Rockstar GTASource leak** | GTA SA `pipes/` render core and PS2/Xbox/PC pipeline behaviour, plus shipped-but-disabled beta paths. Reference only; no Rockstar source is compiled into this project. |
| **CryEngine 3.8.6 shaders/assets** | PBR/BRDF conventions (glossiness workflow, env-map Fresnel), filmic tonemap, height fog, and water/cloud techniques. Reimplemented for SM3.0; no CryEngine code is shipped. |
| **LibertyRecomp GTA IV shader overrides** | GTA IV postFX references: ACES tonemap fit, chromatic aberration, film grain, sun shafts, water shoreline/glitter. Reimplemented for SM3.0. |
| **DK22Pac Normal Map Plugin** | rpNORMMAP pipeline hooks, MatFX material test, and the `<diffuse>_n` naming convention. Current integration: `src/texdefs.cpp` (merged NM lane + DK22Pac external plugin lane). |
| **SilentPatchSA** | Compatibility reference for corona/tail-jump deferral behaviour. No SilentPatch code is copied; it is also a recommended companion mod. |

## Community
- **The GTA Modding Community** — Reverse engineering, documentation
- **GTAForums** — Community knowledge base

## Shader Credits
- **CloudWorks** by Brian Tu (keroroxzz) — CC BY-NC-SA 3.0
- **SMAA** — Jorge Jimenez et al., University of Zaragoza (BSD licensed)

## Academic Research
- Jensen et al. (2001) — Subsurface light transport
- Walter et al. (2007) — GGX microfacet models
- Hable (Uncharted 2) — Filmic tonemapping
- Burley (Disney, 2012) — Disney BRDF

## Inspiration
- **Crytek** — CryEngine rendering techniques (SSS, GGX BRDF)
- **Rockstar Games** — GTA IV, GTA V, RDR2 rendering
- **Criterion Games** — RenderWare Graphics engine
- **O3DE Foundation** — Atom renderer PBR pipeline

## Open Source
- Dear ImGui, stb libraries, Khronos Group, Microsoft DirectX SDK

## Permissions
**No proprietary source code from Crytek, Rockstar, or any commercial entity is compiled into this project.**
aap's SkyGfx is the upstream open-source base; GTASource, CryEngine 3.8.6, LibertyRecomp, and SilentPatchSA material was used as reference and reimplemented. DK22Pac's Normal Map Plugin is credited as the rpNORMMAP-pipeline reference.
