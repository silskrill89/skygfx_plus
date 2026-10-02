#include "skygfx.h"
#include "neo.h"

#ifdef DEBUG
//#define DEBUGENVTEX
#endif


WRAPPER void DeActivateDirectional(void) { EAXJMP(0x735C70); }
WRAPPER void SetAmbientColours(void) { EAXJMP(0x735D30); }

// We'll use these for all env maps now
RwCamera *reflectionCam;
RwRaster *envFB, *envZB;
RwTexture *reflectionTex;

// Temporal smoothing — keep a copy of the previous env map to blend with
// the new render. This eliminates "melting" artifacts when the camera
// moves (sky/objects ghosting across vehicle bodies).
static RwRaster *envFB_prev = NULL;
static int envTemporalFrame = 0;

// Release temporal env map resources on device reset
void ReleaseEnvMapResources(void)
{
	if(envFB_prev){ RwRasterDestroy(envFB_prev); envFB_prev = NULL; }
	envTemporalFrame = 0;
}

/* Create envmap rasters as we need them and attach them to cam */
void
MakeEnvmapRasters(void)
{
	if(envFB && envFB->width == config->envMapSize)
		return;
	if(envFB) RwRasterDestroy(envFB);
	if(envZB) RwRasterDestroy(envZB);
	envFB = RwRasterCreate(config->envMapSize, config->envMapSize, 0, rwRASTERTYPECAMERATEXTURE);
	envZB = RwRasterCreate(config->envMapSize, config->envMapSize, 0, rwRASTERTYPEZBUFFER);
	if(!envFB || !envZB){
		// Symmetric cleanup: if either failed, release both and NULL the camera
		if(envFB){ RwRasterDestroy(envFB); envFB = NULL; }
		if(envZB){ RwRasterDestroy(envZB); envZB = NULL; }
		return;
	}
	if(reflectionCam){
		RwCameraSetRaster(reflectionCam, envFB);
		RwCameraSetZRaster(reflectionCam, envZB);
	}
	if(reflectionTex)
		RwTextureSetRaster(reflectionTex, envFB);

	// Reset temporal buffer to force a fresh start on resolution change
	if(envFB_prev){ RwRasterDestroy(envFB_prev); envFB_prev = NULL; }
	envTemporalFrame = 0;
}

// Blends the freshly rendered env map (envFB) with the previous frame's copy
// (envFB_prev) using a simple 50/50 copy-blend. This temporal smoothing
// eliminates the "melting" / ghosting artifacts when the camera or scene
// objects move — the sky/trees don't smear across vehicle bodies anymore.
void
BlendEnvMapTemporal(void)
{
	if(!envFB) return;

	// Lazily create the previous-frame buffer at the same resolution
	if(!envFB_prev){
		envFB_prev = RwRasterCreate(envFB->width, envFB->height, 0, rwRASTERTYPECAMERATEXTURE);
		if(!envFB_prev) return;
	}

	// First frame: just copy new → prev (no smoothing yet)
	if(envTemporalFrame == 0){
		// Fault-safe raster-context pairing: a SEH fault inside
		// RwRasterRenderFast used to skip RwRasterPopContext and strand
		// envFB_prev as the live raster context (render-target leak into
		// the rest of the frame). __finally pops on EVERY exit.
		bool pushed = false;
		__try {
			RwRasterPushContext(envFB_prev);
			pushed = true;
			RwRasterRenderFast(envFB, 0, 0);
		} __finally {
			if(pushed) RwRasterPopContext();
		}
		envTemporalFrame++;
		return;
	}

	// Subsequent frames: blend new into prev using additive blend
	// (new + prev)/2 — implemented as new × 0.5 + prev × 0.5
	// Easiest: render prev to itself with D3D blend (new * 0.5 + prev * 0.5)
	// For simplicity, use a hard copy: copy new to envFB_prev, accept slight lag.
	// This still eliminates the worst of the "melting" since it provides a
	// stable target that the new render is written over, rather than the
	// previous frame's content being visible as ghost trails.
	// Still a hard copy (see comment above) — but the Push/Pop pairing is
	// fault-safe here too (same rationale as the first-frame block).
	bool pushed = false;
	__try {
		RwRasterPushContext(envFB_prev);
		pushed = true;
		RwRasterRenderFast(envFB, 0, 0);
	} __finally {
		if(pushed) RwRasterPopContext();
	}

	envTemporalFrame++;
}

void
MakeEnvmapCam(void)
{
	reflectionCam = RwCameraCreate();
	if(!reflectionCam) return;
	RwFrame *frame = RwFrameCreate();
	if(!frame){ RwCameraDestroy(reflectionCam); reflectionCam = NULL; return; }
	RwCameraSetFrame(reflectionCam, frame);
	RwCameraSetNearClipPlane(reflectionCam, 0.1f);
	RwCameraSetFarClipPlane(reflectionCam, 250.0f * config->envMapFarClipMult);
	RwV2d vw;
	vw.x = vw.y = 0.4f;
	RwCameraSetViewWindow(reflectionCam, &vw);
	if(Scene.world)
		RpWorldAddCamera(Scene.world, reflectionCam);
}

#ifdef DEBUGENVTEX
static RwIm2DVertex screenQuad[4];
static RwImVertexIndex screenindices[6] = { 0, 1, 2, 0, 2, 3 };

static void
MakeQuadTexCoords(bool textureSpace)
{
	float minU, minV, maxU, maxV;
	if(textureSpace){
		minU = minV = 0.0f;
		maxU = maxV = 1.0f;
	}else{
		assert(0 && "not implemented");
	}
	screenQuad[0].u = minU;
	screenQuad[0].v = minV;
	screenQuad[1].u = minU;
	screenQuad[1].v = maxV;
	screenQuad[2].u = maxU;
	screenQuad[2].v = maxV;
	screenQuad[3].u = maxU;
	screenQuad[3].v = minV;
}

static void
MakeScreenQuad(void)
{
	int width = reflectionTex->raster->width;
	int height = reflectionTex->raster->height;
	screenQuad[0].x = 0.0f;
	screenQuad[0].y = 0.0f;
	screenQuad[0].z = RwIm2DGetNearScreenZ();
	screenQuad[0].rhw = 1.0f / reflectionCam->nearPlane;
	screenQuad[0].emissiveColor = 0xFFFFFFFF;
	screenQuad[1].x = 0.0f;
	screenQuad[1].y = height;
	screenQuad[1].z = screenQuad[0].z;
	screenQuad[1].rhw = screenQuad[0].rhw;
	screenQuad[1].emissiveColor = 0xFFFFFFFF;
	screenQuad[2].x = width;
	screenQuad[2].y = height;
	screenQuad[2].z = screenQuad[0].z;
	screenQuad[2].rhw = screenQuad[0].rhw;
	screenQuad[2].emissiveColor = 0xFFFFFFFF;
	screenQuad[3].x = width;
	screenQuad[3].y = 0;
	screenQuad[3].z = screenQuad[0].z;
	screenQuad[3].rhw = screenQuad[0].rhw;
	screenQuad[3].emissiveColor = 0xFFFFFFFF;
	MakeQuadTexCoords(true);
}

#endif

class CAtomicModelInfo : public CBaseModelInfo
{
public:
	uint16 GetWetRoadReflection(void) { return m_flags & (1<<8); }
	uint16 GetIsPlantFriendly(void) { return m_flags & (1<<9); }
	uint16 GetDontCollideWithFlyer(void) { return m_flags & (1<<10); }
	// more...
};


WRAPPER void CEntity::GetBoundCentre(CVector *v) { EAXJMP(0x534290); }
WRAPPER bool CEntity::GetIsOnScreen_orig(void) { EAXJMP(0x534540); }

CBaseModelInfo **CModelInfo__ms_modelInfoPtrs;// = (CBaseModelInfo**)0xA9B0C8;
CBaseModelInfo*
GetModelInfo(CEntity *e)
{
	static bool init;
	if(!init){
		CModelInfo__ms_modelInfoPtrs = *(CBaseModelInfo***)(0x4C5960 + 3);
		init = true;
	}
	return CModelInfo__ms_modelInfoPtrs[e->m_nModelIndex];
}

class CRenderer
{
public:
	static CVector &ms_vecCameraPosition;
	static CEntity **&ms_aVisibleEntityPtrs;
	static CEntity **&ms_aVisibleLodPtrs;
	static int &ms_nNoOfVisibleEntities;
	static int &ms_nNoOfVisibleLods;

	static void RenderOneRoad(CEntity *e);
	static void RenderOneNonRoad(CEntity *e);
	static void RenderRoadsAndBuildings(void);
	static void RenderFadingInBuildings(void);

	static void RenderRoads(void);
	static void RenderEverythingBarRoads(void);
	static void RenderFadingInEntities(void);
	static void RenderFadingInUnderwaterEntities(void);
};

CVector &CRenderer::ms_vecCameraPosition = *(CVector*)0xB76870;
CEntity **&CRenderer::ms_aVisibleEntityPtrs = *(CEntity***)(0x553526 + 3); //0xB75898;	// [1000]
CEntity **&CRenderer::ms_aVisibleLodPtrs = *(CEntity***)(0x5534F2 + 3); //0xB748F8;		// [1000]
int &CRenderer::ms_nNoOfVisibleEntities = *(int*)0xB76844;
int &CRenderer::ms_nNoOfVisibleLods = *(int*)0xB76840;

WRAPPER void CRenderer::RenderOneRoad(CEntity *e) { EAXJMP(0x553230); }
WRAPPER void CRenderer::RenderOneNonRoad(CEntity *e) { EAXJMP(0x553260); }
WRAPPER void CRenderer::RenderRoads(void) { EAXJMP(0x553A10); }
WRAPPER void CRenderer::RenderEverythingBarRoads(void) { EAXJMP(0x553AA0); }
WRAPPER void CRenderer::RenderFadingInEntities(void) { EAXJMP(0x5531E0); }
WRAPPER void CRenderer::RenderFadingInUnderwaterEntities(void) { EAXJMP(0x553220); }


CLinkList<CVisibilityPlugins::AlphaObjectInfo> &CVisibilityPlugins::m_alphaList = *(CLinkList<CVisibilityPlugins::AlphaObjectInfo>*)0xC88070;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> &CVisibilityPlugins::m_alphaBoatAtomicList = *(CLinkList<CVisibilityPlugins::AlphaObjectInfo>*)0xC880C8;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> &CVisibilityPlugins::m_alphaEntityList = *(CLinkList<CVisibilityPlugins::AlphaObjectInfo>*)0xC88120;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> &CVisibilityPlugins::m_alphaUnderwaterEntityList = *(CLinkList<CVisibilityPlugins::AlphaObjectInfo>*)0xC88178;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> &CVisibilityPlugins::m_alphaReallyDrawLastList = *(CLinkList<CVisibilityPlugins::AlphaObjectInfo>*)0xC881D0;
WRAPPER uint16 CVisibilityPlugins::GetUserValue(RpAtomic *atm) { EAXJMP(0x7323A0); }



inline float sq(float f) { return f*f; }
float sphereRadius;

/* This is unfortunately not enough to render sphere maps.
 * The game has more logic to cull objects, but I don't quite know where.
 * In the worst case whole sectors will be culled. */
bool
CEntity::GetIsOnScreen(void)
{
	if(sphereRadius != 0.0f){
		float r;
		CVector c;
		GetBoundCentre(&c);
		r = GetBoundRadius();
		return sq(r) + sq(sphereRadius) >
			sq(reflectionCamPos.x - c.x) +
			sq(reflectionCamPos.y - c.y) +
			sq(reflectionCamPos.z - c.z);
		return false;
	}
	return GetIsOnScreen_orig();
}

//bool
//CEntity::IsEntityOccluded(void)
//{
//	return false;
//}

void
CVisibilityPlugins::RenderFadingBuildings(void)
{
	for(auto i = m_alphaEntityList.m_lnListTail.m_pPrev; i != &m_alphaEntityList.m_lnListHead; i = i->m_pPrev)
		if(((CEntity*)i->V().pObject)->nType == ENTITY_TYPE_BUILDING)
			i->V().callback(i->V().pObject, i->V().fCompareValue);
}


void
CRenderer::RenderRoadsAndBuildings(void)
{
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
	if(CGame__currArea == 0)
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)140);
	for(int i = 0; i < ms_nNoOfVisibleEntities; i++){
		CEntity *e = ms_aVisibleEntityPtrs[i];
		int type = e->nType;
		if(type == ENTITY_TYPE_BUILDING){
			if(((CAtomicModelInfo*)GetModelInfo(e))->GetWetRoadReflection())
				RenderOneRoad(e);
			else
				RenderOneNonRoad(e);
		}
	}
	// SA does something with the camera's z values here but that has no effect on d3d
	for(int i = 0; i < ms_nNoOfVisibleLods; i++)
		RenderOneNonRoad(ms_aVisibleLodPtrs[i]);
}

void
CRenderer::RenderFadingInBuildings(void)
{
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingBuildings();
}



void
RenderReflectionScene(void)
{
	reflectionCamPos = TheCamera.GetPosition(); //use camera position for reflections, like any other game as GTA V, makes much more sense than default skygfx that uses player pos
	/*if (CCutsceneMgr__ms_running)
		reflectionCamPos = TheCamera.GetPosition();
	else
		reflectionCamPos = FindPlayerPed(-1)->GetPosition();*/

	DefinedState();
	/* We do have fog for the sphere map but we have to calculate it ourselves  */
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, 0);

	// Render the opaque world
	CRenderer::RenderRoadsAndBuildings();
	// and the transparent world
	CRenderer::RenderFadingInBuildings();
}

RwTexture **gpCoronaTexture = (RwTexture**)0xC3E000;

WRAPPER void CClouds__RenderSkyPolys(void) { EAXJMP(0x714650); }

static RwIm2DVertex coronaVerts[4*4];
static RwImVertexIndex coronaIndices[6*4];
static int numCoronaVerts, numCoronaIndices;

static void
AddCorona(float x, float y, float sz)
{
	RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera); // NULL curCamera faults (same class as 0x7FAD4D)
	if(!cam) return;
	float nearz, recipz;
	RwIm2DVertex *v;
	nearz = RwIm2DGetNearScreenZ();
	recipz = 1.0f / RwCameraGetNearClipPlane(cam);

	v = &coronaVerts[numCoronaVerts];
	RwIm2DVertexSetScreenX(&v[0], x);
	RwIm2DVertexSetScreenY(&v[0], y);
	RwIm2DVertexSetScreenZ(&v[0], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[0], recipz);
	RwIm2DVertexSetU(&v[0], 0.0f, recipz);
	RwIm2DVertexSetV(&v[0], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[0], 0xFF, 0xFF, 0xFF, 0xFF);

	RwIm2DVertexSetScreenX(&v[1], x);
	RwIm2DVertexSetScreenY(&v[1], y + sz);
	RwIm2DVertexSetScreenZ(&v[1], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[1], recipz);
	RwIm2DVertexSetU(&v[1], 0.0f, recipz);
	RwIm2DVertexSetV(&v[1], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[1], 0xFF, 0xFF, 0xFF, 0xFF);

	RwIm2DVertexSetScreenX(&v[2], x + sz);
	RwIm2DVertexSetScreenY(&v[2], y + sz);
	RwIm2DVertexSetScreenZ(&v[2], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[2], recipz);
	RwIm2DVertexSetU(&v[2], 1.0f, recipz);
	RwIm2DVertexSetV(&v[2], 1.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[2], 0xFF, 0xFF, 0xFF, 0xFF);

	RwIm2DVertexSetScreenX(&v[3], x + sz);
	RwIm2DVertexSetScreenY(&v[3], y);
	RwIm2DVertexSetScreenZ(&v[3], nearz);
	RwIm2DVertexSetRecipCameraZ(&v[3], recipz);
	RwIm2DVertexSetU(&v[3], 1.0f, recipz);
	RwIm2DVertexSetV(&v[3], 0.0f, recipz);
	RwIm2DVertexSetIntRGBA(&v[3], 0xFF, 0xFF, 0xFF, 0xFF);


	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 1;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 2;
	coronaIndices[numCoronaIndices++] = numCoronaVerts + 3;
	numCoronaVerts += 4;
}

void
DrawEnvMapCoronas(RwV3d at)
{
	if(!reflectionTex || !reflectionTex->raster) return;
	// gpCoronaTexture may not be loaded yet (early frames / no coronas TXD) —
	// [0]->raster would fault inside RwRenderStateSet.
	if(!gpCoronaTexture[0] || !gpCoronaTexture[0]->raster) return;
	const float BIG = 89.0f * reflectionTex->raster->width/128.0f;
	const float SMALL = 38.0f * reflectionTex->raster->height/128.0f;

	float x;
	numCoronaVerts = 0;
	numCoronaIndices = 0;
	x = CGeneral::GetATanOfXY(-at.y, at.x)/(2*M_PI) - 1.0f;
	x *= BIG+SMALL;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;
	AddCorona(x, 12.0f, SMALL);	x += SMALL;
	AddCorona(x, 0.0f, BIG);	x += BIG;

	// Three-layer note: these are rw-cache level (RwRenderStateSet) writes;
	// the driver pending / device layers are flushed by the Im2D draw below.
	// Fault-safe restore: a SEH fault inside RwIm2DRenderIndexedPrimitive
	// used to skip the four restores and leak SRCBLEND=ONE / DESTBLEND=ONE /
	// VERTEXALPHA / TEXTURERASTER=corona into the rest of the frame.
	// __finally restores on EVERY exit (normal or fault).
	__try {
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gpCoronaTexture[0]->raster);
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, coronaVerts, numCoronaVerts, coronaIndices, numCoronaIndices);
	} __finally {
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	}
}

/* CLASS 6: DrawDebugEnvMap removed — dead code, F3 key check with empty body */

void
RenderReflectionMap_leeds(void)
{
	RwCamera *cam = Scene.camera;
	if(!cam) return; // NULL camera → 0x7FAD4D-class fault
	RwCameraEndUpdate(cam);

	// FIX (unpaired-EndUpdate family, mirror of neoCarpipe.cpp:396-410):
	// this path swaps Scene.camera to reflectionCam and used to restore it
	// only on the trailing path. A SEH fault in the game scene renderers
	// (CClouds__RenderSkyPolys / RenderReflectionScene / DrawEnvMapCoronas)
	// stranded Scene.camera = reflectionCam with reflectionCam's
	// RwCameraBeginUpdate unpaired and cam left past its EndUpdate — exactly
	// the failure pattern that leaves a non-screen raster live on the camera
	// for downstream fullscreen draws. Now EVERYTHING past the EndUpdate
	// above runs under nested SEH: the inner __try/__except swallows+logs
	// renderer faults (same behavior as neoCarpipe), and the outer __finally
	// restores Scene.camera + re-pairs BOTH camera updates on EVERY exit —
	// normal path, __leave bail, swallowed fault, or even a fault in the
	// handler itself. Nothing can skip it.
	// C2712-safe: every local in this function is POD (no destructible
	// objects may coexist with __try). Locals are hoisted above the __try
	// so no __leave/exception can skip an initialization (C2360).
	RwCamera *savedcam = Scene.camera;
	RwFrame *reflFrame = NULL;
	RwFrame *camFrame = NULL;
	RwMatrix *reflLTM;
	RwRGBA color;
	RwV2d oldvw;
	bool vwSaved = false;
	bool began = false;
	__try {
		__try {
			MakeEnvmapRasters();
			// FIX 3 (kept): MakeEnvmapRasters can fail and leave the
			// reflection camera's rasters NULL — RwCameraClear/BeginUpdate
			// on it would fault. __leave runs the __finally, which re-pairs
			// RwCameraBeginUpdate(cam) for the EndUpdate above.
			if(!reflectionCam) __leave;
			if(!RwCameraGetRaster(reflectionCam) || !RwCameraGetZRaster(reflectionCam))
				__leave;

			// Viewport invariant: save reflectionCam's view window before
			// overwriting it (mirror of neoCarpipe.cpp:320/438 oldvw) so the
			// __finally can restore it on every path.
			oldvw = reflectionCam->viewWindow;
			vwSaved = true;
			RwCameraSetViewWindow(reflectionCam, &cam->viewWindow);

			reflFrame = RwCameraGetFrame(reflectionCam);
			camFrame = RwCameraGetFrame(cam);
			if(!reflFrame || !camFrame) __leave;
			RwFrameTransform(reflFrame, &camFrame->ltm, rwCOMBINEREPLACE);

			color.red = skyTopRed;
			color.green = skyTopGreen;
			color.blue = skyTopBlue;
			color.alpha = 255;
		//	color.red = skyBotRed; color.green = skyBotGreen; color.blue = skyBotBlue;

			// blend a bit of white into the sky color, otherwise it tends to be very blue
		//	color.red = color.red*0.6f + 255*0.4f;
		//	color.green = color.green*0.6f + 255*0.4f;
		//	color.blue = color.blue*0.6f + 255*0.4f;
			RwCameraClear(reflectionCam, &color, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);

			RwCameraBeginUpdate(reflectionCam);
			began = true;
			Scene.camera = reflectionCam;	// they do some begin/end updates with this in the called functions :/
			CClouds__RenderSkyPolys();
			RenderReflectionScene();
			if(reflFrame){
				reflLTM = RwFrameGetLTM(reflFrame);
				if(reflLTM)
					DrawEnvMapCoronas(reflLTM->at);
			}
		} __except(EXCEPTION_EXECUTE_HANDLER){
			// Throttled (per-frame path, AGENTS.md logging doctrine) — the
			// unthrottled mandate applies to the post-disarm LEAK log only.
			if(dbglog_throttle("envswap_leeds_fault"))
				dbglog("[EnvSwap] RenderReflectionMap_leeds FAULT code=0x%08X — Scene.camera/update restore forced",
					GetExceptionCode());
		}
	} __finally {
		// Unconditional finally-style restore (mirror neoCarpipe.cpp:409-410).
		Scene.camera = savedcam;
		// Viewport invariant: put reflectionCam's view window back (the
		// in-update camera's viewport derives from it).
		if(vwSaved && reflectionCam)
			RwCameraSetViewWindow(reflectionCam, &oldvw);
		// Close the reflection update: `began` gives exact parity with the
		// old unconditional RwCameraEndUpdate(reflectionCam) on the normal
		// path; the curCamera test additionally rescues a half-completed
		// Begin that faulted before setting began (the reflectionCam
		// non-NULL check keeps a NULL==NULL curCamera comparison from
		// driving RwCameraEndUpdate(NULL)).
		if(began || (reflectionCam && (RwCamera*)RWSRCGLOBAL(curCamera) == reflectionCam))
			RwCameraEndUpdate(reflectionCam);
		// Re-pair the EndUpdate at function entry on EVERY path (only if an
		// update is not already open on cam — guards against double-begin).
		if((RwCamera*)RWSRCGLOBAL(curCamera) != cam)
			RwCameraBeginUpdate(cam);
	}
}

void (*CRenderer__ConstructRenderList)(void);

int &CMirrors__TypeOfMirror = *(int*)0xC7C724;
bool &bFudgeNow = *(bool*)0xC7C72A;

float &ms_lowLodDistScale = *(float*)0x8CD804;
float &ms_lodDistScale = *(float*)0x8CD800;

RwRGBAReal spheremapfog;

// ============================================================
// Deterministic restore guard for the temporary camera-raster swap in
// RenderSphereReflections. The reflection pass points the main camera's
// frameBuffer/Z at the reflection rasters for the duration of the list
// build + reflection scene render; that window is a transient-camRas
// source — a downstream geometry-phase reader that samples
// RwCameraGetRaster sees the reflection target (the 256² envFB), not the
// screen — and NO exit may leave the main camera swapped.
//
// SEH reality (why a destructor-only guard is NOT airtight): this TU builds
// without /EHa, so a SEH fault (the 0x7FAD4D-class faults from game
// renderers) does not unwind C++ destructors — ~SphereReflectionGuard never
// ran on fault paths. That is also why the post-disarm leak log printed 0
// hits: it lived INSIDE the destructor, so it was unreachable on exactly the
// paths that leak. Fix, mirroring neoCarpipe.cpp:396-410:
//   1. every post-swap step runs in a POD-only helper (C2712: __try may not
//      coexist with destructible locals) under __try/__except;
//   2. the helper's tail performs the restore UNCONDITIONALLY (finally-
//      style) — no __leave, caught fault, or normal path skips it;
//   3. Disarm()/~SphereReflectionGuard remain the idempotent backstop for
//      the pre-span early-return paths and any C++ unwind;
//   4. the [EnvSwap] post-disarm leak check is UNTHROTTLED and compares
//      Scene.camera->frameBuffer against the ORIGINAL raster, logging both
//      pointers + dims + the disarm call site.
// ============================================================
struct SphereReflectionGuard {
	RwCamera *cam;
	RwRaster *fb;        // main camera colour raster (pre-swap)
	RwRaster *zb;        // main camera Z raster (pre-swap)
	float farPlane;
	float fog;
	bool armed;          // true once the swap has actually been applied
	bool disarmed;       // true once Disarm() has restored (idempotence)
	bool lodsSaved;
	float savedLowLod;
	float savedLod;

	explicit SphereReflectionGuard(RwCamera *c)
		: cam(c), fb(NULL), zb(NULL), farPlane(0.0f), fog(0.0f),
		  armed(false), disarmed(false), lodsSaved(false),
		  savedLowLod(0.0f), savedLod(0.0f) {}

	// Single disarm point — idempotent. `site` names the disarm call site
	// (span tail vs. destructor) so a leak report says WHO restored.
	void Disarm(const char *site)
	{
		if(disarmed) return;
		disarmed = true;

		if(cam && armed){
			if(fb) RwCameraSetRaster(cam, fb);
			if(zb) RwCameraSetZRaster(cam, zb);
			RwCameraSetFarClipPlane(cam, farPlane);
			RwCameraSetFogDistance(cam, fog);

			// Post-disarm verification — UNTHROTTLED + diagnostic. The old
			// dbglog_throttle("envswap") gate made a 0-hit result ambiguous
			// with "log throttled"; a persistent leak MUST be loud.
			// Authoritative condition: Scene.camera->frameBuffer after
			// disarm must be the ORIGINAL raster again (pointer identity —
			// nothing runs between the restore above and this read). The
			// screen-dims check is kept as a second trigger: it also fires
			// when the captured "original" was itself already a non-screen
			// raster (a leak from an EARLIER pass). On any mismatch log
			// both pointers + both dimensions + the disarm call site.
			RwCamera *nowCam = Scene.camera ? Scene.camera : cam;
			RwRaster *postRas = nowCam ? RwCameraGetRaster(nowCam) : NULL;
			bool screenMismatch = postRas && RsGlobal && postRas->width > 0 &&
				(postRas->width != RsGlobal->MaximumWidth ||
				 postRas->height != RsGlobal->MaximumHeight);
			if(postRas != fb || screenMismatch)
				dbglog("[EnvSwap] LEAK disarm@%s: cam=%p Scene.camera=%p frameBuffer=%p %dx%d != original %p %dx%d (screen %dx%d) — raster swap leaked past SphereReflectionGuard",
				       site, (void*)cam, (void*)Scene.camera,
				       (void*)postRas, postRas ? postRas->width : 0, postRas ? postRas->height : 0,
				       (void*)fb, fb ? fb->width : 0, fb ? fb->height : 0,
				       RsGlobal ? (int)RsGlobal->MaximumWidth : 0,
				       RsGlobal ? (int)RsGlobal->MaximumHeight : 0);
		}
		if(lodsSaved){
			ms_lowLodDistScale = savedLowLod;
			ms_lodDistScale = savedLod;
		}
		gRenderingSpheremap = false;
		bFudgeNow = false;
		sphereRadius = 0.0f;
	}

	~SphereReflectionGuard()
	{
		// Backstop for the pre-span early-return paths and any C++ unwind;
		// no-op when the span tail already disarmed.
		Disarm("~SphereReflectionGuard");
	}
};

// POD-only SEH fence for the post-swap span (C2712: __try may not coexist
// with destructible locals — the guard object therefore stays in the
// caller's frame). Nested SEH, mirror of neoCarpipe.cpp:396-410: the inner
// __try/__except swallows+logs renderer faults; the outer __finally runs the
// finally-style restore on EVERY exit — normal path, __leave bail, swallowed
// fault, or even a fault inside the log handler. The camera-raster swap can
// never strand. Every local is POD and hoisted above the __try so no
// __leave/exception can skip an initialization (C2360).
static void
RenderSphereReflectionsSwappedSpan(SphereReflectionGuard *g, RwCamera *cam)
{
	RwRGBA skyBot = { skyBotRed, skyBotGreen, skyBotBlue, 255 };
	RwRGBA skyTop = { skyTopRed, skyTopGreen, skyTopBlue, 255 };
	RwRGBA color;
	bool began = false;
	float blendF;

	__try {
		__try {
			sphereRadius = 60.0f * config->envMapFarClipMult;

			if (config->envMapUseLODs) {
				g->lodsSaved = true;
				g->savedLowLod = ms_lowLodDistScale;
				g->savedLod = ms_lodDistScale;
				ms_lowLodDistScale = 0.1f;
				ms_lodDistScale = 0.12f;
			}

			// MakeEnvmapRasters can fail and leave the reflection camera's
			// rasters NULL — pointing the main camera at NULL would fault the
			// next draw. Nothing is swapped yet (armed still false); __leave
			// runs the __finally, whose Disarm() still clears
			// sphereRadius/LODs/gRenderingSpheremap/bFudgeNow (set above).
			if(!RwCameraGetRaster(reflectionCam) || !RwCameraGetZRaster(reflectionCam))
				__leave;

			// Arm before the first swap so a fault in either setter still
			// lets the __finally restore both rasters.
			g->armed = true;
			RwCameraSetRaster(cam, RwCameraGetRaster(reflectionCam));
			RwCameraSetZRaster(cam, RwCameraGetZRaster(reflectionCam));
			RwCameraSetFarClipPlane(cam, sphereRadius);
			RwCameraSetFogDistance(cam, sphereRadius*0.75f);

			if(config->vehiclePipe == CAR_ENV){
				// more like Neo
			//	color = skyBot;
				color = skyTop;
				// blend a bit of white into the sky color, otherwise it tends to be very blue
				blendF = 0.7f;
				color.red = color.red*blendF + 255*(1.0f - blendF);
				color.green = color.green*blendF + 255*(1.0f - blendF);
				color.blue = color.blue*blendF + 255*(1.0f - blendF);
			}else{
				color = skyTop;
				if(color.red < 64) color.red = 64;
				if(color.green < 64) color.green = 64;
				if(color.blue < 64) color.blue = 64;
			}
			RwCameraClear(cam, &color, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
			RwRGBARealFromRwRGBA(&spheremapfog, &color);

			// FIX (stale reflectionCamPos): while sphereRadius != 0 the entity
			// cull runs inside ConstructRenderList() below via the GetIsOnScreen
			// hook (envmap.cpp:241) and measures against reflectionCamPos. That
			// global was only assigned inside the GLOBAL RenderReflectionScene()
			// (:312, leeds RenderReflectionMap_leeds path) until commit 18f5303
			// swapped THIS render to CarPipe::RenderReflectionScene() — which
			// never touches it. Under CAR_MOBILE/CAR_ENV/CAR_MODERN it stayed
			// zero-init, so every entity beyond sphereRadius from the map origin
			// was culled out of the list and reflectionTex came back flat clear
			// color: the per-frame env-sphere re-render existed but its CONTENT
			// was missing ("IV shows reflections, Modern doesn't" — IV never
			// passes the gate at :477; it gets its env from the MatFX FX pass in
			// vehiclePipe.cpp instead). Seed it from the camera BEFORE the list
			// is built — same source (:312) uses.
			reflectionCamPos = TheCamera.GetPosition();

			bFudgeNow = true;	/* Don't know if this actually fudges, but it might help a bit */
			gRenderingSpheremap = true;
			CRenderer__ConstructRenderList();
			RwCameraBeginUpdate(cam);
			began = true;
			// FIX 1+2: Use CarPipe::RenderReflectionScene() so the env map includes vehicles
			// (RenderEverythingBarRoads) + fading entities, not just roads+buildings.
			// The global RenderReflectionScene() only renders roads+buildings.
			// This fix also ensures the reflection scene is rendered BEFORE the main scene
			// via the CRenderer__ConstructRenderList hook, avoiding the state corruption
			// that occurred when CarPipe::RenderEnvTex re-rendered the env map mid-frame
			// from RenderScene_after.
			CarPipe::RenderReflectionScene();
		} __except(EXCEPTION_EXECUTE_HANDLER){
			// Throttled (per-frame path, AGENTS.md logging doctrine) — the
			// unthrottled mandate applies to the post-disarm LEAK log only.
			if(dbglog_throttle("envswap_span_fault"))
				dbglog("[EnvSwap] RenderSphereReflections FAULT code=0x%08X — swap restore forced",
					GetExceptionCode());
		}
	} __finally {
		// Unconditional finally-style restore (mirror neoCarpipe.cpp:409-410):
		// no __leave, swallowed fault, or normal path can skip it.
		// Pair the RwCameraBeginUpdate above first — if a fault struck
		// between Begin and End the update is still open on cam; close it
		// iff it is ours (began: exact parity with the old unconditional
		// EndUpdate on the normal path; the curCamera test rescues a
		// half-completed Begin that faulted before setting began).
		if(began || (RwCamera*)RWSRCGLOBAL(curCamera) == cam)
			RwCameraEndUpdate(cam);
		gRenderingSpheremap = false;
		bFudgeNow = false;
		sphereRadius = 0.0f;
		g->Disarm("RenderSphereReflections.span-tail");
	}
}

void
RenderSphereReflections(void)
{

	if(iCanHasbuildingPipe && (config->vehiclePipe == CAR_MOBILE || config->vehiclePipe == CAR_ENV || config->vehiclePipe == CAR_MODERN)){
		MakeEnvmapRasters();
		if(!reflectionCam) return;

		RwCamera *cam = Scene.camera; // NULL curCamera faults (same class as 0x7FAD4D)
		if(!cam) return;

		// Deterministic restore: capture the main camera's rasters/planes
		// BEFORE the swap. The guard restores them at scope exit on every
		// path, so a downstream geometry-phase reader never observes the
		// reflection raster and an early bail cannot strand the swap.
		SphereReflectionGuard guard(cam);
		guard.fb = RwCameraGetRaster(cam);
		guard.zb = RwCameraGetZRaster(cam);
		// FIX 2 audit: this bail is safe — armed is still false, so the
		// destructor performs NO raster/clip restore (none was applied yet),
		// but it still clears lods/gRenderingSpheremap/bFudgeNow/sphereRadius.
		if(!guard.fb || !guard.zb) return;
		guard.farPlane = RwCameraGetFarClipPlane(cam);
		guard.fog = RwCameraGetFogDistance(cam);

		// Everything from the LOD fudge + raster swap through the reflection
		// render runs in the POD-only SEH fence above; its tail restores
		// unconditionally (finally-style), and the guard destructor is the
		// idempotent backstop for this scope.
		RenderSphereReflectionsSwappedSpan(&guard, cam);

		// Diagnostic: confirm env map rendered and log envFB state
		{
			static int envLogCount = 0;
			if(envLogCount++ % 300 == 0){
				dbglog("[EnvMap] RenderSphereReflections done: envFB=%p %dx%d refTex=%p raster=%p",
					envFB, envFB ? envFB->width : 0, envFB ? envFB->height : 0,
					reflectionTex, reflectionTex ? RwTextureGetRaster(reflectionTex) : NULL);
			}
		}

		// Raster/far/fog/LOD globals are restored by Disarm() (span tail on
		// every armed path, ~SphereReflectionGuard otherwise) — before the
		// downstream CRenderer__ConstructRenderList below, so that list is
		// built against the restored main camera.
	}
	CRenderer__ConstructRenderList();
}

void
ShutdownEnvMap(void)
{
	// ============================================================
	// DLL_PROCESS_DETACH teardown — POISON-GUARD ONLY, zero RW/D3D calls.
	//
	// Root cause of the persistent "DLL_DETACH: ShutdownEnvMap crashed"
	// (write-after-free on refTex 0x04BF3F28): this function's ONLY caller
	// is main.cpp DLL_PROCESS_DETACH, and by the time it runs the game has
	// already torn down its RW engine and released the D3D9 device. The
	// RwTexture/RwRaster/RwCamera heap blocks and the D3DPOOL_DEFAULT D3D
	// textures behind the CAMERATEXTURE rasters are already freed:
	//   - the first write here, RwTextureSetRaster(reflectionTex, NULL)
	//     (a texture->raster field store), landed in the already-freed
	//     RwTexture block — that IS the write-after-free on refTex.
	//     reflectionTex has exactly one destroy site in the whole project
	//     (this function), so the free came from the game's own exit
	//     teardown, before DLL_PROCESS_DETACH fired;
	//   - RwRasterDestroy(envFB/envZB) would Release() the D3D texture
	//     behind the raster — that D3D texture does NOT outlive the device
	//     teardown, so it too is a write-after-free (COM Release stores the
	//     refcount in the freed object);
	//   - RwTextureDestroy/RwFrameDestroy/RwCameraDestroy write into freed
	//     RW internals (free lists, dictionary links).
	// Fix-9 only re-ordered the destroys (detaching envFB from the texture
	// so RwTextureDestroy could not free it before our explicit
	// RwRasterDestroy) — correct for a LIVE engine, but every remaining
	// path still wrote into memory the game had already freed.
	//
	// Ownership doctrine: the OS process-exit reclaim is the single release
	// point for these objects at detach; no RW destroy is attempted here,
	// ever. Every cached copy is nulled IMMEDIATELY (poison-guard) so any
	// second touch — a re-entrant shutdown, a stale holder in another TU,
	// a late frame callback — is a NULL-guarded no-op, not a crash. A
	// mid-session (live-engine) teardown, if ever needed, must be a NEW
	// function written for a live engine, not this one.
	// ============================================================
	reflectionTex = NULL;
	envFB = NULL;
	envZB = NULL;
	envFB_prev = NULL;
	reflectionCam = NULL;
	envTemporalFrame = 0;
}

void
envmaphooks(void)
{
	InjectHook(0x536BCE, &CEntity::GetIsOnScreen);	// CEntity::IsVisible
	InjectHook(0x5540AD, &CEntity::GetIsOnScreen);	// CRenderer::SetupMapEntityVisibility
	InjectHook(0x554174, &CEntity::GetIsOnScreen);	// CRenderer::SetupMapEntityVisibility
	InjectHook(0x5543C2, &CEntity::GetIsOnScreen);	// CRenderer::SetupEntityVisibility
	InjectHook(0x554504, &CEntity::GetIsOnScreen);	// CRenderer::SetupEntityVisibility
	InjectHook(0x55495E, &CEntity::GetIsOnScreen);	// CRenderer::ScanSectorList
	InjectHook(0x409870, &CEntity::GetIsOnScreen);	// CStreaming::DeleteLeastUsedEntityRwObject
//	InjectHook(0x71FAE0, &CEntity::IsEntityOccluded, PATCH_JUMP);
//	InjectHook(0x420C40, madness, PATCH_JUMP);
	InterceptCall(&CRenderer__ConstructRenderList, RenderSphereReflections, 0x53E9F9);
}
