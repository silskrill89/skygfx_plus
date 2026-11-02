# 2026-10-25 — Fortified debug strategy + Rockstar/aap parity checklist

**Status:** ACTIVE (process + parity reference) · **Type:** docs-only deliverable, no source edited.
**Directive (verbatim intent):** *"fortify our debug strategy based on the findings from mds to make
sure we have parity with leaked rockstar code and we fixed everything they shipped."*

**Superset contract.** The mod must be a correct superset of:

* **(a) Rockstar parity** — everything the shipped game did, per the leaked tree
  `E:\dev(dave)\releases\GTASource\gtasa_src-main` (shipped-but-disabled beta paths are exposed as
  opt-in, never silently dropped).
* **(b) aap SkyGfx legacy preserved** — `backup_original/src/` behaviour is the look baseline for the
  PS2/Xbox/PC-vintage modes (`2026-10-24-aap-xbox-ps2-parity.md` header: *"Parity baseline: aap
  behaviour, not Rockstar console behaviour"* — the Rockstar console pipes are FFP; aap's pipes are
  already shader re-implementations of that look).
* **(c) Our extensions on top** — PBR / Forward+ / PipeChain / water tiers / state contracts, always
  gated so (a)+(b) remain reachable.

**Governing rule:** every divergence from (a) or (b) must be either (i) a documented `INTENTIONAL`
repair that preserves legacy output in the common case, or (ii) behind a default-off INI gate
(`INTENTIONAL*`). Anything else is a `REGRESSION` and goes on the §3 backlog. Verdict vocabulary is
the parity matrix legend (`2026-10-24-legacy-parity-matrix.md` §Verdict legend).

**Sources read for this doc** (cited throughout as short names):

| Short name | File |
|---|---|
| PIPES | `docs/plans/2026-10-24-gtasource-pipes.md` |
| CORE | `docs/plans/2026-10-24-gtasource-render-core.md` |
| AAP | `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` |
| MATRIX | `docs/plans/2026-10-24-legacy-parity-matrix.md` |
| PCINV | `docs/plans/2026-10-24-legacy-pc-feature-inventory.md` |
| MODES | `docs/plans/2026-10-24-mode-preset-architecture.md` |
| CORONA | `docs/plans/2026-10-24-corona-blackquad-diagnosis.md` |
| WATER | `docs/plans/2026-10-24-water-veil-diagnosis.md` |
| ALPHA | `docs/plans/2026-10-21-alpha-reversal-findings.md` |
| CKPT | `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` |
| README | `docs/plans/README.md` |

---

## 1. Debug strategy (fortified)

### 1.1 Build identity

* **`[BUILD]` fingerprint** — every run's log must open with
  `[BUILD] asipath=… size=<bytes> mtime=<ts> built=<__TIMESTAMP__>` (landed wave-3, CKPT §Wave 4;
  proven in WATER §1: `size=2317824 mtime=2026-10-24 21:03:40 built=Oct 24 2026 21:02:54`).
  A diagnosis doc that does not quote it is invalid (WATER §1 complies).
* **Object-timestamp cross-check** — `build/obj/Release/*.obj` mtimes must bracket the compile stamp
  (WATER §1: `waterPipe.obj` 21:03:35, `pipelinecommon.obj` 21:03:26 vs stamp 21:02:54).
* **Shader census** — `[INIT] shaders: compiled OK (ps=57 vs=17), FAILED (ps=0 vs=0)`; FAILED must be
  0 before deploy (WATER §1).
* **One deploy per wave; no per-lane builds.** Parallel fixer builds raced and produced 3 conflicting
  ASI sizes; the orchestrator re-ran the gate and verified via binary strings (CKPT §Wave 1 note).
  Rule: builds run sequentially, or one integrated gate after parallel lanes.
* **Binary string verification after deploy** — grep the deployed ASI for the wave's fix tags; a wave
  is not "deployed" until its tags are FOUND (CKPT waves 1/4/5 all did this:
  `blend_leak/pc_badpack/zstate/corona_exit/defpipe_contract`, `[BUILD]`, `shadow_contract`,
  `PedOpaque`, … FOUND).
* **Never analyse a log whose `[BUILD]` size/mtime does not match the deployed ASI.**

### 1.2 Logging

* **256-bucket FNV throttle** (`dbglog_throttle`) — replaced the 64-bucket ×31 hash whose inter-tag
  collisions starved evidence: `corona_feed`/`corona_exit` shared bucket 33 (the `[CoronaExit]`
  one-shot appeared ~19.5 s late), `corona_shield` collided with `pc_c1chk` (bucket 44),
  `corona_draw` with `mirror_gates_passed` (bucket 7) — CORONA §3 Q3 starvation table. Landed
  wave-3/4 (CKPT).
* **`dbglog_once`** for first-hit facts (CKPT wave 3/4).
* **Unthrottled first-hit probes** — the first occurrence of a critical event logs unthrottled with
  full state. CORONA §3 Patch 3 is the standing rule: *"Use `dbglog_once` for the first hit and a
  plain counter for the rest — never a 2 s boolean for the only evidence."*
* **Per-reason counters** — the texalpha model is the template: `texalpha: <CLASS> tex=… zero=.. full=..
  mid=.. total=.. blendOn=..` (8/class) plus `[VehOpacity]`/`[BldOpacity]` first-16 receipts
  (CKPT §Wave 4 fix-6). Generalise: every classifier/gate logs *why*, not just a boolean.
* **Counter-limited one-shots** — first N hits (e.g. 3) + a 2 s summary line with counters:
  `[FlushShield] flushes=NNN blendOff=N dstZero=N forced=N (last nverts=NNN)` (CORONA §5.3).
* **Log-value hygiene (new standing rule, from two real bugs):**
  1. Read game counters with the right width — `[CoronaFeed]` read `0xC3E038` as `uint16_t` → garbage
     `pool=65524` (CORONA §3 Patch 3, §6). Read the dword; also print nonzero-id count.
  2. Never feed `%d` a pointer/string — `[water] body=` printed a `const char*` as `%d`
     (WATER §2). Both classes are review items for every new log line.
* **Zero is decidable or the probe is wrong** — a zero count in a log must be explainable as either
  "did not run" or "starved"; if neither can be proven, the probe was designed wrong
  (CORONA §3 Q3 verdict).

### 1.3 State observability — three layers + exit receipts

* **Three layers** (PIPES §2 legend; CORE §5.5): **L1** = RW state cache (`RwRenderStateSet`),
  **L2** = RW D3D9 driver cache (`RwD3D9SetRenderState`), **L3** = raw device
  (`d3d9device->SetRenderState`). Rockstar only knows L1/L2 (plus L3 via `D3DDevice_*` on Xbox);
  our postfx and hooks write L3 — which is exactly why the model exists.
* **Frame-boundary monitoring** — RSMon/state probes at SceneStart / EffectsExit / PostFx.exit /
  DeviceReset (`pipeApplyWorldBaseline` call sites, MATRIX §2.2 C4) + per-frame `[FxAlpha]`
  three-layer dump (`rw vtx=… d3d vtx=… dev vtx=…`).
* **Per-cb exit receipts** — every pipe callback exit emits a contract receipt:
  `PedRS`/`PedOpaque` (wave-5 `PedOpaqueState` capture/enforce/restore, CKPT §Wave 5 item 3),
  `shadow_contract` (wave-5 item 6), `FxAlpha` (per frame), `bpipeBlendLeakProbe` at every
  building-cb exit (CKPT §Wave 1 fix-4), `water_state` pre-draw pin (wave-5 item 1).
* **THE RULE (fortified):** *mid-frame divergence is invisible to frame-boundary sampling → every
  pipe exit needs a contract receipt.* Evidence:
  1. The corona cache↔device split (`rw vtx=1 / d3d vtx=1 / dev vtx=0`) persisted **every frame**
     while frame-boundary baselines ran (CORONA §0, §2).
  2. The world baseline only runs at frame boundaries and *"cannot repair a mid-frame water WVP or a
     stale shader cache"* (WATER §5).
  3. postfx raw `D3DRS_ZWRITEENABLE` writes left the device off while the rw cache said on → 19/43/26
     entry repairs per run (ALPHA §ROOT EVIDENCE).
  Corollary: any pass that raw-writes L3 must restore through all three layers
  (`RestoreRawGeomStates` park-and-set, CKPT §Wave 2 Lane A).
* **Constant-cache observability** — RW PS-constant cache desync was the black-pool root cause
  (CKPT §Wave 3 L1); codified pattern: raw+RW double-upload for any constant written raw
  (`postfx.cpp:3049-3061`) + `pc_c1chk` verifier. Gap: only c0/c1 verified today (§1.7 item 2).

### 1.4 Byte-verified patch discipline

* **Opcode+target verification before every `InterceptCall`** —
  `[corona_install] 0x70CF62 op=E8 orig=00734EA0OK | 0x70CF9B op=E8 orig=00734EA0OK`
  (CORONA §1.1, §5.1); early CPointLights site `0x53DE6B` byte-verified `E8→0x7002D0`
  (CKPT §Wave 4); shadow zero-arg sites `0x53E0BE/0x53E0C3/0x53E0C8` byte-verified
  (CKPT §Wave 5 item 6).
* **`[*_install]` OK/MISMATCH gates** — installs are gated on site verification OK; MISMATCH aborts
  the install and logs (CKPT §Wave 3 L2: *"corona_install gates both installs on site OK"*).
* **rel32 math** — `InterceptCall` rewrites `E8 rel32` only; verified correct (CORONA §1.1). A
  non-E8 site is skipped, not forced (e.g. `0x707460` takes an int — CKPT §Wave 5 item 6).
* **Never re-patch SilentPatch-owned sites:** `0x6FB621` (CCoronas tail), `0x70F45A` (appender flag),
  `0x74872D`, and the shadow/mirror sites SP touches. SP coexistence contract: SP probes skygfx via
  `GetProcAddress "GetConfig"` + version field (≥0x360 skips MoonPhases); SP ini
  `SunSizeHack`/`DirectionalFromSun` left alone (CKPT §Wave 3). Rule: before adding any new patch
  site, check it against SP's patch list; if owned, wrap or read-only.
* **Patch ledger** — every `InterceptCall`/`Patch` site emits a one-line ledger entry at install
  (address, opcode, orig target, OK/MISMATCH, owner). The ledger is the diff-vs-binary record.

### 1.5 Evidence collection loop (codified)

1. **Screenshot batch** (ShareX dir, timestamped; WATER §0: 24 shots 21:04:18–21:07:14).
2. **Per-image forensics** — classify geometry vs post-effect. The decisive discriminator example:
   *"hard-edged … straight water.dat quad/triangle boundaries, not a fog or full-screen post effect"*
   (WATER §4).
3. **RGB signatures** — colour/shape/edge → subsystem: black square + tiny coloured core = sprite
   batch drawn REPLACE (blend off); straight horizontal line above the horizon = transform fault;
   irregular ink-blot pools = PipeChain pass 3 (CORONA §0/§4, WATER §4).
4. **Log correlation by timestamp** — screenshot time ↔ log window; `[BUILD]` line first
   (WATER §1/§7; CORONA §2).
5. **Hypothesis ranking with kill switches** — every hypothesis gets an INI-only A/B before any code
   change (WATER §6.F: `waterParallaxEnable=0`; CORONA §4: `pipeChainEnable=0`).
6. **Fix wave → ONE build → retest** against the same screenshot set + the baseline catalogue
   (`docs/baseline/2026-10-24-screenshots/` BASE-01..BASE-16, CKPT §Wave 5).
* **Standing INI bisect levers** (CKPT §Wave 4): `texAlphaClassify`, `defaultCbShaderHygiene`,
  `stateBaselineEnable`, `pipeChainEnable`, `waterParallaxEnable`, `coronaZtest`,
  `radiosityEnable`.
* **Rule:** no fix lands without a named log signature that proves it — CORONA §5 *"Proof lines for
  the next run"* is the template (install receipts → first-hit receipt → 2 s summary → acceptance
  criterion).

### 1.6 Recovery

* **OpenCode DB read-only** — `sqlite3 -readonly "file:C:/Users/aaaaaaaaa/.local/share/opencode/opencode.db?mode=ro"`
  + the session query in README §How to resume (1,767 sessions as of 2026-10-24). Never write.
* **Checkpoint file** — `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` is the
  authoritative restore point; waves 1–5 are recorded there (CKPT).
* **Findings-MD consolidation before re-dispatch** — ALPHA header: *"Authoritative shared record for
  re-dispatched lanes. Read this FIRST — do not re-derive."* Every crashed lane's partial results are
  consolidated into a dated findings MD before new dispatch (ALPHA §Lane re-dispatch scopes).
* **Fallback** — session text dumps under `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\` (README).

### 1.7 What's missing / next instrumentation (gaps identified from the docs)

| # | Gap | Evidence | Proposed instrumentation |
|---|---|---|---|
| G-1 | Exit receipts cannot prove legacy fidelity: `PipeAlphaState` lacks `ALPHABLENDENABLE` / `ALPHATESTENABLE` / `BLENDOP` capture | MATRIX A1-A3, §3 #6 | Add the 3 fields; capture at enter (L2+L3), restore verbatim at exit; emit them in every exit receipt |
| G-2 | Constant-cache watcher covers only c0/c1 in ClassifyBegin | CKPT §Wave 3 L1 (`pc_c1chk`) | Raw-write ledger: every raw `Set*ShaderConstantF` site registers (file:line, register range); periodic RW-cache-vs-device verify for hot registers |
| G-3 | Clip-space bounds probe is water-only | WATER §6.A | Reusable helper: NDC min/max + `w` range one-shot for **every** raw `DrawIndexedPrimitiveUP` path |
| G-4 | Throttle starvation is undetectable | CORONA §3 Q3 | Per-bucket suppression counters + periodic `[throttle]` summary so "zero lines" is always decidable |
| G-5 | No full device-state diff at frame boundaries | MATRIX §4 #2; targeted probes only | Snapshot all D3D9 render states + texture stages at SceneStart/EffectsExit; diff; log deltas |
| G-6 | SilentPatch ownership is a documentation rule, not enforced | CKPT §Wave 3 | Startup probe: detect SP, list owned sites (`0x6FB621/0x70F45A/0x74872D`/shadow/mirror), refuse to patch |
| G-7 | Game-counter drift unwatched | CORONA §6 (`0xC3E038` drifts negative) | Signed/overflow watcher for counters we read |
| G-8 | No mode/preset identity line | MODES §1.4 #3, §2.4 (PushToRenderer log designed, not implemented) | Startup + on-change line printing the full effective config — prerequisite for any parity run |
| G-9 | No UNKNOWN-rate alarm | MATRIX A5 (`TAC_UNKNOWN→OPAQUE` regression); CKPT §Wave 5 fix-2 fixed starvation but no alarm | Alarm when `TAC_UNKNOWN` rate exceeds threshold |
| G-10 | Screenshot↔log correlation is manual | WATER §0/§7 | Small script: pair ShareX timestamps with `skygfx_dbg.log` windows + RGB signature extraction |
| G-11 | No WVP provenance tag per raw draw | WATER §4 (cached `_RwD3D9ActiveViewProjTransform` bug class) | Log which transform source each raw draw used (live view/proj vs cached) |
| G-12 | No parity screenshot harness | AAP §4 Phase E item 4 (4 times of day vs aap) | Fixed save/time/weather capture automation at 01:28/06:30/12:00/20:30 |

---

## 2. Parity checklist

STATUS legend: **OK** = parity, or a documented `INTENTIONAL` repair preserving legacy output ·
**DIVERGENT-INTENTIONAL** = `INTENTIONAL*` (documented, changes output, must be gated/default-off) ·
**REGRESSION** · **MISSING**. Verdicts are lifted from MATRIX (§2, §3, §4), AAP (§2, §3 D1–D16) and
PCINV (§1–§2); citations point at the source docs' sections, not re-derivations. `GS:/` = leaked
tree, `aap:/` = `backup_original/src/`, `src/` = current mod.

### 2.1 Building pipes (PS2 / Xbox / PC)

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| PS2 building cb | no PS2 renderer in GS drop; nearest FFP `GS:/pipes/PC/CustomBuildingPipeline.cpp:750-908` | `aap:/buildingPipe.cpp:166-364`, dispatch `:601`, pipe `:876` | `src/render/buildingPipe.cpp:1023-1363`, dispatch `:2563-2568`, pipe `:2865` | **OK** (AAP §2: ported, shader-for-shader + state hardening) |
| Xbox building cb | `GS:/pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:751-909` (Xbox branch is a bare wrapper; PC branch is the live cb) | `aap:/buildingPipe.cpp:368-511` | `src/render/buildingPipe.cpp:1452-1672` | **DIVERGENT-INTENTIONAL** — direct-sun term `:1574-1575` + `shaders/vs/xboxBuildingVS.hlsl:13-14,65-76`; aap has none (AAP D5; gate per AAP C3) |
| PC building FFP | `GS:/pipes/PC/CustomBuildingPipeline.cpp:750-908` + `CustomPipelinesCommon.cpp:58-125` | aap mapped `PC`→`BUILDING_XBOX` (`aap:/config.cpp:145-148`) — never PC | no `BUILDING_PC`; dead sketch `src/rw/defaultFuncs.cpp:41-116` | **MISSING** (PCINV PC-01/PC-04, G01/G02) |
| Force-black unlit | `GS:/pipes/PC/CustomBuildingPipeline.cpp:773-786` + `CustomPipelinesCommon.cpp:30-48` | none | missing in live cbs | **MISSING** (PCINV PC-12, G04) |
| FFP material + cache resync | `GS:/pipes/PC/CustomPipelinesCommon.cpp:153-262, 272-278` | partial (`aap:/pipelinecommon.cpp:195-204`) | `pipeUploadMatCol` `src/render/pipelinecommon.cpp:477-494` | **MISSING** for exact FFP (PCINV PC-13/PC-20, G05) |
| Building env stage | texgen `GS:/pipes/PC/CustomBuildingPipeline.cpp:824-862` (`TCI_CAMERASPACENORMAL`, matrix never uploaded) | FX pass `aap:/buildingPipe.cpp:326-355` | `src/render/buildingPipe.cpp:1255-1350` (PS2), `:1623-1640` (Xbox) | **OK** ported+hardened; texgen parity = PIPES §5 item 5 / L12 |
| Tags pipe | tag ref `GS:/GtaPlugins.cpp:1911-1947` | `aap:/buildingPipe.cpp:147-163` branches on `tagsBuildingPipe` | `src/render/buildingPipe.cpp:320-338` branches on `buildingPipe`; field dead (`src/core/main.cpp:5338`) | **REGRESSION** (AAP D7 / MATRIX D5 / G14 / S9) |

### 2.2 Vehicle pipes (PS2 / Xbox / PC / Neo / Env)

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| PS2 vehicle cb | no PS2 renderer; nearest `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1231-1594` | `aap:/vehiclePipe.cpp:369-551` | `src/render/vehiclePipe.cpp:896-1173` | **DIVERGENT-INTENTIONAL** — lighting source diverged (AAP D3/D4; sun force `:557`) |
| Xbox vehicle cb | `GS:/pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1087-1579` — live path has **no env passes** (env block `#if 0` `:1207-1474`) | `aap:/vehiclePipe.cpp:774-944` (PC-style env passes) | `src/render/vehiclePipe.cpp:1390-1636` | **DIVERGENT-INTENTIONAL** — we draw env passes like aap/PC, not shipped-Xbox (PIPES §5 item 14 decision record); interior dampening `:1457-1467` (AAP D11) |
| PC vehicle | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1231-1594` | trampoline `aap:/gta.cpp:141` | native trampoline `0x5D9900` (`src/rw/gta.cpp:163`, `vehiclePipe.cpp:3206-3208`) | **OK** (native, exact — PCINV PC-23) |
| Neo | none | `aap:/neoCarpipe.cpp:206-463` | `CreateShaders` nulls both VS (`src/render/neoCarpipe.cpp:475-486`); dispatch reroutes `CAR_NEO` (`vehiclePipe.cpp:3214-3223`) | **MISSING** (AAP D16 / PCINV PC-49 G15 / L17 — restore or deprecate) |
| Env/PBR | nearest mobile env `GS:/pipes/MBR/CustomCarEnvMapPipeline.cpp:809` | `aap:/vehiclePipe.cpp:1246-1398` | `src/render/vehiclePipe.cpp:2012-3146` | **OK** (ported; out of parity scope) |
| Remap-colour clear | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1513-1534` | missing in aap shader cb | native PC has it; shader cbs do not | **MISSING** for shader cbs (PCINV PC-27 G08 / L9) |
| Blown-up gate | `:1263-1275, 1384, 1432` | none | Xbox cb only (`:1423-1425`) | **MISSING** in PS2/Env cbs (PCINV PC-32 G10 / L10) |
| Spec quality gate | `:1329-1337` | missing | missing | **MISSING** (PCINV PC-30 G11) |
| Device caps gate | `:1690-1724` | none | none | **OK** as intentional drop (PCINV PC-39: safe on modern HW; optional one-shot log) |
| Pass bits | `CARFX_*_PASS_BIT` (`GS:/pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:43-47`) | same | `src/render/vehiclePipe.cpp:1091,1254,1480` | **OK** (contract must not change) |

### 2.3 DN day/night vertex colours

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| PC shipped CPU blend | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1513-1576` + 16-frame scheduler `:283-345, :1498-1505` | shader DN (`aap:/buildingPipe.cpp:103-130, 667-842`) | interpolation patched to RET at `0x5D7200` (`src/render/buildingPipe.cpp:2918`); shader DN `:2650-2831` | **DIVERGENT-INTENTIONAL** — we ship the beta DN-VS path; CPU path = PCINV PC-06/07 G06, opt-in `pcDnCpuBlend` |
| Xbox VS contract (night in normals, c4/c5) | `GS:/pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112, 455-525`; `DNshader.vsh:1-28` | two-COLOR-stream shader | our DN instance builds COLOR0+COLOR1 (`src/render/buildingPipe.cpp:2714-2725`) | **MISSING** (PIPES §5 item 1, P0 — normal-packed contract not ported) |
| DN balance param | `GS:/pipes/CustomBuildingRenderer.cpp:194-259` | reads same global | aliased `0x8D12C0` (`src/rw/gta.cpp:63`); `setDnParams` `:228-289` + cutscene repair | **OK** + **DIVERGENT-INTENTIONAL** (repair documented `[dnbal_repair]`, PCINV PC-08) |
| Extra-vert-colour plugin | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1597-1619` | accessors `aap:/buildingPipe.cpp:611-656` | reads offsets (`src/rw/gta.cpp:62`) but never registers | **MISSING** verify (PIPES §5 item 3, P0) |
| DN stream-size bug | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1671-1707` | — | not ported | **OK** (do-not-restore, PCINV §4.1) |

### 2.4 Env map (texgen, env1/env2, scroll)

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Building texgen | `GS:/pipes/PC/CustomBuildingPipeline.cpp:828-861` | shader `envXform` | transform-based (`src/render/buildingPipe.cpp:1256-1290, 1624-1650`) | **MISSING** exact texgen (PIPES §5 item 5 / L12 `bldEnvTexGen`) |
| Env1 (clump LTM, sector snap) | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:790-829` | `aap:/vehiclePipe.cpp:143-156 / :229-240` | `Env1Xform` `:322-345`; `Env1Xform_PC` `:404-422` (only Xbox cb selects PC variant) | **MISSING** selector (PCINV PC-24 G12 / L5) |
| Env2 scroll | `:837-1076` (sector parity, U accum, V clamp [0,0.1], flip) | `aap:/vehiclePipe.cpp:158-226 / :243-303` | `Env2Xform_PC` `:424-487` | **MISSING** literal port (PIPES §5 item 4, P1) |
| Env2 by name `'x'` | `GS:/pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:367-371` | same via vanilla | `RwTextureRead_HACK` `src/render/vehiclePipe.cpp:3330-3335` | **OK** |
| Env light mult | `:1242` (×1.85 spec) | `aap:/vehiclePipe.cpp:428, 504, 511` | aliased `0x8D12D0`; `:953, 1227` | **OK** |

### 2.5 Specular

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| PC FFP spec | light index 1, `LOCALVIEWER=FALSE`, `SPECULARMATERIALSOURCE=MATERIAL`, `min(2*spec*mult,1)`, power `100*spec` (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:42, 1348-1375, 1567-1573`) | shader spec `aap:/vehiclePipe.cpp:516-520` | PS2 `:1142-1146`; Xbox `SetupMaterial_Xbox` `:1342-1388`; native PC exact | **MISSING** exact constants in shader cbs (PCINV PC-28 G09 / L8) |
| Spec mask s2 | not used by PC cb (`:1983-2055` accessors only) | binds s2 | PS2 binds s2 `:1142-1150` | **OK** (aap extension, documented non-PC — PCINV PC-29) |
| Spec save/restore | `:1567-1573` (light not restored on nVidia — comment) | not saved | not in shader cbs | **MISSING** (PCINV PC-35) |

### 2.6 Dual pass

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Building/vehicle dual pass | not in PC pipes (single draw); Xbox dual-pass block `#if 0` (`GS:/pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1207-1474`) | `aap:/main.cpp:219-260`; used `buildingPipe.cpp:324/:504`, `vehiclePipe.cpp:462/:637/:936` | `src/core/main.cpp:208-259` hardened (clamp 254); building `:1252/:1650/:2399`; vehicle `:1083/:1246/:1601` | **OK** (ported+hardened; aap-only feature, documented — AAP §2) |
| Ped dual pass | n/a | `aap:/main.cpp:422-436` | restored `src/core/main.cpp:1400, :1711-1734` | **OK** (MATRIX P1/P2 `INTENTIONAL`) |
| Default-pipe gate | n/a | gate only `zwrite && dualPassDefault` (`aap:/main.cpp:416-421`) | extra `hasAlpha` gate `src/core/main.cpp:1678-1687` | **REGRESSION** (MATRIX P8 / S5) |

### 2.7 Alpha / blend / alpha-test doctrine

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Doctrine | pipes never write `ALPHABLENDENABLE/ALPHATESTENABLE/ALPHAFUNC/ALPHAREF/ZENABLE/ZFUNC/COLORWRITE/BLENDOP`; only rw `VERTEXALPHAENABLE` + `ALPHATESTFUNCTION/REF` + `ZWRITE` (MATRIX §2.3 preamble; `DefinedState` GREATER@2, CORE §5.2) | same (`aap:/main.cpp:224-225` read-only) | forces at every boundary; exit derives blend from `vtxAlpha`, forces test on, hard `BLENDOP=ADD` (`src/render/pipelinecommon.cpp:1504-1686`) | **REGRESSION** (MATRIX A1/A3/A4-exit; fix = capture+restore, S3/S4) |
| `TAC_UNKNOWN` | legacy fell back to `hasAlpha` | n/a | → OPAQUE (`src/render/texalpha.cpp:703-802`; `buildingPipe.cpp:2162-2164`; `vehiclePipe.cpp:2314-2319`) | **REGRESSION** (MATRIX A5; restore fallback) |
| Ref 0→1 bump | none | none | `pipeEnterAlphaMode` `:1504-1509` | **OK** (`INTENTIONAL` — black potted plant fix) |
| Ped GE@1 | none | none | ped contract `src/core/main.cpp:981-991` | **DIVERGENT-INTENTIONAL** (MATRIX P4; narrow per §3 WS-1.1) |
| Tags ONE/ZERO | n/a | `aap:/buildingPipe.cpp:151-162` | `:320-337` | **OK** (values identical; selector tracked in 2.1) |
| `alphaTestLegacy` knob | exe-owned enable/func/ref | exe-owned | no config knob | **MISSING** (MODES S4) |

### 2.8 Z-write flags (`MODEL_DONT_WRITE_ZBUFFER`)

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| DONT_WRITE_ZBUFFER | `GetDontWriteZBuffer()`/`MODEL_DONT_WRITE_ZBUFFER` toggles ZW + alpha-ref in `RenderEntity` (`GS:/modelinfo/BaseModelInfo.h:140,174,341-342`; `GtaPlugins.cpp:1894-1958`) | never touches ZW at cb entry | forces `ZWRITE=TRUE` at cb entry: `src/render/buildingPipe.cpp:2514`, `vehiclePipe.cpp:3188` | **REGRESSION** (PCINV PC-16 G07 — defeats the PC-only feature; detect via `CVisibilityPlugins__GetModelInfo` + `GetDontWriteZ` `src/rw/gta.h:78`) |
| Decal ZWRITE policy (PBR) | dual-pass legs only | legs only | per-mesh DECAL policy `src/render/buildingPipe.cpp:2108-2210` | **DIVERGENT-INTENTIONAL** (MATRIX D2; PBR-only, do not port to PS2/Xbox) |
| Ped ZWRITE force | n/a | inside dual legs only (`aap:/main.cpp:425`) | every ped atomic `src/core/main.cpp:1475-1488` + sticky exit `:1776-1788` | **REGRESSION** (MATRIX P3 / S6) |

### 2.9 Cull modes

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Pipe cull writes | none in pipes; `RenderEntity`/`RenderBoatAlphaAtomics` set/restore (`GS:/GtaPlugins.cpp:1868-1871, 1915-1929`) | grass only (`aap:/main.cpp:372-374/:402`) | `pipeEnterCullMode` preserves NONE/FRONT/BACK (`src/render/pipelinecommon.cpp:1285-1319`) | **OK** (MATRIX C1/C6 `INTENTIONAL`) |
| Inner BACK re-asserts | none | none | `src/render/vehiclePipe.cpp:2080-2088`; `buildingPipe.cpp:1876-1880` | **REGRESSION** (MATRIX C2/C3) |
| World-baseline cull park | none | none | `pipeApplyWorldBaseline` pins CULLBACK (`src/render/pipelinecommon.cpp:1731-1804`) | **DIVERGENT-INTENTIONAL** (MATRIX C4; remove cull from baseline or default `stateBaselineEnable=0`) |
| Grass restore | n/a | restores saved value (`aap:/main.cpp:402`) | always BACK (`src/core/main.cpp:806-807`) | **OK** (`INTENTIONAL`, improvement noted — MATRIX C6) |

### 2.10 Fog

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| FX-leg fog | `FOGCOLOR=0` around FX draws (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1304-1305, 1344-1345`; DN `:1337-1340`); `DefinedState` fog off + timecycle colour (CORE §5.2) | `aap:/buildingPipe.cpp:348-350/:534-536`; vehicle `:534-536` | building `src/render/buildingPipe.cpp:1304-1321`; `vehiclePipe_fxAdditiveBlend` `:674-760` | **OK** (ported+hardened, all 3 layers — AAP §2) |
| PS2 fog placement | PS2: inside `RenderScene` (`Main.cpp:3411-3430`); PC/Xbox: inside `RenderEffects` (`:3621-3640`) | follows PC | single PC hook order | **DIVERGENT-INTENTIONAL** (CORE §8 item 8 — PS2-mode ordering switch required) |

### 2.11 Coronas / CPointLights

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Render order | `CCoronas::Render` in `RenderEffects` (`Main.cpp:3595`); reflections/sun in `RenderScene` (`:3266, :3394`); `CPointLights::RenderFogEffect` leaves state dirty **by design** (`PointLights.cpp:379-391`, restore commented `:613-618`) | `coronaZtest` patch only (`aap:/main.cpp:330-346`); env coronas (`aap:/envmap.cpp:318-342`) | patch `src/core/main.cpp:334-348`; env coronas `:471-509`; shield `:2368-2432` + flush force | **DIVERGENT-INTENTIONAL** (MATRIX X2/X3 `INTENTIONAL*`; gate `coronaStateShield` default 0) |
| `coronaZtest` | n/a (exe z-test) | INI patch `0x6FB17C` | same patch; **modes never set it** | **REGRESSION** (mode wiring — AAP §2 corona row / A3) |
| Producer ≠ rasteriser | shared batch `0x70CF20`, 15 flush entry points (CORONA §1.1) | no state mgmt | window-scoped shield failed; unconditional flush force landed wave-4 (CKPT §Wave 4) | **OK** (fix landed; keep gate per X3) |

### 2.12 Water

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Render placement | `CWaterLevel::RenderWater` inside `RenderScene` with cull NONE/BACK wrapper (`Main.cpp:3301-3308, 3402-3409`); PS2-only `RenderWaterFog` placement | waterStyle Xbox core | `waterPipe.cpp` Xbox core + V/IV tiers; wave-5 clip-space fix (cached view-proj → live) | **REGRESSION → fix landed** (verify: `[water] wvp` NDC bounds; `waterParallaxEnable=0` A/B — WATER §6) |
| Depth copy tier≥2 | n/a | n/a | `PostFX_CopyDepthToTexture` contract kept (`postfx_ssao.cpp:288-395`) | **OK** (WATER §3/§6.E) |
| Buffer sanity | n/a | n/a | `WbBufferCountsSane` (CKPT §Wave 5 item 1) | **OK** |

### 2.13 Shadows

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Shadow passes | inside `RenderScene` only if `!CMirrors::bRenderingReflection` (`Main.cpp:3316`); Z-bias hack `:3319-3339/:3354-3358`; real-time ped shadows PC/Xbox only (`Ped.cpp:8877-8958`) | `pedShadows`/`stencilShadows` toggles | shadow contract wrapper (CKPT §Wave 5 item 6, byte-verified sites `0x53E0BE/C3/C8`) | **OK** (new subsystem, not a parity target — MATRIX §4 #6); keep the mirror gate |
| Shadow blobs residual | n/a | n/a | INTZ stencil hypothesis open (CKPT §Wave 5 "Next") | **MISSING** (open) |

### 2.14 Lighting / ambient path

| Item | Rockstar original | aap legacy | Current mod | STATUS |
|---|---|---|---|---|
| Ambient source | `pAmbient`/`pDirect` + 6 extras; `SetLightsWithTimeOfDayColour` before render list (`lights.c:60-159`; `Main.cpp:4152`) | buildings `timecycle × CCoronas::LightsMult` (`aap:/buildingPipe.cpp:57-59`); vehicles `pAmbient->color` (`aap:/vehiclePipe.cpp:348/:816`); `rpLIGHTLIGHTATOMICS` gate `:349` | `GetTimecycleAmbientPBR` + `ClampAmbientToSky` + `ambientMult` (`src/render/pipelinecommon.cpp:97-213`); force sun `:557`; interior dampening `:1457-1467`; ped override+floor | **DIVERGENT-INTENTIONAL** (AAP D3/D4/D5/D11; MATRIX P5/P9; fix = `legacyAmbient` gate AAP C1/C2 + `pedAmbientOverride` gate) |
| Upload timing | `SetLights` before `ConstructRenderList`; `PS2AllRecalculateLighting` after `PreRender` (`Main.cpp:2917`) | same globals | `pipeUploadPBR` fed after `SetLights` (CORE §8 item 5) | **OK** (verify per run) |
| Scorched gate | `rpLIGHTLIGHTATOMICS` cleared (`lights.c:205-220`) | gated | force-upload ignores it | **REGRESSION** (AAP D4 / C2) |

---

## 3. "Everything they shipped" completion list

Ordered backlog to reach full Rockstar + aap parity. **WS-0 is the umbrella workstream**; the rest
are ordered by visual impact / dependency. Merged from MATRIX §3 (top-10), PIPES §5 (14 items),
PCINV §2/§3 (18 gaps G01–G18), AAP §4 (phases A–E), MODES §6.1 (phases P0–P7), ALPHA (open items),
CKPT (open queue).

### WS-0 — Six-mode preset overhaul (umbrella; MODES §2, §6.1)

One `ModeId` (PS2 / XBOX / PC_VINTAGE / GTAIV / GTAV / MODERN / CUSTOM + NONE), one `ModeProfile`
with an ownership mask, one `ApplyMode()`, one renderer-push listener, `[Modes]`/`[Mode.*]`
persistence.

0.1 P0 instrumentation (log every mode apply; no behaviour change) →
0.2 P1 canonical model (`ModeField`/`ModeValues`/`ModeProfile`/`g_modeProfiles[6]`; delete dead
`*_mode.cpp` files) →
0.3 P2 stop-the-fighting (E1–E4: remove rule-3 pipe rewrite, `qualityPreset` pipe writes,
`pipelineOverride`/pipeline-combo pipe writes, silent latch clear) →
0.4 P3 overrides + persistence (sparse diff, badges, per-config-slot base) →
0.5 P4 vintage parity P0 fields (L1 simpleStochastic, L2 detailTileScale, L3 dnVertexColors,
S1 legacyAmbient, S2 pedAmbientOverride, S3 pipeAlphaExactRestore, S4 alphaTestLegacy, S9
tagsBuildingPipe) →
0.6 P5 PC-vintage P1 (L4–L10, L12, L15, S5–S8, L17 decision) →
0.7 P6 beta + V probe (L11, L13, L14, L16, S10, L18; `MODE_GTAV` probe-gated) →
0.8 P7 cleanup (dead globals, stale citations, duplicate shader sources).

### WS-1 — State-fidelity restorations (MATRIX §3 top-10, in rank order)

1.1 Narrow the ped opaque contract to an alpha-ref repair only (default off / rename
`pedAlphaRefRepair`) — #1 ·
1.2 `TAC_UNKNOWN` → legacy `hasAlpha` fallback in both PBR mesh loops — #2 ·
1.3 Remove the forced blend/alpha-test block from the default-pipe callback (keep depth repair) — #3 ·
1.4 Gate the ped `pAmbient` override (`pedAmbientOverride`, default 0) — #4 ·
1.5 Ped ZWRITE force only inside the dual-pass legs — #5 ·
1.6 `pipeExitAlphaMode` exact restore (capture `blendEnable`/`alphaTestEnable`/`blendOp`) — #6 ·
1.7 Remove the two unconditional BACK cull re-asserts after IBL (C2/C3) — #7 ·
1.8 Gate the corona raw shield (`coronaStateShield` default 0) + restrict the flush force to
corona/point-light windows — #8 ·
1.9 `legacyAmbient` source switch (raw timecycle buildings, `pAmbient` vehicles/peds) — #9 ·
1.10 Restore the legacy default-pipe dual-pass gate (drop the `hasAlpha` probe) — #10 ·
1.11 Honourable mentions: `tagsBuildingPipe` selector, grass cull restore, stale comment/doc-drift
fixes (MATRIX §3 tail).

### WS-2 — Vintage PS2/Xbox mode completion (AAP §4)

2.1 A1 real mode profiles (replace `profilePS2`/`profileXbox` stubs; delete dead `ps2_mode.cpp`/
`xbox_mode.cpp`) · 2.2 A2 mode listener (`resetValues()` + tags sync + log) · 2.3 A3 per-mode
filter/corona values (PS2: `COLORFILTER_PS2`+`coronaZtest=0`; Xbox: `COLORFILTER_PC`+`coronaZtest=1`) ·
2.4 B1 re-add `simpleStochasticPS` (`IDR_SIMPLESTOCHASTICPS` = 278) · 2.5 B2 INI aliases
(`PC`→`BUILDING_XBOX`, `Spec`→`CAR_SPEC`) · 2.6 B3 `tagsBuildingPipe` contract · 2.7 C1 legacy
ambient path · 2.8 C2 `rpLIGHTLIGHTATOMICS` gate for PS2/Xbox vehicles · 2.9 C3 Xbox direct-sun
building term as a parity toggle · 2.10 D1–D3 hygiene (stale shader duplicates, mode-file citations,
Neo decision).

### WS-3 — Legacy PC mode (PCINV §3 ranks; release gate = ranks 1–4 + 6)

3.1 G01+G18 mode plumbing (`BUILDING_PC`/`PIPELINE_PC`, `buildingPipe=PC`, preset, `pipelineOverride`
range) · 3.2 G02+G03+G05 PC FFP building (render cb, create pipe, stage0/1, FFP material, cache
resync) · 3.3 G04+G07 force-black unlit + ZW/`DONT_WRITE_ZBUFFER` fix · 3.4 G06 CPU DN blend +
16-frame scheduler (conditional unpatch of `0x5D7200`) · 3.5 G08–G12 shader-PC vehicle legs (remap
clear, FFP spec light, blown-up gate, quality gate, env1 scroll selector — optional) · 3.6 G13+G18
plain stochastic + aliases · 3.7 G14 tags · 3.8 G15 Neo (restore CSOs or remove the option) ·
3.9 G16 dirt levels · 3.10 PC-60/PC-58 beta DN-VS exactness (`DNshaderPC` packing, ×3 prelit).
Do-not-restore list (PCINV §4) is binding: DN stream bug, RGBA stride bug, header-guard collision,
dead lighting CB, `#if 0` spec/env variants, env texture-matrix "fix", blown-up darkening.

### WS-4 — Rockstar pipes port backlog (PIPES §5, 14 items)

4.1 **P0** DN Xbox vertex-shader contract + normal-packed night colour · 4.2 **P0** DN CPU
interpolation scheduler (verify `0x5D7200` target first; `0xC02C1C` global) · 4.3 **P0** verify the
extra-vert-colour plugin is still attached after the `0x5D7100` hook · 4.4 **P1** Env-map 2 scroll
parity (literal port) · 4.5 **P1** building env texgen (not a texture matrix) · 4.6 **P1** specular
pass constants · 4.7 **P1** vehicle remap-colour fix · 4.8 **P1** blown-up vehicle gating ·
4.9 **P1** device caps gate · 4.10 **P2** `xboxallinone.c` as reference only (D3D9 constant
save/restore audit around skinning) · 4.11 **P2** pool allocators + default data · 4.12 **P2**
plugin chunk IDs + disk payload · 4.13 **P2** naming/structure refactor (PIPES §4.7) · 4.14 **P2**
Xbox vehicle env decision record.
Non-goals (PIPES §5.1): MBR, `#if 0` instance callbacks, DN stream bug, RGBA stride bug.

### WS-5 — Modern/PBR residuals (ALPHA + CKPT open queue)

5.1 `main()` returns raw `tex.a` (drops vertex alpha) · 5.2 DXT3 empty-alpha holes on `main_building`
(per-mesh alpha-test bypass when `!hasAlpha` + buildingPipe L2 `ALPHABLENDENABLE` mirror) ·
5.3 `main_mobileVehicle` env-coupled alpha guard (`:862`) · 5.4 water veil verification (wave-5 fix)
+ `cf_gain` knee patch (move knee into `TonemapPass.hlsl` on measured lum; align `tonemapMaxExposure`
clamps) · 5.5 Forward+ intermittent (`capDrops`, every-tile-maxed grid test) · 5.6 SSS dead when
`ssaoEnable=0` (no depth) · 5.7 `stateBaselineEnable` consumer · 5.8 INI save gaps (8 keys) ·
5.9 commit checkpoint (48+ dirty files) · 5.10 shadow INTZ→D24S8 residual hypothesis.

### WS-6 — Debug instrumentation (§1.7, items G-1…G-12)

Land in probe-only waves: zero behaviour change, build-size delta only, each probe ships with its
own proof line (CORONA §5 template).

---

## 4. Definition of done + test gates

Global gate for every wave: **ONE build → binary string verify → `[BUILD]` line matches → user
retest against the baseline screenshots** (`docs/baseline/2026-10-24-screenshots/`). No wave is
"done" without its named log signature proving the fix (CORONA §5 template).

### WS-0 (mode overhaul)
* **Log:** `[mode] old -> new pipes=B%d/V%d filter=%d ps2mod=%d dual=%d detail=%d stoch=%d env=%d
  corona=%d` on every apply; **zero** `[Config] pipeline re-derived` lines after an apply; mode
  survives restart (`[Modes] active`); tweak survives A→B→A; INI reload (F11) keeps mode.
* **Screens:** MODES §6.2 matrix — all rows pass per mode (pipes, filter, timecyc, detail,
  stochastic-binds-`simpleStochasticPS`, DN at 01:28/06:30/12:00/20:30, env2 scroll both directions,
  spec in shade, remap paint, dual-pass fringe, alpha-test after pipe exit, ped alpha, interior
  lighting, coronas, reflection resolution).

### WS-1 (state fidelity)
* **Log:** `[PedOpaque] contract repaired divergent entry` → 0 unexpected; `[BUILDING]/[PED]/
  [VEHICLE] ZWRITE off/desynced on entry` repairs → **0 per 10-min run**; `[Pipeline]
  pipeExitAlphaMode` shows captured (not derived) restore values; `blend_leak` → **0**; `[Cull]`
  shows no BACK flips inside mirror/shadow spans; `[CoronaDraw]` pre-force `blend=0` observations → 0.
* **Screens:** peds solid incl. fading/cutscene; props/pickups/markers correct; no ghost sprites
  after the world pass; mirror/shadow spans keep NONE/FRONT; no black-quad coronas.

### WS-2 (vintage modes)
* **Log:** mode-switch line lists all fields; `CreateShaders` non-NULL for `ps2BuildingVS`,
  `ps2BuildingFxVS`, `ps2BuildingWindVS`, `xboxBuildingVS`, `xboxBuildingPS`,
  `xboxBuildingStochasticPS`, `ps2CarFxVS`, `ps2EnvSpecFxPS`, `xboxCarVS` (AAP §4 E2); `[BPIPE]`
  binds `simpleStochasticPS` for stochastic-without-detail (E3); `[BUILDING]`/`[VEHICLE]` probes
  quiet for 10 min (E5); INI `buildingPipe=PC` / `vehiclePipe=Spec` resolve, not PBR (E6).
* **Screens:** PS2 vs aap and Xbox vs aap at 01:28 / 06:30 / 12:00 / 20:30 — `buildingAmbient` and
  VS c4/c5 logged and matching (E4); stochastic buildings not flat; PS2 coronas z-test off, Xbox on.

### WS-3 (legacy PC)
* **Log:** `Config: pipeline=… buildingPipe=… vehiclePipe=…` resolves PC (PCINV §5.1); no
  "forcing TRUE" for `DONT_WRITE_ZBUFFER` models (§5.5); unlit prop renders black (§5.4);
  `[dnbal_repair]` only fires in cutscenes with vision FX off (§5.3).
* **Screens:** env-mapped building shows a static camera-space-normal reflection (§5.2); dawn/dusk
  cross-fade smooth; fence/hedge does not occlude; env1 snaps at sector edges, env2 scrolls and
  flips; wrecked cars lose reflection; state-leak checklist §5.8 — after a frame, `ZWRITEENABLE`,
  `ALPHABLENDENABLE`, `SRCBLEND/DESTBLEND`, `ALPHAFUNC/ALPHAREF`, `CULLMODE`, `TEXTUREFACTOR`,
  stage1/2 `COLOROP` equal pre-frame values.

### WS-4 (Rockstar pipes port)
* **Log:** `ms_extraVertColourPluginOffset` non-zero at runtime; global `0xC02C1C` == our DN pipe;
  DN Xbox-emulation uploads c4/c5 exactly once per atomic, c0–c3 not clobbered by skinned draws;
  `CARFX_ENVMAP2_PASS_BIT` honoured only when `rpGEOMETRYTEXTURED2`; specular light disabled after
  each mesh (PIPES §6, items 1–7).
* **Screens:** `Env2Xform_PC` output matches the original for a vehicle crossing a sector boundary at
  `x = ±WIDTH_OF_SECTOR`; building env stage uses texgen, not a texture matrix.

### WS-5 (modern residuals)
* **Log:** `[FlushShield] blendOff=0` after the corona fix (`forced` == flush count); `[water] wvp`
  NDC `y` below horizon and `w > 0`; `cf_gain` > 0 whenever `sceneLuma` > knee floor 0.10; Forward+
  `collected=` non-zero stretches with `tiles lit>0`; `texalpha` budget near zero and UNKNOWN rate
  below alarm threshold.
* **Screens:** no black squares with coloured cores; no water veil over land/sky; no pastel washout;
  no ink-blot pools; distant objects lit on fallback frames.

### WS-6 (instrumentation)
* **Log:** new probes land with zero behaviour change (build-size delta only); `[throttle]` summary
  proves no starvation; device-state diff shows only expected deltas; `[BUILD]`/install ledgers
  complete.

---

## 5. Sources

PIPES · CORE · AAP · MATRIX · PCINV · MODES · CORONA · WATER · ALPHA · CKPT · README (full paths in
the table at the top). Every behavioural claim above is traceable to the cited doc section; where a
source doc flags dead/contradictory code (PIPES §3.7, PCINV §4) it is carried forward as a
do-not-restore rule rather than silently normalized.

*End of document.*
