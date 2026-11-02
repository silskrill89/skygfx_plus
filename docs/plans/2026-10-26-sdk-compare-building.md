# SDK Compare — Building Pipe (2026-10-26)

Status: FINAL (read-only audit; no source edits)

## 0. Scope & Method

Compared:

| Tree | File | Lines |
|---|---|---|
| aap original | `/e/SDKs/skygfx_original/src/buildingPipe.cpp` | 818 |
| junior fork | `/e/SDKs/skygfx_junior/src/buildingPipe.cpp` | 916 |
| MTA fork | `/e/SDKs/skygfx/src/buildingPipe.cpp` | 818 |
| ours | `src/render/buildingPipe.cpp` (+ `src/render/pipelinecommon.cpp`) | 3495 (+2112) |

`diff -q` proves the MTA tree's `buildingPipe.cpp` is **byte-identical** to the aap original; every "aap" line number below therefore applies to MTA too. Runtime side-files used only to weight the suspect list: `skygfx.ini` and `skygfx_dbg.log` (04:34 window).

Method: literal audit of every render-state write/read in the four building files, plus `pipelinecommon.cpp`/`main.cpp` helpers the building path calls (`pipeEnter/ExitCullMode`, `pipeEnter/ExitAlphaMode`, `pipeForceAlphaBlock`, `D3D9RenderDual`, `fixSAMP`). Line numbers are line numbers in this checkout, not stable APIs. "Saves" means reads into a per-callback snapshot; "sets" means writes that can reach the device; "restores" means writes that put a captured value back.

Runtime snapshot (important for ranking):

- `skygfx.ini`: `buildingPipe = Xbox` (line 26), `vehiclePipe = Xbox` (229), `dualPassBuilding = 0` (49), `ivMode = 0` (101), `forceWindShader = 0` (64), `texAlphaClassify = 1` (197), `stateBaselineEnable = 1` (186).
- `skygfx_dbg.log`: `[BPIPE] cb=Xbox ... pipe=XBOX(1)` in the main stream and `[BPIPE] cb=Sphere ... pipe=XBOX(1)` for the 512x512 env capture; `[Cull] enter saved=2` and `[Cull] enter saved=1` both occur, and `[Cull] exit restored=1` is logged every ~2 s; `[BldZW] entry rw=1 drv=1 dev=1 -> restore=verbatim` (the new latch is **not** firing in this run); the 16 sampled `[BldXbox]` meshes are all `tac=1(OPAQUE) blendOn=0 matA=255`; `[PipeAlpha] entry resync` shows `fn=5 (GREATER) ref=2` and `[PipeAlpha] faithful restore ... enable L2=1 L3=0`. Logs are throttled, so these are samples, not per-atomic proof.

## 1. Task 1 — Per-mesh / per-callback render-state contract (four-tree side-by-side)

### 1.1 State save/set/restore table

| State | aap original (== MTA) | junior | ours |
|---|---|---|---|
| **CULLMODE** | Never saved, set or restored. Relies entirely on game/device state. | Never saved, set or restored. | Switch: `savedCull = pipeEnterCullMode()` (`buildingPipe.cpp:3018`; save L1 `pipelinecommon.cpp:1251`, force L1/L2/L3 `pipelinecommon.cpp:1201-1243`), static hand-off `:3022`, exit `pipeExitCullMode(savedCull)` `:3143` (`pipelinecommon.cpp:1282-1300`). Xbox re-asserts saved value at draw boundary `:1822`. PBR hard-codes `rwCULLMODECULLBACK` `:2318`. Sphere does **not** re-assert. |
| **ZWRITE** | PS2 cb saves rw value once (`buildingPipe.cpp:191`), sets FALSE only for the env/reflection leg (`:257`), restores (`:264`). Xbox and Sphere never touch ZWRITE. | PS2 save `:227`, FALSE `:320`, restore `:327`. Xbox and Sphere never touch ZWRITE. | Switch captures all 3 layers (`:3041-3047`), forces TRUE (`:3053-3056`), restores verbatim or canonical TRUE on desync (`:3132-3141`). PS2 env leg save/FALSE/restore L1+L2+L3 (`:1475-1476`, `:1506-1507`, `:1532-1533`, raw L3 `:1549`). Xbox decal policy FALSE/TRUE 3 layers (`:1921-1946`). PBR re-forces TRUE after IBL (`:2311-2312`) and decal policy (`:2681-2706`). |
| **ZENABLE / ZFUNC** | Never saved, set or restored. | Never saved, set or restored. | Never saved/set/restored **in the building path**. Frame baseline only (`pipelinecommon.cpp:2017-2020`, `2043-2044`), and it is config-gated (`stateBaselineEnable`, `main.cpp:3759/4267/4296`). |
| **ALPHABLENDENABLE** | No direct `D3DRS_ALPHABLENDENABLE` write. Enables only via rw `VERTEXALPHAENABLE` per mesh (`:214`, `:341`, `:456`) and the reflection leg (`:258`). | Same: rw VERTEXALPHA per mesh (`:259`, `:422`, `:554`), reflection `:321`. | Switch `pipeEnterAlphaMode` sets L2+L3 FALSE (`pipelinecommon.cpp:1627-1628`, `1710`); `pipeForceAlphaBlock` re-pushes from rw cache (`buildingPipe.cpp:596`, raw `:643-649`); per-mesh L2 mirror on Xbox `:1909`, Sphere `:2148`, PBR `:2766`; **PS2 has no L2 mirror** (only rw `:1372`); PS2 env L2 `:1509/1543`; leak probe `:937-942`. |
| **SRCBLEND / DESTBLEND** | PS2 cb saves (`:188-189`), env leg sets ONE/ONE (`:259-260`), restores (`:265-266`); `TagRenderCB` sets ONE/ZERO for PS2 tags and restores SRCALPHA/INVSRCALPHA (`:129-138`). | PS2 save `:224-225`, ONE/ONE `:322-323`, restore `:328-329`; Tag `:152-161`. | Snapshot L1 (`:426-427`), restore L1 (`:447-448`); Switch canonicalises (`pipelinecommon.cpp:1613-1619`); `pipeForceAlphaBlock` L2/L3 (`buildingPipe.cpp:584-585`, `594-595`); PS2 env L1/L2/L3 set+restore (`:1510-1513/1534-1537`, L3 `:1550-1551`); per-mesh L2 mirror only where `ALPHABLENDENABLE` is mirrored. |
| **ALPHATESTENABLE / ALPHAFUNC / ALPHAREF** | `TagRenderCB` saves/restores rw alpharef (`:127`, `:139`). PS2 cb saves rw ref/func (`:186-187`), sets ALWAYS for env (`:256`), restores (`:267`, `:270-271`). **ALPHATESTENABLE is never written** by any callback. | Same (`:150-162`, `:222-223`, `:319`, `:330`, `:333-334`). No enable write. | Snapshot (`:424-425/445-446`); Switch `pipeEnterAlphaMode` captures rw func/ref and L2/L3 enable, forces test TRUE, func mapping, `ref 0->1` (`pipelinecommon.cpp:1664-1670`, L3 `:1714-1716`); `pipeForceAlphaBlock` forces test TRUE + func/ref/ref bump (`:592-601`, raw `:637-649`, `678-725`); PS2 env ALWAYS L1+L2+L3 (`:1503-1505`, restore `:1538-1539`); `TagRenderCB` `:325-337`; `D3D9RenderDual` re-writes func/ref both legs (`main.cpp:232-239`, `287-298`). |
| **VERTEXALPHAENABLE** | Per-mesh rw set (`:214`, `:341`, `:456`); env leg TRUE (`:258`) and **not restored**. | Per-mesh rw set (`:259`, `:422`, `:554`); env TRUE `:321`, **not restored**. | Per-mesh rw set, elided when unchanged: PS2 `:1371-1372`, Xbox `:1903-1910`, Sphere `:2144-2148`, PBR `:2759-2767`. PS2 env snapshots and restores (`:1475-1476`, `:1542-1543`). Xbox/Sphere/PBR exit restore L1+L2+L3 (`:2027-2030`, `:2171-2174`; PBR relies on `rs` `:2951`). |
| **FOGCOLOR / FOGENABLE** | PS2 saves FOGCOLOR (`:190`), env leg sets 0 (`:261`), restores (`:263`). FOGENABLE never touched. | PS2 save `:226`, 0 `:324`, restore `:326`. FOGENABLE never touched. | Snapshot/restore FOGCOLOR (`:428/449`); PS2 env 0 L1+L2+L3 and restore (`:1514-1515/1530-1531`, L3 `:1554`); `fixSAMP()` syncs FOGENABLE/FOGTYPE only when `fixingSAMP` (`main.cpp:185-204`, called at Switch `:3144`). No FOGENABLE force. |
| **TEXTURERASTER per stage** | Uses `RwD3D9SetTexture`: s0 every mesh (`:199`), s1 env (`:243`), s2 detail (`:227`), s1 NULL at exit (`:272`). No `rwRENDERSTATETEXTURERASTER` write. | s0 `:235`, s1 `:306`, s2 `:278`, s1 NULL `:335`. No rw raster write. | All building binds go through `pipeSetTexture` (`:1173`, detail `:965`, env `:1442/1480/1991`); exit binds `gpWhiteTexture` to s1/s2 (`:381-382`) plus TSS disable; PBR raw s3/s4/s5/s8 binds (`:2467-2478`, `:2532`, `:2724`) and dual unbind (`:2963-2979`). No rw raster write. |
| **Texture address / filter** | Env leg sets rw `TEXTUREADDRESS=WRAP` (`:242` PS2, `:358` Xbox) and never restores. No explicit filters. | Same: `:305`, `:439`, never restored. No filters. | L1 address + L3 sampler U/V for stages 0-2 saved (`:430-438`) and restored (`:451-456`); env WRAP `:1477/1988`; PBR s3 CLAMP/LINEAR (`:2468-2478`), s8 WRAP/LINEAR (`:2729-2733`), exit resets s3-s5 (`:2984-2995`). |
| **COLORWRITEENABLE** | Never touched. | Never touched. | `pipeEnterAlphaMode` forces ALL and restores captured (`pipelinecommon.cpp:1688`, `1698`); `pipeForceAlphaBlock` forces ALL (`buildingPipe.cpp:614`, raw `:649`); frame baseline `pipelinecommon.cpp:2024-2025/2047`. |
| **BLENDOP / SEPARATEALPHABLENDENABLE** | Never touched. | Never touched. | `pipeEnterAlphaMode` forces ADD / separate OFF and restores captures (`pipelinecommon.cpp:1619`, `1684-1698`, `1713-1716`); `pipeForceAlphaBlock` forces same (`buildingPipe.cpp:600-614`, `637-649`). |
| **TSS stage 1/2 ops** | Stage 1 disable/texcoord/transformflags at cb exit (`:273-276`, `:385-388`). | Stage 1 same (`:336-339`, `:483-486`). | `buildingPipe_cleanup` disables stage 1 **and stage 2** and resets stage-1 texcoord/transform (`:383-388`); PBR also disables stage 2/3 (`:2999-3002`). |

### 1.2 Notes on save/restore symmetry

1. **Reference trees are deliberately asymmetric.** Only the PS2 callback snapshots anything (6 rw values: alpharef/func, src/dst, fog, zwrite). Xbox saves nothing; Sphere saves nothing; the Switch wrapper does nothing. Every env/reflection push (TEXTUREADDRESS, VERTEXALPHA, SRC/DEST, FOGCOLOR, ZWRITE) leaks out of the aap/junior callbacks. Our tree repairs this with a per-callback `BuildingRenderState` snapshot (`:421-458`) plus the Switch enter/exit contract.
2. **Our PS2 cb is the only callback without an L2 `D3DRS_ALPHABLENDENABLE` mirror** for the per-mesh `hasAlpha` decision (`:1371-1376`). Xbox `:1903-1910`, Sphere `:2144-2148`, and PBR `:2759-2767` all write L1 + L2. This is a real callback asymmetry, not a reference-parity choice: `D3D9RenderDual` reads the L2 value (`main.cpp:259`), so PS2's dual-pass gate can read the Switch baseline instead of the per-mesh decision.
3. **Our PBR snapshot is taken after PBR's own forced entry** (`:2280-2325`, snapshot `:2552-2553`), so its `rs.zwrite` is always TRUE and its `rs` alpha values are post-repair. The Switch wrapper owns the true pre-entry restore (`:3115-3142`); PBR's own restore is only a hand-off, not a caller-state restore.
4. **Sphere has no cull re-assert and no decal policy**, while Xbox has both and PBR has both (with a hard-coded BACK). Sphere inherits the Switch mode (NONE during the env capture), which is correct for capture but means its ZWRITE is always the Switch TRUE.
5. **`TagRenderCB` gating is aap-style, not junior-style.** Ours checks `config->buildingPipe == BUILDING_PS2` (`:326`); junior uses `config->tagsBuildingPipe` (`junior:151`). Under a non-PS2 pipe, junior would still force ONE/ZERO for tag materials; ours would not.

## 2. Task 2 — Extra hardening vs dropped states

### 2.1 States WE force that aap/junior never touch

Severity is for the reported symptom classes: **(a)** back-faces in front, **(b)** wrong order / stacked translucency, **(c)** see-through solid.

| # | Extra state / mechanism | Our evidence | (a) | (b) | (c) | Why it can matter |
|---|---|---|---|---|---|---|
| E1 | **CULLMODE capture + 3-layer force + restore**; Xbox re-assert; PBR hard-coded BACK | `buildingPipe.cpp:3018/3022/3143`, `1822`, `2318`; `pipelinecommon.cpp:1201-1243` | **Yes** | No | Yes | If the captured mode is `NONE` or `FRONT` (sphere/mirror/shadow leak), we *pin* that mode on the raw device. `NONE` draws the inside/back of single-sided walls; `FRONT` culls the visible side and shows only the interior. aap/junior never move layer 3, so such a cache-only leak stays device-BACK. Runtime log shows `[Cull] enter saved=1` mixed into the main Xbox stream. |
| E2 | **ZWRITE forced TRUE on all 3 layers**, plus `zwriteWasDesync` latch | `:3041-3052`, `:3053-3056`, `:3132-3141` | No | **Yes** | Yes | PS2/Sphere have no decal ZWRITE-off path, so blended material meshes write depth and reject later translucent layers. The latch also restores TRUE over a legitimate pre-entry ZWRITE-off pass. In this run `[BldZW]` is clean, so the latch is idle. |
| E3 | **Alpha block force**: ALPHATESTENABLE TRUE, ALPHAFUNC from cache, `ALPHAREF 0->1`, SRC/DEST canonical, BLENDOP ADD, SEPARATEALPHA OFF, COLORWRITE ALL | `pipelinecommon.cpp:1613-1716`; `buildingPipe.cpp:543-734`, `2280-2325`, `3096` | No | **Yes** | **Yes** | PS2/Xbox simple PSes have no `c48.w` alpha-override (PBR does). Test-on + `GREATER@1/2` (runtime shows `fn=5 GREATER ref=2`) discards texels with alpha 0. `texalpha.cpp:1-12` explicitly documents this exact “solid mesh with unused alpha channel is discarded” failure and relies on the PBR `c48.w` fix; `buildingPipePS.hlsl:25-27/91-95` returns texture alpha unchanged, and the Xbox callback only changes the CPU blend bit (`:1871-1873`). So `TAC_OPAQUE` is **not** honoured on Xbox/PS2 <br>— the alpha test still cuts the surface. Blend-family repairs can also turn a previously accidental no-blend into a real blend. |
| E4 | **Per-mesh L2 ALPHABLENDENABLE mirror** (Xbox/PBR/Sphere) | `:1909`, `:2148`, `:2766` | No | **Yes** | **Yes** | This is the correct intent, but it makes the per-mesh decision actually reach the device. Any misclassification (E5) now becomes visible translucency instead of an accidentally opaque draw. PS2 omits it (see E8). |
| E5 | **Per-texture TAC classification + material-translucent “decal” ZWRITE-off policy** | `:1856-1897`, `:2604-2666`, `:1921-1946`, `:2681-2706` | No | **Yes** | **Yes** | `matTranslucent` is `material->color.alpha != 255` with only the `alpha==16` paint marker excluded. A material alpha of 0/garbage is treated as a real translucent decal: blend ON, ZWRITE OFF, no depth. Consecutive such meshes stack in draw order. aap/junior had no such policy. |
| E6 | **PBR per-cb cull is hard-coded BACK, not the captured value** | `:2318` | Yes (mirror/reflection) | No | Yes | During a legitimately `CULLNONE` mirror/env span, PBR culls back faces (the opposite of the span’s intent). Xbox correctly re-asserts `s_bldSwitchSavedCull`; PBR does not. |
| E7 | **Sampler address/filter snapshot + raw s3/s4/s5/s8 management** | `:430-438/451-456`, `:2467-2478`, `:2729-2733`, `:2984-2995` | No | No | Low | State hygiene; a wrong bind/unbind can feed a render target back as a texture (black/undefined samples), but not change face winding. |
| E8 | **PS2 per-mesh blend is L1-only** (the inverse of E4) | `:1371-1376` | No | **Yes** | **Yes** | The Switch baseline is FALSE. If the L1 set does not dirty the L2 state in this RW build, `D3D9RenderDual` sees FALSE and either skips the cutout leg or draws the mesh opaque; if the device was left TRUE by a previous pass, the opaque mesh blends with stale factors. Either direction produces order/see-through artifacts. |
| E9 | **Vertex-alpha three-layer set/restore in Xbox/Sphere/PBR exits** | `:2027-2030`, `:2171-2174`, `:2951` | No | Low | Low | Correctness hardening, but it overwrites the pre-entry device value rather than the captured L2/L3 value in some paths. |
| E10 | **`buildingPipe_cleanup` binds `gpWhiteTexture` instead of NULL** | `:381-382` | No | No | Low | A shader that samples an unbind stage gets white instead of the previous building raster. This can only hide a leak, not create one. |
| E11 | **PBR IBL/`pipeEnsureIBLBuffer` and classify pass before/after state save** | `:2268`, `:2280-2325`, `:2938-2946` | No | Low | Low | The IBL is a fullscreen Im2D pass with raw states; the alpha/cull/zwrite re-force is defensive. The classify draw is a second draw of the same mesh; if the bracket ever fails, it can double-draw/ghost, but the SEH fence closes it (`:2199-2212`). |

### 2.2 States aap/junior set that WE dropped

No reference-set, symptom-relevant state was removed wholesale. The concrete differences are:

1. **Unconditional per-mesh `VERTEXALPHAENABLE` write** (aap `:214/341/456`, junior `:259/422/554`) is elided when the value equals the previous mesh (`buildingPipe.cpp:1371-1375`, `1903-1912`, `2759-2769`). Net state is the same *if* the caches agree; with PS2's missing L2 mirror (E8), an elided write can leave the device at a stale value.
2. **`RwD3D9SetTexture(NULL, 1)` at PS2/Xbox exit** (aap `:272/384`, junior `:335/482`) is replaced by `gpWhiteTexture` (`:381-382`). Deliberate, safer handoff; not a symptom source.
3. **Junior's `tagsBuildingPipe` gate in `TagRenderCB`** (`junior:151`) is dropped; ours uses `buildingPipe` (`:326`). If tag materials are drawn under Xbox/PBR/GTAIV, ours does not force the additive ONE/ZERO tag blend that junior would.
4. **Junior's stochastic PS selection** (`junior:279-292`, `449-454`, `459-472`) is replaced by the noise-detail PS family (`:953-977`). Shader-selection difference, not render-state.
5. Everything else the references “set” (PS2 env ALWAYS/FALSE/TRUE/ONE/ONE/FOGCOLOR 0; per-mesh VERTEXALPHA; stage-1 disable) still exists in ours, usually with additional L2/L3 mirrors and with a real restore instead of a leak. References never set ZENABLE/ZFUNC, FOGENABLE, COLORWRITE, BLENDOP or SEPARATEALPHA at all, so no such state was dropped.

### 2.3 Symptom causation matrix

| Divergence | (a) Back-face in front | (b) Order / stacked translucency | (c) See-through solid | Confidence |
|---|---|---|---|---|
| E1 cull replay/pin | **High** if captured NONE/FRONT; low if always BACK | None | Medium (interior faces + depth writes) | High for mechanism, medium for this run |
| E2 ZWRITE force/latch | None | **High** (PS2/Sphere blended meshes) | Medium | Medium (latch idle in log) |
| E3 alpha test/blend force | None | High | **High** for alpha-0 “solid” textures | High on Xbox/PS2; PBR has c48 mitigation |
| E4/E8 blend-layer asymmetry | None | **High** | **High** | High |
| E5 material-alpha decal policy | None | **High** | **High** if alpha 0/garbage | Medium (only 16 meshes sampled) |
| E6 PBR hardcoded BACK | Medium (mirror only) | None | Medium | High mechanism, dormant (`buildingPipe=Xbox`) |
| E7 sampler/texture state | None | Low | Low | Low |
| ZENABLE/ZFUNC shared gap | **High** if device test off | Medium | Medium | Medium (frame baseline mitigates) |

## 3. Task 3 — Callback-by-callback asymmetry audit

### 3.1 PS2 callback (`buildingPipe.cpp:1198-1573`)

- Entry: frame-null early return before any state write (`:1229-1230`); IV shader decision hoisted (`:1253-1255`); CPU wind tilt only when the IV pair will be used (`:1267-1270`); WVP upload `:1271`; res entry `:1273`; DN/envmat/eye/lights `:1275-1286`; Forward+ only in the disabled branch (`:1315-1318`); then `BuildingRenderState` snapshot L1 (`:1320-1324`) and `TreeAlpha_ApplyAlphaRef` (`:1332`).
- Per mesh: stage0 bind `:1352`, colorScale PS c0/VS c29 `:1354-1355`, UV transform `:1357`, `GetDefinedShader` `:1360`, WIND `hasAlpha = material alpha only` `:1362-1364`, L1-only `VERTEXALPHAENABLE` (elided) `:1371-1375`, matCol/c4/c20 `:1381-1389`, PS selection `:1391-1453`, draw `:1460`, env/FX leg `:1463-1560` (full 3-layer SRC/DEST/ALPHAFUNC/ZWRITE/VERTEXALPHA/FOGCOLOR/sampler restore).
- Exit: `TreeAlpha_RestoreAlphaRef` `:1566`, `buildingPipe_restoreRenderState` `:1567`, `buildingPipe_cleanup` `:1568`, leak probe `:1572`.
- Asymmetries: **no L2 blend mirror**, **no cull re-assert**, **no per-mesh decal ZWRITE policy**, no per-texture TAC (uses aap/junior legacy `vertexAlpha || matA!=255`), and the wind VS is selected per-mesh (unlike Xbox, which selects it once per callback).
- The PS2 env leg is the only place in our tree that snapshots/restores `VERTEXALPHAENABLE` per mesh; it does so because it forces TRUE for the additive reflection. aap/junior forced TRUE and leaked it.

### 3.2 Xbox callback (`buildingPipe.cpp:1674-2038`)

- Entry: Forward+ only when `buildingPipe_consumesForwardPlus()` (`:1728-1730`, false for Xbox) with a one-shot breadcrumb (`:1744-1746`); `BuildingRenderState` + `savedVtxAlpha` save (`:1749-1752`); frame-null **does** restore both before returning (`:1754-1759`); clipping `:1761`; `TreeAlpha_ApplyAlphaRef` `:1767`; colorScale `:1769-1770`; WVP `:1772-1773`; res entry `:1775`; `GetDefinedShader` `:1778`; DN/envmat `:1780-1801`; forced sun upload `:1797-1798`; wind VS selected once per callback `:1803-1813`; cull re-assert of the captured Switch value `:1822`; decal tracker `:1828`.
- Per mesh: stage0 `:1844`, UV `:1847`, TAC decision table `:1856-1897`, L1+L2 blend pair (elided) `:1903-1912`, decal ZWRITE 3-layer on/off `:1921-1946`, no per-mesh alpha-test write `:1948-1951`, diagnostics `:1956-1963`, c4 `:1965-1970`, matCol/c20 `:1972-1983`, env or simple PS `:1986-2009`, draw `:2016`.
- Exit: restore VERTEXALPHA L1+L2+L3 `:2027-2030`, TreeAlpha restore `:2032`, `rs` restore `:2033`, cleanup `:2034`, probes `:2035-2037`.
- Asymmetry vs PS2: has L2 blend mirror, cull re-assert, decal policy, per-callback wind VS; has **no** reflective FX leg, so the PS2-only 3-layer FX block has no Xbox twin. Also has no per-mesh `D3D9Render` fallback with per-mesh ZWRITE reset except the decal tracker.
- Runtime: all 16 sampled Xbox meshes are `tac=1 OPAQUE`, `blendOn=0`, `matA=255`; the visible artifacts therefore live in unsampled meshes or in state (cull/alpha), not in the sampled decal path.

### 3.3 Sphere callback (`buildingPipe.cpp:2042-2180`)

- Saves `rs` + `savedVtxAlpha` **before** any work (`:2060-2066`); applies TreeAlpha `:2072`; no frame-null early return (uses a zero world matrix if `RpAtomicGetFrame` is NULL `:2103-2112`); sets sphere VS/simpleFogPS `:2113-2116`; per mesh stage0/UV `:2130-2133`, WIND-aware `hasAlpha` `:2135-2143`, L1+L2 blend `:2144-2148`, matCol/c4/c20 `:2150-2160`, plain `D3D9Render` `:2165`; exit restores VERTEXALPHA 3-layer `:2171-2174`, TreeAlpha `:2175`, `rs` `:2176`, cleanup `:2177`, probe `:2179`.
- Asymmetries: no TAC/decal policy, no cull re-assert, no ZWRITE policy, no `D3D9RenderDual` (so no per-texture `dualPass`/`zwriteThreshold` honor), and a zero matrix on a missing frame instead of a skip. It is used for the 512x512 sphere env capture while `gRenderingSpheremap` is true; the captured CULLNONE is intentional there, but the exit pins NONE on all three layers until the next caller repairs it.

### 3.4 PBR callback (`buildingPipe.cpp:2254-3010`)

- Pre-snapshot phase: `pipeEnsureIBLBuffer` `:2268`; manual alpha/blend re-force `:2280-2313`; hard-coded `pipeForceCullMode(rwCULLMODECULLBACK)` `:2318`; `pipeForceAlphaBlock()` (no preEnter) `:2325`; frame-null early return **after** those forces `:2335-2336` (the Switch exit still restores, so this is span-local); WVP/world matrix `:2341-2362`; res entry `:2366`; DN/eye/view rows/lights `:2368-2407`; envmat `:2410-2412`; VS/PS bind `:2414-2437`; PS c24/c20/IBL s3/normal s4/screen c29/Forward+ `:2439-2550`; **only now** `BuildingRenderState` save `:2552-2553`; TreeAlpha `:2560`; decal tracker `:2565`.
- Per mesh: stage0 `:2586`, TAC + c48 alpha override `:2589-2741`, UV `:2750`, L1+L2 blend `:2759-2769`, decal ZWRITE `:2681-2706` (note the decal block precedes the blend write in source order but uses the already-computed `blendOn`), matCol/c20 `:2792-2829`, env/sky `:2831-2909`, `pipeUploadPBR` `:2914`, draw `:2931`, classify second draw `:2938-2946`.
- Exit: TreeAlpha `:2950`, `rs` `:2951`, cleanup `:2959`, raw s3/s4/s5/s8 unbind + sampler resets + TSS disables `:2963-3002`, VS/PS NULL `:3007-3008`.
- Asymmetries: hard-coded BACK (E6), snapshot after IBL/force (section 1.2), second classify draw, c48 alpha override that PS2/Xbox do not have, and a raw-sampler block that no other callback owns. PBR is **dormant** in the current ini.

### 3.5 Switch wrapper / ZWRITE force / `zwriteWasDesync` latch (`buildingPipe.cpp:3013-3145`)

- Entry order: `pipeEnterCullMode()` `:3018` → stash `s_bldSwitchSavedCull` `:3022` → `pipeEnterAlphaMode()` `:3023` → capture ZWRITE L1/L2/L3 `:3041-3047` → compute `zwriteWasDesync = (!savedZWriteRw || !savedZWriteDrv || !savedZWriteDev)` `:3052` → force TRUE L1/L2/L3 `:3053-3056` → camRas probe `:3074-3082` → `pipeForceAlphaBlock(&savedAlpha)` `:3096` → dispatch (Sphere first, then PS2/GTAIV, Xbox, PBR) `:3098-3114`.
- Exit order: `pipeExitAlphaMode(savedAlpha)` `:3115` → `bpipeBlendLeakProbe("Switch")` `:3122` → ZWRITE restore `:3128-3142` → `pipeExitCullMode(savedCull)` `:3143` → `fixSAMP()` `:3144`.
- The latch is a **sticky repair**, not a faithful restore: if *any* layer captured 0, the exit writes canonical TRUE to all three layers; only a fully-clean entry is restored verbatim. Consequence: a legitimately ZWRITE-off span (mirror, transparent overlay, a special pass that contains building atomics) is converted to TRUE on exit. In the current log every entry is `rw=1 drv=1 dev=1`, so the latch is **inactive**; it is not the current artifact, but it is a risk if the game ever enters building geometry with ZWRITE off.
- The forced TRUE is applied **before** the per-callback decal policies, so PS2/Sphere (which have no decal policy) render every blended material with ZWRITE ON. This is the most likely way the wrapper itself changes draw order vs aap/junior, which merely inherit the game's per-material ZWRITE.
- `pipeForceAlphaBlock(&savedAlpha)` runs after `pipeEnterAlphaMode` has already canonicalised the rw cache, so it re-forces canonical values; `savedAlpha` is used for lag-aware logging, not for the values it writes. The raw L3 loop is still the only thing that repairs a device that drifted behind an unchanged cache.
- `pipeExitCullMode` restores the **captured** mode through all three layers. Because the capture can be `NONE` (env capture) or `FRONT` (shadow volume leak), the wrapper can leave the raw device on that mode after the building span; the game's own L1-only restore then cannot move the device back. The next `pipeEnterCullMode` repairs it, but only if the next pipe runs before the offending draws. This is the E1 mechanism.

## 4. Task 4 — WIND shader selection path vs junior

### 4.1 junior `extendedplg.cpp` + `buildingPipe.cpp` gating

Junior has **no model-name / vegetation check**:

- `GetDefinedShader` is a raw read of the atomic plugin byte: `return *RWPLUGINOFFSET(uint8, atomic, ededAtmOffset);` (`junior:extendedplg.cpp:10-13`).
- The only producer of `WIND` is `ededConstruct`, which sets the slot from the global `forceWindShader` at atomic construction: `if(forceWindShader) ... = DefinedVertexShader::WIND; else ... DEFAULT;` (`junior:extendedplg.cpp:20-25`). `forceWindShader` is a global read from `[SkyGfx] forceWindShader` (`junior:main.cpp:30`, `:1248`).
- Plugin attach happens in the plugin chain (`junior:main.cpp:851`), and `ededConstruct` is the atomic plugin constructor (`junior:extendedplg.cpp:44-51`).
- Building selection is purely `definedVertexShader == WIND`:
  - PS2: `hasAlpha` from material alpha only (`junior:buildingPipe.cpp:250-257`); wind VS only when `WIND && instancedData->vertexAlpha` (`:266-272`).
  - Xbox: `vertexAlphaIsAlpha = false` for `WIND`, wind VS bound once per callback for **any** WIND atomic (`:384-399`).
  - Sphere/PBR/PS2 non-wind paths ignore the flag.

So junior gates by **plugin value only**. `forceWindShader` is not a model-name filter; when enabled it makes *every atomic constructed after plugin attach* WIND. There is no tree-name/palm-name check anywhere in `buildingPipe.cpp` or `extendedplg.cpp`.

### 4.2 our post-fix behaviour

- `src/rw/extendedplg.cpp:10-20`: same plugin-byte read, plus a fail-safe `if(ededAtmOffset <= 0) return DEFAULT;` guard. This is the actual fix for the reported root cause (previously offset 0 read `RwObject.type == rpATOMIC == 1`, which equals `WIND`).
- `src/rw/extendedplg.cpp:28-33`: `ededConstruct` is byte-for-byte the junior logic (`forceWindShader` → WIND, else DEFAULT).
- `src/rw/extendedplg.cpp:57-69`: attach registers the 4-byte plugin and returns 1 on failure (degrade safe); called from `src/core/main.cpp:5474-5479` before atomics are created.
- `src/core/main.cpp:6580` reads the same ini key; the runtime ini has `forceWindShader = 0`.
- Selection matches junior exactly: PS2 `:1360-1367` + wind VS `:1443-1448` (`WIND && vertexAlpha`), Xbox `:1778` + `:1803-1813` (WIND → wind VS for the whole callback, `vertexAlphaIsAlpha=false`), Sphere `:2067` + `:2140-2143`, PBR `:2414-2435` (WIND keeps `buildingPBRVS` and gets a CPU tilt; only the NULL-PBR fallback binds `xboxBuildingWindVS`).
- No model-name gate was added, so our post-fix behaviour **matches junior**: plugin value only, with the offset guard preventing the never-attached false-WIND. `forceWindShader=1` would still flag every newly constructed atomic WIND in both trees.

### 4.3 verdict

**Matches junior, with a safety improvement.** There is no vegetation-name gate to port. The remaining wind-specific risk is not selection but the Xbox callback binding the wind VS for every WIND atomic regardless of `instancedData->vertexAlpha` (same as junior); with `forceWindShader=1` that would displace all Xbox building vertices. Current ini has `forceWindShader=0`, and the log confirms `wind(tilt=0 c21c22=0)`.

## 5. Task 5 — `buildingPipe_setUVTransform` and env/UV matrix uploads

### 5.1 aap NULL-matrix bug

aap's MatFX UV block itself **does** guard `m1`:

```cpp
RpMatFXMaterialGetUVTransformMatrices(material, &m1, &m2);
if(m1) RwD3D9SetVertexShaderConstant(REG_texmat, m1, 4);
else   RwD3D9SetVertexShaderConstant(REG_texmat, &ident, 4);
```

(aap `:203-211`, `:330-338`, `:445-453`; same in junior `:239-247`, `:406-414`, `:543-551`). The unguarded NULL-matrix bug in aap is in the **env matrix path**, `CustomBuildingEnvMapPipeline__SetupEnv` (aap `:62-92`): it does `envframe = RwCameraGetFrame(RWSRCGLOBAL(curCamera))` without checking `curCamera`, then `RwFrameGetLTM(envframe)`, `RwFrameGetLTM(frame)`, `RwMatrixInvert` — any NULL frame/camera faults. Junior is byte-identical here (`junior:70-100`) and did **not** fix it.

### 5.2 junior behaviour

- `SetupEnv` `junior:70-100`: same unguarded camera/frame dereferences, same static cache keyed by object/envframe/renderFrame.
- UV transform: same guarded `m1` upload, identity fallback; no scaling; raw `RwMatrix` uploaded as 4 constants.

### 5.3 our behaviour

- `CustomBuildingEnvMapPipeline__SetupEnv` `buildingPipe.cpp:196-226`: guards `curCamera` (`:203`), guards missing `frame`/`envframe` and writes **identity** to `*envmat` instead of dereferencing (`:212-215`), and otherwise performs the same invert/multiply/orthonormalize/cache. This is strictly safer than aap/junior.
- `buildingPipe_setUVTransform` `:355-368`: same `m1` non-NULL guard and identity fallback; the identity pointer passed by every caller (`:1357`, `:1847`, `:2133`, `:2750`) is a local `RwMatrix ident` initialized with `RwMatrixSetIdentity` at the top of each callback, so it is never NULL.
- Uploads are raw: `RwD3D9SetVertexShaderConstant(REG_texmat, m1, 4)` or `ident, 4`; env `RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3)` at `:1278`, `:1801`, `:2412` (and Sphere's world matrix is separate). There is no scale, tile or UV-offset multiplication added by our code. The only per-mesh “tile” value is the PS c1 detail tiling (`buildingPipe_selectPS:962-969`), which is the aap/junior detail-map path, not the UV transform.
- U/V address/filter is handled separately from the UV matrix (section 1); switching address modes does not scale UVs.

### 5.4 verdict

**At least as safe as junior, and safer than aap/junior on the env matrix.** The MatFX `m1` NULL guard was already present in both references; the real aap bug was the unguarded `SetupEnv` camera/frame path, which junior did not fix and ours does. No UV scaling was introduced by the wrapper or the env upload. The tree's UV matrix handling is not a suspect for the reported geometry symptoms.

## 6. Task 6 — Top 5 remaining suspects

Ranked for the **current runtime** (`buildingPipe=Xbox`, `dualPassBuilding=0`, `ivMode=0`, `forceWindShader=0`). Dormant PS2/PBR items are called out.

### 1. CULLMODE replay/pin (Switch + Xbox; PBR hard-coded BACK)

- Evidence: `buildingPipe.cpp:3018/3022/3143`, `:1822`, `:2318`; `pipelinecommon.cpp:1201-1300`. Runtime: `[Cull] enter saved=2` and `[Cull] enter saved=1` both occur, and `[Cull] exit restored=1` is emitted every ~2 s. The `saved=1` samples are logged between `XboxBuilding` calls, consistent with the interleaved sphere/env capture; this does not by itself prove a main-view atomic entered with NONE.
- Mechanism: the Switch captures whatever the rw cache says and `pipeForceCullMode` **pins it on the raw device**. A cache-only NONE (sphere/mirror) or FRONT (shadow-volume leak) that would have stayed device-BACK in aap/junior is now actually applied. NONE makes single-sided building walls draw their interior/back face; FRONT culls the visible face and shows the back. At low camera angles this reads as “the back of the face is drawn in front” and can expose see-through gaps. Crucially, `pipeExitCullMode` also raw-writes the captured NONE at the end of the sphere/env span; the game's own L1-only restore cannot move the device off NONE, so **any geometry drawn before the next `pipeForceCullMode`** (vehicles, peds, effects) is double-sided. That matches the reported building **and** vehicle symptoms.
- Test/fix: log `savedCull` + `gRenderingSpheremap` + the next atomic at the draw; force BACK for the normal building pass and honour NONE only while `gRenderingSpheremap` (or a verified mirror flag). On exit, do not pin NONE with L3 — restore the captured layers only, or let the game's own span restore win. Make PBR use the captured value like Xbox does, never a hard-coded BACK.

### 2. Alpha-test/blend hardening on the PS2/Xbox simple-PS paths (no `c48.w` override)

- Evidence: `pipelinecommon.cpp:1664-1670/1714-1716`; `buildingPipe.cpp:543-734`, `:3096`; runtime `[PipeAlpha] entry resync ... rw ref=2 fn=5` and ALPHATESTENABLE forced TRUE.
- Mechanism: PS2/Xbox simple PSes have no `c48.w` alpha override (unlike PBR). `texalpha.cpp:1-12` documents exactly this failure: a “solid” mesh whose texture has an unused alpha channel decodes `tex.a ~ 0`, and `GREATER@1/2` discards the whole surface — “solid faces not solid”. `buildingPipePS.hlsl:25-27/91-95` returns texture alpha unchanged; the Xbox callback only sets the CPU blend bit (`:1871-1873`). Therefore a `TAC_OPAQUE` classification disables blending but cannot force output alpha to 1 on Xbox, and PS2 has no TAC at all. The same entry force also pins ALPHAFUNC from the cache, so a raw-writer leftover (e.g. `LESS`) inverts the cutout.
- Test/fix: per-material alpha-test decision for PS2/Xbox (TAC_OPAQUE → force shader alpha via a variant, or set `ALPHAFUNC=ALWAYS` / write alpha 1), or gate the global test force on `tac != OPAQUE`. Add a one-shot `tex='...' tac=OPAQUE but alpha0` diagnostic.

### 3. ZWRITE policy asymmetry and the Switch force (Xbox/PS2/Sphere)

- Evidence: Switch `:3041-3056` / `:3132-3141`; Xbox decal `:1921-1946`; PBR decal `:2681-2706`; PS2 `:1506-1507/1532-1533`; Sphere has no decal path at all.
- Mechanism: the wrapper forces ZWRITE TRUE for the whole callback. Only Xbox/PBR have a per-mesh “material-translucent decal” policy that turns it off. In PS2/Sphere (and in Xbox if classification says non-decal), translucent material meshes write depth and reject later translucent layers, producing stacked/out-of-order translucency and solid-looking overlaps. The `zwriteWasDesync` latch additionally restores TRUE over a legitimate off entry. In this run the latch is idle (`[BldZW] rw=1 drv=1 dev=1`), so weight this below the alpha-test and cull items.
- Test/fix: add the decal policy to PS2/Sphere; skip the global TRUE for meshes that will blend; or make the wrapper restore the captured layers always (the latch is a separate risk).

### 4. Xbox/PBR material-alpha “decal” classification (`matTranslucent` includes alpha 0)

- Evidence: `:1856-1863`, `:2604-2605`, `:2646-2649`, `:2681`; `decalBlend = blendOn && matTranslucent && tac != FADED`.
- Mechanism: only alpha `16` is excluded as a paint marker; any other non-255 material alpha (including 0/garbage) is treated as a real translucent overlay and gets blend ON + ZWRITE OFF. A solid mesh with an uninitialized material alpha then draws see-through and without depth, so later faces overwrite it. The runtime sample is too small (16 meshes, all `matA=255`) to rule this out for the other hundreds of meshes.
- Test/fix: extend the diagnostic to print every non-255 `matA` and the decal decision; treat `0` as opaque/full-alpha unless the texture class says FADED, and keep ZWRITE on for anything that is not explicitly an overlay.

### 5. ZENABLE/ZFUNC are never guarded in the building path (shared gap)

- Evidence: no reference or ours saves/sets `ZENABLE`/`ZFUNC` in any building callback; only the config-gated frame baseline (`pipelinecommon.cpp:2017-2020/2043-2044`, `main.cpp:3759/4267/4296`) pins them, and mid-frame raw writers (DepthHook raw `ZENABLE`, postfx Im2D passes) can move the device afterwards.
- Mechanism: if the device reaches a building atomic with ZTEST off (or ZFUNC ALWAYS), depth no longer rejects the farther/back face; the later draw wins. That is the exact “back of the face drawn on front” signature at low angles, while top-down views (fewer overlapping wall faces) look mostly correct.
- Test/fix: capture/repair ZENABLE/ZFUNC in the Switch alongside ZWRITE (L1/L2/L3), and log when the entry device value is not `D3DZB_TRUE`/`LESSEQUAL`. This is a shared gap rather than a divergence from aap/junior, so it ranks below the ours-only hardening.

**Honorable mention (dormant unless the pipe changes):** PS2's missing L2 `ALPHABLENDENABLE` mirror (E8) is the largest callback asymmetry and should be fixed before ever shipping `buildingPipe=PS2/GTAIV`; PBR's hard-coded BACK is folded into suspect 1. The `TagRenderCB` `buildingPipe` vs junior `tagsBuildingPipe` gate is an aap-parity regression that only affects tag/prop materials.

## Appendix A — File:line index

### aap original (`/e/SDKs/skygfx_original/src/buildingPipe.cpp`; MTA identical)

- PS2 state save: `186-191`; UV `203-211`; VERTEXALPHA `214`; env shader `221`; env state burst `256-267`; exit `270-276`; Xbox `340-388`; Sphere `391-467`; Switch `469-487`.

### junior (`/e/SDKs/skygfx_junior/src/buildingPipe.cpp`)

- PS2 state save `222-227`; VERTEXALPHA `252-259`; wind VS `266-272`; env burst `319-330`; exit `333-339`; Xbox `384-422`; env `437-454`; Sphere `543-561`; Switch `567-585`. Wind plugin: `extendedplg.cpp:10-25`, `44-51`; attach call `main.cpp:851`; ini read `main.cpp:1248`.

### ours (`src/render/buildingPipe.cpp`)

- Save/restore helpers `421-458`; alpha block `543-734`; PS selection `953-1046`; elision helpers `1061-1174`; PS2 cb `1198-1573`; Xbox cb `1674-2038`; Sphere cb `2042-2180`; PBR cb `2254-3010`; Switch `3013-3145`; `DNInstance_PS2` `3203-3387`; pipeline creation/hook `3388-3483`.

### ours (`src/render/pipelinecommon.cpp`)

- `pipeEnsureIBLBuffer` `1042-1095`; cull `1201-1300`; blend/alpha conversion `1434-1520`; `pipeEnterAlphaMode` `1522-1744`; `pipeExitAlphaMode` `1746-1896`; frame baseline `1974-2049`.

### ours (`src/core/main.cpp`)

- `fixSAMP` `180-205`; `D3D9Render` `207-215`; `D3D9RenderDual` `251-301`; `EDEDPluginAttach` call `5474-5479`; `forceWindShader` ini read `6580`; state baseline call sites `3759`, `4267`, `4296`, `7738`.

### ours (`src/rw/extendedplg.cpp`)

- `GetDefinedShader` guard `10-20`; construct/copy `28-39`; attach `57-69`.

