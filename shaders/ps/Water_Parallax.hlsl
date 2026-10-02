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
//  the old hard shallow->deep step; distant reflections ride the horizon
//  colour instead of collapsing to flat grey (the murky band).
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
// c7  = (camPosX, camPosY, camPosZ, 0)
// c8  = (nearClip, farClip, depthValid, refractScale)
// c9  = (skyTopR, skyTopG, skyTopB, 0)      <- timecycle sky reflection tint
// c10 = (skyBotR, skyBotG, skyBotB, 0)      <- timecycle horizon tint/fog colour
// c11 = (qualityTier, distFadeK, sunPierceScale, fresnelSplitFlag)
// c12 = (ambientR, ambientG, ambientB, skyLuma)   <- timecycle ambient + sky
// c13 = (dirR, dirG, dirB, dayFactor 0..1)        <- timecycle directional + day
//
// s0 = scene colour (refraction capture)            [tier >= 2]
// s1 = water detail normal map (RGB=normal, A=height; mip-mapped)
// s2 = scene depth copy (raw window-space z, R32F)  [tier >= 2]

sampler2D sceneTex   : register(s0);
sampler2D detailTex  : register(s1);
sampler2D depthTex   : register(s2);

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

struct VS_OUTPUT {
    float4 position  : POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 worldPos  : TEXCOORD1;
    float3 viewDir   : TEXCOORD2;
    float3 lightDir  : TEXCOORD3;
    float4 screenPos : TEXCOORD4;
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
float3 FarBlendWater(float3 waterCol, float distFactor)
{
    float farT = smoothstep(0.40, 1.0, distFactor);
    float3 farCol = lerp(deepColor.rgb, skyBotColor.rgb, 0.22);
    return lerp(waterCol, farCol, farT * 0.85);
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
    t *= saturate(1.0 - frontFade * 0.85);          // junction: reproduce the scene
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
    float3 Nmid = normalize(lerp(N, float3(0.0, 0.0, 1.0), 0.5));
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
    float shoreFade = saturate(thickness / max(specParams.w, 1e-3));
    if(tier < 2) shoreFade = 1.0;

    // ---- Scrolling detail normals (mip-mapped texture -> mip-correct LOD
    //      from screen-space derivatives; no sub-pixel sparkle). One fetch
    //      per layer; tier 3 adds the fine x3.7 layer. Both live layers get
    //      the mip-amplitude restore so neither can fade out alone. ----
    float2 uv = IN.texCoord * waterParams.y;
    float2 flow1 = float2( 0.021, 0.013) * time;
    float2 flow2 = float2(-0.016, 0.027) * time;
    float2 tuv1 = uv * 1.00 + flow1;
    float2 tuv2 = uv * 2.37 + flow2;
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

    // Strength fades with distance -> far water goes calm/smooth.
    float strength = waterParams.x * (1.0 - 0.85 * distFactor);
    // Flat XY water plane: tangent space == world space (z up).
    float3 N = normalize(float3((n1.xy + n2.xy * 0.7) * strength, 1.0));

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
    float foam = 0.0;
    if(tier >= 2){
        float h = (t1.w + h2) * 0.5;
        float band = smoothstep(0.02, 0.02 + max(timeParams.w, 0.01), shoreFade)
                   * (1.0 - smoothstep(max(timeParams.z, 0.05) * 0.5, max(timeParams.z, 0.1), shoreFade));
        foam = band * smoothstep(0.55, 0.85, h) * 0.25 * WaterFoamGate();
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
    // authored timecyc alpha).
    float bodyT = WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below);

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
    float shoreFade = saturate(thickness / max(specParams.w, 1e-3));
    if(tier < 2) shoreFade = 1.0;

    // ---- IV normal layers: world-space XZ mapping (SA: water plane spans
    //      world XY), layer scales 0.002/0.01 x10 rescale, amps 0.0512/0.256
    //      + mip-amplitude restore on BOTH layers ----
    float2 wuv = IN.worldPos.xy * waterParams.y;
    float2 uv1 = wuv * 0.02 + float2( 0.011, 0.007) * time;
    float2 uv2 = wuv * 0.10 + float2(-0.014, 0.019) * time;
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
    float2 dN = (n1.xy * 0.0512 + n2.xy * 0.256) * waterParams.x;
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
        float h = (t1.w + h2) * 0.5;
        float band = smoothstep(0.02, 0.02 + max(timeParams.w, 0.01), shoreFade)
                   * (1.0 - smoothstep(max(timeParams.z, 0.05) * 0.5, max(timeParams.z, 0.1), shoreFade));
        foam = band * smoothstep(0.55, 0.85, h) * 0.25 * WaterFoamGate();
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
    float bodyT = WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below);

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
//   * damped reflection normal lerp(N, (0,0,1), 5/6) (anti-chrome)
//   * fresnel = lerp(NdotV, 1, 0.3) (flattened curve)
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
    float shoreFade = saturate(thickness / max(specParams.w, 1e-3));
    if(tier < 2) shoreFade = 1.0;

    // ---- GetOceanBump stack (V recipe): 2 main taps at worldXZ*scale +
    //      crossed low-freq layers scrolling at time/10 + fine layer x3.7.
    //      One fetch per layer, no dynamic loops, mip-amplitude restore on
    //      both live layers. ----
    float2 wuv = IN.worldPos.xy * waterParams.y * 0.02;
    float2 uv1 = wuv + float2( 0.013, -0.009) * time;
    float2 uv2 = wuv * 1.73 + float2(-0.017, 0.011) * time;
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

    float strength = waterParams.x * (1.0 - 0.85 * S.distFactor);
    float2 dN = (n1.xy + n2.xy * 0.7) * strength * (0.75 + 0.5 * cellMask);
    float3 N = normalize(float3(dN, 1.0));

    // View from below: face the camera (see WaterXbox for the full note).
    if(below > 0.5)
        N = -N;

    float NdotV = max(dot(N, S.V), 0.0);
    float NdotL = max(dot(N, S.L), 0.0);
    float NdotH = max(dot(N, S.H), 0.0);

    // ---- Anti-chrome reflection: damped reflection normal lerp(N,(0,0,1),5/6)
    //      and flattened fresnel lerp(NdotV, 1, 0.3). From below the flat
    //      reference must be the camera-facing side too, or the damped normal
    //      points away from the viewer and the reflection goes dead. ----
    float3 flatN = below > 0.5 ? float3(0.0, 0.0, -1.0) : float3(0.0, 0.0, 1.0);
    float3 Nrefl = normalize(lerp(N, flatN, 5.0 / 6.0));
    float3 skyReflect = SkyReflection(Nrefl, S.V, S.distFactor, below);
    // Flattened fresnel lerp(NdotV, 1, 0.3) is V's face-on-biased curve: at
    // grazing it only reaches ~0.17 and the split would hand those angles to
    // transmission ("reflection missing from above"), so floor it with the
    // physical Schlick curve — the cap keeps it under 1 (anti-chrome).
    float vAmt = saturate(lerp(NdotV, 1.0, 0.3) * waterParams.w);
    float vSchlick = 0.02 + 0.98 * pow(saturate(1.0 - NdotV), 5.0);
    float reflAmt = saturate(max(vAmt, vSchlick * waterParams.w));

    // ---- Water body colour: depth ramp + gentle far blend + timecycle light --
    float3 waterCol = lerp(shallowColor.rgb, deepColor.rgb, deepFactor);
    waterCol = FarBlendWater(waterCol, S.distFactor);
    waterCol = WaterBodyLight(waterCol, NdotL);

    // ---- Blinn specular (watertune SpecularFalloff ~1118, low intensity,
    //      never wide GGX = chrome) ----
    float specPow = max(specParams.x * (1.0 - 0.85 * S.distFactor), 8.0);
    float spec = pow(NdotH, specPow) * specParams.y * (1.0 - 0.6 * S.distFactor);

    // ---- Foam band: FoamMask*(len(flatN.xy)*0.27 + 0.44), foamWeight 0.65,
    //      FoamMask = saturate((512-depth)/512) shore depth gate ----
    float foam = 0.0;
    if(tier >= 2){
        float foamMask = saturate((512.0 - thickness * 100.0) / 512.0);
        float foamBand = foamMask * (length(dN) * 0.27 + 0.44) * 0.65;
        foam = foamBand * smoothstep(0.45, 0.8, (t1.w + h2) * 0.5) * shoreFade * WaterFoamGate();
    }

    float pierce = 0.0;
    if(tier >= 3 && below < 0.5)
        pierce = SunPierce(S.V, N, S.L, thickness) * qualityParams.z * dayF;

    // ---- Composite: fresnel split (V's exp depth colour blend becomes the
    //      BODY colour of the transmission lobe — see WaterSplitOpaque) ----
    float3 highlights = spec * float3(1.0, 0.97, 0.9) * WaterSpecTint()
                      + float3(0.85, 0.9, 0.95) * foam
                      + float3(1.0, 0.97, 0.85) * pierce
                      + float3(0.85, 0.92, 1.0) * WaterUnderShimmer(S.V, N, uv1, time, below);

    float F = (below > 0.5) ? WaterBelowOpacity(NdotV) : reflAmt;
    F *= RefractGate(thickness) * saturate(1.0 - frontFade * lerp(0.85, 0.5, below));
    float bodyT = WaterBodyOpacity(thickness, NdotV, frontFade, S.vtxA, below);

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
