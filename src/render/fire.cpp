// fire.cpp - SOFT PARTICLE FAMILY: fire / flames / sparks
// Owns: kFireVetoTex + IsFirePrim (from neopipe.cpp). Muzzle flash/camflash
// texture policy (sphere/sphere_CJ) lives in muzzleflash.cpp and is consulted
// here so the fire veto stays byte-behavior-identical. waterspark_16 is the
// spark family - deliberately part of flames.
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
bool IsMuzzleFlashTexture(const char *name);

namespace {
const char *const kFireVetoTex[] = {
	// sphere/sphere_CJ (muzzle flash/camflash) moved to muzzleflash.cpp;
	// IsFirePrim consults IsMuzzleFlashTexture first - identical veto set.
	"wjet6",		// fire flames
	"fireball6",		// explosion_* fireballs
	"bullethitsmoke",	// fire_bike/fire_car/flamethrower smoke prims,
				// coke_puff, puke, explosion_fuel_car
	"waterspark_16",	// overheat_car_electric sparks
};
}



// A single prim's fire-family test, used by the dst-byte rewrite. The prim
// signature gate keeps freed/recycled objects out before the texture read.
bool IsFirePrim(BYTE *prim)
{
	if(!IsFxRenderPrim(prim))
		return false;
	void **texHolder = (void**)(prim + 0xC);
	RwTexture *tex = texHolder ? (RwTexture*)*texHolder : NULL;
	const char *name = tex ? tex->name : NULL;
	if(!name || !name[0])
		return false;
	if(IsMuzzleFlashTexture(name))
		return true;
	for(unsigned t = 0; t < sizeof(kFireVetoTex)/sizeof(kFireVetoTex[0]); t++)
		if(strcmp(name, kFireVetoTex[t]) == 0)
			return true;
	return false;
}
