// fire.cpp - SOFT PARTICLE FAMILY: fire / flames / sparks / muzzle flash
// Owns: kFireVetoTex + IsFirePrim (byte-identical from neopipe.cpp).
// The sphere/sphere_CJ entries are the muzzle-flash/camflash prims;
// waterspark_16 is the spark family - both are deliberately part of flames.
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

namespace {
const char *const kFireVetoTex[] = {
	"sphere",		// gunflash/camflash/Flame/flame/molotov_flame/
				// petrolcan/shootlight/tank_fire/extinguisher/
				// prt_spark/prt_spark_2
	"sphere_CJ",		// fire/fire_bike/fire_car/fire_large/fire_med/
				// flamethrower/jetpack/jetthrust/nitro
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
	for(unsigned t = 0; t < sizeof(kFireVetoTex)/sizeof(kFireVetoTex[0]); t++)
		if(strcmp(name, kFireVetoTex[t]) == 0)
			return true;
	return false;
}
