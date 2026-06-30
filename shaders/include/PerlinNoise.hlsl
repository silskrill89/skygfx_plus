// PerlinNoise.hlsl - Perlin gradient noise, FBM, turbulence, domain warping
// Used for stochastic edge blending, height generation, terrain variation, water displacement
// REQUIRES ps_3_0 or higher (uses switch statements, int arrays, dynamic indexing)

#ifndef PERLIN_NOISE_INCLUDED
#define PERLIN_NOISE_INCLUDED

// --- Permutation table (256 entries, wraps) ---
static const int PERM[512] = {
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
	140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
	247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
	57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
	74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
	60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
	65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
	200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
	52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
	207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
	119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
	129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
	218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
	81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
	184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
	222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
	// repeat
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
	140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
	247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
	57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
	74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
	60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
	65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
	200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
	52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
	207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
	119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
	129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
	218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
	81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
	184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
	222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static const int GRAD3[12][3] = {
	{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
	{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
	{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
};

float fade(float t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float lerp_val(float t, float a, float b)
{
	return a + t * (b - a);
}

float grad_dot(int hash, float x, float y)
{
	int idx = hash % 12;
	return GRAD3[idx][0] * x + GRAD3[idx][1] * y;
}

float grad_dot3(int hash, float x, float y, float z)
{
	int idx = hash % 12;
	return GRAD3[idx][0] * x + GRAD3[idx][1] * y + GRAD3[idx][2] * z;
}

// 2D Perlin noise, input: (x, y) world-space coordinate
float Perlin2D(float x, float y)
{
	int xi = floor(x) & 255;
	int yi = floor(y) & 255;
	float xf = x - floor(x);
	float yf = y - floor(y);

	float u = fade(xf);
	float v = fade(yf);

	int aa = PERM[PERM[xi] + yi];
	int ab = PERM[PERM[xi] + yi + 1];
	int ba = PERM[PERM[xi + 1] + yi];
	int bb = PERM[PERM[xi + 1] + yi + 1];

	float x1 = lerp_val(u, grad_dot(aa, xf, yf), grad_dot(ba, xf - 1.0, yf));
	float x2 = lerp_val(u, grad_dot(ab, xf, yf - 1.0), grad_dot(bb, xf - 1.0, yf - 1.0));
	return lerp_val(v, x1, x2);
}

// 3D Perlin noise (for animated/warped domains)
float Perlin3D(float x, float y, float z)
{
	int xi = floor(x) & 255;
	int yi = floor(y) & 255;
	int zi = floor(z) & 255;
	float xf = x - floor(x);
	float yf = y - floor(y);
	float zf = z - floor(z);

	float u = fade(xf);
	float v = fade(yf);
	float w = fade(zf);

	int aaa = PERM[PERM[PERM[xi] + yi] + zi];
	int aba = PERM[PERM[PERM[xi] + yi + 1] + zi];
	int aab = PERM[PERM[PERM[xi] + yi] + zi + 1];
	int abb = PERM[PERM[PERM[xi] + yi + 1] + zi + 1];
	int baa = PERM[PERM[PERM[xi + 1] + yi] + zi];
	int bba = PERM[PERM[PERM[xi + 1] + yi + 1] + zi];
	int bab = PERM[PERM[PERM[xi + 1] + yi] + zi + 1];
	int bbb = PERM[PERM[PERM[xi + 1] + yi + 1] + zi + 1];

	float x1 = lerp_val(u, grad_dot3(aaa, xf, yf, zf), grad_dot3(baa, xf-1.0, yf, zf));
	float x2 = lerp_val(u, grad_dot3(aba, xf, yf-1.0, zf), grad_dot3(bba, xf-1.0, yf-1.0, zf));
	float y1 = lerp_val(v, x1, x2);

	x1 = lerp_val(u, grad_dot3(aab, xf, yf, zf-1.0), grad_dot3(bab, xf-1.0, yf, zf-1.0));
	x2 = lerp_val(u, grad_dot3(abb, xf, yf-1.0, zf-1.0), grad_dot3(bbb, xf-1.0, yf-1.0, zf-1.0));
	float y2 = lerp_val(v, x1, x2);

	return lerp_val(w, y1, y2);
}

// --- FBM (Fractal Brownian Motion) ---
// octaves: number of noise layers
// lacunarity: frequency multiplier per octave (typically 2.0)
// gain: amplitude multiplier per octave (typically 0.5)
float FBM2D(float x, float y, int octaves, float lacunarity, float gain)
{
	float sum = 0.0;
	float amp = 1.0;
	float freq = 1.0;
	float maxAmp = 0.0;
	for(int i = 0; i < octaves; i++)
	{
		sum += Perlin2D(x * freq, y * freq) * amp;
		maxAmp += amp;
		amp *= gain;
		freq *= lacunarity;
	}
	return sum / maxAmp;
}

float FBM3D(float x, float y, float z, int octaves, float lacunarity, float gain)
{
	float sum = 0.0;
	float amp = 1.0;
	float freq = 1.0;
	float maxAmp = 0.0;
	for(int i = 0; i < octaves; i++)
	{
		sum += Perlin3D(x * freq, y * freq, z * freq) * amp;
		maxAmp += amp;
		amp *= gain;
		freq *= lacunarity;
	}
	return sum / maxAmp;
}

// --- Turbulence (absolute value FBM) ---
float Turbulence2D(float x, float y, int octaves, float lacunarity, float gain)
{
	float sum = 0.0;
	float amp = 1.0;
	float freq = 1.0;
	float maxAmp = 0.0;
	for(int i = 0; i < octaves; i++)
	{
		sum += abs(Perlin2D(x * freq, y * freq)) * amp;
		maxAmp += amp;
		amp *= gain;
		freq *= lacunarity;
	}
	return sum / maxAmp;
}

// --- Domain Warping ---
// warp the UV coordinates before sampling noise - breaks up tiling artifacts
float2 WarpUVs(float2 uv, float warpStrength)
{
	float wx = Perlin2D(uv.x * 3.7 + 13.1, uv.y * 3.7 + 7.3) * warpStrength;
	float wy = Perlin2D(uv.x * 3.7 + 91.3, uv.y * 3.7 + 47.9) * warpStrength;
	return uv + float2(wx, wy);
}

// Double domain warp (stronger breakup for large-scale terrain)
float2 DoubleWarpUVs(float2 uv, float warpStrength)
{
	float2 q = float2(
		Perlin2D(uv.x * 1.3 + 5.2, uv.y * 1.3 + 1.3),
		Perlin2D(uv.x * 1.3 + 2.8, uv.y * 1.3 + 7.3)
	) * warpStrength;

	float2 r = float2(
		Perlin2D((uv + q) * 2.1 + 11.7, (uv + q).y * 2.1 + 3.1),
		Perlin2D((uv + q) * 2.1 + 7.9, (uv + q).y * 2.1 + 13.7)
	) * warpStrength * 0.5;

	return uv + float2(
		Perlin2D(uv + r, 0.0) * warpStrength * 0.25,
		Perlin2D((uv + r).yx, 0.0) * warpStrength * 0.25
	);
}

// --- Stochastic Edge Mask ---
// Returns a mask (0..1) that smoothly blends at stochastic texture edges
// Uses perlin noise at the tile boundary frequency to create soft blend zones
float StochasticEdgeMask(float2 uv, float tileFreq, float edgeWidth)
{
	// Compute tile-space coordinates
	float2 tileUV = uv * tileFreq;
	float2 tileId = floor(tileUV);
	float2 tileFrac = frac(tileUV);

	// Perlin noise to create irregular blend boundaries
	float noise = Perlin2D(tileId.x * 0.73 + tileId.y * 0.37,
	                       tileId.x * 0.29 - tileId.y * 0.53);
	noise = noise * 0.5 + 0.5; // remap to 0..1

	// Distance from tile edge
	float edgeDist = min(min(tileFrac.x, 1.0 - tileFrac.x),
	                     min(tileFrac.y, 1.0 - tileFrac.y));

	// Smooth mask: 1.0 in center, fades to 0.0 at edges
	// The noise shifts the blend boundary per-tile so seams aren't straight lines
	float mask = smoothstep(0.0, edgeWidth + noise * edgeWidth * 2.0, edgeDist);
	return mask;
}

// --- Height from Surface Type ---
// Maps GTA SA surface type ID to a height value for parallax/terrain blending
// Surface IDs from CColSurfaceInfo: 0=sand, 1=grass, 2=gravel, 3=mud, etc.
float SurfaceTypeToHeight(int surfaceType)
{
	// Height ordering: lowest to highest
	// sand=0.0 (lowest, beach), mud=0.15, gravel=0.3, grass=0.5,
	// dirt=0.55, concrete=0.7, asphalt=0.8, metal=0.9, stone=1.0
	switch(surfaceType)
	{
		case 0:  return 0.0;   // sand
		case 3:  return 0.15;  // mud
		case 2:  return 0.3;   // gravel
		case 1:  return 0.5;   // grass
		case 4:  return 0.55;  // dirt
		case 5:  return 0.7;   // concrete
		case 6:  return 0.8;   // asphalt
		case 10: return 0.9;   // metal
		case 11: return 1.0;   // stone/rock
		default: return 0.5;   // unknown = middle
	}
}

// --- Water Height Field ---
// Procedural water height from perlin - used for POM and tessellation displacement
float WaterHeight(float2 worldUV, float time, float waveScale)
{
	// Three octaves of traveling waves at different angles
	float h1 = Perlin2D(worldUV.x * waveScale + time * 0.3,
	                    worldUV.y * waveScale + time * 0.15);
	float h2 = Perlin2D(worldUV.x * waveScale * 1.7 - time * 0.2,
	                    worldUV.y * waveScale * 1.7 + time * 0.25);
	float h3 = Perlin2D(worldUV.x * waveScale * 3.1 + time * 0.1,
	                    worldUV.y * waveScale * 3.1 - time * 0.35);

	// Weighted blend: larger waves dominate
	return h1 * 0.6 + h2 * 0.3 + h3 * 0.1;
}

// --- Terrain Blend Factor ---
// Compute blend factor between two terrain layers using perlin as stochastic edge mask
float TerrainBlendFactor(float2 worldUV, float heightA, float heightB, float blendWidth)
{
	// Use perlin noise to create organic blend boundary
	float noise = FBM2D(worldUV.x * 2.3, worldUV.y * 2.3, 4, 2.0, 0.5);
	noise = noise * 0.5 + 0.5;

	// Height-based blend: lower surface bleeds under higher surface
	float heightDiff = heightB - heightA;
	float blendCenter = 0.5 + heightDiff * 0.5;

	return smoothstep(blendCenter - blendWidth, blendCenter + blendWidth, noise);
}

// --- Normal from Height ---
// Compute normal from height field for lighting
float3 HeightToNormal2D(float height, float2 uv, float texelSize, float heightScale)
{
	float hL = height; // would need separate samples in practice
	float hR = height;
	float hD = height;
	float hU = height;

	// Approximate gradient from central differences
	float dx = (hR - hL) * heightScale;
	float dy = (hU - hD) * heightScale;
	return normalize(float3(-dx, -dy, 1.0));
}

#endif
