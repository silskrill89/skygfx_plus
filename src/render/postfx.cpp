#include "skygfx.h"
#include "ModuleList.hpp"
#include "postfx.h"
#include "chars.h"
#include "depthhook.h"

RwIm2DVertex *colorfilterVerts = (RwIm2DVertex*)0xC400D8;
RwImVertexIndex *colorfilterIndices = (RwImVertexIndex*)0x8D5174;

Imf &CPostEffects::ms_imf = *(Imf*)0xC40150;

WRAPPER void CPostEffects::DarknessFilter(uint8 alpha) { EAXJMP(0x702F00); }
WRAPPER void CPostEffects::Grain(int strengh, bool generate) { EAXJMP(0x7037C0); }
WRAPPER void CPostEffects::SpeedFX(float) { EAXJMP(0x7030A0); }
RwRaster *&CPostEffects::pRasterFrontBuffer = *(RwRaster**)0xC402D8;
float &CPostEffects::m_fInfraredVisionFilterRadius = *(float*)0x8D50B8;
RwRaster *&CPostEffects::m_pGrainRaster = *(RwRaster**)0xC402B0;
WRAPPER void CPostEffects::InfraredVision(RwRGBA color1, RwRGBA color2) { EAXJMP(0x703F80); }
WRAPPER void CPostEffects::ImmediateModeRenderStatesStore(void) { EAXJMP(0x700CC0); }
WRAPPER void CPostEffects::ImmediateModeRenderStatesSet(void) { EAXJMP(0x700D70); }
WRAPPER void CPostEffects::ImmediateModeRenderStatesReStore(void) { EAXJMP(0x700E00); }
WRAPPER void CPostEffects::SetFilterMainColour(RwRaster *raster, RwRGBA color) { EAXJMP(0x703520); }
WRAPPER void CPostEffects::DrawQuad(float x1, float y1, float x2, float y2, uchar r, uchar g, uchar b, uchar alpha, RwRaster *ras) { EAXJMP(0x700EC0); }
WRAPPER void CPostEffects::NightVision(RwRGBA color) { EAXJMP(0x7011C0); }
WRAPPER void CPostEffects::ColourFilter(RwRGBA rgb1, RwRGBA rgb2) { EAXJMP(0x703650); }
float &CPostEffects::m_fNightVisionSwitchOnFXCount = *(float*)0xC40300;
int &CPostEffects::m_InfraredVisionGrainStrength = *(int*)0x8D50B4;
int &CPostEffects::m_NightVisionGrainStrength = *(int*)0x8D50A8;
bool &CPostEffects::m_bInfraredVision = *(bool*)0xC402B9;

bool &CPostEffects::m_bDisableAllPostEffect = *(bool*)0xC402CF;

bool &CPostEffects::m_bColorEnable = *(bool*)0x8D518C;
int &CPostEffects::m_colourLeftUOffset = *(int*)0x8D5150;
int &CPostEffects::m_colourRightUOffset = *(int*)0x8D5154;
int &CPostEffects::m_colourTopVOffset = *(int*)0x8D5158;
int &CPostEffects::m_colourBottomVOffset = *(int*)0x8D515C;
float &CPostEffects::m_colour1Multiplier = *(float*)0x8D5160;
float &CPostEffects::m_colour2Multiplier = *(float*)0x8D5164;
float &CPostEffects::SCREEN_EXTRA_MULT_CHANGE_RATE = *(float*)0x8D5168;
float &CPostEffects::SCREEN_EXTRA_MULT_BASE_CAP = *(float*)0x8D516C;
float &CPostEffects::SCREEN_EXTRA_MULT_BASE_MULT = *(float*)0x8D5170;

bool &CPostEffects::m_bRadiosity = *(bool*)0xC402CC;
bool &CPostEffects::m_bRadiosityDebug = *(bool*)0xC402CD;
int &CPostEffects::m_RadiosityFilterPasses = *(int*)0x8D510C;
int &CPostEffects::m_RadiosityRenderPasses = *(int*)0x8D5110;
int &CPostEffects::m_RadiosityIntensityLimit = *(int*)0x8D5114;
int &CPostEffects::m_RadiosityIntensity = *(int*)0x8D5118;
bool &CPostEffects::m_bRadiosityBypassTimeCycleIntensityLimit = *(bool*)0xC402CE;
int &CPostEffects::m_RadiosityFilterUCorrection = *(int*)0x8D511C;
int &CPostEffects::m_RadiosityFilterVCorrection = *(int*)0x8D5120;

bool &CPostEffects::m_bDarknessFilter = *(bool*)0xC402C4;
int &CPostEffects::m_DarknessFilterAlpha = *(int*)0x8D5204;
int &CPostEffects::m_DarknessFilterAlphaDefault = *(int*)0x8D50F4;
int &CPostEffects::m_DarknessFilterRadiosityIntensityLimit = *(int*)0x8D50F8;

bool &CPostEffects::m_bCCTV = *(bool*)0xC402C5;
bool &CPostEffects::m_bFog = *(bool*)0xC402C6;
bool &CPostEffects::m_bNightVision = *(bool*)0xC402B8;
bool &CPostEffects::m_bHeatHazeFX = *(bool*)0xC402BA;
bool &CPostEffects::m_bHeatHazeMaskModeTest = *(bool*)0xC402BB;
bool &CPostEffects::m_bGrainEnable = *(bool*)0xC402B4;
bool &CPostEffects::m_waterEnable = *(bool*)0xC402D3;

bool &CPostEffects::m_bSpeedFX = *(bool*)0x8D5100;
bool &CPostEffects::m_bSpeedFXTestMode = *(bool*)0xC402C7;
uint8 &CPostEffects::m_SpeedFXAlpha = *(uint8*)0x8D5104;

/* My own */
bool CPostEffects::m_bBlurColourFilter = true;
bool CPostEffects::m_bYCbCrFilter = false;
float CPostEffects::m_lumaScale = 219.0f/255.0f;
float CPostEffects::m_lumaOffset = 16.0f/255.0f;
float CPostEffects::m_cbScale = 1.23f;
float CPostEffects::m_cbOffset = 0.0f;
float CPostEffects::m_crScale = 1.23f;
float CPostEffects::m_crOffset = 0.0f;

// SSAO/SMAA
static RwRaster *ssaoRaster = nil;

// ============================================================
// Debug BMP dump checkpoints (INI-gated: postfxDumpDebug=1)
// Saves key surfaces to <gameDir>\skygfx_dump_<tag>_<frame>.bmp
// on the first 5 frames to diagnose black-screen issues.
// ============================================================
static int g_postfxDumpFrame = 0;   // incremented once per ColourFilter_switch
static bool g_postfxDumpLoggedVerts = false;

static void DumpRasterToBMP(const char* tag, IDirect3DSurface9* src)
{
	if(!config || !config->postfxDumpDebug) return;
	if(g_postfxDumpFrame >= 5) return;
	if(!src) { dbglog("[Dump] %s: src NULL", tag); return; }
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) { dbglog("[Dump] %s: no device", tag); return; }

	D3DSURFACE_DESC desc;
	if(FAILED(src->GetDesc(&desc))) { dbglog("[Dump] %s: GetDesc failed", tag); return; }

	IDirect3DSurface9 *sysSurf = NULL;
	HRESULT hr = dev->CreateOffscreenPlainSurface(desc.Width, desc.Height,
		D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, NULL);
	if(FAILED(hr) || !sysSurf) { dbglog("[Dump] %s: CreateOffscreenPlainSurface failed hr=0x%08X", tag, hr); return; }

	hr = dev->GetRenderTargetData(src, sysSurf);
	if(FAILED(hr)) { dbglog("[Dump] %s: GetRenderTargetData failed hr=0x%08X", tag, hr); sysSurf->Release(); return; }

	D3DLOCKED_RECT lr;
	if(FAILED(sysSurf->LockRect(&lr, NULL, D3DLOCK_READONLY))){
		dbglog("[Dump] %s: LockRect failed", tag);
		sysSurf->Release();
		return;
	}

	// Write 24-bit BMP (BGR, bottom-up rows)
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "skygfx_dump_%s_%d.bmp", tag, g_postfxDumpFrame);
	FILE *f = fopen(path, "wb");
	if(f){
		int w = (int)desc.Width, h = (int)desc.Height;
		int rowSize = (w * 3 + 3) & ~3;
		int dataSize = rowSize * h;
		BITMAPFILEHEADER bfh;
		BITMAPINFOHEADER bih;
		memset(&bfh, 0, sizeof(bfh));
		memset(&bih, 0, sizeof(bih));
		bfh.bfType = 0x4D42; // 'BM'
		bfh.bfSize = sizeof(bfh) + sizeof(bih) + dataSize;
		bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
		bih.biSize = sizeof(bih);
		bih.biWidth = w;
		bih.biHeight = h; // positive = bottom-up
		bih.biPlanes = 1;
		bih.biBitCount = 24;
		bih.biSizeImage = dataSize;
		fwrite(&bfh, sizeof(bfh), 1, f);
		fwrite(&bih, sizeof(bih), 1, f);
		// Write rows bottom-up (BMP origin is bottom-left)
		for(int y = h - 1; y >= 0; y--){
			const BYTE *srcRow = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
			BYTE row[4096 * 3];
			for(int x = 0; x < w; x++){
				// A8R8G8B8 → BGR
				row[x*3+0] = srcRow[x*4+0]; // B
				row[x*3+1] = srcRow[x*4+1]; // G
				row[x*3+2] = srcRow[x*4+2]; // R
			}
			fwrite(row, 1, w*3, f);
			// pad to 4-byte boundary
			for(int p = w*3; p < rowSize; p++) fputc(0, f);
		}
		fclose(f);
		dbglog("[Dump] %s: wrote %dx%d BMP", tag, w, h);
	} else {
		dbglog("[Dump] %s: fopen failed", tag);
	}

	sysSurf->UnlockRect();
	sysSurf->Release();
}

// Dump the current bound render target (RT0)
static void DumpCurrentRT(const char* tag)
{
	if(!config || !config->postfxDumpDebug) return;
	if(g_postfxDumpFrame >= 5) return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	IDirect3DSurface9 *rt = NULL;
	dev->GetRenderTarget(0, &rt);
	if(rt){
		DumpRasterToBMP(tag, rt);
		rt->Release();
	} else {
		dbglog("[Dump] %s: no RT0", tag);
	}
}

// Called from RenderScene_after to dump the camera raster after scene render
void PostFX_DumpSceneCamera(void)
{
	if(!config || !config->postfxDumpDebug) return;
	if(g_postfxDumpFrame >= 5) return;
	DumpCurrentRT("scene_cam");
}

// Log colorfilterVerts raw values once
static void LogColorfilterVerts(void)
{
	if(g_postfxDumpLoggedVerts) return;
	g_postfxDumpLoggedVerts = true;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	RwRaster *fb = CPostEffects::pRasterFrontBuffer;
	dbglog("[Dump] colorfilterVerts: camRas=%p(%dx%d) pRasFB=%p(%dx%d)",
		camRas, camRas ? camRas->width : 0, camRas ? camRas->height : 0,
		fb, fb ? fb->width : 0, fb ? fb->height : 0);
	for(int i = 0; i < 4; i++){
		dbglog("[Dump]   vert[%d]: x=%.3f y=%.3f z=%.3f rhw=%.6f u=%.6f v=%.6f color=0x%08X",
			i, colorfilterVerts[i].x, colorfilterVerts[i].y, colorfilterVerts[i].z,
			colorfilterVerts[i].rhw, colorfilterVerts[i].u, colorfilterVerts[i].v,
			colorfilterVerts[i].emissiveColor);
	}
}

// SMAA intermediate rasters (file-scope for cleanup on shutdown)
static RwRaster *g_smaaEdgeRaster = NULL;
static RwRaster *g_smaaBlendRaster = NULL;
static RwRaster *g_smaaPrevFrameRaster = NULL;
static int g_smaaRtWidth = 0, g_smaaRtHeight = 0;
static RwTexture *g_smaaBlendTexRW = NULL;
static RwTexture *g_smaaPrevFrameTexRW = NULL;
static bool s_smaaRastersInitialized = false;
static bool s_smaaPendingInit = false;  // set by DrawSMAA, consumed by SMAATryInitRasters
static bool s_smaaBroken = false;       // set if init fails; DrawSMAA permanently skips

// SMAA temporal history: set to true after the first frame's history is written.
// On the first frame, the history raster is black (never rendered to), so the
// temporal blend would produce a very dark image. We use blendStrength=1.0
// (all current, no history) on the first frame to avoid this.
static bool g_smaaHistoryValid = false;

// Track initialized CAMERATEXTURE rasters for RasterEnsureSurfaceReady.
// Max 8 tracked rasters — plenty for all our passes.
#define MAX_TRACKED_RASTERS 8
static RwRaster *s_trackedRasters[MAX_TRACKED_RASTERS] = {NULL};
static int s_trackedRasterCount = 0;

static void TrackRaster(RwRaster *ras) {
	if(!ras || s_trackedRasterCount >= MAX_TRACKED_RASTERS) return;
	for(int i = 0; i < s_trackedRasterCount; i++)
		if(s_trackedRasters[i] == ras) return;
	s_trackedRasters[s_trackedRasterCount++] = ras;
}

static void UntrackRaster(RwRaster *ras) {
	if(!ras) return;
	for(int i = 0; i < s_trackedRasterCount; i++) {
		if(s_trackedRasters[i] == ras) {
			s_trackedRasters[i] = s_trackedRasters[--s_trackedRasterCount];
			s_trackedRasters[s_trackedRasterCount] = NULL;
			return;
		}
	}
}

// ============================================================
// RasterEnsureSurfaceReady — check if a CAMERATEXTURE raster
// has a valid D3D9 surface before binding as RT or texture.
// Returns true if the raster is safe to use.
// CAMERATEXTURE rasters created via RwRasterCreate do NOT have
// a D3D9 surface until RwCameraBeginUpdate runs on them.
// Binding one without surface = crash at RwD3D9SetRenderTarget.
// Non-CAMERATEXTURE rasters are always safe.
// ============================================================
static bool RasterEnsureSurfaceReady(RwRaster *ras, const char *tag) {
	if(!ras) {
		dbglog("[RasterInit] %s: raster is NULL, skipping", tag);
		return false;
	}
	// Only CAMERATEXTURE rasters need the surface check
	if((ras->cType & rwRASTERTYPEMASK) != rwRASTERTYPECAMERATEXTURE)
		return true;
	// Check if tracked (initialized via camera path)
	for(int i = 0; i < s_trackedRasterCount; i++) {
		if(s_trackedRasters[i] == ras)
			return true;
	}
	// Not tracked — surface may not exist
	if(dbglog_throttle("ras_unsafe"))
		dbglog("[RasterInit] %s: CAMERATEXTURE raster %p not initialized yet, skipping", tag, ras);
	return false;
}

// Scratch camera + Z raster used to force-init SMAA CAMERATEXTURE rasters.
// Created once, persists for the session (mirrors envmap.cpp pattern).
// Cleaned up on device reset and shutdown via ReleaseSMAAStaticResources.
// NOTE: The force-init must happen OUTSIDE the main camera's BeginUpdate
// (i.e., from RenderScene_before), not inline in DrawSMAA (which runs
// mid-frame from ColourFilter_switch/DrawFinalEffects). Even with a
// scratch camera, RW 3.6's D3D9 driver crashes creating render-target
// textures while the device is in an active render state.
static RwCamera *s_smaaInitCam = NULL;
static RwRaster *s_smaaInitZRas = NULL;

void ReleaseSMAAStaticResources(void)
{
	dbglog("ReleaseSMAAStaticResources: releasing...");
	if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
	if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
	if(g_smaaEdgeRaster){ UntrackRaster(g_smaaEdgeRaster); RwRasterDestroy(g_smaaEdgeRaster); g_smaaEdgeRaster = NULL; }
	if(g_smaaBlendRaster){ UntrackRaster(g_smaaBlendRaster); RwRasterDestroy(g_smaaBlendRaster); g_smaaBlendRaster = NULL; }
	if(g_smaaPrevFrameRaster){ UntrackRaster(g_smaaPrevFrameRaster); RwRasterDestroy(g_smaaPrevFrameRaster); g_smaaPrevFrameRaster = NULL; }
	g_smaaRtWidth = 0; g_smaaRtHeight = 0;
	s_smaaRastersInitialized = false;
	s_smaaPendingInit = false;
	s_smaaBroken = false;
	g_smaaHistoryValid = false;
	dbglog("ReleaseSMAAStaticResources: done");
}

// Velocity buffer resources
static IDirect3DTexture9 *g_velocityTex = NULL;
static IDirect3DSurface9 *g_velocitySurf = NULL;
static D3DMATRIX g_prevVPMatrix;
static bool g_prevVPValid = false;

static void ReleaseVelocityBufferResources(void)
{
	if(g_velocityTex){ g_velocityTex->Release(); g_velocityTex = NULL; }
	if(g_velocitySurf){ g_velocitySurf->Release(); g_velocitySurf = NULL; }
	g_prevVPValid = false;
}

// SSAO overhaul forward declaration
static void DrawSSAO_Overhaul(void);

// Atmospheric PostFX forward declarations
static void DrawVelocityBuffer(void);
static void DrawHeightFog(void);
static void DrawGodRays(void);

// View/proj matrices (defined in pipelinecommon.cpp, no header extern)
extern D3DMATRIX &_RwD3D9D3D9ViewTransform;
extern D3DMATRIX &_RwD3D9D3D9ProjTransform;


/////
///// Menu state guard — skip expensive PostFX during menus/loading/pause
/////
static inline bool IsGameInMenuOrPaused() {
	return CMenuManager__m_bMenuActive || CCutsceneMgr__ms_running || CPostEffects::m_bDisableAllPostEffect;
}

/////
///// Im2D overrides
/////


int overrideColorMod = -1;
int overrideAlphaMod = -1;
void *overrideIm2dPixelShader;
void InitSSAOResources(void);

void Im2DColorModulationHook(RwUInt32 stage, RwUInt32 type, RwUInt32 value)
{
	if(overrideColorMod >= 0)
		RwD3D9SetTextureStageState(stage, type, overrideColorMod);
	else
		RwD3D9SetTextureStageState(stage, type, value);
}
void Im2DAlphaModulationHook(RwUInt32 stage, RwUInt32 type, RwUInt32 value)
{
	if(overrideAlphaMod >= 0)
		RwD3D9SetTextureStageState(stage, type, overrideAlphaMod);
	else
		RwD3D9SetTextureStageState(stage, type, value);
}

void
Im2dSetPixelShader_hook(void*)
{
	RwD3D9SetPixelShader(overrideIm2dPixelShader);
}

// Per-frame summary report: logs all critical state once per frame
static unsigned int postfxReportFrame = 0;
static void postfxReportSummary()
{
	static unsigned int lastFrame = 0;
	if(postfxReportFrame == lastFrame) return;
	lastFrame = postfxReportFrame;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : 0;
	int pfbW = CPostEffects::pRasterFrontBuffer ? RwRasterGetWidth(CPostEffects::pRasterFrontBuffer) : 0;
	int pfbH = CPostEffects::pRasterFrontBuffer ? RwRasterGetHeight(CPostEffects::pRasterFrontBuffer) : 0;
	int camW = camRas ? RwRasterGetWidth(camRas) : 0;
	int camH = camRas ? RwRasterGetHeight(camRas) : 0;
	dbglog("[PostFX-REPORT] frame=%u filter=%d pipeline=%d pRasFB=%p(%dx%d) camRas=%p(%dx%d) gradingPS=%p overridePS=%p SSAO=%d smaa=%d normalBuf=%d pipeChain=%d bPBRVS=%p bPBRPS=%p vPBRVS=%p vPBRMod=%p",
		postfxReportFrame, config->colorFilter, config->pipeline,
		CPostEffects::pRasterFrontBuffer, pfbW, pfbH,
		camRas, camW, camH,
		gradingPS, overrideIm2dPixelShader,
		config->ssaoEnable, config->smaaEnable, config->normalBufferEnable, config->pipeChainEnable,
		buildingPBRVS, buildingPBRPS, vehiclePBRVS, VehiclePBR_Modern);
}


/////
/////
/////

// Credits: much of the code in this file was originally written by NTAuthority
// there's not a lot of that left now

void *iiiTrailsPS, *vcTrailsPS, *modernColorFilterPS;
RwRaster *grainRaster;


// Mobile stuff
struct Grade
{
	float r, g, b, a;
};
void *gradingPS, *contrastPS, *tonemapPassPS;
#define NUMHOURS 8
#define NUMWEATHERS 23
#define EXTRASTART 21

struct GradeColorset
{
	Grade red;
	Grade green;
	Grade blue;

	GradeColorset(void) {}
	GradeColorset(int h, int w);
	void Interpolate(GradeColorset *a, GradeColorset *b, float fa, float fb);
};


struct Colorcycle
{
	static bool initialised;
	static Grade redGrade[24][NUMWEATHERS];
	static Grade greenGrade[24][NUMWEATHERS];
	static Grade blueGrade[24][NUMWEATHERS];

	static void Initialise(void);
	static void Update(GradeColorset *colorset);
};




void
CPostEffects::UpdateFrontBuffer(void)
{
	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer pRasterFrontBuffer is NULL!");
		return;
	}
	if(!Scene.camera){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer Scene.camera is NULL!");
		return;
	}
	if(dbglog_throttle("ufb_copy"))
		dbglog("[PostFX] UpdateFrontBuffer pRasFB=%p(%dx%d) camRas=%p(%dx%d)",
			CPostEffects::pRasterFrontBuffer,
			RwRasterGetWidth(CPostEffects::pRasterFrontBuffer), RwRasterGetHeight(CPostEffects::pRasterFrontBuffer),
			RwCameraGetRaster(Scene.camera),
			RwRasterGetWidth(RwCameraGetRaster(Scene.camera)), RwRasterGetHeight(RwCameraGetRaster(Scene.camera)));
	RwCameraEndUpdate(Scene.camera);
	RwRasterPushContext(CPostEffects::pRasterFrontBuffer);
	RwRaster *copyResult = RwRasterRenderFast(RwCameraGetRaster(Scene.camera), 0, 0);
	RwRasterPopContext();
	RwCameraBeginUpdate(Scene.camera);

	// Debug dump: verify the camera→FB copy actually happened
	if(config && config->postfxDumpDebug && g_postfxDumpFrame < 5){
		if(!copyResult)
			dbglog("[Dump] UpdateFrontBuffer: RwRasterRenderFast returned NULL (copy failed)");
		DumpCurrentRT("fb_after_copy");
	}
}

RwRaster *vcs_radiosity_target1, *vcs_radiosity_target2;
static int s_vcsRadLastWidth = 0, s_vcsRadLastHeight = 0, s_vcsRadLastConfigRes = 0;
static bool s_vcsRadPendingInit = false;

void ReleaseVCSRadiosityResources(void)
{
	if(vcs_radiosity_target1){ UntrackRaster(vcs_radiosity_target1); RwRasterDestroy(vcs_radiosity_target1); vcs_radiosity_target1 = nil; }
	if(vcs_radiosity_target2){ UntrackRaster(vcs_radiosity_target2); RwRasterDestroy(vcs_radiosity_target2); vcs_radiosity_target2 = nil; }
	s_vcsRadLastWidth = 0;
	s_vcsRadLastHeight = 0;
	s_vcsRadLastConfigRes = -1; // force recreate next Radiosity_VCS call
	s_vcsRadPendingInit = false;
}
static RwIm2DVertex vcsVertices[24];
RwRect vcsRect;
RwImVertexIndex vcsIndices1[] = {
	0, 1, 2, 1, 2, 3,
		4, 5, 2, 5, 2, 3,
	4, 5, 6, 5, 6, 7,
		8, 9, 6, 9, 6, 7,
	8, 9, 10, 9, 10, 11,
		12, 13, 10, 13, 10, 11,
	12, 13, 14, 13, 14, 15,
};

RwImVertexIndex radiosityIndices[] = {
	0, 1, 2, 1, 2, 3
};

RwD3D9Vertex radiosity_vcs_vertices[44];

//#define LIMIT (config->trailsLimit)
//#define INTENSITY (config->trailsIntensity)

void
makequad(RwD3D9Vertex *v, int width, int height, int texwidth = 0, int texheight = 0)
{
	float w, h, tw, th;
	w = width;
	h = height;
	tw = texwidth > 0 ? texwidth : w;
	th = texheight > 0 ? texheight : h;
	v[0].x = 0;
	v[0].y = 0;
	v[0].z = 0.0f;
	v[0].rhw = 1.0f;
	v[0].u = 0.5f / tw;
	v[0].v = 0.5f / th;
	v[0].emissiveColor = 0xFFFFFFFF;
	v[1].x = 0;
	v[1].y = h;
	v[1].z = 0.0f;
	v[1].rhw = 1.0f;
	v[1].u = 0.5f / tw;
	v[1].v = (h + 0.5f) / th;
	v[1].emissiveColor = 0xFFFFFFFF;
	v[2].x = w;
	v[2].y = 0;
	v[2].z = 0.0f;
	v[2].rhw = 1.0f;
	v[2].u = (w + 0.5f) / tw;
	v[2].v = 0.5f / th;
	v[2].emissiveColor = 0xFFFFFFFF;
	v[3].x = w;
	v[3].y = h;
	v[3].z = 0.0f;
	v[3].rhw = 1.0f;
	v[3].u = (w + 0.5f) / tw;
	v[3].v = (h + 0.5f) / th;
	v[3].emissiveColor = 0xFFFFFFFF;
}

void
CPostEffects::Radiosity_VCS_init(void)
{
	dbglog("Radiosity_VCS_init: start");
	static float uOffsets[] = { -1.0f, 1.0f, 0.0f, 0.0f,   -1.0f, 1.0f, -1.0f, 1.0f };
	static float vOffsets[] = { 0.0f, 0.0f, -1.0f, 1.0f,   -1.0f, -1.0f, 1.0f, 1.0f };
	int i;
	int resMult = config->trailsResolution;
	RwUInt32 c;
	float w, h;

	if(vcs_radiosity_target1)
		{ UntrackRaster(vcs_radiosity_target1); RwRasterDestroy(vcs_radiosity_target1); }
	vcs_radiosity_target1 = RwRasterCreate(256 * resMult, 128 * resMult, RwCameraGetRaster(Scene.camera)->depth, rwRASTERTYPECAMERATEXTURE);
	if(vcs_radiosity_target2)
		{ UntrackRaster(vcs_radiosity_target2); RwRasterDestroy(vcs_radiosity_target2); }
	vcs_radiosity_target2 = RwRasterCreate(256 * resMult, 128 * resMult, RwCameraGetRaster(Scene.camera)->depth, rwRASTERTYPECAMERATEXTURE);

	// NOTE: D3D9 surface for CAMERATEXTURE rasters is NOT created here.
	// It is created lazily by SMAATryInitRasters() (called from
	// RenderScene_before, outside the main camera's BeginUpdate).
	// Using RwD3D9SetRenderTarget on a fresh CAMERATEXTURE raster
	// mid-frame would crash (NULL nativeData). Radiosity_VCS will
	// skip the first frame after init to let the deferred init run.
	// This replaces the old inline force-init via Scene.camera which
	// crashed RW 3.6's D3D9 driver mid-frame (same bug as SMAA force-init).
//	RwD3D9CreateVertexBuffer(stride, size, &vbuf, &offset);

	w = 256 * resMult;
	h = 128 * resMult;

	// TODO: tex coords correct?
	makequad(radiosity_vcs_vertices, 256 * resMult, 128 * resMult);
	makequad(radiosity_vcs_vertices+4, RwCameraGetRaster(Scene.camera)->width, RwCameraGetRaster(Scene.camera)->height);

	// black vertices; at 8
	for(i = 0; i < 4; i++){
		radiosity_vcs_vertices[i+8] = radiosity_vcs_vertices[i];
		radiosity_vcs_vertices[i+8].emissiveColor = 0;
	}

	// two sets blur vertices; at 12
	c = D3DCOLOR_ARGB(0xFF, 36, 36, 36);
	for(i = 0; i < 2*4*4; i++){
		radiosity_vcs_vertices[i+12] = radiosity_vcs_vertices[i%4];
		radiosity_vcs_vertices[i+12].emissiveColor = c;
		switch(i%4){
		case 0:
			radiosity_vcs_vertices[i+12].u = (uOffsets[i/4] + 0.5f) / w;
			radiosity_vcs_vertices[i+12].v = (vOffsets[i/4] + 0.5f) / h;
			break;
		case 1:
			radiosity_vcs_vertices[i+12].u = (uOffsets[i/4] + 0.5f) / w;
			radiosity_vcs_vertices[i+12].v = (h + vOffsets[i/4] + 0.5f) / h;
			break;
		case 2:
			radiosity_vcs_vertices[i+12].u = (w + uOffsets[i/4] + 0.5f) / w;
			radiosity_vcs_vertices[i+12].v = (vOffsets[i/4] + 0.5f) / h;
			break;
		case 3:
			radiosity_vcs_vertices[i+12].u = (w + uOffsets[i/4] + 0.5f) / w;
			radiosity_vcs_vertices[i+12].v = (h + vOffsets[i/4] + 0.5f) / h;
			break;
		}
	}
}

void
CPostEffects::Radiosity_VCS(int limit, int intensity)
{
	int i;
	int resMult = config->trailsResolution;
	RwRaster *fb;
	RwRaster *fb1, *fb2, *tmp;

	fb = RwCameraGetRaster(Scene.camera);
	if(s_vcsRadLastWidth != fb->width || s_vcsRadLastHeight != fb->height || s_vcsRadLastConfigRes != resMult){
		Radiosity_VCS_init();
		s_vcsRadLastWidth = fb->width;
		s_vcsRadLastHeight = fb->height;
		s_vcsRadLastConfigRes = resMult;
		// Defer to next frame: RADIOSITY_VCS rasters are CAMERATEXTURE and
		// need their D3D9 surfaces created by SMAATryInitRasters() (called
		// from RenderScene_before, outside the main camera's BeginUpdate).
		// Bail now; SMAATryInitRasters will init them, then next frame runs.
		s_vcsRadPendingInit = true;
		if(dbglog_throttle("vcs_rad_pending"))
			dbglog("[Radiosity_VCS] SKIP: rasters recreated, pending init, will run next frame");
		return;
	}

	// If rasters were newly created and pending init, skip this frame too
	// (guard against the edge case where the flag wasn't consumed yet)
	if(s_vcsRadPendingInit){
		if(dbglog_throttle("vcs_rad_pending2"))
			dbglog("[Radiosity_VCS] SKIP: rasters still pending init");
		return;
	}

	RwRect r;
	r.x = 0;
	r.y = 0;
	r.w = 256 * resMult;
	r.h = 128 * resMult;

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	RwCameraEndUpdate(Scene.camera);

	RwRasterPushContext(vcs_radiosity_target2);
	RwRasterRenderScaled(fb, &r);
	RwRasterPopContext();

	RwCameraSetRaster(Scene.camera, vcs_radiosity_target2);
	RwCameraBeginUpdate(Scene.camera);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, limit/2, limit/2, limit/2));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices, 4, radiosityIndices, 6);

	fb1 = vcs_radiosity_target1;
	fb2 = vcs_radiosity_target2;
	for(i = 0; i < 4; i++){
		RwD3D9SetRenderTarget(0, fb1);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices+8, 4, radiosityIndices, 6);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, fb2);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		if((i % 2) == 0)
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices+12, 4*4, vcsIndices1, 6*7);
		else
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices+28, 4*4, vcsIndices1, 6*7);

		tmp = fb1;
		fb1 = fb2;
		fb2 = tmp;
	}

	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, fb);
	RwCameraBeginUpdate(Scene.camera);

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, fb2);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, intensity*4, intensity*4, intensity*4));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices+4, 4, radiosityIndices, 6);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, radiosity_vcs_vertices+4, 4, radiosityIndices, 6);

	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	CPostEffects::ImmediateModeRenderStatesReStore();
}

RwD3D9Vertex blur_vcs_vertices[24];
RwImVertexIndex blur_vcs_Indices[] = {
	0, 1, 2, 2, 1, 3,
	4, 5, 6, 6, 5, 7,
	8, 9, 10, 10, 9, 11,
};
RwRaster *lastFrameBuffer;
RwRGBA vcsblurrgb;
RwRGBA rgbTweak;

#define BLUROFFSET (2.1f)
#define BLURINTENSITY (39.0f)

void
CPostEffects::Blur_VCS(void)
{
	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("Blur_VCS: pRasterFrontBuffer is NULL, skipping");
		return;
	}
	static int lastWidth, lastHeight;
	static int justInitialized;
	int i;
	int bufw, bufh;
	int screenw, screenh;
	int intensity;
	bufw = CPostEffects::pRasterFrontBuffer->width;
	bufh = CPostEffects::pRasterFrontBuffer->height;

	/*if(GetAsyncKeyState(VK_F7) & 0x8000){
		justInitialized = 1;
		return;
	}*/

	if(lastWidth != bufw || lastHeight != bufh){
		if(lastFrameBuffer)
			RwRasterDestroy(lastFrameBuffer);
		lastFrameBuffer = RwRasterCreate(bufw, bufh, CPostEffects::pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);
		justInitialized = 1;
		lastWidth = bufw;
		lastHeight = bufh;
	}

	screenw = RwCameraGetRaster(Scene.camera)->width;
	screenh = RwCameraGetRaster(Scene.camera)->height;

	makequad(blur_vcs_vertices, screenw, screenh, bufw, bufh);
	for(i = 0; i < 4; i++)
		blur_vcs_vertices[i].x += BLUROFFSET;
	makequad(blur_vcs_vertices+4, screenw, screenh, bufw, bufh);
	for(i = 4; i < 8; i++){
		blur_vcs_vertices[i].x += BLUROFFSET;
		blur_vcs_vertices[i].y += BLUROFFSET;
	}
	makequad(blur_vcs_vertices+8, screenw, screenh, bufw, bufh);
	for(i = 8; i < 12; i++)
		blur_vcs_vertices[i].y += BLUROFFSET;
	makequad(blur_vcs_vertices+12, screenw, screenh, bufw, bufh);
	for(i = 12; i < 16; i++)
		blur_vcs_vertices[i].emissiveColor = D3DCOLOR_ARGB(0xff, vcsblurrgb.red, vcsblurrgb.green, vcsblurrgb.blue);
	makequad(blur_vcs_vertices+16, screenw, screenh, bufw, bufh);
	makequad(blur_vcs_vertices+20, screenw, screenh, bufw, bufh);
	for(i = 20; i < 24; i++)
		blur_vcs_vertices[i].emissiveColor = 0;

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	// get current frame
	RwCameraEndUpdate(Scene.camera);
	RwRasterPushContext(CPostEffects::pRasterFrontBuffer);
	RwRasterRenderFast(RwCameraGetRaster(Scene.camera), 0, 0);
	RwRasterPopContext();
	RwCameraBeginUpdate(Scene.camera);

	// blur frame
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
	intensity = BLURINTENSITY*0.8f;
	RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, intensity, intensity, intensity));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, blur_vcs_vertices, 12, blur_vcs_Indices, 3*6);

	// add colour filter color
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, blur_vcs_vertices+12, 4, blur_vcs_Indices, 6);

	// blend with last frame
	if(justInitialized)
		justInitialized = 0;
	else{
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, lastFrameBuffer);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
		RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, 32, 32, 32));
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, blur_vcs_vertices+16, 4, blur_vcs_Indices, 6);
	}

	// blend with black. Is this real?
if(0){
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
	RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, 32, 32, 32));
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, blur_vcs_vertices+20, 4, blur_vcs_Indices, 6);
}

	RwCameraEndUpdate(Scene.camera);
	RwRasterPushContext(lastFrameBuffer);
	RwRasterRenderFast(RwCameraGetRaster(Scene.camera), 0, 0);
	RwRasterPopContext();
	RwCameraBeginUpdate(Scene.camera);

	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	CPostEffects::ImmediateModeRenderStatesReStore();
}

/* quad format:
 * 0--3
 * |\ |
 * | \|
 * 1--2 */
void
quadSetXY(RwIm2DVertex *verts, float x0, float y0, float x1, float y1)
{
	RwIm2DVertexSetScreenX(&verts[0], x0);
	RwIm2DVertexSetScreenY(&verts[0], y0);
	RwIm2DVertexSetScreenX(&verts[1], x0);
	RwIm2DVertexSetScreenY(&verts[1], y1);
	RwIm2DVertexSetScreenX(&verts[2], x1);
	RwIm2DVertexSetScreenY(&verts[2], y1);
	RwIm2DVertexSetScreenX(&verts[3], x1);
	RwIm2DVertexSetScreenY(&verts[3], y0);
}

void
quadSetUV(RwIm2DVertex *verts, float u0, float v0, float u1, float v1)
{
	RwIm2DVertexSetU(&verts[0], u0, 1.0f);
	RwIm2DVertexSetV(&verts[0], v0, 1.0f);
	RwIm2DVertexSetU(&verts[1], u0, 1.0f);
	RwIm2DVertexSetV(&verts[1], v1, 1.0f);
	RwIm2DVertexSetU(&verts[2], u1, 1.0f);
	RwIm2DVertexSetV(&verts[2], v1, 1.0f);
	RwIm2DVertexSetU(&verts[3], u1, 1.0f);
	RwIm2DVertexSetV(&verts[3], v0, 1.0f);
}

void
CPostEffects::DrawQuadSetUVs(float utl, float vtl, float utr, float vtr, float ubr, float vbr, float ubl, float vbl)
{
	RwIm2DVertexSetU(&ms_imf.quad_verts[0], utl, ms_imf.recipZ);
	RwIm2DVertexSetV(&ms_imf.quad_verts[0], vtl, ms_imf.recipZ);
	RwIm2DVertexSetU(&ms_imf.quad_verts[1], utr, ms_imf.recipZ);
	RwIm2DVertexSetV(&ms_imf.quad_verts[1], vtr, ms_imf.recipZ);
	RwIm2DVertexSetU(&ms_imf.quad_verts[2], ubl, ms_imf.recipZ);
	RwIm2DVertexSetV(&ms_imf.quad_verts[2], vbl, ms_imf.recipZ);
	RwIm2DVertexSetU(&ms_imf.quad_verts[3], ubr, ms_imf.recipZ);
	RwIm2DVertexSetV(&ms_imf.quad_verts[3], vbr, ms_imf.recipZ);
}

void
CPostEffects::DrawQuadSetDefaultUVs(void)
{
	DrawQuadSetUVs(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
}

void *blurPS, *radiosityPS;

void
CPostEffects::Radiosity_shader(int intensityLimit, int filterPasses, int renderPasses, int intensity)
{
	if(!pRasterFrontBuffer){
		dbglog("Radiosity_shader: pRasterFrontBuffer is NULL, skipping");
		return;
	}
	static RwRaster *workBuffer;
	if(workBuffer)
		if(workBuffer->width != pRasterFrontBuffer->width ||
		   workBuffer->height != pRasterFrontBuffer->height ||
		   workBuffer->depth != pRasterFrontBuffer->depth){
			RwRasterDestroy(workBuffer);
			workBuffer = nil;
		}
	if(workBuffer == nil)
		workBuffer = RwRasterCreate(pRasterFrontBuffer->width, pRasterFrontBuffer->height, pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);

	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);




	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, workBuffer);
	RwCameraBeginUpdate(Scene.camera);

	float params[4];
	params[2] = 1<<filterPasses;
	params[2] *= drawBuffer->width/640.0f;

	overrideIm2dPixelShader = blurPS;
	// Blur vertically
	params[0] = 0;
	params[1] = 1.0f/max(RwRasterGetHeight(pRasterFrontBuffer), 1);
	RwD3D9SetPixelShaderConstant(0, params, 1);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	UpdateFrontBuffer();
	// Blur horizontally
	params[0] = 1.0f/max(RwRasterGetWidth(pRasterFrontBuffer), 1);
	params[1] = 0;
	RwD3D9SetPixelShaderConstant(0, params, 1);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	UpdateFrontBuffer();
	overrideIm2dPixelShader = nil;


	/* Restore original FB */
	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, drawBuffer);
	RwCameraBeginUpdate(Scene.camera);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);

	/* Add to framebuffer */
	params[0] = intensityLimit/255.0f;
	params[1] = intensity/255.0f;
	params[2] = renderPasses;
	RwD3D9SetPixelShaderConstant(0, params, 1);

	float off = ((1<<filterPasses)-1);
	// only for upper left corner actually
	// other one has 2,2 harcoded but since these are 2 by default, we'll reuse them
	float offu = off*m_RadiosityFilterUCorrection;
	float offv = off*m_RadiosityFilterVCorrection;

	float minu = offu;
	float minv = offv;
	float maxu = drawBuffer->width - offu; //off*2;
	float maxv = drawBuffer->height - offv; //off*2;
	float cu = (offu*(drawBuffer->width+0.5f) + offu/*off*2*/*0.5f) / drawBuffer->width;
	float cv = (offv*(drawBuffer->height+0.5f) + offv/*off*2*/*0.5f) / drawBuffer->height;

	params[0] = cu / pRasterFrontBuffer->width;
	params[1] = cv / pRasterFrontBuffer->height;
	params[2] = (maxu-minu) / drawBuffer->width;
	params[3] = (maxv-minv) / drawBuffer->height;
	RwD3D9SetPixelShaderConstant(1, params, 1);

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)!m_bRadiosityDebug);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	overrideIm2dPixelShader = radiosityPS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	UpdateFrontBuffer();
}

void
CPostEffects::Radiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity)
{
	if(!config->radiosityEnable)
		return;
	if(!pRasterFrontBuffer){
		return;
	}
/*
	{
		static bool keystate = false;
		if(GetAsyncKeyState(VK_F5) & 0x8000){
			if(!keystate){
				keystate = true;
				config->radiosity = !config->radiosity;
			}
		}else
			keystate = false;
	}
*/

	if (config->vcsTrails) {
		CPostEffects::Radiosity_VCS(config->trailsLimit, config->trailsIntensity);
		if (config->colorFilter == COLORFILTER_VCS)
			CPostEffects::Blur_VCS();
		return;
	}

	if(!config->doRadiosity)
		return;

	if(config->radiosity == 1){
		Radiosity_shader(intensityLimit, filterPasses, renderPasses, intensity);
		return;
	}

	static RwRaster *workBuffer;
	if(workBuffer)
		if(workBuffer->width != pRasterFrontBuffer->width ||
		   workBuffer->height != pRasterFrontBuffer->height ||
		   workBuffer->depth != pRasterFrontBuffer->depth){
			RwRasterDestroy(workBuffer);
			workBuffer = nil;
		}
	if(workBuffer == nil)
		workBuffer = RwRasterCreate(pRasterFrontBuffer->width, pRasterFrontBuffer->height, pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);

	RwRaster *renderBuffer, *textureBuffer;

	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);

	RwInt32 w = RwRasterGetWidth(drawBuffer);
	RwInt32 h = RwRasterGetHeight(drawBuffer);
	RwReal width = RwRasterGetWidth(pRasterFrontBuffer);
	RwReal height = RwRasterGetHeight(pRasterFrontBuffer);
	float umin, umax, vmin, vmax;

	static RwIm2DVertex verts[4];

	float nearscreen = RwIm2DGetNearScreenZ();
	float nearcam = RwCameraGetNearClipPlane(Scene.camera);
	float recipz = 1.0f/max(nearcam, 1e-7f);
	for(int i = 0; i < 4; i++){
		RwIm2DVertexSetScreenZ(&verts[i], nearscreen);
		RwIm2DVertexSetCameraZ(&verts[i], nearcam);
		RwIm2DVertexSetRecipCameraZ(&verts[i], recipz);
		RwIm2DVertexSetIntRGBA(&verts[i], 255, 255, 255, 255);
	}


	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	renderBuffer = workBuffer;
	textureBuffer  = pRasterFrontBuffer;
	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, renderBuffer);
	RwCameraBeginUpdate(Scene.camera);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)textureBuffer);

	int downsampledwidth = w;
	int downsampledheight = h;

	// First step: Downsample
	for(int i = 0; i < filterPasses; i++){
		umin = (m_RadiosityFilterUCorrection + 0.5f)/width;
		umax = (downsampledwidth + 0.5f)/width;
		vmin = (m_RadiosityFilterVCorrection + 0.5f)/height;
		vmax = (downsampledheight + 0.5f)/height;

		downsampledwidth /= 2;
		downsampledheight /= 2;

		quadSetUV(verts, umin, vmin, umax, vmax);
		quadSetXY(verts, 0.0f, 0.0f, downsampledwidth+1, downsampledheight+1);

		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

		// Switch buffers
		RwRaster *tmp = renderBuffer;
		renderBuffer = textureBuffer;
		textureBuffer = tmp;
		RwD3D9SetRenderTarget(0, renderBuffer);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)textureBuffer);
	}

	// Second step: Subtract intensity value
	umin = (0 + 0.5f)/width;
	umax = (downsampledwidth+1 + 0.5f)/width;
	vmin = (0 + 0.5f)/height;
	vmax = (downsampledheight+1 + 0.5f)/height;

	quadSetUV(verts, umin, vmin, umax, vmax);
	quadSetXY(verts, 0.0f, 0.0f, downsampledwidth+1, downsampledheight+1);

	// D = 2*D - limit
	// We do 2*(D - limit/2) because the fixed function combiners can't do the above
	int limit = intensityLimit*128/255;
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_SUBTRACT);
	RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
	RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CONSTANT);
	RwD3D9SetTextureStageState(1, D3DTSS_CONSTANT, D3DCOLOR_ARGB(255, limit, limit, limit));
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_ADD);
	RwD3D9SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
	RwD3D9SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);

	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);

	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, drawBuffer);
	RwCameraBeginUpdate(Scene.camera);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)renderBuffer);

	// Third step: add to framebuffer
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)!m_bRadiosityDebug);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	umin = (0 + 0.5f)/width;
	umax = (downsampledwidth + 0.5f)/width;
	vmin = (0 + 0.5f)/height;
	vmax = (downsampledheight + 0.5f)/height;
	quadSetUV(verts, umin, vmin, umax, vmax);
	quadSetXY(verts, 0.0f, 0.0f, w, h);
	// Use lower intensity and proper alpha blending
	RwIm2DVertexSetIntRGBA(&verts[0], 255, 255, 255, intensity/4);
	RwIm2DVertexSetIntRGBA(&verts[1], 255, 255, 255, intensity/4);
	RwIm2DVertexSetIntRGBA(&verts[2], 255, 255, 255, intensity/4);
	RwIm2DVertexSetIntRGBA(&verts[3], 255, 255, 255, intensity/4);
	// Single pass instead of multiple additive passes
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);


	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	UpdateFrontBuffer();
}

void
CPostEffects::DarknessFilter_fix(uint8 alpha)
{
	DarknessFilter(alpha);
	UpdateFrontBuffer();
}

void
CPostEffects::ColourFilter_Generic(RwRGBA rgb1, RwRGBA rgb2, void *ps)
{
	if(dbglog_throttle( "cf_generic"))
		dbglog("[PostFX] ColourFilter_Generic ps=%p pRasterFrontBuffer=%p rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d)",
			ps, CPostEffects::pRasterFrontBuffer, rgb1.red, rgb1.green, rgb1.blue, rgb1.alpha,
			rgb2.red, rgb2.green, rgb2.blue, rgb2.alpha);
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	RwRGBAReal color, color2;
	RwRGBARealFromRwRGBA(&color, &rgb1);
	RwRGBARealFromRwRGBA(&color2, &rgb2);
	RwD3D9SetPixelShaderConstant(0, &color, 1);
	RwD3D9SetPixelShaderConstant(1, &color2, 1);

	overrideIm2dPixelShader = ps;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

void
CPostEffects::ColourFilter_Modern(RwRGBA rgba1, RwRGBA rgba2)
{
	if(dbglog_throttle("cfmodern_enter"))
		dbglog("[PostFX] ColourFilter_Modern ENTER rgba1=(%d,%d,%d,%d) rgba2=(%d,%d,%d,%d) pRFB=%p gradingPS=%p camRas=%p",
			rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha,
			rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha,
			CPostEffects::pRasterFrontBuffer, gradingPS,
			Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL);

	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("[PostFX] WARNING: pRasterFrontBuffer is NULL in ColourFilter_Modern!");
		return;
	}

	// Log render state before postfx
	if(dbglog_throttle("cf_modern_state"))
		dbglog("[PostFX] cf_modern PRE-RENDER pRasFB=%p camRas=%p overridePS=%p gradingPS=%p zTest=%d zWrite=%d",
			CPostEffects::pRasterFrontBuffer,
			Scene.camera ? RwCameraGetRaster(Scene.camera) : 0,
			overrideIm2dPixelShader, gradingPS, -1, -1);

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	if(!Colorcycle::initialised)
		Colorcycle::Initialise();

	GradeColorset cset;
	Colorcycle::Update(&cset);
	Grade red, green, blue;
	red = cset.red;
	green = cset.green;
	blue = cset.blue;

	// Modern grading for PBR: timecycle provides color tint, not brightness multiplier.
	// Vanilla two-pass pipeline (MODULATE2X + ADD) is calibrated for gamma-encoded [0,1] values.
	// PBR outputs linear HDR where values can be 5-20+ (sky, reflections).
	// So we extract the timecycle's COLOR HUE and apply it as a gentle tint around neutral (1.0).
	float a1 = rgba1.alpha/128.0f;
	float a2 = rgba2.alpha/128.0f;
	float raw_r = a1*rgba1.red/255.0f + a2*rgba2.red/255.0f;
	float raw_g = a1*rgba1.green/255.0f + a2*rgba2.green/255.0f;
	float raw_b = a1*rgba1.blue/255.0f + a2*rgba2.blue/255.0f;
	// Luminance of the raw tint — encodes overall brightness level (night=dark, day=bright)
	float luma = 0.299f*raw_r + 0.587f*raw_g + 0.114f*raw_b;
	float inv = (luma > 0.001f) ? 1.0f/luma : 1.0f;
	// Extract color hue from raw, blend toward neutral with TINT_STRENGTH
	// 0.0 = no tint (neutral 1,1,1), 1.0 = full vanilla timecycle color shift
	float TINT_STRENGTH = 0.35f;
	red.r   = 1.0f + (raw_r*inv - 1.0f) * TINT_STRENGTH;
	green.g = 1.0f + (raw_g*inv - 1.0f) * TINT_STRENGTH;
	blue.b  = 1.0f + (raw_b*inv - 1.0f) * TINT_STRENGTH;
	// Subtle brightness from timecycle level (preserves day/night atmosphere)
	float brightness = max(0.75f, min(1.25f, 0.85f + luma * 0.1f));
	red.r   *= brightness;
	green.g *= brightness;
	blue.b  *= brightness;
	// Clamp to prevent extremes
	red.r   = max(0.5f, min(2.0f, red.r));
	green.g = max(0.5f, min(2.0f, green.g));
	blue.b  = max(0.5f, min(2.0f, blue.b));
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;

	if(dbglog_throttle("cf_modern_grading"))
		dbglog("[PostFX] ColourFilter_Modern grading: r=%.3f g=%.3f b=%.3f luma=%.3f bright=%.3f tint=%.1f",
			red.r, green.g, blue.b, luma, brightness, TINT_STRENGTH);

	RwD3D9SetPixelShaderConstant(0, &red, 1);
	RwD3D9SetPixelShaderConstant(1, &green, 1);
	RwD3D9SetPixelShaderConstant(2, &blue, 1);

	if(!gradingPS){
		dbglog("[PostFX] WARNING: gradingPS is NULL! ColourFilter_Modern will render with no shader");
	}

	// Pass 1: Color grading (diagonal matrix multiply)
	overrideIm2dPixelShader = gradingPS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Copy graded linear output to pRasterFrontBuffer for tonemap input
	UpdateFrontBuffer();

	// Pass 2: Hable/Uncharted 2 filmic tonemap + sRGB gamma encode
	if(tonemapPassPS){
		// Re-setup render states for tonemap pass (reads pRasterFrontBuffer)
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// Map GTA SA brightness slider (0-384, default 256) to tonemap exposure
		float brightness = (float)CMenuManager__m_PrefsBrightness;
		float baseExposure = max(0.3f, brightness / 256.0f);  // 256 → 1.0 (neutral)

		// === Timecycle-driven adaptive tonemap ===
		// The timecycle is the authority over tonemapping mood.
		// Every parameter derives from CColourSet fields that timecyc.dat authors control.
		CColourSet &tc = CTimeCycle__m_CurrentColours;
		bool isInterior = (*CGame__currArea != 0);
		bool isCutscene = CCutsceneMgr__ms_running;

		// --- Timecycle signals (normalized 0-1 where applicable) ---
		float tcAmbientLuma  = (0.299f*tc.ambientR + 0.587f*tc.ambientG + 0.114f*tc.ambientB) / 255.0f;
		float tcDirLuma      = (0.299f*tc.directionalR + 0.587f*tc.directionalG + 0.114f*tc.directionalB) / 255.0f;
		float sceneLuma      = tcAmbientLuma + tcDirLuma * 0.5f;
		float shadowNorm     = max(0.0f, min(1.0f, (float)tc.shadowStrength / 255.0f));
		float fogFactor      = max(0.0f, min(1.0f, tc.fogStart / 500.0f));       // less fog = 1, heavy fog = 0
		float clouds         = max(0.0f, min(1.0f, tc.cloudAlpha));
		float sunBright      = max(0.0f, min(2.0f, tc.spriteBrightness));
		float streetLights   = max(0.0f, min(1.0f, tc.lightsOnGroundBrightness));
		float envMult        = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;

		float exposure, toeStrength;
		float gradeContrast, gradeBrightness, gradeLift, gradeCurve;

		// === Unified timecycle-driven adaptive tonemap (outdoor + cutscene + interior) ===
		// The timecycle is the authority — cutscene/interior timecyc.dat entries already
		// encode the correct mood. We use the same adaptive path for everything.
		// Cutscene/interior gets a gentle dampening factor where the sun is pointing at camera.

		// --- Garage/sheltered area detection ---
		// Garages are NOT interiors (currArea==0) but have partial occlusion.
		// CCullZones detect sheltered areas (no rain zones). In garages, there's
		// only skylight from the front opening — reduce exposure to match.
		extern bool CCullZones__PlayerNoRain(void);
		bool isSheltered = CCullZones__PlayerNoRain() && !isInterior;
		float garageDampen = isSheltered ? 0.65f : 1.0f;  // 35% darker in garages

		// --- Exposure: reciprocal of scene brightness + timecycle dampening ---
		// sceneLuma is now normalized 0-1 from timecycle ambient+directional
		// Night (sceneLuma≈0.05): exposure≈1.3 (brighter, lifts shadows)
		// Midday (sceneLuma≈0.35): exposure≈0.7 (darker, prevents blowout)
		float sceneExposure = 1.0f / max(0.50f + sceneLuma * 2.5f, 1e-7f);
		sceneExposure = max(0.50f, min(1.50f, sceneExposure));
		// Carcols env mult nudges ±5%
		float carcolsAdapt = 0.95f + envMult * 0.05f;
		// Bright sun → slightly less exposure (prevent highlight blowout)
		float sunDampen = 1.0f - max(0.0f, min(0.08f, sunBright * 0.05f));
		exposure = baseExposure * sceneExposure * carcolsAdapt * sunDampen * garageDampen;

		// Interior: timecycle encodes the correct mood, no extra dampening

		// --- Toe: shadow lift driven by sceneLuma + timecycle shadow depth ---
		// sceneLuma is normalized 0-1, so toe responds properly to time-of-day.
		// NOTE: In the Hable/Uncharted2 filmic tonemap, LOWER D (toeStrength) values
		// produce MORE shadow lift, not less — the denominator term D*F dominates.
		// The minimum of 0.05 was causing a permanent "gray veil" shadow lift even
		// in bright scenes. Raised to 0.15 for midday to keep blacks solid.
		// Night scenes still get up to 0.35 for gentle shadow detail.
		float toeFromLuma   = 0.25f - sceneLuma * 0.8f;
		float toeFromShadow = shadowNorm * 0.10f;  // deeper shadows → more lift
		toeStrength = max(0.15f, min(0.35f, toeFromLuma + toeFromShadow));

		// --- Grade params: fully timecycle-driven ---
		// Contrast: shadow strength × fog clearance (deep shadows + clear sky = max contrast)
		gradeContrast = 1.10f + shadowNorm * 0.30f * fogFactor;

		// Brightness: street lights provide fill in dark scenes
		gradeBrightness = 0.02f + streetLights * 0.05f;

		// Lift: overcast/cloudy raises blacks slightly; heavy fog also lifts
		gradeLift = clouds * 0.020f + (1.0f - fogFactor) * 0.015f;

		// Curve blend: brighter scenes get more S-curve for depth
		gradeCurve = 0.20f + sceneLuma * 0.30f;

		// Throttled diagnostic
		static unsigned int tonemapLogCounter = 0;
		float blackLift = config ? config->tonemapBlackLift : 0.015f;
		if(tonemapLogCounter++ % 3600 == 0){
			dbglog("[Tonemap] TC sceneLuma=%.3f shadow=%d fog=%.0f cloud=%.2f sun=%.2f street=%.2f cutscene=%d interior=%d",
				sceneLuma, tc.shadowStrength, tc.fogStart, clouds, sunBright, streetLights, isCutscene, isInterior);
			dbglog("[Tonemap] VAL exp=%.3f toe=%.3f bl=%.4f ct=%.2f br=%.3f lift=%.3f curve=%.2f",
				exposure, toeStrength, blackLift, gradeContrast, gradeBrightness, gradeLift, gradeCurve);
		}

		// Pack into c5: {exposure, toeStrength, sceneLuma, flags}
		// flags: bit0=isInterior, bit1=isCutscene
		float tonemapP[4] = { exposure, toeStrength, sceneLuma, (isInterior ? 1.0f : 0.0f) + (isCutscene ? 2.0f : 0.0f) };
		RwD3D9SetPixelShaderConstant(5, tonemapP, 1);

		// Pack into c6: {brightness, contrast, lift, curveBlend} — all timecycle-driven
		float gradeP[4] = { gradeBrightness, gradeContrast, gradeLift, gradeCurve };
		RwD3D9SetPixelShaderConstant(6, gradeP, 1);

		// Pack into c7: {blackLift, 0, 0, 0} — Hable curve black-level lift
		float blackP[4] = { blackLift, 0.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(7, blackP, 1);

		overrideIm2dPixelShader = tonemapPassPS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// Restore all render states (match ColourFilter_PC pattern + FOG=TRUE)
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	// Full D3D9 pipeline cleanup after tonemap pass
	IDirect3DDevice9 *dev = d3d9device;
	if(dev){
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
	}
}

void
CPostEffects::ColourFilter_Mobile(RwRGBA rgba1, RwRGBA rgba2)
{
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	if(!Colorcycle::initialised)
		Colorcycle::Initialise();

	GradeColorset cset;
	Colorcycle::Update(&cset);
	Grade red, green, blue;
	red = cset.red;
	green = cset.green;
	blue = cset.blue;

	// Mobile colors
	float r = rgba1.red + rgba2.red;
	float g = rgba1.green + rgba2.green;
	float b = rgba1.blue + rgba2.blue;
	float invsqrt = 1.0f/max(sqrt(r*r + g*g + b*b), 1e-7f);
	r *= invsqrt;
	g *= invsqrt;
	b *= invsqrt;
	red.r = (1.5f + r*1.732f)*0.4f*red.r;
	green.g = (1.5f + g*1.732f)*0.4f*green.g;
	blue.b = (1.5f + b*1.732f)*0.4f*blue.b;

/*	// Fun trick: PS2 colour filter:
	float a = rgba2.alpha/128.0f;
	red.r = rgba1.red/128.0f + a*rgba2.red/128.0f;
	green.g = rgba1.green/128.0f + a*rgba2.green/128.0f;
	blue.b = rgba1.blue/128.0f + a*rgba2.blue/128.0f;
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;
*/
/*	// Also fun: PC colour filter:
	float a1 = rgba1.alpha/128.0f;
	float a2 = rgba2.alpha/128.0f;
	red.r = 1.0f + a1*rgba1.red/255.0f + a2*rgba2.red/255.0f;
	green.g = 1.0f + a1*rgba1.green/255.0f + a2*rgba2.green/255.0f;
	blue.b = 1.0f + a1*rgba1.blue/255.0f + a2*rgba2.blue/255.0f;
	red.g = red.b = red.a = 0.0f;
	green.r = green.b = green.a = 0.0f;
	blue.r = blue.g = blue.a = 0.0f;
*/


	RwD3D9SetPixelShaderConstant(0, &red, 1);
	RwD3D9SetPixelShaderConstant(1, &green, 1);
	RwD3D9SetPixelShaderConstant(2, &blue, 1);

	// contrast
	float mult[4];
	float add[4];
	mult[0] = red.r + red.g + red.b;
	mult[1] = green.r + green.g + green.b;
	mult[2] = blue.r + blue.g + blue.b;
	mult[3] = 1.0f;
	add[0] = red.a;
	add[1] = green.a;
	add[2] = blue.a;
	add[3] = 0.0f;

	RwD3D9SetPixelShaderConstant(3, mult, 1);
	RwD3D9SetPixelShaderConstant(4, add, 1);

	if(!(GetAsyncKeyState(VK_F5) & 0x8000))
		overrideIm2dPixelShader = gradingPS;
	else
		overrideIm2dPixelShader = contrastPS;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
}

void
CPostEffects::ColourFilter_PS2(RwRGBA rgba1, RwRGBA rgba2)
{
	if(dbglog_throttle( "cf_ps2"))
		dbglog("[PostFX] ColourFilter_PS2 ENTER rgba1=(%d,%d,%d,%d) rgba2=(%d,%d,%d,%d) pRasterFrontBuffer=%p",
			rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha,
			rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha,
			CPostEffects::pRasterFrontBuffer);

	RwIm2DVertex *verts;

	verts = colorfilterVerts;
	// Setup state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

	// Colors are already converted to PC space in ColourFilter_switch
	// Just use them directly with MODULATE2X
	overrideColorMod = D3DTOP_MODULATE2X;
	overrideAlphaMod = D3DTOP_MODULATE2X;

	// First color - replace
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DVertexSetIntRGBA(&verts[0], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[1], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[2], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DVertexSetIntRGBA(&verts[3], rgba1.red, rgba1.green, rgba1.blue, 255);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	if(m_bBlurColourFilter){
		static RwIm2DVertex blurVerts[4];
		float rasterWidth = RwRasterGetWidth(CPostEffects::pRasterFrontBuffer);
		float rasterHeight = RwRasterGetHeight(CPostEffects::pRasterFrontBuffer);
		float scale = RwRasterGetWidth(RwCameraGetRaster(Scene.camera))/640.0f;
		float leftOff   = m_colourLeftUOffset*scale   / 16.0f / rasterWidth;
		float rightOff  = m_colourRightUOffset*scale  / 16.0f / rasterWidth;
		float topOff    = m_colourTopVOffset*scale    / 16.0f / rasterHeight;
		float bottomOff = m_colourBottomVOffset*scale / 16.0f / rasterHeight;
		memcpy(blurVerts, verts, sizeof(blurVerts));
		RwIm2DVertexSetU(&blurVerts[0], RwIm2DVertexGetU(&blurVerts[0]) + leftOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[1], RwIm2DVertexGetU(&blurVerts[1]) + leftOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[2], RwIm2DVertexGetU(&blurVerts[2]) + rightOff, 1.0f);
		RwIm2DVertexSetU(&blurVerts[3], RwIm2DVertexGetU(&blurVerts[3]) + rightOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[0], RwIm2DVertexGetV(&blurVerts[0]) + topOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[3], RwIm2DVertexGetV(&blurVerts[3]) + topOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[1], RwIm2DVertexGetV(&blurVerts[1]) + bottomOff, 1.0f);
		RwIm2DVertexSetV(&blurVerts[2], RwIm2DVertexGetV(&blurVerts[2]) + bottomOff, 1.0f);
		verts = blurVerts;
	}

	// Second color - add
	// Colors are already converted to PC space in ColourFilter_switch
	uint8 r2 = rgba2.red;
	uint8 g2 = rgba2.green;
	uint8 b2 = rgba2.blue;
	uint8 a2 = rgba2.alpha;

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwIm2DVertexSetIntRGBA(&verts[0], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[1], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[2], r2, g2, b2, a2);
	RwIm2DVertexSetIntRGBA(&verts[3], r2, g2, b2, a2);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
 	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	// Restore state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

	overrideColorMod = -1;
	overrideAlphaMod = -1;
}

/* For reference only */
#if 0
void
CPostEffects::ColourFilter_PC(RwRGBA rgba1, RwRGBA rgba2)
{
	RwIm2DVertex *verts;

	verts = colorfilterVerts;
	// Setup state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

	// First color
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DVertexSetIntRGBA(&verts[0], rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha);
	RwIm2DVertexSetIntRGBA(&verts[1], rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha);
	RwIm2DVertexSetIntRGBA(&verts[2], rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha);
	RwIm2DVertexSetIntRGBA(&verts[3], rgba1.red, rgba1.green, rgba1.blue, rgba1.alpha);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	// Second color
	RwIm2DVertexSetIntRGBA(&verts[0], rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha);
	RwIm2DVertexSetIntRGBA(&verts[1], rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha);
	RwIm2DVertexSetIntRGBA(&verts[2], rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha);
	RwIm2DVertexSetIntRGBA(&verts[3], rgba2.red, rgba2.green, rgba2.blue, rgba2.alpha);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);

	// Restore state
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}
#endif

void
CPostEffects::SetFilterMainColour_PS2(RwRaster *raster, RwRGBA color)
{
//	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[0], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[1], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[2], color.red, color.green, color.blue, color.alpha);
	RwIm2DVertexSetIntRGBA(&colorfilterVerts[3], color.red, color.green, color.blue, color.alpha);
	overrideColorMod = D3DTOP_MODULATE2X;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideColorMod = -1;

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
}

void
CPostEffects::InfraredVision_PS2(RwRGBA c1, RwRGBA c2)
{
	if(config->infraredVision != 0){
		InfraredVision(c1, c2);
		return;
	}

	CPostEffects::ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();

	float r = m_fInfraredVisionFilterRadius;
	// not sure this scales correctly, but it looks ok (need better brain)
	float ru = r * RsGlobal->MaximumWidth  / RwRasterGetWidth(ms_imf.frontBuffer)  * 1024.0f / 640.0f;
	float rv = r * RsGlobal->MaximumHeight / RwRasterGetHeight(ms_imf.frontBuffer) * 512.0f  / 448.0f;
	float uoff[4] = { -ru, ru, ru, -ru };
	float voff[4] = { -rv, -rv, rv, rv };

	// PS2 draws the filter triangle triangle...we draw the quad
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	for(int i = 0; i < 4; i++){
		DrawQuadSetUVs(ms_imf.tri_umin + uoff[i], ms_imf.tri_vmin + voff[i],
		               ms_imf.tri_umax + uoff[i], ms_imf.tri_vmin + voff[i],
		               ms_imf.tri_umax + uoff[i], ms_imf.tri_vmax + voff[i],
		               ms_imf.tri_umin + uoff[i], ms_imf.tri_vmax + voff[i]);
		DrawQuad(0, 0, RwRasterGetWidth(ms_imf.frontBuffer)*2, RwRasterGetHeight(ms_imf.frontBuffer)*2,
		                       c1.red, c1.green, c1.blue, 0xFFu, ms_imf.frontBuffer);

		UpdateFrontBuffer();
	}
	DrawQuadSetDefaultUVs();
	ImmediateModeRenderStatesReStore();

	SetFilterMainColour_PS2(ms_imf.frontBuffer, c2);
	UpdateFrontBuffer();
}

void
CPostEffects::NightVision_PS2(RwRGBA color)
{
	if(config->nightVision != 0){
		CPostEffects::NightVision(color);
		return;
	}

	if(CPostEffects::m_fNightVisionSwitchOnFXCount > 0.0f){
		CPostEffects::m_fNightVisionSwitchOnFXCount -= CTimer__ms_fTimeStep;
		if(CPostEffects::m_fNightVisionSwitchOnFXCount <= 0.0f)
			CPostEffects::m_fNightVisionSwitchOnFXCount = 0.0f;
		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		int n = CPostEffects::m_fNightVisionSwitchOnFXCount;
		while(n--)
		        CPostEffects::DrawQuad(0.0f, 0.0f,
				RwRasterGetWidth(ms_imf.frontBuffer), RwRasterGetHeight(ms_imf.frontBuffer),
				8, 8, 8, 255, ms_imf.frontBuffer);
		CPostEffects::ImmediateModeRenderStatesReStore();
	}

	UpdateFrontBuffer();
	CPostEffects::SetFilterMainColour_PS2(ms_imf.frontBuffer, color);
	UpdateFrontBuffer();
}


// VU style random number generator -- taken from pcsx2
uint R;
void vrinit(uint x){ R = 0x3F800000 | x & 0x007FFFFF; }
void vradvance(void){
	int x = (R >> 4) & 1;
	int y = (R >> 22) & 1;
	R <<= 1;
	R ^= x ^ y;
	R = (R&0x7fffff)|0x3f800000;
}
inline uint vrget(void){ return R; }
inline uint vrnext(void){ vradvance(); return R; }

void
CPostEffects::Grain_PS2(int strength, bool generate)
{
	if(!config->grainEnable)
		return;
	if(config->grainFilter != 0){
		CPostEffects::Grain(strength, generate);
		return;
	}

	if(generate){
		RwUInt8 *pixels = RwRasterLock(grainRaster, 0, 1);
		vrinit(rand());
		int x = vrget();
		for(int i = 0; i < 64*64; i++){
			*pixels++ = x;
			*pixels++ = x;
			*pixels++ = x;
			*pixels++ = x & strength;
			x = vrnext();
		}
		RwRasterUnlock(grainRaster);
	}

	ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	float umin = 0.0f;
	float vmin = 0.0f;
	float umax = 5.0f * RsGlobal->MaximumWidth/640.0f;
	float vmax = 7.0f * RsGlobal->MaximumHeight/448.0f;

	DrawQuadSetUVs(umin, vmin,
		umax, vmin,
		umax, vmax,
		umin, vmax);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)D3DBLEND_DESTCOLOR);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)D3DBLEND_SRCALPHA);

	overrideColorMod = D3DTOP_SELECTARG2;	// ignore texture color
	overrideAlphaMod = D3DTOP_MODULATE2X;
	CPostEffects::DrawQuad(0.0, 0.0, RsGlobal->MaximumWidth, RsGlobal->MaximumHeight,
	                       0xFFu, 0xFFu, 0xFFu, 0xFF, grainRaster);
	overrideColorMod = -1;
	overrideAlphaMod = -1;

	DrawQuadSetDefaultUVs();
	CPostEffects::ImmediateModeRenderStatesReStore();
}

void DrawNormalBufferToTexture(void);
void DrawPipeChain(void);

void
CPostEffects::ColourFilter_switch(RwRGBA rgb1, RwRGBA rgb2)
{
	DBGLOG_ENTER("ColourFilter_switch");
	// Log entry with full context for crash correlation
	if(dbglog_throttle("cf_switch"))
		dbglog("[PostFX] ColourFilter_switch: filter=%d pipeline=%d smaa=%d ssao=%d motionBlur=%d",
			config->colorFilter, config->pipeline, config->smaaEnable,
			config->ssaoEnable, config->motionBlurEnable);

	if(!CPostEffects::pRasterFrontBuffer){
		DBGLOG_BAIL("ColourFilter_switch", "pRasterFrontBuffer NULL");
		return;
	}

	// Debug dump: log colorfilterVerts once, increment frame counter
	LogColorfilterVerts();
	g_postfxDumpFrame++;

	// Generate normal buffer from stereo disparity (before SSAO)
	if(config->normalBufferEnable){
		PERF_SCOPE("NormalBuf");
		DrawNormalBufferToTexture();
	}

	// Velocity buffer: per-pixel motion vectors via depth reconstruction
	// Must run before SSAO so the velocity texture is available for temporal reprojection
	DrawVelocityBuffer();

	// SSAO must run before color filter to read original scene
	{
		PERF_SCOPE("SSAO");
		if(config->ssaoEnable && SSAO_Temporal && config->ssaoTemporalEnable){
			DrawSSAO_Overhaul();    // Quarter-res temporal SSAO
		}else{
			DrawSSAO();             // Fallback: old full-res SSAO
		}
	}

	// 4-Pipe chain (after SSAO, uses normal buffer)
	if(config->pipeChainEnable && config->normalBufferEnable){
		PERF_SCOPE("PipeChain");
		DrawPipeChain();
	}

	{
		static bool keystate = false;
		if(GetAsyncKeyState(config->keys[0]) & 0x8000){
			if(!keystate){
				keystate = true;
				if(numConfigs){
					currentConfig = (currentConfig+1) % numConfigs;
					CMessages__AddMessageJumpQWithNumber("skygfx~1~.ini", 500, 0, currentConfig + 1, -1, -1, -1, -1, -1, false);
					setConfig();
				}
			}
		}else
			keystate = false;
	}

	{
		static bool keystate = false;
		if(GetAsyncKeyState(config->keys[1]) & 0x8000){
			if(!keystate){
				keystate = true;
				reloadAllInis();
			}
		}else
			keystate = false;
	}

	RwRGBA rgb1pc = rgb1;
	RwRGBA rgb2pc = rgb2;

	// PS2 to PC color space conversion
	// PS2 gamma ~1.5, PC gamma 2.2
	// PS2 uses MODULATE2X (doubles brightness)
	// PS2 alpha range: 0-128, PC: 0-255
	//
	// For color filter values (used with MODULATE2X):
	//   Scale by 0.34 (0.68 gamma * 0.5 mod2x)
	//
	// For sun/ambient/high-intensity values:
	//   Use softer curve to prevent banding
	//   Apply sqrt-based compression for high values
	static const float PS2_TO_PC_GAMMA = 0.68f;
	static const float MODULATE2X_COMPENSATION = 0.5f;
	static const float TOTAL_CORRECTION = PS2_TO_PC_GAMMA * MODULATE2X_COMPENSATION;

	// Soft compression for high-intensity values (sun, bright lights)
	// Prevents banding by compressing the upper range
	auto SoftCompress = [](uint8 val) -> uint8 {
		float f = val / 255.0f;
		// Apply sqrt-based compression for high values
		// This preserves detail in bright areas while preventing banding
		static const float SOFT_COMPRESS_THRESHOLD = 0.5f;
		static const float SQRT_COMPRESS_THRESHOLD = 0.7f;
		if(f > SQRT_COMPRESS_THRESHOLD){
			f = 0.5f + (f - 0.5f) * 0.7f; // Compress upper range
		}
		f *= PS2_TO_PC_GAMMA; // Apply gamma correction
		return (uint8)(f * 255.0f);
	};

	if(config->usePCTimecyc || config->pipeline == PIPELINE_PBR){
		// PC timecycle - values already in PC space
		rgb1.alpha /= 2;
		rgb2.alpha /= 2;
	}else{
		// PS2 timecycle - convert to PC space
		// Use soft compression for color filter values
		rgb1.red = SoftCompress(rgb1.red);
		rgb1.green = SoftCompress(rgb1.green);
		rgb1.blue = SoftCompress(rgb1.blue);
		rgb2.red = SoftCompress(rgb2.red);
		rgb2.green = SoftCompress(rgb2.green);
		rgb2.blue = SoftCompress(rgb2.blue);

		// Apply MODULATE2X compensation
		rgb1.red = (uint8)(rgb1.red * MODULATE2X_COMPENSATION);
		rgb1.green = (uint8)(rgb1.green * MODULATE2X_COMPENSATION);
		rgb1.blue = (uint8)(rgb1.blue * MODULATE2X_COMPENSATION);
		rgb2.red = (uint8)(rgb2.red * MODULATE2X_COMPENSATION);
		rgb2.green = (uint8)(rgb2.green * MODULATE2X_COMPENSATION);
		rgb2.blue = (uint8)(rgb2.blue * MODULATE2X_COMPENSATION);

		// Clamp to prevent overflow
		rgb1.red = min(rgb1.red, (uint8)255);
		rgb1.green = min(rgb1.green, (uint8)255);
		rgb1.blue = min(rgb1.blue, (uint8)255);
		rgb2.red = min(rgb2.red, (uint8)255);
		rgb2.green = min(rgb2.green, (uint8)255);
		rgb2.blue = min(rgb2.blue, (uint8)255);

		// Convert alpha from PS2 range (0-128) to PC range (0-255)
		if(rgb1.alpha >= 128)
			rgb1.alpha = 255;
		else
			rgb1.alpha = (uint8)(rgb1.alpha * 2.0f);
		if(rgb2.alpha >= 128)
			rgb2.alpha = 255;
		else
			rgb2.alpha = (uint8)(rgb2.alpha * 2.0f);

		// Also fix PC variants
		rgb1pc.red = SoftCompress(rgb1pc.red);
		rgb1pc.green = SoftCompress(rgb1pc.green);
		rgb1pc.blue = SoftCompress(rgb1pc.blue);
		rgb2pc.red = SoftCompress(rgb2pc.red);
		rgb2pc.green = SoftCompress(rgb2pc.green);
		rgb2pc.blue = SoftCompress(rgb2pc.blue);

		rgb1pc.red = (uint8)(rgb1pc.red * MODULATE2X_COMPENSATION);
		rgb1pc.green = (uint8)(rgb1pc.green * MODULATE2X_COMPENSATION);
		rgb1pc.blue = (uint8)(rgb1pc.blue * MODULATE2X_COMPENSATION);
		rgb2pc.red = (uint8)(rgb2pc.red * MODULATE2X_COMPENSATION);
		rgb2pc.green = (uint8)(rgb2pc.green * MODULATE2X_COMPENSATION);
		rgb2pc.blue = (uint8)(rgb2pc.blue * MODULATE2X_COMPENSATION);

		rgb1pc.red = min(rgb1pc.red, (uint8)255);
		rgb1pc.green = min(rgb1pc.green, (uint8)255);
		rgb1pc.blue = min(rgb1pc.blue, (uint8)255);
		rgb2pc.red = min(rgb2pc.red, (uint8)255);
		rgb2pc.green = min(rgb2pc.green, (uint8)255);
		rgb2pc.blue = min(rgb2pc.blue, (uint8)255);

		if(rgb1pc.alpha >= 128)
			rgb1pc.alpha = 255;
		else
			rgb1pc.alpha = (uint8)(rgb1pc.alpha * 2.0f);
		if(rgb2pc.alpha >= 128)
			rgb2pc.alpha = 255;
		else
			rgb2pc.alpha = (uint8)(rgb2pc.alpha * 2.0f);
	}

	rgb1.red *= config->rgb1Mult;
	rgb1.green *= config->rgb1Mult;
	rgb1.blue *= config->rgb1Mult;

	rgb2.red *= config->rgb2Mult;
	rgb2.green *= config->rgb2Mult;
	rgb2.blue *= config->rgb2Mult;

	vcsblurrgb = rgb2;

	int colorFilter = config->colorFilter;

	// Debug toggle: bypass colour filter entirely
	if(!config->colorFilterEnable){
		if(dbglog_throttle("cf_switch"))
			dbglog("[PostFX] ColourFilter_switch BYPASSED (colorFilterEnable=0)");
		UpdateFrontBuffer();
		// SSS still runs even when colour filter is bypassed - it's independent
		chars_drawSSSBlur();
		return;
	}

	// PBR pipeline always uses Modern colour filter (Hable filmic tonemap in PostFX)
	if(config->pipeline == PIPELINE_PBR)
		colorFilter = COLORFILTER_MODERN;

	// VCS trails isn't compatible with PC/PS2 color filter, falls off to VCS color filter
	if (config->vcsTrails) {
		if (colorFilter == COLORFILTER_PC || colorFilter == COLORFILTER_PS2) {
			colorFilter = COLORFILTER_VCS;
		}
	}

	if(dbglog_throttle( "cf_switch"))
		dbglog("[PostFX] ColourFilter_switch filter=%d pipeline=%d rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d) pRasterFrontBuffer=%p",
			colorFilter, config->pipeline,
			rgb1.red, rgb1.green, rgb1.blue, rgb1.alpha,
			rgb2.red, rgb2.green, rgb2.blue, rgb2.alpha,
			CPostEffects::pRasterFrontBuffer);

	switch(colorFilter){
	case COLORFILTER_NONE:
		// Fall back to PC filter (same as COLORFILTER_PC)
		CPostEffects::ColourFilter(rgb1pc, rgb2pc);
		break;
	case COLORFILTER_PS2:
		CPostEffects::ColourFilter_PS2(rgb1, rgb2);
		break;
	case COLORFILTER_PC:
		CPostEffects::ColourFilter(rgb1pc, rgb2pc);
		break;
	case COLORFILTER_MOBILE:
		if(!UG_mod)
			CPostEffects::ColourFilter_Mobile(rgb1, rgb2);
		break;
	case COLORFILTER_III:
		CPostEffects::ColourFilter_Generic(rgb1pc, rgb2pc, iiiTrailsPS);
		break;
	case COLORFILTER_VC:
		CPostEffects::ColourFilter_Generic(rgb1, rgb2, vcTrailsPS);
		break;
	case COLORFILTER_VCS:
		CPostEffects::ColourFilter_Generic(rgb1, rgb2, vcTrailsPS);
		break;
	case COLORFILTER_MODERN:
		// CRITICAL: Copy camera raster to pRasterFrontBuffer BEFORE grading.
		UpdateFrontBuffer();

		ColourFilter_Modern(rgb1, rgb2);
		break;
	case COLORFILTER_GTAIV:
		// Bypass mode - no color filter applied.
		// GTAIV filter removed - we rely on SA's own timecycle/carcols values.
		// CRITICAL: unbind shaders to prevent UI corruption.
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
		RwD3D9SetTexture(NULL, 0);
		RwD3D9SetTexture(NULL, 1);
		break;
	default:
		return;
	}

	// Per-frame summary report (one compact line per frame)
	postfxReportFrame++;
	postfxReportSummary();

	// Motion blur: after colour filter + SMAA, before final front buffer sync
	{
		PERF_SCOPE("MotionBlur");
		DrawMotionBlur();
	}

	// Atmospheric effects: after colour filter + motion blur, before front buffer sync
	{
		PERF_SCOPE("HeightFog");
		DrawHeightFog();
	}
	{
		PERF_SCOPE("GodRays");
		DrawGodRays();
	}

	UpdateFrontBuffer();
	// SSS post-process blur: runs after colour filter
	// Reads from pRasterFrontBuffer (colour-filtered scene), writes to camera raster.
	chars_drawSSSBlur();

	if(dbglog_throttle("cf_done"))
		dbglog("ColourFilter_switch: done (filter=%d)", colorFilter);

	// Debug dump: final image after all post-processing
	DumpCurrentRT("after_filter");

	//static int doramp = 0;
	//{
	//	static bool keystate = false;
	//	if(GetAsyncKeyState(VK_F4) & 0x8000){
	//		if(!keystate){
	//			doramp = !doramp;
	//			keystate = true;
	//		}
	//	}else
	//		keystate = false;
	//}
	//if(doramp)
	//	renderRamp();
}

static RwMatrix RGB2YUV = {
	{  0.299f,	-0.168736f,	 0.500f }, 0,
	{  0.587f,	-0.331264f,	-0.418688f }, 0,
	{  0.114f,	 0.500f,	-0.081312f }, 0,
	{  0.000f,	 0.000f,	 0.000f }, 0,
};

static RwMatrix YUV2RGB = {
	{  1.000f,	 1.000f,	 1.000f }, 0,
	{  0.000f,	-0.344136f,	 1.772f }, 0,
	{  1.402f,	-0.714136f,	 0.000f }, 0,
	{  0.000f,	 0.000f,	 0.000f }, 0,
};

void
CPostEffects::DrawFinalEffects(void)
{
	// SMAA: pure D3D9 RT switching — safe regardless of camera Begin/EndUpdate state.
	// Runs before ImGui so the debug menu stays on top (test requires menu open).
	// Sync front buffer first so HUD (already drawn into camera raster) survives
	// SMAA's full-frame repaint from pRasterFrontBuffer.
	if(config->smaaEnable && SMAA_Edge){
		if(dbglog_throttle("smaa_entry"))
			dbglog("[SMAA] DrawFinalEffects: entering SMAA (Edge=%p Temporal=%p)", SMAA_Edge, SMAA_Temporal);
		UpdateFrontBuffer();
		DrawSMAA();
	}

	// ImGui debug menu: always render last (on top of everything)
	{
		IDirect3DDevice9 *dev = d3d9device;
		if(dev && config->debugMenuOpen)
			DrawUnifiedDebugMenu(dev);
	}

	// YCbCr filter (only when enabled)
	if(m_bYCbCrFilter){
		UpdateFrontBuffer();

		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		RwMatrix m = RGB2YUV;

		RwMatrix m2;
		m2.right.x = m_lumaScale;
		m2.up.x = 0.0f;
		m2.at.x = 0.0f;
		m2.pos.x = m_lumaOffset;
		m2.right.y = 0.0f;
		m2.up.y = m_cbScale;
		m2.at.y = 0.0f;
		m2.pos.y = m_cbOffset;
		m2.right.z = 0.0f;
		m2.up.z = 0.0f;
		m2.at.z = m_crScale;
		m2.pos.z = m_crOffset;

		RwMatrixOptimize(&m2, nil);

		RwMatrixTransform(&m, &m2, rwCOMBINEPOSTCONCAT);
		RwMatrixTransform(&m, &YUV2RGB, rwCOMBINEPOSTCONCAT);
		Grade red, green, blue;
		red.r = m.right.x;
		red.g = m.up.x;
		red.b = m.at.x;
		red.a = m.pos.x;
		green.r = m.right.y;
		green.g = m.up.y;
		green.b = m.at.y;
		green.a = m.pos.y;
		blue.r = m.right.z;
		blue.g = m.up.z;
		blue.b = m.at.z;
		blue.a = m.pos.z;

		RwD3D9SetPixelShaderConstant(0, &red, 1);
		RwD3D9SetPixelShaderConstant(1, &green, 1);
		RwD3D9SetPixelShaderConstant(2, &blue, 1);

		float tonemapP[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(5, tonemapP, 1);

		overrideIm2dPixelShader = gradingPS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);

		UpdateFrontBuffer();
	}

	// SMAA runs at top of DrawFinalEffects (before ImGui) via D3D9 SetRenderTarget.
}

IDirect3DTexture9 *g_ssaoDepthTex = NULL;
static IDirect3DSurface9 *g_ssaoDepthSurf = NULL;
static IDirect3DTexture9 *g_ssaoNoiseTex = NULL;
static RwRaster *g_ssaoOutputRaster = NULL;

// SSAO overhaul — quarter-res temporal pipeline
static RwRaster *g_ssaoQuarterRaster[2] = {NULL, NULL};
static RwTexture *g_ssaoQuarterTexRW[2] = {NULL, NULL};
static RwRaster *g_ssaoBlurTempRaster = NULL;
static RwTexture *g_ssaoBlurTempTexRW = NULL;
static int g_ssaoFrameIndex = 0;
static bool g_ssaoHistoryValid = false;
static int g_ssaoQuarterW = 0, g_ssaoQuarterH = 0;

// SMAA D3D textures (D3DPOOL_DEFAULT - must be released on device reset)
static IDirect3DTexture9 *g_smaaAreaTex = NULL;
static IDirect3DTexture9 *g_smaaSearchTex = NULL;

// IBL buffer (quarter-res sky/cloud ambient)
IDirect3DTexture9 *g_iblTex = NULL;
static IDirect3DSurface9 *g_iblSurf = NULL;

extern void *DynamicSky;

// Normal buffer (half-res stereo-derived normals)
IDirect3DTexture9 *g_normalBufferTex = NULL;
static IDirect3DSurface9 *g_normalBufferSurf = NULL;

extern void *NormalBufferShader;

// 4-Pipe chain
static IDirect3DTexture9 *g_pipeChainTexA = NULL;
static IDirect3DSurface9 *g_pipeChainSurfA = NULL;
static IDirect3DTexture9 *g_pipeChainTexB = NULL;
static IDirect3DSurface9 *g_pipeChainSurfB = NULL;

extern void *PipeChainShader;

// Release all D3DPOOL_DEFAULT resources (call on device lost/reset)
void ReleaseDefaultPoolResources(void)
{
	dbglog("ReleaseDefaultPoolResources: releasing...");

	// g_ssaoDepthTex is aliased to depthhook's g_intzTex when depthhook is active.
	// Only release if we own it (not aliased). DepthHook_ReleaseResources() below
	// handles the aliased case.
	if(g_ssaoDepthTex && g_ssaoDepthTex != g_intzTex){ g_ssaoDepthTex->Release(); g_ssaoDepthTex = NULL; }
	if(g_ssaoDepthSurf && g_ssaoDepthSurf != g_intzSurf){ g_ssaoDepthSurf->Release(); g_ssaoDepthSurf = NULL; }
	// Note: g_ssaoNoiseTex is D3DPOOL_MANAGED, survives reset

	// SSAO output raster (RW-managed)
	if(g_ssaoOutputRaster){ RwRasterDestroy(g_ssaoOutputRaster); g_ssaoOutputRaster = nil; }

	// VCS blur last-frame buffer
	if(lastFrameBuffer){ RwRasterDestroy(lastFrameBuffer); lastFrameBuffer = nil; }
	
	// SMAA area/search textures (D3DPOOL_DEFAULT)
	if(g_smaaAreaTex){ g_smaaAreaTex->Release(); g_smaaAreaTex = NULL; }
	if(g_smaaSearchTex){ g_smaaSearchTex->Release(); g_smaaSearchTex = NULL; }

	// VCS radiosity rasters (RW-managed, need explicit destroy + static reset)
	ReleaseVCSRadiosityResources();

	// IBL buffer (D3DPOOL_DEFAULT)
	if(g_iblTex){ g_iblTex->Release(); g_iblTex = NULL; }
	if(g_iblSurf){ g_iblSurf->Release(); g_iblSurf = NULL; }

	// Normal buffer (D3DPOOL_DEFAULT)
	if(g_normalBufferTex){ g_normalBufferTex->Release(); g_normalBufferTex = NULL; }
	if(g_normalBufferSurf){ g_normalBufferSurf->Release(); g_normalBufferSurf = NULL; }

	// Pipe chain (D3DPOOL_DEFAULT)
	if(g_pipeChainTexA){ g_pipeChainTexA->Release(); g_pipeChainTexA = NULL; }
	if(g_pipeChainSurfA){ g_pipeChainSurfA->Release(); g_pipeChainSurfA = NULL; }
	if(g_pipeChainTexB){ g_pipeChainTexB->Release(); g_pipeChainTexB = NULL; }
	if(g_pipeChainSurfB){ g_pipeChainSurfB->Release(); g_pipeChainSurfB = NULL; }

	// RW rasters are managed by RW, not our responsibility

	// SMAA static RW rasters/textures
	ReleaseSMAAStaticResources();

	// Velocity buffer (D3DPOOL_DEFAULT)
	ReleaseVelocityBufferResources();

	// SSAO overhaul (D3DPOOL_DEFAULT)
	ReleaseSSAOOverhaulResources();

	// Forward+ cluster index textures (D3DPOOL_DEFAULT)
	ForwardPlus_ReleaseResources();

	// INTZ depth hook resources (handles g_intzTex/g_intzSurf which g_ssaoDepthTex aliases)
	DepthHook_ReleaseResources();

	dbglog("ReleaseDefaultPoolResources: done");
}

// Check if device is valid - use RenderWare camera state instead
static inline bool CheckDeviceState(void)
{
	// RW camera BeginUpdate handles device state internally
	// Just check if we have a valid camera and raster
	if (!Scene.camera) return false;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	return camRas != NULL;
}

void InitSSAOResources(void)
{
	// If noise exists but depth texture is NULL (e.g. after device Reset),
	// re-alias from depthhook's INTZ which was recreated by hook_SetDS.
	if(g_ssaoNoiseTex) {
		if(!g_ssaoDepthTex && g_intzTex) {
			g_ssaoDepthTex = g_intzTex;
			g_ssaoDepthSurf = g_intzSurf;
			dbglog("InitSSAOResources: re-aliased depthTex from depthhook after Reset");
		}
		return;
	}

	__try {
		dbglog("InitSSAOResources: start");
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("InitSSAOResources: no dev"); return; }
		if(Scene.camera == NULL){ dbglog("InitSSAOResources: no camera"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("InitSSAOResources: no camRas"); return; }
		int w = camRas->width;
		int h = camRas->height;
		dbglog("InitSSAOResources: camRas %dx%d", w, h);

		if(FAILED(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_ssaoNoiseTex, NULL))){
			dbglog("InitSSAOResources: CreateTexture noise 4x4 MANAGED failed");
			return;
		}
		dbglog("InitSSAOResources: noise texture created (pool=MANAGED)");
		D3DLOCKED_RECT lr;
		if(FAILED(g_ssaoNoiseTex->LockRect(0, &lr, NULL, 0))){
			dbglog("InitSSAOResources: LockRect noise failed");
			return;
		}
		for(int y = 0; y < 4; y++){
			for(int x = 0; x < 4; x++){
				float rx = (rand()/(float)RAND_MAX)*2.0f - 1.0f;
				float ry = (rand()/(float)RAND_MAX)*2.0f - 1.0f;
				float len = sqrtf(rx*rx + ry*ry);
				if(len > 1.0f){ rx /= len; ry /= len; }
				((DWORD*)((BYTE*)lr.pBits + y*lr.Pitch))[x] =
					D3DCOLOR_ARGB(0, (int)((rx+1)*127.5f), (int)((ry+1)*127.5f), 0);
			}
		}
		g_ssaoNoiseTex->UnlockRect(0);
		dbglog("InitSSAOResources: noise texture filled");

		// Use depthhook's INTZ texture if available (it's bound as DS by the vtable hook)
		// This is the primary path — depthhook's INTZ has real depth data written by the game.
		if(g_intzTex) {
			g_ssaoDepthTex = g_intzTex;
			g_ssaoDepthSurf = g_intzSurf;
			dbglog("InitSSAOResources: using depthhook INTZ depth texture %dx%d", w, h);
			dbglog("InitSSAOResources: done");
			return;
		}

		// Fallback: depthhook not available — try creating our own INTZ.
		// NOTE: This texture is NEVER bound as DS, so depth consumers will read garbage.
		// This path exists only for the case where depthhook is disabled by INI.
		IDirect3D9 *d3d = NULL;
		if(SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d){
			dbglog("InitSSAOResources: checking INTZ support (fallback, no depthhook)");
			HRESULT hr = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
				(D3DFORMAT)MAKEFOURCC('I','N','T','Z'));
			if(SUCCEEDED(hr)){
				dbglog("InitSSAOResources: INTZ supported, creating depth texture (fallback)");
				hr = dev->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL,
					(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
					D3DPOOL_DEFAULT, &g_ssaoDepthTex, NULL);
				if(SUCCEEDED(hr)){
					g_ssaoDepthTex->GetSurfaceLevel(0, &g_ssaoDepthSurf);
					dbglog("InitSSAOResources: INTZ depth texture created OK (fallback)");
					d3d->Release();
					dbglog("InitSSAOResources: done");
					return;
				}else{
					dbglog("InitSSAOResources: CreateTexture INTZ failed hr=0x%08X", hr);
				}
			}else{
				dbglog("InitSSAOResources: INTZ format not supported, SSAO disabled");
			}
			d3d->Release();
		}
		if(!g_ssaoDepthTex){
			dbglog("InitSSAOResources: SSAO disabled - depth texture not available");
			return;
		}
		dbglog("InitSSAOResources: done");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("InitSSAOResources crashed! exception=0x%08X", GetExceptionCode());
	}
}

void ReleaseSSAOOverhaulResources(void){
	for(int i = 0; i < 2; i++){
		if(g_ssaoQuarterTexRW[i]){ RwTextureDestroy(g_ssaoQuarterTexRW[i]); g_ssaoQuarterTexRW[i] = NULL; }
		if(g_ssaoQuarterRaster[i]){ RwRasterDestroy(g_ssaoQuarterRaster[i]); g_ssaoQuarterRaster[i] = NULL; }
	}
	if(g_ssaoBlurTempTexRW){ RwTextureDestroy(g_ssaoBlurTempTexRW); g_ssaoBlurTempTexRW = NULL; }
	if(g_ssaoBlurTempRaster){ RwRasterDestroy(g_ssaoBlurTempRaster); g_ssaoBlurTempRaster = NULL; }
	g_ssaoHistoryValid = false;
	g_ssaoQuarterW = g_ssaoQuarterH = 0;
}

static void InitSSAOOverhaulResources(void){
	if(g_ssaoQuarterRaster[0] && g_ssaoQuarterRaster[1] && g_ssaoBlurTempRaster)
		return;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return;

	int qw = (camRas->width + 3) / 4;
	int qh = (camRas->height + 3) / 4;

	if(g_ssaoQuarterW == qw && g_ssaoQuarterH == qh) return;

	ReleaseSSAOOverhaulResources();

	for(int i = 0; i < 2; i++){
		g_ssaoQuarterRaster[i] = RwRasterCreate(qw, qh, camRas->depth, rwRASTERTYPECAMERATEXTURE);
		if(!g_ssaoQuarterRaster[i]){
			dbglog("[SSAO] Failed to create quarter raster %d", i);
			return;
		}
		g_ssaoQuarterTexRW[i] = RwTextureCreate(g_ssaoQuarterRaster[i]);
		if(g_ssaoQuarterTexRW[i]){
			RwTextureSetFilterMode(g_ssaoQuarterTexRW[i], rwFILTERLINEAR);
			RwTextureSetAddressingU(g_ssaoQuarterTexRW[i], rwTEXTUREADDRESSCLAMP);
			RwTextureSetAddressingV(g_ssaoQuarterTexRW[i], rwTEXTUREADDRESSCLAMP);
		}
	}

	g_ssaoBlurTempRaster = RwRasterCreate(qw, qh, camRas->depth, rwRASTERTYPECAMERATEXTURE);
	if(!g_ssaoBlurTempRaster){
		dbglog("[SSAO] Failed to create blur temp raster");
		return;
	}
	g_ssaoBlurTempTexRW = RwTextureCreate(g_ssaoBlurTempRaster);
	if(g_ssaoBlurTempTexRW){
		RwTextureSetFilterMode(g_ssaoBlurTempTexRW, rwFILTERLINEAR);
		RwTextureSetAddressingU(g_ssaoBlurTempTexRW, rwTEXTUREADDRESSCLAMP);
		RwTextureSetAddressingV(g_ssaoBlurTempTexRW, rwTEXTUREADDRESSCLAMP);
	}

	g_ssaoQuarterW = qw;
	g_ssaoQuarterH = qh;
	g_ssaoHistoryValid = false;
}

void
CPostEffects::DrawSSAO(void)
{
	if(IsGameInMenuOrPaused()) return;
	if(dbglog_throttle( "ssao_enter"))
		dbglog("[PostFX] DrawSSAO ENTER ssaoEnable=%d SSAO=%p", config->ssaoEnable, SSAO);
	if(!config->ssaoEnable || !SSAO){
		if(dbglog_throttle( "ssao_bail"))
			dbglog("[PostFX] DrawSSAO bailing: ssaoEnable=%d SSAO=%p", config->ssaoEnable, SSAO);
		return;
	}

	__try {
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("[PostFX] DrawSSAO bailing: dev is NULL"); return; }
		if(Scene.camera == NULL){ dbglog("[PostFX] DrawSSAO bailing: Scene.camera is NULL"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("[PostFX] DrawSSAO bailing: camRas is NULL"); return; }
		int w = camRas->width;
		int h = camRas->height;

		InitSSAOResources();

		if(!g_ssaoNoiseTex || !g_ssaoDepthTex){
			dbglog("[PostFX] DrawSSAO bailing: noiseTex=%p depthTex=%p", g_ssaoNoiseTex, g_ssaoDepthTex);
			return;
		}

		if(!g_ssaoOutputRaster || g_ssaoOutputRaster->width != w || g_ssaoOutputRaster->height != h){
			if(g_ssaoOutputRaster) RwRasterDestroy(g_ssaoOutputRaster);
			g_ssaoOutputRaster = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
			if(!g_ssaoOutputRaster){ dbglog("[PostFX] DrawSSAO bailing: output raster creation failed"); return; }
		}

		// Suspend depth hook so g_ssaoDepthTex can be sampled while not bound as DS
		DepthHook_Suspend();

		ImmediateModeRenderStatesStore();
		ImmediateModeRenderStatesSet();

		// Render SSAO occlusion to output raster
		RwRaster *origRaster = camRas;
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoOutputRaster);
		RwCameraBeginUpdate(Scene.camera);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetTexture(1, g_ssaoNoiseTex);
		dev->SetTexture(2, g_normalBufferTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		float radius = config->ssaoRadius > 0.0f ? config->ssaoRadius : 1.0f;
		float power = config->ssaoPower > 0.0f ? config->ssaoPower : 2.0f;
		float noiseScale = 4.0f / w;
		float params[4] = { radius, power, noiseScale, 0.0f };
		RwD3D9SetPixelShaderConstant(0, params, 1);

		float screenSize[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, screenSize, 1);

		RwCamera *cam = Scene.camera;
		float n = cam->nearPlane;
		float f = cam->farPlane;
		float projInfo[4] = {
			cam->recipViewWindow.x,
			cam->recipViewWindow.y,
			-n * f / (f - n),
			f / (f - n)
		};
		RwD3D9SetPixelShaderConstant(2, projInfo, 1);

		overrideIm2dPixelShader = SSAO;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Restore original camera raster
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// Blend SSAO occlusion with scene (multiply)
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);

		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);

		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

		ImmediateModeRenderStatesReStore();
		dbglog("[PostFX] DrawSSAO completed successfully");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[PostFX] DrawSSAO CRASHED exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
	}
}

static void DrawSSAO_Overhaul(void)
{
	DBGLOG_ENTER("DrawSSAO_Overhaul");
	if(IsGameInMenuOrPaused()) return;
	if(dbglog_throttle("ssao_ov_enter"))
		dbglog("[PostFX] DrawSSAO_Overhaul ENTER");
	if(!config->ssaoEnable || !SSAO_Temporal || !SSAO_BilateralBlur || !SSAO_Upsample){
		// Fallback to old SSAO if new shaders not loaded
		CPostEffects::DrawSSAO();
		return;
	}

	__try {
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: dev is NULL"); return; }
		if(Scene.camera == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: camera is NULL"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: camRas is NULL"); return; }
		int w = camRas->width;
		int h = camRas->height;

		InitSSAOResources();
		InitSSAOOverhaulResources();

		if(!g_ssaoNoiseTex || !g_ssaoDepthTex){
			dbglog("[PostFX] DrawSSAO_Overhaul bailing: noiseTex=%p depthTex=%p", g_ssaoNoiseTex, g_ssaoDepthTex);
			return;
		}

		if(!g_ssaoQuarterRaster[0] || !g_ssaoQuarterRaster[1]){
			dbglog("[PostFX] DrawSSAO_Overhaul bailing: quarter rasters not ready");
			return;
		}

		if(!g_ssaoOutputRaster || g_ssaoOutputRaster->width != w || g_ssaoOutputRaster->height != h){
			if(g_ssaoOutputRaster) RwRasterDestroy(g_ssaoOutputRaster);
			g_ssaoOutputRaster = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
			if(!g_ssaoOutputRaster){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: output raster creation failed"); return; }
		}

		// Suspend depth hook so g_ssaoDepthTex can be sampled (INTZ not bound as DS)
		DepthHook_Suspend();

		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();

		RwRaster *origRaster = camRas;
		float radius = config->ssaoRadius > 0.0f ? config->ssaoRadius : 1.0f;
		float power = config->ssaoPower > 0.0f ? config->ssaoPower : 2.0f;
		float noiseScale = 4.0f / w;
		int otherIdx = 1 - g_ssaoFrameIndex;

		// Camera projection info (shared by all passes)
		RwCamera *cam = Scene.camera;
		float n = cam->nearPlane;
		float f = cam->farPlane;
		float projInfo[4] = {
			cam->recipViewWindow.x,
			cam->recipViewWindow.y,
			-n * f / (f - n),
			f / (f - n)
		};

		// =====================================================================
		// Pass 1: SSAO_Temporal — quarter-res temporal SSAO
		// =====================================================================
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoQuarterRaster[g_ssaoFrameIndex]);
		RwCameraBeginUpdate(Scene.camera);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// Textures: s0=depth, s1=noise, s2=normals, s3=velocity, s4=history
		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetTexture(1, g_ssaoNoiseTex);
		dev->SetTexture(2, g_normalBufferTex); // NULL-safe: D3D9 ignores NULL SetTexture
		dev->SetTexture(3, g_velocityTex);     // NULL-safe: D3D9 ignores NULL SetTexture
		RwD3D9SetTexture(g_ssaoQuarterTexRW[otherIdx], 4);

		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// c0: {radius, power, noiseScale, temporalBlend}
		float c0Temporal[4] = { radius, power, noiseScale, g_ssaoHistoryValid ? config->ssaoTemporalBlend : 1.0f };
		RwD3D9SetPixelShaderConstant(0, c0Temporal, 1);
		// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
		float c1Temporal[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, c1Temporal, 1);
		// c2: projInfo
		RwD3D9SetPixelShaderConstant(2, projInfo, 1);

		overrideIm2dPixelShader = SSAO_Temporal;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// =====================================================================
		// Pass 2: SSAO_BilateralBlur — horizontal + vertical blur passes
		// =====================================================================
		for(int pass = 0; pass < config->ssaoBlurPasses; pass++){
			// Horizontal blur: render to blur temp, read from current quarter
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, g_ssaoBlurTempRaster);
			RwCameraBeginUpdate(Scene.camera);

			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

			// s0: source, s1: depth
			RwD3D9SetTexture(g_ssaoQuarterTexRW[g_ssaoFrameIndex], 0);
			dev->SetTexture(1, g_ssaoDepthTex);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// c0: {blurRadius, depthThreshold, 0, 0}  (0 = horizontal pass)
			float c0BlurH[4] = { config->ssaoBlurRadius, config->ssaoDepthThreshold, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, c0BlurH, 1);
			// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
			float c1BlurH[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
			RwD3D9SetPixelShaderConstant(1, c1BlurH, 1);
			// c2: projInfo
			RwD3D9SetPixelShaderConstant(2, projInfo, 1);

			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
			overrideIm2dPixelShader = nil;

			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, origRaster);
			RwCameraBeginUpdate(Scene.camera);

			// Vertical blur: render to quarter frame, read from blur temp
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, g_ssaoQuarterRaster[g_ssaoFrameIndex]);
			RwCameraBeginUpdate(Scene.camera);

			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

			// s0: source (blur temp), s1: depth
			RwD3D9SetTexture(g_ssaoBlurTempTexRW, 0);
			dev->SetTexture(1, g_ssaoDepthTex);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// c0: {blurRadius, depthThreshold, 1, 0}  (1 = vertical pass flag)
			float c0BlurV[4] = { config->ssaoBlurRadius, config->ssaoDepthThreshold, 1.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, c0BlurV, 1);
			// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
			float c1BlurV[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
			RwD3D9SetPixelShaderConstant(1, c1BlurV, 1);
			// c2: projInfo
			RwD3D9SetPixelShaderConstant(2, projInfo, 1);

			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
			overrideIm2dPixelShader = nil;

			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, origRaster);
			RwCameraBeginUpdate(Scene.camera);
		}

		// =====================================================================
		// Pass 3: SSAO_Upsample — quarter-res → full-res
		// =====================================================================
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoOutputRaster);
		RwCameraBeginUpdate(Scene.camera);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// s0: blur result, s1: depth
		RwD3D9SetTexture(g_ssaoBlurTempTexRW, 0);
		dev->SetTexture(1, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// c0: {depthThreshold, 0, 0, 0}
		float c0Up[4] = { config->ssaoDepthThreshold, 0.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0Up, 1);
		// c1: {fullW, fullH, 1/fullW, 1/fullH}
		float c1Up[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, c1Up, 1);
		// c2: {quarterW, quarterH, 1/quarterW, 1/quarterH}
		float c2Up[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
		RwD3D9SetPixelShaderConstant(2, c2Up, 1);

		overrideIm2dPixelShader = SSAO_Upsample;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// =====================================================================
		// Composite: multiply blend SSAO onto scene
		// =====================================================================
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);

		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);

		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

		CPostEffects::ImmediateModeRenderStatesReStore();

		// Swap frame index and mark history valid
		g_ssaoFrameIndex = otherIdx;
		g_ssaoHistoryValid = true;

		if(dbglog_throttle("ssao_ov_done"))
			dbglog("[PostFX] DrawSSAO_Overhaul completed successfully");
		// Debug dump: after SSAO composite
		DumpCurrentRT("after_ssao");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[PostFX] DrawSSAO_Overhaul CRASHED exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
	}
}

#include "AreaTex.h"
#include "SearchTex.h"

void GenerateSMAAAreaTex(IDirect3DDevice9 *dev, IDirect3DTexture9 **outTex)
{
	if(*outTex) return;
	if(FAILED(dev->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8L8, D3DPOOL_DEFAULT, outTex, NULL))){
		dbglog("GenerateSMAAAreaTex: CreateTexture failed");
		return;
	}
	D3DLOCKED_RECT rect;
	if(FAILED((*outTex)->LockRect(0, &rect, NULL, D3DLOCK_DISCARD))){
		dbglog("GenerateSMAAAreaTex: LockRect failed");
		(*outTex)->Release();
		*outTex = NULL;
		return;
	}
	for(int y = 0; y < AREATEX_HEIGHT; y++){
		memcpy((char*)rect.pBits + y * rect.Pitch,
		       areaTexBytes + y * AREATEX_PITCH,
		       AREATEX_PITCH);
	}
	(*outTex)->UnlockRect(0);
	dbglog("GenerateSMAAAreaTex: OK %dx%d", AREATEX_WIDTH, AREATEX_HEIGHT);
}

void GenerateSMAASearchTex(IDirect3DDevice9 *dev, IDirect3DTexture9 **outTex)
{
	if(*outTex) return;
	if(FAILED(dev->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_L8, D3DPOOL_DEFAULT, outTex, NULL))){
		dbglog("GenerateSMAASearchTex: CreateTexture failed");
		return;
	}
	D3DLOCKED_RECT rect;
	if(FAILED((*outTex)->LockRect(0, &rect, NULL, D3DLOCK_DISCARD))){
		dbglog("GenerateSMAASearchTex: LockRect failed");
		(*outTex)->Release();
		*outTex = NULL;
		return;
	}
	for(int y = 0; y < SEARCHTEX_HEIGHT; y++){
		memcpy((char*)rect.pBits + y * rect.Pitch,
		       searchTexBytes + y * SEARCHTEX_PITCH,
		       SEARCHTEX_PITCH);
	}
	(*outTex)->UnlockRect(0);
	dbglog("GenerateSMAASearchTex: OK %dx%d", SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
}

// =====================================================
// IBL Buffer - quarter-res sky/cloud ambient
// =====================================================
static IDirect3DTexture9 *GetIBLTexture(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return NULL;
	if(g_iblTex) return g_iblTex;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return NULL;
	int w = camRas->width / 4;
	int h = camRas->height / 4;
	if(w < 16) w = 16;
	if(h < 16) h = 16;

	if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_iblTex, NULL)))
		return NULL;
	if(FAILED(g_iblTex->GetSurfaceLevel(0, &g_iblSurf))){
		g_iblTex->Release();
		g_iblTex = NULL;
		return NULL;
	}
	dbglog("GetIBLTexture: OK %dx%d", w, h);
	return g_iblTex;
}

void RenderIBLBuffer(void)
{
	static int iblLogged = 0;
	if(!DynamicSky){ if(!iblLogged){ dbglog("RenderIBL: DynamicSky=NULL"); iblLogged=1; } return; }

	// Get screen size BEFORE switching RT so we can bail without leaking refs
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){ if(!iblLogged){ dbglog("RenderIBL: camRas NULL"); iblLogged=1; } return; }

	IDirect3DTexture9 *tex = GetIBLTexture();
	if(!tex || !g_iblSurf){ if(!iblLogged){ dbglog("RenderIBL: no tex/surf"); iblLogged=1; } return; }
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!iblLogged){ dbglog("RenderIBL: OK tex=%p surf=%p", tex, g_iblSurf); iblLogged=1; }

	int rtW = camRas->width / 4;
	int rtH = camRas->height / 4;
	if(rtW < 16) rtW = 16;
	if(rtH < 16) rtH = 16;

	// Save current render target and viewport
	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	// Set IBL buffer as render target (no depth needed)
	dev->SetRenderTarget(0, g_iblSurf);
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 iblVP = { 0, 0, (DWORD)rtW, (DWORD)rtH, 0.0f, 1.0f };
	dev->SetViewport(&iblVP);

	float screenP[4] = { (float)camRas->width, (float)camRas->height, 1.0f/max((float)camRas->width, 1e-7f), 1.0f/max((float)camRas->height, 1e-7f) };
	RwD3D9SetPixelShaderConstant(0, screenP, 1);

	// Sky colors from timecycle (zenith = sky top, horizon = sky bottom)
	extern CColourSet &CTimeCycle__m_CurrentColours;
	CColourSet &tc = CTimeCycle__m_CurrentColours;
	float skyC[4] = {
		tc.skyTopR / 255.0f,
		tc.skyTopG / 255.0f,
		tc.skyTopB / 255.0f,
		tc.fogStart > 0.0f ? 1.0f : 0.0f
	};
	RwD3D9SetPixelShaderConstant(1, skyC, 1);

	// Cloud params: time, coverage from weather, cloud alpha from timecycle, unused
	extern float cloudAnimTimer;
	extern float &CWeather__CloudCoverage;
	float cloudP[4] = {
		cloudAnimTimer * 0.01f,
		CWeather__CloudCoverage,
		tc.cloudAlpha,
		0.0f
	};
	RwD3D9SetPixelShaderConstant(2, cloudP, 1);

	// Sun direction from timecycle
	float sunD[4];
	GetSunDirection(sunD[0], sunD[1], sunD[2]);
	sunD[3] = tc.spriteBrightness / 10.0f;
	RwD3D9SetPixelShaderConstant(3, sunD, 1);

	// Weather type for smog support
	// GTA SA weather types: 0=Sunny, 1=SunnyWindy, 2=Cloudy, 3=Rainy, 4=Smoggy, ...
	extern int16 &CWeather__OldWeatherType;
	extern int16 &CWeather__NewWeatherType;
	extern float &CWeather__InterpolationValue;
	float oldW = (float)CWeather__OldWeatherType;
	float newW = (float)CWeather__NewWeatherType;
	float wInterp = CWeather__InterpolationValue;
	// Calculate smog boost: 1.0 when fully smoggy, 0.0 otherwise
	float smogBoost = 0.0f;
	if(CWeather__OldWeatherType == 4) smogBoost = 1.0f - wInterp;
	if(CWeather__NewWeatherType == 4) smogBoost = wInterp;
	float weatherP[4] = { newW, oldW, wInterp, smogBoost };
	RwD3D9SetPixelShaderConstant(8, weatherP, 1);

	// Horizon colors from timecycle (c4 = skyBot)
	float horizC[4] = {
		tc.skyBotR / 255.0f,
		tc.skyBotG / 255.0f,
		tc.skyBotB / 255.0f,
		0.8f  // horizon blend factor
	};
	RwD3D9SetPixelShaderConstant(4, horizC, 1);

	// Moon data (c5) — opposite sun direction, phase from time
	float moonD[4] = { -sunD[0], -sunD[1], -sunD[2], 0.5f };
	RwD3D9SetPixelShaderConstant(5, moonD, 1);

	// Cloud clump params (c6) — reasonable defaults
	float clumpP[4] = { 3.0f, 0.6f, 1.5f, 6.0f };
	RwD3D9SetPixelShaderConstant(6, clumpP, 1);

	// Weather fog from timecycle (c7)
	// fogStart in SA: lower = denser fog. Invert to get density.
	float fogD = 0.0f;
	if(tc.fogStart > 0.0f){
		fogD = max(0.0f, min(1.0f, 1.0f / max(tc.fogStart, 1.0f)));
	}
	float fogC[4] = {
		tc.lowCloudsR / 255.0f,
		tc.lowCloudsG / 255.0f,
		tc.lowCloudsB / 255.0f,
		fogD
	};
	RwD3D9SetPixelShaderConstant(7, fogC, 1);

	// Render fullscreen quad with IBL shader
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	overrideIm2dPixelShader = DynamicSky;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	CPostEffects::ImmediateModeRenderStatesReStore();

	// Restore old render target and viewport
	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
}

static IDirect3DTexture9* GetNormalBufferTexture(void)
{
	if(g_normalBufferTex) return g_normalBufferTex;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return NULL;
	if(!Scene.camera) return NULL;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return NULL;
	int w = camRas->width / 2;
	int h = camRas->height / 2;
	if(w < 1 || h < 1) return NULL;
	if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_normalBufferTex, NULL)))
		return NULL;
	if(FAILED(g_normalBufferTex->GetSurfaceLevel(0, &g_normalBufferSurf))){
		g_normalBufferTex->Release();
		g_normalBufferTex = NULL;
		return NULL;
	}
	return g_normalBufferTex;
}

static IDirect3DTexture9* GetPipeChainTexture(int idx)
{
	IDirect3DTexture9 **tex = (idx == 0) ? &g_pipeChainTexA : &g_pipeChainTexB;
	IDirect3DSurface9 **surf = (idx == 0) ? &g_pipeChainSurfA : &g_pipeChainSurfB;
	if(*tex) return *tex;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return NULL;
	if(!Scene.camera) return NULL;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return NULL;
	int w = camRas->width;
	int h = camRas->height;
	if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, tex, NULL)))
		return NULL;
	if(FAILED((*tex)->GetSurfaceLevel(0, surf))){
		(*tex)->Release();
		*tex = NULL;
		return NULL;
	}
	return *tex;
}

void DrawNormalBufferToTexture(void)
{
	if(!config->normalBufferEnable || !NormalBufferShader)
		return;

	// Ensure SSAO depth texture exists (needed for depth reconstruction)
	if(!g_ssaoDepthTex){
		InitSSAOResources();
		if(!g_ssaoDepthTex) return;
	}

	IDirect3DTexture9 *tex = GetNormalBufferTexture();
	if(!tex || !g_normalBufferSurf) return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	// Save current RT and viewport
	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	// Set normal buffer as render target
	dev->SetRenderTarget(0, g_normalBufferSurf);
	dev->SetDepthStencilSurface(NULL);
	// Get camera raster for normal buffer dimensions (half-res)
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){ dev->SetRenderTarget(0, oldRT); dev->SetDepthStencilSurface(oldDS); if(oldRT) oldRT->Release(); if(oldDS) oldDS->Release(); return; }
	int nbW = camRas->width / 2, nbH = camRas->height / 2;
	if(nbW < 1) nbW = 1;
	if(nbH < 1) nbH = 1;
	D3DVIEWPORT9 nbVP = { 0, 0, (DWORD)nbW, (DWORD)nbH, 0.0f, 1.0f };
	dev->SetViewport(&nbVP);

	// Get screen size
	float screenW = (float)camRas->width;
	float screenH = (float)camRas->height;

	// c0 = (nearClip, farClip, 0, 0)
	float nearClip = RwCameraGetNearClipPlane(Scene.camera);
	float farClip = RwCameraGetFarClipPlane(Scene.camera);
	float depthP[4] = { nearClip, farClip, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, depthP, 1);

	// c1 = (screenW, screenH, 1/screenW, 1/screenH)
	float screenP[4] = { screenW, screenH, 1.0f/max(screenW, 1e-7f), 1.0f/max(screenH, 1e-7f) };
	RwD3D9SetPixelShaderConstant(1, screenP, 1);

	// c2 = projection matrix diagonal for view-space reconstruction
	// RwD3D9GetTransform wraps D3D9 GetTransform, output is D3DMATRIX (4x4 float, row-major)
	static float projMat[16];
	RwD3D9GetTransform(D3DTS_PROJECTION, projMat);
	float projP[4] = { projMat[0], projMat[5], 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, projP, 1);

	// Set depth texture on stage 0 only (single-pass depth reconstruction)
	dev->SetTexture(0, g_ssaoDepthTex);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

	// Render fullscreen quad with normal buffer shader
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	overrideIm2dPixelShader = NormalBufferShader;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	CPostEffects::ImmediateModeRenderStatesReStore();

	// Cleanup
	dev->SetTexture(0, NULL);

	// Restore old RT and viewport
	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
}

void DrawPipeChain(void)
{
	if(!config->pipeChainEnable || !PipeChainShader)
		return;
	if(!config->normalBufferEnable || !g_normalBufferTex)
		return;
	if(!g_ssaoDepthTex) return;

	IDirect3DTexture9 *texA = GetPipeChainTexture(0);
	IDirect3DTexture9 *texB = GetPipeChainTexture(1);
	if(!texA || !texB || !g_pipeChainSurfA || !g_pipeChainSurfB) return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){ dbglog("pipeChainBlend: camRas is NULL, skipping"); return; }
	float screenP[4] = { (float)camRas->width, (float)camRas->height,
		1.0f/max((float)camRas->width, 1e-7f), 1.0f/max((float)camRas->height, 1e-7f) };
	float projP[4] = { 1.0f, 1.0f, 1.0f, 0.0f };

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);

	// ---- Pass 0: Input -> texA ----
	{
		dev->SetRenderTarget(0, g_pipeChainSurfA);
		dev->SetDepthStencilSurface(NULL);

		float pipeP[4] = { 0.0f, 0.0f, config->pipeChainIntensity, 0.0f };
		RwD3D9SetPixelShaderConstant(0, pipeP, 1);
		RwD3D9SetPixelShaderConstant(1, projP, 1);
		RwD3D9SetPixelShaderConstant(2, screenP, 1);

		dev->SetTexture(0, NULL);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

		// Normal buffer on stage 1
		dev->SetTexture(1, g_normalBufferTex);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// Depth on stage 2
		dev->SetTexture(2, g_ssaoDepthTex);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		overrideIm2dPixelShader = PipeChainShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// ---- Pass 1: Mid-A -> texB ----
	{
		dev->SetRenderTarget(0, g_pipeChainSurfB);
		dev->SetDepthStencilSurface(NULL);

		float pipeP[4] = { 1.0f, 0.0f, config->pipeChainIntensity, 0.0f };
		RwD3D9SetPixelShaderConstant(0, pipeP, 1);
		RwD3D9SetPixelShaderConstant(1, projP, 1);
		RwD3D9SetPixelShaderConstant(2, screenP, 1);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		dev->SetTexture(1, g_normalBufferTex);
		dev->SetTexture(2, g_ssaoDepthTex);
		dev->SetTexture(3, NULL);

		overrideIm2dPixelShader = PipeChainShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// ---- Pass 2: Mid-B -> texA (ping-pong) ----
	{
		dev->SetRenderTarget(0, g_pipeChainSurfA);
		dev->SetDepthStencilSurface(NULL);

		float pipeP[4] = { 2.0f, 0.0f, config->pipeChainIntensity, 0.0f };
		RwD3D9SetPixelShaderConstant(0, pipeP, 1);
		RwD3D9SetPixelShaderConstant(1, projP, 1);
		RwD3D9SetPixelShaderConstant(2, screenP, 1);

		// Scene on stage 0
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		dev->SetTexture(1, g_normalBufferTex);
		dev->SetTexture(2, g_ssaoDepthTex);
		// Intermediate (texB) on stage 3
		dev->SetTexture(3, texB);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		overrideIm2dPixelShader = PipeChainShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// ---- Pass 3: Output -> back buffer ----
	{
		dev->SetRenderTarget(0, oldRT);
		dev->SetDepthStencilSurface(oldDS);

		float pipeP[4] = { 3.0f, 0.0f, config->pipeChainIntensity, 0.0f };
		RwD3D9SetPixelShaderConstant(0, pipeP, 1);
		RwD3D9SetPixelShaderConstant(1, projP, 1);
		RwD3D9SetPixelShaderConstant(2, screenP, 1);

		// Scene on stage 0
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		// Intermediate (texA) on stage 3
		dev->SetTexture(3, texA);

		overrideIm2dPixelShader = PipeChainShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// Cleanup
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
	dev->SetTexture(3, NULL);

	CPostEffects::ImmediateModeRenderStatesReStore();
	// Debug dump: after pipe chain
	DumpCurrentRT("after_pipechain");
}

// ============================================================
// SMAATryInitRasters — deferred force-init of CAMERATEXTURE
// rasters. Called from RenderScene_before (main.cpp) so the init
// runs OUTSIDE the main camera's BeginUpdate (RW 3.6 D3D9 driver
// crashes creating render-target textures mid-frame).
// Handles SMAA rasters + VCS radiosity rasters.
// Harmless to call every frame; only executes once per resource.
// After init, each raster is tracked via TrackRaster so
// RasterEnsureSurfaceReady can verify it.
// ============================================================
void SMAATryInitRasters(void)
{
	if(!s_smaaPendingInit && !s_vcsRadPendingInit)
		return;

	// Diagnostic: confirm this function is reached
	dbglog("[RasterInit] SMAATryInitRasters: s_smaaPendingInit=%d s_vcsRadPendingInit=%d",
		s_smaaPendingInit, s_vcsRadPendingInit);

	if(!s_smaaInitCam){
		s_smaaInitCam = RwCameraCreate();
		if(s_smaaInitCam){
			// Attach a frame (required by BeginUpdate — crashes without it)
			RwCameraSetFrame(s_smaaInitCam, RwFrameCreate());
			// Attach a small Z raster (BeginUpdate may need one)
			RwRaster *camRasForDepth = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
			int depth = camRasForDepth ? camRasForDepth->depth : 32;
			s_smaaInitZRas = RwRasterCreate(4, 4, depth, rwRASTERTYPECAMERATEXTURE);
			if(s_smaaInitZRas)
				RwCameraSetZRaster(s_smaaInitCam, s_smaaInitZRas);
			dbglog("[RasterInit] Scratch camera created %p with frame + Z raster", s_smaaInitCam);
		} else {
			dbglog("[RasterInit] ERROR: RwCameraCreate failed, cannot init rasters");
			s_smaaBroken = true;
			s_smaaPendingInit = false;
			s_vcsRadPendingInit = false;
			return;
		}
	}

	// ---- SMAA rasters ----
	if(s_smaaPendingInit && g_smaaEdgeRaster && g_smaaBlendRaster && g_smaaPrevFrameRaster){
		RwRaster *initRas[] = { g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster };
		bool allOk = true;
		for(int i = 0; i < 3; i++){
			RwCameraSetRaster(s_smaaInitCam, initRas[i]);
			allOk &= (RwCameraBeginUpdate(s_smaaInitCam) != NULL);
			RwCameraEndUpdate(s_smaaInitCam);
			if(allOk)
				TrackRaster(initRas[i]);
		}
		if(allOk){
			dbglog("[RasterInit] SMAA rasters force-initialized as render targets");
			s_smaaRastersInitialized = true;
			s_smaaPendingInit = false;
		} else {
			dbglog("[RasterInit] ERROR: SMAA raster init failed, disabling SMAA");
			s_smaaBroken = true;
			s_smaaPendingInit = false;
		}
	}

	// ---- VCS radiosity rasters ----
	if(s_vcsRadPendingInit && vcs_radiosity_target1 && vcs_radiosity_target2){
		RwRaster *vcsRas[] = { vcs_radiosity_target1, vcs_radiosity_target2 };
		for(int i = 0; i < 2; i++){
			RwCameraSetRaster(s_smaaInitCam, vcsRas[i]);
			RwCameraBeginUpdate(s_smaaInitCam);
			RwCameraEndUpdate(s_smaaInitCam);
			TrackRaster(vcsRas[i]);
		}
		dbglog("[RasterInit] VCS radiosity rasters force-initialized as render targets");
		s_vcsRadPendingInit = false;
	}
}

void
CPostEffects::DrawSMAA(void)
{
	IDirect3DDevice9 *dev = d3d9device;

	// Guard: SMAA disabled or shaders not loaded
	if(!config->smaaEnable || !SMAA_Edge || !SMAA_BlendWeight || !SMAA_BlendNeighbor){
		// When SMAA is disabled, release resources so they're recreated cleanly on re-enable
		if(g_smaaRtWidth != 0 || g_smaaRtHeight != 0){
			ReleaseSMAAStaticResources();
			dbglog("[SMAA] Disabled — released resources (Edge=%p BlendW=%p BlendN=%p)",
				SMAA_Edge, SMAA_BlendWeight, SMAA_BlendNeighbor);
		}
		return;
	}

	// Guard: front buffer not ready
	if(pRasterFrontBuffer == NULL){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: pRasterFrontBuffer=NULL");
		return;
	}

	// Guard: device state invalid
	if(!CheckDeviceState()){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: CheckDeviceState failed");
		return;
	}

	// Guard: D3D9 device not available
	if(!dev){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: d3d9device=NULL");
		return;
	}

	// All intermediates + metrics live on the camera raster's texel grid.
	// Size SMAA rasters to the camera raster, not pRasterFrontBuffer (which
	// is 2048x2048 while camera is 1920x1080). Using pRasterFrontBuffer size
	// wastes memory+bandwidth and the extra pixels are never sampled (viewports
	// are set to camera dimensions in the SMAA passes).
	RwRaster *camRasForSize = RwCameraGetRaster(Scene.camera);
	if(!camRasForSize){ if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: camRas=NULL"); return; }
	int w = camRasForSize->width;
	int h = camRasForSize->height;
	if(w < 8 || h < 8){ if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: dims too small %dx%d", w, h); return; }

	// --- First-frame / resolution-change detailed log ---
	// NOTE: resolution-change check MUST come before the s_smaaBroken check
	// because resolution change resets the broken flag, allowing retry after
	// device reset (alt-tab).
	static bool smaaFirstFrame = true;
	bool resolutionChanged = (g_smaaRtWidth != w || g_smaaRtHeight != h);

	if(smaaFirstFrame || resolutionChanged){
		static const float thresholds[] = { 0.15f, 0.1f, 0.1f, 0.05f };
		static const float maxSearchSteps[] = { 4.0f, 8.0f, 16.0f, 32.0f };
		// SMAA internal constants: HIGH preset, temporal always on
		static const int kSmaaPreset = 2; // HIGH
		int preset = kSmaaPreset;
		dbglog("[SMAA-DIAG] === INIT/FIRST-FRAME ===");
		dbglog("[SMAA-DIAG] pRasterFrontBuffer=%p %dx%d depth=%d",
			pRasterFrontBuffer, w, h, pRasterFrontBuffer->depth);
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		dbglog("[SMAA-DIAG] camRaster=%p %dx%d", camRas,
			camRas ? RwRasterGetWidth(camRas) : 0, camRas ? RwRasterGetHeight(camRas) : 0);
		dbglog("[SMAA-DIAG] Edge=%p EdgeMotionDepth=%p BlendW=%p BlendN=%p Temporal=%p",
			SMAA_Edge, SMAA_EdgeMotionDepth, SMAA_BlendWeight, SMAA_BlendNeighbor, SMAA_Temporal);
		dbglog("[SMAA-DIAG] preset=%d thresh=%.3f searchSteps=%.0f temporal=1",
			preset, thresholds[preset], maxSearchSteps[preset]);
		dbglog("[SMAA-DIAG] velocityTex=%p prevFrame=%p",
			g_velocityTex, g_smaaPrevFrameRaster);
		smaaFirstFrame = false;
	}

	// Recreate rasters if resolution changed (must happen before broken check
	// so resolution change can reset the broken flag and retry)
	if(resolutionChanged){
		dbglog("[SMAA-DIAG] RESOLUTION CHANGE %dx%d -> %dx%d (destroying all rasters)",
			g_smaaRtWidth, g_smaaRtHeight, w, h);
		if(g_smaaEdgeRaster){ UntrackRaster(g_smaaEdgeRaster); RwRasterDestroy(g_smaaEdgeRaster); g_smaaEdgeRaster = NULL; }
		if(g_smaaBlendRaster){ UntrackRaster(g_smaaBlendRaster); RwRasterDestroy(g_smaaBlendRaster); g_smaaBlendRaster = NULL; }
		if(g_smaaPrevFrameRaster){ UntrackRaster(g_smaaPrevFrameRaster); RwRasterDestroy(g_smaaPrevFrameRaster); g_smaaPrevFrameRaster = NULL; }
		g_smaaRtWidth = w; g_smaaRtHeight = h;
		if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
		if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
		s_smaaRastersInitialized = false;
		s_smaaPendingInit = false;
		s_smaaBroken = false;
		g_smaaHistoryValid = false;
	}

	// Check for permanent broken state (init failed previously).
	// Resolution-change reset above clears this, allowing retry on alt-tab.
	if(s_smaaBroken){
		if(dbglog_throttle("smaa_broken"))
			dbglog("[SMAA] SKIP: permanently broken (init failed), will not retry until resolution change");
		return;
	}

	// Create RW camera texture rasters at FRONT-BUFFER size (pRasterFrontBuffer),
	// NOT camera raster size. The game's fullscreen-quad UVs (colorfilterVerts)
	// are in front-buffer texel space � e.g. blurVerts math divides by
	// pRasterFrontBuffer width, and radiosity uses umax=(screenW+0.5)/fbWidth.
	// SMAA shaders sample with IN.texCoord passed through from the quad UVs.
	// If rasters had mismatched size (1920x1080 camera vs 2048x2048 FB), the
	// final image would show only the top-left 93.75%%x52.7%% stretched to fullscreen.
	// This is a known GTA SA gotcha: pRasterFrontBuffer is 2048x2048 while
	// the camera is 1920x1080. Raster storage is cheap; visual correctness is not.
	RwRaster *camRasForDepth = RwCameraGetRaster(Scene.camera);
	int camDepth = camRasForDepth ? camRasForDepth->depth : 32;
	if(!g_smaaEdgeRaster){
		g_smaaEdgeRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaEdgeRaster){ dbglog("[SMAA-DIAG] FATAL: edgeRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created edgeRaster=%p %dx%d (cam size)", g_smaaEdgeRaster, w, h);
	}
	if(!g_smaaBlendRaster){
		g_smaaBlendRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaBlendRaster){ dbglog("[SMAA-DIAG] FATAL: blendRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created blendRaster=%p %dx%d (cam size)", g_smaaBlendRaster, w, h);
	}
	if(!g_smaaPrevFrameRaster){
		g_smaaPrevFrameRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaPrevFrameRaster){ dbglog("[SMAA-DIAG] FATAL: prevFrameRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created prevFrameRaster=%p %dx%d (cam size)", g_smaaPrevFrameRaster, w, h);
	}

	// Deferred force-init: set the pending flag so SMAATryInitRasters() (called
	// from RenderScene_before, outside the main camera's BeginUpdate) will
	// create the D3D9 surfaces via the scratch camera. Cannot do it inline
	// because RW 3.6's D3D9 driver crashes when creating render-target
	// textures while the device is in an active render state (mid-frame).
	// Bail out this frame — the SMAA pass will run next frame after init.
	if(!s_smaaRastersInitialized && g_smaaEdgeRaster && g_smaaBlendRaster && g_smaaPrevFrameRaster){
		s_smaaPendingInit = true;
		if(dbglog_throttle("smaa_pending"))
			dbglog("[SMAA] SKIP: rasters pending init, will run next frame");
		return;
	} else if(!s_smaaRastersInitialized) {
		// Rasters not created yet (shouldn't happen, but be safe)
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: rasters not created yet");
		return;
	}

	// Verify all rasters have valid D3D9 surfaces before proceeding
	if(!RasterEnsureSurfaceReady(g_smaaEdgeRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaBlendRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaPrevFrameRaster, "SMAA")){
		if(dbglog_throttle("smaa_nosurf"))
			dbglog("[SMAA] SKIP: rasters not surface-ready, will retry next frame");
		return;
	}

	// Save/restore D3D9 state around all SMAA passes + texture creation.
	// Must be BEFORE area/search tex generation — those leave D3D9 state dirty,
	// and we need Restore to capture the original game state, not the post-creation state.
	ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();

	// Create D3D textures for area/search lookup
	if(!g_smaaAreaTex){
		if(dev){
			extern void GenerateSMAAAreaTex(IDirect3DDevice9*, IDirect3DTexture9**);
			extern void GenerateSMAASearchTex(IDirect3DDevice9*, IDirect3DTexture9**);
			GenerateSMAAAreaTex(dev, &g_smaaAreaTex);
			GenerateSMAASearchTex(dev, &g_smaaSearchTex);
			dbglog("[SMAA-DIAG] Generated areaTex=%p searchTex=%p", g_smaaAreaTex, g_smaaSearchTex);
		}
	}

	// Camera movement tracking for temporal stabilization
	static CVector prevCamPos = {0, 0, 0};
	static RwMatrix prevCamMatrix = {0};
	static bool camInitialized = false;

	RwMatrix *camMatrix = NULL;
	RwFrame *camFrame = Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL;
	if(camFrame)
		camMatrix = RwFrameGetLTM(camFrame);
	
	CVector camPos = {0, 0, 0};
	if(camMatrix)
		camPos = {camMatrix->pos.x, camMatrix->pos.y, camMatrix->pos.z};

	float cameraVelocity = 0.0f;
	float cameraRotation = 0.0f;

	if(camInitialized && camMatrix){
		float dx = camPos.x - prevCamPos.x;
		float dy = camPos.y - prevCamPos.y;
		float dz = camPos.z - prevCamPos.z;
		cameraVelocity = sqrtf(dx*dx + dy*dy + dz*dz);

		float dot = camMatrix->at.x * prevCamMatrix.at.x +
		            camMatrix->at.y * prevCamMatrix.at.y +
		            camMatrix->at.z * prevCamMatrix.at.z;
		cameraRotation = 1.0f - max(-1.0f, min(1.0f, dot));
	}

	if(camMatrix){
		prevCamPos = camPos;
		prevCamMatrix = *camMatrix;
		camInitialized = true;
	}

	float cameraMovement = min(1.0f, (cameraVelocity * 0.1f) + (cameraRotation * 2.0f));

	// SMAA preset parameters — internal constants (HIGH preset)
	static const float thresholds[] = { 0.15f, 0.1f, 0.1f, 0.05f };
	static const float maxSearchSteps[] = { 4.0f, 8.0f, 16.0f, 32.0f };
	static const int kSmaaPreset = 2; // HIGH
	float smaaThreshold = thresholds[kSmaaPreset];
	float smaaSearchSteps = maxSearchSteps[kSmaaPreset];
	float screenParams[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };

	// Save original camera raster (the draw buffer — back buffer)
	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);
	int camW = drawBuffer ? RwRasterGetWidth(drawBuffer) : w;
	int camH = drawBuffer ? RwRasterGetHeight(drawBuffer) : h;

	// Log pre-pass D3D9 state
	IDirect3DSurface9 *rt0 = NULL;
	dev->GetRenderTarget(0, &rt0);
	if(dbglog_throttle("smaaPre"))
		dbglog("[SMAA-DIAG] PRE-PASS: drawBuf=%p(%dx%d) pRFB=%p(%dx%d) RT0=%p cam=%p",
			drawBuffer, drawBuffer ? drawBuffer->width : 0, drawBuffer ? drawBuffer->height : 0,
			pRasterFrontBuffer, w, h, rt0, Scene.camera);
	if(rt0) rt0->Release();

	// ---- Pass 0: Edge + Motion + Depth Detection ----
	if(dbglog_throttle("smaaP0s"))
		dbglog("[SMAA-DIAG] Pass0-START: cam=%p camFrame=%p drawBuf=%p",
			Scene.camera,
			Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL,
			drawBuffer);

	// RW-native RT switching — no camera Begin/EndUpdate, no raw surface handles.
	if(!g_smaaEdgeRaster || !g_smaaBlendRaster || !g_smaaPrevFrameRaster){
		dbglog("[SMAA-DIAG] FATAL: intermediate rasters NULL (edge=%p blend=%p prev=%p)",
			g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster);
		ImmediateModeRenderStatesReStore();
		return;
	}

	// Intermediates are front-buffer sized; keep rasterization on the camera
	// viewport so the quad writes the same top-left texel region its UVs address.
	D3DVIEWPORT9 vpSaved;
	dev->GetViewport(&vpSaved);
	D3DVIEWPORT9 vpCam = { 0, 0, (DWORD)camW, (DWORD)camH, 0.0f, 1.0f };

	RwD3D9SetRenderTarget(0, g_smaaEdgeRaster);
	dev->SetViewport(&vpCam);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 0.0f, 0);

	// Render states for fullscreen passes — matches SSAO/SSS pattern
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	IDirect3DSurface9 *p0rt = NULL;
	dev->GetRenderTarget(0, &p0rt);
	if(dbglog_throttle("smaaP0"))
		dbglog("[SMAA-DIAG] Pass0: edge=%p RT0=%p shader=%p (EdgeMotionDepth=%d)",
			g_smaaEdgeRaster, p0rt, SMAA_EdgeMotionDepth ? SMAA_EdgeMotionDepth : SMAA_Edge,
			SMAA_EdgeMotionDepth != NULL);
	if(p0rt) p0rt->Release();

	// Set front buffer as input texture on stage 0
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind previous frame for motion detection on stage 1
	if(g_smaaPrevFrameRaster){
		if(!g_smaaPrevFrameTexRW){
			g_smaaPrevFrameTexRW = RwTextureCreate(g_smaaPrevFrameRaster);
			if(g_smaaPrevFrameTexRW){
				RwTextureSetFilterMode(g_smaaPrevFrameTexRW, rwFILTERLINEAR);
				RwTextureSetAddressingU(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
				RwTextureSetAddressingV(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
			}
		}
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);
	}
	// Bind depth texture for depth-based edge detection on stage 2
	extern IDirect3DTexture9 *g_ssaoDepthTex;
	if(g_ssaoDepthTex){
		// Suspend depth hook so INTZ can be sampled on s2
		DepthHook_Suspend();
		d3d9device->SetTexture(2, g_ssaoDepthTex);
	}

	// Bind velocity buffer on stage 3 for combined motion detection
	if(g_velocityTex)
		d3d9device->SetTexture(3, g_velocityTex);

	// Temporal always on — lower motion threshold for better stabilization
	float motionThresh = 0.5f;
	float edgeP[4] = {smaaThreshold, motionThresh, 2.0f, cameraMovement};
	RwD3D9SetPixelShaderConstant(0, edgeP, 1);
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);

	// Use combined edge+motion+depth shader
	overrideIm2dPixelShader = SMAA_EdgeMotionDepth ? SMAA_EdgeMotionDepth : SMAA_Edge;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Restore depth hook after edge pass if it was suspended (blend weight doesn't sample depth)
	if(g_ssaoDepthTex){
		DepthHook_Restore();
	}

	// ---- Pass 1: Blend Weight Calculation ----
	RwD3D9SetRenderTarget(0, g_smaaBlendRaster);
	dev->SetViewport(&vpCam);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 0.0f, 0);

	IDirect3DSurface9 *p1rt = NULL;
	dev->GetRenderTarget(0, &p1rt);
	if(dbglog_throttle("smaaP1"))
		dbglog("[SMAA-DIAG] Pass1: blend=%p RT0=%p shader=%p searchSteps=%.0f",
			g_smaaBlendRaster, p1rt, SMAA_BlendWeight, smaaSearchSteps);
	if(p1rt) p1rt->Release();

	// Bind edge raster as input texture on stage 0
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaEdgeRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind area/search textures on stages 1 and 2
	if(dev){
		if(g_smaaAreaTex){
			dev->SetTexture(1, g_smaaAreaTex);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		}
		if(g_smaaSearchTex){
			dev->SetTexture(2, g_smaaSearchTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		}
	}

	// Set blend weight shader constants
	float blendP[4] = {0.0f, smaaSearchSteps, 0.0f, 0.0f};
	RwD3D9SetPixelShaderConstant(0, blendP, 1);
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);
	overrideIm2dPixelShader = SMAA_BlendWeight;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Clean up texture stages after Pass 1
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
	}

	// ---- Pass 2: Neighborhood Blending → edgeRaster (temp reuse) ----
	// Render to edgeRaster instead of drawBuffer so Pass 3 temporal can read it
	// while writing to drawBuffer — avoids D3D9 read-write conflict on same surface.
	// edgeRaster is done being read after Pass 1, safe to reuse as temp.
	RwD3D9SetRenderTarget(0, g_smaaEdgeRaster);
	dev->SetViewport(&vpCam);

	IDirect3DSurface9 *p2rt = NULL;
	dev->GetRenderTarget(0, &p2rt);
	if(dbglog_throttle("smaaP2"))
		dbglog("[SMAA-DIAG] Pass2: edgeRaster=%p RT0=%p shader=%p", g_smaaEdgeRaster, p2rt, SMAA_BlendNeighbor);
	if(p2rt) p2rt->Release();

	// Bind original front buffer as color input on stage 0
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind blend raster on stage 1 via RwD3D9SetTexture
	if(!g_smaaBlendTexRW && g_smaaBlendRaster){
		g_smaaBlendTexRW = RwTextureCreate(g_smaaBlendRaster);
		if(g_smaaBlendTexRW){
			RwTextureSetFilterMode(g_smaaBlendTexRW, rwFILTERLINEAR);
			RwTextureSetAddressingU(g_smaaBlendTexRW, rwTEXTUREADDRESSCLAMP);
			RwTextureSetAddressingV(g_smaaBlendTexRW, rwTEXTUREADDRESSCLAMP);
		}
	}
	if(g_smaaBlendTexRW)
		RwD3D9SetTexture(g_smaaBlendTexRW, 1);

	// Set neighborhood blend shader
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);
	overrideIm2dPixelShader = SMAA_BlendNeighbor;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Cleanup texture stages after Pass 2
	RwD3D9SetTexture(NULL, 1);
	d3d9device->SetTexture(2, NULL);
	d3d9device->SetTexture(3, NULL);

	// ---- Pass 3: Temporal Resolve → drawBuffer (final output) ----
	// Reads edgeRaster (current SMAA result) + prevFrameRaster (history),
	// writes directly to drawBuffer. No extra copy-back blit needed.
	if(SMAA_Temporal && g_smaaPrevFrameRaster){
		RwD3D9SetRenderTarget(0, drawBuffer);
		dev->SetViewport(&vpSaved);

		// Bind current SMAA result (edgeRaster) on s0
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaEdgeRaster);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

		// Bind previous frame on s1
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);

		// Temporal constants: blendStrength (low = more history), motionScale
		// On the first frame, the history raster contains undefined data (black).
		// Use blendStrength=1.0 (all current, no history) to avoid a dark flash.
		// Subsequent frames blend normally with accumulated history.
		float blendStrength = g_smaaHistoryValid ? 0.1f : 1.0f;
		float temporalP[4] = { blendStrength, 2.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, temporalP, 1);
		RwD3D9SetPixelShaderConstant(1, screenParams, 1);

		overrideIm2dPixelShader = SMAA_Temporal;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Cleanup
		RwD3D9SetTexture(NULL, 1);

		if(dbglog_throttle("smaaTemporal"))
			dbglog("[SMAA-DIAG] Pass3 Temporal: edge=%p + prev=%p -> drawBuf=%p shader=%p",
				g_smaaEdgeRaster, g_smaaPrevFrameRaster, drawBuffer, SMAA_Temporal);
	} else {
		// No temporal: Pass 2 result is in edgeRaster, copy to drawBuffer
		RwD3D9SetRenderTarget(0, drawBuffer);
		dev->SetViewport(&vpSaved);

		RwRasterPushContext(drawBuffer);
		RwRasterRenderFast(g_smaaEdgeRaster, 0, 0);
		RwRasterPopContext();
	}

	// Save current frame for next frame's temporal history
	RwRasterPushContext(g_smaaPrevFrameRaster);
	RwRasterRenderFast(drawBuffer, 0, 0);
	RwRasterPopContext();
	g_smaaHistoryValid = true;

	// Clean up D3D9 state
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
	}

	// Restore RW render states
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	ImmediateModeRenderStatesReStore();
}

static void DrawVelocityBuffer(void)
{
	if(!config->velocityBufferEnable || !VelocityReconstruct)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;

	// Lazy-init velocity texture
	if(!g_velocityTex){
		if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_velocityTex, NULL))){
			dbglog("[PostFX] DrawVelocityBuffer: CreateTexture velocity failed");
			config->velocityBufferEnable = 0;
			return;
		}
		g_velocityTex->GetSurfaceLevel(0, &g_velocitySurf);
		dbglog("[PostFX] DrawVelocityBuffer: texture created %dx%d", w, h);
	}

	// Bail if depth texture is not available — velocity reconstruction needs depth
	if(!g_ssaoDepthTex){
		if(dbglog_throttle("vel_no_depth"))
			dbglog("[PostFX] DrawVelocityBuffer: g_ssaoDepthTex NULL, skipping");
		return;
	}

	// Store current VP matrix for next frame
	D3DMATRIX curView, curProj;
	dev->GetTransform(D3DTS_VIEW, &curView);
	dev->GetTransform(D3DTS_PROJECTION, &curProj);

	// Compute current VP = View * Proj (D3D9 convention)
	D3DMATRIX curVP;
	{
		// Manual 4x4 matrix multiply (avoids D3DX dependency)
		for(int r = 0; r < 4; r++)
			for(int c = 0; c < 4; c++){
				float sum = 0;
				for(int k = 0; k < 4; k++)
					sum += curView.m[r][k] * curProj.m[k][c];
				curVP.m[r][c] = sum;
			}
	}

	// Skip first frame (no previous VP yet)
	if(!g_prevVPValid){
		memcpy(&g_prevVPMatrix, &curVP, sizeof(D3DMATRIX));
		g_prevVPValid = true;
		return;
	}

	// Compute inverse of current VP (manual 4x4 inverse via cofactors)
	D3DMATRIX invCurVP;
	{
		// Compute 2x2 subdeterminants of last two rows
		float s0 = curVP.m[0][0] * curVP.m[1][1] - curVP.m[1][0] * curVP.m[0][1];
		float s1 = curVP.m[0][0] * curVP.m[1][2] - curVP.m[1][0] * curVP.m[0][2];
		float s2 = curVP.m[0][0] * curVP.m[1][3] - curVP.m[1][0] * curVP.m[0][3];
		float s3 = curVP.m[0][1] * curVP.m[1][2] - curVP.m[1][1] * curVP.m[0][2];
		float s4 = curVP.m[0][1] * curVP.m[1][3] - curVP.m[1][1] * curVP.m[0][3];
		float s5 = curVP.m[0][2] * curVP.m[1][3] - curVP.m[1][2] * curVP.m[0][3];

		float c5 = curVP.m[2][2] * curVP.m[3][3] - curVP.m[3][2] * curVP.m[2][3];
		float c4 = curVP.m[2][1] * curVP.m[3][3] - curVP.m[3][1] * curVP.m[2][3];
		float c3 = curVP.m[2][1] * curVP.m[3][2] - curVP.m[3][1] * curVP.m[2][2];
		float c2 = curVP.m[2][0] * curVP.m[3][3] - curVP.m[3][0] * curVP.m[2][3];
		float c1 = curVP.m[2][0] * curVP.m[3][2] - curVP.m[3][0] * curVP.m[2][2];
		float c0 = curVP.m[2][0] * curVP.m[3][1] - curVP.m[3][0] * curVP.m[2][1];

		float det = s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0;
		float invDet = 1.0f / max(fabs(det), 1e-7f);

		invCurVP.m[0][0] = ( curVP.m[1][1]*c5 - curVP.m[1][2]*c4 + curVP.m[1][3]*c3) * invDet;
		invCurVP.m[0][1] = (-curVP.m[0][1]*c5 + curVP.m[0][2]*c4 - curVP.m[0][3]*c3) * invDet;
		invCurVP.m[0][2] = ( curVP.m[3][1]*s5 - curVP.m[3][2]*s4 + curVP.m[3][3]*s3) * invDet;
		invCurVP.m[0][3] = (-curVP.m[2][1]*s5 + curVP.m[2][2]*s4 - curVP.m[2][3]*s3) * invDet;
		invCurVP.m[1][0] = (-curVP.m[1][0]*c5 + curVP.m[1][2]*c2 - curVP.m[1][3]*c1) * invDet;
		invCurVP.m[1][1] = ( curVP.m[0][0]*c5 - curVP.m[0][2]*c2 + curVP.m[0][3]*c1) * invDet;
		invCurVP.m[1][2] = (-curVP.m[3][0]*s5 + curVP.m[3][2]*s2 - curVP.m[3][3]*s1) * invDet;
		invCurVP.m[1][3] = ( curVP.m[2][0]*s5 - curVP.m[2][2]*s2 + curVP.m[2][3]*s1) * invDet;
		invCurVP.m[2][0] = ( curVP.m[1][0]*c4 - curVP.m[1][1]*c2 + curVP.m[1][3]*c0) * invDet;
		invCurVP.m[2][1] = (-curVP.m[0][0]*c4 + curVP.m[0][1]*c2 - curVP.m[0][3]*c0) * invDet;
		invCurVP.m[2][2] = ( curVP.m[3][0]*s4 - curVP.m[3][1]*s2 + curVP.m[3][3]*s0) * invDet;
		invCurVP.m[2][3] = (-curVP.m[2][0]*s4 + curVP.m[2][1]*s2 - curVP.m[2][3]*s0) * invDet;
		invCurVP.m[3][0] = (-curVP.m[1][0]*c3 + curVP.m[1][1]*c1 - curVP.m[1][2]*c0) * invDet;
		invCurVP.m[3][1] = ( curVP.m[0][0]*c3 - curVP.m[0][1]*c1 + curVP.m[0][2]*c0) * invDet;
		invCurVP.m[3][2] = (-curVP.m[3][0]*s3 + curVP.m[3][1]*s1 - curVP.m[3][2]*s0) * invDet;
		invCurVP.m[3][3] = ( curVP.m[2][0]*s3 - curVP.m[2][1]*s1 + curVP.m[2][2]*s0) * invDet;
	}

	// Render velocity buffer
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	// Set velocity render target
	IDirect3DSurface9 *origRT = NULL;
	dev->GetRenderTarget(0, &origRT);
	dev->SetRenderTarget(0, g_velocitySurf);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 128, 128, 0), 1.0f, 0);

	// Suspend depth hook: unbind INTZ as DS so it can be sampled on s0
	__try {
		DepthHook_Suspend();

		// Bind depth texture on s0
		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

		// Upload constants
		RwD3D9SetPixelShaderConstant(0, &invCurVP, 4); // c0-c3: inverse current VP
		RwD3D9SetPixelShaderConstant(4, &g_prevVPMatrix, 4); // c4-c7: previous VP
		float screenParams[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(8, screenParams, 1); // c8: screen params

		// Render fullscreen quad
		overrideIm2dPixelShader = VelocityReconstruct;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Restore depth hook (re-binds INTZ as DS)
		DepthHook_Restore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[PostFX] DrawVelocityBuffer CRASHED in depth pass exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
	}

	// Restore render target
	dev->SetRenderTarget(0, origRT);
	if(origRT) origRT->Release();

	// Cleanup
	dev->SetTexture(0, NULL);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	// Store VP for next frame
	memcpy(&g_prevVPMatrix, &curVP, sizeof(D3DMATRIX));
}

void
CPostEffects::DrawMotionBlur(void)
{
	if(!config->motionBlurEnable || !MotionBlur_Burnout)
		return;
	if(!pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;

	// Track camera velocity for motion blur
	static float prevCamX = 0, prevCamY = 0, prevCamZ = 0;
	static float prevAtX = 0, prevAtY = 0, prevAtZ = 0;
	static bool camInitialized = false;

	float cameraVelocity = 0.0f;
	float cameraRotation = 0.0f;

	RwFrame *camFrame = RwCameraGetFrame(Scene.camera);
	if(camFrame){
		RwMatrix *camLTM = RwFrameGetLTM(camFrame);
		if(camLTM){
			if(camInitialized){
				float dx = camLTM->pos.x - prevCamX;
				float dy = camLTM->pos.y - prevCamY;
				float dz = camLTM->pos.z - prevCamZ;
				cameraVelocity = sqrtf(dx*dx + dy*dy + dz*dz);

				// Rotation delta (dot product of forward vectors)
				float dot = camLTM->at.x * prevAtX +
				            camLTM->at.y * prevAtY +
				            camLTM->at.z * prevAtZ;
				dot = max(-1.0f, min(1.0f, dot));
				cameraRotation = 1.0f - dot; // 0=no rotation, 2=max rotation
			}
			prevCamX = camLTM->pos.x;
			prevCamY = camLTM->pos.y;
			prevCamZ = camLTM->pos.z;
			prevAtX = camLTM->at.x;
			prevAtY = camLTM->at.y;
			prevAtZ = camLTM->at.z;
			camInitialized = true;
		}
	}

	// Combine camera movement into a single factor (0=still, 1=fast movement)
	float cameraMovement = min(1.0f, (cameraVelocity * 0.1f) + (cameraRotation * 2.0f));

	// Skip if barely moving
	if(cameraMovement < 0.01f)
		return;

	// Optional: reduce blur when camera is moving very fast (camera-aware mode)
	float effectiveStrength = config->motionBlurStrength;
	if(config->motionBlurCameraAware && cameraMovement > 0.8f){
		effectiveStrength *= (1.0f - (cameraMovement - 0.8f) * 2.0f);
		effectiveStrength = max(0.05f, effectiveStrength);
	}

	if(dbglog_throttle("mb_draw"))
		dbglog("[PostFX] DrawMotionBlur: vel=%.3f rot=%.3f mov=%.3f strength=%.3f shader=%p",
			cameraVelocity, cameraRotation, cameraMovement, effectiveStrength, MotionBlur_Burnout);

	// Setup render states
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	// Bind front buffer as current frame (s0)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);

	// Bind velocity buffer on s1 (RG=velocity XY packed to [0,1], B=magnitude)
	if(config->velocityBufferEnable && g_velocityTex){
		dev->SetTexture(1, g_velocityTex);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	}else{
		dev->SetTexture(1, NULL);
	}

	// c0: (blurStrength, radialStrength, maxSamples, speedFactor)
	float c0[4] = {
		effectiveStrength,
		config->motionBlurRadial,
		8.0f, // maxSamples (matches shader loop unroll)
		config->motionBlurSpeedFactor
	};
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	// c1: (screenW, screenH, 1/screenW, 1/screenH)
	float c1[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
	RwD3D9SetPixelShaderConstant(1, c1, 1);

	// c2: (cameraVelocity, cameraRotation, deltaTime, 0)
	float dt = CTimer__ms_fTimeStep / 50.0f; // GTA SA tick rate: 50 fps
	float c2[4] = { cameraMovement, cameraRotation, dt, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	// Render fullscreen quad with motion blur shader
	overrideIm2dPixelShader = MotionBlur_Burnout;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Cleanup texture stages
	dev->SetTexture(1, NULL);
	if(config->velocityBufferEnable){
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	}

	// Restore render states
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();

	// Sync front buffer
	UpdateFrontBuffer();
}

static void
DrawHeightFog(void)
{
	DBGLOG_ENTER("DrawHeightFog");
	if(!config->heightFogEnable || !HeightFog)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;

	// Use SSAO's INTZ depth texture — bail if not available
	if(!g_ssaoDepthTex)
		return;

	if(dbglog_throttle("hfog_draw"))
		dbglog("[PostFX] DrawHeightFog: density=%.4f falloff=%.2f startH=%.1f shader=%p",
			config->heightFogDensity, config->heightFogHeightFalloff,
			config->heightFogStartHeight, HeightFog);

	// Suspend depth hook so g_ssaoDepthTex can be sampled on s1
	__try {
		DepthHook_Suspend();

		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// s0: scene color (front buffer)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		// s1: depth buffer
		dev->SetTexture(1, g_ssaoDepthTex);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// Timecycle reference — used for density modulation and fog color
		CColourSet &tc = CTimeCycle__m_CurrentColours;

		// c0: fogParams — density modulated by timecycle fogStart
		float effectiveDensity = config->heightFogDensity;
		if(tc.fogStart > 0.0f && config->heightFogTimecycleScale > 0.0f) {
			// fogStart lower = denser fog. DynamicSky uses 1/fogStart pattern
			float tcDensity = config->heightFogTimecycleScale / tc.fogStart;
			effectiveDensity += tcDensity;
		}
		float c0[4] = {
			effectiveDensity,
			config->heightFogHeightFalloff,
			config->heightFogStartHeight,
			1.0f // maxFog
		};
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		// c1: fog color (from config or timecycle)
		float fogR = config->heightFogR;
		float fogG = config->heightFogG;
		float fogB = config->heightFogB;
		// Optionally modulate with timecycle fog color
		if(tc.fogStart > 0.0f){
			fogR = tc.lowCloudsR / 255.0f;
			fogG = tc.lowCloudsG / 255.0f;
			fogB = tc.lowCloudsB / 255.0f;
		}
		float c1[4] = { fogR, fogG, fogB, 1.0f };
		RwD3D9SetPixelShaderConstant(1, c1, 1);

		// c2: projInfo (same as SSAO pattern)
		RwCamera *cam = Scene.camera;
		float n = cam->nearPlane;
		float f = cam->farPlane;
		float c2[4] = {
			cam->recipViewWindow.x,
			cam->recipViewWindow.y,
			-n * f / (f - n),
			f / (f - n)
		};
		RwD3D9SetPixelShaderConstant(2, c2, 1);

		// c3: screenSize
		float c3[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(3, c3, 1);

		// c4: camera position
		RwFrame *camFrame = RwCameraGetFrame(cam);
		float c4[4] = { 0, 0, 0, 0 };
		if(camFrame){
			RwMatrix *camLTM = RwFrameGetLTM(camFrame);
			c4[0] = camLTM->pos.x;
			c4[1] = camLTM->pos.y;
			c4[2] = camLTM->pos.z;
		}
		RwD3D9SetPixelShaderConstant(4, c4, 1);

		// c5: camera axis Z components (for world Z reconstruction)
		float c5[4] = { 0, 0, 0, 0 };
		if(camFrame){
			RwMatrix *camLTM = RwFrameGetLTM(camFrame);
			c5[0] = camLTM->right.z;   // camRight.z
			c5[1] = camLTM->up.z;      // camUp.z
			c5[2] = camLTM->at.z;      // camForward.z
		}
		RwD3D9SetPixelShaderConstant(5, c5, 1);

		// Render fullscreen quad
		overrideIm2dPixelShader = HeightFog;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Cleanup
		dev->SetTexture(1, NULL);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		CPostEffects::ImmediateModeRenderStatesReStore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[PostFX] DrawHeightFog CRASHED exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
		CPostEffects::ImmediateModeRenderStatesReStore();
	}

	// Sync front buffer so god rays can read the fogged scene
	CPostEffects::UpdateFrontBuffer();
}

static void
DrawGodRays(void)
{
	DBGLOG_ENTER("DrawGodRays");
	if(!config->godRaysEnable || !GodRays)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;

	// Get sun direction
	float sunD[3];
	GetSunDirection(sunD[0], sunD[1], sunD[2]);

	// Project sun direction to screen space
	// Sun direction points TOWARD sun, use as point at infinity
	D3DMATRIX &viewMat = _RwD3D9D3D9ViewTransform;
	D3DMATRIX &projMat = _RwD3D9D3D9ProjTransform;

	// Manual multiply: sunClip = float4(sunD, 0) * viewMat * projMat
	// Since sunD is a direction (w=0), we only need rotation part
	float vx = sunD[0] * viewMat.m[0][0] + sunD[1] * viewMat.m[1][0] + sunD[2] * viewMat.m[2][0];
	float vy = sunD[0] * viewMat.m[0][1] + sunD[1] * viewMat.m[1][1] + sunD[2] * viewMat.m[2][1];
	float vz = sunD[0] * viewMat.m[0][2] + sunD[1] * viewMat.m[1][2] + sunD[2] * viewMat.m[2][2];

	// Apply projection
	float cx = vx * projMat.m[0][0];
	float cy = vy * projMat.m[1][1];
	float cz = vz * projMat.m[2][2];
	float cw = vz * projMat.m[3][2]; // perspective divide factor

	// Compute screen UV
	float sunScreenX = 0.5f, sunScreenY = 0.5f;
	if(cw != 0.0f){
		float ndcX = cx / cw;
		float ndcY = cy / cw;
		sunScreenX = ndcX * 0.5f + 0.5f;
		sunScreenY = -ndcY * 0.5f + 0.5f; // flip Y for screen space
	}

	// Skip if sun is behind camera or too far off screen
	if(cz < 0.0f || sunScreenX < -0.5f || sunScreenX > 1.5f ||
	   sunScreenY < -0.5f || sunScreenY > 1.5f)
		return;

	if(dbglog_throttle("grad_draw"))
		dbglog("[PostFX] DrawGodRays: sun=(%.2f,%.2f) exp=%.4f dec=%.2f shader=%p",
			sunScreenX, sunScreenY, config->godRaysExposure,
			config->godRaysDecay, GodRays);

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	// Enable additive blending
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

	// s0: scene color (front buffer)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

	// c0: sun screen position
	float c0[4] = { sunScreenX, sunScreenY, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	// c1: ray params
	float c1[4] = {
		config->godRaysExposure,
		config->godRaysDecay,
		config->godRaysDensity,
		config->godRaysWeight
	};
	RwD3D9SetPixelShaderConstant(1, c1, 1);

	// c2: num samples
	float numSamples = (float)config->godRaysNumSamples;
	if(config->godRaysNumSamples < 1) numSamples = 20.0f;
	float c2[4] = { numSamples, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	// Render fullscreen quad
	overrideIm2dPixelShader = GodRays;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	// Cleanup
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
}

void (*CPostEffects::Initialise_orig)(void);
void
CPostEffects::Initialise(void)
{
	Initialise_orig();
	Initialise_skygfx(nil);
}

bool
CPostEffects::Initialise_skygfx(void*)
{
	dbglog("Initialise_skygfx entered");

	InjectHook(0x7FB824, Im2dSetPixelShader_hook);
	InjectHook(0x7FB885, Im2DColorModulationHook);
	InjectHook(0x7FB8A6, Im2DAlphaModulationHook);
	dbglog("  im2d hooks done");

	CreateShaders();
	dbglog("  shaders created");

	// Register scope tags for crash backtrace
	diag_registerScope("DrawSSAO", (void*)DrawSSAO);
	diag_registerScope("DrawSSAO_Overhaul", (void*)DrawSSAO_Overhaul);
	diag_registerScope("DrawVelocityBuffer", (void*)DrawVelocityBuffer);
	diag_registerScope("DrawHeightFog", (void*)DrawHeightFog);
	diag_registerScope("DrawGodRays", (void*)DrawGodRays);
	diag_registerScope("DrawNormalBufferToTexture", (void*)DrawNormalBufferToTexture);
	diag_registerScope("DrawPipeChain", (void*)DrawPipeChain);
	diag_registerScope("RenderIBLBuffer", (void*)RenderIBLBuffer);
	diag_registerScope("ColourFilter_switch", (void*)CPostEffects::ColourFilter_switch);

	dbglog("  scope tags registered");

	grainRaster = RwRasterCreate(64, 64, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
	dbglog("  grain raster=%p", grainRaster);
	return true;
}


// Colorcycle stuff, partly taken from NTAuthority...at least originally

class CFileMgr
{
public:
	static void* OpenFile(const char* filename, const char* mode);

	static void  CloseFile(void* file);
};

class CFileLoader
{
public:
	static char* LoadLine(void* file);
};

WRAPPER void* CFileMgr::OpenFile(const char* filename, const char* mode) { EAXJMP(0x538900); }
WRAPPER void  CFileMgr::CloseFile(void* file) { EAXJMP(0x5389D0); }
WRAPPER char* CFileLoader::LoadLine(void* file) { EAXJMP(0x536F80); }

static int &CTimeCycle__m_ExtraColourWeatherType = *(int*)0xB79E40;
static int &CTimeCycle__m_ExtraColour = *(int*)0xB79E44;
static int &CTimeCycle__m_bExtraColourOn = *(int*)0xB7C484;
static float &CTimeCycle__m_ExtraColourInter = *(float*)0xB79E3C;
static float &CWeather__UnderWaterness = *(float*)0xC8132C;
static float &CWeather__InTunnelness = *(float*)0xC81334;
static int &tunnelWeather = *(int*)0x8CDEE0;


// 24 instead of NUMHOURS because we might be using timecycle_24h with extended extra colour hours
Grade Colorcycle::redGrade[24][NUMWEATHERS];
Grade Colorcycle::greenGrade[24][NUMWEATHERS];
Grade Colorcycle::blueGrade[24][NUMWEATHERS];
bool Colorcycle::initialised;

GradeColorset::GradeColorset(int h, int w)
{
	this->red = Colorcycle::redGrade[h][w];
	this->green = Colorcycle::greenGrade[h][w];
	this->blue = Colorcycle::blueGrade[h][w];
}

void
GradeColorset::Interpolate(GradeColorset *a, GradeColorset *b, float fa, float fb)
{
	this->red.r = fa * a->red.r + fb * b->red.r;
	this->red.g = fa * a->red.g + fb * b->red.g;
	this->red.b = fa * a->red.b + fb * b->red.b;
	this->red.a = fa * a->red.a + fb * b->red.a;
	this->green.r = fa * a->green.r + fb * b->green.r;
	this->green.g = fa * a->green.g + fb * b->green.g;
	this->green.b = fa * a->green.b + fb * b->green.b;
	this->green.a = fa * a->green.a + fb * b->green.a;
	this->blue.r = fa * a->blue.r + fb * b->blue.r;
	this->blue.g = fa * a->blue.g + fb * b->blue.g;
	this->blue.b = fa * a->blue.b + fb * b->blue.b;
	this->blue.a = fa * a->blue.a + fb * b->blue.a;
}

static int timecycleHours[] = { 0, 5, 6, 7, 12, 19, 20, 22, 24 };

void
Colorcycle::Update(GradeColorset *colorset)
{
	float time;
	int curHourSel, nextHourSel;
	int curHour, nextHour;
	float timeInterp, invTimeInterp, weatherInterp, invWeatherInterp;

	time = CClock__ms_nGameClockMinutes / 60.0f
	     + CClock__ms_nGameClockSeconds / 3600.0f
	     + CClock__ms_nGameClockHours;
	if(time >= 23.999f)
		time = 23.999f;

	for(curHourSel = 0; time >= timecycleHours[curHourSel+1]; curHourSel++);
	nextHourSel = (curHourSel + 1) % NUMHOURS;
	curHour = timecycleHours[curHourSel];
	nextHour = timecycleHours[curHourSel+1];
	timeInterp = (time - curHour) / (float)(nextHour - curHour);
	invTimeInterp = 1.0f - timeInterp;
	weatherInterp = CWeather__InterpolationValue;
	invWeatherInterp = 1.0f - weatherInterp;
	GradeColorset curold(curHourSel, CWeather__OldWeatherType);
	GradeColorset nextold(nextHourSel, CWeather__OldWeatherType);
	GradeColorset curnew(curHourSel, CWeather__NewWeatherType);
	GradeColorset nextnew(nextHourSel, CWeather__NewWeatherType);

	// Skipping smog weather handling
	GradeColorset oldInterp, newInterp;
	oldInterp.Interpolate(&curold, &nextold, invTimeInterp, timeInterp);
	newInterp.Interpolate(&curnew, &nextnew, invTimeInterp, timeInterp);
	colorset->Interpolate(&oldInterp, &newInterp, invWeatherInterp, weatherInterp);

	float inc = CTimer__ms_fTimeStep/120.0f;
	if(CTimeCycle__m_bExtraColourOn){
		CTimeCycle__m_ExtraColourInter += inc;
		if(CTimeCycle__m_ExtraColourInter > 1.0f)
			CTimeCycle__m_ExtraColourInter = 1.0f;
	}else{
		CTimeCycle__m_ExtraColourInter -= inc;
		if(CTimeCycle__m_ExtraColourInter < 0.0f)
			CTimeCycle__m_ExtraColourInter = 0.0f;
	}
	if(CTimeCycle__m_ExtraColourInter > 0.0f){
		GradeColorset extraset(CTimeCycle__m_ExtraColour, CTimeCycle__m_ExtraColourWeatherType);
		colorset->Interpolate(colorset, &extraset, 1.0f-CTimeCycle__m_ExtraColourInter, CTimeCycle__m_ExtraColourInter);
	}

	if(CWeather__UnderWaterness > 0.0f){
		GradeColorset curuwset(curHourSel, 20);
		GradeColorset nextuwset(nextHourSel, 20);
		GradeColorset tmpset;
		tmpset.Interpolate(&curuwset, &nextuwset, invTimeInterp, timeInterp);
		colorset->Interpolate(colorset, &tmpset, 1.0f-CWeather__UnderWaterness, CWeather__UnderWaterness);
	}

	if(CWeather__InTunnelness > 0.0f){
		GradeColorset tunnelset(tunnelWeather % NUMHOURS, tunnelWeather / NUMHOURS + EXTRASTART);
		colorset->Interpolate(colorset, &tunnelset, 1.0f-CWeather__InTunnelness, CWeather__InTunnelness);
	}

}

void
Colorcycle::Initialise(void)
{
	int have24h = ModuleList().Get(L"timecycle24") != 0;
	for(int i = 0; i < 24; i++)
		for(int j = 0; j < NUMHOURS; j++){
			redGrade[j][i].r = 1.0f;
			redGrade[j][i].g = 0.0f;
			redGrade[j][i].b = 0.0f;
			redGrade[j][i].a = 0.0f;
			greenGrade[j][i].r = 0.0f;
			greenGrade[j][i].g = 1.0f;
			greenGrade[j][i].b = 0.0f;
			greenGrade[j][i].a = 0.0f;
			blueGrade[j][i].r = 0.0f;
			blueGrade[j][i].g = 0.0f;
			blueGrade[j][i].b = 1.0f;
			blueGrade[j][i].a = 0.0f;
		}
	void *f = CFileMgr::OpenFile("data/colorcycle.dat", "r");
	if(f){
		char *line;
		for(int i = 0; i < NUMWEATHERS; i++){
			for(int j = 0; j < NUMHOURS; j++){
				line = CFileLoader::LoadLine(f);
				sscanf(line, "%f %f %f %f %f %f %f %f %f %f %f %f",
				       &redGrade[j][i].r, &redGrade[j][i].g,
				       &redGrade[j][i].b, &redGrade[j][i].a,
				       &greenGrade[j][i].r, &greenGrade[j][i].g,
				       &greenGrade[j][i].b, &greenGrade[j][i].a,
				       &blueGrade[j][i].r, &blueGrade[j][i].g,
				       &blueGrade[j][i].b, &blueGrade[j][i].a);
				float sum;
				sum = redGrade[j][i].r + redGrade[j][i].g + redGrade[j][i].b;
				if(sum > 1.7f)
					redGrade[j][i].a -= (sum - 1.7f)*0.13f;
				sum = greenGrade[j][i].r + greenGrade[j][i].g + greenGrade[j][i].b;
				if(sum > 1.7f)
					greenGrade[j][i].a -= (sum - 1.7f)*0.13f;
				sum = blueGrade[j][i].r + blueGrade[j][i].g + blueGrade[j][i].b;
				if(sum > 1.7f)
					blueGrade[j][i].a -= (sum - 1.7f)*0.13f;


				redGrade[j][i].r /= 1.5f;
				redGrade[j][i].g /= 1.5f;
				redGrade[j][i].b /= 1.5f;
				redGrade[j][i].a /= 1.5f;
				greenGrade[j][i].r /= 1.5f;
				greenGrade[j][i].g /= 1.5f;
				greenGrade[j][i].b /= 1.5f;
				greenGrade[j][i].a /= 1.5f;
				blueGrade[j][i].r /= 1.5f;
				blueGrade[j][i].g /= 1.5f;
				blueGrade[j][i].b /= 1.5f;
				blueGrade[j][i].a /= 1.5f;
				//printf("%f %f %f %f X %f %f %f %f X %f %f %f %f\n",
				//	redGrade[j][i].r, redGrade[j][i].g, redGrade[j][i].b, redGrade[j][i].a,
				//	greenGrade[j][i].r, greenGrade[j][i].g, greenGrade[j][i].b, greenGrade[j][i].a,
				//	blueGrade[j][i].r, blueGrade[j][i].g, blueGrade[j][i].b, blueGrade[j][i].a);
			}
		}
		if(have24h)
			for(int j = 0; j < NUMHOURS; j++){
				redGrade[j+8][21] = redGrade[j][22];
				greenGrade[j+8][21] = greenGrade[j][22];
				blueGrade[j+8][21] = blueGrade[j][22];
			}
		CFileMgr::CloseFile(f);
	}
	initialised = true;
}
