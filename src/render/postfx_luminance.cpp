// postfx_luminance.cpp - luminance measure/adapt targets + auto exposure (m0060 split).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "profiler.h"
void *luminanceReducePS, *luminanceAdaptPS;

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

// White-blowout audit (build 10): clear a raw RT surface to mid-grey luminance
// (~0.18 linear) with the full RT/DS/viewport save-restore dance. Used for the
// 8x8 measure target — a freshly created FP16 RT has UNDEFINED content, and
// the measure pass must never read-modify-write against garbage (with a
// replace blend the first draw overwrites it, but pinning a known start state
// removes the whole undefined-FP16 class the adapt targets already guard).
static void ClearRtToMidGrey(IDirect3DSurface9 *surf)
{
	if(!surf) return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	IDirect3DSurface9 *oldRT = NULL, *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);
	dev->SetRenderTarget(0, surf);
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
	dev->SetViewport(&vp);
	// 46/255 ~= 0.18 linear luminance — same neutral key as the adapt clears
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 46, 46, 46), 0.0f, 0);
	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
}

// --- [exposure] receipt: throttled GPU readback of the 1x1 adapt texel -------
// The adapted luminance lives in a D3DPOOL_DEFAULT FP16 RT (GPU-only by
// design — no CPU readback without a stall). For the white-blowout
// discrimination the task asked for, we pay a 1x1 GetRenderTargetData copy
// at most once every 2s (negligible stall) and mirror TonemapPass's
// key/autoExp math on the CPU so the receipt shows exactly what the tonemap
// WILL derive from this texel. FP16 decode: Inf -> +1e30, NaN -> -1e30
// (negative luminance is impossible in valid data, so the sign discriminates).
static IDirect3DDevice9 *s_adaptReadbackDev = NULL;   // device the staging tex belongs to
static IDirect3DTexture9 *g_adaptReadbackTex = NULL;  // SYSTEMMEM 1x1 staging copy
static IDirect3DSurface9 *g_adaptReadbackSurf = NULL;
static float  s_adaptedLum    = -1.0f; // last read adapted luminance (.r)
static float  s_autoExpRaw    = -1.0f; // key/lum mirrored from TonemapPass
static DWORD  s_adaptedStamp  = 0;     // GetTickCount of last successful read
static bool   s_adaptedValid  = false;

static float LumaHalfToFloat(unsigned short h)
{
	unsigned sign = (h >> 15) & 1;
	unsigned exp  = (h >> 10) & 0x1F;
	unsigned man  = h & 0x3FF;
	float out;
	if(exp == 0x1F)
		out = (man == 0) ? 1e30f : -1e30f;   // Inf / NaN sentinel
	else if(exp == 0)
		out = ldexpf((float)man * (1.0f/1024.0f), -14); // subnormal
	else
		out = ldexpf(1.0f + (float)man * (1.0f/1024.0f), (int)exp - 15);
	return sign ? -out : out;
}

// Copy the 1x1 adapt RT into the SYSMEM staging tex and decode .r. Throttled
// to one poll per 2s (wrap-safe unsigned delta). Fails soft on device loss.
static void PollAdaptedLum(IDirect3DSurface9 *adaptSurf)
{
	DWORD now = GetTickCount();
	if(s_adaptedStamp != 0 && (now - s_adaptedStamp) < 2000)
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev || !adaptSurf)
		return;
	// Device re-create retires the old SYSMEM staging tex with the old device.
	if(s_adaptReadbackDev != dev){
		if(g_adaptReadbackTex){ g_adaptReadbackTex->Release(); g_adaptReadbackTex = NULL; }
		if(g_adaptReadbackSurf){ g_adaptReadbackSurf->Release(); g_adaptReadbackSurf = NULL; }
		s_adaptReadbackDev = dev;
	}
	if(!g_adaptReadbackTex){
		if(FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM,
		                             &g_adaptReadbackTex, NULL)))
			return;
		g_adaptReadbackTex->GetSurfaceLevel(0, &g_adaptReadbackSurf);
	}
	if(!g_adaptReadbackSurf)
		return;
	if(FAILED(dev->GetRenderTargetData(adaptSurf, g_adaptReadbackSurf)))
		return; // device lost / transient — retry next poll
	D3DLOCKED_RECT lr;
	if(FAILED(g_adaptReadbackTex->LockRect(0, &lr, NULL, D3DLOCK_READONLY)) || !lr.pBits)
		return;
	// A16B16G16R16F memory order is R,G,B,A (little-endian) — .r is the first half
	unsigned short h = *(const unsigned short *)lr.pBits;
	g_adaptReadbackTex->UnlockRect(0);
	float adapted = LumaHalfToFloat(h);
	s_adaptedLum = adapted;
	// Mirror TonemapPass.hlsl :91-93 (lum floor, key, raw autoExp) so the
	// receipt shows the pre-clamp value the shader will clamp into c7.y/z.
	float lum = max(adapted, 1e-4f);
	float key = 1.03f - 2.0f / (2.0f + logf(lum + 1.0f) / logf(2.0f));
	s_autoExpRaw = key / lum;
	s_adaptedStamp = now;
	s_adaptedValid = true;
}

// [exposure] receipt getter — postfx_colourfilter.cpp's cf_exp line calls this
// right after RunFrameExposure in the same CF_Modern call, so the value is at
// most 2s old (ageMs reports the exact staleness). Returns false when no
// readback has succeeded yet (receipt prints adapted=0 adaptedAgeMs=-1).
bool PostfxGetAdaptedLum(float *adapted, float *autoExpRaw, int *ageMs)
{
	if(!s_adaptedValid)
		return false;
	if(adapted)    *adapted    = s_adaptedLum;
	if(autoExpRaw) *autoExpRaw = s_autoExpRaw;
	if(ageMs){
		DWORD now = GetTickCount();
		*ageMs = (int)(now - s_adaptedStamp);
	}
	return true;
}

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
		// White-blowout audit: kill the undefined-FP16 first-frame state (the
		// measure pass must never sample against garbage, even with a
		// replace blend on the very first draw).
		ClearRtToMidGrey(g_lumaMeasSurf);
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
	// Raw sampler CLAMP below bypasses the rw cache (no L2 twin); capture the
	// pre-pass stage-0 state so later WRAP-expecting passes are not left
	// sampling clamped. Restored after the draw on every path out of here.
	DWORD s0AddrU = D3DTADDRESS_WRAP, s0AddrV = D3DTADDRESS_WRAP;
	DWORD s0Min = D3DTEXF_LINEAR, s0Mag = D3DTEXF_LINEAR;
	bool s0SampSaved = false;
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
		dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &s0AddrU);
		dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &s0AddrV);
		dev->GetSamplerState(0, D3DSAMP_MINFILTER, &s0Min);
		dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &s0Mag);
		s0SampSaved = true;
		if((s0AddrU != D3DTADDRESS_WRAP || s0AddrV != D3DTADDRESS_WRAP ||
		    s0Min != D3DTEXF_LINEAR || s0Mag != D3DTEXF_LINEAR) &&
		   dbglog_throttle("pf_luma_samp"))
			dbglog("[PF] luma sampler capture != WRAP/LINEAR: u=%lu v=%lu min=%lu mag=%lu (restored after pass)",
				s0AddrU, s0AddrV, s0Min, s0Mag);
		dev->SetTexture(0, tex0);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	}

	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	// White-blowout audit: pin REPLACE blending for the luma draws. The
	// vanilla Im2D state block is replace-with-alpha-1 today, but these
	// targets are tiny FP16 RTs that are never cleared between frames — an
	// inherited additive blend (ONE/ONE) would accumulate every frame and
	// poison the adaptation (huge measured -> guard reset -> wrong key).
	// Restored by ImmediateModeRenderStatesReStore below (same bracket
	// contract DrawIVGrade relies on).
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	overrideIm2dPixelShader = ps;
	if(dbglog_throttle("pf_lumapass"))
		dbglog("[PF] luma pass: ps=%p dst=%p %dx%d src=%s RT/viewport+states restored on exit",
			ps, (void*)dst, w, h, sceneRaster ? "sceneRaster(FB)" : (tex0 ? "tex0" : "none"));
	PfBindPS(ps, sceneRaster ? "IDR_LUMINANCEREDUCE(253)" : "IDR_LUMINANCEADAPT(254)");
	PfDraw(4, 2);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6);
	overrideIm2dPixelShader = nil;

	CPostEffects::ImmediateModeRenderStatesReStore();

	// Raw sampler restore goes LAST: ReStore re-emits the rw-cached texture
	// filter through RwD3D9, which would clobber a raw min/mag restore done
	// before it (sampler state has no rw cache twin, L3 only).
	if(s0SampSaved){
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, s0AddrU);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, s0AddrV);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, s0Min);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, s0Mag);
	}

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
IDirect3DTexture9 *RunFrameExposure(float adaptSpeed)
{
	PF_SCOPE("RunFrameExposure");
	if(!EnsureLuminanceTargets())
		return NULL;

	if(dbglog_throttle("pf_exposure"))
		dbglog("[PF] RunFrameExposure: reducePS=%p(IDR_LUMINANCEREDUCE 253) adaptPS=%p(254) speed=%.3f flip=%d",
			luminanceReducePS, luminanceAdaptPS, adaptSpeed, g_lumaAdaptFlip);

	// 1) Full-res graded frame -> 8x8 luminance measure.
	// The measure quad must cover ONLY the front buffer's valid sub-rect:
	// colorfilterVerts map UV 0..1 across the whole padded 2048x2048 FB, so
	// ~56% of the taps land in the never-written pad region — the measured
	// luminance (and therefore the auto-exposure key) is diluted/offset by
	// garbage. Build an 8x8-position quad whose UVs span exactly
	// (screenW/fbW, screenH/fbH) — the valid 1920x1080 content region.
	RwIm2DVertex lumaQuad[4];
	int lumaScrW = 0, lumaScrH = 0;
	RwRaster *lumaFB = CPostEffects::pRasterFrontBuffer;
	if(!(GetScreenSize(&lumaScrW, &lumaScrH) && lumaFB &&
	     lumaFB->width > 0 && lumaFB->height > 0)){
		// White-blowout audit: NO valid-subrect quad -> NO measurement. The
		// old fallback measured with colorfilterVerts (UV 0..1 across the
		// whole padded 2048x2048 FB), so ~56% of the taps landed in the
		// never-written pad region — a garbage key (pad content is stale/
		// undefined). Returning NULL makes the caller fall back to the
		// timecycle exposure with c5.w=0 (colourfilter :639-643), which is
		// always safer than adapting to garbage.
		if(dbglog_throttle("pf_luma_skip"))
			dbglog("[PF] RunFrameExposure SKIP: no valid screen/FB subrect (scr=%dx%d fb=%p %dx%d) — TC exposure fallback",
				lumaScrW, lumaScrH, (void*)lumaFB,
				lumaFB ? lumaFB->width : 0, lumaFB ? lumaFB->height : 0);
		return NULL;
	}
	{
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
	}
	RenderLumaPass(g_lumaMeasSurf, 8, 8, luminanceReducePS,
	               CPostEffects::pRasterFrontBuffer, NULL, lumaQuad);

	// 2) 8x8 measure + previous 1x1 -> current 1x1 eye adaptation
	IDirect3DTexture9 *curTex  = g_lumaAdaptFlip ? g_lumaAdaptTexB : g_lumaAdaptTexA;
	IDirect3DSurface9 *curSurf = g_lumaAdaptFlip ? g_lumaAdaptSurfB : g_lumaAdaptSurfA;
	IDirect3DTexture9 *prevTex = g_lumaAdaptFlip ? g_lumaAdaptTexA : g_lumaAdaptTexB;

	IDirect3DDevice9 *dev = d3d9device;
	// Stage-0 CLAMP/LINEAR is owned by RenderLumaPass (it binds tex0 itself and
	// captures/restores the pre-pass state); only stage 1 is bound raw here.
	DWORD s1AddrU = D3DTADDRESS_WRAP, s1AddrV = D3DTADDRESS_WRAP;
	DWORD s1Min = D3DTEXF_LINEAR, s1Mag = D3DTEXF_LINEAR;
	bool s1SampSaved = false;
	if(dev){
		dev->SetTexture(0, g_lumaMeasTex);
		dev->SetTexture(1, prevTex);
		dev->GetSamplerState(1, D3DSAMP_ADDRESSU, &s1AddrU);
		dev->GetSamplerState(1, D3DSAMP_ADDRESSV, &s1AddrV);
		dev->GetSamplerState(1, D3DSAMP_MINFILTER, &s1Min);
		dev->GetSamplerState(1, D3DSAMP_MAGFILTER, &s1Mag);
		s1SampSaved = true;
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}
	float adaptP[4] = { adaptSpeed, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(0, adaptP, 1);

	RenderLumaPass(curSurf, 1, 1, luminanceAdaptPS, NULL, g_lumaMeasTex, colorfilterVerts);
	if(dev) dev->SetTexture(1, NULL);
	if(s1SampSaved){
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, s1AddrU);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, s1AddrV);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, s1Min);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, s1Mag);
	}

	// [exposure] receipt: throttled (2s) GPU readback of the texel just
	// written — curSurf holds THIS frame's adapted luminance. Must run before
	// the flip so the surface identity matches what the tonemap will bind.
	PollAdaptedLum(curSurf);

	g_lumaAdaptFlip ^= 1;
	if(dbglog_throttle("pf_exposure2"))
		dbglog("[PF] RunFrameExposure done: adaptTex=%p s1 unbound (tonemap binds it next)",
			(void*)curTex);
	return curTex;
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
	// [exposure] receipt staging copy (SYSMEM — released with the rest so a
	// device-lost/Reset cycle rebuilds it cleanly)
	if(g_adaptReadbackTex){ g_adaptReadbackTex->Release(); g_adaptReadbackTex = nil; }
	if(g_adaptReadbackSurf){ g_adaptReadbackSurf->Release(); g_adaptReadbackSurf = nil; }
	s_adaptReadbackDev = NULL;
	s_adaptedValid = false;
	s_adaptedLum = -1.0f;
	s_autoExpRaw = -1.0f;
	s_adaptedStamp = 0;
	g_lumaAdaptFlip = 0;
}
