# Shader & INI Duplication Audit — 2026-10-27

READ-ONLY audit. No shader/source edits. Reference pattern: `shaders/include/Noise.hlsl`
(one include, one register block `c49-c52`, one sampler `s9`, many consumers).

Baseline (recorded, not rebuilt):

- `tools/fast_build.py` producer count: 113 compiled entries.
- `resources/cso/`: 124 `.cso` files (includes prebuilt GTAIV binaries and orphans).
- Parser keys: 253 active unique keys in `readIniFile()` + 1 commented
  (`ps2grassFiles`) = 254 key names; docs lists 257 keys (253 + 4 read outside
  `readIniFile`).
- Register space: PS samplers `s0-s15`, PS constants `c0-c223`; SM3.0 only.

---

## 1. Include graph + duplication

### 1.1 `shaders/include/*.hlsl` graph

| Include | LOC | Included by (`file:line`) | Compiled-entry reachability | Runtime reachability | Verdict |
|---|---:|---|---|---|---|
| `PBR_Common.hlsl` | 129 | `ps/VehiclePBR_Modern.hlsl:16`, `ps/Glass_Vehicle.hlsl:22`, `ps/Rubber_Vehicle.hlsl:21`, `ps/CarPaint_Reflections.hlsl:25` | Live: 10 CSOs (VehiclePBR 7 + Glass + Rubber + CarPaint) | Live (all four loaders exist) | Keep; expand as BRDF home |
| `Noise.hlsl` | 107 | `noiseDetailBuildingPS.hlsl:26`, `ps/CloudsPS.hlsl:87` | Live: 3 noise-detail CSOs + Clouds/cloudsSky | Noise-detail live (`buildingPipe.cpp:981`); Clouds unbound at runtime | Keep; Clouds is the dormant consumer |
| `LUTOps.hlsl` | 372 | `ps/TonemapLUT.hlsl:44` | Live: `TonemapLUT.cso` | Live: `postfx_newfx.cpp:96` | Keep |
| `colorSpace.hlsl` | 338 | `unifiedPipe.hlsl:9` | Build-live: 5 `unifiedPipe*.cso` | **Dead**: no `IDR_`/loader (`resources/resource.h`, `Resource.rc` have no unified entries) | Keep only until unified pipe is wired |
| `CommonMath.hlsl` | 112 | none (self only) | **Dead** | Dead | Intended home; wire as a source-only pass after Stage 5 |
| `PerlinNoise.hlsl` | 303 | none | **Dead** | Dead; also fails `ps_3_0` (bitwise `&`, see `docs/plans/2026-10-25-noise-system-audit.md:34`) | Delete |
| `CarPaintNoise.hlsl` | 79 | none | **Dead** | Dead; duplicate of `CarPaint_Reflections.hlsl:50-76` | Delete or wire as the live home |
| `SubsurfaceScattering.hlsl` | 205 | none | **Dead** | Dead; its would-be consumers (`SkinSSS`, `SkinEnhance`, `HairEnhance`) are also unbound | Delete with the dead SSS family |

Other shared headers under `shaders/ps/`:

| Header | LOC | Included by | Status |
|---|---:|---|---|
| `SMAA.hlsli` | 1369 | 7 live SMAA PS files (`SMAA_Edge.hlsl:21` … `SMAA_BlendNeighbor.hlsl:22`) | Live, single source — no duplication |
| `SSAO.hlsli` | 82 | none | **Dead helper**; duplicates `GetViewPos`/TBN logic that already exists inline in four SSAO files |

Doc drift: `docs/Shader Architecture.md:309-311` claims
`SubsurfaceScattering.hlsl -> SkinSSS/SkinEnhance/HairEnhance`,
`CarPaintNoise.hlsl -> CarPaint_Reflections/VehiclePBR_Modern`, and
`PerlinNoise.hlsl -> DynamicSky/IBL_SkyCloud/Water_Parallax`. **All three claims are
false in the current tree** (0 include sites except the files themselves).

### 1.2 Duplicated function bodies

`identical?` means whitespace/comment-normalised bodies compare equal.

| Symbol | Site A | Site B | Identical? | Proposed home |
|---|---|---|---|---|
| `Luminance` (Rec.709) | `ps/Bloom_Extract.hlsl:27` | `ps/Bloom_Downsample.hlsl:36`, `ps/Bloom_Composite.hlsl:26` | yes (3/3) | `include/BloomCommon.hlsl` |
| `KarisAverage` | `ps/Bloom_Extract.hlsl:48` | `ps/Bloom_Downsample.hlsl:42` | yes | `include/BloomCommon.hlsl` |
| `SoftThreshold` | `ps/Bloom_Extract.hlsl:33` | (only consumer) | n/a | `include/BloomCommon.hlsl` |
| `AdjustSaturation` / `ScreenBlend` | `ps/Bloom_Composite.hlsl:32/39` | (single) | n/a | `include/BloomCommon.hlsl` |
| `LinearToSRGB` (exact OETF) | `ps/TonemapPass.hlsl:69` | `ps/TonemapLUT.hlsl:60` | yes | `include/TonemapCommon.hlsl` |
| `PostGrade` | `ps/TonemapPass.hlsl:84` | `ps/TonemapLUT.hlsl:69` | yes | `include/TonemapCommon.hlsl` |
| `FilmicGrading` | `ps/TonemapPass.hlsl:130` | `ps/TonemapLUT.hlsl:100` | yes | `include/TonemapCommon.hlsl` |
| `PivotCurve1` / `PivotCurve` | `ps/TonemapPass.hlsl:171/191` | `ps/TonemapLUT.hlsl:127/144` | yes | `include/TonemapCommon.hlsl` |
| `FilmicTonemap` (Hable) | `ps/TonemapPass.hlsl:55` | `ps/GTA_SA_ModernColor.hlsl:35` (parameterised) | no | `include/TonemapCommon.hlsl` (if GTA_SA is wired) |
| `Luma` / `Lum` (Rec.709) | `ps/ColorFilter_CrossMix.hlsl:28`, `ps/SMAA_EdgeCombined.hlsl:34`, `ps/SMAA_EdgeMotionDepth.hlsl:40`, `ps/SMAA_Temporal.hlsl:46`, `ps/HairEnhance.hlsl:28`, `ps/SkinEnhance.hlsl:28` | `ps/LuminanceReduce.hlsl:17` (`Lum`) | yes (7/7) | `include/CommonMath.hlsl` (`LumaRec709`) |
| `D_GGX` | `include/PBR_Common.hlsl:27`, `ps/PBR_Lighting.hlsl:55`, `ps/SkinSSS.hlsl:32` | — | near: PBR_Common has `max(d,1e-7)`; Lighting omits guard; Skin adds `+0.0001` | `include/PBR_Common.hlsl` |
| `V_SmithCorrelated` | `include/PBR_Common.hlsl:37` | `ps/PBR_Lighting.hlsl:64` | yes | `include/PBR_Common.hlsl` |
| `F_Schlick` | `include/PBR_Common.hlsl:15`, `ps/PBR_Lighting.hlsl:74`, `ps/SkinSSS.hlsl:49` | — | near (variable name only) | `include/PBR_Common.hlsl` |
| `hash` / `noise3d` / `fbm` | `ps/DynamicSky.hlsl:33/37/51` | `ps/IBL_SkyCloud.hlsl:25/44/58` | yes (hash/noise3d/fbm) | `include/CloudNoise.hlsl` |
| `PBR_chash` / `PBR_cnoise3d` / `PBR_cloudFBM` | `include/PBR_Common.hlsl:78/79/88` | `DynamicSky`/`IBL` above | near: same math, renamed (`n+1153` etc. instead of `nr`) | `include/CloudNoise.hlsl` with `PBR_*` aliases |
| `hash22` / `perlinNoise` / `metallicFlake` | `ps/CarPaint_Reflections.hlsl:50/56/70` | `include/CarPaintNoise.hlsl:6/13/44` | near (comments only) | Delete dead header, or make it the single include |
| `GetViewPos` | `ps/SSAO.hlsl:31`, `ps/SSAO_VertexDepth.hlsl:28`, `ps/SSAO_ps20.hlsl:18` | — | yes (3/3); `SSAO_Temporal.hlsl:48` adds a signed-denom guard | `include/SSAOCommon.hlsl` |
| `ProjectToUV` | `ps/SSAO.hlsl:46` | `ps/SSAO_Temporal.hlsl:67` | yes | `include/SSAOCommon.hlsl` |
| `BuildTBN` | `ps/SSAO.hlsl:54` | `ps/SSAO_ps20.hlsl:34` | yes; `SSAO_Temporal.hlsl:117` differs (noise scale, no param) | `include/SSAOCommon.hlsl` |
| `UnpackNormal` (`*2-1`) | `include/CommonMath.hlsl:62` | inline at `buildingPipePS.hlsl:142`, `ps/normMapBuildingPS.hlsl:39`, `ps/PBR_Lighting.hlsl:84`, `ps/PipeChain.hlsl:138`, `ps/HairEnhance.hlsl:38`, `ps/SSAO_Temporal.hlsl:81` | near | `include/CommonMath.hlsl` |
| `SafeNormalize2` guard | `ps/CA.hlsl:35` | guarded normalise patterns in `PBR_Common.hlsl:57`, `SSAO_Temporal.hlsl:111`, `SSS_Blur.hlsl:66` | near | `include/CommonMath.hlsl` (`SafeNormalize`) |
| `LinearToSRGB` fast (`pow 1/2.2`) | `include/CommonMath.hlsl:107` | `include/colorSpace.hlsl:91` | near (duplicate name) | `include/ColorSpaceCommon.hlsl` (rename exact/fast) |
| `LinearizeDepth` | `ps/PBR_Lighting.hlsl:49` | `ps/DOF_CoC.hlsl:53`, `ps/Water_Parallax.hlsl:154` | no (three formulas) | Keep per-pass; do not merge |

Highest-value observations:

- `TonemapPass`/`TonemapLUT` are a **verbatim 5-function copy**; only the tone curve differs.
- `SSAO`/`SSAO_VertexDepth`/`SSAO_ps20` share an identical `GetViewPos`; `SSAO_Temporal`
  is a guarded fork (the fork is the corrected one).
- `CarPaint_Reflections` carries the entire dead `CarPaintNoise.hlsl` body inline.
- `PBR_Lighting` is a near-copy of `PBR_Common`, and it is loaded but never bound
  (`pipelinecommon.cpp:799`; no consumer of `PBR_Lighting` anywhere else).

---

## 2. Register / sampler collision map

### 2.1 High-risk reservations

| Register | Owner file(s) | Semantic | Conflicts with | Same-draw? | Verdict |
|---|---|---|---|---|---|
| `s5` | `ps/VehiclePBR_Modern.hlsl:58` (`clusterTex`) | Forward+ tile index atlas | `ps/SSAO_Temporal.hlsl:32` (`prevDepthTexture`) | No — SSAO draw vs vehicle draw | **Pass-exclusive, safe** (explicit clears: `vehiclePipe.cpp:3601-3602`, `postfx_ssao.cpp:1188-1194`) |
| `s6` | `ps/VehiclePBR_Modern.hlsl:76` (`lightPosTex`) | Forward+ light position/radius atlas | `ps/SSAO_Temporal.hlsl:33` (`prevNormalTexture`) | No | **Pass-exclusive, safe** |
| `s7` | `ps/VehiclePBR_Modern.hlsl:77` (`lightColTex`) | Forward+ light colour/type atlas | none | — | Safe |
| `s8` | `ps/VehiclePBR_Modern.hlsl:27` (`nmTex`) | Texture normal map / gloss alpha | building NM lane `buildingPipe.cpp:2791` (`SetTexture(8)`) | No — building vs vehicle draws | Pass-exclusive, safe (same semantic) |
| `s9` | `include/Noise.hlsl:42` (`uniNoiseTex`) | Packed Perlin/Worley/white/blue tile | `ps/Grain.hlsl` | **No** — Grain declares only `s0`/`c0` (`Grain.hlsl:44-46`); its old mode-2 s9 path is removed (`Grain.hlsl:7-14`) | **Collision refuted** |
| `c40-c43` | `include/colorSpace.hlsl:37-40` | Grading/IBL parameters | `VehiclePBR` c44+ | No overlap; `colorSpace` consumer `unifiedPipe` is dead | Safe but dead |
| `c44` | `ps/VehiclePBR_Modern.hlsl:72` (`chromeParams`) | Chrome F0/gloss/boost/clearcoat | VS `vs/mobileBuildingVS.hlsl:12` and `vs/sphereBuildingVS.hlsl:12` (`campos`) | PS vs VS register spaces are separate | Safe |
| `c45` | `ps/VehiclePBR_Modern.hlsl:59` (`clusterParams`) | Tile size/grid/light count | none (only VehiclePBR) | — | Safe |
| `c46` | `ps/VehiclePBR_Modern.hlsl:60` (`layerCfg`) | `vehPBRLayers` bitmask | none | — | Safe |
| `c47` | `ps/VehiclePBR_Modern.hlsl:64` (`bldEnv`) | Building env enable/weights | none | — | Safe |
| `c48` | `ps/VehiclePBR_Modern.hlsl:55` (`nmParams`) | NM lane enable/strength/alpha | none; `Noise` starts at c49 | — | Safe |
| `c49-c52` | `include/Noise.hlsl:43-46` | Noise knobs `{scale, worley, coverage, erosion}`, `{white, blue, time, frame}`, cloud scales, jitter/dither | `VehiclePBR` c44-c48 | No overlap; adjacent blocks | **Confirmed safe** |
| `c0-c4` | per-pass (`TonemapPass.hlsl:38`, `SSAO.hlsl:22`, `Grain.hlsl:46`, postfx passes) | Per-pass params | Reused by design | Each draw uploads its own | Safe by pass-exclusivity |

### 2.2 Explicit confirmations

- **s5/s6/s7 (Forward+ vs SSAO_Temporal history): SAFE.** They are never used by the
  same draw. The vehicle path binds them only when `c45.w > 0` (`forwardplus.cpp:1569-1574`),
  and the SSAO temporal path binds s5/s6 then clears them (`postfx_ssao.cpp:989-990`,
  `1188-1194`). The vehicle callback also clears s5-s7 (`vehiclePipe.cpp:3601-3602`).
  This is pass-exclusive, not same-draw. The only real risk is a future draw that
  inherits both sets without rebinding; add the reservation to the shader contract.
- **s9 (Noise vs Grain): REFUTED.** `Grain.hlsl` reads `s0` and `c0` only. The former
  IV texture-grain mode that read s9/c49-c52 was removed because its caller never
  bound the tile (`Grain.hlsl:7-14`). No same-draw conflict.
- **c45-c52: CONFIRMED, no overlap.** Vehicle owns `c45-c48`; Noise owns `c49-c52`.
  A shader that includes both `PBR_Common.hlsl` and `Noise.hlsl` would use both blocks
  without collision. `Noise.hlsl` is currently included only by `CloudsPS` and
  `noiseDetailBuildingPS`, neither of which uses c44-c48.
- **No unsafe same-draw sampler collision found.** Every s5/s6/s7 owner is a distinct
  pass with explicit bind/clear.

---

## 3. Dead shader sources

### 3.1 Not reachable from any compiled entry

Subdirectory copies are skipped by `tools/fast_build.py:482-484` (stem collision) or by
the explicit skip list (`:464-472`). `_no_output_reason()` (`:608-618`) classifies them.

| Source | LOC | Proof of unreachability | IDR/CSO embedded? | Recommendation |
|---|---:|---|---|---|
| `ps/mobile/mobileVehiclePS.hlsl` | 3 | Subdir stem collides with root; root itself is skip-listed; CSO comes from `VehiclePBR_Modern.hlsl` `main_mobileVehicle` | `IDR_MOBILEVEHICLEPS -> mobileVehiclePS.cso` (`Resource.rc:82`) | Delete |
| `ps/mobileVehiclePS.hlsl` | 3 | Skip list `:464`; wrapper includes `VehiclePBR_Modern.hlsl` | CSO emitted by multi-entry | Delete |
| `ps/normMapVehiclePS.hlsl` | 3 | Skip list `:464`; CSO emitted by `main_normMapVehicle` | `IDR_NORMMAPVEHICLEPS` embedded (`Resource.rc:98`), **no `makePS`** | Delete source and RC entry |
| `ps/xbox/xboxBuildingPS.hlsl` | 18 | Skip list `:464-469`; CSO emitted by `buildingPipePS.hlsl` `main_xboxBuilding` | `IDR_XBOXBUILDINGPS` embedded | Delete (stale: no alpha contract) |
| `ps/simplePS.hlsl` | 13 | Skip list; CSO from `main_simple` | `IDR_SIMPLEPS` | Delete |
| `ps/simpleDetailPS.hlsl` | 21 | Skip list; CSO from `main_simpleDetail` | `IDR_SIMPLEDETAILPS` | Delete |
| `ps/simpleFogPS.hlsl` | 22 | Skip list; CSO from `main_simpleFog` | `IDR_SIMPLEFOGPS` | Delete |
| `vs/mobile/mobileBuildingVS.hlsl` | 47 | Stem collides with root | `mobileBuildingVS.cso` exists, **no `IDR`** | Delete (both root and subdir) |
| `vs/mobile/mobileVehicleVS.hlsl` | 63 | Stem collides with root; root CSO from `main_mobileVehicle` | `IDR_MOBILEVEHICLEVS` | Delete subdir copy only |
| `vs/ps2/ps2BuildingVS.hlsl` | 40 | Stem collides with root | `IDR_PS2BUILDINGVS` | Delete (identical root) |
| `vs/ps2/ps2BuildingFxVS.hlsl` | 34 | Stem collides with root | `IDR_PS2BUILDINGFXVS` | Delete (identical root) |
| `vs/ps2/ps2BuildingSSSVS.hlsl` | 56 | Stem collides with root | `ps2BuildingSSSVS.cso` exists, **no `IDR`** | Delete (both root and subdir) |
| `vs/ps2/ps2BuildingWindVS.hlsl` | 74 | Stem collides with root | `IDR_PS2BUILDINGWINDVS` | Delete; root has c23 `windExt` (subdir is stale) |
| `vs/ps2/neoVehiclePass1VS.hlsl` | 63 | Dead pass skip `:480`; no loader (`neoCarpipe.cpp` NULL passes) | no IDR | Delete |
| `vs/ps2/neoVehiclePass2VS.hlsl` | 56 | Same | no IDR | Delete |
| `vs/xbox/xboxBuildingVS.hlsl` | 49 | Stem collides with root | `IDR_XBOXBUILDINGVS` | Delete; root has c5/c6 sun registers (subdir stale) |
| `vs/xbox/xboxBuildingWindVS.hlsl` | 82 | Stem collides with root | `IDR_XBOXBUILDINGWINDVS` | Delete; root has c23 `windExt` (subdir stale) |
| `vs/neoVehiclePass1VS.hlsl` | 63 | Dead pass skip `:480` | no IDR | Delete (root + CSO) |
| `vs/neoVehiclePass2VS.hlsl` | 56 | Dead pass skip `:480` | no IDR | Delete (root + CSO) |
| `include/CommonMath.hlsl` | 112 | No include sites | no CSO | **Keep**, wire it (after Stage 5) |
| `include/PerlinNoise.hlsl` | 303 | No include sites; fails `ps_3_0` | no CSO | Delete |
| `include/CarPaintNoise.hlsl` | 79 | No include sites | no CSO | Delete or wire |
| `include/SubsurfaceScattering.hlsl` | 205 | No include sites; all consumers dead | no CSO | Delete with SSS family |
| `ps/SSAO.hlsli` | 82 | No include sites | no CSO | Delete or wire |

### 3.2 Compiled but unreachable at runtime

| Source | LOC | CSO / loader situation | Recommendation |
|---|---:|---|---|
| `unifiedPipe.hlsl` | 171 | 5 `unifiedPipe*.cso`; **no `IDR_`/RC entry** | Dead build product. Either wire the forward+ chain or remove from `multi_entry` (`fast_build.py:534-544`) |
| `ps/CloudsPS.hlsl` | 255 | `CloudsPS.cso` 1420 B, `cloudsSkyPS.cso` 1556 B; `IDR_CLOUDPS 284`, `IDR_CLOUDSSKYPS 283` embedded, **no `makePS`/`FindResource` in `src/`** | Wire (it composes on s0+s9/c49-c52) or delete RC entries |
| `ps/PBR_Lighting.hlsl` | 157 | `IDR_PBR_LIGHTING 207`, `makePS` at `pipelinecommon.cpp:799`; handle never bound | Delete (redundant with `PBR_Common`) |
| `vs/pedPBRVS.hlsl` | 128 | `IDR_PEDPBRVS 275`, no `makePS` | Delete or wire ped PBR |
| `ps/SkinSSS.hlsl` | 107 | `IDR_SKINPBR 210`; deliberately not loaded (`pipelinecommon.cpp:802`) | Delete with SSS family |
| `ps/iiiTrailsPS.hlsl` | 15 | `iiiTrailsPS.cso` 448 B; **no IDR**; global `iiiTrailsPS` never assigned (`postfx.cpp:481`), passed as null at `:2088` | Delete, or add IDR + `makePS` (see Stage 8 note) |
| `vs/mobileBuildingVS.hlsl` | 47 | `mobileBuildingVS.cso` 804 B; no IDR/loader | Delete |
| `vs/postfxVS.hlsl` | 15 | `postfxVS.cso` 244 B; no IDR/loader | Delete |
| `vs/ps2BuildingSSSVS.hlsl` | 56 | `ps2BuildingSSSVS.cso` 784 B; no IDR/loader | Delete |
| `ps/SSAO_ps20.hlsl` | 109 | `SSAO_ps20.cso` 12 560 B; old `IDR_SSAOPS` now points at `SSAO.cso` (`Resource.rc:21-26`) | Delete |
| `ps/SSAO_ps20_simple.hlsl` | 54 | CSO 1372 B; no IDR | Delete |
| `ps/SSAO_ps20_depthonly.hlsl` | 10 | CSO 240 B; no IDR | Delete |
| `ps/HairEnhance.hlsl` | 101 | CSO 912 B; no IDR | Delete with SSS family |
| `ps/SkinEnhance.hlsl` | 81 | CSO 712 B; no IDR | Delete with SSS family |
| `ps/GTA_SA_ModernColor.hlsl` | 169 | CSO 1928 B; no IDR | Delete or wire as an alternate colour filter |
| `ps/IBL_SkyCloud.hlsl` | 116 | CSO 7024 B; no IDR; duplicate of `DynamicSky` noise | Delete or wire |
| `ps/GrassInstanced.hlsl` | 60 | CSO 284 B; `#ifdef IDR_GRASSINSTANCED` (`grasssys.cpp:726`) but `resource.h` has **no such define** | Delete, or add IDR + RC entry |

`ps/normMapVehiclePS.hlsl` is both a skip-listed legacy source and its emitted CSO is
embedded without a loader; treat it as dead source + dead RC entry.

---

## 4. INI plumbing duplication

### 4.1 Representative key table

`default` = effective absent-key default under the shipped `pipeline=PBR`,
`qualityPreset=3` (preset block `main.cpp:6116-6176`). `save` = `saveConfigTo()`.
Docs = present in `docs/ini-reference.md`.

| Key | Parsed (`main.cpp`) | Default site | Clamp site | Saved (`main.cpp`) | Docs | Consumed at |
|---|---|---|---|---|---|---|
| `noiseDetail` | 6224-6236 | `c->noiseDetail`; preset 3 = 1 (`6148`) | none | 7298 | Y | `buildingPipe.cpp:943,952,960` |
| `bldEnvReflect` | 6247 | 1 | 6248-6249 | 7301 | Y | `buildingPipe.cpp:2022,2911` |
| `bldSkyReflect` | 6250 | 0.15 | 6251-6252 | 7302 | Y | `buildingPipe.cpp:2974` |
| `envPower` | 6260 | `c->envPower`; preset 3 = **128** (`6169`) | 6264-6270 `[1,128]` | 7339 | Y | `vehiclePipe.cpp:2613` |
| `envFresnel` | 6275 | 0.4 | `clampEnvSliderRanges` 392-414 `[0,0.4]` | 7340 | Y | `vehiclePipe.cpp:2612,2971` |
| `vehEnvIntensity` | 6278 | 0.4 | same helper `[0,0.4]` | 7341 | Y | `vehiclePipe.cpp:3384` |
| `rgb1Mult` | 6419 | `c->rgb1Mult`; preset 3 = **0.88** (`6174`) | none | 7568 | Y | `postfx.cpp:1837-1843` |
| `rgb2Mult` | 6420 | `c->rgb2Mult`; preset 3 = **0.92** (`6175`) | none | 7569 | Y | same |
| `sunGlare` | 6328 | `c->doglare`; preset 3 = **1** (`6154`) | 6329-6332 negative -> 0 | 7312 | Y | `main.cpp` menu/patch path |
| `neoWaterDrops` | 6454 | `c->neoWaterDrops`; preset 3 = **1** (`6155`) | 6455-6457 negative -> 0 | 7313 | Y | `main.cpp` |
| `ssaoEnable` | 6498 | `c->ssaoEnable`; preset 3 = **1** (`6122`) | none | 7392 | Y | `postfx.cpp:1615` |
| `ssaoPower` | 6500 | `c->ssaoPower`; preset 2.0 (`6124`) | `clampSsaoPower` 482-491 `[0.5,4]` | 7394 | Y | `postfx_ssao.cpp:721,942` |
| `ssaoTemporalEnable` | 6694 | 1 | **none** | 7399 | Y | `postfx.cpp:1615`; `postfx_ssao.cpp:1027` |
| `ssaoTemporalBlend` | 6695 | 0.1 | **none** (menu 0.01-0.95 `debugmenu_ui.cpp:1172`) | 7400 | Y | `postfx_ssao.cpp:1027` |
| `ssaoBlurPasses` | 6696 | 2 | **none** (menu 0-3 `:1173`) | 7401 | Y | `postfx_ssao.cpp:1053` |
| `ssaoBlurRadius` | 6697 | 3.0 | **none** (menu 1-8 `:1174`) | 7402 | Y | `postfx_ssao.cpp:1075,1113` |
| `ssaoDepthThreshold` | 6698 | 0.01 | **none** (menu 0.001-0.1 `:1175`) | 7403 | Y | `postfx_ssao.cpp:1075,1113,1163` |
| `smaaEnable` | 6507 | `c->smaaEnable`; preset 3 = **1** (`6121`) | none | 7406 | Y | `postfx.cpp:469` |
| `motionBlurEnable` | 6510 | `c->motionBlurEnable`; preset 3 = **1** (`6127`) | none | 7409 | Y | `postfx.cpp:1549,2240` |
| `tonemapOperator` | 6868 | 0 | 6869-6870 `[0,5]` | 7476 | Y | `postfx_colourfilter.cpp:664` |
| `tonemapCurveLows` | 6301 | 0.0 | 445-453 `[-0.35,0.35]` | 7354 | Y | `postfx_colourfilter.cpp:610` |
| `tonemapBlackLift` | 6651 | 0.0 | none (consumer fallback 0.015) | 7577 | Y | `postfx_colourfilter.cpp:502` |
| `bloomQuality` | 6872 | 2 | 6873-6874 `[0,3]` | 7478 | Y | `postfx_newfx.cpp:688` |
| `sunShaftsDensity` | 6880 | 0.84 | 6881-6882 `[0,1]` | 7482 | Y | `postfx_newfx.cpp:572,577` |
| `dofFocusDistance` | 6859 | 20.0 | 6860 min 0 | 7472 | Y | `postfx_newfx.cpp:622,628` |
| `godRaysExposure` | 6683 | `c->godRaysExposure` (zero-init = 0) | none (consumer fallback 0.0034) | 7464 | Y | `postfx.cpp:4634,4691` |
| `heightFogDensity` | 6673 | `c->heightFogDensity` (zero-init = 0) | none | 7454 | Y | `postfx.cpp:4350` |
| `waterStyle` | 6843 | map default `Xbox` | none | 7605 | Y | `waterPipe.cpp:56` |
| `waterQuality` | 6844 | 3 | 6845-6846 `[0,3]` | 7606 | Y | `waterPipe.cpp:1100` |
| `waterParallaxEnable` | 6768 | 1 | none | 7519 | Y | `waterPipe.cpp:1091`; `postfx_ssao.cpp:338` |
| `grainEnable` | 6606 | 1 | none | 7555 | Y | `postfx.cpp:1974,2148`; `postfx_newfx.cpp:788` |
| `grainIntensity` | 6887 | 0.0 | 6888-6889 `[0,1]` | 7485 | Y | `postfx_newfx.cpp:788,806` |
| `forwardPlusEnable` | 6745 | 1 | none | 7496 | Y | `buildingPipe.cpp:1240,1301` |
| `forwardPlusDebug` | 6746 | 0 | none | 7497 | Y | **2 readers**: `main.cpp:6746` and `forwardplus.cpp:168` |
| `forwardPlusFallback` | not in main | `forwardplus.cpp:257` default 1 | none | emitted `6945` | Y (unresolved) | `forwardplus.cpp:257` |
| `enableNormalMaps` | not in main | `texdefs.cpp:981` default `"0"` | none | not saved by `saveConfigTo` | Y (unresolved) | `texdefs.cpp:981` |
| `ps2grassFiles` | 6337 (**commented**) | none | none | not saved | unresolved `docs:307` | **0 readers**; menu row `debugmenu_ui.cpp:1657`, skipped by INI export `:3514` |
| `forceWindShader` | 6626 | 0 | none | 7620 | Y | `main.cpp` global; **sticky**: only sets true, never false on reload |
| `sssPostProcessThreshold` | 6703 | `c->sssPostProcessThreshold` | none | 7419 | Y | **0 readers** |
| `skinWrapFactor` | 6710 | `c->skinWrapFactor` | none | 7424 | Y | **0 readers** |
| `hairAnisotropicPower` | 6716 | `c->hairAnisotropicPower` | none | 7431 | Y | **0 readers** |

### 4.2 Drift counts (no fixes applied)

1. **`docs/ini-reference.md` source fingerprint is stale.** Documented
   `main.cpp a6a17f2e0088`, actual `f592286a2cbb`; documented `skygfx.h 93389acd28b3`,
   actual `4a27b3f9cab9`; `modeManager.cpp c77536a9cb31` still matches
   (`docs/ini-reference.md:9`). Key set is still complete: 257 = 253 active keys +
   4 external/global reads; `ps2grassFiles` is correctly listed as unresolved.
2. **`skygfx.default.ini` is 4 keys short of docs, all intentional**: `buildingPipe`,
   `vehiclePipe`, `stochastic`, `stochasticTexturing` (`skygfx.default.ini:20-26`).
3. **9 `skygfx.default.ini` values disagree with the effective absent-key default under
   the file's own `qualityPreset=3` assumption**:

   | Key | `skygfx.default.ini` | Effective absent-key value | Evidence |
   |---|---:|---:|---|
   | `envPower` | 20.0 | 128.0 | `main.cpp:6169`, `6260` |
   | `sunGlare` | -1 | 1 | `main.cpp:6154`, `6328` |
   | `neoWaterDrops` | -1 | 1 | `main.cpp:6155`, `6454` |
   | `rgb1Mult` | 1.0 | 0.88 | `main.cpp:6174`, `6419` |
   | `rgb2Mult` | 1.0 | 0.92 | `main.cpp:6175`, `6420` |
   | `ssaoEnable` | 0 | 1 | `main.cpp:6122`, `6498` |
   | `smaaEnable` | 0 | 1 | `main.cpp:6121`, `6507` |
   | `motionBlurEnable` | 0 | 1 | `main.cpp:6127`, `6510` |
   | `vehEnvIntensity` | 1.0 | 0.4 (clamped) | `main.cpp:6278`, `6282` |

4. **`kKeyDefaults` (debug menu reset table, `debugmenu_ui.cpp:1733`) vs
   `skygfx.default.ini`: 20 numeric disagreements.** The notable clusters are
   `godRays*` (`:1747`), `heightFog*` (`:1749`), `sunCorona*`/`sunStreak*` (`:1752-1753`),
   `sunGlare` (`:1754`), `doRadiosity`/`blur*` (`:1768`, `:1823`), `vehEnvIntensity`
   (`:1785`), `neoWaterDrops` (`:1816`).
5. **`kKeyDefaults` vs parser sentinels: 5 disagreements.**
   `doRadiosity` 0 vs 4000 (`main.cpp:6435`), `blurLeft/Top/Right/Bottom` 0 vs 4000
   (`main.cpp:6426-6432`). The sentinel means "use stream.ini/game default"; resetting
   to 0 is a different semantic.
6. **Consumer fallbacks that disagree with parser defaults:**
   `tonemapBlackLift` 0.0 vs 0.015 (`postfx_colourfilter.cpp:502`),
   `tonemapMaxExposure` 1.0 vs 2.0 (`postfx_colourfilter.cpp:560`),
   `godRaysExposure` 0.0 vs 0.0034 (`postfx.cpp:4691`).
7. **Parser keys with no runtime consumer: 15.** `hairEnhanceEnable`,
   `hairAnisotropicPower`, `hairAnisotropicStrength`, `hairSSSStrength`,
   `vegetationEnhanceEnable`, `vegetationSSSStrength`, `vegetationAmbientBoost`,
   `sssPostProcessThreshold`, `sssIntensity`, `sssVegIntensity`, `sssSkinIntensity`,
   `sssClothIntensity`, `skinWrapFactor`, `skinSpecularPower`, `skinSpecularStrength`.
   These match the dead shader family in §3.2.
8. **Missing clamps vs menu ranges: 4 numeric SSAO keys** (`ssaoTemporalBlend`,
   `ssaoBlurPasses`, `ssaoBlurRadius`, `ssaoDepthThreshold`; `main.cpp:6695-6698` vs
   `debugmenu_ui.cpp:1172-1175`; `ssaoTemporalEnable` is an unclamped 0/1 bool).
   Negative/huge INI values reach the shader constants and the blur loop.

### 4.3 Default plumbing duplication map

Every key's default is written in up to five places:

| Site | Range | Count |
|---|---|---:|
| Read-site default (`readint/readfloat`) | `main.cpp:5958-6897` | 253 |
| `SKYGFX_EMIT_DEFAULT_KEYS` (`ADD_IF_MISSING`) | `main.cpp:6913-7025` | 112 |
| Fresh-INI writer (`cfg.set`) | `main.cpp:7026-7155` | ~100 |
| `saveConfigTo` writer | `main.cpp:7256-7626` | ~220 |
| `kKeyDefaults` reset table | `debugmenu_ui.cpp:1733-1824` | 235 |

This is the INI analogue of the shader duplication: one value, many literals. A single
X-macro table (key, section, type, default, clamp-lo, clamp-hi, owner field) would make
the parser, `ADD_IF_MISSING`, fresh-INI writer, save path and reset table read the same
row. The shader side cannot share that table directly, but the keys that feed shader
registers (`noiseDetail`, `ssao*`, `tonemap*`, `water*`, `grain*`, `envPower`,
`envFresnel`, `vehEnvIntensity`) should be marked in it so register audits and INI
audits use the same contract.

---

## 5. Match-together candidates

Each proposal names files, why they belong together, register impact, instruction impact,
risk, and whether it removes a compiled entry.

### P1 — Tonemap common include (strongest shader match)

- Files: `ps/TonemapPass.hlsl`, `ps/TonemapLUT.hlsl` → `include/TonemapCommon.hlsl`.
- Why: five functions are byte-identical (`LinearToSRGB`, `PostGrade`, `FilmicGrading`,
  `PivotCurve1`, `PivotCurve`); only `FilmicTonemap` vs `LUT_ApplyOperator` differs.
  Both entries share `s0/s1`, `c5-c8`; `TonemapLUT` adds `c9`.
- Register impact: none. `c5-c9` contract unchanged; `s0/s1` unchanged.
- Instruction impact: zero for the shared functions (inline expansion identical).
  Keep two entry points. Do **not** merge the two entries into one operator-branch PS.
- Risk: low. CSO hashes should stay identical.
- Saves: ~150 source lines; **0 compiled entries**.

### P2 — Bloom common include

- Files: `Bloom_Extract.hlsl`, `Bloom_Downsample.hlsl`, `Bloom_Upsample.hlsl`,
  `Bloom_Composite.hlsl` → `include/BloomCommon.hlsl`.
- Why: `Luminance` appears 3×, `KarisAverage` 2×, `SoftThreshold`/`AdjustSaturation`/
  `ScreenBlend` are helper-only.
- Register impact: none (`s0`, optional `s1`, `c0` per pass).
- Instruction impact: zero.
- Risk: very low.
- Saves: ~80 source lines; **0 compiled entries**.

### P3 — `CommonMath.hlsl` adoption

- Files: `SMAA_Temporal.hlsl`, `ColorFilter_CrossMix.hlsl`, `MotionBlur_Burnout.hlsl`,
  `SMAA_EdgeCombined.hlsl`, `SMAA_EdgeMotionDepth.hlsl`, `LuminanceReduce.hlsl`,
  `SSAO.hlsl`, `HairEnhance.hlsl`, `SkinEnhance.hlsl`.
- Why: `LumaRec709`, `SafeDivide`, `SafeNormalize`, `UnpackNormal`, `LinearDepthProjInfo`
  already exist there; the call sites duplicate the same bodies.
- Register impact: none.
- Instruction impact: zero (inline).
- Risk: low; watch for duplicate function names (`Luma` vs `LumaRec709`) during migration.
- Saves: ~100 source lines; **0 compiled entries**.

### P4 — SSAO common include

- Files: `SSAO.hlsl`, `SSAO_VertexDepth.hlsl`, `SSAO_ps20.hlsl`,
  `SSAO_Temporal.hlsl` → `include/SSAOCommon.hlsl`.
- Why: `GetViewPos` is identical in SSAO/VertexDepth/ps20; `ProjectToUV` is identical in
  SSAO/Temporal; `BuildTBN` is identical in SSAO/ps20. Temporal's signed-denominator
  guard is the corrected version.
- Register impact: none (`s0-s6`, `c0-c4` already per-pass).
- Instruction impact: zero for identical helpers. If Temporal's guarded `GetViewPos` is
  adopted everywhere, SSAO/VertexDepth CSOs change by a few instructions; test that
  separately.
- Risk: low for identical-only extraction; medium if the guarded version is unified.
- Saves: ~60 source lines; **0 compiled entries**.

### P5 — Building PS common include

- Files: `buildingPipePS.hlsl` (163 LOC, 5 entries), `noiseDetailBuildingPS.hlsl`
  (114 LOC, 3 entries) → `include/BuildingPSCommon.hlsl`.
- Why: both repeat `PS_INPUT_SIMPLE/DETAIL/XBOX`, `tex0/tex1/tex2`, and
  `DoDirLightNm` (`buildingPipePS.hlsl:128`, `normMapBuildingPS.hlsl:20`).
  `Noise.hlsl` is already included only by the noise-detail file; the shared structs are
  the real duplication.
- Register impact: none; the noise entries keep `s9/c49-c52`, the non-noise entries
  remain unchanged if the include only supplies structs/samplers.
- Instruction impact: zero.
- Risk: low.
- Saves: ~60 source lines; **0 compiled entries** (still 8 CSOs).

### P6 — Dead SSS family + dead INI keys (biggest net deletion)

- Files: `include/SubsurfaceScattering.hlsl`, `ps/SkinSSS.hlsl`, `ps/SkinEnhance.hlsl`,
  `ps/HairEnhance.hlsl`; INI keys listed in §4.2 item 7; menu rows in
  `debugmenu_ui.cpp:1291-1308`, `1772-1779`.
- Why: none of the four shaders has an `IDR` loader (`SkinSSS` is deliberately not loaded;
  the others have no IDR at all), and 15 config keys have zero consumers. They match each
  other exactly: dead shader family ↔ dead config family.
- Register impact: none (none are bound).
- Instruction impact: none.
- Risk: low if the feature is truly abandoned; medium if a future skin/hair lane is
  planned. If planned, wire the whole family instead of deleting (then also add
  `pipeBindNoise(9)`/PBR_Common includes).
- Saves: ~500 source lines, 3 CSOs, 1 IDR, 15 INI keys; **0 live compiled entries**
  (3 dead CSO producers).

### P7 — `PBR_Lighting` deletion / BRDF unification

- Files: `ps/PBR_Lighting.hlsl` vs `include/PBR_Common.hlsl`.
- Why: `D_GGX`, `V_SmithCorrelated`, `F_Schlick` are near-copies. `PBR_Lighting` is
  loaded (`pipelinecommon.cpp:799`) but never bound.
- Register impact: none.
- Instruction impact: none while unbound; if wired, use `PBR_Common` and keep
  `s0-s3`/`c0-c7`.
- Risk: low (delete) / medium (wire).
- Saves: 157 source lines, 1 IDR + CSO; **0 live entries**.

### P8 — Noise-family consolidation

- Files: `ps/DynamicSky.hlsl`, `ps/IBL_SkyCloud.hlsl`, `include/PBR_Common.hlsl` →
  `include/CloudNoise.hlsl`; `ps/CarPaint_Reflections.hlsl` vs
  `include/CarPaintNoise.hlsl` → delete or single-home.
- Why: `hash/noise3d/fbm` are identical between DynamicSky and IBL_SkyCloud;
  `PBR_chash/PBR_cnoise3d/PBR_cloudFBM` are the same math with a prefix; CarPaint's
  `hash22/perlinNoise/metallicFlake` is copied from the dead include.
- Register impact: none if only functions move. **Do not** switch these to `Noise.hlsl`
  s9: their callers do not call `pipeBindNoise(9)` (see `Grain.hlsl:7-14` for the same
  trap). That would require C++ binding and would change the noise field.
- Instruction impact: zero if bodies are moved verbatim.
- Risk: low for DynamicSky/IBL/PBR_Common aliasing; medium for CarPaint if the
  `Noise.hlsl` route is chosen.
- Saves: ~120 source lines; **0 compiled entries**.

### P9 — One INI default/clamp table (C++ match-together)

- Files: `src/core/main.cpp` (read/emit/new-INI/save), `src/core/debugmenu_ui.cpp`
  (`kKeyDefaults`), `src/core/modeManager.cpp` (profile defaults).
- Why: one value is written 3-5 times; 15 keys are dead; 4 numeric SSAO clamps are missing;
  `forceWindShader` is sticky; `ps2grassFiles` has a writer and no reader.
- Register impact: none directly; the table should tag shader-fed keys.
- Instruction impact: none.
- Risk: medium (broad C++ touch, INI round-trip required).
- Saves: ~150 duplicated literals and one class of drift; **0 compiled entries**.

### Rejected merges (PS would exceed ~300 slots / different passes)

| Rejected merge | Reason |
|---|---|
| `SSAO.hlsl` + `SSAO_Temporal.hlsl` into one PS | Each is 18 980 B / 21 868 B; temporal-only history branches would push the combined PS well past the 300-slot budget. Keep two entries, share only helpers. |
| `VehiclePBR_Modern.hlsl` 7 entries into one | `VehiclePBR_Modern` 12 560 B + `BuildingPBRPS` 11 856 B + norm/mobile/spec/rubber/ps2Env; a single PS would exceed budget and change every vertex layout. |
| `TonemapPass` + `TonemapLUT` into one entry with `c9` operator branch | `TonemapLUT` already 6216 B; adding the Hable path + operator switch raises static slots with no entry-saving payoff. Keep two entries, share include. |
| 4 Bloom passes into one | Different render targets/mip levels and different `c0` contracts. |
| `CloudsPS` + `noiseDetailBuildingPS` into one | Different passes, different vertex inputs; only the `Noise.hlsl` include is shared. |
| `CarPaint_Reflections` into `VehiclePBR_Modern` | Different PS input layout (`PS_INPUT` vs `PS_INPUT_*`) and different `IDR`/callback path. |

---

## 6. Ordered stage plan

Each stage is independently compilable. Verification command:
`python tools/fast_build.py --shaders` (plus a C++ build only where marked).
Compare the listed CSO SHA-256 (first 12 hex) and byte sizes. Pure include
extractions should produce **byte-identical CSOs**; if a hash moves, inspect
instruction counts before accepting.

Baseline CSO fingerprints (size / sha256-12):

| CSO | Size | Hash | CSO | Size | Hash |
|---|---:|---|---|---:|---|
| `TonemapPass.cso` | 2780 | `b1defd73dab2` | `TonemapLUT.cso` | 6216 | `d4218fb10310` |
| `Bloom_Extract.cso` | 1952 | `9aed5f40ad96` | `Bloom_Downsample.cso` | 2740 | `dee853b813a2` |
| `Bloom_Upsample.cso` | 920 | `d2842c9eb97c` | `Bloom_Composite.cso` | 560 | `7485b2bf07f7` |
| `SSAO.cso` | 18980 | `4f2af9997564` | `SSAO_Temporal.cso` | 21868 | `5de629d2dcc4` |
| `SSAO_VertexDepth.cso` | 5876 | `6bdf45024a07` | `SSAO_ps20.cso` | 12560 | `88a8e8502327` |
| `VehiclePBR_Modern.cso` | 12560 | `fe2139b76bb5` | `BuildingPBRPS.cso` | 11856 | `6a69adaf734a` |
| `simplePS.cso` | 288 | `b18a1e56ed88` | `simpleDetailPS.cso` | 472 | `5f867466658d` |
| `simpleFogPS.cso` | 504 | `580764da5715` | `xboxBuildingPS.cso` | 336 | `86f65e6205d4` |
| `normMapBuildingPS.cso` | 1540 | `65c96e2e8ff5` | `simpleNoiseDetailPS.cso` | 536 | `ac906db76f30` |
| `simpleDetailNoiseDetailPS.cso` | 740 | `b360f13e7a84` | `xboxBuildingNoiseDetailPS.cso` | 600 | `aa499bd8ab78` |
| `CloudsPS.cso` | 1420 | `607827d671e1` | `cloudsSkyPS.cso` | 1556 | `1e258504e186` |
| `PBR_Lighting.cso` | 2348 | `ea4deaa1b967` | `SkinSSS.cso` | 2108 | `fd9de1bdfcdd` |
| `Grain.cso` | 3756 | `907a478c9648` | `Water_Parallax.cso` | 9100 | `73477a6053ef` |

### Stage 0 — Census baseline (no file changes)

- Record the table above and `tools/fast_build.py` census (`113 entries`,
  `producer_sources`, `orphans`).
- Gate: no compile errors, no `UNEXPLAINED no-CSO HLSL`.

### Stage 1 — Delete the non-compiling dead header (safest)

- Delete `include/PerlinNoise.hlsl`.
- Why first: 0 include sites, no CSO producer, cannot affect a compiled output, and the
  file cannot compile on `ps_3_0` anyway. `CarPaintNoise.hlsl` is deferred to Stage 8d
  so P8 can still choose it as the CarPaint single-home.
- Verify: `--shaders`; all 113 entries unchanged; every CSO hash identical; census
  `unexplained` unchanged.

### Stage 2 — CloudNoise extraction

- Add `include/CloudNoise.hlsl` with `hash`, `noise3d`, `fbm`; include from
  `ps/DynamicSky.hlsl` and `ps/IBL_SkyCloud.hlsl`; delete their inline bodies.
- Optionally alias `PBR_chash/PBR_cnoise3d/PBR_cloudFBM` from the same header and
  include it in `include/PBR_Common.hlsl`.
- Verify: `DynamicSky.cso`, all `VehiclePBR*` / `BuildingPBRPS.cso`,
  `Glass_Vehicle.cso`, `Rubber_Vehicle*.cso`, `CarPaint_Reflections.cso` hashes
  **must stay identical**. If a PBR CSO changes, stop and inspect (the prefixed math is
  mathematically identical, so a hash change means codegen order changed).

### Stage 3 — TonemapCommon extraction

- Add `include/TonemapCommon.hlsl`; move `LinearToSRGB`, `PostGrade`, `FilmicGrading`,
  `PivotCurve1`, `PivotCurve`; include from both tonemap files.
- Verify: `TonemapPass.cso` `b1defd73dab2` and `TonemapLUT.cso` `d4218fb10310`
  unchanged.
- Do not merge entries (see rejected merges).

### Stage 4 — BloomCommon extraction

- Add `include/BloomCommon.hlsl`; move the five helpers; include from four Bloom files.
- Verify: the four Bloom hashes above unchanged.

### Stage 5 — SSAOCommon extraction

- Add `include/SSAOCommon.hlsl` with the identical `GetViewPos`, `ProjectToUV`,
  `BuildTBN` bodies; include from `SSAO.hlsl`, `SSAO_VertexDepth.hlsl`,
  `SSAO_ps20.hlsl`; leave `SSAO_Temporal.hlsl`'s guarded fork in place for this stage.
- Verify: `SSAO.cso` `4f2af9997564`, `SSAO_VertexDepth.cso` `6bdf45024a07`,
  `SSAO_ps20.cso` `88a8e8502327` unchanged; `SSAO_Temporal.cso` unchanged.
- Follow-up (separate, opt-in): adopt Temporal's guard in the shared helper. That will
  change `SSAO.cso` and `SSAO_VertexDepth.cso`; re-test AO and record the new hashes.

### Stage 6 — BuildingPSCommon + delete legacy sources

- Add `include/BuildingPSCommon.hlsl` with the structs/samplers/`DoDirLightNm`;
  include from `buildingPipePS.hlsl` and `noiseDetailBuildingPS.hlsl`.
- Delete skip-listed sources: `ps/simplePS.hlsl`, `ps/simpleDetailPS.hlsl`,
  `ps/simpleFogPS.hlsl`, `ps/mobileVehiclePS.hlsl`, `ps/normMapVehiclePS.hlsl`.
- Verify: the 8 building CSOs + `mobileVehiclePS.cso` / `normMapVehiclePS.cso`
  hashes above unchanged. `--shaders` still reports 113 entries (the CSO producers are
  the two consolidated root files).

### Stage 7 — Delete subdirectory duplicates

- Delete all 19 subdir/wrapper files in §3.1 (keep root files and their CSOs).
- Verify: 113 entries unchanged; all live CSO hashes unchanged. Census should show fewer
  "intentional skips" with no new producer.

### Stage 8 — Retire orphan compiled shaders

- 8a (no RC change, sources only): delete `vs/postfxVS.hlsl`,
  `vs/ps2BuildingSSSVS.hlsl`, `vs/mobileBuildingVS.hlsl`, `ps/iiiTrailsPS.hlsl`
  (or wire `iiiTrailsPS` if the III trails path is intended — it is currently null at
  `postfx.cpp:2088`).
- 8b (RC change): remove `IDR_PBR_LIGHTING`, `IDR_SKINPBR`, `IDR_PEDPBRVS`,
  `IDR_NORMMAPVEHICLEPS` and their `Resource.rc` lines; delete the matching sources
  and CSOs. Remove the `makePS(IDR_PBR_LIGHTING)` line at `pipelinecommon.cpp:799`.
- 8c (feature decision): `CloudsPS` / `cloudsSkyPS` — either add a `makePS` + draw
  (the shader is already s0+s9/c49-c52 compatible) or delete `IDR_CLOUDPS 284`,
  `IDR_CLOUDSSKYPS 283` and the two CSOs. `unifiedPipe*` has no RC entry; remove its
  five `multi_entry` rows if the forward+ chain is not being wired.
- 8d: `SSAO_ps20*` (three CSOs), `SkinEnhance` / `HairEnhance` /
  `GTA_SA_ModernColor` / `IBL_SkyCloud` / `GrassInstanced`, plus the dead includes
  `include/SubsurfaceScattering.hlsl` and `include/CarPaintNoise.hlsl` — delete source +
  CSO; add `IDR_GRASSINSTANCED` + RC only if grass instancing is being finished.
- Verify: `--shaders` reports fewer entries/CSOs; the remaining CSO hashes unchanged;
  `rc.exe`/link succeeds (C++ build required for 8b/8c).

### Stage 9 — INI single-table + clamp repair (C++)

- Introduce one X-macro row table (key, section, type, default, lo, hi, save, shader-fed).
  Generate `readIniFile` defaults, `SKYGFX_EMIT_DEFAULT_KEYS`, fresh-INI writes,
  `saveConfigTo` writes and `kKeyDefaults` from it.
- Fix the 4 numeric SSAO clamps (+ the 0/1 bool), the 3 consumer/parser fallback drifts, `forceWindShader`
  stickiness, and the 15 dead SSS/skin/hair keys (delete with Stage 8d).
- Verify: C++ build; save/reload round-trip; `docs/ini-reference.md` regenerated and
  fingerprint updated. No CSO change is expected from this stage; run
  `--shaders` once to confirm all hashes above still hold.

### Stage ordering summary

| Stage | Risk | Compiled entries saved | Primary check |
|---|---|---|---|
| 0 baseline | none | 0 | census + hashes |
| 1 delete PerlinNoise | very low | 0 | all hashes identical |
| 2 CloudNoise | low | 0 | PBR + DynamicSky hashes |
| 3 TonemapCommon | low | 0 | Tonemap hashes |
| 4 BloomCommon | very low | 0 | Bloom hashes |
| 5 SSAOCommon | low | 0 | SSAO/VertexDepth hashes |
| 6 BuildingPSCommon | low | 0 | 8 building hashes |
| 7 delete subdir copies | low | 0 | 113 entries unchanged |
| 8 retire orphans | medium | 0 live (removes dead producers) | RC/build + remaining hashes |
| 9 INI table | medium | 0 | C++ build + round-trip |
