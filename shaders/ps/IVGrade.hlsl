// IVGrade.hlsl - GTA IV style colour grade (ps_3_0)
//
// WHAT THIS IS
//   Single full-screen grade pass used by DrawIVGrade() in
//   src/render/postfx_colourfilter.cpp. In instruction order it applies:
//     exposure (display-referred multiply)
//     -> Rec.601 luma split
//     -> desaturation at HALF the slider value
//     -> per-channel pow(c, 1/gamma)
//     -> 4-tap +/-4 px high-pass "bloom" (hard 0.75 pedestal, added after gamma)
//     -> radial vignette (radius + contrast)
//   Effective chain (audit 2026-10-25, §1.2):
//     exposure -> luma split -> desat(1/2) -> pow(1/gamma) -> +high-pass
//     -> *vignette-gain
//
// PROVENANCE
//   This file is a reconstruction of the shipped, source-less bytecode array
//   g_ivGradePSBytes (src/render/postfx_colourfilter.cpp:1084-1316). The
//   disassembly-derived specification is:
//     docs/plans/2026-10-25-ivgrade-audit.md  §1.2 (stages P1-P24), §1.3, §4.1
//   Compiling this source with fxc 9.29.952.3111 (June 2010 SDK),
//     -T ps_3_0 -E main
//   reproduces the shipped instruction budget exactly:
//     67 slots = 5 texture + 62 arithmetic.
//
// RUNTIME STATUS
//   The runtime still binds the embedded byte array. This source is not wired
//   into the build yet; embedding is a follow-up (audit F9 / Phase 1):
//   compile to a CSO (proposed resource id 285), replace EnsureIVGradePS()'s
//   raw CreatePixelShader with the standard CSO loader, then delete
//   g_ivGradePSBytes. Do NOT add this file to Resource.rc / resource.h before
//   that change lands. Until then the runtime path is unchanged.
//
// REGISTER / SAMPLER BUDGET (audit §4.1)
//   c0 : gradeParams0 = (exposure, desaturation, gamma, 0)
//   c1 : gradeParams1 = (vignetteIntensity, vignetteRadius,
//                        vignetteContrast, bloomIntensity)
//   c2 : gradeParams2 = (1/screenWidth, 1/screenHeight, 0, 0)
//   s0 : sceneTex      = camera-sized frame copy (s_ivGradeRas)
//   Nothing else. c3..cN in the disassembly are compiler-generated def
//   immediates, not host uploads. Do not relocate these constants to c22/c23
//   (frozen to pipeUploadPBR) or to c49-c52/s9 (noise system reservation).
//
// CPU UPLOAD CONTRACT - postfx_colourfilter.cpp DrawIVGrade(), stage H15
//   c0 = { exDisplay, ivDesaturation, gm, 0 }
//     gm        = (ivGamma   > 0.05) ? ivGamma   : 1.0
//     exDisplay = pow((ivExposure > 0.01) ? ivExposure : 1.0, 1/2.2)
//   The linear->sRGB (1/2.2) encode is applied CPU-SIDE (audit F1), because
//   this pass runs on a display-referred frame. The shader therefore treats
//   gradeParams0.x as an already-display-encoded exposure and must NOT apply
//   pow() to it again. The old s_ivExpCap clamp is gone (audit F2).
//   c1 = { ivVignetteIntensity, ivVignetteRadius, ivVignetteContrast,
//          ivBloomIntensity }
//   c2 = { 1/max(w, 1e-7), 1/max(h, 1e-7), 0, 0 }
//
// VANILLA QUIRKS PRESERVED (do not "fix" them in this reconstruction)
//   * Desaturation is HALF the slider: saturate(0.5 * gradeParams0.y).
//     That is the shipped behaviour (audit P14 / D8); F11 changes it later.
//   * Bloom and vignette are CROSS-GATED (audit P21/P23 / D11):
//       - bloom is discarded when ivVignetteIntensity <= 0.001;
//       - the vignette multiply is discarded when ivBloomIntensity <= 0.001.
//     These are vanilla quirks, not bugs to "fix" here. F10 removes them in a
//     separate change.
//   * The "bloom" is a local high-pass, not a blur: four cardinal taps at
//     exactly +/-4 px, mean - 0.75, clamped at zero, added AFTER gamma.
//   * Length is measured in UV space, so the vignette follows the aspect
//     ratio instead of being pixel-circular (same as the shipped shader).
//
// SM3.0 SAFETY
//   No loops, no bitwise operations, no tex2Dlod. The only divisions are
//   1/radius (guarded by the >= 0.01 compare) and 1/gamma (gamma clamped to
//   >= 0.05). pow() bases are max(..., 0) and exponents are positive. The
//   uv == 0.5 centre relies on rsq(0)/rcp yielding 0 (no NaN), exactly like
//   the shipped bytecode. The final colour is explicitly saturated to [0,1]:
//   on the shipped 8-bit UNORM target this is identical to the hardware
//   clamp, and it costs no extra slots (cmp -> cmp_sat).

sampler2D sceneTex : register(s0);

float4 gradeParams0 : register(c0); // (exposure, desaturation, gamma, -)
float4 gradeParams1 : register(c1); // (vigIntensity, vigRadius, vigContrast, bloomIntensity)
float4 gradeParams2 : register(c2); // (1/w, 1/h, -, -)

float4 main(float2 uv : TEXCOORD0) : COLOR
{
	// ------------------------------------------------------------------
	// P1-P6: vignette mask
	//   d   = length(uv - 0.5)                 UV space, aspect-following
	//   k   = 1/radius, or 100 when radius < 0.01
	//   t   = saturate(k * d)
	//   f   = 1 - t^2 * (3 - 2t) = 1 - smoothstep(t)
	//   v   = max(f, 0) ^ max(contrast, 0.01)
	//   V   = max(0, 1 + intensity * (v - 1))
	// ------------------------------------------------------------------
	float2 d = uv - 0.5;
	float dist = length(d);

	// Guarded division: radius >= 0.01 ? 1/radius : 100 (hard limit).
	float invRadius = (gradeParams1.y >= 0.01) ? (1.0 / gradeParams1.y) : 100.0;
	float t = saturate(dist * invRadius);

	float falloff = 1.0 - t * t * (3.0 - 2.0 * t);
	float mask = pow(max(falloff, 0.0), max(gradeParams1.z, 0.01));
	float vig = max(0.0, 1.0 + gradeParams1.x * (mask - 1.0));

	// ------------------------------------------------------------------
	// P7-P9: 4-tap +/-4 px high-pass "bloom"
	//   Offsets are exactly 4 pixels, supplied as 1/screenW, 1/screenH.
	//   residual = mean4 - 0.75 (hard pedestal in display space)
	//   B = residual >= 0 ? residual * bloomIntensity : 0
	// ------------------------------------------------------------------
	float2 texel = 4.0 * gradeParams2.xy;

	float2 uvL = uv - float2(texel.x, 0.0);
	float2 uvD = uv - float2(0.0, texel.y);
	float2 uvR = uv + float2(texel.x, 0.0);
	float2 uvU = uv + float2(0.0, texel.y);

	float3 taps = tex2D(sceneTex, uvL).rgb
	            + tex2D(sceneTex, uvD).rgb
	            + tex2D(sceneTex, uvR).rgb
	            + tex2D(sceneTex, uvU).rgb;

	float3 residual = taps * 0.25 - 0.75;
	float3 bloom = (residual >= 0.0) ? (residual * gradeParams1.w) : 0.0;

	// ------------------------------------------------------------------
	// P10-P15: exposure + Rec.601 luma split + desaturation
	//   E = exposure <= 0.01 ? 1 : exposure   (defensive; the CPU already maps
	//       <= 0.01 to 1.0 before the 1/2.2 encode, so this only fires if a
	//       future caller uploads the raw slider instead)
	//   c = E * rgb                           display-referred multiply
	//   L = 0.299R + 0.587G + 0.114B          Rec.601, on the exposed colour
	//   s = saturate(0.5 * desaturation)      half the slider value (vanilla)
	//   g = c + s * (L - c)                   = lerp(c, L, s)
	// ------------------------------------------------------------------
	float exposure = (gradeParams0.x <= 0.01) ? 1.0 : gradeParams0.x;
	float3 c = exposure * tex2D(sceneTex, uv).rgb;
	float luma = dot(c, float3(0.299, 0.587, 0.114));
	float desat = saturate(0.5 * gradeParams0.y);
	float3 graded = c + (luma.xxx - c) * desat;

	// ------------------------------------------------------------------
	// P16-P19: per-channel pow(c, 1/gamma)
	//   gamma is clamped to >= 0.05 on the GPU too, even though the CPU
	//   fallback already replaces <= 0.05 values with 1.0.
	//   The identity branch when |gamma - 1| <= 0.001 is explicit in the
	//   shipped bytecode; preserve it.
	// ------------------------------------------------------------------
	float gamma = max(gradeParams0.z, 0.05);
	float3 encoded = pow(max(graded, 0.0), 1.0 / gamma);
	float3 colour = (abs(gamma - 1.0) <= 0.001) ? graded : encoded;

	// ------------------------------------------------------------------
	// P20-P24: add bloom AFTER gamma, then vignette
	//   o = B + g'
	//   if (vignetteIntensity <= 0.001) o = g'       <- vanilla cross-gate
	//   result = V * o
	//   if (bloomIntensity    <= 0.001) result = o   <- vanilla cross-gate
	//   alpha = 1
	// ------------------------------------------------------------------
	float3 gradedPlusBloom = colour + bloom;
	if (gradeParams1.x <= 0.001)
		gradedPlusBloom = colour; // vanilla quirk: zero vignette kills bloom

	float3 result = gradedPlusBloom * vig;
	if (gradeParams1.w <= 0.001)
		result = gradedPlusBloom; // vanilla quirk: zero bloom kills vignette

	return float4(saturate(result), 1.0);
}
