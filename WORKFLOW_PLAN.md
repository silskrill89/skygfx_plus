# Workflow Plan — skygfx_plus_expIV

*Reconciled 2026-11-07 (operate-of-now doctrine). Supersedes the 2026-09-15 plan (issue registry / phased-week schedule / single-file lane dicing — removed by standing directive §0.3). Current work items live in `docs/FEATURE_LIST.md` and `docs/plans/2026-11-02-systematic-roadmap.md`; this doc governs HOW work flows.*

## Operating doctrine

### 1. Lanes never build

- Writer lanes edit source only — **never** run `fast_build.py`, MSBuild, or FXC as part of a lane.
- **One integrated build** by the orchestrator, run **after all writers land**:
  ```bash
  python tools/fast_build.py        # incremental (default); --rebuild for clean full
  ```
- Build-race rule (learned 2026-10-20): parallel lane builds corrupt shared outputs. No per-lane builds, ever.

### 2. Integrated build gate → binary verify → deploy

1. **Build gate**: `python tools/fast_build.py` must go GREEN (`.cache/msbuild_last.log` = 0 errors).
2. **Binary string-verify**: confirm receipt/feature tags are present in the built DLL / deployed ASI before release (e.g. `[BUILD]`, `texalpha`, `shadow_install` — tags named per wave; verify `build\Release\skygfx.dll` or `E:\games\gtasa_skygfx_plus\skygfx.asi`).
3. **Deploy**: copy to `E:\games\gtasa_skygfx_plus\skygfx.asi` (done by `fast_build.py`).

### 3. Acceptance → user retest → commit

1. **Receipts in log**: launch and confirm the wave's receipts fire — e.g. `[PedZW]`, `[BldZW]` for the CJ-wave fixes; zero receipts = fix not in the build (receipts rule).
2. **Shot evidence**: screenshot set compared against baseline (test12 forensics: `docs/baseline/2026-11-03-test12/findings.md`).
3. **User retest**: user plays against the stable-config baseline — `docs/baseline/2026-11-02-stable-config/README.md` (no-regression gate).
4. **ONLY THEN commit → push** (with `tools/shot_sync.py` for shot evidence).
   Standing commit gate: unified build → user in-game verification → commit. Never commit on build-green alone.

### 4. Lane execution rules

- **Cluster by subsystem**, not single-file slices (directive §0.3 — file limit lifted; multi-file clustered passes allowed).
- **Never parallelize overlapping writes.**
- **Unify** (directive §0.2): one upload path, one noise system, registry-driven addresses — same constant twice = wrong.
- **Per-system .cpp end state** (directive §0.4): skin system .cpp, ONE noise.cpp (wind+water+trees), physics engines linking systems.
- **ASAP pacing** (directive §0.1): work the remaining list without idle gaps; no deadline, stable-config baseline is the goal line.
- **Write summaries to memory** (directive §0.5): every lane/wave writes its summary to memory-bank + serena — never chat-only.
- Agent dispatch: `python tools/agent_coordinator.py route "<task>" <scope>` (8-agent roster, see AGENTS.md); swarm via `.mimocode/workflows/swarm-dispatch.js`.

Full text: `.serena/memories/standing-directives-2026-11-02.md` (§0).

## Commit hygiene

**NEVER commit:**
- `n1`, `qc`
- `skygfx_plus_expIV.7z`
- `tools/*tabshell*.ps1`, `tools/*cloudstore*.ps1`
- `_backup/`

**NEVER-REVERT:**
- `.serena/memories/` wave records (wave-13, wave-14, … — append-only history)
- `docs/plans/effects-menu-overhaul.md` §0 (standing gates / commit-gate anchor)

## Crash-resume procedure

Follow `docs/plans/README.md` → "How to resume after a crash":

1. `docs/plans/README.md` — plan index + status (stop when you have enough context).
2. `.opencode/memory-bank/activeContext.md` — current build, levers, test-next, open issues (top = newest).
3. `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` — authoritative wave restore point.
4. Latest active plans — `docs/FEATURE_LIST.md` + newest dated plans listed in the README index.
5. `.serena/memories/session-rules.md` + `round6-directives-2026-10-08.md` — dispatch/wave rules.
6. Recover lost sessions from the OpenCode DB **read-only** (exact URI + SQL in the README; never write to the DB).

## Source-freshness hierarchy

When sources disagree, read in this order and stop when context is sufficient:

1. **`docs/plans/README.md`** — plan index, ACTIVE/SUPERSEDED status, crash-resume procedure.
2. **Newest dated plan** in `docs/plans/` (e.g. `2026-11-03-cleanup-pass.md` > `2026-11-02-systematic-roadmap.md`).
3. **`.opencode/memory-bank/activeContext.md`** — top section = newest state.
4. **`.serena/memories/` wave records** — append-only per-wave history.

`docs/FEATURE_LIST.md` §12 is the authoritative MD index of feature state.

## Standing directives (2026-11-02, summary)

1. **ASAP pacing** — no idle gaps; stable-config baseline = goal line.
2. **Unify** — one shared implementation, never per-call-site copies.
3. **File limit lifted** — cluster lanes by subsystem; never parallelize overlapping writes.
4. **Per-system .cpp targets** — skin system, ONE noise.cpp, physics linking systems.
5. **Write summaries to memory** — memory-bank + serena, never chat-only.

Directives EXTEND (not replace): lanes never build · one integrated build · NEVER-REVERT wave records · stable-config acceptance.

## Build reference

```bash
python tools/fast_build.py              # Smart incremental + deploy (default)
python tools/fast_build.py --fastest    # Skip detection, always build+deploy
python tools/fast_build.py --rebuild    # Clean full rebuild + deploy
python tools/fast_build.py --shaders    # Only compile HLSL → CSO
python tools/fast_build.py --launch     # Build + launch game
```

- Deploy target: `E:\games\gtasa_skygfx_plus\skygfx.asi`
- Build log: `.cache/msbuild_last.log` (0 errors = GREEN)
- Diag log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
- Shot sync: `python tools/shot_sync.py`

---

*Reconciled: 2026-11-07. Replaces 2026-09-15 issue-registry plan (stale doctrine removed: weekly phases, per-lane builds, single-file lane dicing, skill-assignment matrix — routing now via tools/agent_coordinator.py).*
