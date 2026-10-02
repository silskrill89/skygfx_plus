#include "skygfx.h"
#include "postfx.h"	// GetScreenSize — trusted screen-size cache (transient-camRas fix)
#include "brdfLibrary.h"
//#include <fstream>

void *ps2BuildingVS;
void *ps2BuildingFxVS;
void *ps2BuildingWindVS;
void *gtaivBuildingVS;
void *gtaivBuildingPS;
void *simpleDetailStochasticPS;
void *simpleDetailPS;
void *xboxBuildingWindVS;
void *xboxBuildingVS;
void *xboxBuildingStochasticPS;
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
// pipelinecommon.cpp — THE single rw->D3D conversion point for
// D3DRS_ALPHAFUNC. The rw and D3D compare enums are identical for 1..8 but
// rw 0 ("NA") has no D3D counterpart, so every raw ALPHAFUNC write in this
// file must go through it instead of pushing the rw value straight down.
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);
//extern std::fstream lg;


enum {
	// common
	REG_transform	= 0,
	REG_ambient	= 4,
	REG_directCol	= 5,	// 7 lights (main + 6 extra)
	REG_directDir	= 12,	//
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
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
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

// rw RwBlendFunction -> D3D D3DBLEND. Mirrors the file-static
// pipelinecommon.cpp pipeBlendToD3D and the vehicle twin
// vehiclePipe_blendToD3D (vehiclePipe.cpp:112): the enums are 1:1 for
// valid values (rwBLENDZERO=1 == D3DBLEND_ZERO ... rwBLENDSRCALPHASAT=11),
// 0 is the rw "NA" placeholder and is not a valid D3D value, so fall back.
static DWORD buildingPipe_blendToD3D(int rwBlend, DWORD fallback)
{
	if(rwBlend == rwBLENDNABLEND || rwBlend > rwBLENDSRCALPHASAT)
		return fallback;
	return (DWORD)rwBlend;
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
void
pipeForceAlphaBlock(void)
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
	int src = (srcRaw <= rwBLENDNABLEND || srcRaw > rwBLENDSRCALPHASAT)
	          ? rwBLENDSRCALPHA : srcRaw;
	int dst = (dstRaw <= rwBLENDNABLEND || dstRaw > rwBLENDSRCALPHASAT)
	          ? rwBLENDINVSRCALPHA : dstRaw;
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
	unsigned drifted = 0;
	bool check = dbglog_throttle("bld_alpha");
	// Both alpha-func DOMAINS in the same line so one live session proves the
	// conversion end-to-end: `fn` is the rw-cache value (RW domain),
	// `wantFn` is what pipeAlphaFuncToD3D maps it to and what the loop below
	// pushes, `devFn` is what the device was actually comparing against
	// BEFORE the repair (D3D domain). Pre-seeded to wantFn so a failed Get
	// can never fabricate a mismatch.
	RwUInt32 wantFn = pipeAlphaFuncToD3D((RwUInt32)fn);
	RwUInt32 devFn = wantFn;
	if(check){
		DWORD have = (DWORD)wantFn;
		dev->GetRenderState(D3DRS_ALPHAFUNC, &have);
		devFn = (RwUInt32)have;
	}
	for(int i = 0; i < 12; i++){
		RwUInt32 want = fallback[i];
		RwD3D9GetRenderState(ids[i], &want);
		if(check){
			DWORD have = (DWORD)want;
			dev->GetRenderState(ids[i], &have);
			if((RwUInt32)have != want)
				drifted |= 1u << i;
		}
		dev->SetRenderState(ids[i], want);
	}
	if(drifted || (check && devFn != wantFn))
		dbglog("[BUILDING] alpha/blend layer-3 drift repaired (mask=0x%03X: "
		       "test|ref|func|src|dst|op|blend|sepA|srcA|dstA|opA|cwr) "
		       "rw fn=%d -> d3d want=%u dev d3d=%u (rw ref=%d)",
		       drifted, fn, wantFn, devFn, ref);
}

void*
buildingPipe_selectPS(TexInfo *texinfo, bool hasEnvMap)
{
	if(hasEnvMap){
		if(texinfo->stochastic && config->stochastic)
			return xboxBuildingStochasticPS;
		else
			return xboxBuildingPS;
	}
	if(config->detailMaps && texinfo->detail){
		float tile = texinfo->detailtile/10.0f;
		RwD3D9SetPixelShaderConstant(1, &tile, 1);
		pipeSetTexture(texinfo->detail, 2);
		if(texinfo->stochastic && config->stochastic)
			return simpleDetailStochasticPS;
		else
			return simpleDetailPS;
	}
	return simplePS;
}

// File-level frame counters (promoted from function-static so they can be zeroed on device reset)
static int ps2FrameCount = 0;
static int xboxFrameCount = 0;
static int pbrFrameCount = 0;

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	if(++ps2FrameCount % 300 == 1)
		dbglog("PS2Building: frame %d object=%p type=%d flags=0x%X", ps2FrameCount, object, type, flags);

	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32	numMeshes;
	CustomEnvMapPipeMaterialData *envData;
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

	pipeGetComposedTransformMatrix(atomic, transform);
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

	// Forward+ clustered lights: upload c45/c48-c111 and bind the tile index
	// texture (s5) — same call/position as the PBR building cb (:1175).
	// pipeline=4 (BUILDING_GTAIV) dispatches HERE (switch below: GTAIV = PS2
	// cb + IV shader swap), and this cb never called SetConstants, so the
	// per-frame cull collected lights that never reached the GPU
	// ("collected N lights, gpu=0"). Guarded by the config flag; the callee
	// self-guards too (forwardplus.cpp:414) and clears stale c45/s5 when off.
	// NOTE (verified via fxc /dumpbin): the currently bound
	// GTAIVBuilding_ps.cso / GTAIVVehicle_ps.cso are 7-slot blends that do NOT
	// read c45 yet (the prebuilt GTAIVForwardPlus_*.cso in shaders/ are
	// unwired), so this fixes the upload path, not today's IV pixels.
	if(config->forwardPlusEnable)
		ForwardPlus_SetConstants();

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);
	int alphafunc = rs.alphafunc, alpharef = rs.alpharef;
	int src = rs.src, dst = rs.dst;
	int fog = rs.fog, zwrite = rs.zwrite;

	for(numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		material = instancedData->material;

		colorScale = 1.0f;
		if(material->texture)
			colorScale = config->ps2ModulateBuilding ? 255.0f/128.0f : 1.0f;
		pipeSetTexture(material->texture, 0);
		RwD3D9SetPixelShaderConstant(0, &colorScale, 1);
		RwD3D9SetVertexShaderConstant(REG_shaderParams, &colorScale, 1);

		buildingPipe_setUVTransform(material, &ident);


		DefinedVertexShader definedVertexShader = (DefinedVertexShader)GetDefinedShader(atomic);

		if (definedVertexShader == DefinedVertexShader::WIND) {
			hasAlpha = instancedData->material->color.alpha != 255;
		}
		else {
			hasAlpha = (bool)(instancedData->vertexAlpha || instancedData->material->color.alpha != 255);
		}

		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);

		pipeUploadMatCol(flags, material, REG_matCol);

		RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		buildingPipe_uploadSurfProps(material, REG_surfProps);

		TexInfo *texinfo = RwTextureGetTexDBInfo(material->texture);

		// IV building shader swap — iv_mode preset (config->ivMode) OR the
		// BUILDING_GTAIV pipe selection itself, matching the vehicle side
		// (`ivMode || vehiclePipe == CAR_GTAIV`, vehiclePipe.cpp:690) and
		// this file's Switch (BUILDING_GTAIV = PS2 callback + IV swap).
		// Without the pipe term, buildingPipe == BUILDING_GTAIV with
		// ivMode = 0 rendered PS2 shaders under a "GTA IV" building pipe
		// selection — the asymmetry the vehicle side never had.
		//
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
		bool wantIVBuilding = config->ivMode || config->buildingPipe == BUILDING_GTAIV;
		if(wantIVBuilding && !(gtaivBuildingVS && gtaivBuildingPS) &&
		   dbglog_throttle("ivbld_missing"))
			dbglog("[BUILDING] IV shaders missing (ivMode=%d buildingPipe=%d; VS=%p PS=%p) — using PS2 path",
			       config->ivMode, config->buildingPipe, gtaivBuildingVS, gtaivBuildingPS);

		if(wantIVBuilding && gtaivBuildingVS && gtaivBuildingPS){
			RwD3D9SetVertexShader(gtaivBuildingVS);
			RwD3D9SetPixelShader(gtaivBuildingPS);
			// GTAIVBuilding_ps computes its env UV as `uv = v1*c0.x + c0.x`
			// (shader-local `def c0, 0.5, 0, 0, 0` — maps [-1,1] -> [0,1]).
			// The per-mesh loop pushed colorScale (1.0, or 255/128 with
			// ps2ModulateBuilding) into PS c0 a few lines above, which warps
			// that remap; re-assert the shader's own constant AFTER the bind.
			float ivPsC0[4] = { 0.5f, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, ivPsC0, 1);
			// GTAIVBuilding_ps samples s1 unconditionally (its env lerp).
			// s1 is only ever bound for env-mapped materials further down, so
			// for everything else the read hit a NULL stage. The lerp weight
			// is 0 for non-env materials, but bind anyway so the sampled texel
			// is defined (NaN-safe: NaN*0 is still NaN and renders black).
			if(material->texture)
				RwD3D9SetTexture(material->texture, 1);
		}else if (definedVertexShader == DefinedVertexShader::WIND && instancedData->vertexAlpha) {
			setWindParams(atomic, frame);
			RwD3D9SetVertexShader(ps2BuildingWindVS);
			RwD3D9SetPixelShader(buildingPipe_selectPS(texinfo, false));
		}
		else {
			RwD3D9SetVertexShader(ps2BuildingVS);
			RwD3D9SetPixelShader(buildingPipe_selectPS(texinfo, false));
		}

		if(material->pipeline == (RxPipeline*)TagRenderCB){
			TagRenderCB(atomic, resEntryHeader, instancedData);
			continue;
		}

		D3D9RenderDual(config->dualPassBuilding, resEntryHeader, instancedData, texinfo);

		// Reflection
		if(*(int*)&material->surfaceProps.specular & 1){
			envData = *RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material, CCustomCarEnvMapPipeline__ms_envMapPluginOffset);
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
			RwUInt32 savedVtxAlpha = FALSE;
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			RwD3D9SetTexture(envData->texture, 1);
			fxParams.shininess = envData->GetShininess();
			fxParams.lightmult = 1.0;
			envXform.x = 0.0;
			envXform.y = 0.0;
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);
			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);

			RwD3D9SetVertexShader(ps2BuildingFxVS);
			// ps2EnvSpecFxPS samples envMapTex(s1) * envcolor + maskTex(s2) *
			// speccolor (backup_original/buildingPipe.cpp:341 used it here);
			// the env bind above already occupies s1. simplePS samples s0
			// (diffuse) at env UVs — wrong texture. NULL-guard: resource-load
			// failure keeps the previous stable behaviour over a NULL PS.
			RwD3D9SetPixelShader(ps2EnvSpecFxPS ? ps2EnvSpecFxPS : simplePS);

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
			RwD3D9SetRenderState(D3DRS_SRCBLEND, buildingPipe_blendToD3D(src, D3DBLEND_SRCALPHA));
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, buildingPipe_blendToD3D(dst, D3DBLEND_INVSRCALPHA));
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
			RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)alphafunc));
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
				d3d9device->SetRenderState(D3DRS_SRCBLEND, buildingPipe_blendToD3D(src, D3DBLEND_SRCALPHA));
				d3d9device->SetRenderState(D3DRS_DESTBLEND, buildingPipe_blendToD3D(dst, D3DBLEND_INVSRCALPHA));
				d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)alphafunc));
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
				d3d9device->SetRenderState(D3DRS_FOGCOLOR, (DWORD)fog);
				d3d9device->SetSamplerState(0, D3DSAMP_ADDRESSU, rs.sampU[0]);
				d3d9device->SetSamplerState(0, D3DSAMP_ADDRESSV, rs.sampV[0]);
				d3d9device->SetSamplerState(1, D3DSAMP_ADDRESSU, rs.sampU[1]);
				d3d9device->SetSamplerState(1, D3DSAMP_ADDRESSV, rs.sampV[1]);
			}
		}
	}
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
}

// The PC callback cannot be salvaged. Just do it Xbox-style instead
void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	if(++xboxFrameCount % 300 == 1)
		dbglog("XboxBuilding: frame %d object=%p type=%d flags=0x%X", xboxFrameCount, object, type, flags);

	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32	numMeshes;
	CustomEnvMapPipeMaterialData *envData;
	RwBool hasAlpha;
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

	colorScale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorScale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);

	buildingPipe_setupResEntry(repEntry, &resEntryHeader, &instancedData);


	DefinedVertexShader definedVertexShader = (DefinedVertexShader)GetDefinedShader(atomic);

	setDnParams(atomic);

	CustomBuildingEnvMapPipeline__SetupEnv(atomic, NULL, &envmat);
	RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);

	bool vertexAlphaIsAlpha = true;
	if (definedVertexShader == DefinedVertexShader::WIND) {
		vertexAlphaIsAlpha = false;
		setWindParams(atomic, frame);
		RwD3D9SetVertexShader(xboxBuildingWindVS);
	}
	else {
		RwD3D9SetVertexShader(xboxBuildingVS);
	}

	for(numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		material = instancedData->material;

		pipeSetTexture(material->texture, 0);

		buildingPipe_setUVTransform(material, &ident);

		if (vertexAlphaIsAlpha) {
			hasAlpha = (bool)(instancedData->vertexAlpha || instancedData->material->color.alpha != 255);
		}
		else {
			hasAlpha = instancedData->material->color.alpha != 255;
		}
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);

		RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		
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

		TexInfo *texinfo = RwTextureGetTexDBInfo(material->texture);
		if(*(int*)&material->surfaceProps.specular & 1){
			envData = *RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material, CCustomCarEnvMapPipeline__ms_envMapPluginOffset);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			RwD3D9SetTexture(envData->texture, 1);
			fxParams.shininess = envData->GetShininess();
			fxParams.lightmult = 1.0f;
			envXform.x = 0.0f;
			envXform.y = 0.0f;
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);
			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);
			RwD3D9SetPixelShader(buildingPipe_selectPS(texinfo, true));
		}else{
			RwD3D9SetPixelShader(buildingPipe_selectPS(texinfo, false));
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
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
}

extern RwRGBAReal spheremapfog;
void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Sphere(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RpAtomic *atomic;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpMaterial *material;
	RwInt32	numMeshes;
	RwBool hasAlpha;
	float transform[16];
	RwMatrix ident;

	atomic = (RpAtomic*)object;
	RwMatrixSetIdentity(&ident);

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);

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
	RwD3D9SetPixelShader(simpleFogPS);

	numMeshes = resEntryHeader->numMeshes;
	while(numMeshes--){
		material = instancedData->material;

		pipeSetTexture(material->texture, 0);

		buildingPipe_setUVTransform(material, &ident);

		hasAlpha = instancedData->vertexAlpha != 0 || instancedData->material->color.alpha != 255;
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);

		pipeUploadMatCol(flags, material, REG_matCol);

		RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);
		// By-name c20 build (sphereBuildingVS reads only surfAmb=.x, but the
		// raw 3-float RwSurfaceProperties struct would overread .w).
		buildingPipe_uploadSurfProps(material, REG_surfProps);

		D3D9Render(resEntryHeader, instancedData);

		instancedData++;
	}
	buildingPipe_restoreRenderState(&rs);
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
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

	if(++pbrFrameCount % 300 == 1)
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

	DefinedVertexShader definedVertexShader = (DefinedVertexShader)GetDefinedShader(atomic);

	// Set PBR building shaders
	bool vertexAlphaIsAlpha = true;
	if (buildingPBRVS) {
		if (definedVertexShader == DefinedVertexShader::WIND) {
			vertexAlphaIsAlpha = false;
			setWindParams(atomic, frame);
		}
		RwD3D9SetVertexShader(buildingPBRVS);
	} else {
		if (definedVertexShader == DefinedVertexShader::WIND) {
			vertexAlphaIsAlpha = false;
			setWindParams(atomic, frame);
			RwD3D9SetVertexShader(xboxBuildingWindVS);
		} else {
			RwD3D9SetVertexShader(xboxBuildingVS);
		}
	}
	RwD3D9SetPixelShader(buildingPBRPS);

	// Upload ambient color to PS c24 for PBR ambient term (matches vehicle pipe)
	float ambientPS[4] = { buildingAmbient.red, buildingAmbient.green, buildingAmbient.blue, 0.0f };
	RwD3D9SetPixelShaderConstant(24, ambientPS, 1);

	// Universal dynamic-sky ambient weight (PS c20) — shared with the vehicle path.
	float iblAmbient[4] = { config->pbrIblAmbientWeight, 0.0f, 0.0f, 0.0f };
	RwD3D9SetPixelShaderConstant(20, iblAmbient, 1);

	// Bind the dynamic-sky IBL capture to stage 3 (raw D3D9, same as the proven
	// vehicle path). Without this, main_building's iblTex sample reads stale/black
	// state. Stage 0 stays RW-cached, so no rwRENDERSTATETEXTURERASTER resync needed.
	// iblBound feeds the per-mesh bldEnv upload: the sky-reflect term must be
	// 0 whenever s3 is unbound (an unbound sampler reads undefined).
	bool iblBound = false;
	{
		extern IDirect3DTexture9 *g_iblTex;
		IDirect3DDevice9 *dev = d3d9device;
		if(dev && g_iblTex){
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

	BuildingRenderState rs;
	buildingPipe_saveRenderState(&rs);

	for(int numMeshes = resEntryHeader->numMeshes; numMeshes--; instancedData++){
		RpMaterial *material = instancedData->material;
		float colorScale = 1.0f;
		if(material->texture)
			colorScale = config->ps2ModulateBuilding ? 255.0f/128.0f : 1.0f;
		pipeSetTexture(material->texture, 0);
		RwD3D9SetPixelShaderConstant(0, &colorScale, 1);
		RwD3D9SetVertexShaderConstant(REG_shaderParams, &colorScale, 1);

		// UV transform support
		buildingPipe_setUVTransform(material, &ident);

		// Vertex alpha handling
		bool hasAlpha;
		if (vertexAlphaIsAlpha) {
			hasAlpha = (bool)(instancedData->vertexAlpha || instancedData->material->color.alpha != 255);
		}
		else {
			hasAlpha = instancedData->material->color.alpha != 255;
		}
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);

		RwD3D9SetVertexShaderConstant(REG_ambient, &buildingAmbient, 1);

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
				pipeSetTexture(envMat->texture, 1);
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
		if(envWeight <= 0.0f)
			pipeSetTexture(material->texture, 1);
		// Sky term only for materials WITHOUT their own env map, and only when
		// s3 really holds the IBL capture (the PS's validity gate is the second
		// line of defence against a dead sample).
		float skyRefl = (config->bldEnvReflect && envWeight <= 0.0f && iblBound)
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
		// already pass it; NULL-safe (falls back to faketexinfo, dualPass=1).
		TexInfo *texinfo = RwTextureGetTexDBInfo(material->texture);
		D3D9RenderDual(config->dualPassBuilding, resEntryHeader, instancedData, texinfo);

		// PipeChain classify: second draw of this mesh into the full-res pack
		// buffer (PipeChainShader forced, c0.x=9 pack mode). Runs AFTER the
		// main draw so ZTEST sees the real depth (same Z, LESSEQUAL passes);
		// Begin keeps ZWRITE off, so the depth buffer is untouched. Values
		// are the same brdfLibrary ones pipeUploadPBR sent in c22/c23.
		{
			extern bool PipeChain_ClassifyBegin(float, float, float, float);
			extern void PipeChain_ClassifyEnd(void);
			if(PipeChain_ClassifyBegin((float)surfaceType, brdf->glossiness, brdf->specular, 0.0f)){
				D3D9Render(resEntryHeader, instancedData);
				PipeChain_ClassifyEnd();
				RwD3D9SetPixelShader(buildingPBRPS); // Begin forced PipeChainShader
			}
		}
	}

	buildingPipe_restoreRenderState(&rs);

	// PBR cb never called buildingPipe_cleanup (PS2 :447 / Xbox :561 do)
	// and additionally raw-binds s3 (g_iblTex), s4 (normal buffer) and s5
	// (forward+ tile). Leave a clean handoff: vehicles rebind what they
	// need, but FFP/HUD/particle draws between the building and vehicle
	// passes must not keep sampling the PBR samplers or a stage1 colour
	// op the env/Xbox passes may have left enabled from an earlier pipe.
	buildingPipe_cleanup();          // s1 unbind + stage1 COLOROP/ALPHAOP DISABLE
	RwD3D9SetTexture(NULL, 2);
	RwD3D9SetTexture(NULL, 3);
	RwD3D9SetTexture(NULL, 4);
	RwD3D9SetTexture(NULL, 5);
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

	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
//	if(GetAsyncKeyState(VK_F4) & 0x8000)
//		return;

	RwUInt32 savedCull = pipeEnterCullMode();
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
	// device) for the duration of the cb; exit restores each layer to its own
	// pre-entry value (postfx manages the raw device side itself with its own
	// save/restore — syncing the sides here would fight that contract).
	RwBool savedZWriteRw = TRUE;
	DWORD savedZWriteDrv = TRUE;
	DWORD savedZWriteDev = TRUE;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWriteRw);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDrv);
	d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDev);
	if(!savedZWriteRw || !savedZWriteDrv || !savedZWriteDev){
		if(dbglog_throttle("BuildingZWrite"))
			dbglog("[BUILDING] ZWRITE off/desynced on entry (rw=%d drv=%d dev=%d) — forcing TRUE for cb",
				(int)savedZWriteRw, (int)savedZWriteDrv, (int)savedZWriteDev);
	}
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
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
	// before dispatch — see pipeForceAlphaBlock.
	pipeForceAlphaBlock();

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
	// Restore each zwrite layer to its own pre-entry value (see entry comment).
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDrv);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDev);
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

	if(explicitBuildingPipe >= 0)
		InjectHook(0x5D7F40, CCustomBuildingRenderer__IsCBPCPipelineAttached, PATCH_JUMP);


//	Patch<BYTE>(0x732B40, 0xC3);	// disable fading entities
//	Patch<BYTE>(0x732610, 0xC3);	// disable fading atomic
}

// Release building pipe caches on device reset
void ReleaseBuildingPipeCaches(void)
{
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
