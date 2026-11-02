#include "skygfx.h"
#include "render/postfx.h"
#include "profiler.h"        // W7-D: PF_SCOPE / PfDraw / PfBindPS / PfBindVS / PfNote
#include "texdefs.h"         // TexDefsGetD3DTexture (vanilla FFP bind; the stage-0
                             // entry restore uses the raw-raster 0xB4E9E0 slot)
#include "noise.h"           // W2: SkyNoise::UniversalTexture — the VS gust tile
                             // (VS sampler s1 = device stage 257)
#include <d3d9.h>
#include <math.h>

// ============================================================
// Water Pipe - Xbox/IV/V water styles (shading-only waves)
// VS: Water_VS.hlsl  (shared by all styles)
// PS: Water_Parallax.hlsl — three style entry points, one CSO each:
//     main_xbox -> Water_Parallax.cso (Xbox/PS2-style core, default)
//     main_iv   -> Water_IV.cso       (GTA IV look)
//     main_v    -> Water_V.cso        (GTA V / RAGE look)
// INI waterStyle selects the shader entry + constant preset (NOT a separate
// draw path); INI waterQuality 0-3 gates cost inside every style (0 = one
// normal layer + timecyc colour + vertex-alpha only, 1 = + dual normals +
// depth ramp, 2 = + refraction/depth copy + foam + shore blend, 3 = + sun
// pierce + fine layer x3.7 + wide shore sampling). The scene/depth copies
// only run at tier >= 2; tiers 0/1 fall back to hardware SRCALPHA blending
// against the framebuffer (live translucency, zero texture cost).
//
// REWRITE core (kept): the water plane geometry is NEVER displaced — wave
// motion lives entirely in shading (scrolling mip-mapped detail normals).
// Colour, alpha and reflection tint come from SA's timecycle (CTimeCycle
// m_CurrentColours) with the INI waterDeep*/waterShallow* keys as optional
// overrides (waterUseTimecycle=0). Scene depth (INTZ copy) drives
// shallow->deep colour, translucency and soft shore/water blending.
// ============================================================

bool g_waterParallaxActive = false;
static float g_waterTime = 0.0f;

// W2 wind-driven swell: the SkyNoise universal tile resolved THIS flush for
// the VS gust sampler (stage 257). Set in the c11/c12 upload block, consumed
// by the vertex-texture bind block, never persisted across flushes.
static IDirect3DTexture9 *g_waterGustTex = nullptr;

// W2 one-time vertex-texture-fetch probe state (file scope: read by BOTH
// waterPipe_setRenderState and waterPipe_restoreRenderState).
// -1 = unprobed, 0 = driver refused stage-256 binds (features off), 1 = OK.
static int s_vtfProbe = -1;

// R2-06 receipt latch: the [water_sun] line is emitted exactly once after the
// first successful water setup (reset by waterPipe_init on reload/startup), so
// a registry run always shows the spec/reflection constants actually in use
// without spamming the log every 2 s like the [water] chain line.
static bool s_waterSunReceiptLogged = false;

// INI `SkyGfx/waterFresnelSplit` (read in main.cpp next to the other water
// keys, default 1): 1 = fresnel-split composite (reflection + refraction both
// live every view direction), 0 = the legacy lerp(scene, surf, opacity)
// composite as a per-user rollback. Kept OUT of the config struct on purpose —
// it is a water-pipe-local switch, not a shared setting.
int g_waterFresnelSplit = 1;

// INI `SkyGfx/waterReflectionMix` — now CONFIG-OWNED (skygfx.h Config field
// :625 + main.cpp readfloat, default 0.7): global reflection mix weight
// uploaded as PS c16.w and applied to every style's reflection lobe in
// Water_Parallax.hlsl (WaterReflMix). Default 0.7 = the moderate reduction
// for the "water has sometimes big reflections" complaint: V effective
// 0.85 x 0.7 ~ 0.60, IV 0.88 x 0.7 ~ 0.62 (both in the 0.5-0.6 target band);
// Xbox 0.55 x 0.7 ~ 0.39. 1.0 = authored strength. The PS treats 0/unset as
// "authored" (stale-pipe mismatch guard), so this must never upload 0 for
// "on" — clamp lo 0 is only for a hand-edited 0, which reads as authored in
// the shader. (The old file-static g_waterReflMix is GONE — the c16 upload
// reads config->waterReflectionMix directly, null-config guarded to 0.7.)

extern void *Water_Parallax;
extern void *Water_IV;
extern void *Water_V;
extern void *Water_VS;

// Style-selecting pixel shader lookup. Missing entries fall back to the
// Xbox core so the draw never binds a stale shader; the hook gate calls this
// too so a style whose CSO failed to load degrades instead of garbage-draws.
void *waterPipe_pixelShader(void)
{
	switch(config->waterStyle){
	case 1:  if(Water_IV) return Water_IV; break;
	case 2:  if(Water_V)  return Water_V;  break;
	default: if(Water_Parallax) return Water_Parallax; break;
	}
	if(Water_Parallax) return Water_Parallax;
	if(Water_IV) return Water_IV;
	return Water_V;
}

// W7-D: resource-name tag for the bound pixel shader handle (used by the
// profiler bind points and the [water] chain log). resources/resource.h:
//   IDR_WATER_PARALLAX 201 (main_xbox), IDR_WATER_VS 202,
//   IDR_WATER_IV 241 (main_iv), IDR_WATER_V 242 (main_v).
static const char *waterPipe_psTag(void *ps)
{
    if(ps == Water_Parallax) return "IDR_WATER_PARALLAX";
    if(ps == Water_IV)       return "IDR_WATER_IV";
    if(ps == Water_V)        return "IDR_WATER_V";
    return "IDR_WATER_UNKNOWN";
}

// Naked thunk defined in main.cpp. rwcore.h's declaration sits behind the
// inline-driver guard (#ifdef _D3D9_H_ block, skipped in this TU) and the
// definition in main.cpp is a bare one — plain C++ linkage, matched here.
void _rwD3D9RenderStateFlushCache(void);

// FFP-fallback stage-0 hardening (defined below :1962). Called from the
// waterPipe_setRenderState early-outs (hooked-path vanilla fall-through in
// main.cpp:6274) and the waterPipe_directFlushRoute failure exits so EVERY
// FFP water draw samples waterclear256, never a leaked screen-family raster.
void waterPipe_BindVanillaRaster3L(void);

static RwCamera *&ccamera = *(RwCamera**)0xC170C4;
static IDirect3DDevice9 *g_d3dDevice = nullptr;
static IDirect3DTexture9 *g_sceneTexture = nullptr;
static int g_sceneWidth = 0, g_sceneHeight = 0;

// Scene depth copy (raw window-space z, R32F) for shore blending.
static IDirect3DTexture9 *g_depthTexture = nullptr;
static int g_depthWidth = 0, g_depthHeight = 0;

// G1 screen-space reflection copy (IV/V cores only, tier >= 2): the scene
// RT captured AGAIN with a VERTICAL FLIP into an A8R8G8B8 texture. Sampled
// projectively by Water_Parallax's WaterIV/WaterV cores (s3) and blended
// over the analytic timecycle sky tint by the fresnel weight F. Validity
// rides the FREE .w of PS c7 (camPos) — the (x,y,z) camera position is
// uploaded but the PS never reads it, so c7.w = 0 today; the shader header
// documents c7.w = refl-valid flag (0 = analytic-only, bit-identical).
static IDirect3DTexture9 *g_reflTexture = nullptr;
static int g_reflWidth = 0, g_reflHeight = 0;

// Detail normal map (RGB = tangent-space normal, A = height). A real texture
// with a hardware mip chain — the old procedural hash noise had no mips,
// which is exactly what aliased into the sub-pixel sparkle at distance.
static IDirect3DTexture9 *g_detailTex = nullptr;
static bool g_detailTexHasMips = false;

// M2 foam texture (tiling white-noise + cellular, procedural — same recipe
// family as the detail normal map, no new asset). Bound at s4, tier >= 2;
// the PS gate c16.x is 0 whenever this is null, so a failed create can never
// change the output. D3DPOOL_MANAGED — survives Reset like g_detailTex.
static IDirect3DTexture9 *g_foamTex = nullptr;
static bool g_foamTexHasMips = false;

// 0xC170C4 normally holds NULL or a real RwCamera*. Corrupted runs leave float

// 0.0004f bits there (pointer value 0x39D1B717, (p & 3) == 3) which passes a
// plain null check and AVs on the first field read (mov eax,[eax+4] ->
// [0x39D1B71B], symbolized waterPipe_setRenderState+0x432). A real RwCamera*
// is always 4-byte aligned and never inside the first page; no module
// range-check — the camera lives in game/heap memory.
static bool waterCamUsable(const RwCamera *p)
{
    return p != NULL && ((uintptr_t)p & 3) == 0 && (uintptr_t)p >= 0x10000;
}

// Cull mode in effect before the water draw — the water plane renders
// double-sided (single unified pass, no front/backface split), and whatever
// the frame had set goes back afterwards. Pre-initialised so a failed Get can
// never leave it as garbage (grassRenderCallback gotcha).
static RwUInt32 s_savedCull = rwCULLMODECULLBACK;

// Z-state bracket for the water draw (three-layer doctrine — see
// pipeEnterAlphaMode in pipelinecommon.cpp: L1 rw cache -> L2 RW D3D9
// driver cache -> flush -> L3 raw device). waterPipe_setRenderState
// snapshots the Z block of all three layers at ENTRY (before the tier >= 2
// depth copy can move it) and pins ZENABLE=TRUE / ZFUNC=LESSEQUAL /
// ZWRITE=FALSE for the raw draw; waterPipe_restoreRenderState hands these
// exact values back (nothing hardcoded). Pre-seeded to the canonical scene
// values so a failed Get can never restore garbage (grassRenderCallback
// gotcha, as above).
static void    *s_savedZTestRw    = (void*)TRUE;   // rwRENDERSTATEZTESTENABLE
static void    *s_savedZWriteRw   = (void*)TRUE;   // rwRENDERSTATEZWRITEENABLE
static RwUInt32 s_savedZEnableDrv = D3DZB_TRUE;    // RwD3D9GetRenderState (L2)
static RwUInt32 s_savedZWriteDrv  = TRUE;
static RwUInt32 s_savedZFuncDrv   = D3DCMP_LESSEQUAL;
static DWORD    s_savedZEnableDev = D3DZB_TRUE;    // raw device Get (L3)
static DWORD    s_savedZWriteDev  = TRUE;
static DWORD    s_savedZFuncDev   = D3DCMP_LESSEQUAL;

// FIX 2b (2026-10-30 horizon band): raw device FOGCOLOR at ENTRY. The water
// pipe never writes FOGCOLOR (a "no rw twin" device state per the
// shadowContract capture tables), so the entry value IS the value the frame's
// own fog consumers expect — the old restore re-enabled fog through L1 only
// and left whatever colour the last fog-writer (building/sky passes) had
// pushed, fogging the horizon black. Captured raw (L3) in
// waterPipe_setRenderState's Z-snapshot block, re-pushed through all three
// layers in waterPipe_restoreRenderState (buildingPipe.cpp cb-entry idiom).
static DWORD    s_savedFogColorDev = 0;

// Fog bracket COMPLETION (2026-10-31 grey-veil family): the 03:01 run's
// beach shots show a whole-scene desaturated wash with a FLAT fog-coloured
// sky — the fog family, not the water quads (the water draw is quad-bounded).
// The old restore FORCED FOGENABLE=TRUE while the fog RANGE/MODE states
// (FOGSTART/FOGEND/FOGDENSITY/FOGTABLEMODE/FOGVERTEXMODE — all "no rw twin"
// device states) held whatever the last fog-writer left; a foreign range
// fogs every subsequent consumer to the fog colour = the fullscreen veil.
// Doctrine fix: capture the ENTRY fog block (all layers) and hand back
// EXACTLY that — never a forced enable. A broken entry range is preserved
// verbatim (it belongs to whoever set it) and is NAMED by the one-shot
// bracket receipt so the next run attributes it.
static void    *s_savedFogEnableRw  = (void*)TRUE;      // rwRENDERSTATEFOGENABLE (L1)
static RwUInt32 s_savedFogEnableDrv = TRUE;            // D3DRS_FOGENABLE (L2)
static DWORD    s_savedFogEnableDev = TRUE;            // raw device (L3)
static RwUInt32 s_savedFogTableModeDrv = D3DFOG_LINEAR; // D3DRS_FOGTABLEMODE (L2)
static DWORD    s_savedFogTableModeDev = D3DFOG_LINEAR;
static RwUInt32 s_savedFogVertexModeDrv = D3DFOG_LINEAR;// D3DRS_FOGVERTEXMODE (L2)
static DWORD    s_savedFogVertexModeDev = D3DFOG_LINEAR;
static RwUInt32 s_savedFogStartDrv  = 0;               // D3DRS_FOGSTART (L2)
static DWORD    s_savedFogStartDev  = 0;
static RwUInt32 s_savedFogEndDrv    = 0x3F800000;      // 1.0f bits
static DWORD    s_savedFogEndDev    = 0x3F800000;
static RwUInt32 s_savedFogDensityDrv = 0x3F800000;     // 1.0f bits
static DWORD    s_savedFogDensityDev = 0x3F800000;

// ---- Blend bracket (test-7 creator fix, 2026-11-01) ----
// test-7 statesync: "blend L2=1 L3=0 diverged — healed" every ~1 s with BLACK
// LIGHT QUADS in the windows = a persistent blend=0 creator in the water
// path. Audit of this TU found exactly one creator family: the restore's
// HARDCODED blend block — VERTEXALPHAENABLE=false forced after EVERY water
// draw with NO entry snapshot, written through L1 only. Two failure shapes:
//   (a) entry blend was ON (water flushes inside a blend-on window; additive
//       light quads follow) -> the forced false leaves ALPHABLENDENABLE=0 on
//       the device -> the quads rasterise opaque black;
//   (b) a stale L1 value-compare no-op skips the device write entirely while
//       the L2 driver cache believes the write landed = exactly the
//       "L2=1 L3=0" divergence shape the statesync keeps healing.
// Doctrine fix, same as the fog bracket above: capture the ENTRY blend block
// (VERTEXALPHAENABLE/SRCBLEND/DESTBLEND, all three layers) and hand back
// EXACTLY that through L1 -> L2 -> flush -> L3 raw. Pre-seeded to the
// documented SA/re3 scene convention so a failed Get can never restore
// garbage.
static void    *s_savedBlendEnableRw = (void*)FALSE;              // rwRENDERSTATEVERTEXALPHAENABLE (L1)
static void    *s_savedSrcBlendRw    = (void*)rwBLENDSRCALPHA;    // rwRENDERSTATESRCBLEND (L1)
static void    *s_savedDstBlendRw    = (void*)rwBLENDINVSRCALPHA; // rwRENDERSTATEDESTBLEND (L1)
static RwUInt32 s_savedBlendEnableDrv = FALSE;                     // D3DRS_ALPHABLENDENABLE (L2)
static RwUInt32 s_savedSrcBlendDrv    = D3DBLEND_SRCALPHA;
static RwUInt32 s_savedDstBlendDrv    = D3DBLEND_INVSRCALPHA;
static DWORD    s_savedBlendEnableDev = FALSE;                     // raw device (L3)
static DWORD    s_savedSrcBlendDev    = D3DBLEND_SRCALPHA;
static DWORD    s_savedDstBlendDev    = D3DBLEND_INVSRCALPHA;

// ---- Stage-0 ENTRY raster capture (fonts regression fix, 2026-11-01) ----
// The white-texture park (FIX 2a) handed the frame back a raster the frame
// never owned. That is a LEAK of a different shape than NULL, and it is what
// the "fonts are completely broken" build surfaced: the HUD/font Im2D path
// binds through the RW texture cache, so a consumer whose own bind hits a
// stale L1/L2 entry (value-compare no-op) samples the white park instead of
// its glyph atlas — text renders blank/white. Worse, the tier >= 2 depth-copy
// helper (PostFX_CopyDepthToTexture, called from waterPipe_setRenderState
// BEFORE the draw binds) raw-NULLs stage 0, so a capture taken at the draw
// site would already be the helper's NULL, not the frame's entry raster.
//
// Doctrine fix, same entry-capture shape as the Z/fog/blend brackets above:
// snapshot the ENTRY raster in waterPipe_setRenderState before ANY stage-0
// mutation (depth copy, reflection copy, water bind) and hand it back VERBATIM
// in waterPipe_restoreRenderState. No AddRef is held (RwRenderStateGet does
// not refcount), so a device replacement / Reset / shutdown can never be
// blocked by a captured DEFAULT-pool texture; the capture is just cleared on
// every device-epoch change (SyncDevice / ReleaseDeviceObjects / shutdown).
static RwRaster *s_savedS0Raster = NULL;   // ENTRY stage-0 raster (L1 truth)

// Raster -> IDirect3DTexture9 accessor for the L3 pin + stage-0 leak receipt.
// game's ONLY raster -> IDirect3DTexture9 path (raster + the RW extension
// offset at 0xB4E9E0, ext+0 = texture pointer): identical formula to
// main.cpp shadowRasterToTex(:1591), chars.cpp RestoreSSSStage0AndSampler(:206)
// and neopipe.cpp:2194. Returns a BORROWED pointer (no AddRef) — never Release.
static IDirect3DTexture9 *waterPipe_RasterToD3D(RwRaster *ras)
{
    if(!ras) return NULL;
    if(((uintptr_t)ras & 3u) != 0) return NULL;         // never deref a misaligned ptr
    // Only the TEXTURE raster family carries the IDirect3DTexture9 extension
    // slot. A camera / front-buffer raster has no such mapping — reading the
    // slot on one would manufacture a garbage pointer. Those entry rasters are
    // deliberately "unprovable": the L3 pin is skipped (never clear a valid
    // bind) and the s0 leak receipt stays silent for them.
    if(ras->cType != rwRASTERTYPETEXTURE && ras->cType != rwRASTERTYPECAMERATEXTURE)
        return NULL;
    DWORD extOff = *(DWORD*)0xB4E9E0;
    if(extOff == 0 || extOff >= 0x100000) return NULL;  // RW ext slot not initialised
    IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)ras + extOff);
    if(!cand || (uintptr_t)cand < 0x10000u || (((uintptr_t)cand & 3u) != 0))
        return NULL;
    return cand;
}

// ============================================================
// Water body classification (lane W7-B; scheme 1 of docs/plans/water-refs.md:175)
//
// CPU per-vertex classify at water-quad flush: the Im3D TempBuffer verts are
// point-tested against a table built once from SA's water.dat tables, and the
// resulting (bodyType, blend, aspect) triple is written into the RwIm3DVertex
// NORMAL channel — Water_VS declared NORMAL but never read it, so the channel
// is free. Water_VS forwards it to the PS as TEXCOORD5:
//
//   TEXCOORD5 = (bodyType code, bodyBlend, signed aspect, 0)
//     bodyType : 0=OCEAN 1=SHALLOW 2=INTERIOR 3=RIVER 4=POND 5=LAKE
//     bodyBlend: 0..1; ramps to 0 within 5 m of an edge that borders a
//                DIFFERENT-type plane so type boundaries cross-fade, while
//                same-type quad seams stay at 1.0 (no checkerboard dips)
//     signed aspect: sign = plane long axis (+ = X, - = Y), |z| = extent
//                ratio >= 1 (capped 64). SA water.dat flowX/flowY are ALL
//                ZERO (water-refs.md:162), so river scroll direction comes
//                from the quad extents instead of flow data. SPARE today:
//                Water_Parallax's WaterBodyContract reads only dv.x/dv.y —
//                the aspect rides in TEXCOORD5.z for the future river-scroll
//                consumer, ignored by the current PS (no visual effect).
//
//   AUTHORITATIVE PS contract — Water_Parallax.hlsl:123-124 + WaterBodyContract
//   :333-401, shipped by lane W7-A. This side conforms TO it:
//   PS c14 = (bodyType, edgeBlend, foamScale, shoreScale)
//     .x/.y draw-level FALLBACKS for TEXCOORD5.x/.y (quad-level scheme);
//       uploaded (0, 2e-4) here — see the "W7-B contract" upload block for
//       why .y carries a hair above the PS's 1e-4 "unset" threshold
//     .z/.w optional GLOBAL fine-tunes (0 = unset = 1.0) — left 0; the PS's
//       per-body deviation table (:370-385) carries all body personality
//   PS c15 = (normScale, opacityScale, glitterScale, spare) — all 0 (unset)
//   VS c10 = (classifyGate, 0, 0, 0) — waterPipe-owned gate, NOT a c14
//     mirror; .x == 1 => TEXCOORD5 carries live codes, 0 => the VS zeroes
//     the channel. ON is signalled by the per-vertex channel feeding
//     WaterBodyContract's mag gate; OFF = TEXCOORD5 0 + c14/c15 0 =>
//     every scale pinned to exactly 1.0 (pre-contract algebra).
//   -> full contract in the upload block at the end of
//      waterPipe_setRenderState (search "W7-B contract").
//
// >>> STRUCT LAYOUTS — VERIFIED by disassembly of gta_sa.exe v1.0 US (NOT
//     guessed; water-refs.md:194 said "sizeof UNCERTAIN 18->20"): <<<
//   AddWaterLevelVertex 0x6E5A40: `add edx,0x14` + `lea eax,[ecx+ecx*4]`
//     `shl eax,2`  => sizeof(CWaterVertex) == 20 (0x14), idx*20 addressing:
//       +0 int16 x, +2 int16 y, +4 float z, +8 float bigWaves,
//       +12 float smallWaves, +16 int8 flowX, +17 int8 flowY, +18 pad
//   AddWaterLevelQuad 0x6E815B: `lea eax,[ebp+ebp*4]` `shl eax,1` => 0xA:
//       +0 uint16 verts[4], +8 flags byte:
//         bit0 bToBeRendered, bit1 bInvisible (= water.dat flags bit0 CLEAR),
//         bit2 bLimitedDepth (= water.dat flags bit1 SET -> shallow)
//   AddWaterLevelTriangle 0x6E7E75: `[esi*8+0xC22854]`, flags byte at +6.
//   Counts: NumWaterTriangles 0xC22884 / NumWaterQuads 0xC22888 /
//           NumWaterVertices 0xC2288C (gta-reversed WaterLevel.h:98-103).
//
// Runs ONLY from waterPipe_setRenderState, at the point of no return — the
// main.cpp:2663 gate hands the buffer to the vanilla draw whenever we bail,
// and the vanilla draw must see pristine normals.
// ============================================================

// water.dat: 301 quads + 6 triangles max (gta-reversed VALIDATE_SIZE).
#define WATER_MAX_POLYS  307
// Blend ramp in metres (water-refs.md:200 "distance-to-quad-edge <= 5 m").
static const float WBODY_RAMP_M = 5.0f;
// World is +-3000 (AddWaterLevelVertex clamps to 0xBB8) — read sanity band.
static const int   WBODY_XY_MAX = 3100;

// Fixed SA water.dat tables (verified addresses above).
static const DWORD ADDR_NUMTRIS     = 0xC22884;
static const DWORD ADDR_NUMQUADS    = 0xC22888;
static const DWORD ADDR_NUMVERTS    = 0xC2288C;
static const DWORD ADDR_TRIANGLES   = 0xC22854;
static const DWORD ADDR_QUADS       = 0xC21C90;
static const DWORD ADDR_VERTS       = 0xC22910;
static const int   WATER_VTX_STRIDE = 20;   // sizeof(CWaterVertex), disassembled

// Im3D TempBuffer (main.cpp:82-85 — redeclared file-locally at the same
// fixed addresses; lane W7-C owns main.cpp, no cross-TU symbol deps).
// Index list spans 0xC4B958..0xC4D958 = 4096 entries; verts start 0xC4D958.
static DWORD &TempBufferVerticesStored = *reinterpret_cast<DWORD*>(0xC4B950);
static DWORD &TempBufferIndicesStored  = *reinterpret_cast<DWORD*>(0xC4B954);
static RwIm3DVertex *TempVertexBuffer   = reinterpret_cast<RwIm3DVertex*>(0xC4D958);
static RwImVertexIndex *TempBufferRenderIndexList = reinterpret_cast<RwImVertexIndex*>(0xC4B958);

enum WaterBodyCode {
    WBODY_OCEAN    = 0,
    WBODY_SHALLOW  = 1,
    WBODY_INTERIOR = 2,
    WBODY_RIVER    = 3,
    WBODY_POND     = 4,
    WBODY_LAKE     = 5
};

struct WaterBodyPoly {
    float minx, miny, maxx, maxy;   // axis-aligned bbox (== the rect for quads)
    float zref;                     // vertex-0 z — game's own IsInInterior test (>950)
    float aspect;                   // long/short extent ratio, >= 1
    float vx[4], vy[4];             // raw verts (exact test for the 6 triangles)
    unsigned char nverts;           // 4 = rect (bbox test), 3 = triangle (exact)
    unsigned char type;             // WBODY_*
    unsigned char edgeMask;         // bit0..3 = minx/maxx/miny/maxy edge borders a DIFFERENT type
};

static WaterBodyPoly s_wbPolys[WATER_MAX_POLYS];
static int   s_wbPolyCount   = 0;
static bool  s_wbReady       = false;
static bool  s_wbAttempted   = false;
static DWORD s_wbSeenQuads   = 0xFFFFFFFF;
static DWORD s_wbSeenTris    = 0xFFFFFFFF;
static DWORD s_wbSeenVerts   = 0xFFFFFFFF;

// ---- Uniform spatial grid over the poly table (perf fix, 2026-10-31) ----
// The per-vertex classify was O(verts x 307): ~2.5M bbox tests/frame at
// 2044-vert flushes — the top water-path perf item (user: "performance is
// arse"). A flush-AABB pre-filter degenerates on ocean flushes (the vert
// cloud spans kilometres, so nearly every plane stays a candidate), so the
// acceleration is a per-vertex CELL lookup instead: 250 m cells over the
// +-3100 world band, each poly registered in EVERY cell its bbox touches
// (CSR layout, table order preserved within a cell -> identical first-hit
// semantics to the linear scan). Built once in WbBuildTable; the query is
// O(cellPolys) per vert (typically 1-3). Fail-open: if the registration
// total ever exceeds the index array, the grid is marked unready and the
// classify falls back to the linear scan (correctness over speed).
static const float WGRID_CELL  = 250.0f;
static const int   WGRID_N     = 25;                    // ceil(6200 / 250)
static const int   WGRID_CELLS = WGRID_N * WGRID_N;     // 625
static bool  s_gridReady = false;
static unsigned short s_gridStart[WGRID_CELLS + 1];     // CSR cell offsets
static unsigned short s_gridPoly[WATER_MAX_POLYS * 64]; // poly idx per cell

static inline int WbGridCellX(float px)
{
    int cx = (int)((px + 3100.0f) * (1.0f / WGRID_CELL));
    if(cx < 0) cx = 0;
    if(cx >= WGRID_N) cx = WGRID_N - 1;
    return cx;
}
static inline int WbGridCellY(float py)
{
    int cy = (int)((py + 3100.0f) * (1.0f / WGRID_CELL));
    if(cy < 0) cy = 0;
    if(cy >= WGRID_N) cy = WGRID_N - 1;
    return cy;
}

static void WbBuildGrid(void)
{
    s_gridReady = false;
    if(s_wbPolyCount <= 0) return;
    int cnt[WGRID_CELLS];
    for(int c = 0; c < WGRID_CELLS; c++) cnt[c] = 0;
    int total = 0;
    for(int i = 0; i < s_wbPolyCount; i++){
        int x0 = WbGridCellX(s_wbPolys[i].minx);
        int x1 = WbGridCellX(s_wbPolys[i].maxx);
        int y0 = WbGridCellY(s_wbPolys[i].miny);
        int y1 = WbGridCellY(s_wbPolys[i].maxy);
        for(int cy = y0; cy <= y1; cy++)
            for(int cx = x0; cx <= x1; cx++){
                cnt[cy * WGRID_N + cx]++;
                total++;
            }
    }
    if(total > WATER_MAX_POLYS * 64){
        if(dbglog_throttle("water_bodytbl"))
            dbglog("[water] body grid: %d registrations overflow -> linear scan",
                   total);
        return;
    }
    int acc = 0;
    for(int c = 0; c < WGRID_CELLS; c++){
        s_gridStart[c] = (unsigned short)acc;
        acc += cnt[c];
    }
    s_gridStart[WGRID_CELLS] = (unsigned short)acc;
    int cur[WGRID_CELLS];
    for(int c = 0; c < WGRID_CELLS; c++) cur[c] = s_gridStart[c];
    for(int i = 0; i < s_wbPolyCount; i++){
        int x0 = WbGridCellX(s_wbPolys[i].minx);
        int x1 = WbGridCellX(s_wbPolys[i].maxx);
        int y0 = WbGridCellY(s_wbPolys[i].miny);
        int y1 = WbGridCellY(s_wbPolys[i].maxy);
        for(int cy = y0; cy <= y1; cy++)
            for(int cx = x0; cx <= x1; cx++)
                s_gridPoly[cur[cy * WGRID_N + cx]++] = (unsigned short)i;
    }
    s_gridReady = true;
    if(dbglog_throttle("water_bodytbl"))
        dbglog("[water] body grid: %d cells, %d registrations (avg %.2f/cell)",
               WGRID_CELLS, total, (float)total / (float)WGRID_CELLS);
}

// ---- W7-D instrumentation (log-only, no render effect) ----
// Per-flush vertex histogram of the classified codes, sampled by the
// [water] chain dbglog so one line every 2 s shows which body types were
// actually in the flush. Never read by any math/path.
static int s_flushHist[6];      // WBODY_* -> vertex count
static int s_flushHistVerts;    // total classified vertices

static bool WbReadVertex(int idx, float *x, float *y, float *z)
{
    const unsigned char *p =
        (const unsigned char*)ADDR_VERTS + (size_t)idx * WATER_VTX_STRIDE;
    short sx = *(const short*)(p + 0);
    short sy = *(const short*)(p + 2);
    float sz = *(const float*)(p + 4);
    // Wrong stride / corrupted table lands outside the world band; the NaN
    // guard covers a float read of non-float bytes.
    if(sx < -WBODY_XY_MAX || sx > WBODY_XY_MAX) return false;
    if(sy < -WBODY_XY_MAX || sy > WBODY_XY_MAX) return false;
    if(!(sz > -2000.0f && sz < 3000.0f)) return false;
    *x = (float)sx;
    *y = (float)sy;
    *z = sz;
    return true;
}

// Inclusive point-in-triangle (degenerate polys are rejected at build time).
static bool WbTriContains(const WaterBodyPoly &p, float px, float py)
{
    float d1 = (px - p.vx[1]) * (p.vy[0] - p.vy[1]) - (p.vx[0] - p.vx[1]) * (py - p.vy[1]);
    float d2 = (px - p.vx[2]) * (p.vy[1] - p.vy[2]) - (p.vx[1] - p.vx[2]) * (py - p.vy[2]);
    float d3 = (px - p.vx[0]) * (p.vy[2] - p.vy[0]) - (p.vx[2] - p.vx[0]) * (py - p.vy[0]);
    bool neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(neg && pos);
}

// Mark every bbox edge that another, DIFFERENT-typed plane borders across.
// Only those edges get the blend ramp — same-type quad seams stay at
// blend = 1.0 (otherwise every ocean quad would show a 5 m foam dip grid).
static void WbBuildEdgeMasks(void)
{
    const float EPS = 1.0f;
    for(int i = 0; i < s_wbPolyCount; i++){
        WaterBodyPoly &a = s_wbPolys[i];
        unsigned char mask = 0;
        for(int j = 0; j < s_wbPolyCount && mask != 0x0F; j++){
            if(j == i) continue;
            const WaterBodyPoly &b = s_wbPolys[j];
            if(b.type == a.type) continue;
            // b's edge coincides with a's edge (within 1 m) AND the spans
            // overlap by more than a single corner point.
            if(fabsf(b.maxx - a.minx) <= EPS && b.maxy > a.miny && b.miny < a.maxy) mask |= 1;
            if(fabsf(b.minx - a.maxx) <= EPS && b.maxy > a.miny && b.miny < a.maxy) mask |= 2;
            if(fabsf(b.maxy - a.miny) <= EPS && b.maxx > a.minx && b.minx < a.maxx) mask |= 4;
            if(fabsf(b.miny - a.maxy) <= EPS && b.maxx > a.minx && b.minx < a.maxx) mask |= 8;
        }
        a.edgeMask = mask;
    }
}

static bool WbBuildTable(DWORD nq, DWORD nt, DWORD nv)
{
    s_wbPolyCount = 0;
    int tried = 0, accepted = 0, skipped = 0, nInvisible = 0, nLimDepth = 0;
    int hist[6] = {0, 0, 0, 0, 0, 0};

    for(int kind = 0; kind < 2; kind++){
        DWORD n       = (kind == 0) ? nq : nt;
        DWORD base    = (kind == 0) ? ADDR_QUADS : ADDR_TRIANGLES;
        int   stride  = (kind == 0) ? 10 : 8;
        int   nverts  = (kind == 0) ? 4  : 3;
        int   flagOff = (kind == 0) ? 8  : 6;

        for(DWORD i = 0; i < n; i++){
            const unsigned char *p = (const unsigned char*)base + i * (DWORD)stride;
            unsigned char flags = p[flagOff];
            tried++;
            if(flags & 2){ nInvisible++; continue; }   // bInvisible -> never drawn
            if(flags & 4) nLimDepth++;                 // bLimitedDepth -> shallow candidate

            WaterBodyPoly poly = {0};
            poly.nverts = (unsigned char)nverts;
            bool ok = true;
            float zv = 0.0f;
            for(int v = 0; v < nverts; v++){
                unsigned short vi = *(const unsigned short*)(p + v * 2);
                if(vi >= nv){ ok = false; break; }
                if(!WbReadVertex(vi, &poly.vx[v], &poly.vy[v], &zv)){ ok = false; break; }
                if(v == 0) poly.zref = zv;   // game's IsInInterior reads v0
            }
            if(!ok){ skipped++; continue; }

            poly.minx = poly.maxx = poly.vx[0];
            poly.miny = poly.maxy = poly.vy[0];
            for(int v = 1; v < nverts; v++){
                if(poly.vx[v] < poly.minx) poly.minx = poly.vx[v];
                if(poly.vx[v] > poly.maxx) poly.maxx = poly.vx[v];
                if(poly.vy[v] < poly.miny) poly.miny = poly.vy[v];
                if(poly.vy[v] > poly.maxy) poly.maxy = poly.vy[v];
            }
            float dx = poly.maxx - poly.minx;
            float dy = poly.maxy - poly.miny;
            // Triangle cross product *2 (needed for area either way).
            float area2 = (poly.vx[1] - poly.vx[0]) * (poly.vy[2] - poly.vy[0]) -
                          (poly.vx[2] - poly.vx[0]) * (poly.vy[1] - poly.vy[0]);

            // Structural validation — a wrong stride/table breaks these.
            if(kind == 0){
                // Quad must be an axis-aligned rect: every corner on the bbox.
                if(!(dx > 0.0f && dy > 0.0f)) ok = false;
                for(int v = 0; v < 4 && ok; v++){
                    bool cx = (poly.vx[v] == poly.minx || poly.vx[v] == poly.maxx);
                    bool cy = (poly.vy[v] == poly.miny || poly.vy[v] == poly.maxy);
                    if(!(cx && cy)) ok = false;
                }
            }else if(area2 == 0.0f){
                ok = false;
            }
            if(!ok){ skipped++; continue; }

            float area = (kind == 0) ? dx * dy : fabsf(area2) * 0.5f;
            float maxAbs = fabsf(poly.minx);
            if(fabsf(poly.maxx) > maxAbs) maxAbs = fabsf(poly.maxx);
            if(fabsf(poly.miny) > maxAbs) maxAbs = fabsf(poly.miny);
            if(fabsf(poly.maxy) > maxAbs) maxAbs = fabsf(poly.maxy);
            float aspect = (dx >= dy)
                ? dx / (dy > 1e-3f ? dy : 1e-3f)
                : dy / (dx > 1e-3f ? dx : 1e-3f);
            if(aspect < 1.0f) aspect = 1.0f;
            poly.aspect = aspect;

            // Evaluation order == water-refs.md:197-200 enum order:
            // OCEAN -> SHALLOW -> INTERIOR -> RIVER -> POND -> LAKE.
            if(maxAbs >= 2990.0f || area > 150000.0f)
                poly.type = WBODY_OCEAN;
            else if(flags & 4)
                poly.type = WBODY_SHALLOW;
            else if(poly.zref > 950.0f)
                poly.type = WBODY_INTERIOR;
            else if(aspect >= 6.0f)
                poly.type = WBODY_RIVER;
            else if(area <= 4000.0f)
                poly.type = WBODY_POND;
            else
                poly.type = WBODY_LAKE;

            if(s_wbPolyCount >= WATER_MAX_POLYS){ skipped++; continue; }
            s_wbPolys[s_wbPolyCount++] = poly;
            accepted++;
            hist[poly.type]++;
        }
    }

    // Stride/layout sanity: an unreadable table (wrong game version,
    // corrupted data) leaves almost nothing accepted — refuse it rather than
    // misclassify. ~2 of 307 planes are legitimately invisible (water-refs:160).
    if(accepted == 0 || accepted * 2 < tried){
        if(dbglog_throttle("water_bodytbl"))
            dbglog("[water] body table: only %d/%d planes readable -> bodyType disabled",
                   accepted, tried);
        s_wbPolyCount = 0;
        return false;
    }

    WbBuildEdgeMasks();
    WbBuildGrid();
    if(dbglog_throttle("water_bodytbl"))
        dbglog("[water] body table: %d planes (ocean %d shallow %d interior %d "
               "river %d pond %d lake %d) invisible %d limitedDepth %d skipped %d",
               accepted, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5],
               nInvisible, nLimDepth, skipped);
    return true;
}

// Build once; rebuild only when the water.dat counts change (water1.dat /
// m_nWaterConfiguration swap). A failed build is not retried until then.
static bool WbEnsureTable(void)
{
    DWORD nq = *(const DWORD*)ADDR_NUMQUADS;
    DWORD nt = *(const DWORD*)ADDR_NUMTRIS;
    DWORD nv = *(const DWORD*)ADDR_NUMVERTS;
    if(s_wbAttempted && nq == s_wbSeenQuads && nt == s_wbSeenTris && nv == s_wbSeenVerts)
        return s_wbReady;
    s_wbAttempted = true;
    s_wbSeenQuads = nq;
    s_wbSeenTris  = nt;
    s_wbSeenVerts = nv;
    s_wbReady = false;
    if(nq > 301 || nt > 6 || nv > 1021 || nv == 0){
        if(dbglog_throttle("water_bodytbl"))
            dbglog("[water] body table: implausible counts q=%u t=%u v=%u -> disabled",
                   nq, nt, nv);
        s_wbPolyCount = 0;
        return false;
    }
    s_wbReady = WbBuildTable(nq, nt, nv);
    if(!s_wbReady) s_wbPolyCount = 0;
    return s_wbReady;
}

static void WbClassifyPoint(float px, float py, float pz,
                            float *oCode, float *oBlend, float *oAspect,
                            int *oPolyIdx)
{
    const WaterBodyPoly *hit = NULL;
    int hitIdx = -1;
    if(s_gridReady){
        // Spatial grid: only the polys registered in this vert's cell can
        // contain it (each poly registers in every cell its bbox touches),
        // and the per-cell lists preserve table order -> the first hit here
        // is bit-identical to the linear scan's first hit.
        int cell = WbGridCellY(py) * WGRID_N + WbGridCellX(px);
        for(int k = s_gridStart[cell]; k < s_gridStart[cell + 1]; k++){
            int i = s_gridPoly[k];
            const WaterBodyPoly &p = s_wbPolys[i];
            if(px < p.minx || px > p.maxx || py < p.miny || py > p.maxy) continue;
            if(p.nverts == 4 || WbTriContains(p, px, py)){ hit = &p; hitIdx = i; break; }
        }
    }else{
        for(int i = 0; i < s_wbPolyCount; i++){
            const WaterBodyPoly &p = s_wbPolys[i];
            if(px < p.minx || px > p.maxx || py < p.miny || py > p.maxy) continue;
            if(p.nverts == 4 || WbTriContains(p, px, py)){ hit = &p; hitIdx = i; break; }
        }
    }
    if(oPolyIdx) *oPolyIdx = hitIdx;

    if(!hit){
        // No table hit (outside-world ocean sheet / point in a gap):
        // analytic fallback so ocean foam still works past +-2990.
        if(pz > 950.0f){
            *oCode = (float)WBODY_INTERIOR;
            *oBlend = 1.0f;
            *oAspect = 1.0f;
        }else if(fabsf(px) >= 2990.0f || fabsf(py) >= 2990.0f){
            *oCode = (float)WBODY_OCEAN;
            *oBlend = 1.0f;
            *oAspect = 1.0f;
        }else{
            // Unknown point: neutral body, blend 0 => PS body effects off.
            *oCode = (float)WBODY_LAKE;
            *oBlend = 0.0f;
            *oAspect = 0.0f;
        }
        return;
    }

    float blend = 1.0f;
    unsigned char m = hit->edgeMask;
    if(m){
        float d = 1e30f;
        float t;
        if(m & 1){ t = px - hit->minx; if(t < d) d = t; }
        if(m & 2){ t = hit->maxx - px; if(t < d) d = t; }
        if(m & 4){ t = py - hit->miny; if(t < d) d = t; }
        if(m & 8){ t = hit->maxy - py; if(t < d) d = t; }
        blend = d / WBODY_RAMP_M;
        if(blend < 0.0f) blend = 0.0f;
        if(blend > 1.0f) blend = 1.0f;
    }

    // Signed aspect: sign = long axis (scroll direction), |.| = extent ratio.
    float a = hit->aspect;
    if(a > 64.0f) a = 64.0f;
    float dx = hit->maxx - hit->minx;
    float dy = hit->maxy - hit->miny;

    *oCode = (float)hit->type;
    *oBlend = blend;
    *oAspect = (dx >= dy) ? a : -a;
}

// Counts no SA water flush can produce. Checked BEFORE any shader/render
// state is touched so main.cpp's hook can hand the buffer to the vanilla
// draw. SA water primitives are non-shared quads (4v/6i = 1.5 idx/vert) or
// triangles (3v/3i), but the OCEAN GRID is a SHARED-VERTEX mesh (live sample
// 621v/3432i = 23x27 verts, 22x26 quads, ratio 5.53): rows share vertices so
// up to ~6 indices/vertex are legitimate. The old nVerts*3 bound rejected
// that grid and bounced it to the vanilla draw with stale pipe shaders still
// bound (giant teal slabs, 2026-10-29) — relaxed to nVerts*6. Still checked:
// non-empty, <=4096 verts, verts<=indices, indices/vertex <= 6, triangle-
// aligned. The 2026-10-24 232v/990i flush (ratio 4.27) now passes the ratio
// bound; if it recurs and is genuinely foreign it needs a different signal
// (e.g. vertex bbox vs water.dat), not ratio.
static bool WbBufferCountsSane(DWORD nVerts, DWORD nIdx)
{
    // nIdx <= 4096: the Im3D index list physically spans 0xC4B958..0xC4D958
    // (4096 uint16 entries — the vertex array starts right after), so any
    // stored count above 4096 is already a game-side overflow; drawing it
    // would read indices out of the list into the vertex bytes. The shared-
    // vertex ocean grid tops out well under this (live 3432).
    return nVerts > 0 && nVerts <= 4096 && nVerts <= nIdx &&
           nIdx <= 4096 && nIdx <= nVerts * 6 && (nIdx % 3) == 0;
}

// ---- test-4 raster-identity receipt (diagnostics only) ----
// Captures the CURRENT device stage-0 texture (GetTexture AddRefs — released
// here) and emits its dims/format/pool so the anonymous pointers the
// main.cpp water_flatpath receipts print (test-4: s0=1035AF80 site 1 /
// 10355B10 site 2, suspected screen-family scene copies = the mirrored-world
// band source) can be named on sight: a screen-size DEFAULT-pool
// X8R8G8B8/G16R16-family texture is a scene/depth copy, 256x256 MANAGED is
// waterclear256. Callers: the water_flood receipt (pre-bind state the FFP
// draw would have sampled) and the directFlushRoute gate block (one-shot +
// 2s throttle). Log-only; no render path reads anything here.

// Local IID definition — the project does not link dxguid.lib, and the QI
// below needs IID_IDirect3DTexture9. Value from WinSDK 10.0.22621 d3d9.h.
static const IID s_IID_IDirect3DTexture9 =
    {0x85c31227, 0x3de5, 0x4f00, {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}};

static void waterPipe_LogStage0Identity(const char *tag)
{
    // Log-hygiene (2026-11-02): this helper exists only for the recurring
    // diag receipts (flood S0 / flatpath S0). Bail before the GetTexture/QI
    // device reads when those receipts are silenced (g_diagLevel 0 or >=2).
    if(g_diagLevel < 1) return;
    void *s0Ptr = NULL;
    unsigned sw = 0, sh = 0, sfmt = 0, spool = 0xFFFFFFFF;
    if(d3d9device){
        IDirect3DBaseTexture9 *s0 = NULL;
        if(SUCCEEDED(d3d9device->GetTexture(0, &s0)) && s0){
            s0Ptr = (void*)s0;
            IDirect3DTexture9 *t2d = NULL;
            if(SUCCEEDED(s0->QueryInterface(s_IID_IDirect3DTexture9,
                                            (void**)&t2d)) && t2d){
                D3DSURFACE_DESC d = {};
                if(SUCCEEDED(t2d->GetLevelDesc(0, &d))){
                    sw = d.Width; sh = d.Height;
                    sfmt = d.Format; spool = d.Pool;
                }
                t2d->Release();
            }
            s0->Release();
        }
    }
    // Pointer-change cache: the QI + GetLevelDesc pair is only worth paying
    // when the bound texture actually CHANGED (same pointer = same identity).
    // Steady state (the pipe's own scene copy bound flush after flush) now
    // costs one GetTexture + compare and emits nothing.
    static void *s_lastLogged = (void *)-1;
    if(s0Ptr == s_lastLogged) return;
    s_lastLogged = s0Ptr;
    dbglog("[water] %s: s0=%p (%ux%u fmt=%u pool=%u)",
           tag, s0Ptr, sw, sh, sfmt, spool);
}

// Hard throttle for the flood receipt family: dbglog_throttle's interval is
// a global fixed 2 s, and the per-plane floodplane[] lines fired 6,684x in
// the 03:01 run (18 planes x 2 s x 12 min) — log spam on top of the perf
// cost. Gates a caller-owned tick slot at an arbitrary interval; combine
// with dbglog_throttle(tag) so g_diagLevel still silences the family.
static bool WbHardThrottle(DWORD *lastTick, DWORD intervalMs)
{
    DWORD now = GetTickCount();
    if(*lastTick && now - *lastTick < intervalMs) return false;
    *lastTick = now;
    return true;
}

// Per-vertex classify of the CURRENT flush. Returns true when TEXCOORD5
// carries valid codes for the draw that follows. Called ONLY at the point of
// no return inside waterPipe_setRenderState — never for a vanilla fall-through.
static bool WbClassifyFlush(void)
{
    DWORD nVerts = TempBufferVerticesStored;
    DWORD nIdx   = TempBufferIndicesStored;
    // W7-D: reset the log-only histogram first so an early-out below reports
    // an empty flush instead of the previous one's counts.
    for(int h = 0; h < 6; h++) s_flushHist[h] = 0;
    s_flushHistVerts = 0;
    // Sanity: every water primitive pushes verts <= indices (rect 4/6,
    // triangle 3/3); a count failing that, or an absurd one, is garbage —
    // leave the buffer untouched and let the PS run neutral (c14.x = 0).
    // WbBufferCountsSane also enforces the 4096-vert cap and the <=6
    // indices/vertex ratio/triangle alignment (shared-vertex ocean grid,
    // live 621v/3432i = 5.53; the 2026-10-24 232v/990i flush = 4.27).
    if(!WbBufferCountsSane(nVerts, nIdx)){
        if(dbglog_throttle("water_badbuf"))
            dbglog("[water] bad buffer v=%u i=%u -> classify disabled",
                   nVerts, nIdx);
        return false;
    }
    if(!WbEnsureTable()) return false;

    // M1 flood guard + per-plane instrumentation (gen-10, F2 flood-over-land):
    // per-plane vert tallies for this flush (the table is <=307 planes) and
    // the count of verts the flood guard faded out.
    static unsigned short s_planeHitCnt[WATER_MAX_POLYS];
    for(int p = 0; p < WATER_MAX_POLYS; p++) s_planeHitCnt[p] = 0;
    unsigned floodVerts = 0;

    for(DWORD i = 0; i < nVerts; i++){
        RwIm3DVertex &v = TempVertexBuffer[i];
        float code, blend, aspect;
        int polyIdx = -1;
        WbClassifyPoint(v.objVertex.x, v.objVertex.y, v.objVertex.z,
                        &code, &blend, &aspect, &polyIdx);
        v.objNormal.x = code;
        v.objNormal.y = blend;
        v.objNormal.z = aspect;
        // W7-D instrumentation only (never read by any render path).
        if(code >= 0.0f && code <= 5.0f) s_flushHist[(int)code]++;
        s_flushHistVerts++;
        // ---- M1 flood guard (F2: water slab over land/streets) ----
        // A vert that is 2D-outside EVERY water.dat poly AND outside both
        // analytic cases (interior z>950, beyond-world |x|,|y|>=2990) sits
        // over land or a terrain hole — the sheet/remainder quads spanning
        // full blocks read as a milky slab there. Fade the water out via the
        // vertex ALPHA: the PS multiplies the body opacity by S.vtxA
        // (Water_Parallax.hlsl WaterBodyOpacity), so alpha ~0.05 collapses
        // the slab to the refracted scene. NOTE: alpha must be NON-zero —
        // the PS's `a > 0.001 ? a : 1.0` guard clamps a true 0 back to 1.0
        // (the vanilla-identity fallback). RGB is preserved (FFP fallback
        // consumers still read the vanilla water colour; the PS reads .a
        // only). Runs at the point of no return: the pipe WILL draw this
        // flush, so the write can never reach a vanilla fall-through.
        //
        // KNOWN LIMITATION (03:01 run: faded=0 on every plane — documented
        // for the next wave, do not "fix" with a game query): the fade only
        // fires for verts 2D-OUTSIDE every poly. Verts over land that sit
        // INSIDE their own plane's 2D footprint (coastal/river planes are
        // large) classify in-poly and are NOT faded — and a z-check cannot
        // help: a water vert's z equals its plane's level by construction
        // (the vanilla wave displacement is +-0.5..1.0 m and the c11 swell
        // +-0.16 m, so vert-z vs poly-zref carries no land information), no
        // cheap terrain-height query exists in this TU, and the depth copy
        // is tier>=2-only (the 03:01 user ran tier 0). The visible
        // over-land veil at those verts is therefore terrain-hole draw-over
        // (the F1/F3 texture-null family) or tier-0 opacity — both outside
        // this file's reach; the 30 s flood summary (planesHit/floodFaded)
        // remains the identification tool.
        if(polyIdx < 0 && code == (float)WBODY_LAKE &&
           blend == 0.0f && aspect == 0.0f){
            v.color = (v.color & 0x00FFFFFFu) | (0x0Du << 24);   // a = 13/255
            floodVerts++;
        }else if(polyIdx >= 0 && polyIdx < WATER_MAX_POLYS){
            s_planeHitCnt[polyIdx]++;
        }
    }

    // ---- water_flood receipt: 30 s summary for the BIG flushes ----
    // The F2 flood family rode the large site-2 remainder draws (live sample
    // v=2036 i=3054). For any flush >= 256 verts, one summary line per 30 s
    // (flush counter + planesHit + floodFaded). The per-plane floodplane[]
    // lines are RETIRED (log-hygiene pass, 2026-11-02): the flood guard
    // faded nothing (floodFaded=0 on every plane) and the bboxes identified
    // nothing new — 6,684 lines in the 03:01 run for zero signal. The
    // s_planeHitCnt tallies stay (they feed planesHit=). Recurring receipt:
    // gated behind g_diagLevel>=1.
    if(nVerts >= 256){
        static unsigned s_floodFlushCnt = 0;
        static DWORD s_floodSummaryTick = 0;
        s_floodFlushCnt++;
        int planesHit = 0;
        for(int p = 0; p < s_wbPolyCount; p++)
            if(s_planeHitCnt[p]) planesHit++;
        if(g_diagLevel >= 1 &&
           dbglog_throttle("water_flood") &&
           WbHardThrottle(&s_floodSummaryTick, 30000)){
            dbglog("[water] flood flush#%u v=%u planesHit=%d floodFaded=%u",
                   s_floodFlushCnt, (unsigned)nVerts, planesHit, floodVerts);
            // test-4 raster identity: this runs BEFORE the pipe's own texture
            // binds (classify happens at the top of setRenderState), so s0 is
            // exactly what an FFP fallback of this flush would have sampled.
            waterPipe_LogStage0Identity("flood S0");
        }
    }
    return true;
}

void waterPipe_init(void)
{
    g_waterParallaxActive = false;
    g_waterTime = 0.0f;
    s_waterSunReceiptLogged = false;
    // Stage-0 entry capture (fonts fix): no reference is held, so a reload
    // only has to forget the pointer — it belongs to the previous device epoch.
    s_savedS0Raster = NULL;
    s_wbPolyCount = 0;
    s_wbReady = false;
    s_wbAttempted = false;
    s_wbSeenQuads = s_wbSeenTris = s_wbSeenVerts = 0xFFFFFFFF;
}

void waterPipe_shutdown(void)
{
    g_waterParallaxActive = false;
    // Stage-0 entry capture (fonts fix): drop the pointer — no AddRef is held.
    s_savedS0Raster = NULL;
    // Body table is plain game memory (no allocs) — just drop the cached state.
    s_wbPolyCount = 0;
    s_wbReady = false;
    s_wbAttempted = false;
    // D3DPOOL_DEFAULT resource — caller (DLL_PROCESS_DETACH) wraps this in
    // __try/__except since the device may already be torn down.
    if(g_sceneTexture){
        dbglog("[water_rt] release scene (shutdown, %dx%d)", g_sceneWidth, g_sceneHeight);
        g_sceneTexture->Release(); g_sceneTexture = nullptr;
    }
    g_sceneWidth = g_sceneHeight = 0;
    if(g_depthTexture){
        dbglog("[water_rt] release depth (shutdown, %dx%d)", g_depthWidth, g_depthHeight);
        g_depthTexture->Release(); g_depthTexture = nullptr;
    }
    g_depthWidth = g_depthHeight = 0;
    if(g_reflTexture){
        dbglog("[water_rt] release refl (shutdown, %dx%d)", g_reflWidth, g_reflHeight);
        g_reflTexture->Release(); g_reflTexture = nullptr;
    }
    g_reflWidth = g_reflHeight = 0;
    if(g_detailTex){ g_detailTex->Release(); g_detailTex = nullptr; }
    g_detailTexHasMips = false;
    if(g_foamTex){ g_foamTex->Release(); g_foamTex = nullptr; }
    g_foamTexHasMips = false;
}

// Release every D3DPOOL_DEFAULT water RT. MUST run BEFORE the game calls
// IDirect3DDevice9::Reset: a live DEFAULT-pool resource makes Reset return
// D3DERR_INVALIDCALL (the documented 0x7F9A3C crash class), and it is the
// same class of leak the other subsystems already close in
// ReleaseDefaultPoolResources. All three RTs are recreated lazily by
// EnsureSceneTexture / EnsureDepthTexture / EnsureReflTexture on the next
// tier >= 2 flush. g_detailTex is D3DPOOL_MANAGED and deliberately survives.
// Exported pair (external linkage; no header edit needed — the Reset lane uses
// the project's function-local extern idiom, same as TexAlpha_OnDeviceReset):
//   WaterPipe_ReleaseDeviceObjects() — pre-Reset release of all three RTs.
//   waterPipe_OnDeviceReset()        — identical body, lifecycle alias used by
//                                      the water-local device-loss path.
// Post-Reset needs NO call: every RT is rebuilt on demand by the tier>=2 flush
// (EnsureSceneTexture / EnsureDepthTexture / EnsureReflTexture).
// WIRING (Reset lane, NOT this file) — depthhook.cpp hook_Reset:
//   pre-Reset, next to SkyNoise::ReleaseDeviceObjects() (depthhook.cpp:358):
//       extern void WaterPipe_ReleaseDeviceObjects(void);
//       WaterPipe_ReleaseDeviceObjects();
//   (or postfx.cpp OnDeviceReset() at :2429, before ReleaseDefaultPoolResources)
void WaterPipe_ReleaseDeviceObjects(void)
{
    // One-shot wiring receipt: proves the pre-Reset release lane is live even
    // when no RT exists yet (the common startup case — all three are created
    // lazily at the first tier >= 2 flush, so the per-RT lines below stay
    // silent exactly when this receipt matters most).
    if(dbglog_once("water_onreset"))
        dbglog("[water] OnDeviceReset: released RTs (scene/depth/refl "
               "DEFAULT-pool; detail MANAGED survives)");
    // Drop the device's stage references first: a texture still bound to a
    // sampler keeps a live device reference, so our own Release() alone may
    // not take the refcount to zero before Reset (the classic
    // D3DERR_INVALIDCALL trap). Stages 0/2/3 are the water RTs; stage 1 is
    // the MANAGED detail map and survives.
    if(g_d3dDevice){
        // Stage-0 handback: the raw NULL below used to leave L1 naming a
        // raster whose device texture this lane is about to release/destroy —
        // a dangling stage-0 association a later cache-hit rebind would
        // trust (three-layer doctrine: never leave L1/L3 divergent). Park-
        // and-set L1 to NULL (park on white so the NULL write is a real
        // transition, never a value-compare no-op).
        RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
            ? gpWhiteTexture->raster : NULL;
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)whiteRas);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        g_d3dDevice->SetTexture(0, nullptr);
        g_d3dDevice->SetTexture(2, nullptr);
        g_d3dDevice->SetTexture(3, nullptr);
    }
    // The entry-raster capture belongs to the device epoch that is ending.
    s_savedS0Raster = NULL;
    if(g_sceneTexture){
        dbglog("[water_rt] release scene (pool=DEFAULT, %dx%d)", g_sceneWidth, g_sceneHeight);
        g_sceneTexture->Release();
        g_sceneTexture = nullptr;
    }
    g_sceneWidth = g_sceneHeight = 0;
    if(g_depthTexture){
        dbglog("[water_rt] release depth (pool=DEFAULT, %dx%d)", g_depthWidth, g_depthHeight);
        g_depthTexture->Release();
        g_depthTexture = nullptr;
    }
    g_depthWidth = g_depthHeight = 0;
    if(g_reflTexture){
        dbglog("[water_rt] release refl (pool=DEFAULT, %dx%d)", g_reflWidth, g_reflHeight);
        g_reflTexture->Release();
        g_reflTexture = nullptr;
    }
    g_reflWidth = g_reflHeight = 0;
}

// Lifecycle alias — same body (either name may be wired by the Reset lane).
void waterPipe_OnDeviceReset(void)
{
    WaterPipe_ReleaseDeviceObjects();
}

// Re-acquire the D3D9 device and drop stale D3DPOOL_DEFAULT refraction RTs.
// Water-local device-loss handling (postfx OnDeviceReset/ReleaseDefaultPoolResources
// is a separate lane) — recreate lazily when the device is lost or replaced.
static void SyncDevice(void)
{
    IDirect3DDevice9 *dev = *(IDirect3DDevice9**)0xC97C28;
    if(!dev) return;
    if(dev != g_d3dDevice){
        if(g_d3dDevice)
            dbglog("[water_rt] forget scene/depth/refl (device object replaced %p -> %p)",
                   (void*)g_d3dDevice, (void*)dev);
        // Device was recreated — the old DEFAULT-pool textures died with the
        // old device; do NOT Release() the dangling pointers, just forget them.
        g_sceneTexture = nullptr;
        g_sceneWidth = g_sceneHeight = 0;
        g_depthTexture = nullptr;
        g_depthWidth = g_depthHeight = 0;
        g_reflTexture = nullptr;
        g_reflWidth = g_reflHeight = 0;
        g_detailTex = nullptr;
        g_detailTexHasMips = false;
        g_foamTex = nullptr;
        g_foamTexHasMips = false;
        // Entry-raster capture (fonts fix) belonged to the replaced device —
        // forget the pointer (no AddRef was held; the old device is gone).
        s_savedS0Raster = NULL;
        g_d3dDevice = dev;
        return;
    }
    // Device lost (alt-tab / mode switch before Reset): drop our RTs now so the
    // game's upcoming Reset() can succeed, and skip custom water this frame.
    // Note TestCooperativeLevel() is only D3DERR_DEVICELOST for a LOST device;
    // a direct resolution-change Reset is not seen here — that path needs the
    // pre-Reset waterPipe_OnDeviceReset() call from the Reset lane.
    if(g_d3dDevice->TestCooperativeLevel() != D3D_OK &&
       (g_sceneTexture || g_depthTexture || g_reflTexture)){
        WaterPipe_ReleaseDeviceObjects();
        if(dbglog_throttle("water_lost"))
            dbglog("[water] device lost, released refraction/depth/refl RTs");
    }
}

static void EnsureSceneTexture(int w, int h)
{
    if(g_sceneTexture && g_sceneWidth == w && g_sceneHeight == h) return;
    if(g_sceneTexture){
        dbglog("[water_rt] release scene (recreate %dx%d -> %dx%d)",
               g_sceneWidth, g_sceneHeight, w, h);
        g_sceneTexture->Release(); g_sceneTexture = nullptr;
    }
    // Dims follow the pointer (same idiom as EnsureDepthTexture /
    // EnsureReflTexture): a released RT must never leave stale dims behind
    // for the next size check.
    g_sceneWidth = g_sceneHeight = 0;
    if(w < 1 || h < 1) return;
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_sceneTexture, nullptr);
    if(FAILED(hr) || !g_sceneTexture){ g_sceneTexture = nullptr; return; }
    g_sceneWidth = w; g_sceneHeight = h;
    dbglog("[water_rt] create scene %dx%d pool=DEFAULT", w, h);
}

// R32F depth-copy RT (raw window-space z copied from the INTZ depth-stencil).
static void EnsureDepthTexture(int w, int h)
{
    if(g_depthTexture && g_depthWidth == w && g_depthHeight == h) return;
    if(g_depthTexture){
        dbglog("[water_rt] release depth (recreate %dx%d -> %dx%d)",
               g_depthWidth, g_depthHeight, w, h);
        g_depthTexture->Release(); g_depthTexture = nullptr;
    }
    g_depthWidth = g_depthHeight = 0;
    if(w < 1 || h < 1) return;
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F, D3DPOOL_DEFAULT, &g_depthTexture, nullptr);
    if(FAILED(hr) || !g_depthTexture){
        g_depthTexture = nullptr;
        if(dbglog_throttle("water_nodepthrt"))
            dbglog("[water] R32F depth RT unavailable (%dx%d hr=0x%08X)", w, h, hr);
        return;
    }
    g_depthWidth = w; g_depthHeight = h;
    dbglog("[water_rt] create depth %dx%d pool=DEFAULT (R32F)", w, h);
}

// WATER AUDIT #1: returns true only when the blit actually landed. The old
// fire-and-forget version ignored the StretchRect HRESULT — a silently failed
// copy (lost-device window, driver blit refusal) left stale/undefined RT
// content refracted for the whole session. The caller degrades to tier 1 on
// false (same pattern as the RT-create-failure path / water_nort).
static bool CopySceneToTexture(void)
{
    if(!g_d3dDevice || !g_sceneTexture) return false;
    IDirect3DSurface9 *backBuf = nullptr, *sceneSurf = nullptr;
    g_d3dDevice->GetRenderTarget(0, &backBuf);
    g_sceneTexture->GetSurfaceLevel(0, &sceneSurf);
    bool ok = false;
    if(backBuf && sceneSurf)
        ok = SUCCEEDED(g_d3dDevice->StretchRect(backBuf, nullptr, sceneSurf,
                                                nullptr, D3DTEXF_POINT));
    if(backBuf) backBuf->Release();
    if(sceneSurf) sceneSurf->Release();
    return ok;
}

// ---- G1 reflection RT: A8R8G8B8 screen-size texture, same ensure idiom as
// EnsureSceneTexture (recreate on dimension change; the caller passes the
// live viewport, so a resolution change recreates it on the next flush). ----
static void EnsureReflTexture(int w, int h)
{
    if(g_reflTexture && g_reflWidth == w && g_reflHeight == h) return;
    if(g_reflTexture){
        dbglog("[water_rt] release refl (recreate %dx%d -> %dx%d)",
               g_reflWidth, g_reflHeight, w, h);
        g_reflTexture->Release(); g_reflTexture = nullptr;
    }
    g_reflWidth = g_reflHeight = 0;
    if(w < 1 || h < 1) return;
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_reflTexture, nullptr);
    if(FAILED(hr) || !g_reflTexture){
        g_reflTexture = nullptr;
        if(dbglog_throttle("water_noreflrt"))
            dbglog("[water] reflection RT unavailable (%dx%d hr=0x%08X)", w, h, hr);
        return;
    }
    g_reflWidth = w; g_reflHeight = h;
    dbglog("[water_rt] create refl %dx%d pool=DEFAULT", w, h);
}

// Flip the scene into the reflection RT as horizontal bands (StretchRect
// cannot mirror; N sub-rect copies of ceil(H/N) rows each, dst.y mirrored).
// POINT filter + exact-size rects = lossless. Prior art for the planar
// projection this feeds: mirrorPass.cpp's projective composite.
// Returns true only when every band landed — the caller drives the c7.w
// flag from this, so a mismatch (backbuffer surface != viewport, the
// front-buffer 2048x2048 case) fails CLOSED to analytic-only reflection.
static bool CopySceneFlippedToTexture(void)
{
    if(!g_d3dDevice || !g_reflTexture) return false;
    IDirect3DSurface9 *backBuf = nullptr, *reflSurf = nullptr;
    g_d3dDevice->GetRenderTarget(0, &backBuf);
    g_reflTexture->GetSurfaceLevel(0, &reflSurf);
    bool ok = false;
    if(backBuf && reflSurf){
        D3DSURFACE_DESC d = {};
        HRESULT hrg = backBuf->GetDesc(&d);
        if(SUCCEEDED(hrg) && d.Height >= 1 && d.Width >= 1 &&
           (int)d.Width == g_reflWidth && (int)d.Height == g_reflHeight){
            const LONG bands = 16;
            LONG bh = ((LONG)d.Height + bands - 1) / bands;
            ok = true;
            for(LONG y = 0; y < (LONG)d.Height; y += bh){
                RECT src = {0, y, (LONG)d.Width, y + bh};
                if(src.bottom > (LONG)d.Height) src.bottom = (LONG)d.Height;
                RECT dst = {0, (LONG)d.Height - src.bottom,
                            (LONG)d.Width,  (LONG)d.Height - src.top};
                if(FAILED(g_d3dDevice->StretchRect(backBuf, &src, reflSurf, &dst,
                                                   D3DTEXF_POINT))){
                    ok = false;
                    break;
                }
            }
        }else if(dbglog_throttle("water_reflmm")){
            dbglog("[water] refl flip skipped: backbuffer %ux%u != RT %dx%d hr=0x%08X",
                   (unsigned)d.Width, (unsigned)d.Height,
                   g_reflWidth, g_reflHeight, (unsigned)hrg);
        }
    }
    if(backBuf) backBuf->Release();
    if(reflSurf) reflSurf->Release();
    return ok;
}

// ============================================================
// Detail normal map generation (CPU, tileable; GPU builds the mip chain).
// Periodic value noise + two seam-free directional ripple trains -> looks
// like capillary wave detail; central-difference normals with wraparound.
// ============================================================
static inline float DetailHash(int x, int y)
{
    unsigned int n = (unsigned int)(x * 374761393 + y * 668265263);
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return (float)(n & 0xFFFFFF) / 16777215.0f;
}

// Periodic value noise: lattice wraps at `period` cells so the tile is seamless.
static float DetailValueNoise(float x, float y, int period)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    int x0 = ((xi % period) + period) % period;
    int y0 = ((yi % period) + period) % period;
    int x1 = (x0 + 1) % period, y1 = (y0 + 1) % period;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float a = DetailHash(x0, y0), b = DetailHash(x1, y0);
    float c = DetailHash(x0, y1), d = DetailHash(x1, y1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

#define DETAIL_TEX_N 256

static void EnsureDetailNormalTex(void)
{
    if(g_detailTex) return;
    if(!g_d3dDevice) return;

    // AUTOGENMIPMAP gives a real hardware mip chain (req: mip-correct LOD).
    HRESULT hr = g_d3dDevice->CreateTexture(DETAIL_TEX_N, DETAIL_TEX_N, 0,
        D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        &g_detailTex, nullptr);
    g_detailTexHasMips = SUCCEEDED(hr) && g_detailTex != nullptr;
    if(!g_detailTex){
        // Fallback: single-level texture (MIPFILTER off at bind time).
        hr = g_d3dDevice->CreateTexture(DETAIL_TEX_N, DETAIL_TEX_N, 1,
            0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_detailTex, nullptr);
        g_detailTexHasMips = false;
        if(FAILED(hr) || !g_detailTex){
            g_detailTex = nullptr;
            if(dbglog_throttle("water_nodetail"))
                dbglog("[water] detail normal texture create failed hr=0x%08X", hr);
            return;
        }
    }

    // ---- tileable heightfield ----
    static float s_h[DETAIL_TEX_N * DETAIL_TEX_N];
    const float hNorm = 1.0f / (0.500f + 0.250f + 0.125f + 0.16f + 0.10f);
    for(int y = 0; y < DETAIL_TEX_N; y++){
        for(int x = 0; x < DETAIL_TEX_N; x++){
            float u = (float)x / (float)DETAIL_TEX_N;
            float v = (float)y / (float)DETAIL_TEX_N;
            float h = 0.0f;
            h += 0.500f * DetailValueNoise(u *  8.0f, v *  8.0f,  8);
            h += 0.250f * DetailValueNoise(u * 16.0f, v * 16.0f, 16);
            h += 0.125f * DetailValueNoise(u * 32.0f, v * 32.0f, 32);
            // Directional ripple trains; integer frequencies over the tile
            // keep them seamless (phase noise is periodic too).
            float ph1 = 6.2831853f * DetailValueNoise(u * 4.0f, v * 4.0f, 4);
            float ph2 = 6.2831853f * DetailValueNoise(u * 6.0f, v * 6.0f, 6);
            h += 0.16f * (0.5f + 0.5f * sinf(6.2831853f * ( 3.0f * u + 1.0f * v) + ph1));
            h += 0.10f * (0.5f + 0.5f * sinf(6.2831853f * (-2.0f * u + 3.0f * v) + ph2));
            s_h[y * DETAIL_TEX_N + x] = h * hNorm;
        }
    }

    D3DLOCKED_RECT lr;
    if(FAILED(g_detailTex->LockRect(0, &lr, nullptr, 0))){
        g_detailTex->Release();
        g_detailTex = nullptr;
        g_detailTexHasMips = false;   // flag follows the pointer (recreate path recomputes it)
        return;
    }
    for(int y = 0; y < DETAIL_TEX_N; y++){
        DWORD *dst = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
        int ym = (y + DETAIL_TEX_N - 1) & (DETAIL_TEX_N - 1);
        int yp = (y + 1) & (DETAIL_TEX_N - 1);
        for(int x = 0; x < DETAIL_TEX_N; x++){
            int xm = (x + DETAIL_TEX_N - 1) & (DETAIL_TEX_N - 1);
            int xp = (x + 1) & (DETAIL_TEX_N - 1);
            float hC = s_h[y * DETAIL_TEX_N + x];
            float nx = (s_h[y  * DETAIL_TEX_N + xm] - s_h[y  * DETAIL_TEX_N + xp]) * 3.0f;
            float ny = (s_h[ym * DETAIL_TEX_N + x ] - s_h[yp * DETAIL_TEX_N + x ]) * 3.0f;
            float nz = 1.0f;
            float invLen = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
            nx *= invLen; ny *= invLen; nz *= invLen;
            int r = (int)((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
            int g = (int)((ny * 0.5f + 0.5f) * 255.0f + 0.5f);
            int b = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
            int a = (int)(hC * 255.0f + 0.5f);
            if(r < 0) r = 0; if(r > 255) r = 255;
            if(g < 0) g = 0; if(g > 255) g = 255;
            if(b < 0) b = 0; if(b > 255) b = 255;
            if(a < 0) a = 0; if(a > 255) a = 255;
            dst[x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        }
    }
    g_detailTex->UnlockRect(0);
    if(g_detailTexHasMips)
        g_detailTex->GenerateMipSubLevels();
    dbglog("[water] detail normal tex %dx%d mips=%d", DETAIL_TEX_N, DETAIL_TEX_N,
           g_detailTexHasMips ? 1 : 0);
}

// ============================================================
// M2 foam texture generation (CPU, tileable; GPU builds the mip chain).
// RAGE WaterFoamMap stand-in (spec M2: "author a foam tiling texture —
// white noise + cellular"): periodic jittered-grid cellular bubble rims
// (toroidal F1 distance -> seamless tiling) + per-pixel white noise + a
// large-scale periodic coverage mask so the repeat never reads flat.
// Grayscale in RGB, A = 255 (unused). Sampled by Water_Parallax's
// WaterFoamTexture (s4) through the RAGE foam block.
// ============================================================
static inline float FoamHash2(int x, int y, int salt)
{
    return DetailHash(x * 2 + salt, y * 3 + salt * 7 + 1);
}

#define FOAM_TEX_N 256
#define FOAM_CELLS 8

static void EnsureFoamTex(void)
{
    if(g_foamTex) return;
    if(!g_d3dDevice) return;

    // AUTOGENMIPMAP: the foam fetch rides scrolling UVs — mips keep it
    // alias-free at distance (same req as the detail map).
    HRESULT hr = g_d3dDevice->CreateTexture(FOAM_TEX_N, FOAM_TEX_N, 0,
        D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        &g_foamTex, nullptr);
    g_foamTexHasMips = SUCCEEDED(hr) && g_foamTex != nullptr;
    if(!g_foamTex){
        // Fallback: single-level texture (MIPFILTER off at bind time).
        hr = g_d3dDevice->CreateTexture(FOAM_TEX_N, FOAM_TEX_N, 1,
            0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_foamTex, nullptr);
        g_foamTexHasMips = false;
        if(FAILED(hr) || !g_foamTex){
            g_foamTex = nullptr;
            if(dbglog_throttle("water_nofoam"))
                dbglog("[water] foam texture create failed hr=0x%08X", hr);
            return;
        }
    }

    // Periodic jittered feature-point grid (FOAM_CELLS^2 cells, one point
    // each, jitter inside the cell -> organic, never a regular dot lattice).
    static float s_px[FOAM_CELLS * FOAM_CELLS];
    static float s_py[FOAM_CELLS * FOAM_CELLS];
    const float cell = (float)FOAM_TEX_N / (float)FOAM_CELLS;
    for(int j = 0; j < FOAM_CELLS; j++){
        for(int i = 0; i < FOAM_CELLS; i++){
            s_px[j * FOAM_CELLS + i] = ((float)i + FoamHash2(i, j, 1)) * cell;
            s_py[j * FOAM_CELLS + i] = ((float)j + FoamHash2(i, j, 2)) * cell;
        }
    }

    D3DLOCKED_RECT lr;
    if(FAILED(g_foamTex->LockRect(0, &lr, nullptr, 0))){
        g_foamTex->Release();
        g_foamTex = nullptr;
        g_foamTexHasMips = false;   // flag follows the pointer (recreate path recomputes it)
        return;
    }
    const float blobR = cell * 0.55f;              // bubble rim radius (px)
    const float invBlobR = 1.0f / (blobR > 1e-3f ? blobR : 1e-3f);
    const int cellI = FOAM_TEX_N / FOAM_CELLS;
    for(int y = 0; y < FOAM_TEX_N; y++){
        DWORD *dst = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
        float v = (float)y / (float)FOAM_TEX_N;
        for(int x = 0; x < FOAM_TEX_N; x++){
            // F1 distance to the nearest feature point, toroidal: 3x3 wrapped
            // cell neighbourhood + delta fold into [-N/2, N/2] -> seamless.
            int cx = x / cellI;
            int cy = y / cellI;
            float d1 = 1e9f;
            for(int oy = -1; oy <= 1; oy++){
                for(int ox = -1; ox <= 1; ox++){
                    int ix = (cx + ox + FOAM_CELLS) % FOAM_CELLS;
                    int iy = (cy + oy + FOAM_CELLS) % FOAM_CELLS;
                    float dx = (float)x - s_px[iy * FOAM_CELLS + ix];
                    float dy = (float)y - s_py[iy * FOAM_CELLS + ix];
                    if(dx >  (float)FOAM_TEX_N * 0.5f) dx -= (float)FOAM_TEX_N;
                    if(dx < -(float)FOAM_TEX_N * 0.5f) dx += (float)FOAM_TEX_N;
                    if(dy >  (float)FOAM_TEX_N * 0.5f) dy -= (float)FOAM_TEX_N;
                    if(dy < -(float)FOAM_TEX_N * 0.5f) dy += (float)FOAM_TEX_N;
                    float d = sqrtf(dx * dx + dy * dy);
                    if(d < d1) d1 = d;
                }
            }
            float blob = 1.0f - d1 * invBlobR;     // 1 at the point -> 0 at blobR
            if(blob < 0.0f) blob = 0.0f;
            blob = blob * blob * (3.0f - 2.0f * blob);   // smoothstep the rim
            // Per-pixel white noise breaks the cellular regularity; the
            // large-scale periodic mask varies the coverage across the tile.
            float noise = DetailHash(x, y);
            float u = (float)x / (float)FOAM_TEX_N;
            float mask = 0.55f + 0.45f * DetailValueNoise(u * 4.0f, v * 4.0f, 4);
            float f = (blob * 0.8f + noise * 0.30f) * mask;
            if(f > 1.0f) f = 1.0f;
            if(f < 0.0f) f = 0.0f;
            int c = (int)(f * 255.0f + 0.5f);
            dst[x] = ((DWORD)255 << 24) | ((DWORD)c << 16) | ((DWORD)c << 8) | (DWORD)c;
        }
    }
    g_foamTex->UnlockRect(0);
    if(g_foamTexHasMips)
        g_foamTex->GenerateMipSubLevels();
    dbglog("[water] foam tex %dx%d mips=%d", FOAM_TEX_N, FOAM_TEX_N,
           g_foamTexHasMips ? 1 : 0);
}

// Timecycle values are stored as floats but come from 0-255 DAT columns;
// accept either scale. (Sky colours are shorts 0-255.)
static inline float tc01(float v)
{
    float s = v > 1.5f ? v * (1.0f / 255.0f) : v;
    if(s < 0.0f) s = 0.0f;
    if(s > 1.0f) s = 1.0f;
    return s;
}

static inline float satf(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// ---- R2-06 "water_sun": bounded specular / reflection inputs ----
// The sun lobe and the reflection lobe are both ADDITIVE terms in
// Water_Parallax.hlsl (highlights = spec + foam + pierce + shimmer, then
// + reflection/transmission). specPow/specInt/reflStr came straight from the
// INI (Xbox's reflStr never even went through satf), so an outlier value could
// pump the sun lobe far past 1.0 — the shader only saturates the SUMMED colour,
// which turns a too-hot lobe into a hard 255-white cone instead of a highlight.
// Clamp the INPUTS here, never the effect: every default preset value
// (Xbox 512/1.0/0.55, IV 250/0.6/0.88, V 1118/0.3/0.8525) passes through
// bit-identically, so the authored look is unchanged; a hand-edited or
// corrupted config can no longer inject an unbounded additive term.
// NaN (readfloat can produce it) falls back to `def`; +/-Inf clamps to a bound.
static float waterClampF(float v, float lo, float hi, float def)
{
    if(!(v == v)) return def;   // NaN -> sane default
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

// ============================================================
// W7-D instrumentation: SEH camera readers, relocated verbatim.
//
// The camera deref chains below used to be two inline __try/__except blocks
// inside waterPipe_setRenderState. That function now opens with
// PF_SCOPE("WaterPipe_setRenderState"), and the release build compiles with
// /EHsc (build/obj/Release/skygfx.tlog CL.command.1.tlog) — MSVC rejects
// "cannot use __try in functions that require object unwinding" (C2712) as
// soon as a destructible local (the profile scope) is in scope. Moving only
// the exception frames here keeps waterPipe_setRenderState __try-free so the
// scope can be measured, with behaviour unchanged:
//   * same camera usability gate, same guardDepth nesting
//   * same fallbacks ({0,0,0} / near=0.5 far=1000)
//   * same dbglog_throttle("water_camav") messages
// POD locals only (C2712) — no objects, no try/except nesting.
// ============================================================

static RwV3d WaterSafeCamPos(RwCamera *wcam)
{
    RwV3d out = {0.0f, 0.0f, 0.0f};
    InterlockedIncrement(&g_guardDepth);
    __try {
        RwFrame *camFrame = RwCameraGetFrame(wcam);
        if(camFrame){
            RwMatrix *camLTM = RwFrameGetLTM(camFrame);
            if(camLTM)
                out = camLTM->pos;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){
        if(dbglog_throttle("water_camav"))
            dbglog("[water] camera frame/LTM fault code=0x%08X p=%p -> pos=0",
                   GetExceptionCode(), wcam);
        out.x = out.y = out.z = 0.0f;
    }
    InterlockedDecrement(&g_guardDepth);
    return out;
}

static void WaterSafeClipPlanes(RwCamera *wcam, float *nearOut, float *farOut)
{
    float n = 0.5f, f = 1000.0f;
    InterlockedIncrement(&g_guardDepth);
    __try {
        n = RwCameraGetNearClipPlane(wcam);
        f = RwCameraGetFarClipPlane(wcam);
    } __except(EXCEPTION_EXECUTE_HANDLER){
        if(dbglog_throttle("water_camav"))
            dbglog("[water] camera clip fault code=0x%08X p=%p -> defaults",
                   GetExceptionCode(), wcam);
        n = 0.5f;
        f = 1000.0f;
    }
    InterlockedDecrement(&g_guardDepth);
    *nearOut = n;
    *farOut  = f;
}

void waterPipe_setRenderState(void)
{
    // W7-D instrumentation: pass scope for the whole water pipe setup. Purely
    // additive — no state/math touched, no behaviour change. (The later
    // "WaterPipe_flushDraw" scope measures the per-flush tail where the
    // caller emits the water primitive with our shaders.)
    PF_SCOPE("WaterPipe_setRenderState");

    // ---- W8 dbg-water-black: entry triage (log only) ----
    // The chain log below is emitted at the very END of this function, so a
    // silent session is ambiguous: "smaDraw" never flushed water vs. this
    // function bailed on one of the early-outs. main.cpp's own gate log
    // (dbglog_throttle("water_fallthru")) is gated on
    // TempBufferVerticesStored != 0, so a ZERO-vertex session logs NOTHING
    // anywhere in the water chain. This one entry line removes that
    // blind spot with no behaviour change: it carries the flush counts and
    // the two RT handles, so the next run distinguishes
    //   (a) no entry line at all      -> no water flush / pipe off upstream
    //   (b) entry line, no chain line -> one of the early-outs below hit
    //   (c) both lines                -> the pipe ran (black is not ours)
    if(dbglog_throttle("waterentry")){
        dbglog("[water] entry: enable=%d waterStyle=%d tier=%d verts=%u indices=%u "
               "sceneRT=%p depthRT=%p router=%s",
               config->waterParallaxEnable ? 1 : 0, (int)config->waterStyle,
               (int)config->waterQuality,
               (unsigned)TempBufferVerticesStored,
               (unsigned)TempBufferIndicesStored,
               (void*)g_sceneTexture, (void*)g_depthTexture,
               waterPipe_psTag(waterPipe_pixelShader()));
    }

    // Reset first — only set true after every setup step succeeded, so callers
    // can trust the flag as "water pipe fully bound this draw".
    g_waterParallaxActive = false;

    SyncDevice();
    if(!g_d3dDevice) return;

    // ---- Stage-0 ENTRY raster snapshot (fonts fix) ----
    // Taken HERE, at the very top of the pipe, because every stage-0 consumer
    // below mutates it before the draw: PostFX_CopyDepthToTexture raw-binds
    // g_intzTex then raw-NULLs at its exit (postfx_ssao.cpp), the water bind
    // block overwrites s0 with the scene copy, and the reflection copy shares
    // the texture cache. RwRenderStateGet does NOT AddRef — the capture is a
    // plain pointer, so it can never keep a DEFAULT-pool resource alive across
    // a Reset and needs no release on the early-outs below.
    // The 0xFFFFFFFF sentinel is the failed-Get marker (chars.cpp:386 idiom).
    s_savedS0Raster = NULL;
    {
        void *s0raw = NULL;
        RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &s0raw);
        if(s0raw != (void*)0xFFFFFFFF)
            s_savedS0Raster = (RwRaster*)s0raw;
    }

    // Buffer sanity (BEFORE any state is touched): an impossible count — the
    // live log's 232v/990i flush — must not reach the raw draw with stale
    // index targets. `g_waterParallaxActive` stays false here, so main.cpp's
    // hook falls through to the vanilla CWaterLevel draw (same fallback as
    // the device-lost / missing-shader early-outs).
    if(!WbBufferCountsSane(TempBufferVerticesStored, TempBufferIndicesStored)){
        // Diagnostic (2026-10-29 teal-quads): the throttled receipt below was
        // starved (0 lines despite a proven FAIL sample), so emit an
        // UNCONDITIONAL one-shot first-hit receipt and keep a plain counter
        // appended to the throttled line — a count>0 is then provable even
        // when the throttle eats every per-frame hit.
        static unsigned s_waterBadBufCount = 0;
        static bool s_waterBadBufFirstHit = true;
        s_waterBadBufCount++;
        unsigned v = (unsigned)TempBufferVerticesStored;
        unsigned i = (unsigned)TempBufferIndicesStored;
        double ratio = v ? (double)i / (double)v : 0.0;
        if(s_waterBadBufFirstHit){
            s_waterBadBufFirstHit = false;
            dbglog("[water] bad buffer first hit: verts=%u indices=%u ratio=%.2f sane=%d",
                   v, i, ratio, 0);
        }
        if(dbglog_throttle("water_badbuf"))
            dbglog("[water] bad buffer v=%u i=%u -> vanilla draw (count=%u)",
                   v, i, s_waterBadBufCount);
        // FFP hardening: the caller (main.cpp hooked path) hands this buffer
        // to the vanilla FFP draw — pin waterclear256 through all 3 layers
        // first so it cannot sample a leaked screen-family raster.
        waterPipe_BindVanillaRaster3L();
        return;
    }

    // ---- Z-state snapshot (ENTRY, three layers) — draw-order fix ----
    // The water primitive is a raw RwD3D9DrawIndexedPrimitiveUP (main.cpp)
    // and inherits whatever Z state the previous consumer left on the
    // DEVICE. PostFX_CopyDepthToTexture (postfx_ssao.cpp, tier >= 2) sets
    // ZTEST/ZWRITE FALSE and detaches the depth-stencil surface — any
    // restore miss there leaves Z-test OFF, the surface then passes depth
    // everywhere and draws IN FRONT of all geometry. Snapshot the Z block of
    // every layer NOW, before the depth copy below can move it, and restore
    // exactly these values in waterPipe_restoreRenderState (nothing is
    // hardcoded). Read-only: an early-out below just leaves the snapshot
    // unused. Pre-seed before each Get (a failed Get leaves the out-param
    // untouched).
    s_savedZTestRw  = (void*)TRUE;
    s_savedZWriteRw = (void*)TRUE;
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE,  (void*)&s_savedZTestRw);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&s_savedZWriteRw);
    s_savedZEnableDrv = D3DZB_TRUE; s_savedZWriteDrv = TRUE; s_savedZFuncDrv = D3DCMP_LESSEQUAL;
    RwD3D9GetRenderState(D3DRS_ZENABLE,     &s_savedZEnableDrv);
    RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &s_savedZWriteDrv);
    RwD3D9GetRenderState(D3DRS_ZFUNC,        &s_savedZFuncDrv);
    s_savedZEnableDev = D3DZB_TRUE; s_savedZWriteDev = TRUE; s_savedZFuncDev = D3DCMP_LESSEQUAL;
    g_d3dDevice->GetRenderState(D3DRS_ZENABLE,     &s_savedZEnableDev);
    g_d3dDevice->GetRenderState(D3DRS_ZWRITEENABLE, &s_savedZWriteDev);
    g_d3dDevice->GetRenderState(D3DRS_ZFUNC,        &s_savedZFuncDev);
    // FIX 2b: same entry snapshot for the device FOGCOLOR (raw L3 read — the
    // water pipe never writes it, so this is the frame's own fog colour the
    // restore must hand back; see the static's comment).
    s_savedFogColorDev = 0;
    g_d3dDevice->GetRenderState(D3DRS_FOGCOLOR, &s_savedFogColorDev);
    // Fog bracket completion (grey-veil family): capture the FULL entry fog
    // block — enable (all three layers) + the range/mode device states — so
    // the restore hands back exactly what the frame had, never a forced
    // enable over a foreign range. Pre-seeded to the canonical scene values
    // (a failed Get leaves the out-param untouched).
    s_savedFogEnableRw = (void*)TRUE;
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, (void*)&s_savedFogEnableRw);
    s_savedFogEnableDrv = TRUE;
    s_savedFogTableModeDrv = D3DFOG_LINEAR;
    s_savedFogVertexModeDrv = D3DFOG_LINEAR;
    s_savedFogStartDrv = 0;
    s_savedFogEndDrv = 0x3F800000;
    s_savedFogDensityDrv = 0x3F800000;
    RwD3D9GetRenderState(D3DRS_FOGENABLE,     &s_savedFogEnableDrv);
    RwD3D9GetRenderState(D3DRS_FOGTABLEMODE,  &s_savedFogTableModeDrv);
    RwD3D9GetRenderState(D3DRS_FOGVERTEXMODE, &s_savedFogVertexModeDrv);
    RwD3D9GetRenderState(D3DRS_FOGSTART,      &s_savedFogStartDrv);
    RwD3D9GetRenderState(D3DRS_FOGEND,        &s_savedFogEndDrv);
    RwD3D9GetRenderState(D3DRS_FOGDENSITY,    &s_savedFogDensityDrv);
    s_savedFogEnableDev = TRUE;
    s_savedFogTableModeDev = D3DFOG_LINEAR;
    s_savedFogVertexModeDev = D3DFOG_LINEAR;
    s_savedFogStartDev = 0;
    s_savedFogEndDev = 0x3F800000;
    s_savedFogDensityDev = 0x3F800000;
    g_d3dDevice->GetRenderState(D3DRS_FOGENABLE,     &s_savedFogEnableDev);
    g_d3dDevice->GetRenderState(D3DRS_FOGTABLEMODE,  &s_savedFogTableModeDev);
    g_d3dDevice->GetRenderState(D3DRS_FOGVERTEXMODE, &s_savedFogVertexModeDev);
    g_d3dDevice->GetRenderState(D3DRS_FOGSTART,      &s_savedFogStartDev);
    g_d3dDevice->GetRenderState(D3DRS_FOGEND,        &s_savedFogEndDev);
    g_d3dDevice->GetRenderState(D3DRS_FOGDENSITY,    &s_savedFogDensityDev);
    // ---- Blend bracket snapshot (ENTRY, three layers) — test-7 creator fix ----
    // Same entry-capture idiom as the Z/fog blocks above, and BEFORE the
    // tier >= 2 depth copy / draw-time blend pin can move anything: the
    // restore must hand back the frame's own blend block, never a hardcoded
    // convention. Pre-seeded before each Get (a failed Get leaves the
    // out-param untouched).
    s_savedBlendEnableRw = (void*)FALSE;
    s_savedSrcBlendRw    = (void*)rwBLENDSRCALPHA;
    s_savedDstBlendRw    = (void*)rwBLENDINVSRCALPHA;
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&s_savedBlendEnableRw);
    RwRenderStateGet(rwRENDERSTATESRCBLEND,  (void*)&s_savedSrcBlendRw);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&s_savedDstBlendRw);
    s_savedBlendEnableDrv = FALSE;
    s_savedSrcBlendDrv = D3DBLEND_SRCALPHA;
    s_savedDstBlendDrv = D3DBLEND_INVSRCALPHA;
    RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s_savedBlendEnableDrv);
    RwD3D9GetRenderState(D3DRS_SRCBLEND,  &s_savedSrcBlendDrv);
    RwD3D9GetRenderState(D3DRS_DESTBLEND, &s_savedDstBlendDrv);
    s_savedBlendEnableDev = FALSE;
    s_savedSrcBlendDev = D3DBLEND_SRCALPHA;
    s_savedDstBlendDev = D3DBLEND_INVSRCALPHA;
    g_d3dDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &s_savedBlendEnableDev);
    g_d3dDevice->GetRenderState(D3DRS_SRCBLEND,  &s_savedSrcBlendDev);
    g_d3dDevice->GetRenderState(D3DRS_DESTBLEND, &s_savedDstBlendDev);

    void *ps = waterPipe_pixelShader();
    if(!config->waterParallaxEnable || !ps || !Water_VS){
        // FFP hardening: main.cpp's hooked path falls through to the vanilla
        // FFP water draw on this exit — pin waterclear256 through all 3
        // layers first (park-and-set defeats the RW value-compare no-op that
        // let a leaked screen-family raster survive on the device).
        waterPipe_BindVanillaRaster3L();
        return;
    }
    if(g_d3dDevice->TestCooperativeLevel() != D3D_OK){
        if(dbglog_throttle("water_lost"))
            dbglog("[water] device not OK, falling back to original draw");
        waterPipe_BindVanillaRaster3L();
        return;
    }

    // Quality tier 0-3: gates cost inside the shader (uniform branch on
    // c11.x) AND the scene/depth copies here — those only run at tier >= 2.
    int tier = config->waterQuality;
    if(tier < 0) tier = 0;
    if(tier > 3) tier = 3;
    bool needRT = (tier >= 2);

    D3DVIEWPORT9 vp;
    g_d3dDevice->GetViewport(&vp);
    if(vp.Width < 1 || vp.Height < 1){
        if(dbglog_throttle("water_novp"))
            dbglog("[water] degenerate viewport %ux%u -> FFP fallback",
                   (unsigned)vp.Width, (unsigned)vp.Height);
        waterPipe_BindVanillaRaster3L();   // FFP hardening (vanilla draw follows)
        return;
    }

    // Copy scene for refraction (tier >= 2 only)
    // G1: reflValid = the flipped reflection copy is live this flush (only
    // the IV/V cores sample it, so the copy is style-gated too); it drives
    // the PS c7.w flag + the s3 bind below. Gate off / tier < 2 / RT create
    // failed -> flag stays 0 and s3 gets NULL: the shader's SSR mix term is
    // then exactly 0 (analytic sky tint, bit-identical to before).
    bool reflValid = false;
    if(needRT){
        EnsureSceneTexture(vp.Width, vp.Height);
        if(!g_sceneTexture){
            // PERF/widening (2026-10-31): a failed refraction RT used to bail
            // the whole pipe to the FFP fallback — flat untextured water
            // adjacent to pipe water (the seam family, 212 flatpath receipts
            // in the 03:01 run's config-off window). The PS's tier gate is a
            // uniform branch: DEGRADE to tier 1 (no s0/s2 fetches, SRCALPHA
            // composite) and keep the SHADERED draw — strictly better than
            // FFP and safe (the shader runs its tier<2 path with zero
            // texture fetches). Transient failures retry next flush.
            tier = 1;
            needRT = false;
            if(dbglog_throttle("water_nort"))
                dbglog("[water] refraction RT unavailable (%ux%u) -> degraded "
                       "to tier 1 (shadered draw, no FFP fallback)",
                       vp.Width, vp.Height);
        }else if(CopySceneToTexture()){
            // G1 screen-space reflection: second capture of the scene, vertically
            // flipped (IV/V water-refs.md "High/Ultra = screen-space reflect").
            if(config->waterStyle != 0){
                EnsureReflTexture(vp.Width, vp.Height);
                if(g_reflTexture)
                    reflValid = CopySceneFlippedToTexture();
            }
        }else{
            // WATER AUDIT #1: the scene copy failed (StretchRect HRESULT now
            // checked inside CopySceneToTexture). Same degrade as the
            // RT-create-failure path above: tier 1 = shadered draw, no s0/s2
            // fetches, SRCALPHA composite — strictly better than refracting
            // stale/undefined RT content all session. Transient failures
            // retry next flush.
            tier = 1;
            needRT = false;
            if(dbglog_throttle("water_copyfail"))
                dbglog("[water] scene copy failed (%ux%u) -> degraded to "
                       "tier 1 (shadered draw, no refraction)",
                       vp.Width, vp.Height);
        }
    }

    // Copy scene depth (INTZ -> R32F) for depth-based shore blending /
    // translucency (tier >= 2 only — this is the expensive helper). Runs per
    // water draw so a second buffer flush sees the first flush's water
    // surface and blends into it instead of popping.
    bool depthValid = false;
    if(needRT){
        EnsureDepthTexture(vp.Width, vp.Height);
        if(g_depthTexture){
            depthValid = PostFX_CopyDepthToTexture(g_depthTexture, g_depthWidth, g_depthHeight);
            // The depth-copy helper juggles RT/DS/raw states — flush RW's render
            // state cache so every RwRenderStateSet below re-applies from scratch.
            _rwD3D9RenderStateFlushCache();
        }
    }

    // ---- W2 wind-driven swell: one-time vertex-texture-fetch probe ----
    // The Water_VS swell samples TWO vertex textures (vs_3_0 texldl, device
    // stages 256/257 — a bank separate from the PS s0-s4): the R32F depth
    // copy (per-vertex shore gate) and the SkyNoise universal tile (spatial
    // gust field). D3D9 has no pre-draw VTF capability flag, so the probe is
    // the bind HRESULT itself: a driver that refuses stage-256 binds disables
    // BOTH features for the session (c12.z/c12.y upload 0 -> the shader's
    // uniform gates never execute the texldl -> the swell runs at its base
    // amplitude with no shore gate, the pre-W2 behaviour). Probed with the
    // depth texture on the first tier>=2 flush that has one; until then the
    // features stay off (probe state -1). s_vtfProbe lives at file scope —
    // waterPipe_restoreRenderState reads it too.
    if(s_vtfProbe < 0 && g_depthTexture){
        HRESULT vhr = g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0, g_depthTexture);
        s_vtfProbe = SUCCEEDED(vhr) ? 1 : 0;
        g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);   // park; real bind below
        if(s_vtfProbe == 0)
            dbglog("[water] vertex texture fetch refused (stage 256 hr=0x%08X) "
                   "-> swell shore gate + SkyNoise gust OFF (base swell only)", vhr);
        else
            dbglog("[water] vertex texture fetch OK (stage 256 probe) -> "
                   "swell shore gate + SkyNoise gust live");
    }

    // Detail normal texture (procedurally generated once, mip-mapped)
    EnsureDetailNormalTex();
    // M2 foam texture (procedurally generated once, mip-mapped).
    // WATER AUDIT #4: tier 0/1 never reach the PS foam block (all three
    // Water_Parallax cores gate it on `tier >= 2`, and c16.x is
    // (needRT && g_foamTex) = 0 there), so the one-time 256² CPU generation
    // was pure waste at those tiers. Gate the create behind the same
    // condition the draw will actually run at (needRT is post-degrade); a
    // later tier>=2 flush creates it on first use and c16.x flips to 1.
    if(needRT)
        EnsureFoamTex();

    // Delta time — WATER AUDIT #3: accumulate ONCE PER RW RENDER FRAME, not
    // per flush. This setup runs per FLUSH (SA can flush the water
    // TempBuffer several times per frame), so the old unconditional
    // accumulate advanced g_waterTime proportionally to the flush count —
    // multi-flush frames animated visibly faster than single-flush ones.
    // Guard on RWSRCGLOBAL(renderFrame) like the clouds noise upload
    // (pipelinecommon.cpp pipeEnsureIBLBuffer) / the grass state gate
    // (grasssys.cpp): the FIRST flush of each frame takes the QPC delta,
    // later flushes reuse it. RwUInt32 counter (treeWind clock idiom) so a
    // wrap can never skip a tick; mirror/shadow re-entries that bump
    // renderFrame get their own delta — correct, that is a new frame pass.
    static RwUInt32 s_lastTimeFrame = (RwUInt32)-1;
    RwUInt32 waterTimeFrame = RWSRCGLOBAL(renderFrame);
    if(waterTimeFrame != s_lastTimeFrame){
        s_lastTimeFrame = waterTimeFrame;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        static LARGE_INTEGER lastTime = {0};
        static double perfFreqInv = 0.0;
        if(perfFreqInv == 0.0){
            LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
            perfFreqInv = 1.0 / (double)freq.QuadPart;
        }
        if(lastTime.QuadPart == 0) lastTime = now;
        g_waterTime += (float)((double)(now.QuadPart - lastTime.QuadPart) * perfFreqInv);
        lastTime = now;
    }

    // ---- Water body classification (W7-B) — per-vertex, at flush ----
    // Every early-out above has passed, so this draw WILL run with our
    // shaders. Writing bodyType/blend into the Im3D vertex NORMAL channel
    // any earlier could reach the vanilla fall-through draw at
    // main.cpp:2663, which must see pristine normals.
    // W7-B gate (INI waterBodyClassify, default 1): 0 skips the classify
    // entirely so the Im3D NORMAL channel keeps the vanilla data, VS c10.x
    // = 0 zeroes TEXCOORD5 and c14/c15 upload all-zero -> the PS's
    // WaterBodyContract mag gate pins every scale to 1.0 (vanilla identity).
    bool bodyOn = config->waterBodyClassify ? WbClassifyFlush() : false;

    // ---- W7-D instrumentation: flush/draw point ----
    // This IS the point of no return: classification of the Im3D buffer is
    // done, every setup step above succeeded, and as soon as this function
    // returns the caller (main.cpp:2696 RwD3D9DrawIndexedPrimitiveUP,
    // D3DPT_TRIANGLELIST, numPrims = indices/3) emits this flush with OUR
    // shaders. The scope therefore covers the bind tail + the handoff, and
    // the counters describe the one primitive about to be drawn.
    // NOTE: explicit PfScope object (NOT the PF_SCOPE macro) because the
    // shared macro pastes a literal `pf_scope___LINE__` under MSVC's
    // traditional preprocessor (__LINE__ is not expanded after ##), so two
    // PF_SCOPE uses in one TU collide (C2374/C2086). The header documents
    // Close() as idempotent for exactly this pattern — it is called at the
    // end of the function so the scope measures the flush tail precisely.
    PfScope pf_scope_flushDraw("WaterPipe_flushDraw");
    PfDraw((int)TempBufferVerticesStored, (int)(TempBufferIndicesStored / 3));
    if(g_pfEnable)
        PfNote("water flush style=%d tier=%d body=%d verts=%d",
               (int)config->waterStyle, tier, bodyOn ? 1 : 0,
               (int)TempBufferVerticesStored);

    // Set shaders
    RwD3D9SetVertexShader(Water_VS);
    RwD3D9SetPixelShader(ps);
    // W7-D bind points: record the exact CSO handles this flush runs with
    // (idle when g_pfEnable == 0 — profiler.cpp no-ops the calls).
    PfBindVS(Water_VS, "IDR_WATER_VS");
    PfBindPS(ps, waterPipe_psTag(ps));

    // VS constants
    float wvp[16];
    // Water TempBuffer verts are world-space (identity world uploaded at VS c4
    // below), but _rwD3D9VSGetComposedTransformMatrix composes with
    // D3D9ActiveTransform — the world matrix left set by the last building/
    // vehicle atomic draw (rpworld.h: "Call SetActiveWorldMatrix before...
    // "). That stale world displaces the water quads by a random object's
    // transform (quads land over land/props) and disagrees with the identity
    // world used for worldPos/viewDir. Null it for the compose only, then
    // restore — D3D9Transform itself is untouched on the NULL path.
    //
    // Cached-viewproj bug: with D3D9ActiveTransform == NULL,
    // _rwD3D9VSGetComposedTransformMatrix returns the CACHED
    // _RwD3D9ActiveViewProjTransform (0xC97C64), which can still hold a
    // reflection/envmap camera's view*proj from an earlier mid-frame pass.
    // The water is then projected into the wrong clip space (the "water veil"
    // over land/sky). Invalidate it so the helper recomposes from the LIVE
    // _RwD3D9D3D9ViewTransform/_RwD3D9D3D9ProjTransform
    // (pipelinecommon.cpp:299-314). The D3D9ActiveTransform workaround below
    // is kept — it neutralises the stale world matrix on the compose path.
    extern RwMatrix *&D3D9ActiveTransform;
    extern D3DMATRIX *&_RwD3D9ActiveViewProjTransform;
    RwMatrix *savedWorldM = D3D9ActiveTransform;
    D3D9ActiveTransform = NULL;
    _RwD3D9ActiveViewProjTransform = NULL;
    _rwD3D9VSGetComposedTransformMatrix(wvp);
    D3D9ActiveTransform = savedWorldM;
    RwD3D9SetVertexShaderConstant(0, wvp, 4);

    float world[16] = {0};
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    RwD3D9SetVertexShaderConstant(4, world, 4);

    RwV3d camPos = {0, 0, 0};
    // Hardened: alignment/wild check first, then SEH around the FIRST deref
    // chain (camera -> frame -> LTM -> pos) so a corrupted-but-aligned camera
    // object fails open to {0,0,0} instead of AVing. POD locals only (C2712).
    // W7-D: the frame itself now lives in WaterSafeCamPos (see its comment);
    // gate, fallbacks and logs below are unchanged.
    // 0xC170C4 is unreliable (docs: "normally holds NULL"; corrupted runs leave
    // float bits there) — second source: Scene.camera, the camera RW is
    // actually rendering the scene with (gta.h GlobalScene).
    RwCamera *wcam = ccamera;
    if(!waterCamUsable(wcam))
        wcam = Scene.camera;
    if(waterCamUsable(wcam))
        camPos = WaterSafeCamPos(wcam);
    float camTime[4] = {camPos.x, camPos.y, camPos.z, g_waterTime};
    RwD3D9SetVertexShaderConstant(8, camTime, 1);

    // Sun direction from pDirect (directional light), matching vehiclePipe pattern
    extern RpLight *&pDirect;
    RwV3d sunDirVec = {0, 0, 0};
    if(pDirect){
        RwFrame *sunFrame = RpLightGetFrame(pDirect);
        if(sunFrame){
            RwMatrix *sunLTM = RwFrameGetLTM(sunFrame);
            if(sunLTM)
                sunDirVec = *RwMatrixGetAt(sunLTM);
        }
    }
    float sunDirNeg[4] = {-sunDirVec.x, -sunDirVec.y, -sunDirVec.z, 1.0f};
    RwD3D9SetVertexShaderConstant(9, sunDirNeg, 1);

    // VS c10 = (classifyGate, 0, 0, 0) — waterPipe-owned gate, NOT a PS c14
    // mirror (reconciled to the authoritative Water_Parallax contract; see
    // the header block and the "W7-B contract" upload below). Water_VS only
    // reads c10.x: it scales TEXCOORD5 by it, so a draw where classification
    // was off forwards plain zeros instead of stale/garbage NORMAL data, and
    // with c14/c15 also all-zero the PS's WaterBodyContract mag gate pins
    // every scale to the vanilla 1.0.
    float bodyP[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if(bodyOn)
        bodyP[0] = 1.0f;            // classify gate — TEXCOORD5 codes valid
    RwD3D9SetVertexShaderConstant(10, bodyP, 1);

    // VS c11/c12 — W1 "animated water" restore + W2 wind-driven sea.
    //   c11 = (swellAmp, timeScale, windDirX, windDirY)
    //   c12 = (windStrength, gustAmp, shoreFadeW, vtfOK)
    // Gated vertical-only swell in Water_VS (two waves, 0.54 m total at the
    // shipped scale — W2 raised the old 0.16 m, which was sub-pixel at SA
    // camera distances and read as "the wave VS isn't working"; still 3x
    // below the old a4e81f8 Gerstner whose ~2.4 m displacement popped
    // through the coast and forced the b7396c4 flat rewrite). c11.x = 0
    // would reproduce that flat rewrite bit for bit; the upload is
    // UNCONDITIONAL every draw so a style/VS switch can never inherit stale
    // wave constants. No INI key yet (the config struct is main.cpp-owned)
    // — tune the literals here; a SkyGfx/waterSwell knob can be wired later
    // in readIni.
    //
    // W2 wind source = the SAME globals treeWind.cpp sways the trees with
    // (gta.h/gta.cpp: CWeather__Wind @0xC812F0, CWeather__WindDir
    // @0xC813E0): "the same way wind moves trees it moves the sea".
    //   c11.zw = the wind direction, NORMALIZED here with an epsilon guard;
    //            a degenerate game vector falls back to the classic dirA
    //            (0.9497, 0.3328) so the shader's lerp target is always a
    //            unit vector (no normalize-of-zero in the VS).
    //   c12.x  = CWeather__Wind clamped [0,1] — amplitude scale
    //            (0.55 + 0.45*wind) and gust weighting in the VS.
    //   c12.y  = SkyNoise gust amplitude (0.5), 0 when the universal tile
    //            is unavailable or VTF was refused — the VS's uniform gate
    //            then never executes the gust texldl.
    //   c12.z  = shore-gate width in metres = the INI waterShoreFade range
    //            (the same band the PS shore algebra lives in), 0 unless
    //            tier >= 2 AND the depth copy is valid AND VTF probed OK —
    //            the gate fades the swell to zero over the shallow band so
    //            the raised amplitude can never fight the beach geometry
    //            (the 0.16 m swell was already sliding the waterline
    //            +-swell/slope across near-flat shores: the "edge of sea
    //            not rendering" defect).
    //   c12.w  = 1 when the depth copy is bound at VS sampler s0 this flush.
    float windX = CWeather__WindDir.x, windY = CWeather__WindDir.y;
    float wLen = sqrtf(windX * windX + windY * windY);
    if(wLen < 1e-4f){
        windX = 0.9497f; windY = 0.3328f;   // unit-length classic dirA
    }else{
        float invLen = 1.0f / wLen;
        windX *= invLen; windY *= invLen;
    }
    float windStr = CWeather__Wind;
    if(windStr < 0.0f) windStr = 0.0f;
    if(windStr > 1.0f) windStr = 1.0f;
    bool vtfShoreOK = (tier >= 2 && depthValid && s_vtfProbe == 1 && g_depthTexture != nullptr);
    // SkyNoise::UniversalTexture() lazily creates the tile (needs a live
    // device — we have one this deep in the flush); NULL = create failed ->
    // gust off for this draw (the tile usually appears on the next flush).
    IDirect3DTexture9 *gustTex = (s_vtfProbe == 1) ? SkyNoise::UniversalTexture() : nullptr;
    float swellP[4] = {1.0f, 1.0f, windX, windY};
    RwD3D9SetVertexShaderConstant(11, swellP, 1);
    float windP[4] = {windStr,
                      (gustTex != nullptr) ? 0.5f : 0.0f,
                      vtfShoreOK ? waterClampF(config->waterShoreFade, 0.5f, 8.0f, 2.0f) : 0.0f,
                      vtfShoreOK ? 1.0f : 0.0f};
    RwD3D9SetVertexShaderConstant(12, windP, 1);
    // Stash the gust tile pointer for the bind block below (same flush).
    g_waterGustTex = gustTex;

    // ---- Timecycle water colour / alpha + sky reflection tint ----
    // SA's CTimeCycle::m_CurrentColours carries the interpolated timecyc
    // values (timecyc per-hour/weather waterR/G/B/A + sky columns). The INI
    // waterDeep*/waterShallow* keys become optional overrides (req 4).
    float shR, shG, shB, dpR, dpG, dpB, waterAlpha;
    float skyTopP[4] = {0, 0, 0, 0};
    float skyBotP[4] = {0, 0, 0, 0};
    float lightP[4] = {0, 0, 0, 0};    // c12 = timecycle ambient rgb + sky luma
    float directP[4] = {0, 0, 0, 0};   // c13 = timecycle directional rgb + day
    {
        CColourSet &tc = CTimeCycle__m_CurrentColours;
        // Reflection tint — always timecycle: sky bottom = horizon colour
        // (grazing), sky top = zenith colour.
        skyTopP[0] = satf((float)tc.skyTopR / 255.0f);
        skyTopP[1] = satf((float)tc.skyTopG / 255.0f);
        skyTopP[2] = satf((float)tc.skyTopB / 255.0f);
        skyBotP[0] = satf((float)tc.skyBotR / 255.0f);
        skyBotP[1] = satf((float)tc.skyBotG / 255.0f);
        skyBotP[2] = satf((float)tc.skyBotB / 255.0f);

        // ---- Timecycle light (defect: night water glowed white) ----
        // Ambient/directional are ALREADY 0-1 in CColourSet (the game divides
        // the timecyc.dat columns by 255, weather.cpp blends the same way) —
        // do NOT divide again. Neither can separate day from night here: the
        // custom timecycle pins ambient near 0 and ships Dir=255 in EVERY
        // slot, so the day/night gate rides the sky luminance instead — the
        // same "sky the player sees" signal pipelinecommon's night ceiling
        // uses (gta.h CColourSet has no fog colour column; skyBot IS the
        // horizon/fog colour distant water fades into).
        lightP[0] = satf(tc.ambientR);
        lightP[1] = satf(tc.ambientG);
        lightP[2] = satf(tc.ambientB);
        float topL = 0.2126f*skyTopP[0] + 0.7152f*skyTopP[1] + 0.0722f*skyTopP[2];
        float botL = 0.2126f*skyBotP[0] + 0.7152f*skyBotP[1] + 0.0722f*skyBotP[2];
        float skyL = 0.5f * (topL + botL);
        lightP[3] = skyL;

        directP[0] = satf(tc.directionalR);
        directP[1] = satf(tc.directionalG);
        directP[2] = satf(tc.directionalB);
        // 0 = full night (sky luma < 0.12), 1 = day (>= 0.22). Dusk/dawn ramp
        // in between and the timecycle lerp smooths it hour to hour.
        // skyL < 0.003 = timecycle not populated yet (startup frame) — same
        // no-data test pipelinecommon's night ceiling uses; treat as daylight
        // so the first frames render the authored colour instead of black.
        float dayF = (skyL - 0.12f) * (1.0f / 0.10f);
        if(skyL < 0.003f) dayF = 1.0f;
        if(dayF < 0.0f) dayF = 0.0f;
        if(dayF > 1.0f) dayF = 1.0f;
        directP[3] = dayF;

        if(config->waterUseTimecycle){
            dpR = tc01(tc.waterR);
            dpG = tc01(tc.waterG);
            dpB = tc01(tc.waterB);
            // SA's OWN water colour quirk from CWaterLevel (col = col*0.65 +
            // 0.27, water-refs.md "HD-water color quirk" 0x6E716B): lifts AND
            // desaturates the authored colour — this is what vanilla renders.
            // It replaces the old dp*2.4+0.08 boost, which over-saturated the
            // day colour into turquoise AND blew the 85/85/65 night colour out
            // to near-white ("toxic sludge"). Derived from the timecyc colour,
            // so it tracks time of day for free.
            float bR = satf(dpR * 0.65f + 0.27f);
            float bG = satf(dpG * 0.65f + 0.27f);
            float bB = satf(dpB * 0.65f + 0.27f);
            // Shallow = same body over a bright bottom: a small desaturating
            // lift off the base (NOT a second hardcoded brightness ramp), so
            // the shallow->deep step stays small enough to read as a ramp.
            shR = satf(bR + (1.0f - bR) * 0.18f);
            shG = satf(bG + (1.0f - bG) * 0.18f);
            shB = satf(bB + (1.0f - bB) * 0.18f);
            dpR = bR;
            dpG = bG;
            dpB = bB;
            waterAlpha = tc01(tc.waterA);
            if(waterAlpha < 0.05f) waterAlpha = 0.05f;
        }else{
            shR = config->waterShallowR;
            shG = config->waterShallowG;
            shB = config->waterShallowB;
            dpR = config->waterDeepR;
            dpG = config->waterDeepG;
            dpB = config->waterDeepB;
            waterAlpha = 1.0f;
        }
    }

    // ---- Style constant presets (waterStyle switches the entry + preset) ----
    // Xbox: the bounded INI knobs (the rewritten core is user-tunable).
    // IV:   tighter Blinn glints, stronger capped reflection, distance normal
    //       fade 1 - d^2*4e-4 (water-refs.md IV section).
    // V:    watertune SpecularFalloff 1118 at low intensity, damped/flattened
    //       reflection baked in-shader (0.5 + lerp(NdotV,1,0.3); P2 was 5/6),
    //       stronger reflection preset (water-refs.md V section).
    float specPow = waterClampF(config->waterSpecularPower,     8.0f, 4096.0f, 512.0f);
    float specInt = waterClampF(config->waterSpecularIntensity, 0.0f,    1.0f,   1.0f);
    float reflStr = waterClampF(config->waterReflectionStrength, 0.0f,   1.0f,  0.55f);
    float distFadeK = 0.0f;     // c11.y — IV distance normal fade coefficient
    float pierceScale = 0.35f;  // c11.z — tier-3 sun pierce intensity
    if(config->waterStyle == 1){          // GTA IV preset
        specPow = 250.0f;
        specInt = config->waterSpecularIntensity * 0.6f;
        reflStr = satf(config->waterReflectionStrength * 1.6f);
        distFadeK = 4e-4f;
    }else if(config->waterStyle == 2){    // GTA V preset (watertune)
        specPow = 1118.0f;
        specInt = config->waterSpecularIntensity * 0.3f;
        reflStr = satf(config->waterReflectionStrength * 1.55f);
        pierceScale = 0.5f;
    }
    // R2-06: cap after the per-style multipliers too. IV/V already route the
    // reflection through satf; this is the Xbox path's missing cap and the
    // guard for the IV/V intensity multipliers (0.6 / 0.3) on an outlier base.
    // specInt is the max possible sun-specular contribution (pow() <= 1), so
    // pinning it to 1.0 bounds the additive lobe at exactly white, never above.
    specInt = waterClampF(specInt, 0.0f, 1.0f, 1.0f);
    reflStr = waterClampF(reflStr, 0.0f, 1.0f, 0.55f);
    specPow = waterClampF(specPow, 8.0f, 4096.0f, 512.0f);

    // PS constants — INI-driven knobs + timecycle colours.
    // W7-A gate (INI waterShoreFoam, default 1): 0 pushes the foam band out
    // of reach. The IV/Xbox cores window the band on timeParams.z/w, so those
    // go to 0 (the band collapses to the waterline sliver); that alone is NOT
    // enough — the V core's foam path never reads the threshold (it multiplies
    // *shoreFade), so the shore-coverage range is pushed out of reach too
    // (see c2.w below): WaterShoreCoverage -> 0 -> shoreFade ~0 -> the foam
    // term vanishes in EVERY core, and the LR smooth-shoreline coverage band
    // goes with it (that is the "off" behaviour the W7-A gate documents).
    float foamThresh = config->waterShoreFoam ? config->waterFoamThreshold : 0.0f;
    float foamSoft   = config->waterShoreFoam ? config->waterFoamSoftness   : 0.0f;
    float timeP[4] = {g_waterTime, 1.0f, foamThresh, foamSoft};
    RwD3D9SetPixelShaderConstant(0, timeP, 1);

    // c1 = (normalStrength, tileScale, fresnelPower, reflectionStrength)
    // R2-06: normalStrength/fresnelPower are bounded for the same reason as
    // specPow/specInt above — fresnelPower < 1 would pin the reflection lobe
    // near its cap at every angle, normalStrength feeds NdotH. Defaults 1/5
    // are unchanged. reflStr was clamped in the preset block.
    float waterP[4] = {waterClampF(config->waterNormalStrength, 0.0f, 4.0f, 1.0f),
                       // R2-06 policy: NaN/Inf INI values must never reach the
                       // shader (NaN UVs fetch NaN -> black water). Bands are
                       // defense-only: the debug-UI range [0.1, 8] passes
                       // bit-identically; NaN falls back to the 1.0 default.
                       waterClampF(config->waterTileScale, 0.01f, 100.0f, 1.0f),
                       waterClampF(config->waterFresnelPower, 1.0f, 64.0f, 5.0f),
                       reflStr};
    RwD3D9SetPixelShaderConstant(1, waterP, 1);

    // c2 = (specularPower, specularIntensity, translucency, shoreFadeRange)
    // R2-06: specPow/specInt are the bounded values from the preset block
    // (pow() <= 1, so specInt is the exact cap of the additive sun lobe).
    // W7-A gate half 2: waterShoreFoam=0 -> 1e6 m coverage range, so
    // WaterShoreCoverage(thickness, 1e6) -> 0 (eps-guarded in HLSL, no NaN)
    // and the V-core foam + the LR shoreline band both vanish. Side effect
    // (accepted, part of "off = vanilla"): the frontFade seam divisor uses
    // the same range, so the junction ramp is disabled too — pre-W7-A water
    // had no junction ramp either.
    float shoreRange = config->waterShoreFoam ? config->waterShoreFade : 1.0e6f;
    float specP[4] = {specPow, specInt,
                      // R2-06 policy (see c1): opacity multiplier, NaN -> 1.0
                      // default; debug-UI range [0, 2] passes bit-identically.
                      waterClampF(config->waterTranslucency, 0.0f, 100.0f, 1.0f),
                      shoreRange};
    RwD3D9SetPixelShaderConstant(2, specP, 1);

    // ---- R2-06 receipt: exact sun/reflection constants for this draw ----
    // One-shot (not throttled): style + tier + the three bounded inputs that
    // feed the additive specular/reflection chain. Log-only; no render path
    // reads this latch. Emitted after the clamps so it reports the values the
    // PS actually receives, not the raw INI ones.
    if(!s_waterSunReceiptLogged){
        s_waterSunReceiptLogged = true;
        dbglog("[water_sun] style=%d tier=%d specPow=%f specInt=%f reflStr=%f",
               (int)config->waterStyle, tier, specPow, specInt, reflStr);
    }

    // c3 = shallow rgb + deepRange (metres of shallow->deep ramp)
    // P3 sea-floor fix: deepRange 20 -> 6. The shader derives its half-
    // opacity column as max(shallowColor.w * 0.5, 0.5) (WaterBodyOpacity), so
    // w = 20 gave a 10 m half-opacity column -> ~0.065 body opacity in 1 m
    // shallow water -> the seabed showed through nearly unattenuated and
    // composed with the refraction lift into the blown near-white floor. 6
    // halves that to 3 m (bodyT ~0.2 at 1 m), the shallow->deep colour ramp
    // still converges monotonically (now at 6 m), and the tier 0/1 zero-fetch
    // thickness estimate scales with it (max(w*2,1) = 12 m). Hardcoded
    // literal, NOT INI-driven — no new INI key (deliberate tuning change,
    // waterline-contrast patch set).
    float shallow[4] = {shR, shG, shB, 6.0f};
    RwD3D9SetPixelShaderConstant(3, shallow, 1);

    // c4 = deep rgb + base water alpha (timecycle waterA)
    float deep[4] = {dpR, dpG, dpB, waterAlpha};
    RwD3D9SetPixelShaderConstant(4, deep, 1);

    float invW = 1.0f / (float)(vp.Width  > 0 ? vp.Width  : 1);
    float invH = 1.0f / (float)(vp.Height > 0 ? vp.Height : 1);
    float screenP[4] = {(float)vp.Width, (float)vp.Height, invW, invH};
    RwD3D9SetPixelShaderConstant(5, screenP, 1);
    RwD3D9SetPixelShaderConstant(6, sunDirNeg, 1);
    // c7 = (camPosX, camPosY, camPosZ, refl-valid flag). The PS never reads
    // c7.xyz today (viewDir arrives as an interpolator), so the FREE .w
    // carries the G1 screen-space-reflection validity: 1 = g_reflTexture is
    // bound at s3 AND freshly copied this flush, 0 = the shader's SSR mix
    // term is exactly 0 (analytic sky tint only, bit-identical to pre-G1).
    float camPosPS[4] = {camPos.x, camPos.y, camPos.z, reflValid ? 1.0f : 0.0f};
    RwD3D9SetPixelShaderConstant(7, camPosPS, 1);

    float nearClip = 0.5f, farClip = 1000.0f;
    // Hardened: same usability check + SEH for the clip-plane deref chain
    // (fresh load of 0xC170C4, matching the original double-read semantics).
    // W7-D: the frame itself now lives in WaterSafeClipPlanes (see its
    // comment); defaults and logs are unchanged.
    RwCamera *wcamClip = ccamera;
    if(!waterCamUsable(wcamClip))
        wcamClip = Scene.camera;
    if(waterCamUsable(wcamClip))
        WaterSafeClipPlanes(wcamClip, &nearClip, &farClip);
    // c8 = (nearClip, farClip, depthValid, refractScale)
    // R2-06 policy (see c1): refractScale feeds a UV offset — a NaN would
    // poison refractUV end to end (black refraction). Debug-UI range [0, 4]
    // passes bit-identically; NaN -> 1.0 default.
    float clipP[4] = {nearClip, farClip, depthValid ? 1.0f : 0.0f,
                      waterClampF(config->waterParallaxScale, 0.0f, 100.0f, 1.0f)};
    RwD3D9SetPixelShaderConstant(8, clipP, 1);

    // VS c13 = (nearClip, farClip, 0, 0) — W2: the SAME linearization pair
    // the PS receives as c8.xy, so the VS shore gate's depth linearization
    // (Water_VS texldl -> sceneZ) and the PS's LinearizeDepth agree exactly.
    // Uploaded after the clip computation above; before the draw, like every
    // other VS constant here.
    float clipVS[4] = {nearClip, farClip, 0.0f, 0.0f};
    RwD3D9SetVertexShaderConstant(13, clipVS, 1);

    RwD3D9SetPixelShaderConstant(9, skyTopP, 1);
    RwD3D9SetPixelShaderConstant(10, skyBotP, 1);

    // c11 = (qualityTier, distFadeK, sunPierceScale, fresnelSplitFlag)
    // The flag drives the composite form in Water_Parallax.hlsl: 1 = the
    // fresnel split (reflection AND refraction live per view direction),
    // 0 = the legacy lerp(scene, surf, bodyOpacity) rollback. Default 1 —
    // it MUST be uploaded, c11.w == 0 selects the legacy path.
    float qualP[4] = {(float)tier, distFadeK, pierceScale,
                      (float)(g_waterFresnelSplit ? 1 : 0)};
    RwD3D9SetPixelShaderConstant(11, qualP, 1);

    // c12/c13 = timecycle light (ambient rgb + sky luma, directional rgb + day
    // factor) — the water body/highlights follow it exactly like the other
    // lighting models instead of an unlit hardcoded brightness (night glow fix).
    RwD3D9SetPixelShaderConstant(12, lightP, 1);
    RwD3D9SetPixelShaderConstant(13, directP, 1);

    // ---- W7-B contract: water body constants c14/c15 ----
    // RECONCILED to the AUTHORITATIVE PS contract (Water_Parallax.hlsl
    // :123-124 + WaterBodyContract :333-401, shipped by lane W7-A):
    //   c14 = (bodyType, edgeBlend, foamScale, shoreScale)
    //   c15 = (normScale, opacityScale, glitterScale, spare)
    //
    // Per-vertex channel (Water_VS TEXCOORD5 <- Im3D NORMAL <- WbClassifyFlush):
    //   x = bodyType code   0=OCEAN 1=SHALLOW 2=INTERIOR 3=RIVER 4=POND 5=LAKE
    //                        (always inside the PS's [-0.25, 5.25] range check)
    //   y = bodyBlend       0..1 (1.0 mid-plane; ramps to 0 within 5 m of an
    //                        edge bordering a DIFFERENT body type only)
    //   z = signed aspect   sign = plane long axis (+ = X, - = Y), |z| =
    //                        extent ratio >= 1 (cap 64). SPARE for the PS
    //                        today: WaterBodyContract reads only dv.x/dv.y —
    //                        SA flowX/Y are all zero (water-refs.md:162), so
    //                        this is the future river-scroll axis, kept in
    //                        the channel until a scroll consumer ships.
    //   w = 0
    //
    // Draw-level constants (SCHEME 1 = per-vertex; these are the quad-level
    // scheme's slots / optional overrides, NOT a second copy of the codes):
    //   c14.x = 0 (unset) — draw-level bodyType FALLBACK; per-vertex dv.x
    //           wins whenever |dv.x| > 1e-4. MUST stay 0: OCEAN codes ARE 0,
    //           and the PS falls back to p0.x exactly when dv.x == 0 — the
    //           old packing's bodyCodeScale=1.0 here misread every OCEAN
    //           vertex as SHORE (code 1).
    //   c14.y = 2e-4 — edgeBlend fallback, a hair ABOVE the PS's 1e-4
    //           "0 = unset" threshold. CRITICAL: WaterBodyContract forces
    //           w=1.0 (FULL body deviations) when both dv.y and p0.y read
    //           as unset — so a blend that ramped to 0 at a type-change
    //           edge would flip to full river/shore deviations exactly at
    //           the boundary. This sentinel makes dv.y=0 read as "ramped to
    //           default" (edge=2e-4 -> w~=0) while the OFF state still
    //           uploads true zeros (mag gate -> vanilla identity).
    //   c14.z/w = 0 (unset -> 1.0) — optional GLOBAL foam/shore fine-tunes;
    //           left unset, the per-body deviation table carries personality.
    //   c15    = (0,0,0,0) — norm/opacity/glitter fine-tunes all unset.
    //
    // OFF STATE: classification did not run -> VS c10.x = 0 zeroes
    // TEXCOORD5 AND c14/c15 upload all-zero -> WaterBodyContract's mag gate
    // (sum of |dv.x|,|dv.y|,|p0.*|,|p1.*| <= 1e-4) -> inactive => every
    // scale pinned to exactly 1.0, the pre-contract algebra bit for bit.
    // Uploaded unconditionally every draw so a style/PS switch (all three
    // style CSOs are entries of Water_Parallax.hlsl) never inherits stale
    // values.
    float bodyContract[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float bodyContractOn[4] = {0.0f, 2e-4f, 0.0f, 0.0f};
    RwD3D9SetPixelShaderConstant(14, bodyOn ? bodyContractOn : bodyContract, 1);
    RwD3D9SetPixelShaderConstant(15, bodyContract, 1);

    // ---- M2/M3/reflection-mix: water FX params c16 ----
    //   c16 = (foamTexValid, foamTexWeight, softIsecGain, reflMix)
    // foamTexValid: g_foamTex exists AND the RT tier is live (the PS fetch is
    //   uniform-tier-gated at tier >= 2; 0 = the M2 texture term is exactly 0
    //   -> pre-M2 foam bit for bit).
    // foamTexWeight: M2 RAGE foam-texture blend weight (0.6 = moderate: the
    //   procedural octaves keep the multi-scale edge dissolve, the texture
    //   adds the cellular/white-noise pattern on top).
    // softIsecGain: M3 Cry softIsec shallow-intersection feather gain,
    //   gated on depthValid — the term reads the s2 depth copy through the
    //   water column thickness; without the copy the thickness estimate is
    //   distance-driven and the feather would key off view distance instead
    //   of real geometry intersections.
    // reflMix: config->waterReflectionMix (INI waterReflectionMix, default
    //   0.7 — see the header comment). Null-config guard -> 0.7 (the shipped
    //   default) so a pre-init draw can never upload garbage. Uploaded EVERY
    //   draw unconditionally, like c14/c15, so a style/PS switch never
    //   inherits stale values.
    float reflMixUser = config ? config->waterReflectionMix : 0.7f;
    float waterFx[4] = {(needRT && g_foamTex) ? 1.0f : 0.0f,
                        g_foamTex ? 0.6f : 0.0f,
                        depthValid ? 0.55f : 0.0f,
                        waterClampF(reflMixUser, 0.0f, 1.0f, 0.7f)};
    RwD3D9SetPixelShaderConstant(16, waterFx, 1);

    // ---- Z state: pin the draw-order contract (all three layers) ----
    // Runs BEFORE the draw setup below so nothing in here can move Z after
    // it is pinned. The water surface is a single translucent draw (tier
    // 0/1: the SRCALPHA blend set below; tier >= 2: translucency composited
    // in-shader — no separate opaque core pass exists in this pipeline), so
    // the classic translucent contract applies:
    //   ZENABLE = TRUE, ZFUNC = LESSEQUAL, ZWRITEENABLE = FALSE
    // Z-test ON against scene geometry is what puts the surface BEHIND
    // buildings/terrain again (the "water always in front" bug); Z-write
    // OFF keeps overlapping water quads in one buffer (ocean + shore +
    // pools) from z-rejecting each other. Doctrine order
    // (pipeEnterAlphaMode): L1/L2 canonical writes, ONE
    // _rwD3D9RenderStateFlushCache() to commit pending[] while it holds
    // those values, then unconditional L3 raw writes — the draw is raw and
    // reads the DEVICE, where a foreign raw write (the depth-copy helper's
    // restore miss) can sit invisible to a cache-hit L1/L2 write. The rw
    // cache is written too so it can never flush a stale Z value over the
    // raw pin.
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,  (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwD3D9SetRenderState(D3DRS_ZENABLE,     D3DZB_TRUE);
    RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    RwD3D9SetRenderState(D3DRS_ZFUNC,        D3DCMP_LESSEQUAL);
    _rwD3D9RenderStateFlushCache();
    g_d3dDevice->SetRenderState(D3DRS_ZENABLE,     D3DZB_TRUE);
    g_d3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_d3dDevice->SetRenderState(D3DRS_ZFUNC,        D3DCMP_LESSEQUAL);

    // ---- Textures + sampler states ----
    // s0 = scene colour copy (refraction), s1 = mip-mapped detail normals,
    // s2 = raw scene depth copy, s3 = G1 vertically-flipped scene copy
    // (screen-space reflection, IV/V cores only). Raw SetTexture bypasses RW's
    // state cache — clear the cached stage-0 raster first (RenderLumaPass
    // idiom). Tier 0/1 never fetch s0/s2 (no copies exist), so leave them
    // unbound.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
    g_d3dDevice->SetTexture(0, needRT ? g_sceneTexture : nullptr);
    g_d3dDevice->SetTexture(1, g_detailTex);
    g_d3dDevice->SetTexture(2, (needRT && depthValid) ? g_depthTexture : nullptr);
    // s3 = G1 reflection copy (flipped scene). NULL whenever the gate is off
    // (tier < 2 / style Xbox / RT create failed) — the PS flag c7.w is 0 in
    // the same cases, so the fetch result is never mixed in.
    g_d3dDevice->SetTexture(3, reflValid ? g_reflTexture : nullptr);
    // s4 = M2 foam texture (procedural, MANAGED). Bound whenever it exists —
    // the PS gate c16.x is 0 when it doesn't, so the fetch result is never
    // mixed in (SM3.0 gradient rules force the fetch; the gate zeroes the
    // contribution). Tier 0/1 never reach the foam block, the bind is inert.
    g_d3dDevice->SetTexture(4, g_foamTex);

    g_d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    g_d3dDevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    // Mip-correct LOD: trilinear on the detail texture (req 3).
    g_d3dDevice->SetSamplerState(1, D3DSAMP_MIPFILTER,
        g_detailTexHasMips ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    g_d3dDevice->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // s3 (G1 reflection copy): linear + clamp like the scene copy — the
    // projective fetch clamps in-shader too, this just keeps edge texels
    // from smearing. Restored in waterPipe_restoreRenderState (stage 3 is
    // now a water-pipe-touched stage).
    g_d3dDevice->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_d3dDevice->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // s4 (M2 foam texture): linear + WRAP (tiling fetch, scrolling UVs) +
    // trilinear when the mip chain exists. Restored in
    // waterPipe_restoreRenderState (stage 4 joined the touched set).
    g_d3dDevice->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(4, D3DSAMP_MIPFILTER,
        g_foamTexHasMips ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    g_d3dDevice->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    // ---- W2: VS vertex-texture binds (device stages 256/257) ----
    // VS sampler 0 = the R32F depth copy (per-vertex shore gate — the SAME
    // copy just captured for the PS, so the gate and the PS shore algebra
    // read one consistent depth field), VS sampler 1 = the SkyNoise
    // universal tile (spatial gust field). Bound only when the one-time
    // probe passed; the shader's uniform gates (c12.z/c12.w, c12.y) skip
    // the texldl whenever the corresponding bind is absent, so an unbound
    // stage can never contribute. Unbound + sampler states reset in
    // waterPipe_restoreRenderState (bracket doctrine).
    if(s_vtfProbe == 1){
        g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0,
            vtfShoreOK ? g_depthTexture : nullptr);
        g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER1, g_waterGustTex);
        // Depth gate: LINEAR (bilinear depth smooths the per-vertex gate
        // across quads) + CLAMP (screen UV is saturate'd in-shader anyway).
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        // Gust tile: LINEAR + WRAP (tileable fetch, downwind-drifting UVs),
        // matching the SkyNoise BindUniversal sampler contract.
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }

    // Tier >= 2: opaque replace — translucency is composited in-shader (lerp
    // with the refracted scene), so no alpha blending is wanted at the draw.
    // Tier 0/1: no scene copy exists — the shader returns an opacity alpha
    // and the classic SRCALPHA/INVSRCALPHA blend (SA/re3 water convention)
    // composites it live against the framebuffer for free.
    // test-7 creator fix: BOTH branches now pin through ALL THREE layers
    // (L1 -> L2 -> flush -> L3 raw), the same doctrine as the Z pin above —
    // the draw is raw and reads the DEVICE, where a cache-hit L1/L2 write
    // (stale equal value) can silently skip and leave the previous
    // consumer's blend state live for the draw AND for the restore below.
    if(tier >= 2){
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)false);
        RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        _rwD3D9RenderStateFlushCache();
        g_d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }else{
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)true);
        RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        _rwD3D9RenderStateFlushCache();
        g_d3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_d3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        g_d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    }
    // Double-sided single pass: the sheet must render from BELOW as well
    // (underwater view of the surface). A backface-culled draw split the
    // plane into front/back halves and showed as seams along the surface
    // edges from under the water. The value is saved pre-Get (a failed Get
    // leaves the out-param untouched -> the CULLBACK default below is the
    // fallback) and restored in waterPipe_restoreRenderState.
    // WATER AUDIT #2: the restore is VERBATIM — hand back the entry cull
    // mode exactly as read. The old sanitization remapped any entry mode
    // that was not NONE/BACK (i.e. CULLFRONT, a legal RW cull mode) to
    // CULLBACK on restore, silently corrupting the frame's own state —
    // exactly the class of drift the bracket doctrine ("restore every state
    // you modify") forbids. Over-cautious; removed.
    s_savedCull = rwCULLMODECULLBACK;
    RwRenderStateGet(rwRENDERSTATECULLMODE, (void*)&s_savedCull);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)false);

    g_waterParallaxActive = true;

    // ---- W7-D instrumentation: per-2s chain summary (log only) ----
    // One line every 2 s (dbglog_throttle("waterchain")) describing exactly
    // what the last flush ran with. Nothing here feeds any render path.
    // Log-hygiene (2026-11-02): recurring receipt -> g_diagLevel>=1 only.
    if(g_diagLevel >= 1 && dbglog_throttle("waterchain")){
        const char *styleN = (config->waterStyle == 1) ? "IV" :
                             (config->waterStyle == 2) ? "V"  : "Xbox";
        // c14 uploads bodyContractOn when classification ran, the all-zero
        // array otherwise; c15 always uploads the all-zero array.
        const float *c14v = bodyOn ? bodyContractOn : bodyContract;
        dbglog("[water] chain: style=%s waterStyle=%d tier=%d needRT=%d "
               "sceneRT=%s depthRT=%s reflRT=%s reflFlag=%d fresnelSplit=%d body=%d "
               "VS c10.gate=%.4f PS c14=(%.6f,%.6f,%.6f,%.6f) c15=(0,0,0,0) "
               "c16=(%.2f,%.2f,%.2f,%.2f) "
               "flushVerts=%d ocean=%d shallow=%d interior=%d river=%d "
               "pond=%d lake=%d nearClip=%.3f farClip=%.3f "
               "PS=%p(%s) VS=%p(IDR_WATER_VS) "
               "cullPre=0x%02X cullSet=NONE restore=paired(waterPipe_restoreRenderState)",
               styleN, (int)config->waterStyle, tier, needRT ? 1 : 0,
               g_sceneTexture ? "ok" : "none", g_depthTexture ? "ok" : "none",
               g_reflTexture ? "ok" : "none", reflValid ? 1 : 0,
               g_waterFresnelSplit ? 1 : 0, bodyOn ? "on" : "off",
                bodyP[0],
                c14v[0], c14v[1], c14v[2], c14v[3],
                waterFx[0], waterFx[1], waterFx[2], waterFx[3],
                s_flushHistVerts, s_flushHist[0], s_flushHist[1],
               s_flushHist[2], s_flushHist[3], s_flushHist[4],
               s_flushHist[5],
               nearClip, farClip,
               (void*)ps, waterPipe_psTag(ps), (void*)Water_VS,
               (unsigned)s_savedCull);
    }

    // W7-D: close the flush/draw scope here (idempotent Close(), see above)
    // so it ends exactly at the handoff instead of at function exit.
    pf_scope_flushDraw.Close();
}

void waterPipe_restoreRenderState(void)
{
    if(!g_waterParallaxActive) return;
    if(!g_d3dDevice) return;
    RwD3D9SetPixelShader(nullptr);
    RwD3D9SetVertexShader(nullptr);
    // ---- FIX 2a/3 (fonts regression, 2026-11-01): stage 0 hands the frame
    // back its OWN ENTRY raster, never a white park. History: the original
    // triple-null (raw s0=NULL + L1 raster=NULL) left FFP consumers that do
    // not rebind stage 0 sampling device-default garbage (corona_blend
    // receipts). FIX 2a replaced that with a white park — a defined texel, but
    // still a raster the frame never owned: the HUD/font Im2D path binds
    // through the RW cache, so a consumer whose rebind value-compares to a
    // no-op now samples white -> the "fonts are completely broken" report on
    // this build. FIX 3: capture the ENTRY raster at the top of
    // waterPipe_setRenderState (before the depth/reflection copies and the
    // water bind can move it) and restore it VERBATIM on every layer this
    // path touched:
    //   L1  RwRenderStateSet(rwRENDERSTATETEXTURERASTER) PARK-AND-SET to the
    //       entry raster — the L1 write forwards to the L2 driver cache on a
    //       real transition (state-leak audit §1: L1 value-compare, forwards
    //       to L2), so the park leg defeats a stale-cache no-op. There is no
    //       raster -> RwTexture reverse map, so the explicit RwD3D9SetTexture
    //       leg cannot name an arbitrary entry texture; it is not needed here
    //       because the water path never writes L2 for stage 0 (its bind block
    //       is L1-NULL + raw L3 only), leaving L2 still naming the entry.
    //   L3  raw device pin of the entry raster's resolved IDirect3DTexture9,
    //       done in the raw block below after the flush (three-layer order).
    // Stages 1-4 stay raw-NULL (pipe-owned scratch: scene copy / detail
    // normals / depth+reflection / M2 foam — every consumer binds its own).
    g_d3dDevice->SetTexture(1, nullptr);
    g_d3dDevice->SetTexture(2, nullptr);
    g_d3dDevice->SetTexture(3, nullptr);   // G1 reflection copy unbind
    g_d3dDevice->SetTexture(4, nullptr);   // M2 foam texture unbind
    // W2: VS vertex-texture unbind (stages 256/257) + sampler-state
    // defaults — the water pipe is the only vertex-sampler user in the
    // process, but the bracket doctrine still hands the stages back clean.
    // Guarded on the probe so a driver that refused the stage is never
    // poked again.
    if(s_vtfProbe == 1){
        g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);
        g_d3dDevice->SetTexture(D3DVERTEXTEXTURESAMPLER1, nullptr);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    {
        RwRaster *entryRas = s_savedS0Raster;   // ENTRY capture (setRenderState)
        RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
            ? gpWhiteTexture->raster : NULL;
        // Park on a guaranteed-different raster so the real set is always an
        // L1 (and therefore L2) transition, never a value-compare no-op.
        RwRaster *parkRas = (entryRas == whiteRas) ? NULL : whiteRas;
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)entryRas);
    }
    // Sampler states back to the RW-ish linear/wrap default for ALL stages the
    // water pipe touched (0-4). Stage 0 was set to CLAMP + MIPFILTER NONE for
    // the scene copy; leaving it leaked into the next stage-0 consumer bound
    // through the RW cache (tiling building/floor textures then edge-smear
    // instead of wrapping — a three-layer doctrine violation: restore every
    // state you modify). Stage 3 joined the touched set with G1 (reflection
    // copy), stage 4 with the M2 foam texture. RW re-applies a texture's own
    // filter/addressing bits on the next bind, so this default is safe for
    // every consumer.
    for(int s = 0; s <= 4; s++){
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    // ---- Blend bracket (test-7 creator fix): hand back the ENTRY blend ----
    // block VERBATIM on all layers — never a hardcoded convention. The old
    // restore forced VERTEXALPHAENABLE=false through L1 only after every
    // water draw: inside a blend-on window that left ALPHABLENDENABLE=0 on
    // the device (black light quads), and a stale L1 value-compare no-op
    // left the L2=1/L3=0 divergence the statesync kept healing. L1 -> L2
    // here; the L3 raw writes land after the flush below, beside the fog/Z
    // raw pins.
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, s_savedBlendEnableRw);
    RwRenderStateSet(rwRENDERSTATESRCBLEND,  s_savedSrcBlendRw);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, s_savedDstBlendRw);
    RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s_savedBlendEnableDrv);
    RwD3D9SetRenderState(D3DRS_SRCBLEND,  s_savedSrcBlendDrv);
    RwD3D9SetRenderState(D3DRS_DESTBLEND, s_savedDstBlendDrv);
    // Back to the frame's own cull mode (double-sided was water-only).
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)s_savedCull);
    // ---- Fog bracket (FIX 2b completion, 2026-10-31 grey-veil family) ----
    // Hand back the ENTRY fog block VERBATIM on all layers — never a forced
    // enable. The old restore forced FOGENABLE=TRUE while the fog range/mode
    // device states held whatever the last fog-writer left; a foreign range
    // fogged every subsequent consumer (sky included) to the fog colour =
    // the whole-scene desaturated veil with the flat fog-coloured sky in the
    // 03:01 beach shots. The water pipe's own fog-OFF draw window closes
    // here with exactly the state the frame had before it.
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, s_savedFogEnableRw);
    RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)s_savedFogColorDev);
    RwD3D9SetRenderState(D3DRS_FOGENABLE,     s_savedFogEnableDrv);
    RwD3D9SetRenderState(D3DRS_FOGCOLOR,      s_savedFogColorDev);
    RwD3D9SetRenderState(D3DRS_FOGTABLEMODE,  s_savedFogTableModeDrv);
    RwD3D9SetRenderState(D3DRS_FOGVERTEXMODE, s_savedFogVertexModeDrv);
    RwD3D9SetRenderState(D3DRS_FOGSTART,      s_savedFogStartDrv);
    RwD3D9SetRenderState(D3DRS_FOGEND,        s_savedFogEndDrv);
    RwD3D9SetRenderState(D3DRS_FOGDENSITY,    s_savedFogDensityDrv);
    // One-shot bracket receipt: names the entry fog block so a broken range
    // (the veil source if another lane set it) is attributable next run.
    static bool s_fogBlockLogged = false;
    if(!s_fogBlockLogged){
        s_fogBlockLogged = true;
        dbglog("[water] fog bracket: entry enable=%d tableMode=%d vertexMode=%d "
               "start=%.1f end=%.1f density=%.3f color=0x%08X",
               (int)s_savedFogEnableDrv, (int)s_savedFogTableModeDrv,
               (int)s_savedFogVertexModeDrv,
               *(const float *)&s_savedFogStartDrv,
               *(const float *)&s_savedFogEndDrv,
               *(const float *)&s_savedFogDensityDrv,
               s_savedFogColorDev);
    }
    // ---- Z state back to the ENTRY snapshot (three layers) ----
    // Mirrors the set block's L1 -> L2 -> flush -> L3 order and hands back
    // exactly what waterPipe_setRenderState snapshotted at entry — never a
    // hardcoded value — so the frame's own Z setup (or the state a helper
    // like PostFX_CopyDepthToTexture left for the next consumer) survives
    // the water draw untouched.
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,  s_savedZTestRw);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, s_savedZWriteRw);
    RwD3D9SetRenderState(D3DRS_ZENABLE,     s_savedZEnableDrv);
    RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s_savedZWriteDrv);
    RwD3D9SetRenderState(D3DRS_ZFUNC,        s_savedZFuncDrv);
    _rwD3D9RenderStateFlushCache();
    // L3 raw: stage-0 ENTRY texture verbatim (fonts fix). Resolved from the
    // entry raster through the proven 0xB4E9E0 extension slot — the raw
    // device is the only layer a cache-hit skip can never hold back, and it
    // is the layer the font/HUD Im2D draws actually sample. A NULL entry
    // raster must still CLEAR the device bind (an empty L1 raster left
    // otherwise leaves the water texture on stage 0); an extraction failure
    // on a non-empty raster must NOT clear a valid bind (L1/L2 already
    // restored) — same asymmetry as chars.cpp RestoreSSSStage0AndSampler.
    if(s_savedS0Raster){
        IDirect3DTexture9 *entryTex = waterPipe_RasterToD3D(s_savedS0Raster);
        if(entryTex)
            g_d3dDevice->SetTexture(0, entryTex);
    }else{
        g_d3dDevice->SetTexture(0, nullptr);
    }
    // L3 raw: the entry fog block verbatim (bracket completion), then Z.
    g_d3dDevice->SetRenderState(D3DRS_FOGENABLE,     s_savedFogEnableDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGCOLOR,      s_savedFogColorDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGTABLEMODE,  s_savedFogTableModeDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGVERTEXMODE, s_savedFogVertexModeDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGSTART,      s_savedFogStartDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGEND,        s_savedFogEndDev);
    g_d3dDevice->SetRenderState(D3DRS_FOGDENSITY,    s_savedFogDensityDev);
    g_d3dDevice->SetRenderState(D3DRS_ZENABLE,     s_savedZEnableDev);
    g_d3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, s_savedZWriteDev);
    g_d3dDevice->SetRenderState(D3DRS_ZFUNC,        s_savedZFuncDev);
    // L3 raw: entry blend block verbatim (bracket completion — the device is
    // the layer a cache-hit skip can never hold back; see the Z-pin note).
    g_d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, s_savedBlendEnableDev);
    g_d3dDevice->SetRenderState(D3DRS_SRCBLEND,  s_savedSrcBlendDev);
    g_d3dDevice->SetRenderState(D3DRS_DESTBLEND, s_savedDstBlendDev);

    // ---- Leak receipt (fonts regression family, 2026-11-01): compare the
    // state this restore just PUBLISHED against the ENTRY capture and name a
    // mismatch on the spot. Covers the two leak shapes the bracket exists for:
    //   water_s0leak    — device stage 0 is not the entry raster's texture
    //                     (foreign/NULL/white-after-restore => the next
    //                     HUD/font Im2D consumer samples nothing/wrong).
    //   water_blendleak — device blend enable/src/dst differ from the entry
    //                     block (the test-7 black-light-quads shape).
    // Throttled: a persistent leak names itself every 2 s instead of every
    // flush. A provable stage-0 compare needs the entry raster to be empty
    // (expect NULL) or resolvable through 0xB4E9E0; an unresolvable non-empty
    // raster is deliberately left unprovable (the restore never clears it).
    {
        IDirect3DBaseTexture9 *devS0 = NULL;
        if(SUCCEEDED(g_d3dDevice->GetTexture(0, &devS0))){
            IDirect3DTexture9 *wantS0 = waterPipe_RasterToD3D(s_savedS0Raster);
            bool provable = (s_savedS0Raster == NULL) || (wantS0 != NULL);
            if(provable &&
               (IDirect3DBaseTexture9*)wantS0 != devS0 &&
               dbglog_throttle("water_s0leak")){
                dbglog("[water] s0 leak after restore: entryRaster=%p entryTex=%p "
                       "devStage0=%p (fonts/HUD will sample the device bind)",
                       (void*)s_savedS0Raster, (void*)wantS0, (void*)devS0);
            }
            if(devS0) devS0->Release();
        }
    }
    {
        DWORD devBlend = 0xFFFFFFFF, devSrc = 0xFFFFFFFF, devDst = 0xFFFFFFFF;
        g_d3dDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &devBlend);
        g_d3dDevice->GetRenderState(D3DRS_SRCBLEND,  &devSrc);
        g_d3dDevice->GetRenderState(D3DRS_DESTBLEND, &devDst);
        if((devBlend != s_savedBlendEnableDev ||
            devSrc   != s_savedSrcBlendDev ||
            devDst   != s_savedDstBlendDev) &&
           dbglog_throttle("water_blendleak")){
            dbglog("[water] blend leak after restore: dev enable=%u src=%u dst=%u "
                   "entry enable=%u src=%u dst=%u",
                   (unsigned)devBlend, (unsigned)devSrc, (unsigned)devDst,
                   (unsigned)s_savedBlendEnableDev,
                   (unsigned)s_savedSrcBlendDev,
                   (unsigned)s_savedDstBlendDev);
        }
    }
    g_waterParallaxActive = false;

    // ---- W7-D instrumentation: render-state restore confirmation ----
    // Pairs with the "restore=paired(...)" note of the setRenderState chain
    // line, so the log shows the save (waterPipe_setRenderState) AND this
    // restore actually ran for the flush. Log only. Log-hygiene
    // (2026-11-02): recurring receipt -> g_diagLevel>=1 only.
    if(g_diagLevel >= 1 && dbglog_throttle("waterchain_restore"))
        dbglog("[water] restore: VS/PS unbound, tex stages 1-4 cleared, "
               "stage 0 -> entry raster verbatim (FIX 3, entryRaster=%p "
               "entryTex=%p), samplers 0-4 -> linear/wrap, "
               "blend -> entry snapshot (3 layers, bracket, enable=%d "
               "src=%u dst=%u), "
               "cull NONE(0x%02X) -> restored 0x%02X, fog -> entry snapshot "
               "(bracket, fogcol=0x%08X), zstate -> entry snapshot (3 layers)",
               (void*)s_savedS0Raster,
               (void*)waterPipe_RasterToD3D(s_savedS0Raster),
               (int)s_savedBlendEnableDrv,
               (unsigned)s_savedSrcBlendDrv, (unsigned)s_savedDstBlendDrv,
               (unsigned)rwCULLMODECULLNONE, (unsigned)s_savedCull,
               s_savedFogColorDev);
    // FIX 2a/2b confirmation receipt (throttled — proves the new restore
    // policy is live every window, not just once). Log-hygiene (2026-11-02):
    // recurring receipt -> g_diagLevel>=1 only.
    if(g_diagLevel >= 1 && dbglog_throttle("water_fogfix"))
        dbglog("[water] fogfix: stage0 -> entry raster verbatim (FIX 3), fog "
               "block -> entry snapshot (enable=%d color=0x%08X)",
               (int)s_savedFogEnableDrv, s_savedFogColorDev);
}

// ============================================================
// waterPipe_BindVanillaRaster3L — FFP-fallback stage-0 hardening (test-4).
// Force-binds the vanilla water texture (texWaterclear256 @0xC228AC /
// waterclear256Raster @0xC228A8) through ALL THREE layers so an FFP water
// draw can never sample whatever stale raster the previous consumer left on
// stage 0 — test-4's flatpath receipts showed s0=1035AF80/10355B10 (unknown,
// suspected screen/reflection-family) and the water UVs sampling such a
// raster render as a vertically-reflected-world band (the "mirrored world"
// symptom). Mirror of vehBindDiffuse3L (vehiclePipe.cpp:801-821):
//   L1  RwRenderStateSet(rwRENDERSTATETEXTURERASTER)  PARK-AND-SET (park on
//       the white raster — guaranteed different — so the real set is always
//       a transition, never a value-compare no-op);
//   L2  RwD3D9SetTexture(texWaterclear256, 0)          RW driver/applied[];
//   L3  d3d9device->SetTexture(0, TexDefsGetD3DTexture(...))  raw device —
//       the only layer a cache-hit skip can never hold back; pinned only
//       when the guarded extraction yields a real pointer (never clear a
//       valid bind on failure — L2 already bound correctly).
// Called from waterPipe_directFlushRoute's failure exits (the FFP fallback
// follows immediately) and safe to call from main.cpp's fallback block
// (function-local extern idiom) — a rebind is state-identical.
// Pre-init safe: both globals are NULL before WaterLevelInitialise and the
// function no-ops without writing anything.
// ============================================================
void waterPipe_BindVanillaRaster3L(void)
{
    RwTexture *tex = *(RwTexture**)0xC228AC;    // texWaterclear256
    if(!tex || !tex->raster){
        // Texture wrapper not ready — fall back to the raw raster global
        // through the L1 RW path (park-and-set; RW's setter maps it to the
        // device). No-op when neither global exists yet.
        RwRaster *ras = *(RwRaster**)0xC228A8;  // waterclear256Raster
        if(ras){
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)ras);
        }
        return;
    }

    // L1 rw cache — park-and-set (white raster is guaranteed != waterclear).
    RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
        ? gpWhiteTexture->raster : NULL;
    RwRaster *targetRas = tex->raster;
    RwRaster *parkRas = (targetRas == whiteRas) ? NULL : whiteRas;
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)targetRas);

    // L2 RW driver cache / texture apply (maps raster -> d3d internally).
    RwD3D9SetTexture(tex, 0);

    // L3 raw device: bypass every cache; guarded extraction.
    if(d3d9device){
        IDirect3DTexture9 *d3d = TexDefsGetD3DTexture(tex);
        if(d3d)
            d3d9device->SetTexture(0, d3d);
    }
}

// ============================================================
// W1 seam fix: pipe-route the two INLINE Im3D flushes inside
// CWaterLevel::RenderWater (exe 1.0 US draw calls 0x6EF8AC outside-world
// ocean sheet + 0x6F0099 quad-loop remainder; main.cpp's
// WaterDirectFlush_Draw calls this BEFORE its FFP fallback).
//
// The FFP fallback renders those flushes flat/pale — waterclear256 + vertex
// colour, no parallax PS, no foam band, no caustics — directly adjacent to
// pipe water: the hard vertical seam + the missing shore foam (2026-10-30
// 04:38 run). This function gives them the EXACT pipe treatment the hooked
// RenderIfDoesntFit flushes get: full waterPipe_setRenderState bind (RT
// copies, WVP/time constants, c10 body gate, c14/c15 body/foam contract,
// textures s0-s3, Z pin, WbClassifyFlush) + the raw pinned-stage draw +
// waterPipe_restoreRenderState. The shore foam band lives in the parallax
// PS (WaterShoreCoverage/WaterFoamOctaves, tier >= 2, depth-copy driven), so
// routing is what brings the water<->coast foam back — no separate shore
// shader exists (resource.h has no shore/foam ID; git history has no lost
// shore-wave CSO — the band was always PS-side).
//
// Returns TRUE when the pipe drew the flush (caller skips its FFP path and
// the original Im3D draw — the buffer is consumed). FALSE = the pipe cannot
// run (disabled / shaders missing / insane counts / device lost / RT
// unavailable); the caller then keeps the FFP fail-open path. Every bail
// here happens BEFORE any device shader/state is touched, and
// WbClassifyFlush only runs after ALL early-outs (point of no return), so a
// false return never leaves classified normals behind for the FFP draw.
// ============================================================
bool waterPipe_directFlushRoute(void)
{
    // ---- GATE SEMANTICS (test-4 misattribution fix, 2026-10-31) ----
    // The water CORE (Water_VS + style parallax PS + scene/depth RTs) is
    // gated by exactly TWO config values, and pipeChainEnable is NOT one of
    // them — grep-verified zero references in this TU; the water CSOs load
    // unconditionally in CreateShaders (pipelinecommon.cpp:795-798):
    //   waterParallaxEnable  master core gate (user intent). 0 = vanilla
    //                        water only: every path here returns false and
    //                        ALL water draws go FFP (test-4: skygfx.ini:250
    //                        waterParallaxEnable=0 was misread as a
    //                        pipeChainEnable coupling — it never was one).
    //   waterStyle           style select (0 Xbox / 1 IV / 2 V). There is NO
    //                        "none" value — the master gate above is the
    //                        only full disable.
    // pipeChainEnable gates the POSTFX pipe chain only (postfx.cpp pf_gate /
    // PipeChainShader) — the "optional chain extras" are already decoupled
    // from water. This route therefore needs NO decoupling change: the
    // one-shot receipt below names the live gate values so an all-FFP-water
    // run is attributable on sight.
    void *ps = waterPipe_pixelShader();
    if(!config->waterParallaxEnable || !ps || !Water_VS){
        // One-shot: the FFP fallback that follows inherits the CURRENT device
        // stage-0 texture — capture its identity (dims/format/pool) so the
        // stale rasters the flatpath receipts print (test-4: 1035AF80 /
        // 10355B10, suspected screen/reflection-family = the mirrored-band
        // source) can be named next run. GetTexture AddRefs — released here.
        static bool s_gateLogged = false;
        if(!s_gateLogged){
            s_gateLogged = true;
            void *s0Ptr = NULL;
            unsigned sw = 0, sh = 0, sfmt = 0, spool = 0xFFFFFFFF;
            if(d3d9device){
                IDirect3DBaseTexture9 *s0 = NULL;
                if(SUCCEEDED(d3d9device->GetTexture(0, &s0)) && s0){
                    s0Ptr = (void*)s0;
                    IDirect3DTexture9 *t2d = NULL;
                    if(SUCCEEDED(s0->QueryInterface(s_IID_IDirect3DTexture9,
                                                    (void**)&t2d)) && t2d){
                        D3DSURFACE_DESC d = {};
                        if(SUCCEEDED(t2d->GetLevelDesc(0, &d))){
                            sw = d.Width; sh = d.Height;
                            sfmt = d.Format; spool = d.Pool;
                        }
                        t2d->Release();
                    }
                    s0->Release();
                }
            }
            dbglog("[water] core disabled by config: waterParallaxEnable=%d "
                   "waterStyle=%d pipeChainEnable=%d (postfx-only, no water "
                   "coupling) ps=%p Water_VS=%p s0=%p (%ux%u fmt=%u pool=%u) "
                   "-> all water draws FFP",
                   config ? config->waterParallaxEnable : -1,
                   config ? config->waterStyle : -2,
                   config ? config->pipeChainEnable : -2,
                   ps, Water_VS, s0Ptr, sw, sh, sfmt, spool);
        }
        // Recurring identity sample (2s throttle): the one-shot above names
        // only the FIRST flush's s0 — test-4 showed TWO distinct stale
        // rasters (site 1 1035AF80, site 2 10355B10), so keep sampling the
        // pre-bind identity. MUST run before the 3-layer bind below.
        // Log-hygiene (2026-11-02): recurring receipt -> g_diagLevel>=1 only
        // (LogStage0Identity also early-returns internally, saving the
        // GetTexture/QI device reads).
        if(g_diagLevel >= 1 && dbglog_throttle("water_flatS0"))
            waterPipe_LogStage0Identity("flatpath S0");
        // FFP fallback hardening: pin the vanilla water raster through ALL
        // THREE layers so the draw that follows samples waterclear256 instead
        // of the stale screen/reflection-family raster (the mirrored-world
        // band source). See waterPipe_BindVanillaRaster3L below.
        waterPipe_BindVanillaRaster3L();
        return false;
    }
    if(!WbBufferCountsSane(TempBufferVerticesStored, TempBufferIndicesStored)){
        if(dbglog_throttle("wroute_badbuf"))
            dbglog("[water] route decline: insane counts v=%u i=%u -> FFP fallback",
                   (unsigned)TempBufferVerticesStored,
                   (unsigned)TempBufferIndicesStored);
        waterPipe_BindVanillaRaster3L();
        return false;
    }
    SyncDevice();
    if(!g_d3dDevice){
        if(dbglog_throttle("wroute_nodevice"))
            dbglog("[water] route decline: no device -> FFP fallback");
        waterPipe_BindVanillaRaster3L();
        return false;
    }
    if(g_d3dDevice->TestCooperativeLevel() != D3D_OK){
        if(dbglog_throttle("wroute_lost"))
            dbglog("[water] route decline: device not cooperative -> FFP fallback");
        waterPipe_BindVanillaRaster3L();
        return false;
    }

    // Park the pre-route input stage (Get* AddRef — released below). The
    // main.cpp caller holds its own diagnostic capture of the SAME state;
    // this park is what the raw draw hands back afterwards.
    IDirect3DVertexShader9 *prevVS = NULL;
    IDirect3DPixelShader9 *prevPS = NULL;
    IDirect3DVertexDeclaration9 *prevDecl = NULL;
    DWORD prevFVF = 0;
    g_d3dDevice->GetVertexShader(&prevVS);
    g_d3dDevice->GetPixelShader(&prevPS);
    g_d3dDevice->GetVertexDeclaration(&prevDecl);
    g_d3dDevice->GetFVF(&prevFVF);

    // Commit any pending rw render-state writes BEFORE the pipe bind — the
    // hooked flush path does exactly this (main.cpp, pre-setRenderState) so
    // a foreign pending value cannot land over the pipe's raw writes later.
    _rwD3D9RenderStateFlushCache();

    // Full pipe bind — identical to the hooked flush path.
    waterPipe_setRenderState();

    bool drew = false;
    if(g_waterParallaxActive){
        // ---- pin the raw draw stage (main.cpp flush-block idiom) ----
        // NULL transition through BOTH layers first (the RW setter is
        // value-compare gated against 0x8E2448/0x8E244C), then raw-bind the
        // water shaders and seed the same caches; decl NULL + FVF 0x152
        // (XYZ|NORMAL|DIFFUSE|TEX1, the 36-byte RwIm3DVertex layout) so the
        // foreign PBR declaration of the last atomic cannot win.
        g_d3dDevice->SetVertexShader(NULL);
        g_d3dDevice->SetPixelShader(NULL);
        *(void**)0x8E2448 = NULL;   // _rwD3D9LastVertexShaderUsed
        *(void**)0x8E244C = NULL;   // _rwD3D9LastPixelShaderUsed
        g_d3dDevice->SetVertexShader((IDirect3DVertexShader9*)Water_VS);
        g_d3dDevice->SetPixelShader((IDirect3DPixelShader9*)ps);
        *(void**)0x8E2448 = Water_VS;
        *(void**)0x8E244C = ps;
        g_d3dDevice->SetVertexDeclaration(NULL);
        g_d3dDevice->SetFVF(0x152);
        *(void**)0x8E2444 = NULL;           // _rwD3D9LastVertexDeclarationUsed
        *(void**)0x8E2440 = (void*)0x152;   // _rwD3D9LastFVFUsed

        // Same raw emit as main.cpp's hooked flush: D3D9 primitive enum
        // (D3DPT_TRIANGLELIST), primitive count = indices/3.
        RwD3D9DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
            TempBufferVerticesStored, TempBufferIndicesStored / 3,
            TempBufferRenderIndexList, TempVertexBuffer, sizeof(RwIm3DVertex));
        // Draw-AND-empty (the hooked flush clears too, main.cpp): the vanilla
        // site blocks also clear inline right after the draw (site 1 at
        // 0x6EFB86, site 2 before RenderBoatWakes), so this is a harmless
        // double-clear — but it guarantees the wake path that follows site 2
        // never appends onto a non-emptied water buffer.
        TempBufferVerticesStored = 0;
        TempBufferIndicesStored = 0;
        drew = true;
    }

    // Paired restore — no-ops when the pipe never activated (guarded by
    // g_waterParallaxActive inside).
    waterPipe_restoreRenderState();

    // ---- unpark: pre-route stage back on BOTH layers (decl/FVF + VS/PS,
    // mutually exclusive per prevDecl — main.cpp flush-block idiom) ----
    g_d3dDevice->SetVertexShader(prevVS);
    g_d3dDevice->SetPixelShader(prevPS);
    *(void**)0x8E2448 = prevVS;
    *(void**)0x8E244C = prevPS;
    if(prevDecl){
        g_d3dDevice->SetVertexDeclaration(prevDecl);
        *(void**)0x8E2444 = prevDecl;
        *(void**)0x8E2440 = (void*)0xFFFFFFFF;
    }else{
        g_d3dDevice->SetVertexDeclaration(NULL);
        g_d3dDevice->SetFVF(prevFVF);
        *(void**)0x8E2444 = NULL;
        *(void**)0x8E2440 = (void*)prevFVF;
    }
    if(prevVS) prevVS->Release();
    if(prevPS) prevPS->Release();
    if(prevDecl) prevDecl->Release();

    // One-shot receipt: proves the route ran at least once (the per-draw
    // outcome line lives in main.cpp's water_flatpath throttle).
    static bool s_routeLogged = false;
    if(drew && !s_routeLogged){
        s_routeLogged = true;
        dbglog("[water] flatroute active: direct flushes now draw through the "
               "pipe (VS=IDR_WATER_VS PS=%s)", waterPipe_psTag(ps));
    }
    return drew;
}
