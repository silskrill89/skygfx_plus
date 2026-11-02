# SDK Compare — L1/L2/L3 Render-State Doctrine vs Reference Forks

Date: 2026-10-26
Scope: READ-ONLY comparison. Our `src/render/pipelinecommon.cpp`, `src/core/main.cpp`,
`src/render/postfx_utils.cpp` against `skygfx_original`, `skygfx_junior`, `skygfx` (MTA)
and `skygfx_aap_v4.2b`.

## 0. Verdict Summary

**The doctrine has no counterpart in any reference tree.** `skygfx_original` (aap),
`skygfx_junior` and the MTA fork contain **zero** frame-boundary or pipe-entry state
forcing. Their entire render-state vocabulary is:

| Reference behaviour | aap | junior | MTA |
|---|---|---|---|
| Save/restore `alphafunc/ref/src/dst/fog/zwrite` at building-cb entry+exit | `buildingPipe.cpp:186-191`, restore `:270-271` (aap) | `buildingPipe.cpp:222-227`, restore `:333-334` | same as aap |
| Per-mesh `rwRENDERSTATEVERTEXALPHAENABLE = hasAlpha` | `buildingPipe.cpp:214` | `:259` | same |
| Dual-pass `ZWRITE`/`ALPHATESTFUNCTION` round-trip | `main.cpp:112-155` | `main.cpp:127-170` | same |
| Grass cull save/restore (cache only) | `main.cpp:267-299` | `main.cpp:289-321` | same |
| `envmap.cpp` sets `CULLBACK` for its own pass (L1 only) | `envmap.cpp:209,232` | same | same |
| Moon separate-alpha (set enable+3 factors, clear enable only) | `main.cpp:696-705` | `main.cpp:773-781` | same |
| Full `D3DSBT_ALL` scoped save/apply around *own* postfx passes | — | — | `src/mta/SkyGfxMTA.cpp:57-84`, used `:295,441,579` |

Consequences:

1. The doctrine's *diagnosis* is real: upstream's own postfx leaves `CULLNONE`,
   `BLENDOP=REVSUBTRACT`, `BLENDFACTOR` and `ALPHATESTENABLE` behind
   (junior `postfx.cpp:310-371,438-487`; its only "restore" is the game's 10-state
   `ImmediateModeRenderStatesReStore`). So **some** three-layer repair is justified.
2. The doctrine's *response* is over-broad. `pipeApplyWorldBaseline` writes **12
   states × 3 layers × 4 frame boundaries** (48 state words per state per frame),
   and `pipeEnterCullMode` re-asserts whatever the last pass left. Both write states
   aap/junior never write, at points where the game has already authored state.
3. The 3-layer primitives themselves (`pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set`)
   are sound and should be kept; the *policy* layered on them is the regression.

**Keep:** park-set primitives, the exit restores, `pipeUploadPBR`, `pipeUploadMatCol`
(except the alpha-16 clamp gating), the light uploaders, `ambientMult=1.0`, the
building ambient source. **Gate/delete:** frame baseline, cull preserve-at-entry,
alpha-test enable at entry, the additive→normal-alpha substitution, the ped ambient
floor, and the night crush in `ClampAmbientToSky`.

## 1. State-Helper Inventory (ours vs aap/junior equivalents)

All line numbers are ours unless prefixed by a reference tree. "None" means no
function of any name, and no inline equivalent, exists in `skygfx_original` or
`skygfx_junior`.

| Helper | file:line | Purpose (one line) | aap / junior equivalent |
|---|---|---|---|
| `pipeRwParkSet` | `src/render/pipelinecommon.cpp:1130` | L1: `RwRenderStateSet(park)`, then target, so a cache-hit target becomes a real transition. | **None.** |
| `pipeL2ParkSet` | `pipelinecommon.cpp:1137` | L2: same park→target pair through `RwD3D9SetRenderState` (driver cache). | **None.** |
| `pipeL3Set` | `pipelinecommon.cpp:1144` | L3: unconditional raw `IDirect3DDevice9::SetRenderState`, null-guarded. | **None.** MTA writes the device raw but always inside a `D3DSBT_ALL` scoped block (`SkyGfxMTA.cpp:245-372`). |
| `pipeForceCullMode` | `pipelinecommon.cpp:1201` | Park+set cull on L1/L2, then pin L3; sanitizes OOD to BACK. | **None.** `envmap.cpp:209,232` sets `CULLBACK` on L1 only; grass save/restores L1 (`main.cpp:289-321`). |
| `pipeEnterCullMode` | `pipelinecommon.cpp:1245` | Read raw cull, sanitize, **re-force the sanitized saved value** on all 3 layers; returns raw saved. | **None.** |
| `pipeExitCullMode` | `pipelinecommon.cpp:1282` | Re-force the saved cull through all 3 layers. | **None.** |
| `pipeCullScopeBegin` | `pipelinecommon.cpp:1347` | Record a post-span cull mode (no writes), nesting ≤8. **No call site.** | **None.** |
| `pipeCullScopeEnd` | `pipelinecommon.cpp:1365` | Pop and `pipeForceCullMode` the recorded mode. **No call site.** | **None.** |
| `pipeEnterAlphaMode` | `pipelinecommon.cpp:1522` | Snapshot rw+device alpha/blend/test/sepa/colorwrite, then force a canonical block on L1/L2, flush, pin L3. | **None.** Junior's nearest analogue is the per-cb save list in `buildingPipe.cpp:222-227` (it never forces). |
| `pipeExitAlphaMode` | `pipelinecommon.cpp:1746` | Restore each layer its own captured value (faithful restore), then L3 pin. | **None.** Junior restores inline at `buildingPipe.cpp:333-334` and `:320-330`. |
| `pipeForceAlphaBlock` | **not in this file** — `src/render/buildingPipe.cpp:537` (void) / `:543` (with snapshot) | Canonicalise rw-cached alpha block + raw-only sepa/colorwrite/L3 pin. | **None.** |
| `pipeApplyWorldBaseline` | `pipelinecommon.cpp:1974` | 12 states × 3 layers at a frame boundary: blend pair/op, ZWRITE/ZTEST/ZFUNC, VERTEXALPHA OFF, ALPHABLEND OFF, ALPHATEST ON, CULL BACK, sepa OFF, CWR ALL. | **None.** Junior/aap have no frame or effect boundary reset (their postfx only round-trips the game's 10-state Im2D block). |
| `pipeBlendToD3D` | `pipelinecommon.cpp:1442` | rw→D3D blend mapping (identity 1..11, caller fallback outside). | **None** (junior passes raw enums, relying on the identity). |
| `pipeBindNoise` | `pipelinecommon.cpp:2070` | Init + bind/upload the universal noise tile to a sampler stage; uploads c49-c52. | **None.** |
| `pipeUploadMatCol` | `pipelinecommon.cpp:457` | Upload material colour to VS `loc`; **forces alpha 1.0 when `m->color.alpha == 16`** (`:470`). | `skygfx_junior/src/pipelinecommon.cpp:187-197` — same function **without** the alpha-16 clamp; aap identical (`:183-193`). |
| `pipeUploadPBR` | `pipelinecommon.cpp:899` | Upload `c22={gloss,spec,x,y}` and `c23={x,y,z,0}` to PS. | **None** (no PBR pipe upstream). Junior uses inline `fxParams` c1 (`vehiclePipe.cpp:348` area) and `surfProps` c0. |
| `pipeUploadLightColor` / `...PS` | `pipelinecommon.cpp:492` / `:519` | Upload light RGB (gated by `rpLIGHTLIGHTATOMICS`), zero otherwise. | `junior/pipelinecommon.cpp:213-239` — identical, **no NULL guard** (ours adds one). |
| `pipeUploadLightColorForce` / `...ForcePS` | `pipelinecommon.cpp:507` / `:534` | Same, but **ignores** `rpLIGHTLIGHTATOMICS`. | **None.** |
| `pipeUploadLightDirection*` | `pipelinecommon.cpp:546-641` | Direction / PS / local / inverse variants; base variants gated by atomics flag. | `junior/pipelinecommon.cpp:241-293` — subset (no Force/local-inverse combo); identical semantics. |
| `pipeUploadZero` / `...PS` | `pipelinecommon.cpp:478` / `:485` | Upload `{0,0,0,0}`. | `junior/pipelinecommon.cpp:199-211` — identical. |
| `ClampAmbientToSky` | `pipelinecommon.cpp:78` | Outdoor night law: blend ambient toward `ambient*k`, `k=skyL/ambL`, then additive `ambientFloor` lift. | **None.** Junior building ambient is raw `CTimeCycle*LightsMult` (`buildingPipe.cpp:57-59`); vehicle ambient is `pAmbient->color` (`vehiclePipe.cpp:348,808`). |

Supporting helpers not requested but part of the same contract:
`pipeBlendHasRwTwin` (`:1434`), `pipeAlphaFuncToD3D` (`:1493`),
`pipeAlphaBlendTrioL2` (`:1506`), `pipeAlphaTestTrioL2` (`:1514`).
`PipeAlphaState`/`PAACAP_*` live in `src/skygfx.h:1389-1445`.

## 2. Alpha-Mode Enter/Exit Deep Compare

There is **no `pipeAlphaBlock`-style helper in junior.** Junior's whole alpha
contract is per-callback and per-mesh:

- `buildingPipe.cpp:222-227` reads `ALPHATESTFUNCTIONREF`, `ALPHATESTFUNCTION`,
  `SRCBLEND`, `DESTBLEND`, `FOGCOLOR`, `ZWRITEENABLE` (aap `:191-196`);
- `:259` sets `rwRENDERSTATEVERTEXALPHAENABLE = hasAlpha` per mesh (aap `:214`);
- the reflection sub-pass at `:320-330` swaps to `ALWAYS` / `ZWRITE=FALSE` /
  `VERTEXALPHA=TRUE` / `ONE,ONE` / `FOG=0`, draws, then restores all six;
- `:333-334` restores `ALPHAREF`/`ALPHAFUNC` at cb exit.

Nothing else. It never writes `ALPHABLENDENABLE`, `ALPHATESTENABLE`, `BLENDOP`,
`SEPARATEALPHABLENDENABLE`, `COLORWRITEENABLE`, or the alpha-blend factors. The
MTA fork is the only reference that saves/restores *everything* — but only inside
its own postfx pass, via `D3DSBT_ALL` (`SkyGfxMTA.cpp:57-84,295`) — never around
game geometry.

**Semantic difference in one sentence:** reference forks **save what they change
and trust the game as the last writer**; our pair **declares a canonical world
state at every atomic entry**, including states the caller never authored and
has no way to opt out of.

### 2.1 State we force that they do not

| State | Ours (entry) | Layers | Junior/aap |
|---|---|---|---|
| `ALPHABLENDENABLE` | `FALSE` | L2 `:1628`, L3 `:1710` | Never written in geometry; only read (`main.cpp:143`). |
| `VERTEXALPHAENABLE` | `FALSE` | L1 `:1627`, L2 `:1628`, L3 `:1710` | Per-mesh `hasAlpha` only (`buildingPipe.cpp:259`); never a blanket FALSE. |
| `ALPHATESTENABLE` | `TRUE` | L2 `:1670`, L3 `:1716` | Never written in geometry; only the game/postfx write it. |
| `ALPHAFUNC` | cache value (identity LUT) | L2 `:1670`, L3 `:1715` | Never forced globally; per-subpass only. |
| `ALPHAREF` | cache value, **bumped 0→1** | L1 `:1669`, L2 `:1670`, L3 `:1714` | Never forced globally; per-subpass only. |
| `SRCBLEND` | `SRCALPHA` | L1 `:1613`, L2 `:1619`, L3 `:1711` | Only inside the reflection sub-pass (`ONE`), restored (`:323-327`). |
| `DESTBLEND` | `INVSRCALPHA` | L1 `:1614`, L2 `:1619`, L3 `:1712` | Same as above (`ONE`). |
| `BLENDOP` | `ADD` | L2 `:1619`, L3 `:1713` | **Never written in geometry** (postfx only). |
| `SEPARATEALPHABLENDENABLE` | `FALSE` | L2 `:1684`, L3 `:1694` | Only moon (`main.cpp:773-781`), and it never sets factors false. |
| `SRCBLENDALPHA`/`DESTBLENDALPHA`/`BLENDOPALPHA` | `SRCALPHA`/`INVSRCALPHA`/`ADD` | L2 `:1685-1687`, L3 `:1695-1697` | Moon sets `SRCALPHA`/`ZERO`/`ADD`; **never** rewrites them elsewhere — upstream leaves `ZERO` in the slot. |
| `COLORWRITEENABLE` | `ALL` | L2 `:1688`, L3 `:1698` | Never written. |
| explicit driver-cache flush | `_rwD3D9RenderStateFlushCache()` ×2 per pair | `:1693`, `:1838` | Only `fixSAMP` (`main.cpp:107`). |
| park writes themselves | opposite value before target | every park | No park idiom anywhere. |

Additional net-new behaviour: the entry **keeps the caller's alpha func/ref
semantics** but rewrites `ALPHAREF` in the rw cache when it was 0
(`pipelinecommon.cpp:1664-1670`). Junior never mutates the cached pair, so any
later `RwRenderStateGet` sees the game's value, not ours.

### 2.2 State they restore that we do not

| State | Junior restore | Ours |
|---|---|---|
| `FOGCOLOR` | saved `buildingPipe.cpp:226`, restored `:326` | **Never touched** by the alpha pair. |
| `ZWRITEENABLE` | saved `:227`, restored `:327` around reflection/dual pass | **Not in the pair.** Enumeration in `PipeAlphaState` (`skygfx.h:1398-1420`) has no ZWRITE. It is handled by the separate Switch guard (`buildingPipe.cpp:3041-3053`) — so the *coverage* exists, but not in the enter/exit contract. |
| texture + stage 1-3 state | restores texture, `D3DTSS_*` stage 1 at `:335-339` | Done in `buildingPipe_cleanup`, outside the pair. |
| real per-draw `ALPHAFUNC`/`ALPHAREF` after each sub-pass | yes (`:320-330`) | Only restored once at pipe exit; inside the span the last mesh's value wins. |

Conversely, we restore one state junior leaves dirty: `VERTEXALPHAENABLE`
(exit `:1755`). That is stricter, not a regression.

**Verdict on the pair:** the *restore* half is better than upstream. The *entry*
half is the over-force: 9 of the 11 states it writes are states junior never
writes, and the two it must write (`SRC/DST` for the pipe's own shader contract)
are already written per-mesh by the callbacks. Gate the entry block behind a
"drift detected" predicate, or delete the raw-only members.

## 3. Frame-Boundary Baseline Compare

**Answer: no reference tree has anything like `pipeApplyWorldBaseline`.**
aap/junior never reset ZWRITE/ZTEST/BLEND/CULL/ALPHATEST at frame start or at an
effect boundary. What they do instead:

- `postfx.cpp` uses the game's own `CPostEffects::ImmediateModeRenderStatesStore()`
  / `Set()` / `ReStore()` around each pass (`junior/postfx.cpp:307-310,331,
  438-441,484-487`) — a 10-ID saved block, scoped to the pass, and it explicitly
  re-enables `ALPHATESTENABLE=TRUE` at the end (`:371,487`). It does not restore
  `CULLMODE` itself (upstream leak), which is the *only* real desync source in
  aap/junior.
- MTA wraps each of its own postfx draws in `ScopedDeviceState` (`D3DSBT_ALL`,
  `SkyGfxMTA.cpp:57-84`), i.e. full save/apply around a **known producer**, never
  a global reset.
- Grass and the peds only toggle the states they immediately use and restore them
  (`main.cpp:289-321`, `:127-170`).

Our call sites (default **on**: `stateBaselineEnable=1`, `main.cpp:6513`):

| Boundary | Call site | Why it is risky |
|---|---|---|
| Scene start | `main.cpp:3760` | Runs before the world; forces CULL=BACK and ALPHATEST=TRUE on both caches+device. |
| RenderEffects exit | `main.cpp:4268` | Runs **mid-frame**, immediately before sprite/HUD producers. |
| PostFX exit | `main.cpp:4297` | Same, after the last postfx pass. |
| DrawFinalEffects end | `main.cpp:7739` | Leaves the forced block as the next frame's inherited state. |

Baseline body: `pipelinecommon.cpp:1975-2049`. It writes, per state, L1
(`RwRenderStateSet`) → L2 (`RwD3D9SetRenderState`) → flush (`:2035`) → L3 raw
(`:2038-2048`).

### 3.1 Baseline writes that can clobber authored state

| Baseline write | Line(s) | Authored state it can overwrite |
|---|---|---|
| `ALPHABLENDENABLE=FALSE` (L2 target-only + L3) | `:2029`, `:2038` | Additive sprite/particle producers (coronas, point lights, clouds) that set `ONE/ONE` on L1 and rely on the device already being on. This is the documented night-lamp mechanism: normal-alpha writes the black RGB surround of a glow texture (`regression-attribution.md` §2.1). |
| `ALPHATESTENABLE=TRUE` (park FALSE→TRUE, L2+L3) | `:2026-2027`, `:2048` | Passes that deliberately run with the test off (decals, glass, UI, postfx). The baseline **deliberately does not set `ALPHAFUNC`/`ALPHAREF`** (`:1968-1972`), so it enables the test with whatever stale func/ref is live — e.g. `LESS@140` left by the envmap outdoor pass → quads cut to nothing, or `GREATEREQUAL@1` → alpha-0 texels discarded where a pass wanted them. |
| `CULLMODE=BACK` (L1 park FRONT→BACK, L2 park CCW→CW, L3 CW) | `:2005-2006`, `:2031-2032`, `:2045` | Mirror pass (`NONE`) and ped/SAShadow volumes (`FRONT`). Because **L1 is written too**, the game's later L1-only cull restore is a no-op and the forced BACK persists — the mirror `NONE` leak documented in `angle-bugs-oracle.md:345-346` becomes a *forced* inversion instead of a leak. |
| `ZWRITE=TRUE`, `ZENABLE=TRUE`, `ZFUNC=LESSEQUAL` | `:1997-2000`, `:2015-2020`, `:2042-2044` | Any 2D/sprite batch that expects depth write off and only re-sets some of the trio. Im2D usually sets its own, but raw exporters (CoronaWindow, custom UI) do not. Also re-enables depth writes on the frame's cleared buffer at EffectsExit, before HUD. |
| `COLORWRITEENABLE=0→0xF` (L2+L3) | `:2024-2025`, `:2047` | Passes that mask channels (moon/radiosity/masks). Re-enabling all channels writes RGB into a masked target. |
| `SEPARATEALPHABLENDENABLE=FALSE` (L2+L3) | `:2023`, `:2046` | Moon (`main.cpp:773-781`) enables it and restores only the enable bit; the baseline further rewrites the factors to SRCALPHA/INVSRCALPHA, so a later moon pass that relies on `DESTBLENDALPHA=ZERO` inherits `INVSRCALPHA` if it sets only the enable. |
| `SRCBLEND/DESTBLEND` + `BLENDOP=ADD` | `:1992-1995`, `:2009-2014`, `:2039-2041` | Any producer that authored `ONE/ONE` or `BLENDFACTOR/INVBLENDFACTOR` (radiosity, blur, additive coronas) and does not re-author **all** of pair+op. |
| `VERTEXALPHAENABLE=FALSE` (L1+L2+L3) | `:2003`, `:2029` (via ALPHABLENDENABLE) | Per-mesh `hasAlpha` is set after this inside a pipe, but the boundaries at EffectsExit have no per-mesh re-author → transparent geometry left with blending off. |

**Net:** 4 of the 6 writes the task calls out (`ALPHABLENDENABLE=FALSE`,
`ZWRITE=TRUE`, `ALPHATESTENABLE=TRUE`, `CULLMODE=BACK`) are exactly the ones with
no upstream precedent, and all four run **mid-frame** at EffectsExit twice. The
baseline is the single widest policy delta in the fork.

## 4. Ambient Handling Compare

Reference sources (verified):

| Pipe | aap | junior | ours |
|---|---|---|---|
| Building | `CTimeCycle_GetAmbient{R,G,B}() * CCoronas__LightsMult`, refreshed per frame in `CustomBuildingPipeline__Update` (`buildingPipe.cpp:49-51`; comment `:47`: "do *not* use pAmbient light") | identical (`buildingPipe.cpp:57-59`, same comment) | `buildingAmbient = GetTimecycleAmbientPBR()` (`buildingPipe.cpp:183`), i.e. `CTimeCycle * LightsMult * ambientMult` then clamp+floors (`pipelinecommon.cpp:171-234`) |
| Vehicle (PS2/Xbox) | `pipeUploadLightColor(pAmbient, REG_ambient)` (`vehiclePipe.cpp:348`) and `LOC_sunAmb = pAmbient->color` (`:808`) | identical | PBR: `GetTimecycleAmbientPBR()` (`vehiclePipe.cpp:540,3121,3439`); other modes `GetTimecycleAmbient()` (`:1521`); neoCarpipe `:559` |
| Peds | game's own `pAmbient` | game's own `pAmbient` | **replaced** with `max(pAmbient, timecycle)` + hue floor (`main.cpp:2063-2139`) |

So the *source* of building ambient matches upstream exactly (timecycle ×
LightsMult), and `ambientMult=1.0` restores that parity (the old hard-coded 0.85
was a −15% deviation with no upstream basis). The deviations are all **post-hoc
laws** applied on top.

### 4.1 Per-mechanism judgement (regression vs fix)

| Mechanism | Line | Upstream analogue | Judgement |
|---|---|---|---|
| Building ambient = `CTimeCycle * LightsMult * ambientMult` | `pipelinecommon.cpp:189-191`; `buildingPipe.cpp:183` | exact (`junior/buildingPipe.cpp:57-59`) | **Fix / keep.** This is the reference formula. |
| `ambientMult` default `1.0` (`0.85` before) | `:187`, parse `main.cpp:6597-6599` | none (implicit 1.0) | **Fix.** Restores reference energy; live INI is 1.0. |
| Sky-luminance night law (`ambL>skyL` → `c *= 1 - nw*(1-k)`) | `:112-142` | **none** — reference never compares ambient to sky | **Look regression.** At night `skyL << ambL`; e.g. `k=0.2 → s=0.388`, i.e. a 61% ambient cut, then the 0.03 floor lifts it back to grey. This is the "dark at night, flat at dawn" class. **Delete the crush**, keep a no-op threshold at most. |
| Interior early-return (`CGame__currArea != 0` → unclamped) | `:91-93` | unnecessary upstream (no clamp) | **Fix, but contingent.** Correct while the clamp exists; delete together with the crush, otherwise it is dead weight. |
| Absolute `ambientFloor` default `0.03` (additive lift) | `:143-168`, parse `main.cpp:6537-6539` | **none** | **Gate.** Prevents true-black night, but it is a global grey lift on every ambient-only pipe → pastel shadows. Default it to 0 (or 0.01) and keep as an INI escape hatch. |
| `pbrAmbientFloor` default `0.0` | `:205-223`, parse `main.cpp:6587` | none | **Keep.** Inert by default; opt-in only. |
| PS2 vehicle source switched from `pAmbient->color` to timecycle | `vehiclePipe.cpp:1521` etc. | `pAmbient->color` | **Gate.** `pAmbient` already contains the game's LightsMult *and* interior/timecycle state; re-deriving it is only safe if the clamp/floors are off. Compare per-pipe before keeping. |
| Ped `pAmbient` override (`max(game,tc)` + `pedAmbientFloor=0.20`) | `main.cpp:2063-2139` (floor `:2110-2123`) | **none** — reference uses the game's `pAmbient` untouched | **Regression (white peds).** Peds are FFP: `final ≈ pAmbient + pDirect`; raising ambient to 0.20 with `pDirect≈1.0` saturates. Default `0.20` (`main.cpp:6592`, `:7049`) makes this active for every user. **Delete the floor** (default 0); keep the override only if `max(game,tc)` is proven necessary. |

Bottom line for task 4: **two mechanisms are fixes** (building source + ambientMult
1.0), **one is a contingent fix** (interior guard), and **three are look
regressions** (sky night law, absolute floor default, ped floor). The reference
trees prove none of the three regressions is needed for parity.

## 5. Shader-Constant Uploader Compare

### 5.1 Register-layout drift check

`pipeUploadPBR` (`pipelinecommon.cpp:899-933`) uploads exactly two float4s:
`c22 = {glossiness, specular, c22_3, c22_4}` and `c23 = {c23_1, c23_2, c23_3, 0}`.
Checked against the shader declarations it feeds:

| Pipe | Shader declaration | C++ call site | Match |
|---|---|---|---|
| Vehicle | `pbrParams : register(c22)` = `{gloss, spec, specTint, metallicness}`; `paintNoise : register(c23)` = `{wheel, noiseScale, edgeBlend}` (`shaders/ps/VehiclePBR_Modern.hlsl:39-40`) | `vehiclePipe.cpp:3333` (`gloss, spec, tint*(1-metallic), metallic, ...`); `:3090` tires; `:2916/:2930` lights | **Yes** |
| Building | `c22 = {glossiness, reflectance, clearcoat, subsurface}`; `c23 = {specTintR, specTintG, specTintB, 0}` (`VehiclePBR_Modern.hlsl:974-976`, read `:1002,1016`) | `buildingPipe.cpp:2914` | **Yes** |
| Rubber | tire param comments `c22={roughness,F0,tintR,tintG}`, `c23={tintB,dirt,wear,0}` (`:1280-1281`) | `vehiclePipe.cpp:3090` | **Yes** |

No drift. Note the layout is *not comparable to junior*, because junior has no PBR
pipe; its closest uploads are the per-pipe `fxParams` c1 and `surfProps` c0
(`junior/vehiclePipe.cpp:348`, `junior/buildingPipe.cpp:263`), which our PBR path
also still uses for the non-PBR legs. `c22/c23` sit in the PBR-only namespace and
do not collide with them (`buildingPipe.cpp:312` documents the split).

The **only** uploader with a semantic delta is `pipeUploadMatCol` (below). The
light uploaders are byte-identical to junior for valid lights:

- `pipeUploadLightColor/PS` — same `rpLIGHTLIGHTATOMICS` gate and `{r,g,b,1}`
  payload (`ours :492-533`, `junior :213-239`); ours adds a NULL guard
  (`:494`, `:509`), which is hardening, not a look change.
- `pipeUploadLightDirection/PS` — same `RwFrameGetLTM` at-vector payload
  (`ours :546-611`, `junior :241-269`).
- `pipeUploadLightDirectionLocal/Inv` — identical (`ours :612-641`,
  `junior :271-293`).
- Our `*Force*` variants (`:507,534,564,597`) bypass the atomics gate. Junior has
  no equivalent; they are a new API, not a drift of the shared ones.

### 5.2 Alpha / prelight clamps upstream does not apply

| Clamp | Ours | Junior/aap | Verdict |
|---|---|---|---|
| **Material alpha byte 16 → 1.0** in `pipeUploadMatCol` | `pipelinecommon.cpp:470-471` | raw pass-through (`junior/pipelinecommon.cpp:191-194`) | **Gate.** The paint-marker rationale is valid for vehicle paint (`m->color.alpha == 16` means "use car colour"), but the function is shared by buildings, peds and props. Any non-vehicle material whose authored alpha happens to be 16 becomes fully opaque. Gate the clamp on the vehicle pipe (pass a flag or clamp in the vehicle call site), not globally. |
| **`ALPHAREF 0 → 1`** (alpha-block entry, not an uploader) | `pipelinecommon.cpp:1664-1670`, mirrored in `pipeForceAlphaBlock` `buildingPipe.cpp:574-575` and `:704-705` | never applied | **Keep** for the degenerate `GREATEREQUAL@0`, but note it mutates the rw cache so a later `RwRenderStateGet` no longer returns the game's value. The standalone `pipeForceAlphaBlock` call sites are where this matters. |
| **Prelight clamps** | none in `pipelinecommon.cpp`. The real ones are `buildingPipe.cpp:2822` (`surfUpload[2]=1` when diffuse≈0), `buildingPipe.cpp:2803-2817` and `vehiclePipe.cpp` `vehPrelightFallback` | none | **Out of scope for the uploader**, but relevant to washout: upstream building VS uses `prelight*surfDiff` with no fallback injection. Treat those as look-constant suspects, not doctrine. |
| **Ambient clamps** | `ClampAmbientToSky` + floors | none | See §4. |

No other numeric clamp or prelight rewrite exists in the uploader layer — the
`pipeUploadPBR` payload is passed straight through with no saturation.

## 6. Ranked Top-5 Doctrine Suspects

Ranked by (a) how many of the four symptoms it can produce, (b) how far it is from
every reference tree, (c) how cheap the A/B is. "Doctrine" = states/forcing the
fork invented; look-constant items already ranked in
`docs/plans/2026-10-26-regression-attribution.md` are excluded except where the
doctrine amplifies them.

| # | Suspect | Exact site | Symptoms | Mechanism | Verdict |
|---:|---|---|---|---|---|
| 1 | `pipeApplyWorldBaseline` | `src/render/pipelinecommon.cpp:1975-2049`; calls `src/core/main.cpp:3760, 4268, 4297, 7739` | **Night light quads**, ordering, **washout** | 12 states × 3 layers, 4×/frame. `ALPHABLENDENABLE=FALSE` + `ALPHATESTENABLE=TRUE` (with inherited func/ref) + `CULL=BACK` + `ZWRITE/ZENABLE=TRUE` + `COLORWRITE=ALL` + `SEPARATEALPHA=FALSE` all run at EffectsExit, i.e. immediately before the sprite/HUD phase. No reference tree has any frame/effect boundary reset. | **GATE (default off), then delete.** Nothing in aap/junior needs it; the park-set primitives already repair a proven drift at the pipe, where the caller owns the state. |
| 2 | `effectsForceAlphaBlock` additive→normal substitution + `pipeForceAlphaBlock` | `src/core/main.cpp:3974-4097` (substitution `:4049-4097`); `src/render/buildingPipe.cpp:543-734` (`ALPHATESTENABLE=TRUE :601`, sepa/cwr `:610-614`) | **Night light quads** | Rewrites an authored `ONE/ONE` pair to `SRCALPHA/INVSRCALPHA` and pins `test ON/op ADD/CWR ALL` on L2/L3 at RenderEffects enter/exit and PostFx exit. Glow textures are black-RGB + opaque alpha, so normal alpha paints the black rectangle. The producer gate (`g_inCoronasRender`, `s_spriteForceValid`, `:4079-4081`) does not cover the fog-cone/`CPointLights` path or a late corona batch flush. | **DELETE the substitution; KEEP `pipeForceAlphaBlock` for the standalone skidmark/shadow callers.** Never rewrite an authored additive pair. |
| 3 | `pipeEnterCullMode` (preserve-saved policy) + the baseline's `CULL=BACK` | `src/render/pipelinecommon.cpp:1245-1280` (force `:1278`), `pipeForceCullMode:1201-1243`, baseline `:2005-2006, 2031-2032, 2045` | **Low-angle face inversion** | Entry forces the *sanitized saved* cull (could be `NONE` from the mirror or `FRONT` from a shadow volume) on all three layers. At grazing angles a `FRONT` span culls the front face; a `NONE` span draws both and the back face wins at shallow depth. The baseline then re-forces `BACK` at boundaries, but because it writes L1 too, the game's later L1-only restore no-ops (mirror `NONE` leak becomes sticky). Reference trees never force cull at pipe entry (`envmap.cpp:209,232` only for its own pass; grass save/restores L1). | **GATE.** Force canonical `BACK` at pipe entry unless a `pipeCullScopeBegin(BACK/FRONT)` explicitly owns the span (the scope pair already exists, `:1347-1386`, and is unused). Keep the exit restore. |
| 4 | Ped `pAmbient` override + `pedAmbientFloor=0.20` | `src/core/main.cpp:2063-2139` (override `:2069-2139`, floor `:2110-2123`), default `:6592`, `:7049` | **White peds** | Replaces the game's `pAmbient` with `max(game, timecycle)` and then scales the peak channel up to 0.20. Peds are FFP (`final ≈ pAmbient + pDirect`), so with `pDirect≈1.0` the sum clips to white outside CJ's shadow. Reference forks never override `pAmbient`; the game's ambient already includes `LightsMult`. | **DELETE the floor** (default 0.0); keep the override only as an opt-in and re-test `.0` first — one INI key confirms it. |
| 5 | `pipeEnterAlphaMode` raw-only block | `src/render/pipelinecommon.cpp:1522-1744` (sepa/cwr `:1684-1698`, `ALPHABLENDENABLE=FALSE :1628/:1710`, `BLENDOP=ADD :1619/:1713`, `ALPHATESTENABLE=TRUE :1670/:1716`) | **Washout**, sprite ordering | Forces 9 states junior never writes. `SEPARATEALPHABLENDENABLE=FALSE` + `DESTBLENDALPHA=INVSRCALPHA` changes how destination alpha is recomposited (the postfx chain reads that channel); `COLORWRITEENABLE=ALL` unmasks channels a pass masked; `BLENDOP=ADD` overwrites radiosity's `REVSUBTRACT`; `ALPHATESTENABLE=TRUE` is applied with the *caller's* func/ref. | **GATE.** Trim the entry to the two states the pipe shader contract actually needs (`SRC/DST`) plus the `ALPHAFUNC/REF` alignment; move sepa/cwr/blendop/test into `pipeForceAlphaBlock` callers that can prove drift. |

**Not in the top 5 but same family:** `ClampAmbientToSky`'s night crush
(`pipelinecommon.cpp:112-142`) is a doctrine-level look regression, but it is a
night/dawn luminance law, not a direct cause of the four listed symptoms; delete it
together with the ped floor in the same pass (§4). Likewise `pipeCullScopeBegin/End`
is dead code (`:1347-1386`) and should either be wired to the mirror hook or removed.

## Appendix A — Reference File Index

| Tree | Path | Role |
|---|---|---|
| aap upstream (`skygfx_original`) | `src/pipelinecommon.cpp` (346 L) | uploaders + matrices only; **no state helpers** at all |
| | `src/main.cpp` (1733 L) | `D3D9RenderDual` `:112-155`, grass cull `:267-299`, moon sepa `:696-705` |
| | `src/buildingPipe.cpp` | ambient `:49-51`; cb save `:186-191`; per-mesh alpha `:214`; reflection restore `:257-267`; cb exit `:270-271` |
| | `src/vehiclePipe.cpp` | `pAmbient` ambient `:348`, `:808`; `pipeUploadMatCol` callers `:444,619,1015,1156,1363` |
| Junior (`skygfx_junior`) | `src/pipelinecommon.cpp` (389 L) | uploaders only; `pipeUploadMatCol:187-197` (no alpha clamp) |
| | `src/main.cpp` (1935 L) | `D3D9RenderDual:127-170`, grass `:289-321`, moon `:773-781` |
| | `src/buildingPipe.cpp` | ambient `:57-59`; cb save `:222-227`; per-mesh alpha `:259`; reflection restore `:320-330`; cb exit `:333-334` |
| | `src/postfx.cpp` | scoped Im2D save/restore `:307-371`, `:438-487` (the only "boundary" logic) |
| MTA (`skygfx`) | `src/pipelinecommon.cpp` (346 L) | identical to aap |
| | `src/mta/SkyGfxMTA.cpp` | `ScopedDeviceState` = `CreateStateBlock(D3DSBT_ALL)` `:57-84`; used `:295,441,579`; raw state writes `:245-372` |
| aap binary (`skygfx_aap_v4.2b`) | `SkyGfx_SA_4.2b/` | **binary release only** (`skygfx.asi`/`skygfx.dll`, 2020-04-30), no source; `skygfx_original` is the matching aap source |
| ours | `src/render/pipelinecommon.cpp` (2112 L) | doctrine §1 |
| | `src/render/buildingPipe.cpp` | `pipeForceAlphaBlock:537-734`; pbr alpha re-force `:2277-2325` |
| | `src/core/main.cpp` (8625 L) | ped contract/ambient `:940-2260`; effects block `:3968-4097`; baseline calls `:3760,4268,4297,7739` |
| | `src/render/postfx_utils.cpp` (338 L) | raw geom save/restore + 3-layer restore `:177-338` |

Method: source-only comparison; no build, no log, no source edits. Line numbers
are from the trees as of 2026-10-26 and will drift with edits.
