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

// First-frame stability: defer INTZ substitution until cam dims stabilize
static int s_lastDsW = 0;
static int s_lastDsH = 0;
static int s_dsStableCount = 0;

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

	// Defer texture creation until plausible dims are known (RsGlobal
	// populated / camera raster valid). g_intzSupported stays true so the
	// lazy recreate in hook_SetDS can create it on a later frame. An INTZ
	// created at an implausible size would fail SetDS on every normal frame
	// (D3D9 rejects a DS smaller than the RT).
	if(w < 64 || h < 64) {
		dbglog("DepthHook: INTZ supported, deferring creation (dims %dx%d not valid yet)", w, h);
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
// Trusted screen dims (same doctrine as forwardplus.cpp's
// FpGetTrustedScreenSize)
// ============================================================
// RsGlobal is a fixed exe address (0xC17040) that is zero until the game's
// Rs init runs, and during the geometry-phase camera-raster swap the LIVE
// raster reports the transient 2048x1024 IBL/classify size. Matching the
// depth-stencil against the live raster during a swap frame caches the swap
// raster's DS as g_gameDS and substitutes an INTZ of the wrong size — the
// substitution SetDS then fails (D3D9 rejects a DS smaller than the render
// target) and Restore's rebind of the poisoned g_gameDS fails the same way,
// leaving DS=NULL with ZENABLE=TRUE: every draw fails and the world goes
// blank. Bounds [64, 8192] reject that.
static bool DpGetTrustedScreenSize(int *w, int *h)
{
	if(RsGlobal){
		DWORD gw = RsGlobal->MaximumWidth;
		DWORD gh = RsGlobal->MaximumHeight;
		if(gw >= 64 && gw <= 8192 && gh >= 64 && gh <= 8192){
			*w = (int)gw;
			*h = (int)gh;
			return true;
		}
	}
	return false;
}

// ============================================================
// INTZ sampler scrub
// ============================================================
// NVIDIA's INTZ constraint (and D3D9's texture/depth-stencil feedback rule in
// general): a surface must never be bound as a texture on a sampler AND as the
// depth-stencil at the same time — the next draw is a feedback loop and is
// rejected/undefined (draw dropped or coloured garbage).
//
// Six postfx passes bind g_ssaoDepthTex (which IS g_intzTex) on a sampler and
// hand control back to DepthHook_Restore without unbinding it:
//   postfx.cpp CopyDepthToPrev (s0), DrawNormalBufferToTexture (s0),
//   DrawPipeChain (s2), DrawSMAA_EdgeDetect (s2), DrawVelocityBuffer (s0),
//   DrawMotionBlur (s2)
// (DrawSSAO / DrawSSAO_Overhaul / DrawHeightFog do unbind first.) If INTZ then
// becomes the device DS — either from hook_SetDS's substitution on the next
// screen SetDepthStencilSurface or from Restore's INTZ fallback paths — any
// stage still holding it turns the following draws into a feedback loop.
// Scrub every stage that still references g_intzTex before binding it as DS.
static void DpUnbindIntzFromSamplers(IDirect3DDevice9 *dev)
{
	if(!dev || !g_intzTex) return;
	for(DWORD s = 0; s < 8; s++) {
		IDirect3DBaseTexture9 *tex = NULL;
		if(FAILED(dev->GetTexture(s, &tex)) || !tex)
			continue;
		if(tex == (IDirect3DBaseTexture9*)g_intzTex) {
			dev->SetTexture(s, NULL);
			if(dbglog_throttle("intz_scrub"))
				dbglog("DepthHook: INTZ still bound on sampler %u while it becomes the DS — unbound", (unsigned)s);
		}
		tex->Release();
	}
}

// ============================================================
// Vtable hook: SetDepthStencilSurface (index 39)
// ============================================================
static HRESULT STDMETHODCALLTYPE hook_SetDS(IDirect3DDevice9 *dev, IDirect3DSurface9 *pDS) {
	if(!pDS) {
		// NULL → pass through (unbind)
		return orig_SetDS(dev, NULL);
	}

	// Never let INTZ become the depth-stencil while a suspend window is open:
	// postfx is sampling INTZ as a shader resource right now, so binding it as
	// the DS as well would be a texture/depth-stencil feedback loop (rejected /
	// undefined per D3D9's INTZ constraints). Suspend already left the DS NULL
	// and disabled Z for the whole window, so dropping the bind is invisible to
	// the pass; Restore re-binds the real DS when the window closes. This must
	// run BEFORE the pDS==g_intzSurf passthrough below, otherwise a mid-window
	// SetDS(INTZ) would slip straight through.
	if(g_depthSuspended) {
		if(pDS == g_intzSurf)
			return orig_SetDS(dev, NULL);
		// A mid-window SetDS with the caller's own surface (the game DS) is a
		// legitimate depth-consuming pass — pass it through raw, no substitution.
		return orig_SetDS(dev, pDS);
	}

	// Prevent double-substitution: if we already substituted, let it through
	if(pDS == g_intzSurf) {
		return orig_SetDS(dev, pDS);
	}

	// First-frame stability: defer INTZ substitution until the same dims are
	// seen on 2+ consecutive SetDS calls.
	//
	// The dims MUST come from the TRUSTED screen size (RsGlobal), not the live
	// Scene.camera raster: outdoors the reflection/corona sub-renders repoint
	// Scene.camera at a non-screen raster between ordinary SetDS calls
	// (skygfx_dbg.log: "[FxAlpha] Coronas.Reflections camRas=... 2048x1024 !=
	// screen 1920x1080 — non-screen raster live"). Reading the live raster here
	// reset s_dsStableCount to 1 on every such call, so dimsStable dropped for
	// the following screen frame and the INTZ substitution was skipped. With
	// no substitution the geometry depth writes go to the plain game DS while
	// every INTZ consumer (velocity buffer, SSAO, SMAA motion/depth edge pass,
	// water depth copy) keeps reading the FROZEN INTZ contents — stale depth
	// reprojected through the velocity buffer covers the outdoor frame in
	// motion/edge garbage while the indoor frame (no reflection swaps) stays
	// clean. RsGlobal falls back to the live raster only before Rs init, which
	// is the startup-only window this guard exists for.
	{
		int cw = 0, ch = 0;
		if(!DpGetTrustedScreenSize(&cw, &ch)) {
			RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
			if(camRas) { cw = camRas->width; ch = camRas->height; }
		}
		if(cw > 0 && ch > 0) {
			if(cw == s_lastDsW && ch == s_lastDsH) {
				if(s_dsStableCount < 999) s_dsStableCount++;
			} else {
				s_lastDsW = cw;
				s_lastDsH = ch;
				s_dsStableCount = 1;
			}
		}
	}
	bool dimsStable = (s_dsStableCount >= 2);

	// Lazy recreate INTZ after device Reset: if we need substitution but
	// INTZ was released, recreate it now. Guards against device-lost.
	// Prefer the trusted screen dims (RsGlobal) — the live camera raster can
	// be the transient swap raster during the geometry phase, and an INTZ
	// created at that size fails SetDS on every normal-resolution frame.
	if(!g_intzSurf && g_hookInstalled && g_intzSupported) {
		int lw = 0, lh = 0;
		if(!DpGetTrustedScreenSize(&lw, &lh)) {
			RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
			if(camRas) { lw = camRas->width; lh = camRas->height; }
		}
		if(lw >= 64 && lh >= 64) {
			// Release a stale texture first: if a previous create made the
			// texture but GetSurfaceLevel failed, g_intzTex is non-NULL while
			// g_intzSurf is NULL and CreateTexture into it would fail/leak.
			if(g_intzTex) { g_intzTex->Release(); g_intzTex = NULL; }
			HRESULT hr = dev->CreateTexture(lw, lh, 1, D3DUSAGE_DEPTHSTENCIL,
				(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
				D3DPOOL_DEFAULT, &g_intzTex, NULL);
			if(SUCCEEDED(hr)) {
				g_intzTex->GetSurfaceLevel(0, &g_intzSurf);
				g_dsWidth = lw;
				g_dsHeight = lh;
				dbglog("DepthHook: lazy INTZ recreate %dx%d after Reset", lw, lh);
			} else {
				dbglog("DepthHook: lazy INTZ recreate failed hr=0x%08X (will retry)", hr);
			}
		}
	}

	// Identify the main scene DS by TRUSTED screen dims (RsGlobal), falling
	// back to the live camera raster only when RsGlobal is not yet valid.
	// Matching against the live raster during a camera-raster swap frame
	// caches the swap raster's DS as g_gameDS — Restore would then rebind a
	// DS that is the wrong size for the screen RT and the rebind fails.
	int tw = 0, th = 0;
	bool haveTrusted = DpGetTrustedScreenSize(&tw, &th);
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	int matchW = haveTrusted ? tw : (camRas ? camRas->width : 0);
	int matchH = haveTrusted ? th : (camRas ? camRas->height : 0);

	D3DSURFACE_DESC desc;
	if(matchW > 0 && SUCCEEDED(pDS->GetDesc(&desc)) &&
	   (int)desc.Width == matchW && (int)desc.Height == matchH) {
		// This is the main scene DS.  Cache always, substitute only when stable.
		if(g_gameDS != pDS) {
			if(g_gameDS) g_gameDS->Release();
			g_gameDS = pDS;
			g_gameDS->AddRef();
		}

		// INTZ size must match the DS being replaced — D3D9 rejects a DS
		// smaller than the current RT, and a stale-size INTZ (created during
		// a camera-raster swap frame) makes every substitution SetDS fail and
		// leaves the device on a stale/NULL DS. Recreate at the DS's own dims
		// when they disagree; if that fails, skip substitution this frame.
		if(g_intzSurf && dimsStable) {
			D3DSURFACE_DESC izd;
			if(SUCCEEDED(g_intzSurf->GetDesc(&izd)) &&
			   ((int)izd.Width != (int)desc.Width || (int)izd.Height != (int)desc.Height)) {
				dbglog("DepthHook: INTZ %dx%d stale vs DS %dx%d — recreating",
				       (int)izd.Width, (int)izd.Height, (int)desc.Width, (int)desc.Height);
				g_intzSurf->Release(); g_intzSurf = NULL;
				if(g_intzTex) { g_intzTex->Release(); g_intzTex = NULL; }
				HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1,
					D3DUSAGE_DEPTHSTENCIL, (D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
					D3DPOOL_DEFAULT, &g_intzTex, NULL);
				if(SUCCEEDED(hr)) {
					g_intzTex->GetSurfaceLevel(0, &g_intzSurf);
					g_dsWidth = (int)desc.Width;
					g_dsHeight = (int)desc.Height;
					dbglog("DepthHook: INTZ recreated %dx%d", (int)desc.Width, (int)desc.Height);
				} else {
					dbglog("DepthHook: INTZ recreate failed hr=0x%08X (no substitution this frame)", hr);
				}
			}
			if(g_intzSurf) {
				// INTZ is about to become the depth-stencil: drop it from any
				// sampler a previous pass left it on (see DpUnbindIntzFromSamplers).
				DpUnbindIntzFromSamplers(dev);
				return orig_SetDS(dev, g_intzSurf);
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
extern void OnDeviceReset(void);

static HRESULT STDMETHODCALLTYPE hook_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pParams) {
	// Release ALL D3DPOOL_DEFAULT resources BEFORE calling original Reset
	// (D3D9 rule: all DEFAULT-pool resources must be released before Reset)
	// This includes INTZ, IBL, normal buffer, pipe chain, SMAA, velocity, SSAO.
	OnDeviceReset();

	// Release game DS separately (not owned by OnDeviceReset)
	if(g_gameDS) { g_gameDS->Release(); g_gameDS = NULL; }
	g_dsWidth = 0;
	g_dsHeight = 0;
	s_lastDsW = 0; s_lastDsH = 0; s_dsStableCount = 0;

	// Clear suspend state so Suspend/Restore doesn't desync after Reset
	g_depthSuspended = false;

	HRESULT hr = orig_Reset(dev, pParams);

	// After Reset the device's ZENABLE is the D3D9 default (TRUE with the
	// autodepthstencil), but if a suspend window was open when Reset hit, the
	// rw cache and RW driver cache still hold ZENABLE=FALSE from
	// DepthHook_Suspend's three-layer write. A later RW state flush would push
	// that stale FALSE to the device (Z drift: geometry draws without depth
	// test). Re-seed all three layers to the post-Reset default so they agree
	// with the device.
	if(SUCCEEDED(hr)) {
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
		dev->SetRenderState(D3DRS_ZENABLE, TRUE);
	}

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

	// Create INTZ resources now — prefer the trusted screen dims (RsGlobal);
	// the camera raster at install time can be the transient swap raster
	// (2048x1024), and an INTZ created at that size fails SetDS on every
	// normal-resolution frame (D3D9 rejects a DS smaller than the RT).
	{
		int iw = 0, ih = 0;
		if(!DpGetTrustedScreenSize(&iw, &ih)) {
			RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
			if(camRas) { iw = camRas->width; ih = camRas->height; }
		}
		CreateINTZResources(dev, iw, ih);
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

	// Save the authoritative device ZENABLE (layer 3).
	dev->GetRenderState(D3DRS_ZENABLE, &g_savedZenable);

	// Unbind depth-stencil and disable Z so INTZ can be sampled
	// We pass NULL DS directly (not through our hook, which would substitute)
	orig_SetDS(dev, NULL);

	// Three-layer Z disable (rw cache / RwD3D9 driver cache / raw device).
	// The rw+driver cache writes stop a later pending flush from re-enabling Z
	// mid-window; the raw write is what the next draw actually sees.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);

	g_depthSuspended = true;
}

void DepthHook_Restore(void) {
	// Close the suspend window UNCONDITIONALLY — do NOT gate on g_intzSurf or
	// g_hookInstalled. If INTZ was released mid-window (device reset / pool
	// loss) an early return here would leave the device with ZENABLE=FALSE and
	// a NULL DS outside the window: Z drift until the next frame's postfx
	// Save/RestoreRawGeomStates happened to repair it. The depth hook must
	// close its own window and never rely on the postfx wrap to undo it.
	if(!g_depthSuspended) {
		// Only warn when a window should have been open. If the hook/INTZ never
		// activated, Suspend was a no-op, so this is a benign no-op too (avoids
		// per-pass log spam when INTZ is unsupported / depthHook disabled).
		if(g_hookInstalled && g_intzSurf)
			dbglog("DepthHook: WARNING unbalanced Restore (not suspended)");
		return;
	}

	IDirect3DDevice9 *dev = d3d9device;
	g_depthSuspended = false;	// window is closed even if the device is gone

	if(!dev) return;

	// Re-enable Z to the saved value on ALL THREE layers (rw cache / RwD3D9
	// driver cache / raw device) so none is left disagreeing with the device.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(g_savedZenable ? TRUE : FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, g_savedZenable);
	dev->SetRenderState(D3DRS_ZENABLE, g_savedZenable);

	// Re-bind the game DS directly (bypasses hook). INTZ will be re-substituted
	// on the next SetDepthStencilSurface call through hook_SetDS.
	if(g_gameDS) {
		// Poisoned-cache guard: g_gameDS is cached by matching the surface's
		// dims against the screen. A camera-raster swap frame can poison it
		// with the swap raster's DS (wrong size for the screen RT) — rebinding
		// that during the screen phase fails SetDS (D3D9 rejects a DS smaller
		// than the RT), leaving DS=NULL with ZENABLE=TRUE: every draw fails
		// and the world goes blank. Verify the size first; drop a poisoned
		// cache and fall back to INTZ (Suspend required INTZ to open the
		// window, so it exists whenever a window was open).
		bool gameDsOk = true;
		D3DSURFACE_DESC gdesc;
		if(SUCCEEDED(g_gameDS->GetDesc(&gdesc))) {
			int tw = 0, th = 0;
			if(DpGetTrustedScreenSize(&tw, &th))
				gameDsOk = ((int)gdesc.Width == tw && (int)gdesc.Height == th);
		}
		if(gameDsOk) {
			orig_SetDS(dev, g_gameDS);
		} else {
			dbglog("DepthHook: Restore dropping poisoned gameDS %dx%d",
			       (int)gdesc.Width, (int)gdesc.Height);
			g_gameDS->Release(); g_gameDS = NULL;
			// Falling back to INTZ as the DS: clear it from any sampler first.
			DpUnbindIntzFromSamplers(dev);
			if(g_intzSurf) orig_SetDS(dev, g_intzSurf);
		}
	} else if(g_intzSurf) {
		// INTZ as the DS: clear it from any sampler first (a pass such as the
		// SMAA edge detect / velocity / pipe-chain composite leaves it bound).
		DpUnbindIntzFromSamplers(dev);
		orig_SetDS(dev, g_intzSurf);
	}
}

void DepthHook_ReleaseResources(void) {
	DBGLOG_ENTER("DepthHook_ReleaseResources");
	// Abnormal-release bound: if a suspend window is still open (e.g. device
	// reset / pool loss mid-window), close it BEFORE dropping INTZ. Otherwise
	// Restore could no longer run (no INTZ to re-bind) and the device would be
	// left with ZENABLE=FALSE. Mirrors DepthHook_Restore's three-layer write.
	if(g_depthSuspended) {
		IDirect3DDevice9 *dev = d3d9device;
		g_depthSuspended = false;
		if(dev) {
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(g_savedZenable ? TRUE : FALSE));
			RwD3D9SetRenderState(D3DRS_ZENABLE, g_savedZenable);
			dev->SetRenderState(D3DRS_ZENABLE, g_savedZenable);
		}
	}
	if(g_intzSurf) { dbglog("DepthHook: Release surf=%p", g_intzSurf); g_intzSurf->Release(); g_intzSurf = NULL; }
	if(g_intzTex) { dbglog("DepthHook: Release tex=%p", g_intzTex); g_intzTex->Release(); g_intzTex = NULL; }
}

// Also called from ReleaseDefaultPoolResources in postfx.cpp context
// (we expose the same cleanup path)