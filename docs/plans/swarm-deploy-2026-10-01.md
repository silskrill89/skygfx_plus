# Fixer-Swarm Deploy — 2026-10-01

Parallel fixer lane plan for the pipe/effects restructure. 8 interchangeable fixers, one writer per
file scope at a time. Source of truth for defects is `questionnaire.md` (sections A–H) plus the live
backlog.

## Pool model

- Pool of **8 fixer lanes**, fully interchangeable: any lane can take any backlog item.
- A lane owns exactly one write-scope (below) while assigned. Two lanes **never** write the same file.
- When a lane finishes, it is re-tasked with the next backlog item — repeat until no known bugs remain.
- Cross-scope items are split by file, not by symptom, so ownership stays disjoint.

## File-ownership matrix (write scope)

| Lane | Owns (write) | Typical work |
|------|--------------|--------------|
| 1 | `src/render/postfx.cpp` | PostFX chain, SSAO/SSR/tonemap, normal buffer, godrays wiring |
| 2 | `src/core/main.cpp` | INI parse/clamp, hooks, pipeline switch, Ctrl+4 debug menu |
| 3 | `src/render/vehiclePipe.cpp` | Vehicle PBR/modern/PS2/Xbox callbacks, paint/env intensity |
| 4 | `src/render/buildingPipe.cpp` | Building PBR/PS2/Xbox callbacks, ground/white-ground, alpha |
| 5 | `src/entities/chars.cpp` + `src/render/envmap.cpp` | Ped/character path, env map |
| 6 | `shaders/**` | HLSL entry points + includes (SM3.0) |
| 7 | `src/render/neoCarpipe.cpp` + `src/render/forwardplus.cpp` + `src/render/depthhook.cpp` | Neo car pipe, forward+ path, depth hook |
| 8 | `docs/plans/**` | Plans, questionnaire, runbooks only (no source) |

Adjacent files (`pipelinecommon.cpp`, `waterPipe.cpp`, `utilitynoise.cpp`, `SMAA.cpp`, `neo.cpp`, …)
are **not** pre-assigned; a lane picks one up only when it is free and no other lane is writing it.

## NO-LANE-BUILDS rule

- **No lane builds.** Fixers edit and stop.
- After **all** writers for a wave have landed, the **orchestrator runs one unified build**:
  `python tools/fast_build.py --rebuild`.
- Rationale: shared render state + embedded CSO resources make concurrent builds race and produce
  false failures; a single rebuild is the only authoritative gate.
- Fixers report changed files + intent; the orchestrator reconciles first, then builds.

## Wave protocol

Each wave is: **reconcile → build → in-game verify → refill**.

1. **Reconcile** — orchestrator confirms all lanes in the wave stopped, checks no overlapping writes,
   resolves conflicts. No build before this.
2. **Build** — orchestrator runs `python tools/fast_build.py --rebuild` once. Green = shaders + ASI.
3. **In-game verify** — user runs the game against the deployed ASI and confirms the targeted defect.
   Build green ≠ fixed.
4. **Commit gate** — only after user in-game verification passes does the wave get committed
   (standing user order; no commits before verification).
5. **Refill** — free lanes take the next backlog items; repeat until no known bugs remain.

## Deploy sequence

- Orchestrator owns the only build + deploy step (`fast_build.py --rebuild` → deploy ASI).
- Lanes never launch the game; the user performs verification.
- Wave reports: lane → files → intent, plus ASI size/timestamp once built.

## Backlog intake

- Bugs routed from `questionnaire.md` A–H (resolved items are regression guards, not new work).
- Open items in `questionnaire.md` block preset/legacy work until user confirms.
- Optimization and effects work start only once the reference combo (GTAIV building + Mobile vehicle +
  GTA IV water) and Modern are verified flawless.
