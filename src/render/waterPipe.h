#pragma once

void waterPipe_init(void);
void waterPipe_shutdown(void);
void waterPipe_setRenderState(void);
void waterPipe_restoreRenderState(void);

// Style-selected water pixel shader (waterStyle: 0=Xbox, 1=IV, 2=V) with
// fallback to any available entry — shared by the draw gate in main.cpp and
// the bind in waterPipe_setRenderState.
void *waterPipe_pixelShader(void);

extern bool g_waterParallaxActive;

// INI SkyGfx/waterFresnelSplit (default 1) — defined in waterPipe.cpp, read
// by main.cpp's water config block. 1 = fresnel split composite, 0 = legacy.
extern int g_waterFresnelSplit;
