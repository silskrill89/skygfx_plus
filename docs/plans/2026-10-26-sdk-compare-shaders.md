# 2026-10-26 — SDK Shader Compare: Legacy Pipeline (junior/original vs expIV)

**Scope:** READ-ONLY shader-content comparison. Reference trees:
`/e/SDKs/skygfx_junior/shaders/` and `/e/SDKs/skygfx_original/shaders/`.
Our live tree: `E:/dev(dave)/skygfx_plus_expIV/shaders/`.
No source or shader was edited; this file is the only write.

**Live-source caveat:** our `tools/fast_build.py` emits the legacy CSOs from
consolidated files, not from the same-named standalone `.hlsl` files.
`shaders/buildingPipePS.hlsl` (5 entries) and
`shaders/noiseDetailBuildingPS.hlsl` (3 entries) own the building PS CSOs;
`shaders/ps/VehiclePBR_Modern.hlsl` owns `specCarFxPS`, `ps2EnvSpecFxPS`,
`mobileVehiclePS`; `shaders/vehiclePipeVS.hlsl` owns the vehicle VS CSOs.
The same-named standalone files are dead sources unless stated otherwise.

## 0. Executive verdict

- **The previously found Xbox building scale bug is fixed in the live source.**
  `main_xboxBuilding` is `tex0(uv)*color + tex1(uv)*envcolor` — identical to
  `skygfx_original`. Junior alone has `*0.5` on both UVs and `*10.0` on the
  diffuse term; those tokens do not exist anywhere in our legacy tree.
- **The four deterministic building PS entries are content-identical to
  upstream.** `main_simple`, `main_simpleDetail`, `main_simpleFog`, and
  `main_xboxBuilding` match `skygfx_original` expression-for-expression.
- **The three noise-detail entries are intentional reimplementations, not
  clones.** They preserve upstream 2_a's `uv*1.2` diffuse scale but replace
  the 3-tap triangle-grid stochastic sampler with one blue-noise-jittered tap
  (`s9`, `c49-c52`). Base colour/alpha math is unchanged.
- **The legacy vehicle FX PS entries are no longer the upstream shaders.**
  `specCarFxPS`, `ps2EnvSpecFxPS`, `mobileVehiclePS` are now compiled from
  `VehiclePBR_Modern.hlsl`; each adds `ClusterLightingScreen` and `saturate()`
  on top of the upstream expression. `envCarPS` is gone; the Env callback uses
  the PBR `main()` shader instead.
- **Two Xbox VS regressions are real content divergences:** (a) the new sun
  term puts `c5.w = 1.0` into the colour-alpha path (`directContrib.a = NdotL`),
  and (b) `xboxBuildingWindVS` no longer remaps `DayColor.a` to
  `NightColor.a`, so the wind weight now leaks into output alpha.
- **Legacy `vehicleVS` has an undocumented ambient floor**
  (`ambient*max(surfAmb, 0.4)`) that upstream does not have. That is the
  largest remaining scale-class divergence in the legacy set.
- **The PS2 building wind VS has a comma-operator bug:** upstream's two-octave
  noise blend is parsed as one octave (the first `lerp` result is discarded).
  The Xbox building wind noise is one octave upstream and remains one octave.
- **Vehicle x-ray is not a legacy-shader decision bug.** The shader leg,
  `main_xboxCar` outputting `matCol.a`, is byte-identical to upstream. Existing
  C++ audit attributes the x-ray to paint-marker alpha 16 + dual pass; the only
  shader-side x-ray risk is the PBR `main()`/`mobileVehicle` alpha override.

## 1. Method and source map

**Reference delta.** `diff -rq` of junior vs original shows five differing
files; exactly one contains a math delta:

| File | Junior vs original | Consequence |
|---|---|---|
| `ps/simplePS.hlsl` | whitespace only | no math delta |
| `ps/xboxBuildingPS.hlsl` | junior adds `texcoord0*0.5`, `texcoord1*0.5`, diffuse `*10.0` | junior is the outlier; original is canonical |
| `vs/xboxBuildingVS.hlsl` | whitespace only | no math delta |
| `vs/envCarVS.hlsl` | whitespace / `WorldPos.xyz` only | no math delta |
| `vs/sphereBuildingVS.hlsl` | `WorldPos` vs `WorldPos.xyz` | same vector truncation |
| `vs/xboxBuildingWindVS.hlsl`, `vs/ps2BuildingWindVS.hlsl` | junior-only files | junior has the wind pair; original does not |

**Runtime CSO producers (authoritative):**

| CSO / IDR | Live source | Entry |
|---|---|---|
| `simplePS.cso` / 101 | `shaders/buildingPipePS.hlsl` | `main_simple` |
| `simpleDetailPS.cso` / 230 | `shaders/buildingPipePS.hlsl` | `main_simpleDetail` |
| `simpleFogPS.cso` / 232 | `shaders/buildingPipePS.hlsl` | `main_simpleFog` |
| `xboxBuildingPS.cso` / 226 | `shaders/buildingPipePS.hlsl` | `main_xboxBuilding` |
| `normMapBuildingPS.cso` / 236 | `shaders/buildingPipePS.hlsl` | `main_normMapBuilding` |
| `simpleNoiseDetailPS.cso` / 278 | `shaders/noiseDetailBuildingPS.hlsl` | `main_simpleNoiseDetail` |
| `simpleDetailNoiseDetailPS.cso` / 231 | `shaders/noiseDetailBuildingPS.hlsl` | `main_simpleDetailNoiseDetail` |
| `xboxBuildingNoiseDetailPS.cso` / 227 | `shaders/noiseDetailBuildingPS.hlsl` | `main_xboxBuildingNoiseDetail` |
| `specCarFxPS.cso` / 215 | `shaders/ps/VehiclePBR_Modern.hlsl` | `main_specCarFx` |
| `ps2EnvSpecFxPS.cso` / 212 | `shaders/ps/VehiclePBR_Modern.hlsl` | `main_ps2EnvSpecFx` |
| `mobileVehiclePS.cso` / 219 | `shaders/ps/VehiclePBR_Modern.hlsl` | `main_mobileVehicle` |
| `xboxBuildingVS.cso` / 225 | `shaders/vs/xboxBuildingVS.hlsl` | `main` |
| `xboxBuildingWindVS.cso` / 228 | `shaders/vs/xboxBuildingWindVS.hlsl` | `main` |
| `ps2BuildingVS.cso` / 222 | `shaders/vs/ps2BuildingVS.hlsl` | `main` |
| `ps2BuildingWindVS.cso` / 224 | `shaders/vs/ps2BuildingWindVS.hlsl` | `main` |
| `ps2BuildingFxVS.cso` / 223 | `shaders/vs/ps2BuildingFxVS.hlsl` | `main` |
| `sphereBuildingVS.cso` / 229 | `shaders/vs/sphereBuildingVS.hlsl` | `main` |
| `vehicleVS.cso` / 102 | `shaders/vs/vehicleVS.hlsl` | `main` |
| `mobileVehicleVS.cso` / 218 | `shaders/vehiclePipeVS.hlsl` | `main_mobileVehicle` |
| `specCarFxVS.cso` / 214 | `shaders/vehiclePipeVS.hlsl` | `main_specCarFx` |
| `ps2CarFxVS.cso` / 213 | `shaders/vehiclePipeVS.hlsl` | `main_ps2CarFx` |
| `xboxCarVS.cso` / 216 | `shaders/vehiclePipeVS.hlsl` | `main_xboxCar` |
| `leedsCarFxVS.cso` / 217 | `shaders/vehiclePipeVS.hlsl` | `main_leedsCarFx` |

`shaders/ps/simplePS.hlsl`, `shaders/ps/simpleDetailPS.hlsl`,
`shaders/ps/simpleFogPS.hlsl`, `shaders/ps/xbox/xboxBuildingPS.hlsl`, and
`shaders/ps/normMapBuildingPS.hlsl` are **dead duplicates** (fast_build skips
them by name). Their contents agree with the consolidated live entries except
that the standalone Xbox file is also already fixed.

`normMapBuildingPS.cso` and `normMapVehiclePS.cso` are embedded but never
created via `makePS()`; both are dead payload. `customBuildingVS.cso` is
embedded (IDR 235) but never loaded; `vehiclePipeVS` is created from
IDR_VEHICLEVS (`vehicleVS.cso`), not from it.

## 2. Task 1 — Per-entry math and register diff

Verdict legend: **identical** = same tokens/math; **harmless** = different
source form, same result (or dead file); **DIVERGENT** = different output.

### 2.1 Building pixel shaders

| Entry (live source) | Upstream expression | Our expression | Registers / samplers (up → ours) | Verdict |
|---|---|---|---|---|
| `main_simple` (`buildingPipePS.hlsl:25`) | `tex2D(tex, uv.xy)*color*colorscale.x` | same | `s0,c0` → same | **identical** |
| `main_simpleDetail` (`:46`) | `final = tex0(uv0)*color*scale.x; final.rgb *= tex2(uv0*detailtile).rgb*2.0` | same | `s0,s1,s2,c0,c1` → same (`tex1` declared unused both) | **identical** |
| `main_simpleFog` (`:68`) | `fog=clamp((fogdist.x-len(WorldPos-campos))/fogdist.y,0,1); col=tex(uv)*color; col.rgb=lerp(fogcol,col.rgb,fog)` | same | `s0,c0,c1,c2` → same | **identical** |
| `main_xboxBuilding` (`:91`) | original: `tex0(uv0)*color + tex1(uv1)*envcolor`; junior: `tex0(uv0*0.5)*color*10 + tex1(uv1*0.5)*envcolor` | `tex0(uv0)*color + tex1(uv1)*envcolor` | `s0,s1` → same | **identical to original; DIVERGENT vs junior only** (junior scale bug absent — confirmed fixed) |
| `main_normMapBuilding` (`:125`) | no upstream | base = `tex0(uv)*color*scale`; normal.xy = `tex1(uv).wy*2-1`; `N = normalize(uv)`; lights via `dot(N,-dir)`; adds `saturate(dot(nmapN, const))*surfDiff*0.3`; debug returns `normal*0.5+0.5` or `lighting` | `s0,s1,c0,c1,c4,c5,c12,c19,c20` | **ours-only**; see §6.1 (dead payload) and §8 (wrong normal basis) |
| `main_xboxBuildingNoiseDetail` (`noiseDetailBuildingPS.hlsl:108`) | 2_a: `tex2DStochastic(tex0, uv*1.2)*color + tex1(uv)*envcolor` | `tex2DNoiseDetail(tex0, uv*1.2)*color + tex1(uv)*envcolor` | 2_a: `s0,s1`; ours: `s0,s1` + `s9,c49-c52` | **DIVERGENT implementation** (blue-noise one tap vs 3-tap stochastic); base math/scale identical |

Fog handling: only `main_simpleFog` has it and it matches upstream exactly
(linear distance fog, RGB only, alpha untouched; `WorldPos` from VS TEXCOORD1).

### 2.2 Vehicle FX pixel shaders

| Entry (live source) | Upstream expression | Our expression | Registers / samplers (up → ours) | Verdict |
|---|---|---|---|---|
| `main_specCarFx` (`VehiclePBR_Modern.hlsl:871`) | `tex2D(tex1, uv1)*envcolor + speccolor` | same two taps, then `ClusterLightingScreen(vpos.xy,...)`, `color.rgb += clusterSpec+clusterDiff`, `return saturate(color)` | up: `s1` only; ours: `s0-s8` + `s5/s6/s7` cluster + `c45`; full PBR constant block | **DIVERGENT** (ours-only modern rewrite; adds cluster light and clamps) |
| `main_ps2EnvSpecFx` (`:844`) | `tex2D(tex1, uv1)*envcolor + tex2D(tex2, uv2)*speccolor` | same two taps, then same cluster block, `return saturate(color)` | up: `s1,s2`; ours: `s0-s8` + cluster `s5-s7`, `c45` | **DIVERGENT** (ours-only modern rewrite; adds cluster light and clamps) |
| `main_mobileVehicle` (`:896`) | `col=tex0(uv)*color; ReflPos=(normalize(t1.xy))*(t1.z*0.5+0.5); ReflPos=ReflPos*(0.5,-0.5)+(0.5,0.5); ReflCol=tex1(ReflPos); col.rgb=lerp(col.rgb,ReflCol.rgb,shininess); col.a+=ReflCol.b*0.125; col.rgb+=spec` | same, with zero-length `normalize` guard, `fxParams.y` as shininess, cluster add, `col.rgb=saturate(col.rgb)`; alpha not clamped | up: `s0,s1,c30`; ours: `s0,s1,c1` + cluster `s5-s7,c45` | **DIVERGENT** (register relocation c30→c1, cluster + RGB clamp; alpha math identical) |
| `envCarPS` | full env/spec shader: `diff*color`, `env*mask`, Fresnel `lerp(diff,env,envColor)`, `final.a=diff.a`, 5-light spec loop | **not shipped** — no source, no CSO, no IDR; Env cb binds `vehiclePBRVS` + PBR `main()` | n/a | **dropped** (see §6) |

`main_specCarFx` / `main_ps2EnvSpecFx` are the most consequential PS rewrites:
upstream is a pure two-tap additive FX expression with no clamp; ours lacks
`tex1`/`tex2` legacy-only semantics because it also owns the PBR sampler set,
adds screen-space cluster lighting, and clamps the result to 1.

### 2.3 PostFX / utility pixel shaders

| Entry | Upstream expression | Our expression | Verdict |
|---|---|---|---|
| `grassPS` | `c*color*255.0/128.0` | same | **identical** (note alpha ×1.992; see §4) |
| `blurPS` | 10-tap `pxSz.xy*(i/9-0.5)*pxSz.z`, `c/=10`, `c.a=1` | same | **identical** |
| `contrastPS` | `c.a=1; c.rgb=c.rgb*contrastMult+contrastAdd` | same | **identical** |
| `gradingPS` | `c.a=1; o.r/g/b=dot(red/green/blueGrade,c); o.a=1` | same | **identical** |
| `radiosityPS` | `uv=uv*xform.zw+xform.xy; saturate(c*2-limit)*intensity*passes; a=1` | same | **identical** |
| `iiiTrailsPS` | 5× `dst*(1-RGB1.a)+prev*RGB1*RGB1.a`; `float4(prev.rgb,1)` | same source, but **not embedded and never created** (`iiiTrailsPS` remains NULL) | **identical source, dropped at runtime** |
| `vcTrailsPS` | `a=30/255; doublec=saturate(RGB1*2); 5× dst*(1-a)+prev*doublec*a; +=prev*RGB1 twice; saturate` | same | **identical** |

### 2.4 Noise-detail (upstream 2_a stochastic) entries

| Entry | Upstream | Ours | Registers | Verdict |
|---|---|---|---|---|
| `main_simpleNoiseDetail` (`noiseDetailBuildingPS.hlsl:65`) | `tex2DStochastic(tex, uv.xy*1.2)*color*colorscale.x` | `tex2DNoiseDetail(tex, uv.xy*1.2)*color*colorscale.x` | up: `s0,c0`; ours: `s0,c0,s9,c49-c52` | **DIVERGENT implementation**, same base math |
| `main_simpleDetailNoiseDetail` (`:86`) | `final=tex2DStochastic(tex0,uv*1.2)*color*scale; final.rgb*=tex2DStochastic(tex2,uv*detailtile).rgb*2` | `jitter=NoiseDetailJitter(uv); final=tex2DNoiseDetailJ(tex0,uv*1.2,jitter)*color*scale; final.rgb*=tex2DNoiseDetailJ(tex2,uv*detailtile,jitter).rgb*2` | same + `s9,c49-c52` | **DIVERGENT implementation**, same base math |
| `main_xboxBuildingNoiseDetail` (`:108`) | `tex2DStochastic(tex0,uv*1.2)*color + tex1(uv)*envcolor` | `tex2DNoiseDetail(tex0,uv*1.2)*color + tex1(uv)*envcolor` | same + `s9,c49-c52` | **DIVERGENT implementation**, same base math |

Both use the same UV scale (`1.2` diffuse; `detailtile` detail) and the same
colour/alpha composition. The difference is sampling: upstream is 3 taps with
triangle-grid barycentrics and `ddx/ddy`; ours is 1 tap at a blue-noise offset
and explicitly uses the raw stage-9 tile. The `s9`/`c52` fallback is safe
(`c52.x==0` → zero jitter), but it means the noise path needs `pipeBindNoise(9)`
before the PS bind. That is wired in `buildingPipe.cpp:995`/`:1025`.

### 2.5 Vertex shaders

| Entry | Upstream | Ours | Verdict |
|---|---|---|---|
| `xboxBuildingVS` | `OUT.Color=(prelight*surfDiff + float4(ambient,0)*surfAmb)*matCol`; `Envcolor=float4(1,1,1,0)*shininess*lightmult`; regs `c0,c4,c19,c20,c29-c32,c36-c38` | same plus `c5` directCol and `c6` directDir: `directContrib=directCol*max(dot(N,-directDir),0)`, added to Color before `*matCol` | **DIVERGENT** (new sun term; alpha side-effect §4/§5) |
| `xboxBuildingWindVS` | same prelight; `intensityMask=1-DayColor.a`; `dayColor.a=NightColor.a`; `dist=(noise(windPos+pos.xy)-0.5)*windIntensity*mask`; x/y += dist, z += dist*0.3; no sun | same noise/offset; **no day-alpha remap**; adds `windExt` height/gust (`c23` always written with `w=1`); adds `c5/c6` sun term | **DIVERGENT** (alpha path + wind displacement + sun) |
| `ps2BuildingVS` | `Color=DayColor*dayparam+NightColor*nightparam; *=matCol/colorScale; rgb+=ambient*surfAmb` | same | **identical** |
| `ps2BuildingWindVS` | 2-octave noise: `lerp(lerp(oct1,oct2,f.y), lerp(oct3,oct4,f.y),0.5)`; no windExt | comma-expression bug: `return lerp(oct1,oct2,f.y), lerp(oct3,oct4,f.y);` — only the **second** octave survives (X3081); adds `c23` windExt | **DIVERGENT** (noise half amplitude/one octave; windExt always enabled) |
| `ps2BuildingFxVS` | `uv=envmat*N.xy-envXform.xy; uv*=-envXform.zw; Envcolor=192/128*shininess*lightmult` | same | **identical** |
| `sphereBuildingVS` | `WorldPos` / world-matrix sphere UV / `length*0.002` / prelight+ambient*matCol | same; `OUT.WorldPos = WorldPos` (float4→float3 truncation) vs original `WorldPos` and junior `.xyz` | **harmless** |
| `vehicleVS` (`shaders/vs/vehicleVS.hlsl`) | `Color=float4(IN.Color.rgb*isPrelit,1)+ambient*surfAmb+7×NdotL*directCol*surfDiff; saturate*matCol` | same but `ambient*max(surfAmb,0.4)` | **DIVERGENT** (ambient floor; §3/§7) |
| `main_mobileVehicle` (`vehiclePipeVS.hlsl:352`) | regs `c30 fxParams, c31 worldmat, c35 campos`; same reflection/spec/light math | regs `c30, c31, c36 campos` (C++ `REG_eye=36` matches); normalize guard; same math | **harmless** register relocation (live file matches C++); dead `vs/mobileVehicleVS.hlsl` still says c35 |
| `main_specCarFx` (`vehiclePipeVS.hlsl:204`) | `eye c35`; same env UV/spec math | `eye c36`; normalize guard; same math | **harmless** relocation (C++ uploads c36) |
| `main_leedsCarFx` (`:319`) | `fxParams c30, envmat c32, texmat c36` | `fxParams c30, envmat c32, texmat c40` (C++ `REG_envtexmat=40` matches) | **harmless** relocation |
| `main_ps2CarFx` (`:156`) | `fxParams c30, envXform c31, envmat c32, specmat c35, lightdir c38` | same | **identical** |
| `main_xboxCar` (`:262`) | `World c0/View c4/Proj c8/WorldIT c12/Texture c16/matCol c20/sunDir c23/sunDiff c24/sunAmb c25/surfProps c26/envSwitch c39/eye c40`; `Color.a=matCol.a`; same 6 envSwitch branches | same | **identical** |
| `neoVehiclePass1/2VS` | same as junior | same, but fast_build no longer emits/loads them (`CarPipe::vertexShaderPass1/2 = NULL`) | **identical source, dead** |
| `mobileBuildingVS` | same as junior | same, but no IDR and no loader | **identical source, dropped** |
| `envCarVS` | env/spec VS with 7-light loop, Fresnel envcolor | not shipped | **dropped** |
| `postfxVS` | passthrough | same source, but no IDR/loader in our tree | **identical source, dropped** |

## 3. Task 2 — Scale-bug sweep (`*0.5` / `*10.0` / `*1.2` class)

**Result: no live legacy entry still carries the junior Xbox `*0.5`/`*10.0`
bug.** The only legacy `*1.2` is the upstream-2_a diffuse scale in the three
noise-detail entries, so it is intentional (unrelated modern effects also use
1.2, outside this scope). The remaining scale-class
divergences are listed below; the only one that changes a shipped non-noise
legacy path is the `vehicleVS` ambient floor.

| Site | Upstream | Ours | Verdict |
|---|---|---|---|
| Xbox building diffuse/env UV | junior `uv*0.5`; original `uv` | `uv` (no 0.5) | **fixed** — matches original |
| Xbox building diffuse gain | junior `*10.0`; original `*1.0` | `*1.0` | **fixed** |
| Noise-detail diffuse UV | 2_a `uv*1.2` (stochastic) | `uv*1.2` (blue noise) | **intentional match**; non-noise simplePS has no 1.2, as upstream |
| Detail-map modulation | `*2.0` | `*2.0` | identical |
| Grass gain | `255.0/128.0` (≈1.992) | same | identical |
| PS2 `colorScale` | `255/128` when ps2ModulateBuilding | same (C++) | identical |
| Env spec strength | `192/128`, `96/128` | same (PS2/spec VS) | identical |
| Sphere/mobile env UV | `*0.5` / `(0.5,-0.5)+(0.5,0.5)` | same | identical |
| Xbox car spec blend | `(diff+spec)*0.5` | same | identical |
| Mobile spec lobe | `pow(...,10.0)*2.0` | same | identical |
| Wind vertical displacement | `dist*0.3` | `dist*0.3` | identical (but `dist` differs, §5) |
| Radiosity | `saturate(c*2-limit)*intensity*passes` | same | identical |
| VC trails | `30/255`, `saturate(RGB1*2)` | same | identical |
| `vehicleVS` ambient | `ambient*surfAmb` | `ambient*max(surfAmb,0.4)` | **DIVERGENT** — 0.4 floor lifts every material below 0.4; can pastel/green the shade side |
| `ps2BuildingWindVS` noise | 2-octave `lerp(...,0.5)` | one octave (comma expression) | **DIVERGENT** — half the intended octave mix |
| `xboxBuildingWindVS` wind | `dist` only | `(dist + 0.35*Weather*sin(phase)) * saturate(z*0.12)`; `c23.w` always 1.0 | **DIVERGENT** — gust + height mask always active, despite "w==0 legacy" comment |
| `main_normMapBuilding` | none | `nmapDiffuse*surfDiff*0.3` | ours-only; dead payload |
| PBR/modern | none | many (1.5, 0.5, 0.15, 0.08, 0.3, 4.0, ...) | ours-only; out of legacy scope |

**Sweep conclusion:** if a future report says "the Xbox building is still
quarter-tiled / 10× bright", that is not supported by the live source. If a
legacy car is pastel/green, `vehicleVS:38` is the first shader-side constant to
test; if foliage sway is wrong, the PS2 wind comma expression and the c23
always-on extension are the first content suspects.

## 4. Task 3 — Alpha output semantics

| Entry | Output alpha | Upstream | Flag |
|---|---|---|---|
| `main_simple` | `tex.a * color.a * colorscale.x` | same | matches; cutout uses texture alpha |
| `main_simpleDetail` | `tex0.a * color.a * scale.x` (detail only RGB) | same | matches |
| `main_simpleFog` | `tex.a * color.a` (fog RGB only) | same | matches |
| `main_xboxBuilding` | `tex0.a*color.a + tex1.a*envcolor.a`; `envcolor.a=0` → texture/vertex alpha | same | matches |
| `main_*NoiseDetail` | same as parent entries; jitter only UV | 2_a same parent composition | matches (implementation differs) |
| `main_normMapBuilding` | base alpha; debug modes force 1 | no upstream | ours-only; dead |
| `grassPS` | `tex.a*color.a*1.992` | same | upstream-identical, but the 1.992 gain can turn a 128-alpha cutout opaque; not a regression |
| `blurPS`, `contrastPS`, `gradingPS`, `radiosityPS` | forced `1.0` | same | postfx; matches |
| `iiiTrailsPS` | forced `1.0` | same | matches, but not created (NULL) |
| `vcTrailsPS` | `saturate(loop result).a` | same | matches |
| `main_specCarFx` (modern) | `saturate(env.a*envcolor.a + spec.a)` | upstream unsat sum | alpha is not consumed: FX pass forces `ALPHAFUNC=ALWAYS`; harmless for blending, but flag if a path ever uses normal alpha |
| `main_ps2EnvSpecFx` (modern) | `saturate(sum).a`; both VS contributions are 0-alpha | upstream unsat | same; FX ALWAYS |
| `main_mobileVehicle` (modern) | `tex.a*color.a + ReflCol.b*0.125`, **not clamped** | same | alpha can exceed 1 or stay <1 on translucent paint; blending is CPU-decided. This is the shader leg if a mobile car ghosts |
| `main_xboxCar` (`vehiclePipeVS.hlsl:262`) | `matCol.a` | same | **flag (not a divergence):** SA paint marker `material->color.alpha==16` reaches the PS as 0.063 → blended panels. This is the known C++ `SetupMaterial_Xbox` gap, not a shader edit |
| `xboxBuildingVS` / `xboxBuildingWindVS` | `(prelightAlpha*surfDiff + NdotL)*matCol.a` | upstream: `prelightAlpha*surfDiff*matCol.a` | **FLAG — DIVERGENT:** `directCol.w=1.0`, so `directContrib.a=NdotL` is injected into colour alpha. Lit cutouts can stop cutting; solid surfaces can gain alpha >1 and blend/overbright |
| `xboxBuildingWindVS` | `(DayColor.a*dayparam.a + NightColor.a*nightparam.a)*matCol.a` | upstream remaps `dayColor.a=NightColor.a` → `NightColor.a*(dayparam.a+nightparam.a)*matCol.a` | **FLAG — DIVERGENT:** output alpha now rides the day vertex alpha, which is the *wind weight*. Foliage cutout alpha changes with sway mask; some vertices can cut to holes, others become opaque |
| `ps2BuildingVS`, `sphereBuildingVS` | prelight alpha * matCol/colorScale | same | matches |
| `specCarFxVS` | `Speccolor.a=1.0` | same | matches |
| `ps2CarFxVS` | `Envcolor.a=0`, `Speccolor.a=0` | same | matches (FX ALWAYS) |
| `leedsCarFxVS` | `Color.a = shininess*lightmult` | same | matches |
| PBR `main()` (`VehiclePBR_Modern.hlsl:818`) | `lerp(diff.a,1.0,nmParams.w)` | no upstream | ours-only; `c48.w` is the per-texture override. Stale/zero `c48.w` on an alpha-bearing body texture is the only shader-side x-ray path |
| PBR `main_building()` (`:1273`) | `lerp(saturate(diff.a*matA),1.0,nmParams.w)` | no upstream | ours-only; same `c48.w` contract |

**Cutout/blend summary:** the only legacy entries whose alpha semantics now
differ from upstream are the two Xbox building VS shaders. The texture-family
PS entries (simple/detail/fog/xbox/noise) all return texture×vertex alpha, so
they cut exactly as upstream. The known car x-ray is *not* a legacy PS alpha
change; `main_xboxCar` matches upstream, and the missing fix is the C++
paint-marker clamp.

## 5. Task 4 — Xbox building VS pair vs junior

### 5.1 Companion PS contract

Junior `xboxBuildingPS.hlsl` (and our `main_xboxBuilding`) expects:

- `TEXCOORD0` = transformed diffuse UV; VS does `mul(texmat, float4(uv,0,1)).xy`.
- `TEXCOORD1` = env UV; VS does `mul(envmat, normal).xy * envXform.zw`.
- `COLOR0` = prelight; PS multiplies by `s0` texture.
- `COLOR1` = env strength; PS multiplies by `s1`; alpha is expected to be 0
  because the VS emits `float4(1,1,1,0)`.

Both our Xbox VS files satisfy the UV/COLOR layout. The **only** companion
mismatch is that our VS emits an extra sun contribution in `COLOR0` (and
therefore in `COLOR0.a`), while both junior and original expect prelight +
ambient only. The PS itself is not at fault; it faithfully returns
`tex0*COLOR0 + tex1*COLOR1`.

### 5.2 Register map (ours)

| Register | Declaration | Upload / consumer |
|---|---|---|
| `c0-c3` | `float4x4 combined` | `pipeGetComposedTransformMatrix` (WVP), both VS |
| `c4` | `float3 ambient` | `buildingAmbient` |
| `c5` | `float4 directCol` | **ours-only**; `pipeUploadLightColorForce(pDirect, 5)` writes `x/y/z=colour, w=1.0` |
| `c6` | `float3 directDir` | **ours-only**; `pipeUploadLightDirectionForce(pDirect, 6)` writes light-travel `at`, `w=1.0` (ignored by float3) |
| `c19` | `float4 matCol` | material colour; multiplies all four channels |
| `c20` | `float3 surfProps` | `.x=surfAmb`, `.z=surfDiff`; `.y` unused by the Xbox family |
| `c21` | `float2 windPos` | wind VS only; `setWindParams` global wind position |
| `c22` | `float windIntensity` | wind VS only; `2.0 + 1.5*CWeather__Wind` |
| `c23` | `float4 windExt` | **ours-only**; `TreeWind_UploadWindExt` writes `{0.12, phase, 0.35*Weather, 1.0}` — `.w` is hard-coded `1.0` |
| `c29` | `float4 shaderParams` | declared, unused by both Xbox VS |
| `c30`/`c31` | `dayparam`/`nightparam` | DN prelight blend |
| `c32-c35` | `float4x4 texmat` | diffuse UV transform; only `.xy` used |
| `c36` | `float4 fxParams` | `.x=shininess`, `.y=lightmult` → `Envcolor` |
| `c37` | `float4 envXform` | `.zw` env UV scale |
| `c38-c40` | `float3x3 envmat` | env UV rotation |
| `c24-c27` | **not declared** | `REG_worldMat=24` is PBR-only (`buildingPBRVS`). The Xbox pair does not use a world matrix; it relies on SA building normals/vertices being world-space |

**c24-27 verdict:** no conflict and no missing upload in the Xbox callbacks.
However, the PBR callback's stale-light guard writes zeros to **VS c6-c11**
(`buildingPipe.cpp:2403`) after uploading the sun. If `buildingPBRVS` is
missing and the PBR callback falls back to `xboxBuildingVS`, c6 (sun direction)
is zeroed and the new sun term silently disappears. This is a latent fallback
only; the Xbox callback does not zero c6. `c24-27` would only be needed if the
Xbox pair ever consumes object-space normals/positions — it does not.

### 5.3 Math and alpha verdict

| Item | Junior | Ours | Verdict |
|---|---|---|---|
| Prelight | `prelight=DayColor*dayparam+NightColor*nightparam`; `OUT.Color=(prelight*surfDiff + float4(ambient,0)*surfAmb)*matCol` | same prelight, same `*surfDiff`, same `ambient*surfAmb`, same `*matCol` | **identical term** |
| Sun | none | `directContrib=directCol*max(dot(IN.Normal,-directDir),0)`, added before `*matCol` | **DIVERGENT** — doubles baked sun on lit faces, can invert perceived face shading if `IN.Normal` is not world-space |
| Env | `Envcolor=float4(1,1,1,0)*shininess*lightmult` | same | identical |
| Wind | `dist=(noise(windPos+pos.xy)-0.5)*windIntensity*(1-DayColor.a)`; x/y += dist, z += dist*0.3 | same base; adds `hmask=saturate(z*windExt.x)`, `gust=windExt.w*windExt.z*sin(windExt.y)`, `dist=(dist+gust)*hmask`; `windExt.w=1.0` | **DIVERGENT** — not byte-identical legacy; the "w==0" escape hatch is never taken because `TreeWind_UploadWindExt` always uploads 1.0 |
| Vertex alpha | `dayColor.a=NightColor.a` before prelight; output alpha = `NightColor.a*(dayparam.a+nightparam.a)*matCol.a` | No remap; output alpha = `(DayColor.a*dayparam.a + NightColor.a*nightparam.a)*matCol.a`, plus `NdotL*matCol.a` from `directContrib.a` | **DIVERGENT** — day alpha (wind mask) leaks into alpha; sun adds NdotL |
| PS alpha | `tex0.a*COLOR0.a + tex1.a*COLOR1.a`; `COLOR1.a=0` | same | PS matches; VS divergence propagates |

**Why the alpha divergences matter.** `DayColor.a` is consumed as the wind
weight (`intensityMask = 1-DayColor.a`) and is *not* an opacity value on wind
geometry. Upstream deliberately replaces it with `NightColor.a` before the
prelight, so the PS alpha is a normal DN blend. Ours keeps the wind weight and
adds `NdotL`, so an alpha-tested leaf vertex can output alpha unrelated to its
cutout mask. Combined with `vertexAlphaIsAlpha=false` in the Xbox callback
(blend decision ignores vertex alpha), this produces *alpha-test* holes/opaque
shifts rather than a consistent blend. This is the highest-confidence
shader-only explanation for foliage/cutout oddities on the Xbox building path.

**Normal-space warning.** The comment claims building normals are world-space
because atomics carry identity object matrices. If any Xbox building atomic has
a non-identity frame, `dot(IN.Normal, -directDir)` lights the wrong side. The
same warning applies to the alpha term: a negative/positive NdotL flips the
alpha contribution. Upstream has no such term, so reverting `c5/c6` removes
both the double-sun and the alpha contamination in one change.

## 6. Task 5 — Coverage: ours-only legacy shaders and dropped upstream shaders

### 6.1 Legacy entries we ship with no upstream counterpart

| Entry / CSO | IDR | Status | Notes |
|---|---:|---|---|
| `normMapBuildingPS.cso` (`main_normMapBuilding`) | 236 | embedded, **never `makePS()`-ed** | ours-only DK22Pac normal-map building path; dead payload today |
| `normMapVehiclePS.cso` (`main_normMapVehicle`) | 237 | embedded, **never `makePS()`-ed** | ours-only; dead payload |
| `customBuildingVS.cso` | 235 | embedded, **never `makeVS()`-ed** | no HLSL source anywhere; `vehiclePipeVS` is created from IDR_VEHICLEVS instead. `vehiclePipe.cpp:855` even mislabels it `IDR_CUSTOMBUILDINGVS` |
| `simpleNoiseDetailPS`, `simpleDetailNoiseDetailPS`, `xboxBuildingNoiseDetailPS` | 278/231/227 | **shipped and loaded** | no same-name upstream; functional counterpart is junior `ps/2_a/*StochasticPS`. Mark as **replacement**, not a missing upstream |
| PBR/Modern family: `VehiclePBR_Modern`, `BuildingPBRPS`, `vehiclePBRVS`, `buildingPBRVS`, `Rubber_Vehicle_Modern`, `Glass_Vehicle` | 203/239/211/238/240/204 | shipped | no upstream reference by definition; out of legacy scope |
| `treeAlphaPS` (postfx) | 279 | shipped | ours-only alpha-test override; not a pipe shader |

`mobileBuildingVS`, `postfxVS`, `iiiTrailsPS`, `neoVehiclePass1/2VS`, and
`ps2BuildingSSSVS` all have CSOs on disk but **no IDR/loader**. They are not
shipped in the runtime sense. (`ps2BuildingSSSVS` is ours-only; the other four
are upstream entries, see §6.2.)

### 6.2 Upstream shaders we dropped

| Upstream entry | Depth | Our status | Replacement / result |
|---|---|---|---|
| `ps/envCarPS.hlsl` and `ps/2_a/envCarPS.hlsl` | full env/spec car shader | no source, no CSO, no IDR | Env callback uses `vehiclePBRVS` + PBR `main()` (`vehiclePipe.cpp` Env cb). The old `diff/env/mask` composition is not reproduced |
| `vs/envCarVS.hlsl` | env-car VS, 7-light loop, Fresnel envcolor | no source, no CSO, no IDR | SAME: Env uses `vehiclePBRVS` |
| `ps/iiiTrailsPS.hlsl` | III trails, 5× feedback, alpha forced 1 | source exists, CSO exists, **no IDR, never `makePS()`** | `iiiTrailsPS` pointer stays NULL; `postfx.cpp:2088` passes NULL into `ColourFilter_Generic` for the III trail mode |
| `vs/mobileBuildingVS.hlsl` | mobile building VS with `ambient*surfAmb*1.5` | source exists, CSO exists, **no IDR, no loader** | mobile building falls back to the PS2 building VS path |
| `vs/postfxVS.hlsl` | passthrough | source exists, CSO exists, **no IDR, no loader** | postfx draws use their own VS/bracket; no IDR_POSTFXVS exists |
| `vs/neoVehiclePass1VS.hlsl`, `vs/neoVehiclePass2VS.hlsl` | neo two-pass specular | source exists, not compiled, not loaded | deliberately dead: `CarPipe::vertexShaderPass1/2` are permanently NULL |
| `include/StochasticSamplerPS.hlsl` | 3-tap triangle-grid stochastic sampler | deleted (`git status: D`) | replaced by `include/Noise.hlsl` + `noiseDetailBuildingPS.hlsl` |
| `ps/2_a/simpleStochasticPS.hlsl` | stochastic simple building | deleted | replaced by `main_simpleNoiseDetail` |
| `ps/2_a/simpleDetailStochasticPS.hlsl` | stochastic detail building | deleted | replaced by `main_simpleDetailNoiseDetail` |
| `ps/2_a/xboxBuildingStochasticPS.hlsl` | stochastic Xbox building | deleted | replaced by `main_xboxBuildingNoiseDetail` |

**Shipped-but-dead duplicates:** `shaders/ps/simplePS.hlsl`,
`simpleDetailPS.hlsl`, `simpleFogPS.hlsl`, `xbox/xboxBuildingPS.hlsl`,
`normMapBuildingPS.hlsl`, `ps/mobile/mobileVehiclePS.hlsl`,
`vs/mobile/*`, `vs/ps2/*`, `vs/xbox/*` are not compiled (root file wins or
fast_build skips them). Do not audit those copies as live content; they contain
stale register values (for example the dead `vs/xbox/*` files lack c5/c6 and
the dead `vs/mobileVehicleVS.hlsl` uses c35 campos while the live consolidated
entry uses c36).

## 7. Task 6 — Ranked shader-level suspects for current symptoms

Ranking is by **proven content divergence × reachability**, not by visual
confirmation. The state-machine layer (corona blend, dual pass, alpha contracts)
still owns the black-quad and x-ray root causes per the existing audits; the
list below isolates the shader-side contribution.

| # | Suspect | Symptoms | Mechanism | Confidence |
|---:|---|---|---|---|
| 1 | **`xboxBuildingVS` / `xboxBuildingWindVS` add `c5/c6` sun on top of baked DN prelight** (`vs/xboxBuildingVS.hlsl:72-75`, `Wind:114-117`) | washout/green, face inversion, cutout/blend | Upstream has no sun in the Xbox building VS. The extra `directCol*NdotL` double-counts sunlight already in `DayColor/NightColor`; if `IN.Normal` is not world-space the lit side flips; `directCol.w=1` makes `directContrib.a=NdotL`, contaminating output alpha | **High** that content differs; **65%** that it contributes to the look |
| 2 | **`xboxBuildingWindVS` no longer remaps `DayColor.a` to `NightColor.a`** (`Wind:79-85`) | cutout/blend on wind foliage; alpha-test holes | `DayColor.a` is the wind weight (`1-DayColor.a`). Upstream replaces it before the prelight so output alpha is the normal DN blend; ours keeps the wind weight, so foliage alpha tracks sway mask, not the cutout. `c23` height/gust is also always enabled (`treeWind.cpp:314-337` writes `w=1.0`) | **High** (direct source diff) |
| 3 | **`vehicleVS` ambient floor `ambient*max(surfAmb,0.4)`** (`shaders/vs/vehicleVS.hlsl:38`) | washout/green, flat shade side (PS2/PC/mobile no-FX vehicles) | Upstream is `ambient*surfAmb`. The 0.4 floor lifts every material whose ambient reflectivity is below 0.4, desaturating/pastelizing the shade side and feeding the "flat/x-ray" read on non-PBR cars. It is a one-token revert | **Medium-High** (proven divergence; 55% look contribution) |
| 4 | **FX PS rewritten to PBR `main_specCarFx` / `main_ps2EnvSpecFx` with `ClusterLightingScreen` + `saturate`** (`VehiclePBR_Modern.hlsl:844-858`, `871-883`) | black light quads / additive FX | Upstream is exactly `env*envcolor + spec*speccolor` with no clamp. Ours samples Forward+ `s5-s7`/`c45`; the legacy spec/FX callbacks do not refresh those registers in-callback (device-persistent only, per prior cluster audit), so a stale `c45.w>0` plus stale atlases can add arbitrary light to lamps/coronas and `saturate` changes additive headroom. Primary black-quad cause remains the C++ blend substitution | **Medium** (35% shader-side; state machine primary) |
| 5 | **Vehicle x-ray shader leg:** `main_xboxCar` writes `matCol.a` unchanged (`vehiclePipeVS.hlsl:281`); PBR `main()` writes `lerp(diff.a,1.0,nmParams.w)` (`VehiclePBR_Modern.hlsl:818`); `main_mobileVehicle` adds `ReflCol.b*0.125` to alpha (`:904`) | vehicle x-ray / ghost panels | The Xbox VS is **identical to upstream**, so the legacy shader is exonerated. The known root is CPU `material->color.alpha==16` + `SetupMaterial_Xbox` missing the paint-marker clamp + dual pass. The only shader-side x-ray risk is a stale/wrong `c48.w` (PBR) or an alpha-bearing mobile texture | **Low shader (10%); C++ root ~85%** |

**Additional real divergence, not in the symptom list:** `ps2BuildingWindVS`
uses a comma expression (compiles with FXC warning X3081), so only the second
noise octave reaches `dist`. Junior blends two octaves at 0.5. This halves/changes
the PS2 tree sway; it is not a black-quad/washout/x-ray cause.

**What is *not* a shader suspect anymore:**

- `main_xboxBuilding` quarter-tile/10× bug — fixed and byte-correct per original.
- PBR normal-buffer world/view mismatch — current `main()` and `main_building()`
  transform view→world via `c25-c27` and apply hemisphere/anti-flip gates; the
  remaining risk is only a stale/unuploaded `c25-c27` or a bad normal buffer.
- PBR clearcoat body replacement — current code is additive (`layer2 = envTerm *
  saturate(kr)*0.5*envTexMod`), not `lerp(layer1, envTerm, kr)`.
- `main_xboxCar` alpha — upstream-identical; changing the shader would mask the
  C++ alpha-16 bug rather than fix it.

## 8. Verification notes / non-findings

- **FXC check performed (read-only):**
  `fxc -T vs_3_0 -E main` on `shaders/vs/ps2BuildingWindVS.hlsl` succeeds with
  `warning X3081: potentially unintended use of a comma expression in a return
  statement` and `X3206` truncation. The comma expression is real, not a display
  artifact: the first `lerp(...)` result is discarded, so the noise is one octave.
- **No shader source was edited during this audit.** The only write is this file.
- **Junior is not canonical for the Xbox PS.** Junior is the only reference with
  `*0.5`/`*10.0`; original matches our fixed source. Any future re-port must use
  original (or the documented aap fix), not junior.
- **`shaders/ps/xbox/xboxBuildingPS.hlsl` is already fixed** but dead; the live
  `buildingPipePS.hlsl` `main_xboxBuilding` is the one that becomes
  `xboxBuildingPS.cso`.
- **Stale comments to correct eventually (not edited here):**
  `buildingPipe.cpp:1796` still says the wind VS "does not declare c5/c6" (it
  does now); `treeWind.cpp:314-337` says `c23.w==0` is the legacy path but always
  writes `1.0`; `vehiclePipe.cpp:855` labels `vehiclePipeVS` as
  `IDR_CUSTOMBUILDINGVS` although it is created from `IDR_VEHICLEVS`.
- **Coverage gaps that cannot be content-audited from source:** `customBuildingVS.cso`
  is an opaque embedded blob with no HLSL; `iiiTrailsPS.cso` is compiled but
  never created; `mobileBuildingVS.cso`/`postfxVS.cso` are built but unshipped.
  These are counted in §6, not diffed.
- **Alpha-cutout acceptance check for any future fix:** with Xbox building
  selected, inspect a known alpha-tested leaf/bush at a wind vertex (day alpha
  varies) and a lit facade. If foliage holes track the sway mask, suspect #2 is
  live; if lit facades are pastel and backfaces look sunlit, suspect #1 is live.
- **Black-quad acceptance check:** the shader-side FX change can only be blamed
  after the C++ `effectsForceAlphaBlock`/`coronaDrawRawShield` contract is shown
  to leave `ONE/ONE` intact for the light/corona draw. Shader `saturate` alone
  cannot create a hard-edged black rectangle.
- **X-ray acceptance check:** toggle `vehiclePipe=XBOX` to `PS2`; if the same
  car becomes solid, the C++ paint-marker/dual-pass path is confirmed and the
  shader list above is not the fix.
