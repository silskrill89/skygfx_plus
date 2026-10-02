#include "../skygfx.h"
#include "../rw/gta.h"
#include "forwardplus.h"
#include <d3d9.h>
#include <math.h>
#include <string.h>

// Tile grid constants
#define FP_TILE_SIZE     16     // pixels per tile edge
#define FP_GRID_W        128    // tile grid width (power of 2, covers up to 2048px)
#define FP_GRID_H        128    // tile grid height
#define FP_MAX_LIGHTS    256    // max lights to collect per frame
#define FP_LIGHTS_PER_TILE 4    // RGBA8 = 4 channels
#define FP_MAX_LIGHTS_GPU 32    // max lights uploaded as PS constants (all game lights)

// D3D resources
static IDirect3DTexture9 *g_fpIndexTex = NULL;    // tile light index texture (128×128 RGBA8)
static IDirect3DTexture9 *g_fpIndexTexB = NULL;   // double buffer
static int g_fpCurrentBuffer = 0;

// Light collection
static ClusterLight g_fpLights[FP_MAX_LIGHTS];
int g_fpNumLights = 0;       // non-static: debug menu shows it next to the GPU count
static int g_fpRawCount = 0; // raw game count before range filtering (diagnostics)

// Tile data (per-tile light indices)
static unsigned char g_fpTileData[FP_GRID_W * FP_GRID_H * 4]; // RGBA8

// Lights forwarded to the GPU — uploaded in g_fpLights order so the 1-based
// indices stored in the tile texture map 1:1 onto the constant arrays.
static ClusterLight g_fpGpuLights[FP_MAX_LIGHTS_GPU];
int g_fpGpuLightCount = 0;

// Release D3D resources
void ForwardPlus_ReleaseResources(void)
{
	if(g_fpIndexTex){ dbglog("ForwardPlus: Release indexTex=%p", g_fpIndexTex); g_fpIndexTex->Release(); g_fpIndexTex = NULL; }
	if(g_fpIndexTexB){ dbglog("ForwardPlus: Release indexTexB=%p", g_fpIndexTexB); g_fpIndexTexB->Release(); g_fpIndexTexB = NULL; }
}

// Initialize textures if needed
static void EnsureFPResources(IDirect3DDevice9 *dev, int screenW, int screenH)
{
	if(g_fpIndexTex)
		return;
	
	// Create double-buffered index textures
	if(FAILED(dev->CreateTexture(FP_GRID_W, FP_GRID_H, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fpIndexTex, NULL))){
		dbglog("[ForwardPlus] CreateTexture index A failed");
		return;
	}
	if(FAILED(dev->CreateTexture(FP_GRID_W, FP_GRID_H, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fpIndexTexB, NULL))){
		dbglog("[ForwardPlus] CreateTexture index B failed");
		return;
	}
	dbglog("[ForwardPlus] textures created %dx%d for %dx%d screen", FP_GRID_W, FP_GRID_H, screenW, screenH);

	// Zero-init both buffers. SetConstants reads the PREVIOUS frame's texture
	// (double-buffer), so without this the first frame after enabling would
	// sample uninitialized indices and could apply random lights.
	for(int b = 0; b < 2; b++){
		IDirect3DTexture9 *tex = b ? g_fpIndexTexB : g_fpIndexTex;
		D3DLOCKED_RECT lr;
		if(SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))){
			for(int y = 0; y < FP_GRID_H; y++)
				memset((BYTE*)lr.pBits + y * lr.Pitch, 0, FP_GRID_W * 4);
			tex->UnlockRect(0);
		}
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
static int *NumLights = (int*)0xC3F0D0;
static CRegisteredPointLight *PointLights = (CRegisteredPointLight*)0xC3F0E0;
#define MAX_GAME_LIGHTS 32

// Collect lights from GTA SA's CPointLights system
static void CollectLights(void)
{
	g_fpNumLights = 0;

	// Guard: NumLights pointer must be readable and count must be sane
	if(!NumLights) return;
	int numLights = *NumLights;
	g_fpRawCount = numLights; // keep raw value even when rejected (log/menu)
	if(numLights <= 0 || numLights > MAX_GAME_LIGHTS)
		return;
	if(!PointLights) return;

	for(int i = 0; i < numLights && g_fpNumLights < FP_MAX_LIGHTS; i++){
		const CRegisteredPointLight &light = PointLights[i];

		// Skip lights with zero radius or zero color
		if(light.radius <= 0.0f) continue;
		float brightness = light.colorR + light.colorG + light.colorB;
		if(brightness < 0.001f) continue;

		ClusterLight &cl = g_fpLights[g_fpNumLights];
		cl.x = light.posX;
		cl.y = light.posY;
		cl.z = light.posZ;
		cl.radius = light.radius;
		cl.r = light.colorR;
		cl.g = light.colorG;
		cl.b = light.colorB;
		cl.intensity = brightness; // use sum of RGB as intensity
		g_fpNumLights++;
	}
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

// Cull lights into 16×16 screen-space tiles (O3DE Atom-style)
static void CullLightsToTiles(int screenW, int screenH,
                               const D3DMATRIX &viewMat, const D3DMATRIX &projMat)
{
	int tilesX = (screenW + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	int tilesY = (screenH + FP_TILE_SIZE - 1) / FP_TILE_SIZE;
	if(tilesX > FP_GRID_W) tilesX = FP_GRID_W;
	if(tilesY > FP_GRID_H) tilesY = FP_GRID_H;

	// Clear tile data
	memset(g_fpTileData, 0, sizeof(g_fpTileData));

	// Camera near/far for depth clipping
	float camNear = 0.5f;   // conservative
	float camFar = 500.0f;  // GTA SA draw distance

	// For each tile, build a frustum slab in view space and test lights
	for(int ty = 0; ty < tilesY; ty++){
		for(int tx = 0; tx < tilesX; tx++){
			int tileIdx = (ty * FP_GRID_W + tx) * 4;
			
			// Tile screen bounds (normalized [0,1])
			float u0 = (float)(tx * FP_TILE_SIZE) / (float)screenW;
			float v0 = (float)(ty * FP_TILE_SIZE) / (float)screenH;
			float u1 = (float)((tx + 1) * FP_TILE_SIZE) / (float)screenW;
			float v1 = (float)((ty + 1) * FP_TILE_SIZE) / (float)screenH;
			
			// Tile view-space frustum corners at near plane
			// Unproject tile corners to view space using inverse projection
			// For a perspective projection: viewX = (2*u-1) * near/right, etc.
			// Simplified: compute tile AABB in view space from UV bounds
			float ndcX0 = u0 * 2.0f - 1.0f;
			float ndcX1 = u1 * 2.0f - 1.0f;
			float ndcY0 = 1.0f - v1 * 2.0f; // flip Y
			float ndcY1 = 1.0f - v0 * 2.0f;
			
			// Extract frustum parameters from projection matrix
			// For standard D3D perspective: proj[0][0] = 2*near/(right-left) ≈ 1/(aspect*tan(fov/2))
			float proj00 = projMat.m[0][0];
			float proj11 = projMat.m[1][1];
			
			if(proj00 < 1e-7f || proj11 < 1e-7f) continue;
			
			// View-space tile bounds at near and far planes
			float viewXNear0 = ndcX0 * camNear / proj00;
			float viewXNear1 = ndcX1 * camNear / proj00;
			float viewYNear0 = ndcY0 * camNear / proj11;
			float viewYNear1 = ndcY1 * camNear / proj11;
			
			float viewXFar0 = ndcX0 * camFar / proj00;
			float viewXFar1 = ndcX1 * camFar / proj00;
			float viewYFar0 = ndcY0 * camFar / proj11;
			float viewYFar1 = ndcY1 * camFar / proj11;
			
			// Tile AABB in view space (union of near and far corners)
			// Note: view space has -Z forward in D3D
			float tileMinX = fminf(fminf(viewXNear0, viewXNear1), fminf(viewXFar0, viewXFar1));
			float tileMaxX = fmaxf(fmaxf(viewXNear0, viewXNear1), fmaxf(viewXFar0, viewXFar1));
			float tileMinY = fminf(fminf(viewYNear0, viewYNear1), fminf(viewYFar0, viewYFar1));
			float tileMaxY = fmaxf(fmaxf(viewYNear0, viewYNear1), fmaxf(viewYFar0, viewYFar1));
			float tileMinZ = -camFar;  // D3D view space: -Z is forward
			float tileMaxZ = -camNear;
			
			// Test each light against this tile's AABB
			int lightCount = 0;
			for(int li = 0; li < g_fpNumLights && lightCount < FP_LIGHTS_PER_TILE; li++){
				const ClusterLight &light = g_fpLights[li];
				
				// Transform light position to view space
				float vx = viewMat.m[0][0]*light.x + viewMat.m[1][0]*light.y + viewMat.m[2][0]*light.z + viewMat.m[3][0];
				float vy = viewMat.m[0][1]*light.x + viewMat.m[1][1]*light.y + viewMat.m[2][1]*light.z + viewMat.m[3][1];
				float vz = viewMat.m[0][2]*light.x + viewMat.m[1][2]*light.y + viewMat.m[2][2]*light.z + viewMat.m[3][2];
				
				// Sphere vs tile AABB test
				if(SphereVsAABB(vx, vy, vz, light.radius, tileMinX, tileMinY, tileMinZ, tileMaxX, tileMaxY, tileMaxZ)){
					g_fpTileData[tileIdx + lightCount] = (unsigned char)(li + 1); // 1-indexed (0 = no light)
					lightCount++;
				}
			}
		}
	}
}

// Upload tile data to GPU texture
static void UploadTileTexture(IDirect3DDevice9 *dev)
{
	if(!g_fpIndexTex) return;
	
	IDirect3DTexture9 *tex = g_fpCurrentBuffer ? g_fpIndexTexB : g_fpIndexTex;
	
	D3DLOCKED_RECT lr;
	if(FAILED(tex->LockRect(0, &lr, NULL, D3DLOCK_DISCARD))){
		dbglog("[ForwardPlus] LockRect failed");
		return;
	}
	
	for(int y = 0; y < FP_GRID_H; y++){
		unsigned char *dst = (unsigned char*)((BYTE*)lr.pBits + y * lr.Pitch);
		const unsigned char *src = &g_fpTileData[y * FP_GRID_W * 4];
		memcpy(dst, src, FP_GRID_W * 4);
	}
	
	tex->UnlockRect(0);
}

// Upload light data as PS constants (c48-c111)
// PS register mapping (VehiclePBR_Modern.hlsl):
//   c48-c79:  clusterLightPos[32] — contiguous (pos.xyz, radius)
//   c80-c111: clusterLightCol[32] — contiguous (col.rgb*intensity, 0)
//   c45:      clusterParams — (tileSize, gridW, gridH, lightCount)
// Lights MUST be uploaded in g_fpLights order: the tile texture stores 1-based
// indices into g_fpLights, so sorting here would silently mis-match them.
// c48+ is verified free — the highest PS constant used elsewhere is c46.
static void UploadLightConstants(void)
{
	g_fpGpuLightCount = (g_fpNumLights < FP_MAX_LIGHTS_GPU) ? g_fpNumLights : FP_MAX_LIGHTS_GPU;

	for(int i = 0; i < g_fpGpuLightCount; i++){
		const ClusterLight &light = g_fpLights[i];
		float posData[4] = { light.x, light.y, light.z, light.radius };
		float colData[4] = { light.r * light.intensity, light.g * light.intensity, light.b * light.intensity, 0.0f };
		RwD3D9SetPixelShaderConstant(48 + i, posData, 1);
		RwD3D9SetPixelShaderConstant(80 + i, colData, 1);
	}

	// (tileSize, gridW, gridH, lightCount) — the shader derives the screen-space
	// tile from VPOS: floor(vpos.xy / tileSize), relative to a 128x128 texture.
	float clusterParams[4] = { (float)FP_TILE_SIZE, (float)FP_GRID_W, (float)FP_GRID_H, (float)g_fpGpuLightCount };
	RwD3D9SetPixelShaderConstant(45, clusterParams, 1);
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

// Main entry point — call each frame before rendering
void ForwardPlus_CullAndUpload(void)
{
	DBGLOG_ENTER("ForwardPlus_CullAndUpload");
	if(!config || !config->forwardPlusEnable){
		// MUST NOT write c45 here: this function runs from RenderScene_before,
		// outside the pipe callbacks, where RwD3D9SetPixelShaderConstant derefs
		// null RW shader state -> READ crash at 0x7FBD4A (+0x60) — the exact
		// hazard documented at the UploadLightConstants call site below. With
		// the feature disabled EnsureFPResources never ran, so there is no
		// bound RW pixel shader whose constant cache could take the write.
		// The gate-close zeroing of c45 (and s5 unbind) is done safely by
		// ForwardPlus_SetConstants' disabled branch, which runs inside the
		// frame render before any draw samples c45 — nothing is lost here.
		return;
	}
	
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!Scene.camera) return;

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
		return;
	}
	if(w < 1 || h < 1) return;
	
	// Ensure textures exist
	EnsureFPResources(dev, w, h);
	if(!g_fpIndexTex) return;

	// Dims-stability guard: when camera dims change, run EnsureFPResources
	// (done above) but skip collect/cull/upload/swap until dims repeat.
	// NOTE: the tile grid itself is NOT cached — CullLightsToTiles recomputes
	// tilesX/tilesY and refills g_fpTileData from the CURRENT w/h every frame,
	// so once real dims are known the grid is rebuilt correctly with no
	// stale-size carryover (the index textures are fixed 128x128 and
	// resolution-independent).
	static int s_fpLastW = 0;
	static int s_fpLastH = 0;
	static int s_fpStableCount = 0;
	if(w != s_fpLastW || h != s_fpLastH) {
		s_fpLastW = w;
		s_fpLastH = h;
		s_fpStableCount = 1;
		return;  // dims just changed — wait for stable repeat
	}
	if(s_fpStableCount < 2) {
		s_fpStableCount++;
		return;  // same dims but not yet repeated enough
	}

	// Get view/proj matrices
	D3DMATRIX viewMat, projMat;
	dev->GetTransform(D3DTS_VIEW, &viewMat);
	dev->GetTransform(D3DTS_PROJECTION, &projMat);

	// Collect lights from game
	CollectLights();

	static int fpLogThrottle = 0;
	if(++fpLogThrottle >= 60){
		dbglog("[ForwardPlus] collected %d lights, gpu=%d rawGameCount=%d",
			g_fpNumLights, g_fpGpuLightCount, g_fpRawCount);
		fpLogThrottle = 0;
	}
	
	// Cull to tiles
	CullLightsToTiles(w, h, viewMat, projMat);
	
	// Upload to GPU — write into current buffer, then swap so SetConstants
	// (called later from render callbacks) reads the PREVIOUS frame's buffer.
	// NOTE (2026-09-14 crash fix): UploadLightConstants uses
	// RwD3D9SetPixelShaderConstant, which is only valid inside the frame
	// render (pipe callbacks). Calling it here in RenderScene_before
	// derefs null RW state -> READ crash at 0x7FBD4A (+0x60). Tile texture
	// upload is pure D3D9 and safe here; constants move to SetConstants.
	UploadTileTexture(dev);
	// UploadLightConstants(); -- deferred to ForwardPlus_SetConstants
	g_fpCurrentBuffer = 1 - g_fpCurrentBuffer;
}

// Called from vehicle/building pipe render callbacks to bind cluster texture
void ForwardPlus_SetConstants(void)
{
	IDirect3DDevice9 *dev = d3d9device;
	if(!config || !config->forwardPlusEnable || !g_fpIndexTex){
		// Defensive: clear stale tile gate (c45) and cluster texture (s5)
		// so shaders fall back to 7-light path instead of magnifying garbage.
		if(dev){
			float zeroParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(45, zeroParams, 1);
			dev->SetTexture(5, NULL);
		}
		return;
	}
	if(!dev) return;

	// Upload cluster light constants (c45, c48-c111). Valid here because
	// pipe callbacks run inside the frame render where RW shader state exists.
	UploadLightConstants();

	// Bind index texture on s5 with POINT filtering
	IDirect3DTexture9 *tex = g_fpCurrentBuffer ? g_fpIndexTexB : g_fpIndexTex;
	dev->SetTexture(5, tex);
	dev->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	dev->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	dev->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}
