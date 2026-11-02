# Interior view-angle blackout — shader / ambient / IBL audit and fix plan

Date: 2026-10-25 (audit run 2026-10-26)
Repo: `E:\dev(dave)\skygfx_plus_expIV`
Scope: read-only audit. **Only this file was written.**
Symptom under test: *"at random angles the screen goes black in interiors"*, with
interiors driven by `BuildingPBRPS` = `main_building` in
`shaders/ps/VehiclePBR_Modern.hlsl` and the PBR callback in
`src/render/buildingPipe.cpp`.

---

## 0. Executive summary

1. **`main_building` has exactly one arithmetic operation that can darken the
   image: the sky-environment `lerp` at
   `shaders/ps/VehiclePBR_Modern.hlsl:1184`.**
   Every other view-dependent term is either additive (GGX/cluster/s1 env) or
   multiplicative with a hard lower bound of 0.6 (`SkyHemisphereTint`). A
   whole-screen blackout attributable to this shader must therefore come through
   line 1184 (or from `color` already being ~0 before it).
2. **A lerp toward a near-black sample at `F -> 1` is possible.** The guard is
   `skyLum > BLD_SKY_LUM_FLOOR` (`:1172`), and the current constant is **0.10**
   (`:1162`), not 0.02. The `0.02` string is only in the comment at `:1153`.
   The HEAD/logged build has **no luminance floor at all** (only
   `dot(skyEnv,skyEnv) > 1e-6`; `HEAD:shaders/ps/VehiclePBR_Modern.hlsl:852`).
   A 0.02 floor rejects almost nothing: typical night/interior sky-horizon
   luminance is 0.02–0.12, so 0.02–0.10 captures pass.
3. **The logged ASI predates the indoor fixes now in the working tree.**
   `[BUILD]` says `built=Oct 25 2026 21:56:44` (log line 27); the current
   `shaders/ps/VehiclePBR_Modern.hlsl` and `src/render/buildingPipe.cpp` are
   mtime 2026-10-26 00:06, `src/render/pipelinecommon.cpp` 23:11, and all three
   carry large uncommitted diffs. The indoor `currArea` gates, the neutral s3
   bind, the 0.10 luminance floor and the no-re-clamp ambient floor **are not in
   the logged binary**. The log therefore cannot be used to judge the current
   source; it documents the pre-fix build.
4. **The log contains no interior PBR frame.** `interior=1` occurs zero times.
   The only PBR building frames are 22:37:22–26 and are explicitly
   `interior=0` (`[Tonemap] ... interior=0`, log line 20718). At 22:41 the mode
   switched to PS2 then Xbox (`[BPIPE] cb=PS2/Xbox`), so the last ~5 minutes of
   the log are not PBR at all.
5. **The 512x512 resource is not the sky IBL capture.** The sky capture
   `g_iblTex` is quarter-screen (`GetIBLTexture: OK 442x248`, log line 20700),
   `D3DFMT_A8R8G8B8`. The 512x512 raster is `envFB`, the per-camera sphere
   reflection capture (`[EnvMap] ... envFB=... 512x512`, log line 20697). The
   building callback rejects `CAMERATEXTURE` rasters for s1
   (`src/render/buildingPipe.cpp:2749-2750`), so the sphere capture cannot be
   sampled by `main_building`; it is a vehicle-env resource plus a render-state
   risk.
6. **The strongest causal chain** is:
   indoor ambient starvation (old `ClampAmbientToSky`, now fixed in the working
   tree) → a body that is already very dim → grazing `F -> 1` selects a dark
   interior sky capture and replaces up to `bldSkyReflect` of that body
   (`:1184`) → black at grazing angles only. At the default
   `bldSkyReflect=0.15` this is a 15% darkening; at `1.0` (menu range) it is up
   to 85% replacement with the current `BLD_ENV_WEIGHT_MAX` and 100% in HEAD.
   The live config value is not in the log (the on-disk ini is not authoritative
   for the session — see §1).

### Ranked hypotheses (details in §8)

| Rank | Hypothesis | Confidence |
|---|---|---|
| H1 | Grazing s3 sky `lerp` toward a dark indoor IBL capture (`:1184`) | **40%** |
| H2 | Indoor ambient crush (`ClampAmbientToSky`) leaves the body near-black; the current fix is not in the logged/deployed build | **30%** |
| H3 | `SkyHemisphereTint` multiplies the body by a dark-capture chromatic ratio (`:92-108`, `:1094`) | **10%** |
| H4 | Tonemap / `ColourFilter_Modern` exposure collapse | **8%** |
| H5 | Normal-buffer tilt / sphere-env pass state feeding the view terms | **6%** |
| H6 | Cull / portal / black-void geometry (not `main_building`) | **4%** |
| H7 | NaN or uninitialized IBL sample | **2%** |

H1 and H2 are not independent: H1 needs a dark body (H2) to become a full
blackout. The combined chain is ~65%.

### Direct answers to the six audit questions

1. **View terms.** `F_atNdotV = F_Schlick(NdotV,F0)` at `VehiclePBR_Modern.hlsl:1042`
   (`PBR_Common.hlsl:15-19`); the only other `(1-NdotV)^5` power is Burley's
   scatter term at `:128`, which only scales additive diffuse. `envW` at `:1176`;
   `skyLum`/`skyValid` at `:1171-1172`; s3 sample at `:1150`; GGX at `:1063-1067`.
   **Only `:1184` can darken.** A lerp to a black sample at `F -> 1` is possible;
   it is bounded by `bldSkyReflect` and the 0.10 floor, and the floor is absent
   in the logged build.
2. **IBL indoors.** `g_iblTex` is the quarter-screen DynamicSky gradient built
   from timecycle `skyTop`/`skyBot` (`postfx.cpp:2597-2610`), so it is dark
   indoors. In the logged build it is refreshed and bound indoors with no
   luminance floor; in the current tree it is suppressed indoors, s3 is a 1x1
   white neutral, `bldEnv.z=0` and `c20.x=0`. A dark capture can reach
   `skyEnv` only if `currArea==0` (garage/sheltered), the neutral bind fails,
   or the build is stale. The 512x512 `envFB` is a different resource and is
   rejected for building s1 by the `CAMERATEXTURE` guard (`buildingPipe.cpp:2749-2750`).
3. **Ambient.** `ClampAmbientToSky` early-returns for `currArea != 0` at
   `pipelinecommon.cpp:91-93`. Writers: `buildingAmbient` (`buildingPipe.cpp:183`),
   `GetTimecycleAmbient` (`pipelinecommon.cpp:171-194`), `GetTimecycleAmbientPBR`
   (`:196-234`), `ambientFloor` (`:158-168`), `pbrAmbientFloor` (`:205-216`),
   PS c24 (`buildingPipe.cpp:2330-2331`) and VS c4 (`:1322/1891/2056/2678`).
4. **Tonemap.** `TonemapPass.hlsl:209-230` clamps the auto-exposure window to a
   positive minimum (0.15) and `FilmicTonemap(0)=0`; it cannot turn a lit wall
   into a whole-screen black. Config auto-exposure was 0 in the logged ini and
   `[Tonemap] VAL exp=1.187` was logged outdoors.
5. **Per-angle resources.** The 512x512 sphere pass runs indoors (no
   `currArea` gate in `envmap.cpp:1012-1062`) but is rejected for building s1;
   the normal buffer is per-view and can tilt `N` into the dark sky texel; the
   PipeChain classify is disabled; the mirror buffer is not on this path.
6. **Log.** No `[BUILD]` interior proof: the latest PBR frames are
   `interior=0`, `interior=1` count is 0, and the logged ASI was built at
   21:56 before the current fixes. The IBL is not logged as dark/indoor; the
   sphere pass is logged but not indoors. Details in §7.

---

## 1. Evidence base and the build mismatch

### 1.1 Log identity

The latest build banner in `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`:

```
27:[22:35:22.707] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

Only one `[BUILD]` line exists. The deployed `skygfx.asi` mtime is
`2026-10-25 21:57:27`.

### 1.2 The logged binary is older than the current fixes

```
2026-10-26 00:06:15  shaders/ps/VehiclePBR_Modern.hlsl
2026-10-26 00:06:37  src/render/buildingPipe.cpp
2026-10-25 23:11:42  src/render/pipelinecommon.cpp
2026-10-26 00:06:42  resources/cso/VehiclePBR_Modern.cso
2026-10-25 21:57:27  E:\games\gtasa_skygfx_plus\skygfx.asi
```

`git diff --numstat` for these three sources is 454/142 (shader), 1463/85
(buildingPipe), 884/101 (pipelinecommon). Explicit HEAD vs working-tree
differences that matter:

| Item | Logged/HEAD | Current working tree |
|---|---|---|
| s3 sky luminance floor | none, `dot(skyEnv,skyEnv) > 1e-6` (`HEAD:shaders/...:852`) | `skyLum > 0.10` (`shaders/...:1171-1172`) |
| grazing weight cap | none | `BLD_ENV_WEIGHT_MAX = 0.85` (`:1163`, `:1181`) |
| indoor `bldEnv.z` gate | none (`HEAD:buildingPipe.cpp:1441`) | `&& *CGame__currArea == 0` (`:2791-2793`) |
| indoor c20.x gate | none (`HEAD:buildingPipe.cpp:1230`) | `currArea==0 ? weight : 0` (`:2335`) |
| indoor s3 bind | stale `g_iblTex` (`HEAD:buildingPipe.cpp:1243`) | 1x1 neutral white (`:2128-2141`, `:2351-2362`) |
| indoor IBL refresh | `RenderIBLBuffer` runs every frame | suppressed indoors (`pipelinecommon.cpp:1068-1077`) |
| indoor ambient clamp | no `currArea` early return (`HEAD:pipelinecommon.cpp:69-116`) | early return (`:91-93`) |
| pbr floor re-clamp | re-clamps after floor (`HEAD:pipelinecommon.cpp:141-144`) | explicitly not re-clamped (`:217-222`) |
| absolute ambient floor | absent | `ambientFloor` tail (`:158-168`) |

**Consequence:** the log describes a build without any of the indoor
mitigations. Every log line quoted below is evidence about the pre-fix build,
not proof about the current source.

### 1.3 The log has no interior PBR frame

- `grep -c "interior=1"` = **0**.
- The only `[Tonemap] TC` line in the PBR window is
  `22:37:23.048 ... interior=0 hour=11 wOld=2 wNew=0 wInterp=0.82`
  (line 20718).
- PBR building frames: `PBRBuilding: frame 1` at 22:37:22.980 through
  `frame 39601` at 22:37:26.604.
- `[BPIPE] cb=PBR` occurs only twice: 22:37:24.277 (line 20948) and
  22:37:26.277 (line 21346).
- At 22:40:48 the callback is `cb=PS2 pipe=PS2(0)` (line 49421); from 22:41:45
  it is `cb=Xbox pipe=XBOX(1)`. The last minutes are not PBR.

### 1.4 The on-disk INI is not authoritative for the logged session

`E:\games\gtasa_skygfx_plus\skygfx.ini` (mtime 22:35) says:

```
buildingPipe = Xbox
vehiclePipe  = Xbox
bldSkyReflect = 0.150000
pbrIblAmbientWeight = 0.800000
pbrAmbientFloor = 0.050000
ambientFloor = 0.030000
tonemapAutoExposure = 0
pipeChainEnable = 0
```

But the 22:37 frames render `pipe=PBR(3)` and `veh=10` (`[BPIPE] cb=PBR ...
pipes(bld=3 veh=10)`, log line 20948). The running config therefore differed
from the on-disk file (in-memory mode switch, or the file was saved later from a
different state). **Do not use the ini value of `bldSkyReflect` as proof of the
failing session's value.** Log the live config instead (§9).

---

## 2. `main_building` — every view-dependent term

Entry point: `shaders/ps/VehiclePBR_Modern.hlsl:958-1200`.
Inputs: `N`/`V`/`L` from `buildingPBRVS` (`shaders/vs/buildingPBRVS.hlsl`),
`NdotV = max(dot(N,V),0)` (`:1035`), `F_atNdotV = F_Schlick(NdotV,F0)`
(`:1042`), `F0 = lerp(float3(reflectance), baseColor*reflectance,
specularTint.r)` (`:1039`).

`F_Schlick` (`shaders/include/PBR_Common.hlsl:15-19`):

```
F = F0 + (1 - F0) * (1 - cosTheta)^5
```

At grazing (`NdotV -> 0`), `F -> 1`; at face-on (`NdotV -> 1`), `F -> F0`.

### 2.1 Term table

| # | Term | Line(s) | Formula / behavior | At grazing | Can it darken? |
|---|---|---|---|---|---|
| 1 | `NdotV` / `NdotL` | `:1035-1036` | `max(dot(...),0)` | `NdotV -> 0` on a silhouette | no (input only) |
| 2 | `F_atNdotV` / `kS` / `kD` | `:1042-1044` | `F0+(1-F0)(1-NdotV)^5`; `kS=F`; `kD=1-F` | `F -> 1`, `kD -> 0` | `kD` only scales **cluster diffuse**, which is additive; the base `baseColor*ibl` is not multiplied by `kD` |
| 3 | Burley diffuse | `:1059` | `BurleyDiffuse(NdotL,NdotV,VdotH,roughness)` | `NdotV` is floored at 0.1 inside Burley (`:125`) | no, additive |
| 4 | GGX specular | `:1062-1067` | `D_GGX * F_Schlick(L·H) * V_Smith * NdotL * directCol` | `F -> 1`, but added | no, additive |
| 5 | 6-light loop | `:1070-1081` | same GGX per light | same | no, additive |
| 6 | Forward+ cluster | `:1088-1090` -> `:276-314`, `ClusterShadeOne` `:236-270` | `specAccum += ...`; `diffAccum += baseColor*...*kD` | `F -> 1`, `kD -> 0`; both added to `color` | no, additive |
| 7 | `SkyHemisphereTint` | `:92-108`, call `:1094` | `skyCol = tex2D(iblTex, uv)` by `N.y`; `ratio = (skyCol/skyL) / (ambient/ambL)`; `ratio = clamp(0.6, 1.5)`; `lerp(1, ratio, iblAmbient.x*0.15)` | sample at `N.y`; `ratio` floor 0.6 | `~5%` darkening at ini `x=0.8` (up to `~12%` at menu max `x=2.0`); **cannot black** |
| 8 | s1 material env | `:1122-1143` | `envSample = tex2D(envMapTex, CamNormal.xy*0.5+0.5)`; `color += envSample*F_atNdotV*bldEnv.y` | `F -> 1`; additive | no; a black sample adds 0 |
| 9 | s3 sky env | `:1144-1185` | `R = reflect(-V,N)`; `skyEnv = tex2D(iblTex, 0.5, saturate(R.y*0.5+0.5))`; `skyLum = dot(skyEnv, LUMA)`; `skyValid = skyLum > 0.10`; `envW = saturate(bldEnv.z * saturate(F.r) * skyValid)`; `envW = min(envW,0.85)`; `envW *= saturate(skyLum*8)`; `color = lerp(color, skyEnv, envW)` | `F -> 1`, `R` near horizontal, `R.y -> 0`, so the sample is the **horizon** of the capture | **YES — the only destructive term** |
| 10 | Screen-space normal blend | `:1006-1024` | `N = normalize(lerp(N, ssNormal, 0.3*ssValid))` | changes `N`, hence `F`, `R.y`, and every env UV | indirect amplifier, not a dark source |
| 11 | NM tangent perturbation | `:1026-1032`, `:181-217` | `nmPerturbNormal` from s8 | changes `N` | indirect amplifier |

### 2.2 The only destructive path: `:1184`

Exact current code (`shaders/ps/VehiclePBR_Modern.hlsl:1144-1185`):

```
} else if(bldEnv.z > 0.0){
    float3 R = reflect(-V, N);
    float3 skyEnv = tex2D(iblTex, float2(0.5, saturate(R.y * 0.5 + 0.5))).rgb;
    const float BLD_SKY_LUM_FLOOR  = 0.10;
    const float BLD_ENV_WEIGHT_MAX = 0.85;
    const float BLD_SKY_LUM_GAIN   = 8.0;
    float skyLum = dot(skyEnv, float3(0.2126, 0.7152, 0.0722));
    float skyValid = (skyLum > BLD_SKY_LUM_FLOOR) ? 1.0 : 0.0;
    float envW = saturate(bldEnv.z * saturate(F_atNdotV.r) * skyValid);
    envW = min(envW, BLD_ENV_WEIGHT_MAX);
    if(skyValid > 0.5)
        envW *= saturate(skyLum * BLD_SKY_LUM_GAIN);
    color = lerp(color, skyEnv, envW);
}
```

Properties:

- `bldEnv.z` is `config->bldSkyReflect`, clamped [0,1] at
  `src/core/main.cpp:5688-5690`; menu range 0..1
  (`src/core/debugmenu_ui.cpp:1154-1157`).
- At `F=1` and `skyValid=1`, `envW = min(bldSkyReflect, 0.85) * gain`.
  - `bldSkyReflect=0.15` (default): max 0.15.
  - `bldSkyReflect=1.0`: max 0.85 (current), 1.0 (HEAD, no cap).
- `gain = saturate(skyLum*8)`: full weight at `skyLum >= 0.125`; a 0.11 sample
  still gets 0.88.
- A lerp toward a black sample at `F -> 1` is **yes, possible**. It is bounded
  only by `bldSkyReflect` and the 0.10 floor.
- The current 0.10 floor rejects `skyLum <= 0.10`; it does **not** reject
  `0.11-0.25`, which is a typical indoor/night horizon. It also does not stop
  the replace-lerp from darkening a body that is brighter than the sample.
- The HEAD/logged build has `skyValid = (dot(skyEnv,skyEnv) > 1e-6)`
  (`HEAD:shaders/...:852`), so **any nonzero sample** (including 0.01) passes.
- `0.02` floor (the figure in the task): it would reject only `< 0.02`
  luminance. Interior/night horizons are commonly 0.02–0.10, so it rejects
  almost nothing and the replace-lerp still runs. **Do not use 0.02 as the
  fix.**

### 2.3 Why the other terms cannot black the screen

- `SkyHemisphereTint` returns `lerp(1, ratio, iblAmbient.x*0.15)`. With the
  current `pbrIblAmbientWeight=0.8`, the weight is 0.12; `ratio` is clamped to
  [0.6,1.5], so the minimum multiplier is `1 + (0.6-1)*0.12 = 0.952`.
  With `iblAmbient.x=0` indoors it is exactly 1.0 (assuming finite inputs).
- `s1` is additive; a dark/black env sample contributes zero, not negative.
- GGX/cluster are add-only.
- `kD=1-F` can zero the cluster diffuse at grazing, but the base
  `baseColor*SkyHemisphereTint` remains. It cannot black a wall by itself.
- `color += specTotal; color += clusterDiffuse` (`:1099-1100`) are monotonic.

**Conclusion:** if the black is produced inside `main_building`, it is line
1184 (with `color` already near zero), or it is an indirect effect of `N` being
tilted so line 1184 samples the darkest part of the capture at grazing.

---

## 3. What the IBL actually contains indoors

### 3.1 The two different resources

| Resource | Producer | Size / format | Content | Reaches `main_building`? |
|---|---|---|---|---|
| `g_iblTex` (`s3`, `iblTex`) | `RenderIBLBuffer` (`src/render/postfx.cpp:2519-2660`) via `GetIBLTexture` (`:2508`) | quarter-screen, `D3DFMT_A8R8G8B8`, one mip; log: `GetIBLTexture: OK 442x248` | procedural **DynamicSky** gradient + clouds built from timecycle `skyTop`/`skyBot` (`postfx.cpp:2597-2610`), not a scene capture | yes, sampled by `SkyHemisphereTint` (`:94`) and the sky env (`:1150`) |
| `envFB` / `reflectionTex` | `MakeEnvmapRasters` (`src/render/envmap.cpp:100`), `RenderSphereReflections` (`:1012-1062`) | 512x512 `rwRASTERTYPECAMERATEXTURE`, log: `envFB=... 512x512` | per-camera scene reflection of the current view (indoors: the interior scene) | **no** for s1: the building cb rejects `CAMERATEXTURE` at `buildingPipe.cpp:2749-2750`; used by the vehicle env path |

The 512x512 capture is not the "IBL cubemap" in the building shader. The
building shader's `iblTex` is the 442x248 (quarter-screen) LDR sky gradient.

### 3.2 `g_iblTex` content indoors

`RenderIBLBuffer` itself has **no** `currArea` gate. It reads
`CTimeCycle__m_CurrentColours.skyTopR/G/B` and `skyBotR/G/B` (postfx.cpp
2597-2610) and draws the DynamicSky shader into the quarter-screen RT. Indoors
those timecycle sky values describe no visible sky and are typically dark; the
capture is therefore a dark (but nonzero) gradient at the interior horizon.

In the **logged/HEAD build** this capture is rendered every frame indoors and
bound to s3 (`HEAD:buildingPipe.cpp:1243`), so `skyEnv` at `R.y≈0` (grazing)
reads the dark horizon. In the **current working tree**:

- `pipeEnsureIBLBuffer` returns early indoors (`pipelinecommon.cpp:1068-1077`),
  with a throttled `[Pipeline] pipeEnsureIBLBuffer: interior currArea=... — IBL
  capture suppressed`.
- The building cb binds a 1x1 opaque-white neutral texture on s3 indoors
  (`buildingPipe.cpp:2128-2141`, `:2351-2362`), `iblBound` stays false.
- `bldEnv.z` is forced 0 indoors (`buildingPipe.cpp:2791-2793`).
- `c20.x` (`iblAmbient.x`) is forced 0 indoors (`buildingPipe.cpp:2335`).
- The vehicle path forces `c20.x=0` indoors too (`vehiclePipe.cpp:3370-3377`),
  but it still binds `g_iblTex` if non-NULL (`vehiclePipe.cpp:2305-2306`).

The current gates are therefore sufficient **only if** `currArea != 0` is true
for the interior in question and the neutral bind succeeds. Two failure modes
remain:

1. `currArea == 0` false negative: garages/sheltered areas are explicitly *not*
   interiors (`postfx_colourfilter.cpp:350-356` comment: "Garages are NOT
   interiors (currArea==0) but have partial occlusion"). A dark garage/night
   interior with `currArea==0` still gets the s3 term.
2. If `bldIndoorNeutralTex` fails to create (`buildingPipe.cpp:2132-2134`),
   the `if(dev && currArea != 0)` arm is taken but no neutral is bound; s3 keeps
   whatever the previous consumer left. The shader's `SkyHemisphereTint` still
   samples it before the `c20.x` weight is applied (see H3).

### 3.3 Every `CGame__currArea` gate (current tree)

| Site | Gate |
|---|---|
| `src/rw/gta.cpp:32` | definition `int* CGame__currArea = (int*)0xB72914` |
| `src/render/pipelinecommon.cpp:91-93` | `ClampAmbientToSky` early return indoors |
| `src/render/pipelinecommon.cpp:1068-1077` | `pipeEnsureIBLBuffer` suppresses the IBL refresh indoors |
| `src/render/buildingPipe.cpp:2335` | c20.x sky-tint weight 0 indoors |
| `src/render/buildingPipe.cpp:2351-2362` | s3 neutral bind indoors |
| `src/render/buildingPipe.cpp:2791-2793` | `bldEnv.z` sky-reflect strength 0 indoors |
| `src/render/vehiclePipe.cpp:545-547` | vehicle ambient dampening uses interior/sheltered |
| `src/render/vehiclePipe.cpp:3370-3377` | vehicle c20.x 0 indoors |
| `src/render/postfx_colourfilter.cpp:318`, `:350-356` | tonemap `isInterior` / garage `isSheltered` |
| `src/render/envmap.cpp:438` | **only** the alpharef 140; the sphere reflection pass is **not** gated indoors |

Not gated: `RenderIBLBuffer` itself; `RenderSphereReflections`; the building
shader (it has no `currArea` knowledge).

### 3.4 Can a dark indoor DynamicSky capture reach `skyEnv` and be selected at grazing?

- **Logged/HEAD build: yes.** No indoor gate, no luminance floor. At grazing,
  `R.y≈0` selects the capture's horizon; `skyValid` is true for any nonzero
  sample; `lerp` replaces up to `bldSkyReflect` of the body. This is the
  strongest code-level match for "view-angle-dependent black in interiors".
- **Current tree: only if the `currArea` test is false (garage/sheltered), the
  neutral bind fails, or the build is stale.** The current indoor path is
  designed to be immune, but the shader-side floor is still only 0.10 and the
  lerp can still darken a brighter body outdoors/night (e.g. `skyLum=0.12`,
  `bldSkyReflect=1.0`, `envW≈0.85`).

---

## 4. Ambient path

### 4.1 `ClampAmbientToSky` — verified

`src/render/pipelinecommon.cpp:79-169`:

- Interior early return is real: `extern int* CGame__currArea; if(*CGame__currArea != 0) return;`
  (`:91-93`). It runs **before** the sky-luminance read, the night crush, and
  the absolute floor.
- Outdoor night crush: `skyL < 0.003` no-data bail (`:104-105`);
  `ambL > skyL` (`:112`); `k=skyL/ambL`; `nightWeight=(0.85-k)/0.85`
  (`:129-132`); `s=1-nightWeight*(1-k)` (`:134`); channels multiplied by `s`
  (`:135-137`).
- Absolute floor: `ambFloor = config->ambientFloor` (`:158`);
  additive lift to `ambFloor` (`:160-164`); default 0.03, clamp [0,0.2]
  (`src/core/main.cpp:6015-6017`).

The old build (`HEAD:src/render/pipelinecommon.cpp:69-116`) has **no**
`currArea` early return, and `GetTimecycleAmbientPBR` re-runs the clamp after
the pbr floor (`HEAD:...:141-144`), defeating the floor at night. That is the
"dark-collapse cause 1/2" the current comments describe. This is the H2
precondition.

### 4.2 Every building-ambient writer

| Writer | Line | Notes |
|---|---|---|
| `buildingAmbient = GetTimecycleAmbientPBR()` | `src/render/buildingPipe.cpp:183` | once per frame in `CustomBuildingPipeline__Update` |
| Lightning override `{1,1,1,0}` | `:185-186` | white flash only |
| `GetTimecycleAmbient()` | `pipelinecommon.cpp:171-194` | `CCoronas__LightsMult` and `ambientMult` (`:181-191`); calls `ClampAmbientToSky` (`:192`) |
| `GetTimecycleAmbientPBR()` | `pipelinecommon.cpp:196-234` | calls `GetTimecycleAmbient()`; `pbrAmbientFloor` additive (`:205-216`); no re-clamp (`:217-222`); logs `[PBR-Ambient] final=...` (`:224-233`) |
| `ambientFloor` | `pipelinecommon.cpp:158-168` | inside `ClampAmbientToSky`, additive |
| `buildingAmbient` VS uploads | `buildingPipe.cpp:1322`, `:1891`, `:2056`, `:2678` | all building cbs re-assert VS c4 |
| PS c24 ambient | `buildingPipe.cpp:2330-2331`, re-upload `:2423-2424` | RGB = buildingAmbient; `.w` = normal-buffer enable |
| `treeWind` c24 reuse | `src/render/treeWind.cpp:472-473` | reads `buildingAmbient` |

`pbrAmbientFloor` parse: `src/core/main.cpp:6065` (default 0.0, menu range
0..1, `debugmenu_ui.cpp:1091-1093`). `ambientMult`: `main.cpp:6075-6077`
(clamped [0,2]).

### 4.3 Log values

Outdoor PBR frame, 22:37:

```
20625:[22:37:22.711] [PBR-Ambient] absolute floor +0.020 (lum 0.010 -> 0.030, skyL=0.462)
20736:[22:37:23.090] [PBR-Ambient] final=(0.076,0.043,0.040) floor=0.050 raw=(0.037,0.003,0.000)
21020:[22:37:24.742] [PBR-Ambient] absolute floor +0.020 (lum 0.010 -> 0.030, skyL=0.454)
21110:[22:37:25.092] [PBR-Ambient] final=(0.076,0.043,0.040) floor=0.050 raw=(0.037,0.003,0.000)
21454:[22:37:26.731] [PBR-Ambient] absolute floor +0.022 (lum 0.008 -> 0.030, skyL=0.448)
```

Even outdoors the raw ambient blue is 0.000 and the final is ~0.076/0.043/0.040.
That is already a dim body before any interior crush; an interior frame would be
worse. The `skyL≈0.45-0.46` shows the outdoor night crush is not firing in this
sample, so this does not prove the interior clamp, but it establishes the low
ambient scale on which H1's 15-85% replace operates.

Ped values in the same PBR window (`[Ped] pAmbient override`):
`22:37:23.163 (0.392,0.363,0.340)` (line 20756),
`22:37:25.198 (0.288,0.266,0.250)` (line 21142). Peds are not starved; buildings
are the outlier.

---

## 5. Tonemap / `ColourFilter_Modern`

### 5.1 The math

`src/render/postfx_colourfilter.cpp`:

- `autoExposureOn = !identity && config->tonemapAutoExposure && luminanceReducePS && luminanceAdaptPS`
  (`:290-295`).
- Timecycle exposure: `sceneExposure = 1/max(0.50+sceneLuma*2.5,1e-7)` clamped
  [0.50,1.50] (`:362-363`); `exposure = baseExposure*carcolsAdapt*sunDampen*garageDampen`
  (`:371`); if auto-exposure is off, multiply by `sceneExposure` (`:372-373`).
- `baseExposure = max(0.3, brightness/256)` (`:311-312`); brightness slider
  minimum 0 gives 0.3, never 0.
- `cf_gain` knee (`:375-426`) is **log-only**; the real knee is in the shader
  on measured luminance (`:413-425`).
- Exposure window guard (`:505-528`): degenerate `max<=min` falls back to
  0.5/2.0; `min` floored at 0.15; `max >= min+1e-3`. c7.y/z are only read when
  `c5.w >= 0.5`.
- c5/c6/c7/c8 uploads: `:496-528`, `:530-560`.
- `Curve` outside ±0.35 is forced to identity (`:549-560`).

`shaders/ps/TonemapPass.hlsl`:

```
209: if (tonemapParams.w >= 0.5) {
212:     float lum = max(tex2D(lumTex, float2(0.5,0.5)).r, 1e-4);
213:     float key = 1.03 - 2.0 / (2.0 + log2(lum + 1.0));
214:     float autoExp = clamp(key / lum, blackLiftParams.y, blackLiftParams.z);
215:     exposure *= lerp(1.0, autoExp, saturate(blackLiftParams.w));
224:     float kneeK = saturate((lum - 0.05) / 0.20) * saturate(curveParams.w);
225:     exposure *= lerp(1.0, 0.5, kneeK);
230: c *= exposure;
241: c = FilmicTonemap(max(c, 0.0), tonemapParams.y);
256: c = c * (1.0 - blackLiftParams.x) + blackLiftParams.x;
```

`RunFrameExposure` (`postfx_luminance.cpp:168-237`) measures an 8x8 reduction of
the valid front-buffer sub-rect, then adapts 1x1 with `[PF] RunFrameExposure`
logs.

### 5.2 Can it produce a whole-screen black?

- In auto-exposure mode a dark frame gives `lum -> 1e-4`, `key/lum -> ~300`,
  clamped to `maxExp` (>= 0.15+); exposure goes **up**, not down.
- A bright wall/sky gives `key/lum` small, clamped to `minExp` (>= 0.15); the
  frame dims but does not go black.
- `exposure` is a positive product; `FilmicTonemap(0) = 0`; the black-level lift
  adds a constant. There is no subtraction to negative.
- The only way to get a zero/negative exposure is a degenerate c7 window or a
  negative `tonemapParams.x`; both are guarded (c7 window at `:519-527`,
  baseExposure `max(0.3, ...)` at `:312`). The old degenerate-window bug is
  documented and fixed at `:505-519`.
- `ColourFilter_Modern` grading (`:198-251`) is a per-channel multiply with
  `r,g,b` each clamped [0.5,2.0] (`:238-240`), plus a hue-only tint at
  `TINT_STRENGTH=0.35`; it cannot zero the frame.
- `pipeChain` can double the frame at pass 3, but `pipeChainEnable=0` in the
  on-disk ini and `[PostFX-REPORT] ... pipeChain=0` in the log (line 20726).
  The `cf_gain` log is absent, consistent with the gate being closed.

**Verdict:** tonemap/CF cannot produce a whole-screen black from a lit wall.
It can make a dim interior dimmer (exposure ~0.15-0.3), which amplifies H1/H2
but is not the trigger. Confidence 8%.

Log evidence: the only `[Tonemap] TC` line in the PBR window is
`interior=0`, `sceneLuma=0.000`, `exp=1.187`, toe 0.313
(lines 20718-20719). `tonemapAutoExposure=0` in the on-disk ini, and no
`[PF] RunFrameExposure` line appears in this window. So auto-exposure was not
the mechanism in the logged session.

---

## 6. Per-angle resources

| Resource | Per-frame? | Per-view? | Feeds `main_building`? | Dark/wrong risk |
|---|---|---|---|---|
| `g_iblTex` (quarter-screen DynamicSky) | yes outdoors; suppressed indoors in current tree | content is **not** view-dependent; the **sample UV** is (`N.y`, `R.y`) | s3: `SkyHemisphereTint` (`:94`) and sky env (`:1150`) | high: content is a dark indoor sky; at grazing `R.y≈0` reads the dark horizon |
| `envFB` / `reflectionTex` (512x512 sphere capture) | yes, `RenderSphereReflections` (`envmap.cpp:1012-1062`) | yes (current camera position/orientation; raster swap) | **no** for s1 (`CAMERATEXTURE` rejected at `buildingPipe.cpp:2749-2750`); vehicles only | dark indoors, but additive/vehicle; state-swap risk |
| Normal buffer `g_normalBufferTex` (half-res) | yes (`postfx.cpp:2873+`) | yes (depth from current view) | s4 normal blend (`:1006-1024`, bind `buildingPipe.cpp:2418-2424`) | can tilt `N` by 0.3; unit-length stale normals pass `ssValid`; this changes which sky texel is selected (amplifier) |
| PipeChain classify pack | only when `pipeChainEnable && normalBufferEnable` (`postfx.cpp:1557`) | yes | no direct sampler in `main_building`; postfx pass 3 | disabled in the logged config (`pipeChain=0`) |
| Mirror buffer | only when a mirror is active (`mirrorPass.cpp:391+`) | yes | no | not the building path |
| Cluster tile/atlas (s5-s7) | yes when Forward+ has lights | screen-tile view | yes (`ClusterLighting`) | additive only; a zero-light tile contributes nothing |

### 6.1 Sphere env pass indoors

`RenderSphereReflections` is called from the `CRenderer__ConstructRenderList`
hook (`envmap.cpp:1139`) and runs when
`iCanHasbuildingPipe && (vehiclePipe == CAR_MOBILE || CAR_ENV || CAR_MODERN)`
(`:1016`). There is **no** `currArea` gate; the only indoor branch in the file
is the alpharef 140 at `:438`. The pass therefore renders the interior scene
into the 512x512 `envFB` when the player is indoors, and the `done` log line
(`:1068`) fires.

Why it is not the building blackout by itself:

- `envFB` is created as `rwRASTERTYPECAMERATEXTURE` (`envmap.cpp:100`).
- The building cb only binds s1 when the raster type is `NORMAL` or `TEXTURE`
  (`buildingPipe.cpp:2749-2750`), so the dynamic sphere capture is rejected and
  s1 falls back to the diffuse.
- Even if it were bound, the s1 term is additive (`:1143`), so a dark sample
  adds zero.

It remains a state risk: the pass swaps the main camera rasters/clip/fog and
renders the whole scene mid-frame. A fault or incomplete restore can leave the
camera/state wrong for the next draws (H5).

### 6.2 Normal buffer per-angle effect

The building shader samples the half-res normal buffer with `vpos.xy *
screenSize.zw` (`:1006-1008`). `ssValid` checks decoded length in [0.55,1.45]
and rejects depth-alpha 1 (`:1019-1021`). Stale normals from the previous view
are still unit length and pass; the shader then tilts `N` by up to 0.3
(`:1023`). At grazing this can move `R.y` toward the horizon (dark) and
`NdotV` toward 0 (F -> 1). It is an amplifier of H1, not a dark source.

---

## 7. Log evidence (quoted)

### 7.1 Build / session

```
27:[22:35:22.707] [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

### 7.2 PBR callback

```
20948:[22:37:24.277] [BPIPE] cb=PBR pipe=PBR(3) ivMode=0 fp=1 VS=IDR_BUILDINGPBRVS PS=IDR_BUILDINGPBRPS ...
21346:[22:37:26.277] [BPIPE] cb=PBR pipe=PBR(3) ivMode=0 fp=1 ...
```

Only two PBR callback banners; the rest of the PBR session is per-atomic
`PBRBuilding` lines.

### 7.3 IBL capture

```
20698:[22:37:22.975] [PF] IBLBuffer: DynamicSky=23D5E150 out=g_iblTex(00000000) qres quarter-screen
20700:[22:37:22.978] RenderIBL: OK tex=2A9F1168 surf=2A492DD0
21104:[22:37:25.034] [PF] IBLBuffer: DynamicSky=23D5E150 out=g_iblTex(2A9F1168) qres quarter-screen
```

- The capture is created mid-frame at 22:37:22.977 (`GetIBLTexture: OK
  442x248`, line 20699).
- The first vehicle PBR draw at 22:37:22.927 had `iblTex=00000000` (line 20669);
  the next at 22:37:24.922 has `iblTex=2A9F1168` (line 21074). This is a
  first-frame unbound-s3 window, not a per-angle cause.
- The capture is not logged as dark/indoor; it is a quarter-screen gradient.
  No `ibl_indoor` message exists because that log was added after the logged
  build.

### 7.4 Environment sphere pass

```
20656:[22:37:22.906] [EnvMap] MakeEnvmapRasters: size=512 (envMapSize=512 reflectionMsaaLevel=0 mirrorDistortionFix=0) target=envFB(048C11C8 256x256) ...
20697:[22:37:22.972] [EnvMap] RenderSphereReflections done: envFB=26555BE8 512x512 refTex=048C4390 raster=26555BE8
21064:[22:37:24.903] [EnvMap] MakeEnvmapRasters: size=512 ... target=envFB(26555BE8 512x512) ...
```

The sphere pass **did run** in the PBR session, but the session is outdoors
(`interior=0`). There is no log line proving it ran indoors; the code has no
indoor gate, so it will.

### 7.5 Tonemap / ambient / peds

```
20718:[22:37:23.048] [Tonemap] TC sceneLuma=0.000 amb=(0.037,0.003,0.000) dir=(0.043,0.004,0.000) shadow=231 fog=13 cloud=1.00 sun=0.00 street=0.18 hour=11 wOld=2 wNew=0 wInterp=0.82 cutscene=0 interior=0
20719:[22:37:23.050] [Tonemap] VAL exp=1.187 toe=0.313 bl=0.0000 ct=1.00 br=0.000 lift=0.000 curve=0.00
20736:[22:37:23.090] [PBR-Ambient] final=(0.076,0.043,0.040) floor=0.050 raw=(0.037,0.003,0.000)
20756:[22:37:23.163] [Ped] pAmbient override: (0.392,0.363,0.340)
21142:[22:37:25.198] [Ped] pAmbient override: (0.288,0.266,0.250)
20726:[22:37:23.061] [PostFX-REPORT] frame=8921 filter=8 pipeline=0 ... normalBuf=1 pipeChain=0 ...
```

Answers:

- **Is the IBL capture logged as dark/indoor?** No. The log has no `interior=1`
  frame and no `ibl_indoor`/`BldIBL` message (those strings are in the current
  source, not the logged binary). The code proves the capture is built from
  timecycle sky colors, which are the interior values when indoors.
- **Does the sphere env pass run indoors?** The code says yes
  (`envmap.cpp:1016-1062`, no `currArea` gate); the log only proves it runs in
  the outdoor PBR session.
- **Ambient:** the building ambient is already low outdoors
  (`final=(0.076,0.043,0.040)`, raw blue 0.000). This is the dim base on which
  the grazing sky term operates. The ped override is much higher (0.29-0.39),
  so the building path is not representative of the other pipes.

---

## 8. Ranked hypotheses, minimal fixes, and proof

### H1 — Grazing s3 sky lerp toward a dark indoor IBL capture (40%)

**Mechanism.** In an interior, `g_iblTex` is the DynamicSky gradient built from
the interior timecycle `skyTop`/`skyBot` (dark). At grazing, `F_atNdotV -> 1`
and `R.y -> 0`, so `skyEnv` is the dark horizon. `skyValid` is true for any
sample above the floor (none in HEAD; 0.10 now). `color = lerp(color, skyEnv,
envW)` (`:1184`) replaces up to `bldSkyReflect` of the already-dim body with the
dark sample. At the default 0.15 this is a 15% darkening; at a live
`bldSkyReflect` near 1.0 it is 85% (current cap) / 100% (HEAD). Random view
angles change `NdotV` and `R.y`, selecting the dark horizon only at some
angles — exactly the reported symptom.

**Exact lines.**

- Shader: `shaders/ps/VehiclePBR_Modern.hlsl:1149-1184`; the destructive op is
  `:1184`; floor/weight `:1162-1183`.
- HEAD: `HEAD:shaders/ps/VehiclePBR_Modern.hlsl:849-857` (no luminance floor).
- CPU: `src/render/buildingPipe.cpp:2791-2795` (current `currArea` gate);
  `HEAD:src/render/buildingPipe.cpp:1441-1443` (no gate).
- Capture: `src/render/postfx.cpp:2597-2610`; `:2508`; `:2519-2660`.
- IBL refresh: `src/render/pipelinecommon.cpp:1068-1077` (current gate),
  `:1085-1093` (render).

**Minimal fix (shader-side luminance + no-darken guarantee).**

Preferred patch at `:1171-1184`:

```hlsl
float skyLum  = dot(skyEnv, float3(0.2126, 0.7152, 0.0722));
float bodyLum = dot(color,  float3(0.2126, 0.7152, 0.0722));
// Only a capture at least as bright as the body may replace it.
float skyValid = (skyLum > max(BLD_SKY_LUM_FLOOR, bodyLum)) ? 1.0 : 0.0;
float envW = saturate(bldEnv.z * saturate(F_atNdotV.r) * skyValid);
envW = min(envW, BLD_ENV_WEIGHT_MAX);
if(skyValid > 0.5) envW *= saturate(skyLum * BLD_SKY_LUM_GAIN);
// max() per channel: the sky can brighten/tint, never darken.
color = lerp(color, max(color, skyEnv), envW);
```

If the look must keep a replace (not max), the `max(bodyLum)` gate alone
guarantees a dark sample can never win at grazing. Raising the floor from 0.10
to 0.20 is a second, weaker option; **do not use 0.02** (it rejects only
near-black). Keep `BLD_ENV_WEIGHT_MAX=0.85` and consider lowering
`bldSkyReflect`'s menu max from 1.0 to ~0.35.

**CPU side.** Keep the current `currArea` gate and add the sheltered/garage
case used by the tonemap (`CCullZones__PlayerNoRain()`; see
`postfx_colourfilter.cpp:350-356`):

```cpp
float skyRefl = (config->bldEnvReflect && envWeight <= 0.0f && iblBound
                 && *CGame__currArea == 0
                 && !CCullZones__PlayerNoRain())
                ? config->bldSkyReflect : 0.0f;
```

**A/B proof.**

1. Same interior, same angle: set `bldSkyReflect=0` (and then
   `bldEnvReflect=0`). If the blackout disappears, H1 is confirmed. This is a
   one-line INI test with no rebuild.
2. Add a throttled `[BldEnv]` log (see §9) printing `currArea`, live
   `bldSkyReflect`, `bldEnv.y/z`, `iblBound`, `buildingAmbient`, and the
   s3 sample luminance. If the failing frame shows `bldEnv.z > 0` indoors or
   `skyLum` in 0.10-0.25, H1 is confirmed without guessing.
3. If a GPU readback is too much, temporarily write `skyLum`/`envW` to the
   output alpha (debug c49 flag) and inspect a screenshot.

---

### H2 — Indoor ambient crush leaves the body near-black (30%)

**Mechanism.** The HEAD/logged `ClampAmbientToSky` has no interior early return
and re-clamps after `pbrAmbientFloor`; at an interior sky of 0.02-0.05 and an
ambient of 0.05-0.1 the night law scales the ambient by `k ≈ 0.2-0.5`, and the
re-clamp defeats the added floor. The result is a body of a few percent, on
which H1's 15% dark blend is enough to read as black. This is not
view-dependent by itself; it is the precondition that makes H1's angle
dependence visible.

**Exact lines.**

- `src/render/pipelinecommon.cpp:79-169`; interior early return `:91-93`;
  floor `:158-168`; `GetTimecycleAmbient` call `:192`;
  `GetTimecycleAmbientPBR` floor/no-re-clamp `:205-222`.
- HEAD: `HEAD:src/render/pipelinecommon.cpp:69-116` (no early return);
  `HEAD:...:141-144` (re-clamp after floor).
- Consumers: `src/render/buildingPipe.cpp:183`, `:2330-2331`,
  `:1322/1891/2056/2678`.
- Config: `src/core/main.cpp:6015-6017`, `:6065`, `:6075-6077`.

**Minimal fix.** The current working tree already has the right shape:
`currArea` early return, no post-floor re-clamp, additive `ambientFloor` tail.
Ship/verify it. If a residual interior crush remains, make the interior branch
explicitly floor the final ambient:

```cpp
if(*CGame__currArea != 0){
    float ambFloor = config ? config->ambientFloor : 0.03f;
    float lum = 0.2126f*c.red + 0.7152f*c.green + 0.0722f*c.blue;
    if(lum < ambFloor){ float add = ambFloor - lum; c.red=min(c.red+add,1); ... }
    return; // no sky clamp indoors
}
```

**A/B proof.** In the failing interior, set `ambientFloor=0.2` and
`pbrAmbientFloor=0.2`. If the blackout disappears or becomes grey, H2 is
confirmed. The `[PBR-Ambient] final=...` line should show the interior ambient
before/after. If it is already >0.05 and the blackout persists, H2 is not
sufficient (look at H1).

---

### H3 — `SkyHemisphereTint` multiplies the body by a dark-capture ratio (10%)

**Mechanism.** `SkyHemisphereTint` samples s3 by `N.y` (`:93-94`) and returns a
chromatic ratio clamped [0.6,1.5] (`:106`), blended by `iblAmbient.x*0.15`
(`:107`), then `color = baseColor * ibl` (`:1098`). In HEAD, `iblAmbient.x` is
not gated indoors (`HEAD:buildingPipe.cpp:1230`), so a dark/colored indoor
capture tints the body even when the s3 sky lerp is off. The floor 0.6 limits
this to ~5% darkening at the in-game weight 0.8 (up to ~12% at the menu max
2.0), so it cannot black a frame alone; it is a secondary darkener and a hue
shift.

**Exact lines.** `shaders/ps/VehiclePBR_Modern.hlsl:92-108`, `:1094`,
`:1098`; CPU `src/render/buildingPipe.cpp:2335` (current gate),
`HEAD:src/render/buildingPipe.cpp:1230` (no gate); vehicle
`src/render/vehiclePipe.cpp:3370-3377`.

**Minimal fix.** Early-out before sampling and make the tint brighten-only:

```hlsl
float3 SkyHemisphereTint(float3 N){
    if(iblAmbient.x <= 0.0) return float3(1.0,1.0,1.0);
    float3 skyCol = tex2D(iblTex, ...).rgb;
    if(!(dot(skyCol, skyCol) > 1e-6)) return float3(1.0,1.0,1.0);
    ...
    ratio = clamp(ratio, 1.0, 1.5);   // was [0.6,1.5]
}
```

The `!(...)` form also rejects NaN (a plain `<=` does not). The LDR A8R8G8B8
capture makes NaN unlikely, but it is free.

**A/B proof.** Set `pbrIblAmbientWeight=0` in the same interior. If the blackout
is unchanged, H3 is not the cause (it is at most a small darkening). If it
lightens but does not disappear, H3 is secondary to H1/H2.

---

### H4 — Tonemap / `ColourFilter_Modern` exposure collapse (8%)

**Mechanism.** Auto-exposure or a degenerate exposure window could dim the
whole frame; a dark wall filling the frame lowers measured luminance and would
normally **raise** exposure, not lower it. The code clamps the window to a
minimum 0.15 and `baseExposure >= 0.3`; `FilmicTonemap(0)=0`; black lift adds a
constant. It cannot make a lit wall black.

**Exact lines.** `src/render/postfx_colourfilter.cpp:290-295`, `:318`,
`:327-340`, `:358-373`, `:375-426`, `:458-492`, `:496-528`, `:530-560`;
`shaders/ps/TonemapPass.hlsl:209-230`, `:241`, `:256`;
`src/render/postfx_luminance.cpp:168-237`.

**Minimal fix.** Floor the exposure product before upload:
`exposure = max(exposure, 0.25f)` and force `autoExposureOn=false` when
`*CGame__currArea != 0` (interior timecycle already encodes the mood). Keep the
0.15 window guard.

**A/B proof.** Set `tonemapAutoExposure=0`, `tonemapMinExposure=0.5`,
`tonemapBlackLift=0.05`. If the black frame is unchanged, H4 is out. If
`colorFilterEnable=0` changes it, investigate the grading pass; if not, it is
not the filter.

The log supports "not H4 in the recorded session": `tonemapAutoExposure=0` in
the ini, no `[PF] RunFrameExposure` line, `[Tonemap] VAL exp=1.187` outdoors
(line 20719). The caveat is that the logged session is not an interior.

---

### H5 — Normal-buffer tilt / sphere-env state (6%)

**Mechanism.** The half-res normal buffer is rendered from the current camera
and sampled by screen UV (`:1006-1024`). Stale unit-length normals pass
`ssValid` and tilt `N` by 0.3, moving `NdotV` and `R.y`; this can push a
borderline grazing pixel over the sky-lerp threshold. The 512x512 sphere pass
also runs mid-frame indoors with no `currArea` gate and swaps the main camera
rasters; a fault/incomplete restore is a state risk, although the building s1
bind rejects its `CAMERATEXTURE` raster.

**Exact lines.** `src/render/buildingPipe.cpp:2418-2424` (normal-buffer bind),
`:2749-2750` (raster guard), `:2208` (cull re-assert);
`shaders/ps/VehiclePBR_Modern.hlsl:1006-1024`;
`src/render/envmap.cpp:100`, `:438`, `:1012-1062`, `:1139`;
`src/render/postfx.cpp:2717-2738`, `:2873+`.

**Minimal fix.** A/B first: `normalBufferEnable=0`; if the blackout changes,
the normal-buffer tilt is involved. Add an indoor gate to the sphere pass:

```cpp
void RenderSphereReflections(void){
    extern int* CGame__currArea;
    if(*CGame__currArea != 0) return;  // interiors: no per-view sphere capture
    ...
}
```

and log `[EnvMap] interior skip` once. This is primarily a vehicle-env
correctness/state fix, not a building-color fix.

**A/B proof.** Toggle `normalBufferEnable`; then `envMapSize`/vehicle env or
the new indoor skip. If the building blackout is unchanged with both, H5 is
out.

---

### H6 — Cull / portal / black-void geometry (4%)

**Mechanism.** At some view angles a wall/portal can be culled (or the camera
sees the backface/void), exposing the black interior background. This is
view-angle-dependent but not a PBR color term. The env pass and mirror pass
swap cull/raster state; the building cb re-forces cull at `:2208`, but a fault
or a missed restore can leave the state wrong.

**Exact lines.** `src/render/buildingPipe.cpp:2208`;
`src/render/pipelinecommon.cpp:1306-1328`; `src/render/mirrorPass.cpp:365-386`;
`src/render/envmap.cpp:400-478` (state audit).

**Minimal fix / proof.** Switch `buildingPipe` to PS2 or Xbox (`buildingPipe=PS2`
/ `Xbox`) in the same interior/angle. If the blackout persists, it is not
`main_building` (H6/postfx/geometry). If it disappears, stay on the shader
track. Also capture `[Cull]`, `[CullResync]`, and `[EnvSwap]` lines around the
failing frame.

---

### H7 — NaN / uninitialized IBL sample (2%)

**Mechanism.** An unbound/undefined s3 sample could produce NaN, which would
propagate through `SkyHemisphereTint` and render black. Mostly ruled out:

- `g_iblTex` is `D3DFMT_A8R8G8B8` (`postfx.cpp:2508`) — an 8-bit unorm
  texture cannot store NaN.
- The texture is created and rendered before the first PBR building draw in the
  log (`GetIBLTexture: OK` 22:37:22.977, first `PBRBuilding` 22:37:22.980).
- An unbound s3 typically returns black (0,0,0), and the shader's `dot <= 1e-6` guard
  (or the current `skyLum > 0.10`) rejects it.
- Current indoor path binds 1x1 white on s3.

Residual: if `bldIndoorNeutralTex` creation fails, the code takes the indoor
arm but binds nothing (`buildingPipe.cpp:2351-2362`); and HEAD binds a stale
`g_iblTex` with no neutral. Harden the shader guard anyway:

```hlsl
if(!(dot(skyCol, skyCol) > 1e-6)) return float3(1.0,1.0,1.0);
```

A/B: none needed beyond the H1 test; if H1's no-darken fix removes the
blackout, H7 is moot.

---

## 9. Instrumentation to add (cheap, decisive)

Add these before further shader tuning; they convert the guesses above into
evidence.

### 9.1 CPU: `[BldEnv]` per interior frame

In `CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR` (near the c47 upload,
`buildingPipe.cpp:2794`), throttled with `dbglog_throttle("bld_env")`:

```
[BldEnv] currArea=%d sheltered=%d bldSkyReflect=%.3f bldEnv=(%.2f,%.2f,%.2f)
         iblBound=%d c20x=%.2f amb=(%.3f,%.3f,%.3f) normalBuf=%d
```

This proves whether the failing frame had `bldEnv.z > 0` indoors, which is the
H1 trigger.

### 9.2 CPU: `[Ambient]` interior path

In `ClampAmbientToSky` before the early return and in
`GetTimecycleAmbientPBR` after the floor, log `currArea`, `skyL`,
`ambL`, `ambientFloor`, `pbrAmbientFloor`, and whether the clamp was skipped.
The existing `[PBR-Ambient] final=...` line does not include `currArea`.

### 9.3 CPU: sphere pass indoor proof

At the top of `RenderSphereReflections` (`envmap.cpp:1012`), log once per
2-second window:

```
[EnvMap] interior pass: currArea=%d vehiclePipe=%d envFB=%dx%d
```

and the same at the indoor early return if H5's gate is added.

### 9.4 GPU: sample luminance / weight

Add a debug constant `c49` (unused) or reuse an unused lane:
`c49 = {debugMode, 0,0,0}`. In `main_building`, if `debugMode > 0.5`, output:

```
return float4(skyLum, envW, skyValid, 1.0);
```

or write `skyLum` into alpha. A screenshot at the failing angle then shows
whether `skyLum` is 0.01 (H1) or `envW` is 0 (gates working). This is the
single most direct proof for H1.

### 9.5 Existing tags to watch

- `[Pipeline] pipeEnsureIBLBuffer: interior currArea=... — IBL capture suppressed`
- `[BldIBL] indoor gate active — s3 neutral bind`
- `[PBR-Ambient] final=...`
- `[Tonemap] TC ... interior=%d`
- `[EnvMap] RenderSphereReflections done`
- `[PostFX-REPORT] ... pipeChain=%d normalBuf=%d`

---

## 10. Recommended sequence

1. **Deploy the current working tree first.** The logged ASI is the 21:56
   build; the current source already contains the interior ambient early
   return, the `pbrAmbientFloor` no-re-clamp, the IBL indoor suppression, the
   neutral s3 bind, the `currArea` c47/c20 gates, and the 0.10 floor. Verify the
   new log contains:
   - `[Pipeline] pipeEnsureIBLBuffer: interior currArea=... — IBL capture suppressed`
   - `[BldIBL] indoor gate active — s3 neutral bind`
   - no `bldEnv.z > 0` indoors.
   If the symptom is gone, the plan is a verification/regression task, not a
   new shader fix. Do not "fix" already-fixed code.
2. If it persists, run the H1 A/B (`bldSkyReflect=0`, `bldEnvReflect=0`) and the
   H2 A/B (`ambientFloor=0.2`, `pbrAmbientFloor=0.2`) in the same interior and
   angle. These two tests split the causal chain.
3. Apply the shader no-darken patch at `:1184` (H1) regardless; it is a
   correctness invariant: an environment capture must never reduce the body's
   luminance at grazing. Keep the `BLD_ENV_WEIGHT_MAX=0.85` cap.
4. Add the sheltered/garage `CCullZones__PlayerNoRain()` term to the CPU
   `skyRefl` gate (H1) and the `[BldEnv]` log.
5. If H2 survives the new build, floor the interior ambient explicitly
   (never below `ambientFloor`) and log it.
6. Only then investigate H4/H5/H6 with the A/B matrix above.
7. Add a regression screenshot at a known interior grazing angle plus a log
   assertion: `currArea != 0 => bldEnv.z == 0 && c20.x == 0`.

---

## Appendix A — constants and formulas

| Name | Value | Source |
|---|---|---|
| `BLD_SKY_LUM_FLOOR` | **0.10** (current); none in HEAD; 0.02 only in a comment | `shaders/ps/VehiclePBR_Modern.hlsl:1162`, `:1153`; `HEAD:...:852` |
| `BLD_ENV_WEIGHT_MAX` | 0.85 | `:1163`, `:1181` |
| `BLD_SKY_LUM_GAIN` | 8.0 (full at 0.125) | `:1164`, `:1183` |
| `bldSkyReflect` | default 0.15, clamp [0,1] | `main.cpp:5688-5690`, `debugmenu_ui.cpp:1154-1157` |
| `pbrIblAmbientWeight` | default 0.5, ini 0.8, tint weight `x*0.15` | `main.cpp:6078`, `shader:107` |
| `ambientFloor` | default 0.03, clamp [0,0.2] | `main.cpp:6015-6017` |
| `pbrAmbientFloor` | default 0.0, ini 0.05, menu [0,1] | `main.cpp:6065`; `debugmenu_ui.cpp:1091-1093` |
| `F_Schlick` | `F0 + (1-F0)(1-cos)^5` | `PBR_Common.hlsl:15-19` |
| `SkyHemisphereTint` ratio | `(skyCol/skyL)/(ambient/ambL)`, clamp [0.6,1.5], weight `iblAmbient.x*0.15` | `VehiclePBR_Modern.hlsl:92-108` |
| `g_iblTex` | quarter-screen, A8R8G8B8, DynamicSky from timecycle | `postfx.cpp:2508`, `:2597-2610` |
| `envFB` | 512x512, `rwRASTERTYPECAMERATEXTURE` | `envmap.cpp:100` |

## Appendix B — file/line index

- Shader: `shaders/ps/VehiclePBR_Modern.hlsl:92-108`, `:958-1200`,
  `:999-1044`, `:1059-1067`, `:1088-1090`, `:1094-1100`, `:1122-1186`,
  `:1197-1199`.
- VS: `shaders/vs/buildingPBRVS.hlsl:1-130` (`Color`/ambient, `CamNormal`,
  `ViewDir`).
- Building cb: `src/render/buildingPipe.cpp:155-187`, `:1322`, `:1891`,
  `:2056`, `:2208`, `:2330-2370`, `:2418-2424`, `:2678`, `:2749-2750`,
  `:2774`, `:2791-2795`.
- Ambient: `src/render/pipelinecommon.cpp:79-169`, `:171-234`.
- IBL: `src/render/pipelinecommon.cpp:1020-1097`;
  `src/render/postfx.cpp:2467-2660`.
- Env sphere: `src/render/envmap.cpp:75-170`, `:400-478`, `:1012-1062`, `:1139`.
- Tonemap: `src/render/postfx_colourfilter.cpp:290-560`;
  `src/render/postfx_luminance.cpp:168-237`;
  `shaders/ps/TonemapPass.hlsl:201-263`.
- Config: `src/core/main.cpp:5688-5690`, `:6015-6017`, `:6065`, `:6075-6077`,
  `:5743-5749`; `src/core/debugmenu_ui.cpp:1088-1093`, `:1154-1157`.
- Globals: `src/rw/gta.cpp:32` (`CGame__currArea`), `:49-55`
  (`GetSunDirection`).
