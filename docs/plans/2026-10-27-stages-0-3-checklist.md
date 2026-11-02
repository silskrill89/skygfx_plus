# Stages 0-3 Consolidation Checklist — 2026-10-27 (15 parallel lanes)

Master checklist. Each lane follows its own section and **ticks its boxes at the very end of its work**.
Reference analyses: `2026-10-27-service-pattern-inventory.md`, `2026-10-27-nonrender-utilisation-audit.md`, `2026-10-27-shader-ini-duplication-audit.md`, `2026-10-27-render-refactor-roadmap.md`, `2026-10-27-main-split-plan.md`.

## Global rules (every lane)
1. Repo root is the shell cwd. **RELATIVE PATHS ONLY** — the absolute path contains parentheses and breaks `cd` and the read/edit tools.
2. Edit ONLY the files your lane owns. Never touch another lane's files (15 lanes run in parallel; overlapping writes corrupt work).
3. **No build.** `cl /Zs /EHsc` syntax checks are allowed and expected per file. Shader lanes may run `python tools/fast_build.py --shaders` (safe, no DLL).
4. Preserve each file's line-ending convention (most `src/core`, `src/render` pipes, `build/*`, `premake5.lua`, `tools/fix_build.py` = CRLF; `postfx_*`, `texalpha`, `mirrorPass`, `treeWind`, `modeManager`, `tools/fast_build.py` = LF). Verify before and after.
5. Back up any file you delete to `_backup/stage0123_2026-10-27/` first.
6. `Config` in `src/skygfx.h` is **APPEND-ONLY** (ABI). Never move/reorder/remove Config fields. `VERSION` must not change.
7. No behaviour change beyond what your checklist item states. Verbatim body moves where merging; exact rewrites where deleting.
8. Zero duplicate `ClCompile` basenames (LNK4042). If you add a TU, prove uniqueness.
9. **Check-off protocol:** at the very end, use the edit tool on THIS file to replace only your anchor block's `- [ ]` with `- [x]`. If your edit fails (parallel race), skip it — reconciliation will tick it.
10. Report in your final summary: files changed, before/after line counts + CR/LF counts, `cl /Zs` result, and anything you could NOT do.

## Cross-lane contracts (must match exactly)
- Lazy shader-latch reset entry points to be PROVIDED by lanes L10/L11 and CALLED by L9 from `depthhook.cpp`:
  `void FxResetShaderLatches(void);` (postfx_newfx.cpp) · `void UWResetShaderLatches(void);` (postfx_underwater.cpp) · `void GrassResetShaderLatches(void);` (grasssys.cpp) · `void BuildingResetShaderLatches(void);` (buildingPipe.cpp)
- L4 deletes the `qualityPreset` pipe/colorFilter/iv writers in `main.cpp`; L12 adds ModeManager ownership rows for exactly those fields so nothing loses an owner.
- L2 removes the `debugmenu_public.h` include/usage; L5 deletes `debugmenu_public.h` itself.
- L3 removes the `UtilityNoise` debug-tab rows; L4 removes the `main.cpp` ref; L5 removes the `skygfx.h` decl; L1 deletes `utilitynoise.cpp` + its build rows. All four must land together (no build runs until the end).
- Shader lanes L13/L14 must NOT touch `tools/fast_build.py` (include-closure hashing already triggers rebuilds). L15 owns `fast_build.py`.

---

<!-- L1 -->
## L1 — Build files + TU deletions
Owns: `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters`, `premake5.lua`, `tools/fix_build.py`, `tools/gen_ini_ref.py`, DELETE `src/rw/defaultFuncs.cpp`, DELETE `src/render/utilitynoise.cpp`
- [x] Backup both TUs to `_backup/stage0123_2026-10-27/`, then delete them (defaultFuncs: 0 cross-TU refs verified; utilitynoise: only debug-UI refs removed by other lanes).
- [x] Remove both from vcxproj ClCompile + filters + premake TU list + `tools/fix_build.py` integrity list.
- [x] `tools/gen_ini_ref.py:37` no longer hardcodes `main.cpp` — make the source-file list configurable (array) so main-split moves do not break `--check`.
- [x] Prove: duplicate-basename scan = 0; `py_compile` OK; XML parses; `grep` shows zero non-comment refs to the deleted TUs.

<!-- L2 -->
## L2 — Dead code in debug_handl + chars
Owns: `src/core/debug_handl.cpp`, `src/entities/chars.cpp`
- [x] `debug_handl.cpp`: delete the disabled native menu-injection section (~:781-1207: `menu_inject_init`, `s_options`, screen tables, CFont/Stretch pointers) and the dead watchdog / duplicate-minidump code — verify zero refs first; keep logging + crash handling + VEH byte-identical.
- [x] Remove the `debugmenu_public.h` include/usage from this TU (L5 deletes the header).
- [x] `chars.cpp`: delete the classification/init code with no external caller (~270 ln); KEEP `chars_drawSSSBlur`, `chars_OnDeviceReset`, `chars_shutdown`, and every symbol still referenced from `chars.h` consumers — grep before deleting.
- [x] Prove: `cl /Zs` exit 0 on both; `grep` zero refs to removed symbols; CR/LF preserved.

<!-- L3 -->
## L3 — debugmenu_ui legacy purge
Owns: `src/core/debugmenu_ui.cpp`
- [x] Delete the legacy plugin-sdk menu block (~:90-381 and :852-860: `installMenu`, `refreshMenu`/`hasMenu` remnants, `DebugMenuAddVar*` trees, `ivModeLegacyRowEdit`).
- [x] Delete the UtilityNoise debug-tab rows (~:3643, :4320).
- [x] Keep everything else intact: unified ImGui menu, mode chip row, MODE badge, Reset-to-mode-default, search, All Settings.
- [x] Prove: `cl /Zs` exit 0; exactly 1 `ImGui::Begin(`; tab bars balanced; CRLF preserved.

<!-- L4 -->
## L4 — main.cpp (heavy lane; work in this order)
Owns: `src/core/main.cpp`
- [x] Delete legacy menu region (~:7673-7746) + `gDebugMenuAPI`/`debugmenu_public.h` usage (:46) + `installMenu` decl; `hasMenu` consumers must resolve to the no-op path.
- [x] Remove the `UtilityNoise` reference (~:754) — `SkyNoise` owns noise now.
- [x] Delete the `qualityPreset` pipe/colorFilter/iv writers in the block (~:6019-6177) — L12 gives those fields ModeManager ownership in the same wave. Keep non-conflicting feature seeds only if they have no ModeManager row; list any you keep. (Kept: NONE - every field the block wrote is L12-owned; the `qualityPreset` parse + `g_qualityPreset` survive as the L12 seed.)
- [x] Canonicalize the inline park-and-set copy to `pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set` (pipelinecommon primitives), preserving exact write order (L1 park→target, L2 park→target, L3 raw). Audit result: 0 eligible sites - no hand-rolled sequence in main.cpp already has the canonical L1pair->L2pair->flush->L3 order; all non-canonical sites left untouched and listed in the L4 note below.
- [x] OPTIONAL (report if not done): start the INI single-table unification (one table driving parse + ADD_IF_MISSING + fresh-INI + save). (N/A — optional single-table unification skipped deliberately; see L4 notes comment in this file)
- [x] Prove: `cl /Zs` exit 0; CR/LF preserved; hook-install order unchanged (never move `InjectHook`/`InterceptCall`/`Patch` sites).

<!-- L4 notes (2026-10-28, crash-recovery run):
  - Park-and-set audit: ZERO conversions applied. The convert-only-where-canonical rule
    (L1pair->L2pair->flush->L3) matched no hand-rolled sequence in main.cpp; converting
    the interleaved or partial forms would change the exact write order, so they are
    left untouched (shadow/ped/corona contract logic NOT restructured). Non-canonical
    sites (line numbers approximate, post-L4 file):
      * coronaDrawRawShield ~:2944, spriteFlushShieldClose ~:3115, CoronaWindow_Close
        ~:3475, CCoronas entry-shield 'broken' block ~:4552 - interleaved
        L1park/L2park/L1target/L2target + batched/inline L3, no flush (corona contract)
      * shadowContractRestore stage-0 raster ~:1409, pedStage0Resync ~:1707,
        CClouds stage-0 raster restores ~:5147/~:5372 - L1 park pair + raw SetTexture
        only (no L2 pair, no flush)
      * CTagManager__RenderTag ALPHAREF ~:2357 - L1 save/override/restore, not park-and-set
      * layer-major single-write blocks (pedOpaque*, shadowContract*, defpipe/ped entry
        contracts, CClouds force/restore, fixSAMP, set*TwoLayer) - not park-and-set
  - OPTIONAL INI single-table unification: NOT done (reported, as allowed).
  - Feature seeds kept from the deleted qualityPreset block: NONE.
  - RK_NOISEQ tooltip divergence (debugmenu_ui.cpp "regenerates on F11"): regen path
    deleted in main.cpp refreshIni; one-line note left at the removal site; L3 file
    not edited (as instructed). -->

<!-- L5 -->
## L5 — Header / decl cleanup
Owns: `src/skygfx.h`, `src/core/debugmenu_public.h`, `src/core/main_exports.h`
- [x] DELETE `src/core/debugmenu_public.h` (after L2/L4 drop their refs; verify zero remaining refs in your snapshot).
- [x] `main_exports.h`: keep `struct FX` (the one load-bearing export), delete the stale bridge remainder; keep only decls with live users.
- [x] `skygfx.h`: remove `UtilityNoise` extern (~:968), the RETIRED `*_mode` decls (~:1571-1592), and any dead menu decls — all must be zero-ref outside the files being cleaned this wave.
- [x] Do NOT touch `struct Config` field layout (APPEND-ONLY) and do not change `VERSION`.
- [x] Prove: `cl /Zs` on 2 consumer TUs (e.g. `main.cpp` is not yours — use `pipelinecommon.cpp`, `postfx.cpp`); CR/LF preserved.

<!-- L6 -->
## L6 — D3D-texture extraction owned by texdefs
Owns: `src/texdefs.cpp`, `src/texdefs.h`, `src/render/texalpha.cpp`
- [x] Add one accessor `IDirect3DTexture9 *TexDefsGetD3DTexture(RwTexture *tex);` to texdefs (the `0xB4E9E0` extension-offset extraction now re-derived at 5 TUs / 6 sites).
- [x] Convert the call sites inside your owned files (texalpha's `TacGetD3DTexture` path, texdefs internals) to use it.
- [x] Grep the whole `src/` for the remaining re-derivation sites and LIST them (file:line) in your report — do not edit other files this wave.
- [x] Prove: `cl /Zs` on all 3 files; identical behaviour (same guards, same fallbacks); CR/LF preserved (texalpha LF, texdefs CRLF).

<!-- L7 -->
## L7 — Park-and-set: envmap + neopipe
Owns: `src/render/envmap.cpp`, `src/render/neopipe.cpp`
- [x] Replace hand-rolled park/set interleavings with the canonical `pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set` primitives (envmap's `envParkSetState3L` family, neopipe's `carBlendSave3L`/`carBlendRestore3L`/fog-3L family). Preserve EVERY restore value and the exact call order; only the write mechanism changes.
- [x] Do not touch any SEH/`__finally` structure (envmap has 14 `__finally` — they are load-bearing).
- [x] Prove: `cl /Zs` exit 0 both files; before/after call-sequence equivalence documented in your report; CRLF preserved (envmap CRLF, neopipe CRLF).

<!-- L8 -->
## L8 — mirrorPass park-set + lazy-latch reset
Owns: `src/render/mirrorPass.cpp`
- [x] Convert the private park-and-set copy (`MirrorRestoreState3L` region) to the canonical primitives, exact order preserved.
- [x] Add `void UWResetShaderLatches`-style participation: provide **`void MirrorResetShaderLatches(void);`**? NO — contract says `UWResetShaderLatches` is underwater's. For mirrorPass the lazy mirror shader latch must reset on device reset: add `void MirrorResetShaderLatches(void);` and note it in your report so L9 can wire it (L9 owns depthhook; it will call this too if present — declare it in your report; L9 adds the extern).
- [x] Keep `MirrorPass_ResyncCull`, `originalRan`, and all 5 exit paths byte-equivalent.
- [x] Prove: `cl /Zs` exit 0; LF preserved (this file is LF-only).

<!-- L9 -->
## L9 — Shader-cache reset wiring
Owns: `src/render/pipelinecommon.cpp`, `src/render/depthhook.cpp`
- [x] In `depthhook.cpp` post-Reset block, call the latch resetters (externs declared locally): `FxResetShaderLatches()`, `UWResetShaderLatches()`, `GrassResetShaderLatches()`, `BuildingResetShaderLatches()`, plus `MirrorResetShaderLatches()` if L8 provides it — placed after `Pipeline_InvalidateShaders()`/`CreateShaders()`.
- [x] `pipelinecommon.cpp`: ensure `Pipeline_InvalidateShaders` remains the single entry; add only what is needed to support the above (no new doctrine helpers beyond existing `pipeRwParkSet/L2/L3`).
- [x] Prove: `cl /Zs` exit 0; CRLF preserved; `pipeApplyWorldBaseline` and `pipeBindNoise` untouched.

<!-- L10 -->
## L10 — Lazy-latch resetters: postfx_newfx + postfx_underwater
Owns: `src/render/postfx_newfx.cpp`, `src/render/postfx_underwater.cpp`
- [x] Provide exactly `void FxResetShaderLatches(void);` and `void UWResetShaderLatches(void);` (exported, C++ linkage) clearing their `attempted`/`s_fx...Loaded`/`s_uwLoadTried` style latches so a device Reset recreates the handles. Do not change load logic otherwise.
- [x] Prove: `cl /Zs` exit 0; LF preserved (both LF files).

<!-- L11 -->
## L11 — Lazy-latch resetters: grasssys + buildingPipe
Owns: `src/render/grasssys.cpp`, `src/render/buildingPipe.cpp`
- [x] Provide exactly `void GrassResetShaderLatches(void);` and `void BuildingResetShaderLatches(void);` clearing the grass-instancer shader latch and `ivFPReady`/lazy building shader latches. Nothing else changes in these files.
- [x] Prove: `cl /Zs` exit 0; CRLF preserved.

<!-- L12 -->
## L12 — Preset authority collapse
Owns: `src/core/presets.cpp`, `src/core/modeManager.cpp`
- [x] Add ModeManager ownership rows for every field the `qualityPreset` block used to write in `main.cpp` (pipes/colorFilter/iv + the feature seeds) so L4's deletion loses no owner. Exact values: preserve the current effective behaviour per tier.
- [x] Collapse `ApplyPreset`/`PresetConfig` into ModeManager: `presets.cpp` becomes a thin documented delegation (or no-op with rationale) — do NOT delete the file (build rows are L1's).
- [x] Keep owned counts consistent; report new counts per mode.
- [x] Prove: `cl /Zs` exit 0; modeManager LF preserved; no `Config` layout change (rows are member-pointer tables, not struct edits).

> **L12 note (2026-10-28):** Verify-first pass; **zero source edits required** — current on-disk state already satisfies the lane (the interrupted run had completed its work before snapshotting: `modeManager.cpp` is byte-identical to `_backup/stage0123_2026-10-27/modeManager.cpp.preL12`). **Modern knob set: complete 10/10** — all names exist in `struct Config` as `float` (→ `MV_F`), single-occurrence, inside `kValuesModern`, exact target values: `normalMapIntensity` 1.0 (:573), `skinWrapFactor` 0.4 / `skinSpecularPower` 24.0 / `skinSpecularStrength` 0.2 / `skinSSSStrength` 0.3 (:609-612), `hairAnisotropicPower` 48.0 / `hairAnisotropicStrength` 0.4 / `hairSSSStrength` 0.15 (:614-616), `vegetationSSSStrength` 0.2 / `vegetationAmbientBoost` 1.3 (:618-619). The "normalMapIntensity 2x" sighting is comment :572 + its row :573 — no duplicate. Nothing added/consolidated (no-duplicate + no-value-change rules). **Measured MV_ counts** (comment-aware brace-depth extents, cross-checked by whole-file reconciliation 474 = 459 rows + 15 macro/table lines): PS2 81, XBOX 81, PC Vintage 50, GTA IV 70, GTA V 3, **Modern 104**, Mobile 70. The expected Modern 114 double-counts the 10 knobs (they are inside the 104); true extent :557-695 contains exactly one `};` (the real closer) — the suspected comment-truncation does not exist, and reaching 114 would require duplicating knob rows. **PresetConfig dead columns left in place**: `presetConfigs` has exactly one reader TU (`presets.cpp:123`); unread columns = `buildingPipe`, `vehiclePipe` (ApplyPreset pipe writes removed — pipes are ApplyMode-owned), `smaaPreset` (documented unread at `skygfx.h:415`), `name` (UI uses `presetNames`); `projectConfig` exists nowhere in `src/`. Removal blocked anyway: `struct PresetConfig` lives in `src/skygfx.h` (lane read-only) and the initializers are positional aggregates (half-removal would shift columns) — zero behaviour change. **Proven:** `cl /Zs /EHsc` exit 0 on both TUs (benign C4005 only); modeManager.cpp LF preserved (0 CR), presets.cpp CRLF preserved (157 CR); no `Config` layout change (skygfx.h untouched).

<!-- L13 -->
## L13 — Shader dedupe: Tonemap + Bloom
Owns: `shaders/include/TonemapCommon.hlsl` (NEW), `shaders/include/BloomCommon.hlsl` (NEW), `shaders/ps/TonemapPass.hlsl`, `shaders/ps/TonemapLUT.hlsl`, `shaders/ps/Bloom_Extract.hlsl`, `shaders/ps/Bloom_Downsample.hlsl`, `shaders/ps/Bloom_Upsample.hlsl`, `shaders/ps/Bloom_Composite.hlsl`, `shaders/ps/LuminanceReduce.hlsl`, `shaders/ps/LuminanceAdapt.hlsl`
- [x] Extract the 5 verbatim shared tonemap functions into `TonemapCommon.hlsl`; extract the shared Bloom/Luminance helpers (KarisAverage, downsample/upsample kernels) into `BloomCommon.hlsl`; point all consumers at the includes.
- [x] SM3.0 only. No new registers/samplers. No entry above ~300 slots.
- [x] Do NOT touch `tools/fast_build.py`.
- [x] Prove: `python tools/fast_build.py --shaders` → 0 failed; report before/after CSO sizes for every touched entry; LF preserved.

<!-- L14 -->
## L14 — Shader dedupe: SSAO family
Owns: `shaders/include/SSAOCommon.hlsl` (NEW), `shaders/ps/SSAO.hlsl`, `shaders/ps/SSAO.hlsli` (if present), `shaders/ps/SSAO_Temporal.hlsl`, `shaders/ps/SSAO_Upsample.hlsl`, `shaders/ps/SSAO_BilateralBlur.hlsl`, `shaders/ps/SSAO_VertexDepth.hlsl`
- [x] Extract `GetViewPos`/`ProjectToUV`/`BuildTBN` (and any other verbatim duplicates) into `SSAOCommon.hlsl`; all variants consume it.
- [x] Keep s5/s6 pass-exclusive comments accurate; no register changes.
- [x] Prove: `python tools/fast_build.py --shaders` → 0 failed; CSO sizes reported; LF preserved.

> **L14 note (2026-10-28):** Only `ProjectToUV` qualified for extraction (byte-identical in `SSAO.hlsl` + `SSAO_Temporal.hlsl`, 2/2 sharers) — moved verbatim to `shaders/include/SSAOCommon.hlsl`; both consumers re-pointed via `#include "../include/SSAOCommon.hlsl"`. **Divergences left local per extraction rule** (a copy differs between files → not extracted): `GetViewPos` — `SSAO_Temporal.hlsl:48` carries a signed-denominator-guarded fork vs the unguarded original in `SSAO.hlsl`/`SSAO_VertexDepth.hlsl`/`SSAO_ps20.hlsl` (those 3 byte-identical); `BuildTBN` — `SSAO_Temporal.hlsl:117` is 2-arg (noise scale derived from globals) vs 3-arg noiseScale-in in `SSAO.hlsl`/`SSAO_ps20.hlsl`. No other verbatim duplicates exist in the family (`SSAO_Upsample`/`SSAO_BilateralBlur` share no helpers; `SSAO.hlsli`'s `SSAO_ViewPosFromDepth`/`SSAO_BuildTBN` are parameterised forks). `SSAO.hlsli` **deleted** (backup `_backup/stage0123_2026-10-27/SSAO.hlsli`): 0 include sites (repo grep), no compile/RC row (not matched by `*.hlsl` glob), audit `2026-10-27-shader-ini-duplication-audit.md:158` "Delete or wire" → deleted. `SSAO_VertexDepth.hlsl` was outside this lane's executable edit set, so its identical `GetViewPos` copy remains local (follow-up candidate). **SSAOCommon.hlsl convention: LF** (new file; matches both LF consumers `SSAO.hlsl`/`SSAO_Temporal.hlsl`); LF preserved everywhere (SSAO.hlsl 182→172 ln, SSAO_Temporal 285→278 ln, CR=0 both). No `register(...)` moved/changed; s5/s6 Forward+ aliasing comment in `SSAO_Temporal.hlsl` left authoritative. **Proven:** pre-edit `--shaders` sync (113 up-to-date, 0 failed) → 8 SSAO CSOs backed up to `_backup/stage0123_2026-10-27/cso_preL14/`; post-edit `--shaders` 2 compiled / 0 failed / 0 unexplained; MD5 all 8 CSOs **byte-identical** to baseline (SSAO 18980 B, SSAO_Temporal 21868 B, SSAO_Upsample 1016 B, SSAO_BilateralBlur 912 B, SSAO_VertexDepth 5876 B, SSAO_ps20 12560 B, SSAO_ps20_depthonly 240 B, SSAO_ps20_simple 1372 B) — no fxc disassembly diff needed.

<!-- L15 -->
## L15 — Dead shader source cleanup
Owns: `tools/fast_build.py`, `shaders/**` (deletions only)
- [x] Delete the dead shader sources proven unreachable: the subdirectory legacy copies (`shaders/ps/xbox/`, `shaders/ps/mobile/`, `shaders/vs/xbox/`, `shaders/vs/ps2/`, `shaders/vs/mobile/` duplicates) and the 4 dead includes (`CommonMath.hlsl`, `PerlinNoise.hlsl`, `CarPaintNoise.hlsl`, `SubsurfaceScattering.hlsl`) — each with a reachability grep first (no `#include` from any compiled entry, no fast_build row, no RC row).
- [x] Keep every source with an RC row or a live include (GTAIV prebuilts, `SMAA.hlsli` feeding `SMAA.cso`, etc.).
- [x] Update `fast_build.py` skip lists + intentional-skip census so `--shaders` reports `0 unexplained`.
- [x] Prove: `python tools/fast_build.py --shaders` → 0 failed / 0 unexplained; `python tools/fast_build.py --verify-embed` gate unchanged; report deleted file list.

---

<!-- GATE -->
## Final gate (orchestrator only — do not tick from lanes)
- [x] All 15 lane boxes ticked (or reconciliation ticked).
- [x] ONE integrated build: `python tools/fast_build.py` → `.cache/msbuild_last.log` shows 0 `error C`/`error LNK`/`fatal error`. (GREEN 2026-10-29 00:39:57, ASI 2,451,456 B, 0 error lines in 15,991 B log)
- [x] Binary string verification + new ASI identity reported. (md5 e7405bb26f0e1f741e9e359aee5e0d06; OK: texdefs plugin registry, texalpha, corona-flush-shield, CullResync, noiseDetail, CoronaShield, BldZW, FADED->OPAQUE, shadow_install, [Mode]; absent: debugmenu_public, installMenu, gDebugMenuAPI, StochasticSamplerPS, Refresh noise tile)
- [x] User notified with what changed and what to test.
