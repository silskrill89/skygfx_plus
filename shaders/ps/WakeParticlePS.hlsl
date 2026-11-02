// WakeParticlePS.hlsl — GPU particle sprite PS (m0336 lane, first slice).
//
// Quad-batch sprite path: FFP vertex pipe (D3DFVF_XYZRHW|DIFFUSE|TEX1, CPU
// projection in particles_gpu.cpp) + THIS pixel shader. The vertex COLOR0
// carries the per-particle alpha envelope (fade-in/out over lifetime, scaled
// with the c11/c12 wind-sea constants — see particles_gpu.cpp); the sprite
// texture comes from the effectsPC.txd harvest (s0 = boatwake1 raster).
//
// SM3.0 only (ps_3_0). Division-free: the edge remap is a constant reciprocal,
// no normalize, no denominator that can hit zero.

sampler2D spriteTex : register(s0);

struct PSIn
{
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

float4
main(PSIn i) : COLOR0
{
    float4 t = tex2D(spriteTex, i.uv);

    // alpha = sprite alpha * vertex envelope, clamped without min/max intrins
    float a = t.a * i.col.a;
    a = a > 0.0f ? a : 0.0f;
    a = a < 1.0f ? a : 1.0f;

    // soften the sprite edge: roll the outer 5% of the alpha ramp off to zero
    // so the quad boundary never shows as a hard line on the water.
    a = (a - 0.05f) * (1.0f / 0.95f);
    a = a > 0.0f ? a : 0.0f;

    return float4(t.rgb * i.col.rgb, a);
}
