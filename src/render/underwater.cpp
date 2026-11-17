// underwater.cpp — water-droplet / splash-foam hook installs extracted from
// main.cpp (byte-identical bodies; docs/plans/2026-11-08-master-plan.md §4).
// Owns:
//   * underwater_installHooks(void) — if(iCanHasNeoDrops) hookWaterDrops()
//     (neoWaterDrops screen-drops) + hookSplashFoamFix() (W2 splash-foam /
//     underwater draw gate). The hook BODIES stay in neopipe.cpp — only the
//     install calls moved here.
// Called once from main.cpp DllMain at the original install position
// (inside InjectDelayedPatches, which DllMain calls).
// NOTE: the W7-D underwater volume-fog pass is a separate, already-modular
// system (src/render/postfx_underwater.cpp) — untouched by this extraction.
// Local-extern idiom (no shared header), matching shadows.cpp / rw_fixes.cpp.
// (No local externs needed: iCanHasNeoDrops comes from skygfx.h, the two
// hook declarations from neopipe.h.)
#include "skygfx.h"
#include "neopipe.h"

// ============================================================================
// SUBSYSTEM: WATER-DROPLET / SPLASH-FOAM INSTALLS   (extraction: render/underwater.cpp)
// ============================================================================

void
underwater_installHooks(void)
{
	if(iCanHasNeoDrops) hookWaterDrops();
	// W2 splash-foam blowout fix — water-splash FX systems: per-draw dst
	// blend ONE->INVSRCALPHA + underwater draw gate (see neopipe.cpp).
	// Independent of the neoWaterDrops lever on purpose: the blowout
	// happens with the screen-drop feature off too.
	hookSplashFoamFix();
}
