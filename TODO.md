# SkyGFX Plus — Project TODO & Plan

> Persistent working plan. Recovered from OpenCode session `ses_09b16e578ffesKBRwznkIrFhFn`
> ("Fix request") + subagent `ses_09ae74fa9ffejxd0Mw1thynb6i` ("Study SilentPatch hooking pattern").
> Working repo: `E:\dev(dave)\skygfx_plus_expIV` (the `C:\Dev\shaisse_hub\skygfx_plus` copy is STALE/divergent — ignore it).
> Game dir: `E:\games\gtasa_skygfx_plus\` (gta_sa.exe, skygfx.asi/dll).

---

## Golden rules (from user)
- **Always use the hardest / most thorough solution.** Proper C++, no fragile SEH when a correct C++ path exists.
- **Follow RW SDK 3.7 coding conventions.**
- **Stick to SilentPatch's standard** for skygfx compat: SilentPatch already accommodates skygfx in source
  (`ModCompat::SkyGfx` namespace, reads `GetConfig()` export → `Config.version`). Keep that export intact.
- **skygfx must ALWAYS load after SilentPatch** (see Load-order note below).
- **Workflow:** assistant builds + deploys; user runs game and reports; we iterate (bounce).
  If no word from user within **3 minutes of a build**, assistant force-kills `gta_sa.exe`
  (PowerShell `Stop-Process -Name gta_sa -Force`; plain `taskkill /F` mangles the arg here).
- **Normal map + txd `_n` feature = LAST.** Everything else below is good to proceed now.

---

## ✅ DONE (this session)
1. **0x5DA610 recursion crash fixed.**
   - Root cause: `main.cpp` hooked `0x5DA610` (CustomPipeAtomicSetup, a *function entry*) with
     `InterceptCall`, which assumes a `call` site → captured a bogus original → `0xC0000005` /
     stack overflow during model loading.
   - Fix: replaced with injector `function_hooker<0x5DA610, RpAtomic*(RpAtomic*)>` — an
     instruction-accurate trampoline (the same discipline SilentPatch uses). The hook just
     forwards to the real original; the RW rwnormal plugin handles normals. No recursion,
     clean frame exit.
   - Build: Release + Debug both **0 errors / 0 warnings**; DLL+ASI deployed to game dir.
2. **Build system fixes (applied in prior session, still in working tree, uncommitted):**
   - Debug `ResourceCompile` RC2104: `rsc_OriginalFilename="skygfx.dll"` → `\"skygfx.dll\"`.
   - Added `dbghelp.lib` to both configs.
   - Robust crash capture: static `s_crashBuf` + `crash_log()` + `diag_writeMinidump()` (VEH + SEH).
3. **`GetConfig()` export confirmed** returning `Config*` with `version` as first member
   (`VERSION = 0x370` ≥ SilentPatch's `SKYGFX_VERSION_WITH_MOONPHASES = 0x360`) → meets
   SilentPatch compat standard.

---

## 🔲 TODO (in priority order)

### A. Stability / verification (do first)
- [ ] **Confirm game loads clean past `CGame::Initialise`** — user run test. Watch
      `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` for the `=== DllMain complete ===` and no `CRASH:`.
- [ ] **Verify vehicle + vegetation normals actually render** via the rwnormal plugin path
      (in-game visual check). Confirm `RpNormMapPluginAttach()` returns success in the log.
- [ ] **Keep crash capture armed** (VEH + minidump) so any future regression is traceable.

### B. Hooking discipline (SilentPatch standard)
- [ ] **Audit ALL remaining hooks** for correct trampoline vs call-site usage:
      - Function *entries* (jump targets) → `injector::function_hooker` / `MakeHook` (trampoline).
      - `call` sites → `InterceptCall` / `MakeCALL`.
      Ensure no other entry is hooked the wrong way (the 0x5DA610 bug must not repeat).
- [ ] **Document the load-order guarantee**: skygfx.asi sorts alphabetically AFTER silentpatch.asi,
      and skygfx defers all real patches to `InjectDelayedPatches` (called from `DllMain`, i.e. at
      game init, after every ASI `DllMain` has run) → skygfx inherently runs after SilentPatch.
      Keep `InjectDelayedPatches` applied directly from `DllMain` (do NOT re-hook `0x74872D`
      IsAlreadyRunning — that clashes with SilentPatch which hooks the same address).
- [ ] **Verify no duplicate hook installations**: `src/main.cpp` and `src/core/main.cpp` both define
      `DllMain` / `InjectDelayedPatches`. Confirm which is compiled (HOOKS_DISABLED?). Resolve any
      duplicate/conflicting wiring so there is exactly ONE active hook path.

### C. Normal map integration (DK22Pac rwnormal) — **DEFERRED, see rule**
- [ ] (LAST) Consolidate the two competing normalmap integrations: `src/rw/normalmap.cpp` is the
      proper self-contained DK22Pac-based integration. Ensure `main.cpp` does not double-hook
      `0x5DA610`/`0x5D7F40`/`0x5D5B80`.
- [ ] (LAST) Wire the txd `_n` normal-map feature: read normal maps from `_n`-suffixed textures
      and feed them through the rwnormal plugin (per "pretend GTA SA shipped with RW SDK 3.7 rwnormal
      lib" goal). This is the final feature step.

### D. Housekeeping (leave to LAST)
- [ ] **Commit the working tree** (crash fix, vcxproj RC/debug fixes, diagnostics minidump,
      function_hooker trampoline) once verification in A/B passes. This file (`TODO.md`) is already
      committed separately to protect it.

---

## Reference (broader roadmap — see `docs/Future Features.md`)
Not scheduled now; recorded for context:
- ImGui debug menu (deferred — SDK integration, low priority)
- Edge tessellation (experimental)
- Multi-pass vehicle glass with parallax (partial)
- Collision-based edge detection (not started)
- Cloud bleeding on trees / IBL onto vegetation
- Wire skin/hair/vegetation enhancement passes
- Wire normal buffer into building pipe for AO
- Forward+ pipeline migration

## Key addresses / facts
- `0x5DA610` = CustomPipeAtomicSetup (vehicle pipe) — MUST use trampoline, never InterceptCall.
- `0x4C88F0` → `0x5DA610` redirect (upgrade parts vehicle pipe) — fine as-is.
- `0x74872D` = IsAlreadyRunning — SilentPatch hooks this; skygfx must NOT (defer instead).
- `CCustomCarEnvMapPipeline__CustomPipeAtomicSetup` = WRAPPER `EAXJMP(0x5DA610)` (gta.cpp:150) —
  never call from a hook at 0x5DA610 (recursion).
- SilentPatch SkyGfx compat: `ModCompat::SkyGfx::GetConfig(module)` → `GetProcAddress("GetConfig")`
  → reads `Config.version`. Keep `GetConfig` exported and `version` first member.
