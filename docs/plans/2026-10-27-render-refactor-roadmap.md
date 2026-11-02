# Render-folder consolidation & fortification roadmap

**Date:** 2026-10-27
**Scope:** `src/render/` (53 files, ~51,345 lines measured with `wc -l`)
**Deliverable of this doc:** exactly one plan. **No source edits, no builds** were performed
to produce it. All numbers below come from read-only inspection (`wc -l`, `file`, `grep`,
targeted `read`) of the working tree on branch `very-experimental`, commit `898e894`.

**The "fortify" pattern being matched** (as established by `src/texdefs.cpp`,
`src/core/debug_handl.cpp`, `src/entities/sftbdyphsx.cpp`):

1. Verbatim body concatenation — function bodies are copied **byte-for-byte**; nothing is
   wrapped, reordered across function boundaries, or rewritten.
2. One prologue comment listing the merged TUs **in concatenation order** with the reason
   order matters (e.g. `debug_handl.cpp`: "the crashhandler static installer must run after
   the diag_* statics above it"; `sftbdyphsx.cpp`: "the dismemberment address-macros MUST
   stay below the ragdoll extern-C block").
3. `// ---- Includes (deduped union of the N TUs) ----` block: strict **union**, original
   relative order preserved, each surviving `#include` annotated with which TU needed it.
4. `// ==== ... (was src/path/file.cpp) ====` section banners per merged TU.
5. Build-file swap in **both** `build/skygfx.vcxproj` (+ `.filters`) **and** `premake5.lua`
   (the premake script is a documented mirror; see its header).
6. Backup-then-delete to `_backup/<name>_merge_YYYY-MM-DD/` (convention from
   `_backup/body_merge_2026-10-26/`, `_backup/core_merge_2026-10-26/`), then delete the
   originals from `src/`.
7. `cl /Zs` syntax-only verification of the merged TU before any link.

---

## 0. Hard constraints carried into every stage

| # | Constraint | Enforcement |
|---|------------|-------------|
| C1 | **`postfx.cpp` (4818 ln) is never merged into anything.** SEH-heavy (27 `__try`), it is the chain orchestrator; 12 TUs reach it through `postfx.h`. | Excluded from every merge family by name. |
| C2 | **Render-state doctrine lives in `pipelinecommon.cpp`.** Blend/state helpers go there; `postfx_utils.cpp::RestoreOneRawGeomState` already delegates to `pipeRwParkSet/pipeL2ParkSet/pipeL3Set` (pipelinecommon.cpp:1135-1155, decl `skygfx.h:1344-1346`). | New primitives are added **only** to `pipelinecommon.cpp`; consumers become thin wrappers. |
| C3 | **SEH discipline.** MSVC C2702 (`__except` inside `__finally`) and C2712 (class-with-destructor local in a `__try` function). | See §2.0 — the safety argument for verbatim concatenation. |
| C4 | **Per-file line endings.** Do not silently normalise. | Every new TU declares its adopted convention (§2). |
| C5 | **LNK4042.** Two `ClCompile` rows producing the same `.obj` basename abort the link. | Basename-uniqueness scan is a per-stage gate (§6). |
| C6 | **Two merges are in flight in another lane — do NOT re-plan them:** `neo.cpp + neoCarpipe.cpp + neoWaterdrops.cpp → neopipe.cpp/.h`; `PC_GrassRenderer.cpp + PC_GrassInstancer.cpp + PC_PlantsMgr.h → grasssys.cpp/.h`; deletion of the 6 dead `*_mode.cpp` stubs. | All stages below are **gated on that lane landing** (it writes `build/skygfx.vcxproj`, `premake5.lua`, and 9 source files). |
| C7 | **A third lane is in flight** on the low-risk `blendToD3D`/`pipeBlendHasRwTwin` dedupe in `pipelinecommon.cpp`/`buildingPipe.cpp`/`vehiclePipe.cpp`/`src/core/main.cpp` + deletion of two `#if 0` blocks. | Stages touching those four files are **gated on that lane landing**. Never run against it. |

### 2.0 The SEH safety argument (C3)

Verbatim concatenation cannot introduce C2702/C2712, and here is exactly why:

* **C2702** requires an `__except` *token* to appear inside a `__finally` body. That can only
  happen if a function body is edited. Bodies are verbatim ⇒ impossible. The pattern that
  keeps it impossible is already in-tree and must be preserved on merge:
  `mirrorPass.cpp:141-150` `MirrorCompositeSaved` POD payload + a free function carrying the
  `__try/__except`, with `__finally` only **calling** it (the file comments this explicitly).
  Measured `__finally` census: `envmap.cpp` 14, `mirrorPass.cpp` 8, **everywhere else 0**.
  So C2702 is a live concern **only** in `envmap.cpp`/`mirrorPass.cpp`.
* **C2712** is a per-function property: a function with `__try` may not have locals of class
  type with a destructor. Bodies verbatim ⇒ each function keeps exactly its current locals.
  The **one** way concatenation could break it is if include-deduping makes a previously
  incomplete/POD type a full class **inside a `__try` function's locals**. Hence the rule:
  **the deduped include block is a strict UNION — never add a header a merged TU did not
  already `#include`.**
* **Static-initialisation order** is the third (usually forgotten) concatenation hazard.
  Verified: `postfx_luminance.cpp`, `postfx_motionblur.cpp`, `postfx_utils.cpp` have **zero**
  dynamic file-scope initialisers (only `static const D3DRENDERSTATETYPE s_rawGeomStateIds[9]`,
  a constant aggregate). Merging them cannot reorder any dynamic init.

---

## 1. Inventory of `src/render/` (post-in-flight state)

Line counts are `wc -l` on the working tree. "SEH" = count of `__try`/`__except`/`__finally`
tokens. "EOL" = `file(1)` result. "Consumers" = TUs that `#include` the paired header (or,
for headerless TUs, the reachability note).

### 1a. Render passes & pipes (never merged away — each is a distinct subsystem)

| File | Lines | Role | SEH | EOL | Consumers |
|---|---|---|---|---|---|
| `postfx.cpp` | 4818 | PostFX chain orchestrator. **C1: never merge.** | 41 | CRLF | `postfx.h` ×12 |
| `vehiclePipe.cpp` | 3887 | Vehicle render callbacks (PS2/Xbox/PBR/modern/Leeds). Hooked at `0x5D9FE9`. | 0 | CRLF | via `texalpha.h`, `neo.h`, `profiler.h`; entry from `main.cpp` hooks |
| `buildingPipe.cpp` | 3577 | Building render callbacks (PS2/Xbox/PBR). Hooked at `0x5D7100`. | 5 | CRLF | `texalpha.h`, `treeWind.h`, `profiler.h`; entry from `main.cpp` hooks |
| `pipelinecommon.cpp` | 2112 | **Doctrine TU.** Matrix/light uploads, `CreateShaders`, `pipeUploadPBR`, L1/L2/L3 write primitives. | 7 | CRLF | decls in `skygfx.h` (global) |
| `forwardplus.cpp` | 1946 | GPU tile light-culling + light atlas upload (`c45` contract). **Not telemetry** — real render infra. | 0 | CRLF | `forwardplus.h` ×2 (`main.cpp`, self) |
| `waterPipe.cpp` | 1762 | Water render callback (parallax, fresnel, wavebank). | 8 | CRLF | `waterPipe.h` ×1 (`main.cpp`) |
| `envmap.cpp` | 1250 | Env-map/reflection pass (`CRenderer::RenderRoadsAndBuildings` mid-frame 512² reflection). | 30 (16 `__try`, **14 `__finally`**) | CRLF | headerless; `neo.h`, `profiler.h`; reached via renderer hook |
| `depthhook.cpp` | 618 | INTZ sampleable depth-stencil + `Reset`/`SetDepthStencilSurface` vtable hooks. Device-lifecycle, **not a render pass**. | 0 | CRLF | `depthhook.h` ×7 |
| `mirrorPass.cpp` | 577 | Mirror composite overdraw (`Mirror_Composite.ps`), CMirrors hook. | 16 (6 `__try`, **8 `__finally`**) | **LF** | `mirrorPass.h` ×1 (self only) |
| `texalpha.cpp` | 962 | Per-`RwRaster*` alpha classification feeding the PBR pipes' blend + `c48.w`. | 4 | **LF** | `texalpha.h` ×3 |
| `treeWind.cpp` | 476 | Wind vertex-shader animation. | 0 | **LF** | `treeWind.h` ×4 |
| `noise.cpp` | 959 | **`SkyNoise`** universal noise service (CPU generators + packed RGBA tile, bound via `pipeBindNoise`, pipelinecommon.cpp:2099). | 0 | **LF** | `noise.h` ×3 |
| `utilitynoise.cpp` | 251 | Legacy utility-noise tile generator. **Superseded** — see §2.4. | 0 | CRLF | headerless (globals in `skygfx.h:968-970`) |

### 1b. PostFX family (9 TUs)

| File | Lines | Role | SEH | EOL | Consumers |
|---|---|---|---|---|---|
| `postfx.cpp` | 4818 | Orchestrator (see above). | 41 | CRLF | ×12 |
| `postfx_colourfilter.cpp` | 1911 | Colour grade / IV grade / timecycle (`DrawIVGrade`). | 1 | **LF** | via `postfx.h` |
| `postfx_smaa.cpp` | 1434 | SMAA + temporal. **Drags `AreaTex.h` (14,980 ln) + `SearchTex.h` (132 ln) lookup data.** | 6 | **LF** | via `postfx.h` |
| `postfx_ssao.cpp` | 1318 | SSAO overhaul (quarter-res, blur, history). | 20 | **LF** | `depthhook.h`, `postfx.h` |
| `postfx_newfx.cpp` | 874 | Sun shafts / DOF / bloom / chroma / grain / vignette / FXAA3. | 0 | **LF** | via `postfx.h` |
| `postfx_underwater.cpp` | 550 | Underwater fog + SSS. **Owns `postfx_underwater.h`.** | 6 | **LF** | `postfx_underwater.h` ×3 |
| `postfx_utils.cpp` | 338 | PostFX state/raster toolkit: `Save/RestoreRawGeomStates`, `TrackRaster`, `GetScreenSize`, `SetupFullscreenQuad`. Delegates to `pipeRwParkSet/pipeL2ParkSet/pipeL3Set`. | 0 | **LF** | via `postfx.h` |
| `postfx_motionblur.cpp` | 302 | Velocity buffer + motion-blur draw. | 6 | **LF** | `depthhook.h`, `postfx.h` |
| `postfx_luminance.cpp` | 290 | Exposure measurement + A/B adaptation targets. | **0** | **LF** | via `postfx.h` |

All 8 `postfx_*.cpp` (excluding `postfx.cpp`) are **symbol-safe to concatenate**: zero
duplicate `static` identifiers (136 statics parsed, cross-file duplicate set empty), zero
duplicate `#define`/`typedef`/`enum` names, and the only repeated non-static name is
`makePS(int,void**)` — a *forward declaration* in three files (`postfx_colourfilter:11`,
`postfx_newfx:29`, `postfx_underwater:70`; definition at `pipelinecommon.cpp:658`), harmless
when duplicated in one TU. The single `static` collision in the family is
`PostfxMenuPauseOnly` (defined in **both** `postfx.cpp:130` and `postfx_colourfilter.cpp:85`)
— and `postfx.cpp` is excluded by C1, so it never bites.

### 1c. Diagnostics / support

| File | Lines | Role | SEH | EOL | Consumers |
|---|---|---|---|---|---|
| `profiler.cpp` | 662 | `PF_SCOPE` CPU/GPU profiler (`PfFrame/PfDraw/PfBind*`). | 0 | CRLF | `profiler.h` ×16 |
| `renderstate_monitor.cpp` | 488 | Three-layer (L1/L2/L3) render-state divergence monitor, 11 states. | 3 | **LF** | `renderstate_monitor.h` ×2 (`main.cpp`, self) |

### 1d. Data headers (no build rows)

| File | Lines | Role | EOL |
|---|---|---|---|
| `AreaTex.h` | 14,980 | SMAA area lookup table, included only by `postfx_smaa.cpp:148`. Not in `ClInclude`. | CRLF |
| `SearchTex.h` | 132 | SMAA search lookup table, included only by `postfx_smaa.cpp:149`. Not in `ClInclude`. | CRLF |

### 1e. In flight elsewhere (C6 — excluded from this plan)

`neo.cpp` (209) + `neoCarpipe.cpp` (812) + `neoWaterdrops.cpp` (810) → `neopipe.cpp/.h`;
`PC_GrassRenderer.cpp` (637) + `PC_GrassInstancer.cpp` (478) + `PC_PlantsMgr.h` (386) →
`grasssys.cpp/.h`; `custom_mode.cpp`, `iv_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`,
`ps2_mode.cpp`, `xbox_mode.cpp` (51-57 ln comment-only stubs) deleted.

### 1f. Line-ending map (authoritative, measured)

**CRLF:** `buildingPipe`, `depthhook`, `envmap`, `forwardplus`, `pipelinecommon`, `postfx`,
`profiler`, `utilitynoise`, `vehiclePipe`, `waterPipe`, `AreaTex.h`, `SearchTex.h`,
`forwardplus.h`, `neo.h`, `PC_GrassRenderer.h`, `PC_PlantsMgr.h`, `postfx.h`, `waterPipe.h`.
**LF:** `mirrorPass`, `noise`, `postfx_colourfilter`, `postfx_luminance`, `postfx_motionblur`,
`postfx_newfx`, `postfx_smaa`, `postfx_ssao`, `postfx_underwater`, `postfx_utils`,
`renderstate_monitor`, `texalpha`, `treeWind`, `PC_GrassInstancer.cpp` (**mixed-file
exception: its `.h` is LF too**), + `depthhook.h`, `mirrorPass.h`, `noise.h`,
`postfx_underwater.h`, `profiler.h`, `renderstate_monitor.h`, `texalpha.h`, `treeWind.h`,
`PC_GrassInstancer.h`.

Note two corrections to the assumed map: `forwardplus.cpp` is **CRLF** (confirmed), and
`PC_GrassInstancer.cpp` is **LF** while its sibling `PC_GrassRenderer.cpp` is CRLF (relevant
only to the in-flight grass merge — that lane must pick one).

---

## 2. Merge-candidate families — feasibility, risk, verdict

### 2.0 Summary verdict table

| Family | Files | Merged size | Feasibility | Risk | Verdict |
|---|---|---|---|---|---|
| PostFX support layer | `postfx_luminance` + `postfx_motionblur` + `postfx_utils` | **~970 ln** | Trivial | Very low | ✅ **MERGE** → `postfx_support.cpp` |
| PostFX effects (all 8) | `colourfilter,smaa,ssao,newfx,underwater,utils,motionblur,luminance` | ~22,200 ln (incl. 15,112 data) | Trivial | **High (bloat)** | ❌ **REJECT** |
| PostFX effects (5 passes) | `colourfilter,smaa,ssao,newfx,underwater` | ~21,250 ln (incl. data) | Trivial | **High (bloat)** | ❌ **REJECT** |
| State-heavy | `waterPipe,envmap,mirrorPass,depthhook` | ~4,250 ln | Moderate | **High** | ❌ **REJECT** (do the helper extraction instead) |
| Telemetry trio | `forwardplus,profiler,renderstate_monitor` | ~3,140 ln | Trivial | Medium | ❌ **REJECT** (false family) |
| Diagnostics pair | `profiler,renderstate_monitor` | ~1,190 ln | Trivial | Low | ❌ **REJECT** (no value) |
| Asset-infra trio | `texalpha,noise,utilitynoise` | ~2,210 ln | Moderate | **High** | ❌ **REJECT** (`utilitynoise` is a *superseded duplicate*, not a sibling) |

Only **one** merge survives review. That is the honest result: `src/render/` is already at a
defensible granularity — one TU per shader family. The real defects in the folder are not
"too many files" but **duplicated render-state idioms** (§3), which consolidation would
*hide* rather than fix.

---

### 2.1 The 9-file `postfx_*` family

**What must stay separate:**

| TU | Why it stays |
|---|---|
| `postfx.cpp` | **C1.** 27 `__try`, orchestrates the chain, 12 consumers. Also owns `PostfxMenuPauseOnly` (collides with `postfx_colourfilter`). |
| `postfx_smaa.cpp` | `#include "AreaTex.h"` (14,980 ln) at line 148 and `"SearchTex.h"` at 149. Any TU it joins gains 15,112 lines of lookup data, pushing the merged file past ~21k lines — a direct violation of this repo's token-efficiency constraint ("hits context limits extremely fast"). Keep the data isolated in the one TU that reads it. |
| `postfx_colourfilter.cpp` | 1,911 ln, own shader family (`ivGradePS` statics at :1389-1408), own timecycle statics (:1736-1775). Self-contained; merging gains one `ClCompile` row and costs a 2.8k-line blast radius. |
| `postfx_ssao.cpp` | 1,318 ln, **20 SEH sites**, own quarter-res raster family + history validity latch. Biggest crash-diagnosis surface in the family — keep it attributable by file. |
| `postfx_newfx.cpp` | 874 ln, seven distinct effects behind one `FxShaders s_fx` bundle. Cohesive internally, unrelated to the others. |
| `postfx_underwater.cpp` | 550 ln + **its own header** (`postfx_underwater.h`, 3 consumers). Folding a TU that owns a public header into a sibling forces a header rename for zero gain. |

**What can safely combine (and why):**

`postfx_luminance.cpp` (290) + `postfx_motionblur.cpp` (302) + `postfx_utils.cpp` (338) =
**930 ln of bodies → `postfx_support.cpp` ≈ 970 ln** including prologue + banners + deduped
includes.

* These three are the **support layer**, as opposed to the five **effect passes** above:
  luminance = temporal exposure/adaptation targets; motionblur = temporal velocity buffer;
  utils = the shared state/raster toolkit both rely on. The boundary "effects vs support" is
  the only non-arbitrary cut in the family.
* Verified symbol-safe: 0 duplicate statics / macros / typedefs / enums across the three.
* Verified SEH-safe (§2.0): `postfx_luminance` has **0** SEH tokens, `postfx_utils` **0**,
  `postfx_motionblur` 4 `__try` and **0** `__finally` ⇒ C2702 is structurally impossible in
  the merged TU and C2712 cannot arise (verbatim bodies).
* Verified static-init-safe: no dynamic file-scope initialisers in any of the three.
* All three are **LF** ⇒ the merged TU adopts **LF** (no mixed-ending file is created).
* All exports are already declared in `postfx.h` (`RunFrameExposure`, `ReleaseLuminanceTargets`,
  `DrawVelocityBuffer`, `ReleaseVelocityBufferResources`, `TrackRaster`, `UntrackRaster`,
  `RasterEnsureSurfaceReady`, `CaptureScreenSize`, `GetScreenSize`, `IsGameInMenuOrPaused`,
  `SetupFullscreenQuad`, `SaveRawGeomStates`, `RestoreRawGeomStates`) ⇒ **`postfx.h` needs no
  edit**. `RestoreOneRawGeomState` is `static` (postfx_utils.cpp:227-228) and stays internal.
* Include union is tiny and near-identical: `skygfx.h`, `debug_handl.h`, `postfx.h`,
  `profiler.h` (+ `depthhook.h` from motionblur). 5 lines.
* Basename `postfx_support` is unique in the tree ⇒ no LNK4042 (C5).
* **Line-ending convention adopted: LF** (matches all three sources).

Estimated result: **`src/render/postfx_support.cpp` ≈ 970 lines**; `src/render/` drops from
53 to 51 files.

**Rejected alternative, recorded for the audit trail:** merging the 8 `postfx_*` into a single
`postfx_ext.cpp` is *symbol-feasible* (verified) but produces ~22,200 lines once `AreaTex.h`
lands in it. That is the single worst outcome available for this repo's context budget.

---

### 2.2 State-heavy group: `waterPipe` / `envmap` / `mirrorPass` / `depthhook`

**Verdict: DO NOT MERGE. More risk than value.** Estimated merge = 1,762 + 1,250 + 577 + 618
= **~4,250 lines**, of which 54 of the 64 SEH tokens in the folder's non-postfx set.

Reasons, in order of weight:

1. **C2702 concentration.** `envmap.cpp` has 14 `__finally` and `mirrorPass.cpp` has 8; every
   other candidate has 0. These two TUs carry the POD-payload + free-function SEH pattern
   (`MirrorCompositeSaved`, `mirrorPass.cpp:141-150`). Concatenating them into one TU does not
   *break* the rule, but it puts the only two rule-sensitive regions in the same file where a
   future edit can reach both. The blast radius of a C2702 mistake becomes "the whole
   reflection+mirror subsystem", not "mirrorPass".
2. **`depthhook.cpp` is not a render pass.** It installs vtable hooks on
   `SetDepthStencilSurface`/`Reset` and owns device-lifetime (INTZ) resources. Merging device
   lifecycle into a render-callback TU inverts the folder's own layering and makes the
   `Reset` ordering contract (`ReleaseDeviceObjects` pre-Reset / `OnDeviceReset` post-Reset,
   shared with `SkyNoise`) harder to audit.
3. **Mixed line endings.** `waterPipe` CRLF, `envmap` CRLF, `depthhook` CRLF, but
   `mirrorPass` **LF**. Any merged TU must normalise one file's convention — a gratuitous
   whole-file diff on a 16-SEH-file.
4. **Four subsystems, four shader families, four resource lifetimes.** The only thing they
   actually share is the park-and-set render-state idiom — and that is a **code** duplication
   (§3.2), not a file-organisation problem. Merging files would bury the duplication instead
   of deleting it.

**The lighter cleanup instead (this is the real win):** make `envmap.cpp::envParkSetState3L`
(:371-385) and `mirrorPass.cpp::MirrorRestoreState3L` (:116-130) thin wrappers over the
canonical `pipeRwParkSet/pipeL2ParkSet/pipeL3Set` (pipelinecommon.cpp:1135-1155), the way
`postfx_utils.cpp::RestoreOneRawGeomState` already does. See §3.2 / Stage 2.

---

### 2.3 "Telemetry group": `forwardplus` / `profiler` / `renderstate_monitor`

**Verdict: false family. REJECT both the trio and the pair.**

* `forwardplus.cpp` (1,946 ln) is **not telemetry** — it is a GPU tile light-culling system
  with a hard shader register contract (`c45`, `s6`/`s7` light atlases) consumed by
  `VehiclePBR_Modern.hlsl`. Folding a real render subsystem into diagnostics is wrong in
  both directions. It stays alone.
* `profiler.cpp` (662) + `renderstate_monitor.cpp` (488) **is** symbol-safe to merge
  (0 duplicate statics/macros/globals) → ~1,190 lines. But the value is **one `ClCompile`
  row**. Against that: `profiler.h` is included by **16 TUs** and is the hot-path
  instrumentation gate (`PF_SCOPE`); `renderstate_monitor.h` is included by **2** and is an
  opt-in diagnostic. Keeping them separate preserves the ability to compile the monitor out
  and keeps the hot-path header free of monitor decls.
* Also note `renderstate_monitor.cpp` is **LF**, `profiler.cpp` **CRLF** — a mixed merge for
  zero gain.

**Lighter cleanup:** nothing. Both TUs are already minimal, coherent, and their headers are
small. Leave them.

---

### 2.4 Asset-infra group: `texalpha` / `noise` / `utilitynoise`

**Verdict: REJECT the merge. `utilitynoise.cpp` must be resolved by deletion, not
consolidation — and that deletion needs explicit sign-off because it is debug-UI-visible.**

* `texalpha.cpp` (962, 4 SEH) and `noise.cpp` (959, 0 SEH) are **unrelated**: per-texture
  alpha classification vs. the noise service. Nothing to consolidate.
* **`utilitynoise.cpp` call-site verification (the question asked):** it is **NOT dead at
  link level**. Live call sites:
  * `src/core/main.cpp:755` — `GenerateUtilityTexture(noiseRes)` inside `refreshIni()`
    (guarded by `g_pUtilityNoise == nullptr || g_CurrentNoiseSize != noiseRes`, :754);
    plus `utilityNoiseResForQuality()` at :731 and the `g_noiseQuality` static at :727.
  * `src/core/debugmenu_ui.cpp:2063`, `:2419`, `:4323` — the **"Utility" debug tab**
    (noise-quality combo `RK_NOISEQ`, "Regenerate noise now" button, and the
    `g_CurrentNoiseSize`/"resident|MISSING" status text at `:3643`, `:4320`).
  * Globals `g_pUtilityNoise` / `g_CurrentNoiseSize` declared `skygfx.h:968-970`.
* **But it IS dead at render level.** `g_pUtilityNoise` is **never bound to any sampler** —
  every one of its 10 references is generation, destruction, a null-check, or menu text. Its
  own header comment admits it: *"Consumers (water/SSAO/clouds/vehicle dither) are Wave 2 —
  nothing binds `g_pUtilityNoise` yet."*
* `noise.cpp`'s `SkyNoise` **supersedes it**: same canonical 4-channel packing
  (R=Perlin fBm, G=Worley F1, B=white, A=blue — `noise.h:4-15` vs
  `utilitynoise.cpp:6-14`), same tileable contract, same deterministic-hash approach — and it
  *is* bound, via `pipeBindNoise` (pipelinecommon.cpp:2061-2106) and driven by
  `depthhook.cpp:358/419` for device reset.

**Do not merge `utilitynoise.cpp` into `noise.cpp`.** The two have incompatible lifetime
models (`RwTexture*` created through RW vs raw `IDirect3DTexture9*` created lazily in
`BindUniversal()`), incompatible quality models (`int res` 32..1024 vs `NoiseQuality`
enum), and `noise.cpp`'s contract explicitly makes texture creation lazy on a live device
while `utilitynoise.cpp` creates the raster eagerly from `refreshIni()`. Forcing one into the
other means rewriting the debug-menu regeneration path = behaviour change, exactly what the
fortify pattern forbids.

**Recommended end state: DELETE `src/render/utilitynoise.cpp`** together with its `core/`
call sites (`main.cpp:727-760` block, `debugmenu_ui.cpp:2063/2419/4323` + the Utility-tab
noise widgets + the `skygfx.h:968-970` decls), in its own late stage (Stage 4) **after the
user signs off** on losing the "Utility" tab's noise-quality row and "Regenerate noise now"
button. Zero rendering change (the texture was never sampled); the change is confined to the
debug UI and one INI key's effect (`[General] NoiseQuality`). If sign-off is withheld, the
fallback is: keep the file and add a one-line deprecation banner pointing at `SkyNoise` —
**do not** merge it anywhere.

---

## 3. Deferred RISKY items — concrete safety plans

These are the items that are worth doing but must not be done casually. Each gets a
behaviour-equivalence argument and a rollback.

### 3.1 FX additive push/restore duplication (`buildingPipe.cpp` ~1511-1585 vs
`vehiclePipe.cpp` ~700-765) → extract a shared push core into `pipelinecommon.cpp`

**What is duplicated.** The identical 5-state *invariant core* of every FX additive leg:

```
ALPHAFUNC = ALWAYS (L1 rw + L2 D3DRS_ALPHAFUNC)   ALPHATESTENABLE = TRUE (L2 + L3)
ZWRITE    = FALSE (L1 + L2 + L3)                  VERTEXALPHA = TRUE (L1 + L2)
ALPHABLEND = TRUE (L2 + L3)                        FOGCOLOR = 0 (L1 + L2)
```

carried by the shared comment *"FX legs MUST keep ALPHAFUNC=ALWAYS + ONE/ONE — RGB=0 soft
borders are the black-quad payload if drift bypasses fxAdditiveBlend"*, which appears at
`buildingPipe.cpp:1511`, `vehiclePipe.cpp:697`, **and** `vehiclePipe.cpp:1180`.

**What genuinely differs (why a full push/restore extraction is NOT safe):**

| Aspect | `buildingPipe` | `vehiclePipe` |
|---|---|---|
| Blend pair pushed | ONE/ONE | ONE/ONE (same) |
| ZWRITE on restore | snapshot `zwrite` (cb-entry locals, :612-614) | **hard-coded `TRUE`** |
| Restore source | locals `src/dst/alphafunc/fog` from `rs` snapshot | `state->src/dst/alphafunc/fog` |
| TEXTUREFACTOR restored (L3) | no | **yes** (`state->texturefactor`) |
| Sampler stages restored | 0-1 (diffuse, env) | 0-2 (diffuse, env, spec) |
| Inline siblings | 1 site | **3 more** (:1180 PS2 cb, :1854 Leeds/VCS cb with `SRCALPHA/ONE`+`INVSRCALPHA` split, :3000 Switch-entry with `isLightMesh` branch) |

The blend pairs are *not* all ONE/ONE across the family (Leeds/VCS and the light-mesh leg use
`SRCALPHA,ONE` / `SRCALPHA,INVSRCALPHA`), and the restore semantics differ deliberately
(the code comments the building/vehicle ZWRITE divergence explicitly).

**Safety plan (two-step, do not skip step 1):**

1. **Step 1 — extract only the invariant core** as `pipeFxAlphaLegPush()` in
   `pipelinecommon.cpp` (decl in `skygfx.h`): exactly the 6 states above, forced through
   L1+L2+L3, no blend factors, no restore. Call it from `buildingPipe.cpp:1514-1529` and
   `vehiclePipe.cpp:704-719` (and the `:1180` inline sibling), leaving every blend-factor
   write and every restore **in place, verbatim, at the call site**.
   *Behaviour-equivalence argument:* the extracted sequence is byte-identical to the six
   statements it replaces at each call site (verified against both files), so the emitted
   state transitions are unchanged in value, layer, and order. The only observable difference
   is function-call indirection, which cannot change device state.
   *Rollback:* revert `pipelinecommon.cpp` + `skygfx.h` + 2 call sites; no data migration.
2. **Step 2 (optional, higher risk) — extract the restore** as
   `pipeFxAlphaLegRestore(const FxLegRestore &)` with an explicit descriptor struct carrying:
   `zwriteTarget` (snapshot value **or** the literal `TRUE`), `srcBlend/dstBlend/alphafunc/
   fog` (already-mapped values), `restoreTextureFactor` (bool), `sampStageCount` (2 or 3),
   and `sampU[]/sampV[]`. The building call site fills `zwriteTarget = zwrite` and
   `restoreTextureFactor = false, sampStageCount = 2`; the vehicle call site fills
   `zwriteTarget = TRUE` and `restoreTextureFactor = true, sampStageCount = 3`.
   *Behaviour-equivalence argument:* every value written today is passed through the
   descriptor unchanged; the divergence is preserved as data, not erased.
   *Rollback:* revert to the Step-1 shape.

**Do not** attempt to also absorb the Leeds/VCS (`:1854`) and Switch-entry (`:3000`) legs in
Step 2 — they push different blend pairs and restore along different exit paths
(`isLightMesh` branching). Absorbing them is a third step and is out of scope here.

### 3.2 `mirrorPass.cpp` private park-and-set copy (~116-130) → canonical primitives

`MirrorRestoreState3L` (mirrorPass.cpp:116-130) and `envParkSetState3L` (envmap.cpp:371-385)
are byte-for-byte the same idiom, and both use the **non-canonical interleaving**
`L1park → L2park → L1target → L2target → flush → L3pin`. `pipelinecommon.cpp:1118-1121`
states the canonical order is *park-then-target per layer* (`pipeRwParkSet` → `pipeL2ParkSet`
→ `pipeL3Set`) and explicitly records that `postfx_utils.cpp` "used to interleave
L1park/L2park/L1target/L2target — same end state, not canonical" and was already converted.

**Safety plan:** add `pipeParkSet3L(RwRenderState rwState, D3DRENDERSTATETYPE d3dState,
RwUInt32 rwPark, RwUInt32 rwTarget, RwUInt32 d3dPark, RwUInt32 d3dTarget)` to
`pipelinecommon.cpp` = `pipeRwParkSet(...)` + `pipeL2ParkSet(...)` + `_rwD3D9RenderStateFlushCache()`
+ `pipeL3Set(...)`, then make both private copies one-line wrappers.

*Behaviour-equivalence argument:* identical end state on all three layers; the transient
interleaving is unobservable because `pipelinecommon.cpp:1126-1129` guarantees
"no draw can occur between the writes, so the transient park never reaches the device", and
the flush still happens **before** the L3 pin in both orderings. The `rwRENDERSTATENARENDERSTATE`
no-rw-twin sentinel is preserved.

*C2702 guard:* `MirrorRestoreState3L` sits next to the `MirrorCompositeSaved` POD pattern;
convert the **helper body only**, never move it inside a `__finally`. `envParkSetState3L` has
no `__finally` around it (envmap's 14 `__finally` are elsewhere) — verify with a grep before
editing.

*Rollback:* restore the two static bodies (they are 15 lines each).

### 3.3 Unreachable `UploadUnifiedConstants` (`src/core/debugmenu_ui.cpp:438`)

**Finding:** the function is self-declared dead — `return;` on line 440 as its first
statement, preceded by a comment *"DEAD CODE: This function is never called from anywhere in
the codebase... retained for reference in case the feature is revived. See
docs/Future Features.md."* Zero call sites (grep confirms: only the definition at :438 and a
comment reference at :1723). Declared `skygfx.h:1259`.

**Verdict: leave it exactly as is.** It is already correctly labelled, it costs one dead
function body, and deleting it touches `src/skygfx.h` — a file **every** lane reads and at
least two lanes (`pipelinecommon`/`buildingPipe`/`vehiclePipe`) write. Spending a shared-file
lock on a cosmetic deletion is negative value. If it is ever removed, it goes with its
`skygfx.h:1259` declaration in the same commit and `docs/Future Features.md` updated — but
that is not part of this roadmap.

### 3.4 Three `blendToD3D` copies — **IN FLIGHT (C7), do not re-plan**

`buildingPipe_blendToD3D` (buildingPipe.cpp:465), `vehiclePipe_blendToD3D`
(vehiclePipe.cpp:117), `pipeBlendToD3D` + `pipeBlendHasRwTwin` (pipelinecommon.cpp:1439-1481).
A separate lane is already collapsing these into the `pipelinecommon` pair and deleting two
`#if 0` blocks. This roadmap's Stage 1/2/5 are **gated on that lane's commit** and never edit
those four files concurrently with it. No further plan is offered here.

### 3.5 Duplicate FX-additive alpha sequence

Covered by §3.1 — the sequence *is* the invariant core described there. The third and fourth
copies (`vehiclePipe.cpp:1180`, `:1854`, `:3000`) are recorded there as explicitly
**out of scope for Step 2** so nobody "tidies" them into the shared helper by accident.

---

## 4. What must NOT be merged (and why)

| Target | Why not |
|---|---|
| **`postfx.cpp`** | **C1.** 4,818 ln, 27 `__try`, chain orchestrator, 12 consumers, owns `PostfxMenuPauseOnly` which collides with `postfx_colourfilter`. |
| **`postfx_smaa.cpp` into anything** | Pulls `AreaTex.h` (14,980 ln) + `SearchTex.h` (132 ln) into the target; no merged form stays under a reviewable size. |
| **`pipelinecommon.cpp` into anything** | It is the doctrine TU (C2). It *absorbs* helpers; it is never absorbed. Merging it into a pipe would scatter blend-state code, violating C2. |
| **`forwardplus.cpp` with diagnostics** | It is a GPU light-culling subsystem with a shader register contract, not telemetry. |
| **`depthhook.cpp` with render passes** | Device-lifecycle/hooks vs render callbacks; merging inverts layering and obscures the `Reset` ordering contract shared with `SkyNoise`. |
| **`utilitynoise.cpp` with `noise.cpp`** | Superseded duplicate, not a sibling; incompatible lifetime + quality models (§2.4). Resolve by deletion, not merge. |
| **`envmap.cpp` + `mirrorPass.cpp`** | The only two `__finally`-carrying TUs (14 + 8). Keep the C2702 blast radius small (§2.2). |
| **Anything into `skygfx.h` / `src/core/main.cpp`** | Shared files; concurrent-lane hazard, and out of `src/render/` scope. |
| **The in-flight families (C6)** | `neo*`, `PC_Grass*`, `*_mode` stubs — another lane owns them right now. |

---

## 5. Staged execution order

**Gates.** Every stage below assumes **both in-flight lanes have landed** (C6: neopipe/grasssys
+ mode-stub deletion; C7: `blendToD3D`/`pipeBlendHasRwTwin` dedupe + `#if 0` deletion). Do not
start otherwise — Stage 1 and Stage 2/5 write `pipelinecommon.cpp`/`buildingPipe.cpp`/
`vehiclePipe.cpp`, and Stage 3/4 write `build/skygfx.vcxproj` + `premake5.lua`, all of which
the in-flight lanes are editing.

Each lane is sized for ~25 minutes. **Write scopes are disjoint within a stage**, so the lanes
in one stage row run in parallel.

### Stage 1 — doctrine primitives *(1 lane)*

| Lane | Files written | Work |
|---|---|---|
| 1.1 | `src/render/pipelinecommon.cpp`, `src/skygfx.h` | Add `pipeFxAlphaLegPush()` (the 6-state FX invariant core) and `pipeParkSet3L()` (canonical park-and-set + flush + L3 pin) next to the existing `pipeRwParkSet/pipeL2ParkSet/pipeL3Set` block at :1104-1155. Docs/comments in the same style. **No callers yet.** |

Gate for Stage 2/3/5. EOL: `pipelinecommon.cpp` and `skygfx.h` are both CRLF — keep CRLF.

### Stage 2 — call-site conversion *(4 lanes, parallel, disjoint)*

| Lane | Files written | Work |
|---|---|---|
| 2.1 | `src/render/envmap.cpp` | `envParkSetState3L` → wrapper over `pipeParkSet3L` (§3.2). |
| 2.2 | `src/render/mirrorPass.cpp` | `MirrorRestoreState3L` → wrapper over `pipeParkSet3L` (§3.2). **Do not touch the `MirrorCompositeSaved` POD/`__finally` pattern.** |
| 2.3 | `src/render/buildingPipe.cpp` | Route the FX push core at :1514-1529 through `pipeFxAlphaLegPush()`; **leave all blend-factor writes and the whole restore verbatim** (§3.1 Step 1). |
| 2.4 | `src/render/vehiclePipe.cpp` | Same for `vehiclePipe_fxAdditiveBlend` :704-719 and the `:1180` inline sibling. **Leave `:1854` and `:3000` alone.** |

EOL: `envmap.cpp` CRLF, `mirrorPass.cpp` LF, `buildingPipe.cpp` CRLF, `vehiclePipe.cpp` CRLF —
each edit keeps its own file's convention (no whole-file normalisation).

### Stage 3 — `postfx_support.cpp` fortify *(1 lane; runs parallel with Stage 2 — disjoint files)*

| Lane | Files written | Work |
|---|---|---|
| 3.1 | **new** `src/render/postfx_support.cpp`; **delete** `src/render/postfx_luminance.cpp`, `src/render/postfx_motionblur.cpp`, `src/render/postfx_utils.cpp`; `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters`, `premake5.lua` | Full fortify pattern: prologue listing the 3 TUs in order (`luminance → motionblur → utils`, order matters: `utils` defines `s_rawGeomStateIds` used only by its own statics, so it can go last; put `utils` first if you prefer the "toolkit before users" reading — either is fine, but record the choice); `// ---- Includes (deduped union) ----` = `skygfx.h`, `debug_handl.h`, `postfx.h`, `profiler.h`, `depthhook.h`; three `// ==== ... (was src/render/....cpp) ====` banners; **verbatim bodies**; `cl /Zs`; backup-then-delete to `_backup/postfx_support_merge_2026-10-27/`; swap the 3 `ClCompile` rows for 1. **EOL adopted: LF.** |

### Stage 4 — `utilitynoise.cpp` resolution *(1 lane; after Stage 3 — shared build files; **NEEDS SIGN-OFF**)*

| Lane | Files written | Work |
|---|---|---|
| 4.1 | `src/render/utilitynoise.cpp` (delete), `src/core/main.cpp`, `src/core/debugmenu_ui.cpp`, `src/skygfx.h`, `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters`, `premake5.lua` | Delete `utilitynoise.cpp` + strip :727-760 `utilityNoiseResForQuality`/`g_noiseQuality`/the `refreshIni` regen block in `main.cpp`; strip `debugmenu_ui.cpp:2063/2419/4323` + the Utility-tab noise widgets + `:3643`/`:4320` status text + `NoiseResForQuality` (:865); drop `skygfx.h:968-970`. **Behaviour delta: debug-UI "Utility" tab loses its noise-quality row, "Regenerate noise now" button and status line; `[General] NoiseQuality` becomes inert. Zero rendering change.** Backup to `_backup/utilitynoise_del_2026-10-27/`. If sign-off withheld → skip; apply the deprecation banner instead. |

### Stage 5 — deferred RISKY: FX restore extraction *(1 lane; after Stage 2; **optional**)*

| Lane | Files written | Work |
|---|---|---|
| 5.1 | `src/render/pipelinecommon.cpp`, `src/skygfx.h`, `src/render/buildingPipe.cpp`, `src/render/vehiclePipe.cpp` | §3.1 **Step 2** only: `pipeFxAlphaLegRestore(const FxLegRestore&)` with the explicit descriptor (zwrite policy, mapped blend/alphafunc/fog, `restoreTextureFactor`, `sampStageCount`, `sampU/sampV`). Convert the two restore blocks; **leave `:1854`/`:3000` untouched.** |

**Parallelism note (deliberate, not a shortfall):** peak concurrency is **5 lanes**
(Stage 2 × 4 + Stage 3 × 1). More lanes would mean inventing merges this review rejected in
§2. The bottleneck is that `pipelinecommon.cpp`/`skygfx.h` are shared by Stages 1 and 5 and
must be serialised, and `build/skygfx.vcxproj`+`premake5.lua` are shared by Stages 3 and 4.

---

## 6. Verification checklist (per stage)

### Universal gates (every stage)

* **U1 — Basename scan (C5):**
  `grep -o 'ClCompile Include="[^"]*"' build/skygfx.vcxproj | grep -o '[^\\"]*\.cpp' | sort | uniq -d`
  must be **empty** (verified clean on the current tree: 174 `ClCompile` rows, no duplicate
  basenames). Additionally confirm
  `postfx_support.cpp`, `neopipe.cpp`, `grasssys.cpp` appear exactly once and that
  `postfx_luminance.cpp`, `postfx_motionblur.cpp`, `postfx_utils.cpp`,
  `utilitynoise.cpp`, `custom_mode.cpp`, `iv_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`,
  `ps2_mode.cpp`, `xbox_mode.cpp`, `neo.cpp`, `neoCarpipe.cpp`, `neoWaterdrops.cpp`,
  `PC_GrassRenderer.cpp`, `PC_GrassInstancer.cpp` appear **zero** times.
* **U2 — Stale-object purge:** delete any `build/obj/**/(postfx_luminance|postfx_motionblur|
  postfx_utils|utilitynoise).obj` before linking (prevents "object specified more than once").
* **U3 — `cl /Zs`** on every touched TU (syntax-only, no link), with the same `/I` and `/D`
  the vcxproj uses. For merged TUs this is the primary gate.
* **U4 — Line endings:** `file <touched>` must report the convention declared in §5 for that
  file. A merged TU must be **uniform** (no mixed CRLF/LF inside one file).
* **U5 — Basename/`ClInclude` hygiene:** any header deleted or renamed must lose its
  `ClInclude` row in the same commit (`neo.h`, `PC_GrassRenderer.h`, `PC_GrassInstancer.h`,
  `PC_PlantsMgr.h` in the in-flight lane; none of the Stage 3/4 headers need `ClInclude`
  edits because `postfx.h` survives).
* **U6 — premake mirror:** `premake5.lua` rows must match `build/skygfx.vcxproj` (the script
  is a documented mirror; its header lists the functional gates, incl. the ClCompile count).
* **U7 — `git status`** shows only the stage's declared files; no stray edits to
  `src/skygfx.h` / `src/core/main.cpp` from unrelated lanes.

### Stage-specific gates

**Stage 1**
* `cl /Zs src/render/pipelinecommon.cpp` clean.
* `grep -c "pipeFxAlphaLegPush\|pipeParkSet3L" src/render/pipelinecommon.cpp` ≥ 2
  (definition + doc block); `grep -n "pipeFxAlphaLegPush\|pipeParkSet3L" src/skygfx.h` shows
  both declarations adjacent to `pipeRwParkSet` (:1344).
* **Behaviour-equivalence argument:** Stage 1 adds **only** new functions; zero call sites
  changed ⇒ the compiled behaviour of every existing path is bit-identical.

**Stage 2**
* Per-lane `cl /Zs` on the single edited TU.
* 2.1/2.2: `grep -n "RwRenderStateSet(rwState, (void\*)rwPark)" src/render/envmap.cpp
  src/render/mirrorPass.cpp` → **no matches** (the inline park body is gone).
  `grep -n "__finally" src/render/mirrorPass.cpp` count unchanged (8) and
  `grep -n "__try" src/render/envmap.cpp` unchanged (16).
* 2.3/2.4: `grep -c "ALPHATESTFUNCTIONALWAYS" src/render/buildingPipe.cpp` and
  `src/render/vehiclePipe.cpp` — the FX-pair count must drop by exactly 1 per converted leg
  and reappear inside `pipeFxAlphaLegPush` (pipelinecommon). The **restore** statements must
  be **byte-identical** (`git diff --word-diff=porcelain` shows only the push-core lines
  changed).
* **Behaviour-equivalence argument:** same state values, same layers (L1/L2/L3), same order
  within the invariant core; only the blend-factor writes and restores are untouched, so no
  divergence in the paths that differ (ZWRITE policy, TEXTUREFACTOR, sampler count) is
  introduced.

**Stage 3**
* `cl /Zs src/render/postfx_support.cpp` clean.
* Symbol equivalence: the merged TU must export exactly the same non-static set as the three
  originals combined — **13 functions** + **5 globals**. Functions:
  `RunFrameExposure`, `ReleaseLuminanceTargets` (luminance);
  `ReleaseVelocityBufferResources`, `DrawVelocityBuffer` (motionblur);
  `TrackRaster`, `UntrackRaster`, `RasterEnsureSurfaceReady`, `CaptureScreenSize`,
  `GetScreenSize`, `IsGameInMenuOrPaused`, `SetupFullscreenQuad`, `SaveRawGeomStates`,
  `RestoreRawGeomStates` (utils — note `RestoreOneRawGeomState` is `static` at
  `postfx_utils.cpp:227` and must **stay** static). Globals:
  `luminanceReducePS`, `luminanceAdaptPS` (luminance:6), `g_velocityTex` (motionblur:9),
  `colorfilterVerts`, `colorfilterIndices` (utils:8-9). Note the K&R-style split
  declarations (`void` on its own line at `postfx_utils.cpp:145/189/245` before
  `SetupFullscreenQuad`/`SaveRawGeomStates`/`RestoreRawGeomStates`) — a naive
  `^<type> <name>` grep misses them; use a parser or grep the name-on-next-line form.
* `grep -c "__try" src/render/postfx_support.cpp` = 4 and `grep -c "__finally"` = 0
  (C2702 structurally impossible — re-verify after the merge, do not trust this doc).
* `file src/render/postfx_support.cpp` → **LF**.
* `grep -rn "postfx_utils.cpp\|postfx_luminance.cpp\|postfx_motionblur.cpp" src/ build/
  premake5.lua` → only stale **comments** may remain; zero build rows. Update the 3-4
  comments that name `postfx_utils.cpp` (e.g. `envmap.cpp:363`, `buildingPipe.cpp:475`,
  `main.cpp:1840/3686/3968/5205`, `depthhook.cpp:541`) in the same commit **or** leave them
  and record that they are historical references — pick one, don't mix.
* **Behaviour-equivalence argument:** no dynamic static initialisers in any of the three
  (verified) ⇒ concatenation cannot reorder any global construction; no `__finally` ⇒ C2702
  impossible; strict include union ⇒ C2712 impossible; all exports already declared in
  `postfx.h` ⇒ no signature drift.

**Stage 4**
* `cl /Zs src/core/main.cpp src/core/debugmenu_ui.cpp` clean.
* `grep -rn "GenerateUtilityTexture\|g_pUtilityNoise\|g_CurrentNoiseSize\|NoiseResForQuality\|utilityNoiseResForQuality\|g_noiseQuality" src/` → **zero matches**.
* `grep -n "NoiseQuality" src/core/debugmenu_ui.cpp` → zero rows left for `RK_NOISEQ`; the
  `RK_NOISEQ` enum value itself may remain only if other rows share the enum — verify and
  record.
* Launch check (`tools/fast_build.py --launch` is **out of scope for this doc**; note it as
  the human acceptance step): the "Utility" tab renders without the noise widgets and no
  `MISSING` status line.
* **Behaviour-equivalence argument:** the deleted texture was **never bound to any sampler**
  (verified — all 10 references are generation/destruction/menu text), so no pixel changes.
  The only visible delta is the debug UI, which is the sign-off item.

**Stage 5**
* `cl /Zs` on all four files.
* `git diff --word-diff=porcelain src/render/buildingPipe.cpp src/render/vehiclePipe.cpp`
  must show the **restore** statements moved **verbatim** into the descriptor fill + helper
  call; a value audit (`D3DRS_ZWRITEENABLE`, `D3DRS_TEXTUREFACTOR`, sampler-loop bound 2 vs 3)
  must show the building and vehicle descriptors still encode their original divergence.
* `grep -n "vehiclePipe_fxAdditiveBlend\|isLightMesh" src/render/vehiclePipe.cpp` confirms
  `:1854` and `:3000` legs are **unchanged**.
* **Behaviour-equivalence argument:** every value written today is carried unchanged in the
  descriptor; the differences (ZWRITE snapshot vs `TRUE`, TEXTUREFACTOR present/absent,
  2 vs 3 sampler stages) are preserved as data. Rollback = revert 4 files.

---

## 7. Bottom line

`src/render/` does not need broad consolidation — it needs **one** safe fortify
(`postfx_support.cpp`, 3 → 1 TU, ~970 lines, LF, zero `__finally`, zero symbol collisions)
and **four** render-state deduplications that belong in `pipelinecommon.cpp` (the doctrine TU),
not in fewer files. Every larger family I evaluated (postfx effects, state-heavy, telemetry,
asset-infra) costs more than it returns: the effect passes are already one file per shader
family, `postfx_smaa.cpp` anchors 15k lines of lookup data, `envmap.cpp`/`mirrorPass.cpp` are
the only `__finally`-bearing TUs and should stay isolated for C2702 blast-radius reasons,
`forwardplus.cpp` is not telemetry at all, and `utilitynoise.cpp` is a superseded duplicate
whose correct end state is **deletion with sign-off**, not a merge.

Total: 53 → 51 files in `src/render/` (plus whatever the in-flight lanes remove), one new
primitives block in `pipelinecommon.cpp`, and four call sites converted to it.
