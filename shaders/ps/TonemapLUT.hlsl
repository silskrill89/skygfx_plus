// TonemapLUT.hlsl - Operator tonemap branch (ps_3_0)
//
// Drop-in sibling of TonemapPass.hlsl: the SAME s0/s1 + c5..c8 contract, the
// SAME downstream chain (exact sRGB OETF -> PostGrade -> FilmicGrading ->
// black lift -> PivotCurve), with the legacy Hable/Uncharted2 curve replaced by
// one of five operators selected by c9.x. Switching tonemapOperator therefore
// changes only the tone curve - every timecycle/user control keeps working.
//
// REGISTER / SAMPLER CONTRACT — read back from src/render/postfx.cpp (this is
// the authoritative binding, not the lane brief):
//   s0 tex        graded linear-HDR front buffer. Bound by
//                 rwRENDERSTATETEXTURERASTER = pRasterFrontBuffer
//                 (ColourFilter_Modern :1674, re-asserted for the tonemap draw
//                 at :1773) — same as TonemapPass.
//   s1 lumTex     frame luminance readback, ONLY bound when auto-exposure is on
//                 (:1985-1991, cleared at :2018). Sampled only when c5.w>=0.5,
//                 exactly like TonemapPass — so an unbound s1 is never read.
//   c5 tonemapParams {exposure, toeStrength, sceneLuma, autoExposureFlag} :1911
//   c6 gradeParams    {brightness, contrast, lift, curveBlend}            :1915
//   c7 blackLiftParams{blackLift, minExposure, maxExposure, keyStrength}  :1937
//   c8 curveParams    {curveLows, curveHighs, curveMid, 0}                :1980
//   c9 operatorParams {operator 1..5, 0, 0, 0}                            :2006
//                 (NOT c0 — the lane brief said c0.x; postfx uploads the
//                 operator at register 9, and c0..c2 hold the grading tint
//                 matrices for pass 1 at :1722-1724. c9 wins.)
//   c0..c2        NOT read here (gradingPS pass-1 tint).
//
// NO LUT TEXTURE: postfx binds no LUT sampler for this pass, so the operators
// in ../include/LUTOps.hlsl are evaluated ANALYTICALLY per pixel — there is no
// tex2D of a LUT anywhere in this file, only the s0 scene sample and the
// conditional s1 luminance sample. Reported to the orchestrator as a contract
// mismatch against "TonemapLUT" naming (a real LUT sampler would need a new
// bind in postfx.cpp, which this lane does not own).
//
// INTENTIONAL DEVIATION from TonemapPass: SoftKnee() is NOT applied. SoftKnee
// pre-compresses >1.0 highlights with log() purely so the Hable curve (useless
// above ~5) keeps some rolloff; every operator here ingests full HDR (clamped
// to LUT_MAX_HDR = 64) and ships its own shoulder. Keeping it would double-
// compress and reshape the operators' highlight rolloff.
//
// SM3.0: entry main, ps_3_0, sampler2D/tex2D only, uniform (c9) branch for
// operator selection, all runtime divisions floored with max(x, 1e-7).

#include "../include/LUTOps.hlsl"

uniform sampler2D tex : register(s0);
uniform sampler2D lumTex : register(s1);
uniform float4 tonemapParams : register(c5);
uniform float4 gradeParams : register(c6);
uniform float4 blackLiftParams : register(c7);
uniform float4 curveParams : register(c8);
uniform float4 operatorParams : register(c9);

struct PS_INPUT
{
	float3 texcoord0 : TEXCOORD0;
};

#include "../include/TonemapCommon.hlsl"

float4 main(PS_INPUT IN) : COLOR
{
	float3 c = tex2D(tex, IN.texcoord0.xy);

	// === Unified exposure — identical to TonemapPass: timecycle supplies
	// COLOUR, the frame supplies BRIGHTNESS ===
	float exposure = tonemapParams.x;
	float sceneLuma = tonemapParams.z;
	if (tonemapParams.w >= 0.5)
	{
		// CryEngine-style auto-exposure (mode 2): scene key from measured luminance.
		// s1 is bound by postfx.cpp only on this path, so the unbound stage is
		// never sampled (uniform branch on c5.w).
		float lum = max(tex2D(lumTex, float2(0.5, 0.5)).r, 1e-4);
		float key = 1.03 - 2.0 / (2.0 + log2(lum + 1.0));
		float autoExp = clamp(key / lum, blackLiftParams.y, blackLiftParams.z);
		exposure *= lerp(1.0, autoExp, saturate(blackLiftParams.w));
		sceneLuma = saturate(lum * 2.0);
	}
	c *= exposure;

	// HDR ceiling before the operator (AGENTS: clamp before log). No SoftKnee
	// here — each operator owns its highlight rolloff (see header).
	c = clamp(c, 0.0, LUT_MAX_HDR);

	// Operator select (c9.x = 1..5). Uniform branch — see LUT_ApplyOperator.
	c = saturate(LUT_ApplyOperator(c, operatorParams.x));

	// Exact sRGB gamma encode
	c = saturate(LinearToSRGB(c));

	// Downstream chain mirrored bit-for-bit from TonemapPass. NOTE: both
	// helpers below are fed tonemapParams.z (c5.z) — TonemapPass reads the
	// register, not its auto-exposure-updated local sceneLuma, so the two
	// paths grade identically under auto-exposure.
	c = PostGrade(c, tonemapParams.z);
	c = FilmicGrading(c, tonemapParams.z);

	// Black-level lift: preserve deep shadow detail without a gray veil.
	c = c * (1.0f - blackLiftParams.x) + blackLiftParams.x;

	// User pivot curve — final OUTPUT stage (after black lift).
	c = PivotCurve(c);

	return float4(c, 1.0f);
}
