// particles_gpu.cpp - GPU particle sprite path (m0336 lane).
//
// Design receipt (see particles_gpu.h for the overview):
//   * Harvest: CTxdStore::FindTxdSlot("fx") 0x731850 + CTxdStore::GetTxd
//     0x408340 (gta-reversed TxdStore.cpp RH installs, SA 1.0 US) give the
//     effectsPC.txd dictionary. Lookup uses RwTexDictionarySetCurrent
//     (0x7F3A70) + RwTextureRead (0x7F3AC0) — the SAME proven address pair
//     neopipe.cpp:1160-1161 uses for neo.txd — deliberately NOT
//     RwTexDictionaryFindNamedTexture (0x7F39F0 is only "may be" per
//     SKYGFXPLUS_DOCS/RenderWare SDK Architecture.md:135). The current-dict
//     dance is wrapped: prev = SetCurrent(fxDict) -> reads -> SetCurrent(prev),
//     so the game's streaming texture context is untouched outside the window.
//   * Textures are BORROWED (no AddRef, no destroy). If the fx dictionary
//     pointer changes (fx reload), the harvest re-runs.
//   * Pool: fixed 512-particle POD array, swap-remove aging. No heap.
//   * Render: full capture/restore of every device state touched (VS/PS
//     handles AddRef'd, decl/FVF, blend trio, zwrite, alphatest, cull, fog,
//     sampler 0 address/filter, RW raster cache via park-NULL restore, RW
//     _rwD3D9Last*Used cache seeds) — same doctrine as FxDrawPin
//     (neopipe.cpp:1422) and WaterDirectFlush_Draw (main.cpp:6605).
//   * Receipts: [gpu_part] install / harvest / tex / spawn / render, first-hit
//     unconditional (dbglog_once) + throttled recurrence.

#include "particles_gpu.h"
#include "skygfx.h"
#include "rw/gta.h"

#include <string.h>
#include <math.h>

// WakeParticlePS.cso handle (IDR_WAKEPARTICLEPS) — created by CreateShaders
// (pipelinecommon.cpp), device-reset-safe via Pipeline_InvalidateShaders.
void *wakeParticlePS;

// ---------------------------------------------------------------------------
// exe addresses (SA 1.0 US)
// ---------------------------------------------------------------------------

// CTxdStore::FindTxdSlot(const char*) — gta-reversed TxdStore.cpp:17 (0x731850)
static int
TxdStore_FindTxdSlot(const char *name)
{
	return ((int(__cdecl*)(const char*))0x731850)(name);
}

// CTxdStore::GetTxd(int) — gta-reversed TxdStore.cpp:36 (0x408340)
static RwTexDictionary *
TxdStore_GetTxd(int index)
{
	return ((RwTexDictionary*(__cdecl*)(int))0x408340)(index);
}

// CTimer::m_snTimeInMilliseconds — gta-reversed Timer.h:43 (0xB7CB84)
static DWORD
TimeInMs(void)
{
	return *(DWORD*)0xB7CB84;
}

// ---------------------------------------------------------------------------
// effectsPC.txd harvest
// ---------------------------------------------------------------------------

struct GpuPartTex {
	const char *name;
	RwTexture *tex;
	bool logged;
};

// Curated effectsPC.txd names (36-texture PC dict; extracted listing in
// SKYGFXPLUS_DOCS/PC-SA-Install/models/effectsPC.txd.md + H:/extracted/
// clean_sa/standalone/effectsPC). Wake slice consumes boatwake1 (primary)
// with gensplash fallback; the rest are harvested for the later systems
// (masks/seafoam/pointlight/smoke/water-droplet alphas per the lane spec).
static GpuPartTex s_tex[] = {
	{ "boatwake1",		NULL, false },	// wake foam sprite (primary)
	{ "boatsplash",		NULL, false },
	{ "gensplash",		NULL, false },	// generic splash (wake fallback)
	{ "bubbles",		NULL, false },
	{ "waterspark_16",	NULL, false },
	{ "cloudmasked",	NULL, false },	// cloud mask ≈ seafoam mask
	{ "pointlight",		NULL, false },	// pointlight alpha
	{ "smoke",		NULL, false },
	{ "smoke4",		NULL, false },
	{ "smoke5",		NULL, false },
	{ "smokeII_3",		NULL, false },
	{ "wjet2",		NULL, false },	// water-droplet sprite alphas
	{ "wjet4",		NULL, false },
	{ "wjet6",		NULL, false },
	{ "wgush1",		NULL, false },
	{ "splash_up",		NULL, false },
	{ "splash_up1",		NULL, false },
	{ "splash_up2",		NULL, false },
};
static const int s_texCount = sizeof(s_tex) / sizeof(s_tex[0]);

static RwTexDictionary *s_fxDict;
static bool s_harvestDone;
static DWORD s_harvestNextTryTick;

static RwTexture *
HarvestFind(const char *name)
{
	// prev = SetCurrent(fxDict) -> read -> SetCurrent(prev): the game's
	// current dictionary is restored before we return (streaming safety).
	RwTexDictionary *prev = RwTexDictionarySetCurrent(s_fxDict);
	RwTexture *tex = RwTextureRead(name, NULL);
	RwTexDictionarySetCurrent(prev);
	return tex;
}

static void
HarvestTextures(void)
{
	// Retry window: the fx slot only exists after FxManager_c::LoadFxProject
	// runs inside CGame::Initialise; the first wake flush can theoretically
	// beat it on a fast load. Retry at most every 2s until it shows up.
	DWORD now = TimeInMs();
	if(s_harvestNextTryTick && now < s_harvestNextTryTick)
		return;
	s_harvestNextTryTick = now + 2000;

	int slot = TxdStore_FindTxdSlot("fx");
	if(slot < 0)
		return;
	RwTexDictionary *dict = TxdStore_GetTxd(slot);
	// Pointer sanity: aligned, plausible, and not a recycled tiny value.
	if(!dict || ((uintptr_t)dict & 3u) || (uintptr_t)dict < 0x10000u)
		return;

	// Re-harvest if the dictionary was rebuilt (fx project reload).
	if(s_harvestDone && dict == s_fxDict)
		return;
	s_fxDict = dict;

	int found = 0;
	for(int i = 0; i < s_texCount; i++){
		RwTexture *tex = HarvestFind(s_tex[i].name);
		if(tex && tex->raster)
			found++;
		if(tex && !s_tex[i].logged){
			s_tex[i].logged = true;
			dbglog("[gpu_part] tex %s=%p raster=%p", s_tex[i].name,
			       (void*)tex, (void*)tex->raster);
		}
		s_tex[i].tex = tex;
	}
	s_harvestDone = true;
	dbglog("[gpu_part] harvest: slot=%d dict=%p found=%d/%d",
	       slot, (void*)dict, found, s_texCount);
}

static RwTexture *
WakeSprite(void)
{
	// primary wake sprite, gensplash fallback
	if(s_tex[0].tex && s_tex[0].tex->raster)
		return s_tex[0].tex;
	if(s_tex[2].tex && s_tex[2].tex->raster)
		return s_tex[2].tex;
	return NULL;
}

// ---------------------------------------------------------------------------
// wind-sea constants (same inputs as waterPipe.cpp:2040-2050 -> VS c11/c12)
// ---------------------------------------------------------------------------

struct WindSea {
	float dirX, dirY;	// normalized wind direction (dirA fallback)
	float str;		// CWeather__Wind clamped [0,1]
};

static WindSea
GetWindSea(void)
{
	WindSea w;
	w.dirX = CWeather__WindDir.x;
	w.dirY = CWeather__WindDir.y;
	float len = sqrtf(w.dirX * w.dirX + w.dirY * w.dirY);
	if(len < 1e-4f){
		// unit-length classic dirA — same fallback as waterPipe.cpp:2043
		w.dirX = 0.9497f;
		w.dirY = 0.3328f;
	}else{
		float inv = 1.0f / len;
		w.dirX *= inv;
		w.dirY *= inv;
	}
	w.str = CWeather__Wind;
	if(w.str < 0.0f) w.str = 0.0f;
	if(w.str > 1.0f) w.str = 1.0f;
	return w;
}

// ---------------------------------------------------------------------------
// particle pool
// ---------------------------------------------------------------------------

#define GPUPART_MAX 512

struct WakeP {
	float x, y, z;		// world position (water surface)
	float vx, vz;		// horizontal drift velocity
	float age, life;	// seconds
	float size0, size1;	// start/end size (metres)
	float alpha;		// peak alpha
};

static WakeP s_pool[GPUPART_MAX];
static int s_count;

// spawn budget: refilled once per game tick, spent across the (multiple)
// site-1 flush calls of that tick — bounds spawns regardless of segment count
static DWORD s_spawnTick;
static int s_spawnBudget;

static DWORD s_rng;

static DWORD
Rng(void)
{
	// LCG — no CRT rand state contention
	s_rng = s_rng * 1664525u + 1013904223u;
	return s_rng;
}

static float
RngF(float lo, float hi)
{
	return lo + (hi - lo) * ((float)(Rng() & 0xFFFFFFu) / 16777215.0f);
}

static void
KillSwap(int i)
{
	s_pool[i] = s_pool[s_count - 1];
	s_count--;
}

// ---------------------------------------------------------------------------
// spawn (called from WakeDirectFlush_Draw site 1)
// ---------------------------------------------------------------------------

// TempBuffer wake verts: 0xC4D958, 36-byte RwIm3DVertex layout
// (XYZ|NORMAL|DIFFUSE|TEX1 — main.cpp:6885), world-space (the W5 analysis,
// main.cpp:6656-6667).
#define WAKE_VERTBUF        (*(RwIm3DVertex**)0xC4D958)
#define WAKE_VERTSTRIDE     36
#define WAKE_VERTCOUNT      (*(DWORD*)0xC4B950)

void
GPUParticles_WakeSpawnFromTempBuffer(int site)
{
	if(!config || !config->gpuParticlesEnable)
		return;
	if(site != 1)			// site 2 = remainder pass, no spawn
		return;

	HarvestTextures();

	DWORD now = TimeInMs();
	if(s_spawnTick != now){
		s_spawnTick = now;
		// ≤3 particles per game tick across all wake segments — a boat
		// renders several segments per frame, so per-call budgets would
		// scale with segment count and flood the pool.
		s_spawnBudget = 3;
	}
	if(s_spawnBudget <= 0 || s_count >= GPUPART_MAX)
		return;

	DWORD nVerts = WAKE_VERTCOUNT;
	RwIm3DVertex *verts = WAKE_VERTBUF;
	if(!verts || nVerts == 0 || nVerts > 4096)
		return;

	static bool spawnFirst = false;
	if(!spawnFirst){
		spawnFirst = true;
		dbglog("[gpu_part] wake spawn first hit: site=%d verts=%u pool=%d",
		       site, (unsigned)nVerts, s_count);
	}

	WindSea w = GetWindSea();

	int n = s_spawnBudget;
	if(n > GPUPART_MAX - s_count)
		n = GPUPART_MAX - s_count;

	for(int k = 0; k < n; k++){
		DWORD idx = Rng() % nVerts;
		const BYTE *v = (const BYTE*)verts + (size_t)idx * WAKE_VERTSTRIDE;
		float px, py, pz;
		memcpy(&px, v + 0, 4);
		memcpy(&py, v + 4, 4);
		memcpy(&pz, v + 8, 4);
		// reject garbage verts (uninitialised slots / NaN-ish)
		if(!(px > -16000.0f && px < 16000.0f) ||
		   !(py > -16000.0f && py < 16000.0f) ||
		   !(pz > -16000.0f && pz < 16000.0f))
			continue;

		WakeP &p = s_pool[s_count];
		p.x = px + RngF(-0.4f, 0.4f);
		p.y = py;
		p.z = pz + RngF(-0.4f, 0.4f);

		// drift: wind push (scaled by the c12.x wind strength) + jitter
		float drift = 0.25f + 0.55f * w.str;
		p.vx = w.dirX * drift + RngF(-0.15f, 0.15f);
		p.vz = w.dirY * drift + RngF(-0.15f, 0.15f);

		p.life = RngF(1.6f, 3.0f);
		p.age = 0.0f;
		// size: starts small, grows with age; growth scales with wind
		p.size0 = RngF(0.28f, 0.45f);
		p.size1 = p.size0 * (2.2f + 0.8f * w.str);
		// peak alpha: modest base, slightly stronger in wind (choppy sea
		// throws more foam) — c12.x scaled like the swell amplitude
		p.alpha = 0.42f + 0.18f * w.str;

		s_count++;
		s_spawnBudget--;
		if(s_spawnBudget <= 0 || s_count >= GPUPART_MAX)
			break;
	}
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

static void
UpdateParticles(float dt, const WindSea &w)
{
	for(int i = s_count - 1; i >= 0; i--){
		WakeP &p = s_pool[i];
		p.age += dt;
		if(p.age >= p.life){
			KillSwap(i);
			continue;
		}
		// drift + wind push (same wind vector the sea VS uses)
		p.vx += w.dirX * w.str * 0.35f * dt;
		p.vz += w.dirY * w.str * 0.35f * dt;
		// damping — foam slows down as it spreads
		float damp = 1.0f - 0.6f * dt;
		if(damp < 0.0f) damp = 0.0f;
		p.vx *= damp;
		p.vz *= damp;
		p.x += p.vx * dt;
		p.z += p.vz * dt;
		// p.y stays at the sampled water-surface height
	}
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

struct PartVert {		// D3DFVF_XYZRHW|DIFFUSE|TEX1 = 0x144, 28 bytes
	float x, y, z, rhw;
	DWORD c;
	float u, v;
};
static PartVert s_verts[GPUPART_MAX * 6];

static float
Smooth01(float a, float b, float x)
{
	// clamped smoothstep, division-guarded
	float t = (x - a) / ((b - a) > 1e-7f ? (b - a) : 1e-7f);
	if(t < 0.0f) t = 0.0f;
	if(t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

static bool
BuildCamMatrices(D3DMATRIX *view, D3DMATRIX *proj, float aspect)
{
	// Same math as Fp_BuildViewProjFromCamera (forwardplus.cpp:1202):
	// view = inverse of the camera frame LTM in row-vector layout,
	// proj = standard D3D9 LH perspective from the RW camera frustum.
	if(!Scene.camera)
		return false;
	RwFrame *frm = RwCameraGetFrame(Scene.camera);
	if(!frm)
		return false;
	RwMatrix *ltm = RwFrameGetLTM(frm);
	if(!ltm)
		return false;

	RwMatrix inv;
	if(!RwMatrixInvert(&inv, ltm))
		return false;

	view->m[0][0] = inv.right.x; view->m[0][1] = inv.right.y; view->m[0][2] = inv.right.z; view->m[0][3] = 0.0f;
	view->m[1][0] = inv.up.x;    view->m[1][1] = inv.up.y;    view->m[1][2] = inv.up.z;    view->m[1][3] = 0.0f;
	view->m[2][0] = inv.at.x;    view->m[2][1] = inv.at.y;    view->m[2][2] = inv.at.z;    view->m[2][3] = 0.0f;
	view->m[3][0] = inv.pos.x;   view->m[3][1] = inv.pos.y;   view->m[3][2] = inv.pos.z;   view->m[3][3] = 1.0f;

	float tanY = Scene.camera->viewWindow.y;
	float n = Scene.camera->nearPlane;
	float f = Scene.camera->farPlane;
	if(!(tanY > 1e-7f) || !(aspect > 1e-7f) || !(n > 1e-7f) || !(f > n + 1e-7f))
		return false;

	memset(proj, 0, sizeof(D3DMATRIX));
	proj->m[0][0] = 1.0f / (aspect * tanY);
	proj->m[1][1] = 1.0f / tanY;
	proj->m[2][2] = f / (f - n);
	proj->m[2][3] = 1.0f;
	proj->m[3][2] = -n * proj->m[2][2];
	return true;
}

static void
ProjectPoint(const D3DMATRIX &vpm, float x, float y, float z,
             float vpW, float vpH, PartVert *out, bool *ok)
{
	// row-vector: clip = (x,y,z,1) * (V*P)
	float cx = x * vpm.m[0][0] + y * vpm.m[1][0] + z * vpm.m[2][0] + vpm.m[3][0];
	float cy = x * vpm.m[0][1] + y * vpm.m[1][1] + z * vpm.m[2][1] + vpm.m[3][1];
	float cz = x * vpm.m[0][2] + y * vpm.m[1][2] + z * vpm.m[2][2] + vpm.m[3][2];
	float cw = x * vpm.m[0][3] + y * vpm.m[1][3] + z * vpm.m[2][3] + vpm.m[3][3];
	if(!(cw > 1e-7f)){		// behind camera / degenerate — cull
		*ok = false;
		return;
	}
	float invW = 1.0f / cw;
	float ndcX = cx * invW;
	float ndcY = cy * invW;
	// screen margin cull (quad can extend past the point, hence 0.25 slack)
	if(ndcX < -1.25f || ndcX > 1.25f || ndcY < -1.25f || ndcY > 1.25f){
		*ok = false;
		return;
	}
	out->x = (ndcX * 0.5f + 0.5f) * vpW;
	out->y = (0.5f - ndcY * 0.5f) * vpH;
	// real NDC depth (D3D9 LH [0,1]) so the foam z-tests against the water
	// sheet instead of pinning to the near plane
	float ndcZ = cz * invW;
	if(ndcZ < 0.0f) ndcZ = 0.0f;
	if(ndcZ > 1.0f) ndcZ = 1.0f;
	out->z = ndcZ;
	out->rhw = invW;
	*ok = true;
}

void
GPUParticles_FrameFlush(const char *tag)
{
	(void)tag;
	if(!config || !config->gpuParticlesEnable)
		return;
	if(s_count <= 0)
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;

	// once-per-game-tick latch: update + render exactly once per frame even
	// though both flush families call this
	static DWORD s_lastTick;
	DWORD now = TimeInMs();
	if(now == s_lastTick)
		return;
	s_lastTick = now;

	float dt = CTimer__ms_fTimeStep * 0.02f;	// ms_fTimeStep is in 1/50s units
	if(dt < 0.0f) dt = 0.0f;
	if(dt > 0.1f) dt = 0.1f;
	WindSea w = GetWindSea();
	UpdateParticles(dt, w);
	if(s_count <= 0)
		return;

	RwTexture *tex = WakeSprite();
	if(!tex || !tex->raster)
		return;

	D3DVIEWPORT9 vp;
	if(FAILED(dev->GetViewport(&vp)))
		return;
	float vpW = (float)vp.Width, vpH = (float)vp.Height;
	if(vpW < 8.0f || vpH < 8.0f)
		return;

	D3DMATRIX view, proj;
	if(!BuildCamMatrices(&view, &proj, vpW / vpH))
		return;
	// compose V*P once (row-vector multiply)
	D3DMATRIX vpm;
	for(int c = 0; c < 4; c++){
		for(int r = 0; r < 4; r++){
			float sum = 0.0f;
			for(int k = 0; k < 4; k++)
				sum += view.m[r][k] * proj.m[k][c];
			vpm.m[r][c] = sum;
		}
	}

	// billboard axes: camera right flattened onto the water plane (XZ) and
	// its horizontal perpendicular — foam quads lie on the surface
	RwFrame *frm = RwCameraGetFrame(Scene.camera);
	RwMatrix *ltm = frm ? RwFrameGetLTM(frm) : NULL;
	if(!ltm)
		return;
	float rx = ltm->right.x, rz = ltm->right.z;
	float rl = sqrtf(rx * rx + rz * rz);
	if(rl < 1e-5f){
		rx = 1.0f; rz = 0.0f;	// degenerate top-down camera fallback
	}else{
		rx /= rl; rz /= rl;
	}
	float fx = rz, fz = -rx;	// up(0,1,0) × r = (rz, 0, -rx)

	// ---- build the quad batch ----
	int nv = 0;
	for(int i = 0; i < s_count && nv <= (GPUPART_MAX - 1) * 6; i++){
		WakeP &p = s_pool[i];
		float t = p.age / ((p.life > 1e-7f) ? p.life : 1e-7f);
		// fade IN over the first 18%, OUT over the last 65% of life
		float env = Smooth01(0.0f, 0.18f, t) * (1.0f - Smooth01(0.35f, 1.0f, t));
		float a = p.alpha * env;
		if(a < 0.004f)
			continue;
		float size = p.size0 + (p.size1 - p.size0) * t;
		float hx = rx * size * 0.5f, hz = rz * size * 0.5f;
		float gx = fx * size * 0.5f, gz = fz * size * 0.5f;

		DWORD col = 0xFFFFFFFFu;			// rgb white
		col |= ((DWORD)(a * 255.0f + 0.5f)) << 24;	// alpha envelope

		PartVert quad[4];
		bool ok = true;
		// corners: -r-g, +r-g, +r+g, -r+g (horizontal quad on the surface)
		ProjectPoint(vpm, p.x - hx - gx, p.y, p.z - hz - gz, vpW, vpH, &quad[0], &ok);
		if(!ok) continue;
		ProjectPoint(vpm, p.x + hx - gx, p.y, p.z + hz - gz, vpW, vpH, &quad[1], &ok);
		if(!ok) continue;
		ProjectPoint(vpm, p.x + hx + gx, p.y, p.z + hz + gz, vpW, vpH, &quad[2], &ok);
		if(!ok) continue;
		ProjectPoint(vpm, p.x - hx + gx, p.y, p.z - hz + gz, vpW, vpH, &quad[3], &ok);
		if(!ok) continue;

		static const float cu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
		static const float cv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
		for(int k = 0; k < 6; k++){
			PartVert &d = s_verts[nv];
			PartVert &s = quad[idx[k]];
			d.x = s.x; d.y = s.y; d.z = s.z; d.rhw = s.rhw;
			d.c = col;
			d.u = cu[idx[k]];
			d.v = cv[idx[k]];
			nv++;
		}
	}
	if(nv <= 0)
		return;

	// ---- capture ----
	IDirect3DVertexShader9 *oVS = NULL;
	IDirect3DPixelShader9 *oPS = NULL;
	IDirect3DVertexDeclaration9 *oDecl = NULL;
	DWORD oFVF = 0, oZw = 0, oAb = 0, oSb = 0, oDb = 0, oBo = 0;
	DWORD oAt = 0, oCull = 0, oFog = 0;
	DWORD oAddrU = 0, oAddrV = 0, oMag = 0, oMin = 0, oMip = 0;
	dev->GetVertexShader(&oVS);
	dev->GetPixelShader(&oPS);
	dev->GetVertexDeclaration(&oDecl);
	dev->GetFVF(&oFVF);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &oZw);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oAb);
	dev->GetRenderState(D3DRS_SRCBLEND, &oSb);
	dev->GetRenderState(D3DRS_DESTBLEND, &oDb);
	dev->GetRenderState(D3DRS_BLENDOP, &oBo);
	dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oAt);
	dev->GetRenderState(D3DRS_CULLMODE, &oCull);
	dev->GetRenderState(D3DRS_FOGENABLE, &oFog);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &oAddrU);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &oAddrV);
	dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &oMag);
	dev->GetSamplerState(0, D3DSAMP_MINFILTER, &oMin);
	dev->GetSamplerState(0, D3DSAMP_MIPFILTER, &oMip);
	// RW cache seeds + raster cache (same layers FxDrawPin captures)
	void *cacheVS = *(void**)0x8E2448;		// _rwD3D9LastVertexShaderUsed
	void *cachePS = *(void**)0x8E244C;		// _rwD3D9LastPixelShaderUsed
	DWORD cacheFVF = *(DWORD*)0x8E2440;		// _rwD3D9LastFVFUsed
	void *cacheDecl = *(void**)0x8E2444;		// _rwD3D9LastVertexDeclarationUsed
	RwRaster *oRas = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &oRas);
	if((uintptr_t)oRas == (uintptr_t)0xFFFFFFFF)	// failed-Get sentinel
		oRas = NULL;

	// ---- set ----
	dev->SetVertexShader(NULL);
	dev->SetPixelShader((IDirect3DPixelShader9*)wakeParticlePS);
	dev->SetVertexDeclaration(NULL);
	dev->SetFVF(0x144);				// XYZRHW|DIFFUSE|TEX1
	*(void**)0x8E2448 = NULL;
	*(void**)0x8E244C = wakeParticlePS;
	*(void**)0x8E2440 = (void*)0x144;
	*(void**)0x8E2444 = NULL;
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	// RW raster bind keeps the RW cache in sync (doctrine: never raw-bind
	// stage 0 behind RW's back — FxDrawPin block comment, neopipe.cpp:1400)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)tex->raster);

	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, nv / 3, s_verts, sizeof(PartVert));

	// ---- restore ----
	// stage 0: park on NULL first so the restore is a guaranteed cache miss
	// (same idiom as FxDrawPinExit, neopipe.cpp:1514)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	if(oRas)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)oRas);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, oAddrU);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, oAddrV);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, oMag);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, oMin);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, oMip);
	dev->SetRenderState(D3DRS_FOGENABLE, oFog);
	dev->SetRenderState(D3DRS_CULLMODE, oCull);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, oAt);
	dev->SetRenderState(D3DRS_BLENDOP, oBo);
	dev->SetRenderState(D3DRS_DESTBLEND, oDb);
	dev->SetRenderState(D3DRS_SRCBLEND, oSb);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oAb);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, oZw);
	dev->SetVertexDeclaration(oDecl);
	dev->SetFVF(oFVF);
	dev->SetVertexShader(oVS);
	dev->SetPixelShader(oPS);
	*(void**)0x8E2448 = cacheVS;
	*(void**)0x8E244C = cachePS;
	*(void**)0x8E2440 = (void*)(uintptr_t)cacheFVF;
	*(void**)0x8E2444 = cacheDecl;
	if(oVS) oVS->Release();
	if(oPS) oPS->Release();
	if(oDecl) oDecl->Release();

	// ---- receipts ----
	static bool s_installLogged = false;
	if(!s_installLogged){
		s_installLogged = true;
		dbglog("[gpu_part] install: PS=%p sprite=%p pool=%d verts=%d ini=gpuParticlesEnable=1",
		       wakeParticlePS, (void*)tex, s_count, nv / 3);
	}
	static bool s_renderFirst = false;
	if(!s_renderFirst){
		s_renderFirst = true;
		dbglog("[gpu_part] first render: tag=%s n=%d quads=%d", tag, s_count, nv / 6);
	}
	if(dbglog_throttle("gpu_part_render"))
		dbglog("[gpu_part] render tag=%s n=%d quads=%d", tag, s_count, nv / 6);
}
