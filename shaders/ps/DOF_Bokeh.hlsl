// DOF_Bokeh.hlsl - 22-tap golden-spiral disc bokeh gather (ps_3_0)
// SkyGFX Plus - PostFX DOF lane, pass 2 of 4 (CoC -> bokeh -> tent -> combine)
//
// Scatter-as-gather bokeh on the half-res CoC buffer: each tap scatters its
// own colour over the disc its CoC prescribes (tap-radius >= distance to this
// pixel = covered). FOREGROUND taps (near field, signed CoC < 0) bleed over
// everything inside their disc - classic near-field over-blur; BACKGROUND taps
// (far field, signed CoC > 0) are gated by how background the CENTRE pixel is,
// so the far field never smears over sharp/foreground pixels. The centre
// sample always carries weight 1 so in-focus pixels stay sharp and wsum >= 1.
//
// Register contract (postfx.cpp DrawDOF :2995-2998 binds EXACTLY):
//   s0 = half-res CoC buffer (rgb = scene, a = 0.5-centred signed CoC;
//        LINEAR/CLAMP sampler - taps outside 0..1 clamp to edge)
//   c0 = (bokehRadius, 1/halfW, 1/halfH, 0)
//        bokehRadius : disc radius in HALF-RES pixels (dofBokehRadius, 1..10)
//        1/halfW/H   : texel size converting pixel offsets to UV
//        0           : unused
//   c3 = (feather, 0, 0, 0) - NOT uploaded by postfx.cpp yet (fixer directive
//        m0061: "Bokeh c3.x feather fallback 1.0 if zero"). feather = soft
//        edge width of the disc coverage step, in half-res pixels; falls back
//        to 1.0 when unset/zero, then is sanity-clamped to [0.25, 4] because
//        the register is never written today and may hold stale values from an
//        earlier pass.
//
// Output -> half-res bokeh RT: rgb = gathered colour, a = pass-through centre
// CoC (downstream only reads rgb; carried for debug continuity).
//
// Tap count: 1 centre + 22 fixed golden-angle spiral taps (r = sqrt((i+.5)/22),
// theta = i * golden angle) - uniform disc coverage, no dynamic loop bound.
//
// SM3.0: no bitwise ops, all fetches straight-line (none inside branches),
// every divide epsilon-guarded (NaN = black fullscreen), single static loop.

sampler2D cocTex : register(s0);

uniform float4 bokehParams : register(c0); // (bokehRadius, 1/halfW, 1/halfH, unused)
uniform float4 bokehAux    : register(c3); // (feather, 0, 0, 0) - unset today

// Unit-disc tap pattern: xy = offset (unit radius), z = distance from centre.
// Golden-angle spiral: even radial coverage at any tap count.
static const float3 kDisk[22] =
{
    float3(+0.1508, +0.0000, 0.1508), float3(-0.1925, +0.1764, 0.2611),
    float3(+0.0295, -0.3358, 0.3371), float3(+0.2427, +0.3165, 0.3989),
    float3(-0.4454, -0.0788, 0.4523), float3(+0.4219, -0.2684, 0.5000),
    float3(-0.1411, +0.5249, 0.5436), float3(-0.2691, -0.5182, 0.5839),
    float3(+0.5839, +0.2132, 0.6216), float3(-0.6074, +0.2507, 0.6571),
    float3(+0.2928, -0.6257, 0.6908), float3(+0.2164, +0.6899, 0.7230),
    float3(-0.6522, -0.3780, 0.7538), float3(+0.7651, -0.1682, 0.7833),
    float3(-0.4669, +0.6641, 0.8118), float3(-0.1079, -0.8324, 0.8394),
    float3(+0.6622, +0.5581, 0.8660), float3(-0.8911, +0.0369, 0.8919),
    float3(+0.6500, -0.6468, 0.9170), float3(-0.0435, +0.9405, 0.9415),
    float3(-0.6185, -0.7411, 0.9653), float3(+0.9797, +0.1318, 0.9886)
};

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float radius = max(bokehParams.x, 0.0);   // disc radius, half-res pixels
    float2 texel = bokehParams.yz;            // 1/halfW, 1/halfH (> 0 from C++)

    // Feather fallback per m0061 fixer directive: c3.x unset/zero -> 1.0 px.
    float feather = bokehAux.x;
    if(feather <= 1e-7)
        feather = 1.0;
    feather = clamp(feather, 0.25, 4.0); // stale-register sanity bound

    float4 center = tex2D(cocTex, uv);
    float centerS = center.a * 2.0 - 1.0;      // decode signed CoC
    // How "background" the centre is: 0 for sharp/foreground (blocks far-field
    // bleed), ramps to 1 within the first quarter of the far CoC.
    float centerBG = saturate(centerS * 4.0);

    float3 acc = center.rgb;
    float wsum = 1.0;                          // centre carries weight 1

    [loop] for(int i = 0; i < 22; i++)
    {
        float3 dp = kDisk[i];
        float2 offPx = dp.xy * radius;         // tap offset, half-res pixels
        float dist = dp.z * radius;            // tap distance from centre, px

        float4 t = tex2D(cocTex, uv + offPx * texel);
        float ts = t.a * 2.0 - 1.0;            // tap signed CoC

        // Disc coverage of this pixel by the tap's own CoC disc, feathered.
        float cov = saturate((abs(ts) * radius - dist) / feather);

        // FG taps (ts < 0) scatter at full coverage; BG taps (ts >= 0) are
        // gated by the centre's background amount (step/lerp branch-free).
        float bg = step(0.0, ts);
        float w = cov * lerp(1.0, centerBG, bg);

        acc += t.rgb * w;
        wsum += w;
    }

    // Clamp before RT write (chain guard rule); alpha pass-through keeps the
    // centre's encoded CoC.
    return float4(saturate(acc / max(wsum, 1e-7)), center.a);
}
