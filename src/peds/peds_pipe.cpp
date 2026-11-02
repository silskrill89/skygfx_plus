// peds_pipe.cpp — ped pipe control point, Phase 1 skeleton (INERT until wired).
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md
// Registry discipline copied from texdefs.cpp:285-317 (TexDefsPluginAttach):
//   - guarded one-shot registration (offset <= 0 re-try pattern),
//   - ctor zeroes the slot at clump creation (recycled-address safety),
//   - registration failure degrades to legacy behaviour, never breaks the
//     game's plugin chain.
//
// RW touchpoints: exactly ONE — RpClumpRegisterPlugin (WRAPPER/EAXJMP into
// the exe-baked RW 3.6, address from plugin-sdk RenderWare.cpp:2375). No
// D3D9 state writes, no device calls, no per-frame RW work in Phase 1.
//
// State storage: fixed static pool of 64 POD entries with an index
// free-list. No heap churn: acquire = pop index + zero fill, release =
// push index (done by the plugin destructor callback at clump destruction).

#include "peds_pipe.h"
#include <string.h>		// memset
#include <stdlib.h>		// atoi

// ---------------------------------------------------------------------------
// RW plugin machinery (exe-baked RW 3.6 — WRAPPER/EAXJMP only, no RW 3.7 lib).
// RpClumpRegisterPlugin lives in this TU (the only definition) — static so
// the merged module owns its only external-linkage use (texdefs.cpp:280-282).
// ---------------------------------------------------------------------------
static WRAPPER RwInt32 RpClumpRegisterPlugin(RwInt32, RwUInt32, RwPluginObjectConstructor, RwPluginObjectDestructor, RwPluginObjectCopy) { EAXJMP(0x74BDD0); }

// ---------------------------------------------------------------------------
// Module state (all POD statics — no dynamic init, no dtors at detach).
// ---------------------------------------------------------------------------

// Clump plugin slot id "Pdp1" — design doc §2.2. Guarded against the ids
// already in play: 0xEDED / 0x131 (atomic, texdefs.cpp:288-300),
// 0xbadeaffe / 0x4E4D3137 (texture, texdefs.cpp:303-312).
static const RwUInt32 PEDS_CLUMP_PLUGIN_ID = 0x50647031;

static RwInt32	s_slotOffset;		// > 0 once registered (texdefs.cpp:87-95 guard pattern)
static bool		s_attached;			// registration succeeded
static bool		s_enabled;			// THE gate: single bool read (pedsPipeEnable && attached)

// [Ped] config mirror (design doc §6). File-static, lane-scoped: main.cpp's
// readIniFile calls PedsPipe_ReadConfig per lane parse; the LAST parse wins
// for the active lane, matching the c == config active-lane discipline.
struct PedsPipeConfig
{
	int		pipeEnable;			// pedsPipeEnable    — Phase 1 wrap lever (0 = legacy inline path)
	int		skinShaderEnable;	// skinShaderEnable  — Phase 2 FFP->shader swap
	int		hairEnable;			// hairEnable        — Phase 3 hair thread
	int		clothEnable;		// clothEnable       — Phase 4 cloth sim
};
static PedsPipeConfig	s_cfg = { 0, 0, 0, 0 };	// all-off default = vanilla behaviour

static uint32	s_deviceResetGen;	// bumped by PedsPipe_OnDeviceReset (latch invalidation)

// ---------------------------------------------------------------------------
// Fixed state pool: 64 POD entries + index free-list. No heap.
// ---------------------------------------------------------------------------
static const int PEDS_POOL_SIZE = 64;

static PedPipeState	s_pool[PEDS_POOL_SIZE];
static int			s_poolFree[PEDS_POOL_SIZE];	// stack of free indices
static int			s_poolFreeCount;

static void
pedsPoolReset(void)
{
	s_poolFreeCount = PEDS_POOL_SIZE;
	for(int i = 0; i < PEDS_POOL_SIZE; i++)
		s_poolFree[i] = PEDS_POOL_SIZE - 1 - i;	// pop order 0,1,2,... (LIFO bottom-up)
	memset(s_pool, 0, sizeof(s_pool));
}

static PedPipeState*
pedsPoolAcquire(RpClump *clump)
{
	if(s_poolFreeCount <= 0)
		return nil;
	int idx = s_poolFree[--s_poolFreeCount];
	PedPipeState *st = &s_pool[idx];
	memset(st, 0, sizeof(*st));		// a recycled pool entry can never surface stale state
	st->ownerClump = clump;
	return st;
}

static void
pedsPoolRelease(PedPipeState *st)
{
	if(!st)
		return;
	// owner guard: only the entry whose ownerClump still matches may return
	// (a stale pointer from a destroyed clump must not poison the free-list)
	int idx = (int)(st - s_pool);
	if(idx < 0 || idx >= PEDS_POOL_SIZE)
		return;
	if(s_pool[idx].ownerClump == nil)
		return;						// already released
	s_pool[idx].ownerClump = nil;
	if(s_poolFreeCount < PEDS_POOL_SIZE)
		s_poolFree[s_poolFreeCount++] = idx;
}

// ---------------------------------------------------------------------------
// Plugin callbacks (texdbCtor pattern, texdefs.cpp:273-278).
// ---------------------------------------------------------------------------

static void*
pedsCtor(void *object, RwInt32 offsetInObject, RwInt32)
{
	// Zero the SLOT at clump creation — a recycled RpClump* address can
	// never surface stale ped state. No pool interaction here: allocation
	// is lazy (PedsPipe_AcquireState), so non-ped clumps cost one store.
	*RWPLUGINOFFSET(PedPipeState*, object, offsetInObject) = nil;
	return object;
}

static void*
pedsDtor(void *object, RwInt32 offsetInObject, RwInt32)
{
	PedPipeState *st = *RWPLUGINOFFSET(PedPipeState*, object, offsetInObject);
	if(st){
		pedsPoolRelease(st);
		*RWPLUGINOFFSET(PedPipeState*, object, offsetInObject) = nil;
	}
	return object;
}

static void*
pedsCopy(void *dstObject, const void *srcObject, RwInt32 offsetInObject, RwInt32)
{
	// Clump clone: dst gets NO state (re-acquired lazily if the clone is a
	// ped). Copying the pointer would alias one pool entry across two
	// clumps — the double-release guard would eat it, but aliasing is
	// wrong by construction, so start clean instead.
	*RWPLUGINOFFSET(PedPipeState*, dstObject, offsetInObject) = nil;
	return dstObject;
}

// ---------------------------------------------------------------------------
// Enabled-gate recompute (the only writer of s_enabled).
// ---------------------------------------------------------------------------

static void
pedsRecomputeEnabled(void)
{
	s_enabled = s_attached && s_cfg.pipeEnable != 0;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

void
PedsPipe_PluginAttach(void)
{
	// Guarded one-shot (texdefs.cpp:288 pattern). Idempotent under repeated
	// myPluginAttach timings.
	if(s_slotOffset > 0)
		return;

	pedsPoolReset();

	s_slotOffset = RpClumpRegisterPlugin(sizeof(PedPipeState*), PEDS_CLUMP_PLUGIN_ID, pedsCtor, pedsDtor, pedsCopy);
	if(s_slotOffset <= 0){
		// Collision or registry failure: log, degrade to legacy, NEVER break
		// the game chain (always-return-1 rule, texdefs.cpp:316 — attach is
		// void here, the "never fail" property is the absence of any throw/
		// abort plus the unattached degrade below).
		dbglog("[peds_pipe] RpClumpRegisterPlugin FAILED (id=0x%08X offset=%d) — module degrades to legacy ped path",
			PEDS_CLUMP_PLUGIN_ID, s_slotOffset);
		s_attached = false;
	}else{
		s_attached = true;
		dbglog("[peds_pipe] plugin attached slot=0x%08X offset=%d pool=%d",
			PEDS_CLUMP_PLUGIN_ID, s_slotOffset, PEDS_POOL_SIZE);
	}

	pedsRecomputeEnabled();
}

bool
PedsPipe_GetEnabled(void)
{
	return s_enabled;	// single bool read — the cheap early-out contract
}

void
PedsPipe_OnDeviceReset(void)
{
	// Latch invalidation stub. Phase 1 owns no device objects (design doc
	// §1.3: chars_OnDeviceReset precedent, chars.h:49). Phase 2+ sub-modules
	// invalidate cached device pointers here and rebuild lazily on first use
	// (EnsureSSSRasters pattern, chars.cpp:170-176).
	s_deviceResetGen++;
	if(dbglog_once("peds_reset"))
		dbglog("[peds_pipe] device reset gen=%u (no device objects owned in Phase 1)", s_deviceResetGen);
}

PedPipeState*
PedsPipe_GetState(RpClump *clump)
{
	if(!s_enabled)		// cheap early-out: single bool read
		return nil;
	if(!clump || s_slotOffset <= 0)
		return nil;
	return *RWPLUGINOFFSET(PedPipeState*, clump, s_slotOffset);
}

PedPipeState*
PedsPipe_AcquireState(RpClump *clump)
{
	if(!s_enabled)		// cheap early-out: single bool read
		return nil;
	if(!clump || s_slotOffset <= 0)
		return nil;

	PedPipeState **slot = RWPLUGINOFFSET(PedPipeState*, clump, s_slotOffset);
	if(*slot)
		return *slot;	// already acquired — idempotent

	PedPipeState *st = pedsPoolAcquire(clump);
	if(!st){
		if(dbglog_throttle("peds_pool_full"))
			dbglog("[peds_pipe] state pool exhausted (%d) — clump %p degrades to legacy path", PEDS_POOL_SIZE, clump);
		return nil;
	}
	*slot = st;
	return st;
}

void
PedsPipe_ReadConfig(linb::ini &cfg)
{
	// [Ped] section — lane-scoped keys (design doc §6). Missing keys parse
	// as 0 via atoi("") so the all-off default needs no special case.
	// Local atoi/strtod parsing: main.cpp's readint/readfloat are static to
	// that TU and this module must not add dependencies on it (Phase 2
	// wiring is a single call line, not a linkage change).
	s_cfg.pipeEnable      = atoi(cfg.get("Ped", "pedsPipeEnable", "").c_str());
	s_cfg.skinShaderEnable = atoi(cfg.get("Ped", "skinShaderEnable", "").c_str());
	s_cfg.hairEnable      = atoi(cfg.get("Ped", "hairEnable", "").c_str());
	s_cfg.clothEnable     = atoi(cfg.get("Ped", "clothEnable", "").c_str());

	pedsRecomputeEnabled();

	if(dbglog_once("peds_cfg"))
		dbglog("[peds_pipe] [Ped] config: pedsPipeEnable=%d skinShaderEnable=%d hairEnable=%d clothEnable=%d (attached=%d)",
			s_cfg.pipeEnable, s_cfg.skinShaderEnable, s_cfg.hairEnable, s_cfg.clothEnable, s_attached);
}

int
PedsPipe_GetSlotOffset(void)
{
	return s_slotOffset;
}

bool
PedsPipe_ProbeOnce(const char *tag)
{
	if(!tag)
		return false;
	return dbglog_once(tag);
}
