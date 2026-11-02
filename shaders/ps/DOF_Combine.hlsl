// DOF_Combine.hlsl - final DOF composite (ps_3_0)
// SkyGFX Plus - PostFX DOF lane, pass 4 of 4 (CoC -> bokeh -> tent -> combine)
//
// Full-frame mix of the sharp scene with the tent-filtered bokeh, driven by
// the half-res CoC: blend = saturate(coc*coc) * maxBlur, where coc is the
// MAGNITUDE of the 0.5-centred signed CoC (abs(a*2-1)). Squaring keeps small
// CoCs nearly transparent so the in-focus zone blends seamlessly into the
// blurred field; maxBlur (dofMaxBlur, 0..1, default 0.5) is the overall
// strength/mix ceiling - the near/far information itself is already carried
// in the s2 CoC alpha (signed encoding), which is why the C++ lane uploads no
// separate near/far constants to this pass (m0061 fixer note "DOF Combine c0
// needs near/far" is satisfied by the encoded CoC, not by extra registers).
//
// Register contract (postfx.cpp DrawDOF :3007-3009 binds EXACTLY,
// FxFramePass: s0 = pRasterFrontBuffer via colorfilterVerts):
//   s0 = scene colour (front buffer, plain UV - same convention as the landed
//        Bloom_Composite frame pass)
//   s1 = half-res tent blur (LINEAR/CLAMP)
//   s2 = half-res CoC buffer (LINEAR/CLAMP, a = encoded signed CoC)
//   c0 = (maxBlur, 1/w, 1/h, 0)
//        maxBlur : dofMaxBlur 0..1 - mix/strength ceiling (default 0.5)
//        1/w, 1/h: full-res texel size uploaded by C++ but UNUSED here - the
//                  composite needs no per-texel offsets (plain-UV linear
//                  fetches only), reserved for future dof taps
//        0       : unused
//
// Output -> camera raster: float4(mixed, 1.0) (replaces the frame; blend is
// disabled ONE/ZERO in FxFramePass).
//
// Tap count: 3.
//
// SM3.0: no bitwise ops, no divides (lerp + saturate only), fixed taps.

sampler2D sceneTex : register(s0);
sampler2D blurTex  : register(s1);
sampler2D coCTex   : register(s2);

uniform float4 combineParams : register(c0); // (maxBlur, 1/w, 1/h, unused)

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float maxBlur = saturate(combineParams.x); // dofMaxBlur strength ceiling
    // combineParams.yz = (1/w, 1/h), unused (see header).

    float3 sharp = tex2D(sceneTex, uv).rgb;
    float3 blur  = tex2D(blurTex,  uv).rgb;

    // Magnitude of the 0.5-centred signed CoC: 0 in focus .. 1 extreme
    // (near or far - both fields mix the same way; FG scatter was already
    // handled asymmetrically in DOF_Bokeh).
    float signedCoC = tex2D(coCTex, uv).a * 2.0 - 1.0;
    float coc = abs(signedCoC);

    float blend = saturate(coc * coc) * maxBlur;

    // Clamp before write (chain guard rule).
    return float4(saturate(lerp(sharp, blur, blend)), 1.0);
}
