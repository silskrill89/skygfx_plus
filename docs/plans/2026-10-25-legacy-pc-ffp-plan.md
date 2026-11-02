# Legacy PC building pipeline (FFP) — ready-to-code plan

**Date:** 2026-10-25 (plan) · **Verified against source:** 2026-10-26
**Status:** implementation plan (read-only audit; no source edits made)
**Goal:** restore the shipped PC building renderer as `BUILDING_PC`, completing the building half of `MODE_PC_VINTAGE`. The vehicle half (`CAR_PC` → exe `0x5D9900`) is already live.

> **Anchor caveat.** `src/core/main.cpp` and `src/render/buildingPipe.cpp` were being edited by another lane while this audit ran (line counts changed: main.cpp 7959 → 8043, buildingPipe.cpp 3363 → 3479). Every anchor below gives the **symbol** first and the line number seen at audit time second. If a line moved, locate by symbol; the insertion point is still valid. No source was modified for this plan.

---

## 0. TL;DR

1. **Vehicle PC is live.** `vehiclePipe=PC` → `CAR_PC` (=1) → `CCustomCarEnvMapPipeline__CustomPipeRenderCB_exe` → `EAXJMP(0x5D9900)`. Sources: `src/skygfx.h:171`, `src/core/main.cpp:5647`, `src/render/vehiclePipe.cpp:3641-3643` (switch), `src/rw/gta.cpp:163` (trampoline).
2. **Building PC does not exist.** No `BUILDING_PC`, no `PIPELINE_PC`, no `buildingPipe=PC` map entry. `buildingPipe=PC` currently falls through `StrAssoc::get` to the `-1` sentinel and is **silently ignored** (`src/core/main.cpp:5634-5642`; `StrAssoc::get` returns the sentinel at `:5286-5294`). `MODE_PC_VINTAGE` is actually Xbox building + `CAR_PC` (`src/core/modeManager.cpp:306-315`).
3. **The shipped PC pipeline is a single-node `D3D9AtomicAllInOne` pipe that installs a render callback and lets the default RW instancing own the streams** (plus a reinstance callback). It does all look work in FFP: stage-0 texture × vertex colour, stage-1 env texgen `CAMERASPACENORMAL`, `MULTIPLYADD(CURRENT + TEX × TFACTOR/DIFFUSE)`, FFP material, force-black for unlit/non-prelit. It never touches Z/cull/fog/alpha-test.
4. **We do not need a new create-pipe function.** The two existing create functions already produce the correct single-node pipes and the correct PC plugin IDs (`RSPIPE_PC_CustomBuilding_PipeID` / `...DN...`). Adding PC-specific create functions would break the mod's live mode switching (one shared pipe object is created once at game init). Reuse them and add a render callback + an instancing branch.
5. **The shipped DN path is CPU interpolation, and the vanilla scheduler is still called.** `hookBuildingPipe` intercepts `0x53C15E` and calls the original `CCustomBuildingRenderer::Update`; only the blend function at `0x5D7200` is patched to `RET`. Restoring the shipped DN path = conditionally **unpatch `0x5D7200`** and feed `geometry->preLitLum` to FFP. Do not port a new scheduler.
6. **Minimum release gate = ranks 1-4 + aliases.** Estimated 4-5.5 developer-days, dominated by the FFP callback (1.5-2 d) and the DN/patch work (0.5-1 d).

---

## 1. Current state (verified)

### 1.1 Live vs missing

| Piece | Status | Evidence (symbol + line at audit time) |
|---|---|---|
| Vehicle PC selection | **LIVE** | `vehiclePipeMap` `src/core/main.cpp:5645-5660` maps `"PC"` → `CAR_PC`; `CAR_PC` enum =1 `src/skygfx.h:171` |
| Vehicle PC draw | **LIVE** | `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch` case `CAR_PC` `src/render/vehiclePipe.cpp:3641-3643` calls the exe trampoline |
| Vehicle exe callback | **LIVE** | `WRAPPER ... EAXJMP(0x5D9900)` `src/rw/gta.cpp:163`; declaration `src/skygfx.h:1298` |
| `BUILDING_PC` enum | **MISSING** | `enum BuildingPipeline` has only PS2/Xbox/GTAIV/PBR `src/skygfx.h:185-193` |
| `buildingPipe=PC` alias | **MISSING** | `buildingPipeMap` `src/core/main.cpp:5634-5642` has PS2/Xbox/GTAIV/PBR/"" only |
| `PIPELINE_PC` | **MISSING** | `enum Pipeline` stops at `PIPELINE_GTAIV` `src/skygfx.h:196-205` |
| PC mode profile | **WRONG** | `kValuesPCVintage` sets `buildingPipe=BUILDING_XBOX`, detail/noise/dual-pass ON `src/core/modeManager.cpp:306-381` |
| Building pipe creation | **shader-only** | `hookBuildingPipe` injects the PS2 create functions at `0x5D7100` / `0x5D7D90`; both install `DNInstance_PS2` + the shader `Switch` callback `src/render/buildingPipe.cpp:3451-3456`, `:3372-3432` |
| PC FFP building callback | **DEAD/BROKEN sketch** | `src/rw/defaultFuncs.cpp:41-116`; no callers; `envData` is a null local dereferenced at `:82/:86`; env ARG2 uses `D3DTA_DIFFUSE` at `:94` (wrong for non-DN) |
| PC DN CPU blend | **DISABLED globally** | `Patch<uint8>(0x5D7200, 0xC3)` `src/render/buildingPipe.cpp:3456` (symbol `hookBuildingPipe`) |
| Shader DN (aap/beta) | **LIVE** | `CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2` `src/render/buildingPipe.cpp:1198`; `DNInstance_PS2` `:3188`; `ps2BuildingVS` expects `COLOR0=NightColor`, `COLOR1=DayColor` (`shaders/vs/ps2/ps2BuildingVS.hlsl:23-24,35`) |
| `0xC02C1C` DN pipeline global | **wired** | `CCustomBuildingDNPipeline__ObjPipeline` `src/render/buildingPipe.cpp:29`; `CCustomBuildingPipeline__ObjPipeline` `:28` |
| `0x5D7F40` atomic detection | **wired** | `CCustomBuildingRenderer__IsCBPCPipelineAttached` `src/render/buildingPipe.cpp:3433-3448`; `normalmap.cpp` also hooks it (`:344`), treating both PC IDs as attached |
| Resource ID for plain stochastic | **NOT A PC GAP** | inventory said "278 free", but `IDR_SIMPLENOISEDETAILPS` is now 278 (`resources/resource.h:138`); do not reuse 278 |

### 1.2 Two source facts that change the implementation

1. **`buildingPipe=PC` is not merely unimplemented — it is silently dropped.** `StrAssoc::get` compares case-insensitively and returns the trailing sentinel on no match (`src/core/main.cpp:5286-5294`). Unknown `"PC"` returns `-1`; the following `if(bpOverride >= 0)` never fires. The user gets whatever `pipeline`/`qualityPreset` set. This is why a **log line is required** in the verification plan: a typo or a mode-owned key must be visible.
2. **The current PC mode is aap's "PC" = Xbox building + `CAR_PC`.** It uses `xboxBuildingVS/PS`, detail maps, noise-detail and dual pass (`src/render/buildingPipe.cpp:1674-2040`, `src/core/modeManager.cpp:306-381`). Selecting `buildingPipe=PC` in aap never produced the shipped PC building look; it produced the Xbox shader path. The real PC FFP path must be written.

---

## 2. Target design (our D3D9 build)

### 2.1 Shipped PC model, restated

- One `RxPipeline`, one node: `RxNodeDefinitionGetD3D9AtomicAllInOne()`.
- Create installs: **default RW instancing**, a reinstance callback, and a render callback. No custom vertex shader declaration.
- Pipeline identity: `pluginId = pluginData = RSPIPE_PC_CustomBuilding_PipeID` (non-DN) or `RSPIPE_PC_CustomBuildingDN_PipeID` (DN), values `0x0253F29C` / `0x0253F298` (`src/rw/gta.h:375-376`).
- Render callback per mesh:
  - stage 0: `MODULATE(TEX, DIFFUSE)`; force-black `SELECTARG2(TFACTOR)` when `!(LIGHTING || PRELIT)`.
  - stage 1 (env materials, pass bit 0): `MULTIPLYADD`, `ARG0=CURRENT`, `ARG1=TEXTURE`, `ARG2=TFACTOR` (non-DN) or `DIFFUSE` (DN), texgen `D3DTSS_TCI_CAMERASPACENORMAL | 1`, `TEXTURETRANSFORMFLAGS=DISABLE`, texture `s1`.
  - stage 2: disabled.
  - material: `RwD3D9SetSurfaceProperties` when `D3DRS_LIGHTING`.
  - vertex alpha: `instancedData->vertexAlpha || material->color.alpha != 255`.
  - draw: `D3D9RenderNotLit` / `D3D9RenderPreLit`.
  - no Z, cull, fog or alpha-test writes.
- DN: CPU interpolation of `preLitLum` by the vanilla scheduler; the DN render callback is the same FFP draw with `ARG2=DIFFUSE`.

### 2.2 Our design decisions

| Decision | Choice | Why |
|---|---|---|
| Create pipe | **Reuse** `CCustomBuildingPipeline__CreateCustomObjPipe_PS2` and `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` (`src/render/buildingPipe.cpp:3372`, `:3403`) | They already create the exact single-node pipe and the correct PC plugin IDs. A separate PC create function would bake a different instance callback into the one shared pipe object and break live mode switching. |
| Render callback | New file-static `buildingPipe_FFPRender(..., bool dn)` inserted before the `Switch`; dispatch from `case BUILDING_PC` | One callback serves non-DN and DN; the only difference is env ARG2. |
| Instancing | **PC branch inside `DNInstance_PS2`** (Option A, recommended) | The pipe object is created once and shared by all modes. `DNInstance_PS2` already builds the RW declaration and reinstance path; for PC it must feed `geometry->preLitLum` to `COLOR0` instead of the plugin night table. |
| Strict-fidelity alternative | Option B: PC create functions that install `RxD3D9AllInOneGetInstanceCallBack(node)` (default RW instancing), no COLOR1 | Literally matches the shipped pipe, but switching PC↔shader modes without a restart leaves the wrong resEntry layout. Cost: +0.5 d and a restart requirement. Not recommended for MVP. |
| Material cache | `RwD3D9SetSurfaceProperties` only; **no** `D3D9SetRenderMaterialProperties` / cache resync | G05's cache resync is vehicle-only (`CustomCarEnvMapPipeline.cpp:1580`). The building PC path never bypasses the RW material cache. |
| Shader IDs | **None** | PC is pure FFP; `CreateShaders()` may still run. No `Resource.rc` / `resource.h` change in MVP. |
| DN scheduler | **Do not port**; unpatch `0x5D7200` and let the vanilla `Update`/`UpdateAtomicPrelitLum` run | The `0x53C15E` intercept already calls the original `CCustomBuildingRenderer::Update`; only the blend function is suppressed. Porting a second scheduler would duplicate and fight the vanilla one. |

### 2.3 Per-mesh state contract

| State | PC FFP callback | Owner |
|---|---|---|
| `ZENABLE` / `ZWRITEENABLE` / `ZFUNC` | **never touched** | `Switch` entry/exit guard; DONT_WRITE branch (Step 3) |
| `CULLMODE` | **never touched** | `pipeEnterCullMode` / `pipeExitCullMode` |
| `FOGENABLE` / `FOGCOLOR` | **never touched** (FFP fog applies to the env add too, as shipped) | `Switch` / global |
| `ALPHATESTENABLE` / `ALPHAFUNC` / `ALPHAREF` | **never touched** | `pipeEnterAlphaMode` / `pipeExitAlphaMode` |
| `ALPHABLENDENABLE` | changed per mesh (`vertexAlpha || material alpha != 255`), L1 + L2 | this callback; `pipeExitAlphaMode` restores |
| `VERTEXALPHAENABLE` | changed per mesh, L1 | this callback; `pipeExitAlphaMode` restores |
| `TEXTUREFACTOR` | env shininess / black; saved and restored at callback exit | this callback |
| stage 1 / stage 2 ops | env setup / disable; `buildingPipe_cleanup()` at exit | this callback |
| stage 0 ops | `D3D9RenderPreLit` / force-black | this callback |
| D3D material | `RwD3D9SetSurfaceProperties` when lighting | this callback |
| textures | `s0` base, `s1` env; `buildingPipe_cleanup()` unbinds `s1` | this callback |
| VS / PS | `NULL`; force `instancedData->vertexShader = NULL` for FFP | this callback |
| texture address | `WRAP` for env; `buildingPipe_saveRenderState` / `restore` | this callback |

### 2.4 Existing code to reuse (do not reinvent)

| Function | File | Use |
|---|---|---|
| `buildingPipe_setupResEntry` | `src/render/buildingPipe.cpp:345` | binds index buffer, streams, declaration |
| `buildingPipe_saveRenderState` / `buildingPipe_restoreRenderState` | `:422` / `:443` | alpha ref/func, blend, fog, ZW, texture address + samplers 0-2 |
| `buildingPipe_cleanup` | `:371` | stage 1/2 disable, texcoord 1, TFF disable; add `RwD3D9SetTexture(NULL, 1)` |
| `D3D9RenderNotLit` | `src/rw/defaultFuncs.cpp:3-12` | FFP draw with PS NULL, VS = `instanceData->vertexShader` |
| `D3D9RenderPreLit` | `src/rw/defaultFuncs.cpp:14-38` | stage 0 modulate + draw |
| `RwD3D9SetSurfaceProperties` | `src/rw/gta.cpp:109` (`0x7FC4D0`) | FFP material |
| `GETENVMAP` | `src/rw/gta.h:324` | env material plugin; use `*GETENVMAP(material)` |
| `CVisibilityPlugins__GetModelInfo` + `CBaseModelInfo::GetDontWriteZ` | `src/render/buildingPipe.cpp:319`, `src/rw/gta.h:78` | DONT_WRITE_ZBUFFER branch |
| `GetPipelineID` | `src/rw/pipeplg.cpp:8` | DN vs non-DN dispatch |
| `DNInstance_PS2` | `src/render/buildingPipe.cpp:3188` | add the PC branch |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` | `src/render/buildingPipe.cpp:3013` | add `case BUILDING_PC` |

**Do not reuse:** the dead building FFP callback as-is (`envData` null, wrong ARG2), `DNInstance_default` as-is (writes night to COLOR0), or the dead vehicle FFP sketch (native `CAR_PC` is exact).

---

## 3. Implementation steps (ordered, checkable)

### Step 0 — Verify what `0x5D7200` is (0.25-0.5 h, LOW risk)

**Why first:** Step 5 (DN CPU blend) depends on this byte being the prelit blender (or the whole DN update). If it is an unrelated helper, unpatching it does nothing or worse.

**Method (static):**
1. Open `gta_sa.exe` in IDA/Ghidra/Cheat Engine; go to `0x5D7200`.
2. Check the prologue and xrefs:
   - references `0x8D12C0` (`CCustomBuildingDNPipeline__m_fDNBalanceParam`) and/or `0xC02C1C` (`CCustomBuildingDNPipeline__ObjPipeline`) → DN update family;
   - calls `RpGeometryLock`/`Unlock` with `rpGEOMETRYLOCKPRELIGHT` and loops over `geometry->preLitLum` → `UpdateAtomicPrelitLum` (the blend);
   - caller is the function that also calls `0x5D7F80` (`UpdateDayNightBalanceParam`) → `Update`.
3. Record the first 16 bytes and the original first byte. Expected original first byte is a push/`sub esp` opcode, **not** `0xC3`. If it is already `0xC3`, this DLL (or another mod) patched it before capture; log a warning.

**Method (runtime, if static is ambiguous):** temporarily replace the unconditional patch with a logging detour that prints the return address and the `atomic` pointer, then calls the original trampoline. The caller's RVA identifies `Update` vs `UpdateAtomicPrelitLum`.

**Exit criteria:** a one-line note in the implementation PR: “`0x5D7200` = `<name>`; original first byte `0x..`; caller `0x......`.” If it is `UpdateAtomicPrelitLum`, the vanilla `Update` still cycles and no extra hook is needed. If it is `Update`, unpatching restores the whole scheduler; still no extra hook.

---

### Step 1 — Mode plumbing: enums, maps, derive, menus, aliases (0.5-0.75 d, MEDIUM risk)

#### 1.1 Enums — `src/skygfx.h:185-205`

```c
enum BuildingPipeline
{
    BUILDING_PS2,
    BUILDING_XBOX,
    BUILDING_GTAIV,
    BUILDING_PBR,
    BUILDING_PC,        // legacy PC FFP building pipe (shipped SA PC)

    NUMBUILDINGPIPES    // = 5
};

enum Pipeline
{
    PIPELINE_PBR,       // 0
    PIPELINE_PS2,       // 1
    PIPELINE_XBOX,      // 2
    PIPELINE_MOBILE,    // 3
    PIPELINE_GTAIV,     // 4
    PIPELINE_PC,        // 5 legacy PC: FFP building + CAR_PC + COLORFILTER_PC

    NUMPIPELINES        // = 6
};
```

Exact values: `BUILDING_PC=4`, `PIPELINE_PC=5`, `CAR_PC=1`, `COLORFILTER_PC=2`.

#### 1.2 `pipeline=` map — `src/core/main.cpp` `pipelineMap` (`:5413-5421`)

Add before the `""` default:

```c
{"PC",     PIPELINE_PC},
```

`pipelineOverride` (`:5424-5426`) range becomes `0..5`:

```c
if(c->pipelineOverride >= 0 && c->pipelineOverride <= 5)
    c->pipeline = c->pipelineOverride;
```

Update the comment and `src/skygfx.h:666` (`0-4` → `0-5`).

#### 1.3 `pipeline` → pipes switch — `src/core/main.cpp:5438-5458`

Add:

```c
case PIPELINE_PC:
    c->buildingPipe = BUILDING_PC;
    c->vehiclePipe  = CAR_PC;
    c->colorFilter  = COLORFILTER_PC;
    break;
```

Also add the same case to `ApplyPipelineFromMenu` in `src/core/debugmenu_ui.cpp:751-779` (before `default`).

#### 1.4 Derive rule — `derivePipelineFromPipes`, `src/core/main.cpp:648-729`

Insert **after** the `BUILDING_PS2` rule and before the fallback (keeps existing rule numbers 1-6 stable; the PBR lock still checks `rule == 3`):

```c
}else if(bp == BUILDING_PC){
    derived = PIPELINE_PC; rule = 7;   // legacy PC family
}else{
    derived = PIPELINE_PBR; rule = 8;  // unknown/out-of-range -> PBR default
}
```

Update the header comment (`:620-635`) with the new rules 7/8. **Do not** add `BUILDING_PC` to rule 2 (`vehicle=GTAIV && building in {PS2,Xbox}`); a PC building + GTAIV vehicle should stay in the PC family, and `PipeFamilyIsIV` already treats PC as non-IV. If a GTAIV vehicle + PC building is a supported combo, document that the vehicle callback still runs `CAR_GTAIV` but the `pipeline` mirror is `PIPELINE_PC`.

> **Why this is mandatory:** without the rule, a `BUILDING_PC` value falls through to rule 8 (`PIPELINE_PBR`), activating every `pipeline == PIPELINE_PBR` gate (colour-filter force, timecycle, PBR postfx). The building renderer keys on `buildingPipe`, but the rest of the frame keys on `pipeline`. This is the single most important non-visual fix.

#### 1.5 `buildingPipe=` alias — `src/core/main.cpp` `buildingPipeMap` (`:5634-5642`)

```c
static StrAssoc buildingPipeMap[] = {
    {"PS2",    BUILDING_PS2},
    {"Xbox",   BUILDING_XBOX},
    {"PCXbox", BUILDING_XBOX},   // legacy aap alias: old "PC" meant Xbox
    {"GTAIV",  BUILDING_GTAIV},
    {"PBR",    BUILDING_PBR},
    {"PC",     BUILDING_PC},
    {"",       -1},
};
```

`StrAssoc::get` is case-insensitive (`strcmpi`), so `pc` works too. Document the semantic change: existing aap configs with `buildingPipe=PC` now get the real PC path; use `PCXbox` for the old look.

#### 1.6 Menu / name arrays (all must match the enum order: PS2, Xbox, GTAIV, PBR, PC)

| File | Symbol | Change |
|---|---|---|
| `src/render/buildingPipe.cpp:824` | `buildingPipeName` | add `case BUILDING_PC: return "PC";` |
| `src/core/debugmenu_ui.cpp:671` | `s_buildNames` | `{ "PS2", "Xbox", "GTAIV", "PBR", "PC", NULL }` |
| `src/core/debugmenu_ui.cpp:232` | `buildPipeStr` | add `"PC"` |
| `src/core/debugmenu_ui.cpp:2940` | receipt line | `< 4` → `< NUMBUILDINGPIPES`; fallback `BUILDING_PBR` |
| `src/core/main.cpp:6718-6720` | `buildPipeNames` in `saveConfigTo` | add `"PC"`; `< 4` → `< NUMBUILDINGPIPES` |
| `src/render/vehiclePipe.cpp:200-204` | `buildPipeNames` init log | add `"PC"`; `< 4` → `< NUMBUILDINGPIPES`; print `shader=FFP` for PC |
| `src/core/menu_inject.cpp:112,125` | `s_pipelineNames`, Pipeline row | add `"PC"`; cycle max `3` → `4` |
| `src/core/debugmenu_ui.cpp:670` | `s_pipeNames` | add `"PC"` at index 5 (enum order PB R, PS2, Xbox, Mobile, GTAIV, PC) |
| `src/core/debugmenu_ui.cpp:973` | `pipelineOverride` row | max `4` → `5` |

Also update `ApplyPipelineOverrideFromMenu` (`src/core/debugmenu_ui.cpp:799-808`) `<= 4` → `<= 5`.

#### 1.7 Mode profile — `src/core/modeManager.cpp` `kValuesPCVintage` (`:306-381`)

> **Compile dependency:** the profile rows below reference `pcDnCpuBlend`, `pcForceBlackUnlit` and `preserveDontWriteZBuffer`. Add the three `Config` fields (Step 5.1) before compiling this edit, or the manager file will fail to compile. The implementation order can be: 5.1 fields → 1.1-1.6 enums/maps → 1.7 profile.

Change the PC mode to the truthful PC triple and disable non-PC building features:

```c
// Pipeline identity + tags
MV_I(pipeline, PIPELINE_PC),
MV_I(buildingPipe, BUILDING_PC),
MV_I(vehiclePipe, CAR_PC),
MV_I(colorFilter, COLORFILTER_PC),
MV_I(tagsBuildingPipe, BUILDING_PC),
MV_IK(preset, "gamePreset", PRESET_SA_PC),
MV_I(ivMode, 0),

// Building — PC has no detail/noise/dual-pass/PS2 modulate
MV_I(ps2ModulateBuilding, 0),
MV_I(detailMaps, 0),
MV_I(noiseDetail, 0),
MV_I(dualPassBuilding, 0),
MV_I(bldEnvReflect, 1),
MV_I(pcDnCpuBlend, 1),
MV_I(pcForceBlackUnlit, 1),
MV_I(preserveDontWriteZBuffer, 1),
MV_I(usePCTimecyc, 1),

// Vehicle / env map
MV_I(dualPassVehicle, 0),
MV_I(envMapSize, 256),
MV_F(envMapFarClipMult, 1.0f),

// Alpha / state
MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
MV_I(ps2ModulateGrass, 0),
MV_IK(dualPassGlobal, "dualPass", 0),
MV_I(dualPassPed, 0),
MV_I(dualPassGrass, 0),
MV_I(dualPassDefault, 0),
...
// PC-only rendering lanes
MV_I(normalMapEnable, 0),
MV_I(normalBufferEnable, 0),
MV_I(pipeChainEnable, 0),
MV_I(treeWind, 0),
MV_I(treeFx, 0),
MV_I(grassInstancing, 0),
// existing postfx rows stay
```

(Only the changed/added rows are shown; keep the existing postfx/safety rows. `pcDnCpuBlend`, `pcForceBlackUnlit`, `preserveDontWriteZBuffer`, `normalMapEnable`, `normalBufferEnable`, `pipeChainEnable`, `treeWind`, `treeFx`, `grassInstancing` must exist as `Config::*` fields — see Step 5.1/5.4.)

**User-visible mode change:** the current `MODE_PC_VINTAGE` deliberately used aap's Xbox building + detail/noise/dual-pass. The shipped PC had none of those. This profile change is the point of the restore, but it will look different to existing PC-mode users; mention it in the release note. Do **not** revive `src/render/pc_patched_mode.cpp` (retired Enhanced-PC stub) or the `PCMode_ApplyDefaults` pattern; `ModeManager` is the single mode owner.

#### 1.8 `qualityPreset` MEDIUM — `src/core/main.cpp:5504-5522`

This is the manual path (no mode). Change:

```c
c->buildingPipe = BUILDING_PC;      // was BUILDING_XBOX
c->vehiclePipe  = CAR_PC;
c->colorFilter  = COLORFILTER_PC;
c->detailMaps       = 0;            // was 1
c->noiseDetail      = 0;
c->dualPassBuilding = 0;            // was 1
c->dualPassVehicle  = 0;            // was 1
```

Leave LOW (PS2) and HIGH/ULTRA (Xbox/PBR) untouched.

#### 1.9 `presets.cpp` (cosmetic only)

`ProcessConfig`/`ApplyPreset` **does not apply** `PresetConfig::buildingPipe/vehiclePipe` (see `src/core/presets.cpp:108-147`; the comments say pipes are mode-owned). Changing `{ "SA PC", ... }` at `:47-48` to `BUILDING_PC` is documentation-consistency only and has no runtime effect. Do it, but do not rely on it. The Style “PC” button only changes colour filter/feature flags; the mode row is the real pipe switch.

**Step 1 exit criteria:**
- `buildingPipe=PC` in an INI without a mode resolves to `BUILDING_PC` and logs it.
- `pipeline=PC` logs 5 and sets the PC triple.
- `buildingPipe=PCXbox` still logs 1 (Xbox) for A/B.
- `pipelineOverride=5` works.
- No array/switch default falls through to PBR for `BUILDING_PC`.

---

### Step 2 — PC FFP render path + PC instancing branch (1.5-2 d, HIGH risk)

#### 2.1 Add the FFP callback — insert before `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` (`src/render/buildingPipe.cpp:3013`)

```c
// ------------------------------------------------------------
// Shipped PC building renderer (FFP).
//
// Contract: mirrors PC/CustomBuildingPipeline.cpp:750-908 and
// PC/CustomBuildingDNPipeline.cpp:1174-1347. The callback owns ONLY
// material/stage/texture-factor/vertex-alpha work; Z, cull, fog and
// alpha-test are owned by the Switch guards. No VS/PS, no shader
// constants, no detail/noise/dual-pass.
//
// dn=false: non-DN pipe, env ARG2 = TFACTOR (shininess*254/255).
// dn=true : DN pipe,     env ARG2 = DIFFUSE (CPU-blended preLitLum).
// ------------------------------------------------------------
static void
buildingPipe_FFPRender(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags, bool dn)
{
    RxD3D9ResEntryHeader *resEntryHeader;
    RxD3D9InstanceData *instancedData;

    buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);

    BuildingRenderState rs;
    buildingPipe_saveRenderState(&rs);

    DWORD savedTFactor = 0;
    RwD3D9GetRenderState(D3DRS_TEXTUREFACTOR, &savedTFactor);

    RwBool lighting = FALSE;
    RwD3D9GetRenderState(D3DRS_LIGHTING, &lighting);
    const bool unlit = !(lighting || (flags & rpGEOMETRYPRELIT));
    const bool forceBlack = unlit && config->pcForceBlackUnlit;

    // PC force-black: unlit AND not prelit draws opaque black, not ambient.
    // Exact PC test: (D3DRS_LIGHTING || flags & PRELIT) else black.
    if(forceBlack){
        RwD3D9SetTexture(NULL, 0);
        RwD3D9SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000);
        RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
    }

    for(int numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
        RpMaterial *material = instancedData->material;

        // FFP: never inherit a shader handle from the previous pipe.
        instancedData->vertexShader = NULL;

        // Reset the env stage for this mesh.
        RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        // Env-mapped material: low bit of surfaceProps.specular is the pass mask.
        if(config->bldEnvReflect && (*(int*)&material->surfaceProps.specular & 1)){
            CustomEnvMapPipeMaterialData *envData = *GETENVMAP(material);
            if(envData && envData->texture){
                // PC source: TFACTOR = shininess * 254/255. The field is
                // int8 in our struct; cast through uint8 so 128..255 do not
                // become negative.
                unsigned shin = (unsigned)(uint8)envData->shininess;
                int tf = (int)((float)shin * 254.0f / 255.0f);
                if(tf < 0) tf = 0;
                if(tf > 255) tf = 255;

                RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
                RwD3D9SetTexture(envData->texture, 1);
                RwD3D9SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_XRGB(tf, tf, tf));

                RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
                RwD3D9SetTextureStageState(1, D3DTSS_COLORARG0, D3DTA_CURRENT);
                RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2,
                    dn ? D3DTA_DIFFUSE : D3DTA_TFACTOR);
                RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX,
                    D3DTSS_TCI_CAMERASPACENORMAL | 1);
                RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            }else if(dbglog_throttle("pcbld_envnull")){
                dbglog("[PCBLD] env pass bit set but envData/texture is NULL (mat=%p)",
                    (void*)material);
            }
        }

        // PC source uses _rwD3D9RenderStateVertexAlphaEnable. We mirror it
        // through L1+L2; the Switch's pipeExitAlphaMode restores both.
        RwBool vtxAlpha = (instancedData->vertexAlpha || material->color.alpha != 255);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)vtxAlpha);
        RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, vtxAlpha ? TRUE : FALSE);

        if(forceBlack){
            D3D9RenderNotLit(resEntryHeader, instancedData);
        }else{
            if(lighting)
                RwD3D9SetSurfaceProperties(&material->surfaceProps, &material->color, flags);
            D3D9RenderPreLit(resEntryHeader, instancedData, (RwUInt8)flags, material->texture);
        }
    }

    // Restore the states this callback owns. Z/cull/fog/alpha-test are
    // intentionally left to the Switch guards.
    RwD3D9SetRenderState(D3DRS_TEXTUREFACTOR, savedTFactor);
    buildingPipe_restoreRenderState(&rs);
    buildingPipe_cleanup();               // stage 1/2 disable + s1 unbind (Step 2.3)
    RwD3D9SetVertexShader(NULL);
    RwD3D9SetPixelShader(NULL);

    if(dbglog_once("pcbld_ffp"))
        dbglog("[PCBLD] FFP cb active pipeID=0x%X dn=%d env=%d forceBlack=%d",
            GetPipelineID((RpAtomic*)object), dn ? 1 : 0,
            config->bldEnvReflect ? 1 : 0, forceBlack ? 1 : 0);
}
```

#### 2.2 Switch dispatch — `src/render/buildingPipe.cpp` `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` (`:3013`), dispatch block `:3097-3116`

Add before `case BUILDING_PS2` (or after `case BUILDING_PBR`):

```c
case BUILDING_PC:
    buildingPipe_FFPRender(repEntry, object, type, flags,
        GetPipelineID((RpAtomic*)object) == RSPIPE_PC_CustomBuildingDN_PipeID);
    break;
```

`default:` must remain `BUILDING_PS2` for out-of-range values. Do not call `pipeUploadMatCol`, `setDnParams`, `buildingPipe_setUVTransform`, `D3D9RenderDual` or any tree-wind helper in the PC path.

#### 2.3 `buildingPipe_cleanup` — add s1 unbind

`src/render/buildingPipe.cpp:371-378`:

```c
RwD3D9SetTexture(NULL, 1);   // env texture leak plug (all building cbs)
RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
...
```

#### 2.4 PC branch in `DNInstance_PS2` — `src/render/buildingPipe.cpp:3188`

The function currently sets `day = GetInstVertexColors(geometry)` (the **plugin day table**) and, for the DN pipe, `night = GetInstExtraColors(geometry)`; the prelit block writes `night → COLOR0`, `day → COLOR1`. For PC FFP we need `preLitLum → COLOR0` (the CPU blend writes it; FFP reads COLOR0).

**After** the existing `day = ...; if(pipeID == DN) night = ...; else night = nil;` block:

```c
// PC FFP: the shipped PC pipe uses default RW instancing, which feeds
// FFP COLOR0 from geometry->preLitLum. The vanilla DN scheduler
// rewrites preLitLum in place, so the plugin day/night tables must NOT
// be used on the PC path. COLOR1 is unused by FFP but is kept defined
// to match the declaration built below.
const bool pcFfp = (config && config->buildingPipe == BUILDING_PC);
if(pcFfp){
    day = geometry->preLitLum;
    night = nil;
    if(day == NULL)
        day = GetInstVertexColors(geometry);
}
```

Replace the prelit copy block (`:3317-3355`) with:

```c
if(isPrelit && (lastLocked & rpGEOMETRYLOCKPRELIGHT)){
    for(i = 0; dcl[i].Usage != D3DDECLUSAGE_COLOR || dcl[i].UsageIndex != 0; ++i)
        ;
    int color1Idx = -1;
    for(j = 0; dcl[j].Stream != 0xFF; ++j){
        if(dcl[j].Usage == D3DDECLUSAGE_COLOR && dcl[j].UsageIndex == 1){
            color1Idx = j;
            break;
        }
    }

    RwRGBA *c0, *c1;
    if(pcFfp){
        c0 = day;        // preLitLum (CPU-blended)
        c1 = day;        // keep COLOR1 defined; FFP ignores it
    }else{
        if(night == NULL)
            night = day;
        c0 = night;      // shader DN: NightColor -> COLOR0
        c1 = day;        // shader DN: DayColor   -> COLOR1
    }
    assert(c0);

    numMeshes = resEntryHeader->numMeshes;
    instData = (RxD3D9InstanceData*)(resEntryHeader+1);
    while(numMeshes--){
        instData->vertexAlpha = _rpD3D9VertexDeclarationInstColor(
            (RwUInt8*)vertexData + dcl[i].Offset +
                resEntryHeader->vertexStream[dcl[i].Stream].stride*instData->minVert,
            c0 + instData->minVert,
            instData->numVertices,
            resEntryHeader->vertexStream[dcl[i].Stream].stride);
        if(color1Idx >= 0){
            RwBool a1 = _rpD3D9VertexDeclarationInstColor(
                (RwUInt8*)vertexData + dcl[color1Idx].Offset +
                    resEntryHeader->vertexStream[dcl[color1Idx].Stream].stride*instData->minVert,
                c1 + instData->minVert,
                instData->numVertices,
                resEntryHeader->vertexStream[dcl[color1Idx].Stream].stride);
            if(!pcFfp)
                instData->vertexAlpha |= a1;
        }
        instData++;
    }
}else if(lastLocked & rpGEOMETRYLOCKPRELIGHT){
    // unchanged: force white
}
```

**Why not Option B (default RW instancing):** the pipe object is created once at game init and shared by all building modes. Option B bakes the PC instance layout into the pipe and breaks live PC↔PS2/Xbox/PBR switching (the shader DN needs `COLOR1=DayColor`). Option A is data-equivalent for FFP (COLOR0 = preLitLum) and keeps switching working. The only cost is an unused COLOR1 stream (4 bytes/vertex) and re-uploading the whole managed buffer when the CPU blend locks prelit (see risk R2-3).

**Step 2 exit criteria:**
- PC building draws with `VS=NULL`, `PS=NULL` (one-shot log or D3D9 query).
- No shader-constant uploads in the PC path.
- Env-mapped building shows a static camera-space-normal reflection; non-env building is texture × prelit, no detail/noise.
- Switching `buildingPipe` PC→Xbox and back live works (instance branch re-evaluates config).
- PS2/Xbox/PBR building modes are byte-identical to before (the PC branch is gated on `config->buildingPipe == BUILDING_PC`).

---

### Step 3 — Force-black + DONT_WRITE_ZBUFFER + state (0.5-0.75 d, MEDIUM risk)

#### 3.1 Config fields (see Step 5.1 for the struct append)

- `pcForceBlackUnlit` (default 1)
- `preserveDontWriteZBuffer` (default 1)

The FFP callback already consumes `pcForceBlackUnlit` (Step 2.1). If 0, `forceBlack` is false and unlit meshes draw normally; useful for A/B.

#### 3.2 DONT_WRITE_ZBUFFER branch — `src/render/buildingPipe.cpp` Switch ZW block (`:3040-3052`)

Replace the unconditional force-TRUE with:

```c
CBaseModelInfo *dontWriteMI = NULL;
bool dontWriteZ = false;
if(config && config->preserveDontWriteZBuffer &&
   config->buildingPipe == BUILDING_PC && type == rpATOMIC && object){
    dontWriteMI = CVisibilityPlugins__GetModelInfo((RpAtomic*)object);
    if(dontWriteMI && dontWriteMI->GetDontWriteZ())
        dontWriteZ = true;
}

if(dontWriteZ){
    // Vanilla RenderEntity: DONT_WRITE_ZBUFFER entities draw with ZW off.
    // Force FALSE through all three layers so a postfx-drifted device cannot
    // keep writing depth for fences/hedges/decals.
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    if(d3d9device)
        d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    if(dbglog_throttle("bld_dontwritez"))
        dbglog("[BUILDING] DONT_WRITE_ZBUFFER model hash=0x%X -> ZW FALSE",
            dontWriteMI ? dontWriteMI->m_hashKey : 0);
}else{
    // existing force-TRUE block, unchanged
}
```

The exit restore (`:3053-3058` currently `savedZWriteRw/Drv/Dev`) already restores each layer to its pre-entry value. Keep it.

**Scope:** gate to `buildingPipe == BUILDING_PC` for MVP. Extending to all modes is a separate bug-fix decision (it changes layering in Xbox/PBR modes). The `normalmap.cpp`/`explicitBuildingPipe` detection already returns true for PC pipe IDs, so this branch sees PC atomics.

#### 3.3 Vehicle twin (optional for MVP, recommended for PC truthfulness)

`src/render/vehiclePipe.cpp` has the same force-TRUE ZW guard (`:3540-3563` in the audit snapshot; symbol `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch`). Add the same `CVisibilityPlugins__GetModelInfo` check gated on `config->vehiclePipe == CAR_PC`. Add a declaration for `CVisibilityPlugins__GetModelInfo` to `src/rw/gta.h` (currently only defined in `buildingPipe.cpp:319`) so `vehiclePipe.cpp` can call it.

**Step 3 exit criteria:**
- An unlit non-prelit prop (debris) renders black with `pcForceBlackUnlit=1`, lit with `0`.
- A fence/hedge/decal with `MODEL_DONT_WRITE_ZBUFFER` does not occlude what is behind it; `[BUILDING] DONT_WRITE_ZBUFFER` log fires once per window.
- No state leak: after a building frame, `ZWRITE`, `ALPHABLENDENABLE`, `SRC/DST`, `CULL`, `FOGCOLOR`, `TFACTOR`, stage1/2 equal the pre-frame values.

---

### Step 4 — DN CPU blend: remove the unconditional `0x5D7200` patch (0.5-1 d, MEDIUM-HIGH risk)

#### 4.1 How the shipped path actually runs

```
0x53C15E (call CCustomBuildingRenderer::Update)  <-- InterceptCall
  └─ orig: UpdateDayNightBalanceParam (0x5D7F80)
     └─ CCustomBuildingDNPipeline::Update(FALSE)   (round-robin, 16 frames)
        └─ UpdateAtomicPrelitLum                  (writes geometry->preLitLum)
```

Our `CustomBuildingPipeline__Update` wrapper calls `orig` (`src/render/buildingPipe.cpp:157-187`), so the scheduler still runs. The only thing stopping the blend is `Patch<uint8>(0x5D7200, 0xC3)` in `hookBuildingPipe` (`:3456`). If Step 0 confirms `0x5D7200` is `UpdateAtomicPrelitLum`, unpatching restores the blend with **no new scheduler code**. If it is `Update`, unpatching restores the whole scheduler. Either way, do not port a second scheduler.

#### 4.2 Conditional patch setter — `src/render/buildingPipe.cpp`, before `hookBuildingPipe`

```c
// -1 = auto (CPU blend only under BUILDING_PC), 0 = off, 1 = on.
// The byte is captured ONCE, before the first patch, so the original
// opcode can be restored exactly. Called from readIniFile(), refreshIni(),
// hookBuildingPipe() and the mode listener.
void
BuildingPipe_UpdateDnPatch(void)
{
    static uint8 s_orig = 0;
    static bool  s_haveOrig = false;
    static int   s_lastWant = -1;

    if(!s_haveOrig){
        s_orig = *(uint8*)0x5D7200;
        s_haveOrig = true;
        dbglog("[PCBLD] 0x5D7200 original first byte = 0x%02X", s_orig);
        if(s_orig == 0xC3)
            dbglog("[PCBLD] WARNING: 0x5D7200 already RET at first capture; "
                   "another patch may own it");
    }

    bool enable = false;
    if(config){
        enable = config->pcDnCpuBlend > 0 ||
                 (config->pcDnCpuBlend < 0 && config->buildingPipe == BUILDING_PC);
    }

    uint8 want = enable ? s_orig : 0xC3;
    if(s_lastWant != (int)want){
        Patch<uint8>(0x5D7200, want);
        s_lastWant = (int)want;
        dbglog("[PCBLD] 0x5D7200 -> 0x%02X (%s) buildingPipe=%d pcDnCpuBlend=%d",
            want, enable ? "interpolation ENABLED" : "interpolation disabled",
            config ? config->buildingPipe : -1,
            config ? config->pcDnCpuBlend : -999);
    }
}
```

Declare it in `src/skygfx.h` next to `hookBuildingPipe` (`:1162`):

```c
void BuildingPipe_UpdateDnPatch(void);
```

#### 4.3 Replace the unconditional patch

`src/render/buildingPipe.cpp` `hookBuildingPipe` (`:3451-3456`):

```c
InjectHook(0x5D7100, CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2);
InjectHook(0x5D7D90, CCustomBuildingPipeline__CreateCustomObjPipe_PS2);
BuildingPipe_UpdateDnPatch();          // was: Patch<uint8>(0x5D7200, 0xC3)
```

#### 4.4 Call sites

| Site | File:line (audit) | Why |
|---|---|---|
| end of `readIniFile` after `[Modes]` apply | `src/core/main.cpp:6617-6628` | startup / F11 reload; final `buildingPipe` known |
| `refreshIni` after `derivePipelineFromPipes(config)` | `src/core/main.cpp:771-782` | config switch / menu writes |
| `ModeListener_OnApply` end | `src/core/main.cpp:7231-7243` | mode row changes `buildingPipe`/`pcDnCpuBlend` live |
| `hookBuildingPipe` | `src/render/buildingPipe.cpp:3451` | covers a startup that skipped refreshIni |

#### 4.5 Verify the DN scheduler sees our pipe

Add a one-shot log (in `CustomBuildingPipeline__Update` after `orig`, or in `BuildingPipe_UpdateDnPatch` after the pipe exists):

```c
if(dbglog_once("pcbld_dnpipe"))
    dbglog("[PCBLD] DN ObjPipeline=%p pluginId=0x%X (expect 0x%X)",
        CCustomBuildingDNPipeline__ObjPipeline,
        CCustomBuildingDNPipeline__ObjPipeline ? CCustomBuildingDNPipeline__ObjPipeline->pluginId : 0,
        RSPIPE_PC_CustomBuildingDN_PipeID);
```

If the global is null or not our pipe, `UsesThisPipeline` skips every atomic and the CPU blend never runs. The game's `CreatePipe` should assign our returned pipe to `0xC02C1C`; verify this before blaming the blend.

**Step 4 exit criteria:**
- `pcDnCpuBlend=1` (or `-1` under PC) logs `0x5D7200 -> <orig> (interpolation ENABLED)`.
- `pcDnCpuBlend=0` logs `-> 0xC3 (interpolation disabled)`.
- At 6:30/20:30 the DN building prelit is partially night and changes over the 16-frame scheduler; at 7:00/21:00 it is full day/night.
- Switching PC→Xbox live re-patches `0x5D7200` to `RET` (the shader DN owns the colors).
- No two owners of `preLitLum`: the shader DN uses plugin tables; the PC DN uses `preLitLum`.

---

### Step 5 — Config fields, INI parse/save, defaults, mode wiring (0.25-0.5 d, LOW-MEDIUM risk)

#### 5.1 Append-only fields — `src/skygfx.h` end of `struct Config` (after `caIntensity`, `:892`; the closing `};` is `:893`)

```c
    // ---- Legacy PC building pipeline (BUILDING_PC) ----
    // pcDnCpuBlend: -1 = auto (CPU blend only under BUILDING_PC), 0 = off
    //   (day-only prelit; debug A/B), 1 = force the shipped CPU blend on.
    int pcDnCpuBlend;
    // pcForceBlackUnlit: 1 = shipped PC force-black for unlit non-prelit meshes.
    int pcForceBlackUnlit;
    // preserveDontWriteZBuffer: 1 = honour CBaseModelInfo::GetDontWriteZ
    //   for BUILDING_PC atomics instead of forcing ZWRITE TRUE.
    int preserveDontWriteZBuffer;
```

**ABI rule:** append only. The first four fields are at pinned offsets (`static_assert` after the struct); the rest of the struct is read by our own modules and the exported `GetConfig()`. Do not insert these in the middle. If a third-party plugin copies `*GetConfig()` by value, `sizeof(Config)` grows by 12 bytes; the safe alternative is TU-static globals + INI keys, but the repository convention is Config fields. Call this out in the PR.

#### 5.2 Parse — `src/core/main.cpp`, after the `bldEnvReflect` block (`:5697-5699`)

```c
c->pcDnCpuBlend = readint(cfg.get("SkyGfx", "pcDnCpuBlend", ""), -1);
if(c->pcDnCpuBlend < -1) c->pcDnCpuBlend = -1;
if(c->pcDnCpuBlend >  1) c->pcDnCpuBlend =  1;

c->pcForceBlackUnlit = readint(cfg.get("SkyGfx", "pcForceBlackUnlit", ""), 1);
if(c->pcForceBlackUnlit < 0) c->pcForceBlackUnlit = 0;
if(c->pcForceBlackUnlit > 1) c->pcForceBlackUnlit = 1;

c->preserveDontWriteZBuffer = readint(cfg.get("SkyGfx", "preserveDontWriteZBuffer", ""), 1);
if(c->preserveDontWriteZBuffer < 0) c->preserveDontWriteZBuffer = 0;
if(c->preserveDontWriteZBuffer > 1) c->preserveDontWriteZBuffer = 1;
```

Placement is after `buildingPipe`/`vehiclePipe`/`derivePipelineFromPipes` (`:5634-5667`), so `-1` auto sees the final `buildingPipe`.

#### 5.3 Save + missing-key writer

- `saveConfigTo` (`src/core/main.cpp:6735-6737` region):

```c
cfg.set("SkyGfx", "pcDnCpuBlend", std::to_string(c->pcDnCpuBlend));
cfg.set("SkyGfx", "pcForceBlackUnlit", std::to_string(c->pcForceBlackUnlit));
cfg.set("SkyGfx", "preserveDontWriteZBuffer", std::to_string(c->preserveDontWriteZBuffer));
```

- `MenuWriteMissingIniKeys` (`:6537` region):

```c
ADD_IF_MISSING("SkyGfx", "pcDnCpuBlend", "-1");
ADD_IF_MISSING("SkyGfx", "pcForceBlackUnlit", "1");
ADD_IF_MISSING("SkyGfx", "preserveDontWriteZBuffer", "1");
```

#### 5.4 Mode profile / menu rows

The mode profile rows are in Step 1.7. If the ImGui menu should expose the three keys, add rows near `bldEnvReflect` (`src/core/debugmenu_ui.cpp:1149`). The local macros are 3-arg (`rW`) and 7-arg with onEdit (`rIo`), not lambdas:

```c
rIo("pcDnCpuBlend", "PC DN CPU blend", &config->pcDnCpuBlend, -1, 1,
    BuildingPipe_UpdateDnPatch,
    "-1 auto (PC only), 0 off (day-only), 1 on. This row re-patches 0x5D7200 immediately.");
rW("pcForceBlackUnlit", "PC force-black unlit", &config->pcForceBlackUnlit);
rW("preserveDontWriteZBuffer", "Honour DONT_WRITE_ZBUFFER", &config->preserveDontWriteZBuffer);
```

(`rIo` expects a `void (*)(void)`; `BuildingPipe_UpdateDnPatch` matches. UI is not required for MVP — INI + mode profile are enough.)

**Step 5 exit criteria:**
- Fresh INI: `pcDnCpuBlend=-1`, `pcForceBlackUnlit=1`, `preserveDontWriteZBuffer=1` are written/read.
- `saveConfig` round-trips all three.
- Mode profile applies all three and the patch state follows on the same frame.

---

## 4. Do-not-restore list

These are the known-buggy/dead parts of the reference tree. Each item is **not** to be copied, with the reason and the substitute.

| # | Reference (GTASource) | Why not | What to do instead |
|---|---|---|---|
| 1 | DN stream size/payload mismatch: `PC/CustomBuildingDNPipeline.cpp:1672-1765` | `GetSize` reports `4 + numVerts*4*2`, write emits `4 + numVerts*4`; every chunk is 4·numVerts short/over → Rw stream desync | Do not serialize DN tables. Use the vanilla plugin (`0x8D12BC` offset) already attached by the game. If serialization is ever needed, write both tables and match `GetSize`, or write one and halve it. |
| 2 | RGBA stride bug: `PC/CustomBuildingPipeline.cpp:496-510` | RGBA branch advances `sizeof(RwRGBA)` (4) but `vertexStream->stride += sizeof(RwV3d)` (12); every attribute after COLOR is at the wrong offset | Never restore the `#if 0` old instance CB. Use `DNInstance_PS2`'s declaration/stride code. |
| 3 | Header-guard collision: `PC/CustomBuildingPipeline.h:10-11` vs `MBR/CustomBuildingPipeline.h:5-6` (`__PCCUSTOMBUILDINGPIPELINE_H__`); `PC/CustomPipelinesCommon.h:8-9` vs `MBR/...:8-9` | Including both variants in one TU silently drops one header | Do not import the reference headers. If copied, use unique platform guards (`__SKYGFX_PC_...`). Never include PC and MBR variants together. |
| 4 | Dead Xbox common: `Xbox/CustomPipelinesCommonXbox.cpp` | D3D8/Xbox state (`D3DDevice_*`, `_rwXbRenderStateForce`); cannot compile against our D3D9/RW 3.6 tree | Use the PC D3D9 helpers; our `D3D9RenderNotLit`/`PreLit`/`RwD3D9SetSurfaceProperties` already cover the live parts. |
| 5 | Dead lighting callback: `PC/CustomBuildingDNPipeline.cpp:990-1044`, attach commented at `:1413` | Would replace RW's lighting callback and change every light contribution; shipped PC did not attach it | Keep RW default lighting. Do not call/recreate `CustomPipeLightingCB`. |
| 6 | `#if 0` variants: old non-reinstancing instance CBs (`PC/CustomBuildingPipeline.cpp:164-383`, `PC/CustomCarEnvMapPipeline.cpp:200-427`), camera-direction spec light (`:1171-1181`), `BLENDFACTORALPHA` env1 (`:1409-1420`), blown-up darkening (`:1537-1551`), debug pass-bit validator (`:1050-1155`), DN dead env branch (`PC/CustomBuildingDNPipeline.cpp:1282-1293`) | All were rejected/disabled in the shipped build; restoring changes the shipped look or resurrects bugs (stride bug, reversed highlight, wrong env brightness) | Use the live PC semantics in Step 2. For the vehicle, keep the native `CAR_PC` trampoline. |
| 7 | `GTA_PC_USE_DN_VS` as-is: `PC/CustomBuildingDNPipeline.cpp:47-50` | Disabled for ATI strictness; needs a matching `COLOR1` stream and `c4/c5` packing; our shader DN already covers the beta path | Target the **shipped** CPU path: `preLitLum` + FFP. If exact beta DN-VS is ever wanted, gate it behind a debug flag and port the full contract; do not make it the default. |
| 8 | PC building env texture matrix: `PC/CustomBuildingPipeline.cpp:829-834` | It builds `envTexMatrix` from `GetfEnvScaleX/Y` but never uploads `D3DTS_TEXTURE1`; FFP `CAMERASPACENORMAL` texgen is the live path. "Fixing" the upload changes reflection tiling and is not PC-faithful | Keep it inert. Do not call `RwD3D9SetTransform(D3DTS_TEXTURE1, ...)` in the PC callback. |
| 9 | Specular mask as a PC feature: `CustomCarEnvMapPipeline.cpp:1983-2055` | The PC car callback never binds `s2`; only the scalar `fSpecularity` is used. The aap `ps2EnvSpecFxPS` mask sampling is an aap extension | Do not add an s2 specular mask to the PC path. |
| 10 | Device-cap hard failure: `CustomCarEnvMapPipeline.cpp:1690-1724` | Returns FALSE / asserts if `MULTIPLYADD`/`BLENDFACTORALPHA`/specular caps are missing; modern D3D9 HW has them; copying turns a driver quirk into a black-screen/no-vehicle failure | Optional one-shot `dbglog` only. Never fail pipe creation on caps. |
| 11 | `rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT` without the CPU blend: `PC/CustomBuildingDNPipeline.cpp:239-244` | The flag exists only because PC rewrites `preLitLum` every frame. Enabling it with the shader DN wastes memory and can leave stale prelight | Only meaningful with `pcDnCpuBlend` on. Do not add the flag manually; the vanilla `CustomPipeAtomicSetup` already owns it for the DN pipe. |
| 12 | Dead `src/rw/defaultFuncs.cpp` building callback as-is (`:41-116`) and `DNInstance_default` (`:126-258`) | The building callback dereferences `envData = nullptr` (`:82/:86`), uses `D3DTA_DIFFUSE` for non-DN env (`:94`), and `DNInstance_default` writes the **night** table to COLOR0 and ignores the day table / CPU blend | Reuse only `D3D9RenderNotLit` (`:3-12`), `D3D9RenderPreLit` (`:14-38`) and `reinstance` (`:118-123`). Write the new callback in Step 2. |
| 13 | Dead vehicle FFP sketch `src/rw/defaultFuncs.cpp:300-466` | Native `CAR_PC` (`0x5D9900`) is exact; the sketch hardcodes a remap-colour list (`:441-448`), has no blown-up quality nuance, and is not wired | Keep the trampoline. Do not replace it with the sketch. |
| 14 | `normalmap.cpp` swapped PC pipe-ID defines (`:32-33`: building=0x53F20098, DN=0x53F2009C, opposite of `gta.h:375-376`) | Both values are checked in `IsCBPCPipelineAttached_hook` (`:137`), so detection works by accident. "Fixing" the defines as part of PC restore risks breaking the normal-map hook | Do not touch in this lane. Separate cleanup: make `normalmap.cpp` include `gta.h` and use the enum names, with a test. |
| 15 | Resource ID 278 for plain stochastic | The inventory's "278 free" is stale: `IDR_SIMPLENOISEDETAILPS` is 278 (`resources/resource.h:138`); 279 is TREEALPHA, 276/277 are underwater/mirror, 280 is reserved | PC needs no new CSO. If G13 is ever done, pick an unused ID (e.g. 281/285 after checking the RC) and add it to `Resource.rc` + `pipelinecommon.cpp`. |

---

## 5. Verification plan

### 5.1 INI the user should use

**Recommended: the mode path.** `[Modes] active=PC` applies the truthful PC profile at the end of `readIniFile` (after every other key):

```ini
[Modes]
active=PC
```

The profile owns `pipeline`, `buildingPipe`, `vehiclePipe`, `colorFilter`, the PC-only flags and the feature switches. Do **not** also set `buildingPipe=PC` in `[SkyGfx]` while the mode is active — mode-owned keys are ignored (`modePipesOwned`), and the log will look confusing.

**Manual path (no mode) — for A/B and bug reports:**

```ini
[SkyGfx]
pipeline=PC
buildingPipe=PC
vehiclePipe=PC
colorFilter=PC
pcDnCpuBlend=-1
pcForceBlackUnlit=1
preserveDontWriteZBuffer=1
bldEnvReflect=1
detailMaps=0
noiseDetail=0
dualPass=0
dualPassBuilding=0
dualPassVehicle=0
ps2Modulate=0
ps2ModulateBuilding=0
envMapSize=256
envMapFarClipMult=1.0
```

**A/B aliases:**

```ini
buildingPipe=PCXbox     ; old aap "PC" = Xbox building (no behavior change)
buildingPipe=Xbox       ; explicit Xbox
pipelineOverride=5      ; force PIPELINE_PC at load (manual path)
```

Warn users in the release note: `buildingPipe=PC` now means the real PC FFP pipe; use `PCXbox` for the old look.

**`qualityPreset` gotcha:** the LOW case (`qualityPreset=0`) writes the PS2 triple (`buildingPipe=BUILDING_PS2`, `vehiclePipe=CAR_PS2`, `colorFilter=COLORFILTER_PS2`) *after* the `pipeline=` map and *before* the explicit `buildingPipe`/`vehiclePipe` overrides (`src/core/main.cpp:5464-5491`, `:5634-5660`). The manual block above survives because it sets the explicit pipe keys, but a config with only `pipeline=PC` + `qualityPreset=0` will be overridden to PS2. Use the default ULTRA, or `qualityPreset=1` (now PC), or the mode path.

### 5.2 Log lines that prove the mode is live

At startup (`skygfx_dbg.log`):

```
Config: pipeline=5 buildingPipe=4 vehiclePipe=1 colorFilter=2 pipelineOverride=-1 ...
[PIPE] buildingPipe=4 (PC) -> shader=FFP
[PCBLD] 0x5D7200 original first byte = 0x..
[PCBLD] 0x5D7200 -> 0x.. (interpolation ENABLED) buildingPipe=4 pcDnCpuBlend=-1
[PCBLD] DN ObjPipeline=0x........ pluginId=0x253F298 (expect 0x253F298)
[PCBLD] FFP cb active pipeID=0x253F29C dn=0 env=1 forceBlack=1
[Mode] active=PC (from INI)
[Mode] engage PC owned=... pipes=5/4/1/2
```

Per-frame/one-shot checks:
- `[PCBLD] FFP cb active ...` must appear once; if it never appears, the Switch did not reach `case BUILDING_PC` (check enum/map/dispatch).
- The PC callback must not bind a PS/VS: add a one-shot `d3d9device->GetPixelShader(&ps); GetVertexShader(&vs);` after the first PC draw and log both pointers (expected `00000000`).
- `[dnbal_repair]` should only fire in cutscenes with vision FX off, as before.
- `[BUILDING] DONT_WRITE_ZBUFFER model hash=...` should fire for fences/hedges/decals, not for ordinary walls.

If `[PCBLD]` is silent but the mode log says `buildingPipe=4`, the likely faults are: (a) `switch` missing `case BUILDING_PC`; (b) the atomic's pipeline is not our pipe (check `0x5D7F40`/`explicitBuildingPipe`); (c) `[Modes] active` applied after the pipe was created and the live instance branch sees a different `config->buildingPipe`.

### 5.3 Visual signature: PC vs Xbox

| Feature | PC FFP (target) | Xbox shader (current PC_VINTAGE) |
|---|---|---|
| Building shader | none; `VS=NULL`, `PS=NULL` | `xboxBuildingVS` / `xboxBuildingPS` |
| Detail maps | ignored even if `detailMaps=1` | used |
| Noise-detail | ignored | used |
| Env reflection | static stage-1 `CAMERASPACENORMAL` add, TFACTOR (non-DN) / DIFFUSE (DN) | shader env UV / `ps2EnvSpecFxPS` |
| DN | CPU `preLitLum` cross-fade (shipped) | two-colour VS interpolate (beta) |
| Unlit non-prelit prop | black | lit/ambient |
| `MODEL_DONT_WRITE_ZBUFFER` | honoured (ZW off) | forced ZW TRUE (regression) |
| Dual pass | single draw | dual pass |
| PS2 modulate | ignored | n/a |

**Test scene:** a MatFX env-mapped building (glass tower), a solid brick wall, a chain-link fence/hedge, a debris prop, and the 6:00-7:00 / 20:00-21:00 clock windows. Take the same camera/screenshot in `buildingPipe=PC` and `buildingPipe=Xbox`; the PC frame must lose detail/noise and show the static env reflection.

### 5.4 State-leak verification

Use the existing `stateBaselineEnable` probes and add a one-shot PC check (temporary debug code, remove before ship):

```c
// after a PC building callback returns, in the Switch exit:
DWORD zw, blend, src, dst, cull, fog, tf;
RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &zw);
RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
RwD3D9GetRenderState(D3DRS_SRCBLEND, &src);
RwD3D9GetRenderState(D3DRS_DESTBLEND, &dst);
RwD3D9GetRenderState(D3DRS_CULLMODE, &cull);
RwD3D9GetRenderState(D3DRS_FOGCOLOR, &fog);
RwD3D9GetRenderState(D3DRS_TEXTUREFACTOR, &tf);
```

Expected after a frame: the same values the frame started with (the Switch guards restore L1/L2/L3; `TFACTOR` is restored by the PC callback). Check stage1/2 `COLOROP/ALPHAOP` are `DISABLE`, `TEXCOORDINDEX=1`, `TEXTURETRANSFORMFLAGS=DISABLE` after the building pass.

### 5.5 A/B matrix (checkable)

| Toggle | Expected |
|---|---|
| `buildingPipe=PC` vs `PCXbox` vs `Xbox` | PC visual signature vs Xbox detail/noise/shader env |
| `pcForceBlackUnlit=1/0` | debris black vs lit |
| `pcDnCpuBlend=1/0` at 6:30 and 20:30 | smooth cross-fade vs day-only prelit |
| `preserveDontWriteZBuffer=1/0` | fence does not occlude vs old force-TRUE occlusion |
| `bldEnvReflect=1/0` | env building reflection on/off; base unchanged |
| `detailMaps=1` under PC | no visual change (PC ignores it) |
| `pcDnCpuBlend` live F11 reload | log re-patches `0x5D7200`; DN look changes without restart |
| `vehiclePipe=PC` | still logs `[PIPE] vehiclePipe=1 (PC)` and draws via `0x5D9900` |
| `buildingPipe=PC` + `vehiclePipe=Modern` | building PC, vehicle PBR; no crash; `pipeline` mirror = 5 |
| PS2 / Xbox / PBR / IV modes | byte-identical to pre-change (PC branch gated on `BUILDING_PC`) |

---

## 6. Effort and risk

| Step | Work | Effort | Risk | Main failure mode / mitigation |
|---|---|---|---|---|
| 0 | Identify `0x5D7200` | 0.25-0.5 h | **LOW** | Wrong function → DN never runs. Mitigate: static xrefs + runtime return-address log; record original byte. |
| 1 | Enums, maps, derive, menus, aliases | 0.5-0.75 d | **MEDIUM** | Enum/menu array mismatch (OOB), `pipeline` fallback to PBR, `buildingPipe=PC` semantics change. Mitigate: update every array/switch; add `PCXbox`; verify `pipeline=5` log. |
| 2 | PC FFP callback + Switch + instance branch | 1.5-2 d | **HIGH** | FFP state leaks, null envData, wrong ARG2, stale VS/PS, wrong COLOR0, alpha/blend. Mitigate: reuse `D3D9RenderPreLit`/`NotLit`, gate on config, A/B toggles, state-leak check. |
| 3 | Force-black + DONT_WRITE + state | 0.5-0.75 d | **MEDIUM** | Wrong lighting/prelit test blackens the world; ZW fix changes layering. Mitigate: exact PC test, gate to `BUILDING_PC`, `pcForceBlackUnlit`/`preserveDontWriteZBuffer` toggles. |
| 4 | DN CPU blend + conditional patch | 0.5-1 d | **MEDIUM-HIGH** | `0x5D7200` target wrong; `preLitLum` not copied; `0xC02C1C` not our pipe; two owners of preLitLum. Mitigate: Step 0, one-shot global log, `pcDnCpuBlend` A/B, verify scheduler at dawn/dusk. |
| 5 | Config/INI/mode wiring | 0.25-0.5 d | **LOW-MEDIUM** | Config ABI append, save/read mismatch, mode ownership. Mitigate: append-only; `ADD_IF_MISSING`; mode profile + F11 round-trip. |
| 6 | Verification / A/B matrix | 0.5 d | **LOW** | False positives from other active lanes. Mitigate: use `PCXbox`/Xbox baseline, one variable at a time. |
| **Total** | | **4-5.5 d** | | Dominated by Step 2; rank 4 (DN) is small because the vanilla scheduler is reused. |

**Release gate:** Steps 0-4 + 5 (config/mode) = the “legacy PC building” feature. Rank 5 (shader-PC vehicle legs G08-G12), rank 6 (G13 stochastic resource), tags (G14), Neo (G15) and dirt levels (G16) are **not** required for a truthful PC building restore.

---

## 7. Open questions / things to confirm before coding

1. **`0x5D7200` identity** — Step 0. Do not write Step 4 until this is recorded.
2. **`CCustomBuildingDNPipeline__ObjPipeline` at `0xC02C1C`** — does the game's `CreatePipe` assign our hooked return value? Log it. If not, the CPU blend scheduler skips every atomic.
3. **`geometry->preLitLum` for all prelit buildings** — if null, the PC branch falls back to the plugin day table (which is not CPU-blended). Add a one-shot log on the fallback.
4. **`envData->shininess` signedness** — our struct uses `int8` (`src/rw/gta.h:193`); the original is an unsigned compressed byte. The Step 2 snippet casts through `uint8`; do not use `GetShininess()` (divides by 255) for the PC TFACTOR.
5. **`RwD3D9SetSurfaceProperties` exactness** — it is the same `0x7FC4D0` RW function the shipped PC uses (`RwSetSurfaceProperties`). Confirm one material (ambient/diffuse/specular/emissive) at day and night; no cache resync is needed for buildings.
6. **`COLOR1` write on the PC path** — we write `preLitLum` to both COLOR0 and COLOR1 to keep the declaration defined. FFP ignores COLOR1. If a future shader path reads COLOR1 under `BUILDING_PC`, it will see day colors, not night; that is intentional for PC.
7. **Config ABI** — appending 3 ints changes `sizeof(Config)`. If a third-party plugin copies the struct by value, move the fields to TU-static globals and keep `Config` frozen. No repository consumer does this today.
8. **`normalmap.cpp` swapped IDs** — leave as-is in this lane; both values are checked. A separate cleanup can include `gta.h` and use the enum names.
9. **`tagsBuildingPipe`** — the mode listener mirrors `buildingPipe`, so tags route to `BUILDING_PC`. The PC callback does not special-case `TagRenderCB`; tags render with the FFP base path. If per-object tag alpha is still required, add the same `material->pipeline == (RxPipeline*)TagRenderCB` intercept used by the shader cbs, but that is not part of the PC restore (PC-48 is dead/optional).
10. **Performance of the managed prelit upload** — `DNInstance_PS2` re-uploads the whole managed vertex buffer when `rpGEOMETRYLOCKPRELIGHT` is set (1/16 of DN atomics per frame). The shipped default instancing uses a dynamic prelit buffer. Functionally correct; if profiling shows a spike, implement Option B behind a restart-required flag.

---

## Appendix A — exact reference facts for the implementation

### A.1 Pipe IDs and enum values

```
BUILDING_PS2    = 0
BUILDING_XBOX   = 1
BUILDING_GTAIV  = 2
BUILDING_PBR    = 3
BUILDING_PC     = 4
NUMBUILDINGPIPES= 5

PIPELINE_PBR    = 0
PIPELINE_PS2    = 1
PIPELINE_XBOX   = 2
PIPELINE_MOBILE = 3
PIPELINE_GTAIV  = 4
PIPELINE_PC     = 5
NUMPIPELINES    = 6

CAR_PC          = 1
COLORFILTER_PC  = 2

RSPIPE_PC_CustomBuilding_PipeID   = 0x0253F29C
RSPIPE_PC_CustomBuildingDN_PipeID = 0x0253F298
```

### A.2 Shipped PC source map (from the decode docs; reference tree was not present at `E:\dev\dave\releases\GTASource` during this verification)

| Behaviour | Reference |
|---|---|
| Non-DN callback | `PC/CustomBuildingPipeline.cpp:750-908` |
| Non-DN create | `:917-957` |
| Non-DN force-black | `:773-786`; `PC/CustomPipelinesCommon.cpp:30-48` |
| Non-DN env | `:824-862` (TFACTOR `:839-854`, texgen `:859-860`) |
| Non-DN cleanup | `:900-903` |
| DN callback | `PC/CustomBuildingDNPipeline.cpp:1174-1347` |
| DN force-black | `:1197-1210` |
| DN env | `:1264-1301` (ARG2 DIFFUSE `:1289-1293`) |
| DN material | `:1322-1324` |
| DN cleanup | `:1338-1342` |
| DN CPU blend | `:1512-1575` (`UpdateAtomicPrelitLum`) |
| DN scheduler | `:275-318`, `:1497-1504`, `DN_NUM_FRAMES_FOR_PROCESS=16` `:68` |
| DN plugin | `:1597-1620`; layout `CustomBuildingDNPipeline.h:33-39` |
| Draw helpers | `PC/CustomPipelinesCommon.cpp:58-125` (default), `:153-262` (surface props) |
| Create pattern | `RxNodeDefinitionGetD3D9AtomicAllInOne` + `RxLockedPipeAddFragment` + `RxD3D9AllInOneSetRenderCallBack` (`:924-947`) |

> The GTASource root cited by the inventory (`E:\dev\dave\releases\GTASource\gtasa_src-main`) was **missing** during this verification. The reference line numbers above come from `docs/plans/2026-10-24-gtasource-pipes.md` and `docs/plans/2026-10-24-legacy-pc-feature-inventory.md`, which were verified against that tree earlier. All current-tree anchors were verified directly.

### A.3 Current-tree insertion checklist (one line per edit)

- [ ] `src/skygfx.h:185-193` — add `BUILDING_PC=4`, update `NUMBUILDINGPIPES`.
- [ ] `src/skygfx.h:196-205` — add `PIPELINE_PC=5`, update `NUMPIPELINES`.
- [ ] `src/skygfx.h:666` — `0-4` → `0-5`.
- [ ] `src/skygfx.h` end of `Config` — append 3 int fields.
- [ ] `src/skygfx.h:1162` — declare `BuildingPipe_UpdateDnPatch`.
- [ ] `src/core/main.cpp:648-729` — add BUILDING_PC derive rule; update header comment.
- [ ] `src/core/main.cpp:5413-5421` — `pipelineMap` add `"PC"`.
- [ ] `src/core/main.cpp:5424-5426` — override range `<=5`.
- [ ] `src/core/main.cpp:5438-5458` — `case PIPELINE_PC`.
- [ ] `src/core/main.cpp:5504-5522` — MEDIUM preset → PC, detail/dual-pass off.
- [ ] `src/core/main.cpp:5634-5642` — `buildingPipeMap` add `PC`, `PCXbox`.
- [ ] `src/core/main.cpp:5697-5699` — parse 3 PC fields.
- [ ] `src/core/main.cpp:6617-6628` — call `BuildingPipe_UpdateDnPatch()` after `[Modes]` apply.
- [ ] `src/core/main.cpp:771-782` — call it in `refreshIni`.
- [ ] `src/core/main.cpp:7231-7243` — call it in `ModeListener_OnApply`.
- [ ] `src/core/main.cpp:6718-6720` — `buildPipeNames` add PC, bound.
- [ ] `src/core/main.cpp:6735-6737` — save 3 PC fields.
- [ ] `src/core/main.cpp:6537` — `ADD_IF_MISSING` 3 PC fields.
- [ ] `src/render/buildingPipe.cpp:371-378` — `buildingPipe_cleanup` unbind s1.
- [ ] `src/render/buildingPipe.cpp:824` — `buildingPipeName` add PC.
- [ ] `src/render/buildingPipe.cpp` before `:3013` — add `buildingPipe_FFPRender`.
- [ ] `src/render/buildingPipe.cpp:3097-3116` — add `case BUILDING_PC`.
- [ ] `src/render/buildingPipe.cpp:3188` — `DNInstance_PS2` PC branch.
- [ ] `src/render/buildingPipe.cpp:3277-3355` — prelit copy c0/c1 restructure.
- [ ] `src/render/buildingPipe.cpp:3451-3456` — replace unconditional patch with `BuildingPipe_UpdateDnPatch()`.
- [ ] `src/core/debugmenu_ui.cpp:232` — `buildPipeStr` add PC.
- [ ] `src/core/debugmenu_ui.cpp:670-672` — `s_pipeNames`/`s_buildNames` add PC.
- [ ] `src/core/debugmenu_ui.cpp:751-779` — `ApplyPipelineFromMenu` case PC.
- [ ] `src/core/debugmenu_ui.cpp:799-808` — override `<=5`.
- [ ] `src/core/debugmenu_ui.cpp:973` — row max 5.
- [ ] `src/core/debugmenu_ui.cpp:2940` — bound `NUMBUILDINGPIPES`.
- [ ] `src/core/modeManager.cpp:306-381` — PC profile truthful values.
- [ ] `src/core/menu_inject.cpp:112,125` — names + max 4.
- [ ] `src/render/vehiclePipe.cpp:200-204` — names + bound.
- [ ] `src/rw/gta.h` — declare `CVisibilityPlugins__GetModelInfo` (for the optional vehicle ZW fix).
- [ ] `src/presets.cpp:47-48` — SA PC building constant (cosmetic; `ApplyPreset` ignores it).

---

## Appendix B — “Why not just set `buildingPipe=PC` today?”

Because the key is **not mapped**. `StrAssoc::get` returns the `""` sentinel `-1` on an unknown key (`src/core/main.cpp:5286-5294`), and the following guard `if(bpOverride >= 0)` never fires (`:5641-5642`). The user keeps whatever `pipeline`/`qualityPreset` selected. The only visible effect of `buildingPipe=PC` in the current build is a silent no-op. Step 1 is what makes the mode selectable at all.

The second reason: even if the enum existed, a `BUILDING_PC` value without the `derivePipelineFromPipes` rule would fall through to `PIPELINE_PBR` (`src/core/main.cpp:679-681`), activating PBR-only gates (colour-filter force, timecycle path, PBR postfx). The building look would be PC while the frame post-processing is PBR — a subtle, confusing mixed mode. The derive rule in Step 1.4 is not optional.
