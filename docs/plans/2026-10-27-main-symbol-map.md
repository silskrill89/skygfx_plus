# `src/core/main.cpp` — symbol / linkage map (2026-10-27)

Mechanical inventory for splitting `src/core/main.cpp` (8,701 lines, CRLF) into multiple TUs.
Read-only survey; no source was modified. Line numbers are from the current file.

Method: brace-aware top-level declaration scan + `grep` of `src/**/*.{cpp,h,hpp}` excluding
`src/core/main.cpp`. “Real refs” below exclude comment-only mentions (`// main.cpp foo`) and
local same-name collisions in other TUs. Header declaration lines are listed separately from
call sites because a declaration is a linkage contract, not a use.

Legend: **linkage** = `static` (internal) / `ext` (external) / `export` (`extern "C"` +
`__declspec(dllexport)`). `—` = no reference from another file.

---

## 1. File-scope symbols defined in `src/core/main.cpp`

### 1A. Referenced from other files (keep the external contract / header)

| Line | Symbol | Kind | Linkage | References from other files |
|---|---|---|---|---|
| 42 | `perfFreq` | variable | ext | `skygfx.h:57,60,61,62,66,68,69,73` (inline `perfInit`/`perfNow`) |
| 43 | `perfFreqInv` | variable | ext | `skygfx.h:58,62,69,73`; `render/waterPipe.cpp:1157,1158,1160,1163` |
| 45 | `dllModule` | variable | ext | `skygfx.h:47`; `extras/debugmenu_public.h:79-81`; `render/forwardplus.cpp:162,198,253`; `render/grasssys.cpp:730,735`; `render/pipelinecommon.cpp:664,670,691,697`; `render/postfx_colourfilter.cpp:1407` (+15 total) |
| 46 | `gDebugMenuAPI` | variable | ext | `extras/debugmenu_public.h:41-68` (anonymous inlines); +42 total |
| 47 | `asipath` | array | ext | `core/debugmenu_ui.cpp:21,526,566,583,892,895,897,902,4389,4422,4424,4451` |
| 48 | `g_allowCrashPassThrough` | variable | ext | `core/debug_handl.cpp:287`; `core/debug_handl.h:84` |
| 54 | `disableClouds` | variable | ext | `core/debugmenu_ui.cpp:634,1638,3576`; `core/main_exports.h:18` |
| 55 | `disableGamma` | variable | ext | `core/debugmenu_ui.cpp:634,1639,3577`; `core/main_exports.h:19` |
| 56 | `fixPcCarLight` | variable | ext | `core/debugmenu_ui.cpp:634,1640,3578`; `core/main_exports.h:20` |
| 57 | `explicitBuildingPipe` | variable | ext | `skygfx.h:953`; `core/main_exports.h:21`; `render/buildingPipe.cpp:3527` |
| 58 | `transparentLockon` | variable | ext | `core/debugmenu_ui.cpp:635,1646,3581`; `core/main_exports.h:26` |
| 59 | `fixShadows` | variable | ext | `core/debugmenu_ui.cpp:635,1647,3582`; `core/main_exports.h:27` |
| 60 | `iCanHasNeoDrops` | variable | ext | `skygfx.h:952`; `core/debugmenu_ui.cpp:20,272,1910,3614`; `core/main_exports.h:24` |
| 61 | `iCanHasbuildingPipe` | variable | ext | `skygfx.h:949`; `core/debugmenu_ui.cpp:245`; `core/main_exports.h:22`; `render/envmap.cpp:1126`; `render/vehiclePipe.cpp:2530,3705,3709,3712` |
| 62 | `iCanHasvehiclePipe` | variable | ext | `skygfx.h:950`; `core/debugmenu_ui.cpp:18,253`; `core/main_exports.h:23` |
| 63 | `iCanHasSunGlare` | variable | ext | `skygfx.h:951`; `core/debugmenu_ui.cpp:19,282,1908,3614`; `core/main_exports.h:25` |
| 64 | `gHasExternalNormalMapPlugin` | variable | ext | `skygfx.h:954`; `texdefs.cpp:180`; `core/debugmenu_ui.cpp:1903,3619`; `render/buildingPipe.cpp:2788`; `render/vehiclePipe.cpp:2828` |
| 69 | `privateHooks` | variable | ext | `core/debugmenu_ui.cpp:634,1641,3579`; `core/main_exports.h:16` |
| 70 | `forceWindShader` | variable | ext | `texdefs.cpp:50,56`; `core/debugmenu_ui.cpp:634,1644,3580`; `core/main_exports.h:17` |
| 72 | `fixingSAMP` | variable | ext | `core/main_exports.h:15` (declaration only; no call sites) |
| 73 | `UG_mod` | variable | ext | `skygfx.h:1181`; `core/main_exports.h:39`; `render/postfx.cpp:2084`; `render/vehiclePipe.cpp:3691-3740` |
| 77 | `numConfigs` | variable | ext | `skygfx.h:928`; `core/main_exports.h:13`; `core/debugmenu_ui.cpp:239,3608,4409`; `render/postfx.cpp:1679,1680` |
| 78 | `currentConfig` | variable | ext | `skygfx.h:929`; `core/main_exports.h:14`; `core/debugmenu_ui.cpp:239,894,895,3608,4386,4409`; `render/postfx.cpp:1680,1681` |
| 79 | `configs` | array | ext | `skygfx.h:930`; `core/modeManager.cpp:35,1025` |
| 80 | `config` | variable | ext | 1,067 occurrences in 40+ files — `skygfx.h:930,939,940`; `texdefs.cpp:179`; `core/debugmenu_ui.cpp:40,51,52,77,85,184,194-205`; `render/*` |
| 124 | `simplePS` | variable | ext | `skygfx.h:1188`; `render/buildingPipe.cpp:768,961,962,1508,1509`; `render/pipelinecommon.cpp:809`; `render/vehiclePipe.cpp:848,1102,1103,1183,1184,1267,1822` (+17) |
| 127 | `gRenderingSpheremap` | variable | ext | `skygfx.h:1133`; `render/buildingPipe.cpp:3165`; `render/envmap.cpp:992,1085,1114`; `render/neopipe.cpp:567,579`; `render/postfx.cpp:3403`; `render/vehiclePipe.cpp:2108,2494,2982,3358` |
| 128 | `reflectionCamPos` | variable | ext | `skygfx.h:1134`; `render/buildingPipe.cpp:2167,2168`; `render/envmap.cpp:328,329,330,532,1082` |
| 135 | `TempBufferVerticesStored` | variable (ref) | ext | `render/waterPipe.cpp:221,524,1039,1057,1060,1191,1195` |
| 136 | `TempBufferIndicesStored` | variable (ref) | ext | `render/waterPipe.cpp:222,525,1040,1057,1061,1191` |
| 138 | `TempVertexBuffer` | variable | ext | `render/waterPipe.cpp:223,544` |
| 140 | `windPos` | variable | ext | `render/buildingPipe.cpp:33` (decl), `300,301,830`; `render/treeWind.cpp:32,326` |
| 144 | `GetConfig` | function | export | none in `src/` (DLL ABI export) |
| 153 | `getpath` | function | ext | `skygfx.h:1104`; `texdefs.cpp:427`; `render/neopipe.cpp:40,657` |
| 175 | `_rwD3D9RenderStateFlushCache` | function (WRAPPER) | ext | `render/buildingPipe.cpp:58,621`; `render/envmap.cpp:11,381,749`; `render/mirrorPass.cpp:104,126`; `render/pipelinecommon.cpp:19,1697,1838,2035`; `render/waterPipe.cpp:81,1146,1586` (+16) |
| 179 | `fixSAMP` | function | ext | `skygfx.h:1180`; `render/buildingPipe.cpp:3211`; `render/vehiclePipe.cpp:3780` |
| 206 | `D3D9Render` | function | ext | `skygfx.h:1175`; `render/buildingPipe.cpp:333,1536,2232,2270`; `render/neopipe.cpp:907`; `render/vehiclePipe.cpp:722,1888,3037,3152,3564` |
| 250 | `D3D9RenderDual` | function | ext | `skygfx.h:1176`; `render/buildingPipe.cpp:1446,2083,2998`; `render/neopipe.cpp:841`; `render/vehiclePipe.cpp:1106,1269,1702,1825,2123,3552` |
| 334 | `CTimeCycle_GetAmbientRed` | function (WRAPPER) | ext | `render/pipelinecommon.cpp:193,236,256`; `rw/gta.h:276` |
| 335 | `CTimeCycle_GetAmbientGreen` | function (WRAPPER) | ext | `render/pipelinecommon.cpp:194,236,257`; `rw/gta.h:277` |
| 336 | `CTimeCycle_GetAmbientBlue` | function (WRAPPER) | ext | `render/pipelinecommon.cpp:195,236,258`; `rw/gta.h:278` |
| 338 | `resetValues` | function | ext | `skygfx.h:934`; `core/debugmenu_ui.cpp:267,269,308,325,331-337,2454,2842` (+28) |
| 550 | `cfEnableSetUserIntent` | function | ext | `core/debugmenu_ui.cpp:38,40` |
| 637 | `derivePipelineFromPipes` | function | ext | `core/debugmenu_ui.cpp:34,4381`; `render/postfx.cpp:1441,1524` |
| 739 | `refreshIni` | function | ext | `skygfx.h:935`; `core/debugmenu_ui.cpp:3127` |
| 2501 | `CMessages__AddMessageJumpQWithNumber` | function | ext | `skygfx.h:1178`; `render/postfx.cpp:1681` |
| 2739 | `doglare` | function (naked) | ext | `skygfx.h:285,927`; `core/debugmenu_ui.cpp:118,283,1021,3613`; `core/main_exports.h:63`; `core/modeManager.cpp:127,236,358,527,665,736` |
| 3485 | `CoronaWindow_Close` | function | ext | `skygfx.h:1520`; `render/depthhook.cpp:407,408` |
| 3754 | `InvalidateCameraRasterCache` | function | ext | `render/postfx.cpp:2633,2661` |
| 3948 | `cloudAnimTimer` | variable | ext | `render/postfx.cpp:2802,2805`; `render/vehiclePipe.cpp:2367,2382` |
| 5080 | `cloudRenderFix` | variable | ext | `core/debugmenu_ui.cpp:24,1069,3570,3611` |
| 5602 | `RwIm3DTransform` | function | ext | `render/grasssys.cpp:811` |
| 5606 | `RwIm3DEnd` | function | ext | `render/grasssys.cpp:816` |
| 5610 | `RwIm3DRenderIndexedPrimitive` | function | ext | `render/grasssys.cpp:815` |
| 5894 | `explicitBuildingPipe_tmp` | variable | ext | `core/debugmenu_ui.cpp:636,1648,3583`; `core/main_exports.h:74` |
| 7234 | `setConfig` | function | ext | `skygfx.h:942`; `core/debugmenu_ui.cpp:239`; `render/postfx.cpp:1682` |
| 7244 | `reloadAllInis` | function | ext | `skygfx.h:937`; `core/debugmenu_ui.cpp:241,2930,4395`; `render/postfx.cpp:1694` |
| 7255 | `saveConfigTo` | function | ext | `skygfx.h:939`; `core/debugmenu_ui.cpp:4425` |
| 7634 | `saveConfig` | function | ext | `skygfx.h:938`; `core/debugmenu_ui.cpp:4387`; `core/debug_handl.cpp:1149,1157,1163` |
| 7653 | `loadConfigFile` | function | ext | `skygfx.h:940`; `core/debugmenu_ui.cpp:593,4452` |
| 7673 | `MENUSETTINGS` | macro | n/a | `core/debugmenu_ui.cpp:90,174,185` |
| 7759 | `menuGetNoiseQuality` | function | ext | `core/debugmenu_ui.cpp:639,2335,2412,3489,4323` |
| 7760 | `menuSetNoiseQuality` | function | ext | `core/debugmenu_ui.cpp:640,2062,2418` |

### 1B. External linkage but **no real cross-TU use** (safe to make `static` / move without a header)

Header-declaration-only = name appears in `main_exports.h`/`skygfx.h` but no other `.cpp` calls it.
Every symbol keeps its line number; grouped rows share one evidence class.

| Line(s) | Symbols (kind) | Linkage | Evidence |
|---|---|---|---|
| 75 | `UG_RegisterEventCallback` (var/fn ptr) | ext | `main_exports.h:40` decl only |
| 121, 123, 125, 126, 137, 779, 780, 2430, 2431, 2433, 2434, 2435, 2437, 2468, 2469, 2470, 2703, 2841, 3223, 3224 | `original_bRadiosity`, `grassPixelShader`, `gpCurrentPixelShaderForDefaultCallbacks`, `gpCurrentVertexShaderForDefaultCallbacks`, `TempBufferRenderIndexList`, `plantTab0/1`, `AmbientLightColourForFrame_PedsCarsAndObjects`, `DirectionalLightColourForFrame`, `ambRed/Green/Blue`, `SetLightsWithTimeOfDayColour_orig`, `tmpintensity`, `tmptexture`, `CPlantMgr_AmbientColor`, `rand_seed`, `pointLights`, `currentLight`, `stkp` (variables) | ext | main.cpp only; 2468/3223/3224 are asm ABI |
| 566, 782, 2387, 2438, 3713, 5081, 5811, 5820, 5834, 5865, 5874, 5884 | `cfEnableHeal`, `grassRenderCallback`, `clamp`, `SetLightsWithTimeOfDayColour`, `RenderScene`, `CClouds__Render_orig`, `readhex` (both overloads), `FileExists`, `StrAssoc::get`, `readint`, `readfloat` (functions) | ext | main.cpp only; 782/5834 comment-only |
| 144, 151, 8108 | `GetConfig`, `SkyGfx_FollowsMovedArrays`, `DllMain` (functions) | export | no in-tree refs (DLL ABI) |
| 303, 311, 2357, 2358, 2359, 2370, 2472, 2506, 2533, 2553, 2576, 2593, 2615, 2643, 2665, 2687, 2695, 2717, 3220, 3226, 3558, 3561, 3568, 3571, 3658, 3674, 3675, 5490, 5491, 5511, 5550, 5556, 5614, 5615, 7191, 7222, 7664, 7810 | `D3D9RenderBlack_DUAL`, `D3D9RenderDefault_DUAL`, `CTagManager__RenderTagForPC`, `CTagManager__SetupAtomic_orig`, `CTagManager__SetupAtomic`, `CTagManager__RenderTag`, `setTextureAndColor`, `fixSeed`, `saveIntensity`, `rxD3D9DefaultRenderCallback_Hook`, `FrustumTestSphere_Guard`, `FrustumTestBox_Guard`, `PSSetCameraConstantD_Guard`, `rxD3D9DefaultRenderCallback_VertexShaderHook`, `CPlantMgr_Initialise`, `FX::GetFxQuality_ped`, `FX::GetFxQuality_stencil`, `floatbitpattern`, `CSprite__..._orig`, `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect`, `CreateRoadsignTexture_RwTextureSetName_orig/_hook`, `CreateRoadsignAtomicA_RpAtomicSetGeometry_orig/_hook`, `renderMoonMask`, `CSkidmarks__Render_orig`, `CSkidmarks__Render`, `PipelinePluginAttach`, `myPluginAttach`, `InitialiseGame`, `installLCMV2Hooks`, `InitialiseGame_hook`, `CWaterLevel__RenderAndEmptyRenderBuffer`, `CWaterLevel__RenderAndEmptyRenderBuffer_hook`, `readIni`, `readInis`, `afterStreamIni`, `InjectDelayedPatches` (functions / fn ptrs) | ext | `main_exports.h` / `skygfx.h` declaration only |
| 1756, 2704, 2706, 2712, 2727, 2729, 2736, 3768, 5412, 5439, 5827, 5843 | `myDefaultCallback`, `ps2randnormalize`, `ps2rand`, `ps2srand`, `gtasrand`, `mysrand`, `CVehicle__DoSunGlare`, `RenderScene_before`, `RenderScene_after`, `RenderScene_hook`, `struct StrAssoc`, `findInis` | ext | declarations + comments only |

### 1C. Internal linkage (`static`) definitions — zero external refs by definition

| Line | Symbol | Kind | Line | Symbol | Kind | Line | Symbol | Kind |
|---|---|---|---|---|---|---|---|---|
| 85 | `s_activeModeIni` | variable | 86 | `s_activeModeIniSeen` | variable | 90 | `modeIniAlias` | function |
| 109 | `modeOwnsPipes` | function | 130 | `defaultColourLeftUOffset` | variable | 131 | `defaultColourRightUOffset` | variable |
| 132 | `defaultColourTopVOffset` | variable | 133 | `defaultColourBottomVOffset` | variable | 228 | `setAlphaTestTwoLayer` | function |
| 241 | `setZWriteTwoLayer` | function | 391 | `clampEnvSliderRanges` | function | 415 | `clampTonemapExposureRanges` | function |
| 481 | `clampSsaoPower` | function | 497 | `clampChromeParams` | function | 545 | `s_cfEnableUser` | array |
| 546 | `s_cfEnableSeen` | array | 728 | `g_noiseQuality` | variable | 730 | `utilityNoiseResForQuality` | function |
| 877 | `atomicHasAlpha` | function | 992 | `pedOpaqueCapture` | function | 1051 | `pedOpaqueDiverged` | function |
| 1066 | `pedOpaqueEnforce` | function | 1112 | `pedOpaqueRestore` | function | 1251 | `shadowContractCapture` | function |
| 1341 | `shadowContractDrift` | function | 1356 | `shadowContractForce` | function | 1397 | `shadowContractRestore` | function |
| 1512 | `CShadows__UpdateStaticShadows_orig` | variable | 1513 | `CShadows__RenderStaticShadows_orig` | variable | 1514 | `CShadows__RenderStoredShadows_orig` | variable |
| 1515 | `CRealTimeShadowManager__Update_orig` | variable | 1519 | `shadowRasterToTex` | function | 1537 | `shadowCallThis` | function (naked) |
| 1553 | `shadowContractRun` | function | 1635 | `CShadows__UpdateStaticShadows_hook` | function | 1643 | `CShadows__RenderStaticShadows_hook` | function |
| 1652 | `CShadows__RenderStoredShadows_hook` | function | 1662 | `CRealTimeShadowManager__Update_hook` | function | 1682 | `vs_probe` | function |
| 1713 | `pedStage0Resync` | function | 2861 | `g_inCoronasRender` | variable | 2867 | `g_coronaBodyDone` | variable |
| 2875 | `g_inCloudsRender` | variable | 2889 | `g_coronaPreValid` | variable | 2890 | `g_coronaPreFuncRw` | variable |
| 2891 | `g_coronaPreRefRw` | variable | 2892 | `g_coronaPreFuncDev` | variable | 2893 | `g_coronaPreRefDev` | variable |
| 2894 | `g_coronaPreSepAlpha` | variable | 2895 | `g_coronaPreColorWrite` | variable | 2904 | `g_coronaPreBlend` | variable |
| 2905 | `g_coronaPreSrcBlend` | variable | 2906 | `g_coronaPreDstBlend` | variable | 2907 | `g_coronaPreBlendOp` | variable |
| 2908 | `g_coronaPreZWrite` | variable | 2935 | `coronaRwBlendFromD3D` | function | 2947 | `coronaDrawRawShield` | function |
| 3042 | `s_spriteFlushState` | variable | 3043 | `s_spriteForceValid` | variable | 3045 | `spriteFlushShieldCapture` | function |
| 3119 | `spriteFlushShieldClose` | function | 3288 | `SpriteFlushIndexedDraw_orig` | variable | 3290 | `SpriteFlushIndexedDraw_hook` | function |
| 3455 | `CPointLights__Render_orig` | variable | 3456 | `CPointLights__Render_shield_hook` | function | 3595 | `alphafunc` | variable |
| 3596 | `setMoonAlphaBlendStates` | function | 3604 | `restoreMoonAlphaBlendStates` | function | 3624 | `moonMaskSavedRet` | variable |
| 3625 | `moonMaskSavedEsp` | variable | 3627 | `renderMoonMaskBody` | function (noinline) | 3712 | `RenderScene_A` | variable |
| 3748 | `s_camRasGame` | variable | 3749 | `s_camRasGameW` | variable | 3750 | `s_camRasGameH` | variable |
| 3751 | `s_camRasScrW` | variable | 3752 | `s_camRasScrH` | variable | 4268 | `RenderEffects_orig` | variable |
| 4269 | `RenderEffects_hook` | function | 4309 | `CPostEffects__Render_orig` | variable | 4310 | `CPostEffects__Render_hook` | function |
| 4401 | `CCoronas__RenderReflections_orig` | variable | 4402 | `CCoronas__RenderReflections_hook` | function | 4411 | `CCoronas__RenderSunReflection_orig` | variable |
| 4412 | `CCoronas__RenderSunReflection_hook` | function | 4472 | `CCoronas__Render_trampoline` | variable | 4473 | `CCoronas__Render_blendprobe_hook` | function |
| 4820 | `s_fbWpState` | variable | 4821 | `s_fbWpVehInstalled` | variable | 4822 | `s_fbWpTotalFires` | variable |
| 4823 | `s_fbWpWatchAddr` | variable | 4841 | `s_fbWpEips` | array | 4842 | `s_fbWpEipCount` | variable |
| 4843 | `s_fbWpOverflow` | variable | 4846 | `fbWp_TargetAddr` | function | 4859 | `fbWp_veh` | function |
| 4979 | `fbWp_ArmOnce` | function | 5083 | `CClouds__Render_hygiene` | function | 5522 | `installDeferredHooks` | function |
| 3992 | `effectsForceAlphaBlock` | function | 4220 | `coronaFeedForwardPlus` | function |  |  |  |
| 5899 | `g_qualityPreset` | variable | 5904 | `readIniFile` | function | 7768 | `DrawFinalEffects_pf` | function |
| 7795 | `ModeListener_OnApply` | function | 8082 | `verifyBytes` | function |  |  |  |

### 1D. Types and macros defined in `main.cpp`

| Line | Name | Kind | External refs |
|---|---|---|---|
| 74 | `UG_EventHook` | typedef | none |
| 983 | `PedOpaqueState` | struct | none |
| 1238 | `ShadowContractState` | struct | none |
| 2823 | `PointLight` | struct | comment only (`forwardplus.cpp:414`) |
| 3033 | `SpriteFlushState` | struct | none |
| 3287 | `SpriteFlushIndexedDraw_t` | typedef | none |
| 4190 | `CoronaFeedEntry` | struct | none |
| 4814 | `FBWP_DR7_MASK` | macro | none |
| 4815 | `FBWP_DR7_ARM` | macro | none |
| 4819 | `FBWP_OFF/ARMED/UNAVAILABLE` | enum | none |
| 4827 | `FBWP_FIRE_CAP` | macro | none |
| 4833 | `FBWP_MAX_EIPS` | macro | none |
| 4834 | `FBWP_LOG_EVERY` | macro | none |
| 4835 | `FbWpEip` | struct | none |
| 6909 | `ADD_IF_MISSING` | macro (mid-function) | none; used in `readIniFile` |
| 6913 | `SKYGFX_EMIT_DEFAULT_KEYS` | macro (mid-function) | none; used in `readIniFile` |
| 7673 | `MENUSETTINGS` | macro | `debugmenu_ui.cpp:90,174,185` (see 1A) |

Note: `struct CVector` at 3585 and `CWaterLevel__CalculateWavesForCoordinate*` at 3586/3588
are inside a `/* ... */` block (3584-3593) — not compiled, not symbols.

---

## 2. External symbols `main.cpp` consumes (include-set derivation)

Counts are textual occurrences in `main.cpp` (declaration lines included).

### 2A. `extern` declarations made inside `main.cpp` itself

| Symbol | Provider | Uses | Notes |
|---|---|---|---|
| `pipeForceAlphaBlock` | `render/buildingPipe.cpp` | 4 calls | declared 3× (857, 3672, 3987) |
| `overrideIm2dPixelShader` | `render/postfx.cpp` | 6 | declared 3988; also in `postfx.h:122` |
| `overrideColorMod` | `render/postfx.cpp` | 4 | declared 3989; `postfx.h:120` |
| `overrideAlphaMod` | `render/postfx.cpp` | 4 | declared 3990; `postfx.h:121` |
| `ForwardPlus_AddLight` | `render/forwardplus.cpp` | 1 call | declared 4185 |
| `ForwardPlus_FeedGameLights` | `render/forwardplus.cpp` | 1 call | declared 4186 |
| `RpNormMapPluginAttach` | rwnormal plugin (`extern "C"`) | 0 | declared 7761; deliberately never called |
| `RpNormMapAtomicIsInitialized` | rwnormal plugin | 0 | declared 8098; unused |
| `RpNormMapMaterialSetNormMapTexture` | rwnormal plugin | 0 | declared 8099; unused |
| `RpNormMapMaterialGetNormMapTexture` | rwnormal plugin | 0 | declared 8100; unused |
| `_rwD3D9VSGetComposedTransformMatrix` | game exe (hook target) | 1 | declared 8154 |
| `pipeAlphaFuncToD3D` | `render/pipelinecommon.cpp` | 10 calls | declared 226, 5078 |
| `TexAlpha_Classify` | texture-alpha lane | 1 call | block-scope extern at 892 |
| `RenderReflectionMap_leeds` | mirror pass | 1 call | forward decl 3715 |
| `RenderReflectionScene` | mirror pass | 0 calls | forward decl 3716; unused |

### 2B. Header-provided externs actually used

| Provider header | Symbols (uses) |
|---|---|
| `skygfx.h` | `config` (151), `configs` (17), `numConfigs` (12), `currentConfig` (4), `perfFreq` (1), `perfFreqInv` (1), `SSAO` (9), `SMAA` (7), `Water_VS` (6), `skinPipe` (4), `simplePS` (3), `g_pUtilityNoise` (2), `g_CurrentNoiseSize` (1), `Water_Parallax` (2), `VehiclePBR_Modern` (1), `UG_mod` (4), `g_ragdollDeathEnable` (1); `CPostEffects::` members/functions (see below) |
| `rw/gta.h` | `d3d9device` (307), `Scene` (24), `pAmbient` (19), `RsGlobal` (12), `gpWhiteTexture` (12), `pDirect` (9), `CWeather__WindDir` (2), `CCutsceneMgr__ms_running` (2) |
| `core/debug_handl.h` | `dbglog` (~181), `dbglog_throttle` (53), `dbglog_once` (23), `diag_init` (1), `diag_installVEH` (1), `diag_getLogPath` (1), `g_allowCrashPassThrough` (def. here; 4) |
| `texdefs.h` | `TexDefsShutdown` (3), `TexDefsExternalInit` (3), `TexDefsTryAttachExternal` (4), `TexDefsInit` (1), `TexDefsAssignInfo` (1), `TexDefsFindInfo` (1) |
| `core/modeManager.h` | `ModeManager_Init` (1), `ModeManager_RegisterListener` (1), `ApplyMode` (1 real call, `:7179`), `ModeManager_RefreshBase` (1), `ModeManager_GetMode` (1), `ModeId` enum |
| `entities/sftbdyphsx.h` | `g_ragdollMan` (4) |
| `render/waterPipe.h` | `g_waterFresnelSplit` (6), `g_waterParallaxActive` (1) |
| `render/profiler.h` | `PfFrame` (4), `g_pfEnable` (3), `g_pfGpuTimers` (2) |
| `render/treeWind.h` | `TreeAlpha_SetProfile` (4), `TreeWind_ModifyMatrixForTreeInWind` (2) |
| `render/PC_GrassInstancer.h` | `CGrassInstancer::SetEnabled` (5) |
| `render/renderstate_monitor.h` | `RSMon_Init` (1) |
| `render/forwardplus.h` | `ForwardPlus_ReleaseResources` (2) |
| `render/postfx.h` | `overrideIm2dPixelShader` (6), `overrideColorMod` (4), `overrideAlphaMod` (4), `CPostEffects::` API |
| `extras/debugmenu_public.h` | `gDebugMenuAPI` (def. here; 42) |
| `rw/MemoryMgr.h` | `InjectHook` (60), `InterceptCall` (27), `Patch` (38), `Nop` (13), `PATCH_JUMP` (25) |
| `neo.h` / `waterPipe.h` / `chars.h` / `weather.h` / `extras/ragdoll_death.h` | `neoInit`, `hookWaterDrops`, `waterPipe_init/shutdown`, `Ragdoll_Init/Shutdown` |
| `ini_parser.hpp` | `linb::ini` (in `readIniFile`/`saveConfigTo`) |
| `ModuleList.hpp` | `ModuleList().Get(...)` |
| `injector/hooking.hpp` | `injector::` namespace (used only in other TUs; main.cpp only includes it) |

### 2C. `CPostEffects` API used from `main.cpp`

`Render` (7), `Grain_PS2` (4), `m_bRadiosity` (2), `m_colourLeftUOffset` (2),
`m_colourRightUOffset` (2), `m_colourTopVOffset` (2), `m_colourBottomVOffset` (2),
`m_InfraredVisionGrainStrength` (2), `m_NightVisionGrainStrength` (2), `Initialise` (2),
`Radiosity` (2), `m_RadiosityFilterPasses`, `m_RadiosityRenderPasses`, `m_RadiosityIntensity`,
`m_bYCbCrFilter`, `m_lumaScale`, `m_lumaOffset`, `m_crScale`, `m_crOffset`, `m_cbScale`,
`m_cbOffset`, `DrawFinalEffects`, `Initialise_skygfx`, `Initialise_orig`,
`ColourFilter_switch`, `DarknessFilter_fix`, `InfraredVision_PS2`, `NightVision_PS2` (1 each).

RW/D3D9 surface used directly (huge include surface): `RwRenderStateGet` (69),
`RwRenderStateSet` (132), `RwD3D9SetRenderState` (151), `RwD3D9SetTextureStageState` (10),
`RwD3D9SetPixelShader` (6), `RwD3D9SetVertexShader` (6), `RwD3D9SetTexture` (2),
`RwD3D9DrawIndexedPrimitive` (1); Win32 (`GetModuleFileNameA`, `VirtualAlloc`, `memcpy`,
`memset`, `strncpy`, `sscanf`, `GetAsyncKeyState`, ...).

---

## 3. Hook / patch sites (every `InjectHook`, `InterceptCall`, `Patch<`, `Patch(`, `Nop(`)

Verification column: `verifyBytes` = byte compare before write; `E8+target` = explicit opcode
and rel32 target check; `memcmp` = explicit prologue compare; `blind` = no check at the write
site. Owner is the enclosing function / local table.

| Line | Call | Address | Verified | Owner |
|---|---|---|---|---|
| 5531 | `InterceptCall` | `&RenderScene_A` | blind | `installDeferredHooks` |
| 7914 | `InterceptCall` | `&CPostEffects::Initialise_orig` | blind | `InjectDelayedPatches` |
| 7920 | `InterceptCall` | `&InitialiseGame` | blind | `InjectDelayedPatches` |
| 7932 | `Nop` | `0x5BBF6F` (2 B) | blind | `InjectDelayedPatches` |
| 7933 | `Nop` | `0x5BBF83` (2 B) | blind | `InjectDelayedPatches` |
| 7956 | `InjectHook` | `0x5E675E` | blind | `InjectDelayedPatches` |
| 7957 | `InjectHook` | `0x5E676D` | blind | `InjectDelayedPatches` |
| 7958 | `InjectHook` | `0x706BC4` | blind | `InjectDelayedPatches` |
| 7959 | `InjectHook` | `0x706BD3` | blind | `InjectDelayedPatches` |
| 7960 | `InjectHook` | `0x7113B8` | blind | `InjectDelayedPatches` |
| 7961 | `InjectHook` | `0x711D95` | blind | `InjectDelayedPatches` |
| 7962 | `InjectHook` | `0x70F9B8` | blind | `InjectDelayedPatches` |
| 7965 | `Patch<uint>` | `0x5D88D1 +6` | blind | `InjectDelayedPatches` |
| 7966 | `Patch<uint>` | `0x5D88DB +6` | blind | `InjectDelayedPatches` |
| 7967 | `Patch<uint>` | `0x5D88E5 +6` | blind | `InjectDelayedPatches` |
| 7968 | `Patch<uint>` | `0x5D88F9 +6` | blind | `InjectDelayedPatches` |
| 7969 | `Patch<uint>` | `0x5D8903 +6` | blind | `InjectDelayedPatches` |
| 7970 | `Patch<uint>` | `0x5D890D +6` | blind | `InjectDelayedPatches` |
| 7973 | `InjectHook` | `0x714145` → `0x71422A` | blind | `InjectDelayedPatches` |
| 7979 | `InterceptCall` | `&CClouds__Render_orig` (`0x53DCA2`) | blind | `InjectDelayedPatches` |
| 7980 | `InterceptCall` | `&CClouds__Render_orig` (`0x53DFA0`) | blind | `InjectDelayedPatches` |
| 8002 | `InjectHook` | `0x74721C` → `0x7472F3` | blind | `InjectDelayedPatches` |
| 8004 | `InjectHook` | `0x6ABCFD` (`doglare`) | blind | `InjectDelayedPatches` |
| 8007 | `InjectHook` | `0x742E33` → `0x742EC1` | blind | `InjectDelayedPatches` |
| 8008 | `InjectHook` | `0x742FE0` → `0x743085` | blind | `InjectDelayedPatches` |
| 8013 | `Patch` | `0x709B2D +2` | blind | `InjectDelayedPatches` |
| 8014 | `Patch` | `0x709B8C +2` | blind | `InjectDelayedPatches` |
| 8015 | `Patch` | `0x709BC5 +2` | blind | `InjectDelayedPatches` |
| 8016 | `Patch` | `0x709BF4 +2` | blind | `InjectDelayedPatches` |
| 8017 | `Patch` | `0x709C91 +2` | blind | `InjectDelayedPatches` |
| 8018 | `Patch` | `0x709E9C +2` | blind | `InjectDelayedPatches` |
| 8019 | `Patch` | `0x709EBA +2` | blind | `InjectDelayedPatches` |
| 8020 | `Patch` | `0x709ED5 +2` | blind | `InjectDelayedPatches` |
| 8021 | `Patch` | `0x70B21F +2` | blind | `InjectDelayedPatches` |
| 8022 | `Patch` | `0x70B371 +2` | blind | `InjectDelayedPatches` |
| 8023 | `Patch` | `0x70B4CF +2` | blind | `InjectDelayedPatches` |
| 8024 | `Patch` | `0x70B633 +2` | blind | `InjectDelayedPatches` |
| 8025 | `Patch` | `0x7085A7 +2` | blind | `InjectDelayedPatches` |
| 8031 | `Patch` | `0x5901BD +1` | blind | `InjectDelayedPatches` |
| 8043 | `InterceptCall` | `&CWaterLevel__RenderAndEmptyRenderBuffer` (`0x6E8790`) | blind | `InjectDelayedPatches` |
| 8044 | `InterceptCall` | same (`0x6E8EF1`) | blind | `InjectDelayedPatches` |
| 8045 | `InterceptCall` | same (`0x6E91E4`) | blind | `InjectDelayedPatches` |
| 8046 | `InterceptCall` | same (`0x6E9963`) | blind | `InjectDelayedPatches` |
| 8054 | `InjectHook` | `0x534E90` (`TreeWind_...`) | blind | `InjectDelayedPatches` |
| 8155 | `InjectHook` | `0x7646E0` (`_rwD3D9VSGet...`) | blind | `DllMain` |
| 8163 | `InjectHook` | `0x713C4C` (`renderMoonMask`) | blind | `DllMain` |
| 8173 | `InjectHook` | `0x5BCF14` (`afterStreamIni`) | blind | `DllMain` |
| 8174 | `InjectHook` | `0x7491C0` (`myDefaultCallback`) | blind | `DllMain` |
| 8175 | `InjectHook` | `0x5BF8EA` (`CPlantMgr_Initialise`) | blind | `DllMain` |
| 8182 | `InjectHook` | `0x756DFE` | `verifyBytes` (5 B) | `DllMain` |
| 8184 | `InjectHook` | `0x7FAD30` | `verifyBytes` (8 B) | `DllMain` |
| 8186 | `InjectHook` | `0x7FAD90` | `verifyBytes` (8 B) | `DllMain` |
| 8188 | `InjectHook` | `0x7618B0` | `verifyBytes` (7 B) | `DllMain` |
| 8190 | `InjectHook` | `0x5DADB7` | `verifyBytes` (7 B) | `DllMain` |
| 8200 | `InjectHook` | `0x5DAE61` | `verifyBytes` (8 B) | `DllMain` |
| 8201 | `Patch` | `0x5DAEC8` | blind (inside `0x5DAE61` verify branch; target not checked) | `DllMain` |
| 8204 | `InjectHook` | `0x5D9EEB` | blind | `DllMain` |
| 8205 | `InjectHook` | `0x5D9EFB` | blind | `DllMain` |
| 8208 | `InjectHook` | `0x4C88F0` → `0x5DA610` | blind | `DllMain` |
| 8211 | `InjectHook` | `0x553AD1` → `0x553AE5` | blind | `DllMain` |
| 8212 | `InterceptCall` | `&CSkidmarks__Render_orig` (`0x53E175`) | blind | `DllMain` |
| 8268 | `InterceptCall` | `sites[i]` (`0x53E0BE`→`0x707F40`) | E8 + target + previous-instr E8 (`zeroArg`) | `DllMain` / `sites[2]` (`ShadowSite`, 8240-8247) |
| 8316 | `InjectHook` | `staticEntry` (`0x708300`) | `memcmp` 5 B `A1 24 7B C9 00` | `DllMain` / static-entry block 8274-8334 |
| 8373 | `InjectHook` | `rtEntry` (`0x706AB0`) | `memcmp` 5 B `51 53 57 8B F9` | `DllMain` / rt-entry block 8338-8385 |
| 8393 | `InterceptCall` | `&RenderEffects_orig` (`0x53EAD3`) | blind | `DllMain` |
| 8394 | `InterceptCall` | `&CCoronas__RenderReflections_orig` (`0x53DCAC`) | blind | `DllMain` |
| 8395 | `InterceptCall` | same (`0x53DFD8`) | blind | `DllMain` |
| 8396 | `InterceptCall` | `&CCoronas__RenderSunReflection_orig` (`0x53E12B`) | blind | `DllMain` |
| 8459 | `InjectHook` | `coronaEntry` (`0x6FAEC0`) | `memcmp` 20 B corona prologue | `DllMain` / corona-entry block 8400-8470 |
| 8499 | `InterceptCall` | `&SpriteFlushIndexedDraw_orig` (`0x70CF62`) | E8 + target `0x734EA0` | `DllMain` / flush-draw verify block 8478-8503 |
| 8500 | `InterceptCall` | same (`0x70CF9B`) | E8 + target `0x734EA0` | same |
| 8523 | `InterceptCall` | `&CPointLights__Render_orig` (`0x53E21D`) | E8 + target `0x7002D0` | `DllMain` / pointlights block 8508-8528 |
| 8547 | `InterceptCall` | `&CPointLights__Render_orig` (`0x53DE6B`) | E8 + target `0x7002D0` | `DllMain` / pointlights block 8532-8550 |
| 8552 | `InterceptCall` | `&CPostEffects__Render_orig` (`0x53E227`) | blind | `DllMain` |
| 8555 | `InterceptCall` | `&CTagManager__RenderTagForPC` (`0x534335`) | blind | `DllMain` |
| 8556 | `InterceptCall` | `&CTagManager__SetupAtomic_orig` (`0x4C4412`) | blind | `DllMain` |
| 8560 | `InjectHook` | `0x704D1E` (`ColourFilter_switch`) | blind | `DllMain` |
| 8561 | `InjectHook` | `0x704D5D` (`Radiosity`) | blind | `DllMain` |
| 8562 | `InjectHook` | `0x704FB3` (`Radiosity`) | blind | `DllMain` |
| 8563 | `InjectHook` | `0x704D48` (`DarknessFilter_fix`) | blind | `DllMain` |
| 8566 | `InjectHook` | `0x704F4B` (`InfraredVision_PS2`) | blind | `DllMain` |
| 8567 | `InjectHook` | `0x704F59` (`Grain_PS2`) | blind | `DllMain` |
| 8569 | `InjectHook` | `0x704EDA` (`NightVision_PS2`) | blind | `DllMain` |
| 8570 | `InjectHook` | `0x704EE8` (`Grain_PS2`) | blind | `DllMain` |
| 8572 | `InjectHook` | `0x705078` (`Grain_PS2`) | blind | `DllMain` |
| 8574 | `InjectHook` | `0x705091` (`Grain_PS2`) | blind | `DllMain` |
| 8576 | `InjectHook` | `0x53EBE9` (`DrawFinalEffects_pf`) | blind | `DllMain` |
| 8579 | `InjectHook` | `0x700B6B` (sprite rotate aspect) | blind | `DllMain` |
| 8581 | `InjectHook` | `0x44E82E` (`ps2rand`) | blind | `DllMain` |
| 8582 | `InjectHook` | `0x44ECEE` (`ps2rand`) | blind | `DllMain` |
| 8583 | `InjectHook` | `0x42453B` (`ps2rand`) | blind | `DllMain` |
| 8584 | `InjectHook` | `0x42454D` (`ps2rand`) | blind | `DllMain` |
| 8586 | `InterceptCall` | `&PipelinePluginAttach` (`0x53D903`) | blind | `DllMain` |
| 8589 | `InjectHook` | `0x5A3C7D` (`ps2srand`) | blind | `DllMain` |
| 8590 | `InjectHook` | `0x5A3DFB` (`ps2srand`) | blind | `DllMain` |
| 8591 | `InjectHook` | `0x5A3C75` (`ps2rand`) | blind | `DllMain` |
| 8592 | `InjectHook` | `0x5A3CB9` (`ps2rand`) | blind | `DllMain` |
| 8593 | `InjectHook` | `0x5A3CDB` (`ps2rand`) | blind | `DllMain` |
| 8594 | `InjectHook` | `0x5A3CF2` (`ps2rand`) | blind | `DllMain` |
| 8595 | `Patch` | `0x5A3CC8` (`ps2randnormalize`) | blind | `DllMain` |
| 8596 | `Patch` | `0x5A3CEA` (`ps2randnormalize`) | blind | `DllMain` |
| 8597 | `Patch` | `0x5A3D05` (`ps2randnormalize`) | blind | `DllMain` |
| 8598 | `InjectHook` | `0x5A3476` (`ps2rand`) | blind | `DllMain` |
| 8599 | `InjectHook` | `0x5A34AB` (`ps2rand`) | blind | `DllMain` |
| 8600 | `InjectHook` | `0x5A34E0` (`ps2rand`) | blind | `DllMain` |
| 8601 | `InjectHook` | `0x5A3515` (`ps2rand`) | blind | `DllMain` |
| 8602 | `Patch` | `0x5A348D +2` (`ps2randnormalize`) | blind | `DllMain` |
| 8603 | `Patch` | `0x5A34C2 +2` (`ps2randnormalize`) | blind | `DllMain` |
| 8604 | `Patch` | `0x5A34FB +2` (`ps2randnormalize`) | blind | `DllMain` |
| 8605 | `Patch` | `0x5A352F +2` (`ps2randnormalize`) | blind | `DllMain` |
| 8609 | `Patch<float*>` | `0x73290A+2` (`multipassMultiplier`) | blind | `DllMain` |
| 8612 | `Nop` | `0x733313` (2 B) | blind | `DllMain` |
| 8613 | `Nop` | `0x73405A` (2 B) | blind | `DllMain` |
| 8614 | `Nop` | `0x733403` (2 B) | blind | `DllMain` |
| 8615 | `Nop` | `0x73431A` (2 B) | blind | `DllMain` |
| 8616 | `Nop` | `0x73444A` (2 B) | blind | `DllMain` |
| 8619 | `Patch<float>` | `0x5DDB3D+1` (`78.0f`) | blind | `DllMain` |
| 8622 | `Nop` | `0x6E716B` (6 B) | blind | `DllMain` |
| 8623 | `Nop` | `0x6E7176` (6 B) | blind | `DllMain` |
| 8627 | `Patch` | `0x553C7D +2` (`zoffset`) | blind | `DllMain` |
| 8628 | `Nop` | `0x553C78` (5 B) | blind | `DllMain` |
| 8629 | `Nop` | `0x553C9A` (5 B) | blind | `DllMain` |
| 8630 | `Nop` | `0x553CD1` (5 B) | blind | `DllMain` |
| 8631 | `Nop` | `0x553CEC` (5 B) | blind | `DllMain` |
| 8634 | `Patch` | `0x726516 +6` (`216.1f`) | blind | `DllMain` |
| 8635 | `Patch` | `0x726534 +6` (`216.1f`) | blind | `DllMain` |
| 8636 | `Patch` | `0x726552 +6` (`216.1f`) | blind | `DllMain` |
| 8637 | `Patch` | `0x726570 +6` (`216.1f`) | blind | `DllMain` |

Conditional gates: 7965-7970 `fixPcCarLight`; 7973 `disableClouds`; 8002 `disableGamma`; 8004 `iCanHasSunGlare`; 8007-8008 `transparentLockon > 0`; 8013-8025 `fixShadows`; 8031 `privateHooks`; 8054 `config->treeWind`; 7914 `!UG_RegisterEventCallback`; 7920 `!g_hasMoonLoader`.

Raw writes not using the `Patch` API (for extraction awareness):
`*(float*)0x8CD4F0 = 256.0f;` (:8026), `*(void**)0xA9AD78 = (void*)TagRenderCB;` (:8557),
game-global ref bindings (135-138, 779-780, 2430-2435, 2469-2470, 2841).

Verification receipts that do **not** install a hook: `0x53D81C` sky-dome check (7985-7998,
deliberately not hooked) and the `sites[]` / static-entry / rt-entry / corona-entry blocks above.
`verifyBytes` itself is at 8082-8094.

---

## 4. SEH / naked / inline-asm regions

All `__try` scopes below contain only POD locals; none has a destructor-bearing object, so no
C2712/C2713 trigger. (`readIniFile`/`saveConfigTo` do use `linb::ini`/`std::string` but have no
`__try`.)

| Lines | Function | Kind | Non-POD locals in `__try` |
|---|---|---|---|
| 3629-3631 | `renderMoonMaskBody` | `__try` → `setMoonAlphaBlendStates()` | none |
| 3633-3642 | `renderMoonMaskBody` | `__try` + `_asm` (3634-3641) → call `0x70D000` | none |
| 3653-3655 | `renderMoonMaskBody` | `__try` → `restoreMoonAlphaBlendStates()` | none |
| 3627-3657 | `renderMoonMaskBody` | `__declspec(noinline)` + 3 SEH fences + asm | — |
| 4332-4350 | `CPostEffects__Render_hook` | `__try` (read-only render-state probe) | none (`RwUInt32`/`DWORD`) |
| 4496-4646 | `CCoronas__Render_blendprobe_hook` | `__try` (shield + trampoline + probe) | none (`DWORD`/`int`) |
| 4651-4744 | `CCoronas__Render_blendprobe_hook` | `__try` (exit restore) | none (`DWORD`) |
| 4850-4853 | `fbWp_TargetAddr` | `__try` (read `Scene.camera->frameBuffer`) | none |
| 4870-4875 | `fbWp_veh` | `__try` (exception record read) | none |
| 4895 | `fbWp_veh` | `__try` (DR6 read) | none |
| 4903 | `fbWp_veh` | `__try` (DR6 clear) | none |
| 4911-4924 | `fbWp_veh` | `__try` (module name/offset) | none (`HMODULE`, `char[]`) |
| 4965-4967 | `fbWp_veh` | `__try` (DR7 clear at fire cap) | none |
| 5009-5036 | `fbWp_ArmOnce` | `__try` (OpenThread/Get/SetThreadContext) | none (`HANDLE`,`CONTEXT`) |
| 5309-5311 | `CClouds__Render_hygiene` | `__try` → `CClouds__Render_orig()` | none (`static DWORD`) |
| 5577-5581 | `InitialiseGame_hook` | `__try` → `InitialiseGame()` | none (pointers/`DWORD`) |
| 8651-8653 | `DllMain` (DETACH) | `__try` → `TexDefsShutdown()` | none |
| 8660-8662 | `DllMain` | `__try` → `ShutdownEnvMap()` | none |
| 8669-8671 | `DllMain` | `__try` → `waterPipe_shutdown()` | none |
| 8678-8680 | `DllMain` | `__try` → `ForwardPlus_ReleaseResources()` | none |
| 8686-8688 | `DllMain` | `__try` → `g_ragdollMan.Exit()` | none |
| 8691-8693 | `DllMain` | `__try` → `Ragdoll_Shutdown()` | none |

`__declspec(naked)` functions (whole body is asm, no C++ locals):

| Lines | Function | Asm lines |
|---|---|---|
| 1537-1546 | `shadowCallThis` | 1540-1545 |
| 2506-2530 | `fixSeed` | 2509-2529 |
| 2533-2550 | `saveIntensity` | 2536-2549 |
| 2553-2572 | `rxD3D9DefaultRenderCallback_Hook` | 2556-2571 |
| 2576-2591 | `FrustumTestSphere_Guard` | 2579-2590 |
| 2593-2608 | `FrustumTestBox_Guard` | 2596-2607 |
| 2615-2641 | `PSSetCameraConstantD_Guard` | 2618-2640 |
| 2643-2663 | `rxD3D9DefaultRenderCallback_VertexShaderHook` | 2646-2662 |
| 2717-2725 | `floatbitpattern` | 2719-2724 |
| 2739-2752 | `doglare` | 2741-2751 |
| 3658-3665 | `renderMoonMask` | 3660-3664 |
| 7664-7671 | `afterStreamIni` | 7667-7670 |

Inline `_asm` in otherwise normal functions: `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect`
3229-3230 (statement-form `_asm`, no block); `renderMoonMaskBody` 3634-3641, 3648-3652,
3660-3664; `afterStreamIni` 7667-7670.

---

## 5. Static mutable file-scope state (extraction blockers)

Every file-scope `static` non-const variable. “Functions (R/W)” lists main.cpp functions that
read or write it. Cross-section = touched by more than one of the logical extractions
(INI/config, mode, shadow, corona, sprite-flush, effects, clouds, framebuffer, FBWP, hooks-wiring).

| Line | Symbol | Type | Functions (R/W) | Cross-section? |
|---|---|---|---|---|
| 85 | `s_activeModeIni` | `ModeId` | `modeOwnsPipes` R; `readIniFile` W (:5949); `saveConfigTo` W (:7279); `ModeListener_OnApply` W (:7803); `InjectDelayedPatches` R (:7834) | **YES** — mode + INI load + INI save + init |
| 86 | `s_activeModeIniSeen` | `bool` | `readIniFile` W (:5950); `saveConfigTo` R (:7278); `ModeListener_OnApply` W (:7804) | **YES** — mode + INI |
| 130-133 | `defaultColour*UOffset` (4) | `int` | `readIniFile` R (:6427,6431); `DllMain` W (:8157-8160) | **YES** — config + init |
| 545 | `s_cfEnableUser[10]` | `RwBool[]` | `cfEnableSetUserIntent` W/R; `cfEnableHeal` R | no (colour-filter section) |
| 546 | `s_cfEnableSeen[10]` | `bool[]` | `cfEnableSetUserIntent` W/R; `cfEnableHeal` R | no |
| 728 | `g_noiseQuality` | `int` | `refreshIni` R (:753); `readIniFile` W (:5958); `saveConfigTo` W (:7624-7626); `menuSetNoiseQuality` W; `menuGetNoiseQuality` R | **YES** — INI + menu API |
| 1512 | `CShadows__UpdateStaticShadows_orig` | fn ptr | hook (:1638) + `DllMain` `sites[]` W (:8248) | **YES** — shadow + hook wiring |
| 1513 | `CShadows__RenderStaticShadows_orig` | fn ptr | hook (:1646) + `DllMain` W (:8314) | **YES** — shadow + hook wiring |
| 1514 | `CShadows__RenderStoredShadows_orig` | fn ptr | hook (:1655) only (site removed) | no |
| 1515 | `CRealTimeShadowManager__Update_orig` | fn ptr | hook (:1665) + `DllMain` W (:8371) | **YES** — shadow + hook wiring |
| 2861 | `g_inCoronasRender` | `bool` | `coronaDrawRawShield` R; `SpriteFlushIndexedDraw_hook` R/W; `CPointLights__Render_shield_hook` R/W; `CoronaWindow_Close` W; `effectsForceAlphaBlock` R; `CCoronas__Render_blendprobe_hook` W; `CSprite__Render...` W | **YES** — corona + sprite-flush + effects + clouds |
| 2867 | `g_coronaBodyDone` | `bool` | `SpriteFlushIndexedDraw_hook` R/W; `CoronaWindow_Close` W; `effectsForceAlphaBlock`; blendprobe W; sprite capture | **YES** — same set |
| 2875 | `g_inCloudsRender` | `bool` | `SpriteFlushIndexedDraw_hook` R; `effectsForceAlphaBlock` R; `CClouds__Render_hygiene` W | **YES** — clouds + sprite-flush + effects |
| 2889 | `g_coronaPreValid` | `bool` | `CoronaWindow_Close` R/W; blendprobe R/W | no (corona section) |
| 2890 | `g_coronaPreFuncRw` | `int` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2891 | `g_coronaPreRefRw` | `int` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2892 | `g_coronaPreFuncDev` | `DWORD` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2893 | `g_coronaPreRefDev` | `DWORD` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2894 | `g_coronaPreSepAlpha` | `DWORD` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2895 | `g_coronaPreColorWrite` | `DWORD` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2904 | `g_coronaPreBlend` | `DWORD` | `CoronaWindow_Close` R; blendprobe W/R | no |
| 2905 | `g_coronaPreSrcBlend` | `DWORD` | `CoronaWindow_Close` R; blendprobe R | no |
| 2906 | `g_coronaPreDstBlend` | `DWORD` | `CoronaWindow_Close` R; blendprobe R | no |
| 2907 | `g_coronaPreBlendOp` | `DWORD` | `CoronaWindow_Close` R; blendprobe R | no |
| 2908 | `g_coronaPreZWrite` | `DWORD` | `CoronaWindow_Close` R; blendprobe R | no |
| 3042 | `s_spriteFlushState` | `SpriteFlushState` | `spriteFlushShieldCapture` W; `spriteFlushShieldClose` R | no |
| 3043 | `s_spriteForceValid` | `bool` | capture W; close W/R; `effectsForceAlphaBlock` R | **YES** — sprite-flush + effects |
| 3288 | `SpriteFlushIndexedDraw_orig` | fn ptr | hook (:3326,3376) + `DllMain` W (:8499-8500) | **YES** — sprite-flush + hook wiring |
| 3455 | `CPointLights__Render_orig` | fn ptr | hook (:3470-3471) + `DllMain` W (:8523,8547) | **YES** — corona + hook wiring |
| 3595 | `alphafunc` | `int` | `setMoonAlphaBlendStates` W; `restoreMoonAlphaBlendStates` R | no (moon mask) |
| 3624 | `moonMaskSavedRet` | `DWORD` | `renderMoonMaskBody` W/R | no |
| 3625 | `moonMaskSavedEsp` | `DWORD` | `renderMoonMaskBody` W/R | no |
| 3712 | `RenderScene_A` | `uint32_t` | `RenderScene` WRAPPER R; `installDeferredHooks` W (:5531) | **YES** — scene hook + hook wiring |
| 3748 | `s_camRasGame` | `RwRaster*` | `InvalidateCameraRasterCache` R; `RenderScene_before` W/R | no (framebuffer cache) |
| 3749 | `s_camRasGameW` | `int` | both functions R/W | no |
| 3750 | `s_camRasGameH` | `int` | both functions R/W | no |
| 3751 | `s_camRasScrW` | `int` | `RenderScene_before` R/W | no |
| 3752 | `s_camRasScrH` | `int` | `RenderScene_before` R/W | no |
| 4268 | `RenderEffects_orig` | fn ptr | `RenderEffects_hook` R; `DllMain` W (:8393) | **YES** — effects + wiring |
| 4309 | `CPostEffects__Render_orig` | fn ptr | hook R; `DllMain` W (:8552) | **YES** |
| 4401 | `CCoronas__RenderReflections_orig` | fn ptr | hook R; `DllMain` W (:8394-8395) | **YES** |
| 4411 | `CCoronas__RenderSunReflection_orig` | fn ptr | hook R; `DllMain` W (:8396) | **YES** |
| 4472 | `CCoronas__Render_trampoline` | fn ptr | blendprobe R (:4627-4628); `DllMain` W (:8458) | **YES** |
| 4820 | `s_fbWpState` | `volatile LONG` | `fbWp_veh` R/W; `fbWp_ArmOnce` R/W | no (FBWP probe) |
| 4821 | `s_fbWpVehInstalled` | `int` | `fbWp_ArmOnce` R/W | no |
| 4822 | `s_fbWpTotalFires` | `DWORD` | `fbWp_veh` W | no |
| 4823 | `s_fbWpWatchAddr` | `DWORD` | `fbWp_veh` R; `fbWp_ArmOnce` W/R | no |
| 4841 | `s_fbWpEips[16]` | `FbWpEip[]` | `fbWp_veh` W/R | no |
| 4842 | `s_fbWpEipCount` | `int` | `fbWp_veh` R/W | no |
| 4843 | `s_fbWpOverflow` | `DWORD` | `fbWp_veh` W/R | no |
| 5899 | `g_qualityPreset` | `int` | `readIniFile` W (:6020); `saveConfigTo` R (:7614) | **YES** — INI read + save |

Function-local `static` state also exists and must move with its function (not file-scope):
`getpath::tmppath`, `fixSAMP::Caps/FogConvTable`, `s_renderFrame` (`RenderScene_hook`),
`s_f4Prev` (`RenderScene_hook`), `s_deferredHooksInstalled` (`installDeferredHooks`),
`initCount` (`InitialiseGame_hook`), `s_buildFpLogged` (`InjectDelayedPatches`),
`s_probeBaselinePostFx` (`CPostEffects__Render_hook`), `s_cloudFaultCode` (`CClouds__Render_hygiene`),
`s_initCrashCode`/`s_initCrashPtrs` (`InitialiseGame_hook`), `s_fedFrame`
(`coronaFeedForwardPlus`), `multipassMultiplier`/`zoffset`/`shadowoffset`/`loadsc0` (`DllMain`),
`s_exp_*` byte tables (`DllMain`), `modeAliasMap`/`pipelineMap`/... (`readIniFile`),
`ShadowSite sites[2]` (`DllMain` local).

---

## 6. Log tags emitted from `main.cpp`

`dbglog()` has no tag argument; the bracketed prefix of the first format literal is used as the
logical tag. Dynamic tags are noted. Grouping by tag, all emission lines listed.

### 6A. `dbglog_throttle` / `dbglog_once` / `dbglog_first`

| Tag | Kind | Lines |
|---|---|---|
| `corona_ztest_retired` | once | 380 |
| `envf_clamp` | throttle | 397 |
| `envi_clamp` | throttle | 402 |
| `tmin_clamp` | throttle | 421 |
| `tmax_clamp` | throttle | 426 |
| `texp_order` | throttle | 433 |
| `tclow_clamp` | throttle | 446 |
| `tchig_clamp` | throttle | 451 |
| `tcmid_clamp` | throttle | 456 |
| `tspd_clamp` | throttle | 465 |
| `tkey_clamp` | throttle | 470 |
| `ssaop_clamp` | throttle | 487 |
| `chrome_clamp` | throttle | 511, 517, 522 |
| `cfenable_guard` | throttle | 575, 581, 586, 591 |
| `pipe_derive` | throttle | 674, 704, 709 |
| `shadow_heal` | once | 1499 |
| `shadow_tex` | once | 1579 |
| `shadow_contract` | throttle | 1601 |
| `shadow_contract_exit` | throttle | 1630 |
| `ped_tex_resync` | once | 1745 |
| `defpipe_contract` | throttle | 1824 |
| `ped_opaque_pre` | once | 1897 |
| `PedZWrite` | throttle | 1952, 2256 |
| `ped_contract_first` | once | 1995 |
| `ped_contract2` | throttle | 1995 |
| `defcb_shader` | throttle | 2033 |
| `ped_amb_cut` | throttle | 2080 |
| `ped_ambient` | throttle | 2134 |
| `ped_dual_skip` | throttle | 2158 |
| `ped_rs` | throttle | 2284 |
| `corona_draw_first` | once | 2958 |
| `corona_draw` | throttle | 2958 |
| `flushshield_restore` | once | 3210 |
| `sprite_flush_force` | once | 3338 |
| `cloud_shield_clouds` / `cloud_shield_sprites` (ternary) | once | 3369 |
| `corona_resync` | throttle | 3425 |
| `corona_window_close` | once | 3545 |
| `sky_raster_inv` | throttle | 3760 |
| `sky_raster` | throttle | 3913, 3918, 3926 |
| `fxalpha_producer_skip` | once | 4006 |
| `fx_alpha` | throttle | 4014 |
| `blend_subst` | throttle | 4109 |
| `corona_feed` | throttle | 4259 |
| `postfx_after` | throttle | 4333 |
| `corona_shield_first` | once | 4609 |
| `corona_shield` | throttle | 4609 |
| `corona_ztest` | throttle | 4621 |
| `corona_probe2` | throttle | 4642 |
| `sky_ps` | throttle | 5110 |
| `cloud_texraster` | once | 5177, 5402 |
| `clouds_fault` | throttle | 5315 |
| `water_fallthru` | throttle | 5628 |
| `cfg_save` | throttle | 7629 |
| `cfg_load` | throttle | 7658 |
| `mode_active` | once | 7833 |
| `tag` (parameter of `vs_probe`) | throttle | 1701 (dynamic) |
| `tag` (parameter of `verifyBytes`) | once | 8087 (dynamic) |

`dbglog_first` is not used in `main.cpp`.

### 6B. `dbglog` bracket tags

| Tag | Lines |
|---|---|
| `Config` | 576, 582, 587, 592, 675, 705, 710, 6268, 6364, 6413, 6484 |
| `ShadowHeal` | 1500 |
| `ShadowTex` | 1581 |
| `shadow_contract` | 1602, 1631 |
| `PedTex` | 1746 |
| `DefPipe` | 1826 |
| `PedAlphaTest` | 1891 |
| `PedOpaque` | 1898, 1996 |
| `PedCull` | 1937 |
| `PED` | 1953, 2159, 2257 |
| `probe` | 2031, 3307, 3347, 3783, 4295, 4324, 7781 |
| `DefCB` | 2043 |
| `Ped` | 2081, 2126, 2135 |
| `PedDual` | 2208, 2216 |
| `PedRS` | 2339 |
| `CoronaDraw` | 2963 |
| `FlushShield` | 3211 |
| `PointLights` | 3241 |
| `CoronaResync` | 3426 |
| `CoronaExit` | 3546, 4730 |
| `RENDER` | 3761, 3914, 3923, 3929, 5463 |
| `FxAlpha` | 4007, 4047, 4065, 4072, 4110, 4346 |
| `CoronaFeed` | 4260 |
| `CoronaShield` | 4610 |
| `CoronaZ` | 4622 |
| `CoronaBlend` | 4643 |
| `FBWP` | 4946, 4959, 4963, 4964, 4995 |
| `SKY` | 5123 |
| `CloudShield` | 3371, 5178, 5403 |
| `water` | 5629, 5709 |
| `CF` | 6601 |
| `Mode` | 7805, 7835, 7837 |
| `INIT` | 7844, 7853, 8460, 8463 |
| `BUILD` | 7882 |
| `skydome_install` | 7995 |
| `shadow_install` | 8262, 8270, 8297, 8302, 8318, 8322, 8354, 8359, 8375, 8379 |
| `corona_entry_install` | 8439, 8445 |
| `corona_install` | 8485, 8502 |
| `pointlights_install` | 8517, 8540 |
| *(no bracket tag)* | 381, 398, 403, 422, 427, 434, 447, 452, 457, 466, 471, 488, 512, 518, 523, 1698, 1702, 3790, 3846, 5316, 5456, 5536, 5540, 5547, 5561, 5566, 5568, 5570, 5573, 5580, 5589, 5591, 5592, 5593, 5596, 5597, 5599, 6014, 6235, 7163, 7630, 7659, 7813, 7819, 7824, 7829, 7906, 7910, 7913, 7919, 7922, 7937, 7941, 7945, 7949, 7951, 7953, 8041, 8053, 8069, 8091, 8124, 8125, 8127, 8128, 8129, 8133, 8136, 8138, 8151, 8164, 8169, 8171, 8641, 8654, 8656, 8663, 8665, 8672, 8681, 8689, 8694, 8697 |

Tag ownership suggestions for the split: config/INI (`Config`, `INIT`, `BUILD`, `CF`, clamp
tags), mode (`Mode`, `mode_active`), shadow (`ShadowHeal`, `ShadowTex`, `shadow_contract*`,
`shadow_install`), ped (`Ped*`, `PED`, `PedZWrite`, `ped_*`), corona/flush
(`Corona*`, `FlushShield`, `flushshield_restore`, `sprite_flush_force`, `PointLights`,
`corona_*`), effects (`FxAlpha`, `fx_alpha`, `blend_subst`, `postfx_after`), clouds
(`CloudShield`, `cloud_texraster`, `clouds_fault`, `SKY`), raster (`RENDER`, `sky_raster*`),
water (`water`, `water_fallthru`), FBWP (`FBWP`), menu/init wiring (no-tag site lines).

---

### Extraction notes (mechanical consequences)

- 1A is the hard boundary: those symbols need a shared header (most already live in
  `skygfx.h` or `main_exports.h`). 1B can be moved to a private header or made `static`
  without touching other TUs.
- 1C statics move with their section, except the cross-section entries in §5
  (`s_activeModeIni*`, `g_noiseQuality`, corona/cloud/sprite flags, hook-orig pointers,
  `defaultColour*`, `g_qualityPreset`) which must be externalised or grouped in one owner TU.
- 128 hook/patch call sites, all in three functions (`DllMain`, `InjectDelayedPatches`,
  `installDeferredHooks`) except `sites[]`-driven `0x53E0BE`. Only 14 have a byte check;
  the rest are blind and remain order-sensitive if the install code is split.
- All SEH scopes are POD-only; no C2712 barrier. The only compiler-sensitive regions are the
  12 naked functions and 5 inline-asm blocks, which must stay in one TU or be moved as a
  single asm unit.
