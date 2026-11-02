# CryEngine 3.8.6 → SM3.0: two cheapest genuine upgrades

Source of truth: `E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\Shaders` and
`...\Engine\EngineAssets` (244 files). Our tree: `E:\dev(dave)\skygfx_plus_expIV`.
Companions: `docs/plans/2026-10-25-cryengine-parity-matrix.md` (effect rows) and
`docs/plans/2026-10-25-cryengine-cloud-reference.md` (cloud/atmosphere recipes).
This document does **not** re-recommend anything already specified there. Line numbers
are from the working tree at audit time; if a file has shifted, use the named
function/comment as the anchor.

**TL;DR**

| Pick | CryEngine source | Technique | New taps | Measured PS slots | INI master (default) |
|---|---|---|---|---|---|
| **A** | `VolumetricFog.cfi:56-79` (`GetVolumetricFogColorInternal`) + constants `D3DHWShader.cpp:1415-1447` | Radial sun in-scatter added to the analytic height-fog colour | **0** | 31 → **57** (68 with `pow`) | `heightFogSunScatter = 0` |
| **B** | `DeferredCaustics.cfx:85-178` (`DeferredCasticsPS`) | Projected/animated caustics on submerged geometry | **2** | 53 → **89** | `waterCaustics = 0` |

Both are post-process passes. Neither touches the already-over-budget PBR pixel shader,
neither needs a loop, MRT, compute, UAV or tessellation, and neither touches the frozen
`c22`/`c23`, `s9`, or `c49-c52` contracts. Rough LOC: A ≈ 10 shader + 15 C++/config
lines; B ≈ 15 shader + 50 C++/resource/config lines (the state save/restore and texture
loader dominate B).

Recommended order: **A first, then B**. A has no new asset, no new sampler and its
constants fit the pass's existing `c0-c7` save/restore window. B adds an embedded texture,
a sampler and `c8-c10`; it should land after A's INI/config plumbing is proven.

---

## 0. Measured budget reality (why both picks are post passes)

The parity matrix estimates the PBR PS at “roughly 300-380 slots”. The actual FXC
(June 2010, `/T ps_3_0 /E <entry> /O3`) counts are much higher:

| Entry point | Slots | Texture | Arithmetic |
|---|---:|---:|---:|
| `VehiclePBR_Modern.hlsl / main` | **702** | 13 | 689 |
| `VehiclePBR_Modern.hlsl / main_building` | **653** | 12 | 641 |
| `HeightFog.hlsl / main` (current) | 31 | 2 | 29 |
| `Water_Underwater.hlsl / main` (current) | 53 | 2 | 51 |

`ps_3_0`'s minimum `MaxPixelShader30InstructionSlots` is 512. The shipped PBR shaders
already exceed that floor on this toolchain (the project is evidently running on devices
with a higher reported cap, or FXC's “approximately” count includes flow-control slots).
The practical conclusion is still the same: **do not add a new per-pixel term to the PBR
PS**. Both recipes below are small post passes with large headroom versus 512.

Measurement command used for every number in this document:

```bat
fxc /nologo /T ps_3_0 /E main /O3 /I shaders\include /Fc out.asm <shader.hlsl>
```

The modified counts (57 for A with the default lobe folded to `l`, 68 with the `pow`;
89 for B with the safe `tex2Dlod` path) are from standalone copies of the pass shaders
with only the new block added. Re-measure the merged `.hlsl` with `/Fc` on the first
build; the delta, not the absolute count, is the portable number.

---

## 1. Recipe A — height-fog radial sun in-scatter

### 1.1 Source and technique

**CryEngine:** `Engine/Shaders/HWScripts/CryFX/VolumetricFog.cfi:56-79`,
`GetVolumetricFogColorInternal()`. The radial lobe is lines 74-77:

```hlsl
const half l = saturate(length(cameraToWorldPos) * invZFar);
const half radialLobe = pow(l, radialLobeCtrl);
const half radialSize = exp2(dot(normalize(cameraToWorldPos), vfSunDir.xyz)
                            * -radialSizeCtrl + radialSizeCtrl);
fogColor += radialLobe * radialSize * radialColor * radialFogShadowInfluence;
```

This is not a raymarch. It is a closed-form addition to the analytic fog colour: a tight
sun-facing lobe whose amplitude grows with distance (`l`). CE uses it in the per-pixel
fog-colour path; the same function is what `FogPassPS` and every material's fog call
consume.

Constants come from `Code/CryEngine/RenderDll/XRenderD3D9/D3DHWShader.cpp:1415-1447`:

```cpp
// sGetFogColorGradientParams()
Vec3 gradientCtrlParams(0, 0.75f, 0.5f);
const float radialSize = -expf((1.0f - clamp_tpl(gradientCtrlParams.y, 0.0f, 1.0f))
                                * 14.0f) * 1.44269502f;
const float radialLobe = 1.0f / clamp_tpl(gradientCtrlParams.z, 1.0f / 21.0f, 1.0f) - 1.0f;
// sGetFogColorGradientRadial()
return Vec4(fogColorRadial, 1.0f / rc.fFar);
```

With CE's defaults (`radial size = 0.75`, `radial lobe = 0.5`):

- `radialSizeCtrl = -exp((1 - 0.75) * 14) * log2(e) = -47.7754976`
- `radialLobeCtrl = 1 / 0.5 - 1 = 1.0` → `pow(l, 1.0)` is just `l`
- `radialColor` defaults to black; time-of-day/weather sets it. Our kill-switch supplies
  that colour only when enabled.

### 1.2 Exact maths for our port

`HeightFog.hlsl` already reconstructs `viewPos` (camera-to-world in view space) and
`worldZ`. View-space distance equals world distance (rotation preserves length), and the
dot product with a view-space sun direction is basis-invariant. So no full world basis is
needed:

```hlsl
// c6 = { sunDirView.xyz, 1 / cameraFar }
// c7 = { radialColor.rgb, radialLobeCtrl }   (lobe 1.0 by default)
float vlen      = length(viewPos);
float l         = saturate(vlen * sunParams.w);
float cosT      = saturate(dot(viewPos * rsqrt(max(vlen * vlen, 1e-7)),
                               sunParams.xyz));
float radialLobe = pow(l, max(radialColor.w, 1e-3)); // 1.0 -> l
float radialSize = exp2((1.0 - cosT) * -47.7755);    // == exp2(-K * (1 - cosT))
float3 fog       = fogColor.rgb + radialLobe * radialSize * radialColor.rgb;
float3 result    = lerp(scene, fog, totalFog);
```

- **Tap pattern:** none. 0 new texture fetches.
- **ALU:** `length` + `rsqrt` + dot + `exp2` + 2 muls/adds ≈ 16-19 scalar ops before the
  optional `pow`; the `pow` adds ~8-10 ALU. The measured whole-pass delta is +26 slots
  with `radialLobe` folded to `l` (CE default `radialLobeCtrl = 1.0`); keeping the `pow`
  for a configurable lobe costs +11 more slots (57 → 68). Both are trivial.
- **Guards:** normalise `sunD` on the CPU (`1/max(length, 1e-6)`) before mapping to view
  space; `saturate(cosT)` only guards overshoot, not a short sun vector.
  `rsqrt(max(...,1e-7))` prevents zero-length; `exp2` underflows to 0 away from the sun,
  which is safe. On the CPU, zero `radialColor` when `sunDir.z <= 0.02` so the lobe cannot
  glow below the horizon at night.
- **Placement:** replace the final `lerp(scene, fogColor.rgb, totalFog)` at
  `shaders/ps/HeightFog.hlsl:68` with the `fog` vector above. The block goes after the
  `totalFog = min(...)` line 65.

### 1.3 Our files and upload site

| Item | Exact site |
|---|---|
| Shader uniforms | `shaders/ps/HeightFog.hlsl`, after line 14: `uniform float4 sunParams : register(c6);` and `uniform float4 radialColor : register(c7);` |
| Shader body | `shaders/ps/HeightFog.hlsl`, after line 65; change line 68 to blend `fog`, not `fogColor.rgb` |
| C++ upload | `src/render/postfx.cpp`, `DrawHeightFog()` at line 4249. After the c5 upload at line 4425, add c6/c7 uploads. `camLTM` is currently fetched inside the c4/c5 `if(camFrame)` blocks (lines 4410/4420); hoist one `RwMatrix *camLTM = camFrame ? RwFrameGetLTM(camFrame) : NULL;` for the c6/c7 upload. `tc` (`CColourSet`) is in scope from line 4308 |
| Sun direction | `GetSunDirection()` declared at `src/rw/gta.h:318`, implemented at `src/rw/gta.cpp:49`; map world→view with `camLTM->right/up/at`, exactly as `postfx_underwater.cpp:425-428` already does |
| Config struct | `src/skygfx.h`, after `heightFogTimecycleScale` at line 692: `RwBool heightFogSunScatter; float heightFogSunScatterIntensity;` |
| INI read | `src/core/main.cpp:6101-6104` height-fog block; add two reads + clamp (intensity 0..1) |
| INI default-inject | `src/core/main.cpp`, near the water `ADD_IF_MISSING` block at line 6549 |
| INI save | `src/core/main.cpp:6890-6897` height-fog save block |
| Default INI | `skygfx.default.ini:214-220` height-fog block |

The `DrawHeightFog` pass already snapshots and restores **c0-c7** (`float savedPSConsts[4 * 8]`
at `postfx.cpp:4337`, get at `:4350`, restore at `:4441`), so c6/c7 need no save-range
change. That is a deliberate reason to use c6/c7 instead of new c8+ constants.

Upload sketch:

```cpp
RwMatrix *camLTM = camFrame ? RwFrameGetLTM(camFrame) : NULL;
if(camLTM){
    float sunD[3];
    GetSunDirection(sunD[0], sunD[1], sunD[2]);
    float invSun = 1.0f / max(sqrt(sunD[0]*sunD[0] + sunD[1]*sunD[1]
                                 + sunD[2]*sunD[2]), 1e-6f);
    sunD[0] *= invSun; sunD[1] *= invSun; sunD[2] *= invSun;
    float sunView[4] = {
        sunD[0] * camLTM->right.x + sunD[1] * camLTM->right.y + sunD[2] * camLTM->right.z,
        sunD[0] * camLTM->up.x    + sunD[1] * camLTM->up.y    + sunD[2] * camLTM->up.z,
        sunD[0] * camLTM->at.x    + sunD[1] * camLTM->at.y    + sunD[2] * camLTM->at.z,
        1.0f / max(cam->farPlane, 1.0f)
    };
    RwD3D9SetPixelShaderConstant(6, sunView, 1);

    float glow = config->heightFogSunScatter ? config->heightFogSunScatterIntensity : 0.0f;
    if(sunD[2] <= 0.02f) glow = 0.0f;      // no ground glow at night
    float radial[4] = {
        (tc.sunCoreR / 255.0f) * glow,
        (tc.sunCoreG / 255.0f) * glow,
        (tc.sunCoreB / 255.0f) * glow,
        1.0f                                 // radialLobeCtrl (CE default -> 1.0)
    };
    RwD3D9SetPixelShaderConstant(7, radial, 1);
}
```

`sunCore` is the timecycle sun-disc colour (`src/rw/gta.h:305`); `directionalR/G/B` is a
softer alternative if the glow should match the light rather than the disc. If `camLTM`
is unexpectedly null, upload `c7 = {0,0,0,1}` (or return before the draw) so stale
constants cannot enable the lobe.

### 1.4 Slot estimate

| Shader | Current | With recipe | Delta | Headroom vs 512 |
|---|---:|---:|---:|---:|
| `HeightFog.hlsl / main` | 31 (2 tex, 29 alu) | **57** (2 tex, 55 alu; 68 with the `pow` lobe) | +26 (+37 with `pow`) | **455** (444 with `pow`) |
| `VehiclePBR_Modern / main` | 702 | — | — | −190 (do not add) |
| `VehiclePBR_Modern / main_building` | 653 | — | — | −141 (do not add) |

The pass stays at 11.1% of the 512 minimum. This is one of the cheapest possible
per-pixel atmosphere additions.

### 1.5 Texture asset

**None.** This recipe is pure ALU and uses no EngineAssets file. The sun direction comes
from the timecycle (`GetSunDirection`) and the colour from `CColourSet`.

### 1.6 INI kill-switch

| Key | Default | Meaning |
|---|---:|---|
| `heightFogSunScatter` | **0** | master switch; 0 = exact current output |
| `heightFogSunScatterIntensity` | 0.5 | radial colour scale when master = 1; clamp 0..1 |

Default OFF; no preset should enable it until the acceptance test is signed off. It
requires `heightFogEnable=1` (it is the height-fog pass), just as `waterCaustics` requires
`waterUnderwaterFog>0`.

### 1.7 Acceptance test

Foggy dawn/dusk, `heightFogEnable=1`, density 0.002-0.004, camera aimed within ~15° of
the sun at 5-15° elevation.

- **A (`heightFogSunScatter=0`)** vs **B (`=1`)**: B shows a tight warm halo in the fog
  around the sun; fog away from the sun is unchanged; the opposite horizon does not
  brighten.
- Rotate the camera: the halo must stay centred on the sun's screen position, not drift
  with the camera. If it drifts, the world→view sun mapping is wrong.
- Look away from the sun: B should be pixel-identical to A (within 1-2 LSB). If the whole
  frame lifts, `radialColor` is not being gated or is too large.
- Night/overcast: no glow (CPU `sunD.z` gate + `sunCore` near zero).

### 1.8 Risk

- **Post-tonemap clipping.** `HeightFog` runs after the colour filter/tonemap, so the
  radial colour is in display space. Keep `heightFogSunScatterIntensity <= 0.75` and
  `radialColor <= 1`; otherwise the fog + sun sum clips to white. This is the main
  difference from CE, which adds it in HDR before tonemap.
- **Double sun.** The game's sun sprite, corona and our god rays already draw around the
  sun. The lobe must be broad enough to read as fog, not as a second corona; start at
  0.5 and compare at dawn/dusk.
- **Basis error.** A wrong view-space sun vector makes the halo orbit as the camera
  rotates. The acceptance rotation test catches it immediately.
- **No new state risk.** c6/c7 are inside the existing c0-c7 save/restore window; no new
  sampler, no new RT, no new pass.

---

## 2. Recipe B — projected underwater caustics

### 2.1 Source and technique

**CryEngine:** `Engine/Shaders/HWScripts/CryFX/DeferredCaustics.cfx:85-178`,
`DeferredCasticsPS()` (the typo is in the source). The texture is declared at lines 41-47:

```hlsl
sampler2D causticsSampler = sampler_state
{
  Texture = EngineAssets/Textures/caustics_sampler.dds;
  MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = NONE;
  AddressU = Clamp; AddressV = Clamp;
};
```

The CE pixel shader projects a world position through a light-space matrix, distorts the
projection with five `oceanwaves_ddn` normal-map taps, samples the caustic texture at
three refracted offsets (R/G/B dispersion), sharpens the pattern, and attenuates by water
height and distance.

**What we already ship is not this.** `WaterUnderShimmer()`
(`shaders/ps/Water_Parallax.hlsl:692-702`) is a subtle shimmer sampled where the eye's
refracted ray crosses the *water surface*, viewed from below. It never lands on submerged
geometry. The parity matrix's `Caustics` row is therefore still MISSING for projected
caustics on the sea bed/pool tiles.

### 2.2 Exact CE maths (reference) and our reduction

CE's core (line references in `DeferredCaustics.cfx`):

```hlsl
float4 animCoeffs = float4(2.0*0.01*0.012, 2.0*0.01*0.01, 0.01*0.5, 0.01*2.0);
// = (0.00024, 0.0002, 0.005, 0.02)
float4 waveTC = vProj.xyxy * animCoeffs.xxyy + vAnimParams.xxyy;
float4 temp   = vProj.xyxy * animCoeffs.zzww;
float4 causticTC0 = temp.xyyx + vAnimParams.zzww;
float4 causticTC1 = temp.zwwz + vAnimParams.zzww;

wave  = (GetXYNormalMap(wavesSampler, waveTC.xy).xy
       + GetXYNormalMap(wavesSampler, waveTC.wz).xy) * 0.02;
causticMapR.xy  = GetXYNormalMap(wavesSampler, causticTC0.xy + wave)
                + GetXYNormalMap(wavesSampler, causticTC0.wz + wave);
causticHighFreq = GetXYNormalMap(wavesSampler, causticTC1.xy + wave)
                + GetXYNormalMap(wavesSampler, causticTC1.wz + wave);
causticMapR.xy = causticHighFreq + causticMapR.xy * 0.5 + vNormal.xy * 0.25;
normalVec = normalize(causticMapR);

fAtten  = (1 - exp(-fHeightAtten.y * 2.0));
belowWater = step(0, fHeightAtten.x);
fAtten *= max(0.25 - vNormal.z, belowWater);
fAtten  = saturate(fAtten) * saturate(vCausticParams.x / (0.075 * fDistToCamSq));

vRefrR = refract(vSunTS.xyz, normalVec.xyz, 0.9).xy;
vRefrG = vRefrR - normalVec.xy * 0.05;
vRefrB = vRefrR - normalVec.xy * 0.10;
cCaustic = float3(tex2D(causticsSampler, vRefrR.xy*0.5+0.5).x,
                  tex2D(causticsSampler, vRefrG.xy*0.5+0.5).x,
                  tex2D(causticsSampler, vRefrB.xy*0.5+0.5).x);
cCaustic = saturate(5.65 * cCaustic - 4.66);
cCaustic *= cCaustic;
cCaustic = saturate(dot(normalVec.xyz, CausticSmoothSunDir.xyz))
         * cCaustic * g_PS_SunColor * 0.5;
OUT.Color.xyz = cCaustic * vCausticParams.y * PS_HDR_RANGE_ADAPT_MAX * fAtten;
```

That is **5 wave taps + 3 caustic taps = 8 texture fetches**, plus `refract()` and
`normalize()`. It is still affordable, but it also needs the wave normal map whose
CE filename (`oceanwaves_ddn.dds`) does not exist in this export (only `.tif` does), and
it uses a light-space matrix we do not have in the underwater pass.

**Our reduction:** drop the wave taps, the light-space matrix, and the three refracted
R/G/B taps; keep the caustic texture, the CE sharpen constants, the depth attenuation,
and the sun tint. Two scrolling layers of the caustic texture replace the wave distortion
and the RGB dispersion. Our underwater pass already has depth, camera basis, water level
and sun tint.

```hlsl
// c8 = { timeSeconds, worldScale, intensity, depthFalloff }
// c9 = { camRight.xy, camUp.xy }
// c10 = { camAt.xy, 0, 0 }
float2 wpXZ = camPos.xz
            + viewPos.x * c9.xy
            + viewPos.y * c9.zw
            + viewPos.z * c10.xy;
float2 cuv  = wpXZ * c8.y + float2(0.031, -0.023) * c8.x;
float lod   = log2(max(1.0, linearDepth * 0.04));   // tex2Dlod: no undefined gradients
float ca    = tex2Dlod(causticTex, float4(cuv, 0, lod)).x;
float cb    = tex2Dlod(causticTex,
                float4(cuv * 1.37 + float2(-0.017, 0.029) * c8.x, 0, lod)).x;
float c     = ca * 0.65 + cb * 0.35;
c = saturate(5.65 * c - 4.66);                       // CE :166
c *= c;
float att   = (1 - exp(-pixDepth * c8.w))            // CE :135 (2.0)
            * saturate(1 - linearDepth / 40.0);      // our distance fade
float3 caustic = sssColour.rgb * c * att * c8.z;
result = lerp(scene + caustic, water, fog);
```

- **Tap pattern:** 2 caustic taps (vs CE's 8). `caustics_sampler.dds` only; the wave TIF
  is not used.
- **Constants:** `5.65` / `-4.66` (CE sharpen), `2.0` (CE depth attenuation), `0.65/0.35`
  layer weights, `1.37` second-layer scale, `0.031/-0.023` and `-0.017/0.029` UV/s,
  `40 m` distance fade. `worldScale` default `0.08` ≈ 12.5 m per 128² tile.
- **`tex2Dlod`, not `tex2D`:** the taps live inside the depth `if`; `tex2D` inside a
  divergent branch has undefined derivatives (the project's own X3526 doctrine). The
  explicit `lod` selects coarser mips with distance (anti-shimmer); the separate `att`
  term fades with distance.
- **Placement:** add the caustic to `scene` **before** the fog blend at
  `Water_Underwater.hlsl:122`, so water fog attenuates it exactly the way CE's fog pass
  does. Do not add it after the blend.

### 2.3 Our files and upload site

| Item | Exact site |
|---|---|
| Shader uniforms | `shaders/ps/Water_Underwater.hlsl`, after line 40: `sampler2D causticTex : register(s2);` and `float4 causticParams : register(c8); float4 camRightUp : register(c9); float4 camAt : register(c10);` |
| Shader body | `shaders/ps/Water_Underwater.hlsl`, inside the `linearDepth > 0` block, after line 116; add `caustic` and feed it into the blend at line 122 |
| C++ upload | `src/render/postfx_underwater.cpp`, `PostFX_UnderwaterTick()` at line 310. After the c7 upload at line 495, upload c8/c9/c10 |
| Sampler bind | `postfx_underwater.cpp:454` binds s1; add `dev->SetTexture(2, g_causticTex)` and LINEAR/WRAP sampler states next to it |
| Constant save/restore | `postfx_underwater.cpp:412` `savedPSConsts[4*8]` → `[4*11]`; `GetPixelShaderConstantF(..., 11)` at `:441`; restore `11` at lines 506 and 532 |
| Cleanup | `postfx_underwater.cpp:509` and the `__except` at `:533`: `dev->SetTexture(2, NULL)` before `DepthHook_Restore()` |
| Texture load | Extend `UWLoadShader()` at `postfx_underwater.cpp:283` with `UWLoadCausticTexture()`: `FindResource(dllModule, MAKEINTRESOURCE(IDR_CAUSTICS_TEX), RT_RCDATA)` → `LoadResource` → `LockResource` → `D3DXCreateTextureFromFileInMemoryEx(...)` (full mip chain, `D3DX_FILTER_BOX`) or `D3DXCreateTextureFromFileInMemory` for the 1-mip fallback; add `#include <d3dx9.h>`; `d3dx9.lib` is already linked (`premake5.lua:368`) |
| Resource ID | `resources/resource.h`: append `#define IDR_CAUSTICS_TEX 286` after `IDR_IVGRADEPS 285` (line 150). 280 is reserved, 281-282 are earmarked by the noise lane, 283-285 are taken; 286 avoids all of them |
| Resource payload | `resources/resource.rc`: `IDR_CAUSTICS_TEX RCDATA "textures/caustics_sampler.dds"`; copy the CE asset to `resources/textures/caustics_sampler.dds` |
| Config struct | `src/skygfx.h`, after `waterUnderwaterFog` at line 557: `RwBool waterCaustics; float waterCausticsIntensity;` |
| INI read | `src/core/main.cpp:6203` water block; add two reads + clamps |
| INI default-inject | `src/core/main.cpp:6549` `ADD_IF_MISSING` water block |
| INI save | `src/core/main.cpp:6960` water save block |
| Default INI | `skygfx.default.ini:277` water block |

Upload sketch:

```cpp
// causticTimeSeconds: (GetTickCount() & 0x7FFFFFFF) * 0.001f — the same
// pattern as pipelinecommon.cpp:2091 — or a frame-accumulated clock.
float causticIntensity = config->waterCaustics ? config->waterCausticsIntensity : 0.0f;
float c8[4] = { causticTimeSeconds, 0.08f, causticIntensity, 2.0f };
RwD3D9SetPixelShaderConstant(8, c8, 1);
float c9[4] = { camLTM->right.x, camLTM->right.y,
                camLTM->up.x,    camLTM->up.y };
RwD3D9SetPixelShaderConstant(9, c9, 1);
float c10[4] = { camLTM->at.x, camLTM->at.y, 0.0f, 0.0f };
RwD3D9SetPixelShaderConstant(10, c10, 1);
```

If `waterCaustics` is off, upload `causticIntensity = 0` but still bind a valid s2 (the
loaded texture, or a 1×1 black fallback). Never leave s2 unbound while the shader samples
it.

### 2.4 Slot estimate

| Shader | Current | With recipe | Delta | Headroom vs 512 |
|---|---:|---:|---:|---:|
| `Water_Underwater.hlsl / main` | 53 (2 tex, 51 alu) | **89** (6 tex, 83 alu) | +36 | **423** |
| `VehiclePBR_Modern / main` | 702 | — | — | −190 (do not add) |
| `VehiclePBR_Modern / main_building` | 653 | — | — | −141 (do not add) |

The pass stays at 17.4% of the 512 minimum: 2 extra source-level taps, ~32 extra ALU, no
loop. FXC reports **6** texture instructions for 2 `texld` + 2 explicit-LOD `texldl` ops
(its tally charges the LOD form extra); the safe `tex2Dlod` path is the 89-slot number. A
`tex2D` version measures 73 slots but is **not** shippable inside the depth `if`
(undefined derivatives at the sky/geometry boundary).

### 2.5 Texture asset (verified by header, not filename)

**Reused: `EngineAssets/Textures/caustics_sampler.dds`**

| Field | Parsed value |
|---|---|
| Absolute path | `E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\EngineAssets\Textures\caustics_sampler.dds` |
| File size | 65,664 bytes |
| Magic | `DDS ` |
| Dimensions | **128 × 128** |
| Format | **A8R8G8B8**, 32 bpp, uncompressed (`FourCC = 0`, `pf_flags = 0x41` = RGB + alpha) |
| Channel masks | R `0x00ff0000`, G `0x0000ff00`, B `0x000000ff`, A `0xff000000` |
| Mips | header count 0 → **1 mip** (65,664 = 128·128·4 + 128) |
| Content | grayscale caustic pattern; CE samples `.x` (red) |
| Tiling | **tileable**: left/right and top/bottom edge deltas ≤ 1 LSB (measured) |

Because the asset tiles, our scrolling UVs can use `D3DTADDRESS_WRAP` at s2. CE uses
`CLAMP` because it projects within a finite region; our scrolling needs `WRAP`, and the
asset is tileable (verified). Create the texture as **linear** A8R8G8B8 (no sRGB read);
CE samples it linearly. If `tex2Dlod` will be used
at LOD > 0, ask D3DX for a full mip chain (`D3DXCreateTextureFromFileInMemoryEx`,
`D3DX_DEFAULT` mips, `D3DX_FILTER_BOX`); the source DDS itself has 1 mip.

**Deliberately not reused: `EngineAssets/Textures/oceanwaves_ddn.tif`**

| Field | Parsed value |
|---|---|
| Path | `...\EngineAssets\Textures\oceanwaves_ddn.tif` |
| Dimensions / format | **256 × 256**, 24-bit RGB TIFF (PIL: RGB; 197,114 bytes) |
| Shader reference | `DeferredCaustics.cfx:33` asks for `oceanwaves_ddn.dds` — that file does not exist in this export |
| Verdict | Not used. D3DX9 cannot load TIFF; the image is not tileable (left/right edge delta mean ≈ 67 summed over RGB, max 219); using it would add 5 taps and a conversion step. The second scrolling caustic layer replaces its job. |

### 2.6 INI kill-switch

| Key | Default | Meaning |
|---|---:|---|
| `waterCaustics` | **0** | master switch; 0 = current underwater pass output |
| `waterCausticsIntensity` | 0.6 | caustic strength when master = 1; clamp 0..1 |

Default OFF. The pass still requires `waterUnderwaterFog > 0` (it is an addition to that
pass). If independent caustics without underwater fog are wanted later, change the gate at
`postfx_underwater.cpp:319` to `if(!(strength > 0.0f) && !config->waterCaustics) return;`
and keep `density = 0` in the fog term.

### 2.7 Acceptance test

Submerge the camera in clear shallow water (sea bed or pool), `waterUnderwaterFog=1`.

- **A (`waterCaustics=0`)** vs **B (`=1`)**: B shows moving bright caustic filaments on
  the sea bed, pool tiles, submerged rocks and the player's legs; the water surface and
  anything above water are unchanged.
- Animate time: the pattern crawls, it does not strobe. Two layers should not visibly
  pulse in sync.
- Rotate the camera in place: the pattern must stay glued to the geometry (world-XZ
  projection). If it slides across the sea bed with rotation, the c9/c10 camera-basis
  upload is wrong.
- Swim close to a vertical wall: some stretching is expected (planar XZ projection); note
  it as a known limitation, not a blocker.
- Disable `waterUnderwaterFog`: caustics disappear with the pass; document this dependency.

### 2.8 Risk

- **Texture load failure.** If `IDR_CAUSTICS_TEX` is missing or D3DX rejects the DDS,
  disable the feature and bind a 1×1 black fallback. Do not sample an unbound s2.
- **State leak.** `Water_Underwater` shares the postfx constant file; expanding the saved
  range from c0-c7 to c0-c10 is mandatory. Restore in both the normal path
  (`:506`) and the `__except` path (`:532`), and unbind s2 before `DepthHook_Restore()`.
- **Gradient correctness.** `tex2D` inside the depth `if` would have undefined
  derivatives at the sky/geometry boundary. Use `tex2Dlod` exactly as written.
- **Aliasing.** The source is 128² with 1 mip. The distance fade plus a generated mip
  chain hides most shimmer; if it still crawls, lower `waterCausticsIntensity` or add a
  third layer at a different scale (one more tap).
- **Planar projection.** World-XZ projection stretches on vertical surfaces. CE's
  light-space projection handles all orientations but needs a matrix we do not have.
  Accept for v1; a normal-blended tri-planar version is a later 3× tap upgrade.
- **Double effect.** Keep caustics on submerged geometry only; do not add them to the
  water surface. `WaterUnderShimmer` remains the surface effect.

---

## 3. Slot ledger

### 3.1 Global world-shader ledger (unchanged by both recipes)

| Slot(s) | Owner | Status after these recipes |
|---|---|---|
| `s0-s4` | diffuse / env / mask / IBL cubemap / screen normal | occupied |
| `s5-s7` | Forward+ tile index / light-pos atlas / light-colour atlas | occupied |
| `s8` | normal map | occupied |
| `s9` | universal noise | **frozen, untouched** |
| `s10` | — | **free, untouched** |
| `c0-c4` | surfProps / fxParams / eyePos / iblParams / cloudShadow | occupied |
| `c5-c18` | 7 direct light colours + directions | occupied |
| `c19` | matCol | occupied |
| `c20` | iblAmbient | occupied |
| `c22-c23` | PBR uploads | **frozen, untouched** (`pipeUploadPBR`) |
| `c24` | ambient | occupied |
| `c25-c28` | view basis / sky params | occupied |
| `c29` | screen size | occupied |
| `c30-c36` | day-night, texture transform, eye position | occupied |
| `c37-c43` | colour-space grading | occupied |
| `c44-c48` | chrome / FP cluster / layer mask / building env / nm params | occupied |
| `c49-c52` | universal noise | **frozen, untouched** |
| `c53` | — | **free, untouched** |

**Next free world slots remain `s10` and `c53`.** Neither recipe consumes them; Recipe A
uses c6/c7 of the `HeightFog` post-pass namespace, Recipe B uses s2/c8-c10 of the
`Water_Underwater` post-pass namespace. `c22`/`c23` remain reserved for
`pipeUploadPBR()` and are not touched by either recipe.

### 3.2 Post-pass namespaces (separate from the world ledger)

| Pass | Samplers | Constants | New in this plan |
|---|---|---|---|
| `HeightFog` (Recipe A) | s0 scene, s1 depth; s2 free | c0-c5 used; **c6, c7 new**; c8+ free | c6 = `{sunDirView.xyz, 1/far}`; c7 = `{radialColor.rgb, lobe}` |
| `Water_Underwater` (Recipe B) | s0 scene, s1 depth, **s2 caustics**, s3 free | c0-c7 used; **c8, c9, c10 new**; c11+ free | s2 = caustics; c8 = time/scale/intensity/falloff; c9/c10 = camera basis XY |
| SMAA / SSAO / DOF / bloom / CA / grain / vignette | own namespaces | own c0-cN | untouched |

Runtime constant save/restore:

- `HeightFog`: already saves c0-c7 (`savedPSConsts[4*8]` at `postfx.cpp:4337`); **no change**.
- `Water_Underwater`: currently saves c0-c7; **expand to c0-c10** at
  `postfx_underwater.cpp:412`, `:441`, `:506`, `:532`.

### 3.3 Texture ledger

| Texture | Source | Format | Used by |
|---|---|---|---|
| `caustics_sampler.dds` | `EngineAssets/Textures/caustics_sampler.dds` → `resources/textures/caustics_sampler.dds` | 128×128 A8R8G8B8, 1 mip, 65,664 B, tileable | Recipe B, s2 |
| *(none)* | — | — | Recipe A |

---

## 4. Rejected candidates

### 4.1 Five candidates considered and rejected

| # | Candidate | CryEngine source | Why rejected |
|---|---|---|---|
| 1 | **HBAO / HMAO** | `ShadowBlur.cfx:451` `HMAO_Blur` + the horizon search in the deferred AO path | 4-8 directions × 4-8 depth steps = 16-64 taps; a depth-dependent dynamic loop; ~90-120 slots (matrix estimate). It improves an AO we already ship, but its gain/slot and gain/LOC are far worse than 2-tap caustics or 0-tap fog sun. |
| 2 | **SSDO** | `DeferredShading.cfx:1182` `SSDOFetchDepths`, `:1244-1302` SSDO taps | 8 directions × 4 taps + normal/colour buffers; ~60-80 slots added to the existing AO resolve (matrix estimate), and it wants the deferred G-buffer (depth + normals + colour) we do not have in one pass. We have a normal buffer, but the cost/benefit loses to caustics. |
| 3 | **General SSR** | `DeferredShading.cfx:1088` `SSR_Raytrace`, `:1123` `SSReflection_Comp` | 24-step depth ray march × 2 taps = 48+ taps; ~110-150 slots (matrix estimate); dynamic hit loop; if forced unrolled it exceeds 512; high risk of smearing/ghosting. Already ranked P0 in the matrix, but it is not a “cheap” pick. |
| 4 | **Specular AA (normal-variance roughness)** | `DeferredShading.cfx:899-960` `FilterGBufferPS` (kappa/variance roughness remap) | 4 extra normal taps + 4 depth taps (SM3.0 has no `Gather`) + ~20 ALU. It would have to go into the PBR PS, which FXC measures at **702** (`main`) and **653** (`main_building`) slots — already over the 512 floor. A separate pass would need a gloss/roughness buffer we do not have. |
| 5 | **Sharpening** | `PostEffects.cfx:1178-1203` `CA_SharpeningPS` | 4-5 taps, ~15-20 slots; genuinely cheap, but it is a subjective contrast operation, amplifies grain/aliasing, and overlaps driver/ReShade sharpening. Lower scene-lighting gain per LOC than the fog sun or caustics. |

### 4.2 Hard blockers and structural rejects (loops, MRT, compute, tessellation, history)

| Effect | Source | Hard blocker | Why it cannot ship as an SM3.0 port |
|---|---|---|---|
| CSM / 4-cascade shadow maps | `ShadowMaskGen.cfx:709` `DeferredShadowPass`; `ShadowCommon.cfi:406-441` cascade select | No shadow-map depth exists; no hardware PCF; 4 cascades × 9 PCF taps; MRT/shadow-map infrastructure | Would replace SA's stencil shadow-volume system wholesale. Even a single cascade with manual PCF is 9 taps; if generalized to our 7 direct lights it is 63 taps/cascade, far over 512, and it needs render targets we do not own. |
| Volumetric clouds (`CloudsLayerPS`) | `Clouds.cfx:917` `CloudsLayerPS`, march around `:1035-1208`; real path uses `InjectCloudDensityAndShadowCS` at `:1697` | 24 steps × 3 taps = 72 fetches; the cloud reference budgets ~48 slots/step; 3D noise/shadow volume; compute for the volume path | Unrolled 24× is >1000 slots (the cloud doc's 16× estimate alone is ~890); a dynamic `[loop]` is legal but the real CE path still needs CS-filled 3D textures. The cloud lane already owns the feasible substitute. |
| GPU particles | `GpuParticles.cfx:53-651` (bitonic sort, bounds reduction, feature update) | Compute/UAV/atomics | D3D9 has no compute, UAV or atomics. A CPU reimplementation is a different project. |
| LPV GI | `LightPropagationVolumes.cfx:290` `LPVPropagate`, `:526` `LPVApply` | 3D volume propagation; RSM inject; additive 3D volume blending | No 3D render targets with blending in D3D9. A ping-pong 2D atlas is possible but is a project, not a cheap port. |
| SVOGI | `Total_Illumination.cfx:2171` `ConeTracePass` | Voxelization + cone tracing + UAV/3D texture | No compute/UAV/3D RT; 32+ cone taps per pixel. |
| Deferred shading / G-buffer | `DeferredShading.cfx` | MRT 4 RTs, no per-RT blend, depth cannot be sampled while bound | SA is forward-rendered through RW 3.6; converting is a renderer rewrite, and MRT(4)+depth-copy+7-light deferred does not fit SM3.0. |
| Water / vegetation / terrain tessellation | `CommonTessellation.cfi`, `Water.cfx`, `Vegetation.cfx`, `Terrain.cfx` | Hardware HS/DS | D3D9 has no tessellation stages. Vertex-texture displacement can fake height, not patch subdivision. |
| MSAA post composites | `PostAA.cfx:1262` `PostAAComposites`, `:1190` `MsaaDebug` | Requires an MSAA depth resolve | We render non-MSAA at native resolution with an INTZ copy; there is nothing to resolve. |
| Screen-space rain / snow | `DeferredRain.cfx`, `DeferredSnow.cfx` | Multi-pass rain-occlusion + normal/depth; deferred normals | The game already owns weather particles; the CE passes assume deferred normals and a rain-occlusion volume. Not cheap. |
| Screen frost / lens droplets | `PostEffectsGame.cfx:342-455` (frost), `:144-233` (droplets) | 5 taps + 2 textures; gameplay overlay | SA already ships `neoWaterDrops`; there is no frost/cold state, and the effect is a full-screen gameplay overlay, not a scene-lighting upgrade. The frost DDS/TIF assets in `EngineAssets/Textures/Frozen/` are therefore not reused. |
| Distance clouds, 48-tap tier | `DistanceClouds.cfx:289` advanced path | 48 unrolled taps × ~8 slots ≈ 384 + prologue | Borderline 512 and needs a height texture; the 16-tap tier is the only safe one. The cloud lane owns this row. |
| Contact shadows | not present in CE 3.8.6 | — | It is a CE 5.x effect. Implementing an 8-tap depth ray would be an invention, not a 3.8.6 port. |
| TAA | `PostAA.cfx:23` `mReprojection`, `:555` reproject | 9-13 taps + history + previous VP + neighbourhood clamp | Not a loop-limit failure, but a persistent history/velocity pipeline with ghosting risk; far more LOC than the two picks. Already P0 in the matrix. |
| Full CE 8-tap caustics | `DeferredCaustics.cfx:85-178` | 5 wave taps + 3 caustic taps + `refract` + light-space matrix | It is affordable, but the wave texture referenced by the shader does not exist in this export and it costs 8 taps. We keep the caustic texture and reduce to 2 taps. |

### 4.3 Not counted (already specified elsewhere)

- **Height-fog closed-form integral** (`FogVolume.cfx:173-193`, `VolumetricFog.cfi:17-50`):
  the cloud reference §5.2 already calls this “the single highest-value change” and gives
  the recipe. This document does not duplicate it; Recipe A is the missing sun term, not
  the integral.
- **Water crest foam / soft intersection** (matrix top-8 #8): already assigned.
- **Sky analytic phase** (cloud reference §5.2): already assigned to `DynamicSky`.

---

## 5. Recommended order

**1. Recipe A (fog sun) — first.**
- Zero new assets, zero new samplers, zero new RTs.
- c6/c7 are already inside the pass's c0-c7 save/restore window; the C++ change is two
  constant uploads.
- It can be validated in any foggy scene without submerging the camera, and the rotation
  test immediately proves the sun basis.
- If it is wrong, the failure is isolated to a 10-line block and cannot affect water or
  the underwater pass.

**2. Recipe B (caustics) — second.**
- Adds an embedded 64 KB texture, a new RCDATA ID, a texture loader, s2, and c8-c10;
  more moving parts and a mandatory save-range change.
- Needs an underwater test scene and the camera-basis rotation test.
- Do it after A so the INI/config plumbing pattern is already proven and a texture-load
  failure cannot mask a fog regression.

If the immediate goal is a demo screenshot rather than engineering order, B has the bigger
single-frame “wow”; but the dependency/risk order is A then B.

---

## 6. Verification / open questions

1. **Re-measure after merge.** The 57/68 (A) and 89 (B) counts are standalone-copy
   measurements. Run `fxc /Fc` on the merged `HeightFog.hlsl` and `Water_Underwater.hlsl`;
   the deltas should stay within ±4 slots. If either crosses 512, do not ship it.
2. **Device cap.** The project already ships a 702-slot PBR PS. Confirm the target
   hardware's `D3DCAPS9.MaxPixelShader30InstructionSlots` is ≥ 702 (it must be, or the
   current build would not run). The new passes are safe even on a 512-cap device.
3. **Caustics dependency.** Decide whether `waterCaustics=1` should work with
   `waterUnderwaterFog=0`. The current plan says no (it is an addition to the underwater
   pass); the one-line gate change in §2.6 makes it independent.
4. **Texture shipping.** `caustics_sampler.dds` comes from the local CE reference tree.
   Confirm the asset is cleared for redistribution before embedding it in `Resource.rc`;
   if not, generate a tileable caustic pattern procedurally at build time and keep the
   same 128² A8R8G8B8 contract.
5. **Menu exposure.** Both masters are INI-only for the first build. Add rows to
   `debugmenu_ui.cpp` only after the acceptance screenshots pass; the existing height-fog
   and water groups are at `:997`/`:1194` and `:1335`/`:1360`.
6. **Fog sun colour.** `sunCore` gives a disc-coloured glow; `directional` gives a
   softer light-coloured glow. Pick one in the acceptance pass; do not mix both.

---

### Appendix — exact reference lines used

| What | File:lines |
|---|---|
| CE radial fog lobe | `Engine/Shaders/HWScripts/CryFX/VolumetricFog.cfi:56-79` (formula 74-77) |
| CE radial constants | `Code/CryEngine/RenderDll/XRenderD3D9/D3DHWShader.cpp:1415-1447` |
| CE caustic pixel shader | `Engine/Shaders/HWScripts/CryFX/DeferredCaustics.cfx:85-178` |
| CE caustic texture declaration | `DeferredCaustics.cfx:41-47` |
| CE caustic technique | `DeferredCaustics.cfx:183-191` |
| CE wave texture reference (missing `.dds`) | `DeferredCaustics.cfx:31-39` (line 33 says `oceanwaves_ddn.dds`) |
| Our height fog | `shaders/ps/HeightFog.hlsl:1-70`; blend at `:68` |
| Our height-fog C++ | `src/render/postfx.cpp:4249` (`DrawHeightFog`), c5 at `:4425`, save c0-c7 at `:4337`/`:4441` |
| Our underwater PS | `shaders/ps/Water_Underwater.hlsl:30-128`; blend at `:122` |
| Our underwater C++ | `src/render/postfx_underwater.cpp:310` (`PostFX_UnderwaterTick`), c7 at `:495`, save at `:412`/`:441`/`:506`/`:532` |
| Our frozen PBR slots | `shaders/ps/VehiclePBR_Modern.hlsl:39-55` (`c22`/`c23`/`c48`), `src/render/pipelinecommon.cpp:899` (`pipeUploadPBR`) |
| Our noise contract | `src/render/noise.cpp:897` (`BindAndUpload`), `shaders/include/Noise.hlsl:42-46` (`s9`, `c49-c52`) |
| Texture header check | `caustics_sampler.dds` parsed directly: `DDS `, 128×128, A8R8G8B8, masks `00ff0000/0000ff00/000000ff/ff000000`, 1 mip, 65,664 B |
| TIFF check | `oceanwaves_ddn.tif` parsed with PIL: 256×256, RGB, 24 bpp, 197,114 B; not tileable (edge delta mean ≈ 67 summed over RGB) |
