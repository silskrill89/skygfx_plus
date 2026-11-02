// texalpha.cpp — per-TEXTURE alpha classification for the PBR pipes.
//
// WHY
//   The PBR pixel shaders alpha-test with GREATER-EQUAL ref 1. A solid mesh
//   whose texture carries an unused alpha channel (canonically a DXT3 whose
//   explicit-alpha blocks are all 0x00 — artists leaving alpha unused) decodes
//   tex.a ~ 0 and the alpha test discards the whole surface: "solid faces not
//   solid". Genuinely cut-out textures need texture alpha, and faded textures
//   (glass, shatter) need both blending and texture alpha. Decals split into
//   TWO families (§4.2). HARD masks (0/255 + thin seam) alpha-test (CUTOUT) —
//   blend off, ZWRITE on, ZTEST on; the DISCARDED texels write nothing, so
//   the quad cannot depth-carve its plane over characters. SOFT masks
//   (mid-dominant ink, no full-alpha population — live log: cleargraf02_LA
//   zero=14% full=0% mid=85%) have nothing to test against: under
//   GREATEREQUAL@1 every mid texel passes (paints hard AND still writes
//   depth), so alpha-testing does NOT stop the carve. They land FADED
//   (blend) and the PIPE must draw them as overlays: ZWRITE OFF, ZTEST ON —
//   a blended quad with ZWRITE on always depth-carves its plane, and
//   characters drawn later (peds, feet at the road plane just behind the
//   decal's raised plane) are depth-rejected ("decals render over peds").
//   The decision belongs to the TEXTURE, so classify once and cache by
//   RwRaster*.
//
//   A previous attempt floored alpha in the shaders and bypassed
//   ALPHATESTENABLE per mesh; that broke vegetation cut-outs and was reverted.
//   This module deliberately touches neither: the pipe only chooses the CPU
//   blend enable + the c48.w alpha override (1 = force opaque, 0 = texture
//   alpha) per MESH from a per-TEXTURE class.
//
// D3D TEXTURE EXTRACTION (game RwD3D9SetTexture internals, 0x7FDCD0):
//   ext = raster + *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*,
//   ext+8 = valid byte flag. The extraction itself now lives in
//   TexDefsGetD3DTexture (texdefs.cpp) — same guards as before:
//   ext+8 is NOT trusted alone: some paths leave it stale while ext+0 still
//   holds a live pointer, so a plausible pointer is accepted as well.
//
// CACHE
//   4-way set-associative table keyed by texture->raster: TAC_CACHE_SETS (1024)
//   sets x TAC_CACHE_WAYS (4) ways = 4096 entries, each the final class +
//   failure reason + the histogram that produced it (so cache hits can still
//   print real diagnostic counts). The old direct-mapped 1024-entry table
//   thrashed whenever two live rasters collided on the index: every eviction
//   forced a fresh LockRect+scan, the per-frame budget ran out, and all later
//   meshes in the callback fell back to TAC_UNKNOWN (live log: only 638/1024
//   slots ever filled, 1.95M-2.11M budget skips). Four ways per set remove that
//   ping-pong while keeping the memory static (no heap).
//   Extraction-failure UNKNOWN is cached too, so a texture that cannot be
//   extracted is not re-probed every frame. At most TAC_CLASSIFY_BUDGET (256)
//   NEW textures are inspected per render frame (RWSRCGLOBAL(renderFrame)); a
//   miss past the budget returns TAC_UNKNOWN and is retried on a later frame.
//   NULL-TEXTURE draws (!texture || !texture->raster) are special-cased
//   BEFORE the budget: there is no cache key and nothing to probe, so they are
//   neither cached nor counted against the budget. Call
//   TexAlpha_ResetCache() to retry everything after a fix.
//
// FAULT SAFETY
//   The whole extraction + scan runs inside __try/__except. The function has
//   POD locals only (C2712-safe); any fault returns TAC_UNKNOWN. UnlockRect is
//   balanced on the normal path AND on the fault path (the lock owner flag is
//   hoisted out of __try; the fault-path unlock is itself fault-guarded).

#include "skygfx.h"
#include "texalpha.h"
#include "texdefs.h"	// TexDefsGetD3DTexture — the ONE 0xB4E9E0 extraction
#include <string.h>

#define TAC_CACHE_SETS      1024
#define TAC_CACHE_WAYS      4
#define TAC_CACHE_SIZE      (TAC_CACHE_SETS * TAC_CACHE_WAYS)
// Per-frame NEW-texture classification budget. 256 ≈ a 4k-texture level warms
// up in ~16 frames; one LockRect+scan is cheap next to the draw it protects
// and the scan caps below are unchanged, so the old 32/frame starved real
// textures into TAC_UNKNOWN for a whole session (26k budget skips per run).
#define TAC_CLASSIFY_BUDGET 256
#define TAC_MAX_BLOCKS      4096
#define TAC_MAX_ALPHA_BYTES 262144
#define TAC_EXT_OFF_UNREAD  0xFFFFFFFFu

// FADED is a FRACTION, not "any intermediate texel". SA stores DXT3/DXT5
// alpha on car paint and foliage atlases with broad 1-texel antialiased
// bands around every cut-out; the old "any nibble in 1..14" rule therefore
// turned whole solid body/vegetation textures FADED, moved those meshes into
// the transparent pass and sorted them after opaque geometry (the "vegetation
// drawn over everything" + translucent-car symptom).
//
// 35% was still far too low for THIN-FEATURE atlases: a chain-link fence
// spends most of its texels on wire (0) and AA fringe (intermediate), so the
// fringe alone cleared 35% and the fence became FADED → blendOn=1 → literally
// transparent (live log: lasjmfence1 mid=56.6%, Bow_meshfence mid=46.5%).
// Only textures that are GENUINELY mostly intermediate may blend (glass,
// green_glass_64 at 100%, vehicleshatter128 at 98%).
//
// §4.2 UPDATE: the fence/decal histograms above are now caught by the
// AA-fringe CUTOUT gate BELOW (extremes >= 40%, zero share 10..85%) and
// alpha-test instead of blending — the fringe can no longer earn FADED at
// all, because the FADED check only runs when the extremes are a minority.
// lasjmfence1/Bow_meshfence-class atlases (mid 40-60%, both extremes
// present) route to CUTOUT: blend OFF, holes cut, ZWRITE ON, ZTEST ON.
//
// Variation is REQUIRED: a histogram with no 0/255 texel at all (a single
// uniform mid value) is not a faded surface — there is nothing to blend, and
// the mesh stayed in the transparent pass with ZWRITE on, so unsorted wall
// faces stacked in camera-pitch order. Live log: LAN2skyscra9b_LOD 0/0/4096,
// vgsNelec_fence_lod 84% mid. Such histograms are demoted to OPAQUE below.
#define TAC_FADED_PERCENT   60

// CUTOUT is reserved for a HARD 0/255 mask — the shape a fence/railing/foliage
// card is authored to. Both extremes must be present AND the intermediate band
// must be negligible (<2%): DXT3/DXT5 compression of a hard edge leaves only a
// thin interpolation seam, while a soft/gradient alpha (or an atlas whose AA
// fringe dominates) exceeds it. Without this bimodality gate the old
// "zero>0 && full>0" rule turned every solid texture carrying a stray alpha
// region into an alpha-tested mask, and the PBR shader's GREATEREQUAL ref 1
// test then discarded every alpha==0 texel → holes in walls and the geometry
// behind them showing through (live log: CUTOUT tancolum1 mid=6.2% even though
// the texture is a solid wall).
// §4.2: this hard gate is now the FIRST of two CUTOUT admissions — the second
// (AA-fringe gate, defines below) admits the 2..60% intermediate fringe that
// every real decal/fence mask carries, under a stricter zero-share floor
// (10%) so stray-alpha solid walls stay out.
#define TAC_CUTOUT_MID_PERCENT 2

// STRAY-ALPHA bounds on the zero share. The >=1% floor keeps one stray
// transparent texel from turning a solid texture into a cut-out; the <=85%
// ceiling keeps a mostly-empty atlas (a solid texture with an unused alpha
// channel that happens to decode ~all zero) from being alpha-tested into
// holes. A real cut-out card leaves a meaningful opaque part inside the
// 1..85% window.
#define TAC_CUTOUT_ZERO_MAX_PERCENT 85

// AA-FRINGE CUTOUT admission (master prompt §4.2 decals). The hard gate above
// demands a seam-free mask (mid < 2%), but EVERY SA cut-out card — road
// decals, shadow/splatter blobs, chain-link fences, foliage — carries a
// 1-texel antialiased band around its mask edge, and DXT3/DXT5 compression
// widens it. Those histograms (mid 2..60%, both extremes present) fell
// through to OPAQUE: the pipes then force shader alpha 1, the GREATEREQUAL@1
// alpha test accepts EVERY texel, and the cut-out paints its transparent
// texels' RGB fill as a SOLID quad with ZWRITE still on — the quad
// depth-carves its own plane, so characters behind it are depth-rejected
// ("fences/decals break peds") and the decal renders as a solid sheet.
// The admission rule: both extremes present, the EXTREMES dominate (>=40%
// combined, i.e. the intermediate band is a minority fringe, not a
// transmission gradient), and the transparent part is a real shape (zero
// share 10..85% — the 10% floor keeps tancolum1-style solid walls with a
// small stray alpha region out; the 85% ceiling keeps mostly-empty atlases
// out). Class result is the same TAC_CUTOUT: blend OFF, texture alpha into
// the GREATEREQUAL@1 test, ZWRITE ON, ZTEST ON — the decal cuts out, writes
// depth only for its opaque texels, and depth-tests against peds (drawn
// after peds but Z-tested), so it can never overdraw a character.
//
// ESCAPE FAMILIES (both deliberate — do NOT "fix" by loosening):
//   - full=0 soft masks (cleargraf02_LA: zero=14% full=0% mid=85%): gate (c)
//     demands a full-alpha population to test against, so these land FADED
//     (blend) — which is CORRECT: alpha-testing them would keep every mid
//     texel at full RGB and still write depth. The carve is stopped by the
//     PIPE's decal policy (ZWRITE OFF for blended FADED meshes — reported
//     hunk for buildingPipe.cpp PBR :2769 / Xbox :1958), not by this gate.
//   - zero 2..10% mostly-ink decals: the 10% zero floor is the tancolum1
//     guard (a solid wall with a small stray alpha region must stay OPAQUE
//     or the ref-1 test holes the wall). Lowering it trades a solid-sheet
//     decal for holes in walls — leave it.
#define TAC_CUTOUT_AA_EXTREMES_PERCENT 40
#define TAC_CUTOUT_AA_ZERO_MIN_PERCENT 10

// F9 TIGHTENING (2026-10-31, black-blotch road decals). The intermediate band
// is split into three sub-bands (see TacHist) so the FADED gate can tell a
// genuine gradient apart from a hard-mask edge band that DXT interpolation /
// authored AA widened:
//   - FADED now requires the mid band to NOT be near-opaque fringe: if >=60%
//     of the intermediate texels sit in 192..254, the "soft" band is the
//     widened edge of a HARD mask, not a transmission gradient. Blending such
//     a decal (the new buildingPipe decalBlend policy, :2771/:1959) smears
//     its dark RGB fill over the road at partial alpha — the black blotches.
//   - Those histograms route to the NEW (c2) hi-fringe CUTOUT admission:
//     alpha-test, blend OFF, ZWRITE ON — painting a near-opaque fringe texel
//     at full RGB is visually next to blending it at 200..250 alpha, and it
//     can never darken the background.
// ESCAPE FAMILIES after the tightening (do NOT "fix" by loosening):
//   - cleargraf02_LA (zero=14% full=0% mid=85%) stays FADED only if its mid
//     band is a real gradient (core/lo spread). The watch receipt prints
//     lo/core/hi for it every run — if it ever shows hi-dominant it is a
//     hard mask and CUTOUT is the correct class for it too.
//   - lo-clustered faint overlays (mid mostly 1..63, e.g. watermark decals):
//     NOT hi-fringe, so they stay FADED and blend faintly — alpha-testing
//     them would paint the faint art at full RGB (way too strong).
//   - uniform mid with no extremes: variation gate demotes to OPAQUE
//     (unchanged).
#define TAC_FADED_HI_FRINGE_PERCENT   60
#define TAC_CUTOUT_HI_FRINGE_PERCENT  60

// ---------------------------------------------------------------------------
// Cache + per-frame budget.
// ---------------------------------------------------------------------------
//
// Set-associative layout: set = index / TAC_CACHE_WAYS, so a set's 4 entries
// are contiguous and TacCacheFind walks at most 4 keys. `s_tacVictim[set]`
// is the round-robin victim way for a set that has no empty way left (the
// small per-set cursor replaces any age bookkeeping). Only static arrays:
// memory use is fixed at 4096 entries + 1024 victim bytes.

	struct TacCacheEntry {
	RwRaster *raster;	// NULL = empty slot; key is texture->raster
	int cls;		// TexAlphaClass
	int reason;		// TacReason that produced cls (TACR_NONE on success)
	// Histogram that produced cls, cached so the first-8-per-class diagnostic
	// can print REAL counts on a cache hit as well. Before this, only the
	// newly-classified path logged, and DXT1 (early-return) plus no-alpha
	// formats logged an all-zero histogram that looked like a dead classifier.
	uint32 nZero;
	uint32 nFull;
	uint32 nMid;
	uint32 nMidLo;		// intermediate 1..63 (near-transparent fringe)
	uint32 nMidCore;	// intermediate 64..191 (gradient body)
	uint32 nMidHi;		// intermediate 192..254 (near-opaque fringe)
	uint32 nTotal;
	int fmt;		// D3DFORMAT the scan saw (int; -1 = not read)
	uint8 logged;		// diagnostic sample already printed for this entry
};

static TacCacheEntry s_tacCache[TAC_CACHE_SIZE];
static uint8 s_tacVictim[TAC_CACHE_SETS];	// next way to evict per set
static uint32 s_tacFrame = 0xFFFFFFFF;
static int s_tacBudget = 0;
static int s_tacUsed = 0;

// Failure reasons. Every path that cannot classify returns one of these so the
// summary line names the exact pipeline step that broke. NO_EXT_PTR is the
// "flag claims the extension is live but ext+0 is not a plausible pointer"
// case; a plain null/wild pointer still reports NO_D3D_TEX.
enum TacReason {
	TACR_NONE = 0,
	TACR_NO_TEXTURE,
	TACR_NO_RASTER,
	TACR_NO_EXT_OFFSET,
	TACR_NO_EXT_PTR,
	TACR_NO_D3D_TEX,
	TACR_NO_LEVELDESC,
	TACR_LOCK_FAILED,
	TACR_UNSUPPORTED_FORMAT,
	TACR_COUNT
};

static const char * const s_tacReasonName[TACR_COUNT] = {
	"none",
	"NO_TEXTURE",
	"NO_RASTER",
	"NO_EXT_OFFSET",
	"NO_EXT_PTR",
	"NO_D3D_TEX",
	"NO_LEVELDESC",
	"LOCK_FAILED",
	"UNSUPPORTED_FORMAT"
};

// Summary counters (diagnostics only).
static unsigned s_tacOpaque = 0;
static unsigned s_tacCutout = 0;
// Sub-count of s_tacCutout: classifications admitted by the AA-fringe gate
// (c) rather than the hard seam-free gate (b). The §4.2 decal/fence fix is
// observable as this counter moving while s_tacCutout grows.
static unsigned s_tacCutoutAACount = 0;
// Gate marker written by TacClassFromHist on EVERY rule exit: 0 = no
// fringe-style admission, 1 = the AA fringe gate (c), 2 = the hi-fringe gate
// (c2, F9 tightening), 3 = the vegetation FADED->CUTOUT rescue (S6
// tightening, name-hinted). Consumed immediately after TacClassifyTex
// returns (single-threaded render); a fault inside the SEH block can only
// leave a stale marker that is ignored unless the class is TAC_CUTOUT, and
// every CUTOUT return rewrites the marker first.
static int s_tacAAGate = 0;
// S6 TIGHTENING (2026-10-31, black plant/bush silhouettes): 1 when the
// texture being classified has a vegetation name (TacNameIsVeg below). Set
// by TacClassifyTex before the scan and read by TacClassFromHist's FADED
// gate — same single-threaded static pattern as s_tacAAGate. A vegetation
// texture whose DXT-widened AA fringe pushed the intermediate band past 60%
// (extremes a minority, hi-fringe check passed) used to blend its dark RGB
// fill at partial alpha = black silhouette; with the hint it alpha-tests.
static int s_tacVegHint = 0;
static unsigned s_tacFaded = 0;
// Sub-count of s_tacOpaque: histograms whose alpha was a single mid value with
// no 0/255 texel at all, demoted from FADED by TacClassFromHist (variation
// gate). Blending those made the whole wall transparent with ZWRITE on.
static unsigned s_tacFadedUniform = 0;
// Sub-count of s_tacCutout: classifications admitted by the hi-fringe gate
// (c2, F9 tightening) — mid-dominant masks whose intermediate band hugs 255.
static unsigned s_tacCutoutHiFringe = 0;
// Histograms synthesized for no-alpha formats (alpha guaranteed 255 after
// expansion): the F5 "hist=none" fix. These are REAL classifications — the
// scan path ran and answered — not skipped probes.
static unsigned s_tacNoAlphaFmt = 0;
// Sub-count of s_tacCutout: FADED histograms rescued to CUTOUT by the S6
// vegetation name hint (gate marker 3). Observable as this counter moving
// while s_tacFaded stays flat on a vegetation-heavy run.
static unsigned s_tacCutoutVeg = 0;
static unsigned s_tacUnknown = 0;
// Split of the UNKNOWN mass for the summary line: texnull = texture/raster
// NULL at the call (expected for untextured materials, NOT a classifier
// failure); budget = real textures skipped past the per-frame budget. Budget
// skips return BEFORE classify and are not counted in s_tacUnknown, so
// unknown_budget mirrors s_tacBudgetSkips (also printed as budget=%u below)
// and texnull + budget does not have to equal the unknown figure — the rest
// are cached extraction-failure UNKNOWNs broken out in unknown_reasons.
static unsigned s_tacUnknownTexNull = 0;
static unsigned s_tacFaults = 0;
static unsigned s_tacReasons[TACR_COUNT] = {};
static uint8 s_tacReasonLogged[TACR_COUNT] = {};
static unsigned s_tacBudgetSkips = 0;
static int s_tacExtOffLogged = 0;
static int s_tacNameLog = 0;
// One-shot cap for the uniform-alpha demotion names (first 8 demotions).
static int s_tacFadedUniformLogged = 0;

// Per-class one-shot samples: the first 8 textures of each class print the
// histogram behind the decision, so a misclassified car/foliage texture can be
// named from the log instead of guessed from the aggregate summary. Indexed by
// TexAlphaClass (TAC_UNKNOWN..TAC_FADED).
static int s_tacClassLogged[4] = {0, 0, 0, 0};

static const char * const s_tacClassName[4] = {
	"UNKNOWN", "OPAQUE", "CUTOUT", "FADED"
};

// Full-width multiplicative mix. RwRaster* is at least 4-byte aligned, so the
// low bits carry almost no entropy; the first xor-shift folds high bits down,
// the odd multiply spreads them, and the second xor-shift mixes back into the
// bit range used for the set index (h & (TAC_CACHE_SETS-1)).
static uint32 TacHashKey(const RwRaster *ras)
{
	uint32 h = (uint32)(uintptr_t)ras;
	h ^= h >> 16;
	h *= 2654435761u;
	h ^= h >> 13;
	return h;
}

// Set-associative probe: at most TAC_CACHE_WAYS (4) pointer compares, no
// LockRect, no scan, no logging on the caller's hot path.
static TacCacheEntry *TacCacheFind(RwRaster *ras, uint32 hash)
{
	uint32 base = (hash & (uint32)(TAC_CACHE_SETS - 1)) * TAC_CACHE_WAYS;
	for(int w = 0; w < TAC_CACHE_WAYS; w++){
		TacCacheEntry *e = &s_tacCache[base + (uint32)w];
		if(e->raster == ras)
			return e;
	}
	return NULL;
}

// Choose an empty way first; otherwise the per-set round-robin victim. The
// slot is returned unfilled except for `raster`; the caller overwrites every
// field, so an evicted entry never leaks its old class/histogram. Never
// allocates: only the static table above.
static TacCacheEntry *TacCacheInstall(RwRaster *ras, uint32 hash)
{
	uint32 set = hash & (uint32)(TAC_CACHE_SETS - 1);
	uint32 base = set * TAC_CACHE_WAYS;
	for(int w = 0; w < TAC_CACHE_WAYS; w++){
		TacCacheEntry *e = &s_tacCache[base + (uint32)w];
		if(e->raster == NULL){
			s_tacUsed++;
			s_tacVictim[set] = (uint8)((w + 1) & (TAC_CACHE_WAYS - 1));
			e->raster = ras;
			return e;
		}
	}
	uint32 w = (uint32)(s_tacVictim[set] & (TAC_CACHE_WAYS - 1));
	s_tacVictim[set] = (uint8)((w + 1) & (TAC_CACHE_WAYS - 1));
	TacCacheEntry *e = &s_tacCache[base + w];
	e->raster = ras;
	return e;
}

// ---------------------------------------------------------------------------
// D3D9 scan helpers (no SEH here — the caller wraps the whole classify).
// ---------------------------------------------------------------------------

static uint16 TacU16(const uint8 *p)
{
	uint16 v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static uint32 TacU32(const uint8 *p)
{
	uint32 v;
	memcpy(&v, p, sizeof(v));
	return v;
}

// Per-texture alpha histogram shared by the four scanning paths. POD (uint32
// only) so TacClassifyTex stays C2712-safe inside __try.
//
// The intermediate band (1..254) is split into THREE sub-bands so the FADED
// gate can tell a genuine gradient apart from a hard-mask edge band hugging
// the extremes (F9 black-blotch decals, 2026-10-31):
//   nMidLo   1..63    near-transparent fringe
//   nMidCore 64..191  gradient body
//   nMidHi   192..254 near-opaque fringe
// nMid stays the sum of the three, so every existing fraction rule that tests
// nMid is unchanged.
struct TacHist {
	uint32 nZero;
	uint32 nFull;
	uint32 nMid;
	uint32 nMidLo;
	uint32 nMidCore;
	uint32 nMidHi;
	uint32 nTotal;
};

static void TacHistClear(TacHist *h)
{
	h->nZero = 0;
	h->nFull = 0;
	h->nMid = 0;
	h->nMidLo = 0;
	h->nMidCore = 0;
	h->nMidHi = 0;
	h->nTotal = 0;
}

// 8-bit alpha binning (DXT5 interpolated alpha / A8R8G8B8): the 0/255
// extremes plus the three intermediate sub-bands 1..63 / 64..191 / 192..254.
static void TacBinAlpha8(int a, uint32 *nZero, uint32 *nFull,
                         uint32 *nMidLo, uint32 *nMidCore, uint32 *nMidHi)
{
	if(a <= 0)        (*nZero)++;
	else if(a >= 255) (*nFull)++;
	else if(a <= 63)  (*nMidLo)++;
	else if(a <= 191) (*nMidCore)++;
	else              (*nMidHi)++;
}

// 4-bit explicit-alpha nibble (DXT3 / A4R4G4B4). Nibble v decodes alpha
// v*17, so the sub-band edges map to: 1..3 -> 17..51 (near-transparent),
// 4..11 -> 68..187 (gradient body), 12..14 -> 204..238 (near-opaque).
static void TacBinNibble(uint8 v, uint32 *nZero, uint32 *nFull,
                         uint32 *nMidLo, uint32 *nMidCore, uint32 *nMidHi)
{
	if(v == 0)       (*nZero)++;
	else if(v == 15) (*nFull)++;
	else if(v <= 3)  (*nMidLo)++;
	else if(v <= 11) (*nMidCore)++;
	else             (*nMidHi)++;
}

// S6 vegetation name hint. Prefix match at position 0, case-insensitive —
// same convention as the watch list above. Deliberately NARROW: the hint
// only ever reroutes a histogram that ALREADY has a cut-out shape (see the
// FADED gate in TacClassFromHist), so a false-positive name on a solid
// texture (planter pot, trunk bark) changes nothing — the hint cannot fire
// without both alpha extremes present and a 10..85% zero share.
static const char * const s_tacVegPrefixes[] = {
	"plant", "bush", "tree", "shrub", "veg", "palm", "trunk", "leaf"
};

static int TacNameIsVeg(const RwTexture *tex)
{
	if(!tex || !tex->name[0])
		return 0;
	for(size_t i = 0;
	    i < sizeof(s_tacVegPrefixes) / sizeof(s_tacVegPrefixes[0]);
	    i++){
		if(_strnicmp(tex->name, s_tacVegPrefixes[i],
		             strlen(s_tacVegPrefixes[i])) == 0)
			return 1;
	}
	return 0;
}

// Single classification rule shared by ALL alpha-bearing formats (DXT1 /
// DXT3 / DXT5 / A8 / A4R4G4B4). Order is the contract:
//   (a) empty histogram                              -> OPAQUE
//   (b) hard 0/255 mask: both extremes present,
//       <2% intermediate, zero share in 1..85%       -> CUTOUT (alpha test)
//   (c) AA-fringe mask: both extremes present,
//       extremes >= 40% combined, zero share in
//       10..85%                                      -> CUTOUT (alpha test)
//   (d) >= TAC_FADED_PERCENT intermediate AND at
//       least one 0/255 texel present AND the mid
//       band is NOT near-opaque fringe (hi < 60% of
//       mid)                                         -> FADED  (blend + tex.a)
//       S6 exception: a vegetation-named texture
//       with a real cut-out shape (full > 0, zero
//       share 10..85%) is rescued to CUTOUT instead
//       — blending a foliage mask's dark RGB fill
//       at partial alpha is the black plant
//       silhouette.
//   (c2) mid-dominant but hi-fringe (>=60% of mid in
//       192..254) with a real cut-out shape (zero
//       share 10..85%)                               -> CUTOUT (alpha test)
//   (e) everything else                              -> OPAQUE (alpha = 1)
//
//   The "at least one extreme" clause in (d) is the variation gate: a texture
//   whose entire alpha is one uniform mid value (zero=0, full=0) has nothing
//   to fade and is demoted to OPAQUE by (e) instead of blending the whole
//   mesh with ZWRITE still on.
//
// RATIONALE
//   (c) exists because the <2% seam-free requirement of (b) is stricter than
//   any real SA asset: road decals, shadow/splatter blobs, chain-link fences
//   and foliage cards all carry an AA/compression fringe (mid 2..60%), and
//   under the old rule those histograms landed in OPAQUE — forced shader
//   alpha 1, the GREATEREQUAL@1 test accepts every texel, and the cut-out
//   painted its transparent texels' RGB fill as a SOLID ZWRITE-on quad that
//   depth-rejected the characters behind it (§4.2 "fences/decals break peds",
//   live log: lasjmfence1 mid=56.6%, Bow_meshfence mid=46.5%). With (c) the
//   same cards alpha-test: blend OFF, holes cut, depth written only for the
//   opaque texels, ZTEST ON — they draw over buildings but can never overdraw
//   a character. (d) is now reachable only when the extremes are a MINORITY
//   (<40%), so a fringe can no longer earn blending; genuinely
//   mostly-intermediate textures (glass, green_glass_64 at 100%,
//   vehicleshatter128 at 98%) still blend.
//   STRAY ALPHA: a solid texture (wall, column, hull) with an unused alpha
//   channel that carries a small stray transparent region fails (c)'s 10%
//   zero floor and/or (b)'s <2% mid test, so it is OPAQUE — the previous
//   "zero>0 && full>0" rule turned exactly those into CUTOUT and the PBR
//   ref-1 alpha test discarded every alpha==0 texel, leaving holes.
//
// The CUTOUT branches are what make genuine thin-feature atlases solid
// where they are opaque and empty where they are cut: alpha-tested, never
// blended, always Z-tested.
static int TacClassFromHist(const TacHist *h)
{
	if(h->nTotal == 0)
		return TAC_OPAQUE;
	// (b) hard seam-free mask — unchanged.
	if(h->nZero > 0 && h->nFull > 0
	   && (h->nMid * 100) < (h->nTotal * TAC_CUTOUT_MID_PERCENT)
	   && (h->nZero * 100) >= h->nTotal	// zero share >= 1%
	   && (h->nZero * 100) <= (h->nTotal * TAC_CUTOUT_ZERO_MAX_PERCENT)){
		s_tacAAGate = 0;
		return TAC_CUTOUT;
	}
	// (c) AA-fringe mask — NEW (§4.2 decals). See the define block above.
	if(h->nZero > 0 && h->nFull > 0
	   && ((h->nZero + h->nFull) * 100) >= (h->nTotal * TAC_CUTOUT_AA_EXTREMES_PERCENT)
	   && (h->nZero * 100) >= (h->nTotal * TAC_CUTOUT_AA_ZERO_MIN_PERCENT)
	   && (h->nZero * 100) <= (h->nTotal * TAC_CUTOUT_ZERO_MAX_PERCENT)){
		s_tacAAGate = 1;
		return TAC_CUTOUT;
	}
	// (d) FADED — reached only when the extremes are a minority (<40%), so an
	// AA fringe can no longer earn blending. Variation gate: no 0/255 texel
	// at all means there is nothing to fade — fall through to OPAQUE.
	// F9 TIGHTENING: the mid band must also be a real gradient, not
	// near-opaque fringe — see the TAC_FADED_HI_FRINGE_PERCENT block above.
	// S6 TIGHTENING (2026-10-31, black plant/bush silhouettes): a vegetation
	// texture can still land here when its DXT-compressed AA fringe is so
	// wide that mid >= 60% while the extremes stay a minority (e.g.
	// zero=15% full=20% mid=65%, or a lo-clustered fringe zero=10% full=5%
	// mid=85%) — the hi-fringe check passes because the fringe hugs 0, not
	// 255. Blending such a mask paints the foliage RGB fill (dark in the
	// cut-out background of most SA vegetation TXDs) at partial alpha over
	// the whole card = the pure-black plant/bush silhouette. Foliage is a
	// HARD mask by construction, so when the name says vegetation AND the
	// histogram still has a real cut-out shape (an opaque body to test
	// against, zero share inside the same 10..85% window gates (c)/(c2)
	// use), alpha-test it instead. The name hint alone can NEVER turn a
	// genuine gradient FADED texture into CUTOUT: cleargraf02_LA (full=0)
	// fails the full>0 requirement, and any texture outside the zero window
	// is untouched. A zero-share band check on gate (d) in general was
	// REJECTED: cleargraf02_LA's zero=14% sits inside the 10..85% window, so
	// a name-free band check would have demoted the real gradient.
	if((h->nMid * 100) >= (h->nTotal * TAC_FADED_PERCENT)
	   && (h->nZero + h->nFull) > 0
	   && (h->nMidHi * 100) < (h->nMid * TAC_FADED_HI_FRINGE_PERCENT)){
		if(s_tacVegHint
		   && h->nFull > 0
		   && (h->nZero * 100) >= (h->nTotal * TAC_CUTOUT_AA_ZERO_MIN_PERCENT)
		   && (h->nZero * 100) <= (h->nTotal * TAC_CUTOUT_ZERO_MAX_PERCENT)){
			s_tacAAGate = 3;
			return TAC_CUTOUT;
		}
		s_tacAAGate = 0;
		return TAC_FADED;
	}
	// (c2) hi-fringe mask — NEW (F9). Reaching here the extremes are a
	// minority, so mid > 60% of total; if >=60% of that mid sits in
	// 192..254 the intermediate band is the widened edge of a HARD mask
	// (DXT interpolation / authored AA), not a transmission gradient —
	// blending it smears the dark RGB fill over the background (black
	// blotch decals). Alpha-test it like any other cut-out: blend OFF,
	// texture alpha into the GREATEREQUAL@1 test, ZWRITE ON, ZTEST ON.
	// Guards mirror (c): a real cut-out shape (zero share 10..85%) keeps
	// stray-alpha solid walls (tancolum1) and mostly-empty atlases out.
	if(h->nMidHi > 0
	   && (h->nMidHi * 100) >= (h->nMid * TAC_CUTOUT_HI_FRINGE_PERCENT)
	   && h->nZero > 0
	   && (h->nZero * 100) >= (h->nTotal * TAC_CUTOUT_AA_ZERO_MIN_PERCENT)
	   && (h->nZero * 100) <= (h->nTotal * TAC_CUTOUT_ZERO_MAX_PERCENT)){
		s_tacAAGate = 2;
		return TAC_CUTOUT;
	}
	// (e) Gradient/soft alpha, a mostly-empty atlas, or a solid texture with
	// a stray alpha region: alpha is not a cut-out mask, so the pipe forces
	// the shader output alpha to 1 (CUTOUT keeps texture alpha + alpha test).
	s_tacAAGate = 0;
	return TAC_OPAQUE;
}

// Formats that have no alpha channel at all → the texture alpha is guaranteed
// 255 after expansion; no scan needed.
static bool TacFormatHasNoAlpha(D3DFORMAT fmt)
{
	switch(fmt){
	case D3DFMT_R8G8B8:
	case D3DFMT_X8R8G8B8:
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_R3G3B2:
	case D3DFMT_L8:
	case D3DFMT_L16:
	case D3DFMT_V8U8:
	case D3DFMT_V16U16:
	case D3DFMT_L6V5U5:
	case D3DFMT_Q8W8V8U8:
	case D3DFMT_X8L8V8U8:
	case D3DFMT_CxV8U8:
		return true;
	default:
		return false;
	}
}

// DXT1: alpha is binary (0 or 255), so nMid stays 0 and the shared rule can
// only return CUTOUT (hard bimodal mask: both extremes present AND the zero
// share inside the 1..85% window) or OPAQUE. A fully transparent DXT1 (all
// indices 3, nFull==0) therefore lands in OPAQUE per the shared rule — there
// is no cut-out shape to test. A block whose colour endpoints satisfy c0 <= c1
// uses the 3-colour + transparent mode: index 3 decodes alpha 0, indices 0..2
// decode alpha 255. Otherwise every texel is opaque. The histogram is FILLED
// (16 texels per block) instead of the old "return CUTOUT on the first
// transparent block and leave the histogram zeroed" path — that early return
// is exactly why the live log showed "CUTOUT ... zero=0 full=0 mid=0 total=0"
// for DXT1 atlases like lw_seat2.
static int TacScanDxt1(const uint8 *bits, uint32 pitch, uint32 width, uint32 height,
                       TacHist *outHist)
{
	uint32 bw = (width + 3) / 4;
	uint32 bh = (height + 3) / 4;
	uint32 blocks = bw * bh;
	if(blocks > TAC_MAX_BLOCKS)
		blocks = TAC_MAX_BLOCKS;
	if(!bw){
		TacHistClear(outHist);
		return TAC_OPAQUE;
	}

	uint32 nZero = 0, nFull = 0, nMid = 0, nTotal = 0;
	for(uint32 i = 0; i < blocks; i++){
		const uint8 *b = bits + (size_t)(i / bw) * pitch + (size_t)(i % bw) * 8;
		if(TacU16(b) <= TacU16(b + 2)){
			uint32 idx = TacU32(b + 4);	// 16 x 2-bit colour indices
			for(int t = 0; t < 16; t++){
				if(((idx >> (2 * t)) & 3u) == 3u) nZero++;
				else                              nFull++;
				nTotal++;
			}
		}else{
			nFull += 16;
			nTotal += 16;
		}
	}
	outHist->nZero = nZero;
	outHist->nFull = nFull;
	outHist->nMid = nMid;
	outHist->nMidLo = 0;	// DXT1 alpha is binary — no intermediate band
	outHist->nMidCore = 0;
	outHist->nMidHi = 0;
	outHist->nTotal = nTotal;
	return TacClassFromHist(outHist);
}

// DXT3: bytes 0-7 = 16 explicit 4-bit alpha nibbles (one texel each).
// Every nibble is binned (0 / 15 / three intermediate sub-bands) and the
// shared fraction rule in TacClassFromHist decides — see the
// TAC_FADED_PERCENT comment above.
static int TacScanDxt3(const uint8 *bits, uint32 pitch, uint32 width, uint32 height,
                       TacHist *outHist)
{
	uint32 bw = (width + 3) / 4;
	uint32 bh = (height + 3) / 4;
	uint32 blocks = bw * bh;
	if(blocks > TAC_MAX_BLOCKS)
		blocks = TAC_MAX_BLOCKS;
	if(!bw)
		return TAC_OPAQUE;

	uint32 nZero = 0, nFull = 0, nMid = 0, nMidLo = 0, nMidCore = 0, nMidHi = 0;
	uint32 nTotal = 0;
	for(uint32 i = 0; i < blocks; i++){
		const uint8 *b = bits + (size_t)(i / bw) * pitch + (size_t)(i % bw) * 16;
		for(int k = 0; k < 8; k++){
			uint8 v = b[k];
			TacBinNibble((uint8)(v & 0xF), &nZero, &nFull, &nMidLo, &nMidCore, &nMidHi);
			TacBinNibble((uint8)(v >> 4), &nZero, &nFull, &nMidLo, &nMidCore, &nMidHi);
			nTotal += 2;
		}
	}
	nMid = nMidLo + nMidCore + nMidHi;
	outHist->nZero = nZero;
	outHist->nFull = nFull;
	outHist->nMid = nMid;
	outHist->nMidLo = nMidLo;
	outHist->nMidCore = nMidCore;
	outHist->nMidHi = nMidHi;
	outHist->nTotal = nTotal;
	return TacClassFromHist(outHist);
}

// DXT5: bytes 0-1 = alpha endpoints, bytes 2-7 = 16 x 3-bit indices.
static int TacDxt5TexelAlpha(const uint8 *b, int i)
{
	int a0 = b[0];
	int a1 = b[1];
	uint64_t bits = 0;
	for(int k = 0; k < 6; k++)
		bits |= (uint64_t)b[2 + k] << (k * 8);
	int idx = (int)((bits >> (3 * i)) & 7);
	if(idx == 0)
		return a0;
	if(idx == 1)
		return a1;
	if(a0 > a1)
		return ((8 - idx) * a0 + (idx - 1) * a1) / 7;
	if(idx == 6)
		return 0;
	if(idx == 7)
		return 255;
	return ((6 - idx) * a0 + (idx - 1) * a1) / 5;
}

static int TacScanDxt5(const uint8 *bits, uint32 pitch, uint32 width, uint32 height,
                       TacHist *outHist)
{
	uint32 bw = (width + 3) / 4;
	uint32 bh = (height + 3) / 4;
	uint32 blocks = bw * bh;
	if(blocks > TAC_MAX_BLOCKS)
		blocks = TAC_MAX_BLOCKS;
	if(!bw)
		return TAC_OPAQUE;

	uint32 nZero = 0, nFull = 0, nMid = 0, nMidLo = 0, nMidCore = 0, nMidHi = 0;
	uint32 nTotal = 0;
	for(uint32 i = 0; i < blocks; i++){
		const uint8 *b = bits + (size_t)(i / bw) * pitch + (size_t)(i % bw) * 16;
		for(int t = 0; t < 16; t++){
			int a = TacDxt5TexelAlpha(b, t);
			TacBinAlpha8(a, &nZero, &nFull, &nMidLo, &nMidCore, &nMidHi);
			nTotal++;
		}
	}
	nMid = nMidLo + nMidCore + nMidHi;
	outHist->nZero = nZero;
	outHist->nFull = nFull;
	outHist->nMid = nMid;
	outHist->nMidLo = nMidLo;
	outHist->nMidCore = nMidCore;
	outHist->nMidHi = nMidHi;
	outHist->nTotal = nTotal;
	return TacClassFromHist(outHist);
}

// A8R8G8B8 / A8B8G8R8: alpha is the high byte of every 4-byte texel.
static int TacScanA8(const uint8 *bits, uint32 pitch, uint32 width, uint32 height,
                     TacHist *outHist)
{
	uint32 nZero = 0, nFull = 0, nMid = 0, nMidLo = 0, nMidCore = 0, nMidHi = 0;
	uint32 nTotal = 0;
	uint32 scanned = 0;
	for(uint32 y = 0; y < height && scanned < TAC_MAX_ALPHA_BYTES; y++){
		const uint8 *row = bits + (size_t)y * pitch;
		uint32 px = width;
		uint32 room = (TAC_MAX_ALPHA_BYTES - scanned) / 4;
		if(px > room)
			px = room;
		for(uint32 x = 0; x < px; x++){
			uint8 a = row[x * 4 + 3];
			TacBinAlpha8(a, &nZero, &nFull, &nMidLo, &nMidCore, &nMidHi);
			nTotal++;
		}
		scanned += px * 4;
	}
	nMid = nMidLo + nMidCore + nMidHi;
	outHist->nZero = nZero;
	outHist->nFull = nFull;
	outHist->nMid = nMid;
	outHist->nMidLo = nMidLo;
	outHist->nMidCore = nMidCore;
	outHist->nMidHi = nMidHi;
	outHist->nTotal = nTotal;
	return TacClassFromHist(outHist);
}

// A4R4G4B4: 16-bit texels, alpha is the high nibble of the second byte
// (word = A<<12 | R<<8 | G<<4 | B). Same 0/15/mid binning as DXT3 nibbles.
static int TacScanA4R4G4B4(const uint8 *bits, uint32 pitch, uint32 width, uint32 height,
                           TacHist *outHist)
{
	uint32 nZero = 0, nFull = 0, nMid = 0, nMidLo = 0, nMidCore = 0, nMidHi = 0;
	uint32 nTotal = 0;
	uint32 scanned = 0;
	for(uint32 y = 0; y < height && scanned < TAC_MAX_ALPHA_BYTES; y++){
		const uint8 *row = bits + (size_t)y * pitch;
		uint32 px = width;
		uint32 room = (TAC_MAX_ALPHA_BYTES - scanned) / 2;
		if(px > room)
			px = room;
		for(uint32 x = 0; x < px; x++){
			uint8 a = (uint8)(row[x * 2 + 1] >> 4);
			TacBinNibble(a, &nZero, &nFull, &nMidLo, &nMidCore, &nMidHi);
			nTotal++;
		}
		scanned += px * 2;
	}
	nMid = nMidLo + nMidCore + nMidHi;
	outHist->nZero = nZero;
	outHist->nFull = nFull;
	outHist->nMid = nMid;
	outHist->nMidLo = nMidLo;
	outHist->nMidCore = nMidCore;
	outHist->nMidHi = nMidHi;
	outHist->nTotal = nTotal;
	return TacClassFromHist(outHist);
}

// Formats handled by a scan (after TacFormatHasNoAlpha). Anything else is a
// real failure: report UNSUPPORTED_FORMAT instead of silently going UNKNOWN.
static bool TacFormatScannable(D3DFORMAT fmt)
{
	switch(fmt){
	case D3DFMT_DXT1:
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:
	case D3DFMT_A8R8G8B8:
	case D3DFMT_A8B8G8R8:
	case D3DFMT_A4R4G4B4:
		return true;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Classify one texture. SEH-wrapped; POD locals only.
// ---------------------------------------------------------------------------

static int TacClassifyTex(const RwTexture *tex, int *outReason,
                          uint32 *outExtOff, int *outFmt, TacHist *outHist)
{
	*outReason = TACR_NONE;
	*outExtOff = TAC_EXT_OFF_UNREAD;
	*outFmt = -1;
	TacHistClear(outHist);
	// S6 vegetation hint, read by TacClassFromHist's FADED gate. Pure name
	// check — no D3D access, safe outside __try, and it must be set before
	// every TacClassFromHist call including the no-alpha-format early path.
	s_tacVegHint = TacNameIsVeg(tex);

	// AUDIT: hoisted out of __try (POD locals — C2712-safe) so the fault
	// path below can balance a LockRect that the scan faulted inside of.
	IDirect3DTexture9 *d3d = NULL;
	D3DLOCKED_RECT lr;
	lr.pBits = NULL;
	lr.Pitch = 0;
	int lrLocked = 0;

	__try{
		// Game RwD3D9SetTexture formula (0x7FDCD0) — the whole guard chain
		// (NULL tex/raster, extOff sanity, ext+8 flag OR plausible pointer)
		// lives in TexDefsGetD3DTexture (texdefs.cpp). outExtOff goes straight
		// through, so the raw offset word lands in the diagnostics at exactly
		// the point it always did (0xFFFFFFFF stays "unread" before the read).
		// The reason mapping is 1:1 — every TACR_* fires on the condition it
		// always did.
		int why = TEXDEFS_D3D_WHY_NO_TEXTURE;
		d3d = TexDefsGetD3DTexture((RwTexture *)tex, &why, outExtOff);
		if(!d3d){
			switch(why){
			case TEXDEFS_D3D_WHY_NO_TEXTURE:
				*outReason = TACR_NO_TEXTURE;
				break;
			case TEXDEFS_D3D_WHY_NO_RASTER:
				*outReason = TACR_NO_RASTER;
				break;
			case TEXDEFS_D3D_WHY_BAD_EXT_OFFSET:
				*outReason = TACR_NO_EXT_OFFSET;
				break;
			case TEXDEFS_D3D_WHY_BAD_EXT_PTR:
				// Flag claims the extension is live but ext+0 is not a
				// plausible D3D9 texture pointer; never call through it
				// (would fault).
				*outReason = TACR_NO_EXT_PTR;
				break;
			default:	// TEXDEFS_D3D_WHY_NO_D3D_TEX
				*outReason = TACR_NO_D3D_TEX;
				break;
			}
			return TAC_UNKNOWN;
		}

		D3DSURFACE_DESC desc;
		memset(&desc, 0, sizeof(desc));
		if(FAILED(d3d->GetLevelDesc(0, &desc))){
			*outReason = TACR_NO_LEVELDESC;
			return TAC_UNKNOWN;
		}
		*outFmt = (int)desc.Format;
		if(desc.Width == 0 || desc.Height == 0){
			*outReason = TACR_NO_LEVELDESC;
			return TAC_UNKNOWN;
		}

		// No alpha channel in the format at all → alpha is 1.0 everywhere
		// after expansion. F5 FIX (2026-10-31, CJ grey mannequin follow-up):
		if(TacFormatHasNoAlpha(desc.Format)){
			// Fill the HONEST histogram instead of returning "hist=none" —
			// the alpha channel is guaranteed fully opaque, so nFull =
			// nTotal = width*height. The old early-return left nTotal==0
			// and the receipt printed "hist=none", which read as "the
			// classifier never ran" — live log: the five player skin parts
			// torso/head/legs/feet/glasses (the ONLY hist=none lines of the
			// whole session). The class is UNCHANGED: the shared rule on an
			// all-full histogram is OPAQUE by (e) (no zero texel to test,
			// no mid to blend). Still one-shot per texture (cached by
			// raster) and budget-friendly: no LockRect is needed — the
			// histogram comes from the format, not the pixels.
			outHist->nZero = 0;
			outHist->nMid = 0;
			outHist->nMidLo = 0;
			outHist->nMidCore = 0;
			outHist->nMidHi = 0;
			outHist->nTotal = outHist->nFull = desc.Width * desc.Height;
			s_tacNoAlphaFmt++;
			return TacClassFromHist(outHist);
		}

		if(!TacFormatScannable(desc.Format)){
			*outReason = TACR_UNSUPPORTED_FORMAT;
			return TAC_UNKNOWN;
		}

		if(FAILED(d3d->LockRect(0, &lr, NULL, D3DLOCK_READONLY))){
			*outReason = TACR_LOCK_FAILED;
			return TAC_UNKNOWN;
		}
		lrLocked = 1;
		if(!lr.pBits){
			d3d->UnlockRect(0);
			lrLocked = 0;
			*outReason = TACR_LOCK_FAILED;
			return TAC_UNKNOWN;
		}

		int cls;
		switch(desc.Format){
		case D3DFMT_DXT1:
			cls = TacScanDxt1((const uint8 *)lr.pBits, (uint32)lr.Pitch,
			                  desc.Width, desc.Height, outHist);
			break;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
		case D3DFMT_DXT4:
			// DXT2/DXT4 are routed through the explicit-nibble DXT3 path.
			cls = TacScanDxt3((const uint8 *)lr.pBits, (uint32)lr.Pitch,
			                  desc.Width, desc.Height, outHist);
			break;
		case D3DFMT_DXT5:
			cls = TacScanDxt5((const uint8 *)lr.pBits, (uint32)lr.Pitch,
			                  desc.Width, desc.Height, outHist);
			break;
		case D3DFMT_A8R8G8B8:
		case D3DFMT_A8B8G8R8:
			cls = TacScanA8((const uint8 *)lr.pBits, (uint32)lr.Pitch,
			                desc.Width, desc.Height, outHist);
			break;
		case D3DFMT_A4R4G4B4:
			cls = TacScanA4R4G4B4((const uint8 *)lr.pBits, (uint32)lr.Pitch,
			                      desc.Width, desc.Height, outHist);
			break;
		default:
			// TacFormatScannable guarantees one of the cases above.
			d3d->UnlockRect(0);
			*outReason = TACR_UNSUPPORTED_FORMAT;
			return TAC_UNKNOWN;
		}
		d3d->UnlockRect(0);	// always balanced on the success path
		lrLocked = 0;
		return cls;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// AUDIT: a fault between LockRect and UnlockRect (wild pBits, texture
		// streamed out mid-scan) used to leak the locked level — every later
		// LockRect on that texture fails for its remaining lifetime. Balance
		// it here when we own one; the unlock itself is fault-guarded so a
		// broken device state cannot turn the recovery into a crash.
		if(lrLocked && d3d){
			__try{
				d3d->UnlockRect(0);
			}__except(EXCEPTION_EXECUTE_HANDLER){
				// unrecoverable either way — the texture is broken
			}
		}
		s_tacFaults++;
		*outReason = TACR_NONE;
		return TAC_UNKNOWN;
	}
}

// ---------------------------------------------------------------------------
// Diagnostics helpers (outside SEH).
// ---------------------------------------------------------------------------

static void TacLogExtOffOnce(const RwTexture *tex, uint32 extOff)
{
	if(s_tacExtOffLogged || extOff == TAC_EXT_OFF_UNREAD)
		return;
	s_tacExtOffLogged = 1;
	dbglog("texalpha: extOff=0x%08X from 0xB4E9E0 (tex='%s' raster=%p)",
	       (unsigned)extOff,
	       (tex && tex->name[0]) ? tex->name : "?",
	       (tex ? (void *)tex->raster : NULL));
}

// One plain (unthrottled) line per reason, so the first failing step is named
// immediately instead of being averaged into the periodic summary.
static void TacNoteReason(int reason, const RwTexture *tex,
                          uint32 extOff, int fmt)
{
	if(reason <= TACR_NONE || reason >= TACR_COUNT)
		return;
	s_tacReasons[reason]++;
	if(s_tacReasonLogged[reason])
		return;
	s_tacReasonLogged[reason] = 1;
	dbglog("texalpha: first %s tex='%s' raster=%p extOff=0x%08X fmt=%d",
	       s_tacReasonName[reason],
	       (tex && tex->name[0]) ? tex->name : "?",
	       (tex ? (void *)tex->raster : NULL),
	       (unsigned)extOff, fmt);
}

// One line per texture, capped at 8 per class. `cached` marks a class served
// from the direct-mapped cache: its counts come from the histogram stored in
// the entry, so they describe the classification inputs instead of printing
// zeroes. nTotal==0 means "not scanned" (extraction failure — no-alpha
// formats now synthesize an all-full histogram instead, F5 fix) and is
// logged as hist=none rather than a misleading all-zero histogram. fmt is
// the D3DFORMAT the scan saw (-1 = not read) — it names the exact reason a
// texture took the no-alpha path. lo/core/hi are the three intermediate
// sub-bands the FADED/(c2) gates test (F9). blendOn mirrors the pipe's
// decision: only FADED enables blending (OPAQUE/CUTOUT never do). mid% is
// the fraction the rule actually tests — FADED at >=60%, CUTOUT only below
// 2% — so the outcome can be argued straight from the log. legacy=1 marks
// the TAC_UNKNOWN class, whose pipe mapping is now the legacy hasAlpha
// fallback (parity item 2) — the marker also makes a run of the parity build
// distinguishable in the log.
static void TacLogClassSample(int cls, const RwTexture *tex, const TacHist *hist,
                              int cached, int fmt)
{
	if(cls < TAC_UNKNOWN || cls > TAC_FADED)
		return;
	// Cap check FIRST (cost audit 2026-10-25): after the 8th sample a call is
	// two compares and no dbglog formatting — on the hit path the caller also
	// pre-checks the counter before building the histogram, so an exhausted
	// class can never pay the format/printf cost.
	if(s_tacClassLogged[cls] >= 8)
		return;
	s_tacClassLogged[cls]++;
	if(hist->nTotal == 0){
		dbglog("texalpha: %s%s tex='%s' hist=none fmt=%d blendOn=%d legacy=%d",
		       s_tacClassName[cls], cached ? "(cached)" : "",
		       (tex && tex->name[0]) ? tex->name : "?",
		       fmt,
		       cls == TAC_FADED ? 1 : 0,
		       cls == TAC_UNKNOWN ? 1 : 0);
		return;
	}
	unsigned midPct = (unsigned)((hist->nMid * 100u) / hist->nTotal);
	dbglog("texalpha: %s%s tex='%s' zero=%u full=%u mid=%u(%u%%) "
	       "lo=%u core=%u hi=%u total=%u fmt=%d blendOn=%d legacy=%d",
	       s_tacClassName[cls], cached ? "(cached)" : "",
	       (tex && tex->name[0]) ? tex->name : "?",
	       (unsigned)hist->nZero, (unsigned)hist->nFull,
	       (unsigned)hist->nMid, midPct,
	       (unsigned)hist->nMidLo, (unsigned)hist->nMidCore,
	       (unsigned)hist->nMidHi, (unsigned)hist->nTotal,
	       fmt,
	       cls == TAC_FADED ? 1 : 0,
	       cls == TAC_UNKNOWN ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Watch-name one-shots (F5/F6/F9 follow-up, 2026-10-31; S6/F6-residual
// extension same day). The per-class sample cap (8) can fill with unrelated
// textures before a ped head part, a road decal or an interior prop is ever
// named. Textures in these families ALWAYS print one receipt with their
// class + full histogram + format, so the next run proves what class each
// head/decal/prop texture lands in. Prefix match, case-insensitive; capped
// globally (32); classification itself is untouched.
//   NEW families first (test-4 victims — the array is scanned in order, so a
//   name matching both a new and an old prefix resolves to the new family):
//     interior screens/props (F6 black TV/doorway/furniture quads):
//       tv television screen door monitor
//     vegetation (S6 black plant/bush silhouettes):
//       plant bush tree shrub veg palm trunk leaf
//     road/ground (F9 residual black blotch decals):
//       road ground asphalt
//   ped skin/head parts (F5 mannequin + F6 green facet / black head):
//     torso head legs feet glasses neck hair eyelash teeth tongue jaw
//   decal/overlay family (F9 black blotches):
//     cleargraf graf trafficlight skid blood shad decal
// NOTE on the cap: the 32-receipt budget is filled in CLASSIFICATION order
// (first watch-named textures the classifier sees win slots), not per
// family; the new prefixes are checked first so ties resolve to them, but
// chronology still governs. If a run exhausts 32 before a victim family is
// seen, raise the cap — do not drop old families (log analysis depends on
// their receipts).
// ---------------------------------------------------------------------------
static const char * const s_tacWatchPrefixes[] = {
	// test-4 victims first (F6 interior / S6 vegetation / F9 road):
	"tv", "television", "screen", "door", "monitor",
	"plant", "bush", "tree", "shrub", "veg", "palm", "trunk", "leaf",
	"road", "ground", "asphalt",
	// ped parts + decals (F5/F9, unchanged):
	"torso", "head", "legs", "feet", "glasses", "neck", "hair",
	"eyelash", "teeth", "tongue", "jaw",
	"cleargraf", "graf", "trafficlight", "skid", "blood", "shad", "decal"
};
static int s_tacWatchLogged = 0;

static void TacLogWatchSample(int cls, const RwTexture *tex,
                              const TacHist *hist, int fmt, int reason)
{
	if(s_tacWatchLogged >= 32)
		return;
	if(!tex || !tex->name[0])
		return;
	const char *name = tex->name;
	for(size_t i = 0;
	    i < sizeof(s_tacWatchPrefixes) / sizeof(s_tacWatchPrefixes[0]);
	    i++){
		if(_strnicmp(name, s_tacWatchPrefixes[i],
		             strlen(s_tacWatchPrefixes[i])) != 0)
			continue;
		s_tacWatchLogged++;
		if(hist->nTotal == 0){
			dbglog("texalpha: watch '%s' -> %s hist=none fmt=%d reason=%s",
			       name, s_tacClassName[cls], fmt,
			       s_tacReasonName[reason]);
		}else{
			unsigned midPct = (unsigned)((hist->nMid * 100u) / hist->nTotal);
			dbglog("texalpha: watch '%s' -> %s zero=%u full=%u mid=%u(%u%%) "
			       "lo=%u core=%u hi=%u total=%u fmt=%d",
			       name, s_tacClassName[cls],
			       (unsigned)hist->nZero, (unsigned)hist->nFull,
			       (unsigned)hist->nMid, midPct,
			       (unsigned)hist->nMidLo, (unsigned)hist->nMidCore,
			       (unsigned)hist->nMidHi, (unsigned)hist->nTotal, fmt);
		}
		return;
	}
}

static void TacLogSummary(void)
{
	if(!dbglog_throttle("texalpha"))
		return;
	dbglog("texalpha: %u opaque, %u cutout (%u aa, %u hifringe), %u faded, "
	       "%u unknown, %u faults "
	       "(cached %d/%d) unknown_texnull=%u unknown_budget=%u "
	       "unknown_reasons: tex=%u raster=%u ext=%u extptr=%u "
	       "d3d=%u leveldesc=%u lock=%u format=%u budget=%u faded_uniform=%u "
	       "noalpha=%u watch=%u vegcutout=%u",
	       s_tacOpaque, s_tacCutout, s_tacCutoutAACount, s_tacCutoutHiFringe,
	       s_tacFaded, s_tacUnknown,
	       s_tacFaults, s_tacUsed, TAC_CACHE_SIZE,
	       s_tacUnknownTexNull, s_tacBudgetSkips,
	       s_tacReasons[TACR_NO_TEXTURE], s_tacReasons[TACR_NO_RASTER],
	       s_tacReasons[TACR_NO_EXT_OFFSET], s_tacReasons[TACR_NO_EXT_PTR],
	       s_tacReasons[TACR_NO_D3D_TEX], s_tacReasons[TACR_NO_LEVELDESC],
	       s_tacReasons[TACR_LOCK_FAILED], s_tacReasons[TACR_UNSUPPORTED_FORMAT],
	       s_tacBudgetSkips, s_tacFadedUniform, s_tacNoAlphaFmt,
	       s_tacWatchLogged, s_tacCutoutVeg);
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

int TexAlpha_Classify(const RwTexture *tex)
{
	// Global kill-switch, checked BEFORE anything else (cache probe, budget,
	// D3D access, counters). The PBR pipes already gate their calls on
	// config->texAlphaClassify, but main.cpp's ped probe and any out-of-tree
	// caller reach this function directly; with the feature off they must get
	// TAC_UNKNOWN at the cost of one load + branch and no state at all.
	if(!config || !config->texAlphaClassify)
		return TAC_UNKNOWN;

	// Null texture / null raster: no cache key and nothing to resolve, so this
	// path must NOT consume the per-frame budget and must not be cached. Live
	// log (23:04): all 15,282 UNKNOWNs were tex= (null-texture draws) and they
	// burned the per-frame classification budget, starving real textures
	// (vehicletyres128 stayed UNKNOWN). Count the reason + unknown and return
	// BEFORE the frame-budget check (null-RASTER case only; a null texture is
	// now OPAQUE — see below).
	if(!tex){
		// No diffuse texture at all: a valid SA state (flat/untextured geometry,
		// car paint, light/trim). The pipes bind gpWhiteTexture, so there is no
		// texture alpha to honour -- classify OPAQUE (blend off, shader alpha 1).
		// NOTE: an untextured mesh that used VERTEX alpha as opacity will now
		// render solid; true material alpha (matTranslucent) still blends.
		return TAC_OPAQUE;
	}
	if(!tex->raster){
		// Texture exists but its raster is not resolvable yet (streaming):
		// keep the retryable UNKNOWN path with its reason counters.
		int reason = tex ? TACR_NO_RASTER : TACR_NO_TEXTURE;
		TacNoteReason(reason, tex, TAC_EXT_OFF_UNREAD, -1);
		s_tacUnknown++;
		s_tacUnknownTexNull++;
		TacLogSummary();
		return TAC_UNKNOWN;
	}

	RwRaster *ras = tex->raster;
	uint32 hash = TacHashKey(ras);

	// Hit path: at most 4 pointer compares inside TacCacheFind. No LockRect,
	// no re-scan, and the only possible logging is the once-per-entry sample.
	TacCacheEntry *e = TacCacheFind(ras, hash);
	if(e){
		// Cached path: print the STORED histogram once per entry (first 8 of
		// the class), marked (cached), so a hit is never mistaken for a
		// zero-histogram/never-classified texture.
		if(!e->logged && e->cls >= TAC_UNKNOWN && e->cls <= TAC_FADED &&
		   s_tacClassLogged[e->cls] < 8){
			TacHist hist;
			hist.nZero = e->nZero;
			hist.nFull = e->nFull;
			hist.nMid = e->nMid;
			hist.nMidLo = e->nMidLo;
			hist.nMidCore = e->nMidCore;
			hist.nMidHi = e->nMidHi;
			hist.nTotal = e->nTotal;
			e->logged = 1;
			TacLogClassSample(e->cls, tex, &hist, 1, e->fmt);
		}
		return e->cls;
	}

	// Frame budget: cap how many NEW textures are inspected per render frame,
	// so a cold cache (level load / device reset) cannot stall a frame. Misses
	// past the budget are NOT cached and are retried on a later frame.
	//
	// ORDER IS THE CONTRACT (cost audit 2026-10-25): cache probe (above) ->
	// budget rollover/check -> TacClassifyTex. Everything D3D (GetLevelDesc,
	// LockRect) lives inside TacClassifyTex, so an over-budget frame makes
	// ZERO D3D calls and a cached texture never locks either. Do not move any
	// D3D access above this check.
	uint32 frame = RWSRCGLOBAL(renderFrame);
	if(frame != s_tacFrame){
		s_tacFrame = frame;
		s_tacBudget = 0;
	}
	if(s_tacBudget >= TAC_CLASSIFY_BUDGET){
		s_tacBudgetSkips++;
		return TAC_UNKNOWN;	// not cached — retried on a later frame
	}

	s_tacBudget++;

	int reason = TACR_NONE;
	uint32 extOff = TAC_EXT_OFF_UNREAD;
	int fmt = -1;
	TacHist hist;
	int cls = TacClassifyTex(tex, &reason, &extOff, &fmt, &hist);

	// UNKNOWN is cached together with its reason: a broken extraction must not
	// burn the per-frame budget re-probing the same texture forever. Install
	// picks an empty way first, else the per-set round-robin victim, and sets
	// the key; every other field is overwritten below.
	e = TacCacheInstall(ras, hash);
	e->cls = cls;
	e->reason = reason;
	e->nZero = hist.nZero;
	e->nFull = hist.nFull;
	e->nMid = hist.nMid;
	e->nMidLo = hist.nMidLo;
	e->nMidCore = hist.nMidCore;
	e->nMidHi = hist.nMidHi;
	e->nTotal = hist.nTotal;
	e->fmt = fmt;
	e->logged = 1;

	TacLogExtOffOnce(tex, extOff);
	TacNoteReason(reason, tex, extOff, fmt);

	// Uniform-alpha demotion: the scan saw intermediate alpha but not a single
	// 0/255 texel, so TacClassFromHist's variation gate sent it to OPAQUE
	// (nZero + nFull == 0 implies nMid == nTotal here). Count it and name the
	// first 8 so a live log shows exactly which textures were rescued from the
	// unsorted transparent pass.
	if(cls == TAC_OPAQUE && hist.nTotal > 0
	   && (hist.nZero + hist.nFull) == 0){
		s_tacFadedUniform++;
		if(s_tacFadedUniformLogged < 8){
			s_tacFadedUniformLogged++;
			dbglog("texalpha: FADED->OPAQUE (uniform alpha) tex='%s' "
			       "zero=%u full=%u mid=%u total=%u",
			       (tex && tex->name[0]) ? tex->name : "?",
			       (unsigned)hist.nZero, (unsigned)hist.nFull,
			       (unsigned)hist.nMid, (unsigned)hist.nTotal);
		}
	}

	switch(cls){
	case TAC_OPAQUE: s_tacOpaque++; break;
	case TAC_CUTOUT:
		s_tacCutout++;
		if(s_tacAAGate == 1)
			s_tacCutoutAACount++;
		else if(s_tacAAGate == 2)
			s_tacCutoutHiFringe++;
		else if(s_tacAAGate == 3)
			s_tacCutoutVeg++;
		break;
	case TAC_FADED:  s_tacFaded++;  break;
	default:         s_tacUnknown++; break;
	}

	TacLogClassSample(cls, tex, &hist, 0, fmt);
	TacLogWatchSample(cls, tex, &hist, fmt, reason);

	// One-shot names for the classes that change the pipe's blend/alpha path
	// (the first 10 tell us whether the distribution matches the TXD corpus).
	if((cls == TAC_CUTOUT || cls == TAC_FADED) && s_tacNameLog < 10){
		dbglog("texalpha: '%s' -> %s",
		       tex->name[0] ? tex->name : "?",
		       cls == TAC_CUTOUT ? "CUTOUT" : "FADED");
		s_tacNameLog++;
	}

	TacLogSummary();

	return cls;
}

void TexAlpha_ResetCache(void)
{
	memset(s_tacCache, 0, sizeof(s_tacCache));
	memset(s_tacVictim, 0, sizeof(s_tacVictim));
	s_tacUsed = 0;
	s_tacFrame = 0xFFFFFFFF;
	s_tacBudget = 0;
	s_tacOpaque = 0;
	s_tacCutout = 0;
	s_tacCutoutAACount = 0;
	s_tacCutoutHiFringe = 0;
	s_tacAAGate = 0;
	s_tacFaded = 0;
	s_tacFadedUniform = 0;
	s_tacUnknown = 0;
	s_tacUnknownTexNull = 0;
	s_tacFaults = 0;
	s_tacNoAlphaFmt = 0;
	s_tacCutoutVeg = 0;
	s_tacVegHint = 0;
	s_tacWatchLogged = 0;
	memset(s_tacReasons, 0, sizeof(s_tacReasons));
	memset(s_tacReasonLogged, 0, sizeof(s_tacReasonLogged));
	s_tacBudgetSkips = 0;
	s_tacExtOffLogged = 0;
	s_tacNameLog = 0;
	s_tacFadedUniformLogged = 0;
	memset(s_tacClassLogged, 0, sizeof(s_tacClassLogged));
}

void TexAlpha_OnDeviceReset(void)
{
	TexAlpha_ResetCache();
}
