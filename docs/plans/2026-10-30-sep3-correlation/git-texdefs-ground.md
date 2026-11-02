# Git archaeology — texture-definition & ground-texture binding (W4 correlation)

**Lane**: READ-ONLY research. No source edited, no build/commit.
**Date**: 2026-10-30 · **Branch**: HEAD `898e894` + uncommitted tree ("Oct waves") is the current build.
**Bug under investigation**: W4 — flat grey untextured road slab under car (hard rectangular edge vs brown dirt, zero texture/detail), finalgate capture #1 (`gta_sa_Fa3HyQjE78.png`, in-game 11:19). Ref: `docs/baseline/2026-10-29-finalgate-0042/findings.md`.
**Sep-3 reference**: `docs/baseline/2026-09-03-reference-caps/findings.md` (all roads textured, checklist item 11).

---

## 1. What the Sep-3 tree actually was (git snapshot resolution)

No commit exists on 2026-09-03. The window is:

| Commit | Date | Content |
|---|---|---|
| `52bf910` | 2026-08-01 | Phase-0 safety pass on texdb.exe (`src/extras/texdb.cpp`): bounds guards, malloc discipline. Classification logic untouched. |
| `c270294` | 2026-08-30 | INI/config consolidation (no texdb logic). |
| `18f5303` | 2026-09-07 | "checkpoint: PBR restore + ..." — **last commit touching `src/extras/texdb.cpp`** (only shutdown/guard additions, see §3.1). |

The Sep-3 reference look therefore corresponds to the `18f5303` tree (texdb and all ground-binding logic byte-identical to the Aug-1 state; the Sep-7 commit is a pure hardening diff).

## 2. Lineage of the texture-definition files

```
Sep-3 tree (18f5303)                      Current working tree (898e894 + dirty)
─────────────────────────────             ──────────────────────────────────────
src/extras/texdb.cpp        [tracked] →   DELETED; replaced by src/texdefs.cpp
                                          + src/texdefs.h   (UNTRACKED, merge of:
                                           texdb.cpp + render/normalmap.cpp*
                                           + rw/normalmap.cpp + extendedplg.cpp
                                           + pipeplg.cpp PDS registry)
src/rw/normalmap.cpp        [tracked] →   DELETED (external DK22Pac lane merged)
src/rw/normalmap_plugin.cpp [tracked] →   DELETED (external-plugin glue merged)
src/rw/extendedplg.cpp      [tracked] →   DELETED (EDED slot merged)
src/rw/pipeplg.cpp          [tracked, M]→ PDS slot now in texdefs.cpp
src/render/texalpha.cpp     —             NEW (untracked; per-TEXTURE alpha class)
```
(*render/normalmap.cpp never existed in git in this repo line, it arrived inside the uncommitted Oct tree already merged.)

**Commit history of a changed ground-binding rule: NONE.** `git log -- src/texdefs.cpp src/texdefs.h src/render/texalpha.cpp` is empty — the whole unified layer lives only in the uncommitted working tree (documented in `premake5.lua:16-36`; the deleted `resources/cso/simpleStochasticPS.cso` / `simpleDetailStochasticPS.cso` are renames to `simpleNoiseDetailPS.cso` / `simpleDetailNoiseDetailPS.cso`).

## 3. Verified invariance Sep-3 → current (texdb classification semantics)

### 3.1 Diff matrix `git diff 18f5303..HEAD -- src/extras/texdb.cpp` — EMPTY, plus uncommitted rewrite

`git show 18f5303:src/extras/texdb.cpp` (the Sep-3 reference, 292 lines) was compared line-by-line against the texdb section inside `src/texdefs.cpp` (lines 120-583):

| Concern | Sep (`texdb.cpp`) | Current (`texdefs.cpp`) | Verdict |
|---|---|---|---|
| texdb.txt parser (`readTxt`/`readTexDbTxt`) | `alphamode/isdetail/hasdetail/detailtile/hassibling/affiliate/stochastic/dualPass/zwriteThreshold` (orig lines 55-90) | identical keys, `stochastic=` kept as **deprecated alias → noiseDetail** (texdefs.cpp:476-479) | invariant |
| Insert rule (`hasdetail || alphamode || hassibling || affiliate || stochastic/noiseDetail`, orig :100) | texdefs.cpp:493 | invariant |
| Affiliate chaining in find CB | orig :164-172 =TexDefsFindCB texdefs.cpp:244-258 | invariant (double affiliate resolve + sibling override preserved) |
| sibling naming `"<name>_<txd>"` | orig :169 =texdefs.cpp:249-254 | invariant |
| find-CB hook site `0x731FD5+1` (previous-CB capture via push immediate) | orig hooktexdb :281-284 | texdefs.cpp:398-412 — now byte-verifies `push imm32` (op 0x68) and captures `gameGetTexture = 0x731720` | same mechanism + hygiene |
| TXD-name hooks `0x731CCB` / `0x7319EB` | orig :269-276 | texdefs.cpp:379-396 | same + byte-verify |
| plugin slot `0xbadeaffe`, ctor zeroing, `RwTextureGetTexDBInfo` → `TexDefsGetInfo` fallback `faketexinfo.dualPass=1` | orig :218-237 | texdefs.cpp:230-236, 273-278, 303-304, 573-583 | invariant |
| roadsign default entry | orig :221-226 | texdefs.cpp:560-566 | invariant; `main.cpp:3561` `TexDefsAssignInfo(texture, TexDefsFindInfo("roadsign"))` == Sep `main.cpp` direct slot write (now NULL/guard-safe) |
| detail-texture load `Mobile_details.txd` | orig :131-147 | texdefs.cpp:536-552 | invariant |

**Conclusion**: the ground/road *classification* (which textures get TexInfo, sibling/affiliate resolution, dualPass/zwriteThreshold metadata) did **not** change after Sep 3. No commit and no uncommitted hunk alters the classification *rules* for ground textures.

### 3.2 Live receipts from the W4 capture run (`E:/games/gtasa_skygfx_plus/skygfx_dbg.log`, 2026-10-29 run 00:40-00:44)

- `[texdefs] find chain installed (get=00731720) pluginA=0 pluginB=0` (log :65) — find CB installed, previous CB = the game's own `0x731720`, same value Sep captured.
- `[texdefs] plugin registry: eded=140 pds=144 texdb=92 nmpair=96` (log :67) — texdb slot 92 registered and active.
- `texalpha: <summary>` lines (:1307, :1607, …) show ~2000 textures classified, `unknown_reasons` ALL ZERO (`tex=0 raster=0 ext=0 extptr=0 d3d=0 leveldesc=0 lock=0 format=0`) — no texture failed D3D extraction; the 72 budget skips warm up in ~16 frames and are retried.
- `[BPIPE] cb=Xbox ... PS=IDR_SIMPLEDETAILPS` (:659, :1772) — the texdb `texinfo->detail` branch (`buildingPipe_selectPS`, buildingPipe.cpp:948-955) selected the DETAIL PS for some ground mesh ⇒ **TexInfo attach on ground textures is provably live in the W4 build.**

**⇒ The Sep-3 "zero texture" symptom cannot be traced to a broken texdb attach — attach is alive and the classification rules are unchanged.**

## 4. What DID change after Sep 3 in the texture class path (all uncommitted Oct waves)

| New layer | Where | Gate in W4 build | Ground impact |
|---|---|---|---|
| `texalpha` per-TEXTURE alpha classifier (`TexAlpha_Classify`) | src/render/texalpha.cpp (all 954 lines untracked) | `texalphaClassify=1` ACTIVE (INI) | Yes — replaces the Sep per-mesh `hasAlpha` decision with a per-texture class: OPAQUE = blend off + force `c48.w` alpha 1 / CUTOUT = alpha test / FADED = blend on / UNKNOWN = legacy hasAlpha. Xbox cb port at buildingPipe.cpp:1886-1949; PBR at buildingPipe.cpp:2702-2754. |
| NM pairing `_n`/`_norm` classification | src/texdefs.cpp:177-264 | `normalMapEnable=0` → `nmPairingEnabled()` false (texdefs.cpp:177-182) → probe inert | None in the W4 build; only relevant later. |
| DK22Pac external normal-map lane | texdefs.cpp:744-1142 | Gated on separate INI `enableNormalMaps=0` (defensive separate key re-read :1034-1043) → attach skipped | None; logic byte-identical to Sep `src/rw/normalmap.cpp` (verified against `git show 18f5303:src/rw/normalmap.cpp` 350-360), only prologue-verify wrap added :1086-1128. |
| EDED slot actually REGISTERED now | texdefs.cpp:288-294 | Always (registry eded=140) | **Behaviour change vs Sep**: Sep `myPluginAttach` (`git show 18f5303:src/core/main.cpp:969`) was `PipelinePluginAttach() && PDSPipePluginAttach() && TexDBPluginAttach()` — it NEVER called `EDEDPluginAttach()` (defined but unreferenced, `git grep` confirmed). With `ededAtmOffset==0`, Sep's `GetDefinedShader` read offset 0 = `RpAtomic.object.object.type` byte = `rpATOMIC(1)` == `DefinedVertexShader::WIND` (enum DEFAULT=0,WIND=1, `18f5303:src/skygfx.h:178-184`) ⇒ **in Sep-3 every atomic looked WIND** (`vertexAlphaIsAlpha=false` term). Current tree returns real per-atomic shader. This is a legit latent-Sep-vs-current difference (vertex-alpha interpretation), NOT a texture-bind mechanism. |
| DualPass leg on the PBR building cb | buildingPipe.cpp:3011-3019 (Sep PBR cb `git show 18f5303` :802 was plain `D3D9Render`) | `dualPassBuilding=0` in the W4 INI → leg inactive | None in the W4 build (PS2/Xbox cbs had it in Sep too). |
| PipeChain classify second draw | postfx.cpp:3388-3534 (`PipeChain_ClassifyBegin`), building cb use at buildingPipe.cpp:3026-3034 | `pipeChainEnable=0`, `pipeChainReflEnable=0` → `ClassifyBegin` returns false (:3397-3399) → NO second draw | None in the W4 build. |

## 5. Stage-0 binding audit (the "zero texture" bind path) — Sep vs current

Every ground draw binds the material texture on s0 unconditionally-per-callback in BOTH trees:

| Callback | Sep (`18f5303`) | Current |
|---|---|---|
| PS2 | `pipeSetTexture(material->texture, 0)` per mesh (orig :360, :502) | `buildingPipe_bindTexStage(material->texture, 0, &elideLastTex0, &elideTex0Valid, BPELIDE_PS2/XBOX)` — buildingPipe.cpp:1359 (PS2), 1881 (Xbox), 2674 (PBR) |
| Xbox | `pipeSetTexture(material->texture, 0)` (orig :360/502 family) | :1881 |
| PBR | `pipeSetTexture(material->texture, 0)` (orig :736) | :2674 |
| NULL diffuse | skygfx.h:1295 `pipeSetTexture` maps NULL→`gpWhiteTexture` (present in BOTH trees) | same |

- The current `buildingPipe_bindTexStage` (buildingPipe.cpp:1169-1181) elides only on a **resolved-pointer** key with POD sentinel locals (`valid=false` at callback entry ⇒ the FIRST mesh of every callback always binds); elision never survives into another callback. Deterministic rebind remains on any `RwTexture*` change. This is strictly *at least* as bind-correct as the Sep unconditional bind, with one theoretical caveat: the tracker does not observe raw `dev->SetTexture(0,...)` writes made between two meshes of the SAME callback; no current code path in the building cbs does that between meshes (PIPE-chain classify draws run with s0 untouched and restore the PS at buildingPipe.cpp:2299).
- `pipeSetTexture(NULL)` in BOTH trees = 1x1 opaque white ⇒ if a road mesh genuinely bound white/grey, `material->texture` was NULL **at draw time** — a streaming/DFF material state, not a texdefs classification state.

## 6. Task item 4 — unconditional early-outs / classify gates that never fire

Dead-gate sweep over the current tree (pattern per known `chars_classifyClump` case):

1. `chars_classifyClump` — CONFIRMED still never called (src/entities/chars.cpp:29-36: "Nothing produces the mask today"). Consequence: chars SSS composite stays a no-op (chars.cpp:189-197 early-out `if(!classifyFresh || !g_charsClassifyTex)`), and classification of pedestrian SSS masks never runs. PEDESTRIAN orb only (W3 family), **not ground**.
2. No equivalent dead classify-gate exists for ground textures: every ground-texture consumer (`TexDefsGetInfo` at buildingPipe.cpp:1398/:2022/:3018, `TexAlpha_Classify` at :1902/:2702, `buildingPipe_bindTexStage` at :1359/:1881/:2674) is on the live mesh-loop path; the texalpha summary counters in the W4 log prove the classifier runs against real textures every session.
3. `TexDefsInit` / `TexDefsTryAttachExternal` contain **byte-verify skips** (texdefs.cpp:381-396, 1086-1128) that would silently skip the find-CB or pipe hooks on a prologue mismatch. EXCLUDED by the log receipts (`find chain installed`, `plugin registry: eded=140 pds=144 texdb=92 nmpair=96`, and `texdefs shutdown complete` at :13872).

## 7. Where the W4 run actually drew the ground (pipeline context)

Config derived during the W4 session (log :16 → :220):

```
Config: pipeline=2 buildingPipe=1 vehiclePipe=2 colorFilter=2 colorFilterEnable=1
```
⇒ the road under the car in capture #1 was drawn by the **XBOX building callback** (ps2-family simplePS/simpleDetailPS per `[BPIPE] cb=Xbox` receipts, :270-:1467), NOT the PBR cb. W1/W4 in that run therefore share the Xbox building cb + the vanilla-fallback path behaviour. Note `dualPassBuilding=0`, `pipeChainEnable=0`, `pipeChainReflEnable=0` (INI), `normalMapEnable=0` — inactive at capture time.

## 8. Ranked suspects for W4

**S1 — STALE-PIPE VANILLA WATER-GRID FLUSH ("mismatched" flat slab) — HIGHEST. *(not a texdefs regression)**

- Commit: introduced post-Sep in the uncommitted water/pipe waves; correlated already in `docs/plans/2026-10-29-teal-quads-diagnosis.md` (H1, evidence :621 v/3432 i = ratio 5.53 FAIL at 00:43:12.8).
- File:lines: `src/render/waterPipe.cpp:513-517` (WbBufferCountsSane rejects shared-vertex grids), bail at waterPipe.cpp:1057-1063, `src/core/main.cpp:5595-5599 / 5739-5747` else-branch hands the buffer to the vanilla draw with the previous pipe's VS/PS still bound.
- Mechanism: the grid flush draws a full-screen-sized hard-edged quad/rect with water-body colour constants through a stale (building/vehicle) shader pair — **reads exactly like a "flat grey untextured slab with hard rect edge"** and sits in the same capture family as W1's teal slab (same emitter, different colour = stale-PS constant variant; teal-diagnosis capture #1 in the SAME run was already tagged "Giant flat GREY slab … (stale-PS colour variant)").
- Why texdefs is exonerated here: texdb classification unchanged (§3), stage-0 bind always happens (§5), classifier extractions all OK (log), and road-family detail attach proven live.

**S2 — texalpha TAC mapping on ground textures (possible visual contributor, still textured) — MEDIUM.**

- Commit: uncommitted Oct waves (no commit).
- File:lines: src/render/texalpha.cpp:785-926 (`TexAlpha_Classify`, kill :792, NULL-tex→OPAQUE :802-808); consumers buildingPipe.cpp:1902-1934 (Xbox cb — the cb that ran in the W4 capture) and :2702-2754 (PBR).
- Mechanism: `TAC_FADED` road/pavement texture forces blend (with ZWRITE on) where Sep alpha-tested — a soft "grey wash"/edge halo is possible; `TAC UNKNOWN` budget-skips yield legacy hasAlpha (same as Sep). Max deviation ≈ blend-state, NOT texture loss. Watch: `santaco6` CUTOUT and the two `FADED`'s in the log are already sane; a repeat capture with `texAlphaClassify=0` isolates it in one run.
- Note: in the W4 build the Xbox simplePS family "reads no c48" (buildingPipe.cpp:1906-1910) so shaderAlpha has no Xbox effect there at all.

**S3 — `stochastic`→`noiseDetail` rename on PS2/Xbox PS selection — LOW/NEUTRAL.**

- File:lines: texdefs.cpp:474-479 (alias keeps old files loading) + buildingPipe.cpp:940-963 (selection) + `pipeBindNoise(9)` :973-983.
- Mechanism invariant because (a) texdb.txt files still parse (`stochastic=` alias), (b) the W4 run log shows only `simplePS`/`IDR_SIMPLEDETAILPS` on the Xbox cb, (c) all new noise CSOs exist on disk (`simpleNoiseDetailPS.cso`, `simpleDetailNoiseDetailPS.cso`, `xboxBuildingNoiseDetailPS.cso`) with a `simplePS` fallback missing-CSO (:961, :855). No evidence of a ground impact.

**S4 — EDED attach change (Sep latent bug now fixed) — LOW.**

- File:lines: texdefs.cpp:79-95 + :288-294; Sep `18f5303:src/core/main.cpp:969`.
- Mechanism: Sep misread `WIND` for all atomics (offset 0). Ground road atomics are not WIND-tagged, so with Sep "vertexAlpha is wind weight" the vertex-alpha term was globally disabled in the reference look; the current tree can now enable it (legit behaviour delta for foliage under WIND, not roads). NOT a texture-bind mechanism.

**S5 — the `chars_classifyClump` no-op (task item 4 answer direct)** — confirmed DEAD, pedestrian SSS family (W3), grounded NOT affected. chars.cpp:29-36, 189-197.

## 9. Recommended fix approach for W4

1. **Do not chase texdefs for W4** — the git archaeology + log receipts prove ground classification/binding is a faithful continuation of the Sep-3 rules (§3, §5).
2. **Verify W4 & W1 are one defect**: in the W1/W4 capture window add a receipt when the WbBufferCountsSane bail fires (current bail is silent — throttle even starved the one FAIL at 00:43:12.8 per teal-diagnosis §4). If every "slab" correlates with a bail, fix the bail path:
   a. Accept shared-vertex grids in `WbBufferCountsSane` (`nIdx <= nVerts*6` or stop rejecting ratio>3 when `i/(v*3) >= 2` and topology check passes), **or**
   b. before handing to vanilla `CWaterLevel__RenderAndEmptyRenderBuffer`, set a safe vanilla state (PS=NULL, identity/simple VS, FFP texture stages bound, restore after) so a mis-projected flush can never inherit the building/vehicle shader pair — this alone de-colours the slab and turns it into a classic water draw.
3. One-run differential for S2 (cheap, also validates W4 independence): `texAlphaClassify=0` (or keep 1 but log `[BldTex]` for the exact road-tex name under the car: `bound==gpWhiteTexture` ptr ⇒ real NULL-material streaming case to chase; `bound==normal ptr` ⇒ the slab is drawn from another cb (S1) — matches the under-car ring).
4. Only if step 3 shows a bind anomaly: re-verify with the noise (`noiseDetail=0`) and detail (`detailMaps=0`) selectors — no code change expected.
5. Keep recovering the Sep-3 look via S1: the reference caps are textured roads per checklist item 11; S1's fix keeps water surfaces textured too (W1).

## 10. Evidence appendix (log line numbers, run 2026-10-29 00:40-00:44, `E:/games/gtasa_skygfx_plus/skygfx_dbg.log`)

| Line | Receipt |
|---|---|
| :16 | `Config: pipeline=2 buildingPipe=0 vehiclePipe=0 colorFilter=0 ...` (boot) |
| :65 | `[texdefs] find chain installed (get=00731720) pluginA=0 pluginB=0` |
| :67 | `[texdefs] plugin registry: eded=140 pds=144 texdb=92 nmpair=96` |
| :220 | `Config: pipeline=2 buildingPipe=1 vehiclePipe=2 colorFilter=2 colorFilterEnable=1` (pipeline switch to Xbox, pre-capture) |
| :270-:560 | `[BPIPE] cb=Xbox pipe=XBOX(1) ... PS=simplePS / IDR_SIMPLEDETAILPS ... handles pbr=.../iv=...` (the cb that drew the W4 ground) |
| :1307 | `texalpha: 1353 opaque, 7 cutout, 0 faded, 0 unknown, 0 faults (cached 1285/4096) ... unknown_reasons: tex=0 raster=0 ext=0 extptr=0 d3d=0 ... format=0 budget=72` |
| :1420/:1540 | road/fence-family CUTOUTs with histograms (classifier sane) |
| :13872 | `texdefs shutdown clean` (end of run) |

Working-tree state used: HEAD `898e894` + dirty `src/` (texdefs.cpp/h, texalpha.cpp/h untracked; texdb.cpp/normalmap*.cpp/extendedplg.cpp deleted).
