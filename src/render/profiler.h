#pragma once
// skygfx minimal pass/frame profiler (prefix Pf/PF).
// API shared by all lanes: include "profiler.h" then wrap any pass with PF_SCOPE("Name").
// profiler.cpp (renderlane) implements it; header is safe to include even if the .cpp
// registration is pending (calls become no-ops until PfInit()).
// Metrics: CPU wall-time per scope (QPC), GPU elapsed via D3D9 Query9 when available,
// draw/vert/tri counters, per-frame ordered chain timeline.

#include <windows.h>

extern volatile LONG g_pfEnable;      // 0 = disabled (default), 1 = on (testing)
extern volatile LONG g_pfGpuTimers;  // 1 = try D3D9 TIMESTAMP/DISJOINT queries

struct PfScope {
	const char *name;
	long long t0;
	PfScope(const char *n);
	~PfScope();
	void Close(); // idempotent, used from destructor
};

extern "C" {
	void PfInit();                     // called from profiler.cpp registration
	void PfFrame();                    // once per frame, after DrawFinalEffects
	void PfDraw(int verts, int tris);  // per render call (cheap: counters only)
	void PfBindPS(void *shader, const char *tag); // ps bind point (tag = shader/IDR name)
	void PfBindVS(void *shader, const char *tag);
	void PfNote(const char *fmt, ...); // brief string tag into the timeline (lossy ring)
}

// MSVC traditional preprocessor does not re-expand __LINE__ in a##b paste,
// so two PF_SCOPE() in one TU collide (C2374). Indirection through the
// helper pair expands it (fix-7 hit this in waterPipe.cpp).
#define PF_CAT2(a, b) PF_CAT1(a, b)
#define PF_CAT1(a, b) a##b
#define PF_SCOPE(name) PfScope PF_CAT2(pf_scope_, __LINE__)(name)
