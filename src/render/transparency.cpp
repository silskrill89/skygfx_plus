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

void
Transparency_SetZEnabled(bool enable)
{
	if(config && !config->transparencyEnable)
		return;
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(enable ? (RwUInt32)TRUE : (RwUInt32)FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, enable ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZENABLE, enable ? TRUE : FALSE);
}

void
Transparency_SetZFunc(DWORD zfunc)
{
	if(config && !config->transparencyEnable)
		return;
	RwD3D9SetRenderState(D3DRS_ZFUNC, zfunc);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZFUNC, zfunc);
}

void
Transparency_SetZWrite(bool enable)
{
	if(config && !config->transparencyEnable)
		return;
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(enable ? (RwUInt32)TRUE : (RwUInt32)FALSE));
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
}

void
Transparency_RepairDepthState(void)
{
	if(config && !config->transparencyEnable)
		return;
	// Force correct depth state on all 3 layers before any scene rendering.
	Transparency_SetZEnabled(true);
	Transparency_SetZFunc(D3DCMP_LESSEQUAL);
	Transparency_SetZWrite(true);
}

void
Transparency_SuspendDepth(void)
{
	if(config && !config->transparencyEnable)
		return;
	// Suspend depth for INTZ sampling — disable Z on all 3 layers.
	Transparency_SetZEnabled(false);
	Transparency_SetZWrite(false);
}

void
Transparency_RestoreDepth(void)
{
	if(config && !config->transparencyEnable)
		return;
	// Restore depth after INTZ sampling — re-enable Z on all 3 layers.
	Transparency_SetZEnabled(true);
	Transparency_SetZWrite(true);
}
