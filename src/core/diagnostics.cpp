// diagnostics.cpp — L2 DIAGNOSTICS LAYER: the scope tag registry.
//
// m0272 layer split: this body was moved VERBATIM out of
// src/core/debug_handl.cpp (which is now deleted — everything it held has
// an explicit layer home):
//   * scope registry               -> HERE (L2, this file)
//   * log/ring/frame/dbglog/init   -> src/core/log.cpp (L0)
//   * crash handling               -> src/core/crashhandler.cpp (L1, m0271)
//
// The registry maps an address to a function name for the crash forensics
// layer: diag_lookupScope resolves the EIP of a crash against every scope
// registered by diag_registerScope, so a GUARD-FAULT receipt reads
// "DrawSMAA+0x1C" instead of a bare address.  Called from L1
// (crashhandler.cpp) at crash time, so this file must stay POD-only and
// allocation-free — no C++ objects with destructors, no locks (the writer
// runs at init only; readers are single-threaded at crash time).
//
// Declarations live in src/skygfx.h (SCOPE_TAG_MAX / ScopeTag / g_scopeTags
// / diag_registerScope / diag_lookupScope) — unchanged by this split; the
// g_scopeTagCount facade re-export is in src/core/diagnostics.h.

// ---- Includes ----
#include "skygfx.h"
#include "diagnostics.h"
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Scope tag registry
// ============================================================
ScopeTag g_scopeTags[SCOPE_TAG_MAX];
int g_scopeTagCount = 0;
int g_scopeTagLock = 0;

void diag_registerScope(const char *name, void *addr) {
	if(g_scopeTagCount >= SCOPE_TAG_MAX) return;
	g_scopeTags[g_scopeTagCount].name = name;
	g_scopeTags[g_scopeTagCount].addr = addr;
	g_scopeTagCount++;
}

void diag_lookupScope(void *eip, char *outName, int outSize, int *outOffset) {
	if(outSize <= 0) return;	// no room: outName[outSize-1] would be outName[-1]
	outName[0] = 0;
	*outOffset = 0;
	uintptr_t addr = (uintptr_t)eip;
	int best = -1;
	int bestDist = 0x7FFFFFFF;
	for(int i = 0; i < g_scopeTagCount; i++) {
		int dist = (int)(addr - (uintptr_t)g_scopeTags[i].addr);
		if(dist >= -0x400 && dist < 0x400 && abs(dist) < bestDist) {
			bestDist = abs(dist);
			best = i;
			*outOffset = dist;
		}
	}
	if(best >= 0) {
		strncpy(outName, g_scopeTags[best].name, outSize - 1);
		outName[outSize - 1] = 0;
	}
}
