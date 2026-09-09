
#include "skygfx.h"
#include <d3d9types.h>
#include <DirectXMath.h>

void *SMAA = nullptr;
void *SMAA_Edge = nullptr;
void *SMAA_EdgeNormal = nullptr;
void *SMAA_EdgeDepth = nullptr;
void *SMAA_EdgeCombined = nullptr;
void *SMAA_EdgeMotionDepth = nullptr;
void *SMAA_BlendWeight = nullptr;
void *SMAA_BlendNeighbor = nullptr;
void *SMAA_Temporal = nullptr;
void *SSAO = nullptr;
void *SSAO_VertexDepth = nullptr;
void *SSAO_Temporal = nullptr;
void *SSAO_BilateralBlur = nullptr;
void *SSAO_Upsample = nullptr;
void *MotionBlur_Burnout = nullptr;
void *VelocityReconstruct = nullptr;
void *ColorFilter_CrossMix = nullptr;
void *SSS_Blur = nullptr;
void *VehiclePaint_GTAIV = nullptr;
void *Water_Parallax = nullptr;
void *Water_VS = nullptr;
void *VehiclePBR_Modern = nullptr;
void *Glass_Vehicle = nullptr;
void *Rubber_Vehicle = nullptr;
void *Rubber_Vehicle_Modern = nullptr;
void *CarPaint_Reflections = nullptr;
void *PBR_Lighting = nullptr;
void *ClampShader = nullptr;
void *DynamicSky = nullptr;
void *SkinPBR = nullptr;
void *HeightFog = nullptr;
void *GodRays = nullptr;
void *NormalBufferShader = nullptr;
void *PipeChainShader = nullptr;
void *GTAIV_PS = nullptr;

// ============================================================
// Shared timecycle lighting — single source of truth for all pipelines
// ============================================================
// Decoupled: CCoronas__LightsMult is NOT baked into ambient.
// Instead, it's a global lighting multiplier applied at upload time.
// This way timecycle controls ambient + sun + directional uniformly,
// and the corona multiplier scales the combined result.
static RwRGBAReal s_tcAmbient = {0, 0, 0, 0};   // pure timecycle ambient (no multiplier)
static float s_lightsMult = 1.0f;                 // CCoronas__LightsMult

RwRGBAReal GetTimecycleAmbient(void)
{
	// Return ambient WITH multiplier applied (for backward compat).
	// Read DIRECTLY from the game's timecycle functions instead of the
	// s_tcAmbient/s_lightsMult statics, because UpdateTimecycleLighting()
	// is deliberately NEVER called (hard rule) — so those statics stay
	// {0,0,0}/1.0 and peds/vehicles would get a flat 0 ambient regardless
	// of time of day. Weather_Update() now populates m_CurrentColours, so
	// CTimeCycle_GetAmbient*() returns the real blended timecycle ambient.
	extern float &CCoronas__LightsMult;
	float mult = (CCoronas__LightsMult > 0.0f) ? CCoronas__LightsMult : 1.0f;
	RwRGBAReal out;
	out.red   = (float)CTimeCycle_GetAmbientRed()   * mult * 0.85f;
	out.green = (float)CTimeCycle_GetAmbientGreen() * mult * 0.85f;
	out.blue  = (float)CTimeCycle_GetAmbientBlue()  * mult * 0.85f;
	return out;
}

RwRGBAReal GetTimecycleAmbientPBR(void)
{
	// Pass-through to GetTimecycleAmbient() by default — no ambient floor/boost.
	// The floor is only active when config->pbrAmbientFloor > 0.0 (opt-in tunable).
	// Pre-regression verified-good build (1,807,872) had no floor at all — peds and
	// vehicles relied on direct lights + IBL + headlights at night, and buildings
	// used baked vertex lighting. Any ambient manipulation here must be tested
	// across all times of day to avoid reintroducing the sepia-veil/black-ped bugs.
	RwRGBAReal out = GetTimecycleAmbient();
	if(config && config->pbrAmbientFloor > 0.0f){
		float floorLum = config->pbrAmbientFloor;
		float luma = 0.2126f*out.red + 0.7152f*out.green + 0.0722f*out.blue;
		if(luma < floorLum){
			// FIX: scale-up (out*k) can never lift a true zero (0*k=0) — with
			// s_tcAmbient={0,0,0} the floor was a silent no-op. Additive lift
			// toward the floor preserves any existing color contribution.
			float add = floorLum - luma;
			out.red   = min(out.red   + add, 1.0f);
			out.green = min(out.green + add, 1.0f);
			out.blue  = min(out.blue  + add, 1.0f);
		}
	}
	if(dbglog_throttle("pbr_ambient"))
		dbglog("[PBR-Ambient] final=(%.3f,%.3f,%.3f) floor=%.3f raw=(%.3f,%.3f,%.3f)",
			out.red, out.green, out.blue,
			config ? config->pbrAmbientFloor : 0.0f,
			s_tcAmbient.red, s_tcAmbient.green, s_tcAmbient.blue);
	return out;
}

RwRGBAReal GetTimecycleAmbientRaw(void)
{
	// Return pure timecycle ambient (no multiplier)
	return s_tcAmbient;
}

float GetLightsMult(void)
{
	return s_lightsMult;
}

void UpdateTimecycleLighting(void)
{
	extern float &CCoronas__LightsMult;
	// Safety: only update if initialized
	if(CCoronas__LightsMult <= 0.0f) return;
	s_tcAmbient.red = CTimeCycle_GetAmbientRed();
	s_tcAmbient.green = CTimeCycle_GetAmbientGreen();
	s_tcAmbient.blue = CTimeCycle_GetAmbientBlue();
	s_lightsMult = CCoronas__LightsMult;
}

void *gtaivFPVS = nullptr, *gtaivFPPS = nullptr;

typedef D3DMATRIX D3DXMATRIX;

D3DMATRIX &_RwD3D9D3D9ViewTransform = *(D3DMATRIX*)0xC9BC80;
D3DMATRIX &_RwD3D9D3D9ProjTransform = *(D3DMATRIX*)0x8E2458;
D3DMATRIX &D3D9ViewProjTransform = *(D3DMATRIX*)0xC94C30;
D3DMATRIX &D3D9Transform = *(D3DMATRIX*)0x8D7390;
D3DMATRIX *&_RwD3D9ActiveViewProjTransform = *(D3DMATRIX**)0xC97C64;
RwMatrix *&D3D9ActiveTransform = *(RwMatrix**)0xC94C70;
D3DMATRIX D3D9WorldViewTransform;	/* not in RW */

WRAPPER D3DXMATRIX *__stdcall D3DXMatrixMultiply(D3DXMATRIX*, const D3DXMATRIX*, const D3DXMATRIX*) { EAXJMP(0x7672E1); }
WRAPPER D3DXMATRIX *__stdcall D3DXMatrixMultiplyTranspose(D3DXMATRIX*, const D3DXMATRIX*, const D3DXMATRIX*) { EAXJMP(0x7676C7); }
WRAPPER D3DXMATRIX *__stdcall D3DXMatrixTranspose(D3DXMATRIX*, const D3DXMATRIX*) { EAXJMP(0x7675EE); }

//int __stdcall D3DXCompileShaderFromFileA(LPCSTR lpFileName, int a2, int a3, const char *a4, const char *a5, unsigned int a6, struct ID3DXBuffer **a7, int a8, struct ID3DXConstantTable **a9)
//v1 = D3DXCompileShaderFromFileA(FileName, 0, 0, "main", "ps_2_0", 1, &shader, &errors, v8);
WRAPPER int D3DXCompileShaderFromFileA(LPCSTR lpFileName, int a2, int a3, const char* a4, const char* a5, unsigned int a6, RwUInt32* shader, int* errors, struct ID3DXConstantTable** a9) { EAXJMP(0x7FAC60); }

/* Fixed RW function */
void
_rwD3D9VSGetComposedTransformMatrix(void *transformMatrix)
{
	/* Probably not that useful anymore */
	if(_RwD3D9ActiveViewProjTransform == NULL){
		D3DXMatrixMultiply(&D3D9ViewProjTransform, &_RwD3D9D3D9ViewTransform, &_RwD3D9D3D9ProjTransform);
		_RwD3D9ActiveViewProjTransform = &D3D9ViewProjTransform;
	}

	if(D3D9ActiveTransform != NULL){
		/* W * (V * P) is bad. we want (W * V) * P */
		//D3DXMatrixMultiplyTranspose((D3DMATRIX*)transformMatrix, &D3D9Transform, _RwD3D9ActiveViewProjTransform);
		D3DXMatrixMultiply(&D3D9WorldViewTransform, &D3D9Transform, &_RwD3D9D3D9ViewTransform);
		D3DXMatrixMultiplyTranspose((D3DMATRIX*)transformMatrix, &D3D9WorldViewTransform, &_RwD3D9D3D9ProjTransform);
	}else
		D3DXMatrixTranspose((D3DMATRIX*)transformMatrix, _RwD3D9ActiveViewProjTransform);
}

void
RwToD3DMatrix(void *d3d, RwMatrix *rw)
{
	D3DMATRIX *m = (D3DMATRIX*)d3d;
	m->m[0][0] = rw->right.x;
	m->m[0][1] = rw->up.x;
	m->m[0][2] = rw->at.x;
	m->m[0][3] = rw->pos.x;
	m->m[1][0] = rw->right.y;
	m->m[1][1] = rw->up.y;
	m->m[1][2] = rw->at.y;
	m->m[1][3] = rw->pos.y;
	m->m[2][0] = rw->right.z;
	m->m[2][1] = rw->up.z;
	m->m[2][2] = rw->at.z;
	m->m[2][3] = rw->pos.z;
	m->m[3][0] = 0.0f;
	m->m[3][1] = 0.0f;
	m->m[3][2] = 0.0f;
	m->m[3][3] = 1.0f;
}


void
MakeProjectionMatrix(void *d3d, RwCamera *cam, float nbias, float fbias)
{
	float f = cam->farPlane + fbias;
	float n = cam->nearPlane + nbias;
	D3DMATRIX *m = (D3DMATRIX*)d3d;
	m->m[0][0] = cam->recipViewWindow.x;
	m->m[0][1] = 0.0f;
	m->m[0][2] = 0.0f;
	m->m[0][3] = 0.0f;
	m->m[1][0] = 0.0f;
	m->m[1][1] = cam->recipViewWindow.y;
	m->m[1][2] = 0.0f;
	m->m[1][3] = 0.0f;
	m->m[2][0] = 0.0f;
	m->m[2][1] = 0.0f;
	m->m[2][2] = f/(f-n);
	m->m[2][3] = -n*m->m[2][2];
	m->m[3][0] = 0.0f;
	m->m[3][1] = 0.0f;
	m->m[3][2] = 1.0f;
	m->m[3][3] = 0.0f;
}

enum {
	LOC_combined = 0,
	LOC_world = 1,
};
void
transpose(void *dst, void *src)
{
	float (*m1)[4] = (float(*)[4])dst;
	float (*m2)[4] = (float(*)[4])src;
	m1[0][0] = m2[0][0];
	m1[0][1] = m2[1][0];
	m1[0][2] = m2[2][0];
	m1[0][3] = m2[3][0];
	m1[1][0] = m2[0][1];
	m1[1][1] = m2[1][1];
	m1[1][2] = m2[2][1];
	m1[1][3] = m2[3][1];
	m1[2][0] = m2[0][2];
	m1[2][1] = m2[1][2];
	m1[2][2] = m2[2][2];
	m1[2][3] = m2[3][2];
	m1[3][0] = m2[0][3];
	m1[3][1] = m2[1][3];
	m1[3][2] = m2[2][3];
	m1[3][3] = m2[3][3];
}

static DirectX::XMMATRIX pipeWorldMat, pipeViewMat, pipeProjMat;

void
pipeGetComposedTransformMatrix(RpAtomic *atomic, float *out)
{
	if(!atomic){ memset(out, 0, 64); return; }
	RwFrame *frame = RpAtomicGetFrame(atomic);
	if(!frame){ memset(out, 0, 64); return; }
	RwMatrix *world = RwFrameGetLTM(frame);
	if(!world){ memset(out, 0, 64); return; }

	RwToD3DMatrix(&pipeWorldMat, world);
	transpose(&pipeViewMat, &_RwD3D9D3D9ViewTransform);
	transpose(&pipeProjMat, &_RwD3D9D3D9ProjTransform);

	DirectX::XMMATRIX combined = DirectX::XMMatrixMultiply(pipeProjMat, DirectX::XMMatrixMultiply(pipeViewMat, pipeWorldMat));
	memcpy(out, &combined, 64);
}

// Have to call the above before!
void
pipeGetCameraTransformMatrix(float *out)
{
	DirectX::XMMATRIX combined = DirectX::XMMatrixMultiply(pipeViewMat, pipeWorldMat);
	memcpy(out, &combined, 64);
}

// Have to call the above before!
void
pipeGetWorldMatrix(float *out)
{
	memcpy(out, &pipeWorldMat, 64);
}

void
pipeGetLeedsEnvMapMatrix(RpAtomic *atomic, float *out)
{
	DirectX::XMMATRIX tmp;

	if(!atomic){ memset(out, 0, 64); return; }
	RwFrame *frame = RpAtomicGetFrame(atomic);
	if(!frame){ memset(out, 0, 64); return; }
	RwMatrix *world = RwFrameGetLTM(frame);
	if(!world){ memset(out, 0, 64); return; }
	RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera);

	float view[16];
	// Kill pitch in camera matrix
	RwMatrix mat;
	memset(&mat, 0, sizeof(mat));
	RwFrame *camFrame = cam ? RwCameraGetFrame(cam) : NULL;
	if(camFrame){
		RwMatrix *camLTM = RwFrameGetLTM(camFrame);
		if(camLTM)
			mat = *camLTM;
	}
	mat.pos.x = 0.0f;
	mat.pos.y = 0.0f;
	mat.pos.z = 0.0f;
	mat.at.z = 0.0f;
	if(RwV3dNormalize(&mat.at, &mat.at) == 0.0f)
		mat.at.z = 1.0f;
	mat.up.x = 0.0f;
	mat.up.y = 0.0f;
	mat.up.z = 1.0f;
	mat.right.x = -mat.at.y;
	mat.right.y = mat.at.x;
	mat.right.z = 0;
	mat.flags = rwMATRIXTYPEORTHONORMAL;

	// this can be simplified, invert is just transpose as well
	RwMatrixInvert((RwMatrix*)view, &mat);
	view[0] = -view[0];
	view[3] = 0.0f;
	view[4] = -view[4];
	view[7] = 0.0f;
	view[8] = -view[8];
	view[11] = 0.0f;
	view[12] = -view[12];
	view[15] = 1.0f;
	transpose(&tmp, view);

	tmp = DirectX::XMMatrixMultiply(tmp, pipeWorldMat);
	memcpy(out, &tmp, 64);
}

void
pipeUploadMatCol(int flags, RpMaterial *m, int loc)
{
	static float white[4] = { 1.0, 1.0, 1.0, 1.0 };
	if(flags & rpGEOMETRYMODULATEMATERIALCOLOR){
		RwRGBAReal color;
		RwRGBARealFromRwRGBA(&color, &m->color);
		RwD3D9SetVertexShaderConstant(loc, &color, 1);
	}else
		RwD3D9SetVertexShaderConstant(loc, white, 1);
}

void
pipeUploadZero(int loc)
{
	static float z[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	RwD3D9SetVertexShaderConstant(loc, (void*)z, 1);
}

void
pipeUploadZeroPS(int loc)
{
	static float z[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(loc, (void*)z, 1);
}

void
pipeUploadLightColor(RpLight *light, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		c[0] = light->color.red;
		c[1] = light->color.green;
		c[2] = light->color.blue;
		c[3] = 1.0f;
		RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZero(loc);
}

void
pipeUploadLightColorForce(RpLight *light, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	c[0] = light->color.red;
	c[1] = light->color.green;
	c[2] = light->color.blue;
	c[3] = 1.0f;
	RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
}

void
pipeUploadLightColorPS(RpLight *light, int loc)
{
	if(!light){ pipeUploadZeroPS(loc); return; }
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		c[0] = light->color.red;
		c[1] = light->color.green;
		c[2] = light->color.blue;
		c[3] = 1.0f;
		RwD3D9SetPixelShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZeroPS(loc);
}

void
pipeUploadLightColorForcePS(RpLight *light, int loc)
{
	if(!light){ pipeUploadZeroPS(loc); return; }
	float c[4];
	c[0] = light->color.red;
	c[1] = light->color.green;
	c[2] = light->color.blue;
	c[3] = 1.0f;
	RwD3D9SetPixelShaderConstant(loc, (void*)c, 1);
}

void
pipeUploadLightDirection(RpLight *light, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		RwFrame *lf = RpLightGetFrame(light);
		if(!lf){ pipeUploadZero(loc); return; }
		RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
		c[0] = at->x;
		c[1] = at->y;
		c[2] = at->z;
		c[3] = 1.0f;
		RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZero(loc);
}

void
pipeUploadLightDirectionForce(RpLight *light, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	RwFrame *lf = RpLightGetFrame(light);
	if(!lf){ pipeUploadZero(loc); return; }
	RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
	c[0] = at->x;
	c[1] = at->y;
	c[2] = at->z;
	c[3] = 1.0f;
	RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
}

void
pipeUploadLightDirectionPS(RpLight *light, int loc)
{
	if(!light){ pipeUploadZeroPS(loc); return; }
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		RwFrame *lf = RpLightGetFrame(light);
		if(!lf){ pipeUploadZeroPS(loc); return; }
		RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
		c[0] = at->x;
		c[1] = at->y;
		c[2] = at->z;
		c[3] = 1.0f;
		RwD3D9SetPixelShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZeroPS(loc);
}

void
pipeUploadLightDirectionForcePS(RpLight *light, int loc)
{
	if(!light){ pipeUploadZeroPS(loc); return; }
	float c[4];
	RwFrame *lf = RpLightGetFrame(light);
	if(!lf){ pipeUploadZeroPS(loc); return; }
	RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
	c[0] = at->x;
	c[1] = at->y;
	c[2] = at->z;
	c[3] = 1.0f;
	RwD3D9SetPixelShaderConstant(loc, (void*)c, 1);
}

void
pipeUploadLightDirectionLocal(RpLight *light, RwMatrix *m, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		RwFrame *lf = RpLightGetFrame(light);
		if(!lf){ pipeUploadZero(loc); return; }
		RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
		RwV3dTransformVector((RwV3d*)c, at, m);
		c[3] = 1.0f;
		RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZero(loc);
}

void
pipeUploadLightDirectionInv(RpLight *light, int loc)
{
	if(!light){ pipeUploadZero(loc); return; }
	float c[4];
	RwFrame *lf = RpLightGetFrame(light);
	if(!lf){ pipeUploadZero(loc); return; }
	RwV3d *at = RwMatrixGetAt(RwFrameGetLTM(lf));
	c[0] = -at->x;
	c[1] = -at->y;
	c[2] = -at->z;
	RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
}

void
makePS(int res, void **sh)
{
	if(*sh == NULL){
		HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(res), RT_RCDATA);
		if(resource == NULL){
			dbglog("  makePS(%d): FindResource FAILED", res);
			return;
		}
		RwUInt32 *shader = (RwUInt32*)LoadResource(dllModule, resource);
		if(shader == NULL){
			dbglog("  makePS(%d): LoadResource FAILED", res);
			return;
		}
		dbglog("  makePS(%d): creating pixel shader...", res);
		RwD3D9CreatePixelShader(shader, sh);
		FreeResource(shader);
		dbglog("  makePS(%d): OK sh=%p", res, *sh);
	}
}

void
makeVS(int res, void **sh)
{
	if(*sh == NULL){
		HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(res), RT_RCDATA);
		if(resource == NULL){
			dbglog("  makeVS(%d): FindResource FAILED", res);
			return;
		}
		RwUInt32 *shader = (RwUInt32*)LoadResource(dllModule, resource);
		if(shader == NULL){
			dbglog("  makeVS(%d): LoadResource FAILED", res);
			return;
		}
		RwD3D9CreateVertexShader(shader, sh);
		FreeResource(shader);
	}
}

/* CLASS 6: makeVSfromFile removed — dead code, unreferenced, broken D3DXCompileShaderFromFileA call */

extern void dbglog(const char *fmt, ...);

static bool shadersCreated = false;

void
CreateShaders(void)
{
	if(shadersCreated){
		dbglog("CreateShaders: already created, skipping");
		return;
	}
	dbglog("CreateShaders started");

	// PostFX
	makePS(IDR_VCTRAILSPS, &vcTrailsPS);
	makePS(IDR_MODERNCOLORFILTERPS, &modernColorFilterPS);
	makePS(IDR_GRADINGPS, &gradingPS);
	makePS(IDR_TONEMAPPASS, &tonemapPassPS);
	makePS(IDR_CONTRASTPS, &contrastPS);
	makePS(IDR_BLURPS, &blurPS);
	makePS(IDR_RADIOSITYPS, &radiosityPS);

	// SSAO & SMAA
	makePS(IDR_SSAOPS, &SSAO);
	makePS(IDR_SMAAPS, &SMAA);
	makePS(IDR_SMAAEDGEPS, &SMAA_Edge);
	makePS(IDR_SMAAEDGENORMALPS, &SMAA_EdgeNormal);
	makePS(IDR_SMAAEDGEDEPTHPS, &SMAA_EdgeDepth);
	makePS(IDR_SMAAEDGECOMBINEDPS, &SMAA_EdgeCombined);
	makePS(IDR_SMAAEDGEMOTIONDEPTHPS, &SMAA_EdgeMotionDepth);
	makePS(IDR_SMAATEMPPS, &SMAA_Temporal);
	makePS(IDR_SSAO_VERTEXDEPTH, &SSAO_VertexDepth);
	makePS(IDR_SSAO_TEMPORAL_PS, &SSAO_Temporal);
	makePS(IDR_SSAO_BILATERALBLUR_PS, &SSAO_BilateralBlur);
	makePS(IDR_SSAO_UPSAMPLE_PS, &SSAO_Upsample);

	// PostFX effects
	makePS(IDR_MOTIONBLUR_BURNOUT, &MotionBlur_Burnout);
	makePS(IDR_COLORFILTER_CROSSMIX, &ColorFilter_CrossMix);
	makePS(IDR_SSS_BLUR, &SSS_Blur);
	makePS(IDR_NORMALBUFFERPS, &NormalBufferShader);
	makePS(IDR_PIPECHAINPS, &PipeChainShader);

	// GTA IV
	makePS(IDR_GTAIVPS, &GTAIV_PS);

	// SMAA blend
	makePS(IDR_SMAABLENDWEIGHTPS, &SMAA_BlendWeight);
	makePS(IDR_SMAABLENDNEIGHBORPS, &SMAA_BlendNeighbor);

	// PBR / Modern
	makePS(IDR_VEHICLEPAINT_GTAIV, &VehiclePaint_GTAIV);
	makePS(IDR_WATER_PARALLAX, &Water_Parallax);
	makeVS(IDR_WATER_VS, &Water_VS);
	makePS(IDR_VEHICLEPBR_MODERN, &VehiclePBR_Modern);
	makePS(IDR_GLASS_VEHICLE, &Glass_Vehicle);
	makePS(IDR_RUBBER_VEHICLE, &Rubber_Vehicle);
	makePS(IDR_RUBBER_VEHICLE_MODERN, &Rubber_Vehicle_Modern);
	makePS(IDR_CARPAINT_REFL, &CarPaint_Reflections);
	makePS(IDR_PBR_LIGHTING, &PBR_Lighting);
	makePS(IDR_CLAMP, &ClampShader);
	makePS(IDR_DYNAMICSKY, &DynamicSky);
	//makePS(IDR_SKINPBR, &SkinPBR); // Unused: shader never referenced in any render callback
	makePS(IDR_HEIGHTFOG, &HeightFog);
	makePS(IDR_GODRAYS, &GodRays);
	makePS(IDR_VELOCITYRECONSTRUCT, &VelocityReconstruct);

	// Utility shaders
	makePS(IDR_SIMPLEPS, &simplePS);

	// Vehicle legacy (needed by CAR_ENV/CAR_MODERN _Env path)
	makeVS(IDR_VEHICLEVS, &vehiclePipeVS);
	makeVS(IDR_VEHICLEPBRVS, &vehiclePBRVS);
	makeVS(IDR_PS2CARFXVS, &ps2CarFxVS);
	makeVS(IDR_SPECCARFXVS, &specCarFxVS);
	makePS(IDR_SPECCARFXPS, &specCarFxPS);
	makeVS(IDR_XBOXCARVS, &xboxCarVS);
	makeVS(IDR_LEEDSCARFXVS, &leedsCarFxVS);
	makeVS(IDR_MOBILEVEHICLEVS, &mobileVehiclePipeVS);
	makePS(IDR_MOBILEVEHICLEPS, &mobileVehiclePipePS);
	makeVS(IDR_GTAIVVEHICLEVS, &gtaivVehicleVS);
	makePS(IDR_GTAIVVEHICLEPS, &gtaivVehiclePS);
	if(gtaivVehicleVS == NULL || gtaivVehiclePS == NULL)
		dbglog("  WARNING: gtaivVehicleVS/PS not created (VS=%p PS=%p) — ivMode vehicle path will render with null shaders",
			gtaivVehicleVS, gtaivVehiclePS);

	// Building legacy
	makeVS(IDR_PS2BUILDINGVS, &ps2BuildingVS);
	makeVS(IDR_PS2BUILDINGFXVS, &ps2BuildingFxVS);
	makeVS(IDR_PS2BUILDINGWINDVS, &ps2BuildingWindVS);
	makeVS(IDR_XBOXBUILDINGVS, &xboxBuildingVS);
	makeVS(IDR_BUILDINGPBRVS, &buildingPBRVS);
	makePS(IDR_BUILDINGPBRPS, &buildingPBRPS);
	makePS(IDR_XBOXBUILDINGPS, &xboxBuildingPS);
	makePS(IDR_XBOXBUILDINGSTOCHASTICPS, &xboxBuildingStochasticPS);
	makeVS(IDR_XBOXBUILDINGWINDVS, &xboxBuildingWindVS);
	makeVS(IDR_SPHEREBUILDINGVS, &sphereBuildingVS);
	makePS(IDR_SIMPLEDETAILPS, &simpleDetailPS);
	makePS(IDR_SIMPLEDETAILSTOCHASTICPS, &simpleDetailStochasticPS);
	makePS(IDR_SIMPLEFOGPS, &simpleFogPS);

	// Log PBR shader handle status for debugging
	dbglog("CreateShaders PBR handles: VehiclePBR_Modern=%p buildingPBRVS=%p buildingPBRPS=%p",
		VehiclePBR_Modern, buildingPBRVS, buildingPBRPS);
	dbglog("CreateShaders GTAIV vehicle handles: gtaivVehicleVS=%p gtaivVehiclePS=%p",
		gtaivVehicleVS, gtaivVehiclePS);
	dbglog("CreateShaders PBR handles: vehiclePBRVS=%p Glass_Vehicle=%p Rubber_Vehicle_Modern=%p",
		vehiclePBRVS, Glass_Vehicle, Rubber_Vehicle_Modern);
	dbglog("CreateShaders PBR handles: PBR_Lighting=%p CarPaint_Reflections=%p",
		PBR_Lighting, CarPaint_Reflections);

	// Log PostFX shader handle status
	dbglog("CreateShaders PostFX handles: gradingPS=%p contrastPS=%p blurPS=%p radiosityPS=%p",
		gradingPS, contrastPS, blurPS, radiosityPS);
	dbglog("CreateShaders PostFX handles: SSAO=%p SSAO_VertexDepth=%p SMAA_Edge=%p",
		SSAO, SSAO_VertexDepth, SMAA_Edge);
	dbglog("CreateShaders PostFX handles: SSS_Blur=%p NormalBufferShader=%p PipeChainShader=%p",
		SSS_Blur, NormalBufferShader, PipeChainShader);
	dbglog("CreateShaders PostFX handles: modernColorFilterPS=%p vcTrailsPS=%p",
		modernColorFilterPS, vcTrailsPS);
	dbglog("CreateShaders PostFX handles: MotionBlur_Burnout=%p ColorFilter_CrossMix=%p",
		MotionBlur_Burnout, ColorFilter_CrossMix);

	shadersCreated = true;
	dbglog("CreateShaders: done");
}

// ============================================================
// Unified PBR constant upload — single source of truth for c22/c23 layout.
// Both vehicle and building pipes MUST call this to avoid param-order bugs.
//
// c22 = {glossiness, specular, pipeParam3, pipeParam4}
// c23 = {pipeParam5, pipeParam6, pipeParam7, 0}
//
// Vehicle:  c22 = {glossiness, specular, specularTintR, envFresnel}
//           c23 = {renderingWheel, noiseScale, edgeBlend, 0}
// Building: c22 = {glossiness, specular, clearcoat, subsurface}
//           c23 = {specularTintR, specularTintG, specularTintB, 0}
// ============================================================
void pipeUploadPBR(float glossiness, float specular, float c22_3, float c22_4,
                   float c23_1, float c23_2, float c23_3)
{
	float c22[4] = { glossiness, specular, c22_3, c22_4 };
	float c23[4] = { c23_1, c23_2, c23_3, 0.0f };
	RwD3D9SetPixelShaderConstant(22, c22, 1);
	RwD3D9SetPixelShaderConstant(23, c23, 1);
}
