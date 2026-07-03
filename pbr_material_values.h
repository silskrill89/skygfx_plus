#ifndef PBR_MATERIAL_VALUES_H
#define PBR_MATERIAL_VALUES_H

// ============================================================================
// PBR Material Reference Values for Architectural/Building Surfaces
// ============================================================================
// Sources:
//   - PBR Book (Pharr, Jakob, Humphreys) - Fresnel equations, IOR
//   - Disney BRDF (Burley 2012) - artistic parameter ranges
//   - UE4/Epic Games (Karis 2013, Naty Hoffman) - measured F0 values
//   - Google Filament - material parameterization
//   - refractiveindex.info - measured IOR data
//
// Physics:
//   F0 = ((n-1)/(n+1))^2  for dielectrics (n = index of refraction)
//   Metals: F0 from measured complex IOR, wavelength-dependent (RGB)
//   Default dielectric base F0 = 0.04 (n approx 1.5)
// ============================================================================

struct PBRSurfaceMaterial {
    const char* name;
    float       roughness;         // 0.0 = mirror, 1.0 = fully rough
    float       f0R, f0G, f0B;    // Fresnel reflectance at normal incidence (linear)
    float       metalness;         // 0 = dielectric, 1 = metal
    float       clearcoat;         // 0-1, for lacquered/glossy top layer
    float       clearcoatRoughness;
    float       specularIntensity; // multiplier, 1.0 = standard
};

static const PBRSurfaceMaterial g_pbrMaterials[] = {

// ---- 1. CONCRETE (n ~1.50-1.55 => F0 ~0.04-0.046) ----
    { "Concrete_RoughCast",       0.90f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Concrete_Smooth",          0.65f,  0.042f, 0.042f, 0.042f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Concrete_Polished",        0.35f,  0.045f, 0.045f, 0.045f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 2. BRICK (n ~1.55 => F0 ~0.046) ----
    { "Brick_CommonRed",          0.85f,  0.044f, 0.044f, 0.044f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Brick_Painted",            0.70f,  0.042f, 0.042f, 0.042f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 3. GLASS (n ~1.52 soda-lime => F0 ~0.04) ----
    { "Glass_Clear",              0.05f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Glass_Tinted",             0.05f,  0.045f, 0.045f, 0.045f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Glass_Frosted",            0.60f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 4. METAL (metalness=1, F0 from Naty Hoffman/UE4 measured data) ----
//    Steel:  F0 ~0.56 (n=2.75, k=3.05)
//    Al:     F0 ~0.91 (n=1.24, k=7.0)
//    Iron:   F0 ~0.56 (n=2.77, k=2.85)
//    Chrome: F0 ~0.95 (n=3.1, k=3.3)
    { "Metal_Steel",              0.35f,  0.560f, 0.570f, 0.580f,  1.0f, 0.0f, 0.0f, 1.0f },
    { "Metal_Aluminium",          0.30f,  0.910f, 0.920f, 0.920f,  1.0f, 0.0f, 0.0f, 1.0f },
    { "Metal_Iron",               0.50f,  0.560f, 0.570f, 0.580f,  1.0f, 0.0f, 0.0f, 1.0f },
    { "Metal_Chrome",             0.10f,  0.950f, 0.950f, 0.950f,  1.0f, 0.0f, 0.0f, 1.0f },
    { "Metal_Painted",            0.40f,  0.040f, 0.040f, 0.040f,  0.0f, 0.5f, 0.1f, 1.0f },

// ---- 5. WOOD (n ~1.50-1.55 => F0 ~0.04-0.046) ----
    { "Wood_Painted",             0.50f,  0.042f, 0.042f, 0.042f,  0.0f, 0.3f, 0.1f, 1.0f },
    { "Wood_Bare",                0.75f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Wood_Aged",                0.90f,  0.038f, 0.038f, 0.038f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 6. TILE ----
//    Ceramic n ~1.5-1.6, glazed => slight clearcoat
//    Porcelain n ~1.55, smooth glazed
//    Roof tile: unglazed clay
    { "Tile_Ceramic",             0.35f,  0.045f, 0.045f, 0.045f,  0.0f, 0.2f, 0.1f, 1.0f },
    { "Tile_Porcelain",           0.20f,  0.048f, 0.048f, 0.048f,  0.0f, 0.4f, 0.1f, 1.0f },
    { "Tile_Roof",                0.80f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 7. ASPHALT (n ~1.50-1.65 => F0 ~0.04) ----
    { "Asphalt_Road",             0.95f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 8. MARBLE (n ~1.48-1.68, calcite n=1.658 => F0 ~0.04-0.06) ----
    { "Marble_Polished",          0.15f,  0.045f, 0.045f, 0.045f,  0.0f, 0.3f, 0.1f, 1.0f },
    { "Marble_Rough",             0.65f,  0.042f, 0.042f, 0.042f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 9. STUCCO / PLASTER (n ~1.50 => F0 ~0.04) ----
    { "Stucco",                   0.85f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Plaster",                  0.80f,  0.038f, 0.038f, 0.038f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 10. PAINTED SURFACES (dielectric, n ~1.5 => F0 ~0.04) ----
//     Disney BRDF clearcoat model, UE4 automotive materials
    { "Paint_Gloss",              0.20f,  0.040f, 0.040f, 0.040f,  0.0f, 0.5f, 0.05f, 1.0f },
    { "Paint_Matte",              0.80f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Paint_Automotive",         0.15f,  0.040f, 0.040f, 0.040f,  0.0f, 0.8f, 0.08f, 1.0f },

// ---- 11. STONE (granite/limestone n ~1.5-1.7 => F0 ~0.04-0.06) ----
    { "Stone_RoughCut",           0.90f,  0.038f, 0.038f, 0.038f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Stone_Smooth",             0.45f,  0.042f, 0.042f, 0.042f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 12. ROOFING ----
//     Corrugated metal: weathered steel
//     Tar/gravel: very rough dielectric
    { "Roof_CorrugatedMetal",     0.55f,  0.560f, 0.570f, 0.580f,  1.0f, 0.0f, 0.0f, 1.0f },
    { "Roof_TarGravel",           0.95f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 13. PLASTIC (n ~1.45-1.60, Disney default specular=0.5 => F0 ~0.04) ----
    { "Plastic",                  0.50f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 14. RUBBER (n ~1.50-1.59 => F0 ~0.04-0.05) ----
    { "Rubber",                   0.90f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

// ---- 15. SAND / GRAVEL (quartz n=1.54 => F0 ~0.044) ----
    { "Sand",                     0.95f,  0.044f, 0.044f, 0.044f,  0.0f, 0.0f, 0.0f, 1.0f },
    { "Gravel",                   0.95f,  0.040f, 0.040f, 0.040f,  0.0f, 0.0f, 0.0f, 1.0f },

};

static const int g_pbrMaterialCount = sizeof(g_pbrMaterials) / sizeof(g_pbrMaterials[0]);

#endif // PBR_MATERIAL_VALUES_H
