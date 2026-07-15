#pragma once

#include "skygfx.h"
#include "PC_GrassRenderer.h"

extern "C" {
	extern float &CTimer__ms_fTimeStep;
	extern float &CWeather__Wind;
	extern CVector& CWeather__WindDir;
}

typedef uint8 bool8;
typedef uint16 uint16;
typedef int32 int32;

struct PPTriPlant
{
	CVector V1, V2, V3;
	CVector center;
	uint16 model_id;
	uint16 num_plants;
	RwV2d scale;
	RwTexture *texture_ptr;
	RwRGBA color;
	uint8 intensity;
	uint8 intensity_var;
	float seed;
	float scale_var_xy;
	float scale_var_z;
	float wind_bend_scale;
	float wind_bend_var;
};

#define PPTRIPLANT_BUFFER_SIZE 32
#define PPTRIPLANT_MODELS_TAB_SIZE 4

enum PlantModelSet {
	PPPLANTBUF_MODEL_SET0 = 0,
	PPPLANTBUF_MODEL_SET1 = 1,
	PPPLANTBUF_MODEL_SET2 = 2,
	PPPLANTBUF_MODEL_SET3 = 3
};

class CGrassRenderer
{
public:
	CGrassRenderer();
	~CGrassRenderer();

public:
	static bool Initialise();
	static void Shutdown();

public:
	static void AddTriPlant(PPTriPlant *pPlant, uint32 ePlantModelSet);
	static void FlushTriPlantBuffer();

public:
	static RwBool DrawTriPlants(PPTriPlant *triPlants, int32 numTris, RpAtomic **plantModelsTab, RwMatrix *pLTM);

private:
	inline static void GenPointInTriangle(CVector *pRes, const CVector *V1, const CVector *V2, const CVector *V3, float s, float t);

public:
	static bool8 SetPlantModelsTab(uint32 index, RpAtomic **plantModels);
	static RpAtomic **GetPlantModelsTab(uint32 index);

public:
	static void SetGlobalCameraPos(const CVector& camPos);
	static void SetCloseFarAlphaDist(float closeDist, float farDist);
	static void SetGlobalWindBending(float bending);

public:
	static void SetCurrentScanCode(uint16 currScanCode);

private:
	static CVector m_vecCameraPos;
	static float m_closeDist;
	static float m_farDist;
	static float m_windBending;

	static uint16 m_currScanCode;
	static RpAtomic *plantModels[PPTRIPLANT_MODELS_TAB_SIZE];
	static PPTriPlant m_Buffer[PPTRIPLANT_BUFFER_SIZE];
	static int32 m_currentIndex;
};

extern CGrassRenderer gGrassRenderer;

inline void CGrassRenderer::GenPointInTriangle(CVector *pRes, const CVector *V1, const CVector *V2, const CVector *V3, float s, float t)
{
	if (s + t > 1.0f) {
		s = 1.0f - s;
		t = 1.0f - t;
	}
	const float a = 1.0f - s - t;
	const float b = s;
	const float c = t;
	pRes->x = a * V1->x + b * V2->x + c * V3->x;
	pRes->y = a * V1->y + b * V2->y + c * V3->y;
	pRes->z = a * V1->z + b * V2->z + c * V3->z;
}