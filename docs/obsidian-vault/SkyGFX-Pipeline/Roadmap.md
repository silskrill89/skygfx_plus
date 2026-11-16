---
tags: [roadmap, planning, milestones]
created: 2025-01-02
updated: 2026-11-16
---

# SkyGFX Plus Roadmap

> [!info] Full documentation
> See `docs/Future Features.md` and `docs/Roadmap to Ultimate Mod.md` for detailed planning.

## Vision

Full forward+ rendering with all original aap/junior features preserved, platform/game style selection, GTA V/IV-style quality settings menu, advanced per-pipe blending. All modern features that fit in SM3.0.

---

## Phase 1: Restore Full Compatibility

- Verify all 9 aap car pipes work
- Verify all 2 aap building pipes work
- Verify all junior features (noise-based variation, wind, radiosity, trails, grading)
- Fix dead code stubs:
  - `UploadUnifiedConstants` (debugmenu_ui.cpp:301) — returns immediately
  - Wheel extender (wheels_extender.cpp:330-348) — swap disabled, install empty
  - Env map normals (envmap.cpp:626-629) — cleared to flat
  - Dead code in main.cpp:470 — unreachable mobile light multiplier
- Restore YCbCr correction from junior fork

## Phase 2: Platform/Game Style Selection

- Add UI for:
  - `vehicleStyle` = PS2 | Xbox | PC | Mobile | GTAIV | Modern
  - `buildingStyle` = PS2 | Xbox | GTAIV | PBR
  - `colorStyle` = PS2 | PC | Mobile | III | VC | VCS | GTAIV | Modern
  - Per-pipe quality sliders (distance, detail, reflections)
- Hook into GTA SA's internal settings menu (CMenuManager)
- INI + debug menu + in-game menu support

## Phase 3: GTA V/IV-Style Settings Menu

- Copy GTA V pause menu structure:
  - Graphics → Quality Preset (Low/Medium/High/Very High/Ultra)
  - Graphics → Advanced → individual feature toggles
  - Graphics → Advanced → per-pipe blend controls
- ImGui-based overlay (existing debugmenu_ui.cpp)
- Keyboard navigation support

## Phase 4: Forward+ Unified Pipeline

- Complete `unifiedPipe.hlsl` with all 4 passes
- G-buffer: depth, normal, albedo, material properties
- Tiled forward lighting (8-iteration dynamic light loop)
- IBL with pre-computed environment maps
- All legacy modes as fallback paths

## Phase 5: SM3.0 Feature Fit

Features that fit in ps_3_0/vs_3_0:

| Feature | Status |
|---------|--------|
| PBR (GGX/Smith/Schlick) | Done |
| SMAA | Done |
| SSAO | Done |
| Motion blur | Done |
| SSS | Done |
| Noise-based variation | Done |
| Wind animation | Done |
| Parallax water | Done |

---

## Related

- [[SkyGFX Pipeline Overview]] — current rendering architecture
- [[Decided Architecture]] — architecture decisions quiz
- [[Wheel Extender Technical Plan]] — wheel system roadmap
- [[Weather System Architecture]] — weather system plan

## Main Docs

- `docs/Future Features.md` — deferred features list
- `docs/Implemented Features.md` — what's done

---

## Status Addendum — 2026-11-10 (wave 20)

- Phases 1–3 are largely realized in the current tree: the aap/junior pipe families are present,
  platform/game-style selection is mode-driven (`modeManager.cpp`), and the settings/debug menu
  exists. Phase 4 (`unifiedPipe.hlsl` forward+) remains the structural end-state; the current
  effort is **stabilization toward the goal line**, not new pipeline work.
- The 2026-11-09/10 waves are fix waves: CA contract (rainbow), far/fog restore, VCS RT-copy
  guards, softbody TOCTOU, luminance cache-desync, safety-lever floors, sentinel honors,
  black-quad white-shield, CClouds split restore.
- Extraction campaign is done on disk (see [[Current Architecture (Wave 20)]]). Last gate build is
  W0.5; W0.6 is pending the address-hygiene sweep ([[Gate Chain]]).
- Current status, watch-list and open backlog: [[Status 2026-11-10]].
- Goal line: match `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-09` (Sep-23 ~07:09)
  with zero on-screen glitches.

---

## Status Addendum — 2026-11-16 (root cause + fix)

- **The ~3-month untextured-world regression is root-caused.** World geometry was drawing
  UNTEXTURED: the `TexDefsFindCB` miss path bound the shared `skygfx_missgrey` 1×1 grey
  sentinel permanently into `material->texture` — mechanism = broken `dict->parent` walk in this
  ModLoader/ROSA install + LOD-DFF-first streaming so full-detail lookups miss at load
  (session log `miss total=158 distinct=32`, ~3 `dictfallback` recoveries). Grey × ambient
  explains every screenshot (pale-blue slabs by day, near-black at dawn, flat tan ground).
  Full verdict: [[Status 2026-11-16]].
- **Fix landed (commit e7bc752, pushed):** per-name sentinel clones (`TexDefsNamedSentinel`),
  `TexDefsMissTextureKind` raster identity, `TexDefsResolveAtDraw` 1 s rate-limited per-name
  retry with pinned CTxdStore slot validation, `TexDefsRemapToWorld`; consumers wired
  (`bldPolicyTexture`, `vehPolicyTexture`, ped sites) and all pipe binds routed through
  `pipeSetTexture`.
- **Phase 1 remains gated on the visual retest:** pipes are verified AAP-identical (hooks
  byte-identical; the 11-15 RVA theory disproven; pipe creation identical), but no pipe A/B is
  meaningful until the world renders textured at day AND dawn. One user game-run is awaited for
  the three instrumentation receipts (`[BPIPE] Switch cb ENTER`, `[BldTex] bldPolicyTexture
  first`, `[texdefs] resolve first-call`).
- Frame-order chart committed: `docs/plans/2026-11-15-rendering-pipeline-order.md` (Layer 0–3,
  hook table, config reference) — its "0x53C15E is NOT a CALL" claim is disproven; see the
  [[SkyGFX Pipeline Overview]] 2026-11-16 addendum.
- Standing rules unchanged (lanes never build; address-hygiene hard rule; never-commit and
  NEVER-REVERT lists): restated in [[Status 2026-11-16]].
