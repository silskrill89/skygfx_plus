// transparency.h — centralized depth-state (ZENABLE/ZFUNC/ZWRITE) management.
// All ZENABLE/ZFUNC/ZWRITE writes across the codebase should go through this
// module so the three layers (L1 rw cache, L2 RwD3D9 driver cache, L3 raw
// device) never disagree. Toggleable via config->transparencyEnable.
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
