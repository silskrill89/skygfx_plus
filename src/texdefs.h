#pragma once
#include "skygfx.h"

// texdefs — unified texture-definition layer (texdb.txt metadata +
// <diffuse>_n normal-map pairing + DK22Pac external plugin lane).
// Replaces src/extras/texdb.cpp, src/render/normalmap.{h,cpp} and
// src/rw/normalmap.cpp. ONE find-callback patch feeds both the TexInfo and
// NmPair layers; BOTH RW texture plugin slots are registered from
// TexDefsPluginAttach().

// --- miss-receipt tokens (log) ---
// The texture-find callback emits three stable `[texdefs]` receipts:
//   [texdefs] miss #N '<name>' txd='<dict>' — ...      one-shot per distinct
//     name (first 32), the NAMING receipt for texture-lookup failures.
//   [texdefs] miss total=<u> distinct=<d> last='<name>' txd='<dict>'
//     throttled rate summary (fires after the 32-name cap fills too).
//   [texdefs] miss-recovered '<req>' -> '<found>' txd='<dict>'
//     fires when TexDefsTryNameVariant turns a failed lookup into a found
//     texture (case/extension mismatch).
//   [texdefs] dictfallback '<name>' txd='<orig>' found-in='<hit>'
//     fires (throttled) when the cross-dict sweep recovers a texture the
//     game's find callback could not reach via its (broken) dict->parent
//     chain — the LOD-slab fix. Semantics are STABLE; the format may gain
//     fields (do not parse positionally past the known columns).
//   [texdefs] defer-retry '<name>' -> HIT via dict '<dict>'   (2026-11-17,
//     ADDITIVE, throttled key "texdefs_deferretry"): a name that was still
//     missing got recovered at a TXD lifecycle event (addTxdSlot /
//     setCurrentTxd) — proof that the containing TXD loaded LATE, i.e. the
//     load-time miss class. Absence of this line for a pending name while
//     resolve keeps logging still-miss means the containing TXD never loads
//     (see the residual noted on TexDefsResolveAtDraw / the deferred-retry
//     block in texdefs.cpp).
//   [texdefs] missbeyond +N '<name>' / missbeyond distinct_unlogged=...    (2026-11-27,
//     W1, ADDITIVE, throttled key "texdefs_missbeyond" — FNV bucket 152,
//     file-written-collision-checked; the dispatch's draft tag
//     "texdefs_misscap" collided with pbrbuilding_frame at bucket 377):
//     distinct-beyond-cap histogram. Names that first miss AFTER the 32
//     `[texdefs] miss #N` slots are full are counted by `miss total` but
//     never named by a `miss #N` line — the (b) case of the white-tree
//     split. `+N` one-shots the first 32 of those names (own bounded table);
//     the throttled line reports distinct_unlogged / overflow_events /
//     seen / total. These names are also now members of the deferred-retry
//     pending set (texdefs.cpp TexDefsDeferRetry), so a late-loading owning
//     dict still applies the hit for them.
//   [texdefs] nulltex #N '<name>' ptr=... site=findcb|getinfo /           (2026-11-27,
//     [texdefs] nulltex total=... distinct=... last='...' site=...          W1,
//     ADDITIVE, throttled key "texdefs_nulltex" — FNV bucket 671, clean):
//     white-tree discriminator. White bare trees are material->texture ==
//     NULL -> gpWhiteTexture, NEVER the grey sentinel, so no miss/policy
//     receipt sees them. site=findcb = TexDefsFindCB actually handed NULL
//     back to the game (stock miss policy does not return NULL, so this
//     line is itself a finding; carries the requested name + pointer).
//     site=getinfo = a consumer read a material whose texture pointer is
//     NULL (the pipes' white-fallback observation point; a NULL texture
//     has no name — logged as "(nil)" + current TXD). First 16 distinct
//     names one-shot, throttled summary past the cap. READ: white-tree
//     name in `nulltex site=findcb` -> looked up, NULL passed through;
//     name in `miss`/`missbeyond` -> looked up, sentinel bound; name in
//     NO receipt while missbeyond counts sit at 0 -> find-CB never called
//     for it (texture absent from TXD — case (a)).
//   [texdefs] resolve '<name>' -> HIT ... / -> still-miss ...   (2026-11-27,
//     RE-KEYED: the dbglog_throttle key moved "texdefs_resolve" ->
//     "texdefs_drawres". RECEIPT BYTES UNCHANGED. The old key shared FNV-1a
//     bucket 577 with postfx.cpp's per-frame "pf_ibl" and was starved to
//     exactly ONE line across the whole on-disk log history (a single
//     `still-miss`, zero HITs) — recovery was running but unobservable.
//     Bucket 489 is unique across every in-tree throttle tag).
//   [texdefs] miss-policy lever texDefsMissPolicy=%d -> ...      (2026-11-27,
//     ADDITIVE, one-shot at TexDefsLoadDatabase: the N9 master lever finally
//     wired — see TexDefsMissPolicyEnable below. Absent INI key == 1 ==
//     unchanged behaviour).
//   [BldTex] first-slab tex=%p kind=%d name='%s'                 (2026-11-27,
//   [BldTex] slab-bind world=%u last='%s' ptr=%p                 ADDITIVE,
//     one-shot + throttled key "bld_slabbind", bucket 125): buildingPipe's
//     bldPolicyTexture names the FIRST WORLD-class policy sentinel that
//     SURVIVED TexDefsResolveAtDraw — the (a)-vs-(b) slab discriminator. Its
//     absence beside the existing `[BldTex-FIRST] tex='(null)'` line (the (b)
//     observation point, buildingPipe_texDiag) is the class-(b) signature.
//     Neither line is produced by a session where draw-time recovery works.
//
// --- cross-dict recovery policy (N8) ---
// On a find miss the game's CTxdStore pool is searched via the game's own
// RwTexDictionaryFindNamedTexture (case-insensitive). First exact hit wins; a
// hit whose TXD name is a substring of the requesting TXD name is preferred
// (LOD dicts embed their parent's name, e.g. lod2lae1 -> lae1). The hit TXD
// is ALWAYS logged so a mis-recovery (same-name/different-texture collision)
// is diagnosable. A (name -> slot) hit is pinned and re-resolved cheaply; the
// pin/miss cache is invalidated on TXD push (storeTxdName). Genuinely-absent
// names used to return NULL (consumer falls back to the white texture); the
// N9 fail-closed miss policy below now intercepts them BEFORE the NULL is
// handed on.

// --- lifecycle ---
int  TexDefsPluginAttach(void);       // myPluginAttach chain (0x53D903). SINGLE RW plugin registry (EDED + PDS atomic slots, texdb + NmPair texture slots). Before any texture/atomic exists. Idempotent. Always returns 1.
void TexDefsInit(void);               // DllMain: 2 txd-name hooks + the ONE find-CB patch. Idempotent.
void TexDefsLoadDatabase(void);       // InitialiseGame_hook: Mobile_details.txd + readTxt + roadsign entry + faketexinfo defaults.
void TexDefsShutdown(void);           // DLL_PROCESS_DETACH: free strdup'd names + delete TexInfo map entries. No RW/D3D9 calls.

// --- external DK22Pac offline plugin lane (enableNormalMaps), deferred until RW engine is up ---
void TexDefsExternalInit(void);        // DllMain one-shot attempt (old normalmap_init)
void TexDefsTryAttachExternal(void);   // per-frame poll from RenderScene_hook (old normalmap_tryAttach)
void TexDefsExternalShutdown(void);    // old normalmap_shutdown

// --- read side (pipe callbacks) ---
TexInfo *TexDefsGetInfo(RwTexture *tex);                      // NEVER NULL — falls back to the internal faketexinfo
TexInfo *TexDefsFindInfo(char *name);                         // map lookup, may return NULL
void     TexDefsAssignInfo(RwTexture *tex, TexInfo *info);    // writes the plugin slot (replaces main.cpp's direct texdbOffset write)
IDirect3DTexture9 *TexDefsGetNormalMap(RwTexture *diffuse, bool *outHasAlpha);
bool     TexDefsVehicleAllowed(const void *atomic);

// --- D3D texture extraction (the ONE 0xB4E9E0 extension-offset formula) ---
// Failure stage codes for callers that keep reason counters (texalpha.cpp).
enum TexDefsD3DWhy {
	TEXDEFS_D3D_WHY_OK = 0,         // success
	TEXDEFS_D3D_WHY_NO_TEXTURE,     // tex == NULL
	TEXDEFS_D3D_WHY_NO_RASTER,      // tex->raster == NULL
	TEXDEFS_D3D_WHY_BAD_EXT_OFFSET, // 0xB4E9E0 word ==0 / >0x100000 / unaligned
	TEXDEFS_D3D_WHY_NO_D3D_TEX,     // ext+8 flag clear + implausible ptr, or ext+0 NULL
	TEXDEFS_D3D_WHY_BAD_EXT_PTR     // ext+8 flag set but ext+0 implausible
};

// THE accessor for the game RwD3D9SetTexture extension-offset extraction
// (0x7FDCD0: ext = raster + *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*,
// ext+8 = valid byte flag). Borrowed pointer: NO AddRef, caller must NOT
// Release it. Accepts when EITHER the ext+8 flag is set OR ext+0 is a
// plausible pointer (the flag can be stale). outWhy (optional) receives a
// TexDefsD3DWhy; outExtOff (optional) receives the raw offset word ONLY when
// it is actually read (callers pre-set 0xFFFFFFFF as "unread"). The optional
// trailing out-params exist so texalpha's reason counters fire on exactly the
// conditions they always did; plain form is TexDefsGetD3DTexture(tex).
IDirect3DTexture9 *TexDefsGetD3DTexture(RwTexture *tex, int *outWhy = NULL, uint32 *outExtOff = NULL);

// --- N9 fail-closed miss policy (2026-11-09) ---
// A texture lookup that STILL fails after the pin/variant/sweep recovery
// (N6/N8) never returns NULL: TexDefsFindCB binds a 1x1 SENTINEL texture
// classified by the CURRENT TXD, so the draw a missing texture produces is
// never again the full-size fallback the user sees (giant white/grey slabs,
// black bars, the bright red world-anchored fallback marker). Policy table:
//
//   class=world — currentTxdName is unresolved ("") or "lod*" (ci): the
//     LOD-parent-chain dictionaries every observed miss binds to (log:
//     lod2_las, lod2lae1, lodhuge_lan2, lod_countn2, LODjetty_SFX,
//     LOD_dem_SFXRF, lod_boxhses_SFSX, LODtnsfn).
//     action=sentinel: 1x1 OPAQUE dim grey (0x606060, alpha FF, tex name
//     "skygfx_missgrey"). World geometry KEEPS DRAWING with a neutral dim
//     sample — a data-side gap must not hole the world (erased geometry is
//     its own #1 complaint class); the alpha-FF texel also survives the
//     building pipes' GREATEREQUAL@1 alpha test, where a transparent texel
//     would erase LOD buildings outright.
//   class=sprite — any other current TXD (effects/particles/markers; also
//     the grass_pc foliage dict). action=skip: 1x1 FULLY TRANSPARENT magenta
//     (FF 00 FF 00, tex name "skygfx_missclear"). Alpha-blended/alpha-tested
//     decoration — screen sprites, particles, the world-anchored marker
//     quad — renders invisible; a consumer that draws it unblended shows RGB
//     magenta: loud but bounded and diagnosable, never white/black. The
//     R/B symmetry is deliberate: it stays magenta under either BGRA/RGBA
//     byte order of an RW 8888 raster.
//   Sentinel raster/texture creation failed -> legacy NULL return (the
//     consumer white fallback), announced in the receipt line.
//
// Receipts: the N6/N8 tokens ([texdefs] miss, miss-recovered, dictfallback)
// are byte-stable and unchanged. NEW line on its OWN throttle key
// "texdefs_misspolicy" (2 s via dbglog_throttle, g_diagLevel-aware):
//   [texdefs] miss-policy '<name>' class=<world|sprite> action=<sentinel|skip> txd='<dict>' total=<N>
//   total pairs with the concurrent [texdefs] miss counter. A sentinel-
//   creation failure logs the same tag with "INACTIVE" instead of a class.
//
// Scoping rules (do not relax):
//   * The policy keys on LOOKUP-FAILURE ONLY. All deliberate gpWhiteTexture
//     resolves (NULL-extraction consumers, pipeSetTexture's NULL -> white in
//     skygfx.h, the skin/buildingPipe/vehiclePipe fallback binds for a NULL
//     MATERIAL texture) are UNTOUCHED — they never represent a find-CB miss
//     and TexDefsFindCB's success path (tex != NULL) is unchanged.
//   * Sentinels never attach TexInfo or NmPair metadata: TexDefsFindCB
//     returns them BEFORE the attach block — no consumer plugin-slot read,
//     no pairing probe on a sentinel name.
//   * Sentinels are created lazily ONCE (the find CB only runs with RW up),
//     process-lifetime, never freed; RwTexture::name is an inline char[32]
//     (rwcore.h) and is filled directly.
//   * Accepted risk (documented): the find CB is also reached by probe-style
//     callers (RwTextureRead searches loaded dicts first) — a missing
//     OPTIONAL texture now resolves to the sentinel at probe time. The
//     receipt names every producer; retune the classification or disable via
//     TexDefsMissPolicyEnable if a probe class misbehaves.
//
// Policy controls + consumer handoff:
// N9 MISS-POLICY LEVER — REFACTORED 2026-11-27 (faithful-descendant doctrine):
//   ON  (default 1): TexDefsFindCB returns NULL on a miss so the game keeps
//        its OWN texture streaming / recovery path alive; our pipes apply the
//        grey sentinel at DRAW time (bldPolicyTexture / vehPolicyTexture /
//        SkinStage0Resync / render_callbacks). With a Vanilla pipe (no custom
//        pipe attached) the game binds NULL exactly as before the policy
//        existed — turning our additions OFF restores stock behaviour.
//   OFF (0): legacy NULL return stands (game binds NULL -> white fallback).
// Read once from TexDefsLoadDatabase with the config lane's own linb::ini
// parser. Full note in texdefs.cpp above TexDefsReadMissPolicyLever.
void TexDefsMissPolicyEnable(bool on);       // 0/1 — driven by texDefsMissPolicy; default ON
bool TexDefsMissPolicyIsEnabled(void);
RwTexture *TexDefsMissSentinelWorld(void);   // 1x1 opaque dim grey, lazily created
RwTexture *TexDefsMissSentinelSprite(void);  // 1x1 transparent magenta
// CONSUMER-HANDOFF QUERY (2026-11-09, wave "N9 consumer honors"): the only
// way a consumer can tell "this pointer is POLICY OUTPUT (a failed lookup)"
// from "this material legitimately has NO texture" (NULL) or "this is a real
// texture". Identity test against the two sentinels — NO state, NO
// allocation: the TexDefsMissSentinel* accessors above CREATE their raster
// on demand, which a consumer on the draw path must never do.
enum TexDefsMissTexKind {
	TEXDEFS_MISS_TEX_NONE = 0,	// not policy output (a real texture, or NULL)
	TEXDEFS_MISS_TEX_WORLD,		// the dim-grey world sentinel  (action=sentinel)
	TEXDEFS_MISS_TEX_SPRITE		// the transparent-magenta one   (action=skip)
};
int TexDefsMissTextureKind(const RwTexture *tex);   // TEXDEFS_MISS_TEX_* — never creates
// DRAW-TIME RESOLUTION (2026-11-16, root-cause fix for the untextured world):
// when `tex` is a policy sentinel carrying its original requested name, retry
// the lookup (game find -> case/variant -> CTxdStore sweep) — the LOD DFF that
// first asked streams in BEFORE its parent full-detail TXD loads, so the
// load-time miss is usually recoverable by draw time. Returns the real
// texture when it now resolves, otherwise `tex` unchanged (rate-limited to
// one full retry per name per second; real textures early-out untouched).
RwTexture *TexDefsResolveAtDraw(RwTexture *tex);
// 2026-11-17 FIX on that path: the CTxdStore sweep now runs as a RECOVERY
// source for a still-missing name (it used to be reachable only after a
// game-find/variant HIT, to pin that hit — so a name whose parent TXD
// loaded late could never recover: 0 HIT / 17 still-miss in the
// 02:17-02:20 log). Complemented by the deferred retry at TXD lifecycle
// (texdefs.cpp: TexDefsDeferRetry from storeTxdName + setCurrentTxdName),
// which probes the newly-current dictionary (and, throttled, the whole
// pool) for every pending name — receipt `[texdefs] defer-retry ...`. Both
// are read-only against the game (pool reads + RwTexDictionaryFindNamed)
// and never allocate on the draw path beyond the existing per-name state.
// Residual: a TXD the game itself never requests still cannot be found
// from this layer (no name->TXD map, no safe load-forcing API) — those
// names keep the policy sentinel until a parent-link repair / load-force
// phase.
// W1 (2026-11-27) resolution of this residual — the never-load preload
// phase was AUDITED and deliberately NOT shipped: the only in-repo TXD
// load is a boot-time file-path RwTexDictionaryStreamRead (no by-name
// loader exists, so a preload would have no target and would risk
// re-entering the addTxdSlot/setCurrentTxd hooks from inside themselves).
// The two additive halves that WERE shipped: (1) TexDefsDeferRetry's
// pending set now includes the beyond-cap miss names, so the
// owning-dict-lates-loads hit application covers them; (2) the
// `texdefs_missbeyond` histogram receipt counts distinct names the
// 32-receipt cap hides (see the receipt block at the top of this file).
// Sprite-class sentinel -> world-class sentinel, PRESERVING the requested
// name (so the remap stays resolvable). Real/world textures pass through.
RwTexture *TexDefsRemapToWorld(RwTexture *tex);
//
// CONSUMER HONORS — status (2026-11-09):
//   HONORED  particles_gpu.cpp (sprite-class consumer: a miss-classified
//            sprite is SKIPPED, never drawn as an untextured white quad) and
//            vehiclePipe.cpp (world-geometry consumer: a miss-classified
//            diffuse is re-mapped to the world action — the dim-grey 1x1,
//            never the transparent-magenta sprite sentinel, which would
//            erase or magenta a vehicle body).
//   PENDING  buildingPipe.cpp bldBindDiffuse3L (:1354) and
//            buildingPipe_bindTexStage (:1467) — resolved = tex ? tex :
//            gpWhiteTexture; a sprite-class sentinel there would ERASE a LOD
//            building (the transparent texel fails the GREATEREQUAL@1 alpha
//            test), i.e. the world-holing the policy exists to prevent.
//            peds/skin.cpp (:927) and core/render_callbacks.cpp (:445) — the
//            NULL-texture recovery walk (recovered ? recovered :
//            gpWhiteTexture). NOTE: the core/main.cpp recovery-walk /
//            corona-producer sites cited by earlier revisions of this
//            contract NO LONGER EXIST (main.cpp was rewritten); its only
//            gpWhiteTexture use is a park raster (main.cpp:2627), not a
//            miss fallback. grasssys.cpp (:360) needs no change — it binds
//            the game's OWN texture, so a sentinel flows through as the
//            policy intends (grass_pc is sprite class: the cards vanish).
// NOTE for every consumer: the discrimination is by POINTER IDENTITY, never
// by "is it NULL". A NULL material texture keeps gpWhiteTexture EXACTLY as
// before the policy (legitimate "no diffuse map"); only a real sentinel is
// re-mapped.

// --- per-atomic defined vertex shader (EDED plugin slot; moved from extendedplg.cpp) ---
uint8 TexDefsGetDefinedShader(RpAtomic *atomic);            // was GetDefinedShader. Degrades to DEFAULT when the slot is not registered.
void  TexDefsSetDefinedShader(RpAtomic *atomic, uint8 s);   // was SetDefinedShader. No-op when the slot is not registered.
