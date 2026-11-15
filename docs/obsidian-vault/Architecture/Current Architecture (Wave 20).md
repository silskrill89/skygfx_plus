---
tags: [architecture, wave-20, extraction, hard-rule]
created: 2026-11-10
updated: 2026-11-10
---

# Current Architecture — Wave 20 (2026-11-10)

> [!info] Scope
> This is the architecture **on disk after wave 20**, not a shipped binary. The last gate build is
> W0.5; see [[Gate Chain]] and [[Status 2026-11-10]]. Line counts below are `wc -l` on the live
> tree at 2026-11-10 and will drift as the address-hygiene sweep lands.

## 1. `main.cpp` decomposition

The 2026-11-09/10 extraction campaign moved four self-contained systems out of `main.cpp`,
following the rule that extraction comes **after** behavioural fixes, one system per wave, with
byte-identical bodies:

| File | Lines | Role | Landed |
|---|---:|---|---|
| `src/core/main.cpp` | 4,476 | `DllMain`, hook installation, pipeline switch, residual shields, frame-boundary helpers | after E1–E4 (wave record: 4,416 → 4,428; live count 4,476) |
| `src/core/shadows.cpp` | 804 | Shadow hook install and shadow render-state machines (`[ShadowHeal]`, `[shadow_contract]`, `[PedRS]`) | E1 (W0.2; missing vcxproj entry caused the W0.2 gate-blocking LNK2019) |
| `src/core/rw_fixes.cpp` | 2,140 | RenderWare-level fix machines: corona / sprite / font / shadow state; `rwFixesEnable` kill-switch, `[rwfix] install OK (6 machines)` | E2 (wave record 2,026 at W0.5) |
| `src/core/config.cpp` | 1,957 | `StrAssoc`, `readint`/`iniGet2`/`readhex`, `readIniFile` incl. `SKYGFX_EMIT_DEFAULT_KEYS`, `IniPathForSlot`, `readIni`/`readInis`, `setConfig`, `reloadAllInis`, save/load | E3 (W0.5) |
| `src/core/render_callbacks.cpp` | 1,435 | `atomicHasAlpha`, `PedOpaqueState` family, `pedStage0Resync`, `vs_probe`, `myDefaultCallback` @ `0x7491C0`; dual-path arms intact | E4 (W0.5) |

Lessons that are now standing rules:

- Every new TU must be added to `build/skygfx.vcxproj`; premake globs do **not** auto-pick it.
- `main.cpp` is single-writer — at most one lane edits it per wave.
- Extract byte-identically; behaviour changes go in their own wave.

The W0.5 R-fix wave then removed duplication inside what remained: `frameBoundaryCloseAndBaseline`
at the 4 frame boundaries, `clampAlphaRef` at inline clamp sites, `forceFFPNeutral` at 8 FFP sites,
`restoreStage0Tex`, and retirement of zero-writer `CTagManager` fallbacks. Audit:
`docs/plans/2026-11-09-main-redundancy-audit.md`.

## 2. Motion-FX and wind family

Wave 17 (W0.3) split the old tree-wind path into a parent/child pair while keeping all 14 external
symbols and signatures exact, so no caller changed:

| File | Lines | Responsibility |
|---|---:|---|
| `src/render/motion_fx.cpp` / `.h` | 265 | Parent: motion orchestration, wind animation, randomness/physics-family integration; one clock `MotionFxClockSeconds()`; `NoiseMaterialVariation` seam |
| `src/render/wind.cpp` | 177 | Child: wind field / gust math; pure math, no family or render-state knowledge |
| `src/render/treeWind.cpp` | — | Only `TreeAlpha` (fixed-function tree lighting/alpha); slimmed 521 → 229 in W0.3 |

The clock is a game `CTimer__ms_fTimeStep` accumulator, not QPC — pause-safe. Randomness is a
±25% per-model gust amplitude band from `NoiseMaterialVariation`; that is the only behavioural
delta of the split. The per-material noise phase rides `c3.x` in `noiseDetailBuildingPS.hlsl`.

## 3. PostFX split (`postfx_*`)

The former monolithic `postfx.cpp` is nine translation units. `postfx.cpp` remains the chain core:

| File | Lines | Responsibility |
|---|---:|---|
| `src/render/postfx.cpp` | 5,532 | `DrawFinalEffects` chain, radiosity (PS2/Shader/VCS), IR/night vision, grain, front-buffer update |
| `src/render/postfx_colourfilter.cpp` | 2,016 | ColourFilter (PS2, PC, Mobile, III, VC, VCS, GTAIV/IV grade, generic), DarknessFilter, colour cycle |
| `src/render/postfx_luminance.cpp` | 490 | Luminance measure/adapt targets, auto exposure |
| `src/render/postfx_motionblur.cpp` | 581 | Velocity buffer, `DrawVelocityBuffer`, tier-0 framerate normalisation |
| `src/render/postfx_newfx.cpp` | 901 | Sun shafts, DOF, bloom, chromatic aberration, film grain, vignette, FXAA3 |
| `src/render/postfx_smaa.cpp` | 1,434 | SMAA passes + static area/search textures |
| `src/render/postfx_ssao.cpp` | 1,450 | SSAO passes + depth / prev-depth / normal-buffer resources |
| `src/render/postfx_underwater.cpp` | 563 | Underwater volume fog + subsurface scatter |
| `src/render/postfx_utils.cpp` | 338 | Fullscreen quad, tracked rasters, screen-size cache, geometry save/restore |

Current weak points inside this family are recorded in [[Status 2026-11-10]]: the `FxPass` raw
`SetTexture(0,NULL)` anti-pattern, `DrawChromaticAberration`'s scope-before-gate, and the
`LuminanceReduce.hlsl` bottom-17% under-sample.

## 4. Peds / skin module

| File | Lines | Responsibility |
|---|---:|---|
| `src/peds/skin.cpp` / `.h` | 1,205 | Ped skin pipeline and callbacks; N9 sentinel honor at `skin.cpp:927-948` |
| `src/peds/peds_pipe.cpp` / `.h` | 265 | Ped pipe mode/policy layer |
| `src/core/render_callbacks.cpp` | 1,435 | Ped opaque state machine (`PedOpaqueState`), stage-0 resync, default render callback |

Ped depth/opaque state is enforced through the `PedOpaqueState` family plus the `[PedZW]` receipt;
the black-quad family-0 white-shield and softbody TOCTOU gates are described in
[[Status 2026-11-10]].

## 5. Address declaration layer (the HARD RULE)

The project now requires one named declaration layer for game/SDK addresses:

- `src/rw/gta.h` holds named externs and offset contracts (precedent: `d3d9device`, `pDirect`,
  `CGame__currArea`, `DirectionalLightColourForFrame`; offset contracts at `gta.h:391-527`).
- Call sites use the **name**. Raw hex at a call site is a violation of the 2026-11-10 hard rule
  (user m0269).
- A one-off constant is allowed only when it is disassembly-proven and documented inline as
  "no symbol exists".

The sweep is not yet built. Its targets are listed in wave-20 §6 and summarized in
[[Status 2026-11-10]].

## Related

- [[Status 2026-11-10]] — what landed, what is pending, watch-list
- [[Gate Chain]] — binary identities and gate protocol
- [[SkyGFX Pipeline Overview]] — rendering pipeline entry points and registers
- [[Decided Architecture]] — original architecture decisions
- `docs/plans/2026-11-09-main-redundancy-audit.md`
- `docs/plans/2026-11-09-junior-rewrite-plan.md`
- `docs/plans/2026-11-08-master-plan.md`
- `CODEBASE_DOCUMENTATION.md` (repo root; note it still carries pre-extraction line counts)
