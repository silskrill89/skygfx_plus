#pragma once
#include "../skygfx.h"

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