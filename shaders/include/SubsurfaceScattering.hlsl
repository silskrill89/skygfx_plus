// SubsurfaceScattering.hlsl - Material-based SSS approximation
// Based on GPU Gems 3 / Crysis vegetation shading (Tiago Sousa, Crytek)
// Material types: MAT_NONE, MAT_SKIN, MAT_CLOTH, MAT_VEGETATION
// Trunks are MAT_NONE (standard Lambert), leaves/cloth/skin get SSS

#ifndef SSS_INCLUDED
#define SSS_INCLUDED

// Material type IDs (passed via shaderParams.y)
#define MAT_NONE        0
#define MAT_SKIN        1
#define MAT_CLOTH       2
#define MAT_VEGETATION  3

// --- Material Definitions ---
// Each material defines: scatter color, wrap amount, view dependency, intensity scale

// Skin: warm red scatter through thin dermis layer
// Wrap: moderate (skin is not double-sided but scatters broadly)
// View-dependent: strong (SSS visible at grazing angles)
static const float3 SSS_SKIN_COLOR    = float3(1.0, 0.15, 0.05);  // warm red-orange
static const float  SSS_SKIN_WRAP     = 0.6;   // N·L wrap amount
static const float  SSS_SKIN_VIEWDEP  = 0.4;   // view dependency blend (0=fully N·L, 1=fully E·L)
static const float  SSS_SKIN_SCALE    = 0.35;  // overall intensity

// Cloth: cool fiber scatter through fabric weave
// Wrap: large (fabric is thin, light wraps around fibers)
// View-dependent: moderate
static const float3 SSS_CLOTH_COLOR   = float3(0.3, 0.4, 0.8);  // cool blue-purple
static const float  SSS_CLOTH_WRAP    = 0.8;
static const float  SSS_CLOTH_VIEWDEP = 0.3;
static const float  SSS_CLOTH_SCALE   = 0.25;

// Vegetation: green-yellow scatter through leaf membrane
// Crysis technique: back-lighting with artist-controlled view dependency
// The back side of leaves transmits light with green tint
static const float3 SSS_VEG_COLOR     = float3(0.15, 0.75, 0.05); // green-yellow
static const float  SSS_VEG_WRAP      = 0.6;   // Crysis: "saturate(dot(-N, L) * 0.6 + 0.4)"
static const float  SSS_VEG_VIEWDEP   = 0.5;   // Crysis default for back-view blend
static const float  SSS_VEG_SCALE     = 0.5;   // vegetation SSS is prominent

// --- SSS Term Calculation (Crysis GPU Gems 3, Listing 16-4) ---

// Back diffuse: light hitting the back face of a thin surface
// fLdotNBack = saturate(dot(-N, L) * wrapScale + (1.0 - wrapScale))
float ComputeBackDiffuse(float3 normal, float3 lightDir, float wrapScale)
{
    return saturate(dot(-normal, lightDir) * wrapScale + (1.0 - wrapScale));
}

// View-dependent term: E·L raised to power, simulates Fresnel-like SSS
// At grazing angles, more light scatters through the surface
float ComputeViewDepTerm(float3 eyeDir, float3 lightDir, float power)
{
    float edotl = saturate(dot(eyeDir, -lightDir));
    float p = edotl * edotl;    // square
    p = p * p;                   // fourth power
    return lerp(p, 1.0, power); // blend toward 1.0 as power increases
}

// --- Per-Material SSS Evaluation ---

// Returns SSS contribution (added to standard diffuse)
// diffuseColor: base texture color
// normal: surface normal (world space)
// lightDir: direction TO light (world space)
// eyeDir: direction TO camera (world space)
// materialType: MAT_NONE/SKIN/CLOTH/VEGETATION
// sssIntensity: global intensity multiplier (0..1)
// vertexAlpha: from vertex color (used for vegetation stiffness, skin thickness)
float3 ComputeSSS(float3 diffuseColor, float3 normal, float3 lightDir, float3 eyeDir,
                   int materialType, float sssIntensity, float vertexAlpha)
{
    if(sssIntensity <= 0.0 || materialType == MAT_NONE)
        return float3(0, 0, 0);

    float3 sssColor;
    float wrapScale;
    float viewDep;
    float baseScale;

    if(materialType == MAT_SKIN)
    {
        sssColor   = SSS_SKIN_COLOR;
        wrapScale  = SSS_SKIN_WRAP;
        viewDep    = SSS_SKIN_VIEWDEP;
        baseScale  = SSS_SKIN_SCALE;
    }
    else if(materialType == MAT_CLOTH)
    {
        sssColor   = SSS_CLOTH_COLOR;
        wrapScale  = SSS_CLOTH_WRAP;
        viewDep    = SSS_CLOTH_VIEWDEP;
        baseScale  = SSS_CLOTH_SCALE;
    }
    else if(materialType == MAT_VEGETATION)
    {
        sssColor   = SSS_VEG_COLOR;
        wrapScale  = SSS_VEG_WRAP;
        viewDep    = SSS_VEG_VIEWDEP;
        baseScale  = SSS_VEG_SCALE;
    }
    else
    {
        return float3(0, 0, 0);
    }

    // Back diffuse from Crysis Listing 16-4
    float backDiffuse = ComputeBackDiffuse(normal, lightDir, wrapScale);

    // View dependency from Crysis
    float viewTerm = ComputeViewDepTerm(eyeDir, lightDir, viewDep);

    // Combine: blend between view-dependent and N·L back-lighting
    float sssTerm = lerp(viewTerm, backDiffuse, 1.0 - viewDep);

    // Vertex alpha modulation:
    //   Vegetation: leaf stiffness (high alpha = stiff leaf = more SSS at edges)
    //   Skin: thickness variation (high alpha = thin skin = more SSS)
    //   Cloth: weave density
    float vertMask = saturate(vertexAlpha);
    sssTerm *= vertMask;

    // Modulate by diffuse color (light filtering through the surface)
    float3 result = sssColor * diffuseColor * sssTerm * baseScale * sssIntensity;

    return max(result, float3(0, 0, 0));
}

// --- Simplified SSS for PS_2_0 (no dynamic branching) ---
// Same math but avoids the int comparison by using step()
float3 ComputeSSS_Simple(float3 diffuseColor, float3 normal, float3 lightDir, float3 eyeDir,
                          float materialType_f, float sssIntensity, float vertexAlpha)
{
    if(sssIntensity <= 0.0 || materialType_f < 0.5)
        return float3(0, 0, 0);

    // Select parameters using step() to avoid branches on ps_2_0
    float isSkin     = step(0.5, materialType_f) * (1.0 - step(1.5, materialType_f));  // matType == 1
    float isCloth    = step(1.5, materialType_f) * (1.0 - step(2.5, materialType_f));  // matType == 2
    float isVeg      = step(2.5, materialType_f);                                        // matType >= 3

    float3 sssColor = SSS_SKIN_COLOR * isSkin + SSS_CLOTH_COLOR * isCloth + SSS_VEG_COLOR * isVeg;
    float wrapScale  = SSS_SKIN_WRAP * isSkin + SSS_CLOTH_WRAP * isCloth + SSS_VEG_WRAP * isVeg;
    float viewDep    = SSS_SKIN_VIEWDEP * isSkin + SSS_CLOTH_VIEWDEP * isCloth + SSS_VEG_VIEWDEP * isVeg;
    float baseScale  = SSS_SKIN_SCALE * isSkin + SSS_CLOTH_SCALE * isCloth + SSS_VEG_SCALE * isVeg;

    float backDiffuse = saturate(dot(-normal, lightDir) * wrapScale + (1.0 - wrapScale));
    float edotl = saturate(dot(eyeDir, -lightDir));
    float viewTerm = edotl * edotl;
    viewTerm = viewTerm * viewTerm;
    viewTerm = lerp(viewTerm, 1.0, viewDep);

    float sssTerm = lerp(viewTerm, backDiffuse, 1.0 - viewDep);
    sssTerm *= saturate(vertexAlpha);

    return max(sssColor * diffuseColor * sssTerm * baseScale * sssIntensity, float3(0, 0, 0));
}

// --- Vegetation-specific: Crysis detail bending + SSS ---
// For vegetation VS: computes wind-driven vertex displacement
// Uses vertex color channels per Crysis convention:
//   R = leaf edge stiffness
//   G = per-leaf phase variation
//   B = leaf overall stiffness
//   A = precomputed ambient occlusion (also used for SSS intensity)

// Smooth triangle wave from Crysis Listing 16-1
float4 SmoothCurve(float4 x) { return x * x * (3.0 - 2.0 * x); }
float4 TriangleWave(float4 x) { return abs(frac(x + 0.5) * 2.0 - 1.0); }
float4 SmoothTriangleWave(float4 x) { return SmoothCurve(TriangleWave(x)); }

// Crysis detail bending (Listing 16-2), returns displaced position
float3 VegetationBend(float3 worldPos, float3 vertexNormal, float4 vertexColor,
                       float time, float windStrength, float3 windDir)
{
    float edgeStiff   = vertexColor.r;   // R: edge stiffness
    float leafPhase   = vertexColor.g;   // G: per-leaf phase
    float branchStiff = vertexColor.b;   // B: branch stiffness
    float ao          = vertexColor.a;   // A: ambient occlusion

    // Object-space phase
    float objPhase = dot(worldPos.xyz, 1.0);
    float branchPhase = objPhase;

    // Vertex phase for detail bending
    float vtxPhase = dot(worldPos.xyz, leafPhase + branchPhase);

    // Triangle waves for edge and branch bending
    float2 wavesIn = time + float2(vtxPhase, branchPhase);
    float4 waves = (frac(wavesIn.xxyy * float4(1.975, 0.793, 0.375, 0.193)) * 2.0 - 1.0)
                   * windStrength * 2.0;
    waves = SmoothTriangleWave(waves);
    float2 wavesSum = waves.xz + waves.yw;

    float3 displaced = worldPos;
    // Edge bending along normal xy
    displaced.xyz += wavesSum.xxy * float3(edgeStiff * vertexNormal.xy, 0) * 0.1;
    // Branch bending along z
    displaced.z += wavesSum.y * branchStiff * 0.05;

    return displaced;
}

#endif
