# 2026-11-25 — Portable Core System: research + extraction plan

Goal (user directive, m2363): research how to improve our systems based on other game-dev tools
online and papers; expand our cpps into an entire system that can later be LIFTED OUT for other
projects. Precedes effects work per stabilization plan.

Sources studied: Frostbite FrameGraph (GDC 2017, O'Donnell), Filament FrameGraph, UE RDG,
Our Machinery render graphs; Google Breakpad/Crashpad design docs (client/handler, intermediate
dumps, no-alloc-in-handler rules), Sentry native backend tradeoffs, minidump/PDB symbol-server
practice; D3D11/D3D12 debug layers + GBV, RenderDoc validation, bindless handle versioning
(Traverse), Digital Dragons "Make Your Game Friendly for Graphics Debugging"; Tracy profiler
(real-time zero-cost zones, client/server telemetry).

## 1. What industry does that we already do (validated)
- **3-layer coherent state write** (our pipeDeviceStateSet L1/L2/L3) ≈ D3D12 resource-state
  tracking + debug-layer assertions. Our doctrine = hand-rolled debug layer for a stateless API.
  Keep; formalize as `StateBackend` interface (research: state tracking per-resource with
  transition-if-changed wrapper is the standard pattern — bugnet/MSFT).
- **Crash receipts + forensics in VEH** ≈ Breakpad client design (filter callback → dump →
  post-dump callback; "code after exception must not allocate/hang"). Our 20-receipt budget,
  no-heap-in-handler, POD SEH islands match their rules.
- **Containment contract (guards/fences/liveness)** ≈ debug-layer "don't crash the OS" guidance +
  bindless validation bits (Traverse: type+writability+version in handle — our liveness guards are
  the CPU-side version).
- **Log throttle/ring/frame prefix** ≈ standard structured-logging practice (spdlog-style levels,
  ring for crash-time last-N).

## 2. What industry does that we DON'T have (opportunity list)
| # | Idea | Source | Our gap |
|---|---|---|---|
| G1 | **Out-of-process crash handler** | Crashpad | We dump in-process (corrupted state). Portable upgrade: optional `crashpad_handler.exe`-style helper; intermediate-dump-on-crash + convert-next-start (Crashpad iOS model) as the lightweight variant. |
| G2 | **Symbol pipeline** | bugnet/MS PDB | No PDB archival/symbol server. Add: build stamps PDBs into `symbols/<module>/<guid>/` keyed store; dump_syms-style flow later. |
| G3 | **FrameGraph / pass DAG** | Frostbite/Filament/UE RDG | Our postfx chain is immediate-mode call list with manual gates. A declarative pass graph (setup→compile→execute, cull unreferenced, transient resource lifetimes) would make effects composable and mode-switch safe = direct answer to "features cause regressions". |
| G4 | **Debug/validation layer as first-class toggle** | D3D11/12 debug layer, Digital Dragons talk | Our renderstate_monitor is a disabled mini-debug-layer. Formalize: `GFX_VALIDATE=1` mode that runs state-drift checks (we already have probes) + reports like a debug layer, zero cost when off. |
| G5 | **Zero-cost real-time profiler zones** | Tracy | Our PerfTimer floods the log (file open/close per line). Replace log-based profiling with Tracy-compatible `ZoneScoped`-style zones (compile-out macro) OR at minimum buffer-based perf sink instead of per-line file I/O. |
| G6 | **Resource-handle validation bits** | Traverse bindless | texdefs sentinels are the crude version; versioned handles (index+type+gen) would make UAF-class texture bugs structurally impossible. |
| G7 | **Declarative config schema** | table-driven config (industry norm) | config.cpp = 1300 lines of procedural reads. Table-driven (key,type,ptr,default) makes keys data → portability + ImGui auto-rows for free. |

## 3. Extraction architecture (from local portability audit, explore ses_de5f7a5d0ffee)
Three-tier package split:

```
gfxcore/   (portable: Win32+std ONLY — no skygfx.h/rw/d3d9/game)
  log.{h,cpp}       L0 ring/throttle/rotate logger; ONLY seam = injected frameProvider
  scope.h           L2 scope registry (types move out of skygfx.h — 100% portable today)
  crash.{h,cpp}     VEH+filter+forensics+minidump+WER (param: log path, dump dir, exe name,
                    receipt writer + scope resolver as callbacks)
  guards.h           header-only containment pattern (GuardScope RAII, guardedCall SEH,
                    isLive predicate convention)
  iniconfig.h       ini parse/lane-precedence/slot store (linb/ini_parser already external)
  modeprof.h         templated owned-field profile layering (member-ptr schema = data)
gfxrw/     (RenderWare+D3D9 backends — reusable in any RW/D3D9 tool)
  statepipe.{h,cpp}  3-layer doctrine behind StateBackend {writeL1,L2,L3,readL3} vtable
  renderstatemonitor  3-layer divergence monitor (= G4 debug layer)
  texpolicy.{h,cpp}  miss policy/sentinels/resolve-at-draw behind TexResolver interface
gfxgta/    (game plugin — NEVER extracted)
  specpool_guard, rw_guard_chokepoints, texdefs_gta (TXD pool/hooks), Config schema + kValues
```

Extraction order (safest first): 1) L2 scope registry (type relocation, zero behavior) →
2) L0 logging (drop skygfx.h umbrella + inject frameProvider) → 3) guards.h pattern codification
→ 4) crash core split (move specpool guard + 2 chokepoints OUT into gfxgta = subtraction) →
5) config/mode engine (table-driven, byte-identical keys) → 6) statepipe behind StateBackend
(highest leverage, 24-file blast radius — ship D3D9+RW default backend so consumers unchanged) →
7) texpolicy kernel last (deepest entanglement).

## 4. Improvement roadmap (research-backed, ordered by leverage)
- **P1 (stability)**: crash core split + G2 symbol archival + G1 lightweight intermediate-dump
  option. Fortifies the m0271 work into a reusable crash package.
- **P2 (regression-proofing)**: G3 frame-graph for the postfx chain — this is the systemic answer
  to "new feature → regression": passes declare reads/writes, compiler culls/allocates, mode
  switches become graph rebuilds instead of gate soup. Start as a thin pass-list wrapper around
  existing ColourFilter_switch stages (no rewrite), grow declarative.
- **P3 (validation)**: G4 GFX_VALIDATE debug-layer mode built on existing probes (zfix/cullfix/
  PipeAlpha drift detectors → one validation report channel instead of scattered dbglogs).
- **P4 (perf)**: G5 kill per-line file I/O in dbglog (batch/append handle kept open — also fixes
  the 88%-of-log PERF flood class); optional Tracy zone macros behind compile flag.
- **P5 (config)**: G7 table-driven keys; keys become data → auto ImGui rows + portability.
- **P6 (lifting out)**: execute §3 extraction order; deliver gfxcore as a standalone header+src
  drop usable by another project with zero game headers.

## 5. Constraints / standing rules that still apply
- Stabilization pass first (m2337: fixes only; expansion of effects parked) — this plan's P1-P2
  ARE stability work; P3-P6 interleave after user retest.
- Lanes never build; orchestrator gates. HARD RULE named vars/no raw addresses (install-layer
  byte-verify exempt). Receipts byte-stable additive-only. NEVER-REVERT wave records +
  effects-menu-overhaul §0. Lane cap 5. m1834 no launching the game.
- vcxproj <ClCompile> required for every new TU (build reads pre-generated vcxproj).
