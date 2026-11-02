#pragma once
// Universal noise service. CPU generators + the packed RGBA utility-noise
// texture consumed by shaders/include/Noise.hlsl (R=Perlin G=Worley B=white A=blue).
// SM3.0-friendly: the shader side is 1 sampler + 3 fetches per cloud sample.
//
// CHANNEL PACKING (the canonical contract; do not change channel order):
//   R = Perlin fBm, tileable            -> cloud shapes / macro variation
//   G = Worley F1, tileable             -> erosion, cauliflower cloud edges
//   B = white noise (integer hash)      -> raymarch / PCF / SSAO jitter
//   A = blue noise (Mitchell best-cand) -> dithering (energy to high freq)
// CPU-side the tile is stored as BGRA bytes so D3DFMT_A8R8G8B8 samples as
//   .r = Perlin, .g = Worley, .b = white, .a = blue.
//
// DETERMINISM
//   Integer hashes + a seeded permutation table: no rand(), no time, no OS
//   entropy. Same seed => bit-identical tile on every run/machine.
//
// LIFETIME
//   Init() builds the CPU tile and needs NO D3D device. The IDirect3DTexture9
//   is created lazily by BindUniversal()/UniversalTexture() and needs a live
//   d3d9device; if the device is null those calls fail gracefully.
//
// DEVICE RESET
//   depthhook.cpp's hook_Reset calls ReleaseDeviceObjects() in the pre-Reset
//   region (before the DEFAULT-pool resource must be gone) and
//   OnDeviceReset() after a successful Reset. The next BindUniversal() /
//   BindAndUpload() recreates the texture from the cached CPU bytes (no
//   rebake). ReleaseDeviceObjects() is also safe to call standalone.
//
// SAMPLER STATE
//   BindUniversal() sets ADDRESS U/V/W = WRAP, MIN/MAG = LINEAR,
//   MIPFILTER = NONE on the requested stage. Saving and restoring caller
//   state is the CALLER's responsibility.

struct IDirect3DTexture9;
struct IDirect3DDevice9;

namespace SkyNoise {

enum NoiseQuality { NQ_LOW = 0, NQ_MEDIUM = 1, NQ_HIGH = 2 };

// lifecycle -------------------------------------------------------------
void Init(unsigned seed = 1337u);           // idempotent; builds the texture data
void Shutdown(void);                        // release D3D texture + state
void SetSeed(unsigned seed);                 // reseeds + rebuilds packed data
void SetQuality(NoiseQuality q);             // rebuilds packed data at new res
void OnDeviceReset(void);                    // release + recreate the D3D texture
void ReleaseDeviceObjects(void);             // release the POOL_DEFAULT texture only

// scalar generators, output in [0,1] ------------------------------------
float Perlin2(float x, float y);
float Perlin3(float x, float y, float z);
float PerlinTileable2(float x, float y, int period);   // seamless, period in lattice cells
float Fbm2(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f);
float FbmTileable2(float x, float y, int octaves, int period);
float Worley2(float x, float y);                        // F1 distance, normalised [0,1]
float WorleyFbm2(float x, float y, int octaves);
float White2(int x, int y);                             // deterministic integer hash -> [0,1]
float BlueNoise2(int x, int y);                         // tiled blue-noise lookup -> [0,1]

// packed texture --------------------------------------------------------
// Layout: R=Perlin fBm (tileable) | G=Worley F1 | B=white | A=blue noise
// Built at Init/SetSeed/SetQuality. Returned data is BGRA-ordered bytes
// suitable for D3DFMT_A8R8G8B8 (so R channel lands in .r after sampling).
int  UniversalSize(void);                              // e.g. 64, 0 when unbuilt
const unsigned char* UniversalData(void);              // BGRA bytes, size*size*4
IDirect3DTexture9* UniversalTexture(void);             // lazily created, AddRef NOT taken
int  BindUniversal(int samplerStage);                   // creates if needed, sets on device; returns 1 on success

// Bind + upload in one call (the shader-consumer entry point):
//   * lazily creates/binds the universal texture to samplerStage with
//     WRAP/WRAP/WRAP, LINEAR/LINEAR, no mips, MAXANISOTROPY=1
//   * uploads the reserved noise parameter block to BOTH the raw device and
//     the RW constant cache (raw first, then RW: the RW cache cannot be left
//     stale and cache-skip a later write)
//
// Reserved project-wide slots (do not move):
//   c49 = { noiseScale,    worleyScale, coverage,    erosion }
//   c50 = { whiteScale,    blueScale,   timeSeconds, frameIndex }
//   c51 = { cloudScaleA,   cloudScaleB, cloudScaleC, cloudDensity }
//   c52 = { jitterAmount,  ditherAmount, detailStrength, unused }
//
// timeSeconds/frameIndex are stamped into c50.z/c50.w so the two call-site
// sources can never disagree. Safe with d3d9device == NULL (no-op).
void BindAndUpload(int samplerStage, IDirect3DDevice9 *dev,
                   float timeSeconds, unsigned frameIndex,
                   const float p49[4], const float p50[4],
                   const float p51[4], const float p52[4]);

// stats / debug ---------------------------------------------------------
struct Stats { unsigned initCount, rebuildCount, seed, size; int quality; int textureLive; };
const Stats& GetStats(void);
void LogStats(const char *tag);                          // dbglog_throttle style, one line

} // namespace SkyNoise
