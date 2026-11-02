// ============================================================================
// CloudsPS.hlsl - universal-noise cloud layer + full-screen sky (ps_3_0/SM3.0)
// ----------------------------------------------------------------------------
// ENTRY POINTS
//   float4 main_clouds    (float2 uv : TEXCOORD0, float4 vcol : COLOR0,
//                          float4 vpos : VPOS) : COLOR0
//       Cloud SPRITE-BATCH rasteriser (audit 2026-10-25-clouds-ffp-audit.md
//       §4.3/§6.2): bound over the vanilla CClouds buffered flushes. The RW
//       Im2D vertex (FVF 0x144 = XYZRHW|DIFFUSE|TEX1) carries the CPU-baked
//       sprite colour in vcol and the per-sprite atlas sub-rect in uv; s0 is
//       the texture the batch already bound (READ-ONLY, never rebound here:
//       batches #1/#3 use the cloud atlas, #2 the shared sprite texture).
//       vpos (VPOS, rasteriser-generated — no vertex-stream change) anchors
//       the legacy grain band in screen space, matching the full-screen
//       legacy grain raster. Output is premultiplied for the ONE/ONE batch
//       (and correct for ONE/INVSRCALPHA). Do NOT pair with SRCALPHA/ONE
//       (alpha twice).
//       Background safety under ONE/ONE comes from the premultiply, for BOTH
//       authoring conventions:
//         * cloudmasked / cloudhigh (A8R8G8B8, white RGB, alpha 0):
//           background alpha 0 -> a = 0 -> premultiplied RGB = 0.
//         * cloud1 / XLUS-style (black RGB background, alpha 1):
//           background RGB = 0 -> premultiplied RGB = 0.
//       drive == 0 (no c49/c51 upload): EXACT fixed-function MODULATE
//       emulation — rgb = tex.rgb * vcol.rgb, a = tex.a * vcol.a. The batch
//       renders as vanilla, so binding the shader is safe BEFORE any knob
//       upload exists (the old flat 0.30 fallback painted every sprite as a
//       flat grey quad — the "messed up flat quads" failure mode).
//       drive == 1: the shared-noise erosion field (CloudMask) modulates the
//       sprite, breaking up the flat quad edges/seams; c51.w is the blend
//       amount (0 = vanilla sprite untouched).
//
//   float4 main_cloudsSky (float2 uv : TEXCOORD0, float4 vpos : VPOS) : COLOR0
//       Full-screen sky: vertical gradient + the same cloud field projected
//       onto a virtual cloud plane by view elevation derived from uv (uv.y=0
//       is screen top = zenith, uv.y=1 is the horizon). Coverage grows toward
//       the horizon and is faded right at it. Alpha is a near-opaque floor
//       (0.55..1.0). ONE/ONE adds it as an additive sky layer; as a
//       premultiplied replacement it takes ONE/INVSRCALPHA (NOT SRCALPHA/ONE,
//       which would apply the opacity twice). RGB is premultiplied by alpha.
//       This entry does NOT sample s0: there is no source cloud texture to
//       un-premultiply on a full-screen layer, and its alpha floor is the sky
//       fill, not a cloud mask (contrast main_clouds, where mask == 0 means
//       RGB == 0).
//
//   float4 main (float2 uv : TEXCOORD0, float4 vcol : COLOR0) : COLOR0
//       Thin alias of main_clouds so the current single-entry rule in
//       tools/fast_build.py (/E main for every shaders/ps/*.hlsl) still emits
//       resources/cso/CloudsPS.cso with NO build-script edit. FXC dead-strips
//       the other entries per build; each entry is also compilable directly.
//
// REGISTER BUDGET - s0 + s9 + c49..c52 ONLY (composes with every other consumer)
//   s0   cloudSrcTex    source cloud/sprite texture, READ-ONLY (bound by the
//                       batch; never rebound here). Sampled by main_clouds.
//   s9   uniNoiseTex     .r Perlin / .g Worley / .b white / .a blue, WRAP
//   c49  { noiseScale, worleyScale, coverage, erosion }
//   c50  { whiteScale, blueScale, timeSeconds, frameIndex }
//   c51  { cloudScaleA, cloudScaleB, cloudScaleC, cloudDensity }
//   c52  { jitterAmount, ditherAmount, detailStrength, tileTexelSize }
//   No c0-c4, no extra noise samplers. Time comes from c50.z, the grain crawl
//   from c50.w, so no new upload. The s9/c49-c52 contract is unchanged; s0 is
//   the pass's already-bound texture, not a new upload. vcol (COLOR0) is a
//   vertex interpolator from the batch's FVF 0x144, not a constant — no new
//   upload either. c52.w is the Noise.hlsl tile-resolution signal
//   (SkyNoise::UniversalSize(); 0 = no signal — see NoiseTileOK).
//
// NOISE KNOBS -> CLOUD LOOK (semantics this shader assigns to the block)
//   c51.x  body scale        bigger = smaller macro cells
//   c51.y  Worley carve ratio (relative to body scale)
//   c51.z  coverage softness (width of the smoothstep edge)
//   c51.w  erosion blend amount on the sprite path; 0 = vanilla sprite
//          untouched, 1 = fully field-driven
//   c49.z  coverage threshold (soft-threshold centre)
//   c49.w  erosion: how far Worley eats into the Perlin body (high band) AND
//          how far the legacy grain eats into the vanilla sprite (low band —
//          same depth knob, different eroder, see TWO-BAND COMPOSITION)
//   c52.y  blue-noise dither amount on the final alpha (0 = off)
//
// TIMECYCLE CONTRACT (vcol is the carrier — CPU-baked, shader-honoured)
//   The vanilla CClouds::Render bakes the timecycle cloud values into the
//   FVF 0x144 DIFFUSE (vcol) BEFORE this shader runs:
//     * low-cloud batch (cloud1): RGB = CTimeCycle::m_CurrentColours
//       lowCloudsR/G/B, alpha from the weather-driven coverage fade.
//     * high-cloud batches (cloudmasked/cloudhigh): RGB from the sky
//       top/bottom family, alpha from coverage.
//   This shader NEVER overrides vcol with its own palette on the sprite path:
//   baseRGB = tex.rgb * vcol.rgb and baseA = tex.a * vcol.a are the exact FFP
//   MODULATE terms, so timecycle colour/coverage flow through vcol into the
//   output, and the erosion bands only ever SCALE the premultiplied magnitude
//   (pmRGB *= cov, pmA *= cov) — the colour/opacity RATIO the timecycle baked
//   is preserved. The fixed CLOUD_COOL/CLOUD_WARM palette below is used by the
//   sky entry ONLY (which has no vcol and no source texture).
//   NOT carried by vcol: the field's coverage THRESHOLD (c49.z) and erosion
//   depth (c49.w) are CPU-side knobs — the main.cpp cloud upload should derive
//   them from CWeather__CloudCoverage (0xC81304) / the m_CurrentColours cloud
//   family so weather drives the field too (reported hunk, main.cpp lane).
//
// 3-SCALE "INFINITE CLOUD" BODY
//   NoiseFbm3() = 3 taps of the SAME tile at 1x / 3.17x / 9.41x. That replaces
//   a 100+ ALU octave stack with 3 fetches and ~6 ALU. Erosion is ONE extra
//   Worley tap at the header's frequency offset, and dither ONE blue tap:
//   5 noise fetches + 1 source-texture fetch per pixel (sprite entry only),
//   no loops, no dynamic branches around tex2D.
//
// TWO-BAND COMPOSITION (SkyNoise high band + legacy grain low band)
//   The field and the legacy grain are two bands of ONE chain, blended by
//   NoiseTileOK() (tile presence x resolution):
//     tileOK == 1  high band only: Perlin fBm body + Worley erosion (SkyNoise),
//                  blue-noise dither on top — the audited live behaviour,
//                  bit-identical to the pre-rechain output.
//     tileOK == 0  low band only: the vanilla sprite is eroded by the LEGACY
//                  grain (NoiseLegacyGrain — the CPostEffects::Grain_PS2
//                  VU-LFSR raster look, ALU-only so it needs no tile) with the
//                  SAME c49.w erosion depth the Worley tap uses in the high
//                  band. One knob, two regimes — the grain takes the Worley
//                  eroder's seat when there is no tile to serve it.
//     0 < tileOK < 1 (low-res 32px tile): both bands compose 50/50 — grain
//                  guides the low band, SkyNoise the high band.
//   This replaces the old failure mode where an uploaded knob block with a
//   dead/blank s9 collapsed the field to mask == 0 and dimmed EVERY sprite to
//   a flat (1 - densityK) grey. The degraded regime now degrades to grain,
//   never to flat.
//
// GRACEFUL DEGRADATION (s9 unbound + c49..c52 all zero)
//   Unbound s9 reads 0; every scale is max(x, NOISE_EPS) so nothing divides
//   by zero. The sprite entry's "drive" term (sum of |c49|+|c51|) selects the
//   output: at drive 0 the batch is an EXACT vanilla MODULATE pass-through
//   (tex.rgb * vcol.rgb premultiplied by tex.a * vcol.a) — no flat fallback
//   quads, no palette dependency, and the s0 texture the batch bound is the
//   intended input, not a hazard. The sky entry (main_cloudsSky) keeps its
//   own 0.55 gradient floor and never samples s0; its degraded regime fills
//   sparse grain wisps instead of an empty gradient.
//
// SM3.0 RULES: tex2D only, no tex2Dlod, no loops, no bitwise ops, no integer
// math; every divide/normalise guarded with >= 1e-7 (NaN writes black).
// ============================================================================

#include "../include/Noise.hlsl"

#define CLOUDS_EPS NOISE_EPS

// Source cloud texture for the quad layer. READ-ONLY: the cloud/sprite path
// already has the current cloud texture bound at s0 and must not rebind it.
// Declared outside the noise block, so s9/c49-c52 keep their contract.
sampler2D cloudSrcTex : register(s0);

// ---------------------------------------------------------------------------
// Authored-knob presence. All zero (nothing uploaded) => 0 => vanilla
// pass-through on the sprite entry. Any nonzero knob => 1 => the procedural
// erosion field drives the output.
// abs()+dp4 keeps this to a handful of slots; time (c50.z) is deliberately
// excluded so a shader running at t=0 is not mistaken for "unconfigured".
// ---------------------------------------------------------------------------
float CloudKnobDrive()
{
    // c52 is dither/jitter only (its zero case is already a no-op), so the
    // field is "configured" iff c49 or c51 was uploaded. Saves 2 slots.
    float4 k = abs(uniNoiseParams0) + abs(uniNoiseParams2);
    return saturate(dot(k, float4(1.0, 1.0, 1.0, 1.0)));
}

// Fixed key light, pre-normalised: no normalize() call, no constant needed.
// (-0.352, 0.554, 0.755) has length ~1.0.
static const float3 CLOUD_LIGHT = float3(-0.352, 0.554, 0.755);

// Warm/cool cloud palette (fixed: no sun/timecycle constant in this budget).
static const float3 CLOUD_COOL = float3(0.58, 0.66, 0.80);
static const float3 CLOUD_WARM = float3(1.00, 0.95, 0.88);

// ---------------------------------------------------------------------------
// Shared cloud field: body fBm - Worley erosion -> smoothstep coverage mask.
// Returns the raw shaped mask in [0,1]; callers apply any flat fallback.
// `uv` is already in cloud-plane space.
// ---------------------------------------------------------------------------
float CloudMask(float2 uv, float coverageFloor)
{
    float coverage  = saturate(uniNoiseParams0.z);
    float erosion   = saturate(uniNoiseParams0.w);
    float bodyScale = max(uniNoiseParams2.x, CLOUDS_EPS);
    float carveFreq = max(uniNoiseParams2.y, CLOUDS_EPS);
    float edgeSoft  = max(uniNoiseParams2.z, 0.02);
    float densityK  = saturate(uniNoiseParams2.w);

    // 3-fetch infinite fBm body + 1 Worley tap for cauliflower erosion.
    float body  = NoiseFbm3(uv, bodyScale);
    float cells = NoiseWorley(uv * bodyScale * carveFreq + 0.19);
    float dens  = saturate(body - cells * erosion);

    // Soft threshold. coverageFloor lifts coverage toward the horizon in the
    // sky variant (pass 0 for the flat layer).
    float cov = saturate(coverage + coverageFloor);
    return smoothstep(cov, cov + edgeSoft, dens) * densityK;
}

// Shading used by the sky entry: derivative pseudo-normal + fixed key light.
// The caller passes the mask; the returned RGB is then premultiplied by the
// caller's opacity. (The sprite entry keeps the vanilla texture colour instead
// — see main_clouds.)
float3 CloudColor(float mask)
{
    // z bias keeps a flat mask normalisable to (0,0,1): no zero-length divide.
    float3 nrm = float3(-ddx(mask), ddy(mask), 0.25);
    nrm *= rsqrt(max(dot(nrm, nrm), CLOUDS_EPS));

    float facing = saturate(dot(nrm, CLOUD_LIGHT));
    float3 col   = lerp(CLOUD_COOL, CLOUD_WARM, facing);
    // Dense bodies read a touch brighter than the wispy fringe.
    return col * (0.55 + 0.45 * saturate(mask)) * 0.90;
}

// ---------------------------------------------------------------------------
// main_clouds - cloud sprite-batch rasteriser (vanilla pass-through + erosion)
// uv: the batch's per-sprite atlas sub-rect. vcol: the CPU-baked sprite colour
// (FVF 0x144 DIFFUSE — the timecycle carrier, see TIMECYCLE CONTRACT). vpos:
// rasteriser-generated screen pixel (VPOS) anchoring the legacy grain band.
// s0: the texture the batch already bound.
// ---------------------------------------------------------------------------
float4 main_clouds(float2 uv : TEXCOORD0, float4 vcol : COLOR0,
                   float4 vpos : VPOS) : COLOR0
{
    float time      = uniNoiseParams1.z;              // c50.z, no new constant
    float frame     = uniNoiseParams1.w;              // c50.w, grain crawl
    float ditherAmt = saturate(uniNoiseParams3.y);    // c52.y
    float drive     = CloudKnobDrive();               // 0 = nothing uploaded

    // Source cloud/sprite texture as bound by the batch. READ-ONLY: the
    // cloud/sprite path already has the current texture at s0 and must not
    // rebind it. Unconditional fetch (SM3.0 implicit-gradient rule).
    //
    // Background safety under ONE/ONE comes from the premultiply below, for
    // BOTH authoring conventions (see header): alpha-0 backgrounds zero the
    // premultiplied RGB, black-RGB backgrounds are zero already.
    float4 tex = tex2D(cloudSrcTex, uv);

    // Vanilla-equivalent base: exact FFP MODULATE (tex * vertex colour).
    // vcol IS the timecycle connection (CPU-baked lowClouds/sky family).
    float3 baseRGB = tex.rgb * vcol.rgb;
    float  baseA   = saturate(tex.a * vcol.a);

    // Premultiplied vanilla contribution — what the ONE/ONE batch should add.
    float3 pmRGB = baseRGB * baseA;
    float  pmA   = baseA;

    // ---- HIGH band: shared SkyNoise erosion field (Noise.hlsl s9 tile) ----
    // Slow drift purely in UV space (no extra constant, no vertex work).
    // x drifts faster than y -> the layer shears instead of sliding rigidly.
    // The drift applies to the FIELD only; the atlas fetch above stays put.
    float2 drift = float2(0.0060, 0.0022) * time;
    float mask   = CloudMask(uv + drift, 0.0);

    // Blue-noise dither on the field: 6th (and last) fetch. Zero amplitude
    // makes this exactly 0, so an unconfigured layer is deterministic.
    float dither = NoiseDither(uv * 256.0, ditherAmt);
    mask = saturate(mask + dither * mask);

    // c51.w = how much the field drives the sprite (0 = vanilla untouched).
    float densityK = saturate(uniNoiseParams2.w);
    float fieldCov = lerp(1.0, mask, densityK);

    // ---- LOW band: legacy grain erosion (ALU-only, no tile needed) --------
    // The CPostEffects::Grain_PS2 raster look (NoiseLegacyGrain), anchored in
    // screen space like the legacy full-screen grain quad. The erosion DEPTH
    // is the SAME c49.w knob the Worley tap uses in the high band — one knob,
    // two regimes, so the bands are connected rather than parallel systems.
    // Zero c49.w (unconfigured) makes grainCov exactly 1: the drive==0 path
    // below stays an exact vanilla pass-through.
    float grain        = NoiseLegacyGrain(vpos.xy, frame);
    float grainErosion = saturate(uniNoiseParams0.w);   // c49.w
    float grainCov     = 1.0 - grainErosion * grain;

    // ---- compose the two bands by tile quality ----------------------------
    // NoiseTileOK() = presence probe of s9 x the c52.w resolution signal:
    //   1  -> fieldCov only (bit-identical to the pre-rechain high band)
    //   0  -> grainCov only (grain-eroded vanilla sprite — the degraded
    //         regime now lands on legacy grain instead of the old flat
    //         (1 - densityK) dim every sprite uniformly)
    //   0..1 (low-res 32px tile) -> both bands compose: grain guides the low
    //         band, SkyNoise the high band.
    float tileOK = NoiseTileOK();
    float cov    = lerp(grainCov, fieldCov, tileOK);

    // Erode ONCE: applied to the premultiplied magnitude and the declared
    // opacity together, so the colour/opacity ratio is preserved (no cov^2).
    pmRGB *= cov;
    pmA   *= cov;

    // Premultiplied additive output, correct for both source conventions and
    // both blend pairs: ONE/ONE ignores A but RGB already carries everything;
    // ONE/INVSRCALPHA divides it back out. SRCALPHA/ONE would apply the
    // opacity a second time (dark fringe) — never pair with it.
    return float4(saturate(pmRGB), saturate(pmA));
}

// ---------------------------------------------------------------------------
// main_cloudsSky - full-screen sky gradient + view-projected cloud layer
// uv: screen UV, (0,0) top-left. Top of screen = zenith, bottom = horizon.
// vpos: rasteriser-generated screen pixel (VPOS) anchoring the legacy grain
// band (same two-band compose as main_clouds).
// ---------------------------------------------------------------------------
float4 main_cloudsSky(float2 uv : TEXCOORD0, float4 vpos : VPOS) : COLOR0
{
    float time      = uniNoiseParams1.z;
    float frame     = uniNoiseParams1.w;
    float ditherAmt = saturate(uniNoiseParams3.y);

    // Elevation proxy: 1 at the zenith, 0 at the horizon. The floor (0.06)
    // is the 1e-7-style guard for the perspective divide below: it only ever
    // turns the horizon into a dense band, never a divide-by-zero.
    float up = max(1.0 - uv.y * 2.0, 0.06);

    // Perspective projection of the screen onto a virtual cloud plane:
    // x/up spreads cells apart near the horizon, 1/up pushes the plane away.
    float2 plane = float2((uv.x * 2.0 - 1.0) / up, 1.0 / up);

    // UV-space drift from c50.z; 0.11 scales the plane back into tile range.
    float2 drift = float2(0.0060, 0.0024) * time;
    float2 uvw   = plane * 0.11 + drift;

    // Coverage grows toward the horizon (longer optical path through the deck)
    // and the deck fades right at the horizon line. No knob-presence fallback
    // here: an unconfigured s9 simply yields no clouds over the sky gradient.
    float mask = CloudMask(uvw, (1.0 - up) * 0.15);
    mask *= saturate(up * 1.6);

    float dither = NoiseDither(uv * 256.0, ditherAmt);
    mask = saturate(mask + dither * mask);

    // LOW band: sparse legacy-grain wisps (grain^2 biases the uniform hash
    // low -> soft, sparse bodies) fill in where the field is absent, blended
    // out by tile quality exactly like the sprite entry. At tileOK == 1 this
    // is a no-op (max with a term the field already dominates); at tileOK
    // == 0 the degraded sky keeps grain texture instead of an empty gradient.
    float grain     = NoiseLegacyGrain(vpos.xy, frame);
    float grainMask = grain * grain * saturate(uniNoiseParams0.w);
    float tileOK    = NoiseTileOK();
    mask = lerp(max(mask, grainMask), mask, tileOK);

    // Vertical sky gradient: warm haze at the horizon, cooler blue overhead.
    float3 skyCol = lerp(float3(0.58, 0.56, 0.52), float3(0.10, 0.20, 0.42),
                         saturate(up));
    float3 col    = lerp(skyCol, CloudColor(mask), mask);

    // Near-opaque full-screen coverage. This entry never samples s0: there is
    // no source cloud texture on a full-screen layer, and this floor is the
    // sky fill, not a cloud mask. ONE/ONE adds the already premultiplied
    // gradient as a layer; ONE/INVSRCALPHA makes it a replacement. Unlike
    // main_clouds, "no cloud" (mask == 0) still contributes the sky gradient
    // at 0.55 alpha, so the fallback can never be fully black.
    float opacity = saturate(0.55 + 0.45 * mask);

    return float4(saturate(col * opacity), opacity);
}

// Thin alias: the current single-entry build rule uses /E main.
float4 main(float2 uv : TEXCOORD0, float4 vcol : COLOR0, float4 vpos : VPOS) : COLOR0
{
    return main_clouds(uv, vcol, vpos);
}
