// DOF_CoC.hlsl - depth-of-field circle-of-confusion prefilter (ps_3_0)
// SkyGFX Plus - PostFX DOF lane, pass 1 of 4 (CoC -> bokeh -> tent -> combine)
//
// Half-res prefilter drawn by postfx.cpp DrawDOF via FxPassToRT: fetches the
// full-res front buffer + screen-sized INTZ depth, linearises depth to eye-
// space metres, splits it into a NEAR and a FAR circle-of-confusion against
// the focus plane and keeps the EXTREME of the two (signed). The sign is
// preserved by encoding the CoC centred on 0.5 in the alpha channel:
//
//     encode: alpha = 0.5 + 0.5 * signedCoC   (signedCoC -1 near .. +1 far)
//     decode: signedCoC = alpha * 2 - 1,  magnitude = abs(signedCoC)
//
// Register contract (postfx.cpp DrawDOF :2983-2992 binds EXACTLY):
//   s0 = scene colour (pRasterFrontBuffer via rwRENDERSTATETEXTURERASTER,
//        LINEAR/CLAMP, raw UV 0..1 - this pass is uploaded no fbParams scale,
//        so the front buffer is fetched at plain UV like the rest of the FX
//        lane: SunShafts/Bloom_Composite do the same)
//   s1 = depth = g_ssaoDepthTex (INTZ; .r = window depth 0..1)
//   c0 = (focusDistance, focusRange, maxBlur, 0)
//        focusDistance : metres to the focal plane (dofFocusDistance, def 20)
//        focusRange    : metres from the focal plane where |CoC| saturates to
//                        1 (dofFocusRange, def 15 -> sharp zone focus +/- range)
//        maxBlur       : dofMaxBlur (0..1) - RESERVED here. The C++ lane also
//                        uploads it to this pass for symmetry, but the actual
//                        strength/mix cap lives in DOF_Combine (c0.x); using it
//                        in both places would square the configured strength.
//        0             : unused
//   c1 = (nearPlane, farPlane, 1/halfW, 1/halfH)
//        near/farPlane : RwCamera clip planes (metres), depth linearisation
//        1/halfW/halfH : half-res texel size - positions the 4-tap depth
//                        footprint (one half-res texel = 2x2 full-res texels)
//
// Output -> half-res A16B16G16R16F (A8R8G8B8 fallback) RT:
//   out.rgb = scene (linear prefilter downsample), out.a = encoded CoC 0..1
//
// Depth fail-open mirrors SSAO.hlsl: never-written INTZ (<= 1e-6) stays IN
// FOCUS (no blur) instead of exploding to full near-field CoC; sky (>= 0.999)
// linearises to farPlane -> full FAR CoC (far field blurs the sky, which is
// what a distance DOF is expected to do).
//
// SM3.0: no bitwise ops, every divide epsilon-guarded (NaN renders as a black
// fullscreen quad), 5 texture fetches (1 scene + 4 depth), fixed tap count,
// no loops.

sampler2D sceneTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 dofParams : register(c0); // (focusDistance, focusRange, maxBlur, unused)
uniform float4 dofCam    : register(c1); // (nearPlane, farPlane, 1/halfW, 1/halfH)

// Window depth (0..1) -> eye-space metres: lin = n*f / (f - d*(f - n)).
// Same guarded form as MotionBlur_Burnout.hlsl's INTZ linearisation.
float LinearizeDepth(float d, float n, float f)
{
    float denom = max(f - d * (f - n), 1e-7);
    return (n * f) / denom;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float focus = dofParams.x;
    float range = max(dofParams.y, 1e-7); // guard: INI may load 0 (menu clamps >= 0.1)
    // dofParams.z = maxBlur: reserved, see header.
    float nearP = dofCam.x;
    float farP  = max(dofCam.y, nearP + 1e-7);
    float2 texel = dofCam.zw;

    // Scene prefilter: linear sample of the front buffer (half-res RT does the
    // downsample through the LINEAR sampler).
    float3 scene = tex2D(sceneTex, uv).rgb;

    // 4 depth taps at the centres of the 2x2 full-res footprint this half-res
    // texel covers: +-0.25 of a half-res texel = +-0.5 of a full-res texel.
    // Plain UV (raw branch-free fetches; INTZ linear filtering is not
    // guaranteed, so explicit taps average the footprint either way).
    float2 dOff = texel * 0.25;
    float d0 = tex2D(depthTex, uv + float2(-dOff.x, -dOff.y)).r;
    float d1 = tex2D(depthTex, uv + float2( dOff.x, -dOff.y)).r;
    float d2 = tex2D(depthTex, uv + float2(-dOff.x,  dOff.y)).r;
    float d3 = tex2D(depthTex, uv + float2( dOff.x,  dOff.y)).r;
    float dAvg = (d0 + d1 + d2 + d3) * 0.25;

    // Linearise (no texture fetch below, branch-safe). Fail-open to zero CoC
    // (stay sharp) on never-written depth (dAvg <= 1e-6) AND on an invalid
    // near plane (nearP <= 0: lin would collapse toward 0 and smear the whole
    // frame as extreme near field).
    float lin;
    if(dAvg <= 1e-6 || nearP <= 0.0)
        lin = focus;
    else
        lin = LinearizeDepth(dAvg, nearP, farP);

    // Extreme-select near/far CoC: both channels 0..1, keep the extreme one
    // with its sign (equivalent to saturate((lin - focus)/range) clamped -1..1,
    // but the split is explicit so the FG/BG contract stays readable).
    float farC  = saturate((lin - focus) / range);
    float nearC = saturate((focus - lin) / range);
    float signedCoC = (farC >= nearC) ? farC : -nearC;

    // Clamp before RT write (chain guard rule); encode keeps alpha in 0..1.
    return float4(saturate(scene), saturate(0.5 + 0.5 * signedCoC));
}
