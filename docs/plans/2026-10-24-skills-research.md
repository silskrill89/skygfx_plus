# Skills Research & Studio-Role Operating Model

**Date:** 2026-10-24
**Status:** Research complete. No skills installed, no configs edited, no code changed.
**Scope:** Find current (2025-2026) agent-skill packages that fit `skygfx_plus_expIV`; reconcile the existing skill/agent/memory systems; define an 8-agent wave protocol for a remaster proof-of-concept.
**Evidence:** local read-only inventory + GitHub API license checks + official OpenCode/Agent Skills docs + upstream READMEs/SKILL.md files. All URLs and license claims were checked on 2026-10-24.

> **Install lane is intentionally deferred.** Every command in Section 8 is a recommendation for a later, clean checkpoint. The working tree was dirty at audit time (`git status --short` showed modified build, resource, shader, and docs files). Do not install into a dirty tree.

---

## 0. Executive summary

1. **Best single addition: `satk` (San Andreas ToolKit).** MIT, GTA SA-native, indexes IMG/DFF/TXD/COL/IDE/IPL/IFP, resolves `gta_sa.exe` crash addresses to `gta-reversed` functions, exposes a viewer with camera bookmarks/captures, and ships an MCP server plus a `SKILL.md`. This directly replaces the weakest part of the current stack: ad-hoc game/RE lookup.
2. **Best process addition: `superpowers`.** MIT, 20+ composable skills, explicitly supports OpenCode. Its `writing-plans` -> `subagent-driven-development` -> `verification-before-completion` chain is the closest published match to the requested 25-minute design / 10-minute pass / 5-minute pass cadence.
3. **Best build addition: `low-level-dev-skills`.** MIT. The relevant subset is `msvc-cl`, `debug-optimized-builds`, `core-dumps`, `build-acceleration`, `pgo`, `static-analysis`. It covers MSVC/MSBuild/PDB/LNK issues that the current CMake-oriented skills do not.
4. **Best GPU evidence addition: `renderdoc-cli`.** MIT. Useful for D3D9 pipeline state, bindings, constant buffers, render-target export, and capture diff. **Important limitation:** RenderDoc source-level shader debugging supports D3D11/D3D12/Vulkan only, not D3D9. For SM3.0 HLSL stepping, a project-local skill is required.
5. **Biggest real gap: SM3.0/D3D9 shader forensics.** No high-quality, maintained online skill was found for `ps_3_0`/`vs_3_0`, FXC instruction limits, constant-register contracts, and D3D9 capture/debug. The existing `shader-development-toolkit` is GLSL/ShaderToy-only; `sm30-shader-safety` is project-specific and should remain the authority.
6. **Existing agents map closely to the studio roles.** The eight `.mimocode/agents/*.md` files cover engine, graphics, postfx, audit, crash, build, research, and knowledge. The missing hats are Tools Engineer, Technical Artist, Producer, and Technical Writer as first-class roles. Re-badge the eight wave slots; do not create eight new agent types (custom roster agents are not dispatchable per `.serena/memories/agent-tiers.md`).
7. **Skill sprawl is now a real risk.** 58 skill directories across three roots, 57 `SKILL.md` files, plus a 122 KB `skills.7z` and a legacy `.mimocode/skills` file. Several names collide (`systematic-debugging`) or duplicate Superpowers (`brainstorming`, `writing-plans`, `test-driven-development`, `requesting-code-review`). Target ~30 active skills, one owner and one root per name.
8. **Several installed skills should be marked blocked, not deleted.** `performance-profiling` is Linux perf/eBPF; `address-sanitizer`/`sanitizers` are not practical inside an injected 32-bit D3D9 game without a separate harness; `cmake` is irrelevant to a premake/MSBuild project; `shader-development-toolkit` is GLSL-only. Keep them as reference, never route render work to them.
9. **The operating model must move from Q&A to artifacts + evidence + gates.** Every wave produces a brief, eight lane cards, one build, one evidence bundle, and a keep/revert decision. Senior-dev behavior is enforced by `verification-planning`, `verification-before-completion`, `docs-sync-audit`, and `harness-engineering`, not by longer prompts.
10. **Recommended cadence:** 25-minute Designer Sprint, 10-minute execution pass, 5-minute reconciliation pass. Once two consecutive waves are green, compress to 5/5/5 and allow 1-5 minute single-symptom diag/fix loops. Any regression falls back one level.

---

## 1. Method and fit criteria

### 1.1 What was checked

- **Local skill roots:** `C:\Users\aaaaaaaaa\.agents\skills`, `C:\Users\aaaaaaaaa\.config\opencode\skills`, `E:\dev(dave)\skygfx_plus_expIV\.opencode\skills`, `C:\Users\aaaaaaaaa\.claude\skills`, `.mimocode/skills`.
- **Local agent systems:** `.mimocode/agents/*.md`, `.serena/memories/agent-tiers.md`, `.serena/memories/agent-skills.md`, `.serena/memories/session-rules.md`, `tools/swarm/roles.json`, `tools/agent_coordinator.py`.
- **Local project docs:** `AGENTS.md`, `WORKFLOW_PLAN.md`, `TODO.md`, `docs/plans/README.md`, `docs/plans/2026-10-24-refactor-blueprint.md`, `docs/plans/2026-10-24-imgui-settings-coverage.md`, `docs/plans/2026-10-08-burst-waves-execution.md`.
- **Official format docs:** OpenCode Agent Skills page (`https://opencode.ai/docs/skills/`) and Agent Skills specification (`https://agentskills.io/specification`).
- **Upstream repos:** GitHub API `/repos/{owner}/{repo}` for license, stars, last push; raw `README.md`/`SKILL.md` for install format and behavior.

### 1.2 Fit criteria (in order)

1. **Engine-native:** understands GTA SA, RenderWare 3.6, D3D9, SM3.0, ASI loading, or MSVC/premake.
2. **Evidence-producing:** returns `file:line`, log tag, crash address, screenshot, capture, or build metric, not a summary.
3. **Role-shaped:** has a clear owner among the eight studio roles and a bounded write scope.
4. **Installable without a platform rewrite:** `SKILL.md` folder format or an OpenCode plugin; no CMake conversion, no Linux-only toolchain.
5. **License-clean:** MIT or Apache-2.0 preferred; no Commons Clause, no unclear redistribution terms.
6. **Low context cost:** description specific enough to trigger, body under ~500 lines, references loaded on demand.

### 1.3 Risk scale

| Risk | Meaning |
|---|---|
| None | Documentation/reference only, no scripts, no writes outside its own folder. |
| Low | Instructions plus small scripts; read-only with respect to the game and repo; MIT/Apache-2.0. |
| Medium | Native build step, game/input automation, autonomous git operations, large dependency surface, or browser/OS assumptions that need adaptation. |
| High | Writes to the game install, kernel/anti-cheat/online interaction, unclear license, or unreviewed executables. **None of the top 10 are high.** |

---

## 2. Top 10 recommendations

### 2.1 Summary table

| # | Pack | Source / license | What it adds | Why it fits this project | Install target | Risk |
|---|---|---|---|---|---|---|
| 1 | **satk** | https://github.com/qxlies/satk-gta / MIT | GTA SA asset index, RE symbol DB, viewer captures, mod lint, MCP + SKILL.md | `re_addr` resolves crash addresses like `0x7F9ECB`; `re_src` reads `gta-reversed`; `asset_refs` maps DFF/TXD; `view_capture` gives reproducible frames | `C:\Users\aaaaaaaaa\.agents\skills\satk` | Low |
| 2 | **superpowers** | https://github.com/obra/superpowers / MIT | Brainstorm -> plan -> subagent-driven execution -> TDD -> review -> finish; OpenCode plugin | Published match for 25-min design, 10-min pass, 5-min reconcile; `verification-before-completion` kills false "done" | OpenCode plugin (not a skill folder) | Low |
| 3 | **low-level-dev-skills** | https://github.com/mohitmishra786/low-level-dev-skills / MIT | `msvc-cl`, `debug-optimized-builds`, `core-dumps`, `build-acceleration`, `pgo`, `static-analysis` | MSVC/vcxproj/MSBuild, PDB/LNK triage, 173-TU build acceleration; matches `fast_build.py` reality | `C:\Users\aaaaaaaaa\.agents\skills\{msvc-cl,debug-optimized-builds,core-dumps,build-acceleration}` | Low |
| 4 | **renderdoc-cli** | https://github.com/FindTea/renderdoc-cli-skill / MIT | 60+ command GPU capture CLI + OpenCode skill | D3D9 pipeline/bindings/cbuffer/RT export and capture diff; no D3D9 source shader debug | `C:\Users\aaaaaaaaa\.agents\skills\renderdoc-cli` | Medium |
| 5 | **imgui-ui-ux-engineering** | https://github.com/muhosekerci/imgui-ui-ux-engineering / MIT | ImGui 1.92+ design, ID-stack, DPI, clipper, tables, regression checks | `debugmenu_ui.cpp` is ~3,220 lines, ~315 rows, known ID/DPI/device-reset defects | `C:\Users\aaaaaaaaa\.agents\skills\imgui-ui-ux-engineering` | Low |
| 6 | **universal-modder** (game automation subset) | https://github.com/rehan-remade/universal-modder / MIT | `game-automation`, `game-recon`, `reverse-engineering`, `port-a-mod` | Launch/screenshot/drive a real Windows game; GPU-safe capture; engine/loader recon | `C:\Users\aaaaaaaaa\.agents\skills\{game-automation,game-recon,reverse-engineering,port-a-mod}` | Medium |
| 7 | **game-qa + test-gap-audit** | https://github.com/Bulugulu/game-qa / MIT; https://github.com/github/awesome-copilot / MIT | Five-role PM-driven QA loop; read-only coverage-gap audit | Separates author/executor/reviewer; evidence bundles; adapt runner to `satk view_capture` + `um win shot` | `docs/skills/vendor/game-qa` (reference), `C:\Users\aaaaaaaaa\.agents\skills\test-gap-audit` | Medium |
| 8 | **github-release + docs-sync-audit + documentation-writer** | https://github.com/github/awesome-copilot / MIT | SemVer/changelog/tag/CI release; docs-vs-code drift audit; Diátaxis writing | ASI release train; 40+ docs with stale plans; release notes and ADR discipline | `C:\Users\aaaaaaaaa\.agents\skills\{github-release,docs-sync-audit,documentation-writer,create-architectural-decision-record}` | Low |
| 9 | **autoresearch** | https://github.com/github/awesome-copilot/tree/main/skills/autoresearch / MIT | Autonomous goal/metric/baseline/keep-discard loop with git rollback | 1-5 minute diag/fix once stable, measured by build time/ASI size/crash count/visual diff | `C:\Users\aaaaaaaaa\.agents\skills\autoresearch` | Medium |
| 10 | **skill-creator** (+ Agent Skills spec) | https://github.com/anthropics/skills/tree/main/skills/skill-creator / Apache-2.0; https://agentskills.io/specification | Create/modify/evaluate skills; description optimization; progressive disclosure | The highest-value skills for this project were not found online in a fitting form (SM3/D3D9 forensics, wave scheduler, state doctrine) | `C:\Users\aaaaaaaaa\.agents\skills\skill-creator` | Low |

### 2.2 Why each one fits

#### 1. satk — the GTA SA evidence layer

- **Freshness:** v0.4.0 public preview; repo pushed 2026-10-09; MIT.
- **What it does:** one Python CLI (`satk`) and one MCP server. Commands include `formats`, `index`, `asset`, `world`, `texture`, `model`, `re`, `crash`, `kb`, `view`, `ingame`, `mod`, `script`, `anim`, `limits`, `shader` (MTA), `blender`, `engine`.
- **Project fit:** the symbol DB is built for `gta_sa.exe` 1.0 US, the exact target. `satk re_addr "0x7F9ECB"` maps a crash log address to a function/file/line; `satk re_src cped::update` reads `gta-reversed`; `satk kb` gives engine structs/opcodes; `satk asset refs model:411` traverses DFF/TXD/handling; `satk view_capture` writes PNG + sidecar from a bookmark; `satk index diff --kind model` compares vanilla vs installed profiles.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\satk` via `satk agent install-skill --client codex`. The package itself should live outside the repo, e.g. `C:\tools\satk`, because it creates a `work/` index.
- **Guardrail:** satk refuses writes to the game folder by design (`PROTECTED_PATH`). Point it at `E:\games\gtasa_skygfx_plus` for the `installed` profile; do not let it write there.
- **License caveat:** satk itself is MIT, but its `NOTICE.md`/credits list GPL-3.0 components and CC-BY-4.0 wiki data. Internal use is fine; if the remaster is distributed, review the notices before shipping satk-derived data.
- **Acceptance:** `satk status` shows `ok`; `satk re_addr "0x7F9ECB"` returns a symbol; `satk asset find infernus` returns `model:411`; one `satk view_capture` produces a PNG and sidecar.
- **Caveat:** satk's `shader` command targets MTA shaders, not SM3.0 HLSL. It is not a replacement for `sm30-shader-safety`.

#### 2. superpowers — the wave operating system

- **Freshness:** v7.0.1 released 2026-10-21; repo pushed 2026-10-23; MIT; 300k+ stars.
- **What it does:** a composable methodology. Core path: `brainstorming` -> `using-git-worktrees` -> `writing-plans` -> `subagent-driven-development` or `executing-plans` -> `test-driven-development` -> `requesting-code-review` -> `finishing-a-development-branch`. Also includes `verification-before-completion`, `dispatching-parallel-agents`, `writing-skills`, `diagnosing-superpowers`.
- **Project fit:** `writing-plans` produces bite-sized tasks with exact files, signatures, and verification; `subagent-driven-development` dispatches a fresh subagent per task with two-stage review; `verification-before-completion` directly addresses the project's recurring "claimed done but visually broken" failure. The 25/10/5 wave clock is a project-specific wrapper around these skills.
- **Install target:** OpenCode plugin entry, not a copied skill folder. On this machine the active project config is V1-style (`plugin` key), so add `superpowers@git+https://github.com/obra/superpowers.git#v7.0.1` to `.opencode/opencode.json` or the global config. Windows fallback is documented: `npm install superpowers@git+... --prefix "$HOME\.config\opencode"` and point OpenCode at the absolute package path.
- **Guardrail:** the repo already has local copies of `brainstorming`, `writing-plans`, `test-driven-development`, `systematic-debugging`, and `requesting-code-review`. Installing the plugin without dedupe creates duplicate names and unpredictable triggering. Archive the local duplicates first (Section 5.3).
- **Acceptance:** the `skill` tool lists `using-superpowers`, `subagent-driven-development`, and `verification-before-completion`; a dry-run `writing-plans` produces a plan with file paths and verification steps.

#### 3. low-level-dev-skills — MSVC/debug/build depth

- **Freshness:** MIT; 266 stars; last push 2026-06-27 (stable reference, not fast-moving).
- **What it does:** 60+ skills across compilers, debuggers, profilers, build systems, binaries, and platforms. The relevant subset is:
  - `msvc-cl`: `cl.exe`/`clang-cl`, `/MT` vs `/MD`, PDB, Windows ABI, LNK errors, MSBuild settings.
  - `debug-optimized-builds`: `RelWithDebInfo`, `-Og`/`/Od`, inlined frames, "value optimized out".
  - `core-dumps`: minidump/core analysis, symbol resolution, crash triage.
  - `build-acceleration`: ccache/sccache, unity builds, PCH, split DWARF.
  - `pgo`: profile-guided optimization.
  - `static-analysis`: clang-tidy/cppcheck/scan-build triage.
- **Project fit:** `fast_build.py` invokes VS2022 Enterprise MSBuild; the project has 173 active TUs and a 32-bit DLL target. `msvc-cl` covers runtime-library mismatch and PDB generation; `debug-optimized-builds` covers the exact "optimized build has no locals" problem during crash analysis; `core-dumps` complements the existing SEH handler; `build-acceleration` is relevant to the 6-24s incremental builds.
- **Install target:** selected dirs only:
  `C:\Users\aaaaaaaaa\.agents\skills\msvc-cl`, `debug-optimized-builds`, `core-dumps`, `build-acceleration`.
- **Guardrail:** do not install the Linux-only `gdb`, `linux-perf`, `valgrind`, or `heaptrack` skills for this project. They are good skills, wrong platform.
- **Acceptance:** ask "explain /MT vs /MD for a 32-bit ASI DLL" and get a project-relevant answer; ask "how do I read a minidump without symbols" and get a PDB-first workflow.

#### 4. renderdoc-cli — GPU state evidence, with a D3D9 caveat

- **Source/license:** `https://github.com/FindTea/renderdoc-cli-skill` (MIT). The CLI wraps the RenderDoc replay API into 60+ commands and ships an OpenCode skill at `skills/renderdoc-cli/SKILL.md`. Alternative: `https://github.com/BANANASJIM/rdc-cli` (MIT, pip-installable CLI, 186 stars).
- **What it does:** `events`, `draws`, `pipeline`, `bindings`, `cbuffer`, `shader`, `list-shaders`, `shader-build`, `shader-replace`, `pixel`, `pick-pixel`, `debug pixel/vertex/thread`, `export-rt`, `snapshot`, `mesh`, `diff`, `assert-pixel`, `assert-state`, `assert-image`, `assert-count`, `assert-clean`, `counters`. All commands support `--json`.
- **Project fit:** D3D9 capture/replay is supported for pipeline state, bindings, constant buffers, textures, render targets, and draw-call diff. For a project whose bugs are often "the right draw with the wrong state", this is a strong evidence tool. `assert-state` and `assert-pixel` can become release gates.
- **Critical limitation:** RenderDoc's own documentation states: "Shader debugging is currently only supported on D3D11, D3D12, and Vulkan." D3D9 source-level HLSL debugging is not available. Use RenderDoc for state/bindings/RT, and use FXC disassembly + PIX (DX SDK June 2010) for D3D9 shader stepping. Visual Studio Graphics Diagnostics and Nsight also target D3D11/12, so the practical D3D9 path is FXC `/Fc` asm + PIX + constant-buffer logging. PIX for Windows is legacy; verify it launches on Windows 11 before depending on it.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\renderdoc-cli`; build the CLI from source (CMake + RenderDoc) or use a release binary, then add it to `PATH`.
- **Risk:** medium. Native build, RenderDoc version must match the capture, and capture can alter timing. The game is 32-bit; verify the RenderDoc build can capture/replay the 32-bit process before relying on it. Use a separate capture profile and do not judge performance from a captured frame.
- **Acceptance:** `renderdoc-cli info capture.rdc`, `renderdoc-cli draws --json`, and `renderdoc-cli pipeline -e <event> --json` return structured data.

#### 5. imgui-ui-ux-engineering — the tools-engineer skill

- **Freshness:** MIT; 7 stars; last push 2026-08-20; Dear ImGui 1.92+.
- **What it does:** a production-oriented skill for ImGui tools, editor panels, debug dashboards, asset browsers, and inspectors. Covers dockspace shells, command bars, tables, `ImGuiListClipper`, stable widget IDs, scope stacks, DPI/font scaling, keyboard navigation, contrast checks, and a 15-case semantic regression suite. Ships `check_imgui_cpp.py`, panel templates, and a design system template.
- **Project fit:** `docs/plans/2026-10-24-imgui-settings-coverage.md` found ~315 rows across a 3,220-line `debugmenu_ui.cpp`, range mismatches, save/load gaps, no device-reset handling, and a second divergent legacy menu. The skill's ID-stack, scope-stack, DPI, and clipper guidance maps directly onto those defects.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\imgui-ui-ux-engineering`.
- **Guardrail:** the full quality gate wants Python 3.9+ and a C++17 compiler; the project uses MSVC, not g++. Run `check_imgui_cpp.py` and the review workflow; skip the g++ template compile gate unless a compatible compiler is available.
- **Acceptance:** review `debugmenu_ui.cpp` and produce a table of ID collisions, unbalanced `PushID`/`PopID`, DPI scaling issues, and missing device-reset calls; `check_imgui_cpp.py` returns a clean or prioritized report.

#### 6. universal-modder — the QA/tech-art hands

- **Freshness:** MIT; 8,869 stars; last push 2026-10-24.
- **What it does:** 12+ skills, including `game-automation`, `game-recon`, `reverse-engineering`, `port-a-mod`, `asset-pipeline`, `showcase-video`. `game-automation` provides `um win launch`, `um win shot` (GPU-safe Windows Graphics Capture), `um win drive` (click/key/hold/relative mouse), `um win record`, `um win idle`, and an in-game JSON bridge pattern. `game-recon` fingerprints engine/loader/anti-cheat and writes `MODDING_PLAN.md`.
- **Project fit:** this is the missing "eyes and hands" for in-game regression. The project currently depends on the user launching the game and reporting; `um win shot` + `um win drive` can capture a fixed window, navigate a known menu path, and record evidence. `game-recon` is useful for the remaster audit (engine, loader, mod folders, save/config paths).
- **Install target:** copy only the four relevant skill folders to `C:\Users\aaaaaaaaa\.agents\skills`; install the `um` CLI with `uv tool install "git+https://github.com/rehan-remade/universal-modder"` only if automation is approved.
- **Risk:** medium. It drives a real game window and can capture/record the desktop. Guardrails: single-player only, never focus an online mode, check `um win idle` before driving, use a fixed window size, and keep a click-point table in `docs/qa/`.
- **Acceptance:** `um win ps` lists the game; `um win shot --exe gta_sa.exe shot.png --scale 0.33` produces a readable frame; `um win idle` returns seconds since user input.

#### 7. game-qa + test-gap-audit — the QA role separation

- **Source/license:** `https://github.com/Bulugulu/game-qa` (MIT); `test-gap-audit` is an Awesome Copilot skill (MIT).
- **What it does:** `game-qa` defines a five-role loop: feature owner (PM hat), enumerator (player-journey cases), QA lead (coverage/dispatch/verdict), QA engineer (runs a journey, captures evidence, never judges), QA reviewer (judges, files tickets immediately). It writes `qa-runs/` with journey YAML, screenshots, per-case review JSON, and a summary. `test-gap-audit` is a read-only audit of missing/weak/stale coverage.
- **Project fit:** the role separation solves the false-completion problem better than a single "QA agent". The runner is browser-specific (Playwright/`window.debug`), so do **not** run its browser path. Adapt the loop: enumerator writes GTA SA player journeys (e.g. "drive a car through Grove Street at noon, enter water, open the menu"); engineer uses `satk view_capture` and `um win shot`; reviewer compares against a visual target and files a ticket with log tag + screenshot + file:line.
- **Install target:** `docs/skills/vendor/game-qa` as a read-only reference (the repo is a Claude plugin at `plugins/game-qa/skills/*`, not a root `SKILL.md`); `test-gap-audit` in `C:\Users\aaaaaaaaa\.agents\skills` for Python/tools coverage. Author a project-specific `skygfx-game-qa` adapter in `.opencode/skills` (Section 9).
- **Risk:** medium. Browser assumptions, subagent orchestration, and "streaming tickets" need adaptation. Do not let it claim a visual pass from DOM/state assertions.
- **Acceptance:** one dry-run journey produces an evidence bundle with before/after screenshots, a log excerpt, and one severity-ranked finding.

#### 8. github-release + docs-sync-audit + documentation-writer — release and docs discipline

- **Source/license:** Awesome Copilot skills, repo MIT.
- **What it does:** `github-release` handles SemVer, Keep a Changelog, tag creation, CI watch, artifact verification, and release notes. `docs-sync-audit` is a read-only drift audit that compares docs claims with code/config/CLI behavior and separates confirmed drift from inferred gaps. `documentation-writer` is a Diátaxis-guided technical writer. `create-architectural-decision-record` writes ADRs.
- **Project fit:** the project ships an ASI, an installer, and a release folder; release notes are currently manual. Docs are numerous and partially stale (`docs/plans/README.md` marks several plans SUPERSEDED/ARCHIVE; `skygfx-workflow` still references a non-existent `memory-sync.py`). `docs-sync-audit` can be scoped to one subsystem per wave, which fits the 5-minute reconciliation pass.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\{github-release,docs-sync-audit,documentation-writer,create-architectural-decision-record}`.
- **Guardrail:** `github-release` expects a GitHub remote and `gh` auth. Current `gh auth status` reports an invalid keyring token; run `gh auth login` before using it. For local ASI releases, use it for version/changelog generation and keep the actual packaging in `tools/release_packager.py`.
- **Acceptance:** a dry-run changelog from the last tag; a docs-sync-audit report on `docs/` with confirmed vs inferred drift; one ADR for the state doctrine or skill registry.

#### 9. autoresearch — the measurable micro-loop

- **Source/license:** `https://github.com/github/awesome-copilot/tree/main/skills/autoresearch` (MIT, author luiscantero; inspired by Karpathy's autoresearch).
- **What it does:** interactive setup of goal, metric, command, extraction regex, direction, scope, and constraints; baseline measurement; autonomous loop of edit -> run -> measure -> keep/discard; TSV results log; git reset to last known good on regression.
- **Project fit:** the requested "1-5 minute diag/fix once stable" needs a metric. Use `build_time`, `asi_size`, `crash_signatures`, `visual_diff_score`, or `p95_frame_ms`. The skill's "commit every experiment before running it" rule is exactly what a dirty render tree needs.
- **Critical guardrail:** it uses `git reset` to discard regressions. Run it only in a dedicated worktree with a clean tree, a narrow scope, and an explicit out-of-scope list. Never run it on the main working tree while 170+ files are modified.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\autoresearch`.
- **Acceptance:** a dry-run with metric `build_time`, scope `tools/`, three experiments, and a TSV log; no source file outside scope changed.

#### 10. skill-creator + Agent Skills spec — author the missing skills

- **Source/license:** `https://github.com/anthropics/skills/tree/main/skills/skill-creator` (Apache-2.0); spec at `https://agentskills.io/specification` (Apache-2.0).
- **What it does:** interviews the user, drafts `SKILL.md`, creates test prompts, runs qualitative/quantitative evals, rewrites descriptions for triggering accuracy, and iterates. The spec defines `name`, `description`, `license`, `compatibility`, `metadata`, optional `scripts/`, `references/`, `assets/`, and progressive disclosure.
- **Project fit:** the missing high-value skills are project-specific: `skygfx-d3d9-shader-forensics`, `skygfx-wave-scheduler`, `skygfx-game-qa`, `skygfx-render-state-doctrine`, `skygfx-release-asi`, `skygfx-technical-art`, `skygfx-docs-drift`, `skygfx-skill-router`. `skill-creator` is the correct way to author and evaluate them instead of writing ad-hoc prompts.
- **Install target:** `C:\Users\aaaaaaaaa\.agents\skills\skill-creator`; optional `skills-ref` validator from the `agentskills/agentskills` repo.
- **Guardrail:** eval scripts may call an external model/API. Authoring and `skills-ref validate` work offline; the eval runner needs credentials/network. Do not put game assets or proprietary code in eval prompts.
- **Acceptance:** one authored skill validates, has a description under 500 characters with explicit triggers, and passes three positive + one negative prompt.

### 2.3 Honorable mentions and "not now"

| Pack | Source / license | Verdict |
|---|---|---|
| `binary-re` | https://github.com/2389-research/binary-re / MIT | Good structured RE (triage/static/dynamic/synthesis), but r2/Ghidra/QEMU-oriented. Use only if satk's `re`/`kb` is insufficient. |
| `as0ler/skills` | https://github.com/as0ler/skills / Apache-2.0 | radare2, Frida, r2frida, UE5 structures. Useful for live-process hook verification; medium risk because Frida/injection needs care. |
| `harness-engineering` | https://github.com/github/awesome-copilot/tree/main/skills/harness-engineering / MIT | Excellent for turning repeated agent failures into checks. Use at wave 2, not as a top-10 install. |
| `dotnet/skills` MSBuild pack | https://github.com/dotnet/skills / MIT | `binlog-failure-analysis`, `incremental-build`, `msbuild-antipatterns`. Use if MSBuild binlog triage becomes the bottleneck. |
| `spec-kit` | https://github.com/github/spec-kit / MIT | Strong SDD, but heavy and overlaps `WORKFLOW_PLAN.md`. Revisit only if the remaster exceeds ~20 waves. |
| `cc-sdd` | https://github.com/gotalab/cc-sdd / MIT | 17-skill long-running SDLC with per-task independent review. Too heavy for the current 8-lane cadence. |
| `agent-skill-stack` | https://github.com/github/awesome-copilot/tree/main/skills/agent-skill-stack / MIT | Use once as a one-off audit to find overlap, then archive. |
| `quality-playbook` | https://github.com/andrewstellman/quality-playbook | Custom license ("Complete terms in LICENSE.txt"). Legal review before use; not recommended now. |
| `release-orchestrator` | https://github.com/borghei/Claude-Skills / MIT + Commons Clause | Commons Clause is not OSI-open; avoid for a distributed mod. |
| `graphics-api-hooking` | gmh5225/awesome-game-security / MIT | DX9 hooking/overlay reference. Cheat-adjacent, not needed; security risk. |
| `slang-shader-engineer` | https://github.com/github/awesome-copilot / MIT | HLSL-compatible but Slang/SM5+ oriented; not SM3.0/D3D9. Reference only. |
| `shader-programming` (gamedev-skills) | https://github.com/gamedev-skills/awesome-gamedev-agent-skills / Apache-2.0 | GLSL-first, engine-agnostic shader concepts; wrong dialect for `ps_3_0`/`vs_3_0`. Reference only. |
| `shader-development-toolkit` | already installed | GLSL/ShaderToy only. Keep as reference, block for HLSL/SM3 lanes. |
| `performance-profiling` | already installed | Linux perf/ftrace/eBPF. Not applicable to Windows/D3D9. Use PIX, WPA/ETW, or Superluminal instead. |
| `address-sanitizer`, `sanitizers` | already installed | ASan runtime is not practical inside an injected 32-bit D3D9 game without a separate harness. Keep for static review only. |
| `cmake` | already installed | Project uses premake + MSBuild. Reference only. |
| `ralph-loop-workflow` | already installed | Autonomous loop conflicts with the wave protocol. Use only for isolated tooling tasks. |
| `deepwork` | already installed | Good for a multi-day refactor (e.g. `main.cpp` carve), too heavy for each wave. |

**AGENTS.md ecosystem note.** `AGENTS.md` is an open project-instruction format (repo: `https://github.com/agentsmd/agents.md`, MIT; curated examples: `https://github.com/Ischca/awesome-agents-md`, CC0). It defines *how agents work in a repo* (commands, conventions, boundaries), not *what skills are installed*; the skill format is `SKILL.md`. Use the project `AGENTS.md` for the A1-A8 role table and skill-root rules, and keep skill packaging in the registry. Do not turn `AGENTS.md` into a 500-line skill catalog; link to this document and the registry instead.

**RenderWare finding.** No maintained RenderWare 3.6-specific agent skill was found. The closest coverage is satk's `rw`/`kb` commands plus the project's own `skygfx-legacy-protector`; treat RenderWare as an authoring gap, not an install gap.

**Generic Win32/D3D9 RE finding.** satk's `re`/`kb` covers GTA SA symbols, crash addresses, engine structs, and patches, which is the project-specific 90%. For generic PE/x86 disassembly/decompilation, the best surveyed options are `binary-re` (MIT, r2/Ghidra/QEMU) and `as0ler/skills` (Apache-2.0, radare2/Frida/r2frida). Neither is top-10 because satk already owns the game-specific path; install one only if a hook/crash needs raw binary analysis outside the gta-reversed symbol set. For D3D9 API hooking, `graphics-api-hooking` is cheat-adjacent and not recommended; the project's own hook table and `skygfx-legacy-protector` are safer.

---

## 3. Role -> skill -> wave slot matrix (one page)

**Abbreviations:** `sm30` = `sm30-shader-safety`; `pbr-ref` = `pbr-pipeline-reference`; `render-comp` = `rendering-compliance`; `rdc` = `renderdoc-cli`; `um` = `universal-modder` subset; `sp` = `superpowers`; `vp` = `verification-planning`; `vt` = `verification-before-completion`; `docs` = `documentation-writer` + `docs-sync-audit`.

The eight slots are **concurrent roles in every wave**, not sequential phases. A wave may leave a slot idle, but it may not double-book a write scope.

| Slot | Studio role | Existing `.mimocode` agent | Must-load skills | On-demand | Wave deliverable | Write scope |
|---|---|---|---|---|---|---|
| **A1** | Engine programmer | `crash-debugger` | `skygfx-workflow`, `skygfx-systematic-debugging`, `satk`, `core-dumps` | `binary-re`, `debug-optimized-builds`, `skygfx-legacy-protector`, `memory-safety-patterns` | Root-cause card: symptom, crash addr, `file:line`, asm/log evidence, bounded fix | `src/core/**`, `src/rw/**`, `src/render/pipelinecommon.cpp` (one file per card) |
| **A2** | Graphics / R&D | `shader-expert` + `pbr-specialist` + `postfx-specialist` | `sm30`, `pbr-ref`, `render-comp`, `rdc` | `satk` (shader/view), `skill-creator`, `black-screen-debug` | Shader/register contract + visual hypothesis + capture evidence | `shaders/**`, `src/render/**` (one file per card) |
| **A3** | Technical artist | `pbr-specialist` + `researcher` (partial) | `satk` (view/model/texture/style), `um` (game-automation), `pbr-ref` | `skygfx-game-qa`, `render-comp`, `docs` | Reference frames, material/asset target, visual diff score | `assets/**`, `docs/visual-targets/**`, `qa-runs/**` (no code) |
| **A4** | Tools engineer | **none (gap)** | `imgui-ui-ux-engineering`, `python-tools`, `code-graph`/`lsai` | `debugging-and-error-recovery`, `cpp-modern-features` | Menu/profiler/diagnostic patch + ID/DPI/device-reset checklist | `src/core/debugmenu_ui.cpp`, `src/render/profiler.cpp`, `tools/**` (one file) |
| **A5** | QA / regression | `render-auditor` + `crash-debugger` (partial) | `vp`, `skygfx-game-qa`, `test-gap-audit`, `satk` | `game-qa` (reference), `rdc`, `um`, `verification-before-completion` | Evidence bundle: before/after PNG, log tags, crash signatures, verdict | `qa-runs/**`, `docs/qa/**` (read-only on code) |
| **A6** | Build / release | `build-deploy` | `msvc-cl`, `debug-optimized-builds`, `build-acceleration`, `github-release`, `python-tools` | `pgo`, `static-analysis`, `clonedeps` | Green build, ASI size/timestamp, release notes, rollback artifact | `premake5.lua`, `resources/resource.rc`, `tools/fast_build.py`, `installer/**` |
| **A7** | Producer / PM | `knowledge-manager` (partial) + orchestrator | `sp` (`writing-plans`, `subagent-driven-development`, `vt`), `vp`, `worktrees`, `parallel-agents` | `deepwork` (multi-day only), `memory-startup`, `reflect` | Wave brief, 8 lane cards, risk register, keep/revert decision | `docs/plans/waves/**`, `WORKFLOW_PLAN.md` (no code) |
| **A8** | Technical writer | `researcher` + `knowledge-manager` (partial) | `docs`, `create-architectural-decision-record`, `github-release`, `memory-octree` | `writing-plans`, `vault-sdk-recon`, `distill` | Wave report, ADR, changelog, docs-drift findings | `docs/**`, `.serena/memories/**` (no code) |

### 3.1 How the eight hats relate to the existing eight agents

- **Do not create eight new agent types.** `.serena/memories/agent-tiers.md` states that custom roster agents are not dispatchable and only the built-in types work (`councillor`, `designer`, `explore`, `explorer`, `fixer`, `general`, `librarian`, `oracle`). The studio role is a **hat** assigned to a wave slot; the execution primitive is still a built-in task type.
- **The `.mimocode` names are role references, not OpenCode task types.** They describe MiMoCode agents and the project's functional split; if the wave runs under OpenCode, use them as prompt/role hats and dispatch through the built-in types. Never pass `shader-expert` or `crash-debugger` as a `subagent_type`.
- **A1** uses `crash-debugger` for log/crash triage and `fixer`/`oracle` for the patch/review split.
- **A2** splits into `shader-expert` (HLSL), `pbr-specialist` (material/pipeline), and `postfx-specialist` (chain/state).
- **A3** is the weakest fit today: `pbr-specialist` understands materials, but no agent owns reference capture, visual targets, or asset QA. Add the hat, not a new agent.
- **A4** is a real gap. The closest agent is `build-deploy` (build tooling) or `researcher` (docs), neither owns ImGui/Win32 tooling. This is the first new **skill**, not a new agent.
- **A5** is split between `render-auditor` (state/consistency) and `crash-debugger` (runtime). The QA loop needs one owner for the evidence bundle.
- **A6** maps cleanly to `build-deploy`.
- **A7** is the orchestrator, not a `.mimocode` agent. `knowledge-manager` can own the memory/reconciliation half.
- **A8** is split between `researcher` (external research) and `knowledge-manager` (memory/docs). The Diátaxis/docs-drift skills fill the gap.

**The 100-role swarm remains a backlog, not a concurrency target.** `tools/swarm/roles.json` maps naturally to the eight slots by its `lane` field: `crash`/`render`/`shader` -> A1/A2, `release` -> A6, `verify` -> A5, `docs`/`research` -> A8. Keep the 100 items as a queue; execute at most 3 concurrent writes and 8 active slots per wave. This is the reconciliation between the old 100-role registry and the 8-agent model.

---

## 4. Local inventory and coverage

### 4.1 Skill roots on this machine

| Root | Scope | Format | Observed count | Notes |
|---|---|---|---|---|
| `C:\Users\aaaaaaaaa\.agents\skills` | Global, agent-compatible | `SKILL.md` dirs | 33 dirs | Primary universal root; OpenCode and Codex both read it. |
| `C:\Users\aaaaaaaaa\.config\opencode\skills` | Global, OpenCode-only | `SKILL.md` dirs | 13 dirs | Contains `deepwork`, `distill`, `reflect`, `memory-startup`, `worktrees`, `clonedeps`, `gta-mmo-wrapper`, `verification-planning`, etc. |
| `E:\dev(dave)\skygfx_plus_expIV\.opencode\skills` | Project | `SKILL.md` dirs | 12 dirs | Contains `skygfx-workflow`, `sm30-shader-safety`, `pbr-pipeline-reference`, `rendering-compliance`, `python-tools`, `token-efficiency`, etc. Also `skills.7z` (122,574 B backup). |
| `C:\Users\aaaaaaaaa\.claude\skills` | Claude-compatible | symlinks + 2 dirs | 32 entries | Mostly symlinks into `.agents/skills`; `lsai` and `codebase-memory` are real dirs. |
| `.mimocode/skills` | Legacy | single file | 1 file | `skygfx-build-test-cycle.md`; not a real skill directory. |

**Total:** 58 skill directories, 57 with `SKILL.md` (`obsidian-skills` is a stub without one).

**Official discovery order (OpenCode docs):** project `.opencode/skills/<name>/SKILL.md`, global `~/.config/opencode/skills/<name>/SKILL.md`, project `.claude/skills/<name>/SKILL.md`, global `~/.claude/skills/<name>/SKILL.md`, project `.agents/skills/<name>/SKILL.md`, global `~/.agents/skills/<name>/SKILL.md`. Names must be unique across locations.

**Format cheat sheet:**

- **Skill:** a directory with `SKILL.md`; required frontmatter `name` and `description`; optional `license`, `compatibility`, `metadata`; optional `scripts/`, `references/`, `assets/`. The `name` must match the directory.
- **OpenCode plugin:** a package entry in `opencode.json` (`plugin` for V1, `plugins` for V2); used by Superpowers. Not a skill folder.
- **MCP server:** a separate `mcp` entry with `type: local` and a command array; used by satk. Not a skill folder.
- **Install root:** `~/.agents/skills` for cross-project skills; `<repo>/.opencode/skills` for project-only skills; `~/.config/opencode/skills` only for OpenCode-specific skills.

### 4.2 Existing skill coverage by studio role

| Role | Already covered by | Gap |
|---|---|---|
| Engine programmer | `skygfx-systematic-debugging` (project C++17), `black-screen-debug`, `memory-safety-patterns`, `skygfx-legacy-protector`, `crash-debugger`, `static-analysis` | No GTA SA/RW 3.6 symbol lookup; no minidump/PDB workflow; no binary RE skill. `satk` + `core-dumps` fill this. |
| Graphics / R&D | `sm30-shader-safety`, `pbr-pipeline-reference`, `rendering-compliance`, `shader-expert`, `pbr-specialist`, `postfx-specialist` | No D3D9 GPU capture; no SM3 instruction/register forensics; `shader-development-toolkit` is GLSL-only. `renderdoc-cli` + a local forensics skill fill this. |
| Technical artist | `pbr-pipeline-reference`, `rendering-compliance`, `python-tools` (DFF/TXD), `game-systems-toolkit` | No reference-capture workflow, no visual target/scoring, no screenshot diff. `satk view_capture` + `um win shot` + `game-qa` fill this. |
| Tools engineer | `python-tools`, `debugmenu_ui.cpp` code, `code-graph`/`lsai` | No ImGui UX/ID/DPI skill; no profiler UI skill. `imgui-ui-ux-engineering` fills this. |
| QA / regression | `verification-planning`, `skygfx-systematic-debugging`, `black-screen-debug`, `render-auditor`, `test-driven-development` | No in-game QA loop, no evidence bundle, no visual baseline. `game-qa` + `test-gap-audit` + `satk`/`um` fill this. |
| Build / release | `python-tools`, `clonedeps`, `build-deploy`, `cmake` (wrong generator), `clang` (partial) | No MSVC/PDB/LNK skill, no release/changelog skill, no ASI packaging skill. `msvc-cl` + `github-release` fill this. |
| Producer / PM | `deepwork`, `worktrees`, `parallel-agents`, `memory-startup`, `distill`, `reflect`, `writing-plans`, `token-efficiency` | No wave scheduler, no lane-card template, no keep/revert gate. `superpowers` + a local `skygfx-wave-scheduler` fill this. |
| Technical writer | `writing-plans`, `memory-octree`, `vault-sdk-recon`; `obsidian-skills` is broken | No Diátaxis writer, no docs-drift audit, no ADR/changelog. `documentation-writer` + `docs-sync-audit` + `github-release` fill this. |

### 4.3 Conflicts, duplicates, and wrong-tool risks

| Name / group | Locations | Problem | Recommended action |
|---|---|---|---|
| `systematic-debugging` | `~/.agents/skills` (Superpowers) + `.opencode/skills` (project C++17) | Same name, different content. OpenCode requires unique names; one shadows the other. | Rename the project skill to `skygfx-systematic-debugging`, update references, keep the Superpowers one global. |
| `brainstorming`, `writing-plans`, `test-driven-development`, `requesting-code-review` | `~/.agents/skills` + Superpowers plugin | Duplicate content after plugin install. | Keep the plugin versions; archive the local copies (Section 5.3). |
| `code-graph`, `codemap`, `graphify`, `lsai`, `codebase-memory-mcp` | `.agents`, `.config/opencode`, MCP | Five discovery paths; context and routing ambiguity. | Primary: `lsai` for symbols + `codebase-memory-mcp`/`code-graph` for graph. Archive/trial `codemap` and `graphify`. |
| `shader-development-toolkit` | `.agents` | GLSL/ShaderToy only; no HLSL/SM3. | Keep as reference; add a `block` rule for `.hlsl`/`ps_3_0` tasks. |
| `performance-profiling` | `.agents` | Linux perf/ftrace/eBPF. | Mark `blocked` on Windows; use PIX/WPA/ETW/Superluminal. |
| `address-sanitizer`, `sanitizers`, `memory-leak-detection` | `.agents` | Runtime ASan/leak tooling is not practical in an injected 32-bit D3D9 game without a separate harness. | Reference-only; use `static-analysis` + `/analyze` + `memory-safety-patterns`. |
| `cmake` | `.agents` | Project uses premake + MSBuild. | Reference-only; do not route build work to it. |
| `obsidian-skills` | `.opencode/skills` | No `SKILL.md`. | Fix or archive. |
| `skills.7z` | `.opencode/skills` | Backup archive in the active skills root. | Move to `_archive/` or keep as a non-directory backup; it is not discovered. |
| `gta-mmo-wrapper` | `.config/opencode/skills` | Multiplayer/MMO scope, not remaster scope. | Strategic only; do not load in render waves. |
| `deepwork` | `.config/opencode/skills` | Heavy orchestrator; overlaps the wave protocol. | Use only for multi-day work such as the `main.cpp` carve. |
| `ralph-loop-workflow` | `.agents` | Autonomous loop with its own gating; conflicts with wave discipline. | Trial only for isolated tooling tasks. |
| `test-driven-development` | `.agents` | No unit-test harness for render code. | Use for Python tools/pure functions; use `verification-planning` for render changes. |
| `cpp-pro` | `.agents` | It is an agent-style skill with `tools`/`model` frontmatter, not a pure reference. | Keep as a role prompt; do not treat it as the only C++ skill. |

---

## 5. Reconciliation plan: one stack, one owner, one source of truth

### 5.1 Target architecture

```
C:\Users\aaaaaaaaa\.agents\skills\          # cross-project, universal (preferred default)
C:\Users\aaaaaaaaa\.config\opencode\skills\ # OpenCode-only, only when a skill needs OpenCode features
E:\dev(dave)\skygfx_plus_expIV\.opencode\skills\  # skygfx-specific only (prefix skygfx-)
```

Rules:

1. **One root per skill.** A vendor skill lives in `~/.agents/skills`; a project skill lives in `.opencode/skills`; do not copy the same skill into both.
2. **One name per skill.** Project-specific skills use the `skygfx-` prefix. Vendor names are never renamed inside the vendor folder (the `name` field must match the directory).
3. **Max ~30 active skills.** Everything else is `reference`, `trial`, `blocked`, or `archived`. The description of every active skill is loaded into context; 58 descriptions is unnecessary overhead.
4. **Every skill has an owner role (A1-A8) and a review wave.** No orphan skills.
5. **Archive, never delete.** Keep a dated archive folder and the `skills.7z` backup.
6. **Scripts are reviewed before install.** A `SKILL.md` is instructions; a `scripts/` folder is executable code. Read both. Prefer read-only skills for render work.
7. **Pin third-party versions.** Record commit/tag in the registry; do not track `main` for anything that can change behavior mid-wave.

### 5.2 Skill registry (proposed file: `docs/skills/registry.yaml`)

```yaml
version: 1
roots:
  global_agents: C:/Users/aaaaaaaaa/.agents/skills
  global_opencode: C:/Users/aaaaaaaaa/.config/opencode/skills
  project: E:/dev(dave)/skygfx_plus_expIV/.opencode/skills

skills:
  - name: satk
    source: https://github.com/qxlies/satk-gta
    license: MIT
    version: 0.4.0
    scope: global
    path: C:/Users/aaaaaaaaa/.agents/skills/satk
    roles: [A1, A2, A3]
    status: active
    last_verified: 2026-10-24

  - name: sm30-shader-safety
    source: local
    license: proprietary-project
    version: 1.0.0
    scope: project
    path: E:/dev(dave)/skygfx_plus_expIV/.opencode/skills/sm30-shader-safety
    roles: [A2]
    status: active
    last_verified: 2026-10-24

  - name: performance-profiling
    source: local-installed
    license: unknown-review
    version: 1.0.0
    scope: global
    path: C:/Users/aaaaaaaaa/.agents/skills/performance-profiling
    roles: []
    status: blocked
    reason: Linux perf/eBPF only; not applicable to Windows/D3D9.
```

**Status values:** `active`, `trial`, `reference`, `blocked`, `archived`.
**Required fields:** name, source, license, version, scope, path, roles, status, last_verified.
**Optional:** reason (for blocked), notes, pin (commit/tag).

### 5.3 Dedupe and normalization actions (later lane, not now)

| Action | Exact target | Why |
|---|---|---|
| Rename project skill | `.opencode/skills/systematic-debugging` -> `skygfx-systematic-debugging`; update `name:` and all references in `skygfx-workflow`, `AGENTS.md`, `WORKFLOW_PLAN.md` | Removes the only hard name collision. |
| Archive duplicate local skills | Move `~/.agents/skills/{brainstorming,writing-plans,test-driven-development,requesting-code-review}` to `~/.agents/skills/_archive/2026-10-24/` after Superpowers is verified | Prevents duplicate triggering. |
| Fix broken stub | `.opencode/skills/obsidian-skills` — add a valid `SKILL.md` or move to `_archive/` | Current directory has no `SKILL.md`; it cannot load. |
| Move backup archive | `.opencode/skills/skills.7z` -> `.opencode/skills/_archive/skills-2026-09-10.7z` | Keeps the active root clean. |
| Consolidate graph tools | Keep `lsai` + `code-graph`/`codebase-memory-mcp`; mark `codemap` and `graphify` as `trial` | Reduces discovery ambiguity and context cost. |
| Mark platform blockers | `performance-profiling`, `address-sanitizer`, `sanitizers`, `memory-leak-detection`, `cmake`, `shader-development-toolkit` -> `reference`/`blocked` | Prevents wrong-tool routing on render/build tasks. |
| Remove legacy stub | `.mimocode/skills/skygfx-build-test-cycle.md` — migrate content into `skygfx-workflow` or `docs/` and archive | It is not a discoverable skill. |
| Pin vendor versions | Add `pin:` to registry for satk, superpowers, renderdoc-cli, imgui, low-level subset | Reproducible behavior across waves. |

### 5.4 Master-skill patch points

| File | Current issue | Patch |
|---|---|---|
| `.opencode/skills/skygfx-workflow/SKILL.md` §4 "Skill Selection Matrix" | Stale/inapplicable entries (`memory-safety-patterns + sanitizers`, `cmake` not listed but installed, no satk/renderdoc/imgui/game-qa) | Replace with the role matrix (Section 3) and add a `Blocked` column. |
| Same file §9 "5-Layer Memory System" | Says 5 layers, but `AGENTS.md` and `tools/memory-startup.py` define 7; references `tools/memory-sync.py`, which does not exist | Change to 7 layers; replace commands with `python tools/memory-startup.py --compact`, `python tools/skytool.py status`, `python tools/skytool.py sync`. |
| Same file §11 "Specialized Agent Routing" | Lists 8 `.mimocode` agents but no studio roles or wave slots | Add `Studio role` and `Wave slot` columns; note A4 gap. |
| `AGENTS.md` "Specialist Routing" | Token-efficiency rules mention specialists, but no A1-A8 mapping | Add a compact 8-row role table and the three skill roots. |
| `WORKFLOW_PLAN.md` | Phases are not mapped to wave slots | Add `Wave slot` and `Primary skill` columns; one wave brief per phase. |
| `docs/plans/README.md` | No entry for this research doc or a `waves/` directory | Add `2026-10-24-skills-research.md` (ACTIVE/reference) and `docs/plans/waves/` to the index. |
| `.serena/memories/agent-tiers.md` | Model routes are current, but no role hats | Add a `Studio hat` column mapping A1-A8 to the existing 8 agent types; keep model routes unchanged. |
| `.serena/memories/agent-skills.md` | Promotion path exists (3 repeats -> candidate, 30 runs -> formal) but no wave hook | Add "wave debrief -> reflect -> distill -> skill-creator -> registry" as the promotion pipeline. |
| `tools/agent_coordinator.py` | `AGENT_ROUTING` lacks tools/producer/writer scopes | Add `tools`, `producer`, `writer` scopes mapping to A4/A7/A8; do not add new dispatchable agent types. |
| `.mimocode/agents/*.md` | No studio-role metadata | Add optional `studio_role:` and `wave_slot:` frontmatter; keep `model`/`description`. |
| `docs/plans/questionnaire.md` | Archived Q&A root-cause doc; still discoverable | Mark clearly as ARCHIVE; point to this doc's maturity ladder. |

### 5.5 Tool/MCP consolidation

| Tool | Role | Keep? | Notes |
|---|---|---|---|
| LSAI | Symbol lookup | **Primary** | Mandatory per `AGENTS.md`; use `lsai_source`/`lsai_callers` instead of reading whole files. |
| `codebase-memory-mcp` | Code graph | **Primary** | `search_graph`, `trace_path`, `get_code_snippet`; the `code-graph` skill wraps it. |
| Obsidian MCP | Docs vaults | **Keep** | 128 RE docs + 195 SkyGFX docs; use section-targeted reads. |
| Sadie/Serena/harness memory | Memory layers | **Keep** | `memory-startup.py` is the real entry point; `memory-sync.py` does not exist. |
| `satk` MCP | GTA SA index/RE/viewer | **Add** | Register as a local MCP only after install; keep tool groups narrow (`core,index,view,re`). |
| `renderdoc-cli` | GPU capture | **Add** | CLI, not MCP; add to `PATH`; prefer JSON output. |
| `codemap` / `graphify` | Graph alternatives | **Trial/archive** | Redundant with LSAI + code-graph. |
| `agentmemory` | Memory search | **Check** | Referenced in `skygfx-workflow` §9 but not present in the current MCP config; do not rely on it until verified. |

---

## 6. Learning and looping protocol for the 8-agent wave model

### 6.1 Wave clock (stable-state target: ~60 minutes including baseline and in-game verification)

| Phase | Clock | Chair | Participants | Output | Gate |
|---|---|---|---|---|---|
| **P0 Baseline** | T-10..0 | A5 + A6 | A1, A3 | `wave-N-baseline.md`: commit, ASI size/timestamp, build time, 3 reference PNGs, open crash signatures | Baseline captured before any edit |
| **P1 Designer Sprint** | 0..25 | A7 | A2, A3, A4, A8; A1/A5/A6 async recon | `docs/plans/waves/wave-N-brief.md` with exactly 8 lane cards | Every card has file scope, acceptance, evidence, rollback, budget; no overlaps |
| **P2 10-minute pass** | 25..35 | A7 | All 8 lanes | 8 lane reports (`lane / files / change / evidence / next`) | All lanes stopped at 10:00; no build |
| **P3 Reconcile** | 35..40 | A7 + A6 + A8 | A1, A5 | Conflict scan, one unified build, updated registry | Build green or revert; no overlapping writes |
| **P4 5-minute verify** | 40..45 | A5 | A3, A6 | Deploy ASI, launch, 3 PNGs, log scan | In-game evidence captured; user pass if needed |
| **P5 Debrief** | 45..50 | A7 + A8 | All | Keep/revert, memory append, one harness rule if a failure repeated | Next wave unblocked |

**Reference baseline from project docs (re-measure at P0, do not compare across machines):** ASI `2,296,832 B` after wave 5; build gate GREEN in `6.24 s` in the 2026-10-17 note. Wave 1 should record its own commit, ASI size/timestamp, incremental build time, and three reference PNGs before touching code.

### 6.2 The 25-minute Designer Sprint (agenda)

| Minutes | Activity | Owner | Output |
|---|---|---|---|
| 0-5 | Evidence review: baseline, crash/RE card, build state, last wave's regressions | A5/A1/A6 | Shared evidence board |
| 5-12 | Define **one** visual/technical target and its acceptance evidence (what the player sees, what the log says, what the capture shows) | A2/A3 | Target statement + evidence contract |
| 12-18 | Write 8 lane cards: scope, mission, acceptance, rollback, budget; assign A1-A8 | A7/A8 | `wave-N-brief.md` |
| 18-23 | Risk review: file collisions, state doctrine, shader registers, build gate, game safety | A1/A2/A4/A6 | Risk register + rollback per card |
| 23-25 | Read-back and freeze; no code before the freeze | A7 | Signed brief |

**Lane card template:**

```yaml
lane: A2
role: graphics-rnd
mission: "Fix water Fresnel split so the IV pipe matches the reference frame at noon."
files: ["src/render/waterPipe.cpp"]
read_only: ["shaders/include/Water_Common.hlsl"]
acceptance:
  - "3 matched frames: before/after at the same bookmark"
  - "no new log tag errors; waterFresnelSplit parsed and saved"
  - "SM3.0 compile clean; instruction count within budget"
evidence: "satk view_capture + skygfx_dbg.log excerpt"
rollback: "git checkout -- src/render/waterPipe.cpp; redeploy previous ASI"
budget_min: 10
build: false
```

### 6.3 The 10-minute pass

- Each lane executes **one** card. No lane starts a second card.
- Write lanes are disjoint. Max 3 concurrent writes; the rest are read-only/analysis.
- **No lane builds.** A6 is the only build lane, and only after all writers stop.
- A5 captures before/after; A3 judges visual readability; A8 timestamps and records.
- Hard stop at 10:00. If a card is incomplete, it goes back to the queue as a smaller card; it does not get extra time.

### 6.4 The 5-minute reconciliation pass

1. **0:00-1:00** — A7 runs `git status --short` and checks for overlapping edits.
2. **1:00-3:00** — A6 runs one incremental build (`python tools/fast_build.py`), deploys only if green.
3. **3:00-4:30** — A5/A3 launch, capture 3 frames, scan `skygfx_dbg.log` for new tags/crash signatures.
4. **4:30-5:00** — A7 decides keep/revert; A8 appends the result to the wave report and memory note.

If the build fails, A6 owns a 5-minute fix-or-revert. No new feature work enters the pass.

### 6.5 The 1-5 minute diag/fix loop (only after two green waves)

| Time | Step | Owner | Rule |
|---|---|---|---|
| 0:00-0:30 | Pull evidence: one log tag, one screenshot, one crash addr, one file:line | A5 | No guessing without evidence |
| 0:30-1:00 | State hypothesis + exact change | A1/A2 | One symptom, one file, one change |
| 1:00-2:00 | Apply patch; no build | Owner | No refactor, no cleanup |
| 2:00-3:30 | Incremental build + deploy | A6 | If build > 90s, stop and investigate |
| 3:30-5:00 | Capture and compare; keep/revert | A3/A5 | If not visibly better, revert |
| >5:00 | Escalate to a 10-minute lane card | A7 | Do not extend the micro-loop |

**Trigger conditions for the micro-loop:** build green, no open P0 crash, the symptom is reproducible from a bookmark/log tag, and the change is confined to one file.

### 6.6 Stability ladder

| State | Signals | Wave clock | Micro-loop |
|---|---|---|---|
| **T0 Unstable** | Crash or build red; >1 open P0 | 25 / 10 / 5 | Not allowed |
| **T1 Green build** | 0 errors/0 warnings; no new crash signature | 10 / 10 / 5 | 5-minute fix cap |
| **T2 Stable** | Two consecutive green waves; visual score >= 4/5 on the target | 5 / 5 / 5 | 1-5 minute diag/fix |
| **T3 Regression** | Any repeated regression or new crash class | Fall back to T1/T0 | Stop feature work; root-cause wave |

### 6.7 Learning capture and promotion

| Cadence | Action | Skill/tool | Artifact |
|---|---|---|---|
| Every lane | Report: files, change, evidence, next | lane report | `.serena/memories/round*-directives` + `docs/plans/waves/wave-N.md` |
| Every wave | Reflect: what repeated, what failed, what should be a check | `reflect` | `docs/plans/waves/wave-N-retro.md` |
| Every 2 repeats of a failure | Turn the failure into a durable check (grep, static_assert, build script, CI) | `harness-engineering` | `docs/skills/harness.md` + check script |
| Every 3 waves | Package a repeated workflow | `distill` | Candidate skill in `docs/skills/drafts/` |
| Every 10 waves | Consolidate memory and prune stale notes | `dream` / `memory-octree` | Updated memory bank + compact index |
| Missing procedure | Author/evaluate a new skill | `skill-creator` + `skills-ref validate` | `SKILL.md` + eval cases |
| 3 repeats -> candidate; 30 runs -> formal | Promotion threshold (matches `.serena/memories/agent-skills.md`) | A7/A8 | Registry entry with owner and status |

### 6.8 Metrics dashboard

| Metric | Source | Target | Owner |
|---|---|---|---|
| `build_green` | `premake_log.txt`, MSBuild output | 0 errors / 0 warnings | A6 |
| `build_time` | `fast_build.py` timing | < 90 s incremental; record full rebuild | A6 |
| `asi_size` | `E:\games\gtasa_skygfx_plus\skygfx.asi` | Track delta per wave; no unexplained growth | A6 |
| `crash_signatures` | `skygfx_dbg.log`, minidumps | 0 new signatures per wave | A1/A5 |
| `visual_score` | `satk view_capture` / `um win shot` vs reference | >= 4/5 on the wave target | A3/A5 |
| `lane_success` | 8 lane reports | 8/8 reports, 0 write conflicts | A7 |
| `diag_p95` | Stopwatch per micro-loop | <= 5 min at T2 | A7 |
| `regression_count` | QA matrix + visual diff | 0 to advance a wave | A5 |

### 6.9 Stop and rollback rules

- **Two consecutive regressions on the same metric** -> freeze feature work; run a root-cause wave with A1/A2/A5; no new cards until the metric is back.
- **Build fails twice in one wave** -> A6 owns the build; the wave is a build wave, not a feature wave.
- **Visual target is ambiguous** -> A3/A7 rewrite the target with a reference PNG before any code.
- **A lane touches another lane's file** -> A7 rejects the card; re-scope or serialize.
- **A skill triggers incorrectly** -> disable it for the wave, record the trigger failure, fix the description in the next debrief.

---

## 7. From Q&A to senior-dev/studio behavior (remaster proof-of-concept)

### 7.1 Maturity ladder

| Level | Behavior | Evidence | Gate |
|---|---|---|---|
| **L0 Q&A** | Answer the question; no artifact | Chat text | None |
| **L1 Artifact** | Produce a lane card or plan with file paths | `docs/plans/waves/*.md` | A7 review |
| **L2 Role** | A named studio role owns the file scope and acceptance | Lane card + owner | No overlap |
| **L3 Gate** | Independent reviewer + build/visual evidence | A5 bundle + A6 build | Keep/revert |
| **L4 Loop** | Metrics, retrospectives, promotion of repeated work | Dashboard + `reflect`/`distill` | Two green waves |
| **L5 Studio** | 8 roles, wave cadence, release train, remaster phases | Wave reports + release notes | Release candidate |

The goal is not "more agents"; it is **fewer unsupported claims**. A senior developer's output is a decision with evidence, a rollback, and a named owner.

### 7.2 Remaster proof-of-concept phases

| Phase | Weeks | Goal | Lead roles | Evidence |
|---|---|---|---|---|
| **R0 Audit** | 0 | Baseline the install, engine, build, crashes, and visual targets | A1, A5, A6, A8 | `satk index`, `renderdoc` capture, 10 reference PNGs, build metrics |
| **R1 Vertical slice** | 1-2 | One district, 3 vehicles, 1 weather, 1 weapon; all 8 roles exercised | A2, A3, A4, A5 | 3 matched frames, 0 new crashes, visual score >= 4/5 |
| **R2 Systemic parity** | 3-4 | Pipe/mode matrix, state doctrine, shader register contracts | A1, A2, A5 | Regression matrix + state audit |
| **R3 Content scale-out** | 5-6 | Vehicles/peds/weapons/map; tech-art pipeline | A3, A4, A6 | Asset checklist + performance budget |
| **R4 Hardening** | 7 | Crash/regression/performance; release candidate | A1, A5, A6 | 0 P0, 0 new signatures, p95 frame target |
| **R5 Release** | 8 | Installer, changelog, tag, docs, proof video | A6, A8 | Release notes + installer + video |

### 7.3 What "proof of concept" means for this title

- **One vertical slice is enough to prove the pipeline.** Do not scale content until R1 passes with evidence.
- **The proof is a before/after reel + a reproducible capture set**, not a feature list. Use `satk view_capture` bookmarks and `um win record`.
- **The release gate is a regression matrix**, not a compile. Every pipe/mode combination needs a pass/fail row.
- **The remaster is a state-integrity problem first.** The refactor blueprint's L1/L2/L3 doctrine and the alpha/blend/corona windows are the real risk; the studio roles must keep A1/A2/A5 on those, not just add effects.

---

## 8. Install lane (recommended commands; do not run until a clean checkpoint)

### 8.0 Preflight

```powershell
# 1. Verify the tree is clean or create a dedicated worktree.
git -C "E:\dev(dave)\skygfx_plus_expIV" status --short
git -C "E:\dev(dave)\skygfx_plus_expIV" worktree add "E:\dev(dave)\skygfx_skills_lane" -b skills/2026-10-24

# 2. Back up the current skill roots (archive, not delete).
$stamp = "2026-10-24"
Compress-Archive -Path "$HOME\.agents\skills\*" -DestinationPath "$HOME\skills-agents-$stamp.zip" -Force
Compress-Archive -Path "$HOME\.config\opencode\skills\*" -DestinationPath "$HOME\skills-opencode-$stamp.zip" -Force
Copy-Item "E:\dev(dave)\skygfx_plus_expIV\.opencode\skills\skills.7z" "$HOME\skills-project-$stamp.7z"

# 3. Fix GitHub CLI auth if you want to use `gh skills install`.
gh auth status
# gh auth login
```

**PowerShell JSON caution:** PowerShell 5.1 `Out-File -Encoding utf8` writes a BOM; JSON configs must be UTF-8 without BOM. Use `Set-Content -Encoding utf8NoBOM` (PowerShell 7) or `[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))`.

### 8.1 Batch A — low-risk global reference skills

```powershell
$dst = "$HOME\.agents\skills"
New-Item -ItemType Directory -Force $dst | Out-Null

# --- satk: portable release, outside the repo ---
# Download satk-<version>-win64.zip from https://github.com/qxlies/satk-gta/releases
# Unpack to C:\tools\satk (no Python, no admin required).
C:\tools\satk\satk.cmd init --game "E:\games\gtasa_skygfx_plus"
C:\tools\satk\satk.cmd index build
C:\tools\satk\satk.cmd agent install-skill --client codex
# Path note: release zips vary (`satk.cmd` vs `Start satk.cmd`); use the actual launcher.
# Verify: Test-Path "$dst\satk\SKILL.md"

# --- skill-creator (Apache-2.0) ---
git clone --depth 1 https://github.com/anthropics/skills.git "$env:TEMP\anthropic-skills"
Copy-Item -Recurse "$env:TEMP\anthropic-skills\skills\skill-creator" "$dst\skill-creator"

# --- Awesome Copilot docs/release pack (MIT) ---
git clone --depth 1 --filter=blob:none --sparse https://github.com/github/awesome-copilot.git "$env:TEMP\awesome-copilot"
git -C "$env:TEMP\awesome-copilot" sparse-checkout set `
  skills/documentation-writer `
  skills/docs-sync-audit `
  skills/github-release `
  skills/create-architectural-decision-record `
  skills/autoresearch
foreach ($n in "documentation-writer","docs-sync-audit","github-release","create-architectural-decision-record","autoresearch") {
  Copy-Item -Recurse "$env:TEMP\awesome-copilot\skills\$n" "$dst\$n"
}
```

**MCP registration for satk** (manual, because OpenCode is not in satk's client list). Add under `mcp` in `.opencode/opencode.json`:

```json
"satk": {
  "type": "local",
  "command": ["cmd", "/c", "C:\\tools\\satk\\satk.cmd", "mcp"],
  "enabled": true
}
```

Then verify: `C:\tools\satk\satk.cmd mcp selftest`. Keep the tool groups narrow if context is tight: set `SATK_MCP_GROUPS=core,index,view,re` in the environment. If the release ships `satk.exe`, use it directly in the `command` array instead of `cmd /c`.

### 8.2 Batch B — medium-risk global skills

```powershell
$dst = "$HOME\.agents\skills"

# --- superpowers: OpenCode plugin, not a skill folder ---
# Option 1: add to .opencode/opencode.json (V1 key):
#   "plugin": ["opencode-cline-mode", "superpowers@git+https://github.com/obra/superpowers.git#v7.0.1"]
# Option 2 (Windows fallback if the git plugin install fails):
npm install "superpowers@git+https://github.com/obra/superpowers.git#v7.0.1" --prefix "$HOME\.config\opencode"
# Then point OpenCode at the absolute package path in the config.

# --- low-level-dev-skills: selected subset only ---
git clone --depth 1 --filter=blob:none --sparse https://github.com/mohitmishra786/low-level-dev-skills.git "$env:TEMP\llds"
git -C "$env:TEMP\llds" sparse-checkout set `
  skills/compilers/msvc-cl `
  skills/debuggers/debug-optimized-builds `
  skills/debuggers/core-dumps `
  skills/build-systems/build-acceleration
Copy-Item -Recurse "$env:TEMP\llds\skills\compilers\msvc-cl" "$dst\msvc-cl"
Copy-Item -Recurse "$env:TEMP\llds\skills\debuggers\debug-optimized-builds" "$dst\debug-optimized-builds"
Copy-Item -Recurse "$env:TEMP\llds\skills\debuggers\core-dumps" "$dst\core-dumps"
Copy-Item -Recurse "$env:TEMP\llds\skills\build-systems\build-acceleration" "$dst\build-acceleration"

# --- imgui-ui-ux-engineering ---
git clone --depth 1 https://github.com/muhosekerci/imgui-ui-ux-engineering.git "$env:TEMP\imgui-skill"
Copy-Item -Recurse "$env:TEMP\imgui-skill" "$dst\imgui-ui-ux-engineering"
Remove-Item -Recurse -Force "$dst\imgui-ui-ux-engineering\.git" -ErrorAction SilentlyContinue

# --- renderdoc-cli (skill + native CLI) ---
git clone --depth 1 https://github.com/FindTea/renderdoc-cli-skill.git "$env:TEMP\rdc-skill"
Copy-Item -Recurse "$env:TEMP\rdc-skill\skills\renderdoc-cli" "$dst\renderdoc-cli"
# Follow $env:TEMP\rdc-skill\README.md for the CMake build; add the binary to PATH.
```

**Superpowers dedupe** (after the plugin is verified):

```powershell
$archive = "$HOME\.agents\skills\_archive\2026-10-24"
New-Item -ItemType Directory -Force $archive | Out-Null
foreach ($n in "brainstorming","writing-plans","test-driven-development","requesting-code-review") {
  if (Test-Path "$HOME\.agents\skills\$n") {
    Move-Item "$HOME\.agents\skills\$n" "$archive\$n"
  }
}
```

Do **not** archive `systematic-debugging` yet; the project copy must be renamed first.

### 8.3 Batch C — project-specific skills to author (not vendor copies)

```powershell
$proj = "E:\dev(dave)\skygfx_plus_expIV\.opencode\skills"

# Rename the colliding project skill and update its frontmatter.
Rename-Item "$proj\systematic-debugging" "skygfx-systematic-debugging"
# Edit SKILL.md: name: skygfx-systematic-debugging; update references in skygfx-workflow.

# Author the missing skills with skill-creator; create directories only after the draft is reviewed.
foreach ($n in `
  "skygfx-d3d9-shader-forensics", `
  "skygfx-wave-scheduler", `
  "skygfx-game-qa", `
  "skygfx-render-state-doctrine", `
  "skygfx-release-asi", `
  "skygfx-technical-art", `
  "skygfx-docs-drift", `
  "skygfx-skill-router") {
  New-Item -ItemType Directory -Force "$proj\$n" | Out-Null
}
```

Each authored skill must have: `name` = directory, `description` with explicit triggers, `license: MIT` (or project license), a body under 500 lines, and `references/` for long material. Validate with `skills-ref validate <dir>` if available.

### 8.4 Batch D — trial-only, guardrailed

```powershell
$dst = "$HOME\.agents\skills"

# universal-modder: copy only the four relevant skills; install the CLI only if approved.
git clone --depth 1 https://github.com/rehan-remade/universal-modder.git "$env:TEMP\um"
foreach ($n in "game-automation","game-recon","reverse-engineering","port-a-mod") {
  Copy-Item -Recurse "$env:TEMP\um\.agents\skills\$n" "$dst\$n"
}
# uv tool install "git+https://github.com/rehan-remade/universal-modder"

# game-qa: reference only; do not run its Playwright runner.
# It is a Claude plugin (plugins/game-qa/skills/*), not a root SKILL.md.
# Keep it outside the active skills root so its browser-specific skills do not trigger.
git clone --depth 1 https://github.com/Bulugulu/game-qa.git "$env:TEMP\game-qa"
New-Item -ItemType Directory -Force "E:\dev(dave)\skygfx_plus_expIV\docs\skills\vendor\game-qa" | Out-Null
Copy-Item -Recurse "$env:TEMP\game-qa\plugins\game-qa\skills\*" "E:\dev(dave)\skygfx_plus_expIV\docs\skills\vendor\game-qa\"

# binary-re: reference only unless satk is insufficient.
git clone --depth 1 https://github.com/2389-research/binary-re.git "$env:TEMP\binary-re"
New-Item -ItemType Directory -Force "E:\dev(dave)\skygfx_plus_expIV\docs\skills\vendor\binary-re" | Out-Null
Copy-Item -Recurse "$env:TEMP\binary-re\skills\*" "E:\dev(dave)\skygfx_plus_expIV\docs\skills\vendor\binary-re\"
```

**Trial guardrails:**

- Single-player only. Never drive or focus an online mode.
- Run `um win idle` first; if the user touched the PC within 60 seconds, ask.
- Fixed window size; record click points in `docs/qa/`.
- No paid asset-generation APIs (fal, ElevenLabs) for this project.
- No kernel/anti-cheat tooling.

### 8.5 Validation and rollback

**Copy semantics:** the `Copy-Item -Recurse <srcDir> <dstDir>` commands above assume `<dstDir>` does not exist yet. If it exists, PowerShell nests the source directory inside it. For a re-install, use `New-Item -ItemType Directory -Force <dstDir>` and `Copy-Item -Recurse <srcDir>\* <dstDir>` instead.

```powershell
# Name/dir consistency check for every installed skill.
Get-ChildItem "$HOME\.agents\skills" -Directory | ForEach-Object {
  $s = Join-Path $_.FullName "SKILL.md"
  if (Test-Path $s) {
    $m = Select-String -Path $s -Pattern '^name:\s*(.+)$' | Select-Object -First 1
    if ($m) {
      $n = $m.Matches.Groups[1].Value.Trim()
      if ($n -ne $_.Name) { "NAME-MISMATCH: $($_.Name) -> $n" }
    } else { "NO-NAME: $($_.Name)" }
  } else { "NO-SKILL-MD: $($_.Name)" }
}
```

**Rollback:** move the new directory to `~\.agents\skills\_archive\<date>\<name>` and restart OpenCode. Restore the zip backup if a config change broke discovery.

**Post-install smoke test:** restart OpenCode; ask "list skills"; confirm `satk`, `msvc-cl`, `imgui-ui-ux-engineering`, `renderdoc-cli`, `documentation-writer`, and `autoresearch` appear once each. Then ask `satk` to resolve `0x7F9ECB` and `msvc-cl` to explain `/MT` vs `/MD`. Trigger probes: "find a GTA SA crash address" (satk), "review this ImGui ID stack" (imgui), "check docs drift in docs/plans" (docs-sync-audit), "measure build time with three experiments" (autoresearch).

---

## 9. Gaps: skills to author locally

These were not found online in a form that fits this project. Author them with `skill-creator`; do not wait for a vendor.

| Skill | Owner | Why it is needed | Acceptance |
|---|---|---|---|
| `skygfx-d3d9-shader-forensics` | A2 | RenderDoc cannot source-debug D3D9; SM3.0 needs FXC asm, register contracts, instruction budgets, PIX/DX SDK workflow | Given a black shader, produce a triage table: entry, asm instruction count, register mismatch, texture bind, constant value, next test |
| `skygfx-wave-scheduler` | A7 | The 25/10/5 cadence, 8 lane cards, keep/revert gate, and metrics are project-specific | Generates a valid `wave-N-brief.md` and 8 cards from a target statement |
| `skygfx-game-qa` | A5 | `game-qa` is browser-specific; the project needs in-game journeys with `satk`/`um` | Runs one journey, writes `qa-runs/<id>/` with PNGs, log excerpt, verdict |
| `skygfx-render-state-doctrine` | A1 | The L1/L2/L3 state model and alpha/blend/corona windows are the top recurring bug source | Reviews a patch for raw `SetRenderState`, missing L1/L2 restore, and alpha-window violations |
| `skygfx-release-asi` | A6 | `github-release` handles git but not FXC/CSO/ASI/installer | Produces version, changelog, ASI hash/size, installer, and rollback artifact |
| `skygfx-technical-art` | A3 | No agent owns reference frames, material targets, visual scoring | Produces a visual target sheet + 3 matched captures + score |
| `skygfx-docs-drift` | A8 | 40+ docs, many stale; `docs-sync-audit` is generic | Scopes a drift audit to one subsystem and produces confirmed vs inferred drift |
| `skygfx-skill-router` | A7/A8 | 58 skills, 3 roots, duplicates; routing must be deterministic | Given a task, returns the slot, 3 must-load skills, 2 on-demand skills, and blocked skills |

**Authoring order:** `skygfx-d3d9-shader-forensics` (unblocks A2) -> `skygfx-wave-scheduler` (unblocks A7) -> `skygfx-game-qa` (unblocks A5) -> `skygfx-render-state-doctrine` (unblocks A1) -> the rest.

---

## 10. Risks and mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| Skill sprawl / context bloat | High | Cap at ~30 active; registry with status; archive duplicates. |
| Duplicate names / shadowing | High | Dedupe before install; name check after install; project prefix `skygfx-`. |
| Wrong-tool routing (Linux perf, ASan, CMake, GLSL) | High | `blocked` status + explicit "do not use for" lines in `skygfx-workflow`. |
| Game automation safety | Medium | Single-player only; idle check; fixed window; no online; user consent per session. |
| Autonomous loop damages the tree | Medium | `autoresearch` only in a clean worktree; commit before each experiment; narrow scope. |
| RenderDoc D3D9 limitation misread as "nothing works" | Medium | Document the limitation in the `renderdoc-cli` card; use FXC/PIX for shader stepping. |
| License contamination | Low | MIT/Apache-2.0 only for top 10; exclude Commons Clause/quality-playbook without review. |
| Third-party scripts execute unexpectedly | Medium | Read `scripts/` before install; prefer read-only; run in a sandbox/worktree; no secrets. |
| Stale skills (last push > 6 months) | Medium | Record `last_verified`; re-check at each remaster phase; pin versions. |
| Over-trust in subagents | Medium | Independent reviewer per wave; `verification-before-completion`; keep/revert gate. |
| Memory-system drift | Medium | Fix `skygfx-workflow` §9 to 7 layers and real commands; `memory-startup.py --check`. |

---

## 11. Sources

### Official format / platform

- OpenCode Agent Skills: https://opencode.ai/docs/skills/
- Agent Skills specification: https://agentskills.io/specification
- Agent Skills repository: https://github.com/agentskills/agentskills (Apache-2.0)
- Anthropic skills repository: https://github.com/anthropics/skills (Apache-2.0 for `skill-creator`; document skills are source-available)

### Recommended packs

- satk: https://github.com/qxlies/satk-gta (MIT)
- satk agent guide: https://github.com/qxlies/satk-gta/blob/main/docs/agent/SKILL.md
- satk AI install: https://github.com/qxlies/satk-gta/blob/main/docs/en/ai.md
- Superpowers: https://github.com/obra/superpowers (MIT)
- Superpowers OpenCode install: https://github.com/obra/superpowers/blob/main/.opencode/INSTALL.md
- low-level-dev-skills: https://github.com/mohitmishra786/low-level-dev-skills (MIT)
- renderdoc-cli skill: https://github.com/FindTea/renderdoc-cli-skill (MIT)
- rdc-cli alternative: https://github.com/BANANASJIM/rdc-cli (MIT)
- RenderDoc shader-debug limitation: https://renderdoc.org/docs/how/how_debug_shader.html
- imgui-ui-ux-engineering: https://github.com/muhosekerci/imgui-ui-ux-engineering (MIT)
- universal-modder: https://github.com/rehan-remade/universal-modder (MIT)
- game-qa: https://github.com/Bulugulu/game-qa (MIT)
- Awesome Copilot skills index: https://github.com/github/awesome-copilot/blob/main/docs/README.skills.md (MIT)
- `github-release`: https://github.com/github/awesome-copilot/blob/main/skills/github-release/SKILL.md
- `docs-sync-audit`: https://github.com/github/awesome-copilot/blob/main/skills/docs-sync-audit/SKILL.md
- `test-gap-audit`: https://github.com/github/awesome-copilot/blob/main/skills/test-gap-audit/SKILL.md
- `autoresearch`: https://github.com/github/awesome-copilot/blob/main/skills/autoresearch/SKILL.md
- `harness-engineering`: https://github.com/github/awesome-copilot/blob/main/skills/harness-engineering/SKILL.md
- dotnet MSBuild skills: https://github.com/dotnet/skills (MIT)
- binary-re: https://github.com/2389-research/binary-re (MIT)
- as0ler RE skills: https://github.com/as0ler/skills (Apache-2.0)
- spec-kit: https://github.com/github/spec-kit (MIT)
- cc-sdd: https://github.com/gotalab/cc-sdd (MIT)

### AGENTS.md ecosystem

- AGENTS.md format: https://github.com/agentsmd/agents.md (MIT)
- AGENTS.md website: https://agents.md
- Curated examples/templates: https://github.com/Ischca/awesome-agents-md (CC0)
- GitHub guidance: https://github.blog/ai-and-ml/github-copilot/how-to-write-a-great-agents-md-lessons-from-over-2500-repositories/

### Tooling references for the SM3.0/D3D9 gap

- RenderDoc D3D9 support and shader-debug limitation: https://renderdoc.org/docs/how/how_debug_shader.html
- Microsoft PIX for Windows (DirectX SDK): https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix
- FXC compiler reference: https://learn.microsoft.com/en-us/windows/win32/direct3dtools/dx-graphics-tools-fxc
- Dear ImGui: https://github.com/ocornut/imgui (MIT)

### License checks

- GitHub REST API `/repos/{owner}/{repo}` license field, queried 2026-10-24. All top-10 packs returned MIT or Apache-2.0. `quality-playbook` uses a custom license; `release-orchestrator` uses MIT + Commons Clause and is excluded.

---

## Appendix A — Existing skill inventory by disposition

### Active / core (load at session start or by role)

`skygfx-workflow`, `skygfx-systematic-debugging`, `sm30-shader-safety`, `token-efficiency`, `python-tools`, `pbr-pipeline-reference`, `rendering-compliance`, `black-screen-debug`, `skygfx-legacy-protector`, `game-systems-toolkit`, `memory-octree`, `memory-startup`, `verification-planning`, `worktrees`, `deepwork`, `distill`, `reflect`, `dream`, `clonedeps`, `parallel-agents`, `lsai`, `code-graph`, `codebase-analysis`, `cpp-modern-features`, `cpp-pro`, `cpp-style-ref`, `cross-review`, `debugging-and-error-recovery`, `git-workflow`, `indexion-refactor`, `memory-safety-patterns`, `requesting-code-review`, `sanitize`, `simplify`, `static-analysis`, `vault-sdk-recon`, `find-skills`, `oh-my-opencode-slim`, `check-compiler-errors`.

### Situational / trial

`brainstorming`, `writing-plans`, `test-driven-development` (Python tools only), `systematic-debugging` (global Superpowers; project copy renamed), `codemap`, `graphify`, `ralph-loop-workflow` (isolated tooling only), `gta-mmo-wrapper` (strategic only).

### Reference / blocked

`performance-profiling` (Linux only), `address-sanitizer`, `sanitizers`, `memory-leak-detection` (runtime tooling not practical without a separate harness in injected 32-bit D3D9), `cmake` (premake/MSBuild project), `shader-development-toolkit` (GLSL only), `clang` (clangd/LSAI only), `cpp-templates` (rare), `error-sanitization` (not relevant).

### Archive / broken

`cloudflare`, `coding-guidance-qt`, `obsidian-skills` (no `SKILL.md`), `skills.7z` (backup), `.mimocode/skills/skygfx-build-test-cycle.md` (legacy stub).

---

## Appendix B — Wave brief template

```markdown
# Wave N — <target>

**Date:** YYYY-MM-DD
**State:** T0/T1/T2
**Baseline:** commit <sha>, ASI <size> B, build <time> s, open P0: <n>

## Target
One player-visible or log-visible outcome. No feature lists.

## Evidence contract
- Bookmark/camera:
- Expected image:
- Expected log tags:
- Expected build result:
- Regression checks:

## Lane cards
| Slot | Role | Mission | Files | Acceptance | Rollback | Budget |
|---|---|---|---|---|---|---|
| A1 | Engine | | | | | |
| A2 | Graphics | | | | | |
| A3 | Tech art | | | | | |
| A4 | Tools | | | | | |
| A5 | QA | | | | | |
| A6 | Build | | | | | |
| A7 | Producer | | | | | |
| A8 | Writer | | | | | |

## Risks
| Risk | Owner | Mitigation |
|---|---|---|

## Decision
- Keep / revert:
- Next wave:
```

---

## Appendix C — One-page operating card

```
WAVE = 8 slots (A1 engine, A2 graphics, A3 tech art, A4 tools,
               A5 QA, A6 build, A7 producer, A8 writer)
CLOCK = 25 design -> 10 execute -> 5 reconcile -> 5 debrief
WRITES = A1/A2/A4/A6 only, one file per card, max 3 concurrent
CODE WRITES = A1/A2/A4/A6; A3 assets/docs only; A5 read-only; A7/A8 docs only
BUILD  = A6 only, after all writers stop
EVIDENCE = PNG + log tag + file:line or crash addr
STABLE = 5/5/5 + 1-5 min diag/fix; regression -> fall back one level
LEARN = reflect every wave; distill every 3; harness on 2 repeats;
        skill-creator on missing procedure; registry on promotion
```

---

## Appendix D — What this document did not do

- No skill was installed, cloned into a skill root, or executed.
- No OpenCode/MCP/INI config was edited.
- No source, shader, resource, or build file was changed.
- The only file written is `docs/plans/2026-10-24-skills-research.md`.
- The tree was dirty at audit time; the install lane must start from a checkpoint or a dedicated worktree.
