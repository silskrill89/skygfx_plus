// physicscfg.h - dedicated sky_physics.ini config service for every physics /
// crash-FX addition (user need m0971). Self-contained TexDefs-style TU: one
// concept, no cross-TU mutable globals, accessors only.
//
// ============================================================================
// FILE FORMAT
// ============================================================================
//   Path : .\sky_physics.ini - the SIBLING of .\skygfx.ini. Resolved from the
//          module path exactly like forwardplus.cpp:160 FpReadDebugViewOnce
//          (GetModuleFileNameA(dllModule) -> same directory, fixed filename),
//          falling back to ".\sky_physics.ini" (process cwd = game dir under
//          the ASI loader) when the module handle is unavailable.
//   Section: [SkyPhysics]
//   I/O  : READ  via GetPrivateProfileStringA/GetPrivateProfileIntA (the
//            forwardplus.cpp:168 / softbody.cpp:548 self-read pattern - lightest
//            in the codebase, no STL, no linb dependency).
//          WRITE via WritePrivateProfileStringA per key (in-place value update:
//            the Win32 profile API preserves the rest of the file, INCLUDING
//            the comment block this module writes on first create - unlike a
//            linb::ini round-trip which drops comments on every save).
//          AUTO-CREATE: when the file is missing, PhysicsCfgLoad writes the
//            full commented default file (every key + defaults), the same
//            first-run contract as main.cpp readIniFile's fresh-INI branch
//            (main.cpp:6906), just via a dedicated writer. The create is
//            atomic (CreateFileA CREATE_NEW - an existing file is NEVER
//            truncated/rewritten on load). MERGE RULE (INI preservation):
//            after every load, keys MISSING from the file are filled with the
//            resolved default; existing keys are never overwritten - user
//            values always win and stay byte-verbatim on disk.
//
// ============================================================================
// KEYS (all DEFAULT-OFF = current shipped behaviour; [SkyPhysics] section)
// ============================================================================
//   softbodyBackend      string  "off"    ped softbody/ragdoll backend.
//                            "off"    = disabled (default)
//                            "native" = built-in RW velocity-integrator
//                                       softbody (src/entities/softbody.cpp)
//                            "bullet" = RESERVED, not implemented - acts as
//                                       "off" until the Bullet ragdoll port
//                                       lands (docs/plans/2026-10-29-rage-
//                                       bullet-ragdoll-plan.md); see the TODO
//                                       in SoftbodyGetEnabled.
//                            Unknown values normalize to "off" (warn once).
//   softbodyNative       int 0/1  0    LEGACY ALIAS of softbodyBackend. Old
//                            skygfx.ini [SkyGfx] softbodyNative=1 migrates to
//                            softbodyBackend=native on first create of
//                            sky_physics.ini (seeded from the sibling
//                            skygfx.ini); the two stay mirrored in memory and
//                            on save so pre-migration readers keep working.
//   softbodyBudget       int      8    max simultaneously ACTIVE softbody
//                            ragdolls (consumer wiring pending - stored now so
//                            the menu lane and the pool cap share one key).
//   crashFxEnable        int 0/1  0    master gate for the crash-FX set.
//   crashFxTirePops      int 0/1  0    tyre pops on hard vehicle impacts.
//   crashFxBurnoutSmoke  int 0/1  0    burnout tyre smoke.
//   crashFxWheelDetach   int 0/1  0    wheels detach on heavy impacts.
//   crashFxEjectOccupants int 0/1 0    occupants ejected on heavy impacts.
//   crashFxBloodDecals   int 0/1  0    blood decals from crash contacts.
//
// ============================================================================
// SINGLE SOURCE OF TRUTH (chosen this wave)
// ============================================================================
// The live PhysicsCfg value store in physicscfg.cpp IS the source of truth.
// struct Config (skygfx.h) carries NO physics fields this wave, so the MV_*
// profile rows cannot address these keys; modeManager.cpp owns mode
// behaviour through a PARALLEL physics-row table (ModePhysicsValue) that
// applies/restores through the PhysicsCfgSet*/PhysicsCfgGet* accessors with
// the same field-level semantics as the Config tables. sky_physics.ini is
// pure persistence: PhysicsCfgLoad fills the store, PhysicsCfgSave mirrors
// the store back. No second copy of any value lives anywhere else.
//
// MODE OWNERSHIP (modeManager.cpp table, applied on ApplyMode):
//   GTA IV profile OWNS: crashFxEnable=1, crashFxTirePops=1,
//     crashFxBurnoutSmoke=1, softbodyBackend="native"  ("flicking GTA IV mode
//     enables the physics", user need m0971). Switching away restores those
//     keys to the pre-mode (user INI) base, and PS2/Xbox/LegacyPC/Mobile/
//     Modern additionally PIN crashFxEnable=0 so a stale user value cannot
//     survive under a vintage look.
//
// MENU INTEGRATION: the ImGui settings UI gets its own physics tab/section
// (checkbox rows bound to these accessors + a Save button calling
// PhysicsCfgSave) - that is a LATER designer lane, deliberately not wired
// here. main.cpp hook wiring (when to Load/Save) is likewise the main.cpp
// lane; every accessor lazy-loads on first use so no call-order contract is
// required from either lane.
//
// Threading: game/main thread only (same contract as every other module's INI
// access in this codebase).

#pragma once

// Idempotent one-shot load (+ first-create of the commented default file).
// Safe to call repeatedly; every accessor also lazy-calls it internally.
void PhysicsCfgLoad(void);

// Mirror the live value store back to sky_physics.ini (in-place per-key
// write; comments and unknown keys in the file are preserved; keys whose
// on-disk value already matches the store are never rewritten). Lazy-loads
// first so a save before any load never writes a default file over a user
// file unread.
void PhysicsCfgSave(void);

// True once a load (or first-create) has completed.
bool PhysicsCfgIsLoaded(void);

// ---- typed accessors (unknown keys never crash: get returns def, set warns
// ---- once via dbglog_throttle and ignores) ----

// Current backend token, always one of "off" | "native" | "bullet"
// (normalized; the returned pointer is a stable static buffer).
const char *PhysicsCfgGetBackend(void);
// Set + normalize the backend token (case-insensitive; unknown -> "off").
// Keeps the legacy softbodyNative int slot mirrored (1 iff "native").
void PhysicsCfgSetBackend(const char *backend);

int   PhysicsCfgGetInt(const char *key, int def);
float PhysicsCfgGetFloat(const char *key, float def);
void  PhysicsCfgSetInt(const char *key, int value);
void  PhysicsCfgSetFloat(const char *key, float value);
