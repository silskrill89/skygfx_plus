#pragma once

// ============================================================
// postfx.h — shared declarations for the postfx module split (m0060).
// Definitions live in:
//   postfx.cpp              — hook entries, init/term ordering, radiosity/VCS,
//                             ColourFilter_switch, DrawFinalEffects, IBL /
//                             normal-buffer / pipe-chain, atmosphere tail
//   postfx_utils.cpp        — colorfilterVerts/Indices, tracked rasters,
//                             screen-size cache, menu guard, fullscreen quad,
//                             raw-geom save/restore
//   postfx_ssao.cpp         — SSAO passes + depth/prev-depth/normal-buffer state
//   postfx_smaa.cpp         — SMAA passes + static resources
//   postfx_colourfilter.cpp — colour-filter chain, IV grade, colour cycle
//   postfx_motionblur.cpp   — velocity buffer / DrawVelocityBuffer
//   postfx_luminance.cpp    — luminance measure/adapt targets + auto exposure
//   postfx_newfx.cpp        — W3456 fx passes (sun shafts/DOF/bloom/CA/grain/
//                             vignette/FXAA3)
// ============================================================

// Deferred SMAA raster init — called from RenderScene_before (main.cpp)
// to force-create D3D9 surfaces for CAMERATEXTURE rasters outside the
// main camera's BeginUpdate (RW 3.6 driver crashes mid-frame).
void SMAATryInitRasters(void);

// Debug dump helper — called from RenderScene_after (main.cpp)
// to capture the camera raster after scene render (INI-gated: postfxDumpDebug=1)
void PostFX_DumpSceneCamera(void);

// Release all D3DPOOL_DEFAULT resources (call on device lost/reset)
void ReleaseDefaultPoolResources(void);

// Release luminance target resources (g_lumaMeas*, g_lumaAdapt*)
void ReleaseLuminanceTargets(void);

// Central cleanup on device reset (called from depthhook::hook_Reset)
void OnDeviceReset(void);

// Copy the live scene depth (INTZ, currently bound as device DS) into dstTex
// (R32F render target, dstW x dstH) as raw window-space z. Used by the water
// pipe at water-draw time for depth-based shore blending / translucency.
// Returns false when the copy cannot run — caller falls back to depth-less
// shading. Defined in postfx_ssao.cpp (needs the Im2D override + raw-state idiom).
bool PostFX_CopyDepthToTexture(struct IDirect3DTexture9 *dstTex, int dstW, int dstH);

// Trusted screen-size cache (defined in postfx_utils.cpp). Returns false until first capture.
bool GetScreenSize(int *w, int *h);

// ============================================================
// Grade / colour-cycle types. Struct definitions live here because both
// postfx_colourfilter.cpp (chain + impls) and postfx.cpp (Initialise_skygfx
// calls Colorcycle::Initialise, DrawFinalEffects builds Grade rows) need them.
// Implementations: postfx_colourfilter.cpp.
// ============================================================
struct Grade
{
	float r, g, b, a;
};

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

// ============================================================
// postfx_utils.cpp — shared helpers.
// (SaveRawGeomStates / RestoreRawGeomStates are declared in skygfx.h)
// ============================================================
extern RwIm2DVertex *colorfilterVerts;
extern RwImVertexIndex *colorfilterIndices;
extern RwIm2DVertex s_ffQuad[4];
extern RwImVertexIndex s_ffQuadIdx[6];
void SetupFullscreenQuad(float w, float h);
void TrackRaster(RwRaster *ras);
void UntrackRaster(RwRaster *ras);
bool RasterEnsureSurfaceReady(RwRaster *ras, const char *tag);
void CaptureScreenSize(RwRaster *camRas);
bool IsGameInMenuOrPaused();

// ============================================================
// Debug dump helpers (defined in postfx.cpp; postfx_ssao.cpp validates the
// AO output with DumpRasterToBMP and DrawSSAO_Overhaul dumps its RT).
// Gate is INI postfxDumpDebug + PostFX_DumpActive(): the first 5 frames of a
// session, PLUS whatever window PostFX_ArmDump opened (F10 hotkey).
// ============================================================
void DumpRasterToBMP(const char* tag, IDirect3DSurface9* src);
void DumpCurrentRT(const char* tag);
// Arm the harness for the next `frames` frames (source is a free-form string
// used only in the log line, e.g. "F10 hotkey"). Safe to call at any time;
// it is idempotent in the sense that a later call just re-arms.
void PostFX_ArmDump(int frames, const char *source);

// ============================================================
// postfx_colourfilter.cpp
// (gradingPS / contrastPS / tonemapPassPS are declared in skygfx.h;
//  ReleaseSSAOOverhaulResources in skygfx.h)
// ============================================================
extern int overrideColorMod;
extern int overrideAlphaMod;
extern void *overrideIm2dPixelShader;
extern bool s_tonemapIdentityGrade;
extern RwRaster *grainRaster;
extern RwRaster *s_ivGradeRas;
extern int s_ivGradeW, s_ivGradeH;
void DrawIVGrade(void);

// ============================================================
// postfx_luminance.cpp
// ============================================================
extern void *luminanceReducePS, *luminanceAdaptPS;
IDirect3DTexture9 *RunFrameExposure(float adaptSpeed);

// ============================================================
// postfx_ssao.cpp
// (g_normalBufferTex is declared in skygfx.h)
// ============================================================
extern IDirect3DTexture9 *g_ssaoDepthTex;
extern IDirect3DSurface9 *g_ssaoDepthSurf;
extern RwRaster *g_ssaoOutputRaster;
extern IDirect3DTexture9 *g_prevDepthTex;
extern IDirect3DSurface9 *g_prevDepthSurf;
extern bool g_prevDepthValid;
extern int s_prevDepthW, s_prevDepthH;
extern IDirect3DSurface9 *g_normalBufferSurf;
extern int s_normalBufW, s_normalBufH;
extern bool g_normalBufferHasContent;
extern IDirect3DTexture9 *g_prevNormalTex;
extern IDirect3DSurface9 *g_prevNormalSurf;
extern bool g_normalHistoryValid;
void InitSSAOResources(void);
void CopyDepthToPrev(void);
void DrawSSAO_Overhaul(void);

// ============================================================
// postfx_smaa.cpp
// (ReleaseSMAAStaticResources is declared in skygfx.h; CheckDeviceState
//  stays file-static there — only DrawSMAA uses it)
// ============================================================
extern IDirect3DTexture9 *g_smaaAreaTex;
extern IDirect3DTexture9 *g_smaaSearchTex;

// ============================================================
// postfx_motionblur.cpp
// ============================================================
extern IDirect3DTexture9 *g_velocityTex;
void DrawVelocityBuffer(void);
void ReleaseVelocityBufferResources(void);
// Tier 0 (a): 60 FPS-equivalent per-frame scale for motion-blur deltas,
// (1/60)/dt from CTimer__ms_fTimeStep (seconds*50), dt clamped to
// [1e-4, 0.1] s, neutral 1.0 outside that range. Quadratic deltas
// (1-cos rotation) must square it. DrawMotionBlur's inline 25/ms_fTimeStep
// should adopt this (its constant assumes the wrong timestep units).
float MotionBlurDtScale(void);

// ============================================================
// VCS radiosity rasters (defined in postfx.cpp, consumed by postfx_smaa.cpp
// for its deferred-init guard)
// ============================================================
extern RwRaster *vcs_radiosity_target1, *vcs_radiosity_target2;
extern bool s_vcsRadPendingInit;

// ============================================================
// postfx_newfx.cpp — W3456 post-effect passes
// ============================================================
void *FxTonemapLutShader(void);
void DrawSunShafts(void);
void DrawDOF(void);
void DrawBloom(void);
void DrawChromaticAberration(void);
void DrawFilmGrain(void);
void DrawVignette(void);
void DrawFXAA3(void);
void ReleaseFxScratchResources(void);

// View/proj matrices (defined in pipelinecommon.cpp, no header extern)
extern D3DMATRIX &_RwD3D9D3D9ViewTransform;
extern D3DMATRIX &_RwD3D9D3D9ProjTransform;
