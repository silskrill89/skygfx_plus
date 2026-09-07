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
//
// Textures:
//   s0 = current frame (after SMAA)
//   s1 = previous frame

sampler2D currentTex : register(s0);
sampler2D prevTex    : register(s1);

uniform float4 params : register(c0); // x=blendStrength, y=motionScale
uniform float4 screenSize : register(c1);

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
    
    // Calculate motion based on luminance difference
    float lumaCurrent = Luma(current);
    float lumaPrev = Luma(prev);
    float motion = abs(lumaCurrent - lumaPrev) * params.y;
    
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
    float motionN = abs(Luma(currentN) - Luma(prevN)) * params.y;
    float motionS = abs(Luma(currentS) - Luma(prevS)) * params.y;
    float motionE = abs(Luma(currentE) - Luma(prevE)) * params.y;
    float motionW = abs(Luma(currentW) - Luma(prevW)) * params.y;
    
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
    // (1.0 - params.x) — never below 10% history even at max motion.
    float minBlend = params.x;
    float maxBlend = 1.0 - params.x * 0.5;
    blendFactor = clamp(blendFactor, minBlend, maxBlend);
    
    // Blend current and previous frame
    float3 result = lerp(prev, current, blendFactor);
    
    return float4(result, 1.0);
}