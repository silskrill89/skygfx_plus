# Master Plan — GTA IV + GTA V Graphics Port, Mode Manager, Bullet Physics (2026-10-08)

Reconciles 5 recon lanes: exp-1 (FusionFix/FusionShaders SDKs), exp-2 (LibertyRecomp),
exp-3 (GTA V/RAGE leak physics), exp-4 (gtasa_src-main + RAGE in E:\dev(dave)\releases\GTASource),
exp-5 (skygfx current state). Directives: .serena/memories/round6-directives-2026-10-08.md.

## 0. Design contract (user, m0022/m0023)

### Mode Manager (`src/core/modeManager.cpp/.h` — new)
- Separate **Modes menu section** with toggle rows per mode: `Vanilla(none) | PS2 | Xbox |
  Mobile | GTAIV | GTAV` (extensible enum).
- Semantics: ONE active mode at a time (or none).
  - First toggle ON → snapshot full `Config` to `g_preModeConfig`.
  - Activate(M) → `Config = g_preModeConfig + M.profile` (mode profile = named config delta,
    incl. which pipe segments/features run).
  - Toggle B while A active → recompute from snapshot + B profile (no stacking).
  - Toggle OFF (all) → `Config = g_preModeConfig`, notify listeners.
- Listener registry `OnModeChanged()` → postfx rebuild, pipe re-select, shader style
  (waterStyle etc.), colour filter re-pick, menu row refresh.
- **ivMode = one mode profile** (guts its preset-creator behaviour; keeps IV-aspect gating
  on broken-down pipes). Presets remain full-profile savers (orthogonal).
- Applies to graphics modes only; physics toggles are separate feature toggles (ragdoll,
  buoyancy, vehicle) that compose with any mode.

### Physics ↔ Graphics synergy (m0023)
- Physics velocities drive motion-vector buffer → motion blur/SMAA/TAA improvement
  (rotors/cloth/peds get real velocity, not camera-only).
- Render buffers (depth/velocity/normals) feed back into physics (terrain collision height
  from depth, surface normals from normal buffer).
- Later phase: boolean damage system (Red Faction 1-style geometry destruction) on Bullet.

## 1. Feature matrix to port (from recon)

### A. PostFX (IV: FusionFix/LibertyRecomp; V: PostProcessFX) — all SM3.0
| Feature | Source | skygfx today | Action |
|---|---|---|---|
| DOF (CoC prefilter→bokeh→tent→combine) | LR `dof_*.hlsl`, FF `dof_*.asm` | MISSING | Port (half-res chain) |
| GTAO-style SSAO | LR `ssao_gtao_ps.hlsl`, FF `AO.fx/SSAO3` | SSAO exists (broken upsample) | Fix 2-liner first, then GTAO variant behind quality tier |
| Sun shafts | LR `sunshafts_{prepass,radial,composite}`, GPU Gems 3 | GodRays.hlsl exists | Adopt radial-scattering math + sky-mask from clouds |
| Bloom (6-mip Jimenez, soft-knee, Karis) | LR `bloom_*.hlsl`, FF `rage_postfx PS6/7` | Simple blur bloom | Replace behind `bloomQuality` tier |
| LUT tonemap ops: ACES(1.3/2.0), AgX, Frostbite, GT7 | LR `hdr_tonemap_ps.hlsl`, FusionShaders `assets/luts/*`, `LUTBaker.hlsl` | filmic/Uncharted only | Add operators + LUT baker tool |
| Auto-exposure (GPU avg luminance) | V `exposureGPU/avgLuminanceCompute` | LuminanceAdapt/Reduce | Adopt V algorithm |
| FXAA 3.11 | FF `FXAA3_11.h`, V `FXAA` | MISSING | Port |
| SMAA quality tiers (Low/Med/High/Ultra) | LR `smaa_pipeline`, our SMAA.cpp | Single quality | Add tiers (edge/weight variants) |
| Motion blur (camera reprojection + physics velocity) | LR `motion_blur_camera_ps`, FF PS26 | Burnout/vector blur | Reprojection + per-object velocity (synergy) |
| Film grain (IGN, TAA-safe) | LR `film_grain_ps.hlsl` | Basic grain | Replace |
| Chromatic aberration | LR `chromatic_aberration_ps.hlsl` | MISSING | Port |
| Vignette (parametric) | LR `vignette_ps.hlsl` | IV vignette exists | Generalize |
| Stipple / screen-door transparency | FF `stipple_filter.asm`, `stipple.wtd` | MISSING | Port |
| Pre-alpha depth (FX through glass) | FF `EnablePreAlphaDepth` | MISSING | Port |
| MLAA / lens artefacts (optional) | V `MLAA.cpp`, `LensArtefacts.cpp` | MISSING | Optional later |
| Console gamma + screen filter | FF `xbox_gamma.csv`, `consolegamma.ixx` | ColorFilter exists | Add ops |
| Volumetric fog (IV method) | FF `VolFogFarClip`, LR fog shaders, V `exponential_height_fog` | HeightFog exists | Extend to IV/V method |
| Volumetric light volumes / smooth | FF `deferred_lighting*smooth`, V `deferred_volume/capsule` | forward+ | Extend |

### B. Shadows
| Feature | Source | Action |
|---|---|---|
| Cascaded shadow maps + cascade blend | V `Shadows/CSM`, FF `cascadeGen/Convert`, `CascadeBlendSize` | Port CSM into shadow system |
| CHSS / PCSS contact-hardening filters | FusionShaders `snippets/Contact_Hardening_Shadows`, `Custom_Shadow_Filter` | Port as filter profiles (Sharp/Soft/CHSS) |
| Night shadow atlas fixes (`shadowSmartBlit`) | FusionShaders | Fix seam pops |
| Extra dynamic shadows | FF `nightshadows.ixx` | Port (segment) |

### C. Water
| Feature | Source | Action |
|---|---|---|
| River/foam/shallow shader family | V `shader_source/Water/*` | Port onto waterPipe.cpp styles |
| Smooth shorelines + foam + offset reflections | FF water PS2, LR `water_ps1_smooth_shorelines` | Add (shoreFade already exists) |
| Mirror distortion fix + Reflection MSAA 2/4/8 | FF `mirror PS1`, `reflectionmsaa.ixx` | Port |
| Water physics: CBuoyancy (sample-grid buoy/lift/keel/drag) | V `Floater.{h,cpp}` | Port to Bullet (P3) |
| River flow/water level helpers | V `WaterTestHelper`, `Water.h` | Port minimal subset |

### D. Vehicles / reflections / glass
| Feature | Source | Action |
|---|---|---|
| Mirror RT technique | V `Mirrors.cpp` | Port (window pipe) |
| Reflection mip blur (RefMipBlur) | FF `RefMipBlur_*.asm` | Port |
| MeshBlendManager (prop/veg colour blend) | V `MeshBlendManager.cpp` | Port (cheap visual win) |
| Vehicle paint/chrome/rims family polish | FusionShaders `gta_vehicle_*` | Extend VehiclePBR variants |

### E. Vegetation / buildings (LOD systems NOT required)
| Feature | Source | Action |
|---|---|---|
| Grass LOD0/1/2 + instanced draws | V `PlantsMgr.cpp`, `grass_batch.fx` | Port (biggest veg win; ours = per-plant RpAtomicRender) |
| Tree wind sway + tree shadows | FF `gta_trees*`, V `Trees/*` | Extend existing wind |
| Apply parsed `intensity/intensity_var` | gtasa_src (ours parses, ignores) | 2-line win |
| Console tree lighting/alpha | FF `OverrideTreeAlpha`, tree lighting PC/PC+/Console | Port |
| DN-pipeline/building visuals | gtasa_src `CustomBuilding(DN)Pipeline` | Compare & port missing bits |

### F. Misc
- LOD lights / 2DFX distant lights: HAVE (weather.cpp corona/2DFX) — verify vs FF `lodlights.ixx`.
- Extended sunlight reach + cloud-mask for sun shafts (`gta_atmoscatt_clouds`): port.
- Soft particles, rain streak/visibility, particle flicker: port into postfx/particles.

## 2. Physics (Bullet only — NEVER Euphoria)

Sources: GTA V/RAGE leak (exp-3), gtasa_src Ragdoll.cpp (434L verlet-style), our code (exp-5).
- **Ragdoll completion (top priority)** — `src/extras/ragdoll_death.{h,cpp}` (1227L, Bullet
  2.87, `g_bulletWorld`) + `src/entities/ragdoll.cpp` (alive-ped blend, 534L):
  1. Terrain collision: `CWorld::FindGroundZForCoord` instead of fixed plane (plan:354).
  2. `Ragdoll_HitReaction` (extension point ragdoll_death.cpp:853-868, zero callers).
  3. Phase 4: vehicle impacts, weapon knockback, writhe, multiple death poses.
  4. Joint upgrades from RAGE `pharticulated`: 3-DOF lean/twist limits + soft-limit springs
     (`SetThreeAngleLimits`) → btGeneric6DofSpring/btConeTwist; muscle drive → constraint motors
     (writhe/relax = `CTaskRageRagdoll` semantics).
  5. Blend-out + getup pose-matcher (V `posematcher`, `TaskBlendFromNM`) — later.
  6. Constraint gameplay API (V `RagdollConstraints` → btPoint2Point/btHinge, breakable).
- **Buoyancy/water physics**: port `CBuoyancy::Process` (sample grid: depth-factor buoyancy,
  Lift/Keel/Drag/Damping forces, per-type drag coeffs) onto `g_bulletWorld` bodies.
- **Vehicle physics (lightweight)**: `CWheel` raycast suspension + tyre forces (slip ratio,
  camber, downforce) + `CHandlingData` fields → Bullet raycast vehicle or force callbacks
  (visual/handling enhancement only; SA keeps its own sim — we layer or gently override).
- Ragdoll.cpp living-ped secondary motion: keep (it is the "advanced" layer per user: bolt for
  what Euphoria does; here verlet blend is fine — extend with Bullet where needed).
- Future: **boolean damage** (Red Faction 1-style destructible geometry) on Bullet.

## 3. Phased execution (build rule: orchestrator runs ONE unified build per wave)

- **P0 STABILISE** (independent of port, do first — sanctioned by m0161 "focus on bug fixing"):
  1. SSAO_Upsample.hlsl:34/:69 2-liner grey-overlay fix (exp SSAO audit).
  2. Colour-filter fix: R2 (None+enable=1 true no-op) + R1 (skip PS2 MOD2X conversion for
     PC/NONE consumer) — postfx.cpp ColourFilter_switch region.
  3. SMAA temporal Step1 (neighbourhood clamp, motion curve) — SMAA_Temporal.hlsl.
  4. Sky foreign-raster repair (RenderScene_before main.cpp:1666) + FBWP rework + clouds hygiene.
  5. IV-mode gut happens inside P1 mode manager (not standalone).
- **P1 MODE MANAGER**: modeManager.cpp/.h + Modes menu + ivMode profile + snapshot/restore +
  listener registry. Files: new cpp, skygfx.h, debugmenu_ui.cpp, main.cpp, iv_mode.cpp (gut).
- **P2 GRAPHICS PORTS** (segmented; each = config key + menu row + shader entry, per m0172
  segmentability; shared helpers in `src/shared/` per m0173): order by value/risk —
  DOF → bloom chain → LUT tonemap → sun shafts adoption → FXAA → CA/vignette/grain →
  stipple/pre-alpha depth → water family → CSM/CHSS → grass instancing → veg polish → misc.
- **P3 PHYSICS**: ragdoll completion (terrain/hit/Phase4/joints) → buoyancy → vehicle wheels.
- **P4 SYNERGY**: physics-velocity → motion vector buffer (per-object velocity textures for
  motion blur/SMAA/TAA); depth/normal buffers → physics collision feedback.
- **P5 BOOLEAN DAMAGE** (Red Faction-style) — later, on Bullet.

File ownership discipline: postfx.cpp ONE writer at a time across waves; shader files
(shaders/ps/*, shaders/vs/*) are disjoint and parallelizable; main.cpp edits serialized into
one lane per wave; waterPipe.cpp own lane; vegetation own lane.

## 4. NEVER-REVERT / doctrine integration
- c22/c23 only via pipeUploadPBR(); PBR colorFilter = MODERN(8); three-layer render state
  (rw→driver→device); division safety 1e-7; read-only lanes never build.
- Round-5 audit fixes (SSAO/SMAA/colour-filter/sky) documented in
  .serena/memories/round5-consolidated-2026-10-08.md — never regress those fix targets.
- Mode manager must NEVER mutate g_preModeConfig after snapshot; presets remain independent.
- Physics work must not touch render state; Bullet world isolated in extras/.
