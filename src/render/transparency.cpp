// transparency.cpp — centralized depth-state (ZENABLE/ZFUNC/ZWRITE) management.
// All ZENABLE/ZFUNC/ZWRITE writes across the codebase go through this module
// so the three layers (L1 rw cache, L2 RwD3D9 driver cache, L3 raw device)
// never disagree. Toggleable via config->transparencyEnable (INI: [SkyGfx]
// transparencyEnable, default 1).
#include "transparency.h"
#include "skygfx.h"
#include "depthhook.h"

extern IDirect3DDevice9 *&d3d9device;

// Saved ZENABLE for Suspend/Restore

void
Transparency_SetZEnabled(bool enable)
{
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(enable ? (RwUInt32)TRUE : (RwUInt32)FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, enable ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZENABLE, enable ? TRUE : FALSE);
}

void
Transparency_SetZFunc(DWORD zfunc)
{
	RwD3D9SetRenderState(D3DRS_ZFUNC, zfunc);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZFUNC, zfunc);
}

void
Transparency_SetZWrite(bool enable)
{
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(enable ? (RwUInt32)TRUE : (RwUInt32)FALSE));
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
}

