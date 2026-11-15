// transparency.cpp — centralized depth-state (ZENABLE/ZFUNC/ZWRITE) management.
// All ZENABLE/ZFUNC/ZWRITE writes across the codebase go through this module
// so the three layers (L1 rw cache, L2 RwD3D9 driver cache, L3 raw device)
// never disagree. Toggleable via config->transparencyEnable (INI: [SkyGfx]
// transparencyEnable, default 1).
#include "transparency.h"
#include "skygfx.h"
#include "depthhook.h"

extern IDirect3DDevice9 *&d3d9device;
extern Config *config;

// Saved ZENABLE for Suspend/Restore
static DWORD s_savedZenable = TRUE;

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

void
Transparency_RepairDepthState(void)
{
	// CRITICAL REPAIR: always runs regardless of transparencyEnable toggle.
	// This fixes the depth state corruption (ZENABLE=0, ZFUNC=ALWAYS) that
	// causes black screens. The toggle only gates the per-site writes.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
}

void
Transparency_SuspendDepth(void)
{
	// Save current ZENABLE and disable on all 3 layers for INTZ sampling.
	if(d3d9device)
		d3d9device->GetRenderState(D3DRS_ZENABLE, &s_savedZenable);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZENABLE, FALSE);
}

void
Transparency_RestoreDepth(void)
{
	// Restore saved ZENABLE on all 3 layers.
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(s_savedZenable ? (RwUInt32)TRUE : (RwUInt32)FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, s_savedZenable);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZENABLE, s_savedZenable);
}

void
Transparency_ParkSetZEnabled(bool value)
{
	if(config && !config->transparencyEnable)
		return;
	// Park to inverse, then set to target (forces cache transition on all layers).
	Transparency_SetZEnabled(!value);
	Transparency_SetZEnabled(value);
}
