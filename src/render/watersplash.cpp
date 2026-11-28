// watersplash.cpp - SOFT PARTICLE FAMILY: water splash policy - extraction 2026-11-28
// Owns: IsWaterSplashSystem (byte-identical from neopipe.cpp).
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

bool
IsWaterSplashSystem(void *sys)
{
	static const char *const triggerTex[] = {
		"carsplash_02",	// water_splash/_big/_sml/water_swim droplets
		"boatwake1",	// prt_wake circles
		"coronaringa",	// water_ripples circles
		"splash_up1",	// prt_splash
		"boatsplash",	// prt_boatsplash, water_speed
		"wjet2",	// water_hydrant/_fountain/_fnt_tme
		"wgush1",	// water_hydrant/_fountain/_fnt_tme
		"gensplash",	// prt_watersplash, water_speed, boat_prop
	};
	if(!sys)
		return false;
	signed char primCount = *(signed char*)((char*)sys + 0x1b);
	void **primList = *(void***)((char*)sys + 0x1c);
	if(primCount <= 0 || !primList)
		return false;
	for(int i = 0; i < primCount; i++){
		if(!IsFxRenderPrim(primList[i]))
			continue;
		void **texHolder = (void**)(((char*)primList[i]) + 0xC);
		if(!texHolder)
			continue;
		RwTexture *tex = (RwTexture*)*texHolder;
		if(!tex)
			continue;
		const char *name = tex->name;
		if(!name || !name[0])
			continue;
		for(unsigned t = 0; t < sizeof(triggerTex)/sizeof(triggerTex[0]); t++)
			if(strcmp(name, triggerTex[t]) == 0)
				return true;
	}
	return false;
}

