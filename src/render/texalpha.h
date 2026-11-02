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
//                 extraction failed, budget-skipped, or classifier disabled
//                 → pipes fall back to the LEGACY hasAlpha decision
//                 (vertexAlpha || material alpha != 255) with texture alpha
//                 preserved (parity item 2); budget skips are retried on a
//                 later frame. A NULL texture is TAC_OPAQUE, not UNKNOWN.
//
// CACHE
//   4-way set-associative, 1024 sets x 4 ways = 4096 entries keyed by
//   texture->raster. At most 256 new textures are inspected per render frame
//   (RWSRCGLOBAL(renderFrame)); the budget keeps a cold cache from stalling a
//   frame and is independent of database/INI config. Extraction-failure
//   UNKNOWN results are cached together with the failure reason, so a texture
//   that cannot be extracted is not re-probed every frame. Null-texture draws
//   (!texture || !texture->raster) are neither cached nor budgeted: they have
//   no cache key and would otherwise starve real textures out of the budget.
//   No-alpha FORMATS (R5G6B5, X8R8G8B8, ...) are classified one-shot from the
//   format alone with a synthesized all-opaque histogram (nFull = nTotal =
//   w*h) — no LockRect — so their receipts show a real histogram instead of
//   "hist=none" (F5 fix: the five player skin parts read as "classifier never
//   ran" when the format simply has no alpha channel).
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
