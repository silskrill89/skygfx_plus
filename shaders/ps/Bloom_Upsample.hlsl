// Bloom_Upsample.hlsl - Jimenez/COD 9-tap tent bloom upsample (ps_3_0)
// SkyGFX Plus - PostFX bloom lane, pass 3 of 4 (repeated once per mip, up to 6 mips)
//
// 3x3 tent filter (1-2-1 separable weights, total 16) over the smaller bloom
// mip. Single-mip shader: this pass emits ONLY the tent-filtered upsample; the
// ADDITIVE accumulation into the larger mip is done by the parallel postfx lane
// via D3D9 render-target blend (SRCBLEND=ONE, DESTBLEND=ONE) - no second texture
// bound here. Walk the chain small -> large; each pass adds its tent blur into
// the next mip up. That reproduces the COD/Jimenez progressive upsample+accumulate.
//
// Reference: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// (Jorge Jimenez).
//
// Register contract (parallel postfx lane binds):
//   s0 = smaller bloom mip being upsampled (LINEAR/CLAMP sampler)
//   c0 = (filterRadius, invSizePx, unused, unused)
//        filterRadius : tent tap spacing scale in texels (0.5 - 1.0 typical)
//        invSizePx    : 1/width of the smaller mip (square texels assumed)
//        unused
//        unused
//
// Tent weights (1-2-1 x 1-2-1):
//   1 2 1
//   2 4 2
//   1 2 1
//
// Tap count: 9
//
// SM3.0: fixed tap count (unrolled), no dynamic loops, 1e-7 guards,
// safe at half/quarter res sizes.

sampler2D smallTex : register(s0);
uniform float4 bloomUpsampleParams : register(c0); // (filterRadius, invSizePx, unused, unused)

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float filterRadius = max(bloomUpsampleParams.x, 1e-7);
    float invSizePx    = max(bloomUpsampleParams.y, 1e-7);

    float2 offset = float2(invSizePx, invSizePx) * filterRadius;

    // 3x3 grid
    float3 tl = tex2Dlod(smallTex, float4(uv + float2(-offset.x, -offset.y), 0.0, 0.0)).rgb;
    float3 tc = tex2Dlod(smallTex, float4(uv + float2(      0.0, -offset.y), 0.0, 0.0)).rgb;
    float3 tr = tex2Dlod(smallTex, float4(uv + float2( offset.x, -offset.y), 0.0, 0.0)).rgb;

    float3 ml = tex2Dlod(smallTex, float4(uv + float2(-offset.x,       0.0), 0.0, 0.0)).rgb;
    float3 mc = tex2Dlod(smallTex, float4(uv,                                0.0, 0.0)).rgb;
    float3 mr = tex2Dlod(smallTex, float4(uv + float2( offset.x,       0.0), 0.0, 0.0)).rgb;

    float3 bl = tex2Dlod(smallTex, float4(uv + float2(-offset.x,  offset.y), 0.0, 0.0)).rgb;
    float3 bc = tex2Dlod(smallTex, float4(uv + float2(      0.0,  offset.y), 0.0, 0.0)).rgb;
    float3 br = tex2Dlod(smallTex, float4(uv + float2( offset.x,  offset.y), 0.0, 0.0)).rgb;

    // Tent filter 1-2-1 separable (total weight 16)
    float3 result  = mc * 4.0;
    result += (tc + ml + mr + bc) * 2.0;
    result += (tl + tr + bl + br);
    result /= 16.0;

    // Additive accumulation is the lane's RT blend state (ONE/ONE), not in-shader.
    return float4(result, 1.0);
}
