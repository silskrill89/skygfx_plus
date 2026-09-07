// depthhook.cpp — INTZ direct-binding depth hook for DXVK compatibility
//
// Hooks IDirect3DDevice9::SetDepthStencilSurface (vtable index 39) and
// IDirect3DDevice9::Reset (vtable index 16) to substitute an INTZ texture
// as the scene depth-stencil. INTZ is sampleable as a shader resource
// while simultaneously bound as the depth-stencil, eliminating all
// StretchRect depth copies that DXVK blocks.
//
// INI kill-switch: [SkyGFX] depthHookEnable=0 (default 1)
//
// If INTZ is unsupported by the GPU, the hook logs once and passes
// through — SSAO/velocity/fog consumers bail on NULL g_intzTex.

#include "depthhook.h"
#include "skygfx.h"
#include "../rw/gta.h" // for d3d9device, Scene

// ============================================================
// INTZ resources
// ============================================================
IDirect3DTexture9 *g_intzTex = NULL;
IDirect3DSurface9 *g_intzSurf = NULL;

// The game's real depth-stencil surface (cached on first substitute)
static IDirect3DSurface9 *g_gameDS = NULL;

// Device dimensions (from camera raster, not pRasterFrontBuffer)
static int g_dsWidth = 0;
static int g_dsHeight = 0;

// Hook state
static bool g_hookInstalled = false;
static bool g_intzSupported = false;   // true after successful CheckDeviceFormat
static bool g_intzChecked = false;     // true after CheckDeviceFormat attempted
static bool g_depthSuspended = false;  // true between Suspend/Restore
static DWORD g_savedZenable = TRUE;    // ZENABLE state saved in Suspend, restored in Restore

// Original vtable function pointers
typedef HRESULT (STDMETHODCALLTYPE *SetDSType)(IDirect3DDevice9*, IDirect3DSurface9*);
typedef HRESULT (STDMETHODCALLTYPE *ResetType)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static SetDSType orig_SetDS = NULL;
static ResetType orig_Reset = NULL;
static IDirect3DDevice9 *g_hookedDev = NULL;

// ============================================================
// INTZ creation
// ============================================================
static bool CreateINTZResources(IDirect3DDevice9 *dev, int w, int h) {
	// Release old first
	DepthHook_ReleaseResources();

	g_dsWidth = w;
	g_dsHeight = h;

	IDirect3D9 *d3d = NULL;
	if(FAILED(dev->GetDirect3D(&d3d)) || !d3d)
		return false;

	HRESULT hr = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
		D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
		(D3DFORMAT)MAKEFOURCC('I','N','T','Z'));

	g_intzChecked = true;
	g_intzSupported = SUCCEEDED(hr);
	d3d->Release();

	if(!g_intzSupported) {
		dbglog("DepthHook: INTZ not supported, hook disabled");
		return false;
	}

	hr = dev->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL,
		(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
		D3DPOOL_DEFAULT, &g_intzTex, NULL);
	if(FAILED(hr)) {
		dbglog("DepthHook: CreateTexture INTZ failed hr=0x%08X", hr);
		return false;
	}

	g_intzTex->GetSurfaceLevel(0, &g_intzSurf);
	dbglog("DepthHook: Create INTZ tex=%p surf=%p %dx%d pool=DEFAULT usage=DEPTHSTENCIL format=INTZ",
		g_intzTex, g_intzSurf, w, h);
	return true;
}

// DepthHook scope tags (registered in Install)

// ============================================================
// Vtable hook: SetDepthStencilSurface (index 39)
// ============================================================
static HRESULT STDMETHODCALLTYPE hook_SetDS(IDirect3DDevice9 *dev, IDirect3DSurface9 *pDS) {
	if(!pDS) {
		// NULL → pass through (unbind)
		return orig_SetDS(dev, NULL);
	}

	// Prevent double-substitution: if we already substituted, let it through
	if(pDS == g_intzSurf) {
		return orig_SetDS(dev, pDS);
	}

	// Lazy recreate INTZ after device Reset: if we need substitution but
	// INTZ was released, recreate it now. Guards against device-lost.
	if(!g_intzSurf && g_hookInstalled && g_intzSupported) {
		RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(camRas) {
			int cw = camRas->width;
			int ch = camRas->height;
			if(cw > 0 && ch > 0) {
				HRESULT hr = dev->CreateTexture(cw, ch, 1, D3DUSAGE_DEPTHSTENCIL,
					(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
					D3DPOOL_DEFAULT, &g_intzTex, NULL);
				if(SUCCEEDED(hr)) {
					g_intzTex->GetSurfaceLevel(0, &g_intzSurf);
					g_dsWidth = cw;
					g_dsHeight = ch;
					dbglog("DepthHook: lazy INTZ recreate %dx%d after Reset", cw, ch);
				} else {
					dbglog("DepthHook: lazy INTZ recreate failed hr=0x%08X (will retry)", hr);
				}
			}
		}
	}

	// If this is the cached game DS → substitute INTZ
	if(pDS == g_gameDS && g_intzSurf) {
		return orig_SetDS(dev, g_intzSurf);
	}

	// New surface: check if it matches camera raster dims
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(camRas) {
		int cw = camRas->width;
		int ch = camRas->height;

		// Get surface desc to check dimensions
		D3DSURFACE_DESC desc;
		if(SUCCEEDED(pDS->GetDesc(&desc))) {
			if((int)desc.Width == cw && (int)desc.Height == ch) {
				// This is the main scene DS. Cache and substitute.
				if(g_gameDS) g_gameDS->Release();
				g_gameDS = pDS;
				g_gameDS->AddRef();

				if(g_intzSurf) {
					return orig_SetDS(dev, g_intzSurf);
				}
			}
		}
	}

	// Small surface (reflection cam, env map, etc.) → pass through
	return orig_SetDS(dev, pDS);
}

// ============================================================
// Vtable hook: Reset (index 16)
// ============================================================
// Forward declaration of postfx's pool resource release
extern void ReleaseDefaultPoolResources(void);

static HRESULT STDMETHODCALLTYPE hook_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pParams) {
	// Release ALL D3DPOOL_DEFAULT resources BEFORE calling original Reset
	// (D3D9 rule: all DEFAULT-pool resources must be released before Reset)
	// This includes INTZ, IBL, normal buffer, pipe chain, SMAA, velocity, SSAO.
	ReleaseDefaultPoolResources();

	// Release game DS separately (not owned by ReleaseDefaultPoolResources)
	if(g_gameDS) { g_gameDS->Release(); g_gameDS = NULL; }
	g_dsWidth = 0;
	g_dsHeight = 0;

	// Clear suspend state so Suspend/Restore doesn't desync after Reset
	g_depthSuspended = false;

	HRESULT hr = orig_Reset(dev, pParams);

	// INTZ will be recreated lazily on next SetDS (first frame after reset)
	return hr;
}

// ============================================================
// Public API
// ============================================================
void DepthHook_Install(IDirect3DDevice9 *dev) {
	DBGLOG_ENTER("DepthHook_Install");
	if(g_hookInstalled) return;
	if(!dev) return;

	// INI kill-switch
	if(!config || !config->depthHookEnable) {
		dbglog("DepthHook: disabled by config (depthHookEnable=%d)", config ? config->depthHookEnable : -1);
		return;
	}

	dbglog("DepthHook: installing vtable hooks on device %p", dev);

	// Find the vtable
	// IDirect3DDevice9 vtable layout:
	//   Index 16: Reset
	//   Index 39: SetDepthStencilSurface
	void **vtable = *(void***)dev;

	// Save originals
	orig_Reset = (ResetType)vtable[16];
	orig_SetDS = (SetDSType)vtable[39];

	// Write hooks (D3D9 vtable is writable)
	DWORD oldProtect = 0;
	VirtualProtect(&vtable[16], sizeof(void*), PAGE_READWRITE, &oldProtect);
	vtable[16] = (void*)hook_Reset;
	VirtualProtect(&vtable[16], sizeof(void*), oldProtect, &oldProtect);

	VirtualProtect(&vtable[39], sizeof(void*), PAGE_READWRITE, &oldProtect);
	vtable[39] = (void*)hook_SetDS;
	VirtualProtect(&vtable[39], sizeof(void*), oldProtect, &oldProtect);

	g_hookedDev = dev;
	g_hookInstalled = true;

	// Register scope tags for crash backtrace
	diag_registerScope("DepthHook_Install", (void*)DepthHook_Install);
	diag_registerScope("DepthHook_Suspend", (void*)DepthHook_Suspend);
	diag_registerScope("DepthHook_Restore", (void*)DepthHook_Restore);
	diag_registerScope("DepthHook_ReleaseResources", (void*)DepthHook_ReleaseResources);

	// Create INTZ resources now
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(camRas) {
		CreateINTZResources(dev, camRas->width, camRas->height);
	} else {
		dbglog("DepthHook: no camera raster yet, INTZ will be created on first frame");
	}

	dbglog("DepthHook: installed successfully (INTZ=%s)", g_intzSupported ? "yes" : "no");
}

void DepthHook_Suspend(void) {
	if(!g_hookInstalled || !g_intzSurf) return;
	if(g_depthSuspended) {
		dbglog("DepthHook: WARNING unbalanced Suspend (already suspended)");
		return;
	}

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	// Save current ZENABLE state so we can restore it exactly
	dev->GetRenderState(D3DRS_ZENABLE, &g_savedZenable);

	// Unbind depth-stencil and disable Z so INTZ can be sampled
	// We pass NULL DS directly (not through our hook, which would substitute)
	orig_SetDS(dev, NULL);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);

	g_depthSuspended = true;
}

void DepthHook_Restore(void) {
	if(!g_hookInstalled || !g_intzSurf) return;
	if(!g_depthSuspended) {
		dbglog("DepthHook: WARNING unbalanced Restore (not suspended)");
		return;
	}

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	// Re-enable Z to saved value and re-bind INTZ via original SetDS (goes through our hook)
	dev->SetRenderState(D3DRS_ZENABLE, g_savedZenable);

	// Re-bind the game DS — our hook will substitute INTZ
	if(g_gameDS) {
		orig_SetDS(dev, g_gameDS); // goes through hook_SetDS → substitutes INTZ
	} else if(g_intzSurf) {
		orig_SetDS(dev, g_intzSurf);
	}

	g_depthSuspended = false;
}

void DepthHook_ReleaseResources(void) {
	DBGLOG_ENTER("DepthHook_ReleaseResources");
	if(g_intzSurf) { dbglog("DepthHook: Release surf=%p", g_intzSurf); g_intzSurf->Release(); g_intzSurf = NULL; }
	if(g_intzTex) { dbglog("DepthHook: Release tex=%p", g_intzTex); g_intzTex->Release(); g_intzTex = NULL; }
}

// Also called from ReleaseDefaultPoolResources in postfx.cpp context
// (we expose the same cleanup path)