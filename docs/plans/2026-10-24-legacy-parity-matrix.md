# Legacy ↔ Current Render-State Parity Matrix

**Date:** 2026-10-24
**HEAD:** `b7396c4` + uncommitted working tree
**Snapshot:** files were being edited concurrently during this audit (the ped dual pass was restored to the legacy sequence at ~23:35, and `pipelinecommon.cpp` grew during the audit). All current-side `file:line` citations below are the values read from this snapshot:

| file | md5 |
|---|---|
| `src/core/main.cpp` | `1fff46d5c2fd22496a9c4611e63d5f2c` |
| `src/render/pipelinecommon.cpp` | `7d253c66001f500d97aa8f09ba7a54b0` |
| `src/render/buildingPipe.cpp` | `fee0bc1b38d12edf23a8240a4f7017d7` |
| `src/render/vehiclePipe.cpp` | `663aa3f6e30037300b228533a287cd4e` |
| `src/render/texalpha.cpp` | `7096fbd55c2172eb37dc07273424bd9b` |
| `backup_original/src/main.cpp` | `d7502146092716c5ac4c8400348ea9e8` |

Re-verify line numbers with `grep -n` before applying any patch.

**Verdict legend**

| verdict | meaning |
|---|---|
| `REGRESSION` | legacy behaviour is correct; current differs in a way that can change what is rendered. |
| `INTENTIONAL` | current differs deliberately (documented) and is a repair that preserves legacy output in the common case. |
| `INTENTIONAL*` | documented, but it is known to change legacy output — a parity break that should be gated/defaulted off. |
| `UNKNOWN` | source alone cannot prove the rendered result (depends on RW/exe internals, device drift, per-material state). |

---

## Key findings (up front)

1. **The legacy ped dual pass is already restored** in this working tree (`src/core/main.cpp:1400`, `:1711-1734`) — sequence, threshold and skin gate match `backup_original/src/main.cpp:422-436`. What remains different around peds is the **opaque contract** (blend off / alpha-test on for the whole pass) and the **entry ZWRITE force**.
2. **Legacy never forced cull globally and never set cull in any building/vehicle/ped callback.** The current tree already stopped forcing BACK in `pipeEnterCullMode` (it preserves NONE/FRONT); two inner `pipeForceCullMode(CULLBACK)` calls (building PBR post-IBL, vehicle Env post-IBL) and the frame baseline still do.
3. **Legacy's alpha doctrine was "rw states only"; the enable/ref/function were exe/RW-owned.** The current three-layer model forces them at every pipe boundary and the exit is not faithful for `ALPHABLENDENABLE` / `ALPHATESTENABLE` / `BLENDOP`.
4. **Legacy decals rely on the per-texture dual pass** (`TexInfo::dualPass` / `zwriteThreshold`) and game sorting; current PS2/Xbox still match, PBR adds a texture classifier plus a new decal ZWRITE policy.
5. **Highest-risk parity breaks:** ped opaque contract, default-pipe contract, `TAC_UNKNOWN → OPAQUE`, ped `pAmbient` override, exit-time blend/test fidelity, inner BACK cull re-asserts, corona shield.

---

## Section 1 — Inventory of `backup_original/`

The backup is a single flat `src/` tree (33 files, no assets/shaders/INI). It is the previous SkyGfx+ source, not aap's pristine upstream (two comments in it say "not on original skygfx" / "like default skygfx").

| File | Role (legacy) |
|---|---|
| `src/main.cpp` | DLL entry, INI/config, all exe hooks, `D3D9Render` / `D3D9RenderDual` / `D3D9RenderDefault_DUAL` / `D3D9RenderBlack_DUAL`, `myDefaultCallback` (peds/default pipe), grass callback, Tag/skidmark/moon/point-light hooks. |
| `src/buildingPipe.cpp` | Custom building pipeline callbacks: PS2, Xbox, Sphere; `buildingAmbient`; `TagRenderCB`; pipeline creation. |
| `src/vehiclePipe.cpp` | Custom car pipeline callbacks: PS2, `exe` (PC), Xbox, Specular, Leeds, Mobile, Env; `Switch`; hooks. |
| `src/pipelinecommon.cpp` | Matrix helpers, light upload, `pipeSetTexture`, shader resource creation (`CreateShaders`). No render-state helpers (the three-layer model did not exist). |
| `src/defaultFuncs.cpp` | PC/non-DN default building & car callbacks, `D3D9RenderNotLit` / `D3D9RenderPreLit`, `DNInstance_default`. |
| `src/envmap.cpp` | Env-map/reflection scene, `CRenderer::RenderRoadsAndBuildings` override, sphere-map reflection, env-map coronas. |
| `src/postfx.cpp` / `postfx.h` | PostFX chain (colour filter, radiosity, SSAO, SMAA, trails, water/underwater effects). Raw D3D writes with no save/restore. |
| `src/texdb.cpp` | Texture metadata DB (`models\texdb.txt`): `dualPass`, `zwriteThreshold`, detail/affiliate/stochastic/materialType; roadsign override. |
| `src/neo.cpp` / `neo.h` | Neo (Xbox-style) TXD/shaders/asset setup. |
| `src/neoCarpipe.cpp` | Neo vehicle pipeline (pass1/pass2, env tex). |
| `src/neoWaterdrops.cpp` | Neo water-drop screen effect. |
| `src/config.cpp` | INI reader used by the alternative config path (duplicates main.cpp's loader). |
| `src/debugmenu_ui.cpp` / `debugmenu_public.h` | DebugMenu integration / ImGui front-end. |
| `src/gta.cpp` / `gta.h` | Game globals (`pAmbient`, `pDirect`, `Scene`, device), structures, RW wrappers. |
| `src/pipeplg.cpp` | `SetPipelineID` / `GetPipelineID` atomic plugin (RSPIPE ids). |
| `src/extendedplg.cpp` | Extended atomic plugin (per-atomic defined vertex shader id). |
| `src/normmap_stubs.cpp` | Stubs for normal-map plugin calls exported by the exe. |
| `src/PC_GrassRenderer.cpp` / `.h` | PS2 grass renderer (CPlantMgr) bridge. |
| `src/SMAA.cpp` / `SMAA.h` / `AreaTex.h` / `SearchTex.h` | SMAA implementation + LUT headers. |
| `src/test.cpp` | Debug menu dump tool (`dumpMenu`), not shipped logic. |
| `src/LinkList.h`, `Pools.h`, `MemoryMgr.h`, `ModuleList.hpp` | Game-structure / memory / module helpers (RW SDK-era utility headers). |
| `src/skygfx.h` | Legacy `Config` struct, enums, externs, `dbglog` macros. |

No `shaders/`, `resources/`, INI, or model files exist under `backup_original/`; the legacy shaders/CSOs are the ones currently in `shaders/` + `resources/cso/`. This matters for parity: "legacy code" here means the .cpp behaviour, not a pinned shader set.

---

## Section 2 — Behaviour parity tables

### 2.1 Ped rendering

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| P1 | Ped dual pass runs when `pipe == skinPipe && dualPassPed && zwriteThresholdPed > 0` (`backup_original/src/main.cpp:422-423`). | Same condition restored (`src/core/main.cpp:1400`). The body is the legacy sequence (`:1711-1734`). | `INTENTIONAL` (state-layer writes only; sequence is parity) | Keep. Do **not** re-add `atomicHasAlpha` for `isPedPipe`. |
| P2 | Leg 1 `GREATEREQUAL@threshold` with ZWRITE TRUE; leg 2 `LESS@threshold` with ZWRITE FALSE; restores caller func/ref and ZWRITE TRUE (`backup_original/src/main.cpp:425-436`). | Same legs, but written through `setAlphaTestTwoLayer` / `setZWriteTwoLayer` (L1+L2+L3) and threshold clamped to 254 (`src/core/main.cpp:1720-1727`, `:1703`). | `INTENTIONAL` (legacy semantics, repaired layering) | Keep. The 254 clamp only changes INI=255 (latent blank mode) — leave it. |
| P3 | ZWRITE forced TRUE **only inside** the skin dual pass (`backup_original/src/main.cpp:425`); a non-dual ped atomic kept the inherited ZWRITE. | Ped entry forces ZWRITE TRUE through all three layers for **every** `isPedPipe` atomic (`src/core/main.cpp:1475-1488`), plus a sticky exit that rewrites canonical TRUE (`:1776-1788`). | `REGRESSION` | Move the force inside `if(dodual)`; for `!dodual` only detect/log. At exit, restore the captured values instead of the sticky TRUE rewrite when no dual pass ran. |
| P4 | No ped state contract. Per-mesh `VERTEXALPHAENABLE` comes from the skin instance data; `ALPHABLENDENABLE`/alpha test are RW/exe-owned; dual pass only touches func/ref/ZWRITE (`backup_original/src/main.cpp:408-440`). | `pedOpaqueContract` (default **1**, `src/core/main.cpp:5424`) captures and forces: `VERTEXALPHAENABLE=FALSE`, `ALPHABLENDENABLE=FALSE`, `ZWRITE/ZTEST/ZENABLE=TRUE`, `ZFUNC=LESSEQUAL`, `ALPHAFUNC=GREATEREQUAL@1` for the whole ped pass (`src/core/main.cpp:891-1014`, enforced `:1517-1536`, restored `:1796`). | `INTENTIONAL*` — documented black/hole-ped fix (`src/skygfx.h:637-642`, `docs/baseline/2026-10-24-screenshots/issues.md:49-68`), but legacy had no such force and blending OFF changes alpha-blended/fading peds. | Keep only the alpha-ref repair: if the cached ref is `0` or `140`, set it to `1` (L1+L2+L3); do **not** force blend/vertex-alpha/ZWRITE/ZTEST/ZFUNC. Default `pedOpaqueContract` to `0`, or rename to `pedAlphaRefRepair` default 1. Note the 140 leak is already fixed at `src/render/envmap.cpp:355-375`. |
| P5 | No `pAmbient` override anywhere. The mobile 1.5× ambient block is unreachable (function returns at `backup_original/src/main.cpp:536-537`); `BrightenLights` is commented out (`:479-500`). | `pAmbient` is replaced for `skinPipe` with `GetTimecycleAmbientPBR()` (max with the game value) and a hue-preserving floor `pedAmbientFloor` default **0.20** (`src/core/main.cpp:1582-1662`); restored after the draw (`:1757-1760`). | `INTENTIONAL*` (documented black-ped fix, `docs/plans/2026-10-24-ped-lighting-shadow-diagnosis.md`) but a direct legacy parity break. | Add `int pedAmbientOverride` (INI default **0**) and gate the block; keep the code for opt-in. When off, draw with the game's `pAmbient` exactly as legacy. |
| P6 | No cull writes in `myDefaultCallback` or the skin path (only grass: `backup_original/src/main.cpp:372-374`, `:402`). | Ped entry `pipeEnterCullMode()` (`src/core/main.cpp:1469`) / exit (`:1789`); helper saves the rw value, forces the sanitized **saved** value (NONE/BACK/FRONT all preserved) through L1+L2+L3 (`src/render/pipelinecommon.cpp:1285-1339`). | `INTENTIONAL` — parity-preserving state repair (the earlier "force BACK" version was the regression; it is already fixed in this snapshot). | Keep. Do not reintroduce an unconditional BACK force. |
| P7 | No VS/PS unbind before default-pipeline atomics. | `defaultCbShaderHygiene` (default 1, `src/core/main.cpp:5419`) unbinds stale VS/PS for every default atomic (`:1539-1567`). | `INTENTIONAL` | Keep (diagnostic/repair; no draw-path change for correct states). |
| P8 | Default-pipe dual pass gate is only `zwrite && dualPassDefault` — no `hasAlpha` (`backup_original/src/main.cpp:416-421`). | Adds `atomicHasAlpha(atomic,false)` + re-read ZWRITE for every `!isPedPipe` atomic (`src/core/main.cpp:1678-1687`). | `REGRESSION` | Delete the extra gate for the default-pipe branch; keep the legacy top gate at `:1348-1350`. Props/pickups/fences with texture-cutout alpha lose the two-leg pass today. |
| P9 | Vehicle/ped ambient source is `pAmbient` (game light), building ambient is `timecycle * CCoronas__LightsMult` (`backup_original/src/vehiclePipe.cpp:346-348`, `backup_original/src/buildingPipe.cpp:56-59`). | All three pipes use `GetTimecycleAmbientPBR()` (`src/render/vehiclePipe.cpp:533-556`, `src/render/buildingPipe.cpp:176-185`, peds `src/core/main.cpp:1587`), which also applies the night law/floor (`src/render/pipelinecommon.cpp:97-213`). | `INTENTIONAL*` (single source of truth) — changes interior/cutscene/dawn lighting versus legacy. | Add `int legacyAmbient` default 1: `GetTimecycleAmbient()` returns raw `timecycle * LightsMult * ambientMult` (skip `ClampAmbientToSky`) and `GetTimecycleAmbientPBR()` skips the floor; vehicle `uploadLights()` uses `pAmbient->color` as legacy did. The new law stays available by setting 0. |

### 2.2 Culling

Legacy cull inventory (exhaustive over the backup): `grassRenderCallback` only (`backup_original/src/main.cpp:372-374`, restored `:402`); `CRenderer::RenderRoadsAndBuildings` / `RenderFadingInBuildings` set CULLBACK for the world pass (`backup_original/src/envmap.cpp:209`, `:232`); postfx Im2D passes set `D3DCULL_NONE` (`backup_original/src/postfx.cpp:319`, `:1576`). **No building/vehicle/ped callback ever set cull, and nothing forced BACK globally.**

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| C1 | No cull writes in building cbs; inherited the game's current mode. | Building Switch: `pipeEnterCullMode()` / `pipeExitCullMode()` (`src/render/buildingPipe.cpp:2484`, `:2589`), saved value preserved (NONE/FRONT included). | `INTENTIONAL` — parity-preserving repair | Keep. |
| C2 | No cull writes in vehicle cbs; inherited. | Vehicle Switch: enter/exit (`src/render/vehiclePipe.cpp:3156`, `:3301`) — parity; **plus** an unconditional `pipeForceCullMode(rwCULLMODECULLBACK)` inside the Env cb after `pipeEnsureIBLBuffer` (`src/render/vehiclePipe.cpp:2080-2088`). | `REGRESSION` (the unconditional BACK at `:2088`) | Delete `:2088` (the Switch already synced the saved mode), or re-read the rw value and force that: `RwUInt32 m=rwCULLMODECULLBACK; RwRenderStateGet(rwRENDERSTATECULLMODE,&m); pipeForceCullMode(m);`. |
| C3 | No cull writes in building PBR (new pipe) — legacy never had a PBR cb; all legacy building cbs inherited. | PBR cb re-asserts BACK after `RenderIBLBuffer` (`src/render/buildingPipe.cpp:1876-1880`). | `REGRESSION` (same class as C2) | Same fix as C2: re-assert the span's saved mode, not BACK. |
| C4 | Never forced BACK globally. | `pipeApplyWorldBaseline()` pins CULLBACK (park FRONT → BACK, L1+L2+L3) at SceneStart / EffectsExit / PostFx.exit / DeviceReset (`src/render/pipelinecommon.cpp:1731-1804`, called `src/core/main.cpp:2888`, `:3387`, `:3416`, `src/render/depthhook.cpp:389`). | `INTENTIONAL*` — repair, but new global behaviour; could clobber a pass that owns NONE/FRONT across a boundary. | Remove the cull park/set from `pipeApplyWorldBaseline` (leave per-pipe saved repair), or default `stateBaselineEnable=0` for parity. |
| C5 | Mirror pass: no SkyGfx cull management. The game's CMirrors CULLNONE (saved at `0xC4028C`, documented in `src/render/pipelinecommon.cpp:1230-1233`) survived every building/vehicle callback; env-map world pass set BACK for its own capture (`backup_original/src/envmap.cpp:209`). | `pipeEnterCullMode()` preserves NONE/FRONT (`src/render/pipelinecommon.cpp:1292-1299`) — correct; mirror composite saves/restores raw geom (`src/render/mirrorPass.cpp:174-240`). The two unconditional BACK re-asserts (C2/C3) can still override NONE/FRONT inside those cbs. | `REGRESSION` limited to C2/C3; otherwise parity | Fix C2/C3. |
| C6 | Grass: save cull, set NONE when `!backfaceCull`, restore the **saved** value (`backup_original/src/main.cpp:372-374`, `:402`). | Grass: set NONE when `!backfaceCull`, but always restore BACK, never the saved value (`src/core/main.cpp:755-763`, `:806-807`). | `INTENTIONAL` (documented: avoid uninitialised `RwRenderStateGet`), but it can turn a mirror/shadow NONE/FRONT back into BACK for later draws. Low impact. | Keep the "don't read garbage" rule, but restore NONE/BACK/FRONT that was captured via a pre-seeded `RwRenderStateGet`; otherwise BACK. |

### 2.3 Alpha / blend / alpha-test doctrine

Legacy doctrine in one paragraph: **the pipe callbacks never set `D3DRS_ALPHABLENDENABLE`, `D3DRS_ALPHATESTENABLE`, `D3DRS_ALPHAFUNC`, `D3DRS_ALPHAREF`, `D3DRS_ZENABLE`, `D3DRS_ZFUNC`, `D3DRS_COLORWRITEENABLE` or `D3DRS_BLENDOP`.** They only set the rw states `VERTEXALPHAENABLE`, `ALPHATESTFUNCTION`, `ALPHATESTFUNCTIONREF`, `SRC/DESTBLEND`, `ZWRITEENABLE`. The enable/ref/function the device actually ran came from the exe's RW all-in-one node and the material/texture (`backup_original/src/main.cpp:224-225`: "this also takes texture alpha into account"). The only raw-D3D state writes in legacy are postfx (`backup_original/src/postfx.cpp:317-334`, `:449-488`, `:1574-1626`), moon (`main.cpp:857-863`) and the tag/skidmark/moon helpers.

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| A1 | `ALPHABLENDENABLE` is read but never written by pipe cbs (`backup_original/src/main.cpp:224-225`); per-mesh `VERTEXALPHAENABLE` drives it (`backup_original/src/buildingPipe.cpp:259`, `:446`, `:578`; `backup_original/src/vehiclePipe.cpp:442`, `:625`; `backup_original/src/defaultFuncs.cpp:102`, `:435`). | Entry forces `ALPHABLENDENABLE=FALSE` L1+L2+L3 (`src/render/pipelinecommon.cpp:1504`, `:1588`); per-mesh values mirrored to L2 (`src/render/vehiclePipe.cpp:765`, `:1493`, `:2384`; `src/render/buildingPipe.cpp:2258`); exit derives device enable from `saved.vtxAlpha` (`src/render/pipelinecommon.cpp:1643`, `:1648`, `:1680`) and hard-restores it — even if the pre-entry device blend was ON due to texture alpha. | `REGRESSION` (state fidelity; legacy never changed this) | Add `RwUInt32 blendEnable;` to `PipeAlphaState` (`src/skygfx.h:1339-1357`), capture `D3DRS_ALPHABLENDENABLE` L2+L3 at enter, restore it verbatim at exit (do not derive from `vtxAlpha`). |
| A2 | SRC/DEST blend: only per-callback save/restore (building `backup_original/src/buildingPipe.cpp:218-227`, `:350-354`; vehicle `backup_original/src/vehiclePipe.cpp:419-425`, `:536-540`); tags ONE/ZERO (`buildingPipe.cpp:151-162`); FX ONE/ONE (`buildingPipe.cpp:343-354`, `vehiclePipe.cpp:529-540`). BLENDOP never touched by pipes. | Entry forces SRCALPHA/INVSRCALPHA + `BLENDOP=ADD` L1+L2+L3 (`src/render/pipelinecommon.cpp:1509-1513`, `:1532-1538`); exit restores saved src/dst but hard-sets `BLENDOP=ADD` (`:1655`, `:1683`). | `INTENTIONAL` (repair) with one `REGRESSION` sub-item: BLENDOP is not captured, so postfx radiosity's `REVSUBTRACT` can never be restored. | Add `RwUInt32 blendOp;` (raw-only, capture device at enter) and restore it at exit instead of hard ADD. Keep the src/dst force for the span. |
| A3 | `ALPHATESTENABLE` is never set (there is not even a rw render state used for it). | Forced TRUE at every pipe entry (`src/render/pipelinecommon.cpp:1545`, `:1594`), every `pipeForceAlphaBlock` (`src/render/buildingPipe.cpp:589`), every exit (`pipelinecommon.cpp:1654`, `:1686`), the default-pipe contract (`src/core/main.cpp:1381`, `:1390`) and the ped contract (`:989`). | `REGRESSION` (legacy intent = exe-owned; current leaks test-on past every pipe span) | Capture `D3DRS_ALPHATESTENABLE` in `PipeAlphaState` and restore the captured value at exit. Keep the entry force only for the span (or gate it behind the same `pipeAlphaContract` flag). |
| A4 | `ALPHAFUNC/ALPHAREF`: only dual-leg pairs (`backup_original/src/main.cpp:236-250`), tag ref (`buildingPipe.cpp:149-162`), env ALWAYS+restore (`buildingPipe.cpp:343-358`), skidmarks ALWAYS (`main.cpp:879-886`). | Entry repairs ref 0→1 and forces device func/ref (`src/render/pipelinecommon.cpp:1504-1509`, `:1556-1558`); exit restores captured pair (`:1650-1654`); `pipeForceAlphaBlock` repeats the ref repair (`src/render/buildingPipe.cpp:562-571`); ped contract forces GE@1 (`src/core/main.cpp:981-991`). | `INTENTIONAL` for the ref==0 fix (black-potted-plant), `REGRESSION` for applying GE@1 to peds (P4) and for the hard ALPHATESTENABLE at exit. | Keep ref==0→1 at building/vehicle entry; remove the ped-wide GE@1; restore ALPHATESTENABLE (A3). |
| A5 | Texture alpha: RW/exe auto-derives blend from texture alpha (`backup_original/src/main.cpp:224-225`); per-texture `dualPass`/`zwriteThreshold` from `models\texdb.txt` (`backup_original/src/texdb.cpp:91-94`, `:128-131`, `:171-180`). No CPU texture scan. | Same `TexInfo` path (`src/extras/texdb.cpp:90-93`, `:123-126`, `:167-175`) **plus** `TexAlpha_Classify` for PBR only (`src/render/texalpha.cpp:703-802`; used `src/render/buildingPipe.cpp:2144`, `src/render/vehiclePipe.cpp:2276`). `TAC_UNKNOWN` is treated as OPAQUE (blend off, shader alpha 1) (`buildingPipe.cpp:2162-2164`, `vehiclePipe.cpp:2314-2319`). | `INTENTIONAL` for OPAQUE/CUTOUT/FADED (fixes unused-alpha DXT3 holes); `REGRESSION` for `TAC_UNKNOWN → OPAQUE` (legacy fell back to `hasAlpha`). | In both PBR mesh loops, make `TAC_UNKNOWN` fall back to legacy `hasAlpha` (`instancedData->vertexAlpha || matTranslucent`), not opaque: `blendOn = hasAlpha ? 1 : 0; shaderAlpha = hasAlpha ? 0.0f : 1.0f;`. |
| A6 | Per-mesh: `VERTEXALPHAENABLE` only. Per-cb: narrow rw-cache save/restore lists (see 2.5). | Per-cb canonical entry/exit contract + per-mesh L2 blend mirror + PBR texture class. | `INTENTIONAL` (documented state model) | Keep, subject to A1-A5 fidelity fixes. |

### 2.4 Decals (road decals / grunge / transparent building meshes)

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| D1 | Building meshes are drawn through `D3D9RenderDual(config->dualPassBuilding, …, texinfo)` (`backup_original/src/buildingPipe.cpp:324`, `:504`), gated per mesh by `hasAlpha = vertexAlpha || material->color.alpha != 255` (`:256-259`, `:440-446`, `:577-578`) and per texture `dualPass`/`zwriteThreshold` (roadsign = 100, `texdb.cpp:179`). No explicit `ALPHATESTENABLE` write. Draw order is the game's opaque / fading-building order (`envmap.cpp:195-236`). | PS2/Xbox callbacks unchanged (`src/render/buildingPipe.cpp:1252`, `:1650`); PBR now also passes through `D3D9RenderDual` (`:2391-2399`). Switch entry forces alpha test on (`pipelinecommon.cpp:1545` / `buildingPipe.cpp:589`). | `INTENTIONAL` (PBR dual-pass parity; state repair), with `REGRESSION` risk from the forced ALPHATESTENABLE/ref (A3/A4). | Restore the pre-entry alpha-test enable at exit (A3). Otherwise the decal path matches legacy. |
| D2 | `ZWRITE` for decals = only the dual-pass legs (leg1 TRUE / leg2 FALSE) (`backup_original/src/main.cpp:239-244`); no per-mesh decal policy; the callback does not force ZWRITE. | Switch forces ZWRITE TRUE for the whole cb (`src/render/buildingPipe.cpp:2514-2517`); PBR adds a per-mesh DECAL policy: material-translucent non-FADED meshes get ZWRITE FALSE, next opaque mesh gets TRUE (`src/render/buildingPipe.cpp:2108-2210`). | `INTENTIONAL*` (documented, user m0452) — new policy; PS2/Xbox do not use it, so legacy pipes are unaffected. | Keep for PBR (or gate `bldDecalDepth`, default 1). Do not port it to PS2/Xbox. |
| D3 | `ZTEST` never touched; decals inherit the world pass (TRUE). | Building Switch does not touch ZTEST; PBR decal policy explicitly keeps ZTEST TRUE (`src/render/buildingPipe.cpp:2185`); default-pipe contract forces ZTEST TRUE (`src/core/main.cpp:1373`, `:1389`). | `INTENTIONAL` for building decals (parity); the default-pipe force is covered by D/P8. | None for decals. |
| D4 | Draw order = game lists/sorting; fading buildings drawn by `CVisibilityPlugins::RenderFadingBuildings` after opaque (`backup_original/src/envmap.cpp:195-236`). No SkyGfx sorting. | Same ordering. PBR adds a second classify draw after the main draw with ZWRITE off (`src/render/buildingPipe.cpp:2401-2414`) — non-visual. | `INTENTIONAL` | Keep. |
| D5 | `TagRenderCB` condition is `config->tagsBuildingPipe == BUILDING_PS2`; ONE/ZERO blend; ref = `255 - 240*alpha/255`; restores src/dst/ref (`backup_original/src/buildingPipe.cpp:146-163`). Roads sign TexInfo threshold 100 (`texdb.cpp:175-180`). | `TagRenderCB` identical but keys off `config->buildingPipe` (`src/render/buildingPipe.cpp:320-337`); `tagsBuildingPipe` is aliased to `buildingPipe` on load (`src/core/main.cpp:5332-5333`) and its menu entry is commented (`src/core/debugmenu_ui.cpp:235-236`); roadsign override preserved (`src/core/main.cpp:2668-2675`). | `INTENTIONAL` (alias) — divergence only if a debug menu/preset writes `tagsBuildingPipe`. | Use `config->tagsBuildingPipe` as legacy (one-line), or delete the field/menu remnants. |
| D6 | Grunge: no special case — rides texture/material alpha like any vehicle/building mesh. | Vehicle glass classifier explicitly rejects grunge/paint surfaces (`src/render/vehiclePipe.cpp:2288-2296`), and PBR classifies texture alpha. | `INTENTIONAL` (ghost-car chrome fix) | Keep; this is a new-pipe feature, not a legacy behaviour to restore. |

### 2.5 Render-state save/restore

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| S1 | **Building PS2** saves `alpharef, alphafunc, src, dst, fog, zwrite` through the rw cache (`backup_original/src/buildingPipe.cpp:218-227`), restores `alpharef/func` at the end (`:357-358`) and `src/dst/fog/zwrite` after the env pass (`:350-354`). Xbox/Sphere save nothing (TagRenderCB and `D3D9RenderDual` own their narrow bits). | `BuildingRenderState` saves the same six rw states **plus** `TEXTUREADDRESS` and the per-stage sampler address (L3) (`src/render/buildingPipe.cpp:394-446`); every callback pairs save/restore (`:1144/1357`, `:1527/1667`, `:1694/1761`, `:2099/2419`). | `INTENTIONAL` (texture-address leak plug; audit item 2) | Keep. |
| S2 | **Vehicle** saves `alphafunc, src, dst, fog` (and `fogenable` in mobile/env) (`backup_original/src/vehiclePipe.cpp:419-425`, `:604-608`, `:997-1000`, `:1305-1308`), restores at cb end; FX passes save/restore their own blend/alpha/zwrite (`:526-540`, `:697-708`, `:1060-1076`). No `ALPHABLENDENABLE`/`ALPHATESTENABLE`/`ZENABLE`/`ZFUNC`/cull. | Same rw list + texture address/samplers; Switch adds cull/alpha/zwrite three-layer guards (`src/render/vehiclePipe.cpp:3156-3301`); FX passes mirror states to L2 (`:690-736`, `:1772-1812`). | `INTENTIONAL` (state repair) | Keep, subject to A1-A3 fidelity. |
| S3 | **Ped** saves only `alphatest/alpharef` in the dual path; ZWRITE restored to TRUE (`backup_original/src/main.cpp:426-436`). Nothing else. | Full ped entry/exit contract: cull, alpha/blend, ZWRITE, ped opaque (`src/core/main.cpp:1403-1537`, `:1762-1798`). | `INTENTIONAL*` (documented) — see P3/P4 for the parity breaks. | Narrow as P3/P4. |
| S4 | **Neo car** reflection-mask overlay saves `zwrite, fogenable, src, dst, alphatest` (`backup_original/src/neoCarpipe.cpp:381-423`). | Equivalent guard extended to 3 layers (`src/render/neoCarpipe.cpp:216-258`, `:597-681`, `:750-754`). | `INTENTIONAL` | Keep. |
| S5 | Legacy restores were **rw-cache only** and several saved locals were uninitialised (`backup_original/src/main.cpp:412-427`, `backup_original/src/buildingPipe.cpp:220-227`); a failed `RwRenderStateGet` could push garbage back. | Pre-seeded every saved value; L1+L2+L3 restore; park-and-set guarantees real transitions (`src/render/pipelinecommon.cpp:1184-1339`, `:1437-1704`). | `INTENTIONAL` (robustness fix) | Keep. This is the part of the three-layer model legacy *wanted* but lacked. |
| S6 | No per-frame/postfx raw-state save; postfx left `D3DRS_*` writes behind (`backup_original/src/postfx.cpp:317-334`, `:449-488`, `:1574-1626`). | `SaveRawGeomStates` / `RestoreRawGeomStates` around postfx passes (`src/render/postfx.cpp:731/798`, `:873/928`, `:2514/2533`, `:3310/3326`, …). | `INTENTIONAL` (fix for the legacy leak) | Keep; this is the root cause the three-layer model exists to contain. |
| S7 | Default-pipe callbacks never forced state. | `if(pipe == NULL)` forces the full canonical world signature and explicitly does **not** save/restore it (`src/core/main.cpp:1346-1400`, comment `:1364-1367`). | `REGRESSION` | See P8/#3: keep only the ZWRITE/ZTEST repair (or gate the whole contract behind `defaultPipeContract` default 0). |

### 2.6 Corona / point-light sprites

| # | Legacy behaviour (file:line) | Current behaviour (file:line) | Verdict | Recommended restoration (minimal) |
|---|---|---|---|---|
| X1 | Env-map coronas: `SRC=ONE, DEST=ONE`, `VERTEXALPHA=TRUE`, bind `gpCoronaTexture[0]`, draw Im2D; no Z writes; restores SRC/DEST/VERTEXALPHA (`backup_original/src/envmap.cpp:318-342`). | Same, plus NULL guards, `__try/__finally` and `TEXTURERASTER` restore (`src/render/envmap.cpp:471-509`). | `INTENTIONAL` (robustness; values identical) | Keep. |
| X2 | Point-light sprites: only the fog-colour hook `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect` (`backup_original/src/main.cpp:795-814`, hooked `:2146`). Additive state comes from the exe (`CCoronas 0x6FAEF8`, `CPointLights 0x7002E7`, `CClouds 0x71396B`). `coronaZtest` INI patches `0x6FB17C` (`main.cpp:330-344`). | Same colour hook plus `coronaDrawRawShield()` immediately before the draw (`src/core/main.cpp:2441-2456`); same `coronaZtest` patch (`src/core/main.cpp:334-348`). | `INTENTIONAL*` — new shield; `coronaZtest` is parity. | Gate the shield (X3); keep the `coronaZtest` patch. |
| X3 | No SkyGfx corona state management; whatever additive block the exe producers set is what ran. | `coronaDrawRawShield()` forces, per draw: `ALPHABLENDENABLE=TRUE`, `SRCBLEND=SRCALPHA`, `DESTBLEND=ONE`, `ZWRITE=FALSE`, `ALPHAFUNC=GE`, `ALPHAREF=0`, `SEPARATEALPHABLENDENABLE=0`, `COLORWRITE=0xF`, `BLENDOP=ADD` (`src/core/main.cpp:2368-2432`); called from the sprite wrapper (`:2455`) and **unconditionally for every non-empty sprite-batch flush** (`:2526-2542`); window close in the flush hook forces canonical normal blend (`:2550-2570`), while frame-boundary `CoronaWindow_Close()` restores the captured pre-window func/ref/blend (`:2644-2665`). | `INTENTIONAL*` — documented black-quad fix (`docs/plans/2026-10-24-corona-blackquad-diagnosis.md`), but legacy never touched any of these states; the unconditional flush force applies to cloud/reflection/point-light batches too, and the flush-close force can override a producer's own blend. | Add `int coronaStateShield` (INI default 0). Gate the body at `src/core/main.cpp:2370`: `if(!config->coronaStateShield) return;`, and change `:2541` to `if(g_inCoronasRender) coronaDrawRawShield(...)`. Or keep the shield but delete the ALPHAFUNC/ALPHAREF/SEPARATEALPHA/COLORWRITE force (legacy had none; the black-quad mechanism is the blend/ZW pair). |
| X4 | Corona Z-test: INI patch only. | Same (`src/core/main.cpp:334-348`). | `INTENTIONAL` (parity) | Keep. |

---

## Section 3 — Top 10 restorations ranked by likely visual impact

Ranking assumes the default preset (`pipeline=PBR`, `pedOpaqueContract=1`, `texAlphaClassify=1`, `stateBaselineEnable=1`) and a third-person daylight/urban scene.

### 1. Narrow the ped opaque contract (peds: blending, alpha test, Z state)
**File/function:** `src/core/main.cpp :: pedOpaqueEnforce()` (`:974-1003`), gate at `:1453` and default at `:5424`.

**Change:** replace the forced block with an alpha-ref repair only:

```cpp
static void
pedOpaqueEnforce(void)
{
    // Legacy ped path never forced blend/Z state. Only repair the two
    // alpha-ref degenerates (envmap's 140 world-pass leak and ref==0).
    int ref = 1;
    RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &ref);
    if(ref == 0 || ref == 140){
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)1);
        RwD3D9SetRenderState(D3DRS_ALPHAREF, 1);
        if(d3d9device)
            d3d9device->SetRenderState(D3DRS_ALPHAREF, 1);
    }
}
```

Then set the INI default at `:5424` and the `ADD_IF_MISSING` default at `:5936` to `0` (or rename to `pedAlphaRefRepair`, default 1). Do **not** force `ALPHABLENDENABLE`, `VERTEXALPHAENABLE`, `ZWRITE`, `ZTEST`, `ZENABLE` or `ZFUNC`.

**Why first:** every ped draw currently starts with blend off + vertex alpha off + GE@1; legacy let the skin pipeline derive all of those per mesh. This is the subsystem the user reports as regressing, and the fix is small and reversible.

**Note:** even with `pedOpaqueContract=0`, `pipeEnterAlphaMode()` + `pipeForceAlphaBlock()` still force `ALPHABLENDENABLE=FALSE` for the ped span (`src/core/main.cpp:1470`, `:1493`). To get true legacy parity, skip `pipeEnterAlphaMode`/`pipeForceAlphaBlock` for `isPedPipe` as well, or add a `pedAlphaContract` gate around them.

### 2. Restore legacy fallback for unclassified textures (PBR)
**File/function:** `src/render/vehiclePipe.cpp :: CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env()` `switch(tac)` default (`:2314-2319`); `src/render/buildingPipe.cpp :: CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR()` `switch(tac)` default (`:2162-2164`).

**Change (vehicle):**

```cpp
case TAC_UNKNOWN:   // legacy fallback: let RW's texture-alpha/material rule decide
default:
    blendOn = hasAlpha ? 1 : 0;
    shaderAlpha = hasAlpha ? 0.0f : 1.0f;
    break;
```

**Change (building):** same, using the already-computed `hasAlpha` (`:2138-2142`, which honours `vertexAlphaIsAlpha`).

**Why:** `TAC_UNKNOWN` is returned when the CPU scan cannot extract the D3D texture (budget skip, non-scannable format, stale ext pointer). Legacy never had this failure mode: `D3D9RenderDual` read `D3DRS_ALPHABLENDENABLE`, which RW had already set from the texture/material (`backup_original/src/main.cpp:224-225`). Today an unclassified glass/grunge/decoration texture renders opaque (and skips the dual pass). This is a high-impact, one-line-per-file fix.

### 3. Remove the forced blend/alpha-test block from the default-pipe callback
**File/function:** `src/core/main.cpp :: myDefaultCallback()`, `if(pipe == NULL)` block (`:1346-1391`).

**Change:** delete the blend/alpha-test writes; keep the depth repair:

```cpp
// Keep: ZWRITE / ZTEST repair (the actual reported leak)
RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
if(d3d9device){
    d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
}
// Remove: VERTEXALPHAENABLE=FALSE, SRC/DEST, ALPHABLENDENABLE=FALSE,
//         BLENDOP, ALPHATESTENABLE=TRUE (lines 1369-1371, 1375-1378,
//         1381, 1384-1387, 1390)
```

**Why:** props/pickups/marker objects (default pipeline) are often alpha-blended (pickup glow, marker cones, breakable fences). Legacy left their blend/alpha-test to RW. The block has no save/restore by design (`:1364-1367`), so it also permanently changes the state for every later default atomic. This likely explains "props/pickups look wrong/opaque" reports.

### 4. Gate the ped `pAmbient` override (default off)
**File/function:** `src/core/main.cpp :: myDefaultCallback()`, block at `:1587-1662`; parse at `:5485`.

**Change:** add to `Config` (`src/skygfx.h`, next to `pedAmbientFloor`):

```cpp
int pedAmbientOverride;   // 1 = timecycle ambient override + floor (new); 0 = game pAmbient (legacy)
```

INI parse (near `:5485`):

```cpp
c->pedAmbientOverride = readint(cfg.get("SkyGfx", "pedAmbientOverride", ""), 0);
if(c->pedAmbientOverride < 0) c->pedAmbientOverride = 0;
if(c->pedAmbientOverride > 1) c->pedAmbientOverride = 1;
```

Then change the gate:

```cpp
if(pipe == skinPipe && pAmbient && config->pedAmbientOverride){
```

Add `ADD_IF_MISSING("SkyGfx", "pedAmbientOverride", "0");` and the save line.

**Why:** legacy draws peds with the game's `pAmbient` (interior/cutscene/weather-correct). The current max-with-timecycle + 0.20 floor is a global lighting change, not a state repair, and is a likely cause of "peds too bright/flat" or cutscene mismatch.

### 5. Stop forcing ZWRITE TRUE for every ped atomic
**File/function:** `src/core/main.cpp :: myDefaultCallback()`, ped entry ZWRITE block (`:1475-1488`) and sticky exit (`:1776-1788`).

**Change:** move the force into the dual-pass branch only:

```cpp
// in the ped entry block, replace the unconditional 3-layer TRUE push with:
if(dodual && isPedPipe){
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    if(d3d9device) d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
}
```

and set `zwriteWasDesync = false` when that force did not run; at exit, restore the captured layers instead of the sticky TRUE rewrite.

**Why:** legacy forced ZWRITE only inside the skin dual pass (`backup_original/src/main.cpp:425`). Transparent/fading ped draws (where the game had ZWRITE off) now write depth and occlude what is behind them.

### 6. Make `pipeExitAlphaMode` restore the real pre-entry blend/test/BLENDOP
**File/function:** `src/skygfx.h :: struct PipeAlphaState` (`:1339-1357`); `src/render/pipelinecommon.cpp :: pipeEnterAlphaMode()` (`:1437-1624`) / `pipeExitAlphaMode()` (`:1626-1704`).

**Change:**

1. Add fields:

```cpp
RwUInt32 blendEnable;      // D3DRS_ALPHABLENDENABLE  (L2 + L3)
RwUInt32 alphaTestEnable;  // D3DRS_ALPHATESTENABLE   (L2 + L3)
RwUInt32 blendOp;          // D3DRS_BLENDOP           (L2 + L3, raw-only)
```

2. Capture in `pipeEnterAlphaMode` (alongside the existing `RwD3D9GetRenderState` calls at `:1474-1479`):

```cpp
RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s.blendEnable);
RwD3D9GetRenderState(D3DRS_ALPHATESTENABLE,  &s.alphaTestEnable);
RwD3D9GetRenderState(D3DRS_BLENDOP,          &s.blendOp);
if(d3d9device){
    d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.blendEnable);
    d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE,  &s.alphaTestEnable);
    d3d9device->GetRenderState(D3DRS_BLENDOP,          &s.blendOp);
}
```

3. In `pipeExitAlphaMode`, replace `devEnable = saved.vtxAlpha ? TRUE : FALSE` with `saved.blendEnable`, replace the hard `D3DRS_ALPHATESTENABLE, TRUE` (two sites) with `saved.alphaTestEnable`, and replace the hard `D3DRS_BLENDOP, D3DBLENDOP_ADD` with `saved.blendOp`.

**Why:** legacy never changed `ALPHABLENDENABLE`/`ALPHATESTENABLE`/`BLENDOP` at all. The current exit can hand the next consumer blend-off and alpha-test-on even when the pipe found blend-on/test-off. This is the generic state leak behind several "black/ghost sprite after world" symptoms.

### 7. Remove the unconditional BACK cull after the IBL pass
**File/function:** `src/render/buildingPipe.cpp :: CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR()` (`:1876-1880`); `src/render/vehiclePipe.cpp :: CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env()` (`:2080-2088`).

**Change:** delete both `pipeForceCullMode(rwCULLMODECULLBACK);` calls. If a re-assert is required after the Im2D/IBL pass, re-assert the *current* rw value:

```cpp
RwUInt32 cull = rwCULLMODECULLBACK;
RwRenderStateGet(rwRENDERSTATECULLMODE, (void*)&cull);
pipeForceCullMode(cull);
```

**Why:** the Switch already forced the saved mode through L1+L2+L3 before the callback (`pipeEnterCullMode`). The inner BACK call is the only remaining place where a span that legitimately entered with CULLNONE (mirror) or CULLFRONT (ped shadow volumes) gets flipped to BACK mid-callback. Legacy never set cull in any building/vehicle callback.

### 8. Gate the corona raw shield (and stop forcing it on every sprite flush)
**File/function:** `src/core/main.cpp :: coronaDrawRawShield()` (`:2368-2432`), sprite flush hook (`:2489-2570`), wrapper (`:2441-2456`).

**Change (minimal, two edits):**

1. `:2541` — only force inside a real corona/point-light window:

```cpp
if(g_inCoronasRender)
    coronaDrawRawShield("SpriteFlush_70CF20", true);
```

2. In `coronaDrawRawShield`, after the device guard (`:2370`), gate the whole body behind a config default off:

```cpp
if(!config || config->coronaStateShield == 0)
    return;
```

Add `int coronaStateShield;` to `Config`, parse default `0`, `ADD_IF_MISSING(..., "0")`.

**Why:** legacy had no corona state code beyond `coronaZtest`. The current shield forces additive blending + ZWRITE off on every non-empty sprite-batch flush, including cloud/reflection/point-light batches whose producers may not want additive blending; it also overrides `ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/COLORWRITEENABLE`, none of which legacy touched.

### 9. Restore the legacy ambient source (raw timecycle for buildings, `pAmbient` for vehicles/peds)
**File/function:** `src/render/pipelinecommon.cpp :: ClampAmbientToSky()` / `GetTimecycleAmbient()` / `GetTimecycleAmbientPBR()` (`:97-213`); `src/render/vehiclePipe.cpp :: uploadLights()` (`:533-556`); `src/render/buildingPipe.cpp` (`:176-185`).

**Change:** add `int legacyAmbient;` default 1, then:

```cpp
// GetTimecycleAmbient(), after building `out`:
if(!config || !config->legacyAmbient)
    ClampAmbientToSky(out);
```

```cpp
// GetTimecycleAmbientPBR():
if(config && config->pbrAmbientFloor > 0.0f && !(config && config->legacyAmbient)) { ... }
```

```cpp
// vehicle uploadLights():
if(config && config->legacyAmbient)
    pipeUploadLightColor(pAmbient, REG_ambient);
else
    RwD3D9SetVertexShaderConstant(REG_ambient, &tcAmbient, 1);
```

**Why:** legacy building ambient was `CTimeCycle_GetAmbient* × CCoronas__LightsMult` (`backup_original/src/buildingPipe.cpp:56-59`); legacy vehicles upload `pAmbient->color` (`backup_original/src/vehiclePipe.cpp:346-348`). The current night law/floor/shelter dampening is a new global lighting law; without a switch there is no way to return to the old look.

### 10. Restore the legacy default-pipe dual-pass gate
**File/function:** `src/core/main.cpp :: myDefaultCallback()`, block `:1678-1687`.

**Change:** delete the extra gate for the default branch:

```cpp
// DELETE this whole block for the default pipe:
if(dodual && !isPedPipe){
    int zwriteNow = TRUE;
    bool hasAlpha = atomicHasAlpha(atomic, false);
    ...
    if(!zwriteNow || !hasAlpha) dodual = 0;
}
```

Keep `atomicHasAlpha()` for the diagnostics at `:1742-1743` (and for any future per-pipe use). The legacy gate is already at `:1348-1350` (`zwrite && dualPassDefault`).

**Why:** legacy ran the two-leg pass for every default-pipe atomic whose inherited ZWRITE was on, without an alpha probe (`backup_original/src/main.cpp:416-421`). Props with texture cutouts (fences, foliage cards, pickup glows) get only one leg now, so their below-threshold fringe is not painted.

**Honourable mentions (below the top 10):**

- `TagRenderCB`: use `config->tagsBuildingPipe` instead of `config->buildingPipe` (`src/render/buildingPipe.cpp:325`; legacy `backup_original/src/buildingPipe.cpp:151`). One line.
- Grass cull: restore the saved mode instead of always BACK (`src/core/main.cpp:806-807`; legacy `backup_original/src/main.cpp:402`).
- `src/skygfx.h:1303-1318` documents "clamped: only NONE/BACK survive, forces CULLBACK" — stale after the cull fix; update to "saved value; NONE/BACK/FRONT preserved".
- `src/core/main.cpp:1343-1344` says default atomics "must keep their current (unguarded) behaviour", but `:1368-1391` guards them. Fix the comment or the code.
- `src/render/vehiclePipe.cpp:2271` says the classifier budget is 8/frame; `texalpha.cpp:52` is 32. Doc drift.

---

## Section 4 — Things the current code does that legacy never did

Candidates for removal or a default-off guard. Ordered roughly by risk.

| # | New behaviour (file:line) | Legacy equivalent | Recommendation |
|---|---|---|---|
| 1 | **Three-layer per-pipe state contract** — `pipeEnter/ExitCullMode`, `pipeEnter/ExitAlphaMode`, `pipeForceCullMode`, `pipeForceAlphaBlock`, park-and-set, raw L3 writes (`src/render/pipelinecommon.cpp:1184-1704`; `src/render/buildingPipe.cpp:460-704`). | None. Legacy saved/restored a narrow rw-cache list per callback (`backup_original/src/buildingPipe.cpp:218-227`; `vehiclePipe.cpp:419-425`; `main.cpp:426-436`). | Keep the *repair* intent, but fix the unfaithful restores (A1-A3). This is the part that must exist because postfx now raw-writes; it is not itself a legacy behaviour. |
| 2 | **Frame-boundary world baseline** — pins blend OFF, SRCALPHA/INVSRCALPHA, ADD, ZWRITE/ZTEST TRUE, cull BACK, ALPHATESTENABLE TRUE, COLORWRITE 0xF (`src/render/pipelinecommon.cpp:1731-1804`; call sites `src/core/main.cpp:2888`, `:3387`, `:3416`; `src/render/depthhook.cpp:389`). | None. Legacy never pinned a frame-global state. | Remove cull/alpha-test from the baseline (keep only the depth/blend-op normalisation if needed), or default `stateBaselineEnable=0`. |
| 3 | **Ped opaque contract** — blend/vertex-alpha OFF, ZWRITE/ZTEST/ZFUNC/ALPHAREF forced (`src/core/main.cpp:891-1014`, `:1517-1536`). | None. | Narrow to the alpha-ref repair (Section 3 #1), default off. |
| 4 | **`pAmbient` override + `pedAmbientFloor`** (`src/core/main.cpp:1582-1662`; `src/skygfx.h:713-714`). | None (`pAmbient` used as-is). | Default-off gate (Section 3 #4). |
| 5 | **Corona/point-light raw shield + sprite-flush force + flush-window canonical resync** (`src/core/main.cpp:2368-2432`, `:2509-2570`). | Only the point-light fog-colour hook and the `coronaZtest` exe patch (`backup_original/src/main.cpp:795-814`, `:2146`, `:330-344`). | Default-off config; restrict the force to the corona/point-light windows (Section 3 #8). |
| 6 | **Shadow render-state contract** — `ShadowContractState` wrapper around `CShadows::Render*` (`src/core/main.cpp:1052-1258`, call sites in `CScene::Render`). | None. | Keep (independent subsystem, no legacy analogue; documented black-blob fix). Not a parity target. |
| 7 | **Texture-alpha classifier + c48.w shader alpha override + per-mesh L2 blend enable** (`src/render/texalpha.cpp`, used `src/render/buildingPipe.cpp:2144`, `:2179`; `src/render/vehiclePipe.cpp:2276`, `:2336`, `:2384`). | RW/exe texture-alpha detection + `TexInfo::dualPass`/`zwriteThreshold` (`backup_original/src/main.cpp:224-225`; `backup_original/src/texdb.cpp:171-180`). | Keep for PBR (fixes unused-alpha DXT3 holes), but restore the `TAC_UNKNOWN` fallback (#2 in Section 3). |
| 8 | **PBR per-mesh decal ZWRITE policy** (`src/render/buildingPipe.cpp:2108-2210`). | Only `D3D9RenderDual` legs (`backup_original/src/main.cpp:239-244`). | Keep (PBR only, documented); do not port to PS2/Xbox. |
| 9 | **Building ambient night law + absolute floor + ambientMult + interior IBL suppression** (`src/render/pipelinecommon.cpp:97-213`; `src/render/buildingPipe.cpp:160-186`; `src/core/main.cpp:5423`, `:5485`). | `timecycle * LightsMult` only; lightning override preserved (`backup_original/src/buildingPipe.cpp:56-62`). | Default-off `legacyAmbient` (Section 3 #9). |
| 10 | **Default-pipe entry contract with no save/restore** (`src/core/main.cpp:1346-1391`). | None. | Remove the blend/alpha-test half; keep depth repair (Section 3 #3). |
| 11 | **Default-callback shader hygiene** — unbind VS/PS for every default atomic (`src/core/main.cpp:1539-1567`). | None. | Keep (cheap insurance; no draw change when state is correct). |
| 12 | **`effectsForceAlphaBlock()` at effects/postfx exits** (`src/core/main.cpp:3101-...`, called `:3376`, `:3381`, `:3411`, `:3502`, `:3512`). | None. | Keep (it is what closes postfx's raw leaks), but it is a new state writer on every effects exit. |
| 13 | **Mirror composite override** (`src/render/mirrorPass.cpp`, W7-C) — replaces the vanilla blit with `Mirror_Composite.ps` and pushes ONE/ZERO + ZTEST/ZWRITE FALSE. | The game's own mirror blit (no SkyGfx hook). | Keep (new feature), but it is not legacy; make sure its state restore does not fight the three-layer model (it currently restores raw geom states, `mirrorPass.cpp:174-240`). |
| 14 | **PBR IBL capture + `PipeChain` classify second draw + Forward+** (`src/render/pipelinecommon.cpp:1107-1180`; `src/render/buildingPipe.cpp:2401-2414`). | None. | Keep (new render pipeline); out of parity scope. |
| 15 | **Vehicle interior/shelter ambient dampening** (`src/render/vehiclePipe.cpp:544-556`, 0.55× when `CCullZones::PlayerNoRain()` and outdoors). | None. | Keep only under a new feature flag; it is a lighting change, not a repair. |
| 16 | **`hasAlpha` excludes the `alpha==16` paint marker** (`src/render/vehiclePipe.cpp:2263-2264`, building `:2134-2135`). | Legacy included it (`instancedData->material->color.alpha != 255`), which made paint-marker meshes blend. | Keep (documented fix for invisible paint meshes); note it is a deliberate legacy divergence. |
| 17 | **`SetLightsWithTimeOfDayColour` 1.5× mobile block is live for `PIPELINE_MOBILE`** (`src/core/main.cpp:1974-1986`); legacy returned before it (`backup_original/src/main.cpp:536-537`). | Dead code in legacy. | The function is not hooked in either build (`0x53E997` interception is commented), so it is latent only. If it is ever hooked, gate it. |
| 18 | **`tagsBuildingPipe` collapsed into `buildingPipe`** (`src/core/main.cpp:5332-5333`) and `TagRenderCB` uses `buildingPipe` (`src/render/buildingPipe.cpp:325`). | Separate config field, honoured by `TagRenderCB` (`backup_original/src/config.cpp:286`; `backup_original/src/buildingPipe.cpp:151`). | Keep the alias (simpler) or restore the legacy field use; either way remove the commented menu remnant. |

---

## Appendix — Evidence notes / how to verify

- **Legacy has no three-layer model:** `backup_original/src/pipelinecommon.cpp` contains only matrix/light helpers and shader creation; no `pipeEnter*`/`pipeExit*`/`pipeForce*` functions exist. `grep -n "pipeEnter\|pipeExit\|pipeForce" backup_original/src` returns nothing. The entire state contract is new code.
- **Legacy never wrote cull in a pipe:** `grep -rn "CULL" backup_original/src` yields only `main.cpp:372-374/402` (grass), `envmap.cpp:209/232` (world pass), `postfx.cpp:319/1576` (Im2D). No building/vehicle/ped callback.
- **Legacy never wrote the D3D alpha/z enable family in a pipe:** `grep -rn "D3DRS_ALPHABLENDENABLE\|D3DRS_ALPHATESTENABLE\|D3DRS_ALPHAFUNC\|D3DRS_ALPHAREF\|D3DRS_ZENABLE\|D3DRS_ZFUNC\|D3DRS_COLORWRITEENABLE" backup_original/src` matches only `main.cpp:225` (a Get), `main.cpp:857-863` (moon separate-alpha) and `postfx.cpp`. The pipe doctrine is: rw `VERTEXALPHAENABLE` + rw `ALPHATESTFUNCTION/REF` + rw `ZWRITE` only.
- **Ped dual pass is now parity:** `src/core/main.cpp:1664-1755` is a direct transcription of `backup_original/src/main.cpp:408-440`, including the `zwriteThresholdPed > 0` gate (`:1400`) and the skin-only `hasAlpha` exemption (`:1678-1687`). The only remaining ped divergence in that function is the extra `!isPedPipe` alpha gate (P8) and the `254` clamp (P2).
- **The 140 ALPHAREF leak is already fixed in the current tree:** `src/render/envmap.cpp:349-375` saves/restores the ref around `RenderRoadsAndBuildings`; legacy set it and never restored (`backup_original/src/envmap.cpp:210-211`). So the ped contract's justification "envmap leaks 140 into peds" no longer holds on this build.
- **Suggested log checks before/after each restoration:** `[PedDual] legacy dual pass restored`, `[PED] dual pass skipped`, `[PedOpaque] contract repaired divergent entry`, `[PipeAlpha] entry resync`, `[Pipeline] pipeExitAlphaMode`, `[Cull] enter/exit`, `[CoronaDraw] pre-force`, `[BUILDING] blend_leak`.
- **Test matrix for parity:** (a) daylight third-person CJ close-up + fade (cutscene), (b) night with street lights on (coronas/point lights), (c) docks/beach outdoor `ref=140` path, (d) mirror (reflection) and stencil-shadow passes, (e) transparent building (fading) + road decals/skidmarks, (f) vehicle glass/paint/grunge, (g) prop/pickup/marker close-up. Compare legacy INI (`pipeline=PS2`, `dualPassPed=1`, `pedOpaqueContract` absent) against current PBR with each restored item toggled.

**Bottom line:** the current tree already contains the two most important parity fixes of this audit window — the ped dual pass was restored to the legacy sequence, and per-pipe cull now preserves NONE/FRONT instead of forcing BACK. What still diverges from legacy in a way that can change pixels is concentrated in four places: the **ped opaque/blend contract**, the **default-pipe contract**, the **TAC_UNKNOWN=OPAQUE fallback**, and the **exit-time blend/alpha-test fidelity** (plus the two inner BACK re-asserts and the corona shield). Those are Section 3 items 1-8.
