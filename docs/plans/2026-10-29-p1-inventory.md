# P1 Inventory — GTA V (dev_ng / RAGE) source tree, vegetation focus

Date: 2026-10-29. Source: `C:\Users\aaaaaaaaa\Downloads\P1` (read-only survey).
Intent (m0968/m0969): find helpful sources to overhaul vegetation, then bring to parity in skygfx_plus.

## VERDICT

**This is the leaked GTA V "dev_ng" (RAGE engine) source tree.** It is the single most
relevant external reference possible for our vegetation lane: GTA V's `CPlantMgr` /
`CGrassRenderer` / `CGrassInstancer` are the *direct descendants* of the SA plant code
our `src/render/grasssys.{cpp,h}` already ports (`PlantsMgr.h:6` says
"initial conversion from SA code", 2005 — same lineage, one generation later).

**License: proprietary Rockstar/Take-Two (installer EULA RTFs only, no OSS license).
NO LICENSE -> reference only, no copying.** Study algorithms, register layouts and
data packing; reimplement from scratch in our style. Do not paste code.

Highest-value targets, in order:
1. `PlantsMgr` per-vertex density/scale packing + LocTri seeding math
2. `PlantsGrassRenderer` PPTriPlant layout + ground-color modulation
3. `Vegetation/Trees` wind funcs (`trees_windfuncs.fxh`, branch bend + tri-wave)
4. `TreeImposters` 16-slot RT cache model (design only — no RT budget for it yet)
5. `Vegetation/Grass` fur/LOD shell chain (concept only; SM3.0 constraints apply)

Everything else in the 202 GB (build/, tools_ng/, script/, docs/art/) is IGNORE.

## 1. Tree summary

Total: **183,622 files, 202 GB**. Single top-level dir `gta5/`.

| Path | Size | Files | What it is | Verdict |
|---|---|---|---|---|
| `gta5/src/dev_ng/` | 12 GB | 69,509 | RAGE + game C++ source (`game/`, `rage/`, `objects/`, `extra/`, `tools/`) | **PRIMARY — mine this** |
| `gta5/build/dev_ng/` | 167 GB | 50,878 | Compiled outputs: x64 (69 G), xboxone (45 G), ps4 (45 G), ELF binaries | IGNORE (binaries) |
| `gta5/docs/art/` | 14 GB | 117 | Art pipeline docs, map renders, SLOD .docx/.wmv, WebMap | Skim only (SLoD/dist tables) |
| `gta5/tools_ng/` | 9.3 GB | 57,968 | RAGE toolchain (ironlib, dcc, packages, web) | IGNORE |
| `gta5/script/dev_ng/` | 628 MB | 4,511 | GTA5_SP solution — game script missions | IGNORE (gameplay) |
| `gta5/assets_ng/` | 30 MB | 639 | metadata/definitions + workbench configs | IGNORE |

Source internals (`src/dev_ng/`): `rage/base/src/` = engine (grcore, grpostfx, phcore,
math, mesh); `game/renderer/` = render pipeline incl. ALL vegetation; `game/shader_source/`
= 656 .fx/.fxh; `game/physics/`, `rage/naturalmotion/` = Euphoria; `game/frontend/`, `rage/scaleform/` = UI.

## 2. License table

| Tree | License artifact | Type | Rule |
|---|---|---|---|
| `src/dev_ng/**` | none (only `extra/installer/**/License.rtf` = game-installer EULA) | Proprietary Rockstar/Take-Two, leaked | **Reference only — no copying** |
| `build/`, `tools_ng/`, `script/`, `assets_ng/`, `docs/` | none | Proprietary | IGNORE |

There is **no** LICENSE/COPYING granting reuse. Treat every file as read-only
reference. Algorithms + concepts (density packing, imposter caching, branch-bend
math) may be re-derived; concrete code and structs must be independently
reimplemented in skygfx style.

## 3. VEGETATION (primary) — file-by-file

All paths under `C:\Users\aaaaaaaaa\Downloads\P1\gta5\src\dev_ng\`.

### 3.1 CPU-side plant system — `game/renderer/`

| File | LOC | What it does | Maps onto |
|---|---|---|---|
| `PlantsMgr.cpp` | 9,167 | `CPlantMgr`: seeds plants on collision triangles (LocTris) around camera; per-vertex density/scaleZ/scaleXYZ packed into vertex alpha (`pv8PackDensityScaleZScaleXYZ`, PlantsMgr.h:331-340); ground-color RGB + density A; model table load (`LoadPlantModel` :835, `LoadPlantModelLOD2` :942); camera-sector update tasks; occlusion; shadow candidates | `src/render/grasssys.cpp` / `.h` — our `CPlantMgr` (grasssys.h:273) is the SA ancestor. Parity gaps: per-vertex density mapping (`pv8MapDensity`), LOD2 second model tier, ground-color modulation, occlusion update |
| `PlantsGrassRenderer.cpp` | 5,760 | `CGrassRenderer` + `CPPTriPlantBuffer`: low-level per-tri plant drawing; instanced path since 2006; HDR/deferred/GBuffer variants | `src/render/grasssys.cpp:272` `CGrassRenderer::DrawTriPlants`; buffer class `CPPTriPlantBuffer` (grasssys.h:495). Our instancer `CGrassInstancer` (grasssys.h:583, cpp:849-1100) ~ their instanced path — compare wind-bend vertex channels and seed-driven variation |
| `PlantsGrassRenderer.h` | 1,174 | `PPTriPlant` struct (:36+): V1/V2/V3 with `.w` = wind bend scale/variation, Float16 packed scale/um, flags bitset, texture ptr/ids, intensity+var, seed, coll_params | `src/render/grasssys.h:136` `struct PPTriPlant` — same struct, SA vintage. Study the extra channels (um_param UV movement, scaleRange, coll radius) as candidate extensions |
| `TreeImposters.cpp/.h` | 824+142 | `CTreeImposters`: 16-slot `ms_imposterCache` of `CImposterCacheInfo` (modelIndex, age, fade, radius, centre, `grcRenderTarget*`); LRU age-out, render-into-RT then draw as camera-facing card | No counterpart in skygfx. Design reference for a future tree-imposter lane; too big for SM3/RW3.6 now — park |
| `RenderPhases/RenderPhaseTreeImposters.cpp/.h` | 266 | Render-phase wrapper: renders trees into imposter RTs as a scene phase | Same — design only |
| `PlantsMgrUpdateSPU.cpp`, `PlantsGrassRendererSPU.cpp` | 233+1,881 | Cell SPU job split (PS3) — multi-threaded update/render | IGNORE (console SPUs); only the *split* idea (update/render double-buffer, `m_RTbufferID` PlantsMgr.h:949) noted for future MT |

### 3.2 Vegetation shaders — `game/shader_source/Vegetation/` (11,644 LOC total)

**Grass/ (6,249 LOC):**

| File | What | skygfx mapping |
|---|---|---|
| `grass.fx` | Master grass technique: LOD0/1/2, ground-color modulation, player-collision sphere scale in IN.color0.a, stipple alpha, AlphaToMask, shadow proxy | Feature checklist for `grasssys` parity: ground-color mod, per-blade collision-scale, LOD tiers |
| `grass_fur.fx`, `grass_fur_lod.fx`, `grass_fur_mask.fx`, `grass_fur_tnt.fx` | "Fur" multi-shell grass (layered shells for depth) | Concept only — shells too fill-rate-heavy for SA targets; possible experimental flag |
| `grass_batch.fx`, `grass_batch_camera_aligned.fx`, `grass_batch_camera_facing.fx` | Batched instanced grass; camera-aligned vs camera-facing blade orientation | Directly relevant to `CGrassInstancer` (grasssys.cpp:849+): compare alignment math vs our `DrawTriPlant` |
| `grass_camera_aligned.fx`, `grass_camera_facing.fx` | Non-batched single plants, same two orientations | Same |
| `grass.fxh`, `grass_globals.fxh`, `BatchCS.fxh`, `grass_regs.h` | Shared includes, register maps | Reference for constant packing conventions (not copyable) |

**Trees/ (5,395 LOC):**

| File | What | skygfx mapping |
|---|---|---|
| `trees_windfuncs.fxh` (502) | Branch-bend (2 freqs), tri-wave micro-movements, 3D wind texture (`SfxWindSampler3D`), shared const buffer `branchBendEtc_*` @c185-c190, global phase, underwater transition | **Highest-value shader reference** for `src/render/treeWind.cpp` (476 LOC; `TreeWind_ApplyToTransform` treeWind.cpp:191, `TreeWind_OnEntity`, c21-c23 contract treeWind.h:47-60). Study: 2-frequency branch bend, tri-wave phase from object pos, wind-texture-driven direction vs our scalar gust |
| `trees_normal_spec_wind.fx`, `trees_normal_spec*.fx`, `trees_normal.fx`, `trees.fx` | Progressive tree shading: diffuse -> normal -> spec -> wind | Alpha/spec/normal handling reference for our FF-tree lighting+alpha (treeWind.h lane W8-2) |
| `trees_camera_aligned.fx`, `trees_camera_facing.fx`, `billboard_*.fx` | LOD card orientation (aligned vs facing), billboard w/o bump | Design reference if we ever add tree LOD cards for SA |
| `trees_lod.fx`, `trees_lod2.fx`, `trees_lod2d.fx` | 3-tier LOD + 2D card LOD | Concept reference — our SA trees are single-tier |
| `trees_shadow_proxy.fx` | Shadow-casting proxy for trees | IGNORE (no shadow proxies in skygfx) |
| `trees_common.fxh`, `trees_shared.h`, `billboard_shared.fxh` | Shared includes | Reference |

### 3.3 Wind (game-side) — `game/game/wind.cpp/.h` (711+137)

`CWind`: global wind + disturbance volumes — downwash (heli), explosions,
hemispheres, spheres, thermals (wind.h:20-32 max counts). Drives plant/tree sway input.
skygfx mapping: our wind is `CWeather::Wind`-scalar only (treeWind.h:51 `windIntensity =
2.0 + 1.5 * CWeather::Wind`). **Parity candidate (low priority):** gust envelopes +
local disturbance falloff could feed the existing `c22/c23` contract without new
registers. Do NOT port the volume system — SA has no thermals/downwash need.

### 3.4 Custom shader hook

`game/shaders/CustomShaderEffectGrass.cpp/.h` — game-side custom effect override for
grass. Reference for how RAGE injects custom passes; skygfx already replaces pipes, IGNORE.

## 4. Secondary flags (not vegetation)

| Area | Key files (under `src/dev_ng/`) | LOC | Note for us |
|---|---|---|---|
| **Water** | `game/renderer/Water.cpp` (11,225), `newWater.cpp` (3,245), `River.*`, `RenderPhases/RenderPhaseWater*.{cpp,h}`; `game/shader_source/Water/*.fx` (16 files) | ~15k+ | Our `src/render/waterPipe.cpp` is SA fixed-function water. Foam/river/shallow separation = design ideas only. MEDIUM value |
| **Clouds/noise** | `game/shader_source/Sky/Clouds/*.fx` (billboard_fast/soft, anim, altitude, fog); `sky_system.fx` | few k | Our PBR sky/cloud shadow comes from `PBR_Common.hlsl`. Cloud billboard layering = LOW-MED value reference |
| **Motion blur** | `rage/base/src/grpostfx/motionblur.cpp` (58 — stub, SPU frag does work), `edge/post/edgepost_motionblur*.cpp`; `game/renderer/PostScan.*` | small | Our `src/render/postfx_motionblur.cpp` already implements camera+entity MB. Reference for velocity-buffer approach only |
| **Destruction/physics** | `game/physics/Breakable.cpp` (816), `Deformable.cpp`, `Rope.*`, `WorldProbe/`; `rage/naturalmotion/` (Euphoria) | large | `sftbdyphsx` lane (softbody) is ours and unrelated. Euphoria = IGNORE entirely. Breakable/deform = IGNORE (no RW equivalent in scope) |
| **Ragdoll** | `game/physics/RagdollConstraints.cpp` (264), `rage/cranimation/`, `pharticulated/` | large | IGNORE |
| **PostFX chain** | `rage/base/src/grpostfx/*`, `game/renderer/PostProcessFX.*`, `SSAO.*`, `MLAA.*`, `AdaptiveDOF.*` | large | Our postfx.cpp chain (SSAO/SMAA/colour-filter) already covers the same ground with SM3 constraints. LOW value |
| **UI** | `game/frontend/**` (.cpp/.psc page system), `rage/scaleform/` | huge | IGNORE — C++ ASI has no UI framework need |
| **Rendering core** | `game/renderer/Renderer.*`, `RenderListBuilder.*`, `RenderTargetMgr.*`, `RenderPhases/` (19 phases) | large | Architecture reference only; RAGE deferred+forward hybrid != RW 3.6 |

## 5. Bring-to-parity recommendations (ranked value/effort)

Rule for all: **reimplement, don't copy** (proprietary, no license). Use P1 to
confirm *what* the descendant-of-SA code does; write it fresh in skygfx style/SM3.0.

| # | Item | Value | Effort | Sketch |
|---|---|---|---|---|
| P1 | **Per-vertex density + scale packing** (PlantsMgr.h:264-343, `pv8MapDensity`) | HIGH | MED | Our LocTri path seeds uniform-density plants. Adopt their 2-bit-density / packed scaleZ / scaleXYZ scheme on `CPlantLocTri` (grasssys.h:225) vertex color alpha, map through a 4-entry density table. Touches `CPlantMgr` update + `CGrassRenderer::DrawTriPlants` (grasssys.cpp:272). Config flag near `Config::grassInstancing` (skygfx.h:561) |
| P2 | **Wind bend per-vertex channels** (PPTriPlant V1.w/V2.w, PlantsGrassRenderer.h:36) | HIGH | LOW | Our PPTriPlant already mirrors the SA struct (grasssys.h:136). Verify V1.w/V2.w (bend scale/variation) are actually consumed by our instancer path (`CGrassInstancer::AddInstance` grasssys.cpp:902); if not, wire through to treeWind c23 contract (treeWind.h:52-60) |
| P3 | **2-frequency branch bend + tri-wave** (trees_windfuncs.fxh) | HIGH | MED | Extend `treeWind.cpp` CPU tilt (`TreeWind_ApplyToTransform` :191) with their dual-frequency decomposition; optionally add tri-wave micro-motion driven by object pos (no new sampler — SM3.0 has no s3D wind texture; fake with sin product). Keep c21-c23 contract stable or extend via unused c23 lanes |
| P4 | **Ground-color modulation** (grass.fx 2008; PlantsMgr ground RGB) | MED | MED | Sample ground texel under plant, tint instance colour. Needs a ground albedo fetch in the plant draw path — expensive on RW; try update-thread only (sample while LocTri is built), store per-tri average colour |
| P5 | **LOD1/LOD2 second model tier** (`LoadPlantModelLOD2` PlantsMgr.cpp:942, grass.fx LOD2) | MED | MED | Our `m_PlantModelsTab` is single-tier. Add a second model slot per plant set + distance switch in `CGrassInstancer` batches |
| P6 | **Camera-aligned vs camera-facing choice** (grass_batch_camera_*.fx) | MED | LOW | Confirm our instancer blade orientation math against both variants; expose INI toggle if not already |
| P7 | **Gust envelope from wind.cpp** | LOW | LOW | Replace linear `CWeather::Wind` scaling with a slow gust LFO feeding c22; no volume system |
| P8 | **Imposter cache design** (TreeImposters 16-slot RT LRU) | LOW (now) | HIGH | Park. Documented for a future SA tree-imposter experiment; RW 3.6 RT plumbing not in place |

## 6. IGNORE list

- `build/` (167 GB compiled console/PC outputs, ELF binaries) — delete candidate from disk
- `tools_ng/` (9.3 GB RAGE toolchain), `script/` (gameplay scripts), `assets_ng/`
- `docs/art/` (14 GB art pipeline media; only SLoD distance `.xlsx` is glance-worthy)
- Euphoria (`rage/naturalmotion/`), all of `game/physics/` except a glance at Breakable design
- `game/frontend/` + `rage/scaleform/` (UI), `game/network/`, `game/audio/`, `game/Peds/`, `game/Vehicles/`
- All SPU/Cell code (`*SPU*`, `.job`, `.frag`) — PS3-only
- `game/extra/installer/**` EULA RTFs (legal boilerplate)
- Deferred/GBuffer/MLAA/adaptive-DOF/post-scan renderer code — unreachable from RW 3.6

## 7. Disk note

202 GB on disk for ~12 GB of genuinely useful source. After this inventory, the
build/ + tools_ng/ + docs/art/ bulk (~190 GB) has no remaining investigative value
and is a safe reclaim candidate — pending user confirmation (not touched by this task).
