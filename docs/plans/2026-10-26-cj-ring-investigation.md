# CJ Ring Investigation — player-centred dark ring / disc

**Date:** 2026-10-26
**Build:** 2,395,136 (`skygfx.asi` mtime 2026-10-26 01:11:54, banner `skygfx build corona-flush-shield`)
**Mode:** Xbox (`[Mode] engage Xbox owned=79 pipes=2/1/2 cf=2`), pipeline re-derived 2 → 1, `buildingPipe=0 vehiclePipe=0`
**Post-processing:** all off (`ssao/smaa/sss/pipeChain/radiosity/heightFog/godRays = 0`)
**Shadow config:** `fixShadows=0`, `pedShadows=-1`, `stencilShadows=-1` (both -1 = use the engine's FX-quality value)
**Scope:** READ-ONLY. No source files edited. One new file (this document).

---

## 0. TL;DR

The "ring around CJ" is almost certainly **not** the function this build calls `RenderExtraPlayerShadows`. The build's shadow labels are rotated by one entry against the real exe:

| This build's label | Installed at | Actual exe function |
|---|---|---|
| `CShadows::RenderStaticShadows` (0x707F40) | call site 0x53E0BE | **`CShadows::UpdateStaticShadows`** |
| `CShadows::RenderStoredShadows` (0x708300) | function entry 0x708300 | **`CShadows::RenderStaticShadows`** |
| `CShadows::RenderExtraPlayerShadows` (0x70A960) | call site 0x53E0C8 | **`CShadows::RenderStoredShadows`** |
| *(not installed)* | — | **`CShadows::RenderExtraPlayerShadows` = 0x707FA0** |

The only live, player-anchored, radial-ish draw in this build is therefore:

**`CShadows::RenderStoredShadows` (exe 0x70A960)** — the pass this build wraps as `CShadows__RenderExtraPlayerShadows_hook` at 0x53E0C8. It batches every shadow registered this frame, binds each batch's texture at **0x70AA2F**, and rasterises at **0x70AE30–0x70AE4A**. It draws:

* CJ's **low-quality "circle below feet" shadow** (`gpShadowPedTex` = `shad_ped`, type `SHADOW_DEFAULT`) when the ped is not on the real-time-shadow path; and
* CJ's **real-time shadow quad** (`SHADOW_INVCOLOR`, SRC=ZERO / DST=INVSRCCOLOR — a multiplicative "darken everything under this quad") when the ped is on the high-quality path.

Both are centred on CJ's root bone, both are suppressed beyond **15.0 m** from the camera (`MAX_DISTANCE_PED_SHADOWS`, 0x8D5240 / squared 0xC4B6B0), and both are drawn by the same pass. That pass is the ring.

The render-state the draw needs is exactly the vanilla `SHADOW_DEFAULT` contract: stage-0 raster = `shad_ped`'s raster, `VERTEXALPHAENABLE=TRUE`, `SRCBLEND=rwBLENDSRCALPHA(5)`, `DESTBLEND=rwBLENDINVSRCALPHA(6)`, `ZTEST=TRUE`, `ZWRITE=FALSE`, `FOG=FALSE`, `CULLNONE`, `TEXTUREADDRESS=CLAMP`, `TEXTUREFILTER=LINEAR`, `ALPHATESTFUNCTIONREF=0`. The build's `shadowContractForce()` forces all of these **except the texture raster** — and the project already has proof that a stale stage-0 raster cache can no-op a material bind (`pedStage0Resync`, `src/core/main.cpp:1579-1615`). A no-op'd raster bind turns the radial `shad_ped` quad into a **solid black polygon/disc** — the hard-edged black wedge in the 01:14:17 screenshot.

**One-line experiment:** set `pedShadows = 1` in `skygfx.ini` (forces `FX_QUALITY_VERY_HIGH` through `FX::GetFxQuality_ped`), then run the same walk. If the radial ring becomes a crisp projected silhouette (or changes shape/position), the ring is the stored ped-shadow draw at 0x70A960. Set `pedShadows = 0` to force the low-quality circle back.

---

## 1. Evidence base

### 1.1 Exe

`E:\games\gtasa_skygfx_plus\gta_sa.exe` (1.0 US, PE image base 0x00400000). Fresh Capstone disassembly of every address below. Address-to-name mapping cross-checked against `E:\dev(dave)\releases\GTASource\gta-reversed-master\source\game_sa\Shadows.cpp` (function install table at lines 13-47) and `Entity/Ped/Ped.cpp`, `RealTimeShadow.cpp`, `RealTimeShadowManager.cpp`, `ShadowCamera.cpp`.

### 1.2 Log

`E:\games\gtasa_skygfx_plus\skygfx_dbg.log` is **live and still being appended** while this audit ran. The requested run is the first **30,661 lines**, `[01:12:26.118]` → `[01:19:31.145]`; all counts below were frozen from that slice (a copy was analysed; the file had grown to 50,979 lines / 01:25:52 by the time of the last read).

### 1.3 Screenshots

ShareX `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\`: 22 shots between 01:13:21 and 01:17:22 (plus later 01:19-01:20). Key frames are correlated in §4.6. The screenshots are debug-camera shots (teleports/cheats), not clean gameplay captures, but they show the shadow failure directly.

---

## 2. Prior claim check

The earlier audit claimed:

> the real static-shadow rasterisation is `0x706AB0` at `0x53EA0D` → `0x707CA0` → `0x705900`, and its inner draw sets `ZTEST=0`, `blend=0`, `TEXTURERASTER=NULL` and does NOT restore the texture.

**Refuted, with correction.** That chain is the **real-time shadow**, not static shadows:

| Address | Actual function | What it does |
|---|---|---|
| 0x53EA0D | `CScene::Render` call site | `ecx = 0xC40350` = `g_realTimeShadowMan`; calls `CRealTimeShadowManager::Update` |
| 0x706AB0 | `CRealTimeShadowManager::Update` | loops `m_apShadows`; calls `CRealTimeShadow::Update` (0x706600) and `CShadows::StoreRealTimeShadow` (0x707CA0) |
| 0x707CA0 | `CShadows::StoreRealTimeShadow` | distance check (15 m), `CalcPedShadowValues`, `SetLightProperties`, `StoreShadowToBeRendered(type=SHADOW_INVCOLOR)`; **no rasterisation** |
| 0x705900 | `CRealTimeShadow::SetLightProperties` | `RwFrameRotate` + `CShadowCamera::SetLight`; **writes no render states at all** |
| 0x706600 | `CRealTimeShadow::Update` | calls `CShadowCamera::Update` (0x705BF0/0x705C80) |
| 0x705620 | `CShadowCamera::Update_Internal` | clear raster, `RwCameraBeginUpdate`, render atomic, `InvertRaster`, `RwCameraEndUpdate` |
| 0x705660 | `CShadowCamera::InvertRaster` | **the draw that sets `TEXTURERASTER=NULL`, `VERTEXALPHA=TRUE`, `SRCBLEND=INVDESTCOLOR`, `DESTBLEND=ZERO`** |

`InvertRaster` details (0x705660, disassembly confirmed):

* state 1 (`rwRENDERSTATETEXTURERASTER`) ← `NULL`
* state 0xC (`rwRENDERSTATEVERTEXALPHAENABLE`) ← `TRUE`
* state 0xA (`rwRENDERSTATESRCBLEND`) ← `rwBLENDINVDESTCOLOR`
* state 0xB (`rwRENDERSTATEDESTBLEND`) ← `rwBLENDZERO`
* full-raster `RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, ...)` (0x734E90)
* after: `ZTESTENABLE=TRUE`, `SRCBLEND=SRCALPHA`, `DESTBLEND=INVSRCALPHA`
* **not restored:** `TEXTURERASTER` (left NULL) and `VERTEXALPHAENABLE` (left TRUE).

So: `TEXTURERASTER=NULL` is true; `ZTEST=0` is **false** for `InvertRaster` (only `CShadowCamera::DrawOutlineBorder`, 0x705790, disables ZTEST); `blend=0` is **false** (it is INVDESTCOLOR/ZERO, a deliberate invert, not blend off). The camera is a **128×128** shadow camera (`Create(7)`), not a static-shadow raster, and the draw is scoped by `RwCameraBeginUpdate(m_pShadowCamera)` to that raster — unless the camera/raster is broken.

**Inside our shields?** No. There is no hook at 0x706AB0, 0x706600, 0x705BF0, 0x705C80, 0x705620 or 0x705660. The only shadow hooks are 0x53E0BE (UpdateStaticShadows), 0x53E0C8 (RenderStoredShadows) and the 0x708300 entry (RenderStaticShadows).

---

## 3. Mechanism dossiers

### M1 — `CShadows::RenderStoredShadows` 0x70A960 (primary; wrapped as "extra")

**Live:** yes, every frame; the log's `[shadow_contract] extra exit healed=0` at 01:13:23.027 proves the 0x53E0C8 wrapper ran.

**What it draws:** all `CRegisteredShadow` entries registered this frame (`ShadowsStoredToBeRendered` at 0xC403DC; array starts 0xC40430, entry stride 0x34). Player-relevant entries:

* `StoreShadowForPedObject` (0x707B40) → `gpShadowPedTex` (`shad_ped`, 0xC403E4), type `SHADOW_DEFAULT` = the **"low quality circle below feet shadow"** (gta-reversed `Ped.cpp:2888`, comment in source). The player is always stored if within 15 m; non-player peds are also visibility-tested.
* `StoreRealTimeShadow` (0x707CA0) → `CRealTimeShadow::GetShadowRwTexture` (0x7059F0), type `SHADOW_INVCOLOR`. This is the high-quality path; the quad multiplies the destination (`dst *= 1 - srcRGB`) so it darkens **everything** under the quad, not just the ground.

**Radius / falloff:**

* Store cull: `MAX_DISTANCE_PED_SHADOWS = 15.0f` (0x8D5240), squared `MAX_DISTANCE_PED_SHADOWS_SQR` at 0xC4B6B0. `StoreShadowForPedObject` rejects `boneToCamDist2DSq >= 0xC4B6B0`; `StoreRealTimeShadow` rejects `> 0xC4B6B0`. This is the only player-centred radius in the shadow code.
* Strength falloff: `CalculateShadowStrength(dist, 15, shadowStrength)` — full strength below 7.5 m, linear fade 7.5 → 15 m.
* Quad size: `frontX/frontY/sideX/sideY` come from `CalcPedShadowValues` (0x7076C0), derived from `CTimeCycle::m_VectorToSun`; `_rw`/RT multiplies by 1.5. Nominal size is ~1-3 m, but it is a **screen-space-looking** large polygon when the camera is close to the ground (see 01:14:17).
* `shad_ped` itself is a radial alpha blob (soft edge). If its alpha is bypassed, the quad reads as a hard-edged dark polygon.

**Draw path inside 0x70A960:**

* entry state block 0x70A960–0x70A9DD: state 8 (ZWRITE) = 0, state 6 (ZTEST) = 1, state 0xC (VERTEXALPHA) = 1, state 0xE (FOG) = 0, state 2 (TEXTUREADDRESS) = 3 (CLAMP), state 0x14 (CULLMODE) = 1 (CULLNONE), state 0x1E (ALPHATESTFUNCTIONREF) = 0, state 9 (TEXTUREFILTER) = 2 (LINEAR).
* per shadow: `call 0x707460` (SetRenderModeForShadowType) at 0x70AA2A; then **`RwRenderStateSet(rwRENDERSTATETEXTURERASTER=1, *(RwRaster**)m_pTexture)` at 0x70AA2F–0x70AA3C**.
* batching: `0x70A7E0` (RT) at 0x70AD0D, `0x70A630` at 0x70AD6F, `0x70A470` at 0x70ADB8; flush at **0x70AE30–0x70AE4A** (`0x7EF450`/`0x7EF550`/`0x7EF520` = RwIm3D transform/render/reset).
* final state restore 0x70B6E1–0x70B713 (VERTEXALPHA false, ZWRITE true, ZTEST true, TEXADDR wrap, CULLBACK) and count reset.

**Render-state contract it needs (per shadow type, 0x707460):**

| type | name | SRC | DST |
|---|---|---|---|
| 1 | `SHADOW_DEFAULT` | `rwBLENDSRCALPHA` (5) | `rwBLENDINVSRCALPHA` (6) |
| 2 | `SHADOW_ADDITIVE` | `rwBLENDONE` (2) | `rwBLENDONE` (2) |
| 3 | `SHADOW_INVCOLOR` | `rwBLENDZERO` (1) | `rwBLENDINVSRCCOLOR` (4) |

Plus the entry block above and the stage-0 raster bind at 0x70AA2F.

**Our wrapper** (`src/core/main.cpp:1345-1382`, `shadowContractForce`) forces through L1+L2+L3: VERTEXALPHA=TRUE, SRC=5, DST=6, ZTEST=TRUE, ZWRITE=FALSE, ALPHATESTENABLE=TRUE, ALPHAFUNC=GREATEREQUAL, ALPHAREF=0, FOG=FALSE. It does **not** capture/force:

* `rwRENDERSTATETEXTURERASTER` (stage 0)
* `rwRENDERSTATECULLMODE`
* `rwRENDERSTATETEXTUREADDRESS`
* `rwRENDERSTATETEXTUREFILTER`

The pass sets 2/0x14/9 itself, so only the **texture raster** is unguarded. This is the same failure class the project already fixed for peds (`pedStage0Resync`, `src/core/main.cpp:1579-1615`: "A raw NULL bind earlier can leave the device on a foreign raster while the cache still reports the old one; setting the cached value alone would then be a cache-hit no-op"). No equivalent exists for the shadow pass.

**Consequence:** if the RW raster cache says `shad_ped` but the device is on NULL/foreign raster, `RwRenderStateSet(1, shad_ped)` at 0x70AA2F is a cache-hit no-op; the `shad_ped` quad rasterises **untextured** → a solid black polygon/disc. The 01:14:17 screenshot is exactly a hard-edged black quadrilateral emanating from CJ's feet.

**Ring vs wedge:** the radial `shad_ped` texture (when bound) gives the soft ring/disc; the same quad with the alpha/texture lost gives the hard black wedge. Same draw, same address.

---

### M2 — Ped "circle below feet" store, 0x707B40

**Live:** yes when `CPed::PreRender` chooses the low-quality branch.

**Selection** (`CPed::PreRender`, gta-reversed `Ped.cpp:2888-2927`; `FX::GetFxQuality_ped` is injected at **0x5E675E / 0x5E676D**, `src/core/main.cpp:7550-7551`):

```
if (g_fx.GetFxQuality() != VERY_HIGH && (g_fx.GetFxQuality() != HIGH || !IsPlayer()))
    DrawDummyShadow();          // StoreShadowForPedObject -> shad_ped, SHADOW_DEFAULT
else if (dist2D(rootBone, camera) <= 15*15) {
    ...
    if (drawRealTimeShadow) { g_realTimeShadowMan.DoShadowThisFrame(this); DrawDummyShadow(); }
}
```

`FX::GetFxQuality_ped()` returns `config->pedShadows ? 3 : 0` when `pedShadows >= 0`, else the engine `fxQuality` (0=low, 1=medium, 2=high, 3=very high).

* `pedShadows=-1` + engine FX quality **0/1**: CJ and all peds use the **circle** (`shad_ped`).
* `pedShadows=-1` + engine FX quality **2**: CJ uses the **real-time shadow** (circle suppressed by `m_pShadowData`); non-player peds use the circle.
* `pedShadows=-1` + engine FX quality **3**: CJ and all nearby peds use the real-time shadow.
* `pedShadows=0` forces **low** (everyone circle); `pedShadows=1` forces **very high** (real-time shadows). Neither value means "off".

**Store call:** `CPed::PreRender` at 0x5E6900 (and `CCutsceneObject::PreRender` at 0x5B1F81) → 0x707B40. `StoreShadowForPedObject` writes `m_pTexture = gpShadowPedTex` and `m_nType = SHADOW_DEFAULT`.

**Verdict:** this is the literal "ring/disc centred on CJ" when the engine FX quality is low/medium. Under the high-quality path CJ instead gets M3's INVCOLOR quad, which is what "darkens stuff" describes.

---

### M3 — Real-time shadow draw (`SHADOW_INVCOLOR`) + the 0x705660 state leak

**Live:** when the engine FX quality is HIGH/VERY_HIGH (the usual PC default is high). The real-time manager runs unconditionally (`0x53EA0D` → `0x706AB0`), but `StoreRealTimeShadow` returns early without `m_pShadowData` or beyond 15 m.

**Texture source:** `CRealTimeShadow::Update` (0x706600) → `CShadowCamera::Update_Internal` (0x705620): clear the 128×128 camera raster to `RGBA(255,255,255,0)`, render the ped atomic, then `InvertRaster` (0x705660). The inverted raster is the shadow texture. `InvertRaster` leaves `TEXTURERASTER=NULL` and `VERTEXALPHA=TRUE` live after `RwCameraEndUpdate`; the next consumer that does not set a texture inherits NULL. This is a **real, unshielded leak**, and it is adjacent to the player shadow.

**Draw:** the `SHADOW_INVCOLOR` quad is stored by 0x707CA0 and drawn by M1 at 0x70AE30-0x70AE4A, with `SRC=ZERO`, `DST=INVSRCCOLOR` (0x707460 type-3 branch). Because the destination is multiplied by `(1 - srcRGB)`, an all-white shadow texture turns the whole quad black. If the shadow camera or the raster is broken, the artifact is a **black quadrilateral centred on CJ**, not a soft ring.

**Inside our shields?** The store/draw is inside M1's wrapper (0x53E0C8). The **raster generation** is not: 0x706AB0/0x706600/0x705620/0x705660 have no hooks.

**Verdict:** second most likely, and the best explanation for the hard-edged black wedge; also the pass that most literally "darkens stuff".

---

### M4 — `CShadows::RenderStaticShadows` 0x708300 (wrapped as "stored")

**Live:** yes; the log's `[shadow_contract] stored pre-entry ...` lines (25 in the window) come from the function-entry hook at 0x708300.

**What it draws:** batched `CStaticShadow` polygons from `aStaticShadows` (0xC4A030..0xC4AC30, stride 0x40). Entry state block (0x708300–0x70835E): ZWRITE=0 (8), ZTEST=1 (6), VERTEXALPHA=1 (0xC), FOG=0 (0xE), ALPHATESTFUNCTIONREF=0 (0x1E), TEXTUREFILTER=LINEAR (9). Per batch: `call 0x707460` (0x7083A4), stage-0 raster bind (same pattern), `RenderBuffer::RenderStuffInBuffer` at the end of each batch. Handles `SHADOW_DEFAULT`/`ADDITIVE`/`INVCOLOR` via 0x707460.

**Radius:** none; the static shadow's own `drawDistance` was baked at `StoreStaticShadow` time. This pass is **not** player-centred.

**Inside our shields?** Yes: entry-hooked at 0x708300, covering both call sites 0x53DDE4 and 0x53E0C3. `UpdateStaticShadows` (0x707F40) is hooked at 0x53E0BE only; it draws nothing (frees expired temporary static shadows, loop 0xC4A034-0xC4AC34).

**Verdict:** live but not the ring; a source of the same texture/state failure class if a static texture is stale, but the geometry is world-anchored.

---

### M5 — `CShadows::RenderExtraPlayerShadows` 0x707FA0 (the real one, NOT hooked)

**Live:** only inside a vehicle. It is reached by a **tail-jump** `E9 00553A04 → 00707FA0` at the end of `CRenderer::PreRender` (0x553910-0x553A0F), not by the scene shadow trio. First checks: `word [0xB7C4EA]` = `CTimeCycle` shadow strength must be non-zero; `FindPlayerVehicle` (0x56E0D0) must return a vehicle; rejects RC Bandit, bikes, boats, planes. Then loops active point lights (`0xC3F0D0` count, base from `lea` at 0x7001FD) and stores `gpShadowCarTex` (0xC403E0) shadows at 0x707390 (calls at 0x708220/0x708255).

**On foot:** returns at 0x707FBB-0x707FCE. It cannot be the walking ring.

**Inside our shields?** No wrapper at all. It is vanilla.

**Note:** the build's `[shadow_install] 0x53E0C8 ... site=CShadows::RenderExtraPlayerShadows` is a misnomer; 0x53E0C8's callee is 0x70A960.

---

### M6 — Point lights / fog pool (`CPointLights::Render` 0x7002D0)

**Live:** both call sites are hooked and verified (`[pointlights_install] 0x53E21D orig=007002D0 OK`, `0x53DE6B orig=007002D0 OK`; `src/core/main.cpp:3071-3111`).

**What it draws:** `CShadowCamera`-unrelated `CPointLights::RenderFogEffect` street-lamp fog cones/dots and the `0x700B6B` glow sprite through the shared buffered-sprite flush 0x70CF20 / 0x70E780 (`CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect`, `main.cpp:2889-2917`). It is **additive** in vanilla: `rwBLENDONE` / `rwBLENDONE`, `ZWRITE=FALSE`, `ZTEST=TRUE`, `VERTEXALPHA=TRUE`, texture `gpCloudTex[0]`. Our shield forces `SRCALPHA/ONE` (or `ONE/ONE` for clouds) at the flush.

**Radius:** `MAX_DIST_POINT_LIGHT = 15.0f` (PointLights.cpp:88) around **the camera**, not the player; the light pool is around each light's position, radius 3-9 m.

**Could it be a black disc?** Only if the RGB of the bound texture is black and the blend is changed to `SRCALPHA/INVSRCALPHA`; the shield explicitly forces `SRCALPHA/ONE`, and the flush is producer-aware. The log shows `[CloudShield] flush batch=sprites forced SRCALPHA/ONE` at 01:13:15 and `[CloudShield] flush batch=clouds forced ONE/ONE` at 01:13:40. Not a darkening pass, not player-centred.

**Verdict:** not the ring; low probability.

---

### M7 — Stencil shadows (`CStencilShadows`)

**Live:** `stencilShadows=-1` leaves the engine's FX-quality value (`FX::GetFxQuality_stencil`, injected at 0x7113B8/0x711D95/0x70F9B8). The pass is `CStencilShadows::RenderStencilShadows` 0x7113B0 / `Render` 0x710D50; vehicle/object volumes (`RenderForVehicle` 0x70FAE0, `RenderForObject` 0x710310). It sets `STENCILENABLE=TRUE`, `STENCILFAIL/ZFAIL/PASS=KEEP/INCR/DECR`, `CULLFRONT`/`CULLBACK`, `SRCBLEND=ZERO`, `DESTBLEND=ONE`, and at the end leaves `ZWRITE=FALSE`, `ZTEST=FALSE`, `STENCILENABLE=TRUE`, `TEXTURERASTER=NULL`, `CULLNONE` before the `RenderBuffer` overlay, then restores with a final block.

**Risk:** if the depth hook's INTZ substitute has **no stencil bits** (`src/render/depthhook.cpp` substitutes the depth-stencil), the stencil test cannot accumulate and the volume faces themselves draw `ZERO/ONE` black; the result is a large black wedge. That matches the 01:14:17 screenshot shape, but the geometry is the vehicle/object volume, not a disc around CJ. `pedShadows` does not control stencil shadows; `stencilShadows=0` is the clean A/B.

**Verdict:** plausible for a black wedge, not for a player-centred ring; keep as a control experiment.

---

### M8 — Ped pipeline (`src/entities/chars.cpp`) and `pAmbient`

Not a draw of ground geometry. `[Ped] pAmbient override` only changes the ped's ambient light; `[PED] ZWRITE ... forcing TRUE for cb` is a state repair. A ped cannot cast a ground ring through this path. `[PED] dual pass skipped` is a dual-pass quality gate, not a shadow.

### M9 — Mirror / envmap / water

`CMirrors::RenderMirrorBuffer` (hook at 0x53EABA) renders the world into a 2048×1024 raster; `[BUILDING] cb camRas=... 2048x1024` (18 occurrences) is that off-screen raster, not a screen effect. `CEnvMap` (256²) and `waterPipe` are camera/reflection passes. None is player-centred, none darkens the main scene radially.

---

## 4. What the log actually says (frozen 30,661-line window)

Window: `[01:12:26.118]` → `[01:19:31.145]` (the file continued to 01:25:52; the requested run is this slice).

### 4.1 `[shadow_contract]` — 110 lines

| Signature | Count | Notes |
|---|---|---|
| `static pre-entry layer drift:` | **81** | this is **0x707F40 UpdateStaticShadows** (draws nothing) |
| `stored pre-entry layer drift:` | **25** | this is **0x708300 RenderStaticShadows** |
| `static exit healed=` | **3** | 1×0, 2×2 |
| `extra exit healed=` | **1** | the 0x70A960 stored pass |

Drift distribution:

```
3  static pre-entry ... drift=0
51 static pre-entry ... drift=1
26 static pre-entry ... drift=2
1  static pre-entry ... drift=3
25 stored pre-entry ... drift=1
```

There is **no** `extra pre-entry` line and **no** `stored exit` line in the window (the `shadow_contract_exit` throttle bucket is shared, so one exit per 2 s survives). The naming in the log is rotated relative to the real exe: `static` = UpdateStaticShadows, `stored pre-entry` = RenderStaticShadows (0x708300), `extra` = RenderStoredShadows (0x70A960).

Representative lines (verbatim):

```
[01:13:03.011] [shadow_contract] stored pre-entry layer drift: drift=1 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=1) drv(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=1)
[01:13:03.013] [ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=1)
[01:13:05.036] [shadow_contract] static exit healed=0
[01:13:23.027] [shadow_contract] extra exit healed=0
[01:13:39.033] [shadow_contract] static pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
[01:13:41.048] [shadow_contract] static pre-entry layer drift: drift=2 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=0 fog=1)
[01:16:53.670] [shadow_contract] stored pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
[01:17:13.514] [shadow_contract] static exit healed=2
[01:17:15.517] [shadow_contract] static exit healed=2
```

Key facts:

* The rw cache frequently says `blend=1` while the **device says `blend=0`** (blend disabled) and often `zw=0` (zwrite off). The wrapper forces the canonical block for the pass; at exit it heals L3 to the rw cache when `drift > 0` (the 01:17:13/15 `healed=2` receipts) and otherwise restores the captured values verbatim.
* `ref=100` (envmap leak) is the dominant alpha-ref value from 01:13:39 onward; the wrapper forces `ref=0` for the pass and restores 100 afterwards.
* Only the 01:13:03 heal fired (`dev(ref=0 blend=0 zw=1 fog=1) -> rw(...)`); the later `static exit healed=2` receipts at 01:17:13/15 are the only other visible heal evidence.

### 4.2 `[ShadowHeal]` — 1 line

```
[01:13:03.013] [ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=1)
```

### 4.3 `ZWRITE` — 91 lines

90 × `ZWRITE off/desynced on entry (...) — forcing TRUE for cb` plus 1 × the `[PedOpaque] pre-entry dev ...` raw dump.

Breakdown of the 90 forced repairs:

| Tag | Count | Signature |
|---|---|---|
| `[PED]` | 26 | `ZWRITE off/desynced on entry (rw=1 dev=0)` (one at start `rw=0 dev=0`) |
| `[BUILDING]` | 47 | `ZWRITE off/desynced on entry (rw=0 drv=0 dev=0)`; one `rw=1 drv=1 dev=0` at 01:14:04 |
| `[VEHICLE]` | 17 | `ZWRITE off/desynced on entry (rw=1 cache=1 dev=0)` |

The single non-forced line:

```
[01:13:02.913] [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 | rw ref=2 fn=5 vtx=0 zw=0 ztest=1 | drv ref=2 fn=5 blend=0 zw=0
```

### 4.4 `[PED]` — 77 lines

| Signature | Count |
|---|---|
| `[PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=04C26410` | 51 (all pipe `04C26410`) |
| `[PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb` | 26 |

(One of the 51 has `zwrite=1 hasAlpha=0 pipe=...`; the invariant is the same.) No `[PED]` line references shadows or a radius.

### 4.5 `[BUILDING]` — 66 lines

| Signature | Count |
|---|---|
| `[BUILDING] ZWRITE off/desynced on entry (rw=0 drv=0 dev=0) — forcing TRUE for cb` | 47 |
| `[BUILDING] cb camRas=1F361870 2048x1024 != screen 1920x1080 — non-screen raster LIVE during building cb` | 18 |
| `[BUILDING] alpha/blend layer-3 drift repaired (...)` | 1 |

The 2048×1024 raster is the mirror/offscreen pass (`[MirrorPass] hook: TypeOfMirror=2 ... pBuffer=1F361870`), not a screen-space darkening.

### 4.6 Screenshot correlation (01:13-01:17)

22 ShareX captures fall in the window. The meaningful coincidences (mtime within ±0.35 s of a log line; routine `[PF]/[PostFX]/[Pipeline]/[BPIPE]/[BLIT]/XboxBuilding` spam excluded):

| Screenshot (mtime) | Visual | Coincident log |
|---|---|---|
| `gta_sa_vqcKxDVC4J.jpg` 01:13:50.109 | outdoor; a detached **soft black blob** on the pavement north of CJ; CJ has no shadow at his feet | `[SKY] entry camRas=...1920x1080 ... blend=1` 01:13:50.033; `[BUILDING] ZWRITE ...` 01:13:50.172 |
| `gta_sa_2KLfpMepLa.jpg` 01:14:17.105 | outdoor; **giant black quadrilateral** from CJ's feet to the right edge of the frame | `[shadow_contract] static pre-entry drift=2 ... rw(blend=1 ref=100 ...) dev(blend=0 ref=100 zfunc=4 zw=0)` 01:14:17.189 |
| `gta_sa_h4OGxh0OLT.png` 01:15:42.989 | helicopter over water; scene normal | `[PED] dual pass skipped` 01:15:42.842; `[PED] ZWRITE ...` 01:15:43.277 |
| `gta_sa_GprLAhjk7I.png` 01:16:53.437 | debug close-up | `[shadow_contract] stored pre-entry drift=1 ... dev(blend=1 ref=100 ...)` 01:16:53.670 |
| `gta_sa_ZGcGFYMm2i.png` 01:17:15.305 | debug shot | `[shadow_contract] static exit healed=2` 01:17:15.517 |
| `gta_sa_t9tPhO7pRN.png` 01:17:22.077 | giant pale ped against sky, large translucent quads | `[PostFX-REPORT] frame=27214 ... SSAO=0 smaa=0 normalBuf=0 pipeChain=0`; `[BUILDING] ZWRITE ...` 01:17:21.778 |

No screenshot-specific event line exists. The recurring 2 s shadow receipts are the only shadow-related lines that line up with the captures. The 01:14:17 pre-entry is the strongest correlation: it is a **drifted/polluted** entry (`rw blend=1`, `dev blend=0`, `dev zw=0`, `ref=100`) on the exact frame of the black wedge.

---

## 5. The "only zone where peds are visible" clue

There is exactly one radius in the shadow code that is tied to the camera/player and matches a visible boundary:

* `MAX_DISTANCE_PED_SHADOWS = 15.0f` (0x8D5240; squared at 0xC4B6B0) gates **every** stored ped shadow, in both `StoreShadowForPedObject` (0x707B40) and `StoreRealTimeShadow` (0x707CA0).
* `MAX_DIST_POINT_LIGHT = 15.0f` (`PointLights.cpp:88`) gates the point-light fog sprites by camera distance.

Both are 15 m. The ped-shadow one is the only one that draws **dark** content. So a 15 m ring around the camera (≈CJ in normal third-person) is the envelope of every stored ped shadow. Inside the envelope the shadow quads are drawn; outside nothing is drawn. If the shadow quads are solid black (M1/M3), the envelope reads as a dark ring/disc with a hard boundary at 15 m. That is the most economical explanation of the user's "only zone" observation, and it does not require a separate ped-render bug.

The other candidate boundary is draw order: the stored-shadow pass is the last thing in the scene before the effects phase, and it leaves `ZWRITE=FALSE`/blend states. A ped drawn after it can be covered by a shadow quad only if the quad is above the ped in depth; the pass is not a depth occluder (ZWRITE off), so it cannot erase peds. The 15 m shadow-store boundary is the better fit.

---

## 6. Ranked answer

| Rank | Mechanism | Exe draw address | Player-centred? | Darkens? | Live? | Inside shield? |
|---|---|---|---|---|---|---|
| **1** | `RenderStoredShadows` — ped circle (`shad_ped`, `SHADOW_DEFAULT`) + RT (`SHADOW_INVCOLOR`) | **0x70A960**; raster bind 0x70AA2F; flush 0x70AE30-0x70AE4A | **Yes** (root bone) | **Yes** (alpha or multiply) | **Yes, every frame** | Yes at 0x53E0C8; texture raster not covered |
| **2** | Real-time shadow camera/invert (texture source) | 0x706AB0 → 0x707CA0 → 0x705900; actual raster 0x705620/0x705660 | Yes (via M1) | Yes (multiply) | Yes | **No** |
| 3 | `RenderStaticShadows` (static world shadows) | 0x708300 | No | Yes | Yes | Yes (entry hook) |
| 4 | `RenderExtraPlayerShadows` (vehicle headlight shadows) | 0x707FA0 (tail-jump 0x553A04) | Vehicle only | Yes | Only in a car | **No** |
| 5 | Stencil shadow volumes | 0x7113B0 / 0x710D50 | No (vehicles/objects) | Yes (black faces if stencil lost) | If FX quality ≥ default | No |
| 6 | Point-light fog/corona pool | 0x7002D0 / 0x700B6B / 0x70E780 | No (lights) | No (additive) | Yes | Yes (re-armed) |
| 7 | Ped pipeline ZWRITE/pAmbient | — | CJ only | Ped only | Yes | Yes |

**Single most likely:** M1, the stored ped shadow, with the **literal ring** being the `SHADOW_DEFAULT` `shad_ped` circle and the **hard black wedge** being the same quad with its stage-0 texture bind no-op'd (or the `SHADOW_INVCOLOR` RT quad with a broken all-white shadow map).

**Exact draw:** `CShadows::RenderStoredShadows`, exe **0x70A960**; per-batch raster bind **0x70AA2F**; rasterisation **0x70AE30–0x70AE4A**.

**Render-state contract it needs:** stage-0 `rwRENDERSTATETEXTURERASTER` = `*(RwRaster**)m_pTexture` (`shad_ped` for the circle; the shadow-map texture for the RT quad), `VERTEXALPHAENABLE=TRUE`, `SRCBLEND=5`/`DESTBLEND=6` for `SHADOW_DEFAULT` (or `SRCBLEND=1`/`DESTBLEND=4` for `SHADOW_INVCOLOR`), `ZTEST=TRUE`, `ZWRITE=FALSE`, `FOG=FALSE`, `CULLNONE`, `TEXTUREADDRESS=CLAMP`, `TEXTUREFILTER=LINEAR`, `ALPHATESTFUNCTIONREF=0` (and `ALPHATESTENABLE=TRUE`, `ALPHAFUNC=GREATEREQUAL`, as forced by the wrapper).

**One-line experiment:** in `skygfx.ini` set `pedShadows = 1` (forces `FX_QUALITY_VERY_HIGH` via `FX::GetFxQuality_ped`, hook 0x5E675E/0x5E676D). If the dark ring changes into a crisp projected silhouette (or moves/disappears), the ring is the stored ped-shadow draw at 0x70A960. Set `pedShadows = 0` to force the low-quality `shad_ped` circle back.

**Second experiment (mechanism-level, one probe):** in `shadowContractRun()` capture `RwRenderStateGet(rwRENDERSTATETEXTURERASTER)` and `d3d9device->GetTexture(0)`, log once at the 0x70A960 entry, and compare. If they differ on the frame with the ring, the 0x70AA2F bind is no-oping and the black solid is the untextured `shad_ped` quad.

---

## 7. Appendix — exe map

```
CShadows::UpdateStaticShadows        0x707F40  (96 B; no draw)
CShadows::RenderExtraPlayerShadows   0x707FA0  (tail-jump 0x553A04; vehicle only)
CShadows::RenderStaticShadows        0x708300  (entry-hooked as "stored")
CShadows::RenderStoredShadows        0x70A960  (wrapped as "extra"; the live player pass)
CShadows::StoreShadowForPedObject    0x707B40  (15 m cull; gpShadowPedTex 0xC403E4)
CShadows::StoreRealTimeShadow        0x707CA0  (15 m cull; SHADOW_INVCOLOR)
CShadows::SetRenderModeForShadowType 0x707460  (SRC/DST per type)
CShadows::CalcPedShadowValues        0x7076C0
CShadows::StoreShadowToBeRendered    0x707390
CRealTimeShadowManager::Update       0x706AB0  (called from 0x53EA0D)
CRealTimeShadow::Update              0x706600
CShadowCamera::Update_Internal       0x705620  (0x705BF0 clump / 0x705C80 atomic)
CShadowCamera::InvertRaster          0x705660  (TEXTURERASTER=NULL etc.)
CRealTimeShadow::SetLightProperties  0x705900  (no render states)
CRealTimeShadow::GetShadowRwTexture  0x7059F0
CPointLights::Render                 0x7002D0
CSprite::RenderBufferedOneXLUSprite  0x70E780  (wrapper 0x2889)
Sprite flush                         0x70CF20  (draw 0x70CF62/0x70CF9B -> 0x734EA0)
CStencilShadows::RenderStencilShadows 0x7113B0
```

Shadow call sites:

```
0x53DDD6 -> 0x707F40   (guarded by bRenderingReflection 0xC7C728; NOT hooked)
0x53DDE4 -> 0x708300   (covered by 0x708300 entry hook)
0x53DDF2 -> 0x70A960   (NOT hooked)
0x53E0BE -> 0x707F40   (hooked as "static")
0x53E0C3 -> 0x708300   (covered by 0x708300 entry hook; fresh bytes E8 38 A2 1C 00)
0x53E0C8 -> 0x70A960   (hooked as "extra")
0x53EA0D -> 0x706AB0   (real-time shadow manager; NOT hooked)
0x553A04 -> 0x707FA0   (tail-jump from CRenderer::PreRender; NOT hooked)
```

RW state numbers used above (from `external/d3d9/rwplcore.h`):

```
1 TEXTURERASTER, 2 TEXTUREADDRESS, 6 ZTESTENABLE, 8 ZWRITEENABLE,
9 TEXTUREFILTER, 0xA SRCBLEND, 0xB DESTBLEND, 0xC VERTEXALPHAENABLE,
0xE FOGENABLE, 0x14 CULLMODE, 0x1E ALPHATESTFUNCTIONREF
```

The wrapper's L1/L2/L3 struct (`src/core/main.cpp:1240-1249`) covers blend/src/dst/ztest/zwrite/alphafunc/alpharef/fog only. It has no field for `TEXTURERASTER`; therefore the stage-0 raster is the one shadow-pass state that can still silently disagree with the device.
