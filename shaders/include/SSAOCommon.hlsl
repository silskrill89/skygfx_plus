// SSAOCommon.hlsl - shared SSAO helper(s), extracted VERBATIM (L14 dedupe).
// Included by the SSAO family from shaders/ps/ via:
//     #include "../include/SSAOCommon.hlsl"
//
// Line endings: LF (matches the LF consumers SSAO.hlsl / SSAO_Temporal.hlsl).
//
// Contents:
//   ProjectToUV - byte-identical in SSAO.hlsl and SSAO_Temporal.hlsl; moved
//   verbatim from SSAO.hlsl (the primary definition site). Its doc comment is
//   the SSAO.hlsl wording; SSAO_Temporal.hlsl's paraphrase was dropped (code
//   identical).
//
// NOT extracted (divergent copies stay local per-file; see the L14 note in
// docs/plans/2026-10-27-stages-0-3-checklist.md):
//   GetViewPos - SSAO_Temporal.hlsl carries a signed-denominator-guarded fork;
//   SSAO.hlsl / SSAO_VertexDepth.hlsl / SSAO_ps20.hlsl carry the unguarded
//   original. Copies differ -> left in place per extraction rule.
//   BuildTBN   - SSAO_Temporal.hlsl's is 2-arg (noise scale derived from the
//   globals); SSAO.hlsl / SSAO_ps20.hlsl pass noiseScale in (3-arg). Copies
//   differ -> left in place per extraction rule.
//
// Usage contract:
//   - Include AFTER the consumer's `uniform float4 projInfo : register(c2);`
//     declaration: the helper references the consumer's projInfo global.
//   - This file declares NO registers/samplers. Register layout is owned by the
//     consumers and must not move (s5/s6 Forward+ aliasing note lives in
//     SSAO_Temporal.hlsl and stays authoritative there).
//   - SM3.0 (ps_3_0) only.

#ifndef SSAO_COMMON_HLSL
#define SSAO_COMMON_HLSL

// Exact inverse of GetViewPos: view-space position -> screen UV.
// GetViewPos does: ndc = uv*2-1; ndc.y = -ndc.y; viewXY = ndc * viewZ * projInfo.xy
// so the reverse must divide by projInfo.xy AND un-flip y. The old code did
// neither (plain samplePos.xy / samplePos.z), so every occlusion sample was
// fetched from a mirrored, projection-scaled wrong texel — garbage occlusion
// everywhere (the other primary cause of the black AO buffer).
float2 ProjectToUV(float3 viewPos)
{
    float invZ = 1.0 / max(viewPos.z, 1e-7);
    float nx = viewPos.x * invZ / max(projInfo.x, 1e-7);
    float ny = viewPos.y * invZ / max(projInfo.y, 1e-7);
    return float2(nx * 0.5 + 0.5, 0.5 - ny * 0.5);
}

#endif // SSAO_COMMON_HLSL
