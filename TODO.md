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

## ✅ DONE
1. **0x5DA610 recursion crash fixed** (committed 89be8b7).
   - Root cause: InterceptCall on function entry → bogus original → stack overflow.
   - Fix: InjectHook trampoline. Build 0 errors/0 warnings. DLL+ASI deployed.
2. **Build system fixes** — /FS for PDB contention, dbghelp.lib, crash capture (VEH+minidump).
3. **GetConfig() export confirmed** — version=0x370 ≥ SilentPatch 0x360.
4. **Game boots and runs clean** — skygfx_dbg.log shows 2035+ frames, PBR active, ragdoll init.
5. **Hooking discipline audit complete** (PLAN.md P3) — all hooks documented, no incorrect trampolines.
6. **Dawn brightness fix** — ambient 0.50, direct 0.35, env 0.30 in vehicle shader; IBL 0.15 in building shader; AMBIENT_FLOOR 0.20.
7. **Adaptive tonemap v2** — timecycle-driven exposure+toe, interior/cutscene detection, carcols integration.

---

## 🔲 TODO (in priority order)

### A. Stability / verification ✅ DONE
- [x] **Confirm game loads clean past `CGame::Initialise`** — verified, 2035+ frames, PBR active.
- [x] **Verify crash capture armed** (VEH + minidump) — working.

### B. Hooking discipline ✅ DONE
- [x] **Audit ALL hooks** — documented in PLAN.md P3. No incorrect trampolines found.
- [x] **Document load-order guarantee** — skygfx.asi sorts after silentpatch.asi alphabetically.
- [x] **Verify no duplicate hook installations** — src/core/main.cpp is the active file; src/core/hooks.cpp is dead code.

### C. Normal map integration (DK22Pac rwnormal) — DEFERRED, see rule
- [ ] (LAST) Wire the txd `_n` normal-map feature: read normal maps from `_n`-suffixed textures
      and feed them through the rwnormal plugin. This is the final feature step.

### D. Visual quality refinement (CURRENT WORK)
- [x] Vehicle PBR specularity/metallic/env reflections improvement — clearcoat Fresnel, metallic energy conservation, ambientObj
- [x] Ground/street brightness at dawn — ambientObj timecycle, removed hardcoded floor
- [x] Sun flare fix — sunCoronaIntensity (0.4), sunCoreIntensity (0.6) INI controls, unified tonemap
- [ ] CJ character material depth
- [ ] Glass shader transparency

### E. Housekeeping
- [x] **Full roadmap plan** — `docs/plans/2026-08-01-remaining-roadmap.md` (Phases 0-5, 25 safety issues, all TODOs consolidated)
- [ ] **Update PLAN.md** — mark P2/P3 done, add visual refinement tracking.

---

## Full Roadmap

See **`docs/plans/2026-08-01-remaining-roadmap.md`** for the complete implementation plan covering:

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Safety hardening (25 issues: normalize, div-by-zero, strcat, sprintf, raw new) | ❌ NOT STARTED |
| 1 | Restore full compatibility (verify 9+2 pipes, dead stubs, YCbCr) | ❌ NOT STARTED |
| 2 | Visual quality remaining (CJ depth, glass transparency, sun streaks) | ❌ NOT STARTED |
| 3 | Architecture cleanup (config refactor, dedup, render state leaks) | ❌ NOT STARTED |
| 4 | Platform selection UI (vehicleStyle/buildingStyle/colorStyle INI + menu) | ❌ NOT STARTED |
| 5 | Advanced features (Forward+, skin/hair/veg, multi-agent) | ❌ NOT STARTED |
| C | Normal map integration (DK22Pac) — **LAST per user rule** | 🔒 DEFERRED |

## Key addresses / facts
- `0x5DA610` = CustomPipeAtomicSetup (vehicle pipe) — MUST use trampoline, never InterceptCall.
- `0x4C88F0` → `0x5DA610` redirect (upgrade parts vehicle pipe) — fine as-is.
- `0x74872D` = IsAlreadyRunning — SilentPatch hooks this; skygfx must NOT (defer instead).
- `CCustomCarEnvMapPipeline__CustomPipeAtomicSetup` = WRAPPER `EAXJMP(0x5DA610)` (gta.cpp:150) —
  never call from a hook at 0x5DA610 (recursion).
- SilentPatch SkyGfx compat: `ModCompat::SkyGfx::GetConfig(module)` → `GetProcAddress("GetConfig")`
  → reads `Config.version`. Keep `GetConfig` exported and `version` first member.
