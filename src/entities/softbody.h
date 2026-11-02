// softbody.h - native RW-physics softbody/ragdoll core (P1 of
// docs/plans/2026-10-29-rw-physics-ragdoll-re.md, section 5).
//
// TexDefs-style lifecycle (cf. src/texdefs.h:11-20). This TU is self-contained:
// it installs NO hooks and patches NOTHING - main.cpp already wires the four
// calls (Init main.cpp:8060, Update main.cpp:5458 at the old Ragdoll_Simulate
// slot, PreRender main.cpp:3961 at the old Ragdoll_ApplyPose slot, Shutdown
// main.cpp:8821).
//
// P1 SCOPE - native DEATH ragdoll (ragdoll parity, doc 5.6 phase 1):
//   The overlay is GATED TO DEAD PEDS (CPed+0x530 == 66 PEDSTATE_DEAD; the
//   dying state 65 stays excluded - ground collision exists now (G8-native),
//   but capturing mid-death-anim would ALSO freeze the ROOT frame's
//   translation, and bit 3 (HasVelocity) extracts the ped's movement from
//   that root translation, so the entity would stop mid-fall and float.
//   Fixing that needs a root-frame capture exemption (anim-owned root +
//   physics limbs) and a playtest - P2 item). On activation the module:
//     1. validates the FIXED 0x18-stride AnimBlendFrameData layout against
//        the live exe (R2 stride probe - refuse to capture on mismatch),
//     2. captures capture bits 1/2 (KeyFramesIgnoreNodeOrientation /
//        KeyFramesIgnoreNodeTranslation, sftbdyphsx.h) so the anim system
//        stops writing the captured nodes (prototype Ragdoll.cpp:60-70, G7),
//     3. integrates per-bone linear velocity (gravity + 3-axis friction, G5)
//        into cross-product parent-space rotations (RtQuatReciprocal +
//        RtQuatTransformVectors + RtQuatRotate, shipped Limit() head-Z
//        coupling G3),
//     4. resolves ground contact via CWorld::FindGroundZForCoord (0x569660,
//        G8-native): velocity reflection + tangential friction, and a
//        rotation-based positional lift of the penetrating bone's parent
//        (translations are anim-owned - only orientations reach the skin),
//        and
//     5. writes back by BlendKeyframe slerp INTO the shipped interp frame
//        (0x616E30 idiom) - the game's own UpdateRpHAnim composes
//        pMatrixArray. NO pMatrixArray pokes (G2/G6: local space by
//        construction). NaN-audited before every interp-frame write (R1).
//   Living peds are never touched (R8) - the anim system keeps full
//   interp-frame ownership there.
//
// INI: [SkyPhysics] softbodyBackend ("off"|"native"|"bullet"; default "off" =
// zero behavior change) + softbodyBudget (max active instances, default 8).
// Both owned by src/core/physicscfg.cpp; this module only calls accessors.
//
// All CPU-side; zero shader impact. No heap in the per-frame path: one static
// instance pool, stack scratch only.

#pragma once

#include "../skygfx.h"		// rwplcore (RwMatrix/RwV3d), gta.h (CVector), int32
#include "../rw/rtquat.h"	// RtQuat (SoftbodyWorldToBoneLocal signature)

// --- lifecycle ---
void SoftbodyInit(void);		// DllMain/InjectDelayedPatches: pool zero + cfg read. Idempotent.
void SoftbodyUpdate(void);		// per game frame: dead-ped assign + velocity integrator (renderFrame-gated). Fast no-op when disabled.
void SoftbodyPreRender(void);	// per render frame: BlendKeyframe slerp write-back into interp frames (renderFrame-gated). Fast no-op when disabled.
void SoftbodyShutdown(void);	// DLL_PROCESS_DETACH: release all instances + restore capture bits. No RW/D3D9 calls.

// --- state ---
bool SoftbodyGetEnabled(void);		// [SkyPhysics] softbodyBackend == "native"
bool SoftbodyIsActiveForPed(void *ped);	// ownership query. Wiring for the main.cpp /
									// ragdoll_death lane: gate the Bullet death ragdoll
									// (Ragdoll_Simulate main.cpp:5454 / Ragdoll_ApplyPose
									// main.cpp:3957) on !SoftbodyGetEnabled() so the two
									// backends never drive the same corpse (A/B exclusivity).

// --- optional impulse entry (chars.cpp / weapon lane wiring) ---
// Inject dir*force into bone `boneTag`'s linear velocity on the ped's active
// native ragdoll (bullet impacts, explosions, melee). No-op when the ped has
// no instance. Wakes a settled ragdoll.
void SoftbodyApplyForce(void *ped, int32 boneTag, float force, RwV3d dir);

// --- G6 helper for the Bullet death lane (ragdoll_death.cpp) ---
// Convert a Bullet WORLD bone transform into the bone-LOCAL space the shipped
// anim pipeline expects: outLocalQuat/outLocalPos = inv(parentWorld) applied
// to childWorld. Intended use in Ragdoll_ApplyPose (ragdoll_death.cpp:1150):
// per bone, parentWorld = the parent bone's Bullet body world transform as an
// RwMatrix (root bone: the clump LTM), childWorld = the bone's own; write the
// results into AnimBlendFrameData.KeyFrame->q/.t (the interp frame - NOT
// pMatrixArray, which UpdateRpHAnim recomposes from interp frames) and set
// capture bits 1/2 in Ragdoll_Activate / restore them in Ragdoll_Deactivate
// (G7). Returns false on singular/degenerate input (caller keeps the anim pose).
bool SoftbodyWorldToBoneLocal(const RwMatrix *parentWorld, const RwMatrix *childWorld,
	RtQuat *outLocalQuat, CVector *outLocalPos);

// --- water layer (G-water; shared by the native softbody AND the Bullet
// --- death-ragdoll lane - ragdoll_death.cpp calls these) ---
// Game water-surface query. VERIFIED against the live gta_sa.exe 1.0 US by
// disassembly: the no-waves height core lives at 0x6E8580
// (see softbody.cpp for the full signature evidence). Returns false when
// (x,y) is not inside any water quad/triangle - the 0.0/no-water fallback -
// and callers treat false as "no water". On true *pLevel is the static water
// surface height (no waves - the animated part is SoftbodyWaterSwell).
bool SoftbodyWaterQuery(float x, float y, float z, float *pLevel);

// Water_VS.hlsl c11 swell law evaluated at world (x, y): vertical surface
// displacement in metres (2-wave: 45 m/0.10 m + 23 m/0.06 m, time-phased).
// SoftbodyWaterSwellVel is its time derivative (the bob velocity the physics
// lanes follow so corpses rise/fall with the rendered surface).
float SoftbodyWaterSwell(float x, float y);
float SoftbodyWaterSwellVel(float x, float y);
