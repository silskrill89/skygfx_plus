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
// RW API wrappers (game functions via direct addresses)
// ============================================================
extern RxPipeline *&skinPipe;

typedef void* (__cdecl *fnClumpForAllAtomics)(void *clump, void *callback, void *data);
typedef void* (__cdecl *fnAtomicGetGeometry)(void *atomic);
typedef void* (__cdecl *fnGeometryGetMaterial)(void *geo, int n);
typedef void* (__cdecl *fnMaterialGetTexture)(void *mat);
typedef void* (__cdecl *fnAtomicGetFrame)(void *atomic);

static fnClumpForAllAtomics  _ClumpForAllAtomics  = (fnClumpForAllAtomics)0x74A890;
static fnAtomicGetGeometry   _AtomicGetGeometry   = (fnAtomicGetGeometry)0x749AB0;
static fnGeometryGetMaterial _GeometryGetMaterial  = (fnGeometryGetMaterial)0x74B280;
static fnMaterialGetTexture  _MaterialGetTexture   = (fnMaterialGetTexture)0x7EF4D0;
static fnAtomicGetFrame      _AtomicGetFrame       = (fnAtomicGetFrame)0x7499A0;

// ============================================================
// Frame name classification
// GTA SA ped clump hierarchy (from anim/RW data):
//   Root
//     Pelvis
//       Spine (chest)
//         Neck
//           Head
//             Hair (optional)
//         L_Bicep -> L_Forearm -> L_Hand
//         R_Bicep -> R_Forearm -> R_Hand
//       L_Thigh -> L_Calf -> L_Foot
//       R_Thigh -> R_Calf -> R_Foot
// ============================================================

int chars_classifyByFrameName(const char *name)
{
	if(!name) return PARTTYPE_NONE;
	char lower[64];
	int i;
	for(i = 0; i < 63 && name[i]; i++)
		lower[i] = tolower(name[i]);
	lower[i] = '\0';

	// Skin parts
	if(strstr(lower, "head"))    return PARTTYPE_SKIN;
	if(strstr(lower, "neck"))    return PARTTYPE_SKIN;
	if(strstr(lower, "hand"))    return PARTTYPE_SKIN;
	if(strstr(lower, "finger"))  return PARTTYPE_SKIN;
	if(strstr(lower, "thumb"))   return PARTTYPE_SKIN;
	if(strstr(lower, "jaw"))     return PARTTYPE_SKIN;
	if(strstr(lower, "cheek"))   return PARTTYPE_SKIN;
	if(strstr(lower, "lip"))     return PARTTYPE_SKIN;
	if(strstr(lower, "nose"))    return PARTTYPE_SKIN;
	if(strstr(lower, "eye"))     return PARTTYPE_EYES;
	if(strstr(lower, "eyeball")) return PARTTYPE_EYES;

	// Hair parts
	if(strstr(lower, "hair"))    return PARTTYPE_HAIR;
	if(strstr(lower, "mohawk"))  return PARTTYPE_HAIR;
	if(strstr(lower, "beard"))   return PARTTYPE_HAIR;
	if(strstr(lower, "mustache"))return PARTTYPE_HAIR;
	if(strstr(lower, "moustache"))return PARTTYPE_HAIR;

	// Shoes
	if(strstr(lower, "foot"))    return PARTTYPE_SHOES;
	if(strstr(lower, "toe"))     return PARTTYPE_SHOES;
	if(strstr(lower, "heel"))    return PARTTYPE_SHOES;
	if(strstr(lower, "shoe"))    return PARTTYPE_SHOES;
	if(strstr(lower, "boot"))    return PARTTYPE_SHOES;

	// Cloth (clothing areas)
	if(strstr(lower, "pelvis"))  return PARTTYPE_CLOTH;
	if(strstr(lower, "spine"))   return PARTTYPE_CLOTH;
	if(strstr(lower, "bicep"))   return PARTTYPE_CLOTH;
	if(strstr(lower, "forearm")) return PARTTYPE_SKIN;  // forearms often exposed
	if(strstr(lower, "thigh"))   return PARTTYPE_CLOTH;
	if(strstr(lower, "calf"))    return PARTTYPE_CLOTH;
	if(strstr(lower, "shoulder"))return PARTTYPE_CLOTH;
	if(strstr(lower, "chest"))   return PARTTYPE_CLOTH;
	if(strstr(lower, "stomach")) return PARTTYPE_CLOTH;
	if(strstr(lower, "belt"))    return PARTTYPE_ACCESS;
	if(strstr(lower, "collar"))  return PARTTYPE_CLOTH;

	// Accessories
	if(strstr(lower, "watch"))   return PARTTYPE_ACCESS;
	if(strstr(lower, "ring"))    return PARTTYPE_ACCESS;
	if(strstr(lower, "bracelet"))return PARTTYPE_ACCESS;

	return PARTTYPE_NONE;
}

// ============================================================
// Texture-based classification
// GTA SA ped textures often have naming conventions
// ============================================================
int chars_classifyByTexture(RwTexture *tex)
{
	if(!tex) return PARTTYPE_NONE;
	const char *name = GetFrameNodeName((RwFrame*)tex);
	if(!name) return PARTTYPE_NONE;
	// Also try RwTextureGetName - but RW doesn't expose it directly
	// We use the TexDB system instead
	TexInfo *info = RwTextureGetTexDBInfo(tex);
	if(info && info->materialType != MATTYPE_NONE)
		return info->materialType;

	char lower[64];
	int i;
	for(i = 0; i < 63 && name[i]; i++)
		lower[i] = tolower(name[i]);
	lower[i] = '\0';

	// Texture name patterns
	if(strstr(lower, "head") || strstr(lower, "face") || strstr(lower, "skin"))
		return PARTTYPE_SKIN;
	if(strstr(lower, "hair"))
		return PARTTYPE_HAIR;
	if(strstr(lower, "shirt") || strstr(lower, "pants") || strstr(lower, "jacket") ||
	   strstr(lower, "torso") || strstr(lower, "vest") || strstr(lower, "top"))
		return PARTTYPE_CLOTH;
	if(strstr(lower, "shoe") || strstr(lower, "boot") || strstr(lower, "feet"))
		return PARTTYPE_SHOES;
	if(strstr(lower, "eye"))
		return PARTTYPE_EYES;

	return PARTTYPE_NONE;
}

// ============================================================
// Position-based classification
// Uses bounding box Y position relative to clump root
// ============================================================
int chars_classifyByPosition(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics)
{
	if(!atomic || numAtomics < 2) return PARTTYPE_NONE;

	void *geo = _AtomicGetGeometry(atomic);
	if(!geo) return PARTTYPE_NONE;

	// RpGeometry bounding box: offset 0x20 = RpBox (contains min/max RwV3d)
	// RpBox.min at +0x00, RpBox.max at +0x0C (each RwV3d = 12 bytes)
	float atomicY = (*(float*)((BYTE*)geo + 0x20 + 0x04) + *(float*)((BYTE*)geo + 0x20 + 0x04 + 0x04)) * 0.5f;

	float minY = 1e10f, maxY = -1e10f;
	for(int i = 0; i < numAtomics; i++){
		void *g = _AtomicGetGeometry(allAtomics[i]);
		if(!g) continue;
		float y0 = *(float*)((BYTE*)g + 0x20 + 0x04);
		float y1 = *(float*)((BYTE*)g + 0x20 + 0x04 + 0x04);
		if(y0 < minY) minY = y0;
		if(y1 > maxY) maxY = y1;
	}

	float range = maxY - minY;
	if(range < 0.01f) return PARTTYPE_NONE;

	float relativeY = (atomicY - minY) / range;

	if(relativeY > 0.80f) return PARTTYPE_SKIN;
	if(relativeY > 0.45f) return PARTTYPE_CLOTH;
	if(relativeY > 0.15f) return PARTTYPE_CLOTH;
	return PARTTYPE_SHOES;
}

// ============================================================
// Master classification: combines all methods
// ============================================================
int chars_classifyAtomic(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics)
{
	if(!atomic) return PARTTYPE_NONE;

	// 1. Try frame name first
	void *frame = _AtomicGetFrame(atomic);
	if(frame){
		// RwFrame name is at offset 0x00 in the name field (first 16 chars)
		// Use GetFrameNodeName which is the game's own function
		char *frameName = GetFrameNodeName((RwFrame*)frame);
		int result = chars_classifyByFrameName(frameName);
		if(result != PARTTYPE_NONE)
			return result;
	}

	// 2. Try texture name
	void *geo = _AtomicGetGeometry(atomic);
	if(geo){
		// RpGeometry::numMaterials is at offset 0x34
		int numMats = *(int*)((BYTE*)geo + 0x34);
		for(int i = 0; i < numMats; i++){
			void *mat = _GeometryGetMaterial(geo, i);
			if(!mat) continue;
			void *tex = _MaterialGetTexture(mat);
			if(!tex) continue;
			TexInfo *info = RwTextureGetTexDBInfo((RwTexture*)tex);
			if(info && info->materialType != MATTYPE_NONE)
				return info->materialType;
		}
	}

	// 3. Fall back to position
	return chars_classifyByPosition(atomic, allAtomics, numAtomics);
}

// ============================================================
// Classification buffer for SSS blur
// ============================================================
IDirect3DTexture9 *g_charsClassifyTex = NULL;
static IDirect3DSurface9 *g_charsClassifySurf = NULL;
static RwRaster *g_charsClassifyRaster = NULL;

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
// Build classification buffer from all atomics in a clump
// ============================================================
struct ClassifyData {
	RpAtomic **atomics;
	int count;
	int *classifications;
};

static RpAtomic *classifyAtomics[256];
static int classifyResults[256];
static int classifyCount = 0;

static void *classifyCB(void *atomic, void *data)
{
	if(classifyCount >= 256) return NULL;
	classifyAtomics[classifyCount] = (RpAtomic*)atomic;
	classifyCount++;
	return (void*)1; // continue
}

void chars_classifyClump(void *clump)
{
	if(!clump) return;
	classifyCount = 0;
	_ClumpForAllAtomics(clump, classifyCB, NULL);

	// Now classify each atomic
	for(int i = 0; i < classifyCount; i++){
		classifyResults[i] = chars_classifyAtomic(
			classifyAtomics[i], classifyAtomics, classifyCount);
	}
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

		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}

	// Cleanup
	dev->SetTexture(1, NULL);
	// Stage-1 sampler was forced to CLAMP for the depth bind — put the
	// default back or every later s1 consumer inherits the leak.
	dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
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
}

// ============================================================
// Init / Shutdown
// ============================================================
void chars_init(void)
{
	dbglog("chars_init: SSS system ready (part params loaded)");
}

PartSSSParams chars_getPartParams(int partType)
{
	if(partType < 0 || partType >= NUM_PARTTYPES)
		return g_partSSSParams[PARTTYPE_NONE];
	return g_partSSSParams[partType];
}

void chars_buildClassificationBuffer(void)
{
	// Placeholder for future per-pixel classification rendering
	// Currently unused — SSS blur uses depth-based character masking instead
}

void chars_shutdown(void)
{
	if(sssBlurRasterA){ RwRasterDestroy(sssBlurRasterA); sssBlurRasterA = nil; }
	if(sssBlurRasterB){ RwRasterDestroy(sssBlurRasterB); sssBlurRasterB = nil; }
	if(g_charsClassifyTex){ g_charsClassifyTex->Release(); g_charsClassifyTex = NULL; }
	if(g_charsClassifySurf){ g_charsClassifySurf->Release(); g_charsClassifySurf = NULL; }
	if(g_charsClassifyRaster){ RwRasterDestroy(g_charsClassifyRaster); g_charsClassifyRaster = nil; }
	sssLastW = sssLastH = 0;
}
