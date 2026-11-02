# Recovered lane findings + opacity fix plan (2026-10-24)

Consolidated from the three collapsed wave-3 explorer lanes (recovered via the
read-only OpenCode DB) plus a direct orchestrator code read. **Read this before
re-deriving any of it.**

Recovery artefacts (temp): `recovered_lanes.txt` (854 KB full dump), `digest_exp-3.txt`,
`digest_exp-4.txt`, `digest_exp-5.txt`.

---

## 1. User's current #1 priority (verbatim m0391)

> "figuring out why cj peds and cars look liek that main rpiroty they shoudl all be
> opaque and render in correct order currently it feil sliek veegtation is being
> drawn over evertyhgin!"

Correction from m0383: **there is no giant-ped bug** — the intro-cutscene weirdness is
camera-angle only. Do not chase exploded geometry.

---

## 2. Ped / car / vegetation opacity — the live decision points

### vehiclePipe.cpp:2229-2302 (Env/Modern PBR callback, per mesh)

```
hasAlpha   = instancedData->vertexAlpha != 0 || material->color.alpha != 255;   // :2244
tac        = texAlphaClassify ? TexAlpha_Classify(material->texture) : TAC_UNKNOWN; // :2246
TAC_OPAQUE  -> blendOn = 0, shaderAlpha = 1.0
TAC_CUTOUT  -> blendOn = 0, shaderAlpha = 0.0
TAC_FADED   -> blendOn = 1, shaderAlpha = 0.0
TAC_UNKNOWN -> blendOn = hasAlpha ? 1 : 0, shaderAlpha = 0.0
if(material->color.alpha != 255) blendOn = 1;                                   // :2259
```

- `c48.w = shaderAlpha` is written on **both** the normal-map-bound and else paths
  (:2285 / :2288), so the shader override is live.
- Blend writes: `RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, blendOn)` (:2296)
  and L2 `RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendOn)` (:2302).
- Glass classification follows at :2312-2329 via
  `VehShaders_IsGlassTexture(texName, hasAlpha, material->color.alpha)`; `hasAlpha`
  is still passed, so an UNKNOWN/vertexAlpha texture can be promoted to glass.

### buildingPipe.cpp:2090-2172 (PBR building callback, per mesh)

```
hasAlpha = vertexAlphaIsAlpha ? (instancedData->vertexAlpha || material->color.alpha != 255)
                              : (material->color.alpha != 255);               // :2105-2109
tac / blendOn / shaderAlpha  -> same table as above                            // :2111-2125
c48.w written on both nm paths                                                 // :2149 / :2152
blend writes                                                                   // :2166 / :2172
```

**Trees/vegetation ride this building PBR callback**, not a separate vegetation pipe:
`TreeAlpha_ApplyAlphaRef(atomic, rs.alpharef)` runs before the mesh loop (:2088) and
the tree meshes are then blended per the table above.

### Peds

Peds do **not** use either PBR block. They draw through `myDefaultCallback`
(`main.cpp:861-1111`) onto the game's skin pipeline. Ped alpha is governed by the
default-callback entry contract (`pipeEnterAlphaMode` / `pipeForceAlphaBlock`), the
ZWRITE sticky repair, and `defaultCbShaderHygiene`. Ped transparency therefore points
at a stale blend/ZWRITE or a leaked shader over the skin draw — not at texalpha.

### Diagnosed failure modes for "vegetation drawn over everything"

1. **TAC_FADED false positives.** Any texture with some intermediate alpha bytes is
   classified FADED → `blendOn = 1` → the mesh moves to the transparent pass and sorts
   after opaque geometry. Vegetation atlases (leaf clusters on a partly transparent
   sheet) commonly have intermediate alpha *around the cutout*, so a naive "any
   intermediate byte" rule misclassifies solid-ish foliage detail as FADED.
2. **TAC_UNKNOWN + vertexAlpha.** Legacy fallback keeps the SA convention
   (vertexAlpha != 0 ⇒ blend), and SA sets vertexAlpha on whole meshes. That is the
   original whole-mesh inversion the classifier was meant to remove.
3. **Cars promoted to glass.** `VehShaders_IsGlassTexture(..., hasAlpha, materialAlpha)`
   still sees `hasAlpha`; a body texture that is UNKNOWN + vertexAlpha can take the
   glass path even though `blendOn` was computed independently.
4. **Cutout sheets must keep blend OFF with alpha test ON** — TAC_CUTOUT already does
   this; if the next log shows vegetation as CUTOUT and still over-draws, the cause is
   ZWRITE being cleared rather than blend.

---

## 3. Giant translucent screen-space overlay (partially recovered)

exp-3's evidence: large **axis-aligned screen-space blocks (~150-200 px) covering the
whole frame including the sky, with a diagonal boundary** — the geometry is not world
water, it is a screen-space composite.

Candidate A — **Radiosity_shader** (`postfx.cpp:990-1115`). The code itself documents
the failure: a transient camRas read "composites the radiosity buffer at the wrong
scale and produces the **full-screen blocky mismatch**". That exact wording matches the
artifact. Note: the current source already carries a "trusted cache" mitigation
(`GetScreenSize` for `drawW/drawH`, `pRasterFrontBuffer` 2048² padded vs 1920×1080
screen), so the remaining question is whether the pass runs at all — exp-3 found **no
`Radiosity` / `Radiosity_shader` tag anywhere in the tested log**. Either radiosity is
disabled at runtime or its throttled tag never fired. Must be instrumented, not guessed.

Candidate B — **water at tier 0 from altitude** (see §4). exp-5 concluded the big
translucent sheet seen from the Hunter is largely the water surface converging to
horizon colour at distance, which can look like a full-screen haze band but would not
cover the sky with axis-aligned blocks.

Candidate C — **PipeChain pass 3**. `pipeChainEnable=1` with `pipeChainReflEnable=0`
still runs the chain; with the reflection half disabled it binds the NoPack dummy and
composites blur/contrast, so it should be gain-only. A/B with `pipeChainEnable=0`
remains the cheapest discriminator.

Candidate D — **CClouds triangles** (ZTEST=0/ZWRITE=0 contract, REPLACE risk). Produces
large flat coloured triangles, matching the earlier "violet-blue wedge", but not
typically axis-aligned blocks.

---

## 4. Water at tier 0 — recovered from exp-5

Full doc: `docs/plans/2026-10-24-water-tier0-diagnosis.md` (41,269 B, exists on disk).
Essence:

- Flat sheet at altitude is **by design at tier 0**: `distFactor` saturates with view
  distance so the normal term collapses to `waterParams.x * (1-0.85)` ≈ 15 %, the detail
  texture mip footprint averages flat, `FarBlendWater` converges to horizon colour, and
  Fresnel reflection is small → flat opaque-ish sheet of deep/horizon colour.
- Water body classification works (WbClassifyFlush writes a body code into the normal
  channel; log histogram `ocean=476`, `pond=4`).
- Prescribed fixes:
  - `src/render/postfx_underwater.cpp:103` `kUWColorDepth` **12.0 → 20.0**.
  - `src/render/postfx_underwater.cpp:91` `kUWSSSGain` **0.08 → 0.30**.
  - Water tier constants: `waterCol*2.0` only exists in the V tier ≥ 2 path
    (waterPipe.cpp ≈:1253); tier 0 has no equivalent lift.
  - `waterUseTimecycle=0` with a matching `waterDeep*` / `waterShallow*` palette gives
    direct control instead of the vanilla timecycle colours.
  - Doc contains a tier-0 feature matrix and an A/B plan.

Current live INI has `waterStyle=Xbox`, `waterQuality=0`, `waterParallaxEnable=1`.

---

## 5. Washout / pastel grading — exp-4 verdict

exp-4 suspected stale luminance CSOs. **Direct mtime check disproves it:**

| file | mtime |
|---|---|
| `resources/cso/LuminanceReduce.cso` | 2026-09-10 21:16 |
| `resources/cso/LuminanceReduce.hlsl` | 2026-09-10 20:51 |
| `resources/cso/LuminanceAdapt.cso` | 2026-09-10 21:16 |
| `resources/cso/LuminanceAdapt.hlsl` | 2026-09-10 20:51 |
| `resources/cso/TonemapPass.cso` | 2026-10-24 19:08 (current source) |
| `resources/cso/PipeChain.cso` | 2026-10-23 20:48 |

CSO is newer than HLSL in every pair → nothing stale. Keep the washout on the open list;
the knee now gates on the real pack bind, and `tonemapMaxExposure` is in range.

---

## 6. Fix plan (execution order)

1. **Opacity correctness (user priority).**
   - Tighten `TexAlpha_Classify`: CUTOUT when the alpha histogram is dominated by the
     0x00 and 0xFF extremes with only a thin antialiased band; FADED only when a
     *substantial* fraction (≥ ~35 %) of texels sits in the intermediate range; ignore
     the outer 1-texel border when sampling blocks. Cap work per texture.
   - Force `blendOn = 0` for TAC_OPAQUE and TAC_CUTOUT, and for TAC_FADED only when
     the intermediate fraction is above the threshold; never let `vertexAlpha` alone
     turn a classified-OUT texture translucent.
   - Vehicle: body materials must be opaque unless `material->color.alpha != 255` or
     the texture is classified FADED above threshold or glass detection fires from the
     *texture name*, not from `hasAlpha`.
   - Building/trees: same, with the tree path keeping alpha **test** (CUTOUT) and
     ZWRITE on.
   - Add one-shot per-class logs naming the first N textures per class so the next run
     can confirm the classification instead of guessing.
2. **Radiosity / overlay instrumentation.** One-shot log at `Radiosity_shader` entry
   with `m_bRadiosity`, filter/render passes, `pRasterFrontBuffer` vs `GetScreenSize`
   dims; if the pass runs while the dims differ, gate the composite (skip) and log it.
   Verify whether the pass is invoked at all in a normal frame.
3. **Water tier 0 + underwater** constants from §4.
4. **One integrated build** (`python tools/fast_build.py`), binary string verify
   (`[BUILD]`, `state_baseline`, `defcb_shader`, `texalpha`, `pointlights_install`,
   `corona_resync`, `Pipeline_InvalidateShaders`, `vs_probe`, theme strings),
   deploy, then hand to the user with INI bisect levers:
   `texAlphaClassify=0`, `dualPassPed=0`, `defaultCbShaderHygiene=0`,
   `radiosityEnable=0`, `pipeChainEnable=0`, `waterParallaxEnable=0`.
