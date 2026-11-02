# Ped rendering & interior lighting — external + local reference dossier

**Date:** 2026-10-25
**Mode:** READ-ONLY research (no source changes made; this file is the only output)
**Companions (existing audits, do not duplicate):**
`docs/plans/2026-10-25-ped-angle-disappearance.md`,
`docs/plans/2026-10-25-interior-angle-blackout.md`.

This document answers three questions with primary evidence:

1. How two-sided / backface-cull is handled for skinned character meshes in
   RenderWare-era GTA (and whether the SA ped skin pipe ever sets `CULLNONE`).
2. How engines compensate for negative-determinant (mirrored) transforms, whether
   GTA SA does it for peds, and what forcing `CULLBACK` actually does.
3. The standard guard that prevents a grazing-angle Fresnel/IBL term from
   darkening an interior wall.

> **Line-number caveat.** `shaders/ps/VehiclePBR_Modern.hlsl` and
> `src/core/main.cpp` were being edited while this was written. Every reference
> below was re-verified at write time and is keyed to a quoted anchor string as
> well as a line, so it survives drift. Notably
> `docs/plans/2026-10-25-interior-angle-blackout.md` cites the sky-env `lerp` as
> `:1184`; the current tree has it at `:1198`.

---

# Q1 — Two-sided / cull handling for skinned character meshes

## Answer in one paragraph

RenderWare 3.x has **no per-material and no per-geometry two-sided flag**. In GTA
San Andreas, two-sidedness is a single bit in the *item definition* (model) flags —
bit 21, `0x200000`, `DISABLE_BACKFACE_CULLING` — which is read into
`CBaseModelInfo::bIsBackfaceCulled` and applied by setting the **global**
`rwRENDERSTATECULLMODE = NONE` around that entity's draw. The vanilla ped path
never reaches it: the ped loader parses that field into `m_nPedFlags` (ped
behaviour flags) and never calls the model-info flag setter, `data/peds.ide` has no
item-definition flags column, and the model-info default is
`bIsBackfaceCulled = true`. The skin pipeline
itself never writes `CULLMODE` at all — it inherits global state. So vanilla peds
are always drawn with whatever global cull mode the caller left; a ped model that
needs two-sided faces must duplicate/flip its faces in the DFF (the community-
accepted fix).

## 1.1 RenderWare 3.x: no two-sided material, no two-sided geometry

| Claim | Evidence |
|---|---|
| `RpMaterial` has no flags field at all (texture, color, pipeline, surfaceProps, refCount only) | RW 3.7 SDK `rpworld.h:415-423`; SA-bundled 3.6 header `gta-reversed/source/game_sa/RenderWare/rw/rpworld.h:195-203` (same layout) |
| The material *stream* flags field is explicitly unused | `struct RpMaterialChunkInfo { RwInt32 flags; /* Material flags - unused currently - for future expansion */ ... }` — RW 3.7 SDK `rpworld.h:390-399`; SA 3.6 `rpworld.h:171-179` |
| There is **no `rpMATERIALFLAGS` enum** anywhere in RW 3.x. The similarly-named `RpMatFXMaterialFlags` is the *MatFX effect type* (bump/env/dual/UV), not culling | RW 3.7 SDK `rpmatfx.h:35-50` |
| `rpGEOMETRYLIGHT` is a **lighting** flag, not a cull flag | `rpGEOMETRYLIGHT = 0x00000020, /**<This geometry will be lit */` — SA 3.6 `rpworld.h:729`; RW 3.7 `rpworld.h:1300` |
| The complete geometry flag set has no two-sided bit | SA 3.6 `rpworld.h:716-750`: TRISTRIP, POSITIONS, TEXTURED, PRELIT, NORMALS, LIGHT, MODULATEMATERIALCOLOR, TEXTURED2, NATIVE, NATIVEINSTANCE |
| Culling is a single global render state, default BACK | `rwRENDERSTATECULLMODE = 20 ... Default: rwCULLMODECULLBACK` — SA 3.6 `rwplcore.h:5164`; enum `rwplcore.h:5468-5472`: NONE = both faces, BACK = only front-facing, FRONT = only back-facing |
| Culling semantics are screen-space winding, not "material side" | D3D9: `D3DCULL_CW` = "Cull back faces with clockwise vertices", `D3DCULL_CCW` = counter-clockwise ([MS docs](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcull)); [Culling State](https://learn.microsoft.com/en-us/windows/win32/direct3d9/culling-state): "you need to know the winding order of the vertices (typically counter-clockwise)" |
| Per-material double-sidedness existed in RW **2.x** (`rwDOUBLE`) and did not survive into RW 3 | [RW 2.1 `RwMaterialModes`](http://www.tnlc.com/rw/api/rwv21011.htm): "`rwDOUBLE` — any polygons using this material will be rendered double sided"; no equivalent in RW 3.7 headers |

Corroboration from the GTAMods wiki: `RpMaterial`'s stream struct is documented as
`int32 flags (unused)` ([RpMaterial](https://gtamods.com/wiki/RpMaterial)), and the
"Rendering with RenderWare" article describes only the lighting/modulate geometry
flags and material color/surface properties — no cull flag
([article](https://gtamods.com/wiki/Rendering_with_RenderWare)).

**Consequence for a DFF exporter:** a "2-sided" checkbox on a material in a
RW2-era exporter has no RW3 representation; the bit is dropped or ignored.

## 1.2 GTA SA: two-sidedness is a model flag, applied as an entity-wide global cull

Item definition bit 21 (`0x200000`) is `DISABLE_BACKFACE_CULLING`:

- GTAMods [Item Definition](https://gtamods.com/wiki/Item_Definition):
  bit 21, `0x200000`, `DISABLE_BACKFACE_CULLING` — "Disables backface culling — as
  a result the texture will be drawed on both sides of the model." Examples:
  "Roads, houses, trees, vehicle parts".
- [Template:Object-Flags](https://gtamods.com/wiki/Template:Object-Flags): flag
  `2097152` = "Disables backface culling".
- [MTA model flags](https://wiki.multitheftauto.com/wiki/Model_flags):
  `disable_backface_culling` — "Disables backface culling – as a result, the
  texture will be drawn on both sides of the model."

The engine path:

```
flags (IDE) ──► SetBaseModelInfoFlags()            gta-reversed/.../Models/BaseModelInfo.cpp:209-216
                 bIsBackfaceCulled = !bDisableBackfaceCulling
             ──► CBaseModelInfo::Init() default   BaseModelInfo.cpp:91   (bIsBackfaceCulled = true)
             ──► CEntity::Init()                  Entity.cpp:114          (SetIsBackfaceCulled(true))
             ──► CEntity::SetModelIndexNoCreate  Entity.cpp:242-254      (clears it only if the model says so)
             ──► CRenderer::RenderOneNonRoad     Renderer.cpp:108-146
                 if (!entity->GetIsBackfaceCulled())
                     RwRenderStateSet(rwRENDERSTATECULLMODE, CULLNONE);
                 ... entity->Render() ...
                 if (!entity->GetIsBackfaceCulled())
                     RwRenderStateSet(rwRENDERSTATECULLMODE, CULLBACK);   // NB: restores BACK, not the saved mode
```

Citations: `E:\dev(dave)\releases\GTASource\gta-reversed-master\source\game_sa\`
`Models\BaseModelInfo.cpp:91,209-216`,
`Entity\Entity.cpp:114,242-254`,
`Renderer.cpp:108-146`.

Note the last line: vanilla restores **BACK**, not the mode that was active before
the entity. This is exactly the class of cache/device desync the mod's
`pipeEnterCullMode`/`pipeExitCullMode` contract was written to survive
(`src/render/pipelinecommon.cpp:1182-1300`).

Bit layout for reference: `gta-reversed\...\Enums\eItemDefinitionFlags.h:35-59`
(`bDisableBackfaceCulling` is bit 21).

## 1.3 Ped models cannot be two-sided in vanilla

`CFileLoader::LoadPedObject` (`gta-reversed\...\FileLoader.cpp:1443-1508`) parses
14 fields:

```
%d model txd pedType stat animGroup carsCanDriveMask flags animFile radio1 radio2 voiceType voiceMin voiceMax
```

and stores field 8 into `mi->m_nPedFlags` (`:1496`). It **never calls**
`SetClumpModelInfoFlags`/`SetBaseModelInfoFlags`. Those are reached only from
`SetAtomicModelInfoFlags` (`AtomicModelInfo.cpp:199`) and `SetClumpModelInfoFlags`
(`ClumpModelInfo.cpp:340`, itself only called by the clump/`objs` loader at
`FileLoader.cpp:141`). Therefore every ped's `bIsBackfaceCulled` stays at
the `CBaseModelInfo::Init()` default `true`.

Data check on the shipped game (`E:\games\gtasa_skygfx_plus\data\peds.ide`):
276 ped rows, **0 occurrences of `2097152`**, and the file has no item-definition
flags column at all — the `0` / `120C` / `1003` tokens are the
`carsCanDriveMask`. So there is no data path to a two-sided ped.

Corroboration from modders ([GTAForums, 2019](https://gtaforums.com/topic/929060-looks-fine-in-blender-broken-in-game/)):

> "SA has a 2097152 IDE flag to disable backface culling and make the faces of a
> particular models 'double sided'."

and, on the older thread about two-sided faces
([GTAForums, 2005](https://gtaforums.com/topic/228414-surface-culling/)):

> "To get 2-sided faces you either set a flag for that material (like explained in
> Illy's tut) or you select the faces, copy them, and flip them around. Of course
> the latter one eats up more space and performance, but you'll need it e.g. for
> characters, since there the 2-sided flag doesn't work."

That last sentence matches the code: the *material* flag never existed in RW3, and
the *model* flag is not reachable from `peds.ide`.

## 1.4 The skin pipeline never sets `CULLNONE`

| Evidence | Location |
|---|---|
| The only render-state write in the shipped Xbox skin pipeline is `VERTEXALPHAENABLE`; no `CULLMODE` anywhere | `E:\dev(dave)\releases\GTASource\gtasa_src-main\pipes\Xbox\xboxallinone.c:372-380`; `grep -n "CULL" pipes/Xbox/xboxallinone.c` → no hits |
| The PC skin pipeline headers (`rwskin.h`, `skindefs.h`) contain no cull state | `...\pipes\PC\rwskin.h`, `...\pipes\PC\skindefs.h` |
| `grep -rn "D3DRS_CULLMODE\|rwRENDERSTATECULLMODE" pipes/` → 0 hits | entire `gtasa_src-main/pipes` tree |
| The skin pipeline's per-mesh state block is material color + vertex alpha only | `xboxallinone.c:368-380` |
| `RpSkin` API documentation mentions pipelines only, never culling | [gta-reversed RpSkin docs](https://gta-reversed.github.io/gta-reversed/group__rpskin.html) |

Vanilla `CULLNONE` writers that could be mistaken for ped handling:

| Site | Purpose |
|---|---|
| `GtaPlugins.cpp:1868-1872` | boat alpha atomics only |
| `GtaPlugins.cpp:1914-1929` | **fading** entities whose model has `!bBackfaceCulled` |
| `GtaPlugins.cpp:2084` | `RenderReallyDrawLastObjects` — sets NONE and **never restores it** (only fog is reset at `:2094`); a real leak source |
| `GtaPlugins.cpp:2025/2035` | dead code (inside a comment block) |
| `PedDebugVisualiser.cpp:18` (gta-reversed) | debug capsule/visualiser only |

Mod precedent: SkyGfx only toggles cull for *grass*; it never touches the ped path
(`aap/skygfx` `src/main.cpp:268-270`,
[GitHub](https://github.com/aap/skygfx/blob/master/src/main.cpp)).

## Q1 verdict

1. There is no `rpMATERIALFLAGS` in RW3 and no two-sided material/geometry flag.
   `rpGEOMETRYLIGHT` is lighting-only.
2. GTA SA expresses two-sidedness **only** as a whole-model IDE bit, applied by
   changing the global cull mode for one entity.
3. Peds are structurally excluded from that path (loader ignores the flags setter;
   `peds.ide` has no flags column), so all vanilla peds are single-sided.
4. The skin pipeline contributes nothing to culling: it renders meshes with the
   ambient global `CULLMODE`.
5. Therefore "make peds two-sided by forcing `CULLNONE`" is not restoring vanilla
   behaviour — it is an override. Forcing `CULLBACK` is also an override.

## Q1 recommendation for our code

| # | Change | Where |
|---|---|---|
| 1 | Keep `pedCullMode = -1` (preserve the game's sanitized raw mode). Do **not** ship `pedCullMode=2` as a "fix"; it is an A/B lever only and culls exactly the faces a winding-broken/mirrored ped shows. | `src/core/main.cpp:1744-1758` (lever); `src/core/main.cpp:6006` (INI default) |
| 2 | If a specific custom ped needs two-sided faces, do it **per model**, not per span: either (a) duplicate+flip the faces in the DFF (the only vanilla-compatible content fix), or (b) add a model-id opt-in and call `pipeForceCullMode(rwCULLMODECULLNONE)` for that atomic only, restoring the span mode after `RxPipelineExecute`. | inside `myDefaultCallback` `isPedPipe` branch, `src/core/main.cpp:1711` / draw at `:2046` (single-pass `:2054`) |
| 3 | Add a one-shot `[PedGeom]` log (atomic, det of the frame LTM, `rpGEOMETRYNATIVE`, per-model id) to settle H6 with data. The existing audit already proposes the same probe. | `src/core/main.cpp:1711` entry block |
| 4 | Do **not** widen the `pedOpaqueEnforce`/cull contract to blanket `CULLNONE`: it doubles raster cost and hides retrograde winding instead of fixing it. | `src/core/main.cpp:1077-1119` |

---

# Q2 — Negative-determinant / mirrored transform culling

## Answer in one paragraph

D3D decides facing from **screen-space winding**, so a world/view/projection
matrix with negative determinant flips which triangles count as front faces.
Engines therefore flip the cull mode (or the front-face winding) per object when
`det(world) < 0`: three.js passes `frontFaceCW = matrixWorld.determinantAffine() < 0`
into the GL state, Unreal exposes `FMeshBatch::ReverseCulling` and the engine's
own idiom is `ReverseCulling = IsLocalToWorldDeterminantNegative()`, and Unity does
it per renderer plus exposes `GL.invertCulling` for mirrored camera
passes. **GTA SA contains no determinant test anywhere in its render path.** It
avoids the problem by construction: the mirror pass rebuilds a *right-handed*
camera basis (`right = up × fwd`) and compensates the horizontal flip in the blit
UVs, and the only deliberate winding reversal in the engine — stencil shadow
volumes — flips `CULLMODE` explicitly per pass. So if a mod forces `CULLBACK`, a
mirrored or CW-wound ped is drawn inside-out and reads as "visible only at some
camera angles"; a correct closed/wound ped is unaffected, and forcing `BACK` across
a mirror/shadow span destroys those passes.

## 2.1 Why the determinant matters

- D3D9: `D3DCULL_CW` culls back faces whose vertices are clockwise in screen space;
  `D3DCULL_CCW` culls counter-clockwise ones; `D3DCULL_NONE` draws both
  ([D3DCULL enumeration](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcull),
  [Culling State](https://learn.microsoft.com/en-us/windows/win32/direct3d9/culling-state)).
- A negative determinant in the transform chain inverts the signed area of every
  projected triangle, so "front" and "back" swap. This is the standard reason
  engines need a per-object flip.

## 2.2 What production engines do

**three.js** — per object, flips the front-face winding (not the cull face):

```js
// src/renderers/WebGLRenderer.js (dev), renderBufferDirect()
const frontFaceCW = ( object.isMesh && object.matrixWorld.determinantAffine() < 0 );
state.setMaterial( material, frontFaceCW );
```

```js
// src/renderers/webgl/WebGLState.js (dev), setMaterial()
let flipSided = ( material.side === BackSide );
if ( frontFaceCW ) flipSided = ! flipSided;
setFlipSided( flipSided );
```

([WebGLRenderer.js](https://github.com/mrdoob/three.js/blob/dev/src/renderers/WebGLRenderer.js),
[WebGLState.js](https://github.com/mrdoob/three.js/blob/dev/src/renderers/webgl/WebGLState.js))

**Unreal Engine** — explicit reverse-culling flag, computed from the primitive's
local-to-world determinant:

```cpp
// e.g. Cesium for Unreal, Voxel Plugin, Houdini Engine, RealtimeMesh — canonical UE pattern
Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
```

- `FPrimitiveSceneProxy::IsLocalToWorldDeterminantNegative()`
  ([API docs](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPrimitiveSceneProxy/IsLocalToWorldDeterminantNegativ-?application_version=5.3))
- `UStaticMeshComponent::bReverseCulling` — "Controls whether the static mesh
  component's backface culling should be reversed"
  ([engine header](https://github.com/folgerwang/UnrealEngine/blob/99a530d4/Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h),
  [Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/StaticMeshComponent))
- Epic's own forum answer on negative scale: "meshes with negative scales require a
  different cull mode"
  ([forum](https://forums.unrealengine.com/t/are-there-any-issues-with-setting-a-negative-scale-to-a-mesh/397236))

**Unity** — implicit per-renderer flip (a 2018 graphics-trace experiment: 1 negative
axis → `cull front`, 2 → `cull back`:
[discussion](https://discussions.unity.com/t/cullmode-front-back-versus-negated-transform-scaling/700167/4)),
plus a global override for mirrored camera passes:

> "Select whether to invert the backface culling (true) or not (false). … Major use
> case: rendering reflections for mirrors, water etc. **Since virtual camera for
> rendering the reflection is mirrored, the culling order has to be inverted.**"
> — [Unity `GL.invertCulling`](https://docs.unity3d.com/ScriptReference/GL-invertCulling.html)

**USD / glTF converters** do the same at export time: reverse winding when
`world_mat.GetDeterminant() < 0` (e.g.
[google/usd_from_gltf `converter.cc`](https://github.com/google/usd_from_gltf/blob/master/convert/converter.cc)).

## 2.3 Does GTA SA do it for peds? — No evidence; the engine avoids the case

| Probe | Result |
|---|---|
| `grep -rn -i "determinant"` over the whole SA decompilation | Only `_rwMatrixDeterminant` (matrix math) and the intersection template. **Zero call sites in rendering.** |
| `grep -rn "CULL" source/game_sa/RenderWare/` | Only the enum/docs. The RW layer never flips cull. |
| `CRenderer::RenderEverythingBarRoads` (the main pass that contains peds) | Unconditionally sets `CULLBACK` once at entry (`Renderer.cpp:344-350`) |
| `CRenderer::RenderOneNonRoad` | Sets `NONE` only for `!bBackfaceCulled` entities, then restores `BACK` (`Renderer.cpp:119-143`) |
| `CPed::Render` | No cull handling; only `ShouldRenderPeds()` for the mirror (`Ped.cpp:3752-3807`) |
| `CStencilShadows::Render` | The notable deliberate winding reversal: `CULLFRONT` for the stencil-increment pass, `CULLBACK` for the decrement pass, `BACK` restore (`StencilShadows.cpp:156-171`) |

**Why the mirror pass does not need a flip.** SA's mirror camera is not a
reflection matrix. `CCamera::CalculateMirroredMatrix` reflects position, forward
and up, then **recomputes** the right axis as a cross product:

```cpp
// Camera.cpp:514-528
mirrorMatrix->GetForward() = fwd;                       // reflected
mirrorMatrix->GetUp()      = up;                        // reflected
mirrorMatrix->GetRight()   = { up.y*fwd.z - up.z*fwd.y,
                               up.z*fwd.x - up.x*fwd.z,
                               up.x*fwd.y - up.y*fwd.x }; // right = up × fwd
```

Recomputing `right` makes the basis right-handed again (det > 0); the resulting
horizontal mirror is compensated in the blit UVs (`Mirrors.cpp:165-170`, U runs
1→0). The mirror scene is rendered through that camera (`Mirrors.cpp:344-376`).
*This is a mathematical inference from the decompiled code, not a runtime
determinant measurement — see UNVERIFIED list.*

There is no SA code path that produces a negative-determinant ped transform, hence
no compensation is needed in vanilla. A mod that introduces one (negative-scale
frame, custom DFF exported with mirrored coordinates, custom camera) is on its own.

## 2.4 What happens if a mod forces `BACK` (or `FRONT`)?

| Situation | Forcing `CULLBACK` | Forcing `CULLFRONT` |
|---|---|---|
| Normal closed ped, CCW winding, det > 0 | no visible change | renders the **inside** of the ped (hollow/dark, normals facing away) |
| Ped with CW winding (right-handed DFF import, flipped faces) | near-side faces culled → you see the far interior wall through the hole; "**visible only at specific camera angles**" | appears roughly correct (accidentally) |
| Ped with negative-determinant frame (mirrored / `RwFrameScale(-1,…)`) | same as above — screen-space winding is flipped | appears correct (accidentally) |
| Span covers the game's mirror pass (`CULLNONE` saved) | mirror geometry becomes single-sided/inside-out | worse |
| Span covers a stencil-shadow volume pass (`CULLFRONT`) | shadow volume caps invert → shadows break | shadow pass still works, main pass broken |
| Ped rendered in SA's mirror (`CMirrors::ShouldRenderPeds()` true) | unaffected *because* SA's mirror camera has det > 0 | broken |

This matches the symptom in the report: cull is the only render state in the ped
path whose effect is intrinsically view-angle dependent.

## 2.5 Marked UNVERIFIED

- That `CalculateMirroredMatrix` yields det > 0 **at runtime** (proved from the
  algebra of the decompiled code; not measured).
- Whether any vanilla ped DFF actually contains reversed-wound triangles (would
  require inspecting DFFs).
- Whether a GPU driver ever compensates a negative determinant by itself (D3D9
  does not document such behaviour; assume it does not).

## Q2 recommendation for our code

1. **Do not compensate at span level.** `pedCullMode` writes one mode for the whole
   ped callback (`src/core/main.cpp:1752-1758`); a determinant is per atomic/frame,
   so a span-level flip cannot be correct. Remove the "force BACK" experiment from
   the shipping INI after the A/B run.
2. **Add a per-atomic det flip** inside the `isPedPipe` branch of
   `myDefaultCallback` (`src/core/main.cpp:1711`, atomic is in scope). Sketch:

```c
// inside if(isPedPipe), BEFORE pipeEnterCullMode / the draw
RwUInt32 detSavedCull = rwCULLMODECULLBACK;
int      detFlipped   = 0;
RwFrame *f = (RwFrame *)atomic->object.object.parent;   // atomic's frame
if (f) {
    const RwMatrix *m = RwFrameGetLTM(f);
    const RwV3d r = m->right, u = m->up, a = m->at;
    const float det = r.x*(u.y*a.z - u.z*a.y)
                    - r.y*(u.x*a.z - u.z*a.x)
                    + r.z*(u.x*a.y - u.y*a.x);
    if (det < 0.0f) {
        RwRenderStateGet(rwRENDERSTATECULLMODE, &detSavedCull);
        if      (detSavedCull == rwCULLMODECULLBACK) pipeForceCullMode(rwCULLMODECULLFRONT);
        else if (detSavedCull == rwCULLMODECULLFRONT) pipeForceCullMode(rwCULLMODECULLBACK);
        // NONE stays NONE
        detFlipped = 1;
    }
}
... RxPipelineExecute(pipe, atomic, 1); ...
if (detFlipped) pipeForceCullMode(detSavedCull);
```

Use the existing 3-layer helpers (`pipeForceCullMode`, `pipelinecommon.cpp:1202-1243`)
so the rw cache, the RW driver cache and the raw device stay in sync; a raw
`RwRenderStateSet` alone is the documented desync trap
(`pipelinecommon.cpp:1155-1180`).

3. If a custom mirrored **camera** pass is ever added by the mod, flip globally for
   that pass (the Unity `GL.invertCulling` pattern), not per object.
4. Instrument once per unique atomic: `[PedGeom] det=...` (also settles Q1/H6).

---

# Q3 — Interior lighting with an environment/IBL term

## Answer in one paragraph

The standard guard is not a magic constant — it is the composition rule. **Indirect
environment light is additive, never a replacement**: `L = diffuse·E(N)/π +
prefiltered(R)·EnvBRDF(F0, roughness, NdotV)`. A black probe adds zero and cannot
subtract the lit body. The Fresnel/diffuse energy split (`kD`) must be evaluated
**per light with `L·H`**, never with `N·V`, and must never be applied to IBL as
`1 - F(NdotV)` — Karis explicitly warns that a grazing-response adjustment tuned
for analytic lights, reused for IBL, makes glancing angles "much too dark".
Interiors should switch the
**IBL source/weight** (indoor probe, sky-light lower-hemisphere off, or `currArea`
gate), not merely tint it. If code must still blend toward an env sample, weight
that blend by the sample's own luminance relative to the body and cap the weight —
CryEngine's `GetEnvmapFresnel` (grazing gain divided by `40 - 39·gloss`) and its
`ApplyGI` luminance ratio are the canonical instances.

## 3.1 The additive IBL composition (reference implementation)

Filament, "Physically Based Rendering in Filament" §5.3, Listing 28
([docs](https://google.github.io/filament/Filament.html#lighting/imagebasedlights)):

```glsl
vec3 evaluateIBL(vec3 n, vec3 v, vec3 diffuseColor, vec3 f0, vec3 f90, float perceptualRoughness) {
    float NoV = max(dot(n, v), 0.0);
    vec3 r = reflect(-v, n);

    // Specular indirect
    vec3 indirectSpecular = evaluateSpecularIBL(r, perceptualRoughness);
    vec2 env = prefilteredDFG_LUT(perceptualRoughness, NoV);
    vec3 specularColor = f0 * env.x + f90 * env.y;

    // Diffuse indirect
    vec3 indirectDiffuse = max(irradianceSH(n), 0.0) * Fd_Lambert();

    // Indirect contribution  ← ADD, never lerp
    return diffuseColor * indirectDiffuse + indirectSpecular * specularColor;
}
```

and the energy split is applied to the *material*, not to `N·V`:
`vec3 diffuseColor = (1.0 - metallic) * baseColor.rgb;` (Listing 11).

## 3.2 The grazing-angle guards engines actually use

**(a) Roughness-dependent env Fresnel damping — CryEngine.** Local source:

```hlsl
// E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\Shaders\HWScripts\CryFX\shadeLib.cfi:223-229
half3 GetEnvmapFresnel(half3 specCol0, half gloss, half fNdotE)
{
    const half3 specCol90 = half3(1,1,1);
    // Empirical approximation to the reduced gain at grazing angles for rough materials
    return lerp(specCol0, specCol90, pow(1 - saturate(fNdotE), 5) / (40 - 39 * gloss));
}
```

At `gloss = 0` the grazing gain is divided by 40; only mirror-smooth surfaces get
the full Schlick curve. This is the guard against "F → 1 at grazing on a rough
wall". Our repo already ports it, but does not use it:
`shaders/include/PBR_Common.hlsl:107-113` (definition, 0 call sites).

**(b) Luminance-proportional env weight — CryEngine GI.**

```hlsl
// ...\CryFX\TiledShading.cfi:108-127
if (nIntegrMode == 0) { // AO only
    float4 vSkyLightIrradiance = TexGiDiffuse.SampleLevel(...);
    specularAcc *= lerp(vSkyLightIrradiance.xyz, 1.f, surfGloss * GetLuminance(surfSpecCol));
    diffuseAcc  *= vSkyLightIrradiance.xyz;                  // AO + sun bounces
} else if (nIntegrMode == 1) {
    float3 diffuseIrradiance = TexGiDiffuse.SampleLevel(...);
    specularAcc *= lerp(min(2.f, GetLuminance(diffuseIrradiance) /
                                  (GetLuminance(diffuseAcc + diffuseForwAcc) + 0.001)),
                        1.f, saturate(surfGloss * GetLuminance(surfSpecCol) * ...));
    diffuseAcc = diffuseIrradiance;
}
```

Two ideas worth copying: (i) the env/GI term is multiplied in, and when it is dark
it only *attenuates* the ambient (which is physically what occlusion means), and
(ii) the specular blend is driven by a **ratio of luminances**, clamped, with a
hard `lerp(..., 1, ...)` upper bound so it can never go negative.

**(c) Specular occlusion instead of "darken everything".** Filament §5.6.2
([docs](https://google.github.io/filament/Filament.html#lighting/occlusion/specularocclusion)):

```glsl
float computeSpecularAO(float NoV, float ao, float roughness) {
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}
// specular indirect ONLY
vec3 indirectSpecular = evaluateSpecularIBL(r, perceptualRoughness);
indirectSpecular *= computeSpecularAO(NoV, ao, roughness);
```

The source comment is explicit about the failure mode we are chasing: *"Smoothly
kill specular AO when entering the perceptual roughness range [0.1..0.3]. Without
this, specular AO can remove all reflections, which looks bad on metals"*
([surface_ambient_occlusion.fs](https://github.com/google/filament/blob/main/shaders/src/surface_ambient_occlusion.fs)).
Note it is applied **only to indirect specular** — not to the whole body.

**(d) Do not reuse analytic-light grazing tweaks for IBL — UE4/Karis.** In "Real
Shading in Unreal Engine 4" the Disney `k = (Roughness+1)²/8` adjustment is
followed by:

> "It's important to note that this adjustment is only used for analytic light
> sources; **if applied to image-based lighting, the results at glancing angles
> will be much too dark.**"
> ([course notes PDF](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf);
> text also mirrored at [DocsLib](https://docslib.org/doc/10446891/real-shading-in-unreal-engine-4-by-brian-karis-epic-games))

The IBL path uses the split-sum DFG term instead — `EnvBRDFApprox(F0, roughness,
NoV) = F0 * AB.x + AB.y`, where the constant `AB.y` bias means grazing reflectance
never collapses to zero
([UE blog](https://www.unrealengine.com/blog/physically-based-shading-on-mobile);
code mirrored at
[lygia envBRDFApprox.hlsl](https://github.com/patriciogonzalezvivo/lygia/blob/main/lighting/common/envBRDFApprox.hlsl)):

```hlsl
float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
float4 c1 = float4( 1.0,  0.0425,  1.04, -0.04);
float4 r = roughness * c0 + c1;
float  a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
return specularColor * AB.x + AB.y;      // additive bias, never zero
```

**(e) Indoor mode, not a tint.**

- Unity: objects that are indoors / in caves must sample a **local reflection
  probe**, not the skybox: *"You should use this for objects that are 'indoors' or
  in covered parts of the scene (eg, caves and tunnels) since the sky is not visible
  from these place and so should never appear in the reflections."*
  ([Using Reflection Probes](https://docs.unity3d.com/cn/2018.3/Manual/UsingReflectionProbes.html));
  box projection is sized to the room
  ([Advanced reflection probe](https://docs.unity.com/en-us/engine/6000.3/manual/lighting-overview/reflections/advanced-ref-probe)).
- Unreal: SkyLight "Lower Hemisphere is Solid Color" — *"Whether all lighting from
  the lower hemisphere should be set to zero. This is useful to prevent leaking
  from the lower hemisphere."*
  ([Sky Lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine)),
  plus reflection captures placed inside the room.
- Frostbite: the canonical PBR reference still composes ambient from **local**
  probes / SH and applies occlusion to indirect only
  ([course notes](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v32.pdf),
  §4 "Lighting" and §5 "Specular occlusion").

## 3.3 The formula to implement

Two acceptable shapes; both are non-destructive.

**A. Additive (preferred, matches Filament/UE):**

```hlsl
float3 body = baseColor * ambientTint + directSpec + clusterDiffuse;   // already lit
float3 env  = prefilteredEnv(R, roughness);
float3 Fenv = EnvBRDFApprox(F0, roughness, NdotV);                     // split-sum, has bias

// optional luminance weight: a dim/dead probe contributes proportionally less
float  envLum = dot(env, LUMA);
float  w      = saturate(envLum * kGain);       // 0 for a black probe
color = body + env * Fenv * w * strength;       // ADD — cannot darken
```

**B. Blend with a hard non-darkening clamp (if the artist-authored look needs a
replacement):**

```hlsl
float3 before = color;
float  bodyLum = max(dot(before, LUMA), 1e-4);
float  envW = F * saturate(envLum / bodyLum) * strength;   // luminance-proportional
envW = min(envW, 0.85);                                     // grazing cap
color = lerp(before, env, envW);
color = max(color, before * 0.75);                          // darkening budget ≤ 25%
```

**C. Indoor branch (required either way):**

```hlsl
if (indoors > 0.5) {
    color = body;                    // ambient + local/point lights only
} else {
    color = body + env * Fenv * w * strength;
}
```

## 3.4 A second angular term our audits do not list

The env `lerp` is the documented suspect
(`shaders/ps/VehiclePBR_Modern.hlsl:1198`, `color = lerp(color, skyEnv, envW);`)
and the interior audit ranks it first. There is a second, purely angular term that
the audit does **not** list, and it is a textbook violation of §3.2(d):

```hlsl
// main_building(), current tree
float3 F_atNdotV = F_Schlick(NdotV, F0);   // :1042
float3 kS = F_atNdotV;                     // :1043
float3 kD = 1.0 - kS;                      // :1044  ← view dependent!
...
ClusterLighting(vpos.xy, IN.WorldPos, N, V, F0, roughness,
                baseColor, kD, 0.0, 1.0, 0.0, specTotal, clusterDiffuse);  // :1088-1090
```

```hlsl
// ClusterShadeOne()
diffAccum += baseColor * diffc * lcol * atten * kD * (1.0 / 3.14159);   // :269
```

`kD` is a per-pixel constant used for **every** Forward+ point light. At grazing
view (`NdotV → 0`, easily reached by a normal map tilting `N` away from `V`) the
Schlick term goes to 1, so `kD → 0` and all point-light diffuse on the wall
vanishes. This is a strong candidate **whenever the interior is lit by Forward+
lights** (candles/lamps/interior omnis); it cannot explain a blackout in an
interior that has no cluster lights and relies on baked ambient alone (that is the
audit's H2). It is fully view-dependent, which matches "black only at certain
angles". The same pattern exists in the vehicle entry:

```hlsl
float3 F_atNdotV = F_Schlick(NdotV, F0);   // :539
float3 kDv = saturate(1.0 - F_atNdotV);    // :540  → ClusterLighting at :755
```

Note `main_building`'s own `kD` comment says "dielectric: all non-reflected energy
is diffuse" — true per *light*, where the Fresnel argument is `L·H`, not `N·V`.

## Q3 recommendation for our code

| # | Change | Where |
|---|---|---|
| 1 | Compute the diffuse energy split per light, not from `NdotV`. Cheapest correct fix: `float3 kD = EnergyConservation(F0);` (view-independent, already in `PBR_Common.hlsl:124-127`, CryEngine's `saturate(1 - luminance(F0))`). Better: move it into `ClusterShadeOne` and use `1 - F_Schlick(LdotHc, F0)` (it already computes `LdotHc`/`Fc` for specular at `:252-256`). | `shaders/ps/VehiclePBR_Modern.hlsl:1042-1044`; mirror in `main()` `:539-540`; consumption `:269` |
| 2 | Make the sky-env path non-destructive: either `color += skyEnv * envW * k;` instead of `lerp`, or keep the lerp and clamp `color = max(color, before * 0.75)`. Add the luminance-proportional factor `saturate(envLum / bodyLum)`. | `shaders/ps/VehiclePBR_Modern.hlsl:1190-1198` |
| 3 | Use the ported-but-unused `GetEnvmapFresnel(F0, gloss, NdotV)` for the env lobe instead of raw `F_Schlick`, so rough walls do not get a full grazing mirror weight. | `shaders/include/PBR_Common.hlsl:110`; env weight at `shaders/ps/VehiclePBR_Modern.hlsl:1190` (uses `F_atNdotV` from `:1042`) |
| 4 | Add an explicit indoor flag. `c47 = bldEnv` has `.w` free (uploaded per mesh as `0.0f`). Upload `(*CGame__currArea != 0)` and branch in the PS; then the shader no longer depends on `bldEnv.z` being correctly zeroed by the CPU. | upload `src/render/buildingPipe.cpp:2908`; branch `shaders/ps/VehiclePBR_Modern.hlsl:1134`/`:1198` |
| 5 | Keep the existing gates (they are correct and match §3.2(e)): `c20.x = 0` indoors (`buildingPipe.cpp:2445`), `c47.z = 0` indoors (`:2905-2907`), neutral 1×1 s3 (`:2461-2472`), `ClampAmbientToSky` early-return (`pipelinecommon.cpp:92-93`), absolute ambient floor (`:158-168`). | as listed |
| 6 | Do **not** add a flat "env cannot be < 1.0" hack: it would lift true blacks. The luminance-ratio + clamp forms above preserve black. | — |

## 3.5 Marked UNVERIFIED

- Frostbite's exact runtime indoor/probe-selection mechanism (the course notes were
  cited, not the runtime code — it is not public). The Unity/UE statements above
  carry the "indoor mode" claim.
- Whether `EnvBRDFApprox`'s `AB.y` bias alone is sufficient for our SM3 shader; it
  is a mobile approximation of the split-sum DFG LUT. For our IBL (a 2D
  sky-gradient sampled by elevation, not a prefiltered cubemap) the physically
  cleaner fix is the additive composition + indoor branch.
- Whether the reported interior blackout is dominated by the `kD` term or by the
  `lerp`; that needs the `[BldEnv]`/GPU-luminance probes already listed in
  `docs/plans/2026-10-25-interior-angle-blackout.md` §9. This dossier only supplies
  the guard and the reference formula.

---

# Sources consulted

## Local (read-only)

**GTA SA decompilation — `E:\dev(dave)\releases\GTASource\gta-reversed-master`**
- `source\game_sa\RenderWare\rw\rpworld.h:167-180` (`RpMaterialChunkInfo.flags` unused), `:716-750` (`RpGeometryFlag`, `rpGEOMETRYLIGHT`)
- `source\game_sa\RenderWare\rw\rwplcore.h:5158-5162`, `:5468-5472` (cull state + enum)
- `source\game_sa\RenderWare\rw\rtintsec.h:47-51`, `rwplcore.cpp:217` (determinant only in math/intersection)
- `source\game_sa\Models\BaseModelInfo.cpp:91,209-216`; `Models\BaseModelInfo.h:105,189`; `Models\ClumpModelInfo.cpp:338-345`
- `source\game_sa\Enums\eItemDefinitionFlags.h:35-59` (bit 21)
- `source\game_sa\Entity\Entity.cpp:114,242-254`; `Entity\Entity.h:88,179-180`
- `source\game_sa\FileLoader.cpp:141,1443-1508` (`LoadPedObject` — no flags setter)
- `source\game_sa\Renderer.cpp:80-146,319-350` (per-entity cull NONE/BACK, main-pass BACK)
- `source\game_sa\VisibilityPlugins.cpp:533-596`; `GtaPlugins.cpp` equivalents
- `source\game_sa\StencilShadows.cpp:135-200` (explicit FRONT/BACK flips)
- `source\game_sa\Mirrors.cpp:100-226,284-285,344-376`; `Camera.cpp:514-543,1625-1640`
- `source\game_sa\Entity\Ped\Ped.cpp:3752-3807` (`CPed::Render`, `ShouldRenderPeds`)

**GTASource partial dump — `E:\dev(dave)\releases\GTASource\gtasa_src-main`**
- `pipes\Xbox\xboxallinone.c:318-387` (skin pipe state block: no CULLMODE)
- `pipes\Xbox\skinxbox.h`, `pipes\PC\rwskin.h`, `pipes\PC\skindefs.h`
- `GtaPlugins.cpp:1857-1872,1877-1960,2075-2095` (NONE writers / really-draw-last leak)
- `ps2all.cpp:151,214` (`rpGEOMETRYLIGHT` usage — lighting, not culling)

**RenderWare 3.7 SDK — `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37`**
- `Graphics\rwsdk\include\d3d9\rpworld.h:390-399` (material flags unused), `:415-423` (`RpMaterial`), `:1287-1321` (`RpGeometryFlag`)
- `Graphics\rwsdk\include\d3d9\rwplcore.h:5158-5162,5454-5474` (cull state/enum)
- `Graphics\rwsdk\include\d3d9\rpmatfx.h:35-50` (`RpMatFXMaterialFlags` is effects, not cull)
- `Graphics\examples\pshader\src\pshaderD3D9.c:600-631` (save/restore `D3DRS_CULLMODE` around a custom pass — the pattern to copy)

**CryEngine — `E:\dev(dave)\releases\GTASource\CRYENGINE-release`**
- `Engine\Shaders\HWScripts\CryFX\shadeLib.cfi:217-238` (`GetFresnel`, `GetEnvmapFresnel`, `GetEnvBRDFFresnel`)
- `Engine\Shaders\HWScripts\CryFX\TiledShading.cfi:108-127` (`ApplyGI`, luminance-ratio weights)

**GTA SA game data**
- `E:\games\gtasa_skygfx_plus\data\peds.ide` — 276 ped rows, 0 × `2097152`, no item-definition flags column

**Project source (our repo)**
- `src/core/main.cpp:1585-1603` (`myDefaultCallback`, `isPedPipe`), `:1711` ped entry, `:1736-1758` cull lever, `:2046`/`:2054` draw, `:2088` exit restore, `:6006-6008` INI default
- `src/render/pipelinecommon.cpp:78-169` (`ClampAmbientToSky`, interior early-return, ambient floor), `:1155-1300` (3-layer cull contract)
- `src/render/buildingPipe.cpp:2439-2479` (c24/c20/s3 indoor gates), `:2900-2909` (`bldEnv` c47 upload), `:2844-2891` (env-map weight)
- `shaders/include/PBR_Common.hlsl:14-19,101-127` (`F_Schlick`, `GetEnvmapFresnel`, `EnergyConservation`)
- `shaders/ps/VehiclePBR_Modern.hlsl:92-108` (`SkyHemisphereTint`), `:237-270` (`ClusterShadeOne`, `kD` at `:269`), `:536-540` (`kDv`), `:1042-1044` (`kD`), `:1088-1090`, `:1134-1200` (env branch, guards, `lerp` at `:1198`)
- `docs/plans/2026-10-25-ped-angle-disappearance.md`, `docs/plans/2026-10-25-interior-angle-blackout.md` (prior audits)

## Web

| # | URL | Used for |
|---|---|---|
| 1 | https://gtamods.com/wiki/Item_Definition | bit 21 `0x200000` = DISABLE_BACKFACE_CULLING |
| 2 | https://gtamods.com/wiki/Template:Object-Flags | flag 2097152 semantics |
| 3 | https://gtamods.com/wiki/RpMaterial | material stream `flags (unused)` |
| 4 | https://gtamods.com/wiki/Rendering_with_RenderWare | geometry/material flags are lighting/modulate only |
| 5 | https://gtaforums.com/topic/929060-looks-fine-in-blender-broken-in-game/ | 0x200000 makes faces "double sided" |
| 6 | https://gtaforums.com/topic/228414-surface-culling/ | "for characters the 2-sided flag doesn't work" |
| 7 | http://www.tnlc.com/rw/api/rwv21011.htm | RW **2.x** `rwDOUBLE` material mode (removed in RW3) |
| 8 | https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcull | D3DCULL_NONE/CW/CCW definitions |
| 9 | https://learn.microsoft.com/en-us/windows/win32/direct3d9/culling-state | winding order determines culling |
| 10 | https://docs.unity3d.com/ScriptReference/GL-invertCulling.html | mirrored camera ⇒ invert cull order |
| 11 | https://discussions.unity.com/t/cullmode-front-back-versus-negated-transform-scaling/700167/4 | per-renderer implicit cull flip on negative scale |
| 12 | https://github.com/mrdoob/three.js/blob/dev/src/renderers/WebGLRenderer.js | `determinantAffine() < 0` ⇒ `frontFaceCW` |
| 13 | https://github.com/mrdoob/three.js/blob/dev/src/renderers/webgl/WebGLState.js | `flipSided` from `frontFaceCW` |
| 14 | https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPrimitiveSceneProxy/IsLocalToWorldDeterminantNegativ-?application_version=5.3 | UE determinant helper |
| 15 | https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/StaticMeshComponent | `reverse_culling` / `set_reverse_culling` |
| 16 | https://github.com/folgerwang/UnrealEngine/blob/99a530d4/Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h | `bReverseCulling` declaration |
| 17 | https://forums.unrealengine.com/t/are-there-any-issues-with-setting-a-negative-scale-to-a-mesh/397236 | "negative scales require a different cull mode" |
| 18 | https://github.com/google/usd_from_gltf/blob/master/convert/converter.cc | reverse winding when `GetDeterminant() < 0` |
| 19 | https://google.github.io/filament/Filament.html#lighting/imagebasedlights | additive IBL, Listing 28 |
| 20 | https://google.github.io/filament/Filament.html#lighting/occlusion/specularocclusion | Lagarde specular occlusion formula |
| 21 | https://github.com/google/filament/blob/main/shaders/src/surface_ambient_occlusion.fs | "specular AO can remove all reflections" comment |
| 22 | https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v32.pdf | Frostbite standard model, specular occlusion |
| 23 | https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf | "if applied to image-based lighting … glancing angles much too dark"; split-sum |
| 24 | https://docslib.org/doc/10446891/real-shading-in-unreal-engine-4-by-brian-karis-epic-games | verbatim text of the quote (PDF fetch exceeded limit) |
| 25 | https://www.unrealengine.com/blog/physically-based-shading-on-mobile | `EnvBRDFApprox` source |
| 26 | https://github.com/patriciogonzalezvivo/lygia/blob/main/lighting/common/envBRDFApprox.hlsl | verified `EnvBRDFApprox` code |
| 27 | https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine | SkyLight lower-hemisphere solid color |
| 28 | https://docs.unity3d.com/cn/2018.3/Manual/UsingReflectionProbes.html | indoor objects must not see the skybox probe |
| 29 | https://docs.unity.com/en-us/engine/6000.3/manual/lighting-overview/reflections/advanced-ref-probe | box projection sized to the room |
| 30 | https://github.com/aap/skygfx/blob/master/src/main.cpp | SkyGfx `backfaceCull` is grass-only — no ped cull precedent |
| 31 | https://gta-reversed.github.io/gta-reversed/group__rpskin.html | RpSkin pipeline docs (no cull API) |
| 32 | https://wiki.multitheftauto.com/wiki/Model_flags | `disable_backface_culling` model flag |
| 33 | https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3drenderstatetype | two-sided stencil requires `D3DCULL_NONE` (context for shadow volumes) |
