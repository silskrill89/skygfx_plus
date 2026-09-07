#include "dismember.h"
#include "ragdoll_death.h"
#include "../rw/gta.h"
#include "../rw/rphanim.h"
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <LinearMath/btDefaultMotionState.h>
#include <LinearMath/btTransform.h>
#include <LinearMath/btVector3.h>
#include <windows.h>
#include <stdio.h>
#include <float.h>

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
typedef int  (*RpClumpDestroy_t)(void*);
#define RpClumpDestroy(c)              ((int(*)(void*))0x748C50)(c)

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
		if (g->body && g_bulletWorld) {
			g_bulletWorld->removeRigidBody(g->body);
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
	((ForAllAtomics_t)0x748E90)(pedClump, cb0, &firstAtomic);
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
	if (hidx < 0) return nullptr;

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

	// Set render callback to the default atomic renderer
	// RpAtomic.renderCallBack is at +0x48 (after object+0x00, repEntry+0x14, geometry+0x18,
	// boundingSphere+0x1C, worldBoundingSphere+0x2C, clump+0x3C, inClumpLink+0x40).
	// Use the SDK macro (rpworld.h:2745) which also handles null callback → default.
	((RpAtomic*)atomic)->renderCallBack = AtomicDefaultRenderCallBack;

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
	if (len < 0.001f) {
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
		((ForAllAtomics_t)0x748E90)(g->clump, cbRend, &firstAtomic);
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