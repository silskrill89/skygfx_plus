#pragma once
// Texture alpha classification — per-TEXTURE alpha semantics for the PBR
// alpha-test path (texalpha.cpp).
//
// WHY
//   The PBR shaders alpha-test with GREATER-EQUAL ref 1. A texture whose alpha
//   channel is unused (e.g. a DXT3 solid mesh with every 4-bit alpha nibble 0)
//   decodes tex.a = 0 and the test discards every texel of a SOLID surface
//   ("holes" / solid faces not solid). Genuinely cut-out or faded textures DO
//   need their texture alpha. The distinction is a property of the texture, so
//   it is classified once and cached by RwRaster*.
//
// CLASSES
//   TAC_OPAQUE  — alpha is unused OR not a trustworthy cut-out mask (no-alpha
//                 format, degenerate DXT3/DXT5 blocks, single-value alpha, a
//                 solid texture with a stray alpha region, or a thin-feature
//                 atlas whose AA fringe dominates) → pipes force shader
//                 alpha 1.
//   TAC_CUTOUT  — hard 0/255 mask (both extremes present, <2% intermediate,
//                 1..85% zero) OR an AA-fringe mask (both extremes present,
//                 extremes >= 40% combined, 10..85% zero — the 1-texel AA /
//                 DXT-compression band every real decal/fence/foliage mask
//                 carries) OR a hi-fringe mask (mid-dominant but >=60% of
//                 the intermediate band sits in 192..254 — a hard-mask edge
//                 widened by DXT interpolation/authored AA, NOT a gradient;
//                 F9 tightening, 2026-10-31) → alpha test, blending off,
//                 ZWRITE on, ZTEST on.
//                 §4.2: decals are coverage masks, not transmission
//                 gradients — for a HARD mask the discarded texels write
//                 nothing, so the quad cannot depth-carve its plane over
//                 characters. (SOFT masks — mid-dominant ink, no full-alpha
//                 population — cannot alpha-test under GREATEREQUAL@1; they
//                 land TAC_FADED and the pipe must draw them ZWRITE OFF.
//                 See texalpha.cpp.)
//   TAC_FADED   — >=60% intermediate alpha WITH at least one 0/255 texel
//                 present AND a real gradient (the intermediate band is NOT
//                 near-opaque fringe: <60% of mid sits in 192..254; glass,
//                 shatter, SOFT decal masks — e.g. cleargraf02_LA zero=14%
//                 full=0% mid=85%) → alpha blend + texture alpha. §4.2: a
//                 blended FADED mesh is an OVERLAY — the pipe must draw it
//                 with ZWRITE OFF (ZTEST stays ON) or the quad depth-carves
//                 its plane and characters drawn later are depth-rejected
//                 ("decals render over peds"). A single uniform mid value
//                 (no extremes at all) is OPAQUE.
//                 S6 exception (2026-10-31, black plant/bush silhouettes):
//                 a vegetation-NAMED texture (plant/bush/tree/shrub/veg/
//                 palm/trunk/leaf prefix) whose histogram still has a real
//                 cut-out shape (full > 0, zero share 10..85%) is rescued
//                 from FADED to CUTOUT — a foliage mask's dark RGB fill
//                 blended at partial alpha is the black silhouette. The
//                 hint can never touch a genuine gradient: cleargraf02_LA
//                 (full=0) fails the full>0 requirement.
//   TAC_UNKNOWN — unresolvable (texture != NULL but raster == NULL),
//                 extraction failed, or classifier disabled → pipes fall
//                 back to their TAC_UNKNOWN SAFE DEFAULT (blend only on true
//                 material translucency — never vertex alpha alone — with
//                 texture alpha preserved; see the decision tables in
//                 buildingPipe.cpp / vehiclePipe.cpp). Budget-skip UNKNOWNs
//                 are GONE (build-11 stability fix: first-sight scans always
//                 run, so the same raster can never alternate between this
//                 fallback and its real class across draws). A NULL texture
//                 is TAC_OPAQUE, not UNKNOWN. Build-12: a TAC_UNKNOWN from a
//                 TRANSIENT failure (D3D texture not realized yet, stream-out
//                 mid-scan, lock/leveldesc hiccup) is re-scanned at most
//                 TAC_UNKNOWN_RETRY_MAX (4) times instead of sticking for the
//                 session, so a real texture whose D3D object appears a frame
//                 or two late resolves to its real class; a permanently
//                 unextractable texture probes only those 4 times and then
//                 stays cached. When the classifier is INI-off the first call
//                 logs one "texalpha: DISABLED" receipt naming the cause
//                 (the 2026-11-08 retest read as "classification broken"
//                 with zero receipts when the feature was simply off).
//
// CACHE
//   4-way set-associative, 1024 sets x 4 ways = 4096 entries keyed by
//   texture->raster. STABILITY (build-11 fix): a raster's class is identical
//   on EVERY draw — first-sight scans always run (bounded CPU read; see
//   texalpha.cpp), so no draw of a texture can see the TAC_UNKNOWN fallback
//   while another draw sees the real class (that split was the
//   angle-dependent transparency: the pipes' safe default and the classified
//   path disagree on empty-alpha solids and soft decals). Cache hits promote
//   the touched way to the LAST eviction slot of its set (cyclic LRU), so
//   every-frame textures survive streaming churn and never re-enter the scan
//   path. TAC_CLASSIFY_BUDGET (256 scans/renderFrame tick) is now scan-rate
//   TELEMETRY only (summary "budget=" slots) — it never gates classification.
//   Extraction-failure UNKNOWN results are cached together with the failure
//   reason, so a texture that cannot be extracted is not re-probed every
//   frame (build-12: transient failures — TacReasonIsRetryable — are
//   re-probed at most TAC_UNKNOWN_RETRY_MAX times, then cached for good).
//   Null-texture draws (!texture || !texture->raster) are neither
//   cached nor counted: they have no cache key and nothing to probe.
//   No-alpha FORMATS (R5G6B5, X8R8G8B8, ...) are classified one-shot from the
//   format alone with a synthesized all-opaque histogram (nFull = nTotal =
//   w*h) — no LockRect — so their receipts show a real histogram instead of
//   "hist=none" (F5 fix: the five player skin parts read as "classifier never
//   ran" when the format simply has no alpha channel). Alpha-bearing scans
//   cover DXT1-5, A8R8G8B8/A8B8G8R8, A4R4G4B4 and — build-12, N6 coverage
//   for PS2-converted / modded TXDs — A1R5G5B5 (1-bit alpha), A8 and A8L8;
//   those used to fall to UNSUPPORTED_FORMAT → TAC_UNKNOWN for every draw of
//   a real texture.
//   VERIFY (2026-11-09 hardening lane, no code change): the build-12 bounded
//   UNKNOWN retry cannot loop hot on a permanently-missing/unextractable
//   texture — `tries` is carried across the drop -> re-scan -> re-install
//   cycle via s_tacRetryCarry (texalpha.cpp, one-call static), increments on
//   every re-probe, and the drop gate
//   (`e->cls == TAC_UNKNOWN && e->retryable && e->tries < TAC_UNKNOWN_RETRY_MAX`,
//   texalpha.cpp ~:1466) stops serving re-scans once the bound fills; the
//   entry then sticks cached for the session. TexAlpha_ResetCache runs only
//   via TexAlpha_OnDeviceReset (D3D9 reset) and on tests — never per frame.
//   Interaction with the texdefs N9 fail-closed miss policy (texdefs.h):
//   each miss sentinel is exactly ONE new stable raster (1x1, system-managed
//   pool, same pool class as streamed textures), classified once and cached
//   like any stream texture — no per-frame rescan is introduced.
//   TexAlpha_ResetCache() drops all 4096 entries.
//
// TexAlpha_OnDeviceReset() clears the cache (call on D3D9 device reset).

struct RwTexture;

enum TexAlphaClass {
	TAC_UNKNOWN = 0,
	TAC_OPAQUE  = 1,
	TAC_CUTOUT  = 2,
	TAC_FADED   = 3,
};

// Classify (or return the cached class of) the texture behind `tex`.
// Never faults out: any extraction/scan failure returns TAC_UNKNOWN.
int TexAlpha_Classify(const RwTexture *tex);

// Drop every cached classification (D3D9 device reset / texture reload).
void TexAlpha_OnDeviceReset(void);

// Same cache drop without a device reset, for tests retrying classification
// after a fix. Also resets the diagnostic counters and one-shot reason logs.
void TexAlpha_ResetCache(void);
