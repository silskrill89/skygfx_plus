#pragma once
#include "skygfx.h"

// texdefs — unified texture-definition layer (texdb.txt metadata +
// <diffuse>_n normal-map pairing + DK22Pac external plugin lane).
// Replaces src/extras/texdb.cpp, src/render/normalmap.{h,cpp} and
// src/rw/normalmap.cpp. ONE find-callback patch feeds both the TexInfo and
// NmPair layers; BOTH RW texture plugin slots are registered from
// TexDefsPluginAttach().

// --- lifecycle ---
int  TexDefsPluginAttach(void);       // myPluginAttach chain (0x53D903). SINGLE RW plugin registry (EDED + PDS atomic slots, texdb + NmPair texture slots). Before any texture/atomic exists. Idempotent. Always returns 1.
void TexDefsInit(void);               // DllMain: 2 txd-name hooks + the ONE find-CB patch. Idempotent.
void TexDefsLoadDatabase(void);       // InitialiseGame_hook: Mobile_details.txd + readTxt + roadsign entry + faketexinfo defaults.
void TexDefsShutdown(void);           // DLL_PROCESS_DETACH: free strdup'd names + delete TexInfo map entries. No RW/D3D9 calls.

// --- external DK22Pac offline plugin lane (enableNormalMaps), deferred until RW engine is up ---
void TexDefsExternalInit(void);        // DllMain one-shot attempt (old normalmap_init)
void TexDefsTryAttachExternal(void);   // per-frame poll from RenderScene_hook (old normalmap_tryAttach)
void TexDefsExternalShutdown(void);    // old normalmap_shutdown

// --- read side (pipe callbacks) ---
TexInfo *TexDefsGetInfo(RwTexture *tex);                      // NEVER NULL — falls back to the internal faketexinfo
TexInfo *TexDefsFindInfo(char *name);                         // map lookup, may return NULL
void     TexDefsAssignInfo(RwTexture *tex, TexInfo *info);    // writes the plugin slot (replaces main.cpp's direct texdbOffset write)
IDirect3DTexture9 *TexDefsGetNormalMap(RwTexture *diffuse, bool *outHasAlpha);
bool     TexDefsVehicleAllowed(const void *atomic);

// --- D3D texture extraction (the ONE 0xB4E9E0 extension-offset formula) ---
// Failure stage codes for callers that keep reason counters (texalpha.cpp).
enum TexDefsD3DWhy {
	TEXDEFS_D3D_WHY_OK = 0,         // success
	TEXDEFS_D3D_WHY_NO_TEXTURE,     // tex == NULL
	TEXDEFS_D3D_WHY_NO_RASTER,      // tex->raster == NULL
	TEXDEFS_D3D_WHY_BAD_EXT_OFFSET, // 0xB4E9E0 word ==0 / >0x100000 / unaligned
	TEXDEFS_D3D_WHY_NO_D3D_TEX,     // ext+8 flag clear + implausible ptr, or ext+0 NULL
	TEXDEFS_D3D_WHY_BAD_EXT_PTR     // ext+8 flag set but ext+0 implausible
};

// THE accessor for the game RwD3D9SetTexture extension-offset extraction
// (0x7FDCD0: ext = raster + *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*,
// ext+8 = valid byte flag). Borrowed pointer: NO AddRef, caller must NOT
// Release it. Accepts when EITHER the ext+8 flag is set OR ext+0 is a
// plausible pointer (the flag can be stale). outWhy (optional) receives a
// TexDefsD3DWhy; outExtOff (optional) receives the raw offset word ONLY when
// it is actually read (callers pre-set 0xFFFFFFFF as "unread"). The optional
// trailing out-params exist so texalpha's reason counters fire on exactly the
// conditions they always did; plain form is TexDefsGetD3DTexture(tex).
IDirect3DTexture9 *TexDefsGetD3DTexture(RwTexture *tex, int *outWhy = NULL, uint32 *outExtOff = NULL);

// --- per-atomic defined vertex shader (EDED plugin slot; moved from extendedplg.cpp) ---
uint8 TexDefsGetDefinedShader(RpAtomic *atomic);            // was GetDefinedShader. Degrades to DEFAULT when the slot is not registered.
void  TexDefsSetDefinedShader(RpAtomic *atomic, uint8 s);   // was SetDefinedShader. No-op when the slot is not registered.
