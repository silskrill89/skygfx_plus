# 2026-10-26 — Visual regression attribution: c270294 (2026-09-03 target) → today

**Scope:** read-only. Only this file was written. Target source state = `c270294`
(2026-08-30, the last commit before the user's 2026-09-03 / ~17:00–18:00 build).
Current = working tree at 2026-10-26 (build 2,399,744 B, post-`b7396c4` layers included).

**Evidence caveat.** The two pre-extracted diffs are treated as **current tree vs
c270294**. They include uncommitted post-Oct-2 layers (`effectsForceAlphaBlock`,
`coronaDrawRawShield`, `pipeForceCullMode`, shadow/ped contracts) even though the
folder is labelled `c270294-to-head`; this is exactly the delta we need. Diff line
numbers below are `L####` in those files; current file line numbers are from the
00:xx tree and will drift. The Sept-3 **INI** is not preserved; config drift is
called out separately.

**One-paragraph verdict.** The regression is not one commit. It is a **layered
accumulation**: (a) a post-Oct-2 **state-machine layer** that rewrites the
producer's additive `ONE/ONE` sprite pair into `SRCALPHA/INVSRCALPHA` (night
lights), (b) a **new shader input** that feeds the PBR shaders a **view-space**
normal as if it were world-space (face ordering / x-ray shading), (c) a **PBR look
rewrite** that replaces the paint with an env term and adds live building lights
on top of baked prelight (vehicles / buildings / washout), and (d) a **ped ambient floor**
(0.20) added on top of the game's direct light (white peds). The fastest wins are
one-line state fixes and one INI key each; the shader look restores must be
surgical, not full-file reverts.

---

## 1. Method and the three facts that frame everything

1. **`c270294` predates the PBR restore.** The first commit in the window is
   `18f5303` (2026-09-07, “PBR restore + …”). The Sept-3 target therefore has:
   no post-Oct-2 corona shield, no shadow contract, no ped ambient floor, no
   normal-buffer blend in `main_building`, no `SkyHemisphereTint`, no
   `vehPrelightFallback`, no `pipeForceCullMode`. All of those are candidates by
   construction.
2. **The current build is a different renderer in several paths.** The pre-extracted
   diff shows the PBR vehicle/building shaders and the ambient/tonemap C++ being
   rewritten in the window (`f36296a`, `b5d36a9`, `e39a6f8`, `9e93dc2`,
   `191dbe7`, `01204d7`, `ca922ef`, `a6d56f8`, `671bcd7`). The target look was the
   old additive/clearcoat/ambient behavior, not the current energy-conserving one.
3. **The live log names the night-light mechanism.** 230/232 `[CoronaDraw]`
   flushes see pre-force device `src=5 dst=6` (normal alpha, not additive);
   `[CoronaShield]` catches 314 cases of `blend=0`. The current repair path
   deliberately converts `ONE/ONE` to `5/6` (§2.1). That is the opposite of the
   vanilla/Sept-3 additive contract.

---

## 2. Ranked attribution per symptom class

### 2.1 Symptom 1 — night lamps / traffic lights / fog cones are opaque black rectangles with a coloured dot

| # | Candidate (commit / file / hunk) | Mechanism | Confidence | Class |
|---:|---|---|---:|---|
| 1 | **`effectsForceAlphaBlock` ONE/ONE → 5/6 substitution + non-ONE/ONE shield pair** — uncommitted post-`b7396c4`; current `src/core/main.cpp:4060-4083`; checkpoint fix-26 “effectsForceAlphaBlock additive→5/6 substitution” | The block reads both cache layers; if either holds `ONE/ONE` it seeds `SRCALPHA/INVSRCALPHA` before `pipeForceAlphaBlock()`. Corona/point-light/fog-cone textures are authored for **additive** blending: black RGB surround with alpha≈1. Normal alpha writes the black surround (`dst*(1-a)`) and only the coloured core survives → “black rectangle + dot”. The per-draw shield then forces `SRCALPHA/ONE` (`:2952-2954`), which is still not the vanilla `ONE/ONE`; and the **fog-cone volume** is a separate `CPointLights::Render` draw (0x700817) that the sprite shield does not cover (`:3410-3449` is only the body re-arm). The target (c270294) has **no substitution/no shield**; vanilla `CCoronas::Render`/`CPointLights` set `ONE/ONE` (`main.cpp:3931-3938`). | **80%** | **State-machine layer** |
| 2 | **`coronaDrawRawShield` default pair is `SRCALPHA/ONE`, not `ONE/ONE`** — uncommitted; `src/core/main.cpp:2952-2954`, `:3336-3342` | The per-draw shield is the last writer before the sprite raster. It forces `SRCALPHA/ONE` for every non-cloud producer; the log’s 230 `src=5 dst=6` pre-force cases are the producer’s state just before this write. For an opaque-alpha black surround `SRC.rgb*SRC.a + dst` adds nothing, but the pair is still not the target and any path that bypasses the shield (fog cone, second sprite flush, raw writer inside the producer) stays on `5/6` and writes black. | **70%** | State-machine layer |
| 3 | `effectsForceAlphaBlock` forces `ALPHAFUNC=ALWAYS` / `ALPHAREF=0` and colour-write all — `main.cpp:4039-4084` | A stuck `ALWAYS@0` makes the sprite’s soft alpha edge irrelevant; the black RGB still writes under normal alpha. It also explains the “jagged where a high inherited ALPHAREF cuts the falloff” class in the code comment (`main.cpp:3947-3950`). | **45%** | State-machine layer |
| 4 | `CClouds__Render_hygiene` / cloud guard leaves the effects phase in a normal-alpha block — `main.cpp:4604-4840` | The cloud guard’s restore is canonical `SRCALPHA/INVSRCALPHA`; the next `CCoronas`/`CPointLights` producer only sets L1. If the guard’s restore wins after the producer, the sprite pair is normal alpha. The cloud path itself is `ONE/ONE` and is correctly excluded from the substitution, which is why the bug is lamp-specific, not cloud-specific. | **30%** | State-machine layer |

**Target contract (c270294):** producer `ONE/ONE`, no shield, no substitution.
**Minimal exposure:** preserve the producer pair for additive sprites; never
rewrite `ONE/ONE` to `5/6` on the effects path.

### 2.2 Symptom 2 — low-angle “back face in front” on buildings/vehicles, top-down nearly correct, peds unaffected; vehicles “3/4 x-ray with a see-through sphere”

| # | Candidate (commit / file / hunk) | Mechanism | Confidence | Class |
|---:|---|---|---:|---|
| 1 | **NormalBuffer is view-space but the PBR shaders blend it into world-space N** — `shaders/ps/NormalBuffer.hlsl:82-88` (“Normal is in view space … TODO multiply by inverse view matrix”); `shaders/ps/VehiclePBR_Modern.hlsl:440-462` (main) and `:1011-1028` (main_building). The building block is **added in the window** (`shader diff L1998-2018`); the target `main_building` has **no** normal-buffer block. Live config `normalBufferEnable=1`, `vehPBRLayers=255` (bit 7 on). | The 5-tap depth normal is reconstructed in **view space**. The shaders decode it and `lerp` it into the **world-space** geometric normal. The result is a normal rotated by the camera basis: at top-down the rotation is least visible; at low angles it tilts the front face toward the camera/back, so the front face is lit like the back, and GGX/env terms blow out. Peds do not sample the normal buffer → unaffected. This is a lighting/face-shading inversion, not a geometry reorder (matches “silhouette correct”). | **65%** | **Look-constant layer** (new shader input) |
| 2 | **PBR clearcoat composite replaces the body with the env term** — `shaders/ps/VehiclePBR_Modern.hlsl:690-701` (`kr`, `layer2 = lerp(layer1, envTerm, kr)`), `:779` (`color = layer2`); commit `b5d36a9` “energy-conserving clearcoat”, plus `f36296a` perspective env sampling. Target (`shader diff L1805-1810, L1935`) used `layer2 = iblBlend*envMask*reflTint*envTexMod; color = layer1 + layer2;` (additive body + env). | At grazing `clearcoatFresnel→1`, `carcolsShine→1`, so `kr→1` and the paint is **fully replaced** by `envTerm`. If the env source is a camera/sphere capture with holes, the body reads as a see-through sphere with only some elements left. The target never removed the body. | **55%** | **Look-constant** |
| 3 | **Cull policy: `pipeEnterCullMode` preserves the last pass’s mode; `MirrorPass_ResyncCull` re-asserts the post-mirror NONE** — uncommitted wave 8; `src/render/pipelinecommon.cpp:1202-1243`, `:1246-1300`; `src/render/mirrorPass.cpp:364-383`. Live log: `[CullResync] mirror exit rw=1` ×212; `[Cull] enter saved=1` ×19. | If a pass leaves `CULLNONE`/`CULLFRONT`, the PBR building/vehicle span inherits it. `CULLFRONT` would cull front faces (back faces in front); `CULLNONE` draws both, and with a ZWRITE-off decal/dual pass the back face can land over the front at shallow depth. Peds use the skin path and are less affected. The mapping `rwCULLBACK→D3DCULL_CW` (`pipelinecommon.cpp:1223-1231`) is also unverified against RW’s actual driver mapping. | **35%** | **State-machine layer** |
| 4 | `vehLitFallback` floor — `shaders/vehiclePipeVS.hlsl:107-109` (`if(surfPrelight<=0.5) OUT.Color.rgb=max(...,vehLitFallback.rgb)`); live `vehPrelightFallback=0.15`; commit window (PBR vehicle look) | Lifts the shade side of every non-prelit car mesh to 0.15 before `matCol`. It does not reorder faces, but it removes the dark side that gives the 3/4 view its depth, contributing to the “flat/x-ray” read. Target c270294 has no fallback (`shader diff L3349-3366`). | **30%** | Look-constant |

**Note:** the confirmed `EDEDPluginAttach` bug (every atomic read as WIND) is fixed
and is not the remaining cause. It explains earlier WIND/flip artifacts, not the
camera-angle-correlated normal inversion above.

### 2.3 Symptom 3 — peds blow out pure white beyond a ragged blob ring; inside the ring correct

| # | Candidate (commit / file / hunk) | Mechanism | Confidence | Class |
|---:|---|---|---:|---|
| 1 | **`pedAmbientFloor=0.20` added on top of `pDirect≈0.996`** — uncommitted wave 5 (checkpoint 2026-10-24 “PED AMBIENT”); current `src/core/main.cpp:2069-2138`, floor at `:2110-2123`; live `pedAmbientFloor=0.200000` | Peds are FFP: final ≈ `pAmbient + direct`. The new floor raises `pAmbient` to 0.20 (or scales the peak channel to it). At high sun (`pDirect=0.996`) the sum saturates to white. Inside CJ’s shadow the direct term is occluded → the ped is correctly lit; outside it is white. The ring is the shadow boundary, not the bug. The target c270294 has no floor and uses the game’s `pAmbient`; the Sept-3 peds are not white. | **65%** | **Look-constant** (C++ ambient) |
| 2 | **`pAmbient` override replaces the game ambient with `max(game, timecycle)` + hue floor** — `main.cpp:2069-2138`; the override existed before, the floor is new | Even with `pedAmbientFloor=0`, the `max(game, tcAmbient)` can be brighter than the game’s own value at noon; combined with `pDirect≈1.0` it clips. The target’s ped ambient is the game’s own. | **55%** | Look-constant |
| 3 | **Shadow state contract hard-edges the ring** — uncommitted wave 4/5; `main.cpp:1379-1410` (`shadowContractForce`: blend ON `SRCALPHA/INVSRCALPHA`, `ALPHAREF=0`, `ZWRITE FALSE`), wrappers `:1519-1533` | The ring is CJ’s shadow edge. The contract forces normal-alpha and `ALPHAREF=0` for all shadow passes. A soft additive/multiplicative shadow texture drawn under that block can gain a hard edge; the ring becomes visible as the boundary between shadowed-correct and sunlit-white peds. It is a contributor to the ring, not the white. | **30%** | State-machine layer |
| 4 | Tonemap black lift / auto exposure — `shaders/ps/TonemapPass.hlsl:238-247`; C++ `tonemapBlackLift=0`, `tonemapAutoExposure=0` (live) | With both keys at 0, the tonemap is a fixed curve; it cannot create a local white ring. It can only clip an already-white input. **Not a primary cause** at this INI. | **10%** | Look-constant (inactive) |

**Fastest confirmation:** `pedAmbientFloor = 0.0` (one INI key). If the white
outside the ring drops to the target’s lit-ped level, the floor is confirmed.

### 2.4 Symptom 4 — washed-out/pastel/greenish daylight, black ground quads and dark patches

| # | Candidate (commit / file / hunk) | Mechanism | Confidence | Class |
|---:|---|---|---:|---|
| 1 | **Building PBR VS adds live ambient + 7 directional lights on top of baked prelight** — `shaders/vs/buildingPBRVS.hlsl:108-118`; `src/render/buildingPipe.cpp:2820-2823` (`surfUpload = {amb, diffuse, diffuse}`; `surfUpload[2]=1` only when diffuse≈0); `buildingAmbient` now `GetTimecycleAmbientPBR()` at `buildingPipe.cpp:183`. Target c270294: `OUT.Color = (prelight*surfDiff + ambient*surfAmb)*matCol`, and the old `GetTimecycleAmbient` was a dead-static **0.0** (the diff comment says it printed 0), so the target was **prelight-only**. The diff hunk is `shader diff L3872-3887`; commit candidate `01204d7` “unify building ambient with vehicle/ped” (75% attribution). | For materials with real `diffuse`, the new VS adds `ambient*surfAmb` **and** `7 × NdotL × directCol × diffuse` to prelight. The sun is already baked into `DayColor/NightColor`; this is double energy → pastel/washout on roads/terrain/facades. For diffuse≈0 assets the loop is zero, which is why only some surfaces blow out. | **65%** | **Look-constant** |
| 2 | **`SkyHemisphereTint` (multiplicative sky/ambient hue ratio) + `pbrIblAmbientWeight=0.8`** — `VehiclePBR_Modern.hlsl:92-105`, `:1112-1122` (building), `:642-649` (vehicle); commits `ca922ef` / `a6d56f8` / `671bcd7`; live `pbrIblAmbientWeight=0.800000`. Target c270294 used a flat **additive** IBL (`iblSample*0.15` or `baseColor*surfProps.x*0.5`) and no sky hue ratio (`shader diff L2077-2096`). | The ratio `(sky/skyL)/(ambient/ambL)` is a **chromatic** multiplier clamped `0.6..1.5` and blended at `weight*0.15` (≈0.12 at 0.8). It preserves luminance but shifts hue; a blue sky vs a red-brown timecycle ambient yields a green/cyan skew on every sky-facing surface. The old additive blue IBL did not tint the base this way. | **45%** | **Look-constant** |
| 3 | **`ambientMult=1.0` (was 0.85) + `pbrAmbientFloor=0.05`** — `pipelinecommon.cpp:183-191`; target `shader/cpp diff L1375-1380` had `*0.85`; floor added at `cpp diff L1388-1414`; live INI `ambientMult=1.0`, `pbrAmbientFloor=0.05`. | +17.6% ambient on every ambient-only pipe; the 0.05 PBR floor is an **additive grey lift** (the live log showed `final=(0.050,0.050,0.050)` from `raw=(0,0,0)`), which lifts blacks and desaturates shadows → grey/pastel. Target had 0.85 and no PBR floor. | **50%** | **Look-constant** (config/constant) |
| 4 | **`main_building` IBL changed from additive to multiplicative** — current `VehiclePBR_Modern.hlsl:1112-1122` (`color = baseColor * ibl`); target `color = baseColor; color += ibl` with `ibl = iblSample*0.15` (or `baseColor*surfProps.x*0.5` fallback) — `shader diff L2077-2096`. | The old additive IBL supplied a small sky-colour fill; the new multiplicative tint supplies no fill and can darken (ibl<1). The target’s “clean bright daylight” relied on prelight + additive IBL. Restoring the additive term is a look-only change; it does not touch the NM/Forward+ paths. | **50%** | Look-constant |
| 5 | **Tonemap toe inversion + auto-exposure/black-lift** — C++ `cpp diff L3116-3133` (toe formula changed from `0.25 - sceneLuma*0.8` to `0.22 + sceneLuma*1.0`), TonemapPass `shader diff L1431-1477`. Live `tonemapAutoExposure=0`, `tonemapBlackLift=0`, curves 0. | The new toe is **higher** in daylight, which tightens the toe and darkens blacks (toward “solid blacks”, not washout). Auto-exposure/black-lift are off in this INI. The toe is therefore a **low-confidence** washout contributor; it is more likely to crush, not pastel. | **20%** | Look-constant (feature mostly inactive) |
| 6 | **Mode profile / colour-filter path switch** — uncommitted modeManager/presets; live `[Modes] active` / `colorFilter` may not match the Sept-3 preset | The target mode is unknown. If Sept 3 ran Xbox/PS2 and today runs PBR/Modern (or a different `colorFilter`), the whole colour pipeline changed before any shader edit. This is a **confound** for every look claim above; the one-key mode A/B must be run before attributing the washout to PBR constants. | **25%** | **State-machine layer** (mode profile) |
| 7 | Black ground quads / dark patches are the **same state-machine layer as §2.1**, not a separate exposure bug — `effectsForceAlphaBlock` / shadow contract / corona-cone flush | The black quads have hard geometric edges; a tonemap/ambient change produces gradients, not hard rectangles. Treat them as the sprite/shadow blend class until §2.1 is fixed. | **55%** | State-machine layer |

---

## 3. Top 5 candidates — exact minimal revert / exposure

> “c270294” values are from the pre-extracted diff. “Current” values are from the
> current file. Safety is stated against s8/c48 (normal maps), s9/c49–c52 (Noise),
> s5–s7 (Forward+).

### 3.1 Corona/point-light additive pair — the night-light fix

- **Current:** `src/core/main.cpp:4060-4083` rewrites `ONE/ONE` to
  `SRCALPHA/INVSRCALPHA`; `:2952-2954` default shield pair is `SRCALPHA/ONE`;
  `:3340-3342` non-cloud flush uses the default.
- **Target c270294:** no shield, no substitution; vanilla `CCoronas::Render` /
  `CPointLights` / clouds use `ONE/ONE` (`main.cpp:3931-3938` documents this).
- **Minimal change (C++ only):**
  1. In `effectsForceAlphaBlock`, **do not rewrite** a `ONE/ONE` pair; leave the
     producer pair untouched (or gate the substitution to `!g_inCoronasRender &&
     !g_inCloudsRender`).
  2. Change the `coronaDrawRawShield` defaults to `D3DBLEND_ONE/D3DBLEND_ONE`
     (`:2952-2954`) and the non-cloud flush call (`:3341`).
  3. Cover the **fog-cone volume** as well: the `CPointLights::Render` body
     (0x7002D0, hook at `main.cpp:3410-3449`) draws the cone mesh at 0x700817;
     re-assert the same `ONE/ONE` pair for that span (or move the pair push to
     the body entry and stop the body from reverting it). The sprite shield alone
     does not cover this draw.
- **Safety:** independent of s8/c48, s9/c49–c52, s5–s7. The shield still repairs
  blend-enable/alpha-test/colour-write; only the additive factors change.
- **Confirmation:** night lamp becomes a soft additive glow; `[FxAlpha]
  blend_subst` stops; `[CoronaDraw] pre-force dev src=2 dst=2` (ONE/ONE) instead
  of `5/6`.

### 3.2 NormalBuffer space mismatch — the face-ordering / x-ray fix

- **Current:** `NormalBuffer.hlsl:82-88` outputs **view-space** normals (explicit
  TODO). `VehiclePBR_Modern.hlsl:440-462` (vehicle) and `:1011-1028` (building)
  decode and `lerp` them into world-space `N`.
- **Target c270294:** `main_building` has **no** normal-buffer block
  (`shader diff L1998-2018` is added); the old vehicle block sampled
  `IN.texcoord0` (diffuse UV), not screen UV.
- **Minimal change (INI or shader-only):**
  - Fastest: `normalBufferEnable = 0` (one key; disables the normal-buffer pass
    and therefore the PBR `N` blend). SSAO is off in the live INI
    (`ssaoEnable=0`), so this costs nothing today; if SSAO is re-enabled later,
    keep the buffer and remove only the two N-blend blocks instead.
  - Surgical shader: delete/gate the two `if(ambientColor.w > 0.5)` blocks; keep
    the NM lane (`s8/c48`) and all Forward+ code untouched.
- **Safety:** normal maps use s8/c48; Noise uses s9/c49–c52; Forward+ uses
  s5–s7. None of them read the normal buffer. The GBuffer/SSAO path still runs
  when `normalBufferEnable=1` but the PBR `N` stays geometric.
- **If face ordering persists after this:** test the cull mapping (§2.2 #3):
  temporarily write `D3DCULL_CCW` for `rwCULLBACK` in `pipeForceCullMode`
  (`pipelinecommon.cpp:1223-1231`) or bypass the helper for PBR building/vehicle
  spans. This is the only other angle-correlated geometry candidate.

### 3.3 Vehicle PBR clearcoat composite — the x-ray/sphere fix

- **Current:** `VehiclePBR_Modern.hlsl:690-701` computes `kr` up to 1 and
  `layer2 = lerp(layer1, envTerm, kr)`; `:779` `color = layer2;`; `:768`
  `specTotal *= 1.5`; `:766` rim `*0.18`.
- **Target c270294:** `layer2 = iblBlend*envMask*reflTint*envTexMod;`
  `color = layer1 + layer2;` (additive body + env; spec not ×1.5; rim ×0.10) —
  `shader diff L1805-1810, L1919-1925, L1935, L1858-1864`.
- **Minimal change (shader-only):**
  - `float3 layer2 = layer1 + envTerm * saturate(kr) * 0.5f;` (or restore the old
    `envMask` formula) and keep `color = layer2`.
  - Restore `specTotal *= 1.0` (delete the 1.5) and rim `0.10`.
  - Set `vehPrelightFallback = 0.0` in the INI (or remove the `max()` floor).
- **Safety:** keeps the view-vector fix (`7b8cf1f`), the NM lane (`s8/c48`), the
  Forward+ atlas (`s5–s7`), the env validity/mask chain, and the `LF` layer
  mask. A **full file revert would lose all of those** and reintroduce the
  inverted-V/kr=1 vanish that `7b8cf1f` fixed.
- **Optional A/B:** if the sphere persists, restore the c270294
  `SphereEnvMapUV(R_view,V_view)` in `main()` and `Glass_Vehicle` for one build.
  Only do this if the env texture is still a sphere map; the current code assumes
  a perspective render (`f36296a`).

### 3.4 Ped ambient floor — the white-ped fix

- **Current:** `main.cpp:2110-2123` `floorV = config->pedAmbientFloor` (0.20),
  hue-preserving scale/floor; `pAmbient->color = base` at `:2138`.
- **Target c270294:** no `pedAmbientFloor`; the ped `pAmbient` is the game’s /
  old timecycle value (the floor is a wave-5 addition; checkpoint 2026-10-24).
- **Minimal change (INI):** `pedAmbientFloor = 0.0` (or 0.05 if peds go too dark
  at night). If that is not enough, skip the `max(game, tcAmbient)` override for
  peds and use the game’s `pAmbient`.
- **Safety:** only the ped/default-callback path; no shader registers, no
  s8/s9/s5–s7 interaction.

### 3.5 Building PBR energy / sky tint — the washout fix

- **Current:** `buildingPBRVS.hlsl:108-118`
  `OUT.Color = prelight*surfDiff; += ambient*surfAmb; += 7×NdotL*directCol*surfLightScale; saturate*matCol`;
  `buildingPipe.cpp:2820-2823` uploads `surfUpload = {amb, diffuse, diffuse}`
  (y = live-light scale); `buildingAmbient = GetTimecycleAmbientPBR()`
  (`buildingPipe.cpp:183`). `main_building` uses multiplicative
  `SkyHemisphereTint` (`VehiclePBR_Modern.hlsl:1112-1122`).
- **Target c270294:** `OUT.Color = (prelight*surfDiff + ambient*surfAmb)*matCol`
  with ambient **0.0** (dead static) and **no** 7-light loop; `main_building`
  `color = baseColor; color += ibl;` with additive `iblSample*0.15` —
  `shader diff L3872-3887`, `L2077-2096`.
- **Minimal change (shader + INI, optional one C++ line):**
  - Delete `buildingPBRVS.hlsl:111-117` (the 7-light loop); keep the `CamNormal`
    output at `:94-102`. If matching the target's prelight-only look exactly,
    also set `surfUpload[0] = 0.0f` (ambient scale) at `buildingPipe.cpp:2820-2823`;
    keep `surfUpload[2]` prelight.
  - In `buildingPipe.cpp:2820-2823` set `surfUpload[1] = 0.0f` (remove the
    dynamic-light scale); keep `surfUpload[2]` prelight.
  - `main_building`: remove the multiplicative `SkyHemisphereTint` from the body
    colour and restore the old **additive** `iblSample*0.15` term (validity-guarded),
    or set `pbrIblAmbientWeight = 0.0` to neutralise the tint while adding the
    additive term back. The two are separate: the weight only removes the hue,
    it does not restore the fill.
  - Look constants (INI): `ambientMult = 0.85`, `pbrAmbientFloor = 0.0`,
    `bldSkyReflect = 0.0`, `vehPrelightFallback = 0.0`.
- **Safety:** the building PS still uses the NM lane (`s8/c48`), Noise
  (`s9/c49–c52`) and Forward+ (`s5–s7`) for its own cluster lights. Removing the
  **VS** 7-light sum only restores prelight-only building lighting (the target);
  it does not touch the PS cluster path. A full revert of `buildingPBRVS` would
  lose `CamNormal`, which `main_building`’s env UV reads — keep that output.

---

## 4. Shader-level “restore the Sept-3 look” patch set

| Shader | Delta class | Look-critical deltas | Surgical look-only restore | What a full revert breaks |
|---|---|---|---|---|
| `shaders/ps/VehiclePBR_Modern.hlsl` — `main()` | **Look-critical** | Normal-buffer blend `:440-462`; env UV perspective `:599-601`; `layer2=lerp` `:701`; `color=layer2` `:779`; spec ×1.5 `:768`; rim ×0.18 `:766`; `vehPrelightFallback` via VS | Remove/gate normal block; `layer2 = layer1 + envTerm*saturate(kr)*0.5` or old `envMask`; spec ×1.0; rim 0.10; INI `vehPrelightFallback=0` | Loses `7b8cf1f` view-vector fix (kr=1 vanish), NM lane (`s8/c48`), Forward+ atlas (`s5–s7`), `LF` layer mask, env validity/mask |
| `shaders/ps/VehiclePBR_Modern.hlsl` — `main_building()` | **Look-critical** | Normal block `:1011-1028`; multiplicative `SkyHemisphereTint` `:1112-1122`; missing additive IBL; `bldEnv` sky term `:1166-1204` | Remove normal block; restore `color = baseColor + iblSample*0.15` (validity-guarded) or set `pbrIblAmbientWeight=0`; keep `bldEnv` at 0.15 or 0 for target | Loses NM lane, `bldEnv` env reflection, cluster lights, `VPOS`/screenSize path |
| `shaders/vs/buildingPBRVS.hlsl` | **Look-critical** | Live ambient + 7-light loop `:108-118`; `surfLightScale` `:35-43` | Delete `:111-117`; restore `(prelight*surfDiff + ambient*surfAmb)*matCol`; keep `CamNormal` `:94-102` | Loses `CamNormal` (required by `main_building` env UV) and the guarded normal/light upload |
| `shaders/vehiclePipeVS.hlsl` (`shaders/` root) | **Look-critical** | `vehLitFallback` floor `:107-109`; view vector `:114-120` (this is the **fix**, keep) | Set `vehPrelightFallback=0` / remove the `max()`; keep `eyePos-WorldPos` | Reintroduces inverted V (`7b8cf1f`), which made `kr=1` and replaced the base paint |
| `shaders/ps/TonemapPass.hlsl` | **Feature-critical; look-neutral at current INI** | `lumTex`/auto-exposure `:201-247`; black lift `:238-247`; pivot curve `:160-198`; the **toe formula is C++** (`cpp diff L3116-3133`) | Keep shader; set `tonemapAutoExposure=0`, `tonemapBlackLift=0`, curves 0 (already live). If restoring the old toe, change the C++ formula, not the shader | Loses `c7/c8`, `lumTex`, identity-grade bypass, negative-exposure guard; no visual gain at current INI |
| `shaders/ps/Water_Parallax.hlsl` | **Feature-critical** | Full water rewrite (SSR/refraction/underwater); colour constants changed | **Do not touch in this pass.** No current symptom depends on it; restore only colour constants later if the target water look is still wrong | Breaks `reflValid`, depth copy, SSR, underwater contracts added after c270294 |
| `shaders/ps/Glass_Vehicle.hlsl` | **Look-critical** | Perspective env `:58-72`; `envIntensity` up to 1.5 `:75`; glass env `lerp(0.15,0.45)` `:139`; lens tint `:111-117` | Restore `envStrength = lerp(0.05,0.20,fresnel)` and `envIntensity = max(envColor.a,0.1)*0.18`; keep the lens tint and c25–c27 view constants | Loses `e39a6f8` lens tint and the perspective/c1 contract; can white-wash lights/glass |
| `shaders/ps/CarPaint_Reflections.hlsl` | **Cosmetic / feature** (verify it is bound) | View-space env basis `shader diff L164-177`; SSR UV `R_world` | If used, restore world-space `SphereEnvMapUV(R,V)`; otherwise leave | Loses view-basis/SSR; safer to leave until binding is confirmed |

**Rule:** the shaders that carry newer contracts (`s8/c48`, `s9/c49–c52`,
`s5–s7`, `c7/c8`, `bldEnv`) must be edited **around** those contracts. Only
`buildingPBRVS` and the `main()`/`main_building()` look blocks have a
self-contained look-only restore.

---

## 5. Order of operations (payoff / risk) + the single fastest experiment

**Ranked next-build order:**

1. **Corona/point-light pair** — one C++ policy change + DLL rebuild. Highest
   payoff, low risk, no shader/feature interaction. Confirm `[FxAlpha]
   blend_subst` stops and the lamp sprite becomes additive.
2. **NormalBuffer N blend off** — one INI key (`normalBufferEnable=0`) for the
   first test, then a surgical shader block removal if confirmed. Fixes the
   low-angle lighting inversion on buildings/vehicles; peds unaffected either way.
3. **`pedAmbientFloor=0`** — one INI key. Confirms/removes the white-ped ring.
4. **Vehicle PBR composite + `vehPrelightFallback=0`** — shader-only additive
   restore; keeps the view-vector/NM/Forward+ contracts. Confirm taxi paint/chrome.
5. **Building PBR VS light sum + look constants** — shader-only + INI
   (`ambientMult=0.85`, `pbrAmbientFloor=0`, `pbrIblAmbientWeight=0`,
   `bldSkyReflect=0`). Restores prelight-only brightness and removes the green
   hue; keeps the PS NM/Forward+ paths.
6. **Cull mapping / mirror resync** — only if face ordering survives #2. This is
   the highest-risk remaining state-machine change; do it last and with a
   one-line A/B.

**Single fastest experiment (biggest candidate):** the **night-light pair**.
Delete or gate the `ONE/ONE → 5/6` rewrite in `effectsForceAlphaBlock`
(`main.cpp:4060-4083`), set the shield default to `ONE/ONE` (`:2952-2954`), and
force the same pair across the `CPointLights::Render` body (`:3410-3449`,
fog-cone draw 0x700817); rebuild the DLL only (no feature-set rebuild). Look at a
street lamp/traffic light at night.

- **Read:** `[FxAlpha] blend_subst` (should stop), `[CoronaDraw] pre-force dev
  src=… dst=…` (should be `2/2` = ONE/ONE, not `5/6`), `[CoronaShield]` (should
  stop firing for lamps).
- **If the black rectangle becomes a soft additive glow → root confirmed.**
- **Zero-build alternative:** `normalBufferEnable=0` is the fastest one-key test
  for the face-ordering candidate; `pedAmbientFloor=0` for the white-ped
  candidate.

---

## 6. What I do not know / what not to revert

- **The Sept-3 mode/preset is unknown.** If the target used the Xbox/PS2 pipe
  rather than PBR, the fastest look test is the mode A/B (`[Modes] active`), not
  the PBR shader reverts. Do that before touching PBR look constants.
- **The current build's exact INI** may differ from the target's; `ambientMult`,
  `pbrAmbientFloor`, `pbrIblAmbientWeight`, `vehPrelightFallback`,
  `bldSkyReflect` are new keys. A config-only rollback of those five may recover
  much of the daylight look without any shader edit.
- **Whether `CarPaint_Reflections.hlsl` is bound** today (some PBR helpers are
  orphans). Do not edit it until binding is verified.
- **Do not full-revert** `VehiclePBR_Modern.hlsl`, `vehiclePipeVS.hlsl`,
  `buildingPBRVS.hlsl`, `TonemapPass.hlsl`, or `Water_Parallax.hlsl`: they now
  carry the view-vector fix, NM lane (`s8/c48`), Noise (`s9/c49–c52`), Forward+
  (`s5–s7`), c7/c8 tonemap, and water SSR/reflValid contracts. The target look is
  recoverable by editing the look blocks above, not by rolling the files back.
- **Do not ship** `pedCullMode=2`/`3` as a fix for the face-order symptom. The
  cull helper’s own comment warns that forcing BACK inside-out’s mirrored CJ and
  kills stencil shadow volumes; use it only as a one-run diagnostic.

---

## Appendix — key diff/log anchors

| Anchor | Meaning |
|---|---|
| `cpp diff L3116-3133` | Tonemap toe formula inverted (`0.22+sceneLuma` vs `0.25-sceneLuma*0.8`); look delta, mostly inactive at current INI. |
| `cpp diff L1388-1414` | `pbrAmbientFloor` additive grey floor. |
| `cpp diff L1375-1380` | `GetTimecycleAmbient` old `*0.85` vs new live timecycle; target buildingAmbient ≈ 0. |
| `shader diff L1805-1810, L1935` | Vehicle PBR old additive `layer1 + layer2` vs new `lerp(layer1, envTerm, kr)`. |
| `shader diff L2077-2096` | `main_building` old additive IBL vs new multiplicative sky tint. |
| `shader diff L3872-3887` | `buildingPBRVS` old prelight+ambient vs new live 7-light sum. |
| `shader diff L1998-2018` | `main_building` normal-buffer block added (view-space normal fed into world N). |
| `main.cpp:4060-4083` | Current `ONE/ONE → 5/6` substitution — night-light root. |
| `main.cpp:2952-2954`, `:3340-3342` | Current shield default `SRCALPHA/ONE`. |
| `main.cpp:2110-2123` | Current ped ambient floor 0.20. |
| Live log | `[CoronaDraw]` 230/232 pre-force `src=5 dst=6`; `[CoronaShield]` 314× forced additive; `[Cull] enter saved=1/2`; live config `normalBufferEnable=1`, `vehPBRLayers=255`, `pedAmbientFloor=0.20`, `pbrIblAmbientWeight=0.8`, `ambientMult=1.0`, `pbrAmbientFloor=0.05`, `vehPrelightFallback=0.15`, `bldSkyReflect=0.15`. |
