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
#define FP_MAX_LIGHTS_GPU 8     // max lights uploaded as PS constants

// D3D resources
static IDirect3DTexture9 *g_fpIndexTex = NULL;    // tile light index texture (128×128 RGBA8)
static IDirect3DTexture9 *g_fpIndexTexB = NULL;   // double buffer
static int g_fpCurrentBuffer = 0;

// Light collection
static ClusterLight g_fpLights[FP_MAX_LIGHTS];
static int g_fpNumLights = 0;

// Tile data (per-tile light indices)
static unsigned char g_fpTileData[FP_GRID_W * FP_GRID_H * 4]; // RGBA8

// Best lights for GPU upload (top FP_MAX_LIGHTS_GPU)
static ClusterLight g_fpGpuLights[FP_MAX_LIGHTS_GPU];
static int g_fpGpuLightCount = 0;

// Release D3D resources
void ForwardPlus_ReleaseResources(void)
{
	if(g_fpIndexTex){ g_fpIndexTex->Release(); g_fpIndexTex = NULL; }
	if(g_fpIndexTexB){ g_fpIndexTexB->Release(); g_fpIndexTexB = NULL; }
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
}

// Collect lights from GTA SA's game systems
static void CollectLights(void)
{
	g_fpNumLights = 0;
	
	// GTA SA stores active point lights in CLights::m_aPointLights
	// The array is at a known address, each entry has position, color, radius, type
	// For now, hook into the game's light pool — this is a placeholder that
	// the real implementation will fill with actual game light extraction.
	
	// TODO: Extract from CPointLights::m_aPointLights (up to 32 entries)
	// TODO: Extract vehicle headlights, muzzle flashes, fire
	// For Phase 1, we just collect from the game's existing light system
	
	// The game's point light pool structure (from gta-reversed):
	// struct CRegisteredPointLight {
	//     CVector position;
	//     CVector direction;  // for spotlights
	//     RwRGBAReal color;
	//     float radius;
	//     unsigned char type; // 0=point, 1=spot
	//     unsigned char fogType;
	//     // ... flags
	// };
	// Array at: CPointLights::m_aPointLights, count: CPointLights::NumLights
	// We'll access this via the addresses from plugin-sdk or direct memory
	
	// For now, create a test light for validation
	// (Remove this once real light extraction is hooked up)
	// g_fpLights[0] = { 0, 5, 0, 20.0f, 1.0f, 0.8f, 0.6f, 2.0f };
	// g_fpNumLights = 1;
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

// Upload light data as PS constants (c29-c44)
static void UploadLightConstants(void)
{
	// Sort lights by brightness (intensity * 1/radius²) and take top FP_MAX_LIGHTS_GPU
	// Simple selection sort — fine for small N
	struct LightScore { int idx; float score; };
	LightScore scores[FP_MAX_LIGHTS];
	for(int i = 0; i < g_fpNumLights; i++){
		scores[i].idx = i;
		float r2 = g_fpLights[i].radius * g_fpLights[i].radius;
		scores[i].score = g_fpLights[i].intensity / fmaxf(r2, 1.0f);
	}
	// Partial sort — find top FP_MAX_LIGHTS_GPU
	for(int i = 0; i < FP_MAX_LIGHTS_GPU && i < g_fpNumLights; i++){
		int bestIdx = i;
		for(int j = i + 1; j < g_fpNumLights; j++){
			if(scores[j].score > scores[bestIdx].score) bestIdx = j;
		}
		if(bestIdx != i){
			LightScore tmp = scores[i];
			scores[i] = scores[bestIdx];
			scores[bestIdx] = tmp;
		}
	}
	
	g_fpGpuLightCount = (g_fpNumLights < FP_MAX_LIGHTS_GPU) ? g_fpNumLights : FP_MAX_LIGHTS_GPU;
	
	for(int i = 0; i < g_fpGpuLightCount; i++){
		const ClusterLight &light = g_fpLights[scores[i].idx];
		// c29 + 2*i = position.xyz + radius
		float posData[4] = { light.x, light.y, light.z, light.radius };
		// c30 + 2*i = color.rgb * intensity
		float colData[4] = { light.r * light.intensity, light.g * light.intensity, light.b * light.intensity, 0.0f };
		RwD3D9SetPixelShaderConstant(29 + i*2, posData, 1);
		RwD3D9SetPixelShaderConstant(30 + i*2, colData, 1);
	}
	
	// Upload cluster params at c45: (gridOffsetX, gridOffsetZ, tileSize, lightCount)
	float clusterParams[4] = { 0.0f, 0.0f, (float)FP_TILE_SIZE, (float)g_fpGpuLightCount };
	RwD3D9SetPixelShaderConstant(45, clusterParams, 1);
}

// Main entry point — call each frame before rendering
void ForwardPlus_CullAndUpload(void)
{
	if(!config || !config->forwardPlusEnable)
		return;
	
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	if(!Scene.camera) return;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1) return;
	
	// Ensure textures exist
	EnsureFPResources(dev, w, h);
	if(!g_fpIndexTex) return;
	
	// Get view/proj matrices
	D3DMATRIX viewMat, projMat;
	dev->GetTransform(D3DTS_VIEW, &viewMat);
	dev->GetTransform(D3DTS_PROJECTION, &projMat);
	
	// Collect lights from game
	CollectLights();
	
	// Cull to tiles
	CullLightsToTiles(w, h, viewMat, projMat);
	
	// Upload to GPU — write into current buffer, then swap so SetConstants
	// (called later from render callbacks) reads the PREVIOUS frame's buffer
	UploadTileTexture(dev);
	UploadLightConstants();
	g_fpCurrentBuffer = 1 - g_fpCurrentBuffer;
}

// Called from vehicle/building pipe render callbacks to bind cluster texture
void ForwardPlus_SetConstants(void)
{
	if(!config || !config->forwardPlusEnable)
		return;
	if(!g_fpIndexTex) return;
	
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return;
	
	// Bind index texture on s5 with POINT filtering
	IDirect3DTexture9 *tex = g_fpCurrentBuffer ? g_fpIndexTexB : g_fpIndexTex;
	dev->SetTexture(5, tex);
	dev->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	dev->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	dev->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}
