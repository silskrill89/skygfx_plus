# Interior vs exterior / mirror vs main — divergence audit

Date: 2026-10-26. Read-only audit of `src/` + `shaders/`; only this file written.
Symptom: save loads inside CJ's house; interior almost black (walls/floor/furniture),
one large pale slab, black plant/stove, CJ crushed to black; outdoors much brighter.
Tester: mirror/reflection copy looks correct.

Evidence (live log, build 2026-10-26 04:14:57; `buildingPipe=Xbox`, `vehiclePipe=Xbox`,
`altTimecycEnable=0`, `pbrAmbientFloor=0.05`, `pedAmbientFloor=0.20`):

```
04:22:52.314 [PBR-Ambient] final=(0.050,0.050,0.050) floor=0.050 raw=(0.000,0.000,0.000)
04:22:52.334 [Ped] ped_amb_cmp pre=(0.000,0.000,0.000) tc=(0.050,0.050,0.050)
             post=(0.200,0.200,0.200) floor=0.200
04:22:52.402 [BLIT] cb=Xbox amb=(0.050,0.050,0.050) dnBal=0.000(day=1 night=0)
             pDirect(rgb=0.000,0.000,0.000 lflags=0x0) clock=10:08
04:22:52.454 [MirrorPass] hook: TypeOfMirror=2 pBuffer=1DEFAC90 (2048x1024)
04:22:52.810 CreateShaders started  (device Reset on save load)
```

Indoor world ambient is exactly `(0,0,0)`, `pDirect` is exactly `(0,0,0)`; the only
dynamic interior light reaching the Xbox building pipe is `pbrAmbientFloor` (0.05),
everything else is baked vertex prelight. This is the frame being explained.

**Screenshot caveat (parallel capture forensics, 04:22-04:25 frames).** The 10:11
reference frame is uniformly faded by ~0.23 **including HUD, radar and marker**
(F1 HUD max 55 vs 240 in later frames); multiplying F1 by 4.4 recovers a normally
lit room. In the steady frames the "large pale slab" is the **bed** (real mesh:
wood frame/border visible, bbox moves with camera, occluded by CJ); the black
blobs on it are shadows with `2,2,2` against lit sheet `98-219`. So a foreign
screen-space slab is **not proven** by the captures; treat a slab report as the
bed until a non-bed mesh is isolated. The real defects are the mirror/main shadow
difference and low fill, covered below.

## 1. Every `CGame__currArea` read in `src/`

Definition `src/rw/gta.cpp:32` (`int* = 0xB72914`), declaration `src/rw/gta.h:184`.

| Site | What it gates |
|---|---|
| `pipelinecommon.cpp:91-93` | **Early return in `ClampAmbientToSky`; skips the sky clamp AND the `ambientFloor` tail at `:143-168`.** |
| `pipelinecommon.cpp:1067-1078` | Suppresses the `g_iblTex` capture refresh indoors. |
| `buildingPipe.cpp:2445` | PBR building PS c20.x (sky tint) = 0 indoors. |
| `buildingPipe.cpp:2461-2472` | PBR building s3 neutral 1x1 white instead of `g_iblTex`. |
| `buildingPipe.cpp:2903-2907` | PBR building `bldEnv.z` (sky reflect) = 0 indoors. |
| `vehiclePipe.cpp:545-553`, `1517-1526` | Only excludes interiors from the `PlayerNoRain` 0.55 shelter dampening. |
| `vehiclePipe.cpp:3453-3460` | Vehicle PBR c20.x = 0 indoors. |
| `postfx_colourfilter.cpp:323` | Tonemap `isInterior`; only used for `isSheltered = PlayerNoRain && !isInterior` (`:364-366`). No interior exposure lift. |
| `neoCarpipe.cpp:556-558` | Same shelter-only dampening. |
| `neoWaterdrops.cpp:421` | Rain drops off. |
| `buildingPipe.cpp:2228`, `pipelinecommon.cpp:1637` | Comments only. |

No pipe/model-style/callback selection reads `currArea`; interiors use the same
building/vehicle/ped pipes. **Pointer-compare latent bug:** `CGame__currArea == 0`
(compares address `0xB72914`, always false) survives in the uncompiled reference
`backup_original/src/envmap.cpp:210` (flagged in
`docs/plans/2026-10-20-video-evidence-wave3-plan.md:87`). Live
`src/render/envmap.cpp:438` is correct (`*CGame__currArea == 0`); no live
pointer-compare remains in `src/`.

## 2. Ambient sources — exact trace

`GetTimecycleAmbient()` (`pipelinecommon.cpp:171-194`) =
`CTimeCycle_GetAmbient*()` (`m_CurrentColours`, incl. interior extra-colour row)
× `CCoronas__LightsMult` (`buildingPipe.cpp:142`, read `:181-182`) × `ambientMult`;
then `ClampAmbientToSky` (`:192`). That function returns immediately indoors
(`:91-93`), **before** the night law and the additive `ambientFloor` (`:143-168`,
default 0.03). `GetTimecycleAmbientPBR()` (`:196-234`) then adds
`pbrAmbientFloor` (`:205-216`, default 0, live 0.05); no re-clamp.
`legacyAmbient` does not exist in `src/`; `s_tcAmbient` (`:66`) /
`GetTimecycleAmbientRaw` (`:236-240`) are dead because `UpdateTimecycleLighting`
(`:247-256`) is never called.

Per pipe (indoor value):

| Pipe | Ambient | Floor? | IBL/sky | Extra dir lights |
|---|---|---|---|---|
| Xbox building (`buildingPipe.cpp:183`, c4 `:1965-1968`) | `buildingAmbient` | `pbrAmbientFloor` only; `ambientFloor` skipped | none | **none** |
| PBR building (`:2392-2407`, c24 `:2440-2446`) | same | same | c20/s3/bldEnv off (`:2445/2461/2906`) | **zeroed** (`:2401-2407`) |
| PS2/GTAIV building (`:1384-1386`) | same | same | env branch only | none |
| Sphere/mirror building (`:2152-2155`) | same | same | none; bright `simpleFogPS` | none |
| Xbox vehicle (`vehiclePipe.cpp:540`, `:556`) | same | same | c20=0 (`:3455`) | **yes** (`:560-566`, `:3476-3482`) |
| Ped skin (`main.cpp:2068-2138`) | `GetTimecycleAmbientPBR`, max with game `pAmbient`, then `pedAmbientFloor` 0.20 | **yes, indoors** | n/a | n/a |
| Default pipe (`main.cpp:1782-1837`) | game `pAmbient` (`gta.cpp:11`) = raw world ambient | **no** | n/a | n/a |

Live proof of the split: `[PBR-Ambient] raw=0 -> 0.05`, `[Ped] pre=0 post=0.20`,
`pDirect=0`.

## 3. Interior-specific engine paths

- `CInteriorManager` is not referenced in `src/`; it is game-side FX/ped/furniture
  management, not a render pipe.
- `CCullZones`: `PlayerNoRain` at `vehiclePipe.cpp:544/1518`, `neoCarpipe.cpp:555`,
  `postfx_colourfilter.cpp:364`; `CamNoRain` at `neoWaterdrops.cpp:421`.
  Vehicle/neo explicitly subtract interiors (`&& !isInterior`), so an interior
  keeps full (zero) ambient instead of 0.55 dampening.
- `CCutsceneMgr`: ped cutscene floor `main.cpp:2080-2091`, water drops
  `neoWaterdrops.cpp:673`; no building/vehicle interior gate.
- `CRenderer::RenderEverythingBarRoads`: one function; the only interior branch is
  the alpha-ref gate `if(!CGame::currArea) ref=140` (game `Renderer.cpp:370`;
  duplicated at `envmap.cpp:438`). No interior renderer variant.
- `CVisibilityPlugins`: sorting/fading `envmap.cpp:304-309`, tag value
  `buildingPipe.cpp:319/331`; no interior branch.
- `CMirror/pBuffer`: `mirrorPass.cpp:67-68`, `envmap.cpp:902` (see §4).
- Style selection is `CCustomBuildingRenderer__IsCBPCPipelineAttached`
  (`buildingPipe.cpp:3451-3463`) by pipe ID/extra colours/prelight, **not** by interior.
- `myDefaultCallback` (`main.cpp:8096`) covers every atom with no custom callback;
  default branch `:1782-1837`, skin branch `:1781/1839+`; only skin overrides
  `pAmbient` (`:2068`). Plant/stove/furniture are default-pipe atoms that miss it.

## 4. Mirror/reflection vs main — what differs

### 4.1 Wall mirror (`CMirrors`)

`CMirrors::BeforeMainRender` renders `RenderScene()` into `pBuffer`/`pZBuffer` with
a mirrored camera, clears black, sets `bRenderingReflection=true`, then
`RenderMirrorBuffer` blits the buffer to a full-screen quad; main scene follows.
Our code intercepts only the **blit** call site `0x53EABA` (`mirrorPass.cpp:532`);
the **main** `RenderScene` call site `0x53EABF` is `main.cpp:5487`. The mirror's
`RenderScene()` is a different call site, so `RenderScene_hook`, `RenderScene_before`,
`Weather_Update`, `Weather_ApplySunConfig` and `RenderScene_after` **do not run for
the mirror**. Main gets per-frame weather override (`weather.cpp:285-299/385-409`),
world baseline (`main.cpp:3755`), camera-raster repair (`:3853-3907`), Forward+
(`:3816`); mirror gets only `DefinedState()`. Mirror uses pBuffer (log 2048x1024)
and a 2:1 aspect; main uses 1920x1080. Mirror is black-cleared; main has the
indoor scene.
**Shadows are a main-only pass:** the game calls `CShadows::RenderStaticShadows` /
`RenderStoredShadows` only when `!CMirrors::bRenderingReflection` (game
`Main.cpp:3316`; our hooks `main.cpp:1648-1665`, install `:8126-8224`). The
mirror therefore has **no shadow geometry**, while the main view draws it. If the
shadow contract leaves the shadow quads too opaque/dark (or a stale blend/depth
state), the main gets `2,2,2` blobs on lit surfaces and dark CJ sides; the mirror
looks "correct". This is the best discriminator for the pitch-black regions.

### 4.2 Env/sphere reflection (`envmap.cpp`)

Swaps the main camera raster to `envFB` (`:1045-1046`), far clip 120
(`:1047`), fog 90 (`:1048`), clears to `skyTop` with channels floored at 64 and
mixed with white (`:1059-1066`), sets `gRenderingSpheremap`/`bFudgeNow`
(`:1084-1085`), then `CarPipe::RenderReflectionScene()` (`:1086-1096`).
`CarPipe::RenderReflectionScene` (`neoCarpipe.cpp:264-289`) calls `RenderRoads`
(which calls `DeActivateDirectional`/`SetAmbientColours`) +
`RenderEverythingBarRoads` + fading entities. The Switch dispatches
`CustomPipeRenderCB_Sphere` (`buildingPipe.cpp:3098-3099`), which binds
`sphereBuildingVS` + `simpleFogPS` (`:2113-2116`).
`sphereBuildingVS.hlsl:39-53` uses c0 as **world matrix** (not WVP), computes
`Color = DayColor*day + NightColor*night + ambient*surfAmb` — **no `surfDiff`,
no direct sun**; `simpleFogPS.hlsl:15-21` lerps to the bright `spheremapfog`.
The main Xbox/PBR/GTAIV shaders use `prelight*surfDiff + ambient*surfAmb (+sun)`
(`xboxBuildingVS.hlsl:75`, `buildingPBRVS.hlsl:109`, `GTAIVBuilding_vs.hlsl:92`).
This is the exact "mirror brighter than main" discriminator.
Other diffs: `envmap.cpp:438` alpha ref 140 outdoors only; `mirrorPass.cpp:256-272`
composite Z off/ONE-ZERO/full-screen (disabled, `mirrorDistortionFix=0`);
`pipelinecommon.cpp:1306-1328` mirror CULLNONE, PBR hardcodes BACK at
`buildingPipe.cpp:2318` (Xbox preserves saved cull `:1822`). The composite never
sets a viewport (it uses the current one) and never touches `pRasterFrontBuffer`;
the main-view front-buffer copy is `UpdateFrontBuffer` (`postfx.cpp:2474`, log
`pRasFB=2048x2048`, `camRas=1920x1080`). Camera LTM: the mirror uses
`TheCamera.SetCameraUpForMirror`; the sphere pass keeps the main camera LTM and
only swaps the raster.

## 5. Pale-slab class

1. **Stale transform (strongest).** `_RwD3D9ActiveViewProjTransform` (0xC97C64),
   `D3D9ActiveTransform` (0xC94C70) `pipelinecommon.cpp:262-267`;
   `_rwD3D9VSGetComposedTransformMatrix` patched `main.cpp:8076-8077`; it
   recomposes only when the cache pointer is NULL (`:283-295`) and uses
   `D3D9ActiveTransform` when non-NULL. `waterPipe.cpp:1216-1231` documents the
   failure and NULLs both. In `src/` the only writers are the patched getter
   (`pipelinecommon.cpp:285`) and that water workaround (`:1229`, which restores
   the old world matrix); nothing invalidates the cache after the mirror pass.
   Building/vehicle use the live
   `_RwD3D9D3D9ViewTransform/Proj` via `pipeGetComposedTransformMatrix`
   (`pipelinecommon.cpp:374-388`), so only **default/skin** geometry is exposed.
   The mirror pass updates the mirror camera VP; main draws after
   `RestoreCameraAfterMirror` → props/peds projected with the mirror camera.
   Test: NULL both at `RenderScene_before`.
2. **NULL texture / white fallback.** `buildingPipe_bindTexStage`
   (`buildingPipe.cpp:1161-1174`) and `pipeSetTexture` (`skygfx.h:1272`) resolve
   NULL to `gpWhiteTexture` (`gta.cpp:17`). `main_simple`
   (`buildingPipePS.hlsl:25-28`) returns `white * vertexColor`; Xbox env branch
   (`buildingPipe.cpp:1986-2006`) adds `tex1*envcolor` with `lightmult=1`, no
   saturate, no `bldEnvReflect` gate, and `envData` deref at `:1991` with no NULL
   check. PBR guards env (`:2844-2891`); Xbox/PS2 do not. Test: skip env when
   `envData==NULL || envData->texture==NULL`, or bind black instead of white.
3. **Vertex declaration/FVF.** `buildingPipe_setupResEntry` (`:345-353`) binds the
   per-atomic declaration from `DNInstance_PS2` (`:3230-3380`). Prelit geometry
   declares COLOR0+COLOR1 (`:3268-3277`); non-prelit gets only COLOR0 filled white
   (`instWhite` `:3195/3373`), while `xboxBuildingVS.hlsl:39-46` declares both;
   missing COLOR1 reads D3D9 default white. `setDnParams` forces night=1
   (`:279-286`). A non-prelit/white mesh or a mismatched declaration → flat pale
   or screen-spanning triangles. Test: log `D3DVERTEXELEMENT9`/`flags`; force
   `ps2BuildingVS`.
4. **Lazy shader dead after Reset.** `depthhook.cpp:389-396` says it: a dead VS
   bind fails silently, leaving the previous VS bound (giant/exploded geometry).
   `Pipeline_InvalidateShaders`/`CreateShaders` (`pipelinecommon.cpp:718-885`)
   cover CreateShaders handles, not lazy loaders: `ivFPReady`
   (`buildingPipe.cpp:1057-1069`), `mirrorCompositePS` (`mirrorPass.cpp:145-149`),
   `FxLoadShaders` (`postfx_newfx.cpp:85-105`), `UWLoadShader`
   (`postfx_underwater.cpp:281-289`), `GrassInstPixelShader`
   (`PC_GrassInstancer.cpp:103-126`). Live log shows a save-load Reset
   (`04:22:52.810`); main building handles recreated with 0 failures, so this is
   only for IV/mirror/FX/underwater/grass.
5. **Camera-raster/viewport.** `RenderScene_before` repairs a foreign raster only
   when the cache is valid (`main.cpp:3874-3907`); repair is disarmed when stale
   (`:3894-3906`). Log shows `[BUILDING] camRas=1DEFAC90 2048x1024` (mirror) and
   `512x512` (env) during building callbacks; a world drawn into a 2:1/square
   raster with a 1920x1080 viewport is stretched.

Ranked pale slab (for a **non-bed** foreign slab; the known captures are the bed):
(1) stale Active VP, (2) NULL texture/white env, (3) lazy shader after Reset;
then declaration/FVF, then raster/viewport.

## 6. Black plant / stove

**Unlit-material victims, not TAC victims.** `TexAlpha_Classify` (`texalpha.cpp:318-330`)
turns uniform-mid alpha into OPAQUE (live `green_glass_64` FADED→OPAQUE); a real
FADED only sets `blendOn=1` (`buildingPipe.cpp:1878-1879`) and can ghost/darken, but
cannot zero RGB. If on the building pipe, light = `prelight*surfDiff +
ambient(0.05)*surfAmb + sun(0)`; `surfAmb` is often 0, `surfDiff` is material
diffuse (fallback 1 only when diffuse==0), so a low nonzero diffuse darkens the
prelight. If on the default pipe, there is **no pAmbient override** (skin-only,
`main.cpp:2068`); the game's pAmbient indoors is 0 (`pre=(0,0,0)`), so only prelight
remains. Interior point lights (`pExtraDirectionals`) are zeroed in the building
callbacks (`buildingPipe.cpp:2401-2407`) and never uploaded by the default pipe.
WIND path (`xboxBuildingWindVS.hlsl:117`) has the same `prelight*surfDiff`.
Order: default pipe with pAmbient=0 + dark prelight → building pipe with
`ambientFloor` bypassed + `surfDiff<1` → TAC_FADED (visual only).

## 7. Ordered divergences (file:line — differs — predicted visual)

0. **A** `CMirrors::bRenderingReflection` shadow gate (game `Main.cpp:3316`; our
   hooks `main.cpp:1648-1665`, install `:8126-8224`) — `CShadows::RenderStaticShadows`
   and `RenderStoredShadows` run **only in the main view**, never in the mirror.
   A too-opaque/wrongly-blended shadow quad leaves `2,2,2` blobs on lit surfaces
   and dark CJ sides; the mirror is shadow-free and looks correct. This is the
   strongest match for the pitch-black regions (F3/F4/F5/F8/F18 in the captures).
1. `pipelinecommon.cpp:91-93` — indoor return before `ambientFloor` tail
   `:143-168`; only `pbrAmbientFloor` (`:205-216`) survives. Interior ambient 0;
   walls/floor near black.
2. `buildingPipe.cpp:130-140` + `xboxBuildingVS.hlsl:75`,
   `buildingPBRVS.hlsl:109`, `GTAIVBuilding_vs.hlsl:92`,
   `xboxBuildingWindVS.hlsl:117` — prelight × material diffuse. PS2
   (`ps2BuildingVS.hlsl:36-38`) and Sphere (`sphereBuildingVS.hlsl:51-53`) do not.
   With ambient/sun 0, Xbox/PBR/GTAIV = `prelight*diffuse`; PS2/mirror keep full prelight.
3. `buildingPipe.cpp:2392-2407` — PBR uploads pDirect then zeroes 6 extras; Xbox
   never uploads them. Vehicle does (`vehiclePipe.cpp:560-566`, `:3476-3482`).
   Interior lamps light vehicles, not walls/furniture.
4. `main.cpp:2068` — pAmbient override only `pipe == skinPipe`; default props
   (plant/stove/furniture) keep game pAmbient (0 indoors) and any default-branch
   atom without prelight is black. CJ is floored to 0.20; props have no floor.
5. `main.cpp:2070/2106-2138` — ped override's base is **world** timecycle ambient;
   it maxes with the game's `pAmbient` (the `_Obj` ambient from
   `SetLightColoursForPedsCarsAndObjects`, game `app_light.cpp:306-321`), then
   floors to `pedAmbientFloor`. A bright object ambient survives, but a zero one
   becomes the flat floor; live `pre=(0,0,0)` proves the object ambient was zero,
   so CJ is 0.20 rather than an authored interior value.
6. `pipelinecommon.cpp:1067-1078`, `buildingPipe.cpp:2445/2461/2906`,
   `vehiclePipe.cpp:3455` — IBL capture/tint/sky reflect off indoors; the neutral
   white s3 is not a fill light. PBR/vehicle interiors lose skylight.
7. `weather.cpp:285-299/385-409` (`main.cpp:5434`) — with `altTimecycEnable=1`,
   overwrites `m_CurrentColours.ambient/directional` with outdoor set-2 after the
   game's interior extra-colour interpolation. Main interior uses outdoor values.
8. `main.cpp:5487` vs `mirrorPass.cpp:532` — mirror's direct `RenderScene` bypasses
   Weather_Update/RenderScene_before/after; mirror and main can use different
   ambient/state; no camera-raster repair or Forward+ in mirror.
9. `envmap.cpp:1045-1048/1059-1066/1086-1096` — env pass: 512² raster, far 120,
   fog 90, bright sky clear, `sphereBuildingVS`+`simpleFogPS`
   (`buildingPipe.cpp:2113-2116`). Main: 1920x1080, far 1600-3000, no indoor fog.
   Reflection copy brighter/fog-washed; main black. Likely the "mirror correct" observation.
10. `envmap.cpp:438` — alpha ref 140 outdoors only in reflection; mirror bypasses
    main state repair, so refs can differ. Cutout alpha differences, not the black.
11. `pipelinecommon.cpp:1306-1328` + `buildingPipe.cpp:2318` — mirror span
    CULLNONE; PBR hardcodes BACK; Xbox preserves saved cull (`:1822`). PBR mirror
    double-sided; not current Xbox.
12. `buildingPipe.cpp:1986-2006` + `buildingPipePS.hlsl:91-97` — Xbox env branch
    `tex1*envcolor`, `lightmult=1`, no saturate, `envData` deref at `:1991` with no
    NULL check. Pale env-mapped window/glass/wall (slab).
13. `buildingPipe.cpp:1161-1174` + `skygfx.h:1272` + `gta.cpp:17` — NULL texture →
    gpWhiteTexture; `main_simple` white×vertexColor. Untextured mesh = flat pale.
14. `buildingPipe.cpp:3268-3286/3195/3373` — non-prelit geometry gets only COLOR0,
    filled white; missing COLOR1 reads the D3D9 default (white); `setDnParams`
    forces night=1 (`:279-286`). Non-prelit mesh renders white regardless of
    ambient/sun.
15. `pipelinecommon.cpp:262-295` + `main.cpp:8076-8077` +
    `waterPipe.cpp:1216-1231` — stale ActiveViewProj/ActiveTransform; building/
    vehicle use live view/proj (`:374-388`), default/skin use the cached getter.
    Props/peds projected with the mirror env camera → stretched slab.
16. `depthhook.cpp:389-396` + §5.4 lazy loaders — dead shader after save-load
    Reset; previous VS bound → giant/exploded geometry or black. Main building
    handles recreated OK.
17. `main.cpp:3874-3907` — camera-raster repair disarmed when cache stale; log
    shows 2048x1024/512x512 during building callbacks. Viewport/raster mismatch;
    stretched geometry/clear slab.
18. `envmap.cpp:1045-1046` — reflection renders the main camera into a 2:1
    512²/2048x1024 raster; main is 16:9. Mirror/reflection aspect distortion.
19. `mirrorPass.cpp:444-458` — wall mirror (`TypeOfMirror=2` in log) blits the
    buffer full-screen before the main scene; bright/stale pBuffer → pale rectangle.
20. `texalpha.cpp:318-330` + `buildingPipe.cpp:1878-1879` — TAC_FADED forces
    blending; dark plant/stove becomes a dark ghost, not black source.
21. `postfx_colourfilter.cpp:323/355-373` — `isInterior` only excludes garage
    dampening; no interior exposure lift. Tonemap uses timecycle `sceneLuma` which
    can be 0 indoors (exposure 1.5) or bright after `Weather_Update`.
22. `rw/defaultFuncs.cpp:75-86` — fallback custom cb dereferences `envData` before
    assignment; latent NULL/black risk, not the current Switch path.

## 8. Top 3 causes + one-key/one-line tests

### Interior black

1. **Shadow pass is main-only and too opaque/dark.** Game gate
   `!CMirrors::bRenderingReflection` (`Main.cpp:3316`; our hooks
   `main.cpp:1648-1665`, install `:8126-8224`): the mirror has no shadows, the
   main draws them. A wrong shadow blend/depth or over-dark quad produces
   `2,2,2` blobs on lit surfaces and dark CJ sides; the mirror looks correct.
   **Test:** disable the shadow hooks (or force the shadow blend to a known
   SRCALPHA/INVSRCALPHA + ZWRITE off) and re-shoot F3/F18. If the `2,2,2` blobs
   lift to ambient/midtone, confirmed.
2. **Indoor `ambientFloor` bypass.** `pipelinecommon.cpp:91-93` returns before the
   floor `:143-168`; only `pbrAmbientFloor` survives (`:205-216`).
   **Test:** `pbrAmbientFloor=0.2` (one key) lifts surfaces; `ambientFloor=0.2`
   alone does nothing indoors. Confirms the bypass and the low-fill half.
3. **Xbox/PBR/GTAIV prelight × surfDiff, with indoor ambient/sun 0.**
   `buildingPipe.cpp:130-140`, `xboxBuildingVS.hlsl:75`, `buildingPBRVS.hlsl:109`,
   `GTAIVBuilding_vs.hlsl:92`; PS2/Sphere omit it (`ps2BuildingVS.hlsl:36-38`,
   `sphereBuildingVS.hlsl:51-53`).
   **Test:** same interior, `buildingPipe=PS2` (one INI key, no rebuild); if the
   room brightens, confirmed. Second: force `surfUpload[2]=1` in
   `buildingPipe_uploadSurfProps`.

The default-pipe plant/stove path (§6) is a fourth, separate cause: they get no
`pAmbient` floor (`main.cpp:2068`). Test it with the one-line `pAmbient->color`
override in the `pipe==NULL` branch.

### Pale slab

**First close the known case:** the captures show the "slab" is the **bed**
(real world mesh, wood frame, moves with camera, occluded by CJ; lit sheet
98-219 vs shadow 2). If the reported slab is the bed, this is not a geometry
bug: the pale look is the white bedspread + the ~0.23 global load fade + the
zero-ambient shadow blobs. The three technical causes below apply to a **non-bed**
slab that spans the room.

1. **Stale `_RwD3D9ActiveViewProjTransform`/`D3D9ActiveTransform`** on default/skin
   geometry after the mirror pass (`pipelinecommon.cpp:262-295`, `main.cpp:8076-8077`;
   precedent `waterPipe.cpp:1216-1231`).
   **Test (one line):** at the top of `RenderScene_before` set both to NULL; if the
   slab disappears, confirmed.
2. **NULL texture → gpWhiteTexture / Xbox additive env with NULL env map**
   (`buildingPipe.cpp:1161-1174`, `1991`, `buildingPipePS.hlsl:91-97`,
   `skygfx.h:1272`, `gta.cpp:17`).
   **Test (one line):** skip the Xbox env branch when
   `envData==NULL || envData->texture==NULL`, or bind black instead of white; if the
   slab goes black, confirmed.
3. **Dead lazy shader after the save-load Reset** (`depthhook.cpp:389-396`; loaders
   in §5.4). **Test:** load with `ivMode=0`, `mirrorDistortionFix=0`,
   `waterUnderwaterFog=0`, grass off; if the slab goes away, one lazy handle is
   stale. Add per-loader null+latch reset to the Reset path.

If all negative: declaration/FVF (§5.3), then raster/viewport (§5.5), then the
full-screen mirror blit (§5.6 item 19).

## 9. Cheap instrumentation (one line each)

- `[CurrArea]` in the building switch / `myDefaultCallback`: `currArea`, `pipe`,
  `pipe==skinPipe`, `pAmbient->color`, `buildingAmbient`.
- `[Surf]` first interior atom: `surfaceProps.{ambient,specular,diffuse}`, `flags`,
  `rpGEOMETRYPRELIT`, pipe (confirm `surfDiff<1` and declaration).
- `[BldTex]` currently caps at 24; log the first **NULL-texture mesh per atom**
  (model/atomic pointer, prelight). Also log `envData==NULL` in the Xbox env branch.
- `[Xform]` at `RenderScene_before` and before `myDefaultCallback`: Active VP,
  ActiveTransform, live View/Proj, camera raster dims.
- `[Env]` in the sphere span: main raster, envFB dims, far/fog, `spheremapfog`.
- `[Mirror]`: already logs Type/pBuffer; add pBuffer dims + camera raster dims at blit.
