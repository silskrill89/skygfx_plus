// veh_shaders.cpp
// Single shader data bridge — all vehicle→shader data goes through here.
// vehiclePipe.cpp calls these functions, which query vehicles.cpp for classification.
//
// Hierarchy: vehiclePipe.cpp → veh_shaders.cpp → vehicles.cpp (data files)
//
// This file owns: paint types, paint weights, light tints, glass tints, tire props,
// mesh detection, and PBR property computation.
// Uses unified BRDF library for consistent materials across all asset types.

#include "skygfx.h"
#include "brdfLibrary.h"
#include <string.h>

// ============================================================
// External API from vehicles.cpp (vehicle registry)
// ============================================================

extern int GetVehicleGroup(int modelID);
extern int GetVehicleEraByID(int modelID);
extern int GetVehicleDrive(int modelID);
extern bool IsVehicleCopByID(int modelID);
extern bool IsVehicleTaxiByID(int modelID);
extern bool IsVehicleFWDByID(int modelID);

extern void Vehicles_Init(const char *gameDir);

// ============================================================
// Paint types — 5 types (including Grid-style clearcoat)
// ============================================================

// CryEngine-style paint properties (NOT Unreal metallic/roughness)
struct PaintProps {
    float specular;        // 0.02-0.05 dielectric, 0.5-1.0 metal
    float glossiness;      // 0=rough, 1=smooth
    float specularTintR;   // Specular color tint (for metals)
    float specularTintG;
    float specularTintB;
    float noiseScale;
    float edgeBlend;
};

static const PaintProps paintTable[5] = {
    // Index 0 = Gloss:       clear coat, mirror-smooth
    { 0.06f, 0.90f, 1.0f, 1.0f, 1.0f, 0.20f, 1.0f },
    // Index 1 = Metallic:    metallic flake, shiny (slight tint for sparkle)
    { 0.70f, 0.88f, 0.9f, 0.9f, 0.9f, 0.25f, 1.2f },
    // Index 2 = Matte:       flat, no reflection
    { 0.04f, 0.30f, 1.0f, 1.0f, 1.0f, 0.05f, 0.3f },
    // Index 3 = Satin:       semi-gloss
    { 0.05f, 0.60f, 1.0f, 1.0f, 1.0f, 0.15f, 0.8f },
    // Index 4 = Clearcoat:   Grid-style (very smooth, strong Fresnel, deep color)
    { 0.06f, 0.95f, 1.0f, 1.0f, 1.0f, 0.15f, 1.5f },
};

// Paint probability weights per vehicle group [group][paintType]
// Indexed by VGROUP_* from vehicles.cpp
// Fancy types (Metallic, Clearcoat) get higher spawn rates
static const float paintWeights[][5] = {
    //                          Gloss  Metal  Matte  Satin  Clearcoat
    /* VGROUP_STANDARD  */  {  0.35f, 0.30f, 0.10f, 0.10f, 0.15f },
    /* VGROUP_SPORT     */  {  0.15f, 0.25f, 0.05f, 0.05f, 0.50f },
    /* VGROUP_MUSCLE    */  {  0.25f, 0.35f, 0.10f, 0.10f, 0.20f },
    /* VGROUP_CLASSIC   */  {  0.40f, 0.25f, 0.10f, 0.10f, 0.15f },
    /* VGROUP_LOWRIDER  */  {  0.15f, 0.40f, 0.05f, 0.15f, 0.25f },
    /* VGROUP_LUXURY    */  {  0.15f, 0.35f, 0.02f, 0.05f, 0.43f },
    /* VGROUP_TRUCK     */  {  0.35f, 0.20f, 0.20f, 0.10f, 0.15f },
    /* VGROUP_VAN       */  {  0.40f, 0.15f, 0.20f, 0.10f, 0.15f },
    /* VGROUP_UTILITY   */  {  0.40f, 0.10f, 0.30f, 0.10f, 0.10f },
    /* VGROUP_EMERGENCY */  {  0.50f, 0.15f, 0.10f, 0.10f, 0.15f },
    /* VGROUP_MILITARY  */  {  0.25f, 0.10f, 0.45f, 0.10f, 0.10f },
    /* VGROUP_BIKE      */  {  0.25f, 0.30f, 0.10f, 0.10f, 0.25f },
    /* VGROUP_BOAT      */  {  0.35f, 0.20f, 0.15f, 0.10f, 0.20f },
    /* VGROUP_AIRCRAFT  */  {  0.30f, 0.20f, 0.25f, 0.10f, 0.15f },
};

// ============================================================
// Light tints — indexed by VehicleEra from vehicles.cpp
// ============================================================

struct LightTint { float r, g, b; };

// [era][0=headlight, 1=taillight]
static const LightTint lightTints[][2] = {
    // VERA_PRE80: yellow sealed beam, orangey taillight
    { { 1.00f, 0.82f, 0.45f }, { 1.00f, 0.35f, 0.10f } },
    // VERA_80S: warm halogen, standard red
    { { 1.00f, 0.90f, 0.65f }, { 1.00f, 0.22f, 0.10f } },
    // VERA_90S: clear white, deep red
    { { 1.00f, 0.97f, 0.92f }, { 0.90f, 0.05f, 0.04f } },
    // VERA_UTILITY: amber, utilitarian red
    { { 1.00f, 0.85f, 0.55f }, { 1.00f, 0.30f, 0.12f } },
};

// ============================================================
// Glass tints — per vehicle class
// ============================================================

struct GlassTint { float r, g, b, strength; };

static const GlassTint glassDefaults = { 0.18f, 0.19f, 0.22f, 0.15f };
static const GlassTint glassCop      = { 0.06f, 0.07f, 0.10f, 0.50f };
static const GlassTint glassTaxi     = { 0.35f, 0.28f, 0.15f, 0.30f };
static const GlassTint glassFWD      = { 0.10f, 0.22f, 0.30f, 0.25f };

// ============================================================
// Tire props — per era (CryEngine-style: specular/glossiness)
// ============================================================

struct TireProps { float specular, glossiness, tintR, tintG, tintB; };

// Tire properties per vehicle era
// Tint values control rubber color: lower = greyer/worn, higher = darker/newer
// PRE80/UTILITY: old, weathered tires — grey/brown rubber
// 80S: standard mid-age — medium grey
// 90S: newer cars — dark black rubber
static const TireProps tireTable[] = {
    { 0.04f, 0.08f, 0.55f, 0.50f, 0.45f },  // VERA_PRE80 — old grey rubber, worn brown tint
    { 0.04f, 0.10f, 0.40f, 0.38f, 0.35f },  // VERA_80S — standard grey rubber
    { 0.04f, 0.12f, 0.20f, 0.20f, 0.22f },  // VERA_90S — dark black, newer rubber
    { 0.04f, 0.06f, 0.50f, 0.45f, 0.40f },  // VERA_UTILITY — off-road, dusty grey-brown
};

// ============================================================
// Init — called once from vehiclePipe init
// ============================================================

static bool shadersInitialized = false;

void VehShaders_Init(const char *gameDir){
    if(shadersInitialized) return;
    shadersInitialized = true;
    Vehicles_Init(gameDir);
    dbglog("VehShaders_Init: bridge initialized");
}

// ============================================================
// Paint type selection (deterministic per model via hash)
// ============================================================

int VehShaders_SelectPaintType(int modelID, unsigned int hash){
    int group = GetVehicleGroup(modelID);
    if(group < 0 || group >= 14) group = 0;
    float r = (float)(hash & 0xFFFF) / 65535.0f;
    const float *w = paintWeights[group];
    float cum = w[0];
    if(r > cum){ cum += w[1]; if(r <= cum) return 1; }
    if(r > cum){ cum += w[2]; if(r <= cum) return 2; }
    if(r > cum){ cum += w[3]; if(r <= cum) return 3; }
    if(r > cum) return 4;
    return 0;
}

// ============================================================
// PBR property queries (CryEngine-style: specular/glossiness)
// ============================================================

void VehShaders_GetPaintPBR(int paintType, float *specular, float *glossiness,
                             float *specularTintR, float *specularTintG, float *specularTintB,
                             float *noiseScale, float *edgeBlend){
    if(paintType < 0 || paintType > 3) paintType = 0;
    const PaintProps *p = &paintTable[paintType];
    *specular = p->specular;
    *glossiness = p->glossiness;
    *specularTintR = p->specularTintR;
    *specularTintG = p->specularTintG;
    *specularTintB = p->specularTintB;
    *noiseScale = p->noiseScale;
    *edgeBlend = p->edgeBlend;
}

// ============================================================
// Light tint queries
// ============================================================

void VehShaders_GetHeadlightTint(int modelID, float *r, float *g, float *b){
    int era = GetVehicleEraByID(modelID);
    if(era < 0 || era > 3) era = 1;
    *r = lightTints[era][0].r;
    *g = lightTints[era][0].g;
    *b = lightTints[era][0].b;
}

void VehShaders_GetTaillightTint(int modelID, float *r, float *g, float *b){
    int era = GetVehicleEraByID(modelID);
    if(era < 0 || era > 3) era = 1;
    *r = lightTints[era][1].r;
    *g = lightTints[era][1].g;
    *b = lightTints[era][1].b;
}

// ============================================================
// Glass tint query
// ============================================================

void VehShaders_GetGlassTint(int modelID, float *r, float *g, float *b, float *strength){
    const GlassTint *gt;
    if(IsVehicleCopByID(modelID))     gt = &glassCop;
    else if(IsVehicleTaxiByID(modelID)) gt = &glassTaxi;
    else if(IsVehicleFWDByID(modelID))  gt = &glassFWD;
    else                                gt = &glassDefaults;
    *r = gt->r; *g = gt->g; *b = gt->b; *strength = gt->strength;
}

// ============================================================
// Tire prop query
// ============================================================

void VehShaders_GetTireProps(int modelID, float *specular, float *glossiness,
                              float *tintR, float *tintG, float *tintB){
    int era = GetVehicleEraByID(modelID);
    if(era < 0 || era > 3) era = 1;
    const TireProps *t = &tireTable[era];
    *specular = t->specular;
    *glossiness = t->glossiness;
    *tintR = t->tintR;
    *tintG = t->tintG;
    *tintB = t->tintB;
}

// ============================================================
// Texture name detection (mesh type identification)
// All detection delegates to VehShaders_GetSurfaceType() —
// single source of truth using vehicles.txd texture names.
// ============================================================

bool VehShaders_IsTireTexture(const char *texName){
    return texName && VehShaders_GetSurfaceType(texName) == SURFACE_CAR_TIRE;
}

bool VehShaders_IsHeadlightTexture(const char *texName){
    return texName && VehShaders_GetSurfaceType(texName) == SURFACE_CAR_HEADLIGHT;
}

bool VehShaders_IsTaillightTexture(const char *texName){
    return texName && VehShaders_GetSurfaceType(texName) == SURFACE_CAR_TAILLIGHT;
}

bool VehShaders_IsGlassTexture(const char *texName, bool hasAlpha, unsigned char alpha){
    // Positive match: known glass texture names from vehicles.txd
    if(texName && VehShaders_GetSurfaceType(texName) == SURFACE_CAR_GLASS)
        return true;
    // Fallback: alpha-based elimination for modded vehicles with unknown names
    if(!hasAlpha || alpha >= 200) return false;
    if(VehShaders_IsHeadlightTexture(texName)) return false;
    if(VehShaders_IsTaillightTexture(texName)) return false;
    return true;
}

// Get unified surface type from vehicle texture name
// Returns SURFACE_CAR_* for vehicle-specific materials
int VehShaders_GetSurfaceType(const char *texName){
    if(!texName || !texName[0]) return SURFACE_DEFAULT;
    
    // Vehicle-specific materials (check first for priority)
    if(strstri(texName, "chrome") || strstri(texName, "bumper_chrome"))
        return SURFACE_CAR_CHROME;
    if(strstri(texName, "tire") || strstri(texName, "tyre") || strstri(texName, "wheel_rubber"))
        return SURFACE_CAR_TIRE;
    if(strstri(texName, "wheel") || strstri(texName, "alloy") || strstri(texName, "rim"))
        return SURFACE_CAR_WHEEL;
    if(strstri(texName, "headlight") || strstri(texName, "light_front"))
        return SURFACE_CAR_HEADLIGHT;
    if(strstri(texName, "taillight") || strstri(texName, "light_rear"))
        return SURFACE_CAR_TAILLIGHT;
    if(strstri(texName, "carbon"))
        return SURFACE_CAR_CARBON;
    if(strstri(texName, "leather"))
        return SURFACE_CAR_LEATHER;
    if(strstri(texName, "fabric") || strstri(texName, "seat"))
        return SURFACE_CAR_FABRIC;
    if(strstri(texName, "windscreen") || strstri(texName, "window") || strstri(texName, "glass"))
        return SURFACE_CAR_GLASS;
    if(strstri(texName, "trim") || strstri(texName, "plastic_interior"))
        return SURFACE_CAR_PLASTIC;
    if(strstri(texName, "rubber_seal") || strstri(texName, "seal"))
        return SURFACE_CAR_RUBBER;
    // Dirt overlay texture detection (GTA SA dirt system)
    if(strstri(texName, "vehiclegrunge") || strstri(texName, "grunge") || strstri(texName, "dirt"))
        return SURFACE_CAR_DIRT;
    if(strstri(texName, "rust"))
        return SURFACE_CAR_RUST;
    
    // Default to car body for unknown vehicle textures
    return SURFACE_CAR_BODY;
}

// ============================================================
// Dirt level from CVehicle (offset 0x4B0, 0.0=clean, 15.0=max dirt)
// ============================================================

// Get dirt level from a CVehicle pointer
// Returns normalized dirt level 0.0 (clean) to 1.0 (max dirt)
float VehShaders_GetDirtLevel(void *vehicle){
    if(!vehicle) return 0.0f;
    // CVehicle::m_fDirtLevel at offset 0x4B0
    float dirtLevel = *(float*)((char*)vehicle + 0x4B0);
    // Normalize from 0-15 to 0-1
    return max(0.0f, min(dirtLevel / 15.0f, 1.0f));
}

// Apply dirt modification to PBR properties (CryEngine-style)
// Dirt increases roughness (reduces glossiness), reduces specular
void VehShaders_ApplyDirtToPBR(float dirtLevel, float *specular, float *glossiness, float *specularTintR, float *specularTintG, float *specularTintB){
    // Dirt makes surfaces rougher (lower glossiness) and less reflective (lower specular)
    *glossiness = max(*glossiness * (1.0f - dirtLevel * 0.60f), 0.05f);
    *specular = max(*specular * (1.0f - dirtLevel * 0.40f), 0.02f);
    // Dirt tint (brownish-grey)
    *specularTintR = *specularTintR * (1.0f - dirtLevel * 0.3f) + dirtLevel * 0.15f;
    *specularTintG = *specularTintG * (1.0f - dirtLevel * 0.3f) + dirtLevel * 0.10f;
    *specularTintB = *specularTintB * (1.0f - dirtLevel * 0.3f) + dirtLevel * 0.05f;
}

// ============================================================
// Model index extraction (from atomic ID)
// ============================================================

int VehShaders_GetModelIndex(void *atomic){
    unsigned short atomId = CVisibilityPlugins__GetAtomicId((RpAtomic*)atomic);
    return atomId & 0x7FF;
}

// ============================================================
// Area-based color saturation system
// GTA SA zones: Richman, Rodeo, Hollywood/Beverly Hills, Vinewood
// ============================================================

enum VehColorArea {
    COLORAREA_DEFAULT = 0,      // Standard LA — moderate saturation
    COLORAREA_RICHMAN,          // Rich hills — high saturation, any color
    COLORAREA_RODEO,            // Beverly Hills — very high saturation
    COLORAREA_HOLLYWOOD,        // Hollywood/Vinewood — craziest colors only
    COLORAREA_INDUSTRIAL,       // Industrial — lower saturation
    COLORAREA_COUNT
};

// Minimum saturation thresholds per area
// 0.0 = any color allowed, 1.0 = only fully saturated colors
static const float minSaturation[] = {
    0.15f,  // DEFAULT — moderate minimum
    0.30f,  // RICHMAN — rejects boring colors
    0.40f,  // RODEO — high saturation minimum
    0.55f,  // HOLLYWOOD — only craziest colors pass
    0.05f,  // INDUSTRIAL — almost anything allowed
};

// Maximum brightness cap per area (prevents washed-out colors)
static const float maxBrightness[] = {
    0.85f,  // DEFAULT
    0.95f,  // RICHMAN
    1.00f,  // RODEO
    1.00f,  // HOLLYWOOD
    0.80f,  // INDUSTRIAL
};

// Random color with saturation filtering
// Returns true if color passes area filter, false if rejected (should re-roll)
bool VehShaders_CheckColorSaturation(float r, float g, float b, int area){
    if(area < 0 || area >= COLORAREA_COUNT) area = COLORAREA_DEFAULT;

    // Calculate HSL saturation from RGB
    float maxC = max(max(r, g), b);
    float minC = min(min(r, g), b);
    float luma = (maxC + minC) * 0.5f;
    float sat = 0.0f;
    if(maxC > 0.001f){
        float delta = maxC - minC;
        sat = delta / maxC;  // HSV saturation (simpler than HSL for filtering)
    }

    // Reject if below minimum saturation for this area
    if(sat < minSaturation[area])
        return false;

    // Reject if too bright/washed out
    if(luma > maxBrightness[area])
        return false;

    return true;
}

// Generate a random color for a vehicle in a given area
// Uses hash for deterministic per-vehicle randomness
// Applies saturation filtering — rejects boring colors
void VehShaders_GenerateColor(unsigned int hash, int area,
    float *outR, float *outG, float *outB)
{
    if(area < 0 || area >= COLORAREA_COUNT) area = COLORAREA_DEFAULT;

    float minSat = minSaturation[area];
    float maxBri = maxBrightness[area];

    // Generate base color from hash (HSV space for better control)
    float h = (float)(hash & 0xFF) / 255.0f;  // hue [0,1]
    float s = (float)((hash >> 8) & 0xFF) / 255.0f;  // saturation [0,1]
    float v = (float)((hash >> 16) & 0xFF) / 255.0f;  // value [0,1]

    // Scale saturation: minimum from area, maximum always 1.0
    s = minSat + s * (1.0f - minSat);

    // Scale brightness: cap from area
    v = v * maxBri;

    // HSV to RGB conversion
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if(h < 1.0f/6.0f)      { r = c; g = x; b = 0; }
    else if(h < 2.0f/6.0f) { r = x; g = c; b = 0; }
    else if(h < 3.0f/6.0f) { r = 0; g = c; b = x; }
    else if(h < 4.0f/6.0f) { r = 0; g = x; b = c; }
    else if(h < 5.0f/6.0f) { r = x; g = 0; b = c; }
    else                    { r = c; g = 0; b = x; }

    *outR = r + m;
    *outG = g + m;
    *outB = b + m;
}

// Determine color area from vehicle position (X,Y world coords)
// GTA SA map zones: Richman = northwest hills, Rodeo = west coast,
// Hollywood = north-central, Industrial = east/south
int VehShaders_GetColorArea(float posX, float posY){
    // Richman (northwest hills — richest area)
    if(posX < -1000.0f && posY > 1000.0f)
        return COLORAREA_RICHMAN;

    // Rodeo (west coast — Beverly Hills equivalent)
    if(posX < -500.0f && posY > 0.0f && posY < 1500.0f)
        return COLORAREA_RODEO;

    // Hollywood/Vinewood (north-central — craziest colors)
    if(posX > -500.0f && posX < 500.0f && posY > 1500.0f)
        return COLORAREA_HOLLYWOOD;

    // Industrial (east/south — muted colors)
    if(posX > 1000.0f || posY < -1000.0f)
        return COLORAREA_INDUSTRIAL;

    return COLORAREA_DEFAULT;
}

// Get minimum saturation for a world position
float VehShaders_GetMinSaturation(float posX, float posY){
    return minSaturation[VehShaders_GetColorArea(posX, posY)];
}
