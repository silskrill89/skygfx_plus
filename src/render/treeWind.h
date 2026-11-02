#pragma once
// =============================================================================
// treeWind.h — tree wind sway extension + FF tree lighting/alpha (lane W8-2)
//
// Two independent features live here:
//
//  (1) WIND SWAY. skygfx already has a per-vertex tree sway for the PS2 and
//      Xbox building callbacks (ps2BuildingWindVS / xboxBuildingWindVS, bound
//      by the PS2-cb wind branch, the Xbox-cb wind branch and the PBR-cb
//      buildingPBRVS==NULL fallback in buildingPipe.cpp). The PBR and GTAIV
//      building paths bind buildingPBRVS / the IV VS pair, which contain NO
//      sway math, so an atomic flagged DefinedVertexShader::WIND silently
//      stopped swaying the moment the user switched buildingPipe to PBR/
//      GTAIV (or ivMode). Two complementary fixes, both owned by this lane:
//        a) TreeWind_ApplyToTransform — CPU-side object-space tilt applied to
//           the composed WVP right after pipeGetComposedTransformMatrix.
//           Used by the PBR cb and by the PS2 cb's IV shader swap (the two
//           branches that have no wind VS). Nothing else is touched: no render
//           state, no persistent matrix mutation, no per-frame restore needed.
//        b) TreeWind_OnEntity — the game-side (FF/console) entry point, driven
//           from a CEntity::ModifyMatrixForTreeInWind hook. Port of the stock
//           routine (SA 0x534E90, gta-reversed Entity.cpp:1348, in-project
//           port of the same math at PC_PlantsMgr.cpp:333) so the matrix-level
//           sway stays ours to extend (wind-table gust, palm lean, FF tuning).
//           Hook INSTALL is deferred to main.cpp (not this lane's file) — see
//           .serena/agent-reports/w78-tree-wind-alpha.md, DEFERRED DIFFS.
//
//      Ownership rule that prevents double sway (applies to BOTH halves):
//          DefinedVertexShader::WIND atomics are skygfx's — the VS path (PS2/
//          Xbox) or the render-time tilt (PBR/GTAIV) sways them.
//          everything else is the game's — TreeWind_OnEntity sways it.
//      In no combination do the two stack.
//
//  (2) TREE LIGHTING/ALPHA. Port of FusionFix's OverrideTreeAlpha +
//      PREF_TREEALPHA / PREF_TREE_LIGHTING profiles (shaders.ixx:489-521 and
//      :548-567; the shader side of the alpha multiplier is gta_treesPS1.asm
//      `mul_sat r0.w, r0.w, c221_abs.w` + threshold). Implemented as:
//        - TreeAlpha_ApplyAlphaRef / _RestoreAlphaRef: scales the FFP alpha
//          test ref by the profile's alphamul so the cutout threshold moves
//          exactly like FF's — works in EVERY building pipe (PBR/IV included).
//        - TreeAlpha_Upload + treeAlphaPS (shaders/ps/TreeAlpha.hlsl): the PS
//          side gate + lighting profile, bound on the simple-PS paths (PS2,
//          wind branch, Xbox). Handle is loaded by a DEFERRED CreateShaders
//          line; it stays NULL until then and every helper no-ops.
//
// =============================================================================
// REGISTER CONTRACT (authoritative — mirror of the block in treeWind.cpp)
//
//   VERTEX shader
//     c21  windPos          float2   (existing) global wind phase + object pos
//     c22  windIntensity    float    (existing) 2.0 + 1.5 * CWeather::Wind
//     c23  windExt          float4   (NEW, this lane)
//            .x  heightScale   1/treeHeight: 0 at the model origin (base),
//                              1 at the canopy top (sway anchored at the
//                              trunk instead of sliding the whole mesh)
//            .y  gustPhase     radians, slow global gust (windPos-derived)
//            .z  gustAmp       gust amplitude in the same units as `dist`
//            .w  enable        0 => height mask disabled (legacy behaviour)
//                              so a stale/never-uploaded c23 can never kill
//                              sway on a path that forgot setWindParams
//     c24-c27 worldMat (PBR VS) / free elsewhere — NOT used by this lane.
//     c29 shaderParams, c30/c31 dayparam/nightparam, c32-c35 texmat,
//     c36 fxParams(PS2-Xbox)/eyePos(PBR), c37 envXform, c38-c40 envmat
//          — all unchanged.
//
//   PIXEL shader (TreeAlpha only)
//     c0   colorScale.x     (existing PS2/Xbox convention — applied to rgb)
//     c1   treeAlpha        float4   (NEW, this lane; overrides the detail
//                                     `tile` value that buildingPipe_selectPS
//                                     may have pushed a line earlier)
//            .x  alphamul    FF OverrideTreeAlpha / TreeFxText multiplier
//            .y  alphaRef    ORIGINAL FFP ref as 0..1 (the PS clip gate must
//                            use the unscaled ref: the CPU scales ALPHAREF,
//                            the PS scales the alpha — same threshold)
//            .z  profile     0=PC, 1=PC+, 2=Console
//            .w  enable      0 => pass-through (no clip, no profile)
//     c24  ambient          RwRGBAReal buildingAmbient (lighting profiles)
//     s0   diffuse, s1/s2 untouched (TreeAlpha does not sample them)
//
//   PS c22/c23 (pbrParams / paintNoise) are a DIFFERENT constant space from
//   VS c22/c23 and are untouched — pipeUploadPBR stays the only writer.
// =============================================================================
#include "skygfx.h"

class CEntity;

// ---- wind -----------------------------------------------------------------
// REQUIRED public API: the CEntity::ModifyMatrixForTreeInWind hook body calls
// this with the entity's rw atomic (install line is deferred to main.cpp).
extern void TreeWind_OnEntity(RpAtomic *atomic);

// thiscall-compatible hook body for 0x534E90 (ECX = self — see the header
// comment above for why it REPLACES the game routine rather than wrapping it).
extern void __fastcall TreeWind_ModifyMatrixForTreeInWind(CEntity *self);

// CBaseModelInfo::m_flags & 0x1800 (TREE 0x0800 | PALM 0x1000) == the game's
// SwaysInWind() predicate (0x4212C0). NULL-safe, false for non-tree atomics.
extern bool TreeWind_IsTreeAtomic(RpAtomic *atomic);

// Post-multiplies the composed WVP by an object-space tilt. transform16 is the
// row-major block pipeGetComposedTransformMatrix just produced (D3D layout:
// translation at [3]/[7]/[11]). Idempotent per call (no accumulation), no
// render state, no LTM mutation — safe inside a render callback.
extern void TreeWind_ApplyToTransform(RpAtomic *atomic, float *transform16);

// Uploads VS c23 (windExt). Called from buildingPipe setWindParams, i.e.
// immediately before any wind VS bind.
extern void TreeWind_UploadWindExt(void);

// ---- tree alpha -----------------------------------------------------------
extern void *treeAlphaPS;            // shaders/ps/TreeAlpha.cso — NULL until the
                                     // deferred CreateShaders line lands
extern float TreeAlpha_alphamul;     // FF alphamul (PC 0.625 / PC+ 1.0 / Console 4.0)
extern int   TreeAlpha_profile;      // 0=PC 1=PC+ 2=Console
extern bool  TreeAlpha_enable;       // master switch (defaults ON, inert by itself)

// Profile setter for the (deferred) INI/menu wiring: fx = 0 Off, 1 PC,
// 2 PC+, 3 Console — maps to {profile, alphamul} with FusionFix's TreeFxText
// values. Defaults/Off = {0, 1.0} = exact no-op (nothing bound, ref moved
// by nothing). Defined in treeWind.cpp; main.cpp's staged diff calls it.
extern void  TreeAlpha_SetProfile(int fx);

// True only when the PS exists AND the atomic really is a tree AND the feature
// is enabled AND the profile actually needs the PS (Console / profile 2 —
// PC and PC+ differ only by alphamul, which the CPU-side ApplyAlphaRef
// implements in every pipe without binding anything). The single gate every
// bind/constant upload goes through.
extern bool  TreeAlpha_Active(RpAtomic *atomic);

// Alpha-test path (main.cpp grassRenderCallback convention: snapshot func/ref,
// sanitise, restore). Apply once per callback AFTER buildingPipe_saveRenderState,
// Restore once before buildingPipe_restoreRenderState. Both push L1 (rw cache)
// + L2 (driver cache) and, only when a push actually happened, L3 (device) —
// symmetric push/restore so an empty callback (no draw between the two) can
// never leave a stale raw ref on the device.
extern void  TreeAlpha_ApplyAlphaRef(RpAtomic *atomic, int baseRef);
extern void  TreeAlpha_RestoreAlphaRef(void);

// PS c1 + c24 upload — call right before RwD3D9SetPixelShader(treeAlphaPS).
extern void  TreeAlpha_Upload(void);
