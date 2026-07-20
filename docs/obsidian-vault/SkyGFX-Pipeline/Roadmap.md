---
tags: [roadmap, planning, milestones]
created: 2025-01-02
updated: 2026-07-15
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
- Verify all junior features (stochastic, wind, radiosity, trails, grading)
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
| Stochastic sampling | Done |
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
