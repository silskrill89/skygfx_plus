# Water reference recipe (from recon lane exp-1, 2026-09-30) — for IV/V pipe variants

## Sources
- Xbox water: `E:\dev(dave)\releases\GTASource\xbox_SA\port_analysis\skygfx_xbox_features.md` +
  `data_diff_3way.txt:175-280,389-501`. NO Xbox shader exists — Xbox look = stock SA CWaterLevel
  + timecyc. Key: WaterRGBA alpha col40 = **127 (Xbox/PS2) vs 255 (PC)** (transparency), extra col51
  "water reflection alpha"=1.0 (UNCERTAIN label — verify vs CloudAlpha). Hooks:
  `CWaterLevel::CalculateWavesForCoordinate` **0x6E6EF0** (outputs z, colorMult, NORMAL — shading
  waves already exist, geometry displacement droppable), HD-water color quirk `*0.65+0.27` at
  **0x6E716B/0x6E7176** (Nop 6), Renderers: RenderWaterTriangle 0x6EFC9B, RenderWaterRectangle
  0x6EFE4C/0x6F004E, RenderFlatWaterRectangle 0x6ECED8/0x6ECE15/0x6EC509/0x6EC5B8,
  RenderHighDetailWaterRectangle 0x6ECD69/0x6EBA1D/0x6EBAF8, detailedWaterDist *(float*)0x8D37D0.
- GTA IV: `E:\SDKs\GTAIV.EFLC.FusionShaders\win32_30_nv8\water\waterPS1/2/3.asm`,
  `waterTex\waterTexPS1.asm+waterTexVS0.asm`, `snippets\water.7z→ps3watervs.hlsl`.
  2-layer scrolling normals: layer scales **0.002** (amp 0.0512) + **0.01** (amp 0.256) world XZ;
  N=(dx,dy,1.00001). Fresnel `mix = 0.2+0.6·(NdotV)⁵` (c4=0.6/0.2). Reflection = screen-space
  ReflectTextureSampler via viewProj c74 × waterReflectionScale c73. Translucency:
  `opacity = depthDiff·waterColour.a` (c66.w); shore `saturate(opacity^0.25 + 0.125)`; colour mixed
  with bottomSkyColour c72.w. Distance normal fade `1 − dist²·4e-4`; fog `max(2·|viewZ|, 2000)`.
  PS3 VS shore: vtx alpha `saturate(exp2(log2(h)*4)*16)`, depth fade `saturate((z−simZ−2)*0.5)`,
  edge blend 0.75/0.25, finite-diff normals 4 height taps.
- GTA V/RAGE: `C:\Users\aaaaaaaaa\Downloads\P1\gta5\build\dev_ng\common\shaders\fx_max\water.fx`
  (+water_common.fxh), `common\data\watertune.xml`, `assets_ng\...\water.psc`,
  `common\data\levels\gta5\water.xml` (per-corner alphas). ALSO
  `E:\dev(dave)\releases\GTASource\rage\GTAV Source` (unverified — recon pending).
  GetOceanBump: 2 taps WaterBumpSampler2(worldXZ·gOceanParams0.y) + fine ×3.7 + 2 crossed
  low-freq layers scrolling ±gScaledTime.x/10 at /448 and /512; N.z=gOceanParams0.x;
  cell-mask pow(cellMask,32) hides tiling. Anti-chrome: reflectionNormal=lerp(N,(0,0,1),5/6),
  fresnel=lerp(dot(-V,N),1,0.3). Translucency: depthBlend=exp(float2(-20,-60·|V.z|)·WaterColor.a·depth·e);
  litRefr=lerp(2·WaterColor·refr,refr,depthBlend.y); water=lerp(waterCol,litRefr,depthBlend.x).
  Shore: refractionColor.a=saturate(depth); foam saturate((512-w)/512), FoamMask·(len(flatN.xy)·0.27+0.44),
  foamScale=1.0 foamWeight=0.65. Sun pierce pow(saturate(dot(refract(V,lerp(N,N0,.5),1/1.5),-Sun)),2)·saturate(depth/10).
  watertune: SpecularFalloff=1118, RippleScale=0.04, OceanFoamScale=0.05, RefractionBlend=0.7,
  RefractionExponent=0.25, FogPierce=1.1, DeepWaterModDepth=90/Fade=80, WaterColor 0x1A00191C.
  water.psc WaterQuad: per-corner alpha a1..a4 (default 26) → coast ramp.
- re3 WaterLevel.cpp (`E:\dev(dave)\releases\GTASource\GTA SA RE\re3-master`): RwIm3D convention
  SRCALPHA/INVSRCALPHA + edge alpha ramp `alpha −= (colorAlpha·0.4/16)·(|dx|+|dy|)`, wake fade
  {0.4,1.0,0.2,1.0,0.4}.
- SA timecyc (parsed already src/core/weather.cpp:33-132): WaterRGBA (rgb+a), Alpha1 col44,
  CloudAlpha, WaterFogAlpha; water.dat 307 rect/tri planes, per-corner alphas.
- CryEngine water + GTAV Source recon: PENDING (separate lane).

## Hybrid recipe (implemented in current Water_Parallax rewrite)
Flat geometry; 2-layer scrolling mipmapped normals (0.02/0.08 m⁻¹ rescale of IV 0.002/0.01,
cross ±t·0.1, fine ×3.7); reflection = timecyc sky tint × fresnel(0.2+0.6·w⁵) × reflectivity<1 with
damped N lerp(N,(0,0,1),5/6); depth shallow→deep exp ramp; shore alpha saturate(depthDiff^0.25+0.125);
foam len(dN.xy)·0.27+0.44; Blinn spec pow(~1000) low intensity (never wide GGX = chrome).
Tuning knobs live in PS composite block + waterPipe.cpp c0-c10 (waterTileScale, waterShoreFade,
waterTranslucency, waterReflectionStrength, waterUseTimecycle).

## Quality tiers (for new IV/V pipes)
Low=flat water.dat quads + vertex-colour timecyc + single normal layer; Med=+dual scrolling normals
+depth ramp; High=+screen-space refraction/reflection RT + foam + sun pierce; Ultra=+SSR-style
reflection march. IV pipe = IV constants (fresnel 0.2+0.6w⁵, shore ^0.25+0.125, 2-layer 0.002/0.01).
V pipe = V constants (damped N 5/6, exp depth blend, cell-mask tiling, Blinn 1118, watertune values).

## CryEngine + GTAV Source (2nd recon)
Sources scanned: `E:\dev(dave)\releases\GTASource\CRYENGINE-release` (main), `E:\SDKs\CRYENGINE-1`
(Code-only clone — NO `Engine/Shaders`, no `.cfx`; just C++ `CREWaterOcean/WaterVolume/waterman`),
`E:\SDKs\Crysis` (only `Game/Config/CVarGroups/sys_spec_Water.cfg`), `E:\SDKs\CloudWorks-rh`
(cloud/atmospheric scattering only — NOT water), `E:\dev(dave)\releases\GTASource\rage\GTAV Source`.
No other CryEngine/Crysis/water folders exist in GTASource beyond `CRYENGINE-release` (v1 gap filled).

### CryEngine water shaders — `CRYENGINE-release\Engine\Shaders\HWScripts\CryFX\`
`Water.cfx` (1290 ln ocean), `WaterVolume.cfx` (1276 ln), `WaterOceanBottom.cfx`, `WaterFogVolume.cfx`,
`Waterfall.cfx`, `WaterCausticsPass.cfi`, `WaterReflectionsPass.cfi`, `DeferredCaustics.cfx`.
C++: `Code\CryEngine\RenderDll\Common\RendElements\CREWaterOcean.cpp`, `CREWaterVolume.cpp`,
`RenderDll\Common\WaterUtils.cpp`, `RenderDll\XRenderD3D9\PostProcessWater.cpp`, `D3DDeferredPasses.cpp`;
params `Code\CryEngine\Cry3DEngine\3dEngine.cpp:2714-2750` + defaults `3dEngine.cpp:290-302`.

- **Fresnel (Water.cfx)** `GetEnvmapFresnel(WaterSpec0=0.02, FresnelGloss=0.9, NdotE)` =
  `lerp(0.02→white, pow(1-NdotE,5)/(40-39·gloss))`; denom at gloss .9 = 4.9. CONFIRMED (`shadeLib.cfi:223`).
  WaterVolume.cfx uses `GetFresnel(NdotE, FresnelBias=0.05, 5.0)` (bias-Schlick). Both are ps_3_0-safe.
- **Water.cfx param defaults** (all `REG_PM_PARAM_*`): ReflectionScale=1.0, SubSurfaceScatteringScale=2.0,
  ReflectionBumpScale=0.1, RefractionBumpScale=0.1, DetailNormalsScale=0.5, NormalsScale=1.25,
  GradientScale=0.1, HeightScale=0.2, RainTilling=1.0, RipplesNormalsScale=1.0. CONFIRMED.
- **Sun glitter (2 specular lobes)** `SunSpecular`: `R=reflect(-V,N)`, `LdotR=saturate(dot(SunDir,R))`,
  `fSpec = w.x·LdotR^E1 + w.y·LdotR^E2`, `E1=4·GlossToSpecExp(MtlSpec.w)`, `E2=0.5·GlossToSpecExp`,
  weights `E/(2π)+1/π`. → SM3.0: two `pow()` Blinn lobes, no special HW needed. CONFIRMED (`Water.cfx:1042-1060,690-693`).
- **Gloss breakup** `cGlossMap = tex(GlossMap, baseTC·0.05)·0.7+0.3` (kills tiling). CONFIRMED (`:1177`).
- **Normal layers** (pixel): detail layer `baseTC.wz`·DetailNormalsScale; two base layers
  `baseTC.xyxy·(0.25,0.25,1,1)` summed then `·NormalsScale(·0.5)`; + rain ripples `·RainTilling·300/Tilling·5.0`.
  World scroll `vTranslation=(AnimGenParams.z·OceanParams0.y·0.0025)·FlowDir`; base tiling `·0.005`. CONFIRMED (`:700-715,951-985`).
- **In-water fog / absorption** (both Water & WaterVolume): `waterVolumeFog=exp2(-cFogColorDensity.w·volumeDepth/
  dot(V,FrontVec))`, `refract=lerp(fogColor, refract, saturate(fog))`; `volumeDepth=max(sceneDepth−viewFront,0)`.
  exp2/log2 native in SM3.0. CONFIRMED (`Water.cfx:1160-1168`).
- **SSS** `surfaceSSSColor=MtlDif·SubSurfaceScatteringScale`; `refract += SSS·(EdotL²)·(fog·Sun)·waveHeight·softIsec`.
  CONFIRMED (`:1202-1210`).
- **Foam** `FoamSoftIntersectionFactor=0.75, FoamAmount=1.0, FoamCrestAmount=1.0, FoamTilling=12.0`;
  `fFoam=tex(Foam, baseTC·FoamTilling+N.xy·0.1)`; edge blend `saturate(-softIsec·(1+thresh)+fFoam)`;
  `fFoam·= saturate(softIsec−softIsec²)`; lit `·(Sun·NdotL·sunShadow+Sky)·FoamAmount`. CONFIRMED (`:1222-1250`).

### Ocean FFT / Gerstner-ish params (C++→shader) — `3dEngine.cpp:2733-2750`
`OceanParams0=(windDir=1, windSpeed=4.0, wavesSpeed, wavesAmount=1.5)`;
`OceanParams1=(wavesSize=0.75, sin(windDir), cos(windDir), waterLevel)`. `wavesSpeed` XML def 1.0 clamped
0-1 then `÷clamp(wavesSize,0.45,1)`; `wavesAmount` clamp 0.4-3.0; `wavesSize` clamp 0-3.0. CONFIRMED.
FFT vert disp (`Water.cfx:598-620`): `tcFFT=pos.xy·0.0125·OceanParams0.w·1.25`; `pos += atten·vtxDispl·0.06·
OceanParams1.x·(1.5,1.5,1)`; `atten=(camDist·0.5)²`; finite-diff normal taps at `1/64`. VTF `tex2Dlod` in
VS → **available in vs_3_0** but limited; safest SM3.0 path = pre-displaced flat grid or CPU waves. CONFIRMED.

### Caustics (`WaterCausticsPass.cfi` + `DeferredCaustics.cfx`)
Params `GetCausticsParams=(causticsTilling=1.0, causticsDistanceAtten=100.0, causticsMultiplier=0.85, 1.0)`;
`GetOceanAnimationCausticsParams=(1, causticHeight=0.0, causticDepth=8.0, causticIntensity=1.0)` (height XML 2.5). CONFIRMED.
Sharpen `cCaustic=saturate(5.65·c−4.66); c=c·c` (≈pow(c,16)); colour dispersion `refract(vSunTS,normal,0.9)`,
RGB offset 0/0.05/0.1; height atten `fAtten=(1−exp(−2·h))·step(0,h)`; dist atten `·vCausticParams.x/(0.075·camDistSq)`.
Anim PB_time 1.0/0.5/0.25/0.125. → SM3.0: forward 2-tap scrolling caustic (drop deferred/MRT + `modf/256`
depth-pack). CONFIRMED.

### Quality tiers — `E:\SDKs\Crysis\Game\Config\CVarGroups\sys_spec_Water.cfg`
Low(spec1)=Refractions0, Caustics0, FFT0, q_ShaderWater0, tess_amt1 · Med(spec2)=Refractions0, FFT0,
q_ShaderWater1, tess6 · High/Ultra(spec3/4)=Refractions1, Caustics1, FFT1, q_ShaderWater2, tess10,
ReflQuality4. Shader gates `_RT_QUALITY&&_RT_QUALITY1`→FFT disp; `GetShaderQuality()>=QUALITY_HIGH`→projected grid. CONFIRMED.

### Per-tier steal map (CryEngine+V sources → skygfx quality tiers) — all SM3.0-translatable
- **Low**: single scrolling normal layer (Water.cfx base TC·0.005 tiling, AnimGen scroll 0.0025·FlowDir)
  + Schlick fresnel (Cry bias-0.05/0.02 or IV 0.2+0.6·w⁵); flat grid, no displacement. CONFIRMED.
- **Med**: +2nd crossed low-freq layer (V: /448,/512 ±gScaledTime/10, fine×3.7, cellMask pow32)
  + forward 2-tap caustics (tilling 1.0, distAtten 100, mult 0.85, sharpen 5.65c−4.66→c², anim 1/.5/.25/.125). CONFIRMED.
- **High**: +FFT-lite height (Cry OceanParams: windSpeed 4.0, wavesAmount 1.5, wavesSize 0.75,
  pos·0.0125·amount·1.25, disp 0.06·size — CPU/VTF ≤64×64 LUT) + foam (softIsec 0.75, tilling 12,
  crest/amount 1.0) + 2-lobe sun glitter (E1=4·GlossExp, E2=0.5·GlossExp, w=E/2π+1/π). CONFIRMED.
- **Ultra**: +underwater volume fog `exp2(−fogDensity.w·volDepth/·(V·Front))` + SSS (scale 2.0, EdotL²·height)
  + screen-space refract/reflect (tex2Dproj on front buffer) + fog pierce (V: FogPierce 1.1). CONFIRMED.
- Tier gates mirror Crysis cfg (Refractions/Caustics/FFT off→Low/Med, on→High/Ultra). CONFIRMED.

### SM3.0 downgrade flags (CryEngine→ps_3_0/vs_3_0)
- **DROP tessellation** (DX11 hull/domain `WaterSurfaceHS/DS`) — use flat grid / VTF / CPU waves.
- **FFT VTF** `tex2Dlod` in VS is the only SM3.0-limiting piece; keep ≤64×64 displacement LUT or bake.
- **DeferredCaustics** = screen-space deferred pass → replace with forward projected/scrolling caustic.
- **MRT packing** (`OUT.Color.xy` depth pack via `modf/256`) — drop, single RT enough.
- Screen-space reflection/refraction via `tex2Dproj` on scene colour/depth is OK (skygfx has front buffer).
- `exp2/log2/pow/reflect/refract`, Schlick fresnel, 2-lobe Blinn: all native SM3.0. CONFIRMED.

### GTAV Source verification — `rage\GTAV Source\src\dev_ng\game\shader_source\Water\`
Full RAGE water shader set present: `water.fx` (797), `water_common.fxh` (1024), `waterTex.fx` (1427),
`water_foam/fountain/poolenv/river/riverfoam/riverlod.fx`. **water_common.fxh CONFIRMS P1/gta5 constants
already in V section** (no diff): `DEFAULT_SPECULARFALLOFF=1118.0`, `FOAMWEIGHT=0.65`, `FOAMSCALE=1.00`,
`GetOceanBump` `cellMask=pow(cellMask,32)` + `bumpHigh·3.7` + lowBump `/448 & /512 ±gScaledTime.x/10`,
`fresnel=lerp(dot(-V,N),1,.3)`, `depthBlend=exp(float2(-20,-60|V.z|)·WaterColor.a·depth·e)`,
`foam=FoamMask·(len(flatN.xy)·0.27+0.44)`, `pierce=pow(dot(refract(V,lerp(N,N0,.5),1/1.5),-Sun),2)·sat(depth/10)`,
`GetSpecularColor` Blinn `pow(dot(-halfAngle,N),SpecularFalloff)`. CONFIRMED.
**DIFFERENCES vs P1/gta5:** (1) extra river/fountain/poolenv/foam shaders; (2) river pierce variant uses
`1/1.65` and `+SunDirection` (ocean uses `1/1.5`,`-SunDirection`) — minor, UNCERTAIN which P1 had;
(3) full C++ renderer `Water.cpp`(11225), `newWater.cpp`(3245), `WaterSPU.*`, `waterdefines.h`;
(4) **`watertune.xml` / `water.xml` NOT in source tree** (data-only) — cannot re-diff those against P1 here,
V-section watertune values remain authoritative. No shader-constant drift detected. CONFIRMED.

## Water body type detection (3rd recon)

Sources: live `E:\games\gtasa_skygfx_plus\data\water.dat` (parsed, 307 planes), gta-reversed
`source/game_sa/WaterLevel.{h,cpp}`, our `src/render/waterPipe.cpp` + `src/core/main.cpp`,
`data/info.zon`+`map.zon`, `VisibilityPlugins.cpp`, re3 `renderer/WaterLevel.cpp`.

### What SA water.dat actually carries (CONFIRMED)
- Line = 3-4 verts × `x y z flowX flowY bigWaves smallWaves` + optional trailing **flags** int
  (WaterLevel.cpp LoadDataFile:55-148). **NO per-corner alpha column in SA** — water-refs line 40's
  "per-corner alphas" is GTA V `water.xml`; SA draw alpha = `WaterLayerAlpha[2]` uniform per layer
  (WaterLevel.h:138, `GetWaterColorForRendering:223`). High-detail path is unreversed (0x6EB810) →
  UNCERTAIN whether SA ramps edge alpha like re3 III (`alpha −= colorAlpha·0.4/16·(|dx|+|dy|)`).
- Measured: flags {1:284, 3:21, 0:2}; bit0 clear → `bInvisible` (collision-only, 2 planes at
  (-848..-464, -2082..-1864)), bit1 → `bLimitedDepth` (`AddWaterLevelQuad 0x6E7EF0` → WaterLevel.cpp:895).
  **flowX/Y are ALL ZERO** (no current data — VC-only concept); `bigWaves/smallWaves` are the only
  per-vertex wave/noise data. Waves correlate with type: mean bigWaves **0.66** ocean (52 planes touch
  ±2990, 16 >100k m²) vs **0.14** lakes vs **0.29** elongated inland strips vs **0.00** limited-depth.
- Geometry+flags classify all 307: OCEAN 56 / LAKE 196 / RIVER-cand 15 (inland, aspect≥6) / POND 19
  (≤4000 m²) / SHALLOW 20 (flag&2, z 7.6-112) / INTERIOR 1 (z=1082.7 → `IsInInterior`, h:49).
- Game's own ocean notion: `BlockHit 0x6E6CA0` pushes edge blocks (idx 0/11 of 12×12, block=500m)
  into `m_BlocksToBeRenderedOutsideWorld` (X 0xC21560, Y 0xC214D0, count 0xC215EC) → "general ocean
  plane" beyond ±3000; consumer is in unreversed `RenderWater 0x6EF650` (UNCERTAIN call addr).
- Zones: `eZoneType` = NAVI/LOCAL_NAVI/INFO/MAP only (Zone.h:12) — OCEAF1-3 exist but no water type →
  LOW signal. `CVisibilityPlugins` = underwater entity lists only, no water tags. `GetWaterLevelNoWaves
  0x6E8580` returns bool+level, **no plane ID** → own point-in-poly needed.

### Candidate schemes (rank = reliability/cost)
1. **CPU per-vertex classify at flush (RECOMMENDED)** — in `CWaterLevel__RenderAndEmptyRenderBuffer_hook`
   (main.cpp:1465) loop `TempVertexBuffer[0..TempBufferVerticesStored)` @0xC4D958 (36B verts:
   pos12/normal12/color4/u4/v4), point-in-poly vs a table built at init, write `bodyType`/`blend` into
   **objNormal** (`Water_VS.hlsl` declares NORMAL but never reads it; write ONLY inside our branch so the
   vanilla fallback path stays pristine). Reliability HIGH, cost ~300 verts × bucketed tests (~O(1)).
   Note flushes are quad-boundary aligned (RenderIfDoesntFit precedes each push) but can hold MULTIPLE
   different quads → per-flush single constant is NOT safe (that's why per-vertex wins).
2. **Quad-level hook → constant** — `RenderWaterRectangle 0x6EC5D0` / `RenderWaterTriangle 0x6EE240`
   (CONFIRMED RH installs) get full quad bbox + all 4×CRenPar (z, bigWaves, smallWaves) → set
   `g_bodyType`, force a flush on type change. Reliability HIGH, extra draw calls, more invasive.
3. **VS analytic + LUT** — ocean = `max(|x|,|y|)>2950` (covers outside-world plane), interior `z>950`;
   else sample 256² R8 LUT on free sampler **s3** via `tex2Dlod` (23 m/texel → rivers 12-64 m wide get
   diluted; dilate/nearest). Reliability MED, zero CPU, no vertex writes.
4. Wave-amp heuristic (bw>0.4→ocean, 0→shallow) — only reachable via scheme 2's CRenPar; weak alone.
5. `CTheZones::FindZone` name (OCEAF) — UNCERTAIN addr, ignores 95% of water → LOW, skip.

### Recommended implementation
- **Table build (once, after `WaterLevelInitialise 0x6EAE80`)**: read `WaterQuads 0xC21C90` (301×0xA),
  `WaterTriangles 0xC22854` (6×8), `m_aVertices 0xC22910` (1021×CWaterVertex{int16 x,y; float z,
  bigWaves, smallWaves; int8 flowX,flowY} — sizeof UNCERTAIN, 18→20 pad), counts 0xC22884/88/8C; or
  hook `AddWaterLevelQuad 0x6E7EF0`/`AddWaterLevelTriangle 0x6E7D40` to capture flags verbatim.
  Bucket into 250 m cells. Rebuild if `m_nWaterConfiguration 0xC228A0` flips (water1.dat) — UNCERTAIN.
- **bodyType enum** (float code in objNormal.x, blend in .y): 0=OCEAN (bbox touches ±2990 OR
  area>150 000 m² OR any vert outside ±3000), 1=SHALLOW/SHORE (flag&2), 2=INTERIOR/POOL (z>950),
  3=RIVER (inland & aspect≥6), 4=POND (≤4000 m²), 5=LAKE (rest). Per-point test is exact → seams only
  at quad borders; ramp `blend` from distance-to-quad-edge (≤5 m) in the CPU pass.
- **Shader side**: VS forwards objNormal.xy → PS interpolator (TEXCOORD5); PS picks effect params from
  free constants (c12-c14) — ocean: full wave/foam/sun-pierce; lake: calm low-amp normals; river:
  anisotropic scroll (direction = plane aspect axis, since flow data is zero); shallow: high opacity +
  strong shore fade. CPU constant-per-draw only possible with scheme 2.

