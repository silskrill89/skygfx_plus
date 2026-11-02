
#include "skygfx.h"
#include "debug_handl.h"
#include "profiler.h"	// PF_SCOPE/PfBindPS/PfBindVS/PfNote (renderlane profiler API)
#include "noise.h"	// SkyNoise::Init/BindAndUpload (pipeBindNoise)
#include <d3d9types.h>
#include <DirectXMath.h>

extern void *overrideIm2dPixelShader;	// postfx.cpp — Im2D override pixel shader dispatch
extern void *treeAlphaPS;	// treeWind.cpp — console tree lighting/alpha PS (W8-2)
extern void *gtaivBuildingVS, *gtaivBuildingPS;	// buildingPipe.cpp — IV-mode building forward pass
extern void *simpleNoiseDetailPS;	// buildingPipe.cpp — legacy non-detail noise-detail building PS (aap parity)
extern void *ps2EnvSpecFxPS;	// vehiclePipe.cpp — PS2 env+spec dual-layer FX (also used by the building FX pass)

// Naked thunk defined in main.cpp (EAXJMP 0x7FC200). rwcore.h's own
// declaration sits behind the inline-driver guard (#ifdef _D3D9_H_ block,
// skipped in this TU — skygfx.h includes rwcore.h before d3d9.h) and the
// definition in main.cpp is a bare one — plain C++ linkage, matched here.
void _rwD3D9RenderStateFlushCache(void);

void *SMAA = nullptr;
void *SMAA_Edge = nullptr;
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
void *Water_Parallax = nullptr;
void *Water_IV = nullptr;
void *Water_V = nullptr;
void *Water_VS = nullptr;
void *VehiclePBR_Modern = nullptr;
void *Glass_Vehicle = nullptr;
void *Rubber_Vehicle = nullptr;
void *Rubber_Vehicle_Modern = nullptr;
void *CarPaint_Reflections = nullptr;
void *PBR_Lighting = nullptr;
void *ClampShader = nullptr;
void *DynamicSky = nullptr;
// Xbox-clouds lane hunk A: cloud sprite PS (IDR_CLOUDPS 284, cso/CloudsPS.cso).
// Bound by the SpriteFlushIndexedDraw cloud path in main.cpp under
// g_inCloudsRender. (cloudsSkyPS removed — created but never bound anywhere;
// grep-verified zero readers 2026-11-02.)
void *cloudsPS = nullptr;
void *HeightFog = nullptr;
void *GodRays = nullptr;
void *NormalBufferShader = nullptr;
void *PipeChainShader = nullptr;

// ============================================================
// Shared timecycle lighting — single source of truth for all pipelines
// ============================================================
// Decoupled: CCoronas__LightsMult is NOT baked into ambient.
// Instead, it's a global lighting multiplier applied at upload time.
// This way timecycle controls ambient + sun + directional uniformly,
// and the corona multiplier scales the combined result.
static RwRGBAReal s_tcAmbient = {0, 0, 0, 0};   // pure timecycle ambient (no multiplier)
static float s_lightsMult = 1.0f;                 // CCoronas__LightsMult

// ============================================================
// Night brightness law — no material may out-shine the sky from
// ambient/IBL alone; only lights (directional sun/moon, Forward+
// tiled point lights, emissives) may exceed it.
// Single shared choke point: GetTimecycleAmbient() feeds buildings
// (via GetTimecycleAmbientPBR -> buildingAmbient -> VS c4/PS c24),
// vehicles (uploadLights / PS c24), peds (pAmbient override in
// main.cpp) and neoCarpipe — so every pipe inherits the ceiling.
// ============================================================
static void
ClampAmbientToSky(RwRGBAReal &c)
{
	// INTERIOR FIX (dark-collapse cause 1): this night law is an OUTDOOR-ONLY
	// contract — it exists to stop materials out-shining the VISIBLE sky, and
	// it compares ambient against the exterior sky (m_CurrentColours.skyTop/
	// skyBot). Indoors (CGame__currArea != 0) there is no visible sky: a
	// dark-but-nonzero interior sky reference (skyL > 0.003 so the no-data
	// bail below doesn't fire) with a normal ambient gives tiny k ->
	// nightWeight ~ 1 -> ambient crushed to black -> view/light-dependent
	// black patches. Pass the ambient through UNCLAMPED whenever the player
	// is in an interior area. Outdoor frames run the exact same code as
	// before (see the skyL/no-data/k/nightWeight trail below — untouched).
	extern int* CGame__currArea;
	// NOTE: this early return makes the ambientFloor tail at the bottom of
	// this function UNREACHABLE for all interiors (CGame__currArea != 0) —
	// deliberate, behaviour unchanged. Interior ambient is handled by
	// pbrAmbientFloor in GetTimecycleAmbientPBR(), not by ClampAmbientToSky.
	if(*CGame__currArea != 0)
		return;

	extern CColourSet &CTimeCycle__m_CurrentColours;
	const CColourSet &tc = CTimeCycle__m_CurrentColours;
	// Sky luminance the player sees: zenith (skyTop) + horizon (skyBot),
	// the same timecycle values the game draws the sky from (gta.h CColourSet).
	float topL = (0.2126f*(float)tc.skyTopR + 0.7152f*(float)tc.skyTopG + 0.0722f*(float)tc.skyTopB) / 255.0f;
	float botL = (0.2126f*(float)tc.skyBotR + 0.7152f*(float)tc.skyBotG + 0.0722f*(float)tc.skyBotB) / 255.0f;
	float skyL = 0.5f * (topL + botL);
	// < 1/255 => timecycle not populated yet (startup frame) — treat as
	// no-data and leave ambient alone rather than crushing it to black.
	if(skyL < 0.003f)
		return;
	float ambL = 0.2126f*c.red + 0.7152f*c.green + 0.0722f*c.blue;
	// NIGHT CRUSH — was three early returns (ambL<=skyL / k>=0.85 / skyL>=0.45)
	// followed by the crush; now one guarded block with IDENTICAL math so
	// every outdoor path falls through to the absolute-floor tail below
	// (dark-collapse cause 2). ambL > skyL implies ambL > 0.003 (the skyL
	// no-data floor above), so the k divide is guarded.
	if(ambL > skyL){
	float k = skyL / ambL;
	// DAYLIGHT-DARKNESS FIX: vanilla SA has NO ambient-vs-sky clamp
	// (app_light.cpp:128-134 — AmbientLightColourForFrame = GetAmbient *
	// LightsMult -> pAmbient); dawn/day ambient is deliberately brighter
	// than the sky. The old hard clamp (c *= k) crushed the Xbox/PBR
	// ambient-only building pipes to near-black every morning (dawn
	// 6:30-8:24 rendered almost black). New night law — only interfere
	// when ambient out-shines the sky by a large factor, and even then
	// BLEND toward vanilla instead of hard-clamping:
	//   nightWeight = saturate((0.85 - k) / 0.85)
	//   final       = lerp(ambient, ambient*k, nightWeight)
	//     k >= 0.85 -> untouched (vanilla-equivalent, zero crush)
	//     k  = 0.3  -> ~65% of the old crush strength
	//     k  -> 0   -> approaches the old full clamp (deep night only)
	// Belt-and-braces day release (was a separate early return): a
	// bright-sky frame (skyL >= 0.45) is never touched regardless of k.
	if(k < 0.85f && skyL < 0.45f){
	float nightWeight = (0.85f - k) / 0.85f;
	if(nightWeight < 0.0f) nightWeight = 0.0f;
	if(nightWeight > 1.0f) nightWeight = 1.0f;
	// lerp(ambient, ambient*k, nightWeight) == ambient * (1 - nw*(1-k))
	float s = 1.0f - nightWeight * (1.0f - k);
	c.red   *= s;
	c.green *= s;
	c.blue  *= s;
	if(dbglog_throttle("night_ceiling"))
		dbglog("[PBR-Ambient] night ceiling k=%.3f nw=%.3f scale=%.3f skyL=%.3f ambL=%.3f",
			k, nightWeight, s, skyL, ambL);
	}
	}
	// ABSOLUTE AMBIENT FLOOR (dark-collapse cause 2): outdoors the ambient
	// must never reach true black. With the Forward+ tile list empty (legacy
	// 7-direct-light path only — see forwardplus.cpp fp_fallback) this
	// ambient is the only light distant unlit objects (cars, poles) get at
	// night-ish timecycles, and the crush above can land it at near-black
	// skyL. Lift the FINAL result to a modest luminance floor (INI:
	// SkyGfx.ambientFloor, default 0.03 — mid of the 0.02-0.04 band, and the
	// observed-good "final=0.034" ambient from the vehicle sky-lift dig;
	// clamped [0..0.2] at parse). config NULL or value <= 0 skips the lift
	// entirely. ADDITIVE lift: scale-up can never raise a
	// zero channel (same lesson as GetTimecycleAmbientPBR's floor). Far below
	// any daytime level so day frames no-op; intentionally overrides the sky
	// ceiling below the floor's sky luminance (a near-black sky must not be
	// allowed to black out geometry). The interior early-return and the
	// no-data bail above are untouched.
	const float ambFloor = config ? config->ambientFloor : 0.0f;
	float outL = 0.2126f*c.red + 0.7152f*c.green + 0.0722f*c.blue;
	if(ambFloor > 0.0f && outL < ambFloor){
		float lift = ambFloor - outL;
		c.red   = min(c.red   + lift, 1.0f);
		c.green = min(c.green + lift, 1.0f);
		c.blue  = min(c.blue  + lift, 1.0f);
		if(dbglog_throttle("amb_floor"))
			dbglog("[PBR-Ambient] absolute floor +%.3f (lum %.3f -> %.3f, skyL=%.3f)",
				lift, outL, ambFloor, skyL);
	}
}

RwRGBAReal GetTimecycleAmbient(void)
{
	PF_SCOPE("Pipeline_GetTimecycleAmbient");
	// Return ambient WITH multiplier applied (for backward compat).
	// Read DIRECTLY from the game's timecycle functions instead of the
	// s_tcAmbient/s_lightsMult statics, because UpdateTimecycleLighting()
	// is deliberately NEVER called (hard rule) — so those statics stay
	// {0,0,0}/1.0 and peds/vehicles would get a flat 0 ambient regardless
	// of time of day. Weather_Update() now populates m_CurrentColours, so
	// CTimeCycle_GetAmbient*() returns the real blended timecycle ambient.
	extern float &CCoronas__LightsMult;
	float mult = (CCoronas__LightsMult > 0.0f) ? CCoronas__LightsMult : 1.0f;
	// Global ambient multiplier (INI: ambientMult, default 1.0 = vanilla).
	// The old hardcoded 0.85 was a straight -15% on the only light source
	// of the ambient-only Xbox building pipe — a daylight-darkness bug.
	// Null-guard matches the neighboring pbrAmbientFloor access below.
	float ambMult = (config ? config->ambientMult : 1.0f);
	RwRGBAReal out;
	out.red   = (float)CTimeCycle_GetAmbientRed()   * mult * ambMult;
	out.green = (float)CTimeCycle_GetAmbientGreen() * mult * ambMult;
	out.blue  = (float)CTimeCycle_GetAmbientBlue()  * mult * ambMult;
	ClampAmbientToSky(out); // night law: ambient can never exceed sky luminance
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
		// NOTE: no ClampAmbientToSky re-assert here. The old re-clamp ran
		// AFTER the floor and re-crushed whatever the floor had just lifted
		// whenever skyL < floor — i.e. exactly at night — so the floor was
		// a silent no-op (defeated pbrAmbientFloor). The ceiling inside
		// GetTimecycleAmbient() already ran on the raw ambient; the floor
		// is an explicit opt-in user override and must win.
	}
	if(dbglog_throttle("pbr_ambient"))
		dbglog("[PBR-Ambient] final=(%.3f,%.3f,%.3f) floor=%.3f raw=(%.3f,%.3f,%.3f)",
			out.red, out.green, out.blue,
			config ? config->pbrAmbientFloor : 0.0f,
			// Live pre-multiplier game ambient (m_CurrentColours). The old
			// s_tcAmbient here was a dead static — UpdateTimecycleLighting()
			// is never called, so it printed (0,0,0) forever and masked the
			// real timecycle value during the cutscene-dark dig.
			CTimeCycle_GetAmbientRed(), CTimeCycle_GetAmbientGreen(), CTimeCycle_GetAmbientBlue());
	return out;
}

RwRGBAReal GetTimecycleAmbientRaw(void)
{
	// LANDMINE DOCUMENTATION (rw-convention cleanup, 2026-11-02): zero
	// callers grep-verified across src/ — declared in skygfx.h, defined
	// here, never called. s_tcAmbient is NEVER updated (UpdateTimecycle-
	// Lighting() below is deliberately never called — hard rule, see
	// buildingPipe.cpp:163), so this returns a permanent {0,0,0}. Any
	// future caller must read the game directly like GetTimecycleAmbient()
	// does (CTimeCycle_GetAmbient*()), NOT trust these statics.
	return s_tcAmbient;
}

float GetLightsMult(void)
{
	// Same landmine as GetTimecycleAmbientRaw: zero callers grep-verified;
	// s_lightsMult stays at its initializer 1.0f forever. Future callers
	// should read CCoronas__LightsMult directly (see GetTimecycleAmbient).
	return s_lightsMult;
}

void UpdateTimecycleLighting(void)
{
	// Zero callers grep-verified — and deliberately so (hard rule): calling
	// this would feed live timecycle values into paths that expect the
	// decoupled statics. Kept as the only writer of s_tcAmbient/s_lightsMult
	// so the statics' provenance stays explicit. Do NOT wire this up without
	// re-auditing GetTimecycleAmbientRaw/GetLightsMult consumers.
	extern float &CCoronas__LightsMult;
	// Safety: only update if initialized
	if(CCoronas__LightsMult <= 0.0f) return;
	s_tcAmbient.red = CTimeCycle_GetAmbientRed();
	s_tcAmbient.green = CTimeCycle_GetAmbientGreen();
	s_tcAmbient.blue = CTimeCycle_GetAmbientBlue();
	s_lightsMult = CCoronas__LightsMult;
}

// GTAIVForwardPlus VS/PS pair cache — NOT dead: lazily loaded by
// buildingPipe.cpp ivFPReady() (makeVS IDR_GTAIVFPVS / makePS IDR_GTAIVFPPS)
// and bound by the buildingPipe.cpp:1681 + vehiclePipe.cpp:1317 IV-FP paths.
// Declared locally in those TUs; no skygfx.h entry (deliberate).
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
		// SA paint marker: material alpha byte 16 is NOT a translucency
		// request (it means "use the vehicle paint colour").
		// RwRGBARealFromRwRGBA turns it into 16/255 = 0.0627, which the VS
		// multiplies into the outgoing colour alpha -> painted surfaces end
		// up ~6% opaque (see-through panels). Upload 1.0 for the marker;
		// every other alpha byte passes through unchanged.
		if(m->color.alpha == 16)
			color.alpha = 1.0f;
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
	// FIX (rw-convention cleanup): c[3] was never written — uninitialized
	// stack garbage shipped as the 4th component of the VS constant every
	// call. Every sibling Direction helper (pipeUploadLightDirection/
	// Force/PS/ForcePS/Local) sets c[3] = 1.0f; same here.
	c[3] = 1.0f;
	RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
}

// CreateShaders batch counters — reset at CreateShaders entry, logged as a
// one-shot "[INIT] shaders" summary at its exit (makePS/makeVS are also used
// by lazy loaders in other TUs, but those run outside the reset/log window
// so they never pollute the init count).
static int s_psCreated = 0, s_psFailed = 0, s_vsCreated = 0, s_vsFailed = 0;

// Set by Pipeline_InvalidateShaders() after a device Reset: makePS/makeVS
// must recreate their handle even though the cache pointer is still non-NULL
// (it points at a shader object created for the pre-Reset device). True only
// for the duration of the CreateShaders pass that follows the invalidation.
static bool s_forceShaderRecreate = false;

void
makePS(int res, void **sh)
{
	if(*sh == NULL || s_forceShaderRecreate){
		// A forced post-Reset recreate must not leave the dead pre-Reset
		// handle behind if creation fails.
		*sh = NULL;
		HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(res), RT_RCDATA);
		if(resource == NULL){
			dbglog("  makePS(%d): FindResource FAILED", res);
			s_psFailed++;
			return;
		}
		RwUInt32 *shader = (RwUInt32*)LoadResource(dllModule, resource);
		if(shader == NULL){
			dbglog("  makePS(%d): LoadResource FAILED", res);
			s_psFailed++;
			return;
		}
		dbglog("  makePS(%d): creating pixel shader...", res);
		RwD3D9CreatePixelShader(shader, sh);
		FreeResource(shader);
		dbglog("  makePS(%d): OK sh=%p", res, *sh);
		if(*sh) s_psCreated++; else s_psFailed++;
	}
}

void
makeVS(int res, void **sh)
{
	if(*sh == NULL || s_forceShaderRecreate){
		// Same post-Reset rule as makePS: never leave a dead handle cached
		// behind a failed recreate.
		*sh = NULL;
		HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(res), RT_RCDATA);
		if(resource == NULL){
			dbglog("  makeVS(%d): FindResource FAILED", res);
			s_vsFailed++;
			return;
		}
		RwUInt32 *shader = (RwUInt32*)LoadResource(dllModule, resource);
		if(shader == NULL){
			dbglog("  makeVS(%d): LoadResource FAILED", res);
			s_vsFailed++;
			return;
		}
		RwD3D9CreateVertexShader(shader, sh);
		FreeResource(shader);
		if(*sh) s_vsCreated++; else s_vsFailed++;
	}
}

/* CLASS 6: makeVSfromFile removed — dead code, unreferenced, broken D3DXCompileShaderFromFileA call */

extern void dbglog(const char *fmt, ...);

static bool shadersCreated = false;

// Device Reset destroys the device-side shader objects behind every handle
// CreateShaders cached, but the cache pointers stay non-NULL — and a D3D9
// Reset clears the device's vertex/pixel shader bindings, so the stale RW
// value-compare caches would suppress every rebind. depthhook.cpp's
// hook_Reset calls this after SUCCEEDED(orig_Reset), then calls
// CreateShaders() so the next frame binds handles built for the live device.
// The dead pre-Reset handles are intentionally not Released.
void
Pipeline_InvalidateShaders(void)
{
	shadersCreated = false;
	s_forceShaderRecreate = true;
}

void
CreateShaders(void)
{
	if(shadersCreated){
		dbglog("CreateShaders: already created, skipping");
		return;
	}
	dbglog("CreateShaders started");
	s_psCreated = s_psFailed = s_vsCreated = s_vsFailed = 0;

	// PostFX
	makePS(IDR_VCTRAILSPS, &vcTrailsPS);
	// m0336 GPU particle lane: SM3.0 sprite PS (particles_gpu.cpp). Created
	// unconditionally like the other handles — the particle entry points
	// gate on config->gpuParticlesEnable (default 0), so an OFF config only
	// pays one 20-instruction PS compile at startup.
	makePS(IDR_WAKEPARTICLEPS, &wakeParticlePS);
	makePS(IDR_MODERNCOLORFILTERPS, &modernColorFilterPS);
	makePS(IDR_GRADINGPS, &gradingPS);
	makePS(IDR_TONEMAPPASS, &tonemapPassPS);
	makePS(IDR_LUMINANCEREDUCE, &luminanceReducePS);
	makePS(IDR_LUMINANCEADAPT, &luminanceAdaptPS);
	makePS(IDR_CONTRASTPS, &contrastPS);
	makePS(IDR_BLURPS, &blurPS);
	makePS(IDR_RADIOSITYPS, &radiosityPS);

	// SSAO & SMAA
	makePS(IDR_SSAOPS, &SSAO);
	makePS(IDR_SMAAPS, &SMAA);
	makePS(IDR_SMAAEDGEPS, &SMAA_Edge);
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
	// (GTAIV_PS removed — created but never bound anywhere; grep-verified
	// zero readers 2026-11-02. IV building/vehicle passes use
	// gtaivBuildingVS/PS and gtaivVehicleVS/PS instead.)

	// SMAA blend
	makePS(IDR_SMAABLENDWEIGHTPS, &SMAA_BlendWeight);
	makePS(IDR_SMAABLENDNEIGHBORPS, &SMAA_BlendNeighbor);

	// PBR / Modern
	// (VehiclePaint_GTAIV removed — created but never bound anywhere;
	// grep-verified zero readers 2026-11-02. IV vehicle paint is handled
	// by gtaivVehiclePS.)
	// Water (W7-B contract — keep in sync with waterPipe.cpp "W7-B contract"):
	// Water_VS forwards the per-vertex body classification that
	// waterPipe.cpp writes into the Im3D NORMAL channel at flush time as
	// TEXCOORD5 = (bodyType code, blend, signed aspect, 0) — 0=OCEAN
	// 1=SHALLOW 2=INTERIOR 3=RIVER 4=POND 5=LAKE; VS c10.x is the CPU
	// classify gate. PS c14/c15 follow Water_Parallax's authoritative
	// contract (draw-level fallbacks + optional fine-tunes; see the upload
	// block in waterPipe_setRenderState). Water_IV/Water_V are multi_entry
	// compiles of the same HLSL; VS and PS must be rebuilt together when
	// either side changes.
	makePS(IDR_WATER_PARALLAX, &Water_Parallax);
	makePS(IDR_WATER_IV, &Water_IV);
	makePS(IDR_WATER_V, &Water_V);
	makeVS(IDR_WATER_VS, &Water_VS);
	makePS(IDR_VEHICLEPBR_MODERN, &VehiclePBR_Modern);
	makePS(IDR_GLASS_VEHICLE, &Glass_Vehicle);
	makePS(IDR_RUBBER_VEHICLE, &Rubber_Vehicle);
	makePS(IDR_RUBBER_VEHICLE_MODERN, &Rubber_Vehicle_Modern);
	makePS(IDR_CARPAINT_REFL, &CarPaint_Reflections);
	makePS(IDR_PBR_LIGHTING, &PBR_Lighting);
	makePS(IDR_CLAMP, &ClampShader);
	makePS(IDR_DYNAMICSKY, &DynamicSky);
	// Xbox-clouds lane hunk A: cloud sprite pixel shader (IDR 284).
	// Contract (audit 2026-11-02): bound by the SpriteFlushIndexedDraw cloud
	// path in main.cpp under g_inCloudsRender; consumes s9 + c49-c52 (the
	// SkyNoise block, uploaded once/frame by CClouds__Render_hygiene) plus
	// c52.w = tile texel size (NoiseTileOK resolution signal — the main.cpp
	// cloud upload must stamp it like pipeBindNoise does below). Two-band
	// compose in CloudsPS.hlsl: SkyNoise field (high band) + NoiseLegacyGrain
	// (low band, ALU-only — needs no tile), blended by NoiseTileOK().
	// (cloudsSkyPS / IDR_CLOUDSSKYPS makePS removed — handle was created but
	// never bound anywhere; grep-verified zero readers 2026-11-02.)
	makePS(IDR_CLOUDPS, &cloudsPS);
	// SkinPBR placeholder removed — IDR_SKINPBR (cso/SkinSSS.cso) was never
	// loaded and no render callback ever bound the handle (ped/skin PBR
	// decision never landed); grep-verified zero readers 2026-11-02. The
	// resource stays embedded but unloaded.
	makePS(IDR_HEIGHTFOG, &HeightFog);
	makePS(IDR_GODRAYS, &GodRays);
	makePS(IDR_VELOCITYRECONSTRUCT, &VelocityReconstruct);

	// Utility shaders
	makePS(IDR_SIMPLEPS, &simplePS);
	// W8-2 tree lane: console tree lighting/alpha PS (treeWind.cpp treeAlphaPS).
	// NULL until this line lands => TreeAlpha_Active() false => no PS swap, no
	// c1/c24 upload; the CPU half (ALPHAREF scaling) works with or without it.
	makePS(IDR_TREEALPHA, &treeAlphaPS);

	// Vehicle legacy (needed by CAR_ENV/CAR_MODERN _Env path)
	makeVS(IDR_VEHICLEVS, &vehiclePipeVS);
	makeVS(IDR_VEHICLEPBRVS, &vehiclePBRVS);
	makeVS(IDR_PS2CARFXVS, &ps2CarFxVS);
	makeVS(IDR_SPECCARFXVS, &specCarFxVS);
	makePS(IDR_SPECCARFXPS, &specCarFxPS);
	// PS2 env+spec dual-layer FX pass (VehiclePBR_Modern.hlsl entry
	// main_ps2EnvSpecFx, cso/ps2EnvSpecFxPS.cso) — used by BOTH the vehicle
	// and building FX callbacks; simplePS cannot stand in for it (it samples
	// s0 diffuse only, so env-UV lookups returned the diffuse texel).
	makePS(IDR_PS2ENVSPECFXPS, &ps2EnvSpecFxPS);
	if(ps2EnvSpecFxPS == NULL)
		dbglog("  WARNING: ps2EnvSpecFxPS not created — PS2 FX passes fall back to simplePS");
	makeVS(IDR_XBOXCARVS, &xboxCarVS);
	makeVS(IDR_LEEDSCARFXVS, &leedsCarFxVS);
	makeVS(IDR_MOBILEVEHICLEVS, &mobileVehiclePipeVS);
	makePS(IDR_MOBILEVEHICLEPS, &mobileVehiclePipePS);
	makeVS(IDR_GTAIVVEHICLEVS, &gtaivVehicleVS);
	makePS(IDR_GTAIVVEHICLEPS, &gtaivVehiclePS);
	if(gtaivVehicleVS == NULL || gtaivVehiclePS == NULL)
		dbglog("  WARNING: gtaivVehicleVS/PS not created (VS=%p PS=%p) — ivMode vehicle path will render with null shaders",
			gtaivVehicleVS, gtaivVehiclePS);
	makeVS(IDR_GTAIVBUILDINGVS, &gtaivBuildingVS);
	makePS(IDR_GTAIVBUILDINGPS, &gtaivBuildingPS);

	// Building legacy
	makeVS(IDR_PS2BUILDINGVS, &ps2BuildingVS);
	makeVS(IDR_PS2BUILDINGFXVS, &ps2BuildingFxVS);
	makeVS(IDR_PS2BUILDINGWINDVS, &ps2BuildingWindVS);
	makeVS(IDR_XBOXBUILDINGVS, &xboxBuildingVS);
	makeVS(IDR_BUILDINGPBRVS, &buildingPBRVS);
	makePS(IDR_BUILDINGPBRPS, &buildingPBRPS);
	makePS(IDR_XBOXBUILDINGPS, &xboxBuildingPS);
	makePS(IDR_XBOXBUILDINGNOISEDETAILPS, &xboxBuildingNoiseDetailPS);
	makeVS(IDR_XBOXBUILDINGWINDVS, &xboxBuildingWindVS);
	makeVS(IDR_SPHEREBUILDINGVS, &sphereBuildingVS);
	makePS(IDR_SIMPLEDETAILPS, &simpleDetailPS);
	makePS(IDR_SIMPLEDETAILNOISEDETAILPS, &simpleDetailNoiseDetailPS);
	// Legacy noise-detail body PS (no detail map) — aap SkyGfx parity.
	// NULL handle => buildingPipe_selectPS falls back to simplePS.
	makePS(IDR_SIMPLENOISEDETAILPS, &simpleNoiseDetailPS);
	makePS(IDR_SIMPLEFOGPS, &simpleFogPS);

	// Log PBR shader handle status for debugging
	dbglog("CreateShaders PBR handles: VehiclePBR_Modern=%p buildingPBRVS=%p buildingPBRPS=%p",
		VehiclePBR_Modern, buildingPBRVS, buildingPBRPS);
	dbglog("CreateShaders GTAIV vehicle handles: gtaivVehicleVS=%p gtaivVehiclePS=%p",
		gtaivVehicleVS, gtaivVehiclePS);
	dbglog("CreateShaders GTAIV building handles: gtaivBuildingVS=%p gtaivBuildingPS=%p",
		gtaivBuildingVS, gtaivBuildingPS);
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

	s_forceShaderRecreate = false;
	shadersCreated = true;
	// One-shot init receipt: batch result of every makePS/makeVS above.
	// Verifies fix-9/10/11 shader resources actually embedded + created.
	dbglog("[INIT] shaders: compiled OK (ps=%d vs=%d), FAILED (ps=%d vs=%d)",
		s_psCreated, s_vsCreated, s_psFailed, s_vsFailed);
	dbglog("CreateShaders: done");
}

// ============================================================
// Unified PBR constant upload — single source of truth for c22/c23 layout.
// Both vehicle and building pipes MUST call this to avoid param-order bugs.
//
// c22 = {glossiness, specular, pipeParam3, pipeParam4}
// c23 = {pipeParam5, pipeParam6, pipeParam7, 0}
//
// Vehicle:  c22 = {glossiness, specular, specTint, metallicness}
//           c23 = {wheelFlag, noiseScale, edgeBlend, 0}
// Building: c22 = {glossiness, specular, clearcoat, subsurface}
//           c23 = {specularTintR, specularTintG, specularTintB, 0}
// ============================================================
void pipeUploadPBR(float glossiness, float specular, float c22_3, float c22_4,
                   float c23_1, float c23_2, float c23_3)
{
	PF_SCOPE("Pipeline_pipeUploadPBR");
	// One-shot init receipt: the first resolved c22/c23 actually uploaded,
	// plus the config-level gloss/spec defaults that feed c22.x/c22.y
	// (chromeGloss/chromeF0 — the only config glossiness/specular defaults;
	// per-material values come from VehShaders_GetPaintPBR/matBRDF at draw
	// time). Verifies fix-9/10/11 config actually loaded. Upload semantics
	// below are untouched.
	static bool s_pbrUploadInitLogged = false;
	if(!s_pbrUploadInitLogged){
		s_pbrUploadInitLogged = true;
		dbglog("[INIT] pbrUpload: first upload c22={gloss=%.3f spec=%.3f %.3f %.3f} "
		       "c23={%.3f %.3f %.3f 0} config defaults chromeGloss=%.3f chromeF0=%.3f pipes(bld=%d veh=%d)",
			glossiness, specular, c22_3, c22_4, c23_1, c23_2, c23_3,
			config ? config->chromeGloss : -1.0f,
			config ? config->chromeF0 : -1.0f,
			config ? config->buildingPipe : -1,
			config ? config->vehiclePipe : -1);
	}
	// Instrumentation (additive, throttled): confirm the single source of
	// truth for c22/c23 actually ran for this draw. A manual upload
	// elsewhere is the flat/dark regression class — this is the receipt.
	if(dbglog_throttle("pipeline_uploadpbr"))
		dbglog("[Pipeline] pipeUploadPBR ran: c22={%.3f,%.3f,%.3f,%.3f} c23={%.3f,%.3f,%.3f,0} "
		       "pipes(bld=%d veh=%d)",
			glossiness, specular, c22_3, c22_4, c23_1, c23_2, c23_3,
			config ? config->buildingPipe : -1,
			config ? config->vehiclePipe : -1);
	float c22[4] = { glossiness, specular, c22_3, c22_4 };
	float c23[4] = { c23_1, c23_2, c23_3, 0.0f };
	RwD3D9SetPixelShaderConstant(22, c22, 1);
	RwD3D9SetPixelShaderConstant(23, c23, 1);
}

// ============================================================
// Class-proof SEH choke-points — contract documented in skygfx.h.
// POD-only bodies (C2712-safe): no C++ objects with destructors live
// across the __try, so MSVC accepts SEH here and callers (DrawSMAA etc.)
// never need __try in functions that hold C++ locals.
// ============================================================
volatile LONG g_guardDepth = 0;

// Bind a raster as render-target 0. NULL raster fails without SEH; any fault
// inside RwD3D9SetRenderTarget (e.g. CAMERATEXTURE raster with no D3D9
// surface -> null COM vtable read at 0x7F9ECB) is caught, logged with the
// phase tag, and reported as false so the caller fails open.
bool guardedSetRT(RwRaster *ras, const char *phase)
{
	const char *ph = phase ? phase : "?";
	if(!ras){
		if(dbglog_throttle("gsetrt_null"))
			dbglog("[GUARD] guardedSetRT: raster NULL (phase=%s)", ph);
		return false;
	}
	bool ok = true;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		RwD3D9SetRenderTarget(0, ras);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[GUARD] guardedSetRT: FAULT phase=%s code=0x%08X raster=%p",
			ph, GetExceptionCode(), ras);
		ok = false;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return ok;
}

// Fullscreen Im2D draw (quad + override pixel shader). Pre-checks replace the
// common fault classes (null verts/idx, missing camera, camera not begun yet)
// with cheap false returns; anything else is caught by SEH. Same
// g_inGuardedIm2DPass save/restore semantics as the old inline __try blocks,
// so calling this from inside an already-hoisted guarded region is safe.
bool guardedIm2DRender(void *ps, RwInt32 primType, RwIm2DVertex *verts, RwInt32 numVerts,
                       RwImVertexIndex *indices, RwInt32 numIndices, const char *phase)
{
	const char *ph = phase ? phase : "?";

	if(!verts || !indices || numVerts <= 0 || numIndices <= 0){
		if(dbglog_throttle("gim2d_param"))
			dbglog("[GUARD] guardedIm2DRender: bad params (phase=%s verts=%p idx=%p n=%d/%d)",
				ph, verts, indices, (int)numVerts, (int)numIndices);
		return false;
	}
	if(!Scene.camera){
		if(dbglog_throttle("gim2d_nocam"))
			dbglog("[GUARD] guardedIm2DRender: Scene.camera NULL (phase=%s)", ph);
		return false;
	}
	// Begun-camera gate: Scene.camera can exist before the first
	// RwCameraBeginUpdate; Im2D dispatch dereferences the exe-side camera
	// global at 0xC9BCC0 -> null deref (same Lane G gate as postfx).
	if(*(void**)0xC9BCC0 == NULL){
		if(dbglog_throttle("gim2d_nobegun"))
			dbglog("[GUARD] guardedIm2DRender: camera not begun (phase=%s)", ph);
		return false;
	}

	bool ok = true;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		overrideIm2dPixelShader = ps;
		RwIm2DRenderIndexedPrimitive((RwPrimitiveType)primType, verts, numVerts, indices, numIndices);
		overrideIm2dPixelShader = nil;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;
		dbglog("[GUARD] guardedIm2DRender: FAULT phase=%s code=0x%08X ps=%p",
			ph, GetExceptionCode(), ps);
		ok = false;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return ok;
}

// ============================================================
// ============================================================
// RenderIBLBuffer — once-per-frame gate shared by every pipe that
// consumes g_iblTex (dynamic-sky capture, bound raw on s3 by the
// vehicle Env cb's opaque :2099 and rubber :1876 paths, and by the
// PBR building cb).
//
// This used to live as a function-local stamp inside the PBR building
// callback ONLY, so any combo with buildingPipe != PBR (e.g.
// BUILDING_GTAIV buildings + CAR_MODERN vehicles) never rendered the
// IBL target: g_iblTex stayed undefined, the vehicle cb skipped its
// s3 bind, and vehicle IBL reflections went black. The Env cb calls
// this too now; the shared stamp guarantees at most ONE render per
// frame regardless of which pipe wins the race (buildings normally
// render before vehicles, so the building call usually wins — same
// timing as before; when no PBR building draws this frame, the
// vehicle side fills the gap instead).
//
// The stamp advances even when the camera gate fails (defer; do not
// retry within the frame) — same contract the building cb had.
// ============================================================
void
pipeEnsureIBLBuffer(void)
{
	PF_SCOPE("Pipeline_pipeEnsureIBLBuffer");
	static RwUInt32 lastIBLFrame = 0;
	RwUInt32 curFrame = RWSRCGLOBAL(renderFrame);
	if(curFrame == lastIBLFrame)
		return;
	lastIBLFrame = curFrame;

	// INTERIOR FIX (dark-collapse cause 2): the DynamicSky IBL capture is
	// rendered from the timecycle sky colors (postfx.cpp RenderIBLBuffer
	// feeds skyTop/skyBot into the capture), which indoors describe no
	// visible sky — the capture comes out dark/meaningless. Its result
	// (g_iblTex) feeds the building env-reflection sky term (bldEnv.z,
	// filled at buildingPipe.cpp:2202) and the SkyHemisphereTint ratio
	// (c20.x knob — buildingPipe.cpp:1983 / vehiclePipe.cpp:2830): the
	// exact per-term CPU gates live in those callbacks / the shader, but
	// the capture produced HERE is the single in-file feed of both. So
	// gate at the feed: indoors never refresh the capture — interiors
	// must not take env tint from a dark/meaningless sky capture — and
	// report its luminance, throttled. The consumers' own dead-sample
	// guards (bldEnv.z's skyValid luminance floor, SkyHemisphereTint's
	// black bail) then keep both terms off whenever no valid capture is
	// bound. Outdoor frames (currArea == 0) fall straight through to the
	// unchanged path below.
	extern int* CGame__currArea;
	if(*CGame__currArea != 0){
		extern CColourSet &CTimeCycle__m_CurrentColours;
		const CColourSet &tc = CTimeCycle__m_CurrentColours;
		float topL = (0.2126f*(float)tc.skyTopR + 0.7152f*(float)tc.skyTopG + 0.0722f*(float)tc.skyTopB) / 255.0f;
		float botL = (0.2126f*(float)tc.skyBotR + 0.7152f*(float)tc.skyBotG + 0.0722f*(float)tc.skyBotB) / 255.0f;
		if(dbglog_throttle("ibl_indoor"))
			dbglog("[Pipeline] pipeEnsureIBLBuffer: interior currArea=%d — IBL capture suppressed, "
			       "capture luminance skyL=%.4f (top=%.4f bot=%.4f) frame %u",
				*CGame__currArea, 0.5f * (topL + botL), topL, botL, (unsigned)curFrame);
		return;
	}

	// Gate on valid camera state: on frame 1 the game renders building
	// atomics before any RwCameraBeginUpdate has run, so the exe-side
	// Im2D dispatch reads a NULL camera global and faults (crash
	// 0x7FBD4A). Defer to a later frame; the previous/stale IBL stays
	// in use until then.
	if(Scene.camera && RwCameraGetRaster(Scene.camera)){
		RenderIBLBuffer();
		// Instrumentation (additive, throttled): receipt that the
		// shared once-per-frame IBL capture ran (s3 cubemap feed).
		if(dbglog_throttle("pipeline_ibl"))
			dbglog("[Pipeline] pipeEnsureIBLBuffer: IBL target rendered (frame %u) pipes(bld=%d veh=%d)",
				(unsigned)curFrame,
				config ? config->buildingPipe : -1,
				config ? config->vehiclePipe : -1);
	}else if(dbglog_throttle("ibl_nocam")){
		dbglog("RenderIBL: skip, camera not ready (frame %u)", curFrame);
	}
}

// ============================================================
// L1/L2/L3 write primitives — one choke point per doctrine layer,
// and THE canonical park order for the whole tree:
//
//     PARK-THEN-TARGET PER LAYER
//       L1:  park -> target   (pipeRwParkSet)
//       L2:  park -> target   (pipeL2ParkSet)
//       L3:  target only      (pipeL3Set, unconditional raw pin)
//
// A state's L1 pair completes BEFORE its L2 pair starts; the raw L3
// pin comes last. That is the order pipeForceCullMode,
// pipeEnterAlphaMode and pipeApplyWorldBaseline compose, and the order
// postfx_utils.cpp RestoreOneRawGeomState uses (it used to interleave
// L1park/L2park/L1target/L2target — same end state, not canonical).
//
// Park values are ALWAYS chosen different from the target on every
// layer they are written through, so no cache can no-op them: layers
// (1) and (2) only transition on a value CHANGE and the driver-cache
// flush is applied[]-gated. No draw can occur between the writes, so
// the transient park never reaches the device.
//
//   (1) pipeRwParkSet — rw cache park -> target. Two RwRenderStateSet
//       calls: the park is what makes the following target a real
//       transition even when the cache already holds it.
//   (2) pipeL2ParkSet — RW D3D9 driver cache park -> target, same rule.
//   (3) pipeL3Set     — unconditional raw device write (null-guarded).
//       It is the only layer an applied[]-gated flush cannot repair.
//
// Callers still own the flush and the layer interleaving; each helper
// is exactly the sequence previously inlined at the call site.
// ============================================================
void
pipeRwParkSet(RwRenderState state, RwUInt32 park, RwUInt32 target)
{
	RwRenderStateSet(state, (void*)park);
	RwRenderStateSet(state, (void*)target);
}

void
pipeL2ParkSet(D3DRENDERSTATETYPE state, RwUInt32 park, RwUInt32 target)
{
	RwD3D9SetRenderState((RwUInt32)state, park);
	RwD3D9SetRenderState((RwUInt32)state, target);
}

void
pipeL3Set(D3DRENDERSTATETYPE state, DWORD value)
{
	if(d3d9device)
		d3d9device->SetRenderState(state, value);
}

// ============================================================
// Cull-scope stack storage (see the pipeCullScopeBegin/End block
// below for the full contract). Declared here because
// pipeEnterCullMode consults the depth: a non-zero depth means a
// foreign-owned span (the game's mirror render) is live, and its
// cull mode is FRESH intent — not the stale-cache case the enter
// guard exists to kill.
// ============================================================
static const unsigned PIPE_CULL_SCOPE_MAX = 8;
static RwUInt32 s_cullScopeSaved[PIPE_CULL_SCOPE_MAX];
static unsigned s_cullScopeDepth = 0;
static unsigned s_cullScopeOverflow = 0;

// ============================================================
// Cull-mode guard for pipe render callbacks.
//
// THREE layers must agree or back-face culling goes wrong:
//   (1) the rw cache RwRenderStateSet/Get read+write,
//   (2) the D3D9 driver cache behind RwD3D9SetRenderState
//       (gta_sa 0x7FC2D0: pending[state] + dirty list; flushed by
//       _rwD3D9RenderStateFlushCache 0x7FC200 before every draw and
//       ONLY when its applied[] shadow differs from pending[]),
//   (3) the raw IDirect3DDevice9 state.
// Layers (1) and (2) both skip work when they already hold the
// requested value, so anything that moves only one layer — raw
// dev->SetRenderState in postfx Save/RestoreRawGeomStates, a direct
// RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE) that layer (1)
// never sees, or a D3D9 device reset restoring D3D's own default
// CULLMODE — leaves (3) desynced from (1)/(2) PERMANENTLY: every
// later RwRenderStateSet(rwCULLMODECULLBACK) is a double no-op
// (layer 1 already says BACK, layer 2 already says BACK) and the
// device keeps the stale value. Stuck at D3DCULL_NONE -> backfaces
// show (double-sided); stuck at the opposite winding -> front faces
// culled. That is the "cull reversed on PBR vehicles/buildings"
// report: the pipes only ever touched layer (1).
//
// Fix: pipeForceCullMode() parks layers (1)/(2) on the other
// canonical value first (guarantees a real cache transition even
// when the cache already holds the target), re-requests the target
// through both cache layers using the index-identical rw->D3D
// mapping (see below; BACK/NONE semantics unchanged), then writes
// that value to the raw device (layer 3). Device==cache on every
// enter/exit unconditionally.
//
// Mapping reference (verified in gta_sa.exe + RW docs): rw enum
// {NACULLMODE=0, CULLNONE=1, CULLBACK=2, CULLFRONT=3}, D3D enum
// {NONE=1, CW=2, CCW=3}; the mapping is index-identical for the
// three valid modes, and back faces are clockwise on screen (RW
// User Guide 7.2.4: visible = anti-clockwise) so rwCULLBACK(2)
// lands on D3DCULL_CW(2). All THREE valid modes are first-class:
// the game really does use CULLNONE (mirror pass, saved at
// 0xC4028C) and CULLFRONT (ped stencil-shadow volumes, exe
// 0x70A9C3); clamping either to BACK draws the mirrored
// negative-determinant CJ inside-out and destroys shadow volumes.
//
// Span-mode contract (2026-10-30, §4.1 fix): the mode a span is
// entered with is the INTENDED span mode, never "whatever the rw
// cache currently holds". Outside a pipeCullScope that is BACK (the
// SA world default) — a stale CULLNONE/FRONT parked in the cache by
// an immediate-mode actor (vegetation, CPostEffects::
// ImmediateModeRenderStates*, the game's L1-only mirror restore) is
// DEAD at every entry and can no longer be laundered across a whole
// callback span. Inside a pipeCullScope the foreign owner's live
// mode (L1) is fresh intent (the mirror's NONE) and survives.
// RwRenderStateGet can fail leaving the out-param untouched, so
// saved stays pre-seeded and out-of-domain values are sanitized to
// BACK — garbage can never leak out of a pipe callback. Enter
// reconciles ALL THREE layers to the intended mode (park-and-set)
// and returns THAT mode; exit re-asserts it (sticky-repair pair,
// the ped-ZWRITE rule: never re-create a pre-entry desync at exit).
// ============================================================
// Identity rw->D3D cull mapping for the three valid modes — SINGLE
// SOURCE (the divergence checks in pipeEnterCullMode compare raw
// device/driver values against the same mapping).
static RwUInt32
pipeCullToD3D(RwUInt32 rwCull)
{
	if(rwCull == rwCULLMODECULLNONE)
		return (RwUInt32)D3DCULL_NONE;
	if(rwCull == rwCULLMODECULLFRONT)
		return (RwUInt32)D3DCULL_CCW;
	return (RwUInt32)D3DCULL_CW;
}

void
pipeForceCullMode(RwUInt32 rwCull)
{
	// Defensive: only the three valid rw cull modes are ever pushed.
	if(rwCull != rwCULLMODECULLNONE && rwCull != rwCULLMODECULLBACK &&
	   rwCull != rwCULLMODECULLFRONT){
		// One-shot receipt: an undefined rw cull value is drift of
		// exactly the class this guard exists for, so make the
		// sanitize visible once instead of coercing silently.
		if(dbglog_once("cull_ood"))
			dbglog("[Cull] out-of-domain value=%u -> BACK", (unsigned)rwCull);
		rwCull = rwCULLMODECULLBACK;
	}

	// Park on a guaranteed-DIFFERENT mode so layers (1)/(2) always
	// see a real transition: BACK parks FRONT, FRONT parks BACK,
	// NONE parks BACK. No draw can happen between the two sets, so
	// the transient never reaches the device.
	RwUInt32 rwPark = rwCULLMODECULLBACK;
	if(rwCull == rwCULLMODECULLBACK)
		rwPark = rwCULLMODECULLFRONT;

	// Identity rw->D3D mapping for the three valid modes (the same
	// direct mapping this file has always used for BACK/NONE).
	RwUInt32 d3dCull = pipeCullToD3D(rwCull);
	RwUInt32 d3dPark = (d3dCull == (RwUInt32)D3DCULL_CW) ?
		(RwUInt32)D3DCULL_CCW : (RwUInt32)D3DCULL_CW;

	// (1) L1 rw cache: park, then the real target.
	pipeRwParkSet(rwRENDERSTATECULLMODE, rwPark, rwCull);

	// (2) L2 RW driver cache: park, then target (same mapping).
	pipeL2ParkSet(D3DRS_CULLMODE, d3dPark, d3dCull);

	// (3) Raw device write. The flush is applied[]-gated, so it can
	// never repair a device that drifted behind an unchanged cache —
	// this unconditional write is what actually re-syncs layer (3).
	pipeL3Set(D3DRS_CULLMODE, d3dCull);
}

// ============================================================
// pipeEnforceDepthContract — shared world depth contract at pipe
// entry (§4.1 culling/depth-z-facing, 2026-10-30). The pipe-entry
// instance of the grasssys GrassEnforceDepthContract idiom
// (grasssys.cpp:823 — the reference implementation): ground truth
// is the RAW DEVICE (GetRenderState), never the rw wrapper caches;
// healthy is SILENT; drift repairs ALL THREE layers so a later RW
// flush cannot re-drift.
//
// Baseline = the pipeApplyWorldBaseline depth targets: Z-test ON,
// ZFUNC LESSEQUAL. ZWRITE is deliberately NOT touched — it is
// per-span, owned by the callbacks' own ZWRITE trio guards.
//
// Measured failure class ([PedOpaque] pre-entry "ZWRITE=0 ZENABLE=0
// ZFUNC=8"): a raw L3 write parks ZENABLE=0 / ZFUNC=ALWAYS behind a
// rw cache still claiming ztest=1, so every later rw-cache-only
// ZTEST restore value-compares as a no-op and the span draws with
// no depth test — meshes through walls, z-fighting on ground,
// vegetation over everything. The building/vehicle Switch entries
// guarded ZWRITE but never ZENABLE/ZFUNC (the ZFUNC drift had no
// repair at all: no rw twin exists), so the shared chokepoint
// repairs it here.
//
// External linkage, no skygfx.h declaration required yet (same
// idiom as pipeCullScopeBegin/End): main.cpp's default-pipe entry
// contract can call this directly once a hunk wires it.
// ============================================================
void
pipeEnforceDepthContract(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;

	// Ground truth = raw device. A failed Get is left alone.
	DWORD devZen = TRUE, devZFunc = D3DCMP_LESSEQUAL;
	if(FAILED(dev->GetRenderState(D3DRS_ZENABLE, &devZen)) ||
	   FAILED(dev->GetRenderState(D3DRS_ZFUNC, &devZFunc)))
		return;

	// Cache layers for the divergence check (L1 ztest has an rw twin;
	// ZFUNC has none — L2/L3 only). A stale cache value is a live
	// mine: the next pre-draw flush can push it back over the device.
	RwUInt32 rwZTest = TRUE;
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &rwZTest);
	DWORD drvZen = TRUE, drvZFunc = D3DCMP_LESSEQUAL;
	RwD3D9GetRenderState(D3DRS_ZENABLE, &drvZen);
	RwD3D9GetRenderState(D3DRS_ZFUNC, &drvZFunc);

	// Healthy world baseline: Z-test on, LESSEQUAL, on every layer.
	if(devZen && devZFunc == D3DCMP_LESSEQUAL && rwZTest &&
	   drvZen && drvZFunc == D3DCMP_LESSEQUAL)
		return;

	// Drift — park-and-set the caches (guaranteed transition, so no
	// value-compare skip can no-op the repair), commit pending[], then
	// pin the raw device (doctrine order: L1 pair, L2 pair, flush, L3).
	pipeRwParkSet(rwRENDERSTATEZTESTENABLE, FALSE, TRUE);
	pipeL2ParkSet(D3DRS_ZENABLE, D3DZB_FALSE, D3DZB_TRUE);
	pipeL2ParkSet(D3DRS_ZFUNC, D3DCMP_ALWAYS, D3DCMP_LESSEQUAL);
	_rwD3D9RenderStateFlushCache();
	pipeL3Set(D3DRS_ZENABLE, D3DZB_TRUE);
	pipeL3Set(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	if(dbglog_once("zfix_first") || dbglog_throttle("zfix"))
		dbglog("[zfix] pipe entry depth drift repaired -> ZENABLE=TRUE ZFUNC=LESSEQUAL: "
		       "rw ztest=%u drv zen=%u zfunc=%u dev zen=%u zfunc=%u",
			(unsigned)rwZTest, (unsigned)drvZen, (unsigned)drvZFunc,
			(unsigned)devZen, (unsigned)devZFunc);
}

// ============================================================
// pipeRepairSceneStartSync — scene-start REPAIR-TO-CACHE (see-through
// fix lane, 2026-11-02). Detection promoted to repair at the
// scene_start edge (main.cpp RenderScene_before, right after the
// RSMon_Sample("scene_start") receipt so the evidence line still
// captures the divergence BEFORE we fix it).
//
// Evidence (docs/baseline/2026-11-02-test11/findings.md, standing
// rs_mon receipt all session):
//   DIVERGE site=scene_start | ALPHABLEND rw=0 dev=1 | ZWRITE rw=1 dev=0
//   | ZTEST rw=1 dev=0 | ZFUNC drv=4 dev=8
// The device enters the scene with blend-on/depth-off while the rw
// cache disagrees, and nothing repairs it until after sky/clouds draw
// -> see-through terrain/vehicles/peds/markers.
//
// Failure mechanism (same class pipeEnforceDepthContract documents):
// the L2 driver cache AGREES with L1 (rw==drv on every line), so the
// pre-draw flush value-compares its pending state against a cache that
// already holds the world values and skips the device write — the
// wrong device values survive the flush and the whole scene draws
// unblended/undepth-tested. Only a PARK-AND-SET on L2 (park on a
// different value, then the target = guaranteed real transition in the
// applied[]-gated flush) plus a raw L3 pin can re-sync the device.
//
// Repair target = the rw cache (L1) — the world-baseline owner. ZFUNC
// has no rw twin; its target is the L2 driver cache (world-baseline
// LESSEQUAL when that read fails). This is the mirror image of
// PostfxStateSyncCheck (postfx.cpp:1623): that one heals L2 to device
// truth inside the postfx phase; this one repairs L2+L3 to CACHE truth
// scene-side, before the frame's first draw.
//
// Cost: 6 device Gets + 6 cheap cache Gets every frame on the healthy
// path (silent, zero writes); the park-and-set + raw pins + one flush
// run only on a real divergence. No draw can occur between the writes,
// so the transient park never reaches a draw.
// L1 is deliberately NOT touched: it is the repair SOURCE and already
// holds the target by construction.
// ============================================================
void
pipeRepairSceneStartSync(const char *tag)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;

	// ---- Targets: rw cache (L1), pre-seeded with the
	// pipeApplyWorldBaseline signature so a failed Get can never
	// fabricate a target (same fault rule as rsmonCapture /
	// buildingPipe_saveRenderState: a failed read leaves the seed).
	RwUInt32 rwBlend  = FALSE;		// rwRENDERSTATEVERTEXALPHAENABLE
	RwUInt32 rwSrc    = rwBLENDSRCALPHA;	// 5
	RwUInt32 rwDst    = rwBLENDINVSRCALPHA;	// 6
	RwUInt32 rwZWrite = TRUE;
	RwUInt32 rwZTest  = TRUE;
	DWORD    drvZFunc = D3DCMP_LESSEQUAL;	// no rw twin: L2 is the target

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&rwBlend);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &rwSrc);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &rwDst);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &rwZWrite);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &rwZTest);
	RwD3D9GetRenderState(D3DRS_ZFUNC, &drvZFunc);

	// Out-of-domain sanitize (rw/D3D blend domains are index-identical
	// 1..11; compare functions 1..8 — see renderstate_monitor.cpp STATE
	// MAP note). Garbage must never be pushed as a repair target.
	if(rwSrc < 1 || rwSrc > 11)
		rwSrc = rwBLENDSRCALPHA;
	if(rwDst < 1 || rwDst > 11)
		rwDst = rwBLENDINVSRCALPHA;
	if(drvZFunc < 1 || drvZFunc > 8)
		drvZFunc = D3DCMP_LESSEQUAL;

	// ---- Detection: 6 device Gets — the whole steady-state cost ----
	DWORD devBlend = 0, devSrc = 0, devDst = 0, devZWrite = 0,
	      devZEnable = 0, devZFunc = 0;
	if(FAILED(dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &devBlend)) ||
	   FAILED(dev->GetRenderState(D3DRS_SRCBLEND, &devSrc)) ||
	   FAILED(dev->GetRenderState(D3DRS_DESTBLEND, &devDst)) ||
	   FAILED(dev->GetRenderState(D3DRS_ZWRITEENABLE, &devZWrite)) ||
	   FAILED(dev->GetRenderState(D3DRS_ZENABLE, &devZEnable)) ||
	   FAILED(dev->GetRenderState(D3DRS_ZFUNC, &devZFunc)))
		return;

	int rBlend  = (!!devBlend)   != (!!rwBlend);
	int rSrc    = devSrc   != (DWORD)rwSrc;
	int rDst    = devDst   != (DWORD)rwDst;
	int rZWrite = (!!devZWrite)  != (!!rwZWrite);
	int rZTest  = (!!devZEnable) != (!!rwZTest);
	int rZFunc  = devZFunc != drvZFunc;

	if(!(rBlend || rSrc || rDst || rZWrite || rZTest || rZFunc))
		return;	// healthy: all three layers agree — silent, no writes

	// ---- Repair: per diverged state, L2 park-and-set then raw L3 pin
	// (pipeEnforceDepthContract doctrine order; L1 untouched — it IS
	// the target). Park values are guaranteed != target so no cache or
	// applied[] gate can no-op the transition.
	if(rBlend){
		pipeL2ParkSet(D3DRS_ALPHABLENDENABLE,
			rwBlend ? FALSE : TRUE, rwBlend ? TRUE : FALSE);
		pipeL3Set(D3DRS_ALPHABLENDENABLE, rwBlend ? TRUE : FALSE);
	}
	if(rSrc){
		RwUInt32 park = (rwSrc == (RwUInt32)rwBLENDSRCALPHA) ?
			(RwUInt32)rwBLENDINVSRCALPHA : (RwUInt32)rwBLENDSRCALPHA;
		pipeL2ParkSet(D3DRS_SRCBLEND, park, rwSrc);
		pipeL3Set(D3DRS_SRCBLEND, (DWORD)rwSrc);
	}
	if(rDst){
		RwUInt32 park = (rwDst == (RwUInt32)rwBLENDINVSRCALPHA) ?
			(RwUInt32)rwBLENDSRCALPHA : (RwUInt32)rwBLENDINVSRCALPHA;
		pipeL2ParkSet(D3DRS_DESTBLEND, park, rwDst);
		pipeL3Set(D3DRS_DESTBLEND, (DWORD)rwDst);
	}
	if(rZWrite){
		pipeL2ParkSet(D3DRS_ZWRITEENABLE,
			rwZWrite ? FALSE : TRUE, rwZWrite ? TRUE : FALSE);
		pipeL3Set(D3DRS_ZWRITEENABLE, rwZWrite ? TRUE : FALSE);
	}
	if(rZTest){
		pipeL2ParkSet(D3DRS_ZENABLE,
			rwZTest ? D3DZB_FALSE : D3DZB_TRUE,
			rwZTest ? D3DZB_TRUE : D3DZB_FALSE);
		pipeL3Set(D3DRS_ZENABLE, rwZTest ? D3DZB_TRUE : D3DZB_FALSE);
	}
	if(rZFunc){
		DWORD park = (drvZFunc == D3DCMP_ALWAYS) ?
			D3DCMP_LESSEQUAL : D3DCMP_ALWAYS;
		pipeL2ParkSet(D3DRS_ZFUNC, park, drvZFunc);
		pipeL3Set(D3DRS_ZFUNC, drvZFunc);
	}

	// Commit pending[] while it holds our targets (same idiom as
	// pipeEnforceDepthContract / pipeApplyWorldBaseline step 2->3).
	_rwD3D9RenderStateFlushCache();

	// Throttled receipt naming the repaired states (first hit always
	// logs — one-shot, never starved — then the shared 2s bucket).
	if(dbglog_once("scene_repair_first") || dbglog_throttle("scene_repair")){
		dbglog("[scene_repair] %s: repaired%s%s%s%s%s%s -> rw-cache baseline "
			"(blend rw=%u dev=%u src rw=%u dev=%u dst rw=%u dev=%u "
			"zwrite rw=%u dev=%u ztest rw=%u dev=%u zfunc drv=%u dev=%u)",
			tag ? tag : "scene_start",
			rBlend  ? " ALPHABLEND" : "",
			rSrc    ? " SRCBLEND" : "",
			rDst    ? " DESTBLEND" : "",
			rZWrite ? " ZWRITE" : "",
			rZTest  ? " ZTEST" : "",
			rZFunc  ? " ZFUNC" : "",
			(unsigned)rwBlend, (unsigned)devBlend,
			(unsigned)rwSrc, (unsigned)devSrc,
			(unsigned)rwDst, (unsigned)devDst,
			(unsigned)rwZWrite, (unsigned)devZWrite,
			(unsigned)rwZTest, (unsigned)devZEnable,
			(unsigned)drvZFunc, (unsigned)devZFunc);
	}
}

// ============================================================
// pipeEnterCullMode — pipe-entry cull reconciliation (§4.1 fix) +
// the shared depth contract (pipeEnforceDepthContract, same entry
// chokepoint: every building/vehicle/neo/ped callback enters here).
//
// The span's cull mode is the INTENDED mode, never "whatever the rw
// cache holds":
//   * outside a pipeCullScope: BACK — the SA world default. A stale
//     CULLNONE/FRONT parked in the rw cache by an immediate-mode
//     actor (vegetation, CPostEffects::ImmediateModeRenderStates*,
//     the game's L1-only mirror restore) is DEAD at every entry; it
//     can no longer be laundered across a whole callback span
//     (double-sided buildings/vehicles, inside-out meshes, black
//     backface facets on skin — "cull mode flipping mid-skin-draw").
//   * inside a pipeCullScope (mirrorPass.cpp wraps
//     CMirrors::RenderMirrorBuffer): the foreign owner's live span
//     mode (L1) is FRESH intent — the mirror's negative-determinant
//     world render needs its CULLNONE — and survives ("the mirror
//     pass's CULLNONE reaches the assert intact", vehiclePipe/
//     buildingPipe Switch comments).
// Divergence is measured against the raw device (grasssys idiom);
// the reconcile is pipeForceCullMode (park-and-set, all three
// layers). Returns the ENFORCED span mode, so the exit pair and the
// sub-callback re-asserts (s_bldSwitchSavedCull /
// s_vehSwitchSavedCull) re-assert the intended mode — the stale
// cache value can no longer cross a callback boundary either.
// Healthy entries are SILENT (receipts fire only when repaired).
// ============================================================
RwUInt32
pipeEnterCullMode(void)
{
	PF_SCOPE("Pipeline_pipeEnterCullMode");
	// Pre-seeded so a failed Get leaves a known-good value.
	RwUInt32 saved = (RwUInt32)rwCULLMODECULLBACK;
	RwRenderStateGet(rwRENDERSTATECULLMODE, &saved);

	// Sanitize ONLY unknown/out-of-domain values (garbage can never
	// drive the decision below).
	RwUInt32 l1 = saved;
	if(l1 != rwCULLMODECULLNONE && l1 != rwCULLMODECULLBACK &&
	   l1 != rwCULLMODECULLFRONT)
		l1 = rwCULLMODECULLBACK;

	// ---- intended span mode ----
	bool scopeActive = (s_cullScopeDepth > 0 || s_cullScopeOverflow > 0);
	RwUInt32 spanCull = scopeActive ? l1 : (RwUInt32)rwCULLMODECULLBACK;

	// ---- divergence detection: raw device is ground truth ----
	RwUInt32 wantD3D = pipeCullToD3D(spanCull);
	RwUInt32 drv = wantD3D;
	RwD3D9GetRenderState(D3DRS_CULLMODE, &drv);
	RwUInt32 dev = wantD3D;
	bool haveDev = false;
	if(d3d9device){
		DWORD v = (DWORD)wantD3D;
		if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_CULLMODE, &v))){
			dev = (RwUInt32)v;
			haveDev = true;
		}
	}
	bool diverged = (l1 != spanCull) || (drv != wantD3D) ||
	                (haveDev && dev != wantD3D);

	if(diverged){
		// Park-and-set through ALL THREE layers to the intended mode.
		pipeForceCullMode(spanCull);
		// Throttled divergence receipt (first hit unthrottled);
		// SILENT when healthy.
		if(dbglog_once("cullfix_first") || dbglog_throttle("cullfix"))
			dbglog("[cullfix] pipe entry cull reconciled -> %u (scope=%d): "
			       "rw=%u drv=%u dev=%u pipes(bld=%d veh=%d)",
				(unsigned)spanCull, (int)scopeActive,
				(unsigned)saved, (unsigned)drv, (unsigned)dev,
				config ? config->buildingPipe : -1,
				config ? config->vehiclePipe : -1);
	}

	// World depth contract rides the same entry (see its comment).
	pipeEnforceDepthContract();

	return spanCull;
}

void
pipeExitCullMode(RwUInt32 saved)
{
	PF_SCOPE("Pipeline_pipeExitCullMode");
	// Defensive re-sanitize in case a caller passes something else.
	// FRONT/NONE are valid and must survive untouched.
	if(saved != rwCULLMODECULLNONE && saved != rwCULLMODECULLBACK &&
	   saved != rwCULLMODECULLFRONT)
		saved = rwCULLMODECULLBACK;
	// Paired restore: re-assert the ENFORCED span mode (what
	// pipeEnterCullMode returned — sticky-repair semantics, the ped
	// ZWRITE rule: never re-create a pre-entry desync at exit).
	// Receipt only on the rare non-BACK legs (mirror NONE span) —
	// silent on healthy BACK exits.
	if((saved == rwCULLMODECULLNONE || saved == rwCULLMODECULLFRONT) &&
	   dbglog_throttle("cull_exit"))
		dbglog("[Cull] exit re-assert span=%u", (unsigned)saved);
	pipeForceCullMode(saved);
}

// ============================================================
// Cull-scope guard — mirror-span repair (call site: mirrorPass.cpp
// MirrorPass_RenderMirrorBuffer wraps the whole game call in
// Begin(BACK) ... End; storage is declared above pipeEnterCullMode
// because the enter guard consults the depth).
//
// The game wraps CMirrors::RenderMirrorBuffer with CULLNONE and
// restores the previous mode through layer (1) only (the rw cache).
// Pipes running inside that span see the NONE cache on entry and
// legitimately leave all three layers at NONE on exit; the game's
// L1-only restore then moves the cache to BACK while layers (2)/(3)
// (driver cache + raw device) keep NONE. Result: cache says BACK,
// device still draws NONE (double-sided / inside-out geometry) until
// something later re-runs pipeForceCullMode — which can be after a
// draw that inherited the stale device state.
//
// Contract (explicit, so a later caller cannot misread it):
//   pipeCullScopeBegin(rwCull)  sanitizes rwCull to NONE/BACK/FRONT
//                               (out-of-domain -> BACK) and RECORDS it.
//                               Performs NO cache/device writes.
//   pipeCullScopeEnd()          pops the most recent record and
//                               re-asserts it on ALL THREE layers via
//                               pipeForceCullMode (L1/L2 park+target,
//                               unconditional raw L3). Unmatched End is
//                               a logged no-op.
// So Begin records the mode that must hold AFTER the span, not the
// mode active during it. Intended call shape:
//   pipeCullScopeBegin(rwCULLMODECULLBACK);   // mode that must hold
//   ... CMirrors::RenderMirrorBuffer() ...    // game flips NONE (L1)
//   pipeCullScopeEnd();                       // BACK on all 3 layers
//
// Nesting is supported to a depth of 8. Begins beyond that are
// counted, and their matching Ends consume the overflow without
// touching the stack, so pairs can never underflow or misalign.
// Render-thread only, like every other state helper in this file.
//
// While a scope is active, pipeEnterCullMode treats the game's live
// span mode (L1) as the intended cull mode instead of forcing BACK —
// the scope is what marks that value as FRESH foreign intent rather
// than the stale-cache value the enter guard kills. Declarations
// live in src/skygfx.h (pipeCullScopeBegin/pipeCullScopeEnd).
// ============================================================
void
pipeCullScopeBegin(RwUInt32 rwCull)
{
	// Same domain rule as pipeForceCullMode/pipeEnterCullMode: NONE and
	// FRONT are legitimate span modes; everything else falls back to BACK.
	if(rwCull != rwCULLMODECULLNONE && rwCull != rwCULLMODECULLBACK &&
	   rwCull != rwCULLMODECULLFRONT){
		if(dbglog_once("cull_scope_ood"))
			dbglog("[Cull] scope out-of-domain value=%u -> BACK", (unsigned)rwCull);
		rwCull = rwCULLMODECULLBACK;
	}

	if(s_cullScopeDepth < PIPE_CULL_SCOPE_MAX)
		s_cullScopeSaved[s_cullScopeDepth++] = rwCull;
	else
		s_cullScopeOverflow++;
}

void
pipeCullScopeEnd(void)
{
	// Overflowed Begin: consume it without touching the stack so the
	// remaining pairs stay aligned.
	if(s_cullScopeOverflow){
		s_cullScopeOverflow--;
		return;
	}
	if(s_cullScopeDepth == 0){
		if(dbglog_once("cull_scope_unmatched"))
			dbglog("[Cull] scope end without begin (ignored)");
		return;
	}

	RwUInt32 saved = s_cullScopeSaved[--s_cullScopeDepth];
	// Additive receipt: proves the repair ran and which mode it pinned.
	if(dbglog_once("cull_scope"))
		dbglog("[Cull] scope end re-assert=%u depth=%u",
			(unsigned)saved, s_cullScopeDepth);
	pipeForceCullMode(saved);
}

// ============================================================
// Alpha/blend-mode guard — see skygfx.h for the contract.
//
// The building callbacks set only rwRENDERSTATEVERTEXALPHAENABLE
// per mesh and round-trip their other D3D states through the rw
// cache (buildingPipe_saveRenderState). But RwRenderStateGet/Set
// only ever see values that went through RwRenderStateSet — foreign
// raw RwD3D9SetRenderState(D3DRS_*) writes bypass that cache
// entirely:
//   - postfx.cpp:814     D3DRS_BLENDOP = REVSUBTRACT (no rw state
//                        exists for BLENDOP — unfixable rw-side)
//   - postfx.cpp:800+    D3DRS_ALPHATESTENABLE = FALSE (~a dozen
//                        sites; restore balance unverified)
//   - chars.cpp:445-447  SRCBLEND = BLENDFACTOR / DESTBLEND = ONE
//                        (SSS blend-back; rw-set restore of these
//                        no-ops against an unchanged cache)
// so the cache happily reports "canonical" while the device still
// holds the leftover — washed-out/wrongly translucent geometry when
// blend factors disagree, solid-black cutouts when the alpha test
// is off at the device. This pair forces parity at pipe entry and
// closes it at exit — through ALL THREE layers (rw cache, RW driver
// cache, raw device): the driver-cache flush is applied[]-gated and
// can never repair a device a foreign raw write moved, so the blend
// family (ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP and the alpha-
// test trio) also gets an unconditional raw-device write of the very
// values the cache layers hold, the same completion pipeForceCullMode
// performs for cull and pipeForceAlphaBlock performs for the effects
// phase. Without it, a device left at SRCALPHA/INVSRCALPHA + blend ON
// under a cache that already reads "additive" is what additive Im2D
// sprites (coronas) inherit — the black-quad mechanism.
// ============================================================

// RwBlendFunction matches D3DBLEND 1:1 for valid values
// (rwBLENDZERO=1 == D3DBLEND_ZERO=1 ... rwBLENDSRCALPHASAT=11);
// 0 is the RW "NA" placeholder and is not a valid D3D value.
//
// SINGLE SOURCE for the rw->D3D blend mapping (exported via skygfx.h).
// Every site that maps a captured/foreign blend value must call this
// pair instead of re-deriving the domain test or the identity mapping:
//   - pipeBlendHasRwTwin: does an rw twin exist at all? Values outside
//     rwBLENDZERO..rwBLENDSRCALPHASAT (NA and raw-only D3DBLEND extras
//     such as D3DBLEND_BLENDFACTOR=14) have none, so the L1 leg must be
//     skipped and the value restored L2/L3-only (raw verbatim).
//   - pipeBlendToD3D: identity inside that domain, `fallback` outside.
//     `fallback` stays caller-supplied because entry and restore want
//     different defaults (pipe entry: SRCALPHA/INVSRCALPHA).
RwBool
pipeBlendHasRwTwin(RwUInt32 rwBlend)
{
	if(rwBlend >= (RwUInt32)rwBLENDZERO && rwBlend <= (RwUInt32)rwBLENDSRCALPHASAT)
		return TRUE;
	return FALSE;
}

RwUInt32
pipeBlendToD3D(RwUInt32 rwBlend, RwUInt32 fallback)
{
	if(!pipeBlendHasRwTwin(rwBlend))
		return fallback;
	return rwBlend;
}

// ---------------------------------------------------------------------------
// rw RwAlphaTestFunction  ->  D3D D3DCMPFUNC   (RW-domain in, D3D-domain out)
//
// VERIFIED against both headers this project compiles against:
//   external/d3d9/rwplcore.h:5426-5446  enum RwAlphaTestFunction:
//       0 = NA, 1 = NEVER, 2 = LESS, 3 = EQUAL, 4 = LESSEQUAL,
//       5 = GREATER, 6 = NOTEQUAL, 7 = GREATEREQUAL, 8 = ALWAYS
//   DirectX SDK d3d9types.h:263-273     typedef enum _D3DCMPFUNC:
//       1 = NEVER, 2 = LESS, 3 = EQUAL, 4 = LESSEQUAL, 5 = GREATER,
//       6 = NOTEQUAL, 7 = GREATEREQUAL, 8 = ALWAYS
// The two enums are index-for-index IDENTICAL for 1..8 — RW picked its
// ordering to match D3D exactly, the same trick its RwBlendFunction uses
// (see pipeBlendToD3D). So the mapping really is the identity there, and
// any hand-written "correction" that permutes 4..7 (e.g. rw GREATER(5) ->
// D3D LESSEQUAL(4)) would INVERT the test: with the ref==0->1 bump,
// D3D LESS/LESSEQUAL accepts alpha==0, i.e. it draws precisely the
// transparent texels this pipeline exists to discard — the solid-black
// cutout pools. The ONLY non-identity entry is rw 0 = "NA", which has no
// D3D counterpart (D3DRS_ALPHAFUNC must hold 1..8).
//
// The table is spelled out rather than `return rwFunc` for that reason: it
// is the single auditable conversion point every raw D3DRS_ALPHAFUNC writer
// must go through, and it pins the NA fallback to D3DCMP_GREATEREQUAL —
// paired with the ref==0->1 bump that becomes GREATEREQUAL@1, which rejects
// alpha==0. D3DCMP_ALWAYS would reproduce the original accept-everything
// bug; D3DCMP_NEVER would hide the geometry entirely.
//
// Callers pass the value they read from the RW cache (RwRenderStateGet).
// Do NOT feed this a raw D3DCMP_*: those equal the rw value for 1..8 only by
// coincidence of the table above — normalize at the source instead.
// ---------------------------------------------------------------------------
static const RwUInt32 s_alphaFuncLut[9] = {
	/* 0 rw NA            */ (RwUInt32)D3DCMP_GREATEREQUAL,
	/* 1 rw NEVER         */ (RwUInt32)D3DCMP_NEVER,
	/* 2 rw LESS          */ (RwUInt32)D3DCMP_LESS,
	/* 3 rw EQUAL         */ (RwUInt32)D3DCMP_EQUAL,
	/* 4 rw LESSEQUAL     */ (RwUInt32)D3DCMP_LESSEQUAL,
	/* 5 rw GREATER       */ (RwUInt32)D3DCMP_GREATER,
	/* 6 rw NOTEQUAL      */ (RwUInt32)D3DCMP_NOTEQUAL,
	/* 7 rw GREATEREQUAL  */ (RwUInt32)D3DCMP_GREATEREQUAL,
	/* 8 rw ALWAYS        */ (RwUInt32)D3DCMP_ALWAYS,
};

RwUInt32
pipeAlphaFuncToD3D(RwUInt32 rwFunc)
{
	// Index 0 is the NA slot; rwFunc > 8 is garbage and lands on it too.
	if(rwFunc > rwALPHATESTFUNCTIONALWAYS)
		return s_alphaFuncLut[0];
	return s_alphaFuncLut[rwFunc];
}

// L2 blend-family trio (src -> dst -> op) and alpha-test trio
// (ref -> func -> enable). Both alpha paths use the same order, so the
// sequence lives here once; the values stay caller-supplied (world-
// canonical at entry, per-layer captured values at exit).
static void
pipeAlphaBlendTrioL2(RwUInt32 src, RwUInt32 dst, RwUInt32 op)
{
	RwD3D9SetRenderState(D3DRS_SRCBLEND, src);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, dst);
	RwD3D9SetRenderState(D3DRS_BLENDOP, op);
}

static void
pipeAlphaTestTrioL2(RwUInt32 ref, RwUInt32 func, RwUInt32 enable)
{
	RwD3D9SetRenderState(D3DRS_ALPHAREF, ref);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, func);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, enable);
}

PipeAlphaState
pipeEnterAlphaMode(void)
{
	// ---- L1/L2 → flush → L3 doctrine (see also pipeForceCullMode) ----
	// RW's D3D9 driver cache (RwD3D9SetRenderState 0x7FC2D0) parks writes in
	// pending[] and only pushes them to the device inside
	// _rwD3D9RenderStateFlushCache (0x7FC200, run pre-draw) and ONLY when
	// applied[] differs from pending[]. A raw d3d9device->SetRenderState
	// (L3) anywhere else leaves device != cache; a later RW set of a value
	// already in the cache is then a cache-hit skip and the stale raw value
	// survives every flush. Order used here:
	//   (1) L1/L2 canonical writes below,
	//   (2) ONE _rwD3D9RenderStateFlushCache() to commit every pending
	//       dirty state while pending[] == our canonical values, so no
	//       later pre-draw flush can overwrite what we push,
	//   (3) the unconditional L3 raw writes pin device == cache for the span.
	// L2 already holds the same canonical values (written in step (1),
	// committed to applied[] by the flush in step (2)), so no post-L3 L2
	// re-sync is needed — L1, L2, flush, L3 is sufficient.
	PF_SCOPE("Pipeline_pipeEnterAlphaMode");
	PipeAlphaState s;

	// Pre-seeded: a failed Get leaves the out-param untouched, so
	// garbage must never reach the device or a later restore.
	s.vtxAlpha  = FALSE;
	s.srcBlend  = rwBLENDSRCALPHA;
	s.dstBlend  = rwBLENDINVSRCALPHA;
	s.alphaFunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	s.alphaRef  = 0;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s.vtxAlpha);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &s.srcBlend);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s.dstBlend);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &s.alphaFunc);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &s.alphaRef);

	// Device-side snapshot before repair — only feeds the throttled
	// leak report below (5 gets per atomic; cheap vs a draw).
	RwUInt32 dSrc = 0, dDst = 0, dVtx = 0, dOp = 0, dTest = 0;
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &dSrc);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &dDst);
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &dVtx);
	RwD3D9GetRenderState(D3DRS_BLENDOP, &dOp);
	RwD3D9GetRenderState(D3DRS_ALPHATESTENABLE, &dTest);

	// ---- faithful-restore captures (legacy-parity item 6, see skygfx.h) ----
	// ALPHABLENDENABLE / ALPHATESTENABLE / BLENDOP have no rw (L1) twin, and
	// the exit used to DERIVE them (enable <- vtxAlpha, test <- hard TRUE,
	// op <- hard ADD) — re-asserting derived values over the caller's truth:
	// a shadow-polluted device state was reproduced after exit, and consumers
	// that had the alpha test off were force-enabled by every restore. Both
	// remaining layers are snapshotted BEFORE the canonical force below (the
	// L2 values come straight from the reads above — dVtx/dTest/dOp — so no
	// extra driver-cache read is needed; the L3 values come from the raw
	// device). The two layers are stored SEPARATELY because they may
	// legitimately disagree (foreign raw write): the exit restores each layer
	// to its own pre-entry value. Dev pre-seeds = the old derived values, so
	// a skipped read (no device) degrades to the pre-parity restore, never
	// to garbage.
	s.alphaBlendEnable = dVtx;
	s.alphaTestEnable  = dTest;
	// 0 is not a valid D3DBLENDOP (D3DBLENDOP_ADD=1 .. MIN=5) — treat it the
	// way pipeBlendToD3D treats rw NA: fall back to the old canonical ADD.
	s.blendOp          = dOp ? dOp : (RwUInt32)D3DBLENDOP_ADD;
	s.alphaBlendEnableDev = FALSE;
	s.alphaTestEnableDev  = TRUE;
	s.blendOpDev          = (RwUInt32)D3DBLENDOP_ADD;
	if(d3d9device){
		DWORD v = (DWORD)s.alphaBlendEnableDev;
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v);
		s.alphaBlendEnableDev = v;
		v = (DWORD)s.alphaTestEnableDev;
		d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &v);
		s.alphaTestEnableDev = v;
		v = (DWORD)s.blendOpDev;
		d3d9device->GetRenderState(D3DRS_BLENDOP, &v);
		if(v)
			s.blendOpDev = v;	// 0 = failed/garbage get: keep the ADD pre-seed
	}
	// Captures are "present" whenever pipeEnterAlphaMode itself ran; the
	// exit's fallback only triggers on a caller-side pre-seed struct
	// (capMask == 0 — e.g. the main.cpp ped pre-seed literal, zero-filled).
	s.capMask = PAACAP_BLENDENABLE | PAACAP_TESTENABLE | PAACAP_BLENDOP;

	// ---- deterministic entry: rw-set (cache) + raw force (device) ----
	// RwRenderStateSet is a no-op when the cache already holds the
	// target — exactly what a foreign raw write leaves behind — so
	// every state is written through both layers.

	// Blend factors -> world-canonical pair (what TagRenderCB,
	// envmap corona restore and the plant renderer all assume).
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	// Device-side completion of the blend trio. Blend op has NO rw render
	// state — nobody can restore it rw-side; postfx radiosity leaves
	// REVSUBTRACT behind.
	pipeAlphaBlendTrioL2(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);

	// Vertex-alpha baseline OFF: all four building callbacks set
	// rwRENDERSTATEVERTEXALPHAENABLE per mesh before drawing, so
	// this is only the cache/device alignment those per-mesh sets
	// need to actually reach the device (a stale device
	// ALPHABLENDENABLE=TRUE under an already-false cache is
	// invisible to RwRenderStateSet).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

	// Alpha test: keep the caller's rw func/ref semantics (grass
	// dual pass 2 legitimately runs LESS@zwriteThresholdGrass;
	// exterior renderer sets ref=140) but force the DEVICE back in
	// line with the rw cache — postfx raw-sets
	// D3DRS_ALPHATESTENABLE=FALSE in a dozen places — and repair
	// the degenerate ref==0 that CPlantMgr::Render restores
	// (PC_PlantsMgr.cpp:610) while envmap.cpp:280 only re-fixes it
	// outdoors (CGame__currArea==0): GREATEREQUAL@0 accepts every
	// texel, so an alpha-cutout drawn with blending off shows its
	// fully-transparent black texels — the solid-black potted
	//     plant. ref=1 rejects exactly those invisible texels and
	//     nothing a player can see.
	//
	// REF==0 -> 1 BUMP: KEPT. Re-evaluated 2026-09-30 once
	// pipeAlphaFuncToD3D was pinned to the real header values (the rw and
	// D3D enums are identical for 1..8, so the funcs below are the true
	// rw-domain comparison the device ends up running). alpha is 0..255:
	//   GREATEREQUAL(7): >=0 accepts EVERY texEL — the degenerate this bump
	//                    exists for; >=1 rejects alpha==0.  [REQUIRED]
	//   GREATER(5)     : >0 already rejects alpha==0. This is the fn the
	//                    game parks the cache at (every "[PipeAlpha] entry
	//                    resync" sample reports rw fn=5), so for the real
	//                    world path the bump is a no-op safety net that
	//                    additionally drops the invisible alpha==1 texel.
	//   NOTEQUAL(6)    : same shape as GREATER — no-op safety net.
	//   NEVER(1)/ALWAYS(8): ref does not affect the outcome.
	//   LESS/EQUAL/LESSEQUAL(2,3,4): ref=0 is degenerate as well, but no
	//                    ref value turns them into a correct cutout and
	//                    nothing in this codebase selects them for one —
	//                    the bump neither helps nor hurts them.
	// Net: required for GREATEREQUAL, harmless everywhere else; dropping it
	// would regress the solid-black-potted-plant fix above.
	// pipeForceAlphaBlock (buildingPipe.cpp:406) mirrors the same rule for
	// its standalone callers (CSkidmarks__Render) that have no enter here.
	RwUInt32 effRef = (s.alphaRef == 0) ? 1u : s.alphaRef;
	// One conversion, reused by the L2 trio below and the L3 pin later:
	// same rw func -> same D3DCMP value on both layers.
	RwUInt32 d3dFunc = pipeAlphaFuncToD3D(s.alphaFunc);
	if(effRef != s.alphaRef)
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)effRef);
	pipeAlphaTestTrioL2(effRef, d3dFunc, TRUE);

	// ---- raw-only members of the alpha block (see skygfx.h PipeAlphaState) ----
	// Pre-seeded so a failed device Get still restores sane values.
	s.sepAlphaBlend = FALSE;
	s.colorWrite    = D3DCOLORWRITEENABLE_ALL;
	if(d3d9device){
		DWORD v = s.sepAlphaBlend;
		d3d9device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &v); s.sepAlphaBlend = v;
		v = s.colorWrite;
		d3d9device->GetRenderState(D3DRS_COLORWRITEENABLE, &v);         s.colorWrite    = v;
	}
	// Canonical on BOTH cache layers (same pattern as the states above), so the
	// restore at exit is a real cache transition instead of a double no-op.
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);
	// Doctrine step (2): commit every pending dirty driver-cache state NOW,
	// before the raw writes below — the flush is applied[]-gated, so running
	// it here (while pending[] == our canonical values) guarantees no later
	// pre-draw flush can push anything over the L3 block.
	_rwD3D9RenderStateFlushCache();
	pipeL3Set(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	pipeL3Set(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
	pipeL3Set(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	pipeL3Set(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	pipeL3Set(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);

	// ---- (2) -> (3): raw-device completion of the same canonical block ----
	// The flush behind RwD3D9SetRenderState is applied[]-gated (see the
	// cull-mode doctrine above), so it can never repair a device that a
	// foreign RAW write moved while the driver cache agreed with itself —
	// for the blend family this guard only pushed layers (1)/(2) before,
	// which left exactly the cache-noop/device-drift window that makes
	// additive Im2D sprites (coronas) inherit SRCALPHA/INVSRCALPHA and
	// draw their RGB=0 glow background as black quads. Write the device
	// unconditionally with the same values the layers above hold, so the
	// span starts with device == cache on every state it overrides.
	pipeL3Set(D3DRS_ALPHABLENDENABLE, FALSE);
	pipeL3Set(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	pipeL3Set(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pipeL3Set(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	pipeL3Set(D3DRS_ALPHAREF, effRef);
	pipeL3Set(D3DRS_ALPHAFUNC, d3dFunc);
	pipeL3Set(D3DRS_ALPHATESTENABLE, TRUE);

	// Throttled evidence trail: only logs when the device was
	// actually out of line (or the ref=0 leak was live). The last two terms
	// cover the raw-only members (read straight from the device BEFORE the
	// force above) — they are the only signal those states ever give.
	if(dbglog_throttle("pipe_alpha") && (
	   dSrc != (RwUInt32)D3DBLEND_SRCALPHA ||
	   dDst != (RwUInt32)D3DBLEND_INVSRCALPHA ||
	   dVtx != 0u || dOp != (RwUInt32)D3DBLENDOP_ADD ||
	   dTest != 1u || s.alphaRef == 0 ||
	   s.sepAlphaBlend != (RwUInt32)FALSE ||
	   s.colorWrite != (RwUInt32)D3DCOLORWRITEENABLE_ALL))
		dbglog("[PipeAlpha] entry resync: dev src=%u dst=%u vtx=%u op=%u test=%u rw ref=%u fn=%u sepA=%u cwr=0x%X",
		       dSrc, dDst, dVtx, dOp, dTest, s.alphaRef, s.alphaFunc,
		       s.sepAlphaBlend, s.colorWrite);

	// Instrumentation (additive, throttled): state-save receipt — the
	// rw-cached baseline the span is overriding, on every entry.
	if(dbglog_throttle("pipeline_alpha_enter"))
		dbglog("[Pipeline] pipeEnterAlphaMode: saved rw(vtx=%u src=%u dst=%u fn=%u ref=%u) sepA=%u cwr=0x%X -> canonical entry pipes(bld=%d veh=%d)",
			(unsigned)s.vtxAlpha, (unsigned)s.srcBlend, (unsigned)s.dstBlend,
			(unsigned)s.alphaFunc, (unsigned)s.alphaRef,
			(unsigned)s.sepAlphaBlend, (unsigned)s.colorWrite,
			config ? config->buildingPipe : -1,
			config ? config->vehiclePipe : -1);

	return s;
}

void
pipeExitAlphaMode(PipeAlphaState saved)
{
	PF_SCOPE("Pipeline_pipeExitAlphaMode");
	// rw-first (so nested/consumer Gets see the restore), then
	// raw-force: if the cache already reads `saved` the rw call
	// is a no-op — we may have raw-forced only the device away
	// from it, and the raw force is what closes that gap. This
	// is the restore direction cache-only save/restore cannot do.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)saved.vtxAlpha);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)saved.srcBlend);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)saved.dstBlend);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)saved.alphaFunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)saved.alphaRef);

	// Device-domain mirrors of the saved rw values — computed ONCE and
	// pushed through BOTH layer (2) and layer (3) below, so the two layers
	// can never disagree on what "restored" means.
	RwUInt32 devSrc = pipeBlendToD3D(saved.srcBlend, D3DBLEND_SRCALPHA);
	RwUInt32 devDst = pipeBlendToD3D(saved.dstBlend, D3DBLEND_INVSRCALPHA);
	RwUInt32 devFn  = pipeAlphaFuncToD3D(saved.alphaFunc);

	// ---- faithful restore of the three formerly-derived states ----
	// (legacy-parity item 6; captures live in PipeAlphaState — skygfx.h.)
	// pipeEnterAlphaMode snapshotted the real pre-entry ALPHABLENDENABLE /
	// ALPHATESTENABLE / BLENDOP on layer (2) AND layer (3); the exit hands
	// each layer back its OWN captured value instead of re-asserting the old
	// derived triple (enable <- vtxAlpha, test <- hard TRUE, op <- hard
	// ADD). Fallback = that pre-parity derived restore bit-for-bit, used
	// ONLY when the capture group is missing (capMask bit unset — a caller-
	// side pre-seeded struct that never went through pipeEnterAlphaMode;
	// pipeEnterAlphaMode itself always sets the bits).
	RwUInt32 devEnableL2 = saved.vtxAlpha ? TRUE : FALSE;	// pre-parity derived
	RwUInt32 devEnableL3 = devEnableL2;
	if(saved.capMask & PAACAP_BLENDENABLE){
		devEnableL2 = saved.alphaBlendEnable;
		devEnableL3 = saved.alphaBlendEnableDev;
	}
	RwUInt32 devTestL2 = TRUE, devTestL3 = TRUE;		// pre-parity unconditional
	if(saved.capMask & PAACAP_TESTENABLE){
		devTestL2 = saved.alphaTestEnable;
		devTestL3 = saved.alphaTestEnableDev;
	}
	RwUInt32 devOpL2 = (RwUInt32)D3DBLENDOP_ADD;		// pre-parity unconditional
	RwUInt32 devOpL3 = devOpL2;
	if(saved.capMask & PAACAP_BLENDOP){
		devOpL2 = saved.blendOp;
		devOpL3 = saved.blendOpDev;
	}

	// SEV1 L1/L2 SPLIT: the blend-enable state is ONE unit across L1 (rw
	// cache, VERTEXALPHAENABLE), L2 (driver cache, D3DRS_ALPHABLENDENABLE)
	// and L3 (raw device). The old TRUE-only mirror could leave L1 TRUE
	// (from the park above) while L2 got the captured FALSE — then a later
	// consumer's RwRenderStateSet(VERTEXALPHAENABLE, FALSE) was a cache
	// no-op on L2 and the device kept blending. Drive BOTH caches from the
	// captured DEVICE enable (the truth pre-entry): TRUE -> L1 AND L2 TRUE,
	// FALSE -> L1 AND L2 FALSE, then pin that same value raw on L3 below
	// (pipeL3Set(D3DRS_ALPHABLENDENABLE, devEnableL3)). Never divergent.
	devEnableL2 = devEnableL3;	// L1 and L2 are one unit — same value
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)devEnableL2);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, devEnableL2);
	pipeAlphaBlendTrioL2(devSrc, devDst, devOpL2);
	// faithful ref (incl. 0) -> func -> enable, one L2 order.
	pipeAlphaTestTrioL2(saved.alphaRef, devFn, devTestL2);

	// ---- (2) -> (3): raw-device completion of the restore ----
	// The driver-cache writes above are applied[]-gated: when pending[]
	// already equals what RW last flushed, a foreign RAW write that moved
	// ONLY the device during the pipe span survives this restore forever —
	// and the sprite/corona phase runs right after the last pipe exit, so
	// that stale device state is exactly what additive Im2D draws inherit
	// (the cache-noop/device-drift mechanism behind the black-corona
	// quads). Unconditional raw writes pin device == cache at every exit,
	// mirroring pipeForceCullMode's contract and the same completion
	// pipeForceAlphaBlock performs for the effects phase.
	// Raw-only members, L2 side: written BEFORE the flush below so their
	// pending[] entries commit together with the rest of the restore —
	// pending[] is never left dirty after the flush. Their L3 mirrors land
	// after it (same value pin as the block below). Cache first (so a later
	// raw read agrees), then raw — the raw write is what closes the gap when
	// the cache already held the saved value.
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, saved.sepAlphaBlend);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, saved.colorWrite);

	// Doctrine step (2), restore direction: the L1/L2 writes above left the
	// restored values in pending[]; commit them to applied[] NOW so no later
	// pre-draw flush can push a stale pending[] over the raw writes below.
	_rwD3D9RenderStateFlushCache();
	pipeL3Set(D3DRS_ALPHABLENDENABLE, devEnableL3);
	pipeL3Set(D3DRS_SRCBLEND, devSrc);
	pipeL3Set(D3DRS_DESTBLEND, devDst);
	pipeL3Set(D3DRS_BLENDOP, devOpL3);
	pipeL3Set(D3DRS_ALPHAREF, saved.alphaRef);
	pipeL3Set(D3DRS_ALPHAFUNC, devFn);
	pipeL3Set(D3DRS_ALPHATESTENABLE, devTestL3);

	// Raw-only members, L3 side: raw-device mirrors of the same saved values
	// (their L2 writes above are applied[]-gated, so this unconditional write
	// is what repairs a device a foreign raw write moved during the span).
	pipeL3Set(D3DRS_SEPARATEALPHABLENDENABLE, saved.sepAlphaBlend);
	pipeL3Set(D3DRS_COLORWRITEENABLE, saved.colorWrite);

	// Instrumentation (additive, throttled): state-restore receipt —
	// the exact values handed back to the caller's baseline.
	if(dbglog_throttle("pipeline_alpha_exit"))
		dbglog("[Pipeline] pipeExitAlphaMode: restored rw(vtx=%u src=%u dst=%u fn=%u ref=%u) sepA=%u cwr=0x%X",
			(unsigned)saved.vtxAlpha, (unsigned)saved.srcBlend, (unsigned)saved.dstBlend,
			(unsigned)saved.alphaFunc, (unsigned)saved.alphaRef,
			(unsigned)saved.sepAlphaBlend, (unsigned)saved.colorWrite);

	// ---- divergence receipt (parity item 6) ----
	// Fires ONLY when the faithful restore differs from what the pre-parity
	// exit would have written (that is the observable behaviour change), or
	// when the two captured layers disagreed with each other going in
	// (layer-desync evidence). One-shot tag so the run's first hit is never
	// starved by throttle-bucket collisions, then the 2s throttle. POD
	// locals only; the compare itself is free (struct reads).
	RwUInt32 derivedEnable = saved.vtxAlpha ? TRUE : FALSE;
	bool faithfulDiverged =
		(saved.capMask & PAACAP_BLENDENABLE) &&
		(saved.alphaBlendEnable    != derivedEnable ||
		 saved.alphaBlendEnableDev != derivedEnable ||
		 saved.alphaBlendEnable    != saved.alphaBlendEnableDev);
	faithfulDiverged = faithfulDiverged ||
		((saved.capMask & PAACAP_TESTENABLE) &&
		 (saved.alphaTestEnable != TRUE ||
		  saved.alphaTestEnableDev != TRUE));
	faithfulDiverged = faithfulDiverged ||
		((saved.capMask & PAACAP_BLENDOP) &&
		 (saved.blendOp != (RwUInt32)D3DBLENDOP_ADD ||
		  saved.blendOpDev != (RwUInt32)D3DBLENDOP_ADD));
	if(faithfulDiverged &&
	   (dbglog_once("pipe_alpha_restore_first") ||
	    dbglog_throttle("pipe_alpha_restore")))
		dbglog("[PipeAlpha] faithful restore (parity 6): rw vtx=%u — captured "
		       "vs pre-parity: enable L2=%u L3=%u (derived %u), "
		       "test L2=%u L3=%u (derived 1), op L2=%u L3=%u (derived 1)",
			(unsigned)saved.vtxAlpha,
			(unsigned)saved.alphaBlendEnable, (unsigned)saved.alphaBlendEnableDev,
			(unsigned)derivedEnable,
			(unsigned)saved.alphaTestEnable, (unsigned)saved.alphaTestEnableDev,
			(unsigned)saved.blendOp, (unsigned)saved.blendOpDev);
}

// ============================================================
// Caller audit for the faithful alpha restore (parity item 6) — every
// pipeEnterAlphaMode/pipeExitAlphaMode pair, and what each assumed about
// the old derived exit (ALPHATESTENABLE=TRUE, BLENDOP=ADD, enable<-
// vtxAlpha). Documented here because the caller TUs are owned by their
// own fixes:
//
//   * main.cpp ped path (myDefaultCallback ~:1429-1797): pre-seeds
//     savedAlpha as a 7-field aggregate literal — zero-fills the new
//     fields, so a defensive exit-without-enter would take the FALLBACK
//     branch = exactly the old derived restore. The real path always
//     overwrites it with pipeEnterAlphaMode() at entry. Between enter and
//     exit it re-pushes only alphaFunc/alphaRef (ALPHATESTENABLE
//     deliberately untouched) and runs the ped opaque contract; on exit
//     pedOpaqueRestore (main.cpp:1007) runs AFTER pipeExitAlphaMode and
//     rewrites the full pre-entry L1/L2/L3 alpha set (incl. devAlphaTest/
//     devBlend) — the ped path is the last writer, so nothing there
//     relied on the exit leaving test TRUE. No change in behaviour.
//
//   * buildingPipe.cpp Switch (~:2485-2590): plain enter/exit pair;
//     bpipeBlendLeakProbe("Switch") runs AFTER the exit and only repairs
//     the exact broken signature (device blend OFF + stale ONE/ONE) —
//     with the faithful restore the device post-exit now holds the true
//     pre-entry state, so the probe fires exactly as often as the
//     pre-entry truth is that signature (same as before). The building
//     cbs themselves draw INSIDE the pair, where the entry force still
//     owns ALPHATESTENABLE=TRUE — unchanged.
//
//   * vehiclePipe.cpp Switch (~:3166-3303): PARTIAL GAP, pre-existing
//     code, vehiclePipe.cpp is outside this change's write scope: right
//     after pipeExitAlphaMode it re-asserts a blend quartet at layer (3)
//     raw (vehiclePipe.cpp:3289-3296) with ALPHABLENDENABLE derived from
//     vtxAlpha and BLENDOP hard ADD — the same derived re-assert this
//     item removes from the exit itself. On a vehicle draw whose
//     pre-entry device enable/op disagreed with the rw cache, that
//     re-assert overrides the faithful L3 values. Follow-up: that block
//     should read the captured values (savedAlpha.alphaBlendEnableDev /
//     blendOpDev) now that they exist.
//
//   * Standalone pipeForceAlphaBlock callers (CSkidmarks__Render
//     main.cpp:2785, effectsForceAlphaBlock main.cpp:3102): no enter/exit
//     pair — they own the canonical ENTRY force (test TRUE / op ADD),
//     which is deliberately unchanged by this item. Their post-draw
//     states now survive until the NEXT entry force instead of being
//     re-derived by an exit — that is the parity goal, not a regression.
//
//   * pipeForceAlphaBlock's lag-aware drift log (buildingPipe.cpp:690-
//     704) reads only vtxAlpha/alphaFunc/alphaRef/srcBlend/dstBlend from
//     the snapshot — untouched fields.
//
//   * neoCarpipe.cpp / postfx.cpp references to pipeEnter/pipeExitAlpha
//     Mode are comments on layer desync, not behavioural dependencies on
//     the old derived exit.
// ============================================================

// ============================================================
// pipeApplyWorldBaseline — canonical frame-boundary state.
//
// Runs ONCE per frame boundary (never per draw), so the whole world
// span starts from a known baseline on all three layers:
//   (1) rw cache (RwRenderStateSet),
//   (2) RW D3D9 driver cache (RwD3D9SetRenderState), committed by
//       _rwD3D9RenderStateFlushCache,
//   (3) raw IDirect3DDevice9::SetRenderState.
// Layers (1) and (2) skip work when they already hold the requested
// value, so a device that a foreign RAW write (or a Reset) moved
// away from the cache survives a plain cache set — the same
// cache-hit-skip mechanism pipeForceCullMode documents above. The
// park-and-set idiom therefore writes a guaranteed-DIFFERENT value
// first (no draw can happen between the two sets, so the transient
// never reaches the device), then the target, then the flush, then
// pins the device raw with the same target values.
//
// DELIBERATELY ABSENT: the alpha-test comparison function and its
// reference value. A global GREATEREQUAL@1 here can reintroduce cutout
// holes in passes that rely on the caller's own func/ref —
// pipeEnterAlphaMode owns that block. Alpha blending itself is only
// ever DISABLED by the baseline, never enabled.
// ============================================================
void
pipeApplyWorldBaseline(const char *tag)
{
	// Null-device safe: nothing to pin when the device is gone
	// (Reset teardown / failed init reach this through the same path).
	if(!d3d9device)
		return;

	// Instrumentation (throttled): one receipt per interval, never
	// one line per call.
	if(dbglog_throttle("state_baseline"))
		dbglog("[Pipeline] pipeApplyWorldBaseline: %s", tag ? tag : "(none)");
	// One-shot unthrottled diagnostic: cull is pinned BACK below.
	if(dbglog_once("cull_baseline"))
		dbglog("[Cull] baseline BACK");

	// ---- (1) L1 rw cache: park-and-set ----
	// Blend pair: park on the opposite canonical value, then target.
	pipeRwParkSet(rwRENDERSTATESRCBLEND, rwBLENDINVSRCALPHA, rwBLENDSRCALPHA);
	pipeRwParkSet(rwRENDERSTATEDESTBLEND, rwBLENDSRCALPHA, rwBLENDINVSRCALPHA);
	// Depth block: park FALSE, then TRUE.
	pipeRwParkSet(rwRENDERSTATEZWRITEENABLE, FALSE, TRUE);
	pipeRwParkSet(rwRENDERSTATEZTESTENABLE, FALSE, TRUE);
	// Vertex alpha OFF. No park: parking on TRUE would request alpha
	// blending, which this baseline never enables.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	// Cull block: park FRONT, then BACK (SA world default).
	pipeRwParkSet(rwRENDERSTATECULLMODE, rwCULLMODECULLFRONT, rwCULLMODECULLBACK);

	// ---- (2) L2 RW driver cache: park-and-set, same targets ----
	pipeL2ParkSet(D3DRS_SRCBLEND, D3DBLEND_INVSRCALPHA, D3DBLEND_SRCALPHA);
	pipeL2ParkSet(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
	pipeL2ParkSet(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT, D3DBLENDOP_ADD);
	pipeL2ParkSet(D3DRS_ZWRITEENABLE, FALSE, TRUE);
	pipeL2ParkSet(D3DRS_ZENABLE, D3DZB_FALSE, D3DZB_TRUE);
	pipeL2ParkSet(D3DRS_ZFUNC, D3DCMP_ALWAYS, D3DCMP_LESSEQUAL);
	// Separate alpha blend OFF (target only: the flush below commits
	// pending[] and layer (3) pins the device).
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	pipeL2ParkSet(D3DRS_COLORWRITEENABLE, 0, 0xF);
	pipeL2ParkSet(D3DRS_ALPHATESTENABLE, FALSE, TRUE);
	// Alpha blending OFF: target only, never parked on TRUE.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	// Cull: park CCW/FRONT, then CW/BACK (identity rw->D3D mapping).
	pipeL2ParkSet(D3DRS_CULLMODE, D3DCULL_CCW, D3DCULL_CW);

	// ---- (2) -> (3): commit pending[] while it holds our targets ----
	_rwD3D9RenderStateFlushCache();

	// ---- (3) raw device: pin device == cache with the same targets ----
	pipeL3Set(D3DRS_ALPHABLENDENABLE, FALSE);
	pipeL3Set(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	pipeL3Set(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pipeL3Set(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	pipeL3Set(D3DRS_ZWRITEENABLE, TRUE);
	pipeL3Set(D3DRS_ZENABLE, D3DZB_TRUE);
	pipeL3Set(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	pipeL3Set(D3DRS_CULLMODE, D3DCULL_CW);
	pipeL3Set(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	pipeL3Set(D3DRS_COLORWRITEENABLE, 0xF);
	pipeL3Set(D3DRS_ALPHATESTENABLE, TRUE);
}

// ============================================================
// pipeBindNoise — single entry point for the universal noise tile.
//
// Sampler stage is the caller's (s9 is the project-wide reservation; s10 is
// reserved for a future standalone blue-noise tap and must NOT be used here).
// The c49-c52 block is the shared default knob set from shaders/include/
// Noise.hlsl; a shader that wants different values should keep passing them
// explicitly to the Noise* helpers rather than re-uploading this block.
//
// Lifetimes:
//   * SkyNoise::Init() is CPU-only and idempotent; the guard just keeps the
//     per-call cost down (Init early-outs on (initialised && same seed) too).
//   * The D3D texture itself is created lazily inside BindAndUpload and is
//     released pre-Reset by SkyNoise::ReleaseDeviceObjects() (depthhook.cpp)
//     then recreated here on the next frame.
//
// Seed is fixed at 1337: this lane owns no config surface. Determinism means
// every run bakes the same tile, so a debug log is enough to reproduce.
// ============================================================
void
pipeBindNoise(int samplerStage)
{
	static bool s_noiseInit = false;
	if(!s_noiseInit){
		SkyNoise::Init(1337u);
		s_noiseInit = true;
	}

	// Null-device safe: nothing to bind and no constants to stamp
	// (Reset teardown / failed init reach this through the same path).
	if(!d3d9device)
		return;

	// Time source: GetTickCount is the clock this tree already uses for
	// elapsed/debounce math (skygfx.h dbglog_throttle, buildingPipe.cpp
	// repair timer); there is no game-side elapsed-seconds counter exposed
	// here (CClock is wall-clock HH:MM, CTimer__ms_fTimeStep is a delta).
	// Masked to 31 bits so the unsigned -> float conversion cannot go
	// negative; the ~24.8-day wrap is a one-frame phase jump in an
	// animation-only term.
	float timeSeconds = (float)(GetTickCount() & 0x7FFFFFFFu) * 0.001f;
	unsigned frameIndex = (unsigned)RWSRCGLOBAL(renderFrame);

	float p49[4] = { 8.0f, 4.0f, 0.45f, 0.35f };
	float p50[4] = { 16.0f, 32.0f, timeSeconds, (float)frameIndex };
	float p51[4] = { 0.10f, 0.32f, 1.0f, 0.6f };
	// c52.w = SkyNoise tile size in texels (Noise.hlsl NoiseTileOK signal):
	// every pipeBindNoise consumer can now degrade to its legacy-grain
	// fallback when the tile is low-res (32) and compose both bands. 0 would
	// mean "no signal" — UniversalSize() is valid here because Init() above
	// has already baked the CPU tile (the D3D texture may still fail later;
	// the shader's NoiseTileLive() probe covers that case independently).
	float p52[4] = { 0.012f, 0.35f, 0.5f, (float)SkyNoise::UniversalSize() };

	SkyNoise::BindAndUpload(samplerStage, d3d9device, timeSeconds, frameIndex,
	                        p49, p50, p51, p52);

	// One-shot receipt: which stage got the tile. Texture creation already
	// logs its own one-shot line (tag noise_tex); this one proves the bind
	// path ran at all, so a missing s9 upload is attributable.
	if(dbglog_once("noise_bind")){
		const SkyNoise::Stats &st = SkyNoise::GetStats();
		dbglog("[Pipeline] pipeBindNoise: first bind stage=%d size=%u tex=%d seed=%u "
		       "quality=%d c49={%.2f %.2f %.2f %.2f} time=%.3f frame=%u",
		       samplerStage, st.size, st.textureLive, st.seed, st.quality,
		       p49[0], p49[1], p49[2], p49[3], timeSeconds, frameIndex);
	}
}
