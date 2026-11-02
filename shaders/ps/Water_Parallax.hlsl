// Water_Parallax.hlsl - Water surface shading (ps_3_0)
//
// THREE STYLE ENTRY POINTS (compiled via tools/fast_build.py multi_entry,
// one CSO per style — selecting waterStyle in the INI switches the shader
// entry + constant preset in waterPipe.cpp, NOT a separate draw path):
//   main_xbox -> Water_Parallax.cso   (Xbox/PS2-style core — the rewritten base)
//   main_iv   -> Water_IV.cso         (GTA IV look, water-refs.md IV recipe)
//   main_v    -> Water_V.cso          (GTA V / RAGE look, water-refs.md V recipe)
// One shared VS (Water_VS.hlsl) for all styles.
//
// SHARED CORE (all styles, the rewritten base):
//  * Flat geometry (no vertex displacement; wave motion = scrolling normal
//    detail sampled from a real mip-mapped detail texture — kills the
//    sub-pixel sparkle of the old procedural hash noise at distance).
//  * Distance-based blend to a calm/smooth far look (normal strength,
//    specular power and refraction offset all fade with view distance).
//  * Fresnel-weighted reflection: sky(timecycle) tint, reflectivity < 1 —
//    NEVER a chrome mirror (V damps its reflection normal, IV caps fresnel).
//  * Depth-based shallow->deep colour + translucency + soft shore blending
//    from the scene depth copy (thickness = sceneZ - waterZ).
//
// TIMECYCLE LIGHTING (defect "night water glows white"):
//  the body colour is lit by the timecycle ambient/directional (c12/c13) like
//  every other lighting model in the project, instead of a hardcoded
//  brightness ramp. Daylight frames reproduce the authored timecyc water
//  colour ~1:1; night frames collapse to ambient + a sky-luminance share, and
//  every white additive term (sun spec, foam, sun pierce, underwater shimmer)
//  is gated by a day factor derived from the timecycle sky luminance — ambient
//  is pinned near 0 and directional is 255 in EVERY slot of this install's
//  timecycle, so neither can tell day from night, the sky can (same signal
//  pipelinecommon's night ceiling uses).
//
// VANILLA COLOUR + GENTLE FAR BLEND (defect "turquoise + dark horizon band"):
//  the body colour comes straight from the timecycle WaterRGBA (C++ side
//  applies SA's own x*0.65+0.27 quirk) and converges over the far half of the
//  distance ramp toward deep colour + timecycle horizon colour — a ramp, not
//  the old hard shallow->deep step. The far weight is capped at 0.45 (was
//  0.85, which flattened the far field into one band); distant reflections
//  ride the horizon colour instead of collapsing to flat grey (the murky band).
//
// FRESNEL SPLIT (defects "one-sided water" + "day transparency not great
// enough at steep angles"): the composite is
//   result = F * envReflect + (1-F) * [ bodyT * waterBody + (1-bodyT) * scene ]
// instead of the old lerp(scene, surf, bodyOpacity) — which multiplied the
// reflection lobe by a DEPTH-driven opacity and so could never show both at
// once (shallow water: scene through everywhere incl. grazing; deep water:
// opaque at every angle incl. a noon view of the bottom). F is each style's
// fresnel (floored with Schlick where a style curve is face-on-biased, capped
// < 1 so it can never go chrome; from below it is the TIR curve); bodyT is the
// water volume along the VIEW PATH (WaterBodyOpacity: steep/short path = clear
// bottom in shallows, grazing/long path = opaque, deep = timecyc alpha), so
// noon-shallow water now reads through while deep water stays tinted.
// INI `waterFresnelSplit` (c11.w) = 0 rolls the whole composite back to the
// legacy lerp(scene, surf, opacity) form without touching anything else.
// Tier 0/1 have no scene copy: WaterSplitBlend hands the same split to the
// hardware SRCALPHA blend (reflection live, the framebuffer behind shows
// through the alpha) so the refraction term degrades instead of killing
// reflection and vice versa.
//
// QUALITY TIERS (INI waterQuality 0-3, uniform branch on c11.x — no dynamic
// loops, no dependent reads; the tier ONLY removes cost, every tier renders
// correctly):
//   0 = 1 normal layer + timecyc colour + vertex-alpha translucency only
//       (no scene/depth copies, no s0/s2 fetches; output alpha hardware
//        blends against the framebuffer = live translucency at zero cost)
//   1 = + dual scrolling normals + shallow->deep depth ramp
//   2 = + refraction/scene copy + depth copy sampling + foam + shore blend
//   3 = + sun pierce + extra fine normal layer x3.7 + wide 5-tap shore depth
// The depth copy (PostFX_CopyDepthToTexture) only runs on the C++ side when
// tier >= 2; when it is unavailable the shader falls back to a distance
// driven column estimate so the ramp stays monotonic and artifact-free.
//
// VIEW FROM BELOW (defects "underwater refraction/caustics wrong at some
// angles", "ped through water not refracted", "seams from underwater"):
//  SurfBase.below flags the camera being under the (flat, Z-up) surface. That
//  one flag drives: camera-facing normal flip (otherwise NdotV/fresnel collapse
//  to 0 and the underside renders as a solid mirror), the refraction offset
//  direction (the depth delta sign reverses when looking up), the opacity
//  (near-clear face-on, mirror at grazing = TIR) and the underwater shimmer
//  sampled along the refracted view ray (angle-correct, not screen-locked).
//  Refraction itself is gated on "there is scene behind this pixel"
//  (thickness ramp) rather than the 2 m shore fade, so ANY geometry under the
//  surface — a swimmer 0.5 m down included — gets distorted, not just seabed.
//
// c0  = (time, waveSpeed, foamThreshold, foamSoftness)
// c1  = (normalStrength, tileScale, fresnelPower, reflectionStrength)
// c2  = (specularPower, specularIntensity, translucency, shoreFadeRange)
// c3  = (shallowR, shallowG, shallowB, deepRange)
// c4  = (deepR, deepG, deepB, waterAlpha)
// c5  = (screenW, screenH, 1/screenW, 1/screenH)
// c6  = (sunDirX, sunDirY, sunDirZ, sunBrightness)
// c7  = (camPosX, camPosY, camPosZ, reflValid 0/1)   <- xyz uploaded, PS-unread;
//                                                    w = G1 SSR validity flag
//                                                    (0 = analytic sky tint only,
//                                                     bit-identical to pre-G1)
// c8  = (nearClip, farClip, depthValid, refractScale)
// c9  = (skyTopR, skyTopG, skyTopB, 0)      <- timecycle sky reflection tint
// c10 = (skyBotR, skyBotG, skyBotB, 0)      <- timecycle horizon tint/fog colour
// c11 = (qualityTier, distFadeK, sunPierceScale, fresnelSplitFlag)
// c12 = (ambientR, ambientG, ambientB, skyLuma)   <- timecycle ambient + sky
// c13 = (dirR, dirG, dirB, dayFactor 0..1)        <- timecycle directional + day
// c14 = (bodyType, edgeBlend, foamScale, shoreScale)   <- W7-B body contract (all 0 = vanilla)
// c15 = (normScale, opacityScale, glitterScale, spare) <- W7-B body contract (0 = default 1.0)
// c16 = (foamTexValid, foamTexWeight, softIsecGain, reflMix) <- M2/M3/reflection-mix
//       foamTexValid: 1 = s4 foam texture bound (0 = M2 texture term exactly 0)
//       foamTexWeight: M2 RAGE foam-texture blend weight (0 = procedural-only)
//       softIsecGain: M3 Cry softIsec shallow-intersection feather gain (0 = off)
//       reflMix: reflection mix weight (INI waterReflectionMix, default 0.7;
//                0/unset = authored per-style strength — mismatch guard)
//
// s0 = scene colour (refraction capture)            [tier >= 2]
// s1 = water detail normal map (RGB=normal, A=height; mip-mapped)
// s2 = scene depth copy (raw window-space z, R32F)  [tier >= 2]
// s3 = G1 reflection copy (scene RT captured VERTICALLY FLIPPED by
//      waterPipe.cpp) [tier >= 2, IV/V cores only] — blended over the
//      analytic timecycle sky tint by the fresnel weight F. The validity
//      flag rides c7.w: 0 -> mix is exactly 0 (analytic-only, pre-G1
//      bit-identical); the fetch itself is unconditional per SM3.0
//      gradient rules (uniform-tier gated like every other fetch here).
// s4 = M2 foam texture (tiling white-noise + cellular, procedural,
//      generated CPU-side in waterPipe.cpp — same recipe family as the
//      detail map, no new asset). [tier >= 2, c16.x-gated: 0 = the blend
//      term is exactly 0 even though the fetch runs (SM3.0 gradient
//      rules), so an unbound/failed s4 can never change the output.]

sampler2D sceneTex   : register(s0);
sampler2D detailTex  : register(s1);
sampler2D depthTex   : register(s2);
sampler2D g_reflTex  : register(s3);   // G1 screen-space reflection (flipped scene)
sampler2D foamTex    : register(s4);   // M2 foam texture (tiling, procedural)

uniform float4 timeParams   : register(c0);
uniform float4 waterParams  : register(c1);
uniform float4 specParams   : register(c2);
uniform float4 shallowColor : register(c3);
uniform float4 deepColor    : register(c4);
uniform float4 screenSize   : register(c5);
uniform float4 sunDir       : register(c6);
uniform float4 camPos       : register(c7);
uniform float4 clipPlanes   : register(c8);
uniform float4 skyTopColor  : register(c9);
uniform float4 skyBotColor  : register(c10);
uniform float4 qualityParams : register(c11);   // (tier, distFadeK, pierceScale, fresnelSplit)
uniform float4 timeLight0    : register(c12);   // (ambient rgb, skyLuma)
uniform float4 timeLight1    : register(c13);   // (directional rgb, dayFactor)
uniform float4 bodyParams0   : register(c14);   // W7-B contract: (bodyType, edgeBlend, foamScale, shoreScale)
uniform float4 bodyParams1   : register(c15);   // W7-B contract: (normScale, opacityScale, glitterScale, spare)
uniform float4 waterFxParams : register(c16);   // M2/M3/refl-mix: (foamTexValid, foamTexWeight, softIsecGain, reflMix)

struct VS_OUTPUT {
    float4 position  : POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 worldPos  : TEXCOORD1;
    float3 viewDir   : TEXCOORD2;
    float3 lightDir  : TEXCOORD3;
    float4 screenPos : TEXCOORD4;
    // W7-B body contract: (bodyType code, edge blend ramp 0..1,
    // signed aspect axis, 0), forwarded by Water_VS from the Im3D vertex
    // normal. ALL-ZERO = contract not landed / vanilla water.dat plane ->
    // WaterBodyContract() pins every body scale to exactly 1.0 and the G3
    // river scroll offset is exactly zero (zero visual change).
    float4 bodyData  : TEXCOORD5;
    float4 vtxColor  : COLOR0;
};

// D3D9 window-space z -> view-space z (LH):  z = n*f / (f - d*(f-n))
float LinearizeDepth(float depth, float near, float far)
{
    float denom = far - depth * (far - near);
    return (near * far) / max(denom, 1e-3);
}

// Per-pixel setup shared by all styles. V points surface -> camera.
struct SurfBase {
    float3 V, L, H;
    float2 screenUV;
    float  viewZ;      // clip w = view-space depth of the water surface
    float  dist;
    float  distFactor; // 0 = near/detailed, 1 = far/calm
    float  vtxA;
    float  below;      // 1 = camera under the surface (V.z < 0, flat Z-up plane)
};

SurfBase WaterSetup(VS_OUTPUT IN)
{
    SurfBase S;
    // ---- Vectors (guard zero-length: NaN renders black in SM3.0) ----
    float vLen = max(length(IN.viewDir), 1e-7);
    float lLen = max(length(IN.lightDir), 1e-7);
    S.dist = vLen;
    S.V = -IN.viewDir / vLen;
    S.L = IN.lightDir / lLen;
    float3 hv = S.V + S.L;
    S.H = hv / max(length(hv), 1e-7);

    // ---- Screen UV: clip -> NDC [-1,1] -> texture [0,1] (v=0 = top row) ----
    float ndcW = max(IN.screenPos.w, 1e-7);
    float2 ndc = IN.screenPos.xy / ndcW;
    S.screenUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    S.viewZ = ndcW;

    // ---- Distance factor: near = detailed, far = calm/smooth ----
    // Smooth-out range ~12% of the far clip plane.
    float smoothRange = max(clipPlanes.y * 0.12, 10.0);
    S.distFactor = saturate(vLen / smoothRange);

    // Vertex alpha: SA buffers carry a per-quad fade — treat exact 0 as
    // "no fade" so an unpopulated alpha can never erase the whole water.
    float a = saturate(IN.vtxColor.a);
    S.vtxA = a > 0.001 ? a : 1.0;

    // Which side of the surface the camera is on: V points surface->camera on
    // a flat Z-up plane, so its Z sign is exact for every water quad height.
    S.below = S.V.z < 0.0 ? 1.0 : 0.0;
    return S;
}

// ============================================================================
// Timecycle lighting gates (defect 1: night water glowed white)
// ============================================================================

// Day/night factor from the timecycle sky luminance (c12.w / c13.w). 0 = full
// night, 1 = day. Same "sky the player sees" signal the project's other
// lighting models gate night brightness with.
float WaterDay(void)
{
    return saturate(timeLight1.w);
}

// Body lighting: ambient*waterColor + daylight*waterColor + directional tint —
// the shape every other lighting model uses. Normalised so a daylight frame
// reproduces the authored timecyc colour ~1:1 (vanilla match), while a night
// frame collapses to ambient + a sky-luminance share (dark, never a glow).
// Replaces the old unlit body + hardcoded brightness multipliers.
float3 WaterBodyLight(float3 waterCol, float NdotL)
{
    float dayF = saturate(timeLight1.w);
    float skyL = timeLight0.w;
    float3 lit = timeLight0.xyz * 1.5                    // ambient * colour
               + dayF * (0.74 + 0.16 * NdotL)            // daylight restore
               + (1.0 - dayF) * skyL * 3.0               // night: sky share
               + timeLight1.xyz * (dayF * NdotL * 0.10); // directional tint
    return waterCol * lit;
}

// Sun specular tint: timecycle directional colour gated by the day factor —
// an unlit white sun sheen can never be pumped into a night frame.
float3 WaterSpecTint(void)
{
    return timeLight1.xyz * lerp(0.12, 1.0, saturate(timeLight1.w));
}

// Foam is a white highlight too — same night gate, gentler (shore foam must
// stay readable at night, just not glow).
float WaterFoamGate(void)
{
    return lerp(0.40, 1.0, saturate(timeLight1.w));
}

// ============================================================================
// Smooth shorelines (LibertyRecomp water_ps1_smooth_shorelines /
// FusionShaders SmoothShorelines port — docs/plans §1C "smooth shorelines")
// ============================================================================
// LR's two additions to the console water_ps1 container:
//  1. EXPONENTIAL shallow-water coverage — the sheet's coverage over the
//     terrain rises as 1-exp2(-kx) instead of a linear ramp, so the alpha
//     meets the waterline with zero slope (no hard-shoulder pop on the shore
//     edge).
//  2. THREE extra spatial foam octaves at 2x/4x/8x frequency, weighted
//     0.5/0.25/0.125 and contrast-shifted saturate(sum - 0.4375) — a cheap
//     deterministic multi-scale breakup that hides the foam texture repeat
//     and lets the foam edge dissolve into individual wavelets instead of
//     ending on one flat smoothstep line.

// LR item 1 — exponential shore coverage: 0 at the waterline with ZERO
// slope, ->1 with thickness. x = thickness / shore range (waterShoreFade).
// exp2 of -x is always finite for x >= 0 (no NaN, no divide by zero: the
// range is epsilon-guarded).
// k = absorption coefficient. LR / the Xbox and V cores use 2 (unchanged);
// the IV core passes the authored GTA IV coefficient 3 — the authored form is
// 1 - exp2(-3 * depth * WaterColor.a) (water-refs.md IV section). At the 2 m
// default waterShoreFade, k = 3 reproduces that rate for a mid-range water
// alpha while keeping the INI range and the W7-B shore contract live.
float WaterShoreCoverageK(float thickness, float range, float k)
{
    float x = max(thickness, 0.0) / max(range, 1e-3);
    return saturate(1.0 - exp2(-k * x));
}

float WaterShoreCoverage(float thickness, float range)
{
    return WaterShoreCoverageK(thickness, range, 2.0);
}

// LR item 1 applied to the junction (seam) term: same endpoints as the old
// saturate(1 - frontFade*0.85) (1 at ff=0, 0.15 at ff=1 — the waterline
// seam-fix magnitude is preserved exactly) but on an exp2 curve, so the
// sheet dissolves into the scene behind on a ramp with a soft shoulder
// instead of a straight line ending in a kink. 2.737 = log2(1/0.15).
float WaterJunctionEase(float frontFade)
{
    return exp2(-saturate(frontFade) * 2.7370);
}

// LR item 2 — multi-octave foam height: the caller's base height channel plus
// three fixed spatial octaves at 2x/4x/8x of the caller's detail UV (plain
// scales -> no dependent reads; mip-mapped texture keeps them alias-free),
// weighted 0.5/0.25/0.125 and recentered by -0.4375 exactly like LR — a flat
// mid-height channel reproduces its own value (0.5 -> 0.5) and only crest
// variance is added. 3 extra tex2D taps, no loops.
float WaterFoamOctaves(float2 uv, float baseH)
{
    float o1 = tex2D(detailTex, uv * 2.0).w;
    float o2 = tex2D(detailTex, uv * 4.0).w;
    float o3 = tex2D(detailTex, uv * 8.0).w;
    return saturate(baseH + o1 * 0.5 + o2 * 0.25 + o3 * 0.125 - 0.4375);
}

// ============================================================================
// M2 — RAGE foam-texture blend (water_common.fxh WaterShadingCommon foam
// block, ~:936) + M3 — CryEngine soft-intersection foam (Water.cfx:1222-1250)
// ============================================================================
// M2 (port of the RAGE block):
//   foam = FoamMask * (len(flatNormal.xy) * 0.27 + 0.44);
//   foam = tex1D(BlendSampler, foam + FoamIntensity).g * refractionColor.a;
//   FOAMSCALE = 1.0, FOAMWEIGHT = 0.65 (water_common.fxh:24).
// FoamMask = the s4 tiling foam texture (procedural white-noise + cellular,
// generated CPU-side in waterPipe.cpp next to the detail normal map — no new
// asset dependency). flatNLen = the caller's pre-normalize wave-gradient XY
// length (RAGE's flatNormal.xy). The BlendSampler 1D gradient LUT is folded
// into ALU (a smoothstep ramp with the LUT's zero-slope endpoints) — saves
// the s5 LUT sampler the spec budgeted (one new sampler total, s4; c16-c31
// headroom untouched except c16). NaN-safe: saturate/smoothstep of saturated
// inputs only, no divides. The fetch runs unconditionally inside the
// caller's uniform tier>=2 branch (SM3.0 gradient rules, same precedent as
// every other fetch in this file); the c16.x gate zeroes the CONTRIBUTION,
// so an unbound/failed s4 can never change the output.
float WaterFoamTexture(float2 fuv, float flatNLen)
{
    float foamMask = saturate(tex2D(foamTex, fuv).r);
    float foam = saturate(foamMask * (flatNLen * 0.27 + 0.44));
    return smoothstep(0.30, 0.85, foam);   // BlendSampler gradient stand-in
}

// M3 (Cry Water.cfx:1228-1232 softIsec shaping, adapted to our depth copy):
// depthDiff = sceneZ - waterZ IS the water column thickness SampleWaterColumn
// already fetched from the s2 depth copy — zero new fetches. Where the water
// is shallow over geometry (piers, seawalls, the shoreline) the foam band
// widens/feathers; deep water leaves it untouched. Cry's
//   softIsec = saturate(0.75 * (sceneDepth - surfaceW))
// is range-normalised by the shore band so the feather width tracks
// waterShoreFade (and the W7-B shore contract), then shaped by Cry's
//   saturate(softIsec - softIsec^2)
// bump: 0 at the exact intersection line (thickness 0 — the junction must
// reproduce the scene behind, no foam smear onto land) and 0 in deep water,
// peaking at softIsec = 0.5 (x4 restores the peak to 1.0). All divides
// guarded (max(range, 1e-3)); saturate of a squared saturated base — NaN-safe.
float WaterSoftIsec(float thickness, float range)
{
    float softIsec = saturate(0.75 * max(thickness, 0.0) / max(range, 1e-3));
    return saturate(softIsec - softIsec * softIsec) * 4.0;
}

// Reflection mix weight (c16.w — INI waterReflectionMix, file-static default
// 0.7 in waterPipe.cpp until main.cpp wires the key): moderate global
// reduction of the reflection lobe (user: "water has sometimes big
// reflections"). 1.0 = authored per-style strength; the anti-chrome cap
// structure is unchanged. 0/unset (older pipe binary + new shader mismatch)
// = the caller's fallback, so a stale pipe can never zero the lobe.
float WaterReflMix(float fallback)
{
    return waterFxParams.w > 1e-4 ? saturate(waterFxParams.w) : fallback;
}

// 2-lobe sun glitter (water-refs.md V/CryEngine SunSpecular port, V style):
// R = reflect(-V, N), tight sparkle lobe LdotR^E1 + wide sheen lobe LdotR^E2
// (exponents derived from the style's Blinn power). Zero fetches; every
// denominator is a saturate/pow of a saturated base (NaN-safe). The CALLER
// gates the result by the day factor and WaterFoamGate — an unlit white sun
// sparkle in a night frame is exactly the defect the night gate exists for.
float WaterGlitter(float3 V, float3 N, float3 L, float blinnPow)
{
    float3 R = reflect(-V, N);
    float ldr = saturate(dot(L, R));
    float e1 = max(blinnPow * 4.0, 64.0);   // tight sparkle lobe
    float e2 = max(blinnPow * 0.5, 8.0);    // wide sheen lobe
    return pow(ldr, e1) * 0.85 + pow(ldr, e2) * 0.15;
}

// ============================================================================
// Body-type contract (W7-B lane — per-vertex body classification)
// ============================================================================
// W7-B's CPU pass classifies each water quad (water-refs.md 3rd recon:
// OCEAN/SHALLOW/INTERIOR/RIVER/POND/LAKE) and writes (bodyType, edgeBlend)
// into the Im3D vertex normal; Water_VS forwards it as
// TEXCOORD5 = (bodyType, edgeBlend, 0, 0). Draw-level fallback/override:
//   c14 = (bodyType, edgeBlend, foamScale, shoreScale)
//   c15 = (normScale, opacityScale, glitterScale, spare)
// c14.z..c15.z are OPTIONAL C++ fine-tune multipliers: 0 = unset = 1.0.
// Codes: 0=OCEAN 1=SHALLOW/SHORE 2=INTERIOR/POOL 3=RIVER 4=POND 5=LAKE.
//
// VANILLA GUARD (never-regress): when TEXCOORD5 AND c14/c15 are all zero
// (W7-B not landed yet / plain water.dat planes) B.active = 0 and every
// derived scale is pinned to exactly 1.0 — the shading algebra is the
// pre-contract one, bit for bit. A code outside the enum range (undefined
// interpolator garbage, NaN — NaN fails both comparisons) also forces
// active = 0.
struct BodyContract {
    float active;     // 0 = vanilla path (all contract data zero)
    float foam;       // foam amount multiplier
    float shore;      // shore-fade range multiplier (<1 = tighter band)
    float norm;       // normal strength multiplier
    float opac;       // body opacity multiplier
    float glitter;    // sun-glitter multiplier
};

// 0-unset convention: an optional multiplier of exactly 0 means "not
// uploaded" and resolves to the neutral 1.0.
float BodyScaleOr(float v, float def)
{
    return v > 1e-4 ? v : def;
}

BodyContract WaterBodyContract(VS_OUTPUT IN)
{
    BodyContract B;
    float4 dv = IN.bodyData;   // per-vertex (W7-B scheme 1)
    float4 p0 = bodyParams0;   // draw-level (quad-level scheme / override)
    float4 p1 = bodyParams1;

    // All-zero guard: no per-vertex code/blend and no draw constants.
    float mag = abs(dv.x) + abs(dv.y)
              + abs(p0.x) + abs(p0.y) + abs(p0.z) + abs(p0.w)
              + abs(p1.x) + abs(p1.y) + abs(p1.z) + abs(p1.w);
    B.active = mag > 1e-4 ? 1.0 : 0.0;

    // Per-vertex code/blend win when present; c14 is the draw-level fallback.
    float type = abs(dv.x) > 1e-4 ? dv.x : p0.x;
    float edge = abs(dv.y) > 1e-4 ? dv.y : p0.y;

    // Sanity: a code outside the enum = garbage interpolator (the VS side of
    // this contract may not land in the same build) -> vanilla.
    B.active *= (type >= -0.25 && type <= 5.25) ? 1.0 : 0.0;
    B.active *= (edge >= -0.25 && edge <= 1.25) ? 1.0 : 0.0;

    // Exclusive per-code weights. Interpolated fractional codes at quad
    // borders (two neighbouring bodies sharing a vertex row) blend smoothly
    // between the two tables instead of stepping.
    float isShore = saturate(type);         // type >= 1
    float isPool  = saturate(type - 1.0);   // type >= 2
    float isRiver = saturate(type - 2.0);   // type >= 3
    float isPond  = saturate(type - 3.0);   // type >= 4
    float isLake  = saturate(type - 4.0);   // type >= 5
    float c0 = 1.0 - isShore;
    float c1 = isShore - isPool;
    float c2 = isPool - isRiver;
    float c3 = isRiver - isPond;
    float c4 = isPond - isLake;
    float c5 = isLake;

    // Per-body deviation table (1.00 = style default):
    //            foam  shore  norm  opac  glitter
    // OCEAN  0:  1.00  1.00   1.00  1.00  1.00
    // SHORE  1:  1.45  0.70   1.00  1.20  1.00  (strong band, tight fade)
    // POOL   2:  0.15  0.80   0.55  1.15  0.60  (calm, near-clear, still)
    // RIVER  3:  1.20  0.85   0.90  1.00  0.80
    // POND   4:  0.45  0.90   0.70  1.05  0.70
    // LAKE   5:  0.80  1.00   0.85  1.00  0.90
    // Edge ramp: body deviations fade back to the style default at quad
    // borders so neighbouring quads of different bodies cannot seam.
    float w = saturate((abs(dv.y) > 1e-4 || abs(p0.y) > 1e-4) ? edge : 1.0);
    B.foam    = lerp(1.0, c0 * 1.00 + c1 * 1.45 + c2 * 0.15 + c3 * 1.20 + c4 * 0.45 + c5 * 0.80, w);
    B.shore   = lerp(1.0, c0 * 1.00 + c1 * 0.70 + c2 * 0.80 + c3 * 0.85 + c4 * 0.90 + c5 * 1.00, w);
    B.norm    = lerp(1.0, c0 * 1.00 + c1 * 1.00 + c2 * 0.55 + c3 * 0.90 + c4 * 0.70 + c5 * 0.85, w);
    B.opac    = lerp(1.0, c0 * 1.00 + c1 * 1.20 + c2 * 1.15 + c3 * 1.00 + c4 * 1.05 + c5 * 1.00, w);
    B.glitter = lerp(1.0, c0 * 1.00 + c1 * 1.00 + c2 * 0.60 + c3 * 0.80 + c4 * 0.70 + c5 * 0.90, w);

    // Optional C++ fine-tunes (0 = unset -> 1.0).
    B.foam    *= BodyScaleOr(p0.z, 1.0);
    B.shore   *= BodyScaleOr(p0.w, 1.0);
    B.norm    *= BodyScaleOr(p1.x, 1.0);
    B.opac    *= BodyScaleOr(p1.y, 1.0);
    B.glitter *= BodyScaleOr(p1.z, 1.0);

    // Inactive contract -> pin every scale to exact vanilla identity.
    float on = B.active;
    B.foam    = lerp(1.0, B.foam,    on);
    B.shore   = lerp(1.0, B.shore,   on);
    B.norm    = lerp(1.0, B.norm,    on);
    B.opac    = lerp(1.0, B.opac,    on);
    B.glitter = lerp(1.0, B.glitter, on);
    return B;
}

// ============================================================================
// Detail-normal mip-amplitude restore (defect: one normal layer "turns
// transparent" and the effect is lost)
// ============================================================================
// Hardware mips average the normal map's XY toward 0, and the higher-frequency
// layer reaches a coarser mip level first — it used to fade out alone while
// the base layer was still live, taking the wave detail with it (worst in the
// IV preset, where that layer carries 5x the amplitude). Measure the fetch's
// own footprint in texels/pixel with ddx/ddy (ps_3_0, no dependent reads, no
// loops) and hand back a bounded amplitude boost: it only applies where the
// fetch is already smooth (>= 1 texel/pixel), so it cannot re-alias, and both
// layers keep contributing instead of one vanishing.
float LayerAmpRestore(float2 uv)
{
    float2 fp = float2(length(ddx(uv)), length(ddy(uv))) * 256.0; // 256px tile
    float blur = saturate(max(fp.x, fp.y) - 1.0);
    return 1.0 + 1.5 * blur;
}

// Gentle far blend (defect: hard shallow->deep / near->far seam, dark band at
// the horizon). Converge the body colour toward the authored deep colour mixed
// with the timecycle horizon colour (the fog colour distant water fades into)
// over the FAR HALF of the distance ramp — a ramp, not a step, endpoint =
// vanilla water colour + horizon.
// Weight capped at 0.45 (was 0.85): the old value let the far half of the ramp
// converge most of the way into the horizon tint, which read as a hard flat
// band of one colour at distance. 0.45 keeps the majority of the body's own
// depth shading alive while still softening the far edge.
float3 FarBlendWater(float3 waterCol, float distFactor)
{
    float farT = smoothstep(0.40, 1.0, distFactor);
    float3 farCol = lerp(deepColor.rgb, skyBotColor.rgb, 0.22);
    return lerp(waterCol, farCol, farT * 0.45);
}

// Opacity of the sheet seen from UNDER the surface: near-clear face-on (you
// look straight up through it) and a mirror at grazing angles (total internal
// reflection). This is the BELOW-side reflectance of the fresnel split below —
// it is what hands the grazing angles to the reflection lobe while the steep
// angles stay transmissive, so refraction AND reflection are both live.
float WaterBelowOpacity(float NdotV)
{
    float fres = 0.02 + 0.98 * pow(saturate(1.0 - NdotV), 5.0);
    return saturate(0.12 + 0.88 * fres);
}

// ============================================================================
// Fresnel split composite (defect: "reflection and refraction work one at a
// time — but not both ways!")
// ============================================================================
// The old composite was  lerp(sceneBehind, surf, bodyOpacity)  with the
// reflection weight living INSIDE surf — so the depth-driven body opacity
// multiplied the reflection too, and the two lobes could never both be live:
//   * shallow/medium water: bodyOpacity ~0.4 => the scene behind showed through
//     at EVERY angle (grazing included, where it should be a mirror) and the
//     reflection read as ~40% of its intended strength;
//   * deep water: bodyOpacity ~0.94 at every angle => a steep noon view of the
//     bottom stayed opaque ("day transparency not great enough").
// Split instead (energy-conserving, both lobes live at every view direction,
// above AND below the surface):
//   result = F * reflectedEnv + (1-F) * [ bodyT * waterBody + (1-bodyT) * scene ]
//   F     = the style's fresnel reflectance (capped < 1: never chrome)
//   bodyT = how much water sits along the VIEW path (depth x view angle)
// ============================================================================
float3 WaterSplitOpaque(float3 behind, float3 reflectC, float3 bodyC,
                        float F, float bodyT, float3 highlights)
{
    // SMOOTH SHORELINES (LR port): the shore blend reaches this composite
    // through bodyT (WaterBodyOpacity's exponential coverage + exp junction
    // ease) and F's RefractGate — the split ALGEBRA is deliberately
    // unchanged so tier 0/1 WaterSplitBlend stays its exact algebraic
    // counterpart (tier-parity contract).
    float bt = saturate(bodyT);
    float3 transmit = lerp(behind, bodyC, bt);
    return reflectC * F + transmit * (1.0 - F) + highlights;
}

// Tier 0/1 have no scene copy, so the hardware SRCALPHA blend has to
// approximate the same split against the framebuffer. Weight w = F+(1-F)*bodyT
// and the source carries the rest, so src*w + dst*(1-w) is algebraically the
// opaque formula (highlights ride w so they cannot blow up in a near-clear
// pixel, where dividing by a tiny w would turn foam into a white line).
float4 WaterSplitBlend(float3 reflectC, float3 bodyC, float F, float bodyT,
                       float3 highlights)
{
    // Same smooth-shoreline bodyT as WaterSplitOpaque (fed from the caller,
    // NOT recomputed) — see the note there; algebra below is unchanged for
    // tier 0/1 parity.
    float bt = saturate(bodyT);
    float w = saturate(F + (1.0 - F) * bt);
    float3 A = reflectC * F + bodyC * (1.0 - F) * bt + highlights * saturate(w * 4.0);
    float invW = 1.0 / max(w, 1e-4);
    return float4(saturate(A * invW), w);
}

// Water-volume opacity along the VIEW path through the water. This is the
// "day transparency" curve: a steep (noon) view takes a short path through the
// volume => the bottom reads clearly in the shallows; a grazing view takes a
// long path => the sheet goes opaque; deep water saturates to the authored
// timecyc alpha (vanilla-like). deepRange/2 (c3.w) is the half-opacity column.
// Under water there is no layer between the eye and the surface (we are inside
// it), so the body lobe is 0 there — fresnel alone decides, which is what makes
// the above-water world refract cleanly while the grazing TIR mirrors.
float WaterBodyOpacity(float thickness, float NdotV, float frontFade,
                       float vtxA, float below)
{
    float pathLen = thickness / max(NdotV, 0.12);   // guarded: grazing clamp
    float halfCol = max(shallowColor.w * 0.5, 0.5);
    float t = 1.0 - exp2(-pathLen / halfCol);       // 2^-x, x>=0 -> (0,1]
    t *= deepColor.w * specParams.z;                // timecyc alpha x translucency
    // Junction: reproduce the scene behind. LR SmoothShorelines port — the
    // old linear saturate(1 - frontFade*0.85) is replaced by an exp2 ease
    // with the SAME endpoints (1 at ff=0, 0.15 at ff=1, so the waterline
    // seam-fix magnitude is preserved) but a soft shoulder instead of a
    // hard kink where the ramp ran out.
    t *= WaterJunctionEase(frontFade);
    t *= saturate(vtxA);
    t *= (1.0 - saturate(below));
    return saturate(t);
}

// Water-column thickness (metres of water below this pixel) + the near-
// junction fade where ANOTHER surface (land or water drawn in an earlier
// buffer flush) meets this one. Only fetches depth when tier >= 2 AND the
// depth copy is live (clipPlanes.z); otherwise a distance-driven estimate
// keeps the shallow->deep ramp monotonic (no artifacts, zero fetches).
// Returns (thickness, frontFade).
float2 SampleWaterColumn(float2 screenUV, float waterViewZ, int tier)
{
    if(clipPlanes.z < 0.5)
        return float2(saturate(waterViewZ * 0.02) * max(shallowColor.w * 2.0, 1.0), 0.0);

    float2 px = screenSize.zw;
    float z0 = tex2D(depthTex, screenUV).x;
    float zmin = z0;
    if(tier >= 3){
        // Centre + 4-tap cross min (tier 3 "wider shore sampling"): the
        // closer neighbour depth pulls the fade in and blends the junction
        // instead of leaving a hard pop line.
        float z1 = tex2D(depthTex, screenUV + float2( px.x, 0)).x;
        float z2 = tex2D(depthTex, screenUV + float2(-px.x, 0)).x;
        float z3 = tex2D(depthTex, screenUV + float2(0,  px.y)).x;
        float z4 = tex2D(depthTex, screenUV + float2(0, -px.y)).x;
        zmin = min(min(min(z0, z1), min(z2, z3)), z4);
    }

    float sceneZ = LinearizeDepth(z0, clipPlanes.x, clipPlanes.y);
    float thickness = max(sceneZ - waterViewZ, 0.0);
    float frontFade = saturate((waterViewZ - LinearizeDepth(zmin, clipPlanes.x, clipPlanes.y))
                               / max(specParams.w, 1e-3));
    return float2(thickness, frontFade);
}

// Timecycle sky reflection tint along reflect(-V, N). World is Z-up so the
// horizon->zenith blend rides the reflected ray's Z component. From below the
// reflected ray points down into the water, so the environment it sees is the
// underwater volume (timecycle deep colour), not the sky. Distant reflections
// converge on the HORIZON colour (timecycle sky bottom = the fog colour the
// far field fades into) instead of collapsing to flat grey — the grey collapse
// is what turned the horizon band murky green.
float3 SkyReflection(float3 N, float3 V, float distFactor, float below)
{
    float3 R = reflect(-V, N);
    float skyBlend = saturate(R.z);
    float3 skyReflect = lerp(skyBotColor.rgb, skyTopColor.rgb, skyBlend);
    float3 underEnv = lerp(deepColor.rgb, skyBotColor.rgb, 0.35);
    skyReflect = lerp(skyReflect, underEnv, below);
    return lerp(skyReflect, skyBotColor.rgb, distFactor * 0.45);
}

// ============================================================================
// G1 screen-space reflection (IV/V cores only — the Xbox core keeps its
// analytic reflectivity-cap look).
// ============================================================================
// waterPipe.cpp (tier >= 2 gate) captures the scene RT AGAIN into an A8R8G8B8
// texture with a VERTICAL FLIP and binds it at s3; the water pixel therefore
// samples the mirrored content of its own screen position — the planar-
// projection approximation of an environment reflection (prior art:
// mirrorPass.cpp projective composite). Blended over the analytic timecycle
// sky tint by the existing fresnel weight F: F already carries the style's
// waterParams.w (= waterReflectionStrength) reflectivity cap plus the
// RefractGate + junction gates, so saturate(F) <= waterReflectionStrength
// (anti-chrome). Validity flag = c7.w (camPos.w, PS-unread component — see
// the c7 line in the header): 0 -> mix exactly 0 -> analytic-only output,
// bit-identical to pre-G1. The fetch is unconditional (no dynamic branch over
// a texture fetch — SM3.0 gradient rules); the uniform tier gate at the call
// site is the same precedent as every other tier-gated fetch in this file.
// From below the flipped capture is meaningless (the analytic underwater env
// is authored), so the mix is zeroed there too.
// P1 WATER FIX: the vertical flip mirrors the scene about SCREEN CENTER
// (v = 0.5), not about the waterline — water pixels in the lower half of the
// screen sample rows above the waterline (pure sky ~= this analytic tint, so
// the SSR read as invisible). WaterSSRBlend below clamps the sample v into
// the water region and fades the mix by how far the unclamped mirror would
// have left it (smooth, no seam).
float3 WaterSSRBlend(float3 analyticSky, float3 N, float2 screenUV,
                     float F, float below, float distFactor)
{
    // Small ripple offset along the caller's (already damped, anti-chrome)
    // reflection normal so the mirror image rides the waves.
    float2 ruv = clamp(screenUV + N.xy * 0.015 * (1.0 - 0.7 * distFactor),
                       float2(0.001, 0.001), float2(0.999, 0.999));
    // ---- P1 waterline mirror fix ----
    // The s3 capture is stored VERTICALLY FLIPPED (backbuffer row y lands in
    // texture row H-1-y, CopySceneFlippedToTexture), so sampling it at the
    // water pixel's OWN screen v mirrors the scene about SCREEN CENTER:
    // a lower-half water pixel (wv > 0.5) reads scene row 1 - v, which is
    // ABOVE the waterline for every pixel below screen center — pure sky,
    // i.e. ~= the analytic tint this function blends over (the SSR was
    // invisible even at reflValid = 1). Cheap robust fix, no horizon
    // constant uploaded: keep the sample INSIDE the water region. In the
    // flipped texture the scene's lower half (water region) lives at v < 0.5,
    // so clamp the flipped sample v to just under 0.5, and fade the SSR mix
    // back toward the analytic sky tint in proportion to how far the
    // unclamped sample would have left the water region (v_fv - 0.5). The
    // clamp shift and the fade grow together (self-masking): by the time the
    // clamp would visibly stretch the strip the weight is already ~0 —
    // smooth fade, no new seam. Fetch stays unconditional (SM3.0 gradient
    // rules); wv/vFv are plain ALU on interpolator data.
    float wv = screenUV.y;                            // water pixel's own screen v
    float vFv = ruv.y;                                // unclamped flipped-sample v
    float leaveT = smoothstep(0.0, 0.25, vFv - 0.5);  // 0 = inside water region
    float vSample = (wv > 0.5) ? min(vFv, 0.5 - 0.004) : vFv;
    float3 reflC = saturate(tex2D(g_reflTex, float2(ruv.x, vSample)).rgb);
    float w = saturate(F) * saturate(camPos.w) * saturate(1.0 - below)
            * (1.0 - leaveT);
    return lerp(analyticSky, reflC, w);
}

// ============================================================================
// G3 river flow scroll — TEXCOORD5.z spare signed aspect (waterPipe W7-B
// classify: sign = plane long axis, |z| = extent ratio 1..64).
// ============================================================================
// Only RIVER bodies (code 3) scroll, weighted by the same exclusive per-code
// weight WaterBodyContract uses so type borders blend instead of stepping.
// Direction = the plane's long axis (+X when the aspect is positive, +Y when
// negative — SA water.dat flowX/flowY are all zero, water-refs.md:162, so the
// plane orientation IS the flow). Speed saturates at aspect ratio 8 and caps
// at 0.03 UV/s (spec cap 0.05). Exactly zero for every non-river body, and
// exactly zero when classification is off (bodyData all-zero -> code 0).
float2 WaterRiverFlow(float4 bodyData, float time)
{
    float type = bodyData.x;
    float riverW = saturate(type - 2.0) - saturate(type - 3.0);   // RIVER c3 weight
    float aspect = bodyData.z;
    float2 dir = aspect > 1e-4  ? float2(1.0, 0.0)
               : aspect < -1e-4 ? float2(0.0, 1.0)
                                : float2(0.0, 0.0);
    float mag = min(abs(aspect) * (1.0 / 8.0), 1.0) * 0.03;       // <= 0.03 UV/s
    return dir * (mag * riverW * time);
}

// Refraction gate: 0 exactly at the waterline (reproduce the scene behind, no
// smear onto land), 1 by ~0.33 m of water. Deliberately NOT the 2 m shore fade
// — any geometry under the surface (a swimmer, a rock 0.5 m down) must refract,
// not only the seabed.
float RefractGate(float thickness)
{
    return saturate(thickness * 3.0);
}

// Underwater shimmer (camera below the surface) — the "caustics" path. The
// pattern is sampled where the eye's REFRACTED ray crosses the surface, so it
// tracks the view angle at every incidence instead of sticking to a fixed
// screen position; day-gated (there is no light to focus at night) and
// deliberately subtle. refract() returns 0 on TIR — guarded, no NaN.
float WaterUnderShimmer(float3 V, float3 N, float2 wuv, float time, float below)
{
    float dayF = saturate(timeLight1.w);
    float3 r = refract(-V, N, 1.5);   // water -> air (0 on TIR -> dir below)
    float rl = max(length(r), 1e-4);
    float2 cuv = wuv * 0.6 + (r.xy / rl) * 0.35 + float2(0.017, -0.011) * time;
    // The fetch runs unconditionally: `below` is per-pixel, and a tex2D inside
    // a divergent branch has undefined gradients (wrong mip at the waterline).
    float h = tex2D(detailTex, cuv).w;
    return pow(saturate(h), 5.0) * 0.20 * dayF * below;
}

// Tier-3 sun pierce (V recipe: pow(saturate(dot(refract(V,lerp(N,N0,.5),
// 1/1.5),-Sun)),2) * saturate(depth/10)) — the view ray continued into the
// water aligned with the sun's underwater propagation direction. Zero on
// total internal reflection (refract returns a zero vector).
float SunPierce(float3 V, float3 N, float3 L, float thickness)
{
    // Guarded normalize (no-unguarded-divide doctrine). Provably non-zero at
    // the call sites (only reached with below < 0.5 => N.z > 0 => the lerp
    // keeps length >= 0.5), but the 1e-7 guard makes the TIR/NaN path exact.
    float3 nmidV = lerp(N, float3(0.0, 0.0, 1.0), 0.5);
    float3 Nmid = nmidV / max(length(nmidV), 1e-7);
    float3 rd = refract(-V, Nmid, 1.0 / 1.5);
    float rdLen = length(rd);
    if(rdLen < 1e-4)
        return 0.0;
    float a = saturate(dot(rd / rdLen, -L));
    return a * a * saturate(thickness * 0.1);
}

// ============================================================================
// main_xbox — Xbox/PS2-style core (the rewritten base, tier-gated)
// ============================================================================

float4 WaterXbox(VS_OUTPUT IN)
{
    SurfBase S = WaterSetup(IN);
    int tier = (int)qualityParams.x;
    float time = timeParams.x * timeParams.y;
    float distFactor = S.distFactor;
    float below = S.below;
    float dayF = saturate(timeLight1.w);
    float split = saturate(qualityParams.w);   // 1 = fresnel split, 0 = legacy
    BodyContract B = WaterBodyContract(IN);    // W7-B contract; all scales = 1.0 until data lands

    // ---- Water column (shore blending + shallow->deep ramp) ----
    float thickness = 0.0;
    float frontFade = 0.0;
    float deepFactor = 0.65;   // tier 0: flat timecyc colour mix
    if(tier >= 2){
        float2 col = SampleWaterColumn(S.screenUV, S.viewZ, tier);
        thickness = col.x;
        frontFade = col.y;
        // Zero-slope approach to full depth: a plain saturate() hit 1.0 with a
        // kink at deepRange and showed as a hard seam where the shallow band
        // met the deep water.
        deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }else{
        // Tiers 0/1: zero-fetch column estimate. The colour ramp and the
        // body-opacity path both still need a thickness without the depth
        // copy, so the composite degrades (no scene fetch, hardware alpha
        // blend) instead of a whole term dying.
        thickness = saturate(S.viewZ * 0.02) * max(shallowColor.w * 2.0, 1.0);
        if(tier >= 1)
            deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }
    // LR smooth shorelines: exponential coverage (zero slope at the
    // waterline) instead of the old linear saturate(thickness/range); the
    // body contract tightens/loosens the band per water body (1.0 = vanilla
    // range until W7-B data arrives).
    float shoreFade = WaterShoreCoverage(thickness, specParams.w * B.shore);
    if(tier < 2) shoreFade = 1.0;

    // ---- Scrolling detail normals (mip-mapped texture -> mip-correct LOD
    //      from screen-space derivatives; no sub-pixel sparkle). One fetch
    //      per layer; tier 3 adds the fine x3.7 layer. Both live layers get
    //      the mip-amplitude restore so neither can fade out alone. ----
    float2 uv = IN.texCoord * waterParams.y;
    float2 flow1 = float2( 0.021, 0.013) * time;
    float2 flow2 = float2(-0.016, 0.027) * time;
    // G3 river flow scroll (see WaterRiverFlow) — RIVER bodies drift along
    // their long axis; every other body gets an exactly-zero offset.
    float2 riverFlow = WaterRiverFlow(IN.bodyData, time);
    float2 tuv1 = uv * 1.00 + flow1 + riverFlow;
    float2 tuv2 = uv * 2.37 + flow2 + riverFlow * 1.6;
    float amp1 = LayerAmpRestore(tuv1);
    float amp2 = LayerAmpRestore(tuv2);

    float4 t1 = tex2D(detailTex, tuv1);
    float3 n1 = t1.xyz * 2.0 - 1.0;
    n1.xy *= amp1;
    float3 n2 = float3(0.0, 0.0, 0.0);
    float h2 = 0.0;
    if(tier >= 1){
        float4 t2 = tex2D(detailTex, tuv2);
        n2 = t2.xyz * 2.0 - 1.0;
        n2.xy *= amp2;
        h2 = t2.w;
    }
    if(tier >= 3){
        float4 t3 = tex2D(detailTex, uv * (2.37 * 3.7) - flow1 * 1.7);
        n2 += (t3.xyz * 2.0 - 1.0) * 0.35;
    }

    // Strength fades with distance -> far water goes calm/smooth, but the
    // damping is 0.55 (was 0.85): 0.85 left only ~15% of the normal detail at
    // range, so the far surface collapsed toward a flat coloured sheet. The
    // body contract's normal multiplier is 1.0 until W7-B data arrives.
    float strength = waterParams.x * B.norm * (1.0 - 0.55 * distFactor);
    // Flat XY water plane: tangent space == world space (z up).
    // M2: keep the pre-normalize gradient — RAGE's foam block modulates the
    // foam texture by len(flatNormal.xy) (this XY, before normalize).
    float2 flatXY = (n1.xy + n2.xy * 0.7) * strength;
    float3 N = normalize(float3(flatXY, 1.0));

    // ---- View from below: the shading normal points up, away from an
    // underwater camera — flip it to face the viewer or NdotV/fresnel collapse
    // to 0 and the underside renders as one solid mirror with no refraction.
    // The flipped XY also reverses the refraction offset direction, which is
    // what the depth delta does when you look up through the surface. ----
    if(below > 0.5)
        N = -N;

    float NdotV = max(dot(N, S.V), 0.0);
    float NdotL = max(dot(N, S.L), 0.0);
    float NdotH = max(dot(N, S.H), 0.0);

    // ---- Fresnel (Schlick, F0 = 0.02 water) ----
    // Weak face-on, moderate at grazing; reflectionStrength is the
    // reflectivity cap (< 1) so it can never read as chrome. reflAmt is the
    // RAW style reflectance (used by the legacy composite); F below is the
    // split's reflection lobe with the below/junction gates applied.
    float fresnel = 0.02 + 0.98 * pow(saturate(1.0 - NdotV), max(waterParams.z, 1.0));
    float reflAmt = saturate(fresnel * waterParams.w);
    // Reflection mix weight (c16.w — see WaterReflMix): moderate global
    // reduction, default 0.7 (user: "water has sometimes big reflections").
    reflAmt *= WaterReflMix(1.0);

    // ---- Sky reflection (timecycle sky top/horizon colours) ----
    float3 skyReflect = SkyReflection(N, S.V, distFactor, below);

    // ---- Water body colour: depth ramp + gentle far blend + timecycle light --
    float3 waterCol = lerp(shallowColor.rgb, deepColor.rgb, deepFactor);
    waterCol = FarBlendWater(waterCol, distFactor);
    waterCol = WaterBodyLight(waterCol, NdotL);

    // ---- Sun specular (roughness grows with distance -> no sparkle) ----
    float specPow = max(specParams.x * (1.0 - 0.85 * distFactor), 8.0);
    float spec = pow(NdotH, specPow) * specParams.y * (1.0 - 0.6 * distFactor);

    // ---- Shore foam band (foamThreshold/softness knobs, tier >= 2) ----
    // LR smooth-shorelines foam: multi-octave height breakup (2x/4x/8x of
    // the detail UV) so the band edge dissolves into wavelets instead of
    // ending on one flat smoothstep line; band algebra and INI knobs
    // (waterFoamThreshold/softness) unchanged. Body-contract foam scale on
    // top (1.0 until W7-B data arrives).
    float foam = 0.0;
    if(tier >= 2){
        float h = WaterFoamOctaves(tuv1, (t1.w + h2) * 0.5);
        float band = smoothstep(0.02, 0.02 + max(timeParams.w, 0.01), shoreFade)
                   * (1.0 - smoothstep(max(timeParams.z, 0.05) * 0.5, max(timeParams.z, 0.1), shoreFade));
        // M2+M3: RAGE foam-texture blend (s4, c16.x/y-gated) + Cry softIsec
        // shallow-intersection feather (c16.z-gated). waterFxParams.x/y/z
        // all 0 -> texFoam = 0, pattern = procFoam, isec term = 0 -> the
        // algebra collapses to the pre-M2/M3 line bit for bit.
        float procFoam = smoothstep(0.55, 0.85, h);
        float texFoam = WaterFoamTexture(tuv1 * 0.31 + float2(0.004, -0.003) * time,
                                         length(flatXY)) * saturate(waterFxParams.x);
        float isec = WaterSoftIsec(thickness, specParams.w * B.shore);
        float pattern = saturate(procFoam + texFoam * saturate(waterFxParams.y) * 0.75);
        foam = (band * pattern
              + isec * saturate(waterFxParams.z) * pattern * (1.0 - band) * 0.5)
              * 0.25 * B.foam * WaterFoamGate();
    }

    // ---- Sun pierce (tier 3, subtle; day-gated, above-water only) ----
    float pierce = 0.0;
    if(tier >= 3 && below < 0.5)
        pierce = SunPierce(S.V, N, S.L, thickness) * qualityParams.z * dayF;

    // ---- Composite -------------------------------------------------------
    // Both lobes of the fresnel split, plus the night-gated white highlights.
    float3 highlights = spec * float3(1.0, 0.97, 0.9) * WaterSpecTint()
                      + float3(0.85, 0.9, 0.95) * foam
                      + float3(1.0, 0.97, 0.85) * pierce
                      + float3(0.85, 0.92, 1.0) * WaterUnderShimmer(S.V, N, tuv1, time, below);

    // Reflection lobe: raw style fresnel, TIR curve from below, and zeroed at
    // the junction where ANOTHER surface sits in front of this pixel (so the
    // waterline stays pixel-identical to the scene behind — the old seam fix).
    float F = (below > 0.5) ? WaterBelowOpacity(NdotV) : reflAmt;
    F *= RefractGate(thickness) * saturate(1.0 - frontFade * lerp(0.85, 0.5, below));

    // Transmission lobe: how much water is along the view path (defect 2:
    // steep/noon views clear, long grazing paths opaque, deep water at the
    // authored timecyc alpha). Body-contract opacity multiplier = 1.0 until
    // W7-B data arrives; applied identically to the opaque AND blend tier
    // paths (this one value feeds both -> tier parity preserved).
    float bodyT = saturate(WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below)
                           * B.opac);

    // Legacy composite (INI waterFresnelSplit = 0): the pre-split
    // lerp(scene, surf, bodyOpacity) form, kept as a per-user rollback.
    float3 legacySurf = waterCol * (1.0 - reflAmt) + skyReflect * reflAmt + highlights;
    float legacyOp = saturate(deepColor.w * specParams.z * lerp(0.35, 1.0, deepFactor)
                              * shoreFade * S.vtxA) * saturate(1.0 - frontFade * 0.85);
    if(below > 0.5)
        legacyOp = saturate(WaterBelowOpacity(NdotV) * saturate(1.0 - frontFade * 0.5));

    if(tier >= 2){
        float2 refractOffset = N.xy * clipPlanes.w * RefractGate(thickness)
                             * (1.0 - 0.7 * distFactor);
        float2 refractUV = clamp(S.screenUV + refractOffset, float2(0.001, 0.001), float2(0.999, 0.999));
        float3 behind = tex2D(sceneTex, refractUV).rgb;
        if(split < 0.5)
            return float4(saturate(lerp(behind, legacySurf, legacyOp)), 1.0);
        // Reflection AND refraction in the same pixel: F of the env, the rest
        // split between the water body and the refracted scene behind it.
        return float4(saturate(WaterSplitOpaque(behind, skyReflect, waterCol,
                                                 F, bodyT, highlights)), 1.0);
    }
    // Tier 0/1: no scene/depth copies — hardware SRCALPHA blend approximates
    // the same split against the framebuffer (reflection still live, the
    // scene behind shows through the alpha instead of being fetched).
    if(split < 0.5)
        return float4(saturate(legacySurf), legacyOp);
    return WaterSplitBlend(skyReflect, waterCol, F, bodyT, highlights);
}

// ============================================================================
// main_iv — GTA IV look (water-refs.md IV section)
//   * 2 scrolling normal layers at IV scales/amps (0.002/0.0512 + 0.01/0.256
//     rescaled x10 for SA world units: 0.02 / 0.1 per metre, world XY mapping)
//   * fresnel mix = 0.2 + 0.6*(NdotV)^5 (c4=0.6/0.2 asm constants)
//   * shore alpha saturate(opacity^0.25 + 0.125), opacity = depthDiff*alpha
//   * shore coverage uses the authored IV absorption coefficient 3
//     (1 - exp2(-3 * depth * WaterColor.a); see WaterShoreCoverageK)
//   * distance normal fade 1 - dist^2*4e-4 (floored so the detail never
//     disappears completely — that was the "effect lost" defect)
// ============================================================================

float4 WaterIV(VS_OUTPUT IN)
{
    SurfBase S = WaterSetup(IN);
    int tier = (int)qualityParams.x;
    float time = timeParams.x * timeParams.y;
    float below = S.below;
    float dayF = saturate(timeLight1.w);
    float split = saturate(qualityParams.w);   // 1 = fresnel split, 0 = legacy
    BodyContract B = WaterBodyContract(IN);    // W7-B contract; all scales = 1.0 until data lands

    // ---- Water column ----
    float thickness = 0.0;
    float frontFade = 0.0;
    float deepFactor = 0.65;
    if(tier >= 2){
        float2 col = SampleWaterColumn(S.screenUV, S.viewZ, tier);
        thickness = col.x;
        frontFade = col.y;
        deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }else{
        // Tiers 0/1: zero-fetch column estimate (see WaterXbox) — the ramp
        // and the body-opacity path stay alive without the depth copy.
        thickness = saturate(S.viewZ * 0.02) * max(shallowColor.w * 2.0, 1.0);
        if(tier >= 1)
            deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }
    // IV depth absorption: authored GTA IV coefficient 3 (see
    // WaterShoreCoverageK). IV only — the Xbox/V cores keep k = 2.
    float shoreFade = WaterShoreCoverageK(thickness, specParams.w * B.shore, 3.0);
    if(tier < 2) shoreFade = 1.0;

    // ---- IV normal layers: world-space XZ mapping (SA: water plane spans
    //      world XY), layer scales 0.002/0.01 x10 rescale, amps 0.0512/0.256
    //      + mip-amplitude restore on BOTH layers ----
    float2 wuv = IN.worldPos.xy * waterParams.y;
    // G3 river flow scroll (see WaterRiverFlow) — RIVER bodies only.
    float2 riverFlow = WaterRiverFlow(IN.bodyData, time);
    float2 uv1 = wuv * 0.02 + float2( 0.011, 0.007) * time + riverFlow;
    float2 uv2 = wuv * 0.10 + float2(-0.014, 0.019) * time + riverFlow * 1.6;
    float amp1 = LayerAmpRestore(uv1);
    float amp2 = LayerAmpRestore(uv2);

    float4 t1 = tex2D(detailTex, uv1);
    float3 n1 = t1.xyz * 2.0 - 1.0;
    n1.xy *= amp1;
    float3 n2 = float3(0.0, 0.0, 0.0);
    float h2 = 0.0;
    if(tier >= 1){
        float4 t2 = tex2D(detailTex, uv2);
        n2 = t2.xyz * 2.0 - 1.0;
        n2.xy *= amp2;
        h2 = t2.w;
    }
    if(tier >= 3){
        float4 t3 = tex2D(detailTex, uv1 * 3.7 + float2(0.023, -0.017) * time);
        n2 += (t3.xyz * 2.0 - 1.0) * 0.35;
    }

    // IV: N = (dx, dy, 1.00001) with per-layer amplitudes 0.0512 / 0.256.
    // Distance normal fade: 1 - dist^2 * distFadeK (IV: 4e-4) — FLOORED at
    // 0.25: the raw curve hit zero at ~50 m and both layers vanished together
    // ("the effect is lost" well inside the view).
    float nFade = 0.25 + 0.75 * saturate(1.0 - S.dist * S.dist * max(qualityParams.y, 0.0));
    float2 dN = (n1.xy * 0.0512 + n2.xy * 0.256) * waterParams.x * B.norm;
    float3 N = normalize(float3(dN * nFade, 1.00001));

    // View from below: face the camera (see WaterXbox for the full note).
    if(below > 0.5)
        N = -N;

    float NdotV = max(dot(N, S.V), 0.0);
    float NdotL = max(dot(N, S.L), 0.0);
    float NdotH = max(dot(N, S.H), 0.0);

    // ---- IV fresnel: mix = 0.2 + 0.6*(NdotV)^5 — planar-style reflection,
    //      strong face-on, deliberately DImmed at grazing (anti-chrome).
    //      Taken alone that curve is face-on-only: at grazing it stays at
    //      ~0.11 and the split would hand those angles entirely to the
    //      transmission lobe ("reflection missing from above at most angles").
    //      Floor it with the physical Schlick curve so grazing still reflects —
    //      the cap (waterParams.w < 1) still guarantees never-chrome. ----
    float ivAmt = saturate((0.2 + 0.6 * pow(NdotV, 5.0)) * waterParams.w);
    float ivSchlick = 0.02 + 0.98 * pow(saturate(1.0 - NdotV), 5.0);
    float reflAmt = saturate(max(ivAmt, ivSchlick * waterParams.w));
    // Reflection mix weight (c16.w — see WaterReflMix): moderate global
    // reduction, default 0.7 (user: "water has sometimes big reflections";
    // IV's authored cap 0.88 -> ~0.62 effective).
    reflAmt *= WaterReflMix(1.0);
    float3 skyReflect = SkyReflection(N, S.V, S.distFactor, below);

    // ---- Water body colour: depth ramp + gentle far blend + timecycle light --
    float3 waterCol = lerp(shallowColor.rgb, deepColor.rgb, deepFactor);
    waterCol = FarBlendWater(waterCol, S.distFactor);
    waterCol = WaterBodyLight(waterCol, NdotL);

    // ---- Sun specular (Blinn, preset power from the IV constant set) ----
    float specPow = max(specParams.x, 8.0);
    float spec = pow(NdotH, specPow) * specParams.y;

    // ---- Foam band (tier >= 2) ----
    float foam = 0.0;
    if(tier >= 2){
        float h = WaterFoamOctaves(uv1, (t1.w + h2) * 0.5);
        float band = smoothstep(0.02, 0.02 + max(timeParams.w, 0.01), shoreFade)
                   * (1.0 - smoothstep(max(timeParams.z, 0.05) * 0.5, max(timeParams.z, 0.1), shoreFade));
        // M2+M3: same c16-gated RAGE texture blend + Cry softIsec feather as
        // the Xbox core (all-zero c16.x/y/z -> bit-identical pre-M2/M3 line).
        // flatNLen = the damped IV gradient XY (dN * nFade, pre-normalize).
        float procFoam = smoothstep(0.55, 0.85, h);
        float texFoam = WaterFoamTexture(uv1 * 0.31 + float2(0.004, -0.003) * time,
                                         length(dN * nFade)) * saturate(waterFxParams.x);
        float isec = WaterSoftIsec(thickness, specParams.w * B.shore);
        float pattern = saturate(procFoam + texFoam * saturate(waterFxParams.y) * 0.75);
        foam = (band * pattern
              + isec * saturate(waterFxParams.z) * pattern * (1.0 - band) * 0.5)
              * 0.25 * B.foam * WaterFoamGate();
    }

    float pierce = 0.0;
    if(tier >= 3 && below < 0.5)
        pierce = SunPierce(S.V, N, S.L, thickness) * qualityParams.z * dayF;

    // ---- Composite (fresnel split — see the block comment by WaterSplitOpaque)
    float3 highlights = spec * float3(1.0, 0.97, 0.9) * WaterSpecTint()
                      + float3(0.85, 0.9, 0.95) * foam
                      + float3(1.0, 0.97, 0.85) * pierce
                      + float3(0.85, 0.92, 1.0) * WaterUnderShimmer(S.V, N, uv1, time, below);

    float F = (below > 0.5) ? WaterBelowOpacity(NdotV) : reflAmt;
    F *= RefractGate(thickness) * saturate(1.0 - frontFade * lerp(0.85, 0.5, below));
    float bodyT = saturate(WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below)
                           * B.opac);

    // ---- G1 screen-space reflection (IV core): blend the flipped scene
    //      capture over the analytic sky tint by F before any composite
    //      consumes skyReflect. c7.w == 0 -> bit-identical analytic path.
    //      Uniform tier gate = the same precedent as the other tier-gated
    //      fetches (tier < 2 never has the RT bound, flag 0 anyway).
    if(tier >= 2)
        skyReflect = WaterSSRBlend(skyReflect, N, S.screenUV, F, below,
                                   S.distFactor);

    // Legacy composite (INI waterFresnelSplit = 0) — IV's own
    // depthDiff x alpha / shore alpha = opacity^0.25 + 0.125 curve.
    float3 legacySurf = waterCol * (1.0 - reflAmt) + skyReflect * reflAmt + highlights;
    float lop = deepFactor * deepColor.w * specParams.z;
    float legacyOp = saturate(pow(saturate(lop), 0.25) + 0.125);
    legacyOp = saturate(legacyOp * shoreFade * S.vtxA * saturate(1.0 - frontFade * 0.85));
    if(below > 0.5)
        legacyOp = saturate(WaterBelowOpacity(NdotV) * S.vtxA * saturate(1.0 - frontFade * 0.5));

    if(tier >= 2){
        float2 refractOffset = N.xy * clipPlanes.w * RefractGate(thickness)
                             * (1.0 - 0.7 * S.distFactor);
        float2 refractUV = clamp(S.screenUV + refractOffset, float2(0.001, 0.001), float2(0.999, 0.999));
        float3 behind = tex2D(sceneTex, refractUV).rgb;
        if(split < 0.5)
            return float4(saturate(lerp(behind, legacySurf, legacyOp)), 1.0);
        return float4(saturate(WaterSplitOpaque(behind, skyReflect, waterCol,
                                                 F, bodyT, highlights)), 1.0);
    }
    if(split < 0.5)
        return float4(saturate(legacySurf), legacyOp);
    return WaterSplitBlend(skyReflect, waterCol, F, bodyT, highlights);
}

// ============================================================================
// main_v — GTA V / RAGE look (water-refs.md V section)
//   * damped reflection normal lerp(N, (0,0,1), 0.5) (anti-chrome; P2: was 5/6)
//   * fresnel = lerp(NdotV, 1, 0.3) (flattened curve; P2: reflStr x0.7 in-shader)
//   * exp depth colour blend (2*WaterColor*refr <-> refr), e = RefractionExponent 0.25
//   * cell-mask pow(...,32) anti-tiling
//   * foam band FoamMask*(len(flatN.xy)*0.27+0.44), foamWeight 0.65
//   * Blinn spec power ~1118 (watertune SpecularFalloff) low intensity
//   * sun pierce (tier 3), fine layer x3.7 + crossed low-freq layers (tier 3)
// ============================================================================

float4 WaterV(VS_OUTPUT IN)
{
    SurfBase S = WaterSetup(IN);
    int tier = (int)qualityParams.x;
    float time = timeParams.x * timeParams.y;
    float below = S.below;
    float dayF = saturate(timeLight1.w);
    float split = saturate(qualityParams.w);   // 1 = fresnel split, 0 = legacy
    BodyContract B = WaterBodyContract(IN);    // W7-B contract; all scales = 1.0 until data lands

    // ---- Water column ----
    float thickness = 0.0;
    float frontFade = 0.0;
    float deepFactor = 0.65;
    if(tier >= 2){
        float2 col = SampleWaterColumn(S.screenUV, S.viewZ, tier);
        thickness = col.x;
        frontFade = col.y;
        deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }else{
        // Tiers 0/1: zero-fetch column estimate (see WaterXbox) — the ramp
        // and the body-opacity path stay alive without the depth copy.
        thickness = saturate(S.viewZ * 0.02) * max(shallowColor.w * 2.0, 1.0);
        if(tier >= 1)
            deepFactor = smoothstep(0.0, 1.0, thickness / max(shallowColor.w, 1e-3));
    }
    // LR smooth shorelines: exponential coverage (zero slope at the
    // waterline); the body contract tightens/loosens the band per body
    // (1.0 = vanilla range until W7-B data arrives).
    float shoreFade = WaterShoreCoverage(thickness, specParams.w * B.shore);
    if(tier < 2) shoreFade = 1.0;

    // ---- GetOceanBump stack (V recipe): 2 main taps at worldXZ*scale +
    //      crossed low-freq layers scrolling at time/10 + fine layer x3.7.
    //      One fetch per layer, no dynamic loops, mip-amplitude restore on
    //      both live layers. ----
    float2 wuv = IN.worldPos.xy * waterParams.y * 0.02;
    // G3 river flow scroll (see WaterRiverFlow) — RIVER bodies only.
    float2 riverFlow = WaterRiverFlow(IN.bodyData, time);
    float2 uv1 = wuv + float2( 0.013, -0.009) * time + riverFlow;
    float2 uv2 = wuv * 1.73 + float2(-0.017, 0.011) * time + riverFlow * 1.6;
    float amp1 = LayerAmpRestore(uv1);
    float amp2 = LayerAmpRestore(uv2);

    float4 t1 = tex2D(detailTex, uv1);
    float3 n1 = t1.xyz * 2.0 - 1.0;
    n1.xy *= amp1;
    float3 n2 = float3(0.0, 0.0, 0.0);
    float h2 = 0.0;
    if(tier >= 1){
        float4 t2 = tex2D(detailTex, uv2);
        n2 = t2.xyz * 2.0 - 1.0;
        n2.xy *= amp2;
        h2 = t2.w;
    }
    if(tier >= 3){
        // Fine detail layer at x3.7 + the two crossed low-freq swell layers
        // (V /448 and /512 recipe, rescaled to SA world units).
        float4 t3 = tex2D(detailTex, uv1 * 3.7 + float2(0.019, 0.015) * time);
        n2 += (t3.xyz * 2.0 - 1.0) * 0.35;
        float4 t4 = tex2D(detailTex, wuv * 0.1 + float2( 0.025,  0.020) * time * 0.1);
        float4 t5 = tex2D(detailTex, wuv * 0.088 + float2(-0.020, 0.025) * time * 0.1);
        n1 += (t4.xyz * 2.0 - 1.0) * 0.30;
        n2 += (t5.xyz * 2.0 - 1.0) * 0.30;
    }

    // Cell-mask anti-tiling: pow(...,32) of the height channel produces a
    // sparse mask that breaks the normal-map repeat pattern.
    float cellMask = pow(saturate(t1.w), 32.0);

    // Distance damping 0.55 (was 0.85): 0.85 left ~15% of the normal detail at
    // range and the wave field died into a flat sheet (flat-cyan defect).
    float strength = waterParams.x * B.norm * (1.0 - 0.55 * S.distFactor);
    float2 dN = (n1.xy + n2.xy * 0.7) * strength * (0.75 + 0.5 * cellMask);
    float3 N = normalize(float3(dN, 1.0));

    // View from below: face the camera (see WaterXbox for the full note).
    if(below > 0.5)
        N = -N;

    float NdotV = max(dot(N, S.V), 0.0);
    float NdotL = max(dot(N, S.L), 0.0);
    float NdotH = max(dot(N, S.H), 0.0);

    // ---- Anti-chrome reflection: damped reflection normal lerp(N,(0,0,1),0.5)
    //      (P2: was 5/6) and flattened fresnel lerp(NdotV, 1, 0.3). From below
    //      the flat reference must be the camera-facing side too, or the
    //      damped normal points away from the viewer and the reflection goes
    //      dead. ----
    float3 flatN = below > 0.5 ? float3(0.0, 0.0, -1.0) : float3(0.0, 0.0, 1.0);
    // P2 flat-cyan fix: 5/6 flattened the reflection normal by 83%, so wave
    // detail died inside the reflection lobe (flat cyan). 0.5 keeps half the
    // amplitude live. NaN-safe: N is unit with |N.z| > 0 on the flatN side,
    // so lerp(N, flatN, 0.5) has length >= sqrt(0.5) — normalize never sees
    // a zero vector.
    float3 Nrefl = normalize(lerp(N, flatN, 0.5));
    float3 skyReflect = SkyReflection(Nrefl, S.V, S.distFactor, below);
    // Flattened fresnel lerp(NdotV, 1, 0.3) is V's face-on-biased curve: at
    // grazing it only reaches ~0.17 and the split would hand those angles to
    // transmission ("reflection missing from above"), so floor it with the
    // physical Schlick curve — the cap keeps it under 1 (anti-chrome).
    // P2 flat-cyan fix: the V preset feeds reflStr = 0.85 (0.55 x 1.55 via
    // waterPipe c1.w) and vAmt read ~0.55 typical -> the surface was ~55%
    // uniform analytic sky tint + 45% transmit = flat cyan, no reflection
    // variation. Scaled INSIDE the shader (CPU constants untouched, c1.w
    // still carries the raw preset): effective reflStr ~0.6 (0.85 x 0.7).
    // Both the V curve and the Schlick floor use the same scaled strength so
    // the anti-chrome cap structure is preserved (never >= 1).
    // Reflection mix weight (c16.w — INI waterReflectionMix, file-static
    // default 0.7 in waterPipe.cpp): the P2 hardcoded 0.7 in-shader scale,
    // now INI-tunable. Default 0.7 = behaviour unchanged (0.85 -> ~0.6
    // effective, the target range for the "big reflections" complaint);
    // 0/unset (older pipe binary) = the P2 0.7 exactly.
    float vReflStr = saturate(waterParams.w * WaterReflMix(0.7));
    float vAmt = saturate(lerp(NdotV, 1.0, 0.3) * vReflStr);
    float vSchlick = 0.02 + 0.98 * pow(saturate(1.0 - NdotV), 5.0);
    float reflAmt = saturate(max(vAmt, vSchlick * vReflStr));

    // ---- Water body colour: depth ramp + gentle far blend + timecycle light --
    float3 waterCol = lerp(shallowColor.rgb, deepColor.rgb, deepFactor);
    waterCol = FarBlendWater(waterCol, S.distFactor);
    waterCol = WaterBodyLight(waterCol, NdotL);

    // ---- Blinn specular (watertune SpecularFalloff ~1118, low intensity,
    //      never wide GGX = chrome) + V gloss breakup (water.fx
    //      tex(gloss, TC*0.05)*0.7+0.3): a very large-tile fetch of the
    //      detail texture stands in for the missing gloss sampler and
    //      modulates the highlight so it cannot stamp the normal-map
    //      repeat onto the sheet. Uniform-tier-gated tap (tier >= 1), same
    //      precedent as the other tier-gated fetches. ----
    float specPow = max(specParams.x * (1.0 - 0.85 * S.distFactor), 8.0);
    float spec = pow(NdotH, specPow) * specParams.y * (1.0 - 0.6 * S.distFactor);
    if(tier >= 1){
        float gloss = tex2D(detailTex, uv1 * 0.05).x * 0.7 + 0.3;
        spec *= gloss;
    }

    // ---- Foam band: FoamMask*(len(flatN.xy)*0.27 + 0.44), foamWeight 0.65,
    //      FoamMask = saturate((512-depth)/512) shore depth gate. LR
    //      multi-octave height breakup (2x/4x/8x) + the cell-mask
    //      pow(...,32) anti-tiling so the foam crests ride the sparse
    //      wavelet mask instead of stamping the normal-map repeat.
    //      Body-contract foam scale on top (1.0 until W7-B data arrives). ----
    float foam = 0.0;
    if(tier >= 2){
        float h = WaterFoamOctaves(uv1, (t1.w + h2) * 0.5);
        float foamMask = saturate((512.0 - thickness * 100.0) / 512.0);
        float foamBand = foamMask * (length(dN) * 0.27 + 0.44) * 0.65;
        float foamTex = smoothstep(0.45, 0.8, h) * (0.6 + 0.4 * saturate(cellMask * 4.0));
        // M2+M3: RAGE foam-texture blend (s4, c16.x/y-gated) + Cry softIsec
        // shallow-intersection feather (c16.z-gated). waterFxParams.x/y/z
        // all 0 -> the two added terms are exactly 0 -> the pre-M2/M3
        // algebra below is bit-identical. flatNLen = length(dN) (V's own
        // damped flat gradient — already the RAGE flatNormal.xy analogue).
        float texFoam = WaterFoamTexture(uv1 * 0.31 + float2(0.004, -0.003) * time,
                                         length(dN)) * saturate(waterFxParams.x);
        float isec = WaterSoftIsec(thickness, specParams.w * B.shore);
        foam = foamBand * foamTex * shoreFade
             + texFoam * saturate(waterFxParams.y) * foamBand * shoreFade * 0.75
             + isec * saturate(waterFxParams.z) * foamTex * (1.0 - shoreFade) * 0.35;
        foam *= B.foam * WaterFoamGate();
    }

    float pierce = 0.0;
    if(tier >= 3 && below < 0.5)
        pierce = SunPierce(S.V, N, S.L, thickness) * qualityParams.z * dayF;

    // ---- 2-lobe sun glitter (V style; water-refs.md CryEngine SunSpecular
    //      port). ALU only, no fetches; night-gated by dayF and WaterFoamGate
    //      like every other white additive term (never-regress doctrine).
    //      Body-contract glitter scale = 1.0 until W7-B data arrives. ----
    float glitter = 0.0;
    if(tier >= 1 && below < 0.5)
        glitter = WaterGlitter(S.V, N, S.L, specPow) * specParams.y * 0.35
                * dayF * WaterFoamGate() * B.glitter;

    // ---- Composite: fresnel split (V's exp depth colour blend becomes the
    //      BODY colour of the transmission lobe — see WaterSplitOpaque) ----
    float3 highlights = spec * float3(1.0, 0.97, 0.9) * WaterSpecTint()
                      + float3(0.85, 0.9, 0.95) * foam
                      + float3(1.0, 0.97, 0.85) * pierce
                      + float3(1.0, 0.98, 0.92) * glitter
                      + float3(0.85, 0.92, 1.0) * WaterUnderShimmer(S.V, N, uv1, time, below);

    float F = (below > 0.5) ? WaterBelowOpacity(NdotV) : reflAmt;
    F *= RefractGate(thickness) * saturate(1.0 - frontFade * lerp(0.85, 0.5, below));
    float bodyT = saturate(WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below)
                           * B.opac);

    // ---- G1 screen-space reflection (V core): the flipped scene capture is
    //      sampled along V's OWN damped anti-chrome normal (Nrefl — the
    //      5/6 flat lerp) and blended over the analytic sky tint by F.
    //      c7.w == 0 -> bit-identical analytic path.
    if(tier >= 2)
        skyReflect = WaterSSRBlend(skyReflect, Nrefl, S.screenUV, F, below,
                                   S.distFactor);

    // Legacy composite (INI waterFresnelSplit = 0).
    float3 legacySurf = waterCol * (1.0 - reflAmt) + skyReflect * reflAmt + highlights;
    float legacyOp = saturate(deepColor.w * specParams.z
                              * lerp(0.35, 1.0, deepFactor) * S.vtxA);
    if(below > 0.5)
        legacyOp = saturate(WaterBelowOpacity(NdotV) * S.vtxA);

    // ---- V translucency: exp depth colour blend ----
    //      depthBlend = exp(float2(-20, -60*|V.z|) * WaterColor.a * depth * e)
    //      litRefr = lerp(2*WaterColor*refr, refr, depthBlend.y)
    //      water   = lerp(waterCol, litRefr, depthBlend.x)
    if(tier >= 2){
        float2 refractOffset = N.xy * clipPlanes.w * RefractGate(thickness)
                             * (1.0 - 0.7 * S.distFactor);
        float2 refractUV = clamp(S.screenUV + refractOffset, float2(0.001, 0.001), float2(0.999, 0.999));
        float3 refr = tex2D(sceneTex, refractUV).rgb;

        float2 depthBlend = exp(float2(-20.0, -60.0 * abs(S.V.z))
                                * deepColor.w * thickness * 0.25);
        float3 litRefr = lerp(2.0 * waterCol * refr, refr, depthBlend.y);
        float3 baseCol = lerp(waterCol, litRefr, depthBlend.x);

        if(split < 0.5){
            float3 surfV = baseCol * (1.0 - reflAmt)
                         + skyReflect * reflAmt + highlights;
            // Junction blend: at the waterline the pixel reproduces the scene
            // behind it (refractOffset -> 0 there, so refr = the exact scene).
            float junction = saturate(shoreFade * (1.0 - frontFade * 0.85));
            // From below the sheet is see-through face-on / a mirror at grazing —
            // the depth-driven junction would paint the underside solid.
            if(below > 0.5)
                junction = saturate(WaterBelowOpacity(NdotV) * (1.0 - frontFade * 0.5));
            return float4(saturate(lerp(refr, surfV, junction)), 1.0);
        }
        return float4(saturate(WaterSplitOpaque(refr, skyReflect, baseCol,
                                                 F, bodyT, highlights)), 1.0);
    }

    if(split < 0.5)
        return float4(saturate(legacySurf), legacyOp);
    return WaterSplitBlend(skyReflect, waterCol, F, bodyT, highlights);
}

// ---- Entry points (one CSO per style, multi_entry compilation) ----
float4 main_xbox(VS_OUTPUT IN) : COLOR { return WaterXbox(IN); }
float4 main_iv(VS_OUTPUT IN)   : COLOR { return WaterIV(IN); }
float4 main_v(VS_OUTPUT IN)    : COLOR { return WaterV(IN); }

// Default entry kept as an alias of the Xbox core for standalone compiles.
float4 main(VS_OUTPUT IN) : COLOR { return WaterXbox(IN); }
