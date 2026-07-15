#pragma once

// chars.h - Human pipeline module
// Interfaces with RenderWare to classify ped atomics (skin, hair, clothes, shoes)
// and drive per-part SSS settings from the BRDF database.
//
// Architecture:
//   chars walks the RW clump hierarchy of each ped using GetFrameNodeName.
//   Each atomic is classified by frame name + texture lookup + bounding box position.
//   Classification is output to a per-pixel buffer during the skin pipe render.
//   The SSS blur reads this buffer to apply different strengths per part.
//   All parts call one shared SSS implementation (SSS_Blur shader).

enum PartType
{
	PARTTYPE_NONE   = 0,
	PARTTYPE_SKIN   = 1,   // exposed skin (face, arms, hands)
	PARTTYPE_HAIR   = 2,   // hair, facial hair
	PARTTYPE_CLOTH  = 3,   // shirt, pants, jacket, accessories
	PARTTYPE_SHOES  = 4,   // footwear
	PARTTYPE_EYES   = 5,   // eye whites/pupils (very low SSS)
	PARTTYPE_ACCESS = 6,   // watches, jewelry, belts (no SSS)

	NUM_PARTTYPES
};

// Per-part SSS parameters (driven by BRDF library)
struct PartSSSParams
{
	float roughness;
	float reflectance;
	float subsurface;
	float specIntensity;
	float sssBlurStrength;
	float sssBlurRadius;
	float pad[2];
};

// Per-atomic classification cache
struct AtomicCharInfo
{
	RpAtomic *atomic;
	int partType;
};

#define MAX_CHAR_ATOMICS 256

void chars_init(void);
void chars_shutdown(void);
void chars_drawSSSBlur(void);

// RW-based classification: walks clump hierarchy, classifies atomics
int chars_classifyAtomic(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics);
int chars_classifyByFrameName(const char *frameName);
int chars_classifyByTexture(RwTexture *tex);
int chars_classifyByPosition(RpAtomic *atomic, RpAtomic **allAtomics, int numAtomics);

PartSSSParams chars_getPartParams(int partType);

// Build classification buffer from visible peds
void chars_buildClassificationBuffer(void);

extern void *SkinPBR;
extern void *SSS_Blur;
extern PartSSSParams g_partSSSParams[NUM_PARTTYPES];
extern struct IDirect3DTexture9 *g_charsClassifyTex;
