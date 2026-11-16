---
tags: [pipeline, rendering, architecture]
created: 2025-01-02
updated: 2026-11-16
---

# SkyGFX Plus - Rendering Pipeline Overview

> [!info] Full documentation
> See `docs/Vehicle Pipeline.md`, `docs/Building Pipeline.md`, `docs/Unified Pipeline.md` for detailed pipeline docs.

## Architecture
3-step forward+ pipeline:
1. **Prepass** — main pass, gbuffer fill
2. **Dynamic Sky + PBR Lighting** — sky rendering, light accumulation
3. **Clamp + PostFX** — tonemapping, AA, color grading

## Vehicle Pipeline (CAR_MODERN)
Entry: `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env` in `vehiclePipe.cpp`

### Mesh Classification (per-mesh in callback)
| Path | VS | PS | Condition |
|------|----|----|-----------|
| Glass + Lights | `vehiclePBRVS` | `Glass_Vehicle` | isGlassMesh or isLightMesh |
| Tires | `vehiclePBRVS` | `Rubber_Vehicle_Modern` | isTireMesh |
| Opaque PBR | `vehiclePBRVS` | `VehiclePBR_Modern` | default (paint) |

### Texture Binding (PBR path)
| Stage | Sampler | Texture | Purpose |
|-------|---------|---------|---------|
| s0 | `diffuseTex` | Diffuse (material) | Material texture |
| s1 | `envMapTex` | Env map / reflection | Scene reflection capture |
| s2 | `maskTex` | Reflection mask | Reflection intensity mask |
| s3 | `iblTex` | IBL buffer | Sky ambient color |
| s4 | `normalBufTex` | Normal buffer | Screen-space normals |

> [!warning] Common mistake
> The texture order is **not** s0=diffuse, s1=normal, s2=mask, s3=IBL, s4=env. The actual order is s0=diffuse, s1=env, s2=mask, s3=IBL, s4=normal. See `shaders/ps/VehiclePBR_Modern.hlsl:18-22`.

### Shader Constants (PBR path)
| Register | Content | Source |
|----------|---------|--------|
| c0 | surfProps (ambient, diffuse, specular, prelight) | material |
| c1 | fxParams (fresnel, power, lightmult, shininess) | material |
| c2 | eyePos | camera |
| c3 | iblParams (roughness, metalness, 0, 0) | BRDF |
| c4 | cloudShadow (sunDir, time, 1, 0) | weather |
| c5 | directCol | lights |
| c6-c11 | lightCol[6] | lights |
| c12 | directDir | lights |
| c13-c18 | lightDir[6] | lights |
| c19 | matCol | material |
| c22 | pbrParams (glossiness, reflectance, clearcoat, subsurface) | BRDF |
| c23 | paintNoise (wheel, noiseScale, edgeBlend, 0) | paint type |
| c24 | ambientColor (r, g, b, normalBufFlag) | timecycle |

## Building Pipeline (BUILDING_PBR)
Entry: building pipe callback in `buildingPipe.cpp`
- Uses vertex color as base color (SA buildings use painted vertex colors)
- Texture on top for detail
- BRDF values from material table based on texture name pattern

## PostFX Chain
1. DynamicSky — sky + clouds (IBL buffer)
2. SSAO — screen-space ambient occlusion
3. Color grading — YCbCr correction + Reinhard tonemapping
4. SSS blur — skin translucency
5. SMAA — anti-aliasing (3-pass)

## Material System
- BRDF table in `brdfLibrary.h` — 37+ surface types
- Glossiness workflow: `glossiness = 1.0 - roughness`
- All dielectric (metalness = 0 for most surfaces)
- F0 from IOR: `((n-1)/(n+1))^2`
- Texture → BRDF mapping via pattern matching on texture names

## Related
- [[Non-Parametric Sparse BRDF]] — BRDF reference
- [[RenderWare V2.1 API Reference]] — RW pipeline
- [[SMAA Enhanced Subpixel Morphological AA]] — AA system
- [[VehiclePBR Modern]] — Vehicle shader details
- [[IBL Env Map Decision]] — IBL approach

---

## Addendum — 2026-11-10 (wave 20)

The pipeline shape above is unchanged; the **code layout** has moved on:

- **PostFX split:** the former single `postfx.cpp` is now nine units. `postfx.cpp` keeps the
  `DrawFinalEffects` chain, radiosity and front-buffer update; colour filter, luminance/auto
  exposure, motion blur, new FX (shafts/DOF/bloom/CA/grain/vignette/FXAA3), SMAA, SSAO,
  underwater and shared utils are separate TUs. Faults in this family are tracked in
  [[Status 2026-11-10]].
- **Motion-FX family:** `motion_fx.cpp` is the parent (motion orchestration, wind animation,
  `MotionFxClockSeconds()`, `NoiseMaterialVariation` seam); `wind.cpp` is the pure-math child;
  `treeWind.cpp` keeps only `TreeAlpha`.
- **Extractions:** `shadows.cpp`, `rw_fixes.cpp`, `config.cpp` and `render_callbacks.cpp` left
  `main.cpp`. See [[Current Architecture (Wave 20)]].
- **HARD RULE:** addresses are named in `src/rw/gta.h`; call sites never use raw hex except for a
  documented, disassembly-proven one-off.
- Last verified binary: W0.5 (`2,616,832 B`, `bb5d1230…`); W0.6 pending ([[Gate Chain]]).

---

## Addendum — 2026-11-16 (full pipe trace + root cause)

- **Frame-by-frame order chart committed:** `docs/plans/2026-11-15-rendering-pipeline-order.md`
  — Layer 0 frame entry → Layer 1 scene geometry (sky, buildings, vehicles, peds, default pipe,
  water, mirrors, skidmarks, tags, effects, tree wind) → Layer 2 postfx → Layer 3 frame exit,
  plus the key-hook table and the `skygfx.ini` config reference.
- ⚠ **Correction:** that document's §1b "Known issues" and its "WHY BUILDINGS DON'T RENDER"
  section claim `0x53C15E` is NOT a CALL and that `InterceptCall` corrupts the building render
  path. **Disproven** by correct PE-section mapping: `0x53C15E` = `E8` (CALL), `0x5D5B10` = `E9`
  (JMP). All hook addresses are byte-identical to AAP, and pipe creation
  (`CreateCustomObjPipe_PS2`, `buildingPipe.cpp:4206/4237` = AAP `buildingPipe.cpp:723/754`) is
  identical — CreateShaders ran (ps=55 vs=17, 0 failed).
- **The pipe is not the defect.** Traced end-to-end against AAP (`E:\SDKs\skygfx_original`):
  hook set, Switch/Xbox cb draw mechanics, CreateShaders, Update ambient all match. Full trace:
  `docs/plans/2026-11-16-pipe-trace-and-root-cause.md`.
- **The defect is texture resolution, not drawing:** `TexDefsFindCB` misses (broken
  `dict->parent` walk in this ModLoader/ROSA install + LOD-DFF-first streaming) bound the shared
  `skygfx_missgrey` 1×1 grey sentinel into `material->texture` forever → grey × ambient world
  (pale-blue slabs by day, near-black at dawn). Fix in commit **e7bc752 (pushed)**: per-name
  sentinel clones + `TexDefsResolveAtDraw` draw-time retry routed through `pipeSetTexture`
  (`src/skygfx.h:1351`) + `TexDefsRemapToWorld` at sprite→world remap sites. See
  [[Status 2026-11-16]].
- Instrumentation one-shots deployed, awaiting one game-run to decide which cb draws the world:
  `[BPIPE] Switch cb ENTER` (`buildingPipe.cpp:3830`), `[BldTex] bldPolicyTexture first`
  (`buildingPipe.cpp:1384`), `[texdefs] resolve first-call` (`texdefs.cpp:794`).
- Xbox engage finding: `[Mode] engage Xbox owned=80 pipes=2/1/2 cf=2 iv=0 smaa=0` — pipes were
  ALREADY Xbox from the ini (`mode=none` at start); engage only flips colorFilter
  Modern(8)→PC(2) + smaa=0 + mode-owned toggles, and the "xbox void" screenshot preceded the
  engage in wall-time.
