# Log forensics — interior spawn, 2026-10-26

Read-only analysis. Source was not modified. Only this file was created.

- Log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
- Analysis cutoff: **2026-10-26 04:30:28** (40,867 lines; the log was still live when sampled, so later lines are ignored)
- Current run: first line `04:22:36.297`, last analyzed line `04:30:28.218`
- Previous log: `skygfx_dbg.prev.log`, run `01:12:26.118`–`01:25:52.873` (50,979 lines). Different run; not the 04:10–04:30 test.
- Log file: mtime **04:30:26.717**, size **4,286,456 B** (last fs stat); 40,867 lines at cutoff (still being appended).

## 1. Run / build identification

| File | `[BUILD]` size | asi mtime | built | run first/last | Verdict |
|---|---|---|---|---|---|
| `skygfx_dbg.log` | **2,399,744** | 2026-10-26 04:15:13 | Oct 26 2026 04:14:57 | 04:22:36.297–04:30:28.218 | **Target run: freshly deployed build** |
| `skygfx_dbg.prev.log` | 2,395,136 | 2026-10-26 01:11:54 | Oct 26 2026 01:11:06 | 01:12:26.118–01:25:52.873 | Older build/run |

Verbatim current build line (line 28):

```text
[04:22:36.323] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2399744 mtime=2026-10-26 04:15:13 built=Oct 26 2026 04:14:57
```

This is the fresh **2,399,744 B** build, built at 04:14:57 (after 02:30), not the older 2,395,136 B build. The interior spawn is in this current run. No 04:10-start run exists in either log; the 04:22:36 run is the only candidate in the 04:10–04:30 window.

### Scene windows used

| Window | Timestamps | Lines | Evidence |
|---|---|---|---|
| **Interior** | 04:22:52.314–04:25:26.738 | 223–3888 | CJ/safehouse textures (`CJ_CHROME2`, `CJ_BACK_BOARD`), `lw_seat2`, `AH_*`, `la_carp3`; building camera raster `1DEFAC90 2048x1024`; PBR ambient raw 0; ped ambient fixed 0.200 |
| **Outdoor early (clean, Xbox)** | 04:25:29.001–04:25:43.548 | 4051–7432 | First `[SKY]`/`[CoronaDraw]`/`[VPIPE]`; vegetation; screen raster `1920x1080`; far clip 1800; raw PBR ambient nonzero |
| **Outdoor late (mode-cycle)** | 04:26:09.214–04:26:34.510 | 11984–14948 | `[Tonemap]`, `[water]`, `[EnvMap]`, PBR/Modern/PS2/GTAIV paths; includes mode-menu switching |

Transition evidence: `veg_largefurs05/06` first at **04:25:26.918**; camera raster changes to `03F1EFC0 1920x1080` at **04:25:27.513**; far clip changes 1600→1800 at **04:25:29.527**; ped ambient changes `(0.200,0.200,0.200)`→`(0.200,0.140,0.081)` at **04:25:29.519**; first outdoor PBR raw ambient at **04:25:29.481**.

## 2. Full tag census

Counts are per message tag in the windows above. `makePS(...)` is collapsed. Some tags are `dbglog_once`/first-N/throttled, so absence in a short window is not proof of non-execution.

```text
tag                                  I       O       L
XboxBuilding                      1366    3018     839
PERF                                 0       0     715
[PF]                               368      63     178
[PostFX]                           216      36     108
[Pipeline]                         213      36      77
Ragdoll                            262      20      15
PS2Building                          0       0     204
PBRBuilding                          0       0     194
[VehiclePBR]                         0       0     154
makePS(...)                        146       0       0
[MirrorPass]                        84      14      24
[PipeAlpha]                         79      14      20
[FxAlpha]                           88       9      12
[PED]                               66       8      12
[Cull]                              42      15      26
[PBR-Ambient]                       42      13      26
[BPIPE]                             42       7      27
[PedOpaque]                         44       7      13
[Ped]                               43       7      13
[shadow_contract]                   43       7      13
[BldZW]                             42       7      13
[CoronaBlend]                       42       7      12
[CoronaShield]                      42       6      13
[CullResync]                        42       7      12
[BldElide]                          40       7      13
[BLIT]                              42       7       7
texalpha                            26      23       7
[PedRS]                             37       5       9
[CoronaFeed]                        42       4       4
[BUILDING]                          40       0       7
[FX]                                 1       0      36
[ForwardPlus]                        0       0      29
rs_mon_report                       19       3       5
[BldTex]                            24       0       0
[VPIPE]                              0       8      14
[SKY]                                0       8      12
[VehOpacity]                         0       0      20
[EnvMap]                             0       0      19
[BldXbox]                           16       0       0
[CoronaDraw]                         0       8       5
CreateShaders                       12       0       0
[Mode]                               0       0      12
[PostFX-REPORT]                      5       3       4
[VehElide]                           0       0      11
[SMAA-DIAG]                          0       0      10
[VehTex]                             0       3       7
DepthHook                            6       0       0
RenderScene_before                   6       0       0
[Dump]                               5       0       0
[SMAA]                               0       0       5
[probe]                              5       0       0
[RENDER]                             4       0       0
ReleaseDefaultPoolResources          2       0       0
ReleaseSMAAStaticResources           2       0       0
[CoronaExit]                         2       0       0
[INIT]                               1       0       1
[ModeUI]                             0       0       2
[PedDual]                            2       0       0
[Tonemap]                            0       0       2
[UW]                                 2       0       0
ColourFilter_switch                  1       0       0
DepthHook_Install                    1       0       0
DepthHook_ReleaseResources           1       0       0
ForwardPlus_CullAndUpload            1       0       0
GetIBLTexture                        0       0       1
RenderIBL                            0       0       1
[CloudShield]                        0       1       0
[Config]                             0       0       1
[DefCB]                              1       0       0
[DefPipe]                            1       0       0
[FlushShield]                        0       1       0
[Menu]                               0       0       1
[Noise]                              1       0       0
[PedAlphaTest]                       1       0       0
[PedCull]                            1       0       0
[PedTex]                             1       0       0
[ShadowHeal]                         1       0       0
[ShadowTex]                          1       0       0
[wx_amb]                             1       0       0
```

## 3. Verbatim first occurrences

Line numbers are from the 04:30:28 snapshot. Quotes use the log's UTF-8 em dash.

```text
61   [04:22:36.990] [INFO ] :0 () - EDED: atomic plugin attached offset=136 (forceWindShader=0)
     EDED: RpAtomicRegisterPlugin FAILED — ABSENT (zero occurrences of RpAtomicRegisterPlugin)
270  [04:22:52.417] [INFO ] :0 () - [BldZW] entry rw=1 drv=1 dev=0 -> restore=TRUE(canonical)
4054 [04:25:29.006] [INFO ] :0 () - [FlushShield] restore path=SpriteFlush_70CF20/clouds states=9 blend=1 src=5 dst=6 zw=0 ref=0
     flushshield_restore — ABSENT as a literal line (it is the dbglog_once key; the emitted line is the [FlushShield] restore above)
304  [04:22:52.478] [INFO ] :0 () - [ShadowTex] rw=1C029E48 dev=11C273A8 match=1
255  [04:22:52.356] [INFO ] :0 () - [ShadowHeal] L2+L3 pinned to cache: dev(ref=2 func=5 blend=0 zw=0 fog=0 tex=00000000) -> rw(ref=2 func=5 blend=0 zw=0 fog=0 tex=00000000)
263  [04:22:52.366] [INFO ] :0 () - [BldTex] tex='lw_seat2' null=0 bound=1BF5C994 blend=0 zw=1
     [BldOpacity] — ABSENT (zero occurrences)
262  [04:22:52.364] [INFO ] :0 () - [BldXbox] tex='lw_seat2' tac=1 blendOn=0 matA=255 zw=1
223  [04:22:52.314] [INFO ] :0 () - [PBR-Ambient] final=(0.050,0.050,0.050) floor=0.050 raw=(0.000,0.000,0.000)
238  [04:22:52.335] [INFO ] :0 () - [Ped] pAmbient override: (0.200,0.200,0.200)
237  [04:22:52.334] [INFO ] :0 () - [Ped] ped_amb_cmp pre=(0.000,0.000,0.000) tc=(0.050,0.050,0.050) post=(0.200,0.200,0.200) floor=0.200
12043 [04:26:09.345] [INFO ] :0 () - [Tonemap] TC sceneLuma=0.000 amb=(0.043,0.017,0.003) dir=(0.051,0.020,0.004) shadow=232 fog=10 cloud=1.00 sun=0.00 street=0.20 hour=12 wOld=2 wNew=0 wInterp=0.17 cutscene=0 interior=0
     [CF] — ABSENT (zero occurrences; colour filter ran as [PostFX] ColourFilter_Modern / ColourFilter_switch)
360  [04:22:52.564] [INFO ] :0 () - [PF] pf_gate: mask=0x00DF5 normalBuf=1 vel=0 ssao=1 depthHist=0 pipeChain=1 sunShafts=1 dof=1 bloom=1 ca=1 grain=0 vignette=1 ivGrade=1 motionBlur=0 heightFog=0 godRays=0 smaa=0 fxaa=0
15174 [04:26:35.807] [INFO ] :0 () - [PF] UpdateFrontBuffer FAILED hr=0x8876086C
10348 [04:26:00.829] [INFO ] :0 () - [water] detail normal tex 256x256 mips=1
4052 [04:25:29.002] [INFO ] :0 () - [CoronaDraw] pre-force dev blend=1 src=5 dst=6 at SpriteFlush_70CF20 (D3DBLEND: 2=ONE 5=SRCALPHA 6=INVSRCALPHA; black-quad=blend 0)
     [BUILDING] ZWRITE off/desynced — ABSENT (zero occurrences; building receipt is [BldZW])
     [VEHICLE] ZWRITE — ABSENT (zero occurrences)
232  [04:22:52.328] [INFO ] :0 () - [PED] ZWRITE off/desynced on entry (rw=0 dev=0) — forcing TRUE for cb
227  [04:22:52.323] [INFO ] :0 () - [Cull] enter saved=1 forced=1
207  [04:22:40.429] [INFO ] :0 () - [EnvMap] MakeEnvmapRasters: size=256 (envMapSize=256 reflectionMsaaLevel=0 mirrorDistortionFix=0) target=envFB(00000000 0x0) envZB(00000000) reflectionCam(03F1C248 raster=00000000 z=00000000) reflectionTex(03F24390)
33   [04:22:36.326] [INFO ] :0 () - [MirrorPass] hook installed at 0x53EABA (CMirrors::RenderMirrorBuffer call site), mirrorDistortionFix=0
```

Context: first real `[EnvMap]` render target is 04:25:54.114 (`size=256`), then 04:26:09.223 (`size=512`); first `[water]` chain is 04:26:00.835 (`interior=0`); `[PF] UpdateFrontBuffer` is the only failure-tagged `[PF]` line (normal emissions are `[PostFX] UpdateFrontBuffer ...`; the failure is immediately preceded by a `SKIP` at 04:26:35.805).

## 4. `[BPIPE]` and wind

First five occurrences, verbatim:

```text
268  [04:22:52.386] [INFO ] :0 () - [BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 VS=IDR_XBOXBUILDINGVS PS=simplePS wind(tilt=0 c21c22=0 ext=0.120/0.000/0.000/1) treeAlpha(act=0 en=1 prof=0 mul=1.000 ps=0CFCD890) handles pbr=0D0196C8/0D01A5A0 iv=0CFCEB50/0D019858 fp=00000000/00000000
542  [04:22:54.517] [INFO ] :0 () - [BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 VS=IDR_XBOXBUILDINGVS PS=simplePS wind(tilt=0 c21c22=0 ext=0.120/0.000/0.000/1) treeAlpha(act=0 en=1 prof=0 mul=1.000 ps=115F1F78) handles pbr=1F0562C8/1F055FA8 iv=1F055E18/1F055AF8 fp=00000000/00000000
626  [04:22:56.513] [INFO ] :0 () - [BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 VS=IDR_XBOXBUILDINGVS PS=simplePS wind(tilt=0 c21c22=0 ext=0.120/0.038/0.000/1) treeAlpha(act=0 en=1 prof=0 mul=1.000 ps=115F1F78) handles pbr=1F0562C8/1F055FA8 iv=1F055E18/1F055AF8 fp=00000000/00000000
673  [04:23:34.758] [INFO ] :0 () - [BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 VS=IDR_XBOXBUILDINGVS PS=simplePS wind(tilt=0 c21c22=0 ext=0.120/0.049/0.000/1) treeAlpha(act=0 en=1 prof=0 mul=1.000 ps=115F1F78) handles pbr=1F0562C8/1F055FA8 iv=1F055E18/1F055AF8 fp=00000000/00000000
755  [04:23:36.751] [INFO ] :0 () - [BPIPE] cb=Xbox pipe=XBOX(1) ivMode=0 fp=0 VS=IDR_XBOXBUILDINGVS PS=simplePS wind(tilt=0 c21c22=0 ext=0.120/0.093/0.000/1) treeAlpha(act=0 en=1 prof=0 mul=1.000 ps=115F1F78) handles pbr=1F0562C8/1F055FA8 iv=1F055E18/1F055AF8 fp=00000000/00000000
```

Counts at cutoff:

| Scope | `IDR_XBOXBUILDINGVS` | `IDR_XBOXBUILDINGWINDVS` | Other VS |
|---|---:|---:|---|
| Interior (42 callbacks) | **42** | **0** | 0 |
| Outdoor early (7 callbacks) | **7** | **0** | 0 |
| Outdoor late (27 callbacks) | 7 | **0** | Sphere 12, PBR 5, PS2 2, GTAIV 1 |
| Whole run (326 callbacks + 1 diagnostic) | 186 | **0** | Sphere 128, GTAIV 5, PBR 5, PS2 2 |

**Answer:** Interior atomics now show `VS=IDR_XBOXBUILDINGVS`, not `IDR_XBOXBUILDINGWINDVS`. `IDR_XBOXBUILDINGWINDVS` is never selected anywhere in the run (0 occurrences).

Wind payload: all 326 callback lines have `c21c22=0`; 316 have `tilt=0`, 10 have `tilt=1` (GTAIV/PBR/PS2 only); **all 326 have `ext` gustAmp = `0.000`**. The nonzero `ext` fields are the constant heightScale `0.120` and the advancing gust phase (`0.038`, `0.049`, …), not wind amplitude. `treeAlpha(act=0 …)` throughout. EDED attached with `forceWindShader=0`; no `RpAtomicRegisterPlugin` failure. This rules out interior wind-shader leakage in this run.

## 5. Ambient / luminance evidence

Distinct `[PBR-Ambient]` final/floor/raw combinations in the interior + first outdoor + late outdoor windows:

```text
04:22:52.314..04:25:27.481  n=43  final=(0.050,0.050,0.050) floor=0.050 raw=(0.000,0.000,0.000)   <-- 42 in I + exit line
04:25:29.481..04:25:58.343  n=15  final=(0.067,0.047,0.027) floor=0.050 raw=(0.043,0.023,0.003)
04:26:00.370..04:26:06.359  n=4   final=(0.070,0.046,0.030) floor=0.050 raw=(0.043,0.020,0.003)
04:26:08.356..04:26:14.475  n=4   final=(0.072,0.045,0.032) floor=0.050 raw=(0.043,0.017,0.003)
04:26:16.467                 n=1   final=(0.069,0.046,0.033) floor=0.050 raw=(0.040,0.017,0.003)
04:26:18.486..04:26:26.482  n=5   final=(0.072,0.045,0.035) floor=0.050 raw=(0.040,0.013,0.003)
04:26:28.483..04:26:32.483  n=3   final=(0.074,0.044,0.037) floor=0.050 raw=(0.040,0.010,0.003)
04:26:34.490                 n=1   final=(0.071,0.045,0.038) floor=0.050 raw=(0.037,0.010,0.003)
```

`absolute floor` events (interior has none; first is at the exit):

```text
04:25:27.560  +0.030  lum 0.000 -> 0.030  skyL=0.500
04:25:31.558..04:25:58.343  +0.004  lum 0.026 -> 0.030  skyL 0.571 -> 0.566
04:26:00.368..04:26:06.357  +0.006  lum 0.024 -> 0.030  skyL 0.559 -> 0.545
04:26:08.355..04:26:16.466  +0.009  lum 0.021 -> 0.030  skyL 0.541 -> 0.522
04:26:18.484..04:26:26.480  +0.012  lum 0.018 -> 0.030  skyL 0.517 -> 0.499
04:26:28.482..04:26:32.481  +0.014  lum 0.016 -> 0.030  skyL 0.490 -> 0.485
04:26:34.488                 +0.015  lum 0.015 -> 0.030  skyL 0.481
```

`[Ped] pAmbient override` — every distinct value in the interior + outdoor early + late windows:

```text
INTERIOR (fixed/step values):
  (0.200,0.200,0.200)  x39  04:22:52.335..04:25:27.504   (38 in I + exit line)
  (0.274,0.274,0.274)  x3   04:23:38.731..04:24:56.945
  (0.214,0.214,0.214)  x1   04:23:54.752

OUTDOOR (timecyc-driven):
  (0.200,0.140,0.081) x9   04:25:29.519..04:25:53.604
  (0.376,0.347,0.326) x1   04:25:31.519
  (0.454,0.419,0.393) x1   04:25:35.525
  (0.271,0.251,0.235) x4   04:25:43.571..04:25:58.379
  (0.200,0.133,0.085) x4   04:26:00.392..04:26:06.394
  (0.269,0.249,0.233) x1   04:26:10.469
  (0.269,0.249,0.232) x1   04:26:12.476
  (0.200,0.126,0.089) x1   04:26:14.482
  (0.200,0.133,0.094) x1   04:26:16.482
  (0.267,0.247,0.232) x1   04:26:18.492
  (0.267,0.248,0.232) x2   04:26:20.495..04:26:22.488
  (0.845,0.781,0.729) x2   04:26:24.483..04:26:26.485
  (0.267,0.248,0.231) x1   04:26:28.485
  (0.844,0.784,0.728) x1   04:26:30.490
  (0.269,0.248,0.231) x1   04:26:32.502
  (0.268,0.249,0.230) x1   04:26:34.510
```

Later outdoors (04:26:36–04:30:28) values continue to vary with timecyc; observed range is roughly `0.200` to `0.855` per channel, with bright spikes such as `(0.855,0.795,0.723)` at 04:26:52.669. No later value returns to a fixed 0.05.

`[Tonemap]` — 5 `TC` lines exist, all outside the interior window, and **all have `interior=0`**. No `interior=1` occurs anywhere in the log.

```text
04:26:09.345  amb=(0.043,0.017,0.003) dir=(0.051,0.020,0.004) hour=12 interior=0
04:27:22.959  amb=(0.033,0.000,0.000) dir=(0.039,0.000,0.000) hour=13 interior=0
04:28:59.179  amb=(0.030,0.007,0.007) dir=(0.035,0.008,0.008) hour=14 interior=0
04:29:39.681  amb=(0.037,0.020,0.010) dir=(0.043,0.024,0.012) hour=15 interior=0
04:30:26.757  amb=(0.037,0.027,0.010) dir=(0.043,0.031,0.012) hour=16 interior=0
```

Direct-light magnitude (`pDirect` / `amb=` / `dir=`):

- Interior `[BLIT]` ambient is fixed `0.050`; most interior `pDirect` reads are `0.000`.
- Interior direct pulses: `pDirect=0.996` at 04:23:48.762, 04:23:50.762, 04:23:52.761, 04:23:54.763, 04:24:22.960, 04:24:24.957; `pDirect=0.498` at 04:24:20.956. So the interior is **not uniformly direct-light-free**.
- Outdoor `pDirect` is commonly `0.996`/`1.017` (e.g. 04:25:29.527, 04:26:25.202) and sometimes `0.264`–`0.549`.
- First outdoor `[Tonemap]` direct is `dir=(0.051,0.020,0.004)`; later it falls to `dir=(0.039,0,0)` and rises to `(0.043,0.031,0.012)`.
- `[VehiclePBR]` outdoor: `ambientPS=(0.07,0.05,0.03,1.00)`, `tcAmb=(0.07,0.05,0.03)`, `sunCol=(0.34,0.34,0.34)` at 04:26:09; `sunCol` ranges ~0.31–1.02.

**Answer:** Interior PBR ambient is **0.050 (the clamp floor) with raw 0.000** — not ~0.2. The 0.2 figure is the **ped** override (`pAmbient`), which is separately forced to 0.200 indoors. Interior direct light can reach ~1.0 on some frames, so ambient and direct light must not be conflated.

## 6. Interior-only vs outdoor-only tags

Interior-only (excluding startup/once-only):

```text
[BldTex] 24, [BldXbox] 16, [BUILDING] 40 (non-screen camera raster during building cb)
[CoronaExit] 2, [ShadowHeal] 1, [ShadowTex] 1
```

Outdoor-only:

```text
early: [SKY] 8, [CoronaDraw] 8, [VPIPE] 8, [VehTex] 3, [FlushShield] 1, [CloudShield] 1
late: [VehiclePBR] 154, PBRBuilding 194, PS2Building 204, [VehOpacity] 20,
      [EnvMap] 19, [Tonemap] 2, [SMAA] 5, [SMAA-DIAG] 10, [ForwardPlus] 29,
      [VehElide] 11, [Mode] 12, [ModeUI] 2, [CoronaDraw] 5, [SKY] 12,
      [VPIPE] 14, [VehTex] 7, [Config] 1, [Menu] 1, GetIBLTexture 1, RenderIBL 1
```

Interpretation caveat: `[BldTex]`, `[BldXbox]`, `[ShadowTex]`, `[ShadowHeal]` are first-N/once diagnostics, so their presence only indoors does **not** mean those paths stopped outdoors. Conversely, `[BldOpacity]` and `[CF]` are absent everywhere in this run, so they cannot support either side. The clean scene-exclusive tags are the sky/corona/vehicle set outdoors, and `[BUILDING]` non-screen-raster receipts indoors (and later during reflection renders outdoors).

## 7. ZWRITE / shield / building-latch

- `[BUILDING] ZWRITE off/desynced`: **0 occurrences**. The source comments mention this string, but the emitted building receipt is `[BldZW]`.
- `[VEHICLE] ZWRITE`: **0 occurrences**.
- `[PED] ZWRITE`: **115 occurrences**, all exactly `off/desynced on entry (rw=0 dev=0) — forcing TRUE for cb`. This is the ped contract, not the building latch.
- `[BldZW]` totals at cutoff: **192 entries = 1 canonical + 191 verbatim**. The only canonical entry is the very first building callback at 04:22:52.417 (`dev=0`). Every later entry is `dev=1 -> restore=verbatim`.
- Interior: 42 `[BldZW]` = 1 canonical + 41 verbatim.
- Outdoor early: 7 verbatim. Outdoor late: 13 verbatim. All other later outdoor periods observed are verbatim as well.

**Verdict:** the building-latch fix took effect. Building ZWRITE desync is confined to the first frame (1/192), effectively zero thereafter. The sprite-shield fix also executed: exactly one `[FlushShield]` restore at the first outdoor cloud batch (`states=9 ... zw=0`), which is expected because the line is `dbglog_once`. No `[FlushShield]`/black-quad evidence appears later, though the once-gating means a second event would not be logged. The persistent 115 ped ZWRITE repairs are a separate, still-live ped-state issue.

## 8. CRASH / WARN / ERROR / MISMATCH

No `[ERROR]`, `[WARN]`, `CRASH`, or `FATAL` level line exists. The log is all `[INFO]`. Notable lines (the uniform `[INFO ] :0 () - ` prefix is omitted for readability; message text is verbatim):

```text
[04:22:36.312] [Config] cfEnableHeal: slot 0 has no recorded user intent (s_cfEnableSeen=0) — early-return, heal is blind
[04:22:36.358] [corona_install] flush draw sites: 0x70CF62 op=E8 orig=00734EA0OK | 0x70CF9B op=E8 orig=00734EA0OK (expect op=E8 orig=734EA0; MISMATCH = flush hook never fires, corona quads unshielded)
[04:22:36.359] [pointlights_install] 0x53E21D op=E8 orig=007002D0OK (expect op=E8 orig=7002D0; MISMATCH = intercept SKIPPED, vanilla path kept, cone batch unshielded)
[04:22:36.359] [pointlights_install] 0x53DE6B op=E8 orig=007002D0OK (expect op=E8 orig=7002D0; MISMATCH = intercept SKIPPED, vanilla path kept, early glow batch unshielded)
[04:22:39.396] [INIT] shaders: compiled OK (ps=58 vs=17), FAILED (ps=0 vs=0)
[04:22:52.996] [INIT] shaders: compiled OK (ps=58 vs=17), FAILED (ps=0 vs=0)
[04:25:48.311] [BPIPE] Xbox building cb: forwardPlusEnable=1 but the Xbox simple-PS family has no cluster consumer — FP constants skipped; GTAIVForwardPlus pair intentionally not created/bound here (handles 00000000/00000000)
[04:26:00.831] [water] body table: 305 planes (ocean 56 shallow 21 interior 0 river 15 pond 19 lake 194) invisible 2 limitedDepth 21 skipped 0
[04:26:08.388] [water] gate failed (enable=0 vs=115F1AC8 ps=115F1488) -> original draw
[04:26:35.805] [PostFX] UpdateFrontBuffer SKIP: camRas 2048x2048 != screen 1920x1080 (transient raster)
[04:26:35.807] [PF] UpdateFrontBuffer FAILED hr=0x8876086C
[04:26:36.499] [PF] Radiosity_shader: composite skipped (front-buffer sync failed this frame)
[04:26:41.293] [PostFX] chars_drawSSSBlur early-out: no fresh classify mask (s_charsClassifyFresh has no producer) — Passes 0/1 (copy+blur) skipped, Pass 2 could not composite
[04:26:35.803] [PF] Radiosity_shader: limit=133 ... gsOK=1 mismatch=1 camMismatch=0
```

Notes:

- `hr=0x8876086C` is `D3DERR_INVALIDCALL`. It occurred once, immediately after `[PostFX] UpdateFrontBuffer SKIP: camRas 2048x2048 != screen 1920x1080 (transient raster)`; it was followed by repeated `[PF] Radiosity_shader: composite skipped (front-buffer sync failed this frame)` (20 by cutoff), and the run continued rendering. This is a real render-path fault, not a crash.
- The three `MISMATCH` texts (one `corona_install`, two `pointlights_install`) are explanatory strings; the actual hook bytes are `OK`.
- `[INIT] shaders ... FAILED (ps=0 vs=0)` reports **zero** failed shaders, not a failure.
- `[water] gate failed` is the expected fallback when the water gate is disabled. `[Radiosity_shader] mismatch=1` repeats but `camMismatch=0`.
- No clean `DLL_DETACH`/shutdown exists at the cutoff; the log ends mid-frame. The run was still live when sampled.

## 9. Ranked conclusions

**What the log proves**

1. The target run is the fresh **2,399,744 B** build (built 04:14:57), not the old 2,395,136 B build. The tester was inside CJ's-house-style interior from 04:22:52 to 04:25:26, then outside from ~04:25:27.
2. Interior building atomics use **`IDR_XBOXBUILDINGVS`**, never `IDR_XBOXBUILDINGWINDVS` (42/42 interior; 0/326 callbacks run-wide). `wind` has `tilt=0`, `c21c22=0`, gustAmp `0.000` for every callback. EDED attached with `forceWindShader=0`; no plugin-register failure. The interior wind-shader leak is ruled out.
3. Interior PBR ambient is **0.050 final / 0.000 raw** (the floor), not ~0.2. The 0.200 is the ped `pAmbient` override. Interior `pDirect` is usually 0 but pulses to 0.498–0.996, so direct light is separate from the ambient floor.
4. The building ZWRITE latch works: only the first frame restored canonically (1/192), everything after is verbatim; zero `[BUILDING] ZWRITE` desyncs. The sprite shield executed once at the first outdoor cloud flush and restored 9 states.
5. The only hard render failure is a one-shot `D3DERR_INVALIDCALL` in `UpdateFrontBuffer` at 04:26:35.807, during a transient 2048x2048 raster; the run survived it.
6. All 5 `[Tonemap] TC` lines are outdoor and have `interior=0`; no `interior=1` was ever logged.

**What it rules out**

- The old 2,395,136 B build and the 01:12 run as the source of this interior test.
- Interior atomics being routed to the wind shader.
- Widespread building-ZWRITE desync after the first frame.
- Interior PBR ambient being ~0.2 or higher (the PBR ambient floor is 0.05; the ped path is 0.2).
- A crash/shutdown during the analyzed interval.
- `EDED: RpAtomicRegisterPlugin FAILED` (never logged).

**Caveats**

- The log has no player position, zone, or `currArea` line; interior identification is from CJ/safehouse textures, the non-screen 2048x1024 building raster, and the ambient/ped transition. It is consistent with CJ's house but not geolocated.
- The clean outdoor census is the 14.5 s Xbox stretch at 04:25:29–04:25:43. The later 04:26:09+ period is a mode-menu cycle (Modern/PS2/GTAIV) and is useful for tags/ambient, not for like-for-like pipeline comparison.
- `[BldOpacity]`, `[CF]`, and literal `flushshield_restore` are absent from this build's output; absence is not evidence that the underlying paths did nothing.
- The log was live at the cutoff; a later crash or shutdown would be outside this report.

**Single most informative next probe**

Emit one throttled line at the first building/PBR callback per second containing: interior flag / `currArea`, player XYZ, raw timecyc ambient and direct light, `pDirect`, clamped `[PBR-Ambient]` raw/final/floor, and the EDED per-atomic wind decision. That would resolve the remaining ambiguity in one shot: whether the interior 0.050 is a correct floor of a true 0.000, or a clamp masking a nonzero interior ambient, and whether any interior atomic is still being classified as wind-capable. The current log cannot answer either question because it has no interior flag (`interior=1` never appears) and no per-atomic EDED decision line.
