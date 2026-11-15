# N-registry root-cause recon + fix decomposition (N1–N6)

Date: 2026-11-07 (recon executed 2026-11-08) · Lane: workload-assessment (READ-ONLY recon, this doc is the only write)
Scope: open defect registry from `.serena/memories/wave-14-record.md` + `docs/plans/2026-11-02-systematic-roadmap.md`
+ `PLAN.md`/`TODO.md` queue. N7 (FAM-W bounded whiteout) is KNOWN/ACCEPTED and skipped per dispatch.

**Method.** Graph recon via codebase-memory-mcp (project `E-dev-dave-skygfx_plus_expIV`, branch
`very-experimental`, HEAD `5cc5d25`) + wave records + on-disk grep/sed spot-checks (10+ refs, §6).
All hypotheses carry file:line or wave-record evidence; every contradiction is cited.

**⚠ Moving-tree caveat (first-class finding).** The parallel comparator lane ("measurement support",
per dispatch) is editing **`src/render/pipelinecommon.cpp`** and **`src/render/renderstate_monitor.cpp`**
RIGHT NOW: `git status --porcelain` shows both `M`, and mid-recon `pipeExitAlphaMode` moved
2095→2113 (+18 ln) while `RSMon_Sample` moved 510→598 (+88 ln). Line numbers below are pinned
**as-of 2026-11-08 recon**; treat function names + receipt strings as the durable anchors and
re-verify line numbers before quoting in a fix lane. The two `M` files are the **contended scope**:
every leaf that writes them must serialize after the comparator lane lands (§5).

---

## 1. Registry recap (evidence chain)

| # | Item | Registry evidence | Status |
|---|------|-------------------|--------|
| N1 | `[PipeAlpha]` L2/L3 faithful-restore replay, 81× `enable L2=1 L3=0` | wave-14-record.md:38, :44; roadmap:165 | OPEN |
| N2 | zfunc=8 dead-depth 106/127, drift source unidentified | wave-14-record.md:39; roadmap:166 | OPEN |
| N3 | FAM-Q giant milky sprites | wave-14-record.md:40; roadmap:167; TODO.md:141 | OPEN (measure after A ships) |
| N4 | FAM-G grass corruption | wave-14-record.md:41; roadmap:168; TODO.md:143 | OPEN |
| N5 | FAM-N near-black palms | wave-14-record.md:41; roadmap:169; TODO.md:145 | OPEN (own sweep) |
| N6 | FAM-T missing textures (minor) | wave-14-record.md:42; roadmap:170; TODO.md:147 | OPEN (minor) |
| N7 | FAM-W bounded whiteout | wave-14-record.md:42; roadmap:171 | KNOWN/ACCEPTED — skipped |

Shared root, verbatim: **"faithful restore replays the capture that IS the leak"** (wave-14-record.md:44;
roadmap:160-161). Family-A discriminator still open: if test12 shot 7 survives the A fixes ⇒ hypothesis
A-2 ⇒ N1 rule (wave-14-record.md:45; roadmap:146-147). Evidence base for all specimens:
`docs/baseline/2026-11-03-test12/findings.md` (shots table :72-83, family map :155-159) — build 11 ran
PRE-fix (0 `[PedZW]`/`[BldZW]` receipts, wave-14-record.md:18-19), so all counts below are pre-fix
A-side measurements. The CJ A/B/C fixes are **FIXED ON DISK, unbuilt** (roadmap:120-143) — the build
gate in §5 Wave 0 precedes all measurement gates.

---

## 2. N1 — `[PipeAlpha]` L2/L3 faithful-restore replay (81×)

### 2.1 Render-path location

| Surface | File :: function (line as-of recon) |
|---|---|
| Entry capture | `src/render/pipelinecommon.cpp :: pipeEnterAlphaMode()` :1889 (captures L2+L3 `alphaBlendEnable(+Dev)` etc. behind `PAACAP_*` bits — `src/skygfx.h` :1482-1484, `PipeAlphaState` :1524-1530) |
| Faithful restore + sticky rule | `src/render/pipelinecommon.cpp :: pipeExitAlphaMode()` :2113; sticky block :2162-2186 ("sticky rule (F1 extension): a SPLIT capture is never replayed"); parity-6 receipt :2281 `[PipeAlpha] faithful restore (parity 6): … enable L2=%u L3=%u (derived %u)` |
| Per-span consumers | `src/render/buildingPipe.cpp :: pipeForceAlphaBlock()` :557 (+ enter/exit pair :3536/:3659); `src/render/vehiclePipe.cpp` :4091/:4208 + clobber-scope audit :4209-4233; `src/render/envmap.cpp:720` (deliberately not using pipeForceAlphaBlock) |
| Attribution history | `docs/plans/2026-10-25-log-divergence-timeline.md` §5 #1 (`:253-267`) + #2 (`:270-277`); `docs/plans/2026-10-26-sdk-compare-building.md:23` (build-era `[PipeAlpha] enable L2=1 L3=0` receipts) |

**Key recon delta vs the registry text:** the replay-stop is **already on disk** — `pipeExitAlphaMode`'s
sticky rule (pipelinecommon.cpp:2162-2186) refuses to replay split captures (a split ends blend OFF /
test TRUE / op ADD on both layers) and logs `[PipeAlpha] sticky exit: split capture NOT REPLAYED`.
So N1 as registered ("extend sticky rule to alpha + cull exits", TODO.md:135-137) is largely LANDED-ON-DISK
by the CJ wave; what stays OPEN is (a) the **source** of the split capture and (b) the **acceptance-probe
semantics** (§2.3 H4).

### 2.2 Root-cause hypotheses (ranked)

- **H1 — Corona/sprite shield raw-write family + incomplete frame-boundary close.** `coronaDrawRawShield`
  writes `ZWRITE=FALSE` through L1+L2+L3 per-draw (main.cpp shield block, comment map :3286-3313,
  `CoronaWindow_Close` :4468) and the corona blend force writes `ALPHABLENDENABLE` park-FALSE→TRUE raw
  through L1+L2+L3 (main.cpp :3476-3510). **Supporting:** log-divergence-timeline.md:253-258 names this
  the "most likely raw writer" for the cache-level ZWRITE class; :267 names the building exit
  (`buildingPipe.cpp:3067-3070`, restore-each-layer-to-its-own-capture) the **"perpetuator, not
  generator"** — "once the split exists it is faithfully re-created every callback", i.e. the exact
  N1 mechanism. `CoronaWindow_Close` restores only `ALPHAFUNC/ALPHAREF/ALPHABLENDENABLE` (timeline:262-265)
  — anything else the shield parked survives the safety close. **Contradicting:** the flush shield
  captures 9 states incl. `blendEn` and replays them at close (`SpriteFlushState`, main.cpp:3547-3557),
  so when the window closes normally the blend split should heal; the L2=1/L3=0 *direction* (caches ON,
  device OFF) needs an L3-only `ALPHABLENDENABLE=FALSE` writer, which the shield block does not contain
  (it parks FALSE→TRUE, ending TRUE).
- **H2 — `RestoreRawGeomStates` drift-promotion engine.** `src/render/postfx_utils.cpp:190
  SaveRawGeomStates` / `:246 RestoreRawGeomStates` captures the **device** and park-and-sets that captured
  value into L1+L2 — any device-only drift at save time is promoted into the caches.
  **Supporting:** log-divergence-timeline.md:270-277 names it the "concrete propagation engine" for the
  device-vs-caches split class (the same split class N1's capture records). A restore that promotes a
  divergent capture is literally "the restore replays the capture that IS the leak".
  **Contradicting:** promotion is uniform (same value into L1/L2 then L3) — it manufactures *full* drift,
  not L2≠L3 splits; a second L3-only writer must act between the promotion and the next pipe entry.
- **H3 — Mid-span raw re-asserts in the vehicle Env cb / effects phase.** vehiclePipe.cpp:4209-4233
  documents "entry forces … push the DEVICE unconditionally" mid-cb and repairs with "faithful L3
  completion" (:4225) at exit. **Supporting:** the in-code audit names the clobber class explicitly;
  envmap.cpp:720 documents a deliberate raw path avoiding `pipeForceAlphaBlock`.
  **Contradicting:** the exit-side L3 completion (vehiclePipe.cpp:4225-4233) pins device==cache at every
  vehicle exit, so a surviving split must originate after the *last* pipe exit of a frame (sprite/corona/
  postfx phase) — which re-ranks H1/H2 above H3.
- **H4 — Acceptance-probe semantics gap (measurement, not mechanism).** The parity-6 receipt
  (pipelinecommon.cpp:2281) fires whenever `faithfulDiverged` — which includes `alphaBlendEnable !=
  alphaBlendEnableDev`, i.e. **the split itself** — so with the sticky rule landed the receipt still
  prints on every split capture, just no longer replaying it. The registered probe "count → 0"
  (roadmap:165) therefore conflates "replayed leak" (fixed) with "split captured" (source unfixed).
  **Supporting:** verbatim receipt logic (pipelinecommon.cpp:2272-2290). **Contradicting:** none —
  this is a definitional defect in the probe; the fix lane must either split the receipt into
  `sticky-resolved` vs `replayed` events or root-kill the split (H1/H2).
- **H5 — 85%-blind classifier as confounder.** 152,749/~181k texalpha probes unknown (85%) in the
  reference run (timeline:182) — opacity-driven `vtxAlpha` derived fallbacks in the same spans make the
  *derived* comparison noisy. **Contradicting:** the 81× count is of captured L2/L3 disagreement, which
  is independent of the classifier.

### 2.3 Octree decomposition (shared with N2 — see §3.3)

N1+N2 are ONE subtree (S1, shared CJ root) — the decomposition lives in §3.3. N1-owned leaves:
S1-L1 (pipelinecommon.cpp receipts+attribution), S1-L2 (renderstate_monitor.cpp verdicts),
S1-L4 (postfx_utils.cpp promotion engine), S1-L6a/b (exit perpetuator replay in building/vehicle pipes),
plus the S1-L3 main.cpp shield leaf's CoronaWindow_Close completeness.

---

## 3. N2 — zfunc=8 dead-depth (106/127)

### 3.1 Render-path location

| Surface | File :: function (line as-of recon) |
|---|---|
| Repair chokepoint | `src/render/pipelinecommon.cpp :: pipeEnforceDepthContract()` :1349 (comment :1321-1347 documents the failure class: "a raw L3 write parks ZENABLE=0 / ZFUNC=ALWAYS behind a rw cache still claiming ztest=1"); park-and-set trio + `[zfix]` receipt :1377-1391 (`[zfix] pipe entry depth drift repaired -> ZENABLE=TRUE ZFUNC=LESSEQUAL: rw ztest=%u drv zen=%u zfunc=%u dev zen=%u zfunc=%u`) |
| Call site | `pipeEnterCullMode()` pipelinecommon.cpp:1588 → `pipeEnforceDepthContract()` at :1628-1629 ("World depth contract rides the same entry") — i.e. only draws passing the pipe/cull entry get repaired |
| Grass twin | `src/render/grasssys.cpp :: GrassEnforceDepthContract()` :189 (reference idiom, called from `main.cpp :: grassRenderCallback()` :820), `GrassResetShaderLatches()` :70, `[grass_zfix]` receipt :324-325 |
| Monitor | `src/render/renderstate_monitor.cpp :: RSMon_Sample()` :598 (persistent-vs-transient verdict; header :45 + :78 explicitly frame the `[PipeAlpha]`/`[zfix]` repair-receipt class as the N1/N2 problem) |
| 2D-state writers | `src/core/main.cpp` sprite/corona/flush shield block :3286-3634 (`SpriteFlushState` :3547-3557, `spriteFlushShieldCapture` :3560, `spriteFlushShieldClose` :3634); font glyph flush hook `CloudRegionFlush_7273D0_hook` :4333 + PERF R5 fast path :4376-4404 |

**Receipt semantics:** the 106/127 statistic is repair-counts under a 2 s throttle
(`dbglog_throttle("zfix")`, pipelinecommon.cpp:1389) — roadmap:166 itself notes "~120 frames between
repairs run with dead depth testing". True drift-episode count is unknown until the comparator lane's
measurement lands; treat 106/127 as a lower bound.

### 3.2 Root-cause hypotheses (ranked)

- **H1 — 2D/Im2D phase parks the full depth trio and the sprite flush shield cannot restore it.**
  The full-drift signature `rw ztest=0 drv zen=0 zfunc=8 dev zen=0 zfunc=8` (roadmap:166; test12 #7
  findings:78 `[zfix] … rw ztest=0 drv zen=0 zfunc=8 dev zen=0 zfunc=8`) means **all three layers agree
  on the 2D state** — someone parked ZTEST off + ZFUNC ALWAYS through L1+L2+L3 and never restored.
  The `SpriteFlushState` capture struct (main.cpp:3547-3557) holds NINE states —
  `blendEn/srcBlend/dstBlend/zwrite/alphaFunc/alphaRef/colorWrite/sepAlpha/blendOp` + L2 + rw mirrors —
  and **no `zen`/`zfunc` pair at all**. A flush window that runs Im2D draws (whose idiom is exactly
  ZTEST-off/ALWAYS) and closes via `spriteFlushShieldClose` restores the nine, leaving the 2D depth
  state live for every later draw until a pipe entry's `[zfix]` catches it.
  **Supporting:** struct field list verbatim; the same "leftover 2D/loading state: ZENABLE=0, ZFUNC=8
  (ALWAYS)" fingerprint in timeline:149 (first frame) and the recurring `[PedOpaque] pre-entry … zfunc=8`
  receipts (timeline:163-169, ped-angle-disappearance.md:271-277).
  **Contradicting:** RW's own Im2D begin/end pairs state save/restore internally; the leak needs the
  hooks around the flush (`SpriteFlushIndexedDraw_hook`, main.cpp:3287, flush sites 0x70CF62/0x70CF9B)
  to bypass that pairing — not yet proven per-draw.
- **H2 — Font-flush PERF R5 fast-path blind spot.** `CloudRegionFlush_7273D0_hook` (main.cpp:4333;
  0x7273D0 = `CFont::RenderFontBuffer` glyph flush, main.cpp:4336-4361) skips the park-and-set + restore
  when the entry blend trio is canonical — the probe reads **only** `ALPHABLENDENABLE/SRCBLEND/DESTBLEND`
  (main.cpp:4385-4404, "no state touched") and never looks at `ZENABLE/ZFUNC/ZWRITE`. A glyph flush that
  inherits (and its Im2D draw re-asserts) zfunc=8 exits with the leak un-repaired and un-restored.
  **Supporting:** verbatim code read (spot-check §6.4); PERF R5 is a *performance* patch
  (wave-13-record.md:79 "R5 (font pin fast path, skip park-and-set when entry==canonical)") — its
  canonical test was written for blend, not depth.
  **Contradicting:** the fast path does not itself write zfunc=8; it only fails to repair/restore, so
  H2 composes with (and is ranked after) H1 as the surviving-window mechanism.
- **H3 — PostFX quad bracket incomplete depth restore.** PostFX End pins the blend trio raw
  (`pipeL2ParkSet(D3DRS_ALPHABLENDENABLE/…)` + `pipeL3Set`, postfx.cpp:5203-5210 region) and the Begin/End
  contract restores "the pre-bracket device values saved by Begin" (postfx.cpp:3728, :3841-3846) — but
  the save/restore plumbing (`SaveRawGeomStates`, postfx_utils.cpp:190) is device-sourced and promotes
  drift (timeline:270-277). Fullscreen quads run with depth-off/ALWAYS by design; a bracket that restores
  blend but not ZENABLE/ZFUNC leaves the 2D depth state behind.
  **Supporting:** water draw-state probe in water-veil-diagnosis.md:406-410 expects `zfunc=4` and logs
  for drift — zfunc drift is observed at consumer draws; test12 #5 log window (findings:76) shows
  `non-screen raster LIVE 512×512 building cb` + `cullfix` + `[BldZW]` in the same second as the grass
  over-water artifact. **Contradicting:** no direct log proof that a PostFX End preceded the sampled
  `[zfix]` windows (2 s throttle destroys ordering) — needs the comparator lane's measurement.
- **H4 — Skidmarks/other draws bypassing the pipe entry.** `CSkidmarks__Render` runs with no
  `pipeEnterAlphaMode` in front (buildingPipe.cpp:528 comment) and likewise no `pipeEnforceDepthContract`
  (which rides `pipeEnterCullMode` only, pipelinecommon.cpp:1628-1629) — any non-pipe consumer inherits
  whatever 2D state persists. **Supporting:** the comment verbatim. **Contradicting:** skidmarks is a
  narrow draw class; it cannot explain 106 repairs across mixed scenes alone.
- **H5 — Family-B residual (already fixed on disk, unbuilt).** Family B's ZTEST/ZFUNC contracts +
  sticky depth force the trio at ped entry and leave canonical depth at exit (roadmap:137 row B) — if
  the CJ build ships and `[zfix]` recurrence drops but not to 0, the residual split of "re-created
  between entries" (roadmap:166) is H1/H2. **Contradicting:** the registry already says the leak is
  "still re-created between entries" with B's forces in place, so B is not sufficient.

### 3.3 S1 = N1+N2 one subtree — octree decomposition (≤1-file leaves)

```
S1 (N1+N2, shared CJ root "restore replays the capture that IS the leak")
├── S1-L1  src/render/pipelinecommon.cpp        [CONTENDED — after comparator lane]
│          parity-6 receipt split (sticky-resolved vs replayed) + [zfix] drift-pattern
│          class (full-drift vs layer-split) + last-span attribution tag
├── S1-L2  src/render/renderstate_monitor.cpp   [CONTENDED — comparator lane's own file]
│          persistent-vs-transient verdicts for split-capture + zfunc=8 classes;
│          recurrence→0 measurable
├── S1-L3  src/core/main.cpp                    ★ MAIN.CPP LEAF (serialized)
│          (a) font fast path: extend canonical probe to ZENABLE/ZFUNC/ZWRITE or
│              park-and-set the trio before orig (PERF R5 block :4376-4404)
│          (b) SpriteFlushState + capture/close: add zen/zfunc pair (struct :3547)
│          (c) CoronaWindow_Close completeness (:4468) — restore everything
│              coronaDrawRawShield parks
├── S1-L4  src/render/postfx_utils.cpp
│          SaveRawGeomStates/RestoreRawGeomStates: never promote a divergent
│          device capture into L1/L2 (canonical-or-verbatim + receipt)
├── S1-L5  src/render/postfx.cpp                [gated on L1 attribution naming postfx]
│          quad-pass depth bracket: Begin saves depth trio, End restores it
├── S1-L6a src/render/buildingPipe.cpp          [gated on H1/H5 measurement]
│          exit perpetuator: :3067-3070 restore-each-layer-to-own-capture stops
│          replaying divergent ZWRITE/depth captures (sticky rule for Z state)
├── S1-L6b src/render/vehiclePipe.cpp           [gated, same rule as L6a]
│          mirror of L6a on the vehicle exit (cf. :4209-4233 clobber-scope audit)
└── S1-L7  src/render/mirrorPass.cpp            [gated on non-screen-raster evidence]
           raw-pin audit around mirror/512² env-capture passes (test12 #5 window)
```

Leaf rules: each leaf is one file; S1-L3 is the only main.cpp leaf in S1 and its three sub-items are
one clustered main.cpp pass (directive §3 allows clustering; main.cpp ownership serialized across
waves). S1-L1/L2 do NOT land until the comparator lane's measurement support lands (§5).

---

## 4. N3–N6 — FAM items

### 4.1 N3 — FAM-Q giant milky sprites

**Location.** `src/render/texalpha.cpp :: TexAlpha_Classify()` :1240 / `TacClassifyTex()` :898 /
histogram gates §4.2 (:12-209; "soft decals painted solid with ZWRITE on" :57 and :1312; AA-FRINGE
cutout admission :157); `src/render/texalpha.h :: TexAlphaClass` :91-96 (TAC_UNKNOWN/OPAQUE/CUTOUT/FADED
— **no soft-glow class exists**); `src/core/main.cpp` sprite shields (`spriteStateShield` four-state
block :3517-3533 forcing `ALPHAFUNC GREATEREQUAL@0` + `SEPARATEALPHABLENDENABLE=0` + `COLORWRITEENABLE=0xF`
per-draw; additive ONE/ONE cloud/sprite path :3375). Specimens: test12 #8/#9/#10 (findings:79-81 —
"giant white haze blob fills upper half", "milky-white dome fills ⅔ of frame, sharp curved edge = giant
quad/sphere, not fog").

**Hypotheses (ranked).**
- **H1 — soft-glow misclassification → painted-solid.** Soft masks (mid-dominant ink band,
  texalpha.h:31-43 "cleargraf02_LA zero=14% … mid=85%") fall to the solid path and render as huge milky
  quads. **Supporting:** registry mechanism verbatim (roadmap:167 "TAC soft-decal fallback 'painted
  solid'"); texalpha.cpp:57/:1312 name the exact failure; family map findings:155 ties FAM-Q to the
  "same soft-sprite misclassification root as A-1". **Contradicting:** roadmap:167 itself says family
  A's deterministic TAC "likely shrinks it — measure before adding fixes"; no post-A measurement exists.
- **H2 — per-draw shield forces kill soft falloff / force additive.** `spriteStateShield`'s
  GREATEREQUAL@0 + the ONE/ONE additive cloud path (main.cpp:3375) turn soft glows into full-frame milky
  accumulation. **Supporting:** main.cpp:3503-3510 comment ("GREATEREQUAL@0 kills the soft falloff of
  every later alpha-tested draw"); H1 additive-accumulation clamp still IN FLIGHT (wave-13-record.md:75).
  **Contradicting:** the dome's sharp curved edge (findings:81) reads as one oversized quad/sprite, not
  additive fog.
- **H3 — unclassified bind renders default-white on a giant quad.** 85% of probes unknown (timeline:182).
  **Contradicting:** fix-34 TAC_UNKNOWN safe default landed (roadmap G, wave-13:76) — unknown binds now
  fall to the bound path, not the white/elided one.

**Decomposition.**
- **S2-L1 `src/render/texalpha.cpp`** — soft-glow class admission + soft-decal fallback policy (stop
  "painted solid"); gated on post-A measurement. Tier: **high**; lane: texture-classify fix lane; model: strong.
- **S2-L2 `src/render/texalpha.h`** — enum/API extension (soft band flag or TAC_SOFT). Tier: **very-low**;
  same lane as S2-L1 (disjoint file, same wave OK).
- **S2-L3 `src/core/main.cpp`** ★ MAIN.CPP LEAF (serialized) — spriteStateShield soft-falloff policy for
  glow sprites (per-draw canonical values scoped to the sprite span). Tier: **medium**; lane: core-render
  fix lane; model: strong. **Not in the same wave as S1-L3** (main.cpp serialized).
- **Gate (non-code):** re-shoot test12 #8/#9/#10 after the CJ build ships before opening S2-L1.

### 4.2 N4 — FAM-G grass corruption

**Location.** `src/render/grasssys.cpp :: GrassEnforceDepthContract()` :189 / `GrassResetShaderLatches()`
:70 / `[grass_zfix]` :324-325; `src/render/grasssys.h` (CGrassInstancer :423, CPlantMgr machinery);
`src/render/texalpha.{cpp,h}` (cutout/faded classes); `src/core/main.cpp :: grassRenderCallback()` :820
(ZERO-modulate c0 receipt :850 — "mat colour black — tmpintensity/saveIntensity chain"). Specimens:
test12 #4/#5/#12 (findings:75-76, :83 — yellow streak haze, jagged shard slabs, grass painting OVER
pond water).

**Hypotheses (ranked).**
- **H1 — cutout/faded misclassification of grass textures.** Wrong class ⇒ wrong blend/test ⇒ shard
  slabs + streaked sampling. **Supporting:** roadmap:168 mechanism verbatim; 85%-unknown classifier
  (timeline:182). **Contradicting:** shard-slab *geometry* could be an instancing/latch defect rather
  than a sampling/class one.
- **H2 — shader latch staleness at plant draw.** `GrassResetShaderLatches` exists precisely because
  plant draws inherit stale PS/sampler state (grasssys.cpp:53-68 comment). **Supporting:** function
  purpose verbatim. **Contradicting:** no per-draw receipt ties a streaked frame to a stale latch yet.
- **H3 — prelight/tmpintensity chain (vertex colour).** `grassRenderCallback` ZERO-modulate c0
  (main.cpp:850) proves the modulate chain misfires at least for black; yellow banding/streaks could be
  the same chain with per-vertex colour. **Supporting:** receipt verbatim. **Contradicting:** the receipt
  is BLACK modulate, not yellow.
- **H4 — grass-over-water is family-B depth carve.** **Supporting:** findings:76 "same ZWRITE/ZTEST carve
  family as draw-over-CJ" — the CJ build's M2 carve + sticky depth (roadmap:137 row B) should fix it;
  measure first. **Contradicting:** none — likely already fixed on disk.

**Decomposition.**
- **S3-L1 `src/render/grasssys.cpp`** — draw-time latch set + cutout/faded grass policy + depth-contract
  coverage at plant draw. Tier: **medium-high**; lane: vegetation fix lane; model: mid-strong.
- **S3-L2 `src/render/grasssys.h`** — types/flags if the policy needs them. Tier: **very-low**; same lane
  as S3-L1 (disjoint file, same wave OK).
- **S3-L3 `src/render/texalpha.cpp`** — grass-texture classification band. Tier: **high**; same lane as
  S2-L1. **COLLIDES with S2-L1 (same file) — must NOT share a wave with it** (see §5).
- **S3-L4 `src/core/main.cpp`** ★ MAIN.CPP LEAF (serialized, gated on H3 surviving measurement) —
  prelight/tmpintensity chain in `grassRenderCallback`. Tier: **medium**.
- **Gate (non-code):** re-shoot Glen Park post-CJ-build; grass-over-water should already be clear (H4).

### 4.3 N5 — FAM-N near-black palms

**Location.** `src/render/pipelinecommon.cpp` light-upload lane: `GetTimecycleAmbient()` :174,
`GetTimecycleAmbientPBR()` :199, `ClampAmbientToSky()` :78, `pbrAmbientFloor` + `[PBR-Ambient]` receipts
:142/:169, `pipeEnsureIBLBuffer()` :1087 (→ `RenderIBLBuffer` :1067-1131); `src/render/buildingPipe.cpp`
normal/IBL lane: raw `D3DRS` writes in the IBL capture path (:504 comment), `g_iblTex` shared capture
:2703-2709. Specimens: test12 #4/#5/#12 (findings:75-76, :83 — palm trunks near-black w/ yellow banding;
washed-white buildings; `[PBR-Ambient] absolute floor` is the only log line at the #12 capture).

**Hypotheses (ranked).**
- **H1 — foliage bypasses the PBR ambient floor.** The absolute floor fires (findings:83) yet palms read
  near-black — if the tree/foliage path does not consume `GetTimecycleAmbientPBR`/`buildingAmbient`
  (pipelinecommon.cpp:72-74 enumerates consumers: buildings VS c4/PS c24, vehicles `uploadLights`, peds
  `pAmbient` — **trees/foliage are not named**), foliage sits below the floor.
  **Supporting:** consumer enumeration verbatim; floor receipt at capture time.
  **Contradicting:** palms render through the building pipe's PBR cb in most configurations (needs
  per-mesh confirmation); washed-white buildings imply the *opposite* error (too much ambient) on
  another material class — a per-material split, not a global floor gap.
- **H2 — C-family cull inversion hitting foliage.** **Supporting:** roadmap:169 lists it;
  findings:83 records an inverted-normal signature at the same shots. **Contradicting:** family C is
  FIXED ON DISK (roadmap:138) — if palms persist on the re-shoot, H2 dies and H1/H3 take over; yellow
  banding is not a cull symptom.
- **H3 — normal-buffer/IBL lane corruption.** The IBL capture does raw `D3DRS` writes
  (buildingPipe.cpp:504) and the normal buffer is INTZ depth-reconstructed (postfx_smaa.cpp:1251) —
  stale normals feed wrong IBL to alpha-tested foliage while full-surface buildings wash out.
  **Supporting:** roadmap:169 "normal-buffer/IBL side"; buildingPipe.cpp:504.
  **Contradicting:** both symptoms (dark palms + white buildings) in the same frames argue for
  per-material constants (prelight/vertex colour), not a shared normal buffer.
- **H4 — treeWind alpha-ref / TreeAlpha path.** `TreeAlpha_RestoreAlphaRef` (treeWind.cpp:450-465)
  force-restores alpha ref around tree draws; a wrong ref bands/darkens trunks. Weak support (no receipt);
  listed for the sweep.

**Decomposition.**
- **S4-L0 gate (non-code):** post-CJ-build palm re-shoot first — H2 dies or survives (roadmap:169 "needs
  its own sweep"; this is that sweep's first step).
- **S4-L1 `src/render/pipelinecommon.cpp`** — ambient/foliage consumption audit + per-material ambient
  split (floor/bypass). Tier: **high**; lane: lighting fix lane; model: strong. **COLLIDES with S1-L1
  (same file) — later wave.**
- **S4-L2 `src/render/buildingPipe.cpp`** — IBL/normal lane audit (raw-write hygiene in the capture path,
  normal reconstruction input for foliage). Tier: **high**; lane: lighting fix lane; model: strong.
  **COLLIDES with S1-L6a (same file) — later wave.**
- **S4-L3 `src/render/treeWind.cpp`** — TreeAlpha ref policy (gated on H4 surviving). Tier: **low**.

### 4.4 N6 — FAM-T missing textures (minor)

**Location.** `src/texdefs.cpp` — `TexDefsGetD3DTexture()` :657 (the centralized extraction/guard chain),
`TexDefsLoadDatabase()` :586, `storeTxdName()` :343 / `setCurrentTxdName()` :361; `src/render/texalpha.{cpp,h}`
demotion path. Specimens: test12 #1/#2 (findings:72-73 — pink untextured pillar, magenta wall scribble;
family map findings:159 "missing/misnamed texture or TAC demotion; minor").

**Hypotheses (ranked).**
- **H1 — missing/misnamed texture resolves to the default/untextured raster.** **Supporting:**
  findings:159 verbatim; `TexDefsGetD3DTexture`'s `outWhy` guard chain exists because lookups fail in
  known ways (texdefs.cpp:26, :726). **Contradicting:** none direct.
- **H2 — TAC demotion to UNKNOWN on a nameless raster binds the wrong path.** **Supporting:** 85% unknown
  (timeline:182). **Contradicting:** fix-34 landed the safe (bound) default for unknown ground/terrain
  (wave-13:76) — residual pink/magenta is more likely a genuinely missing asset or TXD streaming timing.
- **H3 — TXD streaming timing (texture not resident at first draw).** **Supporting:** `storeTxdName`/
  `setCurrentTxdName` track TXD load order (texdefs.cpp:343-371). **Contradicting:** no load-order
  receipt in test12.

**Decomposition.**
- **S5-L1 `src/texdefs.cpp`** — lookup-failure receipt (`outWhy` surfaced) + name/demotion path fix.
  Tier: **low**; lane: texture fix lane; model: fast/mid.
- **S5-L2 `src/render/texalpha.cpp`** — demotion fallback band. Tier: **medium**. **COLLIDES with
  S2-L1/S3-L3 (same file) — later wave.**
- Priority: **last** (minor per registry).

---

## 5. Proposed attack order (waves)

Constraints honored: one integrated build per wave (lanes never build — standing directives
`standing-directives-2026-11-02.md` §5 interpretation rule); no overlapping write scopes within a wave
(verified below); unify directive §2 (no second copy of an existing fix — extend
`pipeEnforceDepthContract`/the park primitives and `TexAlpha_Classify`, never per-site clones);
main.cpp ownership serialized (one main.cpp leaf per wave at most, flagged ★).

| Wave | Leaves | Files (one build, parallel-safe) | Gate / notes |
|---|---|---|---|
| **0** (orchestrator) | — (build only) | none (write scope empty) | Integrated build of the CJ-fix tree (A/B/C on disk) → deploy → test12-route re-shoot. Receipts `[PedZW]`/`[BldZW] … ZWRITE parked` must appear (roadmap:140-141); shot-7 discriminator (wave-14:45); N3/N5 measurement baselines captured. **Comparator lane lands measurement support in parallel** (its files: `pipelinecommon.cpp`, `renderstate_monitor.cpp`) — its landing rides this build or the next. |
| **1** (S1 core) | S1-L1 + S1-L2 + S1-L3 + S1-L4 | `pipelinecommon.cpp`, `renderstate_monitor.cpp`, `main.cpp`★, `postfx_utils.cpp` | **After the comparator lane lands** (its two files are the contended scope). Probe: parity-6 `enable L2=1 L3=0` replay count → 0 with sticky-resolved vs replayed now distinguishable. |
| **2** (S1 tails + N3 data) | S1-L5 + S1-L6a + S1-L6b (if gates open) | `postfx.cpp`, `buildingPipe.cpp`, `vehiclePipe.cpp` | Opened only where Wave-1 attribution named the writer; otherwise skip to Wave 3. |
| **3** (N3) | S2-L1 + S2-L2 (+ S2-L3 if main.cpp free) | `texalpha.cpp`, `texalpha.h`, (`main.cpp`★) | Gate: N3 re-shoot after Wave 0 shows FAM-Q surviving A's TAC. S2-L3 needs exclusive main.cpp slot — include here only if Wave 1's main.cpp leaf has landed and no other wave holds main.cpp. |
| **4** (N4) | S3-L1 + S3-L2 + S3-L3 | `grasssys.cpp`, `grasssys.h`, `texalpha.cpp` | texalpha.cpp is exclusive to THIS wave (S2-L1 landed in Wave 3; S5-L2 waits). Grass-over-water should already be clear post-Wave-0 (family B). |
| **5** (N5) | S4-L1 + S4-L2 (+ S4-L3) | `pipelinecommon.cpp`, `buildingPipe.cpp`, (`treeWind.cpp`) | Gate: S4-L0 sweep verdict (H2 cull-inversion dead or alive). pipelinecommon.cpp/buildingPipe.cpp are free of S1 leaves by now (Waves 1-2 landed). |
| **6** (N6) | S5-L1 + S5-L2 | `texdefs.cpp`, `texalpha.cpp` | Minor; last. texalpha.cpp free (Wave 4 landed). |

**Zero-overlap verification (per wave, leaf file sets pairwise disjoint):** W1 {pipelinecommon,
renderstate_monitor, main, postfx_utils} ✓ · W2 {postfx, buildingPipe, vehiclePipe} ✓ · W3 {texalpha.cpp,
texalpha.h, main} ✓ · W4 {grasssys.cpp, grasssys.h, texalpha.cpp} ✓ · W5 {pipelinecommon, buildingPipe,
treeWind} ✓ · W6 {texdefs, texalpha.cpp} ✓. Cross-wave repeats (pipelinecommon W1→W5, main W1→W3,
buildingPipe W2→W5, texalpha.cpp W3→W4→W6) are sequential, not concurrent. S1-L1/S1-L2 additionally
serialize **behind the comparator lane** (the moving-tree finding, § caveat).

**Unify notes.** (1) `GrassEnforceDepthContract` (grasssys.cpp:189) is a second copy of the
`pipeEnforceDepthContract` idiom — after S1 stabilizes, a small unify leaf may fold the grass depth
contract onto the shared primitive (deferred; not in these waves). (2) All classifier work for N3/N4/N6
goes through ONE `TexAlpha_Classify`/gate change set per wave — never per-consumer copies (directive §2).
(3) The S1-L3 main.cpp shield items are one clustered pass, not three (directive §3).

---

## 6. Verification (spot-checks against disk)

Ten cited references re-read from disk (grep/sed/awk, 2026-11-08). The two moving anchors are marked ⇄
and were the trigger for the moving-tree finding.

| # | Citation | Check | Result |
|---|---|---|---|
| 1 | `pipelinecommon.cpp :: pipeExitAlphaMode` | grep/awk | ✓ present, **⇄ 2095→2113** during recon |
| 2 | `pipelinecommon.cpp:2281` `[PipeAlpha] faithful restore (parity 6): rw` | grep | ✓ (**⇄ was 2263**) |
| 3 | `pipelinecommon.cpp:1349 pipeEnforceDepthContract(void)` | grep/awk | ✓ (**⇄ was 1348**) |
| 4 | `pipelinecommon.cpp:1390` `[zfix] pipe entry depth drift repaired` | grep | ✓ (was 1385) |
| 5 | `main.cpp:4376` PERF R5 fast-path block | sed | ✓ hit ("entry==canonical fast path") |
| 6 | `texalpha.h:91` `enum TexAlphaClass` | sed | ✓ hit (TAC_UNKNOWN..TAC_FADED :91-96) |
| 7 | `skygfx.h:1482` `PAACAP_BLENDENABLE = 0x1` | sed | ✓ hit (struct fields :1524-1530) |
| 8 | `texdefs.cpp:657 TexDefsGetD3DTexture(RwTexture*, int*, uint32*)` | sed | ✓ hit |
| 9 | `grasssys.cpp:189 GrassEnforceDepthContract(void)` | grep | ✓ hit (GrassResetShaderLatches :70) |
| 10 | `postfx_utils.cpp:190 SaveRawGeomStates` / `:246 RestoreRawGeomStates` | sed | ✓ both hit |
| + | `renderstate_monitor.cpp :: RSMon_Sample` | grep/awk | ✓ present, **⇄ 510→598** during recon |
| + | `main.cpp:4468 CoronaWindow_Close` / `:3560 spriteFlushShieldCapture` / `:3634 spriteFlushShieldClose` / `:820 grassRenderCallback` | grep | ✓ all present |

Contended-scope evidence: `git status --porcelain` → `M src/render/pipelinecommon.cpp`,
`M src/render/renderstate_monitor.cpp` (comparator lane active). Leaf-scope disjointness per wave:
verified in §5 (zero overlapping file scopes within each wave). No code, build, or commit touched;
this doc is the only write.

---

## 7. Flags

1. **CONTENDED SCOPE — serialize behind the comparator lane:** `src/render/pipelinecommon.cpp`,
   `src/render/renderstate_monitor.cpp` (mutating live during recon; +18/+88 lines observed). S1-L1/L2
   and S4-L1 are affected.
2. **★ main.cpp ownership is serialized** across waves: S1-L3 (W1), S2-L3 (W3), S3-L4 (gated) may never
   share a wave with each other.
3. **N1 acceptance-probe semantics gap** (§2.2 H4): the parity-6 receipt still fires on sticky-resolved
   splits, so the registered probe "count → 0" (roadmap:165) is unreachable as literally written —
   fix lanes must split `sticky-resolved` vs `replayed` events first (S1-L1).
4. **Measurement gates before code:** N3 (post-A TAC measurement), N4 (grass-over-water expected clear
   post-CJ build), N5 (S4-L0 cull-inversion sweep). Wave 0's build is the prerequisite for all three.
5. **N1 replay-stop is already on disk** (sticky rule, pipelinecommon.cpp:2162-2186) — the registered
   "extend sticky rule to alpha" work is done in the unbuilt tree; remaining N1 work is source-kill +
   probe semantics, not more restore logic.
6. **Tier spread:** ultra-tier items: none. Highest-risk leaves are S1-L1 (state-contract surgery in the
   hottest shared file) and S4-L1/S4-L2 (lighting semantics) — all `strong`-model lanes. Everything else
   is medium or below.
