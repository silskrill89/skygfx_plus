#pragma once
// =============================================================================
// motion_fx.h — motion FX family (parent motion_fx.cpp + child wind.cpp)
//
// PROVENANCE / STRUCTURE (2026-11-09 motion-family restructure):
//   The wind animation was a junior-era bolt-on: a monolith inside
//   treeWind.cpp, wired ad-hoc on top of the pipes. It is now a family that
//   participates in the randomness and physics systems:
//
//     motion_fx.cpp  PARENT — the wind ANIMATION application and orchestration
//                    (TreeWind_ApplyToTransform / TreeWind_OnEntity /
//                    TreeWind_ModifyMatrixForTreeInWind / TreeWind_UploadWindExt,
//                    all declared in treeWind.h with unchanged signatures —
//                    buildingPipe.cpp / main.cpp callers are untouched), plus
//                    the family integration seams described below.
//     wind.cpp       CHILD  — the pure wind field/gust/variation math (the
//                    CEntity::ModifyMatrixForTreeInWind port), byte-identical,
//                    consumed by the parent through the Wind_ClockMs /
//                    Wind_ComputeLean internal seams (local externs, no header).
//     treeWind.cpp   keeps only the unrelated TreeAlpha lighting/alpha system.
//
// ONE MOTION CLOCK (the physics lane's clock doctrine, applied to motion):
//   The family clock is MotionFxClockSeconds() below, and it is the ONLY
//   clock engine in the family (wind.cpp's game-ms accumulator, exported in
//   the seconds domain). CHOICE DOCUMENTED — deliberately NOT a QPC wall
//   clock like SoftbodyWaterClockSeconds() (src/entities/softbody.cpp:190):
//   the CTimer__ms_fTimeStep accumulator is pause-safe by construction (the
//   step reads 0 while paused, so sway freezes with the game — a QPC wall
//   clock would keep trees leaning in menus/pauses) and the wind phase
//   tables are defined in game-ms ticks (4096 ms period, 2^24 ms
//   phase-invisible wrap), so re-expressing them in QPC seconds would be a
//   math rewrite for zero behavioural gain. Same doctrine as MISSION B
//   (one clock per family, one exported accessor, mirrors collapse onto it):
//   there is no second anchor and no duplicate time state anywhere in the
//   family.
//
// FAMILY COUPLING (documented seams, no duplicated state):
//   * randomness (noise.h): the per-material gust-amplitude band applied by
//     the parent (motion_fx.cpp motionFx_lean) is derived from the noise
//     service — NoiseMaterialVariation(unsigned materialId, float u, float v),
//     the ONE randomness-family entry point. The family keeps no private RNG
//     and no private noise state of its own.
//   * physics (softbody / ragdoll / water_swell): wind is strictly READ-ONLY
//     with respect to physics state — it consumes no SoftbodyWaterClockSeconds
//     and no WaterSwell::Height values and writes nothing back, so a wind
//     gust can never desync the water clock the way a second wall-clock
//     anchor could. Both families tick off the same per-frame cadence (CTimer
//     step advanced once per RW render frame / physics step), so any future
//     cross-family effect (e.g. wind-blown swell) has exactly one well-defined
//     integration point on each side: MotionFxClockSeconds() here,
//     SoftbodyWaterClockSeconds() there.
//
//   The ownership / no-double-sway rule is unchanged — see treeWind.h.
// =============================================================================

// The ONE motion-family clock: game-ms accumulated once per RW render frame
// (pause-safe — CTimer step is 0 while paused), as seconds. This is the same
// single engine the wind math consumes in ms (wind.cpp Wind_ClockMs) — no
// second engine, no drift between the family's consumers.
extern float MotionFxClockSeconds(void);
