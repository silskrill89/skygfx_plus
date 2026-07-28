//
//
//
//
//
// PC_GrassRenderer - interface class for drawing plants on GTA_PC/GTA_XBOX:
//
//	03/03/2003	-	Andrzej:	- started;
//	30/07/2004	-	JohnW:		- PC version of this stuff
//	10/04/2005	-	Andrzej:	- full port to GTA_PC/GTA_XBOX;
//
// --------------------------------------------------------------------------------------------------------
// Creating "one big" clump with many plant atomics:
// this doesn't seem to work. It seems that the code fills the buffer completely
// and then empties it by rendering out the contents. Fine on PS2, but on PC we want
// to cache TriPlants into clumps which we keep as long as possible. Otherwise the overhead of
// creating clumps is wasted
//
//


#include <rwcore.h>
#include <rpworld.h>

#include "skygfx.h"
#include "PC_GrassRenderer.h"

extern float &CTimer__ms_fTimeStep;
extern float &CWeather__Wind;
extern CVector& CWeather__WindDir;

#ifndef ASSERT
#define ASSERT(x)
#endif

#ifndef ASSERTMSG
#define ASSERTMSG(x, msg)
#endif

#ifndef FX_QUALITY_HIGH
#define FX_QUALITY_HIGH 2
#endif

static int g_fxQuality = FX_QUALITY_HIGH;

// Stub implementations for missing GTA SA functions
static uint32 g_randomSeed = 12345;

float CGeneral_GetRandomNumber(void) {
	g_randomSeed = g_randomSeed * 1103515245 + 12345;
	return (float)(g_randomSeed & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

float CGeneral_GetRandomNumberInRange(float min, float max) {
	return min + CGeneral_GetRandomNumber() * (max - min);
}

void CGeneral_SetRandomSeed(uint32 seed) {
	g_randomSeed = seed;
}

float CMaths_Sin(float x) { return sinf(x); }
int CMaths_Floor(float x) { return (int)floorf(x); }
int CMaths_Min(int a, int b) { return a < b ? a : b; }
int CMaths_Max(int a, int b) { return a > b ? a : b; }

CVector	CGrassRenderer::m_vecCameraPos;
float	CGrassRenderer::m_windBending	= 0.0f;
float	CGrassRenderer::m_closeDist	= 0.0f;
float	CGrassRenderer::m_farDist		= 10.0f;



static
CPPTriPlantBuffer	PPTriPlantBuffer;




//
//
//
//
CGrassRenderer::CGrassRenderer()
{
}


//
//
//
//
CGrassRenderer::~CGrassRenderer()
{
}




//
//
//
//
void CGrassRenderer::Shutdown()
{
	// do nothing
}


//static uint16		m_oldScanCode = -1;
static uint16 		m_currScanCode = -1;
//static RpAtomic**	m_uploadedPlantModelsSet = NULL;

//
//
//
//
void CGrassRenderer::SetCurrentScanCode(uint16 currScanCode)
{
	m_currScanCode = currScanCode;
}



//
//
//
//
void CGrassRenderer::AddTriPlant(PPTriPlant *pSrcPlant, uint32 ePlantModelSet)
{
	PPTriPlantBuffer.ChangeCurrentPlantModelsSet(ePlantModelSet);

	PPTriPlant *pDstPlant = PPTriPlantBuffer.GetPPTriPlantPtr(1);

	*pDstPlant = *pSrcPlant;
	
	PPTriPlantBuffer.IncreaseBufferIndex(ePlantModelSet, 1);

}// end of CGrassRenderer::AddPlantCluster()...



//
//
//
//
void CGrassRenderer::FlushTriPlantBuffer()
{
	PPTriPlantBuffer.Flush();
}



//
//
//
//
bool8 CGrassRenderer::SetPlantModelsTab(uint32 index, RpAtomic** plantModels)
{
	return(PPTriPlantBuffer.SetPlantModelsTab(index, plantModels));
}


//
//
//
//
RpAtomic** CGrassRenderer::GetPlantModelsTab(uint32 index)
{
	return(PPTriPlantBuffer.GetPlantModelsTab(index));
}



//
//
//
// updates global camera pos; should be called every frame;
//
void CGrassRenderer::SetGlobalCameraPos(const CVector& camPos)
{
	m_vecCameraPos = camPos;
}


//
//
//
// sets FAR_ALPHA_DIST and CLOSE_ALPHA_DIST, which control how alpha fading works:
//
void CGrassRenderer::SetCloseFarAlphaDist(float closeDist, float farDist)
{
	m_closeDist	= closeDist;
	m_farDist	= farDist;
}



//
//
//
//
void CGrassRenderer::SetGlobalWindBending(float bending)
{
	m_windBending = bending;
}







static RwTexture *_pPlantTexture=NULL;

//
//
//
//
static
RpMaterial* setMatColourAndTextureCB(RpMaterial* pMat, void* pData)
{

RwRGBA* pCol = (RwRGBA*)pData;

	RpMaterialSetColor(pMat, pCol);

	RwTexture *pOldTexture = RpMaterialGetTexture(pMat);
	if(pOldTexture != _pPlantTexture)
	{
		RpMaterialSetTexture(pMat, _pPlantTexture);
	}

	return(pMat);
}

//
//
//
//
static
void SetMaterialColourAndTexture(RpAtomic* pAtomic, RwRGBA color, RwTexture *pTexture)
{
	RpGeometry* pGeom = RpAtomicGetGeometry(pAtomic);

	_pPlantTexture = pTexture;

	RpGeometryForAllMaterials(pGeom, setMatColourAndTextureCB, &color);
}



//
//
//
//
RwBool CGrassRenderer::DrawTriPlants(PPTriPlant *triPlants, RwInt32 numTriPlants, RpAtomic** plantModelsTab, RwMatrix *pLTM)
{
		ASSERT(triPlants);
		ASSERT(plantModelsTab);


int32 TriIdx = 0;

		//do for all the TriPlants in the buffer
		while(numTriPlants > TriIdx)
		{	
			PPTriPlant *pCurrTriPlant = &triPlants[TriIdx];

			const CVector *v1 = (CVector*)&(pCurrTriPlant->V1);
			const CVector *v2 = (CVector*)&(pCurrTriPlant->V2);
			const CVector *v3 = (CVector*)&(pCurrTriPlant->V3);

			// ----- set alpha for this tri plant --------
			// check if point too close to camera to render safely
			CVector diff(pCurrTriPlant->center - m_vecCameraPos);
			const float dist = diff.Magnitude();

#define	FAR_FADE_OUT			(20)
#define	MAX_PLANT_ALPHA			(128)

			//reduce plant numbers for medium quality
			float farDist = 0;
			if (g_fxQuality >= FX_QUALITY_HIGH)
			{
				farDist = CGrassRenderer::m_farDist;
			}
			else
			{
				farDist = CGrassRenderer::m_farDist/2;
			}

			const float farFadeLimit = farDist + FAR_FADE_OUT;
#if 1
			if(dist > farFadeLimit)
			{
				TriIdx++;
				continue; //too far away to render
			}
#endif


			RpAtomic* pAtomic	= plantModelsTab[pCurrTriPlant->model_id];	//select the model
			ASSERT(pAtomic);
			RwFrame *pFrame		= RpAtomicGetFrame(pAtomic);
			ASSERT(pFrame);
			RwMatrix *pRwMatrix	= RwFrameGetMatrix(pFrame);
			ASSERT(pRwMatrix);

			// want same values for plant positions each time...
			CGeneral_SetRandomSeed(pCurrTriPlant->seed);

			RwRGBA PCcolour;
			const uint8 srcAlpha = pCurrTriPlant->color.alpha;	//MAX_PLANT_ALPHA;
			if(dist < farDist)
			{
				PCcolour.alpha = srcAlpha;
			}
			else
			{
				int32 alpha = CMaths_Floor( (farFadeLimit - dist)/FAR_FADE_OUT * srcAlpha);
				alpha = CMaths_Min(alpha, 255);
				alpha = CMaths_Max(alpha, 0);
				PCcolour.alpha = alpha;
			}

	
			// intensity support:  final_I = I + VarI*rand01()
			// (btw. should be done per every plant basis, not whole LocTri basis)
			const int16 intensityVar = pCurrTriPlant->intensity_var;
			uint16 intensity = pCurrTriPlant->intensity;	// + uint16(float(intensityVar)*CGeneral_GetRandomNumberInRange(0.0f, 1.0f));
			intensity = CMaths_Min(intensity, 255);

#define CALC_COLOR_INTENSITY(COLOR, I)			uint8((uint16(COLOR)*I) >> 8)
			PCcolour.red		= CALC_COLOR_INTENSITY(pCurrTriPlant->color.red,	intensity);
			PCcolour.green		= CALC_COLOR_INTENSITY(pCurrTriPlant->color.green,	intensity);
			PCcolour.blue		= CALC_COLOR_INTENSITY(pCurrTriPlant->color.blue,	intensity);
#undef CALC_COLOR_INTENSITY

			// can't set the material colour per plant - wonky results... Set it per plantTri
			SetMaterialColourAndTexture(pAtomic, PCcolour, pCurrTriPlant->texture_ptr);
			

			/* I would prefer to have cached clumps containing the atomics for
				each TriPlant and render those instead. But currently the buffer is 
				constantly filled then emptied so a good bit of code needs re-written.
				Come back to this later to optimise? - JW */
				

			// render a single triPlant full of the desired model
			const int32 count = pCurrTriPlant->num_plants; 
			for(int32 i=0; i<count; i++)
			{
				const float s = CGeneral_GetRandomNumberInRange(0.0f, 1.0f);
				const float t = CGeneral_GetRandomNumberInRange(0.0f, 1.0f);
				CVector posPlant;
				CGrassRenderer::GenPointInTriangle(&posPlant, v1, v2, v3, s, t);

				// check if point too close to camera to render safely
				CVector diff(posPlant - m_vecCameraPos);
				const float dist = diff.Magnitude();

#define	NEAR_FADE_OUT	(2)

				const float nearFadeLimit = CGrassRenderer::m_closeDist - NEAR_FADE_OUT;
				if (dist < nearFadeLimit)
				{
					// call getting random number to avoid problems with generating XYZ scale and bending variation:
					CGeneral_GetRandomNumberInRange(0.0f, 1.0f);	
					CGeneral_GetRandomNumberInRange(0.0f, 1.0f);	
					CGeneral_GetRandomNumberInRange(0.0f, 1.0f);
					continue; //too close to render
				}


				RwFrameTranslate(pFrame, (RwV3d*)&posPlant, rwCOMBINEREPLACE);
				//this doesn't work .at isn't initialised

				// ---- apply various amounts of scaling to the matrix -----
				// calculate an x/y scaling value and apply to matrix

				// final_SclXY = SclXY + SclVarXY*rand01()
				const float	variationXY	= pCurrTriPlant->scale_var_xy;
				const float	scaleXY		= pCurrTriPlant->scale.x + variationXY*CGeneral_GetRandomNumberInRange(0.0f, 1.0f);	
				pRwMatrix->right.x	*= scaleXY;		// scale in XY
				pRwMatrix->up.y		*= scaleXY;		// scale in XY

				// calculate a z scaling value and apply to matrix
				const float	variationZ	= pCurrTriPlant->scale_var_z;
				const float	scaleZ		= pCurrTriPlant->scale.y + variationZ*CGeneral_GetRandomNumberInRange(0.0f, 1.0f);
				pRwMatrix->at.z		*= scaleZ;			// scale in Z
				// ----- end of scaling stuff ------


				// ---- muck about with the matrix to make atomic look blown by the wind --------
#if 1
				// use here wind_bend_scale & wind_bend_var
				//	final_bend = [ 1.0 + (bend_var*rand(-1,1)) ] * [ global_wind * bend_scale ]
				const float variationBend = pCurrTriPlant->wind_bend_var;
				const float bending =	(1.0f + (variationBend*CGeneral_GetRandomNumberInRange(0.0f, 1.0f))) *
										(m_windBending * pCurrTriPlant->wind_bend_scale);
				pRwMatrix->at.x = bending;
				pRwMatrix->at.y = bending;
#else
				pRwMatrix->at.x = 0.3f * CMaths_Sin( ((CTimer__ms_fTimeStep * 1000.0f+(i*91))&4095) * (6.28f/4096.0f) );
				pRwMatrix->at.y = pRwMatrix->at.x;
				pRwMatrix->at.x *= CWeather__WindDir.x;
				pRwMatrix->at.y *= CWeather__WindDir.y;
#endif
				// ----- end of wind stuff -----

				RwMatrixUpdate(pRwMatrix);

				// do the rendering stuff
				RpAtomicRender(pAtomic);

			}//for(int32 i=0; i<count; i++)...
			
			TriIdx++;
		}// end of while(numTriPlants > TriIdx)...

        return(TRUE);
}// end of CPPTriPlantBuffer::DrawTriPlants()...




//
//
//
//
#ifdef __GNUC__
#pragma mark --- PPTriPlantBuffer stuff ---
#endif

//
//
//
//
CPPTriPlantBuffer::CPPTriPlantBuffer()
{
	this->m_currentIndex = 0;
	this->SetPlantModelsSet(PPPLANTBUF_MODEL_SET0);
	
	for(int32 i=0; i<PPTRIPLANT_MODELS_TAB_SIZE; i++)
	{
		m_pPlantModelsTab[i] = NULL;
	}
}

//
//
//
//
CPPTriPlantBuffer::~CPPTriPlantBuffer()
{

}


//
//
//
//
void CPPTriPlantBuffer::Flush()
{
	if(m_currentIndex>0)
	{
		RpAtomic** plantModels = NULL;
	
		// flush the buffer:
		switch(this->m_plantModelsSet)
		{
			case(PPPLANTBUF_MODEL_SET0):	plantModels = m_pPlantModelsTab[0];		break;
			case(PPPLANTBUF_MODEL_SET1):	plantModels = m_pPlantModelsTab[1];		break;
			case(PPPLANTBUF_MODEL_SET2):	plantModels = m_pPlantModelsTab[2];		break;
			case(PPPLANTBUF_MODEL_SET3):	plantModels = m_pPlantModelsTab[3];		break;
			default:						plantModels = NULL;						break;
		}
		ASSERTMSG(plantModels, "Unknown PlantModelsSet!");


#if 0
		// this is not necessary for PC/Xbox:

		//
		// decide if we need to upload PlantModelsSet (maybe it's already uploaded):
		//
		bool8 bUploadPlantModels = FALSE;

		// detect "next frame" change:
		if(m_oldScanCode != m_currScanCode)
		{
			m_oldScanCode		= m_currScanCode;
			bUploadPlantModels	= TRUE;
		}

		// detect "next model set" change during frame:
		if(plantModels != m_uploadedPlantModelsSet)
		{
			bUploadPlantModels			= TRUE;
			m_uploadedPlantModelsSet	= plantModels;
		}

		//bUploadPlantModels = TRUE;
#endif

		// save current seed:
		const uint32 nextSeed = CGeneral_GetRandomNumber();
		{
			CGrassRenderer::DrawTriPlants(m_Buffer, m_currentIndex, plantModels,	NULL);
			m_currentIndex = 0;
		}
		// restore randomness:
		CGeneral_SetRandomSeed(nextSeed);
	}
}


//
//
//
//
PPTriPlant* CPPTriPlantBuffer::GetPPTriPlantPtr(int32 amountToAdd)
{
	if((this->m_currentIndex+amountToAdd) > PPTRIPLANT_BUFFER_SIZE)
	{
		this->Flush();
	}

	return(&this->m_Buffer[m_currentIndex]);

}


//
//
//
//

void CPPTriPlantBuffer::ChangeCurrentPlantModelsSet(int32 newSet)
{
	// different modes of pipeline?
	if(this->GetPlantModelsSet() != newSet)
	{
		// flush contents of old pipeline mode:
		this->Flush();
		
		// set new pipeline mode:
		this->SetPlantModelsSet(newSet);
	}

}


//
//
//
//
void CPPTriPlantBuffer::IncreaseBufferIndex(int32 pipeMode, int32 amount)
{
	if(this->GetPlantModelsSet() != pipeMode)
	{
		// incompatible pipeline modes!
		ASSERTMSG(FALSE, "Incompatible pipeline modes!");
		return;
	}

	this->m_currentIndex += amount;
	if(this->m_currentIndex >= PPTRIPLANT_BUFFER_SIZE)
	{
		this->Flush();
	}
}



//
//
//
//
bool8 CPPTriPlantBuffer::SetPlantModelsTab(uint32 index, RpAtomic** pPlantModels)
{
	if(index >= PPTRIPLANT_MODELS_TAB_SIZE)
		return(FALSE);
		
	this->m_pPlantModelsTab[index] = pPlantModels;

	return(TRUE);
}



//
//
//
//
RpAtomic** CPPTriPlantBuffer::GetPlantModelsTab(uint32 index)
{
	if(index >= PPTRIPLANT_MODELS_TAB_SIZE)
		return(NULL);
		
	RpAtomic** plantModels = this->m_pPlantModelsTab[index];

	return(plantModels);
}





