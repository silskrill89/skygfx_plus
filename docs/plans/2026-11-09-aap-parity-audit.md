# aap ↔ skygfx_plus_expIV systematic parity audit

**Date:** 2026-11-09 · **Mode:** READ-ONLY (no source edits, no builds, no commits)
**Question answered:** *where can breakage systematically differ from the original aap skygfx, and which of those differences can actually be seen?*

---

## 0. Scope, references, method

| role | tree | ref |
|---|---|---|
| **REFERENCE (aap)** | `E:\SDKs\skygfx_original` | aap/skygfx @ `926616a` (v4.2b). `src/main.cpp` = **1733 ln monolith** |
| secondary | `E:\SDKs\skygfx_junior` (fb69df1), `backup_original\src\` | lineage checks only |
| **OURS** | `E:\dev(dave)\skygfx_plus_expIV` (live tree) | `5cc5d25` |

Ours is a *split* tree: the 2026-11-09 extraction campaign moved chunks out of a once-monolithic
`main.cpp` **byte-identically** (shadows 804 / rw_fixes 2026 / config 1951 / render_callbacks 1415),
so `src/core/{main,shadows,rw_fixes,config,render_callbacks}.cpp` are treated as one unit and
compared **functionally**, not structurally.

**Method.** (1) enumerate every `InjectHook` / `InterceptCall` / `Nop` / `Patch` on both sides and
diff the address sets; (2) diff the render-state *doctrine* (what each side forces vs. what it
leaves to the game); (3) diff pipeline dispatch (pipe enums, `Get/SetPipelineID`, Switch bodies);
(4) diff postfx entry points, chain order and transforms; (5) diff all 65 aap INI keys against ours
(section-aware `readint2` fallback proved key-preserving); (6) diff `DllMain` / init order.
Every divergence row below carries **both sides' file:line**.

**Systematic class legend**

| class | meaning |
|---|---|
| `ALL-MODES` | shared infrastructure — runs for PS2 / Xbox / PC / Modern / every mode |
| `MODERN-ONLY` | only exercised when `pipeline=PBR` (⚠ **our shipped default INI is `pipeline = PBR`**, `skygfx.default.ini:41`) |
| `PS2-MODE` / `XBOX-MODE` | only when that mode's pipe/filter combo is selected |
| `PEDS-ONLY` | only the skin-pipe group |
| `CONDITIONAL` | needs a specific INI key, foreign ASI, or MoonLoader/SAMP presence |

**Breakage candidate** = *could this produce a visible frame difference vs. aap with the same INI?*
`=N` means structurally different but provably output-equivalent.

---

## 1. Executive summary

1. **The single biggest systematic delta is state-forcing, not code path.** aap's render callbacks
   *read and restore*; ours **force canonical signatures on all three layers (rw cache / RW driver
   cache / raw device) with no save/restore at three separate frame boundaries** plus entry
   contracts on all three pipe groups. This is `ALL-MODES` and it is the only class of change that
   can affect Xbox mode — which is why "xbox = best but broken" fits: the *pipes* are aap-identical,
   the *state around them* is not.
2. **"Modern = very broken" has no aap counterpart at all.** `pipeline` defaults to `PIPELINE_PBR`
   on an absent key (`config.cpp:216`), which force-locks `BUILDING_PBR + CAR_MODERN +
   COLORFILTER_MODERN` (`config.cpp:237-261`, `config.cpp:543-547`). aap has no `pipeline` key, no
   PBR building pipe, no `CAR_MODERN`, no Modern colour filter, no forward+, no SSAO/SMAA/motion
   blur. Modern-mode breakage must be hunted in our-only code; this audit marks those rows.
3. **aap-parity hooks are ~96% intact.** All 30+ DllMain hook addresses and all 11 postfx hook
   addresses match byte-for-byte (§4). Only **four** aap hook sites are *not* installed by us, all
   deliberate (IsAlreadyRunning redirect, `0x748AA8` SilentPatch conflict, corona byte-patch,
   `0x5D7F40` ownership move) — listed in full at §2.1.0.
4. **Two genuine aap-parity omissions** stand out as visual candidates: the scene-start
   `RwCameraEndUpdate/BeginUpdate` far/fog refresh (commented out, `main.cpp:1664-1670`) and the
   `iCanHasbuildingPipe/iCanHasvehiclePipe` retirement (ours always installs the custom pipes).
5. **Everything else in the suspect list is ruled out in §4** — including the hook address set, the
   `envmaphooks` site list, `Get/SetPipelineID`, `fixSAMP`, `D3D9Render`, the dual-pass leg
   sequence, the `readInis` radiosity capture, and the texdb parser core.

---

## 2. Divergence table

### 2.1 Hook installs + call ordering

**2.1.0 Hook-set delta (the brief's "flag any address we hook that aap didn't / any aap hook we don't")**

*Addresses aap hooks that **we do not** — the complete list (4):*

| address | aap site | our status |
|---|---|---|
| `0x74872D` | `InjectDelayedPatches` installed as a call hook (guarded by `IsAlreadyRunning`) | **not installed** — called inline from `DllMain` (H1) |
| `0x748AA8`–`0x748AE7` | `Nop` under `privateHooks` (kills the nvidia splash text) | **deliberately dropped** — SilentPatch owns it (H13) |
| `0x6FB17C` | `coronaZtest` byte patch (`Nop`/`FF 51 20`) | **retired** — SilentPatch owns it; runtime force only (S13) |
| `0x5D7F40` | `IsCBPCPipelineAttached` **only when `explicitBuildingPipe >= 0`** | owned by `texdefs.cpp`, installed **only when the rwnormal plugin attaches**, and ignores `explicitBuildingPipe` (H4) |

> No other aap hook address is missing from our tree — every other `InjectHook`/`InterceptCall`
> in `aap src/main.cpp:1540-1688` and `src/postfx.cpp:1383-1385` has a counterpart at the same
> address (verified in §4).

*Addresses **we** hook that aap never does (grouped; all ours-only):*

| group | addresses | evidence |
|---|---|---|
| shadow call-site / entry shields | `0x53E0BE`, `0x708300`, `0x70A960`, `0x706AB0` | `src/core/shadows.cpp:549-800` |
| RW fix machine (6) | `0x6FAEC0`, `0x70CF62`, `0x701FE9`, `0x7273D0`, `0x53E21D`, `0x53DE6B`, `0x700B6B` | `src/core/rw_fixes.cpp:1852-2077` |
| effects-phase intercepts | `0x53EAD3` (RenderEffects), `0x53DCAC`/`0x53DFD8` (Coronas reflections), `0x53E12B` (sun reflection), `0x53E227` (CPostEffects::Render tail) | `src/core/main.cpp:4251-4260` |
| cloud hygiene | `0x53DCA2`, `0x53DFA0` (`CClouds::Render`) | `src/core/main.cpp:3977-3978` |
| water / wake flushes | `0x6E8790`, `0x6E8EF1`, `0x6E91E4`, `0x6E9963`, `0x6EF8AC`, `0x6F0099`, `0x6EA2E8`, `0x6EDD7C` | `src/core/main.cpp:4042-4061` |
| FX-system render | `0x4A930A` (`fxSystem_c::Render`), `0x726AD0` (motion-blur streaks) | `src/render/neopipe.cpp:1739`, `:1759` |
| mirror composite | `0x53EABA` (`CMirrors::RenderMirrorBuffer`) | `src/render/mirrorPass.cpp:719` |
| tree wind | `0x534E90` | `src/core/main.cpp:4067-4070` |
| device vtable patches | `IDirect3DDevice9` vtable[16] `Reset`, vtable[39] `SetDepthStencilSurface` | `src/render/depthhook.cpp:3-4`, `:493-511` |
| shader/frustum guards | `0x7FAD30`, `0x7FAD90`, `0x7618B0` | `src/core/main.cpp:4209-4214` |
| texdefs / normal-map lane | `0x5D7F40`, `0x5DA610`, `0x5D5B80` | `src/texdefs.cpp:1648`, `:1663`, `:1677` |
| sky-dome site | `0x53D81C` — **verified but deliberately NOT hooked** (receipt only) | `src/core/main.cpp:3989-3999` |

*Note on `0x53E0BE`:* aap never hooks it (only a **commented-out** `Nop(0x53E0C3/0x53E0C8)` in its
cloud-test block, `aap src/main.cpp:1728-1729`), so the shadow call-site intercept is entirely ours.

| # | area | aap behaviour | ours | systematic? | breakage | evidence (aap → ours) |
|---|---|---|---|---|---|---|
| H1 | Delayed-patch install point | `InjectDelayedPatches` installed as a **call hook at `0x74872D`** (`IsAlreadyRunning`), guarded `if(IsAlreadyRunning()) return TRUE;` | called **directly from `DllMain`** at attach; `0x74872D` never touched (deliberate — SilentPatch clash) | ALL-MODES (ordering) | **Y** | aap `src/main.cpp:1555-1556`, `:1368-1371` → ours `src/core/main.cpp:4192-4196` |
| H2 | Exe-version gate | hard-fails: `if(*(DWORD*)0x82457C != 0x94BF && *(DWORD*)0x8245BC == 0x94BF) return FALSE;` → **refuses to load** | logs `version check WARNING … continuing anyway` and proceeds | CONDITIONAL (wrong exe) | **Y** (on non-1.0-US every hardcoded address is wrong) | aap `src/main.cpp:1524-1526` → ours `src/core/main.cpp:4152-4165` |
| H3 | Custom building/vehicle pipe install | `if(iCanHasbuildingPipe) hookBuildingPipe(); if(iCanHasvehiclePipe) hookVehiclePipe();` — both flags go **false when the INI key is absent/invalid** | guards **dropped** (`iCanHas*` constant true, zero writers); hooks always installed | CONDITIONAL (legacy INI without `buildingPipe=`/`vehiclePipe=`) | **Y** | aap `src/main.cpp:1401-1406` + `:933-936` + `:956-959` → ours `src/core/main.cpp:3930-3935` |
| H4 | `0x5D7F40` `IsCBPCPipelineAttached` | installed by `hookBuildingPipe` **only when `explicitBuildingPipe >= 0`**, body honours `explicitBuildingPipe > 0 → FALSE` | **not installed by `hookBuildingPipe` at all**; owned by `texdefs.cpp` and installed **only if the external rwnormal plugin attaches**, and the hook body **ignores `explicitBuildingPipe`** (extern declared, never read) | CONDITIONAL (`explicitBuildingPipe` + rwnormal plugin) | **Y** | aap `src/buildingPipe.cpp:814-815` + `:785-803` → ours `src/render/buildingPipe.cpp:4187-4190` + `src/texdefs.cpp:1634-1655`, `:1396-1419` |
| H5 | `0x5D5B80` `IsCCPCPipelineAttached` | compiled **`#if 0`** — never installed (car pipe disabled path) | installed by `texdefs.cpp` when the rwnormal plugin attaches | CONDITIONAL (rwnormal plugin) | **Y** | aap `src/vehiclePipe.cpp:1465-1472` → ours `src/texdefs.cpp:1671-1684`, `:1423-1439` |
| H6 | `0x5DA610` `CustomPipeAtomicSetup` | never hooked; `0x4C88F0` jumps *into the game's* `0x5DA610` ("give vehicle pipe to upgrade parts") | `0x4C88F0 → 0x5DA610` kept, but `0x5DA610` is **also** replaced by an NM-aware full-body hook (no chain to original; early-returns on NM pipelines / default MatFX) | CONDITIONAL (rwnormal plugin) | **Y** (upgrade parts with MatFX may lose the car pipe) | aap `src/main.cpp:1573` → ours `src/core/main.cpp:4234` + `src/texdefs.cpp:1656-1670`, `:1371-1390` |
| H7 | Byte-verified hook gates | `0x756DFE`, `0x5DADB7`, `0x5DAE61` installed **unconditionally** | each wrapped in `verifyBytes()`; a prologue mismatch **silently skips the hook** and keeps vanilla | CONDITIONAL (foreign ASI rewrote the site) | **Y** (grass PS2-modulate shader / grass seed / grass intensity silently die) | aap `src/main.cpp:1563-1566` → ours `src/core/main.cpp:4202-4216`, `:4224-4226` |
| H8 | Shadow block | **no shadow call-site hooks at all** — only `fixShadows` z-offset patches + `FX::GetFxQuality_*` | `shadows_installHooks()` intercepts `0x53E0BE → CShadows::UpdateStaticShadows`, plus function-entry shields for `0x708300` / `0x70A960` / `0x706AB0` | ALL-MODES | **Y** (shadow passes re-routed through our contract wrapper) | aap `src/main.cpp:1425-1433`, `:1470-1492` (no 0x53E0BE) → ours `src/core/shadows.cpp:549-800`, site table `:584-586` |
| H9 | RW fix machine | point-light colour fix at `0x700B6B` installed unconditionally; **no** corona/sprite-flush/font/pointlight shields | `rwFixes_installHooks()` — 6 machines (corona entry `0x6FAEC0`, sprite-flush, `0x701FE9`, `0x7273D0`, CPointLights `0x53E21D`/`0x53DE6B`, `0x700B6B`), gated on `rwFixesEnable` (default 1) | ALL-MODES | **Y** (every corona/sprite flush now carries a forced blend/depth signature) | aap `src/main.cpp:1606` → ours `src/core/rw_fixes.cpp:1852-2077` |
| H10 | Effects / cloud / corona phase | **none** — aap never wraps `RenderEffects`, `CClouds::Render`, `CCoronas::RenderReflections/SunReflection`, `CPostEffects::Render`, water/wake flushes | 4 `InterceptCall`s + 2 cloud hygiene + 4 water/wake flush + splash-foam + tree-wind + mirror-pass | ALL-MODES | **Y** | aap `src/main.cpp:1579-1606` (only skidmarks/tags/postfx) → ours `src/core/main.cpp:3977-3978`, `:4042-4061`, `:4251-4260` |
| H11 | Scene hook install timing | installed inside `InitialiseGame_hook` | installed by `installDeferredHooks()` **from `DllMain`**, and again (idempotent) from `InitialiseGame_hook`; `InitialiseGame` hook **skipped entirely under MoonLoader** | ALL-MODES (ordering) / CONDITIONAL (MoonLoader) | N (same sites, earlier) — but see H12 | aap `src/main.cpp:773-778` → ours `src/core/main.cpp:3103-3129`, `:3909-3920` |
| H12 | texdb load under MoonLoader | `initTexDB()` runs unconditionally from `InitialiseGame_hook` (aap has no MoonLoader branch) | `TexDefsLoadDatabase()` sits **inside** `InitialiseGame_hook`'s `initCount==1` block, which is **never reached under MoonLoader** → `faketexinfo` stays zeroed → `faketexinfo.dualPass == 0` → **`D3D9RenderDual` disables the building dual pass for every texture** | CONDITIONAL (MoonLoader) | **Y** | aap `src/main.cpp:770-784` + `src/texdb.cpp:128-148` → ours `src/core/main.cpp:3138-3143` + `src/texdefs.cpp:1092-1093`, `:1108-1118` |
| H13 | `privateHooks` nvidia NOP | `Nop(0x748AA8, 0x748AE7-0x748AA8)` (kills "the way it's meant to be played") | **removed** — "SilentPatch installs its 5-byte jump there" | CONDITIONAL (`privateHooks=1`) | N (we only *lose* a cosmetic splash removal) | aap `src/main.cpp:1499-1500` → ours `src/core/main.cpp:4032-4036` |
| H14 | Debug menu | `installMenu()` registers ~50 DebugMenu entries from `InjectDelayedPatches` | no `DebugMenuLoad`/`installMenu` anywhere — replaced by the in-house ImGui menu | none (UI only) | N | aap `src/main.cpp:1514`, `:1252-1364` → ours: *no match* (`grep DebugMenuLoad src` → 0 hits) |

### 2.2 Render-state doctrine  ← the priority-2 area

| # | area | aap behaviour | ours | systematic? | breakage | evidence (aap → ours) |
|---|---|---|---|---|---|---|
| S1 | `myDefaultCallback` overall | **read-only**: resolve default pipe, read `zwrite`, optionally run two legs, restore func/ref. *Zero* forced states. | four contracts stacked: default-pipe entry contract → shader hygiene → pAmbient override → ped entry/exit contract → sticky ZWRITE/depth repair → `[PedRS]` monitor | ALL-MODES (default pipe) / PEDS-ONLY (skin pipe) | **Y** | aap `src/main.cpp:305-335` → ours `src/core/render_callbacks.cpp:520-1414` |
| S2 | **Default-pipe entry contract** (the ~2075-2111 region named in the brief) | *does not exist* — aap's `pipe==NULL` branch (`main.cpp:313-317`) only reads `zwrite` and sets `dodual` | forces through **L1+L2+L3**: `VERTEXALPHA=0`, `SRC=SRCALPHA`, `DST=INVSRCALPHA`, `BLENDOP=ADD`, `ZWRITE=1`, `ZENABLE=1`, `ZFUNC=LESSEQUAL`, `ALPHATESTENABLE=1`, `ALPHAFUNC=GE`, `ALPHAREF=1` — **no save/restore, deliberately left behind** | ALL-MODES (every prop / pickup / marker atomic) | **Y** | aap `src/main.cpp:313-317` → ours `src/core/render_callbacks.cpp:587-655` |
| S3 | Ped depth + opaque contract | *does not exist* — aap's skin branch only flips `dodual` (`main.cpp:318-319`) | unconditional `pedOpaqueEnforce(GE@1, blend-off, zwrite/ztest on, ZFUNC=LESSEQUAL)` 3-layer; cull forced via `pipeForceCullMode`; ZWRITE/ZTEST/ZFUNC entry force + **sticky** exit repair (a divergent capture is never re-emitted) | PEDS-ONLY | **Y** | aap `src/main.cpp:318-331` → ours `src/core/render_callbacks.cpp:731-941`, `:846-855`, `:1231-1319` |
| S4 | Default-cb shader hygiene | *does not exist* — aap leaves whatever VS/PS the previous pass bound | `RwD3D9SetPixelShader(NULL)` + `SetVertexShader(NULL)` through cache **and** raw device, before every default-pipe draw (gated `defaultCbShaderHygiene`, default 1; plus an always-on safety net) | ALL-MODES | **Y** (usually *repairs*; can also unbind a shader a later FFP consumer expected) | aap `src/main.cpp:305-335` (no hygiene) → ours `src/core/render_callbacks.cpp:943-1005` |
| S5 | Frame-boundary world baseline | *does not exist* | `pipeApplyWorldBaseline()` at **4 frame boundaries** forces `SRC/DST=SRCALPHA/INVSRCALPHA`, `VERTEXALPHA=0`, `ZWRITE=1`, `ZTEST=1`, `ZFUNC=LESSEQUAL`, **`CULL=BACK`**, `ALPHATESTENABLE=1`, `COLORWRITEENABLE=0xF`, `BLENDOP=ADD`, `SEPARATEALPHA=0` on all 3 layers. `stateBaselineEnable` is **floored to 1 at parse — structurally always on** | ALL-MODES | **Y** (top candidate) | aap: *absent* → ours `src/render/pipelinecommon.cpp:2592-2663`, gate `src/core/config.cpp:694-709`, call sites `src/core/main.cpp:1580-1597`, `:1605`, `:3760` |
| S6 | Scene-start repair | *does not exist* | `pipeRepairSceneStartSync("scene_start")` runs **every frame, unconditionally**, repairing device-vs-cache divergence on blend/Z family back to the rw-cache baseline | ALL-MODES | **Y** | aap *absent* → ours `src/core/main.cpp:1620`, impl `src/render/pipelinecommon.cpp:1647-1760` |
| S7 | Frame-end canonical enforce | *does not exist*; aap's `DrawFinalEffects` early-returns when `!m_bYCbCrFilter` and touches nothing | `DrawFinalEffects_pf` wrapper calls `StateBlockEnforceCanonical()` **unconditionally** (not gated on any lever) after the last 2D write | ALL-MODES | **Y** | aap `src/postfx.cpp:1311-1312` → ours `src/core/main.cpp:3754-3780` + `src/render/pipelinecommon.cpp:1571-1606` |
| S8 | Scene-start camera far/fog refresh | `RwCameraEndUpdate(Scene.camera); RwCameraBeginUpdate(Scene.camera);` — "far and fog plane are set AFTER calling BeginUpdate in Idle()" | **commented out**: "The far/fog refresh dance is skipped until a safe begin-state query exists" | ALL-MODES | **Y** (frustum + fog distance computed from the previous frame's values) | aap `src/main.cpp:736-742` → ours `src/core/main.cpp:1664-1670` |
| S9 | Dual-pass leg state | `RwRenderStateSet` only (rw cache): leg1 `GE@threshold`+`ZWRITE=TRUE`, leg2 `LESS@threshold`+`ZWRITE=FALSE`, restore | same sequence **plus** `RwD3D9SetRenderState` + raw `d3d9device->SetRenderState` (`setAlphaTestTwoLayer` / `setZWriteTwoLayer`) — 3-layer | ALL-MODES | N (repair of a real cache/device split; output-equal when the cache is healthy) | aap `src/main.cpp:131-142` → ours `src/core/main.cpp:230-318` |
| S10 | Dual-pass **gate** | `myDefaultCallback`: `dodual` iff `zwrite && dualPassDefault` (default pipe) or `pipe==skinPipe && dualPassPed`. **No alpha test.** | non-ped branch adds `atomicHasAlpha(atomic,false)` — **skips the second leg when material+prelit alpha are all 255**; ped branch runs **unconditionally** when `dualPassPed && zwriteThresholdPed>0` | ALL-MODES (`dualPass*=1` in Xbox/PC/Mobile/Modern modes) | **Y** (cutout textures on alpha-255 materials lose their sub-threshold fringe) | aap `src/main.cpp:312-333` → ours `src/core/render_callbacks.cpp:656-669`, `:1074-1084` |
| S11 | Per-texture dual overrides | `D3D9RenderDual(dual, hdr, inst)` — **3 args**, `TexInfo` used only for detail maps | `D3D9RenderDual(..., TexInfo *texInfo = nullptr)` — `texInfo->dualPass` can veto, `texInfo->zwriteThreshold` can retarget (`roadsign` = 100) | ALL-MODES (any texture with a TexInfo) | **Y** | aap `src/main.cpp:122-149`, `src/buildingPipe.cpp:237` → ours `src/core/main.cpp:272-318`, `src/render/buildingPipe.cpp:1859/2535/3594`, `src/texdefs.cpp:1099-1101` |
| S12 | `pAmbient` override on skin pipe | *does not exist* — aap renders skin with the game's own `pAmbient` | `pAmbient->color = GetTimecycleAmbient()` for every skin atomic, with a black-guard fallback, restored on exit (absorbed by `SkinAmbientOverrideEnter/Exit` when PedsPipe owns the span) | PEDS-ONLY | **Y** (ped lighting decoupled from the game's frame ambient) | aap `src/main.cpp:305-335` (no ambient write) → ours `src/core/render_callbacks.cpp:1007-1058`, `:1216-1223` |
| S13 | `coronaZtest` | `Nop(0x6FB17C,3)` (PS2) / `Patch(0x6FB17C, FF 51 20)` (PC) — **byte patch** | **no bytes written** (SilentPatch owns `0x6FB17C`); key re-purposed as a runtime z-test force inside our corona shield | ALL-MODES when `coronaZtest != -1` | N–Y (equivalent only inside our shielded path; aap's patch also affected *unshielded* corona draws) | aap `src/main.cpp:226-240` → ours `src/core/main.cpp:386-389` |
| S14 | `DrawFinalEffects` state restore | restores `TEXTUREFILTER=LINEAR`, `ZTEST=1`, `ZWRITE=1`, `TEXTURERASTER=NULL`, `VERTEXALPHA=TRUE`; **leaks `FOGENABLE=FALSE`** | same restores **plus `FOGENABLE=TRUE`**, and runs the whole SMAA/FXAA/ImGui/YCbCr stack first | ALL-MODES (only when `YCbCrCorrection=1`) | N (ours is strictly more restored) | aap `src/postfx.cpp:1365-1369` → ours `src/render/postfx.cpp:2806-2811` |

### 2.3 Pipeline dispatch

| # | area | aap behaviour | ours | systematic? | breakage | evidence |
|---|---|---|---|---|---|---|
| P1 | `GetPipelineID` / `SetPipelineID` | 10-line accessor over `gtaPipeOffset 0x8D6080` | **byte-identical** | — | **N** | aap `src/pipeplg.cpp:7-17` → ours `src/rw/pipeplg.cpp:27-37` |
| P2 | PDS `0x131` stream-rights CB | `pdsAtmCB` + `PDSPipePluginAttach` live in `pipeplg.cpp`, registered from `myPluginAttach` | same `pdsAtmCB` logic **migrated** into `texdefs.cpp`, registered via `TexDefsPluginAttach` (idempotent, always-1) | — | **N** (same 6 `PDS_PS2_CustomBuilding*` IDs, same pipeline assignment) | aap `src/pipeplg.cpp:46-74` → ours `src/texdefs.cpp:98-115`, `:783-797` |
| P3 | `myPluginAttach` chain | `return PipelinePluginAttach() && PDSPipePluginAttach() && TexDBPluginAttach();` — **short-circuits** | `int ok = PipelinePluginAttach(); TexDefsPluginAttach(); PedsPipe_PluginAttach(); return ok;` — game chain first, ours always run | ALL-MODES | **N** (ours is strictly safer; already known-better) | aap `src/main.cpp:761-766` → ours `src/core/main.cpp:3066-3090` |
| P4 | building pipe enum | `{PS2, Xbox}` only; **`"PC"` is an alias for `BUILDING_XBOX`** | `{PS2, Xbox, GTAIV, PBR}` — **no `"PC"` alias** | CONDITIONAL (INI says `buildingPipe=PC`) | **Y** (aap → Xbox pipe; ours → key not found → falls through to the `pipeline` lock) | aap `src/main.cpp:926-931` → ours `src/core/config.cpp:307-316` |
| P5 | vehicle pipe enum | aliases `Spec`, `LCS`, `Leeds`, `Neo`, `Env`, `Mobile`, `VCS` | aliases `Specular` (no `Spec`), `Leeds` (no `LCS`), `Modern`, `GTAIV` | CONDITIONAL (INI says `vehiclePipe=LCS` / `=Spec`) | **Y** | aap `src/main.cpp:942-954` → ours `src/core/config.cpp:319-333` |
| P6 | `CAR_NEO` dispatch | `if(iCanHasNeoCar && !UG_mod) CarPipe::RenderCallback(...)` — the **neo car pipe** | `if(!UG_mod && config->ivMode) …PS2 cb; else if(!UG_mod && iCanHasneoCar) …**Env cb**` — Neo's own cb is **retired** ("lost its shaders → FFP garbage") | `PS2-MODE`/`NEO`/`IV`-specific | **Y** | aap `src/vehiclePipe.cpp:1405-1407` → ours `src/render/vehiclePipe.cpp:4173-4190` |
| P7 | `CAR_MODERN` / `CAR_GTAIV` | **do not exist** | `CAR_MODERN` → Env cb with PS2 fallback; `CAR_GTAIV` → PS2 cb (+ivMode shader swap) | MODERN-ONLY / GTAIV-ONLY | **Y** (our-only paths) | aap `src/vehiclePipe.cpp:1389-1424` (9 cases) → ours `src/render/vehiclePipe.cpp:4092-4230` |
| P8 | `hookBuildingPipe` body | 4 installs incl. `0x5D7F40` | 3 installs; `0x5D7F40` deliberately **not** double-hooked (see H4) | CONDITIONAL | **Y** (via H4) | aap `src/buildingPipe.cpp:805-817` → ours `src/render/buildingPipe.cpp:4178-4195` |
| P9 | `hookVehiclePipe` body | `0x5D9FE9`, `0x5D5B10`, `0x7323C0` (+ `#if 0` block, + `betaEnvmaptest` menu) | **same three**, plus `betaEnvmaptest` without the menu; `#if 0` block dropped | — | **N** for the live installs | aap `src/vehiclePipe.cpp:1457-1476` → ours `src/render/vehiclePipe.cpp:4301-4315` |
| P10 | `envmaphooks` | 7 `InjectHook` on `CEntity::GetIsOnScreen` + `InterceptCall(0x53E9F9, RenderSphereReflections)` | **byte-identical site list** | — | **N** | aap `src/envmap.cpp:455-466` → ours `src/render/envmap.cpp:1247-1258` |
| P11 | `iCanHasbuildingPipe` in env/vehicle gates | gates `CAR_MOBILE` / `CAR_ENV` / `RenderEnvTex` | same gates, but the flag is now **constant true** (see H3) → the gate never fails | ALL-MODES | **Y** (only if aap would have had it false) | aap `src/vehiclePipe.cpp:1414-1419` → ours `src/render/vehiclePipe.cpp:4190-4197`, `src/core/main.cpp:63-64` |

### 2.4 PostFX chain order + transforms

| # | area | aap behaviour | ours | systematic? | breakage | evidence |
|---|---|---|---|---|---|---|
| F1 | Postfx hook addresses | `0x704D1E` ColourFilter_switch, `0x704D5D`/`0x704FB3` Radiosity, `0x704D48` DarknessFilter, `0x704F4B`/`0x704F59` IR+Grain, `0x704EDA`/`0x704EE8` Night+Grain, `0x705078`/`0x705091` Rain+Grain, `0x53EBE9` DrawFinalEffects | **identical addresses** | — | **N** | aap `src/main.cpp:1587-1603` → ours `src/core/main.cpp:4268-4284` |
| F2 | `ColourFilter_switch` chain | key-switch → `usePCTimecyc` alpha fix → `switch(colorFilter)` → `UpdateFrontBuffer()` | identical skeleton **plus**: state-sync check, per-frame `derivePipelineFromPipes`, normal buffer, velocity buffer, SSAO, pipe-chain, sunShafts/DOF/bloom group, motion blur, IV grade, GodRays, colour filter, then `UpdateFrontBuffer` | MODERN/feature-gated (all default-OFF except what modes enable) | **Y** when any of those features is on | aap `src/postfx.cpp:1203-1292` → ours `src/render/postfx.cpp:1820-2632` |
| F3 | **Colour-filter input transform** | **no RGB transform at all** — `rgb1/rgb2` passed raw to `ColourFilter_PS2` (which then does `D3DTOP_MODULATE2X`, ×2) | for MOD2X consumers (`PS2`/`VC`/`VCS`): `SoftCompress` (≈×0.68 with an upper-range knee) **then ×0.5** (`MODULATE2X_COMPENSATION`) → net ≈ **0.68 × aap** before the shader's own ×2 | `PS2-MODE` + VC/VCS filters | **Y** | aap `src/postfx.cpp:1230-1255`, `:928-943` → ours `src/render/postfx.cpp:2055-2160` |
| F4 | PC/None consumers | raw `rgb1pc/rgb2pc` | **also raw** — "Fix R1" explicitly excludes vanilla consumers from the ×0.34 pre-scale | PC / None / Xbox mode (`COLORFILTER_PC`) | **N** | aap `src/postfx.cpp:1257-1260` → ours `src/render/postfx.cpp:2045-2057`, `:2406-2412` |
| F5 | `usePCTimecyc` alpha fix | `if(config->usePCTimecyc){ alpha/=2 } else { alpha*2 clamp }` | `if(usePCTimecyc \|\| pipeline == PIPELINE_PBR){ alpha/=2 }` — PBR forces the PC-range branch | MODERN-ONLY | **Y** (PBR never converts) | aap `src/postfx.cpp:1233-1248` → ours `src/render/postfx.cpp:2089-2092` |
| F6 | `rgb1Mult` / `rgb2Mult` | **do not exist** | `rgb1/rgb2 *= config->rgb1Mult/rgb2Mult` before the switch (INI default 1.0; **absent key + no mode → 0.0 → black PS2 filter**) | `PS2-MODE`/VC/VCS (only `rgb1/rgb2` are scaled, not `rgb1pc/rgb2pc`) | **Y** on an INI missing the key | aap: *absent* → ours `src/render/postfx.cpp:2162-2168`, `src/core/config.cpp:551-552` |
| F7 | `colorFilter` switch cases | `None/PS2/PC/Mobile/III/VC` (+`VCS` under `#ifdef VCS`); `default: return` for None | + `VCS` always, + `GTAIV`, + `MODERN`; `None` now falls through to a **vanilla `ColourFilter` composite** instead of returning | MODERN-ONLY / VCS | **Y** for None (ours re-composites, aap does nothing) | aap `src/postfx.cpp:1253-1276` → ours `src/render/postfx.cpp:2390-2445` |
| F8 | `DrawFinalEffects` | YCbCr only, early-return otherwise | SMAA → FXAA3 → ImGui → Forward+ debug → YCbCr → dump/sync probes | feature-gated (SMAA/FXAA default?) | **Y** when on; the extra `PostfxStateSyncCheck("cf_end")` runs always | aap `src/postfx.cpp:1309-1370` → ours `src/render/postfx.cpp:2678-2848` |
| F9 | `UpdateFrontBuffer` | 5 lines: `EndUpdate / PushContext / RenderFast(camRas,0,0) / PopContext / BeginUpdate` | same blit, plus NULL guards, a **screen-size mismatch bail** (`camRas != screen → skip + mark FB invalid`), and copy-failure latching | ALL-MODES | **N** (guards only; the blit is identical) — but the 2048² padded `pRasFB` UV handling is ours-only (see F10) | aap `src/postfx.cpp:159-167` → ours `src/render/postfx.cpp:605-660` |
| F10 | Front-buffer UV space | consumers sample `pRasterFrontBuffer` directly; no padded-raster concept | `pRasFB` is a **padded 2048²** raster holding screen content in its top-left; every new consumer must apply the `c3` front-buffer UV scale or it samples black/garbage below 2048 px | ALL-MODES (any new pass) | **Y** (this is the documented root of several of our own bugs; aap cannot have it) | aap `src/postfx.cpp:12` (raw `0xC402D8`) → ours `src/render/postfx.cpp:4053-4059`, `:4673-4695`, `:5009-5011` |
| F11 | `Radiosity` dispatch | `Radiosity` → `Radiosity_shader` when `config->radiosity == PS2`; the **`Radiosity_VCS` call is inside a `/* … */` comment block** and `Radiosity_VCS` itself is compiled out under `#ifdef VCS_POSTFX` (`168..488`) → **dead in the release aap build** | `Radiosity` runs `Radiosity_VCS(trailsLimit, trailsIntensity)` + `Blur_VCS()` **unconditionally whenever `config->vcsTrails`** (which is itself read outside `#ifdef DEBUG`) | VCS-filter / `vcsTrails=1` | **Y** (ours can enter a whole pass aap's build cannot) | aap `src/postfx.cpp:655-670` + `:543-550` (commented) + `:168/:488` → ours `src/render/postfx.cpp:1391`, `:1427-1432` |
| F12 | `Initialise_skygfx` Im2D hooks | hooks `0x7FB824` `Im2dSetPixelShader`, `0x7FB885` colour-mod, `0x7FB8A6` alpha-mod, creates `grainRaster` | **same three hooks + `CreateShaders()`** (plus scope registration) | — | **N** | aap `src/postfx.cpp:1381-1391` → ours `src/render/postfx.cpp:5405-5415`, reached from `src/core/main.cpp:3901-3907` |

### 2.5 Config semantics (all 65 aap keys)

**Key coverage: all 65 aap keys are read.** The 8 that a bare `grep cfg.get` misses
(`ps2Modulate`, `dualPass`, `dualPassBuilding`, `dualPassDefault`, `dualPassPed`, `dualPassVehicle`)
are routed through `readint2()` which **falls back to `[SkyGfx] <key>`** when the lane section is
absent (`config.cpp:117-123`), and `pedShadows`/`stencilShadows` are read straight from `[SkyGfx]`
(`config.cpp:487-506`). `ps2grassFiles` is commented out on both sides. → **rule out "key dropped".**

| # | key / area | aap | ours | systematic? | breakage | evidence |
|---|---|---|---|---|---|---|
| C1 | `buildingPipe`/`vehiclePipe` **absent** | `StrAssoc::get` returns `-1` → `iCanHas* = false` → **custom pipes never installed**, `c->*Pipe = 0` | no `iCanHas*` write → `pipeline` lock runs → absent `pipeline` key ⇒ **`PIPELINE_PBR` ⇒ `BUILDING_PBR`+`CAR_MODERN`+`COLORFILTER_MODERN`** | CONDITIONAL (legacy INI) / MODERN (our default) | **Y** — *largest single INI-semantics delta* | aap `src/main.cpp:926-959` → ours `src/core/config.cpp:208-261` + `src/core/main.cpp:3930-3935` |
| C2 | `pipeline` key | **does not exist** | parsed first, locks building/vehicle/colorFilter; absent → `PIPELINE_PBR` | MODERN-ONLY | **Y** | aap: *absent* → ours `src/core/config.cpp:208-261` |
| C3 | `envFresnel` default | `0.7` | `0.4` (INI ships `0.4`; also clamped to `[0..0.4]`) | MODERN/Env-car only | **Y** on an INI missing the key | aap `src/main.cpp:968` → ours `src/core/config.cpp:401`, `src/core/main.cpp:411-427` |
| C4 | `envPower` default | `20.0` | `c->envPower` → **0.0** when the key is absent (zero-init `Config`) | Env/Modern cars | **Y** on an INI missing the key | aap `src/main.cpp:967` → ours `src/core/config.cpp:386` |
| C5 | `envMapSize` default + pow2 snap | default `256`, then `while(i < size) i *= 2` | default `c->envMapSize` → **0** when absent; snap is applied only if present | Env/Modern | **Y** on an INI missing the key | aap `src/main.cpp:969-972` → ours `src/core/config.cpp:433-436` |
| C6 | `sunGlare` absent | `-1` → `iCanHasSunGlare=false` → **`0x6ABCFD` never hooked** | default `c->doglare` = `0` → hook **installed** but the asm `cmp [ecx+12],0; jle` no-ops | — | **N** (output-equal) | aap `src/main.cpp:973-977`, `:630-643` → ours `src/core/config.cpp:460-464`, `src/core/main.cpp:4007` |
| C7 | `neoWaterDrops` absent | `-1` → `iCanHasNeoDrops=false` → `hookWaterDrops()` skipped | default `0` → hooks installed, **bodies gated on `config->neoWaterDrops`** | — | **N** (output-equal) | aap `src/main.cpp:1055-1059` → ours `src/core/config.cpp:586-590`, `src/render/neopipe.cpp:1755/1764/1779/1821/1862/1873` |
| C8 | `pedShadows`/`stencilShadows` numeric forms | `boolMap` only — a value of `2` silently becomes `-1` (leave game default) | numeric parse + clamp to `[-1..1]` (`2 → 1`) | PEDS-ONLY | **Y** only for hand-edited values `≥2` | aap `src/main.cpp:986-996` → ours `src/core/config.cpp:473-506` |
| C9 | `ps2Modulate`/`dualPass` **global** write target | written to **`config`** (the *live* slot), not `c` — an inactive slot's parse stomps slot 0 | written to **`c`** (the slot being parsed) | multi-config only | **N** (ours is the correct behaviour; single-INI identical) | aap `src/main.cpp:923-924` → ours `src/core/config.cpp:300-301` |
| C10 | `vcsTrails`/`trailsLimit`/`trailsIntensity` | read under `#ifdef DEBUG` (absent in the release aap build) | read **unconditionally** (+ new `trailsResolution`) | VCS-filter only | **Y** (VCS trails reachable in ours, dead in aap) | aap `src/main.cpp:1045-1049` → ours `src/core/config.cpp:577-580` |
| C11 | `colorFilter` PBR force | *n/a* | `if(pipeline==PIPELINE_PBR && colorFilter!=MODERN) colorFilter = MODERN` (warn + override) | MODERN-ONLY | **Y** | aap: *absent* → ours `src/core/config.cpp:543-547` |
| C12 | `coronaZtest` patch | byte patch at `0x6FB17C` | no bytes (SilentPatch), runtime force only | ALL-MODES when set | see S13 | aap `src/main.cpp:226-240` → ours `src/core/main.cpp:373-389` |
| C13 | `doRadiosity` default source | `original_bRadiosity` captured in `readInis()` from `CPostEffects::m_bRadiosity` | **identical** | — | **N** | aap `src/main.cpp:1083-1092`, `:1037` → ours `src/core/config.cpp:1503-1512`, `:568` |
| C14 | `readIni` file discovery / `findInis` / `setConfig` / `reloadAllInis` | — | structurally identical (ours adds bounds guards) | — | **N** | aap `src/main.cpp:839-857`, `:1094-1112` → ours `src/core/config.cpp:1482-1535`, `src/core/main.cpp:3690-3718` |
| C15 | `resetValues` | pushes radiosity/colour offsets/grain/YCbCr **+ coronaZtest byte patch** | pushes the same postfx state; coronaZtest retired | ALL-MODES when `coronaZtest != -1` | see S13 | aap `src/main.cpp:188-242` → ours `src/core/main.cpp:357-389` |

### 2.6 Init order / plugin attach

| # | area | aap | ours | systematic? | breakage | evidence |
|---|---|---|---|---|---|---|
| I1 | `DllMain` ordering | version-gate → console → `0x7646E0` → defaultColour capture → moon-mask → **`0x74872D` hook** → `0x5BCF14` → `0x7491C0` → `0x5BF8EA` → `0x756DFE` → `fixSeed`/`saveIntensity`/`setTextureAndColor` → DUAL ×2 → `0x4C88F0` → `0x553AD1` → skidmarks → tags → postfx → pointlight → ps2rand → `0x53D903` → procobj → misc nops → `hooktexdb` | same order **except** `InjectDelayedPatches()` runs *inline at `:4196`* (between moon-mask and `0x5BCF14`), and the point-light fix moved into `rw_fixes_installHooks` | ALL-MODES (ordering) | N (independent call sites) | aap `src/main.cpp:1520-1688` → ours `src/core/main.cpp:4122-4344` |
| I2 | `readIni` timing | first read inside `InjectDelayedPatches` (game-startup), re-read from `afterStreamIni` (`0x5BCF14`) | first read inside `InjectDelayedPatches` (**DLL attach**), re-read from the same `afterStreamIni` hook | ALL-MODES | N (both precede `stream.ini`, both re-read) | aap `src/main.cpp:1374-1379`, `:1115-1122` → ours `src/core/main.cpp:3815-3821`, `:3722-3729` |
| I3 | `InitialiseGame_hook` | `envmaphooks(); neoInit(); initTexDB(); InitialiseGame();` (no re-entry guard, no SEH) | `installDeferredHooks()` (scene hook + `envmaphooks` + `mirrorPass_init`) → `neoInit()` → `TexDefsLoadDatabase()` → `InitialiseGame()` **inside `__try/__except`**, with a re-entry counter | ALL-MODES; **MoonLoader skips this entirely** | **Y** via H12 (texdb) | aap `src/main.cpp:770-784` → ours `src/core/main.cpp:3132-3175` |
| I4 | `CPostEffects::Initialise` → `Initialise_skygfx` | hooks `0x7FB824` (Im2dSetPixelShader), `0x7FB885` (colour-mod), `0x7FB8A6` (alpha-mod) + `CreateShaders()` + `grainRaster` | **same three hooks + `CreateShaders()`**; reached through the identical `InterceptCall` at `0x5BD779` / UG `EVENT_INITPOSTEFFECTS` | — | **N** | aap `src/postfx.cpp:1381-1391` → ours `src/render/postfx.cpp:5405-5415`, `src/core/main.cpp:3901-3907` |
| I5 | `TexDefsInit` (= aap `hooktexdb`) | 3 unconditional installs: `0x731CCB`, `0x7319EB`, `Patch(0x731FD5+1, TexDbFindCB)` | same 3, all **byte-verified**; mismatch → that install is skipped | CONDITIONAL (foreign ASI) | **Y** (texture find-chain silently not installed) | aap `src/texdb.cpp:248-255` → ours `src/texdefs.cpp:882-930` |
| I6 | `Mobile_details.txd` load | `initTexDB()` from `InitialiseGame_hook`; `MessageBox + exit(0)` on a corrupt file; `faketexinfo` left zeroed | `TexDefsLoadDatabase()` same behaviour **but seeds `faketexinfo.dualPass = 1`** | see H12 | **Y** if the load never runs | aap `src/texdb.cpp:128-148` (`MessageBox` `:138-139`) → ours `src/texdefs.cpp:1069-1102` |
| I7 | `texdb.txt` parser | fields: `alphamode, isdetail, hasdetail, detailtile, hassibling, affiliate` | superset: + `noiseDetail`/`stochastic`, + `dualPass`, + `zwriteThreshold`; entry insert condition also requires those new flags | ALL-MODES (only if a custom `texdb.txt` uses the new keys) | **N** for a stock aap `texdb.txt` | aap `src/texdb.cpp:36-126` → ours `src/texdefs.cpp:936-1067` |
| I8 | `DLL_PROCESS_DETACH` | **none** (aap never cleans up) | guarded shutdown: TexDefs / envmap / water / forward+ / ragdoll / softbody / VEH removal | shutdown only | N | aap: *absent* → ours `src/core/main.cpp:4349-4413` |

---

## 3. TOP-10 — "could break visuals", ranked

Ranked by *(probability the mechanism is live in a normal session) × (visible area of the frame)*.
Class is what the mechanism can reach; `Y/N` = breakage candidate.

| rank | mechanism | class | what you would see | evidence |
|---|---|---|---|---|
| **1** | **Three-system forced state at frame boundaries**: `pipeApplyWorldBaseline` (4 sites) + `pipeRepairSceneStartSync` (every scene) + `StateBlockEnforceCanonical` (every frame, *unconditional*). Forces `VERTEXALPHA=0 / ALPHABLEND=0 / ZWRITE=1 / ZTEST=1 / ZFUNC=LESSEQUAL / CULL=BACK / ALPHATESTENABLE=1 / COLORWRITE=0xF`. `stateBaselineEnable` is floored to 1 — **cannot be turned off from the INI**. | **ALL-MODES** | any pass that legitimately *entered* the frame with alpha blending or CULLNONE gets flipped to opaque + CULLBACK at the boundary: transparent world geometry → flat opaque sheets, double-sided geometry → backface holes, an additive sky/particle pass left over from the tail → re-composited as opaque. This is the strongest single explanation for "grey slab" / "olive band" style sheets that are *not* texture lookups. | ours `src/render/pipelinecommon.cpp:2592-2663`, `:1571-1606`, `:1647-1760`; gates `src/core/config.cpp:694-709`; calls `src/core/main.cpp:1605`, `:1620`, `:3760`, `:3768`. aap: **absent** (`src/main.cpp:736-752` has only the camera dance) |
| **2** | **Default-pipe entry contract with no save/restore** (`pipe==NULL` branch): forces the full canonical world signature through L1+L2+L3 and *leaves it behind* on purpose. | **ALL-MODES** | every prop / pickup / marker atomic rewrites global state. The forced `ALPHAREF=1` changes the alpha-cutout threshold for anything drawn after (vs. aap's inherited ref); forced `VERTEXALPHA=FALSE` makes an inherited blended draw opaque; forced `ZWRITE=1` makes a later transparent draw depth-fight. Direct mechanism for "slabs occlude the player" if a quad is drawn through this pipe. | ours `src/core/render_callbacks.cpp:587-655`. aap `src/main.cpp:313-317` (read-only) |
| **3** | **Ped entry/exit contract + unconditional `pedOpaqueEnforce` + sticky ZWRITE/depth repair**: cull force, alpha/blend block, `GE@1`, blend-off, `ZFUNC=LESSEQUAL`, all 3 layers, restored-or-stickily-repaired at exit. | **PEDS-ONLY** (every ped, every frame) | peds always opaque and depth-writing — fixes draw-over-CJ but *also* means a ped span can no longer inherit a legitimate translucent state (e.g. a ped under a transparent shader, a shadow pass wrapping skin). Sticky repair leaves canonical state behind → the *next* consumer inherits it. | ours `src/core/render_callbacks.cpp:731-941`, `:1231-1319`. aap `src/main.cpp:318-331` |
| **4** | **Ours-only state shields in the effects/cloud/corona/sprite/point-light phase**: `RenderEffects`/`CCoronas::*`/`CClouds::Render` intercepts + `rwFixes_installHooks` 6 machines + `shadows_installHooks`. aap touches *none* of these. | **ALL-MODES** | each shield forces a blend/depth/alpha signature around a draw aap left alone. If a shield's target value is wrong for a particular effect (e.g. forcing vanilla additive where the game wanted normal-alpha, or the reverse), the whole corona/particle/glow batch changes appearance — black-quad and dark-overlay family. | ours `src/core/main.cpp:3977-3978`, `:4251-4260`; `src/core/rw_fixes.cpp:1852-2077`; `src/core/shadows.cpp:549-800`. aap: no equivalent (`src/main.cpp:1579-1606`) |
| **5** | **Missing scene-start far/fog camera re-update** (aap's `RwCameraEndUpdate/BeginUpdate` at `RenderScene_before` is commented out in ours). | **ALL-MODES** | the frame's frustum + fog distance are computed from the *previous* frame's values. On weather/far-plane transitions: distant terrain clipped a frame late (hard horizon cut → reads as a "floor slab" at the vanishing point) and a fog-colour seam band at the cutoff ("olive band"). Known deliberate omission — MED confidence, but it is a pure aap-parity hole. | aap `src/main.cpp:736-742` → ours `src/core/main.cpp:1664-1670` (commented) |
| **6** | **Dual-pass gate semantics changed**: `atomicHasAlpha()` veto on the default pipe + per-texture `TexInfo->dualPass`/`zwriteThreshold` + `faketexinfo.dualPass` seeding. | **ALL-MODES** where `dualPass*=1` (Xbox `modeManager.cpp:314/322`, PC `:405-413`, Mobile, Modern `:715-722`) | alpha-cutout props (fences, grates, bushes — alpha-255 materials over alpha<128 texels) **lose leg 2** → cutout regions vanish instead of being painted → holes/black fringes on exactly the geometry the dual pass exists for. Under MoonLoader the effect is global (H12). | ours `src/core/render_callbacks.cpp:656-669`, `:1074-1084`, `src/core/main.cpp:293-303`; aap `src/main.cpp:312-333` (no alpha gate, no TexInfo) |
| **7** | **`pipeline` key defaults to PBR and force-locks `BUILDING_PBR + CAR_MODERN + COLORFILTER_MODERN`** — the shipped `skygfx.default.ini:41` is `pipeline = PBR`. | **MODERN-ONLY** (= our default) | "modern = very broken" is *this row plus our-only machinery* (PBR building cb, Env-based `CAR_MODERN`, Hable tonemap, forward+, SSAO/SMAA/motion blur, padded-2048² front buffer). **There is no aap baseline to compare against** — hunt inside our-only code, not in the diff. | ours `src/core/config.cpp:208-261`, `:543-547`; aap: no `pipeline` key (`src/main.cpp:920-1079`) |
| **8** | **`pAmbient` override on the skin pipe** — `pAmbient->color = GetTimecycleAmbient()` for every skin atomic. | **PEDS-ONLY** | peds lit from a different ambient source than the rest of the frame → peds lighter/darker than the wall behind them, or grey/black at night (the black-guard only catches exact zeros). Combined with our `Weather_Update()` blending `weathers2.dat` into `m_CurrentColours`, the ped ambient and the world ambient can diverge further than in aap. | ours `src/core/render_callbacks.cpp:1007-1058`; aap `src/main.cpp:305-335` (no ambient write) |
| **9** | **PS2 colour-filter input pre-scale** (`SoftCompress` ≈×0.68 + ×0.5) applied where aap passes raw values. | **PS2-MODE** (+ VC/VCS) | the PS2 filter renders at ≈0.68× aap's brightness with an upper-range knee → visibly darker/less contrasty PS2-mode frames. **PC/None/Xbox mode unaffected** (Fix R1). Watch `rgb1Mult`/`rgb2Mult`: absent key + no mode ⇒ 0 ⇒ black filter. | ours `src/render/postfx.cpp:2055-2168`; aap `src/postfx.cpp:1230-1255` + `:928-943` |
| **10** | **`iCanHasbuildingPipe`/`iCanHasvehiclePipe` retired + pipe-map alias losses** (`PC`→Xbox gone, `LCS`/`Spec` gone) + byte-verified hook gates that can silently skip aap's installs. | **CONDITIONAL** (legacy INI, `explicitBuildingPipe`, foreign ASI) | with an aap-era INI: aap = vanilla PC pipes, ours = derived PBR pipes → *everything* looks different (this is a config-semantics break, not a render bug). With a foreign ASI: `0x756DFE`/`0x5DADB7`/`0x5DAE61`/`0x731FD5` skipped → grass PS2-modulate and the texture find-chain silently die. | aap `src/main.cpp:926-959`, `:1401-1406`, `:1563-1566` → ours `src/core/main.cpp:3930-3935`, `:4202-4226`; `src/core/config.cpp:307-333`; `src/texdefs.cpp:882-930` |

**Runners-up (kept in the table, not in the top-10):** H12 MoonLoader texdb (needs MoonLoader),
H4/H5/H6 rwnormal-plugin hook ownership, F10 padded-front-buffer UV contract (ours-only by
construction), F11 live `Radiosity_VCS` path (needs `vcsTrails=1`), H7/I5 byte-verify skips
(needs a foreign ASI).

---

## 4. Ruled OUT — explicit "stop suspecting" list

Each line has both sides' evidence and a reason the divergence cannot be *visible*.

| area | verdict | evidence (aap → ours) |
|---|---|---|
| **Postfx hook address set** (11 sites) | **identical** | `src/main.cpp:1587-1603` → `src/core/main.cpp:4268-4284` |
| **DllMain hook address set** (`0x7646E0`, `0x713C4C`, `0x5BCF14`, `0x7491C0`, `0x5BF8EA`, `0x5D9EEB`, `0x5D9EFB`, `0x4C88F0`, `0x553AD1`, `0x53E175`, `0x534335`, `0x4C4412`, `0xA9AD78`, `0x53D903`, `0x44E82E/0x44ECEE/0x42453B/0x42454D`, `0x5A3*` procobj set, `0x73290A`, `0x733313/0x73405A/0x733403/0x73431A/0x73444A`, `0x5DDB3D`, `0x6E716B/0x6E7176`, `0x553C7D/78/9A/CD/EC`, `0x726516/34/52/70`) | **identical** | `src/main.cpp:1540-1685` → `src/core/main.cpp:4181-4342` |
| **FX-quality shadow hooks** (7 sites) | **identical** | `src/main.cpp:1425-1433` → `src/core/main.cpp:3954-3960` |
| **`envmaphooks`** (7 `GetIsOnScreen` + `0x53E9F9`) | **byte-identical list** | `src/envmap.cpp:455-466` → `src/render/envmap.cpp:1247-1258` |
| **`GetPipelineID` / `SetPipelineID`** | **byte-identical** | `src/pipeplg.cpp:7-17` → `src/rw/pipeplg.cpp:27-37` |
| **PDS `0x131` `pdsAtmCB`** (6 building pipe IDs) | same logic, migrated file | `src/pipeplg.cpp:46-64` → `src/texdefs.cpp:98-115` |
| **`fixSAMP`** (fog table/vertex resync) | **identical body**, called from `D3D9Render` + both pipe switches on both sides | `src/main.cpp:84-109`, `:114` → `src/core/main.cpp:181-206`, `:211`; `src/buildingPipe.cpp:3855`, `src/render/vehiclePipe.cpp:4266` |
| **`D3D9Render`** (draw wrapper) | **identical** | `src/main.cpp:112-119` → `src/core/main.cpp:209-216` |
| **Dual-pass leg sequence** (GE@thr + ZWRITE on → LESS@thr + ZWRITE off → restore) | **same order**; ours only adds the L2/L3 mirror (S9) | `src/main.cpp:131-142` → `src/core/main.cpp:304-311` |
| **`grassRenderCallback` core** (ps2ModulateGrass PS bind, cull save/patch/restore, dual legs, `plantTab0/1` install) | **same**; ours adds a frame-latched depth contract + receipts | `src/main.cpp:256-302` → `src/core/main.cpp:828-960` |
| **`CTagManager__SetupAtomic` / `__RenderTag` / `TagRenderCB` at `0xA9AD78`** | **identical** | `src/main.cpp:337-365`, `:1582-1584` → `src/core/main.cpp:949-975`, `:4263-4265` |
| **`CSkidmarks__Render`** (alpha-func ALWAYS wrap) | **identical** | `src/main.cpp:718-726` → `src/core/main.cpp:1437-…` |
| **`renderMoonMask` + separate-alpha blend pair** | **identical** hook address & body | `src/main.cpp:693-716`, `:1553` → `src/core/main.cpp:1348-1430`, `:4189` |
| **`doglare`** (`0x6ABCFD`) | **identical** naked stub, `config+12` offset re-pinned by `static_assert` | `src/main.cpp:630-643` → `src/core/main.cpp:1290-…`, `src/skygfx.h:1022` |
| **`fixSeed` / `saveIntensity` / `setTextureAndColor`** (grass placement + colour chain) | **identical** (ours additionally byte-gated — H7) | `src/main.cpp:481-525`, `:1564-1566` → `src/core/main.cpp:1029` (`setTextureAndColor`), `:1063` (`fixSeed`), `:1090` (`saveIntensity`), `:4215-4227` |
| **`readIni` key set (65 keys)** | **all present**; the 8 "missing from grep" keys route through `readint2` → `[SkyGfx]` fallback | `src/main.cpp:920-1079` → `src/core/config.cpp:117-123`, `:300-301`, `:473-506` |
| **`readInis` `original_bRadiosity` capture** | **identical** | `src/main.cpp:1083-1092` → `src/core/config.cpp:1503-1512` |
| **`setConfig` / `reloadAllInis` / `findInis` / multi-config scan** | **identical structure** (ours adds bounds guards only) | `src/main.cpp:839-857`, `:1094-1112` → `src/core/config.cpp:1482-1535`, `src/core/main.cpp:3690-3718` |
| **`ColourFilter_switch` config-switch / reload hotkey blocks** | **identical** (ours adds a toast message) | `src/postfx.cpp:1205-1228` → `src/render/postfx.cpp:1999-2023` |
| **`usePCTimecyc` alpha halving** | present (ours adds a PBR term — F5) | `src/postfx.cpp:1233-1248` → `src/render/postfx.cpp:2089-2092` |
| **`DrawFinalEffects` YCbCr body** (RGB2YUV/YUV2RGB matrices, `gradingPS`, Im2D quad) | **byte-for-byte the same math**; ours adds `FOGENABLE=TRUE` restore (strictly safer) | `src/postfx.cpp:1309-1370` → `src/render/postfx.cpp:2746-2815` |
| **`UpdateFrontBuffer` blit** (`EndUpdate/Push/RenderFast(0,0)/Pop/BeginUpdate`) | **identical blit**; ours only adds guards | `src/postfx.cpp:159-167` → `src/render/postfx.cpp:605-660` |
| **`Radiosity_shader` vs classic-blend dispatch inside `Radiosity`** | **same two-way structure** (the VCS half differs — see F11, it is *not* ruled out) | `src/postfx.cpp:664-670` → `src/render/postfx.cpp:1449-1450` |
| **`ColourFilter_PS2` (`D3DTOP_MODULATE2X`), `_Mobile`, `_Generic(III/VC)`** | **same consumers**; only the *inputs* differ (F3) | `src/postfx.cpp:928-943`, `:1253-1276` → `src/render/postfx_colourfilter.cpp:941-…`, `src/render/postfx.cpp:2390-2445` |
| **`texdb.txt` core fields** (alphamode/detail/hassibling/affiliate) | **same parser core**; ours is a superset (I7) | `src/texdb.cpp:36-126` → `src/texdefs.cpp:936-1067` |
| **`hooktexdb` sites** (`0x731CCB`, `0x7319EB`, `0x731FD5`) | **same 3 sites**; ours byte-verifies first (I5) | `src/texdb.cpp:248-255` → `src/texdefs.cpp:882-930` |
| **`Initialise_skygfx` Im2D hooks** (`0x7FB824` / `0x7FB885` / `0x7FB8A6`) + `CreateShaders()` | **identical three installs** | `src/postfx.cpp:1381-1391` → `src/render/postfx.cpp:5405-5415` |
| **`Mobile_details.txd` hard-fail** (`MessageBox + exit(0)`) | **identical** | `src/texdb.cpp:137-143` (MessageBox `:138-139`) → `src/texdefs.cpp:1081-1084` |
| **`DrawDebugEnvMap`** | aap's body is **empty** unless `DEBUGENVTEX` (release = no-op); ours removed it as dead code → **no visual delta** | `src/envmap.cpp:344-353` → `src/core/main.cpp:1486` |
| **`SetLightsWithTimeOfDayColour`** | **commented out on both sides** (never installed) | `src/main.cpp:1669` → `src/core/main.cpp:988-1006` (defined, never hooked) |
| **`iCanHasSunGlare` / `iCanHasNeoDrops` absent-key semantics** | output-equivalent (C6/C7): aap skips the install, ours installs a body-gated no-op | `src/main.cpp:973-977`, `:1055-1059` → `src/core/config.cpp:460-464`, `:586-590` |
| **`Nop(0x5BBF6F,2)` / `Nop(0x5BBF83,2)`** (timecycle colour-filter alpha) | **identical** | `src/main.cpp:1393-1394` → `src/core/main.cpp:3922-3923` |
| **`fixPcCarLight` / `disableClouds` / `disableGamma` / `transparentLockon` / `fixShadows` / `privateHooks loadsc0`** | **identical patch sets** (only `0x748AA8` differs — H13) | `src/main.cpp:1435-1497` → `src/core/main.cpp:3962-4036` |
| **`hookBuildingPipe` / `hookVehiclePipe` live installs** (minus the 0x5D7F40 ownership move) | **same 3 + same 3 sites** | `src/buildingPipe.cpp:805-812`, `src/vehiclePipe.cpp:1457-1463` → `src/render/buildingPipe.cpp:4178-4186`, `src/render/vehiclePipe.cpp:4301-4310` |

---

## 5. W0.5 symptom cross-reference

| W0.5 symptom | owning lane | what aap's equivalent does | can this audit's rows produce it? |
|---|---|---|---|
| **rainbow RGB channel-strips** | postfx / VCS class (sibling lane owns the hunt) | aap's `COLORFILTER_VCS` **does not exist in the release build** (`#ifdef VCS`, `src/main.cpp:1010-1012`), `vcsblurrgb` is `#ifdef VCS` (`src/postfx.cpp:1250-1252`), `vcsTrails` is read only under `#ifdef DEBUG` (`src/main.cpp:1045-1049`), **and the `Radiosity_VCS` call inside `Radiosity` is commented out** (`src/postfx.cpp:543-550`) with `Radiosity_VCS` itself compiled out by `#ifdef VCS_POSTFX` (`:168/:488`). **aap's release build has no VCS/trails path at all.** | Not attributable from this audit (sibling lane). Note for that lane: ours makes all four of those reachable — `COLORFILTER_VCS` unconditional, `vcsTrails` read unconditionally (`config.cpp:577-580`), `vcsblurrgb` set unconditionally (`postfx.cpp:2170`), and `Radiosity_VCS` called live (`postfx.cpp:1427-1432`) — rows C10 / F11. |
| **black streak bars** | corona/sprite-flush + effects-phase shields | aap has **no** corona/sprite/flush shields at all (`src/main.cpp` has no `0x6FAEC0`/`0x70CF62`/`0x701FE9`/`0x7273D0`) | **Row 4** — our shields are the only new code touching that phase; if a shield's forced value is wrong it produces exactly this class. Also **Row 1** (frame-end canonical forcing `ALPHABLEND=0` after the 2D tail). |
| **grey slab sentinels** | texdefs miss-policy — **intentional** | aap's `TexDbFindCB` (`src/texdb.cpp:185-207`) just returns the game texture or `nil` on a miss — **no fallback geometry at all** | **Ruled out as a *parity* defect — it is our deliberate N9 fail-closed policy**: a world-texture miss binds a 1×1 **opaque dim-grey `0x606060`** sentinel (`src/texdefs.cpp:493-530`, applied in `TexDefsFindCB` `:669-735`), which is precisely what a "huge hard-edged dark-grey slab" is. Confirm with the `[texdefs] miss` receipt (`TexDefsNoteMiss`, `:297`) before hunting any other cause. **But** Rows 1/2 can *also* paint flat opaque quads with no texture miss — a slab without a `[texdefs] miss` line is NOT this policy. |
| **head-height marker cone/box** | game `C3dMarkers` class | aap does nothing to `C3dMarkers` (no hook at any `0x5*` marker address in `src/main.cpp`) | **Row 2** — the default-pipe entry contract runs on marker atomics and forces `ALPHAREF=1` + `VERTEXALPHA=0` + `ZWRITE=1`, which changes how a marker quad renders vs. aap's inherited state. Our own triage already attributes this to `C3dMarkers` geometry/scale; this row explains the *state* side only. |
| **olive bands** | alpha-sheet / vegetation | aap: `grassRenderCallback` + dual pass only (`src/main.cpp:256-302`) | **Row 1** (forced `VERTEXALPHA=0`/`ALPHABLEND=0` at a boundary turning a translucent band opaque), **Row 5** (fog-distance seam), **Row 6** (dual-pass leg loss on cutout vegetation). |
| **floor slabs** | world geometry / depth | aap: nothing equivalent | **Row 1** (baseline forces `ZWRITE=1`+`ZFUNC=LESSEQUAL` globally → an object that should be depth-tested-away is not), **Row 5** (stale far plane clipping distant ground). |

---

## 6. Verification notes & limits

1. **Every divergence row in §2 carries both sides' file:line.** Rows marked `*absent*` were
   verified by grepping the reference tree for the symbol/address (e.g. `grep -n 53E0BE
   E:\SDKs\skygfx_original\src` → 0 hits; `grep DebugMenuLoad src` → 0 hits).
2. **Line numbers are for the two commits named in §0.** They will drift on the next edit; the
   *addresses* (`0x53E0BE`, `0x7491C0`, `0x704D1E`, …) are the stable anchors.
3. **Not verified here (out of scope for a read-only source diff):** runtime shader binaries, the
   shipped `models/texdb.txt`, `skygfx.ini` actually on the user's machine (only
   `skygfx.default.ini` was read), and whether the user's session has MoonLoader / SAMP /
   the DK22Pac rwnormal plugin present. Rows H4/H5/H6/H7/H12/C1 are *conditional* on exactly
   those facts — a single log line (`grep -E "nm_install|texdefs|InitialiseGame|patch_verify"`)
   resolves them.
4. **Three follow-ups this diff cannot settle:** (a) row **F11** — confirm whether the user's
   session ever sets `vcsTrails=1`, because that turns on a whole `Radiosity_VCS` pass aap's
   release build cannot execute; (b) row **4** — the six `rwFixes_installHooks` machines and the
   four effects-phase intercepts have *no aap counterpart*, so their forced target values must be
   validated by reading each shield body — they are the only "new state in an old phase");
   (c) rows
   **H4/H5/H6** — whether the DK22Pac rwnormal plugin actually attaches decides which of three
   different `0x5D7F40`/`0x5D5B80`/`0x5DA610` owners is live.
5. **Deliberately excluded:** pure performance work, crash-only paths, DLL detach, debug-menu UI,
   and anything the brief already labelled "known better" (`myPluginAttach` short-circuit).
