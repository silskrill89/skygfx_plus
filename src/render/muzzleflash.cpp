// muzzleflash.cpp - SOFT PARTICLE FAMILY: muzzle flash / camflash
// Owns: kMuzzleFlashTex + IsMuzzleFlashTexture (byte-identical from fire.cpp).
// The sphere/sphere_CJ prims are the gunflash/camflash family. They remain
// part of the fire veto: fire.cpp's IsFirePrim consults this predicate so the
// dst-byte rewrite still skips them (flashes stay additive).
// No raw game addresses live here - this is a texture-name policy only.

#include "skygfx.h"
#include <string.h>

namespace {
const char *const kMuzzleFlashTex[] = {
	"sphere",		// gunflash/camflash/Flame/flame/molotov_flame/
				// petrolcan/shootlight/tank_fire/extinguisher/
				// prt_spark/prt_spark_2
	"sphere_CJ",		// fire/fire_bike/fire_car/fire_large/fire_med/
				// flamethrower/jetpack/jetthrust/nitro
};
}

bool IsMuzzleFlashTexture(const char *name)
{
	if(!name || !name[0])
		return false;
	for(unsigned t = 0; t < sizeof(kMuzzleFlashTex)/sizeof(kMuzzleFlashTex[0]); t++)
		if(strcmp(name, kMuzzleFlashTex[t]) == 0)
			return true;
	return false;
}
