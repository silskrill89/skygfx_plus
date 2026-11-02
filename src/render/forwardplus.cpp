#include "../skygfx.h"
#include "../rw/gta.h"
#include "forwardplus.h"
#include "profiler.h"   // PF_SCOPE instrumentation (per-frame rebuild + upload)
#include <d3d9.h>
#include <math.h>
#include <string.h>
#include <stdio.h>     // _snprintf (ForwardPlus_StatsText)

// Tile grid constants
#define FP_TILE_SIZE     16     // pixels per tile edge
#define FP_GRID_W        128    // tile grid width (power of 2, covers up to 2048px)
#define FP_GRID_H        128    // tile grid height
#define FP_MAX_LIGHTS    256    // max lights in the unified list / light atlas (64x64 atlas, 256 texels used)
#define FP_LIGHTS_PER_TILE 16   // 2x2 RGBA8 texel block = 16 indices per tile
#define FP_ATLAS_TEX     64     // light atlas edge (64x64 = 4096 texels >= 256 lights)

// ============================================================================
// GPU INTERFACE CONTRACT (VehiclePBR_Modern.hlsl consumer):
//
//   c45 = float4(tileSizePx, gridW, gridH, lightCount)
//         tileSizePx = FP_TILE_SIZE (16), gridW/gridH = FP_GRID_W/FP_GRID_H
//         (128 — TILE counts, not texture texels), lightCount = total lights
//         in the atlases this frame (0..256). lightCount == 0 => shader skips
//         the cluster path entirely (7-light fallback).
//
//   s5  = tile index texture, D3DFMT_A8R8G8B8, size (gridW*2)x(gridH*2)
//         (256x256), POINT/CLAMP. For tile (x,y) its 16 indices live in the
//         2x2 texel block at (2x..2x+1, 2y..2y+1): texel (2x + k%2, 2y + k/2)
//         holds indices 4k..4k+3 in its R,G,B,A channels (k = 0..3).
//         Values: 0 = empty slot, else 1-based index into the light atlases
//         (1..255 representable in a byte — index 256 of a full 256-light list
//         cannot be encoded and is never referenced from tiles).
//
//   s6  = light pos/radius atlas, D3DFMT_A32B32G32R32F, 64x64, POINT.
//         Texel i (i = idx-1, row-major) = (pos.x, pos.y, pos.z, radius) in
//         VIEW space (+Z forward — see Fp_BuildViewProjFromCamera) so the
//         shader skips a world->view transform.
//
//   s7  = light color/type atlas, D3DFMT_A32B32G32R32F, 64x64, POINT.
//         Texel i = (r, g, b, type) — raw color (NOT pre-multiplied by
//         intensity), type as float (0 = point, 1 = spot, CPointLights
//         convention; the current c48-c111 consumers ignored type entirely
//         so the shader may keep ignoring it).
//
// All four resources are written from the SAME CullAndUpload pass each frame,
// so any draw that samples them via ForwardPlus_SetConstants sees a coherent
// (tile indices, light data) frame bundle. The c48-c111 constant arrays are
// GONE (they capped the system at 32 lights).
// ============================================================================

// REWIRE STATUS (2026-10-20): the light collection is no longer dead —
// CollectLights is count-only and ForwardPlus_FeedGameLights (main.cpp
// coronaFeedForwardPlus slot) feeds the fully populated CPointLights
// registry into the merge each frame. The latch below stays as the
// PER-FRAME safety net exactly as before: it engages only when a frame
// genuinely culls to zero tiles.

// D3D resources
static IDirect3DTexture9 *g_fpIndexTex = NULL;    // tile index texture (256×256 RGBA8)
static IDirect3DTexture9 *g_fpIndexTexB = NULL;   // double buffer

// Light atlases (s6 = view-space (pos.xyz, radius), s7 = (r, g, b, type)),
// also double buffered: the tile indices written in frame N index the atlas
// texels written in frame N, and SetConstants must therefore bind both ATLAS
// SETS OF THE SAME FRAME. The old single-buffered atlases were LockRect'ed
// with DISCARD every frame, which is a driver permission to hand the CPU a
// fresh allocation and let the previous content die — so the atlas a frame's
// draws sample could already be overwritten by the NEXT frame's cull while
// the double-buffer invariant held only for s5. Now s5/s6/s7 move together
// (g_fpCurrentBuffer selects the set being written, g_fpLastWrittenBuffer
// selects the set being bound). 2 * 64x64 * 16B = 256 KB total.
static IDirect3DTexture9 *g_fpPosTex[2]  = { NULL, NULL };  // s6 ping-pong
static IDirect3DTexture9 *g_fpColTex[2]  = { NULL, NULL };  // s7 ping-pong
static bool g_fpAtlasDynamic = true;              // atlases created D3DUSAGE_DYNAMIC (DISCARD-lockable)
static int g_fpCurrentBuffer = 0;                 // buffer CullAndUpload WRITES next
// DOUBLE-BUFFER INVARIANT: g_fpLastWrittenBuffer is the ONLY buffer
// ForwardPlus_SetConstants may bind — it is the buffer CullAndUpload wrote
// THIS frame (or the most recent frame that got that far). The tile texture
// and the light atlases (s6/s7) uploaded in the same CullAndUpload pass
// therefore always come from the SAME frame, so the 1-based indices stored in
// the tile texture index the matching atlas texels. CullAndUpload records
// lastWritten BEFORE flipping g_fpCurrentBuffer, so the next frame's
// LockRect(DISCARD) targets the buffer that is NOT being sampled this frame
// (double-buffer pipelining preserved). Binding the flipped
// g_fpCurrentBuffer instead (the old SetConstants behavior) sampled the
// OTHER buffer — frame N-1 tile data next to frame N light data, i.e. wrong
// lights whenever the light set changed between frames.
static int g_fpLastWrittenBuffer = -1;            // -1 = nothing written yet

// Light collection — ONE unified, brightest-sorted list (ALL sources arrive
// through the drained ForwardPlus_AddLight buffer: game CPointLights via
// ForwardPlus_FeedGameLights + 2DFX coronas via main.cpp's corona walk),
// atlas upload order = this order, so the 1-based indices stored in the
// tile texture map 1:1 onto atlas texels.
static ClusterLight g_fpLights[FP_MAX_LIGHTS];
int g_fpNumLights = 0;       // non-static: debug menu shows it next to the GPU count
static int g_fpRawCount = 0; // scene-entry CPointLights count (diagnostic — see CollectLights)
static int g_fpFeedRawReg = 0; // raw CPointLights count seen at the FEED slot (fp_stats)
static int g_fpFeedPushed = 0; // entries the feed actually pushed (fp_path audit line)

// Per-frame lights pushed by ForwardPlus_AddLight (main.cpp / 2DFX etc.).
// Callers run BEFORE ForwardPlus_CullAndUpload each frame; CullAndUpload
// drains this buffer unconditionally at entry so a deferred frame can never
// leak multiple frames of duplicate lights into the next merge.
static ClusterLight g_fpAddLights[FP_MAX_LIGHTS];
static int g_fpAddCount = 0;

// Per-light view-space centers (world->view done ONCE per frame): consumed by
// CullLightsToTiles (sphere-vs-AABB in view space) and the s6 atlas upload.
static float g_fpViewPos[FP_MAX_LIGHTS][3];

// Tile data staging buffer — full (gridW*2)x(gridH*2) RGBA8 image, i.e. one
// 2x2 texel block of 16 light indices per tile (see GPU INTERFACE CONTRACT).
static unsigned char g_fpTileData[(FP_GRID_W * 2) * (FP_GRID_H * 2) * 4];

// Lights forwarded to the GPU this frame (= g_fpNumLights on a successful
// atlas upload, 0 when the atlas upload failed and the c45.w gate must stay
// closed). Kept non-static: debugmenu_ui.cpp externs it.
int g_fpGpuLightCount = 0;

// Per-frame culling statistics (extern'd by forwardplus.h). Reset at the top
// of every CullAndUpload rebuild; only meaningful while the pass runs (they
// keep the last completed frame when it defers). Written by CPU-side code
// only, read by the ImGui row / the debug overlay.
int g_fpStats[6] = { 0, 0, 0, 0, 0, 0 };
#define FP_STAT_TILES_LIT    0   // tiles with >= 1 light
#define FP_STAT_TILES_FULL   1   // tiles pinned at the 16-slot cap
#define FP_STAT_INSTANCES    2   // light instances tested (per tile per light)
#define FP_STAT_INST_OVER    3   // instances dropped (> 16 candidates in one tile)
#define FP_STAT_CAP_DROPS    4   // lights evicted by the 256-texel atlas cap
#define FP_STAT_IDX256       5   // slots lost to the 255-encodable index edge

// ---- Debug view (forwardPlusDebug) ----
// 0 = off, 1 = tile overlay, 2 = tile overlay + legend.
//
// config->forwardPlusDebug is AUTHORITATIVE: it is the very variable
// readIniFile parses the [SkyGfx] key into (main.cpp), the one the debug-menu
// checkbox writes and saveConfigTo persists — so the menu, F11 "Reload Inis"
// and this module can never disagree (menu doctrine: every edit writes the
// variable readIniFile()/refreshIni() read). The GetPrivateProfileIntA read
// below survives ONLY as the first-load fallback for the window before
// `config` is bound. The old mtime/size re-read is gone on purpose: probing
// the ini from here raced the menu's live writes (the checkbox flipped and
// nothing happened until the user separately clicked Save INI).
static int  g_fpDbgView       = 0;
static bool g_fpDbgInited     = false;

int ForwardPlus_DebugView(void) { return g_fpDbgView; }


// Read [SkyGfx] forwardPlusDebug from the module's own ini path (skygfx.asi
// -> skygfx.ini, the same rewrite main.cpp's findInis uses). Win32 profile
// reads are relative to the process cwd, which for the ASI loader is the game
// dir — the same dir the ini lives in, so this matches the game's own reads.
// Returns -1 when the value cannot be determined (no ini), keeping the last
// good value instead of forcing the view off. FIRST-LOAD FALLBACK ONLY: once
// `config` is bound its forwardPlusDebug field wins (FpUpdateDebugView).
static int FpReadDebugViewOnce(void)
{
	char iniPath[MAX_PATH];
	// Use the RETURN value, not strlen: a truncated GetModuleFileNameA result
	// is not guaranteed NUL-terminated (XP semantics), so strlen could read
	// past the buffer before any check could fire. n < MAX_PATH guarantees
	// iniPath[n] is the terminator.
	if(!dllModule)
		return -1;
	DWORD n = GetModuleFileNameA(dllModule, iniPath, MAX_PATH);
	if(n == 0 || n >= MAX_PATH || n < 5)
		return -1;
	// ".asi" -> ".ini" (last 4 chars only; the rest of the name is kept)
	memcpy(iniPath + (n - 3), "ini", 3);
	return GetPrivateProfileIntA("SkyGfx", "forwardPlusDebug", 0, iniPath);
}

// Refresh g_fpDbgView. Called from CullAndUpload and from the top of
// ForwardPlus_DrawClusterDebug — with config bound (the normal case) it is a
// plain int compare, no stat() and no file read, so the menu checkbox takes
// effect the same frame it is clicked.
static void FpUpdateDebugView(void)
{
	// config bound -> it is the single source of truth (menu checkbox, F11
	// reload, config-selector switch and save all land in this one field).
	if(config){
		int v = config->forwardPlusDebug;
		if(v < 0) v = 0;
		else if(v > FP_DBG_LEGEND) v = FP_DBG_LEGEND;
		if(!g_fpDbgInited){
			g_fpDbgInited = true;
			g_fpDbgView = v;
			if(v && dbglog_first("fp_dbg_on"))
				dbglog("[ForwardPlus] debug view %d active (forwardPlusDebug)", v);
			return;
		}
		if(v != g_fpDbgView){
			g_fpDbgView = v;
			dbglog("[ForwardPlus] debug view -> %d", v);
		}
		return;
	}

	// config not bound yet: one-shot ini fallback, first load only.
	if(g_fpDbgInited || !dllModule)
		return;
	int v = FpReadDebugViewOnce();
	g_fpDbgView = (v < 0) ? 0 : (v > FP_DBG_LEGEND) ? FP_DBG_LEGEND : v;
	g_fpDbgInited = true;
	if(g_fpDbgView && dbglog_first("fp_dbg_on"))
		dbglog("[ForwardPlus] debug view %d active (forwardPlusDebug)", g_fpDbgView);
}

// ============================================================
// LEGACY-LIGHT FALLBACK (dark-collapse fix, wave 2) — per-frame
// forwardPlusEnable=0 semantics whenever the tile light list ends up EMPTY.
//
// WHY: the unified light list is dead end-to-end. The game half,
// CollectLights (:332), reads the CPointLights registry (count 0xC3F0D0)
// inside CullAndUpload, which runs from RenderScene_before (main.cpp:1879)
// BEFORE the scene render populates/consumes it — and any out-of-range
// count is rejected wholesale (:340) — so it collects 0. The feed half,
// main.cpp coronaFeedForwardPlus, runs from RenderEffects (AFTER the scene
// render, one-frame latency by design) and currently delivers 0 too. Log
// proof: "[ForwardPlus] tiles lit=0 instances=0" every frame. With the
// cluster path contributing nothing, the only lighting left is the legacy
// 7-direct-light path (VS c5-c11/c12-c18 + PS c5-c18, uploaded
// unconditionally by the pipe callbacks) — so this module must get out of
// the way cleanly instead of leaving the cluster gate/samplers half-live.
//
// WHAT: when the cull lit ZERO tiles this frame (covers both "collected 0 +
// added 0" and "collected N but every light culled out"),
// ForwardPlus_SetConstants takes its DISABLED branch for that frame
// (c45 = 0, s5/s6/s7 unbound) — byte-for-byte the forwardPlusEnable=0
// behaviour — and every consumer (VehiclePBR_Modern
// ClusterLighting/ClusterLightingScreen, building pipes) rides its built-in
// clusterParams.w <= 0 fallback onto the legacy registers. The full
// light-collection rewire has since landed (2026-10-20 — CollectLights is
// count-only, ForwardPlus_FeedGameLights feeds the populated registry from
// main.cpp); this latch stays exactly as built: the PER-FRAME safety net
// whenever a frame genuinely culls to zero tiles.
//
// KILL-SWITCH: [SkyGfx] forwardPlusFallback (default 1 = fallback on, 0 =
// old always-bind behaviour). Read once from the module's own ini path with
// the same first-load pattern as FpReadDebugViewOnce above — main.cpp owns
// readIniFile and is another lane's file, so the config-struct field +
// readIniFile/saveConfigTo/debug-menu wiring is staged for that lane (this
// reader is the drop-in replacement target).
// ============================================================
static bool g_fpFallbackFrame = false;   // this frame's tile list was empty

// First-load kill-switch read (see the block comment above). Cached after
// the first call; -1 marks "not read yet".
static int g_fpFallbackCfg = -1;
static bool FpFallbackEnabled(void)
{
	if(g_fpFallbackCfg < 0){
		int v = 1;
		char iniPath[MAX_PATH];
		// Same return-value discipline as FpReadDebugViewOnce: a truncated
		// GetModuleFileNameA result is not NUL-terminated — never strlen it.
		if(dllModule){
			DWORD n = GetModuleFileNameA(dllModule, iniPath, MAX_PATH);
			if(n >= 5 && n < MAX_PATH){
				memcpy(iniPath + (n - 3), "ini", 3);
				v = GetPrivateProfileIntA("SkyGfx", "forwardPlusFallback", 1, iniPath);
			}
		}
		g_fpFallbackCfg = v ? 1 : 0;
		dbglog("[ForwardPlus] fallback kill-switch [SkyGfx] forwardPlusFallback=%d (1 = legacy 7-light path whenever the tile list is empty)", g_fpFallbackCfg);
	}
	return g_fpFallbackCfg != 0;
}

// Release one texture and return NULL (keeps the ReleaseResources body a
// one-liner without if()s around each pointer).
static IDirect3DTexture9 *Release_safe(IDirect3DTexture9 *t)
{
	if(t) t->Release();
	return NULL;
}

static void FpDbgReleaseVBs(void);   // defined with the debug overlay below

// Zero one index texture. Used at CREATION time (startup / after a device
// reset, when ForwardPlus_ReleaseResources dropped the old ones) and as the
// lock-failure fallback in CullAndUpload — never on the steady-state path
// (see the comment in EnsureFPResources for why the per-frame zero was
// redundant).
static bool FpZeroIndexTexture(IDirect3DTexture9 *tex)
{
	if(!tex) return false;
	D3DLOCKED_RECT lr;
	if(FAILED(tex->LockRect(0, &lr, NULL, 0))) return false;
	for(int y = 0; y < FP_GRID_H * 2; y++)
		memset((BYTE*)lr.pBits + y * lr.Pitch, 0, FP_GRID_W * 2 * 4);
	tex->UnlockRect(0);
	return true;
}

// Pre-optimization the two index textures were zero-filled EVERY frame, which
// also meant a DEFER frame (dims-stability guard) could never sample the
// previous frame's tile indices: g_fpGpuLightCount keeps its last successful
// value there while SetConstants binds g_fpLastWrittenBuffer. Reproduce that
// exact GPU content for the one buffer that can be sampled (the other one is
// fully rewritten before its next bind). Defer frames are rare — steady-state
// frames pay nothing.
static void FpInvalidateBoundTileTexture(void)
{
	if(g_fpLastWrittenBuffer < 0) return;   // nothing written yet: buffer A is creation-zero
	FpZeroIndexTexture(g_fpLastWrittenBuffer ? g_fpIndexTexB : g_fpIndexTex);
}

// Release D3D resources
void ForwardPlus_ReleaseResources(void)
{
	if(g_fpIndexTex){ dbglog("ForwardPlus: Release indexTex=%p", g_fpIndexTex); g_fpIndexTex->Release(); g_fpIndexTex = NULL; }
	if(g_fpIndexTexB){ dbglog("ForwardPlus: Release indexTexB=%p", g_fpIndexTexB); g_fpIndexTexB = Release_safe(g_fpIndexTexB); }
	// Atlas ping-pong: release BOTH sets mirrored (never half).
	for(int b = 0; b < 2; b++){
		if(g_fpPosTex[b]){ dbglog("ForwardPlus: Release posTex[%d]=%p", b, g_fpPosTex[b]); g_fpPosTex[b] = Release_safe(g_fpPosTex[b]); }
		if(g_fpColTex[b]){ dbglog("ForwardPlus: Release colTex[%d]=%p", b, g_fpColTex[b]); g_fpColTex[b] = Release_safe(g_fpColTex[b]); }
	}
	// Debug overlay vertex buffers (D3DPOOL_DEFAULT: must die with the device)
	FpDbgReleaseVBs();
}
// Initialize textures if needed (also the device-reset recreate path:
// postfx.cpp releases everything on reset, EnsureFPResources rebuilds the
// next CullAndUpload — every texture below is created independently so a
// partial failure self-heals on a later frame).
static void EnsureFPResources(IDirect3DDevice9 *dev, int screenW, int screenH)
{
	// Create double-buffered index textures (tile grid 128x128 tiles ->
	// 256x256 texels: each tile owns a 2x2 texel block, see the contract).
	// newA/newB mark the textures created by THIS call so the creation-time
	// zero below runs once per texture instead of every frame.
	bool newA = false, newB = false;
	if(!g_fpIndexTex){
		if(FAILED(dev->CreateTexture(FP_GRID_W * 2, FP_GRID_H * 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fpIndexTex, NULL))){
			dbglog("[ForwardPlus] CreateTexture index A failed");
			return;
		}
		newA = true;
	}
	if(!g_fpIndexTexB){
		if(FAILED(dev->CreateTexture(FP_GRID_W * 2, FP_GRID_H * 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fpIndexTexB, NULL))){
			dbglog("[ForwardPlus] CreateTexture index B failed");
			return;
		}
		newB = true;
	}

	// Light atlases (s6/s7): 64x64 A32B32G32R32F, POINT-sampled. Try
	// D3DUSAGE_DYNAMIC first (DISCARD locks), fall back to a plain DEFAULT
	// texture (some devices reject DYNAMIC for float formats) and lock
	// without DISCARD. UploadLightAtlases handles the lock-flag switch.
	// DOUBLE BUFFERED like the index texture: frame N writes set [cur] and
	// SetConstants binds set [lastWritten], so the atlas a frame's draws
	// sample is never the one the next frame is overwriting.
	if(!g_fpPosTex[0] || !g_fpPosTex[1] || !g_fpColTex[0] || !g_fpColTex[1]){
		// Self-heal partial state (pair invariant: created/released together)
		for(int b = 0; b < 2; b++){
			g_fpPosTex[b] = Release_safe(g_fpPosTex[b]);
			g_fpColTex[b] = Release_safe(g_fpColTex[b]);
		}
		g_fpAtlasDynamic = true;
		for(int b = 0; b < 2; b++){
			HRESULT hr = dev->CreateTexture(FP_ATLAS_TEX, FP_ATLAS_TEX, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &g_fpPosTex[b], NULL);
			if(FAILED(hr)){
				g_fpAtlasDynamic = false;
				hr = dev->CreateTexture(FP_ATLAS_TEX, FP_ATLAS_TEX, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &g_fpPosTex[b], NULL);
			}
			if(FAILED(hr)){
				dbglog("[ForwardPlus] CreateTexture pos atlas[%d] failed (A32B32G32R32F unsupported?)", b);
				g_fpPosTex[b] = NULL;
				// Release the whole set: half a ping-pong is worse than none
				for(int k = 0; k < 2; k++){
					g_fpPosTex[k] = Release_safe(g_fpPosTex[k]);
					g_fpColTex[k] = Release_safe(g_fpColTex[k]);
				}
				return;
			}
			if(FAILED(dev->CreateTexture(FP_ATLAS_TEX, FP_ATLAS_TEX, 1, g_fpAtlasDynamic ? D3DUSAGE_DYNAMIC : 0, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &g_fpColTex[b], NULL))){
				dbglog("[ForwardPlus] CreateTexture col atlas[%d] failed", b);
				for(int k = 0; k < 2; k++){
					g_fpPosTex[k] = Release_safe(g_fpPosTex[k]);
					g_fpColTex[k] = Release_safe(g_fpColTex[k]);
				}
				return;
			}
		}
		dbglog("[ForwardPlus] textures created %dx%d index + 2x 64x64 atlas x2 sets (%s) for %dx%d screen",
			FP_GRID_W * 2, FP_GRID_H * 2, g_fpAtlasDynamic ? "dynamic" : "static", screenW, screenH);
	}

	// Zero-init a NEWLY CREATED index texture (creation / device-reset only).
	// This is the only zeroing the index textures need:
	//   - SetConstants binds buffer A while g_fpLastWrittenBuffer is still -1
	//     (the dims-stability guard defers the first uploads). c45.w is 0 in
	//     that window so the shader skips the cluster fetch anyway, but
	//     zeroing here keeps that bind from ever hitting uninitialized
	//     indices, exactly like the old per-frame zero did on those frames.
	//   - Every later bind is a buffer UploadTileTexture has fully written
	//     (256 rows x 256 texels, D3DLOCK_DISCARD), and the double-buffer
	//     latch (g_fpLastWrittenBuffer) is the ONLY buffer SetConstants
	//     binds — so no frame can ever sample the not-yet-written buffer.
	// The old unconditional loop cost 2 LockRect + 512 KB memset EVERY frame
	// and was pure redundancy on this path.
	for(int b = 0; b < 2; b++){
		if(!(b ? newB : newA)) continue;
		if(!FpZeroIndexTexture(b ? g_fpIndexTexB : g_fpIndexTex))
			dbglog("[ForwardPlus] index texture[%d] zero-init lock failed", b);
	}

	// Register scope tags for crash backtrace
	diag_registerScope("ForwardPlus_CullAndUpload", (void*)ForwardPlus_CullAndUpload);
	diag_registerScope("ForwardPlus_ReleaseResources", (void*)ForwardPlus_ReleaseResources);
	diag_registerScope("ForwardPlus_SetConstants", (void*)ForwardPlus_SetConstants);
}

// GTA SA point light structure — layout verified against gta_sa.exe's own
// CPointLights::Add (0x7001FD writes pos/dir/radius/color at exactly these
// offsets) and matching main.cpp's PointLight / aap-skygfx's reference:
struct CRegisteredPointLight {
	float posX, posY, posZ;       // 0x00 world position
	float dirX, dirY, dirZ;       // 0x0C direction (for spotlights)
	float radius;                 // 0x18 attenuation radius
	float colorR, colorG, colorB; // 0x1C colour (floats, 0-1)
	void *attachedTo;             // 0x28
	unsigned char type;           // 0x2C: 0=point, 1=spot
	unsigned char fogType;        // 0x2D
	unsigned char flags;          // 0x2E: bit 0 = check direction, bit 1 = cast shadow
	unsigned char pad;            // 0x2F
}; // 48 bytes per entry

// GTA SA 1.0 US addresses — the previous pair (0xC3A090 / 0xC3A0A0) was
// fabricated: zero code references to them exist in gta_sa.exe, so the
// collector read unrelated memory (count out of range => "collected 0
// lights" every frame). Proven from the exe's own code:
//   0x700176  mov edx, [0xC3F0D0] / cmp edx, 0x20  <- active count, cap 32
//   0x700272  inc edx / mov [0xC3F0D0], edx        <- count store in Add
//   0x7001FD  lea eax, [ecx + 0xC3F0E0] (stride 0x30 = 48-byte entries)
//   0x700985  fsub dword ptr [esi + 0xC3F0E0]      <- third entries embed
//
// MOVED-ARRAY INDIRECTION (aap PR #43): limit adjusters can relocate both
// arrays, so the bases are READ OUT OF THE GAME'S OWN INSTRUCTIONS instead of
// being compiled in. Verified bytes in the live gta_sa.exe (1.0 US):
//   0x700176: 8B 15 D0 F0 C3 00  = mov edx, dword ptr [0xC3F0D0]
//   0x7001FD: 8D 81 E0 F0 C3 00  = lea eax, [ecx + 0xC3F0E0]
//                                  (opcode 8D, modrm 81 = disp32 at +2)
// Both are 6-byte instructions, so the embedded base is a DWORD at +2. The
// count base (0xC3F0D0) and the entry base (0xC3F0E0) are DIFFERENT arrays,
// so each immediate is read from its own anchor. A pre-patcher that relocates
// the arrays rewrites these immediates; reading them at runtime follows the
// move instead of touching the vanilla static addresses.
//
// SANITY: an unexpected exe build (or an anchor that is not the instruction we
// expect) must not be trusted. The resolver checks the two opcode bytes,
// range-checks the resolved pointer values against the module window
// (0x400000-0xC000000) and the sampled count (0..1024), and falls back to the
// vanilla 1.0 US pair with ONE dbglog line if anything looks wrong. Resolution
// is one-shot; the pointers are cached after the first feed/collect call.
#define FP_LIGHT_COUNT_INSN    0x700176u   // mov edx,[imm32]     (count base)
#define FP_LIGHT_ENTRY_INSN    0x7001FDu   // lea eax,[ecx+imm32] (entries base)
#define FP_LIGHT_COUNT_VANILLA 0xC3F0D0u   // 1.0 US fallback
#define FP_LIGHT_ENTRY_VANILLA 0xC3F0E0u   // 1.0 US fallback
#define FP_LIGHT_PTR_MIN       0x400000u   // module image window (sanity)
#define FP_LIGHT_PTR_MAX       0xC000000u

static int *NumLights = NULL;                      // CPointLights::NumLights
static CRegisteredPointLight *PointLights = NULL;  // CPointLights::aPointLights
static int s_fpLightBases = 0;   // 0 = unresolved, 1 = exe anchors, 2 = vanilla fallback

// Resolve the registry bases from the exe's own instructions (see above).
// POD locals only, no allocation, one-shot. Never leaves the pointers NULL:
// both the anchor path and the fallback install concrete addresses, so the
// hot-path guards test a real pointer VALUE (not the anchor address).
static void FpResolveLightBases(void)
{
	if(s_fpLightBases)
		return;

	NumLights = (int*)(uintptr_t)FP_LIGHT_COUNT_VANILLA;
	PointLights = (CRegisteredPointLight*)(uintptr_t)FP_LIGHT_ENTRY_VANILLA;
	s_fpLightBases = 2;   // fallback unless every check below passes

	const unsigned char *countInsn = (const unsigned char*)(uintptr_t)FP_LIGHT_COUNT_INSN;
	const unsigned char *entryInsn = (const unsigned char*)(uintptr_t)FP_LIGHT_ENTRY_INSN;
	// 8B 15 = mov edx,[disp32]; 8D 81 = lea eax,[ecx+disp32]. An anchor holding
	// anything else is not the 1.0 US code the absolute addresses in this file
	// assume, so the vanilla pair is the only safe choice.
	if(countInsn[0] != 0x8B || countInsn[1] != 0x15 ||
	   entryInsn[0] != 0x8D || entryInsn[1] != 0x81){
		dbglog("[ForwardPlus] CPointLights anchors not found (bytes %02X %02X / %02X %02X) — using 1.0 US 0x%X/0x%X",
		       countInsn[0], countInsn[1], entryInsn[0], entryInsn[1],
		       FP_LIGHT_COUNT_VANILLA, FP_LIGHT_ENTRY_VANILLA);
		return;
	}

	// disp32 sits at +2 in both 6-byte encodings (opcode + modrm precede it).
	int *countPtr = (int*)(uintptr_t)*(const unsigned int*)(uintptr_t)(FP_LIGHT_COUNT_INSN + 2u);
	CRegisteredPointLight *entryPtr =
		(CRegisteredPointLight*)(uintptr_t)*(const unsigned int*)(uintptr_t)(FP_LIGHT_ENTRY_INSN + 2u);

	// The resolvable pointers must stay inside the module/static window; a
	// relocated array on this 32-bit build lands there too (same policy as the
	// other exe-address guards). The count is sampled once for its range test:
	// the game's own Add caps it at 32, so anything outside 0..1024 means the
	// anchor read garbage.
	// Each reject path logs BOTH resolved values (plus the vanilla pair it is
	// falling back to) exactly once — s_fpLightBases latches at function entry,
	// so a failed resolve cannot re-log per frame.
	if((uintptr_t)countPtr < (uintptr_t)FP_LIGHT_PTR_MIN ||
	   (uintptr_t)countPtr > (uintptr_t)FP_LIGHT_PTR_MAX){
		dbglog("[ForwardPlus] CPointLights count anchor implausible (resolved count=0x%X entry=0x%X) — using 1.0 US 0x%X/0x%X",
		       (unsigned)(uintptr_t)countPtr, (unsigned)(uintptr_t)entryPtr,
		       FP_LIGHT_COUNT_VANILLA, FP_LIGHT_ENTRY_VANILLA);
		return;
	}
	if((uintptr_t)entryPtr < (uintptr_t)FP_LIGHT_PTR_MIN ||
	   (uintptr_t)entryPtr > (uintptr_t)FP_LIGHT_PTR_MAX){
		dbglog("[ForwardPlus] CPointLights entry anchor implausible (resolved count=0x%X entry=0x%X) — using 1.0 US 0x%X/0x%X",
		       (unsigned)(uintptr_t)countPtr, (unsigned)(uintptr_t)entryPtr,
		       FP_LIGHT_COUNT_VANILLA, FP_LIGHT_ENTRY_VANILLA);
		return;
	}
	int count = *countPtr;
	if(count < 0 || count > 1024){
		dbglog("[ForwardPlus] CPointLights anchor count implausible (resolved count=0x%X entry=0x%X sampled=%d) — using 1.0 US 0x%X/0x%X",
		       (unsigned)(uintptr_t)countPtr, (unsigned)(uintptr_t)entryPtr, count,
		       FP_LIGHT_COUNT_VANILLA, FP_LIGHT_ENTRY_VANILLA);
		return;
	}

	NumLights = countPtr;
	PointLights = entryPtr;
	s_fpLightBases = 1;
	dbglog("[ForwardPlus] CPointLights bases resolved from exe anchors: count=0x%X entries=0x%X",
	       (unsigned)(uintptr_t)countPtr, (unsigned)(uintptr_t)entryPtr);
}
#define MAX_GAME_LIGHTS 32

// Starved-drain backstop (2026-10-25): the last registry set
// ForwardPlus_FeedGameLights successfully pushed. The per-frame feed latch
// (main.cpp coronaFeedForwardPlus, CTimer::m_FrameCounter) guarantees at
// most ONE push per game frame, and the sky-phase slot can win that push —
// so a drain CAN arrive with an empty deferred buffer while the registry
// itself is populated (sky phase absent on the following frame leaves the
// RenderEffects push one drain too late). The snapshot replays the last
// fed set on such starved drains so a populated registry can never produce
// a collected=0 frame. It is CLEARED whenever a feed observes an empty
// registry, so genuinely lightless frames still take the fp_fallback
// legacy path; the TTL bounds stale-light replay across pauses/hitches.
static ClusterLight g_fpRegSnapshot[MAX_GAME_LIGHTS];
static int   g_fpRegSnapshotCount = 0;
static DWORD g_fpRegSnapshotTick  = 0;
#define FP_SNAPSHOT_TTL_MS 2000

// Collect lights from GTA SA's CPointLights system.
//
// REWIRED (2026-10-20, "Forward+ light collection" work item): the registry
// COPY moved out of this pass — this pass now reads the registry as a
// DIAGNOSTIC COUNT ONLY.
//
// WHY: at this point (CullAndUpload from RenderScene_before) the registry is
// freshly CLEARED every frame — the frame-render glue zeroes NumLights
// INLINE (`mov dword ptr [0xC3F0D0], 0` @ 0x53E977) BEFORE it calls
// RenderScene (0x53EABF, where this pass runs), and the per-frame dynamic
// lights (vehicle head/tail lights, muzzle flashes, fires, 2DFX lamps in
// range — CPointLights::Add 0x7000E0, 19 call sites) register DURING the
// scene render that follows. Copying here therefore collected 0 every frame
// (field evidence: rawGameCount=0), and when the pre-render chain
// (0x53BEE0, called between the clear and RenderScene) HAD registered
// lights (field evidence: rawGameCount=7 frames), copying here would
// DUPLICATE the feed's copy of those same lights in the unified list.
// The single registry source is now ForwardPlus_FeedGameLights (main.cpp
// coronaFeedForwardPlus slot — reads the full, fully populated registry
// and rides the AddLight buffer into the merge): one-frame latency for
// everything, zero ghost duplicates.
static void CollectLights(void)
{
	g_fpNumLights = 0;
	g_fpRawCount = 0;

	// One-shot anchor resolution (no-op after the first frame): the guard
	// below then tests the RESOLVED pointer value, never the anchor address.
	FpResolveLightBases();

	// Diagnostic read: the scene-entry registry count (usually 0 post-clear;
	// nonzero only for the pre-render chain's registrations). Kept for the
	// fp/fp_fallback log lines and the menu row — nothing is copied here.
	if(!NumLights) return;
	g_fpRawCount = *NumLights;
}

// Public light-collection API (main.cpp / 2DFX / any lane with world lights).
// Contract:
//   - Call BEFORE ForwardPlus_CullAndUpload each frame (the buffer is drained
//     once per CullAndUpload, after its transient defer gates pass — a
//     deferred frame KEEPS the buffer losslessly; multi-frame accumulation
//     while deferred is absorbed by the 0.25m dedup + the 256 cap).
//     A push AFTER the drain is legal too — it simply rides the NEXT frame's
//     drain (that is the RenderEffects-entry feed slot's one-frame latency).
//   - x,y,z = WORLD position, radius = attenuation radius (world units),
//     r,g,b = linear color (uploaded raw into the s7 atlas), type = 0=point,
//     1=spot (CPointLights convention; consumers may ignore it).
//   - Buffer capacity is FP_MAX_LIGHTS (256); overflow is silently dropped
//     past 256 with ONE dbglog_first.
//   - Lights are merged with the CPointLights set into ONE unified,
//     brightest-sorted list (≤256) before atlas upload.
void ForwardPlus_AddLight(float x, float y, float z, float radius, float r, float g, float b, float type)
{
	// Feature off: no-op before any buffer bookkeeping. main.cpp's corona walk
	// calls this unconditionally, so without the gate every disabled frame
	// would still push up to 64 lights only for CullAndUpload to discard them.
	if(!config || !config->forwardPlusEnable) return;

	// Same degenerate-light filter the CPointLights feed applies (also the
	// corona walk's): drop zero-radius and invisible lights at the buffer.
	if(!(radius > 0.0f)) return;
	float brightness = r + g + b;
	if(brightness < 0.001f) return;

	if(g_fpAddCount >= FP_MAX_LIGHTS){
		// Silent drop past 256 — one log for the process lifetime.
		if(dbglog_first("fp_add_overflow"))
			dbglog("[ForwardPlus] AddLight buffer full (%d) — dropping extra lights", FP_MAX_LIGHTS);
		return;
	}

	ClusterLight &cl = g_fpAddLights[g_fpAddCount++];
	cl.x = x;
	cl.y = y;
	cl.z = z;
	cl.radius = radius;
	cl.r = r;
	cl.g = g;
	cl.b = b;
	cl.intensity = brightness; // sort key only (r+g+b, same as CollectLights)
	cl.type = type;
	cl.priority = 0.0f;        // set by FpMergeAddedLights once the camera is known
}

// Push the CURRENT CPointLights registry into the AddLight deferred buffer.
// Registry = the game's per-frame dynamic light list (0xC3F0D0 count /
// 0xC3F0E0 entries, 48-byte stride, capacity 32 — layout verified against
// the exe's own CPointLights::Add writes and the per-entity lighting
// consumer 0x6FFBB0, which reads pos/radius/type at exactly these offsets).
//
// TIMING CONTRACT: call when the registry holds a FULLY populated frame —
// after the scene render that filled it, before the NEXT frame's inline
// clear (`mov dword ptr [0xC3F0D0], 0` @ 0x53E977 in the frame-render glue,
// just before RenderScene 0x53EABF). The main.cpp feed slot
// (coronaFeedForwardPlus — sky-phase reflection hooks + RenderEffects
// entry, once per frame via the frame-counter latch) sits inside that
// window; the entries are drained into the NEXT CullAndUpload's merge, so
// lights registered during frame N light frame N+1. One-frame latency
// (SA lights are quasi-static per frame); zero lights was the bug.
//
// VANILLA-LIGHTING SEMANTICS: registry types 3 and 4 are SKIPPED — the
// exe's own entity-lighting consumer (0x6FFBB0, called from CRenderer
// 0x553DE4/0x553EAF) skips exactly those types when computing surface
// lighting, so they are not real contributors. Radius, colour and type
// upload raw into the s6/s7 atlases like every other feed entry.
//
// Over-cap policy: the game's own Add caps the registry at 32
// (`cmp edx, 0x20` @ 0x700176), so counts >32 are external pokes — scan
// the 32 structurally-valid slots (clamp) instead of the old wholesale
// reject->0. GPU budget: the merged unified list caps at FP_MAX_LIGHTS
// (256) with camera-aware eviction (FpInsertLightSorted/FpLightPriority:
// intensity / max(dist_to_camera, radius) — nearest-to-camera / brightest
// win), so over-cap never collapses the list to empty.
// Returns the number of lights pushed (diagnostics).
int ForwardPlus_FeedGameLights(void)
{
	g_fpFeedRawReg = 0;
	g_fpFeedPushed = 0;
	// Feature off: the main.cpp feed slot runs unconditionally (it cannot see
	// this lane's config gate without duplicating it), so the no-op lives
	// HERE — no registry walk, no snapshot churn, no AddLight pushes. The
	// disabled CullAndUpload path discards the buffer anyway; clearing the
	// snapshot keeps a re-enable from replaying pre-disable lights.
	if(!config || !config->forwardPlusEnable){
		g_fpRegSnapshotCount = 0;
		return 0;
	}
	FpResolveLightBases();
	if(!NumLights || !PointLights) return 0;
	int numLights = *NumLights;
	if(numLights <= 0){
		// Registry genuinely empty at this slot — invalidate the backstop so
		// a starved drain falls back instead of replaying vanished lights.
		g_fpRegSnapshotCount = 0;
		g_fpRegSnapshotTick = GetTickCount();
		return 0;
	}
	g_fpFeedRawReg = numLights;                 // raw count seen at the feed slot
	if(numLights > MAX_GAME_LIGHTS) numLights = MAX_GAME_LIGHTS;

	int pushed = 0;
	int snapped = 0;
	for(int i = 0; i < numLights; i++){
		const CRegisteredPointLight &light = PointLights[i];
		if(light.type == 3 || light.type == 4) continue;   // not lighting contributors (vanilla)
		if(!(light.radius > 0.0f)) continue;               // degenerate (also rejects NaN)
		float brightness = light.colorR + light.colorG + light.colorB;
		if(brightness < 0.001f) continue;                  // invisible
		int before = g_fpAddCount;
		ForwardPlus_AddLight(light.posX, light.posY, light.posZ, light.radius,
			light.colorR, light.colorG, light.colorB, (float)light.type);
		// Snapshot every entry that actually landed in the deferred buffer
		// (AddLight re-applies the same degenerate filter and can drop on
		// 256 overflow — compare the count, never assume).
		if(g_fpAddCount > before && snapped < MAX_GAME_LIGHTS)
			g_fpRegSnapshot[snapped++] = g_fpAddLights[g_fpAddCount - 1];
		pushed++;
	}
	g_fpFeedPushed = pushed;
	// Refresh the backstop on EVERY executed feed — including an
	// all-filtered registry (count 0 + fresh tick is the correct "registry
	// holds no valid lights" state for later starved drains).
	g_fpRegSnapshotCount = snapped;
	g_fpRegSnapshotTick = GetTickCount();
	return pushed;
}

// Sphere-vs-AABB test (O3DE Atom approach)
static bool SphereVsAABB(float cx, float cy, float cz, float radius,
                          float minX, float minY, float minZ,
                          float maxX, float maxY, float maxZ)
{
	float dx = (cx < minX) ? (minX - cx) : ((cx > maxX) ? (cx - maxX) : 0.0f);
	float dy = (cy < minY) ? (minY - cy) : ((cy > maxY) ? (cy - maxY) : 0.0f);
	float dz = (cz < minZ) ? (minZ - cz) : ((cz > maxZ) ? (cz - maxZ) : 0.0f);
	return (dx*dx + dy*dy + dz*dz) <= (radius * radius);
}

// Distance from a point to an AABB (0 when inside) — the per-tile brightness
// sort key is intensity / distance (see CullLightsToTiles).
static float AabbDistance(float cx, float cy, float cz,
                          float minX, float minY, float minZ,
                          float maxX, float maxY, float maxZ)
{
	float dx = (cx < minX) ? (minX - cx) : ((cx > maxX) ? (cx - maxX) : 0.0f);
	float dy = (cy < minY) ? (minY - cy) : ((cy > maxY) ? (cy - maxY) : 0.0f);
	float dz = (cz < minZ) ? (minZ - cz) : ((cz > maxZ) ? (cz - maxZ) : 0.0f);
	return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Insertion-sort the collected game lights by intensity DESCENDING (the
// unified list is brightest-sorted so the atlas is filled brightest-first —
// and the dimmest lights are the ones lost to the 255-encodable-index edge
// case and to the 256-light cap).
static void FpSortLightsByIntensity(void)
{
	for(int i = 1; i < g_fpNumLights; i++){
		ClusterLight keyLight = g_fpLights[i];
		int j = i - 1;
		while(j >= 0 && g_fpLights[j].intensity < keyLight.intensity){
			g_fpLights[j + 1] = g_fpLights[j];
			j--;
		}
		g_fpLights[j + 1] = keyLight;
	}
}

// Camera-aware priority key for the 256-texel atlas cap. intensity /
// max(dist_to_camera, radius) keeps a NEAR street lamp above a FAR
// super-bright light (the far light's radius usually exceeds its distance,
// the near lamp's does not), so the globally brightest lights can no longer
// evict every light in the player's vicinity. Below the cap the key is
// irrelevant — the merge keeps pure intensity ordering there.
static float FpLightPriority(const ClusterLight &l, float camX, float camY, float camZ)
{
	float dx = l.x - camX, dy = l.y - camY, dz = l.z - camZ;
	float dist2 = dx*dx + dy*dy + dz*dz;
	float dist = sqrtf(dist2);
	float den = (dist > l.radius) ? dist : l.radius;
	if(den < 1e-3f) den = 1e-3f;
	return l.intensity / den;
}

// Insert ONE light into the unified list keeping intensity-descending order
// ("one unified light list, brightest-sorted, ≤256"). When the list is full
// the lowest-PRIORITY entry is evicted if the new light ranks higher, so the
// 256 atlas texels always hold the best 256 candidates regardless of arrival
// order (game lights vs AddLight callers) and regardless of whether the
// brightest ones are near the camera or half the map away.
// Returns the number of entries this call evicted (diagnostics only).
static int FpInsertLightSorted(const ClusterLight &cl)
{
	if(g_fpNumLights >= FP_MAX_LIGHTS){
		if(cl.priority <= g_fpLights[FP_MAX_LIGHTS - 1].priority)
			return 0;                    // lower priority than the worst kept — dropped
		g_fpNumLights = FP_MAX_LIGHTS - 1; // evict tail
	}
	// First position with intensity < cl.intensity (stable: insert after ties)
	int lo = 0, hi = g_fpNumLights;
	while(lo < hi){
		int mid = (lo + hi) / 2;
		if(g_fpLights[mid].intensity >= cl.intensity) lo = mid + 1;
		else hi = mid;
	}
	for(int i = g_fpNumLights; i > lo; i--)
		g_fpLights[i] = g_fpLights[i - 1];
	g_fpLights[lo] = cl;
	g_fpNumLights++;
	return 1;
}

// Merge the drained ForwardPlus_AddLight buffer into the unified list.
// camXYZ = this frame's camera world position (for the cap priority).
// Returns the number of entries evicted while capping the list at 256.
static int FpMergeAddedLights(const ClusterLight *addLights, int addCount,
                              float camX, float camY, float camZ)
{
	int drops = 0;
	for(int i = 0; i < addCount; i++){
		ClusterLight cl = addLights[i];
		cl.priority = FpLightPriority(cl, camX, camY, camZ);
		drops += FpInsertLightSorted(cl);
	}
	return drops;
}

// Cross-source duplicate removal over the drained add buffer. Street lamps
// and other 2DFX register BOTH a point light (CPointLights registry) and a
// corona (visual quad) at the same world position, and both feeds push into
// the same buffer — the same physical lamp would otherwise light twice
// (double brightness + doubled per-tile instances). Feed order puts the
// registry entries FIRST (main.cpp calls ForwardPlus_FeedGameLights before
// the corona walk), so the surviving entry is the registry copy with the
// true attenuation radius. Position-only test, squared (no sqrt); a
// 0.25 m tolerance keeps adjacent traffic headlights (>= 1.5 m apart)
// distinct while merging the co-registered lamp pair (same 2DFX origin).
// O(n^2) BY DECISION, not by oversight (cost audit 2026-10-25). It runs once
// per frame on the drained buffer, but every in-tree producer is bounded:
// ForwardPlus_FeedGameLights pushes <= 32 registry entries (the game's own
// Add cap at 0x700176) and main.cpp's corona walk pushes <= 64 (MAX_CORONAS),
// so n <= 96 and the worst case is 96*95/2 = 4560 squared-distance tests.
// The replay path is smaller still (snapshot <= 32). Field log (drainRaw):
// max 14 entries, i.e. < 100 tests/frame. A spatial hash or an x-sorted sweep
// would add a static index table + a sort or 27-cell probe per light for a
// saving well under a microsecond — pure complexity. The FP_MAX_LIGHTS (256)
// buffer cap is the only way n can grow, and reaching it needs 256+ feeds in
// one frame, which no current caller does.
//
// COST MODEL (measured, not guessed): each candidate does ONE squared-distance
// compare against every survivor and the inner loop exits on the first hit
// (`for(j = 0; j < out && !dup; j++)`), so a duplicate pair costs FEWER tests
// than the worst case. Worst case = no duplicates: n(n-1)/2 compares. At the
// proven in-tree bound n = 96 that is 4,560 compares (3 mul + 2 add + 1 cmp
// each, ~4 float ops per compare => ~18k FLOPs, tens of microseconds at most
// per FRAME, not per light). Only an out-of-contract producer can push n past
// 96 toward the FP_MAX_LIGHTS (256) cap, where the same formula tops out at
// 32,640 compares — still sub-millisecond, hence no spatial index is worth the
// extra mutable state. `s_fpDedupMaxN` records the live maximum and logs the
// first exceedance of the proven bound once (dbglog_first: process lifetime);
// the throttled fp_stats line prints maxn so field logs carry the real value.
#define FP_DEDUP_PROVEN_MAX 96   // 32 registry (Add cap) + 64 corona (MAX_CORONAS)
static int s_fpDedupMaxN = 0;    // largest drained buffer ever deduped (audit)
// Returns the compacted count.
static int FpDedupAddLights(ClusterLight *buf, int n)
{
	if(n > s_fpDedupMaxN){
		s_fpDedupMaxN = n;
		if(n > FP_DEDUP_PROVEN_MAX && dbglog_first("fp_dedup_maxn"))
			dbglog("[ForwardPlus] dedup n=%d exceeds proven feed bound %d (worst case %d compares)",
			       n, FP_DEDUP_PROVEN_MAX, n * (n - 1) / 2);
	}
	int out = 0;
	for(int i = 0; i < n; i++){
		bool dup = false;
		for(int j = 0; j < out && !dup; j++){
			float dx = buf[i].x - buf[j].x;
			float dy = buf[i].y - buf[j].y;
			float dz = buf[i].z - buf[j].z;
			dup = (dx*dx + dy*dy + dz*dz) < 0.0625f;   // 0.25^2
		}
		if(!dup){
			if(out != i) buf[out] = buf[i];
			out++;
		}
	}
	return out;
}

// Cull lights into 16×16 screen-space tiles (O3DE Atom-style), 16 lights per
// tile kept by brightest-first intensity/distance sort, written as 1-based
// atlas indices into the 2x2 texel block of s5 (see GPU INTERFACE CONTRACT).
static void CullLightsToTiles(int screenW, int screenH,
                               const D3DMATRIX &projMat, float camNear, float camFar)
{
	int tilesX = (screenW + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	int tilesY = (screenH + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	if(tilesX > FP_GRID_W) tilesX = FP_GRID_W;
	if(tilesY > FP_GRID_H) tilesY = FP_GRID_H;

	// Clear tile data — ONLY when the tile geometry changes (and on the
	// degenerate-projection early-out below, see there). Every tile inside
	// the active tilesX x tilesY grid writes its FULL 16-slot (2x2 texel)
	// block below, so those bytes never carry over between frames and the
	// old per-frame 256KB memset was redundant. The bytes OUTSIDE the active
	// grid still exist in the staging buffer (UploadTileTexture copies all
	// 256x256 texels) and the shader clamps tile coords to grid-1 (not to
	// tilesX-1/tilesY-1), so they must stay zero: clearing the whole buffer
	// once per dims change guarantees that, because nothing ever writes
	// outside the active grid. Byte-identical to the old per-frame clear.
	static int s_fpClearW = 0;
	static int s_fpClearH = 0;
	if(screenW != s_fpClearW || screenH != s_fpClearH){
		memset(g_fpTileData, 0, sizeof(g_fpTileData));
		s_fpClearW = screenW;
		s_fpClearH = screenH;
	}

	// Per-tile candidate buffers (hoisted out of the tile loop — ~8k tiles
	// reuse these; insertion sort, so no comparator state needed)
	int candLight[FP_MAX_LIGHTS];
	float candKey[FP_MAX_LIGHTS];

	// Projection + depth-band constants — identical for every tile, hoisted
	// out of the tile loops. For a D3D9 LH perspective matrix a view-space
	// point at depth z projects to ndcX = x * proj00 / z, so the inverse
	// unprojection is x = ndcX * z / proj00 (same for Y with proj11).
	// Degenerate scales would cull nothing sensibly; the old per-tile
	// `continue` left every tile zeroed, so an O(1) early return here is the
	// same all-unlit outcome without ~8k no-op tile iterations. The explicit
	// clear keeps the staging buffer all-zero on this path: CullAndUpload
	// uploads it even when the cull wrote no tile (non-fallback frame), and
	// the old unconditional clear ran before this early return too.
	float proj00 = projMat.m[0][0];
	float proj11 = projMat.m[1][1];
	if(!(proj00 > 1e-7f) || !(proj11 > 1e-7f)){
		memset(g_fpTileData, 0, sizeof(g_fpTileData));
		return;
	}
	float invProj00 = 1.0f / proj00;	// guarded above (1e-7 division guard)
	float invProj11 = 1.0f / proj11;
	// CONVENTION: view space here is +Z FORWARD — Fp_BuildViewProjFromCamera
	// inverts the camera frame LTM, so v_view.z = (world - camPos) . at,
	// positive in front of the camera (the same convention
	// HeightFog.hlsl / ReconstructViewPos assume; the GetTransform
	// fallback matrices are +Z-forward too). Scene depth therefore lives in
	// [+camNear, +camFar] for every tile (the earlier -Z-forward bracketing
	// bug is history — see the W2 z-sign fix).
	float tileMinZ = camNear;
	float tileMaxZ = camFar;

	// For each tile, build a frustum slab in view space and test lights
	for(int ty = 0; ty < tilesY; ty++){
		for(int tx = 0; tx < tilesX; tx++){
			// Tile screen bounds (normalized [0,1]) -> NDC bounds. ndcY0 is
			// the tile's BOTTOM edge (screen V grows downward, NDC Y upward),
			// ndcY1 the top edge, so ndcY0 < ndcY1 always.
			float u0 = (float)(tx * FP_TILE_SIZE) / (float)screenW;
			float v0 = (float)(ty * FP_TILE_SIZE) / (float)screenH;
			float u1 = (float)((tx + 1) * FP_TILE_SIZE) / (float)screenW;
			float v1 = (float)((ty + 1) * FP_TILE_SIZE) / (float)screenH;
			float ndcX0 = u0 * 2.0f - 1.0f;
			float ndcX1 = u1 * 2.0f - 1.0f;
			float ndcY0 = 1.0f - v1 * 2.0f; // flip Y
			float ndcY1 = 1.0f - v0 * 2.0f;

			// Test each light against this tile's frustum slice. Light
			// centers come pre-transformed to view space (g_fpViewPos — same
			// +Z-forward row-vector transform v_world * viewMat as the old
			// inline code, hoisted to once per frame in CullAndUpload for
			// both the cull and the s6 atlas upload).
			int nCand = 0;
			for(int li = 0; li < g_fpNumLights; li++){
				float vx = g_fpViewPos[li][0];
				float vy = g_fpViewPos[li][1];
				float vz = g_fpViewPos[li][2];
				const ClusterLight &light = g_fpLights[li];
				float radius = light.radius;

				// DEGENERATE-CULL FIX (was: every light "lit" every tile —
				// log evidence: 1 collected light -> instances=8160 =
				// tiles lit=8160). The old test bracketed EVERY tile with
				// the full depth band [camNear, camFar] and the union AABB
				// of the tile's near-plane sliver and far-plane wedge. That
				// union stretches from ~0 XY at the near plane out to the
				// screen-edge extent at the far plane, so a light a few
				// units in front of the camera sat inside the XY range of
				// nearly every tile's AABB. Intersect the light's OWN depth
				// extent [vz-radius, vz+radius] with the tile's depth band
				// first; an empty intersection (light wholly behind the near
				// plane or beyond the far plane) rejects the tile outright.
				float zLo = vz - radius;
				float zHi = vz + radius;
				if(zLo < tileMinZ) zLo = tileMinZ;
				if(zHi > tileMaxZ) zHi = tileMaxZ;
				if(!(zHi > zLo)) continue;	// empty slab (also rejects NaN vz)

				// Tile XY bounds AT the light's depth band. The tile's
				// frustum cone scales linearly with depth (x = ndc * z /
				// proj), so the exact XY AABB of the cone slice over
				// [zLo, zHi] is the union of the rects at the two endpoints;
				// which endpoint wins depends on the sign of the NDC bound.
				float xLo = (ndcX0 < 0.0f) ? ndcX0 * zHi * invProj00 : ndcX0 * zLo * invProj00;
				float xHi = (ndcX1 > 0.0f) ? ndcX1 * zHi * invProj00 : ndcX1 * zLo * invProj00;
				float yLo = (ndcY0 < 0.0f) ? ndcY0 * zHi * invProj11 : ndcY0 * zLo * invProj11;
				float yHi = (ndcY1 > 0.0f) ? ndcY1 * zHi * invProj11 : ndcY1 * zLo * invProj11;

				// Sphere vs tile frustum-slice AABB test
				if(SphereVsAABB(vx, vy, vz, radius, xLo, yLo, zLo, xHi, yHi, zHi)){
					g_fpStats[FP_STAT_INSTANCES]++;
					// Brightest-first key: intensity / distance-to-slice.
					// Distance 0 (center inside the slice) floors at 1e-3 so
					// the divide can't blow up (1e-7 min guard).
					float dist = AabbDistance(vx, vy, vz, xLo, yLo, zLo, xHi, yHi, zHi);
					float key = light.intensity / ((dist > 1e-3f) ? dist : 1e-3f);
					// Insertion-sort candidates descending by key
					// (stable: equal keys keep atlas order).
					int pos = nCand;
					while(pos > 0 && candKey[pos - 1] < key){
						candKey[pos] = candKey[pos - 1];
						candLight[pos] = candLight[pos - 1];
						pos--;
					}
					candKey[pos] = key;
					candLight[pos] = li;
					nCand++;
				}
			}

			// Per-tile culling diagnostics — the only visibility into the
			// two truncation edges of the scheme (16 slots/tile, 256 atlas
			// texels) without a debugger. Cheap: a few increments per tile.
			if(nCand > 0) g_fpStats[FP_STAT_TILES_LIT]++;
			if(nCand > FP_LIGHTS_PER_TILE){
				g_fpStats[FP_STAT_TILES_FULL]++;
				g_fpStats[FP_STAT_INST_OVER] += nCand - FP_LIGHTS_PER_TILE;
			}

			// Keep the brightest 16 (fixes the old first-4-wins truncation),
			// 0-fill the rest of the block (0 = empty slot).
			unsigned char slots[FP_LIGHTS_PER_TILE];
			memset(slots, 0, sizeof(slots));
			int keep = (nCand < FP_LIGHTS_PER_TILE) ? nCand : FP_LIGHTS_PER_TILE;
			int idx256 = 0;
			for(int s = 0; s < keep; s++){
				int idx1 = candLight[s] + 1; // 1-based atlas index
				// A byte holds 1..255; index 256 of a FULL 256-light list
				// cannot be encoded (256 would wrap to 0 = "empty"). The list
				// is brightness/priority-sorted, so only the globally weakest
				// light can hit this edge — skip it rather than mis-index it.
				if(idx1 > 255){ idx256++; slots[s] = 0; }
				else slots[s] = (unsigned char)idx1;
			}
			g_fpStats[FP_STAT_IDX256] += idx256;

			// Pack the 16 slots into the tile's 2x2 texel block:
			// texel (2x + k%2, 2y + k/2) holds slots 4k..4k+3 in R,G,B,A
			// channel order (k = 0..3). NOTE: D3DFMT_A8R8G8B8 texel bytes in
			// memory are B,G,R,A (the DWORD is 0xAARRGGBB little-endian), so
			// channel R is byte 2 of the texel — the channel map below is
			// what makes tex2D(...).r read slot 4k+0.
			static const int chanByte[4] = { 2, 1, 0, 3 }; // R,G,B,A -> A8R8G8B8 byte offset
			for(int s = 0; s < FP_LIGHTS_PER_TILE; s++){
				int k = s >> 2;
				int c = s & 3;
				int px = (tx << 1) + (k & 1);
				int py = (ty << 1) + (k >> 1);
				g_fpTileData[(py * (FP_GRID_W * 2) + px) * 4 + chanByte[c]] = slots[s];
			}
		}
	}
}

// Upload tile data to GPU texture. Returns false when the write buffer could
// not be locked (device trouble); the caller then zero-fills that buffer so
// SetConstants can never sample stale indices against this frame's atlases —
// the same content the old per-frame zero left behind on a failed upload.
static bool UploadTileTexture(IDirect3DDevice9 *dev)
{
	if(!g_fpIndexTex) return false;
	
	// WRITE side of the double buffer: g_fpCurrentBuffer is the buffer being
	// written THIS frame (CullAndUpload records it as g_fpLastWrittenBuffer
	// right after this call — see the DOUBLE-BUFFER INVARIANT there).
	IDirect3DTexture9 *tex = g_fpCurrentBuffer ? g_fpIndexTexB : g_fpIndexTex;
	
	D3DLOCKED_RECT lr;
	if(FAILED(tex->LockRect(0, &lr, NULL, D3DLOCK_DISCARD))){
		dbglog("[ForwardPlus] LockRect failed");
		return false;
	}
	
	for(int y = 0; y < FP_GRID_H * 2; y++){
		unsigned char *dst = (unsigned char*)((BYTE*)lr.pBits + y * lr.Pitch);
		const unsigned char *src = &g_fpTileData[y * FP_GRID_W * 2 * 4];
		memcpy(dst, src, FP_GRID_W * 2 * 4);
	}
	
	tex->UnlockRect(0);
	return true;
}

// Upload the light atlases (s6 = view-space (pos.xyz, radius), s7 = (r, g, b,
// type)) — replaces the old c48-c111 PS-constant arrays (32-light ceiling).
// Pure D3D9 LockRect work like UploadTileTexture, so it is safe here in
// RenderScene_before (the constant upload is what crashed outside the pipe
// callbacks and moved to SetConstants). Atlas order = g_fpLights order = the
// 1-based indices the tile texture stores. Writes the ping-pong set the
// CURRENT frame owns (g_fpCurrentBuffer), which is exactly the set
// SetConstants binds this frame (g_fpLastWrittenBuffer). Returns false on
// lock failure so the caller can close the c45.w gate — garbage atlases must
// never be sampled against a nonzero lightCount.
static bool UploadLightAtlases(void)
{
	int slot = g_fpCurrentBuffer ? 1 : 0;
	IDirect3DTexture9 *posTex = g_fpPosTex[slot];
	IDirect3DTexture9 *colTex = g_fpColTex[slot];
	if(!posTex || !colTex) return false;

	UINT lockFlags = g_fpAtlasDynamic ? D3DLOCK_DISCARD : 0;
	D3DLOCKED_RECT lrPos, lrCol;
	if(FAILED(posTex->LockRect(0, &lrPos, NULL, lockFlags))){
		dbglog("[ForwardPlus] LockRect pos atlas failed");
		return false;
	}
	if(FAILED(colTex->LockRect(0, &lrCol, NULL, lockFlags))){
		dbglog("[ForwardPlus] LockRect col atlas failed");
		posTex->UnlockRect(0);
		return false;
	}

	// Texel i (i = atlas index = tile-index value - 1) is row-major over the
	// 64-texel rows. 256 lights use the first 4 rows. Channel order in
	// memory for A32B32G32R32F is R,G,B,A floats = exactly the contract
	// tuple written sequentially below.
	int count = g_fpNumLights;
	if(count > FP_MAX_LIGHTS) count = FP_MAX_LIGHTS;
	for(int i = 0; i < count; i++){
		int row = i / FP_ATLAS_TEX;
		int col = i % FP_ATLAS_TEX;
		float *pos = (float*)((BYTE*)lrPos.pBits + row * lrPos.Pitch) + col * 4;
		pos[0] = g_fpViewPos[i][0];   // view-space X (+Z forward convention)
		pos[1] = g_fpViewPos[i][1];   // view-space Y
		pos[2] = g_fpViewPos[i][2];   // view-space Z (positive in front)
		pos[3] = g_fpLights[i].radius;
		float *colf = (float*)((BYTE*)lrCol.pBits + row * lrCol.Pitch) + col * 4;
		colf[0] = g_fpLights[i].r;    // raw color (not pre-multiplied)
		colf[1] = g_fpLights[i].g;
		colf[2] = g_fpLights[i].b;
		colf[3] = g_fpLights[i].type; // 0=point, 1=spot (consumers may ignore)
	}

	posTex->UnlockRect(0);
	colTex->UnlockRect(0);
	return true;
}

// Trusted screen size: the engine's configured video resolution. Unlike a
// live Scene.camera raster read, this is immune to the geometry-phase
// camera-raster swap (measured 2048x1024 while the real screen is 1920x1080 —
// see main.cpp's scene-entry camRas diagnostic). Returns false when RsGlobal
// is not yet populated OR the values are implausible, so the caller can DEFER
// the tile-grid build to the first frame with valid dims instead of locking
// the dims-stability guard onto a transient/garbage size.
static bool FpGetTrustedScreenSize(int *w, int *h)
{
	if(RsGlobal){
		DWORD gw = RsGlobal->MaximumWidth;
		DWORD gh = RsGlobal->MaximumHeight;
		// Plausibility bounds: RsGlobal is a fixed exe address (0xC17040) that
		// is zero until the game's Rs init runs, and a garbage-but-stable value
		// here would permanently mis-size the tile grid (the dims-stability
		// guard in CullAndUpload would treat it as "stable" and never re-sync,
		// culling lights into tiles for the wrong screen area). Anything
		// outside [64, 8192] cannot be a real SA video mode.
		if(gw >= 64 && gw <= 8192 && gh >= 64 && gh <= 8192){
			*w = (int)gw;
			*h = (int)gh;
			return true;
		}
	}
	return false;
}

// Build the view + projection matrices from the GAME CAMERA instead of the
// live D3D transform state. ForwardPlus_CullAndUpload runs in
// RenderScene_before, BEFORE the camera's BeginUpdate writes D3DTS_VIEW /
// D3DTS_PROJECTION for this frame, so dev->GetTransform() there returns the
// PREVIOUS pass's matrices (the reflection/effects passes leave their own
// view/proj live) and lights get culled into the wrong tiles. Derive the
// matrices the pipeline is about to use:
//   view = inverse of the camera frame's world LTM — the repo's established
//          pattern (cf. vehiclePipe.cpp carfx_view = RwMatrixInvert(camLTM);
//          camera `at` is the forward axis and view space is +Z forward, the
//          same convention shaders/ps/HeightFog.hlsl and
//          shaders/include/colorSpace.hlsl ReconstructViewPos assume).
//   proj = standard D3D9 perspective FOV matrix from the camera FOV (RW
//          stores tan(half-FOV) in viewWindow), near/far planes and the
//          viewport aspect.
// Output layout matches dev->GetTransform() (D3D9 row-vector convention:
// basis vectors in matrix ROWS, translation in m[3]) so CullLightsToTiles
// sees the same matrix format the old GetTransform fallback returns.
// Returns false when the camera (or frame/LTM) is unavailable or the
// FOV/clip values are degenerate, so the caller can KEEP the old
// GetTransform fallback and still run the pass this frame.
static bool Fp_BuildViewProjFromCamera(RwCamera *cam, float aspect, D3DMATRIX *viewMat, D3DMATRIX *projMat)
{
	if(!cam || !viewMat || !projMat) return false;

	// --- View: inverse of the camera frame's world LTM ---
	RwFrame *camFrame = RwCameraGetFrame(cam);
	if(!camFrame) return false;
	RwMatrix *camLTM = RwFrameGetLTM(camFrame);
	if(!camLTM) return false;

	RwMatrix inv;
	// RwMatrixInvert also handles non-unit scale in the LTM (a hand-rolled
	// transpose-inverse would not).
	if(!RwMatrixInvert(&inv, camLTM)) return false;

	// RwMatrix -> D3DMATRIX in GetTransform's row-vector layout: right/up/at
	// become matrix rows, pos becomes the translation row (v_view = v_world
	// * viewMat, matching CullLightsToTiles' transform).
	viewMat->m[0][0] = inv.right.x; viewMat->m[0][1] = inv.right.y; viewMat->m[0][2] = inv.right.z; viewMat->m[0][3] = 0.0f;
	viewMat->m[1][0] = inv.up.x;    viewMat->m[1][1] = inv.up.y;    viewMat->m[1][2] = inv.up.z;    viewMat->m[1][3] = 0.0f;
	viewMat->m[2][0] = inv.at.x;    viewMat->m[2][1] = inv.at.y;    viewMat->m[2][2] = inv.at.z;    viewMat->m[2][3] = 0.0f;
	viewMat->m[3][0] = inv.pos.x;   viewMat->m[3][1] = inv.pos.y;   viewMat->m[3][2] = inv.pos.z;   viewMat->m[3][3] = 1.0f;

	// --- Proj: standard D3D9 perspective FOV matrix (row-vector form) ---
	// tan(half-FOV) comes from the camera FOV (viewWindow), the horizontal
	// scale from the viewport aspect.
	float tanHalfFovY = cam->viewWindow.y;
	float n = cam->nearPlane;
	float f = cam->farPlane;
	if(!(tanHalfFovY > 1e-7f) || !(aspect > 1e-7f) || !(n > 1e-7f) || !(f > n + 1e-7f))
		return false;

	memset(projMat, 0, sizeof(D3DMATRIX));
	projMat->m[0][0] = 1.0f / (aspect * tanHalfFovY);
	projMat->m[1][1] = 1.0f / tanHalfFovY;
	projMat->m[2][2] = f / (f - n);
	projMat->m[2][3] = 1.0f;
	projMat->m[3][2] = -n * projMat->m[2][2];
	return true;
}

// Main entry point — call each frame before rendering
void ForwardPlus_CullAndUpload(void)
{
	DBGLOG_ENTER("ForwardPlus_CullAndUpload");

	// LOSSLESS-DEFER RESTRUCTURE (2026-10-25): the drain moved BELOW the
	// transient defer gates. The old code drained at entry and every defer
	// path (dims-stability guard, RsGlobal wait, device/camera wait, texture
	// recreate) then DISCARDED the snapshot — field log 15:06:31.771 shows a
	// 7-light feed drained at entry and thrown away by the first-frame dims
	// defer. Defers now KEEP the buffer: a deferred frame never loses a
	// feed, and multi-frame accumulation while deferred is absorbed by the
	// 0.25m dedup + the 256-light cap at the next successful merge. The
	// feature-disabled path still discards explicitly (no accumulation
	// across a disabled stretch).

	if(!config || !config->forwardPlusEnable){
		// Feature off: discard whatever the feed pushed this frame so the
		// deferred buffer can never accumulate while disabled.
		// MUST NOT write c45 here: this function runs from RenderScene_before,
		// outside the pipe callbacks, where RwD3D9SetPixelShaderConstant derefs
		// null RW shader state -> READ crash at 0x7FBD4A (+0x60) — the exact
		// hazard documented at the SetConstants call site below. With
		// the feature disabled EnsureFPResources never ran, so there is no
		// bound RW pixel shader whose constant cache could take the write.
		// The gate-close zeroing of c45 (and s5/s6/s7 unbind) is done safely by
		// ForwardPlus_SetConstants' disabled branch, which runs inside the
		// frame render before any draw samples c45 — nothing is lost here.
		g_fpAddCount = 0;
		return;
	}
	
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;                   // transient defer — deferred buffer KEPT
	if(!Scene.camera) return;          // transient defer — deferred buffer KEPT

	// Size the tile grid from the engine's trusted resolution (RsGlobal), NOT
	// a live Scene.camera raster read: during the geometry phase the camera
	// raster can be a transient swap (measured 2048x1024 while the real screen
	// is 1920x1080 — see main.cpp's scene-entry camRas diagnostic). A live read
	// here would mis-size the tile grid for the whole session via the
	// dims-stability guard below. When RsGlobal is not yet populated we DEFER
	// (skip the frame) — see the comment inside the branch. (postfx's
	// GetScreenSize/CaptureScreenSize gives the same guarantee but is static
	// to postfx.cpp and not exported; RsGlobal is the same source main.cpp
	// uses to detect the swap.)
	int w = 0, h = 0;
	if(!FpGetTrustedScreenSize(&w, &h)){
		// DEFER, don't fall back to the live camera raster: that live read is
		// exactly what fix-11 removed as a size source — during the
		// geometry-phase camera-raster swap it reports the transient
		// 2048x1024 IBL/classify raster, and the dims-stability guard below
		// would lock the tile grid onto that wrong size for the whole session
		// (lights culled into tiles for the wrong screen area → no cluster
		// lights where the shader looks for them). Skipping the frame is safe:
		// c45.w keeps its previous value and the shader's 7-light path keeps
		// the world lit until RsGlobal is populated (game init always precedes
		// the first RenderScene call, so this is a startup-only window).
		static bool s_fpTrustedWarned = false;
		if(!s_fpTrustedWarned){
			s_fpTrustedWarned = true;
			dbglog("[ForwardPlus] RsGlobal dims not valid yet — deferring tile grid");
		}
		return;                        // transient defer — deferred buffer KEPT
	}
	if(w < 1 || h < 1) return;         // transient defer — deferred buffer KEPT

	// Reset per-frame diagnostics for the rebuild below (the culler and the
	// merge both add to them). Kept from the last successful frame when the
	// pass defers on a later frame, which is what the ImGui row shows.
	memset(g_fpStats, 0, sizeof(g_fpStats));

	// Ensure textures exist
	EnsureFPResources(dev, w, h);
	if(!g_fpIndexTex || !g_fpIndexTexB ||
	   !g_fpPosTex[0] || !g_fpPosTex[1] || !g_fpColTex[0] || !g_fpColTex[1]){
		if(dbglog_first("fp_res_missing"))
			dbglog("[ForwardPlus] tile/atlas textures unavailable — cluster lights off this session");
		return;                        // transient defer — deferred buffer KEPT
	}

	// Dims-stability guard: when camera dims change, run EnsureFPResources
	// (done above) but skip collect/cull/upload/swap until dims repeat.
	// NOTE: the tile grid itself is NOT cached — CullLightsToTiles recomputes
	// tilesX/tilesY and refills g_fpTileData from the CURRENT w/h every frame,
	// so once real dims are known the grid is rebuilt correctly with no
	// stale-size carryover (the index textures are fixed 256x256 texels /
	// 128x128 tiles and resolution-independent).
	static int s_fpLastW = 0;
	static int s_fpLastH = 0;
	static int s_fpStableCount = 0;
	if(w != s_fpLastW || h != s_fpLastH) {
		s_fpLastW = w;
		s_fpLastH = h;
		s_fpStableCount = 1;
		// Breadcrumb (dig aid): this defer window is where "gpu=0" startup
		// lines come from — one log per process so a dig can tell the
		// resolution-change defer apart from a dead collection path.
		if(dbglog_first("fp_dims_defer"))
			dbglog("[ForwardPlus] screen dims %dx%d — stability guard defers 2 frames", w, h);
		// Old per-frame zeroing blanked the bound index buffer on defer frames
		// too (see FpInvalidateBoundTileTexture); keep that exact content.
		FpInvalidateBoundTileTexture();
		return;                        // transient defer — deferred buffer KEPT
	}
	if(s_fpStableCount < 2) {
		s_fpStableCount++;
		FpInvalidateBoundTileTexture();
		return;                        // transient defer — deferred buffer KEPT
	}

	// All transient gates passed — NOW drain the per-frame
	// ForwardPlus_AddLight buffer (snapshot the count; the merge below runs
	// after CollectLights). Callers push one frame's lights before this
	// call; a push AFTER the drain (RenderEffects-entry feed slot) rides
	// the NEXT drain — the documented one-frame latency.
	int addN = g_fpAddCount;
	g_fpAddCount = 0;
	int drainRaw = addN;               // pre-dedup count (fp_path audit)

	// Get view/proj matrices from the GAME CAMERA, not the D3D transform
	// state: this runs in RenderScene_before, BEFORE the camera's BeginUpdate
	// writes D3DTS_VIEW/PROJECTION for this frame, so dev->GetTransform()
	// returns the PREVIOUS pass's matrices (the reflection/effects passes
	// leave their own view/proj live) and lights land in wrong tiles. When
	// the camera (or its FOV/clip values) is unavailable this frame, KEEP the
	// old GetTransform fallback rather than skipping the pass.
	D3DMATRIX viewMat, projMat;
	if(!Fp_BuildViewProjFromCamera(Scene.camera, (float)w / (float)h, &viewMat, &projMat)){
		dev->GetTransform(D3DTS_VIEW, &viewMat);
		dev->GetTransform(D3DTS_PROJECTION, &projMat);
	}

	// Depth slabs must bracket the lights' actual view-space Z and match the
	// proj matrix used for the tile XY unprojection. View space is +Z forward
	// (Fp_BuildViewProjFromCamera), so scene depth lives in [+near, +far].
	// Prefer the camera's real clip planes (a hardcoded 500 far would drop
	// lights past 500 and under-size the far-plane tile XY bounds); keep the
	// old conservative defaults only when the camera values are degenerate.
	float camNear = 0.5f;
	float camFar = 500.0f;  // GTA SA draw distance
	if(Scene.camera->nearPlane > 1e-7f &&
	   Scene.camera->farPlane > Scene.camera->nearPlane + 1e-7f &&
	   Scene.camera->farPlane <= 10000.0f){
		camNear = Scene.camera->nearPlane;
		camFar = Scene.camera->farPlane;
	}

	// Debug view selection (INI-gated; static/stat based, no main.cpp touch)
	FpUpdateDebugView();

	// Rebuild the unified light list: CollectLights is now a count-only
	// diagnostic (the registry is freshly cleared at this point — see its
	// comment); the actual light data arrives through the drained
	// ForwardPlus_AddLight buffer (fed by main.cpp's coronaFeedForwardPlus
	// slot from the FULLY populated registry + corona pool), position-deduped
	// and merged into ONE unified, brightest-sorted list (atlas order =
	// tile-index order).
	int capDrops = 0;
	int replayN = 0;
	CollectLights();   // cheap count-only diagnostic — kept OUTSIDE the skip

	// PERF R6 — cheap fallback pre-check BEFORE the full rebuild. The unified
	// list is built purely from the drained AddLight buffer (+ the snapshot
	// replay on a starved drain; CollectLights copies nothing). So when the
	// drain arrived empty AND no fresh snapshot can replay, g_fpNumLights
	// stays 0, every tile ends unlit, and — with the fallback kill-switch on —
	// the frame is a legacy 7-light frame no matter what the rebuild computes.
	// Measured: the fallback fired 361/368 windows with all-zero counts, i.e.
	// the full sort/merge/view-transform/cull ran for nothing almost every
	// frame. Skip it (stats stay memset-zero, which is exactly what an empty
	// frame produced before) and jump straight to the fallback bookkeeping.
	// Any frame with lights (addN > 0 or a fresh snapshot) takes the full
	// rebuild below — byte-identical behaviour to the old ordering.
	bool fpFreshSnapshot = g_fpRegSnapshotCount > 0 &&
		(DWORD)(GetTickCount() - g_fpRegSnapshotTick) < FP_SNAPSHOT_TTL_MS;  // unsigned: wrap-safe
	bool fpFastFallback = FpFallbackEnabled() && addN == 0 && !fpFreshSnapshot;

	if(!fpFastFallback){
	{
	PF_SCOPE("FP.rebuild");
	FpSortLightsByIntensity();
	// Camera world position (for the near-light priority of FpMergeAddedLights)
	float camX = viewMat.m[3][0], camY = viewMat.m[3][1], camZ = viewMat.m[3][2];
	addN = FpDedupAddLights(g_fpAddLights, addN);
	capDrops = FpMergeAddedLights(g_fpAddLights, addN, camX, camY, camZ);
	// Starved-drain backstop: the buffer arrived empty but the most recent
	// feed saw a populated registry (g_fpRegSnapshot — see the block comment
	// at the snapshot declaration). Replay it so a populated registry can
	// never produce a collected=0 frame merely because the one-push-per-
	// frame latch left this particular drain unfed. Only fires on a truly
	// empty buffer, so fed frames stay byte-identical to the old behaviour.
	if(addN == 0 && g_fpRegSnapshotCount > 0){
		DWORD snapAge = GetTickCount() - g_fpRegSnapshotTick;   // unsigned: wrap-safe
		if(snapAge < FP_SNAPSHOT_TTL_MS){
			replayN = FpDedupAddLights(g_fpRegSnapshot, g_fpRegSnapshotCount);
			capDrops += FpMergeAddedLights(g_fpRegSnapshot, replayN, camX, camY, camZ);
		}
	}
	g_fpStats[FP_STAT_CAP_DROPS] = capDrops;
	} // PF_SCOPE: rebuild

	// World -> view ONCE per light (row-vector v_world * viewMat, +Z
	// forward): consumed by CullLightsToTiles (sphere-vs-AABB) and the s6
	// atlas (contract: s6 stores VIEW-space positions so the shader skips a
	// transform). The view transform is rigid for unscaled camera frames, so
	// the world-space radius carries over unchanged.
	for(int i = 0; i < g_fpNumLights; i++){
		const ClusterLight &l = g_fpLights[i];
		g_fpViewPos[i][0] = viewMat.m[0][0]*l.x + viewMat.m[1][0]*l.y + viewMat.m[2][0]*l.z + viewMat.m[3][0];
		g_fpViewPos[i][1] = viewMat.m[0][1]*l.x + viewMat.m[1][1]*l.y + viewMat.m[2][1]*l.z + viewMat.m[3][1];
		g_fpViewPos[i][2] = viewMat.m[0][2]*l.x + viewMat.m[1][2]*l.y + viewMat.m[2][2]*l.z + viewMat.m[3][2];
	}

	{
	PF_SCOPE("FP.cull");
	// Cull to tiles
	CullLightsToTiles(w, h, projMat, camNear, camFar);
	} // PF_SCOPE: cull
	} // !fpFastFallback: full rebuild + cull skipped on the empty fast path

	// LEGACY-LIGHT FALLBACK DECISION (see the block comment at
	// FpFallbackEnabled): the tile list is empty when ZERO tiles lit — that
	// covers "fed 0 (no lights registered near)" and "fed N but every light
	// culled out" (instances would be 0 too). Either way the cluster path
	// can contribute nothing this frame, so it must ride the legacy 7-light
	// path instead of a live gate over empty atlases.
	// PERF R6: the "fed 0" case is decided by fpFastFallback above, before
	// the rebuild; the "fed N but all culled out" case still comes from the
	// post-cull tilesLit check.
	g_fpFallbackFrame = fpFastFallback ||
		(FpFallbackEnabled() && (g_fpStats[FP_STAT_TILES_LIT] == 0));
	if(g_fpFallbackFrame){
		g_fpGpuLightCount = 0;   // nothing bindable — c45 gate stays closed
		if(dbglog_throttle("fp_fallback"))
			dbglog("[ForwardPlus] fp_fallback: tile list empty (collected=%d added=%d rawGame=%d tilesLit=0 inst=%d) — legacy 7-light path this frame",
				g_fpNumLights, addN, g_fpRawCount, g_fpStats[FP_STAT_INSTANCES]);
	}

	{
	PF_SCOPE("FP.upload");
	// Upload to GPU — write into buffer[cur] and record it as
	// g_fpLastWrittenBuffer BEFORE flipping, so SetConstants (called later
	// from render callbacks) binds EXACTLY the buffer written this frame:
	// tile indices (frame N) and light atlases (frame N) stay matched
	// (DOUBLE-BUFFER INVARIANT at g_fpLastWrittenBuffer, now covering the
	// s5 index texture AND the s6/s7 atlas sets together).
	// NOTE (2026-09-14 crash fix): constant uploads use
	// RwD3D9SetPixelShaderConstant, which is only valid inside the frame
	// render (pipe callbacks). Calling it here in RenderScene_before
	// derefs null RW state -> READ crash at 0x7FBD4A (+0x60). Tile/atlas
	// texture upload is pure D3D9 and safe here; c45 moves to SetConstants.
	// FALLBACK frames skip the upload entirely: SetConstants takes its
	// disabled branch (nothing binds s5-s7), so the DISCARD locks would
	// only overwrite the write buffer with data no draw samples. The
	// lastWritten/flip bookkeeping below still runs, so the next frame
	// that really uploads re-establishes the same-frame invariant.
	if(!g_fpFallbackFrame){
		if(!UploadTileTexture(dev))
			FpZeroIndexTexture(g_fpCurrentBuffer ? g_fpIndexTexB : g_fpIndexTex);
		if(!UploadLightAtlases())
			g_fpGpuLightCount = 0; // no atlas data — close the c45.w gate
		else
			g_fpGpuLightCount = g_fpNumLights;
	}
	} // PF_SCOPE: upload

	// Light-feed breadcrumb (dig aid): "added 0" in the line below means the
	// per-frame ForwardPlus_AddLight feed (main.cpp coronaFeedForwardPlus
	// slot: CPointLights registry + corona pool) pushed nothing this frame.
	// Log the FIRST frame the feed delivers so a log dig can verify the
	// FeedGameLights -> AddLight -> merge -> atlas pipe end-to-end.
	if(addN > 0 && dbglog_first("fp_feed_alive"))
		dbglog("[ForwardPlus] AddLight feed alive: %d lights delivered this frame", addN);
	if(dbglog_throttle("fp"))
		dbglog("[ForwardPlus] collected %d lights (added %d), gpu=%d rawGameCount=%d capDrops=%d",
			g_fpNumLights, addN, g_fpGpuLightCount, g_fpRawCount, capDrops);
	int fpGridX = (w + FP_TILE_SIZE - 1) / FP_TILE_SIZE; if(fpGridX > FP_GRID_W) fpGridX = FP_GRID_W;
	int fpGridY = (h + FP_TILE_SIZE - 1) / FP_TILE_SIZE; if(fpGridY > FP_GRID_H) fpGridY = FP_GRID_H;
	if(dbglog_throttle("fp_stats"))
		dbglog("[ForwardPlus] collected=%d added=%d feedRaw=%d rawGame=%d tiles lit=%d full(16 cap)=%d instances=%d overflow=%d capDrops=%d idx256=%d maxn=%d grid=%dx%d",
			g_fpNumLights, addN, g_fpFeedRawReg, g_fpRawCount,
			g_fpStats[FP_STAT_TILES_LIT], g_fpStats[FP_STAT_TILES_FULL],
			g_fpStats[FP_STAT_INSTANCES], g_fpStats[FP_STAT_INST_OVER],
			g_fpStats[FP_STAT_CAP_DROPS], g_fpStats[FP_STAT_IDX256],
			s_fpDedupMaxN, fpGridX, fpGridY);

	// End-to-end path audit (fp_path): ONE throttled line tying the FEED
	// side to the DRAIN side so a log dig can pair [CoronaFeed] regFed with
	// what the cull actually consumed:
	//   pushed    = registry entries the most recent feed slot pushed
	//               (same frame as this drain for the sky-phase slot, which
	//               runs before it; previous frame for the RenderEffects slot)
	//   drainRaw  = deferred-buffer entries consumed pre-dedup (0 = starved
	//               drain — the snapshot backstop fed the cull instead)
	//   dedup     = buffer entries after the 0.25m dedup (merge input)
	//   collected = unified list after merge (atlas/tile source of truth)
	//   fallback  = 1 when the tile list ended empty (legacy 7-light frame)
	//   replay    = snapshot entries merged on a starved drain (0 normally)
	if(dbglog_throttle("fp_path"))
		dbglog("[ForwardPlus] path: pushed=%d drainRaw=%d dedup=%d collected=%d gpu=%d fallback=%d replay=%d feedRaw=%d rawGame=%d",
			g_fpFeedPushed, drainRaw, addN, g_fpNumLights, g_fpGpuLightCount,
			(int)g_fpFallbackFrame, replayN, g_fpFeedRawReg, g_fpRawCount);

	g_fpLastWrittenBuffer = g_fpCurrentBuffer;
	// Flip AFTER recording lastWritten: the next frame's LockRect(DISCARD)
	// then targets the buffer that is NOT being sampled this frame.
	g_fpCurrentBuffer = 1 - g_fpCurrentBuffer;
}

// Called from vehicle/building pipe render callbacks to bind cluster texture
void ForwardPlus_SetConstants(void)
{
	PF_SCOPE("FP.setConstants");
	IDirect3DDevice9 *dev = d3d9device;
	// g_fpFallbackFrame: the tile light list was empty this frame (see the
	// LEGACY-LIGHT FALLBACK block comment) — take the SAME disabled branch
	// forwardPlusEnable=0 takes (c45 = 0, s5/s6/s7 unbound) so every shader
	// falls back to the legacy 7-light c5-c11/c12-c18 registers for this
	// frame instead of sampling empty atlases behind a live gate.
	if(!config || !config->forwardPlusEnable || g_fpFallbackFrame || !g_fpIndexTex || !g_fpIndexTexB ||
	   !g_fpPosTex[0] || !g_fpPosTex[1] || !g_fpColTex[0] || !g_fpColTex[1]){
		// Defensive: clear stale tile gate (c45) and cluster textures
		// (s5/s6/s7) so shaders fall back to 7-light path instead of
		// magnifying garbage.
		if(dev){
			float zeroParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(45, zeroParams, 1);
			dev->SetTexture(5, NULL);
			dev->SetTexture(6, NULL);
			dev->SetTexture(7, NULL);
		}
		return;
	}
	if(!dev) return;

	// Upload ONLY c45 (tileSizePx, gridW, gridH, lightCount). The old
	// c48-c111 light constants are gone — light data now rides the s6/s7
	// atlases (256 lights instead of 32). Valid here because pipe callbacks
	// run inside the frame render where RW shader state exists.
	float clusterParams[4] = {
		(float)FP_TILE_SIZE, (float)FP_GRID_W, (float)FP_GRID_H,
		(float)g_fpGpuLightCount
	};
	RwD3D9SetPixelShaderConstant(45, clusterParams, 1);

	// Bind index texture on s5 with POINT filtering — the buffer CullAndUpload
	// wrote THIS frame (g_fpLastWrittenBuffer), NOT the flipped
	// g_fpCurrentBuffer (that is the OTHER buffer: frame N-1 tile data).
	// Binding lastWritten keeps the tile indices and the light atlases
	// uploaded above from the same frame (DOUBLE-BUFFER INVARIANT at
	// g_fpLastWrittenBuffer). -1 = nothing written yet (startup frames where
	// the dims-stability guard deferred the upload): both buffers are
	// zero-init'd there, so fall back to buffer A.
	int bindBuf = (g_fpLastWrittenBuffer >= 0) ? g_fpLastWrittenBuffer : 0;
	IDirect3DTexture9 *tex = bindBuf ? g_fpIndexTexB : g_fpIndexTex;
	dev->SetTexture(5, tex);
	// Atlas ping-pong set of the SAME frame as the bound tile texture — the
	// 1-based indices in s5 index into THIS set's texels (DOUBLE-BUFFER
	// INVARIANT now covers s5, s6 and s7 together).
	dev->SetTexture(6, g_fpPosTex[bindBuf]);
	dev->SetTexture(7, g_fpColTex[bindBuf]);

	// Sampler states are feature-owned on slots 5-7 (same policy the s5 code
	// always had): POINT/CLAMP must stay set for the draws that sample the
	// atlases, so they are not save/restored here — restoring would flip the
	// filtering before the draw. The disabled branch above unbinds all three
	// textures when the feature is off. No other render state is touched.
	static const int fpSamplers[3] = { 5, 6, 7 };
	for(int si = 0; si < 3; si++){
		int s = fpSamplers[si];
		dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}
}

// ============================================================================
// DEBUG OVERLAY — cluster/tile visualization (INI forwardPlusDebug)
//
// Own-lane, CPU-only visualization of the tile grid the culler built: one
// translucent quad per 16x16 px tile, tinted by how many of the 16 slots it
// holds (dark = culled empty, cold = 1, green = 4, yellow = 8, orange = 12,
// red = 16), with a solid stroke on tiles pinned at the 16-light cap so the
// truncation edge is visible without reading the log. PM/VM = the module's
// own g_fpTileData (the same array UploadTileTexture copies to s5), so the
// overlay is a TRUE picture of what the shader sees.
//
// Dispatching: call from the POST-RENDER stage (after the scene, before the
// next frame's cull). STAGED one-liner for postfx.cpp is in the report —
// that file is another lane's. Behaviour is identical with
// forwardPlusDebug=0 (default): the function returns before touching D3D.
// All render state it sets is saved and restored.
// ============================================================================
#define FP_DBG_FVF   (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
#define FP_DBG_MAXQ  (FP_GRID_W * FP_GRID_H)
#define FP_DBG_POOLS (FP_GRID_W * FP_GRID_H + 16)

struct FpDbgVertex {
	float x, y, z, rhw;
	D3DCOLOR   diffuse;
};

static IDirect3DVertexBuffer9 *g_fpDbgVB   = NULL;   // grid quads (DYNAMIC)
static IDirect3DVertexBuffer9 *g_fpDbgLeg  = NULL;   // legend quads (DYNAMIC)
static int   g_fpDbgTilesX = 0, g_fpDbgTilesY = 0, g_fpDbgW = 0, g_fpDbgH = 0;
static float g_fpDbgTileX = 16.0f, g_fpDbgTileY = 16.0f;

// Build one TRANSLUCENT screen quad for a tile. `repeats` = number of
// NON-EMPTY slots that tile holds (0..16), which is the one property of the
// cull result the overlay can show per tile without extra state. `mark` = 1
// paints the tile as pinned-at-cap (all 16 slots used).
static void FpDbgQuad(FpDbgVertex *v, float x0, float y0, float x1, float y1,
                      int mark, int repeats)
{
	D3DCOLOR c;
	if(mark)
		c = D3DCOLOR_ARGB(255, 255, 0, 0);          // full tile: red stroke
	else{
		// Count ladder — 0-light tiles stay invisible so lit tiles read first.
		static const int lad[5][3] = {
			{  0,   0,   0},   // empty (invisible)
			{  0,  80, 200},   // 1 light
			{  0, 190, 110},   // 4 lights
			{225, 200,  30},   // 8 lights
			{225, 110,  20},   // 12 lights
		};
		int li = (repeats >= 12) ? 4 : (repeats >= 8) ? 3 : (repeats >= 4) ? 2 : (repeats >= 1) ? 1 : 0;
		c = D3DCOLOR_ARGB(li ? 110 : 0, lad[li][0], lad[li][1], lad[li][2]);
	}
	v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].diffuse = c;
	v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].diffuse = c;
	v[2].x = x0; v[2].y = y1; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].diffuse = c;
	v[3].x = x1; v[3].y = y1; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].diffuse = c;
}

// Slot value at tile `tile` (pointer at the tile's first texel, 2x2 block).
// A8R8G8B8 bytes in memory are B,G,R,A, so the R channel is byte +2 — the
// same map CullLightsToTiles writes with and the shader reads back.
static inline int FpDbgSlot(const unsigned char *tile, int s)
{
	static const int chanByte[4] = { 2, 1, 0, 3 };  // R,G,B,A -> byte offset
	int k = s >> 2, c = s & 3;
	int px = (k & 1), py = (k >> 1);               // texel inside the 2x2 block
	return tile[(py * (FP_GRID_W * 2) + px) * 4 + chanByte[c]];
}

static void FpDbgReleaseVBs(void)
{
	if(g_fpDbgVB){ g_fpDbgVB->Release(); g_fpDbgVB = NULL; }
	if(g_fpDbgLeg){ g_fpDbgLeg->Release(); g_fpDbgLeg = NULL; }
}

// One-line culling statistics for the debug menu / ImGui (see header). Pass
// timings live in the profiler's PF_SCOPE timeline ("FP.rebuild" / "FP.cull"
// / "FP.upload" / "FP.setConstants") — that is where the user reads them.
void ForwardPlus_StatsText(char *out, int outSize)
{
	if(!out || outSize < 1) return;
	_snprintf(out, outSize,
		"FP lights: gpu=%d/%d  tiles: %d lit / %d capped(16)  cull: %d inst, %d over, capDrops=%d, idx256=%d",
		g_fpGpuLightCount, FP_MAX_LIGHTS,
		g_fpStats[FP_STAT_TILES_LIT], g_fpStats[FP_STAT_TILES_FULL],
		g_fpStats[FP_STAT_INSTANCES], g_fpStats[FP_STAT_INST_OVER],
		g_fpStats[FP_STAT_CAP_DROPS], g_fpStats[FP_STAT_IDX256]);
	out[outSize - 1] = '\0';
}

void ForwardPlus_DrawClusterDebug(void)
{
	// Refresh BEFORE the gate: with config authoritative this is a plain int
	// compare (the old stat()-per-frame ini probe is gone), so the menu
	// checkbox turns the overlay on in the same frame it is clicked instead
	// of waiting for the next CullAndUpload.
	FpUpdateDebugView();
	if(!g_fpDbgView) return;
	// Fallback frames run the legacy 7-light path — the tile overlay would
	// picture an empty grid the shader is not sampling; treat as off.
	if(!config || !config->forwardPlusEnable || g_fpFallbackFrame) return;   // shader gate is closed

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;

	// Tile extents: prefer the device viewport (what the shader's VPOS math
	// actually sees); fall back to the trusted RsGlobal resolution when the
	// viewport read fails or reports a degenerate size.
	int vw = 0, vh = 0;
	D3DVIEWPORT9 vp;
	if(SUCCEEDED(dev->GetViewport(&vp)) && vp.Width >= 1 && vp.Height >= 1){
		vw = (int)vp.Width;
		vh = (int)vp.Height;
	}
	if(vw < 1 || vh < 1)
		FpGetTrustedScreenSize(&vw, &vh);
	if(vw < 1 || vh < 1) return;

	int tilesX = (vw + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	int tilesY = (vh + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	if(tilesX < 1 || tilesY < 1 || tilesX > FP_GRID_W || tilesY > FP_GRID_H) return;

	// Static buffers (WRITEONLY + DISCARD so a full-screen lock of 8k quads
	// stays cheap); recreated on reset by ForwardPlus_ReleaseResources.
	if(!g_fpDbgVB || !g_fpDbgLeg){
		FpDbgReleaseVBs();
		if(FAILED(dev->CreateVertexBuffer(FP_DBG_MAXQ * 4 * sizeof(FpDbgVertex),
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, FP_DBG_FVF, D3DPOOL_DEFAULT, &g_fpDbgVB, NULL))){
			if(dbglog_first("fp_dbg_vb"))
				dbglog("[ForwardPlus] debug overlay: CreateVertexBuffer grid failed");
			return;
		}
		if(FAILED(dev->CreateVertexBuffer(FP_DBG_POOLS * 4 * sizeof(FpDbgVertex),
				D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, FP_DBG_FVF, D3DPOOL_DEFAULT, &g_fpDbgLeg, NULL))){
			if(dbglog_first("fp_dbg_leg"))
				dbglog("[ForwardPlus] debug overlay: CreateVertexBuffer legend failed");
			g_fpDbgVB->Release(); g_fpDbgVB = NULL;
			return;
		}
	}
	g_fpDbgTilesX = tilesX; g_fpDbgTilesY = tilesY;
	g_fpDbgW = vw; g_fpDbgH = vh;

	float tw = (float)vw / (float)tilesX;
	float th = (float)vh / (float)tilesY;
	if(!(tw > 1.0f) || !(th > 1.0f)) return;
	if(g_fpDbgTileX != tw || g_fpDbgTileY != th){
		// Out of the FP_TILE_SIZE band (16 px +/- 15%): our quad grid would no
		// longer match the shader's floor(vpos / 16) binning. Skip the draw
		// rather than paint a misleading picture.
		if(tw < FP_TILE_SIZE * 0.85f || tw > FP_TILE_SIZE * 1.15f ||
		   th < FP_TILE_SIZE * 0.85f || th > FP_TILE_SIZE * 1.15f){
			if(dbglog_throttle("fp_dbg_tile"))
				dbglog("[ForwardPlus] debug overlay skipped: tile %.1fx%.1f px != %d px cull grid",
					tw, th, FP_TILE_SIZE);
			return;
		}
		g_fpDbgTileX = tw; g_fpDbgTileY = th;
	}

	// ---- grid quads: one per tile, count ladder + full-tile stroke ----
	int drawn = 0;
	FpDbgVertex *v;
	if(FAILED(g_fpDbgVB->Lock(0, 0, (void**)&v, D3DLOCK_DISCARD))){
		dbglog("[ForwardPlus] debug overlay: Lock grid VB failed");
		return;
	}
	for(int ty = 0; ty < tilesY; ty++){
		for(int tx = 0; tx < tilesX; tx++){
			if(drawn >= FP_DBG_MAXQ) break;
			// Tile's first texel inside the 2x2 block for tile (tx, ty)
			const unsigned char *tile =
				&g_fpTileData[(ty * 2) * (FP_GRID_W * 2) * 4 + (tx * 2) * 4];
			int n = 0;
			for(int s = 0; s < FP_LIGHTS_PER_TILE; s++)
				if(FpDbgSlot(tile, s)) n++;
			float x0 = (float)tx * tw, y0 = (float)ty * th;
			FpDbgQuad(v + drawn * 4, x0, y0, x0 + tw, y0 + th,
			          (n == FP_LIGHTS_PER_TILE) ? 1 : 0, n);
			drawn++;
		}
	}
	g_fpDbgVB->Unlock();
	if(!drawn) return;

	// ---- legend quads (mode >= 2): 1 / 4 / 8 / 12+ light swatches ----
	int nleg = 0;
	if(g_fpDbgView >= FP_DBG_LEGEND){
		FpDbgVertex *lv;
		if(SUCCEEDED(g_fpDbgLeg->Lock(0, 0, (void**)&lv, D3DLOCK_DISCARD))){
			static const int legBands[4] = { 1, 4, 8, 12 };
			static const int legRgb[4][3] = {
				{  0,  80, 200 },   // 1
				{  0, 190, 110 },   // 4
				{ 225, 200, 30 },   // 8
				{ 225, 110, 20 },   // 12+
			};
			for(int li = 0; li < 4 && nleg < FP_DBG_POOLS; li++){
				D3DCOLOR c = D3DCOLOR_ARGB(220, legRgb[li][0], legRgb[li][1], legRgb[li][2]);
				int vi = nleg * 4;
				float ly0 = 12.0f + 18.0f * li, ly1 = ly0 + 14.0f, lx0 = 12.0f, lx1 = 30.0f;
				lv[vi+0].x = lx0; lv[vi+0].y = ly0; lv[vi+0].z = 0.0f; lv[vi+0].rhw = 1.0f; lv[vi+0].diffuse = c;
				lv[vi+1].x = lx1; lv[vi+1].y = ly0; lv[vi+1].z = 0.0f; lv[vi+1].rhw = 1.0f; lv[vi+1].diffuse = c;
				lv[vi+2].x = lx0; lv[vi+2].y = ly1; lv[vi+2].z = 0.0f; lv[vi+2].rhw = 1.0f; lv[vi+2].diffuse = c;
				lv[vi+3].x = lx1; lv[vi+3].y = ly1; lv[vi+3].z = 0.0f; lv[vi+3].rhw = 1.0f; lv[vi+3].diffuse = c;
				nleg++;
			}
			(void)legBands;
			g_fpDbgLeg->Unlock();
		}
	}

	// ---- draw: fixed-function XYZRHW quads, alpha blended, Z off ----
	// Every state the following draws could inherit is saved and restored;
	// nothing the rest of the frame needs is left modified.
	DWORD srLighting, srZEnable, srZWrite, srAlphaBlend, srSrcBlend, srDstBlend;
	DWORD srCull, srFog, srStencil, srAlphaTest, srAlphaRef, srAlphaFunc, srSpecular;
	DWORD srColorOp, srColorArg1, srColorArg2, srAlphaOp, srAlphaArg1, srAlphaArg2;
	dev->GetRenderState(D3DRS_LIGHTING, &srLighting);
	dev->GetRenderState(D3DRS_ZENABLE, &srZEnable);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &srZWrite);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &srAlphaBlend);
	dev->GetRenderState(D3DRS_SRCBLEND, &srSrcBlend);
	dev->GetRenderState(D3DRS_DESTBLEND, &srDstBlend);
	dev->GetRenderState(D3DRS_CULLMODE, &srCull);
	dev->GetRenderState(D3DRS_FOGENABLE, &srFog);
	dev->GetRenderState(D3DRS_STENCILENABLE, &srStencil);
	dev->GetRenderState(D3DRS_ALPHATESTENABLE, &srAlphaTest);
	dev->GetRenderState(D3DRS_ALPHAREF, &srAlphaRef);
	dev->GetRenderState(D3DRS_ALPHAFUNC, &srAlphaFunc);
	dev->GetRenderState(D3DRS_SPECULARENABLE, &srSpecular);
	dev->GetTextureStageState(0, D3DTSS_COLOROP, &srColorOp);
	dev->GetTextureStageState(0, D3DTSS_COLORARG1, &srColorArg1);
	dev->GetTextureStageState(0, D3DTSS_COLORARG2, &srColorArg2);
	dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &srAlphaOp);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &srAlphaArg1);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG2, &srAlphaArg2);

	// Save the pre-draw vertex/pixel stage through the device (Get* AddRefs —
	// released after the restore below). The previous raw binds bypassed RW's
	// value-compare caches (_rwD3D9Last*Used, 0x8E2440-0x8E2450): a later RW
	// rebind of the same VS/FVF/stream no-oped against the stale cache, so the
	// rest of the frame drew with the debug overlay's VS/FVF/stream bindings.
	// Capture the device side, then restore through the cache-updating RW
	// wrappers below.
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DVertexDeclaration9 *prevDecl = NULL;
	IDirect3DVertexBuffer9 *prevVB = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	DWORD prevFVF = 0;
	UINT prevVBOffset = 0, prevVBStride = 0;
	dev->GetVertexShader(&prevVS);
	dev->GetVertexDeclaration(&prevDecl);
	dev->GetFVF(&prevFVF);
	dev->GetStreamSource(0, &prevVB, &prevVBOffset, &prevVBStride);
	dev->GetPixelShader(&prevPS);

	// Fixed-function is the only guaranteed-available path on this SM3.0-only
	// build (the live RW pipeline shader is unknown), so bind no PS/VS and let
	// the FVF drive the vertex transform. Bind through the RW wrappers so the
	// driver caches follow the device instead of going stale.
	_rwD3D9SetPixelShader(NULL);
	_rwD3D9SetVertexShader(NULL);
	RwD3D9SetFVF(FP_DBG_FVF);
	dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	// Z off — all three layers (vegImposter.cpp:544-551 / depthhook.cpp:448-453
	// doctrine). The old raw-only write left the rw cache + driver pending[]
	// holding stale TRUEs while the device went FALSE: a later vanilla
	// RwRenderStateSet(ZTEST, TRUE) no-oped against the unchanged cache and the
	// device kept Z off — the exact mechanism that made coronas draw through
	// geometry. L1 first (real cache transition re-pushes L2+L3), then the
	// explicit L2/L3 re-asserts defeat pending[]/applied[] gating.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	// Vertex DIFFUSE straight through (no modulation by a bound texture)
	dev->SetTexture(0, NULL);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);

	RwD3D9SetStreamSource(0, g_fpDbgVB, 0, sizeof(FpDbgVertex));
	dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, drawn * 2);
	if(nleg > 0){
		RwD3D9SetStreamSource(0, g_fpDbgLeg, 0, sizeof(FpDbgVertex));
		dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, nleg * 2);
	}

	// Restore the saved vertex/pixel stage through the same RW wrappers: each
	// call re-syncs both the device and the _rwD3D9Last*Used caches, so the
	// next pipeline's value-compare rebind sees the pre-draw state instead of
	// a cache still naming the debug bindings. FVF and vertex declaration are
	// mutually exclusive on the device — restore whichever mode prevDecl says.
	_rwD3D9SetVertexShader(prevVS);
	if(prevDecl)
		RwD3D9SetVertexDeclaration(prevDecl);
	else
		RwD3D9SetFVF(prevFVF);
	RwD3D9SetStreamSource(0, prevVB, prevVBOffset, prevVBStride);
	_rwD3D9SetPixelShader(prevPS);
	if(prevVS) prevVS->Release();
	if(prevDecl) prevDecl->Release();
	if(prevVB) prevVB->Release();
	if(prevPS) prevPS->Release();

	dev->SetRenderState(D3DRS_LIGHTING, srLighting);
	dev->SetRenderState(D3DRS_SPECULARENABLE, srSpecular);
	// Z restore — all three layers, mirror of the set above. srZEnable/srZWrite
	// were read from the raw device at entry, so pushing them through the rw
	// cache + driver cache + raw device re-syncs every layer to the pre-draw
	// value (the old L3-only restore left cache!=device). L1 values normalized
	// to TRUE/FALSE like DepthHook_Restore (depthhook.cpp:481) so the rw cache
	// never holds a non-boolean raw DWORD.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(srZEnable ? TRUE : FALSE));
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(srZWrite ? TRUE : FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, srZEnable);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, srZWrite);
	dev->SetRenderState(D3DRS_ZENABLE, srZEnable);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, srZWrite);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, srAlphaBlend);
	dev->SetRenderState(D3DRS_SRCBLEND, srSrcBlend);
	dev->SetRenderState(D3DRS_DESTBLEND, srDstBlend);
	dev->SetRenderState(D3DRS_CULLMODE, srCull);
	dev->SetRenderState(D3DRS_FOGENABLE, srFog);
	dev->SetRenderState(D3DRS_STENCILENABLE, srStencil);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, srAlphaTest);
	dev->SetRenderState(D3DRS_ALPHAREF, srAlphaRef);
	dev->SetRenderState(D3DRS_ALPHAFUNC, srAlphaFunc);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, srColorOp);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, srColorArg1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG2, srColorArg2);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, srAlphaOp);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, srAlphaArg1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, srAlphaArg2);

	if(dbglog_throttle("fp_dbg_draw"))
		dbglog("[ForwardPlus] debug overlay: %d tiles (%dx%d grid, %dx%d view), lit=%d full=%d instances=%d overflow=%d",
			drawn, tilesX, tilesY, vw, vh,
			g_fpStats[FP_STAT_TILES_LIT], g_fpStats[FP_STAT_TILES_FULL],
			g_fpStats[FP_STAT_INSTANCES], g_fpStats[FP_STAT_INST_OVER]);
}
