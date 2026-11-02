# GTA SA PC v1.0 US — player shadow / dark-ring reference

**Date:** 2026-10-26 · read-only reference from local sources + live `gta_sa.exe`.
**Question:** what draws the radial dark region / ring centred on the player, and what is the standard render-state contract?

## Source map and method

`gtasa_src-main/` is a partial dump (43 files): **no `Shadows.cpp`**, only call sites and `PointLights.cpp`. The full `CShadows` implementation was read from `OpenSA/src_ps2_originals/Renderer/Shadows.cpp` / `.h`, cross-checked against `GTA SA RE/re3-master/src/renderer/Shadows.cpp`, `OpenSA/src/game_sa/ShadowCamera.cpp`, `RealTimeShadow.cpp`, `RealTimeShadowManager.cpp`, and `gtasa_src-main/Main.cpp:1949-1951,3343-3350`, `Ped.cpp:8963,8974`, `Automobile.cpp:7669-7678`, `PointLights.cpp:88-137,353-620`.

The `re3-master` SA source uses `fDrawDistance = 26.0f` and culls at 13 m (`:893-895`); that is **not** the PC v1.0 US path. The live exe at `0x707BF3` loads `0x8D5240` (15.0) and multiplies by 0.5 for the fade start. Use **15.0 m** for PC v1.0 US.

## TL;DR

- The radial dark region is the **ped blob shadow**: `shad_ped` from `models/particle.txd`, stored by `CShadows::StoreShadowForPedObject`, drawn by `CShadows::RenderStoredShadows` on collision geometry. It is a **filled radial disc**, not a ring.
- The circular boundary is the **15.0 m ped-shadow storage cull** (`MAX_DISTANCE_PED_SHADOWS`, VA `0x8D5240`; squared compare 225.0). Vanilla also **fades** alpha from full at 7.5 m to 0 at 15.0 m (`CalculateShadowStrength`), so the vanilla edge is soft. A **hard** ring means the fade is missing/bypassed, or the mod uses 15.0 as a hard mask.
- The only literal **ring primitive** in the shadow code is `CShadowCamera::DrawOutlineBorder` (`0x705790`): 4 verts / 5 indices, `rwPRIMTYPEPOLYLINE`, ZTEST off, vertex alpha off, texture NULL, black (blend inherited). It is drawn into the **shadow camera's offscreen raster**, not the scene; it is a 1 px texture border for the real-time shadow.
- Vanilla **does not restore** most shadow state: stored pass restores only ZWRITE/ZTEST/TEXTUREADDRESS/CULLMODE; static pass only ZWRITE/VERTEXALPHA; none restores stage-0 texture, fog, alpha ref/filter, or blend factors.

## 1. Data model

`CShadows` (`OpenSA Shadows.h:168-180`): `asShadowsStored[48]` per-frame dynamic shadows; `aStaticShadows[48]` persistent map shadows; `aPermanentShadows[48]` oil/blood; `aPolyBunches[360]`; `ShadowsStoredToBeRendered` (u16, reset to 0 after `RenderStoredShadows`).

`eShadowType`: `NONE=0`, `DEFAULT/DARK=1`, `ADDITIVE=2`, `INVCOLOR=3`, `OIL_1..5=4..8` (`Shadows.h:38-48`). `CRegisteredShadow` carries `m_vecPosn`, `m_Front`, `m_Side`, `m_fZDistance`, `m_fScale`, `m_pTexture`, `m_nIntensity`, `m_nType`, RGB, `drawOnWater`, `drawOnBuildings` (`:69-88`).

## 2. The player path

### 2.1 Ped blob shadow — `StoreShadowForPedObject` (`0x707B40`)

`OpenSA Shadows.cpp:360-405`:

```cpp
const auto bonePos = ped->GetBonePosition(BONE_ROOT);
const auto& camPos = TheCamera.GetPosition();
const auto boneToCamDist2DSq = (bonePos - camPos).SquaredMagnitude2D();

if (boneToCamDist2DSq >= MAX_DISTANCE_PED_SHADOWS_SQR)   // 225.0
    return;

const auto strength = (uint8)CalculateShadowStrength(
    sqrt(boneToCamDist2DSq), MAX_DISTANCE_PED_SHADOWS,
    CTimeCycle::m_CurrentColours.m_nShadowStrength);

StoreShadowToBeRendered(
    SHADOW_DEFAULT, gpShadowPedTex, bonePos + CVector{displacementX, displacementY, 0},
    frontX, frontY, sideX, sideY,
    strength, strength, strength, strength,
    4.0f, false, 1.0f, nullptr, isPlayerPed);
```

- Texture `gpShadowPedTex` = `RwTextureRead("shad_ped")`; type `SHADOW_DEFAULT`/DARK; black RGB; alpha = strength; `zDistance=4.0`; player shadow may draw on buildings.
- Distance is 2D (XY) camera-to-`BONE_ROOT`. `Ped.cpp:8963/8974` calls this from `CPed::PreRender`; `FindPlayerPed()` bypasses the visibility check.

### 2.2 Vehicle blob shadow — `StoreShadowForVehicle` (`0x70BDA0`)

`OpenSA Shadows.cpp:994+` / re3 `:489-691`.

| Vehicle class | Draw distance | Fade starts |
|---|---:|---:|
| normal car / bike / boat / RC | 18.0 m | 13.5 m (75%) |
| heli / seaplane / RC plane | 144.0 m | 108 m |
| big plane (OpenSA comment) | 288.0 m | 216 m |

- Type `SHADOW_DEFAULT`; textures `gpShadowCarTex` / `gpShadowBikeTex` / `gpShadowHeliTex` / `gpShadowBaronTex`; `zDistance=4.5`; black RGBA = `nColorStrength`.
- `bDrawOnBuildings` true for RC vehicles and `FindPlayerVehicle()`. If speed*timestep `< 0.1` and not player/RC, it becomes `StoreStaticShadow` with `drawDistance=0`, `upDistance=0.1` — no per-frame cull.

### 2.3 Real-time shadow — `CRealTimeShadowManager::Update` (`0x706AB0`)

PC/Xbox "extended" shadow at high FX quality (this mod's `pedShadows=1` forces FX quality 3, `main.cpp:2560-2561`).

- 16 slots (`RealTimeShadowManager.h:9`); after 16, fallback to blob shadows.
- Each owner has a `CRealTimeShadow` camera. `Update` (`0x706600`) renders the silhouette, `InvertRaster`s it, blurs it; `CShadows::StoreRealTimeShadow` (`0x707CA0`) stores it.
- Type `SHADOW_INVCOLOR` (`ZERO/INVSRCCOLOR`); texture = `GetShadowRwTexture()` (per-object camera raster); `zDistance=4.0`; `scale=1.0`.
- Cull `shdwToCamDist2DSq > 225.0`; strength scaled by `m_nIntensity/100` and the same 7.5/15 m fade (`Shadows.cpp:408-459`).
- Before storing, calls `shadow->DrawBorderAroundTexture({0,0,0,0})` (re3 `:820`; `RealTimeShadow.cpp:50-52`) → `CShadowCamera::DrawOutlineBorder` (§7). Texture-space border, not a world ring.

### 2.4 Extra player shadows — `RenderExtraPlayerShadows` (`0x707FA0`)

`CRenderer::PreRender` (`OpenSA Renderer.cpp:294`) calls this before the world; it sets **no** render state and draws nothing. It stores extra vehicle shadows: returns if shadow strength 0, no player vehicle, RC Bandit, bike, FPLANE or boat; iterates active point lights, skips non-`PLTYPE_POINTLIGHT`, `!m_bGenerateShadows`, black or outside `m_fRadius`; stores `SHADOW_DEFAULT`, `gpShadowCarTex`, black, `5*strength/8`, `zDistance=4.5`, `drawOnBuildings=false`, with `CalculateShadowStrength(lightDist, lightRadius, 5*strength/8)`. It is point-light-driven vehicle blob shadows, not a light pool.

## 3. The 15 m circle

`MAX_DISTANCE_PED_SHADOWS = 15.0f` (`Shadows.h:246`, VA `0x8D5240`); `MAX_DISTANCE_PED_SHADOWS_SQR = 225.0f` (`:247`).

| Function | Test | At exactly 15 m |
|---|---|---|
| `StoreShadowForPedObject` `0x707B40` | `boneToCamDist2DSq >= 225.0` -> return | skipped |
| `StoreRealTimeShadow` `0x707CA0` | `shdwToCamDist2DSq > 225.0` -> return | allowed, strength 0 |
| `CalculateShadowStrength` | `dist >= 7.5` -> `(1-(dist-7.5)/7.5)*max`; else max | 0 |
| `StoreShadowForVehicle` `0x70BDA0` | `camToVehDist2DSq < 18²`; fade at 13.5 m | 0 |
| `StoreStaticShadow` (creation only) | `SQR(fDrawDistance) > distSq || fDrawDistance == 0` | per-call `drawDistance` |
| `CPointLights::AddLight` | 3D dist `< 15+range`; fade from `0.75*(15+range)` | per-light |

**Stored vs static:** `RenderStoredShadows` (`0x70A960`) has **no** distance check; it draws every entry that survived storage. The 15 m limit is applied **when storing**. `RenderStaticShadows` (`0x708300`) has **no** per-frame distance check; static shadows are culled only when `StoreStaticShadow` creates them (`Shadows.cpp:238-258`). A static shadow with `fDrawDistance == 0` is never distance-culled.

**Consequence:** the only camera-centred circular boundary in the ped shadow system is the 15.0 m storage/fade radius. Vanilla fades, so it is a soft gradient. A hard circular edge means the fade was bypassed (mod radial mask, or re-render with constant alpha).

## 4. Textures

`CShadows::Init` (`0x706CD0`) pushes the **"particle"** TXD slot and loads (`OpenSA Shadows.cpp:77-92`):

```cpp
gpShadowCarTex         = RwTextureRead("shad_car",     nullptr);
gpShadowPedTex         = RwTextureRead("shad_ped",     nullptr);
gpShadowHeliTex        = RwTextureRead("shad_heli",    nullptr);
gpShadowBikeTex        = RwTextureRead("shad_bike",    nullptr);
gpShadowBaronTex       = RwTextureRead("shad_rcbaron", nullptr);
gpShadowExplosionTex   = RwTextureRead("shad_exp",     nullptr);
gpShadowHeadLightsTex  = RwTextureRead("headlight",    nullptr);
gpBloodPoolTex         = RwTextureRead("bloodpool_64", nullptr);
gpPostShadowTex        = RwTextureRead("lamp_shad_64", nullptr);
```

There is **no `shadowround`** in SA; `shad_*` live in `models/particle.txd`, not `models/` root.

Vanilla PC metadata (parsed from the TXD, mip 0):

| Texture | Size | Format | Alpha / RGB |
|---|---:|---|---|
| `shad_ped` | 32×32 | 32-bit, 1 mip | RGB=0; alpha radial 0..195, centre 151 |
| `shad_car` | 64×64 | 32-bit, 1 mip | alpha rounded rect 0..211 |
| `shad_bike`, `shad_heli` | 64×64 | 32-bit, 1 mip | alpha blob |
| `shad_rcbaron` | 32×32 | 32-bit, 1 mip | alpha blob |
| `shad_exp` | 32×32 | 32-bit, 1 mip | RGB radial 0..142; alpha 255 (additive) |
| `headlight` | 128×128 | 32-bit | headlight cone |
| `bloodpool_64`, `lamp_shad_64` | 64×64 | 32-bit | blood / lamp shadow |

`shad_ped` is a **filled radial disc**: alpha highest at centre, 0 at border. Not a ring. The installed `E:/games/gtasa_skygfx_plus/models/particle.txd` has a 128×128 replacement (alpha max 165, centre 108) — still a filled radial blob, just a lighter HD upsample.

**If unbound:** `RwTextureRead` -> NULL; `StoreShadowToBeRendered` stores NULL; `RenderStoredShadows` binds `RwTextureGetRaster(NULL)` = no stage-0 texture. Fixed-function shades with **vertex colour/alpha only**: black RGB + vertex alpha = a hard-edged **black polygon** clipped to the ground. It does **not** use the last-bound raster. A NULL/stale raster can leak to later consumers because shadow passes never restore stage 0. The literal outline path binds NULL and disables blend, so it draws an opaque **black line loop**.

## 5. Render-state contract (exact)

Verified against `gta_sa.exe`; source cross-check `OpenSA Shadows.cpp`.

### 5.1 `CShadows::RenderStaticShadows` — `0x708300`

| State | Value | Restored? |
|---|---|---|
| `ZWRITEENABLE` | FALSE | yes -> TRUE |
| `ZTESTENABLE` | TRUE | **no** |
| `VERTEXALPHAENABLE` | TRUE | yes -> FALSE |
| `FOGENABLE` | FALSE | **no** |
| `ALPHATESTFUNCTIONREF` | 0 | **no** |
| `TEXTUREFILTER` | LINEAR | **no** |
| `TEXTURERASTER` | per-shadow | **no** |
| blend factors | per type | **no** |
| `CULLMODE` | not set (inherited) | **no** |

End (`0x708684-0x708693`): `VERTEXALPHA=0`, `ZWRITE=1` only.

### 5.2 `CShadows::RenderStoredShadows` — `0x70A960`

| State | Value | Restored? |
|---|---|---|
| `ZWRITEENABLE` | FALSE | yes -> TRUE |
| `ZTESTENABLE` | TRUE | yes -> TRUE |
| `VERTEXALPHAENABLE` | TRUE | yes -> FALSE |
| `FOGENABLE` | FALSE | **no** |
| `TEXTUREADDRESS` | CLAMP | yes -> WRAP |
| `CULLMODE` | NONE | yes -> BACK (hardcoded, not pre-entry) |
| `ALPHATESTFUNCTIONREF` | 0 | **no** |
| `TEXTUREFILTER` | LINEAR | **no** |
| `TEXTURERASTER` | per-slot | **no** |
| blend factors | per type | **no** |
| alpha test enable/function | inherited | **no** |

End (`0x70B6D8-0x70B713`): `VERTEXALPHA=0`, `ZWRITE=1`, `ZTEST=1`, `TEXTUREADDRESS=WRAP`, `CULLMODE=BACK`; then `ShadowsStoredToBeRendered=0` (`0x70B71A`).

### 5.3 `SetRenderModeForShadowType` — `0x707460`

| Type | SRCBLEND | DESTBLEND |
|---|---|---|
| `SHADOW_DEFAULT` / `OIL_1/2/3/5` | `SRCALPHA` | `INVSRCALPHA` |
| `SHADOW_ADDITIVE` | `ONE` | `ONE` |
| `SHADOW_INVCOLOR` | `ZERO` | `INVSRCCOLOR` |
| `SHADOW_OIL_4` | unhandled (leaves previous) | unhandled |

It does **not** touch alpha test, Z, cull, fog, or texture; it does not enable blending itself. The passes set `VERTEXALPHAENABLE=TRUE`, the RW side of blend enable.

### 5.4 Other shadow-related passes

| Pass | States set | Restore |
|---|---|---|
| `RenderExtraPlayerShadows` `0x707FA0` | **none** (only stores) | n/a |
| `CPointLights::RenderFogEffect` `0x7002D0` | FOG=0, ZWRITE=0, ZTEST=1, VALPHA=1, SRC=ONE, DST=ONE, texture=`gpCloudTex[0]` | **none** (restore commented out, `gtasa_src-main/PointLights.cpp:614-618`) |
| `CCoronas::Render` `0x6FAEC0` | ZWRITE=0, VALPHA=1, SRC=ONE, DST=ONE, ZTEST=1 (per-corona toggle), per-corona texture | **none** (`OpenSA Coronas.cpp:359`) |
| `CShadowCamera::DrawOutlineBorder` `0x705790` | ZTEST=0, VALPHA=0, TEXTURE=NULL; blend inherited | ZTEST=1, VALPHA=0; no texture/blend restore |
| `CShadowCamera::InvertRaster` `0x705660` | TEXTURE=NULL, VALPHA=1, SRC=INVDESTCOLOR, DST=ZERO | ZTEST=1, SRC=SRCALPHA, DST=INVSRCALPHA; no texture/blend-enable restore |

Vanilla's contract is incomplete by design: it relies on the preceding world pass for blend enable, alpha-test function, cull mode, and stage-0 state. A mod hooking the shadow pass must save/restore those itself.

## 6. Render order and depth interaction

Actual order (`gtasa_src-main/Main.cpp:1949-1951`, `:3343-3350`):

```
CRenderer::RenderEverythingBarRoads()   // world, peds, vehicles -> depth written
CWaterLevel::RenderWater()
CShadows::UpdateStaticShadows()         // 0x707F40: cleanup only
CShadows::RenderStaticShadows()         // 0x708300
CShadows::RenderStoredShadows()         // 0x70A960
```

`RenderExtraPlayerShadows` is earlier, in `CRenderer::PreRender` (`OpenSA Renderer.cpp:294`), but only stores. When shadow quads are drawn, opaque world/peds/vehicles have already written depth.

- Shadows are `RwIm3D` world-space triangles, **not** depth-sorted; batching is by `(type, texture)`, draw order = array order.
- `ZTEST=TRUE` + `ZWRITE=FALSE`: a shadow fragment draws only if nearer than stored depth. It **cannot** occlude opaque geometry drawn after it, because it does not write depth.
- The shadow sits at ground `z + 0.03..0.06` (plus the near-clip bias in `Main.cpp`). That offset pushes it toward the camera so it passes the ground z-test; it can also pass against the ped's feet/lower body, which is why a blob can darken the shoes.
- A shadow "covers" the whole ped only if `ZTEST` is off/ALWAYS, the shadow's depth is nearer than the ped, or it is drawn after the ped with a large positive depth bias. With `ZWRITE=0` + `ZTEST=TRUE`, the normal result is the ped occludes the shadow.

`RwIm2D` (coronas, flares, HUD) is screen-space and **not** depth-sorted: submission order with a screen-space z. With ZTEST on it is occluded by opaque depth; with ZTEST off (some flares/reflections) it always paints over. The shadow pass uses `RwIm3D`; only the shadow-camera border/invert use `RwIm2D`.

## 7. The literal ring primitive

`CShadowCamera::DrawOutlineBorder` (`0x705790`, `OpenSA ShadowCamera.cpp:113-156`):

```cpp
RwIm2DVertex vertices[] = { MkVert(0,0), MkVert(w,0), MkVert(w,w), MkVert(0,w) };
RwImVertexIndex indices[] = {0,1,2,3,0};
RwCameraBeginUpdate(m_pRwCamera);
RwRenderStateSet(ZTESTENABLE, FALSE);
RwRenderStateSet(VERTEXALPHAENABLE, FALSE);
RwRenderStateSet(TEXTURERASTER, NULL);
RwIm2DRenderIndexedPrimitive(rwPRIMTYPEPOLYLINE, vertices, 4, indices, 5);
RwCameraEndUpdate(m_pRwCamera);
```

A closed 4-vertex line loop = rectangular outline, not a filled disc. It is drawn into the **shadow camera's raster**, a texture used by `CRealTimeShadow`; offscreen by design. `CRealTimeShadow::StoreRealTimeShadow` calls it via `DrawBorderAroundTexture({0,0,0,0})` to stop blur bleeding at the texture edge. If a mod/hook targets this at the **scene** camera (or breaks camera/raster selection), it becomes a black outline around the shadow camera centre — for CJ's real-time shadow, a literal ring around CJ.

This is the same 4-vertex/5-index polyline that `2026-10-26-shadow-subsystem-audit.md` identified at `0x7058BC`. Correct path: `CRealTimeShadowManager::Update (0x706AB0) → CRealTimeShadow::Update (0x706600) → StoreRealTimeShadow (0x707CA0) → DrawOutlineBorder (0x705790)`, **not** a static map-shadow renderer. `0x707F40` is `UpdateStaticShadows` (cleanup), `0x708300` is `RenderStaticShadows`, `0x70A960` is `RenderStoredShadows`, `0x707FA0` is `RenderExtraPlayerShadows`.

## 8. What would produce a dark ring in our mod

1. **Hard 15 m mask without fade.** A radial decal using `1 - saturate(dist/15)` or `step(15, dist)` instead of `CalculateShadowStrength` makes the 15 m cull a visible circular edge.
2. **Wrong blend type.** `SHADOW_INVCOLOR` (`ZERO/INVSRCCOLOR`) on a blob instead of `SHADOW_DEFAULT` (`SRCALPHA/INVSRCALPHA`) turns the alpha gradient into colour inversion; the outer gradient edge can read as a ring. `SHADOW_ADDITIVE` on black = invisible; on `shad_exp` = bright.
3. **ZTEST off / ZFUNC=ALWAYS.** The shadow paints over CJ and the world in submission order; with a radial texture whose centre is occluded/alpha-tested, the visible part is a ring.
4. **Wrong/absent texture.** NULL stage-0 makes a stored shadow a hard black polygon; the outline path with NULL + blend off makes a literal black line loop. A ring-shaped/inverted-alpha replacement (or a normal/spec map bound by mistake) also shows as a ring.
5. **Cull/address mode.** `CULLBACK` on a facing-away winding, or `TEXTUREADDRESS=WRAP` instead of `CLAMP`, drops/duplicates parts of the projected quad and produces arcs/rings. The stored pass sets `CULLNONE` + `CLAMP` for a reason and restores `CULLBACK` hardcoded, not the entry mode.
6. **Double render / no count reset.** If `ShadowsStoredToBeRendered` is not reset after `RenderStoredShadows` (or the pass runs twice), the same projected polygons accumulate alpha; overlapping batches show as banded rings.
7. **Real-time shadow border leak.** With `pedShadows=1` forcing FX quality 3, the `CRealTimeShadow` path runs; verify `DrawOutlineBorder` writes to the shadow camera raster, not the scene camera. This is the only code path that draws a literal ring primitive.

## 9. References

- `OpenSA/src_ps2_originals/Renderer/Shadows.cpp`: Init 77-108; StoreShadowToBeRendered 147-201; SetRenderMode 221-243; StoreShadowForPedObject 360-405; StoreRealTimeShadow 408-460; RenderExtraPlayerShadows 482-554; RenderStaticShadows 557-646; RenderStoredShadows 757-980.
- `OpenSA/src_ps2_originals/Renderer/Shadows.h`: arrays 168-180; constants 246-247; texture globals 249-260.
- `GTA SA RE/re3-master/src/renderer/Shadows.cpp`: vehicle 489-691; ped 849-921; RenderStoredShadows 1064-1243; RenderStaticShadows 1247-1319; StoreStaticShadow 238-258.
- `OpenSA/src/game_sa/ShadowCamera.cpp`: InvertRaster 65-100; DrawOutlineBorder 113-156; Update_Internal 225-253.
- `OpenSA/src/game_sa/RealTimeShadow.cpp`: DrawBorderAroundTexture 50-52; Update 81-129.
- `OpenSA/src/game_sa/RealTimeShadowManager.cpp`: Update 59-107; DoShadowThisFrame 130-151.
- `OpenSA/src/game_sa/Coronas.cpp`: Render 178-360.
- `gtasa_src-main/PointLights.cpp`: AddLight 88-137; RenderFogEffect 353-620.
- `gtasa_src-main/Main.cpp`: 1949-1951, 3343-3350. `Ped.cpp`: 8963, 8974. `Automobile.cpp`: 7669-7678.
- Live `gta_sa.exe`: `0x8D5240 = 15.0f`; ped fade at `0x707BF3` (`fld 15.0; fmul 0.5`); real-time fade at `0x707D7C`.
