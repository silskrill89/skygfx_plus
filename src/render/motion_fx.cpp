// ---------------------------------------------------------------------------
// motion_fx.cpp — motion FX family PARENT (wind animation application +
// randomness/physics family integration).
//
// PROVENANCE (2026-11-09 motion-family restructure):
//   The wind animation was a junior-era bolt-on: a monolith in treeWind.cpp
//   next to the unrelated TreeAlpha system, wired ad-hoc into whatever pipe
//   needed it. It is now a family that participates in the randomness and
//   physics systems instead of sitting on top of them:
//     this TU (PARENT) — the animation application + orchestration:
//       TreeWind_ApplyToTransform / TreeWind_OnEntity /
//       TreeWind_ModifyMatrixForTreeInWind / TreeWind_UploadWindExt (public
//       API unchanged, declared in treeWind.h), the ONE motion clock
//       accessor (MotionFxClockSeconds) and the family integration seams.
//     wind.cpp (CHILD) — the pure wind field/gust/variation math (the
//       CEntity::ModifyMatrixForTreeInWind port), byte-identical, no family
//       knowledge. Consumed via the Wind_ClockMs / Wind_ComputeLean seams
//       (local-extern idiom — postfx.cpp:39; there is no pipelinecommon.h
//       for this pair).
//     treeWind.cpp     — keeps only TreeAlpha (unrelated lighting/alpha).
//   FAMILY INTEGRATION (the point of the restructure):
//     * randomness: the per-material gust-amplitude band is derived from the
//       noise service (NoiseMaterialVariation, noise.h) — no private RNG, no
//       private noise state (see motionFx_noiseAmp below).
//     * physics: ONE motion clock for the family (MotionFxClockSeconds —
//       choice + pause-safety documented in motion_fx.h) and strictly
//       read-only coupling to softbody / ragdoll / water_swell: wind never
//       consumes or writes physics state, so gusts cannot desync the water
//       clock the way a second wall-clock anchor could.
//   Moved-code deltas (the ONLY edits to the moved bodies — both are
//   call-site renames): TreeWind_ApplyToTransform and TreeWind_OnEntity now
//   call the parent's motionFx_lean (child math + noise band) instead of the
//   child's static treeWind_computeLean. Everything else is byte-identical;
//   receipts ([N5]) stayed with TreeAlpha in treeWind.cpp.
// ---------------------------------------------------------------------------

// REGISTER CONTRACT (unchanged — authoritative mirror lives in treeWind.h):
//
//   VS c21 windPos / c22 windIntensity      existing, setWindParams (buildingPipe)
//   VS c23 windExt   = { heightScale, gustPhase, gustAmp, enable }   (was W8-2)
//
#include "skygfx.h"
#include "treeWind.h"
#include "motion_fx.h"
#include "texdefs.h"	// TexDefsGetDefinedShader — per-atomic EDED slot read
#include "noise.h"		// randomness family — NoiseMaterialVariation (noise lane)
#include <math.h>

// ---------------------------------------------------------------------------
// Cross-TU externs
// ---------------------------------------------------------------------------
extern CVector2D windPos;			// main.cpp — advanced once per frame from
									// CTimer__ms_fTimeStep (pause-safe clock)
// wind.cpp (family CHILD) — internal seams, local-extern idiom (postfx.cpp:39).
// Deliberately not in a header: the pure-math child stays invisible to every
// TU but its parent.
extern float Wind_ClockMs(void);	// the ONE motion clock, game-ms domain
extern void Wind_ComputeLean(RpAtomic *atomic, float *outX, float *outY);
// wind.cpp — cdecl wrapper around CVisibilityPlugins::GetModelInfoIndex
// (0x732250); used here only as the per-material identity for the noise band.
extern short CVisibilityPlugins__GetModelInfoIndex(RpAtomic *atomic);

// ---------------------------------------------------------------------------
// Family clock (see motion_fx.h for the documented one-clock decision)
// ---------------------------------------------------------------------------
float
MotionFxClockSeconds(void)
{
	return Wind_ClockMs() * (1.0f/1000.0f);
}

// ---------------------------------------------------------------------------
// Family integration — randomness (noise.h)
// ---------------------------------------------------------------------------
// The gust amplitude gets a per-material variation from the noise family
// instead of any private RNG: NoiseMaterialVariation(modelId, 0, 0) is the
// material's static variation anchor (noise-lane contract, added by the noise
// lane to noise.h in the same gate build; [0,1] scalar per the noise.h
// "scalar generators, output in [0,1]" house convention), mapped onto a
// tight +-25% amplitude band around the child math's lean. Direction is
// untouched (pure scalar) and the +-0.25 rad sanity clamp in the consumers
// still bounds the result, so the band cannot catapult a mesh. materialId is
// the tree's model index — the same identity the game's own per-model
// seeding used. No variation when the model-info plugin never ran for the
// atomic (idx < 0): amp 1.0, byte-identical child behaviour.
static float
motionFx_noiseAmp(RpAtomic *atomic)
{
	short idx = CVisibilityPlugins__GetModelInfoIndex(atomic);
	if(idx < 0) return 1.0f;
	float v = SkyNoise::NoiseMaterialVariation((unsigned)idx, 0.0f, 0.0f);
	return 0.75f + 0.5f * v;
}

// Parent-side lean: the child math (Wind_ComputeLean — byte-identical port)
// plus the randomness-family amplitude band. This is the ONLY delta vs the
// moved bodies — both consumers call this instead of the child directly.
static void
motionFx_lean(RpAtomic *atomic, float *outX, float *outY)
{
	Wind_ComputeLean(atomic, outX, outY);
	float amp = motionFx_noiseAmp(atomic);
	*outX *= amp;
	*outY *= amp;
}

// ---------------------------------------------------------------------------
// Render-time tilt (THE GAP: PBR + GTAIV building paths have no wind VS)
// ---------------------------------------------------------------------------
//
// pipeGetComposedTransformMatrix stores combined = (W*V*P)^T (RwToD3DMatrix
// transposes the row-vector RW matrix, the view/proj are transposed too, and
// HLSL then reads the upload as column-major — so the shader's math matrix is
// W*V*P in row-vector form). A local-space tilt must therefore be applied as
//
//     combined' = Mtilt * W * V * P      (math, row-vector)
//               = combined_cpu * Mtilt^T  (memory, what we have in hand)
//
// i.e. post-multiply the stored 3x3 by the COLUMN-vector tilt rotation. The
// translation column (indices 3/7/11) and the homogeneous row are left alone,
// which is exactly the "rotate about the model origin" the game gets by
// overwriting the modelling matrix's at vector — trees are authored base-at-
// origin, so the trunk stays planted and only the canopy leans.
//
void
TreeWind_ApplyToTransform(RpAtomic *atomic, float *transform16)
{
	if(!atomic || !transform16) return;
	// Ownership rule: this helper only compensates paths that were asked for
	// per-vertex wind (WIND) but have no wind VS to deliver it. Everything else
	// is the game's (TreeWind_OnEntity) — never stack the two.
	if((DefinedVertexShader)TexDefsGetDefinedShader(atomic) != DefinedVertexShader::WIND)
		return;

	float leanX, leanY;			// image of local +Z = (leanX, leanY, 1)
	motionFx_lean(atomic, &leanX, &leanY);
	if(leanX == 0.0f && leanY == 0.0f) return;
	// Sanity band: the game's own amplitudes stay under 0.06 rad; a garbage
	// CWeather__WindDir must not be able to catapult a mesh.
	if(leanX >  0.25f) leanX =  0.25f;
	if(leanX < -0.25f) leanX = -0.25f;
	if(leanY >  0.25f) leanY =  0.25f;
	if(leanY < -0.25f) leanY = -0.25f;

	// Small-angle inverse of "image of +Z = (leanX, leanY, 1)".
	// AUDIT FIX (post-crash): the surviving code solved this against the
	// wrong matrix. The math matrix pre-multiplied onto the WVP is
	// R_local = R^T (block comment above: stored' = stored · R), so the
	// row-vector image of +Z is row 2 of R^T = column 2 of the built
	// R = Ry(b)·Rx(a):
	//   image(+Z) = (sin b · cos a, -sin a, cos b · cos a)
	//   =>  a = -leanY,  b = leanX
	// (the old `a = leanY, b = -leanX` solved row 2 of R itself, which
	// tilts the tree EXACTLY OPPOSITE to the game's at.x = leanX write in
	// TreeWind_OnEntity — verified against the stock routine at 0x534E90.)
	float a = -leanY;
	float b =  leanX;
	float ca = cosf(a), sa = sinf(a);
	float cb = cosf(b), sb = sinf(b);

	// Column-vector rotation R = Ry(b) * Rx(a). Post-multiplying the stored
	// (transposed) combined by R == pre-multiplying the shader's math matrix
	// by its transpose — see the block comment above.
	float R[3][3] = {
		{ cb,  sb * sa, sb * ca },
		{ 0.0f, ca,    -sa      },
		{ -sb, cb * sa, cb * ca },
	};

	float T[3][3], O[3][3];
	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 3; j++)
			T[i][j] = transform16[i*4 + j];

	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 3; j++){
			float sum = 0.0f;
			for(int k = 0; k < 3; k++)
				sum += T[i][k] * R[k][j];
			O[i][j] = sum;
		}

	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 3; j++)
			transform16[i*4 + j] = O[i][j];
	// transform16[3], [7], [11] (translation) and [12..15] untouched.
}

// ---------------------------------------------------------------------------
// Game-side entry — CEntity::ModifyMatrixForTreeInWind replacement body
// ---------------------------------------------------------------------------
void __fastcall
TreeWind_ModifyMatrixForTreeInWind(CEntity *self)
{
	// __fastcall => `this` arrives in ECX, which is exactly thiscall's register
	// for a void method with no stack arguments, so the InjectHook(0x534E90,..)
	// jump (DEFERRED — main.cpp) lands here with no trampoline. Behavioural
	// contract: the game's original is REPLACED, not wrapped, so there is never
	// a second sway pass on top of ours.
	if(!self) return;
	// Tree models are CAtomicModelInfo, so the entity's rw object IS the atomic;
	// anything else (clump models) has no single at-vector to lean and is
	// deliberately left alone (CEntity::m_pRWObject @ +0x18, gta.h).
	RwObject *obj = self->m_pRWObject;
	if(!obj) return;
	if(obj->type != (RwUInt8)rpATOMIC) return;
	TreeWind_OnEntity((RpAtomic*)obj);
}

void
TreeWind_OnEntity(RpAtomic *atomic)
{
	if(!atomic) return;
	// Skygfx's own wind owns WIND-flagged atomics (VS sway on PS2/Xbox,
	// render-time tilt on PBR/GTAIV) — do not add a matrix sway on top.
	if((DefinedVertexShader)TexDefsGetDefinedShader(atomic) == DefinedVertexShader::WIND)
		return;

	RwFrame *frame = RpAtomicGetFrame(atomic);
	if(!frame) return;
	RwMatrix *m = RwFrameGetMatrix(frame);		// frame->modelling (macro, rwcore.h:3979)
	RwMatrix *ltm = RwFrameGetLTM(frame);
	if(!m || !ltm) return;

	float leanX, leanY;
	motionFx_lean(atomic, &leanX, &leanY);
	if(leanX >  0.25f) leanX =  0.25f;
	if(leanX < -0.25f) leanX = -0.25f;
	if(leanY >  0.25f) leanY =  0.25f;
	if(leanY < -0.25f) leanY = -0.25f;

	// ABSOLUTE write, never an accumulate — the stock routine does the same
	// (it reassigns at.x/at.y from scratch every call), which is what keeps the
	// value stable when the hook fires for an entity whose base matrix we have
	// never observed. at.z is left exactly as authored (same as the game).
	m->at.x = leanX;
	m->at.y = leanY;

	// Push modelling -> ltm. Verified against the stock routine: 0x534E90
	// ends with `call 0x7F0910` = RwFrameUpdateObjects (gta.cpp:194), i.e.
	// the game re-derives the LTM right here too — same call, same frame,
	// so the render-time LTM consumers see the leaned tree immediately.
	RwFrameUpdateObjects(frame);
}

// ---------------------------------------------------------------------------
// VS c23 (windExt)
// ---------------------------------------------------------------------------
void
TreeWind_UploadWindExt(void)
{
	float ext[4];
	// heightScale: 1/treeHeight. 0.12 => full amplitude at ~8m, which is the
	// middle of SA's tree range (3..15m) and, because it is applied as
	// saturate(z * scale), it degrades gracefully: a model whose origin is not
	// at its base simply bends less, and .w==0 disables the mask entirely.
	ext[0] = 0.12f;
	// Slow global gust phase (windPos advances ~0.015 units per game-ms, so
	// *0.05 gives a ~8s period) and amplitude in the units of the existing
	// `dist` term (windIntensity is 2.0..3.5 there).
	ext[1] = (windPos.x + windPos.y) * 0.025f;
	ext[2] = 0.35f * CWeather__Wind;
	ext[3] = 1.0f;					// enable — see the c23.w contract
	RwD3D9SetVertexShaderConstant(23, ext, 1);
}
