// GrassInstanced.hlsl - batched plant/grass pixel shader (ps_3_0)
//
// Used by CGrassInstancer (PC_GrassInstancer.cpp) when the embedded
// resources/cso/GrassInstanced.cso is available (deferred resource.h /
// Resource.rc embedding diff — see .serena/agent-reports/w78-grass-instancer.md).
// Until that lands the instancer draws through the fixed-function Im3D path and
// this file is compiled+validated by tools/fast_build.py but never loaded.
//
// Inputs come from the CPU-expanded Im3D vertex pool:
//   COLOR0   = final per-instance diffuse (plant colour x ambient x locTri
//              lighting x per-plant intensity variance), already folded on the
//              CPU by CGrassInstancer::DrawTriPlant — the legacy path put this
//              in the RpMaterial colour instead (per-plant material churn).
//   TEXCOORD0= plant card UVs taken from the model geometry.
//
// Deliberately uses NO c-constant: stale constants left bound by an earlier
// pass must never be able to gate or zero plant output (a half-wired shader
// that depends on an un-uploaded c0 is a black-grass trap). Alpha test is a
// device render state (rwRENDERSTATEALPHATESTFUNCTION) applied after this
// shader, exactly like the legacy material path.
//
// SM3.0 only (ps_3_0 / vs_3_0), no SM4/5 features. Guard: no unnormalised
// divides anywhere (1e-7 floor rule).
//
// BUILD CONTRACT (static review; fxc not run this lane):
//   - auto-compiled by tools/fast_build.py get_shader_list() (globs shaders/ps/*.hlsl,
//     this file is in NO multi-entry/exclusion list) -> resources/cso/GrassInstanced.cso,
//     entry 'main', profile ps_3_0. No fast_build.py edit needed.
//   - embedding into the DLL needs the DEFERRED resource.h/Resource.rc diff
//     (IDR_GRASSINSTANCED — see checkpoint for the chosen free ID).
//
// FOG CAVEAT — read before ever wiring the PS on:
//   CPlantMgr::Render sets rwRENDERSTATEFOGENABLE=TRUE and the default path is
//   fixed-function Im3D, which applies vertex fog correctly. When a custom
//   ps_3_0 is bound instead, the fog coefficient (oFog from the fixed-function
//   VS) is NOT readable by ps_3_0 — grass would lose distance fog. Before the
//   IDR wiring lands, either keep the PS off (recommended: the FFP path is
//   already visually complete for texture x vertexDiffuse) or extend this
//   shader + CGrassInstancer::SubmitChunk to fold fog on the CPU (upload
//   fogColor/fogParams as c0 at every submit — never leave them stale).

sampler2D plantTex : register(s0);

float4 main(float2 uv : TEXCOORD0, float4 diffuse : COLOR0) : COLOR0
{
    float4 texel = tex2D(plantTex, uv);

    // Texture x per-instance colour. Hard-zero alpha for fully transparent
    // texels so alpha-test/blend behave identically to the legacy path
    // (never expose RGB under a 0 alpha).
    float4 outc;
    outc.rgb = texel.rgb * diffuse.rgb;
    outc.a   = texel.a   * diffuse.a;

    if(outc.a <= 0.0)
        outc = float4(0.0, 0.0, 0.0, 0.0);

    outc.a = saturate(outc.a);
    return outc;
}
