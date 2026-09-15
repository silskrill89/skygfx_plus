# Dispatch queue — current OPEN items in priority order

## P0 — crash re-investigation

| Item | Swarm IDs | Note |
|------|-----------|------|
| 0x7F9ECB NULL-data-slot path (postfx.cpp:4005) | 001 → 017, 068 | Majors first; 017/068 mine logs |
| 0x7F98DF camera-pointer regression (postfx.cpp:3851-3890) | 002, 014 | 014 needs log from user run |
| 0x7FAD4D-class guard sweep (buildingPipe/envmap/vehiclePipe) | 003 | Read-only |

## P1 — release verify

| Item | Swarm IDs | Note |
|------|-----------|------|
| Build+deploy 0 err/0 warn, DLL→ASI | 013 (build allowed), 099 | May run back-to-back, not parallel |
| Shader rebuild --shaders | 097 (build allowed) | After 013 |

## P2 — registry hygiene leftovers

| Item | Swarm IDs | Note |
|------|-----------|------|
| ARCH-3 render-state audit | 004 → 062 | Major defines fix, minor gathers sites |
| ARCH-4 static-locals audit | 005 → 063 | Same pattern |
| MEM-5 raw-new RAII spec | 009 → 057 | Cleanup exists; low priority |
| Rollup patch proposal | 020 → 100 | Orchestrator applies |

## P3 — 9 low-risk defects

| # | Defect | Swarm ID |
|---|--------|----------|
| 1 | MEM-5 (4 raw-new sites) | 009, 057 |
| 2 | ARCH-3 state leaks | 004, 062 |
| 3 | ARCH-4 statics | 005, 063 |
| 4 | UploadUnifiedConstants dead stub (debugmenu_ui:346) | 079 |
| 5 | Unreachable mobile branch (vehiclePipe) | 079 |
| 6 | Review warning: misleading comment (Phase F) | 098 |
| 7 | Review warning: scratch-cam leak (Phase F) | 098 |
| 8 | COLORFILTER_YCBCR unverified | 092 |
| 9 | CSO inventory vs resource.h drift | 096 |

## P4 — TODO-5 III/VC debug menu

- Spec only: swarm-016 (+030, +095). **Blocked** by P1.3 preset framework.

## BLOCKED — do not dispatch

- **TODO.md C normal-map (`_n`/rwnormal): LAST per golden rule.** 046 may
  contract-check the `main_normMapVehicle` surface only; no implementation.
- **In-game verify (088–093 matrices, 014 log evidence): needs user run.**
  Draft matrices now, execute on bounce.
- TODO-1/TODO-2: STALE (wheels_extender deleted, commit 18f5303) — 064
  confirms, no fix dispatched.
