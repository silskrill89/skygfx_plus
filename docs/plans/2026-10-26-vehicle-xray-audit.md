# Xbox Vehicle Render Path — "3/4 X-Ray" Audit (Lane F)

Date: 2026-10-26
Mode: `[Modes] active = Xbox` → `pipeline=2`, `buildingPipe=1`, `vehiclePipe=2` (`CAR_XBOX`)
Callback: `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox` — `src/render/vehiclePipe.cpp:1441-1713`
Build/log audited: `skygfx.asi` 2,395,136 bytes, 2026-10-26 01:11; log session `01:12:26 → 01:25:52`
Scope: **read-only** source audit. No source file was modified. This document is the only write.

---

## TL;DR

The x-ray is a **two-part defect chain**, not a single bad state:

1. **The SA paint-marker alpha (`material->color.alpha == 16`) is uploaded to the Xbox VS as `16/255 = 0.0627`.** The Xbox callback's `SetupMaterial_Xbox` does **not** apply the paint-marker clamp that the shared `pipeUploadMatCol()` helper applies for every other vehicle pipe (`pipelinecommon.cpp:458-477`). `main_xboxCar` writes that value straight into `COLOR0.a` (`shaders/vehiclePipeVS.hlsl:281`). The recent per-mesh L2 mirror (`vehiclePipe.cpp:1553`, added by commit `b7396c4`) then force-enables `D3DRS_ALPHABLENDENABLE` for that mesh. Result: a painted panel is composited at ~6.3 % opacity (`SRCBLEND=SRCALPHA`, `DESTBLEND=INVSRCALPHA`).
2. **The Xbox mode profile forces `dualPassVehicle=1`** (`modeManager.cpp:218`) even though the INI says `dualPassVehicle = 0` (`skygfx.ini:53`). `D3D9RenderDual` leg 1 is `GREATEREQUAL @ zwriteThreshold(128)` with `ZWRITE=TRUE`; leg 2 is `LESS @ 128` with `ZWRITE=FALSE` (`main.cpp:277-294`). A paint texel at alpha 16 (or 0 from an unused texture alpha) is **rejected by leg 1**, so the body writes **no depth at all**; leg 2 then draws it at ~6 % with depth writes off. Opaque trim/chrome/glass (alpha 255) draw in leg 1 (or in the `else` single-draw path when `hasAlpha` is false), keep `ZWRITE=TRUE`, and look correct → **"edges correct, interior see-through."**

The "sphere" is a *passenger*, not the cause: with no body depth and ~6 % body alpha you see the sky dome / far side / interior through the curved shell. The static SA env sphere map on s1 (`envData->texture`, `vehiclePipe.cpp:1593/1627`) can add a spherical highlight, but the env stage cannot remove body depth or make an opaque body transparent. `CAR_XBOX` never enters the sphere-map render pass (`envmap.cpp:1126` excludes it), so `gRenderingSpheremap` feedback is **not** reachable in this mode.

---

## 0. Symptom & Scope

User wording: *"vehicles are completely messed up … 3/4 xray … edges are correct … a sea through sphere being cast on vehicles and partial elements only rendered."*

Translated to D3D terms:

| Observation | D3D mechanism |
|---|---|
| Silhouette/edges correct | Opaque materials (trim, chrome, lights, glass with real alpha) draw in dual-pass leg 1, write colour + depth |
| Body panels partially missing | Paint/body meshes fail the leg-1 alpha test (`alpha < 128`) → no colour, **no depth** |
| Body appears see-through / x-ray | Same paint meshes then draw in leg 2 with `ZWRITE=FALSE` and `SRCALPHA/INVSRCALPHA`; ~6 % contribution |
| "Sphere" visible through the car | No body depth → sky dome / far side / interior visible; the curved body shell frames it |
| Only some elements rendered | The unaffected (alpha 255) meshes are exactly the ones that write depth and occlude |

The 3/4 angle is the worst case: the near body panel writes no depth and the far panel/interior is in the same view. Head-on, the dashboard/engine largely occludes the interior, so the defect reads as "only some panels missing" rather than a full x-ray.

Evidence sources used below:

- Live log `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (session 01:12–01:25).
- `skygfx.ini` in the game dir (`[Modes] active = Xbox` at line 5, `dualPassVehicle = 0` at line 53).
- Prior live evidence of `matA=16` at draw time: `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md:9-11`.
- aap reference implementation: `memory-bank/.cache/external-repos/aap-skygfx/src/vehiclePipe.cpp` and `shaders/vs/xboxCarVS.hlsl`.

Runtime confirmations:

```
01:12:26.136  [Mode] apply Xbox pipes=2/1/2 cf=2 iv=0
01:25:48.552  [Pipeline] pipeEnterCullMode: saved=0x2 -> forcing saved mode pipes(bld=1 veh=2)
01:25:20.272  [PipeAlpha] entry resync: dev src=5 dst=6 vtx=1 op=1 test=1 rw ref=2 fn=5 sepA=0 cwr=0xF
01:13:41.657  [VEHICLE] ZWRITE off/desynced on entry (rw=1 cache=1 dev=0) — forcing TRUE for cb
01:13:41.016  [VehTex] tex='vehiclegeneric256' null=0 bound=173B5414 env=000 spec=255 matA=389764116
```

The last line is the **malformed** vehicle diagnostic: `matA` prints the `bound` pointer, and `spec` actually prints the material alpha (see §3.4). The 24 samples show alphas 255 (body/tyres) and 96 (grunge), but **no sampled mesh has alpha 16**, so they neither confirm nor refute alpha 16 for this particular car. The Env-path log in `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md` does contain `matA=16` at draw time (`vehiclegeneric256`, `vehiclegrunge256`, `vehiclelights128`), which is the same game material data the Xbox callback reads.

---

## 1. Dual pass — `D3D9RenderDual(config->dualPassVehicle, ...)`

### 1.1 Exact behaviour of the second leg

`D3D9RenderDual` — `src/core/main.cpp:252-301`.

| Element | Leg 1 | Leg 2 |
|---|---|---|
| Gate | `dual && hasAlpha && zwrite` (`main.cpp:277`) | same gate |
| Alpha test | `setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zwriteThreshold)` (`:287`) | `setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zwriteThreshold)` (`:290`) |
| ZWRITE | `setZWriteTwoLayer(TRUE)` (`:288`) | `setZWriteTwoLayer(FALSE)` (`:291`) |
| Blend | whatever `D3DRS_ALPHABLENDENABLE` says; factors are the Switch's canonical `SRCALPHA/INVSRCALPHA`, `ADD` | same |
| Draw | `D3D9Render(...)` (`:289`) | `D3D9Render(...)` (`:292`) |
| Restore | `setZWriteTwoLayer(zwrite)` + `setAlphaTestTwoLayer(alphafunc, alpharef)` (`:293-294`) | — |

Key facts:

- `hasAlpha` in the gate is **not** the callback's local `hasAlpha`; it is `RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE)` — the **L2 driver cache** (`main.cpp:259`).
- `zwrite` is `RwRenderStateGet(rwRENDERSTATEZWRITEENABLE)` (`main.cpp:261`). The vehicle Switch forces it `TRUE` on all three layers for the whole callback (`vehiclePipe.cpp:3623-3625`), and the Xbox callback saves/restores its own `xboxZwrite` but never changes it per mesh (`1471-1472`, `1692`). So `zwrite == TRUE` for every Xbox vehicle mesh.
- `zwriteThreshold` comes from `config->zwriteThreshold` (default 128; Xbox profile pins 128 at `modeManager.cpp:279`). It is clamped to ≤254 (`main.cpp:274-276`). The Xbox callback passes **no `TexInfo`** (`vehiclePipe.cpp:1679`), so the per-texture override is never used.
- For a texel with alpha 16 (paint marker):
  - Leg 1 `GREATEREQUAL @128` → **rejected**. No colour, **no depth write**.
  - Leg 2 `LESS @128` → accepted, blended at `16/255`, **`ZWRITE=FALSE`**.
  - Net: the body contributes ~6 % colour and **never writes depth**. Chrome/trim/glass at alpha 255 either pass leg 1 or take the `else` single-draw path (`main.cpp:299-300`); both keep `ZWRITE=TRUE` and occlude correctly.

This is the exact "edges correct, interior see-through" signature. It does **not** require a broken cull, a reversed ZFUNC, a missing texture, or stencil.

### 1.2 Why the forced `dualPassVehicle=1` matters

The Xbox mode profile sets it unconditionally:

```
src/core/modeManager.cpp:200   static const ModeValue kValuesXBOX[] = {
src/core/modeManager.cpp:212       MV_I(dualPassBuilding, 1),
src/core/modeManager.cpp:218       MV_I(dualPassVehicle, 1),
src/core/modeManager.cpp:279       MV_I(zwriteThreshold, 128),
```

`ApplyMode()` layers profile fields over the live Config after `readIniFile()`; the INI's `dualPassVehicle = 0` (`skygfx.ini:53`) is therefore **not** what the runtime uses. The log confirms the Xbox mode was applied (`[Mode] apply Xbox` 01:12:26.136).

The gate is also *only enabled* because of the recent per-mesh mirror:

```
src/render/pipelinecommon.cpp:1628  RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);   // pipeEnterAlphaMode baseline
src/render/buildingPipe.cpp:596     RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, vtx ? TRUE : FALSE); // pipeForceAlphaBlock, vtx==FALSE
src/render/vehiclePipe.cpp:1553     RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE); // per-mesh mirror
src/render/vehiclePipe.cpp:1679     D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);
```

Without line 1553, the L2 value would stay `FALSE` and `D3D9RenderDual` would never take the dual branch in this build. Commit `b7396c4` added that mirror (the aap reference callback has no such line). So the profile supplies the switch and the mirror supplies the gate — but only the alpha-16 material makes the gate *harmful*.

### 1.3 Verdict

**Can reproduce the signature: YES, but only in combination with alpha < 128.** Dual pass alone on a genuinely opaque material is a no-op (leg 1 draws everything, leg 2 rejects everything). Dual pass on `matA=16` paint is the mechanism that removes body depth. This is the amplifier, not the root cause.

### 1.4 A/B lever

`dualPassVehicle=0` **in the INI will be overwritten** by the Xbox profile. Use one of:

- Debug menu: `SkyGFX|Advanced → Dual-pass Vehicles` (`src/core/debugmenu_ui.cpp:313`) — writes the live Config, survives until the next mode apply / INI reload.
- Or `[Modes] active = None` in `skygfx.ini` (keeps `vehiclePipe = Xbox` from line 229) and then `dualPassVehicle = 0`.

Expected result if the pair diagnosis is right: interior/far-side visibility disappears (x-ray gone) but the body may still look ~6 % ghosted, because the blend mirror remains on. See §11.

---

## 2. Env-map / sphere-map on s1

### 2.1 How the Xbox callback selects and binds env

```
src/render/vehiclePipe.cpp:1539   materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
src/render/vehiclePipe.cpp:1541-1543
    hasEnv1 = (materialFlags & 1) && !noFx;
    hasEnv2 = (materialFlags & 2) && !noFx && (flags & rpGEOMETRYTEXTURED2);
    hasSpec = (materialFlags & 4) && !noFx && !renderingWheel;
```

- env1 path (`:1577-1605`): `Env1Xform_PC` (`:1589`), `LOC_Texture` matrix (c16), `RwD3D9SetTexture(envData->texture, 1)` (`:1593`), `D3DRS_TEXTUREFACTOR = XRGB(tfactor)`, stage1 `MULTIPLYADD(CURRENT, TEXTURE, TFACTOR)`, `ALPHAOP=SELECTARG2` (current alpha).
- env2 path (`:1606-1639`): `Env2Xform_PC`, `RwD3D9SetTexture(envData->texture, 1)` (`:1627`), `D3DRS_TEXTUREFACTOR = ARGB(tfactor,255,255,255)`, stage1 `BLENDFACTORALPHA(TEXTURE, CURRENT)`, `TEXCOORDINDEX=1` (`:1637`).
- The shader writes `Texcoord1` from the eye-space normal for env1 (`shaders/vehiclePipeVS.hlsl:277-278`) and from UV1 for env2 (`:279`).
- Stage1 is explicitly disabled for non-env meshes (`:1557-1560`) and at exit (`:1683-1685`).
- There is no `gRenderingSpheremap` check anywhere in the Xbox callback (1441-1713). Guards exist in the Mobile callback (`:2085-2088`) and the Env callback (`:2466-2472`, `:2954-2957`, `:3326-3329`).

### 2.2 Can the sphere-map RT be sampled while bound?

For `CAR_XBOX`: **no.** `RenderSphereReflections()` only runs for `CAR_MOBILE`, `CAR_ENV`, `CAR_MODERN`:

```
src/render/envmap.cpp:1126   if(iCanHasbuildingPipe && (config->vehiclePipe == CAR_MOBILE ||
                                                     config->vehiclePipe == CAR_ENV ||
                                                     config->vehiclePipe == CAR_MODERN)){
```

`CAR_XBOX` is not in that set, so `gRenderingSpheremap` stays false and `reflectionTex` (the sphere RT, `envmap.cpp:24`, `:112-113`) is not being rendered during Xbox vehicle draws. The `envData->texture` bound at s1 is the game's static vehicle env texture, not `reflectionTex`. A feedback loop is therefore **not reachable in this mode**. If the profile is ever changed to `CAR_ENV`/`CAR_MODERN`, the Env callback's guards apply; the Xbox callback's raw s1 bind would then need the same guard.

### 2.3 Residual risks that are real but not the x-ray

1. **Raw s1 bind, no NULL resolution.** Both env branches call `RwD3D9SetTexture(envData->texture, 1)` instead of `pipeSetTexture`/`vehEnvBindTexture`. If `envData->texture` is NULL, the stage keeps the previous raster (or D3D's white default) while stage1 is enabled. With `MULTIPLYADD`, this can add a bright constant/blob. It cannot remove body depth or make the car see-through.
2. **env1 never sets `D3DTSS_TEXCOORDINDEX=1`.** Only env2 does (`:1637`). On the first env1 mesh after a state reset the stage can sample UV0 instead of the spherical UV1, which smears the env sphere map over body UVs. The callback exit sets it back to 1 (`:1685`), so in steady state it is correct. This is a one-line hygiene bug, not the persistent symptom.
3. **`envData` is dereferenced without a NULL check** when a specular/env bit is set (`:1585`, `:1589`, `:1621`). That would crash, not x-ray.

### 2.4 Verdict

**Cannot reproduce the "interior see-through" signature.** It can only add a spherical highlight/overlay (a plausible reading of the user's "sphere"), and even that is bounded by the body's 6 % blend weight. Feedback is impossible for `CAR_XBOX`.

### 2.5 A/B lever

There is **no INI kill switch** for the vehicle env stage in this pipe. Nearest diagnostics:

- Debug menu `SkyGFX → Vehicle Env Map Size` set to 4 (`src/core/debugmenu_ui.cpp:257`). This resizes the **dynamic** reflection texture; `CAR_XBOX` does not use it (`envData->texture` is the game's static texture). If the sphere changes with this slider, the Env/PBR callback is drawing, not the Xbox callback — i.e. the mode is not really `CAR_XBOX`.
- Debug menu `SkyGFX → Vehicle Pipeline` → `PS2` (`debugmenu_ui.cpp:254`) to leave the Xbox env branch entirely.
- `[Modes] active = None` + `vehiclePipe = PS2` is the INI-only equivalent.

---

## 3. Blend / alpha derivation

### 3.1 The Xbox callback's decision

```
src/render/vehiclePipe.cpp:1536   if(instancedData->material->color.alpha == 0) continue;
src/render/vehiclePipe.cpp:1546   hasAlpha = instancedData->vertexAlpha != 0 ||
                                             instancedData->material->color.alpha != 255;
src/render/vehiclePipe.cpp:1547   RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);
src/render/vehiclePipe.cpp:1553   RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE);
```

There is **no paint-marker exclusion**. `16 != 255` → `hasAlpha = TRUE`.

### 3.2 The vertex shader repeats the alpha

```
src/render/vehiclePipe.cpp:1362-1390   SetupMaterial_Xbox(...)
    matColor = material->color;                     // if LIGHT && MODULATEMATERIALCOLOR
    RwRGBARealFromRwRGBA(&color, &matColor);        // 16 -> 0.0627
    RwD3D9SetVertexShaderConstant(LOC_matCol, &color, 1);

shaders/vehiclePipeVS.hlsl:281          OUT.Color = float4(0.0, 0.0, 0.0, matCol_xb.a);
```

So the pixel alpha of a paint texel is:

```
stage0 ALPHAOP = MODULATE(TEXTURE, DIFFUSE)      (vehiclePipe.cpp:1664-1666)
              = tex.a * vertexAlpha
              = 255/255 * 16/255 = 0.0627 (untextured paint, gpWhiteTexture)
```

This is the **same defect** the shared helper already documents and fixes:

```
src/render/pipelinecommon.cpp:458-477   pipeUploadMatCol()
    // SA paint marker: material alpha byte 16 is NOT a translucency request
    // ... painted surfaces end up ~6% opaque (see-through panels).
    if(m->color.alpha == 16)
        color.alpha = 1.0f;
```

`pipeUploadMatCol` is used by the PS2/PC/Specular/mobile callback via `vehiclePipe_setupMaterial` (`vehiclePipe.cpp:787`) and by the Env/PBR paths (`:2923`, `:3038`, `:3188`). The Xbox callback is the **only vehicle pipe that bypasses it** and uses its local `SetupMaterial_Xbox` (`:1362`), which lacks the clamp. That asymmetry is the root cause. The PS2 VS also hard-codes `COLOR0.a = 1.0` (`shaders/vehiclePipeVS.hlsl:43`); `main_xboxCar` does not.

### 3.3 Is the L2 mirror "correct"?

Mechanically, yes for the dual gate: `D3D9RenderDual` reads L2, and the mirror ensures the L2 value matches the per-mesh decision before the draw. Semantically, it is **fed the wrong decision** for the paint marker. Its comment claims it only fixes a stale cache, but it also makes blending **deterministically ON** for every material with `alpha != 255`, including alpha 16 paint. Before the mirror (aap reference), the L2 value was whatever the game left; the paint could draw unblended and look solid. After the mirror, it always blends.

Additional nuance: the Switch's `pipeEnterAlphaMode` forces L2 `ALPHABLENDENABLE=FALSE` (`pipelinecommon.cpp:1628`), and `pipeForceAlphaBlock` re-pushes the rw `VERTEXALPHAENABLE` (FALSE) to L2 (`buildingPipe.cpp:596`). So without line 1553 the dual gate would be dead in this build. The mirror is load-bearing for the dual pass and therefore load-bearing for this bug.

### 3.4 Why the live log cannot confirm the 16 (and how to fix it)

```
src/render/vehiclePipe.cpp:1433-1437
dbglog("[VehTex] tex='%s' null=%d bound=%p env=%d%d%d spec=%d matA=%u",
       name, null, bound, hasEnv1, hasEnv2, hasSpec, (unsigned)material->color.alpha);
```

8 conversions, 7 arguments. `spec` consumes `color.alpha`; `matA` consumes the next stack slot (observed: the `bound` pointer). This is confirmed independently in `docs/plans/2026-10-26-log-timeline-0113.md:416-432`. The 24 samples show `spec` (alpha) = 255/96 and **no alpha 16**; no alpha-16 mesh was sampled, and `matA` is garbage. Fixing this format string is a prerequisite for any future confirmation. The `[VehOpacity]` evidence from the Env path (`docs/plans/2026-10-24-vehicle-opacity-diagnosis.md:9-11`) remains the only live `matA=16` proof, but the material data is the same.

### 3.5 Verdict

**Can reproduce the signature: YES — this is the root cause.** On its own (dual pass off) it produces a ~6 % ghost body with depth writes; combined with dual pass it produces the full no-depth x-ray. The `hasAlpha` term also pulls in `vertexAlpha`, but the VS alpha is the material alpha, so vertexAlpha alone does not make body panels translucent unless the material alpha is <255.

### 3.6 A/B lever

- No config key controls the paint marker.
- **Best isolating test:** debug menu `SkyGFX → Vehicle Pipeline` → `PS2`. The PS2 callback calls `pipeUploadMatCol` (alpha 16 clamped to 1.0) and its VS outputs alpha 1.0. If the same car becomes solid in PS2 but ghosts in Xbox, the Xbox-local alpha path is confirmed.
- Combined with the §1 dual-pass toggle to separate "6 % ghost" from "no depth".

---

## 4. Glass classification

`VehShaders_IsGlassTexture(texName, hasAlpha, matA)` lives at `src/extras/veh_shaders.cpp:239-252`:

```
if(texName && surface == SURFACE_CAR_GLASS) return true;
if(!hasAlpha || alpha >= 200) return false;
... return true;                       // alpha<200 fallback
```

The Env/PBR callback calls it at `src/render/vehiclePipe.cpp:2720`, computes `glassAlphaArg = (tac == TAC_FADED) || matTranslucent` (`:2716-2719`), and applies the ghost-car demotion guard at `:2724-2730` (the fallback only survives if the texture really resolves to `SURFACE_CAR_GLASS`).

The **Xbox callback never calls this function**. There is no `Glass_Vehicle` shader bind, no fresnel alpha, and no glass classification in `1441-1713`. Glass on the Xbox path is just alpha test + dual pass. Therefore the "body panels promoted to Glass_Vehicle" mechanism is **not reachable** when `vehiclePipe = CAR_XBOX` (the log confirms `pipe=XBOX(2)`). It would only apply if the mode/pipeline were actually Env/Modern.

### 4.1 Verdict

**Cannot reproduce the signature in this mode.** The demotion guard is irrelevant to the Xbox callback; it is an Env-cb fix.

### 4.2 A/B lever

`[Modes] active = Modern` (or debug menu Vehicle Pipeline → Modern) engages the Env cb and its `[VehOpacity] isGlassMesh` diagnostics. If the ghost persists identically in Xbox and only the Env mode shows `isGlassMesh=1` on body textures, glass classification is exonerated for the Xbox issue.

---

## 5. Texture binding

### 5.1 s0 (diffuse) is correctly resolved

```
src/render/vehiclePipe.cpp:1659-1661   diffuseBound = material->texture ? material->texture : gpWhiteTexture;
                                        pipeSetTexture(material->texture, 0);
src/render/vehiclePipe.cpp:1671-1673   diffuseBound = gpWhiteTexture;
                                        pipeSetTexture(NULL, 0);
```

`pipeSetTexture` is the inline `RwD3D9SetTexture(t ? t : gpWhiteTexture, n)` (`src/skygfx.h:1272`). A NULL paint texture therefore binds the game's opaque white texture, not a stale raster. Stage 0 is re-authored per mesh (`:1662-1669` / `:1674-1677`). No raw `RwD3D9SetTexture` remains for s0.

### 5.2 s1 (env) is the only raw bind

`RwD3D9SetTexture(envData->texture, 1)` at `:1593` and `:1627`. Neither resolves NULL to `gpWhiteTexture`, and neither uses `vehEnvBindTexture`/`pipeSetTexture` (`:2248-2264`). As in §2.3, a NULL env texture can leave a stale raster and add a colour blob, but cannot make an opaque body transparent. The `RwD3D9SetTexture(NULL, 1)` at `:1556` is the per-mesh stage1 unbind; it does not clear the stage-1 texture if it is already NULL (RW cache no-op), but stage1 ops are disabled immediately after (`:1557-1560`), so it is harmless.

### 5.3 Verdict

**Cannot reproduce the signature.** The missing-NULL-texture class (R2-04) is already fixed for s0 by `pipeSetTexture`; the remaining s1 raw binds are a colour/hygiene risk only.

### 5.4 A/B lever

None (code-owned). If a texture-shaped blob is suspected, the debug menu Vehicle Pipeline → PS2 uses the same `pipeSetTexture` contract and would render the same s0 result.

---

## 6. Cull

```
src/render/vehiclePipe.cpp:1416   static RwUInt32 s_vehSwitchSavedCull = rwCULLMODECULLBACK;
src/render/vehiclePipe.cpp:3591   s_vehSwitchSavedCull = savedCull;      // raw pre-entry value from pipeEnterCullMode
src/render/vehiclePipe.cpp:1531   pipeForceCullMode(s_vehSwitchSavedCull);
```

- The callback re-asserts the **saved raw** cull through all three layers (`pipeForceCullMode`, `pipelinecommon.cpp:1202-1250`); it does **not** hardcode `BACK` or `FRONT`.
- `pipeEnterCullMode` sanitizes only out-of-domain values; `NONE` (mirror) and `FRONT` (stencil shadow volume) are preserved by design (`pipelinecommon.cpp:1255-1300`).
- The log shows `pipeEnterCullMode: saved=0x1` and `saved=0x2` at different times; those are legitimate different passes (normal BACK vs shadow-volume FRONT). A normal-view vehicle with `FRONT` cull would show backfaces/interior, but that would be an enclosing-pass bug, not a hardcoded cull in this callback. The user's symptom is present in ordinary 3/4 views, and the cull log alternates with the pass.
- The callback's per-mesh draws are not preceded by any `D3DRS_CULLMODE` write, so the assert at `:1531` is the only cull state for the whole mesh loop.

### 6.1 Verdict

**Cannot reproduce a consistent 3/4-view x-ray.** Only an entire-pass inversion (the switch's saved value being FRONT/NONE in the main scene) could do this, and the code correctly preserves the caller's intent rather than inverting it. Confidence that cull is *not* the cause: 95 %.

### 6.2 A/B lever

None (code-owned). Use the log tag `[Cull] enter saved=... forced=...` (`pipelinecommon.cpp:1240`) to confirm the main-view vehicle draws are BACK.

---

## 7. Stencil / depth

- `depthHookEnable` is forced to 1 by the Xbox profile (`modeManager.cpp:297`). The hook creates an `INTZ` texture (`depthhook.cpp:88-90`, `246-248`, `294-296`) and binds its surface as the scene depth-stencil. `D3DFMT_INTZ` is depth-only — **no stencil bits**.
- The Xbox vehicle callback never reads/writes stencil (`1441-1713`). It never touches `D3DRS_STENCIL*`, and the dual pass only changes `ALPHAFUNC/ALPHAREF/ZWRITE`.
- `DepthHook_Suspend/Restore` disables/re-enables `ZENABLE` around post-fx fullscreen passes (`depthhook.cpp:509-561`). If that leaked, **all** scene geometry would lose depth, not just vehicles. The log shows the vehicle Switch's ZWRITE repair but no ZENABLE/ZFUNC anomaly (`[VEHICLE] ZWRITE off/desynced` is the ZWRITE layer only).
- A depth-only INTZ can break stencil-based shadow volumes, but that affects shadows, not the car body. No evidence connects it to the vehicle x-ray.

### 7.1 Verdict

**Cannot reproduce "edges correct, interior see-through" for vehicles.** Confidence 98 %. Note it as a shadow-only candidate.

### 7.2 A/B lever

`[SkyGfx] depthHookEnable=0` — but it is mode-owned (Xbox profile line 297), so use the debug menu `SkyGFX → Depth buffer hook` (`debugmenu_ui.cpp:1045`) or `[Modes] active=None`. If the vehicle x-ray changes, the depth hook is involved; if it does not (expected), it is exonerated.

---

## 8. Signature matrix — could this reproduce "edges correct, interior see-through"?

| # | Mechanism | See-through body? | Edges/trim correct? | Depth removed? | Verdict |
|---|---|---|---|---|---|
| 1 | Dual pass (`dualPassVehicle=1`) | Only with alpha <128 | Yes (alpha 255 draws in leg 1 or the single path with `ZWRITE=TRUE`) | **Yes** — leg 1 rejects, leg 2 has `ZWRITE=FALSE` | **YES, as amplifier** |
| 2 | Env/sphere map on s1 | No | N/A | No | **NO** (highlight/overlay only; no feedback for `CAR_XBOX`) |
| 3 | Alpha derivation (`hasAlpha` + paint marker 16 + VS alpha) | **Yes** (6.3 %) | Yes | No (alone) | **YES, root cause** |
| 4 | Glass classification | No in Xbox cb | N/A | No | **NO** (Env-only code path, ghost guard at `:2724-2730`) |
| 5 | Texture binding (s0 NULL→white) | No | N/A | No | **NO** (s0 resolved; s1 raw is colour-only) |
| 6 | Cull | Only if the whole pass is FRONT/NONE | N/A | No | **NO** (saved raw cull preserved) |
| 7 | Stencil/depth hook | No | N/A | No (ZENABLE/ZFUNC unchanged; would affect all geometry) | **NO** |

The only row that reproduces **both** halves (silhouette correct + body interior visible) is **1 + 3 together**.

---

## 9. Ranked top three (confidence %)

1. **Paint-marker alpha 16 + per-mesh L2 blend mirror** — **85 %** as the root cause of the 6 % translucency; **90 %** that it is a necessary link in the observed frame. Evidence: `pipelinecommon.cpp:458-477` documents the exact "~6 % opaque (see-through panels)" defect and clamps it; `SetupMaterial_Xbox` (`vehiclePipe.cpp:1362-1390`) is the only vehicle material path without the clamp; `main_xboxCar` (`shaders/vehiclePipeVS.hlsl:281`) writes `matCol.a`; `hasAlpha` (`vehiclePipe.cpp:1546`) includes 16; the mirror (`:1553`) turns blending on. Caveat: the 24 runtime `[VehTex]` samples do not include a confirmed alpha-16 mesh and `matA` is corrupt, so a one-line diagnostic fix is part of the patch plan.
2. **Forced `dualPassVehicle=1` by the Xbox profile** — **85 %** that it is the amplifier that produces the *no-depth* x-ray; **60 %** standalone (it needs #1). Evidence: `modeManager.cpp:218` vs `skygfx.ini:53`; `main.cpp:277-294`; log `[Mode] apply Xbox`; the gate is only live because of the `:1553` mirror. This is the reason the car reads as a 3/4 x-ray rather than a simple ghost.
3. **Env-map / sphere-map on s1** — **15 %** for the "sphere" visual; **<5 %** for the see-through. It can add a spherical highlight and the raw s1 bind is a real hygiene bug, but `CAR_XBOX` never renders the sphere RT (`envmap.cpp:1126`) and the env stage cannot remove body depth.

Not ranked (cannot reproduce the signature): glass (3 %), texture binding (3 %), cull (2 %), stencil/depth hook (1 %).

---

## 10. Ordered patch plan

| Order | File:line | Exact change | Risk | Why / expected effect |
|---|---|---|---|---|
| P0 | `src/render/vehiclePipe.cpp:1433` | Fix the format string from `"tex='%s' null=%d bound=%p env=%d%d%d spec=%d matA=%u"` to `"tex='%s' null=%d bound=%p env=%d%d spec=%d matA=%u"` (drop one `%d`). Optionally append `paint=%d` and pass `material->color.alpha == 16`. | None (diagnostic only) | Stops `matA` printing the `bound` pointer; makes the next run able to confirm/deny alpha 16 on paint meshes. |
| P1 | `src/render/vehiclePipe.cpp:1389-1390` (`SetupMaterial_Xbox`) | After `RwRGBARealFromRwRGBA(&color, &matColor);` add: `if(material->color.alpha == 16) color.alpha = 1.0f;` | **Low** | Mirrors `pipelinecommon.cpp:470`. Forces the Xbox VS `COLOR0.a` to 1.0 for the SA paint marker. With dual pass on, leg 1 (`GREATEREQUAL@128`) now draws paint opaque and writes depth; leg 2 rejects it. This alone should remove the x-ray if the body texture alpha is opaque; if the body texture alpha is 0, P1 cannot help and P6 is the relevant branch. |
| P2 | `src/render/vehiclePipe.cpp:1546` | Exclude the marker: `RwUInt32 matA = instancedData->material->color.alpha; hasAlpha = instancedData->vertexAlpha != 0 || (matA != 255 && matA != 16);` | **Low** | Stops the L2 blend mirror and the dual gate from firing for paint. The dual gate is a `hasAlpha` read, so this also skips the pointless second draw for paint. If a paint mesh truly needs alpha, this would make it opaque; SA semantics say 16 is not translucency. |
| P3 | `src/render/vehiclePipe.cpp:1593` and `:1627` | Replace `RwD3D9SetTexture(envData->texture, 1)` with `pipeSetTexture(envData->texture, 1)` (NULL→`gpWhiteTexture`), or hoist a declaration for `vehEnvBindTexture` and use it. | **Low** | Removes the last raw s1 bind and the stale-raster/white-blob class. Not the x-ray fix. |
| P4 | `src/render/vehiclePipe.cpp:1598` (env1 branch) | Add `RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);` next to the env1 stage-1 `COLOROP/ALPHAOP` writes. | **Low** | env1 samples the spherical `Texcoord1`, not UV0. Fixes the first-env1-mesh / state-drift env smearing. |
| P5 | `src/core/modeManager.cpp:218` | Only after P1/P2 A/B: change `MV_I(dualPassVehicle, 1)` to `MV_I(dualPassVehicle, 0)` (or `MV_IK(dualPassVehicle, "dualPassVehicle", 0)` if inheritance is desired). | **Medium** (profile parity; the Xbox preset intentionally mirrors aap) | Restores what `skygfx.ini:53` asks for. With P1/P2 the dual pass is no longer needed for paint, so this is a perf/robustness choice, not the primary fix. |
| P6 | Conditional — `src/render/vehiclePipe.cpp:1546-1553` / draw boundary | If the §11 A/B shows the body is **solid** with dual pass off (i.e. the ghost is texture-alpha driven, not matA 16), add an FFP opaque-paint path: for `material->color.alpha == 16` (or `material->texture == NULL`), force `D3DRS_ALPHATESTENABLE=FALSE` (or `ALPHAFUNC=ALWAYS`) for that mesh and restore it after the draw. | **Medium** | FFP analogue of the PBR `shaderAlpha=1.0` override for textures whose alpha channel is an unused fill. Do **not** apply blindly; it would punch holes in genuinely cut-out paint decals. |
| P7 | `src/render/vehiclePipe.cpp:1679` | Add a one-shot `dbglog` (or extend `vehTexDiag`) printing `matA`, `hasAlpha`, `config->dualPassVehicle`, `zwrite`, and `D3D9RenderDual`'s gate result for the first NULL-texture / alpha-16 mesh. | None (diagnostic) | Gives the A/B a receipt instead of relying on the corrupt `[VehTex]` line. |

Order of application: **P0 → P1 → P2** (one build), then A/B verify; P3/P4 are hygiene in the same or the next build; P5/P6 only after the A/B result. Do not change P5 before P1/P2 — the dual pass is the diagnostic lever for §11.

---

## 11. Single best A/B test to run first

**Test: turn off `Dual-pass Vehicles` in the live debug menu and look at the same 3/4 car.**

1. Start the game in `[Modes] active = Xbox`.
2. Open the debug menu: `SkyGFX|Advanced → Dual-pass Vehicles` (`debugmenu_ui.cpp:313`) → **OFF**.
3. Drive/park a car at the same 3/4 angle as the broken capture. Do **not** press F11 / reload the INI / change mode (the Xbox profile will re-apply `dualPassVehicle=1`).
4. Record the result and, if possible, a screenshot + the `[Mode]`/`[PipeAlpha]`/`[VehTex]` log lines.

Interpretation:

| Result | Conclusion | Next patch |
|---|---|---|
| Body stays **~6 % ghosted but the interior/far side is hidden** | Case A: `matA=16`, texture alpha opaque, `hasAlpha=TRUE`. The blend is still on, but the single path writes depth. This is the **expected result** and confirms the alpha-16 + dual-pass pair. | P1 + P2 |
| **No change**; body is still **holey / partly invisible** rather than uniformly ghosted | Case B/D: `hasAlpha` is FALSE (or the texture alpha is 0), so the dual gate was never active for the body. The alpha test is discarding the body on texture alpha, not material alpha. The paint-marker fix alone cannot help. | P6 (FFP opaque-paint / texture-alpha path), plus P0/P7 to log the real matA |
| Body becomes **solid** (no ghost, no interior) | The dual pass was the trigger, but the alpha must have been ≥128 in the single path (or a different material/UV set is involved). Re-check `matA` and `hasAlpha` before trusting; this outcome is not predicted by the Case A/B/D model. | P0/P7 first, then re-evaluate |
| Body still **fully ghosted with interior visible** (no change) | The toggle did not take effect (profile re-applied) or another pass is drawing the car. Check the live `dualPassVehicle` value and `[VPIPE] cb=Xbox`. | Re-run with `[Modes] active=None` + `dualPassVehicle=0` |

**Second test (confirmation, one minute):** debug menu `SkyGFX → Vehicle Pipeline` → **PS2**. The PS2 callback uses `pipeUploadMatCol` (alpha 16 → 1.0) and its VS outputs alpha 1.0 (`vehiclePipeVS.hlsl:43`). If the same car is solid in PS2 and ghosts in Xbox, the Xbox-local `SetupMaterial_Xbox` alpha path is confirmed and P1/P2 are validated.

**Third test (sphere attribution):** at the same spot, change `SkyGFX → Vehicle Env Map Size` to 4. The Xbox callback uses the static game env texture (`envData->texture`) and ignores `reflectionTex`, so the "sphere" should **not** change. If it does change, the mode is not really `CAR_XBOX` (the Env/PBR callback is drawing) and the `gRenderingSpheremap` / `VehShaders_IsGlassTexture` lanes become relevant.

---

## Appendix — evidence index (quick lookup)

| Fact | File:line |
|---|---|
| Xbox callback body | `src/render/vehiclePipe.cpp:1441-1713` |
| Paint alpha `hasAlpha` includes 16 | `src/render/vehiclePipe.cpp:1546` |
| Per-mesh L2 blend mirror | `src/render/vehiclePipe.cpp:1553` |
| Dual-pass call (no `TexInfo`) | `src/render/vehiclePipe.cpp:1679` |
| `SetupMaterial_Xbox` (no alpha-16 clamp) | `src/render/vehiclePipe.cpp:1362-1390` |
| `pipeUploadMatCol` alpha-16 clamp (all other pipes) | `src/render/pipelinecommon.cpp:458-477` |
| PS2 VS alpha hard-coded 1.0 | `shaders/vehiclePipeVS.hlsl:43` |
| Xbox VS alpha = `matCol.a` | `shaders/vehiclePipeVS.hlsl:281` |
| Dual-pass legs | `src/core/main.cpp:277-294` |
| L2 gate read | `src/core/main.cpp:259` |
| `pipeEnterAlphaMode` L2 blend FALSE | `src/render/pipelinecommon.cpp:1628` |
| `pipeForceAlphaBlock` L2 blend from vtx | `src/render/buildingPipe.cpp:596` |
| Xbox profile `dualPassVehicle=1` | `src/core/modeManager.cpp:218` |
| Xbox profile `zwriteThreshold=128` | `src/core/modeManager.cpp:279` |
| Xbox profile `depthHookEnable=1` | `src/core/modeManager.cpp:297` |
| INI `dualPassVehicle=0` | `skygfx.ini:53` (game dir) |
| Debug menu dual-pass toggle | `src/core/debugmenu_ui.cpp:313` |
| Debug menu vehicle-pipe toggle | `src/core/debugmenu_ui.cpp:254` |
| Debug menu env-map size | `src/core/debugmenu_ui.cpp:257` |
| Debug menu depth-hook toggle | `src/core/debugmenu_ui.cpp:1045` |
| Env s1 raw bind | `src/render/vehiclePipe.cpp:1593`, `:1627` |
| Env1 missing `TEXCOORDINDEX=1` | `src/render/vehiclePipe.cpp:1598` vs `:1637` |
| Mobile sphere guard | `src/render/vehiclePipe.cpp:2085-2088` |
| Env sphere guards | `src/render/vehiclePipe.cpp:2466-2472`, `:2954-2957`, `:3326-3329` |
| Sphere pass excludes `CAR_XBOX` | `src/render/envmap.cpp:1126` |
| `VehShaders_IsGlassTexture` | `src/extras/veh_shaders.cpp:239-252` |
| Env glass call + ghost guard | `src/render/vehiclePipe.cpp:2720`, `:2724-2730` |
| Xbox cull re-assert | `src/render/vehiclePipe.cpp:1531`, `:1416`, `:3591` |
| `pipeForceCullMode` | `src/render/pipelinecommon.cpp:1202-1250` |
| INTZ creation (no stencil) | `src/render/depthhook.cpp:88-90`, `:246-248`, `:294-296` |
| `[VehTex]` vararg bug | `src/render/vehiclePipe.cpp:1433-1437`; `docs/plans/2026-10-26-log-timeline-0113.md:416-432` |
| Live `matA=16` evidence (Env path) | `docs/plans/2026-10-24-vehicle-opacity-diagnosis.md:9-11` |
