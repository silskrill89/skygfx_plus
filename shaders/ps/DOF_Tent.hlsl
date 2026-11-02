// DOF_Tent.hlsl - 3x3 tent post-filter (ps_3_0)
// SkyGFX Plus - PostFX DOF lane, pass 3 of 4 (CoC -> bokeh -> tent -> combine)
//
// Smooths the 22-tap bokeh gather: separable tent (triangle) kernel
// (1,2,1)/4 (x) (1,2,1)/4 = the 9-tap weight matrix below, /16 total.
// Softer than a box blur, kills the spiral tap grain without double-blurring.
//
// Register contract (postfx.cpp DrawDOF :3001-3004 binds EXACTLY):
//   s0 = half-res bokeh buffer (LINEAR/CLAMP sampler, uv 0..1)
//   c0 = (1/halfW, 1/halfH, 0, 0)
//        1/halfW/H : half-res texel size for the +-1 texel tap offsets
//        0, 0      : unused
//
// Output -> half-res tent RT: rgb = tent-blurred colour, a = tent-blurred CoC
// (downstream DOF_Combine reads rgb only; alpha carried for debug continuity).
//
// Tap count: 9 (fixed).
//
// SM3.0: no bitwise ops, no divides beyond the constant /16, fixed taps.

sampler2D blurTex : register(s0);

uniform float4 tentParams : register(c0); // (1/halfW, 1/halfH, unused, unused)

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 t = tentParams.xy;

    // (1,2,1) x (1,2,1) tent weights, total 16.
    float4 c00 = tex2D(blurTex, uv + float2(-t.x, -t.y));
    float4 c10 = tex2D(blurTex, uv + float2( 0.0, -t.y));
    float4 c20 = tex2D(blurTex, uv + float2( t.x, -t.y));
    float4 c01 = tex2D(blurTex, uv + float2(-t.x,  0.0));
    float4 c11 = tex2D(blurTex, uv);
    float4 c21 = tex2D(blurTex, uv + float2( t.x,  0.0));
    float4 c02 = tex2D(blurTex, uv + float2(-t.x,  t.y));
    float4 c12 = tex2D(blurTex, uv + float2( 0.0,  t.y));
    float4 c22 = tex2D(blurTex, uv + float2( t.x,  t.y));

    float4 sum = c00 * 1.0 + c10 * 2.0 + c20 * 1.0
               + c01 * 2.0 + c11 * 4.0 + c21 * 2.0
               + c02 * 1.0 + c12 * 2.0 + c22 * 1.0;

    // Clamp before RT write (chain guard rule); both colour and the encoded
    // CoC alpha are averages of 0..1 inputs, so saturate is a no-op guard.
    return saturate(sum * (1.0 / 16.0));
}
