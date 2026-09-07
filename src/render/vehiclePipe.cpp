#include "skygfx.h"
#include "neo.h"
#include "brdfLibrary.h"

extern void *Glass_Vehicle;
extern void *Rubber_Vehicle;
extern void *Rubber_Vehicle_Modern;
extern int GetVehicleEraByID(int modelID);

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
int renderingWheel;

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

	normalTex = RwTextureCreate(nil);
	RwTextureSetFilterMode(normalTex, rwFILTERLINEAR);

	MakeEnvmapCam();
	MakeEnvmapRasters();
	MakeNormalCam();

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
	static void *lastobject;
	static CustomEnvMapPipeMaterialData *lastenvdata;
	static RwUInt16 lastrenderframe;
	static float lastTransX, lastTransY;
	static float lastx, lasty;

	RwV3d diff, upnorm;
	float trans;
	float sclx, scly;
	float val1, val2;

	sclx = envData->GetTransScaleX()*50.0f;
	scly = envData->GetTransScaleY()*50.0f;

	if(lastrenderframe != RWSRCGLOBAL(renderFrame) ||
	   lastobject != atomic ||
	   lastenvdata != envData){
		envData->renderFrameCounter = RWSRCGLOBAL(renderFrame);
		lastrenderframe = RWSRCGLOBAL(renderFrame);
		lastobject = atomic;
		lastenvdata = envData;

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

		lastTransX = atmEnvData->trans = trans;
		lastTransY = envmat->at.x + envmat->at.y;
		if(lastTransY > 0.1) lastTransY = 0.1f;
		if(lastTransY < 0.0) lastTransY = 0.0f;
		if(envmat->at.z < 0.0f)
			lastTransY = 1.0 - lastTransY;
		lastx = atmEnvData->posx = envmat->pos.x;
		lasty = atmEnvData->posy = envmat->pos.y;
	}else{
		atmEnvData->trans = lastTransX;
		atmEnvData->posx = lastx;
		atmEnvData->posy = lasty;
	}
	envXform[0] = lastTransX;
	envXform[1] = lastTransY;
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
	static void *lastobject;
	static CustomEnvMapPipeMaterialData *lastenvdata;
	static RwUInt16 lastrenderframe;
	static float lastTransX, lastTransY;
	static float lastx, lasty;

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

	if(lastrenderframe != RWSRCGLOBAL(renderFrame) ||
	   lastobject != atomic ||
	   lastenvdata != envData){
		envData->renderFrameCounter = RWSRCGLOBAL(renderFrame);
		lastrenderframe = RWSRCGLOBAL(renderFrame);
		lastobject = atomic;
		lastenvdata = envData;

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

		lastTransX = atmEnvData->trans = trans;
		lastTransY = envmat->at.x + envmat->at.y;
		if(lastTransY > 0.1) lastTransY = 0.1f;
		if(lastTransY < 0.0) lastTransY = 0.0f;
		if(envmat->at.z < 0.0f)
			lastTransY = 1.0 - lastTransY;
		lastx = atmEnvData->posx = envmat->pos.x;
		lasty = atmEnvData->posy = envmat->pos.y;
	}else{
		atmEnvData->trans = lastTransX;
		atmEnvData->posx = lastx;
		atmEnvData->posy = lasty;
	}
	envXform[0] = -lastTransX;
	envXform[1] = lastTransY;
}

void
CCustomCarEnvMapPipeline__SetupEnv(RpAtomic *atomic, RwFrame *envframe, RwMatrix *frminv, RwMatrix *envmat)
{
	static RwMatrix lastmat;
	static void *lastobject;
	static RwFrame *lastfrm;
	static RwUInt16 lastrenderframe;
	RpClump *clump;
	RwFrame *frame;

	clump = RpAtomicGetClump(atomic);
	if(lastobject != (clump ? (void*)clump : (void*)atomic) ||
	   lastfrm != envframe ||
	   lastrenderframe != RWSRCGLOBAL(renderFrame)){
		frame = clump ? RpClumpGetFrame(clump) : RpAtomicGetFrame(atomic);
		if(frame)
			RwMatrixMultiply(&lastmat, RwFrameGetLTM(frame), frminv);

		lastobject = (clump ? (void*)clump : (void*)atomic);
		lastfrm = envframe;
		lastrenderframe = RWSRCGLOBAL(renderFrame);
	}
	*envmat = lastmat;
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
}

// Cleanup: unbind shaders, textures, disable TSS
static void vehiclePipe_cleanup()
{
	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTexture(NULL, 2);
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

// FX additive blend pass: sets states, renders, restores
static void vehiclePipe_fxAdditiveBlend(RxD3D9ResEntryHeader *header,
	RxD3D9InstanceData *data, const VehicleRenderState *state)
{
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)0);
	D3D9Render(header, data);
	RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)state->fog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)state->src);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)state->dst);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state->alphafunc);
	d3d9device->SetRenderState(D3DRS_TEXTUREFACTOR, state->texturefactor);
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
	pipeUploadMatCol(flags, *outMaterial, REG_matCol);
	float surfProps[4];
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

		if(config->ivMode){
			RwD3D9SetVertexShader(gtaivVehicleVS);
			RwD3D9SetPixelShader(gtaivVehiclePS);
			float psParams[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, &fxParams, 1);
			RwD3D9SetPixelShaderConstant(1, psParams, 1);
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
			RwD3D9SetPixelShader(simplePS);

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
	RwD3D9SetVertexShaderConstant(LOC_surfProps, (void*)&surf, 1);
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

		RwD3D9SetTexture(NULL, 1);

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
		// Leeds-specific: clamp surfProps[0]
		float surfProps[4];
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
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		if(config->vehiclePipe == 6)	// VCS
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		else
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

		D3D9Render(resEntryHeader, instancedData);

		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)state.dst);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)state.src);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)state.fog);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state.alphafunc);
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
			RwD3D9SetPixelShaderConstant(REG_fxParams, &fxParams, 1);
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
	struct {
		float ambient;
		float diffuse;
		float specular;
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

	// Per-frame debug logging (throttle to once per ~60 seconds)
	static unsigned int vehLogCounter = 0;
	bool vehLogThisFrame = (vehLogCounter++ % 3600 == 0);

	if(vehLogThisFrame){
		RwUInt32 frame = RWSRCGLOBAL(renderFrame);
		dbglog("[VehiclePBR] === FRAME %u ===", frame);
		dbglog("[VehiclePBR] atomic=%p flags=%X", atomic, flags);
		dbglog("[VehiclePBR] shaders: VS=%p PS=%p", vehiclePBRVS, VehiclePBR_Modern);
		dbglog("[VehiclePBR] iCanHasNeoCar=%d iCanHasbuildingPipe=%d", iCanHasNeoCar, iCanHasbuildingPipe);
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
	bool isHeadlight = !isGlassMesh && VehShaders_IsHeadlightTexture(texName);
	bool isTaillight = !isGlassMesh && !isHeadlight && VehShaders_IsTaillightTexture(texName);
	bool isLightMesh = isHeadlight || isTaillight;
	bool isTireMesh  = !isGlassMesh && !isLightMesh && Rubber_Vehicle && VehShaders_IsTireTexture(texName);

	// ================================================================
	// GLASS + LIGHT PATH — alpha-blended glass shader
	// ================================================================
	if((isGlassMesh || isLightMesh) && Glass_Vehicle){		float opacity = (float)material->color.alpha / 255.0f;

		if(isLightMesh){
			float ltR, ltG, ltB;
			if(isTaillight)
				VehShaders_GetTaillightTint(modelIndex, &ltR, &ltG, &ltB);
			else
				VehShaders_GetHeadlightTint(modelIndex, &ltR, &ltG, &ltB);

			float glassP[4] = { ltR, ltG, ltB, opacity };
			RwD3D9SetPixelShaderConstant(22, glassP, 1);
			float lightP[4] = { 1.0f, isTaillight ? 1.5f : 1.2f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(23, lightP, 1);
		}else{
			float gtR, gtG, gtB, gtStr;
			VehShaders_GetGlassTint(modelIndex, &gtR, &gtG, &gtB, &gtStr);

			float glassP[4] = { gtR, gtG, gtB, opacity };
			RwD3D9SetPixelShaderConstant(22, glassP, 1);
			float lightP[4] = { 0.0f, 0.0f, gtStr, 0.0f };
			RwD3D9SetPixelShaderConstant(23, lightP, 1);
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
		surfProps.diffuse = material->surfaceProps.diffuse;
		RwD3D9SetVertexShaderConstant(REG_surfProps, &surfProps, 1);
		RwD3D9SetPixelShaderConstant(0, &surfProps, 1);

		float glassFxVS[4] = { config->envFresnel, 0, 0, config->envPower };
		RwD3D9SetVertexShaderConstant(21, glassFxVS, 1);
		float glassFxPS[4] = { 0, 0, fxParams.lightmult, 0 };
		RwD3D9SetPixelShaderConstant(1, glassFxPS, 1);

		pipeSetTexture(reflectionTex, 1);
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

		// Upload tire params to c22/c23 (read by parametric Rubber_Vehicle shader)
		// c22 = {roughness, F0, tintR, tintG}
		// c23 = {tintB, dirtLevel, wearFactor, 0}
		float tireParams[4] = { tireRough, tireRefl, tireTR, tireTG };
		float tireParams2[4] = { tireTB, dirtLevel, wearFactor, 0.0f };
		RwD3D9SetPixelShaderConstant(22, tireParams, 1);
		RwD3D9SetPixelShaderConstant(23, tireParams2, 1);

		RwD3D9SetVertexShader(vehiclePBRVS);
		RwD3D9SetPixelShader(Rubber_Vehicle_Modern);
		D3D9Render(resEntryHeader, instancedData);
		continue;
	}

		// ================================================================
		// OPAQUE PBR PATH — paint type system via bridge
		// ================================================================

		if(!vehiclePBRVS || !VehiclePBR_Modern){
			// Fallback: use legacy render if PBR shaders missing
			if(vehLogThisFrame)
				dbglog("[VehiclePBR] WARNING: null VS=%p or PS=%p, falling back", vehiclePBRVS, VehiclePBR_Modern);
			D3D9Render(resEntryHeader, instancedData);
			continue;
		}

		fxParams.shininess = 0.0f;
		surfProps.specular = 0.0f;
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
				fxParams.shininess = envData->GetShininess();
				fxParams.shininess *= 8.0f * config->envShininessMult;
			}

			if(hasSpec){
				specData = *GETSPECMAP(material);
				surfProps.specular = specData->specularity;
				surfProps.specular *= 3.0f * config->envSpecularityMult;
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

		// Per-mesh variation from material data
		if(fxParams.shininess > 0.2f){
			glossiness = min(glossiness + fxParams.shininess * 0.1f, 0.95f);
			specular = min(specular + fxParams.shininess * 0.1f, 1.0f);
		}

		// ================================================================
		// Unified BRDF: blend paint with per-material surface type
		// Chrome, rubber, plastic, carbon, leather, dirt all get their
		// own BRDF from the unified library
		// ================================================================
		int surfType = VehShaders_GetSurfaceType(texName);
		// Interior materials: make matte by default so they don't clash with reflections
		if(surfType == SURFACE_CAR_PLASTIC || surfType == SURFACE_CAR_LEATHER || surfType == SURFACE_CAR_FABRIC)
			surfType = SURFACE_CAR_MATTE;
		if(surfType != SURFACE_CAR_BODY){
			// Non-paint surface: use unified BRDF
			const BRDFMaterial *matBRDF = GetBRDF(surfType);
			specular = matBRDF->specular;
			glossiness = matBRDF->glossiness;
			specularTintR = matBRDF->specularTintR;
			specularTintG = matBRDF->specularTintG;
			specularTintB = matBRDF->specularTintB;
		}

		// Unified PBR upload (c22/c23 layout defined in pipeUploadPBR)
		// c22 = {glossiness, specular, specTint, envFresnel}
		pipeUploadPBR(glossiness, specular, specularTintR, 1.0f,
		              (float)renderingWheel, noiseScale, edgeBlend);

		// PBR textures via RW (not raw D3D9)
		// s0 = diffuse (already set above)
		// s1 = normal buffer (raw D3D9 — no RW wrapper for IDirect3DTexture9*)
		// s2 = mask / reflection mask
		// s3 = IBL (raw D3D9)
		// s4 = env map / reflection
		extern IDirect3DTexture9 *g_iblTex;
		IDirect3DDevice9 *dev = d3d9device;

		// Env map on stage 1 (s1 = envMapTex in shader)
		pipeSetTexture(reflectionTex, 1);

		// Reflection mask on stage 2 (RwTexture)
		pipeSetTexture(CarPipe::reflectionMask, 2);

		// IBL on stage 3 (raw D3D9 — no RW wrapper)
		if(dev && g_iblTex){
			dev->SetTexture(3, g_iblTex);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			float iblParams[4] = { specular, glossiness, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(3, iblParams, 1);
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

		// Normal buffer on stage 4 (s4 = normalBufTex in shader)
		float ambientPS[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if(g_normalBufferTex && dev){
			dev->SetTexture(4, g_normalBufferTex);
			ambientPS[3] = 1.0f;  // flag: normal buffer available
		}

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
			dbglog("[VehiclePBR] MESH@frame=%u: glossiness=%.2f specular=%.2f specTintR=%.2f", frame, glossiness, specular, specularTintR);
			dbglog("[VehiclePBR] MESH@frame=%u: ambientPS=(%.2f,%.2f,%.2f,%.2f) iblTex=%p", frame, ambientPS[0], ambientPS[1], ambientPS[2], ambientPS[3], g_iblTex);
			dbglog("[VehiclePBR] MESH@frame=%u: tex=%p matCol=(%.2f,%.2f,%.2f,%.2f) flags=0x%X", frame, material->texture, matColRGBA.red, matColRGBA.green, matColRGBA.blue, matColRGBA.alpha, flags);
			dbglog("[VehiclePBR] MESH@frame=%u: surfProps amb=%.2f diff=%.2f spec=%.2f", frame, surfProps.ambient, surfProps.diffuse, surfProps.specular);
			extern IDirect3DTexture9 *g_normalBufferTex;
			dbglog("[VehiclePBR] MESH@frame=%u: normalBuf=%p dualPass=%d", frame, g_normalBufferTex, config->dualPassVehicle);
		}

		D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instancedData);
		continue;
	}
	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTexture(NULL, 2);
	RwD3D9SetTexture(NULL, 3);
	RwD3D9SetTexture(NULL, 4);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTexture(NULL, 4);
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
		if(iCanHasNeoCar && !UG_mod)
			CarPipe::RenderCallback(repEntry, object, type, flags);
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
		else if(dbglog_throttle("VehicleSkipped:MODERN"))
			dbglog("[VEHICLE] CAR_MODERN skipped: neoCar=%d", iCanHasNeoCar);
		break;
	case CAR_GTAIV:
		// GTA IV: uses CarPipe class (neoCarpipe.cpp) with gtaivVehicleVS/PS
		if(iCanHasNeoCar && !UG_mod)
			CarPipe::RenderCallback(repEntry, object, type, flags);
		break;
	}
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
