#include "ragdoll_death.h"
#include "dismember.h"
#include "../rw/gta.h"
#include "../rw/rphanim.h"
#include "../rw/rtquat.h"
#include "../rw/rtslerp.h"
// Bullet Physics headers
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <LinearMath/btDefaultMotionState.h>
#include <LinearMath/btTransform.h>
#include <LinearMath/btQuaternion.h>
#include <LinearMath/btMatrix3x3.h>
#include <LinearMath/btVector3.h>
#include <windows.h>
#include <stdio.h>

// ============================================================
// Forward declarations
// ============================================================
// ---- RW SDK function calls via direct game addresses (SA 1.0 US) ----
// Avoids linkage conflicts with extern "C" WRAPPER definitions in gta.cpp.
// Functions are defined with __declspec(naked) + extern "C" in gta.cpp,
// which conflicts with normal C++ extern declarations. Using typedef + 
// explicit addresses bypasses this cleanly.
typedef void *(*GetAnimHierarchyFromSkinClump_t)(void *clump);
#define GetAnimHierarchyFromSkinClump(clump)  ((GetAnimHierarchyFromSkinClump_t)0x734A40)(clump)
typedef int (*RpHAnimIDGetIndex_t)(void *hierarchy, int ID);
#define RpHAnimIDGetIndex(h, id)  ((RpHAnimIDGetIndex_t)0x7C51A0)(h, id)
typedef void *(*RpHAnimHierarchyGetMatrixArray_t)(void *hierarchy);
#define RpHAnimHierarchyGetMatrixArray(h)  ((RpHAnimHierarchyGetMatrixArray_t)0x7C5120)(h)

// Local CVector matching GTA SA memory layout (3 floats)
// Not needed, CVector is already defined in src/rw/gta.h included via skygfx.h
typedef void *(*GetAnimHierarchyFromSkinClump_t)(void *clump);

// ============================================================
// Globals
// ============================================================
bool g_ragdollDeathEnable = true;
RagdollDeathInstance g_ragdollDeathPool[RAGDOLL_DEATH_POOL_SIZE];

// Shared Bullet world (singleton) — non-static for dismember.cpp access
btDiscreteDynamicsWorld *g_bulletWorld = nullptr;
static btDefaultCollisionConfiguration *g_collisionConfig = nullptr;
static btCollisionDispatcher *g_dispatcher = nullptr;
static btDbvtBroadphase *g_broadphase = nullptr;
static btSequentialImpulseConstraintSolver *g_solver = nullptr;

// ============================================================
// INI configuration (loaded from ragdoll.ini via Win32 API)
// Follows the wheels_extender / normalmap_byDK INI pattern.
// ============================================================

// Per-body mass config, loaded from [BODY_xxx] sections
static float g_bodyMasses[BULLET_BODYPART_COUNT] = {
	2.0f,  // PELVIS
	1.5f,  // SPINE
	1.0f,  // HEAD
	1.0f,  // L_UPPER_LEG
	0.8f,  // L_LOWER_LEG
	1.0f,  // R_UPPER_LEG
	0.8f,  // R_LOWER_LEG
	1.0f,  // L_UPPER_ARM
	0.8f,  // L_LOWER_ARM
	1.0f,  // R_UPPER_ARM
	0.8f,  // R_LOWER_ARM
};

// Per-joint config, loaded from [JOINT_xxx] sections
struct JointConfig {
	float swingSpan1;   // degrees, for ConeTwist
	float swingSpan2;
	float twistSpan;
	float hingeLow;     // degrees, for Hinge
	float hingeHigh;
};
static JointConfig g_jointConfigs[BULLET_JOINT_COUNT];

// Hit reaction config (loaded from [HITREACTION] section)
static float g_hitImpulseScale   = 8.0f;
static float g_hitUpBias         = 0.35f;
static float g_hitAngScale       = 0.5f;
static float g_hitSpineFraction  = 0.3f;
static float g_hitMassSqrtFactor = 1.0f; // sqrt-mass multiplier modifier

// Default joint configs
static void InitDefaultJointConfigs() {
	// Pelvis-Spine: near rigid (tiny cone)
	g_jointConfigs[BULLET_JOINT_PELVIS_SPINE].swingSpan1 = 5.0f;
	g_jointConfigs[BULLET_JOINT_PELVIS_SPINE].swingSpan2 = 5.0f;
	g_jointConfigs[BULLET_JOINT_PELVIS_SPINE].twistSpan = 5.0f;

	// Spine-Head: near rigid
	g_jointConfigs[BULLET_JOINT_SPINE_HEAD].swingSpan1 = 10.0f;
	g_jointConfigs[BULLET_JOINT_SPINE_HEAD].swingSpan2 = 10.0f;
	g_jointConfigs[BULLET_JOINT_SPINE_HEAD].twistSpan = 10.0f;

	// Left Hip: ConeTwist
	g_jointConfigs[BULLET_JOINT_L_HIP].swingSpan1 = 45.0f;
	g_jointConfigs[BULLET_JOINT_L_HIP].swingSpan2 = 45.0f;
	g_jointConfigs[BULLET_JOINT_L_HIP].twistSpan = 30.0f;

	// Left Knee: Hinge
	g_jointConfigs[BULLET_JOINT_L_KNEE].hingeLow = 0.0f;
	g_jointConfigs[BULLET_JOINT_L_KNEE].hingeHigh = 130.0f;

	// Right Hip: ConeTwist
	g_jointConfigs[BULLET_JOINT_R_HIP].swingSpan1 = 45.0f;
	g_jointConfigs[BULLET_JOINT_R_HIP].swingSpan2 = 45.0f;
	g_jointConfigs[BULLET_JOINT_R_HIP].twistSpan = 30.0f;

	// Right Knee: Hinge
	g_jointConfigs[BULLET_JOINT_R_KNEE].hingeLow = 0.0f;
	g_jointConfigs[BULLET_JOINT_R_KNEE].hingeHigh = 130.0f;

	// Left Shoulder: ConeTwist
	g_jointConfigs[BULLET_JOINT_L_SHOULDER].swingSpan1 = 90.0f;
	g_jointConfigs[BULLET_JOINT_L_SHOULDER].swingSpan2 = 45.0f;
	g_jointConfigs[BULLET_JOINT_L_SHOULDER].twistSpan = 45.0f;

	// Left Elbow: Hinge
	g_jointConfigs[BULLET_JOINT_L_ELBOW].hingeLow = 0.0f;
	g_jointConfigs[BULLET_JOINT_L_ELBOW].hingeHigh = 150.0f;

	// Right Shoulder: ConeTwist
	g_jointConfigs[BULLET_JOINT_R_SHOULDER].swingSpan1 = 90.0f;
	g_jointConfigs[BULLET_JOINT_R_SHOULDER].swingSpan2 = 45.0f;
	g_jointConfigs[BULLET_JOINT_R_SHOULDER].twistSpan = 45.0f;

	// Right Elbow: Hinge
	g_jointConfigs[BULLET_JOINT_R_ELBOW].hingeLow = 0.0f;
	g_jointConfigs[BULLET_JOINT_R_ELBOW].hingeHigh = 150.0f;
}

// Helper: read float from INI via Win32 API
static float ReadIniFloat(const char *section, const char *key, float defaultVal) {
	char buf[64];
	DWORD ret = GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), ".\\ragdoll.ini");
	if (ret > 0 && buf[0] != '\0') {
		return (float)atof(buf);
	}
	return defaultVal;
}

// Helper: read int from INI
static int ReadIniInt(const char *section, const char *key, int defaultVal) {
	return GetPrivateProfileIntA(section, key, defaultVal, ".\\ragdoll.ini");
}

// Load joint config from INI
static void LoadJointIni(const char *section, int jointIdx) {
	const char *type = "coneTwist"; // default
	char buf[32];
	DWORD ret = GetPrivateProfileStringA(section, "type", "", buf, sizeof(buf), ".\\ragdoll.ini");
	if (ret > 0 && buf[0] != '\0') {
		type = buf; // pointer to static buf is OK here since we read immediately
	}

	// Always load both sets; the unused one is ignored
	char typeBuf[32];
	DWORD ret2 = GetPrivateProfileStringA(section, "type", "coneTwist", typeBuf, sizeof(typeBuf), ".\\ragdoll.ini");
	(void)ret2;

	g_jointConfigs[jointIdx].swingSpan1 = ReadIniFloat(section, "swingSpan1", g_jointConfigs[jointIdx].swingSpan1);
	g_jointConfigs[jointIdx].swingSpan2 = ReadIniFloat(section, "swingSpan2", g_jointConfigs[jointIdx].swingSpan2);
	g_jointConfigs[jointIdx].twistSpan  = ReadIniFloat(section, "twistSpan",  g_jointConfigs[jointIdx].twistSpan);
	g_jointConfigs[jointIdx].hingeLow   = ReadIniFloat(section, "lowLimit",   g_jointConfigs[jointIdx].hingeLow);
	g_jointConfigs[jointIdx].hingeHigh  = ReadIniFloat(section, "highLimit",  g_jointConfigs[jointIdx].hingeHigh);
}

static void LoadRagdollConfig() {
	InitDefaultJointConfigs();

	// Body masses
	const char *bodySections[] = {
		"BODY_PELVIS", "BODY_SPINE", "BODY_HEAD",
		"BODY_L_UPPER_LEG", "BODY_L_LOWER_LEG",
		"BODY_R_UPPER_LEG", "BODY_R_LOWER_LEG",
		"BODY_L_UPPER_ARM", "BODY_L_LOWER_ARM",
		"BODY_R_UPPER_ARM", "BODY_R_LOWER_ARM"
	};
	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		g_bodyMasses[i] = ReadIniFloat(bodySections[i], "mass", g_bodyMasses[i]);
	}

	// Joint limits
	LoadJointIni("JOINT_PELVIS_SPINE", BULLET_JOINT_PELVIS_SPINE);
	LoadJointIni("JOINT_SPINE_HEAD",   BULLET_JOINT_SPINE_HEAD);
	LoadJointIni("JOINT_L_HIP",        BULLET_JOINT_L_HIP);
	LoadJointIni("JOINT_L_KNEE",       BULLET_JOINT_L_KNEE);
	LoadJointIni("JOINT_R_HIP",        BULLET_JOINT_R_HIP);
	LoadJointIni("JOINT_R_KNEE",       BULLET_JOINT_R_KNEE);
	LoadJointIni("JOINT_L_SHOULDER",   BULLET_JOINT_L_SHOULDER);
	LoadJointIni("JOINT_L_ELBOW",      BULLET_JOINT_L_ELBOW);
	LoadJointIni("JOINT_R_SHOULDER",   BULLET_JOINT_R_SHOULDER);
	LoadJointIni("JOINT_R_ELBOW",      BULLET_JOINT_R_ELBOW);

	// Hit reaction config
	g_hitImpulseScale  = ReadIniFloat("HITREACTION", "impulseScale", g_hitImpulseScale);
	g_hitUpBias        = ReadIniFloat("HITREACTION", "upBias",       g_hitUpBias);
	g_hitAngScale      = ReadIniFloat("HITREACTION", "angScale",     g_hitAngScale);
	g_hitSpineFraction = ReadIniFloat("HITREACTION", "spineFraction", g_hitSpineFraction);

	dbglog("Ragdoll: config loaded from ragdoll.ini");
}

// ============================================================
// Static data tables
// ============================================================

const int32 g_ragdollDeathBoneTags[RAGDOLL_DEATH_BONES] = {
	RBONE_PELVIS,
	RBONE_SPINE,
	RBONE_SPINE1,
	RBONE_HEAD,
	RBONE_L_THIGH,
	RBONE_L_CALF,
	RBONE_R_THIGH,
	RBONE_R_CALF,
	RBONE_L_UPPERARM,
	RBONE_L_FOREARM,
	RBONE_R_UPPERARM,
	RBONE_R_FOREARM,
};

// Parent indices in the bone array (-1 = root)
const int32 g_ragdollDeathParentIdx[RAGDOLL_DEATH_BONES] = {
	-1,  // PELVIS (root)
	 0,  // SPINE → PELVIS
	 1,  // SPINE1 → SPINE
	 2,  // HEAD → SPINE1
	 0,  // L_THIGH → PELVIS
	 4,  // L_CALF → L_THIGH
	 0,  // R_THIGH → PELVIS
	 6,  // R_CALF → R_THIGH
	 1,  // L_UPPERARM → SPINE
	 8,  // L_FOREARM → L_UPPERARM
	 1,  // R_UPPERARM → SPINE
	 10, // R_FOREARM → R_UPPERARM
};

// Angle limits (degrees) — used for existing verlet fallback if kept
const float g_ragdollDeathAngleLimits[RAGDOLL_DEATH_BONES] = {
	0.0f,    // PELVIS (root, no limit)
	10.0f,   // SPINE
	10.0f,   // SPINE1
	15.0f,   // HEAD
	45.0f,   // L_THIGH
	10.0f,   // L_CALF
	45.0f,   // R_THIGH
	10.0f,   // R_CALF
	45.0f,   // L_UPPERARM
	10.0f,   // L_FOREARM
	45.0f,   // R_UPPERARM
	10.0f,   // R_FOREARM
};

// Map from SA bone index (0-11) to Bullet body part index (0-10)
// SPINE and SPINE1 both map to BULLET_BODYPART_SPINE
const int32 g_boneToBulletMap[RAGDOLL_DEATH_BONES] = {
	BULLET_BODYPART_PELVIS,       // 0 PELVIS
	BULLET_BODYPART_SPINE,        // 1 SPINE
	BULLET_BODYPART_SPINE,        // 2 SPINE1 → spine body (shared)
	BULLET_BODYPART_HEAD,         // 3 HEAD
	BULLET_BODYPART_L_UPPER_LEG,  // 4 L_THIGH
	BULLET_BODYPART_L_LOWER_LEG,  // 5 L_CALF
	BULLET_BODYPART_R_UPPER_LEG,  // 6 R_THIGH
	BULLET_BODYPART_R_LOWER_LEG,  // 7 R_CALF
	BULLET_BODYPART_L_UPPER_ARM,  // 8 L_UPPERARM
	BULLET_BODYPART_L_LOWER_ARM,  // 9 L_FOREARM
	BULLET_BODYPART_R_UPPER_ARM,  // 10 R_UPPERARM
	BULLET_BODYPART_R_LOWER_ARM,  // 11 R_FOREARM
};

// Joint parent-child body part mapping
static const int g_jointParentBody[BULLET_JOINT_COUNT] = {
	BULLET_BODYPART_PELVIS,       // JOINT_PELVIS_SPINE
	BULLET_BODYPART_SPINE,        // JOINT_SPINE_HEAD
	BULLET_BODYPART_PELVIS,       // JOINT_L_HIP
	BULLET_BODYPART_L_UPPER_LEG,  // JOINT_L_KNEE
	BULLET_BODYPART_PELVIS,       // JOINT_R_HIP
	BULLET_BODYPART_R_UPPER_LEG,  // JOINT_R_KNEE
	BULLET_BODYPART_SPINE,        // JOINT_L_SHOULDER
	BULLET_BODYPART_L_UPPER_ARM,  // JOINT_L_ELBOW
	BULLET_BODYPART_SPINE,        // JOINT_R_SHOULDER
	BULLET_BODYPART_R_UPPER_ARM,  // JOINT_R_ELBOW
};

static const int g_jointChildBody[BULLET_JOINT_COUNT] = {
	BULLET_BODYPART_SPINE,        // JOINT_PELVIS_SPINE
	BULLET_BODYPART_HEAD,         // JOINT_SPINE_HEAD
	BULLET_BODYPART_L_UPPER_LEG,  // JOINT_L_HIP
	BULLET_BODYPART_L_LOWER_LEG,  // JOINT_L_KNEE
	BULLET_BODYPART_R_UPPER_LEG,  // JOINT_R_HIP
	BULLET_BODYPART_R_LOWER_LEG,  // JOINT_R_KNEE
	BULLET_BODYPART_L_UPPER_ARM,  // JOINT_L_SHOULDER
	BULLET_BODYPART_L_LOWER_ARM,  // JOINT_L_ELBOW
	BULLET_BODYPART_R_UPPER_ARM,  // JOINT_R_SHOULDER
	BULLET_BODYPART_R_LOWER_ARM,  // JOINT_R_ELBOW
};

// ============================================================
// Utility: convert btTransform ↔ bone transform arrays
// ============================================================

static void BulletTransformToBone(const btTransform &bt, float posOut[3], float rotOut[4]) {
	const btVector3 &origin = bt.getOrigin();
	posOut[0] = origin.x();
	posOut[1] = origin.y();
	posOut[2] = origin.z();

	const btQuaternion &q = bt.getRotation();
	rotOut[0] = q.x();
	rotOut[1] = q.y();
	rotOut[2] = q.z();
	rotOut[3] = q.w();
}

static void BoneToBulletTransform(const float pos[3], const float rot[4], btTransform &out) {
	out.setIdentity();
	out.setOrigin(btVector3(pos[0], pos[1], pos[2]));

	// Clamp quaternion to avoid NaN from degenerate input
	float qlen = rot[0]*rot[0] + rot[1]*rot[1] + rot[2]*rot[2] + rot[3]*rot[3];
	if (qlen < 1e-7f) {
		out.setRotation(btQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
	} else {
		float inv = 1.0f / sqrtf(qlen);
		out.setRotation(btQuaternion(rot[0]*inv, rot[1]*inv, rot[2]*inv, rot[3]*inv));
	}
}

// ============================================================
// Ragdoll_Init — create shared Bullet dynamics world
// ============================================================

void Ragdoll_Init(void) {
	if (g_bulletWorld) return; // already initialized

	g_collisionConfig = new btDefaultCollisionConfiguration();
	if (!g_collisionConfig) {
		dbglog("Ragdoll ERROR: failed to create collision config");
		return;
	}
	g_dispatcher = new btCollisionDispatcher(g_collisionConfig);
	g_broadphase = new btDbvtBroadphase();
	g_solver = new btSequentialImpulseConstraintSolver();
	g_bulletWorld = new btDiscreteDynamicsWorld(g_dispatcher, g_broadphase, g_solver, g_collisionConfig);

	if (!g_bulletWorld) {
		dbglog("Ragdoll ERROR: failed to create dynamics world");
		return;
	}

	// Gravity: (0, 0, -19.6) — SA 2x real-world scale
	g_bulletWorld->setGravity(btVector3(0.0f, 0.0f, -19.6f));

	// Load ragdoll.ini config
	LoadRagdollConfig();

	dbglog("Ragdoll_Init: Bullet world created, gravity (0,0,-19.6)");

	// Register scope tags for crash backtrace
	diag_registerScope("Ragdoll_Simulate", (void*)Ragdoll_Simulate);
	diag_registerScope("Ragdoll_ApplyPose", (void*)Ragdoll_ApplyPose);
	diag_registerScope("Ragdoll_Activate", (void*)Ragdoll_Activate);
	diag_registerScope("Ragdoll_Deactivate", (void*)Ragdoll_Deactivate);
	diag_registerScope("Dismember_Render", (void*)Dismember_Render);
	diag_registerScope("Dismember_ProcessPed", (void*)Dismember_ProcessPed);

	// Init dismemberment
	Dismember_Init();
}

// ============================================================
// Ragdoll_Shutdown — destroy shared Bullet world
// ============================================================

void Ragdoll_Shutdown(void) {
	// Deactivate all active instances first
	for (int i = 0; i < RAGDOLL_DEATH_POOL_SIZE; i++) {
		if (g_ragdollDeathPool[i].state != RAGDOLL_DEATH_INACTIVE) {
			Ragdoll_Deactivate(g_ragdollDeathPool[i].pPed);
		}
	}

	delete g_bulletWorld; g_bulletWorld = nullptr;
	delete g_solver; g_solver = nullptr;
	delete g_broadphase; g_broadphase = nullptr;
	delete g_dispatcher; g_dispatcher = nullptr;
	delete g_collisionConfig; g_collisionConfig = nullptr;

	Dismember_Shutdown();

	dbglog("Ragdoll_Shutdown: Bullet world destroyed");
}

// ============================================================
// Ragdoll_Activate — spawn Bullet ragdoll for a dead ped
// ============================================================

bool Ragdoll_Activate(void *pPed) {
	if (!g_ragdollDeathEnable || !g_bulletWorld) return false;
	if (!pPed) return false;

	// Find free pool slot
	RagdollDeathInstance *inst = nullptr;
	for (int i = 0; i < RAGDOLL_DEATH_POOL_SIZE; i++) {
		if (g_ragdollDeathPool[i].state == RAGDOLL_DEATH_INACTIVE) {
			inst = &g_ragdollDeathPool[i];
			break;
		}
	}
	if (!inst) return false; // pool full

	// Initialize instance
	inst->state = RAGDOLL_DEATH_ACTIVE;
	inst->pPed = pPed;
	inst->settleFrames = 0;
	inst->settleTimer = 0.0f;
	inst->groundZ = 0.0f;
	inst->dismemberState = (void*)new DismemberState();
	inst->processedBodypart = 0;

	// Get ped clump (RpClump*)
	// SA v1.0 US: CEntity::m_pRwObject (or m_pRwClump) at offset 0x18
	// Verified: plugin-sdk CEntity.h:154 VALIDATE_OFFSET(CEntity, m_pRwObject, 0x18)
	//           existing ragdoll.cpp:480 reads ped+0x18 for clump
	void *clump = *(void**)((uintptr_t)pPed + 0x18);
	if (!clump) {
		dbglog("Ragdoll_Activate: no clump for ped %p", pPed);
		inst->state = RAGDOLL_DEATH_INACTIVE;
		return false;
	}
	inst->pClump = clump;

	// Get hierarchy using the game's helper function
	RpHAnimHierarchy *hierarchy = (RpHAnimHierarchy*)GetAnimHierarchyFromSkinClump(clump);
	if (!hierarchy) {
		dbglog("Ragdoll_Activate: no hierarchy for ped %p", pPed);
		inst->state = RAGDOLL_DEATH_INACTIVE;
		return false;
	}

	// Resolve bone indices
	for (int i = 0; i < RAGDOLL_DEATH_BONES; i++) {
		inst->boneIndices[i] = RpHAnimIDGetIndex(hierarchy, g_ragdollDeathBoneTags[i]);
		if (inst->boneIndices[i] < 0) {
			dbglog("Ragdoll_Activate: bone tag %d not found in hierarchy", g_ragdollDeathBoneTags[i]);
		}
	}

	// Get matrix array from hierarchy
	RwMatrix *matArray = (RwMatrix*)RpHAnimHierarchyGetMatrixArray(hierarchy);
	if (!matArray) {
		dbglog("Ragdoll_Activate: null matrix array");
		inst->state = RAGDOLL_DEATH_INACTIVE;
		return false;
	}

	// We need the ped's world position for ground plane
	// SA v1.0 US: CPed inherits CPlaceable. Plugin-sdk CPed.h:
	//   CPlaceable.m_placement at CPed+0x04 (CSimpleTransform)
	//   CSimpleTransform.m_vPosn at +0x00 (CVector: x,y,z)
	//   CSimpleTransform.m_fHeading at +0x0C
	// Therefore m_vPosn.z is at CPed + 0x04 + 0x08 = CPed + 0x0C
	float pedWorldZ = 0.0f;
	{
		uintptr_t pedAddr = (uintptr_t)pPed;
		pedWorldZ = *(float*)(pedAddr + 0x0C);
		pedWorldZ = fabs(pedWorldZ) > 10000.0f ? 0.0f : pedWorldZ;
	}
	inst->groundZ = pedWorldZ;

	// Capture bone transforms: each bone index gives us a world-space matrix
	// from the hierarchy. Store as pos + quat.
	for (int i = 0; i < RAGDOLL_DEATH_BONES; i++) {
		int idx = inst->boneIndices[i];
		if (idx < 0 || idx >= 256) {
			// Invalid bone, use identity
			inst->boneWorldPos[i][0] = 0.0f;
			inst->boneWorldPos[i][1] = 0.0f;
			inst->boneWorldPos[i][2] = pedWorldZ;
			inst->boneWorldRot[i][0] = 0.0f;
			inst->boneWorldRot[i][1] = 0.0f;
			inst->boneWorldRot[i][2] = 0.0f;
			inst->boneWorldRot[i][3] = 1.0f;
			continue;
		}

		RwMatrix *m = &matArray[idx];
		inst->boneWorldPos[i][0] = m->pos.x;
		inst->boneWorldPos[i][1] = m->pos.y;
		inst->boneWorldPos[i][2] = m->pos.z;

		// Convert RwMatrix to quaternion
		// RwMatrix right = at.x, up.x, up.y, at.x... Actually RwMatrix is:
		// right.x, right.y, right.z, 0
		// up.x,    up.y,    up.z,    0
		// at.x,    at.y,    at.z,    0
		// pos.x,   pos.y,   pos.z,   1
		// We need a 3x3 rotation matrix for quaternion conversion
		{
			float r00 = m->right.x, r01 = m->right.y, r02 = m->right.z;
			float r10 = m->up.x,    r11 = m->up.y,    r12 = m->up.z;
			float r20 = m->at.x,    r21 = m->at.y,    r22 = m->at.z;

			float trace = r00 + r11 + r22;
			float qx, qy, qz, qw;
			if (trace > 0.0f) {
				float s = 0.5f / sqrtf(trace + 1.0f);
				qw = 0.25f / s;
				qx = (r21 - r12) * s;
				qy = (r02 - r20) * s;
				qz = (r10 - r01) * s;
			} else if (r00 > r11 && r00 > r22) {
				float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
				qw = (r21 - r12) / s;
				qx = 0.25f * s;
				qy = (r01 + r10) / s;
				qz = (r02 + r20) / s;
			} else if (r11 > r22) {
				float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
				qw = (r02 - r20) / s;
				qx = (r01 + r10) / s;
				qy = 0.25f * s;
				qz = (r12 + r21) / s;
			} else {
				float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
				qw = (r10 - r01) / s;
				qx = (r02 + r20) / s;
				qy = (r12 + r21) / s;
				qz = 0.25f * s;
			}
			inst->boneWorldRot[i][0] = qx;
			inst->boneWorldRot[i][1] = qy;
			inst->boneWorldRot[i][2] = qz;
			inst->boneWorldRot[i][3] = qw;
		}
	}

	// ---- Create Bullet shapes ----
	// Capsule dimensions (radius, half-height) — relative sizes, Bullet capsules are Z-aligned
	// We use SA scale: 1.0 ≈ 1 meter. Ped is ~1.8m tall.
	struct CapsuleDim { float radius; float halfHeight; };
	static const CapsuleDim capsuleDims[BULLET_BODYPART_COUNT] = {
		{0.20f, 0.12f},  // PELVIS
		{0.15f, 0.20f},  // SPINE
		{0.12f, 0.08f},  // HEAD
		{0.10f, 0.22f},  // L_UPPER_LEG
		{0.08f, 0.20f},  // L_LOWER_LEG
		{0.10f, 0.22f},  // R_UPPER_LEG
		{0.08f, 0.20f},  // R_LOWER_LEG
		{0.08f, 0.18f},  // L_UPPER_ARM
		{0.07f, 0.14f},  // L_LOWER_ARM
		{0.08f, 0.18f},  // R_UPPER_ARM
		{0.07f, 0.14f},  // R_LOWER_ARM
	};

	// Map from body part to the first bone index that drives it (for initial pos/rot)
	static const int bodyToBoneMap[BULLET_BODYPART_COUNT] = {
		0,  // PELVIS → bone 0
		1,  // SPINE → bone 1 (SPINE)
		3,  // HEAD → bone 3
		4,  // L_UPPER_LEG → bone 4
		5,  // L_LOWER_LEG → bone 5
		6,  // R_UPPER_LEG → bone 6
		7,  // R_LOWER_LEG → bone 7
		8,  // L_UPPER_ARM → bone 8
		9,  // L_FOREARM → bone 9
		10, // R_UPPER_ARM → bone 10
		11, // R_FOREARM → bone 11
	};

	// Create bodies from captured transforms
	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		inst->shapes[i] = nullptr;
		inst->bodies[i] = nullptr;
	}

	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		int boneIdx = bodyToBoneMap[i];

		// Create capsule shape (Bullet capsule is Z-aligned)
		btCapsuleShape *shape = new btCapsuleShape(capsuleDims[i].radius, capsuleDims[i].halfHeight * 2.0f);
		inst->shapes[i] = shape;

		// Setup initial transform from captured bone
		btTransform startTransform;
		BoneToBulletTransform(inst->boneWorldPos[boneIdx], inst->boneWorldRot[boneIdx], startTransform);

		// Compute inertia
		float mass = g_bodyMasses[i];
		btVector3 localInertia(0, 0, 0);
		if (mass > 0.0f) {
			shape->calculateLocalInertia(mass, localInertia);
		}

		// Create motion state and rigid body
		btDefaultMotionState *motionState = new btDefaultMotionState(startTransform);
		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, shape, localInertia);
		btRigidBody *body = new btRigidBody(rbInfo);
		inst->bodies[i] = body;

		// Damping: linear 0.05, angular 0.85
		body->setDamping(0.05f, 0.85f);
		body->setDeactivationTime(0.8f);
		body->setSleepingThresholds(1.6f, 2.5f);

		// Add to world with ragdoll collision group
		// Ragdoll bodies collide with GROUND but not with each other by default
		// (we explicitly disable collisions between jointed pairs below)
		g_bulletWorld->addRigidBody(body, COLGROUP_RAGDOLL, COLGROUP_GROUND);
	}

	// ---- Create constraints ----
	for (int j = 0; j < BULLET_JOINT_COUNT; j++) {
		inst->joints[j] = nullptr;
	}

	// Helper: compute joint frame in local space of each body
	// The joint pivot is at the midpoint between the two bodies' origins
	// We'll compute this from the captured bone positions
	for (int j = 0; j < BULLET_JOINT_COUNT; j++) {
		int parentBodyIdx = g_jointParentBody[j];
		int childBodyIdx  = g_jointChildBody[j];

		btRigidBody *parentBody = inst->bodies[parentBodyIdx];
		btRigidBody *childBody  = inst->bodies[childBodyIdx];
		if (!parentBody || !childBody) continue;

		// Joint pivot is average of parent and child origins
		btVector3 parentOrigin = parentBody->getCenterOfMassPosition();
		btVector3 childOrigin  = childBody->getCenterOfMassPosition();
		btVector3 pivot = (parentOrigin + childOrigin) * 0.5f;

		// Joint frame in local space of each body
		btTransform parentFrame, childFrame;
		parentFrame = parentBody->getCenterOfMassTransform().inverse();
		parentFrame.setOrigin(parentFrame * pivot);
		childFrame = childBody->getCenterOfMassTransform().inverse();
		childFrame.setOrigin(childFrame * pivot);
		// Keep identity rotation for local frames — the constraint axes
		// are defined relative to the body local frames

		btTypedConstraint *constraint = nullptr;

		// Determine constraint type based on joint index
		if (j == BULLET_JOINT_L_KNEE || j == BULLET_JOINT_R_KNEE ||
			j == BULLET_JOINT_L_ELBOW || j == BULLET_JOINT_R_ELBOW) {
			// ---- Hinge constraint for knees/elbows ----
			// Hinge axis is local X in both frames (flexion/extension)
			btHingeConstraint *hinge = new btHingeConstraint(
				*parentBody, *childBody,
				parentFrame, childFrame
			);
			// Set the hinge axis (local X axis)
			// For knees: limit 0° to 130° (backward bend only)
			// For elbows: limit 0° to 150°
			float lowLimit = g_jointConfigs[j].hingeLow;
			float highLimit = g_jointConfigs[j].hingeHigh;
			hinge->setLimit(lowLimit * SIMD_RADS_PER_DEG, highLimit * SIMD_RADS_PER_DEG);
			constraint = hinge;

		} else {
			// ---- ConeTwist constraint for all other joints ----
			btConeTwistConstraint *coneTwist = new btConeTwistConstraint(
				*parentBody, *childBody,
				parentFrame, childFrame
			);
			// Set angular limits
			float swing1 = g_jointConfigs[j].swingSpan1 * SIMD_RADS_PER_DEG;
			float swing2 = g_jointConfigs[j].swingSpan2 * SIMD_RADS_PER_DEG;
			float twist  = g_jointConfigs[j].twistSpan * SIMD_RADS_PER_DEG;
			coneTwist->setLimit(swing1, swing2, twist);

			// For spine/pelvis/head (near-rigid), set high stiffness
			if (j == BULLET_JOINT_PELVIS_SPINE || j == BULLET_JOINT_SPINE_HEAD) {
				coneTwist->setDamping(0.5f);
				// Motors disabled — pure constraint limit is sufficient
			}

			constraint = coneTwist;
		}

		inst->joints[j] = constraint;

		if (constraint) {
			constraint->setDbgDrawSize(btScalar(5.0f));
			g_bulletWorld->addConstraint(constraint, true); // disableCollisionsBetweenLinkedBodies=true
		}
	}

	// Disable collision between all ragdoll bodies (they should not self-collide)
	// The "disableCollisionsBetweenLinkedBodies=true" on constraints handles
	// linked pairs, but bodies that aren't directly linked via a constraint
	// (e.g. left arm and right leg) could still collide. We disable ALL
	// internal collisions by making ragdoll bodies not collide with each other.
	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		if (inst->bodies[i]) {
			// Re-register to only collide with ground
			g_bulletWorld->removeRigidBody(inst->bodies[i]);
			g_bulletWorld->addRigidBody(inst->bodies[i], COLGROUP_RAGDOLL, COLGROUP_GROUND);
		}
	}

	// ---- Create ground plane ----
	btStaticPlaneShape *groundShape = new btStaticPlaneShape(btVector3(0.0f, 0.0f, 1.0f), -inst->groundZ);
	inst->groundShape = groundShape;

	btDefaultMotionState *groundMotionState = new btDefaultMotionState();
	btRigidBody::btRigidBodyConstructionInfo groundRbInfo(0.0f, groundMotionState, groundShape, btVector3(0,0,0));
	btRigidBody *groundBody = new btRigidBody(groundRbInfo);
	inst->groundBody = groundBody;

	// Ground is static, collides with ragdoll only
	g_bulletWorld->addRigidBody(groundBody, COLGROUP_GROUND, COLGROUP_RAGDOLL);

	// ============================================================
	// Hit-area-aware ragdoll reaction
	// Apply death impulse to the limb matching the hit bodypart.
	//
	// Verified offsets (plugin-sdk SA v1.0 US):
	//   CPhysical::m_vecMoveSpeed (+0x44)  — physics velocity at death
	//   CPhysical::m_fDamageIntensity (+0xD8) — damage magnitude
	//   CPed::m_nBodypartToRemove (+0x754)  — ePedBone ID of hit limb
	//   ePedBones enum: ePedBones.h (plugin-sdk)
	// ============================================================
	{
		// Read hit body part and damage direction from the ped
		unsigned char hitBone   = *(unsigned char*)((uintptr_t)pPed + 0x754);
		float damageIntensity   = *(float*)((uintptr_t)pPed + 0xD8);
		CVector moveSpeed       = *(CVector*)((uintptr_t)pPed + 0x44);

		// Build damage direction from m_vecMoveSpeed.
		// The game applies the weapon impulse as a velocity delta,
		// so the last move speed is a reasonable direction indicator.
		float dirLen = sqrtf(moveSpeed.x*moveSpeed.x + moveSpeed.y*moveSpeed.y + moveSpeed.z*moveSpeed.z);
		btVector3 damageDir(0.0f, 0.0f, 0.0f);
		if (dirLen > 0.001f) {
			damageDir = btVector3(moveSpeed.x / dirLen, moveSpeed.y / dirLen, moveSpeed.z / dirLen);
		} else {
			// Fallback: random horizontal + slight upward
			// Use a hash of the ped address for determinism
			unsigned int h = (unsigned int)((uintptr_t)pPed);
			float angle = (float)(h & 0xFFFF) * 0.00009587f; // map to ~2pi
			damageDir = btVector3(cosf(angle), sinf(angle), 0.3f);
			dirLen = 0.5f; // weak fallback
		}

		// Add upward bias for dramatic fold
		damageDir.setZ(damageDir.z() + g_hitUpBias);
		damageDir.normalize();

		// Map ePedBone to Bullet body index
		static const int hitBoneToBody[55] = {
			-1,                       // 0  (unused)
			BULLET_BODYPART_PELVIS,   // 1  BONE_PELVIS1
			BULLET_BODYPART_PELVIS,   // 2  BONE_PELVIS
			BULLET_BODYPART_SPINE,    // 3  BONE_SPINE1
			BULLET_BODYPART_SPINE,    // 4  BONE_UPPERTORSO
			BULLET_BODYPART_HEAD,     // 5  BONE_NECK
			BULLET_BODYPART_HEAD,     // 6  BONE_HEAD2
			BULLET_BODYPART_HEAD,     // 7  BONE_HEAD1
			BULLET_BODYPART_HEAD,     // 8  BONE_HEAD
			-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 9-20
			-1,                       // 21 BONE_RIGHTUPPERTORSO
			BULLET_BODYPART_R_UPPER_ARM, // 22 BONE_RIGHTSHOULDER
			BULLET_BODYPART_R_LOWER_ARM, // 23 BONE_RIGHTELBOW
			BULLET_BODYPART_R_LOWER_ARM, // 24 BONE_RIGHTWRIST
			BULLET_BODYPART_R_LOWER_ARM, // 25 BONE_RIGHTHAND
			BULLET_BODYPART_R_LOWER_ARM, // 26 BONE_RIGHTTHUMB
			-1, -1, -1, -1,          // 27-30
			-1,                       // 31 BONE_LEFTUPPERTORSO
			BULLET_BODYPART_L_UPPER_ARM, // 32 BONE_LEFTSHOULDER
			BULLET_BODYPART_L_LOWER_ARM, // 33 BONE_LEFTELBOW
			BULLET_BODYPART_L_LOWER_ARM, // 34 BONE_LEFTWRIST
			BULLET_BODYPART_L_LOWER_ARM, // 35 BONE_LEFTHAND
			BULLET_BODYPART_L_LOWER_ARM, // 36 BONE_LEFTTHUMB
			-1, -1, -1, -1,          // 37-40
			BULLET_BODYPART_L_UPPER_LEG, // 41 BONE_LEFTHIP
			BULLET_BODYPART_L_LOWER_LEG, // 42 BONE_LEFTKNEE
			BULLET_BODYPART_L_LOWER_LEG, // 43 BONE_LEFTANKLE
			BULLET_BODYPART_L_LOWER_LEG, // 44 BONE_LEFTFOOT
			-1, -1, -1, -1, -1, -1,  // 45-50
			BULLET_BODYPART_R_UPPER_LEG, // 51 BONE_RIGHTHIP
			BULLET_BODYPART_R_LOWER_LEG, // 52 BONE_RIGHTKNEE
			BULLET_BODYPART_R_LOWER_LEG, // 53 BONE_RIGHTANKLE
			BULLET_BODYPART_R_LOWER_LEG, // 54 BONE_RIGHTFOOT
		};

		int hitBodyIdx = (hitBone >= 1 && hitBone <= 54) ? hitBoneToBody[hitBone] : -1;
		if (hitBodyIdx < 0) hitBodyIdx = BULLET_BODYPART_SPINE; // fallback

		// Apply impulse to the hit body
		float impulseMag = g_hitImpulseScale * sqrtf(g_bodyMasses[hitBodyIdx]);
		if (damageIntensity > 0.0f && damageIntensity < 1000.0f) {
			impulseMag *= damageIntensity * 0.1f;  // scale by damage
		}
		btVector3 impulse = damageDir * impulseMag;

		btRigidBody *hitBody = inst->bodies[hitBodyIdx];
		if (hitBody) {
			// Apply impulse at the body's center of mass
			btVector3 com = hitBody->getCenterOfMassPosition();
			hitBody->applyImpulse(impulse, com);

			// Apply torque to add rotational reaction
			// Cross product of damage direction with a random-ish offset gives spin
			btVector3 upVec(0.0f, 0.0f, 1.0f);
			btVector3 spinAxis = damageDir.cross(upVec);
			float spinLen = spinAxis.length();
			if (spinLen > 0.001f) {
				spinAxis /= spinLen;
				btVector3 torque = spinAxis * impulseMag * g_hitAngScale;
				hitBody->applyTorqueImpulse(torque);
			}

			// Also apply a fraction to the spine for whole-body pivot
			btRigidBody *spineBody = inst->bodies[BULLET_BODYPART_SPINE];
			if (spineBody && hitBodyIdx != BULLET_BODYPART_SPINE) {
				spineBody->applyImpulse(impulse * g_hitSpineFraction, spineBody->getCenterOfMassPosition());
			}
		}

		// Log hit reaction
		static const char *boneNames[] = {
			"", "PELVIS1", "PELVIS", "SPINE1", "UPPERTORSO", "NECK",
			"HEAD2", "HEAD1", "HEAD", "", "", "", "", "", "", "", "", "", "", "", "",
			"", "RIGHTUPPERTORSO", "RIGHTSHOULDER", "RIGHTELBOW", "RIGHTWRIST", "RIGHTHAND", "",
			"", "", "", "",
			"LEFTUPPERTORSO", "LEFTSHOULDER", "LEFTELBOW", "LEFTWRIST", "LEFTHAND", "",
			"", "", "", "",
			"LEFTHIP", "LEFTKNEE", "LEFTANKLE", "LEFTFOOT",
			"", "", "", "", "", "",
			"RIGHTHIP", "RIGHTKNEE", "RIGHTANKLE", "RIGHTFOOT"
		};
		const char *hitName = (hitBone >= 1 && hitBone <= 54) ? boneNames[hitBone] : "?";
		if (dbglog_throttle("ragdoll_hit")) {
			dbglog("Ragdoll hit: bone=%d(%s) bodyIdx=%d impulse=(%.2f,%.2f,%.2f) mag=%.1f",
				hitBone, hitName, hitBodyIdx, impulse.x(), impulse.y(), impulse.z(), impulseMag);
		}
	}

	// ---- Extension point: partial hit-reaction ----
	// To add per-limb hit-reaction on alive (not dead) peds:
	// 1. Keep the alive-ped Ragdoll_c (src/entities/ragdoll.cpp) running
	//    for blend-target computation.
	// 2. Define `Ragdoll_HitReaction(void *pPed, int ePedBone, btVector3 impulse)`:
	//    - Find or create a RagdollDeathInstance for the ped.
	//    - If body doesn't exist yet for the hit bone (ePedBone → hitBoneToBody[]):
	//      create it as a dynamic body at the current hierarchy position,
	//      add it to the world with COLGROUP_RAGDOLL.
	//    - Call hitBody->activate() to wake it.
	//    - Call hitBody->applyImpulse(impulse, com).
	//    - Track a "hitTimer" per body; blend from body transform→animation pose
	//      using lerp. When timer expires, remove body.
	// 3. Call from death polling or weapon-fire hook when ped is still alive
	//    and the limb is not yet a physics body.
	// ---- End extension point ----

	dbglog("Ragdoll_Activate: ped %p activated (%d bodies, %d joints)", pPed, BULLET_BODYPART_COUNT, BULLET_JOINT_COUNT);
	return true;
}

// ============================================================
// Ragdoll_Deactivate — remove and destroy a ragdoll instance
// ============================================================

void Ragdoll_Deactivate(void *pPed) {
	if (!g_bulletWorld) return;

	// Find instance for this ped
	RagdollDeathInstance *inst = nullptr;
	for (int i = 0; i < RAGDOLL_DEATH_POOL_SIZE; i++) {
		if (g_ragdollDeathPool[i].pPed == pPed &&
			g_ragdollDeathPool[i].state != RAGDOLL_DEATH_INACTIVE) {
			inst = &g_ragdollDeathPool[i];
			break;
		}
	}
	if (!inst) return;

	// Remove and delete constraints
	for (int j = 0; j < BULLET_JOINT_COUNT; j++) {
		if (inst->joints[j]) {
			g_bulletWorld->removeConstraint(inst->joints[j]);
			delete inst->joints[j];
			inst->joints[j] = nullptr;
		}
	}

	// Remove and delete bodies (including their motion states)
	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		if (inst->bodies[i]) {
			g_bulletWorld->removeRigidBody(inst->bodies[i]);

			// Delete motion state
			btMotionState *ms = inst->bodies[i]->getMotionState();
			if (ms) delete ms;

			delete inst->bodies[i];
			inst->bodies[i] = nullptr;
		}
		if (inst->shapes[i]) {
			delete inst->shapes[i];
			inst->shapes[i] = nullptr;
		}
	}

	// Remove and delete ground body
	if (inst->groundBody) {
		g_bulletWorld->removeRigidBody(inst->groundBody);
		btMotionState *ms = inst->groundBody->getMotionState();
		if (ms) delete ms;
		delete inst->groundBody;
		inst->groundBody = nullptr;
	}
	if (inst->groundShape) {
		delete inst->groundShape;
		inst->groundShape = nullptr;
	}

	inst->state = RAGDOLL_DEATH_INACTIVE;
	inst->pPed = nullptr;
	inst->settleFrames = 0;
	inst->settleTimer = 0.0f;

	// Free dismember state
	if (inst->dismemberState) {
		delete (DismemberState*)inst->dismemberState;
		inst->dismemberState = nullptr;
	}
	inst->processedBodypart = 0;

	dbglog("Ragdoll_Deactivate: ped %p deactivated", pPed);
}

// ============================================================
// Ragdoll_Simulate — death polling + physics stepping
// V7: Gated per render-frame using RWSRCGLOBAL(renderFrame) to
// prevent multiple steps per frame from mirror/shadow passes.
// Hierarchy write-back NOT done here — call Ragdoll_ApplyPose
// from RenderScene_before so matrices are set BEFORE rendering.
// ============================================================

// RwEngineInstance global for renderFrame access
// (same pattern as buildingPipe.cpp ~:96, which uses RWSRCGLOBAL(renderFrame)).
// RWSRCGLOBAL is defined in rwplcore.h:6255 as ((RwGlobals *)RwEngineInstance)->variable
// RwGlobals.renderFrame is at offset +8 (after curCamera +0, curWorld +4).
// RwEngineInstance is declared extern void* in the RW SDK headers and linked from the game's RW DLL.
#define CURRENT_RENDER_FRAME RWSRCGLOBAL(renderFrame)

void Ragdoll_Simulate(void) {
	if (!g_ragdollDeathEnable || !g_bulletWorld) return;

	// V7: Frame gate — only step physics once per unique render frame.
	// Mirror/shadow passes re-enter RenderScene but share the same
	// renderFrame value, so the gate prevents double-stepping.
	static RwUInt16 s_lastRenderFrame = 0;
	RwUInt16 curFrame = CURRENT_RENDER_FRAME;
	if (curFrame == s_lastRenderFrame) return;
	s_lastRenderFrame = curFrame;

	// ============================================================
	// Ped death polling — scan GTA SA ped pool for newly dead peds.
	//
	// Verified offsets (plugin-sdk, SA v1.0 US, CPed.h:468, CEntity.h:154):
	//   CPool<CPed>* at 0xB74490   — pool structure pointer
	//   CPool.m_aStorage (+0x00)   — T* base of ped array
	//   CPool.m_aFlags   (+0x04)   — uint8* flags (0x80 = free)
	//   CPool.m_nSize    (+0x08)   — int32 pool capacity
	//   CEntity.m_pRwClump (+0x18) — RpClump* for skinned peds
	//   CPed.m_ePedState (+0x530)  — ePedState (PEDSTATE_DEAD=66)
	// ============================================================
	struct PoolIter {
		void *storage;          // +0
		unsigned char *flags;   // +4
		int size;               // +8
	};
	static int s_activatedTotal = 0;
	static int s_throttleCounter = 0;
	s_throttleCounter++;

	PoolIter *pool = *(PoolIter**)0xB74490;
	int scanned = 0, deadCount = 0;

	if (pool && pool->storage && pool->flags && pool->size > 0) {
		static const int kPedStride = 0x7C4;

		for (int i = 0; i < pool->size; i++) {
			if (pool->flags[i] & 0x80) continue;
			scanned++;
			unsigned char *ped = (unsigned char*)pool->storage + i * kPedStride;
			if (!ped) continue;

			int pedState = *(int*)(ped + 0x530);
			if (pedState == 66 || pedState == 65) {
				deadCount++;
				bool alreadyActive = false;
				for (int pi = 0; pi < RAGDOLL_DEATH_POOL_SIZE; pi++) {
					if (g_ragdollDeathPool[pi].pPed == (void*)ped &&
						g_ragdollDeathPool[pi].state != RAGDOLL_DEATH_INACTIVE) {
						alreadyActive = true;
						break;
					}
				}
				if (!alreadyActive) {
					if (Ragdoll_Activate((void*)ped)) {
						s_activatedTotal++;
						int bodypartToRemove = *(unsigned char*)(ped + 0x754);
						float dmX = *(float*)(ped + 0x44);
						float dmY = *(float*)(ped + 0x48);
						float dmZ = *(float*)(ped + 0x4C);
						float dmIntensity = *(float*)(ped + 0xD8);

						for (int pi = 0; pi < RAGDOLL_DEATH_POOL_SIZE; pi++) {
							if (g_ragdollDeathPool[pi].pPed == (void*)ped &&
								g_ragdollDeathPool[pi].state == RAGDOLL_DEATH_ACTIVE) {
								g_ragdollDeathPool[pi].processedBodypart = bodypartToRemove;
								if (bodypartToRemove > 0 && bodypartToRemove <= 18) {
									void *clump = *(void**)((uintptr_t)ped + 0x18);
									if (clump) {
										Dismember_ProcessPed((void*)ped, clump, bodypartToRemove,
											dmX, dmY, dmZ, dmIntensity,
											(DismemberState*)g_ragdollDeathPool[pi].dismemberState);
									}
								}
								break;
							}
						}
						dbglog("Ragdoll: death detected -> activated ped %p (state=%d total=%d)",
							ped, pedState, s_activatedTotal);
					}
				}
			} else if (pedState > 0 && pedState != 1 && (s_throttleCounter & 0xFF) == 0) {
				static unsigned long s_lastRawLog[32] = {};
				unsigned long h = ((unsigned long)(uintptr_t)ped >> 4) & 0x1F;
				if (s_lastRawLog[h] != *(unsigned long*)(ped + 0x530)) {
					s_lastRawLog[h] = *(unsigned long*)(ped + 0x530);
					dbglog("Ragdoll: ped %p raw state at +0x530 = %d, health=%.1f",
						ped, pedState, *(float*)(ped + 0x540));
				}
			}
		}
	}

	if ((s_throttleCounter % 100) == 0) {
		int activeCount = 0;
		for (int pi = 0; pi < RAGDOLL_DEATH_POOL_SIZE; pi++) {
			if (g_ragdollDeathPool[pi].state != RAGDOLL_DEATH_INACTIVE)
				activeCount++;
		}
		if (scanned > 0 || deadCount > 0 || activeCount > 0) {
			dbglog("Ragdoll poll: scanned=%d dead=%d active=%d totalActivated=%d",
				scanned, deadCount, activeCount, s_activatedTotal);
		}
	}

	// Get frame delta
	float timeStep = *(float*)0xB7CB5C;
	if (timeStep < 0.001f) timeStep = 0.001f;
	if (timeStep > 1.0f) timeStep = 1.0f;
	float dt = timeStep / 50.0f;
	if (dt > 0.05f) dt = 0.05f;

	// Step simulation for each active instance
	for (int pi = 0; pi < RAGDOLL_DEATH_POOL_SIZE; pi++) {
		RagdollDeathInstance *inst = &g_ragdollDeathPool[pi];
		if (inst->state == RAGDOLL_DEATH_INACTIVE) continue;
		if (inst->state == RAGDOLL_DEATH_DONE) {
			Ragdoll_Deactivate(inst->pPed);
			continue;
		}

		if (g_bulletWorld) {
			g_bulletWorld->stepSimulation(dt, 2, 1.0f / 60.0f);
		}

		// Read back body transforms
		for (int bi = 0; bi < RAGDOLL_DEATH_BONES; bi++) {
			int bodyPart = g_boneToBulletMap[bi];
			if (bodyPart < 0 || bodyPart >= BULLET_BODYPART_COUNT) continue;
			btRigidBody *body = inst->bodies[bodyPart];
			if (!body) continue;

			btTransform worldTrans;
			btMotionState *ms = body->getMotionState();
			if (ms) ms->getWorldTransform(worldTrans);
			else worldTrans = body->getWorldTransform();

			float pos[3], rot[4];
			BulletTransformToBone(worldTrans, pos, rot);

			inst->boneWorldPos[bi][0] = pos[0];
			inst->boneWorldPos[bi][1] = pos[1];
			inst->boneWorldPos[bi][2] = pos[2];
			inst->boneWorldRot[bi][0] = rot[0];
			inst->boneWorldRot[bi][1] = rot[1];
			inst->boneWorldRot[bi][2] = rot[2];
			inst->boneWorldRot[bi][3] = rot[3];
		}

		// Check settle condition
		if (inst->state == RAGDOLL_DEATH_ACTIVE) {
			if (inst->AllBodiesSleeping()) {
				inst->settleTimer += dt;
				if (inst->settleTimer >= 1.0f) {
					inst->state = RAGDOLL_DEATH_DONE;
					dbglog("Ragdoll: ped %p settled, removing", inst->pPed);
				} else {
					inst->state = RAGDOLL_DEATH_SETTLING;
				}
			} else {
				inst->settleTimer = 0.0f;
			}
		} else if (inst->state == RAGDOLL_DEATH_SETTLING) {
			if (inst->AllBodiesSleeping()) {
				inst->settleTimer += dt;
				if (inst->settleTimer >= 1.0f) {
					inst->state = RAGDOLL_DEATH_DONE;
					dbglog("Ragdoll: ped %p settled, removing", inst->pPed);
				}
			} else {
				inst->settleTimer = 0.0f;
				inst->state = RAGDOLL_DEATH_ACTIVE;
			}
		}
	}

	// Gib lifetime management
	Dismember_Update();
	Dismember_Render();
}

// ============================================================
// Ragdoll_ApplyPose — hierarchy write-back for visible bones
// Called from RenderScene_before so matrices are live for render.
// V7: Separated from Simulate to avoid 1-frame stale poses.
// ============================================================
void Ragdoll_ApplyPose(void) {
	if (!g_ragdollDeathEnable) return;

	for (int pi = 0; pi < RAGDOLL_DEATH_POOL_SIZE; pi++) {
		RagdollDeathInstance *inst = &g_ragdollDeathPool[pi];
		if (inst->state == RAGDOLL_DEATH_INACTIVE) continue;
		if (inst->state == RAGDOLL_DEATH_DONE) continue;

		// ---- Write-back to hierarchy ----
		{
			void *clump = inst->pClump;
			if (clump) {
				void *hier = GetAnimHierarchyFromSkinClump(clump);
				if (hier) {
					RwMatrix *matArray = (RwMatrix*)RpHAnimHierarchyGetMatrixArray(hier);
					if (matArray) {
						for (int bi = 0; bi < RAGDOLL_DEATH_BONES; bi++) {
							int idx = inst->boneIndices[bi];
							if (idx < 0) continue;

							float qx = inst->boneWorldRot[bi][0];
							float qy = inst->boneWorldRot[bi][1];
							float qz = inst->boneWorldRot[bi][2];
							float qw = inst->boneWorldRot[bi][3];

							float qlen = qx*qx + qy*qy + qz*qz + qw*qw;
							if (qlen < 1e-7f) { qw = 1.0f; qlen = 1.0f; }
							float inv = 1.0f / sqrtf(qlen);
							qx *= inv; qy *= inv; qz *= inv; qw *= inv;

							float xx = qx*qx, yy = qy*qy, zz = qz*qz;
							float xy = qx*qy, xz = qx*qz, xw = qx*qw;
							float yz = qy*qz, yw = qy*qw, zw = qz*qw;

							RwMatrix *m = &matArray[idx];
							m->right.x = 1.0f - 2.0f*(yy+zz);
							m->right.y = 2.0f*(xy+zw);
							m->right.z = 2.0f*(xz-yw);
							m->up.x    = 2.0f*(xy-zw);
							m->up.y    = 1.0f - 2.0f*(xx+zz);
							m->up.z    = 2.0f*(yz+xw);
							m->at.x    = 2.0f*(xz+yw);
							m->at.y    = 2.0f*(yz-xw);
							m->at.z    = 1.0f - 2.0f*(xx+yy);
							m->pos.x   = inst->boneWorldPos[bi][0];
							m->pos.y   = inst->boneWorldPos[bi][1];
							m->pos.z   = inst->boneWorldPos[bi][2];
						}
					}
				}
			}
		}

		// Dismemberment: zero-scale severed bones + blood spurts
		if (inst->dismemberState && inst->pClump) {
			Dismember_ApplyZeroScale(inst->pClump, (DismemberState*)inst->dismemberState);
			Dismember_SpurtBlood(inst->pClump, (DismemberState*)inst->dismemberState);
		}
	}
}

// ============================================================
// RagdollDeathInstance::AllBodiesSleeping
// ============================================================

bool RagdollDeathInstance::AllBodiesSleeping() const {
	for (int i = 0; i < BULLET_BODYPART_COUNT; i++) {
		if (bodies[i]) {
			// In Bullet, isActive() returns true for active (awake) bodies.
			// Sleeping bodies return false from isActive().
			// Also check if the body was explicitly deactivated.
			int actState = bodies[i]->getActivationState();
			if (actState == ACTIVE_TAG || actState == WANTS_DEACTIVATION) {
				return false;
			}
		}
	}
	return true;
}