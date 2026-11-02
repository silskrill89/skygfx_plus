# 2026-10-24 — Corona black-quad diagnosis (19:06 run) + exact fix spec

Status: investigation complete, READ-ONLY (no source edited).
Evidence base: `skygfx_dbg.log` of the 19:06:19–19:07:22 run (captured before the file was
overwritten at 20:01:40 by a later run), the exe on disk (`E:\games\gtasa_skygfx_plus\gta_sa.exe`,
Hoodlum 1.0 US, image base 0x400000), `SilentPatchSA.asi` bytes, current `src/core/main.cpp`
(mtime 19:58 — **being edited concurrently; line numbers are as of this read, 6347 lines**).
Corroboration: the live 20:01–20:07 run on the 19:55 build (256-bucket throttle) — same code
paths, different location.

---

## 0. Verdict (one paragraph)

The 19:06 zeros are **not** an install failure and **not** "the hooks never ran". The corona
**pool was empty all run** (`rej(id0=64)` in all 9 `[CoronaFeed]` samples → all 64
`CRegisteredCorona` slots have `m_dwId == 0`), so `CCoronas::Render` appended **zero** quads,
`0xC6A158` stayed 0, and the flush's count guard at `0x70CF27` returned **before** the two
hooked draw calls (`0x70CF62` / `0x70CF9B`). Independently, the 19:06 binary still used the
**64-bucket x31 throttle** (the 256-bucket FNV version is an *uncommitted* change made at
~19:56, after the run — `git diff src/skygfx.h`), and all three corona tags collided with tags
that did fire; the log itself proves the collision starved `corona_exit` for ~20 s. The black
squares therefore are **not** `CCoronas::Render` quads: they are the **shared buffered-sprite
flush** (`0x70CF20 → 0x734EA0`) drawing **CPointLights glow sprites** pushed by
`CSprite::RenderBufferedOneXLUSprite_Rotate_Aspect` (`0x70E780`, hooked call site `0x700B6B`),
at a flush that happens **outside** the shield window — the un-intercepted early
`CPointLights::Render` call site `0x53DE6B` — while the device sits on
`ALPHABLENDENABLE = FALSE` even though the RW caches say TRUE (the exact cache↔device split
logged every frame at `Coronas.Reflections`: `rw vtx=1 / d3d vtx=1 / dev vtx=0`). With blend
off the sprite texture's black border is written by REPLACE: a black square with the tiny
coloured core. The yellow triangle is the marker path (`0x60BA80`), which bypasses this batch
entirely.

---

## 1. Binary facts (verified, capstone on the on-disk exe)

### 1.1 The sprite batch

`0x70CF20` = the **only** rasteriser of the buffered sprite batch:

```
70CF20  mov  eax,[0xC6A158]      ; sprite count
70CF25  test eax,eax
70CF27  jle  0x70CFAD            ; <=0 -> return, NO draw, count reset skipped
70CF2D  mov  ecx,[0xC6A15C]      ; appender flag
70CF3C  push 0 / push 6 / call [RwD3D9+0x20]   ; rwRENDERSTATEZTESTENABLE=0 (flag!=0 only)
70CF60  push 3
70CF62  call 0x734EA0            ; primType=3, 0xC5F958, count*4, 0xC4B958, count*6
70CF73  push 1 / push 6 / call   ; ZTESTENABLE back to 1
70CF76  mov  [0xC6A158],0        ; count reset
```

Two draw paths, both hooked (`0x70CF62` and `0x70CF9B`), both verified `E8 rel32 → 0x734EA0`
in the exe and confirmed by `[corona_install] ... op=E8 orig=00734EA0OK` on both sites.
`0x734EA0` is a **stub**: `jmp 0x403927` → obfuscated indirection → `jmp [edx+0x34]` (the real
`RwIm2DRenderIndexedPrimitive` is resolved at runtime). The rel32 math in `InterceptCall`
(`src/rw/MemoryMgr.h:87` = `InjectHook` PATCH_NOTHING) is correct because the opcode is E8.

Other callers of `0x734EA0` (they **bypass** the flush/hook): `0x701FE9` (a postfx sprite
builder called from `0x705116` inside `CPostEffects::Render`), `0x7058BC` (postfx),
`0x726284`, `0x7273FF` (particle/Im2D systems). None of these feed the sprite batch.

Flush entry points (producers): `0x6FB621` (CCoronas tail, vanilla), `0x700BCD`
(CPointLights tail), `0x70E48C/0x70E76F/0x70EA9F/0x70EDD1` (appender overflow tails),
`0x70F409/0x70F531/0x70F756` (`0x70F440` family tails), plus direct `E8` calls from
`0x6FB9C2`, `0x714019`, `0x71422A`, `0x714387`, `0x715EFB`, `0x719B26`.

### 1.2 The producers

* `CCoronas::Render` `0x6FAEC0`: entry contract `0x6FAEF8–0x6FAF25` =
  `rwZWRITEENABLE=0`, `rwVERTEXALPHAENABLE=1`, `rwSRCBLEND=rwBLENDONE`, `rwDESTBLEND=rwBLENDONE`,
  `rwZTESTENABLE=1`. Body appends via `0x70F440` (`0x6FB461/0x6FB561/0x6FB5EA`) into
  `0xC5F958` with count `0xC6A158`; loop is **fixed 64 slots** (`mov [esp+0x58],0x40`,
  `add edi,0x3C`) and **skips every slot whose `id` (base+0x0C) is 0**.
* `CPointLights::Render` `0x7002D0`: entry `0x7002E7–0x700346` = `rwFOGENABLE=0`,
  `rwZWRITEENABLE=0`, `rwZTESTENABLE=1`, `rwVERTEXALPHAENABLE=1`, `SRCBLEND=ONE`,
  `DESTBLEND=ONE`, texture from `[0xC6AA78]` (the shared 2D-sprite texture global written by
  CClouds at `0x712FA1/0x7138F9/0x7140CD`). Draws fog cones at `0x700817` and glow dots at
  `0x700B6B` (both call `0x70E780`); **tail is an unconditional `jmp 0x70CF20` at `0x700BCD`**.
  Early-out only on `CCutsceneMgr::ms_running` (`0xB5F851`, `0x7002E1 → 0x700BD2`, skips even
  the tail flush). It is called from **two** sites: `0x53DE6B` (sky/reflection phase, before
  `RenderScene`) and `0x53E21D` (RenderEffects).
* `CSprite::RenderBufferedOneXLUSprite_Rotate_Aspect` `0x70E780`: writes 4 Im2D vertices at
  `0xC5F958 + count*0x70`, sets `[0xC6A15C]=0` at `0x70E78E`.
* `0x70F440` (corona appender) sets `[0xC6A15C]=1` at `0x70F454` (the dword SilentPatch zeroes
  at `0x70F45A`). `[0xC6A15C]` only toggles ZTEST in the flush — **it has nothing to do with
  blending**.
* Markers `0x60BA80` (called only from `0x53E20E`): sets its own states at `0x60BA9B–0x60BAE7`
  (`ZWRITE=0`, `ZTEST=1`, `SRC=SRCALPHA`, `DEST=INVSRCALPHA`, `VERTEXALPHA=1`, texture NULL) and
  draws its own geometry via `0x7EF450/0x7EF520/0x7EF550`. It never touches `0x70E780` or
  `0x70CF20`. This is the solid yellow triangle.

### 1.3 SilentPatch (both patches are conditional and irrelevant to the zeros)

Bytes in `SilentPatchSA.asi` (~+0x14D58..+0x14DA5):

```
83 3D 04 78 06 10 00   cmp dword [0x10067804],0 / je skip
83 3D 08 78 06 10 00   cmp dword [0x10067808],0 / je skip
C7 05 5A F4 70 00 00 00 00 00   mov dword [0x70F45A],0     ; appender flag -> 0
BA 20 CF 70 00                  mov edx,0x70CF20
66 C7 05 21 B6 6F 00 C3 E8      mov word [0x6FB621],0xE8C3 ; tail jmp -> RET
B9 23 B6 6F 00                  mov ecx,0x6FB623
...
```

The file on disk is vanilla (`0x6FB621 = E9 → 0x70CF20`, `0x70F45A` immediate = 1); the mod
patches at runtime, gated on its own two option flags. **Whether it was active in the 19:06
run cannot change the result**: with an empty corona pool the batch count is 0, so the vanilla
tail flush at `0x6FB621` also hits the `jle 0x70CFAD` guard before the hooks. SilentPatch's
`0x70F45A` write only disables the ZTEST-off flag, not blending. → **not the cause of either
symptom.**

---

## 2. Log facts (19:06 run)

| Fact | Evidence |
|---|---|
| Hooks installed | `[INIT] CoronaBlend probe: entry 0x6FAEC0 hooked`; `[corona_install] ... OK \| ... OK`; `[pointlights_install] ... OK` |
| `CCoronas::Render` body ran every frame | `[CoronaBlend]` ×18 at the exact 2 s throttle cadence over 37 s (19:06:45.742 → 19:07:21.595) |
| Exit restore ran (not just once) | `[CoronaExit]` ×1 is **by construction** one-shot (`static bool coronaExitLogged`, main.cpp:3050) |
| Corona pool empty | `[CoronaFeed] pool=… fed=0 rej(id0=64 sz=0 rgb0=0 int0=0) regFed=7` ×9; walk base/stride verified against `0x6FAF50` (`edi-0x2C` = id at base+0x0C) and `0x6FC35D–0x6FC369` (id store at `0xC3E064 + i*0x3C`, count `0xC3E038`) |
| Cache↔device split every frame | `[FxAlpha] Coronas.Reflections: … rw(vtx=1) d3d(vtx=1) dev(vtx=0) …` (all 13 occurrences) |
| End-of-frame blend OFF | `[FxAlpha] PostFx.repaired: rw(vtx=0 src=5 dst=6 …) dev(vtx=0 …)` (all occurrences) |
| `CoronaDraw` / `CoronaShield` / `corona_resync` | 0 / 0 / 0 |
| `[CoronaExit]` delayed ~20 s | first possible window exit 19:06:45.74 (first `[CoronaBlend]`), line at 19:07:05.281 → bucket 33 starvation (see Q3) |

---

## 3. Answers to the five questions

### Q1 — Which call site actually rasterises the black quads?

**The sprite-batch flush `0x70CF20 → 0x734EA0` (call sites `0x70CF62`/`0x70CF9B`), fed by
`CPointLights::Render` glow sprites (`0x700B6B → 0x70E780`), at a flush outside the shield
window.**

* **Not `CCoronas::Render`**: the 64-slot pool is empty in every sample (`rej(id0=64)`), and
  the body skips `id==0` slots; the count stays 0 → `jle 0x70CFAD` → the two draw calls are
  never reached. `[CoronaBlend]` fires because the function is called, not because it draws.
* **Not the fog cones** (`0x700817`): cones are only drawn for `fogType 1/2`, are large/metric
  sized additive gradients; the user reports *small* squares.
* **Not markers** (see Q4).
* The tiny red/green/yellow core is the glow sprite's vertex colour, which the hook at
  `0x700B6B` re-derives from the CPointLights registry (`pointLights[currentLight].color[]`,
  main.cpp:1783-1786). The black border is the sprite texture's RGB ≈ 0 background being
  written by REPLACE when `D3DRS_ALPHABLENDENABLE = FALSE`.
* **Why no shield**: the producer is the **early** `CPointLights::Render` call `0x53DE6B`
  (sky/reflection phase, before `RenderScene`), and only `0x53E21D` is intercepted
  (main.cpp:6173-6195). At `0x53DE6B` the window is always closed (it was closed at the
  previous frame's `DrawFinalEffects` / this frame's `RenderScene_before`), so
  `coronaDrawRawShield` returns immediately and no log is emitted. This is the blind spot.
* Live corroboration (20:0x run, 256-bucket build): `[CoronaDraw]` ×33, **all** at
  `SpriteFlush_70CF20`, pre-force `dev blend=1 src=5 dst=2` (30×) or `src=2 dst=2` (3×) — i.e.
  the *windowed* (late, `0x53E21D`) flush is already additive and is not the offender. The
  un-windowed early flush is exactly the one that never logs.

### Q2 — Is `SpriteFlushIndexedDraw_hook` reachable in the window at all?

**Yes, the hook is correctly installed and reachable — but only when the batch count is > 0,
and the log lines are additionally gated by the window flag, so "reachable" ≠ "logged".**

* Both sites are real `E8` calls to `0x734EA0` in the live exe (verified by disassembly and by
  the runtime install check). `InterceptCall` rewrites the rel32 only — correct, and it works
  (live run proves it fires).
* The flush that matters *is* `0x70CF20`; its two calls are the only sprite-batch draw calls.
  No internal call of `RwIm2DRenderIndexedPrimitive` bypasses the hook — `0x734EA0` is the
  game's entry stub. Other `0x734EA0` callers (`0x701FE9`, `0x7058BC`, `0x726284`,
  `0x7273FF`) are separate Im2D users, not the sprite batch.
* **Reachability failure modes in the 19:06 window**: (a) count 0 (empty corona pool) → the
  draw calls are skipped; (b) a flush outside the window → hook runs but both log paths
  (`[CoronaDraw]`, `[CoronaResync]`) are inside `if(g_inCoronasRender)` / `if(g_coronaBodyDone)`
  and emit nothing; (c) 64-bucket starvation (see Q3).
* The window is force-closed at **four** frame boundaries — `RenderScene_before`
  (main.cpp:2187), RenderEffects exit (2677), CPostEffects exit (2706), `DrawFinalEffects`
  (5751) — plus the flush close (1849) and the corona/CPointLights entry re-arms. Any flush at
  `0x53DE6B` (before RenderScene) is outside by construction.

### Q3 — Can `dbglog_throttle` starvation fully explain zero?

**For `CoronaDraw` and `corona_resync`: yes, starvation is a sufficient explanation for the
missing lines (the code paths can have run). For `CoronaShield`: no single answer — either the
`broken` condition was false, or bucket 44 was starved. In all three cases the zero is a
logging artifact at least in part; it is NOT evidence that the hooks are broken.**

19:06 binary used the old 64-bucket x31 hash (proven: the 256-bucket FNV rewrite is an
uncommitted diff created after the run; `git diff src/skygfx.h` shows `static double lastTime[64]`
→ `static DWORD lastTick[256]`). The log **proves** collision starvation in this exact run:

| tag | old bucket (x31 mod 64) | collision partner | partner fired? |
|---|---|---|---|
| `corona_feed` | 33 | `corona_exit`, `sky_ps`, `postfx_report`, `pf_tonemap`, `ras_unsafe` | yes — `[CoronaFeed]` ×9 |
| `corona_exit` | 33 | same | delayed to 19:07:05.281 although the first window closed at ~19:06:45.74 |
| `corona_draw` | 7 | `mirror_gates_passed` | mirror pass installed |
| `corona_shield` | 44 | `pc_c1chk`, `gim2d_nocam`, `pf_lens3`, `pf_mb_gate`, `ivmode_reapply`, `tmax_clamp` | **`c1chk ok` ×17** (2 s cadence) |
| `corona_resync` | 17 | `pc_noclassify`, `blend_leak`, `preset_custom`, `ssao_ov_enter` | classify path active |

The `[CoronaExit]` line is the smoking gun: it is a `static bool` one-shot that should fire on
the *first* window exit; it appeared only at 19:07:05.281 — ~19.5 s after the first `[CoronaBlend]` (19:06:45.742), the first frame in which the window could exit. That is
bucket-33 starvation between `corona_feed` and `corona_exit`, exactly the same mechanism that
starves `corona_shield` against `pc_c1chk` (which demonstrably logged 17× in that run).

Independent of starvation, **`CoronaShield` legitimately may not have run**: its body is
`if(broken)` where `broken = blendEn ? (dstB == INVSRCALPHA) : TRUE` (main.cpp:2895-2896). The
live run catches it 3× with `blend=0 src=5 dst=6` and `src=2 dst=2` — i.e. sometimes the entry
is healthy (dst=ONE) and the shield correctly does nothing. The 19:06 after-body probe values
(`dev blend=1 src=5 dst=2` ×9, `src=2 dst=2` ×9) are consistent with a healthy entry.

`CoronaDraw` in the 19:06 run additionally has the **genuine** non-execution case: the corona
batch was empty, so the only windowed flush that could produce it is the CPointLights batch.
That one did run (7 registry lights, live run proves the batch draws), and its line was most
likely starved by bucket 7.

### Q4 — Does the marker path bypass the corona batch?

**Yes, completely.** `0x60BA80` (`C3dMarkers::Render`, only caller `0x53E20E`) sets up its own
normal-alpha block at `0x60BA9B–0x60BAE7` and draws its own geometry through the RW Im2D
functions (`0x7EF450/0x7EF520/0x7EF550`), never `0x70E780`/`0x70CF20`. It is the solid yellow
triangle in the screenshot and is unaffected by every blend bug discussed here.

### Q5 — Exact fix spec

Two source changes + diagnostics. The recommended option is **(a), but made unconditional at
the flush**, not window-scoped. (b) is not viable: the appenders write vertex data only and
`0x70F440`'s flag is ZTEST, not blend; per-sprite CPU work would still leave the device state
wrong at rasterisation. (c) is too broad: `0x734EA0` is shared by postfx full-screen quads,
particles and markers; forcing additive there would break the postfx chain.

#### Patch 1 (core) — make the raw draw-time shield unconditional for the sprite flush

`src/core/main.cpp:1705` add a `force` parameter; `src/core/main.cpp:1842` pass `true`.

```c
// main.cpp:1705
static void coronaDrawRawShield(const char *where, bool force = false)
{
    if((!g_inCoronasRender && !force) || !d3d9device)
        return;
    ...
}

// main.cpp:1842 inside SpriteFlushIndexedDraw_hook, replace the call:
    coronaDrawRawShield("SpriteFlush_70CF20", true);   // force: every sprite flush
```

The body already forces exactly the right block through L1+L2+raw L3 (park-and-set):
`ALPHABLENDENABLE=TRUE`, `SRCBLEND=SRCALPHA(5)`, `DESTBLEND=ONE(2)`, `BLENDOP=ADD(1)`,
`ZWRITEENABLE=FALSE`, `ALPHAFUNC=GREATEREQUAL(7)`, `ALPHAREF=0`,
`SEPARATEALPHABLENDENABLE=FALSE`, `COLORWRITEENABLE=0xF`. Keep the per-sprite
`CSprite_70E780` call at `main.cpp:1790` window-gated (`false`) — the flush covers it.

Why this is safe: every in-exe producer of this batch sets additive itself — CCoronas
`0x6FAEF8–0x6FAF25` (ONE/ONE), CPointLights `0x7002E7–0x700330` (ONE/ONE), CClouds
`0x71396B–0x7139A7` (ONE/ONE + ZTEST off). In the live run every observed flush was already
`blend=1 src=5/2` before the force, so for the normal path this is a no-op; it only repairs the
broken/dev-drifted flush. Residual risk: a flush that genuinely wanted normal alpha
(SRCALPHA/INVSRCALPHA) would become additive — no such producer exists in this exe; the
`0x715EFB`/`0x719B26` effect batches are additive sprites (they follow `0x70E4A0`/`0x70EAB0`
appends defined for additive use).

Prefer `DESTBLEND=ONE` over `INVSRCALPHA`; `ONE` is the only value that makes a black texture
border contribute 0 under all src choices. `SRCALPHA` (5) preserves the vertex-alpha fade; `ONE`
(2) matches gta-reversed's vanilla corona entry. The existing 5/2 pair is fine and already
field-proven.

#### Patch 2 — stop the exit restore from leaving blend OFF

`src/core/main.cpp:2870-2915` capture the pre-window `ALPHABLENDENABLE` (new POD
`g_coronaPreBlend`, same place as `g_coronaPreFuncDev`); `src/core/main.cpp:3051` and `3056`
currently hard-set blend OFF:

```c
RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);   // :3051
...
RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);              // :3056
```

Replace both with `g_coronaPreBlend` (fallback `TRUE`). The comment at `:3026` says "canonical
normal-alpha" but the code writes blend-off, which is exactly what the next
`[FxAlpha] … dev(vtx=0)` line reports. Risk: negligible; it restores what the function found
instead of imposing a REPLACE state on the rest of the frame. This does not by itself fix the
raw-writer split (Patch 1 does), but it stops manufacturing it.

#### Patch 3 (diagnostics, required to prove it)

* `SpriteFlushIndexedDraw_hook`: replace the `if(g_inCoronasRender)` probe at `:1827` with an
  **unconditional** counter and a `dbglog_first`-style receipt that reports `blend/src/dst`
  *and* `nverts/nindices`/`vptr` (identifies the batch) before the force, plus a per-2 s
  summary line `flushes=N blendOff=N dstZero=N forced=N`. Use `dbglog_once` for the first hit
  and a plain counter for the rest — never a 2 s boolean for the only evidence.
* `[CoronaShield]`: keep the `dbglog_throttle`, but add `static int s_shieldHits;` and log
  `hit#%d` on the first 3 plus in the 2-s summary. Same for `[CoronaDraw]` and `[CoronaResync]`.
* Fix the `[CoronaFeed]` count read: `0xC3E038` is a **dword** (`0x6FC35D` inc / `0x6FC48B`
  dec); `main.cpp:2629` reads `*(uint16_t*)0xC3E038`, which is why the log shows nonsense
  `pool=65524, 62550, …`. Read the dword and also print the number of nonzero ids.
* Optional: intercept `0x53DE6B` as well (same `CPointLights__Render_shield_hook`, one more
  `InterceptCall` after `main.cpp:6193`) so the early flush is windowed too. Not needed once
  Patch 1 is in, but it makes the two passes symmetric and self-documenting.

#### Why window-scoped logic keeps failing (root cause of the recurring regressions)

1. **Producer ≠ rasteriser.** `CCoronas::Render`, `CPointLights::Render` and the CSprite
   appenders all push into one buffer (`0xC5F958` / `0xC6A158`) and the buffer is drawn by
   whichever consumer flushes next — 15 entry points. A window tied to one body cannot cover a
   shared queue.
2. **Two CPointLights passes, one hooked.** `0x53DE6B` (sky/reflection, before `RenderScene`)
   is un-intercepted; `0x53E21D` (RenderEffects) is. The un-hooked one is exactly where the
   previous frame's postfx leak is still live.
3. **Four frame-boundary closes** (`RenderScene_before:2187`, `RenderEffects exit:2677`,
   `CPostEffects exit:2706`, `DrawFinalEffects:5751`) guarantee the window is dead by the time
   the next frame's sky-phase flush runs; the (`SilentPatch`-deferred) batch that survives the
   frame is therefore always drawn unshielded.
4. **Every CCoronas entry clears the flags** (`main.cpp:2869-2872`, "accepted defensive loss")
   — a pending batch whose flush did not arrive loses its shield.
5. **Gate and telemetry are the same flag.** A window miss produces neither the shield nor a
   log line, so the failure is invisible (the 19:06 `[CoronaDraw] 0` is exactly this).
6. **The actual defect is a cache/device split**, not a factor choice. `rw/d3d` say
   `ALPHABLENDENABLE=1`, the device is 0 (`[FxAlpha]` every frame). Any producer that calls
   `RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, TRUE)` gets a cache no-op and draws
   REPLACE. Only a raw device write at the rasterisation boundary fixes that deterministically;
   all window-scoped L1/L2 "park-and-set" tricks are defeated when the flush happens outside
   the window or after the raw writer.

---

## 4. Black blotches (pools): same batch or PipeChain?

**Not the sprite batch.** Evidence: (i) the corona pool was empty in the 19:06 run, so the
batch carries only CPointLights/cloud sprites (metric-sized quads, not world-scale pools);
(ii) every windowed flush in the live run is additive *before* the force (`dev blend=1
src=5/2`), so that batch is not painting black; (iii) CPointLights fog cones exist only for
`fogType 1/2` and are additive gradients — with blend broken they would be black *cones* with
a bright core, not huge irregular pools.

**Prime suspect: PipeChain pass 3.** `src/render/postfx.cpp:3212-3216` names the symptom
verbatim: *"A present-but-empty / stale / half-res normal buffer still passes the shader's nlen
gate and scatters reflection UVs into dark regions → black ink-blot pools."* Pass 3 is a
full-screen re-presentation (`postfx.cpp:3462-3525`) that runs every frame in the live log
(`[PipeChain] pass3: reflection off (reflEnable=0 nbDead=0) pack0 dummy`, 27 `[PipeChain]`
lines in the 19:06 run, `c1chk ok` ×17). The reflection term is either killed (dummy pack) or
driven by the classify/normal buffers; both can darken world-shaped regions.

**A/B to settle it (no code):** run once with `pipeChainEnable=0` (or `pipeChainReflEnable=1`
when it is currently 0) and compare the blotches; if they vanish → pass 3. Do not spend sprite
time on this lane.

---

## 5. Proof lines for the next run

After Patch 1 + Patch 3, the log should contain, in this order:

1. `[corona_install] flush draw sites: 0x70CF62 op=E8 orig=00734EA0OK | 0x70CF9B op=E8 orig=00734EA0OK`
   (unchanged, confirms the sites).
2. One unconditional first-hit receipt from the flush hook:
   `[FlushShield] hit#1 pre dev blend=0 src=5 dst=6 nverts=NNN nindices=NNN -> forced blend=1 src=5 dst=2 zw=0`
   — the `blend=0` is the proof that the previous build was drawing the batch in REPLACE.
3. A 2-second summary that cannot be starved:
   `[FlushShield] flushes=NNN blendOff=N dstZero=N forced=N (last nverts=NNN)`.
   `blendOff > 0` before the force means the black-quad path is real; `forced` should equal the
   number of flush calls after Patch 1.
4. `[CoronaDraw] hit#N pre-force dev blend=… src=… dst=…` at least once with `blend=0` — if it
   never shows `blend=0`, the offender is a non-flush Im2D path and the next step is
   `0x701FE9`/`0x7058BC`/`0x726284`/`0x7273FF` (particles/postfx), not this lane.
5. `[CoronaShield] hit#1/2/3 was src=… dst=… blend=…` (proves the entry shield ran at least
   once; no more zero-vs-starvation ambiguity).
6. `[CoronaPool] count=NNN activeIds=NNN` (dword; replaces the bogus `pool=65524`) — if
   `activeIds > 0` and the coronas are visible, re-check that the black squares are not corona
   quads after all.
7. `[PipeChain] pass3: …` with the A/B result for the blotches.

Acceptance: the small black squares disappear while `[FlushShield] blendOff>0` was non-zero
before the force; the `[CoronaShield]`/`[CoronaDraw]` counts are non-zero in the next log even
if the visuals are unchanged (that would prove the remaining offender is outside the batch).

---

## 6. Caveats / open items

* The on-disk `skygfx_dbg.log` is now the 20:01+ run (the 19:06 file was truncated at
  20:01:40). All 19:06 numbers here were captured before that; they match the requester's tag
  counts exactly.
* `src/core/main.cpp` is being edited concurrently (mtime 19:58). The line numbers above are
  the 6347-line version at read time; the function names are stable.
* The `[CoronaFeed]` `pool=` field was always garbage because of the `uint16_t` read; it does
  not affect the `id0=64` conclusion (which uses the verified array base/stride).
* `0xC3E038` drifting (65524 → 28503 over 2 min, both runs) suggests the corona counter is
  being driven negative by removals; worth a separate look because it feeds
  `RegisterCorona` slot selection. Not required for this fix.
