// ============================================================================
// Utility Noise Texture (docs/plans/effects-menu-overhaul.md §2)
//
// One runtime-generated RGBA noise tile, channel-packed per pixel as
// (a<<24)|(r<<16)|(g<<8)|b — the D3DFMT_A8R8G8B8 byte order of the RW PC
// raster (little-endian store emits B,G,R,A bytes):
//   R = tileable Perlin fBm (6 octaves, periods 4..128)  — clouds macro shape,
//       vehicle dirt large grime
//   G = tileable Worley/cellular F1 (8x8 cells)          — erosion/detail,
//       cloud cauliflower edges, water caustics, rain droplets
//   B = white noise (integer hash per pixel)             — SSAO/raymarch/PCF
//       kernel jitter
//   A = Bayer 8x8 (blue-noise-ish)                       — screen-door dither
//
// Consumers (water/SSAO/clouds/vehicle dither) are Wave 2 — nothing binds
// g_pUtilityNoise yet. Globals: g_pUtilityNoise (texture), g_CurrentNoiseSize
// (its resolution, 0 = not generated).
//
// tiling: the Perlin lattice wraps by its octave period and the Worley cells
// wrap by the cell count, so both channels are seamless when sampled with
// rwTEXTUREADDRESSWRAP at any UV scale.
// ============================================================================

#include "skygfx.h"

RwTexture *g_pUtilityNoise = NULL;
int g_CurrentNoiseSize = 0;

// ---------------------------------------------------------------------------
// small deterministic hashes / noise primitives
// ---------------------------------------------------------------------------

// integer hash of a wrapped lattice/cell coordinate -> avalanche (both
// channels below depend only on WRAPPED coords, which is what makes the
// pattern tile: copies of a cell hash identically)
static inline uint32
hash2u(uint32 x, uint32 y)
{
	uint32 h = x * 0x8DA6B343u + y * 0xD8163841u;
	h = (h ^ (h >> 13)) * 0x9E3779B1u;
	h ^= h >> 16;
	return h;
}

static inline float
fadef(float t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);	// Perlin quintic
}

static inline float
clampf01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// 8 unit gradients (axes + diagonals)
static inline void
grad2(uint32 h, float *gx, float *gy)
{
	static const float dirs[8][2] = {
		{ 1.0f,       0.0f       }, { -1.0f,      0.0f       },
		{ 0.0f,       1.0f       }, {  0.0f,     -1.0f       },
		{ 0.70710678f, 0.70710678f }, { -0.70710678f, 0.70710678f },
		{ 0.70710678f,-0.70710678f }, { -0.70710678f,-0.70710678f },
	};
	const float *d = dirs[h & 7];
	*gx = d[0];
	*gy = d[1];
}

// Tileable 2D gradient (Perlin) noise on a lattice of `period` cells across
// [0,1). Lattice indices wrap by `period`, so the field is periodic. Returns
// roughly [-1, 1].
static float
perlinTile(float u, float v, int period)
{
	float x = u * period, y = v * period;
	int xi = (int)floorf(x), yi = (int)floorf(y);
	float xf = x - xi, yf = y - yi;

	// wrap lattice coords (u,v in [0,1) -> xi,yi already in [0,period-1])
	int x0 = ((xi % period) + period) % period;
	int y0 = ((yi % period) + period) % period;
	int x1 = (x0 + 1) % period;
	int y1 = (y0 + 1) % period;

	float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
	grad2(hash2u((uint32)x0, (uint32)y0), &g00x, &g00y);
	grad2(hash2u((uint32)x1, (uint32)y0), &g10x, &g10y);
	grad2(hash2u((uint32)x0, (uint32)y1), &g01x, &g01y);
	grad2(hash2u((uint32)x1, (uint32)y1), &g11x, &g11y);

	// dot products of offset-to-sample with each corner gradient
	float n00 = g00x * xf       + g00y * yf;
	float n10 = g10x * (xf-1.0f)+ g10y * yf;
	float n01 = g01x * xf       + g01y * (yf-1.0f);
	float n11 = g11x * (xf-1.0f)+ g11y * (yf-1.0f);

	float s = fadef(xf), t = fadef(yf);
	float nx0 = n00 + s * (n10 - n00);
	float nx1 = n01 + s * (n11 - n01);
	return nx0 + t * (nx1 - nx0);
}

// R channel: 6-octave fBm over perlinTile (periods 4,8,16,32,64,128),
// persistence 0.5, normalized to ~[-1,1]. Real Perlin — keeps 512/1024
// textures crisp (highest octave: 8 px/cell at 1024).
static float
perlinFbm(float u, float v)
{
	float sum = 0.0f, amp = 1.0f, norm = 0.0f;
	int period = 4;
	for(int o = 0; o < 6; o++){
		sum += amp * perlinTile(u, v, period);
		norm += amp;
		amp *= 0.5f;
		period *= 2;
	}
	return sum / norm;
}

// Tileable Worley/cellular F1 (distance to nearest feature point) on an
// 8x8 grid of cells with one hashed point per cell. Neighbor cell coords wrap,
// and the point offset comes from the WRAPPED cell hash, so feature points
// repeat identically across tile edges. Returns [0, ~1.2].
static float
worleyTile(float u, float v, int cells)
{
	float fx = u * cells, fy = v * cells;
	int cx = (int)floorf(fx), cy = (int)floorf(fy);
	float minD = 1e9f;
	for(int oy = -1; oy <= 1; oy++){
		for(int ox = -1; ox <= 1; ox++){
			int gx = cx + ox, gy = cy + oy;
			int wx = ((gx % cells) + cells) % cells;
			int wy = ((gy % cells) + cells) % cells;
			uint32 h = hash2u((uint32)wx, (uint32)wy);
			// feature point inside cell (gx,gy) — copies share the wrapped hash
			float px = gx + ((h & 1023u) * (1.0f / 1023.0f));
			float py = gy + (((h >> 10) & 1023u) * (1.0f / 1023.0f));
			float dx = px - fx, dy = py - fy;
			float d = dx * dx + dy * dy;
			if(d < minD) minD = d;
		}
	}
	return sqrtf(minD);
}

// A channel: Bayer 8x8 ordered-dither matrix (values 0..63)
static const uint8 s_bayer8[64] = {
	 0, 32,  8, 40,  2, 34, 10, 42,
	48, 16, 56, 24, 50, 18, 58, 26,
	12, 44,  4, 36, 14, 46,  6, 38,
	60, 28, 52, 20, 62, 30, 54, 22,
	 3, 35, 11, 43,  1, 33,  9, 41,
	51, 19, 59, 27, 49, 17, 57, 25,
	15, 47,  7, 39, 13, 45,  5, 37,
	63, 31, 55, 23, 61, 29, 53, 21,
};

// ---------------------------------------------------------------------------
// GenerateUtilityTexture
//   res clamped to 32..1024. Destroys + frees any previous texture first.
//   RwRasterCreate(w,h,32, rwRASTERTYPETEXTURE|rwRASTERFORMAT8888) ->
//   RwRasterLock(WRITE) fill (stride-aware!) -> RwRasterUnlock ->
//   RwTextureCreate -> linear-mip-linear filter + wrap addressing.
// ---------------------------------------------------------------------------
void
GenerateUtilityTexture(int res)
{
	if(res < 32) res = 32;
	if(res > 1024) res = 1024;

	// RW must be up (raster/texture creation runs game-exe RW code). The
	// refreshIni caller retries while this stays null, so skipping here is
	// safe and self-healing.
	if(!d3d9device){
		if(dbglog_throttle("unoise_nodev"))
			dbglog("[UtilityNoise] SKIP: d3d9device null (RW not ready), res=%d", res);
		return;
	}

	double t0 = perfNow();
	int oldSize = g_CurrentNoiseSize;

	// destroy + free old texture on regen (RwTextureDestroy frees its raster)
	if(g_pUtilityNoise){
		RwTextureDestroy(g_pUtilityNoise);
		g_pUtilityNoise = NULL;
		g_CurrentNoiseSize = 0;
	}

	RwRaster *raster = RwRasterCreate(res, res, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
	if(!raster){
		dbglog("[UtilityNoise] ERROR: RwRasterCreate(%dx%d) FAILED", res, res);
		return;
	}

	RwUInt8 *pixels = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
	if(!pixels){
		dbglog("[UtilityNoise] ERROR: RwRasterLock FAILED (%dx%d)", res, res);
		RwRasterDestroy(raster);
		return;
	}

	// stride is in BYTES and may exceed width*4 — never assume row width
	// equals the pixel count
	const int stridePix = RwRasterGetStride(raster) / 4;

	for(int y = 0; y < res; y++){
		uint32 *row = (uint32*)pixels + y * stridePix;
		float v = (float)y / (float)res;
		for(int x = 0; x < res; x++){
			float u = (float)x / (float)res;

			// R = Perlin fBm ~[-1,1] -> [0,1]
			float rf = clampf01(perlinFbm(u, v) * 0.5f + 0.5f);
			// G = Worley F1, normalized over the ~0.75 typical cell radius
			float gf = clampf01(worleyTile(u, v, 8) * (1.0f / 0.75f));
			// B = white noise, integer hash
			uint32 b = hash2u((uint32)x, (uint32)y) & 0xFFu;
			// A = Bayer 8x8 -> 0..255
			uint32 a = ((uint32)s_bayer8[(y & 7) * 8 + (x & 7)] * 255u) / 63u;

			uint32 r = (uint32)(rf * 255.0f + 0.5f);
			uint32 g = (uint32)(gf * 255.0f + 0.5f);

			row[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	RwRasterUnlock(raster);

	RwTexture *tex = RwTextureCreate(raster);
	if(!tex){
		dbglog("[UtilityNoise] ERROR: RwTextureCreate FAILED");
		RwRasterDestroy(raster);
		return;
	}
	RwTextureSetFilterMode(tex, rwFILTERLINEARMIPLINEAR);
	RwTextureSetAddressing(tex, rwTEXTUREADDRESSWRAP);

	g_pUtilityNoise = tex;
	g_CurrentNoiseSize = res;

	dbglog("[UtilityNoise] %s %dx%d tex=%p raster=%p (%.1f ms, stride=%d B/row) "
	       "R=Perlin-fBm G=Worley-F1 B=white A=Bayer8",
	       oldSize ? "resized" : "generated", res, res, tex, raster,
	       perfNow() - t0, RwRasterGetStride(raster));
}
