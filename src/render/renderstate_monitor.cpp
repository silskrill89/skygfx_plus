// renderstate_monitor.cpp — three-layer render-state divergence monitor.
//
// WHY
//   Every render state in this project can be written through three layers:
//     L1  rw cache        RwRenderStateSet / RwRenderStateGet
//     L2  RW D3D9 driver  RwD3D9SetRenderState / RwD3D9GetRenderState
//                          (gta_sa 0x7FC2D0 / 0x7FC320: pending + dirty list,
//                           flushed by _rwD3D9RenderStateFlushCache 0x7FC200
//                           before every draw)
//     L3  raw device      IDirect3DDevice9::SetRenderState / GetRenderState
//   A raw postfx/DepthHook/Im2D write moves only L3. A direct
//   RwD3D9SetRenderState (e.g. D3DRS_CULLMODE) moves only L2. A value-compare
//   cache that already holds the requested value makes the next set a no-op,
//   so one missing layer desyncs the other two PERMANENTLY. This module is
//   the evidence trail: it reads all three layers and reports every state
//   that is not identical across them.
//
// STATE MAP (11 states)
//   label      L1 rw render state                    L2/L3 D3D state
//   CULL       rwRENDERSTATECULLMODE                 D3DRS_CULLMODE
//   ALPHABLEND rwRENDERSTATEVERTEXALPHAENABLE        D3DRS_ALPHABLENDENABLE
//   SRCBLEND   rwRENDERSTATESRCBLEND                 D3DRS_SRCBLEND
//   DESTBLEND  rwRENDERSTATEDESTBLEND                D3DRS_DESTBLEND
//   ZWRITE     rwRENDERSTATEZWRITEENABLE             D3DRS_ZWRITEENABLE
//   ZTEST      rwRENDERSTATEZTESTENABLE              D3DRS_ZENABLE
//   ZFUNC      (no rw twin)                          D3DRS_ZFUNC
//   ALPHAFUNC  rwRENDERSTATEALPHATESTFUNCTION        D3DRS_ALPHAFUNC
//   ALPHAREF   rwRENDERSTATEALPHATESTFUNCTIONREF     D3DRS_ALPHAREF
//   FOG        rwRENDERSTATEFOGENABLE                D3DRS_FOGENABLE
//   BLENDOP    (no rw twin)                          D3DRS_BLENDOP
//
//   rw and D3D domains are index-identical where a twin exists:
//   cull NONE=1, BACK/CW=2, FRONT/CCW=3; blend factors 1..11; alpha-test
//   functions NEVER=1..ALWAYS=8 (verified in gta_sa.exe + RW docs — see
//   pipelinecommon.cpp pipeForceCullMode and postfx_utils.cpp). Booleans are
//   normalised to 0/1 before comparing; the raw values are printed.
//
// LOG FORMAT
//   rs_mon: FIRST_DIVERGE site=<site> pipe=<id> n=<site count> | CULL rw=2 drv=2 dev=1 | ...
//   rs_mon: DIVERGE site=<site> pipe=<id> n=<site count> | ...
//   rs_mon_report: samples=<n> agree=<n> diverge=<n> pct=<n> bad=<n> sites=<n>
//                  top=<site> top_n=<n> transient=<n> cache=<n>
//
//   Only mismatching states are printed. States with no rw-cache twin print
//   rw=- (ZFUNC, BLENDOP). `n=` is omitted when the per-site table is full and
//   the site is untracked. The first divergence for a site is always
//   unthrottled (per-site table); later divergences share the single
//   "rs_mon" 2 s bucket. Report uses its own 5 s gate (the shared
//   dbglog_throttle interval is 2 s, and a shared bucket can be starved).
//
// CLASSIFICATION (aggregate semantics — 2026-11-02 fix, recalibrated
// 2026-11-08 for N1/N2 verdict accuracy)
//   The per-sample lines above are the RAW truth: any cross-layer difference
//   logs. The aggregate report classifies instead of raw-counting, because
//   the two frame-edge sites legitimately differ in steady state:
//     scene_start fires BEFORE the frame's first draw — the device still
//     holds the previous frame's postfx/HUD tail (raw L3 writes are the
//     postfx chain's designed behaviour, and the driver flushes pending
//     state only before a draw). Raw-counting that as "diverged" made every
//     healthy gameplay session report 100% diverge (13885/13885, bad=0 —
//     every line showed rw==drv with only dev at the postfx tail values).
//   Verdict rule (the FLUSH-LAG NOTE's entry/exit comparison, automated,
//   with the layer split that the N1/N2 receipts depend on):
//     agree      sample where all three layers match (raw, per sample).
//     transient  DEVICE-ONLY entry diff that is GONE by the paired exit
//                sample (flush-lag/postfx tail healed within its frame), or
//                an entry diff that never got an exit verdict (unpaired —
//                loading screen, early out). Mid-frame DEVICE-ONLY diffs
//                between writes carry no verdict and are transient.
//     diverge    (a) any diff still present at the EXIT sample; OR
//                (b) an L1<->L2 CACHE SPLIT at entry, or at any mid-frame
//                site. The rw cache (L1) and the RW D3D9 driver cache (L2)
//                are BOTH value-compare caches: a disagreement between them
//                has no self-healing path (the next matching Set is a
//                no-op), so the split survives until an explicit
//                park-and-set repair. That makes re-created-every-frame
//                drift persistent even when the frame's exit repair hides
//                it — the [PipeAlpha]/[zfix] repair-receipt class (N1/N2).
//                A cache split is counted ONCE per entry frame: the paired
//                exit resolves it, or a missing exit resolves it at the
//                next entry. ZFUNC/BLENDOP have no rw twin, so their only
//                comparison (L2 vs L3) stays device-class.
//     bad        capture faults (failed Get) — never classified as
//                agreement or divergence.
//   Entry/exit pairing uses the shared 1-in-30 frame latch in main.cpp:
//   frame_end only samples on frames where scene_start sampled, so each
//   entry diff has at most one exit verdict. At most one sample (the
//   stashed entry diff) sits between buckets until its verdict arrives.
//   `sites=`/`top=`/`top_n=` count PERSISTENT divergence only (exit
//   samples); a site with raw-but-healed diffs does not appear.
//
// FLUSH-LAG NOTE
//   The RW D3D9 driver applies its pending state before a draw. At callback
//   ENTRY the raw device can legitimately still hold the pre-entry value, so
//   a drv!=dev line from an entry sample can be flush lag, not drift. The
//   per-sample lines report the raw truth; the aggregate now applies the
//   entry/exit comparison automatically (see CLASSIFICATION above) — an
//   entry-only diff is reported as transient, never as diverged.
//
// FAULT SAFETY
//   Capture runs inside __try/__except with a POD-only local. A Get fault
//   drops the sample (counted in samples + bad, never in diverge) instead of
//   taking down the render callback. No heap, no C++ objects, static arrays
//   only.

#include "skygfx.h"
#include "renderstate_monitor.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// 32 (was 16): the recalibrated verdict is meant to be sampled at pipe /
// postfx edges too (2026-10-25 log-divergence-timeline §8.4), which greatly
// increases how many distinct sites reach the first-divergence table.
#define RSMON_MAX_SITES  32
#define RSMON_SITE_LEN   32
#define RSMON_MSG_LEN    1024
#define RSMON_REPORT_MS  5000

// The paired frame-edge sites (main.cpp call sites). The exit sample is the
// verdict point for the entry sample's diff — see CLASSIFICATION above.
#define RSMON_ENTRY_SITE "scene_start"
#define RSMON_EXIT_SITE  "frame_end"

struct RMonCapture {
	// L1 — rw render-state cache.
	RwUInt32 rwCull, rwBlend, rwSrc, rwDst, rwZWrite, rwZTest,
	         rwAlphaFunc, rwAlphaRef, rwFog;
	// L2 — RW D3D9 driver cache.
	DWORD drvCull, drvBlend, drvSrc, drvDst, drvZWrite, drvZEnable,
	      drvZFunc, drvAlphaFunc, drvAlphaRef, drvFog, drvBlendOp;
	// L3 — raw device.
	DWORD devCull, devBlend, devSrc, devDst, devZWrite, devZEnable,
	      devZFunc, devAlphaFunc, devAlphaRef, devFog, devBlendOp;
};

struct RMonSite {
	char name[RSMON_SITE_LEN];
	uint32 diverged;	// raw: every diff sample at this site (drives n=)
	uint32 persistent;	// real: diff survived to the exit verdict (drives top=)
};

// Runtime switch (RSMon_SetEnabled). RSMON_ENABLED is the compile-time one.
// ROUGH REDUCTION (2026-11-10): default OFF — measurement-only layer, no
// behavior; the divergence classes it watched are fixed and understood.
// Flip to 1 at runtime (RSMon_SetEnabled) when a new divergence hunt needs it.
static int s_enabled = 0;
static int s_initialized = 0;

static uint32 s_total = 0;
static uint32 s_agree = 0;
static uint32 s_diverged = 0;	// REAL divergence: survived the exit verdict
static uint32 s_transient = 0;	// device-only entry diff: healed by exit (or unpaired)
static uint32 s_faults = 0;
static uint32 s_cacheSplits = 0;	// samples whose raw diff held an L1<->L2 split

// Entry-diff stash: 1 while a scene_start diff awaits its frame_end verdict.
// s_entryCacheMask remembers the L1<->L2 subset of that diff: a cache split
// is the persistent class even when a repair hides it before the exit. At
// most one entry sample is in flight (see the frame-edge pairing in main.cpp).
static int s_frameEntryPending = 0;
static uint32 s_entryCacheMask = 0;

static struct RMonSite s_sites[RSMON_MAX_SITES];
static int s_siteCount = 0;

static DWORD s_lastReportTick = 0;
static int s_reportPrimed = 0;

// State bits returned by rsmonDiffMask; one bit per monitored state.
enum {
	RSMON_CULL       = 1u << 0,
	RSMON_ALPHABLEND = 1u << 1,
	RSMON_SRCBLEND   = 1u << 2,
	RSMON_DESTBLEND  = 1u << 3,
	RSMON_ZWRITE     = 1u << 4,
	RSMON_ZTEST      = 1u << 5,
	RSMON_ZFUNC      = 1u << 6,
	RSMON_ALPHAFUNC  = 1u << 7,
	RSMON_ALPHAREF   = 1u << 8,
	RSMON_FOG        = 1u << 9,
	RSMON_BLENDOP    = 1u << 10,
};

// ---------------------------------------------------------------------------
// Capture — all 11 states from all three layers.
// Returns 0 when the device is absent or the capture faulted/failed.
// ---------------------------------------------------------------------------

// The whole struct is 32-bit fields. A failed Get leaves the 0xFF sentinel in
// place; detect it per field (no type-punning). A failed read must count as a
// fault, never as a fabricated agreement (0xFFFFFFFF normalises to TRUE).
#define RSMON_SENT(v) ((v) == 0xFFFFFFFFu)

static int
rsmonHasSentinel(const struct RMonCapture *c)
{
	return RSMON_SENT(c->rwCull) || RSMON_SENT(c->rwBlend) ||
	       RSMON_SENT(c->rwSrc) || RSMON_SENT(c->rwDst) ||
	       RSMON_SENT(c->rwZWrite) || RSMON_SENT(c->rwZTest) ||
	       RSMON_SENT(c->rwAlphaFunc) || RSMON_SENT(c->rwAlphaRef) ||
	       RSMON_SENT(c->rwFog) ||
	       RSMON_SENT(c->drvCull) || RSMON_SENT(c->drvBlend) ||
	       RSMON_SENT(c->drvSrc) || RSMON_SENT(c->drvDst) ||
	       RSMON_SENT(c->drvZWrite) || RSMON_SENT(c->drvZEnable) ||
	       RSMON_SENT(c->drvZFunc) || RSMON_SENT(c->drvAlphaFunc) ||
	       RSMON_SENT(c->drvAlphaRef) || RSMON_SENT(c->drvFog) ||
	       RSMON_SENT(c->drvBlendOp) ||
	       RSMON_SENT(c->devCull) || RSMON_SENT(c->devBlend) ||
	       RSMON_SENT(c->devSrc) || RSMON_SENT(c->devDst) ||
	       RSMON_SENT(c->devZWrite) || RSMON_SENT(c->devZEnable) ||
	       RSMON_SENT(c->devZFunc) || RSMON_SENT(c->devAlphaFunc) ||
	       RSMON_SENT(c->devAlphaRef) || RSMON_SENT(c->devFog) ||
	       RSMON_SENT(c->devBlendOp);
}

#undef RSMON_SENT

static int
rsmonCapture(struct RMonCapture *c)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return 0;

	// 0xFF sentinel: a failed Get must never look like an agreement.
	memset(c, 0xFF, sizeof(*c));

	__try{
		// L1 — rw render-state cache.
		RwRenderStateGet(rwRENDERSTATECULLMODE, (void*)&c->rwCull);
		RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&c->rwBlend);
		RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&c->rwSrc);
		RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&c->rwDst);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&c->rwZWrite);
		RwRenderStateGet(rwRENDERSTATEZTESTENABLE, (void*)&c->rwZTest);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&c->rwAlphaFunc);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&c->rwAlphaRef);
		RwRenderStateGet(rwRENDERSTATEFOGENABLE, (void*)&c->rwFog);

		// L2 — RW D3D9 driver cache (pending state + dirty list).
		RwD3D9GetRenderState(D3DRS_CULLMODE, &c->drvCull);
		RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &c->drvBlend);
		RwD3D9GetRenderState(D3DRS_SRCBLEND, &c->drvSrc);
		RwD3D9GetRenderState(D3DRS_DESTBLEND, &c->drvDst);
		RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &c->drvZWrite);
		RwD3D9GetRenderState(D3DRS_ZENABLE, &c->drvZEnable);
		RwD3D9GetRenderState(D3DRS_ZFUNC, &c->drvZFunc);
		RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &c->drvAlphaFunc);
		RwD3D9GetRenderState(D3DRS_ALPHAREF, &c->drvAlphaRef);
		RwD3D9GetRenderState(D3DRS_FOGENABLE, &c->drvFog);
		RwD3D9GetRenderState(D3DRS_BLENDOP, &c->drvBlendOp);

		// L3 — raw IDirect3DDevice9.
		dev->GetRenderState(D3DRS_CULLMODE, &c->devCull);
		dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &c->devBlend);
		dev->GetRenderState(D3DRS_SRCBLEND, &c->devSrc);
		dev->GetRenderState(D3DRS_DESTBLEND, &c->devDst);
		dev->GetRenderState(D3DRS_ZWRITEENABLE, &c->devZWrite);
		dev->GetRenderState(D3DRS_ZENABLE, &c->devZEnable);
		dev->GetRenderState(D3DRS_ZFUNC, &c->devZFunc);
		dev->GetRenderState(D3DRS_ALPHAFUNC, &c->devAlphaFunc);
		dev->GetRenderState(D3DRS_ALPHAREF, &c->devAlphaRef);
		dev->GetRenderState(D3DRS_FOGENABLE, &c->devFog);
		dev->GetRenderState(D3DRS_BLENDOP, &c->devBlendOp);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		return 0;
	}

	if(rsmonHasSentinel(c))
		return 0;

	return 1;
}

// ---------------------------------------------------------------------------
// Comparison — one bitmask drives both detection and message formatting, so
// the two can never drift apart.
// ---------------------------------------------------------------------------
static int
rsmonBoolDiff(uint32 a, uint32 b, uint32 c)
{
	uint32 na = a ? 1u : 0u;
	uint32 nb = b ? 1u : 0u;
	uint32 nc = c ? 1u : 0u;
	return (na != nb) || (na != nc);
}

static uint32
rsmonDiffMask(const struct RMonCapture *c)
{
	uint32 m = 0;

	if(c->rwCull != c->drvCull || c->rwCull != c->devCull)
		m |= RSMON_CULL;
	if(rsmonBoolDiff(c->rwBlend, c->drvBlend, c->devBlend))
		m |= RSMON_ALPHABLEND;
	if(c->rwSrc != c->drvSrc || c->rwSrc != c->devSrc)
		m |= RSMON_SRCBLEND;
	if(c->rwDst != c->drvDst || c->rwDst != c->devDst)
		m |= RSMON_DESTBLEND;
	if(rsmonBoolDiff(c->rwZWrite, c->drvZWrite, c->devZWrite))
		m |= RSMON_ZWRITE;
	if(rsmonBoolDiff(c->rwZTest, c->drvZEnable, c->devZEnable))
		m |= RSMON_ZTEST;
	if(c->drvZFunc != c->devZFunc)
		m |= RSMON_ZFUNC;
	if(c->rwAlphaFunc != c->drvAlphaFunc || c->rwAlphaFunc != c->devAlphaFunc)
		m |= RSMON_ALPHAFUNC;
	if(c->rwAlphaRef != c->drvAlphaRef || c->rwAlphaRef != c->devAlphaRef)
		m |= RSMON_ALPHAREF;
	if(rsmonBoolDiff(c->rwFog, c->drvFog, c->devFog))
		m |= RSMON_FOG;
	if(c->drvBlendOp != c->devBlendOp)
		m |= RSMON_BLENDOP;

	return m;
}

// ---------------------------------------------------------------------------
// Cache-split mask — the subset of the diff that is an L1<->L2 disagreement.
//
// Recalibration (2026-11-08, N1/N2 verdict accuracy): not every cross-layer
// difference means the same thing. The rw cache (L1) and the RW D3D9 driver
// cache (L2) are both value-compare caches; when THEY disagree there is no
// self-healing path — the next matching Set is a no-op and the split survives
// until an explicit park-and-set repair. That is the persistent-desync class
// (`[PipeAlpha]` L2=1/L3=0 replays, `[zfix]` drift repairs). A diff between
// the caches and the raw device (L3) may instead be the driver's pending
// state waiting for the next draw flush (FLUSH-LAG NOTE) and is benign at an
// entry sample. Keeping the two apart is what stops a repaired-but-
// re-created split from being reported as transient.
//
// ZFUNC/BLENDOP have no rw twin: their only comparison is L2 vs L3, so they
// stay out of this mask (device-class, verdict at the exit sample).
// ---------------------------------------------------------------------------
static uint32
rsmonCacheSplitMask(const struct RMonCapture *c)
{
	uint32 m = 0;
	uint32 rwB = c->rwBlend ? 1u : 0u, drvB = c->drvBlend ? 1u : 0u;
	uint32 rwW = c->rwZWrite ? 1u : 0u, drvW = c->drvZWrite ? 1u : 0u;
	uint32 rwT = c->rwZTest ? 1u : 0u, drvT = c->drvZEnable ? 1u : 0u;
	uint32 rwF = c->rwFog ? 1u : 0u, drvF = c->drvFog ? 1u : 0u;

	if(c->rwCull != c->drvCull)
		m |= RSMON_CULL;
	if(rwB != drvB)
		m |= RSMON_ALPHABLEND;
	if(c->rwSrc != c->drvSrc)
		m |= RSMON_SRCBLEND;
	if(c->rwDst != c->drvDst)
		m |= RSMON_DESTBLEND;
	if(rwW != drvW)
		m |= RSMON_ZWRITE;
	if(rwT != drvT)
		m |= RSMON_ZTEST;
	if(c->rwAlphaFunc != c->drvAlphaFunc)
		m |= RSMON_ALPHAFUNC;
	if(c->rwAlphaRef != c->drvAlphaRef)
		m |= RSMON_ALPHAREF;
	if(rwF != drvF)
		m |= RSMON_FOG;

	return m;
}

// ---------------------------------------------------------------------------
// Canonical depth contract (ORTHOGONAL axis — [zfix2] receipt).
//
// The diff/cache-split masks above compare the three layers against EACH
// OTHER and answer "which layer moved?". They are blind to a state where all
// three layers AGREE on a WRONG value — the N2 "zfunc=8 dead-depth" signature
// (rw ztest=0, drv zen=0 zfunc=8, dev zen=0 zfunc=8) has rsmonDiffMask()==0
// yet is broken depth. This comparator checks each layer against a FIXED
// canonical contract instead:
//     ALPHABLEND = 0   ZWRITE = 1   ZTEST = 1   ZFUNC = LESSEQUAL (=4)
// ZTEST reads D3DRS_ZENABLE on L2/L3 (the ZTEST row's D3D twin); ZFUNC has no
// rw twin so L1 is excluded. Booleans are normalised to truthiness (the same
// rule as rsmonBoolDiff) so the contract is "on/off", not a raw register
// value. This is a distinct axis from diverge/transient/cache-split and is
// never folded into those counters.
// ---------------------------------------------------------------------------
#define RSMON_CT_BLEND  0u               // ALPHABLEND canonical: off
#define RSMON_CT_ZWRITE 1u               // ZWRITE canonical: on
#define RSMON_CT_ZTEST  1u               // ZTEST canonical: on
#define RSMON_CT_ZFUNC  D3DCMP_LESSEQUAL // ZFUNC canonical: LESSEQUAL (=4)

static uint32
rsmonContractMask(const struct RMonCapture *c, uint32 *rwMask, uint32 *drvMask,
                  uint32 *devMask)
{
	uint32 rwB = c->rwBlend ? 1u : 0u, drvB = c->drvBlend ? 1u : 0u, devB = c->devBlend ? 1u : 0u;
	uint32 rwW = c->rwZWrite ? 1u : 0u, drvW = c->drvZWrite ? 1u : 0u, devW = c->devZWrite ? 1u : 0u;
	uint32 rwT = c->rwZTest ? 1u : 0u, drvT = c->drvZEnable ? 1u : 0u, devT = c->devZEnable ? 1u : 0u;
	uint32 rw = 0, drv = 0, dev = 0;

	if(rwB != RSMON_CT_BLEND)  rw  |= RSMON_ALPHABLEND;
	if(drvB != RSMON_CT_BLEND) drv |= RSMON_ALPHABLEND;
	if(devB != RSMON_CT_BLEND) dev |= RSMON_ALPHABLEND;
	if(rwW != RSMON_CT_ZWRITE)  rw  |= RSMON_ZWRITE;
	if(drvW != RSMON_CT_ZWRITE) drv |= RSMON_ZWRITE;
	if(devW != RSMON_CT_ZWRITE) dev |= RSMON_ZWRITE;
	if(rwT != RSMON_CT_ZTEST)  rw  |= RSMON_ZTEST;
	if(drvT != RSMON_CT_ZTEST) drv |= RSMON_ZTEST;
	if(devT != RSMON_CT_ZTEST) dev |= RSMON_ZTEST;
	if(c->drvZFunc != (DWORD)RSMON_CT_ZFUNC) drv |= RSMON_ZFUNC;
	if(c->devZFunc != (DWORD)RSMON_CT_ZFUNC) dev |= RSMON_ZFUNC;

	if(rwMask)  *rwMask  = rw;
	if(drvMask) *drvMask = drv;
	if(devMask) *devMask = dev;
	return rw | drv | dev;
}

// Resolve the stashed entry diff once the frame's verdict is known.
// `persistent` = the frame did not heal it, OR it was an L1<->L2 split
// (structural: re-created until repaired).
static void
rsmonResolveEntry(int persistent)
{
	if(!s_frameEntryPending)
		return;
	if(persistent)
		s_diverged++;
	else
		s_transient++;
	s_frameEntryPending = 0;
	s_entryCacheMask = 0;
}

static int
rsmonAppend(char *buf, size_t cap, int len, const char *fmt, ...)
{
	va_list ap;
	int n;

	if(len < 0 || (size_t)len >= cap)
		return len;

	va_start(ap, fmt);
	n = vsnprintf(buf + len, cap - (size_t)len, fmt, ap);
	va_end(ap);

	if(n < 0)
		return len;
	if((size_t)n >= cap - (size_t)len)
		return (int)(cap - 1);
	return len + n;
}

// label + all three layers
#define RSMON_APPEND3(label, rwv, drvv, devv) \
	len = rsmonAppend(buf, cap, len, " | " label " rw=%u drv=%u dev=%u", \
		(unsigned)(rwv), (unsigned)(drvv), (unsigned)(devv))
// label + L2/L3 only (no rw-cache twin)
#define RSMON_APPEND2(label, drvv, devv) \
	len = rsmonAppend(buf, cap, len, " | " label " rw=- drv=%u dev=%u", \
		(unsigned)(drvv), (unsigned)(devv))

static void
rsmonFormat(char *buf, size_t cap, const char *kind, const char *site,
            int pipeId, int tracked, uint32 siteDiverged, uint32 mask,
            const struct RMonCapture *c)
{
	int len;

	if(cap == 0 || !buf)
		return;
	len = snprintf(buf, cap, "rs_mon: %s site=%s pipe=%d", kind, site, pipeId);

	if(len < 0){
		buf[0] = 0;
		return;
	}
	if((size_t)len >= cap)
		len = (int)(cap - 1);
	if(tracked)
		len = rsmonAppend(buf, cap, len, " n=%u", (unsigned)siteDiverged);

	// Order matches the STATE MAP table at the top of the file.
	if(mask & RSMON_CULL)
		RSMON_APPEND3("CULL", c->rwCull, c->drvCull, c->devCull);
	if(mask & RSMON_ALPHABLEND)
		RSMON_APPEND3("ALPHABLEND", c->rwBlend, c->drvBlend, c->devBlend);
	if(mask & RSMON_SRCBLEND)
		RSMON_APPEND3("SRCBLEND", c->rwSrc, c->drvSrc, c->devSrc);
	if(mask & RSMON_DESTBLEND)
		RSMON_APPEND3("DESTBLEND", c->rwDst, c->drvDst, c->devDst);
	if(mask & RSMON_ZWRITE)
		RSMON_APPEND3("ZWRITE", c->rwZWrite, c->drvZWrite, c->devZWrite);
	if(mask & RSMON_ZTEST)
		RSMON_APPEND3("ZTEST", c->rwZTest, c->drvZEnable, c->devZEnable);
	if(mask & RSMON_ZFUNC)
		RSMON_APPEND2("ZFUNC", c->drvZFunc, c->devZFunc);
	if(mask & RSMON_ALPHAFUNC)
		RSMON_APPEND3("ALPHAFUNC", c->rwAlphaFunc, c->drvAlphaFunc, c->devAlphaFunc);
	if(mask & RSMON_ALPHAREF)
		RSMON_APPEND3("ALPHAREF", c->rwAlphaRef, c->drvAlphaRef, c->devAlphaRef);
	if(mask & RSMON_FOG)
		RSMON_APPEND3("FOG", c->rwFog, c->drvFog, c->devFog);
	if(mask & RSMON_BLENDOP)
		RSMON_APPEND2("BLENDOP", c->drvBlendOp, c->devBlendOp);
}

#undef RSMON_APPEND3
#undef RSMON_APPEND2

// ---------------------------------------------------------------------------
// [zfix2] receipt — canonical-contract violations, per layer. Only fields
// violated on ANY layer are printed, with the canonical value in parentheses.
// rw=/drv=/dev= are per-layer violation bitmasks (RSMON_* bits); ZFUNC shows
// rw=- (no rw twin). Order matches the STATE MAP at the top of the file.
// ---------------------------------------------------------------------------
static void
rsmonContractFormat(char *buf, size_t cap, const char *site, int pipeId,
                    uint32 rwMask, uint32 drvMask, uint32 devMask,
                    const struct RMonCapture *c)
{
	int len;
	uint32 total;

	if(cap == 0 || !buf)
		return;
	len = snprintf(buf, cap, "[zfix2] CONTRACT site=%s pipe=%d rw=%02X drv=%02X dev=%02X",
	               site, pipeId, (unsigned)rwMask, (unsigned)drvMask, (unsigned)devMask);
	if(len < 0){
		buf[0] = 0;
		return;
	}
	if((size_t)len >= cap)
		len = (int)(cap - 1);

	total = rwMask | drvMask | devMask;
	if(total & RSMON_ALPHABLEND)
		len = rsmonAppend(buf, cap, len, " | ALPHABLEND(0) rw=%u drv=%u dev=%u",
			(unsigned)c->rwBlend, (unsigned)c->drvBlend, (unsigned)c->devBlend);
	if(total & RSMON_ZWRITE)
		len = rsmonAppend(buf, cap, len, " | ZWRITE(1) rw=%u drv=%u dev=%u",
			(unsigned)c->rwZWrite, (unsigned)c->drvZWrite, (unsigned)c->devZWrite);
	if(total & RSMON_ZTEST)
		len = rsmonAppend(buf, cap, len, " | ZTEST(1) rw=%u drv=%u dev=%u",
			(unsigned)c->rwZTest, (unsigned)c->drvZEnable, (unsigned)c->devZEnable);
	if(total & RSMON_ZFUNC)
		len = rsmonAppend(buf, cap, len, " | ZFUNC(4) rw=- drv=%u dev=%u",
			(unsigned)c->drvZFunc, (unsigned)c->devZFunc);
}

// ---------------------------------------------------------------------------
// Per-site table (16 entries; site name truncated to 31 chars).
// ---------------------------------------------------------------------------
static int
rsmonFindSite(const char *name)
{
	int i;
	for(i = 0; i < s_siteCount; i++){
		if(strcmp(s_sites[i].name, name) == 0)
			return i;
	}
	return -1;
}

static void
rsmonCopySite(char *dst, const char *src)
{
	size_t i;
	for(i = 0; i + 1 < RSMON_SITE_LEN && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

static int
rsmonAddSite(const char *name)
{
	int i;
	if(s_siteCount >= RSMON_MAX_SITES)
		return -1;
	i = s_siteCount++;
	rsmonCopySite(s_sites[i].name, name);
	s_sites[i].diverged = 0;
	s_sites[i].persistent = 0;
	return i;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void
RSMon_Init(void)
{
	if(s_initialized)
		return;
	s_initialized = 1;
	RSMon_Reset();
	if(RSMON_ENABLED)
		dbglog("rs_mon: init enabled=%d — L1 rw cache / L2 RW D3D9 driver / L3 raw device, 11 states; device-only entry diff=transient, L1<->L2 cache split or exit-persistent=diverged",
			s_enabled);
}

void
RSMon_Sample(const char *site, int pipeId)
{
	const char *name;
	struct RMonCapture c;
	uint32 mask, cacheMask;
	uint32 siteCount;
	int si, first;
	int isEntry, isExit;

	if(!RSMON_ENABLED || !s_enabled)
		return;
	if(!s_initialized)
		RSMon_Init();

	name = site ? site : "null";
	isEntry = strcmp(name, RSMON_ENTRY_SITE) == 0;
	isExit = strcmp(name, RSMON_EXIT_SITE) == 0;

	s_total++;
	if(!rsmonCapture(&c)){
		s_faults++;
		return;
	}

	mask = rsmonDiffMask(&c);
	cacheMask = rsmonCacheSplitMask(&c);
	if(cacheMask)
		s_cacheSplits++;

	// --- classification (see CLASSIFICATION note at the top of the file) ---
	// The exit sample is the verdict point for the entry sample's stash.
	// Raw per-sample logging below is untouched: it stays keyed on the raw
	// mask so the evidence trail keeps reporting every real difference.
	if(isExit){
		if(mask == 0){
			s_agree++;
			// Clean exit: resolve the stashed entry diff. A device-only
			// entry diff is transient; an L1<->L2 cache split is persistent
			// even though a repair hid it before this sample.
			if(s_frameEntryPending)
				rsmonResolveEntry(s_entryCacheMask != 0);
			return;	// healthy exit: no formatting, no log I/O
		}
		// Divergence survived the whole frame — the persistent-desync class.
		// One frame, one verdict: a pending entry diff resolves through the
		// same increment instead of being counted again.
		if(s_frameEntryPending)
			rsmonResolveEntry(1);
		else
			s_diverged++;
	}else{
		// Entry (or mid-frame) sample. An unresolved stash from a previous
		// ENTRY means its exit verdict never came (loading screen, early
		// out) — resolve it before stashing a new one. A mid-frame sample
		// must not consume the frame's pending entry verdict.
		if(isEntry && s_frameEntryPending)
			rsmonResolveEntry(s_entryCacheMask != 0);
		if(mask == 0){
			s_agree++;
			return;	// healthy sample: no formatting, no log I/O
		}
		if(isEntry){
			// Stash the entry diff; the paired exit (or the next entry if
			// the exit never came) supplies the verdict. Remember the
			// cache-split subset — that one is the persistent class.
			s_frameEntryPending = 1;
			s_entryCacheMask = cacheMask;
		}else if(cacheMask){
			// Mid-frame L1<->L2 split: no self-healing path -> persistent.
			s_diverged++;
		}else{
			// Mid-frame device-only diff between writes: flush-lag class.
			s_transient++;
		}
	}

	si = rsmonFindSite(name);
	first = 0;
	if(si < 0){
		si = rsmonAddSite(name);
		if(si >= 0)
			first = 1;	// first divergence for this site: always unthrottled
	}
	if(si >= 0){
		s_sites[si].diverged++;
		// Persistent observation: the exit verdict, or any L1<->L2 split
		// (structural, re-created until repaired).
		if(isExit || cacheMask)
			s_sites[si].persistent++;
	}
	siteCount = (si >= 0) ? s_sites[si].diverged : 0u;

	// One shared bucket ("rs_mon") avoids a tag explosion across sites; the
	// per-site first line above bypasses it so a cold site is never starved.
	if(first || dbglog_throttle("rs_mon")){
		char msg[RSMON_MSG_LEN];
		rsmonFormat(msg, sizeof(msg), first ? "FIRST_DIVERGE" : "DIVERGE",
		            name, pipeId, si >= 0, siteCount, mask, &c);
		dbglog("%s", msg);
	}
}

void
RSMon_ContractSample(const char *site, int pipeId)
{
	const char *name;
	struct RMonCapture c;
	uint32 rwMask, drvMask, devMask;
	static uint32 s_contractCalls = 0;

	if(!RSMON_ENABLED || !s_enabled)
		return;
	if(!s_initialized)
		RSMon_Init();

	// Hot-path call divisor (2026-11-08): the 4 pipe-edge sites fire per pipe
	// entry and every call costs the full 31-Get capture. Sample 1-in-16 calls
	// (first call always sampled) — still denser than main.cpp's 1-in-30 frame
	// gate on RSMon_Sample, at ~6% of the per-entry cost.
	if((s_contractCalls++ & 15u) != 0)
		return;

	name = site ? site : "null";

	// Measurement-only: a capture fault drops the sample; a held contract is
	// silent. Violations log under their own [zfix2] bucket and never touch
	// the diverge/transient/cache-split counters (orthogonal axis).
	if(!rsmonCapture(&c))
		return;
	if(!rsmonContractMask(&c, &rwMask, &drvMask, &devMask))
		return;

	if(dbglog_once("zfix2_first") || dbglog_throttle("zfix2")){
		char msg[RSMON_MSG_LEN];
		rsmonContractFormat(msg, sizeof(msg), name, pipeId,
		                    rwMask, drvMask, devMask, &c);
		dbglog("%s", msg);
	}
}

void
RSMon_Report(void)
{
	DWORD now;
	const char *topName = "(none)";
	uint32 topCount = 0;
	uint32 persistentSites = 0;
	uint32 pct;
	uint32 classified;
	int i;

	if(!RSMON_ENABLED || !s_enabled)
		return;

	// Own 5 s gate: DBGLOG_THROTTLE_MS is 2 s and a shared bucket can be
	// starved by the per-frame tags.
	now = GetTickCount();
	if(s_reportPrimed && (now - s_lastReportTick) < (DWORD)RSMON_REPORT_MS)
		return;
	s_reportPrimed = 1;
	s_lastReportTick = now;

	if(s_total == 0)
		return;	// no samples yet: stay silent instead of a zero heartbeat

	// top= scans PERSISTENT divergence only (exit-verdict survivors and
	// L1<->L2 cache splits). A site with raw-but-healed device-only diffs
	// must not appear here — that is exactly the benign frame-edge pattern
	// the transient bucket exists for.
	for(i = 0; i < s_siteCount; i++){
		if(s_sites[i].persistent > 0){
			persistentSites++;
			if(s_sites[i].persistent > topCount){
				topCount = s_sites[i].persistent;
				topName = s_sites[i].name;
			}
		}
	}
	// pct over CLASSIFIED samples (agree+diverge+transient), not over raw
	// sample count: faults and still-pending entry stashes are not verdicts,
	// so including them understated the rate (recalibrated 2026-11-08).
	classified = s_agree + s_diverged + s_transient;
	pct = classified ?
		(uint32)(((unsigned long long)s_diverged * 100ull) / classified) : 0u;

	dbglog("rs_mon_report: samples=%u agree=%u diverge=%u pct=%u bad=%u sites=%d top=%s top_n=%u transient=%u cache=%u",
		(unsigned)s_total, (unsigned)s_agree, (unsigned)s_diverged, (unsigned)pct,
		(unsigned)s_faults, persistentSites, topName, (unsigned)topCount,
		(unsigned)s_transient, (unsigned)s_cacheSplits);
}

void
RSMon_Reset(void)
{
	s_total = 0;
	s_agree = 0;
	s_diverged = 0;
	s_transient = 0;
	s_faults = 0;
	s_cacheSplits = 0;
	s_frameEntryPending = 0;
	s_entryCacheMask = 0;
	s_siteCount = 0;
	memset(s_sites, 0, sizeof(s_sites));
	s_reportPrimed = 0;
	s_lastReportTick = 0;
}

void
RSMon_SetEnabled(int enabled)
{
	s_enabled = enabled ? 1 : 0;
	if(RSMON_ENABLED)
		dbglog("rs_mon: enabled=%d", s_enabled);
}
