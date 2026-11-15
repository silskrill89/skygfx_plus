// Burnout Paradise-Style Motion Blur (ps_3_0)
// Inspired by GTA V's directional motion blur with velocity clustering.
//
// TIER 1 (2026-10-29 motionblur-physics-research §1d.3): pure shader rewrite,
// no C++/register change. (a) bidirectional taps, (b) per-tap depth rejection
// using the already-bound depth sampler s2 + c3.x/y, (c) texel-snapped tap
// UVs, (d) normalised + clamped blur direction, (e) small-length early discard,
// (f) one fetch per tap (the old 3x RGB split is gone), (g) depth-scaled
// attenuation (weight falls off with depth distance).
//
// N7 (2026-11-08 family-F rotor smear): per-pixel velocity sanity on top of
// Tier 1, still s0-s2 / c0-c3 only. (h) blur length capped at 4x the pixel's
// own reconstructed velocity (rotor blades have a short per-pixel vector even
// while the helicopter flies fast) and per-tap velocity-variance rejection
// (taps whose s1 velocity disagrees with the centre are faded out, so rotor
// interior cannot drag background across its silhouette).
//
// Register budget unchanged: s0-s2 and c0-c3 only. Ships behind the existing
// motionBlurEnable gate (C++ never dispatches this pass when it is off).
//
// Constants:
//   c0 = (blurStrength, radialStrength, maxSamples, speedFactor)
//   c1 = (screenW, screenH, 1/screenW, 1/screenH)
//   c2 = (cameraTerm, fullFrameW, fbScaleU, fbScaleV)
//        cameraTerm  = camera movement x strength x speed-ramp (C++ smoothstep)
//        fullFrameW  = 0 edge-only lens blur (running) .. 1 full frame (vehicle)
//        fbScaleU/V  = (screenW/frontBufferW, screenH/frontBufferH): the front
//                      buffer holds the frame 1:1 at its top-left of a larger
//                      (e.g. 2048x2048) raster, so s0 fetches scale into the
//                      content region. s1/s2 are screen-sized -> UV 0..1 direct.
//   c3 = (near, far, tanFovX, maxBlurPx) — depth-aware blur scaling
//
// Textures:
//   s0 = current frame (front buffer — fetched at tex*fbScale)
//   s1 = motion buffer (RG=motion XY, B=motion magnitude, A=depth)
//   s2 = depth buffer (INTZ, sampled while depth hook is suspended)

sampler2D currentTex : register(s0);
sampler2D motionTex  : register(s1);
sampler2D depthTex   : register(s2);

uniform float4 blurParams    : register(c0);
uniform float4 screenSize    : register(c1);
uniform float4 cameraParams  : register(c2);
uniform float4 depthFov      : register(c3); // x=near, y=far, z=tanFovX, w=maxBlurPx

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Simple noise for jitter (reduces banding)
float2 Jitter(float2 tex, float2 color)
{
    // Magic offset from GTA V's jitter formula
    const float2 JitterOffset = { 58.164f, 47.13f };
    float2 jitterLookup = tex * JitterOffset + (color * 8.0f);
    // Simple hash-based noise
    float noise = frac(sin(dot(jitterLookup, float2(12.9898, 78.233))) * 43758.5453);
    return float2(noise - 0.5, noise - 0.5);
}

// Linear eye-space depth from the INTZ depth buffer (NormalBuffer.hlsl formula,
// same one the pre-Tier-1 centre fetch used).
float LinearDepth(float z)
{
    return depthFov.x * depthFov.y / max(depthFov.y - z * (depthFov.y - depthFov.x), 1e-7);
}

// Snap a screen-space UV to the centre of its source texel. Keeps tap fetches
// off sub-texel positions so the blur stops shimmering as the view moves.
float2 SnapToTexel(float2 uv)
{
    return (floor(uv * screenSize.xy) + 0.5) * screenSize.zw;
}

// Accumulate one depth-aware tap, fetching colour exactly once (f).
//   (b) taps in front of the centre surface are rejected outright: an occluder
//       closer to the camera must not bleed into this pixel.
//   (g) remaining taps are attenuated by their relative depth distance; this
//       is what fades background taps that sit behind a foreground surface, so
//       background colour cannot smear through a silhouette either.
//   (h) N7 per-pixel velocity sanity: a tap whose reconstructed velocity
//       disagrees with the centre pixel's velocity is down-weighted. A fast
//       per-object surface (helicopter rotor blade) sits against a background
//       moving very differently; without this the blade's blur drags that
//       background across its silhouette as a milky sheet.
void AccumTap(float2 screenUV, float baseWeight, float centerLin, float depthTol,
              float2 centerVel, inout float3 result, inout float totalWeight)
{
    float2 uv = SnapToTexel(screenUV);

    float tapLin = LinearDepth(tex2D(depthTex, uv).r);
    if(tapLin < centerLin - depthTol)
        return; // in front of the surface: reject occluder bleed

    float depthDist = abs(tapLin - centerLin) / max(centerLin, 1e-4);
    float depthAtten = saturate(1.0 - depthDist * 1.5);
    if(depthAtten <= 0.0)
        return;

    // (h) Velocity-variance rejection. Tolerance = 2 screen texels (8-bit
    // velocity-quantisation headroom) plus 1.5x the centre speed, so coherent
    // motion is never rejected while wildly disagreeing taps (rotor interior
    // vs. background) fade to zero weight. s1 is screen-sized -> direct UV.
    float2 tapVel = tex2D(motionTex, uv).rg * 2.0 - 1.0;
    float velTol = 2.0 * max(screenSize.z, screenSize.w) + length(centerVel) * 1.5;
    float velAtten = saturate(1.0 - length(tapVel - centerVel) / max(velTol, 1e-6));

    float weight = baseWeight * depthAtten * velAtten;
    if(weight <= 0.0)
        return;

    result += tex2D(currentTex, uv * cameraParams.zw).rgb * weight;
    totalWeight += weight;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    float2 center = float2(0.5, 0.5);
    
    // Sample current pixel (front buffer content region — see fbScale above)
    float3 color = tex2D(currentTex, tex * cameraParams.zw).rgb;
    
    // Sample motion buffer
    float4 motionData = tex2D(motionTex, tex);
    float2 motionVector = motionData.rg * 2.0 - 1.0; // Unpack from [0,1] to [-1,1]
    float motionMagnitude = motionData.b;
    
    // Calculate radial vector from screen center (vanishing point)
    float2 radialVec = tex - center;
    float radialDist = length(radialVec);
    float2 radialDir = radialVec / max(radialDist, 0.001);
    
    // Combine motion vector with radial blur (Burnout style)
    // At screen center, use mostly motion vector
    // At edges, blend in radial component
    float radialBlend = saturate(radialDist * 2.0);
    float2 blurDir = lerp(motionVector, radialDir, radialBlend * blurParams.y);
    
    // Calculate blur amount based on:
    // 1. Motion magnitude from buffer
    // 2. Camera velocity (global movement, already speed-ramped on the C++ side)
    // 3. Speed factor from config
    float blurAmount = motionMagnitude * blurParams.x;
    blurAmount += cameraParams.x * blurParams.w; // Camera velocity contribution

    // Lens weight: fullFrameW (cameraParams.y) ramps 0->1 with speed on the
    // C++ side. 0 = edge-only "running" blur (centre stays sharp, radial
    // falloff to the frame edges), 1 = uniform full-frame blur (bicycle and
    // vehicles). radialDist is 0 at centre .. ~0.707 at corners, so the
    // 1.41421356 factor normalises corner weight to 1.
    float radialW = saturate(radialDist * 1.41421356);
    float lensW = lerp(radialW, 1.0, saturate(cameraParams.y));
    blurAmount *= lensW;

    blurAmount = min(blurAmount, blurParams.z * 0.01); // Clamp to max blur

    // Depth-aware blur scaling: linear eye-space depth from the INTZ buffer
    // (NormalBuffer.hlsl pattern). Far surfaces keep full blur; near surfaces
    // (lin -> near) fade toward 0 so the global camera-velocity term doesn't
    // smear close geometry.
    float centerLin = LinearDepth(tex2D(depthTex, tex).r);
    float depthW = saturate(centerLin / depthFov.y * 1.5);
    blurAmount *= depthW;

    // Clamp to max blur in pixels (c3.w), converted to UV units via 1/screenW
    blurAmount = min(blurAmount, depthFov.w * pixel.x);

    // (h) N7 per-pixel length sanity: cap the blur at a small multiple of this
    // pixel's OWN reconstructed velocity. The cinematic camera term can inflate
    // blurAmount far beyond the per-pixel motion of a fast per-object surface
    // (rotor blades barely move per-pixel even while the helicopter flies), and
    // an uncapped blur then drags the blade across the frame as a milky sheet.
    // 4x keeps genuine camera-driven blur; the 2-texel floor stops near-zero
    // (quantised) velocities from collapsing the camera term to nothing while
    // still keeping the blur short enough to stay inside the object's edge.
    float velFloor = 2.0 * max(screenSize.z, screenSize.w);
    float velCap = 4.0 * length(motionVector) + velFloor;
    blurAmount = min(blurAmount, velCap);
    
    // Early exit if no blur needed (GTA V technique: discard when velocity near zero)
    if(blurAmount < 0.001)
        return float4(color, 1.0);

    // (d) Normalise then clamp the blur direction before it is used
    // (NaN-safe: guard against zero length when motionVector cancels radialDir).
    float dirLen = length(blurDir);
    float2 unitDir = dirLen > 1e-7 ? blurDir / dirLen : float2(0.0, 0.0);
    unitDir = clamp(unitDir, -1.0, 1.0);
    float2 blurStep = unitDir * blurAmount;

    // Depth tolerance for the in-front rejection: scale with distance so a
    // near pixel keeps tight rejection while a far one tolerates its own slope.
    float depthTol = max(centerLin * 0.05, 0.01);

    // Bidirectional taps: 4 each side (8 total, same tap count as the old
    // one-sided loop) so the blur is symmetric around the pixel. The old
    // sub-texel jitter is intentionally dropped — it defeats the texel snap
    // in (c); symmetric snapped taps already suppress the banding it hid.
    float3 result = float3(0, 0, 0);
    float totalWeight = 0.0;

    // Center sample (highest weight)
    result += color;
    totalWeight += 1.0;

    const int kHalfTaps = 4;
    for(int i = 1; i <= kHalfTaps; i++)
    {
        float t = (float)i / (float)kHalfTaps;

        // Weight falloff (sharper center, softer edges)
        float weight = 1.0 - t;
        weight *= weight; // Quadratic falloff

        AccumTap(tex + blurStep * t, weight, centerLin, depthTol, motionVector, result, totalWeight);
        AccumTap(tex - blurStep * t, weight, centerLin, depthTol, motionVector, result, totalWeight);
    }
    
    // Normalize
    result /= totalWeight;
    
    return float4(result, 1.0);
}
