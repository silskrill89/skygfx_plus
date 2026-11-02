# Ped Integrity Verification — build 2,395,136

**Date:** 2026-10-26
**Scope:** READ-ONLY verification. No source edited. This file is the only write.
**Question:** is the ped pipeline corrupting itself, or is it a victim of external
state/ordering? The user's stance: "rinse back whole ped pipe, it shouldn't be
corrupted, so whatever is deeper in code affecting the order is doing it all."

**One-line verdict:** the skin-ped **draw** is internally consistent in this build
and is being repaired on every sampled entry. The ped is a **victim**: 96/96 sampled
entries were already divergent when the ped branch captured them, the shared rw
cache is parked at `ALPHAREF=100` / `VERTEXALPHA=1` by earlier passes, and the ped
exit faithfully re-emits that per-layer split. The only ped-internal contradiction
is order-of-writers: with `dualPassPed=1` the dual pass overwrites the contract's
`GREATEREQUAL@1` with `@128` before the actual draw.

---

## 0. Evidence base

| Item | Value |
|---|---|
| Running ASI | `[BUILD] size=2395136 mtime=2026-10-26 01:11:54 built=Oct 26 2026 01:11:06` |
| Log | `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (3,874,244 bytes / 38,635 lines; 89 `[PedRS]` whole file) |
| Analysis window | `01:12:26.118` → `01:19:31.145` (30,661 lines). Lines after `01:19:31` are a 35 s freeze (`01:20:06`) + tail to `01:21:26`; excluded from counts. |
| Mode | `[Mode] engage Xbox owned=79 pipes=2/1/2 cf=2`; `[Mode] active=Xbox (from INI)` |
| Config (INI + log receipts) | `pedOpaqueContract=1`, `pedCullMode=-1`, `pedAlphaTestEnable=1`, `dualPassPed=1`, `zwriteThresholdPed=128`, `pedShadows=-1`, `stencilShadows=-1` |
| Counts in window | `[PedRS]` 67, `[PedOpaque]` 97, `[PED]` 77, `[PedTex]` 1, `[PedCull]` 1, `[PedAlphaTest]` 1, `[PedDual]` 2, `[CloudShield]` 2 |
| Source snapshot | `src/core/main.cpp` md5 `a8a1f1bc0ae16b872373ed052cb85382`, 392,111 bytes, mtime 01:09 (older than the 01:11 build → matches). `src/render/pipelinecommon.cpp` md5 `8da92ac15cece96eb07565ac9e9b8efb`, mtime 00:11. |

Build/config identity is consistent. The log is this build, this config.

---

## 1. Ped path — every state write in order, with layer

Layer model (same as the state-leak audit):

* **L1** = rw render-state cache (`RwRenderStateSet/Get`)
* **L2** = RW D3D9 driver cache (`RwD3D9SetRenderState`, `pending[]`/`applied[]`, `_rwD3D9RenderStateFlushCache()`)
* **L3** = raw `IDirect3DDevice9::SetRenderState`

`myDefaultCallback` is `main.cpp:1625`; the ped branch is `main.cpp:1751-1880`
(entry) and `2111-2219` (exit). `pipe == skinPipe` is the only ped test
(`main.cpp:1643`). All writes below are **inside** the ped span unless noted.

| # | Site | L1 (rw cache) | L2 (RW driver cache) | L3 (raw device) |
|---:|---|---|---|---|
| 0 | `dodual` gate `main.cpp:1698` | — | — | — |
| 1 | `pipeEnterCullMode()` `main.cpp:1776` → `pipelinecommon.cpp:1245/1201` | `CULLMODE` park → sanitized raw saved mode | `D3DRS_CULLMODE` park → target | `D3DRS_CULLMODE` target |
| 2 | optional `pedCullMode` lever `main.cpp:1792-1799` | **not taken** (`-1`); would be `CULLMODE` park → forced | same | same |
| 3 | `pipeEnterAlphaMode()` `main.cpp:1809` → `pipelinecommon.cpp:1523` | `SRCBLEND=SRCALPHA`, `DESTBLEND=INVSRCALPHA`, `VERTEXALPHA=FALSE`; `ALPHAREF 0→1` if the cached ref was 0 | `SRCBLEND`, `DESTBLEND`, `BLENDOP=ADD`; `ALPHABLENDENABLE=FALSE`; `ALPHAREF=effRef`, `ALPHAFUNC=cached`, `ALPHATESTENABLE=TRUE`; `SEPARATEALPHABLENDENABLE=FALSE`, `SRCBLENDALPHA`, `DESTBLENDALPHA`, `BLENDOPALPHA=ADD`, `COLORWRITEENABLE=ALL` | `_rwD3D9RenderStateFlushCache()` first; then `SEPARATEALPHABLENDENABLE`, `SRCBLENDALPHA`, `DESTBLENDALPHA`, `BLENDOPALPHA`, `COLORWRITEENABLE`, `ALPHABLENDENABLE=FALSE`, `SRCBLEND`, `DESTBLEND`, `BLENDOP=ADD`, `ALPHAREF`, `ALPHAFUNC`, `ALPHATESTENABLE=TRUE` |
| 4 | ZWRITE guard `main.cpp:1814-1827` | `ZWRITE=TRUE` | `D3DRS_ZWRITEENABLE=TRUE` | `D3DRS_ZWRITEENABLE=TRUE` |
| 5 | `pipeForceAlphaBlock()` `main.cpp:1832` → `buildingPipe.cpp:543` | validate + rewrite only if changed: `ALPHATESTFUNCTION`, `ALPHATESTFUNCTIONREF` (0→1), `SRCBLEND`, `DESTBLEND`, `VERTEXALPHA` | 12 states: `ALPHATESTENABLE=TRUE`, `ALPHAREF`, `ALPHAFUNC`, `SRCBLEND`, `DESTBLEND`, `BLENDOP=ADD`, `ALPHABLENDENABLE`, `SEPARATEALPHABLENDENABLE=FALSE`, `SRCBLENDALPHA`, `DESTBLENDALPHA`, `BLENDOPALPHA=ADD`, `COLORWRITEENABLE=ALL`; flush | same 12 through raw `SetRenderState` (write only when device ≠ L2 value while the `bld_alpha` throttle is due; otherwise unconditional) |
| 6 | saved alpha-pair restore `main.cpp:1841-1848` | `ALPHATESTFUNCTION=savedAlpha.func`, `ALPHATESTFUNCTIONREF=savedAlpha.ref` | `ALPHAFUNC`, `ALPHAREF` same | `ALPHAFUNC`, `ALPHAREF` same |
| 7 | `pedOpaqueEnforce()` `main.cpp:1874` → `main.cpp:1077` | `VERTEXALPHA=FALSE`, `ZWRITE=TRUE`, `ZTEST=TRUE`; if at: `ALPHATESTFUNCTION=GREATEREQUAL`, `ALPHAREF=1` | `ALPHABLENDENABLE=FALSE`, `ZWRITE=TRUE`, `ZENABLE=TRUE`, `ZFUNC=LESSEQUAL`; if at: `ALPHATESTENABLE=TRUE`, `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=1` | identical to L2 |
| 8 | `pedStage0Resync()` `main.cpp:1879` → `main.cpp:1582` | `TEXTURERASTER` park (white or NULL) → cached raster | — (no explicit L2 write) | `SetTexture(0, resolved)` only if a plausible ext pointer was resolved |
| 9 | default shader hygiene `main.cpp:1917-1922` (runs in every default callback) | — | `SetPixelShader(NULL)`, `SetVertexShader(NULL)` | `SetPixelShader(NULL)`, `SetVertexShader(NULL)` |
| 10 | ambient override `main.cpp:1930-2005` | CPU-only (`pAmbient->color`), restored at 2101 | — | — |
| 11 | dual pass leg 1 `main.cpp:2063-2065` | `ZWRITE=TRUE`; `ALPHATESTFUNCTION=GREATEREQUAL`, `ALPHATESTFUNCTIONREF=128` | `ZWRITE=TRUE`; `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=128` | same |
| 12 | `RxPipelineExecute` leg 1 `main.cpp:2065` | — (draw) | — | — |
| 13 | dual pass leg 2 `main.cpp:2066-2068` | `ZWRITE=FALSE`; `ALPHATESTFUNCTION=LESS`, `ALPHATESTFUNCTIONREF=128` | `ZWRITE=FALSE`; `ALPHAFUNC=LESS`, `ALPHAREF=128` | same |
| 14 | `RxPipelineExecute` leg 2 `main.cpp:2068` | — (draw) | — | — |
| 15 | post-dual restore `main.cpp:2069-2070` | `ZWRITE=TRUE`; `ALPHATESTFUNCTION/REF` = post-enforce values (`GREATEREQUAL@1`) | same | same |
| 16 | `pipeExitAlphaMode(savedAlpha)` `main.cpp:2112` → `pipelinecommon.cpp:1747` | `VERTEXALPHA`, `SRCBLEND`, `DESTBLEND`, `ALPHATESTFUNCTION`, `ALPHATESTFUNCTIONREF` per pre-entry capture; optional `VERTEXALPHA=TRUE` mirror when captured dev enable=TRUE and cache said FALSE | `ALPHABLENDENABLE`, `SRCBLEND`, `DESTBLEND`, `BLENDOP`, `ALPHAREF`, `ALPHAFUNC`, `ALPHATESTENABLE`, `SEPARATEALPHABLENDENABLE`, `COLORWRITEENABLE`; flush | same 9 states, raw |
| 17 | ZWRITE exit `main.cpp:2119-2131` | if `zwriteWasDesync`: `TRUE`; else `savedZWriteRw` | if desync: `TRUE`; else `savedZWriteDrv` | if desync: `TRUE`; else `savedZWriteDev` |
| 18 | `pipeExitCullMode(savedCull)` `main.cpp:2132` → `pipelinecommon.cpp:1283` | `CULLMODE` park → saved | `D3DRS_CULLMODE` park → saved | `D3DRS_CULLMODE` saved |
| 19 | `pedOpaqueRestore(savedOpaque)` `main.cpp:2138` → `main.cpp:1122` | `VERTEXALPHA`, `ZTEST`, `ALPHATESTFUNCTION`, `ALPHATESTFUNCTIONREF` (ZWRITE deliberately excluded) | `ALPHABLENDENABLE`, `ZENABLE`, `ZFUNC`, `ALPHATESTENABLE`, `ALPHAFUNC`, `ALPHAREF` (no ZWRITE) | same 6, raw (no ZWRITE) |
| 20 | `[PedRS]` monitor `main.cpp:2141-2218` | read-only | read-only | read-only |
| 21 | `vs_probe("ped_exit")` `main.cpp:2222` | read-only | read-only | read-only |

Notes on the ledger:

* `zwriteWasDesync = (!savedZWriteRw || !savedZWriteDev)` — **L2 is not in the test**
  (`main.cpp:1818`). It was captured (`savedZWriteDrv`) but only used in the
  non-desync restore. All 67 `[PedRS]` samples show `zw 1/1/1`, so this did not
  produce an observable ZWRITE failure in this run.
* `pedOpaqueRestore` excludes ZWRITE by design (`main.cpp:1125-1127`); the sticky
  block owns it.
* `[PedTex]`'s `dev=` field is the code's **resolved** pointer from the cached
  raster (`main.cpp:1600-1614`), not a device `GetTexture` readback.

---

## 2. Dual pass — it is running, not skipped

Gate (`main.cpp:1698-1699`):

```c
}else if(isPedPipe && config->dualPassPed && config->zwriteThresholdPed > 0)
    dodual = 1;
```

With `dualPassPed=1` and `zwriteThresholdPed=128`, every `isPedPipe` call sets
`dodual=1`. The one-shot receipt proves the legacy branch executed:

```
[01:13:02.929] [INFO ] :0 () - [PedDual] legacy dual pass restored (zwriteThresholdPed=128)
[01:13:02.937] [INFO ] :0 () - [PedDual] first ped atomic alpha probe: material/prelit=0 texture=0
```

The 51 `[PED] dual pass skipped` lines are **not** the ped branch:

```
[01:13:02.945] [INFO ] :0 () - [PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=04C26410
```

All 51 have `pipe=04C26410` (the default-pipeline pointer) and the skip branch is
guarded by `dodual && !isPedPipe` (`main.cpp:2021`). A skipped **skin** ped would
have logged the skin-pipe pointer; it never does. **The ped dual pass is live.**

---

## 3. `pedStage0Resync` / `[PedTex]` / `[CloudShield]`

The one and only `[PedTex]` line in the whole file, verbatim:

```
[01:13:02.922] [INFO ] :0 () - [PedTex] stage0 resync cached=00000000 dev=00000000
```

* `cached=00000000` is **NULL**, not a ped texture and not the cloud atlas.
* `dev=00000000` is the resolved pointer for that NULL cache; it is **not** a
  device readback (see `main.cpp:1600-1614`). The function was a no-op on its only
  sample.
* The line is `dbglog_once("ped_tex_resync")`, so later ped atomics are completely
  unsampled. This proves the function ran once; it proves nothing about steady-state
  stage-0 corruption.

**The `[CloudShield] restored stage-0 raster=… dev=…` lines do not exist in this
log.** Zero occurrences. The only two `[CloudShield]` lines are:

```
[01:13:15.246] [INFO ] :0 () - [CloudShield] flush batch=sprites forced SRCALPHA/ONE
[01:13:40.044] [INFO ] :0 () - [CloudShield] flush batch=clouds forced ONE/ONE
```

The "2 occurrences" in the prompt are the **two source sites**
(`main.cpp:4793` and `main.cpp:5018`), both gated by
`dbglog_once("cloud_texraster")` **and** `savedTexRaster != whiteRas`. Neither fired
in this run. There is therefore **no pointer to cross-check** and no evidence in
this log that a cloud atlas was left on stage 0.

---

## 4. Log grouping

### 4.1 `[PedRS]` — 67 lines: 56 `ok`, 11 `DIVERGED`

All 56 `ok` lines share `src 5/5/5`, `dst 6/6/6`, `zw 1/1/1`, `afunc 5/5/5`.
Distinct signatures:

| Count | cull | blend | aref |
|---:|---|---|---|
| 1 | 1/1/1 | 0/0/0 | 2/2/2 (first span) |
| 22 | 2/2/2 | 0/0/0 | 0/0/0 |
| 4 | 2/2/2 | 0/0/0 | 2/2/2 |
| 22 | 2/2/2 | 1/1/1 | 100/100/100 |
| 7 | 2/2/2 | 1/1/1 | 2/2/2 |

The 11 `DIVERGED` lines are **all** blend/aref-only; cull, src, dst, zw and afunc
agree in every one. Tabulated (`rw/drv/dev`):

| # | Time | blend | aref | everything else |
|---:|---|---|---|---|
| 1 | 01:14:13.173 | 1/1/0 | 100/100/100 | cull 2/2/2, src 5/5/5, dst 6/6/6, zw 1/1/1, afunc 5/5/5 |
| 2 | 01:15:35.537 | 1/0/0 | 100/100/0 | same |
| 3 | 01:15:37.532 | 1/1/0 | 100/100/100 | same |
| 4 | 01:15:39.535 | 1/1/0 | 100/100/0 | same |
| 5 | 01:15:41.538 | 1/1/0 | 100/100/0 | same |
| 6 | 01:15:47.545 | 1/1/1 | 100/100/0 | same |
| 7 | 01:15:51.569 | 1/1/1 | 100/100/0 | same |
| 8 | 01:15:59.647 | 1/1/1 | 100/100/0 | same |
| 9 | 01:16:07.662 | 1/1/1 | 100/100/1 | same |
| 10 | 01:16:15.653 | 1/1/1 | 100/100/0 | same |
| 11 | 01:16:39.669 | 1/1/1 | 100/100/0 | same |

Interpretation: these are **not** failed repairs. `pipeExitAlphaMode` restores
L1/L2 from the pre-entry capture, and `pedOpaqueRestore` restores L3 from its own
capture (`main.cpp:1134-1148`). When the pre-entry capture was `rw/drv = 100/blend 1`
and `dev = 0/blend 0`, the monitor's "DIVERGED" is the contract doing exactly what
it documents. The [PedRS] boolean has no notion of "faithful per-layer restore".

### 4.2 `[PedOpaque]` — 97 lines: 1 pre-entry + 96 "contract repaired"

The single true pre-entry device snapshot:

```
[01:13:02.913] [INFO ] :0 () - [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 | rw ref=2 fn=5 vtx=0 zw=0 ztest=1 | drv ref=2 fn=5 blend=0 zw=0
```

`ZENABLE=0` + `ZFUNC=8` (D3DCMP_ALWAYS) + `ZWRITE=0` + `ALPHATEST=0` on the raw
device: the first ped of the run entered with depth completely off.

The 96 "contract repaired" lines split into 14 distinct entry triplets:

| Count | Entry triplet (dev … / rw … / drv …) |
|---:|---|
| 46 | `dev(ref=0 fn=5 blend=0 zw=1 zen=1 zfunc=4 atest=1) rw(ref=0 fn=5 vtx=0 zw=1 ztest=1) drv(ref=0 fn=5 blend=0 zw=1)` |
| 19 | `dev(ref=100 fn=5 blend=1 zw=1 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 9 | `dev(ref=2 fn=5 blend=1 zw=1 zen=1 zfunc=4 atest=1) rw(ref=2 fn=5 vtx=1 zw=1 ztest=1) drv(ref=2 fn=5 blend=1 zw=1)` |
| 5 | `dev(ref=2 fn=5 blend=0 zw=1 zen=1 zfunc=4 atest=1) rw(ref=2 fn=5 vtx=0 zw=1 ztest=1) drv(ref=2 fn=5 blend=0 zw=1)` |
| 5 | `dev(ref=100 fn=5 blend=1 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 3 | `dev(ref=0 fn=5 blend=1 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 2 | `dev(ref=0 fn=5 blend=1 zw=1 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 1 | `dev(ref=2 fn=5 blend=1 zw=1 zen=1 zfunc=4 atest=1) rw(ref=2 fn=5 vtx=0 zw=1 ztest=1) drv(ref=2 fn=5 blend=1 zw=1)` |
| 1 | `dev(ref=2 fn=5 blend=0 zw=1 zen=1 zfunc=4 atest=0) rw(ref=2 fn=5 vtx=0 zw=1 ztest=1) drv(ref=2 fn=5 blend=0 zw=1)` |
| 1 | `dev(ref=2 fn=5 blend=0 zw=0 zen=0 zfunc=8 atest=0) rw(ref=2 fn=5 vtx=0 zw=0 ztest=1) drv(ref=2 fn=5 blend=0 zw=0)` |
| 1 | `dev(ref=100 fn=5 blend=0 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 1 | `dev(ref=1 fn=5 blend=1 zw=1 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 1 | `dev(ref=0 fn=5 blend=0 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)` |
| 1 | `dev(ref=0 fn=5 blend=0 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=0 zw=1)` |

Dominant cold state: `ref=0`, blend 0, zwrite 1 (`46`). Dominant hot state:
`ref=100`, blend 1, vtx 1 (`19 + 5 + 3 + 2 + 1 + 1 + 1 = 32` lines carry rw
`ref=100`). **Every one of the 96 sampled entries was divergent and was repaired.**

### 4.3 `[PED]` — 77 lines, two signatures only

| Count | Verbatim signature |
|---:|---|
| 51 | `[PED] dual pass skipped: zwrite=1 hasAlpha=0 pipe=04C26410` (default pipe, `!isPedPipe`) |
| 26 | `[PED] ZWRITE off/desynced on entry (rw=… dev=…) — forcing TRUE for cb` |
| 1 of 26 | `rw=0 dev=0` (first ped, 01:13:02.920) |
| 25 of 26 | `rw=1 dev=0` |

There are **zero** `[PED] ZWRITE sticky repair` lines. The entry and exit share the
`"PedZWrite"` throttle bucket (`main.cpp:1820` and `2124`), so the exit receipt is
starved by the entry receipt. This is a logging gap, not proof the exit repair did
not run — the 26 desync entries all show `[PedRS] zw 1/1/1` afterward.

---

## 5. Verdict

### (a) Is the ped pipeline internally consistent in this build?

**At the draw, yes for the states it repairs.** The contract is live
(`pedOpaqueContract=1`), every sampled entry was divergent and repaired, the dual
pass ran, cull stayed `NONE/BACK` (never `FRONT`), and all 67 `[PedRS]` samples show
`src 5`, `dst 6`, `zw 1`, `afunc 5`. The 11 DIVERGED lines are the contract's own
faithful per-layer restore, not a failed repair.

**At the contract level, no — two internal order/consistency defects are present:**

1. **The dual pass is the last alpha-test writer, not `pedOpaqueEnforce`.** The
   contract comments say "Enforce is the LAST writer before the skin pipeline"
   (`main.cpp:948`, `1851`), but with `dualPassPed=1` the sequence is
   `enforce(GE@1)` → `leg1(GE@128)` → `leg2(LESS@128)` → restore(`GE@1`). The last
   mod write before `RxPipelineExecute` is therefore **128**, not the contract's
   `1` (the skin pipeline's own per-material writes are not logged, so the draw
   value itself is only source-inferred). The contract's `ALPHAREF=1` repair is
   inert for the draw. This is deterministic and applies to all skin peds equally,
   so it cannot explain zone-gated visibility, but it falsifies the "last writer"
   invariant.
2. **The shared alpha exit captures `BLENDOP=0xFFFFFFFF` into L2.** The log proves
   it at the first ped exit:

   ```
   [01:13:02.940] … [PipeAlpha] faithful restore (parity 6): rw vtx=0 — captured vs pre-parity: enable L2=0 L3=0 (derived 0), test L2=0 L3=0 (derived 1), op L2=4294967295 L3=1 (derived 1)
   ```

   `pipelinecommon.cpp:1585` uses `dOp ? dOp : ADD`, which rejects only `0`, not
   `0xFFFFFFFF`. The exit writes `0xFFFFFFFF` into L2 `BLENDOP` and then pins L3 to
   `ADD`. `[PedRS]` does not read `BLENDOP`, so this is invisible to the monitor.
   It is a real layer-cache corruption, but the next `pipeEnterAlphaMode` overwrites
   `BLENDOP=ADD`, so it is not the visibility cause.

**The exit is deliberately not a three-layer canonical write.** It restores each
layer to its own pre-entry value; that is why 11 `[PedRS]` samples are "DIVERGED".
If the goal is "rinse the whole ped pipe", this is the one place where the ped pipe
re-emits the external corruption instead of healing it.

### (b) Zone-gated visibility: ped-state bug or external occluder/ordering?

**It requires an external occluder/ordering cause.** The decisive argument:

* There is no zone/area/currArea term anywhere in the ped branch. The state machine
  is per-atomic and identical for every skin ped. It either repairs (it does, 96/96)
  or it does not; it cannot repair "inside zone A but not zone B".
* All 67 exit monitors show the states that could hide a ped are canonical or
  faithfully restored: **cull never `FRONT`** (all `1/1/1` or `2/2/2`), **ZWRITE
  never off** (`1/1/1` in all 67), **`ALPHAFUNC` always `5`**, **`ZTEST` 1**. The
  monitor is exit-only, so draw-time state is unproven (§7), but a per-atomic
  constant repair cannot be gated by a world zone in any case.
* The only zone/order-correlated signal in the log is the **entry pollution
  window**: `rw ref=100 / vtx=1` appears after the mirror/static-shadow pass and is
  reset to `0` whenever a building/vehicle pipe entry runs between that pass and
  the ped. That changes the *receipt*, not the repaired draw. Exact example:
  * `01:13:39.033` shadow pre-entry `ref=100`; `01:13:39.058-060` a pipe entry
    resets to `ref=0`; the ped at `.061` enters `ref=0`.
  * `01:14:13.193` shadow pre-entry `ref=100`; no pipe entry before the ped;
    the ped at `.265` enters `ref=100`.
  The ped is downstream of that order. This is the "deeper code affecting the
  order" the user suspects — but it is **outside** the ped pipe.
* Therefore a zone-gated disappearance means one of:
  1. the ped atomics are not submitted in that zone (game streaming / interior
     culling), or
  2. they are drawn and a later **world-space occluder** covers them.

**External cause class: post-ped world-space occluder / draw-order leak.** The
concrete candidate class is the deferred shadow/Im2D quad — stored/static shadows
(`CShadows::RenderStoredShadows` `0x708300`, `RenderStaticShadows` `0x707F40`) and
the corona/sprite flush — which rasterize world-space quads outside the ped state
contract. This matches the prior `2026-10-25-ped-occluder-audit.md` verdict
("drawn then covered", 0.72). The secondary class is **mis-gated default-pipe
ped atomics**: `isPedPipe = (pipe == skinPipe)` is a single pointer compare; any
ped component on the default pipeline takes the branch with no alpha/cull contract
and inherits `ref=100` (the 51 `pipe=04C26410` lines are that branch).

---

## 6. What is actually corrupting the ped (victim evidence)

1. **96/96 sampled ped entries were already divergent.** The ped branch captures,
   then repairs. The dominant hot state is `rw ref=100 / vtx=1`, device `blend=0`
   or `1`, `zw=0` or `1`. The ped is not producing this; it inherits it.
2. **The shared shadow contract sees the same pollution.** 106 shadow samples:
   103 with `drift > 0`. Example:

   ```
   [01:13:39.033] … [shadow_contract] static pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=100 …) drv(blend=1 … ref=100 …) dev(blend=0 … ref=100 …)
   ```

   The shadow contract heals L3 **to the polluted L1** (`ref=100`, `blend=1`), so
   the pollution is shared world state, not a ped-only value.
3. **No instrumented code writes `ALPHAREF=100`.** `envmap.cpp` writes `140`, the
   shadow contract writes `0`, the corona path writes `0`, `pipeApplyWorldBaseline`
   does not touch `ALPHAREF`. The `100` appears between a pipe exit (`ref=0`) and
   the next shadow pre-entry (`ref=100`), with the mirror/world pass in that gap.
   The producer is game/mirror/world-pass or another ASI, i.e. outside the ped pipe.
4. **The ped exit re-emits the split by design.** `pipeExitAlphaMode` restores
   L1/L2 from the pre-entry capture; `pedOpaqueRestore` restores L3 from its own
   capture. 11 `[PedRS]` DIVERGED lines are the proof. That is propagation, not
   production.
5. **The first ped of the run is the worst entry:** device `ZWRITE=0`,
   `ZENABLE=0`, `ZFUNC=ALWAYS`, `ALPHATEST=0`, cache `ZWRITE=0`. The contract
   repairs all of it. This is external Im2D/postfx/mirror pollution, exactly the
   class the state-leak audit documented.

---

## 7. Log lines that would prove a ped bug — and are ABSENT

These are the lines we would expect if the ped pipe itself were corrupting the draw.
None of them occur in the window (or at all):

| Expected proof line | Occurrences | Meaning of absence |
|---|---:|---|
| `[PedRS] … zw rw=1 drv=1 dev=0 …` | **0** | ZWRITE would be off at the device; all 67 show `1/1/1` |
| `[PedRS] … cull rw=3 …` / any cull triplet mismatch | **0** | FRONT cull or layer split at exit; all cull are `1/1/1` or `2/2/2` |
| `[PedRS] … afunc rw=8 …` (or anything ≠5) | **0** | ALPHAFUNC mismatch at exit; all 67 are `5` |
| `[PedRS]` `src`/`dst` mismatch | **0** | all `5/5/5`, `6/6/6` |
| `[PedRS]` DIVERGED where cull/zw/afunc differ | **0** | all 11 divergences are blend/aref only and match the per-layer capture |
| `[PED] dual pass skipped: … pipe=<skinPipe>` | **0** | all 51 skips are `pipe=04C26410` (default pipe) |
| `[PedAlphaTest] enabled=0` | **0** | it is `enabled=1` |
| `[PedCull] mode=3` / `saved=3` / `forced=3` | **0** | only `mode=-1 saved=1 forced=1`; cull never FRONT |
| `[PedTex] stage0 resync cached=<non-null> dev=<different>` | **0** | only `0/0`; no ped/cloud-atlas pointer evidence |
| `[CloudShield] restored stage-0 raster=… dev=…` | **0** | no cloud-atlas restore event at all in this log |
| `[PED] ZWRITE sticky repair: canonical TRUE written to rw/drv/dev at exit` | **0** | expected ~26 times; starved by the shared `PedZWrite` throttle tag (logging gap, not state failure) |
| Per-atomic pre-draw `[PedDraw]` / probe of `ALPHATESTENABLE`, `ALPHAREF`, `ZENABLE`, `ZFUNC`, `BLENDOP`, depth-stencil | **does not exist** | `[PedRS]` is exit-only and does not read these; the actual draw state is unproven |
| Per-frame/unique-atomic `[PedTex]` receipt | **does not exist** | one `dbglog_once` only; steady-state texture state unproven |

The missing `[PedDraw]` probe is the single biggest evidence gap: the log proves the
**entry** and **exit**, not the state at `RxPipelineExecute`. Everything in §5(a)
about the dual pass overriding the contract is source-derived, not log-proven.

---

## 8. What would settle it (instrumentation only — no source edited)

1. Add a one-shot-per-atomic (or throttled) **pre-draw probe after dual leg 1**:
   `ALPHATESTENABLE`, `ALPHAREF`, `ALPHAFUNC`, `ZENABLE`, `ZFUNC`, `ZWRITE`,
   `CULLMODE`, `BLENDOP`, and the bound depth-stencil surface. This is the only way
   to separate "ped state is fine" from "ped is covered".
2. Add a distinct `[PED] ZWRITE sticky repair` tag (`ped_zw_exit`) so the exit line
   is not starved by the entry tag.
3. Log the final `pedOpaqueRestore` triplet immediately before `[PedRS]` and add
   `BLENDOP` to `[PedRS]`; that would expose the `0xFFFFFFFF` L2 capture.
4. Sample `[PedTex]` on the first N unique atomics (or once per 2 s) instead of
   `dbglog_once`, and log the raw `GetTexture(0)` pointer alongside the resolved
   pointer, so a cache/device split is visible.
5. Add `currArea` / interior flag and a draw counter to the ped receipt, so a
   "zone" correlation is measurable rather than inferred.
6. A/B (next build, config only): `dualPassPed=0` (does the contract's `@1`
   matter?), `pedOpaqueContract=0` (contrast), `pedCullMode=1` (two-sided),
   `stencilShadows=0` + `pedShadows=0` (remove the shadow occluder class). If the
   zone symptom survives `pedOpaqueContract=1` + `pedCullMode=1` + shadows off,
   the ped pipe is conclusively not the cause.

---

## 9. Bottom line

* **Ped pipe:** internally consistent at the draw for cull/depth/blend/alpha-test
  entry; not internally consistent with its own "enforce is last writer" invariant
  when `dualPassPed=1`, and it propagates the external per-layer split at exit.
* **Victim status:** confirmed. Every sampled ped entry was polluted before the ped
  branch ran; the ped repairs it. The pollution is shared world state (`ref=100`,
  `vtx=1`, `blend=1`, `zw=0`), visible in the shadow contract and the first ped
  pre-entry.
* **Zone-only visibility:** not a ped-state bug. It requires an external
  occluder/ordering cause. The concrete class is the post-ped world-space shadow /
  Im2D quad (stored/static shadows, corona/sprite flush) drawn outside the ped
  state contract; the secondary class is mis-gated default-pipe ped atomics that
  never enter the ped contract.
