// =============================================================================
// treeWind.cpp — tree wind sway extension + FF tree lighting/alpha (lane W8-2)
//
// REGISTER CONTRACT (authoritative — mirrored in treeWind.h):
//
//   VS c21 windPos / c22 windIntensity      existing, setWindParams (buildingPipe)
//   VS c23 windExt   = { heightScale, gustPhase, gustAmp, enable }   NEW (this lane)
//   PS c1  treeAlpha = { alphamul, alphaRef, profile, enable }       NEW (this lane)
//   PS c24 ambient   = buildingAmbient                               reused here
//
//   PS c22/c23 stay owned by pipeUploadPBR (PS constant space — no overlap with
//   the VS c22/c23 above).
//
// OWNERSHIP / NO-DOUBLE-SWAY RULE
//   * DefinedVertexShader::WIND atomics are skygfx's: the wind VS sways them
//     on the PS2/Xbox paths, TreeWind_ApplyToTransform sways them on the two
//     paths that have no wind VS (PBR cb + PS2 cb's IV shader swap).
//   * every other atomic is the game's: TreeWind_OnEntity (the
//     CEntity::ModifyMatrixForTreeInWind replacement) sways it.
//   The two gates are mutually exclusive, so the halves never stack — including
//   the case where the game hook is never installed (TreeWind_OnEntity simply
//   does not run; nothing here changes behaviour until main.cpp installs it).
// =============================================================================
#include "skygfx.h"
#include "treeWind.h"
#include "texdefs.h"	// TexDefsGetDefinedShader — per-atomic EDED slot read
#include <math.h>

// ---------------------------------------------------------------------------
// Cross-TU externs
// ---------------------------------------------------------------------------
extern CVector2D windPos;			// main.cpp — advanced once per frame from
									// CTimer__ms_fTimeStep (pause-safe clock)
extern RwRGBAReal buildingAmbient;	// buildingPipe.cpp — ambient for PS c24
// buildingPipe.cpp:231 — cdecl wrapper around CVisibilityPlugins::GetModelInfoIndex
// (0x732250; plugin-sdk declares it `static short ... (RpAtomic*)`, i.e. cdecl).
WRAPPER short CVisibilityPlugins__GetModelInfoIndex(RpAtomic *atomic) { EAXJMP(0x732250); }

// ---------------------------------------------------------------------------
// Handles / knobs
// ---------------------------------------------------------------------------
// Loaded ONLY by the deferred CreateShaders line (pipelinecommon.cpp + the
// resource IDs — see the checkpoint's DEFERRED DIFFS). NULL until then, which
// makes every TreeAlpha_Active() consumer fall through to today's behaviour.
void *treeAlphaPS;

// FF profiles (FusionFix shaders.ixx:509-520). Defaults are the exact
// no-op state: alphamul 1.0 leaves ALPHAREF untouched and profile 0 leaves the
// PS output alone, so the feature is inert even after the CSO load lands.
float TreeAlpha_alphamul = 1.0f;	// OverrideTreeAlpha / TreeFxText multiplier
int   TreeAlpha_profile  = 0;		// 0=PC (ePC), 1=PC+ (ePCPlus), 2=Console (eConsole)
bool  TreeAlpha_enable   = true;	// master switch (INI wiring deferred)

// ---------------------------------------------------------------------------
// Model info lookup
// ---------------------------------------------------------------------------
// Same pattern as envmap.cpp:198 (CModelInfo::ms_modelInfoPtrs recovered from
// the immediate operand of the game's own accessor at 0x4C5960).
static CBaseModelInfo **treeWind_modelInfoPtrs(void)
{
	static bool init = false;
	static CBaseModelInfo **ptrs = NULL;
	if(!init){
		init = true;
		ptrs = *(CBaseModelInfo***)(0x4C5960 + 3);
	}
	return ptrs;
}

static CBaseModelInfo *
treeWind_modelInfo(RpAtomic *atomic)
{
	if(!atomic) return NULL;
	short idx = CVisibilityPlugins__GetModelInfoIndex(atomic);
	if(idx < 0) return NULL;			// -1 = plugin never set for this atomic
	// AUDIT HARDENING: ms_modelInfoPtrs has 0x4E20 (20000) entries — the
	// game's own accessor bounds the same way (`cmp ecx, 0x4E20` at
	// 0x4C5971, verified in gta_sa.exe). The plugin field is an int16, so a
	// stale/garbage value could index up to 32767 and turn TreeWind's flag
	// read into a wild pointer; clamp it the way the game does.
	if(idx >= 0x4E20) return NULL;
	CBaseModelInfo **ptrs = treeWind_modelInfoPtrs();
	if(!ptrs) return NULL;
	return ptrs[idx];
}

bool
TreeWind_IsTreeAtomic(RpAtomic *atomic)
{
	CBaseModelInfo *mi = treeWind_modelInfo(atomic);
	if(!mi) return false;
	// CBaseModelInfo::m_flags sits at +0x12 (gta.h static_assert 0x20 layout).
	// The 4-bit nSpecialType field occupies bits 11..14 of that uint16, so
	// TREE = 0x0800, PALM = 0x1000 and the game's SwaysInWind() (0x4212C0) is
	// exactly `(m_flags & 0x1800) != 0`. Verified against the two accessors
	// that share the field: bDrawLast (bit 1) and gta-reversed's own
	// IsSwayInWind1/2 comments (0x0800 / 0x1000).
	return (mi->m_flags & 0x1800) != 0;
}

// ---------------------------------------------------------------------------
// Wind math — port of CEntity::ModifyMatrixForTreeInWind (SA 0x534E90,
// gta-reversed Entity.cpp:1348) with the same table the game and the in-project
// PC_PlantsMgr port use (PC_PlantsMgr.cpp:358 keeps `extern float WindTabel[]`;
// the 16 values below are CWeather::saTreeWindOffsets from Weather.cpp:54).
// ---------------------------------------------------------------------------
static const float s_windTable[16] = {
	1.0f, 0.5f, 0.2f, 0.7f, 0.4f, 1.0f, 0.5f, 0.3f,
	0.2f, 0.1f, 0.7f, 0.6f, 0.3f, 1.0f, 0.5f, 0.2f
};

// Clock. CTimer__ms_fTimeStep is in milliseconds and reads 0 while paused, so
// accumulating it once per RW render frame reproduces CTimer::GetTimeInMS()
// for our purposes without needing that global's address. The frame guard
// keeps the accumulation at exactly one step per frame no matter how many
// atomics are visited (TreeWind_OnEntity is per-entity, the tilt helper is
// per-atomic).
static float s_windTimeMs;
static RwUInt32 s_windTimeFrame = (RwUInt32)-1;

static float
treeWind_clock(void)
{
	RwUInt32 fr = RWSRCGLOBAL(renderFrame);
	if(fr != s_windTimeFrame){
		s_windTimeFrame = fr;
		s_windTimeMs += CTimer__ms_fTimeStep;
		// AUDIT (integer overflow): treeWind_computeLean converts this clock
		// with (uint32_t)(t * 8.0f) and (uint32_t)t — a float->uint conversion
		// is undefined once the value reaches 2^32 (t*8, ~6 days) / 2^32 (t,
		// ~49 days). Wrap at 2^24 ms: every consumer masks low bits
		// (& 0x0FFF / & 0xFFFF) and 2^24 is a multiple of both 4096 and
		// 65536, so the wrap is phase-invisible to the wind math.
		if(s_windTimeMs >= 16777216.0f)
			s_windTimeMs -= 16777216.0f;
	}
	return s_windTimeMs;
}

// Lean of the trunk's up axis: image of local +Z = (x, y, 1), radians.
// Magnitude mirrors the game: calm |wind| < 0.2 -> +-0.005, 0.2..0.5 -> +-0.008
// (0.005*1.6), >= 0.5 -> 0.015 * (1 + table lerp) * wind.
static void
treeWind_computeLean(RpAtomic *atomic, float *outX, float *outY)
{
	float t = treeWind_clock();
	// SEED DEVIATION vs stock (documented, deliberate): gta_sa.exe 0x534E90
	// seeds the >=0.5 table branch from m_nModelIndex ([esi+0x20] — the same
	// word the routine then uses to index ms_modelInfoPtrs, verified by
	// disassembly) so every instance of one model sways IN PHASE, and the
	// calm branch from `this`. This port seeds BOTH from the atomic pointer:
	// same shape (stable per-instance 16-bit-ish noise), phase-accurate to
	// the millisecond clock above, and visually a little more organic than
	// the stock in-phase march. Nothing else in the math depends on it.
	uint32_t seed = (uint32_t)(uintptr_t)atomic;
	float k;
	if(CWeather__Wind >= 0.5f){
		uint32_t ticks = seed + (uint32_t)(t * 8.0f);			// game: <<3
		uint32_t i1 = ((ticks & 0xFFFF) >> 12) & 0x0F;
		uint32_t i2 = (i1 + 1) & 0x0F;
		float inter = (float)(ticks & 0x0FFF) * (1.0f/4096.0f);
		float windValue = 1.0f + (1.0f - inter) * s_windTable[i1]
		                        + inter * s_windTable[i2];
		k = 0.015f * windValue * CWeather__Wind;
	}else{
		uint32_t off = (seed + (uint32_t)t) & 0x0FFF;			// 4096ms period
		k = sinf(off * (6.28f/4096.0f)) * 0.005f;
		if(CWeather__Wind >= 0.2f) k *= 1.6f;
	}
	// Palms keep a permanent downwind lean (IsSwayInWind2 / 0x1000, game adds
	// it unconditionally before the direction multiply).
	CBaseModelInfo *mi = treeWind_modelInfo(atomic);
	if(mi && (mi->m_flags & 0x1000) != 0)
		k += CWeather__Wind * 0.03f;

	*outX = k * CWeather__WindDir.x;
	*outY = k * CWeather__WindDir.y;
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
	treeWind_computeLean(atomic, &leanX, &leanY);
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
	treeWind_computeLean(atomic, &leanX, &leanY);
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

// ---------------------------------------------------------------------------
// Tree alpha — FF OverrideTreeAlpha / TreeFxText profiles
// ---------------------------------------------------------------------------
static bool s_alphaApplied = false;
static int  s_alphaBaseRef = 1;

static bool
treeAlpha_isAlphaActive(RpAtomic *atomic)
{
	if(!TreeAlpha_enable) return false;
	if(atomic == NULL) return false;
	return TreeWind_IsTreeAtomic(atomic);
}

bool
TreeAlpha_Active(RpAtomic *atomic)
{
	// bind gate: alpha-profile gate PLUS the CSO actually being loaded.
	// AUDIT REFINEMENT: plus "the profile actually needs the PS".
	//   profile 0/1 (PC/PC+) differ only by TreeAlpha_alphamul, which the
	//   CPU side (TreeAlpha_ApplyAlphaRef) implements alone, in EVERY pipe,
	//   with no PS at all. Binding the PS there would only duplicate the
	//   clip and cost the tree its detail/stochastic PS
	//   (buildingPipe_selectPS). So the PS is bound for Console (profile 2,
	//   lighting) only; PC/PC+ stay CPU-only. Same effect, zero regression
	//   on the default profile (0 => never binds => inert).
	if(!treeAlphaPS) return false;
	if(TreeAlpha_profile < 2) return false;
	return treeAlpha_isAlphaActive(atomic);
}

// INI/profile setter (wiring deferred to main.cpp — see the checkpoint's
// DEFERRED DIFFS). fx: 0=Off 1=PC 2=PC+ 3=Console. Values mirror FusionFix's
// TreeFxText (shaders.ixx:509-520): PC alphamul 0.625, PC+ 1.0, Console 4.0.
// Off/defaults keep {1.0, 0} = exact no-op: ALPHAREF untouched, PS never
// bound, lighting pass-through — inert until an INI/menu writes a real value.
void
TreeAlpha_SetProfile(int fx)
{
	switch(fx){
	case 1:	// PC — stricter cutout (FF ePC)
		TreeAlpha_profile  = 0;
		TreeAlpha_alphamul = 0.625f;
		break;
	case 2:	// PC+ — today's threshold (FF ePCPlus)
		TreeAlpha_profile  = 1;
		TreeAlpha_alphamul = 1.0f;
		break;
	case 3:	// Console — dense canopy, flat/bright lighting (FF eConsole)
		TreeAlpha_profile  = 2;
		TreeAlpha_alphamul = 4.0f;
		break;
	default:// Off (and anything unknown)
		TreeAlpha_profile  = 0;
		TreeAlpha_alphamul = 1.0f;
		break;
	}
}

// main.cpp grassRenderCallback convention (main.cpp:747-777): pre-seeded
// snapshot, sanitised range, restored on exit. The caller has already snapshotted
// `baseRef` through buildingPipe_saveRenderState, so the restore path here only
// has to undo what THIS function pushed.
//
// Scaling direction: FF gates on `saturate(tex.a * alphamul) >= ref`. The PS
// emits the unscaled alpha (so the FFP blend path keeps today's translucency)
// and the CPU moves the fixed-function ref instead — same inequality, one
// threshold, both halves agreeing (see TreeAlpha.hlsl).
void
TreeAlpha_ApplyAlphaRef(RpAtomic *atomic, int baseRef)
{
	s_alphaApplied = false;
	if(!treeAlpha_isAlphaActive(atomic)) return;

	float mul = TreeAlpha_alphamul;
	if(!(mul > 0.0f)) mul = 1.0f;
	if(mul == 1.0f){				// exact no-op: nothing to push, nothing to undo
		s_alphaBaseRef = baseRef;
		return;
	}

	float scaled = (float)baseRef / mul;
	int ref = (int)(scaled + 0.5f);
	if(ref < 1) ref = 1;			// ref==0 + GREATEREQUAL accepts EVERY texel
	if(ref > 255) ref = 255;
	// AUDIT FIX: record the base ref BEFORE the "nothing to push" early-out
	// (the surviving code returned first, leaving s_alphaBaseRef stale —
	// TreeAlpha_Upload would then ship the PREVIOUS base ref into PS c1.y
	// and the clip gate would compare against the wrong threshold; hit when
	// rounding lands ref == baseRef, e.g. baseRef 1 with any alphamul).
	s_alphaBaseRef = baseRef;
	if(ref == baseRef) return;		// rounded back to the base: nothing to push

	s_alphaApplied = true;

	// L1 (rw cache) -> L2 (driver cache) -> L3 (raw device).
	// AUDIT FIX: the surviving comment claimed "no raw L3 write here" while
	// the code below DID raw-write D3DRS_ALPHAREF — the comment and the code
	// disagreed. The code is the correct half: the L2 flush is applied[]-
	// gated, so a device sitting behind an unchanged cache would keep the
	// base ref for the whole callback without this write. The raw write is
	// safe ONLY because the pairing is unconditional — every callback that
	// calls Apply also calls Restore (see treeWind.h), so the device always
	// lands back on baseRef even when no draw ran in between.
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
}

void
TreeAlpha_RestoreAlphaRef(void)
{
	if(!s_alphaApplied) return;
	s_alphaApplied = false;
	int ref = s_alphaBaseRef;
	if(ref < 1) ref = 1;
	if(ref > 255) ref = 255;
	// Symmetric push — the raw L3 write is safe ONLY because this restore is
	// unconditional (every callback that applied also restores), so a device
	// whose applied[] was never advanced still gets the correct value.
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
}

void
TreeAlpha_Upload(void)
{
	if(!treeAlphaPS) return;

	float c1[4];
	c1[0] = TreeAlpha_alphamul > 0.0f ? TreeAlpha_alphamul : 1.0f;
	// The PS clip gate uses the ORIGINAL ref: it scales the alpha, the CPU
	// scaled the FFP ref, both must compare against the same threshold.
	c1[1] = (float)s_alphaBaseRef * (1.0f/255.0f);
	c1[2] = (float)TreeAlpha_profile;
	c1[3] = 1.0f;
	RwD3D9SetPixelShaderConstant(1, c1, 1);

	float amb[4] = { buildingAmbient.red, buildingAmbient.green,
	                 buildingAmbient.blue, 0.0f };
	RwD3D9SetPixelShaderConstant(24, amb, 1);
}
