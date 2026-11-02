// SMAA Temporal Motion Blur Pass (ps_3_0)
// Blends current frame with previous frame based on motion detection
// Moving edges get more blur for temporal stability
//
// Motion blend: blendFactor = clamp(lerp(0.15, 1.0, motionFactor), params.x, histParams.z)
//   low motion keeps history (temporal AA), high motion rejects history
//   entirely (current frame only). params.x (c0.x = blendStrength, uploaded
//   by DrawSMAA pass 3) is the FLOOR on the current-frame weight: 0.4 at
//   rest, 1.0 when the history is invalid (first frame after raster
//   create / release / device reset) and on every 8th valid-history frame
//   (hard TTL refresh) — WITHOUT that floor, an uncleared history raster
//   feeds undefined content at 85% weight straight into the presented
//   camera raster. The reprojected history is
//   clamped to the 3x3 current-frame neighbourhood (anti-ghosting) and
//   rejected at disocclusions via depth/normal disagreement (s3/s4).
//
// Constants:
//   c0   = (blendStrength, motionScale, 0, 0)
//   c1   = (screenW, screenH, 1/screenW, 1/screenH)
//   c2   = (velocity, rotation, tanFovX, tanFovY) — velocity-driven temporal weight
//   c3   = (near, far, maxHistClamp, velToPx) — history reprojection params
//
// Textures:
//   s0 = current frame (after SMAA)
//   s1 = previous frame
//   s2 = velocity buffer (RG=velocity XY packed to [0,1])
//   s3 = depth buffer (optional; disocclusion rejection, inert if unbound)
//   s4 = normal buffer (optional; disocclusion rejection, inert if unbound)

sampler2D currentTex : register(s0);
sampler2D prevTex    : register(s1);
sampler2D velocityTex : register(s2);
sampler2D depthTex   : register(s3);
sampler2D normalTex  : register(s4);

uniform float4 params : register(c0); // x=blendStrength, y=motionScale
uniform float4 screenSize : register(c1);
uniform float4 motionParams : register(c2); // x=velocity, y=rotation, z=tanFovX, w=tanFovY
uniform float4 histParams : register(c3); // x=near, y=far, z=maxHistClamp, w=velToPx

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Sample current and previous frame
    float3 current = tex2D(currentTex, tex).rgb;
    float3 prev = tex2D(prevTex, tex).rgb;
    
    // Sample velocity buffer for velocity-magnitude-driven motion signal
    float2 velVec = tex2D(velocityTex, tex).rg * 2.0 - 1.0;
    float velMag = length(velVec);
    bool hasVel = histParams.w > 0.0;
    
    // Calculate motion based on luminance difference
    float lumaCurrent = Luma(current);
    float lumaPrev = Luma(prev);
    float lumaDiff = abs(lumaCurrent - lumaPrev) * params.y;
    float motion = hasVel ? max(lumaDiff, saturate(velMag * 8.0) * params.y) : lumaDiff;
    
    // Clamp motion to [0, 1]
    motion = saturate(motion);
    
    // Sample neighbors for edge-aware blending
    float3 currentN = tex2D(currentTex, tex + float2(0, -pixel.y)).rgb;
    float3 currentS = tex2D(currentTex, tex + float2(0, pixel.y)).rgb;
    float3 currentE = tex2D(currentTex, tex + float2(pixel.x, 0)).rgb;
    float3 currentW = tex2D(currentTex, tex + float2(-pixel.x, 0)).rgb;
    
    float3 prevN = tex2D(prevTex, tex + float2(0, -pixel.y)).rgb;
    float3 prevS = tex2D(prevTex, tex + float2(0, pixel.y)).rgb;
    float3 prevE = tex2D(prevTex, tex + float2(pixel.x, 0)).rgb;
    float3 prevW = tex2D(prevTex, tex + float2(-pixel.x, 0)).rgb;
    
    // Calculate motion at neighbors
    float lumaDiffN = abs(Luma(currentN) - Luma(prevN)) * params.y;
    float lumaDiffS = abs(Luma(currentS) - Luma(prevS)) * params.y;
    float lumaDiffE = abs(Luma(currentE) - Luma(prevE)) * params.y;
    float lumaDiffW = abs(Luma(currentW) - Luma(prevW)) * params.y;
    
    float motionN = lumaDiffN;
    float motionS = lumaDiffS;
    float motionE = lumaDiffE;
    float motionW = lumaDiffW;
    
    if (hasVel)
    {
        float2 velVecN = tex2D(velocityTex, tex + float2(0, -pixel.y)).rg * 2.0 - 1.0;
        float2 velVecS = tex2D(velocityTex, tex + float2(0, pixel.y)).rg * 2.0 - 1.0;
        float2 velVecE = tex2D(velocityTex, tex + float2(pixel.x, 0)).rg * 2.0 - 1.0;
        float2 velVecW = tex2D(velocityTex, tex + float2(-pixel.x, 0)).rg * 2.0 - 1.0;
        
        motionN = max(lumaDiffN, saturate(length(velVecN) * 8.0) * params.y);
        motionS = max(lumaDiffS, saturate(length(velVecS) * 8.0) * params.y);
        motionE = max(lumaDiffE, saturate(length(velVecE) * 8.0) * params.y);
        motionW = max(lumaDiffW, saturate(length(velVecW) * 8.0) * params.y);
    }
    
    // Average motion in neighborhood
    float avgMotion = (motion + motionN + motionS + motionE + motionW) * 0.2;
    avgMotion = saturate(avgMotion);
    
    // ---- Motion blend curve ----
    // blendFactor = weight of the CURRENT frame:
    //   motionFactor 0 -> 0.15  (85% history: strong temporal AA)
    //   motionFactor 1 -> 1.00  (history fully rejected: current frame only)
    // High motion must kill history outright, otherwise reprojected stale
    // colour trails behind moving geometry (ghosting). No divides here.
    // params.x (blendStrength, c0.x) is the FLOOR on the current-frame
    // weight — the C++ contract from DrawSMAA:
    //   * 1.0 on the first frame after init/release/resolution change and on
    //     every 8th valid-history frame (hard history TTL) -> full current
    //     frame, so a cleared/undefined history raster is never blended in;
    //   * 0.4 at rest (the tuned 60/40 history split).
    // histParams.z (maxHistClamp, c3.z) is the matching CEILING.
    // (This floor used to be hardcoded away; the dead params.x let uncleared
    // history dominate the presented frame.)
    float vel = saturate(motionParams.x + motionParams.y);
    float motionFactor = saturate(max(avgMotion, vel));
    float minBlend = min(params.x, histParams.z);
    float maxBlend = max(params.x, histParams.z);
    float blendFactor = clamp(lerp(0.15, 1.0, motionFactor), minBlend, maxBlend);

    // Reproject history using the velocity buffer (s2): sample the previous
    // frame at the position this pixel occupied last frame. velToPx (c3.w) is
    // 0 when no velocity buffer is bound, so the sample stays unshifted.
    // Pure velocity offset — no radial/edge bias (it distorted the lookup).
    float2 velUV = (tex2D(velocityTex, tex).rg * 2.0 - 1.0) * histParams.w;
    float3 hist = tex2D(prevTex, tex - velUV);

    // ---- 3x3 neighbourhood clamp (anti-ghosting) ----
    // Clamp the reprojected history into the min/max colour box of the
    // current-frame 3x3 neighbourhood. Cross taps (currentN/S/E/W) are
    // reused from the motion pass; only the 4 diagonals are extra taps.
    float3 cNW = tex2D(currentTex, tex + float2(-pixel.x, -pixel.y)).rgb;
    float3 cNE = tex2D(currentTex, tex + float2( pixel.x, -pixel.y)).rgb;
    float3 cSW = tex2D(currentTex, tex + float2(-pixel.x,  pixel.y)).rgb;
    float3 cSE = tex2D(currentTex, tex + float2( pixel.x,  pixel.y)).rgb;

    float3 nmin = min(min(min(current, currentN), min(currentS, currentE)),
                      min(min(currentW, cNW), min(cNE, min(cSW, cSE))));
    float3 nmax = max(max(max(current, currentN), max(currentS, currentE)),
                      max(max(currentW, cNW), max(cNE, max(cSW, cSE))));
    hist = clamp(hist, nmin, nmax);

    // ---- Disocclusion rejection: depth (s3) + normal (s4) ----
    // Compare current vs reprojected-position samples. If a stage is unbound
    // the fetch returns the same constant at both UVs, the difference is 0
    // and nothing is rejected. Thresholds use reciprocal form — no divides.
    // The step() gate additionally skips depth rejection when both reads are 0.
    float depthCur  = tex2D(depthTex, tex).r;
    float depthHist = tex2D(depthTex, tex - velUV).r;
    float depthReject = saturate((abs(depthCur - depthHist) - 0.01) * 100.0);
    depthReject *= step(1e-7, max(depthCur, depthHist));

    float3 nrmCur  = tex2D(normalTex, tex).rgb * 2.0 - 1.0;
    float3 nrmHist = tex2D(normalTex, tex - velUV).rgb * 2.0 - 1.0;
    float normalReject = saturate((length(nrmCur - nrmHist) - 0.35) * 4.0);

    // Blend: history weight is what survives motion + disocclusion rejection.
    // blendFactor >= params.x guarantees histWeight == 0 exactly when the C++
    // uploads blendStrength = 1.0 (invalid history / TTL refresh) — the
    // current frame is taken whole and no history is presented.
    float histWeight = (1.0 - blendFactor) * (1.0 - depthReject) * (1.0 - normalReject);
    float3 result = lerp(current, hist, histWeight);

    return float4(result, 1.0);
}