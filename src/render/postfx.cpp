#include "skygfx.h"
#include "diagnostics.h"
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
// Camera-sized copy of the final graded frame, refreshed at SMAA entry.
// SMAA sampling doctrine: every raster the SMAA shaders touch (scene input,
// edge, blend, prevFrame history, velocity, depth) must be CAMERA-sized with
// content filling UV 0..1, drawn with s_ffQuad (UV 0..1). The game's
// colorfilterVerts are a hardcoded 2048x2048 quad (positions 0..2048, UV
// 0..1) whose visible window only spans UV 0..0.9375 x 0..0.5273 — correct
// for the padded front buffer, but WRONG for camera-sized rasters: pass 3's
// prevFrame/velocity samples landed on the top-left 93.75%x52.7% sub-rect of
// the history (spatially compressed vs the current frame) -> ghosting/
// doubling; and the c1 texel size (1/1920,1/1080) was measured in the wrong
// UV space (1/2048,1/2048 in quad-UV space) -> edge taps off by 1.9x
// vertically. Camera-sized scene copy + UV 0..1 quad makes every tap exact.
static RwRaster *g_smaaSceneRaster = NULL;
static int g_smaaRtWidth = 0, g_smaaRtHeight = 0;
static RwTexture *g_smaaBlendTexRW = NULL;
static RwTexture *g_smaaPrevFrameTexRW = NULL;
static bool s_smaaRastersInitialized = false;
static bool s_smaaPendingInit = false;  // set by DrawSMAA, consumed by SMAATryInitRasters
static bool s_smaaBroken = false;       // fail-open latch: set on init failure OR any
                                        // guarded fault in DrawSMAA; skips until resolution change

// Raw geometry-state snapshot for DrawSMAA (file-scope so smaaGuardBail can
// restore from any early-exit without extra parameters). DrawSMAA raw-writes
// D3DRS_ALPHATESTENABLE/CULLMODE/ZENABLE/ZWRITEENABLE — it was the ONLY
// postfx pass without a SaveRawGeomStates/RestoreRawGeomStates wrap. Those
// raw writes are device-side only, so ImmediateModeRenderStatesReStore()
// (rw-cached) does not undo them: the values leaked past SMAA (last pass of
// the frame) into the next frame's scene and showed up at every pipe entry as
// "[BUILDING] ZWRITE off/desynced on entry (rw=1 dev=0)" and
// "[PipeAlpha] ... test=0". RestoreRawGeomStates re-pushes both layers.
static DWORD s_smaaRawGeom[9];
static bool s_smaaRawGeomSaved = false;

// fwd decl — defined with the other raw-geom helpers ~line 2400
// (non-static: exported via skygfx.h for chars.cpp's SSS blur pass)
void RestoreRawGeomStates(const DWORD *in);

static void smaaRestoreRawGeom(void)
{
	if(s_smaaRawGeomSaved){
		RestoreRawGeomStates(s_smaaRawGeom);
		s_smaaRawGeomSaved = false;
	}
}

// SMAA temporal history: set to true after the first frame's history is written.
// On the first frame, the history raster is black (never rendered to), so the
// temporal blend would produce a very dark image. We use blendStrength=1.0
// (all current, no history) on the first frame to avoid this.
static bool g_smaaHistoryValid = false;
// Hard history TTL: count of valid-history frames since the last full refresh.
// Every 8 frames the temporal resolve forces blendStrength=1.0 (all current) to
// kill the accumulated ghost tail, then restarts the count. Reset to 0 whenever
// history is invalidated (resource release / resolution change).
static int s_smaaHistAge = 0;

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

	// Release scratch camera created by SMAATryInitRasters()
	if(s_smaaInitZRas){ RwRasterDestroy(s_smaaInitZRas); s_smaaInitZRas = NULL; }
	if(s_smaaInitCam){
		RwFrame *f = RwCameraGetFrame(s_smaaInitCam);
		if(f){ RwFrameDestroy(f); }
		RwCameraDestroy(s_smaaInitCam);
		s_smaaInitCam = NULL;
	}
	if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
	if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
	if(g_smaaEdgeRaster){ UntrackRaster(g_smaaEdgeRaster); RwRasterDestroy(g_smaaEdgeRaster); g_smaaEdgeRaster = NULL; }
	if(g_smaaBlendRaster){ UntrackRaster(g_smaaBlendRaster); RwRasterDestroy(g_smaaBlendRaster); g_smaaBlendRaster = NULL; }
	if(g_smaaPrevFrameRaster){ UntrackRaster(g_smaaPrevFrameRaster); RwRasterDestroy(g_smaaPrevFrameRaster); g_smaaPrevFrameRaster = NULL; }
	if(g_smaaSceneRaster){ UntrackRaster(g_smaaSceneRaster); RwRasterDestroy(g_smaaSceneRaster); g_smaaSceneRaster = NULL; }
	g_smaaRtWidth = 0; g_smaaRtHeight = 0;
	s_smaaRastersInitialized = false;
	s_smaaPendingInit = false;
	s_smaaBroken = false;
	g_smaaHistoryValid = false;
	s_smaaHistAge = 0;
	dbglog("ReleaseSMAAStaticResources: done");
}

// Velocity buffer resources
static IDirect3DTexture9 *g_velocityTex = NULL;
static IDirect3DSurface9 *g_velocitySurf = NULL;
static int s_velocityW = 0, s_velocityH = 0;  // cached dims for res-aware recreate
static D3DMATRIX g_prevVPMatrix;
static bool g_prevVPValid = false;

static void ReleaseVelocityBufferResources(void)
{
	if(g_velocityTex){ g_velocityTex->Release(); g_velocityTex = NULL; }
	if(g_velocitySurf){ g_velocitySurf->Release(); g_velocitySurf = NULL; }
	s_velocityW = 0; s_velocityH = 0;
	g_prevVPValid = false;
}

// SSAO overhaul forward declaration
static void DrawSSAO_Overhaul(void);

// Atmospheric PostFX forward declarations
static void DrawVelocityBuffer(void);
static void DrawHeightFog(void);
static void DrawGodRays(void);

// Raw geom-state save/restore helper (defined ~line 2326, used earlier by
// Radiosity_VCS/Blur_VCS; also exported to chars.cpp's SSS blur — see skygfx.h)
bool SaveRawGeomStates(DWORD *out);
void RestoreRawGeomStates(const DWORD *in);

/////
///// Authoritative screen-size cache.
/////
///// RwCameraGetRaster(Scene.camera) is NOT a stable screen-size source
///// outside the postfx entry points: other passes (envmap/reflection and
///// friends) temporarily point the main camera at a NON-screen raster, and
///// every geometry-phase reader observes that transient. Measured in
///// skygfx_dbg.log (PBR pipeline + COLORFILTER_MODERN session, m0169):
/////
/////   [PipeChain] classify RT created 2048x1024   <- transient camRas read
/////   [PipeChain] classify RT created 1920x1080   <- same frame, restored
/////   GetIBLTexture: OK 512x256                   <- camRas/4 == 2048x1024/4
/////
///// i.e. the live read reported 2048x1024 while the actual screen is
///// 1920x1080, TWICE per frame for the whole PBR + Modern-filter window.
///// Anything SIZED during that window inherits the wrong dimensions: the
///// pipe-chain classify pack RT was destroyed+recreated twice per frame
///// (every recreate CLEARS the pack PipeChain pass 3 consumes), the IBL
///// capture was allocated 2:1 instead of 16:9 (its per-frame viewport never
///// matched the texture), and any UV remap derived from the read composites
///// at the wrong scale/coords.
/////
///// Contract: capture ONLY from on-screen postfx entry points
///// (ColourFilter_switch / DrawPipeChain — both verified 1920x1080 every
///// frame in the same log); geometry-phase and mid-frame readers use the
///// cache instead of the live read. The cache self-corrects on resolution
///// changes at the next postfx entry (worst case one frame of stale size —
///// the same failure mode as today, but bounded to one frame instead of
///// every frame).
/////
static int g_screenSizeW = 0;
static int g_screenSizeH = 0;

static void CaptureScreenSize(RwRaster *camRas)
{
	if(camRas && camRas->width > 0 && camRas->height > 0){
		g_screenSizeW = camRas->width;
		g_screenSizeH = camRas->height;
	}
}

// Returns true when a trusted screen size has been captured at least once.
bool GetScreenSize(int *w, int *h)
{
	if(g_screenSizeW > 0 && g_screenSizeH > 0){
		*w = g_screenSizeW;
		*h = g_screenSizeH;
		return true;
	}
	return false;
}


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
void *luminanceReducePS, *luminanceAdaptPS;

// colourFilterEnable=0 bypass ownership (single owner: ColourFilter_Modern's
// tonemap pass). The bypass must skip the COLOUR FILTER, never the sRGB gamma
// encode: the pipes emit linear HDR and TonemapPass is the ONLY LinearToSRGB
// in the chain. Returning before it put the raw linear frame on screen —
// mid-tones land ~2x under their display value, so daylight exteriors render
// near-black while the HUD (already display-referred) stays bright.
// Set for exactly one ColourFilter_Modern call from the bypass path.
static bool s_tonemapIdentityGrade = false;

// --- Unified tonemap: frame-adaptive exposure resources ---
// 8x8 luminance measure target + two 1x1 temporal eye-adaptation targets.
// Allocated lazily, released in ReleaseDefaultPoolResources().
static IDirect3DTexture9 *g_lumaMeasTex = NULL;
static IDirect3DSurface9 *g_lumaMeasSurf = NULL;
static IDirect3DTexture9 *g_lumaAdaptTexA = NULL;
static IDirect3DSurface9 *g_lumaAdaptSurfA = NULL;
static IDirect3DTexture9 *g_lumaAdaptTexB = NULL;
static IDirect3DSurface9 *g_lumaAdaptSurfB = NULL;
static int g_lumaAdaptFlip = 0;   // 0 = current A (prev B), 1 = current B (prev A)

// Lazily allocate the luminance measure/adapt targets. Returns true if ready.
static bool EnsureLuminanceTargets(void)
{
	if(g_lumaMeasTex && g_lumaAdaptTexA && g_lumaAdaptTexB)
		return true;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !luminanceReducePS || !luminanceAdaptPS)
		return false;

	// A16B16G16R16F is a linear floating-point target (no sRGB decode).
	if(!g_lumaMeasTex){
		if(FAILED(dev->CreateTexture(8, 8, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_lumaMeasTex, NULL))){
			dbglog("[Tonemap] luma measure RT create FAILED");
			return false;
		}
		g_lumaMeasTex->GetSurfaceLevel(0, &g_lumaMeasSurf);
	}
	// Dark-frame fix: a freshly created FP16 RT has UNDEFINED content. The
	// adapt pass reads the OTHER 1x1 target as `prev` on its first run —
	// undefined FP16 can be NaN or a huge value. NaN propagates through
	// `prev + s*(measured-prev)` into the adapted luminance, and TonemapPass's
	// `key/lum` then yields NaN exposure -> saturate(NaN) = 0 -> BLACK frame
	// that persists via the ping-pong. A huge prev decays only slowly
	// (adaptSpeed 0.12/frame) -> long dark window. Clear both adapt targets
	// to a sane mid-grey luminance (~0.18) at creation so adaptation always
	// starts from a valid state (LuminanceAdapt's prev<=0 first-frame branch
	// then never sees garbage either).
	if(!g_lumaAdaptTexA){
		if(FAILED(dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_lumaAdaptTexA, NULL))){
			dbglog("[Tonemap] luma adapt RT A create FAILED");
			return false;
		}
		g_lumaAdaptTexA->GetSurfaceLevel(0, &g_lumaAdaptSurfA);
		if(g_lumaAdaptSurfA){
			IDirect3DSurface9 *oldRT = NULL, *oldDS = NULL;
			D3DVIEWPORT9 oldVP;
			dev->GetRenderTarget(0, &oldRT);
			dev->GetDepthStencilSurface(&oldDS);
			dev->GetViewport(&oldVP);
			dev->SetRenderTarget(0, g_lumaAdaptSurfA);
			dev->SetDepthStencilSurface(NULL);
			D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
			dev->SetViewport(&vp);
			// 46/255 ~= 0.18 linear luminance — plausible indoor scene key
			dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 46, 46, 46), 0.0f, 0);
			dev->SetViewport(&oldVP);
			dev->SetRenderTarget(0, oldRT);
			dev->SetDepthStencilSurface(oldDS);
			if(oldRT) oldRT->Release();
			if(oldDS) oldDS->Release();
		}
	}
	if(!g_lumaAdaptTexB){
		if(FAILED(dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_lumaAdaptTexB, NULL))){
			dbglog("[Tonemap] luma adapt RT B create FAILED");
			return false;
		}
		g_lumaAdaptTexB->GetSurfaceLevel(0, &g_lumaAdaptSurfB);
		if(g_lumaAdaptSurfB){
			IDirect3DSurface9 *oldRT = NULL, *oldDS = NULL;
			D3DVIEWPORT9 oldVP;
			dev->GetRenderTarget(0, &oldRT);
			dev->GetDepthStencilSurface(&oldDS);
			dev->GetViewport(&oldVP);
			dev->SetRenderTarget(0, g_lumaAdaptSurfB);
			dev->SetDepthStencilSurface(NULL);
			D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
			dev->SetViewport(&vp);
			dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 46, 46, 46), 0.0f, 0);
			dev->SetViewport(&oldVP);
			dev->SetRenderTarget(0, oldRT);
			dev->SetDepthStencilSurface(oldDS);
			if(oldRT) oldRT->Release();
			if(oldDS) oldDS->Release();
		}
	}
	return true;
}

// Render one fullscreen pass into a raw D3D9 target using a scene raster or an
// explicit stage-0 texture. Mirrors the proven DrawNormalBuffer idiom.
// `verts` selects the quad: colorfilterVerts (2048² quad, correct when
// sampling the padded front buffer across its full UV range) or a caller
// built quad (e.g. the valid-sub-rect luma measure quad).
static void RenderLumaPass(IDirect3DSurface9 *dst, int w, int h,
                           void *ps, RwRaster *sceneRaster, IDirect3DTexture9 *tex0,
                           RwIm2DVertex *verts)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !dst || !ps)
		return;

	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	dev->SetRenderTarget(0, dst);
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 vp = { 0, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f };
	dev->SetViewport(&vp);

	if(sceneRaster){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)sceneRaster);
		dev->SetTexture(0, NULL);
	} else if(tex0){
		dev->SetTexture(0, tex0);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	overrideIm2dPixelShader = ps;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	CPostEffects::ImmediateModeRenderStatesReStore();

	// Raw SetTexture bypasses RW's render-state cache; explicitly clear the cached
	// texture-raster too so the next pass reliably rebinds stage 0 (otherwise the
	// tonemap pass samples a NULL stage 0 and the whole frame goes black).
	dev->SetTexture(0, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
}

// Measure the current graded frame, run temporal eye adaptation, and return the
// adapt texture to bind on the tonemap pass (or NULL on failure).
static IDirect3DTexture9 *RunFrameExposure(float adaptSpeed)
{
	if(!EnsureLuminanceTargets())
		return NULL;

	// 1) Full-res graded frame -> 8x8 luminance measure.
	// The measure quad must cover ONLY the front buffer's valid sub-rect:
	// colorfilterVerts map UV 0..1 across the whole padded 2048x2048 FB, so
	// ~56% of the taps land in the never-written pad region — the measured
	// luminance (and therefore the auto-exposure key) is diluted/offset by
	// garbage. Build an 8x8-position quad whose UVs span exactly
	// (screenW/fbW, screenH/fbH) — the valid 1920x1080 content region.
	RwIm2DVertex lumaQuad[4];
	RwIm2DVertex *measureVerts = colorfilterVerts;
	int lumaScrW = 0, lumaScrH = 0;
	RwRaster *lumaFB = CPostEffects::pRasterFrontBuffer;
	if(GetScreenSize(&lumaScrW, &lumaScrH) && lumaFB &&
	   lumaFB->width > 0 && lumaFB->height > 0){
		float uMax = min(1.0f, (float)lumaScrW / max((float)lumaFB->width, 1.0f));
		float vMax = min(1.0f, (float)lumaScrH / max((float)lumaFB->height, 1.0f));
		static const float lpx[4] = { 0.0f, 0.0f, 8.0f, 8.0f };
		static const float lpy[4] = { 0.0f, 8.0f, 8.0f, 0.0f };
		static const float luu[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		static const float lvv[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
		for(int i = 0; i < 4; i++){
			RwIm2DVertex *v = &lumaQuad[i];
			v->x = lpx[i];
			v->y = lpy[i];
			v->z = 0.0f;
			v->rhw = 1.0f;
			v->u = luu[i] * uMax;
			v->v = lvv[i] * vMax;
			RwIm2DVertexSetIntRGBA(v, 255, 255, 255, 255);
		}
		measureVerts = lumaQuad;
	}
	RenderLumaPass(g_lumaMeasSurf, 8, 8, luminanceReducePS,
	               CPostEffects::pRasterFrontBuffer, NULL, measureVerts);

	// 2) 8x8 measure + previous 1x1 -> current 1x1 eye adaptation
	IDirect3DTexture9 *curTex  = g_lumaAdaptFlip ? g_lumaAdaptTexB : g_lumaAdaptTexA;
	IDirect3DSurface9 *curSurf = g_lumaAdaptFlip ? g_lumaAdaptSurfB : g_lumaAdaptSurfA;
	IDirect3DTexture9 *prevTex = g_lumaAdaptFlip ? g_lumaAdaptTexA : g_lumaAdaptTexB;

	IDirect3DDevice9 *dev = d3d9device;
	if(dev){
		dev->SetTexture(0, g_lumaMeasTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetTexture(1, prevTex);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}
	float adaptP[4] = { adaptSpeed, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, adaptP, 1);

	RenderLumaPass(curSurf, 1, 1, luminanceAdaptPS, NULL, g_lumaMeasTex, colorfilterVerts);
	if(dev) dev->SetTexture(1, NULL);

	g_lumaAdaptFlip ^= 1;
	return curTex;
}
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
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer camera raster is NULL!");
		return;
	}
	// Screen-size doctrine: the camera raster is NOT a stable screen-size
	// source mid-chain. When a geometry pass (envmap/reflection) temporarily
	// points it at a non-screen raster, copying that at 0,0 into the padded
	// front buffer composites the wrong region -> blocky colour rectangles
	// across the interior. Bail the copy; the next postfx entry re-syncs.
	int ufbScrW = 0, ufbScrH = 0;
	if(GetScreenSize(&ufbScrW, &ufbScrH) &&
	   (camRas->width != ufbScrW || camRas->height != ufbScrH)){
		if(dbglog_throttle("ufb_mismatch"))
			dbglog("[PostFX] UpdateFrontBuffer SKIP: camRas %dx%d != screen %dx%d (transient raster)",
				camRas->width, camRas->height, ufbScrW, ufbScrH);
		return;
	}
	if(dbglog_throttle("ufb_copy"))
		dbglog("[PostFX] UpdateFrontBuffer pRasFB=%p(%dx%d) camRas=%p(%dx%d)",
			CPostEffects::pRasterFrontBuffer,
			RwRasterGetWidth(CPostEffects::pRasterFrontBuffer), RwRasterGetHeight(CPostEffects::pRasterFrontBuffer),
			camRas, camRas->width, camRas->height);
	RwCameraEndUpdate(Scene.camera);
	RwRasterPushContext(CPostEffects::pRasterFrontBuffer);
	RwRaster *copyResult = RwRasterRenderFast(camRas, 0, 0);
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

	// Full-screen quad maps UV 0..1 across the SCREEN. Derive its extents
	// from the trusted screen-size cache (live camRas only as the pre-capture
	// fallback): a transient camRas read here left the quad sized to a
	// non-screen raster, compositing the VCS trails at the wrong scale.
	int screenW = 256 * resMult, screenH = 128 * resMult;
	if(!GetScreenSize(&screenW, &screenH)){
		RwRaster *camRasLive = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(camRasLive){
			screenW = camRasLive->width;
			screenH = camRasLive->height;
		}
	}

	// TODO: tex coords correct?
	makequad(radiosity_vcs_vertices, 256 * resMult, 128 * resMult);
	makequad(radiosity_vcs_vertices+4, screenW, screenH);

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

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
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
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom); // BLENDOP/SRCBLEND/DESTBLEND/BLENDFACTOR leak past ReStore
		rawGeomSaved = false;
	}
}

RwD3D9Vertex blur_vcs_vertices[24];
RwImVertexIndex blur_vcs_Indices[] = {
	0, 1, 2, 2, 1, 3,
	4, 5, 6, 6, 5, 7,
	8, 9, 10, 10, 9, 11,
};
RwRaster *lastFrameBuffer;
static int s_blurVCS_lastWidth = 0, s_blurVCS_lastHeight = 0;
static int s_blurVCS_justInitialized = 0;
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
	int i;
	int bufw, bufh;
	int screenw, screenh;
	int intensity;
	bufw = CPostEffects::pRasterFrontBuffer->width;
	bufh = CPostEffects::pRasterFrontBuffer->height;

	/*if(GetAsyncKeyState(VK_F7) & 0x8000){
		s_blurVCS_justInitialized = 1;
		return;
	}*/

	if(s_blurVCS_lastWidth != bufw || s_blurVCS_lastHeight != bufh){
		if(lastFrameBuffer)
			RwRasterDestroy(lastFrameBuffer);
		lastFrameBuffer = RwRasterCreate(bufw, bufh, CPostEffects::pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);
		s_blurVCS_justInitialized = 1;
		s_blurVCS_lastWidth = bufw;
		s_blurVCS_lastHeight = bufh;
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

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
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
	if(s_blurVCS_justInitialized)
		s_blurVCS_justInitialized = 0;
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
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom); // SRCBLEND/DESTBLEND/BLENDFACTOR leak past ReStore
		rawGeomSaved = false;
	}
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
// Hoisted from function-statics (were invisible to Reset → dangling after
// ReleaseDefaultPoolResources). Released alongside lastFrameBuffer below.
static RwRaster *s_radiosityShaderWorkBuffer = nil;
static RwRaster *s_radiosityWorkBuffer = nil;

void
CPostEffects::Radiosity_shader(int intensityLimit, int filterPasses, int renderPasses, int intensity)
{
	if(!pRasterFrontBuffer){
		dbglog("Radiosity_shader: pRasterFrontBuffer is NULL, skipping");
		return;
	}
	RwRaster *workBuffer = s_radiosityShaderWorkBuffer;
	if(workBuffer)
		if(workBuffer->width != pRasterFrontBuffer->width ||
		   workBuffer->height != pRasterFrontBuffer->height ||
		   workBuffer->depth != pRasterFrontBuffer->depth){
			RwRasterDestroy(workBuffer);
			workBuffer = nil;
		}
	if(workBuffer == nil)
		workBuffer = RwRasterCreate(pRasterFrontBuffer->width, pRasterFrontBuffer->height, pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);
	s_radiosityShaderWorkBuffer = workBuffer;

	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);
	if(!drawBuffer){
		dbglog("Radiosity_shader: camera raster NULL, skipping");
		return;
	}
	// Screen dims for the composite UV remap (c1): the remap maps the padded
	// 2048² front buffer onto the screen quad, so deriving it from a transient
	// camRas read (see GetScreenSize) would composite the radiosity buffer at
	// the wrong scale/coords — a full-screen blocky mismatch. Use the trusted
	// cache; live drawBuffer dims only as pre-capture fallback.
	int drawW = drawBuffer->width, drawH = drawBuffer->height;
	GetScreenSize(&drawW, &drawH);
	if(drawW < 1) drawW = 1;
	if(drawH < 1) drawH = 1;

	// Three-layer contract: this pass flips the camera raster and pushes
	// ONE/ONE additive blend + vertex alpha through the rw cache — snapshot
	// the raw device alpha block first so nothing leaks past the pass into
	// the world draw (BLENDOP/SRCBLEND/DESTBLEND/BLENDFACTOR family).
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom);

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
	params[2] *= drawW/640.0f;

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
	// Screen dims (trusted cache) — NOT the live drawBuffer read. A transient
	// camRas read here composites the radiosity buffer at the wrong scale and
	// produces the full-screen blocky mismatch. Live read is only the
	// pre-capture fallback that seeded drawW/drawH above.
	float maxu = drawW - offu; //off*2;
	float maxv = drawH - offv; //off*2;
	float cu = (offu*(drawW+0.5f) + offu/*off*2*/*0.5f) / max((float)drawW, 1.0f);
	float cv = (offv*(drawH+0.5f) + offv/*off*2*/*0.5f) / max((float)drawH, 1.0f);

	params[0] = cu / max((float)pRasterFrontBuffer->width, 1.0f);
	params[1] = cv / max((float)pRasterFrontBuffer->height, 1.0f);
	params[2] = (maxu-minu) / max((float)drawW, 1.0f);
	params[3] = (maxv-minv) / max((float)drawH, 1.0f);
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
	// Pass breadcrumb (throttled): Radiosity is a full-screen compositing pass
	// and had NO logging at all, while the game only calls it on its outdoor
	// path (hooks main.cpp:4007-4008, right behind ColourFilter_switch). If a
	// covered-frame forensics pass ever sees this tag immediately before the
	// symptom, start here. m_bRadiosityDebug is the ONE switch that turns both
	// composite variants into a REPLACE of the screen with the (heavily
	// downsampled, edge-only looking) radiosity buffer instead of a blend.
	if(dbglog_throttle("radiosity"))
		dbglog("[PostFX] Radiosity: ff=%d vcs=%d doRad=%d mode=%d int=%d limit=%d passes=%d debug=%d",
			config->radiosityEnable, config->vcsTrails, config->doRadiosity, config->radiosity,
			intensity, intensityLimit, filterPasses, (int)m_bRadiosityDebug);
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

	// REPLACE hazard (upgrades the breadcrumb above): m_bRadiosityDebug is a
	// game global (0xC402CD) that nothing in SA or this mod ever writes — a
	// garbage nonzero value flips VERTEXALPHAENABLE off in BOTH composite
	// variants (Radiosity_shader and the classic blend below), replacing the
	// screen with the downsampled radiosity buffer. Force the composite off
	// when nonzero. VCS trails above is unaffected (never reads the flag).
	if(m_bRadiosityDebug){
		if(dbglog_throttle("rad_dbg"))
			dbglog("[Radiosity] m_bRadiosityDebug=%d nonzero — composite suppressed", (int)m_bRadiosityDebug);
		return;
	}

	if(!config->doRadiosity)
		return;

	if(config->radiosity == 1){
		Radiosity_shader(intensityLimit, filterPasses, renderPasses, intensity);
		return;
	}

	RwRaster *workBuffer = s_radiosityWorkBuffer;
	if(workBuffer)
		if(workBuffer->width != pRasterFrontBuffer->width ||
		   workBuffer->height != pRasterFrontBuffer->height ||
		   workBuffer->depth != pRasterFrontBuffer->depth){
			RwRasterDestroy(workBuffer);
			workBuffer = nil;
		}
	if(workBuffer == nil)
		workBuffer = RwRasterCreate(pRasterFrontBuffer->width, pRasterFrontBuffer->height, pRasterFrontBuffer->depth, rwRASTERTYPECAMERATEXTURE);
	s_radiosityWorkBuffer = workBuffer;

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

	// SEH-guarded choke-point: resets overrideIm2dPixelShader on every exit
	// (incl. fault) so a faulting dispatch can't leave the override bound.
	guardedIm2DRender(ps, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_generic");

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

	// Filter-quad blend doctrine: the grading/tonemap quads must REPLACE the
	// frame, never blend with it. The rw cache can carry a stale blend block
	// from an earlier pass (e.g. BLENDFACTOR additive from VCS trails, or
	// HUD SRCALPHA with a quad whose vertex alpha is 0 — colorfilterVerts
	// carry emissiveColor=0) which darkens or no-ops the graded output.
	// Force ONE/ZERO (replace) on both layers for the filter draws; the
	// cleanup below restores SRCALPHA/INVSRCALPHA.
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

	// Pass 1: Color grading (diagonal matrix multiply). SEH-guarded so the
	// override is cleared on fault as well as success. SKIPPED on the
	// colorFilterEnable=0 bypass (s_tonemapIdentityGrade): that path only
	// wants the tonemap's gamma encode, not the timecycle tint.
	if(!s_tonemapIdentityGrade)
		guardedIm2DRender(gradingPS, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "cf_modern_p1");

	// Copy graded linear output to pRasterFrontBuffer for tonemap input
	UpdateFrontBuffer();

	// --- Unified tonemap: measure the graded frame for adaptive exposure ---
	// timecyc supplies COLOUR (pass 1 above); the rendered frame supplies
	// BRIGHTNESS here, so exposure is frame-consistent across all pipes.
	// Only runs when the feature is enabled; otherwise the legacy timecyc-only
	// exposure path below is used unchanged. The identity-grade bypass forces
	// it OFF: TonemapPass re-derives sceneLuma (and therefore PostGrade's
	// adaptive intensity) from the measured frame whenever c5.w >= 0.5, which
	// would re-introduce grading the bypass asked us to skip.
	bool autoExposureOn = !s_tonemapIdentityGrade &&
		(config && config->tonemapAutoExposure && luminanceReducePS && luminanceAdaptPS);
	float adaptSpeed = config ? config->tonemapAdaptSpeed : 0.12f;
	IDirect3DTexture9 *frameLumaTex = autoExposureOn ? RunFrameExposure(adaptSpeed) : NULL;
	if(autoExposureOn && !frameLumaTex)
		autoExposureOn = false;

	// Pass 2: Hable/Uncharted 2 filmic tonemap + sRGB gamma encode
	if(tonemapPassPS){
		// Re-setup render states for tonemap pass (reads pRasterFrontBuffer).
		// Re-assert replace blending — UpdateFrontBuffer's blit ran in between.
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

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
		// NOTE: SA CColourSet stores ambient/directional as floats ALREADY in 0-1
		// range (game divides timecyc.dat values by 255; weather.cpp blends them
		// with altR/255.0f and clamps to [0,1] — same convention). Do NOT divide
		// by 255 here: doing so collapsed sceneLuma to ~0.003, which clamped
		// exposure to 1.5 + toe to 0.33 → the permanent gray veil, day and night.
		float tcAmbientLuma  = 0.299f*tc.ambientR + 0.587f*tc.ambientG + 0.114f*tc.ambientB;
		float tcDirLuma      = 0.299f*tc.directionalR + 0.587f*tc.directionalG + 0.114f*tc.directionalB;
		float sceneLuma      = tcAmbientLuma + tcDirLuma * 0.5f;
		float shadowNorm     = max(0.0f, min(1.0f, (float)tc.shadowStrength / 255.0f));
		float fogFactor      = max(0.0f, min(1.0f, tc.fogStart / 500.0f));       // less fog = 1, heavy fog = 0
		float clouds         = max(0.0f, min(1.0f, tc.cloudAlpha));
		float sunBright      = max(0.0f, min(2.0f, tc.spriteBrightness));
		float streetLights   = max(0.0f, min(1.0f, tc.lightsOnGroundBrightness));
		float envMult        = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
		// Dark-frame guard: envMult feeds the exposure product directly. A
		// garbage/negative value (pipe not initialised this frame, cutscene
		// reset, etc.) would scale exposure to <= 0 -> black frame. Clamp to
		// the plausible physical range before use.
		envMult = max(0.0f, min(2.0f, envMult));

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
		// Mood exposure (prefs brightness + carcols + sun + garage). When
		// auto-exposure is on, the frame-derived term inside TonemapPass supplies
		// brightness, so the timecyc reciprocal is dropped here (no double-apply).
		exposure = baseExposure * carcolsAdapt * sunDampen * garageDampen;
		if(!autoExposureOn)
			exposure *= sceneExposure;

		// Interior: timecycle encodes the correct mood, no extra dampening

		// --- Toe: shadow lift driven by sceneLuma + timecycle shadow depth ---
		// sceneLuma is normalized 0-1, so toe responds properly to time-of-day.
		// NOTE: In the Hable/Uncharted2 filmic tonemap, LOWER D (toeStrength) values
		// produce MORE shadow lift, not less — the denominator term D*F dominates.
		// The minimum of 0.05 was causing a permanent "gray veil" shadow lift even
		// in bright scenes. Raised to 0.15 for midday to keep blacks solid.
		// Night scenes still get up to 0.35 for gentle shadow detail.
		// D RISES with daylight: in the Hable/Uncharted2 curve, HIGHER D = tighter toe =
		// solid blacks; LOWER D = more shadow lift. The old form drove D down to its
		// 0.15 floor at midday (max gray veil) and up to ~0.26 at night — inverted, so
		// daylight washed out and shadows vanished. Now night keeps the proven gentle
		// lift (~0.27) and midday clamps high (tight toe, blacks stay black).
		float toeStrengthVal = 0.22f + sceneLuma * 1.0f + shadowNorm * 0.08f;
		toeStrength = max(0.18f, min(0.55f, toeStrengthVal));

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

		// --- colorFilterEnable=0 bypass: IDENTITY grade -----------------------
		// The bypass path routes through this tonemap so the frame still gets
		// its sRGB gamma encode; everything downstream must be a no-op:
		//   sceneLuma 0        -> PostGrade adaptive scale 0 => brightness/
		//                         contrast/lift/curve all neutral
		//   blackLift 0        -> no black-level lift
		//   curveP x/y 0       -> PivotCurve bit-exact early-out (see below)
		// Exposure/toe stay on the normal timecycle path so overall brightness
		// matches the graded path — only the timecycle TINT (pass 1) is skipped.
		if(s_tonemapIdentityGrade){
			sceneLuma = 0.0f;
			gradeBrightness = 0.0f;
			gradeContrast = 1.0f;
			gradeLift = 0.0f;
			gradeCurve = 0.0f;
			blackLift = 0.0f;
		}

		if(tonemapLogCounter++ % 3600 == 0){
			extern int16 &CWeather__OldWeatherType;
			extern int16 &CWeather__NewWeatherType;
			extern float &CWeather__InterpolationValue;
			extern uint8 &CClock__ms_nGameClockHours;
			dbglog("[Tonemap] TC sceneLuma=%.3f amb=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) shadow=%d fog=%.0f cloud=%.2f sun=%.2f street=%.2f hour=%d wOld=%d wNew=%d wInterp=%.2f cutscene=%d interior=%d",
				sceneLuma, tc.ambientR, tc.ambientG, tc.ambientB, tc.directionalR, tc.directionalG, tc.directionalB,
				tc.shadowStrength, tc.fogStart, clouds, sunBright, streetLights,
				CClock__ms_nGameClockHours, CWeather__OldWeatherType, CWeather__NewWeatherType, CWeather__InterpolationValue,
				isCutscene, isInterior);
			dbglog("[Tonemap] VAL exp=%.3f toe=%.3f bl=%.4f ct=%.2f br=%.3f lift=%.3f curve=%.2f",
				exposure, toeStrength, blackLift, gradeContrast, gradeBrightness, gradeLift, gradeCurve);
		}

		// Pack into c5: {exposure, toeStrength, sceneLuma, autoExposureFlag}
		// c5.w is the frame-adaptive exposure flag read by TonemapPass.
		float tonemapP[4] = { exposure, toeStrength, sceneLuma, autoExposureOn ? 1.0f : 0.0f };
		RwD3D9SetPixelShaderConstant(5, tonemapP, 1);

		// Pack into c6: {brightness, contrast, lift, curveBlend} — all timecycle-driven
		float gradeP[4] = { gradeBrightness, gradeContrast, gradeLift, gradeCurve };
		RwD3D9SetPixelShaderConstant(6, gradeP, 1);

		// Pack into c7: {blackLift, minExposure, maxExposure, keyStrength}
		// The auto-exposure clamp + key blend are only read when c5.w >= 0.5.
		// Dark-frame guard: enforce a sane exposure FLOOR — the TonemapPass
		// computes autoExp = key/lum and clamps to [c7.y, c7.z]; a config
		// minExposure of 0 (or negative) lets a bogus huge measured luminance
		// drive the exposure to ~0 -> black frame. Floor at 0.15 and keep
		// maxExposure >= minExposure.
		float minExpCfg = config ? config->tonemapMinExposure : 0.5f;
		float maxExpCfg = config ? config->tonemapMaxExposure : 2.0f;
		minExpCfg = max(0.15f, minExpCfg);
		maxExpCfg = max(maxExpCfg, minExpCfg);
		float blackP[4] = {
			blackLift,
			autoExposureOn ? minExpCfg : 0.0f,
			autoExposureOn ? maxExpCfg : 0.0f,
			autoExposureOn ? (config ? config->tonemapKeyStrength : 1.0f) : 0.0f
		};
		RwD3D9SetPixelShaderConstant(7, blackP, 1);

		// Pack into c8: {curveLows, curveHighs, curveMid, 0} — user pivot
		// curve applied at the TonemapPass OUTPUT (per-channel gain ramp
		// around the mid pivot; bit-exact identity when both intensities
		// are 0). Uploaded here for THIS draw only — other passes (DynamicSky
		// weatherP, screenParams) re-upload their own c8 before theirs.
		// Identity-grade bypass forces both intensities to exact 0.0 so
		// PivotCurve's early-out returns the pixel untouched.
		float curveP[4] = {
			s_tonemapIdentityGrade ? 0.0f : (config ? config->tonemapCurveLows : 0.0f),
			s_tonemapIdentityGrade ? 0.0f : (config ? config->tonemapCurveHighs : 0.0f),
			config ? config->tonemapCurveMid : 0.5f,
			0.0f
		};
		RwD3D9SetPixelShaderConstant(8, curveP, 1);

		// Bind the frame luminance (s1) for frame-adaptive exposure.
		IDirect3DDevice9 *cfDev = d3d9device;
		if(autoExposureOn && frameLumaTex && cfDev){
			cfDev->SetTexture(1, frameLumaTex);
			cfDev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			cfDev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			cfDev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			cfDev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		}

		overrideIm2dPixelShader = tonemapPassPS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
		if(cfDev) cfDev->SetTexture(1, NULL);
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
static void CopyDepthToPrev(void);

/////
///// Local fullscreen quad — SSAO composites and the IV grade draw with this
///// instead of the game's colorfilterVerts. colorfilterVerts carry
///// emissiveColor=0x00000000 (they are only coloured by the game's own
///// colour-filter paths, which COLORFILTER_MODERN never runs), so any
///// fixed-function draw using them modulates texture x 0 = black; they are
///// also a hardcoded 2048x2048 quad whose UVs misalign on smaller camera
///// rasters. White + RT-sized = correct multiply/replace and UV 0..1.
/////
static RwIm2DVertex s_ffQuad[4];
static RwImVertexIndex s_ffQuadIdx[6] = { 0, 1, 2, 0, 2, 3 };

static void
SetupFullscreenQuad(float w, float h)
{
	// Corner order matches colorfilterVerts: TL, BL, BR, TR
	static const float px[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const float py[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float uu[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const float vv[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	for(int i = 0; i < 4; i++){
		RwIm2DVertex *v = &s_ffQuad[i];
		v->x = px[i] * w;
		v->y = py[i] * h;
		v->z = 0.0f;
		v->rhw = 1.0f;
		v->u = uu[i];
		v->v = vv[i];
		RwIm2DVertexSetIntRGBA(v, 255, 255, 255, 255);
	}
}

/////
///// IV mode grade — first real consumer of config->ivMode + iv* sliders
///// (previously dead config fields: read from INI, shown in the debug menu,
///// never read by any pass). Grade runs after the colour-filter switch on
///// the freshly graded/tonemapped frame; every slider maps to a live pixel
///// shader constant uploaded each frame.
/////

// ps_3_0 bytecode of IVGrade.hlsl (compiled with the project's FXC:
//   fxc /T ps_3_0 /E main IVGrade.hlsl)
// Registers:
//   c0 = {exposure, desaturation, gamma, 0}
//   c1 = {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
//   c2 = {1/screenW, 1/screenH, 0, 0}
//   s0 = scene (front buffer)
alignas(4) static const BYTE g_ivGradePSBytes[] =
{
      0,   3, 255, 255, 254, 255, 
     58,   0,  67,  84,  65,  66, 
     28,   0,   0,   0, 179,   0, 
      0,   0,   0,   3, 255, 255, 
      4,   0,   0,   0,  28,   0, 
      0,   0,   0,   1,   0,   0, 
    172,   0,   0,   0, 108,   0, 
      0,   0,   2,   0,   0,   0, 
      1,   0,   2,   0, 116,   0, 
      0,   0,   0,   0,   0,   0, 
    132,   0,   0,   0,   2,   0, 
      1,   0,   1,   0,   6,   0, 
    116,   0,   0,   0,   0,   0, 
      0,   0, 139,   0,   0,   0, 
      2,   0,   2,   0,   1,   0, 
     10,   0, 116,   0,   0,   0, 
      0,   0,   0,   0, 146,   0, 
      0,   0,   3,   0,   0,   0, 
      1,   0,   2,   0, 156,   0, 
      0,   0,   0,   0,   0,   0, 
    103, 114,  97, 100, 101,  48, 
      0, 171,   1,   0,   3,   0, 
      1,   0,   4,   0,   1,   0, 
      0,   0,   0,   0,   0,   0, 
    103, 114,  97, 100, 101,  49, 
      0, 103, 114,  97, 100, 101, 
     50,   0, 115,  99, 101, 110, 
    101,  84, 101, 120,   0, 171, 
      4,   0,  12,   0,   1,   0, 
      1,   0,   1,   0,   0,   0, 
      0,   0,   0,   0, 112, 115, 
     95,  51,  95,  48,   0,  77, 
    105,  99, 114, 111, 115, 111, 
    102, 116,  32,  40,  82,  41, 
     32,  72,  76,  83,  76,  32, 
     83, 104,  97, 100, 101, 114, 
     32,  67, 111, 109, 112, 105, 
    108, 101, 114,  32,  57,  46, 
     50,  57,  46,  57,  53,  50, 
     46,  51,  49,  49,  49,   0, 
     81,   0,   0,   5,   3,   0, 
     15, 160,  10, 215,  35,  60, 
      0,   0, 128,  63,   0,   0, 
      0,  63, 205, 204,  76,  61, 
     81,   0,   0,   5,   4,   0, 
     15, 160, 135,  22, 153,  62, 
    162,  69,  22,  63, 213, 120, 
    233,  61, 111,  18, 131,  58, 
     81,   0,   0,   5,   5,   0, 
     15, 160,   0,   0,   0,   0, 
      0,   0, 128,  64,   0,   0, 
      0, 128,   0,   0, 200,  66, 
     81,   0,   0,   5,   6,   0, 
     15, 160,   0,   0, 128,  62, 
      0,   0,  64, 191,   0,   0, 
      0, 192,   0,   0,  64,  64, 
     31,   0,   0,   2,   5,   0, 
      0, 128,   0,   0,   3, 144, 
     31,   0,   0,   2,   0,   0, 
      0, 144,   0,   8,  15, 160, 
      2,   0,   0,   3,   0,   0, 
      3, 128,   3,   0, 170, 161, 
      0,   0, 228, 144,  90,   0, 
      0,   4,   0,   0,   1, 128, 
      0,   0, 228, 128,   0,   0, 
    228, 128,   5,   0,   0, 160, 
      7,   0,   0,   2,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      6,   0,   0,   2,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      1,   0,   0,   2,   1,   0, 
     15, 128,   3,   0, 228, 160, 
      2,   0,   0,   3,   0,   0, 
      2, 128,   1,   0,   0, 129, 
      1,   0,  85, 160,   6,   0, 
      0,   2,   0,   0,   4, 128, 
      1,   0,  85, 160,  88,   0, 
      0,   4,   0,   0,   2, 128, 
      0,   0,  85, 128,   0,   0, 
    170, 128,   5,   0, 255, 160, 
      5,   0,   0,   3,   0,   0, 
     17, 128,   0,   0,  85, 128, 
      0,   0,   0, 128,   4,   0, 
      0,   4,   0,   0,   2, 128, 
      0,   0,   0, 128,   6,   0, 
    170, 160,   6,   0, 255, 160, 
      5,   0,   0,   3,   0,   0, 
      1, 128,   0,   0,   0, 128, 
      0,   0,   0, 128,   4,   0, 
      0,   4,   0,   0,   1, 128, 
      0,   0,  85, 128,   0,   0, 
      0, 129,   3,   0,  85, 160, 
     11,   0,   0,   3,   2,   0, 
      1, 128,   0,   0,   0, 128, 
      5,   0,   0, 160,  11,   0, 
      0,   3,   0,   0,   1, 128, 
      1,   0, 170, 160,   1,   0, 
      0, 128,  32,   0,   0,   3, 
      3,   0,   1, 128,   2,   0, 
      0, 128,   0,   0,   0, 128, 
      2,   0,   0,   3,   0,   0, 
      1, 128,   3,   0,   0, 128, 
      3,   0,  85, 161,   4,   0, 
      0,   4,   0,   0,   1, 128, 
      1,   0,   0, 160,   0,   0, 
      0, 128,   1,   0,  85, 128, 
     11,   0,   0,   3,   2,   0, 
      1, 128,   0,   0,   0, 128, 
      5,   0,   0, 160,   1,   0, 
      0,   2,   0,   0,   2, 128, 
      5,   0,  85, 160,   5,   0, 
      0,   3,   0,   0,   5, 128, 
      0,   0,  85, 128,   2,   0, 
    212, 160,   1,   0,   0,   2, 
      3,   0,   9, 128,   0,   0, 
    164, 129,   1,   0,   0,   2, 
      3,   0,   6, 128,   5,   0, 
    170, 160,   2,   0,   0,   3, 
      3,   0,  15, 128,   3,   0, 
    228, 128,   0,   0,  68, 144, 
     66,   0,   0,   3,   4,   0, 
     15, 128,   3,   0, 228, 128, 
      0,   8, 228, 160,  66,   0, 
      0,   3,   3,   0,  15, 128, 
      3,   0, 238, 128,   0,   8, 
    228, 160,   4,   0,   0,   4, 
      0,   0,   9, 128,   2,   0, 
    100, 160,   0,   0,  85, 128, 
      0,   0, 100, 144,   1,   0, 
      0,   2,   0,   0,   6, 128, 
      0,   0, 196, 144,  66,   0, 
      0,   3,   5,   0,  15, 128, 
      0,   0, 228, 128,   0,   8, 
    228, 160,  66,   0,   0,   3, 
      0,   0,  15, 128,   0,   0, 
    238, 128,   0,   8, 228, 160, 
      2,   0,   0,   3,   2,   0, 
     14, 128,   4,   0, 144, 128, 
      5,   0, 144, 128,   2,   0, 
      0,   3,   0,   0,   7, 128, 
      0,   0, 228, 128,   2,   0, 
    249, 128,   2,   0,   0,   3, 
      0,   0,   7, 128,   3,   0, 
    228, 128,   0,   0, 228, 128, 
      4,   0,   0,   4,   0,   0, 
      7, 128,   0,   0, 228, 128, 
      6,   0,   0, 160,   6,   0, 
     85, 160,   5,   0,   0,   3, 
      2,   0,  14, 128,   0,   0, 
    144, 128,   1,   0, 255, 160, 
     88,   0,   0,   4,   0,   0, 
      7, 128,   0,   0, 228, 128, 
      2,   0, 249, 128,   5,   0, 
      0, 160,   2,   0,   0,   3, 
      0,   0,   8, 128,   1,   0, 
      0, 128,   0,   0,   0, 161, 
     88,   0,   0,   4,   0,   0, 
      8, 128,   0,   0, 255, 128, 
      1,   0,  85, 128,   0,   0, 
      0, 160,  66,   0,   0,   3, 
      3,   0,  15, 128,   0,   0, 
    228, 144,   0,   8, 228, 160, 
      5,   0,   0,   3,   2,   0, 
     14, 128,   0,   0, 255, 128, 
      3,   0, 144, 128,   8,   0, 
      0,   3,   1,   0,   1, 128, 
      2,   0, 249, 128,   4,   0, 
    228, 160,   4,   0,   0,   4, 
      3,   0,   7, 128,   3,   0, 
    228, 128,   0,   0, 255, 129, 
      1,   0,   0, 128,   5,   0, 
      0,   3,   0,   0,  24, 128, 
      1,   0, 170, 128,   0,   0, 
     85, 160,   4,   0,   0,   4, 
      1,   0,   7, 128,   0,   0, 
    255, 128,   3,   0, 228, 128, 
      2,   0, 249, 128,  88,   0, 
      0,   4,   1,   0,   7, 128, 
      0,   0, 255, 129,   2,   0, 
    249, 128,   1,   0, 228, 128, 
     11,   0,   0,   3,   2,   0, 
     14, 128,   1,   0, 144, 128, 
      5,   0,   0, 160,  15,   0, 
      0,   2,   3,   0,   1, 128, 
      2,   0,  85, 128,  15,   0, 
      0,   2,   3,   0,   2, 128, 
      2,   0, 170, 128,  15,   0, 
      0,   2,   3,   0,   4, 128, 
      2,   0, 255, 128,  11,   0, 
      0,   3,   0,   0,   8, 128, 
      0,   0, 170, 160,   1,   0, 
    255, 128,   6,   0,   0,   2, 
      1,   0,   8, 128,   0,   0, 
    255, 128,   2,   0,   0,   3, 
      0,   0,   8, 128,   0,   0, 
    255, 128,   3,   0,  85, 161, 
      2,   0,   0,   3,   0,   0, 
      8, 128,   0,   0, 255, 140, 
      4,   0, 255, 160,   5,   0, 
      0,   3,   2,   0,  14, 128, 
      3,   0, 144, 128,   1,   0, 
    255, 128,  14,   0,   0,   2, 
      3,   0,   1, 128,   2,   0, 
     85, 128,  14,   0,   0,   2, 
      3,   0,   2, 128,   2,   0, 
    170, 128,  14,   0,   0,   2, 
      3,   0,   4, 128,   2,   0, 
    255, 128,  88,   0,   0,   4, 
      1,   0,   7, 128,   0,   0, 
    255, 128,   1,   0, 228, 128, 
      3,   0, 228, 128,   2,   0, 
      0,   3,   0,   0,   7, 128, 
      0,   0, 228, 128,   1,   0, 
    228, 128,   1,   0,   0,   2, 
      3,   0,   9, 128,   1,   0, 
    228, 160,   2,   0,   0,   3, 
      2,   0,   6, 128,   3,   0, 
    204, 129,   4,   0, 255, 160, 
     88,   0,   0,   4,   0,   0, 
      7, 128,   2,   0,  85, 128, 
      1,   0, 228, 128,   0,   0, 
    228, 128,   5,   0,   0,   3, 
      1,   0,   7, 128,   2,   0, 
      0, 128,   0,   0, 228, 128, 
     88,   0,   0,   4,   0,   8, 
      7, 128,   2,   0, 170, 128, 
      0,   0, 228, 128,   1,   0, 
    228, 128,   1,   0,   0,   2, 
      0,   8,   8, 128,   3,   0, 
     85, 160, 255, 255,   0,   0
};


static void *s_ivGradePS = NULL;
static bool s_ivGradePSFailed = false;

// Camera-sized frame copy that feeds the IV grade (see DrawIVGrade): the
// grade's tex-space math (vignette centred at 0.5, c2 bloom taps =
// 1/screenW) assumes UV 0..1 spans a FULL frame of content, but
// pRasterFrontBuffer is a padded (e.g. 2048x2048) raster with the frame only
// in its top-left — sampling it with a UV 0..1 quad read black below
// y = h*h/fbH (the IV-mode black band). Private w x h copy = content fills
// UV 0..1. Managed like Blur_VCS's lastFrameBuffer (PushContext + RenderFast).
static RwRaster *s_ivGradeRas = NULL;
static int s_ivGradeW = 0, s_ivGradeH = 0;

// ============================================================
// Raw geometry render-state guard.
// ImmediateModeRenderStates{Store,Set,ReStore} only round-trip the 10 RW-
// cached state IDs; every postfx pass ALSO writes raw D3D9 states
// (ALPHATEST/CULL/Z*/blend factors) that bypass that cache. A pass that
// exits without re-asserting them — or faults mid-way — leaks the values
// into the NEXT frame's geometry (evidenced: vegetation drawing over
// buildings after the SSAO multiply left ALPHABLEND/SRCBLEND/DESTBLEND
// wrong). Save/restore the raw states around every pass that writes them.
// Save BEFORE DepthHook_Suspend (it writes raw ZENABLE itself); restore
// AFTER ReStore so these values win on any overlap.
// ============================================================
static const D3DRENDERSTATETYPE s_rawGeomStateIds[9] = {
	D3DRS_ALPHATESTENABLE,
	D3DRS_CULLMODE,
	D3DRS_ZENABLE,
	D3DRS_ZWRITEENABLE,
	D3DRS_ALPHABLENDENABLE,
	D3DRS_SRCBLEND,
	D3DRS_DESTBLEND,
	D3DRS_BLENDOP,      // raw-written by Radiosity_VCS/Blur_VCS — NOT in the
	D3DRS_BLENDFACTOR   // game ReStore's 10-state list, leaks past ReStore
};

bool
SaveRawGeomStates(DWORD *out)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return false;
	for(int i = 0; i < RAW_GEOM_STATE_COUNT; i++)
		dev->GetRenderState(s_rawGeomStateIds[i], &out[i]);
	return true;
}

void
RestoreRawGeomStates(const DWORD *in)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	// Layer (2) driver cache first (pending[]+dirty, flushed before every
	// draw): a raw layer-(3)-only restore leaves pending==applied at the
	// leaked value, so the next RwD3D9SetRenderState to THAT value would
	// no-op against an already-matching pending and could never push it
	// down — and worse, a pending!=applied pair left behind by the pass
	// would flush OVER our raw restore and put the leak straight back.
	// D3D9 domain, no rw conversion needed.
	for(int i = 0; i < RAW_GEOM_STATE_COUNT; i++)
		RwD3D9SetRenderState(s_rawGeomStateIds[i], in[i]);
	// Layer (3) raw — authoritative device write, wins over any pending
	// bookkeeping disagreement.
	for(int i = 0; i < RAW_GEOM_STATE_COUNT; i++)
		dev->SetRenderState(s_rawGeomStateIds[i], in[i]);
	// Canonical on-state LAST, forced on ALL THREE layers. These passes
	// dirty the rw cache to FALSE with rw-only restores that no-op when
	// the cache never changed back, and a stale saved FALSE would otherwise
	// re-create the every-frame "[BUILDING] ZWRITE off/desynced (rw=1
	// dev=0)" the pipe entries log: the rw sets re-sync the cache
	// (FALSE->TRUE transition re-pushes to the device too), the RwD3D9
	// sets re-sync pending[] (otherwise a pending TRUE!=applied FALSE
	// flush would undo the raw TRUE below on the NEXT draw), and the raw
	// writes guarantee the device value. Vertex alpha is forced the same
	// way: SSS/postfx rw-set it FALSE, and a cache stuck FALSE would turn
	// every later alpha-blended draw opaque.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_ZENABLE, TRUE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

static void*
EnsureIVGradePS(void)
{
	if(s_ivGradePS || s_ivGradePSFailed)
		return s_ivGradePS;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev){
		s_ivGradePSFailed = true;
		return NULL;
	}
	HRESULT hr = dev->CreatePixelShader((const DWORD*)g_ivGradePSBytes,
		(IDirect3DPixelShader9**)&s_ivGradePS);
	if(FAILED(hr) || !s_ivGradePS){
		dbglog("[PostFX] IVGrade CreatePixelShader FAILED hr=0x%08X", (unsigned)hr);
		s_ivGradePS = NULL;
		s_ivGradePSFailed = true;
		return NULL;
	}
	dbglog("[PostFX] IVGrade pixel shader created sh=%p", s_ivGradePS);
	return s_ivGradePS;
}

static void
DrawIVGrade(void)
{
	// Gate decision breadcrumb (lane-A, build-2 outdoor-occluder hunt):
	// the log line "[IVGrade] 0.00 ms" is PerfTimer's UNCONDITIONAL scope
	// exit line (skygfx.h:79 `~PerfTimer(){ dbglog(...) }`) — it fires even
	// when this function returns instantly at the gate below, so it is NOT
	// evidence the grade ran. This throttle logs the ACTUAL gate input
	// every ~2s so the next test proves either way: ivMode=0 + no
	// "DrawIVGrade:" lines => grade never ran, occluder is elsewhere.
	// ivMode writers to watch: derivePipelineFromPipes (main.cpp:597-607,
	// flips it on pipe-change into/out-of the GTAIV family), menu_inject
	// "GTA IV Mode" toggle (menu_inject.cpp:123), presets.cpp:130,
	// debug-menu checkbox.
	if(dbglog_throttle("ivgrade_gate"))
		dbglog("[PostFX] IVGrade GATE: ivMode=%d pipeline=%d buildingPipe=%d vehiclePipe=%d cfEnable=%d -> %s",
			config ? (int)config->ivMode : -1,
			config ? config->pipeline : -1,
			config ? config->buildingPipe : -1,
			config ? config->vehiclePipe : -1,
			config ? (int)config->colorFilterEnable : -1,
			(config && config->ivMode) ? "RUN" : "SKIP");
	if(!config || !config->ivMode)
		return;
	void *ps = EnsureIVGradePS();
	if(!ps)
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	// Screen-size doctrine: size the private grade copy from the trusted
	// cache (live camRas only as the pre-capture fallback). If the camera
	// raster is temporarily pointed at a non-screen raster (envmap/
	// reflection), skip — copying it would composite at the wrong scale.
	int w = 0, h = 0;
	if(!GetScreenSize(&w, &h)){
		w = camRas->width;
		h = camRas->height;
	}
	if(w < 1 || h < 1)
		return;
	if(camRas->width != w || camRas->height != h){
		if(dbglog_throttle("ivgrade_skip"))
			dbglog("[PostFX] DrawIVGrade SKIP: camRas %dx%d != screen %dx%d (transient raster)",
				camRas->width, camRas->height, w, h);
		return;
	}
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	if(dbglog_throttle("ivgrade"))
		dbglog("[PostFX] DrawIVGrade: desat=%.2f gamma=%.2f exposure=%.2f vig=(%.2f,%.2f,%.2f) bloom=%.2f",
			config->ivDesaturation, config->ivGamma, config->ivExposure,
			config->ivVignetteIntensity, config->ivVignetteRadius,
			config->ivVignetteContrast, config->ivBloomIntensity);

	// Feed the grade a CAMERA-SIZED frame copy instead of pRasterFrontBuffer.
	// Root cause of the IV-mode black band: the front buffer is a padded
	// (2048x2048) raster holding the 1920x1080 frame 1:1 at its TOP-LEFT
	// (UpdateFrontBuffer = RwRasterRenderFast at 0,0), while s_ffQuad maps
	// UV 0..1 across the screen — screen rows below y = h*h/fbH (~570 on
	// 1080/2048) sampled past the content into black. A private w x h copy
	// has content filling UV 0..1, so the grade's authored tex-space math
	// (vignette centred at 0.5, c2 bloom taps = 1/screenW) stays exact.
	// The game's own colorfilterVerts avoid the band with 0..2048 positions
	// + UV 0..1 (visible window = content region) — same idea, different
	// mechanism (we can't move positions without breaking tex 0..1 math).
	// Mirrors Blur_VCS's lastFrameBuffer copy (PushContext + RenderFast).
	if(!s_ivGradeRas || s_ivGradeW != w || s_ivGradeH != h){
		if(s_ivGradeRas){ RwRasterDestroy(s_ivGradeRas); s_ivGradeRas = NULL; }
		s_ivGradeRas = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
		s_ivGradeW = w;
		s_ivGradeH = h;
		if(!s_ivGradeRas){
			dbglog("[PostFX] DrawIVGrade: frame-copy raster create failed %dx%d", w, h);
			return;
		}
	}
	{
		// Copy the CURRENT post-colour-filter camera frame into the private
		// raster (same EndUpdate/PushContext/RenderFast/BeginUpdate dance as
		// UpdateFrontBuffer). Fail-open: if the copy fails, skip the grade
		// this frame rather than grading garbage.
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(s_ivGradeRas);
		RwRaster *copyResult = RwRasterRenderFast(camRas, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		if(!copyResult){
			dbglog("[PostFX] DrawIVGrade: frame copy failed (RenderFast NULL), skipping grade");
			return;
		}
	}

	SetupFullscreenQuad((float)w, (float)h);

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // no depth hook in this pass
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s_ivGradeRas);

	// c0: {exposure, desaturation, gamma, 0} — live from the debug-menu sliders
	float ex = (config->ivExposure > 0.01f) ? config->ivExposure : 1.0f;
	float gm = (config->ivGamma > 0.05f) ? config->ivGamma : 1.0f;
	float c0[4] = { ex, config->ivDesaturation, gm, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);
	// c1: {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
	float c1[4] = {
		config->ivVignetteIntensity,
		config->ivVignetteRadius,
		config->ivVignetteContrast,
		config->ivBloomIntensity
	};
	RwD3D9SetPixelShaderConstant(1, c1, 1);
	// c2: {1/screenW, 1/screenH, 0, 0} — bloom tap offsets
	float c2[4] = { 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f), 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	overrideIm2dPixelShader = ps;
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
	overrideIm2dPixelShader = nil;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	// Leave the front buffer in sync so motion blur / height fog / god rays
	// sample the IV-graded frame even if they skip their own start-sync.
	CPostEffects::UpdateFrontBuffer();
}

// Defined in main.cpp (pipe-conjunction fix #4) — re-derives
// config->pipeline/colorFilter/ivMode from the actual building/vehicle pipes.
extern void derivePipelineFromPipes(Config *c);

void
CPostEffects::ColourFilter_switch(RwRGBA rgb1, RwRGBA rgb2)
{
	DBGLOG_ENTER("ColourFilter_switch");

	// Pipe-conjunction #4 — EVERY-FRAME re-derivation, before any gate below
	// reads config->pipeline (SSAO dispatch region above runs first in this
	// function but doesn't key on pipeline; the timecycle gate and the PBR
	// Modern-filter force further down do). This is the catch-all for pipe
	// writers with no reload hook of their own: pause-menu menu_inject
	// cycles (menu_inject.cpp:124 buildingPipe-only) and the legacy DebugMenu
	// vars (debugmenu_ui.cpp:169/:177, nil callbacks). Snapshot-cheap: only
	// acts when the pipes (or a deliberate ivMode write) actually changed.
	derivePipelineFromPipes(config);

	// Pipeline-source breadcrumb (lane-A, build-2 flip hunt): the reported
	// "pipeline=3 (Mobile)" <-> "pipeline=0" flip — note pipeline 0 is
	// PIPELINE_PBR (skygfx.h:178), NOT PS2 — is NOT two different variables:
	// there is no g_pipeline-style global anywhere in src; every postfx gate
	// (this log, the bypass cfGate, the PBR Modern force) reads the SAME
	// config->pipeline, which derivePipelineFromPipes rewrites EVERY FRAME
	// from the ground-truth pipes (buildingPipe/vehiclePipe). A flip with
	// filter=8 (MODERN, the rule-3 PBR lock at main.cpp:585) therefore means
	// SOMETHING IS WRITING THE PIPES at runtime (menu_inject buildingPipe
	// cycle, debug-menu pipelineOverride application
	// (debugmenu_ui.cpp:612-615), presets.cpp:117-118). postfx stays on the
	// EFFECTIVE value — the render callbacks key off the pipes, so reading
	// the derived pipeline is the only self-consistent choice. This line
	// logs all sources so the next test pins the writer.
	if(dbglog_throttle("cf_pipeline"))
		dbglog("[PostFX] CF-SRC: effective pipeline=%d override=%d bp=%d vp=%d colorFilter=%d ivMode=%d",
			config->pipeline, config->pipelineOverride, config->buildingPipe,
			config->vehiclePipe, config->colorFilter, (int)config->ivMode);

	// Log entry with full context for crash correlation
	if(dbglog_throttle("cf_switch"))
		dbglog("[PostFX] ColourFilter_switch: filter=%d pipeline=%d smaa=%d ssao=%d motionBlur=%d",
			config->colorFilter, config->pipeline, config->smaaEnable,
			config->ssaoEnable, config->motionBlurEnable);

	if(!CPostEffects::pRasterFrontBuffer){
		DBGLOG_BAIL("ColourFilter_switch", "pRasterFrontBuffer NULL");
		return;
	}

	// Trusted screen-size capture point (postfx entry — main camera is on the
	// screen here, see GetScreenSize above). Geometry-phase readers
	// (pipe-chain classify, IBL capture) size themselves from this cache
	// instead of a live read that can hit a transient non-screen raster.
	CaptureScreenSize(Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL);

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
		// Overhaul path runs when Temporal is ON, OR when Blur Passes > 0 —
		// the bilateral blur loop lives ONLY inside DrawSSAO_Overhaul
		// (:3836), so requiring ssaoTemporalEnable here made blur dead on
		// the old path (old DrawSSAO has no blur at all). Verified safe
		// without temporal: entry guard :3698 does not test ssaoTemporalEnable,
		// ping-pong history RTs are always allocated + NULL-checked (:3725),
		// and the history weight is validity-gated (and now also gated on
		// ssaoTemporalEnable at :3810) so no temporal accumulation happens
		// when Temporal is off. SSAO_Temporal still required (shader load).
		if(config->ssaoEnable && SSAO_Temporal &&
		   (config->ssaoTemporalEnable || config->ssaoBlurPasses > 0)){
			DrawSSAO_Overhaul();    // Quarter-res SSAO (+temporal, +bilateral blur)
		}else{
			DrawSSAO();             // Fallback: old full-res SSAO (no blur)
		}
	}

	// Copy current depth to prev-depth history AFTER SSAO has consumed
	// the previous frame's depth, so temporal reprojection (T5) works correctly.
	CopyDepthToPrev();

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

		// Gamma ownership — the bypass skips the COLOUR FILTER, not the sRGB
		// encode (see s_tonemapIdentityGrade). Only routes through the Modern
		// tonemap when this frame WOULD have run it (filter resolves to
		// MODERN, or PBR forces it): pipelines whose filter never gamma-
		// encoded must not gain an encode here (double gamma).
		int cfGate = colorFilter;
		if(config->pipeline == PIPELINE_PBR)
			cfGate = COLORFILTER_MODERN;
		if(cfGate == COLORFILTER_MODERN && tonemapPassPS){
			s_tonemapIdentityGrade = true;
			ColourFilter_Modern(rgb1, rgb2); // pass-1 tint skipped, tonemap runs
			s_tonemapIdentityGrade = false;
			// Re-sync: ColourFilter_Modern leaves pRasterFrontBuffer at the
			// PRE-tonemap linear frame (the normal path re-syncs further down,
			// after the IVGrade/height-fog chain — the bypass returns here).
			// Without this, chars_drawSSSBlur and any later FB consumer would
			// read the un-gamma-encoded frame again.
			UpdateFrontBuffer();
		}

		// SSS still runs even when colour filter is bypassed - it's independent
		chars_drawSSSBlur();
		// Keep the per-frame report alive during bypass windows: this early
		// return used to skip postfxReportSummary (below), so the report went
		// silent exactly when the filter was bypassed — the "PostFX-REPORT
		// stops while rendering continues" symptom. NOTE: the
		// colorFilterEnable flag itself is owned by main.cpp (two call sites
		// alternate it) — reported to orchestrator, NOT fixed here.
		postfxReportFrame++;
		postfxReportSummary();
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

	// IV mode grade: runs on the freshly graded/tonemapped frame, before
	// motion blur / height fog / god rays. Consumes config->ivMode and the
	// iv* sliders (exposure/desaturation/gamma/vignette/bloom) as live
	// shader constants — previously all dead config fields.
	{
		PERF_SCOPE("IVGrade");
		DrawIVGrade();
	}

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

// Wave-2 temporal history: previous-frame depth (own R32F render target, never aliased)
IDirect3DTexture9 *g_prevDepthTex = NULL;
static IDirect3DSurface9 *g_prevDepthSurf = NULL;
bool g_prevDepthValid = false;
static int s_prevDepthW = 0, s_prevDepthH = 0;  // cached dims for res-aware recreate
static bool s_prevDepthLogDone = false;         // one-shot failure log

// Wave-2 helper defined below InitSSAOResources; forward-declared for its two call sites.
static void CreatePrevDepthTexture(IDirect3DDevice9 *dev, int w, int h);

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
static int s_normalBufW = 0, s_normalBufH = 0;  // cached dims for res-aware recreate
// True only after DrawNormalBufferToTexture completed at least one full write.
// A freshly created D3DPOOL_DEFAULT RT is driver garbage (cleared to the
// invalid-normal marker in GetNormalBufferTexture, but the marker must also
// never be blended into geometry normals), so consumers gate binding on this
// flag — see the s4 bind gate in buildingPipe.cpp PBR callback.
bool g_normalBufferHasContent = false;

// Wave-2 temporal history: previous-frame normals (half-res, same dims as g_normalBufferTex)
IDirect3DTexture9 *g_prevNormalTex = NULL;
static IDirect3DSurface9 *g_prevNormalSurf = NULL;
bool g_normalHistoryValid = false;

extern void *NormalBufferShader;

// 4-Pipe chain
static IDirect3DTexture9 *g_pipeChainTexA = NULL;
static IDirect3DSurface9 *g_pipeChainSurfA = NULL;
static IDirect3DTexture9 *g_pipeChainTexB = NULL;
static IDirect3DSurface9 *g_pipeChainSurfB = NULL;

extern void *PipeChainShader;

// g_pipeChainClassify* statics are defined AFTER this function (~:4560) —
// forward-declare a helper so the Reset release below can reach them
// (direct references would be use-before-declaration).
static void ReleasePipeChainClassifyResources(void);

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

	// Wave-2 prev-depth history (own R32F RT, never aliased — always released here)
	if(g_prevDepthTex){ g_prevDepthTex->Release(); g_prevDepthTex = NULL; }
	if(g_prevDepthSurf){ g_prevDepthSurf->Release(); g_prevDepthSurf = NULL; }
	g_prevDepthValid = false;
	s_prevDepthW = 0; s_prevDepthH = 0;

	// SSAO output raster (RW-managed)
	if(g_ssaoOutputRaster){ RwRasterDestroy(g_ssaoOutputRaster); g_ssaoOutputRaster = nil; }

	// VCS blur last-frame buffer
	if(lastFrameBuffer){ RwRasterDestroy(lastFrameBuffer); lastFrameBuffer = nil; }
	s_blurVCS_lastWidth = 0; s_blurVCS_lastHeight = 0; s_blurVCS_justInitialized = 1;

	// IV grade frame copy (RW-managed; recreated on next DrawIVGrade)
	if(s_ivGradeRas){ RwRasterDestroy(s_ivGradeRas); s_ivGradeRas = nil; }
	s_ivGradeW = 0; s_ivGradeH = 0;

	// Hoisted radiosity work buffers (were function-static, invisible to Reset)
	if(s_radiosityShaderWorkBuffer){ RwRasterDestroy(s_radiosityShaderWorkBuffer); s_radiosityShaderWorkBuffer = nil; }
	if(s_radiosityWorkBuffer){ RwRasterDestroy(s_radiosityWorkBuffer); s_radiosityWorkBuffer = nil; }
	
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
	s_normalBufW = 0; s_normalBufH = 0;
	g_normalBufferHasContent = false;

	// Wave-2 prev-normal history (D3DPOOL_DEFAULT)
	if(g_prevNormalTex){ g_prevNormalTex->Release(); g_prevNormalTex = NULL; }
	if(g_prevNormalSurf){ g_prevNormalSurf->Release(); g_prevNormalSurf = NULL; }
	g_normalHistoryValid = false;

	// Pipe chain (D3DPOOL_DEFAULT)
	if(g_pipeChainTexA){ g_pipeChainTexA->Release(); g_pipeChainTexA = NULL; }
	if(g_pipeChainSurfA){ g_pipeChainSurfA->Release(); g_pipeChainSurfA = NULL; }
	if(g_pipeChainTexB){ g_pipeChainTexB->Release(); g_pipeChainTexB = NULL; }
	if(g_pipeChainSurfB){ g_pipeChainSurfB->Release(); g_pipeChainSurfB = NULL; }

	// CRITICAL-1: classify pack RT (D3DPOOL_DEFAULT, created lazily by
	// GetPipeChainClassifySurf ~:4577) was NEVER released here → after
	// PipeChain had run once, device Reset failed D3DERR_INVALIDCALL.
	ReleasePipeChainClassifyResources();

// Unified tonemap luminance targets (D3DPOOL_DEFAULT)
ReleaseLuminanceTargets();

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
	// DepthHook_ReleaseResources() just freed g_intzTex; clear the alias too or
	// g_ssaoDepthTex dangles and the re-alias guard in InitSSAOResources never
	// fires -> use-after-free on the next frame. Safe unconditionally (the
	// non-aliased case already nulled these above).
	g_ssaoDepthTex = NULL;
	g_ssaoDepthSurf = NULL;

	dbglog("ReleaseDefaultPoolResources: done");
}

// Release luminance target resources (g_lumaMeas*, g_lumaAdapt*)
void ReleaseLuminanceTargets(void)
{
	// g_lumaMeasTex (and related) – released here to free memory
	if(g_lumaMeasTex){ g_lumaMeasTex->Release(); g_lumaMeasTex = nil; }
	if(g_lumaMeasSurf){ g_lumaMeasSurf->Release(); g_lumaMeasSurf = nil; }
	if(g_lumaAdaptTexA){ g_lumaAdaptTexA->Release(); g_lumaAdaptTexA = nil; }
	if(g_lumaAdaptSurfA){ g_lumaAdaptSurfA->Release(); g_lumaAdaptSurfA = nil; }
	if(g_lumaAdaptTexB){ g_lumaAdaptTexB->Release(); g_lumaAdaptTexB = nil; }
	if(g_lumaAdaptSurfB){ g_lumaAdaptSurfB->Release(); g_lumaAdaptSurfB = nil; }
	g_lumaAdaptFlip = 0;
}

// Central cleanup on device reset (called from depthhook::hook_Reset)
extern void ReleaseEnvMapResources(void);
extern void ReleaseVehiclePipeCaches(void);
extern void ReleaseBuildingPipeCaches(void);
extern void ReleaseNeoCarPipeResources(void);

void OnDeviceReset(void)
{
	// Release all D3DPOOL_DEFAULT resources (including luminance targets)
	ReleaseDefaultPoolResources();

	// Release RW-managed caches that survive device reset
	ReleaseEnvMapResources();
	ReleaseVehiclePipeCaches();
	ReleaseBuildingPipeCaches();
	ReleaseNeoCarPipeResources();

	// Reset guard state: never leave the VEH in silent pass-through mode or
	// with a stale phase tag across a device Reset (gap: previously the
	// hoisted DrawSMAA guard flag survived reset).
	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	g_guardDepth = 0;
}
static inline bool CheckDeviceState(void)
{
	// Fail fast while the device is lost — TestCooperativeLevel is cheap and
	// every RT bind / Im2D draw downstream would fault or fail anyway.
	if (!d3d9device || d3d9device->TestCooperativeLevel() == D3DERR_DEVICELOST)
		return false;
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
			CreatePrevDepthTexture(dev, w, h);
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
					CreatePrevDepthTexture(dev, w, h);
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

// Wave-2: create the previous-frame depth history texture (own R32F render target).
// A plain R32F render target is used instead of INTZ/DEPTHSTENCIL because INTZ is a
// texture format that can neither be read back via GetRenderTargetData (requires
// POOL_SYSTEMMEM) nor copied via StretchRect (requires a plain depth-stencil surface).
// The history is instead populated by rendering a fullscreen quad that samples the
// current scene depth (see CopyDepthToPrev).
// Res-aware: mirrors the velocity/normal buffer idiom — recreate when dims change.
static void CreatePrevDepthTexture(IDirect3DDevice9 *dev, int w, int h)
{
	if(!dev || w < 1 || h < 1) return;
	if(g_prevDepthTex && s_prevDepthW == w && s_prevDepthH == h) return;

	// Release on dim change (or re-init)
	if(g_prevDepthTex){ g_prevDepthTex->Release(); g_prevDepthTex = NULL; }
	if(g_prevDepthSurf){ g_prevDepthSurf->Release(); g_prevDepthSurf = NULL; }
	g_prevDepthValid = false;

	HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_R32F, D3DPOOL_DEFAULT, &g_prevDepthTex, NULL);
	if(SUCCEEDED(hr)){
		g_prevDepthTex->GetSurfaceLevel(0, &g_prevDepthSurf);
		s_prevDepthW = w;
		s_prevDepthH = h;
		dbglog("InitSSAOResources: prevDepth R32F texture created OK %dx%d", w, h);
	}else{
		dbglog("InitSSAOResources: CreateTexture prevDepth R32F failed hr=0x%08X", hr);
		g_prevDepthTex = NULL;
		g_prevDepthSurf = NULL;
		s_prevDepthW = 0;
		s_prevDepthH = 0;
		g_prevDepthValid = false;
	}
}

// Wave-2: copy current depth (g_ssaoDepthTex) into the previous-frame history texture
// by rendering a fullscreen quad whose pixel shader samples the scene depth and writes
// it to the R32F render target. History is only marked valid when the draw succeeds.
static void CopyDepthToPrev(void)
{
	if(!g_ssaoDepthTex){
		g_prevDepthValid = false;
		return;
	}
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev){ g_prevDepthValid = false; return; }
	if(!ClampShader){ g_prevDepthValid = false; return; }

	// Res-aware lazy (re)create of the history texture (full camera resolution).
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(!camRas){ g_prevDepthValid = false; return; }
	CreatePrevDepthTexture(dev, camRas->width, camRas->height);

	if(!g_prevDepthTex || !g_prevDepthSurf){ g_prevDepthValid = false; return; }

	// Save current RT, DS, viewport
	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	bool ok = false;

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	dev->SetRenderTarget(0, g_prevDepthSurf);
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 pdVP = { 0, 0, (DWORD)s_prevDepthW, (DWORD)s_prevDepthH, 0.0f, 1.0f };
	dev->SetViewport(&pdVP);

	__try {
		// Suspend depth hook so g_ssaoDepthTex (INTZ) can be sampled while not bound as DS
		DepthHook_Suspend();

		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);

		// ClampShader is the generic s0→RT copy shader; c0={min,max,tonemap,0} with
		// [0,1] clamp is a no-op for depth (already in [0,1]).
		float c0[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		overrideIm2dPixelShader = ClampShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Unbind INTZ from s0 BEFORE the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO/DrawHeightFog); the cleanup below stays as-is.
		dev->SetTexture(0, NULL);
		DepthHook_Restore();
		ok = true;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dev->SetTexture(0, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // keep Suspend/Restore balanced on fault
	}

	dev->SetTexture(0, NULL);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();

	g_prevDepthValid = ok;
	if(!ok && !s_prevDepthLogDone){
		s_prevDepthLogDone = true;
		dbglog("[PostFX] CopyDepthToPrev: depth copy failed, prev-depth history disabled (one-shot)");
	}
}

// ============================================================
// PostFX_CopyDepthToTexture — copy the live scene depth (INTZ depth-stencil,
// currently bound as the device DS) into dstTex (an R32F render target) as
// raw window-space z. Used by the water pipe at water-draw time for
// depth-based shore blending / translucency / shallow->deep colour.
//
// Unlike CopyDepthToPrev (postfx tail, Z irrelevant) this runs MID-SCENE,
// so the exact incoming depth-stencil surface is re-bound at the end instead
// of DepthHook_Restore's cached game DS — the water draw that follows must
// keep Z-testing against the live INTZ contents.
//
// Returns false (and touches nothing) when the copy cannot run: no INTZ,
// INTZ not the active DS (its contents would be stale), or a draw fault —
// callers fall back to depth-less shading.
// ============================================================
bool PostFX_CopyDepthToTexture(IDirect3DTexture9 *dstTex, int dstW, int dstH)
{
	if(!dstTex || dstW < 1 || dstH < 1) return false;
	if(!g_intzTex || !g_intzSurf) return false;
	if(!ClampShader) return false;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return false;

	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	// Only trust INTZ contents when INTZ is the surface receiving depth writes.
	if(oldDS != g_intzSurf){
		if(oldRT) oldRT->Release();
		if(oldDS) oldDS->Release();
		return false;
	}

	IDirect3DSurface9 *dstSurf = NULL;
	if(FAILED(dstTex->GetSurfaceLevel(0, &dstSurf)) || !dstSurf){
		if(oldRT) oldRT->Release();
		if(oldDS) oldDS->Release();
		return false;
	}

	bool ok = false;

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

	dev->SetRenderTarget(0, dstSurf);
	// Unbind the DS so INTZ can be sampled (hook_SetDS passes NULL straight
	// through to the original — no substitution on the unbind path).
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 vp = { 0, 0, (DWORD)dstW, (DWORD)dstH, 0.0f, 1.0f };
	dev->SetViewport(&vp);

	__try {
		dev->SetTexture(0, g_intzTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);

		// Fullscreen quad in the dst RT's own pixel grid (colourfilterVerts
		// would only be valid once the colourfilter has run this frame).
		RwIm2DVertex quad[4];
		for(int i = 0; i < 4; i++){
			quad[i].z = 0.0f;
			quad[i].rhw = 1.0f;
			quad[i].emissiveColor = 0xFFFFFFFF;
		}
		quad[0].x = 0.0f;         quad[0].y = 0.0f;         quad[0].u = 0.0f; quad[0].v = 0.0f;
		quad[1].x = (float)dstW;  quad[1].y = 0.0f;         quad[1].u = 1.0f; quad[1].v = 0.0f;
		quad[2].x = 0.0f;         quad[2].y = (float)dstH;  quad[2].u = 0.0f; quad[2].v = 1.0f;
		quad[3].x = (float)dstW;  quad[3].y = (float)dstH;  quad[3].u = 1.0f; quad[3].v = 1.0f;
		RwImVertexIndex quadIdx[6] = { 0, 1, 2, 2, 1, 3 };

		// ClampShader is the generic s0→RT copy shader; c0={min,max,tonemap,0}
		// with a [0,1] clamp is a no-op for raw depth (already in [0,1]).
		float c0[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		overrideIm2dPixelShader = ClampShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, quadIdx, 6);
		overrideIm2dPixelShader = nil;

		ok = true;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] CopyDepthToTexture fault code=0x%08X", GetExceptionCode());
		ok = false;
	}

	dev->SetTexture(0, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	// Re-bind the EXACT incoming DS (== g_intzSurf -> hook_SetDS passthrough)
	// so the following water draw Z-tests against live scene depth.
	dev->SetDepthStencilSurface(oldDS);
	dstSurf->Release();
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
	return ok;
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

// Throttled AO-output validation: read back a sparse grid from the occlusion
// RT and fail-open (skip the multiply composite) if it is near-black — under
// SRCBLEND=ZERO/DESTBLEND=SRCCOLOR a black AO texture *replaces* the whole
// scene with black. Reuses the DumpRasterToBMP GetRenderTargetData pattern,
// but runs regardless of postfxDumpDebug because it is a safety gate, not a
// debug dump. First 3 frames always validate (catch immediate failures),
// then once every 60 frames (~2s) to bound the GetRenderTargetData stall.
// Returns true = composite may proceed.
static bool ValidateSSAOOutput(IDirect3DSurface9 *aoRT)
{
	static int s_validateCount = 0;
	s_validateCount++;
	if(s_validateCount > 3 && (s_validateCount % 60) != 0)
		return true;
	if(!aoRT)
		return true; // no surface captured — can't verify, don't block
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return true;

	D3DSURFACE_DESC desc;
	if(FAILED(aoRT->GetDesc(&desc)) || desc.Width < 1 || desc.Height < 1)
		return true;
	IDirect3DSurface9 *sysSurf = NULL;
	if(FAILED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height,
			D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, NULL)) || !sysSurf)
		return true;
	if(FAILED(dev->GetRenderTargetData(aoRT, sysSurf))){
		sysSurf->Release();
		return true; // readback failed — don't skip the composite on a glitch
	}
	D3DLOCKED_RECT lr;
	if(FAILED(sysSurf->LockRect(&lr, NULL, D3DLOCK_READONLY))){
		sysSurf->Release();
		return true;
	}
	// Sparse 16x16 grid of the red channel (AO output is greyscale)
	float amin = 1.0f, amax = 0.0f, asum = 0.0f;
	int count = 0;
	for(int gy = 0; gy < 16; gy++){
		int y = (int)((size_t)gy * (desc.Height - 1) / 15);
		const BYTE *row = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
		for(int gx = 0; gx < 16; gx++){
			int x = (int)((size_t)gx * (desc.Width - 1) / 15);
			float a = row[x * 4 + 2] / 255.0f; // A8R8G8B8 -> R
			if(a < amin) amin = a;
			if(a > amax) amax = a;
			asum += a;
			count++;
		}
	}
	sysSurf->UnlockRect();
	sysSurf->Release();

	// Healthy AO: mostly white (avg ~0.7+). avg < 0.02 = effectively all
	// black -> the multiply would black out the frame -> fail open.
	float aavg = asum / (float)count;
	bool ok = (aavg >= 0.02f);
	if(dbglog_throttle("ssao_aoval"))
		dbglog("[PostFX] SSAO ao validate: min=%.3f max=%.3f avg=%.3f -> %s",
			amin, amax, aavg, ok ? "composite" : "SKIP (near-black, fail-open)");
	return ok;
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

	// Declared before __try so the __except path can restore them too
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	bool imStored = false; // set after ImmediateModeRenderStatesStore

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

		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		// Suspend depth hook so g_ssaoDepthTex can be sampled while not bound as DS
		DepthHook_Suspend();

		ImmediateModeRenderStatesStore();
		imStored = true;
		ImmediateModeRenderStatesSet();

		// Render SSAO occlusion to output raster
		RwRaster *origRaster = camRas;
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoOutputRaster);
		RwCameraBeginUpdate(Scene.camera);

		// White-clear the occlusion target: if the pass is skipped or fails
		// mid-frame the composite multiplies by 1.0 (graceful no-op) instead
		// of stale/garbage data — SSAO must degrade to "off", never black.
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);

		// Capture the AO output surface while bound for the post-pass readback
		// validation (fail-open gate on the multiply composite below).
		IDirect3DSurface9 *aoRT = NULL;
		dev->GetRenderTarget(0, &aoRT);

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

		// RT-sized white quad: correct UV 0..1 on this raster, white vertex
		// colour so the fixed-function composite can't modulate to black.
		SetupFullscreenQuad((float)w, (float)h);

		overrideIm2dPixelShader = SSAO;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Restore original camera raster
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// Unbind SSAO samplers (s0-s2) so g_ssaoDepthTex doesn't leak past
		// DepthHook_Restore and stay bound while INTZ is re-bound as DS
		// (feedback lock risk), mirroring DrawSSAO_Overhaul/DrawHeightFog.
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// Photoshop-multiply contract: final = scene x aoTexture, white =
		// no occlusion. Fail-open: skip the composite entirely if the readback
		// says the AO buffer is near-black (a black texture under
		// ZERO/SRCCOLOR *replaces* the scene with black).
		bool aoOk = ValidateSSAOOutput(aoRT);
		if(dbglog_throttle("ssao_comp"))
			dbglog("[PostFX] DrawSSAO composite: raster=%p aoOk=%d",
				g_ssaoOutputRaster, (int)aoOk);
		if(aoRT){ aoRT->Release(); aoRT = NULL; }

		if(aoOk){
			// Set blend through the RW cache FIRST (VERTEXALPHAENABLE /
			// SRCBLEND / DESTBLEND): RwIm2D re-emits cached states at draw
			// time and would clobber raw-only D3D9 factors set before the
			// draw — the old raw-only sequence is how the multiply could
			// silently degrade to a plain replace (showing the AO texture
			// alone / black). Mirror the factors with raw calls immediately
			// before the draw as a second layer.
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);        // src factor dropped
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);   // dest x src = multiply
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

			// White RT-sized quad — NOT colorfilterVerts (emissiveColor=0 would
			// modulate the SSAO texture to black and zero the whole scene).
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);

			// Restore blend defaults through the RW cache too (keep it coherent
			// with the D3D9 state before ImmediateModeRenderStatesReStore)
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}else if(dbglog_throttle("ssao_skipped"))
			dbglog("[PostFX] DrawSSAO: multiply composite SKIPPED (AO buffer near-black — fail-open)");

		ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		dbglog("[PostFX] DrawSSAO completed successfully");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawSSAO CRASHED exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
		// ReStore BEFORE the raw restore (same order as the normal exit): a fault
		// between the composite's rw SRCBLEND=ZERO/DESTBLEND=SRCCOLOR sets and
		// their restore left the rw cache holding the multiply factors — the raw
		// restore alone cannot fix layer (1), and the next RwIm2D draw re-emits
		// the cached ZERO/SRCCOLOR over the world pass.
		if(imStored)
			ImmediateModeRenderStatesReStore();
		if(rawGeomSaved)
			RestoreRawGeomStates(rawGeom);
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

	// Declared before __try so the __except path can restore them too
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	bool imStored = false; // set after ImmediateModeRenderStatesStore

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

		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		// Suspend depth hook so g_ssaoDepthTex can be sampled (INTZ not bound as DS)
		DepthHook_Suspend();

		CPostEffects::ImmediateModeRenderStatesStore();
		imStored = true;
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
		// Wave-2: s5=prev-depth, s6=prev-normals (NULL-safe: D3D9 ignores NULL SetTexture)
		dev->SetTexture(5, g_prevDepthValid ? g_prevDepthTex : NULL);
		dev->SetTexture(6, g_normalHistoryValid ? g_prevNormalTex : NULL);

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
		// Wave-2: history samplers POINT/CLAMP (matching s0's CLAMP addressing, point filter for depth)
		dev->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(6, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(6, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(6, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(6, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

		// c0: {radius, power, noiseScale, temporalBlend}
		// temporalBlend is the HISTORY weight (SSAO_Temporal.hlsl lerps toward
		// historyOcclusion by this value), so it only applies when Temporal is
		// ON AND both SSAO history and prev-depth history are valid; otherwise
		// 0.0 = no history. This is the ONLY in-function temporal-accumulation
		// switch (the function never reads ssaoTemporalEnable elsewhere), and
		// the dispatch now routes blurPasses>0 here even with Temporal OFF —
		// gating on the flag keeps OFF = pure current-frame AO, no ghosting.
		float c0Temporal[4] = { radius, power, noiseScale,
			(config->ssaoTemporalEnable && g_ssaoHistoryValid && g_prevDepthValid) ? config->ssaoTemporalBlend : 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0Temporal, 1);
		// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
		float c1Temporal[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, c1Temporal, 1);
		// c2: projInfo
		RwD3D9SetPixelShaderConstant(2, projInfo, 1);
		// c4: Wave-2 history flags {prevDepthValid, depthBlend, prevNormalValid, 0}
		float c4History[4] = { (float)g_prevDepthValid, 0.05f, (float)g_normalHistoryValid, 0.0f };
		RwD3D9SetPixelShaderConstant(4, c4History, 1);

		// Quarter-res RT needs quarter-sized white quad (colorfilterVerts are a
		// 2048x2048 quad whose UVs would sample only a corner of the depth map).
		SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);

		overrideIm2dPixelShader = SSAO_Temporal;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
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

			SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);
			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
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

			SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);
			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
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

		// White-clear output so a failed upsample degrades to "no AO", never
		// black garbage through the multiply composite.
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);

		// Capture the AO output surface while bound for the post-pass readback
		// validation (fail-open gate on the multiply composite below).
		IDirect3DSurface9 *aoRT = NULL;
		dev->GetRenderTarget(0, &aoRT);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// s0: final V-pass result (quarter frame), s1: depth
		RwD3D9SetTexture(g_ssaoQuarterTexRW[g_ssaoFrameIndex], 0);
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

		// Full-res RT: white quad sized to the camera raster (same UV fix as
		// the temporal pass; composite below reuses it).
		SetupFullscreenQuad((float)w, (float)h);

		overrideIm2dPixelShader = SSAO_Upsample;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// Wave-2: unbind all Pass 1 samplers (s0-s6) so the depth texture and
		// history textures don't leak past DepthHook_Restore (feedback lock risk).
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
		dev->SetTexture(4, NULL);
		dev->SetTexture(5, NULL);
		dev->SetTexture(6, NULL);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// =====================================================================
		// Composite: multiply blend SSAO onto scene (Photoshop multiply:
		// final = scene x ao, white = no occlusion). Fail-open on a
		// near-black AO readback — see ValidateSSAOOutput.
		// =====================================================================
		bool aoOk = ValidateSSAOOutput(aoRT);
		if(dbglog_throttle("ssao_ov_comp"))
			dbglog("[PostFX] DrawSSAO_Overhaul composite: raster=%p aoOk=%d",
				g_ssaoOutputRaster, (int)aoOk);
		if(aoRT){ aoRT->Release(); aoRT = NULL; }

		if(aoOk){
			// Blend factors through the RW cache FIRST (RwIm2D re-emits cached
			// states at draw time and would clobber raw-only D3D9 factors),
			// then mirror them raw immediately before the draw — same contract
			// as DrawSSAO's composite.
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);        // src factor dropped
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);   // dest x src = multiply
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

			// White RT-sized quad — NOT colorfilterVerts (their emissiveColor is 0
			// under COLORFILTER_MODERN, which modulates SSAO to black).
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);

			// Restore blend defaults through the RW cache (keep it coherent
			// with the D3D9 state before ImmediateModeRenderStatesReStore)
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}else if(dbglog_throttle("ssao_ov_skip"))
			dbglog("[PostFX] DrawSSAO_Overhaul: multiply composite SKIPPED (AO buffer near-black — fail-open)");

		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}

		// Swap frame index and mark history valid
		g_ssaoFrameIndex = otherIdx;
		g_ssaoHistoryValid = true;

		if(dbglog_throttle("ssao_ov_done"))
			dbglog("[PostFX] DrawSSAO_Overhaul completed successfully");
		// Debug dump: after SSAO composite
		DumpCurrentRT("after_ssao");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawSSAO_Overhaul CRASHED exception=0x%08X", GetExceptionCode());
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
		// Same three-layer contract gap as DrawSSAO: the rw cache can be left
		// holding the multiply-composite factors (SRCBLEND=ZERO/DESTBLEND=
		// SRCCOLOR) if the fault lands between the composite and its restore —
		// ReStore layer (1) first, then the raw snapshot.
		if(imStored)
			CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved)
			RestoreRawGeomStates(rawGeom);
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

	// Size from the trusted screen-size cache: a live Scene.camera read here
	// can hit a transient non-screen raster (measured 2048x1024 during PBR
	// geometry => this texture was allocated 512x256 instead of 480x270, see
	// GetScreenSize). Fall back to the live read only before the first
	// postfx capture of the session.
	int scrW = 0, scrH = 0;
	if(!GetScreenSize(&scrW, &scrH)){
		RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(!camRas) return NULL;
		scrW = camRas->width;
		scrH = camRas->height;
	}
	int w = scrW / 4;
	int h = scrH / 4;
	if(w < 16) w = 16;
	if(h < 16) h = 16;

	// Self-heal: a texture allocated inside a transient window keeps the wrong
	// aspect for the whole session, and RenderIBLBuffer derives its viewport
	// from the (now correct) screen size each frame — viewport/texture would
	// never match. Recreate instead of silently reusing the mismatched one.
	if(g_iblTex){
		D3DSURFACE_DESC desc = {};
		bool sizeOk = SUCCEEDED(g_iblTex->GetLevelDesc(0, &desc)) &&
		              (int)desc.Width == w && (int)desc.Height == h;
		if(sizeOk)
			return g_iblTex;
		dbglog("[PostFX] GetIBLTexture: mis-sized %ux%u -> %dx%d (transient camRas read at creation)",
			desc.Width, desc.Height, w, h);
		if(g_iblSurf){ g_iblSurf->Release(); g_iblSurf = NULL; }
		g_iblTex->Release();
		g_iblTex = NULL;
	}

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

	// Defense-in-depth: skip if camera isn't ready (frame 1 before BeginUpdate).
	// RwCameraGetRaster(NULL) would crash the D3D9 driver, and Im2D dispatch
	// needs a live camera context. The call-site gate in buildingPipe.cpp
	// catches this earlier; this guard prevents crash if entry is reached anyway.
	if(!Scene.camera){
		if(!iblLogged){ dbglog("RenderIBL: Scene.camera NULL (first-frame defer)"); iblLogged=1; }
		return;
	}

	// Lane G gate: Scene.camera exists on frame 1 but the exe-side camera global
	// at 0xC9BCC0 is still NULL before the first RwCameraBeginUpdate. Existence
	// != begun. RwIm2DRenderIndexedPrimitive dereferences it → null deref fault.
	if(*(void**)0xC9BCC0 == NULL){
		if(dbglog_throttle("ibl_nobegun"))
			dbglog("RenderIBL: skip, camera not begun");
		return;
	}

	// Get screen size BEFORE switching RT so we can bail without leaking refs
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){ if(!iblLogged){ dbglog("RenderIBL: camRas NULL"); iblLogged=1; } return; }

	IDirect3DTexture9 *tex = GetIBLTexture();
	if(!tex || !g_iblSurf){ if(!iblLogged){ dbglog("RenderIBL: no tex/surf"); iblLogged=1; } return; }
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!iblLogged){ dbglog("RenderIBL: OK tex=%p surf=%p", tex, g_iblSurf); iblLogged=1; }

	// Viewport size MUST come from the same source GetIBLTexture sized the
	// texture with (trusted screen cache, live read only as pre-capture
	// fallback) — otherwise a texture allocated inside a transient camRas
	// window is rendered into with a viewport that never matches it.
	int scrW = camRas->width, scrH = camRas->height;
	GetScreenSize(&scrW, &scrH);
	int rtW = scrW / 4;
	int rtH = scrH / 4;
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

	float screenP[4] = { (float)scrW, (float)scrH, 1.0f/max((float)scrW, 1e-7f), 1.0f/max((float)scrH, 1e-7f) };
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
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
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
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	// Restore old render target and viewport
	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
}

static IDirect3DTexture9* GetNormalBufferTexture(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return NULL;
	if(!Scene.camera) return NULL;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return NULL;
	int w = camRas->width / 2;
	int h = camRas->height / 2;
	if(w < 1 || h < 1) return NULL;

	// Res-aware: recreate if the camera resolution changed since creation
	if(g_normalBufferTex && (w != s_normalBufW || h != s_normalBufH)){
		if(g_normalBufferSurf){ g_normalBufferSurf->Release(); g_normalBufferSurf = NULL; }
		g_normalBufferTex->Release();
		g_normalBufferTex = NULL;
	}
	// Wave-2: prev-normal history shares the main dims — recreate + invalidate on change
	if(g_prevNormalTex && (w != s_normalBufW || h != s_normalBufH)){
		if(g_prevNormalSurf){ g_prevNormalSurf->Release(); g_prevNormalSurf = NULL; }
		g_prevNormalTex->Release();
		g_prevNormalTex = NULL;
		g_normalHistoryValid = false;
	}

	if(g_normalBufferTex) return g_normalBufferTex;
	if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_normalBufferTex, NULL)))
		return NULL;
	if(FAILED(g_normalBufferTex->GetSurfaceLevel(0, &g_normalBufferSurf))){
		g_normalBufferTex->Release();
		g_normalBufferTex = NULL;
		return NULL;
	}
	s_normalBufW = w;
	s_normalBufH = h;

	// A D3DPOOL_DEFAULT render target starts as driver garbage: sampling it
	// feeds random normals into main_building's screen-normal blend -> GGX
	// blowout ("white ground patches", see the s4 gate note in
	// buildingPipe.cpp). Clear to the invalid-normal marker: RGB 0x808080
	// decodes to (0,0,0) which fails every normal-length validity gate
	// (main_building lerp-out, SSAO's unit-length window -> depth-derivative
	// fallback), alpha 0 = "no depth" (NormalBuffer.hlsl writes depth/far).
	// ColorFill targets THIS surface directly — dev->Clear would hit whatever
	// RT happens to be bound.
	if(g_normalBufferSurf)
		dev->ColorFill(g_normalBufferSurf, NULL, D3DCOLOR_ARGB(0, 0x80, 0x80, 0x80));
	g_normalBufferHasContent = false;

	// Wave-2: lazy-create prev-normal history texture (half-res, same dims/format as main)
	if(!g_prevNormalTex){
		if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
			D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_prevNormalTex, NULL))){
			dbglog("[PostFX] GetNormalBufferTexture: CreateTexture prevNormal failed");
			g_normalHistoryValid = false;
		}else if(FAILED(g_prevNormalTex->GetSurfaceLevel(0, &g_prevNormalSurf))){
			g_prevNormalTex->Release();
			g_prevNormalTex = NULL;
			g_normalHistoryValid = false;
		}
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

// Fail-open classify pack for PipeChain pass 3: a 1x1 A8R8G8B8 texture
// cleared to 0 (pack.y/z = 0 => the pass3 gate skips reflection entirely).
// Used when the geometry-classify RT was never created this frame (zero
// classify draws) — sampling an unbound s4 yields undefined values in
// SM3.0 and garbage gloss/spec passed the >0.001 gate as ink-blot
// reflections. pack 0 = "no reflection by design", same as a real clear.
static IDirect3DTexture9*
GetPipeChainNoPackTex(void)
{
	static IDirect3DTexture9 *noPackTex = NULL;
	static bool noPackLogDone = false;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return NULL;
	if(!noPackTex){
		if(FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
			D3DPOOL_MANAGED, &noPackTex, NULL))){
			noPackTex = NULL;
			return NULL;
		}
		D3DLOCKED_RECT lr;
		if(SUCCEEDED(noPackTex->LockRect(0, &lr, NULL, 0))){
			*(DWORD*)lr.pBits = 0x00000000; // pack = 0
			noPackTex->UnlockRect(0);
		}
	}
	if(!noPackLogDone){
		dbglog("[PipeChain] no-pack dummy created %p", noPackTex);
		noPackLogDone = true;
	}
	return noPackTex;
}

void DrawNormalBufferToTexture(void)
{
	// Invalidate first: any early-out below means this frame did NOT refresh
	// the buffer, so consumers must stop binding it (stale/garbage normals ->
	// GGX blowout). Re-set to true only after a completed write.
	g_normalBufferHasContent = false;
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
	// Suspend depth hook so the INTZ depth can be sampled while not bound as DS
	// (mirrors DrawSSAO) — otherwise the depth texture is feedback-locked.
	// Save raw geom states BEFORE Suspend (it raw-writes ZENABLE).
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Suspend + Store below
	__try {
		DepthHook_Suspend();
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

		// RT-sized white quad with UV 0..1 — NOT colorfilterVerts. The game's
		// quad is a hardcoded 2048x2048 (positions 0..2048, UV 0..1); this
		// pass rasterizes into the HALF-RES viewport (w/2 x h/2 = 960x540),
		// which clips it at pixel 960x540 -> the shader only ever saw depth
		// UVs 0..0.469 x 0..0.264. The whole normal buffer therefore held the
		// screen's top-left ~47%x26% region and every consumer reading it at
		// full-screen UV (SSAO's hemisphere orientation, PipeChain edge
		// detection, the pipes' s4 normal blend) got normals from the wrong
		// place — garbled AO that read as "inverted" (dark open areas, bright
		// edges). UV is resolution-independent, so a quad spanning the RT with
		// UV 0..1 maps depth 1:1 onto the half-res buffer.
		SetupFullscreenQuad((float)nbW, (float)nbH);
		overrideIm2dPixelShader = NormalBufferShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Unbind INTZ from s0 BEFORE the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO/DrawHeightFog); the cleanup below stays as-is.
		dev->SetTexture(0, NULL);
		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dev->SetTexture(0, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // keep Suspend/Restore balanced on fault
	}

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	// Cleanup
	dev->SetTexture(0, NULL);

	// Restore old RT and viewport
	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();

	// Full write completed — the buffer now holds one valid frame of normals
	// (consumed by next frame's geometry draws, see the s4 bind gate in
	// buildingPipe.cpp).
	g_normalBufferHasContent = true;

	// Wave-2: copy current normals to previous-frame history (half-res)
	if(g_normalBufferTex && g_prevNormalTex && g_prevNormalSurf){
		IDirect3DSurface9 *srcSurf = NULL;
		if(SUCCEEDED(g_normalBufferTex->GetSurfaceLevel(0, &srcSurf)) && srcSurf){
			g_normalHistoryValid = SUCCEEDED(dev->StretchRect(srcSurf, NULL, g_prevNormalSurf, NULL, D3DTEXF_NONE));
			srcSurf->Release();
		}else{
			g_normalHistoryValid = false;
		}
	}else{
		g_normalHistoryValid = false;
	}
}

// ============================================================
// PipeChain classify buffer — full-res RGBA8 pack emitted by the PBR
// geometry pipes (building/vehicle) and consumed by PipeChain pass 3
// for the BRDF-weighted screen-space reflection.
// Pack layout (0..1 floats stored to 8-bit):
//   R = surfaceType/255 (reserved), G = glossiness,
//   B = specular/F0,     A = metallicness
// Cleared to 0 at creation and after consumption each frame => pixels
// never drawn (sky/background) keep weight 0.
// ============================================================
static IDirect3DTexture9 *g_pipeChainClassifyTex = NULL;
static IDirect3DSurface9 *g_pipeChainClassifySurf = NULL;
static int s_classifyW = 0, s_classifyH = 0;
static IDirect3DSurface9 *s_classifyOldRT = NULL;
static void *s_classifyVtxAlpha = NULL;
static void *s_classifyFog = NULL;
static void *s_classifyZWrite = NULL;
static int s_classifyBegins = 0; // PipeChain_ClassifyBegin successes this frame (grab+reset at DrawPipeChain entry)

// CRITICAL-1: Reset release for the classify RT — called from
// ReleaseDefaultPoolResources (~:3130), which appears BEFORE these statics
// in the file (hence the forward declaration there). SafeRelease pattern
// matches the g_pipeChainTexA/B neighbors; s_classifyOldRT/VtxAlpha/Fog/
// ZWrite are transient per-draw grabs (always released by ClassifyEnd
// within the same draw), so only tex+surf+size need the Reset treatment.
static void ReleasePipeChainClassifyResources(void)
{
	if(g_pipeChainClassifySurf){ g_pipeChainClassifySurf->Release(); g_pipeChainClassifySurf = NULL; }
	if(g_pipeChainClassifyTex){ g_pipeChainClassifyTex->Release(); g_pipeChainClassifyTex = NULL; }
	s_classifyW = 0;
	s_classifyH = 0;
}

static IDirect3DSurface9 *
GetPipeChainClassifySurf(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !Scene.camera)
		return NULL;
	// Pack size comes from the trusted screen-size cache (GetScreenSize) —
	// this runs from the GEOMETRY pipes, where Scene.camera's raster can be a
	// transient non-screen raster (measured: live read alternated
	// 2048x1024 / 1920x1080 twice per frame, so this RT was destroyed and
	// recreated — and CLEARED — twice per frame for the whole PBR window).
	// DrawPipeChain, the consumer of this pack (PipeChain pass 3 samples it
	// with screen UVs), refreshes the cache at postfx time where the main
	// camera is verified on-screen, so pack size always matches the screen
	// the consumer draws with. Live read only as pre-capture fallback.
	int w = 0, h = 0;
	if(!GetScreenSize(&w, &h)){
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(!camRas)
			return NULL;
		w = camRas->width;
		h = camRas->height;
	}
	if(w < 1 || h < 1)
		return NULL;
	// Res-aware: recreate on camera resolution change
	if(g_pipeChainClassifyTex && (w != s_classifyW || h != s_classifyH)){
		if(g_pipeChainClassifySurf){ g_pipeChainClassifySurf->Release(); g_pipeChainClassifySurf = NULL; }
		g_pipeChainClassifyTex->Release();
		g_pipeChainClassifyTex = NULL;
	}
	if(!g_pipeChainClassifyTex){
		if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
			D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_pipeChainClassifyTex, NULL)))
			return NULL;
		if(FAILED(g_pipeChainClassifyTex->GetSurfaceLevel(0, &g_pipeChainClassifySurf))){
			g_pipeChainClassifyTex->Release();
			g_pipeChainClassifyTex = NULL;
			return NULL;
		}
		s_classifyW = w;
		s_classifyH = h;
		// start from a clean pack (0 = no reflection)
		IDirect3DSurface9 *prevRT = NULL;
		dev->GetRenderTarget(0, &prevRT);
		dev->SetRenderTarget(0, g_pipeChainClassifySurf);
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0.0f);
		dev->SetRenderTarget(0, prevRT);
		if(prevRT) prevRT->Release();
		dbglog("[PipeChain] classify RT created %dx%d", w, h);
	}
	return g_pipeChainClassifySurf;
}

// ---- Classify begin/end -------------------------------------------------
// Called by the PBR geometry pipes around a second draw of the SAME mesh
// with PipeChainShader forced on as the PS (c0.x = 9 pack mode). The depth
// stencil stays bound, so ZTEST against the just-written depth resolves
// occlusion (classify runs AFTER the main draw); ZWRITE stays off so the
// main depth buffer is untouched. Fail-open: any inactive gate = no-op.
bool PipeChain_ClassifyBegin(float surfaceType, float gloss, float spec, float metal)
{
	// HIGH-4: match the consumer dispatch gate (postfx ~:2624 pipeChainEnable
	// && normalBufferEnable) — without normalBufferEnable the classify pack
	// was written but DrawPipeChain never consumed it (wasted RT + second
	// draw per mesh).
	if(!config || !config->pipeChainEnable || !config->normalBufferEnable || !PipeChainShader)
		return false;
	if(gRenderingSpheremap || IsGameInMenuOrPaused())
		return false;
	IDirect3DSurface9 *classifySurf = GetPipeChainClassifySurf();
	if(!classifySurf || !d3d9device)
		return false;

	s_classifyOldRT = NULL;
	d3d9device->GetRenderTarget(0, &s_classifyOldRT);
	if(!s_classifyOldRT)
		return false;
	d3d9device->SetRenderTarget(0, classifySurf);

	// Pack must arrive unblended, unfogged, no depth write. RW cache + raw
	// mirrors (pipeEnterAlphaMode raw writes can desync the two layers).
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s_classifyVtxAlpha);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &s_classifyFog);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s_classifyZWrite);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

	// c0.x = 9 selects pack mode in PipeChain.hlsl; c1 = pack floats.
	float flagP[4] = { 9.0f, 0.0f, 0.0f, 0.0f };
	float cl = surfaceType < 0.0f ? 0.0f : (surfaceType > 255.0f ? 255.0f : surfaceType) / 255.0f;
	float gl = gloss < 0.0f ? 0.0f : (gloss > 1.0f ? 1.0f : gloss);
	float sp = spec  < 0.0f ? 0.0f : (spec  > 1.0f ? 1.0f : spec);
	float mt = metal < 0.0f ? 0.0f : (metal > 1.0f ? 1.0f : metal);
	float packP[4] = { cl, gl, sp, mt };
	RwD3D9SetPixelShaderConstant(0, flagP, 1);
	RwD3D9SetPixelShaderConstant(1, packP, 1);
	RwD3D9SetPixelShader(PipeChainShader);
	s_classifyBegins++; // diagnostic: proves classify landed this frame
	return true;
}

void PipeChain_ClassifyEnd(void)
{
	if(!s_classifyOldRT)
		return;
	// Restore RW cache first, then raw mirrors to match (covers the case
	// where the cache value didn't change but we forced the device).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, s_classifyVtxAlpha);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, s_classifyFog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, s_classifyZWrite);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s_classifyVtxAlpha == (void*)FALSE ? FALSE : TRUE);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, s_classifyFog == (void*)FALSE ? FALSE : TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s_classifyZWrite == (void*)FALSE ? FALSE : TRUE);
	d3d9device->SetRenderTarget(0, s_classifyOldRT);
	s_classifyOldRT->Release();
	s_classifyOldRT = NULL;
	// NOTE: caller re-sets its own pixel shader (Begin forced PipeChainShader).
}

void DrawPipeChain(void)
{
	// Grab-and-reset BEFORE any gate: geometry classify runs earlier in the
	// frame than this postfx entry, so this snapshot is "begins this frame".
	// (Resetting inside the throttled pc_ok log accumulated across many
	// frames — the 43503 vs 635 mismatch.)
	int classifyBegins = s_classifyBegins;
	s_classifyBegins = 0;

	if(!config->pipeChainEnable || !PipeChainShader){
		if(dbglog_throttle("pc_g1"))
			dbglog("[PipeChain] skip: enable=%d shader=%p",
				config ? config->pipeChainEnable : -1, PipeChainShader);
		return;
	}
	if(!config->normalBufferEnable || !g_normalBufferTex){
		if(dbglog_throttle("pc_g2"))
			dbglog("[PipeChain] skip: normalBufferEnable=%d tex=%p",
				config ? config->normalBufferEnable : -1, g_normalBufferTex);
		return;
	}
	if(!g_ssaoDepthTex){
		if(dbglog_throttle("pc_g3")) dbglog("[PipeChain] skip: no depth texture");
		return;
	}

	IDirect3DTexture9 *texA = GetPipeChainTexture(0);
	IDirect3DTexture9 *texB = GetPipeChainTexture(1);
	if(!texA || !texB || !g_pipeChainSurfA || !g_pipeChainSurfB){
		if(dbglog_throttle("pc_g4")) dbglog("[PipeChain] skip: ping-pong RTs unavailable");
		return;
	}
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){
		if(dbglog_throttle("pc_g5"))
			dbglog("[PipeChain] skip: camRas NULL");
		return;
	}
	// Second trusted capture point (postfx time, main camera on-screen) —
	// refreshes the cache the geometry-phase classify RT sizes itself from.
	CaptureScreenSize(camRas);
	// Pass 0-3 read pRasterFrontBuffer as scene source — without it the whole
	// chain (and env sampling) runs on a never-written padded raster.
	if(!CPostEffects::pRasterFrontBuffer){
		if(dbglog_throttle("pc_g6")) dbglog("[PipeChain] skip: pRasterFrontBuffer NULL");
		return;
	}

	// Sync the front buffer BEFORE the chain samples it: passes 0-2 read
	// pRasterFrontBuffer as the scene source, and a stale/never-written
	// front buffer feeds the whole chain (including the classify composite)
	// junk — the other front-buffer consumers (motion blur, height fog)
	// already re-sync at their entry points.
	CPostEffects::UpdateFrontBuffer();

	// Force-create the classify RT now (it was lazily created only inside
	// PipeChain_ClassifyBegin, so frames with zero classify draws reached
	// pass 3 with s4 unbound). Creation clears to pack 0 = no reflection.
	GetPipeChainClassifySurf();

	// Screen-space quad sized to the camera raster (UV 0..1). The old
	// colorfilterVerts are a hardcoded 2048² quad — their /2048 UVs misalign
	// every screen-sized buffer (depth/normal) below 2048px.
	SetupFullscreenQuad((float)camRas->width, (float)camRas->height);

	float screenP[4] = { (float)camRas->width, (float)camRas->height,
		1.0f/max((float)camRas->width, 1e-7f), 1.0f/max((float)camRas->height, 1e-7f) };
	// c3: front-buffer UV scale — s0 is the padded 2048² front buffer while
	// depth/normal/classify are screen-sized (raw UV).
	float fbParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	if(CPostEffects::pRasterFrontBuffer){
		fbParams[0] = (float)camRas->width  / max((float)CPostEffects::pRasterFrontBuffer->width,  1.0f);
		fbParams[1] = (float)camRas->height / max((float)CPostEffects::pRasterFrontBuffer->height, 1.0f);
	}
	// c4: real projection info for pass 3's view-space reflection
	// (the old c1 upload was junk {1,1,1,0} and nothing consumed it).
	RwCamera *cam = Scene.camera;
	float nf = cam->farPlane - cam->nearPlane;
	if(nf < 1e-7f) nf = 1e-7f;
	float proj4[4] = {
		cam->recipViewWindow.x, cam->recipViewWindow.y,
		-cam->nearPlane * cam->farPlane / nf,
		    cam->farPlane / nf
	};

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store and the DepthHook below
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
	if(!oldRT){
		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		if(oldDS) oldDS->Release();
		return;
	}

	__try {
		// Keep the depth hook suspended for the WHOLE chain — pass 3 samples
		// depth too (the old code restored before pass 3 and fed it junk).
		DepthHook_Suspend();

		RwD3D9SetPixelShaderConstant(2, screenP, 1);
		RwD3D9SetPixelShaderConstant(3, fbParams, 1);
		RwD3D9SetPixelShaderConstant(4, proj4, 1);

		// ---- Pass 0: Input -> texA ----
		{
			dev->SetRenderTarget(0, g_pipeChainSurfA);
			dev->SetDepthStencilSurface(NULL);

			float pipeP[4] = { 0.0f, 0.0f, config->pipeChainIntensity, 0.0f };
			RwD3D9SetPixelShaderConstant(0, pipeP, 1);

			dev->SetTexture(0, NULL);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

			// Normal buffer on stage 1
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// Depth on stage 2 (hook suspended above so INTZ can be sampled)
			dev->SetTexture(2, g_ssaoDepthTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			overrideIm2dPixelShader = PipeChainShader;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;
		}

		// ---- Pass 1: Mid-A -> texB ----
		{
			dev->SetRenderTarget(0, g_pipeChainSurfB);
			dev->SetDepthStencilSurface(NULL);

			float pipeP[4] = { 1.0f, 0.0f, config->pipeChainIntensity, 0.0f };
			RwD3D9SetPixelShaderConstant(0, pipeP, 1);

			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetTexture(2, g_ssaoDepthTex);
			dev->SetTexture(3, NULL);

			overrideIm2dPixelShader = PipeChainShader;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;
		}

		// ---- Pass 2: Mid-B -> texA (ping-pong) ----
		{
			dev->SetRenderTarget(0, g_pipeChainSurfA);
			dev->SetDepthStencilSurface(NULL);

			float pipeP[4] = { 2.0f, 0.0f, config->pipeChainIntensity, 0.0f };
			RwD3D9SetPixelShaderConstant(0, pipeP, 1);

			// Scene on stage 0
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetTexture(2, g_ssaoDepthTex);
			// Intermediate (texB) on stage 3
			dev->SetTexture(3, texB);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			overrideIm2dPixelShader = PipeChainShader;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;
		}

		// ---- Pass 3: Output -> back buffer (BRDF reflection composite) ----
		{
			// DS intentionally NOT re-bound: the depth hook is still suspended
			// and s2 samples INTZ (binding it as DS again would feedback-lock).
			// Z-test is off, so no depth buffer is needed for this draw.
			dev->SetRenderTarget(0, oldRT);

			float pipeP[4] = { 3.0f, 0.0f, config->pipeChainIntensity, 0.0f };
			RwD3D9SetPixelShaderConstant(0, pipeP, 1);

			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetTexture(2, g_ssaoDepthTex);
			// Intermediate (texA) on stage 3
			dev->SetTexture(3, texA);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			// Classify pack on stage 4 — POINT so gloss/spec don't bleed
			// across silhouettes. Fall back to a 1x1 pack=0 dummy when the
			// classify RT doesn't exist: an unbound s4 samples undefined
			// values in SM3.0 and garbage gloss/spec passes the gate below
			// as ink-blot reflections (pack 0 = no reflection, fail-open).
			IDirect3DTexture9 *packTex = g_pipeChainClassifyTex;
			if(!packTex){
				packTex = GetPipeChainNoPackTex();
				if(packTex){
					if(dbglog_throttle("pc_noclassify"))
						dbglog("[PipeChain] pass3: classify RT NULL, using pack0 dummy %p", packTex);
					dev->SetTexture(4, packTex);
					dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
					dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
					dev->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
					dev->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_POINT);
				}else{
					dev->SetTexture(4, NULL); // last resort (dummy creation failed)
				}
			}else{
				dev->SetTexture(4, packTex);
				dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
				dev->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			}

			overrideIm2dPixelShader = PipeChainShader;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;
		}

		// Unbind INTZ from s2 before the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO_Overhaul/DrawHeightFog); the stage 1-4
		// cleanup below stays as the belt-and-braces unbind.
		dev->SetTexture(2, NULL);

		// Re-bind INTZ as DS before anything else touches sampler/RT state
		DepthHook_Restore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		if(dbglog_throttle("pc_crash"))
			dbglog("[PipeChain] crashed exception=0x%08X", GetExceptionCode());
		dev->SetTexture(2, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // keep Suspend/Restore balanced on fault
		dev->SetRenderTarget(0, oldRT); // fail-open: leave the frame as-is
	}

	// Depth: re-assert the pre-chain surface (idempotent when the depth hook
	// already restored it; covers the no-depthhook fallback path where
	// pass 1's SetDepthStencilSurface(NULL) unbound the game's real DS).
	dev->SetDepthStencilSurface(oldDS);

	// Wipe the classify buffer for the NEXT frame (packs are re-emitted by
	// geometry every frame; stale packs would otherwise leak).
	if(g_pipeChainClassifySurf){
		dev->SetRenderTarget(0, g_pipeChainClassifySurf);
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0.0f);
		dev->SetRenderTarget(0, oldRT);
	}

	// Cleanup
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
	dev->SetTexture(3, NULL);
	dev->SetTexture(4, NULL);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();

	if(dbglog_throttle("pc_ok")){
		dbglog("[PipeChain] ran: intensity=%.2f classify=%p fb=(%.4f,%.4f) classifyBegins=%d",
			config->pipeChainIntensity, g_pipeChainClassifySurf,
			fbParams[0], fbParams[1], classifyBegins); // grab+reset at entry: this-frame count
	}

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

	// Crash fix: save the exe's global camera pointer (0xC9BCC0) before we
	// call RwCameraBeginUpdate on our scratch camera. Each EndUpdate clears it
	// to NULL, and game code at 0x7F98DF dereferences [0xC9BCC0]+0x60 → crash.
	// Restore it after all BeginUpdate/EndUpdate pairs are done.
	void* savedCam = *(void**)0xC9BCC0;

	// ---- SMAA rasters ----
	if(s_smaaPendingInit && g_smaaEdgeRaster && g_smaaBlendRaster && g_smaaPrevFrameRaster && g_smaaSceneRaster){
		RwRaster *initRas[] = { g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster, g_smaaSceneRaster };
		bool allOk = true;
		for(int i = 0; i < 4; i++){
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

	// Restore the game's camera pointer so 0x7F98DF doesn't read NULL+0x60.
	*(void**)0xC9BCC0 = savedCam;

	// Crash fix (2026-09-14 #3): the scratch camera leaves the last init
	// raster bound as the D3D9 render target, corrupting all downstream
	// rendering (READ crash at 0x7FBD4A +0x60). Detach it and restore the
	// backbuffer so the main frame renders to the correct target.
	RwCameraSetRaster(s_smaaInitCam, NULL);
	if(d3d9device){
		IDirect3DSurface9 *bb = NULL;
		if(SUCCEEDED(d3d9device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))){
			d3d9device->SetRenderTarget(0, bb);
			bb->Release();
		}
		dbglog("[RasterInit] scratch camera detached, backbuffer restored");
	}
}

// SMAA edge-detect pass — extracted so the draw goes through the guardedIm2DRender
// choke-point (DrawSMAA itself can't take __try: error C2712) and so depth-hook
// suspend/restore brackets the draw.
// Returns true on success, false if the pass faulted (caller skips remaining passes).
static bool DrawSMAA_EdgeDetect(float smaaThreshold, float cameraMovement, const float screenParams[4],
                                RwIm2DVertex *verts)
{
	extern IDirect3DTexture9 *g_ssaoDepthTex;
	bool depthSuspended = false;

	// Suspend depth hook so INTZ can be sampled on s2
	if(g_ssaoDepthTex){
		DepthHook_Suspend();
		d3d9device->SetTexture(2, g_ssaoDepthTex);
		depthSuspended = true;
	}

	// Bind velocity buffer on stage 3 for combined motion detection
	if(g_velocityTex)
		d3d9device->SetTexture(3, g_velocityTex);

	// Temporal always on — lower motion threshold for better stabilization
	float motionThresh = 0.5f;
	float edgeP[4] = {smaaThreshold, motionThresh, 2.0f, cameraMovement};
	RwD3D9SetPixelShaderConstant(0, edgeP, 1);
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);

	// SEH containment + g_inGuardedIm2DPass save/restore now live in
	// guardedIm2DRender (pipelinecommon.cpp) — POD-only body, C2712-safe,
	// single guarded path (no double SEH).
	bool ok = guardedIm2DRender(
		SMAA_EdgeMotionDepth ? SMAA_EdgeMotionDepth : SMAA_Edge,
		rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6,
		"smaa_edge");

	// Restore depth hook after edge pass if it was suspended (blend weight doesn't sample depth)
	if(depthSuspended){
		d3d9device->SetTexture(2, NULL); // unbind INTZ before Restore (mirrors DrawHeightFog)
		DepthHook_Restore();
	}

	return ok;
}

static bool SMAA_DrawPass(IDirect3DPixelShader9 *shader, RwIm2DVertex *verts)
{
	// SEH containment + g_inGuardedIm2DPass save/restore live in
	// guardedIm2DRender (POD-only, C2712-safe) — one guarded path, no
	// duplicate __try here.
	return guardedIm2DRender(shader, rwPRIMTYPETRILIST,
		verts, 4, colorfilterIndices, 6, "smaa_draw");
}

// Common fail-open bail for guarded DrawSMAA faults: put the frame back on
// the camera draw buffer (a fault mid-pass can leave an SMAA raster bound as
// RT0), restore viewport, clear the hoisted guard + phase tag, latch SMAA
// broken (fail-open until resolution change resets it), restore the game's
// immediate-mode render states.
// POD-only, no SEH here — safe to call from DrawSMAA (no __try in it).
static void smaaGuardBail(IDirect3DDevice9 *dev, RwRaster *drawBuffer, const D3DVIEWPORT9 *vp)
{
	// Restore the camera frame raster as RT0. Do NOT use RwD3D9SetRenderTarget
	// here: the game's frame raster is rwRASTERTYPECAMERA, which that entry
	// point dereferences as a CAMERATEXTURE (null parent -> 0xC0000005 at
	// 0x7F9ECB). End/BeginUpdate re-binds it through the camera driver path,
	// exactly like UpdateFrontBuffer.
	if(Scene.camera){
		if(drawBuffer && RwCameraGetRaster(Scene.camera) != drawBuffer)
			dbglog("[GUARD] smaaGuardBail: camera raster changed during SMAA");
		RwCameraEndUpdate(Scene.camera);
		RwCameraBeginUpdate(Scene.camera);
	}
	if(dev && vp)
		dev->SetViewport(vp);
	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	s_smaaBroken = true;
	// Edge-detect raw-binds s2=g_ssaoDepthTex / s3=g_velocityTex and pass1
	// binds s1/s2 (area/search) — mirror the success-path unbind so a fault
	// mid-pass doesn't leave an INTZ/velocity texture on a stage while
	// DepthHook still re-binds it as the depth-stencil (feedback lock).
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
	}
	CPostEffects::ImmediateModeRenderStatesReStore();
	smaaRestoreRawGeom();
}

// SEH-guarded RwTextureCreate for SMAA RT-backed textures — wrapping a
// CAMERATEXTURE raster without a live D3D9 surface can fault inside the
// driver's texture-create path. POD-only body (C2712-safe).
// *faulted distinguishes a caught SEH fault (caller must bail) from a plain
// NULL return (caller just skips the bind, as before).
static RwTexture *smaaTexCreate(RwRaster *ras, const char *phase, bool *faulted)
{
	*faulted = false;
	if(!ras)
		return NULL;
	const char *ph = phase ? phase : "?";
	RwTexture *tex = NULL;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		tex = RwTextureCreate(ras);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[GUARD] smaaTexCreate: FAULT phase=%s code=0x%08X raster=%p",
			ph, GetExceptionCode(), ras);
		tex = NULL;
		*faulted = true;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return tex;
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

	// Guard: skip first 2 frames to let the game's entity pool initialize.
	// SMAA Pass2 calls RwIm2DRenderIndexedPrimitive which triggers the game's
	// text/entity rendering pipeline. On frame 1-2, the entity pool at [0xB4E9E0]
	// has slots with NULL data pointers, causing a crash at 0x7F9ECB.
	static int smaaFrameCount = 0;
	smaaFrameCount++;
	if(smaaFrameCount <= 2){
		dbglog("[SMAA] SKIP: frame %d, letting game init entity pool", smaaFrameCount);
		return;
	}

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
		if(g_smaaSceneRaster){ UntrackRaster(g_smaaSceneRaster); RwRasterDestroy(g_smaaSceneRaster); g_smaaSceneRaster = NULL; }
		g_smaaRtWidth = w; g_smaaRtHeight = h;
		if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
		if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
		s_smaaRastersInitialized = false;
		s_smaaPendingInit = false;
		s_smaaBroken = false;
		g_smaaHistoryValid = false;
		s_smaaHistAge = 0;
	}

	// Check for permanent broken state (init failed previously).
	// Resolution-change reset above clears this, allowing retry on alt-tab.
	if(s_smaaBroken){
		if(dbglog_throttle("smaa_broken"))
			dbglog("[SMAA] SKIP: permanently broken (init failed), will not retry until resolution change");
		return;
	}

	// Create RW camera texture rasters at CAMERA size (w x h = camRas dims).
	// SMAA sampling doctrine (see g_smaaSceneRaster comment): all SMAA
	// rasters are camera-sized with content filling UV 0..1, drawn with
	// s_ffQuad (UV 0..1, positions 0..w x 0..h). The scene input is a
	// camera-sized copy of the front buffer's valid sub-rect (the FB itself
	// is padded 2048x2048 and only correct under colorfilterVerts' 0..2048
	// positions — under a UV 0..1 quad it would sample stretched).
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
	if(!g_smaaSceneRaster){
		g_smaaSceneRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaSceneRaster){ dbglog("[SMAA-DIAG] FATAL: sceneRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created sceneRaster=%p %dx%d (cam size)", g_smaaSceneRaster, w, h);
	}

	// Self-check (bug 5): every SMAA raster must be camera-sized. A stale
	// raster — resolution change that destroyed/recreated only some of them,
	// a device Reset that dropped one, or a partial recreate after alt-tab —
	// makes UV 0..1 address the wrong texel grid: edge/blend/history then
	// sample a sub-rect (or a stretched copy) and the artifact shows as
	// residual edging/ghosting. Fail CLOSED: latch s_smaaBroken so SMAA
	// self-skips until the next resolution change instead of drawing a
	// wrong image.
	{
		struct { RwRaster *r; const char *n; } chk[4] = {
			{ g_smaaEdgeRaster,    "edge"    },
			{ g_smaaBlendRaster,   "blend"   },
			{ g_smaaPrevFrameRaster,"prevFrame" },
			{ g_smaaSceneRaster,   "scene"   },
		};
		for(int i = 0; i < 4; i++){
			RwRaster *r = chk[i].r;
			if(r && (r->width != w || r->height != h)){
				dbglog("[SMAA] SELF-SKIP: %s raster %dx%d != camera %dx%d (stale, latching broken until resolution change)",
					chk[i].n, r->width, r->height, w, h);
				s_smaaBroken = true;
				return;
			}
		}
	}

	// Deferred force-init: set the pending flag so SMAATryInitRasters() (called
	// from RenderScene_before, outside the main camera's BeginUpdate) will
	// create the D3D9 surfaces via the scratch camera. Cannot do it inline
	// because RW 3.6's D3D9 driver crashes when creating render-target
	// textures while the device is in an active render state (mid-frame).
	// Bail out this frame — the SMAA pass will run next frame after init.
	if(!s_smaaRastersInitialized && g_smaaEdgeRaster && g_smaaBlendRaster &&
	   g_smaaPrevFrameRaster && g_smaaSceneRaster){
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
	   !RasterEnsureSurfaceReady(g_smaaPrevFrameRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaSceneRaster, "SMAA")){
		if(dbglog_throttle("smaa_nosurf"))
			dbglog("[SMAA] SKIP: rasters not surface-ready, will retry next frame");
		return;
	}

	// Refresh the camera-sized scene copy from the front buffer's VALID
	// sub-rect. UpdateFrontBuffer just synced camRas -> FB (DrawFinalEffects
	// :3066), so the camera draw buffer holds the identical final graded
	// frame at 1:1 — copy from it (same-size blit, no padded-region math).
	// Same EndUpdate/PushContext/RenderFast/Pop/BeginUpdate idiom as
	// DrawIVGrade's frame copy. Fail-open: skip SMAA this frame on failure.
	{
		RwRaster *sceneSrc = RwCameraGetRaster(Scene.camera);
		if(!sceneSrc){
			if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: scene copy src NULL");
			return;
		}
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(g_smaaSceneRaster);
		RwRaster *copyResult = RwRasterRenderFast(sceneSrc, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		if(!copyResult){
			dbglog("[SMAA] scene copy failed (RenderFast NULL), skipping SMAA this frame");
			return;
		}
	}

	// UV 0..1 fullscreen quad on the camera texel grid — ALL SMAA draws use
	// this instead of colorfilterVerts (hardcoded 2048x2048 quad whose
	// visible window spans only UV 0..0.9375 x 0..0.5273, correct for the
	// padded FB but wrong for every camera-sized SMAA raster).
	SetupFullscreenQuad((float)w, (float)h);

	// Save/restore D3D9 state around all SMAA passes + texture creation.
	// Must be BEFORE area/search tex generation — those leave D3D9 state dirty,
	// and we need Restore to capture the original game state, not the post-creation state.
	// Raw geom states first: the rw-only Store/Set below cannot capture (or
	// later undo) the raw D3DRS writes at the pass0 block (ALPHATEST/CULL/
	// ZENABLE/ZWRITE off) — every other postfx pass pairs them like this.
	s_smaaRawGeomSaved = SaveRawGeomStates(s_smaaRawGeom);
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

		// Normalize both forward vectors so the dot is a true cosine (the
		// rotation/acosf metric). Guard the reciprocal length against zero with the
		// project pattern max(x, 1e-7f): a degenerate 'at' vector would divide to
		// NaN, which SM3.0 renders as black. CPU-side math, keeps ps_3_0 legal.
		float curLen = sqrtf(camMatrix->at.x * camMatrix->at.x +
		                     camMatrix->at.y * camMatrix->at.y +
		                     camMatrix->at.z * camMatrix->at.z);
		float prvLen = sqrtf(prevCamMatrix.at.x * prevCamMatrix.at.x +
		                     prevCamMatrix.at.y * prevCamMatrix.at.y +
		                     prevCamMatrix.at.z * prevCamMatrix.at.z);
		float invCur = 1.0f / max(curLen, 1e-7f);
		float invPrv = 1.0f / max(prvLen, 1e-7f);
		float dot = (camMatrix->at.x * invCur) * (prevCamMatrix.at.x * invPrv) +
		            (camMatrix->at.y * invCur) * (prevCamMatrix.at.y * invPrv) +
		            (camMatrix->at.z * invCur) * (prevCamMatrix.at.z * invPrv);
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

	// Hoisted guard: cover the Pass0-3 setup windows (RwTextureCreate, RT
	// switches) that run outside the SEH helpers. Helpers save/restore this
	// flag so the hoist survives across passes. Every return below clears it.
	g_inGuardedIm2DPass = 1;

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
		g_inGuardedIm2DPass = 0;
		g_renderPhase = "";
		ImmediateModeRenderStatesReStore();
		smaaRestoreRawGeom();
		return;
	}

	// Intermediates are camera-sized; keep rasterization on the camera
	// viewport so the quad writes the same texel region its UVs address
	// (s_ffQuad: positions 0..w x 0..h = UV 0..1 on a w x h raster).
	D3DVIEWPORT9 vpSaved;
	dev->GetViewport(&vpSaved);
	D3DVIEWPORT9 vpCam = { 0, 0, (DWORD)camW, (DWORD)camH, 0.0f, 1.0f };

	g_renderPhase = "smaa_p0_rt";
	if(!guardedSetRT(g_smaaEdgeRaster, "smaa_p0_rt")){
		// Guarded fault (or NULL raster) — fail open this frame, latch broken.
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
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

	// Set camera-sized scene copy as input texture on stage 0 (UV 0..1 =
	// full valid frame; the padded FB would sample stretched under s_ffQuad)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaSceneRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind previous frame for motion detection on stage 1
	if(g_smaaPrevFrameRaster){
		if(!g_smaaPrevFrameTexRW){
			g_renderPhase = "smaa_texcreate";
			bool texFaulted = false;
			g_smaaPrevFrameTexRW = smaaTexCreate(g_smaaPrevFrameRaster, "smaa_texcreate", &texFaulted);
			if(texFaulted){
				smaaGuardBail(dev, drawBuffer, &vpSaved);
				return;
			}
			if(g_smaaPrevFrameTexRW){
				RwTextureSetFilterMode(g_smaaPrevFrameTexRW, rwFILTERLINEAR);
				RwTextureSetAddressingU(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
				RwTextureSetAddressingV(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
			}
		}
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);
	}
	// Edge-detect pass — draw goes through the guardedIm2DRender choke-point
	// (C2712: DrawSMAA can't take __try). Suspend/Restore are inside the helper.
	g_renderPhase = "smaa_edge";
	if(!DrawSMAA_EdgeDetect(smaaThreshold, cameraMovement, screenParams, s_ffQuad)){
		// Edge detect faulted — depth hook already restored inside helper.
		// Skip remaining passes; fail open + latch broken.
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// ---- Pass 1: Blend Weight Calculation ----
	g_renderPhase = "smaa_p1_rt";
	if(!guardedSetRT(g_smaaBlendRaster, "smaa_p1_rt")){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
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
	g_renderPhase = "smaa_draw";
	if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_BlendWeight, s_ffQuad)){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// Clean up texture stages after Pass 1
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
	}

	// Pass2-entry guard (lane-A hardening): g_smaaSceneRaster is pass2's ONLY
	// colour input (bound at the "Bind camera-sized scene copy" line below).
	// If it went NULL or stale-sized since the entry self-check, the
	// neighbourhood blend renders NOTHING into edgeRaster and Pass3 (or the
	// no-temporal fallback presenting edgeRaster) would show a stale
	// breadcrumb frame. Fail CLOSED with the same latch as the entry
	// self-check: smaaGuardBail rebinds the camera RT, restores state and
	// sets s_smaaBroken — a stale edgeRaster can NEVER be presented; SMAA
	// self-skips until the next resolution change.
	if(!g_smaaSceneRaster ||
	   g_smaaSceneRaster->width != w || g_smaaSceneRaster->height != h){
		dbglog("[SMAA] PASS2 GUARD: scene=%p (%dx%d) != camera %dx%d — latching broken, edgeRaster NOT presented",
			(void*)g_smaaSceneRaster,
			g_smaaSceneRaster ? g_smaaSceneRaster->width : 0,
			g_smaaSceneRaster ? g_smaaSceneRaster->height : 0, w, h);
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// ---- Pass 2: Neighborhood Blending → edgeRaster (temp reuse) ----
	// Render to edgeRaster instead of drawBuffer so Pass 3 temporal can read it
	// while writing to drawBuffer — avoids D3D9 read-write conflict on same surface.
	// edgeRaster is done being read after Pass 1, safe to reuse as temp.
	g_renderPhase = "smaa_p2_rt";
	if(!guardedSetRT(g_smaaEdgeRaster, "smaa_p2_rt")){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
	dev->SetViewport(&vpCam);

	IDirect3DSurface9 *p2rt = NULL;
	dev->GetRenderTarget(0, &p2rt);
	if(dbglog_throttle("smaaP2"))
		dbglog("[SMAA-DIAG] Pass2: edgeRaster=%p RT0=%p shader=%p", g_smaaEdgeRaster, p2rt, SMAA_BlendNeighbor);
	if(p2rt) p2rt->Release();

	// Bind camera-sized scene copy as color input on stage 0 (UV 0..1)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaSceneRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind blend raster on stage 1 via RwD3D9SetTexture
	if(!g_smaaBlendTexRW && g_smaaBlendRaster){
		g_renderPhase = "smaa_texcreate";
		bool texFaulted = false;
		g_smaaBlendTexRW = smaaTexCreate(g_smaaBlendRaster, "smaa_texcreate", &texFaulted);
		if(texFaulted){
			smaaGuardBail(dev, drawBuffer, &vpSaved);
			return;
		}
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
	g_renderPhase = "smaa_draw";
	if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_BlendNeighbor, s_ffQuad)){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// Cleanup texture stages after Pass 2
	RwD3D9SetTexture(NULL, 1);
	d3d9device->SetTexture(2, NULL);
	d3d9device->SetTexture(3, NULL);

	// ---- Pass 3: Temporal Resolve → drawBuffer (final output) ----
	// Reads edgeRaster (current SMAA result) + prevFrameRaster (history),
	// writes directly to drawBuffer. No extra copy-back blit needed.
	// Pause gate: DrawVelocityBuffer skips paused/menu frames (~:6512) so the
	// velocity buffer freezes, but reprojection against that stale velocity
	// ghosts while paused. Gate the temporal pass off in menus/pause so it falls
	// through to the spatial-only fallback below (unchanged).
	if(SMAA_Temporal && g_smaaPrevFrameRaster && !IsGameInMenuOrPaused()){
		g_renderPhase = "smaa_p3_rt";
		// Re-bind the camera frame raster through the camera context (see
		// smaaGuardBail): RwD3D9SetRenderTarget faults on rwRASTERTYPECAMERA.
		if(Scene.camera){
			RwCameraEndUpdate(Scene.camera);
			RwCameraBeginUpdate(Scene.camera);
		}
		// BeginUpdate may have re-applied camera states — re-assert the
		// fullscreen-pass state set from Pass0 before the final draw.
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		dev->SetViewport(&vpSaved);

		// Bind current SMAA result (edgeRaster) on s0
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaEdgeRaster);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

		// Bind previous frame on s1
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);

		// Bind velocity buffer on s2 for history reprojection (POINT/CLAMP)
		if(g_velocityTex){
			dev->SetTexture(2, g_velocityTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		}

		// Temporal constants: blendStrength = CURRENT-frame weight at rest
		// (0.4 = 60% history at rest — audit: rest-state retention 85.4%->46%,
		// ghost tail 34->7 frames), motionScale = how fast the
		// motion signal saturates luma/velocity difference.
		// On the first frame, the history raster contains undefined data (black).
		// Use blendStrength=1.0 (all current, no history) to avoid a dark flash.
		// Subsequent frames blend normally with accumulated history.
		// motionScale 2.0 -> 3.0: moderate camera motion (walking pans) only
		// produced lumaDiff ~0.1..0.3, which left blendFactor around 0.5..0.65
		// = 35..45% of every pixel taken from the PREVIOUS frame — reads as a
		// permanent smear/"motion blur that never turns off" plus trailing
		// artifacts, entirely independent of the motionBlurEnable checkbox
		// (DrawMotionBlur's own gate is correct — see :6494+).
		float blendStrength = g_smaaHistoryValid ? 0.4f : 1.0f;
		// Hard history TTL: every 8 valid-history frames force a full current-frame
		// refresh (blendStrength=1.0) to kill the accumulated ghost tail, then reset.
		if(g_smaaHistoryValid && ++s_smaaHistAge >= 8){
			blendStrength = 1.0f;
			s_smaaHistAge = 0;
		}
		float temporalP[4] = { blendStrength, 3.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, temporalP, 1);
		RwD3D9SetPixelShaderConstant(1, screenParams, 1);

		// c2 = (velocity, rotation, tanFovX, tanFovY) — velocity-driven temporal
		// weight for the resolve shader. Velocity/rotation reuse the per-frame
		// camera tracking above (same static-prev pattern as DrawMotionBlur) with
		// the same gains so SMAA softens in sync with motion blur. FOV comes from
		// the camera view window (tan half-FOV, same as vehiclePipe.cpp iblParams).
		float tanX = 0.65f, tanY = 0.45f;
		if(Scene.camera){
			tanX = Scene.camera->viewWindow.x;
			tanY = Scene.camera->viewWindow.y;
		}
		float velFactor = min(1.0f, cameraVelocity * 0.25f);
		float rotFactor = min(1.0f, cameraRotation * 2.0f);
		float motionP[4] = { velFactor, rotFactor, tanX, tanY };
		RwD3D9SetPixelShaderConstant(2, motionP, 1);

		// c3 = (near, far, maxHistClamp, velToPx) — history reprojection params.
		// near/far mirror DrawNormalBufferToTexture's c0 source. velToPx is 1.0
		// when the velocity buffer is bound (velocity is stored in UV units) and
		// 0.0 otherwise, so the shader samples history unshifted when s2 is null.
		// maxHistClamp 0.9 -> 1.0: 0.9 is the shader's CEILING on the
		// current-frame weight, i.e. it forced >=10% of the previous frame into
		// every pixel on EVERY frame ("never fully discards history"). That
		// floor is the residual trailing/ghosting after lane3's scene-copy fix,
		// and it is what reads as motion blur while the motionBlur checkbox is
		// off. 1.0 lets the motion signal reach a full current frame at speed
		// while minBlend (= blendStrength, 0.1) still keeps the slow static
		// accumulation; it also makes the blendStrength=1.0 first frame a true
		// 100% current (0.9 used to leak 10% undefined history on frame 1).
		float smaaNear = Scene.camera ? RwCameraGetNearClipPlane(Scene.camera) : 0.1f;
		float smaaFar = Scene.camera ? RwCameraGetFarClipPlane(Scene.camera) : 500.0f;
		float histP[4] = { smaaNear, smaaFar, 1.0f, g_velocityTex ? 1.0f : 0.0f };
		RwD3D9SetPixelShaderConstant(3, histP, 1);

		if(dbglog_throttle("smaa_vel"))
			dbglog("[SMAA] velocity: vel=%.3f rot=%.3f tanFov=(%.3f, %.3f)",
				velFactor, rotFactor, tanX, tanY);

		g_renderPhase = "smaa_draw";
		if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_Temporal, s_ffQuad)){
			RwD3D9SetTexture(NULL, 1);
			dev->SetTexture(2, NULL);
			smaaGuardBail(dev, drawBuffer, &vpSaved);
			return;
		}

		// Cleanup
		RwD3D9SetTexture(NULL, 1);
		dev->SetTexture(2, NULL);

		if(dbglog_throttle("smaaTemporal"))
			dbglog("[SMAA-DIAG] Pass3 Temporal: edge=%p + prev=%p -> drawBuf=%p shader=%p",
				g_smaaEdgeRaster, g_smaaPrevFrameRaster, drawBuffer, SMAA_Temporal);
	} else {
		// No temporal: Pass 2 result is in edgeRaster, copy it into the camera
		// frame raster with the same EndUpdate/PushContext/RenderFast/Pop/
		// BeginUpdate idiom as UpdateFrontBuffer. RwD3D9SetRenderTarget faults
		// on rwRASTERTYPECAMERA, so never bind drawBuffer that way.
		if(dbglog_throttle("smaa_p3_fallback"))
			dbglog("[SMAA-DIAG] Pass3 skipped (Temporal=%p prev=%p): presenting edgeRaster (pass2 composite, NOT raw edge/blend)",
				SMAA_Temporal, g_smaaPrevFrameRaster);
		g_renderPhase = "smaa_p3_rt";
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(drawBuffer);
		RwRasterRenderFast(g_smaaEdgeRaster, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		dev->SetViewport(&vpSaved);
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

	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	ImmediateModeRenderStatesReStore();
	smaaRestoreRawGeom(); // push the raw ZENABLE/ZWRITE/ALPHATEST/CULL values
	                       // back on BOTH layers — the rw-only ReStore above
	                       // cannot see them, and SMAA is the last pass of the
	                       // frame (they would carry into the next scene).
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

	// Lazy-init velocity texture (res-aware: recreate if camera resolution changed)
	if(!g_velocityTex || w != s_velocityW || h != s_velocityH){
		if(g_velocityTex){
			if(g_velocitySurf){ g_velocitySurf->Release(); g_velocitySurf = NULL; }
			g_velocityTex->Release();
			g_velocityTex = NULL;
		}
		if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_velocityTex, NULL))){
			dbglog("[PostFX] DrawVelocityBuffer: CreateTexture velocity failed");
			config->velocityBufferEnable = 0;
			return;
		}
		g_velocityTex->GetSurfaceLevel(0, &g_velocitySurf);
		s_velocityW = w;
		s_velocityH = h;
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
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // BEFORE Store (DepthHook below writes raw ZENABLE)
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
		// Wave-2: previous-frame depth on s1 (POINT/CLAMP, NULL-safe)
		dev->SetTexture(1, g_prevDepthValid ? g_prevDepthTex : NULL);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

		// Upload constants
		RwD3D9SetPixelShaderConstant(0, &invCurVP, 4); // c0-c3: inverse current VP
		RwD3D9SetPixelShaderConstant(4, &g_prevVPMatrix, 4); // c4-c7: previous VP
		float screenParams[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(8, screenParams, 1); // c8: screen params
		// c9: Wave-2 prev-depth {prevDepthValid, farPlane, 0, 0}
		float c9PrevDepth[4] = { (float)g_prevDepthValid, Scene.camera->farPlane, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(9, c9PrevDepth, 1);

		// Render fullscreen quad — camera-sized with UV 0..1, NOT
		// colorfilterVerts (0..2048 positions): the velocity RT is w x h, so
		// the game's quad clipped at the screen only wrote UV 0..0.9375 x
		// 0..0.527 — the right edge and the WHOLE BOTTOM HALF of the velocity
		// buffer kept the Clear() value (zero velocity) and every reprojection
		// consumer (SMAA temporal history, SSAO temporal, motion blur) was
		// unshifted there -> trailing/ghosting on the lower half of the screen.
		SetupFullscreenQuad((float)w, (float)h);
		overrideIm2dPixelShader = VelocityReconstruct;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Unbind INTZ from s0 before the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO/DrawHeightFog); the cleanup below stays as-is.
		dev->SetTexture(0, NULL);
		// Restore depth hook (re-binds INTZ as DS)
		DepthHook_Restore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawVelocityBuffer CRASHED in depth pass exception=0x%08X", GetExceptionCode());
		dev->SetTexture(0, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
	}

	// Restore render target
	dev->SetRenderTarget(0, origRT);
	if(origRT) origRT->Release();

	// Cleanup
	dev->SetTexture(0, NULL);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetTexture(1, NULL);
	dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}
	// Store VP for next frame
	memcpy(&g_prevVPMatrix, &curVP, sizeof(D3DMATRIX));

	// NOTE: CopyDepthToPrev() moved to ColourFilter_switch, after DrawSSAO_Overhaul,
	// so SSAO temporal reprojection reads the actual previous-frame depth.
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

	// Re-sync the front buffer: ColourFilter_Modern leaves pRasterFrontBuffer
	// at the pre-tonemap grade, so binding it stale would repaint the camera
	// from an intermediate frame and make motion blur look like it bypasses
	// colour filtering/tonemapping. Reads the CURRENT post-grade frame.
	CPostEffects::UpdateFrontBuffer();

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

	// Combine camera movement into a single factor (0=still, 1=fast movement).
	// Gains softened from 0.25/2.0: the old values saturated mov to 1.0 at
	// vel≈4 (and instantly on teleports where vel spikes to ~1000), pinning
	// max blur on every fast frame. 0.15/1.0 ramps gradually at driving
	// speeds; gate 0.002 still engages at slow driving (~0.013 units/frame).
	float cameraMovement = min(1.0f, (cameraVelocity * 0.15f) + (cameraRotation * 1.0f));

	// Skip if barely moving
	if(cameraMovement < 0.002f)
		return;

	// Speed ramp — smoothstep deadzone + full-frame weight (the "ramps too
	// fast" fix: previously ANY motion drove the velocity-buffer term straight
	// into the 32px maxBlurPx clamp, so walking looked like driving).
	// Telemetry from skygfx_dbg.log: idle/walking vel≈0.018, city driving
	// 0.135..0.487; rot = 1-cos(dtheta) ≈ 0 while translating, spikes on fast
	// mouse flicks. speedRef = max(translation, turn-rate) so flicks still blur.
	//   speedRamp  : walking (<0.05) → 0 (no blur), full by 0.12 (bicycle/low drive)
	//   fullFrameW : running (<0.09) → 0 = edge-only lens blur,
	//                bicycle/vehicle (≥0.14) → 1 = uniform full-frame blur
	float speedRef = max(cameraVelocity, cameraRotation);
	float speedRamp;
	if(speedRef >= 0.12f) speedRamp = 1.0f;
	else if(speedRef <= 0.05f) speedRamp = 0.0f;
	else { float t = (speedRef - 0.05f) / (0.12f - 0.05f); speedRamp = t * t * (3.0f - 2.0f * t); }
	float fullFrameW;
	if(speedRef >= 0.14f) fullFrameW = 1.0f;
	else if(speedRef <= 0.09f) fullFrameW = 0.0f;
	else { float t = (speedRef - 0.09f) / (0.14f - 0.09f); fullFrameW = t * t * (3.0f - 2.0f * t); }

	// Optional: reduce blur when camera is moving very fast (camera-aware mode).
	// effectiveStrength is speed-ramped FIRST: walking deadzone zeroes both the
	// velocity-buffer term (c0.x) and the camera term (via camTermScale below).
	float effectiveStrength = config->motionBlurStrength * speedRamp;
	if(config->motionBlurCameraAware && cameraMovement > 0.8f){
		effectiveStrength *= (1.0f - (cameraMovement - 0.8f) * 2.0f);
		effectiveStrength = max(0.05f, effectiveStrength);
	}

	if(dbglog_throttle("mb_draw"))
		dbglog("[PostFX] DrawMotionBlur: vel=%.3f rot=%.3f mov=%.3f ramp=%.2f fullF=%.2f strength=%.3f shader=%p",
			cameraVelocity, cameraRotation, cameraMovement, speedRamp, fullFrameW, effectiveStrength, MotionBlur_Burnout);

	// Walking/idle deadzone: intensity ramp is 0 → nothing to blur, skip the
	// whole pass (FB already synced by UpdateFrontBuffer above, same as the
	// cameraMovement gate — no state left dirty on this path).
	if(effectiveStrength < 1e-4f)
		return;

	// Setup render states
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
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

	// Bind depth texture on s2 for depth-aware blur scaling (INTZ, sampled while
	// the depth hook is suspended — mirrors the SMAA edge pass).
	__try {
		if(g_ssaoDepthTex){
			DepthHook_Suspend();
			dev->SetTexture(2, g_ssaoDepthTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
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

		// c2: (cameraTerm, fullFrameW, fbScaleU, fbScaleV)
		// cameraTerm = camera movement x strength knob (normalized so the INI
		//   default 0.4 keeps its prior response: 0.4 * 2.5 = 1.0). The
		//   strength knob is already speed-ramped above, so the walking
		//   deadzone flows into BOTH the velocity-buffer term (c0.x) and this
		//   camera term. Lowering motionBlurStrength tames both.
		// fullFrameW = 0 edge-only lens blur (running) .. 1 full frame (vehicle),
		//   consumed by MotionBlur_Burnout's radial lerp.
		// fbScale = screen/frontBuffer: pRasterFrontBuffer holds the frame 1:1
		//   top-left of a larger (e.g. 2048^2) raster — the quad is screen-space
		//   UV 0..1, so the s0 fetch scales into the content region. s1 (motion)
		//   and s2 (depth) are screen-sized and sample UV 0..1 directly.
		//   (Old code dropped cameraRotation/dt here — the shader never read them.)
		float camTermScale = min(1.0f, max(0.0f, effectiveStrength * 2.5f));
		RwRaster *fbRas = CPostEffects::pRasterFrontBuffer;
		float fbU = fbRas ? (float)w / max((float)fbRas->width, 1.0f) : 1.0f;
		float fbV = fbRas ? (float)h / max((float)fbRas->height, 1.0f) : 1.0f;
		float c2[4] = { cameraMovement * camTermScale, fullFrameW, fbU, fbV };
		RwD3D9SetPixelShaderConstant(2, c2, 1);

		// c3: (near, far, tanFovX, maxBlurPx) — depth-aware blur scaling.
		// near/far mirror DrawNormalBufferToTexture's c0 source; tanFovX mirrors
		// the SMAA temporal c2 FOV source. maxBlurPx caps the blur in pixels.
		float mbNear = Scene.camera ? RwCameraGetNearClipPlane(Scene.camera) : 0.1f;
		float mbFar = Scene.camera ? RwCameraGetFarClipPlane(Scene.camera) : 500.0f;
		float mbTanX = Scene.camera ? Scene.camera->viewWindow.x : 0.65f;
		float c3[4] = { mbNear, mbFar, mbTanX, 32.0f }; // maxBlurPx (was 64 — halved: the old cap produced a heavy 64px smear on every fast frame)
		RwD3D9SetPixelShaderConstant(3, c3, 1);

		// Screen-space white quad UV 0..1 (NOT colorfilterVerts: those are a
		// 2048-space quad whose visible UVs only span ~0.9375 x 0.527, leaving
		// the screen-sized motion/depth fetches covering only the top-left of
		// their buffers and putting the radial/lens centre off-screen — the
		// bottom half of the screen sampled a stretched top-half depth slice).
		// FB content region is remapped in-shader via c2.zw (fbScale).
		SetupFullscreenQuad((float)w, (float)h);

		// Render fullscreen quad with motion blur shader
		overrideIm2dPixelShader = MotionBlur_Burnout;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Restore depth hook after the pass (mirrors edge pass)
		if(g_ssaoDepthTex){
			dev->SetTexture(2, NULL); // unbind INTZ before Restore (mirrors DrawHeightFog)
			DepthHook_Restore();
		}
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		if(g_ssaoDepthTex){
			dev->SetTexture(2, NULL); // same unbind before the bail-out Restore
			DepthHook_Restore(); // keep Suspend/Restore balanced on fault
		}
	}

	// Cleanup texture stages
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
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
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

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

	// Re-sync the front buffer: ColourFilter_Modern leaves pRasterFrontBuffer
	// at the pre-tonemap graded intermediate. Without this the fog pass
	// repaints the camera from that stale buffer, which made enabling height
	// fog look like it was replacing the colour filter / tonemapper output.
	CPostEffects::UpdateFrontBuffer();

	// Effective fog params — sanitize the values skygfx.h's comments promised
	// but readIni never defaulted (INI ships heightFogEnable only, so falloff
	// read as 0 => heightFactor == 1.0 at EVERY height, and menu-dragged
	// density 0.042 => 98% fog at 100m: a full-screen blue wash stacked on top
	// of the game's own distance fog).
	CColourSet &tc = CTimeCycle__m_CurrentColours;
	float fogDensity = config->heightFogDensity;
	if(fogDensity <= 0.0f) fogDensity = 0.0015f;   // documented default
	if(fogDensity > 0.004f) fogDensity = 0.004f;    // never a near-opaque wall
	float fogFalloff = config->heightFogHeightFalloff;
	if(fogFalloff <= 0.0f) fogFalloff = 0.1f;       // 0 = no height falloff anywhere
	float tcDensity = 0.0f;
	if(config->heightFogTimecycleScale > 0.0f && tc.fogStart > 1.0f){
		tcDensity = config->heightFogTimecycleScale / tc.fogStart;
		if(tcDensity > 0.001f) tcDensity = 0.001f; // capped: was the grey-wash term
	}
	float effectiveDensity = fogDensity + tcDensity;
	// Colour: explicit config RGB wins, else timecycle horizon (skyBot).
	// lowCloudsR/G/B was the low-cloud tint = the blue/cyan haze itself.
	float fogR = config->heightFogR, fogG = config->heightFogG, fogB = config->heightFogB;
	bool fogFromConfig = (fogR + fogG + fogB) > 0.001f;
	if(!fogFromConfig){
		fogR = tc.skyBotR / 255.0f;
		fogG = tc.skyBotG / 255.0f;
		fogB = tc.skyBotB / 255.0f;
	}
	if(dbglog_throttle("hfog_draw"))
		dbglog("[PostFX] DrawHeightFog: density=%.4f (tc=%.4f) falloff=%.2f startH=%.1f colour=%s (%.2f,%.2f,%.2f) shader=%p",
			effectiveDensity, tcDensity, fogFalloff, config->heightFogStartHeight,
			fogFromConfig ? "config" : "skyBot", fogR, fogG, fogB, HeightFog);

	// PS constants c0-c7 are shared by the whole postfx chain — save them so
	// the fog pass can't leak state into later same-frame/next-frame passes
	// (decouples height fog from tonemap/colourfilter consumers).
	float savedPSConsts[4 * 8];
	bool savedPSConstsValid = false;

	// Declared before __try so the __except path can restore them too
	DWORD rawGeom[9];
	bool rawGeomSaved = false;

	// Suspend depth hook so g_ssaoDepthTex can be sampled on s1
	__try {
		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		DepthHook_Suspend();
		savedPSConstsValid = SUCCEEDED(dev->GetPixelShaderConstantF(0, savedPSConsts, 8));

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

		// c0: fogParams — sanitized values computed above. maxFog 0.7 keeps a
		// horizon line visible and compounds with the game's own distance fog
		// instead of replacing it at full opacity.
		float c0[4] = {
			effectiveDensity,
			fogFalloff,
			config->heightFogStartHeight,
			0.7f // maxFog (was hardcoded 1.0 = total whiteout)
		};
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		// c1: fog colour (config RGB or timecycle horizon — computed above)
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

		// c3: front-buffer UV scale — s0 is the padded 2048² front buffer while
		// s1 (INTZ depth) is screen-sized; the shader fetches scene × fbParams.xy
		// and depth at raw UV (the old setup sampled BOTH at /2048 UVs).
		float fbU = 1.0f, fbV = 1.0f;
		if(CPostEffects::pRasterFrontBuffer){
			fbU = (float)w / max((float)CPostEffects::pRasterFrontBuffer->width, 1.0f);
			fbV = (float)h / max((float)CPostEffects::pRasterFrontBuffer->height, 1.0f);
		}
		float c3[4] = { fbU, fbV, 0.0f, 0.0f };
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

		// Render fullscreen quad — RT-sized with UV 0..1 (colorfilterVerts are a
		// hardcoded 2048² quad whose /2048 UVs misalign every screen-sized buffer)
		SetupFullscreenQuad((float)w, (float)h);
		overrideIm2dPixelShader = HeightFog;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Restore PS constants saved before the pass (fog c0-c5 overwrite)
		if(savedPSConstsValid)
			dev->SetPixelShaderConstantF(0, savedPSConsts, 8);

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
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawHeightFog CRASHED exception=0x%08X", GetExceptionCode());
		if(savedPSConstsValid)
			dev->SetPixelShaderConstantF(0, savedPSConsts, 8);
		DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved)
			RestoreRawGeomStates(rawGeom);
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

	// Re-sync the front buffer so god rays read the current post-grade frame
	// (pRasterFrontBuffer may still hold the pre-tonemap intermediate if the
	// earlier effects were disabled this frame).
	CPostEffects::UpdateFrontBuffer();

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

	// Pass breadcrumb: fires ONLY on frames where the sun gate above passed and
	// the ray quad is about to be drawn — if a covered-frame forensics pass ever
	// sees this line immediately before the symptom, this is the pass.
	if(dbglog_throttle("grad_draw"))
		dbglog("[PostFX] DrawGodRays: sun=(%.2f,%.2f) exp=%.4f dec=%.2f shader=%p blend=ONE/ONE(rw+raw)",
			sunScreenX, sunScreenY, config->godRaysExposure,
			config->godRaysDecay, GodRays);

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // no depth hook in this pass
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	// Enable additive blending — through the RW cache FIRST, raw mirror after.
	// Doctrine (same failure mode the SSAO multiply documents): RwIm2D re-emits
	// its cached rwRENDERSTATEVERTEXALPHAENABLE (just set FALSE above) at draw
	// time, which rewrites D3DRS_ALPHABLENDENABLE=FALSE over these raw sets.
	// The draw then degrades to a REPLACE of the whole screen with the shader's
	// ray-only output (GodRays.hlsl deliberately emits NO scene colour for the
	// additive contract) = full-screen black with bright streaks along the
	// sun-facing edges, OUTDOORS ONLY — the sun-on-screen gate below returns
	// early whenever the sun is behind the camera or off-screen, which is why
	// interiors looked perfect.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

	// s0: scene color (front buffer)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

	// c0: sun screen position
	float c0[4] = { sunScreenX, sunScreenY, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	// c1: ray params — sanitize: config may be zero-initialised (no INI key),
	// which previously collapsed the effect to a pure additive frame double.
	float grExposure = (config->godRaysExposure > 0.0f) ? config->godRaysExposure : 0.0034f;
	float grDecay    = (config->godRaysDecay > 0.0f && config->godRaysDecay <= 1.0f) ? config->godRaysDecay : 1.0f;
	float grDensity  = (config->godRaysDensity > 0.0f) ? config->godRaysDensity : 0.84f;
	float grWeight   = (config->godRaysWeight > 0.0f) ? config->godRaysWeight : 1.0f;
	float c1[4] = { grExposure, grDecay, grDensity, grWeight };
	RwD3D9SetPixelShaderConstant(1, c1, 1);

	// c2: num samples
	float numSamples = (float)config->godRaysNumSamples;
	if(config->godRaysNumSamples < 1) numSamples = 20.0f;
	float c2[4] = { numSamples, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	// Render fullscreen quad — SEH-guarded so the override clears on fault too.
	guardedIm2DRender(GodRays, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "godrays");

	// Cleanup — push the blend factors back through BOTH layers: the rw cache
	// now owns ONE/ONE for this pass, and leaving it there would leak additive
	// blending into the next draw that re-emits cached states.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}
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
