# Synthesis — building ordering, pitch-dependent transparency, the CJ ring, and the clouds

**Date:** 2026-10-26
**Build:** `[BUILD] size=2395136 mtime=2026-10-26 01:11:54 built=Oct 26 2026 01:11:06` (matches the reported 2,395,136).
**Log:** `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, 38,598 lines, **01:12:26 → 01:25:52** — i.e. the session
ran ~6 minutes past the 01:19:31 cut used for the tag counts in the brief. Treat the brief's counts as a
prefix of this file, not the whole run.
**Runtime config (log, not the report):** `pipeline=2 buildingPipe=1 vehiclePipe=2 colorFilter=2
colorFilterEnable=1` (`:212`). Xbox pipes are as reported; **the PC colour filter is ON at runtime** even
though the report says `colorFilterEnable=0`. Post-processing is otherwise off — `pf_gate` bits mean
"pass skipped", and this run reports `pipeChain=1 sunShafts=1 dof=1 bloom=1 ca=1 vignette=1 ivGrade=1`
(skipped) throughout (`:356`, `:48957`+).
**Screenshots examined:** 01:13:06 → 01:17:22, 10 frames.

---

## 0. Verdict in one paragraph

**Two mechanisms, one amplifier.** (M1) The sprite/corona flush shield force-writes a full additive block
through the RW cache for **every** sprite batch — not just the corona window — and the compensating
resync sits behind `if(g_coronaBodyDone)`, which is false for most flushes. The world therefore inherits
`ZWRITE=0 + ALPHAFUNC=GREATEREQUAL@0 + additive` from the rw cache itself. (M2) The shadow state
contract forces `ALPHAREF=0 / GREATEREQUAL` and **never binds the shadow raster**, so shadow quads draw
with alpha test disabled and whatever texture the previous consumer left. (A) The building pipe's entry
guard captures that leaked `ZWRITE`, forces TRUE for its own callback, then faithfully re-installs `0` at
exit. M1 explains (1), (2) and (4); M2 explains the black blobs and the "ring"; A is why the leak is
permanent. H2, H5 and H6 are rejected on this build. H1 is rejected as written and replaced by M2.

---

## 1. What the log actually shows (new receipts, not the 2026-10-25 ones)

| Observation | Count | Source |
|---|---:|---|
| `[shadow_install] 0x708300 entry hook installed (tramp=03D60000)` | 1 | `:51` |
| `[shadow_contract] stored …` | **77** | first `:299` |
| stored pre-entry drift = **1**, all three layers agreeing on `blend/src/dst/ref/ztest/zw/fog` | 77/77 | `:299`, `:517`… |
| `[BUILDING] ZWRITE off/desynced (rw=0 drv=0 dev=0)` | **46/66** | `:254`… |
| `[BUILDING] cb camRas=1F361870 2048x1024 != screen 1920x1080` | 18, all 01:13:02–01:13:39 | `:254`…`:2012` |
| `[BldXbox] tac=1 blendOn=0 matA=255 zw=1` | 16/16 logged meshes | `:257`… |
| `[BldTex] null=0 … blend=0` | 24/24 | `:258`… |
| `texalpha: 60773 opaque, 120 cutout, 103 faded, **0 unknown, 0 faults**` | final | `:44675` |
| `[PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=04C26410` | **65/65** | `:228`… |
| `[PedOpaque] contract repaired … rw(ref=0 … vtx=0)` | 128 | `:1`… |
| `[PedRS] ok … blend rw=0 drv=0 dev=0 … aref 0` | 78 | — |
| `[CoronaDraw] pre-force dev blend=0 src=5 dst=6` (REPLACE) | 51/263 sampled | — |
| `[CoronaShield] forced additive … ztest=1->1` | 98, **never** `1->0` | — |
| `[CloudShield] flush batch=sprites forced SRCALPHA/ONE` / `clouds … ONE/ONE` | 1 + 1 (`dbglog_once`) | `:988`, `:2097` |
| `[CloudShield] restored stage-0 raster=…` | **0** | — |
| `CoronaResync` | **0** (bucket 208 is starved — not decisive alone) | — |

The decisive line is `[BUILDING] ZWRITE off/desynced (rw=0 drv=0 dev=0)`: the **rw cache**, the layer the
next consumer believes, has depth writes off on 70 % of building entries. That is not a device drift; a
writer called `RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,FALSE)` and no restore followed.

---

## 2. Hypothesis rulings

### H1 — shadow pass leaves ZWRITE off / binds no texture → **SPLIT: reject the ZWRITE half, accept the texture half**

* **ZWRITE half rejected.** The stored pass is now wrapped (`77` receipts) and enters **clean**
  (`drift=1`, `rw=drv=dev` on all reported fields). The old run had `0` stored receipts; this build has
  77. The shield also forces `ZWRITEENABLE=FALSE` *by design* (correct for a shadow quad).
* **Texture half accepted.** `shadowContractForce` (`src/core/main.cpp:1345-1378`) writes
  `ALPHAREF=0`, `ALPHAFUNC=GREATEREQUAL` and **touches no `TEXTURERASTER`**. `CShadows::RenderStoredShadows`
  does not set the raster itself (disassembly, `docs/plans/2026-10-25-ped-occluder-audit.md` §3.3).
  So the quad rasterises (a) with a texture borrowed from the previous consumer and (b) with **every
  texel passing the alpha test** — the soft falloff that makes a shadow read as a shadow is gone.
  Screenshot 01:13:50 (top-down) and 01:13:21/01:13:27 (interior) show exactly this: pure-black floor
  blobs with hard, blocky, dithered edges, far larger than their casters.
* **The "ring" is not a shadow *radius bug*.** It is the near-camera disc in which stored/static shadows
  exist at all, rendered as black (M2). No permission is needed from a distance constant to get a disc;
  the shadow system is already radius-limited.

### H2 — per-mesh blend/TAC puts walls in the transparent pass → **REJECT**

Every logged Xbox building mesh is `tac=1` (opaque) → `blendOn=0`, material alpha 255, `zw=1`; all 24
texture probes are `null=0 blend=0`; the classifier ends at **0 unknown / 0 faults**. The decision table
(`src/render/buildingPipe.cpp:1874-1910`) only blends on `TAC_FADED`, a real material alpha, or the
legacy fallback — none observed. Walls are in the opaque pass with ZWRITE on. The "ordering" is not
transparent-pass sorting.

### H3 — ZWRITE leak makes far geometry draw over near geometry → **ACCEPT, with the scope corrected**

*The leak is real and cache-level* (46/66 building entries, 128 ped-contract receipts with
`rw(ref=0 vtx=0)`, 78 `[PedRS]` with `blend rw=0`). *But the building pipe itself is not the victim* —
it forces ZWRITE TRUE and draws opaque. The victim is everything drawn **outside a pipe scope**: the sky
and cloud pass at frame start, stored/static shadows, water, grass, skidmarks, `RenderEffects`, and the
sprite flush itself. Outside those scopes nothing writes depth, so the visible order is submission order —
and the submission order of sky → clouds → sprites → world changes with pitch. That is why top-down is
best: at top-down the sky/cloud quads project off-screen and the submission order stops mattering.

### H4 — the ring is a player-anchored sprite/quad → **REJECT**

The darkening is world-space, textured and attached to the ground (screenshots 01:13:50, 01:13:21,
01:13:27); the blobs have sloped, kinked, world-projected edges and a dithered interior, not a
camera-facing rectangle. A light pool / blob shadow would be additive (brighter) and screen-stable under
camera yaw. Neither holds. (The **pale rectangles over water** at 01:13:17 and 01:17:00 *are* sprite
quads — see M1 — but they are not the ring.)

### H5 — `texAlphaClassify` + NULL→`gpWhiteTexture` makes untextured faces render with alpha 0/1 → **REJECT**

`texalpha` ends at `60773 opaque / 120 cutout / 103 faded / **0 unknown / 0 faults**`, `unknown_texnull=0`
(`:44675`); all 24 `[BldTex]` have `null=0` and a real bound pointer; `[PedTex] stage0 resync
cached=00000000 dev=00000000` shows the resync path runs before the first ped. The classifier is no longer
blind (the 2026-10-25 run ended at 152,749 unknown) and it is not producing a wrong class for buildings.

### H6 — the ped resync / `CClouds` texture-raster fix is over-correcting → **REJECT**

The cloud fix captures stage 0 at `CClouds__Render` entry (`src/core/main.cpp:4763`) and restores it by
park-and-set through L1 plus the raw L3 texture (`:4787-4788`, `:5012-5013`). That is the correct shape.
`[CloudShield] restored stage-0 raster=…` never fired (0), which means the captured raster was NULL/white
on every frame — i.e. the capture is clean and **nothing is being restored over**. The ped resync is
likewise a no-op at entry (both NULL). Neither is the regression.

---

## 3. The minimum causal model

### M1 — sprite-flush shield leaks an additive block into the rw cache, and the close never runs

**Where:** `SpriteFlushIndexedDraw_hook` (`src/core/main.cpp:2949-3069`) → `coronaDrawRawShield`
(`src/core/main.cpp:2811-2881`).

**What it does:** for **every** sprite batch with `batchCount>0` (the `force=true` path, `:3012`), it
park-and-sets through **L1 + L2 + raw L3**:

```
ALPHABLENDENABLE = TRUE
SRCBLEND         = SRCALPHA   (ONE/ONE when g_inCloudsRender)
DESTBLEND        = ONE
ZWRITEENABLE     = FALSE
ALPHAFUNC        = GREATEREQUAL
ALPHAREF         = 0
SEPARATEALPHA    = 0
COLORWRITE       = 0xF
BLENDOP          = ADD
```

Because the writes go through `RwRenderStateSet`, the **rw cache** now holds the additive block. The
resync that would undo it lives at `:3028-3068`, inside `if(g_coronaBodyDone)`. `g_coronaBodyDone` is
armed only by the `CCoronas::Render` trampoline (`:4257`) and `CPointLights::Render` (`:3108`). A flush
owned by the cloud pass, the sun reflection, water reflections, a HUD sprite or a marker is *forced*
(`force=true`) but never *closed* — so the leak survives into the next consumer.

**Produces:**
* (1) depth-order errors for everything outside a pipe scope (H3);
* (2) pitch dependence — see H3;
* the pale additive rectangles over the water (01:13:17, 01:17:00) and the rectangular cloud billboards
  (01:16:35, 01:17:22);
* (4) the clouds: the cloud batch is forced `ONE/ONE` *and* `ALPHAFUNC=GREATEREQUAL@0` + `ALPHAREF=0`,
  which is not the cloud pass's native contract (clouds own blend/Z; they do not own alpha test). The sky
  gradient is unaffected because the sky dome is drawn before the sprite flush — hence "sky looks really
  good, clouds half fucked".
* the 46/66 `rw=0` building entries and the 128 `rw(ref=0 vtx=0)` ped-contract entries.

**Evidence it is this writer, not another:** the audit's previous suspect (`CoronaWindow_Close`) now
clears `g_coronaPreValid` (`:3192`) and restores the pre-window block; the only other per-draw writer that
sets ZWRITE=FALSE through **L1** is this shield. `[CoronaDraw]` proves the shield runs on 263 sampled
flushes; `[CloudShield] batch=sprites` proves the *non-corona* branch is taken.

### M2 — the shadow contract canonicalises alpha-test-off and never binds the shadow raster

**Where:** `shadowContractForce` (`src/core/main.cpp:1345-1378`), capture in `ShadowContractState`
(`:1240-1330`), three hooks (`:1511`, `:1532`, `:1534`).

**What it does:** forces `ALPHAFUNC=GREATEREQUAL, ALPHAREF=0`, `ZWRITE=FALSE`, `blend on`, `SRC/INVSRC`,
`FOG=off` on all three layers; it does **not** capture or force `TEXTURERASTER`, `COLORWRITEENABLE`,
`CULLMODE` or the shadow texture.

**Produces:**
* the pure-black floor blobs (01:13:50 top-down, 01:13:21 and 01:13:27 interior);
* the "ring"/disc around CJ that darkens everything — this is the shadow disc, black because the alpha
  shape is destroyed by `ARREF=0` and the texture is whatever was last bound;
* "only inside the ring can I see the peds" — see §4.

### A — the building pipe re-emits the leaked ZWRITE at every exit

**Where:** `src/render/buildingPipe.cpp:3043-3050` (capture + force) and `:3123-3126` (restore).

The entry guard captures `rw/drv/dev` ZWRITE, forces TRUE for the callback, and then writes the **captured
polluted values** back. Once M1 has set `rw=0`, every building callback re-installs `rw=0` for the rest of
the frame. The ped path already latches this (`zwriteWasDesync`, `src/core/main.cpp:1748-1800`); the
building path does not. This is why the leak is permanent rather than one frame.

### Not mechanisms

* **PostFX / colour filter.** Postfx are skipped (`pf_gate`). `colorFilterEnable=1` at runtime is real
  (mode profile overrides the INI) but the PC filter path is not in the four symptoms; flag it as config
  drift, not a cause.
* **The mirror raster live during building callbacks** (18 receipts, first 37 s only) is a startup
  transient; it ends at 01:13:39, before every screenshot in the set except 01:13:06/08/14. Do not chase
  it for this report.
* **`CoronaResync` = 0** is not decisive: bucket 208 collides with `postfx_after`/`ssao_skipped`
  (`docs/plans/2026-10-25-log-divergence-timeline.md` §1). The `rw=0` building receipts are the proof.

---

## 4. "Only inside the ring can I see the peds" — every distance-culled pass

The constraint is a **radius**, so the answer must be one of the passes whose extent is set by distance to
the camera/player, not by the frustum.

| # | Pass | Anchor / metric | Programme | If broken, what it looks like |
|---|---|---|---|---|
| 1 | `CShadows::RenderStoredShadows` `0x708300` (8 stored slots) | near-camera, per-entity | wrapped now, 77 receipts | **Black quads only in a disc around CJ**; ×M2 → the ring |
| 2 | `CShadows::RenderExtraPlayerShadows` `0x70A960` | **CJ** (player only) | wrapped | A dark blob that follows CJ everywhere; the innermost part of the "ring" |
| 3 | `CShadows::RenderStaticShadows` `0x707F40` (+ `0x706AB0` late) | near-camera volume radius | wrapped | Static geometry's shadows only near the player |
| 4 | `CStencilShadows` (via `FX::GetFxQuality_stencil`) | near-camera | `stencilShadows=-1` = vanilla | Stencil shadow volumes only within the shadow radius |
| 5 | Ped/vehicle draw distance (`CPopCycle`, `CStreaming`, `CRenderer` visible list) | camera | vanilla | **Peds exist and draw only within a disc** |
| 6 | `CPlantMgr::Render` grass | camera | vanilla | Grass appears/disappears on a disc edge |
| 7 | `CWaterLevel` water quads | camera | vanilla (parallax off, `[water] gate failed` ×75) | Water tiles fade in on a disc |
| 8 | `CCoronas::Render` / `CPointLights::Render` | camera distance cull | vanilla + M1 shield | Lamp glows/cones only within a disc |
| 9 | `CRenderer` far clip / LOD multiplier | camera | vanilla | Whole-world pop at a radius |

**Verdict.** Only #1–#4 are *dark* radius-limited passes; only #5 is a *ped-visibility* radius. They
share the same centre (camera/CJ) and very similar radii, so the two read as one "ring". The parsimonious
reading is: **the ring is #1/#2 rendered black by M2; the peds are visible there because (a) that is the
near draw band and (b) a black backdrop is the only place where a still-slightly-ghosted ped has contrast
against the pavement.** No culling bug is required, and none is evidenced: `[Cull]` shows
`enter saved=2 forced=2` 127×, `saved=1` 24×, never `saved=3`; `[PedCull] mode=-1 saved=1 forced=1`;
`[PedRS] cull rw=2 drv=2 dev=2` in every sample.

**What would settle it (one measurement).** `stencilShadows=0` (see §5). If the ring and the black blobs
vanish, it is #1–#4 + M2. If the ring persists with shadows off, the ped component is real and #5 needs
its own probe (log `CRenderer`'s visible-entity count and the nearest-ped distance at the boundary).

---

## 5. The single most informative next experiment

**`stencilShadows=0`** (one INI key, no build).

* `FX::GetFxQuality_stencil` (`src/core/main.cpp:2565-2571`) returns `0` when the key is 0, which drives
  the game's shadow quality to 0 — i.e. it removes the entire near-camera shadow system (#1–#4), which is
  the strongest single lever for "is the ring made of shadows".
* **If the ring and the black floor blobs disappear →** M2 is confirmed, and items 1–4 of the plan are the
  fix. Take the same 01:13:50 / 01:13:21 / 01:13:27 shots.
* **If the ring remains →** it is not shadow geometry; the ped-visibility half is real, and the next probe
  is the shadow-raster log described under item 6 (build) rather than more INI keys.

Second step if the ring persists: `pedShadows=0` (isolates #2 alone). Do **not** start with `pedCullMode`,
`fixShadows` or `pedOpaqueContract` — none of them can produce a radius.

**Why not a different key.** `disableClouds=1` tests M1's cloud branch but not the ring. `pedOpaqueContract=0`
tests the ped state, which the log already shows is repaired cleanly. `colorFilterEnable=0` is a config
integrity check, not a diagnosis.

---

## 6. Ordered fix plan

Order is *generator → persister → symptom → amplifier → polish → guard*. Items 1–2 are M1; item 3 is M2;
item 4 is A; 5 is symptom (4); 6 is the regression guard that keeps this from happening again.

### 1. Gate the sprite-flush force to the producers that need it

* **Where:** `src/core/main.cpp:2988-3021` (`SpriteFlushIndexedDraw_hook`), `:3012` in particular.
* **Change:** stop passing `force=true` for every batch. `coronaDrawRawShield` already refuses to act
  outside a window when `force=false` (`:2815`). Keep the force for `g_inCoronasRender` (armed by
  `CCoronas` **and** `CPointLights`, `:3108`/`:4257`) and for `g_inCloudsRender`; for every other
  producer, call with `force=false` so the batch inherits the game's own state.
* **Why:** this is M1's generator. The 51 `[CoronaDraw] blend=0` receipts are genuine, but they come from
  the flush boundary the force was added for; producers outside that set never had a black-quad problem
  and should not be rewritten.
* **Risk:** the original black cone/dot bug returns for `CPointLights` if its window is not armed. The
  `CPointLights__Render_shield_hook` (`:3095`) already re-arms `g_inCoronasRender`, so this should hold —
  verify with a night-time lamp shot.
* **Build:** yes.

### 2. Make the window close unconditional and restore the full shield block

* **Where:** `src/core/main.cpp:3028-3068`.
* **Change:** move the close out of `if(g_coronaBodyDone)`. After **any** forced flush, push the canonical
  world signature **and** the four states the shield writes per draw but the close currently omits:
  `ALPHAFUNC`, `ALPHAREF`, `SEPARATEALPHABLENDENABLE`, `COLORWRITEENABLE` (captured pre-window, or
  canonical `GREATEREQUAL@1` / `0` / `0xF` when no capture exists). Also emit the receipt with
  `dbglog_once` in addition to the throttled tag so a future run cannot read silence as "never ran".
* **Why:** this is M1's persister. Today the close restores
  `VERTEXALPHA/SRC/DST/ZWRITE/ZTEST` only; the alpha-test and colour-write half of the block stays in the
  cache. That is the `rw(ref=0)` in 128 ped-contract receipts.
* **Risk:** a legitimate alpha-test pair in flight at the flush boundary is replaced by the canonical pair
  — the same trade the pipe entry contract already makes. Low.
* **Build:** yes.

### 3. Bind the shadow raster and stop forcing alpha-test-off in the shadow contract

* **Where:** `src/core/main.cpp:1240-1330` (`ShadowContractState` / capture), `:1345-1378`
  (`shadowContractForce`), `:1386-1465` (`shadowContractRestore`).
* **Change:** (a) capture `rwRENDERSTATETEXTURERASTER` + the raw `GetTexture(0)` at entry and re-bind the
  captured raster through `pipeSetTexture` immediately before the pass if it is non-NULL (never bind NULL
  or `gpWhiteTexture` — that produces a solid quad); (b) change the canonical from
  `ALPHAREF=0/GREATEREQUAL` to `ALPHAREF=2/GREATER`, so the shadow texture's transparent texels are
  rejected; (c) add a one-shot `[ShadowTex] raster=%p dev=%p` receipt so the next run proves whether the
  raster is the game's shadow atlas.
* **Why:** M2. `ARREF=0` is what destroys the soft shadow edge; the missing raster bind is what makes the
  remaining texels arbitrary. This is the item that removes the black blobs / the ring.
* **Risk:** if the captured raster is a world texture (not the shadow atlas), the quad will sample it —
  hence (c), and hence the three-layer restore of (a). Medium: this is the only item that changes pixels
  on the main path.
* **Build:** yes.

### 4. Latch the building ZWRITE repair instead of restoring the leak

* **Where:** `src/render/buildingPipe.cpp:3043-3050` (guard), `:3123-3126` (restore).
* **Change:** add a `zwriteWasDesync` local (the ped path's pattern, `src/core/main.cpp:1748-1800`). When
  the entry was desynced, force TRUE and restore **TRUE** at exit; only restore the captured value when
  the entry was clean. Applies to the Xbox, PS2, PBR and GTAIV callbacks that share the guard.
* **Why:** mechanism A. Without this, item 1/2's repair is undone by the next building callback, and the
  `rw=0` receipts persist at ~70 %.
* **Risk:** a decal sublayer inside the callback that legitimately wanted `ZWRITE=0` loses its write for
  the rest of the callback — but the Xbox decal policy re-forces `ZWRITE=0` per mesh
  (`:1925-1943`) and closes it before the next mesh, so the callback baseline is the only thing affected.
  Low.
* **Build:** yes.

### 5. Take the cloud batch out of the shield's alpha/colour-write half

* **Where:** `src/core/main.cpp:3003-3012` (producer-aware pair) and `:2859-2880`
  (`coronaDrawRawShield`'s ALPHAFUNC/ALPHAREF/SEPARATEALPHA/CWR/BLENDOP writes).
* **Change:** when `g_inCloudsRender`, skip the `ALPHAFUNC/ALPHAREF/SEPARATEALPHA/COLORWRITE/BLENDOP`
  writes entirely; keep only `ALPHABLENDENABLE + ONE/ONE + ZWRITE=FALSE`, which is the cloud pass's own
  contract. (Or, cleaner: do not force the cloud batch at all — it already sets its own additive state.)
* **Why:** symptom (4). The sky dome is drawn before the flush and is untouched, which is exactly the
  "sky fine, clouds half fucked" split. The cloud texture relies on its own alpha/edge conditioning;
  forcing `GREATEREQUAL@0` removes it.
* **Risk:** if a cloud batch ever arrives blend-off, the clouds go black. The `[CloudShield]` receipt
  (`:988`, `:2097`) currently shows the sprite branch, not a broken cloud branch — measure before removing
  the blend enable.
* **Build:** yes.

### 6. Guarantee a canonical frame start and add a one-shot world-state receipt

* **Where:** `src/core/main.cpp:3423` (`RenderScene_before` → `CoronaWindow_Close`) and `:7369`
  (`DrawFinalEffects_pf` tail); `pipeApplyWorldBaseline` in `src/render/pipelinecommon.cpp`.
* **Change:** call `pipeApplyWorldBaseline` **after** the sky/cloud pass (i.e. at the end of
  `CClouds__Render_hygiene`, not only at `EffectsExit`), and emit a one-shot
  `[WorldState] end-of-frame zw/alphafunc/alpharef/blend rw/drv/dev + stage0 raster` receipt.
* **Why:** the last line of defence. If any pass leaks again, the world still starts the frame canonical
  and the receipt names the leak in one run instead of six. It also proves item 2 worked: the receipt's
  three layers must agree at `EffectsExit`.
* **Risk:** a per-frame baseline push over a pass that deliberately begins mid-state (the mirror) — the
  baseline already runs at `EffectsExit` today, so this is a placement change, not a new contract. Low.
* **Build:** yes (only the receipt strictly needs it; the reposition is a compile-time change).

---

## 7. What this report does *not* prove

1. **That the shadow quad is sampling a world texture.** The missing-bind inference comes from
   disassembly + the pixel signature, not from a probe. Item 3(c)'s `[ShadowTex]` log is the measurement.
2. **That the ring is 100 % shadow.** §4 ranks it first; `stencilShadows=0` decides it.
3. **That M1 is the only writer of `rw ZWRITE=0`.** The corona flush shield is the only per-draw writer
   that sets it through L1 without a matching restore on the same path, and the timing (first sprite flush
   → building entry) fits — but a counter on the writer (item 6's receipt) would close it.
4. **Postfx.** `colorFilterEnable=1` at runtime contradicts the report and should be reconciled before any
   A/B that assumes "post-processing off".
