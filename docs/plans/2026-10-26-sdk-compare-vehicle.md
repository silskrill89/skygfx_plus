# SDK Compare — Vehicle Pipe (aap / junior vs expIV)

Symptom: vehicles render "3/4 x-ray" — silhouette/edges correct, interior partially see-through with a sphere-like cast; panels appear replaced by reflections. Context: paint-marker alpha (`material->color.alpha == 16`) has already been clamped to 1.0 in `SetupMaterial_Xbox` and excluded from the Xbox `hasAlpha`; the additive clearcoat has been restored in `VehiclePBR_Modern`.

Method: read-only diff of the two reference source trees against expIV, plus the live `skygfx_dbg.log` (session 04:22–04:34, `[VPIPE] cb=Env pipe=MODERN(10)`, body `[VehOpacity] tac=1(OPAQUE) blendOn=0 c48w=1.00`, `dualPass=0`). No source was modified; this document is the only write. Prior art: `docs/plans/2026-10-26-vehicle-xray-audit.md` (Xbox path, pre-fix) and `docs/plans/2026-10-25-texalpha-unknown.md:166`.

## 1. Per-mesh / per-callback state contract

Sources: aap = `/e/SDKs/skygfx_original/src/vehiclePipe.cpp` (`skygfx_original`), junior = `/e/SDKs/skygfx_junior/src/vehiclePipe.cpp`, neo = `neoCarpipe.cpp` in both. `junior` differs from aap only in the `hasAlpha` casts (`441/833/1333`), a 3-line commented sunPos block after `1282`, and line-number drift; **every state write below is byte-identical between aap and junior**. expIV = `src/render/vehiclePipe.cpp` (line numbers as of this audit).

### 1.1 Reference contract (aap; junior identical)

| Callback | CULL | ZWRITE | ZENABLE/ZFUNC | Blend | Alpha-test | VERTEXALPHA | FOG | s0 | s1 | TFACTOR | TCI / TEXGEN |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **PS2** `aap:369-543` | not set | FX leg FALSE→TRUE `521-529` | never | FX ONE/ONE `524-525`, restore `530-531` | save `422`, FX ALWAYS `521`, restore `532` | per-mesh `441-442`; FX TRUE `523` (leak) | save FOGCOLOR `425`, FX 0 `526`, restore `528` | `pipeSetTexture` `439` | raw `RwD3D9SetTexture(envData->texture,1)` `485/500` | — | exit TCI=1 `541`, TFF=DISABLE `542` |
| **Xbox** `aap:766-936` | not set | via dual only | never | no explicit (VERTEXALPHA `833-834`) | not set (inherits) | `833-834` | not touched | raw `914/922` | raw `865/895` | XRGB `869` / ARGB `898` | TCI=1 env2 only `905`; exit TCI `934`, TFF `935` |
| **Env** `aap:1238-1386` | not set | not set | never | not set | not set | per-mesh `1330-1331` | saves FOGCOLOR `1297` (never restored) | `pipeSetTexture` `1328` | `pipeSetTexture(reflectionTex,1)` `1306` (+ mask s2 `1307`) | — | exit TCI=1 `1384`, TFF `1385` |
| **Leeds** `aap:941-1078` | not set | FX FALSE `1054` → TRUE `1067` | never | FX SRCALPHA + ONE (VCS INVSRCALPHA) `1056-1060`; restore DEST only `1064` | save `989`, FX ALWAYS `1052`, restore `1068` | per-mesh `1012-1013`; FX TRUE `1055` (leak) | save FOGENABLE `992`, FX off `1053`, restore `1066` | `pipeSetTexture` `1010`; FX rebinds s0=reflectionTex `1051` | — | — | exit TCI=1 `1076` |
| **Mobile** `aap:1081-1235` | not set | not set | never | not set | not set | per-mesh `1153-1154` | not touched | `pipeSetTexture` `1151` | reflectionTex `1217` (no sphere guard) | — | exit TCI=1 `1233` |
| **exe/PC** `CAR_PC` | — | — | — | — | — | — | — | — | — | — | game callback `0x5D9900` (WRAPPER `gta.cpp:137`); no source in any tree |
| **neo** `aap neo:296-368` + `371-424` | not set | spec FALSE `387`, restore `419` | never | diffuse SRCALPHA/INVSRCALPHA `317-318`; spec ONE/ONE `389-390` | spec ALWAYS `391`, restore `423` | diffuse forces TRUE `316`; spec TRUE `397` | spec off `388`, restore `420` | raw material `326`; spec NULL s0-s3 `392-395` | reflectionTex s1 `302` | — | no TCI; spec `D3D9Render` (no dual) `415` |

### 1.2 expIV contract

| Callback | CULL | ZWRITE | ZENABLE/ZFUNC | Blend | Alpha-test | VERTEXALPHA | FOG | s0 | s1 | TFACTOR | TCI / TEXGEN |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **PS2** `917-1194` | only Switch `3608/3644` (saved) | FX FALSE `705-706` → TRUE `729-730` | never | FX ONE/ONE `708-722`, 3-layer restore `731-734` | FX ALWAYS `702-704`, restore `735-736` | per-mesh `777/786`; FX TRUE `707-708`, restore `739-740` | FX FOGCOLOR 0 `713-714`, restore `727-728` | `pipeSetTexture` `775` | **raw** `1136/1151` (same as aap) | restored `755` | cleanup TCI=1 `643`, TFF `644` |
| **Xbox** `1450-1737` | saved `1540` | saved/restored `1480-1481/1713` | never | L2 ALPHABLENDENABLE `1570` (no factors) | not set | `1561-1564` (alpha 16 excluded) | not touched | `pipeSetTexture` `1681/1693` + TSS `1682-1697` | `pipeSetTexture` `1613/1648` | XRGB `1617` / ARGB `1651`, restored `1720-1722` | TCI=1 env2 `1658`; exit `1706-1707` |
| **Env/PBR** `2423-3599` | **hardcoded BACK** `2503` | entry via Switch `3644-3646`; glass L1-only `2983/3032` | never | per-mesh L1+L2 `2854`→`2310-2325`; glass `2994-3007`, restore `3037-3048` | alpha block re-assert `2514`; glass ALWAYS `2980`, restore `3033` | via `vehEnvSetAlphaBlend` `2854` | not touched (Switch does not force) | `vehEnvBindTexture` `2683` | `vehEnvBindTexture(reflectionTex)` + sphere guard `2976/3348`; mask s2 `3353` | — | no TCI; cleanup `643-644` |
| **Leeds** `1740-1917` | only Switch | FX FALSE `1869` → TRUE `1895` | never | FX SRCALPHA + ONE/INVSRCALPHA `1871-1883`, 3-layer restore `1889-1911` | FX ALWAYS `1865`, restore `1897/1910` | per-mesh; FX TRUE `1870`, restore `1904/1911` | save FOGENABLE `1774`, FX off `1867`, restore `1893/1908` | `pipeSetTexture` `1842`; FX s0=reflectionTex `1848` | — | — | envmat c32 `1801`, envtexmat **c40** `1802` (aap c36 `1002`) |
| **Mobile** `1920-2180` | saved `1991` | not set | never | per-mesh L1+L2 `777/786` | alpha block `1992` | per-mesh | not touched | `pipeSetTexture` `775` | sphere-guarded `2106-2109` | — | exit `643-644` |
| **exe/PC** | — | — | — | — | — | — | — | — | — | — | same game callback `src/rw/gta.cpp:163` |
| **neo** `src/render/neoCarpipe.cpp` | not set | spec FALSE `703`, restore `745` | never | diffuse/spec as aap; 3-layer restore `242-260` | spec ALWAYS `707`, restore `749` | diffuse TRUE/restore `613/681`; spec TRUE `717`, restore `754` | 3-layer fog guard `137-179` | `pipeSetTexture` `630` | reflectionTex `599` | — | no TCI |

### 1.3 Divergence summary

1. **CULL** — aap/junior/neo never touch cull in any vehicle callback. expIV adds a Switch-level saved-cull guard (`3608/3612/3768`), a saved-cull re-assert in Xbox (`1540`) and Mobile (`1991`), but a **hardcoded BACK** in Env (`2503`).
2. **ZWRITE** — aap relies entirely on inherited/dual-leg state; expIV forces TRUE on all three layers for the whole callback (`3644-3646`) and restores each layer (`3765-3767`). The glass path's `vehEnvSetZWrite` is L1-only (`2334-2341`), so its FALSE never reaches L2/device.
3. **ZENABLE/ZFUNC** — **no tree touches them in any vehicle callback**. expIV inserts Im2D/IBL/Forward+ passes inside the callback; those helpers save/restore ZENABLE, but the vehicle Switch itself only guards ZWRITE (`3623-3646`).
4. **Blend** — aap's FX passes use rw-cache sets only; expIV mirrors ALPHABLENDENABLE to L2 (`786/1570/2324`) and forces the FX pair through all three layers (`708-722`). This is what makes `D3D9RenderDual`'s L2 gate deterministic (it is otherwise at the Switch's FALSE baseline).
5. **Alpha-test** — expIV canonicalises on every Switch entry (`3622/3657`, Env re-assert `2514`); aap only saves/restores around FX passes.
6. **VERTEXALPHA** — expIV restores per mesh (`739-740`, `1904`, Env tracker); aap leaves FX legs at TRUE.
7. **s0** — expIV resolves NULL→`gpWhiteTexture` everywhere (`pipeSetTexture`/`vehEnvBindTexture`); aap Xbox uses raw `RwD3D9SetTexture` (`914/922`).
8. **s1** — expIV adds `pipeSetTexture` in Xbox (`1613/1648`) and `vehEnvBindTexture` + `gRenderingSpheremap` guards in Env/Mobile; aap binds raw env textures and the global sphere map unconditionally.
9. **TFACTOR** — expIV snapshots/restores (`755`, `1720-1722`); aap leaks the env TFACTOR past the callback.
10. **TCI/TEXGEN** — same shape in all trees (exit TCI=1, TFF=DISABLE; no TEXGEN). No tree uses `D3DTSS_TEXGEN`.

## 2. Env-map handling (env1/env2 selection, gRenderingSpheremap, NULL resolution, matrices, matCol alpha)

### 2.1 env1 / env2 selection

All trees read the MatFX flags out of the **bitfield stored in `material->surfaceProps.specular`** and require `RpMatFXMaterialGetEffects(material) == rpMATFXEFFECTENVMAP`:

- aap/junior PS2: `vehiclePipe.cpp:462-472`, `hasEnv1 = bit1`, `hasEnv2 = bit2`, `hasSpec = bit4 && !renderingWheel`; `if(hasEnv1) … else if(hasEnv2)` (`479/494`) — **env1 wins over env2**.
- aap/junior Xbox: `829-831`; `hasEnv2` additionally requires `flags & rpGEOMETRYTEXTURED2` (`830`), `hasSpec` excludes wheels (`831`). Same `else if` (`849/878`), with `blownUp` overriding to envSwitch 3/4 (`846-848`).
- aap/junior Mobile: `1163-1177`; `vehiclelights` textures explicitly disable env1/env2 (`1150`, `1173-1176`).
- aap/junior Env (`CAR_ENV`) / ours Env (`CAR_MODERN`, `CAR_NEO`): **env1/env2 are never selected for binding at all.** aap binds the dynamic `reflectionTex` on s1 (`1306`) and only reads the bits to gate `fxParams.shininess` (`1339-1355`). Ours does the same (`2691-2699`, `3180-3193`, `3348`). So `envData->texture` (the static MatFX sphere map) is irrelevant on the Modern/PBR path; only `reflectionTex` + `CarReflectionMask` are sampled.
- expIV PS2 (`1130-1161`), Xbox (`1594-1660`) and Mobile (`2040-2121`) mirror the aap selection order exactly (env1 else-if env2, env2 Xbox requires TEXTURED2, lights excluded on mobile), with added NULL guards on the `atmEnvData` pool (`1153-1156`, `1635-1638`, `1309-1312`).

### 2.2 `gRenderingSpheremap` guard

- **aap/junior vehiclePipe.cpp: no guard anywhere.** The only guard is in `buildingPipe.cpp:475` (sphere callback dispatch) / `573` in junior. In the original sphere pass, `envmap.cpp:437-442` sets the flag around `RenderReflectionScene()` (`envmap.cpp:241-253`), which calls only `CRenderer::RenderRoadsAndBuildings()` — **vehicles are not rendered into the sphere map at all**, so the missing guard is harmless upstream.
- **expIV:** guards at `2106-2109` (mobile s1), `2492-2493` (IBL capture), `2975-2978` (glass/light s1), `3347-3350` (opaque s1). `envmap.cpp:1085-1114` sets the flag, and the sphere scene is now `CarPipe::RenderReflectionScene()` (`envmap.cpp:1093`, `neoCarpipe.cpp:265-402`), which renders **roads + everything-bar-roads + fading entities, i.e. vehicles too** (`neoCarpipe.cpp:372-375` explicitly calls out the change from the global roads/buildings-only function). The guard is therefore required in expIV, but it also means **the reflection texture now contains the car itself/other traffic** — a new, plausible source of the "sphere-like cast" content on panels (self-reflection), though not of depth removal.

### 2.3 `pipeSetTexture`-style NULL resolution

- Helper exists in **both** trees: aap `skygfx.h:328`, expIV `skygfx.h:1272` — `RwD3D9SetTexture(t ? t : gpWhiteTexture, n)`.
- aap uses it for the material/diffuse only (`vehiclePipe.cpp:439`, `614`, `1010`, `1151`, `1328`), but **raw `RwD3D9SetTexture` for every env bind** (`485`, `500`, `658`, `669`, `865`, `895`) and raw material binds in the Xbox path (`914`, `922`).
- expIV uses it for Xbox env1/env2 (`1613`, `1648`) and resolves NULL for s0 in every callback (`775`, `1681/1693`); the Env/PBR path uses `vehEnvBindTexture` (`2278-2292`) which has the same NULL→white contract and forces a cache transition. **PS2 env1/env2 are still raw** (`1136`, `1151`) — the same residual risk aap has.

### 2.4 Env matrices and transform values (identical in all trees)

- env1 frame: `RwMatrixRotate(X, 33°) → Y(−33°) → Z(50°)` (aap `87-98`, expIV `241-268`; `betaEnvmaptest` uses X 60° in both). env2 frame: identity (`99-103` / `269-274`).
- `SetupEnv`: `envmat = atomic/clump LTM * inverse(envFrameLTM)` (aap `306-327`, expIV `489-507`); results cached per object/frame.
- `Env1Xform`: fractional `(pos − trunc(pos/scl)*scl)/scl`, `scl = GetTransScaleX/Y()*50` (aap `143-156`, expIV `322-335`). `Env1Xform_PC`: **negated** fractions (aap `229-240`, expIV `404-421`).
- `Env2Xform`: `scaledfract` sum, `trans = oldTrans ∓ |val2−val1|` wrapped to [0,1), `transY = at.x+at.y` clamped 0…0.1 and flipped by `at.z < 0` (aap `168-226`, expIV `347-401`). `Env2Xform_PC` returns **−trans** for x (aap `301`, expIV `484`).
- PS2 uploads `envXform.z/w = GetScaleX/Y` (aap `487-488`, `503-504`; expIV `1138-1139`, `1158-1159`). Xbox builds `texMat._11=scaleX, _22=scaleY, _31/_32=trans` for env1 (aap `857-864`, expIV `1602-1609`) and identity scale + trans for env2 (aap `888-894`, expIV `1640-1646`). Leeds `envtexmat` is the 0.5 / −0.5 / 0.5 bias matrix (aap `105-123`, expIV `240-283`). **No numeric divergence found.**

### 2.5 matCol alpha

- aap `pipeUploadMatCol` uploads the raw `RwRGBAReal` including alpha (aap `pipelinecommon.cpp:183-192`), so a paint-marker material (`alpha == 16`) reaches the VS as `0.0627` and `hasAlpha` treats it as translucent (`vehiclePipe.cpp:441/616/833/1012/1153/1330`). On the PS2 path aap's `vehicleVS.hlsl:32/38` starts alpha at 1.0 but then multiplies by `matCol` (marker → 0.0627); the Xbox `xboxCarVS.hlsl:57` writes `matCol.a` directly. The marker clamp is therefore an **expIV behavioural change from both forks**, not a restore of earlier upstream behaviour.
- expIV clamps the marker in the shared helper (`pipelinecommon.cpp:464-471`) and in `SetupMaterial_Xbox` (`1397-1398`); `hasAlpha` excludes 16 in the Xbox cb (`1561-1563`) and `matTranslucent` excludes it in the Env cb (`2708-2709`). **Leftover:** the Env cb's `hasAlphaLegacy` (`2726-2727`) still contains the raw `alpha != 255` test and feeds the `TAC_UNKNOWN` blend decision (`2781`); and the Env cb's explicit **PS** c19 upload (`3212-3219`) does not clamp alpha — inert today because `main()` reads only `matCol.rgb` (`shaders/ps/VehiclePBR_Modern.hlsl:545`), but a latent trap if the PS ever reads `.a`.
- `SetupMaterial_Xbox` builds the c26 surfProps **by name** in expIV (`1411-1416`) where aap uploads the raw `RwSurfaceProperties` (`762`). The Xbox VS reads `.x/.y/.z` and both writers land ambient/spec/diffuse in the same slots, so there is no behavioural divergence; the expIV form additionally zero-fills `.w` instead of over-reading the 3-float struct.

### 2.6 Verdict

The env **math** is bit-identical to aap/junior. The only real env-path divergences are (a) the guards made necessary by rendering vehicles into the sphere map, and (b) `pipeSetTexture` NULL resolution on the Xbox env stage. Neither can remove body depth or make an opaque body transparent; the new **content** difference (cars inside `reflectionTex`) is the best explanation for a sphere-like reflection cast, not for the x-ray.

## 3. Dual-pass path

### 3.1 Enablers

| Tree | Switch / default | Profile overrides | Store |
|---|---|---|---|
| aap | `dualPassVehicle` inherits `dualPassGlobal` (`main.cpp:924`, `:961`) | none | INI default `dualPassGlobal = 0` → vehicles off |
| junior | same (`main.cpp:1080`, `:1118`) | none | same default 0 |
| expIV | `config->dualPassVehicle` read from INI (`src/core/main.cpp:6226`; game INI = `0`) | **Xbox `1` (`modeManager.cpp:218`)**, Legacy PC `1` (`324`), **Modern `1` (`533`)**, GTA V `1` (`657`); PS2 `0` (`113`), GTA IV `0` (`415`); ULTRA sets `1` (`main.cpp:6122`) | profile applied after INI read → the INI `0` is not what runs in Xbox/Modern |

The expIV per-mesh L2 mirrors (`vehiclePipe.cpp:786`, `1570`, `2324`) are load-bearing for the gate: the Switch forces `D3DRS_ALPHABLENDENABLE = FALSE` at entry (`pipelinecommon.cpp:1628`, plus `pipeForceAlphaBlock` from `buildingPipe.cpp:596`), so without the mirror the gate would read FALSE for every mesh. aap/junior have neither the entry force nor the mirror (they only set `rwRENDERSTATEVERTEXALPHAENABLE`, which RW applies lazily at draw time).

### 3.2 Leg semantics (identical in all three trees)

| | Leg 1 | Leg 2 |
|---|---|---|
| Condition | `dual && D3DRS_ALPHABLENDENABLE && rwZWrite` (aap `main.cpp:131`; junior `:149`; expIV `src/core/main.cpp:277`) | same |
| ALPHAFUNC/REF | `GREATEREQUAL @ min(config->zwriteThreshold, 254)` (aap `:133-136`; junior `:155-158`; expIV `:287-288`) | `LESS @ threshold` (aap `:137`; junior `:159`; expIV `:290-291`) |
| ZWRITE | TRUE (aap `:135`; junior `:157`; expIV `:288`) | **FALSE** (aap `:138`; junior `:160`; expIV `:291`) |
| Blend factors | unchanged — whatever ALPHABLENDENABLE/`SRCBLEND`/`DESTBLEND` currently hold (canonical `SRCALPHA/INVSRCALPHA` from the pipe entry) | same |
| Draw | `D3D9Render` (aap `:136`; junior `:158`; expIV `:289`) | `D3D9Render` (aap `:139`; junior `:161`; expIV `:292`) |
| Restore | `ZWRITE=zwrite`, `ALPHAFUNC/REF` saved (aap `:140-142`; junior `:162-164`; expIV `:293-294`) | — |
| `!zwrite` fallback | `ALPHAFUNC=ALWAYS`, single draw (aap `:143-146`; junior `:165-168`; expIV `:295-298`) | — |

Junior's only change is the optional `TexInfo *` (building-pipe per-texture `dualPass`/`zwriteThreshold`, `main.cpp:146-153`); all vehicle callers pass three arguments, so **vehicle semantics are identical to aap**. expIV matches the same two legs but pushes every leg state through three layers (`setAlphaTestTwoLayer`/`setZWriteTwoLayer`, `src/core/main.cpp:230-249`) and clamps the ref to ≤254 (`:282-286`); aap/junior use `RwRenderStateSet` only.

### 3.3 Consequence for the x-ray

The dual pass changes **only** alpha test + ZWRITE; it never changes blend. For a blended mesh (`ALPHABLENDENABLE=TRUE`) with texels below the threshold, **leg 1 rejects those texels entirely — no colour and no depth — and leg 2 draws them with ZWRITE off**. Opaque trim/glass pass leg 1 and keep depth. That is exactly "edges correct, interior see-through". It is an **amplifier**: it requires the body to be classified blended (`ALPHABLENDENABLE=TRUE`) in the first place, or to have output alpha < threshold.

### 3.4 Verdict

aap/junior have identical `D3D9RenderDual` semantics. expIV differs in (a) **profiles forcing the flag on** where upstream defaults to 0, and (b) the **L2 mirrors that make the gate deterministic** instead of relying on the game's leftover L2 value. If the body reaches the dual pass with alpha < 128, it loses depth and ghosts. The dual pass alone cannot make an alpha-255/opaque mesh transparent (leg 1 draws every texel, leg 2 rejects every texel).

## 4. Glass

### 4.1 Presence in each tree

- **aap/junior: none.** `VehShaders_IsGlassTexture` does not exist in either tree and is not called from `vehiclePipe.cpp` or `neoCarpipe.cpp` (only unrelated `prt_glass` in `neoWaterdrops.cpp:249`). There is no `Glass_Vehicle` bind, no glass branch, and no per-texture glass promotion. Glass on aap/junior is simply alpha-test/blend from the material.
- **expIV: addition.** Classifier `src/extras/veh_shaders.cpp:239-248`; call at `src/render/vehiclePipe.cpp:2741`; demotion guard `2748-2750`; glass/light branch `2907-3050`; head/tail-light tests `2888-2890`; tire branch `2891`, `3055-3147`. `Glass_Vehicle` is loaded in `pipelinecommon.cpp:791` and bound at `3028`.

### 4.2 Classifier logic and guard

```
IsGlassTexture(name, hasAlpha, alpha):
  if GetSurfaceType(name) == SURFACE_CAR_GLASS           -> true      // positive name match
  if !hasAlpha || alpha >= 200                          -> false
  if IsHeadlightTexture || IsTaillightTexture           -> false
  return true                                                        // alpha<200 fallback
```

expIV calls it with `hasAlpha = (tac == TAC_FADED) || matTranslucent` (`2740`), then:
`if(isGlassMesh && GetSurfaceType(texName) != SURFACE_CAR_GLASS) isGlassMesh = false;` (`2748-2750`).
Because the fallback only fires when the surface type is **not** glass, **the guard makes the alpha<200 fallback dead code** — only the positive name match survives. Paint-marker alpha 16 is also excluded from `matTranslucent` (`2708-2709`).

### 4.3 Can it misclassify body / grime / lights?

- **Grime/dirt:** `GetSurfaceType` classifies `vehiclegrunge*`, `grunge`, `dirt` as `SURFACE_CAR_DIRT` (`veh_shaders.cpp:279-280`), so it is not glass. OK.
- **Body:** unknown names → `SURFACE_CAR_BODY` → not glass. Only names containing `windscreen`/`window`/`glass` (and not `chrome`/`tire`/`wheel`/`headlight`/`taillight`/`carbon`/`leather`/`fabric`/`trim`) are glass. **The one real hazard is priority order:** `window`/`glass` is tested before `trim`/`plastic_interior` (`272` vs `274-275`), so a name like `window_trim`, `glass_frame` or `windowrubber` is promoted to glass and drawn with `Glass_Vehicle` (blend forced on at `2805-2806`, c48.w overridden to 1.0 at `3013-3015`).
- **Lights:** `vehiclelights128` does **not** contain `headlight`/`light_front`/`taillight`/`light_rear`, so it is neither glass nor light — it falls through to the opaque PBR body path. The light branch only catches explicitly named modded light textures. That is a coverage gap, not a misclassification.
- The glass branch itself: forces `blendOn=1` (`2805-2806`), `ZWRITE=FALSE` (L1 only, `2983`), `ALPHAFUNC=ALWAYS` (`2980`), and `Glass_Vehicle` outputs its own Fresnel alpha `lerp(0.3, 0.8)` (`shaders/ps/Glass_Vehicle.hlsl:157`); the material-alpha `opacity` only tints (`2907-2931`). A misclassified body panel would therefore read as a 30–80 % transparent, reflective panel — a “panels replaced by reflections” look.

### 4.4 Verdict

Ours is an **expIV-only addition**; aap/junior have no equivalent. After the `SURFACE_CAR_GLASS` guard the alpha fallback is inert, so body/grime/vanilla-light atlases are **not broadly misclassified**. The residual risk is narrow and name-driven (`window*`/`glass*` trim/body textures), and it can produce a reflective semi-transparent panel. It is not a plausible cause of a whole-car 3/4 x-ray on stock vehicles.

## 5. Cull / winding special-casing

### 5.1 Reference trees

- **No determinant, winding or mirrored-atomic code anywhere** in aap/junior `vehiclePipe.cpp`, `neoCarpipe.cpp`, or the vehicle path of `main.cpp` (`grep -i determinant|winding|mirror` finds only unrelated mirror-fix comments in `main.cpp`).
- **No cull writes in any vehicle callback** in any tree. The callbacks inherit whatever `rwRENDERSTATECULLMODE` the game/RW pipeline set. The only cull writes are the env-map pass (`aap envmap.cpp:209/232` → `rwCULLMODECULLBACK`) and postfx (`postfx.cpp:310` → `D3DCULL_NONE`).

### 5.2 expIV additions

- Switch entry captures the game's pre-entry cull and forces it through all three layers (`vehiclePipe.cpp:3608`, `3612`, `3768`; `pipelinecommon.cpp:1246-1300`). `pipeForceCullMode` uses a park-then-target trick because `RwRenderStateSet`/`RwD3D9SetRenderState` are cache-gated; its comment block (`pipelinecommon.cpp:1164-1191`) explicitly names **“stuck at the opposite winding -> front faces culled … the 'cull reversed on PBR vehicles/buildings' report”**, and preserves `NONE`/`FRONT` because the game really uses them (mirror, stencil shadow volumes).
- Xbox (`1540`) and Mobile (`1991`) re-assert the **saved** value — correct.
- **Env/PBR hardcodes `rwCULLMODECULLBACK` (`2503`)** instead of the saved value. Its rationale is real (the in-callback `pipeEnsureIBLBuffer()` Im2D pass parks cull at NONE and raw-writes `D3DCULL_NONE`), but the correct repair is the saved cull, not BACK: the log shows `[Cull] enter saved=0x1` (NONE) on legitimate mirror/sphere spans that reach the vehicle Switch.
- No determinant check exists in expIV either; `pipeCullScopeBegin/End` (`pipelinecommon.cpp:1327-1400`) exists but **has no call site**. There is no per-atomic “negative determinant → flip cull” logic in any of the four trees.

### 5.3 Consequence

For the ordinary main view the game enters the Switch at cull BACK (`[Pipeline] pipeEnterCullMode: saved=0x2`), so the Env hardcode is a no-op. It only bites when the incoming mode is NONE or FRONT (mirror pass, shadow-volume span, or a genuinely mirrored/negative-determinant atomic): there the Env cb flips to BACK and culls the opposite face, exposing the interior/backfaces. **Verdict: expIV-only divergence; low probability for the main-view x-ray, but a real correctness bug and the cheapest one-line parities fix.**

## 6. Ranked suspects

| # | Suspect | Exact file:line | Kind | Mechanism / why it still fits | Confidence |
|---|---|---|---|---|---|
| 1 | **Env cb `hasAlphaLegacy` still includes the paint marker (alpha 16)** | `src/render/vehiclePipe.cpp:2726-2727` (definition), consumed at `:2781` | **fix-forward** | `hasAlpha` (`:2720`) and `SetupMaterial_Xbox` (`:1397-1398`) now exclude 16, but the TAC_UNKNOWN fallback still uses the raw `alpha != 255`. When `TexAlpha_Classify` returns UNKNOWN (per-frame budget skip — live log `unknown_budget=55206`; unsupported/locked format; or `texAlphaClassify=0`) a paint mesh gets `blendOn=1` with `c48.w=0`, so the PS emits **texture alpha** (`VehiclePBR_Modern.hlsl:818`) and the body blends/dual-legs. Change `hasAlphaLegacy` to `matTranslucent` (or `alpha != 255 && alpha != 16`). | 65 % (Modern/Env, conditional on UNKNOWN; 0 % once the mesh is OPAQUE) |
| 2 | **Xbox FFP stage-0 alpha has no TAC_OPAQUE override** | `src/render/vehiclePipe.cpp:1685-1687` (ALPHAOP MODULATE; the dual call is `:1700`, the alpha force is the Switch `:3622/:3657`) | **fix-forward** | The PBR path forces output alpha 1 for OPAQUE textures via c48.w (`:2838-2842`); the Xbox path still computes FFP alpha = `tex.a * matCol.a`. For a DXT3/DXT5 body texture whose alpha channel is unused/all-zero (the exact class `TacClassFromHist` calls OPAQUE, `texalpha.cpp:327-330`), FFP alpha is 0 → the canonical `GREATEREQUAL@1` alpha test (`pipeEnterAlphaMode`) discards the body, or the dual pass rejects it in leg 1 and draws it ghosted in leg 2. The Xbox cb never calls the classifier. Fix: per-mesh, for `TexAlpha_Classify(material->texture) == TAC_OPAQUE`, force `D3DRS_ALPHATESTENABLE=FALSE` (restore after the draw) or `ALPHAFUNC=ALWAYS`, and/or bind a white-alpha diffuse. | 55 % for CAR_XBOX (depends on body texture alpha; upstream has the same MODULATE but expIV adds the always-on alpha block and the classifier knows the answer) |
| 3 | **Env cb hardcodes CULLBACK** | `src/render/vehiclePipe.cpp:2503` | **revert-to-upstream** (inherit/saved) | aap/junior never touch cull in any vehicle callback; expIV's own `pipeForceCullMode` doc (`pipelinecommon.cpp:1174-1177`) names “stuck at the opposite winding → front faces culled” as the cull-reversed x-ray. The Switch already captured the correct pre-entry mode (`s_vehSwitchSavedCull`, `:3612`); use it instead of `BACK`. The live log shows `[Cull] enter saved=0x1` (NONE) on legitimate mirror/sphere spans, which this line overrides. | 25 % main view; 85 % mirror/sphere passes |
| 4 | **Dual-pass forced on by the Xbox/Modern profiles** | `src/core/modeManager.cpp:218` (Xbox), `:533` (Modern) | **revert-to-upstream** (or gate on class) | aap/junior default `dualPassVehicle` to 0 (`aap main.cpp:924/961`); the game INI says 0 (`skygfx.ini:53`), but both profiles force 1. `D3D9RenderDual` leg 1 (`GREATEREQUAL@128`, ZWRITE on) drops depth for every texel below 128 and leg 2 (`LESS@128`, ZWRITE off) draws it at partial alpha — the amplifier that turns “6 % ghost” into “no-depth x-ray” (`docs/plans/2026-10-26-vehicle-xray-audit.md:16,79-82`). Set 0 in the profiles, or gate `dual` on `tac != TAC_OPAQUE` in the Env cb (`:3541`) and on the new TAC check in the Xbox cb. | 40 % (only in combination with a blended/alpha<128 body) |
| 5 | **In-callback IBL pass + entry guard missing ZENABLE/ZFUNC** | `src/render/vehiclePipe.cpp:2492` (IBL), `:2514` (alpha re-assert), Switch `:3623-3646` (only ZWRITE guarded) | **fix-forward** | aap never runs a fullscreen Im2D pass inside the vehicle callback; expIV calls `pipeEnsureIBLBuffer()` between the Switch force and the mesh loop, then re-asserts cull/alpha but **not** `D3DRS_ZENABLE`/`ZFUNC`. The helper normally saves/restores them (`postfx.cpp:2860-2865`, `postfx_utils.cpp:180`), but the vehicle entry guard does not verify them. A device left with ZENABLE off (raw postfx/Im2D write, cache no-op) draws the whole body without depth test → interior/far side visible. Fix: force ZENABLE=TRUE/ZFUNC=LESSEQUAL through L1/L2/L3 at Switch entry and restore on exit, or move the IBL capture out of the vehicle cb. | 15 % |

**Not suspects (checked and cleared):** env1/env2 selection and all env matrices/xforms (numerically identical, §2.4); `Env1Xform_PC`/`Env2Xform_PC` sign/scale; `matCol` VS alpha (clamped in `pipeUploadMatCol`, and `main_xboxCar` now gets 1.0); PS c19 alpha (inert — `main()` reads `.rgb` only); glass alpha fallback (dead after the `SURFACE_CAR_GLASS` guard); s1 NULL resolution (fixed in expIV); FOG/TFACTOR/TEXCOORDINDEX (state-hygiene only, cannot remove depth).

**Apply order:** #1 (one-line, Modern) and #2 (Xbox per-mesh alpha override) are independent and can ship together; #3 is a one-line revert of the hardcode. Re-test #4 only after #1/#2, using the debug-menu `Dual-pass Vehicles` toggle (the profile will re-apply it on mode change). #5 last; it is the highest-risk change (touches the shared Switch contract).
