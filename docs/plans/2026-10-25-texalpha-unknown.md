# TexAlpha UNKNOWN = NULL `RpMaterial::texture` — investigation

Date: 2026-10-25
Scope: `src/render/texalpha.cpp`, `src/render/buildingPipe.cpp`,
`src/render/vehiclePipe.cpp`, latest `skygfx_dbg.log`.

## Verdict

1. **The classifier is not being called in the wrong place.** All three render
   call sites are inside the per-mesh loop, after the mesh material is known
   and after the diffuse texture bind. `TexAlpha_Classify` receives the same
   `material->texture` pointer that was just bound.
2. **The NULL is real and legitimate.** `unknown_reasons: tex=...` decodes to
   `TACR_NO_TEXTURE`, i.e. `!tex` — not a raster/extraction failure. SA has a
   large population of untextured/flat materials (car paint/trim, untextured
   map geometry, procedural atomics). For those, a NULL diffuse texture is a
   valid material state, not a classifier failure.
3. **The draw itself is not texture-less on the GPU.** The pipes substitute
   the game's opaque white texture for NULL:
   `pipeSetTexture()` falls back to `gpWhiteTexture`
   (`src/skygfx.h:1272`), and `vehEnvBindTexture()` does the same
   (`src/render/vehiclePipe.cpp:2205-2218`). The mesh is drawn as
   white × material/vertex colour, i.e. a flat material colour.
4. **`TAC_OPAQUE` is the correct texture-alpha class for a NULL diffuse
   texture**, because there is no texture alpha to preserve. The current
   `TAC_UNKNOWN` path is the wrong fallback: it re-enters the legacy
   `hasAlpha` decision, which can enable blending / the dual pass for
   paint-marker alpha `16` and vertex-alpha meshes, and leaves `c48.w = 0`
   (texture alpha) instead of forcing alpha `1`.
5. **The visible impact is conditional, not universal.** The only NULL mesh
   sampled by `[VehOpacity]` in the latest log already resolved to
   `blendOn=0, c48w=0.00, matA=255` (line 20689), so switching it to OPAQUE
   is visually neutral for that mesh (white texture alpha is already 1).
   NULL meshes whose material alpha is `16` or whose vertex alpha is set are
   the ones whose blend state would change.
6. **Recommendation: (a)** — classify `!tex` as `TAC_OPAQUE` in
   `src/render/texalpha.cpp:789-795`; keep `!tex->raster` as UNKNOWN/retry.
   Risk: untextured meshes that intentionally use vertex alpha as opacity
   will become solid. Material-alpha translucency (`matTranslucent`) is
   preserved by the existing overrides.

---

## 1. What the summary counters actually mean

`TacLogSummary()` (`src/render/texalpha.cpp:751-767`) prints:

```text
%u opaque, %u cutout, %u faded, %u unknown, %u faults
(cached %d/%d) unknown_texnull=%u unknown_budget=%u
unknown_reasons: tex=%u raster=%u ext=%u extptr=%u
                 d3d=%u leveldesc=%u lock=%u format=%u budget=%u
```

Key semantics from the implementation:

| Counter | Incremented where | Meaning |
|---|---|---|
| `opaque/cutout/faded` | `texalpha.cpp:864-869`, only after a **new** `TacClassifyTex` | number of newly classified **textures** (not draws) |
| `unknown` | `texalpha.cpp:792`, only on the NULL early-return | number of NULL-texture mesh **calls** (per frame, cumulative) |
| `unknown_texnull` | same branch (`:793`) | covers both `!tex` and `!tex->raster` |
| `unknown_budget` / `budget` | `texalpha.cpp:836` | per-frame budget skips; **not** added to `s_tacUnknown` |
| `unknown_reasons` | `TacNoteReason()` | `tex` = `TACR_NO_TEXTURE` (`!tex`), `raster` = `TACR_NO_RASTER` (`!tex->raster`) |

Consequence: the "unknown" figure cannot be compared with the class figures.
`opaque+cutout+faded` counts distinct/new textures; `unknown` counts every
NULL-texture mesh submission every frame. Cache hits for textured meshes are
not counted at all. The apparent "84% unknown" ratio
(`152,749 / 181,631` in the latest log) is a counter-unit mismatch, not a draw
fraction.

This is also why the user's `~33,912 UNKNOWN` looks enormous: it is a
cumulative per-draw counter, not 33,912 unique textures.

---

## 2. Call sites and sampling point

`grep` finds three render call sites plus one probe:

| Site | Function | Texture bind | Classify call | What is passed | NULL possible? |
|---|---|---|---|---|---|
| `buildingPipe.cpp:1800` | Xbox building cb | `pipeSetTexture(material->texture,0)` at `:1776` | `:1800` | raw `material->texture` | yes |
| `buildingPipe.cpp:2508` | PBR building cb | `pipeSetTexture(material->texture,0)` at `:2477` | `:2508` | raw `material->texture` | yes |
| `vehiclePipe.cpp:2640` | `CustomPipeRenderCB_Env` | `vehEnvBindTexture(diffuseTex,0)` at `:2611` (`diffuseTex` already resolved to `gpWhiteTexture`) | `:2640` | raw `material->texture` | yes |
| `main.cpp:906` | `atomicHasAlpha` ped probe | n/a | `:906` | `tm->texture` | no (`tm->texture` guard at `:905`) |

The Xbox vehicle callback (`vehiclePipe.cpp:1416-1675`) does **not** call the
classifier. Therefore the pre-Modern unknown mass (`126,577` at 22:37:22) is
from the **Xbox building callback**, not from vehicles.

Timing is correct: every render site classifies after the bind, inside the
mesh loop, with the material that is about to be drawn. The classifier does
not read the bound `IDirect3DTexture9`; it reads the `RwTexture*` argument, so
the bind order is irrelevant to what it sees. The mismatch is semantic: the
pipe binds `gpWhiteTexture` for NULL, but the classifier is shown the raw NULL
and treats it as a failure.

`TexAlpha_Classify()` NULL branch (`src/render/texalpha.cpp:789-796`):

```cpp
if(!tex || !tex->raster){
    int reason = tex ? TACR_NO_RASTER : TACR_NO_TEXTURE;
    TacNoteReason(reason, tex, TAC_EXT_OFF_UNREAD, -1);
    s_tacUnknown++;
    s_tacUnknownTexNull++;
    TacLogSummary();
    return TAC_UNKNOWN;
}
```

The branch deliberately runs **before** the per-frame budget (`:821-838`) and
before the cache probe (`:798-819`), so NULL draws neither consume the budget
nor get cached. That design is why the current log has `budget` starvation
mostly separated from the NULL mass.

---

## 3. Can `material->texture` legitimately be NULL?

RenderWare stores the diffuse texture on `RpMaterial`; an `RpAtomic` /
`RpGeometry` does not own a diffuse raster. Detail maps (`s2`), environment
maps (`s1`), IBL (`s3`) and normal maps (`s8`) are separate stages and are not
the source of diffuse texture alpha. There is no "atomic-owned diffuse
raster" path in this codebase.

| Mesh category | Can `material->texture` be NULL? | Evidence |
|---|---|---|
| **Car paint** | **Yes — documented valid SA state.** Paint is material colour + env map; geometry can still carry UVs and the textured flag. | `vehiclePipe.cpp:1645-1656`: “SA paint meshes are flagged textured but carry `material->texture == NULL` — paint is material colour + env map”. Same statement at `:2651-2655`. The code already resolves NULL to `gpWhiteTexture` for the draw. |
| **Chrome / trim** | **Yes.** Stock SA puts an env map on paint; trim can be texture-less and colour-driven. | `vehiclePipe.cpp:3275-3278`, `skygfx.h:805` (“untextured chrome trim”). |
| **Light meshes** | Usually textured (`vehiclelights128`), but a flat material-colour light is legal. | The light path is name-driven (`VehShaders_IsHeadlightTexture(texName)`); a NULL texture has an empty name and falls through to the opaque path. |
| **Glass meshes** | Usually textured; a texture-less glass mesh can only be recognised by material alpha. | `vehiclePipe.cpp:2659-2661` requires a non-empty name and `SURFACE_CAR_GLASS` for the name-based glass path; `matTranslucent` (`:2652`) remains the only signal for a NULL glass mesh. |
| **Procedural / atomic geometry** | **Yes.** Generated/flat geometry has no reason to own a texture. | RW material model; `pipeSetTexture`/`vehEnvBindTexture` NULL fallbacks. |
| **Grass / imposters** | Textured cards are normally CUTOUT. Untextured cards/vertex-coloured imposters can be NULL. | WIND lane sets `vertexAlphaIsAlpha = false` (`buildingPipe.cpp:1734-1737`, `:2307-2317`), so vertex alpha is wind weight, not opacity; a NULL WIND card is still flat-colour. |
| **Texture in the atomic's own raster** | **Not a RenderWare case.** No atomic/geometry diffuse raster exists. | `RpMaterial::texture` is the only diffuse source; detail/env/IBL/NM are separate stages. |
| **Missing/failed TXD** | At draw time it is indistinguishable from a deliberately untextured material. | The classifier sees `tex == NULL`; the draw gets `gpWhiteTexture`. `raster` failures (`TACR_NO_RASTER`) are a different reason and are zero in the latest log. |

The game's/legacy pipe's own behaviour for an untextured material is the same:
bind the white fallback (or bind nothing and use the diffuse stage) and derive
`hasAlpha` from vertex/material alpha, not from a texture:

- `buildingPipe.cpp:1286-1306` (PS2 building cb): `pipeSetTexture(material->texture,0)`,
  then `hasAlpha = vertexAlpha || material->color.alpha != 255`.
- `shaders/buildingPipePS.hlsl:25-28` (`main_simple`): returns
  `tex2D(s0) * IN.color * colorscale`. With `gpWhiteTexture` this is the
  material/vertex colour, alpha = vertex colour alpha.
- `vehiclePipe.cpp:1654-1656` and `:2611` already implement the NULL → white
  fallback for vehicles.

So the renderer does draw a NULL material as a flat material colour. The
classifier should classify that as OPAQUE (no texture alpha), not UNKNOWN.

---

## 4. Classification table

Notation: **Bld-X** = `buildingPipe.cpp` Xbox callback (`:1800`),
**Bld-PBR** = PBR building callback (`:2508`),
**Veh-Env** = `vehiclePipe.cpp` Env callback (`:2640`).
`matTranslucent` excludes the paint marker (`material->color.alpha == 16`);
`hasAlphaLegacy` includes raw material alpha (paint marker included).

| Case | Current class | Current `blendOn` / `shaderAlpha` | Correct class | Visible consequence of correcting |
|---|---|---|---|---|
| `material->texture == NULL` — plain untextured/flat material, building or vehicle | `TAC_UNKNOWN` (`TACR_NO_TEXTURE`) | Bld-X: `blendOn = vertexAlpha || matA != 255`; no `c48.w` (PS uses `tex.a × vertex alpha`). Bld-PBR: `blendOn = vertexAlpha || matTranslucent`, `shaderAlpha = 0`. Veh-Env: `blendOn = vertexAlpha || raw matA != 255` (includes paint marker 16), `shaderAlpha = 0` | **`TAC_OPAQUE`** | Bld-PBR/Veh-Env: `(blendOn, shaderAlpha) = (0, 1.0)` for non-translucent, `(1, 0.0)` if `matTranslucent`; glass override can still force `blendOn=1`. Bld-X: `blendOn = 0` for non-translucent, no c48. Removes unnecessary blend + `D3D9RenderDual` second pass; output alpha forced 1. Sampled NULL mesh (`[VehOpacity]`, line 20689, matA=255) was already `blendOn=0` and white alpha 1, so **no visible change there**. |
| `material->texture == NULL` — car paint (`matA == 16` paint marker) | `TAC_UNKNOWN` | Veh-Env: `blendOn = 1` because `hasAlphaLegacy` includes raw alpha 16; `shaderAlpha = 0`. Bld-PBR: `blendOn = 0` (paint marker not in `matTranslucent`), `shaderAlpha = 0` | **`TAC_OPAQUE`** | Veh-Env: `blendOn = 0`, `shaderAlpha = 1.0`, matching the documented expected `[VehOpacity] … paint=1 blendOn=0 c48w=1.00` (`vehiclePipe.cpp:2853-2855`). Removes the paint-marker blend / dual-pass path. White texture alpha was already 1, so the painted surface was usually not see-through; the change is state/perf and removes a latent inconsistency. |
| `material->texture == NULL` — light / glass / trim | `TAC_UNKNOWN` | Veh-Env: same as above; if a name-based glass/light branch is taken it later overrides `c48.w=1.0` and may set `blendOn=1` | **`TAC_OPAQUE`** | Ordinary opaque light/trim: `blendOn=0`, `shaderAlpha=1.0`, correct flat colour. A NULL glass mesh with `matA != 255` is still blended via `matTranslucent`; a NULL glass mesh with `matA == 255` becomes opaque (it has no texture/name/material-alpha signal, so the game would draw it flat too). |
| `material->texture == NULL` — procedural/atomic geometry | `TAC_UNKNOWN` | same as building | **`TAC_OPAQUE`** | Flat colour drawn opaque; correct because there is no texture alpha. Intentional vertex-alpha fade is the risk (see §6). |
| `material->texture == NULL` — grass/imposter card | `TAC_UNKNOWN` | Bld-X WIND: `blendOn = matA != 255`; Bld-PBR WIND: `blendOn = matTranslucent`, `shaderAlpha = 0` | **`TAC_OPAQUE`** | Bld-X WIND unchanged (already material-alpha only). Bld-PBR WIND: `shaderAlpha` becomes 1 — correct, because WIND vertex alpha is wind weight, not opacity. Textured grass remains CUTOUT. |
| `texture != NULL`, `texture->raster == NULL` | `TAC_UNKNOWN` (`TACR_NO_RASTER`) | legacy `hasAlpha` fallback | **`TAC_UNKNOWN` (keep)** | Not texture-less: the texture may be streaming in. Do not force OPAQUE; retry on a later frame. Latest log has `raster=0`. |
| Non-NULL texture, format has no alpha | `TAC_OPAQUE` | `(0, 1.0)` / Bld-X `blendOn=0` | `TAC_OPAQUE` | No change. |
| Non-NULL texture, unused/degenerate alpha | `TAC_OPAQUE` | `(0, 1.0)` / Bld-X `blendOn=0` | `TAC_OPAQUE` | No change. |
| Non-NULL hard 0/255 mask | `TAC_CUTOUT` | `(0, 0.0)` / Bld-X `blendOn=0` | `TAC_CUTOUT` | No change. |
| Non-NULL ≥60% intermediate | `TAC_FADED` | `(1, 0.0)` | `TAC_FADED` | No change. |

Resulting mapping for the recommended NULL → OPAQUE (current table):

| Pipe | `blendOn` | `shaderAlpha` (c48.w) |
|---|---|---|
| Bld-X (`buildingPipe.cpp:1809-1818`) | `matTranslucent ? 1 : 0` | no c48 slot |
| Bld-PBR (`buildingPipe.cpp:2523-2551`) | `matTranslucent ? 1 : 0` | `matTranslucent ? 0.0 : 1.0` |
| Veh-Env (`vehiclePipe.cpp:2676-2717`) | `matTranslucent ? 1 : 0`; glass override may set 1 | `matTranslucent ? 0.0 : 1.0`; glass/light/tire branches override to 1.0 |

The existing overrides at `buildingPipe.cpp:2540-2551` and
`vehiclePipe.cpp:2696-2717` preserve genuine material translucency; the paint
marker (`matA == 16`) is explicitly excluded from `matTranslucent`.

---

## 5. Quantified evidence (latest on-disk log)

Log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (mtime 2026-10-25 22:42,
6.15 MB). The summary is throttled to one line per ~2 s
(`dbglog_throttle("texalpha")`). The counters were reset by a device reset at
22:35:41 (`[RENDER] camera-raster cache invalidated`); the run then continued
to 22:42:46.

### 5.1 Latest summary (line 63382, 22:42:46.339)

```text
texalpha: 26372 opaque, 2505 cutout, 5 faded, 152749 unknown, 0 faults
(cached 2404/4096) unknown_texnull=152749 unknown_budget=400
unknown_reasons: tex=152749 raster=0 ext=0 extptr=0 d3d=0 leveldesc=0
lock=0 format=0 budget=400
```

Interpretation:

- **All 152,749 counted UNKNOWNs are `!tex`** (`tex=` reason). No raster, ext,
  extptr, d3d, leveldesc, lock or format failures; `faults=0`.
- **Cache occupancy: 2,404 / 4,096 = 58.7%.**
- **Budget starvation: 400 skips.** Real, but 400 / 152,749 = **0.26%** of the
  NULL mass. Budget skips return UNKNOWN but are not counted in `unknown`, so
  total UNKNOWN returns are 152,749 + 400 (plus any cached extraction
  failures, of which there are none).
- First NULL in the run: line 1053, 22:35:52.707:
  `texalpha: first NO_TEXTURE tex='?' raster=00000000 extOff=0xFFFFFFFF fmt=-1`
  — proof that the pointer itself is NULL, not a raster/lock failure.

### 5.2 The `~33,912` figure

The exact value `33,912` is not present in either on-disk log. The nearest
snapshot is line 6514 at 22:36:20.621:

```text
texalpha: 4966 opaque, 671 cutout, 2 faded, 34174 unknown, 0 faults
(cached 1599/4096) unknown_texnull=34174 unknown_budget=371
unknown_reasons: tex=34174 raster=0 ext=0 extptr=0 d3d=0 leveldesc=0
lock=0 format=0 budget=371
```

The conclusion is the same: `tex=unknown`, all other reasons zero. The
absolute figure is cumulative-since-reset, not a per-frame count.

### 5.3 Is it a large fraction of draws?

The NULL counter is per mesh submission; the class counters are per newly
classified texture. To estimate the draw fraction, use the callback counter
(`XboxBuilding`/`PBRBuilding` log every 300 callbacks):

- At the 34,174 snapshot: Xbox frame counter ≈ 1,455,000
  (`XboxBuilding: frame 1450501` at 22:36:20.518, just before the summary at
  22:36:20.621). 34,174 / 1,455,000 ≈ **2.3% of Xbox building callbacks**
  (if one mesh per callback; lower with more meshes per callback).
- At the latest snapshot: Xbox counter ≈ 6,019,000
  (`XboxBuilding: frame 6019201` at 22:42:46.474), PBR ≈ 39,601.
  152,749 / 6,059,000 ≈ **2.5% of Xbox+PBR building callbacks**.
- Peak NULL rate: 5,276 submissions between 22:36:18.624 and 22:36:20.621
  (~2,638/s). Late rate: 1,173 between 22:42:44.352 and 22:42:46.339
  (~590/s). Average since reset: 152,749 / 425 s ≈ 359/s.
- The 84% ratio in §1 is `152,749 / (152,749 + 26,372 + 2,505 + 5)`; it is a
  per-draw vs per-texture mismatch, not a draw fraction.

**Best estimate:** the texture-less submissions are a low-single-digit
percentage of building mesh draws (~1-2.5% if one mesh per callback; lower
with multi-mesh atomics). They are not "every draw" and not 85% of draws.

### 5.4 What blends today?

The log does not print `blendOn` for every NULL draw, but the one sampled NULL
vehicle mesh is unambiguous (line 20689, 22:37:22.954):

```text
[VehOpacity] tex='' tac=0(UNKNOWN) hasAlpha=0 paint=0 blendOn=0 c48w=0.00
matA=255 legacy=1
```

So at least this NULL mesh is **not** blending. Under the current table the
cases that do blend are:

- Bld-X: NULL mesh with `vertexAlpha != 0` or raw `matA != 255` (paint marker
  16 included).
- Bld-PBR: NULL mesh with `vertexAlpha != 0` or `matTranslucent`.
- Veh-Env: NULL mesh with `vertexAlpha != 0` or raw `matA != 255` (paint
  marker 16 included); glass override can also force blend.

The classifier's own documented expected line for a paint-marker mesh is
`paint=1 blendOn=0 c48w=1.00 matA=16` (`vehiclePipe.cpp:2853-2855`); a NULL
paint mesh today would log `paint=1 blendOn=1 c48w=0.00 legacy=1`. That is the
clearest visible/state regression in the current fallback.

### 5.5 Cache occupancy anomaly (secondary)

The class counters sum to 28,882 new-texture classifications, but
`cached = 2,404`. Since `s_tacUsed` increments only on empty-way installs,
28,882 installs should have filled all 4,096 slots before evicting. The skew
means either heavy eviction/reclassification or a cache reset without a
counter reset. Note the deployed `skygfx.asi` (21:57) predates the last
`texalpha.cpp` edit (22:32); the current source's `TexAlpha_ResetCache()`
resets the counters (`:887-906`). **Rebuild before trusting the cache
occupancy number.** This does not affect the NULL diagnosis.

### 5.6 Budget starvation verdict

- `unknown_budget = 400`, `budget = 400`: **real budget starvation exists**,
  but it is 0.26% of the NULL mass.
- All *counted* `unknown` is `tex`-NULL. The 400 budget skips are separate:
  they returned UNKNOWN for real textures and took the legacy fallback, but
  are not included in `s_tacUnknown`.
- The previous run (`skygfx_dbg.prev.log`, 16:43) still shows the old
  starvation pattern (`unknown=2,237`, `budget=1,229`); the current build's
  null-before-budget + 256/frame budget has largely fixed it.

---

## 6. Recommendation

### Single change: classify `!tex` as `TAC_OPAQUE`

File/line: **`src/render/texalpha.cpp:789-795`**.

```cpp
// Current:
if(!tex || !tex->raster){
    int reason = tex ? TACR_NO_RASTER : TACR_NO_TEXTURE;
    TacNoteReason(reason, tex, TAC_EXT_OFF_UNREAD, -1);
    s_tacUnknown++;
    s_tacUnknownTexNull++;
    TacLogSummary();
    return TAC_UNKNOWN;
}

// Recommended:
if(!tex){
    // No diffuse texture: a valid SA state (untextured/flat material, car
    // paint, light/trim). The pipes bind gpWhiteTexture (opaque white), so
    // there is no texture alpha to preserve. OPAQUE prevents the legacy
    // hasAlpha fallback from enabling blending for paint-marker alpha 16 /
    // vertex-alpha meshes and lets c48.w force alpha 1 in the PBR paths.
    return TAC_OPAQUE;
}
if(!tex->raster){
    // Texture exists but its raster is not available (streaming/reload).
    // Keep UNKNOWN so the mesh is retried on a later frame.
    int reason = TACR_NO_RASTER;
    TacNoteReason(reason, tex, TAC_EXT_OFF_UNREAD, -1);
    s_tacUnknown++;
    s_tacUnknownTexNull++;
    TacLogSummary();
    return TAC_UNKNOWN;
}
```

No pipe call-site change is required. The existing `matTranslucent` and glass
overrides already preserve genuine translucency.

### Why this is the minimal correct change

- It fixes the class at the source, so all three call sites and any future
  caller get the same answer.
- It keeps `!tex->raster` as a real retryable failure; the latest log has zero
  such cases, so it does not affect the current numbers.
- It removes the misleading `unknown_texnull` growth. After the change the
  summary should show `unknown_texnull=0`, `unknown_reasons: tex=0`, and
  `unknown` approximately equal to `unknown_budget` (plus any real extraction
  failures).
- It matches the classifier's own contract: `TAC_OPAQUE` is documented as
  “alpha is unused / not a trustworthy mask → force shader alpha 1”
  (`src/render/texalpha.h:13-27`).

### Risk

1. **Vertex-alpha-only fades on untextured meshes.** OPAQUE forces
   `shaderAlpha=1` and `blendOn=0` in the PBR paths; a NULL-texture mesh that
   deliberately uses vertex alpha as opacity (e.g. an untextured LOD fade or
   untextured translucent decal) will become solid. The module already does
   this for every *classified* texture (`buildingPipe.cpp:2545-2551`,
   `vehiclePipe.cpp:2705-2712`), so the change extends the existing contract
   to untextured meshes. True material alpha (`matTranslucent`) still blends.
   If an A/B shows a lost fade, the correct follow-up is a dedicated
   `TAC_NO_TEXTURE` class mapped to “legacy vertex/material alpha + forced
   alpha 1”, not a return to UNKNOWN.
2. **Bld-X has no `c48.w` slot.** OPAQUE fixes the CPU blend/dual-pass state
   but cannot force pixel alpha in the Xbox building PS; a vertex-alpha-0
   surface can still be alpha-tested away there. This is an existing Xbox
   limitation, not introduced by the change.
3. **Diagnostics/docs.** `unknown_texnull` and the `NO_TEXTURE` reason become
   unused for `!tex`. Update the comments at `texalpha.cpp:38-40`,
   `:164-170`, `texalpha.h:23-37`, and any baseline doc that quotes
   `unknown_texnull`. Add a separate `nullOpaque` counter if the NULL draw
   rate is still wanted.
4. **Config-off parity.** `config->texAlphaClassify == 0` still returns
   UNKNOWN for everything (`texalpha.cpp:780-781`); the legacy path is
   unchanged for users who disable the feature.

### Why not (b) `blendOn=0` in the pipe callbacks

- It must be duplicated in three places: `buildingPipe.cpp:1809` (Xbox),
  `buildingPipe.cpp:2523` (PBR), `vehiclePipe.cpp:2676` (Env); each has its
  own `matTranslucent`/glass override to replicate.
- It leaves the classifier's contract wrong and the summary misleading; a
  future caller (or the ped probe) still gets UNKNOWN for a valid material.
- It does not fix `shaderAlpha=0` unless each site also writes `1.0`, and the
  Xbox site has no `c48` slot at all.

### Why not (c) neither

The only sampled NULL mesh was visually unchanged (`blendOn=0`, white alpha
1), so “no visible bug in this sample” is true. But the code path for
paint-marker alpha 16 / vertex-alpha NULL meshes demonstrably enables
blending and `D3D9RenderDual` (`main.cpp:252-292` gates the dual pass on
`D3DRS_ALPHABLENDENABLE`), and the documented expected paint-marker line is
`blendOn=0`. Keeping UNKNOWN leaves a latent state bug and a summary that
looks like 84% failure when the real cause is a valid untextured material.
Not recommended.

---

## 7. Secondary observations / follow-ups

1. **Rebuild before re-measuring.** `skygfx.asi` is 21:57; `texalpha.cpp` is
   22:32. The cache/counter skew in §5.5 may be a stale-build artifact.
2. **Xbox vehicle cb raw NULL bind.** The baseline run's R2-04 fix already
   changed the textured branch to `pipeSetTexture(material->texture, 0)`
   (`vehiclePipe.cpp:1654-1656`). Keep that; a raw
   `RwD3D9SetTexture(NULL, 0)` is a cache no-op and can sample the previous
   pass's raster.
3. **Ped probe budget.** `atomicHasAlpha` (`main.cpp:887-920`) calls the
   classifier for every non-NULL ped material before the pipe callbacks. It
   does not create NULL unknowns (guarded by `tm->texture`), but it can
   consume the per-frame budget. If budget skips matter later, move the probe
   after the draw or give it a separate budget.
4. **Counter semantics.** Consider printing `unknown` as two fields:
   `unknown_null` (per draw) and `unknown_budget` (per skipped texture), or
   stop incrementing a class-style counter on the NULL path. The current
   single `unknown` number is the root of the “33,912 classifications”
   misreading.
5. **Validation targets after the change:**
   - `texalpha: ... unknown_texnull=0 ... unknown_reasons: tex=0 ...`
   - `[VehOpacity] tex='' tac=1(OPAQUE) paint=1 blendOn=0 c48w=1.00 ...`
     for a NULL paint mesh (if the sampled car has one).
   - No new holes/vanishing untextured geometry in a map/vehicle A/B.
   - If a previously fading untextured LOD becomes a pop, implement the
     dedicated `TAC_NO_TEXTURE` class described in §6 risk 1.
