#pragma once

// ============================================================
// postfx_underwater.h — W7-D lane: underwater volume fog + SSS (Ultra tier).
// NEW postfx module, self-contained (src/render/postfx_underwater.cpp +
// shaders/ps/Water_Underwater.hlsl). Recipe: docs/plans/water-refs.md:122 —
//   fog    = exp2(-fogDensity.w * volDepth / max(V·Front, 1e-7))
//   SSS    = scale 2.0 · EdotL² · height
//
// WIRING CONTRACT (deferred — this lane owns neither postfx.cpp nor
// resource.h/.rc nor the vcxproj; exact hunks live under "DEFERRED DIFFS FOR
// ORCHESTRATOR" in .serena/agent-reports/w78-underwater-fog.md):
//   1. postfx.cpp ColourFilter_switch (starts :1094) — insert the call after
//      the pre-tonemap fx group's UpdateFrontBuffer() (:1382), before the
//      "// Debug toggle: bypass colour filter entirely" block (:1384, unique
//      anchor) — so the pass applies to BOTH the bypass path and the
//      colour-filter switch path:
//          #include "postfx_underwater.h"      (after #include "depthhook.h",
//                                               postfx.cpp:6)
//          PostFX_UnderwaterTick();            (one line, see the diff)
//   2. resources/resource.h (ends :125) + resources/Resource.rc (ends :129) —
//      IDR_WATER_UNDERWATER 276 -> cso/Water_Underwater.cso (fast_build.py
//      auto-compiles shaders/ps/*.hlsl single-entry, ps_3_0, entry "main";
//      no build-tool diff needed).
//   3. build/skygfx.vcxproj (:191, after the postfx_utils.cpp ClCompile) +
//      build/skygfx.vcxproj.filters (:137, same position) — register
//      ..\src\render\postfx_underwater.cpp. Without this the TU never
//      compiles and item 1 fails at LINK with unresolved PostFX_UnderwaterTick.
// Until all three land this header is never included and the TU is dead code.
//
// DEFAULT-OFF SAFETY: the tick's first statements are the Config gate
// (config->waterUnderwaterFog > 0, NaN-safe via !(x > 0)) and the
// camera-underwater test. Until both pass the pass touches ZERO render state,
// allocates nothing and makes zero D3D/game calls. Set waterUnderwaterFog=0
// (INI or the debug-menu "Underwater fog" slider) for a full disable.
// ============================================================

// Chain entry point — ONE line in ColourFilter_switch (see above). Cheap when
// disabled: a float compare and an early return.
void PostFX_UnderwaterTick(void);

// Optional eager warm-up (parse data/water.dat + create the pixel shader).
// Not required: the tick lazy-inits on first use. Safe to leave unwired.
void PostFX_UnderwaterInit(void);

// True when the most recent tick actually rendered the pass (camera below the
// water surface AND the Config gate open). Debug/report helper.
bool PostFX_UnderwaterActive(void);

// Device-Reset seam (depthhook.cpp's post-Reset block, function-local extern):
// clears the lazy PS load latch + cached handle so the next tick re-creates
// the shader. Fail-open: a failed reload just skips the pass.
void UWResetShaderLatches(void);

// No Release(): the only resource held is the embedded pixel shader, which —
// like every other makePS() shader in this codebase — survives device reset
// and is released with the DLL. Nothing else is allocated.
