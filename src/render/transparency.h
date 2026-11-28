// transparency.h — convenience 3-layer writers for the depth triple
// (ZENABLE/ZFUNC/ZWRITE). NOT a chokepoint: the codebase has ~70 raw L3
// `->SetRenderState(D3DRS_{ZWRITEENABLE,ZENABLE,ZFUNC})` writes outside
// this module, plus ~14 via pipeDeviceStateSet and ~12 via pipeL3Set
// (96 L3 depth writes total; this module owns 8 of them). Layer census:
// L1 RwRenderStateSet 150, L2 RwD3D9SetRenderState 89, L3 96.
// Transparency_* has 17 live call sites outside this file. Callers pick
// their own layer strategy; this module only guarantees 3-layer coherence
// FOR ITS OWN CALLS. Toggleable via config->transparencyEnable.
#pragma once
#include "skygfx.h"

// Set ZENABLE on all three layers.
void Transparency_SetZEnabled(bool enable);

// Set ZFUNC on all three layers.
void Transparency_SetZFunc(DWORD zfunc);

// Set ZWRITE on all three layers.
void Transparency_SetZWrite(bool enable);

// Repair depth state before scene rendering (forces ZENABLE=TRUE, ZFUNC=LESSEQUAL).
// Toggleable via config->transparencyEnable.
void Transparency_RepairDepthState(void);

// Suspend depth for INTZ sampling (disables Z on all 3 layers).
void Transparency_SuspendDepth(void);

// Restore depth after INTZ sampling.
void Transparency_RestoreDepth(void);

// Park-then-set ZENABLE (forces cache transition): sets to !value then value.
void Transparency_ParkSetZEnabled(bool value);
