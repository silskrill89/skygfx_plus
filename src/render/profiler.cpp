// =============================================================================
// profiler.cpp — implementation of profiler.h (pass/frame profiler, prefix Pf/PF)
//
// WHAT IT MEASURES
//   * CPU wall time per PF_SCOPE("Name") block  — QPC (QueryPerformanceCounter
//     through skygfx.h's perfNow(), which self-initialises the frequency).
//   * GPU elapsed time per scope                  — IDirect3DQuery9
//     D3DQUERYTYPE_TIMESTAMP pair bracketed by a D3DQUERYTYPE_TIMESTAMPDISJOINT,
//     resolved one frame late (queries are asynchronous by nature). Opt-in via
//     g_pfGpuTimers: timestamp queries are NOT universally supported and cost
//     driver round-trips, so the default path is CPU-only.
//   * frame time          — gap between PfFrame() calls (once per frame).
//   * draw / vert / tri   — PfDraw() counters (per render call).
//   * PS/VS binds         — PfBindPS()/PfBindVS() last-bind + counts per tag.
//   * short notes         — PfNote() lossy ring, shown in the imgui timeline.
//
// COST WHEN DISABLED (g_pfEnable == 0)
//   Every entry point early-outs after a single volatile read: PfScope's ctor
//   clears `name` so the dtor is one branch, PfDraw/PfBind*/PfNote are one
//   branch, PfFrame keeps only the frame clock (two QPC calls) so enabling the
//   toggle mid-session never reports a multi-second "frame".
//
// THREADING: the RW/D3D9 render path is single-threaded (the same thread that
// calls DrawFinalEffects), so every table below is touched from one thread and
// needs no lock. g_pfEnable/g_pfGpuTimers are volatile because the imgui row
// writes them from the same thread while the game reads them.
//
// DEVICE SAFETY: queries belong to the IDirect3DDevice9 that created them. The
// device pointer is cached and every query is released + recreated when
// d3d9device changes (device reset / recreation), and the whole GPU path is
// abandoned for the session when a CreateQuery or a query op fails (device
// removal, driver without timestamp support) — the profiler degrades to CPU
// timings instead of crashing.
// =============================================================================

#include "profiler.h"
#include "skygfx.h"	// perfInit/perfNow, dbglog, d3d9.h
#include "gta.h"		// d3d9device (extern IDirect3DDevice9 *&)
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

volatile LONG g_pfEnable = 0;	// 0 = off; main.cpp sets it from Config
volatile LONG g_pfGpuTimers = 0;

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static const int   PF_MAX_SLOTS = 32;	// top-N scope names kept
static const int   PF_MAX_BINDS = 48;	// distinct shader tags tracked
static const int   PF_HIST      = 128;	// frame-time sparkline samples
static const int   PF_NOTE_SLOTS= 8;	// note ring depth
static const int   PF_NOTE_LEN  = 64;	// per-note chars (incl. NUL)
static const float PF_MAX_FRAME = 1000.0f;	// ms; anything above is a stall/hitch
static const double PF_LOG_MS   = 1000.0;	// dbglog summary interval

// ---------------------------------------------------------------------------
// Scope slots (name-hashed, fixed size)
// ---------------------------------------------------------------------------
struct PfSlot {
	const char *name;		// caller's string literal — stable for the process
	unsigned    hash;
	int         inUse;

	int         open;		// nesting depth (outermost owns the timers)
	double      t0ms;		// QPC ms of the outermost open
	double      cpuAcc;		// ms accumulated this frame
	int         closes;		// closes this frame (>0 => slot was hit)

	// GPU span state
	IDirect3DQuery9 *qStart;
	IDirect3DQuery9 *qEnd;
	IDirect3DQuery9 *qDisjoint;
	int         gpuOpen;		// disjoint BEGIN + start END issued
	int         gpuPending;	// end issued, result not read back yet
	int         gpuFailed;	// CreateQuery refused here — stop retrying

	// last completed frame + rolling stats
	float cpuMs, cpuMin, cpuAvg, cpuMax;
	float gpuMs, gpuMin, gpuAvg, gpuMax;
	int   frames;
};
static PfSlot s_slots[PF_MAX_SLOTS];
static int    s_slotCount = 0;
static int    s_overflow = 0;	// names dropped because the table is full

// ---------------------------------------------------------------------------
// GPU query ownership (one set per slot, one freq query shared)
// ---------------------------------------------------------------------------
static IDirect3DDevice9 *s_dev = NULL;	// device the queries belong to
static IDirect3DQuery9  *s_qFreq = NULL;
static long long         s_gpuFreq = 0;	// ticks/second, 0 until readable
static int               s_gpuBroken = 0;	// 1 => abandon GPU timers for the session

// ---------------------------------------------------------------------------
// Frame totals
// ---------------------------------------------------------------------------
static double s_frameT0 = 0.0;		// ms stamp of the previous PfFrame()
static float  s_frameMs = 0.0f, s_frameMin = 1e30f, s_frameAvg = 0.0f, s_frameMax = 0.0f;
static long   s_frameN = 0;
static long long s_draws = 0, s_verts = 0, s_tris = 0;	// running totals (PfDraw accumulates)
static long long s_lastDraws = 0, s_lastVerts = 0, s_lastTris = 0;	// last log snapshot
static int    s_psBinds = 0, s_vsBinds = 0;	// counts this frame
static float  s_hist[PF_HIST];			// linear, oldest first
static int    s_histN = 0;
static double s_lastLog = 0.0;
static int    s_inited = 0;

// ---------------------------------------------------------------------------
// Shader bind table (last-bind + counts per tag)
// ---------------------------------------------------------------------------
struct PfBindEnt {
	const char *tag;
	void       *lastPS, *lastVS;	// last bound pointer for this tag
	int         psTotal, vsTotal;	// since reset
	int         psFrame, vsFrame;	// last completed frame
	int         frames;		// frames this tag was bound on
};
static PfBindEnt s_binds[PF_MAX_BINDS];
static int       s_bindCount = 0;

// ---------------------------------------------------------------------------
// Note ring
// ---------------------------------------------------------------------------
static char s_notes[PF_NOTE_SLOTS][PF_NOTE_LEN];
static int  s_noteIdx = 0;	// next write position
static int  s_noteFill = 0;	// entries written (clamped to PF_NOTE_SLOTS)

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static unsigned
pfHash(const char *s)
{
	unsigned h = 2166136261u;
	while(*s){
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}
	return h ? h : 1u;
}

// Find (or allocate) the slot for `name`. NULL when the table is full.
static PfSlot *
pfSlot(const char *name)
{
	unsigned h = pfHash(name);
	for(int i = 0; i < s_slotCount; i++)
		if(s_slots[i].inUse && s_slots[i].hash == h && strcmp(s_slots[i].name, name) == 0)
			return &s_slots[i];
	if(s_slotCount >= PF_MAX_SLOTS)
		return NULL;
	PfSlot *s = &s_slots[s_slotCount++];
	memset(s, 0, sizeof(*s));
	s->name = name;
	s->hash = h;
	s->inUse = 1;
	s->cpuMin = 1e30f;
	s->gpuMin = 1e30f;
	return s;
}

static void
pfRoll(float *mn, float *avg, float *mx, float v, long n)
{
	if(v < *mn) *mn = v;
	if(v > *mx) *mx = v;
	if(n <= 1) *avg = v;
	else *avg += (v - *avg) / (float)n;
}

// Release every query and forget the device they belonged to. Called when
// d3d9device changes (reset/recreate) — stale queries would return invalid
// data or D3DERR_INVALIDCALL forever.
static void
pfReleaseQueries(void)
{
	for(int i = 0; i < s_slotCount; i++){
		PfSlot *s = &s_slots[i];
		if(s->qStart){   s->qStart->Release();   s->qStart = NULL; }
		if(s->qEnd){     s->qEnd->Release();     s->qEnd = NULL; }
		if(s->qDisjoint){ s->qDisjoint->Release(); s->qDisjoint = NULL; }
		s->gpuOpen = 0;
		s->gpuPending = 0;
		// gpuFailed is per-slot intent, not device state — keep it so a driver
		// that refuses queries is not re-probed 32 times per frame.
	}
	if(s_qFreq){ s_qFreq->Release(); s_qFreq = NULL; }
	s_gpuFreq = 0;
	s_dev = NULL;
}

// Make sure the cached device pointer is current. Returns false when there is
// no device at all (device lost / pre-init) — callers must then skip GPU work.
static bool
pfSyncDevice(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(dev == NULL)
		return false;
	if(s_dev == dev)
		return true;
	pfReleaseQueries();
	s_dev = dev;
	return true;
}

static bool
pfHaveDevice(void)
{
	return s_dev != NULL && d3d9device == s_dev;
}

// Lazily create the timestamp/disjoint trio for one slot.
static bool
pfCreateGpu(PfSlot *s)
{
	if(!pfHaveDevice() || s->gpuFailed)
		return false;
	if(s->qEnd != NULL)
		return true;

	IDirect3DDevice9 *dev = s_dev;
	HRESULT hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s->qStart);
	if(FAILED(hr)) goto fail;
	hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s->qEnd);
	if(FAILED(hr)) goto fail;
	hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &s->qDisjoint);
	if(FAILED(hr)) goto fail;

	if(s_qFreq == NULL){
		// Frequency is optional: without it GPU ms stay 0 (never a crash).
		if(FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &s_qFreq)))
			s_qFreq = NULL;
		else
			s_qFreq->Issue(D3DISSUE_END);	// result is read back in pfGpuResolve
	}
	return true;

fail:
	if(s->qStart){ s->qStart->Release(); s->qStart = NULL; }
	if(s->qEnd){ s->qEnd->Release(); s->qEnd = NULL; }
	if(s->qDisjoint){ s->qDisjoint->Release(); s->qDisjoint = NULL; }
	s->gpuFailed = 1;
	if(!s_gpuBroken){
		s_gpuBroken = 1;
		dbglog("[Profiler] GPU timers unavailable (CreateQuery hr=%08x) - CPU timings only", (unsigned)hr);
	}
	return false;
}

// Issue the GPU span start. Returns true when the span is armed.
static bool
pfGpuSpanStart(PfSlot *s)
{
	if(!g_pfGpuTimers || s_gpuBroken || !pfSyncDevice())
		return false;
	if(!pfCreateGpu(s))
		return false;
	HRESULT hr = s->qDisjoint->Issue(D3DISSUE_BEGIN);
	if(FAILED(hr)) return false;
	hr = s->qStart->Issue(D3DISSUE_END);
	if(FAILED(hr)){
		// Close the disjoint bracket so the next attempt's BEGIN is not nested
		// (TIMESTAMPDISJOINT does not support nesting).
		s->qDisjoint->Issue(D3DISSUE_END);
		return false;
	}
	s->gpuOpen = 1;
	return true;
}

static void
pfGpuSpanEnd(PfSlot *s)
{
	if(!s->gpuOpen)
		return;
	s->gpuOpen = 0;
	// Re-check the live flag: it may have been switched off between the scope's
	// ctor and dtor, in which case we must not leave a pending query behind.
	if(!g_pfGpuTimers || !pfHaveDevice()){
		// Close the disjoint bracket so a later re-enable's BEGIN is not nested
		// (TIMESTAMPDISJOINT cannot nest). Only safe while the queries' device
		// is still current; a device change resets everything in pfReleaseQueries.
		if(pfHaveDevice() && s->qDisjoint)
			s->qDisjoint->Issue(D3DISSUE_END);
		return;
	}
	if(FAILED(s->qEnd->Issue(D3DISSUE_END))){
		// End timestamp refused: close the disjoint bracket so the next
		// span's BEGIN is not nested (TIMESTAMPDISJOINT cannot nest).
		s->qDisjoint->Issue(D3DISSUE_END);
		return;
	}
	s->gpuPending = 1;
}

// Read back finished spans. Non-blocking: a query that is not ready yet stays
// pending and is retried on the next PfFrame (its result is dropped if the
// same scope is issued again meanwhile — a latency of one frame is fine for a
// debugging profiler, and it never blocks the render thread).
static void
pfGpuResolve(void)
{
	if(!g_pfGpuTimers || s_gpuBroken || !pfHaveDevice())
		return;

	// Device lost / not reset: queries would fail or return garbage.
	if(s_dev->TestCooperativeLevel() != D3D_OK)
		return;

	if(s_qFreq){
		if(s_gpuFreq == 0){
			long long f = 0;
			if(s_qFreq->GetData(&f, sizeof(f), 0) == S_OK && f > 0)
				s_gpuFreq = f;
			else
				s_qFreq->Issue(D3DISSUE_END);	// (re)issue until the first result lands
		}
	}

	for(int i = 0; i < s_slotCount; i++){
		PfSlot *s = &s_slots[i];
		if(!s->gpuPending || s->qEnd == NULL)
			continue;

		unsigned long long e = 0;
		if(s->qEnd->GetData(&e, sizeof(e), 0) != S_OK)
			continue;	// still in flight — retry next frame
		s->gpuPending = 0;

		unsigned long long b = 0;
		bool haveStart = (s->qStart->GetData(&b, sizeof(b), 0) == S_OK);
		BOOL disjoint = TRUE;	// conservative: treat "not ready" as unusable
		s->qDisjoint->GetData(&disjoint, sizeof(disjoint), 0);
		if(!haveStart || disjoint)
			continue;
		if(s_gpuFreq <= 0 || e < b)	// 1e-7-style guard: never divide by 0
			continue;

		float ms = (float)((double)(e - b) * 1000.0 / (double)s_gpuFreq);
		if(ms > 0.0f && ms < PF_MAX_FRAME){
			s->gpuMs = ms;
			if(s->frames > 0)
				pfRoll(&s->gpuMin, &s->gpuAvg, &s->gpuMax, ms, s->frames);
			else{
				s->gpuMin = s->gpuMax = ms;
				s->gpuAvg = ms;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// public API — instrument side
// ---------------------------------------------------------------------------
extern "C" void
PfInit(void)
{
	perfInit();
	s_gpuFreq = 0;
	s_gpuBroken = 0;
	s_frameT0 = perfNow();
	s_inited = 1;
	dbglog("[Profiler] init: slots=%d gpuTimers=%ld enable=%ld",
		PF_MAX_SLOTS, g_pfGpuTimers, g_pfEnable);
}

PfScope::PfScope(const char *n) : name(n), t0(0)
{
	if(n == NULL || !g_pfEnable){	// near-zero cost when disabled
		name = NULL;
		return;
	}
	if(!s_inited)
		PfInit();

	PfSlot *s = pfSlot(n);
	if(s == NULL){
		if(!s_overflow)
			dbglog("[Profiler] scope table full (%d) - '%s' not tracked", PF_MAX_SLOTS, n);
		s_overflow++;
		name = NULL;
		return;
	}
	if(++s->open == 1){
		s->t0ms = perfNow();
		pfGpuSpanStart(s);
	}
}

void
PfScope::Close(void)
{
	if(name == NULL)	// idempotent + disabled scopes
		return;
	const char *n = name;
	name = NULL;

	PfSlot *s = pfSlot(n);
	if(s == NULL || s->open <= 0)
		return;
	if(--s->open > 0)
		return;	// nested: the outermost close stops the clock

	s->cpuAcc += perfNow() - s->t0ms;
	s->closes++;
	pfGpuSpanEnd(s);
}

PfScope::~PfScope()
{
	Close();
}

extern "C" void
PfFrame(void)
{
	double now = perfNow();
	double dt = (s_frameT0 > 0.0) ? (now - s_frameT0) : 0.0;
	s_frameT0 = now;	// always: a disabled->enabled toggle must not report a stall

	if(!g_pfEnable)
		return;
	if(!s_inited)
		PfInit();

	pfGpuResolve();

	// ---- frame time -----------------------------------------------------
	if(dt > 0.0 && dt < (double)PF_MAX_FRAME){
		s_frameN++;
		s_frameMs = (float)dt;
		pfRoll(&s_frameMin, &s_frameAvg, &s_frameMax, (float)dt, s_frameN);
		if(s_histN < PF_HIST)
			s_hist[s_histN++] = (float)dt;
		else {	// slide the window
			memmove(s_hist, s_hist + 1, (PF_HIST - 1) * sizeof(float));
			s_hist[PF_HIST - 1] = (float)dt;
		}
	}

	// ---- per-scope rollup ------------------------------------------------
	for(int i = 0; i < s_slotCount; i++){
		PfSlot *s = &s_slots[i];
		if(!s->inUse)
			continue;
		if(s->closes > 0){
			s->frames++;
			s->cpuMs = (float)s->cpuAcc;
			if(s->cpuMs < PF_MAX_FRAME)
				pfRoll(&s->cpuMin, &s->cpuAvg, &s->cpuMax, s->cpuMs, s->frames);
		}else
			s->cpuMs = 0.0f;	// scope not hit this frame
		s->cpuAcc = 0.0;
		s->closes = 0;
	}

	// ---- ps/vs bind rollup: freeze this frame's counts, then zero ---------
	int psFrame = 0, vsFrame = 0;
	for(int i = 0; i < s_bindCount; i++){
		PfBindEnt *e = &s_binds[i];
		if(e->psFrame || e->vsFrame)
			e->frames++;
		psFrame += e->psFrame;
		vsFrame += e->vsFrame;
		e->psFrame = 0;
		e->vsFrame = 0;
	}
	s_psBinds = psFrame;
	s_vsBinds = vsFrame;

	// ---- summary log (~1/s) ----------------------------------------------
	if(now - s_lastLog >= PF_LOG_MS){
		s_lastLog = now;
		// per-frame deltas of the running draw counters (pfDraw accumulates)
		int dDraws = (int)(s_draws - s_lastDraws), dVerts = (int)(s_verts - s_lastVerts), dTris = (int)(s_tris - s_lastTris);
		s_lastDraws = s_draws; s_lastVerts = s_verts; s_lastTris = s_tris;
		dbglog("[Profiler] frame %.2f ms (min %.2f avg %.2f max %.2f) draws+%d verts+%d tris+%d psBinds %d vsBinds %d",
			s_frameMs, s_frameMin, s_frameAvg, s_frameMax, dDraws, dVerts, dTris, psFrame, vsFrame);
		for(int i = 0; i < s_slotCount; i++){
			PfSlot *s = &s_slots[i];
			if(!s->inUse || s->frames == 0)
				continue;
			dbglog("[Profiler]   %-22s cpu %7.3f ms (min %7.3f max %7.3f) gpu %7.3f ms",
				s->name, s->cpuMs, s->cpuMin, s->cpuMax, s->gpuMs);
		}
		if(s_overflow)
			dbglog("[Profiler]   %d scopes dropped (table full, %d slots)", s_overflow, PF_MAX_SLOTS);
	}
}

extern "C" void
PfDraw(int verts, int tris)
{
	if(!g_pfEnable)
		return;
	s_draws++;
	s_verts += verts > 0 ? verts : 0;
	s_tris  += tris  > 0 ? tris  : 0;
}

static PfBindEnt *
pfBindEnt(const char *tag, void *shader)
{
	if(tag == NULL || tag[0] == '\0')
		tag = "(unnamed)";
	for(int i = 0; i < s_bindCount; i++)
		if(strcmp(s_binds[i].tag, tag) == 0)
			return &s_binds[i];
	if(s_bindCount >= PF_MAX_BINDS)
		return NULL;
	PfBindEnt *e = &s_binds[s_bindCount++];
	memset(e, 0, sizeof(*e));
	e->tag = tag;
	return e;
}

extern "C" void
PfBindPS(void *shader, const char *tag)
{
	if(!g_pfEnable)
		return;
	PfBindEnt *e = pfBindEnt(tag, shader);
	if(e == NULL)
		return;
	e->lastPS = shader;
	e->psTotal++;
	e->psFrame++;
}

extern "C" void
PfBindVS(void *shader, const char *tag)
{
	if(!g_pfEnable)
		return;
	PfBindEnt *e = pfBindEnt(tag, shader);
	if(e == NULL)
		return;
	e->lastVS = shader;
	e->vsTotal++;
	e->vsFrame++;
}

extern "C" void
PfNote(const char *fmt, ...)
{
	if(!g_pfEnable || fmt == NULL)
		return;
	char *dst = s_notes[s_noteIdx];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(dst, PF_NOTE_LEN, fmt, ap);
	va_end(ap);
	dst[PF_NOTE_LEN - 1] = '\0';
	s_noteIdx = (s_noteIdx + 1) % PF_NOTE_SLOTS;
	if(s_noteFill < PF_NOTE_SLOTS)
		s_noteFill++;
}

// ---------------------------------------------------------------------------
// snapshot surface — consumed by the imgui "Profiler" tab (debugmenu_ui.cpp).
// Plain C arrays so no struct has to be shared across TUs; profiler.h carries
// only the instrument API and stays untouched.
// ---------------------------------------------------------------------------
extern "C" int
PfSnapshotNames(const char **names, float *cpuMs, float *cpuMin, float *cpuAvg,
                float *cpuMax, float *gpuMs, int max)
{
	int n = 0;
	for(int i = 0; i < s_slotCount && n < max; i++){
		PfSlot *s = &s_slots[i];
		if(!s->inUse || s->frames == 0)
			continue;
		if(names) names[n] = s->name;
		if(cpuMs) cpuMs[n] = s->cpuMs;
		if(cpuMin) cpuMin[n] = (s->cpuMin > 1e29f) ? 0.0f : s->cpuMin;
		if(cpuAvg) cpuAvg[n] = s->cpuAvg;
		if(cpuMax) cpuMax[n] = s->cpuMax;
		if(gpuMs) gpuMs[n] = s->gpuMs;
		n++;
	}
	return n;
}

extern "C" void
PfFrameInfo(float *ms, float *mn, float *avg, float *mx,
            long long *draws, long long *verts, long long *tris, int *psBinds, int *vsBinds)
{
	if(ms) *ms = s_frameMs;
	if(mn) *mn = s_frameMin;
	if(avg) *avg = s_frameAvg;
	if(mx) *mx = s_frameMax;
	if(draws) *draws = s_draws;
	if(verts) *verts = s_verts;
	if(tris) *tris = s_tris;
	if(psBinds) *psBinds = s_psBinds;
	if(vsBinds) *vsBinds = s_vsBinds;
}

// Frame-time history, oldest sample first (for ImGui::PlotLines).
extern "C" const float *
PfFrameHistory(int *count)
{
	if(count) *count = s_histN;
	return s_hist;
}

extern "C" int
PfBindSnapshot(const char **tags, void **lastPS, void **lastVS,
               int *ps, int *vs, int max)
{
	int n = 0;
	for(int i = 0; i < s_bindCount && n < max; i++){
		PfBindEnt *e = &s_binds[i];
		if(e->frames == 0)
			continue;
		if(tags)   tags[n] = e->tag;
		if(lastPS) lastPS[n] = e->lastPS;
		if(lastVS) lastVS[n] = e->lastVS;
		if(ps)     ps[n] = e->psFrame;
		if(vs)     vs[n] = e->vsFrame;
		n++;
	}
	return n;
}

// Newest note first, one per line. Returns the length written.
extern "C" int
PfNotesSnapshot(char *out, int outSize)
{
	if(out == NULL || outSize <= 0)
		return 0;
	out[0] = '\0';
	int len = 0;
	for(int k = 1; k <= s_noteFill && len < outSize - 1; k++){
		const char *src = s_notes[(s_noteIdx - k + PF_NOTE_SLOTS * 2) % PF_NOTE_SLOTS];
		int w = _snprintf(out + len, outSize - len, "%s\n", src);
		if(w < 0 || w >= outSize - len)
			break;	// _snprintf truncation leaves no NUL — terminate below
		len += w;
	}
	out[len] = '\0';	// len <= outSize-1 by the loop condition: always in range
	return len;
}

extern "C" void
PfResetStats(void)
{
	for(int i = 0; i < s_slotCount; i++){
		PfSlot *s = &s_slots[i];
		s->frames = 0;
		s->cpuMs = s->gpuMs = 0.0f;
		s->cpuMin = s->gpuMin = 1e30f;
		s->cpuAvg = s->cpuMax = 0.0f;
		s->gpuAvg = s->gpuMax = 0.0f;
		s->cpuAcc = 0.0;
		s->closes = 0;
	}
	s_frameMin = 1e30f;
	s_frameMax = 0.0f;
	s_frameAvg = 0.0f;
	s_frameN = 0;
	s_histN = 0;
	s_draws = s_verts = s_tris = 0;
	s_lastDraws = s_lastVerts = s_lastTris = 0;
	s_psBinds = s_vsBinds = 0;
	for(int i = 0; i < s_bindCount; i++){
		s_binds[i].psTotal = s_binds[i].vsTotal = 0;
		s_binds[i].psFrame = s_binds[i].vsFrame = 0;
		s_binds[i].frames = 0;
	}
	s_noteIdx = 0;
	s_noteFill = 0;
	s_overflow = 0;
	dbglog("[Profiler] stats reset");
}
