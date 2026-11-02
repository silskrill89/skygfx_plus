# Feature Matrix

Effect × mode. Status vocabulary: **shipped** (gate owned on and renderer implemented), **partial** (implemented but gate inherited/off, probe values, or one tier missing), **off** (mode explicitly disables it), **n-a** (not applicable to that renderer).

Source of truth: `kValues*` in `src/core/modeManager.cpp` plus the renderer symbol named in each row. Columns cover the six hardware-era looks plus Mobile, which is a first-class 70-field profile (the picker has eight chips: PS2 · Xbox · PC · Mobile · GTA IV · GTA V · Modern · Custom). `None` and `Custom` own nothing, so they always inherit (not repeated per row).

Legend: `PS2 | Xbox | PC | IV | V | Modern | Mobile`.

| Effect | PS2 | Xbox | PC | IV | V | Modern | Mobile | Evidence |
|---|---|---|---|---|---|---|---|---|
| PBR building | off | off | off | off | inherit | **shipped** | off | `BUILDING_PBR` only in `kValuesModern:514`; `buildingPipe.cpp` |
| PBR vehicle | off | off | off | off | inherit | **shipped** | off | `CAR_MODERN` in Modern only |
| PS2 modulate (building) | shipped | off | off | off | inherit | off | off | `ps2ModulateBuilding` 1 only in PS2 |
| Dual-pass building | off | shipped | shipped | off | inherit | shipped | shipped | `dualPassBuilding` |
| Detail maps | off | shipped | shipped | off | inherit | shipped | shipped | `detailMaps` |
| Noise detail | off | shipped | shipped | off | inherit | shipped | off | `noiseDetail` (legacy aliases `stochastic`, `stochasticTexturing`) |
| Env map (vehicle) | shipped | shipped | shipped | shipped | inherit | shipped | shipped | all own `envMapSize`; 256 vs Modern 512 |
| Water style/tier | partial (style 0) | partial (style 0) | inherit | **shipped** (style 1, q3) | **partial** (style 2, q3 only) | inherit | partial (style 0) | `waterStyle` in PS2/Xbox/IV/V; tuning knobs inherited by every profile (only IV owns `waterParallaxEnable`, `modeManager.cpp:458`) |
| PipeChain reflections | off | off | off | inherit | inherit | off | off | `pipeChainEnable` 0 in Modern; not owned elsewhere |
| Forward+ | off | off | off | inherit | inherit | **shipped** | off | `forwardPlusEnable` 1 in Modern; IV does not own it |
| Normal maps | inherit | inherit | inherit | inherit | inherit | inherit | off | Mobile owns `normalMapEnable=0`; all other profiles inherit it |
| noiseDetail | off | shipped | shipped | off | inherit | shipped | off | `noiseDetail` |
| Radiosity | off | shipped | shipped | off | inherit | shipped | off | `radiosity`/`doRadiosity` |
| Colour filter | PS2 | PC | PC | GTAIV | inherit | **Modern** | Mobile | `colorFilter` per profile |
| SSAO | off | off | off | **shipped** | inherit | **shipped** | off | `ssaoEnable=1` IV/Modern |
| SSS post | off | off | off | inherit | inherit | **shipped** | off | `sssPostProcessEnable` Modern only |
| Skin/hair/vegetation enhance | inherit | inherit | inherit | inherit | inherit | **shipped** | off | Modern only |
| SMAA | off | off | off | **shipped** | inherit | **shipped** | off | `smaaEnable=1` IV/Modern |
| FXAA | inherit | inherit | inherit | off | inherit | inherit | off | IV owns `fxaaEnable=0`; Modern does not own it |
| Motion blur | off | off | off | **shipped** | inherit | **shipped** | off | MB + `velocityBufferEnable` IV/Modern |
| Bloom | off | off | inherit | **shipped** (0.90/0.35) | inherit | inherit | off | IV owns new bloom pyramid; Modern inherits |
| Depth of field | off | off | inherit | **shipped** (probe values) | inherit | inherit | off | IV only |
| Sun shafts | off | off | inherit | **shipped** (0.84/0.97) | inherit | inherit | off | IV only |
| God rays (legacy) | inherit | inherit | inherit | off | inherit | inherit | inherit | IV pins `godRaysEnable=0` |
| Height fog | off | off | inherit | **shipped** | inherit | inherit | inherit | IV only |
| Grain | off | off | shipped | **shipped** (0.03) | inherit | shipped (legacy gate) | off | IV uses new `grainEnable`, Modern legacy `grainFilter` |
| Chromatic aberration | off | off | inherit | **shipped** (0.12) | inherit | inherit | inherit | IV only |
| Vignette (standalone) | off | off | inherit | off (pinned) | inherit | inherit | inherit | IV pins 0; `DrawIVGrade` vignettes instead (`modeManager.cpp:465`) |
| Tonemap / exposure | off | off | inherit | **shipped** (ACES) | inherit | **shipped** (auto) | inherit | `tonemapAutoExposure` |
| Coronas | shipped (no z-test) | shipped (z-test) | shipped (z-test) | inherit | inherit | shipped (z-test) | shipped (z-test) | `coronaZtest` owned PS2=0, PC/Modern=1, IV not owned |
| Ped/stencil shadows | inherit | inherit | inherit | inherit | inherit | inherit | inherit | Mobile owns both as `-1` (engine decides); every other profile inherits |
| texalpha / alpha contracts | shipped | shipped | shipped | shipped | inherit | shipped | shipped | safety four in every profile |

Notes that matter more than the cells:

- **Mobile is a full profile, not a stub.** 70 owned fields, first-class chip; building reuses the Xbox pipe. It explicitly turns `noiseDetail` off (the Mobile vehicle shader owns its detail) and turns normal maps off rather than inheriting them.
- **GTA V is water-only.** Three owned fields (`waterStyle=2`, `waterQuality=3`, `waterParallaxEnable=1`, `modeManager.cpp:545-550`). Every other GTA V cell is "whatever the base had". It is not a peer of the other modes.
- **GTA IV inherits motion blur/SSS/normal map/Forward+/buffers?** No longer true for MB/SSAO/SMAA/buffers: the 2026-10-25 IV audit added them (`modeManager.cpp:378-476`). Still inherited: `ivExposure`, `ivDesaturation`, `ivGamma`, `ivSaturation`, `ivCurves`, `ivVignette*`, `ivBloomIntensity`, `coronaZtest`. The IV grade itself is `DrawIVGrade`, not `COLORFILTER_GTAIV` (a bypass).
- **Fixed PC inherits normal maps, colour-filter enable, sun-corona gains, rgb multipliers, bYCbCr.** `kValuesPCVintage` does not own them, so a base with `normalMapEnable=1` keeps them on in "PC".
- **Bloom/DoF/sun shafts/height fog/CA are IV-only gates.** Modern does not own them; they come from the base config (ULTRA writes some of them in `main.cpp:5357-5365`). Do not advertise them as "Modern features" without checking the base.
- **Motion-blur value is a probe.** IV's 0.35 and Modern's 0.4 are heuristics, not measured parity.
- **Normal maps:** `normalMapEnable` is owned only by Mobile (=0); every other profile inherits it. `enableNormalMaps` is still read by `src/texdefs.cpp:981` via `GetPrivateProfileStringA`, so do not call it dead without checking that TU.
- **PipeChain is off in Modern** (`pipeChainEnable=0`) even though the renderer exists.

## Status rules

- **shipped** — the mode's `kValues*` table owns the gate and sets it on, and the renderer path named in the evidence column exists in the tree. "Shipped" does not mean pixel-verified; the verification column says that separately (none of the cells below have a recorded run).
- **partial** — the renderer exists but the mode only owns part of it (GTA V water), the value is a probe heuristic (IV), or the gate is inherited rather than owned.
- **off** — the mode table explicitly writes the gate to 0.
- **inherit** — not present in the mode table; the cell takes the base snapshot value. This is the most common source of "mode X is broken" reports.
- **n-a** — the renderer does not exist for that era (e.g. PS2 vector water).

No cell in this matrix is backed by a screenshot/RGB run in this document. Per-mode verification still has to be done with [debugging.md](debugging.md).

## Effects with no mode ownership at all

These are global keys; a mode cannot turn them on or off:

- `normalMapEnable` (Mobile owns =0; all others inherit) and the related `normalMap*` keys.
- `pedShadows`, `stencilShadows` (Mobile owns both as the engine sentinel `-1`; no other profile owns them).
- `sunCoronaIntensity`/`sunCoronaSize`/`sunStreak*` (except PS2/Xbox/PC write identity gains 1.0).
- `rgb1Mult`, `rgb2Mult`, `bYCbCrFilter` (owned only by PS2/Xbox).
- `waterParallax*`, `waterNormalStrength`, `waterFresnelPower`, `waterSpecular*`, `waterUnderwaterFog`, `waterFoam*`, `waterShallow*`, `waterDeep*`, `waterTileScale`, `waterShoreFade`, `waterTranslucency`, `waterReflectionStrength`, `waterUseTimecycle`, `waterShoreFoam`, `waterBodyClassify` — inherited by every mode (only IV owns `waterParallaxEnable`).
- `envFresnel` (Modern writes 0.4; every other mode inherits, clamped `[0,0.4]`).

## Prerequisites per effect

A gate being on is not enough; the pass also needs its producer.

| Effect | Requires | Failure if missing |
|---|---|---|
| SSAO | `normalBufferEnable`, depth hook | pass runs but reads flat normals — uniform darkening |
| Motion blur | `velocityBufferEnable`, depth hook | no smear / black frame |
| DoF | depth hook | focus stuck at one plane |
| Sun shafts | depth + sky/eye constants | streaks through geometry or no shafts |
| Bloom | colour-filter/tonemap chain context | clipped or absent highlight bleed |
| SMAA | camera colour raster initialized | skipped pass (see `[SMAA]` raster init lines) |
| Forward+ | tile atlases created for the current screen size | fallback to 7-light path (`forwardPlusFallback`) |
| PipeChain | `pipeChainEnable` | off in Modern profile |
| Water IV/V | scene/depth copy for refraction | opaque water, no shore blend |
| PBR | `pipeUploadPBR` for c22/c23 | flat/dark materials |
| Normal maps | RW normal-map plugin attached (`normalmap_tryAttach`) | no detail, no crash |
| Coronas | sprite window armed at `CCoronas::Render` | missing glows or black quads |
| Shadows | shadow contract around each pass | stale ALPHAREF/black blobs |

## Verification per mode

There is no recorded verification run in this document. The minimum run for any cell:

1. Build, deploy, start, confirm `[BUILD]` identity.
2. Confirm `[Mode] apply` numbers.
3. Confirm the effect's tag (table above).
4. Screenshot the same scene with the effect gate on and off; record RGB signatures.
5. Only then change the cell from **inherit** to **shipped**.

## INI gates

One key per effect. Default shown is the fresh-Install ULTRA base unless a mode overrides it.

| Effect | Gate key | Default | Notes |
|---|---|---|---|
| PBR pipes | `pipeline` / `buildingPipe` / `vehiclePipe` | `PBR` → PBR/Modern | ignored while a pipe-owning mode is active (`main.cpp:110-120`) |
| Colour filter | `colorFilter` | `Modern` (PBR force) | `PC` only if `pipeline` changed away from PBR |
| SSAO | `ssaoEnable` + `ssaoRadius/Power/KernelSize/SampleCount` | 1 in IV/Modern | needs normal buffer |
| SMAA | `smaaEnable` | 1 in IV/Modern | `smaaPreset`/`smaaPredication`/`smaaTemporal` are **dead padding** (`skygfx.h:413-415`) |
| FXAA | `fxaaEnable` | 0 in IV | cheap fallback |
| Motion blur | `motionBlurEnable` + `motionBlurStrength/Radial/SpeedFactor/CameraAware` | 1 in IV/Modern | needs `velocityBufferEnable` |
| SSS post | `sssPostProcessEnable` + strength/radius | 1 Modern | skin/hair/veg separate keys |
| Bloom | `bloomEnable` + `bloomQuality/Threshold/Intensity` | IV 1/2/0.90/0.35 | LDR threshold must stay < 1.0 for IV |
| DoF | `dofEnable` + `dofFocusDistance/FocusRange/BokehRadius/MaxBlur` | IV 1 | probe values |
| Sun shafts | `sunShaftsEnable` + density/decay | IV 1/0.84/0.97 | legacy `godRaysEnable` pinned 0 in IV |
| Height fog | `heightFogEnable` | IV 1 | |
| Grain | `grainEnable`, `grainIntensity` (new) / `grainFilter` (legacy) | IV 1/0.03 | two paths |
| CA | `caIntensity` | IV 0.12 | |
| Vignette | `vignetteIntensity` / `ivVignetteIntensity` | IV standalone 0, grade 0.15 | IV double-vignettes if both on |
| Tonemap | `tonemapAutoExposure` + adapt/key/min/max; `tonemapOperator` | IV operator 1 | ACES |
| Forward+ | `forwardPlusEnable` (+`forwardPlusFallback`) | 1 Modern | IV inherits |
| PipeChain | `pipeChainEnable` | 0 Modern | implemented, not preset-enabled |
| Normal maps | `normalMapEnable` | 0 | inherits except Mobile (=0); `enableNormalMaps` read by the normalmap plugin |
| Water | `waterStyle`, `waterQuality` + `water*` tuning | style per mode, tuning inherited | `waterParallaxEnable` only IV owns |
| Coronas | `coronaZtest`, `doglare`, `sunCorona*` | PS2 0, others 1 | sun gains not mode-owned |
| Shadows | `pedShadows`, `stencilShadows` | -1 (engine) | not mode-owned |
| Alpha/state | `stateBaselineEnable`, `texAlphaClassify`, `pedOpaqueContract`, `defaultCbShaderHygiene` | 1 in every profile | safety, not look |
| Env map | `envMapSize`, `envMapFarClipMult`, `envFresnel` | 512 / 2.0 / 0.4 | envFresnel clamped `[0,0.4]` |
| Quality preset | `qualityPreset` | 3 (ULTRA) | tiers 0-2 write pipes only when no mode is active |

## Log tags

Which line proves the effect ran (see [debugging.md](debugging.md) for the full dictionary).

| Effect | Tag / receipt |
|---|---|
| Mode change | `[Mode] apply <name> pipes=<b>/<v>/<cf> iv=<n>` |
| Pipe / filter init | `[Pipeline] ...`, `[Config] PBR selected -> ...` |
| PostFX chain | `[PostFX] ...` (65 emission sites, `postfx*.cpp`) |
| SSAO / SMAA / MB | `[PostFX]` sub-lines, `[SMAA] ...` |
| Forward+ | `[ForwardPlus] textures created ...`, `[ForwardPlus] ... fallback` |
| Water | `[water] ...` (20 sites) |
| Mirror / env map | `[MirrorPass] ...`, `[EnvMap] ...`, `[EnvTex] ...` |
| PipeChain | `[PipeChain] ...` (19 sites) |
| Pellet/vehicle PBR | `[VehiclePBR] ...` |
| Shadows | `[shadow_contract] <pass> ...`, `[ShadowHeal] ...` |
| State drift | `[RSMon]` report from `renderstate_monitor.cpp` |
| Build identity | `[BUILD] asipath= size= mtime= built=` |
