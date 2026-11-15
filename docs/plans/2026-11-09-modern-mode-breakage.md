# Modern-mode breakage — MODE-side analysis (2026-11-09, planner lane, read-only)

User verdict under analysis: *"xbox mode best performance but llot stuf fbroken … modenr mode is very broken"*.
Scope: the mode system (`src/core/modeManager.cpp` MV_/MP_ tables, `ApplyMode`) vs the W0.5 retest
shots + `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (session 23:12:09.660–23:13:25.205, clean
`DLL_DETACH`, log:12880-12883). Sibling lane owns the VCS/trails hunt; this doc maps the MODE side.
No source edits, no builds. Every claim carries `file:line` (live tree `E:\dev(dave)\skygfx_plus_expIV`,
HEAD `5cc5d259`) or `log:line` (the session log above) or `ini:line`
(`E:\games\gtasa_skygfx_plus\skygfx.ini`).

---

## 0. Headline findings

1. **The "CF=8 vs modern" tension is a stale log comment, not a misdispatch.** `COLORFILTER_MODERN = 8`
   (`src/skygfx.h:1064`; VCS=6 `:1062`, GTAIV=7 `:1063`). The switch uses symbolic cases
   (`src/render/postfx.cpp:2397` VCS, `:2400` MODERN), so the engaged Modern profile's `cf=8`
   (log:6307) correctly ran `ColourFilter_Modern`. The dbglog string at `postfx.cpp:2433`
   ("0=NONE 1=PS2 2=PC 3=MOBILE 6=III 7=VC 8=VCS 9=MODERN") is **wrong/stale** — pure doc bug (P2-1).
2. **The user's INI is already the Modern-PBR identity with `[Modes] active=None`**
   (log:18 `pipeline=0 buildingPipe=3 vehiclePipe=10 colorFilter=8`; ini:148/26/242/35; ini:5).
   Three of the four W0.5 shots (F 50123/66540/86127) were taken in **MODE_NONE** running that INI —
   i.e. "modern is broken" reproduces **without any mode engaged**. The mode system is not the
   producer of the slab/blob families; the Modern *config* is.
3. **Rainbow (F 156602) is a Modern-profile-only lever: `caIntensity 0 → 0.10`** (`modeManager.cpp:670`;
   INI `caIntensity = 0` ini:27). The CA pass has a **c-register contract mismatch**: the HLSL expects
   `c0 = (intensity, radialFalloff, maxOffset-UV, aspect)` (`shaders/ps/CA.hlsl:13-16`, c1/c2 unused
   `:18-19`) but the C++ writes `c0 = {intensity, 1, 1, 1}` (`src/render/postfx_newfx.cpp:775`) — i.e.
   **maxOffset = 1.0 UV = a full screen** — while the intended "2.5 px cap" + aspect ride in the
   *ignored* `c1` (`postfx_newfx.cpp:777-782`). At intensity 0.1 the per-channel radial offset reaches
   ~0.1 UV ≈ 10 % of screen: exactly the giant hue-separated ghost bands in the shot. GTAIV owns
   `caIntensity = 0.12` (`modeManager.cpp:504`) → same trigger in IV mode.
4. **Profile application machinery verified intact this session.** Window-3 receipts show the Modern
   rows landing exactly (`[CF] exposure … minExp=0.5 maxExp=1.0 key=1.0` log:6366 = `modeManager.cpp:697-699`;
   `pipes=0/3/10 cf=8` log:6307). The `stateBaselineEnable` revert-via-base defect is closed by the parse
   floor (`src/core/config.cpp:708-709`), but a **narrower door of the same shape remains**: the debug menu
   row `stateBaselineEnable` is writable (`src/core/debugmenu_ui.cpp:820`) and `adoptLiveEdits` folds a
   divergent live value into the base (`modeManager.cpp:1229-1233`), so a menu-written 0 while latched can
   still be restored by a later switch/disengage. The other four MV_I(...,1) safety levers have **no**
   parse floor (opt-out respected; the user INI actually ships `defaultCbShaderHygiene = 0`, ini:40).

---

## 1. Session timeline + shot → mode mapping (task 3)

Log window boundaries come from the `[Mode]` receipts (9 hits):

| event | log:line | wall clock | F counter | mode after |
|---|---|---|---|---|
| SESSION-START | log:1 | 23:12:09.660 | 0 | — |
| Config dump (pre-INI) `pipeline=0 buildingPipe=3 vehiclePipe=10 colorFilter=8 … colorFilterEnable=0` | log:18 | 23:12:09.686 | 0 | — |
| `[CF] colorFilterEnable forced 1 for PBR` (`config.cpp:794`) | log:21 | 23:12:09.687 | 0 | — |
| `[mode] base refreshed slot=0 active=none pipes=0/3/10 cf=8` (`ModeManager_RefreshBase`, `modeManager.cpp:1569-1629`) | log:25 | 23:12:09.688 | 0 | MODE_NONE, base = INI |
| `[Mode] active=none` (ini:5 `active = None`) | log:27 | 23:12:09.692 | 0 | **W1 = MODE_NONE (INI-PBR)** |
| `[Mode] physics apply Xbox` + `engage Xbox owned=82 pipes=2/1/2 cf=2` + apply + listener | log:4047-4050 | 23:12:57.468 | 91148 | **W2 = MODE_XBOX** |
| `[Mode] physics apply Modern` + `engage Modern owned=105 pipes=0/3/10 cf=8` + apply + listener | log:6306-6309 | 23:13:11.834 | 140041 | **W3 = MODE_MODERN** |
| clean exit (`DLL_DETACH`) | log:12883 | 23:13:25.205 | 166654 | — |

Pipe receipts bracket the windows exactly:
`[VPIPE] pipe=MODERN(10) VS=IDR_VEHICLEPBRVS PS=IDR_VEHICLEPBR_MODERN` at log:3516/3791/3928 (W1),
`pipe=XBOX(2) VS=IDR_XBOXCARVS` log:4132…6256 (W2), `pipe=MODERN(10)` log:6329…10857 (W3);
`[BPIPE] cb=PBR … IDR_BUILDINGPBRVS/PS` log:342…3991 (W1), `cb=Xbox … IDR_XBOXBUILDINGVS/IDR_SIMPLEDETAILPS`
log:4057…6286 (W2), `cb=PBR` log:6313…10874 (W3).

Shot mapping (overlay `F <n> | C 0x<hex>` == log `[#n:hex]`; confidence **HIGH** — the hex pair matches
the log stamps verbatim, e.g. log:10769 `[#156602:000263BA]`):

| shot | overlay | window / mode | symptoms in frame |
|---|---|---|---|
| `gta_sa_HTmdFzxoxn.jpg` | F 50123 / C 0xC3CB | **W1 MODE_NONE (INI-PBR)** (bracketed by log #48135/#54127) | interior: CJ head = grey untextured box + yellow C3dMarker cone; black door-side panels; dark floor slab; ceiling specks |
| `gta_sa_Wo3DZ2MDJ8.jpg` | F 66540 / C 0x103EC | **W1** (bracketed by log #60777/#67610) | interior: giant black speckled floor blotches; grey "sentinel" slabs floating over the floor; black quad on table |
| `gta_sa_ECtpXKCWWK.jpg` | F 86127 / C 0x1506F | **W1** (bracketed by log #85990/#87331; Xbox engage is 3 s later at F91148) | outdoors: black diagonal streak-bars in sky; pure-red cube; olive translucent band; black slab across CJ torso; black diamonds on road |
| `gta_sa_hd9HXqites.jpg` | F 156602 / C 0x263BA | **W3 MODE_MODERN engaged** (exact log:10769) | whole-scene rainbow RGB channel-shred: hue-separated ghost copies (R/G/B) offset ~10 % screen, flat clamp-colour regions |

Consequences:
- The slab/blob/bar/red-cube/grey-box families are **all-modes on the Modern-PBR config** (W1 evidence),
  not mode-engage deltas. They also fire in W2 (see §5 receipts), which rules out "PBR pipe only".
- The rainbow is the **only W3-exclusive symptom** in this shot set, and W3's exclusive deltas vs W1 are
  exactly the lever flips enumerated in §3 (CA/vignette/DoF/SSS-post/MB-strength/dualPass/tonemap/…).
- "XBOX best performance" is consistent: XBOX pins the whole postfx stack off
  (`modeManager.cpp:277-291`: ssao/smaa/mb/sss/skin/hair/veg/normalMap/bloom/dof/sunShafts/fxaa = 0,
  caIntensity = 0, forwardPlus = 0, normalBuffer = 0, profiler = 0) and keeps waterQuality=2 (`:265`).
  "llot stuf broken" under XBOX is the all-modes family set still visible (receipts fire in W2, §5).

---

## 2. Profile diff table — MODE_XBOX vs MODE_MODERN (task 1)

Tables: `kValuesPS2` `modeManager.cpp:126-231`, `kValuesXBOX` `:238-341` (82 rows, log:4048),
`kValuesPCVintage` `:349-423`, `kValuesGTAIV` `:452-564`, `kValuesGTAV` `:586-591`,
`kValuesModern` `:598-737` (105 rows, log:6307), `kValuesMobile` `:750-846`; registry `:877-887`.
Physics rows: `kPhysicsGTAIV` `:861-866`, `kPhysicsCrashFxOff` `:871-873` (owned by PS2/XBOX/PC/Modern/Mobile).

### 2.1 Identity / look rows where both profiles write (value differs)

| field | XBOX (line) | MODERN (line) |
|---|---|---|
| pipeline | PIPELINE_XBOX=2 (`:240`) | PIPELINE_PBR=0 (`:600`) |
| buildingPipe | BUILDING_XBOX=1 (`:241`) | BUILDING_PBR=3 (`:601`) |
| vehiclePipe | CAR_XBOX=2 (`:242`) | CAR_MODERN=10 (`:602`) |
| colorFilter | COLORFILTER_PC=2 (`:243`) | COLORFILTER_MODERN=8 (`:603`) |
| preset | PRESET_SA_XBOX (`:245`) | PRESET_SKYGFXPLUSULTRAMAXDELUXE (`:604`) |
| waterStyle / waterQuality | 0 / 2 High (`:263,265`) | 2 (V) / 3 Ultra (`:676-677`) |
| grainFilter | 0 (`:274`) | 1 (`:632`) |
| ssaoEnable / smaaEnable | 0 / 0 (`:277-278`) | 1 / 1 (`:635,640`) |
| motionBlurEnable | 0 (`:279`) | 1 (`:641`) |
| sssPostProcessEnable / skin / hair / vegetation enhance | 0×5 (`:280-283`) | 1×5 (`:646,649,654,658` + veg `:658`) |
| normalMapEnable | 0 (`:284`) | 1 (`:615`) |
| bloomEnable / dofEnable / sunShaftsEnable | 0×3 (`:285-287`) | 1×3 (`:665-667`) |
| caIntensity | 0.0 (`:290`) | **0.10 (`:670`)** ← rainbow trigger |
| envMapSize / envMapFarClipMult | 256 / 1.0 (`:257-258`) | 512 / 2.0 (`:619-620`) |
| forwardPlusEnable / normalBufferEnable / profilerEnable | 0 / 0 / 0 (`:336,338,340`) | 1 / 1 / 1 (`:728,730,736`) |

(identical in both: ivMode=0, ps2ModulateBuilding=0, detailMaps=1, noiseDetail=1, dualPassBuilding=1,
usePCTimecyc=1, dualPassVehicle=0, waterParallaxEnable=1, sunGlare=0, radiosity=1, doRadiosity=1,
coronaZtest=1, ps2Modulate*=0, dualPass*=1, the five safety levers, pipeChainEnable=0, depthHookEnable=1.)

### 2.2 MODERN-only rows (the modern-only surface area; XBOX does not write them)

`normalMapIntensity=1.0` `:614`; `envShininessMult/envSpecularityMult/envPower/envFresnel`
`:621-624`; `vehEnvIntensity=0.4` `:625`; `vehPBRLayers=255` `:627`; `ssaoRadius/Power/KernelSize/SampleCount`
`:636-639`; `motionBlurStrength/Radial/SpeedFactor/CameraAware` `:642-645`; `sssPostProcessStrength/Radius`
`:647-648`; skin sliders `:650-653`; hair sliders `:655-657`; vegetation sliders `:659-660`;
`grainEnable=1` `:668`; `heightFogEnable=1` `:669`; `vignetteIntensity=0.15` `:671`; `fxaaEnable=0` `:672`;
iv-slider block `:684-692` (**inert: ivMode=0 `:605` makes DrawIVGrade return early**, comment `:680-683`);
`tonemapAutoExposure/AdaptSpeed/KeyStrength/MinExposure/MaxExposure` `:695-699`; `pbrIblAmbientWeight=0.5`
`:700`; `vehEnvGlint=0` `:701`; `vehChromeEnvThreshold=0` `:702`; `velocityBufferEnable=1` `:731`.

### 2.3 XBOX-only rows (MODERN inherits the base/INI value — the *silent* modern surface)

`tagsBuildingPipe` `:244`; `envMapUseLODs` `:259`; `reflectionMsaaLevel` `:260`; `grainIntensity` `:289`;
`colorFilterEnable` `:292`; `rgb1Mult/rgb2Mult` `:293-294`; `bYCbCrFilter` `:295`;
`sunCoronaIntensity/sunCoreIntensity/sunStreakIntensity/sunStreakSize` `:303-306`;
`grassAddAmbient` `:315`; `grassBackfaceCull` `:316`; `grassFixPlacement` `:317`; `pedShadows` `:318`;
`stencilShadows` `:319`; `zwriteThreshold{,Grass,Ped}` `:320-322`.
Live INI values that therefore ride into Modern: `sun*Intensity = 0` (ini around §SkyGfx
`sunCoreIntensity/sunCoronaIntensity/sunStreakIntensity/sunStreakSize = 0`), `grainIntensity = 0.05`
(ini:84), `pedShadows = 1` / `stencilShadows = 1`, `godRaysEnable = 1` with **all** godray params 0
(not owned by any profile except GTAIV `:498`), `tonemapOperator = 0` (ini:220; GTAIV owns =1 `:526`,
Modern does not → W3 `TonemapPass … operator=0` log:6368), `colorFilterEnable = 1` (ini:36, plus the PBR
force `config.cpp:794`).

### 2.4 PS2 / GTAIV / GTAV relevance

- PS2 (`:126-231`): same five safety levers `:216-220`; everything postfx off; waterQuality=1 `:155`.
- GTAIV (`:452-564`): owns `caIntensity = 0.12` `:504` (**same CA rainbow trigger**), owns
  `tonemapOperator = 1` `:526`, `godRaysEnable = 0` `:498`, iv-grade sliders `:530-539` (ivMode=1 `:459`),
  water IV/Ultra `:543-545`, physics crash-FX + softbody `:861-866`.
- GTAV (`:586-591`): water knobs only; H1 note `:579-584` documents that its revert of
  `stateBaselineEnable` is safe *only because of the parse floor*.
- Mobile (`:750-846`): full vintage field set; safety levers `:831-835`.

---

## 3. Modern-only path audit (task 2) — implementation, broken state, receipts vs silence

| lever (profile line) | implementation it turns on | known-broken state (wave records) | log receipts in this session |
|---|---|---|---|
| vehiclePipe=CAR_MODERN `:602` | `vehiclePipe.cpp:4199 case CAR_MODERN` → PBR Env cb (`vehiclePipe.cpp:77,206`), shaders `IDR_VEHICLEPBRVS`/`IDR_VEHICLEPBR_MODERN` (`pipelinecommon.cpp:823`, name `vehiclePipe.cpp:1091`); neo-unavailable → PS2 fallback logs `vehiclePipe.cpp:4211,4215` | vehicle-PBR wash family (FAM); `vehPBRLayers=255` enables every layer incl. glass | **RECEIPT** `[VPIPE] pipe=MODERN(10) … IDR_VEHICLEPBR_MODERN` log:3516/6329/7247/7928/8694/9753/10857 |
| buildingPipe=BUILDING_PBR `:601` | `buildingPipe.cpp` cb=PBR → `IDR_BUILDINGPBRVS/PS`; **surfProps.w baked-prelight split**: `.w` = N5 prelight strength, only the PBR cb packs it (`buildingPipe.cpp:104-115`, uploads `:1788,2442-2450`); the "huge black floor / near-black scene" mechanism for diffuse=0 baked-prelight assets is documented at `buildingPipe.cpp:126-130` | black-floor / pale-slab family; `[BldZW]` PBR blended meshes park ZWRITE (W0.1 triage §0) | **RECEIPT** `[BPIPE] cb=PBR` log:342…10874; `[BldZW]` ×77 (all windows) |
| colorFilter=COLORFILTER_MODERN `:603` (+PBR force `postfx.cpp:2036-2037`) | switch case `postfx.cpp:2400` → `ColourFilter_Modern` (grade pass 1 → tonemap pass 4, `postfx_colourfilter.cpp`; receipts `[PF] CF_Modern p1/TonemapPass/CF_Modern done`) | auto-exposure adapt stuck: `adapted=0.0001`, `autoExpRaw=300.72` clamped (log:6366; same in W1 log:947) → luminance-chain suspect | **RECEIPT** log:6361-6371, `[CF] exposure` log:6366 (minExp/maxExp/key = profile values ⇒ rows applied) |
| caIntensity=0.10 `:670` | `DrawChromaticAberration` `postfx_newfx.cpp:757-787`; lens-group gate `postfx.cpp:2479-2483` | **rainbow F156602** — c-register contract mismatch vs `shaders/ps/CA.hlsl:13-16` (maxOffset UV in c0.z; code writes 1.0 at `postfx_newfx.cpp:775`; 2.5 px cap + aspect in ignored c1 `:777-782`; c1/c2 declared unused `CA.hlsl:18-19`) | **SILENT** — zero `[FX] CA enable` lines; the shared `dbglog_throttle("fx")` key is consumed first each cycle by SunShafts/Bloom (log:466/823), so Grain/Vignette enable lines are absent too despite grainEnable=1 ⇒ silence is a throttle artifact, not proof of skip |
| vignetteIntensity=0.15 `:671` | `DrawVignette` `postfx_newfx.cpp:832-850` (`postfx.cpp:2498+` group) | untested in waves | **SILENT** (same throttle) |
| dofEnable=1 `:666` | DoF chain (handles log:465/701) | untested in waves | **SILENT** (no enable receipt exists in the pass) |
| sssPostProcessEnable=1 `:646` | `chars_drawSSSBlur` | untested | **RECEIPT** W1 bail `sssPostProcessEnable=0` log:508-509; W3 run `=1` log:7004/7728/8544/9436/10539 |
| motionBlur*=… `:641-645` (strength ×14 vs INI ini:122-124) | MB + velocity reconstruct | untested; velocity-buffer drift family adjacent | **RECEIPT** `[PF] VelocityBuffer bind/done` log:6753-6755, 6954-6955 |
| ssao* `:635-639` | `DrawSSAO_Overhaul` | untested | **RECEIPT** `[PF] SSAO_Dispatch …` log:6343-6348 |
| normalBuffer/velocityBuffer=1 `:730-731` | normal/velocity passes | — | **RECEIPT** log:6341, 6951-6955 |
| dualPass{Global,Building,Ped,Default} 0→1 `:611,715-718` | 2-pass alpha draws for buildings/peds/default | look delta; **not** the slab producer (BldZW fires in W2 where dualPass is also 1, and in W1 where it is 0) | **SILENT** (no dedicated receipt) |
| forwardPlusEnable/profilerEnable=1 `:728,736` | forward+ config/upload (`[VPIPE] fpCfg=1 fpUp=1`), profiler | log spam: `PERF [FXAA3] 0.00 ms` ~2 k lines at tail (scope sits before the fxaa gate, `postfx_newfx.cpp:856-859`) | **RECEIPT** `[VPIPE] … fpCfg=1 fpUp=1`; PERF spam log:10769-12858 |
| GTAIV parity note | `caIntensity=0.12` `:504` → same CA path | rainbow in IV mode too (predicted, untested) | n/a this session |

---

## 4. Profile-application integrity (task 4)

Mechanism: `ApplyMode` `modeManager.cpp:1398-1503` (toggle-off `:1418-1423`, switch restore
`:1439-1444`, base capture `:1463-1466`, apply `:1474-1475`); `restoreProfile` `:974-988`;
`adoptLiveEdits` `:1222-1247`; `ModeManager_RefreshBase` `:1569-1630`; `ModeManager_ClearLatch`
`:1340-1355`; per-slot base `:46-48`.

MV_I(...,1) safety set, owned by every field-owning profile:
`stateBaselineEnable` (PS2 `:216`, XBOX `:326`, PC `:416`, IV `:557`, MODERN `:721`, MOBILE `:831`),
`pedOpaqueContract` (`:217/327/413/559/723/833`), `texAlphaClassify` (`:218/328/417/558/722/832`),
`defaultCbShaderHygiene` (`:219/329/418/560/724/834`), `spriteStateShield` (`:220/330/419/561/725/835`).
MODE_GTAV owns **none** of them (`:586-591`); MODE_NONE/CUSTOM own nothing (`:878,885`).

Findings:
1. **stateBaselineEnable**: parse floor `config.cpp:708-709` (both directions clamped to 1) closes the
   INI-authored-0 leak; the GTAV revert note `modeManager.cpp:579-584` documents exactly this dependency.
   Residual door: writable menu row `debugmenu_ui.cpp:820` + `adoptLiveEdits` folding a divergent live
   value into the base (`modeManager.cpp:1229-1233`) ⇒ a menu-written 0 while latched re-enters via the
   base on switch/disengage. Narrower than the old defect (menu-only), same shape. **P2-4.**
2. **The other four levers have no floor** — clamps are `[0,1]` opt-outs (`config.cpp:691-693`
   texAlphaClassify, `:710-712` spriteStateShield, `:751-753` defaultCbShaderHygiene, `:756-757`
   pedOpaqueContract). The user INI ships `defaultCbShaderHygiene = 0` (ini:40) ⇒ W1 ran hygiene-off;
   W3 (and any engaged profile) forces 1. Mechanically not a bug (base == authored user intent), but a
   policy asymmetry vs stateBaselineEnable. **P2-3** (decision: floor it or accept the opt-out).
3. **No stale-base symptom observed in this session**: W3 receipts equal profile values
   (log:6307 pipes/cf; log:6366 tonemap min/max/key; log:6362 adaptSpeed=0.120 = `:696`);
   W2 receipts equal XBOX values (log:4048 pipes=2/1/2 cf=2). The XBOX→MODERN switch executed
   adopt→restore→apply with no residue visible in receipts.
4. **colorFilterEnable** is XBOX-owned (`:292`) but not MODERN-owned; it is held at 1 in both PBR
   windows by the PBR force `config.cpp:794` (+ `cfEnableHeal` `main.cpp:580-600`, call `:668`;
   blind-heal receipt log:19). Not a modern gap while pipeline==PBR; it is a gap for a non-PBR
   Modern-derived config (none exists today because of the force `postfx.cpp:2036-2037`).

---

## 5. Ranked modern-breakage list → fix lanes (task 5)

Ranking = user-visible severity × confidence; "all-modes" rows are ruled out as mode deltas but kept
because the user experiences them *as* "modern broken" (their INI *is* modern).

| # | symptom (shot/log evidence) | attribution | mode-side? | fix target (write scope) |
|---|---|---|---|---|
| **P0-1** | rainbow RGB shred, F156602 (shot 4; log:10769) | CA c-register contract mismatch: `postfx_newfx.cpp:775-784` vs `shaders/ps/CA.hlsl:11-19`; enabled only by `caIntensity>0` = Modern `modeManager.cpp:670` / GTAIV `:504` (INI 0, ini:27) | YES (profile lever flips the broken pass on) | **Lane A** `src/render/postfx_newfx.cpp` + `shaders/ps/CA.hlsl`; optional mitigation **Lane B** pin `caIntensity 0` in `modeManager.cpp:670/504` until Lane A lands |
| **P0-2** | black/grey untextured slabs, red cube, grey box head, ceiling specks (shots 1-3; W1 **and** W2 **and** W3: BldZW/texdefs W1=87/W2=14/W3=14) | fail-open texture-miss draw + corona/sprite-flush black quads + PBR blended-mesh ZWRITE park: `texdefs.cpp:257-262,292-294` (red fallback marker / untextured white quad), `main.cpp:2966` (`[CoronaDraw] black-quad=blend 0`), `[BldZW]` (buildingPipe lane) — per `2026-11-09-visual-triage.md` §0/§4 | NO (all-modes on PBR config; fires under XBOX too) | **Lane C** `src/texdefs.cpp`; **Lane D** `src/core/main.cpp` sprite/corona-flush region + `src/render/buildingPipe.cpp` |
| **P0-3** | airborne grey sentinel slabs / black blotches over floors (shots 1-2) | same fail-open family + frame-transform (family-C) placement; triage §4 "single fix = fail-closed the texture-miss draw path" | NO | Lane C (+ family-C owner) |
| **P1-1** | CJ head grey box / head blob (shot 1; W0.1 #3/#5/#6) | ped head-part path (`[PedHeadPath]` 1×, log grep) + head-blob family, triage §1-2 | NO (W1 evidence) | **Lane E** ped pipe / head path (`src/render/peds_pipe*.cpp`) |
| **P1-2** | olive bands / milky sheets outdoors (shot 3; W0.1 #4/#7/#8/#9) | alpha-sheet family; `[FxAlpha]` repairs ×60 this session | NO | **Lane F** postfx alpha/scene-repair (`src/render/postfx*.cpp` alpha layer) |
| **P1-3** | auto-exposure adapt pinned at 0.0001 / autoExpRaw=300 (log:6366, also W1 log:947) | luminance reduce/adapt chain feeding `ColourFilter_Modern` tonemap (PBR-forced, both windows) | config-family (PBR), not a mode lever | **Lane G** `src/render/postfx_luminance.cpp` |
| **P2-1** | misleading ops string "8=VCS 9=MODERN" | stale comment, `postfx.cpp:2433` (enum truth `skygfx.h:1062-1064`) | doc-only | Lane F (comment) |
| **P2-2** | ~2 k lines `PERF [FXAA3] 0.00 ms` spam in W3 | profiler scope before the fxaa gate, `postfx_newfx.cpp:856-859`; enabled by Modern `profilerEnable=1` `:736` | cosmetic | Lane A |
| **P2-3** | `defaultCbShaderHygiene=0` INI vs safety intent | no parse floor (`config.cpp:751-753`); profiles force 1 | policy | **Lane H** `src/core/config.cpp` (+ INI) |
| **P2-4** | stateBaseline menu-live-write → base → revert | `debugmenu_ui.cpp:820` writable row + `modeManager.cpp:1229-1233` adopt | mechanism residue | **Lane H** `src/core/debugmenu_ui.cpp` or floor in `modeManager.cpp` adopt path |
| **P2-5** | Modern runs `tonemapOperator=0` (filmic) while IV=1; sun corona/streak gains 0 in Modern/None windows | unowned rows: `tonemapOperator` (IV `:526`), `sun*Intensity` (XBOX `:303-306`) — look deltas, deliberate? | policy | Lane B `modeManager.cpp` table rows |

Lane write-scopes are disjoint: A = {postfx_newfx.cpp, shaders/ps/CA.hlsl}; B = {modeManager.cpp};
C = {texdefs.cpp}; D = {main.cpp sprite/corona region, buildingPipe.cpp}; E = {peds pipe files};
F = {postfx.cpp/postfx_colourfilter.cpp alpha layer}; G = {postfx_luminance.cpp};
H = {config.cpp, debugmenu_ui.cpp}.

Confidence statement: shot→mode HIGH (hex-overlay/log stamp identity, 4/4); rainbow→CA HIGH on code
contract (both files read, offset magnitude matches the visual), MEDIUM-HIGH on causation (no direct CA
receipt exists because of the shared `fx` throttle key — DoF/vignette equally silent; no competing
per-channel mechanism exists among the W3 deltas); slab family all-modes HIGH (receipts in W1+W2+W3);
W1==INI-modern equivalence HIGH (log:18/25/27 + ini:5/26/35/148/242).

## 6. Open questions for the orchestrator

1. Sanction Lane B mitigation (pin `caIntensity=0` in Modern/GTAIV profiles) as a stopgap while Lane A
   fixes the CA contract, or hold Modern broken until A lands?
2. P2-3 policy: floor `defaultCbShaderHygiene` (and the other three opt-out levers) like
   `stateBaselineEnable`, or keep authored opt-out and document it?
3. P2-5: is `tonemapOperator=0` under Modern intended (ULTRA look = filmic) or an ownership oversight?
