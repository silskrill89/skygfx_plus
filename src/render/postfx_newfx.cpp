// postfx_newfx.cpp - W3456 post-effect passes: sun shafts, DOF, bloom,
// chromatic aberration, film grain, vignette, FXAA3 (m0060 split).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "profiler.h"

/////
///// New post-effect passes (W4/W5/W6 lanes) — DOF chain, LUT tonemap branch,
///// bloom pyramid, sun shafts, FXAA3, film grain, chromatic aberration,
///// vignette.
/////
///// Config gates (struct Config, skygfx.h — the config lane owns the keys):
/////   dofEnable/dofFocusDistance/dofFocusRange/dofBokehRadius/dofMaxBlur,
/////   tonemapOperator(0-5), bloomEnable/bloomQuality(0-3)/bloomThreshold/
/////   bloomIntensity, sunShaftsEnable/sunShaftsDensity/sunShaftsDecay,
/////   fxaaEnable, grainIntensity, caIntensity, vignetteIntensity.
///// Every pass: config gate first, dbglog_throttle("fx") enable log,
///// three-layer render-state doctrine (rw cache + raw D3D9 mirror +
///// ImmediateMode/RawGeom save-restore), guardedIm2DRender for the draws.
/////
///// Shader loading mirrors the SSAO/SMAA pattern (pipelinecommon.cpp
///// makePS/CreateShaders chokepoint) but is lazy/one-shot here so this file
///// binds independently of CreateShaders ordering.
/////

// Shared shader loader (pipelinecommon.cpp) — same chokepoint CreateShaders
// uses for every embedded shader (see buildingPipe.cpp's identical forward decl).
void makePS(int res, void **sh);

// Resource IDs 260-275 are reserved for the post-effect shader packs
// (resource.h/Resource.rc lane). That lane owns the canonical defines — fall
// back to the reserved numerics here so this file compiles and binds whether
// or not they have landed.
#ifndef IDR_DOF_COC
#define IDR_DOF_COC             265
#endif
#ifndef IDR_DOF_BOKEH
#define IDR_DOF_BOKEH           266
#endif
#ifndef IDR_DOF_TENT
#define IDR_DOF_TENT            267
#endif
#ifndef IDR_DOF_COMBINE
#define IDR_DOF_COMBINE         268
#endif
#ifndef IDR_TONEMAPLUT
#define IDR_TONEMAPLUT          269
#endif
#ifndef IDR_BLOOM_EXTRACT
#define IDR_BLOOM_EXTRACT       270
#endif
#ifndef IDR_BLOOM_DOWNSAMPLE
#define IDR_BLOOM_DOWNSAMPLE    271
#endif
#ifndef IDR_BLOOM_UPSAMPLE
#define IDR_BLOOM_UPSAMPLE      272
#endif
#ifndef IDR_BLOOM_COMPOSITE
#define IDR_BLOOM_COMPOSITE     273
#endif
#ifndef IDR_SUNSHAFTS
#define IDR_SUNSHAFTS           274
#endif
#ifndef IDR_FXAA3
#define IDR_FXAA3               260
#endif
#ifndef IDR_GRAIN
#define IDR_GRAIN               261
#endif
#ifndef IDR_CA
#define IDR_CA                  262
#endif
#ifndef IDR_VIGNETTE
#define IDR_VIGNETTE            263
#endif

struct FxShaders {
	void *dofCoC, *dofBokeh, *dofTent, *dofCombine;
	void *tonemapLUT;
	void *bloomExtract, *bloomDownsample, *bloomUpsample, *bloomComposite;
	void *sunShafts, *fxaa3, *grain, *ca, *vignette;
};
static FxShaders s_fx = { 0 };
static bool s_fxLoadTried = false;   // one-shot: makePS logs on every failure

static void FxLoadShaders(void)
{
	if(s_fxLoadTried)
		return;
	s_fxLoadTried = true;
	makePS(IDR_DOF_COC,          &s_fx.dofCoC);
	makePS(IDR_DOF_BOKEH,        &s_fx.dofBokeh);
	makePS(IDR_DOF_TENT,         &s_fx.dofTent);
	makePS(IDR_DOF_COMBINE,      &s_fx.dofCombine);
	makePS(IDR_TONEMAPLUT,       &s_fx.tonemapLUT);
	makePS(IDR_BLOOM_EXTRACT,    &s_fx.bloomExtract);
	makePS(IDR_BLOOM_DOWNSAMPLE, &s_fx.bloomDownsample);
	makePS(IDR_BLOOM_UPSAMPLE,   &s_fx.bloomUpsample);
	makePS(IDR_BLOOM_COMPOSITE,  &s_fx.bloomComposite);
	makePS(IDR_SUNSHAFTS,        &s_fx.sunShafts);
	makePS(IDR_FXAA3,            &s_fx.fxaa3);
	makePS(IDR_GRAIN,            &s_fx.grain);
	makePS(IDR_CA,               &s_fx.ca);
	makePS(IDR_VIGNETTE,         &s_fx.vignette);
	dbglog("[FX] shaders: dof=%p/%p/%p/%p lut=%p bloom=%p/%p/%p/%p shafts=%p fxaa=%p grain=%p ca=%p vig=%p",
		s_fx.dofCoC, s_fx.dofBokeh, s_fx.dofTent, s_fx.dofCombine,
		s_fx.tonemapLUT, s_fx.bloomExtract, s_fx.bloomDownsample,
		s_fx.bloomUpsample, s_fx.bloomComposite,
		s_fx.sunShafts, s_fx.fxaa3, s_fx.grain, s_fx.ca, s_fx.vignette);
}

// TonemapLUT handle for the ColourFilter_Modern tonemap branch (that seam
// sits above this section — see the forward decl at the top of the file).
void *FxTonemapLutShader(void)
{
	FxLoadShaders();
	return s_fx.tonemapLUT;
}

// Device-Reset seam (depthhook.cpp's post-Reset block calls this via a
// function-local extern): drop every cached PS handle and the one-shot load
// latch so the next pass re-creates them through FxLoadShaders/makePS. A
// failed reload simply leaves the handle NULL and every pass hits its
// existing NULL-handle skip — fail-open, no crash. Load logic untouched.
void FxResetShaderLatches(void)
{
	s_fx.dofCoC = s_fx.dofBokeh = s_fx.dofTent = s_fx.dofCombine = NULL;
	s_fx.tonemapLUT = NULL;
	s_fx.bloomExtract = s_fx.bloomDownsample = s_fx.bloomUpsample = s_fx.bloomComposite = NULL;
	s_fx.sunShafts = s_fx.fxaa3 = s_fx.grain = s_fx.ca = s_fx.vignette = NULL;
	s_fxLoadTried = false;
}

// ============================================================================
// CONTRACT ASSUMPTIONS — the authoritative register layouts live in each
// shader's own file header (parallel shader lane). Bloom_* layouts below are
// taken VERBATIM from the landed Bloom_*.hlsl headers; the rest were bound
// before those files existed and MUST be reconciled against their headers:
//
//   DOF_CoC      s0 = scene (front buffer), s1 = depth (g_ssaoDepthTex)
//                c0 = {focusDistance, focusRange, maxBlur, 0}
//                c1 = {nearPlane, farPlane, 1/halfW, 1/halfH}
//                out.rgb = scene, out.a = CoC (0..1)
//   DOF_Bokeh    s0 = CoC half-res; c0 = {bokehRadius, 1/halfW, 1/halfH, 0}
//   DOF_Tent     s0 = bokeh half-res; c0 = {1/halfW, 1/halfH, 0, 0}
//   DOF_Combine  s0 = scene, s1 = tent half-res, s2 = CoC half-res
//                c0 = {maxBlur, 1/w, 1/h, 0}
//
//   TonemapLUT   SAME s0/s1 + c5-c8 contract as TonemapPass (the c5-c8
//                uploads and the s1 auto-exposure bind in ColourFilter_Modern
//                stay valid for the LUT pass), PLUS c9 = {operator, 0, 0, 0}
//                with operator 1-5 (ACES 1.3/2.0, AgX, Frostbite, GT7).
//
//   SunShafts    GodRays.hlsl-compatible (the in-repo template the lane most
//                likely follows): c0 = {sunScreenX, sunScreenY, 0, 0},
//                c1 = {exposure, decay, density, weight}, c2 = {numSamples,0,0,0}.
//                ADDITIVE rays-only output contract like GodRays (drawn
//                ONE/ONE — safe degrade if the shader is composite instead).
//                exposure/weight/samples use the GodRays defaults below.
//
//   Grain        plan-doc §6.3: s0 = scene, c0 = {intensity, frameIndex,
//                luminanceScale, coloredGrain}
//   CA           plan-doc §6.2: s0 = scene, c0 = {intensity, redOffset,
//                blueOffset, radialFalloff}, c1 = {maxOffset, softKnee,
//                aspect, texelW}, c2 = {texelH, 0, 0, 0}
//   Vignette     plan-doc §6.1: s0 = scene, c0 = {intensity, radius,
//                softness, roundness}
//   FXAA3        s0 = scene, c0 = {1/w, 1/h, 0, 0} (FXAA3_11.h quality
//                settings assumed compile-time in the shader)
//
//   Bloom_Extract/Downsample/Upsample/Composite: EXACT contracts from the
//   landed shader headers (see comments at each bind site below).
// ============================================================================

// SunShafts assumed defaults (GodRays.hlsl knob values; only density/decay
// have config keys in this lane).
static const float kSunShaftsExposure = 0.0034f;
static const float kSunShaftsWeight   = 1.0f;
static const float kSunShaftsSamples  = 20.0f;

// --- Scratch render targets (D3DPOOL_DEFAULT, recreated on res change) ------
// DOF: two half-res intermediates (CoC, bokeh) + one half-res tent blur.
// Bloom: mip pyramid, mip0 = half-res, up to 6 levels.
// Float-first with A8R8G8B8 fallback (port-plan doctrine: don't silently
// clip HDR in the bloom/DOF chain where the card supports float RTs).
#define FX_MAX_BLOOM_MIPS 6
static IDirect3DTexture9  *g_fxDofCoCTex = NULL;    static IDirect3DSurface9 *g_fxDofCoCSurf = NULL;
static IDirect3DTexture9  *g_fxDofBokehTex = NULL;  static IDirect3DSurface9 *g_fxDofBokehSurf = NULL;
static IDirect3DTexture9  *g_fxDofTentTex = NULL;   static IDirect3DSurface9 *g_fxDofTentSurf = NULL;
static int s_fxDofW = 0, s_fxDofH = 0;
static IDirect3DTexture9  *g_fxBloomTex[FX_MAX_BLOOM_MIPS] = { NULL };
static IDirect3DSurface9  *g_fxBloomSurf[FX_MAX_BLOOM_MIPS] = { NULL };
static int s_fxBloomW[FX_MAX_BLOOM_MIPS] = { 0 }, s_fxBloomH[FX_MAX_BLOOM_MIPS] = { 0 };
static bool s_fxFloatRT = true;

static bool FxCreateRT(IDirect3DTexture9 **tex, IDirect3DSurface9 **surf, int w, int h, const char *tag)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !tex || !surf || w < 1 || h < 1)
		return false;
	if(*tex && *surf)
		return true;

	HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		s_fxFloatRT ? D3DFMT_A16B16G16R16F : D3DFMT_A8R8G8B8,
		D3DPOOL_DEFAULT, tex, NULL);
	if(FAILED(hr) && s_fxFloatRT){
		s_fxFloatRT = false;
		dbglog("[FX] %s: float RT unsupported (hr=0x%08X), falling back to A8R8G8B8",
			tag, (unsigned)hr);
		hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
			D3DPOOL_DEFAULT, tex, NULL);
	}
	if(FAILED(hr)){
		dbglog("[FX] %s: RT create FAILED %dx%d hr=0x%08X", tag, w, h, (unsigned)hr);
		*tex = NULL;
		*surf = NULL;
		return false;
	}
	if(FAILED((*tex)->GetSurfaceLevel(0, surf)) || !*surf){
		dbglog("[FX] %s: GetSurfaceLevel FAILED %dx%d", tag, w, h);
		(*tex)->Release();
		*tex = NULL;
		*surf = NULL;
		return false;
	}
	return true;
}

static void FxReleaseDofRTs(void)
{
	if(g_fxDofCoCTex){ g_fxDofCoCTex->Release(); g_fxDofCoCTex = NULL; }
	if(g_fxDofCoCSurf){ g_fxDofCoCSurf->Release(); g_fxDofCoCSurf = NULL; }
	if(g_fxDofBokehTex){ g_fxDofBokehTex->Release(); g_fxDofBokehTex = NULL; }
	if(g_fxDofBokehSurf){ g_fxDofBokehSurf->Release(); g_fxDofBokehSurf = NULL; }
	if(g_fxDofTentTex){ g_fxDofTentTex->Release(); g_fxDofTentTex = NULL; }
	if(g_fxDofTentSurf){ g_fxDofTentSurf->Release(); g_fxDofTentSurf = NULL; }
	s_fxDofW = 0;
	s_fxDofH = 0;
}

static void FxReleaseBloomRTs(void)
{
	for(int i = 0; i < FX_MAX_BLOOM_MIPS; i++){
		if(g_fxBloomTex[i]){ g_fxBloomTex[i]->Release(); g_fxBloomTex[i] = NULL; }
		if(g_fxBloomSurf[i]){ g_fxBloomSurf[i]->Release(); g_fxBloomSurf[i] = NULL; }
		s_fxBloomW[i] = 0;
		s_fxBloomH[i] = 0;
	}
}

// Released from ReleaseDefaultPoolResources (device reset / shutdown).
void ReleaseFxScratchResources(void)
{
	FxReleaseDofRTs();
	FxReleaseBloomRTs();
}

static bool FxEnsureDofRTs(int hw, int hh)
{
	if(g_fxDofCoCTex && g_fxDofBokehTex && g_fxDofTentTex &&
	   s_fxDofW == hw && s_fxDofH == hh)
		return true;
	FxReleaseDofRTs();
	if(!FxCreateRT(&g_fxDofCoCTex, &g_fxDofCoCSurf, hw, hh, "dof_coc")) return false;
	if(!FxCreateRT(&g_fxDofBokehTex, &g_fxDofBokehSurf, hw, hh, "dof_bokeh")){ FxReleaseDofRTs(); return false; }
	if(!FxCreateRT(&g_fxDofTentTex, &g_fxDofTentSurf, hw, hh, "dof_tent")){ FxReleaseDofRTs(); return false; }
	s_fxDofW = hw;
	s_fxDofH = hh;
	return true;
}

static bool FxEnsureBloomRTs(int hw, int hh, int mips)
{
	bool ok = (g_fxBloomTex[0] != NULL);
	for(int i = 0; ok && i < mips; i++){
		int mw = max(1, hw >> i), mh = max(1, hh >> i);
		if(!g_fxBloomTex[i] || s_fxBloomW[i] != mw || s_fxBloomH[i] != mh)
			ok = false;
	}
	if(ok)
		return true;
	FxReleaseBloomRTs();
	for(int i = 0; i < mips; i++){
		int mw = max(1, hw >> i), mh = max(1, hh >> i);
		if(!FxCreateRT(&g_fxBloomTex[i], &g_fxBloomSurf[i], mw, mh, "bloom_mip")){
			FxReleaseBloomRTs();
			return false;
		}
		s_fxBloomW[i] = mw;
		s_fxBloomH[i] = mh;
	}
	return true;
}

// Project the sun direction to screen UV — same idiom as DrawGodRays (the
// matrix rows there are the proven convention; kept local so the existing
// pass stays untouched). Returns false when the sun is behind the camera or
// far enough off screen that scattering is invisible.
static bool FxGetSunScreenPos(float *outX, float *outY)
{
	float sunD[3];
	GetSunDirection(sunD[0], sunD[1], sunD[2]);
	D3DMATRIX &viewMat = _RwD3D9D3D9ViewTransform;
	D3DMATRIX &projMat = _RwD3D9D3D9ProjTransform;
	float vx = sunD[0] * viewMat.m[0][0] + sunD[1] * viewMat.m[1][0] + sunD[2] * viewMat.m[2][0];
	float vy = sunD[0] * viewMat.m[0][1] + sunD[1] * viewMat.m[1][1] + sunD[2] * viewMat.m[2][1];
	float vz = sunD[0] * viewMat.m[0][2] + sunD[1] * viewMat.m[1][2] + sunD[2] * viewMat.m[2][2];
	float cx = vx * projMat.m[0][0];
	float cy = vy * projMat.m[1][1];
	float cz = vz * projMat.m[2][2];
	float cw = vz * projMat.m[3][2];
	float sx = 0.5f, sy = 0.5f;
	if(cw != 0.0f){
		sx = (cx / cw) * 0.5f + 0.5f;
		sy = -(cy / cw) * 0.5f + 0.5f;
	}
	if(cz < 0.0f || sx < -0.5f || sx > 1.5f || sy < -0.5f || sy > 1.5f)
		return false;
	*outX = sx;
	*outY = sy;
	return true;
}

// Draw a fullscreen pass into a raw D3D9 RT surface. Mirrors the proven
// RenderLumaPass idiom (RT/viewport swap, ImmediateMode save-restore) plus:
// optional extra stage-1 bind (DOF depth) and additive blending (bloom
// upsample accumulation — the Bloom_Upsample.hlsl contract explicitly makes
// the accumulate the C++ lane's job via ONE/ONE).
static void FxPassToRT(IDirect3DSurface9 *dst, int w, int h, void *ps,
                       RwRaster *srcRas, IDirect3DTexture9 *src0,
                       IDirect3DTexture9 *src1, bool additive, const char *tag)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !dst || !ps || w < 1 || h < 1)
		return;

	// Blend-leak fix: snapshot the pre-pass device blend block and restore it
	// through all three layers at cleanup (same treatment as FxFramePass /
	// postfx_ssao.cpp:676-679). The non-additive branch writes ONE/ZERO
	// (replace) below; without a raw snapshot the rw-cache-only cleanup left
	// ONE/ZERO in the driver cache + device, which later passes'
	// RestoreRawGeomStates entry snapshots kept re-applying until PostFx.exit
	// — coronas drawn in between rendered as opaque black squares.
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom);

	// Raw sampler CLAMP below bypasses the rw cache (no L2 twin); capture the
	// pre-pass state per touched stage so later WRAP-expecting passes are not
	// left sampling clamped. Restored in the shared cleanup below.
	DWORD s0AddrU = D3DTADDRESS_WRAP, s0AddrV = D3DTADDRESS_WRAP;
	DWORD s0Min = D3DTEXF_LINEAR, s0Mag = D3DTEXF_LINEAR;
	DWORD s1AddrU = D3DTADDRESS_WRAP, s1AddrV = D3DTADDRESS_WRAP;
	DWORD s1Min = D3DTEXF_LINEAR, s1Mag = D3DTEXF_LINEAR;
	bool s0SampSaved = false, s1SampSaved = false;

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

	if(srcRas){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)srcRas);
		dev->SetTexture(0, NULL);
	}else if(src0){
		dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &s0AddrU);
		dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &s0AddrV);
		dev->GetSamplerState(0, D3DSAMP_MINFILTER, &s0Min);
		dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &s0Mag);
		s0SampSaved = true;
		dev->SetTexture(0, src0);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}
	if(src1){
		dev->GetSamplerState(1, D3DSAMP_ADDRESSU, &s1AddrU);
		dev->GetSamplerState(1, D3DSAMP_ADDRESSV, &s1AddrV);
		dev->GetSamplerState(1, D3DSAMP_MINFILTER, &s1Min);
		dev->GetSamplerState(1, D3DSAMP_MAGFILTER, &s1Mag);
		s1SampSaved = true;
		dev->SetTexture(1, src1);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}
	if(((s0SampSaved && (s0AddrU != D3DTADDRESS_WRAP || s0AddrV != D3DTADDRESS_WRAP ||
	                     s0Min != D3DTEXF_LINEAR || s0Mag != D3DTEXF_LINEAR)) ||
	    (s1SampSaved && (s1AddrU != D3DTADDRESS_WRAP || s1AddrV != D3DTADDRESS_WRAP ||
	                     s1Min != D3DTEXF_LINEAR || s1Mag != D3DTEXF_LINEAR))) &&
	   dbglog_throttle("pf_fxpass_samp"))
		dbglog("[FX] pass->RT %s sampler capture != WRAP/LINEAR (restored): s0 u=%lu v=%lu min=%lu mag=%lu | s1 u=%lu v=%lu min=%lu mag=%lu",
			tag, s0AddrU, s0AddrV, s0Min, s0Mag, s1AddrU, s1AddrV, s1Min, s1Mag);

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	// Three-layer blend doctrine: rw cache FIRST (RwIm2D re-emits the cached
	// VERTEXALPHAENABLE/SRCBLEND/DESTBLEND at draw time and would clobber
	// raw-only factors), raw D3D9 mirror after — see the GodRays additive note.
	if(additive){
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	}else{
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	SetupFullscreenQuad((float)w, (float)h);
	if(dbglog_throttle("pf_fxpass"))
		dbglog("[FX] pass->RT %s: ps=%p dst=%p %dx%d src0=%s s1=%p add=%d",
			tag, ps, (void*)dst, w, h,
			srcRas ? "raster(FB)" : (src0 ? "tex0" : "none"), (void*)src1, (int)additive);
	PfBindPS(ps, tag);
	PfDraw(4, 2);
	guardedIm2DRender(ps, rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6, tag);

	// Cleanup — blend OFF through the RW cache FIRST (guaranteed transition →
	// pushes L2+L3). The old order — L2-only ALPHABLENDENABLE FALSE first, rw
	// VERTEXALPHA TRUE last — left the device blend OFF under a cache that
	// said ON whenever the additive branch ran (the TRUE no-oped against the
	// cache this pass set at entry), so later consumers whose TRUE set
	// no-oped drew REPLACE (black squares). No TRUE re-set: the next consumer
	// (another FxPassToRT/FxFramePass or the game) always sets its own blend
	// state on entry. Factors go back through the cache too.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	CPostEffects::ImmediateModeRenderStatesReStore();
	// 3-layer restore of the SAVED pre-pass blend block (rw cache + RwD3D9
	// driver cache + raw device) — mirrors FxFramePass / postfx_ssao.cpp.
	if(rawGeomSaved)
		RestoreRawGeomStates(rawGeom);

	// Restore the captured raw sampler state (L3 has no rw twin) before the
	// texture unbind, then clear raw sampler binds AND the rw cache's
	// stage-0 raster (RenderLumaPass doctrine: a leaked raw bind leaves the
	// next pass sampling NULL).
	if(s0SampSaved){
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, s0AddrU);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, s0AddrV);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, s0Min);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, s0Mag);
	}
	if(s1SampSaved){
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, s1AddrU);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, s1AddrV);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, s1Min);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, s1Mag);
	}
	dev->SetTexture(0, NULL);
	dev->SetTexture(1, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
	if(dbglog_throttle("pf_fxpass2"))
		dbglog("[FX] pass->RT %s done: RT+DS+viewport restored, s0/s1+rw raster cleared",
			tag);
}

// Full-frame pass onto the CURRENT RT (the camera raster): s0 = front buffer
// (colorfilterVerts map UV 0..1 over the padded FB — the ColourFilter_Modern/
// DrawGodRays sampling convention), optional raw s1/s2 for combine/composite
// passes. Additive only for the GodRays-contract sun shafts; every other
// consumer replaces the frame (their shaders emit full-frame colour).
static void FxFramePass(void *ps, IDirect3DTexture9 *src1, IDirect3DTexture9 *src2,
                        bool additive, const char *tag)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !ps || !CPostEffects::pRasterFrontBuffer)
		return;

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom);
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	if(additive){
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	}else{
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
	if(src1){
		dev->SetTexture(1, src1);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}
	if(src2){
		dev->SetTexture(2, src2);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}

	if(dbglog_throttle("pf_fxframe"))
		dbglog("[FX] pass->cameraRT %s: ps=%p s0=pRasterFrontBuffer(%p) s1=%p s2=%p add=%d",
			tag, ps, (void*)CPostEffects::pRasterFrontBuffer, (void*)src1, (void*)src2, (int)additive);
	PfBindPS(ps, tag);
	PfDraw(4, 2);
	guardedIm2DRender(ps, rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6, tag);

	if(src1) dev->SetTexture(1, NULL);
	if(src2) dev->SetTexture(2, NULL);
	// Cleanup — blend OFF through the RW cache FIRST (guaranteed transition →
	// pushes L2+L3). The old order — L2-only ALPHABLENDENABLE FALSE first, rw
	// VERTEXALPHA TRUE last — left the device blend OFF under a cache that
	// said ON whenever the additive branch ran (the TRUE no-oped against the
	// cache this pass set at entry), so later consumers whose TRUE set
	// no-oped drew REPLACE (black squares). No TRUE re-set: RestoreRawGeomStates
	// below restores the pre-pass snapshot through all three layers, and the
	// next consumer pass sets its own blend state on entry.
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
	if(dbglog_throttle("pf_fxframe2"))
		dbglog("[FX] pass->cameraRT %s done: ImmediateMode+RawGeom restored, fog/z re-enabled",
			tag);
}

// 1) Sun shafts — pre-tonemap, additive rays (GodRays-compatible contract;
// see the CONTRACT ASSUMPTIONS block).
void DrawSunShafts(void)
{
	PF_SCOPE("SunShaftsPass");
	FxLoadShaders();
	if(!config->sunShaftsEnable || !s_fx.sunShafts)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;

	float sunX = 0.5f, sunY = 0.5f;
	if(!FxGetSunScreenPos(&sunX, &sunY))
		return;

	if(dbglog_throttle("fx"))
		dbglog("[FX] SunShafts enable: sun=(%.2f,%.2f) density=%.2f decay=%.2f ps=%p",
			sunX, sunY, config->sunShaftsDensity, config->sunShaftsDecay, s_fx.sunShafts);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	// Sanitize like DrawGodRays: zero-init config must not collapse the pass.
	float density = (config->sunShaftsDensity > 0.0f) ? config->sunShaftsDensity : 0.84f;
	float decay   = (config->sunShaftsDecay > 0.0f && config->sunShaftsDecay <= 1.0f) ? config->sunShaftsDecay : 1.0f;
	float c0[4] = { sunX, sunY, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);
	float c1[4] = { kSunShaftsExposure, decay, density, kSunShaftsWeight };
	RwD3D9SetPixelShaderConstant(1, c1, 1);
	float c2[4] = { kSunShaftsSamples, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	FxFramePass(s_fx.sunShafts, NULL, NULL, true, "fx_sunshafts");
}

// 2) DOF — CoC -> bokeh -> tent on half-res intermediates, combine full-res.
//    Depth source = g_ssaoDepthTex (INTZ/depthhook alias); without it CoC
//    cannot be computed and the pass degrades to off.
void DrawDOF(void)
{
	PF_SCOPE("DOFPass");
	FxLoadShaders();
	if(!config->dofEnable)
		return;
	if(!s_fx.dofCoC || !s_fx.dofBokeh || !s_fx.dofTent || !s_fx.dofCombine)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(!camRas)
		return;
	int w = camRas->width, h = camRas->height;
	if(w < 1 || h < 1)
		return;
	if(!g_ssaoDepthTex){
		if(dbglog_throttle("fx"))
			dbglog("[FX] DOF skip: g_ssaoDepthTex NULL (no depth for CoC)");
		return;
	}
	int hw = max(1, w / 2), hh = max(1, h / 2);
	if(!FxEnsureDofRTs(hw, hh))
		return;

	if(dbglog_throttle("fx"))
		dbglog("[FX] DOF enable: focus=%.1f range=%.1f radius=%.2f maxBlur=%.2f ps=%p/%p/%p/%p",
			config->dofFocusDistance, config->dofFocusRange, config->dofBokehRadius,
			config->dofMaxBlur, s_fx.dofCoC, s_fx.dofBokeh, s_fx.dofTent, s_fx.dofCombine);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	// CoC prefilter: scene + depth -> half-res (rgb = scene, a = CoC).
	float cocP[4] = { config->dofFocusDistance, config->dofFocusRange, config->dofMaxBlur, 0.0f };
	RwD3D9SetPixelShaderConstant(0, cocP, 1);
	RwCamera *cam = Scene.camera;
	float cocCam[4] = {
		cam->nearPlane, cam->farPlane,
		1.0f / max((float)hw, 1.0f), 1.0f / max((float)hh, 1.0f)
	};
	RwD3D9SetPixelShaderConstant(1, cocCam, 1);
	FxPassToRT(g_fxDofCoCSurf, hw, hh, s_fx.dofCoC,
		CPostEffects::pRasterFrontBuffer, NULL, g_ssaoDepthTex, false, "fx_dof_coc");

	// Bokeh gather on the half-res CoC buffer.
	float bokP[4] = { config->dofBokehRadius, 1.0f / max((float)hw, 1.0f), 1.0f / max((float)hh, 1.0f), 0.0f };
	RwD3D9SetPixelShaderConstant(0, bokP, 1);
	// c3 = feather - W3456 gap fix: the Bokeh pass reads c3 but the C++ side
	// never uploaded it (stale register garbage). Neutral default 1.0;
	// DOF_Bokeh.hlsl clamps it to [0.25, 4].
	float bokFeather[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(3, bokFeather, 1);
	FxPassToRT(g_fxDofBokehSurf, hw, hh, s_fx.dofBokeh,
		NULL, g_fxDofCoCTex, NULL, false, "fx_dof_bokeh");

	// Tent postfilter.
	float tentP[4] = { 1.0f / max((float)hw, 1.0f), 1.0f / max((float)hh, 1.0f), 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, tentP, 1);
	FxPassToRT(g_fxDofTentSurf, hw, hh, s_fx.dofTent,
		NULL, g_fxDofBokehTex, NULL, false, "fx_dof_tent");

	// Combine: full scene + tent blur + CoC -> camera raster.
	float combP[4] = { config->dofMaxBlur, 1.0f / max((float)w, 1.0f), 1.0f / max((float)h, 1.0f), 0.0f };
	RwD3D9SetPixelShaderConstant(0, combP, 1);
	FxFramePass(s_fx.dofCombine, g_fxDofTentTex, g_fxDofCoCTex, false, "fx_dof_combine");
}

// 3) Bloom — extract -> downsample xN -> upsample/accumulate chain -> composite.
//    bloomQuality 0-3 selects 2-6 mips (contract per the landed Bloom_*.hlsl
//    headers: extract c0=(threshold, softThreshold, karisOn, invSizePx);
//    downsample c0=(mipLevel, filterRadius, invSizePx, unused) with mipLevel
//    0 = first (Karis) downsample; upsample c0=(filterRadius, invSizePx, 0, 0)
//    drawn ONE/ONE into the larger mip; composite c0=(intensity, saturation,
//    blendMode, unused) with s0 = scene, s1 = fully upsampled pyramid).
void DrawBloom(void)
{
	PF_SCOPE("BloomPass");
	FxLoadShaders();
	if(!config->bloomEnable)
		return;
	if(!s_fx.bloomExtract || !s_fx.bloomDownsample || !s_fx.bloomUpsample || !s_fx.bloomComposite)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(!camRas)
		return;
	int w = camRas->width, h = camRas->height;
	if(w < 1 || h < 1)
		return;
	int mips = config->bloomQuality + 2;   // 0=2 mips .. 3=6 mips
	mips = max(2, min(FX_MAX_BLOOM_MIPS, mips));
	int hw = max(1, w / 2), hh = max(1, h / 2);
	if(!FxEnsureBloomRTs(hw, hh, mips))
		return;

	float threshold = (config->bloomThreshold > 0.0f) ? config->bloomThreshold : 1.0f;
	float intensity = (config->bloomIntensity > 0.0f) ? config->bloomIntensity : 0.8f;

	if(dbglog_throttle("fx"))
		dbglog("[FX] Bloom enable: mips=%d threshold=%.2f intensity=%.2f ps=%p/%p/%p/%p",
			mips, threshold, intensity, s_fx.bloomExtract, s_fx.bloomDownsample,
			s_fx.bloomUpsample, s_fx.bloomComposite);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	// Extract: scene -> mip0 (half-res). soft knee 0.5 + Karis anti-firefly
	// (no config keys in this lane — the shader header owns the semantics).
	float extP[4] = { threshold, 0.5f, 1.0f, 1.0f / max((float)w, 1.0f) };
	RwD3D9SetPixelShaderConstant(0, extP, 1);
	FxPassToRT(g_fxBloomSurf[0], s_fxBloomW[0], s_fxBloomH[0], s_fx.bloomExtract,
		CPostEffects::pRasterFrontBuffer, NULL, NULL, false, "fx_bloom_extract");

	// Downsample chain: mip i-1 -> mip i (downsample mipLevel = i-1).
	for(int i = 1; i < mips; i++){
		float downP[4] = {
			(float)(i - 1), 1.0f,
			1.0f / max((float)s_fxBloomW[i - 1], 1.0f), 0.0f
		};
		RwD3D9SetPixelShaderConstant(0, downP, 1);
		FxPassToRT(g_fxBloomSurf[i], s_fxBloomW[i], s_fxBloomH[i], s_fx.bloomDownsample,
			NULL, g_fxBloomTex[i - 1], NULL, false, "fx_bloom_down");
	}

	// Upsample + additive accumulate, small -> large (ONE/ONE per the
	// Bloom_Upsample.hlsl contract: the accumulation is THIS lane's job).
	for(int i = mips - 1; i >= 1; i--){
		float upP[4] = {
			1.0f, 1.0f / max((float)s_fxBloomW[i], 1.0f), 0.0f, 0.0f
		};
		RwD3D9SetPixelShaderConstant(0, upP, 1);
		FxPassToRT(g_fxBloomSurf[i - 1], s_fxBloomW[i - 1], s_fxBloomH[i - 1], s_fx.bloomUpsample,
			NULL, g_fxBloomTex[i], NULL, true, "fx_bloom_up");
	}

	// Composite: scene + pyramid (mip0) -> camera raster. Additive blend
	// mode 0, saturation 1 (only intensity has a config key in this lane).
	float compP[4] = { intensity, 1.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, compP, 1);
	FxFramePass(s_fx.bloomComposite, g_fxBloomTex[0], NULL, false, "fx_bloom_composite");
}

// 5) Chromatic aberration — plan-doc §6.2 contract. redOffset/blueOffset/
//    radialFalloff/maxOffset/softKnee have no config keys in this lane:
//    neutral per-channel offsets with a 2.5px radial cap.
void DrawChromaticAberration(void)
{
	PF_SCOPE("CAPass");
	FxLoadShaders();
	if(config->caIntensity <= 0.0f || !s_fx.ca)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;
	int w = 0, h = 0;
	GetScreenSize(&w, &h);
	if(w < 1 || h < 1)
		return;

	if(dbglog_throttle("fx"))
		dbglog("[FX] CA enable: intensity=%.3f ps=%p", config->caIntensity, s_fx.ca);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	float c0[4] = { config->caIntensity, 1.0f, 1.0f, 1.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);
	float c1[4] = {
		2.5f, 0.5f,
		(float)w / max((float)h, 1.0f),
		1.0f / max((float)w, 1.0f)
	};
	RwD3D9SetPixelShaderConstant(1, c1, 1);
	float c2[4] = { 1.0f / max((float)h, 1.0f), 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(2, c2, 1);

	FxFramePass(s_fx.ca, NULL, NULL, false, "fx_ca");
}

// 6) Film grain — plan-doc §6.3 contract (procedural IGN). The legacy
//    grainFilter path (CPostEffects::Grain / Grain_PS2) is untouched.
void DrawFilmGrain(void)
{
	// Self-gating master check (grainEnable, skygfx.h:652 "0=disable film
	// grain"). This is the same master switch the legacy Grain_PS2 lane uses;
	// it used to be enforced only by the two postfx.cpp call sites, so any
	// other caller ran the full pass with the master switch off. FIRST
	// statement: no profiler scope, shader load, state save, texture bind or
	// RT work happens when the pass cannot contribute.
	// The handle check stays below FxLoadShaders(): s_fx.grain is populated by
	// that one-shot lazy loader, so testing it here would deadlock the load
	// whenever grain is the first (or only) W3456 pass to run this frame.
	if(!config->grainEnable || config->grainIntensity <= 0.0f)
		return;
	PF_SCOPE("GrainPass");
	FxLoadShaders();
	if(!s_fx.grain)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;

	static unsigned int s_grainFrame = 0;
	s_grainFrame++;

	if(dbglog_throttle("fx"))
		dbglog("[FX] Grain enable: intensity=%.3f ps=%p", config->grainIntensity, s_fx.grain);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	float c0[4] = {
		config->grainIntensity,
		(float)(s_grainFrame % 1024),
		1.0f,   // luminanceScale (neutral)
		0.0f    // coloredGrain off
	};
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	FxFramePass(s_fx.grain, NULL, NULL, false, "fx_grain");
}

// 7) Vignette — plan-doc §6.1 contract. Only vignetteIntensity has a config
//    key in this lane; radius/softness/roundness use neutral defaults.
void DrawVignette(void)
{
	PF_SCOPE("VignettePass");
	FxLoadShaders();
	if(config->vignetteIntensity <= 0.0f || !s_fx.vignette)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;

	if(dbglog_throttle("fx"))
		dbglog("[FX] Vignette enable: intensity=%.3f ps=%p", config->vignetteIntensity, s_fx.vignette);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	float c0[4] = { config->vignetteIntensity, 0.85f, 0.45f, 1.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	FxFramePass(s_fx.vignette, NULL, NULL, false, "fx_vignette");
}

// 8) FXAA3 — late AA slot (DrawFinalEffects), same slot family as SMAA.
//    Mutual exclusion: SMAA wins when both are enabled (logged once).
void DrawFXAA3(void)
{
	PF_SCOPE("FXAA3Pass");
	FxLoadShaders();
	if(!config->fxaaEnable)
		return;
	if(config->smaaEnable && SMAA_Edge){
		static bool s_suppressLogged = false;
		if(!s_suppressLogged){
			s_suppressLogged = true;
			dbglog("[FX] FXAA3 suppressed: SMAA enabled (SMAA wins when both are on)");
		}
		return;
	}
	if(!s_fx.fxaa3)
		return;
	if(!CPostEffects::pRasterFrontBuffer || IsGameInMenuOrPaused())
		return;
	int w = 0, h = 0;
	GetScreenSize(&w, &h);
	if(w < 1 || h < 1)
		return;

	if(dbglog_throttle("fx"))
		dbglog("[FX] FXAA3 enable: %dx%d ps=%p", w, h, s_fx.fxaa3);

	CPostEffects::UpdateFrontBuffer();   // s0 = current scene

	// Contract assumption: quality settings are compile-time in the shader
	// (FXAA3_11.h style); only the texel size is bound here.
	float c0[4] = { 1.0f / max((float)w, 1.0f), 1.0f / max((float)h, 1.0f), 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, c0, 1);

	FxFramePass(s_fx.fxaa3, NULL, NULL, false, "fx_fxaa3");
}
