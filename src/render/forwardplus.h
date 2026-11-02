#pragma once

struct ClusterLight {
	float x, y, z;        // world position
	float radius;          // attenuation radius (world units)
	float r, g, b;         // color (linear, as collected/added — uploaded RAW
	                       // into the s7 atlas; NOT pre-multiplied by intensity)
	float intensity;       // brightness = r+g+b — sort key only (the unified
	                       // list is brightest-sorted before atlas upload)
	float type;            // 0 = point, 1 = spot (CPointLights convention;
	                       // stored in s7 .w as float — consumers may ignore)

	// FpMergeAddedLights (fp_canDeep) camera-distance weighted priority key:
	// intensity / max(dist_to_camera, radius) — the per-frame priority used
	// ONLY when the merged list exceeds the 256 atlas texels, so a distant
	// super-bright light cannot evict a NEAR street lamp from the atlas.
	// Zero on the plain CollectLights path (selection below 256 is pure
	// intensity, byte-identical to the old behaviour).
	float priority;
};

// Call each frame before rendering — collects lights, culls to tiles, uploads to GPU
void ForwardPlus_CullAndUpload(void);

// Call on DLL_PROCESS_DETACH and device reset
void ForwardPlus_ReleaseResources(void);

// Call from vehicle/building pipe render callbacks to set cluster constants
void ForwardPlus_SetConstants(void);

// Append a world light to the PER-FRAME light buffer (2DFX / main.cpp lane).
// CONTRACT:
//   - Call BEFORE ForwardPlus_CullAndUpload each frame — the buffer is
//     drained once per CullAndUpload, after its transient defer gates pass
//     (a deferred frame KEEPS the buffer losslessly; multi-frame
//     accumulation while deferred is absorbed by the 0.25m dedup + the
//     256 cap; the feature-disabled path discards explicitly).
//   - x,y,z = WORLD position; radius = attenuation radius (world units);
//     r,g,b = linear color (uploaded raw into the s7 light atlas); type =
//     0=point, 1=spot (CPointLights convention; shader may ignore).
//   - Capacity 256 lights; overflow past 256 is silently dropped with one
//     dbglog_first for the process lifetime.
//   - Merged into ONE unified list (<=256), brightest-sorted, before upload
//     to the s6/s7 light atlases (the game CPointLights set arrives through
//     this same buffer via ForwardPlus_FeedGameLights below).
void ForwardPlus_AddLight(float x, float y, float z, float radius, float r, float g, float b, float type);

// Push the CURRENT CPointLights registry (the game's per-frame dynamic light
// list: vehicle head/tail lights, muzzle flashes, fires, explosions, 2DFX
// lamps in range) into the AddLight deferred buffer. BASES ARE NOT HARDCODED:
// the count base is the disp32 of `mov edx,[imm32]` at exe 0x700176 and the
// entry base is the disp32 of `lea eax,[ecx+imm32]` at 0x7001FD, so a limit
// adjuster that relocates either array is followed automatically (0xC3F0D0 /
// 0xC3F0E0 are only the 1.0 US fallback, used with one logged sanity reject).
// TIMING: call when the registry holds a FULLY populated frame — after
// the scene render that filled it and before the NEXT frame's inline clear
// (`mov [0xC3F0D0],0` @ exe 0x53E977, just before RenderScene 0x53EABF); the
// main.cpp coronaFeedForwardPlus slot satisfies this. Entries merge into the
// next CullAndUpload's unified list (one-frame latency; zero lights was the
// bug this fixes). Skips vanilla non-lighting types 3/4 (exe consumer
// 0x6FFBB0 skips exactly those), degenerate radius/colour; clamps counts
// >32 (game's own Add cap) to the 32 valid slots instead of rejecting.
// Returns the number of lights pushed (diagnostics).
int ForwardPlus_FeedGameLights(void);

// Lane-local diagnostics (owned by forwardplus.cpp; debugmenu_ui.cpp already
// externs g_fpGpuLightCount / g_fpNumLights).
extern int g_fpNumLights;        // lights in the unified list this frame
extern int g_fpGpuLightCount;    // lights actually uploaded (c45.w)

// Per-frame culling statistics (0 when the pass defers/disabled):
//   [0] tiles touched by >= 1 light      [1] tiles at the 16-light cap
//   [2] light instances evaluated        [3] light instances OVERFLOWED (>16)
//   [4] candidates dropped by the 256 cap (evicted, brighter merged in)
//   [5] tile slots lost to the 255-encodable-index edge (atlas index 256)
extern int g_fpStats[6];

// ---- Debug: cluster/tile visualization (INI gated: forwardPlusDebug) ----
// Screen-space visualization of the 16x16 px tile grid computed by the
// forward+ culler: tile tint by light count (dark = culled empty, cold =
// 1 light, green = 4, yellow = 8, orange = 12, red = 16/capped), plus a
// brighter stroke on capped tiles. Pure D3D9 fixed-function quads drawn
// AFTER the scene (call it from the post-render stage, e.g. the top of
// CPostEffects::DrawFinalEffects AFTER the ImGui block so the menu draws on
// top). Transparent no-op when disabled or the pass never ran this frame;
// saves and restores every render state it touches.
void ForwardPlus_DrawClusterDebug(void);

// Debug view modes (forwardPlusDebug INI): 0 = tile grid, 1 = current
// per-tile light counts printed by the (staged) ImGui row, 2 = overlay
// legend drawn in the top-left corner.
#define FP_DBG_TILES 0
#define FP_DBG_STATS 1
#define FP_DBG_LEGEND 2
int ForwardPlus_DebugView(void);   // effective mode this session (0..2)

// One-line culling statistics for the debug menu / ImGui, written into `out`
// (snprintf semantics; always NUL-terminated). Example:
//   lights: gpu=42/256  tiles: 812 lit / 7 capped (16)  cull inst: 3412
//   overflow: 119  capDrops: 0  idx256: 0  ms: r0.04 c0.21 u0.06
void ForwardPlus_StatsText(char *out, int outSize);
