#include "skygfx.h"
#include "chars.h"
#include <string>
#include <map>

// ============================================================
// BRDF database for character parts
// ============================================================
PartSSSParams g_partSSSParams[NUM_PARTTYPES] = {
	// roughness, reflectance, subsurface, specIntensity, blurStrength, blurRadius
	{ 0.0f,  0.0f,  0.0f,  0.0f, 0.0f,  0.0f  },  // NONE
	{ 0.55f, 0.04f, 0.80f, 0.6f, 0.35f, 4.0f  },  // SKIN   - soft scattering, warm
	{ 0.40f, 0.05f, 0.30f, 0.8f, 0.15f, 2.0f  },  // HAIR   - anisotropic-like, low SSS
	{ 0.80f, 0.02f, 0.10f, 0.3f, 0.05f, 1.0f  },  // CLOTH  - rough, minimal SSS
	{ 0.70f, 0.03f, 0.05f, 0.4f, 0.03f, 0.5f  },  // SHOES  - rough leather/rubber
	{ 0.10f, 0.08f, 0.00f, 1.0f, 0.00f, 0.0f  },  // EYES   - mirror-like, no SSS
	{ 0.30f, 0.50f, 0.00f, 0.8f, 0.00f, 0.0f  },  // ACCESS - metallic, no SSS
};

// ============================================================
// S5 diagnostic receipt — head-part texture probe (bandana family)
// ============================================================
// Called from the ped skin-pipe draw path (main.cpp myDefaultCallback,
// isPedPipe branch) once per run for the first ped atomic that carries a
// head-part texture. Names the texture, its TexAlpha class, and the frame
// the atomic is attached to — the three facts needed to attribute the
// "bandana green mask over the face" symptom (S5) to either (a) the
// texture's alpha class (a bandana mask should classify TAC_CUTOUT) or
// (b) the draw state at the ped pipe (alpha test OFF + blending OFF paints
// the chroma-green RGB stored in the transparent texels as SOLID fill —
// the same mechanism as the ped dual-pass leg 2, which is why the leg-2
// blend fix at main.cpp:2188-2204 and this receipt name the same family).
//
// NOTE: there is NO bandana-specific draw path and NO component
// attach/orientation code anywhere in this mod — the bandana is a game-side
// ped component rendered through the skin pipe like every other ped atomic.
// The historical "bandana draws backwards" note therefore cannot be an
// attach flip in this codebase; this receipt exists to prove which texture
// and class actually draw on the head part.
static bool icontains_ascii(const char *hay, const char *needle)
{
	if(!hay || !needle) return false;
	for(; *hay; hay++){
		const char *h = hay, *n = needle;
		while(*n){
			char hc = *h, nc = *n;
			if(hc >= 'A' && hc <= 'Z') hc += 32;
			if(nc >= 'A' && nc <= 'Z') nc += 32;
			if(hc != nc) break;
			h++; n++;
		}
		if(!*n) return true;
	}
	return false;
}

void chars_logHeadPartReceipt(const RpAtomic *atomic)
{
	static bool logged = false;
	if(logged) return;
	if(!atomic) return;
	RpGeometry *geom = atomic->geometry;
	if(!geom || !geom->matList.materials) return;

	// Attach frame: the atomic's own frame plus the clump root (the ped
	// hierarchy root), so the receipt names where the head part hangs.
	RwFrame *frame = RpAtomicGetFrame((RpAtomic*)atomic);
	const char *frameName = frame ? GetFrameNodeName(frame) : NULL;
	RpClump *clump = RpAtomicGetClump((RpAtomic*)atomic);
	RwFrame *rootFrame = clump ? RpClumpGetFrame(clump) : NULL;
	const char *rootName = rootFrame ? GetFrameNodeName(rootFrame) : NULL;

	// Local extern (texalpha.cpp): TAC_CUTOUT=2, TAC_FADED=3 — same pattern
	// as main.cpp atomicHasAlpha; enum TexAlphaClass lives in texalpha.h.
	extern int TexAlpha_Classify(const RwTexture *tex);

	char texList[256];
	size_t pos = 0;
	texList[0] = 0;
	bool found = false;
	for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
		RpMaterial *m = geom->matList.materials[i];
		if(!m || !m->texture) continue;
		const char *tn = m->texture->name;
		bool isHeadPart = icontains_ascii(tn, "bandana") ||
			icontains_ascii(tn, "hair") ||
			icontains_ascii(tn, "afro") ||
			icontains_ascii(tn, "cap") ||
			icontains_ascii(tn, "hat");
		int tac = TexAlpha_Classify(m->texture);
		// Report head-part textures by name, plus any alpha-carrying mask
		// (CUTOUT/FADED) — head-part planes are the chroma-green carriers
		// even when the texture name does not match the keywords.
		if(isHeadPart || tac == 2 || tac == 3){
			found = true;
			int cls = isHeadPart ? 1 : 0;	// 1=name-matched head part
			int w = _snprintf_s(texList + pos, sizeof(texList) - pos,
				_TRUNCATE, "%s[%s tac=%d]%s",
				pos ? " " : "", tn ? tn : "(null)", tac,
				cls ? "*" : "");
			if(w < 0) break;	// list full — first entries are the important ones
			pos += (size_t)w;
		}
	}
	if(!found) return;
	logged = true;
	dbglog("[CharsHead] receipt: atomic=%p frame='%s' root='%s' textures:%s "
		"(*=name-matched head part; tac: 1=OPAQUE 2=CUTOUT 3=FADED 0=UNKNOWN)",
		(const void*)atomic,
		frameName ? frameName : "(null)",
		rootName ? rootName : "(null)",
		texList[0] ? texList : " (none)");
}

// ============================================================
// Classification buffer for SSS blur
// ============================================================
IDirect3DTexture9 *g_charsClassifyTex = NULL;
static IDirect3DSurface9 *g_charsClassifySurf = NULL;
static RwRaster *g_charsClassifyRaster = NULL;

// Per-frame validity latch for the SSS composite gate (chars_drawSSSBlur
// Pass 2 "Blend back"). Set ONLY by the mask producer when it has produced
// g_charsClassifyTex for the CURRENT frame (chars_classifyClump marks it —
// see its tail and the TODO in chars_buildClassificationBuffer). The
// composite consumes it via snapshot-and-clear at chars_drawSSSBlur entry, so
// a mask produced on an earlier frame can never satisfy the gate later.
// Nothing produces the mask today (chars_classifyClump is never called), so
// the latch is never set and the composite stays a no-op by design — that is
// the CJ ghost fix: the depth-masked blur blend laid a double image over the
// ped whenever a real per-part mask was absent.
static bool s_charsClassifyFresh = false;

// Per-pixel classification values stored as RGBA
// R=partType (encoded), G=sssStrength, B=sssRadius, A=unused
static void EnsureClassifyBuffer(int w, int h)
{
	if(g_charsClassifyTex) return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_charsClassifyTex, NULL)))
		return;
	g_charsClassifyTex->GetSurfaceLevel(0, &g_charsClassifySurf);
	g_charsClassifyRaster = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE);
	dbglog("chars: classify buffer created %dx%d", w, h);
}

// ============================================================
// SSS blur post-process
// ============================================================
extern IDirect3DTexture9 *g_ssaoDepthTex;
extern void *overrideIm2dPixelShader;
extern RwIm2DVertex *colorfilterVerts;
extern RwImVertexIndex *colorfilterIndices;
// Trusted screen-size cache — defined in postfx.cpp, captured at the on-screen
// postfx entry (ColourFilter_switch -> CaptureScreenSize). A live
// RwCameraGetRaster read can observe a transient/swapped raster (the env-map
// reflection pass briefly swaps Scene.camera onto the reflection targets) and
// mis-size the SSS helper rasters; this cache is the same source the rest of
// the postfx chain sizes from. NOTE: postfx.cpp currently declares this
// `static`; it must be exported (remove `static`, add the declaration to
// postfx.h) for this call to link.
bool GetScreenSize(int *w, int *h);

static RwRaster *sssBlurRasterA = nil;
static RwRaster *sssBlurRasterB = nil;
static int sssLastW = 0, sssLastH = 0;

static void EnsureSSSRasters(int w, int h)
{
	if(sssLastW == w && sssLastH == h && sssBlurRasterA)
		return;
	if(sssBlurRasterA) RwRasterDestroy(sssBlurRasterA);
	if(sssBlurRasterB) RwRasterDestroy(sssBlurRasterB);
	sssBlurRasterA = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE);
	sssBlurRasterB = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE);
	sssLastW = w;
	sssLastH = h;
}

// SSS exit contract, shared by the normal path and the SEH fault path.
// rwRENDERSTATETEXTURERASTER is NOT one of ImmediateModeRenderStatesReStore's
// 10 rw states, so the pass's sssBlurRasterA/B stage-0 binds would be inherited
// by the next consumer as its texture. Park on a guaranteed-different raster
// first (so the L1 cache cannot no-op), set the captured entry raster back,
// then raw-pin its resolved IDirect3DTexture9 (L3). Sampler stage 1 is
// raw-device state too: the CLAMP forced for the depth bind must go back to
// the captured entry fields.
static void RestoreSSSStage0AndSampler(RwRaster *saved0,
	DWORD s1AddrU, DWORD s1AddrV, DWORD s1Min, DWORD s1Mag, bool s1Valid)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	RwRaster *park = sssBlurRasterA;
	if(park == saved0) park = sssBlurRasterB;
	if(park == saved0) park = NULL;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)saved0);

	// L3 raw pin — proven extraction (main.cpp pedStage0Resync): ext =
	// raster + *(DWORD*)0xB4E9E0, ext+0 = IDirect3DTexture9*. An empty entry
	// raster must still clear the device bind, otherwise sssBlurRasterB's
	// texture would survive the rw-level NULL.
	IDirect3DTexture9 *tex = NULL;
	if(saved0){
		DWORD extOff = *(DWORD*)0xB4E9E0;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)saved0 + extOff);
			if(cand && (uintptr_t)cand >= 0x10000u && (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
	}
	if(tex || !saved0)
		dev->SetTexture(0, tex);

	if(s1Valid){
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, s1AddrU);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, s1AddrV);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, s1Min);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, s1Mag);
	} else {
		// Capture failed: fall back to the old default rather than leaving the
		// depth bind's CLAMP behind.
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	}
}

void chars_drawSSSBlur(void)
{
	if(dbglog_throttle( "sss_blur"))
		dbglog("[PostFX] chars_drawSSSBlur ENTER sssPostProcessEnable=%d SSS_Blur=%p pRasterFrontBuffer=%p skinEnhance=%d",
			config->sssPostProcessEnable, SSS_Blur, CPostEffects::pRasterFrontBuffer,
			config->skinEnhanceEnable);
	// Master switch: menu "Screen-space SSS". This key was read/written by
	// the config code but NEVER consulted by any draw path — the toggle was
	// dead. Now it gates the whole pass (sssPostProcessEnable remains the
	// per-effect switch below, so every SSS knob stays individually
	// toggleable).
	if(!config->sssEnable){
		if(dbglog_throttle( "sss_bail0"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: sssEnable=0 (master switch)");
		return;
	}
	if(!config->sssPostProcessEnable){
		if(dbglog_throttle( "sss_bail1"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: sssPostProcessEnable=0");
		return;
	}
	if(!SSS_Blur){
		if(dbglog_throttle( "sss_bail2"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: SSS_Blur=NULL");
		return;
	}
	if(!CPostEffects::pRasterFrontBuffer){
		if(dbglog_throttle( "sss_bail3"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: pRasterFrontBuffer=NULL");
		return;
	}
	// SSS needs valid scene depth for the character mask. With SSAO disabled the
	// depth texture is absent or stale (never rendered) — sampling it smears the
	// blur across the whole frame and corrupts the tonemapped image. Skip until
	// a standalone depth fallback exists.
	if(!config->ssaoEnable || !g_ssaoDepthTex){
		if(dbglog_throttle( "sss_bail4"))
			dbglog("[PostFX] chars_drawSSSBlur bailing: no valid depth (ssaoEnable=%d depthTex=%p)",
				config->ssaoEnable, g_ssaoDepthTex);
		return;
	}

	// Snapshot-and-clear the per-frame mask-validity latch (see
	// s_charsClassifyFresh above). Consumed ONCE here, at entry: a mask
	// produced later in this frame can never retroactively satisfy the Pass 2
	// gate below. POD local only — the SEH fence further down (__try, C2712)
	// forbids non-POD locals in this function.
	bool classifyFresh = s_charsClassifyFresh;
	s_charsClassifyFresh = false;

	// EARLY-OUT (dead-pass elimination): Pass 2 composites ONLY when
	// classifyFresh && g_charsClassifyTex, and nothing produces the mask
	// today (s_charsClassifyFresh has no setter anywhere in this TU;
	// chars_classifyClump has no caller). Passes 0/1 are a full-screen copy
	// plus a two-tap blur pair whose only consumer is that gated composite,
	// so they would render two camera-sized rasters for nothing. Bail BEFORE
	// any raster creation/copy/blur (EnsureSSSRasters/EnsureClassifyBuffer
	// included). No camera raster has been touched yet — the first
	// RwCameraSetRaster lives inside the pass body — so no camera restore is
	// owed here; every existing bail above stays verbatim.
	if(!classifyFresh || !g_charsClassifyTex){
		if(dbglog_throttle("sss_early"))
			dbglog("[PostFX] chars_drawSSSBlur early-out: %s — Passes 0/1 (copy+blur) skipped, Pass 2 could not composite",
				!classifyFresh ? "no fresh classify mask (s_charsClassifyFresh has no producer)"
				               : "g_charsClassifyTex is NULL");
		return;
	}

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!Scene.camera) return;

	// Size from the trusted screen-size cache (GetScreenSize, postfx.cpp):
	// captured at the on-screen postfx entry and immune to transient camRas
	// reads (the env-map reflection pass swaps Scene.camera onto the
	// reflection raster for part of the frame — a live read here can observe
	// it and mis-size the SSS/classify rasters). Fall back to the live camera
	// raster only before the first capture of the session, mirroring
	// postfx.cpp GetIBLTexture.
	int w = 0, h = 0;
	if(!GetScreenSize(&w, &h)){
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(!camRas) return;
		w = camRas->width;
		h = camRas->height;
	}
	if(w < 1 || h < 1) return;

	EnsureSSSRasters(w, h);
	EnsureClassifyBuffer(w, h);
	if(!sssBlurRasterA || !sssBlurRasterB) return;

	float nearClip = Scene.camera->nearPlane;
	float farClip = Scene.camera->farPlane;
	float pixelW = 1.0f / (float)w;
	float pixelH = 1.0f / (float)h;
	// Clamp the INI-driven intensity inputs (the menu sliders bound them, but
	// a hand-edited skygfx.ini does not): radius scales kernO[16]=8 in the
	// shader, strength feeds the composite blend factor — at the old
	// DESTBLEND=ONE an INI strength near 1.0 was a full-strength additive
	// over-blend (white blowout).
	float sssWidth = config->sssPostProcessRadius;
	if(!(sssWidth > 0.0f)) sssWidth = 0.0f;	// also catches NaN
	if(sssWidth > 16.0f) sssWidth = 16.0f;
	float strength = config->sssPostProcessStrength;
	if(!(strength > 0.0f)) strength = 0.0f;
	if(strength > 1.0f) strength = 1.0f;
	// Ambient-match knob (menu "SSS ambient boost"): scales the timecycle
	// ambient the shader adds to character pixels (c21). 0 = pure blur.
	float ambBoost = config->sssAmbientBoost;
	if(!(ambBoost >= 0.0f)) ambBoost = 0.0f;
	if(ambBoost > 2.0f) ambBoost = 2.0f;

	// Skin enhancement: boost SSS for character depth pixels
	bool skinEnhanced = (config->skinEnhanceEnable != 0) && (config->skinSSSStrength > 0.01f);
	float skinSSSStrength = skinEnhanced ? config->skinSSSStrength : 1.0f;
	float warmTint = skinEnhanced ? 0.5f : 0.0f;
	float rimStrength = skinEnhanced ? 0.3f : 0.0f;

	if(skinEnhanced && dbglog_throttle("sss_skin"))
		dbglog("[PostFX] SSS skinEnhanced: strength=%.2f warmTint=%.2f rimStr=%.2f",
			skinSSSStrength, warmTint, rimStrength);

	RwRaster *origRaster = RwCameraGetRaster(Scene.camera);

	// Three-layer state contract: snapshot the raw device states BEFORE the
	// game's Store. The pass writes ALPHATESTENABLE/ZENABLE/ZWRITEENABLE and
	// driver-cache SRCBLEND/DESTBLEND/ALPHABLENDENABLE/BLENDFACTOR — none of
	// which ImmediateModeRenderStatesReStore's 10-state rw round-trip sees.
	// Leaked into the following frame's scene they meant: no depth test,
	// alpha-test off (which is also why alpha "looked better" with SSS on —
	// the leak was masking an alpha bug), and BLENDFACTOR/ONE additive blend
	// stuck on (white blowout on particle/splash draws).
	DWORD rawGeom[RAW_GEOM_STATE_COUNT];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom);

	// Exit-contract captures (read by BOTH exit paths): the entry stage-0
	// raster (pre-seeded NULL) and the sampler-1 fields the depth bind
	// overwrites. POD only — they live across __try into the handler.
	RwRaster *savedStage0 = NULL;
	DWORD savedS1AddrU = 0, savedS1AddrV = 0, savedS1Min = 0, savedS1Mag = 0;
	bool savedS1Valid = false;

	// SEH fence (postfx DrawHeightFog idiom, POD locals only — C2712): a
	// fault in the camera/raster/RT section below used to skip the restore
	// at the bottom, leaking raw ZWRITE=FALSE/ZENABLE=FALSE/ALPHATEST=FALSE
	// + blend factors into the NEXT frame's scene render (log-proven:
	// "[PED] ZWRITE off/desynced on entry (rw=1 dev=0)" — see-through cars,
	// ground dots).
	__try {
		// Capture the ENTRY stage-0 raster before this pass binds anything
		// (Pass 0/1 bind pRasterFrontBuffer / sssBlurRasterA, Pass 2 binds
		// sssBlurRasterB). A failed Get returns 0xFFFFFFFF; the NULL pre-seed
		// stands. Restored by RestoreSSSStage0AndSampler on every exit.
		RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedStage0);
		if(savedStage0 == (RwRaster*)0xFFFFFFFF)
			savedStage0 = NULL;
		// Sampler-1 entry fields: the depth bind below forces U/V CLAMP
		// (filters are captured too so a future filter change cannot leak).
		savedS1Valid =
			SUCCEEDED(dev->GetSamplerState(1, D3DSAMP_ADDRESSU, &savedS1AddrU)) &&
			SUCCEEDED(dev->GetSamplerState(1, D3DSAMP_ADDRESSV, &savedS1AddrV)) &&
			SUCCEEDED(dev->GetSamplerState(1, D3DSAMP_MINFILTER, &savedS1Min)) &&
			SUCCEEDED(dev->GetSamplerState(1, D3DSAMP_MAGFILTER, &savedS1Mag));

		// Common render state
		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// Depth on stage 1
		if(g_ssaoDepthTex){
			dev->SetTexture(1, g_ssaoDepthTex);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		}

		float depthP[4] = { nearClip, farClip, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(18, depthP, 1);

		// Skin material params: c19 = (skinStrength, warmTint, rimStrength, texelSizeX)
		//                        c20 = (texelSizeY, unused, unused, unused)
		float skinP[4] = { skinSSSStrength, warmTint, rimStrength, pixelW };
		RwD3D9SetPixelShaderConstant(19, skinP, 1);
		float texelP[4] = { pixelH, 0.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(20, texelP, 1);

		// Timecycle ambient for brightness matching: c21 = (ambientR, ambientG, ambientB, luminance)
		// Building PBR uses GetTimecycleAmbient() for PS c24 (world ambient * 0.85).
		// Previously used ambientObj which caused peds to be dimmer than buildings.
		// Scaled by the sssAmbientBoost knob — the shader adds c21 once per blur
		// pass (so the ambient match compounds across the two passes), and that
		// boost is then composited over the whole depth-masked lower half of the
		// frame (water surface included — the mask is depth-only).
		RwRGBAReal tcAmbient = GetTimecycleAmbientPBR();
		float ambR = tcAmbient.red * ambBoost;
		float ambG = tcAmbient.green * ambBoost;
		float ambB = tcAmbient.blue * ambBoost;
		float ambLuma = ambR * 0.2126f + ambG * 0.7152f + ambB * 0.0722f;
		float tcAmbP[4] = { ambR, ambG, ambB, ambLuma };
		RwD3D9SetPixelShaderConstant(21, tcAmbP, 1);

		// ---- Pass 0: Horizontal blur ----
		{
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, sssBlurRasterA);
			RwCameraBeginUpdate(Scene.camera);

			float sssP[4] = { pixelW, 0.0f, sssWidth, strength };
			RwD3D9SetPixelShaderConstant(17, sssP, 1);

			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);

			// guardedIm2DRender (not a bare RwIm2DRenderIndexedPrimitive): it sets
			// overrideIm2dPixelShader itself AND clears it in __except — a fault
			// in the unguarded form used to skip the `override = nil` line, leaving
			// SSS_Blur bound as the override for EVERY later Im2D dispatch (HUD,
			// front-buffer sync), i.e. a full-screen composite of this shader.
			guardedIm2DRender(SSS_Blur, rwPRIMTYPETRILIST, colorfilterVerts, 4,
				colorfilterIndices, 6, "SSS_Blur_H");
		}

		// ---- Pass 1: Vertical blur ----
		{
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, sssBlurRasterB);
			RwCameraBeginUpdate(Scene.camera);

			float sssP[4] = { 0.0f, pixelH, sssWidth, strength };
			RwD3D9SetPixelShaderConstant(17, sssP, 1);

			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sssBlurRasterA);

			guardedIm2DRender(SSS_Blur, rwPRIMTYPETRILIST, colorfilterVerts, 4,
				colorfilterIndices, 6, "SSS_Blur_V");
		}

		// ---- Pass 2: Blend back ----
		{
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, origRaster);
			RwCameraBeginUpdate(Scene.camera);

			// VALIDITY GATE (s_charsClassifyFresh / g_charsClassifyTex): the
			// composite is a STRICT NO-OP unless the per-part classify mask
			// (g_charsClassifyTex, produced by chars_classifyClump) exists for THIS
			// frame. classifyFresh was snapshotted and the latch cleared at function
			// entry, so a mask produced later in this frame can never satisfy this
			// gate retroactively. Without a real per-part mask the depth-masked blur
			// blend laid a translucent double image over the ped — the CJ ghost.
			// The camera restore above and the blend-off resets below stay OUTSIDE
			// the gate, so a skipped composite still leaves camera/render state
			// correct (nothing was drawn over the scene raster either).
			if(classifyFresh && g_charsClassifyTex){
				// Copy original scene — force blend OFF through BOTH driver layers.
				// colorfilterVerts carry alpha=0, so if a stale device
				// ALPHABLENDENABLE survived (rw cache already FALSE -> rw set no-ops)
				// the copy would blend instead of replace.
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);
				guardedIm2DRender(NULL, rwPRIMTYPETRILIST, colorfilterVerts, 4,
					colorfilterIndices, 6, "SSS_Copy");

				// Blend blurred result back with a BOUNDED lerp, not a plain additive:
				//   out = blur*factor + scene*(1-factor),  factor = strength (0..1)
				// (SRCBLEND=BLENDFACTOR, DESTBLEND=INVBLENDFACTOR). The old
				// DESTBLEND=ONE over-blended: out = scene + strength*blur could exceed
				// 1.0 and clipped to white on bright content (glow plume). Both the
				// RwD3D9 push and the raw write are needed — RwD3D9SetRenderState
				// no-ops against an unchanged pending[] value, which is exactly how a
				// device drift survives a cache-only set.
				// Three-layer doctrine (rw cache / RwD3D9 driver cache / raw device,
				// see pipelinecommon.cpp:883): all three must carry the blend state or
				// a later restore can no-op against a stale/hidden layer.
				// BLENDFACTOR/INVBLENDFACTOR (14/15) are OUTSIDE the RwBlendFunction
				// enum (rwplcore.h:5299-5314: 0=NA .. 11=SRCALPHASAT) — pushing them
				// through RwRenderStateSet makes the game's RW D3D9 driver index its
				// rw->D3D blend conversion table out of bounds (undefined device
				// state / garbage blend). So the layers SPLIT by domain:
				//   (1) rw cache gets the closest VALID rw pair, SRCALPHA/INVSRCALPHA
				//       (5/6) — the canonical lerp modes — so any later rw round-trip
				//       of the cache stays in-domain and sane;
				//   (2)+(3) driver pending[] and raw device get the real
				//       BLENDFACTOR/INVBLENDFACTOR (raw D3D domain: RwD3D9SetRenderState
				//       stores pending[] verbatim with no enum conversion, so 14/15 are
				//       valid THERE) — these drive the actual composite draw.
				// (1) is written first; the explicit (2) write below then wins the
				// pending[] slot (pending holds 5/6 from (1), 14/15 differ → real
				// device write), so the device ends on BLENDFACTOR/INVBLENDFACTOR
				// exactly as before — visual unchanged, OOB table read gone.
				RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE); // == D3DRS_ALPHABLENDENABLE
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
				RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
				int blendAmt = (int)(strength * 255.0f);
				if(blendAmt < 0) blendAmt = 0;
				if(blendAmt > 255) blendAmt = 255;
				// D3DRS_BLENDFACTOR has NO rw render state — it is raw-only, so only
				// layers (2)/(3) can carry it; layer (1) has no slot for it.
				RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, blendAmt, blendAmt, blendAmt));
				dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
				dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
				dev->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, blendAmt, blendAmt, blendAmt));

				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sssBlurRasterB);
				guardedIm2DRender(NULL, rwPRIMTYPETRILIST, colorfilterVerts, 4,
					colorfilterIndices, 6, "SSS_Composite");
			} else if(dbglog_throttle("sss_nomask")){
				dbglog("[PostFX] chars_drawSSSBlur: Pass 2 composite skipped (no fresh classify mask)");
			}

			// Blend-off resets stay unconditional (outside the validity gate) so a
			// skipped composite still scrubs any blend state the pass left behind.

			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}

		// Cleanup
		dev->SetTexture(1, NULL);
		// Sampler stage 1 was forced to CLAMP for the depth bind — the captured
		// entry fields are restored in the shared exit tail below. Keep the rw
		// cache default at WRAP (the pass only changed the raw device).
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSWRAP);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSWRAP);
		// The device pixel shader is still SSS_Blur after the last override draw
		// (the Im2D hook only rewrites it on the NEXT Im2D dispatch). Clear it so
		// a non-Im2D full-screen blit in between (front-buffer sync) can't run
		// through SSS_Blur and composite garbage to the screen.
		RwD3D9SetPixelShader(NULL);

		CPostEffects::ImmediateModeRenderStatesReStore();
		// Restore LAST so it wins over ReStore's rw-only round-trip: this pushes
		// all 9 raw states (incl. the 6 that ReStore never sees) through rw +
		// driver cache + raw device.
		if(rawGeomSaved)
			RestoreRawGeomStates(rawGeom);
		// Exit contract that ReStore cannot cover: stage 0 (TEXTURERASTER) and
		// the raw sampler-1 fields.
		RestoreSSSStage0AndSampler(savedStage0, savedS1AddrU, savedS1AddrV,
			savedS1Min, savedS1Mag, savedS1Valid);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		// Fault path: the normal-path restore at the bottom of the __try was
		// skipped. Mirror its tail so the raw ZWRITE/ZENABLE/ALPHATEST + blend
		// snapshot still reaches the device (no DepthHook_Suspend in this pass,
		// so no DepthHook_Restore), clear the pass-bound PS/s1 texture, log
		// once, and bail (void function).
		if(dbglog_throttle("sss_fault"))
			dbglog("[PostFX] chars_drawSSSBlur CRASHED exception=0x%08X", GetExceptionCode());
		dev->SetTexture(1, NULL);
		RwD3D9SetPixelShader(NULL);
		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved)
			RestoreRawGeomStates(rawGeom);
		// Same exit contract as the normal path: stage 0 back to the captured
		// entry raster (park-and-set + raw pin) and sampler-1 entry fields back.
		RestoreSSSStage0AndSampler(savedStage0, savedS1AddrU, savedS1AddrV,
			savedS1Min, savedS1Mag, savedS1Valid);
		return;
	}
}

// ============================================================
// Init / Shutdown
// ============================================================
void chars_shutdown(void)
{
	if(sssBlurRasterA){ RwRasterDestroy(sssBlurRasterA); sssBlurRasterA = nil; }
	if(sssBlurRasterB){ RwRasterDestroy(sssBlurRasterB); sssBlurRasterB = nil; }
	if(g_charsClassifyTex){ g_charsClassifyTex->Release(); g_charsClassifyTex = NULL; }
	// Mask released — clear the validity latch so a stale "fresh" flag can
	// never enable the SSS composite without a live mask.
	s_charsClassifyFresh = false;
	if(g_charsClassifySurf){ g_charsClassifySurf->Release(); g_charsClassifySurf = NULL; }
	if(g_charsClassifyRaster){ RwRasterDestroy(g_charsClassifyRaster); g_charsClassifyRaster = nil; }
	sssLastW = sssLastH = 0;
}

// Device-reset release (called from postfx.cpp OnDeviceReset).
//
// CRITICAL: g_charsClassifyTex is a raw D3DPOOL_DEFAULT render-target texture
// (EnsureClassifyBuffer). It is NOT an RW raster, so RW's video-memory raster
// restore list never sees it — if it is alive when the game calls
// IDirect3DDevice9::Reset, Reset fails (D3DERR_INVALIDCALL) and the vanilla
// RW restore path (D3D9DeviceRestoreVideoMemory, 0x7F9A21..) does an UNCHECKED
// IDirect3DSurface9_Release(_RwD3D9RenderSurface) right after a failed
// GetRenderTarget → Release(NULL) → 0xC0000005 read of NULL at 0x7F9A3C
// (crash log 2026-10-17 07:04:00.350, 1.3s after the frame-1 Reset).
// Everything here is lazily recreated by EnsureClassifyBuffer /
// EnsureSSSBlurRasters on the next SSS pass, so releasing is invisible.
void chars_OnDeviceReset(void)
{
	if(g_charsClassifyTex){
		dbglog("chars_OnDeviceReset: releasing classify tex=%p surf=%p",
			(void*)g_charsClassifyTex, (void*)g_charsClassifySurf);
		g_charsClassifyTex->Release(); g_charsClassifyTex = NULL;
	}
	// Device reset — same as shutdown: released mask, so kill the latch.
	s_charsClassifyFresh = false;
	if(g_charsClassifySurf){ g_charsClassifySurf->Release(); g_charsClassifySurf = NULL; }
	if(g_charsClassifyRaster){ RwRasterDestroy(g_charsClassifyRaster); g_charsClassifyRaster = nil; }
	// SSS blur rasters are RW CAMERATEXTURE rasters (DEFAULT-pool RTs under the
	// hood); RW's restore list covers them, but destroying here keeps the
	// module fully device-reset-clean and they recreate lazily next pass.
	if(sssBlurRasterA){ RwRasterDestroy(sssBlurRasterA); sssBlurRasterA = nil; }
	if(sssBlurRasterB){ RwRasterDestroy(sssBlurRasterB); sssBlurRasterB = nil; }
	sssLastW = sssLastH = 0;
}
