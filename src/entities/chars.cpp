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
	dbglog("[PostFX] chars_drawSSSBlur ENTER sssPostProcessEnable=%d SSS_Blur=%p pRasterFrontBuffer=%p",
		config->sssPostProcessEnable, SSS_Blur, CPostEffects::pRasterFrontBuffer);
	if(!config->sssPostProcessEnable){
		dbglog("[PostFX] chars_drawSSSBlur bailing: sssPostProcessEnable=0");
		return;
	}
	if(!SSS_Blur){
		dbglog("[PostFX] chars_drawSSSBlur bailing: SSS_Blur=NULL");
		return;
	}
	if(!CPostEffects::pRasterFrontBuffer){
		dbglog("[PostFX] chars_drawSSSBlur bailing: pRasterFrontBuffer=NULL");
		return;
	}

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!Scene.camera) return;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1) return;

	EnsureSSSRasters(w, h);
	EnsureClassifyBuffer(w, h);
	if(!sssBlurRasterA || !sssBlurRasterB) return;

	float nearClip = Scene.camera->nearPlane;
	float farClip = Scene.camera->farPlane;
	float pixelW = 1.0f / (float)w;
	float pixelH = 1.0f / (float)h;
	float sssWidth = config->sssPostProcessRadius;
	float strength = config->sssPostProcessStrength;

	RwRaster *origRaster = RwCameraGetRaster(Scene.camera);

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

	// ---- Pass 0: Horizontal blur ----
	{
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, sssBlurRasterA);
		RwCameraBeginUpdate(Scene.camera);

		float sssP[4] = { pixelW, 0.0f, sssWidth, strength };
		RwD3D9SetPixelShaderConstant(17, sssP, 1);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);

		overrideIm2dPixelShader = SSS_Blur;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// ---- Pass 1: Vertical blur ----
	{
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, sssBlurRasterB);
		RwCameraBeginUpdate(Scene.camera);

		float sssP[4] = { 0.0f, pixelH, sssWidth, strength };
		RwD3D9SetPixelShaderConstant(17, sssP, 1);

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sssBlurRasterA);

		overrideIm2dPixelShader = SSS_Blur;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;
	}

	// ---- Pass 2: Blend back ----
	{
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);

		// Copy original scene
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);

		// Additive blend of blurred result
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		int blendAmt = (int)(strength * 255.0f);
		if(blendAmt > 255) blendAmt = 255;
		RwD3D9SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0xFF, blendAmt, blendAmt, blendAmt));

		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sssBlurRasterB);
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);

		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}

	// Cleanup
	dev->SetTexture(1, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSWRAP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSWRAP);

	CPostEffects::ImmediateModeRenderStatesReStore();
}

// ============================================================
// Init / Shutdown
// ============================================================
void chars_init(void)
{
	dbglog("chars_init: SSS system ready (part params loaded)");
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
