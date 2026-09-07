#pragma once

// Deferred SMAA raster init — called from RenderScene_before (main.cpp)
// to force-create D3D9 surfaces for CAMERATEXTURE rasters outside the
// main camera's BeginUpdate (RW 3.6 driver crashes mid-frame).
void SMAATryInitRasters(void);

// Debug dump helper — called from RenderScene_after (main.cpp)
// to capture the camera raster after scene render (INI-gated: postfxDumpDebug=1)
void PostFX_DumpSceneCamera(void);