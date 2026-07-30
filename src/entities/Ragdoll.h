#pragma once

#include "../skygfx.h"
#include "../rw/rtquat.h"
#include "../rw/rtslerp.h"

// ---- Animation / Bone types (RW SDK baked into game exe) ----

#define rpFRAMEROTCAPTURED    0x100
#define rpFRAMETRANSCAPTURED  0x200

struct RpHAnimKeyFrame;

struct AnimBlendFrameData;
struct CAnimBlendLink { void *next, *prev; };
class CAnimBlendClumpData {
public:
	CAnimBlendLink m_AnimList;
	uint32 m_NumFrameData;
	CVector *m_PedPosition;
	AnimBlendFrameData *m_FrameDatas;
};
static_assert(sizeof(CAnimBlendClumpData) == 0x14, "Wrong size: CAnimBlendClumpData");

struct AnimBlendFrameData {
	RpHAnimKeyFrame *KeyFrame;
	uint32 Flags;
};
static_assert(sizeof(AnimBlendFrameData) == 0x8, "Wrong size: AnimBlendFrameData");

inline CAnimBlendClumpData *ANIMBLENDCLUMPFROMCLUMP(void *clump) {
	return *(CAnimBlendClumpData**)((uint32)clump + 0x5C);
}

struct tBoneInfo {
	int32 m_current;
	int32 m_prev;
	CVector m_Max;
	CVector m_Min;
	CVector m_ABC;
};

#define MAX_BONE_NUM 32
#define RAGDOLL_MAX_BONES 32
#define MAX_RAGDOLLS 24
#define RAGDOLL_DIST_LOW  15.0f
#define RAGDOLL_DIST_HIGH 60.0f

// ---- Ragdoll Bone ----

struct RagdollBone {
	int32 boneTag;
	RagdollBone *parent;
	RagdollBone *children[8];
	int numChildren;

	RwMatrix worldMat;
	CVector pos;
	RtQuat orientation;
	RtQuat animQuat;

	RwV3d vel;

	CVector limitMin;
	CVector limitMax;

	RagdollBone() : boneTag(0), parent(nullptr), numChildren(0) {
		memset(&worldMat, 0, sizeof(RwMatrix));
		pos.x = pos.y = pos.z = 0.0f;
		memset(&orientation, 0, sizeof(RtQuat));
		memset(&animQuat, 0, sizeof(RtQuat));
		vel.x = vel.y = vel.z = 0.0f;
		limitMin.x = limitMin.y = limitMin.z = -45.0f;
		limitMax.x = limitMax.y = limitMax.z = 45.0f;
	}

	void Init(int32 tag, RpHAnimKeyFrame *keyFrame, const tBoneInfo *boneInfo);
	void CalcWldMat(const RwMatrix *parentMat);
	void BlendKeyframe(float blend);
	void Limit();
};

class CPed;

class Ragdoll_c {
public:
	Ragdoll_c();
	~Ragdoll_c();

	bool Init(CPed *ped);
	void Exit();
	void Update(float deltaTime);
	void SetBlend(float blend);
	void ApplyForce(int32 boneTag, float force, RwV3d dir);
	void ProcessMotion(float deltaTime, CVector &pedPos, CVector &pedVel);

	bool IsActive() const { return m_pPed != nullptr; }
	CPed *GetPed() const { return m_pPed; }

private:
	void ResolveSystem(float deltaTime);
	void SetupBoneHierarchy();

	CPed *m_pPed;
	RagdollBone m_bones[RAGDOLL_MAX_BONES];
	RagdollBone *m_pRootBone;
	float m_blend;

	CVector m_prevPos;
	CVector m_velocity;
	CVector m_acceleration;
};

class RagdollManager_c {
public:
	RagdollManager_c();
	~RagdollManager_c();

	bool Init();
	void Exit();
	void Reset();
	void Update(float deltaTime);

	// Visibility-based pool management: scans ped pool, assigns ragdolls by distance/LOD
	void ProcessAllPeds(float deltaTime);

	Ragdoll_c *AddRagdoll(CPed *ped);
	void RemoveRagdoll(Ragdoll_c *ragdoll);

private:
	Ragdoll_c m_ragdolls[MAX_RAGDOLLS];

	// Find existing ragdoll for a ped (returns nullptr if not assigned)
	Ragdoll_c *FindForPed(CPed *ped);
};

extern RagdollManager_c g_ragdollMan;
extern int32 aBONETAG_ENUM_TAB[RAGDOLL_MAX_BONES];
