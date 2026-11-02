// sftbdyphsx.h - consolidated public API for src/entities/sftbdyphsx.cpp.
//
// Merged 2026-10-26 from TWO former headers:
//   - src/entities/Ragdoll.h        (living-ped secondary-motion ragdoll types)
//   - src/extras/dismember.h        (Bullet gib dismemberment state + API)
//
// Section order (ragdoll -> dismemberment) matches the .cpp. Declarations are
// verbatim; only #pragma once and the include block were deduped, with section
// banners added. ragdoll_death.{h,cpp} stay separate and include this header
// for DismemberState / the Dismember_* API.

#pragma once

#include "../skygfx.h"
#include "../rw/rtquat.h"
#include "../rw/rtslerp.h"

// ---- RAGDOLL (was src/entities/Ragdoll.h) ----

// ---- Animation / Bone types (RW SDK baked into game exe) ----

// ---- AnimBlendFrameData: SHIPPED layout (fixed 2026-10-29, plan G1) ----
// Authority: gta-reversed-master\source\game_sa\Animation\AnimBlendFrameData.h:12-40
// (VALIDATE_SIZE 0x18). The2003-leak prototype's rpFRAMEROTCAPTURED 0x100 /
// rpFRAMETRANSCAPTURED 0x200 constants do NOT exist in the shipped struct — the
// capture bits are bitfield POSITIONS 1 and 2 of the Flags byte (values 0x02 /
// 0x04). Our pre-fix redeclaration was {RpHAnimKeyFrame*; uint32} = 0x8 with
// capture at 0x100/0x200 — silent layout drift (static_assert passed against our
// own wrong size). Nothing in the tree indexed m_FrameDatas before this fix, so
// no behavior changed; any future capture code MUST use the bits below.
#define rpFRAMEROTCAPTURED    0x02	// bit1 KeyFramesIgnoreNodeOrientation
#define rpFRAMETRANSCAPTURED  0x04	// bit2 KeyFramesIgnoreNodeTranslation

struct RpHAnimKeyFrame;

// Shipped interp frame the anim system writes and BlendKeyframe slerps into:
// gta-reversed RpHAnimBlendInterpFrame.h (VALIDATE_SIZE 0x1C) — { RtQuat q; CVector t; }.
// NOTE: distinct from the RW SDK's RpHAnimInterpFrame (rphanim.h:132-140, which
// carries keyFrame1/keyFrame2 pointers too).
struct RpHAnimBlendInterpFrame {
	RtQuat q;
	CVector t;
};
static_assert(sizeof(RpHAnimBlendInterpFrame) == 0x1C, "Wrong size: RpHAnimBlendInterpFrame");

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

// Field-for-field port of the shipped class (bitfield union + BonePos/FramePos +
// KeyFrame/Frame unions + BoneTag). Shipped offsets: Flags 0x0, BonePos 0x4,
// KeyFrame 0x10, BoneTag 0x14. KeyFrame is RpHAnimBlendInterpFrame* (NOT the
// SDK RpHAnimKeyFrame* we wrongly used pre-fix).
struct AnimBlendFrameData {
	union {
		struct {
			bool bf1 : 1;								//!< unused
			bool KeyFramesIgnoreNodeOrientation : 1;	//!< capture ROT (bit 1)
			bool KeyFramesIgnoreNodeTranslation : 1;	//!< capture TRANS (bit 2)
			bool HasVelocity : 1;						//!< root-frame translation drives ped movement (bit 3)
			bool HasZVelocity : 1;						//!< 3D vs 2D velocity extraction (bit 4)
			bool NeedsKeyFrameUpdate : 1;				//!< defer RpAnimBlendNodeUpdateKeyFrames (bit 5)
			bool IsCompressed : 1;						//!< compressed anim data (bit 6)
			bool IsUpdatingFrame : 1;					//!< unused (bit 7)
		};
		uint8 Flags;
	};
	union {
		CVector BonePos;	//!< skinned clumps
		CVector FramePos;	//!< non-skinned clumps
	};
	union {
		RpHAnimBlendInterpFrame *KeyFrame;	//!< skinned clumps — physics write-back target
		RwFrame *Frame;						//!< non-skinned clumps
	};
	uint32 BoneTag;	//!< -1 (BONE_UNKNOWN) = non-skinned clump
};
static_assert(sizeof(AnimBlendFrameData) == 0x18, "Wrong size: AnimBlendFrameData (shipped 0x18, AnimBlendFrameData.h VALIDATE_SIZE)");

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

// ---- DISMEMBERMENT (was src/extras/dismember.h) ----

// ============================================================
// Dismemberment system
// Severs limbs from dead peds, spawns physics gibs (Bullet),
// zero-scales stump bones, emits blood spurts.
// ============================================================

class btRigidBody;
class btCollisionShape;

#define DISMEMBER_MAX_GIBS 32
#define DISMEMBER_DEFAULT_LIFETIME 45.0f

// Gib in the world
struct GibInstance {
	bool active;
	float spawnTime;
	float lifetime;
	void *clump;  // RpClump*
	void *frame;  // RwFrame* (root frame of clump)
	btRigidBody *body;
	btCollisionShape *shape;
};

// Per-ragdoll-instance dismemberment state
struct DismemberState {
	bool headRemoved;
	bool lArmRemoved;
	bool rArmRemoved;
	bool lLegRemoved;
	bool rLegRemoved;
	float lastBloodSpurt;

	DismemberState()
		: headRemoved(false), lArmRemoved(false), rArmRemoved(false),
		  lLegRemoved(false), rLegRemoved(false), lastBloodSpurt(0.0f) {}
};

// ---- API ----
void Dismember_Init(void);
void Dismember_Shutdown(void);
void Dismember_Update(void);
void Dismember_Render(void);
// Called from death poll when m_nBodypartToRemove != 0
void Dismember_ProcessPed(void *pPed, void *clump, int nodeId,
	float damageX, float damageY, float damageZ,
	float damageIntensity, DismemberState *state);
// Called per frame from hierarchy write-back to zero severed bones
void Dismember_ApplyZeroScale(void *clump, DismemberState *state);
// Called per frame to emit blood spurts from stumps
void Dismember_SpurtBlood(void *clump, DismemberState *state);

extern bool g_dismemberEnable;
