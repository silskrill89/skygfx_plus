#pragma once

// ============================================================
// Unified BRDF Library - CryEngine/Lumberyard/O3DE Style
// Diffuse/Specular/Gloss workflow (NOT Unreal metallic/roughness)
// 
// specular: 0.02-0.05 for dielectrics, 0.5-1.0 for metals
//           Can be tinted for metals (gold=yellow, copper=orange)
// glossiness: 0=rough, 1=smooth (inverse of roughness)
// ============================================================

#include <string.h>

struct BRDFMaterial {
    const char *name;
    float specular;        // Specular intensity (0.02-0.05 dielectric, 0.5-1.0 metal)
    float glossiness;      // 0=rough, 1=smooth
    float clearcoat;       // clear coat intensity (0-1)
    float subsurface;      // SSS amount (0-1)
    float specularTintR;   // Specular color tint (for metals)
    float specularTintG;
    float specularTintB;
};

// ============================================================
// Surface Type IDs - Merged from all GTA SA data files
// ============================================================

enum SurfaceType {
    // --- Generic Surfaces (from surfinfo.dat) ---
    SURFACE_DEFAULT = 0,
    SURFACE_TARMAC,
    SURFACE_TARMAC_FUCKED,
    SURFACE_TARMAC_REALLYFUCKED,
    SURFACE_PAVEMENT,
    SURFACE_PAVEMENT_FUCKED,
    SURFACE_GRAVEL,
    SURFACE_FUCKED_CONCRETE,
    SURFACE_PAINTED_GROUND,
    
    // --- Grass Types (from plants.dat + surfinfo.dat) ---
    SURFACE_GRASS_SHORT_LUSH,
    SURFACE_GRASS_MEDIUM_LUSH,
    SURFACE_GRASS_LONG_LUSH,
    SURFACE_GRASS_SHORT_DRY,
    SURFACE_GRASS_MEDIUM_DRY,
    SURFACE_GRASS_LONG_DRY,
    SURFACE_GOLFGRASS_ROUGH,
    SURFACE_GOLFGRASS_SMOOTH,
    SURFACE_STEEP_SLIDYGRASS,
    SURFACE_FLOWERBED,
    SURFACE_MEADOW,
    SURFACE_CORNFIELD,
    SURFACE_HEDGE,
    
    // --- Procedural Vegetation (from procobj.dat) ---
    SURFACE_P_GRASS_SHORT,
    SURFACE_P_GRASS_MEADOW,
    SURFACE_P_GRASS_DRY,
    SURFACE_P_GRASSDRYTALL,
    SURFACE_P_GRASSLUSHTALL,
    SURFACE_P_GRASSLIGHT,
    SURFACE_P_GRASSLIGHTER,
    SURFACE_P_GRASSLIGHTER2,
    SURFACE_P_GRASSMID1,
    SURFACE_P_GRASSMID2,
    SURFACE_P_GRASSDARK,
    SURFACE_P_GRASSDARK2,
    SURFACE_P_GRASSDIRTMIX,
    SURFACE_P_GRASSGRNMIX,
    SURFACE_P_GRASSBRNMIX,
    SURFACE_P_GRASSROCKY,
    SURFACE_P_GRASSSMALLTREES,
    SURFACE_P_GRASSLOW,
    SURFACE_P_GRASSWEEFLOWERS,
    SURFACE_P_GRASSWEEDS,
    SURFACE_P_BUSHY,
    SURFACE_P_BUSHYDRY,
    SURFACE_P_BUSHYMID,
    SURFACE_P_BUSHYMIX,
    SURFACE_P_WOODLAND,
    SURFACE_P_WOODDENSE,
    SURFACE_P_FORESTSTUMPS,
    SURFACE_P_FORESTSTICKS,
    SURFACE_P_FORRESTDRY,
    SURFACE_P_FORRESTLEAVES,
    SURFACE_P_CACTUSDENSE,
    SURFACE_P_SPARSEFLOWERS,
    SURFACE_P_FLOWERBED,
    SURFACE_P_CORNFIELD,
    SURFACE_P_ROADSIDE,
    SURFACE_P_ROADSIDEDES,
    
    // --- Sand Types (from surfinfo.dat) ---
    SURFACE_SAND_DEEP,
    SURFACE_SAND_MEDIUM,
    SURFACE_SAND_COMPACT,
    SURFACE_SAND_ARID,
    SURFACE_SAND_MORE,
    SURFACE_SAND_BEACH,
    SURFACE_CONCRETE_BEACH,
    SURFACE_P_SAND,
    SURFACE_P_SAND_DENSE,
    SURFACE_P_SAND_ARID,
    SURFACE_P_SAND_COMPACT,
    SURFACE_P_SAND_ROCKY,
    SURFACE_P_SANDBEACH,
    
    // --- Dirt/Mud Types ---
    SURFACE_DIRT,
    SURFACE_DIRTTRACK,
    SURFACE_MUD_WET,
    SURFACE_MUD_DRY,
    SURFACE_WASTEGROUND,
    SURFACE_WOODLANDGROUND,
    SURFACE_P_DIRTWEEDS,
    SURFACE_P_DIRTROCKY,
    SURFACE_P_RUBBLE,
    SURFACE_P_ALLEYRUBISH,
    SURFACE_P_DUMP,
    SURFACE_P_JUNKYARDGRND,
    SURFACE_P_JUNKYARDPILES,
    SURFACE_P_WASTEGROUND,
    SURFACE_P_SKANKYFLOOR,
    
    // --- Rock Types ---
    SURFACE_ROCK_DRY,
    SURFACE_ROCK_WET,
    SURFACE_ROCK_CLIFF,
    SURFACE_STEEP_CLIFF,
    SURFACE_P_MOUNTAIN,
    SURFACE_P_DESERTROCKS,
    
    // --- Water Types ---
    SURFACE_WATER_RIVERBED,
    SURFACE_WATER_SHALLOW,
    SURFACE_P_RIVERBED,
    SURFACE_P_RIVERBEDSHALLOW,
    SURFACE_P_RIVERBEDSTONE,
    SURFACE_P_RIVERBEDWEEDS,
    SURFACE_P_RIVEREDGE,
    SURFACE_P_UNDERWATERBARREN,
    SURFACE_P_UNDERWATERCORAL,
    SURFACE_P_UNDERWATERDEEP,
    SURFACE_P_UNDERWATERLUSH,
    SURFACE_P_SEAWEED,
    SURFACE_P_MARSH,
    
    // --- Building Materials ---
    SURFACE_P_CONCRETE,
    SURFACE_P_CONCRETCELITTER,
    SURFACE_P_BUILDINGSITE,
    SURFACE_P_INDUSTRIAL,
    SURFACE_P_INDUSTJETTY,
    SURFACE_P_DOCKLANDS,
    
    // --- Interior Floors ---
    SURFACE_FLOORCONCRETE,
    SURFACE_FLOORMETAL,
    SURFACE_FLOORBOARD,
    SURFACE_CARPET,
    SURFACE_STAIRSCARPET,
    SURFACE_STAIRSMETAL,
    SURFACE_STAIRSSTONE,
    SURFACE_STAIRSWOOD,
    SURFACE_P_711FLOOR,
    SURFACE_P_711SHELF1,
    SURFACE_P_711SHELF2,
    SURFACE_P_711SHELF3,
    SURFACE_P_BARTABLE,
    SURFACE_P_BEDROOMFLOOR,
    SURFACE_P_CORRIDORFLOOR,
    SURFACE_P_FASTFOODFLOOR,
    SURFACE_P_KIRCHENFLOOR,
    SURFACE_P_LIVINGRMFLOOR,
    SURFACE_P_OFFICEDESK,
    SURFACE_P_RESTUARANTTABLE,
    SURFACE_P_POOLSIDE,
    SURFACE_P_AIRPORTGRND,
    
    // --- Wood Types ---
    SURFACE_WOOD_SOLID,
    SURFACE_WOOD_THIN,
    SURFACE_WOOD_CRATES,
    SURFACE_WOOD_BENCH,
    SURFACE_WOOD_PICKET_FENCE,
    SURFACE_WOOD_RANCH_FENCE,
    SURFACE_WOOD_SLATTED_FENCE,
    
    // --- Metal Types ---
    SURFACE_THIN_METAL_SHEET,
    SURFACE_THICK_METAL_PLATE,
    SURFACE_METAL_BARREL,
    SURFACE_METAL_CHAIN_FENCE,
    SURFACE_METAL_DUMPSTER,
    SURFACE_METAL_GATE,
    SURFACE_GIRDER,
    SURFACE_RAILTRACK,
    SURFACE_SCAFFOLD_POLE,
    SURFACE_LAMP_POST,
    SURFACE_WHEELBASE,
    
    // --- Glass Types ---
    SURFACE_GLASS,
    SURFACE_GLASS_WINDOWS_LARGE,
    SURFACE_GLASS_WINDOWS_SMALL,
    SURFACE_UNBREAKABLE_GLASS,
    
    // --- Plastic/Rubber Types ---
    SURFACE_PLASTIC,
    SURFACE_PLASTICBARRIER,
    SURFACE_PLASTIC_CONE,
    SURFACE_PLASTIC_DUMPSTER,
    SURFACE_RUBBER,
    
    // --- Vehicle-Specific Materials ---
    SURFACE_CAR_BODY,           // Car body panels (painted metal)
    SURFACE_CAR_CHROME,         // Chrome bumpers/trim
    SURFACE_CAR_PLASTIC,        // Interior/exterior plastic trim
    SURFACE_CAR_RUBBER,         // Rubber seals/bumpers
    SURFACE_CAR_GLASS,          // Windshield/windows
    SURFACE_CAR_TIRE,           // Tire rubber
    SURFACE_CAR_WHEEL,          // Wheel alloy
    SURFACE_CAR_HEADLIGHT,      // Headlight lens
    SURFACE_CAR_TAILLIGHT,      // Taillight lens
    SURFACE_CAR_CARBON,         // Carbon fiber panels
    SURFACE_CAR_LEATHER,        // Leather seats/interior
    SURFACE_CAR_FABRIC,         // Fabric seats/convertible top
    SURFACE_CAR_DIRT,           // Dirt accumulation
    SURFACE_CAR_RUST,           // Rust patches
    SURFACE_CAR_MATTE,          // Matte paint
    SURFACE_CAR_CLEARCOAT,      // Clear coat layer
    
    // --- Misc Objects ---
    SURFACE_BIN_BAG,
    SURFACE_CARDBOARDBOX,
    SURFACE_CONTAINER,
    SURFACE_DOOR,
    SURFACE_GARAGE_DOOR,
    SURFACE_HAY_BALE,
    SURFACE_NEWS_VENDOR,
    SURFACE_TRANSPARENT_CLOTH,
    SURFACE_TRANSPARENT_STONE,
    SURFACE_GORE,
    SURFACE_CAR,
    SURFACE_CAR_PANEL,
    SURFACE_CAR_MOVINGCOMPONENT,
    SURFACE_PED,
    SURFACE_FIRE_HYDRANT,
    
    NUM_SURFACE_TYPES
};

// ============================================================
// Unified BRDF Table - CryEngine Style (Diffuse/Specular/Gloss)
// Format: {name, specular, glossiness, clearcoat, subsurface, tintR, tintG, tintB}
// ============================================================

static const BRDFMaterial g_brdfTable[NUM_SURFACE_TYPES] = {
    // --- Generic Surfaces ---
    // Dielectrics: specular=0.02-0.05, glossiness=inverse of roughness, tint=white
    { "Default",              0.04f, 0.30f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Tarmac",               0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Tarmac Fucked",        0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Tarmac Really Fucked", 0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Pavement",             0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Pavement Fucked",      0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Gravel",               0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Fucked Concrete",      0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Painted Ground",       0.04f, 0.40f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Grass Types (dielectric, low specular, medium glossiness, some SSS) ---
    { "Grass Short Lush",     0.04f, 0.15f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "Grass Medium Lush",    0.04f, 0.12f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "Grass Long Lush",      0.04f, 0.10f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "Grass Short Dry",      0.04f, 0.12f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "Grass Medium Dry",     0.04f, 0.10f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "Grass Long Dry",       0.04f, 0.08f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "Golf Grass Rough",     0.04f, 0.15f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "Golf Grass Smooth",    0.04f, 0.18f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "Steep Slidy Grass",    0.04f, 0.12f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "Flowerbed",            0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "Meadow",               0.04f, 0.12f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "Cornfield",            0.04f, 0.10f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "Hedge",                0.04f, 0.12f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    
    // --- Procedural Vegetation ---
    { "P Grass Short",        0.04f, 0.15f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Meadow",       0.04f, 0.12f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Grass Dry",          0.04f, 0.10f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Grass Dry Tall",     0.04f, 0.08f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Grass Lush Tall",    0.04f, 0.12f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "P Grass Light",        0.04f, 0.15f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Lighter",      0.04f, 0.17f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Lighter 2",    0.04f, 0.18f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Mid 1",        0.04f, 0.13f, 0.0f, 0.28f, 1.0f, 1.0f, 1.0f },
    { "P Grass Mid 2",        0.04f, 0.12f, 0.0f, 0.28f, 1.0f, 1.0f, 1.0f },
    { "P Grass Dark",         0.04f, 0.10f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Grass Dark 2",       0.04f, 0.09f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Grass Dirt Mix",     0.04f, 0.08f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Grass Grn Mix",      0.04f, 0.12f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Brn Mix",      0.04f, 0.10f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Grass Rocky",        0.04f, 0.08f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Grass Small Trees",  0.04f, 0.12f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Grass Low",          0.04f, 0.14f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Grass Wee Flowers",  0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Grass Weeds",        0.04f, 0.10f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Bushy",              0.04f, 0.12f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "P Bushy Dry",          0.04f, 0.08f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Bushy Mid",          0.04f, 0.10f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Bushy Mix",          0.04f, 0.11f, 0.0f, 0.32f, 1.0f, 1.0f, 1.0f },
    { "P Woodland",           0.04f, 0.10f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Wood Dense",         0.04f, 0.08f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Forest Stumps",      0.04f, 0.15f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Forest Sticks",      0.04f, 0.12f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Forest Dry",         0.04f, 0.10f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Forest Leaves",      0.04f, 0.12f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Cactus Dense",       0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Sparse Flowers",     0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Flowerbed",          0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P Cornfield",          0.04f, 0.10f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "P Roadside",           0.04f, 0.12f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Roadside Des",       0.04f, 0.10f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    
    // --- Sand Types (dielectric, very low specular, low glossiness) ---
    { "Sand Deep",            0.04f, 0.05f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Sand Medium",          0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Sand Compact",         0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Sand Arid",            0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Sand More",            0.04f, 0.07f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Sand Beach",           0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Concrete Beach",       0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand",               0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand Dense",         0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand Arid",          0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand Compact",       0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand Rocky",         0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Sand Beach",         0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Dirt/Mud Types ---
    { "Dirt",                 0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Dirt Track",           0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Mud Wet",              0.04f, 0.40f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Mud Dry",              0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wasteground",          0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Woodland Ground",      0.04f, 0.12f, 0.0f, 0.10f, 1.0f, 1.0f, 1.0f },
    { "P Dirt Weeds",         0.04f, 0.10f, 0.0f, 0.15f, 1.0f, 1.0f, 1.0f },
    { "P Dirt Rocky",         0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Rubble",             0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Alley Rubbish",      0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Dump",               0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Junkyard Grnd",      0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Junkyard Piles",     0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Wasteground",        0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Skanky Floor",       0.04f, 0.08f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Rock Types ---
    { "Rock Dry",             0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Rock Wet",             0.04f, 0.40f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Rock Cliff",           0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Steep Cliff",          0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Mountain",           0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Desert Rocks",       0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Water Types (very low specular, high glossiness) ---
    { "Water Riverbed",       0.02f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Water Shallow",        0.02f, 0.98f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Riverbed",           0.04f, 0.30f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Riverbed Shallow",   0.04f, 0.35f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Riverbed Stone",     0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Riverbed Weeds",     0.04f, 0.15f, 0.0f, 0.30f, 1.0f, 1.0f, 1.0f },
    { "P River Edge",         0.04f, 0.20f, 0.0f, 0.10f, 1.0f, 1.0f, 1.0f },
    { "P Underwater Barren",  0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Underwater Coral",   0.04f, 0.20f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "P Underwater Deep",    0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Underwater Lush",    0.04f, 0.18f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f },
    { "P Seaweed",            0.04f, 0.15f, 0.0f, 0.35f, 1.0f, 1.0f, 1.0f },
    { "P Marsh",              0.04f, 0.12f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    
    // --- Building Materials ---
    { "P Concrete",           0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Concrete Litter",    0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Building Site",      0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Industrial",         0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Indust Jetty",       0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Docklands",          0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Interior Floors ---
    { "Floor Concrete",       0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Floor Metal",          0.60f, 0.60f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f },  // Metal: high specular, tinted
    { "Floorboard",           0.04f, 0.25f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Carpet",               0.04f, 0.05f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Stairs Carpet",        0.04f, 0.05f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Stairs Metal",         0.60f, 0.55f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f },
    { "Stairs Stone",         0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Stairs Wood",          0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P 711 Floor",          0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P 711 Shelf 1",        0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P 711 Shelf 2",        0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P 711 Shelf 3",        0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Bar Table",          0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Bedroom Floor",      0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Corridor Floor",     0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Fast Food Floor",    0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Kitchen Floor",      0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Living Room Floor",  0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Office Desk",        0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Restaurant Table",   0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Poolside",           0.04f, 0.40f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "P Airport Grnd",       0.04f, 0.20f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Wood Types (dielectric, low specular, medium glossiness, clearcoat) ---
    { "Wood Solid",           0.04f, 0.25f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Thin",            0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Crates",          0.04f, 0.20f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Bench",           0.04f, 0.28f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Picket Fence",    0.04f, 0.25f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Ranch Fence",     0.04f, 0.22f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Wood Slatted Fence",   0.04f, 0.27f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Metal Types (high specular, medium-high glossiness, grey tint) ---
    { "Thin Metal Sheet",     0.70f, 0.65f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Thick Metal Plate",    0.65f, 0.60f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Metal Barrel",         0.60f, 0.55f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Metal Chain Fence",    0.55f, 0.50f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Metal Dumpster",       0.50f, 0.45f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Metal Gate",           0.60f, 0.55f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Girder",               0.55f, 0.50f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Rail Track",           0.65f, 0.60f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Scaffold Pole",        0.60f, 0.55f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Lamp Post",            0.55f, 0.50f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Wheelbase",            0.60f, 0.55f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    
    // --- Glass Types (very low specular, very high glossiness) ---
    { "Glass",                0.04f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Glass Windows Large",  0.04f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Glass Windows Small",  0.04f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Unbreakable Glass",    0.04f, 0.92f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Plastic/Rubber Types ---
    { "Plastic",              0.04f, 0.60f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Plastic Barrier",      0.04f, 0.55f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Plastic Cone",         0.04f, 0.50f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Plastic Dumpster",     0.04f, 0.45f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Rubber",               0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Vehicle-Specific Materials ---
    { "Car Body",             0.04f, 0.85f, 0.9f, 0.0f, 1.0f, 1.0f, 1.0f },  // Clearcoat paint
    { "Car Chrome",           0.56f, 0.90f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },  // Chrome: F0=0.56 (real chrome), neutral reflections
    { "Car Plastic",          0.04f, 0.55f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Rubber",           0.04f, 0.12f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Glass",            0.04f, 0.95f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Tire",             0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Wheel",            0.70f, 0.75f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f },  // Alloy wheel
    { "Car Headlight",        0.04f, 0.90f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Taillight",        0.04f, 0.88f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Carbon",           0.04f, 0.75f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Leather",          0.04f, 0.35f, 0.0f, 0.1f, 1.0f, 1.0f, 1.0f },
    { "Car Fabric",           0.04f, 0.10f, 0.0f, 0.3f, 1.0f, 1.0f, 1.0f },
    { "Car Dirt",             0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Rust",             0.30f, 0.25f, 0.0f, 0.0f, 0.6f, 0.4f, 0.2f },  // Rust: orange tint
    { "Car Matte",            0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Clearcoat",        0.04f, 0.92f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    
    // --- Misc Objects ---
    { "Bin Bag",              0.04f, 0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Cardboard Box",        0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Container",            0.50f, 0.50f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
    { "Door",                 0.04f, 0.30f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Garage Door",          0.04f, 0.25f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Hay Bale",             0.04f, 0.08f, 0.0f, 0.20f, 1.0f, 1.0f, 1.0f },
    { "News Vendor",          0.04f, 0.40f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Transparent Cloth",    0.04f, 0.10f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Transparent Stone",    0.04f, 0.25f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Gore",                 0.04f, 0.50f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f },
    { "Car",                  0.04f, 0.85f, 0.9f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Panel",            0.04f, 0.82f, 0.9f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Car Moving Component", 0.04f, 0.80f, 0.9f, 0.0f, 1.0f, 1.0f, 1.0f },
    { "Ped",                  0.04f, 0.40f, 0.0f, 0.8f, 1.0f, 1.0f, 1.0f },
    { "Fire Hydrant",         0.55f, 0.50f, 0.0f, 0.0f, 0.7f, 0.7f, 0.7f },
};

// ============================================================
// Helper Functions
// ============================================================

static inline const BRDFMaterial* GetBRDF(int surfaceType) {
    if(surfaceType < 0 || surfaceType >= NUM_SURFACE_TYPES)
        return &g_brdfTable[SURFACE_DEFAULT];
    return &g_brdfTable[surfaceType];
}

// Pack BRDF into float4 for shader constant upload
// c0 = {specular, glossiness, clearcoat, subsurface}
// c1 = {specularTintR, specularTintG, specularTintB, 0}
static inline void PackBRDFConstants(const BRDFMaterial *brdf, float *out0, float *out1) {
    out0[0] = brdf->specular;
    out0[1] = brdf->glossiness;
    out0[2] = brdf->clearcoat;
    out0[3] = brdf->subsurface;
    out1[0] = brdf->specularTintR;
    out1[1] = brdf->specularTintG;
    out1[2] = brdf->specularTintB;
    out1[3] = 0.0f;
}

// Case-insensitive substring search
static inline const char* strstri(const char *str, const char *sub) {
    if(!str || !sub) return NULL;
    size_t len = strlen(sub);
    if(len == 0) return str;
    while(*str) {
        if(_strnicmp(str, sub, len) == 0)
            return str;
        str++;
    }
    return NULL;
}

// ============================================================
// Surface Type Detection from Material/Texture
// ============================================================

static inline int GetSurfaceTypeFromMaterial(void *material) {
    // RpMaterial structure from RW SDK
    struct RpMaterial {
        void *texture;
        struct { float ambient, diffuse, specular; } surfaceProps;
        unsigned int color;
        void *pipeline;
    };
    
    struct RwTexture {
        void *raster;
        char name[32];
        char mask[32];
    };
    
    RpMaterial *mat = (RpMaterial*)material;
    if(!mat) return SURFACE_DEFAULT;
    
    RwTexture *tex = (RwTexture*)mat->texture;
    if(!tex || !tex->name[0]) return SURFACE_DEFAULT;
    
    const char *name = tex->name;
    
    // --- Vehicle-Specific Materials ---
    if(strstri(name, "chrome") || strstri(name, "bumper_chrome"))
        return SURFACE_CAR_CHROME;
    if(strstri(name, "tire") || strstri(name, "tyre") || strstri(name, "wheel_rubber"))
        return SURFACE_CAR_TIRE;
    if(strstri(name, "wheel") || strstri(name, "alloy") || strstri(name, "rim"))
        return SURFACE_CAR_WHEEL;
    if(strstri(name, "headlight") || strstri(name, "light_front"))
        return SURFACE_CAR_HEADLIGHT;
    if(strstri(name, "taillight") || strstri(name, "light_rear"))
        return SURFACE_CAR_TAILLIGHT;
    if(strstri(name, "carbon"))
        return SURFACE_CAR_CARBON;
    if(strstri(name, "leather"))
        return SURFACE_CAR_LEATHER;
    if(strstri(name, "fabric") || strstri(name, "seat"))
        return SURFACE_CAR_FABRIC;
    if(strstri(name, "windscreen") || strstri(name, "window") || strstri(name, "glass"))
        return SURFACE_CAR_GLASS;
    if(strstri(name, "trim") || strstri(name, "plastic_interior"))
        return SURFACE_CAR_PLASTIC;
    if(strstri(name, "rubber_seal") || strstri(name, "seal"))
        return SURFACE_CAR_RUBBER;
    if(strstri(name, "dirt") || strstri(name, "grime"))
        return SURFACE_CAR_DIRT;
    if(strstri(name, "rust"))
        return SURFACE_CAR_RUST;
    
    // --- Generic Materials ---
    if(strstri(name, "chrome") || strstri(name, "polished"))
        return SURFACE_THICK_METAL_PLATE;  // Use thick metal for chrome
    if(strstri(name, "metal") || strstri(name, "iron") || strstri(name, "steel"))
        return SURFACE_THIN_METAL_SHEET;
    if(strstri(name, "rubber"))
        return SURFACE_RUBBER;
    if(strstri(name, "plastic") || strstri(name, "vinyl"))
        return SURFACE_PLASTIC;
    if(strstri(name, "wood") || strstri(name, "timber") || strstri(name, "plank"))
        return SURFACE_WOOD_SOLID;
    if(strstri(name, "brick"))
        return SURFACE_P_CONCRETE;  // Brick uses concrete-like properties
    if(strstri(name, "stone") || strstri(name, "marble"))
        return SURFACE_ROCK_DRY;
    if(strstri(name, "concrete") || strstri(name, "cement"))
        return SURFACE_P_CONCRETE;
    if(strstri(name, "grass") || strstri(name, "lawn"))
        return SURFACE_GRASS_SHORT_LUSH;
    if(strstri(name, "leaf") || strstri(name, "plant") || strstri(name, "bush"))
        return SURFACE_P_BUSHY;
    if(strstri(name, "dirt") || strstri(name, "mud") || strstri(name, "soil"))
        return SURFACE_DIRT;
    if(strstri(name, "sand"))
        return SURFACE_SAND_MEDIUM;
    if(strstri(name, "road") || strstri(name, "asphalt") || strstri(name, "tarmac"))
        return SURFACE_TARMAC;
    if(strstri(name, "gravel"))
        return SURFACE_GRAVEL;
    if(strstri(name, "fabric") || strstri(name, "cloth") || strstri(name, "canvas"))
        return SURFACE_TRANSPARENT_CLOTH;
    if(strstri(name, "carpet"))
        return SURFACE_CARPET;
    if(strstri(name, "tile") || strstri(name, "ceramic"))
        return SURFACE_FLOORCONCRETE;  // Tile uses floor-like properties
    if(strstri(name, "paint"))
        return SURFACE_PAINTED_GROUND;
    
    // --- Fallback ---
    return SURFACE_DEFAULT;
}
