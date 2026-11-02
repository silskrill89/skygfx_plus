// ============================================================================
// Universal noise service (src/render/noise.h)
//
// One deterministic CPU noise core. Produces:
//   * scalar generators (Perlin/fBm/Worley/white/blue) usable with no device
//   * one packed RGBA utility tile for the GPU (R=Perlin G=Worley B=white
//     A=blue) so a cloud/stochastic shader needs 1 sampler + 3 fetches
//
// "Infinite detail" recipe the shader side uses:
//   sample the same tile at 3 scales and sum -> 3 fetches instead of 100+ ALU.
//
// WHY A BLUE-NOISE MASK INSTEAD OF THE OLD BAYER 8x8:
//   Bayer is a regular ordered pattern: it dithers but leaves structured
//   cross-hatch artefacts and aliases when minified. Blue noise pushes the
//   error energy into high frequencies, which TAA/mip filtering removes
//   smoothly. A tileable 64x64 blue mask is produced here at init (see
//   BuildBlueNoise) so there is no runtime cost.
//
// ALGORITHMS (documented so the HLSL mirror can match constants)
//   Perlin 2D: 8 unit gradients (axes + diagonals), quintic fade
//              6t^5-15t^4+10t^3, lattice index wrap & 255, 512-entry seeded
//              permutation table. Tileable variant wraps the lattice by the
//              integer period and hashes the WRAPPED corners, so the field is
//              exactly periodic.
//   Perlin 3D: classic 12 edge gradients, normalised to unit length, same fade.
//   fBm:       sum(amp * noise(freq * p)), amp *= gain, freq *= lacunarity,
//              divided by sum(amp) so amplitude is stable at every octave
//              count. Octaves clamped to 1..8.
//   Worley:    F1 over a 3x3 cell neighbourhood. One feature point per cell,
//              offset from an integer hash of (cellX, cellY, seed). Cells wrap
//              by the requested period; feature offsets use the WRAPPED hash
//              while the position is anchored in the unwrapped cell, so the
//              field is seamless.
//   White:     x*374761393 + y*668265263, xor-shift 13, *1274126177, xor-shift
//              16 -> 24 bits -> [0,1] (the standard fast 2D hash; seed is
//              folded into the sum so SetSeed regenerates it).
//   Blue:      Mitchell best-candidate. Details at BuildBlueNoise.
//
// NORMALISATION / MEASURED SCALE
//   With unit-length gradients the largest possible |dot| inside one lattice
//   cell is sqrt(2)/2 = 0.70711 in 2D and sqrt(3)/2 = 0.86603 in 3D (0.5,
//   0.5[, 0.5] offset times unit gradient, maximised over the corner set).
//   The signed generators are multiplied by 1/0.70711 and 1/0.86603
//   respectively and then mapped with *0.5+0.5, so [0,1] is the full range and
//   clipping is rare. These constants are the measured envelope, not a
//   statistical fit: no per-tile min/max pass is performed (that would make
//   the look depend on resolution and destroy tile-to-tile continuity).
//
// THREADING
//   Not thread safe. The project calls this from the render thread only.
// ============================================================================

#include "skygfx.h"
#include "noise.h"

#include <math.h>
#include <string.h>

namespace SkyNoise {

// ---------------------------------------------------------------------------
// constants / state
// ---------------------------------------------------------------------------

static const int kMaxSize = 128;         // NQ_HIGH; also the static buffer cap
static const int kBlueCandidates = 12;   // K (8..16 is the sweet spot for 64^2)
static const unsigned kDefaultSeed = 1337u;

static const float kSqrtHalf = 0.70710678f;
static const float kPerlin2Scale = 1.41421356f;   // 1 / 0.70710678
static const float kPerlin3Scale = 1.15470054f;   // 1 / 0.86602540
static const float kEpsilon = 1e-7f;

static bool s_initialized = false;
static bool s_tablesReady = false;
static bool s_dataReady = false;
static bool s_blueReady = false;

static unsigned s_seed = kDefaultSeed;
static NoiseQuality s_quality = NQ_MEDIUM;
static int s_size = 64;

static unsigned char s_perm[512];
static unsigned char s_rgba[kMaxSize * kMaxSize * 4];

static IDirect3DTexture9 *s_texture = NULL;

static unsigned char s_blueValue[kMaxSize * kMaxSize];
static unsigned char s_bluePlaced[kMaxSize * kMaxSize];
static float s_blueDist[kMaxSize * kMaxSize];
static int s_blueSize = 0;
static unsigned s_blueSeed = 0;

static Stats s_stats = { 0, 0, kDefaultSeed, 0, (int)NQ_MEDIUM, 0 };

// ---------------------------------------------------------------------------
// small math helpers
// ---------------------------------------------------------------------------

static inline float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float lerpf(float a, float b, float t)
{
	return a + t * (b - a);
}

// Perlin quintic fade: 6t^5 - 15t^4 + 10t^3 (C2 continuous).
static inline float fade(float t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Positive modulo for lattice/cell wrapping.
static inline int wrapi(int v, int period)
{
	int m = v % period;
	return m < 0 ? m + period : m;
}

static inline unsigned char toByte(float v)
{
	int i = (int)(clamp01(v) * 255.0f + 0.5f);
	if(i < 0) i = 0;
	if(i > 255) i = 255;
	return (unsigned char)i;
}

static int clampOctaves(int octaves)
{
	// Hard cap: the shader mirror unrolls 8 iterations; more is wasted on a
	// 64/128 px tile anyway.
	if(octaves < 1) return 1;
	if(octaves > 8) return 8;
	return octaves;
}

static int qualitySize(NoiseQuality q)
{
	switch(q){
	case NQ_LOW:    return 32;
	case NQ_HIGH:   return 128;
	case NQ_MEDIUM:
	default:        return 64;
	}
}

// ---------------------------------------------------------------------------
// deterministic integer hashes / RNG (no rand(), no time)
// ---------------------------------------------------------------------------

// xorshift32; state must be non-zero.
static inline unsigned nextRand(unsigned &state)
{
	unsigned x = state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return x;
}

// Avalanche hash of a wrapped lattice/cell coordinate with the global seed
// folded in. Only WRAPPED coordinates are hashed by tileable code paths, which
// is what makes their fields repeat exactly.
static inline unsigned hashCell(unsigned x, unsigned y)
{
	unsigned h = x * 0x8DA6B343u + y * 0xD8163841u + s_seed * 0x9E3779B1u;
	h = (h ^ (h >> 13)) * 0x9E3779B1u;
	h ^= h >> 16;
	return h;
}

// Seeded Fisher-Yates shuffle of 0..255, duplicated to 512 entries so
// perm[perm[X] + Y] never needs an explicit mask after the sum.
static void buildPermutation(void)
{
	unsigned state = s_seed * 0x9E3779B1u + 0x6D2B79F5u;
	if(state == 0) state = 0x1234567u;

	for(int i = 0; i < 256; i++)
		s_perm[i] = (unsigned char)i;

	for(int i = 255; i > 0; i--){
		unsigned j = nextRand(state) % (unsigned)(i + 1);
		unsigned char tmp = s_perm[i];
		s_perm[i] = s_perm[j];
		s_perm[j] = tmp;
	}
	for(int i = 0; i < 256; i++)
		s_perm[i + 256] = s_perm[i];
}

static void ensureTables(void)
{
	if(s_tablesReady) return;
	buildPermutation();
	s_tablesReady = true;
}

// ---------------------------------------------------------------------------
// gradients
// ---------------------------------------------------------------------------

// 8 unit directions: axes + diagonals.
static const float kGrad2[8][2] = {
	{  1.0f,        0.0f        },
	{ -1.0f,        0.0f        },
	{  0.0f,        1.0f        },
	{  0.0f,       -1.0f        },
	{  kSqrtHalf,   kSqrtHalf   },
	{ -kSqrtHalf,   kSqrtHalf   },
	{  kSqrtHalf,  -kSqrtHalf   },
	{ -kSqrtHalf,  -kSqrtHalf   },
};

// Classic 12 edge gradients, normalised to unit length.
static const float kGrad3[12][3] = {
	{  kSqrtHalf,  kSqrtHalf, 0.0f },
	{ -kSqrtHalf,  kSqrtHalf, 0.0f },
	{  kSqrtHalf, -kSqrtHalf, 0.0f },
	{ -kSqrtHalf, -kSqrtHalf, 0.0f },
	{  kSqrtHalf, 0.0f,  kSqrtHalf },
	{ -kSqrtHalf, 0.0f,  kSqrtHalf },
	{  kSqrtHalf, 0.0f, -kSqrtHalf },
	{ -kSqrtHalf, 0.0f, -kSqrtHalf },
	{ 0.0f,  kSqrtHalf,  kSqrtHalf },
	{ 0.0f, -kSqrtHalf,  kSqrtHalf },
	{ 0.0f,  kSqrtHalf, -kSqrtHalf },
	{ 0.0f, -kSqrtHalf, -kSqrtHalf },
};

// ---------------------------------------------------------------------------
// Perlin (gradient) noise — signed, roughly [-1, 1]
// ---------------------------------------------------------------------------

static float perlin2Signed(float x, float y)
{
	float fx = floorf(x);
	float fy = floorf(y);

	int X = ((int)fx) & 255;
	int Y = ((int)fy) & 255;

	float xf = x - fx;
	float yf = y - fy;

	// perm[] is duplicated (512 entries), so X+1 / A+1 wrap for free and the
	// sum index max 510 stays in range.
	int A = s_perm[X] + Y;
	int B = s_perm[X + 1] + Y;

	int h00 = s_perm[A];
	int h10 = s_perm[B];
	int h01 = s_perm[A + 1];
	int h11 = s_perm[B + 1];

	const float *g;
	float n00, n10, n01, n11;

	g = kGrad2[h00 & 7];
	n00 = g[0] * xf + g[1] * yf;
	g = kGrad2[h10 & 7];
	n10 = g[0] * (xf - 1.0f) + g[1] * yf;
	g = kGrad2[h01 & 7];
	n01 = g[0] * xf + g[1] * (yf - 1.0f);
	g = kGrad2[h11 & 7];
	n11 = g[0] * (xf - 1.0f) + g[1] * (yf - 1.0f);

	float sx = fade(xf);
	float sy = fade(yf);
	float nx0 = lerpf(n00, n10, sx);
	float nx1 = lerpf(n01, n11, sx);
	return lerpf(nx0, nx1, sy);
}

// Seamless 2D Perlin. (u,v) is in tile space; `period` lattice cells span one
// tile. Both lattice corners are hashed through the permutation of their
// WRAPPED coordinates, giving an exactly periodic field.
static float perlinTileSigned(float u, float v, int period)
{
	if(period < 1) period = 1;
	if(period > 256) period = 256;	// permutation table window

	float x = u * (float)period;
	float y = v * (float)period;
	float fx = floorf(x);
	float fy = floorf(y);

	int x0 = wrapi((int)fx, period);
	int y0 = wrapi((int)fy, period);
	int x1 = (x0 + 1) % period;
	int y1 = (y0 + 1) % period;

	float xf = x - fx;
	float yf = y - fy;

	int h00 = s_perm[s_perm[x0] + y0];
	int h10 = s_perm[s_perm[x1] + y0];
	int h01 = s_perm[s_perm[x0] + y1];
	int h11 = s_perm[s_perm[x1] + y1];

	const float *g;
	float n00, n10, n01, n11;

	g = kGrad2[h00 & 7];
	n00 = g[0] * xf + g[1] * yf;
	g = kGrad2[h10 & 7];
	n10 = g[0] * (xf - 1.0f) + g[1] * yf;
	g = kGrad2[h01 & 7];
	n01 = g[0] * xf + g[1] * (yf - 1.0f);
	g = kGrad2[h11 & 7];
	n11 = g[0] * (xf - 1.0f) + g[1] * (yf - 1.0f);

	float sx = fade(xf);
	float sy = fade(yf);
	float nx0 = lerpf(n00, n10, sx);
	float nx1 = lerpf(n01, n11, sx);
	return lerpf(nx0, nx1, sy);
}

static float perlin3Signed(float x, float y, float z)
{
	float fx = floorf(x);
	float fy = floorf(y);
	float fz = floorf(z);

	int X = ((int)fx) & 255;
	int Y = ((int)fy) & 255;
	int Z = ((int)fz) & 255;

	float xf = x - fx;
	float yf = y - fy;
	float zf = z - fz;

	int A = s_perm[X] + Y;
	int B = s_perm[X + 1] + Y;
	int AA = s_perm[A] + Z;
	int AB = s_perm[A + 1] + Z;
	int BA = s_perm[B] + Z;
	int BB = s_perm[B + 1] + Z;

	int h000 = s_perm[AA];
	int h100 = s_perm[BA];
	int h010 = s_perm[AB];
	int h110 = s_perm[BB];
	int h001 = s_perm[AA + 1];
	int h101 = s_perm[BA + 1];
	int h011 = s_perm[AB + 1];
	int h111 = s_perm[BB + 1];

	const float *g;
	float n000, n100, n010, n110, n001, n101, n011, n111;

	g = kGrad3[h000 % 12];
	n000 = g[0] * xf + g[1] * yf + g[2] * zf;
	g = kGrad3[h100 % 12];
	n100 = g[0] * (xf - 1.0f) + g[1] * yf + g[2] * zf;
	g = kGrad3[h010 % 12];
	n010 = g[0] * xf + g[1] * (yf - 1.0f) + g[2] * zf;
	g = kGrad3[h110 % 12];
	n110 = g[0] * (xf - 1.0f) + g[1] * (yf - 1.0f) + g[2] * zf;
	g = kGrad3[h001 % 12];
	n001 = g[0] * xf + g[1] * yf + g[2] * (zf - 1.0f);
	g = kGrad3[h101 % 12];
	n101 = g[0] * (xf - 1.0f) + g[1] * yf + g[2] * (zf - 1.0f);
	g = kGrad3[h011 % 12];
	n011 = g[0] * xf + g[1] * (yf - 1.0f) + g[2] * (zf - 1.0f);
	g = kGrad3[h111 % 12];
	n111 = g[0] * (xf - 1.0f) + g[1] * (yf - 1.0f) + g[2] * (zf - 1.0f);

	float sx = fade(xf);
	float sy = fade(yf);
	float sz = fade(zf);

	float nx00 = lerpf(n000, n100, sx);
	float nx10 = lerpf(n010, n110, sx);
	float nx01 = lerpf(n001, n101, sx);
	float nx11 = lerpf(n011, n111, sx);
	float nxy0 = lerpf(nx00, nx10, sy);
	float nxy1 = lerpf(nx01, nx11, sy);
	return lerpf(nxy0, nxy1, sz);
}

// ---------------------------------------------------------------------------
// Worley / cellular F1 — signed internal (0..~1), periodic when period > 0
// ---------------------------------------------------------------------------

static float worleyF1(float x, float y, int period)
{
	float fx = floorf(x);
	float fy = floorf(y);
	int cx = (int)fx;
	int cy = (int)fy;

	float minD2 = 1e9f;

	for(int oy = -1; oy <= 1; oy++){
		for(int ox = -1; ox <= 1; ox++){
			int gx = cx + ox;
			int gy = cy + oy;

			// Hash the wrapped cell so copies across the tile edge produce the
			// same feature offset, but anchor the point in the unwrapped cell.
			int hx = period > 0 ? wrapi(gx, period) : gx;
			int hy = period > 0 ? wrapi(gy, period) : gy;
			unsigned h = hashCell((unsigned)hx, (unsigned)hy);

			float px = (float)gx + (float)(h & 1023u) * (1.0f / 1023.0f);
			float py = (float)gy + (float)((h >> 10) & 1023u) * (1.0f / 1023.0f);

			float dx = px - fx;
			float dy = py - fy;
			float d2 = dx * dx + dy * dy;
			if(d2 < minD2) minD2 = d2;
		}
	}

	return sqrtf(minD2);
}

// ---------------------------------------------------------------------------
// blue noise — Mitchell best-candidate, tileable
// ---------------------------------------------------------------------------
//
// Algorithm
//   1. Keep a toroidal distance field: dist[p] = squared distance from pixel p
//      to the nearest already-placed sample.
//   2. For each of the N*N samples, draw K=12 uniformly random pixels and pick
//      the one whose dist[] is largest (Mitchell's best-candidate).
//   3. Place it, store its rank mapped to 0..255, then update dist[] for every
//      pixel using toroidal (wrap-around) distance so the tile is seamless.
//
// Complexity
//   Candidate selection is O(N^2 * K) distance lookups in total.
//   The distance-field update is O(N^4) worst case: every placement touches
//   the whole grid. That is 16.8M float compares at 64x64 (a few ms) and
//   ~268M at 128x128 (tens of ms, NQ_HIGH only) — paid once per seed/size at
//   init, never per frame. A k-d tree or incremental stamping would lower the
//   constant but is not worth the code for a one-shot bake.
//
// Output is a permutation of 0..N*N-1 mapped to [0,1]: every rank used exactly
// once, so the channel is an unbiased dither source at any threshold.

static void buildBlueNoise(int n)
{
	if(n < 8) n = 8;
	if(n > kMaxSize) n = kMaxSize;

	const int total = n * n;

	unsigned rng = s_seed * 0x9E3779B1u + 0x85EBCA6Bu;
	if(rng == 0) rng = 0x6C8E9CF5u;

	for(int i = 0; i < total; i++){
		s_blueDist[i] = 1e9f;
		s_bluePlaced[i] = 0;
		s_blueValue[i] = 0;
	}

	for(int rank = 0; rank < total; rank++){
		int best = -1;
		float bestD = -1.0f;

		for(int k = 0; k < kBlueCandidates; k++){
			int idx = (int)(nextRand(rng) % (unsigned)total);
			if(s_bluePlaced[idx]) continue;
			if(s_blueDist[idx] > bestD){
				bestD = s_blueDist[idx];
				best = idx;
			}
		}

		// Extremely unlikely (all K candidates already placed): linear fallback.
		if(best < 0){
			for(int i = 0; i < total; i++){
				if(!s_bluePlaced[i]){ best = i; break; }
			}
		}
		if(best < 0) break;

		s_bluePlaced[best] = 1;
		s_blueValue[best] = (unsigned char)((rank * 255) / (total - 1));

		int px = best % n;
		int py = best / n;

		for(int y = 0; y < n; y++){
			int dy = y - py;
			if(dy < 0) dy = -dy;
			if(dy > n / 2) dy = n - dy;
			int row = y * n;
			for(int x = 0; x < n; x++){
				int dx = x - px;
				if(dx < 0) dx = -dx;
				if(dx > n / 2) dx = n - dx;
				float d2 = (float)(dx * dx + dy * dy);
				if(d2 < s_blueDist[row + x])
					s_blueDist[row + x] = d2;
			}
		}
	}
}

static void ensureBlueNoise(void)
{
	if(s_blueReady && s_blueSize == s_size && s_blueSeed == s_seed)
		return;
	buildBlueNoise(s_size);
	s_blueSize = s_size;
	s_blueSeed = s_seed;
	s_blueReady = true;
}

// ---------------------------------------------------------------------------
// public scalar generators
// ---------------------------------------------------------------------------

float Perlin2(float x, float y)
{
	ensureTables();
	float n = perlin2Signed(x, y) * kPerlin2Scale;
	return clamp01(n * 0.5f + 0.5f);
}

float Perlin3(float x, float y, float z)
{
	ensureTables();
	float n = perlin3Signed(x, y, z) * kPerlin3Scale;
	return clamp01(n * 0.5f + 0.5f);
}

float PerlinTileable2(float x, float y, int period)
{
	ensureTables();
	float n = perlinTileSigned(x, y, period) * kPerlin2Scale;
	return clamp01(n * 0.5f + 0.5f);
}

float Fbm2(float x, float y, int octaves, float lacunarity, float gain)
{
	ensureTables();

	int n = clampOctaves(octaves);
	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	float freq = 1.0f;

	for(int o = 0; o < n; o++){
		sum += amp * perlin2Signed(x * freq, y * freq);
		norm += amp;
		amp *= gain;
		freq *= lacunarity;
	}

	if(norm < kEpsilon) norm = kEpsilon;
	float v = (sum / norm) * kPerlin2Scale;
	return clamp01(v * 0.5f + 0.5f);
}

float FbmTileable2(float x, float y, int octaves, int period)
{
	ensureTables();

	int n = clampOctaves(octaves);
	if(period < 1) period = 1;

	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	int p = period;

	for(int o = 0; o < n; o++){
		sum += amp * perlinTileSigned(x, y, p);
		norm += amp;
		amp *= 0.5f;
		p *= 2;
		if(p > 256) p = 256;	// lattice wrap limit of the permutation table
	}

	if(norm < kEpsilon) norm = kEpsilon;
	float v = (sum / norm) * kPerlin2Scale;
	return clamp01(v * 0.5f + 0.5f);
}

float Worley2(float x, float y)
{
	return clamp01(worleyF1(x, y, 0));
}

float WorleyFbm2(float x, float y, int octaves)
{
	int n = clampOctaves(octaves);

	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	float freq = 1.0f;

	for(int o = 0; o < n; o++){
		sum += amp * Worley2(x * freq, y * freq);
		norm += amp;
		amp *= 0.5f;
		freq *= 2.0f;
	}

	if(norm < kEpsilon) norm = kEpsilon;
	return clamp01(sum / norm);
}

float White2(int x, int y)
{
	// Standard fast 2D integer hash; the seed is folded into the sum so a
	// reseed changes the B channel too (still fully deterministic).
	unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
	h += s_seed * 0x9E3779B9u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (float)(h & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

float BlueNoise2(int x, int y)
{
	ensureBlueNoise();

	int n = s_blueSize > 0 ? s_blueSize : s_size;
	// Sizes are 32/64/128 (powers of two), so masking replaces modulo.
	int xi = x & (n - 1);
	int yi = y & (n - 1);
	return (float)s_blueValue[yi * n + xi] * (1.0f / 255.0f);
}

// ---------------------------------------------------------------------------
// packed RGBA tile
// ---------------------------------------------------------------------------

static void buildUniversal(void)
{
	ensureTables();

	int size = qualitySize(s_quality);
	s_size = size;
	ensureBlueNoise();

	unsigned char *out = s_rgba;

	for(int y = 0; y < size; y++){
		float v = (float)y / (float)size;
		for(int x = 0; x < size; x++){
			float u = (float)x / (float)size;

			// R: 4-octave tileable Perlin fBm, 4 base cells per tile (x/size*4
			// maps one tile to the [0,4) lattice domain).
			float perlin = FbmTileable2(u * 4.0f, v * 4.0f, 4, 4);

			// G: periodic Worley F1, 8 cells across the tile. Use the periodic
			// internal form so both tile edges match (public Worley2 is
			// unbounded by contract).
			float worley = clamp01(worleyF1(u * 8.0f, v * 8.0f, 8));

			// B: white noise, A: blue noise (both deterministic per pixel).
			float white = White2(x, y);
			float blue = BlueNoise2(x, y);

			unsigned char *p = out + ((size_t)y * (size_t)size + (size_t)x) * 4;

			// BGRA byte order so D3DFMT_A8R8G8B8 decodes to
			// .r=Perlin .g=Worley .b=white .a=blue in the shader.
			p[0] = toByte(white);
			p[1] = toByte(worley);
			p[2] = toByte(perlin);
			p[3] = toByte(blue);
		}
	}

	s_dataReady = true;
	s_stats.size = (unsigned)size;
	s_stats.seed = s_seed;
	s_stats.quality = (int)s_quality;

	dbglog("[Noise] rebuild %dx%d quality=%d seed=%u (R=Perlin G=Worley B=white A=blue)",
	       size, size, (int)s_quality, s_seed);
}

// ---------------------------------------------------------------------------
// D3D texture service
// ---------------------------------------------------------------------------

static void releaseTexture(void)
{
	if(s_texture){
		s_texture->Release();
		s_texture = NULL;
	}
	s_stats.textureLive = 0;
}

// Create/upload the POOL_DEFAULT texture from the cached CPU bytes.
// Returns 1 on success; safe to call with d3d9device == NULL (returns 0).
static int ensureTexture(void)
{
	if(s_texture) return 1;

	if(!d3d9device){
		if(dbglog_throttle("noise_nodev"))
			dbglog("[Noise] texture deferred: d3d9device is null (CPU data is ready)");
		return 0;
	}

	if(!s_dataReady)
		buildUniversal();

	IDirect3DTexture9 *tex = NULL;
	HRESULT hr = d3d9device->CreateTexture((UINT)s_size, (UINT)s_size, 0,
	                                       D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
	                                       D3DPOOL_DEFAULT, &tex, NULL);
	if(FAILED(hr) || !tex){
		dbglog("[Noise] ERROR: CreateTexture(%dx%d) failed hr=0x%08X",
		       s_size, s_size, (unsigned)hr);
		return 0;
	}

	D3DLOCKED_RECT locked;
	ZeroMemory(&locked, sizeof(locked));
	hr = tex->LockRect(0, &locked, NULL, D3DLOCK_DISCARD);
	if(FAILED(hr) || !locked.pBits){
		dbglog("[Noise] ERROR: LockRect failed hr=0x%08X", (unsigned)hr);
		tex->Release();
		return 0;
	}

	const int srcPitch = s_size * 4;
	unsigned char *dst = (unsigned char *)locked.pBits;
	for(int y = 0; y < s_size; y++){
		memcpy(dst + (size_t)y * (size_t)locked.Pitch,
		       s_rgba + (size_t)y * (size_t)srcPitch,
		       (size_t)srcPitch);
	}
	tex->UnlockRect(0);

	s_texture = tex;
	s_stats.textureLive = 1;

	// One-shot receipt: the first time the GPU tile actually exists. Reset
	// recreates the texture but this receipt is intentionally once per
	// process (OnDeviceReset has its own throttled line).
	if(dbglog_once("noise_tex")){
		dbglog("[Noise] texture created %dx%d fmt=A8R8G8B8 seed=%u quality=%d "
		       "(R=Perlin G=Worley B=white A=blue) inits=%u rebuilds=%u",
		       s_size, s_size, s_stats.seed, s_stats.quality,
		       s_stats.initCount, s_stats.rebuildCount);
	}
	return 1;
}

// ---------------------------------------------------------------------------
// public lifecycle
// ---------------------------------------------------------------------------

void Init(unsigned seed)
{
	if(s_initialized && s_seed == seed)
		return;	// idempotent

	bool first = !s_initialized;

	s_initialized = true;
	s_seed = seed;
	s_stats.seed = seed;
	s_tablesReady = false;
	s_blueReady = false;

	ensureTables();
	releaseTexture();
	buildUniversal();

	if(first) s_stats.initCount++;
	else      s_stats.rebuildCount++;
}

void Shutdown(void)
{
	releaseTexture();
	s_initialized = false;
	s_dataReady = false;
	s_tablesReady = false;
	s_blueReady = false;
	s_stats.size = 0;
	s_stats.textureLive = 0;
}

void SetSeed(unsigned seed)
{
	if(s_initialized && s_seed == seed)
		return;

	s_initialized = true;
	s_seed = seed;
	s_stats.seed = seed;
	s_tablesReady = false;
	s_blueReady = false;

	ensureTables();
	releaseTexture();
	buildUniversal();
	s_stats.rebuildCount++;
}

void SetQuality(NoiseQuality q)
{
	if(q < NQ_LOW) q = NQ_LOW;
	if(q > NQ_HIGH) q = NQ_HIGH;

	if(s_dataReady && s_quality == q)
		return;

	s_initialized = true;
	s_quality = q;
	s_stats.quality = (int)q;
	s_blueReady = false;

	releaseTexture();
	buildUniversal();
	s_stats.rebuildCount++;
}

void ReleaseDeviceObjects(void)
{
	// Device-owned resource only: the CPU tile (s_rgba) and the tables are
	// kept, so a Reset never forces a rebake. Null-safe: releaseTexture()
	// handles s_texture == NULL, which makes this a clean no-op when no
	// device ever created the texture.
	releaseTexture();
	if(dbglog_throttle("noise_release"))
		dbglog("[Noise] ReleaseDeviceObjects: D3D texture released (CPU data cached)");
}

void OnDeviceReset(void)
{
	// D3DPOOL_DEFAULT resources die with the device. Keep the CPU bytes; the
	// next BindUniversal()/BindAndUpload() recreates the texture. Stats are
	// cleared so post-reset receipts describe the new device generation
	// instead of accumulating pre-Reset counters.
	ReleaseDeviceObjects();

	s_stats.initCount = 0;
	s_stats.rebuildCount = 0;
	s_stats.textureLive = 0;
	s_stats.seed = s_seed;
	s_stats.size = s_dataReady ? (unsigned)s_size : 0;
	s_stats.quality = (int)s_quality;

	if(dbglog_throttle("noise_reset"))
		dbglog("[Noise] OnDeviceReset: device objects released, stats cleared, CPU data cached");
}

int UniversalSize(void)
{
	return s_dataReady ? s_size : 0;
}

const unsigned char *UniversalData(void)
{
	return s_dataReady ? s_rgba : NULL;
}

IDirect3DTexture9 *UniversalTexture(void)
{
	if(!ensureTexture()) return NULL;
	return s_texture;
}

int BindUniversal(int samplerStage)
{
	if(samplerStage < 0 || samplerStage > 15)
		return 0;
	if(!ensureTexture())
		return 0;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return 0;

	dev->SetTexture((DWORD)samplerStage, s_texture);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(samplerStage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(samplerStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	return 1;
}

void BindAndUpload(int samplerStage, IDirect3DDevice9 *dev,
                   float timeSeconds, unsigned frameIndex,
                   const float p49[4], const float p50[4],
                   const float p51[4], const float p52[4])
{
	if(samplerStage < 0 || samplerStage > 15)
		return;
	if(!d3d9device)
		return;		// no live device: clean no-op (CPU data is unaffected)
	if(!dev)
		dev = d3d9device;
	if(!p49 || !p50 || !p51 || !p52)
		return;
	if(!ensureTexture())
		return;

	// Sampler s9 contract: wrap both axes, LINEAR, no mips, anisotropy off.
	dev->SetTexture((DWORD)samplerStage, s_texture);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	dev->SetSamplerState(samplerStage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(samplerStage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(samplerStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	dev->SetSamplerState(samplerStage, D3DSAMP_MAXANISOTROPY, 1);

	// Stamp time/frame into c50 so the parameter block and the explicit
	// arguments cannot diverge (c50 = white, blue, timeSeconds, frameIndex).
	float c50[4] = { p50[0], p50[1], timeSeconds, (float)frameIndex };

	// Double-upload doctrine: RAW first, then RW. The raw write guarantees
	// the device holds the new values now; the RW write updates the RW
	// constant cache so a later RwD3D9SetPixelShaderConstant with the same
	// values cannot be skipped against a cache the raw write never touched,
	// and vice versa. Both carry identical values.
	dev->SetPixelShaderConstantF(49, p49, 1);
	dev->SetPixelShaderConstantF(50, c50, 1);
	dev->SetPixelShaderConstantF(51, p51, 1);
	dev->SetPixelShaderConstantF(52, p52, 1);

	RwD3D9SetPixelShaderConstant(49, p49, 1);
	RwD3D9SetPixelShaderConstant(50, c50, 1);
	RwD3D9SetPixelShaderConstant(51, p51, 1);
	RwD3D9SetPixelShaderConstant(52, p52, 1);
}

const Stats &GetStats(void)
{
	return s_stats;
}

void LogStats(const char *tag)
{
	if(!dbglog_throttle("noise_stats"))
		return;
	dbglog("[Noise] %s size=%u quality=%d seed=%u tex=%d inits=%u rebuilds=%u data=%d blue=%d",
	       tag ? tag : "stats",
	       s_stats.size, s_stats.quality, s_stats.seed, s_stats.textureLive,
	       s_stats.initCount, s_stats.rebuildCount,
	       s_dataReady ? 1 : 0, s_blueReady ? 1 : 0);
}

} // namespace SkyNoise
