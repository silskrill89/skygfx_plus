// Water_VS.hlsl - Flat water plane vertex shader (vs_3_0)
//
// GEOMETRY POLICY (reconciled W1, 2026-10-30; W2 wind-driven swell 2026-11-02):
// the plane stays FLAT by default and carries a GATED vertical-only swell
// (c11, see below) — the b7396c4 "never displaced" rewrite existed because
// the old a4e81f8 Gerstner VS displaced up to ~2.4 m and popped through the
// coast. W2 raises the swell total to ~0.54 m (was 0.16 m — sub-pixel at SA
// camera distances, which read as "the wave VS isn't working") and adds the
// WIND DRIVE: the same CWeather__Wind / CWeather__WindDir that sways the
// trees (treeWind.cpp) now steers the sea —
//   * direction bias: both wave trains rotate toward the wind direction,
//     strength-weighted (calm wind = the hardcoded classic dirs, bit-exact),
//   * amplitude: (0.55 + 0.45*wind) scale, so calm seas are gentler and
//     windy seas run the full 0.54 m,
//   * SkyNoise gust: the universal packed tile's Perlin channel (VS sampler
//     s1) sampled on a downwind-drifting world-XY UV gives SPATIAL gust
//     variation — gust fronts sweep across the sea and the amplitude swells
//     under them (the "waves really crash because of moving winds" ask).
//     Bounded to [0.5, 1.5]x so a degenerate fetch can never explode it.
// and a SHORE GATE fades the swell to zero in the shallow band: the VS
// samples the same R32F depth copy the PS uses (VS sampler s0, freshly
// copied every flush BEFORE this draw) at the flat vertex's screen position,
// linearizes it with c13 and gates the swell on the water column thickness.
// This is what re-enables the raised amplitude safely: the 0.16 m swell was
// already sliding the waterline +-swell/slope across near-flat shores (the
// "edge of sea not rendering properly" defect); the gate pins the waterline
// geometry flat while open water keeps the full waves. Position-pure: the
// gate is a pure function of world position + the frame-stable depth copy,
// so shared edge verts of adjacent quads gate identically (no cracks).
//
// All wave DETAIL (normal perturbation, scrolling detail, shore foam/
// wavelets) remains per-pixel in Water_Parallax.hlsl — the two shaders pair
// exactly as before: this VS animates the surface, that PS shades it.
//
// Constants:
//   c0-c3 = WorldViewProjection
//   c4-c7 = World
//           WATER AUDIT #5 (closed 2026-11-02, FXC-verified): c6 is world
//           row 2 — READ by the compiled VS (dp4 r1.z, r0, c6 feeding
//           worldPos/viewDir). There is NO dead c6 sunDir here: sunDir
//           lives at c9, and the CPU's only c4-c7 write is the world
//           matrix upload (waterPipe.cpp RwD3D9SetVertexShaderConstant(4,
//           world, 4)). The PS c6 sunDir upload is likewise live (read by
//           Water_Parallax.hlsl register(c6)). No CPU write removed.
//   c8 = (camPosX, camPosY, camPosZ, time)
//   c9 = (sunDirX, sunDirY, sunDirZ, sunBrightness)
//   c10 = (bodyClassifyOn, 0, 0, 0)
//         water body classification gate, owned by waterPipe.cpp (NOT a
//         PS c14 mirror — the authoritative contract is Water_Parallax's
//         c14/c15); .x is 1 when the CPU classified this flush's vertices,
//         0 otherwise — the WHOLE constant is (0,0,0,0) when classification
//         is off, which zeroes TEXCOORD5 below and lets the PS's
//         WaterBodyContract all-zero gate pin every scale to 1.0.
//   c11 = (swellAmp, timeScale, windDirX, windDirY) -- W1 + W2 extension
//         Gated vertical-only swell (two waves). Uploaded by waterPipe.cpp
//         every flush; c11.x = 0 reproduces the flat rewrite bit for bit
//         (the safe default if a caller ever forgets the upload).
//         .zw = UNIT wind direction (CPU-normalized CWeather__WindDir.x/y;
//               falls back to the classic dirA (0.9497, 0.3328) when the
//               game vector is degenerate). Consumed only where c12.x > 0,
//               so a calm frame keeps the hardcoded dirs bit-exact.
//   c12 = (windStrength, gustAmp, shoreFadeW, vtfOK)  -- W2 wind block
//         .x = CWeather__Wind clamped [0,1] (the tree-sway wind source)
//         .y = SkyNoise gust amplitude (0 = gust off: tile unavailable)
//         .z = shore-gate width in metres (the INI waterShoreFade range);
//              0 = gate off (tier < 2 / depth copy invalid / VTF refused)
//         .w = 1 when the depth copy is bound at VS sampler s0
//   c13 = (nearClip, farClip, 0, 0)  -- W2 depth linearization pair (same
//         values the PS receives as c8.xy — one linearization contract)
//
// Vertex samplers (vs_3_0 texture fetch; device stages 256/257 — a bank
// SEPARATE from the PS s0-s4, bound raw by waterPipe.cpp):
//   s0 = scene depth copy (R32F, raw window z; copied fresh every flush
//        BEFORE this draw by PostFX_CopyDepthToTexture) — shore gate.
//   s1 = SkyNoise universal packed tile (R = tileable Perlin fBm) — gust.
//   Both texldl fetches sit inside uniform gates (c12.y and c12.z/c12.w): a
//   gate at 0 never executes the fetch, so an unbound sampler can never
//   contribute. vs_3_0 texldl carries an explicit LOD — no gradient
//   constraint, so the uniform branch is legal (unlike the PS tex2D rule).

uniform float4x4 worldViewProj : register(c0);
uniform float4x4 world         : register(c4);
uniform float4   camTime       : register(c8);  // xyz=camera pos, w=time
uniform float4   sunDir        : register(c9);  // xyz=sun dir (negated), w=brightness
uniform float4   bodyParams    : register(c10); // x=classification gate (see above)
uniform float4   waveSwell     : register(c11); // x=amp gate, y=time scale, zw=wind dir (unit)
uniform float4   windParams    : register(c12); // x=wind strength, y=gust amp, z=shore width, w=vtf ok
uniform float4   clipParams    : register(c13); // x=near clip, y=far clip

sampler2D depthVSTex : register(s0);   // VS sampler 0 = device stage 256 (depth copy)
sampler2D noiseVSTex : register(s1);   // VS sampler 1 = device stage 257 (SkyNoise tile)

struct VS_INPUT {
    float3 position : POSITION;
    // NORMAL is the free channel: waterPipe.cpp writes the per-vertex body
    // classification here at flush time (the game's vanilla water pass never
    // puts anything useful in it for this plane geometry).
    float3 normal   : NORMAL;
    float4 color    : COLOR0;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 position  : POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 worldPos  : TEXCOORD1;
    float3 viewDir   : TEXCOORD2;
    float3 lightDir  : TEXCOORD3;
    float4 screenPos : TEXCOORD4;
    float4 vtxColor  : COLOR0;
    // Water body channel (W7-B -> PS, Water_Parallax.hlsl):
    //   x = bodyType code 0=OCEAN 1=SHALLOW 2=INTERIOR 3=RIVER 4=POND 5=LAKE
    //   y = bodyBlend 0..1 (ramps to 0 within 5 m of a type-changing edge)
    //   z = signed aspect: sign = plane long axis, |z| = extent ratio >= 1
    //       (SA flowX/Y are all zero) — SPARE for the PS today
    //       (WaterBodyContract reads only x/y); kept for a future
    //       river-scroll consumer
    //   w = 0
    // ZEROED when c10.x == 0 (classification off) — the PS then sees an
    // all-zero channel plus all-zero c14/c15 and pins every body scale to
    // the vanilla 1.0 via WaterBodyContract's mag gate (mid-plane code 0
    // alone would otherwise read as OCEAN).
    float4 bodyType  : TEXCOORD5;
};

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;

    // ---- Gated vertical swell (W1 restore + W2 wind drive) ----
    // Two vertical-only waves as a pure function of world position + time.
    // Every divide/normalize is epsilon-guarded (SM3.0: NaN renders black);
    // the wavelengths are nonzero literals so the guards only exist to
    // satisfy the no-unguarded-divide doctrine. c11.x = 0 (or a missing
    // upload) collapses this to exactly the flat rewrite.
    float2 posXY  = IN.position.xy;
    float  swTime = camTime.w * max(waveSwell.y, 1e-3);
    float  swAmp  = saturate(waveSwell.x);

    // ---- Wind direction bias (W2) ----
    // c11.zw is a UNIT vector (CPU-guaranteed), c12.x the clamped game wind.
    // Both wave trains rotate toward the wind, strength-weighted: at calm
    // (c12.x = 0) the lerp weights are 0 and the hardcoded classic dirs are
    // bit-exact; at full wind the trains align with the wind (0.8/0.55 keep
    // a cross-chop remnant so the sea never reads as one pure train).
    float  wStr  = saturate(windParams.x);
    float2 dirA  = lerp(float2(1.0, 0.35), waveSwell.zw, wStr * 0.8);
    dirA /= max(length(dirA), 1e-7);
    float2 dirB  = lerp(float2(-0.45, 1.0), waveSwell.zw, wStr * 0.55);
    dirB /= max(length(dirB), 1e-7);

    // Wave A: long swell, wavelength 45 m, amp 0.34 m (W2: was 0.10)
    float kA = 6.2831853 / 45.0;
    float cA = sqrt(9.81 / max(kA, 1e-7));
    float fA = kA * (dot(dirA, posXY) - cA * swTime);
    // Wave B: cross chop, wavelength 23 m, amp 0.20 m (W2: was 0.06)
    float kB = 6.2831853 / 23.0;
    float cB = sqrt(9.81 / max(kB, 1e-7));
    float fB = kB * (dot(dirB, posXY) - cB * swTime);

    // ---- SkyNoise gust (W2): spatial amplitude variation ----
    // The universal tile's Perlin fBm channel (R) sampled on a world-XY UV
    // that DRIFTS DOWNWIND (gust fronts travel with the wind). Zero-mean
    // +-1, scaled by c12.y and wind-weighted (0.35 base so a calm sea still
    // breathes a little). Bounded to [0.5, 1.5]: a degenerate/unbound fetch
    // can never explode the amplitude. Uniform gate: c12.y == 0 (tile
    // unavailable) never executes the texldl.
    float gustMod = 1.0;
    if(windParams.y > 1e-4){
        float2 guv = posXY * 0.0042 + waveSwell.zw * (camTime.w * 0.011);
        float g = tex2Dlod(noiseVSTex, float4(guv, 0.0, 0.0)).r;
        gustMod = clamp(1.0 + (g - 0.5) * 2.0 * windParams.y * (0.35 + 0.65 * wStr),
                        0.5, 1.5);
    }

    // ---- Shore gate (W2): fade the swell out over the shallow band ----
    // The flat vertex's clip position gives its screen UV; the R32F depth
    // copy (fresh this flush, bound at VS s0) gives the scene depth BEHIND
    // the water at that pixel; linearized with c13 exactly like the PS's
    // LinearizeDepth, the difference to the vertex's own view depth
    // (clip w) is the water column thickness. smoothstep over c12.z metres
    // (the INI waterShoreFade range — the same band the PS shore algebra
    // lives in): 0 at the waterline (the waterline geometry stays FLAT and
    // can never fight the beach), 1 from the band edge outward (open water
    // keeps the full swell). Behind-camera vertices (clip w <= 0) read
    // thickness = sceneZ - 0 = large -> gate 1 (neutral full swell).
    // Uniform gates: c12.z == 0 (gate off) or c12.w < 0.5 (no depth bind)
    // never execute the texldl -> gate stays exactly 1 (pre-W2 behaviour).
    float4 flatClip = mul(float4(IN.position, 1.0), worldViewProj);
    float shoreGate = 1.0;
    if(windParams.z > 1e-4 && windParams.w > 0.5){
        float2 suv = saturate(flatClip.xy / max(flatClip.w, 1e-7)
                              * float2(0.5, -0.5) + 0.5);
        float dz = tex2Dlod(depthVSTex, float4(suv, 0.0, 0.0)).x;
        float denom = max(clipParams.y - dz * (clipParams.y - clipParams.x), 1e-3);
        float sceneZ = (clipParams.x * clipParams.y) / denom;
        float thick = max(sceneZ - max(flatClip.w, 0.0), 0.0);
        float g = saturate(thick / max(windParams.z, 1e-3));
        shoreGate = g * g * (3.0 - 2.0 * g);
    }

    // Amplitude: wind-scaled (calm 0.55x -> 0.30 m total, full wind 1.0x ->
    // 0.54 m total), gust-modulated (bounded +-50%), shore-gated (0 in the
    // shallow band). Peak gust burst 0.81 m — still 3x below the old
    // a4e81f8 Gerstner's ~2.4 m pop threshold.
    float ampMod = (0.55 + 0.45 * wStr) * gustMod * shoreGate;
    float swell = (cos(fA) * 0.34 + cos(fB) * 0.20) * swAmp * ampMod;
    float3 surfPos = float3(posXY, IN.position.z + swell);

    // Flat plane base: position passes through (displaced only by the gated
    // swell above — no Gerstner horizontal chop, no per-vertex foam channel;
    // the shore-wave/foam band lives per-pixel in Water_Parallax.hlsl).
    OUT.position  = mul(float4(surfPos, 1.0), worldViewProj);
    OUT.screenPos = OUT.position;
    OUT.worldPos  = mul(float4(surfPos, 1.0), world);

    // Base UV: the game fills the water render buffer with the original SA
    // water texture mapping, so passing it through keeps the tile scale
    // identical to the original SA water texture scale. Degenerate-UV guard:
    // fall back to a world-space mapping per vertex (SM3.0 allows branches
    // without dependent texture reads).
    float2 baseUV = IN.texCoord;
    if(abs(baseUV.x) + abs(baseUV.y) < 1e-6)
        baseUV = IN.position.xy * 0.0625;
    OUT.texCoord = baseUV;

    // Unnormalized camera->surface vector; the PS normalizes with a 1e-7 guard.
    OUT.viewDir = OUT.worldPos.xyz - camTime.xyz;

    // Surface->light vector L. c9 already carries the NEGATED light-frame At
    // (waterPipe.cpp uploads -At; At is the direction the light TRAVELS, so
    // -At points back up at the sun — the same L every other pipe in the
    // project derives by negating its uploaded +At: buildingPipePS
    // dot(N,-lightDir), VehiclePBR Ll=-lightDir[i], PBR_Lighting
    // L=normalize(-sunDirection.xyz)). Pass it through UNNEGATED: the old
    // `-sunDir.xyz` here re-flipped it into the travel direction, so the PS's
    // S.L pointed AWAY from the sun — NdotL pinned at 0, the Blinn half-vector
    // mirrored the sun glint to the anti-sun side, and SunPierce/WaterGlitter
    // (both dot against -L / L) evaluated to exactly 0.
    OUT.lightDir = sunDir.xyz;

    // Game vertex colour/alpha — SA modulates its water quads through COLOR0;
    // the PS uses it as a modulation so per-vertex shore fades (if present
    // in the buffer) survive.
    OUT.vtxColor = IN.color;

    // Water body channel (W7-B): the CPU classified every vertex of this
    // flush inside waterPipe_setRenderState and stored
    // (bodyType, blend, signed aspect) in the NORMAL channel. Scale by
    // c10.x so a flush where classification was off (or failed its sanity
    // checks) forwards plain zeros instead of stale/garbage values —
    // vs_3_0-safe: one compare-free saturate + multiply, no branches, no
    // texture reads. The PS gates on its own all-zero mag check over this
    // channel plus c14/c15 (WaterBodyContract).
    OUT.bodyType = float4(IN.normal, 0.0) * saturate(bodyParams.x);

    return OUT;
}
