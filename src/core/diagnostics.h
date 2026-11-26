// diagnostics.h — L2 DIAGNOSTICS LAYER of the diagnostics stack
// (m0272 layer split).
//
// L2 owns the SCOPE TAG REGISTRY: the address -> function-name table the
// crash forensics layer resolves an EIP against, so a GUARD-FAULT or a
// FORENSICS backtrace names a function instead of a bare address.
//
// L2 MAY DEPEND ON L0 only.
//
// Ownership note: the registry TYPES and the registry API declarations are
// not file-local, so they stayed where they already were —
//   SCOPE_TAG_MAX, struct ScopeTag, g_scopeTags, g_scopeTagCount,
//   g_scopeTagLock, diag_registerScope(), diag_lookupScope()
// are declared in src/skygfx.h:185-195 (untouched by this split, and every
// consumer reaches them through it).  The DEFINITIONS are what moved here,
// into src/core/diagnostics.cpp.  The g_scopeTagCount extern that
// debug_handl.h used to carry is re-exported below for facade compatibility.
//
// Consumers should keep including the facade (debug_handl.h); this header
// exists so the layer boundary is explicit and so future registry work has
// an obvious home.

#pragma once

#include <windows.h>

#include "log.h"   // L0 (L2's only permitted dependency)

// Scope tag registry count (definition: diagnostics.cpp; also declared in
// skygfx.h — duplicate extern declarations are identical, so ODR-safe).
// NOT gated: the definition in diagnostics.cpp is not gated either (it never
// was — same as debug_handl.cpp before the split).
extern int g_scopeTagCount;

// Registry API (decls: skygfx.h; definitions: diagnostics.cpp):
//   void diag_registerScope(const char *name, void *addr);
//   void diag_lookupScope(void *addr, char *outName, int outSize, int *outOffset);
// diag_lookupScope is called from the L1 forensics path (crashhandler.cpp).
