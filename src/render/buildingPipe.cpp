#include "skygfx.h"
#include "postfx.h"	// GetScreenSize — trusted screen-size cache (transient-camRas fix)
#include "brdfLibrary.h"
#include "treeWind.h"	// W8-2 tree lane: TreeWind_ApplyToTransform (PBR/IV tilt),
 						// TreeWind_UploadWindExt (VS c23), TreeAlpha_* (alpha/PS)
#include "profiler.h"	// PF_SCOPE / PfBindVS / PfBindPS — instrumentation only
#include "texdefs.h"	// texdb.txt TexInfo + NM lane: <diffuse>_n pairing lookup (s8 bind)
#include "texalpha.h"	// per-texture alpha classification (solid vs cutout/faded)
//#include <fstream>

void *ps2BuildingVS;
void *ps2BuildingFxVS;
void *ps2BuildingWindVS;
void *gtaivBuildingVS;
void *gtaivBuildingPS;
void *simpleDetailNoiseDetailPS;
void *simpleDetailPS;
void *simpleNoiseDetailPS;
void *xboxBuildingWindVS;
void *xboxBuildingVS;
void *xboxBuildingNoiseDetailPS;
void *xboxBuildingPS;
void *sphereBuildingVS;
void *simpleFogPS;
void *buildingPBRVS;
void *buildingPBRPS;

RxPipeline *&CCustomBuildingPipeline__ObjPipeline = *(RxPipeline**)0xC02C68;
RxPipeline *&CCustomBuildingDNPipeline__ObjPipeline = *(RxPipeline**)0xC02C1C;

float &CWeather__WetRoads = *(float*)0xC81308;

extern CVector2D windPos;
extern void pipeEnsureIBLBuffer(void);	// pipelinecommon.cpp — shared once-per-frame IBL gate
extern void *ps2EnvSpecFxPS;	// vehiclePipe.cpp — PS2 env+spec dual-layer FX pass
// GTAIVForwardPlus pair cache (pipelinecommon.cpp) + its lazy load path —
// same makeVS/makePS chokepoints CreateShaders uses for every embedded shader
// (IDR_ -> RCDATA in resources/Resource.rc). vehiclePipe.cpp reuses
// ivFPReady() (defined below) for its own IV swap.
extern void *gtaivFPVS, *gtaivFPPS;
void makePS(int res, void **sh);
void makeVS(int res, void **sh);
// pipelinecommon.cpp — THE single rw->D3D conversion point for
// D3DRS_ALPHAFUNC. The rw and D3D compare enums are identical for 1..8 but
// rw 0 ("NA") has no D3D counterpart, so every raw ALPHAFUNC write in this
// file must go through it instead of pushing the rw value straight down.
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);
//extern std::fstream lg;

// RW 3.6 D3D9 driver-cache flush (gta_sa 0x7FC200; WRAPPER definition in
// main.cpp) — same file-scope extern resolution as pipelinecommon.cpp:17 /
// waterPipe.cpp:75. Ordering contract: it must run BEFORE any raw
// dev->SetRenderState layer-(3) write so every pending[] dirty state is
// committed first and no later pre-draw flush can push a stale pending[]
// value over the raw write (RW 3.6 cache-hit skip: a later
// RwD3D9SetRenderState of a value already in stateCache[] marks nothing
// dirty -> flush no-op -> the device keeps the raw value forever).
void _rwD3D9RenderStateFlushCache(void);

// postfx.cpp — PipeChain classify bracket (pack-buffer second draw).
// Begin binds the classify RT, forces ZWRITE/blend/fog FALSE through layers
// (1)+(2) and forces PipeChainShader + c0/c1; End restores them and the
// caller re-sets its own pixel shader. The draw between the two is
// SEH-fenced (buildingPipe_guardedClassifyDraw below) so a fault can never
// skip the End — a stranded bracket leaves every later building mesh that
// frame drawing into the pack buffer with no depth write (ghosting).
extern bool PipeChain_ClassifyBegin(float, float, float, float);
extern void PipeChain_ClassifyEnd(void);


enum {
	// common
	REG_transform	= 0,
	REG_ambient	= 4,
	REG_directCol	= 5,	// 7 lights (main + 6 extra)
	REG_directDir	= 12,	//
	REG_xbDirectDir	= 6,	// Xbox building VS sun direction (c6). c5 doubles as
						// REG_directCol for the single Xbox sun color; c6 is free
						// in the Xbox VS family (xboxBuildingVS/xboxBuildingWindVS
						// declare c0/c4/c19-c23/c29-c32/c36-c38 only — c5-c18 are
						// the PS2/GTAIV/PBR VS light-array space, unused here).
	REG_matCol	= 19,
	REG_surfProps	= 20,
	// Wind
	REG_windPos = 21,
	REG_windIntensity = 22,

	REG_shaderParams= 29,
	// PBR building
	REG_worldMat = 24,	// 4x4 world matrix (c24-c27) for PBR vertex shader
	// DN and UVA
	REG_dayparam	= 30,
	REG_nightparam	= 31,
	REG_texmat	= 32,
	// Env
	REG_fxParams	= 36,
	REG_envXform	= 37,
	REG_envmat	= 38,

};

// ============================================================
// c20 "surfProps" packing for the building vertex shaders.
//
// EVERY building VS in this project reads the register as a PACKED float4:
//     #define surfAmb  (surfProps.x)
//     #define surfDiff (surfProps.z)   <- xboxBuildingVS / buildingPBRVS /
//                                         GTAIVBuilding_vs / vehicleVS alike
// (buildingPBRVS additionally reads .y as surfLightScale — the dynamic-light
//  scale; none of the other building VS reads .y.)
//
// RwSurfaceProperties' MEMORY layout is { ambient, specular, diffuse }
// (external/d3d9/rwplcore.h:1492-1497 — NOT the "ambient, diffuse, specular"
// the doxygen prose above it suggests, which this comment previously repeated).
// Uploading the raw 3-float struct to a 4-component constant therefore maps
// .y=specular, .z=diffuse AND reads 4 bytes past the struct for .w. The envmap
// flag SA keeps in surfaceProps.specular (`*(int*)&surfaceProps.specular & 1`,
// the detect at buildingPipe:569/695, bits 0-2 are stock SA's) lands in .y,
// not .z.
//
// SA assets additionally ship diffuse=0 with the lighting baked into the
// vertex prelight (documented in the PBR cb below), so the by-contract
// surfDiff (.z) is 0 for ordinary materials and `prelight * surfDiff`
// collapses to 0, leaving the ambient term alone: that is the "huge black
// floor" in the Xbox building pipe and the near-black scene in the GTAIV
// pipe. The PBR cb has packed this BY NAME all along (buildingPipe.cpp
// surfUpload); this helper gives the PS2/GTAIV and Xbox cbs the same layout
// and the same diffuse==0 prelight fallback, without the raw-struct overread.
// ============================================================
static void
buildingPipe_uploadSurfProps(RpMaterial *material, int loc)
{
	RwSurfaceProperties const &sp = material->surfaceProps;
	float v[4] = { sp.ambient, sp.diffuse, sp.diffuse, 0.0f };
	// diffuse==0 is the SA baked-lighting convention: prelight must pass at
	// full strength (identical fallback to the PBR cb's surfUpload).
	if(v[2] < 1e-4f)
		v[2] = 1.0f;
	RwD3D9SetVertexShaderConstant(loc, v, 1);
}

float &CCoronas__LightsMult = *(float*)0x8D4B5C;
bool &CWeather__LightningFlash = *(bool*)0xC812CC;
WRAPPER bool CPostEffects__IsVisionFXActive(void) { EAXJMP(0x7034F0); }

// Vision-FX / cutscene state (SA 1.0 US addresses) — defined here (NOT next
// to bpipeLightAuditLog below) because setDnParams' dnBal leak repair reads
// them. See the "outdoor cutscene dark" dig-field comment above
// bpipeLightAuditLog for the full leak description.
static bool &CPostEffects__m_bInCutscene = *(bool*)0xC402B7;
static bool &CPostEffects__m_bNightVision = *(bool*)0xC402B8;
static bool &CPostEffects__m_bInfraredVision = *(bool*)0xC402B9;
static float &CPostEffects__m_VisionFXDayNightBalance = *(float*)0x8D50A4;

RwRGBAReal buildingAmbient;

void (*CustomBuildingPipeline__Update_orig)(void);
void
CustomBuildingPipeline__Update(void)
{
	CustomBuildingPipeline__Update_orig();

	// NOTE: UpdateTimecycleLighting() is deliberately NOT called here.
	// When wired into any per-frame hook, GetTimecycleAmbient() returns
	// the game's raw ambient values which include timecycle ambient that
	// may be near-zero at certain hours or under certain weather/interior
	// conditions. The working build (1,807,872) had no call site for
	// UpdateTimecycleLighting() — s_tcAmbient stayed {0,0,0} and
	// buildingAmbient was set from GetTimecycleAmbient() (which was 0).
	// The building pipe's baked vertex lighting compensates, and the
	// vehicle/ped/vehicle PBR paths survived via IBL/env/headlights.
	// Calling UpdateTimecycleLighting() introduces live timecycle values
	// that break the existing tuned balance. If re-enabling in future,
	// validate against the "good" reference (white car visible at 01:28,
	// peds visible in daytime, no sepia veil) at each time of day.

	// Unified ambient: use the SAME helper as vehicles (uploadLights) and peds
	// (myDefaultCallback pAmbient override) so all three pipes receive the exact
	// same timecycle ambient in the same amount. GetTimecycleAmbientPBR() is a
	// pass-through of GetTimecycleAmbient() plus the opt-in pbrAmbientFloor, so
	// this is identical to the old call when the floor is off (default) but keeps
	// buildings consistent if the floor is ever enabled.
	buildingAmbient = GetTimecycleAmbientPBR();

	if(config->lightningIlluminatesWorld && CWeather__LightningFlash && !CPostEffects__IsVisionFXActive())
		buildingAmbient = { 1.0, 1.0, 1.0, 0.0 };
}

// File-level cache statics for CustomBuildingEnvMapPipeline__SetupEnv
// (promoted from function-static so they can be nulled on device reset)
static RwMatrix s_setupEnv_lastmat;
static void *s_setupEnv_lastobject = NULL;
static RwFrame *s_setupEnv_lastfrm = NULL;
static RwUInt16 s_setupEnv_lastrenderframe = 0;

void
CustomBuildingEnvMapPipeline__SetupEnv(RpAtomic *atomic, RwFrame *envframe, RwMatrix *envmat)
{
	RwMatrix inv;
	RpClump *clump;
	RwFrame *frame;

	if(envframe == NULL){ RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera); if(cam) envframe = RwCameraGetFrame(cam); }
	// NULL curCamera faults our image at RVA 0x4D5A reading [NULL+4]

	clump = RpAtomicGetClump(atomic);

	if(s_setupEnv_lastobject != (clump ? (void*)clump : (void*)atomic) ||
	   s_setupEnv_lastfrm != envframe ||
	   s_setupEnv_lastrenderframe != RWSRCGLOBAL(renderFrame)){
		frame = clump ? RpClumpGetFrame(clump) : RpAtomicGetFrame(atomic);
		if(!frame || !envframe){
			RwMatrixSetIdentity(envmat);
			return;
		}
		RwMatrixInvert(&inv, RwFrameGetLTM(envframe));
		RwMatrixMultiply(&s_setupEnv_lastmat, RwFrameGetLTM(frame), &inv);
		if((rwMatrixGetFlags(&s_setupEnv_lastmat) & rwMATRIXTYPEMASK) != rwMATRIXTYPEORTHONORMAL)
			RwMatrixOrthoNormalize(&s_setupEnv_lastmat, &s_setupEnv_lastmat);

		s_setupEnv_lastobject = (clump ? (void*)clump : (void*)atomic);
		s_setupEnv_lastfrm = envframe;
		s_setupEnv_lastrenderframe = RWSRCGLOBAL(renderFrame);
	}
	*envmat = s_setupEnv_lastmat;
}

void
setDnParams(RpAtomic *atomic)
{
	float balance;
	float dayparam[4], nightparam[4];

	balance = CCustomBuildingDNPipeline__m_fDNBalanceParam;

	// dnBal leak repair (read-only — vanilla owns the global 0x8D12C0):
	// FilterFX_StoreAndSetDayNightBalance (0x7034B0) / Restore (0x7034D0)
	// write the vision-FX neutral blend (0.5) into m_fDNBalanceParam, both
	// gated on !m_bInCutscene — a vision-FX/cutscene interleave leaves dnBal
	// stuck at 0.5 (log-proven: [BLIT] dnBalRaw=0.500 in all 11 cutscene
	// samples at clock 6:30-6:46). 0.5 = 50% night prelight at dawn = dark
	// buildings. When a cutscene IS active and NO vision FX is active,
	// recompute the vanilla CCustomBuildingRenderer::UpdateDayNightBalanceParam
	// (0x5D7F80) clock formula locally and upload THAT instead of the leaked
	// global. When vision FX is active (it legitimately owns the blend) or
	// no cutscene, keep the global byte-identical.
	if(CPostEffects__m_bInCutscene &&
	   !CPostEffects__m_bNightVision &&
	   !CPostEffects__m_bInfraredVision){
		int minutesToday = (int)CClock__ms_nGameClockHours*60 +
		                   (int)CClock__ms_nGameClockMinutes;
		float recomputed;
		if(minutesToday < 360)        // < 6:00 → full night
			recomputed = 1.0f;
		else if(minutesToday < 420)   // 6:00-7:00 → dawn decay 1→0
			recomputed = (420.0f - (float)minutesToday) / 60.0f;
		else if(minutesToday < 1200)  // 7:00-20:00 → full day
			recomputed = 0.0f;
		else if(minutesToday < 1260)  // 20:00-21:00 → dusk rise 0→1
			recomputed = 1.0f - (1260.0f - (float)minutesToday) / 60.0f;
		else                          // ≥ 21:00 → full night
			recomputed = 1.0f;
		if(recomputed < 0.0f) recomputed = 0.0f;
		if(recomputed > 1.0f) recomputed = 1.0f;
		if(dbglog_throttle("dnbal_repair"))
			dbglog("[dnbal_repair] cut=1 nv=0 ir=0 clock=%d:%02d leaked=%.3f recomputed=%.3f",
			       (int)CClock__ms_nGameClockHours, (int)CClock__ms_nGameClockMinutes,
			       balance, recomputed);
		balance = recomputed;
	}

	if(balance < 0.0f) balance = 0.0f;
	if(balance > 1.0f) balance = 1.0f;
	dayparam[0] = dayparam[1] = dayparam[2] = 1.0f - balance;
	dayparam[3] = CWeather__WetRoads;
	nightparam[0] = nightparam[1] = nightparam[2] = balance;
	nightparam[3] = 1.0f - CWeather__WetRoads;

	int noExtraColors = atomic->pipeline->pluginData == RSPIPE_PC_CustomBuilding_PipeID;

	// If no extra colors, force the one we have (night, unintuitively).
	// Night colors are guaranteed to be present by the instance callback.
	if(noExtraColors){
		dayparam[0] = dayparam[1] = dayparam[2] = dayparam[3] = 0.0f;
		nightparam[0] = nightparam[1] = nightparam[2] = nightparam[3] = 1.0f;
	}

	RwD3D9SetVertexShaderConstant(REG_dayparam, dayparam, 1);
	RwD3D9SetVertexShaderConstant(REG_nightparam, nightparam, 1);
}

void
setWindParams(RpAtomic *atomic, RwFrame *frame)
{
	CVector2D globalWindPos;

	if(!frame) return;
	RwV3d objPos = RwFrameGetLTM(frame)->pos;

	globalWindPos.x = windPos.x + objPos.x;
	globalWindPos.y = windPos.y + objPos.y;
	float windIntensity = 2.0f + (1.5f * CWeather__Wind);

	RwD3D9SetVertexShaderConstant(REG_windPos, &globalWindPos, 1);
	RwD3D9SetVertexShaderConstant(REG_windIntensity, &windIntensity, 1);
	// W8-2: VS c23 windExt = {heightScale, gustPhase, gustAmp, enable} — the
	// NEW constant this lane owns (treeWind.cpp TreeWind_UploadWindExt).
	// Uploaded here so EVERY wind-VS bind below (ps2 :768, xbox :967/:1288,
	// PBR fallback :1288) is preceded by a fresh c23; buildingPBRVS /
	// GTAIVBuilding_vs do not read c23, so the PBR/IV callers of this
	// function upload it harmlessly. VS c22/c23 are a DIFFERENT constant
	// space from PS c22/c23 (pipeUploadPBR) — no overlap.
	TreeWind_UploadWindExt();
	// Instrumentation only: VS c21/c22 (and c23) just reached the GPU.
	// (logged via the [BPIPE] throttled line in the callbacks — no scope
	// object here: this function is called from SEH-bearing callbacks.)
}

WRAPPER CBaseModelInfo *CVisibilityPlugins__GetModelInfo(RpAtomic*) { EAXJMP(0x732260); }

void
TagRenderCB(RpAtomic *atomic, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData)
{
	int alpha, alpharef;
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
	if(config->buildingPipe == BUILDING_PS2){
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
	}

	alpha = CVisibilityPlugins::GetUserValue(atomic);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)(255 - (int)(240.0f*alpha/255.0f)));
	D3D9Render(resEntryHeader, instanceData);

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)alpharef);
}

// =============================================================================
// Shared building render helpers
// =============================================================================

void
buildingPipe_setupResEntry(RwResEntry *repEntry, RxD3D9ResEntryHeader **outHeader, RxD3D9InstanceData **outData)
{
	*outHeader = (RxD3D9ResEntryHeader*)(repEntry + 1);
	*outData = (RxD3D9InstanceData*)(*outHeader + 1);
	if((*outHeader)->indexBuffer)
		RwD3D9SetIndices((*outHeader)->indexBuffer);
	_rwD3D9SetStreams((*outHeader)->vertexStream, (*outHeader)->useOffsets);
	RwD3D9SetVertexDeclaration((*outHeader)->vertexDeclaration);
}

void
buildingPipe_setUVTransform(RpMaterial *material, RwMatrix *ident)
{
	RwMatrix *m1, *m2;
	int effect = RpMatFXMaterialGetEffects(material);
	if(effect == rpMATFXEFFECTUVTRANSFORM){
		RpMatFXMaterialGetUVTransformMatrices(material, &m1, &m2);
		if(m1)
			RwD3D9SetVertexShaderConstant(REG_texmat, m1, 4);
		else
			RwD3D9SetVertexShaderConstant(REG_texmat, ident, 4);
	}else
		RwD3D9SetVertexShaderConstant(REG_texmat, ident, 4);
}

void
buildingPipe_cleanup()
{
	// Handoff bind: gpWhiteTexture, NOT NULL. An RwD3D9SetTexture(NULL, n) is
	// an applied[]-gated cache write in RW 3.6 and is not a guaranteed device
	// unbind, so the last mesh's raster can survive the callback on stage 1
	// and — on PS2/Xbox, which never unbound the detail lane — stage 2. The
	// 1x1 opaque-white is the pipe's neutral sample (pipeSetTexture), so a
	// consumer that samples a stage it did not rebind gets a defined value
	// instead of the building/vegetation raster, and the stage ops are
	// disabled below so fixed-function never reads it.
	pipeSetTexture(NULL, 1);	// gpWhiteTexture fallback (env lane)
	pipeSetTexture(NULL, 2);	// gpWhiteTexture fallback (detail lane)
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

RwV3d
buildingPipe_getEyePos()
{
	RwV3d eye = {0, 0, 0};
	RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera);
	if(cam){
		RwFrame *camFrame = RwCameraGetFrame(cam);
		if(camFrame){
			RwMatrix *camLTM = RwFrameGetLTM(camFrame);
			if(camLTM) eye = camLTM->pos;
		}
	}
	return eye;
}

struct BuildingRenderState {
	int alphafunc, alpharef;
	int src, dst;
	int fog;
	int zwrite;
	// TEXTUREADDRESS leak plug (audit item 2, building twin): the env/FX
	// branches push rwTEXTUREADDRESSWRAP through the rw cache only and
	// nothing ever restores it — the WRAP survived past the callback into
	// every later pass. L1 = rw cache; there is NO D3DRS counterpart for
	// addressing (L2 not applicable), so the device layer is the per-stage
	// sampler addressing (L3), snapshotted here and restored on exit.
	int textureaddress;
	DWORD sampU[3], sampV[3];
};

void
buildingPipe_saveRenderState(BuildingRenderState *state)
{
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &state->alpharef);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &state->alphafunc);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &state->src);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &state->dst);
	RwRenderStateGet(rwRENDERSTATEFOGCOLOR, &state->fog);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &state->zwrite);
	RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &state->textureaddress);
	// Pre-seed: a failed Get must not make the restore write garbage.
	state->sampU[0] = state->sampU[1] = state->sampU[2] = D3DTADDRESS_WRAP;
	state->sampV[0] = state->sampV[1] = state->sampV[2] = D3DTADDRESS_WRAP;
	if(d3d9device){
		for(int i = 0; i < 3; i++){
			d3d9device->GetSamplerState(i, D3DSAMP_ADDRESSU, &state->sampU[i]);
			d3d9device->GetSamplerState(i, D3DSAMP_ADDRESSV, &state->sampV[i]);
		}
	}
}

void
buildingPipe_restoreRenderState(const BuildingRenderState *state)
{
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)state->alpharef);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state->alphafunc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)state->src);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)state->dst);
	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)state->fog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)state->zwrite);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)state->textureaddress);
	if(d3d9device){
		for(int i = 0; i < 3; i++){
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSU, state->sampU[i]);
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSV, state->sampV[i]);
		}
	}
}

// =============================================================================
// Three-layer alpha/blend repair — the alpha/blend half of pipeForceCullMode
// (pipelinecommon.cpp:883 documents the layer model):
//   (1) the rw cache behind RwRenderStateSet/Get,
//   (2) the D3D9 driver cache behind RwD3D9SetRenderState (pending[]+dirty
//       list, flushed by _rwD3D9RenderStateFlushCache before every draw and
//       ONLY where pending[] differs from applied[]),
//   (3) the raw IDirect3DDevice9 state.
//
// A state only ever moves layer (3) when its layer-(2) value CHANGES (the
// flush is applied[]-gated, so it can never repair a device that drifted
// behind an unchanged cache). pipeEnterAlphaMode canonicalises (1) and (2)
// at pipe entry but reaches (2) only through RwD3D9SetRenderState — when the
// cache already holds the canonical value (TRUE / ref / SRCALPHA / ADD) the
// set is a no-op and the stale device value survives the whole atomic.
// Raw (3) writers that create that divergence: postfx Save/RestoreRawGeomStates
// raw-restores D3DRS_ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP from a
// snapshot that can already disagree with the caches, DepthHook writes raw
// ZENABLE, and the PBR cb's RenderIBLBuffer does raw D3DRS writes of its own.
//
// Observed evidence — skygfx_dbg.log throttled "[PipeAlpha] entry resync"
// lines at building entry (146 samples, one run):
//   dev src=5 dst=6 vtx=0 op=1 test=0 rw ref=0/1 fn=5   25 samples — device
//       alpha TEST off while a live alpha mesh is about to draw
//   dev ... op=4294967295                                1 sample — garbage
//       BLENDOP
//   rw ref=0                                             114 samples — the
//       degenerate GREATEREQUAL@0 "accept everything" ref pipeEnterAlphaMode
//       has to repair cache-side
// An alpha-cutout road decal mesh is drawn with VERTEXALPHAENABLE=FALSE
// (material alpha 255, no vertex alpha) and relies ONLY on the alpha test to
// discard its transparent texels. With the device test off (or ref stuck at
// 0 under GREATEREQUAL) every texel is accepted and, blending being off by
// design, the cutout region's RGB=0 lands unmodulated: a solid black pool
// that follows the decal texture's splatter shape — the road-decal artifact,
// in EVERY building pipe (PS2/GTAIV/Xbox/PBR all route through the Switch).
//
// Fix: re-push (1) into (2), then raw-force (2) -> (3). Same deterministic-
// entry contract as pipeForceCullMode / pipeEnterAlphaMode; unconditional
// device write, so a no-op at either cache still repairs layer (3).
//
// Two gaps this revision closes (both reachable from the standalone
// CSkidmarks__Render call site, which has no pipeEnterAlphaMode in front):
//   * ref==0 was passed through untouched — GREATEREQUAL@0 accepts every
//     texel, so the helper raw-pushed the very degenerate it exists to kill;
//   * D3DRS_SEPARATEALPHABLENDENABLE (+SRCBLENDALPHA/DESTBLENDALPHA/
//     BLENDOPALPHA) and D3DRS_COLORWRITEENABLE have no rw render state at
//     all, so nothing else in the codebase could ever repair them at layer
//     (3): a leaked separate-alpha mask rewrites DESTINATION ALPHA for every
//     later draw, a leaked colour-write mask drops RGB entirely and leaves
//     the frame's cleared (black) content — the "huge black floor".
//     (STENCILENABLE and D3DRS_TEXTUREFACTOR are deliberately NOT forced
//     here: stencil is owned by the game's own shadow passes and TFACTOR is
//     not part of the alpha block. D3DTSS_* stage ops are also left alone —
//     they are ignored whenever a pixel shader is bound, which every building
//     cb does, and stage 0 is re-authored per material by RW's
//     D3D9SetRenderMaterialProperties; stages 1-3 are already DISABLEd by
//     buildingPipe_cleanup on every cb exit.)
// =============================================================================
// Overload taking the caller's pipeEnterAlphaMode snapshot (see the lag note
// in the readback section below). Call sites without a snapshot in front
// (CSkidmarks__Render, vehicle/effects pipes) keep using the void form.
static void pipeForceAlphaBlock(const PipeAlphaState *preEnter);

void
pipeForceAlphaBlock(void)
{
	pipeForceAlphaBlock((const PipeAlphaState*)NULL);
}

static void
pipeForceAlphaBlock(const PipeAlphaState *preEnter)
{
	// ---- (1) rw cache is the authority for the states that have one ----
	// Pre-seeded: a failed RwRenderStateGet leaves the out-param untouched.
	int fnRaw  = rwALPHATESTFUNCTIONGREATEREQUAL;
	int refRaw = 1;
	int srcRaw = rwBLENDSRCALPHA;
	int dstRaw = rwBLENDINVSRCALPHA;
	int vtxRaw = 0;
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &fnRaw);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &refRaw);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &srcRaw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dstRaw);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &vtxRaw);

	// rw NA(0) / out-of-range never reaches the device (see
	// pipelinecommon pipeAlphaFuncToD3D / pipeBlendToD3D) — and pushing a
	// validated value into layer (2) while layer (1) keeps the raw one would
	// re-create the very cache/cache divergence this helper exists to kill,
	// so a repaired value is written back rw-side too.
	int fn  = (fnRaw  < rwALPHATESTFUNCTIONNEVER || fnRaw > rwALPHATESTFUNCTIONALWAYS)
	          ? rwALPHATESTFUNCTIONGREATEREQUAL : fnRaw;
	int ref = refRaw < 0 ? 0 : (refRaw > 255 ? 255 : refRaw);
	// ref==0 under GREATEREQUAL (and every other non-NEVER func) ACCEPTS EVERY
	// TEXEL — the exact degenerate pipeEnterAlphaMode repairs with its
	// effRef bump. This helper must repair it too: its standalone callers
	// (CSkidmarks__Render in main.cpp) go through it with NO
	// pipeEnterAlphaMode in front, so a rw-cached ref of 0 used to be
	// raw-pushed straight to the device as GREATEREQUAL@0. The rw side is
	// written back as well (line below) so layers (1)/(2)/(3) all agree;
	// pipeExitAlphaMode still restores the caller's own saved ref afterwards.
	if(ref == 0 && fn != rwALPHATESTFUNCTIONNEVER)
		ref = 1;
	int src = (int)pipeBlendToD3D((RwUInt32)srcRaw, rwBLENDSRCALPHA);
	int dst = (int)pipeBlendToD3D((RwUInt32)dstRaw, rwBLENDINVSRCALPHA);
	int vtx = vtxRaw ? 1 : 0;

	if(fn  != fnRaw)  RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)fn);
	if(ref != refRaw) RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	if(src != srcRaw) RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
	if(dst != dstRaw) RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
	if(vtx != vtxRaw) RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)vtx);

	// ---- (1) -> (2): RwD3D9SetRenderState writes the driver cache even when
	// an rw-only set would no-op against an unchanged rw cache ----
	// D3DRS_* are the D3D domain, `fn` is the rw domain — convert, never
	// pass the rw value straight down (see pipeAlphaFuncToD3D).
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)fn));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, (RwUInt32)src);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, (RwUInt32)dst);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, vtx ? TRUE : FALSE);
	// These two have NO rw render state — only the driver cache can carry
	// them, so they must be forced the way pipeEnterAlphaMode forces them
	// (alpha test ON, blend op ADD) or nothing else ever repairs them.
	RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	// Raw-only members of the same block (they have no rw render state either):
	//   SEPARATEALPHABLENDENABLE + its three alpha factors — setMoonAlphaBlendStates
	//     (main.cpp) pushes separate-alpha ON with DESTBLENDALPHA=ZERO and its
	//     restore only clears the enable bit through the driver cache; a device
	//     stuck ON recomposites DESTINATION ALPHA for every draw afterwards and
	//     the postfx chain reads that channel back.
	//   COLORWRITEENABLE — a leaked channel mask drops RGB writes entirely, so
	//     the frame keeps whatever the clear left (a black floor).
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);

	// ---- (2) -> (3): read the driver cache back (exactly the value the
	// flush would push) and raw-write it. The flush is applied[]-gated, so
	// this unconditional write is the only thing that repairs layer (3). ----
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;

	// Doctrine step (2), write direction — the same ordering contract
	// pipeExitAlphaMode performs at pipelinecommon.cpp:1529: commit every
	// pending[] dirty state to the device BEFORE the raw layer-(3) writes
	// below, so no later pre-draw flush can push a stale pending[] value
	// over a raw write. RW 3.6's cache-hit skip makes this mandatory: a raw
	// L3 write leaves the device holding a value stateCache[] may already
	// match, and a later RwD3D9SetRenderState of that cached value marks
	// nothing dirty -> flush no-op -> the device keeps the raw value. With
	// nothing left pending, the only values a later flush can push are ones
	// written through layer (2) AFTER this point — and the L2 block above
	// already re-synced layer (2) to exactly the values this loop pushes
	// (want is read back from that cache), so L2 stays consistent.
	_rwD3D9RenderStateFlushCache();

	static const D3DRENDERSTATETYPE ids[12] = {
		D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
		D3DRS_ALPHABLENDENABLE,
		D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA,
		D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA,
		D3DRS_COLORWRITEENABLE,
	};
	static const RwUInt32 fallback[12] = {
		TRUE, 1, (RwUInt32)D3DCMP_GREATEREQUAL, (RwUInt32)D3DBLEND_SRCALPHA,
		(RwUInt32)D3DBLEND_INVSRCALPHA, (RwUInt32)D3DBLENDOP_ADD, FALSE,
		FALSE, (RwUInt32)D3DBLEND_SRCALPHA, (RwUInt32)D3DBLEND_INVSRCALPHA,
		(RwUInt32)D3DBLENDOP_ADD, D3DCOLORWRITEENABLE_ALL,
	};

	// Device readback only for the throttled evidence trail (a raw
	// GetRenderState per atomic would cost more than the repair itself).
	// The raw writes stay unconditional (belt-and-braces contract above);
	// only the LOG is lag-aware: pipeEnterAlphaMode canonicalised the driver
	// cache moments ago, so until the next flush the device legitimately
	// still holds the PRE-entry cache value — that lag is not drift (the
	// flush delivers the canonical value before the draw; the raw write
	// below only duplicates it). Logging it fired a "[BUILDING] drift
	// repaired" line every throttle window and buried the real raw-writer
	// signal. When preEnter is available, a device value equal to the
	// pre-entry baseline is lag (suppressed from the mask); only a value
	// matching NEITHER the pushed value NOR the baseline is genuine
	// layer-(3) drift (postfx/Im2D raw writes) and earns the log.
	// preEnter == NULL call sites keep the original any-diff behaviour
	// (lag == want degenerates the condition to the old have != want).
	unsigned drifted = 0;
	bool check = dbglog_throttle("bld_alpha");
	// Both alpha-func DOMAINS in the same line so one live session proves the
	// conversion end-to-end: `fn` is the rw-cache value (RW domain),
	// `wantFn` is what pipeAlphaFuncToD3D maps it to and what the loop below
	// pushes, `devFn` is what the device was actually comparing against
	// BEFORE the repair (D3D domain, read back at loop index 2 — no other
	// device write in the loop touches ALPHAFUNC). Pre-seeded to wantFn so a
	// failed Get can never fabricate a mismatch.
	RwUInt32 wantFn = pipeAlphaFuncToD3D((RwUInt32)fn);
	RwUInt32 devFn = wantFn;
	for(int i = 0; i < 12; i++){
		RwUInt32 want = fallback[i];
		RwD3D9GetRenderState(ids[i], &want);
		if(check){
			DWORD have = (DWORD)want;
			if(SUCCEEDED(dev->GetRenderState(ids[i], &have))){
				if(i == 2)
					devFn = (RwUInt32)have;
				if((RwUInt32)have == want)
					continue;	// device already holds it — redundant write skipped
				// What the device reads when it is merely lagging the entry
				// canonicalisation: the pre-entry cache value, converted the
				// same way pipeExitAlphaMode pushed it (pipeBlendToD3D's
				// fallback pair; the rw/D3D blend enums are 1:1). ALPHAREF
				// mirrors pipeEnterAlphaMode's effRef bump: the entry
				// canonicalisation pushed ref 0->1, so a device still holding
				// the pre-entry ref reads 1 after the bump, not 0 — comparing
				// against the raw saved ref would flag that legitimate lag as
				// drift. States whose cache entry never moves (test/op —
				// TRUE/ADD across the whole enter/exit cycle) and the raw-only
				// members have no lag baseline: any diff there is real drift
				// (lag == want -> condition degenerates).
				RwUInt32 lag = want;
				if(preEnter){
					switch(ids[i]){
					case D3DRS_ALPHAREF:
						// mirror effRef (pipelinecommon.cpp:1397): 0 -> 1
						lag = preEnter->alphaRef ? preEnter->alphaRef : 1u; break;
					case D3DRS_ALPHAFUNC:
						lag = pipeAlphaFuncToD3D(preEnter->alphaFunc); break;
					case D3DRS_SRCBLEND:
						lag = pipeBlendToD3D(preEnter->srcBlend,
						                      D3DBLEND_SRCALPHA); break;
					case D3DRS_DESTBLEND:
						lag = pipeBlendToD3D(preEnter->dstBlend,
						                      D3DBLEND_INVSRCALPHA); break;
					case D3DRS_ALPHABLENDENABLE:
						lag = preEnter->vtxAlpha ? TRUE : FALSE; break;
					default: break;	// test/op/sepA/srcA/dstA/opA/cwr
					}
				}
				if((RwUInt32)have != lag)
					drifted |= 1u << i;
			}
			// failed Get: fall through to the unconditional write
		}
		dev->SetRenderState(ids[i], want);
	}
	// devFn != wantFn is exactly mask bit 2 (the driver cache holds wantFn —
	// entry pushed it — so a device diff there is the same condition), so a
	// separate condition term would bypass the lag suppression above.
	if(drifted)
		dbglog("[BUILDING] alpha/blend layer-3 drift repaired (mask=0x%03X: "
		       "test|ref|func|src|dst|op|blend|sepA|srcA|dstA|opA|cwr) "
		       "rw fn=%d -> d3d want=%u dev d3d=%u (rw ref=%d)",
		       drifted, fn, wantFn, devFn, ref);
}

// ============================================================
// Instrumentation only (dbg-pipes-logging lane) — NO behaviour change.
//
// Every field below is written NEXT TO an existing bind/upload so a 2s
// throttled dbglog can report what each building cb actually did this
// frame: which VS/PS it bound, whether the wind tilt ran, whether the
// wind VS constants were uploaded, the tree-alpha state and the
// forward+ cluster-light upload. No render path ever reads this data;
// the maths, render state and draw calls are untouched.
// ============================================================
struct BuildingPipeTrace {
	const char *vs;	// last VS tag bound by this cb ("-" = none)
	const char *ps;	// last PS tag bound by this cb ("-" = none)
	int  tilt;	// TreeWind_ApplyToTransform ran (CPU-side object tilt)
	int  wind;	// setWindParams ran -> VS c21/c22 (+c23 windExt) uploaded
	int  fp;	// ForwardPlus_SetConstants called (cluster lights)
};

enum { BTR_PS2 = 0, BTR_XBOX = 1, BTR_PBR = 2, BTR_SPHERE = 3, BTR_COUNT = 4 };

static BuildingPipeTrace bTrace[BTR_COUNT] = {
	{ "-", "-", 0, 0, 0 }, { "-", "-", 0, 0, 0 },
	{ "-", "-", 0, 0, 0 }, { "-", "-", 0, 0, 0 },
};
// cb currently executing — lets the shared helpers (setWindParams,
// buildingPipe_bindSimplePS) tag the right slot. Log bookkeeping only.
static BuildingPipeTrace *bTraceCur = &bTrace[BTR_PS2];

static inline void
bTraceBegin(BuildingPipeTrace &t)
{
	t.vs = "-"; t.ps = "-"; t.tilt = 0; t.wind = 0; t.fp = 0;
}

// pointer -> tag for the report (IDR_* names from resources/resource.h)
static const char *
bpipePSTag(void *sh)
{
	if(sh == NULL) return "-";
	if(sh == buildingPBRPS) return "IDR_BUILDINGPBRPS";
	if(sh == treeAlphaPS) return "treeAlphaPS";
	if(sh == simpleDetailNoiseDetailPS) return "IDR_SIMPLEDETAILNOISEDETAILPS";
	if(sh == simpleDetailPS) return "IDR_SIMPLEDETAILPS";
	if(sh == xboxBuildingNoiseDetailPS) return "IDR_XBOXBUILDINGNOISEDETAILPS";
	if(sh == xboxBuildingPS) return "IDR_XBOXBUILDINGPS";
	if(sh == simpleNoiseDetailPS) return "IDR_SIMPLENOISEDETAILPS";
	if(sh == simplePS) return "simplePS";
	if(sh == simpleFogPS) return "simpleFogPS";
	if(sh == gtaivBuildingPS) return "IDR_GTAIVBUILDINGPS";
	if(sh == gtaivFPPS) return "IDR_GTAIVFPPS";
	if(sh == ps2EnvSpecFxPS) return "ps2EnvSpecFxPS";
	return "?";
}

static const char *
bpipeVSTag(void *sh)
{
	if(sh == NULL) return "-";
	if(sh == buildingPBRVS) return "IDR_BUILDINGPBRVS";
	if(sh == ps2BuildingVS) return "IDR_PS2BUILDINGVS";
	if(sh == ps2BuildingFxVS) return "IDR_PS2BUILDINGFXVS";
	if(sh == ps2BuildingWindVS) return "IDR_PS2BUILDINGWINDVS";
	if(sh == xboxBuildingVS) return "IDR_XBOXBUILDINGVS";
	if(sh == xboxBuildingWindVS) return "IDR_XBOXBUILDINGWINDVS";
	if(sh == sphereBuildingVS) return "IDR_SPHEREBUILDINGVS";
	if(sh == gtaivBuildingVS) return "IDR_GTAIVBUILDINGVS";
	if(sh == gtaivFPVS) return "IDR_GTAIVFPVS";
	return "?";
}

// record the bind point for the profiler timeline + the throttled report
static inline void
bpipeBindPS(BuildingPipeTrace &t, void *sh)
{
	const char *tag = bpipePSTag(sh);
	t.ps = tag;
	PfBindPS(sh, tag);
}

static inline void
bpipeBindVS(BuildingPipeTrace &t, void *sh)
{
	const char *tag = bpipeVSTag(sh);
	t.vs = tag;
	PfBindVS(sh, tag);
}

static const char *
buildingPipeName(int p)
{
	switch(p){
	case BUILDING_PS2:   return "PS2";
	case BUILDING_XBOX:  return "XBOX";
	case BUILDING_GTAIV: return "GTAIV";
	case BUILDING_PBR:   return "PBR";
	}
	return "?";
}

// Log-only mirror of treeWind.cpp TreeWind_UploadWindExt (VS c23 windExt =
// {heightScale, gustPhase, gustAmp, enable}). Read-only reporting: the real
// upload stays owned by TreeWind_UploadWindExt; nothing here writes any
// shader constant.
static const float *
bpipeWindExtLog(void)
{
	static float ext[4];
	ext[0] = 0.12f;                            // heightScale
	ext[1] = (windPos.x + windPos.y) * 0.025f; // gustPhase
	ext[2] = 0.35f * CWeather__Wind;           // gustAmp
	ext[3] = 1.0f;                             // enable
	return ext;
}

// Throttled [BPIPE] report — one short line per pipe, at most one per 2s.
static void
bpipeTraceLog(const char *cb, int idx, const char *tag, RpAtomic *atomic)
{
	if(!dbglog_throttle(tag))
		return;
	if(config == NULL)	// instrumentation must never be the first NULL deref
		return;
	BuildingPipeTrace &t = bTrace[idx];
	const float *ext = bpipeWindExtLog();
	dbglog("[BPIPE] cb=%s pipe=%s(%d) ivMode=%d fp=%d VS=%s PS=%s "
	       "wind(tilt=%d c21c22=%d ext=%.3f/%.3f/%.3f/%d) "
	       "treeAlpha(act=%d en=%d prof=%d mul=%.3f ps=%p) "
	       "handles pbr=%p/%p iv=%p/%p fp=%p/%p",
		cb, buildingPipeName(config->buildingPipe), config->buildingPipe,
		config->ivMode, t.fp, t.vs, t.ps, t.tilt, t.wind,
		ext[0], ext[1], ext[2], (int)(ext[3] != 0.0f),
		TreeAlpha_Active(atomic) ? 1 : 0, TreeAlpha_enable ? 1 : 0,
		TreeAlpha_profile, TreeAlpha_alphamul, treeAlphaPS,
		buildingPBRVS, buildingPBRPS, gtaivBuildingVS, gtaivBuildingPS,
		gtaivFPVS, gtaivFPPS);
}

// ============================================================
// Stale-blend leak probe + exit-contract hardening (corona hunt).
//
// Symptom: corona quads draw as opaque black/red squares. Diagnosis: some
// mod pass leaves the RAW device with ALPHABLENDENABLE=FALSE and stale
// ONE/ONE factors; vanilla CCoronas::Render re-sets blend per-quad through
// the RW cache, and the cache-hit skip against a desynced cache leaves the
// device blend-off. The leaker is still unknown — this probe runs at every
// building-cb exit to CATCH the device holding the "broken signature"
// (blend OFF + SRC=ONE + DST=ONE) and name which pipe fired.
//
// Contract (near-free): the 3 raw GetRenderState detection reads run at
// most once per ~2s through a static tick gate, NOT once per cb exit (they
// are raw device reads = GPU pipeline sync stalls, and this probe fires on
// every building-cb exit). The LOG is gated on the shared "blend_leak" 2s
// throttle (one line per 2s max,
// tagged with the cb + the pipe that fired), but the REPAIR is
// unconditional on detection — no building-cb exit may leave
// blend-off+ONE/ONE behind (the corona-facing exit contract).
//
// Repair = canonical normal-alpha (blend ON, SRCALPHA/INVSRCALPHA) through
// ALL THREE layers, same pattern as the cb-exit restores (:1235-1248):
//   (1) RwRenderStateSet        — resync the rw cache,
//   (2) RwD3D9SetRenderState    — retarget the driver cache pending[] so no
//       later flush can push the stale ONE/ONE factors back over the repair
//       (a layer-(1) set alone can cache-hit-skip past a desynced cache —
//       the exact bug class this hunts),
//   (3) raw d3d9device->SetRenderState — the only layer a cache-hit skip
//       cannot no-op. Runs on the BROKEN path only — correct paths pay
//       at most one sample of 3 reads per ~2s and nothing else.
// ============================================================
static void
bpipeBlendLeakProbe(const char *cb)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	// Perf gate: the 3 GetRenderState reads below are raw device reads (each
	// one a GPU pipeline sync stall) and this probe fires on EVERY building-
	// cb exit (hundreds of calls per frame). Sample at most once per ~2s
	// instead: static tick gate, wrap-safe DWORD arithmetic (same pattern as
	// diagnostics.cpp GetTickCount use). REPAIR semantics unchanged — when a
	// sample DOES catch the broken signature, the repair below runs
	// immediately on that exit, exactly as before.
	static DWORD lastSampleTick = 0;
	DWORD now = GetTickCount();
	if(now - lastSampleTick < 2000)
		return;
	lastSampleTick = now;
	// Pre-seed to the canonical (non-signature) values: a failed Get must
	// never fabricate a hit. POD locals only (SEH choke-point discipline).
	DWORD abe = TRUE, src = D3DBLEND_SRCALPHA, dst = D3DBLEND_INVSRCALPHA;
	if(FAILED(dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &abe)) ||
	   FAILED(dev->GetRenderState(D3DRS_SRCBLEND, &src)) ||
	   FAILED(dev->GetRenderState(D3DRS_DESTBLEND, &dst)))
		return;
	// The broken signature: blend OFF with stale ONE/ONE factors.
	if(abe != FALSE || src != D3DBLEND_ONE || dst != D3DBLEND_ONE)
		return;

	// Exit-contract repair — canonical normal-alpha, all three layers.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, (RwUInt32)D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, (RwUInt32)D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	// Detect + repair share the one "blend_leak" throttle: one line per 2s
	// max across all exit probes, tagged with which pipe fired.
	if(dbglog_throttle("blend_leak"))
		dbglog("[BUILDING] blend_leak: cb=%s pipe=%s exit left ALPHABLENDENABLE=0 "
		       "SRC=ONE DST=ONE (broken signature) — repaired to blend-on "
		       "SRCALPHA/INVSRCALPHA",
		       cb, config ? buildingPipeName(config->buildingPipe) : "?");
}

void*
buildingPipe_selectPS(TexInfo *texinfo, bool hasEnvMap)
{
	// AAP-PARITY sweep: every family member now carries the same missing-CSO
	// fallback the noise-detail branch already had (:957-961 policy — "a
	// missing CSO must never render nothing"). A NULL RwD3D9SetPixelShader
	// bind drops the mesh onto fixed-function with building-VS constants
	// uploaded — the exact "floating white squares" failure class the IV-swap
	// guard in the PS2 cb documents. When the CSOs load (the normal case)
	// every return value is unchanged — binding-only hardening.
	if(hasEnvMap){
		if(texinfo->noiseDetail && config->noiseDetail)
			return xboxBuildingNoiseDetailPS ? xboxBuildingNoiseDetailPS : simplePS;
		else
			return xboxBuildingPS ? xboxBuildingPS : simplePS;
	}
	if(config->detailMaps && texinfo->detail){
		float tile = texinfo->detailtile/10.0f;
		RwD3D9SetPixelShaderConstant(1, &tile, 1);
		pipeSetTexture(texinfo->detail, 2);
		if(texinfo->noiseDetail && config->noiseDetail)
			return simpleDetailNoiseDetailPS ? simpleDetailNoiseDetailPS : simplePS;
		else
			return simpleDetailPS ? simpleDetailPS : simplePS;
	}
	// aap SkyGfx parity: noise-detail body material with NO detail map binds
	// the dedicated jitter PS (backup_original buildingPipe.cpp:310-315). A
	// missing CSO must never render nothing — fall back to simplePS.
	if(texinfo->noiseDetail && config->noiseDetail)
		return simpleNoiseDetailPS ? simpleNoiseDetailPS : simplePS;
	return simplePS;
}

// Noise-detail PS family (shaders/noiseDetailBuildingPS.hlsl — main_simpleNoiseDetail,
// main_simpleDetailNoiseDetail, main_xboxBuildingNoiseDetail) fetches the packed
// tile and the c49-c52 knob block from s9, so pipeBindNoise(9) must land BEFORE
// that PS is bound. pipeBindNoise re-uploads the whole block on every call, so
// the bind is latched per render callback (both cbs that can select one of
// these PSes clear the latch on entry): one upload per callback, never per mesh.
static bool s_noiseDetailBoundCB = false;

static void
buildingPipe_bindNoiseFor(void *ps)
{
	if(s_noiseDetailBoundCB || ps == NULL)
		return;
	if(ps != simpleNoiseDetailPS && ps != simpleDetailNoiseDetailPS &&
	   ps != xboxBuildingNoiseDetailPS)
		return;
	pipeBindNoise(9);	// skygfx.h — sampler s9 (+ c49-c52), idempotent
	s_noiseDetailBoundCB = true;
}

// =============================================================================
// W8-2 tree lane — simple-PS bind with the tree alpha/lighting overlay.
// =============================================================================
// Selects the normal simple PS first (detail/noise-detail/env bookkeeping in
// buildingPipe_selectPS still runs), then swaps in treeAlphaPS when — and
// ONLY when — TreeAlpha_Active says the overlay has work (CSO loaded + real
// tree + enabled + Console profile; see treeWind.h). PC/PC+ never reach the
// swap: they only move the FFP alpha ref (TreeAlpha_ApplyAlphaRef, applied
// once per cb below), so a tree with a detail map keeps its detail PS.
//
// Order matters: buildingPipe_selectPS may push the detail `tile` into PS c1
// for the detail branch — TreeAlpha_Upload runs AFTER it and takes c1 over
// for this mesh (c1 = treeAlpha while the tree PS is bound), exactly the
// ownership the treeWind.h register contract documents.
//
// TagRenderCB sentinel materials are excluded: TagRenderCB re-bases the
// FFP alpha ref to ITS OWN per-object tag value (255 - 240*alpha/255),
// which is a different threshold than the one TreeAlpha_Upload put in PS
// c1 — binding the overlay there would clip against the wrong ref.
static void
buildingPipe_bindSimplePS(RpAtomic *atomic, RpMaterial *material, TexInfo *texinfo, bool hasEnvMap)
{
	// Instrumentation only: same call, result kept in a local so the
	// throttled [BPIPE] report can name the PS this helper selected
	// (evaluation order and value are unchanged).
	void *selPS = buildingPipe_selectPS(texinfo, hasEnvMap);
	buildingPipe_bindNoiseFor(selPS);	// s9 live before the PS bind
	RwD3D9SetPixelShader(selPS);
	bpipeBindPS(*bTraceCur, selPS);
	if(selPS && selPS == simpleNoiseDetailPS){
		// One-shot proof the noise-detail lane is live (static latch +
		// throttle: first real selection only, never per-frame).
		static bool s_noiseDetailLogged = false;
		if(!s_noiseDetailLogged && dbglog_throttle("bld_noisedetail")){
			s_noiseDetailLogged = true;
			dbglog("[BPIPE] bld_noisedetail: noise-detail body PS bound tex=%s ps=%p (no detail map)",
			       (material && material->texture) ? material->texture->name : "(none)",
			       selPS);
		}
	}
	if(material && material->pipeline == (RxPipeline*)TagRenderCB)
		return;		// tag materials keep their own ref/PS scheme
	if(TreeAlpha_Active(atomic)){
		TreeAlpha_Upload();
		RwD3D9SetPixelShader(treeAlphaPS);
		bpipeBindPS(*bTraceCur, treeAlphaPS);
	}
}

// File-level frame counters (promoted from function-static so they can be zeroed on device reset)
static int ps2FrameCount = 0;
static int xboxFrameCount = 0;
static int pbrFrameCount = 0;

// Lazy one-shot load of the GTAIVForwardPlus pair into gtaivFPVS/gtaivFPPS.
// One-shot because makeVS/makePS log on every failed call; shared with
// vehiclePipe.cpp so both IV swaps see one cache. Returns true only when BOTH
// handles exist — the IV swaps must swap VS+PS together, never half-swap.
// (The one-shot latch is file-scope — was a function-static — so
// BuildingResetShaderLatches() can clear it on device Reset.)
static bool s_ivFPAttempted = false;

bool
ivFPReady(void)
{
	if(!s_ivFPAttempted){
		s_ivFPAttempted = true;
		makeVS(IDR_GTAIVFPVS, &gtaivFPVS);
		makePS(IDR_GTAIVFPPS, &gtaivFPPS);
		if(!(gtaivFPVS && gtaivFPPS))
			dbglog("[IV-FP] GTAIVForwardPlus not created (VS=%p PS=%p) — IV draws keep the plain pair",
			       gtaivFPVS, gtaivFPPS);
	}
	return gtaivFPVS && gtaivFPPS;
}

// Device-Reset shader-latch resetter (L11; called by depthhook.cpp after
// Pipeline_InvalidateShaders()/CreateShaders()). Clears ONLY the TU-local
// lazy caches: the ivFPReady() one-shot latch and the GTAIVForwardPlus pair
// it lazy-loads. CreateShaders does NOT recreate those two handles and its
// s_forceShaderRecreate window has already closed by the time the next lazy
// load runs, so a stale non-NULL handle would suppress recreation forever.
// The simplePS/detail/noise-detail family is intentionally untouched —
// CreateShaders re-creates those handles after every Reset. Fail-open: if the
// lazy load fails again, ivFPReady() returns false and both IV swaps keep the
// plain pair exactly as before.
void
BuildingResetShaderLatches(void)
{
	s_ivFPAttempted = false;
	gtaivFPVS = NULL;
	gtaivFPPS = NULL;
}

// =============================================================================
// Per-mesh write elision — building callbacks.
//
// Only writes issued BY THIS CODE are elided; no state is ever read back from a
// cache to decide. Two classes:
//
//  (b) invariant across the callback (VS c4 = buildingAmbient, refreshed once
//      per frame by CustomBuildingPipeline__Update): the tracker is an
//      "uploaded yet" bool + an exact-BITWISE (memcmp) copy of the last value
//      THIS callback pushed. The first mesh always uploads.
//
//  (a) per mesh but equal on consecutive meshes (L1 VERTEXALPHAENABLE + L2
//      ALPHABLENDENABLE pair, decal ZWRITE bursts, same RwTexture* on a
//      stage): the tracker starts at an impossible sentinel (-1 /
//      valid=false), so the first write after callback entry is never elided.
//
// Contract safety: every intervening writer of these states is balanced —
// D3D9RenderDual restores ZWRITE; PipeChain_ClassifyBegin/End restores ZWRITE
// + ALPHABLENDENABLE + VERTEXALPHAENABLE; the PS2 env branch restores
// VERTEXALPHAENABLE. Trackers are locals reset at every callback entry and the
// Switch enter/exit contract writes are untouched.
// =============================================================================
enum {
	BPELIDE_PS2 = 0,
	BPELIDE_XBOX,
	BPELIDE_PBR,
	BPELIDE_SPHERE,
	BPELIDE_SLOTS
};
static int s_bpipeElideSkip[BPELIDE_SLOTS] = { 0, 0, 0, 0 };

static void
bpipeElideSummary(void)
{
	// Cheap gate first: one static increment per callback; the (hash +
	// GetTickCount) throttle check runs at most every 64th callback.
	static int tick = 0;
	if((++tick & 63) != 0)
		return;
	if(!s_bpipeElideSkip[BPELIDE_PS2] && !s_bpipeElideSkip[BPELIDE_XBOX] &&
	   !s_bpipeElideSkip[BPELIDE_PBR] && !s_bpipeElideSkip[BPELIDE_SPHERE])
		return;	// nothing skipped since the last report — stay silent
	if(!dbglog_throttle("bpipe_elide"))
		return;
	dbglog("[BldElide] writes skipped since last window: PS2=%d Xbox=%d PBR=%d Sphere=%d",
	       s_bpipeElideSkip[BPELIDE_PS2], s_bpipeElideSkip[BPELIDE_XBOX],
	       s_bpipeElideSkip[BPELIDE_PBR], s_bpipeElideSkip[BPELIDE_SPHERE]);
	s_bpipeElideSkip[BPELIDE_PS2] = 0;
	s_bpipeElideSkip[BPELIDE_XBOX] = 0;
	s_bpipeElideSkip[BPELIDE_PBR] = 0;
	s_bpipeElideSkip[BPELIDE_SPHERE] = 0;
}

// Invariant-class tracker: returns true when the caller must upload (first
// call after callback entry, or the value differs). The comparison is exact
// bitwise — never epsilon — so a value the shader saw is never assumed
// unchanged when a float bit changed.
static inline bool
bpipeElideChanged4(bool &valid, float *last, const float *cur)
{
	if(!valid){
		memcpy(last, cur, 4 * sizeof(float));
		valid = true;
		return true;
	}
	if(memcmp(last, cur, 4 * sizeof(float)) != 0){
		memcpy(last, cur, 4 * sizeof(float));
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// bldBindDiffuse3L — THREE-LAYER stage-0 diffuse bind (the BINDING FIX,
// building twin of vehiclePipe.cpp vehBindDiffuse3L :794).
//
// Dominant test-4 symptom (black road/ground with hard-edged giant blotches):
// a building mesh's stage-0 diffuse bind went through pipeSetTexture ->
// RwD3D9SetTexture (L2 ONLY), which is change-gated against RW's texture
// cache. When postfx/water leaves a screen-family raster (front-buffer /
// render-target) on device stage 0, the device has drifted from that cache and
// the L2 set cache-hit NO-OPS — the mesh samples the leaked raster with its
// UVs -> black / hard-edged blotches.
//
// This helper writes the resolved texture through ALL THREE layers so no
// cache-hit no-op can hold the screen raster back:
//   L1  RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ...)  stage-0 rw cache;
//       PARK-AND-SET (park on a guaranteed-different raster first) so the real
//       set is always a transition, never a cache-hit no-op.
//   L2  RwD3D9SetTexture(resolved, stage)                 RW driver/applied[].
//   L3  d3d9device->SetTexture(stage, d3d)                RAW device — the only
//       layer a cache-hit skip can never hold back.
// NULL resolves to gpWhiteTexture (the pipe's neutral opaque sample), so an
// untextured mesh draws its material colour via MODULATE(white, DIFFUSE)
// instead of inheriting the previous pass's raster. A same-texture rebind is
// state-identical (L1/L2 cache no-op, L3 sets the identical pointer), so
// correctly-textured meshes are visually unchanged — binding-only.
// [bind-elision] stage-0 last-bound D3D texture pointer cache (POD statics),
// building twin of vehiclePipe.cpp s_vehLastBoundD3D0. bldBindDiffuse3L's L1
// park-and-set forces two real driver texture transitions per write; the
// cache elides the whole three-layer write when the device PROVABLY already
// holds the target: because L3 (d3d9device->SetTexture) is always written on
// the miss path below, a valid cache entry with a matching pointer proves the
// device holds that texture — skipping L1/L2 as well is then state-identical
// (zero driver calls). The cache is invalidated at every building cb entry
// (bldEntryContractTex0 -> bldBindElideInvalidate): postfx/water/other pipes
// write foreign rasters onto device s0 between callbacks, so a cache that
// survived a callback boundary could prove nothing. Only stage 0 (diffuse)
// is wired; stage 1+ keep the plain L2 bind.
static IDirect3DBaseTexture9 *s_bldLastBoundD3D0 = NULL;
static bool                   s_bldLastBoundD3D0Valid = false;

// [bind-elision] counters + one-shot receipt. skips >> writes is the healthy
// signature (same-texture rebinds dominate); writes are the entry-contract
// leak repairs and genuine texture changes.
static int  s_bldBindElideSkips  = 0;
static int  s_bldBindElideWrites = 0;
static int  s_bldElideMismatch   = 0;	// hits whose readback proved the cache stale
static bool s_bldBindElideActive = false;

static void
bldBindElideInvalidate(void)
{
	s_bldLastBoundD3D0 = NULL;
	s_bldLastBoundD3D0Valid = false;
}

// Throttled skip/write receipt (same cheap-gate idiom as bpipeElideSummary).
// dev_mismatch counts elide hits the g_diagLevel>=1 readback verify found
// stale (tags: bld_elide_mismatch at the site, bld_bindelide for this line).
static void
bldBindElideLog(void)
{
	static int tick = 0;
	if((++tick & 63) != 0)
		return;
	if(!s_bldBindElideSkips && !s_bldBindElideWrites && !s_bldElideMismatch)
		return;
	if(!dbglog_throttle("bld_bindelide"))
		return;
	dbglog("[bind-elision] bld hits=%d writes=%d dev_mismatch=%d",
	       s_bldBindElideSkips, s_bldBindElideWrites, s_bldElideMismatch);
	s_bldBindElideSkips = 0;
	s_bldBindElideWrites = 0;
	s_bldElideMismatch = 0;
}

static void
bldBindDiffuse3L(RwTexture *tex, int stage)
{
	RwTexture *resolved = tex ? tex : gpWhiteTexture;

	// [bind-elision] resolve the L3 target FIRST so the elision check can run
	// before ANY driver call. A NULL extraction leaves the device state
	// unprovable — the cache must never hit on it (invalidation (b) below).
	IDirect3DTexture9 *d3d = NULL;
	if(d3d9device)
		d3d = TexDefsGetD3DTexture(resolved);

	if(stage == 0){
		bldBindElideLog();
		if(!s_bldBindElideActive){
			s_bldBindElideActive = true;
			dbglog("[bind-elision] bld active");
		}
		// Hit: the device provably holds this exact texture (L3 was the last
		// stage-0 writer on the previous miss) — skip all three layers,
		// zero driver calls. Same-texture rebind stays a no-op, now a
		// zero-call no-op.
		//
		// [verify lever] The proof only holds while nothing else wrote device
		// s0 since the arm. Any raw stage-0 writer (postfx/water/text/sprite
		// flush or an in-callback raw bind) breaks it. With g_diagLevel>=1
		// read the device back on every hit; a mismatch is counted/logged
		// (tag bld_elide_mismatch), the cache is dropped and the FULL
		// three-layer write runs instead of the hit (self-heal). Lever off =
		// untouched zero-call fast path.
		if(d3d && s_bldLastBoundD3D0Valid &&
		   s_bldLastBoundD3D0 == (IDirect3DBaseTexture9*)d3d){
			bool mismatch = false;
			if(g_diagLevel >= 1 && d3d9device){
				IDirect3DBaseTexture9 *dev0 = NULL;
				if(SUCCEEDED(d3d9device->GetTexture(0, &dev0))){
					mismatch = dev0 != (IDirect3DBaseTexture9*)d3d;
					if(dev0)
						dev0->Release();	// GetTexture AddRefs
				}
			}
			if(!mismatch){
				s_bldBindElideSkips++;
				return;
			}
			s_bldElideMismatch++;
			if(dbglog_throttle("bld_elide_mismatch"))
				dbglog("[bind-elision] bld elide HIT mismatch: cached s0=%p but device holds a different texture — invalidating + full rebind",
				       (void*)d3d);
			bldBindElideInvalidate();
		}
	}

	// L1 rw cache — only stage 0 has a rw render-state twin (TEXTURERASTER);
	// stages 1+ are pure D3D9 texture stages with no rw twin to park.
	if(stage == 0 && resolved->raster){
		RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
			? gpWhiteTexture->raster : NULL;
		RwRaster *targetRas = resolved->raster;
		RwRaster *parkRas = (targetRas == whiteRas) ? NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)targetRas);
	}

	// L2 RW driver cache / texture apply (maps raster -> d3d internally).
	RwD3D9SetTexture(resolved, stage);

	// L3 raw device: bypass every cache. Only pin when the guarded extraction
	// (texdefs.h TexDefsGetD3DTexture) yields a real d3d pointer — never clear
	// a valid bind on extraction failure (L2 already bound the resolved
	// texture correctly).
	if(d3d9device){
		if(d3d){
			d3d9device->SetTexture(stage, d3d);
			if(stage == 0){
				// [bind-elision] miss path: arm the cache with the pointer
				// this write just put on the device.
				s_bldLastBoundD3D0 = (IDirect3DBaseTexture9*)d3d;
				s_bldLastBoundD3D0Valid = true;
				s_bldBindElideWrites++;
			}
		}else if(stage == 0){
			// [bind-elision] L3 extraction failure: the device state is
			// unprovable, so the cache must not survive (invalidation (b)).
			// L2 already bound the resolved texture correctly (as before).
			bldBindElideInvalidate();
			s_bldBindElideWrites++;
		}
	}
}

// Entry contract (building cbs): flush whatever the previous pass
// (postfx/water/IBL) left on device stage 0 by binding the neutral
// gpWhiteTexture through ALL THREE layers BEFORE the first mesh draws. Raw
// bypass of the per-mesh elision tracker on purpose — the tracker starts
// invalid at callback entry, so the first per-mesh bind still runs full.
static void
bldEntryContractTex0(void)
{
	// [bind-elision] entry-contract invalidation: foreign passes (postfx/
	// water/vehicle/ped pipes) may have written device s0 since the last
	// building cb, so the cache proves nothing — this white repair bind
	// always runs in full, and the first per-mesh bind of the callback
	// resolves against a freshly-armed (white) cache.
	bldBindElideInvalidate();
	bldBindDiffuse3L(gpWhiteTexture, 0);
}

// -----------------------------------------------------------------------------
// Material stage bind + resolved-pointer elision.
//
// Every per-mesh material texture bind in the four building callbacks goes
// through this helper:
//   * pipeSetTexture maps NULL -> gpWhiteTexture (skygfx.h), so an untextured
//     mesh binds a defined 1x1 opaque-white sample instead of leaving the
//     previous mesh's raster on the stage. A raw RwD3D9SetTexture(NULL, n) is
//     NOT a reliable unbind in RW 3.6 (applied[]-gated cache), which is the
//     texture-raster leak class the round-2 registry lists under R2-01.
//   * the elision key is the RESOLVED pointer (what is actually bound), not
//     material->texture. A NULL material and an explicit gpWhiteTexture
//     material share one tracker state; keying on the raw pointer would
//     rebind the same white texture and could desync the tracker from the
//     device (the vehicle pipe's vehEnvBindTexture uses the same resolved-key
//     contract).
// `last`/`valid` may be NULL for stages that are re-bound unconditionally.
static void
buildingPipe_bindTexStage(RwTexture *tex, int stage, RwTexture **last, bool *valid, int elideSlot)
{
	RwTexture *resolved = tex ? tex : gpWhiteTexture;
	if(last && valid && *valid && *last == resolved){
		s_bpipeElideSkip[elideSlot]++;
		return;
	}
	if(last)
		*last = resolved;
	if(valid)
		*valid = true;
	// Stage 0 (diffuse) goes through the THREE-LAYER bind — a plain
	// pipeSetTexture (L2 only) can cache-hit NO-OP while the device holds a
	// leaked screen-family raster (see bldBindDiffuse3L above). Stages 1+
	// (env / detail lanes) keep the plain L2 bind.
	if(stage == 0)
		bldBindDiffuse3L(resolved, stage);
	else
		pipeSetTexture(resolved, stage);
}

// Round-2 one-shot bind audit (R2-01/R2-07): the first 24 meshes of the
// session print the material texture name, whether the material had no
// texture, the texture actually bound (the gpWhiteTexture fallback), and the
// blend/zwrite decision this callback took for that mesh — so the next log
// names a texture-less/leaking mesh instead of guessing. dbglog_throttle
// keeps it to one line per 2 s window; the counter still stops at 24 so a long
// session cannot flood the log.
static int s_bldTexLogged = 0;

// L3 device stage-0 readback shared by the bind receipts (mirror of
// vehiclePipe.cpp vehTexDiagDev0). GetTexture AddRefs (caller Releases dev0).
// `want` is a BORROWED TexDefsGetD3DTexture pointer (never released). Returns
// true when the device disagrees with the intended bind — the exact
// divergence bldBindDiffuse3L exists to kill.
static bool
bldTexDiagDev0(RwTexture *bound, IDirect3DTexture9 **outWant, IDirect3DBaseTexture9 **outDev0)
{
	IDirect3DBaseTexture9 *dev0 = NULL;
	if(d3d9device)
		d3d9device->GetTexture(0, &dev0);
	IDirect3DTexture9 *want = bound ? TexDefsGetD3DTexture(bound) : NULL;
	if(outWant) *outWant = want;
	if(outDev0) *outDev0 = dev0;
	return dev0 != (IDirect3DBaseTexture9*)want;
}

// First-hit receipt — fires ONCE for the first mesh whose diffuse texture is
// NULL (mirror of vehiclePipe.cpp vehTexDiagFirstHit): the material with no
// diffuse map is the class whose bind used to fall through to a leaked screen
// raster. Prints the intended-vs-device stage-0 readback so the next log tells
// "the bind never reached the device" (DIVERGED) apart from "bind worked,
// material simply has no diffuse map".
static void
bldTexDiagFirstHit(RpMaterial *material, RwTexture *bound)
{
	if(!dbglog_once("bld_tex_first"))
		return;
	IDirect3DTexture9 *want = NULL;
	IDirect3DBaseTexture9 *dev0 = NULL;
	bool diverged = bldTexDiagDev0(bound, &want, &dev0);
	dbglog("[BldTex-FIRST] tex='%s' null=1 bound=%p want=%p dev0=%p %s",
	       (material && material->texture && material->texture->name[0])
	           ? material->texture->name : "(null)",
	       (void*)bound, (void*)want, (void*)dev0,
	       diverged ? "DIVERGED" : "ok");
	if(dev0)
		dev0->Release();	// GetTexture AddRefs
}

static void
buildingPipe_texDiag(RpMaterial *material, RwTexture *bound, int blend, int zw)
{
	if(material && !material->texture)
		bldTexDiagFirstHit(material, bound);	// NULL-diffuse first-hit
	if(s_bldTexLogged >= 24 || !dbglog_throttle("bld_tex"))
		return;
	s_bldTexLogged++;
	IDirect3DTexture9 *want = NULL;
	IDirect3DBaseTexture9 *dev0 = NULL;
	bool diverged = bldTexDiagDev0(bound, &want, &dev0);
	dbglog("[BldTex] tex='%s' null=%d bound=%p want=%p dev0=%p%s blend=%d zw=%d",
	       (material && material->texture && material->texture->name[0])
	           ? material->texture->name : "",
	       (material && material->texture) ? 0 : 1,
	       (void*)bound, (void*)want, (void*)dev0, diverged ? " DIVERGED" : "",
	       blend, zw);
	if(dev0)
		dev0->Release();	// GetTexture AddRefs
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	PF_SCOPE("BuildingPipe_PS2");	// instrumentation only
	bTraceBegin(bTrace[BTR_PS2]);	// instrumentation only
	bTraceCur = &bTrace[BTR_PS2];	// instrumentation only
	bpipeElideSummary();	// throttled elision report (previous window)
	s_noiseDetailBoundCB = false;	// one pipeBindNoise(9) per callback at most
	if(++ps2FrameCount % 300 == 1)
		dbglog("PS2Building: frame %d object=%p type=%d flags=0x%X", ps2FrameCount, object, type, flags);

	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32	numMeshes;
	CustomEnvMapPipeMaterialData *envData = NULL;
	RwBool hasAlpha;
	float colorScale;
	struct {
		float shininess;
		float lightmult;
	} fxParams;
	RwV4d envXform;
	RwMatrix envmat;
	RwV3d eye;
	float transform[16];
	RwMatrix ident;

	atomic = (RpAtomic*)object;
	RwMatrixSetIdentity(&ident);

	RwFrame* frame = (RwFrame*)atomic->object.object.parent;
	if(!frame) return;

	if (RWSRCGLOBAL(curCamera)) { _rwD3D9EnableClippingIfNeeded(object, type); }
	// NULL curCamera → 0x7FAD4D fault

	// ---- IV shader-swap decision (W8-2: hoisted) ----
	// This block used to live inside the per-mesh loop below; it only
	// depends on config + the lazily-created IV handles, so it is computed
	// once per cb now — the WVP upload (and therefore the wind tilt) happens
	// BEFORE the loop, and the tilt must know which VS branch the whole
	// atomic will take. Contract (unchanged):
	//   * wantIVBuilding = iv_mode preset (config->ivMode) OR the
	//     BUILDING_GTAIV pipe selection itself, matching the vehicle side
	//     (`ivMode || vehiclePipe == CAR_GTAIV`, vehiclePipe.cpp:690) —
	//     without the pipe term, BUILDING_GTAIV with ivMode=0 rendered PS2
	//     shaders under a "GTA IV" pipe selection.
	//   * useIVFP: forwardPlusEnable upgrades the IV pair to the
	//     GTAIVForwardPlus VS/PS pair (gtaivFPVS/gtaivFPPS) — VS and PS
	//     swap TOGETHER, never a half-swap; lazy ivFPReady() one-shot load.
	//   * haveIVPair: IV swap happens ONLY when both handles were created
	//     (commit e21b697 dropped the IDR lines — binding NULL would drop
	//     every building onto FFP → "floating white squares"; the log at
	//     the old site fires while they are missing).
	bool wantIVBuilding = config->ivMode || config->buildingPipe == BUILDING_GTAIV;
	bool useIVFP = wantIVBuilding && config->forwardPlusEnable && ivFPReady();
	bool haveIVPair = useIVFP || (gtaivBuildingVS && gtaivBuildingPS);

	pipeGetComposedTransformMatrix(atomic, transform);
	// W8-2 THE GAP: GTAIV/ivMode draws run the IV VS pair (gtaivBuildingVS /
	// gtaivFPVS), which contains NO wind math — an atomic flagged
	// DefinedVertexShader::WIND stopped swaying the moment the pipe switched
	// to GTAIV (or ivMode=1). TreeWind_ApplyToTransform is the CPU-side
	// object-space tilt for exactly the branches with no wind VS; it self-
	// guards on the WIND flag, so non-wind atomics pay one plugin read.
	// NOT applied on the wind-VS branches below (ps2BuildingWindVS already
	// displaces per-vertex — the two must never stack; ownership rule in
	// treeWind.h).
	if(wantIVBuilding && haveIVPair){
		TreeWind_ApplyToTransform(atomic, transform);
		bTrace[BTR_PS2].tilt = 1;	// instrumentation only: CPU tilt ran
	}
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);

	buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);

	setDnParams(atomic);

	CustomBuildingEnvMapPipeline__SetupEnv(atomic, NULL, &envmat);
	RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);

	//for gloss - with null checks
	eye = buildingPipe_getEyePos();
	RwD3D9SetVertexShaderConstant(34, &eye, 1);
	RwD3D9SetPixelShaderConstant(2, &eye, 1);

	pipeUploadLightColorPS(pDirect, REG_directCol);
	pipeUploadLightDirectionPS(pDirect, REG_directDir);

	// Forward+ clustered lights — LEAK CONTRACT (orc-1 finding #7): a
	// NON-consumer cb must not bind c45/s5-s7. This cb serves BOTH
	// BUILDING_PS2 and BUILDING_GTAIV (switch :2063 "GTAIV = PS2 callback +
	// IV shader swap"), and neither the PS2/GTAIV building pair nor the IV-FP
	// pair consumes the cluster state (consumer list at
	// buildingPipe_consumesForwardPlus; the IV-FP PS reads no PS constants
	// and samples s0+s1 only — see the bind block below). The old
	// unconditional call therefore bound s5/s6/s7 and pushed POINT/CLAMP onto
	// samplers 5-7 with NO restore (forwardplus.cpp "feature-owned" samplers)
	// on every PS2/GTAIV building draw while forwardPlusEnable=1 — the same
	// regression-suspect-#5 leak the Xbox cb gates at :1249. Gate it the same
	// way, keeping only the callee's disabled-branch cleanup:
	//   * forwardPlusEnable=1 → SKIP: nothing here consumes the data, so no
	//     binds/sampler writes may happen (behaviour change is the fix).
	//   * forwardPlusEnable=0 → CALL (unchanged behaviour): the callee's
	//     disabled branch (forwardplus.cpp:1021) does cleanup ONLY — c45=0 +
	//     s5/s6/s7 unbind, no texture binds, no sampler writes — which is the
	//     gate-close path ForwardPlus_CullAndUpload relies on (:842) and which
	//     must keep running from inside the frame render.
	// BUILDING_PBR never dispatches here (switch :2071), so the PBR cb's own
	// unconditional consumer call (:1752) is untouched; g_fpGpuLightCount is
	// advanced by ForwardPlus_CullAndUpload, not by this call, so nothing is
	// lost. NOTE (verified via token disassembly): the currently bound
	// GTAIVBuilding_ps.cso / GTAIVVehicle_ps.cso are 7-slot blends that do NOT
	// read c45 — and the GTAIVForwardPlus pair (bound behind
	// config->forwardPlusEnable below) is byte-identical to the frozen
	// GTAIVVehicle pair and reads no PS constants at all.
	if(!config->forwardPlusEnable){
		ForwardPlus_SetConstants();	// disabled branch: cleanup only (c45=0, s5/s6/s7 unbind)
		bTrace[BTR_PS2].fp = 1;	// instrumentation only: SetConstants ran (cleanup branch)
	}

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);
	int alphafunc = rs.alphafunc, alpharef = rs.alpharef;
	int src = rs.src, dst = rs.dst;
	int fog = rs.fog, zwrite = rs.zwrite;

	// W8-2 tree lane: FF OverrideTreeAlpha — move the FFP alpha-test ref by
	// the profile's alphamul for this cb (tree atomics only; no-ops when the
	// profile is Off or the atomic is not a tree). Applied AFTER the cb-entry
	// snapshot above so `rs` keeps the pre-tree base ref, restored before
	// buildingPipe_restoreRenderState below — the push/restore pair is
	// unconditional on every path out of this cb (see treeWind.h).
	TreeAlpha_ApplyAlphaRef(atomic, rs.alpharef);

	// Per-mesh write elision (POD locals, reset every callback): the L1 blend
	// enable is skipped only when THIS callback already pushed the same value,
	// the sentinel forces the first mesh to write, and stage 0 only re-binds a
	// different RwTexture*. c4 is invariant for the whole callback
	// (buildingAmbient is refreshed once per frame), so its compare is
	// exact-bitwise and the first mesh always uploads.
	int elideLastVtxAlpha = -1;
	RwTexture *elideLastTex0 = NULL;
	bool elideTex0Valid = false;
	bool elideAmbientValid = false;
	float elideAmbientLast[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// ENTRY CONTRACT (test-4 binding fix): same three-layer white-texture
	// flush as the Xbox/PBR cbs — this cb's stage-0 binds ride the same
	// bldBindDiffuse3L path and the same postfx leak class.
	bldEntryContractTex0();

	for(numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		material = instancedData->material;

		colorScale = 1.0f;
		if(material->texture)
			colorScale = config->ps2ModulateBuilding ? 255.0f/128.0f : 1.0f;
		buildingPipe_bindTexStage(material->texture, 0, &elideLastTex0,
		                          &elideTex0Valid, BPELIDE_PS2);
		RwD3D9SetPixelShaderConstant(0, &colorScale, 1);
		RwD3D9SetVertexShaderConstant(REG_shaderParams, &colorScale, 1);

		buildingPipe_setUVTransform(material, &ident);


		DefinedVertexShader definedVertexShader = (DefinedVertexShader)TexDefsGetDefinedShader(atomic);

		if (definedVertexShader == DefinedVertexShader::WIND) {
			hasAlpha = instancedData->material->color.alpha != 255;
		}
		else {
			hasAlpha = (bool)(instancedData->vertexAlpha || instancedData->material->color.alpha != 255);
		}

		// L1+L2 blend pair — elided only when the previous mesh in THIS callback
		// already pushed the same value (sentinel -1 forces the first write).
		// L2 mirror (Xbox :2215 / PBR :3138 / Sphere :2511 idiom): the rw set
		// no-ops against an unchanged cache, and D3D9RenderDual's dual-pass
		// gate reads D3DRS_ALPHABLENDENABLE (layer 2). The Switch entry
		// (pipeEnterAlphaMode + pipeForceAlphaBlock) aligns L1/L2 today, so
		// this mirror is redundant on the current paths — it keeps this cb in
		// the same three-layer contract as the other three cbs so a future
		// L2-only writer in the PS2/IV path cannot strand the gate on a stale
		// baseline. AAP-PARITY sweep: doctrine uniformity, zero behaviour
		// change while the caches agree.
		if((int)hasAlpha != elideLastVtxAlpha){
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE);
			elideLastVtxAlpha = hasAlpha ? 1 : 0;
		}else
			s_bpipeElideSkip[BPELIDE_PS2] += 2;

		// Round-2 bind audit: names a texture-less mesh and the actual bound
		// fallback (elideLastTex0 now holds the RESOLVED pointer).
		buildingPipe_texDiag(material, elideLastTex0, hasAlpha ? 1 : 0, zwrite);

		pipeUploadMatCol(flags, material, REG_matCol);

		// c4 ambient: invariant for the whole callback — exact-bitwise skip.
		if(bpipeElideChanged4(elideAmbientValid, elideAmbientLast,
		                       (const float*)&buildingAmbient))
			RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		else
			s_bpipeElideSkip[BPELIDE_PS2]++;
		buildingPipe_uploadSurfProps(material, REG_surfProps);

		TexInfo *texinfo = TexDefsGetInfo(material->texture);

		// Swap ONLY when both handles were actually created. Commit e21b697 dropped the
		// IDR_GTAIVBUILDINGVS/PS lines from resources/Resource.rc AND the
		// makeVS/makePS calls from CreateShaders, so the handles stay NULL
		// forever; binding NULL drops every building onto the fixed-function
		// path while this cb only uploads WVP to VS constants c0-c3 (which
		// FFP ignores) and never sets FFP texture-stage/transform state —
		// that is the IV-only "floating white squares" artifact. Fall back
		// to the proven PS2 shader path; the swap resumes automatically once
		// the loading side (pipelinecommon/Resource.rc — other lane) is
		// repaired.
		// W8-2: wantIVBuilding / useIVFP / haveIVPair are now hoisted above
		// the loop (the wind tilt needs them before the WVP upload); the
		// log below keeps its original position and condition.
		if(wantIVBuilding && !haveIVPair &&
		   dbglog_throttle("ivbld_missing"))
			dbglog("[BUILDING] IV shaders missing (ivMode=%d buildingPipe=%d; VS=%p PS=%p) — using PS2 path",
			       config->ivMode, config->buildingPipe, gtaivBuildingVS, gtaivBuildingPS);

		if(wantIVBuilding && haveIVPair){
			if(useIVFP){
				RwD3D9SetVertexShader(gtaivFPVS);
				bpipeBindVS(bTrace[BTR_PS2], gtaivFPVS);
				RwD3D9SetPixelShader(gtaivFPPS);
				bpipeBindPS(bTrace[BTR_PS2], gtaivFPPS);
			}else{
				RwD3D9SetVertexShader(gtaivBuildingVS);
				bpipeBindVS(bTrace[BTR_PS2], gtaivBuildingVS);
				RwD3D9SetPixelShader(gtaivBuildingPS);
				bpipeBindPS(bTrace[BTR_PS2], gtaivBuildingPS);
			}
			// GTAIVBuilding_ps computes its env UV as `uv = v1*c0.x + c0.x`
			// (shader-local `def c0, 0.5, 0, 0, 0` — maps [-1,1] -> [0,1]).
			// The per-mesh loop pushed colorScale (1.0, or 255/128 with
			// ps2ModulateBuilding) into PS c0 a few lines above, which warps
			// that remap; re-assert the shader's own constant AFTER the bind.
			// (Inert for the current GTAIVForwardPlus_ps — it reads no PS
			// constants at all; kept so both IV pairs share one bind block.)
			float ivPsC0[4] = { 0.5f, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, ivPsC0, 1);
			// GTAIVBuilding_ps samples s1 unconditionally (its env lerp).
			// s1 is only ever bound for env-mapped materials further down, so
			// for everything else the read hit a NULL stage. The lerp weight
			// is 0 for non-env materials, but bind anyway so the sampled texel
			// is defined (NaN-safe: NaN*0 is still NaN and renders black).
			// (Same need for GTAIVForwardPlus_ps: it samples s0+s1 too.)
			// NULL material texture: pipeSetTexture binds gpWhiteTexture.
			// The old `if(material->texture)` skipped the bind entirely and
			// left the PREVIOUS mesh's stage-1 raster under the IV shader —
			// exactly the texture-raster leak class of R2-01.
			pipeSetTexture(material->texture, 1);
		}else if (definedVertexShader == DefinedVertexShader::WIND && instancedData->vertexAlpha) {
			setWindParams(atomic, frame);
			RwD3D9SetVertexShader(ps2BuildingWindVS);
			bpipeBindVS(bTrace[BTR_PS2], ps2BuildingWindVS);
			buildingPipe_bindSimplePS(atomic, material, texinfo, false);
		}
		else {
			RwD3D9SetVertexShader(ps2BuildingVS);
			bpipeBindVS(bTrace[BTR_PS2], ps2BuildingVS);
			buildingPipe_bindSimplePS(atomic, material, texinfo, false);
		}

		if(material->pipeline == (RxPipeline*)TagRenderCB){
			TagRenderCB(atomic, resEntryHeader, instancedData);
			continue;
		}

		D3D9RenderDual(config->dualPassBuilding, resEntryHeader, instancedData, texinfo);

		// Reflection
		bool ps2EnvOk = false;
		if(*(int*)&material->surfaceProps.specular & 1){
			envData = *RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material, CCustomCarEnvMapPipeline__ms_envMapPluginOffset);
			// NULL-deref / uninitialised-plugin-slot guard (audit): this cb
			// dereferenced the envmap plugin slot unchecked — the Xbox
			// (:2288-2295) and PBR (:3197-3244) cbs gate the same lane on
			// envData && texture && raster + the RT-class raster check. A
			// material with the env bit but no allocated plugin crashed on
			// envData->texture; an RT-class raster (envFB sphere capture /
			// Coronas reflection RT swapped through the plugin slot) bound
			// animated screen content into the reflection (the "baked
			// lightmap" artifact). Same three guards; the bldEnvReflect
			// config gate stays Xbox/PBR-only (PS2 env parity is the INI
			// lane's call, not a defect fix).
			if(envData && envData->texture && envData->texture->raster){
				RwUInt32 envRasType = envData->texture->raster->cType & rwRASTERTYPEMASK;
				ps2EnvOk = (envRasType == rwRASTERTYPENORMAL ||
				            envRasType == rwRASTERTYPETEXTURE);
			}
		}
		if(ps2EnvOk){
			// Three-layer mirror of vehiclePipe_fxAdditiveBlend
			// (vehiclePipe.cpp:640-695). This branch used to push AND restore
			// blend/alpha/zwrite/fogcolor through the rw cache (L1) ONLY: an
			// applied[]-gated no-op could leave the DEVICE on the previous
			// mesh's blend while the cache claimed ONE/ONE, and the
			// VERTEXALPHA=TRUE push was never restored at all — the exact
			// failure class the vehicle twin documents. Every state below now
			// writes L1 (RwRenderStateSet) + L2 (RwD3D9SetRenderState driver
			// cache) + L3 (raw device), and the per-mesh VERTEXALPHA value is
			// snapshotted and handed back.
			// ALPHATESTENABLE is forced TRUE below on L2 + raw L3 and was
			// never handed back, so every later pass ran with the test on.
			// Snapshot the raw device value at leg entry (pre-seeded TRUE so
			// a NULL device still yields the conservative pair) and restore
			// it on exit. ALPHAFUNC is already restored from the cb-entry
			// `alphafunc` snapshot (same L2 + raw L3 pair); ALPHAREF is
			// re-based raw because the rw restore path has no D3DRS_ALPHAREF
			// write.
			DWORD savedAlphaTest = TRUE;
			DWORD savedAlphaRef = (DWORD)alpharef;
			if(d3d9device){
				d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &savedAlphaTest);
				d3d9device->GetRenderState(D3DRS_ALPHAREF, &savedAlphaRef);
			}
			RwUInt32 savedVtxAlpha = FALSE;
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			// NULL env texture: gpWhiteTexture fallback, never a raw NULL bind
			// (the previous stage-1 raster would leak under the FX PS).
			pipeSetTexture(envData->texture, 1);
			// Same int8 wraparound guard as the Xbox lane: GetShininess() is
			// int8/255.0f, so an authored value >=128 wraps negative and makes
			// the env term subtractive. GetScaleX/Y are int8 too -- 0 collapses
			// Texcoord1 to one sphere-map texel on a flat surface.
			float ps2EnvShin = envData->GetShininess();
			if(!(ps2EnvShin >= 0.0f)) ps2EnvShin = 0.0f;	// also catches NaN
			if(ps2EnvShin > 1.0f) ps2EnvShin = 1.0f;
			fxParams.shininess = ps2EnvShin;
			fxParams.lightmult = 1.0;
			envXform.x = 0.0;
			envXform.y = 0.0;
			float ps2EnvSX = envData->GetScaleX();
			float ps2EnvSY = envData->GetScaleY();
			if(!(ps2EnvSX > 0.0f)) ps2EnvSX = 1.0f;
			if(!(ps2EnvSY > 0.0f)) ps2EnvSY = 1.0f;
			envXform.z = ps2EnvSX;
			envXform.w = ps2EnvSY;
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);
			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);

			RwD3D9SetVertexShader(ps2BuildingFxVS);
			bpipeBindVS(bTrace[BTR_PS2], ps2BuildingFxVS);
			// ps2EnvSpecFxPS samples envMapTex(s1) * envcolor + maskTex(s2) *
			// speccolor (backup_original/buildingPipe.cpp:341 used it here);
			// the env bind above already occupies s1. simplePS samples s0
			// (diffuse) at env UVs — wrong texture. NULL-guard: resource-load
			// failure keeps the previous stable behaviour over a NULL PS.
			RwD3D9SetPixelShader(ps2EnvSpecFxPS ? ps2EnvSpecFxPS : simplePS);
			bpipeBindPS(bTrace[BTR_PS2], ps2EnvSpecFxPS ? ps2EnvSpecFxPS : simplePS);

			// FX legs MUST keep ALPHAFUNC=ALWAYS + ONE/ONE — RGB=0 soft
			// borders are the black-quad payload if drift bypasses
			// fxAdditiveBlend.
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
			RwD3D9SetRenderState(D3DRS_ALPHAFUNC, (RwUInt32)D3DCMP_ALWAYS);
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, (RwUInt32)D3DBLEND_ONE);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, (RwUInt32)D3DBLEND_ONE);
			RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)0);
			RwD3D9SetRenderState(D3DRS_FOGCOLOR, 0);
			if(d3d9device){
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
				d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				d3d9device->SetRenderState(D3DRS_FOGCOLOR, 0);
			}
			D3D9Render(resEntryHeader, instancedData);
			// Restore: SAME values as before (the cb-entry snapshot locals at
			// :612-614 — note the vehicle twin hard-codes ZWRITE=TRUE here;
			// the building branch keeps its snapshot semantics), now pushed
			// through all three layers.
			RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)fog);
			RwD3D9SetRenderState(D3DRS_FOGCOLOR, (RwUInt32)fog);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)zwrite);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, (RwUInt32)zwrite);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, pipeBlendToD3D((RwUInt32)src, D3DBLEND_SRCALPHA));
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, pipeBlendToD3D((RwUInt32)dst, D3DBLEND_INVSRCALPHA));
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
			RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)alphafunc));
			// Hand ALPHATESTENABLE back (L2 + raw L3 below): the TRUE force
			// above is leg-local, not a callback-wide state.
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, savedAlphaTest);
			// Close the VERTEXALPHA=TRUE leak through all three layers (L1 read
			// back per mesh; L2 is what D3D9RenderDual's gate reads).
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
			// TEXTUREADDRESS: no D3DRS counterpart (L2 N/A) — restore the L1
			// cache and the L3 device sampler addressing for the stages this
			// pass samples (0 diffuse, 1 env) to the cb-entry snapshot.
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rs.textureaddress);
			if(d3d9device){
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, (DWORD)zwrite);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, pipeBlendToD3D((RwUInt32)src, D3DBLEND_SRCALPHA));
				d3d9device->SetRenderState(D3DRS_DESTBLEND, pipeBlendToD3D((RwUInt32)dst, D3DBLEND_INVSRCALPHA));
				d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)alphafunc));
				d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, savedAlphaTest);
				d3d9device->SetRenderState(D3DRS_ALPHAREF, savedAlphaRef);
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
				d3d9device->SetRenderState(D3DRS_FOGCOLOR, (DWORD)fog);
				d3d9device->SetSamplerState(0, D3DSAMP_ADDRESSU, rs.sampU[0]);
				d3d9device->SetSamplerState(0, D3DSAMP_ADDRESSV, rs.sampV[0]);
				d3d9device->SetSamplerState(1, D3DSAMP_ADDRESSU, rs.sampU[1]);
				d3d9device->SetSamplerState(1, D3DSAMP_ADDRESSV, rs.sampV[1]);
			}
		}
	}
	// W8-2: undo the tree alpharef move BEFORE the cb-entry restore below
	// (both write the same base ref through the same layers; the explicit
	// Restore also guarantees the RAW D3DRS_ALPHAREF write from Apply is
	// re-based even if nothing drew during this cb).
	TreeAlpha_RestoreAlphaRef();
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
	bpipeTraceLog("PS2", BTR_PS2, "bpipe_ps2", atomic);	// instrumentation only
	// Exit contract: this cb also serves BUILDING_GTAIV (IV = PS2 cb + IV
	// shader swap) — pipe= in the probe line names the mode that fired.
	bpipeBlendLeakProbe("PS2");	// stale-blend leak probe + repair
}

// The PC callback cannot be salvaged. Just do it Xbox-style instead
//
// ============================================================
// Forward+ cluster-data consumers — which building pipes sample PS c45
// and the s5/s6/s7 atlases that ForwardPlus_SetConstants uploads?
//
// Verified consumers: the PBR building PS (main_building, IDR_BUILDINGPBRPS
// — VehiclePBR_Modern.hlsl), VehiclePBR_Modern main() and the vehicle
// glass/rubber passes. NON-consumers: the Xbox simple-PS path
// (buildingPipePS.hlsl registers s0/s1/s2 only — simplePS, simpleDetailPS,
// simpleDetailNoiseDetailPS, xboxBuildingPS, xboxBuildingNoiseDetailPS,
// simpleFogPS), the PS2/GTAIV building pair and sphereBuildingVS (none of
// them reads c45 — see the IV-pair note in the PS2 cb).
//
// Why it matters: ForwardPlus_SetConstants binds s5-s7 and sets
// POINT/CLAMP on those samplers WITHOUT restoring them (forwardplus.cpp,
// "feature-owned" comment). A cb that does not sample the cluster data
// still leaves that state live for every later pass of the frame — the
// three-layer doctrine violation behind regression suspect #5.
// ============================================================
static bool
buildingPipe_consumesForwardPlus(void)
{
	if(!config || !config->forwardPlusEnable)
		return false;
	return config->buildingPipe == BUILDING_PBR;
}

// Black-world lighting audit — one 2s-throttled line with the values the
// Xbox/PS2 building VS actually lights from (VS c4/c30/c31 + day/night
// prelight balance + camera frame). Instrumentation only: reads game state,
// writes nothing. Values printed here are exactly what the W-tree uploads
// in setDnParams / CustomBuildingPipeline__Update, so a mismatch between
// "renders black" and these numbers points at the consumer, not the upload.
//
// "outdoor cutscene dark" dig fields (addresses per gta-reversed, SA 1.0 US):
//   clock   = CClock ms_nGameClockHours:ms_nGameClockMinutes (0xB70153/0xB70152)
//   cut     = CPostEffects::m_bInCutscene          (0xC402B7)
//   nv/ir   = m_bNightVision / m_bInfraredVision   (0xC402B8 / 0xC402B9)
//   vfxBal  = CPostEffects::m_VisionFXDayNightBalance (0x8D50A4)
//   dnBalRaw= unclamped CCustomBuildingDNPipeline::m_fDNBalanceParam (0x8D12C0)
// Vanilla FilterFX_StoreAndSetDayNightBalance (0x7034B0) overwrites
// m_fDNBalanceParam with vfxBal when !m_bInCutscene and Restore (0x7034D0)
// re-applies the same guard — a vision-FX/cutscene interleave leaves dnBal
// stuck at the vision value. If dnBalRaw!=0 while cut=1 nv=0 ir=0 and
// clock is daytime, that leak (or the 0x5D7F80 writer) is the suspect.
// The four CPostEffects refs themselves are defined near the top of this
// file (above setDnParams) — the dnBal repair reads them there.

static void
bpipeLightAuditLog(const char *cb, RwUInt32 flags)
{
	if(!dbglog_throttle("bpipe_light_audit"))
		return;
	if(!config || Scene.camera == NULL)	// instrumentation must never be the first NULL deref
		return;
	float balanceRaw = CCustomBuildingDNPipeline__m_fDNBalanceParam;
	float balance = balanceRaw;
	if(balance < 0.0f) balance = 0.0f;
	if(balance > 1.0f) balance = 1.0f;
	RwRaster *cr = RwCameraGetRaster(Scene.camera);
	float dirR = -1.0f, dirG = -1.0f, dirB = -1.0f;
	RwUInt32 dirFlags = 0xFFFFFFFF;
	if(pDirect){
		dirR = pDirect->color.red;
		dirG = pDirect->color.green;
		dirB = pDirect->color.blue;
		dirFlags = (RwUInt32)RpLightGetFlags(pDirect);
	}
	dbglog("[BLIT] cb=%s amb=(%.3f,%.3f,%.3f) dnBal=%.3f(day=%.3f night=%.3f) "
		"camRas=%p %dx%d clip=%.2f/%.1f pDirect(rgb=%.3f,%.3f,%.3f lflags=0x%X) flags=0x%X "
		"clock=%d:%02d cut=%d nv=%d ir=%d vfxBal=%.3f dnBalRaw=%.3f",
		cb, buildingAmbient.red, buildingAmbient.green, buildingAmbient.blue,
		balance, 1.0f - balance, balance,
		(void*)cr, cr ? cr->width : 0, cr ? cr->height : 0,
		(float)RwCameraGetNearClipPlane(Scene.camera),
		(float)RwCameraGetFarClipPlane(Scene.camera),
		dirR, dirG, dirB, dirFlags, flags,
		(int)CClock__ms_nGameClockHours, (int)CClock__ms_nGameClockMinutes,
		(int)CPostEffects__m_bInCutscene,
		(int)CPostEffects__m_bNightVision,
		(int)CPostEffects__m_bInfraredVision,
		CPostEffects__m_VisionFXDayNightBalance,
		balanceRaw);
}

// Cull mode the building Switch captured at pipe entry (pipeEnterCullMode),
// re-asserted by the Xbox cb immediately before its mesh draw loop. The saved
// value — not a hardcoded BACK — is the contract: the mirror pass legitimately
// runs with CULLNONE and must survive.
static RwUInt32 s_bldSwitchSavedCull = (RwUInt32)rwCULLMODECULLBACK;

// One-shot opacity diagnostic shared by the Xbox and PBR building cbs: the
// first 16 meshes (total) print the texture name, alpha class and the
// resulting blend decision so misclassified foliage/building textures are
// named instead of guessed.
static int s_bldMeshLogged = 0;

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	PF_SCOPE("BuildingPipe_Xbox");	// instrumentation only
	bTraceBegin(bTrace[BTR_XBOX]);	// instrumentation only
	bTraceCur = &bTrace[BTR_XBOX];	// instrumentation only
	bpipeElideSummary();	// throttled elision report (previous window)
	s_noiseDetailBoundCB = false;	// one pipeBindNoise(9) per callback at most
	if(++xboxFrameCount % 300 == 1)
		dbglog("XboxBuilding: frame %d object=%p type=%d flags=0x%X", xboxFrameCount, object, type, flags);

	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32	numMeshes;
	CustomEnvMapPipeMaterialData *envData = NULL;
	float colorScale;
	struct {
		float shininess;
		float lightmult;
	} fxParams;
	RwV4d envXform;
	RwMatrix envmat;
	float transform[16];
	RwMatrix ident;

	atomic = (RpAtomic*)object;
	RwMatrixSetIdentity(&ident);

	// TEXTUREADDRESS/sampler/vertex-alpha bundle — mirror of the PS2/GTAIV cb
	// (save :647-648 / restore :835) and the building twin of the vehicle
	// three-layer doctrine. The Xbox env branch (:952) pushes
	// rwTEXTUREADDRESSWRAP through the rw cache (L1) ONLY and this cb never
	// saved/restored anything — the WRAP and the last mesh's
	// VERTEXALPHAENABLE (:932) leaked into every later draw.
	// BuildingRenderState covers all three TEXTUREADDRESS layers: L1 (rw
	// cache), L2 (N/A — addressing has no D3DRS counterpart), L3 (per-stage
	// device sampler addressing, stages 0-2 = the stages this cb samples:
	// s0 diffuse, s1 env, s2 detail via buildingPipe_selectPS). Saved BEFORE
	// the frame check so the early return below also restores — save/restore
	// stay paired on every path.

	// Forward+ clustered lights: the cluster data (PS c45 + s5/s6/s7
	// atlases) is sampled ONLY by the PBR building PS (main_building) and
	// the vehicle PBR passes — the Xbox simple-PS path never touches those
	// registers (buildingPipePS.hlsl declares s0/s1/s2 only). Uploading
	// here anyway (suspect #5) bound the atlases and pushed POINT/CLAMP
	// onto samplers 5-7 with no restore (forwardplus.cpp SetConstants), so
	// that state leaked past this cb into every later pass of the frame
	// (SMAA/pipechain/velocity/normal-buffer/water). Gate it on the pipe
	// whose shaders actually consume the cluster data; BUILDING_PBR keeps
	// its own unconditional call in the PBR cb, so PBR default behaviour
	// is unchanged. g_fpGpuLightCount is advanced by
	// ForwardPlus_CullAndUpload — not by this call — so nothing is lost.
	if(buildingPipe_consumesForwardPlus()){
		ForwardPlus_SetConstants();
		bTrace[BTR_XBOX].fp = 1;	// instrumentation only: cluster-light upload ran
	}else if(config && config->forwardPlusEnable){
		// Fail-safe breadcrumb: "handles fp=00000000/00000000" in the
		// [BPIPE] Xbox line is BY DESIGN, not a load failure. The Xbox
		// simple-PS family (xboxBuilding*/simple* — buildingPipePS.hlsl)
		// declares s0/s1/s2 and never reads PS c45 or samples s5-s7, so this
		// cb must not bind cluster state (same leak contract as the PS2 cb's
		// gated ForwardPlus call). The GTAIVForwardPlus pair
		// (gtaivFPVS/gtaivFPPS) is created lazily by ivFPReady() ONLY on the
		// IV-swap paths (PS2/GTAIV cb useIVFP + vehicle IV swap) and is
		// currently a byte-identical placeholder of the GTAIVVehicle pair
		// (zero forward+ registers) — creating or binding it here would put
		// vehicle shaders on Xbox buildings for nothing. One log per process
		// so the next log dig reads fp=NULL as "no consumer on this pipe".
		if(dbglog_first("bpipe_xbox_fp"))
			dbglog("[BPIPE] Xbox building cb: forwardPlusEnable=1 but the Xbox simple-PS family has no cluster consumer — FP constants skipped; GTAIVForwardPlus pair intentionally not created/bound here (handles %p/%p)",
			       gtaivFPVS, gtaivFPPS);
	}

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);
	RwUInt32 savedVtxAlpha = FALSE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);

	RwFrame* frame = (RwFrame*)atomic->object.object.parent;
	if(!frame){
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
		buildingPipe_restoreRenderState(&rs);
		return;
	}

	if (RWSRCGLOBAL(curCamera)) { _rwD3D9EnableClippingIfNeeded(object, type); }
	// NULL curCamera → 0x7FAD4D fault

	// W8-2 tree lane: FF OverrideTreeAlpha alpharef move (see the PS2 cb
	// for the full contract). Placed AFTER the frame-NULL early return so
	// Apply/Restore stay paired on every path that applies.
	TreeAlpha_ApplyAlphaRef(atomic, rs.alpharef);

	colorScale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorScale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);

	buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);


	DefinedVertexShader definedVertexShader = (DefinedVertexShader)TexDefsGetDefinedShader(atomic);

	setDnParams(atomic);

	// Xbox direct-light term (dawn/day black-world fix): the Xbox building
	// VS family previously lit from ambient + DN prelight ONLY (VS combine
	// Color=(prelight*surfDiff + ambient*surfAmb)*matCol). With lflags=0x0
	// (vanilla DeActivateDirectional 0x735C70 — a per-entity transient, NOT
	// a light-off signal; log-proven 0x0 even in daylight) ambient is the
	// only dynamic term, so a crushed ambient at dawn renders the world
	// near-black. Force-upload the sun like the vehicle slot-0 idiom
	// (pipeUploadLightColorForce, vehiclePipe uploadLights :555): pDirect's
	// color is near-black when the timecycle deactivates the sun, so the
	// term is naturally zero then — no flag gating. Direction = pDirect
	// frame LTM 'at' (light-travel direction, unit-length from RW
	// orthonormal frames — NOT re-normalized in the VS; the VS negates it,
	// dot(N,-dir), same convention as buildingPBRVS :112 / mobileVehicleVS
	// :57). Uploaded once per cb, before the mesh loop; the wind VS
	// (xboxBuildingWindVS) does not declare c5/c6 and simply ignores them.
	pipeUploadLightColorForce(pDirect, REG_directCol);
	pipeUploadLightDirectionForce(pDirect, REG_xbDirectDir);

	CustomBuildingEnvMapPipeline__SetupEnv(atomic, NULL, &envmat);
	RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);

	bool vertexAlphaIsAlpha = true;
	if (definedVertexShader == DefinedVertexShader::WIND) {
		vertexAlphaIsAlpha = false;
		setWindParams(atomic, frame);
		RwD3D9SetVertexShader(xboxBuildingWindVS);
		bpipeBindVS(bTrace[BTR_XBOX], xboxBuildingWindVS);
	}
	else {
		RwD3D9SetVertexShader(xboxBuildingVS);
		bpipeBindVS(bTrace[BTR_XBOX], xboxBuildingVS);
	}

	// Cull re-assert at the draw boundary (PBR :1898 idiom): the Switch
	// forced the sanitized pre-entry cull through L1/L2/L3 and this cb has no
	// mid-callback Im2D/state-heavy pass, so one assert here is the whole
	// contract. Re-assert the SAVED value, never a hardcoded BACK — the
	// mirror pass legitimately runs CULLNONE. A device that drifted behind an
	// unchanged cache (the double no-op in pipelinecommon pipeForceCullMode)
	// is repaired here before any mesh draws.
	pipeForceCullMode(s_bldSwitchSavedCull);

	// ENTRY CONTRACT (test-4 binding fix): flush any screen-family raster a
	// postfx/water pass leaked onto device stage 0 by binding gpWhiteTexture
	// through all three layers BEFORE the first mesh draws. Raw bypass of the
	// elision tracker (below) so the first per-mesh bind still runs full.
	bldEntryContractTex0();

	// Per-callback decal depth tracking (PBR :2126-2129): set when a
	// material-translucent mesh forced ZWRITE off, so the next non-decal mesh
	// gets the callback baseline (ZWRITE TRUE, forced by the Switch guard)
	// back before it draws.
	bool decalZWriteOff = false;

	// Per-mesh write elision (POD locals, reset every callback): sentinels
	// force the first write, texture stage 0 only re-binds a changed
	// RwTexture*, and c4 (invariant for the whole callback) is compared
	// exact-bitwise. See bpipeElideSummary for the safety contract.
	int elideLastBlend = -1;
	int elideLastZWrite = -1;
	RwTexture *elideLastTex0 = NULL;
	bool elideTex0Valid = false;
	bool elideAmbientValid = false;
	float elideAmbientLast[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	for(numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		material = instancedData->material;

		buildingPipe_bindTexStage(material->texture, 0, &elideLastTex0,
		                          &elideTex0Valid, BPELIDE_XBOX);

		buildingPipe_setUVTransform(material, &ident);

		// Per-texture alpha class + decal policy — the PBR table (:2178-2236)
		// ported into the Xbox cb. The old block wrote ONLY L1
		// VERTEXALPHAENABLE from vertexAlpha||matA!=255, so D3D9RenderDual's
		// L2 hasAlpha gate saw the Switch's FALSE baseline and the
		// material-translucent decal sublayers blended with ZWRITE on.
		// SA paint marker: color.alpha==16 selects the paint colour, it is
		// NOT a translucency value (same rule as PBR/vehiclePipe).
		const bool matPaintMarker = (material->color.alpha == 16);
		const bool matTranslucent  = (material->color.alpha != 255 && !matPaintMarker);
		// The wind lane keeps its rule — vertex alpha there is the wind
		// weight, not opacity, so only material alpha counts (the
		// !vertexAlphaIsAlpha override below).

		int tac = (config && config->texAlphaClassify) ? TexAlpha_Classify(material->texture) : TAC_UNKNOWN;
		int blendOn;
		// Same decision table as the PBR cb. tac=0 is TAC_UNKNOWN, NOT
		// OPAQUE — a budget-skipped / not-yet-classified texture takes the
		// SAFE DEFAULT (matTranslucent-only blend; see the PBR cb's
		// TAC_UNKNOWN arm for the full rationale — vertex alpha alone must
		// never blend solid geometry or the mesh blends AND ZWRITE-carves).
		// The Xbox building PS
		// family (buildingPipePS.hlsl: main_simple / main_simpleDetail /
		// main_simpleFog / main_xboxBuilding / main_normMapBuilding) reads no
		// c48 and outputs the texture alpha unchanged, so there is no
		// shaderAlpha slot to write — only the CPU blend decision.
		switch(tac){
		case TAC_OPAQUE:	// no usable alpha data → alpha test only
		case TAC_CUTOUT:	// hard 0/255 mask → alpha test, no blending
			blendOn = matTranslucent ? 1 : 0; break;
		case TAC_FADED:		// intermediate alpha → blend + texture alpha
			blendOn = 1; break;
		case TAC_UNKNOWN:	// tac=0 == UNKNOWN → SAFE DEFAULT (matTranslucent)
		default:
			blendOn = matTranslucent ? 1 : 0; break;
		}
		// Only a REAL material alpha (never the alpha-16 paint marker) forces
		// blending.
		if(matTranslucent)
			blendOn = 1;

		// FOLIAGE FIX (R2-07): for a WIND atomic vertex alpha is the WIND
		// WEIGHT, not opacity (vertexAlphaIsAlpha is false above). A TAC_FADED
		// texture must not switch blending back on for a tree: PS2 already
		// derives hasAlpha from material alpha only, so this callback used to
		// draw the same foliage blended while PS2 drew it alpha-tested.
		// Foliage keeps alpha TEST (Switch GREATERQUAL@ref block) with blend
		// OFF and ZWRITE ON; only a real MATERIAL alpha may blend.
		if(!vertexAlphaIsAlpha)
			blendOn = matTranslucent ? 1 : 0;

		// L1+L2 blend pair — elided only when the previous mesh in THIS
		// callback already pushed the same value. Sentinel -1 forces the
		// first write; D3D9RenderDual and the PipeChain bracket both restore
		// this pair balanced, so the tracker cannot go stale.
		if(blendOn != elideLastBlend){
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)blendOn);
			// Layer-(2) mirror (PBR :2279-2284): the rw set no-ops against an
			// unchanged cache and leaves D3DRS_ALPHABLENDENABLE at the Switch's
			// FALSE baseline, so D3D9RenderDual's hasAlpha gate would read
			// blend-off and drop the dual pass.
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendOn ? TRUE : FALSE);
			elideLastBlend = blendOn;
		}else
			s_bpipeElideSkip[BPELIDE_XBOX] += 2;

		// DECAL depth policy (PBR :2208-2236): a mesh that blends because of
		// TRUE material translucency, OR a FADED soft decal mask
		// (texture-alpha decals — material alpha 255), is an overlay and
		// must not write depth. ZTEST stays TRUE. All three layers are
		// written because a cache-only write is applied[]-gated and can leave
		// the device writing depth; the Switch restores the pre-entry values
		// on exit, and the else closes the policy before the next non-decal
		// mesh's draw.
		const bool decalBlend = (blendOn != 0) && (matTranslucent || tac == TAC_FADED);
		if(decalBlend){
			// Consecutive decal meshes share the same three-layer FALSE
			// burst — skip it when THIS callback already pushed it.
			if(elideLastZWrite != 0){
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				if(d3d9device)
					d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				elideLastZWrite = 0;
			}else
				s_bpipeElideSkip[BPELIDE_XBOX] += 3;
			decalZWriteOff = true;
		}else if(decalZWriteOff){
			// Next mesh is not a decal: hand the callback baseline (ZWRITE
			// TRUE, forced by the Switch guard) back before its draw.
			if(elideLastZWrite != 1){
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				if(d3d9device)
					d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				elideLastZWrite = 1;
			}else
				s_bpipeElideSkip[BPELIDE_XBOX] += 3;
			decalZWriteOff = false;
		}

		// Alpha test is left to the Switch (pipeForceAlphaBlock) and
		// TreeAlpha_ApplyAlphaRef exactly as the PBR cb leaves it: blendOn
		// meshes keep the Switch's values, !blendOn meshes are cut by the
		// Switch's GREATEREQUAL@ref block (no per-mesh ALPHA* write here).

		// One-shot opacity diagnostic (first 16 meshes): texture, class,
		// final blend decision, material alpha and zw (1 = normal depth
		// write, 0 = material-translucent decal with ZWRITE forced off).
		if(s_bldMeshLogged < 16){
			s_bldMeshLogged++;
			dbglog("[BldXbox] tex='%s' tac=%d blendOn=%d matA=%u zw=%d",
			       (material->texture && material->texture->name[0]) ? material->texture->name : "",
			       tac, blendOn, (unsigned)material->color.alpha, decalBlend ? 0 : 1);
		}
		// Round-2 bind audit (first 24 meshes, throttled).
		buildingPipe_texDiag(material, elideLastTex0, blendOn, decalBlend ? 0 : 1);

		// c4 ambient: invariant for the whole callback — exact-bitwise skip.
		if(bpipeElideChanged4(elideAmbientValid, elideAmbientLast,
		                       (const float*)&buildingAmbient))
			RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		else
			s_bpipeElideSkip[BPELIDE_XBOX]++;
		
		if(flags & rpGEOMETRYLIGHT){
			pipeUploadMatCol(flags, material, REG_matCol);
			buildingPipe_uploadSurfProps(material, REG_surfProps);
		}else{
			static float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			// float4 with defined .w — a raw RwSurfaceProperties{3 floats}
			// upload would read 4 bytes past the struct (same class as the
			// c20 surfProps contract above).
			float surf[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
			RwD3D9SetVertexShaderConstant(REG_matCol, white, 1);
			RwD3D9SetVertexShaderConstant(REG_surfProps, surf, 1);
		}

		TexInfo *texinfo = TexDefsGetInfo(material->texture);
		// ENV-LANE GATE (Xbox). The old gate was the raw specular bit and
		// dereferenced the plugin data unchecked. Same three guards the PBR cb
		// has (:2848-2872):
		//  1) envData NULL -- the envmap plugin slot was never allocated on
		//     this material;
		//  2) texture or raster NULL -- an uninitialised plugin slot;
		//  3) RT-class raster -- a CAMERATEXTURE/ZBUFFER raster here is a
		//     render-target the envmap/water lanes swap through the plugin
		//     slot; sampling it binds animated screen content into the
		//     building reflection (floor shows a moving "baked lightmap").
		// bldEnvReflect (INI `bldEnvReflect`, default 1) now gates this Xbox
		// env lane too -- it previously only affected PBR -- giving a zero-build
		// A/B for the env-lane regressions.
		bool envOk = false;
		if(*(int*)&material->surfaceProps.specular & 1){
			envData = *RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material, CCustomCarEnvMapPipeline__ms_envMapPluginOffset);
			if(envData != NULL && envData->texture != NULL && envData->texture->raster != NULL){
				RwUInt32 envRasType = envData->texture->raster->cType & rwRASTERTYPEMASK;
				envOk = (envRasType == rwRASTERTYPENORMAL || envRasType == rwRASTERTYPETEXTURE);
			}
			envOk = envOk && (!config || config->bldEnvReflect);
		}
		if(envOk){
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			// NULL env texture: gpWhiteTexture fallback (defined sample for the
			// Xbox env PS); never a raw NULL bind that leaks the previous s1.
			pipeSetTexture(envData->texture, 1);
			// GetShininess() is int8/255.0f (gta.h CustomEnvMapPipeMaterialData):
			// a value >=128 wraps negative and makes the env term subtractive.
			// NaN-safe clamp into [0,1].
			float envShin = envData->GetShininess();
			if(!(envShin >= 0.0f)) envShin = 0.0f;   // also catches NaN
			if(envShin > 1.0f) envShin = 1.0f;
			fxParams.shininess = envShin;
			fxParams.lightmult = 1.0f;
			envXform.x = 0.0f;
			envXform.y = 0.0f;
			// GetScaleX/Y() are int8 too: 0 collapses Texcoord1 to a constant,
			// which on a flat floor samples one sphere-map texel (the "baked
			// lightmap" patch). Clamp to >=1.
			float envSX = envData->GetScaleX();
			float envSY = envData->GetScaleY();
			if(!(envSX > 0.0f)) envSX = 1.0f;
			if(!(envSY > 0.0f)) envSY = 1.0f;
			envXform.z = envSX;
			envXform.w = envSY;
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);
			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);
			// First observability of the Xbox env lane (throttled).
			if(dbglog_throttle("bld_env"))
				dbglog("[BldEnv] tex=%p cType=0x%X scale=%.2f/%.2f shin=%.3f spec=0x%X bldEnvReflect=%d",
				       (void*)envData->texture, (unsigned)envData->texture->raster->cType,
				       envXform.z, envXform.w, envShin,
				       (unsigned)(*(int*)&material->surfaceProps.specular & 7),
				       config ? config->bldEnvReflect : -1);
			// Env-mapped mesh: keep the env PS (the tree overlay would drop
			// the reflection layer); the alpharef move still covers it.
			// Instrumentation only: same call, result kept for the report.
			void *selPS = buildingPipe_selectPS(texinfo, true);
			buildingPipe_bindNoiseFor(selPS);	// s9 live before the PS bind
			RwD3D9SetPixelShader(selPS);
			bpipeBindPS(bTrace[BTR_XBOX], selPS);
		}else{
			// Reject receipt: only when the specular bit actually asked for the
			// lane (plain non-env meshes were never candidates). The non-env PS
			// (main_simple/main_simpleDetail) samples s0 (and s2 for detail)
			// only, never s1, so no extra s1 rebind is needed on this fall-through.
			if((*(int*)&material->surfaceProps.specular & 1) && dbglog_throttle("bld_env"))
				dbglog("[BldEnv] rejected spec=0x%X env=%p tex=%p raster=%p",
				       (unsigned)(*(int*)&material->surfaceProps.specular & 7),
				       (void*)envData,
				       (void*)(envData ? envData->texture : NULL),
				       (void*)((envData && envData->texture) ? envData->texture->raster : NULL));
			buildingPipe_bindSimplePS(atomic, material, texinfo, false);
		}

		if(material->pipeline == (RxPipeline*)TagRenderCB){
			TagRenderCB(atomic, resEntryHeader, instancedData);
			continue;
		}

		D3D9RenderDual(config->dualPassBuilding, resEntryHeader, instancedData, texinfo);
	}
	// Close the cb-level leaks. The per-mesh VERTEXALPHAENABLE sets (:932) and
	// the env branch's TEXTUREADDRESS=WRAP (:952) otherwise survive past this
	// cb into every later pass. Vertex alpha restored through L1 + its L2/L3
	// mirror (D3DRS_ALPHABLENDENABLE) — same savedVtxAlpha pattern as the
	// PS2/GTAIV reflection branch (:815-816 + :826). TEXTUREADDRESS (L1 + L3
	// sampler addressing for stages 0-2, L2 N/A) and the alpha/blend/fog/zwrite
	// bundle come back through buildingPipe_restoreRenderState. TagRenderCB's
	// `continue` (:968) skips a draw, not the function — it lands here
	// like every other mesh. Same ordering as the PS2 cb exit (:835).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
	// W8-2: close the tree alpharef push before the cb-entry restore.
	TreeAlpha_RestoreAlphaRef();
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
	bpipeTraceLog("Xbox", BTR_XBOX, "bpipe_xbox", atomic);	// instrumentation only
	bpipeLightAuditLog("Xbox", flags);	// instrumentation only: black-world lighting audit
	bpipeBlendLeakProbe("Xbox");	// stale-blend leak probe + repair
}

extern RwRGBAReal spheremapfog;
void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Sphere(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	PF_SCOPE("BuildingPipe_Sphere");	// instrumentation only
	bTraceBegin(bTrace[BTR_SPHERE]);	// instrumentation only
	bTraceCur = &bTrace[BTR_SPHERE];	// instrumentation only
	bpipeElideSummary();	// throttled elision report (previous window)
	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32 numMeshes;
	RwBool hasAlpha;
	float transform[16];
	RwMatrix ident;

	atomic = (RpAtomic*)object;
	RwMatrixSetIdentity(&ident);

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);
	// Mirror the PS2/Xbox/PBR exit contract: the per-mesh blend enable below is
	// a shared state and this cb previously restored NOTHING, so the last
	// mesh's VERTEXALPHAENABLE leaked into every later pass.
	RwUInt32 savedVtxAlpha = FALSE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);
	const DefinedVertexShader definedVertexShader = (DefinedVertexShader)TexDefsGetDefinedShader(atomic);

	// W8-2 tree lane: the sphere env capture runs building atomics — including
	// vegetation — through this cb too, so the tree alpharef move must be
	// pushed and restored here like every other building callback.
	TreeAlpha_ApplyAlphaRef(atomic, rs.alpharef);


	if (RWSRCGLOBAL(curCamera)) { _rwD3D9EnableClippingIfNeeded(object, type); }
	// NULL curCamera → 0x7FAD4D fault

	RwD3D9SetPixelShaderConstant(0, &spheremapfog, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);


	if (RWSRCGLOBAL(curCamera)) {
		RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera);
		float fog[2];
		fog[0] = RwCameraGetFarClipPlane(cam);
		fog[1] = fog[0] - RwCameraGetFogDistance(cam);
		RwD3D9SetPixelShaderConstant(2, fog, 1);
	} else {
		// NULL curCamera → 0x7FAD4D fault when camera not begun
		float fog[2] = { 1000.0f, 1000.0f };
		RwD3D9SetPixelShaderConstant(2, fog, 1);
	}

	buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);

	setDnParams(atomic);

	RwD3D9SetVertexShaderConstant(44, &reflectionCamPos, 1);
	RwD3D9SetPixelShaderConstant(1, &reflectionCamPos, 1);
	float worldmat[16];
	RwFrame *atomicFrame = RpAtomicGetFrame(atomic);
	if(atomicFrame){
		RwMatrix *atomicLTM = RwFrameGetLTM(atomicFrame);
		if(atomicLTM)
			RwToD3DMatrix(worldmat, atomicLTM);
		else
			memset(worldmat, 0, sizeof(worldmat));
	}else
		memset(worldmat, 0, sizeof(worldmat));
	RwD3D9SetVertexShaderConstant(REG_transform, worldmat, 4);
	RwD3D9SetVertexShader(sphereBuildingVS);
	bpipeBindVS(bTrace[BTR_SPHERE], sphereBuildingVS);	// instrumentation only
	RwD3D9SetPixelShader(simpleFogPS);
	bpipeBindPS(bTrace[BTR_SPHERE], simpleFogPS);	// instrumentation only

	// Per-mesh write elision (POD locals, reset every callback): stage 0 only
	// re-binds a changed RwTexture*, c4 is invariant for the whole callback
	// (exact-bitwise compare, first mesh always uploads).
	RwTexture *elideLastTex0 = NULL;
	bool elideTex0Valid = false;
	bool elideAmbientValid = false;
	float elideAmbientLast[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// ENTRY CONTRACT (test-4 binding fix): same three-layer white-texture
	// flush as the Xbox/PBR cbs — this cb's stage-0 binds ride the same
	// bldBindDiffuse3L path and the same postfx leak class.
	bldEntryContractTex0();

	numMeshes = resEntryHeader->numMeshes;
	while(numMeshes--){
		material = instancedData->material;

		buildingPipe_bindTexStage(material->texture, 0, &elideLastTex0,
		                          &elideTex0Valid, BPELIDE_SPHERE);

		buildingPipe_setUVTransform(material, &ident);

		// FOLIAGE (R2-07): same WIND rule as the PS2/Xbox/PBR cbs. Vertex
		// alpha on a tree is the WIND WEIGHT, not opacity, so the env capture
		// must alpha-TEST foliage with blend OFF like the main pass instead of
		// blending it (which produced translucent/detached leaf cards in the
		// reflection). Only a material alpha may blend.
		if(definedVertexShader == DefinedVertexShader::WIND)
			hasAlpha = instancedData->material->color.alpha != 255;
		else
			hasAlpha = instancedData->vertexAlpha != 0 || instancedData->material->color.alpha != 255;
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);
		// L2 mirror: a cache-only write is applied[]-gated (same desync the
		// other three cbs mirror), so the blend enable actually reaches the
		// device for this mesh.
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE);

		pipeUploadMatCol(flags, material, REG_matCol);

		// c4 ambient: invariant for the whole callback — exact-bitwise skip.
		if(bpipeElideChanged4(elideAmbientValid, elideAmbientLast,
		                       (const float*)&buildingAmbient))
			RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		else
			s_bpipeElideSkip[BPELIDE_SPHERE]++;
		// By-name c20 build (sphereBuildingVS reads only surfAmb=.x, but the
		// raw 3-float RwSurfaceProperties struct would overread .w).
		buildingPipe_uploadSurfProps(material, REG_surfProps);

		// Round-2 bind audit (first 24 meshes, throttled).
		buildingPipe_texDiag(material, elideLastTex0, hasAlpha ? 1 : 0, (int)rs.zwrite);

		D3D9Render(resEntryHeader, instancedData);

		instancedData++;
	}
	// Close the per-mesh blend leak through all three layers (Xbox/PS2 exit
	// idiom), then the tree alpharef push and the callback snapshot.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
	TreeAlpha_RestoreAlphaRef();
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
	bpipeTraceLog("Sphere", BTR_SPHERE, "bpipe_sphere", atomic);	// instrumentation only
	bpipeBlendLeakProbe("Sphere");	// stale-blend leak probe + repair
}

// ============================================================
// PipeChain classify SEH fence — POD-only helper (C2712: __try may not
// coexist with destructible locals, and the PBR cb below holds PF_SCOPE
// PfScope objects). Same choke-point idiom as pipelinecommon.cpp:948 /
// skygfx.h "Class-proof SEH choke-points": the __try body lives in this
// helper so callers with C++ objects in scope keep SEH containment.
//
// The classify bracket (PipeChain_ClassifyBegin -> draw -> End) MUST always
// close: Begin binds the classify RT, forces ZWRITE/blend/fog FALSE through
// layers (1)+(2) and forces PipeChainShader + c0/c1. If the draw faults and
// End never runs, every later building mesh that frame draws into the pack
// buffer with no depth write (ghosting) with the classify shader still
// bound. So End + the pipe-shader restore sit OUTSIDE the __try
// (neoCarpipe.cpp:273 idiom): they run on the normal path AND the
// swallowed-fault path — no exit can skip them.
// ============================================================
static void
buildingPipe_guardedClassifyDraw(RxD3D9ResEntryHeader *resEntryHeader,
                                 RxD3D9InstanceData *instancedData)
{
	__try{
		D3D9Render(resEntryHeader, instancedData);
	}
	__except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("classify_fault"))
			dbglog("[BUILDING] PipeChain classify draw CRASHED exception=0x%08X — closing bracket",
			       GetExceptionCode());
	}
	PipeChain_ClassifyEnd();
	RwD3D9SetPixelShader(buildingPBRPS);	// Begin forced PipeChainShader
}

// TexAlphaClass -> name for the diagnostic below (texalpha.h exports only the
// classifier API). The bare tac NUMBER misled the log reader once: tac=0 is
// TAC_UNKNOWN, not TAC_OPAQUE.
static const char *TexAlphaClassTag(int cls)
{
	switch(cls){
	case TAC_OPAQUE: return "OPAQUE";
	case TAC_CUTOUT: return "CUTOUT";
	case TAC_FADED:  return "FADED";
	default:         return "UNKNOWN";
	}
}

// 1x1 opaque-white stand-in for the DynamicSky capture (PS s3) on INTERIOR
// frames (CGame__currArea != 0). pipeEnsureIBLBuffer() never refreshes g_iblTex
// indoors (pipelinecommon.cpp: indoor early-return), so the stage would
// otherwise hold the LAST OUTDOOR capture — potentially a night/dusk sky. The
// shader's sky term is CPU-gated off indoors (bldEnv.z = 0) and its ambient
// tint weight is 0 (c20.x = 0), but binding a neutral, fully-defined sample
// makes the indoor path immune by construction: every s3 read is
// luminance-neutral and no unbound/undefined sampler state can leak. The
// shader additionally rejects dark captures (BLD_SKY_LUM_FLOOR). D3DPOOL_MANAGED
// survives a device reset, so the single lazily-created texture never goes
// stale. Never used outdoors — the real g_iblTex capture is bound there.
static IDirect3DTexture9 *bldIndoorNeutralTex(IDirect3DDevice9 *dev)
{
	static IDirect3DTexture9 *tex = NULL;
	if(tex || !dev)
		return tex;
	if(FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL)))
		return (tex = NULL);
	D3DLOCKED_RECT lr;
	if(SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))){
		*(DWORD*)lr.pBits = 0xFFFFFFFFu;	// A8R8G8B8 opaque white
		tex->UnlockRect(0);
	}
	return tex;
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	PF_SCOPE("BuildingPipe_PBR");	// instrumentation only
	bTraceBegin(bTrace[BTR_PBR]);	// instrumentation only
	bTraceCur = &bTrace[BTR_PBR];	// instrumentation only
	bpipeElideSummary();	// throttled elision report (previous window)
	// Render IBL cubemap once per frame (sky capture for vehicle environment
	// reflections). Buildings render before vehicles, so this is usually the
	// right time. The frame stamp + camera gate now live in
	// pipeEnsureIBLBuffer (pipelinecommon.cpp) and are SHARED with the
	// vehicle Env cb — the only other g_iblTex consumer — so combos with
	// buildingPipe != PBR (e.g. BUILDING_GTAIV buildings + CAR_MODERN
	// vehicles) still get a rendered IBL, and neither pipe can render it
	// twice in one frame.
	pipeEnsureIBLBuffer();

	// RenderIBLBuffer draws a fullscreen Im2D pass (ImmediateMode
	// Store/Set/ReStore + raw D3DRS writes). Only re-asserting cull left
	// D3DRS_ALPHATESTENABLE / ALPHAREF / blend on the DEVICE free to drift
	// from the rw cache Switch already canonicalised — alpha-cutout road
	// decals then accept their fully-transparent black texels and paint
	// ink blots across the road (PS2/Xbox never call RenderIBLBuffer, so
	// only Building=PBR shows this). Re-force the same dual-layer entry
	// states pipeEnterAlphaMode sets, WITHOUT a nested save/exit (the
	// Switch owns the save). Per-mesh VERTEXALPHAENABLE sets after this
	// still reach the device because the baseline is cache/device aligned.
	{
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

		int aref = 0, afn = 0;
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &aref);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &afn);
		// ref=0 + GREATEREQUAL accepts every texel (same degenerate the
		// pipelinecommon entry repairs) — force ref>=1 and ON.
		if(aref == 0){
			aref = 1;
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)aref);
		}
		// rw -> D3D conversion is pipeAlphaFuncToD3D's job (pipelinecommon);
		// its LUT pins rw NA/garbage to D3DCMP_GREATEREQUAL and is the
		// identity for 1..8 (both enums verified against the headers).
		// `afn` stays untouched in the RW cache on purpose — the
		// pipeForceAlphaBlock() call a few lines below normalises it
		// rw-side as well, so layers (1)/(2)/(3) converge there.
		DWORD d3dfn = (DWORD)pipeAlphaFuncToD3D((RwUInt32)afn);
		RwD3D9SetRenderState(D3DRS_ALPHAREF, (DWORD)aref);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, d3dfn);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

		// Switch forced zwrite TRUE both layers; the IBL pass can leave
		// the raw device side off again.
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	}
	// Re-assert the pipe cull (Im2D fullscreen pass may leave CULLNONE):
	// rw-only was not enough — when the rw cache already said BACK the
	// set no-opped while the device kept the pass's leftover value
	// (double no-op, see pipelinecommon pipeForceCullMode block comment).
	pipeForceCullMode(rwCULLMODECULLBACK);
	// RenderIBLBuffer's Im2D pass also raw-writes the alpha/blend block, and
	// the Switch's repair above ran BEFORE it: re-run it so the IBL pass's
	// leftovers (device alpha test/blend left off, ref left at 0) cannot
	// reach this cb's decal sublayers. PBR never re-asserts alpha itself
	// inside the mesh loop, so this is the last repair point before the
	// draws.
	pipeForceAlphaBlock();

	// Debug logging — wall-clock throttle (PERF R1). The old %300 atomic-count
	// gate counted DRAWS, not frames, and produced 25,886 lines (31% of the
	// log) in a run. Same pattern as vehiclePipe.cpp "vehpbr_env".
	if(dbglog_throttle("pbrbuilding_frame"))
		dbglog("PBRBuilding: frame %d object=%p type=%d flags=0x%X", pbrFrameCount, object, type, flags);

	RpAtomic *atomic = (RpAtomic*)object;

	RwMatrix ident;
	RwMatrixSetIdentity(&ident);

	RwFrame* frame = (RwFrame*)atomic->object.object.parent;
	if(!frame) return;
	if (RWSRCGLOBAL(curCamera)) { _rwD3D9EnableClippingIfNeeded(object, type); }
	// NULL curCamera before first BeginUpdate or during scene teardown causes 0x7FAD4D fault;
	// clipping is meaningless without a camera

	// Transform (WVP + world matrix for PBR)
	float transform[16];
	pipeGetComposedTransformMatrix(atomic, transform);
	// W8-2 THE GAP: buildingPBRVS contains no wind math, so a WIND-flagged
	// atomic rendered static here while the PS2/Xbox pipes swayed it. The
	// CPU-side object-space tilt compensates — applied ONLY when the PBR VS
	// is the one that will bind (the else-branch below falls back to
	// xboxBuildingWindVS, which displaces per-vertex; stacking the two would
	// double the sway — ownership rule in treeWind.h). TreeWind_ApplyToTransform
	// self-guards on the WIND flag: non-wind atomics pay one plugin read.
	// pipeGetWorldMatrix below is NOT affected (the tilt mutates only this
	// composed block, leaving world-space normals/lighting as authored).
	if(buildingPBRVS){
		TreeWind_ApplyToTransform(atomic, transform);
		bTrace[BTR_PBR].tilt = 1;	// instrumentation only: CPU tilt ran
	}
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);

	// World matrix at c24-c27 for buildingPBRVS (world-space normals, positions, view dir)
	float worldMat[16];
	pipeGetWorldMatrix(worldMat);
	RwD3D9SetVertexShaderConstant(24, worldMat, 4);

	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);

	setDnParams(atomic);

	// Eye position (PBR addition) - with null checks like vehicle pipe
	RwV3d eyePos = buildingPipe_getEyePos();
	RwD3D9SetVertexShaderConstant(36, &eyePos, 1);
	RwD3D9SetPixelShaderConstant(2, &eyePos, 1);
	// PS c25-c27 = view rotation rows (unit rows of the world→view rotation,
	// w=0). main_building's ClusterLighting lifts the s6 view-space light
	// positions back to world via eyePos (c2) + these rows, and gates on
	// |rowsLen-3|>0.5 — without this upload the rows are garbage/zero and
	// building forward+ cluster lighting is silently skipped. Same 3-float
	// block / register layout the vehicle pipe uploads (vehiclePipe.cpp:1959).
	{
		D3DMATRIX viewMat;
		RwD3D9GetTransform(D3DTS_VIEW, &viewMat);
		float viewRot[12] = {
			viewMat._11, viewMat._12, viewMat._13, 0.0f,
			viewMat._21, viewMat._22, viewMat._23, 0.0f,
			viewMat._31, viewMat._32, viewMat._33, 0.0f
		};
		RwD3D9SetPixelShaderConstant(25, viewRot, 3);
	}

	// Lights — Force variants bypass rpLIGHTLIGHTATOMICS flag gate
	pipeUploadLightColorForce(pDirect, REG_directCol);
	pipeUploadLightDirectionForce(pDirect, REG_directDir);
	pipeUploadLightColorForcePS(pDirect, REG_directCol);
	pipeUploadLightDirectionForcePS(pDirect, REG_directDir);

	// Clear extra light arrays — D3D9 constants persist across draw calls.
	// Stale values from vehicle pipe or game rendering would be treated as
	// phantom extra lights by the PBR multi-light loop, creating a bright
	// halo that follows the camera.
	static float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for(int i = 0; i < 6; i++){
		RwD3D9SetVertexShaderConstant(REG_directCol + 1 + i, zero4, 1);   // VS c6-c11
		RwD3D9SetVertexShaderConstant(REG_directDir + 1 + i, zero4, 1);   // VS c13-c18
		RwD3D9SetPixelShaderConstant(REG_directCol + 1 + i, zero4, 1);    // PS c6-c11
		RwD3D9SetPixelShaderConstant(REG_directDir + 1 + i, zero4, 1);    // PS c13-c18
	}

	// Env map setup (from Xbox building pipeline)
	RwMatrix envmat;
	CustomBuildingEnvMapPipeline__SetupEnv(atomic, NULL, &envmat);
	RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);

	DefinedVertexShader definedVertexShader = (DefinedVertexShader)TexDefsGetDefinedShader(atomic);

	// Set PBR building shaders
	bool vertexAlphaIsAlpha = true;
	if (buildingPBRVS) {
		if (definedVertexShader == DefinedVertexShader::WIND) {
			vertexAlphaIsAlpha = false;
			setWindParams(atomic, frame);
		}
		RwD3D9SetVertexShader(buildingPBRVS);
		bpipeBindVS(bTrace[BTR_PBR], buildingPBRVS);	// instrumentation only
	} else {
		if (definedVertexShader == DefinedVertexShader::WIND) {
			vertexAlphaIsAlpha = false;
			setWindParams(atomic, frame);
			RwD3D9SetVertexShader(xboxBuildingWindVS);
			bpipeBindVS(bTrace[BTR_PBR], xboxBuildingWindVS);
		} else {
			RwD3D9SetVertexShader(xboxBuildingVS);
			bpipeBindVS(bTrace[BTR_PBR], xboxBuildingVS);
		}
	}
	RwD3D9SetPixelShader(buildingPBRPS);
	bpipeBindPS(bTrace[BTR_PBR], buildingPBRPS);	// instrumentation only

	// Upload ambient color to PS c24 for PBR ambient term (matches vehicle pipe)
	float ambientPS[4] = { buildingAmbient.red, buildingAmbient.green, buildingAmbient.blue, 0.0f };
	RwD3D9SetPixelShaderConstant(24, ambientPS, 1);

	// Universal dynamic-sky ambient weight (PS c20) — shared with the vehicle path.
	// Outdoor-only: zero the sky-hemisphere tint weight indoors (no visible sky).
	float iblAmbient[4] = { (*CGame__currArea == 0) ? config->pbrIblAmbientWeight : 0.0f, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(20, iblAmbient, 1);

	// Bind the dynamic-sky IBL capture to stage 3 (raw D3D9, same as the proven
	// vehicle path). Without this, main_building's iblTex sample reads stale/black
	// state. Stage 0 stays RW-cached, so no rwRENDERSTATETEXTURERASTER resync needed.
	// iblBound feeds the per-mesh bldEnv upload: the sky-reflect term must be
	// 0 whenever s3 is unbound (an unbound sampler reads undefined).
	// INDOOR GATE: indoors g_iblTex is never refreshed (stale outdoor capture, or
	// NULL before the first outdoor frame) — bind the neutral 1x1 white instead
	// so a dark indoor IBL is unreachable. iblBound stays false (bldEnv.z is
	// already 0 indoors via the currArea gate below).
	bool iblBound = false;
	{
		extern IDirect3DTexture9 *g_iblTex;
		IDirect3DDevice9 *dev = d3d9device;
		if(dev && *CGame__currArea != 0){
			IDirect3DTexture9 *neutral = bldIndoorNeutralTex(dev);
			if(neutral){
				if(dbglog_once("bld_ibl_indoor"))
					dbglog("[BldIBL] indoor gate active — s3 neutral bind (currArea=%d, g_iblTex=%p not sampled)",
					       *CGame__currArea, (void*)g_iblTex);
				dev->SetTexture(3, neutral);
				dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
				dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
				dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			}
		}else if(dev && g_iblTex){
			dev->SetTexture(3, g_iblTex);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			iblBound = true;
		}
	}

	// Bind the half-res normal buffer on stage 4 (s4 = normalBufTex in
	// main_building) — mirrors the vehicle pipe. ambientPS[3] gates the shader
	// via c24.w (1.0 when bound, 0.0 otherwise; old shaders ignore the flag).
	// Gates: normalBufferEnable — DrawNormalBufferToTexture early-returns when
	// disabled (postfx.cpp:4426), so g_normalBufferTex may hold stale/garbage
	// content; without the gate main_building lerps N toward random normals
	// → GGX blowout (white ground patches) + glints (foliage sparkle).
	// WIND: foliage has no meaningful screen-space normal (cutout/background
	// shows through), same garbage path — keep c24.w=0 for vegetation.
	// Content: g_normalBufferHasContent is only true after a completed
	// DrawNormalBufferToTexture write — a fresh D3DPOOL_DEFAULT RT holds
	// driver garbage (now also cleared to the invalid-normal marker), and a
	// skipped refresh (menu/shader-missing/depth-missing) leaves the buffer
	// stale. Never bind without real content.
	// TRANSIENT-camRas FIX (wavy floor): the live RwCameraGetRaster read can
	// return a NON-SCREEN raster while the scene pass runs (log evidence:
	// "[FxAlpha] Coronas.Reflections camRas=... 2048x1024 != screen 1920x1080"
	// ×23 — a reflection-class RT left live as the camera raster). Two
	// consumers downstream were poisoned by it:
	//  1. c29 (screenSize) built from the 2048x1024 raster scales VPOS by
	//     1/2048,1/1024 instead of 1/1920,1/1080 → main_building's screenUV
	//     samples the normal buffer at drifting offsets → the SS-normal blend
	//     tilts N per-pixel → BOTH the GGX specular AND the env Fresnel gate
	//     (F_atNdotV) wave → "the whole floor moves like water/lava".
	//  2. s4 itself: a normal buffer bound while the viewport belongs to a
	//     non-screen raster feeds the same wavy N.
	// Fix: derive the screen size from the trusted postfx cache (captured at
	// ColourFilter_switch with the camera provably on the screen — the same
	// source the pipe-chain classify RT and IBL capture use), fall back to the
	// live read only before the first capture, and SKIP the SS-normal path
	// entirely whenever the live camera raster disagrees with the cached
	// screen size (transient window → no stable screenUV → no blend).
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL; // NULL camera → 0x7FAD4D-class fault
	int scrW = 0, scrH = 0;
	bool screenKnown = GetScreenSize(&scrW, &scrH);
	bool camRasIsScreen = camRas && (!screenKnown ||
		(camRas->width == scrW && camRas->height == scrH));
	{
		extern IDirect3DTexture9 *g_normalBufferTex;
		extern bool g_normalBufferHasContent;
		IDirect3DDevice9 *dev = d3d9device;
		// camRasIsScreen gate: during a transient non-screen-raster window the
		// viewport no longer matches the normal buffer's screen frame — skip
		// the bind (c24.w stays 0 → shader skips the SS-normal blend) instead
		// of feeding wavy normals. See the TRANSIENT-camRas FIX comment below.
		if(dev && g_normalBufferTex && g_normalBufferHasContent
		   && config->normalBufferEnable
		   && camRasIsScreen
		   && definedVertexShader != DefinedVertexShader::WIND){
			dev->SetTexture(4, g_normalBufferTex);
			ambientPS[3] = 1.0f;
			RwD3D9SetPixelShaderConstant(24, ambientPS, 1);
		}
		if(screenKnown){
			float sw = (float)scrW, sh = (float)scrH;
			float screenP[4] = { sw, sh, 1.0f/max(sw, 1e-7f), 1.0f/max(sh, 1e-7f) };
			RwD3D9SetPixelShaderConstant(29, screenP, 1);
		}else if(camRas){
			// First-frame fallback only (cache not yet captured).
			float sw = (float)camRas->width, sh = (float)camRas->height;
			float screenP[4] = { sw, sh, 1.0f/max(sw, 1e-7f), 1.0f/max(sh, 1e-7f) };
			RwD3D9SetPixelShaderConstant(29, screenP, 1);
		}
	}

	// Forward+ clustered point lights: bind the tile index texture (s5).
	ForwardPlus_SetConstants();
	bTrace[BTR_PBR].fp = 1;	// instrumentation only: cluster-light upload ran

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);

	// W8-2 tree lane: FF OverrideTreeAlpha alpharef move — works in EVERY
	// pipe, PBR included (buildingPBRPS keeps its own alpha output; the CPU
	// only slides the fixed-function threshold). No tree PS swap here:
	// buildingPBRPS is the lighting shader and is never replaced (the
	// console lighting profile is PS2/Xbox-path only — see treeWind.h).
	TreeAlpha_ApplyAlphaRef(atomic, rs.alpharef);

	// Per-callback decal depth tracking: set when a material-translucent mesh
	// forced ZWRITE off, so the next non-decal mesh gets the callback baseline
	// (ZWRITE TRUE, forced by the Switch guard) back before it draws.
	bool decalZWriteOff = false;

	// Per-mesh write elision (POD locals, reset every callback): sentinels
	// force the first write, textures only re-bind when the RwTexture*
	// changes, and c4 (invariant for the whole callback) is compared
	// exact-bitwise. See bpipeElideSummary for the safety contract.
	int elideLastBlend = -1;
	int elideLastZWrite = -1;
	RwTexture *elideLastTex0 = NULL;
	bool elideTex0Valid = false;
	RwTexture *elideLastTex1 = NULL;
	bool elideTex1Valid = false;
	bool elideNM8SampValid = false;
	bool elideAmbientValid = false;
	float elideAmbientLast[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// ENTRY CONTRACT (test-4 binding fix): flush any screen-family raster a
	// postfx/water pass leaked onto device stage 0 by binding gpWhiteTexture
	// through all three layers BEFORE the first mesh draws. Placed after the
	// IBL render + alpha-state repair above (both can leave rasters/states on
	// the device) and raw-bypasses the elision tracker so the first per-mesh
	// bind still runs full.
	bldEntryContractTex0();

	for(int numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		RpMaterial *material = instancedData->material;
		float colorScale = 1.0f;
		if(material->texture)
			colorScale = config->ps2ModulateBuilding ? 255.0f/128.0f : 1.0f;
		buildingPipe_bindTexStage(material->texture, 0, &elideLastTex0,
		                          &elideTex0Valid, BPELIDE_PBR);

		// Per-texture alpha class: a solid mesh whose texture has an unused
		// alpha channel (canonically empty DXT3 blocks) decodes tex.a~0, and
		// GREATER-EQUAL@1 would discard the whole surface. The class decides
		// the CPU blend enable AND c48.w (main_building's alpha override:
		// 1 = force output alpha 1, 0 = use the texture alpha).
		// config->texAlphaClassify=0 disables the classifier (TAC_UNKNOWN,
		// legacy-hasAlpha decision below); TexAlpha_Classify self-budgets
		// to 256 newly-seen textures per render frame (warm-up ~16 frames
		// for a 4k-texture level).
		//
		// SA paint marker: material->color.alpha == 16 means "this mesh uses
		// the paint colour" — it is NOT a translucency value (same rule as
		// vehiclePipe's matPaintMarker). Feeding it into the alpha decision
		// blended painted meshes with empty texture alpha and made them
		// see-through. Only a real material alpha may force blending.
		const bool matPaintMarker = (material->color.alpha == 16);
		const bool matTranslucent  = (material->color.alpha != 255 && !matPaintMarker);
		// Diagnostic only (reported in [BldOpacity]); excludes the
		// paint marker so the log does not look like a translucent surface.
		// No longer feeds the blend decision — the TAC_UNKNOWN arm below
		// uses the safe matTranslucent default.
		bool hasAlpha;
		if(vertexAlphaIsAlpha)
			hasAlpha = (bool)(instancedData->vertexAlpha || matTranslucent);
		else
			hasAlpha = matTranslucent;

		int tac = (config && config->texAlphaClassify) ? TexAlpha_Classify(material->texture) : TAC_UNKNOWN;
		int blendOn;
		float shaderAlpha;
		// AUDITED mapping (texalpha.h): TAC_UNKNOWN=0, TAC_OPAQUE=1,
		// TAC_CUTOUT=2, TAC_FADED=3. tac=0 in a log line is UNKNOWN, NOT
		// OPAQUE — a budget-skipped / not-yet-classified texture takes
		// the SAFE DEFAULT below (replaces the legacy hasAlpha fallback /
		// parity item 2, same shape as vehiclePipe). TAC_OPAQUE MUST still
		// yield 1.0 or solid meshes with an unused alpha channel (all-zero
		// DXT3 nibbles, DXT1 with unused transparency) are alpha-tested away.
		switch(tac){
		case TAC_OPAQUE:	// no usable alpha data → FORCE output alpha 1
			blendOn = 0; shaderAlpha = 1.0f; break;
		case TAC_CUTOUT:	// hard 0/255 mask → alpha test, no blending
			blendOn = 0; shaderAlpha = 0.0f; break;
		case TAC_FADED:		// intermediate alpha → blend + texture alpha
			blendOn = 1; shaderAlpha = 0.0f; break;
		case TAC_UNKNOWN:	// tac=0 == UNKNOWN → SAFE DEFAULT (see below)
		default:
			// SAFE DEFAULT (unconditional — runs when the classifier is
			// INI-off AND when it budget-skips a texture): opaque geometry
			// draws opaque, only real decal/vedecal materials blend.
			//   blendOn = matTranslucent — vertex alpha alone must never
			//   blend solid geometry. SA world meshes carry vertex alpha
			//   (D/N extra-vert-colour data), so the legacy hasAlpha term
			//   blended terrain/building meshes at texture*vertex alpha
			//   (see-through terrain) AND left them ZWRITE-on while
			//   blending: the decal policy below only parks ZWRITE for
			//   matTranslucent/FADED meshes, so the blended mesh carved
			//   its depth plane and every later draw behind it (peds,
			//   vehicles, markers) depth-rejected. build-10 live log:
			//   [BldTex] green_glass_64 blend=1 zw=1.
			//   shaderAlpha = 1 for the alpha-16 paint marker — the
			//   classified path resolves paint markers to TAC_OPAQUE (the
			//   marker's texture alpha is unused), so c48w=1 is the
			//   DESIGNED result and the INI-off fallback must match it
			//   (the legacy shaderAlpha=0 alpha-tested painted meshes
			//   away with their empty texture alpha). Everything else
			//   keeps texture alpha (shaderAlpha 0) so cutout masks
			//   (fences, railings) keep cutting at the GREATER@ref test.
			//   No per-mesh device reads; the 3-layer blend/ZWRITE writes
			//   and the decal policy below are untouched.
			blendOn = matTranslucent ? 1 : 0;
			shaderAlpha = matPaintMarker ? 1.0f : 0.0f; break;
		}
		// Only a REAL material alpha (never the alpha-16 paint marker) forces
		// blending; shaderAlpha drops to 0 so the blend has texture/vertex
		// alpha to work with (a forced alpha 1 would draw the decal solid).
		if(matTranslucent){
			blendOn = 1;
			shaderAlpha = 0.0f;
		}

		// Classified textures own the blend decision: solid (OPAQUE) and
		// alpha-tested (CUTOUT) textures must never blend just because the
		// mesh carries vertexAlpha — foliage/tree meshes ride this callback
		// and keep alpha TEST (TreeAlpha_ApplyAlphaRef) with blend OFF and
		// ZWRITE ON. Only an explicitly translucent MATERIAL may force blend.
		if(tac == TAC_OPAQUE || tac == TAC_CUTOUT)
			blendOn = matTranslucent ? 1 : 0;

		// FOLIAGE FIX (R2-07): for a WIND atomic vertex alpha is the WIND
		// WEIGHT, not opacity (vertexAlphaIsAlpha is false above). No texture
		// class may switch blending on for a tree unless the MATERIAL itself
		// is translucent — TAC_FADED would otherwise blend leaf cards here
		// while PS2 alpha-tests the same foliage. Keeps alpha TEST with blend
		// OFF and ZWRITE ON on every WIND mesh.
		if(!vertexAlphaIsAlpha)
			blendOn = matTranslucent ? 1 : 0;

		// DECAL depth policy (user m0452): a mesh that blends because of TRUE
		// material translucency, OR a FADED soft decal mask (texture-alpha
		// decals — material alpha 255), is an overlay — decals, road
		// sublayers, shadows — and must not write depth or it carves
		// itself into the depth buffer. ZTEST stays TRUE (never disable it):
		// the uneven depth buffer already holds CJ and the world, so a nearer
		// CJ depth-rejects the decal (decal over buildings, never over CJ)
		// while the decal still passes where it really is in front. All three
		// layers are written (rw cache / D3D9 driver cache / raw device)
		// because a cache-only write is applied[]-gated and can leave the
		// device writing depth. Exit restoration is covered twice:
		// buildingPipe_restoreRenderState(&rs) restores the rw cache (L1) and
		// the Switch ZWRITE guard restores driver/device (L2/L3) to their
		// pre-callback values.
		const bool decalBlend = (blendOn != 0) && (matTranslucent || tac == TAC_FADED);
		if(decalBlend){
			// Consecutive decal meshes share the same three-layer FALSE
			// burst — skip it when THIS callback already pushed it.
			if(elideLastZWrite != 0){
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				if(d3d9device)
					d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				elideLastZWrite = 0;
			}else
				s_bpipeElideSkip[BPELIDE_PBR] += 3;
			decalZWriteOff = true;
		}else if(decalZWriteOff){
			// Next mesh is not a decal: hand the callback baseline (ZWRITE
			// TRUE, forced by the Switch guard) back before its draw.
			if(elideLastZWrite != 1){
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				if(d3d9device)
					d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				elideLastZWrite = 1;
			}else
				s_bpipeElideSkip[BPELIDE_PBR] += 3;
			decalZWriteOff = false;
		}

		// NM lane: bind <diffuse>_n on s8 (raw D3D9) + c48 = {bound, intensity,
		// glossFromAlpha, shaderAlpha}. Buildings are never player-scoped
		// (normalMapPlayerOnly gates vehicles only). The explicit else-reset is
		// REQUIRED — D3D9 constants and textures persist across draws, so a
		// mesh without a pairing must zero c48.x/y/z and unbind s8 or it would
		// sample the previous mesh's normal map. c48.w must be written on BOTH
		// paths — the else branch used to write 0.0f, which under
		// normalMapEnable=0 would silently force main_building's alpha override
		// to 0 (texture alpha) for every mesh, leaving solid DXT3 surfaces
		// hole-prone again (the #1 trap for this fix).
		{
			bool nmAlpha = false;
			IDirect3DTexture9 *nmD3d =
				(config->normalMapEnable && !gHasExternalNormalMapPlugin)
				? TexDefsGetNormalMap(material->texture, &nmAlpha) : NULL;
			if(nmD3d && d3d9device){
				d3d9device->SetTexture(8, nmD3d);
				// s8 sampler block is a CONSTANT (WRAP/LINEAR) for the whole
				// callback — invariant class: push it on the first NM mesh
				// only. The per-mesh SetTexture above stays (it changes).
				if(!elideNM8SampValid){
					d3d9device->SetSamplerState(8, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
					d3d9device->SetSamplerState(8, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
					d3d9device->SetSamplerState(8, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					d3d9device->SetSamplerState(8, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
					d3d9device->SetSamplerState(8, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
					elideNM8SampValid = true;
				}else
					s_bpipeElideSkip[BPELIDE_PBR] += 5;
				float nmP[4] = { 1.0f, config->normalMapIntensity, nmAlpha ? 1.0f : 0.0f, shaderAlpha };
				RwD3D9SetPixelShaderConstant(48, nmP, 1);
			}else{
				float nmZero[4] = { 0.0f, 0.0f, 0.0f, shaderAlpha };
				RwD3D9SetPixelShaderConstant(48, nmZero, 1);
				if(d3d9device)
					d3d9device->SetTexture(8, NULL);
			}
		}
		RwD3D9SetPixelShaderConstant(0, &colorScale, 1);
		RwD3D9SetVertexShaderConstant(REG_shaderParams, &colorScale, 1);

		// UV transform support
		buildingPipe_setUVTransform(material, &ident);

		// Vertex alpha handling: the per-TEXTURE class owns blendOn for
		// OPAQUE/CUTOUT/FADED; TAC_UNKNOWN uses the SAFE DEFAULT
		// (matTranslucent-only blend, same contract as the vehicle Env cb).
		// L1+L2 blend pair — elided only when the previous mesh in THIS
		// callback already pushed the same value. Sentinel -1 forces the
		// first write; D3D9RenderDual and the PipeChain bracket both restore
		// this pair balanced, so the tracker cannot go stale.
		if(blendOn != elideLastBlend){
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)blendOn);
			// Layer-(2) mirror — same desync as vehiclePipe_setupMaterial: the rw
			// set no-ops against an unchanged cache and leaves D3DRS_ALPHABLENDENABLE
			// at the Switch's FALSE baseline, so the opaque path's D3D9RenderDual
			// gate reads blend-off and drops the dual pass. Force L2 so the
			// per-mesh alpha actually reaches the gate and the device flush.
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendOn ? TRUE : FALSE);
			elideLastBlend = blendOn;
		}else
			s_bpipeElideSkip[BPELIDE_PBR] += 2;

		// One-shot opacity diagnostic (first 16 meshes): texture, class,
		// legacy hasAlpha, the final blend decision, the c48.w alpha override
		// written by the NM block ABOVE (same shaderAlpha variable / same
		// write, so the line reports the value actually uploaded and nothing
		// later in this loop rewrites c48) and zw (1 = normal depth write,
		// 0 = material-translucent decal with ZWRITE forced off). Class NAME
		// is printed because tac=0 is TAC_UNKNOWN, not TAC_OPAQUE. legacy=1
		// marks a mesh whose decision came from the TAC_UNKNOWN SAFE DEFAULT
		// (matTranslucent blend + paint-marker opaque override) and makes
		// parity-build runs distinguishable in the log.
		if(s_bldMeshLogged < 16){
			s_bldMeshLogged++;
			dbglog("[BldOpacity] tex='%s' tac=%d(%s) hasAlpha=%d blendOn=%d c48w=%.2f matA=%d zw=%d legacy=%d",
			       (material->texture && material->texture->name[0]) ? material->texture->name : "",
			       tac, TexAlphaClassTag(tac), hasAlpha ? 1 : 0, blendOn,
			       shaderAlpha, material->color.alpha, decalBlend ? 0 : 1,
			       tac == TAC_UNKNOWN ? 1 : 0);
		}
		// Round-2 bind audit (first 24 meshes, throttled).
		buildingPipe_texDiag(material, elideLastTex0, blendOn, decalBlend ? 0 : 1);

		// c4 ambient: invariant for the whole callback — exact-bitwise skip.
		if(bpipeElideChanged4(elideAmbientValid, elideAmbientLast,
		                       (const float*)&buildingAmbient))
			RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		else
			s_bpipeElideSkip[BPELIDE_PBR]++;

		// Material color and surface properties
		if(flags & rpGEOMETRYLIGHT){
			pipeUploadMatCol(flags, material, REG_matCol);
			// c20 layout for buildingPBRVS: x=ambient scale, y=dynamic-light
			// scale, z=prelight scale (float4 upload — the old
			// RwSurfaceProperties{3 floats} upload read 4 bytes past the
			// struct for the 4th component).
			//
			// SA assets commonly ship diffuse=0 (lighting baked into vertex
			// colours; ps2BuildingVS never uses surfDiff) → surfDiff=0 zeroes
			// the prelight term → black silhouette trees/palms/decals. The old
			// blanket fallback (diffuse → 1.0) boosted the prelight AND the
			// 7-direct-light sum to full, stacking live sun ON TOP of the
			// baked prelight lighting → ~2x energy → white blown-out
			// roads/terrain. Fix: boost the PRELIGHT scale only and keep the
			// dynamic-light scale at the material's original diffuse (~0 for
			// baked-lighting assets) → prelight + ambient, no double sun —
			// exactly the fallback's documented "PS2-equivalent" intent
			// (ps2BuildingVS = prelight + ambient*surfAmb, no light sum).
			// Materials with a real diffuse value are a complete no-op.
			RwSurfaceProperties surf = material->surfaceProps;
			float surfUpload[4] = { surf.ambient, surf.diffuse, surf.diffuse, 0.0f };
			if(surf.diffuse < 1e-4f)
				surfUpload[2] = 1.0f; // prelight passes at full strength
			RwD3D9SetVertexShaderConstant(REG_surfProps, surfUpload, 1);
		}else{
			static float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			static float surfW[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
			RwD3D9SetVertexShaderConstant(REG_matCol, white, 1);
			RwD3D9SetVertexShaderConstant(REG_surfProps, surfW, 1);
		}

		// ---- ENVIRONMENT REFLECTION (ported from the IV / PS2 / Xbox cbs) ----
		// PS2 :683 and Xbox :809 both branch on the envmap material flag and
		// run an additive Envcolor = 192/128 * shininess pass; the GTA IV path
		// inherits that branch (it is the PS2 cb with an IV shader swap), so
		// IV/PS2/Xbox all reflect and the PBR cb — which had NO branch here —
		// rendered those materials flat. Bind the material env map on s1 and
		// hand the shader the exact Envcolor weight those paths use.
		// Non-env materials still get a DEFINED s1 sample (the IV callback's
		// own trick — GTAIVBuilding bind of the diffuse at :655 — so a
		// speculatively-hoisted tex2D can never read an unbound stage; the
		// shader additionally rejects empty samples). s1 is released by
		// buildingPipe_cleanup() on cb exit.
		float envWeight = 0.0f;
		if(config->bldEnvReflect && (*(int*)&material->surfaceProps.specular & 1)){
			CustomEnvMapPipeMaterialData *envMat =
				*RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material,
				                CCustomCarEnvMapPipeline__ms_envMapPluginOffset);
			// NULL-guard + REAL-texture guard: the PS2/Xbox cbs dereference this
			// unchecked — an envmap-flagged material whose plugin was never
			// allocated, or whose texture slot holds a raster-less entry (an
			// uninitialised plugin slot), must NOT be bound and sampled. A
			// garbage s1 texel added at Envcolor weight renders the mesh flat.
			// Require a real texture WITH a valid raster; otherwise fall through
			// to the non-env path below (envWeight stays 0 -> no env sample).
			if(envMat && envMat->texture && envMat->texture->raster){
				// RASTER-CLASSIFICATION GUARD (wavy floor, defence-in-depth):
				// s1 must ONLY ever hold a STATIC env texture from the asset
				// (rwRASTERTYPENORMAL / rwRASTERTYPETEXTURE rasters). A
				// CAMERATEXTURE/ZBUFFER/CAMERA raster here is a render-target
				// (envFB sphere-capture, Coronas reflection RT, pipe-chain pack
				// buffer — anything the envmap/water lanes swap through the
				// plugin slot): sampling it binds ANIMATED screen-space content
				// into the building reflection → the floor "waves like water".
				// Never bind an RT-class raster on s1; fall through to the
				// non-env path (envWeight stays 0, s1 gets the diffuse).
				RwUInt32 envRasType = envMat->texture->raster->cType & rwRASTERTYPEMASK;
				if(envRasType == rwRASTERTYPENORMAL || envRasType == rwRASTERTYPETEXTURE){
				// s1: only re-bind when the RwTexture* actually changes
				// (consecutive meshes often share one env map / diffuse).
				// Resolved-pointer key: a NULL env texture resolves to
				// gpWhiteTexture, the defined neutral the PS's env gate
				// expects, never a stale previous s1 raster.
				buildingPipe_bindTexStage(envMat->texture, 1, &elideLastTex1,
				                          &elideTex1Valid, BPELIDE_PBR);
				// Envcolor weight. PS2/Xbox add `192/128*shininess` (=1.5*) to a
				// FLAT `prelight*surfDiff + ambient*surfAmb` pass (ps2BuildingFxVS:31)
				// where the additive IS the only bright term. The PBR base is
				// ALREADY fully lit (ambient + prelight + live sun + GGX specular
				// + IBL), so the same full additive double-counts the reflection
				// energy and saturates env-mapped materials to white — the "all
				// buildings blank/white" regression. Keep the PS2 formula shape
				// (scaled by shininess) but cap it to a Fresnel-average broad-
				// reflection level that sits on the lit base without pushing it
				// to 1.0: low-shininess materials keep near-PS2 strength, only
				// the high-shininess ones that drove `base + 0.75*envSample > 1`
				// are reined in. The sharp reflection already lives in the base's
				// GGX specular; this term is only the broad env wash.
				float envShine = envMat->GetShininess();	// 0..1 (shininess/255)
				envWeight = 1.5f * envShine;	// PS2/Xbox Envcolor scale (192/128 == 1.5)
				if(envWeight > 0.20f) envWeight = 0.20f;
				} // end raster-classification guard
			}
		}
		if(envWeight <= 0.0f){
			// Same resolved-pointer elision as the env branch above; a NULL
			// material texture binds gpWhiteTexture, never NULL.
			buildingPipe_bindTexStage(material->texture, 1, &elideLastTex1,
			                          &elideTex1Valid, BPELIDE_PBR);
		}
		// Sky term only for materials WITHOUT their own env map, and only when
		// s3 really holds the IBL capture (the PS's validity gate is the second
		// line of defence against a dead sample).
		// Outdoor-only: indoors (CGame__currArea != 0) the sky capture is
		// dark/meaningless, so suppress the sky-reflect term there.
		float skyRefl = (config->bldEnvReflect && envWeight <= 0.0f && iblBound
		                && *CGame__currArea == 0)
		                ? config->bldSkyReflect : 0.0f;
		float bldEnvP[4] = { config->bldEnvReflect ? 1.0f : 0.0f, envWeight, skyRefl, 0.0f };
		RwD3D9SetPixelShaderConstant(47, bldEnvP, 1);

		// PBR material params (c22/c23)
		int surfaceType = GetSurfaceTypeFromMaterial(material);
		const BRDFMaterial *brdf = GetBRDF(surfaceType);
		pipeUploadPBR(brdf->glossiness, brdf->specular, brdf->clearcoat, brdf->subsurface,
		              brdf->specularTintR, brdf->specularTintG, brdf->specularTintB);

		// Tag rendering support
		if(material->pipeline == (RxPipeline*)TagRenderCB){
			TagRenderCB(atomic, resEntryHeader, instancedData);
			continue;
		}

		// Dual-pass parity with PS2/Xbox (dualPassBuilding=1 in skygfx.ini):
		// hasAlpha+zwrite road decals need the zwriteThreshold cutout pair —
		// plain D3D9Render was the only building pipe without it, so PBR
		// alone showed black alpha-blend artifacts on road sublayers.
		// texinfo: per-texture dualPass opt-outs (dualPass=0) and
		// zwriteThreshold from texdb.txt — both PS2 (:439) and Xbox (:590)
		// already pass it; NULL-safe (falls back to the default/fallback texinfo, dualPass=1).
		TexInfo *texinfo = TexDefsGetInfo(material->texture);
		D3D9RenderDual(config->dualPassBuilding, resEntryHeader, instancedData, texinfo);

		// PipeChain classify: second draw of this mesh into the full-res pack
		// buffer (PipeChainShader forced, c0.x=9 pack mode). Runs AFTER the
		// main draw so ZTEST sees the real depth (same Z, LESSEQUAL passes);
		// Begin keeps ZWRITE off, so the depth buffer is untouched. Values
		// are the same brdfLibrary ones pipeUploadPBR sent in c22/c23.
		{
			if(PipeChain_ClassifyBegin((float)surfaceType, brdf->glossiness, brdf->specular, 0.0f)){
				// SEH-fenced draw; End + pipe-shader restore run inside the
				// helper on EVERY exit path (normal + swallowed fault), so
				// the bracket always closes — see
				// buildingPipe_guardedClassifyDraw above.
				buildingPipe_guardedClassifyDraw(resEntryHeader, instancedData);
			}
		}
	}

	// W8-2: close the tree alpharef push before the cb-entry restore.
	TreeAlpha_RestoreAlphaRef();
	buildingPipe_restoreRenderState(&rs);

	// PBR cb never called buildingPipe_cleanup (PS2 :447 / Xbox :561 do)
	// and additionally raw-binds s3 (g_iblTex), s4 (normal buffer) and s5
	// (forward+ tile). Leave a clean handoff: vehicles rebind what they
	// need, but FFP/HUD/particle draws between the building and vehicle
	// passes must not keep sampling the PBR samplers or a stage1 colour
	// op the env/Xbox passes may have left enabled from an earlier pipe.
	buildingPipe_cleanup();          // s1/s2 neutral bind + stage1/2 ops DISABLE
	// (s2 is owned by cleanup now — a NULL write here would undo the
	// gpWhiteTexture handoff and, being applied[]-gated, might not reach the
	// device anyway.)
	RwD3D9SetTexture(NULL, 3);
	RwD3D9SetTexture(NULL, 4);
	RwD3D9SetTexture(NULL, 5);
	RwD3D9SetTexture(NULL, 8);	// NM lane: s8 (<diffuse>_n) bound raw per mesh
	// s3/s4/s5 were bound RAW above (dev->SetTexture — g_iblTex, the normal
	// buffer, the forward+ tile), so RW's per-stage texture cache for them can
	// still be NULL while the DEVICE still holds the texture: the
	// RwD3D9SetTexture(NULL, n) calls above then no-op (cache already NULL)
	// and the handoff leaves the IBL cubemap / half-res normal buffer bound on
	// stages the next pass may rebind as render targets (feedback lock /
	// black read). rw-set first keeps the cache NULL, the raw call then
	// guarantees the device side regardless of what the cache thought.
	if(d3d9device){
		d3d9device->SetTexture(3, NULL);
		d3d9device->SetTexture(4, NULL);
		d3d9device->SetTexture(5, NULL);
		d3d9device->SetTexture(8, NULL);	// NM lane: s8 same two-step
		// HIGH-5: these raw SetSamplerState calls were OUTSIDE the guard
		// (null-deref on a lost device). Reset s3-s5 addressing the vehicle
		// cleanup also resets (CLAMP/POINT left behind would stick on the
		// next consumer of those stages).
		d3d9device->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		d3d9device->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		d3d9device->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	}
	// Stage2 ops: PBR never enables them, but an inherited MULTIPLYADD
	// from a prior pipeline would blend against an undefined texel.
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	// Instrumentation only: report while the trace still holds the last real
	// bind (the NULL clears below are exits, not binds).
	bpipeTraceLog("PBR", BTR_PBR, "bpipe_pbr", atomic);
	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
	bpipeBlendLeakProbe("PBR");	// stale-blend leak probe + repair
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
//	if(GetAsyncKeyState(VK_F4) & 0x8000)
//		return;

	RwUInt32 savedCull = pipeEnterCullMode();
	// Hand the raw pre-entry cull to the Xbox cb's draw-boundary re-assert
	// (pipeForceCullMode sanitizes out-of-domain values itself, so the mirror
	// pass's CULLNONE reaches the assert intact).
	s_bldSwitchSavedCull = savedCull;
	PipeAlphaState savedAlpha = pipeEnterAlphaMode();
	// ZWRITE three-layer guard — mirrors vehiclePipe.cpp:2570. The alpha guard
	// above covers blend/alpha-test but NOT zwrite. postfx raw
	// D3DRS_ZWRITEENABLE writes (postfx.cpp:3451, 3630, 3703, 3741, 3791,
	// 5140) leave the DEVICE with zwrite off while the rw cache still says
	// TRUE (its rw-side restore no-ops against an unchanged cache), so the
	// building cbs — which read rw (buildingPipe_saveRenderState,
	// D3D9RenderDual) and never force the device side — draw with no depth
	// writes: near geometry doesn't occlude far (far drawn later overwrites
	// it), vegetation/skidmarks drawn after the buildings pass depth-test
	// against an empty buffer and paint over them. PBR is worst hit because
	// it uses plain D3D9Render (no dual-pass ZWRITE roundtrip to
	// accidentally repair the device like PS2's hasAlpha path does). Force
	// TRUE through ALL THREE layers (rw cache / D3D9 driver cache / raw
	// device) for the duration of the cb; exit restores the per-layer capture
	// when the entry was clean, or the canonical TRUE when it was desynced
	// (postfx manages the raw device side itself with its own save/restore —
	// syncing the sides here would fight that contract).
	RwBool savedZWriteRw = TRUE;
	DWORD savedZWriteDrv = TRUE;
	DWORD savedZWriteDev = TRUE;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWriteRw);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDrv);
	if(d3d9device)
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDev);
	// Sticky-repair latch (ped path, main.cpp:1738-1743): the entry capture
	// is often already polluted, so replaying it verbatim at exit would
	// faithfully re-emit the leak and make it permanent. Treat any captured
	// layer of 0 as desynced and restore the canonical TRUE instead.
	bool zwriteWasDesync = (!savedZWriteRw || !savedZWriteDrv || !savedZWriteDev);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

	// Non-screen-raster probe — geometry half of the blank-world hunt
	// (main.cpp:1570 m0170/m0171 documents the failure class: a mid-frame
	// RwCameraSetRaster swap makes the SCENE render into a non-screen raster
	// so "the screen never receives the world ... only effects-phase sprites
	// on top"). Current skygfx_dbg.log shows camRas is ALWAYS the screen at
	// scene entry ([RENDER] scene-entry probe: 0 hits) and at postfx copy
	// (UpdateFrontBuffer SKIP: 0 hits), but a 2048x1024 raster IS live at the
	// effects phase ([FxAlpha] Coronas.Reflections camRas=... 2048x1024 x23,
	// pointer recycled 1D59F1A8 -> 1D5B0790 -> 272837A0). The open question
	// that splits the suspect list is whether GEOMETRY ever sees it too:
	//   hit here  -> the world really is drawn into the foreign raster (the
	//                covered-display root cause); correlate this pointer with
	//                the [FxAlpha] one to name the owning pass;
	//   no hit    -> the swap is strictly post-geometry and the owner lives
	//                in the effects phase, not in any pipe.
	// Mismatch-only + throttled: zero cost when camRas is the screen.
	if(dbglog_throttle("bld_camras")){
		RwRaster *cbRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(cbRas && RsGlobal && cbRas->width > 0 &&
		   (cbRas->width != (int)RsGlobal->MaximumWidth ||
		    cbRas->height != (int)RsGlobal->MaximumHeight))
			dbglog("[BUILDING] cb camRas=%p %dx%d != screen %dx%d — non-screen raster LIVE during building cb",
			       (void*)cbRas, cbRas->width, cbRas->height,
			       (int)RsGlobal->MaximumWidth, (int)RsGlobal->MaximumHeight);
	}

	// Three-layer alpha/blend repair — pipeEnterAlphaMode canonicalised the
	// rw + driver caches above, but its RwD3D9SetRenderState writes are
	// applied[]-gated and cannot repair a DEVICE that drifted behind an
	// unchanged cache (postfx raw restores, the Im2D pass). Every building
	// pipe (PS2/GTAIV/Xbox/PBR/Sphere) draws its alpha-cutout decal
	// sublayers from this device state for the WHOLE atomic, so repair it
	// before dispatch — see pipeForceAlphaBlock. savedAlpha (captured at
	// entry, BEFORE pipeEnterAlphaMode canonicalised anything) lets the
	// repair's throttled log tell entry-canonicalisation LAG (device still
	// holds the pre-entry cache value; the next flush delivers the canonical
	// one anyway) apart from genuine layer-(3) drift, so the no-op lag
	// stops spamming "[BUILDING] drift repaired" every window.
	pipeForceAlphaBlock(&savedAlpha);

	if(gRenderingSpheremap)
		CCustomBuildingDNPipeline__CustomPipeRenderCB_Sphere(repEntry, object, type, flags);
	else switch(config->buildingPipe){
	default:
	case BUILDING_PS2:
	case BUILDING_GTAIV:  // GTAIV = PS2 callback + IV shader swap
	                         // (ivMode || buildingPipe==BUILDING_GTAIV —
	                         // parity with vehiclePipe.cpp:690)
		CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		break;
	case BUILDING_XBOX:
		CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox(repEntry, object, type, flags);
		break;
	case BUILDING_PBR:
		CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR(repEntry, object, type, flags);
		break;
	}
	pipeExitAlphaMode(savedAlpha);
	// Exit-contract hardening — INTEGRATED with the alpha restore above (no
	// duplicate restore path): pipeExitAlphaMode writes the saved alpha state
	// through the caches, and its cache-hit skip can leave the RAW device
	// holding blend-off+ONE/ONE (the corona-breaking signature) even after
	// the inner-cb probes repaired it. Probe + repair AFTER the restore so no
	// building-cb exit can leave the signature behind.
	bpipeBlendLeakProbe("Switch");
	// Restore zwrite. A desynced entry (any layer captured 0) gets the
	// canonical TRUE forced through all three layers instead of replaying the
	// polluted capture; a clean entry restores each layer verbatim (postfx
	// owns the raw device side with its own save/restore). Single throttled
	// bld_zw receipt (~2 s) for this wrapper.
	if(dbglog_throttle("bld_zw"))
		dbglog("[BldZW] entry rw=%d drv=%d dev=%d -> restore=%s",
			(int)savedZWriteRw, (int)savedZWriteDrv, (int)savedZWriteDev,
			zwriteWasDesync ? "TRUE(canonical)" : "verbatim");
	if(zwriteWasDesync){
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	}else{
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDrv);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDev);
	}
	pipeExitCullMode(savedCull);
	fixSAMP();
}

static RwBool
reinstance(void *object, RwResEntry *resEntry, RxD3D9AllInOneInstanceCallBack instanceCallback)
{
	return (instanceCallback == NULL ||
	        instanceCallback(object, (RxD3D9ResEntryHeader*)(resEntry + 1), true) != 0);
}

bool
isNightColorZero(RwRGBA *rgbac, int nv)
{
	RwUInt32 *c = (RwUInt32*)rgbac;
	while(nv--)
		if(*c++)
			return false;
	return true;
}

RwRGBA*
GetVertColourPtr(RpGeometry *geometry)
{
	return *RWPLUGINOFFSET(RwRGBA*, geometry, CCustomBuildingDNPipeline__ms_extraVertColourPluginOffset+4);
}

RwRGBA*
GetExtraVertColourPtr(RpGeometry *geometry)
{
	return *RWPLUGINOFFSET(RwRGBA*, geometry, CCustomBuildingDNPipeline__ms_extraVertColourPluginOffset);
}

RwRGBA*
GetInstVertexColors(RpGeometry *geometry)
{
	RwRGBA *day = GetVertColourPtr(geometry);
	if(day == NULL)
		return geometry->preLitLum;
	return day;
}

RwRGBA*
GetInstExtraColors(RpGeometry *geometry)
{
	RwRGBA *night = GetExtraVertColourPtr(geometry);
	if(night && isNightColorZero(night, geometry->numVertices))
		return NULL;
	return night;
}

void
instWhite(RwUInt8 *mem, RwInt32 numVerts, RwUInt32 stride)
{
	while(numVerts--){
		*(D3DCOLOR*)mem = 0xFFFFFFFF;
		mem += stride;
	}
}

RwBool
DNInstance_PS2(void *object, RxD3D9ResEntryHeader *resEntryHeader, RwBool reinstance)
{
	RpAtomic *atomic;
	RpGeometry *geometry;
	RpMorphTarget *morphTarget;
	RwBool isTextured, isPrelit, hasNormals;
	D3DVERTEXELEMENT9 dcl[6];
	void *vertexBuffer;
	RwUInt16 stride;
	RxD3D9InstanceData *instData;
	IDirect3DVertexBuffer9 *vertBuffer;
	int i, j;
	int numMeshes;
	void *vertexData;
	int lastLocked;
	uint32 pipeID;
	RwRGBA *day, *night;

	atomic = (RpAtomic*)object;
	pipeID = GetPipelineID(atomic);
	geometry = atomic->geometry;
	isTextured = (geometry->flags & (rpGEOMETRYTEXTURED|rpGEOMETRYTEXTURED2)) != 0;
	morphTarget = geometry->morphTarget;
	isPrelit = geometry->flags & rpGEOMETRYPRELIT;
	hasNormals = geometry->flags & rpGEOMETRYNORMALS;
	day = GetInstVertexColors(geometry);
	/* If the non-DN pipe was forced (as we do with tags),
	 * ignore extra colours that might be there */
	if(pipeID == RSPIPE_PC_CustomBuildingDN_PipeID)
		night = GetInstExtraColors(geometry);
	else
		night = nil;
	if(reinstance){
		IDirect3DVertexDeclaration9 *vertDecl = (IDirect3DVertexDeclaration9*)resEntryHeader->vertexDeclaration;
		vertDecl->GetDeclaration(dcl, (UINT*)&i);
	}else{
		resEntryHeader->totalNumVertex = geometry->numVertices;
		vertexBuffer = resEntryHeader->vertexStream[0].vertexBuffer;
		if(vertexBuffer){
			RwD3D9DestroyVertexBuffer(resEntryHeader->vertexStream[0].stride,
			                          geometry->numVertices * resEntryHeader->vertexStream[0].stride,
			                          vertexBuffer,
			                          resEntryHeader->vertexStream[0].offset);
			resEntryHeader->vertexStream[0].vertexBuffer = NULL;
		}
		resEntryHeader->vertexStream[0].offset = 0;
		resEntryHeader->vertexStream[0].managed = 0;
		resEntryHeader->vertexStream[0].dynamicLock = 0;
		dcl[0] = {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0};
		i = 1;
		stride = 12;
		resEntryHeader->vertexStream[0].stride = stride;
		resEntryHeader->vertexStream[0].geometryFlags = rpGEOMETRYLOCKVERTICES;
		if(isTextured){
			dcl[i++] = {0, stride, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0};
			stride += 8;
			resEntryHeader->vertexStream[0].stride = stride;
			resEntryHeader->vertexStream[0].geometryFlags |= rpGEOMETRYLOCKTEXCOORDS;
		}
		if(hasNormals){
			dcl[i++] = {0, stride, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0};
			stride += 12;
			resEntryHeader->vertexStream[0].stride = stride;
			resEntryHeader->vertexStream[0].geometryFlags |= rpGEOMETRYLOCKNORMALS;
		}
		if(isPrelit){
			dcl[i++] = {0, stride, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0};
			stride += 4;
			dcl[i++] = {0, stride, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1};
			stride += 4;
			resEntryHeader->vertexStream[0].stride = stride;
			resEntryHeader->vertexStream[0].geometryFlags |= rpGEOMETRYLOCKPRELIGHT;
		}else{
			// we need vertex colors in the shader so force white like on PS2, one set is enough
			dcl[i++] = {0, stride, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0};
			stride += 4;
			resEntryHeader->vertexStream[0].stride = stride;
			resEntryHeader->vertexStream[0].geometryFlags |= rpGEOMETRYLOCKPRELIGHT;
		}
		dcl[i] = D3DDECL_END();

		if(!RwD3D9CreateVertexDeclaration(dcl, &resEntryHeader->vertexDeclaration))
			return 0;
		resEntryHeader->vertexStream[0].managed = 1;
		if(!RwD3D9CreateVertexBuffer(resEntryHeader->vertexStream[0].stride,
		                             resEntryHeader->vertexStream[0].stride * resEntryHeader->totalNumVertex,
		                             (void**)resEntryHeader->vertexStream,
		                             &resEntryHeader->vertexStream[0].offset))
			return 0;

		numMeshes = resEntryHeader->numMeshes;
		instData = (RxD3D9InstanceData*)(resEntryHeader+1);
		while(numMeshes--){
			instData->baseIndex += instData->minVert + resEntryHeader->vertexStream[0].offset/resEntryHeader->vertexStream[0].stride;
			instData++;
		}
	}

	vertBuffer = (IDirect3DVertexBuffer9*)resEntryHeader->vertexStream[0].vertexBuffer;
	lastLocked = reinstance ? geometry->lockedSinceLastInst : rpGEOMETRYLOCKALL;
	if(lastLocked == rpGEOMETRYLOCKALL || resEntryHeader->vertexStream[0].geometryFlags & lastLocked){
		vertBuffer->Lock(resEntryHeader->vertexStream[0].offset,
		                 resEntryHeader->totalNumVertex * resEntryHeader->vertexStream[0].stride,
		                 &vertexData, D3DLOCK_NOSYSLOCK);
		if(vertexData){
			if(lastLocked & rpGEOMETRYLOCKVERTICES){
				for(i = 0; dcl[i].Usage != D3DDECLUSAGE_POSITION || dcl[i].UsageIndex != 0; ++i)
					;
				_rpD3D9VertexDeclarationInstV3d(dcl[i].Type, (RwUInt8*)vertexData + dcl[i].Offset,
					morphTarget->verts,
					resEntryHeader->totalNumVertex,
					resEntryHeader->vertexStream[dcl[i].Stream].stride);
			}
			if(isTextured && (lastLocked & rpGEOMETRYLOCKTEXCOORDS)){
				for(i = 0; dcl[i].Usage != D3DDECLUSAGE_TEXCOORD || dcl[i].UsageIndex != 0; ++i)
					;
				_rpD3D9VertexDeclarationInstV2d(dcl[i].Type, (RwUInt8*)vertexData + dcl[i].Offset,
					(RwV2d*)geometry->texCoords[0],
					resEntryHeader->totalNumVertex,
					resEntryHeader->vertexStream[dcl[i].Stream].stride);
			}
			if(hasNormals && (lastLocked & rpGEOMETRYLOCKNORMALS)){
				for(i = 0; dcl[i].Usage != D3DDECLUSAGE_NORMAL || dcl[i].UsageIndex != 0; ++i)
					    ;
				_rpD3D9VertexDeclarationInstV3d(dcl[i].Type, (RwUInt8*)vertexData + dcl[i].Offset,
					morphTarget->normals,
					resEntryHeader->totalNumVertex,
					resEntryHeader->vertexStream[dcl[i].Stream].stride);
			}
if(isPrelit && (lastLocked & rpGEOMETRYLOCKPRELIGHT)){
			for(i = 0; dcl[i].Usage != D3DDECLUSAGE_COLOR || dcl[i].UsageIndex != 0; ++i)
				;
			// Scan for COLOR1 with bounds check (D3DDECL_END has Stream==0xFF)
			int color1Idx = -1;
			for(j = 0; dcl[j].Stream != 0xFF; ++j){
				if(dcl[j].Usage == D3DDECLUSAGE_COLOR && dcl[j].UsageIndex == 1){
					color1Idx = j;
					break;
				}
			}
			// If no extra colors, use regular colors
			if(night == NULL)
				night = day;
			assert(day);
			assert(night);
			numMeshes = resEntryHeader->numMeshes;
			instData = (RxD3D9InstanceData*)(resEntryHeader+1);
			while(numMeshes--){
				instData->vertexAlpha = _rpD3D9VertexDeclarationInstColor(
				          (RwUInt8*)vertexData + dcl[i].Offset + resEntryHeader->vertexStream[dcl[i].Stream].stride*instData->minVert,
				          night + instData->minVert,
				          instData->numVertices,
				          resEntryHeader->vertexStream[dcl[i].Stream].stride);
				if(color1Idx >= 0){
					instData->vertexAlpha |= _rpD3D9VertexDeclarationInstColor(
					          (RwUInt8*)vertexData + dcl[color1Idx].Offset + resEntryHeader->vertexStream[dcl[color1Idx].Stream].stride*instData->minVert,
					          day + instData->minVert,
					          instData->numVertices,
					          resEntryHeader->vertexStream[dcl[color1Idx].Stream].stride);
				}
				instData++;
			}
			}else if(lastLocked & rpGEOMETRYLOCKPRELIGHT){
				for(i = 0; dcl[i].Usage != D3DDECLUSAGE_COLOR || dcl[i].UsageIndex != 0; ++i)
					;
				numMeshes = resEntryHeader->numMeshes;
				instData = (RxD3D9InstanceData*)(resEntryHeader+1);
				while(numMeshes--){
					instData->vertexAlpha = 0;
					instWhite((RwUInt8*)vertexData + dcl[i].Offset + resEntryHeader->vertexStream[dcl[i].Stream].stride*instData->minVert,
					          instData->numVertices,
					          resEntryHeader->vertexStream[dcl[i].Stream].stride);
					instData++;
				}
			}
		}

		if(vertexData)
			vertBuffer->Unlock();
	}
	return 1;
}

RxPipeline*
CCustomBuildingPipeline__CreateCustomObjPipe_PS2(void)
{
	RxPipeline *pipeline;
	RxLockedPipe *lockedpipe;
	RxPipelineNode *node;
	RxNodeDefinition *instanceNode;

	pipeline = RxPipelineCreate();
	instanceNode = RxNodeDefinitionGetD3D9AtomicAllInOne();
	if(pipeline == NULL)
		return NULL;
	lockedpipe = RxPipelineLock(pipeline);
	if(lockedpipe == NULL ||
	   RxLockedPipeAddFragment(lockedpipe, NULL, instanceNode, NULL) == NULL ||
	   RxLockedPipeUnlock(lockedpipe) == NULL){
		RxPipelineDestroy(pipeline);
		return NULL;
	}
	node = RxPipelineFindNodeByName(pipeline, instanceNode->name, NULL, NULL);
	RxD3D9AllInOneSetInstanceCallBack(node, DNInstance_PS2);
	RxD3D9AllInOneSetReinstanceCallBack(node, reinstance);
	RxD3D9AllInOneSetRenderCallBack(node, CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch);

	CreateShaders();

	pipeline->pluginId = RSPIPE_PC_CustomBuilding_PipeID;
	pipeline->pluginData = RSPIPE_PC_CustomBuilding_PipeID;
	return pipeline;
}

RxPipeline*
CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2(void)
{
	RxPipeline *pipeline;
	RxLockedPipe *lockedpipe;
	RxPipelineNode *node;
	RxNodeDefinition *instanceNode;

	pipeline = RxPipelineCreate();
	instanceNode = RxNodeDefinitionGetD3D9AtomicAllInOne();
	if(pipeline == NULL)
		return NULL;
	lockedpipe = RxPipelineLock(pipeline);
	if(lockedpipe == NULL ||
	   RxLockedPipeAddFragment(lockedpipe, NULL, instanceNode, NULL) == NULL ||
	   RxLockedPipeUnlock(lockedpipe) == NULL){
		RxPipelineDestroy(pipeline);
		return NULL;
	}
	node = RxPipelineFindNodeByName(pipeline, instanceNode->name, NULL, NULL);
	RxD3D9AllInOneSetInstanceCallBack(node, DNInstance_PS2);
	RxD3D9AllInOneSetReinstanceCallBack(node, reinstance);
	RxD3D9AllInOneSetRenderCallBack(node, CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch);

	CreateShaders();

	pipeline->pluginId = RSPIPE_PC_CustomBuildingDN_PipeID;
	pipeline->pluginData = RSPIPE_PC_CustomBuildingDN_PipeID;
	return pipeline;
}

RwBool
CCustomBuildingRenderer__IsCBPCPipelineAttached(RpAtomic *atomic)
{
	uint32 pipeID = GetPipelineID(atomic);
	RpGeometry *geo = RpAtomicGetGeometry(atomic);
	RxPipeline *pipe;
	RpAtomicGetPipeline(atomic, &pipe);

	if(pipeID == RSPIPE_PC_CustomBuilding_PipeID || pipeID == RSPIPE_PC_CustomBuildingDN_PipeID)
		return TRUE;

	if(explicitBuildingPipe > 0)
		return FALSE;

	return pipe == nil && GetExtraVertColourPtr(geo) && RpGeometryGetPreLightColors(geo);
}

void
hookBuildingPipe(void)
{
	InterceptCall(&CustomBuildingPipeline__Update_orig, CustomBuildingPipeline__Update, 0x53C15E);
	InjectHook(0x5D7100, CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2);
	InjectHook(0x5D7D90, CCustomBuildingPipeline__CreateCustomObjPipe_PS2);
	Patch<uint8>(0x5D7200, 0xC3);	// disable interpolation

	// 0x5D7F40 is owned by src/texdefs.cpp (DK22Pac plugin lane) — do not double-hook here.
	// Injecting it here overwrote the normalmap plugin's jump (and vice versa),
	// so exactly one side may install the hook.


//	Patch<BYTE>(0x732B40, 0xC3);	// disable fading entities
//	Patch<BYTE>(0x732610, 0xC3);	// disable fading atomic
}

// Release building pipe caches on device reset
void ReleaseBuildingPipeCaches(void)
{
	// [bind-elision] a device reset/recreate invalidates every D3D texture
	// pointer the static elision cache could hold — never let one survive.
	bldBindElideInvalidate();
	s_setupEnv_lastobject = NULL;
	s_setupEnv_lastfrm = NULL;
	s_setupEnv_lastrenderframe = 0;
	// RwMatrix doesn't need explicit destroy, just zero it
	memset(&s_setupEnv_lastmat, 0, sizeof(RwMatrix));

	// Frame counters
	ps2FrameCount = 0;
	xboxFrameCount = 0;
	pbrFrameCount = 0;
}
