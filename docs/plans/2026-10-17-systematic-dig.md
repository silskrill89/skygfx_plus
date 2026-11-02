# Systematic Dig — Process Documentation (2026-10-17)

Methodology used to get to the bottom of the in-game breakage, recorded for workflow optimization.

## Process (what was done, in order)

1. **Build gate first** — every fix wave ends with `python tools/fast_build.py` before any diagnosis. A fix that doesn't build is noise (caught: ivModeLegacyRowEdit fwd-decl missing → C2065 → fixed → GREEN 6.24s).
2. **Two parallel read-only recon lanes** (explorer, no writes):
   - **Log lane**: inventory ALL logs in game dir (skygfx_dbg.log, .prev.log, gta_bridge_session.log, dmp files) → sample head/tail + grep error tags → build a timeline → map findings to the known-bug registry (.serena/memories/round5-bugs-2026-10-08.md).
   - **Git lane**: `git log --oneline -30` timeline → `git status/diff --stat` uncommitted inventory grouped by subsystem → per-known-bug: does the fix marker exist in HEAD or only the tree? → stash/dangling audit → regression-risk scan (tree vs HEAD contradictions).
3. **Reconcile to disk** — findings appended to round6-directives before any fix dispatch (session rule A).
4. **Fix only what has evidence** — dispatch fixer lanes per confirmed root cause, not per suspicion.

## Key findings (evidence-based)

- **The 5 known bugs could NOT have manifested in the user's test sessions**: config ran colorFilter=0, ssaoEnable=0, ivMode=0 → bugs 1/2/4 untestable. "A lot was broken" ≠ those bugs.
- **Real breakage = Forward+ half-wired**: light collection `added 0` always; all 8160 tiles lit from ≤10 lights (75k instances); Xbox building cb fp VS/PS handles NULL; CoronaFeed feeds 0.
- **SMAA velocity never wired**: DrawVelocityBuffer RUNs but velocityTex=NULL at SMAA Pass3 → ghosting (bug 3) persists.
- **Perf collapse (9.4 fps) largely self-inflicted**: postfxDumpDebug=1 left ON → 14 BMP dumps/frame ×6.2MB + stuck frame counter in menu → 590ms spikes. Turn OFF for perf sessions.
- **All fixes live only in the uncommitted tree** (last commit Oct 2). 139 files uncommitted = biggest project risk. Nothing lost in stashes/dangling commits. `_backup/uncommitted.patch` is STALE — do not trust.
- **VEH "CRASH" ×20/session = 0x40010006 OutputDebugString noise**, not real crashes (both skygfx and Earshot VEH handlers log it).

## Workflow optimizations (for later)

1. **Commit checkpoints per wave** — dependency order: postfx split+bug1-3 → FP gating+atlas → modeManager+menus → water/veg/infra. Never commit: n1, qc, *.7z, tabshell/recon ps1, _backup/.
2. **Test-session config contract** — user retests must run with the features under test ON (colorFilter=8, ssaoEnable=1, ivMode=1) or the log dig is blind. Record the config used per test.
3. **postfxDumpDebug hygiene** — default OFF; only enable for named-writer hunts (e.g. cam-raster foreign swap).
4. **Profiler scope table** — 32 slots overflow 657k×/session; enlarge or aggregate.
5. **VEH watchdog** — filter 0x40010006 (OutputDebugString) from "CRASH" logging.
6. **Board labels** — fixed via oh-my-opencode-slim patch (claim-aware peekByParent); verified live.
7. **Session rule A works** — per-wave disk reconciliation kept everything recoverable across 2 PC crashes + 1 relaunch.

## Next fix targets (evidence-backed)

1. Forward+ light collection (`added 0`) + cluster degeneracy + fp VS/PS creation in Xbox cb path.
2. SMAA velocityTex → Pass3 wiring.
3. Then ONE user retest with colorFilter=8 + ssaoEnable=1 + ivMode=1 + postfxDumpDebug=0.
