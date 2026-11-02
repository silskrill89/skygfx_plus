# docs/plans — Index & Crash-Resume Procedure

Last updated: 2026-11-02 (recon-pass reconciliation: 2026-11-01/02 rows indexed).

Status legend: **ACTIVE** = current guidance / still authoritative · **SUPERSEDED** = replaced by a
newer doc, kept for history · **ARCHIVE** = executed, deferred, or reference-only.

| File | Date | Status | Purpose | Superseded by / notes |
|---|---|---|---|---|
| `2026-07-30-ragdoll-restoration.md` | 2026-07-30 | ARCHIVE (deferred) | Ragdoll physics restoration, Phases 1-4 (bone nodes, joints, collision). | No successor; still in backlog (progress.md "Pending — Ragdoll"). |
| `2026-08-01-remaining-roadmap.md` | 2026-08-01 | SUPERSEDED | Global TODO roadmap as of Aug 1 (branch `experimental`, HEAD 535cde7). | `2026-10-08-iv-v-graphics-physics-port.md`. |
| `2026-09-10-libertyrecomp-postfx-port.md` | 2026-09-10 | ARCHIVE (executed/reference) | LibertyRecomp PostFX port reconciliation + ordered implementation (SSAO, HeightFog, GodRays, tonemap, SMAA). | No successor; effects largely landed — reference only. |
| `2026-10-08-burst-waves-execution.md` | 2026-10-08 | ARCHIVE (schedule absorbed) | Burst-wave delivery schedule (W1-W12, 4-8 lanes, Halloween ship target). | Sequencing absorbed by later wave plans; feature matrix lives in the master plan. |
| `2026-10-08-iv-v-graphics-physics-port.md` | 2026-10-08 | ACTIVE | Master feature matrix: GTA IV/V graphics port, Mode Manager, Bullet physics. | Supersedes `2026-08-01-remaining-roadmap.md`. |
| `2026-10-08-restructure-2026-10-08.md` | 2026-10-08 | ACTIVE (reference) | Architecture restructure to modular feature-domain layout; god-file split plan. | No successor. |
| `2026-10-17-systematic-dig.md` | 2026-10-17 | ACTIVE (process) | Build-gate-first + recon-lane + reconcile workflow; commit ordering. | No successor. |
| `2026-10-20-video-evidence-wave3-plan.md` | 2026-10-20 | SUPERSEDED | Crash recovery + video evidence B1-B5 + wave-3 diagnosis/fix plan (L1-L6, Lane N). | `2026-10-21-alpha-reversal-findings.md` (alpha/water/washout re-root-caused). |
| `2026-10-21-alpha-reversal-findings.md` | 2026-10-21 | ACTIVE | Alpha-state map, water/washout schematic, shader output-alpha audit, remaining issues. | Supersedes the wave-3 plan's B1-B5 diagnosis. |
| `2026-10-24-imgui-settings-coverage.md` | 2026-10-24 | ACTIVE | ImGui settings coverage: 436 Config fields, 235 rows, parser/save/clamp defects, menu plan. | No successor (latest). |
| `2026-10-25-debug-strategy-parity.md` | 2026-10-25 | ACTIVE (process) | Fortified debug strategy (build identity, logging, 3-layer receipts, byte-verified patches, evidence loop, recovery, gaps) + Rockstar/aap parity checklist + completion backlog (WS-0..WS-6). | No successor. |
| `docs/FEATURE_LIST.md` | 2026-11-01 | ACTIVE (source of truth) | Consolidated 12-section master feature list from ALL project MDs; stale root MDs defer to it (see its §1/§12). | Reconciles PLAN.md / FUTURE_FEATURES.md / CODEBASE_DOCUMENTATION.md — see `2026-10-31-md-reconciliation.md`. |
| `2026-10-30-water-overhaul-spec.md` (+ `water-refs.md`) | 2026-10-30 | ACTIVE | Water overhaul spec: current landed water pipe + the 9 missing features M1-M9, Phases 0-3, INI keys, physics clock contract. | Supersedes the water sections of `2026-10-24-water-tier0-diagnosis.md` / `2026-10-24-water-veil-diagnosis.md`. |
| `2026-10-30-sep3-correlation/` (folder, **6 files** on disk — NOT 7; prompt count was off) | 2026-10-30 | ACTIVE (evidence) | Sep-3 look-regression archaeology: `git-render-pipes`, `git-alpha-corona`, `git-chars-lighting`, `git-texdefs-ground`, `ini-diff-exposure`, `shader-diff`. | Cross-referenced by `2026-10-31-source-inventory.md` D-flags. |
| `../baseline/2026-10-30-test2-0438/findings.md` | 2026-10-30 | ARCHIVE (forensics) | Screenshot forensics, 22 test2-build shots (04:38 session). | Companion to earlier 8-shot review; feeding the test-bisect findings. |
| `../baseline/2026-10-31-stripped-ini-bisect/findings.md` | 2026-10-31 | ACTIVE | Stripped-INI bisect: with ALL repair features OFF core diseases return — proves the shields are the repairs, not the disease; Reset-fix verified (clean detach on 0x7F9A3C). | Frontline for "make core renderer correct WITHOUT shields"; see `2026-10-31-test4`. |
| `../baseline/2026-11-02-stable-config/README.md` | 2026-11-02 | ACTIVE (directive) | STABLE REFERENCE CONFIG (m0283): user's current INI = "most stable, least bugs" = canonical test baseline; optional repair layers (stateBaseline/texAlphaClassify/pedOpaqueContract/dualPass) stay OFF; new features default OFF/inert here; acceptance = no regression at this config. | Replaces Xbox/PS2 as primary test mode (PBR pipeline + waterStyle V). |
| `../baseline/2026-10-31-test4/findings.md` | 2026-10-31 | ACTIVE | Test-4 run (user INI 01:26): `pipeChainEnable=0` kills water pipe core → FFP fallback ×14; shields disabled by user; log receipts (water/corona/PedTex/washout). | Builds on the stripped-INI bisect verdict. |
| `2026-10-31-source-inventory.md` | 2026-10-31 | ACTIVE | Full source inventory: 86 files / 84,609 LOC, dead-code D-flags, merge candidates, config-lane map, ped hook map/bind idiom. | Supersedes `.serena/memories/project-status.md` for inventory facts. |
| `2026-10-31-peds-pipe-design.md` | 2026-10-31 | ACTIVE (design only) | Peds pipe phases 1-4: `src/peds/` layout (peds_pipe/skin/hair/cloth), extraction plan for the default callback, INI keys, SkinSSS binding. | Next work = Phase 1 extraction (main.cpp:1881-2440). |
| `2026-10-31-md-reconciliation.md` | 2026-10-31 | ACTIVE (process) | MD reconciliation record: fixes applied to FUTURE_FEATURES.md / PLAN.md / CODEBASE_DOCUMENTATION.md, coronaZtest + pedAmbientFloor conflict resolutions, sep3 file count correction. | Companion to `docs/FEATURE_LIST.md`. |
| `2026-11-01-sa-font-path.md` | 2026-11-01 | ACTIVE (READ-ONLY constraint) | SA font/text path PE map (opinion doc); consult BEFORE touching font-flush pins; describes an UNCOMMITTED tree state. | Constraint listed in `.opencode/memory-bank/activeContext.md`. |
| `2026-11-02-recon-pass.md` | 2026-11-02 | ACTIVE (recon) | Deployed-vs-committed reconciliation: build 9 (ASI 2,540,032 B, md5 4459b4b7) vs HEAD 898e894 + 413-entry uncommitted tree (waves 1-11); risk register (R1 = staged wave-history commits to a branch) + next actions. | Companion: `.opencode/memory-bank/activeContext.md` (2026-11-02 section). |
| `effects-menu-overhaul.md` | 2026-09-29 | ACTIVE (reference) | Effects + menu overhaul consolidated research; §0 is NEVER-REVERT. | No successor; §0 protected. |
| `questionnaire.md` | 2026-10-01 | ARCHIVE (resolved) | Consolidated user Q&A root causes (A-H) for the Oct-1 fix swarm. | Resolved; no successor. |
| `swarm-deploy-2026-10-01.md` | 2026-10-01 | SUPERSEDED | 8-fixer swarm deployment plan and write-scope pool. | `.serena/memories/swarm-round3-2026-10-01.md` (rounds 1-4 authoritative record). |
| `water-refs.md` | 2026-09-30 | ACTIVE (reference) | Water reference recipe for IV/V variants: Xbox/PC constants, addresses, renderers. | No successor; reference only. |

`README.md` itself is the index and is intentionally not listed as a plan.

---

## How to resume after a crash

Read order (stop when you have enough context):

1. **This README** — plan index + status.
2. `.opencode/memory-bank/activeContext.md` — current build, levers, test-next, open issues.
3. `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` — authoritative wave restore point
   (Wave 4/5 section is the latest).
4. **Latest active plans** — `docs/FEATURE_LIST.md` (consolidated feature checklist) and
   `docs/plans/2026-10-31-source-inventory.md` + `2026-10-30-water-overhaul-spec.md` +
   `2026-10-31-peds-pipe-design.md`; rendering-bug context:
   `2026-10-21-alpha-reversal-findings.md`; menu/config:
   `2026-10-24-imgui-settings-coverage.md`; newest test evidence:
   `../baseline/2026-10-31-test4/findings.md`.
5. `.serena/memories/session-rules.md` + `round6-directives-2026-10-08.md` — dispatch/wave rules.

Then recover lost sessions from the OpenCode DB (read-only; never write to it).

Exact URI (sqlite3 CLI, URI mode + read-only):

```bash
sqlite3 -readonly "file:C:/Users/aaaaaaaaa/.local/share/opencode/opencode.db?mode=ro"
```

Session table query (project directory is stored with forward slashes):

```sql
SELECT id,
       parent_id,
       datetime(time_updated/1000, 'unixepoch', 'localtime') AS updated_local,
       title
FROM session
WHERE directory = 'E:/dev(dave)/skygfx_plus_expIV'
ORDER BY time_updated DESC
LIMIT 40;
```

Python fallback (same URI, `uri=True`):

```bash
python -c "import sqlite3; c=sqlite3.connect('file:C:/Users/aaaaaaaaa/.local/share/opencode/opencode.db?mode=ro',uri=True); [print(r) for r in c.execute(\"SELECT id,datetime(time_updated/1000,'unixepoch','localtime'),title FROM session WHERE directory='E:/dev(dave)/skygfx_plus_expIV' ORDER BY time_updated DESC LIMIT 40\")]"
```

Notes: the DB had 1,767 sessions on 2026-10-24; `parent_id` non-NULL marks subagent sessions.
Session text dumps may also exist under `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\`.
