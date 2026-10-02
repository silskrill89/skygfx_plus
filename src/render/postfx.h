#pragma once

// Deferred SMAA raster init — called from RenderScene_before (main.cpp)
// to force-create D3D9 surfaces for CAMERATEXTURE rasters outside the
// main camera's BeginUpdate (RW 3.6 driver crashes mid-frame).
void SMAATryInitRasters(void);

// Debug dump helper — called from RenderScene_after (main.cpp)
// to capture the camera raster after scene render (INI-gated: postfxDumpDebug=1)
void PostFX_DumpSceneCamera(void);

// Release all D3DPOOL_DEFAULT resources (call on device lost/reset)
void ReleaseDefaultPoolResources(void);

// Release luminance target resources (g_lumaMeas*, g_lumaAdapt*)
void ReleaseLuminanceTargets(void);

// Central cleanup on device reset (called from depthhook::hook_Reset)
void OnDeviceReset(void);

// Copy the live scene depth (INTZ, currently bound as device DS) into dstTex
// (R32F render target, dstW x dstH) as raw window-space z. Used by the water
// pipe at water-draw time for depth-based shore blending / translucency.
// Returns false when the copy cannot run — caller falls back to depth-less
// shading. Defined in postfx.cpp (needs its Im2D override + raw-state idiom).
bool PostFX_CopyDepthToTexture(struct IDirect3DTexture9 *dstTex, int dstW, int dstH);

// Trusted screen-size cache (defined in postfx.cpp). Returns false until first capture.
bool GetScreenSize(int *w, int *h);
