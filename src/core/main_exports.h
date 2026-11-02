// main_exports.h — Symbols from main.cpp that out-of-TU hook sites need.
// (2026-10-27 stage0-3 purge: the old hooks.cpp bridge remainder is gone —
// hooks.cpp no longer exists, and every remaining consumer of the old bridge
// symbols reaches them via skygfx.h or its own externs. Only struct FX
// survives here: its members are defined in main.cpp and the FxQuality hook
// sites need the type.)

#ifndef MAIN_EXPORTS_H
#define MAIN_EXPORTS_H

#include "skygfx.h"

// ============================================================
// FX quality (struct with methods, used by InjectDelayedPatches)
// ============================================================
struct FX {
	char data[0x54];
	int fxQuality;
	int GetFxQuality_ped(void);
	int GetFxQuality_stencil(void);
};

#endif // MAIN_EXPORTS_H
