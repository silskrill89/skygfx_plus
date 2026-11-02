// =============================================================================
// TreeAlpha.hlsl — console/PC tree lighting + alpha-test override (lane W8-2)
//
// Profile: ps_3_0 (fast_build compiles every shaders/ps/*.hlsl at ps_3_0 with
// entry `main` — no build-script change needed for this file).
//
// WHAT THIS IS
//   Port of FusionFix's OverrideTreeAlpha / PREF_TREEALPHA + PREF_TREE_LIGHTING
//   (source/shaders.ixx:509-520 and :548-567) into skygfx's forward building
//   pipe. FF's shader side is, in gta_treesPS1.asm:
//       mul_sat r0.w, r0.w, c221_abs.w      // saturate(alpha * alphamul)
//       add  r20.x, r0.w, -c2.y             // ... vs threshold
//       cmp  ...                            // cutout gate
//   with c221.w = the profile's alphamul (PC 0.625 / PC+ 1.0 / Console 4.0)
//   and c223.x = the tree-lighting profile (PC 0 / PC+ 1 / Console 2).
//
// HOW THE GATE IS SPLIT (see treeWind.cpp TreeAlpha_ApplyAlphaRef / _Upload)
//   * The CPU moved the fixed-function ALPHAREF by 1/alphamul
//     (ref' = round(baseRef/mul)). Because the FFP alpha test runs on THIS
//     shader's output alpha, `a_out >= ref'/255` is the same inequality as
//     FF's `saturate(a_out * mul) >= baseRef/255` — the cutout moves exactly
//     like FF's, in EVERY building pipe (PBR/IV included, where this PS is
//     never bound).
//   * The clip below is the redundant half: it re-evaluates FF's own
//     expression — scaled alpha vs the ORIGINAL (unscaled) ref — so a pass
//     that resets ALPHAREF mid-cb still gets FF's threshold. Both halves
//     express the same inequality, so they can never disagree by more than
//     the ref rounding (<=1 LSB).
//   * Output alpha stays UNSCALED (unlike FF, which blends with the scaled
//     alpha): tree translucency in the blend path keeps today's look —
//     deliberate deviation, documented in treeWind.h.
//
// LIGHTING PROFILES
//   profile 0 (PC)   : pass-through (this file would not even be bound —
//                      TreeAlpha_Active only admits profile >= 2).
//   profile 1 (PC+)  : pass-through (same lighting as PC in FF's forward
//                      equivalent; PC/PC+ differ only via alphamul on the CPU).
//   profile 2 (Console): FF's "Console Tree Lighting" block (half-lambert
//                      wrap + tree ambient desaturation) lives in IV's
//                      deferred_lightingPS1 — there is no deferred renderer
//                      in this forward pipe, so the visible outcome is
//                      approximated: flatten the vertex lighting toward its
//                      luminance, lift it a little toward unlit, and add a
//                      desaturated ambient floor so canopies stay readable
//                      at night. Constants are named tunables below (first
//                      launch tunes them by eye; nothing here runs unless
//                      the user picks the Console profile).
//
// REGISTERS (contract in treeWind.h)
//   s0          diffuse (same stage as simplePS)
//   c0.x        colorScale   (PS2/Xbox convention — applied to rgb AND alpha,
//                             exactly like buildingPipePS main_simple)
//   c1          treeAlpha = { alphamul, alphaRef(0..1 original), profile,
//                             enable }      — uploaded by TreeAlpha_Upload()
//   c24         ambient rgb  (buildingAmbient) — console profile floor
//   COLOR0      VS-lit vertex colour (day/night blend * matCol + ambient)
//
// Interface is byte-compatible with buildingPipePS main_simple (same input
// semantics, same `float4 ... : COLOR` output), so it is a drop-in swap.
// =============================================================================

sampler2D        tex0       : register(s0);
float4           colorScale : register(c0);	// .x only (simplePS convention)
float4           treeAlpha  : register(c1);	// {alphamul, alphaRef, profile, enable}
float4           ambient    : register(c24);	// rgb = buildingAmbient

// Console-profile tunables (opt-in only — see header).
#define kConsoleFlat	0.50	// lerp of the vertex lighting toward its luminance
							// (0 = keep PC shading, 1 = fully flat)
#define kConsoleLift	0.15	// lerp toward fullbright (1,1,1) — console trees
							// read brighter than the PC prelight
#define kConsoleAmb		0.20	// desaturated ambient floor weight (night
							// readability; mirrors FF's ambient desat)

struct PS_INPUT {
	float3 texcoord0 : TEXCOORD0;
	float4 color     : COLOR0;
};

float4 main(PS_INPUT IN) : COLOR
{
	float4 tex = tex2D(tex0, IN.texcoord0.xy);

	// ---- alpha-test override (FF: mul_sat r0.w, r0.w, c221_abs.w) -------
	// Compared BEFORE lighting so discarded pixels skip the rest.
	// alphaRef (treeAlpha.y) is the ORIGINAL FFP threshold as 0..1; the CPU
	// already scaled the device ref by 1/alphamul to the same inequality.
	if(treeAlpha.w > 0.5){
		float aOut = tex.a * IN.color.a * colorScale.x;	// = simplePS output alpha
		if(saturate(aOut * treeAlpha.x) < treeAlpha.y)
			discard;
	}

	float4 vcol = IN.color;

	// ---- console tree lighting (profile 2) ----
	if(treeAlpha.z > 1.5){
		float3 luma = float3(0.299, 0.587, 0.114);
		float  lum  = dot(vcol.rgb, luma);
		// 1. flatten the shading contrast (FF's ambient-desat effect, applied
		//    to the whole lit colour since this pipe has no separate ambient
		//    term left by the time the PS runs)
		vcol.rgb = lerp(vcol.rgb, lum.xxx, kConsoleFlat);
		// 2. lift toward unlit + desaturated ambient floor
		float ambLum = dot(ambient.rgb, luma);
		vcol.rgb = saturate(lerp(vcol.rgb, float3(1.0, 1.0, 1.0), kConsoleLift)
		                    + ambLum * kConsoleAmb);
	}

	// main_simple's body: tex * color * colorScale.x (alpha too)
	return tex * vcol * colorScale.x;
}
