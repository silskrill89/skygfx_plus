# Water tier-0 diagnosis — flat teal / blown shallow / green underwater

Date: 2026-10-24  
Scope: `waterStyle=Xbox`, `waterQuality=0`, `waterParallaxEnable=1` (live `skygfx.ini`)  
Read-only diagnosis; no source files were modified.

---

## 0. Verdict (TL;DR)

1. **Tier 0 is not "the same water with fewer effects"; it removes the depth signal the water model is built on.**
   `waterPipe.cpp:966` gates **both** the scene-colour copy **and** the INTZ depth copy behind
   `needRT = (tier >= 2)`. Tier 0/1 therefore have no `frontFade`, no true shallow/deep
   thickness, and the shader falls back to a view-distance estimate
   (`Water_Parallax.hlsl:722-745`, `538-541`). This is the root cause of the flat colour,
   the hard waterline, and the wrong transparency/opacity. The log confirms it:
   `needRT=0 sceneRT=none depthRT=none reflRT=none reflFlag=0`.

2. **The detail normal map is not forced off at tier 0.** It is sampled (`s1`, one tap,
   `Water_Parallax.hlsl:762-765`), but at tier 0 it only perturbs Fresnel and a narrow
   specular lobe. There is **no screen-space reflection and no refraction** to make the
   normals visible. At distance the normal strength fades to 15 %
   (`:780`, `1 - 0.85*distFactor`). This is why the surface reads as flat colour.

3. **The teal is the timecycle water colour, not the INI colour.** With
   `waterUseTimecycle=1` (`waterPipe.cpp:1183-1207`) the surface ignores
   `waterShallow*` / `waterDeep*`; those keys only feed the underwater fog in
   `postfx_underwater.cpp:375-390`. `timecyc.dat` water is
   `(145,170,170,240)` at 7 AM and `(90,170,170,240)` at midday; SA's own
   `*0.65 + 0.27` quirk (`waterPipe.cpp:1194-1196`) and the tier-0 constant
   `deepFactor=0.65` (`Water_Parallax.hlsl:722`) produce roughly
   `(0.58, 0.72, 0.72)` — a pale, desaturated teal.

4. **The near-white shallow/sea-floor is mostly the bright framebuffer behind insufficiently
   opaque water (and/or the separate giant overlay), not a `waterCol*2.0` bug.**
   `WaterSplitBlend` (`Water_Parallax.hlsl:493-504`) saturates its source and the hardware
   blend weights it by alpha. At tier 0 the alpha comes from a **distance estimate**, not
   water depth (`:736`, `:853`), so close shallow water can leave 35–50 % of the bright
   seabed/sky visible. `waterTranslucency` is only `1.0`. The `*2.0` term exists only in the
   **V style tier >= 2** path (`:1253`), not tier 0.

5. **Underwater is a separate, currently enabled pass.** `PostFX_UnderwaterTick` is called
   at `postfx.cpp:1672`; the log shows `[UW] underwater ON` at 20:07:44/46. It fogs the whole
   frame with the **config** `waterDeep* -> waterShallow*` palette, and its SSS in-scatter is
   negligible because the logged `light=(0.08,0.08,0.08)` makes
   `sssScale = 0.16` and the tint contribution ≈ `0.013`.
   The `[PF] FX pre-tonemap group ... gates: 0/0/0` line and `sunShaftsEnable=0` prove the
   global sun-shafts pass is off; `Water_Underwater.hlsl` has no shaft/caustic term (only
   exponential fog + SSS). The tier-0 underside of the surface is an analytic TIR sheet
   (no scene copy), so it is flat green.

6. **The huge translucent wedge is probably the other lane, but the water plane is not
   exonerated.** It has straight water.dat-quad-like edges and appears in the same frames as
   the water draw. In 08:19 it reaches the top of the frame; in 08:53 it starts at the
   horizon and covers the sea. A flat Z-up water plane should stay below the horizon (caveat:
   at high altitude the upper half may itself be sea). The decisive test is
   `waterParallaxEnable=0` (Section 7). Do not chase the overlay lane until that test is done.

7. **`waterStyle=Xbox` is not stock Xbox water.** `docs/plans/water-refs.md:4-6` states there
   is no Xbox shader; the stock Xbox look is `CWaterLevel` + timecyc. This build **replaces**
   the stock draw whenever `waterParallaxEnable=1` (`main.cpp:3914-3946`). Setting
   `waterParallaxEnable=0` falls back to the original draw and is the only true "Xbox"
   setting.

---

## 1. Inputs, build identity, and evidence

### Live INI (`E:\games\gtasa_skygfx_plus\skygfx.ini`)

| Key | Value | Line |
|---|---|---|
| `waterStyle` | `Xbox` | 244 |
| `waterQuality` | `0` | 235 |
| `waterParallaxEnable` | `1` | 233 |
| `waterUseTimecycle` | `1` | 248 |
| `waterNormalStrength` | `1.0` | 232 |
| `waterTileScale` | `1.0` | 245 |
| `waterReflectionStrength` | `0.55` | 236 |
| `waterTranslucency` | `1.0` | 246 |
| `waterSpecularPower` / `Intensity` | `512` / `1.0` | 243 / 242 |
| `waterFresnelSplit` | `1` | 231 |
| `waterBodyClassify` | `1` | 224 |
| `waterShoreFoam` | `1` | 241 |
| `waterUnderwaterFog` | `1.0` | 247 |
| `sunShaftsEnable` | `0` | 186 |
| `godRaysEnable` | `0` | 69 |

Defaults are parsed in `src/core/main.cpp:4847-4932`; `waterQuality` defaults to 3 but is
clamped 0–3 at `:4923-4925`.

### Build / log identity

- `skygfx.asi` mtime **2026-10-24 19:55**; log `skygfx_dbg.prev.log` covers
  **20:01:40 – 20:07:56**. Water first drawn at **20:07:24** (after load).
- Water assets: `resources/cso/Water_Parallax.cso` **2026-10-23 20:59**,
  `Water_IV.cso`/`Water_V.cso` 20:59, `Water_VS.cso` 2026-10-24 19:08.
  Sources `src/render/waterPipe.cpp` and `shaders/ps/Water_Parallax.hlsl` are
  **2026-10-23 20:58**, i.e. the water lane in the log matches the current water code.
- `src/core/main.cpp` is 20:28 (after the 19:55 build), so the log **predates wave-2
  main.cpp edits**; none of the water findings below depend on those edits. The log's
  `body=1806844436` format bug (Section 5.7) also proves the build carries the current
  `waterPipe.cpp` logging line.

### Key log lines (`skygfx_dbg.prev.log`)

| Time | Line |
|---|---|
| 20:02:21.850 | `[PostFX-REPORT] ... filter=8 pipeline=0 pRasFB=04910A38(2048x2048) camRas=0490EFC0(1920x1080) ... SSAO=0 smaa=1 normalBuf=1 pipeChain=1 ...` |
| 20:02:25.371 | `[UW] water.dat: 307 planes (ok=1 bbox -3000..3000 / -3000..3000)` |
| 20:02:25.377 | `[UW] Water_Underwater PS res=276 sh=22670B98` |
| 20:07:23.497 | `[PF] FX pre-tonemap group: SunShafts->DOF->Bloom (gates: 0/0/0) ...` |
| 20:07:24.194 | `[water] entry: enable=1 waterStyle=0 tier=0 verts=72 indices=108 sceneRT=00000000 depthRT=00000000 router=IDR_WATER_PARALLAX` |
| 20:07:24.225 | `[water] detail normal tex 256x256 mips=1` |
| 20:07:24.621 | `[PBR-Ambient] ... skyL=0.611` (daylight; `dayF=1`) |
| 20:07:30.752 | `[water] chain: style=Xbox waterStyle=0 tier=0 needRT=0 sceneRT=none depthRT=none reflRT=none reflFlag=0 fresnelSplit=1 body=1806844436 VS c10.gate=1.0000 PS c14=(0.000000,0.000200,0.000000,0.000000) c15=(0,0,0,0) flushVerts=80 ocean=76 shallow=0 interior=0 river=0 pond=4 lake=0 nearClip=2.028 farClip=1227.143 PS=0E6323B0(IDR_WATER_PARALLAX) ...` |
| 20:07:44.159 | `[UW] underwater ON: cam=(2987.1,-1497.7,-2.6) level=0.00 depth=2.58 str=1.00 density=0.0850 fog=(0.036,0.120,0.181) light=(0.08,0.08,0.08)` |
| 20:07:46.160 | `[UW] underwater ON: cam=(2987.9,-1498.1,-1.0) level=0.00 depth=0.97 str=1.00 density=0.0850 fog=(0.020,0.064,0.118) light=(0.08,0.08,0.08)` |
| 20:07:50.793 | `[water] chain: ... needRT=0 ... flushVerts=16 ocean=16 ...` |
| 20:07:52.793 | `[water] chain: ... needRT=0 ... flushVerts=476 ocean=476 ...` |

Notes:
- `tier=0`, `needRT=0`, `sceneRT=none`, `depthRT=none`, `reflRT=none`, `reflFlag=0` are the
  direct proof that tier 0 has no scene/depth/reflection RT.
- `waterStyle=0` in the log is the numeric Xbox style; `PS=IDR_WATER_PARALLAX` is the
  `main_xbox` CSO (`resources/resource.h:47-48`, `tools/fast_build.py:426-429`).
- `body=1806844436` is **not a body code**; it is a string pointer passed to `%d`
  (`waterPipe.cpp:1498-1508`). Classification is actually on: `c10.gate=1.0000`,
  `c14=(0,0.0002,0,0)`. The real body histogram is `ocean=76 pond=4` / `ocean=476` etc.
- `nearClip=2.028` then `0.300` shows the camera changing (underwater near plane); not a
  water bug.

### Screenshot evidence

Files in `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\` (mtime 20:07):

| File | In-game time | Observation |
|---|---|---|
| `gta_sa_xX6m03Rkiq.png` | 08:19 | High-altitude aircraft over coast. Sea is pale blue/white; a giant straight-edged translucent band/wedge crosses the view. Land and water are washed. |
| `gta_sa_AuJjl6hbMs.png` | 08:36 | Helicopter low over coastal city. Sea upper-left has a fine sparkle field (normal/spec is alive), but a large grey translucent film covers the lower-left/sea; land also looks washed. |
| `gta_sa_4uJYc4OTLk.png` | 08:53 | Helicopter over open sea. Sky is teal; water is grey-white; a huge straight-edged wedge covers the right/lower half. No visible wave shape or reflection detail. |
| `gta_sa_jyA9ufK9Y2.png` | 09:12 | Underwater. Flat murky green-grey; a lighter green surface band above; bubbles; no light shafts, no caustics, no surface detail. |

The 08:36 shot is important: a **fine sparkle field** is visible in the sea, so the detail
normal texture is being sampled and specular is working. The water is not "normal map off";
it is "normal map has no visible carrier except specular". The 09:12 shot is the underwater
pass plus the tier-0 underside of the surface.

---

## 2. Tier-0 feature matrix: implemented vs `docs/plans/water-refs.md`

### Tier 0 (Low) — intended vs actual

| Feature | `water-refs.md` Low intent | Implemented at tier 0 | Evidence |
|---|---|---|---|
| Flat water.dat geometry, no displacement | Yes | Yes (`Water_VS.hlsl:65-73`) | `water-refs.md:52`; `Water_VS.hlsl:69-73` |
| Single scrolling normal layer | Yes | Yes: one `s1` tap; `n2=0` at tier 0 | `Water_Parallax.hlsl:762-772`; `waterPipe.cpp:1424` |
| Normal strength | n/a | `waterNormalStrength=1.0`, then `*(1-0.85*distFactor)` → 15 % at distance | `Water_Parallax.hlsl:780` |
| Timecyc colour | "vertex-colour timecyc" | Timecyc **constants**; `vtxColor.rgb` is never read; only `.a` is used | `waterPipe.cpp:1183-1207`; `Water_Parallax.hlsl:193-196` |
| Shallow→deep ramp | Not in Low (flat) | Constant `deepFactor=0.65`; no depth | `Water_Parallax.hlsl:722`; `water-refs.md:52` |
| Translucency | Vertex alpha / hardware alpha | Hardware SRCALPHA/INVSRCALPHA; `bodyT` from a **distance estimate**; `vtxA==0` is coerced to 1.0 | `Water_Parallax.hlsl:193-196`, `:500-503`, `:514-530`; `waterPipe.cpp:1466-1472` |
| Scene colour / refraction | No | No (`needRT=0`) | `waterPipe.cpp:966`, `:1423` |
| Depth copy / shore blend | No | No; `frontFade=0`, `shoreFade` forced 1.0 | `waterPipe.cpp:1002-1011`; `Water_Parallax.hlsl:540-541`, `:745` |
| Reflection | Hybrid recipe: fresnel sky tint | Analytic `skyReflect` only; no `s3`, no SSR | `Water_Parallax.hlsl:571-579`, `:801-805`; `waterPipe.cpp:1429` |
| Foam / sun pierce | No | No (tier >= 2 / 3) | `Water_Parallax.hlsl:823-833` |
| Underwater | Separate Ultra pass | Separate `Water_Underwater` pass active independently of tier; surface underside analytic | `postfx_underwater.cpp`; log 20:07:44 |

### Tier table — docs vs implementation

| Tier | Docs (`water-refs.md:51-55`) | Implemented | Gap |
|---|---|---|---|
| 0 | 1 normal + timecyc colour + vertex alpha | Same, but `deepFactor=0.65`, analytic reflection only, no depth | No shallow/deep variation; no junction/shore; no SSR |
| 1 | + dual scrolling normals + depth ramp | + second normal tap (`n2`); "depth ramp" is a **viewZ estimate** | No INTZ depth copy; `SampleWaterColumn` not used (`clipPlanes.z=0`) |
| 2 | + refraction/scene copy + depth copy + foam + shore | Yes: scene copy + depth copy + `s0`/`s2` + `frontFade`; foam/shore live | Xbox style still has no SSR (`s3` is style-gated to IV/V); style `0` never samples it |
| 3 | + sun pierce + fine normal x3.7 + wide 5-tap shore | Yes | Matches |

The important mismatch is **tier 1**: the docs call it "depth ramp", but the code only gets a
distance ramp until tier 2. A user lowering from 3 to 1 still gets no true depth-based shore
blend or shallow/deep colour; only tier 2 restores the depth copy.

### 2.1 Tier-0 constant audit (explicit zero/over-bright check)

| Constant / term | Uploaded / evaluated at tier 0 | Forced to 0? | Verdict |
|---|---|---|---|
| `waterParams.x` = `waterNormalStrength` | `1.0` (INI 232) → c1.x (`waterPipe.cpp:1258`) | No | Normal map is **on**; it fades only with distance (`Water_Parallax.hlsl:780`) |
| `waterParams.y` = `waterTileScale` | `1.0` (INI 245) | No | Detail UV frequency is live |
| `waterParams.z` = `waterFresnelPower` | `5.0` (INI 230) | No | Fresnel curve live |
| `waterParams.w` = `reflStr` | `0.55` Xbox (INI 236; style preset `waterPipe.cpp:1228`) | No | Analytic reflection is **on**, but has no SSR/detail (`reflFlag=0`) |
| `specParams.x/y` = spec power/intensity | `512` / `1.0` (INI 243/242) | No | Narrow specular glints live; visible as the 08:36 sparkle |
| `specParams.z` = translucency | `1.0` (INI 246) | No | Multiplies `bodyT` (`Water_Parallax.hlsl:520`) |
| `specParams.w` = shore range | `2.0` (INI 240) | No | Uploaded, but `shoreFade` is forced 1.0 at tier<2 (`:745`); no seam effect |
| `deepFactor` | `0.65` hardcoded (`:722`) | n/a | **Mismatch**: no shallow/deep ramp at tier 0 |
| `frontFade` | `0.0` hardcoded (`:721`) | **effectively yes** | **Mismatch**: no junction/shore fade |
| `shoreFade` | computed then overwritten to `1.0` (`:745`) | **yes** | **Mismatch**: smooth-shoreline term disabled |
| `waterCol * 2.0` | only `WaterV` tier>=2 (`:1253`) | n/a | **Not in tier 0**; no tier-0 over-bright from this term |
| `c11.w` = `waterFresnelSplit` | `1` (INI 231) | No | Split composite live; legacy `legacyOp` path is not used |

So the tier-0 problem is **not** "normal/reflection strength forced to 0". It is the
hardcoded `deepFactor=0.65`, the forced `frontFade=0`/`shoreFade=1`, and the absence of the
depth copy. The only zero-ish constants are the ones that disable the depth-driven terms.

---

## 3. Ranked causes and fix spec

### Cause 1 (root): tier 0/1 have no depth signal; `needRT=(tier>=2)` couples depth to the scene copy

**Evidence**
- `waterPipe.cpp:966` `bool needRT = (tier >= 2);`
- Scene copy: `:979-996` (inside `if(needRT)`).
- Depth copy: `:1002-1011` (also inside `if(needRT)`).
- Shader fallback: `Water_Parallax.hlsl:538-541` returns a distance estimate when
  `clipPlanes.z < 0.5`; `:722-738` sets `deepFactor=0.65` at tier 0 and a distance ramp at
  tier 1; `:745` forces `shoreFade=1.0` for tier < 2.
- Log: `needRT=0 sceneRT=none depthRT=none` at 20:07:30 / 20:07:50 / 20:07:52.

**Consequences**
- Flat colour: no true shallow→deep ramp; the whole sea is the same 65 % mix.
- Hard waterline: `frontFade=0` means `WaterJunctionEase(0)=1`, so the water does not
  dissolve into the scene at the shoreline; `RefractGate(thickness)` is evaluated on a
  distance estimate, not real water thickness. The water plane/terrain intersection is a
  hard depth cut.
- Wrong opacity: `WaterBodyOpacity` gets thickness from `S.viewZ*0.02`, not from the depth
  buffer, so near/far water is not physically thick/thin.
- The code path is intentionally "no depth"; this is not a broken uniform or a zeroed
  constant.

**Fix (recommended, does not re-enable tiers 2–3)**
Decouple the **depth** copy from the **scene colour** copy. Keep `s0`/refraction/foam/SSR
off; add only the INTZ depth read.

1. `src/render/waterPipe.cpp`
   - Keep `bool needRT = (tier >= 2);` for the scene-colour/refl copies (`:979-996`).
   - Add `bool needDepth = true;` (or an INI `waterDepthShore=1` gate; default on).
   - Move the depth block (`:1002-1011`) out of `if(needRT)` into `if(needDepth)`.
   - `:1425` bind `s2`: `g_d3dDevice->SetTexture(2, depthValid ? g_depthTexture : nullptr);`
   - `c8.z` already uploads `depthValid ? 1.0f : 0.0f` (`:1316`), no change.
   - Cost: one `PostFX_CopyDepthToTexture` per water flush (INTZ→R32F). No scene-colour
     copy, no reflection copy, no foam/shore SSR. This is a subset of tier 2, not a tier
     re-enable.

2. `shaders/ps/Water_Parallax.hlsl` — `WaterXbox` (and the same block in `WaterIV`,
   `WaterV`):
   ```hlsl
   // Replace the tier<2 fallback block:
   if (clipPlanes.z >= 0.5) {
       float2 col = SampleWaterColumn(S.screenUV, S.viewZ, tier);
       thickness = col.x;
       frontFade = col.y;
   } else {
       thickness = saturate(S.viewZ * 0.02) * max(shallowColor.w * 2.0, 1.0);
   }
   deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
   ```
   - Do **not** add the `tier>=2` scene fetch/refract offset.
   - Change `if(tier < 2) shoreFade = 1.0;` (`:745`, `:921`, `:1085`) so the computed
     `WaterShoreCoverage` survives for the legacy path; the split path uses `frontFade`
     directly in `F`/`bodyT` (`:845-846`, `:853-854`).
   - `SampleWaterColumn`'s tier-3 five-tap branch is guarded by `tier >= 3`; at tier 0/1 it
     is one depth tap.
   - Expected: true shallow/deep ramp, `frontFade` junction at the waterline, and correct
     near/far opacity. This addresses Causes 1, 4 (opacity), and 5 (seam) together.

3. If no code change is allowed: the only existing complete fix is `waterQuality=2`
   (which re-enables the whole RT path). `waterQuality=1` will **not** fix the seam; it only
   adds the second normal tap and a distance colour ramp.

**Underwater surface-toggle hard seam (separate, small)**
`src/render/postfx_underwater.cpp:355-359` returns as soon as `camDepth <= 0.25`, so the
full-screen fog switches on/off hard as the camera bobs at the surface. Replace the hard
return with a fade:
```cpp
float uwFade = saturate((camDepth - kUWSurfaceEps) / 0.75f);
if (uwFade <= 0.0f) return;
...
density *= uwFade;
sssScale *= uwFade;
```
This removes the screen-wide waterline pop without changing the tier.

---

### Cause 2: tier 0's normal map has almost no visible carrier

**Evidence**
- `Water_Parallax.hlsl:762-782`: one normal tap; `n2=0`; `N` is normalized; at tier 0 the
  normal only feeds `NdotV`, `NdotL`, `NdotH`.
- `:780` `strength = waterParams.x * B.norm * (1.0 - 0.85 * distFactor)`; from an aircraft
  `distFactor` saturates at `farClip*0.12` (~147 m at the logged far clip 1227), so
  `strength` → 0.15. The same fade applies to specular (`:813-814`, power down to 8,
  intensity ×0.4).
- `:805` `skyReflect = SkyReflection(N, S.V, ...)`: the normal only shifts the
  `skyTop`/`skyBot` blend; at 08:19–09:12 both are blue, so the analytic reflection is nearly
  uniform.
- `:845-846` `F = reflAmt * RefractGate(thickness) * ...`; `reflAmt` is `fresnel *
  waterReflectionStrength`. At a steep view `fresnel ≈ 0.02`, so `F ≈ 0.011`; at grazing it
  is larger, but the sky tint is flat.
- Log: `reflRT=none reflFlag=0` — no SSR; `PS=IDR_WATER_PARALLAX` — Xbox core never samples
  `s3` even at tier 2 (`waterPipe.cpp:991`: `if(config->waterStyle != 0)`).
- Screenshot 08:36 shows a fine sparkle field, proving the normal map/spec path runs; the
  sparkle is the only visible normal cue.

**Fix (INI / constants; no tier 2–3)**
- `waterNormalStrength = 2.0` (line 232) — stronger normal perturbation.
- `waterTileScale = 2.0` (line 245) — finer detail; with `waterNormalStrength` this makes
  the ripples read at altitude.
- `waterReflectionStrength = 0.75` (line 236) — more Fresnel modulation at grazing; this can
  also brighten/wash, so test 0.35 vs 0.75.
- `waterSpecularIntensity = 0.6-0.8` (line 242) — the specular glints are the main normal
  cue at tier 0; do not set to 0.
- Constant: `Water_Parallax.hlsl:780` `(1.0 - 0.85 * distFactor)` → `(1.0 - 0.55 *
  distFactor)`; same in `WaterV` `:1125`. This keeps ripples alive at distance. `WaterIV`
  uses a different `nFade` (`:954`), leave it.
- Note: tier 0 cannot show screen-space reflection. If the goal is "reflection detail",
  either use `waterStyle=IV`/`V` **and** `waterQuality>=2` (SSR is style- and tier-gated),
  or accept the analytic sky tint. `waterStyle=Xbox` will never sample `s3`.

**Expected:** visible ripple modulation through Fresnel/specular; no SSR. The surface will
still be a flat colour at the horizon because `FarBlendWater` washes it.

---

### Cause 3: flat teal comes from timecyc + SA colour quirk; the INI colour keys are inert on the surface

**Evidence**
- `waterPipe.cpp:1183-1207`: `waterUseTimecycle=1` → `dpR/G/B = tc01(tc.waterR/G/B)`.
- `:1194-1196`: SA's own quirk `col = col*0.65 + 0.27` (docs `water-refs.md:9`).
- `:1200-1202`: shallow = base + 18 % toward white.
- `:1285`: `shallow[3] = 6.0f` (`deepRange`), `:1289` deep alpha.
- `Water_Parallax.hlsl:722`: tier-0 `deepFactor=0.65` → `waterCol = lerp(shallow, deep,
  0.65)`.
- `timecyc.dat` (`E:\games\gtasa_skygfx_plus\data\timecyc.dat`) water columns:
  - 7 AM (EXTRASUNNY_LA row): `145 170 170 240`
  - Midday: `90 170 170 240`
  - After quirk: 7 AM `(0.64,0.70,0.70)`, midday `(0.50,0.70,0.70)`.
  - Shallow lift +18 %: 7 AM `(0.70,0.75,0.75)`, midday `(0.59,0.75,0.75)`.
  - Tier-0 65 % mix: roughly `(0.60,0.72,0.72)` (7 AM) / `(0.55,0.72,0.72)` (midday),
    before `WaterBodyLight` (≈×0.9–1.0) and `FarBlendWater`.
- The live INI leaves `waterShallow*` / `waterDeep*` at the default teal
  `(0.13,0.45,0.55)` / `(0.01,0.03,0.08)`; with `waterUseTimecycle=1` these are **not used
  by the surface**. They are used by the underwater fog (`postfx_underwater.cpp:375-390`),
  which is why the underwater palette and surface palette are inconsistent.

**Fix (minimal, INI)**
- Set `waterUseTimecycle = 0` (line 248). The surface then uses
  `waterShallowR/G/B` and `waterDeepR/G/B` (`waterPipe.cpp:1208-1216`).
- Start from the existing defaults or choose a darker, less green Xbox palette, e.g.
  `waterShallow = (0.08, 0.30, 0.32)` and `waterDeep = (0.01, 0.04, 0.08)` (0–1 floats).
  Because `postfx_underwater.cpp` uses the **same** keys, the underwater fog colour becomes
  consistent with the surface immediately.
- If the timecyc look must be kept, change the quirk constants instead:
  `waterPipe.cpp:1194-1196` `0.65f/0.27f` → `0.75f/0.12f`, and the shallow lift
  `0.18f` (`:1200-1202`) → `0.10f`. This removes the white lift/desaturation and darkens the
  teal, but it affects every tier and every style.
- Do **not** tune `waterShallow*` / `waterDeep*` while `waterUseTimecycle=1`; they are dead
  for the surface. This is the main reason the user's "lowered water settings" did not
  change the teal.

---

### Cause 4: near-white shallow/sea-floor is bright scene content through low-opacity water (plus overlay)

**Evidence**
- `Water_Parallax.hlsl:493-504` (tier 0/1 composite):
  `w = saturate(F + (1-F)*bt)`, `A = reflectC*F + bodyC*(1-F)*bt + highlights*...`,
  return `(saturate(A/w), w)`. The hardware blend then does `src*w + dst*(1-w)`. The shader
  source is saturated, so **the water cannot emit >1**; the white comes from the destination
  (bright seabed/sky/overlay) weighted by `1-w`.
- `Water_Parallax.hlsl:514-530`: `WaterBodyOpacity` uses
  `halfCol = max(shallowColor.w*0.5, 0.5)` = 3 m with `deepRange=6`; `thickness` comes from
  `saturate(S.viewZ*0.02)*12` (`:736`), not real depth.
- At ~10 m view distance, `thickness ≈ 2.4`; at `NdotV≈0.5`, `pathLen≈4.8`,
  `t=1-exp2(-1.6)≈0.67`, `bodyT≈0.63`; `w≈0.65`. That leaves ~35 % of the bright bottom
  visible at close range. With a bright sand seabed and the Modern colour filter, this reads
  near-white.
- `waterTranslucency=1.0` (line 246) is the multiplier on `t` (`:520`); increasing it makes
  the water more opaque.
- `FarBlendWater` (`:439-443`) lerps far water toward `lerp(deep, skyBot, 0.22)` with weight
  `farT*0.85`; at `distFactor→1` the surface is 85 % sky/horizon colour, which can look
  white in bright weather.
- The `2.0 * waterCol` over-bright is only in `WaterV` tier >= 2 (`:1253`), not tier 0.

**Fix (minimal)**
- INI: `waterTranslucency = 1.4-1.6` (line 246) — raises `bodyT`, hides the bright bottom.
- INI: `waterReflectionStrength = 0.35-0.45` (line 236) — less uniform sky wash on the
  surface.
- INI: `waterSpecularIntensity = 0.6` (line 242) — fewer white glints; do not remove it
  entirely or the normal detail disappears at tier 0.
- Constant: `waterPipe.cpp:1285` `deepRange 6.0f` → `4.0f` (or `3.0f` for a strong fix).
  `halfCol` becomes 2/1.5 m, so the same thickness is more opaque. This is the same knob the
  P3 sea-floor fix changed from 20 → 6 (`:1274-1284`); 4 is a safe next step, 3 if the
  bottom still blows out.
- Constant: `Water_Parallax.hlsl:441-443` `farT * 0.85` → `farT * 0.45` (or
  `smoothstep(0.55, 1.0, distFactor)`); this reduces the far sky-white wash. It affects all
  tiers; test with `waterParallaxEnable=0` first to make sure the white is not the overlay.
- If the white remains with `waterTranslucency=1.6` and `deepRange=3`, it is almost
  certainly the separate overlay or the scene behind (see Section 4), not the water shader.

---

### Cause 5: hard waterline seams — no junction fade at tier 0

**Evidence**
- `Water_Parallax.hlsl:721` `frontFade=0`; `:845-846` `F *= ... saturate(1 - frontFade*...)`
  → no junction gate; `:853-854` `bodyT` → `WaterBodyOpacity(... frontFade=0 ...)` →
  `WaterJunctionEase(0)=1` (`:276-279`), so the sheet stays opaque to the geometry edge.
- `:745` `if(tier < 2) shoreFade = 1.0;` — the smooth-shoreline coverage is discarded at
  tier 0/1.
- `RefractGate` (`:667-670`) is `saturate(thickness*3)`, but `thickness` is a distance
  estimate, so it does not go to 0 at the actual waterline.
- The water plane is flat; the shoreline is the depth-buffer intersection of the plane with
  sloped terrain. Without a depth sample, there is no way to know the pixel is at the shore.
- Additional hard toggle: `postfx_underwater.cpp:356-359` (camera crossing the surface).

**Fix**
- Primary: the **depth-only decoupling** in Cause 1. With `depthValid=1`,
  `SampleWaterColumn` returns `frontFade = saturate((waterViewZ - sceneZ)/shoreRange)`
  (`:559-561`), which goes to 0 exactly at the shoreline; `F` and `bodyT` then fade the
  sheet into the scene (`:845-846`, `:853-854`). This is the same junction math the tier-2
  path uses, without the scene colour/refraction/foam.
- Secondary: `postfx_underwater.cpp` fade in Cause 1 removes the full-screen surface toggle.
- No-code workarounds at tier 0: none of `waterShoreFade` (only `c2.w`), `waterShoreFoam`,
  `waterFoamThreshold`, or `waterFoamSoftness` will help. `waterShoreFade` is overwritten
  by `shoreFade=1` for tier<2; foam is gated `tier>=2` (`:823-828`). Lowering these makes
  no difference — that is a likely reason the user's tuning felt ineffective.
- If the seam is the **water body classification** (mixed `ocean=76 pond=4` in one flush),
  A/B `waterBodyClassify=0` (line 224). The body scales `B.opac`/`B.norm`/`B.shore` change at
  type borders; the 5 m edge ramp (`waterPipe.cpp:285-305`) only softens type-change edges,
  not the coastline. This is a secondary seam source, not the primary one.

---

### Cause 6: underwater is a separate pass, enabled; green + no shafts

**Evidence**
- `postfx.cpp:1672` calls `PostFX_UnderwaterTick()` after the pre-tonemap group and before
  the colour filter.
- `postfx_underwater.cpp:310-316` gate: `waterUnderwaterFog > 0`; live INI 1.0, so active.
- `:348-359`: camera vs `water.dat` level; `camDepth = level - camZ`; the log shows
  `level=0.00 depth=2.58` and `depth=0.97`, so the pass is genuinely below the ocean
  surface, not a false positive.
- `:370-373`: `density = 0.085 * strength`; `sssScale = 2.0 * 0.08 * strength`.
- `:375-390`: fog colour = **config** `waterDeep*` → `waterShallow*` lerp by `camDepth/12`.
  The log values are exactly this:
  - depth 2.58 → `fog=(0.036,0.120,0.181)`
  - depth 0.97 → `fog=(0.020,0.064,0.118)`
- `:394-397`: SSS tint = `tc.directionalR/G/B`; log `light=(0.08,0.08,0.08)`, so the SSS
  contribution is at most `sssScale * 1 * heightF * 0.08 ≈ 0.013` — invisible.
- `Water_Underwater.hlsl:108-122`: `fog = 1 - exp2(-density*path)`, capped at `0.98`;
  `water = fogColour + sssColour*sss`; `result = lerp(scene, water, fog)`. There is **no
  shaft/caustic term** in this shader.
- `[PF] FX pre-tonemap group ... gates: 0/0/0` at 20:07:23; `sunShaftsEnable=0` (INI 186),
  `godRaysEnable=0` (INI 69). So the engine's actual light-shaft pass is off.
- Tier-0 surface from below: `Water_Parallax.hlsl:845` uses `WaterBelowOpacity` (TIR curve),
  `:853-854` `bodyT` is 0 because `WaterBodyOpacity` multiplies `(1 - below)` (`:528`);
  `:864-881` tier<2 returns `WaterSplitBlend`, which at grazing is almost pure
  `skyReflect`/`underEnv` (`:571-579`), i.e. a flat green sheet. No scene copy/refraction at
  tier 0.

**Fix**
- Murk/colour: `waterUnderwaterFog = 0.5-0.6` (line 247) to reduce density and SSS; set it
  to `0` to disable the pass entirely and compare. `waterUseTimecycle=0` plus the same
  `waterDeep*` / `waterShallow*` palette as Cause 3 makes the surface and fog consistent.
- If keeping the code default palette, darker/less green values:
  `postfx_underwater.cpp:107-109`
  `kUWDeepDefault = (0.005,0.02,0.05)`, `kUWShallowDefault = (0.03,0.12,0.16)`;
  `kUWColorDepth 12.0f` → `20.0f` keeps the deep colour longer.
- To make the existing SSS visible without adding a new pass:
  `kUWSSSGain 0.08f` → `0.30f` (line 91) and/or use a floor on the light colour
  (`max(tc.directionalR, 0.25f)` at `:395-397`). This is a tint, not shafts.
- Actual shafts: enable the existing global pass (`sunShaftsEnable=1`, `sunShaftsDensity`
  0.84, line 186) or `godRaysEnable=1` (line 69). That is a global postfx change, not a
  water-tier fix. The water shader has no caustic/shaft implementation; adding one is a new
  feature, not a tier-0 bug fix.
- Underside face-on transparency: `Water_Parallax.hlsl:451-455`
  `WaterBelowOpacity` base `0.12` → `0.05` and slope `0.88` → `0.75` if you want more of the
  real scene through the surface face-on; at grazing it remains TIR (correct).

---

### Cause 7 (minor): log bug and dead tier-0 knobs

- `waterPipe.cpp:1498-1508`: format string says `body=%d`, argument is
  `bodyOn ? "on" : "off"` (a `const char*`). It prints a pointer
  (`1806844436`), which can be mistaken for a body code. Fix to `body=%s` (argument already
  a string) or `body=%d` with `bodyOn ? 1 : 0`.
- Tier-0 dead knobs (do not tune them for these symptoms):
  - `waterShoreFade` — used as `c2.w` (`waterPipe.cpp:1269`), but `shoreFade` is forced 1.0
    at tier<2 (`Water_Parallax.hlsl:745`) and the split path does not use `shoreFade`.
  - `waterShoreFoam`, `waterFoamThreshold`, `waterFoamSoftness` — foam is tier>=2
    (`:823-828`).
  - `waterParallaxScale` — only the refraction offset at tier>=2 (`:865`);
    `c8.w` is uploaded but unused at tier 0.
  - `waterShallow*` / `waterDeep*` — surface-inert while `waterUseTimecycle=1`; only the
    underwater fog uses them (Cause 3).
- Live knobs at tier 0: `waterNormalStrength`, `waterTileScale`, `waterFresnelPower`,
  `waterReflectionStrength`, `waterSpecularPower`, `waterSpecularIntensity`,
  `waterTranslucency`, `waterUseTimecycle`, `waterUnderwaterFog`, `waterBodyClassify`.
  `waterFresnelSplit` selects the legacy vs split composite.

---

## 4. The giant translucent overlay — evidence and boundary

What the screenshots show:
- 08:19 and 08:53: a large straight-edged translucent band/wedge with perspective-like
  converging edges. In 08:19 it reaches the top of the frame over the sea/coast; in 08:53 it
  starts at the horizon and covers the sea. It is much lighter than the water around it.
- 08:36: a grey film over the lower-left/sea and some land; a fine sparkle field is visible
  in the sea, so the water shader is drawing.
- 09:12 (underwater): no such wedge; the surface reads as a green band, consistent with the
  tier-0 underside (`WaterBelowOpacity`/`underEnv`).

Evidence that it is **not** the water pipe:
- In 08:19 it reaches the top of the frame and in 08:53 it starts at the horizon; a flat
  Z-up water plane should stay below the horizon. (Caveat: at high altitude the upper half
  may actually be sea, so this is not conclusive.)
- The water draws in the same frames are small flushes (16–476 verts per `[water] chain`);
  there is no fullscreen water/reflection pass (`needRT=0`, `sceneRT=none`, `reflRT=none`).
- The water pipe pins Z-test ON, Z-write OFF, ZFUNC LESSEQUAL
  (`waterPipe.cpp:1405-1413`); a water plane over land should be depth-occluded by land.
- The overlay is absent in the underwater shot, while the water surface is still visible as
  a green band. A separate above-water overlay would explain that; the water plane would
  also be visible from below, but its colour/opacity differs, so this is weak evidence.

Evidence that it **could** be the water plane:
- The straight edges look like water.dat quad/triangle boundaries; the water is drawn with
  `rwCULLMODECULLNONE` (double-sided, `waterPipe.cpp:1483`), so a large quad can cover a
  large screen area.
- The water body classifier sees mixed types in one flush (`ocean=76 pond=4` at 20:07:30);
  `B.opac`/`B.norm`/`B.shore` differ per body type (`Water_Parallax.hlsl:381-396`), and the
  edge ramp is only 5 m (`waterPipe.cpp:199`, `285-305`). A body-type boundary can leave a
  visible straight seam.
- The water surface's `vtxA` is per-vertex from the game buffer (`Water_Parallax.hlsl:193-196`);
  a different water layer/quad alpha can make one polygon more transparent. This is not
  logged, so it is unproven.

Do **not** spend more time on the overlay in this lane. The A/B in Section 7 decides it:
`waterParallaxEnable=0`. If the wedge remains, it belongs to the other lane. If it vanishes,
the water pipe owns it; then log `min/max/avg vtxColor.a` per flush and re-test with
`waterBodyClassify=0`.

---

## 5. Minimal fix spec summary

| Symptom | Primary cause | Minimal fix (INI/constant) | Exact location | Expected | Risk |
|---|---|---|---|---|---|
| Flat teal | Timecyc colour + tier-0 65 % mix; depth absent | `waterUseTimecycle=0`; set `waterShallow*`/`waterDeep*` to a darker blue-green; or quirk `0.65/0.27`→`0.75/0.12` | `waterPipe.cpp:1194-1196`, `:1200-1202`, `:1208-1216`; INI 225-227/237-239/248 | Authored colour replaces the pale timecyc mix | Medium; palette taste |
| No normal/reflection detail | Tier 0 has only one analytic sky tint; normal strength fades | `waterNormalStrength=2.0`; `waterTileScale=2.0`; `waterReflectionStrength=0.75`; shader fade `0.85`→`0.55` | INI 232/245/236; `Water_Parallax.hlsl:780`, `:1125` | Visible ripples via Fresnel/spec; still no SSR | Medium; stronger sparkle |
| Blown shallow/sea-floor | Bright seabed/sky through low opacity; far blend | `waterTranslucency=1.5`; `deepRange 6→4`; `waterSpecularIntensity=0.6`; `FarBlendWater` `0.85→0.45` | INI 246/242; `waterPipe.cpp:1285`; `Water_Parallax.hlsl:443` | More opaque, less white wash | Medium; may look too opaque/dark |
| Hard waterline | No `frontFade`/depth; `shoreFade=1` | Depth-only decoupling (Cause 1); or `waterQuality=2` | `waterPipe.cpp:966/1002-1011/1425`; `Water_Parallax.hlsl:723-745` | Soft junction at the shoreline | Code change; one INTZ copy |
| Green underwater | UW fog uses config palette; SSS light 0.08; no shafts | `waterUnderwaterFog=0.5-0.6`; `waterUseTimecycle=0` + dark palette; `sunShaftsEnable=1` for shafts | INI 247/248/186; `postfx_underwater.cpp:107-109`; `Water_Underwater.hlsl:108-122` | Clearer, bluer water; shafts only if the global pass is on | Medium; shafts are a new global effect |
| Giant wedge | Likely other lane; water plane not exonerated | A/B `waterParallaxEnable=0`; `waterBodyClassify=0` | `main.cpp:3914-3924`; INI 233/224 | Ownership decided | None |

Suggested starting INI set (for one run, not all at once):
```ini
waterParallaxEnable = 1
waterStyle = Xbox
waterQuality = 0
waterUseTimecycle = 0
waterShallowR = 0.08
waterShallowG = 0.30
waterShallowB = 0.32
waterDeepR = 0.01
waterDeepG = 0.04
waterDeepB = 0.08
waterNormalStrength = 2.0
waterTileScale = 2.0
waterReflectionStrength = 0.45
waterSpecularIntensity = 0.6
waterTranslucency = 1.5
waterUnderwaterFog = 0.6
waterBodyClassify = 1
waterFresnelSplit = 1
```
Then change one key at a time. If the goal is the authentic Xbox look, start with
`waterParallaxEnable = 0` instead; the custom "Xbox" style is not stock Xbox water
(`water-refs.md:4-6`).

---

## 6. A/B test plan for the next run (3 steps)

**Step 1 — ownership: water pipe vs overlay**
- Set `waterParallaxEnable = 0` (keep `waterStyle=Xbox`, `waterQuality=0`).
- Expected: original `CWaterLevel__RenderAndEmptyRenderBuffer` runs
  (`main.cpp:3918-3924`); log shows `[water] gate failed (enable=0 ...) -> original draw`
  or no `[water] chain` at all; water becomes the game's vanilla water.
- Decide:
  - Overlay and flat teal both disappear → water pipe owns them; continue to Step 2.
  - Overlay remains → it is the separate lane; stop chasing it here and treat it as a
    confounder. Flat teal may still be the water.
  - Overlay disappears but the water still looks wrong → the "Xbox" custom style is the
    issue; keep `waterParallaxEnable=0` for the authentic look, or continue.

**Step 2 — tier: 0 vs 1 vs 2 (depth coupling proof)**
- `waterParallaxEnable=1`, `waterStyle=Xbox`.
- Run A: `waterQuality=0`; Run B: `waterQuality=1`; (diagnostic) Run C:
  `waterQuality=2`.
- Expected:
  - Tier 1 adds `n2` (dual normals) and a **distance** colour ramp, but still
    `needRT=0` (no depth copy). If flat teal/seams persist → the depth coupling is the
    cause, not the normal count.
  - Tier 2 sets `needRT=1`, `sceneRT=ok`, `depthRT=ok`, `reflRT=ok` for IV/V
    (`waterPipe.cpp:979-1011`); if the seams/flat colour improve dramatically, this proves
    the depth copy is the missing piece. Tier 2 is a diagnostic here, not the target
    setting (it re-enables the scene copy/refraction/foam).
- Log check: `[water] chain ... needRT=0/1 ... sceneRT/depthRT/reflRT ...`.

**Step 3 — style constants: Xbox vs IV vs V at tier 0**
- `waterQuality=0`, `waterParallaxEnable=1`.
- Run A: `waterStyle=Xbox`; Run B: `waterStyle=IV`; Run C: `waterStyle=V`.
- Expected:
  - IV: `specPow=250`, `specInt=0.6*`, `reflStr=1.6*` (`waterPipe.cpp:1231-1235`);
    stronger reflection cap, broader glints.
  - V: `specPow=1118`, `specInt=0.3*`, `reflStr=1.55*`, `pierce=0.5`
    (`:1236-1241`); tighter, dimmer glints, flatter reflection normal.
  - Log: `PS=IDR_WATER_IV` / `IDR_WATER_V` / `IDR_WATER_PARALLAX`, `style=IV/V/Xbox`.
  - If IV/V show more wave/reflection detail, the flatness is the Xbox preset's conservative
    constants; if all three are flat, the tier-0 no-SSR/no-depth limit dominates.

Optional same-run sub-tests (one at a time, after the three steps):
- `waterUseTimecycle=0` (flat teal cause; immediately changes surface + UW palette).
- `waterFresnelSplit=0` (legacy composite; compare white/seams; the split path is
  `c11.w=1` today).
- `waterBodyClassify=0` (wedge/seam; if the overlay shape changes, body-type scales are
  involved).
- `waterUnderwaterFog=0` (green underwater; if the green disappears, the UW pass owns it).
- `sunShaftsEnable=1` (shafts; global postfx, not water; watch for white-out).

---

## 7. Non-goals / open questions

- The giant overlay's owner is out of scope. This lane only provides the ownership test.
- The exact `COLOR0.a` distribution in the water TempBuffer is not logged. If the
  depth-only fix does not remove the seam, add a per-flush
  `min/max/avg vtxColor.a` log in `WbClassifyFlush` (`waterPipe.cpp:503-534`) before
  changing `S.vtxA` (`Water_Parallax.hlsl:193-196`).
- The timecycle at 08:19–09:12 is clear/day (`skyL≈0.6`, `dayF=1`), so the flat look is not
  a night-gating bug. The underwater `light=(0.08,0.08,0.08)` is low enough to kill SSS,
  but the surface's daylight term is independent of it.
- The Modern colour filter (`filter=8`, `PostFX-REPORT`) can amplify white/green. It is not
  a water-lane fix; compare with `colorFilterEnable=0` only if the water fixes leave a
  residual cast.
- `water-cso` build: `Water_Parallax.cso` is compiled from `main_xbox`
  (`tools/fast_build.py:426-429`); the log's `PS=IDR_WATER_PARALLAX` proves the right entry
  is bound.

---

## 8. References

- `docs/plans/water-refs.md:4-6` — no Xbox shader; stock Xbox = CWaterLevel + timecyc.
- `docs/plans/water-refs.md:43-56` — hybrid recipe + quality-tier intent (Low = 1 normal +
  timecyc + vertex alpha; Med = +dual normals + depth ramp; High = +refraction/RT + foam).
- `docs/plans/water-refs.md:114-124` — CryEngine/V per-tier steal map.
- `src/render/waterPipe.cpp:14-28` — tier contract comment.
- `src/render/waterPipe.cpp:961-1011` — tier gate + scene/depth copies.
- `src/render/waterPipe.cpp:1183-1216` — timecyc vs INI colour.
- `src/render/waterPipe.cpp:1219-1289` — style presets + constants (`deepRange`).
- `shaders/ps/Water_Parallax.hlsl:59-70` — tier docs.
- `shaders/ps/Water_Parallax.hlsl:708-882` — `WaterXbox` tier branches/composite.
- `src/render/postfx_underwater.cpp:80-109`, `:310-403`, `:450-522` — underwater pass.
- `shaders/ps/Water_Underwater.hlsl:84-122` — fog/SSS (no shafts).
- `src/core/main.cpp:3914-3946` — water hook/fallback.
- `src/core/main.cpp:4847-4932` — water config parse.
