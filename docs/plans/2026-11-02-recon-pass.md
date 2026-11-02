# 2026-11-02 — Recon pass: deployed-vs-committed reconciliation

Type: READ-ONLY recon + docs updates (no source edits, no build, no commit).
Branch `very-experimental`, HEAD `898e894`. Complements `docs/FEATURE_LIST.md` (§12 index) and
`.opencode/memory-bank/activeContext.md` (updated same day).

---

## 1. Current state (verified against reality)

| Item | Verified value |
|---|---|
| HEAD | `898e894` — "docs(ini): mark external Win32-read keys as UNRESOLVED defaults" |
| Uncommitted scale | 413 entries = 173 tracked-modified (27,394 insertions / 25,917 deletions) + 240 untracked (re-verified second pass) |
| Deployed ASI | `E:\games\gtasa_skygfx_plus\skygfx.asi` — **2,540,032 B, md5 `4459b4b7a3686cc7a8b46a77e34995d4`, mtime 2026-11-02 12:39** (= build 9) |
| Wave count landed | Waves 1-11 (user count, 2026-11-02) — most recent src edits 12:23-12:32 Nov 2 (vehiclePipe, postfx*, waterPipe, buildingPipe, main.cpp, softbody) |
| Last run receipts | `skygfx_dbg.log` (6.4 MB) run ending 12:51 Nov 2: PBR pipeline live `pipes(bld=3 veh=10)`, SMAA on, `pipeUploadPBR` c22/c23 normal, `font_flush gate pinned`, clean `DLL_DETACH complete` — no crash/watchdog in tail |
| Live config | `docs/baseline/2026-11-02-stable-config/README.md` — `[Modes] active = None`, PBR pipeline; optional repair layers OFF (stateBaselineEnable=0, texAlphaClassify=0, pedOpaqueContract=0, dualPass*=0) |
| Memory layers | activeContext.md updated 2026-11-02 (was stale at Oct 29); agentmemory store EMPTY (0 sessions) |
| build/Release | Not on disk (build intermediates cleaned; deploy-direct from fast_build pipeline) |

## 2. What's deployed vs committed — the gap

- **Deployed (build 9)** = working tree as of 12:39 Nov 2, containing waves 1-11 (post-fix-94).
- **Committed** = HEAD `898e894`, which by message lineage is docs/INI generation commits —
  the last code-wave commits predate the Oct-29 final-gate wave.
- **Gap** = everything landed after HEAD: ~11 waves of shader/C++ fixes + doc updates +
  merged CSOs live ONLY in the working tree. Committed tree ≠ playable ASI.
- Wave archaeology for Oct 29 → Nov 2 (waves 9-11 area) has no memory-bank records;
  evidence base: src mtimes, deployed md5/size series (2,451,456 → 2,540,032 B), dbg log,
  and the 2026-11-01/02 baseline dirs. Reconstruct before committing (§5).

## 3. Risk register

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | **Uncommitted work = one crash from loss.** 27k+ insertion working tree with zero checkpoints; any editor/disk/git accident is unrecoverable. | HIGH | **Recommended: staged commits of wave history to a branch** — e.g. `wave-history/very-experimental` receiving one checkpoint commit per reconstructable wave, ordered oldest-newest, wt-tree left untouched between commits (commit from a snapshot index, not from the dirty tree). |
| R2 | Hidden crash/vector: last run clean, but PBR is now the top-level pipeline AND the stable-config runs without TAC/pedOpaqueContract/stateBaseline shields — a regression introduced in waves 9-11 by those OFF layers interacting was last observed pre-wave-9. | MEDIUM | One upcoming user run with the stable config + fresh log tail check per session. |
| R3 | Two open code decisions can invalidate visuals mid-stream: CopySceneToTexture HRESULT (depth-stencil detach/handback class) and ps2ModulateGlobal cross-slot (config-vs-c). | MEDIUM | Decide explicitly; both decision locations are linked from activeContext 2026-11-02. |
| R4 | rs_mon_report 100% diverge at scene_start (samples=13885, bad=0) — unexplained; benign today, but the monitor's purpose (disagree = desync) is currently contradicted by run stability. | LOW-MED | One-off rs_mon field trip next wave; otherwise recalibrate the comparison gate. |
| R5 | Wave records gap: memory layers miss Oct 29 → Nov 2; both doc header values (waves/build index) are user-declared, not independently reconstructed. | LOW | Fold into R1 checkpoint commits (commit messages double as wave receipts). |

## 4. Next actions (suggested order)

1. **Commit checkpoints (R1)** — reconstruct wave boundaries oldest-first and checkpoint-commit
   off the dirty tree (per-`docs/plans/2026-10-17-systematic-dig.md` commit-ordering rules).
2. Reconcile `[Modes] active = None` handling in modeManager (stable config runs profile-less —
   confirm None-profile is a first-class no-op on the uncommitted tree).
3. Clean up the two decision items (CopySceneToTexture HRESULT, ps2ModulateGlobal slot).
4. peds_pipe Phase 3 (hair) — design doc §4; verify Phase 1/2 landed state first.
5. Water M4 → M9 in spec phase order, stable-config as acceptance gate.
6. Gibs render fix + unified* cleanup (low-risk, mechanical, good lane material).

## 5. Files written/updated by this pass

- `.opencode/memory-bank/activeContext.md` — new Current state / Remaining work / Constraints (2026-11-02) + stale-first archaeology note.
- `docs/plans/2026-11-02-recon-pass.md` — this file.
- `docs/plans/README.md` — index rows for this doc + 2026-11-01 sa-font-path doc + 2026-11-02 stable-config baseline; "Last updated" bumped (first pass missed this — applied in the second reconciling pass).

Second pass of the day (memory-reconcile, same date) additionally touched: `.serena/memories/receipt-health.md` (new), `.serena/memories/standing-directives-2026-11-02.md` (new), `.opencode/memory-bank/activeContext.md` (log-hygiene waves + refreshed remaining-work list), `.opencode/skills/memory-octree/SKILL.md` (EP-9 + receipt-grep table + EP-1 baseline), §7 above.

## 6. Constraints acknowledgement

- Directive m0283: 2026-11-02 stable-config = test baseline; no feature may "fix" it back on.
- Belonging investments already documented are unaffected by this pass.

## 7. Late addendum (2026-11-02): statesync + mb_pssplit receipts

Two fresh receipt observations from the latest runs are too new for the older per-wave docs;
recorded here until the next wave docs absorb them. Also mirrored as the healthy-vs-broken
tag table in `.serena/memories/receipt-health.md` and triaged via EP-9 in
`.opencode/skills/memory-octree/SKILL.md`.

### statesync — `diverged — healed`

- Receipt: the statesync gate emitted `diverged` between cached/world state and live device
  state, then immediately an `healed` line on the follow-up frame(s).
- Verdict: benign / self-healing. The repair path is being exercised and recovering without
  customer-visible defect (no washout/black-class artifact correlation in the same window).
- Watches: if a `diverged` line ever appears with NO subsequent `healed` receipt in the same
  run, that is the broken signature — open a lane (route via EP-9).

### mb_pssplit — `foreign PS before bind`

- Receipt: a foreign pixel shader observed bound at the motion-blur ps-split bind site before
  the split's own PS bind lands.
- Verdict: OPEN defect observation (broken signature — expected count ZERO). Not yet
  reproduced against a visual artifact; treat as a one-frame/undef second-pass risk until the
  bind ordering is proven.
- Next action: route as part of the next postfx-clustered lane (clustered per the §0 standing
  directives — multi-file passes allowed); on fix, assert absence: `rg "mb_pssplit" skygfx_dbg.log`
  returns zero hits.

Both tags join the EP-9 receipt-grep quick reference; run-to-run invariant checks stay in
receipt-health.md.
