// particles_gpu.h - GPU particle sprite path (m0336 lane).
//
// First slice: boat wake particles, live-generated from the wake flush hooks.
//
//   * effectsPC.txd harvest — the game's FX manager loads models\effectsPC.txd
//     into the CTxdStore "fx" slot (FxManager_c::LoadFxProject 0x5C2420); we
//     lazily BORROW named textures out of that dictionary (masks/alphas:
//     boatwake1, boatsplash, gensplash, bubbles, waterspark_16,
//     cloudmasked≈seafoam, pointlight, smoke*, wjet*/wgush1/splash_up*
//     water-droplet alphas). Borrowed pointers — never destroyed here.
//   * SM3.0 sprite path — quad-batch (D3DFVF_XYZRHW|DIFFUSE|TEX1, CPU
//     projection through the camera LTM inverse + D3D9 LH perspective, the
//     proven Fp_BuildViewProjFromCamera math) shaded by WakeParticlePS.cso
//     (IDR_WAKEPARTICLEPS). No custom VS: FFP vertex pipe + custom PS.
//   * Wake source — main.cpp WakeDirectFlush_Draw site 1 (RenderWakeSegment
//     0x6EA2E8): the TempBuffer verts (0xC4D958, 36-byte Im3D layout) are the
//     world-space wake strip; foam sprites spawn at sampled verts.
//   * Sea-physics scaling — the SAME wind inputs waterPipe.cpp:2040-2050
//     uploads to VS c11/c12 (CWeather__WindDir 0xC813E0 normalized with the
//     dirA fallback, CWeather__Wind 0xC812F0 clamped [0,1]): drift velocity,
//     size growth and spawn alpha all scale with it; alpha fades in over the
//     first 18% of life and out over the last 65%.
//   * INI: SkyGfx/gpuParticlesEnable, DEFAULT 0 — every entry point no-ops and
//     zero device-state traffic happens when gated off (stable-config rule:
//     new features ship inert).
//
// All entry points are safe to call unconditionally from the flush hooks.

#pragma once

// Called from main.cpp WakeDirectFlush_Draw (site 1 = RenderWakeSegment
// flush): lazily harvests effectsPC.txd on first hit and spawns wake foam
// particles sampled from the TempBuffer wake verts. No-op when gated off.
void GPUParticles_WakeSpawnFromTempBuffer(int site);

// Called from main.cpp WakeDirectFlush_Draw / WaterDirectFlush_Draw after the
// original flush: once-per-game-tick update + render of the live particle
// pool (internally latched on CTimer::m_snTimeInMilliseconds 0xB7CB84, so
// multiple flush calls per frame render at most once). No-op when gated off
// or the pool is empty.
void GPUParticles_FrameFlush(const char *tag);
