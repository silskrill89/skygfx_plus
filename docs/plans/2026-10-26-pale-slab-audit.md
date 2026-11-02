# 2026-10-26 — Pale slab / black props / yellow triangle audit

**READ-ONLY.** No source edited; only this file created.

Evidence: current tree; live log `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
(04:22:36–04:28:40; line numbers cited); captures
`...\2026-10\gta_sa_Mm5FXg2vji.png` (10:11, load-save spawn) and
`gta_sa_Ew1nMAorAf.png` (10:17, same room). Config: `buildingPipe=Xbox(1)`,
`vehiclePipe=Xbox(2)`, `ivMode=0`, `forwardPlusEnable=1`,
`colorFilterEnable=0`, `mirrorDistortionFix=0`, `texAlphaClassify=1`,
`ambientFloor=0.03`, `pbrAmbientFloor=0.05`.

## 0. What the captures show — the slab is the bed

At 10:11 the camera is ~0.3–0.5 m from the bed in CJ's low-end house; the bed
fills the lower/centre frame and hides CJ's torso, with his jeans/shoes below
its far edge. At 10:17 the same surface is still in the lower third and reads
unambiguously as the bed, behind it the black wardrobe/door panels, black
potted plant, and the yellow marker triangle in the doorway. The "magnified
texels" match a 64–128 px SA interior texture plus a low-res projected shadow
at this distance (~10–20 screen px/texel); no UV transform is required. The
dark irregular shapes are the **shadow**, not a tiled albedo pattern.

Log receipts:

| fact | line | reading |
|---|---|---|
| `[BLIT] amb=(0.050,0.050,0.050)` | 674, 756, 837 | only the PBR floor |
| `dnBal=0.000 (day=1.000 night=0.000)` | same | day prelight only |
| `pDirect(rgb=0.000 lflags=0x0)` | same | no direct term |
| `[PBR-Ambient] final=(0.050) raw=(0.000,0.000,0.000)` | 659, 740 | timecycle ambient is exactly 0 |
| `[BPIPE] VS=IDR_XBOXBUILDINGVS PS=simplePS` | 755, 836 | correct Xbox pair on the sampled atomic |
| `[BldXbox] tex='lw_seat2' / door_pan1_64_128 / la_carp3` | 262–283 | CJ-house meshes |
| `[CoronaFeed] fed=0 rej(id0=64)` | 752 | corona batch empty |
| `[MirrorPass] pBuffer=1DEFAC90` + `[BUILDING] camRas=1DEFAC90 2048x1024` | 287/671/753 | the 2048×1024 raster is the **mirror** buffer |
| main frame `camRas=03F1EFC0 1920x1080` | 837 | real screen is 1920×1080 |
| `pRasFB=2048x2048 camRas=1920x1080` | 526 | front buffer ≠ camera raster |

So the slab is a correctly placed large mesh whose **colour/texture** is wrong.
The 2048×1024 lines are the mirror pass, not a foreign main camera raster. If a
future repro shows a slab that is *not* the bed, the geometry candidates below
move up; §7 instruments that distinction.

## 1. Candidate mechanisms, ranked

Two are added because the captures force them: **(g) NULL material texture →
1×1 white fallback** and **(h) env-map term with NULL/small-scale env texture**.
They rank first. Brief candidates (a)–(f) follow with their real ranks.

### 1.1 (g) NULL texture → `gpWhiteTexture` — highest

`buildingPipe_bindTexStage()` resolves NULL to `gpWhiteTexture`
(`buildingPipe.cpp:1162-1174`); `pipeSetTexture` itself maps NULL to
`gpWhiteTexture` (`skygfx.h:1272`; fallback noted at `buildingPipe.cpp:381-382`).
A bed material with no texture (streaming/load-save, TXD mismatch,
texture-less interior material) is therefore a **uniform white surface exactly
the size of the bed**, with the shadow pass painting the dark blobs. This is
the best fit for "very large pale slab, one huge blurry texel": there is no
albedo at all. `TAC_OPAQUE` is returned for NULL (`texalpha.h:29`), so it draws
opaque. `buildingPipe_texDiag` already logs `null=1`
(`buildingPipe.cpp:1183-1194`) but stops at the **first 24 meshes**
(line 1187), so the bed is outside the current window.

### 1.2 (h) Env-map term on a correctly sized mesh — second

Xbox env branch (`buildingPipe.cpp:1986-2009`):
- condition is only `surfaceProps.specular & 1` (1986); it does **not** require
  `envData->texture` to exist;
- `pipeSetTexture(envData->texture,1)` maps NULL to white (1991);
- `xboxBuildingPS` adds `tex2D(tex1,uv1)*envcolor`
  (`shaders/ps/xbox/xboxBuildingPS.hlsl:16-17`),
  `envcolor = 1*shininess*lightmult` (`xboxBuildingVS.hlsl:77`;
  `gta.h:202`);
- `uv1 = mul(envmat,N).xy * envXform.zw` (`xboxBuildingVS.hlsl:62`), and
  `envXform.zw = GetScaleX()/GetScaleY() = scaleX/8, scaleY/8`
  (`buildingPipe.cpp:1996-1997`; `gta.h:198-199`). `scaleX/scaleY` are `int8`
  and can be **0**, collapsing `uv1` to one texel of the env texture across
  the bed; small values (1–8 → 0.125–1.0) give the large blurry blotches.
This is the only code path found that magnifies a texture on a correctly sized
quad. A low-res (256/512) room-reflection env map would look pale and blotchy.

### 1.3 (a) Wrong/missing vertex declaration — third (geometry class)

`buildingPipe_setupResEntry()` binds the res-entry declaration every callback
(`buildingPipe.cpp:345-352`: `SetVertexDeclaration` 352). Xbox VS expects
`POSITION,NORMAL,TEXCOORD0,COLOR0,COLOR1`
(`shaders/vs/xboxBuildingVS.hlsl:39-46`); a wrong stride/offset makes POSITION
read another attribute → thin/stretched/exploded triangles, not a coherent
bed. Real cache hazard: after Reset only the VS/PS caches are zeroed
(`depthhook.cpp:436-437`); `0x8E2444/0x8E2440` are not
(`main.cpp:5597-5599`, `5643-5650`; repairs at `forwardplus.cpp:1832`,
water `main.cpp:5649-5650`). Probe already exists: `vs_probe`
(`main.cpp:1689-1713`).

### 1.4 (b) Stale lazy shader after Reset — latent

`Pipeline_InvalidateShaders()` (`pipelinecommon.cpp:719-723`) only drops the
batch guard; `CreateShaders` recreates the 74 batch handles via
`makePS/makeVS`, which test `*sh==NULL || s_forceShaderRecreate`
(`pipelinecommon.cpp:651-703`, batch 736-851; reset lane
`depthhook.cpp:395-396`). Everything created **outside** the batch lacks an
invalidator:

| handle(s) | creator + latch | covered? | live here? |
|---|---|---|---|
| `gtaivFPVS/gtaivFPPS` | `ivFPReady()` `buildingPipe.cpp:1057-1070`; `attempted` 1060; bind 1413-1416, `vehiclePipe.cpp:1018/1078-1079` | **NO** | no (`ivMode=0`, Xbox cb) |
| `mirrorCompositePS` | `mirrorPass.cpp:142-149`; `mirrorPSLoadTried` 98; Im2D override 267 | **NO** | no (`mirrorDistortionFix=0`, gate 288/517) |
| `s_grassInstPS` | `PC_GrassInstancer.cpp:103-126`; `s_grassInstPSTried` 72; raw create 120 | **NO** | no (`#ifdef IDR_GRASSINSTANCED` 106 is off) |
| `s_fx.*` (14) | `postfx_newfx.cpp:78-111`; `s_fxLoadTried` 85 | **NO** | no (FX gates 0) |
| `s_uwPS` | `postfx_underwater.cpp:133-144, 283-289`; `s_uwLoadTried` 140 | **NO** | no (underwater only) |
| `s_ivGradePS` | `postfx_colourfilter.cpp:1371/1384-1455`; raw create 1437 | **YES** — device check 1387-1395 + invalidator 1470-1477, wired `depthhook.cpp:425-426` | no (`ivMode=0`) |
| `grassPixelShader` | `main.cpp:2682` (`CPlantMgr_Initialise`); bound 800 | **NO** | no (`ps2ModulateGrass=0`) |

`postfx_smaa.cpp:28` `s_smaaBroken` is an init-failure latch; its handles are
in the batch. A stale VS can produce "giant/exploded geometry"
(`depthhook.cpp:389-394`), but the live `[BPIPE]` line shows the correct pair,
so not this run.

### 1.5 (c) Stale transform composition — screen-veil class

`pipeGetComposedTransformMatrix()` uses live `RwFrameGetLTM` + live
`_RwD3D9D3D9ViewTransform/Proj` (`pipelinecommon.cpp:374-388`; globals
262-267), **not** `_RwD3D9ActiveViewProjTransform`/`D3D9ActiveTransform`.
The cached helper `_rwD3D9VSGetComposedTransformMatrix`
(`pipelinecommon.cpp:280-295`) returns the cached active viewproj when
`D3D9ActiveTransform==NULL`; that pointer can still hold a reflection/mirror
camera's view*proj. `waterPipe.cpp:1207-1232` documents the exact "water veil"
failure and nulls both around the compose. Real class for a future
screen-spanning quad; not the bed.

### 1.6 (d) Decal/shadow quad borrowing a world texture — low

No custom shadow/decal geometry: `shadowContractRun` only captures/forces
state around the game's `CShadows` (`main.cpp:1153-1630`, hooks 1534-1636);
building decal sublayers reuse the same material texture via
`D3D9RenderDual` (`buildingPipe.cpp:2016`). The white fallback prevents a raw
NULL bind leaking the previous raster (`buildingPipe.cpp:1147-1173`). The dark
blobs are the legitimately projected shadow, not a borrowed-texture quad.

### 1.7 (e) Im2D sprite batch flush — low for the bed

Batch draws only at `SpriteFlushIndexedDraw_hook` (`main.cpp:3267-3360`); the
force path needs `batchCount>0` **and** (`g_inCoronasRender || g_inCloudsRender`)
(~3315-3345), ONE/ONE only for clouds (3332-3339). Corona pool is empty (752),
mirror composite off. Im2D is screen-space with no world depth; it cannot sit
behind CJ's legs. Relevant to the triangle (§5), not the bed.

### 1.8 (f) `pRasterFrontBuffer` as camera raster — low

Main frames have `pRasFB=2048×2048`, `camRas=1920×1080` (526); postfx skips
when `camRas != screen` (`postfx_colourfilter.cpp:1528-1532`) and documents the
2048-UV hazard at 1554-1565. The building cb has its own `camRasIsScreen` gate
(`buildingPipe.cpp:2515-2534`) and probe (3074-3082). A front-buffer mix-up
would rescale the **whole frame**, not one object.

## 2. Which mechanism looks "hugely magnified"?

| mechanism | expected signature | verdict |
|---|---|---|
| (a) bad declaration | garbage positions, thin/stretched tris, no clean silhouette | no |
| (b) stale VS | wrong space/transform; huge quad possible, UVs unchanged | class, not this run (correct pair logged) |
| (b) stale PS | previous shader's colour/alpha on this mesh | class, not this run |
| (c) stale viewproj | coherent huge quad/veil; whole pass displaced, texture scale intact | future risk, not the bed |
| (d) shadow/decal texture | small author-sized shadow quad | no |
| (e) Im2D flush | screen-aligned, no depth | no |
| (f) front buffer | whole frame rescaled | no |
| **(g) NULL → white** | coherent mesh silhouette, flat white; detail is only shadow | **best for featureless bed** |
| **(h) env term** | correct mesh, pale low-res wash; `uv1` collapse → huge blotches | **best for "magnified texel"** |

A transform bug changes *where/how big*; a UV bug changes *sampling*; a
texture-bind bug changes *what* (or whether) is sampled. The captures show a
correctly placed bed whose texture is gone/washed out, so (g)/(h) beat geometry
and transform. The only UV-scale path in the building pipe is the env lane,
not `c32-c35`.

## 3. UV/texture-transform audit

**c32-c35 (diffuse, `REG_texmat=32`, `buildingPipe.cpp:94`).** Single writer:
`buildingPipe_setUVTransform()` (`buildingPipe.cpp:356-368`); MatFX
`rpMATFXEFFECTUVTRANSFORM` uploads authored `m1` (361-363), else the caller's
identity (365/367). Call sites inside the per-mesh loops: PS2 1357, Xbox 1847,
Sphere 2133, PBR 2750; identities created 1227/1701/2058/2333. No path scales
by a large factor, and none leaves a previous mesh's matrix (identity is
rewritten before every non-MatFX draw; TagRenderCB `continue` is after the
write — Xbox 2011, PBR 2811). A large MatFX `m1` would tile *more*, not
magnify.

**c38-c40 (env matrix, `REG_envmat=38`, line 98) + c37 (env scale).** Writers
1278 (PS2), 1801 (Xbox), 2412 (PBR). `CustomBuildingEnvMapPipeline__SetupEnv`
caches `s_setupEnv_lastmat` keyed by object/frame (191-225); cache cleared by
`ReleaseBuildingPipeCaches` (3483-3489, called `postfx.cpp:2643`).
`envXform.zw = GetScaleX()/GetScaleY()` (1994-1998; PS2 1483-1487);
`GetScaleX = scaleX/8` (`gta.h:198-199`). `int8` zero/tiny values collapse
`uv1` (`xboxBuildingVS.hlsl:62`; `ps2BuildingFxVS.hlsl:28-29`;
`GTAIVBuilding_vs.hlsl:83-84`) → the only live UV-magnification path.

**c16 is not a UV matrix here.** It is `matCol` in
`neoVehiclePass1VS/2VS:23` and is not written by any building UV path. Watch
**c32-c35 / c37 / c38-c40**, not c16.

**`pipeGetComposedTransformMatrix`** writes only WVP to the caller's `out`
(`pipelinecommon.cpp:374-388`); callers upload c0 (Xbox 1772-1773, PS2
1257/1271, PBR 2343, Sphere 2080). It never touches UVs, so a stale UV matrix
via this helper is impossible.

## 4. Black plant/stove — indoor light term

Xbox VS lighting (`xboxBuildingVS.hlsl:64-75`):

```
OUT.Color = (prelight*surfDiff + ambient*surfAmb + directCol*ndl) * matCol
```

`surfDiff` comes from `surfaceProps.diffuse`; when 0 (SA baked convention)
`buildingPipe_uploadSurfProps()` substitutes 1.0 (`buildingPipe.cpp:130-140`).
`TAC_` only chooses blend/alpha (`buildingPipe.cpp:2614-2665`), not the light
term. `c4` (`REG_ambient=74`) = `buildingAmbient` (=`GetTimecycleAmbientPBR()`,
`buildingPipe.cpp:159-187`; upload 1965-1968). `c5/c6` direct is from
`pDirect` (Xbox 1797-1798) = 0 in the captures. At 10:11 `day=1 night=0`, so
only DayColor prelight contributes.

Result for a non-env prop:
`0.05 * surfAmb + DayColor * texture`. A dark DayColor/texture → black
silhouette; a bright one (bed) → pale. This is a lighting/prelight split, not
alpha class or shadow.

Ambient floors:
- Raw timecycle ambient is **0.000**; c4 is exactly **0.05**, applied additively
  by `pbrAmbientFloor` in `GetTimecycleAmbientPBR()`
  (`pipelinecommon.cpp:196-223`).
- `ClampAmbientToSky()` returns **before** the absolute floor for interiors:
  `if(*CGame__currArea != 0) return;` (`pipelinecommon.cpp:91-93`), while the
  `ambientFloor` lift is at 143-168. So `SkyGfx.ambientFloor` (0.03) **never
  reaches interiors**; only `pbrAmbientFloor` does. With the shipped default
  0.0 (`main.cpp:6571/7032`), indoor c4 would be exactly 0 → pure black props.
- The 0.05 floor *does* reach c4 (shared by buildings/peds/vehicles); the gap is
  specifically the `ambientFloor` tail, not the PBR lift.

Second-order: `setDnParams`'s no-extra-colours path forces
`dayparam=0, nightparam=1` for `RSPIPE_PC_CustomBuilding_PipeID`
(`buildingPipe.cpp:279-286`). If an interior atomic carries only day prelight,
its prelight is zeroed. The logged `day=1 night=0` is the global balance, not
this per-atomic rewrite; probe it (§7).

## 5. Yellow triangle

The marker path `C3dMarkers::Render 0x60BA80` (only caller `0x53E20E`) sets
`ZWRITE=0, ZTEST=1, SRC=SRCALPHA, DST=INVSRCALPHA, VERTEXALPHA=1,
texture=NULL` and draws its own geometry (`corona-blackquad-diagnosis.md:89-92`).
That is a solid, untextured vertex-coloured yellow triangle — exactly the
capture. We have **no hook** at 0x60BA80 (`grep 60BA80 src/` → none).

- Corona shield: `coronaDrawRawShield` only forces blend factors
  (`main.cpp:2952-3022`); it never binds a texture, and ONE/ONE is only for
  `g_inCloudsRender` (2881, 3332-3339). The marker is not a batch producer, so
  it cannot be shield-forced to ONE/ONE with an untextured raster.
- The batch is empty anyway (log 752), and the mirror composite is off.
- Default-pipeline path: the contract (`main.cpp:1787-1835`) applies to marker
  atomics, then the marker sets its own states; `defaultCbShaderHygiene=1`
  unbinds VS/PS (2030-2061), so it draws fixed-function with its vertex colour.
- Untextured Im2D triangle / degenerate shadow volume: low; the stale-Im2D
  override sweep (4030-4034) and the shadow ring geometry (`primType=2`,
  4 verts/5 indices) do not match.

Conclusion: vanilla marker, rendered flat because it is an untextured cone.
Not a shield bug; the only lever is cull/marker state at 0x60BA80 (currently
unhooked).

## 6. Interior-only triggers

| file:line | branch | switches |
|---|---|---|
| `pipelinecommon.cpp:91-93` | `currArea != 0` early return | skips night clamp **and** `ambientFloor` tail (143-168) |
| `pipelinecommon.cpp:1067-1077` | `currArea != 0` | suppresses IBL capture |
| `buildingPipe.cpp:2445` | `currArea == 0` | PBR IBL weight 0 indoors |
| `buildingPipe.cpp:2461-2472` | `currArea != 0` | PBR s3 → `bldIndoorNeutralTex` (2238-2251) |
| `buildingPipe.cpp:2903-2906` | `currArea == 0` | PBR `bldEnv.z` disabled indoors |
| `buildingPipe.cpp:279-286` | PC interior pipeline data | forces night prelight |
| `envmap.cpp:438-439` | `currArea == 0` | outdoor alpha ref 140 |
| `vehiclePipe.cpp:543-547, 1516-1520, 3455` | `currArea`/`PlayerNoRain` | interior/garage ambient dampening, IBL 0 |
| `neoCarpipe.cpp:555-558` | `currArea`+`PlayerNoRain` | interior/sheltered lighting |
| `neoWaterdrops.cpp:139-142, 421` | `CCullZones__*`+`currArea` | rain-drop suppression |
| `postfx_colourfilter.cpp:323, 355-365` | `isInterior`/`isSheltered` | tonemap branch |
| `postfx_colourfilter.cpp:442-465` | interior | exposure floor/damping |
| `postfx_underwater.cpp:97-101, 363` | interior plane | underwater false positive |
| `waterPipe.cpp:236, 340, 418` | `IsInInterior` | water body classification |

None selects a different VS/PS, declaration, UV matrix, or transform for the
bed. The only interior branch that explains black props is the ambient
early-return; the PBR IBL gates are inactive on the Xbox pipe. "Indoor-only" is
a lighting fact here, not a geometry bug.

## 7. Ranked shortlist (max 5): instrument line + one-line fix

1. **NULL texture → white bed.** Instrument `buildingPipe.cpp:1183-1194`
   (remove the 24-mesh cap / log only `null=1` with model name) and
   `buildingPipe.cpp:1164`. *Fix:* bind a neutral mid-grey (or skip until the
   texture streams) for interior atomics with NULL material texture.
2. **Env white/low-res wash + UV collapse.** Instrument
   `buildingPipe.cpp:1986` (log `specular&7`, env texture name, `scaleX/Y`,
   `shininess`) and 1996 (`envXform.zw`). *Fix:* take the env branch only when
   `envData && envData->texture && (abs(scaleX)+abs(scaleY) > 0)`, else
   `simplePS` and zero c37/c36.
3. **Indoor ambient floor gap.** Instrument `pipelinecommon.cpp:92` (log
   `currArea`, raw ambient, whether the ambientFloor tail ran) and
   `buildingPipe.cpp:1965-1968`. *Fix:* apply the absolute floor in the
   interior early-return branch too (or add `interiorAmbientFloor`, default
   0.10).
4. **Per-atomic prelight zeroed.** Instrument `buildingPipe.cpp:279-289` (log
   `pluginData`, day/night params, model name). *Fix:* for `currArea != 0` use
   `max(day,night)` per channel instead of forcing one side to 0.
5. **Latent stale viewproj.** Instrument `pipelinecommon.cpp:283-285` (log the
   cached pointer + camera raster at the next compose; compare with the mirror
   pointer). *Fix:* null `_RwD3D9ActiveViewProjTransform` and
   `D3D9ActiveTransform` at the building/marker Switch entry after a
   mirror/env span, as `waterPipe.cpp:1227-1230` already does locally.

Keep as latent (not top 5): `ivFPReady`'s `attempted` (`buildingPipe.cpp:1060`)
and the other non-batch loaders in §1.4; add invalidators next to
`Pipeline_InvalidateShaders` (`depthhook.cpp:395-396`) if chasing a post-Reset
repro.

## 8. One-run discriminator

1. Remove the 24-mesh cap and log `null=1` with the model name. If the bed logs
   `null=1`, (g) is confirmed.
2. If textured, log the env branch (§7.2). `specular&1` set with NULL env
   texture or `scaleX/Y=0` confirms (h); `specular&1` clear means the bed is
   simply the white prelit sheet under the 0.05 ambient and the room lighting
   (§7.3/4) is the defect.
3. Log per-atomic day/night params for the stove/plant (§7.4); a zeroed side is
   the black-prop fix.
4. Only for a future slab **not** shaped like the bed: run the `vs_probe`
   declaration check (§1.3) and the active-viewproj probe (§1.5).
