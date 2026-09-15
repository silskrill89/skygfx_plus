// SMAA Temporal Motion Blur Pass (ps_3_0)
// Blends current frame with previous frame based on motion detection
// Moving edges get more blur for temporal stability
//
// Smooth motion-speed-weighted blending: never fully discards history,
// even at abrupt scene transitions (portals, interiors).
// History accumulates gradually; fast motion adapts faster but still smoothly.
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

sampler2D currentTex : register(s0);
sampler2D prevTex    : register(s1);
sampler2D velocityTex : register(s2);

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
    
    // ---- Cloud-shadow-style exponential accumulation ----
    // Linear blend factor from params.x (high history) up to (1.0 - params.x)
    // When avgMotion=0: blendFactor = params.x (e.g. 0.1 = 90% history, slow accumulation)
    // When avgMotion=1: blendFactor = 1.0 - params.x (e.g. 0.9 = 10% history, still smooth)
    // NEVER fully discards history at any motion value.
    float blendFactor = params.x + avgMotion * (1.0 - 2.0 * params.x);
    
    // Soft power curve: biases toward more history when motion is moderate
    // Low motion: barely moves from params.x (long accumulation)
    // High motion: gently pushes up but never reaches 1.0 (no hard cut)
    blendFactor = max(blendFactor, params.x);
    blendFactor = 1.0 - pow(1.0 - blendFactor, 1.5);
    
    // Clamp: always keep at least params.x fraction of current, and at most
    // histParams.z (maxHistClamp, 0.9) — never below 10% history even at max motion.
    float minBlend = params.x;
    float maxBlend = histParams.z;
    blendFactor = clamp(blendFactor, minBlend, maxBlend);

    // ---- Velocity-driven temporal weight (c2 = velocity, rotation, tanFovX, tanFovY) ----
    // Real-lens-style motion softness: camera motion raises the current-frame
    // fraction (less ghosting) while screen edges keep more history (softer,
    // like lens edge softness). FOV-normalized via tanFov so the radial
    // falloff is angular, not resolution-dependent. No luminance change,
    // no vignette darkening.
    float vel = saturate(motionParams.x + motionParams.y);
    float2 ndc = (tex - 0.5) * 2.0;
    float2 ang = ndc * float2(motionParams.z, motionParams.w);
    float radial = saturate(length(ang));
    float velScale = 1.0 + 0.6 * vel;             // more current when moving
    float edgeScale = 1.0 - 0.35 * radial * vel;  // edges keep more history
    blendFactor = clamp(blendFactor * velScale * edgeScale, minBlend, maxBlend);

    // Reproject history using the velocity buffer (s2): sample the previous
    // frame at the position this pixel occupied last frame. velToPx (c3.w) is
    // 0 when no velocity buffer is bound, so the sample stays unshifted.
    float2 velUV = (tex2D(velocityTex, tex).rg * 2.0 - 1.0) * histParams.w;
    float3 hist = tex2D(prevTex, tex - velUV);

    // Blend current and reprojected history
    float3 result = lerp(hist, current, blendFactor);
    
    return float4(result, 1.0);
}