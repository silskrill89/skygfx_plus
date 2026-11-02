#pragma once
// renderstate_monitor.h — three-layer render-state divergence monitor.
//
// The renderer writes render state through three independent layers:
//   L1  rw cache        RwRenderStateSet / RwRenderStateGet
//   L2  D3D9 driver     RwD3D9SetRenderState / RwD3D9GetRenderState
//   L3  raw device      IDirect3DDevice9::SetRenderState / GetRenderState
//
// A write that moves only one layer leaves the caches disagreeing with the
// device. RW's value-compare caches then no-op on later sets, and the wrong
// state persists (the "[FxAlpha] / [PED] desync" failure class). RSMon_Sample
// reads the same states from all three layers and logs ONE throttled line
// only when they disagree; healthy samples cost no log I/O.
//
// Integration (diagnostic only — no repair, no hooks):
//   RSMon_Init();                       // once, when the renderer is up
//   RSMon_Sample("building_in", pipe);  // callback entry/exit, postfx edges
//   RSMon_Report();                     // once per frame (internally 5 s)
//   RSMon_Reset();                      // after a D3D9 device reset
//   RSMon_SetEnabled(0);                // runtime kill switch (default on)
//
// RSMon_Sample is null-device safe and never faults out: a capture fault
// drops the sample instead of crashing the render callback. Call these
// functions from the render thread only (counters/table are not atomic).

// Compile-time kill switch, default ON. Set to 0 to compile the probe out.
#ifndef RSMON_ENABLED
#define RSMON_ENABLED 1
#endif

void RSMon_Init(void);

// Capture all monitored states from the three layers and compare them.
// `site` is a short stable label (truncated to 31 chars in the per-site
// table); `pipeId` is the active pipeline enum for correlation.
void RSMon_Sample(const char *site, int pipeId);

// Aggregate counter summary. Emits at most once every 5 seconds.
// Classification: diverge= = diffs still present at the paired frame_end
// (exit) sample — the persistent-desync class; transient= = entry-only diffs
// healed within their frame (the benign postfx-tail/flush-lag class);
// agree= = fully clean samples; bad= = capture faults. top=/top_n=/sites=
// count persistent divergence only.
void RSMon_Report(void);

// Zero all counters and the 16-site divergence table (D3D9 device reset).
void RSMon_Reset(void);

// Runtime enable flag (file-static int in the .cpp). 0 = Sample/Report no-op.
void RSMon_SetEnabled(int enabled);
