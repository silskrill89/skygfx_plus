# Washout PS-Side Audit — c270294 vs HEAD (2026-10-26)

## Scope & Method

Read-only audit of the daylight washout / green tint between the 2026-09-03 target
(`c270294`) and the current working tree. No source or shader was modified.

**Sources used**

- `docs/plans/regression/shaders-c270294-to-head.diff` — cited as `shader diff L####`.
- `docs/plans/regression/cpp-c270294-to-head.diff` — cited as `cpp diff L####`.
- `docs/plans/2026-10-26-regression-attribution.md` — prior attribution notes.
- Current tree: `shaders/ps/VehiclePBR_Modern.hlsl`, `shaders/vs/buildingPBRVS.hlsl`,
  `shaders/ps/TonemapPass.hlsl`, `shaders/ps/ModernColorFilterPS.hlsl`,
  `src/render/postfx_colourfilter.cpp`, `src/render/postfx.cpp`,
  `src/render/pipelinecommon.cpp`, `src/render/buildingPipe.cpp`,
  `src/render/vehiclePipe.cpp`, `shaders/vehiclePipeVS.hlsl`.
- Live INI: `E:\games\gtasa_skygfx_plus\skygfx.ini` (04:22, 2026-10-26).
- Runtime receipt: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (latest session).

**Evidence caveats (important)**

1. **The pre-extracted diffs are stale in two look-critical places.** They show
   `main()` ending in `layer2 = lerp(layer1, envTerm, kr); color = layer2`
   (`shader diff L1837`, `L1938`), but the current tree has already restored the
   additive Sept-3 composite: `layer2 = envTerm * (saturate(kr) * 0.5) * envTexMod`
   and `color = layer1 + layer2` (`VehiclePBR_Modern.hlsl:722-732`, `:811`).
   They also lack the current `iblGate` (building tint fade by capture luminance,
   `:1163-1166`) and the current softened `ClampAmbientToSky`/`ambientMult`
   (`pipelinecommon.cpp:78-191`). This audit quotes both the diff expression and
   the current expression wherever they differ.
2. **The live INI is not the live renderer state.** The INI says
   `pipeline=Xbox`, `buildingPipe=Xbox`, `vehiclePipe=Xbox`, `colorFilter=PC`,
   `colorFilterEnable=0`; the captured session logs
   `CF-SRC: effective pipeline=2 ... vp=10 colorFilter=8` and
   `[PF] TonemapPass ... identGrade=0`, i.e. Modern vehicle + Modern colour
   filter + full tonemap. A mode/profile/debug-menu writer changed the effective
   state. Conclusions about the tonemap path therefore use the log, not the INI.
3. **The prompt's "ambient ~0.4" is illustrative.** The live log's 20:00
   timecycle ambient was `final=(0.073,0.050,0.050)`, and its raw daytime values
   are typically 0.05-0.20, not 0.4. Calculations below show both the prompt
   value (0.4) and the live-scale value where the difference matters.
4. **`c270294` predates the PBR restore.** For materials that the Sept-3 build
   rendered through the Xbox/PS2 pipe, a "PBR shader delta" is a path change,
   not a like-for-like regression. The ranking distinguishes these.

**Metric used for "energy".** Linear pre-tonemap RGB, diffuse albedo 1.0,
`matCol = 1`, no specular unless stated. `prelight = 0.6`, `ambient = 0.4`
(prompt case) and a direct-light sum of `0.08` (the VS comment's own daylight
estimate: `directCol[0]~1.0 * NdotL~0.7 * diffuse~0.8 * kDirectScale 0.15`).

## 1. Shader `main()` / `main_building()` light-energy changes (c270294 → HEAD)

All expressions below are quoted from the diff; the "current" column is the
working tree when it has drifted from the diff. Entries are tagged **[ADD]** for
net energy, **[SUB]** for a removal, **[HUE]** for a chromatic change with only a
small luminance delta, and **[VIEW]** for view/energy re-distribution.

### 1.1 `main()` — vehicle PBR

| # | Term | c270294 (old) | HEAD (diff) | Current tree | Effect |
|---|------|---------------|-------------|--------------|--------|
| 1 | Daylight sky lift | absent | `float3 skyFill = tex2D(iblTex, float2(0.5, saturate(N.y*0.5+0.5))).rgb; ... baseColor *= 1.0 + skyFillLuma * 0.22 * LF(4) * saturate(N.y*0.5+0.5);` — `shader diff L1658-1668` | `:577-579` | **[ADD]** up to **+22 %** of base at `N.y=1, skyLuma=1`; typical sky-luma 0.5 / upward normal ≈ **+9 %**; zero at night/dark capture. |
| 2 | Env content | `float envMask = lerp(0.15, 0.50, clearcoatFresnel) * roughnessResponse; envMask = lerp(envMask, envMask*1.5, metallicFactor);` — `shader diff L1767-1772` | `float envContent = 1.2; if(chromeParams.z > 0.5) envCoverage = saturate(envIntensity); else envContent = 1.2 * envIntensity;` — `shader diff L1685-1704` | `:605-613` | **[ADD]** content ×1.2 vs implicit 1.0. Live `vehEnvStrengthMode=1`, so coverage=1.0 and the content is **not** scaled down. |
| 3 | Env sun glint | absent | `float envGlint = pow(saturate(dot(R_world, L)), envPow); float3 envTerm = iblBlend * reflTint * envContent * (1.0 + 0.5 * envGlint);` — `shader diff L1813-1821` | `:705-707` | **[ADD]** up to **+50 %** of the env layer where the mirror vector aligns with the sun; zero elsewhere. |
| 3b | `envGlint` exponent / coverage source | old `carcolsShine = saturate(fxParams.y)` (shininess) — old shader `:166` | new `envPow = max(fxParams.y,1)` and `carcolsShine = min(fxParams.w,4)` — `shader diff L1781-1792`, `L1819` | `:686`, `:705`; CPU still packs `fxParams = {fxSwitch, shininess, specularity, lightmult}` (`vehiclePipe.cpp:3217-3229`) | **[ADD/mismatch]** live `fxParams.y≈1.0` → glint exponent 1 instead of ~20 (broad +50 %); `fxParams.w=EnvMapLightingMult≈1.0` → carcolsShine maxed → `kr=F*3+0.05`. This is a constant-contract bug, not just tuning. |
| 4 | Clearcoat lobe width | `D_GGX(NdotH, 0.05)` / `V_SmithCorrelated(..., 0.05)` — `shader diff L1858-1859` | `D_GGX(NdotH, 0.25)` / `V_SmithCorrelated(..., 0.25)` — `shader diff L1860-1861` | `:760-761` (and loop `:777-778`) | **[VIEW]** broader clearcoat lobe; peak lower, coverage wider. Not a net multiplier, but spreads energy over more pixels. |
| 5 | Specular multiplier | no multiplier | `specTotal *= 1.5 * LF(2);  // visible sun glints` — `shader diff L1925` | `:799` | **[ADD]** **+50 %** on every direct/clearcoat specular term. |
| 6 | Rim multiplier | `sunContrib * rimFresnel * 0.10` — `shader diff L1922` | `sunContrib * rimFresnel * 0.18 * LF(3)` — `shader diff L1923` | `:797` | **[ADD]** **+80 %** on the Fresnel rim (grazing angles). |
| 7 | Sky tint on reflection | absent | `iblBlend *= SkyHemisphereTint(N);` — `shader diff L1774-1777` | `:671` | **[HUE]** ±6 % luminance at live weight 0.8 (ratio clamp 0.6..1.5, blend 0.12); the green/cyan skew source. |
| 8 | Sky blend weight | `iblBlend = lerp(iblBlend, skyParams.rgb, skyBlend * 0.5);` — `shader diff L1761` | `... * 0.3 * LF(5)` — `shader diff L1773` | `:667` | **[SUB]** 40 % less sky mixed into the env layer than old. |
| 9 | Env-source fallback | absent | `envSrc = envRefl; if(iblAmbient.y>0.5 && rawEnvHasContent<0.5){ envSrc=iblSample; if(dead) envSrc=skyParams.rgb; } ... envUsable=...` — `shader diff L1737-1755` | `:655-662` | **[ADD]** replaces a missing reflectionTex with IBL/sky. Old additive path simply added zero; current adds up to the full env layer where the old looked flat. |
| 10 | Cluster diffuse | specular only | `clusterDiffuse += baseColor * diffc * clCol.rgb * atten * kDv / 3.14159;` — `shader diff L1909-1914` | `:789-792` (`ClusterLighting`) | **[ADD] night/indoor only**; daylight point lights are normally off. |
| 11 | Composite | `float3 color = layer1 + layer2;` — `shader diff L1935` | `float3 color = layer2;` — `shader diff L1938` | **current is additive again:** `:722-732` (`layer2 = envTerm*(saturate(kr)*0.5)*envTexMod`) + `:811` (`color = layer1 + layer2`) | **[STALE DIFF]** the diff overstates body replacement. Current does not erase the body; treat the diff's `color=layer2` as historical, not current. |
| 12 | kD / kS | old had no `kD` use in `main()`; spec used `kD not used` | new `kDv = EnergyConservation(F0)` only for cluster diffuse | `:567`, `:790-792` | No daylight energy add. |

**Current `main()` envelope (face-on paint, base 0.3, spec S, skyLuma 0.5):**
`skyFill` ≈ +0.03; env content/glint ≈ +0.00 to +0.02; spec ≈ +0.5·S; rim ≈
+0.08·rimFresnel·sun; cluster 0. The uniform daylight add is **skyFill (~+9 %)**;
the peak add is **spec ×1.5** on sun-facing paint.

### 1.2 `main_building()` — building PBR

| # | Term | c270294 (old) | HEAD (diff) | Current tree | Effect |
|---|------|---------------|-------------|--------------|--------|
| 1 | Base IBL | `float2 iblUV = N.xy * 0.5 + 0.5; float3 iblSample = tex2D(iblTex, iblUV).rgb; float3 ibl = (dot(iblSample,iblSample) > 1e-6) ? iblSample * 0.15 : baseColor * surfProps.x * 0.5;` — `shader diff L2077-2081` | `float3 ibl = SkyHemisphereTint(N);` — `shader diff L2088-2090` | `:1162-1166`, then `ibl = lerp(1, ibl, iblGate)` | **[SUB + HUE]** removes the flat **+0.15·sky** additive fill; adds a multiplicative ±6 % tint. Net face-on is **darker** than c270294 when the sky is bright. |
| 2 | Composite | `float3 color = baseColor; ... color += ibl;` — `shader diff L2087`, `L2095-2096` | `float3 color = baseColor * ibl; ... color += specTotal; color += clusterDiffuse;` — `shader diff L2092-2097` | `:1170-1172` | **[SUB]** additive IBL removed; body modulated instead. |
| 3 | Env-mapped material | absent (no envMapTex sample in `main_building`) | `if(bldEnv.y > 0.0){ ... if(dot(envSample,envSample)>1e-6) color += envSample * F_atNdotV * bldEnv.y; }` — `shader diff L2121-2142` | `:1194-1215` | **[ADD]** up to `min(1.5*shininess, 0.20) * F * envSample`; face-on dielectric F≈0.04 → +0.008, grazing F→1 → **+0.20**. Only env-mapped materials; new IV/PS2 parity feature. |
| 4 | Sky reflection | absent | `float3 skyEnv = tex2D(iblTex, float2(0.5, saturate(R.y*0.5+0.5))).rgb; ... float envW = saturate(bldEnv.z * saturate(F_atNdotV.r) * skyValid); envW = min(envW, 0.85); ... color = lerp(color, skyEnv, envW);` — `shader diff L2143-2156` | `:1216-1258` | **[ADD/VIEW]** for non-env materials: mix weight up to **0.15×F** (the 0.85 cap is not binding at the default 0.15); brightness delta = `0.15×(sky−body)` ≈ **±0.03** for a 0.2 sky/body gap (up to +0.05 for a bright sky). Face-on F≈0.04 → ~0.006. |
| 5 | Normal-buffer blend | absent | `if(ambientColor.w > 0.5){ ... N = normalize(lerp(N, ssNormalWorld/..., 0.3*ssValid)); }` — `shader diff L1998-2018` | `:1043-1079` | **[VIEW]** not a direct light add; tilts N per pixel, which can raise `NdotL`, `F_atNdotV` and the sky-reflect `envW`. Local GGX/sky blowout source. |
| 6 | Cluster lights | existed (4/8 lights, world-XZ tile bug) | same concept, 32 lights + screen tile — `shader diff L2044-2073` | `ClusterLighting` `:1147-1150` | No daylight add; fixes night/indoor lights. |
| 7 | `iblGate` | absent | absent from diff | `:1163-1166`: `iblLum = dot(iblRaw,...); iblGate = saturate(iblLum*6); ibl = lerp(1, ibl, iblGate);` | **[SUB]** fades the tint out on dark/indoor captures; not present in the diff, reduces the green tint indoors. |
| 8 | Ambient c24 | `buildingAmbient` was a dead static = **0** (`cpp diff L1375-1380`); PS received it but added nothing | c24 only feeds `SkyHemisphereTint`'s denominator and the normal gate | `:1043` (normal gate) and `:104` (`SkyHemisphereTint` denominator); **not** added to `color` | No direct PS energy; ambient enters through the VS c4 upload (`buildingPBRVS:133`). |
| 9 | kD / kS | building already had `kD = EnergyConservation(F0)` | unchanged | `:1104` (`kD`), `:1098` (`kS`) | No delta. |

**Key shader-level verdict.** `main_building()` is **not a net light-energy
adder** versus c270294 for an ordinary non-env wall: it removes
`iblSample*0.15` and adds only a ±6 % tint plus a grazing sky-reflect lerp. The
new additive energy is confined to (a) env-mapped materials (`bldEnv.y` up to
0.20) and (b) grazing non-env walls (`bldSkyReflect=0.15`). The green tint is the
`SkyHemisphereTint` ratio, not its magnitude.

## 2. C++ constant upload changes (old vs current vs INI default)

Values are the **uploaded** values, not the config fields, unless the row says
"config". "Old" = `c270294`; "current" = working tree; "INI" = live
`E:\games\gtasa_skygfx_plus\skygfx.ini`; "default" = compiled fallback in
`main.cpp` when the key is absent.

| Constant / key | c270294 | Current code | Live INI | Compiled default | Washout relevance |
|---|---|---|---|---|---|
| `buildingAmbient` (VS c4, PS c24) | `GetTimecycleAmbient()` returned `s_tcAmbient * s_lightsMult * 0.85`; `s_tcAmbient` was **never updated** (`UpdateTimecycleLighting()` has no call site), so the building ambient was effectively **0.0**. Old upload at `buildingPipe.cpp:70`; old body `cpp diff L1375-1378`. | `GetTimecycleAmbientPBR()` reads live `CTimeCycle_GetAmbient*() * CCoronasLightsMult * ambientMult`, then optional `pbrAmbientFloor`, then `ClampAmbientToSky` (`buildingPipe.cpp:183`; `pipelinecommon.cpp:171-234`). Live 20:00 receipt `final=(0.073,0.050,0.050)` from `raw=(0.073,0.050,0.050)`. | `ambientMult=1.0`, `pbrAmbientFloor=0.05`, `ambientFloor=0.03` | `ambientMult=1.0`, `pbrAmbientFloor=0.0`, `ambientFloor=0.03` | **Adds ambient where old was 0.** In the building VS it is scaled by `kAmbientScale=0.10`, so +0.04 at ambient 0.4 (prompt) or +0.007 at live 0.07. `pbrAmbientFloor` is a no-op above 0.05 and only lifts night/dawn (live 20:00 floor added 0.000-0.007). |
| `c20` surfProps (building, **all pipes**) | Raw `RwSurfaceProperties` upload: memory order **{ambient, specular, diffuse}**, 3 floats; GPU read a fourth component past the struct. For SA `diffuse=0` assets the prelight scale (`surfProps.z` in every building VS) was **0**. Old uploads at `buildingPipe.cpp:367/504/608/744` (PS2/GTAIV/Xbox/PBR); `cpp diff L927`. | The new helper `buildingPipe_uploadSurfProps()` packs `{ambient, diffuse, diffuse, 0}` and **if `diffuse < 1e-4`, `v[2]=1.0`** (`buildingPipe.cpp:133-139`). It is called by the PS2/GTAIV/Xbox/sphere cbs (`:1389`, `:1974`, `:2160`); the PBR cb has the same pack inline (`:2820-2823`). | no INI key | no key | **Largest single daylight add for baked assets, and it is not PBR-only:** prelight scale 0 → 1.0 (+0.60 at prelight 0.6) on **every** building pipe, including the live INI's Xbox pipe. For real `diffuse>0` materials it is a no-op versus old z=diffuse. |
| `c24` ambientColor (building PS) | `{buildingAmbient.rgb, 0}` = **{0,0,0}** because ambient was dead. Old `buildingPipe.cpp:711-712`. | `{buildingAmbient.rgb, 0}` then `.w=1` when s4 normal buffer is bound (`buildingPipe.cpp:2440-2446`, `2533-2534`). Live 20:00 `~(0.073,0.050,0.050)`. | `normalBufferEnable=1` | `normalBufferEnable=1` | **No direct add in `main_building`:** c24 feeds only `SkyHemisphereTint`'s denominator (`:104`) and the normal gate (`:1043`). The ambient energy enters through VS c4, not here. |
| `c47` bldEnv (building PS) | register unused / undefined | `{enable=bldEnvReflect(1), envWeight=min(1.5*shininess, 0.20), skyRefl=bldSkyReflect(0.15) for non-env + outdoors + iblBound, 0}` (`buildingPipe.cpp:2905-2909`). Per-mesh env map bound on s1; non-env binds diffuse as a defined sample. | `bldEnvReflect=1`, `bldSkyReflect=0.15` | same | **New energy path:** env-mapped `+envSample*F*0.20` (up to +0.20·F·envSample at grazing); non-env mix weight up to `0.15*F` (brightness delta `0.15*(sky−body)` ≈ ±0.03, up to +0.05 for bright sky). Face-on negligible. |
| `c48` (building NM lane) | register unused | `{bound, normalMapIntensity, glossFromAlpha, shaderAlpha}` (`buildingPipe.cpp:2737-2741`) | `normalMapEnable=0`, `normalMapIntensity=1.0` | `normalMapEnable=0` | **Indirect only:** perturbs N, which can raise `NdotL`/`F_atNdotV`/`envW`. Not a light constant; do not remove (new feature). |
| `pipeUploadPBR` c22/c23 (building) | `pipeUploadPBR(brdf->glossiness, brdf->specular, brdf->clearcoat, brdf->subsurface, specTintRGB)` — old `cpp diff L755-756`; old function `pipelinecommon.cpp:660-666`. | Same call and same layout (`buildingPipe.cpp:2914-2915`; `pipelinecommon.cpp:899-933`). | n/a (BRDF data) | n/a | **No delta.** `c22` still `{glossiness, specular, clearcoat, subsurface}`, `c23` still `{specTintR,G,B,0}`. |
| `pipeUploadPBR` c22/c23 (vehicle) | `(glossiness, specular, specularTintR, 1.0f, wheel, noise, edge)` — `cpp diff L1737-1738`. `c22.w=1.0` was a **dead `envFresnel`** in the old PS. | `(glossiness, specular, specularTintR*(1-metallicness), metallicness, wheel, noise, edge)` (`vehiclePipe.cpp:3333-3334`). Paint metallicness=0 → c22.z identical; c22.w becomes 0. | `vehAutoChrome=1`, `chromeMetallic=1.0` | same | **Paint delta 0; chrome adds reflection only when metallicness=1.** Not a broad daylight washout term. |
| `c1` fxParams (vehicle PS) | CPU struct `{fxSwitch, shininess, specularity, lightmult}`; old PS `carcolsShine = saturate(fxParams.y)` (shininess) — old `vehiclePipe.cpp:573-577`, old shader `:166`. | **Same CPU struct/upload** (`vehiclePipe.cpp:3217-3229`, `3269-3270`), but the new PS reads `envPow = fxParams.y` and `carcolsShine = min(fxParams.w,4.0)` (`VehiclePBR_Modern.hlsl:686`, `:705`). Live `shininess≈1.0`, `lightmult=m_EnvMapLightingMult≈1.0` → **envPow≈1.0** (broad glint, not 20) and **carcolsShine≈1.0** (max coverage). | `envShininessMult=1.0`, `envPower=128` (INI key not read by this path) | `envShininessMult=1.0` | **Mismatch, likely a real PS add:** broad `+50 %` glint over the whole sun-facing reflection and `kr = F*(1+2*1.0)+0.05`, i.e. face-on env coverage ~0.17 instead of the intended ~0.05-0.17. Check the c1 pack before tuning anything else. |
| `c3` iblParams (vehicle) | `{specular, glossiness, 0, 0}` — old `vehiclePipe.cpp:1762`. `specular` was the BRDF/phong spec value; old PS declared `iblParams` but did not read it in `main()` (dead). | `{vehEnvIntensity*2.5, glossiness, tanX, tanY}`; live `vehEnvIntensity=0.4` → **1.0** (`vehiclePipe.cpp:3366-3380`). `chromeEnvBoost` folded in for chrome. | `vehEnvIntensity=0.4`, `vehEnvStrengthMode=1`, `chromeEnvBoost=1.0` | read default `1.0` (`main.cpp:6250`) then load-time clamp `[0..0.4]` (`main.cpp:411-414`) → effective **0.4** → shader **1.0** | **Can add env coverage vs old** because old c3 was dead. In live strength mode the knob is coverage; value 1.0 means full coverage. Secondary. |
| `pbrAmbientFloor` | absent (no `GetTimecycleAmbientPBR`) | Additive grey lift toward `floorLum` when `luma < floorLum` (`pipelinecommon.cpp:205-216`). | **0.05** | **0.0** (`main.cpp:6587`) | **Not a daylight term** at ambient 0.4; live 20:00 it added ~0.000-0.008. It can lift night/dawn blacks and desaturate shadows (grey/pastel), which matches the "night grey" not the noon washout. |
| `ambientMult` | Hardcoded `*0.85` (`cpp diff L1375-1378`). | `* config->ambientMult` (`pipelinecommon.cpp:183-191`). | **1.0** | **1.0** (`main.cpp:6597`) | **+17.6 % ambient** versus old. In the PBR building VS this is only `0.4*0.176*0.10 = +0.007`; in ambient-only Xbox/vehicle it is a real +17.6 %. |
| `pbrIblAmbientWeight` | absent; old IBL was flat `iblSample*0.15` | `c20.x`; `SkyHemisphereTint` blends `weight*0.15` of the chromatic ratio (`VehiclePBR_Modern.hlsl:92-107`). Building c20 `buildingPipe.cpp:2445`; vehicle c20 `vehiclePipe.cpp:3455`. | **0.8** | **0.5** (`main.cpp:6600`) | **[HUE]** live blend 0.12; ratio clamp 0.6..1.5. Luminance ±6 %, but this is the green/cyan source. No energy at weight 0. |
| `bldEnvReflect` | absent (0) | c47.x enable; binds s1 env map and runs the env block (`buildingPipe.cpp:2905-2909`). | **1** | **1** (`main.cpp:6219`) | Enables the new env/sky reflection; current deletes the old additive IBL, so net energy depends on material. |
| `bldSkyReflect` | absent (0) | c47.z; non-env sky reflection strength (`buildingPipe.cpp:2905-2909`). | **0.15** | **0.15** (`main.cpp:6222`) | New grazing sky mix (up to 0.15 mix; brightness delta ≈ ±0.03, up to +0.05 for bright sky). |
| `vehPrelightFallback` | absent (0) | VS c28.x; `max(OUT.Color.rgb, 0.15)` when `surfPrelight <= 0.5` (`vehiclePipe.cpp:2646-2647`; `vehiclePipeVS.hlsl:107-108`). | **0.15** | **0.15** (`main.cpp:6605`) | **Vehicle-only broad shade lift:** every non-prelit car surface gets at least 0.15 before `matCol`; white cars +0.15, dark paint less. |
| `vehEnvFallback` / `vehEnvMask` / `vehWetEnv` | absent | c20.y/.w/.z (`vehiclePipe.cpp:3455-3458`); env validity, mask, wet roads. | 1 / 1 / 1 | 1 / 1 / 1 | **Feature inputs, not light multipliers.** `vehEnvFallback=1` restores reflection when the env texture is empty (adds up to the env layer); `vehWetEnv` darkens base and widens env. |

**C++ verdict.** The only C++ upload that adds a **large, uniform daylight**
term is the `c20` prelight fallback (`+0.60` for `diffuse=0` assets). The rest
are either small (ambient through `kAmbientScale=0.10`), night-only
(`pbrAmbientFloor`), hue-only (`pbrIblAmbientWeight`), or view-dependent
(`c47`, `vehPrelightFallback` on cars).

## 3. Cumulative daylight energy for a lit building (prelight 0.6 / ambient 0.4)

Assumptions: `matCol=1`, `surfAmb=surfDiff=1`, `prelight=0.6`, ambient `A=0.4`,
direct-light sum `D=0.08` (`kDirectScale=0.15` × the VS comment's daylight
estimate), sky luminance `L_sky=0.5`, `F_atNdotV≈0.04` face-on / `≈0.8` at
grazing, `bldSkyReflect=0.15`, `SkyHemisphereTint` neutral (`ratio=1`) unless
noted. `S` = the shared GGX/clearcoat specular (identical in both builds for
non-env materials; ignored except where a multiplier changed).

### 3.1 Case A — ordinary material with real `diffuse` (fallback not triggered)

| Stage | c270294 | HEAD |
|---|---|---|
| VS prelight | `0.6 * 1 = 0.600` | `0.6 * 1 = 0.600` |
| VS ambient | `A_old = 0.0` (dead static) → `0.000` | `0.4 * 1 * 0.10 = 0.040` |
| VS 7-light sum | absent → `0.000` | `D = 0.080` (already the `kDirectScale`-scaled sum) |
| **VS base** | **0.600** | **0.720** |
| PS additive IBL | `L_sky * 0.15 = 0.075` | **removed** → `0.000` |
| PS `SkyHemisphereTint` | absent → ×1.000 | `× (0.952…1.060)` → `−0.035…+0.043` |
| PS `c47` sky reflect (non-env) | absent → `0` | face-on `0.15*0.04≈0.006` → `+0.000…0.001`; grazing `0.15*0.8=0.12` mix → `0.12*(sky−body)` ≈ `−0.03…+0.05` |
| **PS total** | **+0.075 + S** | **−0.065…+0.093 + S** |
| **Grand total** | **0.675 + S** | **0.655…0.813 + S** |
| **Delta vs c270294** | — | **−0.020…+0.138 (−3 %…+20 %)**; typical face-on **+0.04 (+6 %)** |

**Reading:** the +0.12 VS add (ambient + 7-light) is almost exactly cancelled by
removing the old +0.075 additive IBL at face-on. The net washout for a normal
diffuse=1 wall is therefore only **+1.5 %…+6 %** at typical face-on views; at
grazing angles the new `bldSkyReflect` can push it to **+20 %** if the sky is
brighter than the wall (or pull it slightly negative if the sky is darker). The
**single largest term** in this case is the **VS 7-light sum (+0.08)**, followed
by the VS ambient (+0.04); the largest **PS** term is the grazing `c47` sky
reflect (mix 0.15, ≈+0.05 brightness), then the tint (+0.043 max).

### 3.2 Case B — SA baked-lighting material (`diffuse=0`, fallback triggered)

| Stage | c270294 | HEAD |
|---|---|---|
| VS prelight | `0.6 * 0 = 0.000` (old raw z=diffuse=0) | `0.6 * 1.0 (fallback) = 0.600` |
| VS ambient | `0.000` | `0.040` |
| VS 7-light sum | `0.000` (y=0 for baked assets) | `0.000` |
| **VS base** | **0.000** | **0.640** |
| PS additive IBL | `+0.075` | `0.000` |
| PS tint | `×1.000` | `−0.032…+0.038` |
| PS `c47` sky reflect | `0` | `−0.03…+0.05` (0.15 mix × sky−body) |
| **Grand total** | **0.075 + S** (old PBR path; if c270294 used Xbox, this path did not exist) | **0.578…0.728 + S** |
| **Delta** | — | **+0.503…+0.653 (+671 %…+871 %)** |

**Reading:** for a baked-lighting asset the `c20` fallback turns a
prelight-zero / black-or-IBL-only surface into a 0.64 base. **`c20`
`v[2]=1.0` contributes +0.60, i.e. 88-113 % of the positive delta (it is
partly offset by the removed additive IBL).** It is the single term that most
explains a broad, uniform daylight lift on roads, terrain, vegetation and
facades. **This is not PBR-only:** the same helper feeds the PS2/GTAIV/Xbox
cbs, so under the live INI's `buildingPipe=Xbox` it is also the live path. The
`SkyHemisphereTint` (+0.04 max) and `c47` (≈+0.05 max) are second-order;
`pbrAmbientFloor` is 0 above 0.05 and contributes nothing at the prompt
ambient; `ambientMult` contributes only `+0.007` through `kAmbientScale=0.10`.

### 3.3 Cumulative "everything PS/C++" versus old

- **Case A (real diffuse, building):** maximum stacked add is `+0.12 (VS)
  −0.075 (old IBL removed) +0.043 (tint) +0.05 (sky reflect at grazing)
  = +0.138` at grazing; typical face-on **+0.04**. Vehicle-only spec/rim/
  skyFill adds are separate and material-dependent (§1.1).
- **Case B (baked):** maximum stacked add is `+0.60 (c20) +0.04 (tint) +0.05
  (sky reflect) −0.075 (old IBL removed) ≈ +0.615` typical, up to `+0.653` if
  the grazing sky is brighter than the wall.
- **Largest single term overall: c20 prelight fallback (+0.60).**
  **Largest pure-PS term: `c47` sky reflection at grazing (0.15 mix, ≈+0.05
  brightness), or `SkyHemisphereTint` for hue (not magnitude).**
- **Tonemap is a negative term, not a positive one:** current daylight exposure
  is ~0.5-0.9 versus the old ~1.4-1.5 (see §4), so the tone curve currently
  hides ~0.4-0.6 of linear energy. It cannot be the washout source at this
  build; reverting it to the old curve would make the washout worse.

## 4. Tonemap chain: TonemapPass / colour-filter exposure / live path

### 4.1 What actually runs with `colorFilterEnable=0`

`postfx.cpp` resolves the final consumer **before** the bypass:

```cpp
int colorFilter = config->colorFilter;                 // postfx.cpp:1709
if(config->pipeline == PIPELINE_PBR)
    colorFilter = COLORFILTER_MODERN;                  // :1711-1712
...
if(!config->colorFilterEnable){                        // :1906
    ...
    int cfGate = colorFilter;
    if(config->pipeline == PIPELINE_PBR)
        cfGate = COLORFILTER_MODERN;                   // :1931-1933
    if(cfGate == COLORFILTER_MODERN && tonemapPassPS){
        s_tonemapIdentityGrade = true;
        ColourFilter_Modern(rgb1, rgb2);               // pass-1 tint skipped, tonemap runs
        s_tonemapIdentityGrade = false;                // :1934-1937
    }
```

- If the final `colorFilter` is **Modern** (PBR force or a mode/profile), the
  bypass still calls `ColourFilter_Modern` with `s_tonemapIdentityGrade=true`.
  `ColourFilter_Modern` still executes `TonemapPass`
  (`postfx_colourfilter.cpp:302-303`, `646-667`); only the pass-1 timecycle
  tint and the post-grade/black-lift/curve stages are neutralised
  (`:499-515`). **Tonemap is applied.**
- If `colorFilter` is **PC** and `pipeline != PBR`, `cfGate == PC`, the bypass
  does **not** call `ColourFilter_Modern`, so **no tonemap and no sRGB encode**
  runs on that path. That is the literal reading of the INI
  (`colorFilter=PC`, `colorFilterEnable=0`), but it is **not** what the captured
  session did: `skygfx_dbg.log` shows
  `[PostFX] CF-SRC: effective pipeline=2 ... vp=10 colorFilter=8` and
  `[PF] TonemapPass ... identGrade=0 autoExp=0`, i.e. full Modern filter,
  full (not identity) grade, tonemap applied. A mode/debug-menu writer changed
  the effective state. Treat the INI as stale for path selection; verify
  `filter=8` / `identGrade=` in the log for each run.

### 4.2 TonemapPass shader differences

| Feature | c270294 | HEAD | Washout effect |
|---|---|---|---|
| Exposure input | `c *= tonemapParams.x;` — `shader diff L1432` | `c *= exposure;` where `exposure = c5.x` and, if `c5.w >= 0.5`, `exposure *= lerp(1, autoExp, keyStrength)` — `shader diff L1433-1448`; current `TonemapPass.hlsl:207-230` | Auto-exposure is **off** live (`tonemapAutoExposure=0`, c5.w=0). No delta from this branch. |
| Filmic curve | Hable/Uncharted2: A=0.22, B=0.30, CB=0.03, D=toe, E=0.01, F=0.30 — `diff L1453-1454` | Same constants; only input `max(c,0)` added for negative exposure — `diff L1456-1461`; current `:55-65`, `:241` | Curve shape **unchanged**. The added clamp is a no-op for non-negative exposure. |
| Toe strength (C++) | `toeFromLuma = 0.25f - sceneLuma*0.8f; toeStrength = clamp(toeFromLuma + shadowNorm*0.10, 0.05, 0.35)` — old `postfx.cpp:1111-1113`; `cpp diff L3118` | `toeStrengthVal = 0.22f + sceneLuma*1.0f + shadowNorm*0.08f; toeStrength = clamp(0.18,0.55)` — `cpp diff L3132-3133`; current `postfx_colourfilter.cpp:479-480` | **Inverted**: old lowered D in daylight (more shadow lift / grey veil); new raises D in daylight (tighter toe, darker blacks). This is an **anti-washout** change, not a cause. |
| Scene luma normalization | `tcAmbientLuma` and `tcDirLuma` divided by **255** — old `postfx.cpp:1070-1072`; `cpp diff L3085` | No `/255` — `cpp diff L3086-3087`; current `postfx_colourfilter.cpp:332-334` | Old sceneLuma ≈ 0.001-0.01 at day → `sceneExposure` capped at 1.5 → exposure ≈ 1.4-1.5. New sceneLuma ≈ 0.05-0.3 → `sceneExposure ≈ 0.5-0.9` → exposure ≈ 0.5-0.9. **Current is ~2× darker.** |
| Exposure assembly | `exposure = baseExposure * sceneExposure * carcolsAdapt * sunDampen * garageDampen` — old `postfx.cpp:1105` | `exposure = baseExposure * carcolsAdapt * sunDampen * garageDampen; if(!autoExposureOn) exposure *= sceneExposure;` — `cpp diff L3106-3112`; current `postfx_colourfilter.cpp:381-383` | With auto off, mathematically identical factors; the difference is the **sceneLuma normalization** above. |
| Black lift | absent | `c = c*(1-blackLift)+blackLift` — `shader diff L1469-1471`; current `:256` | Live `tonemapBlackLift=0` → exact identity. |
| Pivot curve | absent | `PivotCurve(c)` — `shader diff L1473-1475`; current `:171-199`, `:260` | Live `tonemapCurveLows=0`, `Highs=0` → bit-exact early-out. |
| cf_gain knee | absent | `exposure *= lerp(1,0.5,kneeK)` on measured lum, gated by `c8.w` — `shader diff L1433-1448`; current `:216-225` | Only inside `if(c5.w>=0.5)` (auto-exposure ON). Live off; `c8.w=0` also forces kneeK=0. |
| c5.w semantics | flags: bit0 interior, bit1 cutscene — `shader diff L1345-1349`; old `postfx.cpp:1139` | auto-exposure flag — `shader diff L1350-1354`; current `:209` | Live c5.w=0 either way; old flags were ignored by the old shader. |

### 4.3 Verdict

`ModernColorFilterPS.hlsl` (pass-1, when full Modern runs) is itself a
**darkener**: `lerp(color, color*RGB1, 0.7)`, `+= RGB2*0.05`, `pow(1.2)`,
contrast `1.1` around 0.5 (`ModernColorFilterPS.hlsl:13-23`). It cannot create a
broad white washout; if anything it reduces it.

At the captured settings (`autoExp=0`, `blackLift=0`, curves 0, `identGrade=0`)
the tonemap is the **old curve with a higher (darker) toe and a ~2× lower
exposure**. It is a net **darkener/contrast** stage relative to c270294, not a
washout source. If `colorFilterEnable=0` is honoured with a non-Modern filter,
the tonemap is skipped entirely and the PBR linear HDR output is **not**
sRGB-encoded — that would be a different bug (dark/linear), not a washout.
Keep `TonemapPass`; do not revert it as part of the washout fix.

**Latent risk:** `ModernColorFilterPS.hlsl` pass-1 is the timecycle tint; the
bypass identity grade skips it. If a future run truly bypasses with
`colorFilter=PC`, PBR frames lose both the tint and the sRGB encode. The
current code's `cfGate` check is the guard; the log must show `filter=8` or
`identGrade=1` for the PBR path to be gamma-correct.

## 5. Ranked top 5 PS/C++ washout contributors (with fixes)

Ranking is by **daylight linear energy contribution**, then by hue impact. Every
row states the c270294 value, the current value, the exact site, and whether the
fix is a **revert to Sept 3** or **keep the feature but reduce**.

| Rank | Contributor | c270294 old | Current | Proposed fix | Class |
|---:|---|---|---|---|---|
| **1** | **`c20` prelight fallback** — `src/render/buildingPipe.cpp:133-139` (helper; PS2/GTAIV/Xbox/sphere at `:1389`, `:1974`, `:2160`) and `:2820-2823` (PBR inline) | Raw `RwSurfaceProperties` upload; `.z = diffuse`; `diffuse=0` on SA baked assets → prelight term **0.0** (old `buildingPipe.cpp:367/504/608/744`; `cpp diff L927`) | `{ambient, diffuse, diffuse, 0}`; **`v[2]=1.0` when `diffuse<1e-4`** → prelight term **0.6** at prelight 0.6. Delta **+0.60** (88-113 % of the positive delta; partly offset by the removed old IBL). **Applies to every building pipe, including the live Xbox pipe.** | Keep the 4-float packing and the anti-black fallback, but reduce the boost: `v[2] = 0.30f-0.35f` for PBR/Xbox, or gate the fallback to the PS2/Xbox cbs (where the prelight-only base is expected). Do **not** delete the packing (it fixes the 4-byte overread). | **Keep but reduce** (the fallback is needed to avoid black silhouettes). |
| **2** | **Vehicle skyFill** — `shaders/ps/VehiclePBR_Modern.hlsl:577-579` | absent (no skyFill in c270294 `main()`) | `baseColor *= 1.0 + skyFillLuma * 0.22 * LF(4) * saturate(N.y*0.5+0.5)`; up to **+22 %** base, typical **+5-11 %**. | Reduce `0.22` → `0.07-0.10`; optionally multiply by `saturate(1.0 - sunContrib)` so it fills shade only, not sunlit paint. Keep `LF(4)` gate. | **Keep but reduce** (needed as daylight fill because live ambient is near-zero). |
| **3** | **Vehicle `c1` fxParams contract mismatch** — `src/render/vehiclePipe.cpp:3217-3229`, `3269-3270`; shader `shaders/ps/VehiclePBR_Modern.hlsl:686`, `:705` | CPU packed `{fxSwitch, shininess, specularity, lightmult}`; old shader read `.y` as shininess for `carcolsShine` (old shader `:166`). | Same CPU pack, but new shader reads `.y` as `envPower` and `.w` as `carcolsShine`. Live `fxParams.y≈1.0` (also clamped from `shininess*8`), `fxParams.w≈1.0` (`m_EnvMapLightingMult`) → **glint exponent 1 instead of ~20** (broad +50 %) and **carcolsShine maxed** (`kr=F*3+0.05`). | **Fix the pack or the shader contract.** Preferred: `c1 = {fresnel, envPower, specTint, shininess}` (upload `config->envPower` or a shader constant 20 in `.y`, real shininess in `.w`). If not changing C++, make the shader use `.y` for `carcolsShine` and a constant `envPow=20`. | **Bug fix** (not a Sept-3 look revert): the old `c1.y` semantic was shininess; the new PS comment expects envPower. |
| **4** | **Vehicle specular/rim multipliers** — `shaders/ps/VehiclePBR_Modern.hlsl:799` (spec), `:797` (rim) | `specTotal` unscaled; `rimLight = sunContrib * rimFresnel * 0.10` (`shader diff L1922`) | `specTotal *= 1.5 * LF(2)`; `rimLight ... * 0.18 * LF(3)`; **+50 % spec, +80 % rim** on sun-facing/grazing paint. | Revert to `1.0` / `0.10` for the exact Sept-3 look, or compromise `1.15` / `0.12`. Keep the `LF(2)`/`LF(3)` gates. | **Keep but reduce** (or exact revert of the two constants). |
| **5** | **Building env/sky reflection** — `src/render/buildingPipe.cpp:2905-2909` (c47), `shaders/ps/VehiclePBR_Modern.hlsl:1214-1258` (consumer) | absent; old `main_building` had only additive `iblSample*0.15` (`shader diff L2077-2081`) | `bldEnvReflect=1`, `bldSkyReflect=0.15`, `envWeight=min(1.5*shininess, 0.20)`. Adds up to **+0.20·F·envSample** for env-mapped materials and a 0.15 mix for non-env grazing walls (≈±0.03, up to +0.05 brightness). | For the Sept-3 target set `bldSkyReflect=0.0` (INI) and keep `bldEnvReflect=1` only for genuinely env-mapped materials; if keeping the sky term, reduce to `0.05` and cap `envWeight` at `0.10`. | **Keep but reduce** (new IV/PS2 parity + dynamic-sky reflection feature). Exact target: `bldSkyReflect=0`. |
| **6 (hue special)** | **`SkyHemisphereTint` weight** — `shaders/ps/VehiclePBR_Modern.hlsl:92-107`, `:671` (vehicle env), `:1162-1166` (building body); uploads `src/render/buildingPipe.cpp:2445`, `src/render/vehiclePipe.cpp:3455` | absent; old IBL was flat additive `iblSample*0.15` (`shader diff L2077-2081`) | `pbrIblAmbientWeight=0.8` → blend `0.8*0.15=0.12`; ratio clamp `0.6..1.5`; multiplier range **0.952..1.060**, green/cyan hue skew on every sky-facing surface. | INI `pbrIblAmbientWeight=0.3` (or `0.0` for the exact Sept-3 hue); if keeping the dynamic-sky tie, tighten the shader clamp to `0.85..1.15` and/or blend `0.08`. `pbrIblAmbientWeight` is mode-owned ("Modern") — change it in the debug menu or the active mode profile, not just the INI. | **Keep but reduce** (dynamic-sky/IBL hue tie is a real feature). |
| 7 (hon.) | **`vehPrelightFallback`** — `src/render/vehiclePipe.cpp:2646-2647`, `shaders/vehiclePipeVS.hlsl:107-108` | absent; no floor | `0.15`; every non-prelit car surface maxes to 0.15 before `matCol`. | INI `vehPrelightFallback=0.0` for exact Sept 3; or `0.05` if black silhouettes return. | **Revert (INI) / keep at low value**; feature fixes black non-prelit cars. |
| 8 (hon.) | **Ambient multiplier / PBR floor** — `src/render/pipelinecommon.cpp:183-191`, `:205-216` | Hardcoded `*0.85`; no PBR floor | `ambientMult=1.0` (+17.6 % ambient); `pbrAmbientFloor=0.05` (adds only when ambient luma <0.05; no daylight effect at 0.4). | INI `ambientMult=0.85`, `pbrAmbientFloor=0.0` for the Sept-3 constants. Keep the code (floor is a night/interior guard). | **Revert to Sept 3 (config only)**. Low daylight impact in PBR because `kAmbientScale=0.10`; still affects Xbox/ambient-only pipes. |
| 9 (hon.) | **`pipeUploadPBR` c22/c23** — `src/render/pipelinecommon.cpp:899-933`; building call `buildingPipe.cpp:2914-2915` | same building call/layout (`cpp diff L755-756`) | same building call/layout; vehicle c22.w becomes metallicness (`vehiclePipe.cpp:3333-3334`) | **No change for paint.** Keep the unified upload. | **No-op / keep**. |
| 10 (hon.) | **Tonemap** — `TonemapPass.hlsl:201-262`; `postfx_colourfilter.cpp:332-383`, `499-515` | exposure ~1.4-1.5, toe 0.25-0.8L | exposure ~0.5-0.9, toe 0.22+1.0L; `autoExp=0`, `blackLift=0`, curves 0 | **Keep.** New curve is darker; reverting would worsen washout. | **Do not revert.** |

**One-line answer:** the top **uniform daylight** add is the `c20` prelight
fallback (+0.60 for baked assets); the top **pure-PS** adds are the `c1`
fxParams mismatch (broad env glint/coverage), vehicle `skyFill` (+22 %) and
`specTotal*1.5`, the building `bldSkyReflect` grazing term (0.15 mix, ≈+0.05
brightness), and the `SkyHemisphereTint` hue. `ambientMult`/`pbrAmbientFloor` are real but small
in the PBR building path, and the tonemap is currently a net darkener.

## 6. Terms to keep (needed by newer features)

Do **not** blindly revert these to the c270294 expression. Each is a feature
input that would break a newer contract or reintroduce a known bug.

| Term | Why it must stay | Safe way to reduce / disable for a Sept-3 look |
|---|---|---|
| **`c20` packed 4-float surfProps + `v[2]` fallback** (`buildingPipe.cpp:133-139`, `:2820-2823`) | The raw `RwSurfaceProperties` upload read 4 bytes past a 3-float struct (`.w` overread) and left baked-lighting assets with a zero prelight. The packing is the fix; it is shared by PS2/GTAIV/Xbox/PBR. | Keep the packing; lower `v[2]` (0.30-0.35) or gate the fallback to PS2/Xbox. |
| **`c47` `bldEnv` block + `bldEnvReflect` / `bldSkyReflect`** (`buildingPipe.cpp:2905-2909`; shader `:1194-1258`) | IV/PS2/Xbox all reflect env-mapped facades; the PBR pipe had no branch and rendered them flat. It is also the only sky-reflection lane. | Set `bldSkyReflect=0` for the exact target; keep `bldEnvReflect=1` for env-mapped materials. Reduce `envWeight` cap 0.20 → 0.10. |
| **`SkyHemisphereTint` + `pbrIblAmbientWeight`** (`VehiclePBR_Modern.hlsl:92-107`, `:671`, `:1162-1166`; uploads `buildingPipe.cpp:2445`, `vehiclePipe.cpp:3455`) | It is the only tie between the dynamic-sky capture (s3) and the timecycle ambient; both vehicle and building PBR paths share the same ambient/IBL timeline. It also has the `iblGate` dark-capture fade. | Set weight 0.3/0.0 (INI or mode profile), or tighten the ratio clamp to 0.85-1.15. Do not remove the function; the vehicle env tint and the building tint both call it. |
| **`vehEnvFallback` / `vehEnvMask` / `vehWetEnv`** (`vehiclePipe.cpp:3455-3458`; shader `:641-662`) | Fallback prevents an empty reflectionTex from replacing the body; mask matches PS2/Neo parity; wet roads widen the reflection band and darken the base. | Keep all three; only `vehWetEnv` can be disabled if wet-road reflections are not wanted. |
| **`vehPrelightFallback`** (`vehiclePipe.cpp:2646-2647`; `vehiclePipeVS.hlsl:107-108`) | SA vehicle geometry is usually not prelit; without the floor the shade side is black (the documented black-silhouette bug). | INI `0.0` only if the Sept-3 look is the priority; otherwise `0.05`. Keep the `max()` branch, which is view/energy bounded. |
| **`pbrAmbientFloor` + `ambientFloor` + `ClampAmbientToSky`** (`pipelinecommon.cpp:78-169`, `:196-234`) | Night/interior black-crush guards; the floor is opt-in and additive (scale-up cannot lift a true zero); `ClampAmbientToSky` prevents ambient from out-shining the visible sky. | Set `pbrAmbientFloor=0.0` for the exact target. Keep the code. `ClampAmbientToSky` has a daylight release (skyL≥0.45), so it is not a daylight washout term. |
| **`ambientMult` as a config key** (`pipelinecommon.cpp:183-191`) | Replaces the hardcoded 0.85 and gives a single ambient knob for all pipes. | Set `ambientMult=0.85` in the INI; do not hardcode it again. |
| **`c48` NM lane, Forward+ (`s5-s7`), `LF` bitmask, `VPOS`/`screenSize`** (`buildingPipe.cpp:2737-2741`; shader `:1043-1079`, `:1147-1150`) | New features: normal maps, 32-light forwarding, layer toggles, screen-space normal sampling. Their energy is indirect (normal tilt) and view/local, not the uniform washout. | Keep. If diagnosing GGX blowout, use `normalBufferEnable=0` or `normalMapEnable=0` as a one-run A/B, not a permanent revert. |
| **Tonemap `c7/c8`, auto-exposure, black lift, pivot curve** (`TonemapPass.hlsl:201-262`; `postfx_colourfilter.cpp:295-300`, `499-515`, `559-565`, `605-624`) | All live-off features; the `identGrade` bypass depends on them being present; the negative-exposure guard prevents NaN. | Keep. Do not revert the shader to the 168-line c270294 file. |
| **`c22/c23` metallicness layout** (`pipelinecommon.cpp:891-933`) | Chrome/wheel need c22.w=metallicness and the neutral reflection tint; paint still gets the same c22.z at metallicness 0. | Keep. No washout impact for paint. |
| **`c24` live ambient + `c20` weight** (`buildingPipe.cpp:2440-2446`, `:2445`; `vehiclePipe.cpp:3435-3458`) | These feed `SkyHemisphereTint` and the normal-buffer gate; the actual ambient add is in the VS, not the PS. | Keep. Reduce `pbrIblAmbientWeight` for hue; the ambient magnitude is controlled by `ambientMult`/`pbrAmbientFloor`. |

## Appendix: Evidence Index

| Anchor | Meaning |
|---|---|
| `shader diff L1525-1547` | New `SkyHemisphereTint` (ratio, clamp 0.6-1.5, blend `weight*0.15`). |
| `shader diff L1658-1668` | Vehicle `skyFill` multiplicative lift `*0.22`. |
| `shader diff L1685-1704` | `envContent=1.2`, coverage/content split. |
| `shader diff L1737-1755` | Env-source fallback env→ibl→sky. |
| `shader diff L1757-1777` | Sky blend 0.3, `iblBlend *= SkyHemisphereTint`. |
| `shader diff L1813-1821` | `envGlint` and `envTerm` `(1+0.5*glint)`. |
| `shader diff L1826-1837` | `kr` from clearcoat/carcols/metallic; `lerp(layer1, envTerm, kr)` (stale). |
| `shader diff L1919-1925` | Rim 0.18, `specTotal *= 1.5`. |
| `shader diff L1932-1941` | Composite `layer1+layer2` → `layer2` (stale; current restored additive). |
| `shader diff L2077-2097` | Old additive IBL → new multiplicative tint in `main_building`. |
| `shader diff L2101-2157` | New `bldEnv` env/sky reflection block. |
| `shader diff L3872-3887` | Old building VS `(prelight*surfDiff + ambient*surfAmb)*matCol` → new ambient + 7-light loop. |
| `cpp diff L78`, `L1375-1380` | Old `buildingAmbient = GetTimecycleAmbient()`; dead static `s_tcAmbient`, hardcoded `*0.85`. |
| `cpp diff L927-949` | Old raw `RwSurfaceProperties` upload → packed `{amb, diffuse, diffuse}` fallback. |
| `cpp diff L959-1024` | `c47` env/sky upload; `envWeight=1.5*shininess` capped 0.20. |
| `cpp diff L1385-1417` | `GetTimecycleAmbientPBR` / `pbrAmbientFloor` additive floor. |
| `cpp diff L3085-3087` | Removed `/255` from `tcAmbientLuma`/`tcDirLuma` (2× exposure change). |
| `cpp diff L3106-3112` | Exposure assembly with optional auto-exposure. |
| `cpp diff L3118-3133` | Toe formula inversion. |
| `cpp diff L3141-3184` | `blackLift`, `c5.w` auto flag, `c7`. |
| `cpp diff L8771-8778` | `vehPrelightFallback` VS c28 upload. |
| `src/render/vehiclePipe.cpp:3217-3229`, `3269-3270` | CPU `fxParams` c1 upload `{fxSwitch, shininess, specularity, lightmult}`; does not match the new shader's `.y=envPower`, `.w=shininess` contract. |
| `src/render/buildingPipe.cpp:133-139` | `buildingPipe_uploadSurfProps()`: packed `{ambient,diffuse,diffuse}`, `v[2]=1` when `diffuse<1e-4`; used by PS2/GTAIV/Xbox/sphere. |
| `src/render/buildingPipe.cpp:1389`, `:1974`, `:2160` | PS2 / Xbox / sphere calls to the surf fallback. |
| `src/render/buildingPipe.cpp:183` | Current `buildingAmbient = GetTimecycleAmbientPBR()`. |
| `src/render/buildingPipe.cpp:2440-2446` | c24 ambient + c20 `pbrIblAmbientWeight`. |
| `src/render/buildingPipe.cpp:2820-2823` | c20 surf pack + prelight fallback. |
| `src/render/buildingPipe.cpp:2905-2909` | c47 `bldEnv` upload. |
| `src/render/buildingPipe.cpp:2914-2915` | `pipeUploadPBR` building call (unchanged). |
| `src/render/pipelinecommon.cpp:183-191` | `ambientMult` replaces hardcoded 0.85. |
| `src/render/pipelinecommon.cpp:205-216` | `pbrAmbientFloor` additive lift. |
| `src/render/pipelinecommon.cpp:899-933` | `pipeUploadPBR` c22/c23. |
| `src/render/vehiclePipe.cpp:2646-2647` | VS c28 `vehPrelightFallback`. |
| `src/render/vehiclePipe.cpp:3333-3334` | Vehicle c22 metallicness. |
| `src/render/vehiclePipe.cpp:3366-3380` | c3 `vehEnvIntensity*2.5` → 1.0 live. |
| `src/render/vehiclePipe.cpp:3435-3458` | c24 live ambient, c20 weight/fallback/wet/mask. |
| `shaders/ps/VehiclePBR_Modern.hlsl:92-107` | `SkyHemisphereTint`. |
| `shaders/ps/VehiclePBR_Modern.hlsl:577-579` | Vehicle `skyFill`. |
| `shaders/ps/VehiclePBR_Modern.hlsl:605-613` | `envContent`/coverage. |
| `shaders/ps/VehiclePBR_Modern.hlsl:706-707` | `envGlint`/`envTerm`. |
| `shaders/ps/VehiclePBR_Modern.hlsl:722-732` | Current additive env layer (`kr*0.5`). |
| `shaders/ps/VehiclePBR_Modern.hlsl:797-799` | Rim 0.18 / spec ×1.5. |
| `shaders/ps/VehiclePBR_Modern.hlsl:811` | Current additive composite. |
| `shaders/ps/VehiclePBR_Modern.hlsl:1162-1166` | Building tint + `iblGate`. |
| `shaders/ps/VehiclePBR_Modern.hlsl:1214-1258` | Building env/sky reflection. |
| `shaders/ps/TonemapPass.hlsl:201-262` | Current tonemap main. |
| `src/render/postfx.cpp:1709-1712`, `1906-1937` | Colour-filter resolve + bypass/identity-grade path. |
| `src/render/postfx_colourfilter.cpp:332-383` | Current exposure assembly. |
| `src/render/postfx_colourfilter.cpp:499-515` | Identity-grade zeroing. |
| `src/render/postfx_colourfilter.cpp:646-667` | Tonemap draw. |
| `src/core/main.cpp:6219-6224`, `6587`, `6597-6605` | Defaults for `bldEnvReflect`, `bldSkyReflect`, `pbrAmbientFloor`, `ambientMult`, `pbrIblAmbientWeight`, `vehPrelightFallback`. |
| Live INI | `ambientMult=1.0`, `pbrAmbientFloor=0.05`, `ambientFloor=0.03`, `pbrIblAmbientWeight=0.8`, `bldEnvReflect=1`, `bldSkyReflect=0.15`, `vehPrelightFallback=0.15`, `vehEnvIntensity=0.4`, `vehEnvStrengthMode=1`, `vehPBRLayers=255`, `colorFilter=PC`, `colorFilterEnable=0`. |
| Live log | `CF-SRC ... vp=10 colorFilter=8`; `TonemapPass ... identGrade=0 autoExp=0`; `[PBR-Ambient] final=(0.073,0.050,0.050) floor=0.050 raw=(0.073,0.050,0.050)` at 20:00. |

### Bottom line

The PS-side `main_building()` did **not** add a broad daylight term versus
c270294: it removed the old `+0.15·sky` additive IBL and replaced it with a
±6 % multiplicative tint, a grazing sky-reflect lerp (≤0.15 mix) and an
env-mapped additive term (≤0.20·F). The green tint is real and comes from
`SkyHemisphereTint`; the broad brightening is dominated by the **C++ `c20`
prelight fallback** (+0.60 for `diffuse=0` assets, applied to every building
pipe including the live Xbox path). Vehicle `main()` adds
`skyFill` (+22 %), spec ×1.5, rim ×1.8, `vehPrelightFallback=0.15`, and the
`c1` fxParams contract mismatch makes the env glint broad (`envPow≈1`) with
`carcolsShine` pinned high. The tonemap currently runs (Modern, full grade in
the captured session) and is ~2× darker than the c270294 curve, so it is
masking, not causing, the washout. The highest-value low-risk experiment is a
config-only A/B: `pbrIblAmbientWeight=0.0`, `bldSkyReflect=0.0`,
`ambientMult=0.85`, `pbrAmbientFloor=0.0`, `vehPrelightFallback=0.0`, plus a
one-line `surfUpload[2]` reduction to 0.30-0.35 for the baked-asset fallback;
then fix the `c1` pack if vehicle reflections still read too bright.
