// =============================================================================
// wind.cpp — wind field / gust / variation math (motion_fx family CHILD).
//
// PROVENANCE (2026-11-09 motion-family restructure): moved byte-identical from
// treeWind.cpp, where the wind math sat as a junior-era bolt-on next to the
// unrelated TreeAlpha system. It is now the pure-math CHILD of the motion_fx
// family (see motion_fx.h): no noise service, no pipeline writes, no family
// integration — the PARENT (motion_fx.cpp) owns the animation application and
// the randomness/physics seams and consumes this TU through exactly two
// internal accessors (Wind_ClockMs / Wind_ComputeLean, local-extern idiom per
// postfx.cpp:39 — there is no pipelinecommon.h-style header for this pair).
// The public TreeWind_* API is unchanged (treeWind.h declarations); only the
// defining TU moved.
// =============================================================================
#include "skygfx.h"
#include "treeWind.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Cross-TU externs
// ---------------------------------------------------------------------------
// buildingPipe.cpp:231 — cdecl wrapper around CVisibilityPlugins::GetModelInfoIndex
// (0x732250; plugin-sdk declares it `static short ... (RpAtomic*)`, i.e. cdecl).
WRAPPER short CVisibilityPlugins__GetModelInfoIndex(RpAtomic *atomic) { EAXJMP(0x732250); }

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
// Family seams (the ONLY external-linkage symbols besides TreeWind_IsTreeAtomic)
// ---------------------------------------------------------------------------
// Internal motion_fx-family API — consumed by motion_fx.cpp (the PARENT) via
// the local-extern idiom (postfx.cpp:39); deliberately NOT in a header so the
// pure-math child stays invisible to every other TU.
//
// Wind_ClockMs: the family's ONE motion clock (game-ms accumulator above —
// see motion_fx.h for the documented one-clock decision and why it is not a
// QPC wall clock). The ms domain is the wind math's native unit (4096 ms /
// 65536 ms masks, 2^24 ms phase-invisible wrap).
float
Wind_ClockMs(void)
{
	return treeWind_clock();
}

// Wind_ComputeLean: trunk lean for one atomic, radians (image of local +Z).
// Pure ported math — the noise-family variation band is applied by the
// PARENT (motion_fx.cpp motionFx_lean), not here, so this TU stays free of
// randomness-family dependencies.
void
Wind_ComputeLean(RpAtomic *atomic, float *outX, float *outY)
{
	treeWind_computeLean(atomic, outX, outY);
}
