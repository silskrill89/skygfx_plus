// Mirror_Composite.hlsl — GTA SA CMirrors composite (mirror distortion fix)
// ps_3_0, entry `main`. Loaded by src/render/mirrorPass.cpp (makePS,
// IDR_MIRROR_COMPOSITE = 277 — 276 is W7-D's IDR_WATER_UNDERWATER) and drawn
// over the game's CMirrors::RenderMirrorBuffer fullscreen quad when
// SkyGfx/mirrorDistortionFix is on.
//
// PORT OF: GTAIV.EFLC.FusionFix shaders/GTAIV.EFLC.FusionShaders/
// win32_30_nv8/mirror/mirrorPS1.asm — "fixed distorted mirror reflections
// thanks to nukie":
//
//     rcp  r0.x, v0_abs.w
//     mul  r0.x, r0.x, c0.y          ; c0.y = -0.5
//     mad  r0.xy, v0, r0.x, c0.w     ; c0.w =  0.5   ->  uv = 0.5 - 0.5*v0/w
//     texld r0, r0, s0               ; ReflectTextureSampler
//     mul  oC0.xyz, r0, c39.y        ; globalScalars.y colour scale
//     mov  oC0.w, c46.x              ; gInvColorExpBias alpha
//
// The IV fix samples the mirror render target from the fragment's projected
// screen position with an explicit per-pixel reciprocal-w, instead of trusting
// a vertex-interpolated [0..1] UV pair — the latter warps the image whenever
// the composite quad's mapping and the buffer's projection are not exactly
// affine (wide FOV / non-4:3 aspect / a 3D-positioned mirror quad). SA's
// composite (CMirrors::RenderMirrorBuffer else-branch) is the same class of
// draw: a screen quad that stretches CMirrors::pBuffer (1024x512 or 512x256)
// over the viewport with fixed UVs.
//
// SA CONTRACT (mirrorPass.cpp MirrorPass_Composite):
//   s0      = CMirrors::pBuffer (the mirror render target, bound through the
//             rw RENDERSTATETEXTURERASTER cache — RenderLumaPass idiom)
//   v0.xy   = NDC of the FINAL screen position ([-1..1], y down). The rw Im2D
//             vertex format hands ps_3_0 only the texcoord pair, so the quad
//             carries the projected position THERE and the reciprocal-w
//             operand rides c0.x. c0.x is the quad's clip w (constant 1.0 for
//             SA's screen-aligned composite) — the divide stays live in the
//             math exactly like the asm, so a future 3D/Im3D mirror quad with
//             varying w composites through the same path without a shader
//             change.
//   c0      = (rcpW, colourScale, alpha, 0)
//   c1      = (0.5, 0.5, 0, 0)
//
// Mapping: u = 0.5 - 0.5*ndcX*rcpW (U flipped — mirror handedness, matches the
// game's own UV set {1,0}..{0,0}), v = 0.5 + 0.5*ndcY*rcpW (V identity).

sampler2D MirrorTexSampler : register(s0);
float4    c0                : register(c0);
float4    c1                : register(c1);

struct PSIn
{
	float2 ndc : TEXCOORD0;
};

float4
main(PSIn i) : COLOR
{
	// mirrorPS1.asm: rcp on |w|, floored (division safety — NaN renders
	// black on SM3.0 and never recovers).
	float w = abs(c0.x);
	float rcpW = 1.0f / (w > 1e-7f ? w : 1e-7f);

	float2 ndc = i.ndc * rcpW;
	float2 uv;
	uv.x = c1.x - c1.x * ndc.x;	// 0.5 - 0.5*ndcX  (U flip)
	uv.y = c1.y + c1.y * ndc.y;	// 0.5 + 0.5*ndcY

	float4 t = tex2D(MirrorTexSampler, uv);
	return float4(t.rgb * c0.y, c0.z);
}
