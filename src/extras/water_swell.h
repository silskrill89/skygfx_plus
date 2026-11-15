// water_swell.h - the canonical (x,y,t) -> surface-height water swell law
// (M9 shared; docs/plans/2026-10-30-water-overhaul-spec.md sec 4 contract #1).
//
// This is the ONE analytic water swell law shared by every CPU-side water
// consumer, and the exact law the M9 GPU displacement LUT must reproduce:
//
//   1. Bullet death-ragdoll lane  (src/extras/ragdoll_death.cpp - this wave)
//   2. Native softbody lane       (src/entities/softbody.cpp - REBOUND
//                                  2026-11-09, mission B: SoftbodyWaterSwell /
//                                  SoftbodyWaterSwellVel now delegate to
//                                  WaterSwell::Height/HeightVel; the local
//                                  WaterSwellMakeWave/s_waveA/s_waveB duplicate
//                                  is deleted)
//   3. M9 water displacement LUT  (src/render/waterPipe.cpp - later wave;
//                                  binds this header and evaluates
//                                  Height(x, y, g_waterTime) per LUT texel)
//
// It is a verbatim port of the Water_VS.hlsl c11 2-wave swell - the same
// law softbody.cpp's WaterSwellMakeWave / SoftbodyWaterSwell /
// SoftbodyWaterSwellVel encode (this header supersedes those as the single
// source of truth):
//
//   dirA = normalize(1, 0.35),    lambda 45 m, amplitude 0.10 m
//   dirB = normalize(-0.45, 1),   lambda 23 m, amplitude 0.06 m
//   fW   = kW * (dot(dirW, xy) - cW * t),   kW = 2pi/lambda
//   cW   = sqrt(9.81 / kW)                  (deep-water phase speed)
//   swell = cos(fA)*0.10 + cos(fB)*0.06
//
// c11 is uploaded as (1,1,0,0) by waterPipe.cpp, so swAmp/timeScale are 1.0
// here. The direction normalisation and the sqrt(9.81/k) phase speed are
// DERIVED with the same guarded math as the shader - no hand-typed constants
// that could drift from the HLSL literals.
//
// TIME: the law is explicitly parameterised by the water clock `t` so this
// header stays self-contained and each consumer supplies its own clock
// (the M9 LUT: g_waterTime / camTime.w). ONE-CLOCK RESOLUTION 2026-11-09
// (spec sec 4 contract #1, physics lane): the single clock is
// SoftbodyWaterClockSeconds() (src/entities/softbody.cpp, QPC seconds since
// the SoftbodyInit anchor; exported via softbody.h). Direction documented:
// softbody's clock was exposed rather than adding a clock accessor here so
// this header keeps its no-clock/self-contained contract. HANDOFF: the
// ragdoll lane deletes ragdoll_death.cpp's RagdollWaterClockSeconds() mirror
// (s_ragdollWaterClockAnchor + body) and calls SoftbodyWaterClockSeconds()
// directly at its seeding (:348) and per-frame sample (:1048).
//
// Self-contained: no project includes, no RW/device types, no clock, no
// circular dependency. Compiles standalone (<cmath> only). The SM3.0 rules
// (guarded divide, NaN-safe) are observed CPU-side for the same reasons.
#pragma once

#include <cmath>

namespace WaterSwell {

// One cosine wave: unit travel direction (dirX,dirY), angular wavenumber k,
// and deep-water phase speed c. Mirrors softbody.cpp's WaterSwellWave
// field-for-field so bit-for-bit parity holds.
struct Wave {
	float dirX, dirY;   // unit direction (derived, not hand-typed)
	float k;            // 2pi / lambda
	float c;            // sqrt(9.81 / k)
};

// Verbatim port of softbody.cpp WaterSwellMakeWave. The float-vs-double
// ordering of every operation matters for bit-for-bit parity with the native
// softbody lane and the M9 LUT - do not "simplify".
inline Wave MakeWave(float dx, float dy, float wavelength)
{
	Wave w;
	double len = std::sqrt((double)dx * dx + (double)dy * dy);
	if (len < 1e-7) len = 1e-7;                       // shader's max(length, 1e-7)
	w.dirX = (float)(dx / len);
	w.dirY = (float)(dy / len);
	w.k = (float)(6.2831853 / (double)wavelength);    // 2pi / lambda
	double kk = (double)w.k < 1e-7 ? 1e-7 : (double)w.k;
	w.c = (float)std::sqrt(9.81 / kk);                // deep-water phase speed
	return w;
}

// Project division-safety rule: NaN/inf guard (softbody.cpp WaterFinite).
inline bool Finite(float v) { return (v == v) && v > -1e30f && v < 1e30f; }

// The two c11 waves, derived once. The values are deterministic and
// bit-identical to softbody.cpp's file-scope s_waveA / s_waveB; the static
// local is ODR-shared across translation units (C++11 magic statics).
inline const Wave *Waves()
{
	static const Wave w[2] = {
		MakeWave(1.0f, 0.35f, 45.0f),     // wave A: 45 m, amp 0.10
		MakeWave(-0.45f, 1.0f, 23.0f),    // wave B: 23 m, amp 0.06
	};
	return w;
}

// THE single analytic swell law: vertical surface displacement (metres) at
// world (x, y) at water-clock time t. Bit-identical to the old
// SoftbodyWaterSwell(x, y) when t == WaterClockSeconds().
inline float Height(float x, float y, float t)
{
	if (!Finite(x) || !Finite(y)) return 0.0f;
	const Wave *w = Waves();
	float fA = w[0].k * (w[0].dirX * x + w[0].dirY * y - w[0].c * t);
	float fB = w[1].k * (w[1].dirX * x + w[1].dirY * y - w[1].c * t);
	return (float)(std::cos((double)fA) * 0.10 + std::cos((double)fB) * 0.06);
}

// Time derivative of Height(...) - the bob velocity the physics lanes follow
// so submerged bodies rise/fall with the rendered surface. Bit-identical to
// the old SoftbodyWaterSwellVel(x, y) when t == WaterClockSeconds().
inline float HeightVel(float x, float y, float t)
{
	if (!Finite(x) || !Finite(y)) return 0.0f;
	const Wave *w = Waves();
	float fA = w[0].k * (w[0].dirX * x + w[0].dirY * y - w[0].c * t);
	float fB = w[1].k * (w[1].dirX * x + w[1].dirY * y - w[1].c * t);
	float v = (float)(std::sin((double)fA) * (double)(w[0].k * w[0].c) * 0.10 +
	                  std::sin((double)fB) * (double)(w[1].k * w[1].c) * 0.06);
	return Finite(v) ? v : 0.0f;
}

} // namespace WaterSwell
