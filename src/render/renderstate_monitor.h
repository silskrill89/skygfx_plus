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

// Canonical depth-contract comparator — ORTHOGONAL axis (see the contract
// block next to RSMon_Report below). Measurement-only: no repair, no
// cross-layer classification, never folded into diverge/transient/cache-split.
// Logs a [zfix2] receipt per violated contract field, per layer, against the
// fixed canonical {ALPHABLEND=0, ZWRITE=1, ZTEST=1, ZFUNC=LESSEQUAL}.
// Call-divisor sampled inside (1-in-16, first call kept) since the pipe-edge
// sites are hot; 31-Get capture per sampled call; silent when the contract holds.
void RSMon_ContractSample(const char *site, int pipeId);

// Aggregate counter summary. Emits at most once every 5 seconds.
// Classification (recalibrated 2026-11-08 — layer-split verdict):
//   diverge   = a diff still present at the paired frame_end (exit) sample,
//               OR an L1<->L2 cache split (rw cache vs RW D3D9 driver
//               cache) at an entry or mid-frame sample. Both caches are
//               value-compare caches, so an L1/L2 split has no self-healing
//               path and is the persistent class even when the frame's exit
//               repair hides it (the [PipeAlpha]/[zfix] re-created-drift
//               class). This is the persistent-desync class.
//   transient = a DEVICE-ONLY entry diff healed within its frame (the
//               postfx/flush-lag tail), or an entry diff with no exit
//               verdict (unpaired — loading screen, early out). Per-sample
//               lines stay raw: any cross-layer difference logs.
//   agree     = fully clean samples; bad= = capture faults.
// top=/top_n=/sites= count persistent divergence only. pct= is over
// classified samples (agree+diverge+transient). cache= counts samples whose
// diff held an L1<->L2 split.
//
// CANONICAL DEPTH CONTRACT (ORTHOGONAL axis — [zfix2] receipt)
//   The classification above measures whether the three layers AGREE WITH
//   EACH OTHER. It is structurally blind to a state where all three layers
//   agree on a WRONG value: the N2 "zfunc=8 dead-depth" signature
//   (rw ztest=0, drv zen=0 zfunc=8, dev zen=0 zfunc=8) has rsmonDiffMask==0
//   yet is broken depth. RSMon_ContractSample instead checks each layer
//   against a FIXED canonical contract, never against the other layers:
//       ALPHABLEND = 0  (blend off)
//       ZWRITE     = 1  (write on)
//       ZTEST      = 1  (test on)   [L2/L3 read via D3DRS_ZENABLE]
//       ZFUNC      = LESSEQUAL (=4) [L2/L3 only — no rw twin]
//   A violation is reported per layer under the [zfix2] token. This axis is
//   never mixed with diverge/transient/cache-split: it answers "is the depth
//   contract violated?" while the diff axis answers "which layer moved?".
//   Measurement-only — no repair, no counters.
void RSMon_Report(void);

// Zero all counters and the 32-site divergence table (D3D9 device reset).
void RSMon_Reset(void);

// Runtime enable flag (file-static int in the .cpp). 0 = Sample/Report no-op.
void RSMon_SetEnabled(int enabled);
