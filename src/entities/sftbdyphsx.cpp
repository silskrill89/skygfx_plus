// sftbdyphsx.cpp - consolidated body/limb translation unit.
//
// Merged 2026-10-26 from TWO former TUs, concatenated in this order:
//   1. src/entities/ragdoll.cpp    - living-ped secondary-motion ragdoll overlay
//                                    (RagdollBone / Ragdoll_c / RagdollManager_c)
//   2. src/extras/dismember.cpp    - Bullet gib dismemberment (limb bake, gibs,
//                                    zero-scale stumps, blood spurts)
//
// Bodies are verbatim; only the includes were deduped and section banners added.
// Order matters: the ragdoll section uses the extern "C" RW animation entry
// points, so the address-macros of the dismemberment section (same names,
// different calling convention) MUST stay below it.

// ---- Includes (deduped union of the two TUs) ----
#include "../skygfx.h"
#include "sftbdyphsx.h"
#include "../extras/ragdoll_death.h"	// g_bulletWorld, g_ragdollDeathEnable, COLGROUP_*
#include "../rw/gta.h"
#include "../rw/rphanim.h"

#include <cmath>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <LinearMath/btDefaultMotionState.h>
#include <LinearMath/btTransform.h>
#include <LinearMath/btVector3.h>
#include <windows.h>
#include <stdio.h>
#include <float.h>

// ---- RAGDOLL (was src/entities/ragdoll.cpp) ----

extern "C" {
RpHAnimHierarchy *GetAnimHierarchyFromSkinClump(RpClump *clump);
RwInt32 RpHAnimIDGetIndex(RpHAnimHierarchy *hierarchy, RwInt32 ID);
RwMatrix *RpHAnimHierarchyGetMatrixArray(RpHAnimHierarchy *hierarchy);
RwBool RpHAnimHierarchyUpdateMatrices(RpHAnimHierarchy *hierarchy);
RwBool RtQuatConvertFromMatrix(RtQuat *qpQuat, const RwMatrix *mpMatrix);
RtQuat *RtQuatRotate(RtQuat *quat, const RwV3d *axis, RwReal angle, RwOpCombineType combineOp);
RwV3d *RtQuatTransformVectors(RwV3d *vectorsOut, const RwV3d *vectorsIn, const RwInt32 numPoints, const RtQuat *quat);
void RtQuatSetupSlerpCache(RtQuat *qpFrom, RtQuat *qpTo, RtQuatSlerpCache *sCache);
}

static void RagdollQuatSlerp(RtQuat *result, const RtQuat *from, const RtQuat *to, float t) {
	RtQuatSlerpCache cache;
	RtQuatSetupSlerpCache((RtQuat*)from, (RtQuat*)to, &cache);
	float omega = cache.omega;
	float sinOmega = (float)std::sin(omega);
	if (sinOmega <= 1e-6f) {
		// Degenerate omega (near-identical quats — e.g. the init pose where
		// orientation == animQuat): the old recip=0 path zeroed BOTH scales
		// and returned a ZERO quaternion, which then fed the matrix
		// converter as garbage. The quats are ~equal here — hand back the
		// sign-adjusted target (a unit quat from the cache).
		*result = cache.raTo;
		return;
	}
	float recipSinOmega = 1.0f / sinOmega;
	float scaleFrom = (float)std::sin((1.0f - t) * omega) * recipSinOmega;
	float scaleTo = (float)std::sin(t * omega) * recipSinOmega;
	result->real = from->real * scaleFrom + cache.raTo.real * scaleTo;
	result->imag.x = from->imag.x * scaleFrom + cache.raTo.imag.x * scaleTo;
	result->imag.y = from->imag.y * scaleFrom + cache.raTo.imag.y * scaleTo;
	result->imag.z = from->imag.z * scaleFrom + cache.raTo.imag.z * scaleTo;
}

int32 aBONETAG_ENUM_TAB[RAGDOLL_MAX_BONES] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8,
	21, 22, 23, 24, 25, 26,
	31, 32, 33, 34, 35, 36,
	41, 42, 43, 44,
	51, 52, 53, 54,
	201, 301, 302,
};

static RagdollBone *FindBone(RagdollBone *bones, int32 tag) {
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag == tag)
			return &bones[i];
	}
	return nullptr;
}

// ---- RagdollBone ----

void RagdollBone::Init(int32 tag, RpHAnimKeyFrame *keyFrame, const tBoneInfo *boneInfo) {
	boneTag = tag;
	parent = nullptr;
	numChildren = 0;

	pos.x = keyFrame->t.x;
	pos.y = keyFrame->t.y;
	pos.z = keyFrame->t.z;
	orientation = keyFrame->q;
	animQuat = keyFrame->q;

	vel.x = vel.y = vel.z = 0.0f;

	if (boneInfo) {
		limitMin = boneInfo->m_Min;
		limitMax = boneInfo->m_Max;
	} else {
		limitMin.x = limitMin.y = limitMin.z = -45.0f;
		limitMax.x = limitMax.y = limitMax.z = 45.0f;
	}

	memset(&worldMat, 0, sizeof(RwMatrix));
}

void RagdollBone::CalcWldMat(const RwMatrix *parentMat) {
	// Build rotation matrix from quaternion directly using RtQuatUnitConvertToMatrixMacro
	// OUT-OF-PLACE compose (softbody.cpp CalcWldMat idiom, shipped BoneNode_c
	// shape): the game's RwMatrixMultiply composes component-wise and is NOT
	// alias-safe for out == in1 — the old in-place multiply read half-updated
	// rows of worldMat while writing the result back into it.
	RwMatrix local;
	RtQuatUnitConvertToMatrixMacro(&orientation, &local);
	// Set position
	local.pos.x = pos.x;
	local.pos.y = pos.y;
	local.pos.z = pos.z;
	rwMatrixSetFlags(&local, rwMATRIXTYPEORTHONORMAL);

	// worldMat = localMat * parentMat
	RwMatrixMultiply(&worldMat, &local, parentMat);

	for (int i = 0; i < numChildren; i++) {
		children[i]->CalcWldMat(&worldMat);
	}
}

void RagdollBone::BlendKeyframe(float blend) {
	if (blend <= 0.0f) return;
	if (blend >= 1.0f) {
		orientation = animQuat;
		return;
	}

	RtQuat result;
	RagdollQuatSlerp(&result, &animQuat, &orientation, blend);
	orientation = result;
}

void RagdollBone::Limit() {
	CVector euler;
	{
		float sinr_cosp = 2.0f * (orientation.real * orientation.imag.x + orientation.imag.y * orientation.imag.z);
		float cosr_cosp = 1.0f - 2.0f * (orientation.imag.x * orientation.imag.x + orientation.imag.y * orientation.imag.y);
		euler.x = (float)std::atan2(sinr_cosp, cosr_cosp) * (180.0f / 3.14159265f);

		float sinp = 2.0f * (orientation.real * orientation.imag.y - orientation.imag.z * orientation.imag.x);
		if (std::abs(sinp) >= 1.0f)
			euler.y = (float)std::copysign(90.0f, sinp);
		else
			euler.y = (float)std::asin(sinp) * (180.0f / 3.14159265f);

		float siny_cosp = 2.0f * (orientation.real * orientation.imag.z + orientation.imag.x * orientation.imag.y);
		float cosy_cosp = 1.0f - 2.0f * (orientation.imag.y * orientation.imag.y + orientation.imag.z * orientation.imag.z);
		euler.z = (float)std::atan2(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
	}

	if (euler.x < limitMin.x) euler.x = limitMin.x;
	if (euler.x > limitMax.x) euler.x = limitMax.x;
	if (euler.y < limitMin.y) euler.y = limitMin.y;
	if (euler.y > limitMax.y) euler.y = limitMax.y;
	if (euler.z < limitMin.z) euler.z = limitMin.z;
	if (euler.z > limitMax.z) euler.z = limitMax.z;

	{
		float hx = euler.x * 0.5f * (3.14159265f / 180.0f);
		float hy = euler.y * 0.5f * (3.14159265f / 180.0f);
		float hz = euler.z * 0.5f * (3.14159265f / 180.0f);
		float cr = (float)std::cos(hx), sr = (float)std::sin(hx);
		float cp = (float)std::cos(hy), sp = (float)std::sin(hy);
		float cy = (float)std::cos(hz), sy = (float)std::sin(hz);
		orientation.real = cr*cp*cy + sr*sp*sy;
		orientation.imag.x = sr*cp*cy - cr*sp*sy;
		orientation.imag.y = cr*sp*cy + sr*cp*sy;
		orientation.imag.z = cr*cp*sy - sr*sp*cy;
	}
}

// ---- Ragdoll_c ----

Ragdoll_c::Ragdoll_c() : m_pPed(nullptr), m_pRootBone(nullptr), m_blend(1.0f) {
	m_prevPos.x = m_prevPos.y = m_prevPos.z = 0.0f;
	m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
	m_acceleration.x = m_acceleration.y = m_acceleration.z = 0.0f;
}

Ragdoll_c::~Ragdoll_c() {}

bool Ragdoll_c::Init(CPed *ped) {
	m_pPed = ped;
	m_blend = 0.5f;
	m_prevPos.x = m_prevPos.y = m_prevPos.z = 0.0f;
	m_velocity.x = m_velocity.y = m_velocity.z = 0.0f;
	m_acceleration.x = m_acceleration.y = m_acceleration.z = 0.0f;

	// Motion overlay mode: do NOT capture bones or freeze animation.
	// We only track the ped and apply secondary motion forces via ProcessMotion.
	// Verify ped has a valid clump with hierarchy.
	// Zombie-slot guard: m_pPed was set above, so a validation failure MUST
	// clear it again — AddRagdoll/FindForPed treat the slot as active while
	// m_pPed != nullptr, and a failed Init otherwise strands an unusable
	// slot on that ped until it leaves render range.
	void *rwObject = *(void**)((uint32)m_pPed + 0x18);
	if (!rwObject) { m_pPed = nullptr; return false; }
	RpClump *clump = (RpClump*)rwObject;
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier) { m_pPed = nullptr; return false; }
	if (hier->numNodes < 1) { m_pPed = nullptr; return false; }

	// Reset per-bone state: a reused pool slot carries the PREVIOUS ped's
	// parent/children links (SetupBoneHierarchy only appends, never clears),
	// so without this reset numChildren grew on every re-Init and Update
	// visited the same child twice per frame.
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		m_bones[i].boneTag = aBONETAG_ENUM_TAB[i];
		m_bones[i].parent = nullptr;
		m_bones[i].numChildren = 0;
		m_bones[i].vel.x = m_bones[i].vel.y = m_bones[i].vel.z = 0.0f;
	}

	SetupBoneHierarchy();
	return true;
}

void Ragdoll_c::Exit() {
	if (!m_pPed) return;
	m_pPed = nullptr;
}

void Ragdoll_c::Update(float deltaTime) {
	if (!m_pPed) return;

	void *rwObject = *(void**)((uint32)m_pPed + 0x18);
	if (!rwObject) return;
	RpClump *clump = (RpClump*)rwObject;
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier) return;
	if (hier->numNodes < 1) return;

	RwMatrix *matArray = RpHAnimHierarchyGetMatrixArray(hier);
	if (!matArray) return;

	// Physics step: resolve forces into bone velocity changes
	ResolveSystem(deltaTime);

	// Apply ragdoll rotation deltas ON TOP of animation matrices
	// This blends: animation provides the base pose, ragdoll adds secondary motion
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		RagdollBone *bone = &m_bones[i];
		if (bone->boneTag == 0) continue;  // skip root

		float speed = sqrtf(bone->vel.x * bone->vel.x +
		                     bone->vel.y * bone->vel.y +
		                     bone->vel.z * bone->vel.z);
		if (speed < 0.001f) continue;

		// Find this bone's index in the hierarchy
		int32 hierIdx = RpHAnimIDGetIndex(hier, bone->boneTag);
		if (hierIdx < 0 || hierIdx >= hier->numNodes) continue;

		RwMatrix *boneMat = &matArray[hierIdx];

		// Compute rotation axis from velocity
		RwV3d axis;
		axis.x = bone->vel.x;
		axis.y = bone->vel.y;
		axis.z = bone->vel.z;
		float axisLen = RwV3dNormalize(&axis, &axis);
		if (axisLen < 0.001f) continue;

		// Scale rotation angle by blend and speed — strong secondary motion
		float angle = axisLen * deltaTime * m_blend * 100.0f;
		if (angle > 4.0f) angle = 4.0f;  // clamp max rotation per frame

		// Build rotation matrix and apply to bone's world matrix
		RwV3d right, up, at;
		RwV3dAssign(&right, (RwV3d*)&boneMat->right);
		RwV3dAssign(&up, (RwV3d*)&boneMat->up);
		RwV3dAssign(&at, (RwV3d*)&boneMat->at);

		// Rodrigues rotation around axis by angle
		float cosA = (float)cos(angle);
		float sinA = (float)sin(angle);
		float oneMinusCos = 1.0f - cosA;

		// Rotate each basis vector
		RwV3d newRight, newUp, newAt;

		newRight.x = right.x * (cosA + axis.x*axis.x*oneMinusCos) +
		             right.y * (axis.x*axis.y*oneMinusCos - axis.z*sinA) +
		             right.z * (axis.x*axis.z*oneMinusCos + axis.y*sinA);
		newRight.y = right.x * (axis.y*axis.x*oneMinusCos + axis.z*sinA) +
		             right.y * (cosA + axis.y*axis.y*oneMinusCos) +
		             right.z * (axis.y*axis.z*oneMinusCos - axis.x*sinA);
		newRight.z = right.x * (axis.z*axis.x*oneMinusCos - axis.y*sinA) +
		             right.y * (axis.z*axis.y*oneMinusCos + axis.x*sinA) +
		             right.z * (cosA + axis.z*axis.z*oneMinusCos);

		newUp.x = up.x * (cosA + axis.x*axis.x*oneMinusCos) +
		          up.y * (axis.x*axis.y*oneMinusCos - axis.z*sinA) +
		          up.z * (axis.x*axis.z*oneMinusCos + axis.y*sinA);
		newUp.y = up.x * (axis.y*axis.x*oneMinusCos + axis.z*sinA) +
		          up.y * (cosA + axis.y*axis.y*oneMinusCos) +
		          up.z * (axis.y*axis.z*oneMinusCos - axis.x*sinA);
		newUp.z = up.x * (axis.z*axis.x*oneMinusCos - axis.y*sinA) +
		          up.y * (axis.z*axis.y*oneMinusCos + axis.x*sinA) +
		          up.z * (cosA + axis.z*axis.z*oneMinusCos);

		newAt.x = at.x * (cosA + axis.x*axis.x*oneMinusCos) +
		          at.y * (axis.x*axis.y*oneMinusCos - axis.z*sinA) +
		          at.z * (axis.x*axis.z*oneMinusCos + axis.y*sinA);
		newAt.y = at.x * (axis.y*axis.x*oneMinusCos + axis.z*sinA) +
		          at.y * (cosA + axis.y*axis.y*oneMinusCos) +
		          at.z * (axis.y*axis.z*oneMinusCos - axis.x*sinA);
		newAt.z = at.x * (axis.z*axis.x*oneMinusCos - axis.y*sinA) +
		          at.y * (axis.z*axis.y*oneMinusCos + axis.x*sinA) +
		          at.z * (cosA + axis.z*axis.z*oneMinusCos);

		*(RwV3d*)&boneMat->right = newRight;
		*(RwV3d*)&boneMat->up = newUp;
		*(RwV3d*)&boneMat->at = newAt;
	}

	// Update child matrices
	RpHAnimHierarchyUpdateMatrices(hier);

	// Friction
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		m_bones[i].vel.x *= 0.85f;
		m_bones[i].vel.y *= 0.85f;
		m_bones[i].vel.z *= 0.85f;
	}
}

void Ragdoll_c::SetBlend(float blend) {
	m_blend = blend;
}

void Ragdoll_c::ApplyForce(int32 boneTag, float force, RwV3d dir) {
	RagdollBone *bone = FindBone(m_bones, boneTag);
	if (!bone) return;

	bone->vel.x += dir.x * force;
	bone->vel.y += dir.y * force;
	bone->vel.z += dir.z * force;
}

// ---- Motion overlay: called per-ped per frame ----
// Computes velocity/acceleration from position and applies
// secondary motion forces to bones (spine lean, arm sway, head bob)
void Ragdoll_c::ProcessMotion(float deltaTime, CVector &pedPos, CVector &pedVel) {
	if (!m_pPed) return;
	if (deltaTime <= 0.0f) return;

	// Compute acceleration from velocity change
	m_acceleration.x = (pedVel.x - m_velocity.x) / deltaTime;
	m_acceleration.y = (pedVel.y - m_velocity.y) / deltaTime;
	m_acceleration.z = (pedVel.z - m_velocity.z) / deltaTime;
	m_velocity = pedVel;
	m_prevPos = pedPos;

	// Speed magnitude (ground plane)
	float speed = sqrtf(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

	// Scale forces by speed — stationary peds get no secondary motion
	float forceScale = fminf(speed * 1.0f, 2.0f);
	if (forceScale < 0.01f) return;

	// Inertia force: opposite to acceleration (body lags behind movement changes)
	RwV3d inertiaForce;
	inertiaForce.x = -m_acceleration.x * forceScale * 0.4f;
	inertiaForce.y = -m_acceleration.y * forceScale * 0.4f;
	inertiaForce.z = -m_acceleration.z * forceScale * 0.2f;

	// Apply inertia to upper spine bones (they lean/flex more than pelvis)
	// BONE_SPINE = 2, BONE_SPINE1 = 3, BONE_NECK = 4, BONE_HEAD = 5
	ApplyForce(2, 1.5f, inertiaForce);   // spine — main lean
	ApplyForce(3, 2.0f, inertiaForce);   // upper spine — more lean
	ApplyForce(4, 1.2f, inertiaForce);   // neck
	ApplyForce(5, 0.8f, inertiaForce);   // head — dampened

	// Arm sway: arms swing opposite to movement direction
	RwV3d armSway;
	armSway.x = -m_velocity.x * forceScale * 0.15f;
	armSway.y = -m_velocity.y * forceScale * 0.15f;
	armSway.z = speed * 0.05f;  // slight upward during movement

	// Right arm: BONE_R_UPPER_ARM=22, BONE_R_FORE_ARM=23
	ApplyForce(22, 1.0f, armSway);
	ApplyForce(23, 0.8f, armSway);
	// Left arm: BONE_L_UPPER_ARM=32, BONE_L_FORE_ARM=33
	ApplyForce(32, 1.0f, armSway);
	ApplyForce(33, 0.8f, armSway);

	// Low blend for living peds (subtle secondary motion)
	// The blend factor is set per-frame based on ped state
}

void Ragdoll_c::ResolveSystem(float deltaTime) {
	// Motion overlay: NO gravity (peds don't fall), only friction on forces
	// The ProcessMotion function applies velocity-based forces to bone velocities.
	// ResolveSystem just clamps and dampens them.
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		// Clamp max velocity per bone
		float speed = sqrtf(m_bones[i].vel.x * m_bones[i].vel.x +
		                     m_bones[i].vel.y * m_bones[i].vel.y +
		                     m_bones[i].vel.z * m_bones[i].vel.z);
		if (speed > 15.0f) {
			float scale = 15.0f / speed;
			m_bones[i].vel.x *= scale;
			m_bones[i].vel.y *= scale;
			m_bones[i].vel.z *= scale;
		}
	}
}

void Ragdoll_c::SetupBoneHierarchy() {
	m_pRootBone = FindBone(m_bones, 0);
	if (!m_pRootBone) return;

	tBoneInfo *infos = (tBoneInfo*)0x8D26D0;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		int32 tag = m_bones[i].boneTag;
		for (int j = 0; j < MAX_BONE_NUM; j++) {
			if (infos[j].m_current == tag && infos[j].m_prev > 0) {
				RagdollBone *parentBone = FindBone(m_bones, infos[j].m_prev);
				if (parentBone) {
					m_bones[i].parent = parentBone;
					if (parentBone->numChildren < 8) {
						parentBone->children[parentBone->numChildren++] = &m_bones[i];
					}
				}
				break;
			}
		}
	}
}

// ---- RagdollManager_c ----

RagdollManager_c::RagdollManager_c() {}
RagdollManager_c::~RagdollManager_c() {}

bool RagdollManager_c::Init() {
	return true;
}

void RagdollManager_c::Exit() {
	for (int i = 0; i < MAX_RAGDOLLS; i++) {
		if (m_ragdolls[i].IsActive()) {
			m_ragdolls[i].Exit();
		}
	}
}

void RagdollManager_c::Reset() {
	Exit();
}

void RagdollManager_c::Update(float deltaTime) {
	for (int i = 0; i < MAX_RAGDOLLS; i++) {
		if (m_ragdolls[i].IsActive()) {
			m_ragdolls[i].Update(deltaTime);
		}
	}
}

Ragdoll_c *RagdollManager_c::FindForPed(CPed *ped) {
	for (int i = 0; i < MAX_RAGDOLLS; i++) {
		if (m_ragdolls[i].IsActive() && m_ragdolls[i].GetPed() == ped) {
			return &m_ragdolls[i];
		}
	}
	return nullptr;
}

Ragdoll_c *RagdollManager_c::AddRagdoll(CPed *ped) {
	if (FindForPed(ped)) return nullptr;
	for (int i = 0; i < MAX_RAGDOLLS; i++) {
		if (!m_ragdolls[i].IsActive()) {
			if (m_ragdolls[i].Init(ped)) {
				return &m_ragdolls[i];
			}
			return nullptr;
		}
	}
	return nullptr;
}

void RagdollManager_c::RemoveRagdoll(Ragdoll_c *ragdoll) {
	if (!ragdoll) return;
	ragdoll->Exit();
}

// ---- Visibility-based pool management ----
// Scans GTA SA ped pool, assigns/removes ragdolls based on distance
void RagdollManager_c::ProcessAllPeds(float deltaTime) {
	// GTA SA v1.0 US: CPool<CPed>* at 0xB74490
	// CPool layout: m_pSlots (T*), m_pSlotInfos (union with m_bFree bit), m_nNumSlots
	struct PedSlotInfo { unsigned char id : 7; bool free : 1; };
	struct PedPool {
		void *m_pSlots;
		PedSlotInfo *m_pSlotInfos;
		int m_nNumSlots;
	};
	PedPool *pool = *(PedPool**)0xB74490;
	if (!pool || !pool->m_pSlots || !pool->m_pSlotInfos) return;

	// Camera/player position for distance check
	CVector camPos;
	if (Scene.camera) {
		RwV3d *rp = RwMatrixGetPos(RwFrameGetLTM(RwCameraGetFrame(Scene.camera)));
		camPos.x = rp->x; camPos.y = rp->y; camPos.z = rp->z;
	} else {
		return;
	}

	// Iterate all peds in pool
	for (int i = 0; i < pool->m_nNumSlots; i++) {
		if (pool->m_pSlotInfos[i].free) continue;

		CPed *ped = (CPed*)((uint8*)pool->m_pSlots + i * 0x7C4);

		// CPed+0x18 = RwObject* (RpClump* for skinned peds)
		void *rwObject = *(void**)((uint32)ped + 0x18);
		if (!rwObject) continue;

		// Check if ped has a valid clump with skin hierarchy
		RpClump *clump = (RpClump*)rwObject;
		RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(clump);
		if (!hier) continue;

		// Get ped position from the clump's frame LTM
		CVector pedPos;
		RwFrame *frame = *(RwFrame**)((uint32)rwObject + 0x04);
		if (!frame) continue;
		RwMatrix *ltm = RwFrameGetLTM(frame);
		if (!ltm) continue;
		pedPos.x = ltm->pos.x;
		pedPos.y = ltm->pos.y;
		pedPos.z = ltm->pos.z;

		// Distance from camera
		float dx = pedPos.x - camPos.x;
		float dy = pedPos.y - camPos.y;
		float dz = pedPos.z - camPos.z;
		float distSq = dx*dx + dy*dy + dz*dz;
		float dist = sqrtf(distSq);

		Ragdoll_c *existing = FindForPed(ped);

		if (dist < RAGDOLL_DIST_HIGH) {
			// Within range — ensure ragdoll is assigned
			if (!existing) {
				Ragdoll_c *rag = AddRagdoll(ped);
				if (rag) {
					// Close range: strong override, far: moderate
					float blend = (dist < RAGDOLL_DIST_LOW) ? 0.5f : 0.25f;
					rag->SetBlend(blend);
				}
			} else {
				// Update blend based on distance
				float blend = (dist < RAGDOLL_DIST_LOW) ? 0.5f : 0.25f;
				existing->SetBlend(blend);

				// Feed motion data
				CVector vel = *(CVector*)((uint32)ped + 0x44);
				existing->ProcessMotion(deltaTime, pedPos, vel);
			}
		} else {
			// Out of range — remove ragdoll
			if (existing) {
				RemoveRagdoll(existing);
			}
		}
	}
}

RagdollManager_c g_ragdollMan;

// ---- DISMEMBERMENT (was src/extras/dismember.cpp) ----

// ============================================================
// External globals from ragdoll_death.cpp
// ============================================================
extern btDiscreteDynamicsWorld *g_bulletWorld;
extern bool g_ragdollDeathEnable;

// RW SDK function macros (game addresses, SA 1.0 US)
typedef void *(*GetAnimHierarchyFromSkinClump_t)(void *clump);
#define GetAnimHierarchyFromSkinClump(clump)  ((GetAnimHierarchyFromSkinClump_t)0x734A40)(clump)
typedef int (*RpHAnimIDGetIndex_t)(void *hierarchy, int ID);
#define RpHAnimIDGetIndex(h, id)  ((RpHAnimIDGetIndex_t)0x7C51A0)(h, id)

// RpSkin
typedef void *(*RpSkinGeometryGetSkin_t)(void *geo);
#define RpSkinGeometryGetSkin(g)  ((RpSkinGeometryGetSkin_t)0x7C77A0)(g)
typedef unsigned int (*RpSkinGetNumBones_t)(void *skin);
#define RpSkinGetNumBones(s)      ((RpSkinGetNumBones_t)0x7C77E0)(s)
typedef void *(*RpSkinGetVertexBoneWeights_t)(void *skin);
#define RpSkinGetVertexBoneWeights(s)  ((RpSkinGetVertexBoneWeights_t)0x7C77F0)(s)
typedef const unsigned int *(*RpSkinGetVertexBoneIndices_t)(void *skin);
#define RpSkinGetVertexBoneIndices(s)  ((RpSkinGetVertexBoneIndices_t)0x7C7800)(s)
typedef const void *(*RpSkinGetSkinToBoneMatrices_t)(void *skin);
#define RpSkinGetSkinToBoneMatrices(s) ((RpSkinGetSkinToBoneMatrices_t)0x7C7810)(s)

// RpWorld
typedef void *(*RpGeometryCreate_t)(int, int, unsigned int);
#define RpGeometryCreate(nv, nt, fmt)  ((RpGeometryCreate_t)0x74CA90)(nv, nt, fmt)
typedef int  (*RpGeometryDestroy_t)(void*);
#define RpGeometryDestroy(g)           ((RpGeometryDestroy_t)0x74CCC0)(g)
typedef void *(*RpGeometryLock_t)(void*, int);
#define RpGeometryLock(g, m)           ((RpGeometryLock_t)0x74C7D0)(g, m)
typedef void *(*RpGeometryUnlock_t)(void*);
#define RpGeometryUnlock(g)            ((RpGeometryUnlock_t)0x74C800)(g)

typedef void *(*RpAtomicCreate_t)();
#define RpAtomicCreate()               ((RpAtomicCreate_t)0x749C50)()
typedef int  (*RpAtomicDestroy_t)(void*);
#define RpAtomicDestroy(a)             ((RpAtomicDestroy_t)0x749DC0)(a)
typedef void *(*RpClumpCreate_t)();
#define RpClumpCreate()                ((RpClumpCreate_t)0x74A290)()
typedef int  (*RpClumpAddAtomic_t)(void*, void*);
// RpClumpAddAtomic at 0x74A490 — byte-verified 2026-11-02 (inClumpLink +0x40/+0x44
// -> clump root +8; atomic->clump +0x3C). 0x748C50/0x748E90 are mid-function game code.
#define RpClumpAddAtomic(c, a)         ((RpClumpAddAtomic_t)0x74A490)(c, a)
typedef int  (*RpClumpDestroy_t)(void*);
#define RpClumpDestroy(c)              ((int(*)(void*))0x74A310)(c)	// byte-verified 2026-11-02 (0x748C50 = mid-function game code, NOT RpClumpDestroy)

typedef void *(*RwFrameCreate_t)();
#define RwFrameCreate()                ((RwFrameCreate_t)0x7F0410)()
typedef int  (*RwFrameUpdateObjects_t)(void*);
#define RwFrameUpdateObjects(f)        ((RwFrameUpdateObjects_t)0x7F0910)(f)

typedef void *(*RpMaterialCreate_t)();
#define RpMaterialCreate()             ((RpMaterialCreate_t)0x74D990)()
typedef void *(*RpAtomicSetGeometry_t)(void*, void*, unsigned int);
// RpAtomicSetGeometry at 0x749D40 — gta-reversed rpworld.h:2514
#define RpAtomicSetGeometry(a,g,f)     ((RpAtomicSetGeometry_t)0x749D40)(a,g,f)

// Immediate mode render state pattern (from postfx.cpp)
typedef void (*ImmediateModeRS_t)();
#define IMM_STORE()  ((ImmediateModeRS_t)0x700CC0)()
#define IMM_SET()    ((ImmediateModeRS_t)0x700D70)()
#define IMM_RESTORE() ((ImmediateModeRS_t)0x700E00)()

// RwMatrixWeights
struct RwMatrixWeights { float w0, w1, w2, w3; };

// ============================================================
// Config
// ============================================================
bool g_dismemberEnable = true;
static float g_gibLifetime = DISMEMBER_DEFAULT_LIFETIME;
static float g_bloodInterval = 0.2f;
static float g_impulseScale = 8.0f;

GibInstance g_dismemberGibs[DISMEMBER_MAX_GIBS] = {};
static int s_gibCount = 0;

// ============================================================
// ePedNode → bone tag mapping (RpAnimBlend.cpp:222)
// ============================================================
static int NodeToBone(int nodeId) {
	switch (nodeId) {
		case 1:  return 3;
		case 2:  return 5;
		case 3:  return 32;
		case 4:  return 22;
		case 5:  return 35;
		case 6:  return 25;
		case 7:  return 31;
		case 8:  return 21;
		case 9:  return 44;
		case 10: return 54;
		case 11: return 35;
		case 12: return 25;
		case 13: return 33;
		case 14: return 23;
		case 17: case 18: return 5;
		default: return 5;
	}
}

static float GetGameTime() { return *(float*)0xB7CB58; }

static float ReadIniF(const char *s, const char *k, float d) {
	char buf[64]=""; GetPrivateProfileStringA(s,k,"",buf,64,".\\ragdoll.ini");
	return buf[0] ? (float)atof(buf) : d;
}
static int ReadIniI(const char *s, const char *k, int d) {
	return GetPrivateProfileIntA(s,k,d,".\\ragdoll.ini");
}

// ============================================================
// Dismember_Init / Shutdown
// ============================================================
void Dismember_Init(void) {
	g_dismemberEnable = ReadIniI("DISMEMBER","enable",1) != 0;
	g_gibLifetime = ReadIniF("DISMEMBER","gibLifetime",DISMEMBER_DEFAULT_LIFETIME);
	g_bloodInterval = ReadIniF("DISMEMBER","bloodSpurtInterval",0.2f);
	g_impulseScale = ReadIniF("DISMEMBER","impulseScale",8.0f);
	dbglog("Dismember_Init: enable=%d", g_dismemberEnable);
}

void Dismember_Shutdown(void) {
	for (int i = 0; i < DISMEMBER_MAX_GIBS; i++) {
		GibInstance *g = &g_dismemberGibs[i];
		if (!g->active) continue;
		if (g->body) {
			// removeRigidBody needs the world; deleting the body does not.
			// Guarding only the remove keeps body+motion-state teardown
			// correct even if the shared world is already gone.
			if (g_bulletWorld) g_bulletWorld->removeRigidBody(g->body);
			delete g->body->getMotionState(); delete g->body;
		}
		delete g->shape;
		if (g->clump) RpClumpDestroy(g->clump);
		g->active = false;
	}
	s_gibCount = 0;
	dbglog("Dismember_Shutdown");
}

// ============================================================
// Internal: gib alloc/free
// ============================================================
static GibInstance *GibAlloc() {
	for (int i = 0; i < DISMEMBER_MAX_GIBS; i++) {
		if (g_dismemberGibs[i].active && (GetGameTime() - g_dismemberGibs[i].spawnTime) > g_dismemberGibs[i].lifetime) {
			GibInstance *g = &g_dismemberGibs[i];
			if (g->body && g_bulletWorld) {
				g_bulletWorld->removeRigidBody(g->body);
				delete g->body->getMotionState(); delete g->body;
			}
			delete g->shape;
			if (g->clump) RpClumpDestroy(g->clump);
			memset(g,0,sizeof(GibInstance)); g->active = true;
			return g;
		}
	}
	for (int i = 0; i < DISMEMBER_MAX_GIBS; i++) {
		if (!g_dismemberGibs[i].active) {
			memset(&g_dismemberGibs[i],0,sizeof(GibInstance));
			g_dismemberGibs[i].active = true; s_gibCount++;
			return &g_dismemberGibs[i];
		}
	}
	return nullptr;
}

static void GibFree(GibInstance *g) {
	if (!g||!g->active) return;
	if (g->body && g_bulletWorld) {
		g_bulletWorld->removeRigidBody(g->body);
		delete g->body->getMotionState(); delete g->body;
	}
	delete g->shape;
	if (g->clump) RpClumpDestroy(g->clump);
	g->active = false; s_gibCount--;
	dbglog("Dismember: gib freed, remaining=%d", s_gibCount);
}

// ============================================================
// BuildGibClump — bake a limb mesh from the ped's RpSkin.
// V4 FIX: Clone material and AddRef the texture instead of
// sharing the source material pointer (prevents use-after-free
// when RpGeometryDestroy decrements material refcount).
// V5 FIX: Use RpAtomicSetGeometry (game WRAPPER at 0x749D40)
// to properly build D3D9 ResEntries instead of raw pointer pokes.
// ============================================================
static void *BuildGibClump(void *pedClump, int boneTag) {
	if (!pedClump) return nullptr;

	// Get first atomic
	void *firstAtomic = nullptr;
	typedef void *(*AtomicCB)(void*, void*);
	typedef void *(*ForAllAtomics_t)(void*, AtomicCB, void*);
	auto cb0 = [](void *a, void *d) -> void* { if (!*(void**)d) *(void**)d = a; return a; };
	((ForAllAtomics_t)0x749B70)(pedClump, cb0, &firstAtomic);
	if (!firstAtomic) return nullptr;

	// RpAtomic layout (rpworld.h:2643): object(RwObjectHasFrame=20) +0, repEntry +0x14, geometry +0x18
	// Header: external/d3d9/rpworld.h:2643 — verified against RW 3.6 SDK
	void *srcGeo = *(void**)((uintptr_t)firstAtomic + 0x18);
	if (!srcGeo) return nullptr;

	void *skin = RpSkinGeometryGetSkin(srcGeo);
	if (!skin) return nullptr;

	RwMatrixWeights *weights = (RwMatrixWeights*)RpSkinGetVertexBoneWeights(skin);
	const unsigned int *indices = RpSkinGetVertexBoneIndices(skin);
	if (!weights || !indices) return nullptr;

	// RpGeometry layout (rpworld.h:1414, RW 3.6 SDK):
	// object(RwObject=8) +0, flags +8, lockedSinceLastInst +0xC, refCount +0xE,
	// numTriangles +0x10, numVertices +0x14, numMorphTargets +0x18, numTexCoordSets +0x1C,
	// matList +0x20, triangles +0x2C, preLitLum +0x30, texCoords +0x34[8],
	// mesh +0x54, repEntry +0x58, morphTarget +0x5C
	int numVerts  = *(int*)((uintptr_t)srcGeo + 0x14);
	int numTris   = *(int*)((uintptr_t)srcGeo + 0x10);
	int numTexSets = *(int*)((uintptr_t)srcGeo + 0x1C);
	void *triangles = *(void**)((uintptr_t)srcGeo + 0x2C);
	void *morphTarget = *(void**)((uintptr_t)srcGeo + 0x5C);
	void **materials = *(void***)((uintptr_t)srcGeo + 0x20); // matList.materials at +0x20
	int numMaterials = *(int*)((uintptr_t)srcGeo + 0x24);   // matList.numMaterials at +0x24
	if (!triangles || !morphTarget || numVerts <= 0 || numTris <= 0) return nullptr;

	// RpMorphTarget layout (rpworld.h:1388): parentGeom +0, boundingSphere +4, verts +0x14, normals +0x18
	void *verts = *(void**)((uintptr_t)morphTarget + 0x14);
	void *norms = *(void**)((uintptr_t)morphTarget + 0x18);
	// texCoords is RpGeometry.texCoords[0] at +0x34 (first entry of RwTexCoords*[8] array)
	void *texCoords = (numTexSets > 0) ? *(void**)((uintptr_t)srcGeo + 0x34) : nullptr;

	void *hier = GetAnimHierarchyFromSkinClump(pedClump);
	if (!hier) return nullptr;
	int hidx = RpHAnimIDGetIndex(hier, boneTag);
	// removeBone[32] / boneWorld[32] index domain: a hierarchy node index
	// >= 32 (multi-bone ped variants) would write past removeBone — the
	// bake cannot represent it, so refuse.
	if (hidx < 0 || hidx >= 32) return nullptr;

	bool removeBone[32] = {}; removeBone[hidx] = true;

	bool *vertInSet = (bool*)malloc(numVerts * sizeof(bool));
	if (!vertInSet) return nullptr;
	int vertsInSet = 0;
	for (int v = 0; v < numVerts; v++) {
		int domBone = indices[v*4] & 0xFF;
		if (domBone < 32 && removeBone[domBone] && weights[v].w0 > 0.0001f) {
			vertInSet[v] = true; vertsInSet++;
		} else vertInSet[v] = false;
	}
	if (vertsInSet < 3) { free(vertInSet); return nullptr; }

	int *vertRemap = (int*)malloc(numVerts * sizeof(int));
	if (!vertRemap) { free(vertInSet); return nullptr; }
	int newIdx = 0;
	for (int v = 0; v < numVerts; v++) {
		if (vertInSet[v]) vertRemap[v] = newIdx++;
		else vertRemap[v] = -1;
	}

	struct Tri { unsigned short v[3]; unsigned short mat; };
	Tri *srcTris = (Tri*)triangles;
	int *triRemap = (int*)malloc(numTris * sizeof(int));
	if (!triRemap) { free(vertInSet); free(vertRemap); return nullptr; }
	int newTris = 0;
	for (int t = 0; t < numTris; t++) {
		if (vertInSet[srcTris[t].v[0]] && vertInSet[srcTris[t].v[1]] && vertInSet[srcTris[t].v[2]])
			triRemap[t] = newTris++;
		else triRemap[t] = -1;
	}
	if (newTris < 1 || newIdx < 3) {
		free(vertInSet); free(vertRemap); free(triRemap);
		return nullptr;
	}

	unsigned int fmt = 0x11 | 0x100; // positions | norms | textured
	void *newGeo = RpGeometryCreate(newIdx, newTris, fmt);
	if (!newGeo) { free(vertInSet); free(vertRemap); free(triRemap); return nullptr; }

	RpGeometryLock(newGeo, 0x0F);

	const RwMatrix *skinToBone = (const RwMatrix*)RpSkinGetSkinToBoneMatrices(skin);
	int numBones = RpSkinGetNumBones(skin);
	RwMatrix *matArray = RpHAnimHierarchyGetMatrixArray((RpHAnimHierarchy*)hier);
	if (!matArray) {
		RpGeometryUnlock(newGeo); RpGeometryDestroy(newGeo);
		free(vertInSet); free(vertRemap); free(triRemap);
		return nullptr;
	}

	RwMatrix boneWorld[32];
	int maxB = numBones < 32 ? numBones : 32;
	for (int b = 0; b < maxB; b++) {
		if (b < 32 && matArray) {
			typedef void (*MulM_t)(RwMatrix*, const RwMatrix*, const RwMatrix*);
			((MulM_t)0x7F18B0)(&boneWorld[b], &matArray[b], (RwMatrix*)&skinToBone[b]);
		} else {
			RwMatrix *m = &boneWorld[b];
			m->right.x=1;m->right.y=0;m->right.z=0;m->flags=0;
			m->up.x=0;m->up.y=1;m->up.z=0;
			m->at.x=0;m->at.y=0;m->at.z=1;
			m->pos.x=0;m->pos.y=0;m->pos.z=0;
		}
	}

	// Same RpGeometry layout for new geometry
	void *newVerts = *(void**)((uintptr_t)newGeo + 0x5C + 0x14); // morphTarget(+0x5C).verts(+0x14)
	void *newNorms = *(void**)((uintptr_t)newGeo + 0x5C + 0x18); // morphTarget.normals(+0x18)
	void *newTex   = *(void**)((uintptr_t)newGeo + 0x34); // texCoords[0] at +0x34
	Tri *newTrisArr = (Tri*)*(void**)((uintptr_t)newGeo + 0x2C); // triangles at +0x2C

	for (int v = 0; v < numVerts; v++) {
		if (!vertInSet[v]) continue;
		int nv = vertRemap[v];
		int boneIdx = indices[v*4] & 0xFF; if (boneIdx>=32) boneIdx=0;
		CVector *srcV = &((CVector*)verts)[v];
		CVector *dstV = &((CVector*)newVerts)[nv];
		typedef void (*XformPt_t)(CVector*,const CVector*,const RwMatrix*);
		((XformPt_t)0x7EDD60)(dstV, srcV, &boneWorld[boneIdx]);
		if (newNorms && norms) {
			CVector *srcN = &((CVector*)norms)[v];
			CVector *dstN = &((CVector*)newNorms)[nv];
			typedef void (*XformVec_t)(CVector*,const CVector*,const RwMatrix*);
			((XformVec_t)0x7EDDC0)(dstN, srcN, &boneWorld[boneIdx]);
			typedef float (*Norm_t)(CVector*,const CVector*);
			((Norm_t)0x7ED9B0)(dstN, dstN);
		}
		if (newTex && texCoords) ((CVector*)newTex)[nv] = ((CVector*)texCoords)[v];
	}
	for (int t = 0; t < numTris; t++) {
		if (triRemap[t] < 0) continue;
		int nt = triRemap[t];
		newTrisArr[nt].v[0] = vertRemap[srcTris[t].v[0]];
		newTrisArr[nt].v[1] = vertRemap[srcTris[t].v[1]];
		newTrisArr[nt].v[2] = vertRemap[srcTris[t].v[2]];
		newTrisArr[nt].mat = 0;
	}

	// V4 FIX: Clone material with proper refcounting.
	// RpGeometryCreate creates one default material at matList.materials[0].
	// RpMaterial layout (rpworld.h:415, RW 3.6 SDK, external/d3d9/rpworld.h:415-423):
	//   texture(RwTexture*) +0x00, color(RwRGBA) +0x04, pipeline(RxPipeline*) +0x08,
	//   surfaceProps(ambient,specular,diffuse=12 bytes) +0x0C, refCount(int16) +0x18, pad +0x1A
	// Size: 0x1C
	{
		void **newMats = *(void***)((uintptr_t)newGeo + 0x20); // newGeo.matList.materials
		if (newMats && materials && numMaterials > 0 && *(void**)materials) {
			// Source material pointer
			RpMaterial *srcMat = (RpMaterial*)materials[0];
			// Create a new material via game function
			RpMaterial *newMat = (RpMaterial*)RpMaterialCreate();
			if (newMat) {
				// Copy surface properties (3 floats at +0x0C)
				newMat->surfaceProps = srcMat->surfaceProps;
				// Copy color
				newMat->color = srcMat->color;
				// Copy texture and AddRef
				newMat->texture = srcMat->texture;
				if (newMat->texture) {
					RwTextureAddRef(newMat->texture);
				}
				// Assign to gib geometry matList[0]
				newMats[0] = (void*)newMat;
			}
		}
	}

	RpGeometryUnlock(newGeo);

	// Create atomic
	void *atomic = RpAtomicCreate();
	if (!atomic) { RpGeometryDestroy(newGeo); free(vertInSet); free(vertRemap); free(triRemap); return nullptr; }

	// Create frame
	void *frame = RwFrameCreate();
	if (!frame) { RpAtomicDestroy(atomic); RpGeometryDestroy(newGeo); free(vertInSet); free(vertRemap); free(triRemap); return nullptr; }

	// Create clump
	void *clump = RpClumpCreate();
	if (!clump) { RpAtomicDestroy(atomic); RpGeometryDestroy(newGeo); free(vertInSet); free(vertRemap); free(triRemap); return nullptr; }

	// Wire up: atomic->frame via _rwObjectHasFrameSetFrame
	typedef void (*ObjFrameSet_t)(void*, void*);
	((ObjFrameSet_t)0x804EF0)(atomic, frame);

	// V5 FIX: Use the game's RpAtomicSetGeometry (0x749D40) instead of raw
	// pointer write. This builds the D3D9 ResEntry properly so the atomic
	// actually renders. Flags=0 means replace geometry, no extra flags.
	RpAtomicSetGeometry(atomic, newGeo, 0);

	// GIB RENDER FIX: the atomic was never spliced into the clump, so
	// RpClumpForAllAtomics found zero atomics and gibs never rendered.
	// Share the frame with the clump too (canonical single-atomic-clump
	// pattern: clump frame == atomic frame).
	RpClumpAddAtomic(clump, atomic);
	((ObjFrameSet_t)0x804EF0)(clump, frame);

	// Set render callback to the default atomic renderer.
	// VERIFIED LAYOUT (2026-11-02, exe byte-evidence): renderCallBack is at
	// +0x10 — RpAtomicCreate (0x749C50) stores the default callback there,
	// and the field order object 0x00-0x0F / cb 0x10 / repEntry 0x14 /
	// geometry 0x18 / boundingSphere 0x1C / worldBoundingSphere 0x2C /
	// clump 0x3C / inClumpLink 0x40 only closes arithmetically with cb at
	// 0x10. The SDK struct member resolves to a different offset in this
	// game (the old +0x48 claim) — writing through it corrupted +0x48.
	// Write the verified offset directly.
	*(void**)((uintptr_t)atomic + 0x10) = (void*)AtomicDefaultRenderCallBack;

	free(vertInSet); free(vertRemap); free(triRemap);
	return clump;
}

// ============================================================
// Dismember_ProcessPed
// ============================================================
void Dismember_ProcessPed(void *pPed, void *clump, int nodeId,
	float damageX, float damageY, float damageZ,
	float damageIntensity, DismemberState *state)
{
	if (!g_dismemberEnable || !g_bulletWorld || !pPed || !clump || !state) return;
	if (nodeId < 1 || nodeId > 18) return;

	bool *flag = nullptr;
	switch (nodeId) {
		case 2: flag = &state->headRemoved; break;
		case 3: case 5: case 13: flag = &state->lArmRemoved; break;
		case 4: case 6: case 14: flag = &state->rArmRemoved; break;
		case 7: case 9: case 11: flag = &state->lLegRemoved; break;
		case 8: case 10: case 12: flag = &state->rLegRemoved; break;
	}
	if (flag && *flag) return;
	if (flag) *flag = true;

	int boneTag = NodeToBone(nodeId);

	float dx=damageX, dy=damageY, dz=damageZ;
	float len = sqrtf(dx*dx+dy*dy+dz*dz);
	// NaN/inf guard: `len < 0.001f` is false for NaN, so a poisoned damage
	// vector used to divide straight into a NaN impulse. `!(x in range)`
	// catches NaN, zero and inf -> deterministic hash fallback below.
	if (!(len > 0.001f && len < 1e30f)) {
		unsigned int h = (unsigned int)(uintptr_t)pPed;
		float a = (float)(h&0xFFFF)*0.00009587f;
		dx=cosf(a); dy=sinf(a); dz=0.3f;
		len = sqrtf(dx*dx+dy*dy+dz*dz);
	}
	dx/=len; dy/=len; dz/=len;
	dz += 0.35f;
	float blen = sqrtf(dx*dx+dy*dy+dz*dz);
	dx/=blen; dy/=blen; dz/=blen;

	float dm = (damageIntensity>0&&damageIntensity<1000) ? damageIntensity*0.1f : 1.0f;
	float imp = g_impulseScale * dm;

	void *hier = GetAnimHierarchyFromSkinClump(clump);
	RwMatrix *ma = hier ? RpHAnimHierarchyGetMatrixArray((RpHAnimHierarchy*)hier) : nullptr;
	int hidx = hier ? RpHAnimIDGetIndex(hier, boneTag) : -1;
	float px=0, py=0, pz=0;
	if (hidx>=0 && ma) { px=ma[hidx].pos.x; py=ma[hidx].pos.y; pz=ma[hidx].pos.z; }
	else { pz = *(float*)((uintptr_t)pPed + 0x0C); }

	void *gibClump = BuildGibClump(clump, boneTag);

	btVector3 bPos(px, py, pz);
	btVector3 bImp(dx*imp, dy*imp, dz*imp);

	btBoxShape *box = new btBoxShape(btVector3(0.10f, 0.10f, 0.08f));
	btVector3 inertia(0,0,0);
	box->calculateLocalInertia(0.5f, inertia);
	btTransform btStart; btStart.setIdentity(); btStart.setOrigin(bPos);
	btDefaultMotionState *ms = new btDefaultMotionState(btStart);
	btRigidBody::btRigidBodyConstructionInfo rbinfo(0.5f, ms, box, inertia);
	btRigidBody *body = new btRigidBody(rbinfo);
	body->setDamping(0.1f, 0.3f);
	body->applyImpulse(bImp, bPos);
	unsigned int h = (unsigned int)(uintptr_t)pPed;
	body->applyTorqueImpulse(btVector3(
		(float)(h&0xFF)*0.1f-5.0f, (float)((h>>8)&0xFF)*0.1f-5.0f, (float)((h>>16)&0xFF)*0.1f-5.0f)*3.0f);
	g_bulletWorld->addRigidBody(body, COLGROUP_RAGDOLL, COLGROUP_GROUND);

	GibInstance *gib = GibAlloc();
	if (gib) {
		gib->spawnTime = GetGameTime();
		gib->lifetime = g_gibLifetime;
		gib->body = body;
		gib->shape = box;
		gib->clump = gibClump;
		gib->frame = nullptr;
	} else {
		g_bulletWorld->removeRigidBody(body);
		delete ms; delete body; delete box;
		if (gibClump) RpClumpDestroy(gibClump);
	}

	static const char *names[] = {"","TORSO","HEAD","L_ARM","R_ARM","L_HAND","R_HAND","L_LEG","R_LEG",
		"L_FOOT","R_FOOT","L_LOWER_LEG","R_LOWER_LEG","L_LOWER_ARM","R_LOWER_ARM","L_CLAVICLE","R_CLAVICLE","NECK","JAW"};
	const char *nn = (nodeId>=1&&nodeId<=18) ? names[nodeId] : "?";
	dbglog("Dismember: %s(node=%d) bone=%d impulse=(%.1f,%.1f,%.1f) mesh=%s",
		nn, nodeId, boneTag, bImp.x(), bImp.y(), bImp.z(), gibClump?"baked":"box");
}

// ============================================================
// Dismember_ApplyZeroScale
// ============================================================
void Dismember_ApplyZeroScale(void *clump, DismemberState *state) {
	if (!clump || !state) return;
	if (!state->headRemoved && !state->lArmRemoved && !state->rArmRemoved &&
		!state->lLegRemoved && !state->rLegRemoved) return;

	void *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier) return;
	RwMatrix *ma = RpHAnimHierarchyGetMatrixArray((RpHAnimHierarchy*)hier);
	if (!ma) return;

	struct ZB { int tag; bool *flag; };
	ZB list[] = {
		{5, &state->headRemoved},
		{32, &state->lArmRemoved}, {33, &state->lArmRemoved},
		{22, &state->rArmRemoved}, {23, &state->rArmRemoved},
		{31, &state->lLegRemoved}, {35, &state->lLegRemoved},
		{21, &state->rLegRemoved}, {25, &state->rLegRemoved},
	};
	for (int i = 0; i < 9; i++) {
		if (!*list[i].flag) continue;
		int idx = RpHAnimIDGetIndex(hier, list[i].tag);
		if (idx < 0) continue;
		ma[idx].right.x = 0; ma[idx].right.y = 0; ma[idx].right.z = 0;
		ma[idx].up.x = 0;    ma[idx].up.y = 0;    ma[idx].up.z = 0;
		ma[idx].at.x = 0;    ma[idx].at.y = 0;    ma[idx].at.z = 0;
	}
}

// ============================================================
// Dismember_Update — step gibs lifetime
// ============================================================
void Dismember_Update(void) {
	if (!g_dismemberEnable || !g_bulletWorld) return;
	float now = GetGameTime();
	for (int i = 0; i < DISMEMBER_MAX_GIBS; i++) {
		GibInstance *g = &g_dismemberGibs[i];
		if (!g->active) continue;
		if ((now - g->spawnTime) > g->lifetime) { GibFree(g); continue; }
	}
}

// ============================================================
// Dismember_Render — render all active gibs
// V6 FIX: Write to the modelling matrix (RwFrame+0x14 is the
// modelling matrix pointer, NOT the LTM). After writing, call
// RwFrameUpdateObjects which recomputes the LTM for rendering.
// Also wrap in ImmediateModeRenderStatesStore/Set/ReStore to
// avoid polluting the game's render states.
// ============================================================
void Dismember_Render(void) {
	if (!g_dismemberEnable) return;

	IMM_STORE();
	IMM_SET();

	for (int i = 0; i < DISMEMBER_MAX_GIBS; i++) {
		GibInstance *g = &g_dismemberGibs[i];
		if (!g->active || !g->clump || !g->body) continue;

		btTransform trans;
		g->body->getMotionState()->getWorldTransform(trans);

		const btMatrix3x3 &basis = trans.getBasis();
		const btVector3 &origin = trans.getOrigin();

		// Get atomic from clump
		void *firstAtomic = nullptr;
		typedef void *(*AtomicCB)(void*, void*);
		typedef void *(*ForAllAtomics_t)(void*, AtomicCB, void*);
		auto cbRend = [](void *a, void *d) -> void* { if (!*(void**)d) *(void**)d = a; return a; };
		((ForAllAtomics_t)0x749B70)(g->clump, cbRend, &firstAtomic);
		if (!firstAtomic) continue;

		// Get frame from atomic: _rwObjectGetFrame at +0x08
		void *frame = *(void**)((uintptr_t)firstAtomic + 0x08);
		if (!frame) continue;

		// RwFrame+0x14 = pointer to the MODELLING matrix (NOT LTM).
		// Writing to the modelling matrix and then calling
		// RwFrameUpdateObjects will correctly recompute the LTM.
		// Verified: RwFrameGetMatrix/GetLTM at 0x7F0990 reads from
		// frame+0x18 (the actual LTM cache slot).
		RwMatrix *modMat = *(RwMatrix**)((uintptr_t)frame + 0x14);
		if (!modMat) continue;

		// Write Bullet body transform as the modelling matrix
		modMat->right.x = basis[0][0]; modMat->right.y = basis[1][0]; modMat->right.z = basis[2][0];
		modMat->up.x    = basis[0][1]; modMat->up.y    = basis[1][1]; modMat->up.z    = basis[2][1];
		modMat->at.x    = basis[0][2]; modMat->at.y    = basis[1][2]; modMat->at.z    = basis[2][2];
		modMat->pos.x   = origin.x();  modMat->pos.y   = origin.y();  modMat->pos.z   = origin.z();
		modMat->flags   = 0;

		// Update frame hierarchy: recomputes LTMs from modelling matrices
		RwFrameUpdateObjects(frame);

		// Render the atomic via default render callback
		typedef void *(*RenderCB_t)(void*);
		((RenderCB_t)0x7491C0)(firstAtomic);
	}

	IMM_RESTORE();
}

// ============================================================
// Blood spurts
// ============================================================
void Dismember_SpurtBlood(void *clump, DismemberState *state) {
	if (!g_dismemberEnable || !clump || !state) return;
	if (!state->headRemoved && !state->lArmRemoved && !state->rArmRemoved &&
		!state->lLegRemoved && !state->rLegRemoved) return;

	float now = GetGameTime();
	if ((now - state->lastBloodSpurt) < g_bloodInterval * 1000.0f) return;
	state->lastBloodSpurt = now;

	void *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier) return;
	RwMatrix *ma = RpHAnimHierarchyGetMatrixArray((RpHAnimHierarchy*)hier);
	if (!ma) return;

	int bleedBone = 0;
	if (state->headRemoved) bleedBone = 5;
	else if (state->lArmRemoved) bleedBone = 32;
	else if (state->rArmRemoved) bleedBone = 22;
	else if (state->lLegRemoved) bleedBone = 31;
	else if (state->rLegRemoved) bleedBone = 21;
	if (!bleedBone) return;

	int idx = RpHAnimIDGetIndex(hier, bleedBone);
	if (idx < 0) return;

	typedef void (*AddParticle_t)(int, void*, void*, void*, float, void*, void*);
	AddParticle_t addPart = (AddParticle_t)0x4A9B50;
	if (!addPart) return;

	CVector pos(ma[idx].pos.x, ma[idx].pos.y, ma[idx].pos.z);
	CVector vel(0, 0, -0.3f);
	__try { addPart(45, &pos, &vel, nullptr, 1.0f, nullptr, nullptr); }
	__except(EXCEPTION_EXECUTE_HANDLER) {}
}
