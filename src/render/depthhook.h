// depthhook.h — INTZ direct-binding depth hook
// Vtable hooks SetDepthStencilSurface on the D3D9 device to substitute
// an INTZ texture as the scene depth-stencil, eliminating all StretchRect
// depth copies that DXVK blocks.
//
// Design:
//   Every time the game sets a new DS via SetDepthStencilSurface, we
//   substitute our INTZ surface. INTZ is sampleable as a shader resource
//   while bound as DS — no copies needed. Consumers suspend the hook
//   before sampling the depth texture (INTZ can't be sampled while being
//   simultaneously written by ongoing rendering).
//
// Thread safety: all hook functions called from D3D9 render thread only.

#pragma once

#include <d3d9.h>

// INTZ surface and texture (externed for postfx.cpp consumers)
extern IDirect3DTexture9 *g_intzTex;
extern IDirect3DSurface9 *g_intzSurf;

// Install the vtable hook on d3d9device. Safe to call multiple times.
void DepthHook_Install(IDirect3DDevice9 *device);

// Suspend: unbind current DS + disable Z so depth texture can be sampled.
// Restore: re-bind the original cached DS (goes through hook → INTZ).
// These MUST be balanced and MUST NOT nest.
void DepthHook_Suspend(void);
void DepthHook_Restore(void);

// Release INTZ resources (call before Reset, on device lost, DllMain detach)
void DepthHook_ReleaseResources(void);