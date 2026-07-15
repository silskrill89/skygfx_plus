#pragma once

void waterPipe_init(void);
void waterPipe_shutdown(void);
void waterPipe_setRenderState(void);
void waterPipe_restoreRenderState(void);

extern bool g_waterParallaxActive;
