#include "skygfx.h"
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

float &CCoronas__LightsMult = *(float*)0x8D4B5C;
bool &CWeather__LightningFlash = *(bool*)0xC812CC;
WRAPPER bool CPostEffects__IsVisionFXActive(void) { EAXJMP(0x7034F0); }

RwRGBAReal buildingAmbient;

void (*CustomBuildingPipeline__Update_orig)(void);
void
CustomBuildingPipeline__Update(void)
{
	CustomBuildingPipeline__Update_orig();

	// buildingAmbient still uses the old pattern for now
	// UpdateTimecycleLighting() is called from RenderScene_hook instead
	buildingAmbient = GetTimecycleAmbient();
	// green already set by GetTimecycleAmbient()
	// blue already set by GetTimecycleAmbient()

	if(config->lightningIlluminatesWorld && CWeather__LightningFlash && !CPostEffects__IsVisionFXActive())
		buildingAmbient = { 1.0, 1.0, 1.0, 0.0 };
}

void
CustomBuildingEnvMapPipeline__SetupEnv(RpAtomic *atomic, RwFrame *envframe, RwMatrix *envmat)
{
	static RwMatrix lastmat;
	static void *lastobject;
	static RwFrame *lastfrm;
	static RwUInt16 lastrenderframe;
	RwMatrix inv;
	RpClump *clump;
	RwFrame *frame;

	if(envframe == NULL)
		envframe = RwCameraGetFrame(RWSRCGLOBAL(curCamera));

	clump = RpAtomicGetClump(atomic);

	if(lastobject != (clump ? (void*)clump : (void*)atomic) ||
	   lastfrm != envframe ||
	   lastrenderframe != RWSRCGLOBAL(renderFrame)){
		frame = clump ? RpClumpGetFrame(clump) : RpAtomicGetFrame(atomic);
		if(!frame){
			RwMatrixSetIdentity(envmat);
			return;
		}
		RwMatrixInvert(&inv, RwFrameGetLTM(envframe));
		RwMatrixMultiply(&lastmat, RwFrameGetLTM(frame), &inv);
		if((rwMatrixGetFlags(&lastmat) & rwMATRIXTYPEMASK) != rwMATRIXTYPEORTHONORMAL)
			RwMatrixOrthoNormalize(&lastmat, &lastmat);

		lastobject = (clump ? (void*)clump : (void*)atomic);
		lastfrm = envframe;
		lastrenderframe = RWSRCGLOBAL(renderFrame);
	}
	*envmat = lastmat;
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
}

void
buildingPipe_restoreRenderState(const BuildingRenderState *state)
{
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)state->alpharef);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)state->alphafunc);
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

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	static int ps2FrameCount = 0;
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

	_rwD3D9EnableClippingIfNeeded(object, type);

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
		RwD3D9SetVertexShaderConstant(REG_surfProps, &material->surfaceProps, 1);

		TexInfo *texinfo = RwTextureGetTexDBInfo(material->texture);

		if(config->ivMode){
			RwD3D9SetVertexShader(gtaivBuildingVS);
			RwD3D9SetPixelShader(gtaivBuildingPS);
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
			RwD3D9SetPixelShader(simplePS);

			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
			RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)0);
			D3D9Render(resEntryHeader, instancedData);
			RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)fog);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)zwrite);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
			RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
		}
	}
	buildingPipe_restoreRenderState(&rs);
	buildingPipe_cleanup();
}

// The PC callback cannot be salvaged. Just do it Xbox-style instead
void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	static int xboxFrameCount = 0;
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

	RwFrame* frame = (RwFrame*)atomic->object.object.parent;
	if(!frame) return;

	_rwD3D9EnableClippingIfNeeded(object, type);

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
			RwD3D9SetVertexShaderConstant(REG_surfProps, &material->surfaceProps, 1);
		}else{
			static float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			RwSurfaceProperties surf = { 1.0f, 1.0f, 1.0f };
			RwD3D9SetVertexShaderConstant(REG_matCol, white, 1);
			RwD3D9SetVertexShaderConstant(REG_surfProps, &surf, 1);
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

	_rwD3D9EnableClippingIfNeeded(object, type);

	RwD3D9SetPixelShaderConstant(0, &spheremapfog, 1);

	pipeGetComposedTransformMatrix(atomic, transform);
	RwD3D9SetVertexShaderConstant(REG_transform, transform, 4);


	RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera);
	float fog[2];
	fog[0] = RwCameraGetFarClipPlane(cam);
	fog[1] = fog[0] - RwCameraGetFogDistance(cam);
//	RwD3D9SetVertexShaderConstant(45, fog, 1);
	RwD3D9SetPixelShaderConstant(2, fog, 1);

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
		RwD3D9SetVertexShaderConstant(REG_surfProps, &material->surfaceProps, 1);

		D3D9Render(resEntryHeader, instancedData);

		instancedData++;
	}
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
	// Render IBL cubemap once per frame (sky capture for vehicle environment reflections)
	// Buildings render before vehicles, so this is the right time
	{
		static RwUInt32 lastIBLFrame = 0;
		RwUInt32 curFrame = RWSRCGLOBAL(renderFrame);
		if(curFrame != lastIBLFrame){
			extern void RenderIBLBuffer(void);
			RenderIBLBuffer();
			lastIBLFrame = curFrame;
		}
	}

	static int pbrFrameCount = 0;
	if(++pbrFrameCount % 300 == 1)
		dbglog("PBRBuilding: frame %d object=%p type=%d flags=0x%X", pbrFrameCount, object, type, flags);

	RpAtomic *atomic = (RpAtomic*)object;

	RwMatrix ident;
	RwMatrixSetIdentity(&ident);

	RwFrame* frame = (RwFrame*)atomic->object.object.parent;
	if(!frame) return;
	_rwD3D9EnableClippingIfNeeded(object, type);

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
			RwD3D9SetVertexShaderConstant(REG_surfProps, &material->surfaceProps, 1);
		}else{
			static float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			RwSurfaceProperties surf = { 1.0f, 1.0f, 1.0f };
			RwD3D9SetVertexShaderConstant(REG_matCol, white, 1);
			RwD3D9SetVertexShaderConstant(REG_surfProps, &surf, 1);
		}

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

		D3D9Render(resEntryHeader, instancedData);
	}

	RwD3D9SetVertexShader(NULL);
	RwD3D9SetPixelShader(NULL);
}

void
CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
//	if(GetAsyncKeyState(VK_F4) & 0x8000)
//		return;

	if(gRenderingSpheremap)
		CCustomBuildingDNPipeline__CustomPipeRenderCB_Sphere(repEntry, object, type, flags);
	else switch(config->buildingPipe){
	default:
	case BUILDING_PS2:
	case BUILDING_GTAIV:  // GTAIV = PS2 callback + ivMode shader swap
		CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2(repEntry, object, type, flags);
		break;
	case BUILDING_XBOX:
		CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox(repEntry, object, type, flags);
		break;
	case BUILDING_PBR:
		CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR(repEntry, object, type, flags);
		break;
	}
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
				for(j = 0; dcl[j].Usage != D3DDECLUSAGE_COLOR || dcl[j].UsageIndex != 1; ++j)
					;
				// If no extra colors, use regular colors
				// TODO: only instance one set in this case
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
					instData->vertexAlpha |= _rpD3D9VertexDeclarationInstColor(
					          (RwUInt8*)vertexData + dcl[j].Offset + resEntryHeader->vertexStream[dcl[j].Stream].stride*instData->minVert,
					          day + instData->minVert,
					          instData->numVertices,
					          resEntryHeader->vertexStream[dcl[j].Stream].stride);
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
