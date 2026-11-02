# Merge / dedup / deletion rinse — 2026-10-29

**Status:** READ-ONLY rinse. No source, build, shader or docs edits were made to produce this
document. The only commands run were read-only: `wc -l`, `grep`, `file`, a byte-level
line-ending counter, a static-reference scanner, and `python tools/gen_ini_ref.py --check`
/ `--stdout` (neither writes). The one artefact produced is this file.

**Scope:** all of `src/` (24 render `.cpp` + 17 render `.h`, 7 core `.cpp`, 6 core `.h`,
`src/entities/*`, `src/extras/*`, `src/rw/*`, `src/texdefs.*`, `src/skygfx.h`), `shaders/`,
`resources/`, and the build metadata.

**Template** is unchanged from `docs/plans/2026-10-27-service-pattern-inventory.md`
(file layout / API / INI / reset rules) and
`docs/plans/2026-10-27-render-refactor-roadmap.md` (fortify pattern, C1-C7 constraints,
the SEH safety argument). Items those two docs already ruled out are **not** re-proposed
except where a status change is reported.

## Baseline (measured today, working tree)

| Metric | Value |
|---|---|
| `src/**/*.cpp` | 40 files |
| `src/render/*.cpp` | 24 files / **33,982 lines** (roadmap 10-27 said 53 files / 51,345 ln) |
| `src/render/*.h` | 17 files |
| Non-render src (cpp+h) | 27,561 lines |
| `build/skygfx.vcxproj` `ClCompile` / `ClInclude` rows | **163 / 22** |
| Duplicate `ClCompile` basenames (LNK4042 gate) | **0** (gate clean) |
| `src/` files carrying `__finally` | **2** — `envmap.cpp` (14), `mirrorPass.cpp` (8); everything else 0 |
| TUs with naked asm / `_asm` | 3 — `main.cpp`, `ragdoll_death.cpp`, `texdefs.cpp` |
| Git | HEAD `898e894`, **376 dirty entries — every prior merge in this series is UNCOMMITTED** |

> **C0 (carried forward):** never `git checkout` / `git stash` this tree. Every "DONE" merge
> listed in the prompt exists only as working-tree changes. A rollback = manual restore from
> `_backup/<lane>_<date>/`, not VCS.
>
> **C0b (observed 2026-10-29, before this rinse):** a further lane is in flight and has
> already written `tools/fast_build.py` (+544/-31, adds `--helper-tex` and
> `--verify-embed`, mtimes 00:05/00:06) and added the untracked
> `shaders/ps/Water_Underwater.hlsl` (01:17) + its `.cso` (01:17). **Stage 5 (shader
> deletions) and Stage 0 (build-tool work) must be serialised against that lane.**
> Nothing in this rinse touched those files.

### SEH census (per-TU, `__try` / `__except` / `__finally`)

`main.cpp` 28/25/0 · `postfx.cpp` 27/16/0 · `postfx_ssao` 13/9/0 · `envmap` 16/5/**14** ·
`mirrorPass` 6/7/**8** · `waterPipe` 6/4/0 · `postfx_smaa` 5/1/0 · `pipelinecommon` 5/2/0 ·
`buildingPipe` 4/1/0 · `neopipe` 4/3/0 · `postfx_motionblur` 4/3/0 · `postfx_underwater` 4/4/0 ·
`chars` 4/2/0 · `debug_handl` 4/4/0 · `texalpha` 3/2/0 · `renderstate_monitor` 2/2/0 ·
`postfx_colourfilter` 1/0/0 · `sftbdyphsx` 1/1/0. **C2702 is live only in `envmap.cpp` and
`mirrorPass.cpp`** — unchanged from the roadmap.

---

## 0. What landed since 2026-10-27 (so it is not re-proposed)

| Lane | Status proof |
|---|---|
| `texdefs`, `debug_handl`, `sftbdyphsx`, `neopipe`, `grasssys`, `*_mode` stubs | files exist / are gone as expected |
| `blendToD3D` dedupe + 2 `#if 0` deletions (C7) | `buildingPipe_blendToD3D`/`vehiclePipe_blendToD3D` gone; `grep -rn "^\s*#if 0" src/ shaders/` -> **no matches** |
| `utilitynoise.cpp` deletion (roadmap Stage 4) | file gone; `grep -rn "g_pUtilityNoise\|GenerateUtilityTexture" src/` -> 0 hits. `premake5.lua:187-188` documents it |
| `defaultFuncs.cpp` deletion | file gone |
| Legacy DebugMenu UI + `debugmenu_public.h` deletion | `src/extras/debugmenu_public.h` gone; `debugmenu_ui.cpp` 4,461 -> **4,131** |
| `debug_handl.cpp` prune (roadmap Stage 5, partial) | 1,207 -> **662**; `menu_inject.cpp` section gone |
| `chars.cpp` prune (roadmap Stage 6, partial) | 817 -> **552** |
| Roadmap Stage 2.1/2.2 — `envParkSetState3L` / `MirrorRestoreState3L` -> canonical primitives | `envmap.cpp:376-379`, `mirrorPass.cpp:130-133` now call `pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set`; `mirrorPass.cpp:108-109` documents the conversion |
| `TexDefsGetD3DTexture` (inventory 3.1) | `texdefs.h:49`, impl `texdefs.cpp:605`, consumer `texalpha.cpp:591` |
| Shader L13/L14/L15 | `include/TonemapCommon.hlsl`, `include/BloomCommon.hlsl`, `include/SSAOCommon.hlsl` exist and are included; `PerlinNoise.hlsl`, `CarPaintNoise.hlsl`, `SubsurfaceScattering.hlsl`, **`CommonMath.hlsl`**, `ps/SSAO.hlsli` and ~15 subdir sources **deleted** |
| Roadmap Stage 1 (`pipeParkSet3L` + `pipeFxAlphaLegPush`) | **NOT landed** — `grep -rn "pipeParkSet3L\|pipeFxAlphaLegPush" src/` -> 0 hits |
| Roadmap Stage 3 (`postfx_support.cpp`) | **NOT landed** — `grep -c postfx_support build/skygfx.vcxproj premake5.lua` -> 0 / 0 |

---

## 1. The surviving merge: `postfx_luminance` + `postfx_motionblur` + `postfx_utils` -> `postfx_support.cpp`

**Verdict: MERGE — still fully viable.** Every gate re-verified against the current tree.

| Gate | Roadmap claim | Measured today | OK |
|---|---|---|---|
| Line counts | 290 + 302 + 338 = 930 | **290 + 302 + 338 = 930** (unchanged) | yes |
| `__finally` | 0 / 0 / 0 | `grep -c "__finally"` -> **0 / 0 / 0** | yes |
| Other SEH | motionblur "4 `__try`" | `__try`/`__except`/`__finally` = lum **0/0/0**, mblur **4/3/0**, utils **0/0/0** -> C2702 structurally impossible | yes |
| Static collisions | 0 duplicate statics | luminance `{g_lumaMeas*, g_lumaAdapt*, g_lumaAdaptFlip, EnsureLuminanceTargets, RenderLumaPass}`; motionblur `{g_velocitySurf, s_velocityW/H, g_prevVPMatrix, g_prevVPValid}`; utils `{s_trackedRasters, s_trackedRasterCount, g_screenSizeW/H, s_rawGeomStateIds, RestoreOneRawGeomState}` — **disjoint** | yes |
| Non-static collisions | n/a | `luminanceReducePS/AdaptPS`, `g_velocityTex`, `colorfilterVerts/Indices`, `s_ffQuad`, `s_ffQuadIdx` — **all unique**, all declared in `postfx.h:91-94,132,167` (+ `skygfx.h:1179` for the luminance pair). `postfx.h` needs **no edit** | yes |
| Dynamic file-scope init | none | every initialiser is `NULL`/`0`/constant aggregate/constant address (`0xC400D8`, `0x8D5174`) — no dynamic init to reorder | yes |
| EOL | all LF | byte count: `crlf=0` in all three (290/302/338 pure-LF lines) | yes |
| Include union | 5 lines | `skygfx.h`, `debug_handl.h`, `postfx.h`, `profiler.h` (+ `depthhook.h` from motionblur) — exactly as documented; strict UNION, nothing extra (C2712 guard) | yes |
| Basename / LNK4042 | unique | `postfx_support` absent from vcxproj+premake+src; duplicate-basename scan over 163 rows -> **empty** | yes |
| Build rows to swap | 3 -> 1 | `build/skygfx.vcxproj:195,196,200`; `.filters:129,132,144`; `premake5.lua:220,221,225` | yes |

**Risk: LOW.** SEH/C2702 impossible (0 `__finally`); C2712 guarded by the strict include
union; no line-ending normalisation (adopt **LF**); verbatim bodies => no signature drift.
Stale-object purge required before link: `build/obj/Release/{postfx_luminance,postfx_motionblur,postfx_utils}.obj`
(U2) — note `build/obj/Release/utilitynoise.obj` and its `.tlog` entry still exist today and
should go in the same purge.

**Savings:** -2 `ClCompile` rows, `src/render` 24 -> 22 `.cpp`; file ~= 950 lines (930 bodies
+ prologue + 3 banners + 5-line include union - 14 original include lines). The win is TU
count, not lines.

---

## 2. Remaining render-state idiom duplication

### 2.1 Canonical primitive usage today (code call sites only, comments excluded)

| Primitive | Total | `pipelinecommon.cpp` (doctrine) | `envmap.cpp` | `mirrorPass.cpp` | `postfx_utils.cpp` |
|---|---:|---:|---:|---:|---:|
| `pipeRwParkSet(` | 10 | 7 | 1 (`:376`) | 1 (`:130`) | 1 (`:235`) |
| `pipeL2ParkSet(` | 14 | 11 | 1 (`:377`) | 1 (`:131`) | 1 (`:238`) |
| `pipeL3Set(` | 37 | 34 | 1 (`:379`) | 1 (`:133`) | 1 (`:242`) |

`pipeParkSet3L` / `pipeSave3L` / `pipeFxAlphaLegPush` do **not exist** (0 hits in `src/`).

### 2.2 The one dedup that is actually left: three byte-identical wrappers

`envmap.cpp:370-380 envParkSetState3L`, `mirrorPass.cpp:124-134 MirrorRestoreState3L` and
`postfx_utils.cpp:227-243 RestoreOneRawGeomState` are the same five-line body
(`if(rwState != NA) pipeRwParkSet(...)` -> `pipeL2ParkSet(...)` ->
`_rwD3D9RenderStateFlushCache()` -> `pipeL3Set(...)`), each with its own ~15-line comment
block asserting it is the canonical order.

* **Evidence:** one call of each primitive in each file (table above); the three bodies
  differ only in the function name.
* **Est. savings:** ~15 lines (3 bodies -> 3 one-line wrappers) once `pipeParkSet3L` is added
  to `pipelinecommon.cpp` beside `pipeL3Set` (`pipelinecommon.cpp:1149`) + a `skygfx.h` decl.
* **Risk: LOW-MEDIUM — SEH-aware.** `MirrorRestoreState3L` sits next to the
  `MirrorCompositeSaved` POD/`__finally` pattern (`mirrorPass.cpp`, **8 `__finally`**) and
  `envParkSetState3L` inside `envmap.cpp` (**14 `__finally`**). Convert **the helper body
  only**; never move it inside a `__finally` (C2702). EOL: `envmap.cpp` CRLF, `mirrorPass.cpp`
  LF — keep each file's own convention, no normalisation.
* **Verdict: MERGE** (low priority; doctrine win, not a size win).

### 2.3 Hand-rolled raw write runs still outstanding

Counting *consecutive runs of >= 3* `RwRenderStateSet` / `RwD3D9SetRenderState` /
`d3d9device->SetRenderState` in one place (comments excluded):

**189 runs across 18 TUs** — `main.cpp` 58, `postfx.cpp` 32, `postfx_colourfilter` 15,
`vehiclePipe` 13, `buildingPipe` 12, `neopipe` 11, `postfx_ssao` 11, `postfx_newfx` 8,
`postfx_smaa` 7, `envmap` 4, `pipelinecommon` 4, `waterPipe` 4, `chars` 2, `forwardplus` 2,
`postfx_motionblur` 2, `postfx_underwater` 2, `mirrorPass` 1, `postfx_luminance` 1.

Two are explicitly the inventory "hand-rolled three-layer park-and-set" copies:

* `main.cpp:2946-2975` (corona per-draw force) and `main.cpp:3488-3512` (corona pre-block) —
  both re-implement `park -> target` per layer for `ALPHABLENDENABLE`, `SRCBLEND`,
  `DESTBLEND`, `ZWRITEENABLE` (+ raw L3 for the first), using `coronaRwBlendFromD3D` for the
  rw twin, and call **neither** `pipeRwParkSet` **nor** `_rwD3D9RenderStateFlushCache`.

* **Corrections to the 10-27 inventory:**
  * `neopipe.cpp:400 carBlendRestore3L` is **not** a park-and-set copy — it is a 3-layer
    *save/restore* (`carBlendSave3L` `:372`) with `carBlendClampDev` domain clamping and
    **no park**. Converting it to `pipeParkSet3L` would change behaviour. -> **LEAVE**
    (reclassify).
  * The remaining runs are per-pass draw sequences (postfx / colourfilter / ssao), which the
    inventory already classifies as `SaveRawGeomStates`/`RestoreRawGeomStates` work, not
    park-set work.

* **Verdict: MERGE-INTO-SERVICE / TEMPLATE-APPLY** (inventory 3.3 + 1.2), **not** a file
  merge. Touches `main.cpp` + `skygfx.h` => serialise with any other lane that writes those.
  Risk MED — corona is the black-quad-sensitive path; the roadmap 3.2 behaviour-equivalence
  argument (identical end state on all three layers; transient park never reaches the device;
  flush still precedes the L3 pin) applies.

### 2.4 Not this rinse

`pipeForceAlphaBlock` is still owned by the **wrong TU** — defined `buildingPipe.cpp:525`
(void overload; `PipeAlphaState` overload `:531`), declared three times locally in
`main.cpp:834`, `:3649`, `:3964`; the canonical `pipeEnterAlphaMode` lives in
`pipelinecommon.cpp` (`skygfx.h:1441`). Inventory 3.13 (**MERGE-INTO-SERVICE**: move
`buildingPipe.cpp:522+` verbatim next to `pipeEnterAlphaMode`) has **not** landed.
**Verdict: MERGE** (relocate; ~0 line delta, -3 local `extern`s), risk MED — FX alpha path,
`main.cpp` is a shared file, `buildingPipe.cpp` carries 4 `__try`.

---

## 3. Dead code census

### 3.1 `#if 0` blocks — none remain

```
grep -rn "^\s*#if 0" src/ shaders/   ->   no matches
```
C7's two `#if 0` deletions landed and no new ones appeared.

One **preprocessor-disabled** block exists and is intentional:
`src/render/envmap.cpp:191-243` is inside `#ifdef DEBUGENVTEX` with the switch commented one
line above (`envmap.cpp:14: //#define DEBUGENVTEX`), alongside a sibling `#ifdef DEBUG` at
`:13`. It contains `static RwIm2DVertex screenQuad[4]` (`:192`), `MakeQuadTexCoords` (`:196`)
and `MakeScreenQuad` (`:216`) — `MakeScreenQuad` has **0 call sites even inside the block**.
-> **Verdict: LEAVE** (documented debug hatch, same class as `#ifdef DEBUG`; deleting it is a
53-line optional cleanup, risk 1).

### 3.2 Zero-reference statics (definition-only scan over all 40 `.cpp`)

| Location | Symbol(s) | Lines | Proof | Verdict |
|---|---|---:|---|---|
| `src/core/main.cpp:1626-1634` | `CShadows__RenderStoredShadows_hook` | ~9 | `grep -rn CShadows__RenderStoredShadows src/` -> only `main.cpp:1491` (`_orig`), `:1630` (def), `:1632` (its own body). No `InjectHook(0x70A960...)` install exists — only `:7994` (`UpdateStatic`) and `:8060` (`RenderStatic`) are installed | **DELETE** (+ `:1491` `_orig`; ~15 ln total) |
| `src/extras/ragdoll_death.cpp:155-158` | `ReadIniInt` | 4 | 1 occurrence in the tree (its definition). `ReadIniFloat` (`:145`) has 11 real callers | **DELETE** |
| `src/render/envmap.cpp:191-243` | `MakeScreenQuad` (+ `MakeQuadTexCoords`, `screenQuad`) | 53 | inside `#ifdef DEBUGENVTEX`; `MakeScreenQuad` 0 call sites | **LEAVE** (3.1) |
| `src/texdefs.cpp:916-1021` | `DestroyLight`, `SetupDirectionalLight`, `UnsetDirectionalLight`, `SetupAmbientLight`, `UnsetAmbientLight` | ~106 | each has **1 occurrence in the tree** (its definition). `CreateLight` (`:896`) *is* called (`:1068`) | **DELETE** (chain below) |

**texdefs light chain (must be cut as one unit):** `TexDefsExternalShutdown`
(`texdefs.cpp:1141-1147`, `texdefs.h:20`) has **0 callers** and is the *only* caller of
`DestroyLight`. Delete `TexDefsExternalShutdown` + `DestroyLight` + the four `Setup/Unset*`
statics together. `TexDefsExternalInit` (`:1135`) and `TexDefsTryAttachExternal` (`:1027`)
are **live** (`main.cpp:7689`, `main.cpp:5460`) and stay.

**Est. ~110 lines. Risk LOW (0 refs), with one follow-up:** `gAmbClr`, `LightFlags`,
`GetAmbientLight`, `extraDirectionalLights`, `sunColorsContribution`, `useSunColors`,
`gfNightState` are referenced **only from `texdefs.cpp`** (26 hits, all in that file) — after
the cut, re-run the zero-ref static scan for newly-orphaned helpers. Note
`LightFlags(gDirectionalLight) = 1` is set **only** by the dead `SetupDirectionalLight`, so
the created light never turns on — the cut is behaviour-neutral on that axis.
**Verdict: DELETE.**

### 3.3 Unused exports / stale declarations

**A. `src/skygfx.h` — declarations with NO definition and NO use anywhere (0 refs):**

| Line | Declaration | Origin |
|---:|---|---|
| 1246 | `void UpdateVehicleRing();` | orphan |
| 1276 | `void D3D9RenderNotLit(...)` | leftover of the `defaultFuncs.cpp` deletion |
| 1277 | `void D3D9RenderPreLit(...)` | " |
| 1278 | `RwBool DNInstance_default(...)` | " |
| 1279 | `void CCustomCarEnvMapPipeline__CustomPipeRenderCB(...)` | " |

-> **DELETE, 5 rows, risk 1** (zero refs cannot break the build). Roadmap Stage 1 listed
`skygfx.h:1290-1293` for this cleanup; only the `...CreateCustomObjPipe` row moved — these
five were missed.

**B. `skygfx.h` declaration + definition, but ZERO callers (remove, or `static` the pipelinecommon five):**

| skygfx.h | definition | callers |
|---:|---|---:|
| 1251 `MakeProjectionMatrix` | `pipelinecommon.cpp:325` | 0 |
| 1254 `pipeGetCameraTransformMatrix` | `pipelinecommon.cpp:396` | 0 |
| 1268 `pipeUploadLightDirectionInv` | `pipelinecommon.cpp:632` | 0 |
| 1545 `GetTimecycleAmbientRaw` | `pipelinecommon.cpp:240` | 0 |
| 1547 `GetLightsMult` | `pipelinecommon.cpp:246` | 0 |
| 1008 `CPostEffects::SetFilterMainColour` | `postfx.cpp:50` (WRAPPER) | 0 |
| 1012 `CPostEffects::SpeedFX` | `postfx.cpp:42` (WRAPPER) | 0 |

-> **~40 lines, risk 1-2.** For the two WRAPPERs, first check the readers of
`m_bSpeedFX` / `m_bSpeedFXTestMode` / `m_SpeedFXAlpha` (`skygfx.h:1064-1066`) — those may go
in the same pass if they too are dead.

**C. `src/rw/gta.cpp` dead WRAPPERs (+ their `skygfx.h` decls):**

| gta.cpp | skygfx.h | Status |
|---:|---:|---|
| 149 `DNInstance` | 1287 | **0 callers** -> DELETE |
| 150 `D3D9RestoreSurfaceProperties` | 1289 | **0 callers** -> DELETE |
| 156 `CCustomCarEnvMapPipeline__CustomPipeAtomicSetup` | 1291 | **0 callers** -> DELETE |
| 158 `gtaGetPipelineID` | 1293 | **0 callers** — distinct from the LIVE `GetPipelineID` (`pipeplg.cpp:8`; called `main.cpp:2343/3553`, `buildingPipe.cpp:3311/3540`, `texdefs.cpp:106/113`) -> DELETE |
| 164 `GTAfree` | 1296 | **0 callers** -> DELETE |
| 163 `...CustomPipeRenderCB_exe` | 1295 | **LIVE** — `vehiclePipe.cpp:3674` -> KEEP |

-> **~15 lines, risk 1.** EAXJMP wrappers are inert when unreferenced. Do **not** touch
`gta.cpp`'s mixed EOL (231 CRLF + 1 LF) — just delete whole lines.

**D. Header-by-header stale surface (explicitly requested: `src/entities/*.h`, `src/extras/*.h`)**

| Header | Finding | Evidence | Verdict |
|---|---|---|---|
| `src/entities/chars.h` (55 ln; includers `main.cpp:5`, `chars.cpp:2`, `postfx.cpp:5`) | **`main.cpp` uses nothing from it** (every `chars.h` symbol searched in `main.cpp` -> 0). Dead: `enum PartType`+`PARTTYPE_*` (`:14-25`), `struct PartSSSParams` (`:28-37`), `struct AtomicCharInfo` (`:40-44`, **0 refs in tree**), `MAX_CHAR_ATOMICS` (`:46`, **0 refs**), `chars_shutdown` (`:48` decl; body `chars.cpp:510`, **0 callers**), `extern void *SkinPBR` (`:52` — duplicate of `skygfx.h:1209`; `SkinPBR` is defined `pipelinecommon.cpp:52` and is **never `makePS`'d nor bound**), `extern g_charsClassifyTex` (`:55`, only `chars.cpp` uses it). Plus `chars.cpp:9-20 PartSSSParams g_partSSSParams[NUM_PARTTYPES]` has **0 readers** | **DELETE ~= 60 ln** (chars.h ~35 + chars.cpp table ~13 + `chars_shutdown` body ~10) and drop `main.cpp:5`. **KEEP `chars.h:53 extern void *SSS_Blur`** — it is the *only* declaration of `SSS_Blur` (defined `pipelinecommon.cpp:38`; consumed `postfx.cpp`, `chars.cpp`), and **KEEP** `chars_drawSSSBlur` / `chars_OnDeviceReset` (`postfx.cpp:2021/2287/2651`). Risk **LOW-MEDIUM** — the SSS path is live; launch must still log `[PostFX] chars_drawSSSBlur ENTER` |
| `src/entities/sftbdyphsx.h` (206 ln) | only forward decls at 0-1 refs (`AnimBlendFrameData`, `CAnimBlendClumpData`, `CAnimBlendLink` are forward declarations; `BlendKeyframe` decl+def) | **LEAVE** — forward decls are free and correct |
| `src/entities/PlantSurfPropMgr.h` (32 ln) | `CPlantSurfPropMgr` / `CPlantSurfPropPlantData` -> **0 refs in tree**; only `CPLANT_SURF_PROP_PLANTDATA_NUM` is used (`grasssys.h:245-246`) | **ABSORB** into `grasssys.h` (candidate E) |
| `src/extras/ragdoll_death.h` (143 ln) | `extern const int32 g_ragdollDeathParentIdx[]` (`:67`) and `extern const float g_ragdollDeathAngleLimits[]` (`:68`) are declared + defined (`ragdoll_death.cpp:236`, `:252`) but have **0 readers in the whole tree** (the only `g_ragdollDeath*` hits outside the TU are `g_ragdollDeathEnable`, a different symbol). `:252`'s own comment says "used for existing verlet fallback if kept" | **DELETE after one confirmation pass**, ~32 ln. `RagdollDeathState` (`:73`) *is* used by the `state` field (`:81`) -> LEAVE |
| `src/extras/wheels.h` (44 ln) | only includer is `wheels.cpp` | **DELETE with `wheels.cpp`** (section 4) |
| `src/extras/brdfLibrary.h` (627 ln) | 3 live includers: `veh_shaders.cpp:12`, `buildingPipe.cpp:3`, `vehiclePipe.cpp:3` | **RELOCATE** -> `src/render/` (candidate F), not delete |

**E. Other live-file dead API (the 10-27 audit's Stage 3 — still open):**

| Item | Evidence | Est. | Verdict |
|---|---|---:|---|
| `veh_shaders.cpp:288-312` dirt block (`VehShaders_GetDirtLevel`, `VehShaders_ApplyDirtToPBR`) | grep -> `veh_shaders.cpp` definition + `skygfx.h:1149/1150` declaration only. `vehiclePipe.cpp:3086-3098` computes `dirtLevel` locally from era, never via the API | 25 ln + 2 decls | **DELETE** |
| `veh_shaders.cpp:411-539` colour-area system (`enum VehColorArea`, `minSaturation[]`, `maxBrightness[]`, `CheckColorSaturation`, `GenerateColor`, `GetColorArea`, `GetMinSaturation`) | every symbol: definition + `skygfx.h:1153-1157` declaration, **0 external callers**; `GetMinSaturation` is the only caller of `GetColorArea` | 129 ln + 7 decl lines | **DELETE** |
| `debug_handl.cpp:293-301` `s_heartbeat`+`diag_heartbeat`; `:356-363` `diag_removeVEH`; `debug_handl.h:48/:63` decls; `debug_handl.h:66 diag_checkCrashMarker` | grep -> only the header's own `DIAGNOSTICS_DISABLED` stub macros (`:34-36`). **`diag_checkCrashMarker` is declared and never defined** (would be LNK2001 if called). `main.cpp:7869` records the watchdog as removed | ~25 ln | **DELETE** — keep `diag_init/installVEH/getLogPath/registerScope/ringDump` |
| `weather.cpp:61-70` `Weather_SelectSet`/`Weather_GetBlendFactor` + `weather.h:22/25` | only callers are `weather.cpp:68` and `:330`. `weather.h:8 enum WeatherSet` tag has **0 refs** (enumerators used only by the always-2 abstraction) | 10 ln + 3 decl rows | **MAKE STATIC + prune header** |
| `debugmenu_theme.cpp:260-264` `UI_ImGuiIsInitialized` + `debugmenu_theme.h:84` | definition + declaration only, 0 callers | 6 | **DELETE** |
| `vehicles.cpp:377-378` `GetVehicleDBCount` / `GetVehicleDB` | 0 refs outside their definitions | 2 | **DELETE** |
| `vehicles.cpp:342` `GetVehicleEntry` | internal-only (`:350`, `:355`, `:360`) | qualifier only | **MAKE STATIC** |
| `veh_shaders.cpp:21` `extern int GetVehicleDrive(...)` | 0 uses inside `veh_shaders.cpp`; live caller is `vehicles.cpp:374` | 1 | **DELETE extern** |
| `texdefs.cpp` `TexDefsSetDefinedShader` + `texdefs.h:53` | 0 callers outside `texdefs.*` | ~10 + 1 | **DELETE** |
| `texdefs.h:20 TexDefsExternalShutdown` + `texdefs.cpp:1141` | 0 callers | 7 + 1 | **DELETE** (3.2) |
| `texdefs.h:31 enum TexDefsD3DWhy` | **NOT dead** — enumerators used at `texalpha.cpp:590-609` and `texdefs.cpp:608+` | — | **LEAVE** (a tag-name-only grep gives a false positive here) |
| `noise.h` `SkyNoise` members with **0 callers**: `Perlin2`, `Perlin3`, `PerlinTileable2`, `Fbm2`, `WorleyFbm2`, `UniversalSize`, `UniversalData`, `UniversalTexture`, `LogStats` | declaration + body only | ~10 decls + bodies | **MAKE STATIC / DELETE** — **except `SetQuality`**: it is the intended target of `[General] NoiseQuality` (section 6) -> **LEAVE `SetQuality` until section 6 is decided** |
| `skygfx.h:277 presetConfigs` extern | 0 readers outside `presets.cpp` (`:25` def, `:123` use) | 1 | **MAKE STATIC** (rides along with candidate B) |
| `debugmenu_ui.cpp:144 UploadUnifiedConstants` + `skygfx.h:1245` | self-declared dead; early `return;` at `:146`; body retained "for reference" | — | **LEAVE** — roadmap 3.3 explicitly ruled this; not re-proposed |

---

## 4. `wheels.cpp` dead API — DELETE confirmed, unchanged

```
grep -rn "Wheels_" src/
  src/extras/wheels.cpp:47,57,104,111,157,162,167   (definitions + own log lines)
  src/extras/wheels.h:32,35,38,41,44                (declarations)
  src/render/vehiclePipe.cpp:215-216                (local extern + call)
grep -rln "wheels.h" src/   ->   src/extras/wheels.cpp, src/extras/wheels.h   (0 other includers)
```

* Only `Wheels_Init` has a call site, and it is declared **locally** in `vehiclePipe.cpp:215`
  — `wheels.h` is not even on that path.
* Zero-call-site exports: `Wheels_SelectForVehicle` (`:111`), `Wheels_GetEntry` (`:157`),
  `Wheels_GetClassForModel` (`:162`), `Wheels_GetCount` (`:167`); `g_wheels[MAX_WHEELS]`
  (`:17`) and `g_wheelsInitialized` (`:19`) are read only by those.
* **Savings: 169 + 44 = 213 lines**, -1 `ClCompile` (`vcxproj:217`), -3 lines in
  `vehiclePipe.cpp:215-216`. No `.filters` row, no `premake5.lua` row (glob), no
  `fix_build.py` row, no `skygfx.h` rows.
* **Risk: LOW.** CRLF/CRLF (irrelevant on delete); no SEH, no naked asm, no `__finally`.
  Visible delta: the `Wheels_Init: ...` line disappears from the launch log (expected).
* **Verdict: DELETE** (backup `_backup/wheels_del_2026-10-29/`).

**Other zero-call-site public APIs found this pass:** section 3.3 — notably the `veh_shaders`
dead blocks (~162 ln), `TexDefsExternalShutdown` / `TexDefsSetDefinedShader`,
`diag_heartbeat` / `diag_removeVEH` / `diag_checkCrashMarker`, `chars_shutdown`, and the 9
`SkyNoise` members.

---

## 5. Shader side — duplicates remaining after L13/L14/L15

**Verified landed:** `TonemapCommon.hlsl` (included by `TonemapPass.hlsl:67`,
`TonemapLUT.hlsl:59`), `BloomCommon.hlsl` (`Bloom_Extract:26`, `Bloom_Downsample:35`,
`Bloom_Composite:25`, `LuminanceReduce:17`), `SSAOCommon.hlsl` (`SSAO.hlsl:42`,
`SSAO_Temporal.hlsl:66`; header states "extracted VERBATIM (L14 dedupe)"). Deleted includes:
`PerlinNoise.hlsl`, `CarPaintNoise.hlsl`, `SubsurfaceScattering.hlsl`, **`CommonMath.hlsl`**,
`ps/SSAO.hlsli`.

### 5.1 Duplicates that remain (grep proof)

| # | Symbol | Sites | Identical? | Est. | Risk | Verdict |
|---|---|---|---|---:|---|---|
| S1 | `GetViewPos` | `ps/SSAO.hlsl:31`, `ps/SSAO_VertexDepth.hlsl:28`, `ps/SSAO_ps20.hlsl:18` (SSAOCommon carries only `ProjectToUV`) | yes (`ndc=uv*2-1; ndc.y=-ndc.y; viewZ=projInfo.z/(depth-projInfo.w)`) | ~24 ln | LOW | **LEAVE after deleting the dead SSAO sources** — 2 of the 3 hosts are unloaded (5.2); delete them first and the duplicate disappears for free |
| S2 | `hash` / `noise3d` / `fbm` | `ps/DynamicSky.hlsl:33/37/51` vs `ps/IBL_SkyCloud.hlsl:25/44/58` | yes | ~40 ln | LOW — `--check-shaders` | **MERGE** -> new `include/CloudNoise.hlsl` (shader-audit Stage 2, **not landed**). Then alias `PBR_Common.hlsl:78-95 PBR_chash/PBR_cnoise3d/PBR_cloudFBM` onto it (3rd, renamed copy) |
| S3 | `Luma` (Rec.709) | **8 copies**: `ColorFilter_CrossMix:28`, `HairEnhance:28`, `MotionBlur_Burnout:35`, `SkinEnhance:28`, `SMAA_EdgeCombined:34`, `SMAA_EdgeMotionDepth:40`, `SMAA_Temporal:46`, `SunShafts:45` | yes | ~24 ln | LOW | **MERGE-INTO-SERVICE, but the proposed home `CommonMath.hlsl` was DELETED in L13/14 instead of wired.** A host decision is required first: wire `BloomCommon.hlsl` (already provides `Luminance`, 4 includers) or add a dedicated `Luma.hlsl`. Do **not** resurrect `CommonMath.hlsl` — it was explicitly unwired (`:9-11`) and has been removed |
| S4 | `D_GGX` / `V_SmithCorrelated` / `F_Schlick` | `include/PBR_Common.hlsl:27/37/15` (guarded `max(d,1e-7)`) vs `ps/PBR_Lighting.hlsl:55/64/74` (unguarded) vs `ps/SkinSSS.hlsl:32/49` | near | ~60 ln | MED — the unguarded copies are a NaN source if ever bound | **DELETE the two dead sources first (5.2); then LEAVE `PBR_Common`** |
| S5 | `hash22` / `perlinNoise` / `metallicFlake` | `ps/CarPaint_Reflections.hlsl:50/56/70` only | n/a | 0 | — | **RESOLVED** by the `CarPaintNoise.hlsl` deletion — single copy now |
| S6 | `LinearToSRGB` (fast) | `include/colorSpace.hlsl:91` only | n/a | 0 | — | **RESOLVED** (was `CommonMath.hlsl:107`); `colorSpace.hlsl` itself is reachable only from the dead `unifiedPipe` family -> **LEAVE** |
| S7 | `UnpackNormal`, `SafeNormalize` guards, `LinearizeDepth` | inline in `buildingPipePS.hlsl:142`, `normMapBuildingPS.hlsl:39`, `PBR_Lighting.hlsl:84`, `PipeChain.hlsl:138`, `HairEnhance.hlsl:38`, `SSAO_Temporal.hlsl:81`; `LinearizeDepth` has 3 different formulas | near / no | ~40 ln | LOW | **LEAVE** — `LinearizeDepth` is explicitly "keep per-pass, do not merge"; most `UnpackNormal` copies live in files that are themselves dead (5.2) |

### 5.2 NEW: loaded-but-unbound pixel/vertex shader handles

Nine handles are created in `pipelinecommon.cpp::CreateShaders` and declared in `skygfx.h`,
but occur **nowhere else in `src/`** (grep `\<name\>` outside `pipelinecommon.cpp` /
`skygfx.h` -> **0 hits** for every one):

| handle | `makePS` line | IDR / `.cso` | source |
|---|---:|---|---|
| `PBR_Lighting` | `:799` | 207 `PBR_Lighting.cso` | `ps/PBR_Lighting.hlsl` (+ `skygfx.h:1206`) |
| `CarPaint_Reflections` | `:798` | 206 | `ps/CarPaint_Reflections.hlsl` |
| `ColorFilter_CrossMix` | `:766` | 141 | `ps/ColorFilter_CrossMix.hlsl` |
| `GTAIV_PS` | `:772` | 110 | `ps/GTAIV_ps20.hlsl` |
| `VehiclePaint_GTAIV` | `:779` | 200 | `ps/VehiclePaint_GTAIV.hlsl` |
| `SSAO_VertexDepth` | `:759` | 130 | `ps/SSAO_VertexDepth.hlsl` |
| `SMAA_EdgeCombined` | `:757` | 128 | `ps/SMAA_EdgeCombined.hlsl` |
| `SMAA_EdgeDepth` | `:755` | 126 | `ps/SMAA_EdgeDepth.hlsl` |
| `SMAA_EdgeNormal` | `:754` | 125 | `ps/SMAA_EdgeNormal.hlsl` |

Proof the SMAA edge family is only partially wired: `postfx_smaa.cpp:335`, `:509-514`,
`:583`, `:965`, `:1015`, `:1066`, `:1139`, `:1349` bind **only** `SMAA_Edge` and
`SMAA_EdgeMotionDepth` (plus blend-weight / neighbour / temporal).

**Six `IDR_*` RCDATA entries are embedded but referenced by no `src/` file** (out of 103
`RCDATA` rows): `IDR_NORMMAPBUILDINGPS`, `IDR_NORMMAPVEHICLEPS`, `IDR_VECTORMOTIONBLUR`,
`IDR_PEDPBRVS`, `IDR_CLOUDPS`, `IDR_CLOUDSSKYPS`.

**Three shader sources carry no `IDR` at all:** `ps/SkinEnhance.hlsl`, `ps/HairEnhance.hlsl`
(+ their `.cso` on disk). `ps/SkinSSS.hlsl` is embedded as `IDR_SKINPBR` but
`pipelinecommon.cpp:802` documents "deliberately NOT loaded here".

**21 `resources/cso/*.cso` files on disk are not referenced by `Resource.rc` at all**
(124 on disk, 103 embedded). Documented precedent at `Resource.rc:21-25`:
"`SSAO_ps20.cso` stays on disk but is no longer referenced (only docs)".

**Work (one commit per group):** delete the `.hlsl`, the `.cso`, the `Resource.rc` row, the
`resource.h` IDR, the `pipelinecommon.cpp` `makePS` line + `void *` global + log-line mention,
and the `skygfx.h` `extern`.

**Est. savings:** ~900-1,200 shader lines (`PBR_Lighting`, `SkinSSS`, `SkinEnhance`,
`HairEnhance`, the three unused SMAA edge files, `SSAO_VertexDepth`, `SSAO_ps20*`) + ~20
C++/`.rc`/`resource.h` rows + DLL size. **Risk: MEDIUM** — each handle must be proven
unselected by *every* config/mode branch (the by-name grep is that proof, and it is clean);
run `python tools/fast_build.py --check-shaders`, then a full build + launch.

**Independent cross-check (read-only):** `python tools/fast_build.py --verify-embed`
(added by the in-flight lane) reports exactly the same set:
`IDRs with no code ref: 6 (IDR_CLOUDPS, IDR_CLOUDSSKYPS, IDR_NORMMAPBUILDINGPS,
IDR_NORMMAPVEHICLEPS, IDR_PEDPBRVS, IDR_VECTORMOTIONBLUR)`, plus
`17 CSOs produced but not embedded` and `4 CSOs with no producer`
(`buildingPipePS.cso`, `neoVehiclePass1VS.cso`, `neoVehiclePass2VS.cso`,
`vehiclePipeVS.cso` — stale disk artifacts, purge with `--rebuild`).
Exit gate is OK, so `resource.h`/`Resource.rc`/CSO rows are internally consistent today.

**Excluded pending feature decisions:** `IDR_CLOUDPS` / `IDR_CLOUDSSKYPS` (inventory 3.11 says
**activate**, not delete) and `IDR_PEDPBRVS` (ped PBR VS may be staged).

**Verdict: DELETE the four groups above; LEAVE the clouds / pedPBR pair.**

---

## 6. INI — `gen_ini_ref --check`

```
$ python tools/gen_ini_ref.py --check
STALE: docs\ini-reference.md        (exit 1)
```

Regenerating to stdout and diffing with CRLF normalised shows the delta is **drift only**:

* source fingerprint: `main.cpp a6a17f2e0088` -> `dcc6a0c327ed`,
  `skygfx.h 93389acd28b3` -> `0117e99f7c46` (`modeManager.cpp c77536a9cb31` unchanged);
* every `main.cpp:<line>` anchor shifted (-33 ... -121) — `readIniFile` moved up 33 lines;
* `enableNormalMaps` source path corrected `normalmap.cpp:304` -> `texdefs.cpp:1038`;
* one trailing blank line added.
* **257 keys resolved -> 257; 1 unresolved -> 1; 442 Config fields -> 442;
  `ADD_IF_MISSING` cross-check 0/0 -> 0/0. No key added, no key removed, no new duplication.**

**Verdict: REGENERATE** (`python tools/gen_ini_ref.py`) — docs-only, not a source edit.
Re-run `--check` after any `readIniFile` / `Config` / `kValues*` change. The hard-coded
anchors flagged by the 10-27 audit (delta D5) are unchanged and still the main-split Stage-6
blocker: `gen_ini_ref.py:37 MAIN_CPP = src/core/main.cpp`, `:742` literal
`readIniFile(const char *modulePath, Config *c)`, `:287` the `ADD_IF_MISSING` regex,
`:269-275 EXTERNAL_INI_SOURCES`.

### 6.1 NEW dead INI key created by the `utilitynoise` deletion

`[General] NoiseQuality` is now a **pure round-trip with zero effect**:

| Stage | Location |
|---|---|
| read | `main.cpp:5935` -> `g_noiseQuality` (`main.cpp:724`) |
| save | `main.cpp:7455`; fresh-INI `:6854`; `ADD_IF_MISSING` `:6984` |
| menu accessors | `main.cpp:7510-7511` `menuGet/SetNoiseQuality` — write only the TU-static; "the old regen path is gone (SkyNoise owns the tile)" (`:7506-7508`, `:721-722`) |
| debug rows | `debugmenu_ui.cpp:356 RK_NOISEQ`, `:1310-1311`, `:1501`, `:1744`, `:1874`, `:2016`, `:2093-2099`, `:3169` |
| consumer | **none** — `SkyNoise::SetQuality` has **0 callers** (`noise.h:46`, `noise.cpp:812` only) |
| self-documented breakage | `main.cpp:734` — "L4: utility-noise regen removed ... the RK_NOISEQ 'regenerates on F11' tooltip in debugmenu_ui.cpp is now stale"; `debugmenu_ui.cpp:1311` tooltip still claims "the tile is regenerated (same path refreshIni takes on F11)" |

* **Option A — repoint (~6 lines):** `menuSetNoiseQuality` / the read site call
  `SkyNoise::SetQuality` (0 -> `NQ_LOW`, 1/2 -> `NQ_MEDIUM`, 3 -> `NQ_HIGH`). This is exactly
  the fallback roadmap Stage 4 specified and it was **never applied**. Sizes differ
  (32/64/128 vs 32/128/512), so the tooltip and `docs/ini-reference.md` must be updated too.
* **Option B — delete (~70 lines):** drop `g_noiseQuality` + read/save/EMIT sites + the
  `RK_NOISEQ` rows + the `menuGet/SetNoiseQuality` pair + one INI key.
* **Risk: LOW-MEDIUM** (user-visible: the menu row either starts working or disappears).
  Needs the same sign-off class as the landed `utilitynoise` deletion.
* **Verdict: MERGE-INTO-SERVICE (Option A preferred) — needs a decision.**

---

## 7. File-level merge / absorb candidates still open

All candidates from the two 10-27 audits that were **not** part of the "DONE" list were
re-verified; every one still exists on disk with its build row intact.

| # | Candidate | Files / lines (today) | Evidence | Est. | Risk (SEH / EOL / other) | Verdict |
|---|---|---|---|---|---|---|
| A | `rw/pipeplg.cpp` -> `rw/gta.cpp` | 17 ln **CRLF** -> 232 ln **mixed** (231 CRLF + 1 LF) | `GetPipelineID`/`SetPipelineID` declared `gta.h:387-388`, called `main.cpp:2343/3553`, `buildingPipe.cpp:3311/3540`, `texdefs.cpp:106/113`; `vcxproj:192` + filters row; premake is a glob | -1 TU, -17 ln, -1 `ClCompile` | 0 SEH; **do NOT normalise `gta.cpp`'s mixed EOL** — append a CRLF block | **MERGE** (audit Stage 7) |
| B | `core/presets.cpp` -> `core/modeManager.cpp` | 157 ln **CRLF** -> 1,357 ln **LF** | `ApplyPreset` live at `debugmenu_ui.cpp:2554,2576,2586-2592`; `presetConfigs` has **0 external readers**; `ApplyPreset` already calls `ModeManager_ClearLatch` | -1 TU, -1 `ClCompile` (`vcxproj:203`) | 0 SEH; **mixed EOL**; Style combo is user-facing; the `qualityPreset`/`ApplyPreset` owner decision (audit 2.1 / delta D4) is still open | **MERGE (audit Stage 9 pass 1) — GATED on that decision** |
| C | `extras/vehicles.cpp` + `extras/veh_shaders.cpp` | 378 + 539 = **917 ln, both CRLF** | `VehShaders_Init` -> `Vehicles_Init` (`veh_shaders.cpp:133`); three files hand-redeclare the same externs (`veh_shaders.cpp:19-26`, `wheels.cpp:41`, `vehiclePipe.cpp:11-16`); one init chain `vehiclePipe.cpp:203-216` | -1 TU, 2 rows (`vcxproj:216-217`) | 0 SEH, uniform EOL; **`tools/fix_build.py:263` requires `extras/veh_shaders.cpp` -> must be updated** | **MERGE (audit Stage 10)** — run **after** the 3.3-E `veh_shaders` prune so the merged TU is smaller |
| D | `entities/sftbdyphsx.cpp` + `extras/ragdoll_death.cpp` | 1,184 **CRLF** + 1,227 **LF** = 2,411 | mutual dep verified: `ragdoll_death.cpp:381/1033/1141` -> `Dismember_*`; `sftbdyphsx.cpp:17` -> `g_bulletWorld` / `g_ragdollDeathEnable` | -1 TU | 1 `__try` at `sftbdyphsx.cpp:1182` must stay verbatim; **mixed EOL**; creates a >2k TU (context-budget hostile); `fix_build.py:237` requires the name | **LEAVE** — the 10-27 audit's own recommendation |
| E | `rw/ColData.h` + `entities/PlantSurfPropMgr.h` -> `render/grasssys.h:18-19` | 17 + 32 = **49 ln, both CRLF**; host `grasssys.h` 656 ln CRLF | exactly one includer each (`grasssys.h:18/19`); **no `ClInclude` row in the vcxproj** -> zero build delta; only `CColTriangle` and `CPLANT_SURF_PROP_PLANTDATA_NUM` are consumed | -2 files, -49 ln | no SEH; uniform EOL; 2 include edits | **ABSORB** (audit Stage 8) |
| F | `extras/brdfLibrary.h` -> `render/brdfLibrary.h` | 627 ln **CRLF** | 3 includers: `veh_shaders.cpp:12`, `buildingPipe.cpp:3`, `vehiclePipe.cpp:3` (`skygfx.h:823` is only a comment) | 0 lines (pure move) | no SEH; CRLF -> CRLF; **`tools/fix_build.py:262` path must be updated** | **RELOCATE** (audit Stage 8) |
| G | `core/main_exports.h` -> `main.cpp` | **23 ln CRLF** (was 87; already purged to just `struct FX`) | 1 includer (`main.cpp:2`); `hooks.cpp` no longer exists; header documents its own 2026-10-27 purge | -1 file, -23 ln | no SEH; no `ClInclude` row | **ABSORB** (audit Stage 8) |
| H | delete `render/PC_GrassInstancer.h` | **3 ln LF** forwarding header | 1 includer `main.cpp:18`; created only because `main.cpp` sat in a *different lane* during the grass merge (`grasssys.h:12-14` says so). No concurrent lane now | -1 file, -1 `ClInclude` (`vcxproj:141`, filters:69), -1 premake entry (`:246`) | no SEH; no other includer | **DELETE** — finish the header merge (`main.cpp:18` -> `#include "grasssys.h"`) |

**Rejected again, unchanged reasons:** `weather.cpp` + `pipelinecommon.cpp` (write-owner vs
render-reader, layer inversion); `treeWind.cpp` + `grasssys.cpp` (distinct shader families);
`debug_handl.cpp` + `debugmenu_ui.cpp` (logging vs UI); anything into `postfx.cpp` (C1),
`envmap.cpp`/`mirrorPass.cpp` (the only two `__finally` TUs), `pipelinecommon.cpp` (C2),
`main.cpp`/`debugmenu_ui.cpp` (>2,500 lines); `postfx_smaa.cpp` into anything (drags
`AreaTex.h`, 14,980 ln).

---

## 8. The one question this rinse cannot answer: `grasssys` reachability

`src/render/grasssys.cpp` (1,115 ln) + `grasssys.h` (656 ln) = **1,771 lines**.

A scan of *every identifier defined in `grasssys.cpp` and referenced outside
`grasssys.cpp`/`grasssys.h`* returns exactly **two live edges**:

| Edge | Caller |
|---|---|
| `GrassResetShaderLatches` | `depthhook.cpp:407` (local extern), `:412` (call) |
| `CGrassInstancer::SetEnabled` | `main.cpp:751`, `:6648`, `:7646` (+ comment `debugmenu_ui.cpp:1204`) |

There is **no `InjectHook` / `EAXJMP` / `WRAPPER` anywhere in `grasssys.cpp`**, and
`CPlantMgr_Initialise` (`main.cpp:2643`, installed `:7921`) attaches only
`grassRenderCallback`, a `main.cpp` function.

Consequently these are **defined but never called**:
`CGrassRenderer::{SetCurrentScanCode :133, AddTriPlant :144, FlushTriPlantBuffer :162,
SetGlobalCameraPos :195, SetCloseFarAlphaDist :206, SetGlobalWindBending :218,
AddPlantCluster}` (0 refs at all), and `CGrassInstancer::{BeginFrame :863, EndBatch :894,
ShaderReady :1100, StatInstances :1106, StatDraws :1112, DrawTriPlant :1007}`.
A further **24 `grasssys.h` declarations have zero occurrences anywhere in `src/`**
(21 function declarations plus the three `#ifndef MASTER` extern bools):
`gbPlantMgrActive`, `gbShowCPlantMgrPolys`, `gbDisplayCPlantMgrInfo`, `_ColEntityCache_Add`,
`_ColEntityCache_FindInCache`, `_ColEntityCache_Remove`, `_ColEntityCache_Update`,
`_ProcessEntryCollisionDataSections`, `_ProcessEntryCollisionDataSections_AddLocTris`,
`_ProcessEntryCollisionDataSections_RemoveLocTris`, `_UpdateLocTris`, `DbgRenderLocTris`,
`DbgRenderCachedEntities`, `DbgCountLocTrisAndPlants`, `DbgCountCachedEntities`,
`ReloadConfig`, `PreUpdateOnceForNewCameraPos`, `UpdateAmbientColor`, `MoveLocTriToList`,
`MoveColEntToList`, `ReleaseEntry`, `SetPlantFriendlyFlagInAtomicMI`,
`CalculateWindBending`.

The file documents this state itself:

* `grasssys.cpp:662-670` — "TASK 2 gate: master switch, DEFAULT OFF ... every entry point
  below is a no-op";
* `grasssys.cpp:857` — "Staged main.cpp install path only — see DEFERRED DIFF (checkpoint)";
* `GrassInstPixelShader` is `#ifdef IDR_GRASSINSTANCED` and `resources/resource.h` has **no
  `IDR_GRASSINSTANCED`**, so the loader always returns NULL (`GrassInstanced.cso` is built
  but never embedded).

**Est. savings if deleted: 1,771 lines — the single largest deletion available in the tree**
(plus the `grassInstancing` INI key, its `modeManager` rows, its debug-menu row, and the
`vcxproj:144` / `premake:247` rows). Two symbols **must survive as stubs**:
`GrassResetShaderLatches` (`depthhook.cpp:412`) and `CGrassInstancer::SetEnabled`
(`main.cpp` x3 + `debugmenu_ui`).

**Risk: HIGH for feature intent, LOW for compile.** It removes a *documented staged feature*
plus two live config/menu rows — that needs explicit sign-off, exactly like the `utilitynoise`
sign-off the roadmap required.

**Verdict: LEAVE — force a decision.** Two consistent options only: (i) **DELETE** with
sign-off, or (ii) **WIRE** the deferred `CPlantMgr::Render` thunk (new feature work, out of
scope for a rinse). Do **not** half-delete, and do **not** merge `grasssys` anywhere.

---

## 9. Consolidated verdict table

| ID | Candidate | Type | Est. savings | Risk | Verdict |
|---|---|---|---:|---|---|
| R1 | `postfx_luminance` + `postfx_motionblur` + `postfx_utils` -> `postfx_support.cpp` | MERGE | -2 `ClCompile`, 24 -> 22 render `.cpp` | **LOW** — 0 `__finally`, 0 static collisions, LF -> LF, strict include union | **MERGE** |
| R2 | `pipeParkSet3L` replacing the 3 identical wrappers (`envmap:370`, `mirrorPass:124`, `postfx_utils:227`) | MERGE | ~15 ln | LOW-MED — C2702: edit the helper body only; both hosts carry `__finally` (14 / 8); mixed EOL | **MERGE** (low priority) |
| R3 | `pipeForceAlphaBlock` `buildingPipe.cpp:525` -> `pipelinecommon.cpp` | MERGE | -3 `main.cpp` local `extern`s | MED — FX alpha path; `skygfx.h` + `main.cpp` shared; 4 `__try` in `buildingPipe.cpp` | **MERGE** (later) |
| R4 | corona park blocks `main.cpp:2946-2975` / `:3488-3512` -> `pipeParkSet3L` | MERGE | ~50 ln | MED — black-quad-sensitive path; `main.cpp` shared | **MERGE-INTO-SERVICE** (later) |
| D1 | `extras/wheels.cpp` + `wheels.h` | DELETE | **213 ln** + 1 `ClCompile` | LOW | **DELETE** |
| D2 | `veh_shaders` dirt + colour-area blocks + 9 `skygfx.h` rows | DELETE | **~162 ln** | LOW (0 refs; unrelated to the `GetPaintPBR` clamp bug) | **DELETE** |
| D3 | `texdefs` light chain: `TexDefsExternalShutdown`, `DestroyLight`, `Setup/UnsetDirectionalLight`, `Setup/UnsetAmbientLight` + `texdefs.h:20` | DELETE | **~110 ln** | LOW (0 refs); follow-up scan for newly-orphaned `texdefs.cpp` helpers | **DELETE** |
| D4 | `texdefs.h:53 TexDefsSetDefinedShader` + body | DELETE | ~11 ln | LOW | **DELETE** |
| D5 | `debug_handl` `s_heartbeat`/`diag_heartbeat`/`diag_removeVEH` + `debug_handl.h:34-36,48,63,66` | DELETE | **~25 ln** | LOW — `diag_checkCrashMarker` is declared but never defined | **DELETE** |
| D6 | `skygfx.h` 5 zero-ref rows (`:1246,1276,1277,1278,1279`) | DELETE | 5 ln | **1** — zero refs | **DELETE** |
| D7 | `skygfx.h` 7 decl+def-no-caller pairs (`:1008,1012,1251,1254,1268,1545,1547`) | DELETE / static | ~40 ln | 1-2 — check the `m_bSpeedFX` family readers first | **DELETE / make static** |
| D8 | `gta.cpp` 5 dead WRAPPERs (`:149,150,156,158,164`) + their `skygfx.h` decls | DELETE | ~15 ln | 1 — do not touch `gta.cpp`'s mixed EOL | **DELETE** |
| D9 | `main.cpp:1626-1634 CShadows__RenderStoredShadows_hook` + `:1491 _orig` | DELETE | ~15 ln | 1 — no install site exists | **DELETE** |
| D10 | `ragdoll_death.cpp:155-158 ReadIniInt` + 2 dead bone tables (`:236`,`:252`) + 2 `ragdoll_death.h` decls | DELETE | ~40 ln | 1 — grep proves 0 readers (confirm tables) | **DELETE** |
| D11 | `chars.h` dead surface + `chars.cpp:9-20` table + `chars_shutdown` + `main.cpp:5` include | DELETE | **~60 ln** | 1-2 — keep `chars.h:53 SSS_Blur`, `chars_drawSSSBlur`, `chars_OnDeviceReset` | **DELETE** |
| D12 | `weather` `Weather_SelectSet`/`GetBlendFactor` -> `static` + `weather.h:8,22,25` | DELETE | ~13 ln | 1 | **DELETE / static** |
| D13 | `debugmenu_theme` `UI_ImGuiIsInitialized` + decl | DELETE | 6 ln | 1 | **DELETE** |
| D14 | `vehicles.cpp:377-378` DB getters; `GetVehicleEntry` -> static; `veh_shaders.cpp:21` dead `extern` | DELETE | ~5 ln | 1 | **DELETE** |
| D15 | `noise.h` 9 unused `SkyNoise` members -> static (**keep `SetQuality`**) | DELETE | ~10 decls + bodies | 1 | **DELETE / static** |
| D16 | `skygfx.h:277 presetConfigs` extern -> static | DELETE | 1 ln | 1 | **DELETE** (rides with B) |
| Q1 | `render/PC_GrassInstancer.h` (3 ln forwarder) | DELETE | -1 file, -1 `ClInclude` | 1 — 1 includer (`main.cpp:18`) | **DELETE** |
| S1 | 9 loaded-but-unbound shader handles + 4 orphan `IDR_*` + `SkinEnhance`/`HairEnhance` sources | DELETE | **~900-1,200 shader ln** + ~20 C++/`.rc` rows + DLL size | **MED** — needs `--check-shaders` + build + launch; **exclude `IDR_CLOUDPS`/`IDR_CLOUDSSKYPS` and `IDR_PEDPBRVS`** | **DELETE** |
| S2 | `DynamicSky` / `IBL_SkyCloud` `hash`+`noise3d`+`fbm` -> `include/CloudNoise.hlsl` | MERGE | ~40 shader ln | LOW — `--check-shaders` | **MERGE** |
| S3 | `Luma` x8 | MERGE | ~24 shader ln | LOW — **but the home (`CommonMath.hlsl`) was deleted; host decision required** | **MERGE-INTO-SERVICE (blocked)** |
| S4 | `GetViewPos` x3 | MERGE | ~24 shader ln | LOW | **LEAVE** — delete S1's `SSAO_VertexDepth` / `SSAO_ps20` first and it disappears |
| S5 | `D_GGX`/`V_Smith`/`F_Schlick` copies in `PBR_Lighting` / `SkinSSS` | MERGE | ~60 shader ln | MED (unguarded denominators) | **DELETE dead sources (S1) -> LEAVE `PBR_Common`** |
| M-A | `rw/pipeplg.cpp` -> `rw/gta.cpp` | MERGE | -1 TU, -17 ln | LOW; **do not normalise `gta.cpp` mixed EOL** | **MERGE** |
| M-B | `core/presets.cpp` -> `core/modeManager.cpp` | MERGE | -1 TU | MED; mixed EOL; **gated on the 2.1 owner decision** | **MERGE (gated)** |
| M-C | `vehicles.cpp` + `veh_shaders.cpp` | MERGE | -1 TU | MED; `fix_build.py:263` update required | **MERGE** (after D2) |
| M-D | `sftbdyphsx.cpp` + `ragdoll_death.cpp` | MERGE | -1 TU | **HIGH** — mixed EOL, 1 `__try`, >2k TU | **LEAVE** |
| M-E | `ColData.h` + `PlantSurfPropMgr.h` -> `grasssys.h` | MERGE | -2 files, -49 ln | LOW; no build rows | **ABSORB** |
| M-F | `extras/brdfLibrary.h` -> `render/` | MERGE | 0 (move) | LOW; `fix_build.py:262` update | **RELOCATE** |
| M-G | `core/main_exports.h` -> `main.cpp` | MERGE | -23 ln | LOW; no build row | **ABSORB** |
| I1 | `[General] NoiseQuality` inert key (6.1) | MERGE / DELETE | ~6 (repoint) or ~70 (delete) | 1-2 — needs sign-off; same class as the landed `utilitynoise` delete | **MERGE-INTO-SERVICE (decision)** |
| I2 | `gen_ini_ref.py --check` -> STALE | DOCS | 0 | 0 — drift only; key set identical | **REGENERATE** |
| Z1 | `grasssys.{cpp,h}` 1,771 ln reachability gap (section 8) | ? | 0 **or 1,771** | compile 1 / feature intent **HIGH** | **LEAVE — force decision (delete-with-sign-off vs wire)** |
| Z2 | `envmap.cpp:191-243` `#ifdef DEBUGENVTEX` | LEAVE | 53 ln | 1 | **LEAVE** |
| Z3 | `UploadUnifiedConstants` | LEAVE | — | — | **LEAVE** (roadmap 3.3) |
| Z4 | `texdefs.h:31 TexDefsD3DWhy` | LEAVE | — | — | **LEAVE** (enumerators live: `texalpha.cpp:590-609`) |
| Z5 | `neopipe.cpp:400 carBlendRestore3L` | LEAVE | — | — | **LEAVE** (reclassified: save/restore, not park-and-set) |

**Totals if every DELETE/MERGE lands (excluding Z1):** ~**1,200-1,400 lines of C++/headers**
+ **900-1,200 lines of shaders** + 8 files removed + 6 `ClCompile` rows removed.
**Including Z1 (delete option):** a further 1,771 lines.

---

## 10. Suggested ordering (lanes are disjoint unless noted)

| Stage | Content | Write set | Gate |
|---|---|---|---|
| 0 | Regenerate the INI reference; purge stale objects (`utilitynoise.obj` + its `.tlog`, the 10 orphan `.obj` named in the 10-27 audit) | `docs/ini-reference.md`, `build/obj/` | `gen_ini_ref.py --check` clean; duplicate-`ClCompile`-basename scan empty |
| 1 | **R1** `postfx_support.cpp` fortify | new `src/render/postfx_support.cpp`; delete 3 TUs; `vcxproj` + `.filters` + `premake5.lua` | `cl /Zs`; `grep -c "__finally"` = 0; `file` -> LF; symbol set = 13 functions + 5 globals; `postfx.h` untouched |
| 2 | **D1 + D6 + D7 + D8 + D9** (zero-risk deletions) | `wheels.*`, `skygfx.h`, `gta.cpp`, `main.cpp`, `vehiclePipe.cpp`, `vcxproj` | `grep -rn "Wheels_\|D3D9RenderNotLit\|DNInstance_default\|UpdateVehicleRing" src` -> 0; build + launch |
| 3 | **D2 + D3 + D4 + D5 + D10-D16** (dead API in live TUs) | `veh_shaders.cpp`, `texdefs.*`, `debug_handl.*`, `weather.*`, `debugmenu_theme.*`, `vehicles.cpp`, `noise.*`, `chars.*`, `ragdoll_death.*`, `skygfx.h`, `presets.cpp` | per-symbol grep -> definition only; `cl /Zs`; launch: `chars_drawSSSBlur` line unchanged; log tag set unchanged except removed ones |
| 4 | **M-A + M-E + M-F + M-G + Q1** (header / file absorbs, no behaviour) | `pipeplg.cpp`, `ColData.h`, `PlantSurfPropMgr.h`, `brdfLibrary.h`, `main_exports.h`, `PC_GrassInstancer.h`, `grasssys.h`, `main.cpp`, `fix_build.py`, `vcxproj`, `premake5.lua` | basename scan; `grep -rln "ColData.h\|brdfLibrary.h\|main_exports.h\|PC_GrassInstancer.h" src` -> new paths only |
| 5 | **S1 + S2** (shader dead-weight) | `shaders/**`, `resources/resource.h`, `resources/Resource.rc`, `pipelinecommon.cpp`, `skygfx.h` | `fast_build.py --check-shaders`; full build; launch `[INIT] shaders` summary; **exclude clouds + pedPBR** |
| 6 | **R2 + R4 + R3** (render-state doctrine; writes `skygfx.h` + `main.cpp` => serialise) | `pipelinecommon.cpp`, `skygfx.h`, `envmap.cpp`, `mirrorPass.cpp`, `postfx_utils.cpp`, `main.cpp`, `buildingPipe.cpp` | `__finally` counts unchanged (envmap 14, mirrorPass 8); `git diff --word-diff` shows only the wrapper bodies changed |
| 7 | **M-B + M-C** (gated merges) | `presets.cpp`, `modeManager.cpp`, `vehicles.cpp`, `veh_shaders.cpp`, `vcxproj`, `fix_build.py` | M-B needs the 2.1 owner decision first; M-C: `grep "VehShaders_Init\|Vehicles_Init" src` -> one chain |
| 8 | **S3 + I1** (decisions) | new `shaders/include/CloudNoise.hlsl`, `main.cpp`, `debugmenu_ui.cpp`, generated INI docs | `--check-shaders`; `gen_ini_ref.py --check`; menu round-trip |
| 9 | **Z1** — `grasssys`: delete-with-sign-off **or** wire | — | needs explicit sign-off either way; **not part of a rinse** |

**Parallelism:** stages 1 / 2 / 3 are disjoint and may run concurrently. Stage 6 writes
`skygfx.h` and `main.cpp` and must be serialised against anything else touching those two.
Stages 4 and 5 both write build metadata (`fix_build.py`, `vcxproj`) => serialise.

**Universal per-stage gates (unchanged from the roadmap's section 6):**

* **U1** duplicate-`ClCompile`-basename scan must print nothing;
* **U2** stale `.obj` purge before every link;
* **U3** `cl /Zs` on every touched TU;
* **U4** line endings per file — never normalise (see the per-file EOL column in section 7;
  `gta.cpp` stays mixed, `envmap.cpp` CRLF, `mirrorPass.cpp` LF, the three postfx-support
  sources LF);
* **U5** `ClInclude` rows follow any header delete / rename (`PC_GrassInstancer.h:141`,
  filters:69, premake:246; `brdfLibrary.h` has no row but `fix_build.py:262` needs the path);
* **U6** `premake5.lua` mirror matches `build/skygfx.vcxproj`;
* **U7** `git status --short` shows only the stage's declared files — and **never
  `git checkout` (C0)**.
