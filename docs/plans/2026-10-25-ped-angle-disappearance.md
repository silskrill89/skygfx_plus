# Ped angle-dependent disappearance — render-state audit & experiment plan

**Date:** 2026-10-25 (audit performed 2026-10-26 00:15 UTC)
**Scope:** READ-ONLY audit. No source changes in this task.
**Symptom:** outdoors, peds are only visible under specific view angles. Peds render through `myDefaultCallback` (hook 0x7491C0) into the game's own RW skin pipeline with **no custom pixel shader**.

---

## 0. Snapshot warning — the log was produced by an OLDER build than the current tree

This is the single most important caveat for reading the evidence.

| Item | Value |
|---|---|
| Current `src/core/main.cpp` | `md5 6c9c0166200e2f2fcbfdd4f4fe4c43fc`, mtime **2026-10-26 00:13:53**, 384391 bytes (edited again during the audit → `md5 4b1206e35c5cde905d6f78716d515bdf` by 00:16; all anchors below shifted +1 line) |
| Current `src/render/pipelinecommon.cpp` | mtime 2026-10-26 00:11:17 |
| Running `skygfx.asi` in the log | size **2360832**, mtime 2026-10-25 21:57:27, **built 21:56:44** |
| Log session | `22:35:22.682` → `22:42:48.161`, 63,421 lines, one `[BUILD]` (line 27) |

`[BUILD]` line (log line 27):

```
[22:35:22.707] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

Expected size 2360832 — **matches**.

But the current working tree has code the running DLL cannot contain:

- `pedCullMode` / `pedAlphaTestEnable` INI parse: `main.cpp:6005-6011` (current tree).
- `ADD_IF_MISSING("SkyGfx","pedCullMode","-1")` / `pedAlphaTestEnable":"1"`: `main.cpp:6522-6523`.
- `[PedCull] mode=... saved=... forced=...` receipt: `main.cpp:1762-1766`.
- `[PedAlphaTest] enabled=...` receipt: `main.cpp:1713-1719`.
- `pedOpaqueEnforce(pedAlphaTestOn ? 1 : 0)`: `main.cpp:1833`.

The log contains **zero** `[PedCull]` and **zero** `[PedAlphaTest]` lines, and `E:\games\gtasa_skygfx_plus\skygfx.ini` contains neither key, even though startup logged `skygfx: INI exists, adding any missing keys` (log line 21). Conclusion: **the 21:56 build predates the A/B levers entirely.** The view-angle A/B has never actually run. Any test of these levers requires a rebuild first.

All source line numbers below refer to the 00:13 snapshot above; they will shift if the tree keeps being edited (it was being modified during this audit — a 00:16 re-edit shifted the ped anchors by +1 line). **Anchor by symbol name when in doubt**; e.g. `pedOpaqueEnforce(...)` is at 1076 in the snapshot and 1077 now.

---

## 1. TL;DR — ranked hypotheses

| # | Hypothesis | Confidence | Why |
|---|---|---|---|
| **H1** | **Cull mode / winding** — ped span inherits a wrong/stale `CULLMODE` (FRONT from stencil-shadow volumes, NONE from the mirror pass), or BACK is forced on mirrored/negative-determinant geometry | **30 %** | Cull is the *only* render state in the ped path whose effect is intrinsically view-angle dependent. The A/B lever written for exactly this report was not in the log build. |
| **H2** | **Per-atomic gating** — skinned atomics whose `pipeline != skinPipe` take the default-pipe branch, which has **no** `pipeEnterCullMode`, no alpha func/ref force and no ped contract | **20 %** | `isPedPipe` is a single pointer compare (`main.cpp:1602`). The default branch (`1603-1655`) forces ATEST TRUE but leaves the leaked ALPHAREF, and never repairs cull. Nothing logs which atomics are mis-gated. |
| **H3** | **Depth / ZFUNC / depth-surface pollution** — the contract repairs ZWRITE/ZENABLE/ZFUNC only for the ped span, then faithfully restores a polluted per-layer ZFUNC; non-contract atomics can draw with `ZFUNC=ALWAYS` | **15 %** | 64 ZWRITE-off entries and 14 `ZFUNC=ALWAYS` entries prove the pollution is real and recurring. But the ped entry forces the state ON before the draw, so this explains *other* geometry over peds more than peds vanishing. |
| **H4** | **Alpha test** — test disabled and/or leaked ALPHAREF (envmap 100/140, corona 0, shadow 0) | **10 %** | Real and proven in the log, but **alpha test cannot be view-angle dependent** (constant per-pixel comparison). Ruled out as the angle mechanism; kept as a quality bug. |
| **H5** | **Overlay** — giant shadow/decal quads or the sprite/corona shield erase peds only at some cameras | **10 %** | Both force `ZWRITE=FALSE`; they **cannot** depth-occlude peds. They pollute blend/AREF/CWR, which the ped contract repairs. Ruled out as the occluder. |
| **H6** | **Geometry / winding flags** — `rpGEOMETRYNATIVE`, two-sided material flags, negative determinant or a world transform on ped atomics | **10 %** | No repo code touches any of these. This is the *mechanism* through which H1 hides faces, not an independent mod bug. |
| **H7** | **Residual: lighting, not visibility** — peds are black/silhouette from most angles and read as “invisible” | 5 % | Not a state bug; `pAmbient` override is the only per-ped lighting change (`main.cpp:1885-1960`). Cheap to test (`pedAmbientFloor=1.0`). |

Percentages are audit priors, not measurements. H4 and H5 are explicitly **ruled out as the view-angle mechanism**; they remain on the list because their pollution is what the ped contract spends most of its code repairing.

---

## 2. Cull audit (H1)

### 2.1 Call chain

```
myDefaultCallback (main.cpp:1584)
  isPedPipe = (pipe == skinPipe)                     main.cpp:1602
  if(isPedPipe) {                                    main.cpp:1710
      savedCull = pipeEnterCullMode();               main.cpp:1735
      ... pedCullMode lever ...                      main.cpp:1737-1766
      pipeExitCullMode(savedCull);                   main.cpp:2087
  }
```

`pipeEnterCullMode` (`pipelinecommon.cpp:1246-1281`):

- pre-seeds `saved = rwCULLMODECULLBACK` (1250);
- `RwRenderStateGet(rwRENDERSTATECULLMODE, &saved)` (1252);
- sanitizes only **out-of-domain** values to BACK (1257-1260);
- **forces the sanitized RAW saved value** (1269), not BACK unconditionally;
- returns the **raw** saved value so exit restores exactly what the caller had (1271-1279).

`pipeExitCullMode` (`pipelinecommon.cpp:1283-1301`) re-sanitizes NONE/FRONT as legitimate and calls `pipeForceCullMode(saved)`.

`pipeForceCullMode` (`pipelinecommon.cpp:1202-1243`):

- only NONE (1), BACK (2), FRONT (3) are accepted; anything else → BACK (1205-1207);
- park value is always different from the target, so L1/L2 transitions are never cache-hit no-ops (1213-1216);
- writes three layers: rw cache park→target (1219-1221), RW driver cache park→target (1224-1237), raw device (1242).

**Is the raw saved value preserved?** Yes, exactly — including NONE and FRONT. `pipeEnterCullMode` returns the raw value before sanitization; the ped branch stores it in `savedCull` and restores it at 2087. The only mutation is the *forced* value for the span, and only the optional `pedCullMode` lever can replace it (1743-1758).

**What is forced for a ped atomic today (pedCullMode = -1 default):** the sanitized raw saved mode. In the log that is BACK for the ped span in 156/157 `[PedRS]` samples, and NONE once (first frame).

### 2.2 Other ped-path cull writers in `main.cpp` (and the rest of the tree)

`grep` for `CULLMODE|pipeForceCullMode|savedCull` in `main.cpp`:

| Site | What it does | Can it hit the ped span? |
|---|---|---|
| `main.cpp:811,857` (`grassRenderCallback`) | forces `rwCULLMODECULLNONE` when `backfaceCull==0`; restores BACK | No — separate render callback, never peds |
| `main.cpp:1737-1766` | `pedCullMode` lever (NONE/BACK/FRONT) | Yes — only ped-path writer |
| `main.cpp:1735,2087` | enter/exit save-restore | Yes |
| `main.cpp:2766-2860` `coronaDrawRawShield` | **no cull write** (blend/zw/AREF/CWR/BLENDOP only) | No |
| `main.cpp:4040-4130` corona entry shield | **no cull write** | No |
| `pipelinecommon.cpp:1975+` `pipeApplyWorldBaseline` | parks FRONT→BACK on L1/L2 (2031-2032), raw BACK (2045) at frame boundaries | Not inside a ped span, but it is the only thing that resets a leaked NONE/FRONT between boundaries |
| `pipelinecommon.cpp:1202-1243` | the canonical 3-layer cull writer | via enter/exit |
| `envmap.cpp:426,475` | env-map pass sets BACK, restores `savedCull` through `pipeForceCullMode` | Before/after the reflection raster, not inside a ped draw |
| `waterPipe.cpp:1603-1605,1707` | water sets NONE, restores `s_savedCull` (only if `g_waterParallaxActive`) | Separate pass |
| `mirrorPass.cpp:365-379` | mirror exit re-syncs to the **rw-cache** value | After mirror; can re-assert NONE |
| `postfx_utils.cpp:179,267-277` | `SaveRawGeomStates`/`RestoreRawGeomStates` include `D3DRS_CULLMODE` twin | Separate Im2D/postfx passes |
| `postfx_ssao.cpp:364` | SSAO depth copy sets `rwCULLMODECULLNONE` | Separate pass; restored via raw-geom snapshot |
| `forwardplus.cpp:1874,1928` | raw `D3DCULL_NONE`, then restores `srCull` | Forward+ only; `fpCfg=0` in this log |
| `buildingPipe.cpp:1753,2208` | building switch restores its own saved cull / forces BACK | Building spans only |

**No other writer touches cull between a ped entry and its draw.** The game's own skin pipeline may change cull per material (two-sided material flags are owned by RW/the game — see §5), but the mod does not write cull there.

### 2.3 Can a NONE (two-sided) save reach the ped draw?

Yes. Evidence:

- 19 × `[Cull] enter saved=1 forced=1` in 144 enter samples (log).
- `[Cull] exit restored=1` 209 times — NONE is commonly restored by other pipe spans.
- The **first ped span of the run** is proven NONE at exit:

```
[22:35:40.941] [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 | rw ref=2 fn=5 vtx=0 zw=0 ztest=1 | drv ref=2 fn=5 blend=0 zw=0
[22:35:40.943] [Cull] enter saved=1 forced=1
[22:35:40.947] [PED] ZWRITE off/desynced on entry (rw=0 dev=0) — forcing TRUE for cb
[22:35:40.970] [PedRS] ok cull rw=1 drv=1 dev=1 | ... | zw rw=1 drv=1 dev=1 ...
```

- **Every** mirror exit sees NONE in the rw cache: `[CullResync] mirror exit rw=1` **212/212** times (first at 22:35:41.015). This means the mirror pass leaves `rwRENDERSTATECULLMODE == NONE`, and `MirrorPass_ResyncCull` faithfully re-asserts it on all three layers instead of restoring a pre-mirror truth (`mirrorPass.cpp:365-379`). Any ped draw after the mirror and before a baseline/enter capture inherits NONE.

NONE = two-sided, i.e. **more** faces visible, not fewer. So NONE alone does not explain disappearing peds. It does prove the cull cache is routinely polluted, which is the enabling condition for the dangerous value below.

### 2.4 Can FRONT reach the ped draw? (the dangerous mode)

`pipeEnterCullMode` deliberately preserves FRONT (`pipelinecommon.cpp:1257-1262`, comment at 1189-1193: FRONT is used by **ped stencil-shadow volumes**, exe 0x70A9C3). If the stencil-shadow-volume pass leaves FRONT in the **rw cache**, the ped entry captures FRONT and forces it for the whole ped span. With FRONT culling, a closed outward-wound ped is drawn **inside-out**: the outside shell is culled and only the far-side inner faces remain. At many camera angles that reads as “the ped is gone”, and it changes with view angle.

Log evidence is *inconclusive*, not negative:

- 0 × `[Cull] enter saved=3` in 144 samples.
- But `[Cull] enter` and `[PedRS]` are throttled to at most once per 2 s **globally** (`dbglog_throttle("cull_enter")`, `dbglog_throttle("ped_rs")`). A FRONT window that lasts a few milliseconds inside a ped burst can be missed entirely.
- No probe logs cull **immediately before the draw**. The `[PedRS]` cull is read at the **exit**, after `pipeExitCullMode(savedCull)` restored `savedCull` — it proves what the span captured, not what the device held during `RxPipelineExecute`.
- `[CullResync]` proves a pass can leave the cache on a non-BACK value and the “trust the cache” restore will keep it. The shadow-volume pass is the same class of pass (it sets FRONT).

**Negative-determinant interaction:** forcing BACK on a mirrored atomic is the documented failure mode (`pipelinecommon.cpp:1189-1193`: “draws the mirrored negative-determinant CJ inside-out”). There is **no determinant guard anywhere in `src/`** (see §5). If a ped atomic/clump is mirrored (negative scale) or rendered in the mirror reflection, BACK is wrong and only NONE is safe.

### 2.5 Log cull summary

```
[Cull] enter saved=1 forced=1   : 19
[Cull] enter saved=2 forced=2   : 125
[Cull] exit  restored=1         : 209
[Cull] baseline BACK            : 1   (22:35:41.025)
[Cull] enter saved=3            : 0
[CullResync] mirror exit rw=1   : 212 / 212
[PedRS] cull 2/2/2              : 156 / 157
[PedRS] cull 1/1/1              : 1   (22:35:40.970, first ped span)
```

### 2.6 Verdict and experiment

**Verdict:** cull is the highest-value hypothesis because it is the only view-angle-dependent state and because the intended A/B was never in the running build. The log weakens a *persistent* wrong cull (ped spans sampled at BACK), but does not exclude a transient FRONT leak from the stencil-volume pass or a mirrored-atomic case.

**Minimal confirming/killing change (requires rebuild):**

```ini
[SkyGfx]
pedCullMode = 1     ; force CULLNONE (two-sided) for the ped span
; then 2 = CULLBACK, 3 = CULLFRONT
stencilShadows = 0  ; remove the only known FRONT source
```

- **Kill:** `pedCullMode=1` and the peds still disappear at the bad angles → cull is **ruled out** (two-sided cannot hide faces by winding).
- **Confirm:** `pedCullMode=3` reproduces the exact symptom (outside-invisible, inside visible) → cull/winding confirmed; then fix by not preserving a stale FRONT (prefer the last *world* cull, or force NONE for skinned atomics with det<0).
- **Expected next-run lines:**
  - `[PedCull] mode=1 saved=2 forced=1`
  - a new pre-draw probe (see §7) reading `cull rw/drv/dev` at 1/1/1 while a ped is on screen.
  - If FRONT leaks: `[Cull] enter saved=3 forced=3` or `[PedRS] ... cull rw=3 ...`.

---

## 3. Alpha test audit (H4)

### 3.1 What `pedOpaqueEnforce` enforces

`pedOpaqueEnforce(int alphaTestEnable)` — `main.cpp:1076-1119`:

```c
RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, FALSE);   // 1083
RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, TRUE);         // 1084
RwRenderStateSet(rwRENDERSTATEZTESTENABLE, TRUE);          // 1085
if(alphaTestEnable){
    ... ALPHATESTFUNCTION = GREATEREQUAL; ALPHAREF = 1 ...   // 1086-1090
}
RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);       // 1093
RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);            // 1094
RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);                 // 1095
RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);       // 1096
if(alphaTestEnable){
    RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);     // 1100
    ... ALPHAFUNC=GREATEREQUAL, ALPHAREF=1 ...
}else
    RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);    // 1104
// same block on the raw device, 1110-1118; else ATEST FALSE at 1117
```

Call site: `pedOpaqueEnforce(pedAlphaTestOn ? 1 : 0);` — `main.cpp:1833`.
`pedAlphaTestOn = (config->pedAlphaTestEnable != 0);` — `main.cpp:1712`.

**Current tree defaults:** `readint(...pedAlphaTestEnable..., 1)` at `main.cpp:6009`; clamp 0/1 at 6010-6011. The `[PedAlphaTest] enabled=` receipt at 1713-1719 will prove it next run.

**Log build:** neither the key nor the receipt exists in the log (0 `[PedAlphaTest]`, 0 `[PedCull]`; INI has no key). The old build's effective value is therefore **unproven**. If it had the field but no parse, `Config configs[10]` is zero-initialized (`main.cpp:80`) and `pedAlphaTestEnable` would be 0 → `pedOpaqueEnforce(0)` would force ATEST **OFF**. That is the most likely old-build behaviour and contradicts the comment “1 = canonical cutout test (current behaviour)”. Treat this as a probable old-build bug, fixed in the current tree by the new default.

### 3.2 Other writers that can leave a different ref at the ped draw

Those below all run **before** the ped contract; the contract is the last writer (when `pedOpaqueContract=1`), so they are *sources of divergence*, not the final value:

| Source | Evidence |
|---|---|
| Envmap outdoor world pass parks `ALPHAREF=140` mid-frame; its save/restore is 3-layer now (`envmap.cpp:426-475`), but a raw drift during the span could survive | `[PedOpaque] ... dev ref=100 ...` repeatedly (e.g. 22:42:08.220, 22:42:42.309); one-shot pre-entry earlier in the run: `ALPHAREF=2` |
| `pipeForceAlphaBlock` repairs a cached ref of 0 → 1 (`buildingPipe.cpp:560-576`), but pushes the **cache's** func/ref, not the caller's | `[PipeAlpha] entry resync: ... test=0 rw ref=2 fn=5` at startup |
| `pipeEnterAlphaMode` effRef 0→1 bump (`pipelinecommon.cpp:1714,1843`) | `[Pipeline] pipeEnterAlphaMode: saved rw(...ref=0...)` |
| Corona shield per-draw `ALPHAFUNC=GREATEREQUAL, ALPHAREF=0` (`main.cpp:2820-2826`), restored to pre-window at exit (4216-4240) | `[FxAlpha] PostFx.exit ... ref=0` 10×; `[PedOpaque] ... dev ref=0 ...` (e.g. 22:42:22.226) |
| Shadow contract forces `GREATEREQUAL@0` (`main.cpp:1361-1370`), heals L3 to the rw cache | `[ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)` (22:35:41.044) |
| Ped dual pass legs write only FUNC/REF, never `ALPHATESTENABLE` (`setAlphaTestTwoLayer`, `main.cpp:230-240`) | Code; if enforce set ATEST FALSE, both legs run with the test disabled |

### 3.3 Is alpha test view-angle dependent?

**No.** `D3DRS_ALPHATESTENABLE` + `ALPHAFUNC` + `ALPHAREF` compare each pixel's alpha against a constant. There is no camera/view term. It can make peds **partially eaten** or **opaque cutouts**, but it cannot make the same ped vanish as the camera orbits.

**Verdict:** ruled out as the angle mechanism. It remains a real quality bug class (and the current tree's default-1 fix + receipt should be kept). The apparent angle dependence could be a secondary effect only through single-sided alpha cards (§5): an opaque card blocks the face from one side and is culled from the other — but that is still cull that hides it, not alpha test.

**Minimal experiment:** new build, `[PedAlphaTest] enabled=1`; compare `pedAlphaTestEnable=1` vs `0`. If visibility at the bad angles is identical, alpha is conclusively out. Add a post-enforce ATEST probe (§7) because `[PedRS]` does not read `D3DRS_ALPHATESTENABLE` at all (`main.cpp:2116-2142`).

---

## 4. Depth audit (H3)

### 4.1 Ped ZWRITE / ZENABLE / ZFUNC

Entry (`main.cpp:1768-1789`):

- `savedZWriteRw/Drv/Dev` captured from L1/L2/L3 (1776-1781).
- `zwriteWasDesync = (!savedZWriteRw || !savedZWriteDev)` (1782). **`savedZWriteDrv` is not part of the test.**
- Forces ZWRITE TRUE on L1+L2+L3 (1786-1789).
- Logs `[PED] ZWRITE off/desynced on entry` under tag `"PedZWrite"` (1783-1785).

Contract (`pedOpaqueEnforce`, 1094-1096 / 1104-1118): ZWRITE TRUE, ZENABLE TRUE, ZFUNC LESSEQUAL.

Dual pass (`main.cpp:2010-2045`): leg 1 ZWRITE TRUE, leg 2 ZWRITE FALSE, then ZWRITE TRUE again (2022-2024). It never leaves ZWRITE off at callback exit.

Exit (`main.cpp:2072-2094`):

- If `zwriteWasDesync`: sticky repair writes canonical TRUE to all three layers (2073-2081). It logs `[PED] ZWRITE sticky repair` under the **same** `"PedZWrite"` throttle tag (2082-2084) that the entry just consumed.
- Else faithful per-layer restore of the captured values (2084-2089).
- `pedOpaqueRestore` deliberately excludes ZWRITE (`main.cpp:1122-1133`): the sticky block owns it.

### 4.2 Does any per-atomic path leave ZWRITE off?

No for the ped branch itself: the entry forces TRUE before `RxPipelineExecute`, the dual pass restores TRUE, and the exit sticky-restores TRUE. The only ZWRITE-off interval is dual-pass leg 2, which is between two draws of the same atomic and is closed before the callback returns.

The log confirms the repair is active, not that it was ever absent at the draw:

- 64 × `[PED] ZWRITE off/desynced on entry` — first `rw=0 dev=0` (22:35:40.947), then 63 × `rw=1 dev=0` (e.g. 22:38:40.607 … 22:42:44.581).
- 27 × `[PedOpaque]` entries with `zw=0` on the device (e.g. 22:42:08.220, 22:42:06.331).
- `[PedRS]` ZWRITE is **1/1/1 in all 157 samples** → the exit state (and therefore the repaired state) has depth writes on.
- `[PED] ZWRITE sticky repair` count = **0**. This is a *logging* bug: entry and exit share the `"PedZWrite"` throttle bucket, so the exit line is starved. It is not evidence the repair didn't run. **Recommend a distinct tag** (`"PedZWriteExit"`) so the next log can prove it.

### 4.3 The ZFUNC leak (real, recurring)

`[PedOpaque]` pre-entry shows `dev zfunc=8` (**D3DCMP_ALWAYS**) 14 times, e.g.:

```
[22:35:40.948] dev(ref=2 fn=5 blend=0 zw=0 zen=0 zfunc=8 atest=0) ...
[22:36:00.327] dev(ref=1 fn=7 blend=0 zw=1 zen=1 zfunc=8 atest=0) ...
[22:36:02.327] dev(ref=1 fn=7 blend=0 zw=1 zen=1 zfunc=8 atest=0) ...
[22:36:04.325] dev(ref=1 fn=7 blend=0 zw=1 zen=1 zfunc=8 atest=0) ...
```

The contract repairs it to LESSEQUAL for the ped draw. But `pedOpaqueRestore` (1136-1148) restores `s->devZFunc` **per layer**, i.e. it re-emits ALWAYS to the device after the ped span. The next ped repairs; an interleaved **non-ped** atomic (default-pipeline prop/pickup or the next shadow pass) can draw with `ZFUNC=ALWAYS` and overwrite peds. That produces “props/markers draw over peds”, not “peds disappear”. `[PedRS]` does not monitor ZENABLE/ZFUNC (2110-2142), so this leak is invisible except when the ped entry samples it.

### 4.4 Verdict and experiment

**Verdict:** a persistent ZWRITE-off ped draw is ruled out by the entry force + 157/157 `[PedRS] zw=1`. ZFUNC/DS pollution is real and can make *other* geometry occlude/paint over peds, and a depth-surface mismatch would be screen-position (hence camera) dependent — but there is no evidence of a DS mismatch in the main view (only 2 `[FxAlpha] Coronas.Reflections camRas=2048x1024 != screen 1768x992` lines, both at startup; the main camera is `048BEFC0 1768x992`).

**Minimal experiment:**

```ini
pedOpaqueContract = 0   ; contrast: removes the depth/blend/ATEST repair for the ped span
stateBaselineEnable = 1 ; keep the frame-boundary cleanup
stencilShadows = 0      ; also tests the shadow-volume path
```

- If the symptom **changes** when the contract is off, the contract is load-bearing → depth/blend repair is involved in the *appearance*, but not necessarily the angle; inspect the first frame after the change.
- If **unchanged**, depth/blend is ruled out for the angle.
- **Expected proof line:** new `[PedDraw]` probe after `pedOpaqueEnforce` and after dual-leg 1, reading `zw/zen/zfunc/cwr` + the bound DS pointer from all three layers. If all canonical (`1/1/4/0xF`, DS == main camera DS) while the ped is invisible → depth ruled out.

---

## 5. Geometry / winding audit (H6)

Searches over `src/`:

- `rpGEOMETRYNATIVE` — **0 hits**.
- `rpMATERIALFLAGS` / `MATERIALFLAG` / `two.sided` / `twosided` / `TWOSIDED` — **0 hits**.
- `determinant` — only comments at `pipelinecommon.cpp:1191` (“mirrored negative-determinant CJ inside-out”) and `1250`.
- `negative determinant` — same comment only.
- `myDefaultCallback` applies **no transform** to ped atomics: it only overrides `pAmbient` (`main.cpp:1885-1960`) and calls `RxPipelineExecute(pipe, atomic, 1)` (2045). No world matrix, no frame matrix, no `RpAtomicSetMatrix`.
- `0x7491C0` is hooked with `InjectHook` at `main.cpp:7591` (current tree; the hook itself is unchanged).

**Conclusion:** the mod introduces no geometry bug, no two-sided material override and no ped transform. Material two-sidedness is owned by the game/RW skin pipeline. The only mod-controlled winding decision is cull (§2). If a ped atomic has a negative determinant (mirrored variant or mirror reflection), `CULLBACK`/`CULLFRONT` is wrong for it and `CULLNONE` is the only safe mode — this is the mechanism by which H1 hides faces, not a separate bug.

**Minimal probe (no INI lever):** at ped entry, once per unique atomic:

```c
// pseudo
RwMatrix *m = RwFrameGetMatrix(RwFrameGetParent((RwFrame*)atomic->object.object.parent));
float det = RwMatrixGetDeterminant?  // or compute 3x3 det from m->right/up/at
dbglog("[PedGeom] atomic=%p det=%.3f native=%d twoSidedMat=%d",
       atomic, det, !!(atomic->geometry->flags & rpGEOMETRYNATIVE), ...);
```

Proof line: `[PedGeom] det<0` for a ped that vanishes. If all dets are positive, H6 is dead.

---

## 6. Per-atomic gating audit (H2)

### 6.1 The gate

```c
pipe = atomic->pipeline;                                  // main.cpp:1600
bool isPedPipe = (pipe == skinPipe);                      // main.cpp:1602
if(pipe == NULL){ ... default branch ... }                // main.cpp:1603-1655
else if(isPedPipe && dualPassPed && zwriteThresholdPed>0) // main.cpp:1657
    dodual = 1;
```

- `isPedPipe` is captured **before** the NULL fallback overwrites `pipe`.
- `skinPipe` is the game global at `0xC978C4` (`src/rw/gta.cpp:66`).
- If `atomic->pipeline` is NULL, `isPedPipe` is false even for a skinned ped; the NULL branch executes the game default pipeline.

### 6.2 What the default branch does differently

`main.cpp:1603-1655`:

- forces `VERTEXALPHAENABLE=FALSE`, `SRCBLEND=SRCALPHA`, `DESTBLEND=INVSRCALPHA`, `ZWRITE=TRUE`, `ZTEST=TRUE` on L1+L2+L3;
- forces `D3DRS_ALPHABLENDENABLE=FALSE`, `BLENDOP=ADD`, `ZWRITE/ZENABLE=TRUE`, **`ALPHATESTENABLE=TRUE`** on L2+L3 (1640-1651);
- **never writes `ALPHAFUNC`/`ALPHAREF`** — a leaked ref=100/140/0 is now applied by the forced ATEST TRUE;
- **never calls `pipeEnterCullMode`** — the atomic inherits whatever cull the previous pass left (NONE from the mirror, FRONT from a shadow volume);
- **never runs `pedOpaqueEnforce`/`pedOpaqueRestore`** (the ped contract is inside `if(isPedPipe)` at 1710-1835);
- `dualPassDefault`/`atomicHasAlpha` gate at 1976-1990; the skip logs `[PED] dual pass skipped ... pipe=...` (1982-1985) — the tag says “PED” but the branch is the **default** pipe.

### 6.3 Why this could be the real bug

If any **skinned** atomic in a ped clump does not point at `skinPipe` (a cloned pipeline, a per-model pipeline, a NULL pipeline, a ped prop/weapon/hat atomic), it gets the default contract: forced ATEST TRUE with the **stale ALPHAREF** and **no cull repair**. That set of atomics can be partially/fully hidden depending on the leaked cull and ref, and the visible set changes as the camera moves because cull does.

There is no log line today that identifies such an atomic. `[DefPipe]` is one-shot (`main.cpp:1652-1655`), and `[PED] dual pass skipped` only fires for the default dual gate. `[PedOpaque]`/`[PedRS]` only run for `isPedPipe`.

### 6.4 `atomicHasAlpha`, `dualPassPed`, `TexAlpha_Classify`

- `atomicHasAlpha` (`main.cpp:888-930`) is used **only** in the non-ped dual gate (`main.cpp:1976-1990`) and as one-shot diagnostic output for peds (`main.cpp:2046-2055`). It does **not** change which peds are drawn.
- `dualPassPed` is unconditional for the ped branch when `isPedPipe && dualPassPed && zwriteThresholdPed>0` (`main.cpp:1657`, 1962-2045). Draw order does not change the set of peds; it changes each ped's own two-leg order (fixed).
- `TexAlpha_Classify` is diagnostic only for peds (`main.cpp:912-925`).

**Verdict:** plausible hidden set, especially for ped sub-atomics that are not on `skinPipe`. Draw order alone cannot change the set, but the per-atomic contract can.

**Minimal confirming/killing change (no INI lever):**

- One-shot per unique pipeline pointer:
  `[PedGate] pipe=%p skin=%p skinGeom=%d` for every atomic with `RpSkinGeometryGetSkin(atomic->geometry) != NULL`.
- If any line has `pipe != skin` and `skinGeom!=0`, extend the ped gate to
  `bool isPedPipe = (pipe == skinPipe) || RpSkinGeometryGetSkin(atomic->geometry) != NULL;`
  and re-run.
- **Proof line:** `[PedGate] skinned-not-skinPipe pipe=... skin=...` appearing on the frame where a ped vanishes.
- **Kill:** all visible ped atomics report `pipe == skin` → H2 dead.

---

## 7. Overlay audit (H5)

### 7.1 Shadow contract

`main.cpp:1199-1533`:

- Hooks `CShadows__RenderStaticShadows` (0x707F40), `RenderStoredShadows` (0x708300), `RenderExtraPlayerShadows` (0x70A960) via `shadowContractRun` (1474-1517).
- Capture per-layer (1251-1327), force (`shadowContractForce`, 1344-1383):
  - `VERTEXALPHAENABLE=TRUE`, SRCALPHA/INVSRCALPHA, `ZTEST=TRUE`, **`ZWRITE=FALSE`**, `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=0`, FOGENABLE=FALSE on L1+L2+L3.
- Restore (`shadowContractRestore`, 1385-1462): per-layer; if `drift` (L3 ≠ cache), L3 is pinned to the **rw-cache** values, not the device capture.
- One-shot `[ShadowHeal]` at 22:35:41.044:

```
[ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)
```

- 212 `[shadow_contract]` lines: **128 `extra`**, **84 `static`**, drift 1–4, e.g.:

```
[22:42:08.223] [shadow_contract] extra pre-entry layer drift: drift=2 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=1 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=0) dev(blend=1 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=0 fog=0)
[22:42:12.220] [shadow_contract] static pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
```

- The exit-healed line never appeared (`shadow_contract_exit` throttle bucket starved), so the heal count is not visible.

**Can it erase peds?** Only if it wrote depth **after** peds. It forces `ZWRITE=FALSE`; the shadow quads blend over whatever is already there. They can darken peds (a blob shadow projected onto them), not depth-occlude them. The restore can leave the device on blend=0 / ref=0/100, but the next ped entry's `pipeForceAlphaBlock` + contract repairs it.

### 7.2 Corona/sprite shield

`coronaDrawRawShield` (`main.cpp:2766-2860`) runs immediately before each sprite rasterisation inside the corona window:

- parks/sets blend additively (SRCALPHA/ONE, or ONE/ONE for clouds), `ZWRITE=FALSE`;
- `ALPHAFUNC=GREATEREQUAL, ALPHAREF=0`, `SEPARATEALPHABLENDENABLE=0`, `COLORWRITEENABLE=0xF`, `BLENDOP=ADD` on L2+L3;
- pre-window values captured at `CCoronas__Render_blendprobe_hook` (4002-4100) and restored at exit (4180-4240) / `CoronaWindow_Close`.

73 × `[CoronaShield] forced additive, was src=5 dst=6 blend=0 op=1 zw=1 atest=1 ztest=1->1`.

**Can it erase peds?** It forces `ZWRITE=FALSE`; like the shadow contract it cannot depth-occlude. A *stuck* window (flag never closed) would leave additive blending and `AREF=0` on the next draws, but the ped entry forces blend off and `pipeForceAlphaBlock` sets `COLORWRITEENABLE=0xF` + ATEST TRUE before the contract; the contract then sets ATEST per the lever. A ped draw inside the window is not possible: the shield runs at the sprite flush, between draws, and the ped entry runs at the start of its own callback.

**Verdict:** ruled out as the depth occluder. Keep only as a state-pollution source and for the stuck-window edge case.

**Minimal experiment:**

```ini
pedShadows    = 0   ; disable ped shadow pass
stencilShadows= 0   ; disable stencil shadow volumes (also the FRONT-cull source)
coronaZtest   = 1   ; already canonical
```

If the symptom disappears with shadows off, inspect the shadow-volume cull/state path (H1), not the shadow contract, because the contract's `ZWRITE=FALSE` cannot hide peds.

---

## 8. Log appendix (latest run)

Log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, 63,421 lines, 6,152,664 bytes.
One session, 22:35:22.682 → 22:42:48.161. Xbox mode (`pipes(bld=1 veh=2)`, in-game 17:05, outdoors).

### 8.1 Counts

| Tag | Count | Notes |
|---|---|---|
| `[BUILD]` | 1 | size 2360832, built Oct 25 2026 21:56:44 |
| `[Cull]` | 354 | 19 enter NONE, 125 enter BACK, 209 exit NONE, 1 baseline BACK |
| `[CullResync]` | 212 | **all** `mirror exit rw=1` |
| `[PedCull]` | **0** | build predates the lever/receipt |
| `[PedAlphaTest]` | **0** | build predates the receipt |
| `[PedOpaque]` | 213 | 1 pre-entry one-shot + 212 “contract repaired divergent entry” |
| `ped_contract` | **0** | old tag name; current receipt is `[PedOpaque]` |
| `[PedRS]` | 157 | 130 ok, 27 DIVERGED; cull 2 in 156, cull 1 in 1 |
| `[PED] ZWRITE` | 64 | all “off/desynced on entry … forcing TRUE”; sticky line 0 (tag starvation) |
| `[PedDual]` | 2 | legacy dual restored; first alpha probe material/prelit=0 texture=0 |
| `[DefCB]` | 1 | pre-unbind devVS=0 devPS=0 cacheVS/PS=0xFFFFFFFF |
| `[DefPipe]` | 1 | default-pipe contract forced canonical world signature |
| `shadow_contract` | 212 | 128 extra, 84 static; `[ShadowHeal]` 1; exit healed 0 |
| `[FxAlpha]` | 240 | 212 PostFx.repaired, 10 PostFx.exit, 6 RenderEffects.enter, 7 Coronas.SunReflection, 3 Coronas.Reflections, 2 non-screen raster |
| `[CoronaShield]` | 73 | all “forced additive … ztest=1->1” |

### 8.2 `[PedRS]` — every DIVERGED line (27)

Format: timestamp | line | cull | blend | aref (the other states agreed in every line). **Which layer disagreed** is called out.

| # | Timestamp | Line | cull | blend rw/drv/dev | aref rw/drv/dev | Disagreeing layer |
|---|---|---|---|---|---|---|
| 1 | 22:36:45.034 | 12031 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 2 | 22:36:47.032 | 12498 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 3 | 22:37:15.046 | 18842 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 4 | 22:37:19.044 | 19764 | 2/2/2 | 1/1/**0** | 100/100/**0** | L3 blend + aref |
| 5 | 22:37:25.095 | 21111 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 6 | 22:37:33.091 | 22713 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 7 | 22:37:35.093 | 23099 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 8 | 22:37:47.108 | 25089 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 9 | 22:37:49.107 | 25412 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 10 | 22:37:51.110 | 25756 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 11 | 22:37:53.111 | 26148 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 12 | 22:38:11.131 | 29369 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 13 | 22:38:27.121 | 32047 | 2/2/2 | 1/1/1 | 100/100/**1** | L3 aref |
| 14 | 22:38:29.121 | 32295 | 2/2/2 | 1/1/1 | 100/100/**1** | L3 aref |
| 15 | 22:38:31.123 | 32712 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 16 | 22:38:51.126 | 35329 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 17 | 22:38:53.121 | 35610 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 18 | 22:38:55.130 | 35901 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 19 | 22:38:57.127 | 36247 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 20 | 22:38:59.130 | 36564 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 21 | 22:39:43.249 | 41184 | 2/2/2 | 1/**0/0** | 100/100/**1** | L2+L3 blend + L3 aref |
| 22 | 22:40:01.248 | 44401 | 2/2/2 | 1/1/1 | 100/100/**1** | L3 aref |
| 23 | 22:41:01.246 | 50767 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 24 | 22:41:37.245 | 54837 | 2/2/2 | 1/**0/0** | 1/1/1 | L2+L3 blend |
| 25 | 22:42:12.218 | 57556 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |
| 26 | 22:42:32.251 | 60271 | 2/2/2 | 1/1/**0** | 100/100/**1** | L3 blend + aref |
| 27 | 22:42:42.281 | 62356 | 2/2/2 | 1/1/**0** | 100/100/100 | L3 device blend |

Raw lines (verbatim, for grep):

```
[22:36:45.034] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:36:47.032] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:15.046] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:19.044] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=0
[22:37:25.095] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:33.091] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:35.093] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:47.108] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:49.107] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:51.110] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:37:53.111] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:38:11.131] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:38:27.121] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=1 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=1
[22:38:29.121] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=1 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=1
[22:38:31.123] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:38:51.126] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:38:53.121] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:38:55.130] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:38:57.127] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:38:59.130] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:39:43.249] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=100 drv=100 dev=1
[22:40:01.248] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=1 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=1
[22:41:01.246] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:41:37.245] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:42:12.218] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
[22:42:32.251] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=1
[22:42:42.281] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
```

**Key point:** in all 27 lines `cull = 2/2/2` and `zw = 1/1/1`. The disagreement is always **blend** and/or **aref** at L3 (rarely L2+L3). No cull divergence at ped exit. That is the strongest single piece of evidence against a *persistent* cull-layer desync (it does not rule out a wrong-but-consistent cull value).

### 8.3 `[PedOpaque]` — distinct forms and representative lines

Only two formats exist:

```
[PedOpaque] pre-entry dev ALPHAREF=%u ALPHAFUNC=%u ALPHABLENDENABLE=%u ZWRITE=%u ZENABLE=%u ZFUNC=%u ALPHATEST=%u | rw ref=%u fn=%u vtx=%u zw=%u ztest=%u | drv ref=%u fn=%u blend=%u zw=%u
[PedOpaque] contract repaired divergent entry: dev(ref=%u fn=%u blend=%u zw=%u zen=%u zfunc=%u atest=%u) rw(ref=%u fn=%u vtx=%u zw=%u ztest=%u) drv(ref=%u fn=%u blend=%u zw=%u)
```

Counts across the 212 repaired entries: `dev blend=1` 140; `dev zw=0` 27; `dev zfunc=8` 14; `dev ref=100` recurring; `dev ref=0` recurring; `dev ref=1/2` at startup.

Representative:

```
[22:35:40.941] [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 | rw ref=2 fn=5 vtx=0 zw=0 ztest=1 | drv ref=2 fn=5 blend=0 zw=0
[22:42:08.220] [PedOpaque] contract repaired divergent entry: dev(ref=100 fn=5 blend=1 zw=0 zen=1 zfunc=4 atest=1) rw(ref=100 fn=5 vtx=1 zw=1 ztest=1) drv(ref=100 fn=5 blend=1 zw=1)
[22:42:22.226] [PedOpaque] contract repaired divergent entry: dev(ref=0 fn=7 blend=0 zw=1 zen=1 zfunc=4 atest=1) rw(ref=0 fn=7 vtx=0 zw=1 ztest=1) drv(ref=0 fn=7 blend=0 zw=1)
[22:42:34.300] [PedOpaque] contract repaired divergent entry: dev(ref=1 fn=7 blend=0 zw=1 zen=1 zfunc=8 atest=0) rw(ref=1 fn=7 vtx=1 zw=1 ztest=1) drv(ref=1 fn=7 blend=1 zw=1)
[22:42:46.329] [PedOpaque] contract repaired divergent entry: dev(ref=0 fn=7 blend=0 zw=1 zen=1 zfunc=4 atest=1) rw(ref=0 fn=7 vtx=0 zw=1 ztest=1) drv(ref=0 fn=7 blend=0 zw=1)
```

`ped_contract` (the old tag) has **0** lines. The current receipt is `[PedOpaque]`.

### 8.4 `[PED] ZWRITE` (64 lines, one format)

```
[PED] ZWRITE off/desynced on entry (rw=%d dev=%d) — forcing TRUE for cb
```

First / last:

```
[22:35:40.947] [PED] ZWRITE off/desynced on entry (rw=0 dev=0) — forcing TRUE for cb
[22:38:40.607] [PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb
[22:42:40.363] [PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb
[22:42:44.581] [PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb
```

Distribution: 1 × `rw=0 dev=0` (first frame), 63 × `rw=1 dev=0`. `[PED] ZWRITE sticky repair` = 0 (same throttle tag reused at exit; `main.cpp:1783-1785` and `2082-2084`).

### 8.5 `shadow_contract` (212 lines, two formats)

```
[shadow_contract] extra pre-entry layer drift: ...
[shadow_contract] static pre-entry layer drift: ...
```

212 total: 128 `extra`, 84 `static`. [`stored` never sampled in this session.] Representative:

```
[22:42:08.223] [shadow_contract] extra pre-entry layer drift: drift=2 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=1 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=0) dev(blend=1 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=0 fog=0)
[22:42:12.220] [shadow_contract] static pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=100 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
[22:42:40.250] [shadow_contract] extra pre-entry layer drift: drift=4 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=0) dev(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
```

`[ShadowHeal]` (only one):

```
[22:35:41.044] [ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)
```

No `exit healed=` line (throttle starved).

### 8.6 `[FxAlpha]` (240 lines, six formats)

Counts and one representative each:

| Format | Count | Representative |
|---|---|---|
| `PostFx.repaired: rw(vtx=0 src=5 dst=6 zw=1) dev(vtx=0 src=5 dst=6 zw=1)` | 212 | `[22:35:41.205]` |
| `PostFx.exit: layer drift BEFORE repair — rw(src=5 dst=6 vtx=0 fn=7 ref=0) d3d(...) dev(vtx=1 ... op=1)` | 10 | `[22:36:48.996]` |
| `RenderEffects.enter: layer drift BEFORE repair — ...` | 6 | `[22:36:10.996]` |
| `Coronas.SunReflection: layer drift BEFORE repair — rw(src=5 dst=2 vtx=0 fn=7 ref=0) ... dev(vtx=1 ...)` | 7 | `[22:36:26.996]` |
| `Coronas.Reflections: layer drift BEFORE repair — ...` | 3 | `[22:35:40.973]` |
| `Coronas.Reflections camRas=2048x1024 != screen 1768x992 — non-screen raster live` | 2 | `[22:35:40.974]`, `[22:35:42.975]` |

The drift is consistently **L3 vertex-alpha (blend enable)** and/or **ALPHAREF=0**; the PostFx chain repairs it every 2 s.

### 8.7 `[CoronaShield]` (73 lines, one format)

```
[CoronaShield] forced additive, was src=5 dst=6 blend=0 op=1 zw=1 atest=1 ztest=1->1
```

Example: `[22:35:41.045]`, `[22:42:34.256]`. Always `zw=1` pre-window, `ztest=1->1`.

### 8.8 Missing lines that matter

- `[PedCull]` — 0. The running build has no cull lever/receipt.
- `[PedAlphaTest]` — 0. The running build has no ATEST receipt.
- `[PED] ZWRITE sticky repair` — 0, but only because the tag is shared with the entry line (logging bug).
- `[ShadowHeal]` exit-healed — 0, same throttle starvation.
- `[PedRS]` does **not** read `D3DRS_ALPHATESTENABLE`, `D3DRS_ZENABLE`, `D3DRS_ZFUNC`, `D3DRS_COLORWRITEENABLE`, or the bound DS (`main.cpp:2110-2142`). This is the biggest blind spot for H3/H4.

---

## 9. Experiment matrix (ordered, minimal)

| Order | Change | Purpose | Pass (confirm) | Fail (kill) |
|---|---|---|---|---|
| **0** | `python tools/fast_build.py` + relaunch | Get the current levers into the DLL; the log build predates them | `[PedCull] mode=-1 saved=2 forced=2`, `[PedAlphaTest] enabled=1` appear | If they don't appear, the build/deploy is stale — stop and fix that first |
| **1** | `pedCullMode = 1` | Two-sided peds: no winding can hide a face | Peds visible from all angles → H1 confirmed (wrong cull/FRONT leak) | Peds still vanish → **H1 killed**; proceed |
| **2** | `pedCullMode = 3` | Reproduce the inside-out failure | Exact symptom reproduces → cull path confirmed | Does not reproduce → H1 narrowed to a transient leak |
| **3** | `stencilShadows = 0` (+ `pedShadows=0`) | Remove the only known FRONT source and the shadow overlay | Symptom disappears → shadow-volume cull/stencil leak; inspect the volume pass | Unchanged → shadow overlay ruled out |
| **4** | `pedAlphaTestEnable = 0` vs `1` | Isolate the ATEST path | Any visual change (hair/cutouts) but **same** angle visibility | Same on both → H4 dead (expected) |
| **5** | `pedOpaqueContract = 0` | Remove the ped depth/blend/ATEST repair | Symptom changes (expect worse) → contract is load-bearing | Unchanged → repair path not involved |
| **6** | `pedAmbientFloor = 1.0`, `ambientMult = 2.0` | Separate “black ped” from “missing ped” | Peds become visible → H7 (lighting, not culling) | Still gone → lighting ruled out |
| **7** | Code probes (§10) | Per-atomic/depth truth | `[PedGate]`/`[PedDraw]`/`[PedGeom]` lines identify the failing atomic | All canonical → residual |

**Recommended INI snapshot for the next run** (everything else unchanged):

```ini
[SkyGfx]
pedCullMode        = 1
pedAlphaTestEnable = 1
pedOpaqueContract  = 1
stencilShadows     = 0
pedShadows         = 0
stateBaselineEnable= 1
coronaZtest        = 1
```

Then repeat with `pedCullMode=2/3`, `stencilShadows=1`, etc., one variable at a time.

---

## 10. Code probes to add before the next run (small, read-only diagnostics)

1. **`[PedCull]` is already in the current tree** (`main.cpp:1762-1766`) — rebuild guarantees it. Add a *post-enforce* readback (cull rw/drv/dev) in the same line so the value at draw time, not entry time, is proven.
2. **`[PedDraw]` after `pedOpaqueEnforce` and after dual-leg 1** (`main.cpp:1833`, `2022-2024`): log `ATEST`, `ALPHAFUNC`, `ALPHAREF`, `ZWRITE`, `ZENABLE`, `ZFUNC`, `COLORWRITEENABLE`, `CULLMODE`, and the bound depth-stencil surface pointer (`d3d9device->GetDepthStencilSurface`) from all three layers. This closes every blind spot in `[PedRS]`.
3. **`[PedGate]`** at callback entry, once per unique `atomic->pipeline`: `pipe`, `skinPipe`, `RpSkinGeometryGetSkin(atomic->geometry) != NULL`, `atomic->geometry->flags & rpGEOMETRYNATIVE`, and a 3×3 determinant of the atomic's frame matrix. Emit `skinned-not-skinPipe` for the H2 case and `det<0` for H6.
4. **`[PedZWriteExit]`**: give the sticky-repair log its own throttle tag (the current `"PedZWrite"` reuse is why the 0-count is ambiguous).
5. **`[PedRS]`**: add `atest`, `zen`, `zfunc`, `cwr` and the DS pointer; drop the 2 s throttle for the *first* ped span after a config change so the A/B is captured.
6. **`[CullLeak]`**: in `pipeEnterCullMode`, when `saved != rwCULLMODECULLBACK`, log the previous owner if available (at minimum `saved` + the previous `pipeApplyWorldBaseline` tag / last pass tag). The current `[Cull] enter saved=1` lines cannot be attributed to a pass.

---

## 11. Bottom line

- The ped contract is doing its job for the states it owns: ZWRITE is forced ON (157/157 `[PedRS]`), blend/alpha refs are repaired at entry, and the 27 DIVERGED lines are all **post-exit** blend/aref restores, not draw-time failures.
- Therefore **alpha test, ZWRITE depth-off, and the shadow/corona overlays are not the view-angle mechanism**. Alpha test cannot be view-dependent; both overlays force ZWRITE off; the depth log shows writes on.
- The only remaining view-angle-dependent decision is **cull**. The log proves NONE reaches ped spans (first frame, mirror resync 212/212) and proves the cull cache is routinely left on a non-BACK value by a pass; it does **not** sample FRONT or the draw-time value often enough to exclude a stencil-volume FRONT leak or a mirrored atomic. The A/B lever written for this report was not in the 21:56 build, so the hypothesis has never been tested.
- Second most likely is a **gating hole**: skinned atomics not on `skinPipe` get no cull repair and a forced ATEST with the leaked ref. Nothing logs them today.
- Rebuild, then run `pedCullMode=1` (kills cull if peds still vanish), `stencilShadows=0`, and add the `[PedDraw]`/`[PedGate]` probes. Those three steps will either identify the cause or eliminate every state-based candidate in this audit.

---

### Appendix A — key source anchors (current snapshot)

| Topic | File:line |
|---|---|
| `Config configs[10]` zero-init | `main.cpp:80` |
| `pedAlphaTestOn` / `[PedAlphaTest]` | `main.cpp:1712-1719` |
| `pipeEnterCullMode` call | `main.cpp:1735` |
| `pedCullMode` lever | `main.cpp:1737-1766` |
| ZWRITE entry guard | `main.cpp:1768-1789` |
| `pipeForceAlphaBlock` call | `main.cpp:1791` |
| saved alpha pair restore | `main.cpp:1793-1808` |
| `pedOpaqueEnforce` call | `main.cpp:1833` |
| dual pass | `main.cpp:1962-2055` |
| `pipeExitCullMode` | `main.cpp:2087` |
| `pedOpaqueRestore` call | `main.cpp:2094` |
| `[PedRS]` monitor | `main.cpp:2096-2173` |
| `pedOpaqueCapture` | `main.cpp:1002-1057` |
| `pedOpaqueDiverged` | `main.cpp:1061-1073` |
| `pedOpaqueEnforce` | `main.cpp:1076-1119` |
| `pedOpaqueRestore` | `main.cpp:1122-1150` |
| `atomicHasAlpha` | `main.cpp:888-930` |
| `pedCullMode` parse | `main.cpp:6005-6011` |
| `ADD_IF_MISSING` ped keys | `main.cpp:6522-6523` |
| save ped keys | `main.cpp:6948-6949` |
| `shadowContractForce` | `main.cpp:1344-1383` |
| `shadowContractRestore` | `main.cpp:1385-1462` |
| `shadowContractRun` | `main.cpp:1474-1517` |
| `coronaDrawRawShield` | `main.cpp:2766-2860` |
| `CCoronas__Render_blendprobe_hook` | `main.cpp:4002-4130` |
| `pipeForceCullMode` | `pipelinecommon.cpp:1202-1243` |
| `pipeEnterCullMode` | `pipelinecommon.cpp:1246-1281` |
| `pipeExitCullMode` | `pipelinecommon.cpp:1283-1301` |
| `pipeApplyWorldBaseline` | `pipelinecommon.cpp:1975+` (cull 2031-2032, 2045) |
| `pipeForceAlphaBlock` | `buildingPipe.cpp:532-720` |
| `MirrorPass_ResyncCull` | `mirrorPass.cpp:365-379` |
| `RestoreRawGeomStates` (cull twin) | `postfx_utils.cpp:179,246-277` |
| envmap cull save/restore | `envmap.cpp:426,475` (`[CullResync] env pass`) |
| water cull | `waterPipe.cpp:1603-1605,1707` |
| SSAO cull NONE | `postfx_ssao.cpp:364` |
| Forward+ raw cull | `forwardplus.cpp:1874,1928` |

### Appendix B — raw grep commands used

```bash
grep -o "\[Cull\] [a-z]* [a-z]*=[0-9]*" skygfx_dbg.log | sort | uniq -c
grep -c "\[PedRS\] DIVERGED" skygfx_dbg.log
grep -n "\[PedRS\] DIVERGED" skygfx_dbg.log
grep -o "\[CullResync\] mirror exit rw=[0-9]*" skygfx_dbg.log | sort | uniq -c
grep -n "\[PED\] ZWRITE" skygfx_dbg.log
grep -c "\[PedCull\]\|\[PedAlphaTest\]" skygfx_dbg.log
grep -rn "rpGEOMETRYNATIVE\|determinant\|rpMATERIALFLAGS\|two.sided" src
```
