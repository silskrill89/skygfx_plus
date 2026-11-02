# Render-State Leak Audit — every write with no matching restore, ranked by blast radius

**Status:** investigation complete, READ-ONLY (no source edited by this audit)
**Date:** 2026-10-25 (snapshot taken 2026-10-26 00:20 GMTST)
**Scope:** `src/**/*.{cpp,h}` — every raw `IDirect3DDevice9::Set*` write and every `RwRenderStateSet` / `RwD3D9SetRenderState` write, with the restore (or absence of one) on **every** exit path.
**Tooling:** `tools/layering_check.py` (index) + manual read of every site; the parser was extended locally to attach each site to its enclosing function (not committed).
**HEAD:** `898e894` + working-tree modifications.

> **Snapshot caveat.** The working tree was being edited *while this audit ran* (mtimes moved
> repeatedly; two fixes landed mid-audit — see §3.5 and §3.6). Numbers below are a snapshot:
> `layering_check` reported **706 raw sites at session start and 710 at the end**; the Rw-setter
> count moved from 1,351 to 1,203. **Function names are the stable anchor; line numbers may
> drift by a few lines.** Re-grep the cited symbol before applying any fix.

---

## 0. Executive answer

The recurring layer-drift logs are **not** primarily a CULL problem. CULLMODE is the
best-covered invisible-class state in the tree (every writer except the debug overlay goes
through `pipeForceCullMode` / `pipeEnterCullMode` + a captured restore). The drift is produced
by four structural gaps:

1. **Stale captures replayed forever.** `g_coronaPreValid` is set once and never cleared, so
   `CoronaWindow_Close()` re-applies the *first-ever* corona window's ALPHABLEND/ALPHAFUNC/
   ALPHAREF at **every** `RenderScene_before` — **after** `pipeApplyWorldBaseline`. The world
   baseline is overwritten every frame by a stale snapshot. This alone explains a recurring
   "entry blend/alpha drift repaired" cadence. (Finding #1.)
2. **The two shared save/restore helpers do not cover the states the passes actually write.**
   `CPostEffects::ImmediateModeRenderStatesReStore()` covers 10 rw IDs; `SaveRawGeomStates()`
   covers 9 raw states. Neither covers **ALPHAFUNC, ALPHAREF, ZFUNC, COLORWRITEENABLE,
   SEPARATEALPHABLENDENABLE, the three `*ALPHA` blend factors, FOGCOLOR, STENCILENABLE, or
   `TEXTURERASTER`/sampler state for any stage**. Every pass that writes one of those must
   hand-roll the restore; the ones that don't are the leaks in §2.
3. **`TEXTURERASTER` is the single most-leaked state.** The game's ReStore deliberately omits it
   (the Luminance pass comments say so), so every postfx pass that binds s0 through
   `RwRenderStateSet(rwRENDERSTATETEXTURERASTER, …)` and does not explicitly clear/capture it
   hands its working buffer to the next draw. SSAO hands over the **AO multiply buffer**, SMAA
   the **edge map**, PipeChain the **front buffer**, SSS the **blur buffer**.
4. **Camera-raster swaps are not SEH-fenced.** The Radiosity family swaps
   `RwCameraSetRaster(Scene.camera, workBuffer)` and restores only on the straight-line path.
   A fault (or any future early return) leaves the whole rest of the frame rendering into an
   off-screen work buffer — the documented "screen never receives the world" class.

Two paths are already **exemplary** and should be the template for the rest:
`mirrorPass.cpp` composite (`__finally`, per-layer captured park-and-set, PS constants,
override save/restore) and `envmap.cpp` `DrawEnvMapCoronas` (fixed mid-audit, §3.5).

---

## 1. The layer model, and what "restored" means here

| Layer | Owner | Write API | Cache-hit behaviour |
|---|---|---|---|
| **L1** | rw render-state cache | `RwRenderStateSet` / `RwRenderStateGet` | value-compare; forwards to L2 on change |
| **L2** | RW D3D9 driver `pending[]`/`applied[]` | `RwD3D9SetRenderState` | `applied[]`-gated flush; a cache-hit is a **no-op** |
| **L3** | raw `IDirect3DDevice9` | `dev->SetRenderState` | none — authoritative |

Consequences used throughout this audit:

* A **raw-only** write desyncs L3 from L1/L2 permanently. A later `RwRenderStateSet` of the
  value L1 already holds is a double no-op and can never repair L3.
* A **L1-only** restore cannot repair a device that drifted behind an unchanged cache.
* The only correct restore is **park-and-set per layer** (`pipeRwParkSet` → `pipeL2ParkSet` →
  `pipeL3Set`, or the `RestoreOneRawGeomState` / `envParkSetState3L` / `MirrorRestoreState3L`
  idiom): write a guaranteed-different value first, then the captured value, then pin L3 raw.
* **Hardcoded canonical** values are not restores. They are repairs. They are acceptable at a
  pipe *entry* contract; they are a leak at an *exit* if the pre-entry value differed.

### 1.1 Coverage of the two shared helpers

| State | `ImmediateModeRenderStatesReStore` (10) | `SaveRawGeomStates` (9) | Who writes it and must hand-roll |
|---|---|---|---|
| SRCBLEND / DESTBLEND | ✅ | ✅ | — |
| ALPHABLENDENABLE / VERTEXALPHAENABLE | ✅ | ✅ | — |
| ZTESTENABLE / ZWRITEENABLE | ✅ | ✅ | — |
| CULLMODE | ✅ | ✅ | — |
| FOGENABLE | ✅ | ❌ | CClouds, water, waterdrops, Radiosity, ColourFilter |
| SHADEMODE | ✅ | ❌ | — |
| TEXTUREADDRESS / TEXTUREFILTER | ✅ (stage 0) | ❌ | stages 1-9 are **not** covered |
| ALPHATESTENABLE | ❌ | ✅ | PS2 building, vehicle FX, `pipeForceAlphaBlock` |
| BLENDOP / BLENDFACTOR | ❌ | ✅ | — |
| **ALPHAFUNC / ALPHAREF** | ❌ | ❌ | every alpha-test pass |
| **ZFUNC** | ❌ | ❌ | CClouds, water, shadow contract |
| **COLORWRITEENABLE** | ❌ | ❌ | corona shield, `pipeForceAlphaBlock`, baseline |
| **SEPARATEALPHABLENDENABLE** | ❌ | ❌ | moon, corona shield, `pipeForceAlphaBlock` |
| **SRC/DEST/BLENDOPALPHA** | ❌ | ❌ | moon |
| **FOGCOLOR** | ❌ | ❌ | building/vehicle FX legs |
| **STENCILENABLE** | ❌ | ❌ | Forward+ debug |
| **TEXTURERASTER (all stages)** | ❌ | ❌ | every Im2D/postfx pass |
| **Sampler state (stages 1-9)** | ❌ (stage 0 only) | ❌ | SMAA, SSAO, PipeChain, water, PBR cbs |
| TEXTUREFACTOR | ❌ | ❌ | vehicle Xbox/FX |

**This table is the root class.** A pass is safe only if it either (a) touches nothing outside
the two helpers, or (b) hand-rolls a captured, per-layer restore. §2 records which is which.

### 1.2 Blast-radius scale

| Rank | Meaning |
|---|---|
| **A — invisible / whole frame** | Can blank, cull, remove depth writes, or redirect the render target for the rest of the frame / next frame. |
| **B — alpha / whole frame** | ALPHATEST / ALPHAFUNC / ALPHAREF / blend: cutout holes, black pools, additive blowout, see-through depth. |
| **C — texture / next pass** | Wrong stage texture or sampler; wrong colour on any FFP draw that does not rebind that stage. |
| **D — local / canonical** | Value is canonical, debug-only, feature-owned, or confined to one atomic. |

Frequency modifiers used in the ranking: **every frame**, **every atomic**, **fault-only**.

---

## 2. Master table — every raw site, by file and function

Legend: `RESTORED` = captured restore on all exit paths (cite); `PARTIAL` = some state/exit
missed (named); `UNRESTORED` = no restore. Sites that are deliberate *repairs* (entry
canonicalisation) are marked `REPAIR (canonical)` and are not leaks by design — they are still
listed so the list is exhaustive.

### 2.1 `src/core/main.cpp` (131 raw sites)

| file:line | state(s) | value written | restore? | blast |
|---|---|---|---|---|
| `main.cpp:1109-1118` `pedOpaqueEnforce` | ALPHABLEND, ZWRITE, ZENABLE, ZFUNC, ALPHATEST, ALPHAFUNC, ALPHAREF | canonical opaque (blend off, zw on, GE@1) | **RESTORED** `pedOpaqueRestore` 1143-1148 (ZWRITE excluded — sticky block 2079/2086) | B, ped span |
| `main.cpp:1143-1148` `pedOpaqueRestore` | same | captured dev values | (is the restore) | — |
| `main.cpp:1371-1380` `shadowContractForce` | blend, src, dst, z, zfunc, zwrite, atest, afunc, aref, fog | canonical shadow | **RESTORED** `shadowContractRestore` 1420-1453 | B, pass-local |
| `main.cpp:1420-1453` `shadowContractRestore` | same | L1 truth (heal) / L3 capture (clean) | (is the restore; see §3.2) | — |
| `main.cpp:1642-1648` `myDefaultCallback` default-pipe entry | ALPHABLEND, SRC, DST, BLENDOP, ZWRITE, ZENABLE, ALPHATEST | canonical world signature | **REPAIR (canonical)** — intentional, no save | D |
| `main.cpp:1787` ped ZWRITE force | ZWRITE=TRUE | — | **RESTORED** 2086 (or sticky 2079) | — |
| `main.cpp:1806-1807` ped alpha pair re-push | ALPHAFUNC, ALPHAREF | savedAlpha pair | **RESTORED** `pedOpaqueRestore` 1148/1147, else `pipeExitAlphaMode` | B |
| `main.cpp:1876-1877` default-cb shader hygiene | PS=NULL, VS=NULL | neutral | **UNRESTORED** (deliberate neutral; pre-entry shader lost) | D |
| `main.cpp:2792-2836` `coronaDrawRawShield` | ALPHABLEND, SRC, DST, ZWRITE, ALPHAFUNC, ALPHAREF, SEPARATEALPHA, COLORWRITE, BLENDOP | additive block + GE@0 + sepA=0 + cwr=0xF + ADD | **PARTIAL** — hook exit 4213-4224 + 4241-4244 restores 6 captured / 4 canonical; `CoronaWindow_Close` 3095-3097 restores only 3 of 9 | **A/B, every frame** |
| `main.cpp:3013-3018` `SpriteFlushIndexedDraw_hook` window close | ALPHABLEND, SRC, DST, BLENDOP, ZWRITE, ZENABLE | canonical resync | **REPAIR (canonical)** | D |
| `main.cpp:3095-3097` `CoronaWindow_Close` | ALPHAFUNC, ALPHAREF, ALPHABLEND | **stale** first-window capture | **PARTIAL + STALE** (6 of 9 shield states missing; capture never invalidated) | **A/B, every frame** |
| `main.cpp:4089-4149` `CCoronas__Render_blendprobe_hook` entry shield | BLENDOP, ALPHABLEND, SRC, DST, ZWRITE, ZENABLE | additive block | **RESTORED** at 4213-4224 — but SRC/DST/ZWRITE/BLENDOP to **canonical**, not the captured `g_coronaPre*` | B |
| `main.cpp:4241-4244` CCoronas exit | ALPHAFUNC, ALPHAREF, SEPARATEALPHA, COLORWRITE | captured | (is the restore) | — |
| `main.cpp:4757-4870` `CClouds__Render_hygiene` | PS/VS, sampler0 min/mag/mip, ALPHAFUNC, ALPHAREF, FOGENABLE, TSS0 color/alpha op, ZENABLE, ZFUNC, ZWRITE, ALPHABLEND, SRC, DST | linear, ALWAYS@0, fog off, MODULATE, z off/on | **PARTIAL** — most captured/3-layer (4859-4870); **`SetTexture(5,NULL)` 4853 UNRESTORED**; PS/VS restored to NULL (hardcoded) | C (s5), D (rest) |
| `main.cpp:5123-5201` `CWaterLevel__RenderAndEmptyRenderBuffer_hook` | VS, PS, VertexDeclaration, FVF | water shaders / 0x152 | **RESTORED** 5189-5201 from captured device state + RW cache seeds | D |
| `main.cpp:191/202/203` `fixSAMP` | FOGENABLE, FOGTABLEMODE, FOGVERTEXMODE | derived from L1 fog state | **REPAIR (resync)** — no restore needed | D |

### 2.2 `src/entities/chars.cpp` (13)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `chars.cpp:476-478` `chars_drawSSSBlur` | s1 texture, s1 addr U/V | INTZ depth, CLAMP | **PARTIAL** — texture unbound 627; **addr restored to hardcoded WRAP** 630-631, not captured | C |
| `chars.cpp:563,607-610,623` SSS blend | ALPHABLEND, SRC=BLENDFACTOR, DST=INVBLENDFACTOR, BLENDFACTOR | lerp factors | **RESTORED** `RestoreRawGeomStates` 645 / 658 (BLENDFACTOR is in the 9) | — |
| `chars.cpp:515/535/564/612` (Rw) | `TEXTURERASTER` stage 0 | front buffer / sssBlurRasterA/B | **UNRESTORED** — no final clear/capture | C, mid-frame |

### 2.3 `src/render/buildingPipe.cpp` (65)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `buildingPipe.cpp:1517-1523` PS2 cb force | ALPHABLEND, SRC=ONE, DST=ONE, ALPHAFUNC=ALWAYS, **ALPHATESTENABLE=TRUE**, ZWRITE=FALSE, FOGCOLOR=0 | additive FX leg | **PARTIAL** — all restored 1549-1558 + 454-455 **except ALPHATESTENABLE** (left TRUE) | B/D |
| `buildingPipe.cpp:1549-1558` PS2 cb restore | fogcolor, zwrite, src, dst, alphafunc, alphablend, sampler0/1 addr | captured `rs` | (is the restore) | — |
| `buildingPipe.cpp:2467-2532` PBR cb | s3 IBL (neutral then g_iblTex), s4 normal buffer, sampler3 | CLAMP/LINEAR | **RESTORED (hardcoded)** 2976-2995 — canonical WRAP/LINEAR, not captured | C/D |
| `buildingPipe.cpp:2689/2701` PBR decal ZWRITE | ZWRITE=FALSE/TRUE | per-mesh | **RESTORED** by Switch 3123-3125 (captured trio) | — |
| `buildingPipe.cpp:2724-2743` PBR NM lane | s8 texture, sampler8 WRAP/LINEAR | per-mesh | **RESTORED (hardcoded)** 2979/2996 | D |
| `buildingPipe.cpp:1929/1941` Xbox decal ZWRITE | FALSE/TRUE | per-mesh | **RESTORED** by Switch | — |
| `buildingPipe.cpp:2030` Xbox blend | ALPHABLEND | per-mesh | **RESTORED** by Switch `pipeExitAlphaMode` | — |
| `buildingPipe.cpp:2174` Sphere cb blend | ALPHABLEND | savedVtxAlpha | **RESTORED** 2171-2174 | — |
| `buildingPipe.cpp:3055/3126` Switch ZWRITE | TRUE / captured trio | entry force | **RESTORED** | — |
| `buildingPipe.cpp:940-942` `bpipeBlendLeakProbe` | ALPHABLEND, SRC, DST | canonical | **REPAIR (canonical)** on broken signature only | D |
| `buildingPipe.cpp:724` `pipeForceAlphaBlock` | 12 states incl. ALPHATESTENABLE, SEPARATEALPHA, SRC/DEST/BLENDOPALPHA, COLORWRITE, ALPHAFUNC/REF | canonical | **PARTIAL** — `pipeEnter/ExitAlphaMode` restores blend/alpha-test/sepA/cwr, but **ALPHATESTENABLE, the three `*ALPHA` factors and BLENDOP are left canonical**; building cb restore (454) covers only the alpha pair/fogcolor/zwrite/texaddr | B/D |

### 2.4 `src/render/depthhook.cpp` (5)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `depthhook.cpp:531/561/611` | ZENABLE | FALSE / saved | **RESTORED** from `g_savedZenable`, three layers, on Suspend/Restore/Release | — |
| `depthhook.cpp:441` `hook_Reset` | ZENABLE=TRUE | canonical | **REPAIR (canonical)** after Reset | D |
| `depthhook.cpp:157` `DpUnbindIntzFromSamplers` | `SetTexture(s,NULL)` raw | unbind INTZ | **PARTIAL** — raw-only; RW's per-stage texture cache still holds INTZ, so a later `RwD3D9SetTexture(INTZ)` no-ops and the device stays NULL | C/D |

### 2.5 `src/render/envmap.cpp` (4)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `envmap.cpp:753-755` `DrawEnvMapCoronas` | SRC, DST, ALPHABLEND | per-layer captured | **RESTORED** — 3-layer captured park-and-set in `__finally` 727-760 (fixed mid-audit; was hardcoded SRCALPHA/INVSRCALPHA + L1-only) | — |
| `envmap.cpp:383` `envParkSetState3L` | helper | park→target per layer | — | — |

### 2.6 `src/render/forwardplus.cpp` (47)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `forwardplus.cpp:1541-1587` `ForwardPlus_SetConstants` | s5 tile, s6 pos atlas, s7 colour atlas, sampler5/6/7 POINT/CLAMP | per-frame cluster data | **UNRESTORED** (feature-owned; only s6/s7 are cleared in `vehiclePipe_cleanup` 657-661, s5 only by the PBR/building cleanups) | C |
| `forwardplus.cpp:1856-1939` `ForwardPlus_DrawClusterDebug` | LIGHTING, SPECULAR, ZENABLE, ZWRITE, ALPHABLEND, SRC, DST, CULL, FOG, STENCIL, ALPHATEST, ALPHAREF, ALPHAFUNC, 6× TSS0, **texture0** | debug overlay state | **PARTIAL** — all restored 1911-1939 **except `SetTexture(0,NULL)` 1879** (no capture) | C/D, debug-gated |

### 2.7 `src/render/mirrorPass.cpp` (1)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `mirrorPass.cpp:128` `MirrorRestoreState3L` + composite 240-350 | SRC, DST, ALPHABLEND, ZENABLE, ZWRITE, FOG, TEXTUREFILTER, **TEXTURERASTER**, PS c0/c1, override | captured per layer | **RESTORED — reference implementation.** `__finally`, park-and-set, rawGeom, PS consts, override. | — |

### 2.8 `src/render/neoCarpipe.cpp` (5)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `neoCarpipe.cpp:166/179` | FOGENABLE | off / captured | **RESTORED** three layers | — |
| `neoCarpipe.cpp:258-260` | ALPHABLEND, SRC, DST | captured per layer | **RESTORED** (clamped D3D domain) | — |

### 2.9 `src/render/neoWaterdrops.cpp` (2)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `neoWaterdrops.cpp:764-765` | SRC, DST | captured per layer | **RESTORED** 771-777 | — |
| `neoWaterdrops.cpp:768` | **FOGENABLE=0** | fog off | **UNRESTORED** — set after `DefinedState()`, never put back | B/D, every overlay frame |

### 2.10 `src/render/noise.cpp` (15)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `noise.cpp:887-893` `BindUniversal` | texture + 6 sampler states on caller stage | WRAP/LINEAR/NONE | **UNRESTORED** (helper; caller owns) | C/D |
| `noise.cpp:914-921` `BindAndUpload` | same + MAXANISOTROPY=1 | WRAP/LINEAR/NONE/1 | **UNRESTORED** (helper) | C/D |

### 2.11 `src/render/pipelinecommon.cpp` (12 — doctrine TU)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `pipelinecommon.cpp:2038-2048` `pipeApplyWorldBaseline` | ALPHABLEND, SRC, DST, BLENDOP, ZWRITE, ZENABLE, ZFUNC, CULL, SEPARATEALPHA, COLORWRITE, ALPHATEST | canonical world | **REPAIR (canonical)** — by design, no save | D |
| `pipelinecommon.cpp:1148` `pipeL3Set` | one state | caller | helper | — |

### 2.12 `src/render/postfx.cpp` (78)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx.cpp:3113-3177` `DrawNormalBufferToTexture` | s0 INTZ, sampler0 | CLAMP/POINT | **RESTORED** — raw `SetTexture(0)` unbound 3160/3166/3177; sampler0 covered by ReStore. Residual: the raw bind bypasses RW's stage-0 texture cache (cache still names the previous texture) — benign here, but it is a cache/device split | D |
| `postfx.cpp:3692-3928` `DrawPipeChain` | s1 normal, s2 depth, s3/s4 ping-pong/pack, sampler0-4 CLAMP/POINT, blend fade | per-pass | **PARTIAL** — textures 1-4 unbound 3950-3953; blend restored 3809-3811; ReStore 3955; **stage-0 `TEXTURERASTER` 3716/3743/3764/3861 UNRESTORED; sampler address stages 1-4 3855-3903 UNRESTORED** | C, every frame |
| `postfx.cpp:3411-3413 / 3496-3498` `PipeChain_ClassifyBegin/End` | ALPHABLEND, FOG, ZWRITE + c0/c1 + RT | pack state | **RESTORED** — 3-layer park-and-set + raw c0/c1 + RT, SEH both sides | — |
| `postfx.cpp:4103-4198` `DrawMotionBlur` | s1 velocity, s2 INTZ, sampler1/2 POINT/CLAMP | pass state | **PARTIAL** — textures unbound; sampler min/mag restored hardcoded LINEAR 4197-4198; **s2 address CLAMP not restored** | C |
| `postfx.cpp:4336-4462` `DrawHeightFog` | s1 INTZ, sampler1 CLAMP | pass state | **PARTIAL** — texture unbound 4416/4462; **sampler1 address CLAMP not restored** (ReStore covers stage 0 only) | C |
| `postfx.cpp:1059-1223` `Radiosity_shader` | 9 raw states (snapshot 1114) + camera raster swap 1137/1166 | Z off, fog off, blend ONE/ONE, hardcoded canonical exit 1213-1220 | **PARTIAL/UNRESTORED** — snapshot restored only on the `!PfFrontBufferValid` early return 1122-1124; **normal path never calls `RestoreRawGeomStates`**; hardcoded canonical exit; **no SEH around the raster swap** | **A, fault-only** |
| `postfx.cpp:1226-1420` `Radiosity` (classic) | camera raster 1336/1392 + raw `RwD3D9SetRenderTarget` 1341 + TSS1/2 | radiosity composite | **PARTIAL** — straight-line restore only; **no SEH**; RT/viewport not saved | **A, fault-only** |
| `postfx.cpp:848` `Radiosity_VCS` | `TEXTURERASTER=fb2` | stage 0 | **UNRESTORED** (no final clear) | C |
| `postfx.cpp:981` `Blur_VCS` | `TEXTURERASTER=NULL` | — | **RESTORED** | — |

### 2.13 `src/render/postfx_colourfilter.cpp` (12)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_colourfilter.cpp:629-633` `ColourFilter_Modern` | s1 frame luma, sampler1 CLAMP/LINEAR | auto-exposure | **PARTIAL** — texture unbound 677; **sampler1 address/filter left hardcoded** | C |
| `postfx_colourfilter.cpp:700-702` | ALPHABLEND, SRC, DST | captured device | **RESTORED** (3 layers) | — |
| `postfx_colourfilter.cpp:681-688` | FILTER, FOG, ZTEST, ZWRITE, TEXTURERASTER, SRC, DST, VERTEXALPHA | hardcoded canonical (no Store/ReStore in this pass) | **PARTIAL** — Z/FOG/FILTER not captured | B/D |
| `postfx_colourfilter.cpp:1515+` `DrawIVGrade` | rawGeom + Store/ReStore + TEXTUREADDRESS by hand | pass state | **RESTORED** | — |

### 2.14 `src/render/postfx_luminance.cpp` (14)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_luminance.cpp:103-164` `RenderLumaPass` | RT0/DS/viewport swap, s0 texture, sampler0 CLAMP/LINEAR, Z/FOG/blend | luma pass | **PARTIAL** — no `SaveRawGeomStates`; **no SEH**; RT swap restored only straight-line; texture/raster cleared 156-157 | **A, fault-only** |
| `postfx_luminance.cpp:219-230` `RunFrameExposure` | s0 measure, s1 prev | POINT | **PARTIAL** — s1 unbound 230; s0 cleared by RenderLumaPass; samplers not captured | C |

### 2.15 `src/render/postfx_motionblur.cpp` (16)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_motionblur.cpp:193-252` `DrawVelocityBuffer` | s0 INTZ, s1 prev depth, sampler0/1 POINT/CLAMP | velocity pass | **RESTORED** — rawGeom 172 + ReStore 259 + SEH; samplers restored hardcoded LINEAR 248-252 | C (sampler values only) |

### 2.16 `src/render/postfx_newfx.cpp` (25)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_newfx.cpp:348-417` `FxPassToRT` | s0/s1, sampler0/1, blend | pass state | **RESTORED** — rawGeom + ReStore + textures/raster cleared 416-418 | — |
| `postfx_newfx.cpp:467-489` `FxFramePass` | s0-s2, sampler1/2, blend | pass state | **RESTORED** — rawGeom + ReStore + textures/raster cleared 488-505 | — |

### 2.17 `src/render/postfx_smaa.cpp` (58)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_smaa.cpp:908/958/1033/1120` `DrawSMAA` | `TEXTURERASTER` stage 0 = scene/edge raster | scene copy / edge map | **UNRESTORED** — cleanup 1328-1334 unbinds stages 1-4 only; stage 0 raster survives | **C, next frame** |
| `postfx_smaa.cpp:965-976/1148-1203` | s1 area, s2 search/velocity, s3 INTZ, s4 normal, samplers | pass state | **PARTIAL** — textures unbound; **samplers restored hardcoded** `smaaRestoreSamplerStates` 362-366 (LINEAR/LINEAR/LINEAR/WRAP/WRAP, not captured) | C |
| `postfx_smaa.cpp:1174` | ZENABLE=FALSE | history save | **RESTORED** `smaaRestoreRawGeom` 1346 (rawGeom saved 764) | — |
| `postfx_smaa.cpp:399-401` `smaaGuardBail` | s1/s2/s3 + sampler restore + ReStore + raw | bail path | **RESTORED** | — |
| `postfx_smaa.cpp:295-331` `DrawSMAA_EdgeDetect` | s2 INTZ, s3 velocity | — | **RESTORED** unbind + DepthHook_Restore | — |

### 2.18 `src/render/postfx_ssao.cpp` (76)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_ssao.cpp:636-684` `DrawSSAO` | s0 INTZ, s1 noise, s2 normal, sampler0-2 | pass state | **RESTORED** textures unbound 682-684 | — |
| `postfx_ssao.cpp:708` `DrawSSAO` composite | `TEXTURERASTER` stage 0 = **g_ssaoOutputRaster** | AO multiply buffer | **UNRESTORED** — ReStore 730 does not cover `TEXTURERASTER` | **C, every frame** |
| `postfx_ssao.cpp:841-874/926-1018` `DrawSSAO_Overhaul` | s0-s6, sampler0-6 | pass state | **RESTORED** textures unbound 1048-1054 | — |
| `postfx_ssao.cpp:1076` `DrawSSAO_Overhaul` composite | `TEXTURERASTER` stage 0 = **g_ssaoOutputRaster** | AO multiply buffer | **UNRESTORED** | **C, every frame** |
| `postfx_ssao.cpp:254-283` `CopyDepthToPrev` | s0 INTZ, sampler0 | — | **RESTORED** unbind + rawGeom 286 | — |
| `postfx_ssao.cpp:374-414` `PostFX_CopyDepthToTexture` | s0 INTZ, sampler0, CULL | — | **RESTORED** unbind + rawGeom 417 + ReStore 416 | — |

### 2.19 `src/render/postfx_underwater.cpp` (5)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `postfx_underwater.cpp:454-533` | s1 INTZ, sampler1, blend, Z, fog | pass state | **RESTORED** rawGeom 437 + ReStore 521 + SEH | — |

### 2.20 `src/render/treeWind.cpp` (2)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `treeWind.cpp:438/455` | ALPHAREF | scaled / base | **RESTORED** 3-layer, `s_alphaBaseRef` capture | — |

### 2.21 `src/render/vehiclePipe.cpp` (82)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `vehiclePipe.cpp:646-682` `vehiclePipe_cleanup` | sampler3-7, texture6/7 | canonical WRAP/LINEAR | **REPAIR (canonical)** | D |
| `vehiclePipe.cpp:716-722` `vehiclePipe_fxAdditiveBlend` | ALPHABLEND, SRC=ONE, DST=ONE, ALPHAFUNC=ALWAYS, **ALPHATESTENABLE=TRUE**, ZWRITE=FALSE, FOGCOLOR=0 | additive FX leg | **PARTIAL** — 750-759 restores FOGCOLOR/SRC/DST/ALPHAFUNC/ALPHABLEND/VERTEXALPHA/TEXTUREFACTOR/sampler0-2; **ALPHATESTENABLE left TRUE; ZWRITE hardcoded TRUE** | B/D |
| `vehiclePipe.cpp:1701/1709-1710` Xbox | TEXTUREFACTOR, sampler addr | captured `xboxState` | **RESTORED** 1666-1667 region | — |
| `vehiclePipe.cpp:1857-1862` leeds FX | ALPHAFUNC=ALWAYS, FOG=FALSE, ZWRITE=FALSE, ALPHABLEND, SRC, DST | additive | **PARTIAL** — restored 1885-1890; **ZWRITE hardcoded TRUE**, FOG restored, ALPHATESTENABLE not touched | D |
| `vehiclePipe.cpp:2077-2105` mobile | sampler1 addr | captured | **RESTORED** | — |
| `vehiclePipe.cpp:2281` `vehEnvBindTextureRaw` | `SetTexture(stage,tex)` raw | IBL/env | **PARTIAL** — cleanup unbinds 3/4/5/8; other stages not | C/D |
| `vehiclePipe.cpp:2352-2355` IBL samplers | CLAMP/LINEAR | — | **RESTORED (hardcoded)** cleanup | D |
| `vehiclePipe.cpp:2809-2813` Env s8 sampler | WRAP/LINEAR | — | **RESTORED (hardcoded)** cleanup | D |
| `vehiclePipe.cpp:2983/2984/3025/3026` Env blend | SRC/DST | per-mesh | **RESTORED** 3570-3573 from captured `alphafunc/src/dst/fog` | — |
| `vehiclePipe.cpp:3567-3570` Env unbind | s3/s4/s5/s8 | — | **RESTORED** | — |
| `vehiclePipe.cpp:3625/3725-3746` Switch | ZWRITE, ALPHABLEND, SRC, DST, BLENDOP | captured per layer | **RESTORED** | — |

### 2.22 `src/render/waterPipe.cpp` (42)

| file:line | state(s) | value | restore? | blast |
|---|---|---|---|---|
| `waterPipe.cpp:1587-1589` | ZENABLE, ZWRITE, ZFUNC | TRUE/FALSE/LESSEQUAL | **RESTORED** from per-layer snapshot 1747-1749 | — |
| `waterPipe.cpp:1599-1635` | s0-s3 textures, sampler0-3 | scene/detail/depth/refl | **RESTORED (hardcoded)** 1706-1724 (NULL + LINEAR/WRAP) | C |
| `waterPipe.cpp:1643/1647` | VERTEXALPHA, SRC, DST | SRCALPHA/INVSRCALPHA | **RESTORED (hardcoded)** 1729-1731 | B/D |
| `waterPipe.cpp:1661` | FOGENABLE=FALSE | — | **RESTORED (hardcoded TRUE)** 1734 | B/D |
| `waterPipe.cpp:1656/1659` | CULLMODE | captured / NONE | **RESTORED** 1733 | — |
| `waterPipe.cpp:624-626` `WaterPipe_ReleaseDeviceObjects` | s0/s2/s3 NULL | release-time | n/a | — |

---

## 3. Specific call-outs

### 3.1 The corona per-draw shield — **NO, it does not restore every state it touches**

`coronaDrawRawShield` (`main.cpp:2792-2836`) writes **9** states: ALPHABLENDENABLE, SRCBLEND,
DESTBLEND, ZWRITEENABLE, ALPHAFUNC, ALPHAREF, SEPARATEALPHABLENDENABLE, COLORWRITEENABLE,
BLENDOP. The shield's contract is "park-and-set, no restore by itself"; restoration is split
across two different exits:

| Exit | States restored | Source |
|---|---|---|
| `CCoronas__Render_blendprobe_hook` exit (`main.cpp:4213-4244`) | all 9 — ALPHABLEND, ALPHAFUNC/REF, SEP, CWR from capture; SRC/DST/ZWRITE/BLENDOP to **canonical** | capture + hardcoded |
| `SpriteFlushIndexedDraw_hook` window close (`main.cpp:3013-3018`) | 6 (blend/src/dst/op/z/ztest) to **canonical** | hardcoded resync |
| `CoronaWindow_Close` (`main.cpp:3095-3097`) | **3** — ALPHAFUNC, ALPHAREF, ALPHABLEND | capture |

So the answer is **no**: on the fault / deferred-flush path (the path `CoronaWindow_Close` exists
for), **SRCBLEND, DESTBLEND, ZWRITEENABLE, BLENDOP, SEPARATEALPHABLENDENABLE and
COLORWRITEENABLE are left at the corona values**. Worse, the three states it does restore come
from a capture that is **never invalidated** (see #1 below).

Also note: the hook captures `g_coronaPreSrcBlend`, `g_coronaPreDstBlend`,
`g_coronaPreBlendOp`, `g_coronaPreZWrite` at `main.cpp:4045-4067` and then **never uses them**
in the restore (they are only printed in the `[CoronaExit]` log at 4249-4262). That is the
smoking gun that the captured-state restore was intended but only half-implemented.

### 3.2 The shadow contract heal path — **restores to rw truth, not the polluted capture**

`shadowContractRun` (`main.cpp:1483-1520`) captures a POD `ShadowContractState` per call (stack
local — no stale-capture risk), forces the canonical shadow block, calls the original, then:

* **drift == 0** → verbatim per-layer restore (L1/L2/L3 each from its own capture).
* **drift > 0** → L1 from its capture; L2 from its capture; **L3 pinned to the L1 rw-cache
  truth** (blend/src/dst/ztest/zwrite/alphafunc/alpharef/fog). The polluted L3 capture is not
  re-emitted.
* **ZFUNC and ALPHATESTENABLE have no rw twin** and are always restored from the L3 capture
  (`main.cpp:1449-1450`).
* It only touches the 10 states in `ShadowContractState`; CULLMODE/COLORWRITE/BLENDOP are
  neither written nor restored.

Verdict: **correct heal doctrine**, and the model for a "capture → force → heal" wrapper. The
only residual risk is that L1 itself can be polluted by an earlier L1-only writer (e.g. a raw
restore elsewhere); the heal then propagates the cache's belief, which is the documented
doctrine (L1 is "the world's belief"). It does not have the stale-capture bug the corona window
has.

### 3.3 `setMoonAlphaBlendStates` / moon mask — **three alpha factors are never restored**

`main.cpp:3139-3150`:

```
capture:  ALPHATESTFUNCTION (L1 only)
force:    ALPHATESTFUNCTION=ALWAYS (L1→L2→L3)
          SEPARATEALPHABLENDENABLE=1, BLENDOPALPHA=ADD,
          SRCBLENDALPHA=SRCALPHA, DESTBLENDALPHA=ZERO   (L2 only)
restore:  SEPARATEALPHABLENDENABLE=0 (hardcoded), ALPHATESTFUNCTION from L1
```

* **UNRESTORED:** `BLENDOPALPHA`, `SRCBLENDALPHA`, `DESTBLENDALPHA`. They are dormant while
  `SEPARATEALPHABLENDENABLE=0`, but any later writer that enables separate-alpha inherits
  `DESTBLENDALPHA=ZERO` (destination alpha zeroed for the rest of the frame).
* `SEPARATEALPHABLENDENABLE` is restored to a **hardcoded 0**, not the captured value.
* `alphafunc` is a single file-scope static (not re-entrant) and is **not domain-sanitized**
  before the restore. A failed `RwRenderStateGet` leaves 0 (`rwALPHATESTFUNCTIONNA`), which
  `RwRenderStateSet` will index into the rw→D3D conversion table out of bounds — the same
  class as the documented `BLENDFACTOR(14)` OOB.
* The SEH bracket itself is good: `renderMoonMaskBody` (`main.cpp:3170-3200`) runs the restore
  in its own `__try` after the frame recovery, so a fault in `0x70D000` cannot skip it.

Mitigation today: `pipeForceAlphaBlock` (`buildingPipe.cpp:601-614`) re-forces SEP=0 and the
canonical alpha factors at every building/vehicle entry, so the leak is usually repaired
before it can bite. It is still a latent state split.

### 3.4 `CClouds` hygiene — which states are still missing

`CClouds__Render_hygiene` (`main.cpp:4606-4871`) currently **saves and 3-layer-restores**:
TEXTUREFILTER, ALPHAFUNC, ALPHAREF, FOGENABLE, ZENABLE, ZFUNC, ZWRITE, ALPHABLEND,
SRCBLEND, DESTBLEND, sampler0 min/mag/mip, TSS0 COLOROP/ALPHAOP.

Still missing:

| Gap | Detail | Blast |
|---|---|---|
| **`SetTexture(5, NULL)` at 4853** | Forward+ cluster tile is clobbered on the device with no capture/restore. | C |
| **PS/VS forced NULL, restored to NULL** (4757-4758, 4851-4852) | Not the pre-entry shader. Deliberate neutral, but it is a hardcoded "restore". | D |
| **No entry force for CULLMODE** | The sky dome is the first FFP draw of the frame; an inherited `FRONT`/`NONE` is not repaired here. | **A potential** (sky invisible if FRONT) |
| **No entry force for COLORWRITEENABLE** | An inherited channel mask blanks the sky. | **A potential** |
| **No entry force for ALPHATESTENABLE** | An inherited FALSE lets the cloud/sun cutouts accept every texel. | B |
| **No `TEXTURERASTER` entry force/clear** | The previous frame's last pass's stage-0 raster is inherited. | C |

The first three are the reason CClouds is in the top-10: it is the **first consumer** after the
frame boundary, so whatever the previous frame leaked lands here before any pipe entry can
repair it.

### 3.5 `DrawEnvMapCoronas` — **was** hardcoded SRCALPHA/INVSRCALPHA + L1-only; **fixed mid-audit**

The reported defect was real in the version this audit started from: the `__finally` wrote
hardcoded `SRCALPHA/INVSRCALPHA/FALSE/NULL` through `RwRenderStateSet` (L1) only. During the
audit the file was edited to capture **per layer** (L1 `RwRenderStateGet`, L2
`RwD3D9GetRenderState`, L3 `dev->GetRenderState`) and restore via `envParkSetState3L` in
`__finally` (`envmap.cpp:648-760`), including the pre-entry `TEXTURERASTER`. **Current state:
RESTORED.** Keep this as the reference for "capture, don't canonicalise" — but re-verify the
fix is still in the tree before relying on it (the tree was live).

### 3.6 The mirror composite — **the reference implementation**

`mirrorPass.cpp:240-350` is the only pass that does everything right in one place:

* captures every state it touches **per layer** (SRC/DST/ALPHABLEND/FOG/ZTEST/ZWRITE/FILTER/
  **TEXTURERASTER**), domain-sanitizes the filter;
* captures `SaveRawGeomStates` + PS constants c0/c1 + `overrideIm2dPixelShader`;
* restores in `__finally` (normal, `__leave`, swallowed fault, fault in handler);
* uses park-and-set on L1+L2 + one flush + raw L3 pin per state;
* parks the filter so the cache always transitions;
* clears the Im2D PS override.

Copy this shape for every pass in §2 that is marked PARTIAL/UNRESTORED.

### 3.7 Every postfx pass that toggles ZTEST / ZWRITE / CULL

| Pass | ZTEST | ZWRITE | CULL | Restore status |
|---|---|---|---|---|
| `PipeChain_ClassifyBegin/End` | — | FALSE | — | **RESTORED** 3-layer + raw |
| `DrawPipeChain` | FALSE (Rw) | FALSE (Rw) | — | **RESTORED** via ReStore + rawGeom (Z is in both) |
| `DrawNormalBufferToTexture` | FALSE | FALSE | — | **RESTORED** ReStore + rawGeom |
| `DrawSSAO` / `_Overhaul` | FALSE | FALSE | NONE | **RESTORED** ReStore + rawGeom (CULL is in ReStore) |
| `CopyDepthToPrev` | FALSE | FALSE | — | **RESTORED** |
| `PostFX_CopyDepthToTexture` | FALSE | FALSE | NONE | **RESTORED** |
| `DrawVelocityBuffer` | FALSE | FALSE | — | **RESTORED** |
| `DrawSMAA` | FALSE (raw 1174) | FALSE | NONE (Rw) | **RESTORED** `smaaRestoreRawGeom` + ReStore |
| `DrawMotionBlur` | FALSE | FALSE | — | **RESTORED** |
| `DrawHeightFog` | FALSE | FALSE | — | **RESTORED** |
| `DrawGodRays` | FALSE | FALSE | — | **RESTORED** |
| `PostFX_UnderwaterTick` | FALSE | FALSE | — | **RESTORED** |
| `FxPassToRT` / `FxFramePass` | FALSE | FALSE | — | **RESTORED** |
| `Radiosity_shader` | FALSE | FALSE | — | **PARTIAL** (hardcoded TRUE exit; no rawGeom on normal path) |
| `Radiosity` (classic) | FALSE | FALSE | — | **PARTIAL** (hardcoded TRUE exit; no SEH) |
| `RenderLumaPass` | FALSE | FALSE | — | **PARTIAL** (no rawGeom, no SEH) |
| `ColourFilter_Modern` | FALSE | FALSE | — | **PARTIAL** (hardcoded TRUE exit) |
| `ForwardPlus_DrawClusterDebug` | FALSE | FALSE | NONE | **RESTORED** (raw; debug only) |

**Conclusion:** every pass that uses the `SaveRawGeomStates` + `ReStore` + SEH pattern is clean.
The four PARTIALs are exactly the passes that predate or skip that pattern.

---

## 4. Top-10 fixes, in order

### 1. `CoronaWindow_Close`: clear the one-shot capture, restore all 9 shield states, and stop overriding the baseline

**Where:** `main.cpp:3078-3098` (`CoronaWindow_Close`), `main.cpp:2712` (`g_coronaPreValid`),
`main.cpp:4067` (set true), `main.cpp:3321-3329` (SceneStart order).

**Why #1:** `g_coronaPreValid` is never cleared. After the first corona window,
`CoronaWindow_Close` re-applies the **first-ever** ALPHABLEND/ALPHAFUNC/ALPHAREF at every
`RenderScene_before` — and it runs **after** `pipeApplyWorldBaseline`, so the stale values win
for the first draw of every frame. It also restores only 3 of the 9 states the per-draw shield
touches.

**Exact fix:**

```c
void CoronaWindow_Close(void)
{
    g_inCoronasRender = false;
    g_coronaBodyDone = false;
    if(!g_coronaPreValid)
        return;
    // existing 3 (ALPHAFUNC/ALPHAREF/ALPHABLEND) stay
    // ... add the six the per-draw shield also writes:
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)g_coronaPreSrcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)g_coronaPreDstBlend);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)g_coronaPreZWrite);
    RwD3D9SetRenderState(D3DRS_SRCBLEND, g_coronaPreSrcBlend);
    RwD3D9SetRenderState(D3DRS_DESTBLEND, g_coronaPreDstBlend);
    RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, g_coronaPreZWrite ? TRUE : FALSE);
    RwD3D9SetRenderState(D3DRS_BLENDOP, g_coronaPreBlendOp);
    RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
    RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
    if(d3d9device){
        d3d9device->SetRenderState(D3DRS_SRCBLEND, g_coronaPreSrcBlend);
        d3d9device->SetRenderState(D3DRS_DESTBLEND, g_coronaPreDstBlend);
        d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, g_coronaPreZWrite ? TRUE : FALSE);
        d3d9device->SetRenderState(D3DRS_BLENDOP, g_coronaPreBlendOp);
        d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
        d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
    }
    g_coronaPreValid = false;      // <-- one-shot: never replay a stale capture
}
```

Also: `CCoronas__Render_blendprobe_hook` exit (`main.cpp:4233-4244`) must set
`g_coronaPreValid = false` after its restore, and `RenderScene_before` should call
`CoronaWindow_Close()` **before** `pipeApplyWorldBaseline` (so the baseline is the last word at
frame start), or keep the order and rely on the flag clear.

### 2. Radiosity family: SEH-fence the camera-raster swap and restore the raw snapshot

**Where:** `postfx.cpp:1059-1223` (`Radiosity_shader`), `postfx.cpp:1226-1420` (`Radiosity`),
raster swaps at `postfx.cpp:1137/1166` and `1336/1392`, raw snapshot at `1114`.

**Why #2:** a fault between `RwCameraSetRaster(Scene.camera, workBuffer)` and the restore leaves
`Scene.camera` pointed at an off-screen work buffer for the rest of the frame (and until the
next successful Radiosity pass) — the documented "screen never receives the world" class. The
normal path also never calls `RestoreRawGeomStates` even though it saves the snapshot.

**Exact fix:** move the SEH into a POD-only helper (the function carries `PF_SCOPE`, a
destructible object → C2712; same reason `renderMoonMaskBody` exists):

```c
// POD-only helper; __finally is not available in MSVC C++, so use the
// renderMoonMaskBody pattern: __try/__except + unconditional tail.
__try {
    RwCameraSetRaster(Scene.camera, workBuffer);
    RwCameraBeginUpdate(Scene.camera);
    ... blur + composite ...
} __except(EXCEPTION_EXECUTE_HANDLER) { /* log */ }
// unconditional tail (normal + fault):
RwCameraEndUpdate(Scene.camera);
RwCameraSetRaster(Scene.camera, drawBuffer);
RwCameraBeginUpdate(Scene.camera);
CPostEffects::ImmediateModeRenderStatesReStore();
if(rawGeomSaved) RestoreRawGeomStates(rawGeom);
```

For the classic `Radiosity`, additionally save/restore RT0, DS and viewport (it currently uses
raw `RwD3D9SetRenderTarget(0, renderBuffer)` at 1341 and restores only the camera raster).

### 3. SSAO composites: clear stage-0 `TEXTURERASTER`

**Where:** `postfx_ssao.cpp:708` (`DrawSSAO`), `postfx_ssao.cpp:1076` (`DrawSSAO_Overhaul`),
`postfx_ssao.cpp:730/1098` (`ImmediateModeRenderStatesReStore`).

**Why #3:** the AO multiply buffer is left as the rw-cache stage-0 texture. `ReStore` does not
cover `TEXTURERASTER`. Any later FFP draw that does not rebind stage 0 multiplies by the AO
buffer (dark blotches / black). SSAO runs every frame.

**Exact fix:** capture at entry and restore at the composite tail + in the `__except`:

```c
void *savedTexRaster = NULL;
RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedTexRaster);
...
RwRenderStateSet(rwRENDERSTATETEXTURERASTER, savedTexRaster);
```

(or `NULL` if the pre-pass binding is not wanted — but capture is the doctrine).

### 4. SMAA: restore stage-0 `TEXTURERASTER` and capture sampler states

**Where:** `postfx_smaa.cpp:908/958/1033/1120` (`TEXTURERASTER`), cleanup `1328-1334`,
`smaaGuardBail` `398-407`, `smaaRestoreSamplerStates` `357-368`.

**Why #4:** if SMAA is the last pass of the frame (FXAA suppressed, YCbCr off), the next frame's
first FFP draw (sky/cloud) inherits the **edge map** or **scene copy** on stage 0. The sampler
restore is hardcoded `LINEAR/WRAP` for stages 1-4, not the pre-pass values.

**Exact fix:** add `void *savedTexRaster` (captured at entry, restored in both the success tail
and `smaaGuardBail`), and replace the hardcoded `smaaRestoreSamplerStates` body with a captured
per-stage snapshot taken before the first `SetSamplerState` (same shape as
`buildingPipe_saveRenderState`).

### 5. DrawPipeChain: restore stage-0 raster and stages 1-4 sampler address

**Where:** `postfx.cpp:3716/3743/3764/3861` (`TEXTURERASTER`), `postfx.cpp:3855-3903`
(sampler CLAMP), cleanup `3950-3953`, ReStore `3955`.

**Why #5:** the chain leaves the **front buffer** on stage 0 and CLAMP on stages 1-4. ReStore
only covers stage 0's address/filter. A later pass that samples a stage-1-4 texture without
setting its own address inherits CLAMP (edge smear on tiled world textures).

**Exact fix:** capture `savedTexRaster` + the four sampler address pairs at entry; restore after
`ImmediateModeRenderStatesReStore()` and in the `__except`.

### 6. `chars_drawSSSBlur`: restore stage-0 raster and stage-1 sampler

**Where:** `chars.cpp:612` (`TEXTURERASTER = sssBlurRasterB`), `chars.cpp:626-631` (cleanup).

**Why #6:** the blur buffer is left as stage 0; stage-1 addressing is restored to a hardcoded
WRAP. The pass runs mid-frame (after the colour filter), so the next draw inherits both.

**Exact fix:** capture `TEXTURERASTER` and sampler1 ADDRESSU/V at entry; restore in the normal
tail and the `__except` (the pass already has the SEH shape).

### 7. `CClouds__Render_hygiene`: add the three missing entry forces and stop clobbering s5

**Where:** `main.cpp:4606` (entry), `main.cpp:4757-4870` (force/restore), `main.cpp:4853`
(`SetTexture(5,NULL)`).

**Why #7:** CClouds is the first FFP consumer of the frame. It repairs blend/alpha/fog/Z but
not **CULLMODE, COLORWRITEENABLE, ALPHATESTENABLE**, and it clobbers the Forward+ s5 tile with
no capture. An inherited `CULLMODE=FRONT` or `COLORWRITEENABLE` mask makes the sky dome
invisible/blank before any pipe entry can repair it.

**Exact fix:** at entry (before the first draw), capture + force + restore in the existing
SEH tail:

```c
RwUInt32 savedCull; RwRenderStateGet(rwRENDERSTATECULLMODE, &savedCull);
DWORD savedCwr = 0xF; dev->GetRenderState(D3DRS_COLORWRITEENABLE, &savedCwr);
DWORD savedAtest = TRUE; dev->GetRenderState(D3DRS_ALPHATESTENABLE, &savedAtest);
RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
// ... tail: restore savedCull / savedCwr / savedAtest through L1+L2+L3
```

And capture/restore the stage-5 texture (or delete the `SetTexture(5,NULL)`; the vehicle
cleanup already clears s6/s7 at 657-661).

### 8. `vehiclePipe_fxAdditiveBlend`: capture ALPHATESTENABLE and ZWRITE

**Where:** `vehiclePipe.cpp:716-722` (force), `vehiclePipe.cpp:750-759` (restore).

**Why #8:** ALPHATESTENABLE is forced TRUE and never restored; ZWRITE is restored to a hardcoded
TRUE. If the pre-mesh ZWRITE was FALSE (a translucent/decal mesh), the next mesh writes depth
and carves itself into the depth buffer.

**Exact fix:**

```c
DWORD savedAtest, savedZWrite;
d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &savedAtest);
d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWrite);
...
d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, savedAtest);
RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWrite);
RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWrite);
d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWrite);
```

Apply the same capture to the PS2 building FX leg (`buildingPipe.cpp:1521`) and to
`pipeForceAlphaBlock`'s callers (`buildingPipe.cpp:724`) — the latter leaves ALPHATESTENABLE,
BLENDOP and the three `*ALPHA` factors canonical after every building/vehicle Switch.

### 9. Forward+: own and release s5/s6/s7, and restore stage 0 in the debug overlay

**Where:** `forwardplus.cpp:1541-1587` (`SetConstants`), `forwardplus.cpp:1879`
(`DrawClusterDebug`).

**Why #9:** `SetConstants` binds s5/s6/s7 + POINT/CLAMP and never unbinds; the building PBR
cleanup clears s3/s4/s5/s8 but not s6/s7, and the vehicle cleanup clears s6/s7 but not s5.
`DrawClusterDebug` leaves stage 0 NULL (no capture). Any FFP/HUD draw that samples those stages
gets cluster data.

**Exact fix:** add a single `ForwardPlus_ReleaseTextureStages()` that unbinds s5/s6/s7 and
resets their samplers, called from the last PBR cleanup and from `DrawClusterDebug`'s tail; in
`DrawClusterDebug`, capture/restore `dev->SetTexture(0, …)`.

### 10. `waterPipe` / `neoWaterdrops`: capture the blend quartet and FOG

**Where:** `waterPipe.cpp:1729-1734` (hardcoded SRCALPHA/INVSRCALPHA/VERTEXALPHA/FOG),
`neoWaterdrops.cpp:768` (`FOGENABLE=0`).

**Why #10:** water is mid-frame; the hardcoded restore overwrites any pre-water blend pair
(e.g. additive from a corona pass) and forces fog on. Waterdrops sets fog off and never restores
it. Both are tint/alpha-class, but they run every frame.

**Exact fix:** in `waterPipe_setRenderState`, capture `SRCBLEND`, `DESTBLEND`,
`VERTEXALPHAENABLE`, `FOGENABLE` per layer alongside the existing Z snapshot, and restore them
in `waterPipe_restoreRenderState` (the Z block at 1735-1749 is already the correct shape). In
waterdrops, capture `FOGENABLE` before `DefinedState()` and restore it after the blend restore.

**Runner-up (#11):** `pipeForceAlphaBlock` canonical leftovers (ALPHATESTENABLE/BLENDOP/`*ALPHA`)
and `DrawEnvMapCoronas`'s fix should be re-verified after the live edit (§3.5). Neither is
currently a top-10 leak on the healthy path.

---

## 5. Doctrine — states that must always be saved/restored around any Im2D or raw draw

Any function that issues a raw `dev->Set*` or a `RwRenderStateSet`/`RwD3D9SetRenderState`
**and is not the pipe entry/exit contract itself** must, at its outermost scope:

1. **Capture before the first write** (pre-seed every out-param; a failed `Get` must never
   push garbage). Capture **per layer** where a raw device write is involved: L1 `Get`,
   L2 `RwD3D9GetRenderState`, L3 `dev->GetRenderState`.
2. **Restore on every exit** — normal, early `return`, `__leave`, SEH fault, fault in the
   handler. Use `__finally` where legal; otherwise the `renderMoonMaskBody` pattern (SEH +
   unconditional tail). Never rely on the straight-line path.
3. **Restore per layer with park-and-set** (`pipeRwParkSet` → `pipeL2ParkSet` → one
   `_rwD3D9RenderStateFlushCache` → `pipeL3Set`). A plain `RwRenderStateSet` is an L1 write and
   cannot repair L3.
4. **Never hardcode a value you did not capture** at an exit. Canonical values belong at a pipe
   *entry* contract, not at a pass exit.

### 5.1 The save/restore checklist (union of everything observed in this tree)

**Depth / rasterisation**
- `D3DRS_ZENABLE`, `D3DRS_ZWRITEENABLE`, `D3DRS_ZFUNC`
- `D3DRS_CULLMODE`
- `D3DRS_COLORWRITEENABLE` (a 0 mask makes geometry invisible)
- `D3DRS_STENCILENABLE` / stencil func/op/mask (if touched)
- RT0 (`GetRenderTarget`), depth-stencil (`GetDepthStencilSurface`), viewport (`GetViewport`)
- `RwCameraSetRaster` if you swap the camera raster (capture the `RwRaster*`)

**Alpha test (invisible / black-pool class)**
- `D3DRS_ALPHATESTENABLE`
- `D3DRS_ALPHAFUNC`, `D3DRS_ALPHAREF`

**Blend (tint / black-quad class)**
- `D3DRS_ALPHABLENDENABLE`, `D3DRS_SRCBLEND`, `D3DRS_DESTBLEND`, `D3DRS_BLENDOP`,
  `D3DRS_BLENDFACTOR`
- `D3DRS_SEPARATEALPHABLENDENABLE`, `D3DRS_SRCBLENDALPHA`, `D3DRS_DESTBLENDALPHA`,
  `D3DRS_BLENDOPALPHA`

**Fog / misc**
- `D3DRS_FOGENABLE`, `D3DRS_FOGCOLOR`
- `D3DRS_TEXTUREFACTOR`
- `D3DRS_LIGHTING`, `D3DRS_SPECULARENABLE` (FFP debug draws)
- `D3DRS_SHADEMODE`

**Textures / samplers (the most-leaked class)**
- `rwRENDERSTATETEXTURERASTER` (stage 0) — **not** covered by ReStore
- every stage you bind: `SetTexture(stage, …)`
- every stage you touch: `D3DSAMP_ADDRESSU/V/W`, `MINFILTER`, `MAGFILTER`, `MIPFILTER`,
  `MAXANISOTROPY`
- `D3DTSS_COLOROP`, `D3DTSS_COLORARG1/2/0`, `D3DTSS_ALPHAOP`, `D3DTSS_ALPHAARG1/2/0`,
  `D3DTSS_TEXCOORDINDEX`, `D3DTSS_TEXTURETRANSFORMFLAGS`

**Shaders / input layout**
- `SetPixelShader`, `SetVertexShader` **and** the RW value-compare caches
  `0x8E2448` (`_rwD3D9LastVertexShaderUsed`) / `0x8E244C` (`_rwD3D9LastPixelShaderUsed`)
- `SetVertexDeclaration` / `SetFVF` **and** `0x8E2444` / `0x8E2440`
- `SetStreamSource(0, …)` / `SetIndices`
- PS constants you upload (the postfx chain shares c0-c8; capture with
  `GetPixelShaderConstantF` and restore raw)

**Im2D override (project-specific)**
- `overrideIm2dPixelShader`
- `overrideColorMod`, `overrideAlphaMod`
- `g_inGuardedIm2DPass`, `g_renderPhase`

### 5.2 The four anti-patterns this audit found

| Anti-pattern | Example | Fix shape |
|---|---|---|
| **Stale one-shot capture** | `g_coronaPreValid` never cleared; `CoronaWindow_Close` replays the first window forever | clear the flag after the restore; capture per window |
| **L1-only restore** | `DrawEnvMapCoronas` (pre-fix), `DrawEnvMapCoronas`'s old `RwRenderStateSet` pair | park-and-set L1+L2, flush, raw L3 pin |
| **Hardcoded canonical restore** | `waterPipe_restoreRenderState`, `vehiclePipe_fxAdditiveBlend` ZWRITE, `ColourFilter_Modern` Z/FOG | capture per layer, restore captured |
| **Save without restore** | `Radiosity_shader` `rawGeomSaved` unused on the normal path | `RestoreRawGeomStates` in the unconditional tail (and in `__except`) |

---

## Appendix A — raw-site inventory (snapshot)

| File | Raw sites | Files with a captured 3-layer restore | Notes |
|---|---:|---|---|
| `src/core/main.cpp` | 131 | shadow contract, ped opaque, CCoronas exit, water hook | corona window close is the outlier |
| `src/render/vehiclePipe.cpp` | 82 | Switch, Env blend, Xbox, mobile | FX additive ALPHATEST leak |
| `src/render/postfx.cpp` | 78 | PipeChain classify, MotionBlur, HeightFog, GodRays, NewFX | Radiosity family + PipeChain textures |
| `src/render/postfx_ssao.cpp` | 76 | all passes | stage-0 raster leak in both composites |
| `src/render/buildingPipe.cpp` | 65 | PS2/Xbox/PBR via Switch | PS2 ALPHATESTENABLE |
| `src/render/postfx_smaa.cpp` | 58 | DrawSMAA raw geom, bails | stage-0 raster + hardcoded samplers |
| `src/render/forwardplus.cpp` | 47 | debug overlay | s5/s6/s7 feature-owned |
| `src/render/waterPipe.cpp` | 42 | Z block captured | blend/fog/textures hardcoded |
| `src/render/postfx_newfx.cpp` | 25 | both passes | — |
| `src/render/postfx_motionblur.cpp` | 16 | DrawVelocityBuffer | hardcoded samplers |
| `src/render/noise.cpp` | 15 | — | helper, caller owns |
| `src/render/postfx_luminance.cpp` | 14 | — | no rawGeom, no SEH |
| `src/entities/chars.cpp` | 13 | SSS blend | stage-0 raster |
| `src/render/pipelinecommon.cpp` | 12 (doctrine) | baseline by design | — |
| `src/render/postfx_colourfilter.cpp` | 12 | IVGrade, Modern blend | Modern Z/FOG hardcoded |
| `src/render/depthhook.cpp` | 5 | Suspend/Restore | INTZ sampler scrub raw |
| `src/render/neoCarpipe.cpp` | 5 | fog/blend | — |
| `src/render/postfx_underwater.cpp` | 5 | — | — |
| `src/render/envmap.cpp` | 4 | DrawEnvMapCoronas (fixed) | — |
| `src/render/neoWaterdrops.cpp` | 2 | blend | fog leak |
| `src/render/treeWind.cpp` | 2 | alpharef | — |
| `src/render/mirrorPass.cpp` | 1 | composite (reference) | — |
| **Total** | **710** | | 698 outside the two doctrine TUs |

Rw setter sites (`RwRenderStateSet` + `RwD3D9SetRenderState`): **1,203** across 100 files.

## Appendix B — the two helper-coverage holes to close first

1. **`SaveRawGeomStates` should grow from 9 to 14** (or a second helper should be added):
   add `ALPHAFUNC`, `ALPHAREF`, `ZFUNC`, `COLORWRITEENABLE`, `SEPARATEALPHABLENDENABLE`.
   That single change makes every pass already using the raw-geom pattern cover the
   invisible/alpha class without hand-rolling.
2. **`TEXTURERASTER` is not restored by anything.** Add a small
   `SaveTextureStage0()/RestoreTextureStage0()` (or extend the postfx raw-geom helper with
   stage-0 raster + sampler) and call it in every pass that writes
   `rwRENDERSTATETEXTURERASTER`. This is a mechanical, low-risk change that kills the whole
   `§2` "C" column.

Both changes are additive and do not alter the canonical entry contracts; they only make the
existing save/restore pattern cover the states the passes already write.
