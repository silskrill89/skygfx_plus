#pragma once
#include <stddef.h>	// offsetof — the layout contract below pins offsets with it

class CGeneral
{
public:
	static float GetATanOfXY(float x, float y);
};


class CEntity;
class CBaseModelInfo;
CBaseModelInfo *GetModelInfo(CEntity *e);

struct CVector
{
	float x, y, z;
	
	CVector() : x(0), y(0), z(0) {}
	CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
	
	CVector operator-(const CVector &v) const { return CVector(x - v.x, y - v.y, z - v.z); }
	CVector operator+(const CVector &v) const { return CVector(x + v.x, y + v.y, z + v.z); }
	CVector operator*(float s) const { return CVector(x * s, y * s, z * s); }
	
	float Magnitude() const { return sqrtf(x*x + y*y + z*z); }
};

struct CVector2D
{
	float x, y;
};

struct CMatrix
{
	RwMatrix matrix;
	RwMatrix *pMatrix;
	int haveRwMatrix;
};


struct CBoundingBox
{
	CVector min;
	CVector max;
};

struct CBoundingSphere
{
	CVector center;
	float radius;
};

struct CColModel
{
	CBoundingBox box;
	CBoundingSphere sphere;
	char flags;
	char allocationFlag;
	void *colData;
};

class CBaseModelInfo
{
public:
	void	*vtable;
	uint32	m_hashKey;
	uint16	m_numRefs;
	int16	m_texDict;
	uint8	m_alpha;
	uint8	m_numOf2dEffects;
	uint16	m_first2dEffect;
	uint16	m_dynamicIndex;
	uint16	m_flags;

	uint16	GetHasBeenPreRendered(void) { return m_flags & (1<<0); }
	uint16	GetDrawLast(void) { return m_flags & (1<<1); }
	uint16	GetDrawAdditive(void) { return m_flags & (1<<2); }
	uint16	GetDontWriteZ(void) { return m_flags & (1<<3); }
	uint16	GetNoShadows(void) { return m_flags & (1<<4); }
	uint16	GetIsLod(void) { return m_flags & (1<<5); }
	uint16	GetDoBackfaceCulling(void) { return m_flags & (1<<6); }
	uint16	GetOwnsColModel(void) { return m_flags & (1<<7); }

	CColModel	*pColModel;
	float	fLodDistanceUnscaled;
	RwObject	*pRwObject;
};
static_assert(sizeof(CBaseModelInfo) == 0x20, "Wrong size: CBaseModelInfo");


struct CSimpleTransform
{
	CVector pos;
	float angle;
};

struct CPlaceable
{
	void *vtable;
	CSimpleTransform placement;
	CMatrix *m_pCoords;
	CVector GetPosition(void){
		if(m_pCoords)
			return *(CVector*)&m_pCoords->matrix.pos;
		else
			return placement.pos;
	}
};

class CEntity : public CPlaceable
{
public:
	RwObject *m_pRWObject;
	uint32 m_flags;
	uint16 RandomSeed;
	uint16 m_nModelIndex;
	void *pReferences;
	void *m_pLastRenderedLink;
	uint16 m_nScanCode;
	uint8 m_iplIndex;
	uint8 m_areaCode;
	CEntity *m_pLod;
	uint8 numLodChildren;
	uint8 numLodChildrenRendered;
	uint8 nType : 3;
	uint8 nStatus : 5;

	void GetBoundCentre(CVector *v);
	float GetBoundRadius(void) { return GetModelInfo(this)->pColModel->sphere.radius; }
	bool GetIsOnScreen(void);
	bool GetIsOnScreen_orig(void);
	bool IsEntityOccluded(void);
};
static_assert(sizeof(CEntity) == 0x38, "Wrong size: CEntity");

enum eEntityType
{

	ENTITY_TYPE_NOTHING,
	ENTITY_TYPE_BUILDING,
	ENTITY_TYPE_VEHICLE,
	ENTITY_TYPE_PED,
	ENTITY_TYPE_OBJECT,
	ENTITY_TYPE_DUMMY,
	ENTITY_TYPE_NOTINPOOLS
};

class CVisibilityPlugins
{
public:
	typedef void	(*VisibilityCallback)(void*, float);
	struct AlphaObjectInfo
	{
		void*	pObject;
		VisibilityCallback callback;
		float	fCompareValue;
	};

	static CLinkList<AlphaObjectInfo> &m_alphaList;
	static CLinkList<AlphaObjectInfo> &m_alphaBoatAtomicList;
	static CLinkList<AlphaObjectInfo> &m_alphaEntityList;
	static CLinkList<AlphaObjectInfo> &m_alphaUnderwaterEntityList;
	static CLinkList<AlphaObjectInfo> &m_alphaReallyDrawLastList;

	static void RenderFadingBuildings(void);
	static uint16 CVisibilityPlugins::GetUserValue(RpAtomic *atm);
};

struct CCamera : CPlaceable
{
};

CPlaceable *FindPlayerPed(int);

extern float &CTimer__ms_fTimeStep;
extern CCamera &TheCamera;
extern float &CWeather__Rain;
extern float& CWeather__Wind;
extern CVector& CWeather__WindDir;
extern float &CWeather__UnderWaterness;
extern bool &CCutsceneMgr__ms_running;
extern bool &CMenuManager__m_bMenuActive;
extern int &CMenuManager__m_PrefsBrightness;
extern int* CGame__currArea;
extern int* CEntryExitManager__ms_exitEnterState;
// Game state + VideoPlayer's DirectShow media control (named layer; addresses
// in gta.cpp). Used by the WndProc WM_ACTIVATEAPP guard — vanilla GTA:SA
// dereferences GetMediaControl() during the logo/intro even when it is NULL.
extern int &gGameState;
extern void *&g_pvMediaControl;

struct CustomEnvMapPipeMaterialData
{
	int8 scaleX;
	int8 scaleY;
	int8 transScaleX;
	int8 transScaleY;
	int8 shininess;
	uint8 pad3;
	uint16 renderFrameCounter;
	RwTexture *texture;

	float GetScaleX(void) { return scaleX/8.0f; }
	float GetScaleY(void) { return scaleY/8.0f; }
	float GetTransScaleX(void) { return transScaleX/8.0f; }
	float GetTransScaleY(void) { return transScaleY/8.0f; }
	float GetShininess(void) { return shininess/255.0f; }
};

struct CustomEnvMapPipeAtomicData
{
	float trans;
	float posx;
	float posy;

	void *operator new(size_t size);
};
CustomEnvMapPipeAtomicData *CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData(RpAtomic *atomic);

struct CustomSpecMapPipeMaterialData
{
	float specularity;
	RwTexture *texture;
};

struct PsGlobalType
{
	HWND	window;
	DWORD	instance;
	DWORD	fullscreen;
	DWORD	lastMousePos_X;
	DWORD	lastMousePos_Y;
	DWORD	unk;
	DWORD	diInterface;
	DWORD	diMouse;
	void*	diDevice1;
	void*	diDevice2;
};

struct RsGlobalType
{
	DWORD			AppName;
	DWORD			MaximumWidth;
	DWORD			MaximumHeight;
	DWORD			frameLimit;
	DWORD			quit;
	PsGlobalType*	ps;
	void*			keyboard;
	void*			mouse;
	void*			pad;
};

struct GlobalScene
{
	RpWorld *world;
	RwCamera *camera;
};
extern GlobalScene &Scene;

struct Imf
{
	float zScreenNear;
	float recipZ;
	RwRaster *frontBuffer;
	int width;
	int height;
	float umin;
	float vmin;
	float umax;
	float vmax;
	RwD3D9Vertex triangle_verts[3];
	float tri_umin;
	float tri_umax;
	float tri_vmin;
	float tri_vmax;
	RwD3D9Vertex quad_verts[4];
};

void DefinedState(void);

double CTimeCycle_GetAmbientRed(void);
double CTimeCycle_GetAmbientGreen(void);
double CTimeCycle_GetAmbientBlue(void);

#define RwEngineInstance (*rwengine)
extern RsGlobalType *RsGlobal;
extern IDirect3DDevice9 *&d3d9device;
extern RpLight *&pAmbient;
extern RpLight *&pDirect;
extern RpLight **pExtraDirectionals;
extern int &NumExtraDirLightsInWorld;
extern D3DLIGHT9 &gCarEnvMapLight;


extern int16 &CClock__ms_nGameClockSeconds;
extern uint8 &CClock__ms_nGameClockMinutes;
extern uint8 &CClock__ms_nGameClockHours;
extern int16 &CWeather__OldWeatherType;
extern int16 &CWeather__NewWeatherType;
extern float &CWeather__InterpolationValue;
extern float &CWeather__CloudCoverage;
extern float &CWeather__Foggyness;

struct CColourSet {
	float ambientR, ambientG, ambientB;
	float ambientObjR, ambientObjG, ambientObjB;
	float directionalR, directionalG, directionalB;
	short skyTopR, skyTopG, skyTopB;
	short skyBotR, skyBotG, skyBotB;
	short sunCoreR, sunCoreG, sunCoreB;
	short sunCoronaR, sunCoronaG, sunCoronaB;
	float sunSize, spriteSize, spriteBrightness;
	short shadowStrength, lightShadowStrength, poleShadowStrength;
	float farClip, fogStart, lightsOnGroundBrightness;
	short lowCloudsR, lowCloudsG, lowCloudsB;
	short fluffyBottomR, fluffyBottomG, fluffyBottomB;
	float waterR, waterG, waterB, waterA;
	float postFx1R, postFx1G, postFx1B, postFx1A;
	float postFx2R, postFx2G, postFx2B, postFx2A;
	float cloudAlpha;
};
extern CColourSet &CTimeCycle__m_CurrentColours;
void GetSunDirection(float &sx, float &sy, float &sz);

extern void **rwengine;
extern RwInt32 &CCustomCarEnvMapPipeline__ms_envMapPluginOffset;
extern RwInt32 &CCustomCarEnvMapPipeline__ms_envMapAtmPluginOffset;
extern RwInt32 &CCustomCarEnvMapPipeline__ms_specularMapPluginOffset;
#define GETENVMAP(m) RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, m, CCustomCarEnvMapPipeline__ms_envMapPluginOffset)
#define GETENVMAPATM(m) RWPLUGINOFFSET(CustomEnvMapPipeAtomicData*, m, CCustomCarEnvMapPipeline__ms_envMapAtmPluginOffset)
#define GETSPECMAP(m) RWPLUGINOFFSET(CustomSpecMapPipeMaterialData*, m, CCustomCarEnvMapPipeline__ms_specularMapPluginOffset)

extern RwReal &CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
extern RwInt32 &CCustomBuildingDNPipeline__ms_extraVertColourPluginOffset;
extern RwReal &CCustomBuildingDNPipeline__m_fDNBalanceParam;

void CRenderer__RenderRoads(void);
void CRenderer__RenderEverythingBarRoads(void);
void CRenderer__RenderFadingInEntities(void);
void CRenderer__RenderFadingInUnderwaterEntities(void);
extern short &skyTopRed, &skyTopGreen, &skyTopBlue;
extern short &skyBotRed, &skyBotGreen, &skyBotBlue;

extern RwTexture *&gpWhiteTexture;

// ---------------------------------------------------------------------------
// Address-hygiene naming layer (hard rule, session m0269): raw game/SDK hex
// lives ONLY here and in gta.cpp; every consumer uses these names.
// ---------------------------------------------------------------------------

// RwCameraBeginUpdate / RwCameraEndUpdate "camera in progress" slot (0xC9BCC0).
// Set to the begun camera by RwCameraBeginUpdate, cleared to NULL by
// RwCameraEndUpdate (see postfx_smaa.cpp crash-fix save/restore; game code at
// 0x7F98DF dereferences [0xC9BCC0]+0x60 and faults on NULL). No stable public
// symbol (disassembly-verified 2026-11-10); typed void* to match every
// consumer's existing void* read/write.
extern void *&pCameraBeingUpdated;

// RwD3D9SetTexture raster-extension offset word (0xB4E9E0). The game derives a
// raster's D3D9 extension block as: ext = raster + this word; ext+0 =
// IDirect3DTexture9*, ext+8 = "valid" byte flag (may be stale). The game's
// producer is 0x7FDCD0; texdefs.cpp TexDefsGetD3DTexture carries the full
// guard chain.
extern uint32 &RwD3D9RasterExtOffset;

// ---- CPool<CPed> ped-pool contract (SA 1.0 US; plugin-sdk CPool.h / CPed.h:468) ----
// The game's ped pool object lives at 0xB74490. Layout:
//   +0x00 m_pSlots     — CPed* base of the array (m_aStorage)
//   +0x04 m_pSlotInfos — uint8* per-slot flags (m_aFlags; PED_SLOT_FREE_BIT = free)
//   +0x08 m_nNumSlots  — int32 capacity (m_nSize)
// Consumers walk it as: ped_i = m_pSlots + i * PED_POOL_STRIDE; skip when
// m_pSlotInfos[i] & PED_SLOT_FREE_BIT. (Same evidence as ragdoll_death.cpp.)
struct CPedPool
{
	void  *m_pSlots;       // +0x00
	uint8 *m_pSlotInfos;   // +0x04
	int    m_nNumSlots;    // +0x08
};
extern CPedPool *&pedPool;    // *(CPool<CPed>**)0xB74490

#define PED_POOL_STRIDE    0x7C4    // CPed array element stride (m_aStorage stride)
#define PED_SLOT_FREE_BIT  0x80     // m_aFlags free-slot bit


#define rpPDS_MAKEPIPEID(vendorID, pipeID)              \
    (((vendorID & 0xFFFF) << 16) | (pipeID & 0xFFFF))

enum RockstarPipelineIDs
{
	VEND_ROCKSTAR     = 0x0253F2,

	// PS2 pipes
	// building
	PDS_PS2_CustomBuilding_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x80),
	PDS_PS2_CustomBuilding_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x81),
	PDS_PS2_CustomBuildingDN_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x82),
	PDS_PS2_CustomBuildingDN_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x83),
	PDS_PS2_CustomBuildingEnvMap_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8C),
	PDS_PS2_CustomBuildingEnvMap_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8D),
	PDS_PS2_CustomBuildingDNEnvMap_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8E),
	PDS_PS2_CustomBuildingDNEnvMap_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8F),
	PDS_PS2_CustomBuildingUVA_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x90),
	PDS_PS2_CustomBuildingUVA_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x91),
	PDS_PS2_CustomBuildingDNUVA_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x92),
	PDS_PS2_CustomBuildingDNUVA_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x93),
	// car
	PDS_PS2_CustomCar_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x84),
	PDS_PS2_CustomCar_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x85),
	PDS_PS2_CustomCarEnvMap_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x86),
	PDS_PS2_CustomCarEnvMap_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x87),
//	PDS_PS2_CustomCarEnvMapUV2_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8A),	// this does not exist
	PDS_PS2_CustomCarEnvMapUV2_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x8B),
	// skin
	PDS_PS2_CustomSkinPed_AtmPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x88),
	PDS_PS2_CustomSkinPed_MatPipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x89),

	// PC pipes
	RSPIPE_PC_CustomBuilding_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x9C),
	RSPIPE_PC_CustomBuildingDN_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x98),
	RSPIPE_PC_CustomCarEnvMap_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x9A),	// same as XBOX!

	// Xbox pipes
	RSPIPE_XBOX_CustomBuilding_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x9E),
	RSPIPE_XBOX_CustomBuildingDN_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x96),
	RSPIPE_XBOX_CustomBuildingEnvMap_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0xA0),
	RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0xA2),
	RSPIPE_XBOX_CustomCarEnvMap_PipeID = rpPDS_MAKEPIPEID(VEND_ROCKSTAR, 0x9A),	// same as PC!
};

uint32 GetPipelineID(RpAtomic *atomic);
void SetPipelineID(RpAtomic *atomic, uint32 id);

// ============================================================================
// SA 1.0 US RenderWare layout contract — RpAtomic / RwFrame / RpClump
// (family-C fix pin; byte-verified 2026-11-02 against gta_sa.exe 1.0 US)
// ============================================================================
// WHY: the family-C fix (BuildGibClump / Dismember_Render in
// src/entities/sftbdyphsx.cpp) drives game memory through RAW BYTE OFFSETS.
// Those offsets are the fix's ABI with the game binary — this contract makes
// them explicit and pins them at compile time. Any later migration onto
// named accessors must keep these numbers (standing directive "unify": the
// named offsets below are the single source of truth for the raw writers).
//
// Byte evidence (gta_sa.exe 1.0 US, PE .text VA 0x401000 / raw 0x400,
// file offset = VA - 0x400C00):
//   RpAtomicCreate 0x749C50             stores AtomicDefaultRenderCallBack
//                                       (0x7491C0) at atomic+0x48; the
//                                       frame-sync store 0x749D20 writes
//                                       atomic+0x10 (RwObjectHasFrame.sync)
//   clump render walk 0x749B20          reads the frame at [atomic+0x04],
//                                       calls [atomic+0x48](atomic)
//   re-instancing path 0x74BF7A         calls [atomic+0x48](atomic)
//   _rwObjectHasFrameSetFrame 0x804EF0  writes the frame at atomic+0x04
//   RwFrame LTM flush 0x8097D0          rep movsd frame+0x10 -> frame+0x50
//   RwFrameGetLTM 0x7F0990              returns frame+0x50
//   RwFrameUpdateObjects 0x7F0910       marks the frame root dirty (recompute)
//   RpClumpForAllAtomics 0x749B70       walks the clump atomicList at +0x08
//
// TRAPS (the family-C root causes):
//   * atomic+0x10 is RwObjectHasFrame.sync (the frame-sync callback) — the
//     old +0x10 renderCallBack write clobbered the sync path and rerouted
//     frame sync through the hooked 0x7491C0. The render callback lives at
//     +0x48 ONLY.
//   * frame+0x14 is matrix right.y/right.z (FLOATS) — there is NO matrix
//     pointer at +0x14. The modelling matrix is EMBEDDED at frame+0x10
//     (RwMatrix, 0x40 bytes) and the LTM is EMBEDDED at frame+0x50.
//   * 0x804EF0 (_rwObjectHasFrameSetFrame) sets an ATOMIC's frame. Calling
//     it on the CLUMP spliced the clump atomicList head (+0x08) into
//     frame->objectList (+0x90) — garbage atomics + dirty-list poison. The
//     clump-splice call stays REMOVED; wire atomics via RpClumpAddAtomic.
//
// Consumers today (raw-offset idiom, entities lane): sftbdyphsx.cpp
// BuildGibClump :1006 (renderCallBack +0x48), Dismember_Render :1181 (frame
// +0x04) / :1193 (modelling matrix +0x10). They duplicate these numbers
// inline; point those writes at the names below when that lane picks this up.

// Named offsets — the single source of truth for raw-offset consumers.
struct RpAtomicOffsets
{
	enum {
		objectHdr             = 0x00,	// type/subType/flags/privateFlags (4B)
		frame                 = 0x04,	// object.parent — RwFrame* (THE frame pointer)
		lFrame                = 0x08,	// rwLLLink next/prev — NOT the frame
		sync                  = 0x10,	// RwObjectHasFrame.sync — NOT a render cb!
		repEntry              = 0x14,
		geometry              = 0x18,	// RpGeometry*
		boundingSphere        = 0x1C,	// 0x10 bytes
		worldBoundingSphere   = 0x2C,	// 0x10 bytes
		clump                 = 0x3C,	// RpClump*
		inClumpLink           = 0x40,	// rwLLLink next/prev
		renderCallBack        = 0x48,	// THE render callback (called as +0x48(atomic))
		interpolator          = 0x4C,	// 0x18 bytes
		llWorldSectorsInAtomic = 0x64,	// rwLLLink next/prev
		pipeline              = 0x6C,
		size                  = 0x70,
	};
};

// Documented layout struct — byte-map overlay of the game's RpAtomic
// (POD, never instantiated; exists to pin the map at compile time).
// 32-bit target: every pointer below is 4 bytes (the SA exe is x86).
struct SAAtomicLayout
{
	uint8	objectHdr[4];		// +0x00 type/subType/flags/privateFlags
	void	*frame;				// +0x04 object.parent (RwFrame*)
	void	*lFrameNext;		// +0x08
	void	*lFramePrev;		// +0x0C
	void	*sync;				// +0x10 RwObjectHasFrame.sync (frame-sync cb)
	void	*repEntry;			// +0x14
	void	*geometry;			// +0x18 (RpGeometry*)
	uint8	boundingSphere[0x10];		// +0x1C
	uint8	worldBoundingSphere[0x10];	// +0x2C
	void	*clump;				// +0x3C (RpClump*)
	void	*inClumpLinkNext;	// +0x40
	void	*inClumpLinkPrev;	// +0x44
	void	*renderCallBack;	// +0x48 THE render callback
	uint8	interpolator[0x18];	// +0x4C
	void	*llWorldSectorsNext;// +0x64
	void	*llWorldSectorsPrev;// +0x68
	void	*pipeline;			// +0x6C
};

// RwFrame — partial map (only what the family-C fix touches is pinned).
struct SAFrameLayout
{
	uint8	 objectHdr[0x10];	// +0x00..0x0F
	RwMatrix modelling;			// +0x10 EMBEDDED RwMatrix (0x40) — no ptr at +0x14
	RwMatrix ltm;				// +0x50 EMBEDDED RwMatrix (0x40)
	void	*objectListNext;	// +0x90
	void	*objectListPrev;	// +0x94
};

// RpClump — partial map. The 0x804EF0 clump-splice bug wrote THIS head into
// frame->objectList (+0x90); never share frames between a clump and its
// atomics through _rwObjectHasFrameSetFrame (see TRAPS above).
struct SAClumpLayout
{
	uint8	objectHdr[0x08];	// +0x00..0x07
	void	*atomicListNext;	// +0x08
	void	*atomicListPrev;	// +0x0C
};

static_assert(sizeof(void *) == 4,
	"SA 1.0 US layout contract is x86-only (pointers must be 4 bytes)");
static_assert(sizeof(RwMatrix) == 0x40, "Wrong size: RwMatrix (layout contract)");

static_assert(sizeof(SAAtomicLayout) == 0x70, "Wrong size: SAAtomicLayout");
static_assert(offsetof(SAAtomicLayout, frame) == 0x04,
	"RpAtomic frame is at +0x04 (object.parent)");
static_assert(offsetof(SAAtomicLayout, lFrameNext) == 0x08,
	"RpAtomic lFrame is at +0x08 — NOT the frame");
static_assert(offsetof(SAAtomicLayout, sync) == 0x10,
	"RpAtomic +0x10 is RwObjectHasFrame.sync — never a render callback");
static_assert(offsetof(SAAtomicLayout, geometry) == 0x18,
	"RpAtomic geometry is at +0x18");
static_assert(offsetof(SAAtomicLayout, clump) == 0x3C,
	"RpAtomic clump is at +0x3C");
static_assert(offsetof(SAAtomicLayout, renderCallBack) == 0x48,
	"RpAtomic renderCallBack is at +0x48 (RpAtomicCreate 0x749C50 / walk 0x749B20)");
static_assert(offsetof(SAAtomicLayout, pipeline) == 0x6C,
	"RpAtomic pipeline is at +0x6C");

static_assert(offsetof(SAFrameLayout, modelling) == 0x10,
	"RwFrame modelling matrix is EMBEDDED at +0x10");
static_assert(offsetof(SAFrameLayout, ltm) == 0x50,
	"RwFrame LTM is EMBEDDED at +0x50 (flush 0x8097D0, RwFrameGetLTM 0x7F0990)");
static_assert(offsetof(SAFrameLayout, objectListNext) == 0x90,
	"RwFrame objectList is at +0x90");

static_assert(offsetof(SAClumpLayout, atomicListNext) == 0x08,
	"RpClump atomicList is at +0x08 (walk 0x749B70)");

// The named offsets must agree with the pinned overlay.
static_assert(RpAtomicOffsets::frame == offsetof(SAAtomicLayout, frame), "offset drift");
static_assert(RpAtomicOffsets::sync == offsetof(SAAtomicLayout, sync), "offset drift");
static_assert(RpAtomicOffsets::renderCallBack == offsetof(SAAtomicLayout, renderCallBack), "offset drift");
static_assert(RpAtomicOffsets::size == sizeof(SAAtomicLayout), "size drift");
