#include "skygfx.h"
#include "neo.h"
#include "brdfLibrary.h"

extern void *Glass_Vehicle;
extern void *Rubber_Vehicle;
extern void *Rubber_Vehicle_Modern;
extern int GetVehicleEraByID(int modelID);
extern float &CWeather__WetRoads;	// buildingPipe.cpp — 0..1 rain/wetness for the PS c20.z wet term

enum {
	// common
	REG_transform	= 0,
	REG_ambient	= 4,
	REG_directCol	= 5,	// 7 lights (main + 6 extra)
	REG_directDir	= 12,	//
	REG_matCol	= 19,
	REG_surfProps	= 20,

	// env
	REG_fxParams	= 30,
	REG_envXform	= 31,
	REG_envmat	= 32,

	// spec tex
	REG_specmat	= 35,	// 3 regs: c35-c37 (PS2 cb)
	REG_lightdir	= 38,

	// spec light
	REG_eye		= 36,	// c36 per AGENTS.md register map (PBR VS convention)

	// leeds env tex matrix (main_leedsCarFx texmat_leeds)
	REG_envtexmat	= 40,	// 4 regs: c40-c43; relocated from c36 (now claimed by REG_eye)
};

// ===========================================================================
// c20 SURFPROPS CONTRACT (vehicle pipe) — SINGLE SOURCE OF TRUTH.
// (Finding #4: every upload site and every consumer, in one place.)
//
// VS domain (vertex constants, REG_surfProps = c20):
//     x = surfaceProps.ambient   -> surfAmb    (ambient scale)
//     y = SPEC SLOT              -> see ".y semantics" below
//     z = surfaceProps.diffuse   -> surfDiff   (directional-light scale)
//     w = prelit flag            -> isPrelit / surfPrelight
//                                   (1.0 iff flags & rpGEOMETRYPRELIT else 0.0)
//
// UPLOAD SITES (all four):
//   1. vehiclePipe_setupMaterial()  float4 {amb, 0, diff, prelit}
//      — PS2 / PC / Specular / mobile callbacks (callers :673/:925/:1475).
//   2. leeds cb                     same slots after the ambient clamp (:1351).
//   3. Env cb (CAR_MODERN / PBR)    by-name struct {ambient, specular,
//      diffuse, prelight} declared :1595, filled at :1752 (prelight) and per
//      mesh at :1858 (glass) / :1924 (rubber) / :2085 (opaque). The SAME
//      float4 is mirrored to PS c0 at each of those sites.
//   4. Xbox cb (SetupMaterial_Xbox)   float4 {amb, spec, diff, prelit} built
//      BY NAME at :1096 — its LOC_ map keeps surfProps at c26 (LOC_matCol=20 /
//      LOC_surfProps=26, see :1048), but the SLOT MEANINGS above are shared
//      with c20. Never upload the raw RwSurfaceProperties struct: its memory
//      layout is {ambient, specular, diffuse} (external/d3d9/rwplcore.h:1492 —
//      NOT the "ambient, diffuse, specular" the doxygen prose and
//      buildingPipe.cpp:70 suggest), so a raw upload maps slots through the
//      struct layout and reads 4 bytes past the 3-float struct for .w.
//
// CONSUMERS:
//   vehiclePipeVS.hlsl main_vehicle, main_vehiclePBR, main_mobileVehicle:
//      surfAmb = .x, surfDiff = .z, isPrelit/surfPrelight = .w. None reads .y.
//      (main_vehiclePBR additionally reads the separate VS c28.x
//       vehPrelightFallback — the non-prelit lit-colour floor.)
//   main_xboxCar (a DIFFERENT shader object): c20 is matCol_xb and its
//      surfProps live at c26 (surfProps_xb) — do not reconcile the REGISTER
//      maps, but the slot MEANINGS are the same contract: .x = ambient scale,
//      .y = spec scale (the ONE live spec consumer in the file), .z =
//      diffuse/light scale. It reads no .w; the prelit flag is written there
//      for contract parity only.
//   VehiclePBR_Modern.hlsl main()/main_rubber and Glass_Vehicle.hlsl: PS c0
//      surfProps is DECLARED BUT NEVER READ (grep: no `surfProps.` member
//      access in either file) — the PS-side upload is dead weight kept for
//      register parity.
//
// .y (SPEC) SEMANTICS: the Env cb writes material->surfaceProps.specular
//   there — which for SA is the ENVMAP-FLAG BITFIELD (read back as
//   *(RwUInt32*)&material->surfaceProps.specular & 1/2/4 at :2010-2013), not
//   a magnitude — while setupMaterial and the leeds cb write 0.0. No bound
//   shader reads it, so both are inert. If a spec scale is ever wanted, use
//   PS c44.y (envSpecularityMult), the live knob — do NOT start reading c20.y.
//   (The Xbox c26 mirror of this slot is the one exception: main_xboxCar reads
//   it as a real spec scale, and SetupMaterial_Xbox writes the computed
//   `specularity` there — never the flag bitfield.)
// ===========================================================================

void *vehiclePipeVS;
void *ps2CarFxVS;
void *specCarFxVS;
void *specCarFxPS;
void *vehiclePBRVS;
void *xboxCarVS;
void *leedsCarFxVS;
void *gtaivVehicleVS;
void *gtaivVehiclePS;
void *mobileVehiclePipeVS, *mobileVehiclePipePS;
void *ps2EnvSpecFxPS;	// also used by the building pipeline
int renderingWheel;
extern void pipeEnsureIBLBuffer(void);	// pipelinecommon.cpp — shared once-per-frame IBL gate
extern void pipeForceAlphaBlock(void);	// buildingPipe.cpp — layer-3 alpha/blend repair
                                        // (the alpha half of pipeForceCullMode)
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);	// pipelinecommon.cpp — rw alpha-func -> D3DCMP

// rw RwBlendFunction -> D3D D3DBLEND. Mirrors the (file-static)
// pipelinecommon.cpp pipeBlendToD3D: the enums are 1:1 for valid values
// (rwBLENDZERO=1 == D3DBLEND_ZERO ... rwBLENDSRCALPHASAT=11), 0 is the rw
// "NA" placeholder and is not a valid D3D value, so fall back.
static DWORD vehiclePipe_blendToD3D(int rwBlend, DWORD fallback)
{
	if(rwBlend == rwBLENDNABLEND || rwBlend > rwBLENDSRCALPHASAT)
		return fallback;
	return (DWORD)rwBlend;
}

float black4f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
float white4f[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

int betaEnvmaptest = 0;
float envmap1tweak = 1.0f;
float envmap2tweak = 1.0f;

CPool<CustomEnvMapPipeAtomicData> *&gEnvMapPipeAtmDataPool = *(CPool<CustomEnvMapPipeAtomicData>**)0xC02D2C;

void*
CustomEnvMapPipeAtomicData::operator new(size_t size)
{
	return gEnvMapPipeAtmDataPool->New();
}

static RwMatrix carfx_view, carfx_env1Inv, carfx_env2Inv;
static RwV3d carfx_lightdir;	// view space
static RwFrame *carfx_env1Frame, *carfx_env2Frame;

// File-level cache statics (promoted from function-static so they can be nulled on device reset)
static void *s_env2xform_lastobject = NULL;
static CustomEnvMapPipeMaterialData *s_env2xform_lastenvdata = NULL;
static RwUInt16 s_env2xform_lastrenderframe = 0;
static float s_env2xform_lastTransX = 0.0f, s_env2xform_lastTransY = 0.0f;
static float s_env2xform_lastx = 0.0f, s_env2xform_lasty = 0.0f;

static void *s_env2xformpc_lastobject = NULL;
static CustomEnvMapPipeMaterialData *s_env2xformpc_lastenvdata = NULL;
static RwUInt16 s_env2xformpc_lastrenderframe = 0;
static float s_env2xformpc_lastTransX = 0.0f, s_env2xformpc_lastTransY = 0.0f;
static float s_env2xformpc_lastx = 0.0f, s_env2xformpc_lasty = 0.0f;

static RwMatrix s_setupEnv_lastmat;
static void *s_setupEnv_lastobject = NULL;
static RwFrame *s_setupEnv_lastfrm = NULL;
static RwUInt16 s_setupEnv_lastrenderframe = 0;

// Leeds reflections
static D3DMATRIX envtexmat;

CustomEnvMapPipeAtomicData*
CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData(RpAtomic *atomic)
{
	CustomEnvMapPipeAtomicData *atmEnvData = *GETENVMAPATM(atomic);
	if(atmEnvData == NULL){
		atmEnvData = new CustomEnvMapPipeAtomicData;
		// POD struct: new leaves fields uninitialized, so zero the used fields explicitly.
		atmEnvData->trans = 0;
		atmEnvData->posx = 0;
		atmEnvData->posy = 0;
		*GETENVMAPATM(atomic) = atmEnvData;
	}
	return atmEnvData;
}

static bool s_vehiclePipeInitialized = false;

void
CCustomCarEnvMapPipeline__Init(void)
{
	if(s_vehiclePipeInitialized){
		return;
	}
	s_vehiclePipeInitialized = true;
	dbglog("CCustomCarEnvMapPipeline__Init: first-time init");

	// Log the active pipe mapping for debugging
	static const char* pipeNames[] = {"PS2", "PC", "Xbox", "Specular", "Mobile", "Neo", "Leeds", "VCS", "Env", "GTAIV", "Modern"};
	int pipeIdx = config->vehiclePipe;
	if(pipeIdx >= 0 && pipeIdx < 11)
		dbglog("[PIPE] vehiclePipe=%d (%s) → CCustomCarEnvMapPipeline__CustomPipeRenderCB_%s",
			pipeIdx, pipeNames[pipeIdx],
			pipeIdx == CAR_MODERN ? "Env (PBR)" :
			pipeIdx == CAR_ENV ? "Env" :
			pipeIdx == CAR_NEO ? "NeoCallback" :
			pipeIdx == CAR_GTAIV ? "GTAIV" : "Switch");
	static const char* buildPipeNames[] = {"PS2", "Xbox", "GTAIV", "PBR"};
	if(config->buildingPipe >= 0 && config->buildingPipe < 4)
		dbglog("[PIPE] buildingPipe=%d (%s) → shader=%s",
			config->buildingPipe, buildPipeNames[config->buildingPipe],
			config->buildingPipe == BUILDING_PBR ? "buildingPBRPS" : "legacy");

	// Null-guard: ensure D3D device is available before any D3D calls
	if(!d3d9device){
		dbglog("CCustomCarEnvMapPipeline__Init: d3d9device is NULL, aborting");
		s_vehiclePipeInitialized = false;
		return;
	}

	// Initialize vehicle classification from game data files
	extern void VehShaders_Init(const char *gameDir);
	char gameDir[MAX_PATH];
	GetModuleFileNameA(NULL, gameDir, MAX_PATH);
	char *lastSlash = strrchr(gameDir, '\\');
	if(lastSlash) *lastSlash = '\0';
	VehShaders_Init(gameDir);

	// Initialize weather system (multi-timecyc)
	extern void Weather_Init(const char *gameDir);
	Weather_Init(gameDir);

	// Initialize wheel system (shared wheel DFFs)
	extern void Wheels_Init(const char *gameDir);
	Wheels_Init(gameDir);

	static RwV3d axis_X = { 1.0, 0.0, 0.0 };
	static RwV3d axis_Y = { 0.0, 1.0, 0.0 };
	static RwV3d axis_Z = { 0.0, 0.0, 1.0 };

	reflectionTex = RwTextureCreate(nil);
	RwTextureSetFilterMode(reflectionTex, rwFILTERLINEAR);

	MakeEnvmapCam();
	MakeEnvmapRasters();

	CreateShaders();

	// PBR pipeline does not depend on Neo car tweaking table data.
	// Ensure the Neo flag is set so CAR_MODERN/CAR_ENV render paths
	// fire even when neo\carTweakingTable.dat is absent.
	iCanHasNeoCar = 1;

	if(carfx_env1Frame == NULL){
		carfx_env1Frame = RwFrameCreate();
if(betaEnvmaptest)
		RwMatrixRotate(RwFrameGetMatrix(carfx_env1Frame), &axis_X, 60.0f, rwCOMBINEREPLACE);
else{
		RwMatrixRotate(RwFrameGetMatrix(carfx_env1Frame), &axis_X, 33.0, rwCOMBINEREPLACE);
		RwMatrixRotate(RwFrameGetMatrix(carfx_env1Frame), &axis_Y, -33.0, rwCOMBINEPOSTCONCAT);
		RwMatrixRotate(RwFrameGetMatrix(carfx_env1Frame), &axis_Z, 50.0, rwCOMBINEPOSTCONCAT);
}
		RwFrameUpdateObjects(carfx_env1Frame);
		RwFrameGetLTM(carfx_env1Frame);
	}
	if(carfx_env2Frame == NULL){
		carfx_env2Frame = RwFrameCreate();
		RwFrameSetIdentity(carfx_env2Frame);
		RwFrameUpdateObjects(carfx_env2Frame);
	}

	envtexmat.m[0][0] = 0.5f;
	envtexmat.m[0][1] = 0.0f;
	envtexmat.m[0][2] = 0.0f;
	envtexmat.m[0][3] = 0.0f;

	envtexmat.m[1][0] = 0.0f;
	envtexmat.m[1][1] = -0.5f;
	envtexmat.m[1][2] = 0.0f;
	envtexmat.m[1][3] = 0.0f;

	envtexmat.m[2][0] = 0.0f;
	envtexmat.m[2][1] = 0.0f;
	envtexmat.m[2][2] = 0.0f;
	envtexmat.m[2][3] = 0.0f;

	envtexmat.m[3][0] = 0.5f;
	envtexmat.m[3][1] = 0.5f;
	envtexmat.m[3][2] = 0.0f;
	envtexmat.m[3][3] = 1.0f;
}

void (*CCustomCarEnvMapPipeline__PreRenderUpdate_orig)(void);
void
CCustomCarEnvMapPipeline__PreRenderUpdate(void)
{
	RwV3d l;

	RwCamera *curCam = (RwCamera*)RWSRCGLOBAL(curCamera);
	if(!curCam) return;
	RwFrame *camFrame = RwCameraGetFrame(curCam);
	if(!camFrame) return;
	RwMatrix *camLTM = RwFrameGetLTM(camFrame);
	if(!camLTM) return;
	RwMatrixInvert(&carfx_view, camLTM);

	if(pDirect){
		RwFrame *lightFrame = RpLightGetFrame(pDirect);
		if(lightFrame){
			l = RwFrameGetMatrix(lightFrame)->at;
			RwV3dTransformVector(&carfx_lightdir, &l, &carfx_view);
			if(RwV3dNormalize(&carfx_lightdir, &carfx_lightdir) == 0.0f)
			carfx_lightdir = {0.0f, 1.0f, 0.0f};
		}
	}

	if(carfx_env1Frame){
		RwMatrix *env1LTM = RwFrameGetLTM(carfx_env1Frame);
		if(env1LTM) RwMatrixInvert(&carfx_env1Inv, env1LTM);
	}
	if(carfx_env2Frame){
		RwMatrix *env2LTM = RwFrameGetLTM(carfx_env2Frame);
		if(env2LTM) RwMatrixInvert(&carfx_env2Inv, env2LTM);
	}

	CCustomCarEnvMapPipeline__PreRenderUpdate_orig();
}

void
CCustomCarEnvMapPipeline__Env1Xform(RwMatrix *envmat,
	CustomEnvMapPipeMaterialData *envData, float *envXform)
{
	float sclx, scly;
	sclx = envData->GetTransScaleX()*50.0f;
	scly = envData->GetTransScaleY()*50.0f;
if(betaEnvmaptest){
sclx *= envmap1tweak;
scly *= envmap1tweak;
}
	// fractional parts of pos/scl
	envXform[0] = (envmat->pos.x - ((float)(int)(envmat->pos.x/sclx))*sclx)/sclx;
	envXform[1] = (envmat->pos.y - ((float)(int)(envmat->pos.y/scly))*scly)/scly;
}

// get absolute fractional part of pos/scl, or 1 - fractional part
inline float scaledfract(float pos, float scl){
	int i;
	float f;
	i = pos/scl;
	f = fabs((i*scl - pos)/scl);
	return i & 1 ? f : 1.0 - f;
}

void
CCustomCarEnvMapPipeline__Env2Xform(RpAtomic *atomic, RwMatrix *envmat,
	CustomEnvMapPipeMaterialData *envData, CustomEnvMapPipeAtomicData *atmEnvData, float *envXform)
{
	RwV3d diff, upnorm;
	float trans;
	float sclx, scly;
	float val1, val2;

	sclx = envData->GetTransScaleX()*50.0f;
	scly = envData->GetTransScaleY()*50.0f;

	if(s_env2xform_lastrenderframe != RWSRCGLOBAL(renderFrame) ||
	   s_env2xform_lastobject != atomic ||
	   s_env2xform_lastenvdata != envData){
		envData->renderFrameCounter = RWSRCGLOBAL(renderFrame);
		s_env2xform_lastrenderframe = RWSRCGLOBAL(renderFrame);
		s_env2xform_lastobject = atomic;
		s_env2xform_lastenvdata = envData;

		val1 = scaledfract(envmat->pos.x, sclx) + scaledfract(envmat->pos.y, scly);
		val2 = scaledfract(atmEnvData->posx, sclx) + scaledfract(atmEnvData->posy, scly);

		diff = { envmat->pos.x - atmEnvData->posx, envmat->pos.y - atmEnvData->posy, 0.0 };

		if(RwV3dNormalize(&diff, &diff) == 0.0f)
			diff = {0.0f, 1.0f, 0.0f};
		if(RwV3dNormalize(&upnorm, &envmat->up) == 0.0f)
			upnorm = {0.0f, 1.0f, 0.0f};
		if(RwV3dDotProduct(&diff, &diff) > 0.0f ||
		   RwV3dDotProduct(&diff, &upnorm) < 0.0f){
			trans = atmEnvData->trans - fabs(val2-val1);
			if(trans < 0.0f)
				trans += 1.0f;
		}else{
			trans = atmEnvData->trans + fabs(val2-val1);
			if(trans >= 1.0f)
				trans -= 1.0f;
		}

		s_env2xform_lastTransX = atmEnvData->trans = trans;
		s_env2xform_lastTransY = envmat->at.x + envmat->at.y;
		if(s_env2xform_lastTransY > 0.1) s_env2xform_lastTransY = 0.1f;
		if(s_env2xform_lastTransY < 0.0) s_env2xform_lastTransY = 0.0f;
		if(envmat->at.z < 0.0f)
			s_env2xform_lastTransY = 1.0 - s_env2xform_lastTransY;
		s_env2xform_lastx = atmEnvData->posx = envmat->pos.x;
		s_env2xform_lasty = atmEnvData->posy = envmat->pos.y;
	}else{
		atmEnvData->trans = s_env2xform_lastTransX;
		atmEnvData->posx = s_env2xform_lastx;
		atmEnvData->posy = s_env2xform_lasty;
	}
	envXform[0] = s_env2xform_lastTransX;
	envXform[1] = s_env2xform_lastTransY;
}

void
CCustomCarEnvMapPipeline__Env1Xform_PC(RpAtomic *atomic,
	CustomEnvMapPipeMaterialData *envData, float *envXform)
{
	float sclx, scly;
	RwMatrix *envmat;
	RwFrame *envFrame = RpAtomicGetClump(atomic) ? RpClumpGetFrame(RpAtomicGetClump(atomic)) : RpAtomicGetFrame(atomic);
	if(envFrame)
		envmat = RwFrameGetLTM(envFrame);
	else{
		static RwMatrix ident = { {1,0,0}, 0, {0,1,0}, 0, {0,0,1}, 0, {0,0,0}, 0 };
		envmat = &ident;
	}
	sclx = envData->GetTransScaleX()*50.0f;
	scly = envData->GetTransScaleY()*50.0f;
	// fractional parts of pos/scl
	envXform[0] = -(envmat->pos.x - ((float)(int)(envmat->pos.x/sclx))*sclx)/sclx;
	envXform[1] = -(envmat->pos.y - ((float)(int)(envmat->pos.y/scly))*scly)/scly;
}

void
CCustomCarEnvMapPipeline__Env2Xform_PC(RpAtomic *atomic,
	CustomEnvMapPipeMaterialData *envData, CustomEnvMapPipeAtomicData *atmEnvData, float *envXform)
{
	RwV3d diff, upnorm;
	float trans;
	float sclx, scly;
	float val1, val2;
	RwMatrix *envmat;

	sclx = envData->GetTransScaleX()*50.0f;
	scly = envData->GetTransScaleY()*50.0f;
	RwFrame *envFrame2 = RpAtomicGetClump(atomic) ? RpClumpGetFrame(RpAtomicGetClump(atomic)) : RpAtomicGetFrame(atomic);
	if(envFrame2)
		envmat = RwFrameGetLTM(envFrame2);
	else{
		static RwMatrix ident2 = { {1,0,0}, 0, {0,1,0}, 0, {0,0,1}, 0, {0,0,0}, 0 };
		envmat = &ident2;
	}

	if(s_env2xformpc_lastrenderframe != RWSRCGLOBAL(renderFrame) ||
	   s_env2xformpc_lastobject != atomic ||
	   s_env2xformpc_lastenvdata != envData){
		envData->renderFrameCounter = RWSRCGLOBAL(renderFrame);
		s_env2xformpc_lastrenderframe = RWSRCGLOBAL(renderFrame);
		s_env2xformpc_lastobject = atomic;
		s_env2xformpc_lastenvdata = envData;

		val1 = scaledfract(envmat->pos.x, sclx) + scaledfract(envmat->pos.y, scly);
		val2 = scaledfract(atmEnvData->posx, sclx) + scaledfract(atmEnvData->posy, scly);

		diff = { envmat->pos.x - atmEnvData->posx, envmat->pos.y - atmEnvData->posy, 0.0 };

		if(RwV3dNormalize(&diff, &diff) == 0.0f)
			diff = {0.0f, 1.0f, 0.0f};
		if(RwV3dNormalize(&upnorm, &envmat->up) == 0.0f)
			upnorm = {0.0f, 1.0f, 0.0f};
		if(RwV3dDotProduct(&diff, &diff) > 0.0f ||
		   RwV3dDotProduct(&diff, &upnorm) < 0.0f){
			trans = atmEnvData->trans - fabs(val2-val1);
			if(trans < 0.0f)
				trans += 1.0f;
		}else{
			trans = atmEnvData->trans + fabs(val2-val1);
			if(trans >= 1.0f)
				trans -= 1.0f;
		}

		s_env2xformpc_lastTransX = atmEnvData->trans = trans;
		s_env2xformpc_lastTransY = envmat->at.x + envmat->at.y;
		if(s_env2xformpc_lastTransY > 0.1) s_env2xformpc_lastTransY = 0.1f;
		if(s_env2xformpc_lastTransY < 0.0) s_env2xformpc_lastTransY = 0.0f;
		if(envmat->at.z < 0.0f)
			s_env2xformpc_lastTransY = 1.0 - s_env2xformpc_lastTransY;
		s_env2xformpc_lastx = atmEnvData->posx = envmat->pos.x;
		s_env2xformpc_lasty = atmEnvData->posy = envmat->pos.y;
	}else{
		atmEnvData->trans = s_env2xformpc_lastTransX;
		atmEnvData->posx = s_env2xformpc_lastx;
		atmEnvData->posy = s_env2xformpc_lasty;
	}
	envXform[0] = -s_env2xformpc_lastTransX;
	envXform[1] = s_env2xformpc_lastTransY;
}

void
CCustomCarEnvMapPipeline__SetupEnv(RpAtomic *atomic, RwFrame *envframe, RwMatrix *frminv, RwMatrix *envmat)
{
	RpClump *clump;
	RwFrame *frame;

	clump = RpAtomicGetClump(atomic);
	if(s_setupEnv_lastobject != (clump ? (void*)clump : (void*)atomic) ||
	   s_setupEnv_lastfrm != envframe ||
	   s_setupEnv_lastrenderframe != RWSRCGLOBAL(renderFrame)){
		frame = clump ? RpClumpGetFrame(clump) : RpAtomicGetFrame(atomic);
		if(frame)
			RwMatrixMultiply(&s_setupEnv_lastmat, RwFrameGetLTM(frame), frminv);

		s_setupEnv_lastobject = (clump ? (void*)clump : (void*)atomic);
		s_setupEnv_lastfrm = envframe;
		s_setupEnv_lastrenderframe = RWSRCGLOBAL(renderFrame);
	}
	*envmat = s_setupEnv_lastmat;
}

void
CCustomCarEnvMapPipeline__SetupSpec(RpAtomic *atomic, RwMatrix *specmat, RwV3d *specdir)
{
	RwFrame *specFrame = RpAtomicGetFrame(atomic);
	if(specFrame)
		RwMatrixMultiply(specmat, RwFrameGetLTM(specFrame), &carfx_view);
	else
		memset(specmat, 0, sizeof(RwMatrix));
	*specdir = carfx_lightdir;
}

void
uploadLightCol(RwUInt32 registerAddress, RpLight *l)
{
	if(!l){ RwD3D9SetVertexShaderConstant(registerAddress,(void*)black4f,1); return; }
	if(RpLightGetFlags(l) & rpLIGHTLIGHTATOMICS)
		RwD3D9SetVertexShaderConstant(registerAddress,(void*)&l->color,1);
	else
		RwD3D9SetVertexShaderConstant(registerAddress,(void*)black4f,1);
}

void uploadNoLights(void);

void
uploadLights(RwMatrix *lightmat)
{
	if(!lightmat){ uploadNoLights(); return; }

	// Use shared timecycle ambient (single source of truth for all pipelines)
	// This ensures vehicles get the same ambient as buildings and peds.
	// PBR floor applied so vehicles never drop to a black silhouette at night.
	RwRGBAReal tcAmbient = GetTimecycleAmbientPBR();
	// 0.85 scale is applied in GetTimecycleAmbient() for consistency

	// Interior/garage dampening: reduce ambient in sheltered areas
	extern bool CCullZones__PlayerNoRain(void);
	extern int* CGame__currArea;
	bool isInterior = (*CGame__currArea != 0);
	bool isSheltered = CCullZones__PlayerNoRain() && !isInterior;

	if(isSheltered){
		tcAmbient.red *= 0.55f;
		tcAmbient.green *= 0.55f;
		tcAmbient.blue *= 0.55f;
	}

	// Upload timecycle ambient directly (not pAmbient)
	RwD3D9SetVertexShaderConstant(REG_ambient, &tcAmbient, 1);
	pipeUploadLightColorForce(pDirect, REG_directCol);
	pipeUploadLightDirectionLocal(pDirect, lightmat, REG_directDir);
	for(int i = 0; i < 6; i++)
		if(i < NumExtraDirLightsInWorld && RpLightGetType(pExtraDirectionals[i]) == rpLIGHTDIRECTIONAL){
			pipeUploadLightColor(pExtraDirectionals[i], REG_directCol+i+1);
			pipeUploadLightDirectionLocal(pExtraDirectionals[i], lightmat, REG_directDir+i+1);
		}else{
			pipeUploadZero(REG_directCol+i+1);
			pipeUploadZero(REG_directDir+i+1);
		}
}

void
uploadNoLights(void)
{
	static float black[4*(1+2*7)] = { 0.0f };
	RwD3D9SetVertexShaderConstant(REG_ambient, black, 1+2*7);
}

struct VehicleRenderState {
	int alphafunc, src, dst, fog;
	DWORD texturefactor;
	// TEXTUREADDRESS leak plug (audit item 2): the env FX branches at :929/
	// :944 (PS2 cb) and :1081/:1092 (spec cb) push rwTEXTUREADDRESSWRAP
	// through the rw cache only and NOTHING ever restores it — the WRAP
	// survived past the callback into every later pass. L1 = rw cache; there
	// is NO D3DRS counterpart for addressing (L2 not applicable), so the
	// device layer is the per-stage sampler addressing (L3), snapshotted at
	// entry and restored by vehiclePipe_fxAdditiveBlend.
	int textureaddress;
	DWORD sampU[3], sampV[3];
};

// ResEntry setup: extracts header, instanced data, sets indices/streams/declaration
// Returns numMeshes via pointer
static void vehiclePipe_setupResEntry(RwResEntry *repEntry,
	RxD3D9ResEntryHeader **outHeader, RxD3D9InstanceData **outData, RwInt32 *outMeshes)
{
	*outHeader = (RxD3D9ResEntryHeader *)(repEntry + 1);
	*outData = (RxD3D9InstanceData *)(*outHeader + 1);
	if((*outHeader)->indexBuffer != NULL)
		RwD3D9SetIndices((*outHeader)->indexBuffer);
	_rwD3D9SetStreams((*outHeader)->vertexStream, (*outHeader)->useOffsets);
	RwD3D9SetVertexDeclaration((*outHeader)->vertexDeclaration);
	*outMeshes = (*outHeader)->numMeshes;
}

// Render state save bundle
static void vehiclePipe_saveRenderState(VehicleRenderState *state)
{
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &state->alphafunc);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &state->src);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &state->dst);
	RwRenderStateGet(rwRENDERSTATEFOGCOLOR, &state->fog);
	d3d9device->GetRenderState(D3DRS_TEXTUREFACTOR, &state->texturefactor);
	// TEXTUREADDRESS (audit item 2): the FX branches push
	// rwTEXTUREADDRESSWRAP through the rw cache only (":929/:944 — is this
	// needed?") and NOTHING ever restored it — the WRAP survived past the
	// callback into every later pass. There is NO D3DRS counterpart for
	// addressing (L2 not applicable), so the three layers here are the rw
	// cache (L1) plus the per-stage device sampler addressing (L3),
	// snapshotted here and restored by vehiclePipe_fxAdditiveBlend.
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

// Cleanup: unbind shaders, textures, disable TSS
static void vehiclePipe_cleanup()
{
	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTexture(NULL, 2);
	// Unbind sampler 3 too (IBL/env bind) — the sampler-state resets below
	// only touch addressing/filtering, they do not release the texture.
	RwD3D9SetTexture(NULL, 3);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	// Reset sampler 3 states (IBL sampler)
	d3d9device->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	d3d9device->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	// Unbind sampler 4 (normal buffer) and 5 (forward+ index) to prevent
	// texture leaks into subsequent game/HUD draws
	RwD3D9SetTexture(NULL, 4);
	RwD3D9SetTexture(NULL, 5);
	d3d9device->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	d3d9device->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	d3d9device->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	d3d9device->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	d3d9device->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
}

// FX additive blend pass: sets states, renders, restores.
// Three-layer doctrine (rw cache L1 + RwD3D9SetRenderState L2 + device L3):
// this pass used to poke ONLY the rw cache, so an applied[]-gated no-op could
// leave the DEVICE on the previous mesh's blend/alpha while the cache claimed
// ONE/ONE, and it left rwRENDERSTATEVERTEXALPHAENABLE=TRUE behind for every
// later mesh on the same atomic. Every state is forced through all three
// layers, and the per-mesh VERTEXALPHA value is snapshotted and restored.
static void vehiclePipe_fxAdditiveBlend(RxD3D9ResEntryHeader *header,
	RxD3D9InstanceData *data, const VehicleRenderState *state)
{
	// setupMaterial (called immediately before this per mesh) set the rw-side
	// VERTEXALPHA state; capture it so the additive pass can hand it back.
	RwUInt32 savedVtxAlpha = FALSE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);

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

	D3D9Render(header, data);

	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)state->fog);
	RwD3D9SetRenderState(D3DRS_FOGCOLOR, (RwUInt32)state->fog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)state->src);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, vehiclePipe_blendToD3D(state->src, D3DBLEND_SRCALPHA));
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)state->dst);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, vehiclePipe_blendToD3D(state->dst, D3DBLEND_INVSRCALPHA));
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state->alphafunc);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)state->alphafunc));
	// Close the VERTEXALPHA=TRUE leak through all three layers (L1 read back
	// per mesh; L2 is what D3D9RenderDual's gate reads).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
	// TEXTUREADDRESS: the env branches (PS2 cb :929/:944, spec cb :1081/
	// :1092) pushed rwTEXTUREADDRESSWRAP through the rw cache before this
	// pass and nothing else ever restored it — the WRAP leaked past the
	// callback into every later pass. No D3DRS counterpart exists (L2 N/A),
	// so restore L1 (cache) and L3 (device sampler addressing, stages 0-2:
	// 0 diffuse, 1 env, 2 spec) to the cb-entry snapshot. Idempotent for
	// callers that never set WRAP (restores what was already there).
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)state->textureaddress);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, vehiclePipe_blendToD3D(state->src, D3DBLEND_SRCALPHA));
		d3d9device->SetRenderState(D3DRS_DESTBLEND, vehiclePipe_blendToD3D(state->dst, D3DBLEND_INVSRCALPHA));
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)state->alphafunc));
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_TEXTUREFACTOR, state->texturefactor);
		// L3 mirror of the TEXTUREADDRESS restore above (see comment).
		for(int i = 0; i < 3; i++){
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSU, state->sampU[i]);
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSV, state->sampV[i]);
		}
	}
}

// Material common setup: alpha check, texture, matcol, surfProps
// Returns false if alpha==0 (skip this mesh)
static bool vehiclePipe_setupMaterial(RxD3D9InstanceData *inst, RwUInt32 flags,
	RpMaterial **outMaterial, RwBool *outAlpha)
{
	*outMaterial = inst->material;
	if((*outMaterial)->color.alpha == 0)
		return false;
	pipeSetTexture((*outMaterial)->texture, 0);
	*outAlpha = inst->vertexAlpha != 0 || (*outMaterial)->color.alpha != 255;
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)*outAlpha);
	// Layer-(2) mirror. RwRenderStateSet only touches the rw cache (L1); when
	// the cache already held the target the set no-ops entirely and layer (2)
	// stays at the Switch's forced FALSE baseline (pipeEnterAlphaMode sets
	// D3DRS_ALPHABLENDENABLE=FALSE through L2 at pipelinecommon.cpp:1120).
	// D3D9RenderDual gates its dual pass on RwD3D9GetRenderState(
	// D3DRS_ALPHABLENDENABLE) (main.cpp:165), so a desynced L2 skips the pass
	// and the mesh draws unblended — the opaque black rotor. Force the driver
	// cache; the applied[] flush then reaches the device at draw time.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, *outAlpha ? TRUE : FALSE);
	pipeUploadMatCol(flags, *outMaterial, REG_matCol);
	// Zero-init: [1] (the y/"spec" slot of the c20 contract documented at the
	// Env cb ~:1576) was never assigned below, so it uploaded raw stack
	// garbage to VS c20.y on every PS2/PC/Specular/mobile mesh. The Env cb
	// establishes the convention (memset(&surfProps,0,...) before its named
	// fills) — zero is the neutral value, and nothing the Switch binds reads
	// y today (vehiclePipeVS reads .x/.z/.w, mobile/vehicleVS read .z/.w),
	// so this can only ever remove an undefined value, never change output.
	float surfProps[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfProps[0] = (*outMaterial)->surfaceProps.ambient;
	surfProps[2] = (*outMaterial)->surfaceProps.diffuse;
	surfProps[3] = flags & rpGEOMETRYPRELIT ? 1.0f : 0.0f;
	RwD3D9SetVertexShaderConstant(REG_surfProps, surfProps, 1);
	return true;
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	CustomEnvMapPipeAtomicData *atmEnvData;
	CustomSpecMapPipeMaterialData *specData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv1, hasEnv2, hasSpec, hasAlpha;
	struct {
		float fxSwitch;
		float shininess;
		float specularity;
		float lightmult;
	} fxParams = {};
	RwMatrix envmat;
	RwV4d envXform;
	RwMatrix specmat;
	RwV3d specdir;
	RwMatrix lightmat;
	float transform[16];
	memset(&fxParams, 0, sizeof(fxParams));

	atomic = (RpAtomic*)object;

	_rwD3D9EnableClippingIfNeeded(object, type);

	float colorscale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorscale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);

	RwFrame *atomicFrame_ps2 = RpAtomicGetFrame(atomic);
	if(!atomicFrame_ps2){
		RwD3D9SetVertexShader(NULL);
		RwD3D9SetPixelShader(NULL);
		return;
	}
	if(flags & rpGEOMETRYLIGHT){
		RwMatrixInvert(&lightmat, RwFrameGetLTM(atomicFrame_ps2));
		uploadLights(&lightmat);
	}else
		uploadNoLights();

	vehiclePipe_setupResEntry(repEntry, &resEntryHeader, &instancedData, &numMeshes);

	VehicleRenderState state;
	vehiclePipe_saveRenderState(&state);

	noFx = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000;
	fxParams.lightmult = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
	CCustomCarEnvMapPipeline__SetupSpec(atomic, &specmat, &specdir);
	RwD3D9SetVertexShaderConstant(REG_specmat, &specmat, 3);
	RwD3D9SetVertexShaderConstant(REG_lightdir, &specdir, 1);

	for(; numMeshes--; instancedData++){
		if(!vehiclePipe_setupMaterial(instancedData, flags, &material, &hasAlpha))
			continue;

		// GTA IV vehicles: iv_mode preset (ivMode=1) OR PIPELINE_GTAIV
		// (vehiclePipe == CAR_GTAIV).
		//
		// Root cause of the broken IV look (verified by disassembling the
		// frozen resources/cso/GTAIVVehicle_vs.cso / GTAIVVehicle_ps.cso):
		//   - the frozen IV VS declares NO texcoord input; it derives UVs
		//     from the normal (c31/c32) -> diffuse smears across the body;
		//   - the frozen IV PS reads NO constants:
		//       out = v2.w*(env*v2 - d) + 2*d   (RGB doubled), alpha = d.a;
		//   - its sources were deleted in commit d2faefa (PS still carries
		//     a merge conflict) and tools/fast_build.py skips GTAIV VS
		//     entries, so the CSO cannot be rebuilt here.
		// Instead of the smeared frozen VS we pair gtaivVehiclePS with the
		// existing vehiclePBRVS (entry main_vehiclePBR, shaders/vehiclePipeVS.hlsl):
		//   PS v0 (texcoord bank) <- TEXCOORD0 = real UV
		//   PS v1 (color bank)    <- COLOR0    = saturate(lit)*matCol
		//                                 (same formula as the frozen IV VS)
		//   PS v2 (color1 bank)   <- COLOR1    = lerp(1,b^5,fresnel)*shininess
		// c21 (fxParams of that entry) = 0 forces COLOR1 to an exact 0 ->
		// the PS env term (v2.w * ...) collapses and stale s1 cannot wash
		// the mesh white; the surviving output is 2*diff*lit*matCol, so
		// matCol RGB is halved below (alpha untouched) to land on the
		// intended 1x brightness. IV's real-time env reflections keep
		// coming from the PS2-callback MatFX env FX pass below
		// (hasEnv1/hasEnv2 -> ps2CarFxVS + envData->texture) — unchanged.
		if(config->ivMode || config->vehiclePipe == CAR_GTAIV){
			if(vehiclePBRVS){
				RwD3D9SetVertexShader(vehiclePBRVS);
				RwD3D9SetPixelShader(gtaivVehiclePS);
				// c21 = fxParams {fresnel, -, -, shininess} -> COLOR1 = 0
				static const float ivFxZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				RwD3D9SetVertexShaderConstant(21, ivFxZero, 1);
				// world (c30-c33) + eye (c34) for main_vehiclePBR's guarded
				// world-space outputs (only TEXCOORD1-4, which the IV PS
				// does not read) — keep them finite/deterministic. Same
				// upload pattern as the Env cb at vehiclePipe.cpp:1542-1556;
				// pipeWorldMat was set by pipeGetComposedTransformMatrix
				// above (:635) and is still the one for THIS atomic. The FX
				// pass below stomps c30/c31/c32, but the next mesh's branch
				// re-establishes them before its own draw.
				float ivWorld[16];
				pipeGetWorldMatrix(ivWorld);
				RwD3D9SetVertexShaderConstant(30, ivWorld, 4);
				RwV3d ivEye = { 0.0f, 0.0f, 0.0f };
				RwCamera *ivCam = (RwCamera*)RWSRCGLOBAL(curCamera);
				if(ivCam){
					RwFrame *ivCamFrame = RwCameraGetFrame(ivCam);
					if(ivCamFrame){
						RwMatrix *ivCamLTM = RwFrameGetLTM(ivCamFrame);
						if(ivCamLTM) ivEye = ivCamLTM->pos;
					}
				}
				RwD3D9SetVertexShaderConstant(34, &ivEye, 1);
				// Compensate the frozen IV PS's RGB doubling (see above).
				// Overrides the pipeUploadMatCol() write from
				// vehiclePipe_setupMaterial() (:591) — same register, same
				// pass, deterministic order: this is the final value the
				// draw sees.
				RwRGBAReal ivMatCol;
				if(flags & rpGEOMETRYMODULATEMATERIALCOLOR)
					RwRGBARealFromRwRGBA(&ivMatCol, &material->color);
				else{
					ivMatCol.red = 1.0f; ivMatCol.green = 1.0f;
					ivMatCol.blue = 1.0f; ivMatCol.alpha = 1.0f;
				}
				ivMatCol.red *= 0.5f;
				ivMatCol.green *= 0.5f;
				ivMatCol.blue *= 0.5f;
				RwD3D9SetVertexShaderConstant(REG_matCol, &ivMatCol, 1);
				// PS constants kept for a future rebuilt IV PS: the frozen IV PS
				// reads NO constants (both uploads are inert today) — c0 gets the
				// fxParams struct as-is (NOT zeros), c1 colorScale = {1,0,0,0}.
				float psParams[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
				RwD3D9SetPixelShaderConstant(0, &fxParams, 1);
				RwD3D9SetPixelShaderConstant(1, psParams, 1);
			}else{
				// Legacy fallback: frozen GTAIVVehicle_vs.cso needs identity
				// envXform/envmat + zero fxParams (its fresnel/env interpolator
				// then dies and stale s1 cannot wash the mesh white). UV smear
				// is an intrinsic shader limitation — see report.
				RwD3D9SetVertexShader(gtaivVehicleVS);
				RwD3D9SetPixelShader(gtaivVehiclePS);
				static const float ivEnvXform[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
				static const float ivEnvMat[12] = {
					1.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 1.0f };
				static const float ivFxParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				RwD3D9SetVertexShaderConstant(REG_envXform, ivEnvXform, 1);
				RwD3D9SetVertexShaderConstant(REG_envmat, ivEnvMat, 3);
				RwD3D9SetVertexShaderConstant(REG_fxParams, ivFxParams, 1);
				float psParams[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
				RwD3D9SetPixelShaderConstant(0, &fxParams, 1);
				RwD3D9SetPixelShaderConstant(1, psParams, 1);
			}
		}else{
			RwD3D9SetVertexShader(vehiclePipeVS);
			RwD3D9SetPixelShader(simplePS);
		}

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);

		//
		// FX pass
		//
		if(noFx)
			continue;

		materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
		hasEnv1  = !!(materialFlags & 1);
		hasEnv2  = !!(materialFlags & 2);
		hasSpec  = !!(materialFlags & 4) && !renderingWheel;
if(betaEnvmaptest) hasSpec = false;
		// Needed: materialFlags bits can be set even when the matFX effect is not
		// envmap (bump/other effects); without this guard envData below would be invalid.
		if(RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTENVMAP){
			hasEnv1 = false;
			hasEnv2 = false;
			hasSpec = false;
		}

		int fxpass = 0;
		fxParams.shininess = 0.0f;
		fxParams.specularity = 0.0f;
		envData = *GETENVMAP(material);
		specData = *GETSPECMAP(material);
		if(hasEnv1){
			fxParams.fxSwitch = 1;
			fxParams.shininess = envData->GetShininess();
			CCustomCarEnvMapPipeline__SetupEnv(atomic, carfx_env1Frame, &carfx_env1Inv, &envmat);
			RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);	// is this needed?
			RwD3D9SetTexture(envData->texture, 1);
			CCustomCarEnvMapPipeline__Env1Xform(&envmat, envData, &envXform.x);
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
if(betaEnvmaptest){
	envXform.z *= envmap2tweak;
	envXform.w *= envmap2tweak;
}
			fxpass = 1;
		}else if(hasEnv2){
			fxParams.fxSwitch = 2;
			fxParams.shininess = envData->GetShininess();
			CCustomCarEnvMapPipeline__SetupEnv(atomic, carfx_env2Frame, &carfx_env2Inv, &envmat);
			RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);	// is this needed?
			RwD3D9SetTexture(envData->texture, 1);
			atmEnvData = CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData(atomic);
			if(!atmEnvData){
				dbglog("PS2_CB: atmEnvData NULL (pool full), skipping env2 for atomic %p", atomic);
				goto skip_fx_ps2;
			}
			CCustomCarEnvMapPipeline__Env2Xform(atomic, &envmat, envData, atmEnvData, &envXform.x);
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
			fxpass = 1;
		}

		if(hasSpec){
			fxParams.specularity = specData->specularity;
			RwD3D9SetTexture(specData->texture, 2);
			fxpass = 1;
		}

		if(fxpass){
			RwD3D9SetVertexShader(ps2CarFxVS);
			// ps2EnvSpecFxPS samples envMapTex(s1) * envcolor + maskTex(s2) *
			// speccolor — the binds above already put env on s1 and the spec
			// map on s2 (backup_original/vehiclePipe.cpp:524 did the same).
			// simplePS would sample s0 (diffuse) at env UVs instead — the
			// wrong-texture bug. NULL-guard: if the resource failed to load,
			// keep today's stable behaviour rather than binding a NULL PS.
			RwD3D9SetPixelShader(ps2EnvSpecFxPS ? ps2EnvSpecFxPS : simplePS);

			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);

			vehiclePipe_fxAdditiveBlend(resEntryHeader, instancedData, &state);
		}
	skip_fx_ps2:
		;
	}
	vehiclePipe_cleanup();
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_Specular(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	CustomEnvMapPipeAtomicData *atmEnvData;
	CustomSpecMapPipeMaterialData *specData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv1, hasEnv2, hasSpec, hasAlpha;
	struct {
		float fxSwitch;
		float shininess;
		float specularity;
		float lightmult;
	} fxParams = {};
	RwMatrix envmat;
	RwV4d envXform;
	RwV3d eye;
	RwMatrix lightmat;
	float transform[16];
	memset(&fxParams, 0, sizeof(fxParams));

	atomic = (RpAtomic*)object;

	_rwD3D9EnableClippingIfNeeded(object, type);

	float colorscale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorscale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(0, transform, 4);
	RwFrame *atomicFrame_spec = RpAtomicGetFrame(atomic);
	if(!atomicFrame_spec){ RwD3D9SetVertexShader(NULL); RwD3D9SetPixelShader(NULL); return; }
	RwMatrixInvert(&lightmat, RwFrameGetLTM(atomicFrame_spec));
	if(flags & rpGEOMETRYLIGHT)
		uploadLights(&lightmat);
	else
		uploadNoLights();

	vehiclePipe_setupResEntry(repEntry, &resEntryHeader, &instancedData, &numMeshes);

	VehicleRenderState state;
	vehiclePipe_saveRenderState(&state);

	noFx = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000;
	fxParams.lightmult = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
	RwFrame *camFrame = Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL;
	RwMatrix *camfrm = camFrame ? RwFrameGetLTM(camFrame) : NULL;
	if(camfrm)
		RwV3dTransformPoint(&eye, RwMatrixGetPos(camfrm), &lightmat);
	else
		eye = {0,0,0};
	RwD3D9SetVertexShaderConstant(REG_eye, &eye, 1);

	for(; numMeshes--; instancedData++){
		material = instancedData->material;
		if(!material) continue;

		if(!vehiclePipe_setupMaterial(instancedData, flags, &material, &hasAlpha))
			continue;

		RwD3D9SetVertexShader(vehiclePipeVS);
		RwD3D9SetPixelShader(simplePS);

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);

		//
		// FX pass
		//
		if(noFx)
			continue;

		materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
		hasEnv1  = !!(materialFlags & 1);
		hasEnv2  = !!(materialFlags & 2);
		hasSpec  = !!(materialFlags & 4) && !renderingWheel;
		if(RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTENVMAP){
			hasEnv1 = false;
			hasEnv2 = false;
			hasSpec = false;
		}

		int fxpass = 0;
		fxParams.shininess = 0.0f;
		fxParams.specularity = 0.0f;
		envData = *GETENVMAP(material);
		specData = *GETSPECMAP(material);
		if(hasEnv1){
			fxParams.fxSwitch = 1;
			fxParams.shininess = envData->GetShininess();
			CCustomCarEnvMapPipeline__SetupEnv(atomic, carfx_env1Frame, &carfx_env1Inv, &envmat);
			RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);	// is this needed?
			RwD3D9SetTexture(envData->texture, 1);
			CCustomCarEnvMapPipeline__Env1Xform(&envmat, envData, &envXform.x);
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
			fxpass = 1;
		}else if(hasEnv2){
			fxParams.fxSwitch = 2;
			fxParams.shininess = envData->GetShininess();
			CCustomCarEnvMapPipeline__SetupEnv(atomic, carfx_env2Frame, &carfx_env2Inv, &envmat);
			RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);
			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);	// is this needed?
			RwD3D9SetTexture(envData->texture, 1);
			atmEnvData = CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData(atomic);
			if(!atmEnvData){
				dbglog("SPEC_CB: atmEnvData NULL (pool full), skipping env2 for atomic %p", atomic);
				goto skip_fx_spec;
			}
			CCustomCarEnvMapPipeline__Env2Xform(atomic, &envmat, envData, atmEnvData, &envXform.x);
			envXform.z = envData->GetScaleX();
			envXform.w = envData->GetScaleY();
			fxpass = 1;
		}

		if(hasSpec){
			fxParams.specularity = specData->specularity;
			fxpass = 1;
		}

		if(fxpass){
			RwD3D9SetVertexShader(specCarFxVS);
			RwD3D9SetPixelShader(specCarFxPS);

			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);
			RwD3D9SetVertexShaderConstant(REG_envXform, &envXform, 1);

			vehiclePipe_fxAdditiveBlend(resEntryHeader, instancedData, &state);
		}
	skip_fx_spec:
		;
	}
	vehiclePipe_cleanup();
}

// OLD
enum {
	LOC_World = 0,
	LOC_View = 4,
	LOC_Proj = 8,
	LOC_WorldIT = 12,
	LOC_Texture = 16,
	LOC_matCol = 20,
	LOC_reflData = 21,
	LOC_envXForm = 22,
	LOC_sunDir = 23,
	LOC_sunDiff = 24,
	LOC_sunAmb = 25,
	LOC_surfProps = 26,
	LOC_lights = 27,
	LOC_envSwitch = 39,
	LOC_eye = 40,
};

// This re-implementation of the original code is perhaps a bit too slavish
void
SetupMaterial_Xbox(RpMaterial *material, RwBool lighting, RwUInt32 flags, RwBool blownUp, float specularity)
{
	static RwRGBA white = { 255, 255, 255, 255 };
	RwRGBA matColor;
	RwSurfaceProperties surf = material->surfaceProps;

	surf.specular = specularity;
	if(lighting && flags & rpGEOMETRYLIGHT){
		if(flags & rpGEOMETRYMODULATEMATERIALCOLOR)
			matColor = material->color;
		else
			matColor = white;
	}else{
		matColor = white;
		surf.ambient = 1.0f;
		surf.diffuse = 1.0f;
		surf.specular = 0.0f;
	}

	if(blownUp){
		surf.diffuse *= 0.1f;
		surf.ambient *= 0.1f;
		surf.specular = 0.0f;
	}

	RwRGBAReal color;
	RwRGBARealFromRwRGBA(&color, &matColor);
	RwD3D9SetVertexShaderConstant(LOC_matCol, (void*)&color, 1);
	// c26 surfProps — contract slot order (see "c20 SURFPROPS CONTRACT" at the
	// top of this file): {x:ambient, y:spec, z:diffuse, w:prelit}, exactly what
	// main_xboxCar reads (surfProps_xb.x = ambient, .y = spec, .z = diffuse).
	// Write the float4 BY NAME. The old raw `&surf` upload mapped slots through
	// RwSurfaceProperties' MEMORY layout (the SDK field order is {ambient,
	// specular, diffuse} — external/d3d9/rwplcore.h:1492; NOT the "ambient,
	// diffuse, specular" order the doxygen prose and buildingPipe.cpp:70
	// suggest) and read 4 bytes past the 3-float struct for .w (stack garbage).
	// Under the mis-read order the upload lands diffuse at .y and specular at
	// .z — the diffuse/specular swap vs main_xboxCar. Name-based slots make the
	// mapping layout-proof: writer and reader agree unconditionally.
	float surfProps[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	surfProps[0] = surf.ambient;
	surfProps[1] = surf.specular;	// = specularity, or 0 on the unlit/blownUp paths
	surfProps[2] = surf.diffuse;
	surfProps[3] = flags & rpGEOMETRYPRELIT ? 1.0f : 0.0f;	// contract .w (unread by main_xboxCar)
	RwD3D9SetVertexShaderConstant(LOC_surfProps, surfProps, 1);
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwBool lighting, blownUp;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	CustomEnvMapPipeAtomicData *atmEnvData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv1, hasEnv2, hasSpec, hasAlpha;
	D3DMATRIX worldMat, worldITMat, viewMat, projMat;
	float envSwitch;
	float specularity;

	_rwD3D9EnableClippingIfNeeded(object, type);

	atomic = (RpAtomic*)object;
	if(!atomic) return;

	RwFrame *atomicFrame_xbox = RpAtomicGetFrame(atomic);
	if(!atomicFrame_xbox) return;

	// Save render state at entry
	VehicleRenderState xboxState;
	vehiclePipe_saveRenderState(&xboxState);
	RwBool xboxZwrite;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &xboxZwrite);

	noFx = !!(CVisibilityPlugins__GetAtomicId(atomic) & 0x6000);
	blownUp = !((RpLightGetFlags(pDirect) & rpLIGHTLIGHTATOMICS) == 0 ||
	           (CVisibilityPlugins__GetAtomicId(atomic) & 0x4000) == 0);

	RwD3D9SetPixelShader(NULL);
	RwD3D9SetVertexShader(xboxCarVS);

	RwD3D9GetTransform(D3DTS_WORLD, &worldMat);
	RwD3D9GetTransform(D3DTS_VIEW, &viewMat);
	RwD3D9GetTransform(D3DTS_PROJECTION, &projMat);
	RwD3D9SetVertexShaderConstant(LOC_World,(void*)&worldMat,4);
	RwD3D9SetVertexShaderConstant(LOC_View,(void*)&viewMat,4);
	RwD3D9SetVertexShaderConstant(LOC_Proj,(void*)&projMat,4);
	_rwD3D9VSSetActiveWorldMatrix(RwFrameGetLTM(atomicFrame_xbox));
	_rwD3D9VSGetInverseWorldMatrix((void *)&worldITMat);
	RwD3D9SetVertexShaderConstant(LOC_WorldIT,(void*)&worldITMat,4);

	RwFrame *camFrame = Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL;
	RwMatrix *camfrm = camFrame ? RwFrameGetLTM(camFrame) : NULL;
	RwV3d eyePos = {0, 0, 0};
	if(camfrm)
		eyePos = *RwMatrixGetPos(camfrm);
	RwD3D9SetVertexShaderConstant(LOC_eye, (void*)&eyePos, 1);

	RwFrame *pDirectFrame = pDirect ? RpLightGetFrame(pDirect) : NULL;
	RwMatrix *pDirectLTM = pDirectFrame ? RwFrameGetLTM(pDirectFrame) : NULL;
	RwV3d sunDir = {0, 0, 0};
	if(pDirectLTM)
		sunDir = *RwMatrixGetAt(pDirectLTM);
	RwD3D9SetVertexShaderConstant(LOC_sunDir, (void*)&sunDir, 1);
	RwD3D9SetVertexShaderConstant(LOC_sunDiff, pDirect ? (void*)&pDirect->color : black4f, 1);

	// Interior/garage dampening for ambient
	extern bool CCullZones__PlayerNoRain(void);
	extern int* CGame__currArea;
	bool isInterior = (*CGame__currArea != 0);
	bool isSheltered = CCullZones__PlayerNoRain() && !isInterior;
	RwRGBAReal ambColor = GetTimecycleAmbient();
	if(isSheltered){
		ambColor.red *= 0.55f;
		ambColor.green *= 0.55f;
		ambColor.blue *= 0.55f;
	}
	RwD3D9SetVertexShaderConstant(LOC_sunAmb, &ambColor, 1);

	RwD3D9GetRenderState(D3DRS_LIGHTING, &lighting);

	vehiclePipe_setupResEntry(repEntry, &resEntryHeader, &instancedData, &numMeshes);

	for(; numMeshes--; instancedData++){
		material = instancedData->material;

		if(instancedData->material->color.alpha == 0)
			continue;

		envSwitch = 1;
		materialFlags = *(RwUInt32*)&material->surfaceProps.specular;

		hasEnv1  = (materialFlags & 1) && !noFx;
		hasEnv2  = (materialFlags & 2) && !noFx && (flags & rpGEOMETRYTEXTURED2);
		hasSpec  = (materialFlags & 4) && !noFx && !renderingWheel;

		hasAlpha = instancedData->vertexAlpha != 0 || instancedData->material->color.alpha != 255;
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);
		// Layer-(2) mirror — same class as setupMaterial/Env: this cb also
		// draws through D3D9RenderDual (:1396), whose pass gate reads
		// RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE). Without the driver-
		// cache write a mesh whose rw value was already set leaves L2 at the
		// forced FALSE baseline and draws unblended.
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE);

		RwD3D9SetTexture(NULL, 1);
		// Deterministic FFP baseline: non-env meshes must not inherit
		// stage1/stage2 color ops from a previous mesh or pipeline (with s1
		// unbound above, an inherited MULTIPLYADD would blend against an
		// undefined texel -> nondeterministic colours). The env branches
		// below re-enable stage1 and force stage2 off themselves.
		RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

		specularity = 0.0f;
		if(hasSpec && !blownUp){
			envSwitch = 4;
			specularity = CCustomCarEnvMapPipeline__m_EnvMapLightingMult * 1.8f;
			if(specularity > 1.0f) specularity = 1.0f;
		}

		envData = *GETENVMAP(material);
		if(blownUp){
			if(hasEnv2)
				envSwitch = 3;
		}else if(hasEnv1){
			envSwitch = 5;
			if(!hasSpec) envSwitch = 2;
			static D3DMATRIX texMat;
			float trans[2];
			RwInt32 tfactor;

			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			texMat._11 = envData->GetScaleX();
			texMat._22 = envData->GetScaleY();
			texMat._33 = 1.0f;
			texMat._44 = 1.0f;
			CCustomCarEnvMapPipeline__Env1Xform_PC(atomic, envData, trans);
			texMat._31 = trans[0];
			texMat._32 = trans[1];
			RwD3D9SetVertexShaderConstant(LOC_Texture, (void*)&texMat, 4);
			RwD3D9SetTexture(envData->texture, 1);
			tfactor = CCustomCarEnvMapPipeline__m_EnvMapLightingMult * 96.0f;
			if(tfactor > 255)
				tfactor = 255;
			RwD3D9SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_XRGB(tfactor,tfactor,tfactor));
			RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
			RwD3D9SetTextureStageState(1, D3DTSS_COLORARG0, D3DTA_CURRENT);
			RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
			RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		}else if(hasEnv2){
			envSwitch = 6;
			if(!hasSpec) envSwitch = 3;
			static D3DMATRIX texMat;
			float trans[2] = { 0, 0 };
			RwInt32 tfactor;

			RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
			atmEnvData = CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData(atomic);
			if(!atmEnvData){
				dbglog("XBOX_CB: atmEnvData NULL (pool full), skipping env2 for atomic %p", atomic);
				goto skip_fx_xbox;
			}
			CCustomCarEnvMapPipeline__Env2Xform_PC(atomic, envData, atmEnvData, trans);
			texMat._11 = 1.0f;
			texMat._22 = 1.0f;
			texMat._33 = 1.0f;
			texMat._44 = 1.0f;
			texMat._31 = trans[0];
			texMat._32 = trans[1];
			RwD3D9SetVertexShaderConstant(LOC_Texture, (void*)&texMat, 4);
			RwD3D9SetTexture(envData->texture, 1);
			tfactor = CCustomCarEnvMapPipeline__m_EnvMapLightingMult * 24.0f;
			if(tfactor > 255) tfactor = 255;
			RwD3D9SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(tfactor,255,255,255));
			RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_BLENDFACTORALPHA);
			RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
			RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
			RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		}
	skip_fx_xbox:

		RwD3D9SetVertexShaderConstant(LOC_envSwitch, (void*)&envSwitch, 1);

		SetupMaterial_Xbox(material, lighting, flags, blownUp, specularity);

		if(flags & (rxGEOMETRY_TEXTURED2 | rxGEOMETRY_TEXTURED)){
			RwD3D9SetTexture(material->texture, 0);
			RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			RwD3D9SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		}else{
			RwD3D9SetTexture(NULL, 0);
			RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
			RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		}
		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);
	}
	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	// Restore render state
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)xboxState.alphafunc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)xboxState.src);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)xboxState.dst);
	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)xboxState.fog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)xboxZwrite);
	// Same TEXTUREADDRESS leak class as the PS2/spec cb (sets at :1325/:1354
	// pushed WRAP through the rw cache only). This cb never routes through
	// vehiclePipe_fxAdditiveBlend, so restore L1 here; the L3 sampler state
	// is re-authored by RW's own texture-apply on every later bind.
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)xboxState.textureaddress);
	if(d3d9device){
		for(int i = 0; i < 3; i++){
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSU, xboxState.sampU[i]);
			d3d9device->SetSamplerState(i, D3DSAMP_ADDRESSV, xboxState.sampV[i]);
		}
	}
}



void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_leeds(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv, hasAlpha;
	struct {
		float fxSwitch;
		float shininess;
		float specularity;
		float lightmult;
	} fxParams = {};
	float envmat[16];
	RwV3d eye;
	RwMatrix lightmat;
	float transform[16];
	memset(&fxParams, 0, sizeof(fxParams));

	atomic = (RpAtomic*)object;
	if(!atomic) return;

	RwFrame *atomicFrame_leeds = RpAtomicGetFrame(atomic);
	if(!atomicFrame_leeds) return;

	_rwD3D9EnableClippingIfNeeded(object, type);

	float colorscale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorscale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(0, transform, 4);
	RwMatrixInvert(&lightmat, RwFrameGetLTM(atomicFrame_leeds));
	if(flags & rpGEOMETRYLIGHT)
		uploadLights(&lightmat);
	else
		uploadNoLights();

	vehiclePipe_setupResEntry(repEntry, &resEntryHeader, &instancedData, &numMeshes);

	VehicleRenderState state;
	vehiclePipe_saveRenderState(&state);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &state.fog);	// Leeds saves fog enable, not fog color

	noFx = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000;
	fxParams.lightmult = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
	RwFrame *camFrame = Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL;
	RwMatrix *camfrm = camFrame ? RwFrameGetLTM(camFrame) : NULL;
	if(camfrm)
		RwV3dTransformPoint(&eye, RwMatrixGetPos(camfrm), &lightmat);
	else
		eye = {0,0,0};
	RwD3D9SetVertexShaderConstant(REG_eye, &eye, 1);

	pipeGetLeedsEnvMapMatrix(atomic, envmat);
	RwD3D9SetVertexShaderConstant(REG_envmat, &envmat, 3);
	RwD3D9SetVertexShaderConstant(REG_envtexmat, &envtexmat, 4);

	for(; numMeshes--; instancedData++){
		if(!vehiclePipe_setupMaterial(instancedData, flags, &material, &hasAlpha))
			continue;
		// Leeds-specific: clamp surfProps[0]. Same zero-init rationale as
		// vehiclePipe_setupMaterial — [1] was never written, so VS c20.y got
		// stack garbage on this path too.
		float surfProps[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		surfProps[0] = material->surfaceProps.ambient;
		if(surfProps[0] > 0.1f && surfProps[0] < 1.0f)
			surfProps[0] = 1.0f;
		surfProps[2] = material->surfaceProps.diffuse;
		surfProps[3] = !!(flags & rpGEOMETRYPRELIT);
		RwD3D9SetVertexShaderConstant(REG_surfProps, surfProps, 1);

		RwD3D9SetVertexShader(vehiclePipeVS);
		RwD3D9SetPixelShader(simplePS);

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);

		//
		// FX pass
		//
		materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
		hasEnv  = !!(materialFlags & 3);
		if(RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTENVMAP)
			hasEnv = false;
		if(noFx || !hasEnv)
			continue;

		RwD3D9SetVertexShader(leedsCarFxVS);

		envData = *GETENVMAP(material);

		fxParams.lightmult = 1.0f;
		fxParams.shininess = envData ? envData->GetShininess() * 3.0f * config->leedsShininessMult : 0.0f;
		if(fxParams.shininess > 1.0f)
			fxParams.shininess = 1.0f;
		fxParams.shininess *= 0.5f;

		RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);

		RwD3D9SetTexture(reflectionTex, 0);

		// Snapshot the rw-side VERTEXALPHA that setupMaterial just programmed,
		// so the FX pass can hand it back on every exit path (same discipline
		// as vehiclePipe_fxAdditiveBlend).
		RwUInt32 savedVtxAlpha = FALSE;
		RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);

		DWORD leedsDstBlend = (config->vehiclePipe == 6)	// VCS
			? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE;

		// Three-layer doctrine (rw cache L1 + RwD3D9SetRenderState L2 + device
		// L3): this block used to poke ONLY the rw cache, so an applied[]-gated
		// no-op could leave L2/L3 on the previous mesh's blend/zwrite while the
		// cache claimed the FX values — the ZWRITE desyncs the drift log kept
		// repairing. Every state is forced through all three layers.
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, (RwUInt32)D3DCMP_ALWAYS);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, (RwUInt32)D3DBLEND_SRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND,
			(void*)(config->vehiclePipe == 6 ? rwBLENDINVSRCALPHA : rwBLENDONE));
		RwD3D9SetRenderState(D3DRS_DESTBLEND, (RwUInt32)leedsDstBlend);
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
			d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			d3d9device->SetRenderState(D3DRS_DESTBLEND, leedsDstBlend);
		}

		D3D9Render(resEntryHeader, instancedData);

		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)state.dst);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, vehiclePipe_blendToD3D(state.dst, D3DBLEND_INVSRCALPHA));
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)state.src);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, vehiclePipe_blendToD3D(state.src, D3DBLEND_SRCALPHA));
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)state.fog);
		RwD3D9SetRenderState(D3DRS_FOGENABLE, (RwUInt32)(state.fog != 0));
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state.alphafunc);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)state.alphafunc));
		// Close the VERTEXALPHA=TRUE leak through all three layers (L1 read
		// back per mesh; L2 is what D3D9RenderDual's gate reads). The FX states
		// are only written on the single path that reaches this restore — the
		// noFx/!hasEnv `continue` above happens before any of them — so this
		// covers all exit paths.
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_DESTBLEND, vehiclePipe_blendToD3D(state.dst, D3DBLEND_INVSRCALPHA));
			d3d9device->SetRenderState(D3DRS_SRCBLEND, vehiclePipe_blendToD3D(state.src, D3DBLEND_SRCALPHA));
			d3d9device->SetRenderState(D3DRS_FOGENABLE, state.fog != 0 ? TRUE : FALSE);
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D((RwUInt32)state.alphafunc));
			d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedVtxAlpha ? TRUE : FALSE);
		}
	}
	vehiclePipe_cleanup();
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_mobile(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv1, hasEnv2, hasSpec, hasAlpha;
	struct {
		float fxSwitch;
		float shininess;
		float specularity;
		float lightmult;
	} fxParams = {};
	RwV3d eye;
	RwMatrix lightmat;
	float transform[16];
	memset(&fxParams, 0, sizeof(fxParams));

	atomic = (RpAtomic*)object;
	if(!atomic) return;

	RwFrame *atomicFrame_m = RpAtomicGetFrame(atomic);
	if(!atomicFrame_m) return;

	_rwD3D9EnableClippingIfNeeded(object, type);

	float colorscale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorscale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(0, transform, 4);
	RwMatrixInvert(&lightmat, RwFrameGetLTM(atomicFrame_m));
	if(flags & rpGEOMETRYLIGHT)
		uploadLights(&lightmat);
	else
		uploadNoLights();

	vehiclePipe_setupResEntry(repEntry, &resEntryHeader, &instancedData, &numMeshes);

	noFx = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000;
	fxParams.lightmult = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;

	RwCamera *curCam_m = (RwCamera*)RWSRCGLOBAL(curCamera);
	if(curCam_m){
		RwFrame *cf_m = RwCameraGetFrame(curCam_m);
		if(cf_m){
			RwMatrix *cmLTM_m = RwFrameGetLTM(cf_m);
			if(cmLTM_m) eye = cmLTM_m->pos;
			else eye = {0,0,0};
		} else eye = {0,0,0};
	} else eye = {0,0,0};
	RwD3D9SetVertexShaderConstant(REG_eye, &eye, 1);
	float worldmat[16];
	RwToD3DMatrix(worldmat, RwFrameGetLTM(atomicFrame_m));
	RwD3D9SetVertexShaderConstant(31, worldmat, 4);	// world mat


	// Try to get mobile direct color
	float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if(pDirect){
		c[0] = pDirect->color.red * 1.28f * 1.5f;
		c[1] = pDirect->color.green * 1.28f * 1.5f;
		c[2] = pDirect->color.blue * 1.28f * 1.5f;
	}
	RwD3D9SetVertexShaderConstant(REG_directCol, (void*)c, 1);


	for(; numMeshes--; instancedData++){
		if(!vehiclePipe_setupMaterial(instancedData, flags, &material, &hasAlpha))
			continue;
		bool lighttex = material->texture && strstr(material->texture->name, "vehiclelights");

		materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
		hasEnv1  = !!(materialFlags & 1);
		hasEnv2  = !!(materialFlags & 2);
		hasSpec  = !!(materialFlags & 4) && !renderingWheel;
		if(noFx || RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTENVMAP){
			hasEnv1 = false;
			hasEnv2 = false;
			hasSpec = false;
		}
		// Try to get rid of reflections on light textures, not perfect but they don't look too nice
		if(lighttex){
			hasEnv1 = false;
			hasEnv2 = false;
		}
		envData = *GETENVMAP(material);

		if(hasEnv1 || hasEnv2 || hasSpec){
			float shininess = envData ? envData->GetShininess() : 0.0f;

			if(!hasAlpha){
				float sum = material->color.red + material->color.green + material->color.blue;
				float envmult = (255.0f - sum)*2.0f/255.0f;
				if(envmult <= 1.0f){
					if(material->color.red == 255)
						envmult = 1.0f;
					else{
						envmult = (sum/600.0f) * (sum/600.0f);
						if(envmult <= 1.0f)
							envmult = 1.0f;
					}
				}
				shininess *= envmult;
			}

			//shininess *= 1.5f;	// this only happens for the LQ env map

			// the window effect seems to be a bit overblown in the mobile version
			// on ps3 it looks a bit more pleasant
			if(hasAlpha)
			//	shininess *= 5.0f;	// mobile
				shininess *= 2.5f*fxParams.lightmult;	// see if this works out

			if(shininess > 0.32f) shininess = 0.32f;
			if(shininess < 0.01f) shininess = 0.01f;
			shininess *= 1.4f;

			// shininess *= 0.5f;	// not sure if this is done or not, i think not

			fxParams.shininess = (hasEnv1 || hasEnv2) ? shininess : 0.0f;
			// lets have some more specularity, our light seems to be a bit
			// darker so compensate for that
			fxParams.specularity = hasSpec ? shininess*1.5f : 0.0f;
			// DEAD CODE: fxParams.lightmult (c30.w) is uploaded here but the
			// mobile VS (mobileVehicleVS) defines lightmult without using it,
			// and the mobile PS (main_mobileVehicle) only reads fxParams.y.
			// The C++ usage at line 1344 (shininess *= 2.5f*lightmult) is live,
			// but the .w component of the uploaded constant is never consumed.
			RwD3D9SetVertexShaderConstant(REG_fxParams, &fxParams, 1);
			// The COMPILED mobileVehiclePS.cso reads fxParams at PS c1
			// (c1.y = env lerp factor, verified via fxc /dumpbin), NOT at
			// REG_fxParams (30). Uploading to c30 left c1 holding stale
			// cross-frame postfx values — lerp extrapolated toward the env
			// map color -> washed-white mobile vehicles.
			RwD3D9SetPixelShaderConstant(1, &fxParams, 1);
			pipeSetTexture(reflectionTex, 1);
			RwD3D9SetVertexShader(mobileVehiclePipeVS);
			RwD3D9SetPixelShader(mobileVehiclePipePS);
		}else{
			RwD3D9SetVertexShader(vehiclePipeVS);
			RwD3D9SetPixelShader(simplePS);
		}

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);
	}
	vehiclePipe_cleanup();
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	if(!repEntry || !object)
		return;
	RxD3D9ResEntryHeader *resEntryHeader;
	RxD3D9InstanceData *instancedData;
	RpAtomic *atomic;
	RwInt32	numMeshes;
	RwBool noFx;
	CustomEnvMapPipeMaterialData *envData;
	CustomSpecMapPipeMaterialData *specData;
	RpMaterial *material;
	RwUInt32 materialFlags;
	RwBool hasEnv1, hasEnv2, hasSpec, hasAlpha;
	struct {
		float fresnel;
		float power;
		float lightmult;
		float shininess;
	} fxParams;
	// C1 FIX: member order must match the VS contract at c20 — canonical
	// write-up at the top of this file ("c20 SURFPROPS CONTRACT"): every
	// sibling upload writes {x:ambient, y:spec, z:diffuse, w:prelit}
	// (setupMaterial writes [0]/[2]/[3]; vehiclePipeVS reads surfDiff = .z).
	// The old {ambient, diffuse, specular, prelight} order put diffuse at y
	// and specular (usually 0) at z → VS directional loop multiplied by spec
	// → dark/wrong vehicles. Fills below assign by member name, so they land
	// in the correct slots after this reorder.
	struct {
		float ambient;
		float specular;
		float diffuse;
		float prelight;
	} surfProps;
	RwV3d eye;
	RwMatrix lightmat;
	float transform[16];
	memset(&fxParams, 0, sizeof(fxParams));
	memset(&surfProps, 0, sizeof(surfProps));

	atomic = (RpAtomic*)object;
	if(!atomic) return;

	RwFrame *atomicFrame = RpAtomicGetFrame(atomic);
	if(!atomicFrame){
		static int logOnce = 0;
		if(!logOnce){ dbglog("ENV_CB: NULL frame for atomic %p, skipping", atomic); logOnce++; }
		return;
	}

	_rwD3D9EnableClippingIfNeeded(object, type);

	// Render the IBL cubemap once per frame if no PBR building cb claimed
	// it first: this Env cb is the only OTHER g_iblTex consumer (s3 binds at
	// the opaque path ~:2145 and the rubber path ~:1925), and with
	// buildingPipe != PBR nothing else renders it → s3 never binds → black
	// vehicle IBL ("combo C"). Shared frame stamp in pipeEnsureIBLBuffer
	// makes this idempotent against the PBR building cb's call.
	// Skipped under gRenderingSpheremap: this cb also runs inside the
	// sphere-map pass (see the s1 feedback guards below), where a main-
	// camera Im2D pass must not execute. The stamp is NOT consumed on the
	// skipped path, so the first normal invocation later in the frame still
	// renders it.
	if(!gRenderingSpheremap)
		pipeEnsureIBLBuffer();

	// Re-assert the pipe cull — mirror of buildingPipe.cpp:731. This cb
	// runs pipeEnsureIBLBuffer above (fullscreen Im2D pass:
	// ImmediateModeRenderStatesSet parks rw cull at NONE and the pass's
	// raw D3DRS_CULLMODE writes bypass the rw cache), then keeps drawing
	// vehicle geometry. If the caches still say BACK while the device
	// kept the pass's leftover value, a plain rw set no-ops (double
	// no-op) and vehicles render double-sided / wrongly culled. Force all
	// three cull layers back to BACK through pipeForceCullMode.
	pipeForceCullMode(rwCULLMODECULLBACK);

	// ...and re-assert the ALPHA block for the same reason. The
	// pipeEnsureIBLBuffer() Im2D pass above restores the raw alpha/blend
	// states from a device-side snapshot (postfx.cpp Save/RestoreRawGeom
	// States) which can disagree with the driver cache, and the Switch's
	// pipeForceAlphaBlock ran BEFORE it — mirror of buildingPipe.cpp:961,
	// which exists for exactly this ordering. Without it this cb keeps
	// drawing vehicles with whatever alpha-test/ref/blend the pass left:
	// alpha-test off at the device -> cutout shells and glass show their
	// transparent texels, ref off-by-a-domain -> grey posterized blobs.
	pipeForceAlphaBlock();

	// Per-frame debug logging (throttle to once per ~60 seconds)
	static unsigned int vehLogCounter = 0;
	bool vehLogThisFrame = (vehLogCounter++ % 3600 == 0);

	if(vehLogThisFrame){
		RwUInt32 frame = RWSRCGLOBAL(renderFrame);
		dbglog("[VehiclePBR] === FRAME %u ===", frame);
		dbglog("[VehiclePBR] atomic=%p flags=%X", atomic, flags);
		dbglog("[VehiclePBR] shaders: VS=%p PS=%p", vehiclePBRVS, VehiclePBR_Modern);
		dbglog("[VehiclePBR] iCanHasNeoCar=%d iCanHasbuildingPipe=%d", iCanHasNeoCar, iCanHasbuildingPipe);
		// Env map texture diagnostics
		extern RwTexture *reflectionTex;
		extern RwRaster *envFB;
		if(reflectionTex){
			RwRaster *r = RwTextureGetRaster(reflectionTex);
			dbglog("[VehiclePBR] refTex=%p raster=%p %dx%d", reflectionTex, r, r ? r->width : 0, r ? r->height : 0);
		}else{
			dbglog("[VehiclePBR] refTex=NULL");
		}
		dbglog("[VehiclePBR] envFB=%p %dx%d", envFB, envFB ? envFB->width : 0, envFB ? envFB->height : 0);
	}

	float colorscale = 1.0f;
	RwD3D9SetPixelShaderConstant(0, &colorscale, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(0, transform, 4);
	pipeGetWorldMatrix(transform);
	RwD3D9SetVertexShaderConstant(30, transform, 4);

	RwCamera *curCam = (RwCamera*)RWSRCGLOBAL(curCamera);
	if(curCam){
		RwFrame *camFrame = RwCameraGetFrame(curCam);
		if(camFrame){
			RwMatrix *camLTM = RwFrameGetLTM(camFrame);
			if(camLTM) eye = camLTM->pos;
			else eye = {0,0,0};
		} else eye = {0,0,0};
	} else eye = {0,0,0};
	RwD3D9SetVertexShaderConstant(34, &eye, 1);
	RwD3D9SetPixelShaderConstant(2, &eye, 1);
	// PS c25-c27 = view rotation for SphereEnvMapUV. Opaque main() uploads
	// these per-mesh, but Glass_Vehicle also reads them and the glass path
	// never set them — with Building=PBR first in the frame they hold
	// stale/zero values from the building pass, so glass env UVs collapse
	// and the window quads wash white (ghost-car look). Upload once here
	// for every Env-cb mesh path.
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
	RwMatrixInvert(&lightmat, RwFrameGetLTM(atomicFrame));
	if(flags & rpGEOMETRYLIGHT)
		uploadLights(&lightmat);
	else
		uploadNoLights();

	//CVector *sunPos = (CVector*)(*(DWORD*)0xB79FD0 * 12 + 0xB7CA50);
	//RwD3D9SetPixelShaderConstant(3, &sunPos, 1);

	resEntryHeader = (RxD3D9ResEntryHeader *)(repEntry + 1);
	instancedData = (RxD3D9InstanceData *)(resEntryHeader + 1);
	if(resEntryHeader->indexBuffer != NULL)
		RwD3D9SetIndices(resEntryHeader->indexBuffer);
	_rwD3D9SetStreams(resEntryHeader->vertexStream,resEntryHeader->useOffsets);
	RwD3D9SetVertexDeclaration(resEntryHeader->vertexDeclaration);
	numMeshes = resEntryHeader->numMeshes;

	int alphafunc;
	int src, dst;
	int fog;
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
	RwRenderStateGet(rwRENDERSTATEFOGCOLOR, &fog);

	noFx = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000 || !(flags & rpGEOMETRYLIGHT);
	fxParams.fresnel = config->envFresnel;
	fxParams.power = config->envPower;
	fxParams.lightmult = CCustomCarEnvMapPipeline__m_EnvMapLightingMult;

	// Part A3 + Part B — PS c44 = {chromeClearcoat, envSpecularityMult, 0, 0}.
	// Single upload site for the whole Env cb (before the mesh loop, same
	// region as fxParams init) — D3D9 PS constants are device-global, so this
	// must be re-uploaded every cb invocation (mobileBuildingVS uses c44 as VS
	// campos, but c44 is untouched in any PS — safe). Separate register from
	// the fix-10 c22/c23 single-upload site (pipeUploadPBR), so no conflict.
	{
		// REFLECTION REGISTER CONTRACT (see VehiclePBR_Modern.hlsl c44 note):
		//   x chromeClearcoat, y envSpecularityMult — unchanged
		//   z vehEnvStrengthMode (0/1): intensity -> Fresnel coverage instead
		//     of env content (energy-consistent; identical at intensity 1.0)
		//   w vehEnvGlint (0..1): standalone Mobile-style sun glint, 0 = off
		float chromeParams[4] = {
			config->chromeClearcoat,
			config->envSpecularityMult,
			config->vehEnvStrengthMode ? 1.0f : 0.0f,
			config->vehEnvGlint
		};
		RwD3D9SetPixelShaderConstant(44, chromeParams, 1);
	}
	// Mask term is CPU-gated on the TXD actually loading (see the c20.w upload
	// below) — say so once instead of silently losing the PS2/Specular/Neo
	// mask parity this toggle is supposed to restore.
	if(config->vehEnvMask && !CarPipe::reflectionMask &&
	   dbglog_throttle("VehEnvMask"))
		dbglog("[VehiclePBR] vehEnvMask=1 but CarReflectionMask is NULL — mask term off");

	// VS c28.x = vehPrelightFallback — the non-prelit lit-colour floor the
	// main_vehiclePBR VS applies (see the branch at vehiclePipeVS.hlsl and
	// the config note in skygfx.h). Uploaded once per cb, BEFORE the mesh
	// loop, so every path that binds vehiclePBRVS (opaque paint, glass, the
	// rubber/tire branch) reads the same value; D3D9 VS constants are
	// device-global, so it must be re-sent on every cb invocation.
	{
		float litFallback[4] = { config->vehPrelightFallback, 0.0f, 0.0f, 0.0f };
		RwD3D9SetVertexShaderConstant(28, litFallback, 1);
	}

	surfProps.prelight = flags & rpGEOMETRYPRELIT ? 1.0f : 0.0f;

	// Per pixel lights (uploaded once before mesh loop)
	pipeUploadLightColorForcePS(pDirect, REG_directCol);
	pipeUploadLightDirectionForcePS(pDirect, REG_directDir);
	for(int i = 0; i < 6; i++)
		if(i < NumExtraDirLightsInWorld && RpLightGetType(pExtraDirectionals[i]) == rpLIGHTDIRECTIONAL){
			pipeUploadLightColorPS(pExtraDirectionals[i], REG_directCol+i+1);
			pipeUploadLightDirectionPS(pExtraDirectionals[i], REG_directDir+i+1);
		}else{
			pipeUploadZeroPS(REG_directCol+i+1);
			pipeUploadZeroPS(REG_directDir+i+1);
		}


	for(; numMeshes--; instancedData++){
		material = instancedData->material;

		if(instancedData->material->color.alpha == 0)
			continue;

		pipeSetTexture(material->texture, 0);

		hasAlpha = instancedData->vertexAlpha != 0 || instancedData->material->color.alpha != 255;
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)hasAlpha);
		// Layer-(2) mirror — same desync as vehiclePipe_setupMaterial: the rw
		// set no-ops against an unchanged cache and leaves D3DRS_ALPHABLENDENABLE
		// at the Switch's FALSE baseline, so the opaque path's D3D9RenderDual
		// gate reads blend-off and drops the dual pass. Force L2 so the
		// per-mesh alpha actually reaches the gate and the device flush.
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, hasAlpha ? TRUE : FALSE);

	// ================================================================
	// Vehicle glass system (skygfx core — like carcols/timecycle)
	// All color/tint data computed here, passed to shader as constants
	// ================================================================

	// ================================================================
	// Vehicle subsystem — all classification via veh_shaders bridge
	// ================================================================
	const char *texName = material->texture ? material->texture->name : "";
	int modelIndex = VehShaders_GetModelIndex(object);

	bool isGlassMesh = VehShaders_IsGlassTexture(texName, hasAlpha, material->color.alpha);
	// Ghost-car guard: IsGlassTexture's alpha<200 fallback also fires for
	// body panels whose material alpha is <200. Positive window/glass name
	// matches classify as SURFACE_CAR_GLASS, not BODY — so BODY + glass
	// means the fallback claimed a paint mesh: it then draws through
	// Glass_Vehicle with fresnel alpha 0.3-0.8 -> translucent white ghost.
	// Keep the fallback for empty/unknown names (modded glass with no
	// window/glass/windscreen substring) only.
	if(isGlassMesh && texName && texName[0] &&
	   VehShaders_GetSurfaceType(texName) == SURFACE_CAR_BODY)
		isGlassMesh = false;
	bool isHeadlight = !isGlassMesh && VehShaders_IsHeadlightTexture(texName);
	bool isTaillight = !isGlassMesh && !isHeadlight && VehShaders_IsTaillightTexture(texName);
	bool isLightMesh = isHeadlight || isTaillight;
	bool isTireMesh  = !isGlassMesh && !isLightMesh && Rubber_Vehicle && VehShaders_IsTireTexture(texName);

	// ================================================================
	// GLASS + LIGHT PATH — alpha-blended glass shader
	// ================================================================
	// Register audit (finding #3): the things this branch deliberately does
	// NOT set are exactly the ones Glass_Vehicle never declares.
	// Glass_Vehicle.hlsl reads only s0/s1 plus c0, c1, c22, c23 and c25-c27 —
	// so PS c24 (ambientColor), the c45/c48-c111 Forward+ block, c46 (layer
	// bitmask), c28/c29, and s2/s3/s4/s5 have no consumer here and uploading
	// them would be dead weight rather than a fix. c25-c27 (view rotation)
	// ARE needed and are uploaded once for EVERY Env-cb mesh path at :1690 —
	// that was the earlier "glass env UVs collapse -> washed white" bug.
	// The VS side (main_vehiclePBR, shared by glass/rubber/opaque) does need
	// c4/c5/c12 (lights, cb level :1701) and c28 (lit-colour floor, :1747);
	// both are uploaded before the mesh loop, so all three paths inherit them.
	if((isGlassMesh || isLightMesh) && Glass_Vehicle){		float opacity = (float)material->color.alpha / 255.0f;

		if(isLightMesh){
			float ltR, ltG, ltB;
			if(isTaillight)
				VehShaders_GetTaillightTint(modelIndex, &ltR, &ltG, &ltB);
			else
				VehShaders_GetHeadlightTint(modelIndex, &ltR, &ltG, &ltB);

			pipeUploadPBR(ltR, ltG, ltB, opacity, 0.0f, 0.0f, 0.0f);
			float lightP[4] = { 1.0f, isTaillight ? 1.5f : 1.2f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(23, lightP, 1);
			// Diagnostic: which path/tint the light mesh actually took
			{
				static int lgtLog = 0;
				if(lgtLog++ % 300 == 0)
					dbglog("[VehLight] frame=%u tex='%s' head=%d tail=%d tint=(%.2f,%.2f,%.2f) alpha=%d",
						RWSRCGLOBAL(renderFrame), texName ? texName : "", isHeadlight, isTaillight, ltR, ltG, ltB, material->color.alpha);
			}
		}else{
			float gtR, gtG, gtB, gtStr;
			VehShaders_GetGlassTint(modelIndex, &gtR, &gtG, &gtB, &gtStr);

			pipeUploadPBR(gtR, gtG, gtB, opacity, 0.0f, 0.0f, 0.0f);
			float lightP[4] = { 0.0f, 0.0f, gtStr, 0.0f };
			RwD3D9SetPixelShaderConstant(23, lightP, 1);
			// Diagnostic: flag light-looking textures that were classified GLASS
			// (they'd get the window tint — the coupling we're hunting)
			{
				static int glsLog = 0;
				bool looksLight = texName && (strstr(texName, "light") || strstr(texName, "Light"));
				if(looksLight && glsLog++ % 300 == 0)
					dbglog("[VehGlass] frame=%u GLASS-CLASSIFIED-LIGHT tex='%s' alpha=%d tint=(%.2f,%.2f,%.2f)",
						RWSRCGLOBAL(renderFrame), texName, material->color.alpha, gtR, gtG, gtB);
			}
		}

		pipeUploadMatCol(flags, material, REG_matCol);
		// sRGB→linear for glass tint (same as opaque paint path)
		RwRGBAReal glassMatColRGBA;
		if(flags & rpGEOMETRYMODULATEMATERIALCOLOR){
			RwRGBARealFromRwRGBA(&glassMatColRGBA, &material->color);
			glassMatColRGBA.red   = powf(glassMatColRGBA.red,   2.2f);
			glassMatColRGBA.green = powf(glassMatColRGBA.green, 2.2f);
			glassMatColRGBA.blue  = powf(glassMatColRGBA.blue,  2.2f);
			RwD3D9SetPixelShaderConstant(REG_matCol, &glassMatColRGBA, 1);
		}
		surfProps.ambient = material->surfaceProps.ambient;
		surfProps.specular = 0.0f; // C1: glass/light path has no spec term (stale-mesh guard)
		surfProps.diffuse = material->surfaceProps.diffuse;
		RwD3D9SetVertexShaderConstant(REG_surfProps, &surfProps, 1);
		RwD3D9SetPixelShaderConstant(0, &surfProps, 1);

		// VS c21 = {fresnel, 0, 0, shininess}. The VS multiplies its env fresnel
		// rim by .w (EnvColor.a) and the glass/lights PS reads that as env
		// intensity. Uploading envPower (~20) here blew glass/lights out to white;
		// a shininess-scale 1.0 keeps EnvColor.a in the 0..1 range.
		float glassFxVS[4] = { config->envFresnel, 0, 0, 1.0f };
		RwD3D9SetVertexShaderConstant(21, glassFxVS, 1);
		// PS c1.xy = camera view-window tanHalfFov — the glass PS projects its
		// env UVs with the same perspective projection the body path uses
		// (see Glass_Vehicle.hlsl); .z keeps lightmult for parity.
		float gtanX = 0.65f, gtanY = 0.45f;
		if(Scene.camera){ gtanX = Scene.camera->viewWindow.x; gtanY = Scene.camera->viewWindow.y; }
		float glassFxPS[4] = { gtanX, gtanY, fxParams.lightmult, 0 };
		RwD3D9SetPixelShaderConstant(1, glassFxPS, 1);

		// Env map on stage 1 — guard against D3D9 feedback loop during sphere render
		if(!gRenderingSpheremap)
			pipeSetTexture(reflectionTex, 1);
		else
			pipeSetTexture(NULL, 1);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

		if(isLightMesh){
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
		}else{
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		}

		RwD3D9SetVertexShader(vehiclePBRVS);
		RwD3D9SetPixelShader(Glass_Vehicle);
		D3D9Render(resEntryHeader, instancedData);

		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
		continue;
	}

	// ================================================================
	// RUBBER PATH — tires
	// ================================================================
	if(isTireMesh && Rubber_Vehicle_Modern && vehiclePBRVS){
		float tireRough, tireRefl, tireTR, tireTG, tireTB;
		VehShaders_GetTireProps(modelIndex, &tireRough, &tireRefl, &tireTR, &tireTG, &tireTB);

		pipeUploadMatCol(flags, material, REG_matCol);
		// sRGB→linear for rubber (same as opaque paint path)
		RwRGBAReal rubberMatColRGBA;
		if(flags & rpGEOMETRYMODULATEMATERIALCOLOR){
			RwRGBARealFromRwRGBA(&rubberMatColRGBA, &material->color);
			rubberMatColRGBA.red   = powf(rubberMatColRGBA.red,   2.2f);
			rubberMatColRGBA.green = powf(rubberMatColRGBA.green, 2.2f);
			rubberMatColRGBA.blue  = powf(rubberMatColRGBA.blue,  2.2f);
			RwD3D9SetPixelShaderConstant(REG_matCol, &rubberMatColRGBA, 1);
		}
		surfProps.ambient = material->surfaceProps.ambient;
		surfProps.specular = 0.0f; // C1: rubber path has no spec term (stale-mesh guard)
		surfProps.diffuse = material->surfaceProps.diffuse;
		RwD3D9SetVertexShaderConstant(REG_surfProps, &surfProps, 1);
		RwD3D9SetPixelShaderConstant(0, &surfProps, 1);
		float zero[4] = {0,0,0,0};
		RwD3D9SetVertexShaderConstant(21, zero, 1);

		// Compute wear and dirt from vehicle era (pre-baked per-era defaults)
		float wearFactor = 0.0f;
		float dirtLevel = 0.0f;
		int era = GetVehicleEraByID(modelIndex);
		if(era < 0 || era > 3) era = 1;
		// Pre-80 and utility vehicles get base wear and dirt
		wearFactor = (era == 0) ? 0.5f : (era == 3 ? 0.4f : (era == 1 ? 0.2f : 0.05f));
		dirtLevel = (era == 3) ? 0.5f : (era == 0 ? 0.3f : 0.1f);

		// Rubber c22/c23 via pipeUploadPBR (single source of truth for register layout).
		// main_rubber reads the same registers as pbrParams/paintNoise but with
		// tire semantics: c22 = {roughness, F0, tintR, tintG},
		// c23 = {tintB, dirtLevel, wearFactor, 0} (VehiclePBR_Modern.hlsl:656+).
		pipeUploadPBR(tireRough, tireRefl, tireTR, tireTG,
		              tireTB, dirtLevel, wearFactor);

		// main_rubber consumes the same shared per-vehicle state as the opaque
		// path: c46 (layer bitmask via LF(4)), s3 (iblTex), c24 (ambientColor)
		// and the Forward+ cluster block (c45/c48-c111 + s5 tile texture).
		// Mirror the opaque branch uploads so tires don't read stale/undefined
		// D3D9 state. Order follows the opaque path: c46 -> s3 -> Forward+ -> c24.
		extern IDirect3DTexture9 *g_iblTex;
		IDirect3DDevice9 *dev = d3d9device;

		// c46.x = PBR layer bitmask — modular layer toggles (config->vehPBRLayers)
		{
			float layerCfg[4] = { (float)config->vehPBRLayers, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(46, layerCfg, 1);
		}

		// IBL on stage 3 (raw D3D9 — no RW wrapper) + cloud shadow (c4)
		if(dev && g_iblTex){
			dev->SetTexture(3, g_iblTex);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			extern float cloudAnimTimer;
			float cloudShadow[4] = { 0.0f, cloudAnimTimer * 0.01f, 1.0f, 0.0f };
			extern RpLight *&pDirect;
			if(pDirect){
				RwFrame *sunFrame = RpLightGetFrame(pDirect);
				if(sunFrame){
					RwMatrix *sunLTM = RwFrameGetLTM(sunFrame);
					if(sunLTM){
						cloudShadow[0] = sunLTM->at.x;
						cloudShadow[1] = sunLTM->at.y;
						cloudShadow[2] = sunLTM->at.z;
					}
				}
			}
			RwD3D9SetPixelShaderConstant(4, cloudShadow, 1);
		}else if(dev){
			dev->SetTexture(3, NULL);
		}

		// Forward+ clustered point lights: bind the tile index texture (s5) and
		// upload c45/c48-c111 — main_rubber reads all of these for street lights.
		ForwardPlus_SetConstants();

		// Object ambient from timecycle (PS c24) — same source as the opaque path.
		{
			RwRGBAReal tcAmbient = GetTimecycleAmbientPBR();
			float ambientPS[4] = { tcAmbient.red, tcAmbient.green, tcAmbient.blue, 0.0f };
			RwD3D9SetPixelShaderConstant(24, ambientPS, 1);
		}

		RwD3D9SetVertexShader(vehiclePBRVS);
		RwD3D9SetPixelShader(Rubber_Vehicle_Modern);
		D3D9Render(resEntryHeader, instancedData);
		continue;
	}

		// ================================================================
		// OPAQUE PBR PATH — paint type system via bridge
		// ================================================================

		if(!vehiclePBRVS || !VehiclePBR_Modern){
			// Silent-failure fix: this used to fall through to a BLIND
			// D3D9Render() with whatever VS/PS happened to be bound last —
			// a stale shader from an earlier mesh/pipeline producing
			// garbage, and its warning was gated behind vehLogThisFrame
			// (once per 3600 frames), i.e. effectively silent. A missing
			// shader handle means CreateShaders failed; there is no valid
			// draw to fall back to inside this cb (the legacy PS2/PC/leeds
			// cbs are separate entry points, not reachable per-mesh), so
			// SKIP the mesh and say so on a throttled tag.
			if(dbglog_throttle("VehPBRShaderMissing"))
				dbglog("[VehiclePBR] SKIP mesh: VS=%p PS=%p (shader creation failed)",
				       vehiclePBRVS, VehiclePBR_Modern);
			continue;
		}

		fxParams.shininess = 0.0f;
		surfProps.specular = 0.0f;
		// Part A2/B: RAW env shininess captured BEFORE the ×8×mult shaping and
		// before the glossiness fallback — the chrome threshold and (default 1)
		// shaped-math identity both need the un-mutated carcols value.
		float rawShininess = 0.0f;
		if(!noFx){
			materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
			hasEnv1 = !!(materialFlags & 1);
			hasEnv2 = !!(materialFlags & 2);
			hasSpec = !!(materialFlags & 4) && !renderingWheel;
			if(RpMatFXMaterialGetEffects(material) != rpMATFXEFFECTENVMAP){
				hasEnv1 = false;
				hasEnv2 = false;
				hasSpec = false;
			}

			if(hasEnv1 || hasEnv2){
				envData = *GETENVMAP(material);
				rawShininess = envData->GetShininess();
				fxParams.shininess = rawShininess * 8.0f;
				// Part A2: envShininessMult now drives ALL paths (raw + fallback
				// below), not just this branch — that's why the slider "didn't
				// seem to work". Shaped response: identity at m<=1 (exact old
				// parity for defaults/attenuation), log-compressed above 1 so
				// m=10 → ~2.15× instead of a 10× blowout. Threshold/chrome logic
				// uses rawShininess, never this shaped value.
				float m = config->envShininessMult;
				float shaped = (m <= 1.0f) ? m : (1.0f + logf(m) * 0.5f);
				fxParams.shininess *= shaped;
			}

			if(hasSpec){
				specData = *GETSPECMAP(material);
				surfProps.specular = specData->specularity;
				surfProps.specular *= 3.0f;
				// Part A3 (semantics note): PS main() never reads surfProps.y
				// (c0 = {ambient, spec-unread, diffuse, prelit} after the C1
				// struct reorder), so this mult was DEAD in the PS. Real spec
				// scaling now rides PS c44.y at the specTotal combine
				// (VehiclePBR_Modern :383). Keep the surfProps scale for the
				// (currently unused) VS path + VS uploads.
				surfProps.specular *= config->envSpecularityMult;
			}
		}

		pipeUploadMatCol(flags, material, REG_matCol);
		// PS c19 = matCol — vehicle PBR main() uses matCol.rgb for baseColor/paintTint
		// pipeUploadMatCol only sets VS constant; PS needs it too
		RwRGBAReal matColRGBA;
		if(flags & rpGEOMETRYMODULATEMATERIALCOLOR){
			RwRGBARealFromRwRGBA(&matColRGBA, &material->color);
			// sRGB→linear: carcols colors are gamma-encoded, PBR needs linear
			matColRGBA.red   = powf(matColRGBA.red,   2.2f);
			matColRGBA.green = powf(matColRGBA.green, 2.2f);
			matColRGBA.blue  = powf(matColRGBA.blue,  2.2f);
			RwD3D9SetPixelShaderConstant(REG_matCol, &matColRGBA, 1);
		}else{
			matColRGBA = { 1.0f, 1.0f, 1.0f, 1.0f };
			RwD3D9SetPixelShaderConstant(REG_matCol, &matColRGBA, 1);
		}
		surfProps.ambient = material->surfaceProps.ambient;
		surfProps.diffuse = material->surfaceProps.diffuse;
		RwD3D9SetVertexShaderConstant(REG_surfProps, &surfProps, 1);
		RwD3D9SetVertexShaderConstant(21, &fxParams, 1);
		RwD3D9SetPixelShaderConstant(0, &surfProps, 1);
		RwD3D9SetPixelShaderConstant(1, &fxParams, 1);

		// Paint type selection via bridge (deterministic per model)
		unsigned int paintHash = modelIndex * 2654435761u;
		int paintType = VehShaders_SelectPaintType(modelIndex, paintHash);

		// CryEngine-style: specular/glossiness (NOT metallic/roughness)
		float specular, glossiness, specularTintR, specularTintG, specularTintB;
		float noiseScale, edgeBlend;
		VehShaders_GetPaintPBR(paintType, &specular, &glossiness, &specularTintR, &specularTintG, &specularTintB, &noiseScale, &edgeBlend);

		// Derive carcols shininess from paint glossiness when the material has no
		// MatFX env-map flag — true for MOST GTA SA vehicle paint. Without this
		// the PS carcolsShine collapses to 0 and reflections go invisible.
		// (Plan: docs/superpowers/plans/2026-09-08-vehicle-reflection-fix.md —
		// this fallback was specified but never applied.)
		// Part A2: the mult now applies to the fallback path too (previously the
		// slider only bit the hasEnv branch — the common paint case).
		if(fxParams.shininess < 0.01f){
			fxParams.shininess = glossiness;
			float m = config->envShininessMult;
			float shaped = (m <= 1.0f) ? m : (1.0f + logf(m) * 0.5f);
			fxParams.shininess *= shaped;
		}
		// Part A2: clamp shininess ≤1 when shaped ≤1 for exact old parity
		// (PS :237 previously saturate'd everything anyway); allow >1 when
		// the user pushes envShininessMult above 1 (widened to min(w,4) in PS).
		if(fxParams.shininess > 1.0f && config->envShininessMult <= 1.0f)
			fxParams.shininess = 1.0f;
		if(fxParams.shininess > 4.0f)
			fxParams.shininess = 4.0f;

		// Per-mesh variation from material data
		if(fxParams.shininess > 0.2f){
			glossiness = min(glossiness + fxParams.shininess * 0.1f, 0.95f);
			specular = min(specular + fxParams.shininess * 0.1f, 1.0f);
		}

		// Re-upload fxParams now that shininess is corrected — the upload earlier
		// in this block carried shininess=0 for most paint materials.
		RwD3D9SetVertexShaderConstant(21, &fxParams, 1);
		RwD3D9SetPixelShaderConstant(1, &fxParams, 1);

		// ================================================================
		// Unified BRDF: blend paint with per-material surface type
		// Chrome, rubber, plastic, carbon, leather, dirt all get their
		// own BRDF from the unified library
		// ================================================================
		int surfType = VehShaders_GetSurfaceType(texName);
		// Interior materials: make matte by default so they don't clash with reflections
		if(surfType == SURFACE_CAR_PLASTIC || surfType == SURFACE_CAR_LEATHER || surfType == SURFACE_CAR_FABRIC)
			surfType = SURFACE_CAR_MATTE;
		// Part C: chrome by atomic frame-name (bumper/trim/exhaust/...) OR
		// material specular bit 3. AFTER the interior→MATTE conversion (so a
		// "trim_*" frame beat matte — user's exact ask) and BEFORE the non-BODY
		// BRDF override below (single promotion path). vehAutoChrome checked
		// inside VehShaders_FrameNameIsChrome.
		if(VehShaders_FrameNameIsChrome(atomic))
			surfType = SURFACE_CAR_CHROME;
		if(surfType != SURFACE_CAR_BODY){
			// Non-paint surface: use unified BRDF
			const BRDFMaterial *matBRDF = GetBRDF(surfType);
			specular = matBRDF->specular;
			glossiness = matBRDF->glossiness;
			specularTintR = matBRDF->specularTintR;
			specularTintG = matBRDF->specularTintG;
			specularTintB = matBRDF->specularTintB;
		}

		// Optional chrome promotion: stock SA assigns a MatFX env map to ALL car
		// paint, so name-only classification misses untextured chrome trim. When
		// vehChromeEnvThreshold > 0, body materials whose env-map shininess is at
		// least that value are treated as chrome (mirror Fresnel + white tint).
		// Part B: compare RAW shininess (captured pre-×8×mult/pre-fallback) —
		// the old compare ran against the SHAPED value, so envShininessMult
		// silently scaled past (or shorted) the threshold (the "broken" part).
		if(config->vehChromeEnvThreshold > 0.0f && surfType == SURFACE_CAR_BODY &&
		   !noFx && (hasEnv1 || hasEnv2) && rawShininess >= config->vehChromeEnvThreshold){
			surfType = SURFACE_CAR_CHROME;
			const BRDFMaterial *chromeBRDF = GetBRDF(surfType);
			specular = chromeBRDF->specular;
			glossiness = chromeBRDF->glossiness;
			specularTintR = chromeBRDF->specularTintR;
			specularTintG = chromeBRDF->specularTintG;
			specularTintB = chromeBRDF->specularTintB;
		}

		// Metalness for the PS env-reflection path: chrome/wheel get a mirror
		// Fresnel F0 and neutral (white) reflection tint; paint stays dielectric.
		float metallicness = 0.0f;
		if(surfType == SURFACE_CAR_CHROME)     metallicness = config->chromeMetallic; // Part B: config-driven, default 1.0
		else if(surfType == SURFACE_CAR_WHEEL) metallicness = 0.6f;

		// Part B: chrome parameter breakdown — F0/gloss ride the SAME pipeUploadPBR
		// site as the BRDF row (single fix-10 upload), overriding the row defaults
		// (brdfLibrary "Car Chrome" 0.56/0.90) with the config values.
		if(surfType == SURFACE_CAR_CHROME){
			specular  = config->chromeF0;
			glossiness = config->chromeGloss;
		}

		// Unified PBR upload (c22/c23 layout defined in pipeUploadPBR)
		// c22 = {glossiness, specular, specTint, metallicness}
		// Metals use a neutral specular tint so F0 isn't paint-coloured.
		pipeUploadPBR(glossiness, specular, specularTintR * (1.0f - metallicness), metallicness,
		              (float)renderingWheel, noiseScale, edgeBlend);

		// PBR textures via RW (not raw D3D9)
		// s0 = diffuse (already set above)
		// s1 = env map / reflection (RwTexture)
		// s2 = mask / reflection mask
		// s3 = IBL (raw D3D9)
		// s4 = normal buffer (raw D3D9 — no RW wrapper for IDirect3DTexture9*)
		extern IDirect3DTexture9 *g_iblTex;
		IDirect3DDevice9 *dev = d3d9device;

		// Env map on stage 1 (s1 = envMapTex in shader)
		// Guard: skip during sphere map render to avoid D3D9 feedback loop
		if(!gRenderingSpheremap)
			pipeSetTexture(reflectionTex, 1);
		else
			pipeSetTexture(NULL, 1);

		// Reflection mask on stage 2 (RwTexture)
		pipeSetTexture(CarPipe::reflectionMask, 2);

		// IBL on stage 3 (raw D3D9 — no RW wrapper)
		// c3 = {envIntensity, glossiness, tanHalfFovX, tanHalfFovY} — .zw let the
		// PS project reflection vectors with the main camera's view window to
		// sample the perspective-rendered env map. .x is the env-reflection
		// strength knob (was the dead specular slot).
		{
			float tanX = 0.65f, tanY = 0.45f;
			if(Scene.camera){
				tanX = Scene.camera->viewWindow.x;
				tanY = Scene.camera->viewWindow.y;
			}
			// The load-time clamp wave in readIni (uncommitted main.cpp) hard-ranges
			// vehEnvIntensity to [0..0.4]; the INI default 1.0 therefore arrives as
			// 0.4, while the shader treats 1.0 = default env-reflection strength —
			// uploaded raw, the env term ran at 40% and reflections read as "lost".
			// Normalize internally: map slider [0..0.4] → intensity [0..1] so slider
			// max ≈ the old default. Slider ranges themselves stay untouched
			// (owned by the clamp/debugmenu lane).
			float envEff = config->vehEnvIntensity * 2.5f;
			// Part B: chromeEnvBoost folds into the SAME c3.x upload (per-mesh,
			// inside this block) when the BRDF resolved to CHROME — no new
			// register, no second upload site.
			if(surfType == SURFACE_CAR_CHROME)
				envEff *= config->chromeEnvBoost;
			float iblParams[4] = { envEff, glossiness, tanX, tanY };
			RwD3D9SetPixelShaderConstant(3, iblParams, 1);
		}
		// c46.x = PBR layer bitmask — modular layer toggles (config->vehPBRLayers)
		{
			float layerCfg[4] = { (float)config->vehPBRLayers, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(46, layerCfg, 1);
		}
		if(dev && g_iblTex){
			dev->SetTexture(3, g_iblTex);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			extern float cloudAnimTimer;
			float cloudShadow[4] = { 0.0f, cloudAnimTimer * 0.01f, 1.0f, 0.0f };
			extern RpLight *&pDirect;
			if(pDirect){
				RwFrame *sunFrame = RpLightGetFrame(pDirect);
				if(sunFrame){
					RwMatrix *sunLTM = RwFrameGetLTM(sunFrame);
					if(sunLTM){
						cloudShadow[0] = sunLTM->at.x;
						cloudShadow[1] = sunLTM->at.y;
						cloudShadow[2] = sunLTM->at.z;
					}
				}
			}
			RwD3D9SetPixelShaderConstant(4, cloudShadow, 1);
		}else if(dev){
			dev->SetTexture(3, NULL);
		}

		// Normal buffer on stage 4 (s4 = normalBufTex in shader).
		// Gate mirrored from buildingPipe.cpp:1083-1089: a D3DPOOL_DEFAULT
		// RT holds driver garbage until DrawNormalBufferToTexture has
		// actually written it once (g_normalBufferHasContent), and a skipped
		// refresh (menu / shader missing / depth hook missing) leaves it
		// stale — binding it anyway makes the PS lerp its normals toward
		// random data (GGX blowout + glints). normalBufferEnable is the
		// user/menu switch that also gates DrawNormalBufferToTexture, so
		// without it the texture is guaranteed dead content. With the gate
		// off, ambientPS[3] stays 0 and the shader ignores s4 entirely.
		float ambientPS[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		{
			extern bool g_normalBufferHasContent;
			if(g_normalBufferTex && dev
			   && g_normalBufferHasContent
			   && config->normalBufferEnable){
				dev->SetTexture(4, g_normalBufferTex);
				ambientPS[3] = 1.0f;  // flag: normal buffer available
			}
		}

		// c29 = (screenW, screenH, 1/screenW, 1/screenH) — lets the PS convert
		// VPOS pixels to screen UV for sampling the half-res normal buffer.
		{
			RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL; // NULL camera → 0x7FAD4D-class fault
			if(camRas){
				float sw = (float)camRas->width, sh = (float)camRas->height;
				float screenP[4] = { sw, sh, 1.0f/max(sw, 1e-7f), 1.0f/max(sh, 1e-7f) };
				RwD3D9SetPixelShaderConstant(29, screenP, 1);
			}
		}

		// Forward+ clustered point lights: bind the tile index texture (s5) and
		// its POINT/CLAMP sampler. Lights arrive as PS constants c48-c111.
		ForwardPlus_SetConstants();

		// Object ambient from timecycle (PS c24) — use the same ambient as the
		// building PBR pipe (GetTimecycleAmbient), which is the proven working
		// path. Previously used ambientObjR/G/B which are the game's separate
		// object ambient values — typically much lower than the world ambient,
		// causing vehicles to render as near-black silhouettes in PBR mode.
		{
			RwRGBAReal tcAmbient = GetTimecycleAmbientPBR();
			ambientPS[0] = tcAmbient.red;
			ambientPS[1] = tcAmbient.green;
			ambientPS[2] = tcAmbient.blue;
		}
		RwD3D9SetPixelShaderConstant(24, ambientPS, 1);

		// Universal dynamic-sky ambient weight (PS c20) — shared with the building path.
		// .y/.z/.w carry the reflection-model toggles this cb is the only writer
		// of (see the c20 note in VehiclePBR_Modern.hlsl):
		//   y = vehEnvFallback   z = WetRoads (0 when vehWetEnv is off)
		//   w = vehEnvMask, and only when the TXD actually loaded — the PS
		//       samples maskTex (s2) behind this flag, so a missing mask must
		//       read as "off" rather than multiply by an unbound sampler.
		float iblAmbient[4] = {
			config->pbrIblAmbientWeight,
			config->vehEnvFallback ? 1.0f : 0.0f,
			config->vehWetEnv ? CWeather__WetRoads : 0.0f,
			(config->vehEnvMask && CarPipe::reflectionMask) ? 1.0f : 0.0f
		};
		RwD3D9SetPixelShaderConstant(20, iblAmbient, 1);

		// PS light constants (c5-c18): sun color/direction + 6 extra directional lights
		// The VS uploads these to VS registers via uploadLights(), but the PS has its
		// own independent constant registers. Without these, directCol (c5) and
		// directDir (c12) are stale/zero → no specular highlights and wrong L vector.
		// Mirror what the glass path does at lines 1552-1560.
		{
		extern RpLight *&pDirect;
		pipeUploadLightColorForcePS(pDirect, REG_directCol);
		pipeUploadLightDirectionForcePS(pDirect, REG_directDir);
		}
		for(int i = 0; i < 6; i++)
			if(i < NumExtraDirLightsInWorld && RpLightGetType(pExtraDirectionals[i]) == rpLIGHTDIRECTIONAL){
				pipeUploadLightColorPS(pExtraDirectionals[i], REG_directCol+i+1);
				pipeUploadLightDirectionPS(pExtraDirectionals[i], REG_directDir+i+1);
			}else{
				pipeUploadZeroPS(REG_directCol+i+1);
				pipeUploadZeroPS(REG_directDir+i+1);
			}

		// View matrix rotation for sphere map UV computation (PS c25-c27)
		// The sphere map is rendered from the camera's viewpoint, so reflection
		// vectors must be in view space for correct UV mapping.
		D3DMATRIX viewMat;
		RwD3D9GetTransform(D3DTS_VIEW, &viewMat);
		float viewRot[12] = {
			viewMat._11, viewMat._12, viewMat._13, 0.0f,
			viewMat._21, viewMat._22, viewMat._23, 0.0f,
			viewMat._31, viewMat._32, viewMat._33, 0.0f
		};
		RwD3D9SetPixelShaderConstant(25, viewRot, 3);

		// Sky color for env reflection (PS c28)
		// Upward-facing surfaces on the car reflect the sky from the sphere map.
		float skyP[4] = {
			CTimeCycle__m_CurrentColours.skyTopR / 255.0f,
			CTimeCycle__m_CurrentColours.skyTopG / 255.0f,
			CTimeCycle__m_CurrentColours.skyTopB / 255.0f,
			0.6f  // skyReflectStrength — how much sky tints upward reflections
		};
		RwD3D9SetPixelShaderConstant(28, skyP, 1);

		RwD3D9SetVertexShader(vehiclePBRVS);
		RwD3D9SetPixelShader(VehiclePBR_Modern);

		if(vehLogThisFrame){
			RwUInt32 frame = RWSRCGLOBAL(renderFrame);
			dbglog("[VehiclePBR] MESH@frame=%u: glossiness=%.2f specular=%.2f specTintR=%.2f carcolsShine=%.2f", frame, glossiness, specular, specularTintR, fxParams.shininess);
			dbglog("[VehiclePBR] MESH@frame=%u: envIntensity slider=%.3f eff=%.3f (slider*2.5 after [0..0.4] load clamp)", frame, config->vehEnvIntensity, config->vehEnvIntensity * 2.5f);
			dbglog("[VehiclePBR] MESH@frame=%u: ambientPS=(%.2f,%.2f,%.2f,%.2f) iblTex=%p", frame, ambientPS[0], ambientPS[1], ambientPS[2], ambientPS[3], g_iblTex);
			dbglog("[VehiclePBR] MESH@frame=%u: tex=%p matCol=(%.2f,%.2f,%.2f,%.2f) flags=0x%X", frame, material->texture, matColRGBA.red, matColRGBA.green, matColRGBA.blue, matColRGBA.alpha, flags);
			dbglog("[VehiclePBR] MESH@frame=%u: surfProps amb=%.2f diff=%.2f spec=%.2f", frame, surfProps.ambient, surfProps.diffuse, surfProps.specular);
			extern IDirect3DTexture9 *g_normalBufferTex;
			dbglog("[VehiclePBR] MESH@frame=%u: normalBuf=%p dualPass=%d", frame, g_normalBufferTex, config->dualPassVehicle);
			// Paint bisect diagnostics: which VS color terms are active
			dbglog("[VehiclePBR] MESH@frame=%u: bits PRELIT=%d LIGHT=%d MODULATE=%d", frame,
				!!(flags & rpGEOMETRYPRELIT), !!(flags & rpGEOMETRYLIGHT), !!(flags & rpGEOMETRYMODULATEMATERIALCOLOR));
			{
				RwRGBAReal tcDbg = GetTimecycleAmbientPBR();
				extern RpLight *&pDirect;
				RwRGBAReal sunDbg; sunDbg.red = 0; sunDbg.green = 0; sunDbg.blue = 0; sunDbg.alpha = 0;
				if(pDirect) sunDbg = pDirect->color;
				dbglog("[VehiclePBR] MESH@frame=%u: tcAmb=(%.2f,%.2f,%.2f) sunCol=(%.2f,%.2f,%.2f) nExtra=%d",
					frame, tcDbg.red, tcDbg.green, tcDbg.blue, sunDbg.red, sunDbg.green, sunDbg.blue, NumExtraDirLightsInWorld);
			}
		}

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);

		// PipeChain classify pack (see buildingPipe): R=class, G=gloss,
		// B=spec(F0), A=metallicness — the same final values the pipe just
		// fed into pipeUploadPBR (post surface-type/chrome promotion).
		{
			extern bool PipeChain_ClassifyBegin(float, float, float, float);
			extern void PipeChain_ClassifyEnd(void);
			if(PipeChain_ClassifyBegin((float)surfType, glossiness, specular, metallicness)){
				D3D9Render(resEntryHeader, instancedData);
				PipeChain_ClassifyEnd();
				RwD3D9SetPixelShader(VehiclePBR_Modern); // Begin forced PipeChainShader
			}
		}
		continue;
	}
	// Exit hygiene — route through the SHARED vehiclePipe_cleanup() instead of
	// the hand-rolled copy this cb used to run. The Env cb was the only vehicle
	// callback that never called it, so its exit unbound the textures but left
	// the s3/s4/s5 SAMPLER states behind (s3 = g_iblTex CLAMP/LINEAR, s4 =
	// normal buffer CLAMP/LINEAR, s5 = Forward+ tile POINT/CLAMP) — those stick
	// on every later FFP/HUD/particle draw that reuses those stages, while the
	// building side resets them at buildingPipe.cpp:1222. cleanup() covers
	// VS/PS NULL + s1/s2/s3/s4/s5 unbind + stage1 ops + the sampler resets.
	vehiclePipe_cleanup();
	// Parts cleanup() does not cover: this cb is the only vehicle path that
	// can leave stage2/stage3 colour ops enabled.
	RwD3D9SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	// s3/s4/s5 are bound RAW in this cb (dev->SetTexture — g_iblTex, the normal
	// buffer, the Forward+ tile), so RW's per-stage texture cache for them can
	// read NULL while the DEVICE still holds the texture: the
	// RwD3D9SetTexture(NULL, n) calls inside vehiclePipe_cleanup() then no-op
	// (cache already NULL) and the handoff leaves those textures bound on
	// stages the next pass may rebind as render targets (feedback lock / black
	// read). cleanup() did the rw-set; this raw pass guarantees layer (3) —
	// same two-step as buildingPipe's PBR exit.
	if(d3d9device){
		d3d9device->SetTexture(3, NULL);
		d3d9device->SetTexture(4, NULL);
		d3d9device->SetTexture(5, NULL);
	}
	// Restore render state saved at entry
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)fog);
}

void
CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	if(!repEntry || !object || !config){
		dbglog("SWITCH_CB: bail repEntry=%p object=%p config=%p", repEntry, object, config);
		return;
	}
	RwUInt32 savedCull = pipeEnterCullMode();
	// Same alpha/blend guard the building Switch uses (buildingPipe.cpp:868):
	// resyncs device<->rw blend/alpha-test state on entry (raw postfx/chars
	// D3DRS_* writes bypass the rw cache and leave the device blending or
	// with alpha-test off/ref=0 -> translucent "ghost" shells and interior
	// cutout faces visible). Every vehicle callback re-enables
	// rwRENDERSTATEVERTEXALPHAENABLE per mesh after this (vehiclePipe_
	// setupMaterial / Env 1571 / Xbox 1041), so the forced OFF baseline is
	// safe; exit restores the pre-entry states through both layers (also
	// closes CarPipe::SpecularPass's VERTEXALPHAENABLE=1 leak).
	PipeAlphaState savedAlpha = pipeEnterAlphaMode();
	// ZWRITE three-layer guard (hardened from two): the alpha guard above
	// covers blend/alpha-test but NOT zwrite. postfx raw D3DRS_ZWRITEENABLE
	// writes (postfx.cpp:3451, 3630, 3703, 3741, 3791, 5140) can leave the
	// DEVICE with zwrite off while the rw cache still says TRUE (its rw-side
	// restore no-ops) — pipes that trust rwRenderStateGet (D3D9RenderDual,
	// main.cpp:158) then draw without depth writes -> interior faces through
	// shells / translucent look. Vehicles force TRUE through all three layers
	// for the duration of the cb; exit restores each layer to its own
	// pre-entry value (postfx manages the raw device side itself with its own
	// save/restore — syncing the sides here would fight that contract).
	RwBool savedZWriteRw = TRUE;
	RwUInt32 savedZWriteCache = TRUE;
	DWORD savedZWriteDev = TRUE;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWriteRw);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteCache);
	d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDev);
	if(!savedZWriteRw || !savedZWriteCache || !savedZWriteDev){
		if(dbglog_throttle("VehicleZWrite"))
			dbglog("[VEHICLE] ZWRITE off/desynced on entry (rw=%d cache=%u dev=%d) — forcing TRUE for cb",
				(int)savedZWriteRw, savedZWriteCache, (int)savedZWriteDev);
	}
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	// Layer-3 alpha/blend repair — the vehicle Switch was the ONLY one of the
	// three pipe-group switches (building :1197, ped main.cpp:747) that had
	// just pipeEnterAlphaMode, whose RwD3D9SetRenderState writes are
	// applied[]-gated and so cannot reach a DEVICE that drifted behind an
	// unchanged cache (postfx Save/RestoreRawGeomStates raw-restores
	// ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP from a snapshot that can
	// already disagree, DepthHook raw ZENABLE). Every vehicle cb then draws
	// from that device state for the WHOLE atomic: alpha-test off -> cutout
	// shells/glass paint their transparent texels, ref/domain wrong -> grey
	// posterized blobs. Same call, same position as buildingPipe.cpp:1204.
	pipeForceAlphaBlock();
	switch(config->vehiclePipe){
	case CAR_PS2:
		CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		break;
	case CAR_PC:
		CCustomCarEnvMapPipeline__CustomPipeRenderCB_exe(repEntry, object, type, flags);
		break;
	case CAR_XBOX:
		CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox(repEntry, object, type, flags);
		break;
	case CAR_SPEC:
		CCustomCarEnvMapPipeline__CustomPipeRenderCB_Specular(repEntry, object, type, flags);
		break;
	case CAR_NEO:
		// CarPipe (neoCarpipe.cpp) lost its shaders — CreateShaders sets
		// vertexShaderPass1/2 = NULL ("unified PBR pipeline") — so the old
		// direct CarPipe::RenderCallback dispatch rendered fixed-function
		// garbage (translucent/white). iv_mode preset (iv_mode.cpp:59,
		// ivMode=1) wants the IV vehicle shaders -> PS2 cb + ivMode swap;
		// otherwise render through the PBR Env cb (same as CAR_MODERN).
		// main.cpp:1017 still runs CarPipe::RenderEnvTex for vehiclePipe
		// == CAR_NEO, so the Env cb gets a fresh reflectionTex either way.
		if(!UG_mod && config->ivMode)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		else if(!UG_mod && iCanHasNeoCar)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env(repEntry, object, type, flags);
		else if(dbglog_throttle("VehicleSkipped:NEO"))
			dbglog("[VEHICLE] CAR_NEO skipped: ivMode=%d neoCar=%d UG_mod=%p",
				config->ivMode, iCanHasNeoCar, (void*)UG_mod);
		break;
	case CAR_LCS:
	case CAR_VCS:
		CCustomCarEnvMapPipeline__CustomPipeRenderCB_leeds(repEntry, object, type, flags);
		break;
	case CAR_MOBILE:
		/* Need hooked building pipe for sphere maps */
		if(iCanHasbuildingPipe)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_mobile(repEntry, object, type, flags);
		break;
	case CAR_ENV:
		if(iCanHasbuildingPipe && iCanHasNeoCar)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env(repEntry, object, type, flags);
		else if(dbglog_throttle("VehicleSkipped:ENV"))
			dbglog("[VEHICLE] CAR_ENV skipped: buildingPipe=%d neoCar=%d", iCanHasbuildingPipe, iCanHasNeoCar);
		break;
	case CAR_MODERN:
		// PBR modern pipeline with glass shader, 4 color channels, GGX specular
		if(iCanHasNeoCar)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env(repEntry, object, type, flags);
		else if(!UG_mod){
			// Silent-failure fix: iCanHasNeoCar == 0 used to fall into a
			// throttled log and render NOTHING — every car disappeared while
			// the log looked clean. Take the same graceful path CAR_NEO and
			// CAR_GTAIV already take when their gate fails: the PS2 cb needs
			// no neo-car assets, so it still produces a lit vehicle instead
			// of a hole in the world.
			if(dbglog_throttle("VehicleSkipped:MODERN"))
				dbglog("[VEHICLE] CAR_MODERN: neoCar=%d unavailable -> PS2 fallback",
				       iCanHasNeoCar);
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		}else if(dbglog_throttle("VehicleSkipped:MODERN"))
			dbglog("[VEHICLE] CAR_MODERN skipped: neoCar=%d UG_mod=%p (PS2 fallback disabled under UG)",
			       iCanHasNeoCar, (void*)UG_mod);
		break;
	case CAR_GTAIV:
		// PIPELINE_GTAIV -> CAR_GTAIV (main.cpp:1417). CarPipe has no
		// shaders (removed with the neo pass1/pass2 shaders), so the old
		// direct dispatch was fixed-function garbage ("cooked"). Dispatch
		// the PS2 callback; its iv-mode shader swap below also covers
		// vehiclePipe == CAR_GTAIV -> gtaivVehicleVS/PS — same pattern
		// BUILDING_GTAIV uses (buildingPipe.cpp:395).
		if(!UG_mod)
			CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		break;
	}
	pipeExitAlphaMode(savedAlpha);
	// Restore each zwrite layer to its own pre-entry value (see entry comment).
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteCache);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDev);
	pipeExitCullMode(savedCull);
	fixSAMP();
}

void
setVehiclePipeCB(RxPipelineNode *node, RxD3D9AllInOneRenderCallBack callback)
{
	CCustomCarEnvMapPipeline__Init();
	RxD3D9AllInOneSetRenderCallBack(node, CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch);
}

RpAtomic*
CVisibilityPlugins__RenderWheelAtomicCB(RpAtomic *atomic)
{
	if(!atomic) return atomic;
	renderingWheel = 1;
	AtomicDefaultRenderCallBack(atomic);
	renderingWheel = 0;
	return atomic;
}

int CCarFXRenderer__IsCCPCPipelineAttached(RpAtomic *atomic)
{
	// CLASS 6: Simplified — always returns false (car pipe disabled via hook at 0x5D5B80)
	(void)atomic;
	return false;
}

#include "debugmenu_public.h"
RwTexture *RwTextureRead_HACK(const RwChar * name, const RwChar * maskName)
{
	if(strcmp(name, "xvehicleenv128") == 0)
		return RwTextureRead("vehicleenvmap128", maskName);
	return RwTextureRead(name, maskName);
}

void
hookVehiclePipe(void)
{
	InjectHook(0x5D9FE9, setVehiclePipeCB);
	InterceptCall(&CCustomCarEnvMapPipeline__PreRenderUpdate_orig, CCustomCarEnvMapPipeline__PreRenderUpdate, 0x5D5B10);
	InjectHook(0x7323C0, CVisibilityPlugins__RenderWheelAtomicCB, PATCH_JUMP);

	// TEMP - disable car pipe
#if 0
	InjectHook(0x5D5B80, CCarFXRenderer__IsCCPCPipelineAttached, PATCH_JUMP);
	Nop(0x4C8907, 5);	// don't auto-assign on vehicles
	*(float*)0x8A7780 = 1.0f;	// hardcoded coefficient
//	Nop(0x4C8899, 5);	// don't set coefficient
	Nop(0x4C982F, 5);	// don't update coefficient for lighting
#endif
	// temp hack RwTextureRead to read other env map
	if(betaEnvmaptest){
		InjectHook(0x80483C, RwTextureRead_HACK);
		if(DebugMenuLoad()){
			DebugMenuAddVarBool32("SkyGFX", "Beta Env map test", &betaEnvmaptest, nil);
			DebugMenuAddVar("SkyGFX", "envmap tweak 1", &envmap1tweak, nil, 0.1f, 0.0f, 10.0f);
			DebugMenuAddVar("SkyGFX", "envmap tweak 2", &envmap2tweak, nil, 0.1f, 0.0f, 10.0f);
		}
	}

}

// Release vehicle pipe caches on device reset
void ReleaseVehiclePipeCaches(void)
{
	if(carfx_env1Frame){ RwFrameDestroy(carfx_env1Frame); carfx_env1Frame = NULL; }
	if(carfx_env2Frame){ RwFrameDestroy(carfx_env2Frame); carfx_env2Frame = NULL; }
	carfx_view = RwMatrix();
	carfx_env1Inv = RwMatrix();
	carfx_env2Inv = RwMatrix();
	carfx_lightdir = RwV3d();
	s_vehiclePipeInitialized = false;

	// Reset Env2Xform caches
	s_env2xform_lastobject = NULL;
	s_env2xform_lastenvdata = NULL;
	s_env2xform_lastrenderframe = 0;
	s_env2xform_lastTransX = 0.0f;
	s_env2xform_lastTransY = 0.0f;
	s_env2xform_lastx = 0.0f;
	s_env2xform_lasty = 0.0f;

	// Reset Env2Xform_PC caches
	s_env2xformpc_lastobject = NULL;
	s_env2xformpc_lastenvdata = NULL;
	s_env2xformpc_lastrenderframe = 0;
	s_env2xformpc_lastTransX = 0.0f;
	s_env2xformpc_lastTransY = 0.0f;
	s_env2xformpc_lastx = 0.0f;
	s_env2xformpc_lasty = 0.0f;

	// Reset SetupEnv caches
	s_setupEnv_lastobject = NULL;
	s_setupEnv_lastfrm = NULL;
	s_setupEnv_lastrenderframe = 0;
	memset(&s_setupEnv_lastmat, 0, sizeof(RwMatrix));
}
