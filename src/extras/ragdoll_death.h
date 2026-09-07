#pragma once
#include "../skygfx.h"

// ============================================================
// Death ragdoll system — Bullet Physics ragdoll solver.
// Replaces the previous verlet solver with Bullet 2.87 dynamics.
// ============================================================

// Forward declare Bullet types (incomplete types ok in header)
class btDiscreteDynamicsWorld;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDefaultCollisionConfiguration;
class btRigidBody;
class btCollisionShape;
class btTypedConstraint;

#define RAGDOLL_DEATH_POOL_SIZE  16   // Only dead peds
#define RAGDOLL_DEATH_BONES  12   // PELVIS..CALF (SA skeleton mapping)

// Bone tags — SA v1.0 values verified against R* source
#define RBONE_PELVIS     1
#define RBONE_SPINE      2
#define RBONE_SPINE1     3
#define RBONE_HEAD       5
#define RBONE_L_UPPERARM 32
#define RBONE_L_FOREARM  33
#define RBONE_R_UPPERARM 22
#define RBONE_R_FOREARM  23
#define RBONE_L_THIGH    31
#define RBONE_L_CALF     35
#define RBONE_R_THIGH    21
#define RBONE_R_CALF     25

// Bullet body part indices (11 bodies matching Bullet demo)
#define BULLET_BODYPART_PELVIS       0
#define BULLET_BODYPART_SPINE        1
#define BULLET_BODYPART_HEAD         2
#define BULLET_BODYPART_L_UPPER_LEG  3
#define BULLET_BODYPART_L_LOWER_LEG  4
#define BULLET_BODYPART_R_UPPER_LEG  5
#define BULLET_BODYPART_R_LOWER_LEG  6
#define BULLET_BODYPART_L_UPPER_ARM  7
#define BULLET_BODYPART_L_LOWER_ARM  8
#define BULLET_BODYPART_R_UPPER_ARM  9
#define BULLET_BODYPART_R_LOWER_ARM  10
#define BULLET_BODYPART_COUNT        11

#define BULLET_JOINT_COUNT  10
#define BULLET_JOINT_PELVIS_SPINE  0
#define BULLET_JOINT_SPINE_HEAD    1
#define BULLET_JOINT_L_HIP         2
#define BULLET_JOINT_L_KNEE        3
#define BULLET_JOINT_R_HIP         4
#define BULLET_JOINT_R_KNEE        5
#define BULLET_JOINT_L_SHOULDER    6
#define BULLET_JOINT_L_ELBOW       7
#define BULLET_JOINT_R_SHOULDER    8
#define BULLET_JOINT_R_ELBOW       9

// Collision groups
#define COLGROUP_RAGDOLL  (1 << 0)
#define COLGROUP_GROUND   (1 << 1)

extern const int32 g_ragdollDeathBoneTags[RAGDOLL_DEATH_BONES];
extern const int32 g_ragdollDeathParentIdx[RAGDOLL_DEATH_BONES];
extern const float g_ragdollDeathAngleLimits[RAGDOLL_DEATH_BONES];

// Mapping from SA bone index (0-11) to bullet body part index (0-10) or -1
extern const int32 g_boneToBulletMap[RAGDOLL_DEATH_BONES];

enum RagdollDeathState {
	RAGDOLL_DEATH_INACTIVE = 0,
	RAGDOLL_DEATH_ACTIVE,
	RAGDOLL_DEATH_SETTLING,
	RAGDOLL_DEATH_DONE
};

struct RagdollDeathInstance {
	RagdollDeathState state;
	void *pPed;
	void *pClump; // RpClump* of the ped (for hierarchy access)
	int settleFrames;
	float settleTimer;
	float groundZ;
	int boneIndices[RAGDOLL_DEATH_BONES]; // RpHAnimIDGetIndex results (-1 invalid)

	// Bullet physics per-instance objects
	btRigidBody *bodies[BULLET_BODYPART_COUNT];
	btCollisionShape *shapes[BULLET_BODYPART_COUNT];
	btTypedConstraint *joints[BULLET_JOINT_COUNT];
	btRigidBody *groundBody;
	btCollisionShape *groundShape;

	// Dismemberment state (opaque pointer, defined in dismember.h)
	void *dismemberState;
	int processedBodypart; // last m_nBodypartToRemove value we processed

	// Captured bone transforms at activation (world space)
	// Store as raw floats to avoid Bullet header dependency in .h
	float boneWorldPos[RAGDOLL_DEATH_BONES][3];
	float boneWorldRot[RAGDOLL_DEATH_BONES][4]; // quaternion (x,y,z,w)

	RagdollDeathInstance() : state(RAGDOLL_DEATH_INACTIVE), pPed(nullptr), pClump(nullptr),
		settleFrames(0), settleTimer(0.0f), groundZ(0.0f),
		groundBody(nullptr), groundShape(nullptr),
		dismemberState(nullptr), processedBodypart(0) {
		for (int i = 0; i < RAGDOLL_DEATH_BONES; i++) {
			boneIndices[i] = -1;
			boneWorldPos[i][0] = boneWorldPos[i][1] = boneWorldPos[i][2] = 0.0f;
			boneWorldRot[i][0] = boneWorldRot[i][1] = boneWorldRot[i][2] = 0.0f;
			boneWorldRot[i][3] = 1.0f;
		}
		for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
			bodies[i] = nullptr;
			shapes[i] = nullptr;
		}
		for (int i = 0; i < BULLET_JOINT_COUNT; i++) {
			joints[i] = nullptr;
		}
	}

	// Check if all Bullet bodies are deactivated (settled)
	bool AllBodiesSleeping() const;
};

// ---- API ----
void Ragdoll_Init(void);

// V7: Split into Simulate (physics+death polling, frame-gated) and
//     ApplyPose (hierarchy write-back, called before rendering).
void Ragdoll_Simulate(void);   // physics step, death polling, gib lifetime (frame-gated)
void Ragdoll_ApplyPose(void);  // hierarchy write-back, zero-scale, blood spurts
// Legacy wrapper: Ragdoll_Update still calls both (backward compat)
inline void Ragdoll_Update(void) { Ragdoll_Simulate(); Ragdoll_ApplyPose(); }

bool Ragdoll_Activate(void *pPed);
void Ragdoll_Deactivate(void *pPed);
void Ragdoll_Shutdown(void);

extern bool g_ragdollDeathEnable;
extern RagdollDeathInstance g_ragdollDeathPool[RAGDOLL_DEATH_POOL_SIZE];
extern class btDiscreteDynamicsWorld *g_bulletWorld;