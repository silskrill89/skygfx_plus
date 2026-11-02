// softbody.cpp - native RW-physics softbody/ragdoll core.
//
// P1 of docs/plans/2026-10-29-rw-physics-ragdoll-re.md (section 5). Ports the
// shipped BoneNode_c layer (gta-reversed BoneNode_c.cpp, addresses 0x616B30-
// 0x6177B0) + Mark Nicholson's 2003 prototype integrator (GTASource
// Ragdoll.cpp:176-248) into one self-contained TU with a TexDefs-style
// lifecycle. NO hooks, NO patches - main.cpp wires the four Softbody* calls
// (8060 / 5458 / 3961 / 8821).
//
// P1 lane spec (2026-10-30) mapped to doc gaps:
//   G1 stride  - consumes the FIXED 0x18 AnimBlendFrameData (sftbdyphsx.h,
//                capture bits 1/2) and validates it against the live exe
//                before any capture write (R2 probe: per-entry BoneTag trust
//                check + one-shot aggregate dbglog; refuse-to-capture on
//                mismatch - a wrong stride would scatter bitfield writes
//                through random memory).
//   G2 conflict- resolved by GATING to dead peds (CPed+0x530 == 66) + capture
//                bits 1/2 at activation: the anim system stops writing the
//                captured nodes, so the BlendKeyframe slerp owns the interp
//                frame and UpdateRpHAnim delivers it. Living peds never get
//                an instance (R8). State 65 (dying) stays excluded: ground
//                collision now exists (G8-native below), but capturing
//                mid-death-anim would also freeze the ROOT frame's
//                translation, and bit 3 (HasVelocity) extracts the ped's
//                movement from that root translation - the entity would stop
//                mid-fall and float. Fixing that needs a root-frame capture
//                exemption (anim-owned root, physics limbs) + playtest ->
//                deferred to P2, documented in softbody.h.
//   G6 space   - native write-back is LOCAL by construction (interp frame).
//                The Bullet lane's world-space write-back fix ships as
//                SoftbodyWorldToBoneLocal() + the wiring report.
//   G8 ground  - game ground-Z collision (CWorld::FindGroundZForCoord
//                0x569660, doc 2.4): velocity reflection + tangential
//                friction on contact, and a rotation-based positional lift -
//                translations are anim-owned (capture bit 2 freezes interp
//                frame t; only orientations reach the skin), so a penetrating
//                bone is lifted by rotating its PARENT orientation, the
//                native analog of Bullet positional correction that actually
//                renders. 1 root anchor query + per-bone queries only inside
//                the near-ground band (airborne bones skip).
//   Integrator - per-bone linear velocity (gravity, 3-axis friction G5) ->
//                axis = cross(parentToNodeVec, vel), |axis| = angle ->
//                parent space via RtQuatReciprocal + RtQuatTransformVectors ->
//                RtQuatRotate on the parent's local orientation quat ->
//                shipped Limit() (G3, head-Z coupling) -> subtree CalcWldMat
//                refresh FROM the rotated parent.
//
// Reuse of sftbdyphsx (not duplication - plan 4.2 keep-list):
//   - aBONETAG_ENUM_TAB (sftbdyphsx.cpp:59) - the 32 eBoneTag values, verified
//     match to shipped Enums\eBoneTag.h.
//   - tBoneInfo + ms_boneInfos@0x8D26D0 parent links.
//   - AnimBlendFrameData / CAnimBlendClumpData / ANIMBLENDCLUMPFROMCLUMP from
//     the FIXED 0x18-stride sftbdyphsx.h (capture bits 1/2, plan G1).
//   - RAGDOLL_MAX_BONES / RAGDOLL_DIST_HIGH constants.
// The old living Ragdoll_c/RagdollManager_c in sftbdyphsx stay untouched
// (their retirement - main.cpp:5502-5503 gating - is the main.cpp lane's
// wiring item; until then the two systems must not both target one ped).
//
// CPU-side only; zero shader impact. All game-memory access is plain pointer
// math on validated pool/hierarchy pointers (same exposure as the old
// ProcessAllPeds walk); POD-only types, static pool, stack scratch - no heap
// in the per-frame path.

#include "../skygfx.h"
#include "softbody.h"
#include "sftbdyphsx.h"		// fixed AnimBlendFrameData (0x18), tBoneInfo, aBONETAG_ENUM_TAB, ANIMBLENDCLUMPFROMCLUMP, RAGDOLL_* constants
#include "../rw/gta.h"		// CTimer__ms_fTimeStep, Scene, CVector
#include "../rw/rphanim.h"	// RpHAnimHierarchy, RpHAnimIDGetIndex, ...
#include "../core/physicscfg.h"	// [SkyPhysics] softbodyBackend / softbodyBudget

#include <cmath>

// Game entry points baked into the exe (same extern "C" block pattern as
// sftbdyphsx.cpp:34-43; gta.cpp provides the extern "C" WRAPPER definitions).
extern "C" {
RpHAnimHierarchy *GetAnimHierarchyFromSkinClump(RpClump *clump);
RwInt32 RpHAnimIDGetIndex(RpHAnimHierarchy *hierarchy, RwInt32 ID);
RwBool RtQuatConvertFromMatrix(RtQuat *qpQuat, const RwMatrix *mpMatrix);
RtQuat *RtQuatRotate(RtQuat *quat, const RwV3d *axis, RwReal angle, RwOpCombineType combineOp);
RwV3d *RtQuatTransformVectors(RwV3d *vectorsOut, const RwV3d *vectorsIn, const RwInt32 numPoints, const RtQuat *quat);
void RtQuatSetupSlerpCache(RtQuat *qpFrom, RtQuat *qpTo, RtQuatSlerpCache *sCache);
}

// CWorld::FindGroundZForCoord - 0x569660 (gta-reversed World.cpp:2567,
// `float FindGroundZForCoord(float x, float y)`; returns 20.0f when nothing
// is hit - guarded at the call site). Game collision query per doc 2.4; no
// gta.cpp WRAPPER exists, so the TU-local address call follows the
// sftbdyphsx.cpp:571 macro pattern (gta.cpp is not this lane's file).
typedef float (*FindGroundZForCoord_t)(float x, float y);
#define SOFTBODY_FINDGROUNDZ(x, y) ((FindGroundZForCoord_t)0x569660)((x), (y))

// ---- Tunables (native death ragdoll, doc 5.2/5.6 phase 1) ----
#define SOFTBODY_MAX_RAGDOLLS	12			// static pool (R3 budget headroom; live cap = softbodyBudget)
#define SOFTBODY_BUDGET_DEFAULT	8			// physicscfg.h softbodyBudget default
#define SOFTBODY_GRAVITY		9.81f		// prototype Ragdoll.cpp:183
#define SOFTBODY_FRICTION		0.98f		// prototype :246, ALL axes (G5 fix)
#define SOFTBODY_BLEND_START	0.25f		// crossfade-in start, ramps to 1.0
#define SOFTBODY_BLEND_RAMP		2.5f		// blend units per second (full ownership in ~0.3 s)
#define SOFTBODY_MIN_AXIS_SPEED	0.01f		// prototype :205 gate
#define SOFTBODY_EPS			1e-7f		// project division-safety floor
#define SOFTBODY_SETTLE_DOT		0.99996f	// |quat dot| ~ < 0.57 deg/frame = pose stable
#define SOFTBODY_SETTLE_TIME	1.0f		// seconds of stability before settle (death-lane pattern)
#define SOFTBODY_SETTLE_MAX		10.0f		// hard settle timeout - bounds per-instance cost
#define SOFTBODY_PROBE_MIN		8			// R2: min trusted BoneTag matches to accept the layout

// Ground collision (G8-native, doc 4.4 phase 1). Bone radius approximates the
// Bullet lane's capsule radii (ragdoll_death.cpp 0.06-0.15); the lift gain /
// per-frame clamp keep the correction gentle enough to settle instead of
// oscillating (penetration per resting frame is ~vel*dt*dt millimetres).
#define SOFTBODY_BONE_RADIUS		0.08f	// bone collision radius (m)
#define SOFTBODY_GROUND_BAND		1.5f	// query bones within this height above the root anchor ground
#define SOFTBODY_GROUND_RESTITUTION	0.25f	// vertical bounce factor on contact
#define SOFTBODY_GROUND_FRICTION	0.80f	// tangential velocity damp on contact
#define SOFTBODY_GROUND_ROT_GAIN	2.0f	// lift radians per metre of penetration
#define SOFTBODY_GROUND_ROT_MAX		0.30f	// max lift angle per frame (rad)
#define SOFTBODY_GROUND_PEN_MAX		0.25f	// penetration clamp per contact (m)

// Water layer (G-water; buoyancy + wave bob, task 2026-10-30). The same
// constants drive BOTH lanes (softbody.cpp native + ragdoll_death.cpp Bullet
// via SoftbodyWaterQuery/Swell). Force model per the lane spec:
//   netUp = G * (submergedFraction * buoyancyGain - 1) + wave terms
// Directive tunables (106-116 style): buoyancyGain / dragCoeff /
// waveForceScale. buoyancyGain is the directive's 0.6-1.0 density-ratio
// knob; 1.0 (used) is the sink-proof end: a FULLY submerged body is neutral
// (it floats/hangs at the surface and can never sink through the water
// plane - the task-4 lifecycle guarantee).
#define SOFTBODY_WATER_BUOYANCY_GAIN	1.0f	// buoyancyGain: upward accel = G*frac*gain (0.6-1.0; 1.0 = sink-proof)
#define SOFTBODY_WATER_DRAG_COEFF	2.5f	// dragCoeff: velocity-proportional linear drag (1/s) at full submersion
#define SOFTBODY_WATER_WAVE_FORCE_SCALE	1.0f	// waveForceScale: master scale on both swell-driven wave terms
#define SOFTBODY_WATER_ANG_DRAG		2.0f	// angular drag (1/s) at full submersion (Bullet lane)
#define SOFTBODY_WATER_COUPLING		4.0f	// wave-velocity follow rate (1/s) at full submersion
#define SOFTBODY_WATER_SPRING		2.0f	// surface-height target drift (1/s) at full submersion
#define SOFTBODY_WATER_DRIFT_MAX	1.0f	// height-drift clamp (m) - deep-water floor guard
#define SOFTBODY_WATER_VZ_MAX		8.0f	// wave-follow correction clamp (m/s)
#define SOFTBODY_WATER_BAND		2.0f	// per-bone query band above the surface (m)

// ---- Water layer (G-water): game query + Water_VS swell law + shared clock ----

// CWaterLevel no-waves height core - VERIFIED in the live gta_sa.exe 1.0 US
// by byte inspection (PE section map + opcode dump). All task/gta-reversed
// candidates are REJECTED: 0x6E84C0 ("GetWaterLevelNoWaves") and 0x6E8460
// ("GetWaterLevel") land mid-instruction (6E8460: 8B 5C 24 5C = deep-stack
// mov inside the render builder; 6E84C0: 10 52 50 51 = orphan operand byte);
// an earlier lane's 0x6E8870/0x6E8A60 likewise; the "non-standard thunks"
// 0x541330 -> 0x409B56 are a call-COUNTER stub pair (90 E9 .. to
// inc [0xB73708]; jmp back to a RET) - not query entries. The real query
// entry is 0x6E8580 (byte signature: fld x, fmul [0x858F44]=0.002f grid
// scale per axis, cmp 12/cmp 12 sector bounds, movzx grid word
// [0xC21B70 + idx*2] - a 12x12 sector grid, 500 m cells:
// cell = floor(x*0.002+6.0); per-quad resolver 0x6E5BB0 / per-triangle
// resolver 0x6E5E90):
//   unsigned char f(float x, float y, float z, float *pLevel,
//                   float *pOpt1 /*nullable*/, float *pOpt2 /*nullable*/)
//   true  = (x,y) is inside a water quad/triangle; *pLevel = surface height.
//   false = no water: the 0.0/no-water fallback (*pLevel <- 0.0). Treated as
//           "no water" here (the bool is authoritative - a true 0.0 level is
//           legitimate sea level and must NOT be rejected).
// The waves-composing sibling is 0x6EB690 (calls this core +
// CalculateWavesForCoordinate 0x6E6EF0) and is deliberately unused: the
// rendered surface is OUR Water_VS swell on the static level, not the game's
// wave field. Return declared unsigned char: the asm leaves garbage in the
// upper EAX bits (setne al), callers must read AL only - which is exactly
// what an unsigned-char return compiles to.
typedef unsigned char (*WaterLevelCore_t)(float x, float y, float z,
	float *pLevel, float *pOpt1, float *pOpt2);
#define SOFTBODY_WATERLEVEL(x, y, z, out) \
	((WaterLevelCore_t)0x6E8580)((x), (y), (z), (out), nullptr, nullptr)

// Shared swell clock: Water_VS.hlsl c11 semantics are swTime = camTime.w *
// max(waveSwell.y, 1e-3) where camTime.w is waterPipe.cpp g_waterTime - QPC
// seconds of water rendering accumulated since waterPipe_init (main.cpp:8301).
// Anchored here at SoftbodyInit (main.cpp:8208 - same init burst, so the
// epoch gap is sub-frame). KNOWN LIMITATION (report): g_waterTime only
// advances while water flushes run, this wall clock does not - the phase
// diverges by accumulated no-water-render time (interiors). Worst case is
// half a wavelength of phase (~0.16 m vertical mismatch vs the rendered
// surface); the bob PERIOD/wavelength always match the shader exactly.
static double s_waterClockAnchor = -1.0;

static double
WaterClockSeconds(void)
{
	LARGE_INTEGER freq, cnt;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&cnt);
	double now = (double)cnt.QuadPart / (double)(( freq.QuadPart > 0) ? freq.QuadPart : 1);
	if(s_waterClockAnchor < 0.0)
		s_waterClockAnchor = now;	// lazy anchor (SoftbodyInit normally seeds it)
	return now - s_waterClockAnchor;
}

// Water_VS.hlsl c11 swell law - verbatim port of the VS block:
//   dirA = normalize(1, 0.35),  kA = 2pi/45, cA = sqrt(9.81/kA), amp 0.10
//   dirB = normalize(-0.45, 1), kB = 2pi/23, cB = sqrt(9.81/kB), amp 0.06
//   fW = kW * (dot(dirW, xy) - cW * t)
//   swell = (cos(fA)*0.10 + cos(fB)*0.06) * swAmp
// c11 is uploaded as (1, 1, 0, 0) (waterPipe.cpp:1312) so swAmp/timeScale
// are both 1.0 here. The direction normalization and the sqrt(9.81/k) wave
// speed are DERIVED at init with the same guarded math as the shader (no
// hand-typed constants - they must match the HLSL literals exactly).
struct WaterSwellWave { float dirX, dirY, k, c; };

static WaterSwellWave
WaterSwellMakeWave(float dx, float dy, float wavelength)
{
	WaterSwellWave w;
	double len = std::sqrt((double)dx * dx + (double)dy * dy);
	if(len < 1e-7) len = 1e-7;			// shader's max(length, 1e-7)
	w.dirX = (float)(dx / len);
	w.dirY = (float)(dy / len);
	w.k = (float)(6.2831853 / (double)wavelength);	// 2pi / lambda
	double kk = (double)w.k < 1e-7 ? 1e-7 : (double)w.k;
	w.c = (float)std::sqrt(9.81 / kk);		// deep-water phase speed
	return w;
}

static const WaterSwellWave s_waveA = WaterSwellMakeWave(1.0f, 0.35f, 45.0f);
static const WaterSwellWave s_waveB = WaterSwellMakeWave(-0.45f, 1.0f, 23.0f);

static inline bool
WaterFinite(float v) { return (v == v) && v > -1e30f && v < 1e30f; }

float
SoftbodyWaterSwell(float x, float y)
{
	if(!WaterFinite(x) || !WaterFinite(y)) return 0.0f;
	float t = (float)WaterClockSeconds();
	float fA = s_waveA.k * (s_waveA.dirX * x + s_waveA.dirY * y - s_waveA.c * t);
	float fB = s_waveB.k * (s_waveB.dirX * x + s_waveB.dirY * y - s_waveB.c * t);
	return (float)(std::cos((double)fA) * 0.10 + std::cos((double)fB) * 0.06);
}

float
SoftbodyWaterSwellVel(float x, float y)
{
	// d/dt cos(k*(d - c t)) = k*c*sin(f); same wave pair as above.
	if(!WaterFinite(x) || !WaterFinite(y)) return 0.0f;
	float t = (float)WaterClockSeconds();
	float fA = s_waveA.k * (s_waveA.dirX * x + s_waveA.dirY * y - s_waveA.c * t);
	float fB = s_waveB.k * (s_waveB.dirX * x + s_waveB.dirY * y - s_waveB.c * t);
	float v = (float)(std::sin((double)fA) * (double)(s_waveA.k * s_waveA.c) * 0.10 +
		std::sin((double)fB) * (double)(s_waveB.k * s_waveB.c) * 0.06);
	return WaterFinite(v) ? v : 0.0f;
}

bool
SoftbodyWaterQuery(float x, float y, float z, float *pLevel)
{
	if(!pLevel) return false;
	*pLevel = 0.0f;
	if(!WaterFinite(x) || !WaterFinite(y) || !WaterFinite(z)) return false;
	unsigned char found = SOFTBODY_WATERLEVEL(x, y, z, pLevel);
	if(!found){
		*pLevel = 0.0f;	// the 0.0/no-water fallback -> "no water"
		return false;
	}
	// plausibility guard (streaming holes / corrupt hits): SA water lives
	// well inside +/-1000 z
	if(!WaterFinite(*pLevel) || *pLevel > 1000.0f || *pLevel < -1000.0f){
		*pLevel = 0.0f;
		return false;
	}
	return true;
}

// Ped state gate (ragdoll_death.cpp:1006-1007 semantics, plugin-sdk CPed.h:468)
#define SOFTBODY_PEDSTATE_DIE	65			// dying - death anim playing (NOT captured, see G2 note)
#define SOFTBODY_PEDSTATE_DEAD	66			// dead - death anim finished/held: activation state
#define PED_OFF_STATE			0x530		// CPed.m_ePedState
#define PED_OFF_CLUMP			0x18		// CEntity.m_pRwObject (RpClump* for skinned peds)
#define PED_OFF_MOVESPEED		0x44		// CPed.m_vecMoveSpeed (CVector)
#define CLUMP_OFF_FRAME			0x04		// RpClump.object.parent -> RwFrame* root frame

// ---- Shipped BoneNode_c port + prototype m_vel (doc 5.2 SoftBone_c) ----

struct SoftBone_c {
	int32 boneTag;
	SoftBone_c *parent;
	SoftBone_c *children[8];
	int numChildren;

	AnimBlendFrameData *frameData;	// FIXED 0x18-stride entry; KeyFrame = write-back target
	uint8 savedFlags;				// Flags byte at capture time (restored on release)
	RtQuat orientation;				// physics orientation (BoneNode_c::m_Orientation)
	RtQuat prevOrientation;			// orientation at the start of this frame (settle metric)
	CVector pos;					// bind-pose local offset (m_Pos)
	RwV3d vel;						// linear velocity (prototype Ragdoll.cpp:166)
	RwMatrix worldMat;				// integrator world matrix (m_WorldMat)
	CVector limitMin, limitMax;		// euler limits, degrees (InitLimits mixing)

	void Init(int32 tag, AnimBlendFrameData *fd, const tBoneInfo *info);
	void InitLimits(const tBoneInfo *info);
	void Limit();					// shipped 0x617650 incl. head-Z coupling
	void BlendKeyframe(float blend);// shipped 0x616E30: slerp INTO interp frame
	void CalcWldMat(const RwMatrix *parentMat);	// shipped 0x616CD0

	static void EulerToQuat(const CVector &angles, RtQuat &quat);	// 0x6171F0
	static void QuatToEuler(const RtQuat &quat, CVector &angles);	// 0x617080
	static int32 GetIdFromBoneTag(int32 bone);						// 0x617050
};

// ms_boneInfos: BoneNodeManager_c::ms_boneInfos@0x8D26D0 (BoneNodeManager_c.h:12)
static tBoneInfo *SoftbodyBoneInfos(void) { return (tBoneInfo*)0x8D26D0; }

int32 SoftBone_c::GetIdFromBoneTag(int32 bone) {
	tBoneInfo *infos = SoftbodyBoneInfos();
	for (int i = 0; i < MAX_BONE_NUM; i++)
		if (infos[i].m_current == bone)
			return i;
	return -1;
}

// shipped 0x6171F0 - verbatim (degrees in/out)
void SoftBone_c::EulerToQuat(const CVector &angles, RtQuat &quat) {
	float hx = angles.x * 0.5f * (3.14159265f / 180.0f);
	float hy = angles.y * 0.5f * (3.14159265f / 180.0f);
	float hz = angles.z * 0.5f * (3.14159265f / 180.0f);
	float cr = (float)std::cos(hx), sr = (float)std::sin(hx);
	float cp = (float)std::cos(hy), sp = (float)std::sin(hy);
	float cy = (float)std::cos(hz), sy = (float)std::sin(hz);
	quat.real = cr * cp * cy + sr * sp * sy;
	quat.imag.x = sr * cp * cy - cr * sp * sy;
	quat.imag.y = cr * sp * cy + sr * cp * sy;
	quat.imag.z = cr * cp * sy - sr * sp * cy;
}

// shipped 0x617080 - port with epsilon guards (project division-safety rule;
// shipped's exact |v9|==1.0f test becomes >= 1-1e-6, and 1-v9^2 is clamped
// >= 0 before sqrt). y is computed unconditionally (atan2 is safe at v10=0,
// matching shipped); the gimbal branch only overrides x/z.
void SoftBone_c::QuatToEuler(const RtQuat &quat, CVector &angles) {
	float v9 = 2.0f * (quat.imag.x * quat.imag.z - quat.imag.y * quat.real);
	float disc = 1.0f - v9 * v9;
	if (disc < 0.0f) disc = 0.0f;
	float v10 = (float)std::sqrt(disc);

	angles.y = (float)std::atan2(2.0f * (quat.imag.y * quat.real - quat.imag.x * quat.imag.z), v10) * (180.0f / 3.14159265f);
	if (std::fabs(v9) >= 1.0f - 1e-6f) {
		// gimbal branch (shipped abs(v9)==1.0f) - no division by v10
		angles.x = (float)std::atan2(-2.0f * (quat.imag.y * quat.imag.z - quat.imag.x * quat.real),
			1.0f - 2.0f * (quat.imag.x * quat.imag.x + quat.imag.z * quat.imag.z)) * (180.0f / 3.14159265f);
		angles.z = 0.0f;
	} else {
		angles.x = (float)std::atan2(2.0f * (quat.imag.x * quat.real + quat.imag.y * quat.imag.z) / v10,
			(1.0f - 2.0f * (quat.imag.x * quat.imag.x + quat.imag.y * quat.imag.y)) / v10) * (180.0f / 3.14159265f);
		angles.z = (float)std::atan2(2.0f * (quat.imag.z * quat.real + quat.imag.x * quat.imag.y) / v10,
			(1.0f - 2.0f * (quat.imag.y * quat.imag.y + quat.imag.z * quat.imag.z)) / v10) * (180.0f / 3.14159265f);
	}
}

// shipped InitLimits 0x617490 - the (odd but shipped) Max/Min/ABC mixing.
// Doc G4: do NOT trust raw m_Min/m_Max as euler limits; mix first.
void SoftBone_c::InitLimits(const tBoneInfo *bi) {
	limitMin.x = bi->m_Max.x - bi->m_Min.x;
	limitMin.y = bi->m_Max.y - bi->m_Min.z;
	limitMin.z = bi->m_Max.z - bi->m_ABC.y;

	limitMax.x = bi->m_Min.y + bi->m_Max.x;
	limitMax.y = bi->m_ABC.x + bi->m_Max.y;
	limitMax.z = bi->m_ABC.z + bi->m_Max.z;
}

// shipped Init 0x6177B0 - capture bind pose straight out of the RpAnimBlend
// interp keyframe (doc 3.1 step 2). frameData comes from the FIXED 0x18
// m_FrameDatas array indexed by RpHAnimIDGetIndex, pre-trusted by the R2
// probe in SoftRagdoll_c::Init. savedFlags snapshots the byte so release can
// restore game-set bits exactly.
void SoftBone_c::Init(int32 tag, AnimBlendFrameData *fd, const tBoneInfo *info) {
	boneTag = tag;
	parent = nullptr;
	numChildren = 0;
	frameData = fd;
	savedFlags = fd ? fd->Flags : 0;
	vel.x = vel.y = vel.z = 0.0f;
	prevOrientation.real = 1.0f;
	prevOrientation.imag.x = prevOrientation.imag.y = prevOrientation.imag.z = 0.0f;

	if (fd && fd->KeyFrame) {
		orientation = fd->KeyFrame->q;
		pos = fd->KeyFrame->t;
	} else {
		RtQuatInitMacro(&orientation, 0.0f, 0.0f, 0.0f, 1.0f);
		pos.x = pos.y = pos.z = 0.0f;
	}

	if (info)
		InitLimits(info);
	else {
		limitMin.x = limitMin.y = limitMin.z = -45.0f;
		limitMax.x = limitMax.y = limitMax.z = 45.0f;
	}

	memset(&worldMat, 0, sizeof(RwMatrix));
}

// shipped Limit 0x617650 - QuatToEuler -> clamp X/Y -> head-specific Z
// coupling (BONE_HEAD=5: Z range warps toward the table's head Max.z as
// eulerX grows, `multy = max(|x|/-45+1, 0)`) -> EulerToQuat. (blend arg of
// shipped Limit is ignored - dropped here.)
void SoftBone_c::Limit() {
	CVector euler;
	QuatToEuler(orientation, euler);

	euler.x = (euler.x < limitMin.x) ? limitMin.x : (euler.x > limitMax.x) ? limitMax.x : euler.x;
	euler.y = (euler.y < limitMin.y) ? limitMin.y : (euler.y > limitMax.y) ? limitMax.y : euler.y;

	float clampZMin = limitMin.z;
	float clampZMax = limitMax.z;
	if (boneTag == 5) {	// BONE_HEAD
		int32 headId = GetIdFromBoneTag(5);
		if (headId >= 0) {
			float maxHeadZ = SoftbodyBoneInfos()[headId].m_Max.z;
			// fmaxf not std::max - windows.h max macro would bite (C2589)
			float multy = fmaxf(std::fabs(euler.x) / -45.0f + 1.0f, 0.0f);
			clampZMin = maxHeadZ + (limitMin.z - maxHeadZ) * multy;
			clampZMax = maxHeadZ + (limitMax.z - maxHeadZ) * multy;
		}
	}
	euler.z = (euler.z < clampZMin) ? clampZMin : (euler.z > clampZMax) ? clampZMax : euler.z;

	EulerToQuat(euler, orientation);
}

// shipped BlendKeyframe 0x616E30 - THE write-back: slerp from the interp
// frame's current q toward the physics orientation, store INTO the interp
// frame. With capture bits 1/2 set, the anim system no longer rewrites this
// frame, so `src` is stable across frames and the slerp converges as blend
// ramps to 1. RpHAnimSlerp is the SDK macro (rtslerp.h); its t<=0 / t>=1
// branches and nearlyZeroOm flag make the degenerate cases safe.
void SoftBone_c::BlendKeyframe(float blend) {
	if (!frameData || !frameData->KeyFrame) return;
	if (blend <= 0.0f) return;

	RtQuat src = frameData->KeyFrame->q;
	RtQuat dst = orientation;

	// NaN audit (doc R1: nothing NaN may reach skinning). A poisoned physics
	// orientation defers to the anim pose; a poisoned anim frame (capture
	// bits make the interp frame ours) is repaired with the audited physics
	// quat so the NaN cannot propagate frame-to-frame.
	if (!(dst.real == dst.real) || !(dst.imag.x == dst.imag.x) ||
		!(dst.imag.y == dst.imag.y) || !(dst.imag.z == dst.imag.z))
		return;
	if (!(src.real == src.real) || !(src.imag.x == src.imag.x) ||
		!(src.imag.y == src.imag.y) || !(src.imag.z == src.imag.z)) {
		frameData->KeyFrame->q = dst;
		return;
	}

	// epsilon guard: near-identical quats - keep the frame as-is (the SDK
	// cache handles omega~0 via nearlyZeroOm, this just skips the work)
	float d = src.real * dst.real + src.imag.x * dst.imag.x +
		src.imag.y * dst.imag.y + src.imag.z * dst.imag.z;
	if (d > (1.0f - SOFTBODY_EPS) && blend >= 1.0f) return;

	RtQuatSlerpCache cache;
	RtQuatSetupSlerpCache(&src, &dst, &cache);
	RtQuat out;
	RtQuatSlerp(&out, &src, &dst, blend, &cache);
	// audit the slerp output too (SetupSlerpCache is game code at 0x7EC220 -
	// belt and braces before the write reaches the interp frame)
	if (!(out.real == out.real) || !(out.imag.x == out.imag.x) ||
		!(out.imag.y == out.imag.y) || !(out.imag.z == out.imag.z))
		return;	// keep the anim pose (frame untouched)
	frameData->KeyFrame->q = out;
}

// shipped CalcWldMat 0x616CD0 - local rot from orientation + pos, compose
// with parent, recurse. Integrator/world queries only; NOT the skin path.
// The multiply is OUT-OF-PLACE into a stack temp (shipped BoneNode_c shape):
// the game's RwMatrixMultiply composes component-wise and is not guaranteed
// alias-safe for out == in1.
void SoftBone_c::CalcWldMat(const RwMatrix *parentMat) {
	RwMatrix local;
	RtQuatUnitConvertToMatrixMacro(&orientation, &local);
	local.pos.x = pos.x;
	local.pos.y = pos.y;
	local.pos.z = pos.z;
	rwMatrixSetFlags(&local, rwMATRIXTYPEORTHONORMAL);

	RwMatrixMultiply(&worldMat, &local, parentMat);

	for (int i = 0; i < numChildren; i++)
		children[i]->CalcWldMat(&worldMat);
}

// ---- SoftRagdoll_c - prototype Ragdoll_c completed (doc 5.2) ----

struct SoftRagdoll_c {
	void *ped;						// CPed*
	RpClump *clump;					// ped's clump at Init - slot-reuse detector
	int poolIndex;					// ped-pool slot index at assignment (-1 = unknown)
	SoftBone_c bones[RAGDOLL_MAX_BONES];
	SoftBone_c *rootBone;
	float blend;					// ramps SOFTBODY_BLEND_START -> 1.0
	bool captured;					// capture bits 1/2 currently set on our frames
	bool settled;					// pose stable - integration + write-back skipped
	float settleTimer;				// seconds of pose stability
	float simTime;					// seconds since activation (hard settle timeout)

	bool Init(void *pPed, int poolIdx);
	void Exit();
	void Update(float deltaTime);
	void PreRender();
	void ApplyForce(int32 boneTag, float force, RwV3d dir);
	bool IsActive() const { return ped != nullptr; }
	void *GetPed() const { return ped; }
	RpClump *GetClump() const { return clump; }
	int GetPoolIndex() const { return poolIndex; }

private:
	void SetupBoneHierarchy();
	void ResolveSystem(float deltaTime, const RwMatrix *pedMat);
	void GroundResponse(const RwMatrix *pedMat);
	void WaterResponse(float deltaTime);	// G-water: buoyancy + wave bob (runs BEFORE GroundResponse)
	void CaptureFrames();
	void ReleaseFrames();
};

// doc 3.1 step 3 / leak Ragdoll.cpp:255-273: parent links from
// ms_boneInfos[i].m_prev (sftbdyphsx.cpp:399-419 reads the same table with
// the same semantics; `>= 0` matches shipped `> -1`).
void SoftRagdoll_c::SetupBoneHierarchy() {
	rootBone = nullptr;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++)
		if (bones[i].boneTag == 0) { rootBone = &bones[i]; break; }
	if (!rootBone) return;

	tBoneInfo *infos = SoftbodyBoneInfos();
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		int32 tag = bones[i].boneTag;
		if (tag < 0) continue;
		for (int j = 0; j < MAX_BONE_NUM; j++) {
			if (infos[j].m_current == tag && infos[j].m_prev >= 0) {
				for (int p = 0; p < RAGDOLL_MAX_BONES; p++) {
					if (bones[p].boneTag == infos[j].m_prev && &bones[p] != &bones[i]) {
						bones[i].parent = &bones[p];
						if (bones[p].numChildren < 8)
							bones[p].children[bones[p].numChildren++] = &bones[i];
						break;
					}
				}
				break;
			}
		}
	}
}

// G7 capture (prototype Ragdoll.cpp:60-70, shipped bits per sftbdyphsx.h):
// bit1 KeyFramesIgnoreNodeOrientation + bit2 KeyFramesIgnoreNodeTranslation
// freeze the anim system's writes on our nodes - physics owns the interp
// frames from here. Only called for a validated (R2-probed) frame array.
void SoftRagdoll_c::CaptureFrames() {
	if (captured) return;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		AnimBlendFrameData *fd = bones[i].frameData;
		if (!fd || bones[i].boneTag < 0) continue;
		fd->KeyFramesIgnoreNodeOrientation = true;	// bit1
		fd->KeyFramesIgnoreNodeTranslation = true;	// bit2
	}
	captured = true;
}

// R8 release: restore the exact pre-capture Flags byte (game-set bits
// preserved), so a revived/scripted ped resumes anim-driven poses cleanly.
void SoftRagdoll_c::ReleaseFrames() {
	if (!captured) return;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		AnimBlendFrameData *fd = bones[i].frameData;
		if (!fd || bones[i].boneTag < 0) continue;
		fd->Flags = bones[i].savedFlags;
	}
	captured = false;
}

// Activation (prototype Init 54-97, completed): validate hierarchy + FIXED
// 0x18 frame array (R2 probe), capture bind pose, build parent links, seed
// momentum from the ped's death velocity, then capture bits 1/2.
bool SoftRagdoll_c::Init(void *pPed, int poolIdx) {
	ped = pPed;
	poolIndex = poolIdx;
	clump = *(RpClump**)((uint32)ped + PED_OFF_CLUMP);
	blend = SOFTBODY_BLEND_START;
	captured = false;
	settled = false;
	settleTimer = 0.0f;
	simTime = 0.0f;
	rootBone = nullptr;

	if (!clump) { ped = nullptr; return false; }
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(clump);
	if (!hier || hier->numNodes < 1) { ped = nullptr; return false; }

	// FIXED 0x18-stride frame array (plan G1): index = hierarchy node index,
	// exactly the prototype's frameData[boneId] (Ragdoll.cpp:78-79). The
	// pre-fix 0x8 stride would have corrupted every access here.
	CAnimBlendClumpData *clumpData = ANIMBLENDCLUMPFROMCLUMP(clump);
	if (!clumpData || !clumpData->m_FrameDatas || clumpData->m_NumFrameData == 0) {
		ped = nullptr; return false;
	}

	// R2 stride probe + per-bone init in one pass: for every ragdoll tag the
	// resolved entry's BoneTag must equal the tag (shipped semantic: BoneTag
	// stores the eBoneTag for skinned clumps) or the hierarchy node index
	// (tolerated alternate). A wrong stride misaligns every read and cannot
	// pass; mismatched entries are never touched (no capture write reaches
	// unvalidated memory).
	tBoneInfo *infos = SoftbodyBoneInfos();
	int matches = 0, checked = 0;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		int32 tag = aBONETAG_ENUM_TAB[i];
		int32 boneId = RpHAnimIDGetIndex(hier, tag);
		AnimBlendFrameData *fd = nullptr;
		if (boneId >= 0 && (uint32)boneId < clumpData->m_NumFrameData) {
			fd = &clumpData->m_FrameDatas[boneId];
			checked++;
			if (fd->BoneTag != (uint32)tag && fd->BoneTag != (uint32)boneId)
				fd = nullptr;	// untrusted entry - leave it to the anim system
			else
				matches++;
		}
		if (fd && !fd->KeyFrame)
			fd = nullptr;		// no interp frame (non-skinned entry) - inert bone

		// tBoneInfo lookup by m_current (same table walk as sftbdyphsx.cpp:403)
		const tBoneInfo *info = nullptr;
		for (int j = 0; j < MAX_BONE_NUM; j++)
			if (infos[j].m_current == tag) { info = &infos[j]; break; }

		// Absent bones (some peds lack brows/jaw) stay inactive: null
		// frameData makes BlendKeyframe/Init no-ops; tag kept for parent links.
		bones[i].Init(tag, fd, info);
		if (!fd)
			bones[i].boneTag = -1;
	}

	// one-shot R2 aggregate log (first activation only)
	static bool s_probeLogged = false;
	if (!s_probeLogged) {
		s_probeLogged = true;
		dbglog("[Softbody] frame-layout probe: %d/%d ragdoll bone tags trusted (0x18 stride %s)",
			matches, checked,
			(checked > 0 && matches >= SOFTBODY_PROBE_MIN) ? "OK" : "MISMATCH");
	}
	if (checked == 0 || matches < SOFTBODY_PROBE_MIN) {
		ped = nullptr;	// layout drift - refuse to capture (R2)
		return false;
	}

	SetupBoneHierarchy();
	if (!rootBone) { ped = nullptr; return false; }

	// seed momentum: every bone inherits the ped's death velocity
	// (CPed+0x44 m_vecMoveSpeed) so hits/explosions carry into the collapse
	CVector mv = *(CVector*)((uint32)ped + PED_OFF_MOVESPEED);
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag < 0) continue;
		bones[i].vel.x = mv.x;
		bones[i].vel.y = mv.y;
		bones[i].vel.z = mv.z;
	}

	CaptureFrames();
	return true;
}

void SoftRagdoll_c::Exit() {
	if (!ped) return;
	ReleaseFrames();	// R8: anim system resumes full control of the frames
	ped = nullptr;
	clump = nullptr;
	poolIndex = -1;
	rootBone = nullptr;
}

// prototype Update 125-143 + blend/settle lifecycle. Runs once per game frame
// (renderFrame-gated in SoftbodyUpdate).
void SoftRagdoll_c::Update(float deltaTime) {
	if (!ped) return;

	simTime += deltaTime;

	// blend-in crossfade: frozen death pose -> physics ownership
	if (blend < 1.0f) {
		blend += deltaTime * SOFTBODY_BLEND_RAMP;
		if (blend > 1.0f) blend = 1.0f;
	}

	if (settled) return;

	// world matrices from the ped root (prototype Update :128)
	RwFrame *frame = *(RwFrame**)((uint32)clump + CLUMP_OFF_FRAME);
	if (!frame) return;
	RwMatrix *ltm = RwFrameGetLTM(frame);
	if (!ltm) return;

	// snapshot orientations for the settle metric
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++)
		if (bones[i].boneTag >= 0)
			bones[i].prevOrientation = bones[i].orientation;

	// 1. world matrices, 2. integrator (prototype ResolveSystem)
	rootBone->CalcWldMat(ltm);
	ResolveSystem(deltaTime, ltm);

	// settle detection: pose stability (|quat dot| per bone). Velocity-based
	// settle never fires here because gravity keeps a terminal velocity; the
	// POSE stops changing once every joint rests on its Limit() clamp.
	float minDot = 1.0f;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag < 0) continue;
		const RtQuat &a = bones[i].prevOrientation;
		const RtQuat &b = bones[i].orientation;
		float d = (float)std::fabs(a.real * b.real + a.imag.x * b.imag.x +
			a.imag.y * b.imag.y + a.imag.z * b.imag.z);
		if (d < minDot) minDot = d;
	}
	if (minDot > SOFTBODY_SETTLE_DOT) {
		settleTimer += deltaTime;
		if (settleTimer >= SOFTBODY_SETTLE_TIME || simTime >= SOFTBODY_SETTLE_MAX) {
			settled = true;
			dbglog("[Softbody] ragdoll %p settled (t=%.1fs)", ped, simTime);
		}
	} else {
		settleTimer = 0.0f;
		if (simTime >= SOFTBODY_SETTLE_MAX) {
			settled = true;	// hard timeout - pose is limit-pinned by now
			dbglog("[Softbody] ragdoll %p force-settled (timeout)", ped);
		}
	}
}

// prototype ResolveSystem 176-248, completed per doc 5.2: ALL bones (not
// HEAD-only), Limit() enabled (G3), 3-axis friction (G5), subtree refresh
// from the ROTATED parent (rotating parent->orientation requires recomputing
// the parent's worldMat before children read it).
void SoftRagdoll_c::ResolveSystem(float deltaTime, const RwMatrix *pedMat) {
	// gravity (prototype :181-184)
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag < 0 || !bones[i].frameData) continue;
		bones[i].vel.z -= SOFTBODY_GRAVITY * deltaTime;
	}

	// per-bone velocity -> parent-space rotation (prototype :187-241)
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		SoftBone_c *bone = &bones[i];
		if (bone->boneTag < 0 || !bone->frameData || !bone->parent)
			continue;	// root and frameless bones produce no rotation

		// parentToNodeVec = node.pos - parent.pos (from world matrices)
		RwV3d parentToNodeVec;
		parentToNodeVec.x = bone->worldMat.pos.x - bone->parent->worldMat.pos.x;
		parentToNodeVec.y = bone->worldMat.pos.y - bone->parent->worldMat.pos.y;
		parentToNodeVec.z = bone->worldMat.pos.z - bone->parent->worldMat.pos.z;

		// axis = cross(parentToNodeVec, vel); speed = |axis| = swing angle.
		// Explicit guarded normalize (project division-safety rule): the
		// length is checked BEFORE the divide - the SDK's normalize path
		// (rwplcore.h _rwV3dNormalizeMacro) runs rwInvSqrt on length^2 with
		// no zero guard, and the game WRAPPER's zero behavior is unverified.
		RwV3d axis;
		RwV3dCrossProduct(&axis, &parentToNodeVec, &bone->vel);
		float speed = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
		if (speed < SOFTBODY_MIN_AXIS_SPEED) continue;
		float invSpeed = 1.0f / speed;
		axis.x *= invSpeed;
		axis.y *= invSpeed;
		axis.z *= invSpeed;

		// axis into parent space: inv(parent world quat) * axis
		RtQuat parentQuat, invParentQuat;
		RtQuatConvertFromMatrix(&parentQuat, &bone->parent->worldMat);
		// unit quat from a matrix: modulus^2 ~= 1; guard anyway (rule)
		float qval = parentQuat.imag.x * parentQuat.imag.x + parentQuat.imag.y * parentQuat.imag.y +
			parentQuat.imag.z * parentQuat.imag.z + parentQuat.real * parentQuat.real;
		if (qval < SOFTBODY_EPS) continue;
		RtQuatReciprocal(&invParentQuat, &parentQuat);
		RwV3d relativeAxis;
		RtQuatTransformVectors(&relativeAxis, &axis, 1, &invParentQuat);

		// rotate the PARENT's local orientation (prototype :226 - the keyframe
		// quat of the node whose subtree swings)
		RtQuatRotate(&bone->parent->orientation, &relativeAxis, speed, rwCOMBINEPOSTCONCAT);

		// joint limits on the rotated node (G3: prototype left this commented)
		bone->parent->Limit();

		// refresh world matrices down the subtree FROM THE ROTATED PARENT so
		// its own worldMat reflects the new orientation before children read it
		SoftBone_c *par = bone->parent;
		if (par->parent)
			par->CalcWldMat(&par->parent->worldMat);
		else
			par->CalcWldMat(pedMat);
	}

	// friction - 3 axes, the prototype's x-only damp was a bug (G5)
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag < 0) continue;
		bones[i].vel.x *= SOFTBODY_FRICTION;
		bones[i].vel.y *= SOFTBODY_FRICTION;
		bones[i].vel.z *= SOFTBODY_FRICTION;
	}

	// water layer (G-water) - ORDER CONTRACT (task 3): WaterResponse runs
	// BEFORE GroundResponse, every frame, unchanged. Rationale: water is a
	// soft volume force (buoyancy/wave/drag on velocities), ground is the
	// hard constraint - applying ground LAST keeps its reflection + lift as
	// the final word so water can never reintroduce floor penetration, and
	// preserves the contract below (reflected velocities persist into the
	// next frame; the settle metric sees the ground-corrected pose).
	WaterResponse(deltaTime);

	// ground collision (G8-native) - runs last so the reflected velocities
	// and lift rotations persist into the next frame's integration, and so
	// the settle metric in Update sees the ground-corrected pose
	GroundResponse(pedMat);
}

// G8-native (doc 4.4 phase 1): game ground-Z collision as velocity reflection
// + rotation-based positional lift. Translations are anim-owned (capture bit 2
// freezes interp-frame t; only orientations reach the skin via BlendKeyframe),
// so a penetrating bone is lifted by rotating its PARENT orientation - the
// native analog of Bullet positional correction that actually renders.
//
// Queries: 1 root anchor per instance (band gate) + per-bone queries only for
// bones inside the near-ground band - airborne bones skip, typically <=6
// queries per instance per frame. worldMats are consistent here: ResolveSystem
// refreshed every rotated subtree and untouched bones still match their
// initial-pass matrices.
void SoftRagdoll_c::GroundResponse(const RwMatrix *pedMat) {
	SoftBone_c *root = rootBone;
	if (!root || root->boneTag < 0 || !root->frameData) return;

	// root anchor: band gate + root velocity response (the root has no
	// parent to rotate - the ped entity placement owns the corpse's global
	// height; a sunken root means the entity itself is under ground)
	float rootGroundZ = SOFTBODY_FINDGROUNDZ(root->worldMat.pos.x, root->worldMat.pos.y);
	if (root->worldMat.pos.z < rootGroundZ + SOFTBODY_BONE_RADIUS && root->vel.z < 0.0f)
		root->vel.z = -root->vel.z * SOFTBODY_GROUND_RESTITUTION;

	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		SoftBone_c *bone = &bones[i];
		if (bone->boneTag < 0 || !bone->frameData) continue;
		RwV3d *wp = &bone->worldMat.pos;

		// airborne bones (above the band) cannot touch ground this frame
		if (wp->z > rootGroundZ + SOFTBODY_GROUND_BAND) continue;

		float groundZ = SOFTBODY_FINDGROUNDZ(wp->x, wp->y);
		// bogus-query guard: 0x569660 returns a 20.0f fallback when nothing
		// is hit (interiors / streaming holes) - never lift a bone more than
		// 1 m toward a reported ground plane
		if (groundZ > wp->z + 1.0f) continue;

		float pen = (groundZ + SOFTBODY_BONE_RADIUS) - wp->z;
		if (pen <= 0.0f) continue;
		if (pen > SOFTBODY_GROUND_PEN_MAX) pen = SOFTBODY_GROUND_PEN_MAX;

		// velocity response (world up = +Z): reflect the fall, damp the slide
		if (bone->vel.z < 0.0f)
			bone->vel.z = -bone->vel.z * SOFTBODY_GROUND_RESTITUTION;
		bone->vel.x *= SOFTBODY_GROUND_FRICTION;
		bone->vel.y *= SOFTBODY_GROUND_FRICTION;

		// positional response BY ROTATION of the parent orientation (root
		// handled above). Swing axis = cross(parentToNodeVec, worldUp):
		// rotating around it displaces the bone along +Z (pendulum lift out
		// of the ground).
		SoftBone_c *par = bone->parent;
		if (!par) continue;

		RwV3d parentToNodeVec;
		parentToNodeVec.x = wp->x - par->worldMat.pos.x;
		parentToNodeVec.y = wp->y - par->worldMat.pos.y;
		parentToNodeVec.z = wp->z - par->worldMat.pos.z;

		RwV3d up;
		up.x = 0.0f; up.y = 0.0f; up.z = 1.0f;
		RwV3d axis;
		RwV3dCrossProduct(&axis, &parentToNodeVec, &up);
		float axisLen = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
		if (axisLen < SOFTBODY_EPS) continue;	// bone straight above pivot - no lift direction
		float invAxisLen = 1.0f / axisLen;
		axis.x *= invAxisLen;
		axis.y *= invAxisLen;
		axis.z *= invAxisLen;

		float angle = pen * SOFTBODY_GROUND_ROT_GAIN;
		if (angle > SOFTBODY_GROUND_ROT_MAX) angle = SOFTBODY_GROUND_ROT_MAX;

		// axis into parent space (same transform as the velocity swing)
		RtQuat parentQuat, invParentQuat;
		RtQuatConvertFromMatrix(&parentQuat, &par->worldMat);
		float qval = parentQuat.imag.x * parentQuat.imag.x + parentQuat.imag.y * parentQuat.imag.y +
			parentQuat.imag.z * parentQuat.imag.z + parentQuat.real * parentQuat.real;
		if (qval < SOFTBODY_EPS) continue;
		RtQuatReciprocal(&invParentQuat, &parentQuat);
		RwV3d relativeAxis;
		RtQuatTransformVectors(&relativeAxis, &axis, 1, &invParentQuat);

		RtQuatRotate(&par->orientation, &relativeAxis, angle, rwCOMBINEPOSTCONCAT);
		par->Limit();

		// refresh the subtree from the rotated parent (same rule as
		// ResolveSystem) so later bones read consistent matrices
		if (par->parent)
			par->CalcWldMat(&par->parent->worldMat);
		else
			par->CalcWldMat(pedMat);
	}
}

// G-water (task 3): per-bone submerged check via the game water query +
// buoyancy as a velocity response in the integrator + the Water_VS swell as
// the target vertical drift, so corpses bob with the rendered water. Runs
// BEFORE GroundResponse (order contract in ResolveSystem). Translations stay
// anim-owned (capture bit 2) exactly like the ground lane, so the buoyancy
// manifests through the same velocity->parent-rotation swing - the water
// cannot move a bone's world position, only the pose/limb motion, and the
// velocity clamp keeps a floating corpse's pose stable while the wave terms
// drive the bobbing. Queries: 1 root anchor (no-water early-out + band gate)
// + per-bone queries only inside the near-surface band.
void SoftRagdoll_c::WaterResponse(float deltaTime) {
	if (deltaTime <= 0.0f) return;
	SoftBone_c *root = rootBone;
	if (!root || root->boneTag < 0 || !root->frameData) return;

	// root anchor: no-water early-out + band gate (mirrors GroundResponse)
	float rootLevel;
	if (!SoftbodyWaterQuery(root->worldMat.pos.x, root->worldMat.pos.y,
			root->worldMat.pos.z, &rootLevel))
		return;	// 0.0/no-water fallback -> no water under this corpse
	float rootSurf = rootLevel + SoftbodyWaterSwell(root->worldMat.pos.x, root->worldMat.pos.y);
	if (!WaterFinite(rootSurf)) return;

	int total = 0, wet = 0;
	float fracSum = 0.0f, fracMax = 0.0f;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		SoftBone_c *bone = &bones[i];
		if (bone->boneTag < 0 || !bone->frameData) continue;
		total++;
		RwV3d *wp = &bone->worldMat.pos;

		// airborne bones (above the band) are not in the water this frame
		if (wp->z > rootSurf + SOFTBODY_WATER_BAND) continue;

		float level;
		if (!SoftbodyWaterQuery(wp->x, wp->y, wp->z, &level)) continue;
		float surf = level + SoftbodyWaterSwell(wp->x, wp->y);
		if (!WaterFinite(surf)) continue;

		// submerged fraction of the bone sphere (SOFTBODY_BONE_RADIUS,
		// the directive's per-bone sphere radius 0.08); 0 = fully clear of
		// the surface, 1 = fully under it
		float frac = (surf + SOFTBODY_BONE_RADIUS - wp->z) / (2.0f * SOFTBODY_BONE_RADIUS);
		if (frac <= 0.0f) continue;
		if (frac > 1.0f) frac = 1.0f;
		wet++;
		fracSum += frac;
		if (frac > fracMax) fracMax = frac;

		// buoyancyGain: gravity * submergedFraction * gain. Combined with
		// the integrator's -G this is net -G*(1 - frac*gain); gain 1.0 makes
		// a fully submerged bone neutral (task 4: never sinks through).
		bone->vel.z += SOFTBODY_GRAVITY * frac * SOFTBODY_WATER_BUOYANCY_GAIN * deltaTime;

		// wave bob: follow the swell's vertical velocity ("wave force using
		// the same swell law as the water VS") ...
		float targetVz = SoftbodyWaterSwellVel(wp->x, wp->y);
		float dv = (targetVz - bone->vel.z) * SOFTBODY_WATER_COUPLING * frac *
			SOFTBODY_WATER_WAVE_FORCE_SCALE * deltaTime;
		if (dv > SOFTBODY_WATER_VZ_MAX) dv = SOFTBODY_WATER_VZ_MAX;
		else if (dv < -SOFTBODY_WATER_VZ_MAX) dv = -SOFTBODY_WATER_VZ_MAX;
		bone->vel.z += dv;

		// ... and drift toward the wave surface height ("wave height as
		// target y-drift"); clamped so a deep-water floor cannot spin limbs
		float drift = surf - wp->z;
		if (drift > SOFTBODY_WATER_DRIFT_MAX) drift = SOFTBODY_WATER_DRIFT_MAX;
		else if (drift < -SOFTBODY_WATER_DRIFT_MAX) drift = -SOFTBODY_WATER_DRIFT_MAX;
		bone->vel.z += drift * SOFTBODY_WATER_SPRING * frac *
			SOFTBODY_WATER_WAVE_FORCE_SCALE * deltaTime;

		// velocity-proportional linear drag scaled by submersion (3 axes, G5)
		float damp = 1.0f - SOFTBODY_WATER_DRAG_COEFF * frac * deltaTime;
		if (damp < 0.0f) damp = 0.0f;
		bone->vel.x *= damp;
		bone->vel.y *= damp;
		bone->vel.z *= damp;

		// NaN/inf audit (project rule: nothing NaN may persist in the
		// integrator - the swing would poison the orientations next frame)
		if (!WaterFinite(bone->vel.x) || !WaterFinite(bone->vel.y) || !WaterFinite(bone->vel.z)) {
			bone->vel.x = bone->vel.y = bone->vel.z = 0.0f;
		}
	}

	// throttled receipt (task 2): submerged fractions per contact event
	if (wet > 0 && dbglog_throttle("softbody_water"))
		dbglog("[Softbody] water: ped %p %d/%d bones submerged (frac avg %.2f max %.2f)",
			ped, wet, total, fracSum / (float)wet, fracMax);
}

// prototype Update tail (:139-142) - the shipped write-back. Runs per render
// frame AFTER the anim system's last write (capture bits guarantee the interp
// frames are ours) and BEFORE the ped's PreRender UpdateRpHAnim pushes them
// into pMatrixArray (doc 3.3 order). Settled ragdolls at blend 1.0 already
// hold their final pose in the frames - skip.
void SoftRagdoll_c::PreRender() {
	if (!ped) return;
	if (settled && blend >= 1.0f) return;
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag < 0 || !bones[i].frameData) continue;
		bones[i].BlendKeyframe(blend);
	}
}

// prototype ApplyForce 160-169 - external impulse entry (bullet impacts).
// Wakes a settled ragdoll.
void SoftRagdoll_c::ApplyForce(int32 boneTag, float force, RwV3d dir) {
	for (int i = 0; i < RAGDOLL_MAX_BONES; i++) {
		if (bones[i].boneTag != boneTag) continue;
		bones[i].vel.x += dir.x * force;
		bones[i].vel.y += dir.y * force;
		bones[i].vel.z += dir.z * force;
		settled = false;
		settleTimer = 0.0f;
		simTime = 0.0f;
		return;
	}
}

// ---- Manager (static pool, doc 5.2 SoftRagdollManager_c) ----

static SoftRagdoll_c g_softbodyRagdolls[SOFTBODY_MAX_RAGDOLLS];

static SoftRagdoll_c *SoftbodyFindForPed(void *ped) {
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		if (g_softbodyRagdolls[i].IsActive() && g_softbodyRagdolls[i].GetPed() == ped)
			return &g_softbodyRagdolls[i];
	return nullptr;
}

static SoftRagdoll_c *SoftbodyAddRagdoll(void *ped, int poolIdx) {
	if (SoftbodyFindForPed(ped)) return nullptr;
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		if (!g_softbodyRagdolls[i].IsActive()) {
			if (g_softbodyRagdolls[i].Init(ped, poolIdx))
				return &g_softbodyRagdolls[i];
			// refused (R2 probe / hierarchy gate) - visible, throttled, so a
			// layout mismatch never fails silently
			if (dbglog_throttle("softbody_refuse"))
				dbglog("[Softbody] activation refused for ped %p (frame-layout probe / hierarchy gate)", ped);
			return nullptr;
		}
	return nullptr;
}

// ped pool walk - same layout proven in RagdollManager_c::ProcessAllPeds
// (sftbdyphsx.cpp:479-559) and ragdoll_death.cpp:994: pool 0xB74490, slot
// stride 0x7C4, free bit 0x80, clump at CPed+0x18, state at CPed+0x530.
struct SoftbodyPedSlotInfo { unsigned char id : 7; bool free : 1; };
struct SoftbodyPedPool {
	void *m_pSlots;
	SoftbodyPedSlotInfo *m_pSlotInfos;
	int m_nNumSlots;
};

// ---- Backend gate (m0971: sky_physics.ini via physicscfg.cpp) ----
bool SoftbodyGetEnabled(void) {
	const char *backend = PhysicsCfgGetBackend();
	if (strcmp(backend, "native") == 0)
		return true;
	if (strcmp(backend, "bullet") == 0) {
		// TODO(bullet): reserved backend - the Bullet ragdoll port
		// (docs/plans/2026-10-29-rage-bullet-ragdoll-plan.md) is not wired
		// yet, so "bullet" deliberately behaves as OFF for now.
		return false;
	}
	return false;	// "off" (and any normalized-to-off value)
}

bool SoftbodyIsActiveForPed(void *ped) {
	if (!ped || !SoftbodyGetEnabled()) return false;
	return SoftbodyFindForPed(ped) != nullptr;
}

void SoftbodyApplyForce(void *ped, int32 boneTag, float force, RwV3d dir) {
	if (!ped || !SoftbodyGetEnabled()) return;
	SoftRagdoll_c *r = SoftbodyFindForPed(ped);
	if (r) r->ApplyForce(boneTag, force, dir);
}

// ---- G6 helper: Bullet world transform -> bone-local (see softbody.h) ----

bool SoftbodyWorldToBoneLocal(const RwMatrix *parentWorld, const RwMatrix *childWorld,
	RtQuat *outLocalQuat, CVector *outLocalPos)
{
	if (!parentWorld || !childWorld || !outLocalQuat || !outLocalPos) return false;

	RwMatrix inv;
	RwMatrixInvert(&inv, parentWorld);

	// NaN/inf guard on the inverted matrix (project division-safety rule)
	const float *p = (const float*)&inv;
	for (int i = 0; i < 12; i++) {
		if (!(p[i] == p[i]) || p[i] > 1e30f || p[i] < -1e30f)
			return false;	// singular/degenerate parent - caller keeps anim pose
	}

	RwMatrix local;
	RwMatrixMultiply(&local, &inv, childWorld);

	if (!RtQuatConvertFromMatrix(outLocalQuat, &local))
		return false;
	float qm2 = outLocalQuat->real * outLocalQuat->real +
		outLocalQuat->imag.x * outLocalQuat->imag.x +
		outLocalQuat->imag.y * outLocalQuat->imag.y +
		outLocalQuat->imag.z * outLocalQuat->imag.z;
	if (qm2 < SOFTBODY_EPS) return false;

	outLocalPos->x = local.pos.x;
	outLocalPos->y = local.pos.y;
	outLocalPos->z = local.pos.z;
	return true;
}

// ---- Lifecycle (texdefs.h:11-20 contract) ----

void SoftbodyInit(void) {
	WaterClockSeconds();	// anchor the swell clock NOW (same init burst as
							// waterPipe_init main.cpp:8301 - its g_waterTime is
							// the Water_VS c11 time base we phase-match)
	PhysicsCfgLoad();	// idempotent; also lazy-called by GetBackend above
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		g_softbodyRagdolls[i].Exit();	// no-op on fresh slots; safe re-init
	if (!SoftbodyGetEnabled()) return;	// fast early-return, default OFF
	dbglog("[Softbody] native softbody active (softbodyBackend=native, pool %d, budget key softbodyBudget)",
		SOFTBODY_MAX_RAGDOLLS);
}

void SoftbodyUpdate(void) {
	if (!SoftbodyGetEnabled()) {
		// backend toggled off mid-session: release survivors so capture bits
		// never outlive the module (R8)
		for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
			if (g_softbodyRagdolls[i].IsActive())
				g_softbodyRagdolls[i].Exit();
		return;
	}

	// renderFrame gate - mirror/shadow passes re-enter the simulate slot;
	// step once per unique frame (ragdoll_death.cpp:969 pattern)
	static RwUInt16 s_lastUpdateFrame = 0;
	RwUInt16 curFrame = RWSRCGLOBAL(renderFrame);
	if (curFrame == s_lastUpdateFrame) return;
	s_lastUpdateFrame = curFrame;

	float deltaTime = CTimer__ms_fTimeStep / 50.0f;
	if (!(deltaTime > 0.0f)) return;	// also catches NaN (NaN > 0 is false) — the old `<= 0` let NaN through into the integrator
	if (deltaTime > 0.1f) deltaTime = 0.1f;	// spike clamp (death-ragdoll dt pattern)

	// live budget ([SkyPhysics] softbodyBudget, default 8; physicscfg owns it)
	int budget = PhysicsCfgGetInt("softbodyBudget", SOFTBODY_BUDGET_DEFAULT);
	if (budget < 1) budget = 1;
	if (budget > SOFTBODY_MAX_RAGDOLLS) budget = SOFTBODY_MAX_RAGDOLLS;

	SoftbodyPedPool *pool = *(SoftbodyPedPool**)0xB74490;
	if (!pool || !pool->m_pSlots || !pool->m_pSlotInfos) return;

	CVector camPos;
	if (!Scene.camera) return;
	RwFrame *camFrame = RwCameraGetFrame(Scene.camera);
	if (!camFrame) return;
	RwV3d *rp = RwMatrixGetPos(RwFrameGetLTM(camFrame));
	camPos.x = rp->x; camPos.y = rp->y; camPos.z = rp->z;

	// pass 1: validate active instances - slot freed/reused (free bit or clump
	// changed), revive (state left 66), lost frame, out of range. The slot
	// check runs BEFORE any ped-memory read so a freed/reused slot is never
	// dereferenced. Any release restores the capture bits (R8) first.
	int activeCount = 0;
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++) {
		SoftRagdoll_c *r = &g_softbodyRagdolls[i];
		if (!r->IsActive()) continue;
		bool release = false;
		int idx = r->GetPoolIndex();
		if (idx < 0 || idx >= pool->m_nNumSlots || pool->m_pSlotInfos[idx].free) {
			release = true;	// slot freed (stream-out) or pool shrank
		} else {
			void *p = r->GetPed();
			RpClump *curClump = *(RpClump**)((uint32)p + PED_OFF_CLUMP);
			int state = *(int*)((uint32)p + PED_OFF_STATE);
			release = (curClump != r->GetClump()) || (state != SOFTBODY_PEDSTATE_DEAD);
			if (!release) {
				RwFrame *frame = *(RwFrame**)((uint32)curClump + CLUMP_OFF_FRAME);
				RwMatrix *ltm = frame ? RwFrameGetLTM(frame) : nullptr;
				if (!ltm) {
					release = true;
				} else {
					float dx = ltm->pos.x - camPos.x, dy = ltm->pos.y - camPos.y, dz = ltm->pos.z - camPos.z;
					if (sqrtf(dx * dx + dy * dy + dz * dz) >= RAGDOLL_DIST_HIGH)
						release = true;
				}
			}
		}
		if (release)
			r->Exit();
		else
			activeCount++;
	}

	// pass 2: assign newly dead peds within range, up to budget. G2 gate:
	// DEAD only (66) - dying peds (65) keep their death anim; ground collision
	// exists now (G8-native), but capturing at 65 would also freeze the ROOT
	// frame's translation and with it the ped's HasVelocity movement (see
	// softbody.h P1 scope note - root-frame exemption is the P2 fix).
	if (activeCount < budget) {
		for (int i = 0; i < pool->m_nNumSlots; i++) {
			if (pool->m_pSlotInfos[i].free) continue;
			void *ped = (void*)((uint8*)pool->m_pSlots + i * 0x7C4);
			if (*(int*)((uint32)ped + PED_OFF_STATE) != SOFTBODY_PEDSTATE_DEAD) continue;
			if (SoftbodyFindForPed(ped)) continue;
			RpClump *clump = *(RpClump**)((uint32)ped + PED_OFF_CLUMP);
			if (!clump) continue;
			RwFrame *frame = *(RwFrame**)((uint32)clump + CLUMP_OFF_FRAME);
			if (!frame) continue;
			RwMatrix *ltm = RwFrameGetLTM(frame);
			if (!ltm) continue;
			float dx = ltm->pos.x - camPos.x, dy = ltm->pos.y - camPos.y, dz = ltm->pos.z - camPos.z;
			if (sqrtf(dx * dx + dy * dy + dz * dz) >= RAGDOLL_DIST_HIGH) continue;

			if (SoftbodyAddRagdoll(ped, i)) {
				activeCount++;
				if (activeCount >= budget) break;
			}
		}
	}

	// pass 3: integrator, once per frame over the instance pool (G9 pattern:
	// iterate instances; no shared external world to step)
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		if (g_softbodyRagdolls[i].IsActive())
			g_softbodyRagdolls[i].Update(deltaTime);
}

void SoftbodyPreRender(void) {
	if (!SoftbodyGetEnabled()) return;

	// renderFrame gate - RenderScene re-enters for mirrors/shadows; the slerp
	// must advance once per frame (same pattern as SoftbodyUpdate)
	static RwUInt16 s_lastPreRenderFrame = 0;
	RwUInt16 curFrame = RWSRCGLOBAL(renderFrame);
	if (curFrame == s_lastPreRenderFrame) return;
	s_lastPreRenderFrame = curFrame;

	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		if (g_softbodyRagdolls[i].IsActive())
			g_softbodyRagdolls[i].PreRender();
}

void SoftbodyShutdown(void) {
	// ALWAYS release - a mid-session backend toggle must not leak capture
	// bits into live peds (R8). Exit() is a no-op on fresh slots.
	for (int i = 0; i < SOFTBODY_MAX_RAGDOLLS; i++)
		g_softbodyRagdolls[i].Exit();
	dbglog("[Softbody] shutdown");
}
