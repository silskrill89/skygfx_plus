// grasssys.h - consolidated PC plant island public API (merged 2026-10-27).
//
// Merged from THREE former headers, declarations verbatim:
//   - src/render/PC_PlantsMgr.h       (PPTriPlant, CPlantMgr types, CPLANT_* tuning)
//   - src/render/PC_GrassRenderer.h   (CGrassRenderer, CPPTriPlantBuffer)
//   - src/render/PC_GrassInstancer.h  (CGrassInstancer batching API)
//
// Section order is dependency-driven (PlantsMgr types are used by both
// other headers). Only #pragma once + the include block were deduped.
// The 3-line forwarding header PC_GrassInstancer.h was deleted 2026-11-02
// (M6); core/main.cpp includes grasssys.h directly. PC_PlantsMgr.h and
// PC_GrassRenderer.h are gone; their content is below.
//
// 2026-11-01 dead-code sweep (source-inventory §2): the never-wired
// CGrassRenderer / CPPTriPlantBuffer classes and the CGrassInstancer
// batching API (BeginFrame..StatDraws — zero callers) were removed.
// Remaining live surface: CGrassInstancer::IsEnabled/SetEnabled (the
// main.cpp grassInstancing gate), GrassResetShaderLatches (depthhook.cpp)
// and GrassEnforceDepthContract (main.cpp grassRenderCallback).

#pragma once

// ---- Includes (deduped union of the three headers) ----
#include "skygfx.h"
#include "ColData.h"			// ColData...
#include "PlantSurfPropMgr.h"	// CPLANT_SURF_PROP_PLANTDATA_NUM...

// ---- PLANTS MANAGER (was src/render/PC_PlantsMgr.h) ----
//
// PlantsMgr - management of plants seeded around camera;
//
//
//	30/07/2004	- JohnW:	- initial;
//	10/04/2005	- Andrzej:	- full port to GTA_PC/GTA_XBOX;
//
//
//
//




class CPlantColEntEntry;
class CPlantLocTri;
class CPlantMgr;
class CEntity;
class CAtomicModelInfo;

//
// stuff for VarConsole:
//
#ifndef MASTER
	extern bool gbShowCPlantMgrPolys;
	extern bool gbDisplayCPlantMgrInfo;
	extern bool gbPlantMgrActive;
#endif


//
//
// number of grass GroupModels with "big" textures:
// each group consist of 4 models + 1 "big" texture;
//
#define CPLANT_NUM_PLANT_SLOTS				(4)

//
// num of models per slot:
//
#define CPLANT_SLOT_NUM_MODELS				(4)

//
// num of textures per slot:
//
#define CPLANT_SLOT_NUM_TEXTURES			(4)




//
// extra last slot is used exlusively for procedural objects (Mark N):
//
//#define CPLANT_PROC_OBJ_SLOT				(4)



//
// "bigtexture" plant texture scale:
//
// no change for u:
#define CPLANT_BIGTEXTURE_U_SCALE			(1.0f)

// 1/16 for 64x1024 (16 textures, 64x64 each)
//#define CPLANT_BIGTEXTURE_V_SCALE			(1.0f/16.0f)
//
// 1/4  for 64x256  (4 textures, 64x64 each)
#define CPLANT_BIGTEXTURE_V_SCALE			(1.0f/4.0f)




//
//
// distance where TriLocs are considered to be used as base for plant sectors:
//
#define CPLANT_TRILOC_FAR_DIST				(100.0f)
#define CPLANT_TRILOC_FAR_DIST_SQR			(CPLANT_TRILOC_FAR_DIST*CPLANT_TRILOC_FAR_DIST)


#define CPLANT_ALPHA_CLOSE_DIST				(3.0f)			//(5.0f)		//(20.0f)		// fade start range
#define CPLANT_ALPHA_FAR_DIST				(60.0f)			//(100.0f)		// fade stop range



//
//
// min distance, where CEntity will be added to our internal Entity "cache":
//
#define CPLANT_COL_ENTITY_DIST			(220.0f + 120.0f)
//(220.0f + 60.0f)
#define CPLANT_COL_ENTITY_DIST_SQR		(CPLANT_COL_ENTITY_DIST*CPLANT_COL_ENTITY_DIST)



//
//
// number of entities hold in our internal CEntity cache:
//
#define CPLANT_COL_ENTITY_CACHE_SIZE	(32+8)



//
//
// update cache every 32th frame (must be power-of-2):
//
#define CPLANT_COL_ENTITY_UPDATE_CACHE	(32)


//
//
//
//
struct PPTriPlant
{
public:
    CVector		V1, V2, V3;			// 3 vertices
	CVector		center;				// center of the plant triangle		

    uint16		model_id;			// model_id used to calculate offset address for model in VU mem
	uint16		num_plants;			// num plants to generate

    RwV2d		scale;				// x=SxSy, y=Sz
	//uint16		texture_id;		// number of texture to use
	RwTexture	*texture_ptr;		// ptr to texture to use


    RwRGBA		color;				// color of the model
    
    uint8		intensity;			// scale value for the colour
    uint8		intensity_var;		// variation of intensity value
    
    float		seed;				// seed starting value for this triangle;
    float		scale_var_xy;		// scale variation XY
    float		scale_var_z;		// scale variation Z

	float		wind_bend_scale;	// wind bending scale
	float		wind_bend_var;		// wind bending variation
};



//
//
//
//
class CPlantColEntEntry
{
	friend class CPlantMgr;

public:
	CPlantColEntEntry()		{ }
	~CPlantColEntEntry()	{ }


public:
	CPlantColEntEntry*	AddEntry(CEntity *pEntity);
	void				ReleaseEntry();


public:
	CEntity				*m_pEntity;			// pointer to CEntity
	CPlantLocTri		**m_LocTriArray;	// array of size [pEntity->GetColModel().m_nNoOfTriangles],
											// it holds pointers to CPlantLocTri for every CColTriangle in CEntity's ColModel;
	uint16				m_nNumTris;			// size of above array


private:
	CPlantColEntEntry	*m_pNextEntry;
	CPlantColEntEntry	*m_pPrevEntry;
	
};





//
//
// max number of triangles kept in loc triangle cache:
//
#define CPLANT_MAX_CACHE_LOC_TRIS_NUM			(256)

//
// process every 8th TriLoc (must be power-of-2):
//
#define CPLANT_ENTRY_TRILOC_PROCESS_UPDATE		(8)

//
// unique mask value to mark case, when we want all LocTris to be processed:
//
#define CPLANT_ENTRY_TRILOC_PROCESS_ALWAYS		(0xFAFAFAFA)





//
// 
// "Plant Location" triangle:
// it's generated from ColModel data;
//
class CPlantLocTri
{
	friend class CPlantMgr;

public:
	CPlantLocTri()		{ }
	~CPlantLocTri()		{ }


public:
	CPlantLocTri*	Add(const RwV3d& v1, const RwV3d& v2, const RwV3d& v3, uint8 nSurfaceType, uint8 nLighting, bool8 createsPlants, bool8 createsObjects);
	void			Release();



public:
	CVector			m_V1, m_V2, m_V3;									// 3 vertices of triangle
	CVector			m_Center;
	float			m_SphereRadius;										// radius of the sphere containing triangle
	
	float			m_Seed			[CPLANT_SURF_PROP_PLANTDATA_NUM];	// constant seed value for the triangle
	uint16			m_nMaxNumPlants	[CPLANT_SURF_PROP_PLANTDATA_NUM];	// max number of plants to generate for this TriLoc
	
	uint8			m_nSurfaceType;										// copied from CColTriangle;
	uint8			m_nLighting;										// lighting (calculated from CColTriangle);

//	bool8			m_IsUsedByProcObj;

	bool8			m_createsPlants		: 1;
	bool8			m_createsObjects	: 1;
	bool8			m_createdObjects	: 1;
	uint8			m_pad				: 5;

private:
	CPlantLocTri	*m_pNextTri;
	CPlantLocTri	*m_pPrevTri;
};







//
//
//
//
class CPlantMgr
{
	friend class	CPlantLocTri;
	friend class	CPlantColEntEntry;



public:
	CPlantMgr();
	~CPlantMgr();

public:
	static bool8	Initialise();
	static void		Shutdown();

	static bool8	ReloadConfig();

public:
	static bool8	Update(const CVector& camPos);
	static bool8	PreUpdateOnceForNewCameraPos(const CVector& newCamPos);

	static void		Render();

	static void		UpdateAmbientColor();


private:
	static float	CalculateWindBending();


public:
	static bool8 	SetPlantFriendlyFlagInAtomicMI(CAtomicModelInfo *pModelInfo);



private:
	static bool8				_ColEntityCache_Update(const CVector& camPos, bool8 bQuickUpdate=FALSE);
	static CPlantColEntEntry*	_ColEntityCache_FindInCache(CEntity *pEntity);
	static CPlantColEntEntry*	_ColEntityCache_Add(CEntity *pEntity, bool8 bCheckCacheFirst=FALSE);
	static void					_ColEntityCache_Remove(CEntity *pEntity);
	
private:
	static bool8				_ProcessEntryCollisionData(CPlantColEntEntry *pEntry);

	static bool8				_UpdateLocTris(const CVector &camPos, int32 iTriProcessSkipMask);


	static bool8				_ProcessEntryCollisionData(CPlantColEntEntry *pEntry, const CVector& camPos, int32 iTriProcessSkipMask);


	static bool8				_ProcessEntryCollisionDataSections(CPlantColEntEntry *pEntry, const CVector& camPos, int32 iTriProcessSkipMask);
	static void					_ProcessEntryCollisionDataSections_AddLocTris(CPlantColEntEntry *pEntry, const CVector& camPos, int32 iTriProcessSkipMask,
														int32 colStartIndex, int32 colEndIndex);
	static void					_ProcessEntryCollisionDataSections_RemoveLocTris(CPlantColEntEntry *pEntry, const CVector& camPos, int32 iTriProcessSkipMask,
															int32 colStartIndex, int32 colEndIndex);


private:
	//
	// particular debug stuff:
	//
	static bool8				DbgRenderLocTris();
	static bool8				DbgRenderCachedEntities(uint32 *pCountAll=NULL);

	static bool8				DbgCountLocTrisAndPlants(uint32 groupID, uint32 *pCountLocTris=NULL, uint32 *pCountPlants=NULL);
	static bool8				DbgCountCachedEntities(uint32 *pCountAll=NULL);
	


	//
	// CPlantColEntityEntry:
	//
private:
	static CPlantColEntEntry*	MoveColEntToList(CPlantColEntEntry **ppCurrentList, CPlantColEntEntry **ppNewList, CPlantColEntEntry *pEntity);

private:
	static CPlantColEntEntry	*m_UnusedColEntListHead;
	static CPlantColEntEntry	*m_CloseColEntListHead;
	static CPlantColEntEntry	m_ColEntCacheTab[CPLANT_COL_ENTITY_CACHE_SIZE];


	//
	// CPlantLocTri:
	//
private:
	static CPlantLocTri* MoveLocTriToList(CPlantLocTri **ppCurrentList, CPlantLocTri **ppNewList, CPlantLocTri *pTri);

private:
	static CPlantLocTri	*m_UnusedLocTriListHead;
	static CPlantLocTri	*m_CloseLocTriListHead[CPLANT_NUM_PLANT_SLOTS];	// each grass GroupModel has its own TriLoc close list;
	static CPlantLocTri	m_LocTrisTab[CPLANT_MAX_CACHE_LOC_TRIS_NUM];



	//
	// stuff used to skip unnecessary PlantModelsSet upload many times during one frame: 
	//
private:
	static void		AdvanceCurrentScanCode()			{ m_scanCode++;			}
	static uint16	GetCurrentScanCode()				{ return(m_scanCode);	}


private:
	static uint16	m_scanCode;
	static RwRGBA	m_AmbientColor;
	


public:
	static RwTexture*					PC_PlantTextureTab0[CPLANT_SLOT_NUM_TEXTURES];
	static RwTexture*					PC_PlantTextureTab1[CPLANT_SLOT_NUM_TEXTURES];
	static RwTexture*					PC_PlantTextureTab2[CPLANT_SLOT_NUM_TEXTURES];
	static RwTexture*					PC_PlantTextureTab3[CPLANT_SLOT_NUM_TEXTURES];

	static RwTexture**					PC_PlantSlotTextureTab[CPLANT_NUM_PLANT_SLOTS];

public:
	static RpAtomic*					PC_PlantModelsTab0[CPLANT_SLOT_NUM_MODELS];
	static RpAtomic*					PC_PlantModelsTab1[CPLANT_SLOT_NUM_MODELS];
	static RpAtomic*					PC_PlantModelsTab2[CPLANT_SLOT_NUM_MODELS];
	static RpAtomic*					PC_PlantModelsTab3[CPLANT_SLOT_NUM_MODELS];
	
	static RpAtomic**					PC_PlantModelSlotTab[CPLANT_NUM_PLANT_SLOTS];

};
// ---- GRASS INSTANCER (was src/render/PC_GrassInstancer.h) ----
//
// TASK 2 — double-render safety gate. DEFAULTS OFF.
//
// Single source of truth for two things that must agree:
//   1. main.cpp (DEFERRED/staged diff — not applied this burst): the thunk
//      at the CPlantMgr::Render call site 0x53E103 forwards to our island
//      ONLY when IsEnabled(); otherwise it calls the game's original.
//   2. PC_PlantsMgr.cpp CPlantMgr::Render(): early-returns unless enabled.
// While the gate is OFF the game's own CPlantMgr::Render (0x5DBAE0) keeps
// drawing and ours never runs → half-wired state can neither double-render
// nor blank the grass.
//
// (2026-11-01: the batching API this gate once controlled — BeginFrame/
// EndFrame/BeginBatch/EndBatch/AddInstance/DrawTriPlant/ShaderReady/
// StatInstances/StatDraws — was removed with the dead draw path;
// source-inventory §2. Only the gate pair remains.)
//
class CGrassInstancer
{
public:
	static bool		IsEnabled(void);
	static void		SetEnabled(bool enable);	// only the staged install path calls this
};

//
// Device-Reset shader-latch resetter (L11; called by depthhook.cpp after
// Pipeline_InvalidateShaders()/CreateShaders()). Two duties:
//   1. Clears the GrassInstanced.ps lazy-load cache (handle + load-tried
//      latch) so the next grass draw re-creates the PS for the live device.
//      Fail-open: a failed reload keeps the fixed-function Im3D fallback
//      path unchanged.
//   2. S6 (test-4): recreates the LIVE plant mod PS grassPixelShader
//      (main.cpp:121, created once by CPlantMgr_Initialise) against the new
//      device generation — it is covered by neither
//      Pipeline_InvalidateShaders/CreateShaders (makePS/makeVS cache) nor
//      any lazy latch, so post-Reset it is a dead handle whose silent bind
//      failure leaves the previous draw's shader bound (black plant pass).
//      Fail-open: a failed recreate leaves NULL => FFP plant draw (visible).
//
void GrassResetShaderLatches(void);

//
// §4.1 vegetation draw-order fix (2026-10-30): enforce the world depth
// baseline (Z-test ON, ZFUNC LESSEQUAL) at the plant draw, repaired through
// all three state layers (rw cache / RW driver cache / raw device) when the
// device has drifted behind the rw cache (measured class: dev ZENABLE=0 /
// ZFUNC=ALWAYS — the vanilla rw-cache-only ZTEST restore then no-ops and the
// plant pass draws through walls / over peds). Ground truth is the raw device
// GetRenderState; healthy frames cost two Gets and no writes. ZWRITE, alpha
// test and cull mode are deliberately NOT touched (owned by the dual-pass
// legs / caller cutout state / grassRenderCallback's own cull patch).
//
// HOT-PATH GATE (2026-10-31): the caller (grassRenderCallback) is per-atomic,
// but the whole check (Z + stage-0 texture contract) runs at most ONCE per RW
// render frame (RWSRCGLOBAL(renderFrame) gate, softbody idiom) — drift
// sources are frame-boundary passes, so the first plant atomic of the frame
// checks+repairs for the entire pass. Device calls per frame: 2, regardless
// of plant-atomic count.
//
// S6 (test-4) extension: the same helper also enforces the stage-0 TEXTURE
// contract — the plant pass is the one world-geometry draw whose diffuse
// reaches device stage 0 through the game's own RW bind (L2-only,
// change-gated), so a leaked screen raster / NULL left on the device behind
// RW's cache makes that bind a cache-hit no-op => pure-black plant
// silhouettes (the bldBindDiffuse3L class, buildingPipe.cpp:1152). Repair is
// gated on the rw cache already naming the target raster AND the raw device
// readback disagreeing; then the intended plant texture (*tmptexture
// @0xC02DC0) is rebound through L1 (park-and-set) / L2 / L3. Binding-only:
// state-identical when healthy (one rw Get + one device Get, no writes).
//
// Call site: main.cpp grassRenderCallback (per-atomic plant render callback,
// attached by CPlantMgr_Initialise @0x5BF8EA) — one line at function top.
// See grasssys.cpp GrassEnforceDepthContract for the full root-cause block.
//
void GrassEnforceDepthContract(void);
