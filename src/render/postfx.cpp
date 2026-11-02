#include "skygfx.h"
#include "debug_handl.h"
#include "ModuleList.hpp"
#include "postfx.h"
#include "chars.h"
#include "depthhook.h"
#include "postfx_underwater.h"
#include "profiler.h"

// ---- PERF log spam gate (postfx.cpp-local; skygfx.h untouched) ----------
// skygfx.h's PerfTimer logs UNCONDITIONALLY at scope exit, and this file's
// PERF_SCOPE sites (NormalBuf, SSAO, PipeChain, SunShafts, DOF, Bloom, CA,
// Grain, Vignette, IVGrade, MotionBlur, HeightFog, GodRays, FXAA3 — ~18)
// fired every draw -> 13,988 "PERF [...]" lines accumulated in skygfx_dbg.log.
// Per-draw file I/O is unacceptable. #undef skygfx.h's macro here and swap in
// a local gated variant that only logs at scope exit when config->profilerEnable
// is set. Timing is preserved when profilerEnable=1 (identical "PERF [x] y ms"
// line). The scoped object is only used in non-__try functions (first __try in
// this file is :2751, after the last PERF_SCOPE at :1990), so no C2712.
#undef PERF_SCOPE
struct PostfxPerfGate {
	const char *name;
	double startMs;
	PostfxPerfGate(const char *n) : name(n), startMs(perfNow()) {}
	~PostfxPerfGate(){
		if(config && config->profilerEnable)
			dbglog("PERF [%s] %.2f ms", name, perfNow() - startMs);
	}
};
#define PERF_SCOPE(name) PostfxPerfGate _perf##__LINE__(name)

// Forward decls (definitions live further down this file) — the pass-entry
// instrumentation logs below reference these handles.
extern void *radiosityPS, *blurPS;
extern void ForwardPlus_DrawClusterDebug(void);

// Peds pipe device-reset latch invalidation (peds/peds_pipe.cpp:188, declared
// in peds/peds_pipe.h:78 — header not included here; chars.h precedent is a
// plain extern for the same reason). Called from OnDeviceReset after
// chars_OnDeviceReset().
extern void PedsPipe_OnDeviceReset(void);

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
// Cutscene≠menu postfx gate (expIV gate fix)
//
// IsGameInMenuOrPaused() (postfx_utils.cpp) bundles CCutsceneMgr__ms_running
// into its skip bool. Git-blame rationale (545bc02, 2026-07-30): the guard
// was introduced as a blanket "skip expensive PostFX during menus/loading/
// pause" performance guard for SMAA/SSAO — cutscene was bundled in without
// any pass-specific reason (no double-tonemap protection involved). But
// cutscenes are RENDERED SCENES: the grading chain must run on them, and
// skipped frames displayed the PBR pipes' linear HDR untonemapped ->
// washed-out white/pink (expIV session 07:26:29, airport cutscene).
//
// These helpers gate on REAL menu/pause + the game's disable flag only.
// Passes that run on cutscene frames emit a one-shot
// "[PF] cutscene postfx: ENABLED (was skipped)" breadcrumb for log
// verification. Deliberately duplicated as static inline in
// postfx_colourfilter.cpp — postfx.h is outside this fix's edit set.
// ============================================================
static inline bool PostfxMenuPauseOnly(void)
{
	return CMenuManager__m_bMenuActive || CPostEffects::m_bDisableAllPostEffect;
}

// One-shot per pass: first cutscene frame where the pass now runs (it was
// skipped before the cutscene≠menu fix). Fires once per session, no throttle
// needed.
static inline void
CutscenePostfxEnabledLog(const char *pass, bool *fired)
{
	if(*fired) return;
	*fired = true;
	dbglog("[PF] cutscene postfx: ENABLED (was skipped) pass=%s", pass);
}

// ============================================================
// Debug BMP dump checkpoints (INI-gated: postfxDumpDebug=1)
// Saves key surfaces to <gameDir>\skygfx_dump_<tag>_<frame>.bmp.
//
// Two capture windows:
//   1. AUTO  — the first POSTFX_DUMP_AUTO_FRAMES frames of the session.
//   2. ARMED — POSTFX_DUMP_ARM_FRAMES frames starting at the next frame after
//      the F10 hotkey (or PostFX_ArmDump()) fires.
//
// The AUTO window alone was useless for the black-screen bug: the first frames
// of a session are the loading/spawn screen, so every auto dump captured a
// healthy frame. The arming window is what lets the user capture AFTER they
// can see the bug. F10 was checked to be unclaimed (F5 is config->keys[],
// F8 is main.cpp, F7/F4 are commented out).
// ============================================================
static int g_postfxDumpFrame = 0;   // incremented once per ColourFilter_switch
static bool g_postfxDumpLoggedVerts = false;

#define POSTFX_DUMP_AUTO_FRAMES 5
#define POSTFX_DUMP_ARM_FRAMES  3
#define POSTFX_DUMP_HOTKEY      VK_F10

static int  g_postfxDumpArmCount = 0;     // armed frames remaining
static bool g_postfxDumpKeyPrevDown = false;

// --- per-frame tag bookkeeping (one-line summary at end of frame) ---
#define POSTFX_DUMP_MAX_TAGS 32
static char g_postfxDumpTagBuf[POSTFX_DUMP_MAX_TAGS][24];
static int  g_postfxDumpTagCount = 0;
static int  g_postfxDumpTagFrame = -1;

// SINGLE SOURCE OF TRUTH for the dump gate. Every checkpoint (postfx.cpp,
// postfx_ssao.cpp) must route through this so "armed" bypasses the
// first-5-frames cap uniformly.
static bool PostFX_DumpActive(void)
{
	return g_postfxDumpArmCount > 0 || g_postfxDumpFrame < POSTFX_DUMP_AUTO_FRAMES;
}

// Records a tag on the first SUCCESSFUL BMP write. Deliberately NOT reset on
// a frame-counter change: scene_cam runs before ColourFilter_switch bumps
// g_postfxDumpFrame, so a per-counter reset there would silently discard the
// most important tag of the capture. The buffer is cleared by
// PostFX_DumpFlushTags() at the end of each rendered frame instead, so one
// group == one displayed frame (tags numbered N and N+1 by write time, which
// is why the group is labelled with the frame of its FIRST tag).
static void PostFX_DumpNoteTag(const char *tag)
{
	if(!tag) return;
	if(g_postfxDumpTagCount == 0)
		g_postfxDumpTagFrame = g_postfxDumpFrame;
	if(g_postfxDumpTagCount < POSTFX_DUMP_MAX_TAGS){
		strncpy(g_postfxDumpTagBuf[g_postfxDumpTagCount], tag,
			sizeof(g_postfxDumpTagBuf[0]) - 1);
		g_postfxDumpTagBuf[g_postfxDumpTagCount][sizeof(g_postfxDumpTagBuf[0]) - 1] = '\0';
		g_postfxDumpTagCount++;
	}
}

// Called once per frame from DrawFinalEffects, AFTER every checkpoint that
// frame can produce (scene_cam, fb_after_copy, after_ssao, after_pipechain,
// the dfe_*/cf_* fine-grained probes) has had its chance to fire.
static void PostFX_DumpFlushTags(void)
{
	if(g_postfxDumpTagCount <= 0) return;
	char line[1024];
	size_t used = 0;
	int n = snprintf(line, sizeof(line),
		"[Dump] frame=%d wrote %d tag(s):", g_postfxDumpTagFrame, g_postfxDumpTagCount);
	if(n <= 0) return;
	used = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
	for(int i = 0; i < g_postfxDumpTagCount; i++){
		size_t tl = strlen(g_postfxDumpTagBuf[i]);
		if(used + tl + 2 >= sizeof(line)) break;
		line[used++] = ' ';
		memcpy(line + used, g_postfxDumpTagBuf[i], tl);
		used += tl;
	}
	line[used] = '\0';
	dbglog("%s", line);
	g_postfxDumpTagCount = 0;
}

// Arm the harness for the NEXT `frames` frames (bypasses the AUTO cap).
void PostFX_ArmDump(int frames, const char *source)
{
	if(frames <= 0) frames = POSTFX_DUMP_ARM_FRAMES;
	g_postfxDumpArmCount = frames;
	dbglog("[Dump] ARMED by %s for %d frame(s) from frame=%d (postfxDumpDebug=%d, F10)",
		source ? source : "call", frames, g_postfxDumpFrame,
		config ? (int)config->postfxDumpDebug : -1);
	if(!config || !config->postfxDumpDebug)
		dbglog("[Dump] WARNING: postfxDumpDebug is off — no BMPs will be written until it is enabled");
}

void DumpRasterToBMP(const char* tag, IDirect3DSurface9* src)
{
	if(!config || !config->postfxDumpDebug) return;
	if(!PostFX_DumpActive()) return;
	if(!src) { dbglog("[Dump] %s: src NULL", tag); return; }
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) { dbglog("[Dump] %s: no device", tag); return; }

	D3DSURFACE_DESC desc;
	if(FAILED(src->GetDesc(&desc))) { dbglog("[Dump] %s: GetDesc failed", tag); return; }

	// The staging surface MUST match the source format exactly: GetRenderTargetData
	// rejects a mismatch with D3DERR_INVALIDCALL (0x8876086C), which is what killed
	// every dump in the 23:28 session (151x "GetRenderTargetData failed") and left
	// the black screen visually undiagnosable. The camera/FB rasters are not
	// guaranteed A8R8G8B8 (X8R8G8B8 / R5G6B5 are in play).
	IDirect3DSurface9 *sysSurf = NULL;
	HRESULT hr = dev->CreateOffscreenPlainSurface(desc.Width, desc.Height,
		desc.Format, D3DPOOL_SYSTEMMEM, &sysSurf, NULL);
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
			// Bytes-per-pixel must match desc.Format now that the staging
			// surface mirrors it — a hardcoded 4 mis-reads R5G6B5 sources.
			const int bpp = (desc.Format == D3DFMT_R5G6B5 || desc.Format == D3DFMT_X1R5G5B5 ||
			                 desc.Format == D3DFMT_A1R5G5B5) ? 2 : 4;
			BYTE row[4096 * 3];
			if(bpp == 4){
				for(int x = 0; x < w; x++){
					// X8R8G8B8 / A8R8G8B8 -> BGR
					row[x*3+0] = srcRow[x*4+0]; // B
					row[x*3+1] = srcRow[x*4+1]; // G
					row[x*3+2] = srcRow[x*4+2]; // R
				}
			}else{
				for(int x = 0; x < w; x++){
					const WORD p = *(const WORD*)(srcRow + x*2);
					row[x*3+0] = (BYTE)((p & 0x001F) << 3);  // B (5->8)
					row[x*3+1] = (BYTE)((p & 0x07E0) >> 3);  // G (6->8)
					row[x*3+2] = (BYTE)((p & 0xF800) >> 8);  // R (5->8)
				}
			}
			fwrite(row, 1, w*3, f);
			// pad to 4-byte boundary
			for(int p = w*3; p < rowSize; p++) fputc(0, f);
		}
		fclose(f);
		PostFX_DumpNoteTag(tag);
		dbglog("[Dump] %s: wrote %dx%d BMP", tag, w, h);
	} else {
		dbglog("[Dump] %s: fopen failed", tag);
	}

	sysSurf->UnlockRect();
	sysSurf->Release();
}

// Dump the current bound render target (RT0)
void DumpCurrentRT(const char* tag)
{
	if(!config || !config->postfxDumpDebug) return;
	if(!PostFX_DumpActive()) return;
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
	if(!PostFX_DumpActive()) return;
	DumpCurrentRT("scene_cam");
}

// ============================================================
// colorfilterVerts white-init (black-screen hardening)
//
// colorfilterVerts (0xC400D8) is the game's shared 2048x2048 fullscreen quad,
// consumed by EVERY post pass: the colorFilterEnable=0 bypass composite, the
// FXAA3 late-AA pass, the YCbCr filter in DrawFinalEffects, the IV grade,
// GodRays and FxFramePass. It is drawn with VERTEXALPHA blending, so the
// vertex colour MULTIPLIES every one of those draws.
//
// The log proves its emissiveColor is 0x00000000 (fully black) every frame:
// its only writer in vanilla was CPostEffects::ColourFilter (0x703650), which
// stamps rgb2/rgb1 into the verts immediately before drawing. That writer no
// longer runs on the colorFilterEnable==0 path (the bypass early-returns above
// the switch) and never ran for the shader-driven filters, so nothing ever
// re-initialises the verts and a black vertex colour silently blackens every
// consumer — exactly the "world outdoors is black, HUD/particles fine"
// symptom, because only the world goes through colorfilterVerts.
//
// Fix: re-seed the verts to opaque white (0xFFFFFFFF) — the neutral for a
// multiply-style composite (src * 1). Alpha is set to 0xFF too, which under
// SRCALPHA/INVSRCALPHA makes the quad a clean source-over copy. Positions and
// UVs are deliberately untouched. Idempotent and cheap: a compare pass over
// 4 words, and nothing is written once the verts already hold white.
// ============================================================
static void EnsureColorfilterVertsVisible(void)
{
	bool needs = false;
	for(int i = 0; i < 4; i++){
		if(colorfilterVerts[i].emissiveColor != 0xFFFFFFFFu){ needs = true; break; }
	}
	if(!needs) return;
	const RwUInt32 old0 = colorfilterVerts[0].emissiveColor;
	for(int i = 0; i < 4; i++)
		colorfilterVerts[i].emissiveColor = 0xFFFFFFFFu;
	if(dbglog_throttle("cfv_white"))
		dbglog("[PostFX] EnsureColorfilterVertsVisible: colorfilterVerts[0] 0x%08X -> 0xFFFFFFFF "
			"(black vertex colour was multiplying every colorfilterVerts draw to black)",
			old0);
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


// Atmospheric PostFX forward declarations
static void DrawHeightFog(void);
static void DrawGodRays(void);






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
	// ---- corona_blend diagnostic (§4.7) ------------------------------------
	// This hook sits on the RW Im2D pixel-shader set site (0x7FB824) that EVERY
	// RwIm2DRenderIndexedPrimitive dispatch passes through — including the
	// buffered-sprite flush draws (0x70CF20 -> 0x734EA0) that rasterise the
	// corona/pointlight/cloud glow quads. Postfx passes always arrive with
	// overrideIm2dPixelShader != nil (guardedIm2DRender / manual overrides),
	// so the nil-override population below is exactly the GAME-owned Im2D
	// draws: coronas, point lights, particles, markers, HUD. Logs the
	// blend/alpha-test state + bound raster at rasterisation time: first draw
	// one-shot (full detail incl. the raw device s0 texture), then a 2 s
	// throttled sample. spriteBatch > 0 marks a buffered-sprite flush draw
	// (the corona carrier; count at 0xC6A158, same dword the flush guards on).
	// Black-quad signature to watch for: blend=0 (REPLACE) or dst=ZERO with
	// the glow texture's black RGB surround.
	//
	// RETIRED behind g_diagLevel (perf audit fix, 2026-11-01): diagnosis is
	// complete (349 samples/run) and the corona shield landed, so at the
	// default diagLevel=0 this block is a pure per-draw cost (7 raw device
	// Gets + RwRenderStateGet + GetTexture AddRef/Release on every game Im2D
	// draw that passes the throttle window). Opt back in with diagLevel>=1;
	// code kept intact for future diagnosis. NOTE: this inverts the
	// debug_handl.h "0 = full diagnostics" default for THIS receipt only —
	// deliberate opt-in, not an accident.
	if(!overrideIm2dPixelShader && d3d9device && g_diagLevel >= 1){
		bool cbFirst = dbglog_once("corona_blend_first");
		if(cbFirst || dbglog_throttle("corona_blend")){
			DWORD blendEn = 0, srcB = 0, dstB = 0, aFunc = 0, aRef = 0, aTest = 0, bop = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &aFunc);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &aRef);
			d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &aTest);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &bop);
			RwUInt32 cbRas = 0;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&cbRas);
			// L2 driver pending cache — the values _rwD3D9RenderStateFlushCache
			// will push to the device pre-draw; a d3d!=dev pair here IS the
			// cache<->device split that draws the batch REPLACE.
			DWORD cbDrvBlend = 0, cbDrvSrc = 0, cbDrvDst = 0;
			RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &cbDrvBlend);
			RwD3D9GetRenderState(D3DRS_SRCBLEND, &cbDrvSrc);
			RwD3D9GetRenderState(D3DRS_DESTBLEND, &cbDrvDst);
			int cbBatch = *(int*)0xC6A158;	// buffered-sprite count (corona carrier)
			IDirect3DBaseTexture9 *cbTex0 = NULL;
			d3d9device->GetTexture(0, &cbTex0);	// AddRef'd — released below
			dbglog("[corona_blend] %s game Im2D draw: dev blend=%d src=%d dst=%d op=%d "
			       "atest=%d func=%d ref=%d | drv blend=%d src=%d dst=%d | "
			       "rw raster=%p dev s0=%p | spriteBatch=%d "
			       "(blend 0 = REPLACE -> black quads; D3DBLEND 2=ONE 5=SRCALPHA 6=INVSRCALPHA)",
			       cbFirst ? "FIRST" : "2s",
			       (int)blendEn, (int)srcB, (int)dstB, (int)bop,
			       (int)aTest, (int)aFunc, (int)aRef,
			       (int)cbDrvBlend, (int)cbDrvSrc, (int)cbDrvDst,
			       (void*)cbRas, (void*)cbTex0, cbBatch);
			if(cbTex0)
				cbTex0->Release();
		}
	}
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
	// Throttled (was unconditional per-frame): at ~90 fps this was one log
	// write every ~11ms — pure file-I/O cost that drowned real diagnostics.
	// One sample per 2s keeps the report visible in every log; message
	// content unchanged (binary tag verification depends on it).
	if(dbglog_throttle("postfx_report"))
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


// Mobile stuff
// (struct Grade now lives in postfx.h — shared with postfx_colourfilter.cpp)


// ---- front-buffer sync status (black-frame guard) ------------------------
// UpdateFrontBuffer is a void API shared with other translation units, so it
// cannot report failure to its callers. Track the last sync result here and
// check it at every pRasterFrontBuffer consumer that lives in this file: a
// failed copy (transient non-screen camera raster, lost device, NULL camera)
// then SKIPS that consumer instead of letting it composite a stale/black
// buffer over the frame. A later successful sync repairs the latch.
static bool s_pfFbValid = true;

static bool PfFrontBufferValid(void)
{
	return s_pfFbValid;
}

static void PfFrontBufferFailed(HRESULT hr)
{
	s_pfFbValid = false;
	if(dbglog_once("pf_fb_fail"))
		dbglog("[PF] UpdateFrontBuffer FAILED hr=0x%08X", (unsigned)hr);
}

// HRESULT for a failed RwRasterRenderFast copy. Rw does not surface the
// device error, so prefer TestCooperativeLevel when the device is lost;
// otherwise the copy itself failed (E_FAIL).
static HRESULT PfFrontBufferCopyHr(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(dev){
		HRESULT coop = dev->TestCooperativeLevel();
		if(FAILED(coop))
			return coop;
	}
	return E_FAIL;
}

// Throttled breadcrumb for the SKIP side. The one-shot failure line itself is
// emitted by PfFrontBufferFailed under the pf_fb_fail tag.
static bool PfFrontBufferReady(const char *consumer)
{
	if(s_pfFbValid)
		return true;
	if(dbglog_throttle("pf_fb_skip"))
		dbglog("[PF] skip %s: front buffer sync failed this frame (stale buffer not consumed)",
			consumer ? consumer : "front-buffer consumer");
	return false;
}

void
CPostEffects::UpdateFrontBuffer(void)
{
	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer pRasterFrontBuffer is NULL!");
		PfFrontBufferFailed(E_POINTER);
		return;
	}
	if(!Scene.camera){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer Scene.camera is NULL!");
		PfFrontBufferFailed(E_POINTER);
		return;
	}
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas){
		dbglog("[PostFX] WARNING: UpdateFrontBuffer camera raster is NULL!");
		PfFrontBufferFailed(E_POINTER);
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
		// The copied region would be garbage and the FB is left stale; tell
		// the consumers in this frame to stay off it (D3DERR_INVALIDCALL is
		// the closest stock code for "source is not the screen surface").
		PfFrontBufferFailed((HRESULT)0x8876086CL);
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

	// Failure handling: RwRasterRenderFast returns NULL when the copy did not
	// happen. Latch the failure so this frame's FB consumers skip instead of
	// compositing whatever the stale front buffer still holds (the reported
	// whole-screen black at random interior angles). A lost device makes even
	// a non-NULL blit untrustworthy, so TestCooperativeLevel gates that too.
	HRESULT ufbCoop = d3d9device ? d3d9device->TestCooperativeLevel() : S_OK;
	if(!copyResult || FAILED(ufbCoop)){
		PfFrontBufferFailed(FAILED(ufbCoop) ? ufbCoop : PfFrontBufferCopyHr());
	}else{
		s_pfFbValid = true;
	}

	// Debug dump: verify the camera→FB copy actually happened
	if(config && config->postfxDumpDebug && PostFX_DumpActive()){
		if(!copyResult)
			dbglog("[Dump] UpdateFrontBuffer: RwRasterRenderFast returned NULL (copy failed)");
		DumpCurrentRT("fb_after_copy");
	}
}

RwRaster *vcs_radiosity_target1, *vcs_radiosity_target2;
static int s_vcsRadLastWidth = 0, s_vcsRadLastHeight = 0, s_vcsRadLastConfigRes = 0;
bool s_vcsRadPendingInit = false;

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
	PF_SCOPE("Radiosity_VCS");
	if(dbglog_throttle("pf_radvcs"))
		dbglog("[PF] Radiosity_VCS: radEnable=%d limit=%d intensity=%d ps=%p resMult=%d",
			config->radiosityEnable, limit, intensity, radiosityPS, config->trailsResolution);
	int i;
	int resMult = config->trailsResolution;
	RwRaster *fb;
	RwRaster *fb1, *fb2, *tmp;

	// Null guards: RwCameraGetRaster(NULL) faults inside RW, and a NULL
	// camera raster / VCS target faults at the PushContext/SetRaster sites
	// below (raster creates in Radiosity_VCS_init are unchecked).
	if(!Scene.camera)
		return;
	fb = RwCameraGetRaster(Scene.camera);
	if(!fb)
		return;
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

	// Radiosity_VCS_init does not check its RwRasterCreate results; a failed
	// create leaves a NULL target that would fault at PushContext/SetRaster.
	if(!vcs_radiosity_target1 || !vcs_radiosity_target2)
		return;

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
	PF_SCOPE("Blur_VCS");
	if(dbglog_throttle("pf_blurvcs"))
		dbglog("[PF] Blur_VCS: in=pRasterFrontBuffer out=lastFrameBuffer ps=%p",
			blurPS);
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
	// Create failed (OOM/lost device): the RwRasterPushContext(lastFrameBuffer)
	// below would fault. Bail before any state is mutated.
	if(!lastFrameBuffer)
		return;

	// Null guards: RwCameraGetRaster(NULL) faults inside RW.
	if(!Scene.camera)
		return;
	RwRaster *blurCamRas = RwCameraGetRaster(Scene.camera);
	if(!blurCamRas)
		return;
	screenw = blurCamRas->width;
	screenh = blurCamRas->height;

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

	// get current frame (guarded copy: never sample a stale/black FB)
	UpdateFrontBuffer();
	if(!PfFrontBufferReady("Blur_VCS")){
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		return;
	}

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
	PF_SCOPE("Radiosity_Shader");
	if(dbglog_once("pf_rad_first") || dbglog_throttle("pf_rad_sh")){
		int fbW = pRasterFrontBuffer ? (int)pRasterFrontBuffer->width  : 0;
		int fbH = pRasterFrontBuffer ? (int)pRasterFrontBuffer->height : 0;
		int camW = 0, camH = 0;
		RwRaster *camRasLog = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(camRasLog){ camW = (int)camRasLog->width; camH = (int)camRasLog->height; }
		int scrW = 0, scrH = 0;
		int scrOk = GetScreenSize(&scrW, &scrH) ? 1 : 0;
		int mismatch = (scrW != fbW || scrH != fbH) ? 1 : 0;
		int camMismatch = (scrW != camW || scrH != camH) ? 1 : 0;
		dbglog("[PF] Radiosity_shader: limit=%d filtP=%d rendP=%d int=%d ps=%p blend=ONE/ONE "
		       "rad=%d radDbg=%d fb=%dx%d cam=%dx%d screen=%dx%d gsOK=%d mismatch=%d camMismatch=%d",
			intensityLimit, filterPasses, renderPasses, intensity, radiosityPS,
			(int)m_bRadiosity, (int)m_bRadiosityDebug, fbW, fbH, camW, camH,
			scrW, scrH, scrOk, mismatch, camMismatch);
	}
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
	// Create failed: RwCameraSetRaster(Scene.camera, NULL) below would fault
	// inside RW. Nothing has been mutated yet — clean bail (retries next frame).
	if(!workBuffer){
		dbglog("Radiosity_shader: work buffer create failed, skipping");
		return;
	}

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
	bool radFbOk = true;

	// Sample only a frame we know copied this frame (the pass reads FB as its
	// blur source); skip the whole pass rather than blurring stale/black.
	UpdateFrontBuffer();
	radFbOk = PfFrontBufferValid();
	if(!radFbOk){
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		return;
	}

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

	// Blur vertically — SEH-guarded (guardedIm2DRender) so a fault can't
	// leave the blurPS override bound: Im2dSetPixelShader_hook would
	// re-apply it to every later Im2D dispatch, including coronas.
	params[0] = 0;
	params[1] = 1.0f/max(RwRasterGetHeight(pRasterFrontBuffer), 1);
	RwD3D9SetPixelShaderConstant(0, params, 1);
	guardedIm2DRender(blurPS, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "blur_v");
	// Never leave FB bound on s0 while UpdateFrontBuffer makes it the RT:
	// D3D9 would be sampling the same surface it is rendering to.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	UpdateFrontBuffer();
	radFbOk = PfFrontBufferValid();
	if(radFbOk){
		// Blur horizontally
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);
		params[0] = 1.0f/max(RwRasterGetWidth(pRasterFrontBuffer), 1);
		params[1] = 0;
		RwD3D9SetPixelShaderConstant(0, params, 1);
		guardedIm2DRender(blurPS, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "blur_h");
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		UpdateFrontBuffer();
		radFbOk = PfFrontBufferValid();
	}


	/* Restore original FB */
	RwCameraEndUpdate(Scene.camera);
	RwCameraSetRaster(Scene.camera, drawBuffer);
	RwCameraBeginUpdate(Scene.camera);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);

	/* Add to framebuffer */
	if(radFbOk){
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
	if(dbglog_once("pf_rad_composite"))
		dbglog("[PF] Radiosity_composite: limit=%d int=%d rendP=%d draw=%dx%d "
		       "params=(%.6f,%.6f,%.6f,%.6f) fb=%dx%d",
			intensityLimit, intensity, renderPasses, drawW, drawH,
			params[0], params[1], params[2], params[3],
			(int)pRasterFrontBuffer->width, (int)pRasterFrontBuffer->height);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;
	}else if(dbglog_throttle("pf_rad_fbskip")){
		dbglog("[PF] Radiosity_shader: composite skipped (front-buffer sync failed this frame)");
	}

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
	PF_SCOPE("Radiosity");
	if(dbglog_throttle("pf_rad"))
		dbglog("[PF] Radiosity: radEnable=%d radShaderPS=%p blurPS=%p tributary=%d/%d/%d",
			config->radiosityEnable, radiosityPS, blurPS,
			intensityLimit, filterPasses, renderPasses);
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

	if(!PfFrontBufferReady("radiosity (classic)"))
		return;

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
	// Create failed: renderBuffer (= workBuffer) would be NULL at the
	// RwCameraSetRaster/RwD3D9SetRenderTarget sites below. Nothing has been
	// mutated yet — clean bail (retries next frame).
	if(!workBuffer){
		dbglog("Radiosity: work buffer create failed, skipping");
		return;
	}

	RwRaster *renderBuffer, *textureBuffer;

	// Null guards: RwCameraGetRaster(NULL) faults inside RW.
	if(!Scene.camera)
		return;
	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);
	if(!drawBuffer){
		dbglog("Radiosity: camera raster NULL, skipping");
		return;
	}

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

void DrawNormalBufferToTexture(void);
void DrawPipeChain(void);

// Defined in main.cpp (pipe-conjunction fix #4) — re-derives
// config->pipeline/colorFilter/ivMode from the actual building/vehicle pipes.
extern void derivePipelineFromPipes(Config *c);

// Defined in postfx_colourfilter.cpp (fix-30): clears s_cfTonemapRanFrame so a
// DrawIVGrade early-return in the PREVIOUS frame can't leave the exposure-stack
// handshake stale-true into this frame. Declared locally: postfx.h is outside
// this fix's edit set.
extern void PostfxResetTonemapHandshake(void);

// ---- pf_gate: per-frame pass-gate bookkeeping (perf lane) -----------------
// One bit per pass that did no work this frame because it cannot contribute
// (config gate off, or its input latch is empty). Bits are set where the
// decision is made, then emitted ONCE per frame by PostfxGateSummary under
// the existing global throttle. POD-only (unsigned + int args): safe on the
// ColourFilter_switch path, and no formatting/string work happens unless
// the throttle window is open.
enum PostfxGateBits {
	PFG_NORMALBUF  = 1u << 0,   // normalBufferEnable=0 (caller block skipped)
	PFG_VELOCITY   = 1u << 1,   // velocityBufferEnable=0 or shader NULL
	PFG_SSAO       = 1u << 2,   // ssaoEnable=0 (dispatch skipped)
	PFG_DEPTHHIST  = 1u << 3,   // no prev-depth consumer this frame
	PFG_PIPECHAIN  = 1u << 4,   // pipeChainEnable=0 / normalBufferEnable=0
	PFG_SUNSHAFTS  = 1u << 5,
	PFG_DOF        = 1u << 6,
	PFG_BLOOM      = 1u << 7,
	PFG_CA         = 1u << 8,
	PFG_GRAIN      = 1u << 9,   // grainEnable=0 (call-site gate)
	PFG_VIGNETTE   = 1u << 10,
	PFG_IVGRADE    = 1u << 11,  // ivMode=0 (call-site gate)
	PFG_MOTIONBLUR = 1u << 12,
	PFG_HEIGHTFOG  = 1u << 13,
	PFG_GODRAYS    = 1u << 14,
	PFG_SMAA       = 1u << 15,
	PFG_FXAA       = 1u << 16,
};

static void PostfxGateSummary(unsigned skipped)
{
	if(!dbglog_throttle("pf_gate"))
		return;
	dbglog("[PF] pf_gate: mask=0x%05X normalBuf=%d vel=%d ssao=%d depthHist=%d pipeChain=%d sunShafts=%d dof=%d bloom=%d ca=%d grain=%d vignette=%d ivGrade=%d motionBlur=%d heightFog=%d godRays=%d smaa=%d fxaa=%d",
		skipped,
		(skipped & PFG_NORMALBUF) ? 1 : 0,
		(skipped & PFG_VELOCITY) ? 1 : 0,
		(skipped & PFG_SSAO) ? 1 : 0,
		(skipped & PFG_DEPTHHIST) ? 1 : 0,
		(skipped & PFG_PIPECHAIN) ? 1 : 0,
		(skipped & PFG_SUNSHAFTS) ? 1 : 0,
		(skipped & PFG_DOF) ? 1 : 0,
		(skipped & PFG_BLOOM) ? 1 : 0,
		(skipped & PFG_CA) ? 1 : 0,
		(skipped & PFG_GRAIN) ? 1 : 0,
		(skipped & PFG_VIGNETTE) ? 1 : 0,
		(skipped & PFG_IVGRADE) ? 1 : 0,
		(skipped & PFG_MOTIONBLUR) ? 1 : 0,
		(skipped & PFG_HEIGHTFOG) ? 1 : 0,
		(skipped & PFG_GODRAYS) ? 1 : 0,
		(skipped & PFG_SMAA) ? 1 : 0,
		(skipped & PFG_FXAA) ? 1 : 0);
}

// ---- test-4 Symptom B: L2/L3 render-state divergence detector -------------
// The corona_blend receipts (79/79 samples) show a persistent split: device
// ALPHABLENDENABLE=TRUE while the RW driver cache (L2, the RwD3D9SetRenderState
// shadow) holds FALSE. A cache-hit skip at L2 then leaves the device untouched
// — the same pipeEnterAlphaMode raw-write desync class the classify park
// (PipeChain_ClassifyBegin/End) already defends against. Audit of every
// blend/ZWRITE raw writer in THIS file found them all paired (SaveRawGeomStates
// /RestoreRawGeomStates brackets or explicit L2+L3 twins: Radiosity_VCS :846,
// Blur_VCS :988, classify :3492-:3638, pipe-chain pass-3 fade :3904-:3944,
// MotionBlur :4268, HeightFog :4494, DynamicSky :2922, NormalBuf :3216), so the
// creator lives outside postfx.cpp. This check detects the divergence at the
// postfx frame boundary and HEALS it by re-seeding L2 from the device truth
// (L3): visually a no-op (the device already holds the value), but it ends the
// persistent split so the next L1/L2 blend transition real-transitions instead
// of no-op'ing against a cache that no longer matches the device.
static void PostfxStateSyncCheck(const char *at)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	// test-7 WIDENED COVERAGE (2026-11-01): BOTH call sites (cf_enter :1672
	// and cf_end :2670) now run EVERY frame — the once-per-frame latch from
	// the perf audit is REMOVED. Evidence: the cf_enter "blend L2=1 L3=0
	// diverged — healed" receipts recur every ~1s, i.e. a persistent creator
	// outside postfx.cpp re-splits the layers every frame, and the latch left
	// mid-frame divergence windows unhealed for up to a full frame — black
	// light quads rasterise during those windows. With both sites active the
	// unhealed window halves: game-phase divergence is healed at cf_enter,
	// postfx-phase divergence at cf_end of the SAME frame (previously it
	// waited for the next frame's cf_enter).
	// Trade-off: 2 full sweeps/frame = 20 device Gets/frame (~30µs) —
	// acceptable now that diagnosis matters more than the microsecond cost.
	// POD statics only, no destructors near any __try — SEH-safe.

	static const struct { DWORD state; const char *name; } stChk[] = {
		{ D3DRS_ALPHABLENDENABLE, "blend"  },
		{ D3DRS_SRCBLEND,         "src"    },
		{ D3DRS_DESTBLEND,        "dst"    },
		{ D3DRS_BLENDOP,          "op"     },
		{ D3DRS_ZWRITEENABLE,     "zwrite" },
	};
	for(int i = 0; i < (int)(sizeof(stChk)/sizeof(stChk[0])); i++){
		DWORD l2 = 0, l3 = 0;
		RwD3D9GetRenderState(stChk[i].state, &l2);
		if(FAILED(dev->GetRenderState((D3DRENDERSTATETYPE)stChk[i].state, &l3)))
			continue;
		if(l2 == l3)
			continue;
		// Heal: L2 := L3. RwD3D9SetRenderState sees cache != target, writes
		// the device (the value it already holds) and re-seeds the cache —
		// all three layers agree again.
		RwD3D9SetRenderState(stChk[i].state, l3);
		// Log-hygiene: the heal above is functional and unconditional; only
		// the receipt is gated — first hit always logs (one-shot, never
		// starved), then diagLevel>=1 throttled (2s, ×5 at diagLevel 1,
		// silent at >=2).
		if(dbglog_once("statesync_first") || (g_diagLevel >= 1 && dbglog_throttle("statesync")))
			dbglog("[statesync] %s: %s L2=%u L3=%u diverged — healed (cache re-seeded to device truth)",
				at, stChk[i].name, (unsigned)l2, (unsigned)l3);
	}
}

// ---- test-4 Symptom A: auto-exposure luminance-chain watchdog -------------
// Dark-at-noon (F284810, game time 12:15): one frame renders dusk-dark while
// neighbours at 12:09-12:12 are bright. Gen-6 finding: a dead luminance chain
// makes RunFrameExposure (postfx_luminance.cpp) return NULL; the CF_Modern
// bind site (postfx_colourfilter.cpp ~:639 `if(autoExposureOn && frameLumaTex
// && cfDev)`) then skips the s1 bind WITHOUT any neutral-exposure fallback —
// TonemapPass samples whatever s1 still holds (a stale adapt texel from an
// earlier bright/HDR frame, or an unresolved RT) and key/lum clamps to the
// exposure-window edge -> sudden darkening. This watchdog detects the dead-
// chain precondition from the postfx side and emits the `[tonemap] lum
// fallback` receipt; the actual neutral fallback (force c5.w=0 so TonemapPass
// ignores s1 when frameLumaTex==NULL) must land at the bind site — hunk
// reported to the orchestrator (file outside this fix's edit set).
static void PostfxLuminanceChainWatch(int cfGate)
{
	if(!config->tonemapAutoExposure)
		return;
	if(cfGate != COLORFILTER_MODERN || !tonemapPassPS)
		return;
	if(luminanceReducePS && luminanceAdaptPS)
		return; // chain armed — RunFrameExposure can produce a luma tex
	// Log-hygiene: this watchdog is log-only; receipt gated — first hit
	// always logs (one-shot), then diagLevel>=1 throttled.
	if(dbglog_once("tonemap_lumfb_first") || (g_diagLevel >= 1 && dbglog_throttle("tonemap_lumfb")))
		dbglog("[tonemap] lum fallback: autoExposure=1 cfGate=MODERN but luminance chain DEAD "
			"(reducePS=%p adaptPS=%p tonemapPS=%p) — RunFrameExposure will return NULL, "
			"s1 stays unbound, tonemap samples stale/unresolved lum; neutral-exposure "
			"fallback required at the CF_Modern bind site",
			luminanceReducePS, luminanceAdaptPS, tonemapPassPS);
}

// ---- test-7: night-crush vs blend-blackout discriminator --------------------
// Extreme-darkness shots (game time 01:38-01:46, tonemapAutoExposure=1,
// min 0.5 / max 2.0): need to discriminate tonemap night-crush from the
// blend-blackout the statesync receipts track.
// HONEST REACHABILITY REPORT: the ADAPTED exposure value (the 1x1 adapt
// texel RunFrameExposure produces) is NOT reachable from postfx.cpp — it
// lives in file-static g_lumaAdaptTexA/B (postfx_luminance.cpp :13-16), a
// D3DPOOL_DEFAULT FP16 render target that cannot be locked without a
// staging-copy pipeline stall, and postfx_luminance.cpp is outside this
// edit set. The GPU-measured luminance is CPU-invisible by design
// (postfx_colourfilter.cpp :584-586 documents the same limitation).
// What IS reachable from here: the DEVICE-SIDE constant state CF_Modern
// uploaded earlier in this same call — read back via GetPixelShaderConstantF
// (same idiom as postfx.cpp:3633, mirrorPass.cpp:405; driver constant
// shadow, no stall) IMMEDIATELY after ColourFilter_Modern returns, before
// any later pass or the next frame's scene-phase PBR uploads clobber c5/c7:
//   c5 = {exposure, toeStrength, sceneLuma, autoExpFlag}   (colourfilter :542)
//   c7 = {blackLift, minExposure, maxExposure, keyStrength} (colourfilter :549)
// c5.w is the frameLumaTex-bound discriminator: the bind site re-uploads
// c5 with w=0 when RunFrameExposure returned NULL (colourfilter :639-643
// "lum fallback"), so w==1 means the adapt texture WAS bound this draw.
// Interpretation against the black-frame evidence:
//   c5.x pinned at/near kCFExposureFloor + dark sceneLuma -> tonemap
//        night-crush candidate (exposure collapsed);
//   c5.x sane + c5.w==1 + frame still black -> blend-blackout candidate
//        (cross-check the [statesync] heal receipts from the same log).
// Cross-check the upload-time [CF] cf_exp receipt (colourfilter :588, ~2s
// throttle) — if cf_exp and this readback disagree, a later pass clobbered
// the constants mid-chain (new diagnostic lead).
// Throttled ~2s via the global dbglog_throttle interval.
// (test-8 compile fix: takes the effective filter as a parameter — `colorFilter`
// is a LOCAL of ColourFilter_switch (postfx.cpp:1913), not a global; the call
// site inside the MODERN case passes it while it is in scope.)
static void PostfxExposureReceipt(int cfGate)
{
	if(!config->tonemapAutoExposure)
		return;
	if(cfGate != COLORFILTER_MODERN || !tonemapPassPS)
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	float c5[4] = { 0, 0, 0, 0 }, c7[4] = { 0, 0, 0, 0 };
	if(FAILED(dev->GetPixelShaderConstantF(5, c5, 1)) ||
	   FAILED(dev->GetPixelShaderConstantF(7, c7, 1)))
		return;
	if(dbglog_throttle("cf_expchk"))
		dbglog("[CF] expchk: c5={exp=%.4f toe=%.3f sceneLuma=%.4f frameLumaBound=%d} "
			"c7={blackLift=%.3f minExp=%.3f maxExp=%.3f key=%.3f} "
			"(adapt texel GPU-only — see postfx_luminance.cpp; frameLumaBound is a "
			"decoding key, not live state: 1 = adapt tex WAS bound this draw, "
			"0 = colourfilter's lum-fallback path re-uploaded c5 with w=0)",
			c5[0], c5[1], c5[2], c5[3] >= 0.5f ? 1 : 0,
			c7[0], c7[1], c7[2], c7[3]);
}

void
CPostEffects::ColourFilter_switch(RwRGBA rgb1, RwRGBA rgb2)
{
	DBGLOG_ENTER("ColourFilter_switch");
	PF_SCOPE("ColourFilter_switch");

	// pf_gate mask for this frame (see PostfxGateBits) — plain POD, reset
	// here so every path that returns below emits only this frame's gates.
	unsigned pfSkip = 0;

	// fix-30: start each frame's exposure handshake clean — BEFORE any gate/bail
	// below (pRasterFrontBuffer NULL bail included) and before CF_Modern's
	// tonemap sets it later in this same call.
	PostfxResetTonemapHandshake();

	// test-4 Symptom B: frame-entry L2/L3 divergence check. Coronas/particles
	// draw in the scene phase BEFORE this entry, so a divergence created out
	// there (the corona_blend dev=1/drv=0 signature) is still live here —
	// detect + heal it before any postfx pass (and before Radiosity, which
	// runs right behind this hook) consumes it with cache-skipped L2 writes.
	PostfxStateSyncCheck("cf_enter");

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

	// WHITE-INIT (call site 1 of 2 — the main path). Must run before ANY
	// colorfilterVerts consumer below (SSAO/pipe-chain composites, the bypass,
	// the colour-filter switch, IVGrade, GodRays, FXAA3, YCbCr). The bypass
	// branch further down has its own call because it can be reached from a
	// path where this one has already bailed on the pRasterFrontBuffer NULL
	// early-return.
	EnsureColorfilterVertsVisible();

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
		PF_SCOPE("NormalBuffer");
		if(dbglog_throttle("pf_normbuf"))
			dbglog("[PF] NormalBuffer: normEnable=%d out=g_normalBufferTex(%p) depth=g_ssaoDepthTex(%p)",
				config->normalBufferEnable, (void*)g_normalBufferTex, (void*)g_ssaoDepthTex);
		DrawNormalBufferToTexture();
		if(dbglog_throttle("pf_normbuf2"))
			dbglog("[PF] NormalBuffer done: RT restored, s0 unbound, states restored");
	}else{
		pfSkip |= PFG_NORMALBUF;
	}

	// Velocity buffer: per-pixel motion vectors via depth reconstruction
	// Must run before SSAO so the velocity texture is available for temporal reprojection
	// NOTE: call-site gate mirrors DrawVelocityBuffer's own first gate (it
	// allocates/recreates the velocity RT only AFTER that check) — skipping
	// the call here can never change output, it just avoids the entry work.
	if(config->velocityBufferEnable && VelocityReconstruct){
		PF_SCOPE("VelocityBuffer");
		if(dbglog_throttle("pf_vel"))
			dbglog("[PF] VelocityBuffer: velEnable=%d dvShader=%p velTex=%p depth=%p",
				config->velocityBufferEnable, VelocityReconstruct, g_velocityTex, g_ssaoDepthTex);
		DrawVelocityBuffer();
		if(dbglog_throttle("pf_vel2"))
			dbglog("[PF] VelocityBuffer done: RT back, s0/s1 unbound, states restored");
	}else{
		pfSkip |= PFG_VELOCITY;
	}

	// SSAO must run before color filter to read original scene
	// Gate BEFORE entering either callee: both DrawSSAO_Overhaul and
	// DrawSSAO bail on !ssaoEnable only after function entry, and the
	// overhaul path is the one that lazily creates the quarter/blur/output
	// rasters. ssaoEnable=0 => neither can contribute => whole dispatch off.
	if(config->ssaoEnable){
		PERF_SCOPE("SSAO");
		PF_SCOPE("SSAO_Dispatch");
		if(dbglog_throttle("pf_ssao"))
			dbglog("[PF] SSAO_Dispatch: ssaoEnable=%d SSAO_Temporal=%p temporal=%d blurPasses=%d -> %s",
				config->ssaoEnable, SSAO_Temporal, config->ssaoTemporalEnable,
				config->ssaoBlurPasses,
				(SSAO_Temporal &&
				 (config->ssaoTemporalEnable || config->ssaoBlurPasses > 0))
					? "DrawSSAO_Overhaul(qr+temporal+blur)" : "DrawSSAO(fullres)");
		if(SSAO_Temporal &&
		   (config->ssaoTemporalEnable || config->ssaoBlurPasses > 0)){
			DrawSSAO_Overhaul();    // Quarter-res SSAO (+temporal, +bilateral blur)
		}else{
			DrawSSAO();             // Fallback: old full-res SSAO (no blur)
		}
		if(dbglog_throttle("pf_ssao2"))
			dbglog("[PF] SSAO_Dispatch done: pass returned, depth hook state final");
	}else{
		pfSkip |= PFG_SSAO;
	}
	// Fine-grained bisect: covers BOTH the overhaul composite and the
	// full-res fallback. If this dump is the first black one, the AO
	// multiply composite is the culprit (noted in postfx_ssao.cpp:1038 —
	// the AO buffer can go near-black and the composite is a MULTIPLY).
	// Kept OUTSIDE the enable gate: an armed dump capture keeps the same
	// checkpoint sequence even when SSAO is disabled for the frame.
	DumpCurrentRT("cf_after_ssao");

	// Copy current depth to prev-depth history AFTER SSAO has consumed
	// the previous frame's depth, so temporal reprojection (T5) works correctly.
	// Perf gate (pf_gate): the history is only read by the SSAO temporal
	// resolve (c0.w historyBlend > 0 requires ssaoTemporalEnable — see
	// postfx_ssao.cpp:836 and SSAO_Temporal.hlsl:270) and by velocity
	// reconstruction (s1 prev-depth, c9.x). With neither consumer live the
	// full-res R32F create + full-screen depth copy is dead work: skip it and
	// invalidate the latch so a later toggle can never read stale history.
	bool prevDepthWanted = (config->velocityBufferEnable && VelocityReconstruct) ||
		(config->ssaoEnable && SSAO_Temporal && config->ssaoTemporalEnable);
	if(prevDepthWanted){
		CopyDepthToPrev();
	}else{
		g_prevDepthValid = false;
		pfSkip |= PFG_DEPTHHIST;
	}

	// 4-Pipe chain (after SSAO, uses normal buffer)
	if(config->pipeChainEnable && config->normalBufferEnable){
		PERF_SCOPE("PipeChain");
		PF_SCOPE("PipeChain");
		if(dbglog_throttle("pf_pchain"))
			dbglog("[PF] PipeChain: pcEnable=%d normEnable=%d normalTex=%p",
				config->pipeChainEnable, config->normalBufferEnable, (void*)g_normalBufferTex);
		DrawPipeChain();
		if(dbglog_throttle("pf_pchain2"))
			dbglog("[PF] PipeChain done: camera raster repainted from pRasterFrontBuffer");
		// Fine-grained bisect: after the pass-3 composite (the pass that
		// repaints the camera raster from pRasterFrontBuffer through the
		// colourfilterVerts quad — a black vertex colour lands exactly here).
		DumpCurrentRT("cf_after_pipechain");
	}else{
		pfSkip |= PFG_PIPECHAIN;
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

	// ---- Resolve the FINAL colour-filter consumer up front (Fix R1) ----
	// The PS2 conversion below is consumer-gated, so the PBR force +
	// vcsTrails remap (previously applied below the conversion) are hoisted
	// here. Behaviour is identical: the PBR force is idempotent and the
	// vcsTrails remap never yields MODERN, so every later test (the bypass
	// cfGate, the switch, the cf logs) sees the same value as before.
	int colorFilter = config->colorFilter;
	// PBR pipeline always uses Modern colour filter (Hable filmic tonemap in PostFX)
	if(config->pipeline == PIPELINE_PBR)
		colorFilter = COLORFILTER_MODERN;
	// VCS trails isn't compatible with PC/PS2 color filter, falls off to VCS color filter
	if (config->vcsTrails) {
		if (colorFilter == COLORFILTER_PC || colorFilter == COLORFILTER_PS2) {
			colorFilter = COLORFILTER_VCS;
		}
	}

	// Fix R1: the PS2 SoftCompress (x0.68) + MODULATE2X compensation (x0.5)
	// pre-scale exists for MOD2X-STYLE consumers only — consumers that double
	// the colour again at the draw: ColourFilter_PS2's fixed-function
	// D3DTOP_MODULATE2X (:2113) or vcTrailsPS's in-shader x2 (doublec +
	// twin-add). Vanilla consumers (None(0)/PC -> CPostEffects::ColourFilter
	// @0x703650) expect raw PC-space colours — the blanket x0.34 (including
	// the "Also fix PC variants" copy) made them ~3x too dark (worst
	// indoors). iiiTrailsPS (no doubling), Mobile (direction-only math) and
	// Modern (hue extract + luma clamp; its alpha/128 wants raw PS2 alpha)
	// are not MOD2X-style either and now receive the raw values as well.
	bool mod2xConsumer = colorFilter == COLORFILTER_PS2 ||
	                     colorFilter == COLORFILTER_VC  ||
	                     colorFilter == COLORFILTER_VCS;

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
	}else if(mod2xConsumer){
		// PS2 timecycle - convert to PC space (Fix R1: only for MOD2X-style
		// consumers; vanilla/None/PC consumers keep raw/PC-space values)
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

	{
		// Front-buffer sync is the group's input contract: SunShafts/DOF/Bloom
		// all read pRasterFrontBuffer. Establish/verify it once here so a
		// failed copy skips the whole group instead of letting each pass
		// consume the stale buffer. The group exit below still re-syncs (the
		// passes write the camera raster).
		bool fxGroupAny = config->sunShaftsEnable || config->dofEnable || config->bloomEnable;
		if(fxGroupAny){
			UpdateFrontBuffer();
		}
		if(fxGroupAny && !PfFrontBufferReady("pre-tonemap group")){
			pfSkip |= PFG_SUNSHAFTS | PFG_DOF | PFG_BLOOM;
		}else{
			{
				PERF_SCOPE("SunShafts");
				PF_SCOPE("SunShafts");
				if(dbglog_throttle("pf_fxgroup"))
					dbglog("[PF] FX pre-tonemap group: SunShafts->DOF->Bloom (gates: %d/%d/%d) in=cameraRT out=cameraRT",
						config->sunShaftsEnable, config->dofEnable, config->bloomEnable);
				DrawSunShafts();
			}
			{
				PERF_SCOPE("DOF");
				PF_SCOPE("DOF");
				DrawDOF();
			}
			{
				PERF_SCOPE("Bloom");
				PF_SCOPE("Bloom");
				DrawBloom();
			}
		}
	}
	UpdateFrontBuffer();

	// pf_gate: the pre-tonemap group self-gates on its enable key before its
	// first RT alloc (FxEnsureDofRTs / FxEnsureBloomRTs live below the gate),
	// so record the skips here — shared by the main and bypass paths.
	if(!config->sunShaftsEnable) pfSkip |= PFG_SUNSHAFTS;
	if(!config->dofEnable)       pfSkip |= PFG_DOF;
	if(!config->bloomEnable)     pfSkip |= PFG_BLOOM;

	// W7-D: underwater volume fog + SSS (pre-tonemap; self-gated, default-off)
	PostFX_UnderwaterTick();

	// Everything below (colour filter, lens group, IV grade, motion blur,
	// height fog, god rays, SSS blur) samples pRasterFrontBuffer. If the last
	// sync failed, the camera raster still holds the scene, so returning here
	// preserves a valid (if ungraded) frame instead of compositing stale black.
	if(!PfFrontBufferReady("colour filter chain")){
		pfSkip |= PFG_CA | PFG_GRAIN | PFG_VIGNETTE | PFG_IVGRADE |
		          PFG_MOTIONBLUR | PFG_HEIGHTFOG | PFG_GODRAYS;
		postfxReportFrame++;
		postfxReportSummary();
		PostfxGateSummary(pfSkip);
		return;
	}

	// test-4 Symptom A: luminance-chain watchdog. `colorFilter` already
	// carries the PBR->MODERN force (above), so this covers both the main
	// dispatch and the colorFilterEnable=0 bypass (which routes through the
	// same MODERN tonemap via cfGate).
	PostfxLuminanceChainWatch(colorFilter);

	// Debug toggle: bypass colour filter entirely
	if(!config->colorFilterEnable){
		if(dbglog_throttle("cf_switch"))
			dbglog("[PostFX] ColourFilter_switch BYPASSED (colorFilterEnable=0) rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d)",
				rgb1.red, rgb1.green, rgb1.blue, rgb1.alpha,
				rgb2.red, rgb2.green, rgb2.blue, rgb2.alpha);
		// WHITE-INIT (call site 2 of 2 — the bypass path). This branch is the
		// one that NEVER reaches CPostEffects::ColourFilter, i.e. the exact
		// case where the vanilla writer does not run and the verts would stay
		// black. Deliberately placed before the first UpdateFrontBuffer() so
		// the re-seed is in place before any consumer reads the verts.
		EnsureColorfilterVertsVisible();
		UpdateFrontBuffer();
		if(!PfFrontBufferReady("bypass identity tonemap")){
			pfSkip |= PFG_CA | PFG_GRAIN | PFG_VIGNETTE | PFG_IVGRADE;
			postfxReportFrame++;
			postfxReportSummary();
			PostfxGateSummary(pfSkip);
			return;
		}

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
			if(!PfFrontBufferReady("bypass lens group")){
				pfSkip |= PFG_CA | PFG_GRAIN | PFG_VIGNETTE | PFG_IVGRADE;
				postfxReportFrame++;
				postfxReportSummary();
				PostfxGateSummary(pfSkip);
				return;
			}
		}

		// New post-tonemap lens passes (W6 lane) — the bypass skips the
		// COLOUR FILTER only, so CA/grain/vignette still run here, in the
		// same order as the main path's post-switch group (and after the
		// identity tonemap above).
		{
			PERF_SCOPE("CA");
			PF_SCOPE("CA");
			if(config->caIntensity > 0.0f && PfFrontBufferValid()){
				DrawChromaticAberration();
			}else{
				pfSkip |= PFG_CA;   // DrawCA's own first gate; mirrors it
			}
		}
		{
			PERF_SCOPE("Grain");
			PF_SCOPE("Grain");
			// grainEnable is the MASTER (skygfx.h: "0=disable film grain";
			// the legacy Grain_PS2 lane gates on it too, postfx_colourfilter
			// .cpp:1019). DrawFilmGrain (postfx_newfx.cpp:737) only checks
			// grainIntensity, so without this call-site gate grainEnable=0
			// still ran the full pass. grainIntensity stays the strength.
			if(config->grainEnable && PfFrontBufferValid()){
				DrawFilmGrain();
			}else{
				pfSkip |= PFG_GRAIN;
			}
		}
		{
			PERF_SCOPE("Vignette");
			PF_SCOPE("Vignette");
			if(config->vignetteIntensity > 0.0f && PfFrontBufferValid()){
				DrawVignette();
			}else{
				pfSkip |= PFG_VIGNETTE; // DrawVignette's own first gate; mirrors it
			}
		}
		UpdateFrontBuffer();
		if(dbglog_throttle("pf_lens3"))
			dbglog("[PF] Lens group (bypass path) done: FB resynced");
		if(!PfFrontBufferReady("bypass IV grade / SSS")){
			pfSkip |= PFG_IVGRADE;
			postfxReportFrame++;
			postfxReportSummary();
			PostfxGateSummary(pfSkip);
			return;
		}

		// IV grade must still run when the colour filter is bypassed: this
		// path early-returns below, so it used to skip the only DrawIVGrade()
		// call (normal path, post-switch) and the iv* grade never applied.
		// Placement matches the normal path's ordering (IVGrade before SSS
		// blur). DrawIVGrade's own gate (config->ivMode) keeps it a no-op when
		// the grade is disabled, and it ends with its own UpdateFrontBuffer so
		// chars_drawSSSBlur reads the IV-graded frame.
		{
			PERF_SCOPE("IVGrade");
			if(config->ivMode && PfFrontBufferValid()){
				DrawIVGrade();
			}else{
				if(config->ivMode)
					PfFrontBufferReady("bypass IV grade");
				pfSkip |= PFG_IVGRADE; // DrawIVGrade's own first gate; mirrors it
			}
		}
		DumpCurrentRT("cf_b_ivgrade");

		// SSS still runs even when colour filter is bypassed - it's independent
		if(PfFrontBufferValid())
			chars_drawSSSBlur();
		else if(dbglog_throttle("pf_sss_skip"))
			dbglog("[PF] skip chars_drawSSSBlur: front buffer sync failed this frame");
		DumpCurrentRT("cf_b_sss");
		// Keep the per-frame report alive during bypass windows: this early
		// return used to skip postfxReportSummary (below), so the report went
		// silent exactly when the filter was bypassed — the "PostFX-REPORT
		// stops while rendering continues" symptom. NOTE: the
		// colorFilterEnable flag itself is owned by main.cpp (two call sites
		// alternate it) — reported to orchestrator, NOT fixed here.
		postfxReportFrame++;
		postfxReportSummary();
		PostfxGateSummary(pfSkip); // bypass path: CA/grain/vignette/IV + FX group gates
		return;
	}

	// (PBR force + vcsTrails remap hoisted above the PS2 conversion — Fix R1
	// resolves the final consumer before the conversion is gated on it.)

	if(dbglog_throttle( "cf_switch"))
		dbglog("[PostFX] ColourFilter_switch filter=%d pipeline=%d rgb1=(%d,%d,%d,%d) rgb2=(%d,%d,%d,%d) pRasterFrontBuffer=%p",
			colorFilter, config->pipeline,
			rgb1.red, rgb1.green, rgb1.blue, rgb1.alpha,
			rgb2.red, rgb2.green, rgb2.blue, rgb2.alpha,
			CPostEffects::pRasterFrontBuffer);

	switch(colorFilter){
	case COLORFILTER_NONE:
		// Fix R2 regression revert: None(0) with colorFilterEnable=1 must
		// still perform the vanilla full-screen composite.
		//
		// Vanilla CPostEffects::ColourFilter (0x703650, disassembled from the
		// exe) is the game's always-on final present-path draw:
		//   RwRenderStateSet(rwRENDERSTATETEXTURERASTER, pRasterFrontBuffer);
		//   colorfilterVerts[0..3].emissiveColor = rgb2; RwIm2DRenderPrimitive(...)
		//   colorfilterVerts[0..3].emissiveColor = rgb1; RwIm2DRenderIndexedPrimitive(...)
		// CPostEffects::Render (0x7046E0) already copied camRas -> FB in its
		// prologue (EndUpdate/PushContext/0x7faf50 RenderFast/Pop/BeginUpdate,
		// i.e. our UpdateFrontBuffer idiom) before reaching the 0x704D1E call
		// we replace, so this blit is the ONLY code in a vanilla frame that
		// re-presents the front buffer onto the camera raster. Making NONE a
		// true no-op removed the last unconditional full-screen draw of the
		// chain, so any earlier frame damage survives to the screen
		// un-repaired and nothing re-composites camRas.
		//
		// rgb1pc/rgb2pc stay RAW PC-space for NONE (Fix R1: None is not a
		// MOD2X-style consumer, so neither the SoftCompress nor the x0.5
		// compensation runs on it) — identical to what COLORFILTER_PC passes.
		// PBR never reaches here: the pipeline==PIPELINE_PBR force above
		// (line ~1271) switches the case to COLORFILTER_MODERN first.
		if(dbglog_throttle("cf_none"))
			dbglog("[PostFX] ColourFilter_switch NONE: vanilla composite (CPostEffects::ColourFilter) rgb1pc=(%d,%d,%d,%d) rgb2pc=(%d,%d,%d,%d)",
				rgb1pc.red, rgb1pc.green, rgb1pc.blue, rgb1pc.alpha,
				rgb2pc.red, rgb2pc.green, rgb2pc.blue, rgb2pc.alpha);
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
		if(!PfFrontBufferReady("ColourFilter_Modern")){
			// The grading/tonemap passes sample FB; the camera raster still
			// has the scene. Skip this branch and the tail below.
			break;
		}

		ColourFilter_Modern(rgb1, rgb2);
		// test-7: read back c5/c7 NOW — ColourFilter_Modern just uploaded
		// them (colourfilter :542/:574) and nothing has clobbered them yet.
		// Later in the frame the scene-phase PBR uploads and the postfx
		// passes reuse c0-c7, so a cf_end readback would be garbage; this
		// is the only postfx.cpp vantage where the readback is authoritative.
		// Bypass path (:2100, colorFilterEnable=0) intentionally not
		// instrumented — debug-only toggle, main dispatch is the evidence path.
		PostfxExposureReceipt(colorFilter);
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

	if(dbglog_throttle("pf_cf_branch"))
		dbglog("[PF] CF switch branch executed: colorFilter=%d (0=NONE 1=PS2 2=PC 3=MOBILE 6=III 7=VC 8=VCS 9=MODERN)",
			colorFilter);

	// ColourFilter_Modern performs its own mid-pass UpdateFrontBuffer (pass 1
	// grade -> FB, then tonemap reads FB). If that copy failed, every pass
	// below would sample the stale intermediate. Bail with the scene still on
	// the camera raster rather than compositing black.
	if(!PfFrontBufferReady("post-colour-filter lens group")){
		pfSkip |= PFG_CA | PFG_GRAIN | PFG_VIGNETTE | PFG_IVGRADE |
		          PFG_MOTIONBLUR | PFG_HEIGHTFOG | PFG_GODRAYS;
		postfxReportFrame++;
		postfxReportSummary();
		PostfxGateSummary(pfSkip);
		return;
	}

	// === New post-tonemap lens passes (W6 lane), in effect order:
	// CA -> grain -> vignette. Runs on the graded/tonemapped frame straight
	// out of the switch above (ColourFilter_Modern's tonemap is pass 4 of the
	// new chain; these are 5/6/7), before the IV grade / motion blur /
	// atmosphere tail. The legacy grainFilter path (CPostEffects::Grain /
	// Grain_PS2) is untouched. Self-gated per pass; group exit re-syncs FB.
	//
	// Entry sync: ColourFilter_Modern leaves FB holding the PRE-tonemap
	// graded frame. Verify the camera->FB copy here so a failed sync skips the
	// lens group instead of letting the first pass sample that stale frame.
	{
		bool lensAny = (config->caIntensity > 0.0f) || config->grainEnable ||
		               (config->vignetteIntensity > 0.0f);
		if(lensAny)
			UpdateFrontBuffer();
		if(lensAny && !PfFrontBufferReady("lens group")){
			pfSkip |= PFG_CA | PFG_GRAIN | PFG_VIGNETTE | PFG_IVGRADE |
			          PFG_MOTIONBLUR | PFG_HEIGHTFOG | PFG_GODRAYS;
			postfxReportFrame++;
			postfxReportSummary();
			PostfxGateSummary(pfSkip);
			return;
		}
	}
	{
		PERF_SCOPE("CA");
		PF_SCOPE("CA");
		if(dbglog_throttle("pf_lens"))
			dbglog("[PF] Lens group in: CA(int=%.3f) Grain(int=%.3f) Vignette(int=%.3f) in=cameraRT out=cameraRT",
				config->caIntensity, config->grainIntensity, config->vignetteIntensity);
		if(config->caIntensity > 0.0f && PfFrontBufferValid()){
			DrawChromaticAberration();
		}else{
			pfSkip |= PFG_CA;   // DrawCA's own first gate; mirrors it
		}
	}
	{
		PERF_SCOPE("Grain");
		PF_SCOPE("Grain");
		// grainEnable is the MASTER (skygfx.h: "0=disable film grain"; the
		// legacy Grain_PS2 lane gates on it too, postfx_colourfilter.cpp:1019).
		// DrawFilmGrain (postfx_newfx.cpp:737) only checks grainIntensity, so
		// without this call-site gate grainEnable=0 still ran the full pass.
		if(config->grainEnable && PfFrontBufferValid()){
			DrawFilmGrain();
		}else{
			pfSkip |= PFG_GRAIN;
		}
	}
	{
		PERF_SCOPE("Vignette");
		PF_SCOPE("Vignette");
		if(config->vignetteIntensity > 0.0f && PfFrontBufferValid()){
			DrawVignette();
		}else{
			pfSkip |= PFG_VIGNETTE; // DrawVignette's own first gate; mirrors it
		}
	}
	UpdateFrontBuffer();
	if(dbglog_throttle("pf_lens2"))
		dbglog("[PF] Lens group done: FB resynced (cameraRT -> pRasterFrontBuffer)");
	if(!PfFrontBufferReady("IV grade / motion blur tail")){
		pfSkip |= PFG_IVGRADE | PFG_MOTIONBLUR | PFG_HEIGHTFOG | PFG_GODRAYS;
		postfxReportFrame++;
		postfxReportSummary();
		PostfxGateSummary(pfSkip);
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
		PF_SCOPE("IVGrade");
		if(dbglog_throttle("pf_iv"))
			dbglog("[PF] IVGrade: ivMode=%d pipeline=%d cfEnable=%d identGrade=%d",
				config->ivMode, config->pipeline, (int)config->colorFilterEnable,
				(int)s_tonemapIdentityGrade);
		if(config->ivMode && PfFrontBufferValid()){
			DrawIVGrade();
		}else{
			if(config->ivMode)
				PfFrontBufferReady("IV grade");
			pfSkip |= PFG_IVGRADE; // DrawIVGrade's own first gate; mirrors it
		}
		if(dbglog_throttle("pf_iv2"))
			dbglog("[PF] IVGrade done: states restored (tex/z/fog/blend)");
		// Fine-grained bisect: the grade repaints the camera raster from the
		// IV frame copy through the colourfilterVerts quad.
		DumpCurrentRT("cf_m_ivgrade");
	}

	// Motion blur: after colour filter + SMAA, before final front buffer sync
	{
		PERF_SCOPE("MotionBlur");
		PF_SCOPE("MotionBlur");
		if(dbglog_throttle("pf_mb"))
			dbglog("[PF] MotionBlur: mbEnable=%d shader=%p velOn=%d strength=%.3f",
				config->motionBlurEnable, MotionBlur_Burnout, config->velocityBufferEnable,
				config->motionBlurStrength);
		if(PfFrontBufferValid())
			DrawMotionBlur();
		else
			pfSkip |= PFG_MOTIONBLUR;
		if(dbglog_throttle("pf_mb2"))
			dbglog("[PF] MotionBlur done: states restored");
	}

	// Atmospheric effects: after colour filter + motion blur, before front buffer sync
	{
		PERF_SCOPE("HeightFog");
		PF_SCOPE("HeightFog");
		if(dbglog_throttle("pf_hf"))
			dbglog("[PF] HeightFog: hfEnable=%d density=%.5f shader=%p depthTex=%p",
				config->heightFogEnable, config->heightFogDensity, HeightFog, g_ssaoDepthTex);
		if(PfFrontBufferValid())
			DrawHeightFog();
		else
			pfSkip |= PFG_HEIGHTFOG;
		if(dbglog_throttle("pf_hf2"))
			dbglog("[PF] HeightFog done: DepthHook_Restore + states restored");
	}
	{
		PERF_SCOPE("GodRays");
		PF_SCOPE("GodRays");
		// H1 fix: no caller-side blend claims. The old pre-call log printed
		// "blend=ONE/ONE(rw+raw)" unconditionally — even when godRaysEnable=0
		// gated the pass off before a single SetRenderState — and the paired
		// "done" line ran under an independent throttle tag, so set/restore
		// counts drifted apart (log-proven 5x vs 4x, session 05:55). The pass
		// itself now logs the blend SET only after all of its gates pass, and
		// gates the RESTORE log on that same set-log bool, so they cannot
		// unpair. When godRaysEnable=0 the only output is the throttled,
		// honest gate line inside DrawGodRays ("-> SKIP"); no state writes.
		if(PfFrontBufferValid())
			DrawGodRays();
		else
			pfSkip |= PFG_GODRAYS;
	}

	UpdateFrontBuffer();
	// SSS post-process blur: runs after colour filter
	// Reads from pRasterFrontBuffer (colour-filtered scene), writes to camera raster.
	if(PfFrontBufferValid())
		chars_drawSSSBlur();
	else if(dbglog_throttle("pf_sss_skip"))
		dbglog("[PF] skip chars_drawSSSBlur: front buffer sync failed this frame");
	DumpCurrentRT("cf_m_sss");

	if(dbglog_throttle("cf_done"))
		dbglog("ColourFilter_switch: done (filter=%d)", colorFilter);

	// pf_gate: passes below have no call-site gate of their own (their entry
	// gates already precede any RT/state setup — see DrawMotionBlur/
	// DrawHeightFog/DrawGodRays/DrawSMAA), so record the skips for the
	// per-frame summary. Pure reads; no behaviour change.
	if(!(config->motionBlurEnable && MotionBlur_Burnout))        pfSkip |= PFG_MOTIONBLUR;
	if(!(config->heightFogEnable && config->heightFogDensity > 0.0f && HeightFog && g_ssaoDepthTex)) pfSkip |= PFG_HEIGHTFOG;
	if(!(config->godRaysEnable && GodRays))                      pfSkip |= PFG_GODRAYS;
	if(!(config->smaaEnable && SMAA_Edge))                       pfSkip |= PFG_SMAA;
	if(!config->fxaaEnable || (config->smaaEnable && SMAA_Edge)) pfSkip |= PFG_FXAA;
	PostfxGateSummary(pfSkip);

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
	PF_SCOPE("DrawFinalEffects");
	if(dbglog_throttle("pf_dfe"))
		dbglog("[PF] DrawFinalEffects: smaaEnable=%d SMAA_Edge=%p fxaaEnable=%d",
			config->smaaEnable, SMAA_Edge, config->fxaaEnable);

	// --- On-demand dump arming (F10) -------------------------------------
	// Edge-detected: g_postfxDumpKeyPrevDown latches while the key is held, so a
	// single press arms exactly once (holding F10 must not re-arm every
	// frame). Polled here rather than in ColourFilter_switch because
	// DrawFinalEffects is the last per-frame call — arming here covers the
	// FULL next frame for every earlier checkpoint (scene_cam, fb_after_copy,
	// cf_after_ssao, cf_after_pipechain, ...) plus the rest of this one.
	// The plain dbglog (NOT dbglog_throttle) is deliberate: the user needs
	// one unambiguous "the key registered" line, and throttling would swallow
	// repeats.
	{
		const bool down = (GetAsyncKeyState(POSTFX_DUMP_HOTKEY) & 0x8000) != 0;
		const bool edge = down && !g_postfxDumpKeyPrevDown;
		g_postfxDumpKeyPrevDown = down;
		if(edge)
			PostFX_ArmDump(POSTFX_DUMP_ARM_FRAMES, "F10 hotkey");
		// Spend one armed frame for the frame we are currently in, so an arm
		// at frame N covers N (partial), N+1 and N+2.
		if(g_postfxDumpArmCount > 0)
			g_postfxDumpArmCount--;
	}
	DumpCurrentRT("dfe_enter");

	// SMAA: pure D3D9 RT switching — safe regardless of camera Begin/EndUpdate state.
	// Runs before ImGui so the debug menu stays on top (test requires menu open).
	// Sync front buffer first so HUD (already drawn into camera raster) survives
	// SMAA's full-frame repaint from pRasterFrontBuffer.
	if(config->smaaEnable && SMAA_Edge){
		if(dbglog_throttle("smaa_entry"))
			dbglog("[SMAA] DrawFinalEffects: entering SMAA (Edge=%p Temporal=%p)", SMAA_Edge, SMAA_Temporal);
		UpdateFrontBuffer();
		if(PfFrontBufferReady("SMAA"))
			DrawSMAA();
	}

	// FXAA3 (W6 lane, new-chain pass 8): same late AA slot as SMAA. Never
	// together with SMAA — DrawFXAA3 self-gates and, when both are enabled,
	// SMAA (above) wins and DrawFXAA3 logs the suppression exactly once.
	{
		PERF_SCOPE("FXAA3");
		PF_SCOPE("FXAA3_Slot");
		if(dbglog_throttle("pf_fxaa"))
			dbglog("[PF] FXAA3 slot: fxaaEnable=%d smaaWins=%d (DrawFXAA3 self-gates)",
				config->fxaaEnable, (int)(config->smaaEnable && SMAA_Edge ? 1 : 0));
		if(PfFrontBufferValid())
			DrawFXAA3();
		if(dbglog_throttle("pf_fxaa2"))
			dbglog("[PF] FXAA3 slot done: camera raster = final displayed surface");
	}

	// ImGui debug menu: always render last (on top of everything)
	{
		IDirect3DDevice9 *dev = d3d9device;
		if(dev && config->debugMenuOpen)
			DrawUnifiedDebugMenu(dev);
	}

	// Forward+ cluster/tile debug overlay (INI forwardPlusDebug; no-op at 0)
	ForwardPlus_DrawClusterDebug();

	// YCbCr filter (only when enabled)
	if(m_bYCbCrFilter){
		PF_SCOPE("YCbCrFilter");
		if(dbglog_throttle("pf_ycbcr"))
			dbglog("[PF] YCbCrFilter: ON ps=%p in=pRasterFrontBuffer out=cameraRaster", gradingPS);
		UpdateFrontBuffer();

		if(PfFrontBufferReady("YCbCr filter")){
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
		PfBindPS(gradingPS, "IDR_GRADINGPS(160) YCbCr");
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
	}

	// NOTE: PfFrame() (profiler frame boundary) lives in the DrawFinalEffects_pf
	// wrapper at the 0x53EBE9 hook (main.cpp) — exactly one snap per frame.
	// It used to live here too (fix-6 + fix-9 both landed it) => double timing.

	// End-of-frame raster checkpoint (black-screen bisect). Paired with
	// scene_cam (postfx.cpp PostFX_DumpSceneCamera, called from
	// RenderScene_after) and the fine-grained dfe_enter / cf_* probes, this
	// localises the black to exactly one stage:
	//   scene_cam black   -> the WORLD render produced black
	//   scene_cam fine +
	//   final_frame black -> something in the post chain painted it over
	//   cf_after_ssao / cf_after_pipechain / cf_m_ivgrade / cf_m_sss black
	//                       -> that specific pass is the one blackening it
	// lastFrame here is the surface the game presents next, i.e. the screen
	// the user actually sees.
	// Both are INI-gated on postfxDumpDebug; the first-N-frames cap is now
	// PostFX_DumpActive() so the F10 arming window bypasses it.
	DumpCurrentRT("final_frame");

	// One-line summary of everything captured this frame (only fires when at
	// least one BMP was actually written).
	PostFX_DumpFlushTags();

	// test-4 Symptom B: frame_end L2/L3 divergence check — catches a desync a
	// postfx pass created THIS frame (the cf_enter check catches game-phase
	// divergence). Runs after the dump probes, before the frame is presented.
	// Early-return paths above skip this check; a persistent divergence is
	// still caught by the next frame's cf_enter check.
	PostfxStateSyncCheck("cf_end");

	// SMAA runs at top of DrawFinalEffects (before ImGui) via D3D9 SetRenderTarget.
}



// IBL buffer (quarter-res sky/cloud ambient)
IDirect3DTexture9 *g_iblTex = NULL;
static IDirect3DSurface9 *g_iblSurf = NULL;

extern void *DynamicSky;

// Normal buffer (half-res stereo-derived normals)

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

	// New postfx scratch chains: DOF half-res pair + bloom mip pyramid
	// (D3DPOOL_DEFAULT, recreated lazily on the next pass)
	ReleaseFxScratchResources();

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

// Central cleanup on device reset (called from depthhook::hook_Reset)
extern void ReleaseEnvMapResources(void);
extern void ReleaseVehiclePipeCaches(void);
extern void ReleaseBuildingPipeCaches(void);
extern void ReleaseNeoCarPipeResources(void);
extern void InvalidateCameraRasterCache(void);	// main.cpp

void OnDeviceReset(void)
{
	// Water pipe RTs (scene colour / R32F depth / reflection) are
	// D3DPOOL_DEFAULT and created lazily at tier>=2 — a live one fails the
	// Reset with D3DERR_INVALIDCALL (the 0x7F9A3C NULL-deref crash class).
	// Release them FIRST so no later step can observe them alive; they
	// recreate lazily on the next tier>=2 flush (skygfx.h declaration).
	waterPipe_OnDeviceReset();

	// Release all D3DPOOL_DEFAULT resources (including luminance targets)
	ReleaseDefaultPoolResources();

	// Release RW-managed caches that survive device reset
	ReleaseEnvMapResources();
	ReleaseVehiclePipeCaches();
	ReleaseBuildingPipeCaches();
	ReleaseNeoCarPipeResources();

	// chars module: release the raw D3DPOOL_DEFAULT classify texture (NOT an
	// RW raster — RW's video-memory restore list never sees it). If it is
	// alive during the game's Reset, Reset fails D3DERR_INVALIDCALL and the
	// vanilla RW restore path does an unchecked Release(NULL) on
	// _RwD3D9RenderSurface (0xC97C30) → 0xC0000005 at 0x7F9A3C.
	chars_OnDeviceReset();

	// Peds pipe latch invalidation (chars_OnDeviceReset precedent, chars.h:49):
	// Phase 1 owns no device objects — bumps the generation counter, logs once
	// (dbglog_once "peds_reset"). Phase 2+ sub-modules invalidate cached device
	// pointers here and rebuild lazily (EnsureSSSRasters pattern).
	PedsPipe_OnDeviceReset();

	// Scene-entry camera-raster repair cache (main.cpp s_camRasGame): RW
	// destroys + recreates the game's camera raster across Reset, so the
	// cached pointer dangles — and the repair branch only runs when a
	// NON-screen raster is live, i.e. exactly when the screen-sized
	// re-adoption branch cannot refresh it. Drop it here (hook_Reset calls
	// OnDeviceReset BEFORE orig_Reset, so this lands before the teardown);
	// the next screen-sized sighting re-adopts and re-arms. No-op when the
	// cache is already empty.
	InvalidateCameraRasterCache();

	// Reset guard state: never leave the VEH in silent pass-through mode or
	// with a stale phase tag across a device Reset (gap: previously the
	// hoisted DrawSMAA guard flag survived reset).
	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	g_guardDepth = 0;
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
	PF_SCOPE("IBLBuffer");
	static int iblLogged = 0;
	if(dbglog_throttle("pf_ibl"))
		dbglog("[PF] IBLBuffer: DynamicSky=%p out=g_iblTex(%p) qres quarter-screen",
			DynamicSky, (void*)g_iblTex);
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

	// SEH-guarded (guardedIm2DRender) so a fault can't leave the DynamicSky
	// override bound: Im2dSetPixelShader_hook would re-apply it to every
	// later Im2D dispatch, including coronas.
	PfBindPS(DynamicSky, "IDR_DYNAMICSKY(209)");
	PfDraw(4, 2);
	guardedIm2DRender(DynamicSky, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "dynamicsky");

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

// ============================================================
// BUG 1a — CPU-side screen-edge fade for the PipeChain reflection weight.
// pass_output's reflBlend is proportional to pack.y*pack.z (gloss*spec), so
// multiplying the classify pack by this CPU-built mask fades the reflection
// contribution to 0 as the reflection UVs approach the screen edges — smooth
// smoothstep fade, margin 0.075 — WITHOUT touching PipeChain.hlsl (the true
// per-pixel envUV clamp/reflect + edge fade belongs in the shader's
// pass_output; shaders are off-limits, see report). Fading the weights also
// kills the black pools: near-edge rays used to fetch the 2048² front-buffer
// pad (unwritten = black) or wrap across the raster border (ghost double
// image) — those samples now carry weight 0. 128² A8R8G8B8, filled CPU-side
// once: rgb = w(u)*w(v), alpha = 1 (pack.a/metal survives the multiply).
// Applied by the fixed-function multiply draw in DrawPipeChain pass 3
// (Grain_PS2's D3DBLEND_DESTCOLOR pattern in postfx_colourfilter.cpp).
// ============================================================
static const float kPipeChainReflFadeMargin = 0.075f; // edge fade width (0.05-0.1 spec)
static RwRaster *s_pipeChainFadeRas = nil;

static float
PipeChainEdgeFade1D(float t)
{
	// w(t) = smoothstep(0,m,t) * smoothstep(0,m,1-t): 0 at both screen
	// edges, 1 across the interior [m, 1-m].
	float m = kPipeChainReflFadeMargin;
	float a = t / m;          if(a < 0.0f) a = 0.0f; else if(a > 1.0f) a = 1.0f;
	float b = (1.0f - t) / m; if(b < 0.0f) b = 0.0f; else if(b > 1.0f) b = 1.0f;
	a = a * a * (3.0f - 2.0f * a);
	b = b * b * (3.0f - 2.0f * b);
	return a * b;
}

static RwRaster*
GetPipeChainEdgeFadeRaster(void)
{
	if(!s_pipeChainFadeRas){
		s_pipeChainFadeRas = RwRasterCreate(128, 128, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
		if(!s_pipeChainFadeRas)
			return nil;
		RwUInt8 *px = RwRasterLock(s_pipeChainFadeRas, 0, 1);
		if(px){
			RwInt32 stride = s_pipeChainFadeRas->stride;
			for(int j = 0; j < 128; j++){
				float wv = PipeChainEdgeFade1D(((float)j + 0.5f) / 128.0f);
				RwUInt8 *row = px + (RwInt32)j * stride;
				for(int i = 0; i < 128; i++){
					float f = PipeChainEdgeFade1D(((float)i + 0.5f) / 128.0f) * wv;
					RwUInt8 b = (RwUInt8)(f * 255.0f + 0.5f);
					row[i*4 + 0] = b;
					row[i*4 + 1] = b;
					row[i*4 + 2] = b;
					row[i*4 + 3] = 0xFF; // alpha 1: pack.a survives the multiply
				}
			}
			RwRasterUnlock(s_pipeChainFadeRas);
		}
		dbglog("[PipeChain] edge-fade mask created %p margin=%.3f",
			s_pipeChainFadeRas, kPipeChainReflFadeMargin);
	}
	return s_pipeChainFadeRas;
}

void DrawNormalBufferToTexture(void)
{
	// No PF_SCOPE here: this function contains __try, and MSVC /EHsc forbids
	// destructible locals (C2712). Instrumentation lives in the throttled
	// dbglog lines below + the caller's NormalBuffer scope.
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

	// SEV2: this pass raw-binds/raw-unbinds stage 0 (dev->SetTexture), which
	// bypasses the value-compare L1 rw cache. Capture BOTH layers of the entry
	// stage-0 texture here: the rw raster (L1) and the raw device texture (L3,
	// GetTexture AddRefs -> released on every exit path below). Without the L1
	// re-sync at the exit the cache keeps the pre-pass raster while the device
	// samples NULL, so a later RwRenderStateSet(<same raster>) is a double
	// no-op and the surface loses its texture (black / normal-less).
	void *saved0 = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &saved0);
	IDirect3DBaseTexture9 *saved0Dev = NULL;
	dev->GetTexture(0, &saved0Dev);

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
	if(!camRas){ dev->SetRenderTarget(0, oldRT); dev->SetDepthStencilSurface(oldDS); if(oldRT) oldRT->Release(); if(oldDS) oldDS->Release(); if(saved0Dev){ saved0Dev->Release(); saved0Dev = NULL; } return; }
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
		if(dbglog_throttle("pf_normbuf_bind"))
			dbglog("[PF] NormalBuffer bind: ps=%p(IDR_NORMALBUFFERPS 143) s0=g_ssaoDepthTex out=%dx%d",
				NormalBufferShader, nbW, nbH);
		PfBindPS(NormalBufferShader, "IDR_NORMALBUFFERPS(143)");
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

	// Cleanup. SEV2: the raw NULL below used to desync L1 from the device —
	// the cache still held the pre-pass raster, so a later
	// RwRenderStateSet(TEXTURERASTER, <same raster>) was a value-compare no-op
	// while stage 0 sampled NULL. Sync L1 to the raw NULL through a
	// park-and-set (park != cached value, so each Set is a real transition),
	// then re-establish the entry raster through L1 (park != entry) and
	// raw-pin the entry device texture so L1/L3 end on the same surface.
	dev->SetTexture(0, NULL);
	{
		RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
		RwRaster *park = ((RwRaster*)saved0 == whiteRas) ? NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		park = ((RwRaster*)saved0 == whiteRas) ? NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, saved0);
	}
	{
		IDirect3DBaseTexture9 *resolved0 = NULL;
		dev->GetTexture(0, &resolved0);	// AddRef
		if((void*)resolved0 != (void*)saved0Dev
		|| (saved0 != NULL && resolved0 == NULL)){
			if(dbglog_throttle("pf_normbuf_s0_restore"))
				dbglog("[PF] NormalBuffer s0 restore: raster=%p entryDevTex=%p resolvedDevTex=%p (mismatch, raw-pinning entry)",
					saved0, (void*)saved0Dev, (void*)resolved0);
		}
		if(saved0Dev)
			dev->SetTexture(0, saved0Dev);	// raw-pin L3 to the entry texture
		if(resolved0) resolved0->Release();
		if(saved0Dev){ saved0Dev->Release(); saved0Dev = NULL; }
	}

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
// BUG-2: exact pre-bracket RAW device values (L3 restore targets in End —
// the vtxAlpha-derived guess could not reproduce a desynced device state).
static DWORD s_classifyBlendDev = 0;
static DWORD s_classifyFogDev = 0;
static DWORD s_classifyZWriteDev = 0;
static int s_classifyBegins = 0; // PipeChain_ClassifyBegin successes this frame (grab+reset at DrawPipeChain entry)
// Pre-bracket PS constants c0/c1: ClassifyBegin uploads flagP/packP (raw +
// RW); without a save/restore the device keeps c0=flagP/c1=packP after the
// bracket — the next RW consumer only re-synced by coincidence of encoding.
// Saved raw in Begin, raw-restored in End (the raw restore must defeat the
// RW PS-constant cache; the next RW consumer re-syncs the cache via its own
// uploads). POD-only: touched inside the SEH fence.
static float s_savedC0[4];
static float s_savedC1[4];
static bool s_savedC01Valid = false;

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
	// Edge-fade mask (BUG 1a) rides the same Reset lifecycle — lazily
	// recreated by GetPipeChainEdgeFadeRaster on next use.
	if(s_pipeChainFadeRas){ RwRasterDestroy(s_pipeChainFadeRas); s_pipeChainFadeRas = nil; }
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
	// the consumer draws with.
	// pf_gate: if no trusted size has been captured yet, DEFER creation.
	// The old live-read fallback is exactly what created this RT at
	// 2048x1024 in the geometry phase, only for DrawPipeChain to destroy,
	// recreate and clear it at 1920x1080 later the SAME frame. Returning
	// NULL here makes that frame's classify draws no-op (reflection off for
	// one frame); DrawPipeChain captures the size first and re-enters this
	// function with a valid cache, so steady-state is unchanged.
	int w = 0, h = 0;
	if(!GetScreenSize(&w, &h))
		return NULL;
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
void PipeChain_ClassifyEnd(void); // Begin's fault path closes its own bracket

bool PipeChain_ClassifyBegin(float surfaceType, float gloss, float spec, float metal)
{
	// HIGH-4: match the consumer dispatch gate (postfx ~:2624 pipeChainEnable
	// && normalBufferEnable) — without normalBufferEnable the classify pack
	// was written but DrawPipeChain never consumed it (wasted RT + second
	// draw per mesh).
	// pf_gate: additionally require pipeChainReflEnable. DrawPipeChain pass 3
	// binds the 1x1 pack0 dummy (never this pack) when reflection is off, so
	// the whole second draw per mesh + classify RT write would be dead work.
	if(!config || !config->pipeChainEnable || !config->normalBufferEnable ||
	   !config->pipeChainReflEnable || !PipeChainShader)
		return false;
	// Cutscene≠menu: the classify producer must match its consumer dispatch —
	// DrawPipeChain runs un-gated from ColourFilter_switch, so skipping only
	// the pack during cutscenes left the consumer reading a stale RT.
	if(gRenderingSpheremap || PostfxMenuPauseOnly())
		return false;
	// BUG-1b validity gate (cheap, CPU-side): pass 3's weight is
	// brdfW = saturate(pack.z*1.6)*pack.y — NaN/garbage gloss/spec sailed
	// through the clamps below (NaN compares false both ways, so the old
	// ternaries passed it straight into c1) and burnt ink-blot reflections
	// into glossy-classified surfaces. Anything outside (0, 1e4) — NaN
	// included — DISCARDS the reflection: no pack write, pack stays 0.
	if(!(gloss > 0.0f && gloss < 1e4f) || !(spec > 0.0f && spec < 1e4f)){
		if(dbglog_throttle("pc_badpack"))
			dbglog("[PipeChain] classify rejected (NaN/garbage gate): gloss=%.6g spec=%.6g",
				gloss, spec);
		return false;
	}
	// BUG-2 belt: a swallowed SEH fault in a previous classify draw can skip
	// End entirely — close that leaked bracket (full 3-layer blend/RT
	// restore) before opening a new one instead of overwriting its saves.
	if(s_classifyOldRT)
		PipeChain_ClassifyEnd();
	static bool s_csFired_classify = false;
	if(CCutsceneMgr__ms_running)
		CutscenePostfxEnabledLog("PipeChainClassify", &s_csFired_classify);
	IDirect3DSurface9 *classifySurf = GetPipeChainClassifySurf();
	if(!classifySurf || !d3d9device)
		return false;

	// SAVE EVERYTHING before mutating (so the fault path restores the exact
	// pre-bracket state): RT + RW cache (L1) + the RAW device values (L3 —
	// the restore in End must reproduce these, not a vtxAlpha-derived guess)
	// + PS constants c0/c1 (the bracket uploads flagP/packP; without the
	// save the device would keep them after End — the next RW consumer only
	// re-synced by coincidence of encoding).
	s_classifyOldRT = NULL;
	d3d9device->GetRenderTarget(0, &s_classifyOldRT);
	if(!s_classifyOldRT)
		return false;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s_classifyVtxAlpha);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &s_classifyFog);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s_classifyZWrite);
	s_classifyBlendDev = 0;
	s_classifyFogDev = 0;
	s_classifyZWriteDev = 0;
	d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s_classifyBlendDev);
	d3d9device->GetRenderState(D3DRS_FOGENABLE, &s_classifyFogDev);
	d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &s_classifyZWriteDev);
	s_savedC01Valid =
		SUCCEEDED(d3d9device->GetPixelShaderConstantF(0, s_savedC0, 1)) &&
		SUCCEEDED(d3d9device->GetPixelShaderConstantF(1, s_savedC1, 1));

	// SEH fence (POD locals only — C2712): any fault inside the mutation
	// window below closes the bracket via End (3-layer restore + RT) and
	// fails closed = no pack this mesh.
	// c0.x = 9 selects pack mode in PipeChain.hlsl; c1 = pack floats.
	// NaN-safe clamps (`x != x` is the NaN test — the old `< 0`/`> 1`
	// ternaries let NaN through into the pack).
	// Hoisted ABOVE the SEH fence: pure float math, no device state, and the
	// post-fence pc_c1chk diagnostic must be able to read packP (names inside
	// the __try block go out of scope at its end).
	float flagP[4] = { 9.0f, 0.0f, 0.0f, 0.0f };
	float cl = (surfaceType != surfaceType) ? 0.0f : (surfaceType < 0.0f ? 0.0f : (surfaceType > 255.0f ? 255.0f : surfaceType) / 255.0f);
	float gl = (gloss != gloss) ? 0.0f : (gloss < 0.0f ? 0.0f : (gloss > 1.0f ? 1.0f : gloss));
	float sp = (spec  != spec)  ? 0.0f : (spec  < 0.0f ? 0.0f : (spec  > 1.0f ? 1.0f : spec));
	float mt = (metal != metal) ? 0.0f : (metal < 0.0f ? 0.0f : (metal > 1.0f ? 1.0f : metal));
	float packP[4] = { cl, gl, sp, mt };
	__try{
		d3d9device->SetRenderTarget(0, classifySurf);

		// Pack must arrive unblended, unfogged, no depth write — forced
		// through ALL THREE layers: L1 RwRenderStateSet, L2 RwD3D9SetRenderState,
		// L3 raw device (pipeEnterAlphaMode raw writes can desync the caches,
		// and a cache-hit skip at L2 leaves the pack draw blended).
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		// RAW FIRST, then RW: HeightFog/GodRays/underwater/mirror restore
		// c0-c7 via the RAW device (postfx.cpp:3957/:4002,
		// postfx_underwater.cpp:502/:528, mirrorPass.cpp:232), which desyncs
		// RW's PS-constant cache exactly like the documented render-state
		// cache skip. A classify c0/c1 upload equal to RW's stale cached
		// value is then SKIPPED and the pack draw runs on the raw-restored
		// garbage c1 (often vehicle fxParams — chrome-like gloss/spec) ->
		// full-strength ink-blot reflection pools on repeated-material
		// meshes (whole road segments share one BRDF). The raw write always
		// reaches the device; the following RW write re-syncs the cache so
		// later RW consumers of c0/c1 can't inherit the desync.
		d3d9device->SetPixelShaderConstantF(0, flagP, 1);
		d3d9device->SetPixelShaderConstantF(1, packP, 1);
		RwD3D9SetPixelShaderConstant(0, flagP, 1);
		RwD3D9SetPixelShaderConstant(1, packP, 1);
		RwD3D9SetPixelShader(PipeChainShader);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("pc_classifybegin"))
			dbglog("[PipeChain] ClassifyBegin fault 0x%08X — closing bracket", GetExceptionCode());
		PipeChain_ClassifyEnd(); // full 3-layer restore + RT on the fault path
		return false;
	}
	s_classifyBegins++; // diagnostic: proves classify landed this frame
	// pc_c1chk: read the device constants BACK and compare with what we
	// intended to upload — now ALL FOUR c1 components (c1[0]=surfaceType,
	// c1[3]=metallic included; a partial compare could miss a raw writer
	// that only clobbers those). A mismatch proves the RW PS-constant
	// cache desync (raw SetPixelShaderConstantF restores) was eating classify
	// uploads — the ink-blot pool root cause. After the raw+RW double-upload
	// above this must always report "ok"; a mismatch line means a NEW raw
	// writer appeared between our upload and the draw. The check runs every
	// draw; only the OK line is throttled — an error signal must not be.
	float chkC0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float chkC1[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	bool chkOK =
		SUCCEEDED(d3d9device->GetPixelShaderConstantF(0, chkC0, 1)) &&
		SUCCEEDED(d3d9device->GetPixelShaderConstantF(1, chkC1, 1)) &&
		chkC0[0] == 9.0f &&
		chkC1[0] == packP[0] && chkC1[1] == packP[1] &&
		chkC1[2] == packP[2] && chkC1[3] == packP[3];
	if(chkOK){
		if(dbglog_throttle("pc_c1chk"))
			dbglog("[PipeChain] c1chk ok: c0.x=9 c1=(%.4f,%.4f,%.4f,%.4f) device matches upload",
				chkC1[0], chkC1[1], chkC1[2], chkC1[3]);
	}else
		dbglog("[PipeChain] c1chk MISMATCH: intended c1=(%.4f,%.4f,%.4f,%.4f) got c0=(%.4f,%.4f,%.4f,%.4f) c1=(%.4f,%.4f,%.4f,%.4f)",
			packP[0], packP[1], packP[2], packP[3],
			chkC0[0], chkC0[1], chkC0[2], chkC0[3],
			chkC1[0], chkC1[1], chkC1[2], chkC1[3]);
	return true;
}

void PipeChain_ClassifyEnd(void)
{
	if(!s_classifyOldRT)
		return;
	// BUG-2: complete the blend restore across ALL THREE layers. L1 cache +
	// L2 wrapped run fault-guarded so a swallowed SEH inside the RW
	// wrappers cannot skip the L3 raw completion below — the L3 raw write
	// is the missing piece: RwD3D9SetRenderState is cache-skipped when the
	// RW cache already holds the target value, and pipeEnterAlphaMode-style
	// raw writes desync that cache — the device then stayed blend-off for
	// the next consumer (vanilla coronas drew opaque quads).
	__try{
		// Restore RW cache first, then the wrapped/raw mirrors to the EXACT
		// pre-bracket device values saved by Begin (not a vtxAlpha-derived
		// guess — the two can legitimately disagree).
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, s_classifyVtxAlpha);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, s_classifyFog);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, s_classifyZWrite);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s_classifyBlendDev);
		RwD3D9SetRenderState(D3DRS_FOGENABLE, s_classifyFogDev);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s_classifyZWriteDev);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("pc_classifyend"))
			dbglog("[PipeChain] ClassifyEnd L1/L2 fault 0x%08X — L3 raw still restores",
				GetExceptionCode());
	}
	// L3 RAW completion (unconditional — outside the SEH scope above):
	// bypasses every cache layer, so the blend state lands on the device no
	// matter what the caches believe.
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s_classifyBlendDev);
	d3d9device->SetRenderState(D3DRS_FOGENABLE, s_classifyFogDev);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, s_classifyZWriteDev);
	// L3 RAW PS-constant restore (c0/c1 saved in Begin): must go through the
	// raw device — a RW cache-hit skip would leave the bracket's flagP/packP
	// live on the device. The next RW consumer of c0/c1 re-syncs the cache
	// via its own uploads.
	if(s_savedC01Valid){
		d3d9device->SetPixelShaderConstantF(0, s_savedC0, 1);
		d3d9device->SetPixelShaderConstantF(1, s_savedC1, 1);
		s_savedC01Valid = false;
	}
	d3d9device->SetRenderTarget(0, s_classifyOldRT);
	s_classifyOldRT->Release();
	s_classifyOldRT = NULL;
	// NOTE: caller re-sets its own pixel shader (Begin forced PipeChainShader).
}

// Frame handshake for the colour filter's soft-knee composite-gain gate
// (postfx_colourfilter.cpp): TRUE only on frames where DrawPipeChain pass 3
// bound the REAL classify pack (g_pipeChainClassifyTex — not the pack0
// NoPackTex dummy) on s4, i.e. the chain actually composited real reflection
// energy this frame. Reset at DrawPipeChain entry, set at the s4 bind site.
static bool s_cfPipeChainReflRan = false;

bool PostfxPipeChainReflRanFrame(void)
{
	return s_cfPipeChainReflRan;
}

void DrawPipeChain(void)
{
	// No PF_SCOPE here (__try in function -> C2712); see DrawNormalBufferToTexture.
	// Instrumentation: throttled dbglog lines below + caller's PipeChain scope.
	// Grab-and-reset BEFORE any gate: geometry classify runs earlier in the
	// frame than this postfx entry, so this snapshot is "begins this frame".
	// (Resetting inside the throttled pc_ok log accumulated across many
	// frames — the 43503 vs 635 mismatch.)
	// Reflection handshake reset (before any bind decision): the colour
	// filter's gain knee reads this via PostfxPipeChainReflRanFrame().
	s_cfPipeChainReflRan = false;
	int classifyBegins = s_classifyBegins;
	s_classifyBegins = 0;
	// BUG-2 belt: close any classify bracket a swallowed geometry-phase fault
	// left open (skipped End) before this chain touches device state — the
	// leak would otherwise reach every later consumer (coronas) and the
	// next frame's geometry.
	if(s_classifyOldRT)
		PipeChain_ClassifyEnd();

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
	// Normal-buffer CONTENT gate (POD int — DrawPipeChain has __try at :3191,
	// so no non-POD locals allowed). g_normalBufferHasContent (postfx.h:148)
	// is set only when DrawNormalBufferToTexture actually filled the buffer
	// this frame (:2819). A present-but-empty / stale / half-res normal buffer
	// still passes the shader's nlen gate and scatters reflection UVs into
	// dark regions -> black ink-blot pools. Do NOT return here: the
	// blur/contrast chain must keep running. Just mark the reflection HALF
	// dead so pass 3 binds the pack0 dummy on s4 (same kill as the
	// pipeChainReflEnable switch at the s4 pack bind).
	int normalBufDead = 0; // 1 = skip the pass-3 reflection term this frame
	if(!g_normalBufferHasContent){
		normalBufDead = 1;
		if(dbglog_throttle("pc_nbdead"))
			dbglog("[PipeChain] normal buffer has no content — reflection half off (blur/contrast on)");
	}
	if(!g_ssaoDepthTex){
		if(dbglog_throttle("pc_g3")) dbglog("[PipeChain] skip: no depth texture");
		return;
	}
	// pf_gate: input/device gates are evaluated BEFORE the first ping-pong RT
	// allocation below — a NULL scene source / camera raster used to allocate
	// the two screen-size RTs and then bail, leaving those renders unused for
	// the frame (allocation is lazy and stays for later frames, but the
	// create/driver work was wasted on this one).
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(!camRas){
		if(dbglog_throttle("pc_g5"))
			dbglog("[PipeChain] skip: camRas NULL");
		return;
	}
	// Pass 0-3 read pRasterFrontBuffer as scene source — without it the whole
	// chain (and env sampling) runs on a never-written padded raster.
	if(!CPostEffects::pRasterFrontBuffer){
		if(dbglog_throttle("pc_g6")) dbglog("[PipeChain] skip: pRasterFrontBuffer NULL");
		return;
	}
	// Second trusted capture point (postfx time, main camera on-screen) —
	// refreshes the cache the geometry-phase classify RT sizes itself from.
	CaptureScreenSize(camRas);

	IDirect3DTexture9 *texA = GetPipeChainTexture(0);
	IDirect3DTexture9 *texB = GetPipeChainTexture(1);
	if(!texA || !texB || !g_pipeChainSurfA || !g_pipeChainSurfB){
		if(dbglog_throttle("pc_g4")) dbglog("[PipeChain] skip: ping-pong RTs unavailable");
		return;
	}

	// Sync the front buffer BEFORE the chain samples it: passes 0-2 read
	// pRasterFrontBuffer as the scene source, and a stale/never-written
	// front buffer feeds the whole chain (including the classify composite)
	// junk — the other front-buffer consumers (motion blur, height fog)
	// already re-sync at their entry points.
	CPostEffects::UpdateFrontBuffer();
	if(!PfFrontBufferReady("pipe chain"))
		return;

	// Force-create the classify RT now (it was lazily created only inside
	// PipeChain_ClassifyBegin, so frames with zero classify draws reached
	// pass 3 with s4 unbound). Creation clears to pack 0 = no reflection.
	// pf_gate: only when the reflection half is ON. With pipeChainReflEnable=0
	// pass 3 binds the 1x1 pack0 dummy on s4 and never samples this RT, so
	// creating it (and the pass-3 edge-fade multiply that targets it) is
	// dead work; the geometry producer gate below skips the classify draws too.
	if(config->pipeChainReflEnable)
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

			// Stage-0 bind: park-and-set (same class as the MotionBlur park).
			// The previous form raw-cleared the device (dev->SetTexture(0,NULL))
			// and then re-bound the front buffer through the L1 cache — if the
			// cache already named pRasterFrontBuffer (left by an earlier pass
			// this frame), that Set value-compared as a no-op and pass 0 drew
			// with s0 = NULL (black input to the whole chain). Parking on a
			// guaranteed-different raster forces a real L1+L2+L3 transition.
			{
				RwRaster *pcWhite = (gpWhiteTexture && gpWhiteTexture->raster)
					? gpWhiteTexture->raster : NULL;
				RwRaster *pcFb = CPostEffects::pRasterFrontBuffer;
				RwRaster *pcPark = (pcFb == pcWhite) ? NULL : pcWhite;
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pcPark);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pcFb);
			}

			// Normal buffer on stage 1
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// Depth on stage 2 (hook suspended above so INTZ can be sampled)
			dev->SetTexture(2, g_ssaoDepthTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			overrideIm2dPixelShader = PipeChainShader;
			PfBindPS(PipeChainShader, "IDR_PIPECHAINPS(144)");
			PfDraw(4, 2);
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
			PfBindPS(PipeChainShader, "IDR_PIPECHAINPS(144)");
			PfDraw(4, 2);
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
			PfBindPS(PipeChainShader, "IDR_PIPECHAINPS(144)");
			PfDraw(4, 2);
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;
		}

		// ---- Pass 3: Output -> back buffer (BRDF reflection composite) ----
		{
			// BUG 1a (CPU-side, shaders off-limits): the reflection sample
			// (front buffer at reflection-vector UVs) is where the black pools
			// + ghost double-image came from — near-edge rays fetch the 2048²
			// front-buffer pad (unwritten = BLACK) or wrap across the raster
			// border into the opposite screen edge (s0 had NO address mode
			// set — WRAP by default). Two CPU levers before the composite:
			//   1) fade the pack's gloss/spec weights to 0 near the screen
			//      edges (smooth 0.075 margin, CPU-built mask) — reflBlend is
			//      proportional to pack.y*pack.z so the reflection
			//      contribution itself fades to 0 at the edges;
			//   2) D3DTADDRESS_CLAMP on every stage pass 3 samples (s0 most
			//      importantly) = clamp sample UVs to [0,1] at the fetch.
			// The per-pixel envUV clamp/reflect + smooth edge fade proper
			// lives in PipeChain.hlsl pass_output (shaders/ps/) — NOT edited.
			// pf_gate: with pipeChainReflEnable=0 pass 3 binds the pack0 dummy
			// on s4 (below), so the pack this multiply edits is never sampled:
			// skip the extra full-screen draw + RT bind entirely.
			if(config->pipeChainReflEnable && g_pipeChainClassifySurf){
				RwRaster *fadeRas = GetPipeChainEdgeFadeRaster();
				if(fadeRas){
					DWORD prevAB = 0, prevSB = 0, prevDB = 0;
					void *prevAddr = NULL;
					dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevAB);
					dev->GetRenderState(D3DRS_SRCBLEND, &prevSB);
					dev->GetRenderState(D3DRS_DESTBLEND, &prevDB);
					RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &prevAddr);
					dev->SetRenderTarget(0, g_pipeChainClassifySurf);
					dev->SetDepthStencilSurface(NULL);
					// FF multiply: pack.rgb *= mask.rgb (mask alpha = 1 keeps
					// pack.a). All three blend layers forced — the cache-hit
					// skip is exactly the BUG-2 leak mechanism.
					RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
					RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
					RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)D3DBLEND_DESTCOLOR);
					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)D3DBLEND_ZERO);
					RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
					RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
					RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
					dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
					dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
					dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
					overrideIm2dPixelShader = nil; // FF: the stage ops below apply
					overrideColorMod = D3DTOP_SELECTARG1; // colour = mask texel
					overrideAlphaMod = D3DTOP_SELECTARG1; // mask alpha = 1
					CPostEffects::DrawQuadSetDefaultUVs();
					CPostEffects::DrawQuad(0.0f, 0.0f, (float)camRas->width, (float)camRas->height,
						0xFFu, 0xFFu, 0xFFu, 0xFF, fadeRas);
					overrideColorMod = -1;
					overrideAlphaMod = -1;
					// hand pass 3 the pre-fade blend/address state back
					dev->SetRenderState(D3DRS_ALPHABLENDENABLE, prevAB);
					dev->SetRenderState(D3DRS_SRCBLEND, prevSB);
					dev->SetRenderState(D3DRS_DESTBLEND, prevDB);
					RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, prevAB);
					RwD3D9SetRenderState(D3DRS_SRCBLEND, prevSB);
					RwD3D9SetRenderState(D3DRS_DESTBLEND, prevDB);
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)prevSB);
					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)prevDB);
					RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
					RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, prevAddr);
				}
			}
			// DS intentionally NOT re-bound: the depth hook is still suspended
			// and s2 samples INTZ (binding it as DS again would feedback-lock).
			// Z-test is off, so no depth buffer is needed for this draw.
			dev->SetRenderTarget(0, oldRT);

			float pipeP[4] = { 3.0f, 0.0f, config->pipeChainIntensity, 0.0f };
			RwD3D9SetPixelShaderConstant(0, pipeP, 1);

			// s0/s1/s2 CLAMP (default was WRAP): s0 is the front buffer fetched
			// at reflection-vector UVs, s1/s2 at raw UVs — out-of-range fetches
			// must clamp into [0,1], never wrap to the opposite screen edge.
			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
			dev->SetTexture(1, g_normalBufferTex);
			dev->SetTexture(2, g_ssaoDepthTex);
			// Intermediate (texA) on stage 3
			dev->SetTexture(3, texA);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			// Classify pack on stage 4 — POINT so gloss/spec don't bleed
			// across silhouettes. Reflection kill-switch (INI: SkyGfx.pipeChainReflEnable)
			// + normal-buffer content gate (normalBufDead, set at the entry
			// gates): when the reflection term must die, bind the 1x1 pack0
			// dummy (pack.y/z=0 => reflection skipped in-shader) instead of the
			// classify RT — the blur/contrast chain keeps running. The
			// pc_noclassify fallback is preserved separately for a genuinely-
			// missing classify RT (reflection otherwise wanted): an unbound s4
			// samples undefined values in SM3.0 and garbage gloss/spec passes
			// the gate as ink-blot reflections (pack 0 = no reflection,
			// fail-open). The producer side also rejects NaN/garbage gloss/spec
			// before they reach the pack (ClassifyBegin validity gate, BUG 1b).
			IDirect3DTexture9 *packTex;
			if(!config->pipeChainReflEnable || normalBufDead){
				// Reflection off (INI: SkyGfx.pipeChainReflEnable=0) or no
				// usable normals this frame -> force the pack0 dummy.
				packTex = GetPipeChainNoPackTex();
				if(packTex && dbglog_throttle("pc_refloff"))
					dbglog("[PipeChain] pass3: reflection off (reflEnable=%d nbDead=%d) pack0 dummy %p",
						config->pipeChainReflEnable, normalBufDead, packTex);
			}else{
				packTex = g_pipeChainClassifyTex;
				if(packTex)
					s_cfPipeChainReflRan = true; // REAL classify pack on s4 this frame
				else{
					packTex = GetPipeChainNoPackTex();
					if(packTex && dbglog_throttle("pc_noclassify"))
						dbglog("[PipeChain] pass3: classify RT NULL, using pack0 dummy %p", packTex);
				}
			}
			if(packTex){
				dev->SetTexture(4, packTex);
				dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
				dev->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			}else{
				dev->SetTexture(4, NULL); // last resort (dummy creation failed)
			}

			overrideIm2dPixelShader = PipeChainShader;
			PfBindPS(PipeChainShader, "IDR_PIPECHAINPS(144)");
			PfDraw(4, 2);
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
		overrideColorMod = -1;		// same for the pass-3 fade draw's FF stage ops
		overrideAlphaMod = -1;

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

void
CPostEffects::DrawMotionBlur(void)
{
	// No PF_SCOPE here (__try in function -> C2712); see DrawNormalBufferToTexture.
	// Gate log honesty (cutscene≠menu fix): the old line printed the COMBINED
	// IsGameInMenuOrPaused() bool under a "menu=" label, so cutscene frames
	// logged "menu=1 -> SKIP" while IsGameInMenuOrPaused's own log said
	// menu=0. Print the real menu/pause and cutscene flags separately, and
	// make the RUN/SKIP ternary include the full gate decision.
	// Tier 0 (Q5): camera-history continuity latch. ANY return below taken
	// BEFORE the camera block runs (enable off, no FB, menu/pause, cutscene,
	// FB not ready, no device/camera) leaves this false, so the first frame
	// back sees mbCutReset and re-latches the prev camera pose instead of
	// diffing against pre-skip state (the stale-velocity pop on re-entry).
	static bool mbHistValid = false;
	bool mbCutReset = !mbHistValid;
	bool mbMenuSkip = PostfxMenuPauseOnly();
	bool mbRun = config->motionBlurEnable && MotionBlur_Burnout && pRasterFrontBuffer &&
		!mbMenuSkip && !CCutsceneMgr__ms_running;
	if(dbglog_throttle("pf_mb_gate"))
		dbglog("[PF] MotionBlur gate: enable=%d ps=%p fb=%p menu=%d cutscene=%d -> %s",
			config->motionBlurEnable, MotionBlur_Burnout, (void*)pRasterFrontBuffer,
			(int)mbMenuSkip, (int)CCutsceneMgr__ms_running,
			mbRun ? "RUN" : "SKIP");
	if(!config->motionBlurEnable || !MotionBlur_Burnout)
		return;
	if(!pRasterFrontBuffer)
		return;
	// Menu/pause skip as before. CUTSCENE STILL SKIPS, deliberately: motion
	// blur is not part of the core grading chain, and during cutscenes
	// (a) camera cuts spike cameraVelocity -> one full-frame smear frame per
	// cut, and (b) the velocity-buffer lane (postfx_motionblur.cpp, outside
	// this fix's file set) still skips, so s1 would bind a stale pre-cutscene
	// velocity texture. Revisit both together in a later round. TIER 0
	// (2026-10-29): the stale-CPU-history half is fixed at the consumption
	// point below (mbHistValid/camInitialized re-latch = one clean frame on
	// re-entry); the stale s1 texture half for the OTHER velocity consumers
	// (SMAA/SSAO) is still a later lane.
	if(mbMenuSkip)
		return;
	if(CCutsceneMgr__ms_running)
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
	if(!PfFrontBufferReady("motion blur"))
		return;

	// Track camera velocity for motion blur
	static float prevCamX = 0, prevCamY = 0, prevCamZ = 0;
	static float prevAtX = 0, prevAtY = 0, prevAtZ = 0;
	static bool camInitialized = false;

	// Q5 cut reset (Tier 0): first VALID frame after a skipped stretch —
	// force a history re-latch so this frame contributes ZERO camera
	// velocity/rotation instead of diffing the pose across the cut. The
	// pass then exits through the existing cameraMovement < 0.002 gate:
	// one clean frame, matching GTA V's "cut → reset prev matrix"
	// (postfx.fx:7494-7497), not a stale-reprojection smear.
	if(mbCutReset)
		camInitialized = false;

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
			mbHistValid = true;	// pose re-latched: history continuous into the next frame
		}
	}

	// Tier 0 (Q4) framerate normalisation: cameraVelocity/cameraRotation are
	// PER-FRAME deltas but the thresholds below (gate 0.002, ramps
	// 0.05/0.12/0.09/0.14, camera-aware 0.8) are absolute 60 FPS-per-frame
	// values from telemetry — at 144 FPS the deltas are ~2.4x smaller and
	// blur silently vanished. Rescale both deltas to their 60 FPS-equivalent
	// with MotionBlurDtScale() (postfx_motionblur.cpp): dt = ms_fTimeStep/50
	// seconds (the game-wide seconds*50 clock — same /50 the ragdoll update
	// at main.cpp uses), scale = (1/60)/dt — exactly 1.0 at 60 FPS. The old
	// inline 25/ms_fTimeStep assumed ms_fTimeStep=25 at 60 FPS (a /1500
	// clock); the real clock reads ~0.833 there, so the old scale was ~30x
	// and saturated cameraMovement on every moving frame.
	// cameraRotation = 1-cos(dtheta) ~ dtheta^2/2 is a QUADRATIC per-frame
	// delta, so it takes the scale squared.
	float rawCamVelocity = cameraVelocity;	// pre-scale, for the teleport-cut test
	float mbMs = CTimer__ms_fTimeStep;
	float mbVelScale = MotionBlurDtScale();
	cameraVelocity *= mbVelScale;
	cameraRotation *= mbVelScale * mbVelScale;

	// Q5 teleport cut: a raw per-frame position jump far beyond any real
	// camera motion (warp/teleport spikes reach ~1000 units/frame, see the
	// gain comment below) is a camera CUT, not motion — zero this frame's
	// contribution and re-latch; same clean-frame result as mbCutReset.
	bool mbTeleportCut = (rawCamVelocity > 100.0f);

	// One throttled receipt: dt path + cut resets observable per run
	if(dbglog_throttle("mb_tier0"))
		dbglog("[PostFX] mb_tier0: dt=%.4f velScale=%.3f cutReset=%d",
			mbMs * (1.0f/50.0f), mbVelScale, mbCutReset ? 1 : (mbTeleportCut ? 2 : 0));
	if(mbTeleportCut){
		camInitialized = false;	// re-latch next frame against the post-warp pose
		return;			// one clean frame; no smear across the warp
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

	// ---- Entry stage-0 capture (font-safety / test-9) ---------------------
	// The park below overwrites the L1 raster cache (RwRenderStateSet) and the
	// front-buffer bind overwrites the device stage-0 texture. The old exit
	// hardcoded a NULL raster and never touched the device, so the pass ended
	// with L1 = NULL while L3 still held pRasterFrontBuffer's texture: any
	// text/font/HUD quad drawn afterwards could sample the frame buffer
	// instead of its glyph atlas (reported "fonts completely broken").
	// Capture BOTH observable layers BEFORE the park — L1 via RwRenderStateGet,
	// L3 via GetTexture (AddRef -> released after the restore below). The
	// restore at the exit re-asserts the entry raster through L1 (which drives
	// L2) and raw-pins the captured L3 texture.
	void *mbEntryS0 = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &mbEntryS0);
	if(mbEntryS0 == (void*)0xFFFFFFFF)
		mbEntryS0 = NULL;	// failed-Get sentinel (see pedStage0Resync)
	IDirect3DBaseTexture9 *mbEntryS0Dev = NULL;
	dev->GetTexture(0, &mbEntryS0Dev);	// AddRef
	// Entry PS is captured by the park block below (AddRef held there) and
	// released at the exit after both PS layers are forced back to NULL.
	IDirect3DPixelShader9 *mbEntryPS = NULL;

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

	// Bind front buffer as current frame (s0).
	// test-8 PAIRED BIND (motion-vector-trails fix, 2026-11-01): park NULL
	// through the RW cache first so the real bind below is always a cache
	// miss -> real device write. The TEXTURERASTER cache persists across
	// frames, and raw stage-0 writers in this same frame's earlier passes
	// (SSAO s0=depth postfx_ssao.cpp:983, velocity pass s0=depth
	// postfx_motionblur.cpp:321) leave the DEVICE holding a foreign texture
	// while the cache still believes a stale value — a cache-hit elision
	// here would draw this quad sampling whatever s0 really holds.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pRasterFrontBuffer);

	// Bind velocity buffer on s1 (RG=velocity XY packed to [0,1], B=magnitude).
	// Raw dev->SetTexture — no RW cache, always a real device write. velBound
	// mirrors the bind for the cleanup below (sampler restore must gate on
	// exactly what was written, not on a config read that can drift).
	bool velBound = false;
	if(config->velocityBufferEnable && g_velocityTex){
		dev->SetTexture(1, g_velocityTex);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		velBound = true;
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

		// Render fullscreen quad with motion blur shader.
		// test-8 PAIRED PS BIND (motion-vector-trails fix, 2026-11-01):
		// RwD3D9SetPixelShader value-compares against the exe-side
		// _rwD3D9LastPixelShaderUsed cache (normmap_stubs.cpp:82). That cache
		// persists ACROSS FRAMES, so at this point it still holds
		// MotionBlur_Burnout from the PREVIOUS frame's MB draw — while raw
		// device-PS writers in the scene phase (waterPipe.cpp:2621
		// g_d3dDevice->SetPixelShader, mirrorPass.cpp:184, forwardplus) can
		// leave a FOREIGN shader on the device. The im2d hook's Set
		// (Im2dSetPixelShader_hook, postfx.cpp:503) then ELIDES, and this
		// quad rasterises with the foreign PS sampling s0=frame s1=velocity
		// s2=depth under wrong semantics — velocity-buffer content painted
		// to the screen = the reported "motion vector trails". Same L2/L3
		// split disease PostfxStateSyncCheck heals for blend — but on the PS
		// layer, which that check does not cover.
		// Fix: park-and-set. Raw SetPixelShader(NULL) forces the device to a
		// known state (always a real write); RwD3D9SetPixelShader(NULL)
		// re-seeds the RW cache to NULL; the hook's Set(MotionBlur_Burnout)
		// is then a guaranteed cache miss -> real device write. The entry PS
		// capture here (AddRef) is DIAGNOSTIC-ONLY — mbEntryPS has no readers
		// outside the exit Release (the entry PS is deliberately NOT
		// restored) — so the per-frame device Get is opt-in via diagLevel
		// (first hit still probes once); the exit still forces BOTH layers
		// back to NULL — the game's FFP HUD/text must not inherit a postfx
		// shader.
		{
			bool psFirst = dbglog_once("mb_pssplit_first");
			if(psFirst || g_diagLevel >= 1){
				IDirect3DPixelShader9 *mbCurPS = NULL;
				if(SUCCEEDED(dev->GetPixelShader(&mbCurPS))){
					mbEntryPS = mbCurPS;	// AddRef held; released at the exit
					if(mbCurPS != (IDirect3DPixelShader9*)MotionBlur_Burnout &&
					   (psFirst || (g_diagLevel >= 1 && dbglog_throttle("mb_pssplit"))))
						dbglog("[mb_pssplit] DrawMotionBlur: device PS %p != MotionBlur_Burnout %p BEFORE bind "
							"(raw scene-phase writer left a foreign shader; un-parked bind would have elided)",
							(void*)mbCurPS, MotionBlur_Burnout);
				}
			}
			dev->SetPixelShader(NULL);		// raw park: device = known NULL (functional, unconditional)
			RwD3D9SetPixelShader(NULL);		// cache park: RW cache = NULL (functional, unconditional)
		}
		overrideIm2dPixelShader = MotionBlur_Burnout;
		PfBindPS(MotionBlur_Burnout, "IDR_MOTIONBLUR_BURNOUT(140)");
		PfDraw(4, 2);
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

	// Cleanup texture stages — paired with the binds above: s1/s2 unbind runs
	// on BOTH the normal and __except paths (it sits after the handler), and
	// the s1 sampler restore gates on velBound (what was actually written)
	// instead of a fresh config read, so the set/restore pair cannot drift.
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
	if(velBound){
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	}

	// PS exit (font-safety): the draw's Im2D hook set MotionBlur_Burnout through
	// RwD3D9SetPixelShader, so BOTH the RW PS cache and the device hold it when
	// this pass ends. The next consumer of the pipeline is the game's FFP
	// HUD/text, which must run with NO pixel shader — and a raw NULL alone
	// would leave the cache on MotionBlur_Burnout (a later cache-gated
	// Set(NULL) could then elide). Force both layers to NULL. The captured
	// entry PS is deliberately NOT restored (FFP text must not inherit a
	// postfx shader); the capture itself is diagnostic-only (mbEntryPS has no
	// readers outside this Release) and is skipped at diagLevel=0 after the
	// first-hit probe — mb_pssplit logs it when it was foreign.
	dev->SetPixelShader(NULL);		// L3 raw: always a real write
	RwD3D9SetPixelShader(NULL);		// L2 cache re-seed (+ device transition)
	if(mbEntryPS){
		mbEntryPS->Release();
		mbEntryPS = NULL;
	}

	// Restore stage 0 to the captured ENTRY raster through all three layers.
	// The park + front-buffer bind above overwrote the L1 cache AND the device.
	// L1: park-and-set — park is guaranteed != the entry raster, so the final
	//     Set is a real transition even if the draw left the same raster; the
	//     L1 Set drives L2 (RW's texture-apply cache) and L3.
	// L3: raw-pin the captured entry device texture, so no cache hit can leave
	//     the device on pRasterFrontBuffer (= the "font quad samples the frame
	//     buffer" leak). GetTexture AddRef is released after the receipt below.
	{
		RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
			? gpWhiteTexture->raster : NULL;
		RwRaster *entryRas = (RwRaster*)mbEntryS0;
		RwRaster *parkRas = (entryRas == whiteRas) ? NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)entryRas);
	}
	if(mbEntryS0Dev)
		dev->SetTexture(0, mbEntryS0Dev);	// raw L3 pin: entry texture

	// mb_s0leak: what this pass leaves on stage 0 must equal the entry capture.
	// A mismatch proves an incomplete restore (or a raw writer moved the device
	// mid-pass) — the state that makes the following FFP font/HUD quads sample
	// a foreign texture. Purely diagnostic (RwRenderStateGet + GetTexture
	// AddRef/Release — reads only, no state writes): the whole probe is
	// opt-in via diagLevel — first hit still probes once (one-shot receipt),
	// then diagLevel>=1; the L3 comparison is skipped when the entry device
	// texture was NULL (nothing to restore on that layer).
	{
		bool s0First = dbglog_once("mb_s0leak_first");
		if(s0First || g_diagLevel >= 1){
			void *exitS0 = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &exitS0);
			IDirect3DBaseTexture9 *exitS0Dev = NULL;
			dev->GetTexture(0, &exitS0Dev);	// AddRef
			bool rasOk = (exitS0 == mbEntryS0);
			bool devOk = (mbEntryS0Dev == NULL) || ((void*)exitS0Dev == (void*)mbEntryS0Dev);
			if(!rasOk || !devOk){
				if(s0First || (g_diagLevel >= 1 && dbglog_throttle("mb_s0leak")))
					dbglog("[mb_s0leak] DrawMotionBlur left s0 raster=%p devTex=%p but entry was raster=%p devTex=%p "
						"(incomplete restore -> following font/HUD quads may sample a foreign texture)",
						exitS0, (void*)exitS0Dev, mbEntryS0, (void*)mbEntryS0Dev);
			}
			if(exitS0Dev) exitS0Dev->Release();
		}
	}
	if(mbEntryS0Dev){
		mbEntryS0Dev->Release();
		mbEntryS0Dev = NULL;
	}

	// Restore render states
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
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
	// No PF_SCOPE here (__try in function -> C2712); see DrawNormalBufferToTexture.
	DBGLOG_ENTER("DrawHeightFog");
	// Gate log honesty + cutscene≠menu (see helper block at top of file): the
	// old line printed the combined skip bool as "menu=", so cutscene frames
	// logged menu=1 while the real menu flag was 0. HeightFog is scene
	// atmosphere — it now RUNS during cutscenes. Depth input stays current:
	// the INTZ DS is bound persistently by the depthhook vtable hook (not
	// per-SSAO-run), so g_ssaoDepthTex holds this frame's depth even though
	// DrawSSAO still early-returns on cutscene frames.
	bool hfMenuSkip = PostfxMenuPauseOnly();
	// Family E fix (2026-11-02-test11): heightFogDensity=0 is now OFF — the pass
	// is a bit-exact no-op (no substitute 0.0015 density, no skyBot colour bleed,
	// no UpdateFrontBuffer, no state writes). Previously INI density=0 was
	// "sanitized" to a forced 0.0015 + skyBot tint = the grey veil in 6/14 shots.
	bool hfRun = config->heightFogEnable && config->heightFogDensity > 0.0f &&
		HeightFog && CPostEffects::pRasterFrontBuffer &&
		g_ssaoDepthTex && !hfMenuSkip;
	if(dbglog_throttle("pf_hf_gate"))
		dbglog("[PF] HeightFog gate: enable=%d density=%.5f ps=%p fb=%p depth=%p menu=%d cutscene=%d -> %s",
			config->heightFogEnable, config->heightFogDensity, HeightFog, (void*)CPostEffects::pRasterFrontBuffer,
			(void*)g_ssaoDepthTex, (int)hfMenuSkip, (int)CCutsceneMgr__ms_running,
			hfRun ? "RUN" : "SKIP");
	if(!config->heightFogEnable || config->heightFogDensity <= 0.0f || !HeightFog)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(hfMenuSkip)
		return;
	static bool s_csFired_hf = false;
	if(CCutsceneMgr__ms_running)
		CutscenePostfxEnabledLog("HeightFog", &s_csFired_hf);

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
	if(!PfFrontBufferReady("height fog"))
		return;

	// Effective fog params — sanitize the values skygfx.h's comments promised
	// but readIni never defaulted. density<=0 never reaches here (gate returns
	// above: density=0 is OFF, not a substitute default — Family E fix). Only
	// the upper cap remains: menu-dragged density 0.042 => 98% fog at 100m was
	// a full-screen blue wash stacked on the game's own distance fog.
	CColourSet &tc = CTimeCycle__m_CurrentColours;
	float fogDensity = config->heightFogDensity;
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
		if(dbglog_throttle("pf_hf_bind"))
			dbglog("[PF] HeightFog bind: ps=%p(IDR_HEIGHTFOG 250) s0=cameraRT s1=g_ssaoDepthTex out=cameraRT %dx%d",
				HeightFog, w, h);
		PfBindPS(HeightFog, "IDR_HEIGHTFOG(250)");
		PfDraw(4, 2);
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
		DWORD ex = GetExceptionCode();	// captured before any nested __try (see below)

		// Nested guard: this handler re-enters D3D/RW (ImmediateMode ReStore,
		// DepthHook_Restore, RwRenderStateSet), and a lost/device-faulted device
		// can fault AGAIN inside __except -> unhandled crash outside any filter.
		// POD-only locals inside (C2712); inner filter swallows + throttled-logs.
		__try {
			if(dbglog_throttle("hf_crash"))
				dbglog("[PostFX] DrawHeightFog CRASHED exception=0x%08X", ex);

			// RW-cache re-push (doctrine): the raw restores below are NOT enough —
			// the game's Im2D re-emits the L2-cached states at draw time, so every
			// state this pass changed via RwRenderStateSet (:3611-3618) must be
			// re-pushed through the cache too. Mirrors the normal-path cleanup
			// (:3701-3706) and the DrawGodRays __except (rw cache first, raw after).
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

			if(savedPSConstsValid)
				dev->SetPixelShaderConstantF(0, savedPSConsts, 8);
			// SEH: mirror the normal path's s1 cleanup (:3700) — unbind the INTZ
			// depth tex BEFORE DepthHook_Restore re-binds it as DS (feedback-lock
			// guard, same ordering as the normal path).
			dev->SetTexture(1, NULL);
			DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
			CPostEffects::ImmediateModeRenderStatesReStore();
			if(rawGeomSaved)
				RestoreRawGeomStates(rawGeom);
		} __except(EXCEPTION_EXECUTE_HANDLER){
			if(dbglog_throttle("hf_crash2"))
				dbglog("[PostFX] DrawHeightFog cleanup re-faulted exception=0x%08X", GetExceptionCode());
		}
	}

	// Sync front buffer so god rays can read the fogged scene
	CPostEffects::UpdateFrontBuffer();
}

static void
DrawGodRays(void)
{
	// No PF_SCOPE here (__try in function -> C2712); see DrawNormalBufferToTexture.
	DBGLOG_ENTER("DrawGodRays");
	// Gate log honesty + cutscene≠menu (see helper block at top of file):
	// same mislabel as HeightFog — combined skip bool printed as "menu=".
	// GodRays is scene lighting — it now RUNS during cutscenes.
	bool grMenuSkip = PostfxMenuPauseOnly();
	bool grRun = config->godRaysEnable && GodRays && CPostEffects::pRasterFrontBuffer &&
		!grMenuSkip;
	if(dbglog_throttle("pf_gr_gate"))
		dbglog("[PF] GodRays gate: enable=%d ps=%p fb=%p menu=%d cutscene=%d -> %s",
			config->godRaysEnable, GodRays, (void*)CPostEffects::pRasterFrontBuffer,
			(int)grMenuSkip, (int)CCutsceneMgr__ms_running,
			grRun ? "RUN" : "SKIP");
	if(!config->godRaysEnable || !GodRays)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(grMenuSkip)
		return;
	static bool s_csFired_gr = false;
	if(CCutsceneMgr__ms_running)
		CutscenePostfxEnabledLog("GodRays", &s_csFired_gr);

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
	if(!PfFrontBufferReady("god rays"))
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

	// Pass breadcrumb: fires ONLY on frames where the sun gate above passed and
	// the ray quad is about to be drawn — if a covered-frame forensics pass ever
	// sees this line immediately before the symptom, this is the pass.
	// (H1 fix: no blend claim here — the blend SET log below, emitted at the
	// actual state writes, is the single source of truth for "blend was set".)
	if(dbglog_throttle("grad_draw"))
		dbglog("[PostFX] DrawGodRays: sun=(%.2f,%.2f) exp=%.4f dec=%.2f shader=%p",
			sunScreenX, sunScreenY, config->godRaysExposure,
			config->godRaysDecay, GodRays);

	// POD locals only from here — the function contains __try (C2712), and
	// these must stay visible to the __except cleanup below.
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	bool grBlendSetLogged = false;

	__try {
		rawGeomSaved = SaveRawGeomStates(rawGeom); // no depth hook in this pass
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
		// H1 fix: paired blend logging. grBlendSetLogged records whether the SET
		// log fired this invocation; the RESTORE log at the end of the pass is
		// gated on this same bool — NOT on an independent throttle tag — so a
		// restore log fires whenever a set log fired (1:1 by construction, no
		// throttle drift). Both live after every gate above, so a disabled pass
		// (godRaysEnable=0) can never emit either one. Declared before __try so
		// both the __except path and the restore log below can see it.
		grBlendSetLogged = dbglog_throttle("pf_gr_set");
		if(grBlendSetLogged)
			dbglog("[PF] GodRays: blend ONE/ONE SET (rw cache + RwD3D9 + raw), sun=(%.2f,%.2f)",
				sunScreenX, sunScreenY);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		// test-11 lane: 3-layer park-and-set pairing for the blend family
		// (doctrine: pipelinecommon.cpp:1144 — L1 park->target, L2 park->target,
		// L3 unconditional raw pin). The old entry wrote L1+L2 only: if both
		// caches already held ONE/ONE from a prior leak while the device held
		// something else, both sets no-op'd and the quad blended with a stale
		// device factor; and no raw pin meant a foreign L3 writer between the
		// set and the draw went uncorrected. Park values are the normal world
		// blend (SRCALPHA/INVSRCALPHA) — != the ONE/ONE target on every leg, so
		// each cache takes a guaranteed real transition. Exit-side restore is
		// already 3-layer paired via RestoreRawGeomStates (snapshot bracket).
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		pipeL2ParkSet(D3DRS_ALPHABLENDENABLE, FALSE, TRUE);
		pipeL2ParkSet(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DBLEND_ONE);
		pipeL2ParkSet(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, D3DBLEND_ONE);
		// L3 raw pins — authoritative device writes (applied[]-gated flush can
		// never repair a device a foreign raw writer moved).
		pipeL3Set(D3DRS_ALPHABLENDENABLE, TRUE);
		pipeL3Set(D3DRS_SRCBLEND, D3DBLEND_ONE);
		pipeL3Set(D3DRS_DESTBLEND, D3DBLEND_ONE);

		// s0: scene color (front buffer)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);

		// c0: sun screen position
		float c0[4] = { sunScreenX, sunScreenY, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		// c2: num samples — also clamped to the shader's compile-time loop
		// bound (GodRays.hlsl:56 `for(int i = 0; i < 20; i++)`): a hand-edited
		// INI value above 20 would make the CPU-side energy bound below assume
		// more accumulation than the shader actually performs.
		float numSamples = (float)config->godRaysNumSamples;
		if(config->godRaysNumSamples < 1) numSamples = 20.0f;
		if(numSamples > 20.0f) numSamples = 20.0f;
		float c2[4] = { numSamples, 0.0f, 0.0f, 0.0f };

		// c1: ray params — sanitize: config may be zero-initialised (no INI key),
		// which previously collapsed the effect to a pure additive frame double.
		float grExposure = (config->godRaysExposure > 0.0f) ? config->godRaysExposure : 0.0034f;
		float grDecay    = (config->godRaysDecay > 0.0f && config->godRaysDecay <= 1.0f) ? config->godRaysDecay : 1.0f;
		float grDensity  = (config->godRaysDensity > 0.0f) ? config->godRaysDensity : 0.84f;
		float grWeight   = (config->godRaysWeight > 0.0f) ? config->godRaysWeight : 1.0f;

		// test-11 white-blowout lane: BOUND the additive accumulation. This pass
		// draws ONE/ONE onto the back buffer, and the shader's ray sum samples
		// the FRONT BUFFER (post-tonemap, so every sample is <= 1.0 per channel).
		// Worst-case per-pixel additive contribution is therefore
		//   exposure * weight * sum(decay^i, i=0..N-1)      (sunVis <= 1 in-shader)
		// With decay=1.0 the geometric sum is N, so a bright-fog scene (weather
		// 9->2, fog=-29, cloud=1.00 — test-11 findings) turns a large exposure/
		// weight config into a full-screen white add on an already-near-white
		// frame. Scale exposure+weight down UNIFORMLY when the worst case
		// exceeds the cap: ray shape/falloff/intent preserved, saturation made
		// impossible. Defaults (0.0034 * 1.0 * 20 = 0.068) sit far below the cap
		// and are untouched. NaN-safe: the sanitize above rejects NaN config
		// (all comparisons false -> defaults), and grWorst > cap is false for NaN.
		float grGeoSum = (grDecay >= 1.0f)
			? numSamples
			: (1.0f - powf(grDecay, numSamples)) / max(1.0f - grDecay, 1e-7f);
		float grWorst = grExposure * grWeight * grGeoSum;
		const float kGRAdditiveCap = 0.30f;
		if(grWorst > kGRAdditiveCap){
			float grScale = kGRAdditiveCap / max(grWorst, 1e-7f);
			grExposure *= grScale;
			grWeight  *= grScale;
			if(dbglog_throttle("gr_bound"))
				dbglog("[PF] GodRays: additive energy bound applied worst=%.4f -> cap=%.2f "
					"(exp=%.5f w=%.3f N=%.0f decay=%.2f)",
					grWorst, kGRAdditiveCap, grExposure, grWeight, numSamples, grDecay);
		}
		float c1[4] = { grExposure, grDecay, grDensity, grWeight };
		RwD3D9SetPixelShaderConstant(1, c1, 1);
		RwD3D9SetPixelShaderConstant(2, c2, 1);

		// Render fullscreen quad — SEH-guarded so the override clears on fault too.
		PfBindPS(GodRays, "IDR_GODRAYS(251)");
		PfDraw(4, 2);
		guardedIm2DRender(GodRays, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, "godrays");

		// Cleanup — blend OFF through the RW cache FIRST (guaranteed transition:
		// this pass set VERTEXALPHA TRUE on entry, so the FALSE set always moves
		// and re-pushes L2+L3), then the factors back through the cache. The old
		// order — L2-only ALPHABLENDENABLE FALSE first, rw VERTEXALPHA TRUE last
		// — left the device blend OFF under a cache that said ON: the TRUE
		// no-oped against the cache this pass set at entry, so later consumers
		// whose TRUE set no-oped drew REPLACE (black squares). No TRUE re-set
		// afterwards: what follows (ImmediateModeRenderStatesReStore +
		// RestoreRawGeomStates) restores the pre-pass snapshot through all three
		// layers, and every consumer pass sets its own blend state on entry.
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
	} __except(EXCEPTION_EXECUTE_HANDLER){
		// Fault-safety: run the SAME cleanup sequence as the normal path so a
		// fault in the state-set/draw span can't leak blend ONE/ONE + ZWRITE
		// FALSE + ALPHATEST FALSE into the next frame's world pass.
		DWORD ex = GetExceptionCode();	// captured before any nested __try (see below)
		// Nested guard: the cleanup below re-enters D3D/RW (device-lost can
		// re-fault inside __except -> unhandled crash outside any filter).
		// POD-only locals inside (C2712); inner filter swallows + throttled-logs.
		__try {
			if(dbglog_throttle("gr_crash"))
				dbglog("[PostFX] DrawGodRays CRASHED exception=0x%08X", ex);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			CPostEffects::ImmediateModeRenderStatesReStore();
			if(rawGeomSaved)
				RestoreRawGeomStates(rawGeom);
		} __except(EXCEPTION_EXECUTE_HANDLER){
			if(dbglog_throttle("gr_crash2"))
				dbglog("[PostFX] DrawGodRays cleanup re-faulted exception=0x%08X", GetExceptionCode());
		}
		return;
	}
	// H1 fix: restore log gated on the set-log bool, not its own throttle tag.
	// Fires iff the SET log fired above, AFTER the full 3-layer restore has
	// completed (rw cache OFF + SRCALPHA/INVSRCALPHA, ImmediateMode ReStore,
	// raw snapshot ReStore incl. ALPHABLENDENABLE/SRCBLEND/DESTBLEND), so the
	// line is always truthful when it appears. (Fault path: the __except above
	// returns after cleanup, so a crashing frame logs CRASHED instead of the
	// restore line.)
	if(grBlendSetLogged)
		dbglog("[PF] GodRays: blend RESTORED (rw cache OFF + SRCALPHA/INVSRCALPHA + raw snapshot ReStore, all 3 layers)");
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
	diag_registerScope("DrawSunShafts", (void*)DrawSunShafts);
	diag_registerScope("DrawDOF", (void*)DrawDOF);
	diag_registerScope("DrawBloom", (void*)DrawBloom);
	diag_registerScope("DrawChromaticAberration", (void*)DrawChromaticAberration);
	diag_registerScope("DrawFilmGrain", (void*)DrawFilmGrain);
	diag_registerScope("DrawVignette", (void*)DrawVignette);
	diag_registerScope("DrawFXAA3", (void*)DrawFXAA3);
	diag_registerScope("DrawNormalBufferToTexture", (void*)DrawNormalBufferToTexture);
	diag_registerScope("DrawPipeChain", (void*)DrawPipeChain);
	diag_registerScope("RenderIBLBuffer", (void*)RenderIBLBuffer);
	diag_registerScope("ColourFilter_switch", (void*)CPostEffects::ColourFilter_switch);

	dbglog("  scope tags registered");

	grainRaster = RwRasterCreate(64, 64, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
	dbglog("  grain raster=%p", grainRaster);
	return true;
}
