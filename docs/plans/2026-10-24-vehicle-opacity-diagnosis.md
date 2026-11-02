# Vehicle Opacity / Mesh Corruption Diagnosis — 2026-10-24

Scope: PBR vehicle pipe (`PIPELINE_PBR` / Env mesh callback). Fixes see-through
roofs, dissolving/detached body panels and mirror-chrome body shells.

## Live log evidence (before)

```
[VehOpacity] tex='vehiclegrunge256'   tac=0 hasAlpha=1 blendOn=1 shaderAlpha=0.00 matA=16
[VehOpacity] tex='vehiclelights128'   tac=0 hasAlpha=1 blendOn=1 shaderAlpha=0.00 matA=16
[VehOpacity] tex='vehiclegeneric256'  tac=0 hasAlpha=1 blendOn=1 shaderAlpha=0.00 matA=16
```

`tac=0` is `TAC_UNKNOWN` (classifier budget-skipped / not yet classified), not
`TAC_OPAQUE`. `matA=16` is the SA material paint marker.

## Cause 1 — paint marker `material->color.alpha == 16` treated as translucency

- `hasAlpha = vertexAlpha != 0 || material->color.alpha != 255` made 16/255
  count as alpha, so `TAC_UNKNOWN` selected `blendOn = 1; shaderAlpha = 0.0f`.
- With `c48.w = 0` the PS returns `lerp(diff.a, 1.0, 0) = diff.a`. DXT3/DXT5
  body textures with empty alpha decode `diff.a ~ 0` → invisible / ghost
  panels and roofs.
- Because the classifier had skipped the texture (`tac=0`), the pixel never got
  the `shaderAlpha = 1.0` rescue that `TAC_OPAQUE` provides.

## Cause 2 — glass misclassification of dirt/grunge and low-alpha body meshes

- `glassAlphaArg = (tac == TAC_FADED) || (material->color.alpha != 255)` was
  true for the paint marker (16 != 255).
- `VehShaders_IsGlassTexture` (`src/extras/veh_shaders.cpp:239-247`) falls back
  to `alpha < 200 -> true` when no glass name matches.
- `vehiclegrunge*` maps to `SURFACE_CAR_DIRT` (`veh_shaders.cpp:279-280`).
  The old ghost-car guard only reset `isGlassMesh` for
  `surface == SURFACE_CAR_BODY`, so the grunge/dirt meshes stayed "glass" and
  drew through `Glass_Vehicle` = mirror-chrome shell.

## Changes (exact)

### `src/render/vehiclePipe.cpp` — lines 2257-2342 (per-mesh alpha decision)

- `2257-2264`: `matPaintMarker = (material->color.alpha == 16)` and
  `matTranslucent = (material->color.alpha != 255 && !matPaintMarker)`.
- `2274`: `hasAlpha = instancedData->vertexAlpha != 0 || matTranslucent`
  (paint marker excluded; diagnostic/legacy value only).
- `2278-2296`: glass classification HOISTED above the TAC switch:
  `glassAlphaArg = (tac == TAC_FADED) || matTranslucent`;
  `isGlassMesh` now keeps the `alpha < 200` fallback only when `texName` is
  non-empty AND `VehShaders_GetSurfaceType(texName) == SURFACE_CAR_GLASS`.
  Positive window/glass/windscreen name matches are untouched.
- `2307-2320`: `TAC_UNKNOWN` is now opaque: `blendOn = 0; shaderAlpha = 1.0f;`.
- `2321-2328`: a genuinely translucent material (`matTranslucent`, i.e. real
  alpha, not 16) still blends and keeps texture alpha (`shaderAlpha = 0.0f`).
- `2330-2337`: OPAQUE/CUTOUT clamp is `blendOn = matTranslucent ? 1 : 0;`.
- `2339-2342`: glass keeps `blendOn = 1` even when the class is UNKNOWN.
- `2398-2412`: log now prints `paint=%d` and the final `blendOn` / `c48w`:

```
[VehOpacity] tex='vehiclegrunge256' tac=0(UNKNOWN) hasAlpha=? paint=1 blendOn=0 c48w=1.00 matA=16
```

  (expected for `vehiclegrunge256`, `vehiclelights128`, `vehiclegeneric256`).

### `src/render/pipelinecommon.cpp` — `pipeUploadMatCol()` lines 476-494

`RwRGBARealFromRwRGBA` yields alpha `16/255 = 0.0627`; the VS multiplies that
into the outgoing colour alpha. The function is unambiguous (float RGBA), so:

```cpp
if(m->color.alpha == 16)
    color.alpha = 1.0f;     // SA paint marker, not translucency
```

Note: this helper is shared with the building lanes; the clamp only fires on
alpha byte 16 exactly. Building materials with a genuine 16/255 translucency
would change, but main_building's own 0.0-0.1 guard (below) makes the outcome
consistent.

### `shaders/ps/VehiclePBR_Modern.hlsl` — `main_building()` lines 1168-1180

```hlsl
float matA = IN.color.a;
if(matA > 0.0 && matA < 0.1) matA = 1.0;
return float4(max(color, 0.0), lerp(saturate(diff.a * matA), 1.0, nmParams.w));
```

Only `main_building` changed; `main` / `main_rubber` / `main_mobileVehicle`
are untouched. Validated with `python tools/fast_build.py --shaders`:
`7 compiled, 103 up-to-date, 0 failed`.

## Before / after decision table (Env/PBR mesh path)

| Material alpha | TAC | Old blendOn / c48.w | New blendOn / c48.w |
|---|---|---|---|
| 16 (paint marker) | UNKNOWN (0) | 1 / 0.00 → texture alpha (invisible) | **0 / 1.00** |
| 16 | OPAQUE (1) | 1 / 1.00 | **0 / 1.00** |
| 16 | CUTOUT (2) | 1 / 0.00 | **0 / 0.00** |
| 16 | FADED (3) | 1 / 0.00 | 1 / 0.00 (unchanged) |
| 255 | UNKNOWN | 0 / 0.00 | **0 / 1.00** |
| 128 (real translucency) | UNKNOWN | 1 / 0.00 | 1 / 0.00 (unchanged) |
| 128 | OPAQUE | 1 / 1.00 | 1 / 1.00 (unchanged) |
| 128 | CUTOUT | 1 / 0.00 | 1 / 0.00 (unchanged) |
| 255 | FADED | 1 / 0.00 | 1 / 0.00 (unchanged) |
| any, surface == SURFACE_CAR_GLASS | any | glass branch: 1 / 1.00 | glass branch: 1 / 1.00 (forced) |
| `vehiclegrunge*` / `vehiclegeneric256` / `vehiclelights128` | UNKNOWN | glass path (grunge DIRT escaped guard) | **opaque body path** |

## Glass classification before / after

| Texture | Surface | Old | New |
|---|---|---|---|
| `vehiclegrunge256` | SURFACE_CAR_DIRT | glass via alpha<200 fallback → Glass_Vehicle chrome | not glass (fallback requires GLASS surface) |
| `vehiclegeneric256` | SURFACE_CAR_BODY | glass via fallback, then reset only by BODY guard | not glass (same guard, now general) |
| `vehiclelights128` | SURFACE_CAR_BODY | glass via fallback | not glass |
| `*window*` / `*glass*` / `*windscreen*` | SURFACE_CAR_GLASS | glass (positive match) | glass (positive match, unchanged) |
| modded glass, unknown name | SURFACE_CAR_BODY | glass via fallback | not glass (intentional per diagnosis) |

## Residual issue — "hovering vehicles" (NOT addressed here)

Observed symptom: a vehicle body sits above its shadow/wheels ("hovering"),
sometimes only for part of the frame.

Working hypothesis: **transform / draw-order**, not a leftover reflection copy.
The envmap lane renders roads + buildings only, so an envmap copy of the car
cannot be the floating body. Likely candidates, in probe order:

1. Stale world matrix: `c24-c27` (VS world matrix) and/or the WVP translation
   are uploaded once per callback / inherit the previous atomic's matrix in
   the instanced mesh loop (`pipeWorldMat` concatenation). A previous
   atomic's Z translation would offset the whole vehicle.
2. Draw-order inversion between the body and the wheel/shadow passes, e.g. the
   dual pass (`D3D9RenderDual`) running with a stale eye position (`c36`) or a
   PostFX depth/SSAO pass reading a mismatched depth buffer.
3. Vehicle matrix update timing: Env cb runs before the game commits the
   vehicle's new matrix for the frame, so the body lags the wheels by one
   frame during suspension movement.

Suggested next probe: for one vehicle, log per Env-cb invocation
`RwFrameGetLTM(atomic frame)` translation, the `c24-c27` world matrix
translation actually uploaded, the WVP (c0-c3) translation, and the camera
position, then compare against the vehicle's wheel contact points and its
shadow position on the same frame. Toggle the dual pass off for one frame to
see whether the floating copy disappears (that discriminates candidate 2);
compare `frame counter` of body vs wheel draws for candidate 3.
