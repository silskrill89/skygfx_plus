# 2026-10-30 — Shader diff 2026-09-03 → now (correlation study, read-only)

Baseline for every diff below is **c270294** (2026-08-30, the last commit before 2026-09-03 —
no shader commit exists between 08-30 and 09-03). Target = **current working tree** (committed
HEAD b7396c4 2026-10-02 **plus** uncommitted changes; deployed `E:\games\gtasa_skygfx_plus\skygfx.asi`
mtime 2026-10-29 21:58, i.e. built from this tree, CSOs baked 2026-10-26).

```
git diff --stat c270294 -- shaders/        → 57 files, +3387 / −2319  (tracked)
git status --porcelain shaders/            → 45 M/D + 24 untracked new .hlsl
git log --since=2026-09-03 --oneline -- shaders/ → 20 commits (18f5303..b7396c4)
```

Scope of the churn between the Sept-3 look and now, by date:

| Window | Commits | Theme |
|---|---|---|
| 09-07 → 09-09 | 94dcd37..e39a6f8, b5d36a9, f36296a | vehicle env gloss sessions (bisect, clearcoat, perspective env) |
| 09-10 → 09-11 | 7b8cf1f, 191dbe7, 01204d7, ca922ef, a6d56f8, 671bcd7 | **view-vector inversion fix**, CryEngine auto-exposure, hemisphere ambient, inverted-toe fix |
| 09-15 | 59bd64f | crash-4 fix + SMAA/SSAO guards |
| 10-02 | b7396c4 | render-state integrity, SMAA temporal fix, occluder hunt |
| uncommitted (10-26/29 tree) | — | NM normal-map lane, Forward+ light atlases, unifiedPipe classify/SSR, water flat-plane rewrite + body classification, underwater pass, clouds, bloom/DOF/LUT, GTAIV building VS rebuild |

So the "stable Sept-3 look" predates the **entire vehicle env-reflection rework**, the
auto-exposure tonemap, the hemisphere-ambient system, and the whole uncommitted W-wave.

---

## 1. File-by-file what changed

### 1.1 Committed + uncommitted, high-impact

| File | Δ (c270294→wt) | What changed, mechanism-grade |
|---|---|---|
| `shaders/ps/VehiclePBR_Modern.hlsl` | +911/−166 | Massive. See §2 break-down. **No discard/TEXKILL at either end** — alpha flows via blend, alpha-affecting edits are opacity-*increasing* (§2.3). |
| `shaders/ps/Water_Parallax.hlsl` | +1261/−136 | W7-B body contract: new `c14/c15` body params, `WaterBodyContract()` classifies OCEAN/SHALLOW/…/LAKE from `TEXCOORD5`, `WaterSplitBlend/FarBlendWater/WaterSSRBlend` compositor, SSR against the screen chain. Body colour families include the timecycle *deep-teal* colour. |
| `shaders/vs/Water_VS.hlsl` | +69/−68 | **Total rewrite**: Gerstner displacement removed → flat plane; waves now PS normal detail. `TEXCOORD5 = float4(IN.normal,0) * saturate(bodyParams(c10).x)` — the body-class code is carried in the **NORMAL vertex channel**, written into the vertex buffer per-flush by waterPipe.cpp. If c10 is stale (≠0) while the flush supplies **real normals**, each vertex gets a garbage body code (normal.x≈0 ⇒ code 0 = OCEAN teal). This is the shader-side ingredient of W1/W4 (see ranks). |
| `shaders/vs/buildingPBRVS.hlsl` | +68/−5 | New output `TEXCOORD5 = mul(envmat(c38), IN.Normal)` (camera-space normal, IV parity). Vertex colour rebalanced to "Sept-3 prelight-dominant": `prelight*surfDiff*1.0 + ambient*surfAmb*0.10 + Σ sun*surfLightScale*0.15`, now `saturate()`-ed. Sept-3 had effectively zero ambient (dead 0.0 static), so this adds ≤~2-4% trace of colour tint in shadow. |
| `shaders/ps/TonemapPass.hlsl` | +64/−90 | Auto-exposure mode: `lumTex` (new s1 sample) from LuminanceReduce/Adapt; `exposure *= lerp(1, key/lum, c7.w)` + soft-knee `lerp(1,0.5,kneeK)` gated by `c8.w`; black-lift `c7.x`; user pivot curve `c8`. Negative-exposure guard added (`max(c,0)` before Hable). `PostGrade` (adaptive B&C) **deleted**, folded into new `include/TonemapCommon.hlsl` (FilmicGrading + PivotCurve). If `lumTex` center is black → `key/lum` clamps to `c7.z` (up to 4.0 ⇒ blowout); if `c5.w` flag=1 but the reduce/adapt chain never rendered, exposure goes wild — **every frame brighter/darker than Sept-3 possible**. |
| `shaders/ps/PipeChain.hlsl` | +144/−16 | Pass-3 reworked into **BRDF-classified SSR composite**: new `classifyTex` s4, `packParams` c1, `fbParams` c3, `projInfo` moved c1→c4, depth-consistency + off-screen rejection, asymmetric hit test. Scene fetches now `× fbParams.xy` (padded 2048² buffer → screen UV). Plus pass-9 "classify pack" mode. Dead pack ⇒ SSR silently off (guarded), wrong pack ⇒ ink-blots (old issue, now gated). |
| `shaders/buildingPipePS.hlsl` | +19/−2 | `main_xboxBuilding`: **removed `texcoord0 * 0.5` halving and the `diffuse * 10.0` multiplier** ("aap parity"). Claims Xbox VS now writes `envcolor = (1,1,1,0)*saturate(shininess)*lightmult*kXboxEnvScale` and C++ rejects invalid/RT rasters + zero env scales. Legit colour-changing if the CPU side is out of step. Legacy simple/simpleDetail/simpleFog sources **deleted** — now emitted from this consolidated file via multi-entry in `tools/fast_build.py`. |
| `shaders/ps/Glass_Vehicle.hlsl` | +30/−16 | Env UV switched sphere-map → **perspective projection** (`c1.xy` reinterpreted as tanHalfFovX/Y — *register contract change for c1*), `viewRight/Up/Fwd` c25-c27 added. `envIntensity` no longer ×0.18 (`clamp(a,0.1,1.5)`), envStrength band 0.05–0.20 → **0.15–0.45**. Lens gloss tinted by lens colour. Brighter, busier windows than Sept-3. |
| `shaders/ps/Rubber_Vehicle.hlsl`, `CarPaint_Reflections.hlsl` | +3/−3, +12/−7 | NaN-guarded normalize + view-space env projection (same theme). No alpha discard changes. |
| `shaders/vehiclePipeVS.hlsl` | +49/−19 | `main_vehiclePBR`: **view-vector inversion fix** (`7b8cf1f`, after Sept-3): ViewDir is now surface→camera; `Envcolor` grazing term sign-corrected. **New `float4 vehLitFallback : register(c28)`** = config `vehPrelightFallback` (default **0.15**): for non-prelit geometry (log: 1793/1793 SA vehicles ship unprelit) `OUT.Color.rgb = max(OUT.Color.rgb, c28.rgb)` — a *grey floor* on all vehicle shading (not teal). Register moves: `eyePos_spec`/`campos` c35→c36, `texmat_leeds` c36→c40 — CPU must match (it does, per vehiclePipe.cpp upload sites). Edge-tessellation VS entry deleted. |
| `shaders/ps/SSAO*`, `SMAA_Temporal`, `NormalBuffer`, `LuminanceReduce` (new), `LuminanceAdapt` (new) | mod/new | NormalBuffer keeps view-space contract (comment-only change). SMAA_Temporal gains velocity/depth/normal samplers + history clamp (`histParams` c3) — unbound velocity ⇒ gated by `histParams.w>0`. LuminanceReduce = 4×4 tile luminance pyramid feeding TonemapPass s1. |
| `shaders/vs/GTAIVBuilding_vs.hlsl` | **new (+104)** | First-ever source for the no-source `GTAIVBuilding_vs.cso`. The header documents the OLD prebuilt CSO's defects: **it never read mesh texcoords** (emitted `(N·envmat).xy*envXform.zw` as TEXCOORD0 ⇒ UV scale collapsed to 0 for un-env-mapped materials ⇒ "flat, posterised colour"), and drew **night prelight at noon** (COLOR1 read as env weight). Rebuild matches `CustomPipeRenderCB_PS2` uploads (c30/c31 DN params, correct c37/c38/c38-40 envmat). CSO rebuilt 1596→920 B. Directly targets the "untextured flat building slab" defect under PIPELINE_GTAIV. |
| `shaders/vs/xboxBuildingVS.hlsl`, `xboxBuildingWindVS.hlsl`, `ps2BuildingWindVS.hlsl` | +47/−, +64/−, +16 | Wind entries merged/redirected; subfolder `xbox/` and `ps2/` duplicates deleted (root files are the single sources).
| new postfx family (untracked) | +~2000 | `Bloom_Extract/Downsample/Upsample/Composite`, `DOF_CoC/Bokeh/Tent/Combine`, `CA`, `Grain`, `Vignette`, `FXAA3`, `SunShafts`, `IVGrade`, `Mirror_Composite`, `TonemapLUT`, `TreeAlpha` (the **only** `discard` in the whole tree, its own alpha contract), `GrassInstanced`. All gated by config toggles; none shadow the three target symptoms while off. |
| new water/cloud/sky (untracked) | | `Water_Underwater.hlsl` (W7-D full-screen underwater volume fog + SSS, HeightFog-compatible contract — **white-blowout capable if depth/projInfo wrong**), `CloudsPS.hlsl` (premultiplied ONE/ONE contract; header warns SRCALPHA/ONE pairing = dark fringe), `IBL_SkyCloud.hlsl`, `DynamicSky.hlsl` — *the two sky files are unchanged since baseline (tracked, no diff)*. |
| `shaders/include/` | | **PBR_Common.hlsl: UNCHANGED** since c270294 (GGX/Smith/Schlick identical — the BRDF core is not the regression). `colorSpace.hlsl` +26/−3 (fyc scalar-splat fix + declares c28-c43 constants for unifiedPipe consumers — declared only where safe). Deleted includes `CarPaintNoise/PerlinNoise/StochasticSamplerPS/SubsurfaceScattering/SSAO.hlsli` were consolidated into `Noise.hlsl`/`SSAOCommon.hlsl` + the merged buildingPipePS (producers verified against CSO rebuild dates; no dangling `#include` — the 10-26 compile batch succeeded). |

### 1.2 VehiclePBR_Modern.hlsl break-down (the big one)

Per entry:

- **`main` (vehicle body)**: adds `vpos : VPOS`; screen-UV normal-buffer blend with hardened
  validity gates (long/anti-flip/view→world transform — was raw `dot>0.25` accept at Sept-3);
  NM lane (`nmTex s8`, `nmParams c48`, cotangent frame, gloss-in-alpha 85%);
  `layerCfg c46` layer bitmask gates **every** term incl. `layer1 = baseColor * LF(0)`
  (a wrong/stale c46 zeroes the body); WET-ROADS term (c20.z); perspective
  env-UV projection (c3.zw tanHalf, replaces SphereEnvMapUV); reflection-mask term
  (`maskTex s2` @ envReflUV); env validity fallback chain (env→ibl→sky;
  "no-content ⇒ envUsable=0 ⇒ no reflection"); `SkyHemisphereTint` multiplicative hue
  (clamp 0.6–1.5); c44 chromeParams (clearcoat strength, envSpecularityMult,
  vehEnvStrengthMode, standalone mobile glint); additive-env composite restored
  (`layer1 + layer2`, `kr = saturate(k)*0.5` instead of Sept-3's `lerp(body,env,envMask)`);
  clearcoat σ 0.05→0.25; alpha out `lerp(diff.a, 1.0, nmParams.w)`.
- **`main_building` (same file, buildings under Building=PBR)**: screen-UV normal-buffer
  blend (same gates), NM lane, `kD = EnergyConservation(F0)` (was view-dependent
  `1−F_Schlick`), `color = baseColor * SkyHemisphereTint(...)` (Sept-3: `baseColor + ibl*fallback`),
  **new bldEnv c47 env-reflection block** (perspective CamNormal UV `TEXCOORD5.xy*0.5+0.5`,
  Fresnel-weighted IV parody + sky-reflection superset with
  `BLD_SKY_LUM_FLOOR 0.10`, `BLD_ENV_WEIGHT_MAX 0.85`, `saturate(skyLum*6)` scaling),
  cluster-light Burley diffuse added, and **alpha out ≈ `diff.a * matA` with the
  "paint-marker" band lift: any material alpha in (0, 0.1) → forced 1.0, plus
  `lerp(…, 1.0, nmParams.w)`**.
- **`main_rubber`**: sky-lift `LF(4)`, cluster diffuse added, alpha `lerp(diff.a,1,c48.w)`.
- **`main_mobileVehicle`, `main_specCarFx`, `main_ps2EnvSpecFx`, `main_normMapVehicle`**:
  VPOS added; normMapVehicle env switched to sphere-UV + lerp-replace + cluster lights;
  mobile gains `ClusterLightingScreen` (atlas sampling). Alpha sources unchanged.

### 1.3 Register contract deltas (shader ← CPU)

| Register | Old | New | CPU upload verified at |
|---|---|---|---|
| PS c29 | `clusterLightPos[0..]` (constant-array F+ scheme, c29-c37, c45, c48-c111) | `screenSize` (screen UV for normalBuf); light atlases now s6/s7 + tile s5 (`tex2Dlod`, POINT) | buildingPipe.cpp:2627/2632, vehiclePipe.cpp:3429 |
| PS c44 | unused | chromeParams | vehiclePipe.cpp:2634 |
| PS c20 | unused | iblAmbient `{pbrIblAmbientWeight, vehEnvFallback, vehWetEnv, vehEnvMask}` | vehiclePipe.cpp, buildingPipe.cpp:2534 (building writes **1 float** — .y/.z/.w stale-but-unread per shader note) |
| PS c45 | `(offX,offZ,tileSize,lightCount)` | `(tileSizePx, gridW, gridH, lightCount)` — semantics reordered | forwardplus.cpp |
| PS c46 | unused | `layerCfg` bitmask | **only 2 sites: vehiclePipe.cpp:3109/3395** (risk, §3) |
| PS c47 | unused | bldEnv (buildings) | buildingPipe.cpp:2997 |
| PS c48 | ≤32 light constant array | nmParams `{bound,intensity,glossFromAlpha,alphaOverride}` | vehiclePipe.cpp:2846/2849/3022/3138, buildingPipe.cpp:2826/2829 |
| VS c28 | unused | vehPrelightFallback | vehiclePipe.cpp:2651 |
| VS (spec/mobile/leeds) | eyePos c35, texmat c36 | c36 / c40 | committed with the shader change |
| PS c1 (glass) | `{fresnel,shininess,specularity,lightmult}` | `{tanHalfFovX, tanHalfFovY, lightmult,…}` for Glass_Vehicle | glass cb (Env cb) |
| Water VS c9-c13 | wave constants | `c9` sunDir+brightness, `c10` bodyParams gate | waterPipe.cpp |
| PipeChain | c1 projInfo | c1 packParams, c3 fbParams, c4 projInfo, s4 classifyTex | postfx/unifiedPipe CPU |
| TonemapPass | c5/c6 | + c7 blackLift, c8 curveParams, s1 lumTex | postfx CPU |

Every gate's default in `main.cpp` is **ON** (`vehPBRLayers=255`, `bldEnvReflect=1`,
`vehEnvFallback=1`, `vehEnvMask=1`, `vehWetEnv=1`, `forwardPlusEnable=1`,
`pbrIblAmbientWeight=0.5`, `vehPrelightFallback=0.15`) ⇒ the Sept-3-equivalent
"none of the new terms" configuration is **not** reachable by defaults; the stable look
must be restored by INI overrides or partial reverts, not by assumptions of off-paths.

---

## 2. Symptom analysis — what in the shader diff can produce them

### 2.1 Untextured flat teal/grey slabs (W1/W4)

Already diagnosed with per-capture telemetry in
`docs/plans/2026-10-29-teal-quads-diagnosis.md` (H1: shared-vertex ocean-grid flush
fails `WbBufferCountsSane` (621v/3432i ⇒ ratio 5.53) → bails to vanilla
`CWaterLevel__RenderAndEmptyRenderBuffer`, which rasterises through **the water-pipe
shaders still bound** with no water constants uploaded). The shader diff adds a
*shader-side amplifier* to exactly that mechanism:

1. **`Water_VS.main` + `Water_Parallax.main_*` (W7-B body contract).**
   `OUT.bodyType = float4(IN.normal, 0) * saturate(bodyParams(c10).x)`.
   In the vanilla bail draw c10 is stale-but-nonzero (D3D9 constants persist) and the
   flush supplies **real water normals** (norm.x ≈ 0 ⇒ code 0 = OCEAN, c14/c15 stale);
   `WaterBodyContract` then classifies the entire mis-projected plane as a water body
   and paints it with that body's colour family — the **deep-water teal** family is
   exactly the measured #3E5C55 fill. Pre-Sept-3 the water VS had none of this; the
   slabs would have been texture-coloured, not teal.

2. **`Water_VS` flat-plane rewrite removed Gerstner displacement** — the plane geometry
   and the bail path now share the *un*-displaced layout; nothing else
   normalises the mangling, so the mis-projection is maximally visible (Giant slabs,
   razor edges at frame borders — captures 2/3/7/8).

3. **`GTAIVBuilding_vs` (old no-source CSO)** produced literally *"flat, posterised
   colour" + "night prelight at noon"* for BUILDING_GTAIV(2) — i.e. under
   PIPELINE_GTAIV, untextured flat slabs were the *valid old behaviour*; the new
   source (rebuilt into `resources/cso/` on 10-26) fixes UV/dn but **only if the
   deployed DLL embeds the rebuilt CSO** (it does: git-tracked binary changed). If the
   user's build predates that or the pipeline is GTAIV, slabs persist until
   re-deployment.

4. **`buildingPipePS.main_xboxBuilding` de-multiplier** (×0.5 UV and ×10.0 colour
   removed): if the C++ counter-scales (kXboxEnvScale illumination) ride `envcolor` but
   the Xbox cb keeps uploading the old `envcolor` = plain `(1,1,1,0)×…` without the
   `kXboxEnvScale` fold, Xbox-style building materials render **10× darker than Sept-3**
   (flattened, dark-grey slabs). Verify the Xbox cb actually saturates
   `shininess * lightmult * kXboxEnvScale` before shipping blame on the shader.

5. **`VehiclePBR_Modern.main` `layerCfg (c46) LF(0)` gate on `layer1`** — c46 is only
   uploaded by 2 call sites (vehiclePipe.cpp:3109/3395). If any additional cb binds
   `VehiclePBR_Modern.cso` (e.g. a future/side cb) without c46, register persistence
   reuses whatever the *previous* cb left ⇒ whole vehicle bodies collapse to env-only
   ghost. Currently the two sites appear to cover the binding callbacks, but the gate
   is a landmine (a stale 0 ⇒ black body, not teal; low priority but real).

### 2.2 Black coronas (§4.7)

*No shader-side mechanism found at uniform strength.* Reasoning:

- SA coronas/sun/sprite sets draw through the **fixed-function sprite flush**, not
  through any .hlsl in this project. Changed HLSL cannot make them black directly.
- Prior art `docs/plans/2026-10-24-corona-blackquad-diagnosis.md` and the
  10-29 teal-quads report both assign the black-quads family to the
  **sprite-flush blend=0/REPLACE render-state lane (C++)**, which the current build
  reportedly fixed (`CoronaDraw blend=1`, `CoronaShield forced ONE/ONE` receipts).
- Shader-reachable near-misses (rank low):
  (a) `TonemapPass` auto-exposure with a dead `lumTex` can sink the whole frame toward
  clamped minimum exposure — everything dims *including* coronas, but that's global, not
  per-corona.
  (b) `CloudsPS.main_clouds`'s header warning: premultiplied output paired with
  `SRCALPHA/ONE` ⇒ dark fringe on additive sprites; only applies to the mod's own
  cloud sprite batches (main.cpp SpriteFlush producer-aware pairing), not vanilla coronas.
  (c) `PipeChain.pass_output` ×-`fbParams.xy` fetch at padded-2048² UV — if `fbParams`
  is stale/mis-set the POST chain can imprint edge garbage, but it's a full-screen pass.

### 2.3 See-through solids (§4.1)

**No discard/clip/TEXKILL/alpha-test logic exists in building or vehicle shaders, at
Sept-3 nor now** (grep across `VehiclePBR_Modern.hlsl`, `buildingPipePS.hlsl`,
consolidated legacy entries: only `TreeAlpha.hlsl:91 discard` on its own gated lane).
So the classic "discard threshold removed/inverted" regression class is **ruled out**.
The alpha-affecting edits that remain:

- `main_building` alpha out: `saturate(diff.a * matA)` where **any matA in (0.0, 0.1) is
  lifted to 1.0** (paint-marker guard) — makes faded decals *more* opaque, not less.
- `main_building`/`main`/`main_rubber` alpha override `lerp(…, 1.0, nmParams.w)` — c48.w
  is CPU-driven per texture ("glass/tire c48.w=1.0 override"); a **mistaken w=1 on a
  cutout/semi-alpha texture** forces opacity (vehicle: solid overlays look "pasted";
  the *inverse* \*see-through\* direction is not reachable from this lerp).
- 🟡 **buildingPipePS.main_xboxBuilding alpha regression is the only real
  see-through mechanism in the diff**: previously `tex.a * color.a * 10.0` saturated to
  opaque (≥1 for any tex.a ≥ 0.1); now `tex.a * color.a` — a texture with alpha 0.5 or
  a material alpha 0.5 now blends at **half opacity** *if the CPU no longer enables
  D3D alpha-test for that legacy path* (the merged-file comment says blend decisions are
  CPU-driven: buildingPipe.cpp:2680/2860). Cutout leaf/fence planes rendered through
  the Xbox/simple building path could turn translucent. Median probability.
- 🟡 `main_normMapVehicle` rewrite keeps `diff.a`, no transparency change, but replaced
  the `diff + env` additive with `lerp(diff, env, a)` — darker grazing fields make the
  hull read as "windows see through" — perceptual, not alphá.

### 2.4 Underwater white blow-out (seen in captures 7/8)

New `Water_Underwater.hlsl` (W7-D): fullscreen `exp2(-fogDensity · volDepth / max(V·Front,1e-7))`
quat with `camAxisZ c5` and INTZ depth s1. Any corrupted `projInfo`/depth or `c4.w`
(camera-below-water metres) going negative ⇒ exp2 of a huge positive = white —
exactly the "bottom half white blowout". ≥fresh-onset vs Sept-3 (feature did not exist).

---

## 3. Ranked suspect list (file + entry + mechanism)

| # | Suspect | Mechanism | Symptoms hit | Confidence |
|---|---|---|---|---|
| 1 | `shaders/vs/Water_VS.hlsl main` + `shaders/ps/Water_Parallax.hlsl main_*` (W7-B body contract; c10 = gate, normal-channel code) | Vanilla water-grid bail (C++ WbBufferCountsSane) draws through the **new** water VS/PS with stale c10/c14/c15; REAL vertex normals become body-class codes → whole mis-projected plane classified/painted as the body family (deep-teal #3E5C55), no displacement to mask it | W1/W4 teal/grey slabs | **HIGH** — corroborated by telemetry doc H1 + colour match |
| 2 | `shaders/vs/GTAIVBuilding_vs.hlsl` (was embedded no-source CSO) | Old baked binary emitted `(N·envmat).xy*envXform.zw` as diffuse UV (collapses to one texel) + night prelight at noon ⇒ flat posterised untextured slabs; new source bakes correct uv/normal/COLOR0/COLOR1.a contract | "untextured flat slabs" if PIPELINE_GTAIV (4); persists until re-deploy | **HIGH (conditional)** |
| 3 | `shaders/ps/TonemapPass.hlsl main` (s1 lumTex; c5.w mode; c7/c8 un-uniform) | Auto-exposure with a dead/never-rendered Luminance chain → `key/lum` clamps to `c7.z` (≤4.0): global brightness blow-out or crush; `PostGrade` removal rewrites the whole colour grading relative to Sept-3 | global wash/blowout (incl. underwater captures' white) | **MEDIUM-HIGH** |
| 4 | `buildpipePS.main_xboxBuilding` (de-×10 colour, UV un-halving) | If Xbox cb didn't fold `kXboxEnvScale` into envcolor as the shader comment assumes, all Xbox-like materials render ~10× darker flat lanes; ×10 also used to make alpha ≥0.1 fully opaque | dark grey/flat slabs; faint see-through on low-alpha cutouts | MEDIUM |
| 5 | `VehiclePBR_Modern.main` (env-UV perspective projection c3.zw; `rawEnvHasContent`/`envUsable`/`kr·LF(1)` gating; SkyHemisphereTint ratio clamp) | Wrong/stale c3 or c20 yields dead reflections (paint flat) or arbitrary patch content (wash); `lf(1)` gating via c46 persistence risk | flat/dull paint, wash, mood shift vs Sept-3 (expected — this is post-Sept-3 rework by design) | MEDIUM |
| 6 | `VehiclePBR_Modern.main_building` (bldEnv c47 block + `SkyHemisphereTint` multiply + TEXCOORD5 dependency) | bldEnvReflect=1 default ON; if VS/PS interpolator contract mismatches any extant stale CSO (or c47 stale nonzero on non-PBR cbs), grazing walls lerp toward sky→ dark/black or washed planes | building planes darker/washed vs Sept-3 | MEDIUM (guards place black-screen walls but were "PENDING A/B") |
| 7 | `Water_Underwater.hlsl main` (new, W7-D) | exp2 fog with negative/garbage depth factor ⇒ white wash | underwater white blowout | MEDIUM |
| 8 | `VehiclePBR_Modern.main` normalBuf screen-UV blend + LF(7) | c29 screenP or s4 unbound on non-Env cbs → ssValid fail-closed (no blend) = benign; but if stale screenP ≠ real raster ⇒ wrong UV sampling — hardened gates make this unlikely to black | paint detail loss | LOW |
| 9 | `PipeChain.pass_output` (×fbParams fetches + classifyTex s4 SSR) | Stale fbParams → UV misfetch over whole scene; dead classify pack → silently disabled SSR (benign) | edge artifacts / non-effects | LOW |
| 10 | `SMAA_Temporal main` velocity samplers s2/s3/s4 + histParams | unbound velocity → `hasVel=false` gate = benign; stale velocities → ghosting | ghosting | LOW |
| — | **Coronas black §4.7** | All shader-side mechanisms weak; prior telemetry + resource receipts identify sprite-flush blend=0 REPLACE (C++), allegedly fixed in this build | §4.7 | Shader-side: LOW |

**Explicitly exonerated by the diff:** `include/PBR_Common.hlsl` (unchanged — GGX/Smith/Schlick
math identical since Sept-3), `IBL_SkyCloud.hlsl` & `DynamicSky.hlsl` (tracked, no diff),
`NormalBuffer.hlsl` (comment only), `colorSpace.hlsl` (FXC-compat splat only).

---

## 4. CSO staleness (resources/cso/ vs sources)

Method: `getmtime` of every `resources/cso/*.cso` vs every producing `.hlsl` (incl. rglob
include-hash rules in `tools/fast_build.py`; the build tool emits legacy per-file CSO names
from consolidated multi-entry producers, so name-mapping was done the same way).

- **0 CSOs older than their producing source** in the compiled set — the 2026-10-26
  bake rebuilt 124 CSOs; deployed ASI (10-29 21:58) embeds those.
- **Prebuilt / no-source CSOs (cannot be re-derived; keep in mind):**

  | CSO | Compiled | Source | Note |
  |---|---|---|---|
  | `SMAA.cso` | 2026-07-29 | none | CPU-through macro lane; **predates Sept-3 ⇒ part of the stable look** |
  | `customBuildingVS.cso` | 2026-07-29 | none | same |
  | `GTAIVBuilding_ps.cso`, `GTAIVVehicle_ps/vs.cso` | 2026-07-03 | none | pre-Sept-3; part of the Sept-3 look |
  | `GTAIVBuilding_vs.cso` | **rebuilt 10-26 from the new source** (git-tracked 1596→920 B) | `shaders/vs/GTAIVBuilding_vs.hlsl` | the fix for §2.1-3 |
  | `GTAIVForwardPlus_ps/vs.cso` | 2026-10-08 | none | **post-Sept-3, unaudited binary** — only reached from the GTAIV Forward+ lane; if happening to bind under other pipelines, an unaudited source could mangle output |
  | `neoVehiclePass1VS/2VS.cso` | 2026-09-30 | **deleted from shaders/** (multi-entry rows removed; comments mark the passes dead) | resource.rc still embeds them, but `CarPipe::vertexShaderPass1/2 = NULL` ⇒ unreferenced (safe) |

- **resource.rc unchanged since c270294** — it embeds `cso/*.cso` names, so all deleted
  legacy .hlsl sources are correctly superseded by the consolidated multi-entry
  producers (`buildingPipePS.hlsl` 5 entries, `noiseDetailBuildingPS.hlsl` 3,
  `vehiclePipeVS.hlsl` 7, `VehiclePBR_Modern.hlsl` 7, `Water_Parallax.hlsl` 3,
  `unifiedPipe.hlsl` 5, `CloudsPS.hlsl` full-screen row). Deletions are *not* a
  build-level staleness problem.
- Include-dependency staleness is covered by fast_build's mtime+size include hashing
  (`shaders/include/*` edits invalidate dependents — lines 145-168); the new shared
  includes (`TonemapCommon`, `BloomCommon`, `SSAOCommon`, `Noise`, `LUTOps`) are untracked
  but present in the tree that baked the 10-26 CSOs.

---

## 5. Bottom line for recovery of the Sept-3 look

1. Water body-contract + flat-plane rewrite and the water-grid bail interaction are the
   best-correlated shader-side components of W1/W4 (with the C++ sane-check bail as the
   prerequisite). Guard path: pin `c10 = (0,0,0,0)` sanitisation immediately before the
   vanilla bail draw, or force the Water VS to classify code 0 (vanilla) whenever
   TEXCOORD5 is not flushed-managed (the "VANILLA GUARD" note at Water_Parallax.hlsl:333
   is the intended safety net — verify it actually fires with stale c10).
2. PIPELINE_GTAIV users must run the **rebuilt** `GTAIVBuilding_vs.cso` (done in this
   tree) — the old baked one is a documented flat-building generator.
3. The whole vehicle/env/ambient/tonemap rework is *by design* different from Sept-3;
   to recover the look INIs must pin: `vehPBRLayers`, `bldEnvReflect=0`,
   `vehEnvFallback=0`, `vehEnvMask=0`, `vehWetEnv=0`, `pbrIblAmbientWeight=0`,
   auto-exposure OFF (c5.w path), `vehEnvGlint=0`, and tonemap `blackLift=0`.
   Alternatively revert `VehiclePBR_Modern.hlsl` + `buildingPBRVS.hlsl` +
   `TonemapPass.hlsl` + `buildingPipePS.hlsl` to c270294 state — the 4 commit-hunks
   (7b8cf1f..671bcd7, 59bd64f, b7396c4) would then be re-landed selectively.
4. No discard/alpha-test inversion exists anywhere; the only genuinely see-through-capable
   edit is the Xbox/simple building de-multiplier (suspect #4) — compare C++ blend setup
   before/after for the legacy alpha-test path.
5. CSO staleness is clean; do not burn time there. The only *unaudited* binaries shipped
   post-Sept-3 are the GTAIVForwardPlus pair (2026-10-08, no source).
