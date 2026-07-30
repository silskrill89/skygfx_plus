#include "../skygfx.h"
#include "Ragdoll.h"
#include "../rw/rphanim.h"

#include <cmath>

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
	float recipSinOmega = (sinOmega > 1e-6f) ? (1.0f / sinOmega) : 0.0f;
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
	RtQuatUnitConvertToMatrixMacro(&orientation, &worldMat);
	// Set position
	worldMat.pos.x = pos.x;
	worldMat.pos.y = pos.y;
	worldMat.pos.z = pos.z;
	rwMatrixSetFlags(&worldMat, rwMATRIXTYPEORTHONORMAL);

	// worldMat = localMat * parentMat
	RwMatrixMultiply(&worldMat, &worldMat, parentMat);

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
	void *rwObject = *(void**)((uint32)m_pPed + 0x18);
	if (!rwObject) return false;
	RpClump *clump = (RpClump*)rwObject;
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier) return false;
	if (hier->numNodes < 1) return false;

	// Zero out bone velocities
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		m_bones[i].boneTag = aBONETAG_ENUM_TAB[i];
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
