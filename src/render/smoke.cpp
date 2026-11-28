// smoke.cpp - SOFT PARTICLE FAMILY: white/grey smoke policy - extraction 2026-11-28
// Owns: kSmokeTriggerTex + IsWhiteSmokeSystem (byte-identical from neopipe.cpp).
// Fire/spark/muzzle-flash veto lives in fire.cpp (sparks are part of the flames family).
// 
#include "skygfx.h"
#include "neopipe.h"
#include <DirectXMath.h>
#include "MemoryMgr.h"
#include "d3d9.h"
#include "d3d9types.h"
#include <time.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <injector\injector.hpp>
#include <injector\hooking.hpp>
#include <injector\calling.hpp>
#include <injector\assembly.hpp>
#include <injector\utility.hpp>

bool IsFxRenderPrim(void *prim);

// F4 (test2-0438): the saturating non-water white/grey smoke family.
// Triggers = the white-smoke textures. Fire prims are vetoed PER PRIM in the
// dst-byte rewrite (IsFirePrim), not per system: a gunshot system carries
// BOTH smokeII_3 (gunsmoke) and sphere (muzzle flash) prims — vetoing the
// whole system whenever the flash was alive meant the smoke prims lost their
// blend pin half the time and drew REPLACE = opaque grey squares (the
// 2026-11-28 "blends properly half the time" report). The pin forces normal
// alpha only as the ENTRY state; authored (5,2) fire prims still transition
// to their own additive pair, so per-prim veto keeps sparks/flames additive.
// Systems already authored SRCALPHA/INVSRCALPHA (the prt_smoke_huge
// exhaust/skid family) match too: their byte rewrite is a no-op, but the
// blend pin below repairs their split-cache draw (the F4 "hard additive
// streaks" are those prims rendering REPLACE after the post-water window).
namespace {
const char *const kSmokeTriggerTex[] = {
	"smoke",		// spraycan mist
	"smoke4",		// smoke_flare
	"smoke5",		// WS_factorysmoke - the map smoke sheet
	"collisionsmoke",	// prt_smoke_huge/prt_collisionsmoke/prt_smokeII_3_expand,
				// cigarette_smoke, riot_smoke
	"smokeII_3",		// gunsmoke, heli_dust, prt_wheeldirt, exhale
};
}

bool
IsWhiteSmokeSystem(void *sys)
{
	if(!sys)
		return false;
	signed char primCount = *(signed char*)((char*)sys + 0x1b);
	void **primList = *(void***)((char*)sys + 0x1c);
	if(primCount <= 0 || !primList)
		return false;
	bool trigger = false;
	for(int i = 0; i < primCount; i++){
		if(!IsFxRenderPrim(primList[i]))
			continue;
		void **texHolder = (void**)(((char*)primList[i]) + 0xC);
		RwTexture *tex = texHolder ? (RwTexture*)*texHolder : NULL;
		if(!tex)
			continue;
		const char *name = tex->name;
		if(!name || !name[0])
			continue;
		// Per-prim fire veto lives in IsFirePrim (the dst-byte rewrite skips
		// those prims). A fire prim here does NOT veto the whole system: the
		// gunsmoke/muzzle-flash pair must both be in the pin window, and the
		// authored (5,2) fire prim transitions back to additive on its own.
		for(unsigned t = 0; t < sizeof(kSmokeTriggerTex)/sizeof(kSmokeTriggerTex[0]); t++)
			if(strcmp(name, kSmokeTriggerTex[t]) == 0)
				trigger = true;
	}
	return trigger;
}
