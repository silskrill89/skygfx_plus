# SDK Compare — Sprites / Coronas / CPointLights / Clouds Render State (2026-10-26)

Fork: `skygfx_plus_expIV` vs `skygfx_original` (aap) / `skygfx_junior` (our ancestor) / `skygfx` (MTA) / `skygfx_aap_v4.2b` binary.

Symptom: at night, street lamps / traffic lights / fog cones draw as opaque black rectangles with a small coloured dot in the centre instead of an additive soft glow.

## 1. Reference-tree state writes (aap / junior)

Trees compared: `skygfx_original/src/main.cpp` (1733 lines, aap clean),
`skygfx_junior/src/main.cpp` (1935 lines, our ancestor),
`skygfx/src/main.cpp` (MTA fork). **The MTA fork is byte-identical to
`skygfx_original`** (`diff` exit 0), so every line reference below applies to
both. `skygfx_aap_v4.2b/.../skygfx.asi` is binary-only (303,616 B, 2020-04-30);
no state writes can be enumerated from it without disassembly — the accepted
proxy is `skygfx_original/src` (same author/release line), and nothing in that
source contains the symbols we added.

### 1.1 Grep proof — there is NO corona/sprite/cloud shield in any reference tree

```
for p in 6FAEC0 6FB621 70CF20 734EA0 7002D0 700817 700BCD ; do
  grep -c $p skygfx_original/src/main.cpp ; grep -c $p skygfx_junior/src/main.cpp ; done
→ 0 / 0 for all seven addresses, in BOTH trees.
grep -c "effectsForceAlphaBlock\|pipeForceAlphaBlock\|CoronaWindow_Close" \
     skygfx_{original,junior}/src → 0
```

Both trees touch exactly **one** address in the corona corpus: the `0x6FB17C`
Z-test call inside `CCoronas::Render` (3 bytes NOP/patch, `coronaZtest`), plus
the single point-light sprite call site `0x700B6B`. No entry hook, no flush
hook, no cloud hook, no alpha-block sweep, no capture/restore, no window.

### 1.2 `skygfx_original` (≡ MTA) — every relevant write

| # | Site | Writes | Lines |
|---|------|--------|-------|
| 1 | `fixSAMP` | FOGENABLE / FOGTABLEMODE / FOGVERTEXMODE only | 84-109 |
| 2 | `D3D9RenderDual` (dual-pass vehicle/default) | **reads** ALPHABLENDENABLE (128); ZWRITEENABLE (129,135,138,140); ALPHATESTFUNCTION (130,134,137,142,144,146); ALPHATESTFUNCTIONREF (132,133,141). Never reached from the sprite path | 121-149 |
| 3 | `resetValues` — `coronaZtest` | `Nop(0x6FB17C,3)` = 0 (PS2), `FF 51 20` = 1 (PC), `-1` = untouched. **Z-test only; no blend, no ZWRITE, no alpha test** | 226-240 |
| 4 | `grassRenderCallback` dual pass | ZWRITE / ALPHATEST / ALPHATESTFUNCTIONREF | 274-292 |
| 5 | `myDefaultCallback` dual pass | ZWRITE / ALPHATEST / ALPHATESTFUNCTIONREF | 308-331 |
| 6 | `CTagManager__RenderTag` | ALPHATESTFUNCTIONREF only | 359-363 |
| 7 | **`CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect` wrapper** | **NO render-state write at all** — reads `esi`/`ebp`, rewrites r,g,b from `pointLights[]` (`+16` when `fogType!=1`), forces `f=0xFF`, tail-calls 0x70E780 | 660-680 |
| 8 | `setMoonAlphaBlendStates/restore` | SEPARATEALPHABLENDENABLE, BLENDOPALPHA, SRC/DESTBLEND**ALPHA**, ALPHATESTFUNCTION | 693-716 |
| 9 | `CSkidmarks__Render` | ALPHATESTFUNCTION (ALWAYS around orig) | 718-726 |
| 10 | install | `InjectHook(0x700B6B, CSprite__…_Rotate_Aspect)` — **single call site only** | 1606 |

`CCoronas::Render`, `CPointLights::RenderFogEffect`, `CClouds::Render`,
`CSprite::FlushSpriteBuffer` (0x70CF20) and the shadow passes are **untouched
in aap**. Contract left to the game: additive `ONE/ONE` + `ZWRITE=0` +
`ZTEST=1` + `VERTEXALPHA=1` set by each producer and never restored.

### 1.3 `skygfx_junior` — aap plus two cloud/water changes

Identical to §1.2 (same 11 `SetRenderState` hits, same moon/skidmark blocks,
same `0x700B6B` single-site wrapper at 712-731 and 1808, same `0x6FB17C`
`coronaZtest` switch at 248). Additions:

| # | Site | Writes | Lines | State |
|---|------|--------|-------|-------|
| 1 | `disableClouds` | `InjectHook(0x714145, 0x71422A, PATCH_JUMP)` — skips the cloud loop. **No render-state write**; side effect is that `gpCloudTex`/the shared 2D-sprite raster is never (re)bound that frame | 1637-1639 |
| 2 | `CWaterLevel__RenderAndEmptyRenderBuffer_hook` | `_rwD3D9RenderStateFlushCache()`, `rwRENDERSTATEVERTEXALPHAENABLE=true`, `RwD3D9SetPixelShader(simplePS)`, `RwD3D9DrawIndexedPrimitiveUP` — **the only junior code that competes for a TempBuffer flush** | 889-924 | **DEAD**: all four call-site intercepts are commented out (1706-1709). Uses the 3D water buffer `0xC4D958`, not the sprite buffer `0xC5F958`. |

**Conclusion for task 1:** neither reference tree contains *any* corona /
point-light / cloud / sprite-flush state shield. The only point-light code is
the colour rewrite at `0x700B6B`, which changes per-sprite RGB, never state.
Any state repair in our tree is therefore an addition, not a port.

## 2. Our-tree additions (file:line + presence in aap/junior)

All line numbers are `src/core/main.cpp` (8,625 lines). "aap/junior" answers
whether the **addition** exists in `skygfx_original` / `skygfx_junior` (grep
proof: both are 0 for every symbol below).

| Addition | Lines | Installed at | aap | junior |
|---|---|---|---|---|
| `g_inCoronasRender` / `g_coronaBodyDone` / `g_inCloudsRender` | 2869 / 2875 / 2883 | file-scope flags | NO | NO |
| Pre-window capture block (`g_coronaPre*`, 9 states) | 2897-2916 | filled in the CCoronas entry hook | NO | NO |
| `coronaDrawRawShield(where, force, src, dst)` | 2955-3025 | called at 3239 (0x700B6B), 3342/3346 (flush) | NO | NO |
| `SpriteFlushState` + `spriteFlushShieldCapture()` | 3037-3111 | every non-empty flush | NO | NO |
| `spriteFlushShieldClose(path)` | 3119-3201 | every forced flush | NO | NO |
| `CSprite__…_Rotate_Aspect` wrapper (**no** theme hop) | 3209-3241 | `InjectHook(0x700B6B, …)` at 8502 | **YES** (660-680, identical colour math) | **YES** (712-731) |
| `SpriteFlushIndexedDraw_hook` (flush force + window close + 5/6 re-sync) | 3273-3413 | `InterceptCall` 0x70CF62 **and** 0x70CF9B at 8422-8423, byte-verified at 8402-8425 | NO | NO |
| `CPointLights__Render_shield_hook` | 3439-3455 | `InterceptCall` 0x53E21D + 0x53DE6B at 8446/8470 (both byte-verified) | NO | NO |
| `CoronaWindow_Close()` | 3465-3530 | 3767 (RenderScene_before), 4266 (RenderEffects.exit), 4295 (PostFx.exit), 7737 (DrawFinalEffects_pf) | NO | NO |
| `effectsForceAlphaBlock()` + ONE/ONE→5/6 substitution + `producerOwnsAdditive` gate | 3973-4098 (gate 4079-4095) | 4257, 4262, 4292, 4383, 4393, 5071 | NO | NO |
| `CCoronas__Render_blendprobe_hook` (entry shield) | 4451-4734 | `InjectHook(0x6FAEC0, …, PATCH_JUMP)` + 20-byte trampoline, verified 8328-8387 | NO (only the `0x6FB17C` nop/patch, 226-240) | NO (only `coronaZtest` at 248) |
| `CClouds__Render_hygiene` (alpha block, PS/VS null, stage-0 save/restore, `g_inCloudsRender`) | 5060-5310+ | `InterceptCall` 0x53DCA2 + 0x53DFA0 at 7941-7942 | NO | NO (only `disableClouds`, 1637) |
| `CCoronas__RenderReflections_hook` / `__RenderSunReflection_hook` | 4378-4396 | 8317-8319 | NO | NO |

Notes that matter for the verdicts:

* The point-light **wrapper** is the only addition with a real aap ancestor.
  Ours differs from aap by exactly two things: the anchor log (3221-3226) and
  `coronaDrawRawShield("CSprite_70E780")` at 3239. The colour math is
  line-for-line identical.
* `pointLights` in our tree is bound from the `lea` immediate at `0x7001FF`
  (2849), not `0xC3F0E0` as in aap (658)/junior (710). Equivalent unless the
  immediate is not the array base (the one-shot log at 3224 prints the value).
* The flush hook is installed on **both** inner draw calls of `0x70CF20`; the
  `0x734EA0` stub also has direct callers (`0x701FE9`, `0x7058BC`, `0x726284`,
  `0x7273FF` — postfx/Im2D/particles) that **do not** go through our hook and
  do not feed the sprite batch (`docs/plans/2026-10-24-corona-blackquad-diagnosis.md` §1.1).
* `CClouds__Render_hygiene` is the only addition that saves/restores the
  **stage-0 texture raster** (5123-5132, 5146-5152, and the guarded path) —
  vanilla CClouds leaves its raster bound.

## 3. Judgement: REQUIRED vs REGRESSION RISK

### 3.1 Vanilla values (quoted)

| Source | Quote |
|---|---|
| `2026-10-26-cj-ring-investigation.md:210` | `CPointLights` "is **additive** in vanilla: `rwBLENDONE` / `rwBLENDONE`, `ZWRITE=FALSE`, `ZTEST=TRUE`, `VERTEXALPHA=TRUE`, texture `gpCloudTex[0]`." |
| `2026-10-24-corona-blackquad-diagnosis.md:71-73` | `CCoronas::Render` `0x6FAEC0` entry contract `0x6FAEF8–0x6FAF25` = `rwZWRITEENABLE=0`, `rwVERTEXALPHAENABLE=1`, `rwSRCBLEND=rwBLENDONE`, `rwDESTBLEND=rwBLENDONE`, `rwZTESTENABLE=1`. |
| `2026-10-24-corona-blackquad-diagnosis.md:76-80` | `CPointLights::Render` `0x7002D0` entry `0x7002E7–0x700346` = `FOGENABLE=0`, `ZWRITE=0`, `ZTEST=1`, `VERTEXALPHA=1`, `SRC=ONE`, `DST=ONE`, "texture from `[0xC6AA78]` (the shared 2D-sprite texture global written by CClouds at `0x712FA1/0x7138F9/0x7140CD`)"; "tail is an unconditional `jmp 0x70CF20` at `0x700BCD`". |
| Leaked SA source (`PointLights.cpp`, archive `E:/dev/dave/releases/gtasa_src-main.zip`) `353-391`, `613-618`, `513-523`, `579-588` | the same contract, and the restore block is **commented out** — `// RwRenderStateSet(rwRENDERSTATESRCBLEND, srcBlend); … // VERTEXALPHA=FALSE; // ZWRITE=TRUE`. Vanilla therefore *leaks* `ONE/ONE` out of `RenderFogEffect`. The fog-cone sprites (directional branch) and the glow dots both call `CSprite::RenderBufferedOneXLUSprite_Rotate_Aspect`; the tail `FlushSpriteBuffer()` is the only raster. |
| `2026-10-26-shadow-subsystem-audit.md:64` | `CShadows::RenderStoredShadows` exit = "VERTEXALPHA=0, ZWRITE=1 only (`0x708687`, `0x708693`); **no** texture/ZTEST/FOG/ref/blend-factor restore", SRC/DST "via `0x707460` (5/6 default)". |
| `2026-10-26-shadow-subsystem-audit.md:66-67` | `0x7058BC` draw runs with **"VERTEXALPHA=0/blend OFF"**, texture NULL, and restores only `ZTEST=1`/`VERTEXALPHA=1` (texture never); `0x705660` runs `SRC=INVDESTCOLOR(10)/DST=ZERO(1)` and leaves "texture/blend-enable/ZWRITE not restored". `:110-112`: canonical shadow state = blend ON, 5/6, alpha test ON, `GREATEREQUAL@0`. |

So the game itself does leave the device on `blend=0` (REPLACE) and on `5/6`
**after** the effects-phase shadow passes, and both producers assume someone
will set `ONE/ONE` again. That is exactly the field evidence
`[CoronaDraw] pre-force dev blend=0 src=5 dst=6 SpriteFlush_70CF20`
(2 occurrences) / `blend=1 src=5 dst=6` (1) in
`docs/plans/2026-10-26-log-*.md`.

### 3.2 Verdict per addition

| Addition | Verdict | Reason |
|---|---|---|
| Flush-boundary `ONE/ONE` force (3294-3357) | **REQUIRED** | A genuine vanilla REPLACE leak reaches the shared flush (shadow exit + `CShadows` 5/6 + `CCoronas`/`CPointLights` never restoring). Without it the batch rasterises with `blend=0` → texture RGB written directly = black surround + coloured core, i.e. the reported symptom. Requires the capture/close pair, which is correct in principle. |
| `spriteFlushShieldCapture/Close` (3049-3201) | **REQUIRED, but implementation risky** | Needed so the global pair override is bounded to the batch. Risk: the close replays **three different snapshots** (L1 `s.rwSrcBlend`, L2 `s.drvSrcBlend`, L3 `s.srcBlend`, 3136-3154) — a mixed triple can *re-create* the cache↔device split the file blames. The pre-seed is sane (5/6, ref 2). |
| `effectsForceAlphaBlock` ONE/ONE gate `producerOwnsAdditive` (4079-4095) | **REQUIRED** | Prevents the 5/6 substitution from landing inside a live corona/cloud/sprite window. That rewrite is the documented 80 % mechanism of the night-lamp regression (`2026-10-26-regression-attribution.md:55`). The gate is the correct minimal fix; `pipeForceAlphaBlock` itself is cache-authoritative (buildingPipe.cpp:552-596), so leaving `ONE/ONE` in the cache keeps it `ONE/ONE` on the device. |
| `effectForceAlphaBlock` rest of the repair (4036-4097) | **REGRESSION RISK (look)** | `pipeForceAlphaBlock` always runs at RenderEffects.enter/exit and bumps `ALPHAREF 0→1` in the rw cache (buildingPipe.cpp:574-576), forces `ALPHATESTENABLE=TRUE` (:601) and `COLORWRITEENABLE` all (:614). It is a *state rewrite*, not neutral, and it runs immediately before/after `CCoronas::Render`. |
| `CCoronas__Render_blendprobe_hook` entry shield (4451-4602) | **REGRESSION RISK / mostly redundant** | After the ONE/ONE fix the only forced pair is inside `if(broken)` — i.e. only when the device is on `dst=INVSRCALPHA` or blend off, which the flush force also repairs. The hook additionally forces `ZWRITE=TRUE` at exit (4664, 4671, 4678) regardless of the producer's `ZWRITE=FALSE`, and installs a 20-byte RWX trampoline + two SEH fences on the hottest sprite producer. Keep only the `coronaZtest` wiring. |
| `CPointLights__Render_shield_hook` (3439-3455) | **REQUIRED (cheap), but not a shield** | It only re-arms a flag. Because `g_coronaBodyDone = true` is set **before** `orig()` (3452), the **first** non-empty flush inside the body closes the window (3372-3374) — so the window does not actually span the body. All cone/dot raster coverage comes from the flush `force=true` path. The hook is harmless; its comment ("every CPointLights flush is then inside the shield") is wrong. |
| `coronaDrawRawShield("CSprite_70E780")` at 3239 | **REGRESSION RISK (performance + cache churn), DELETE** | `0x70E780` only **appends** 4 vertices; it never rasterises. The 9-state park-and-set (20+ cache transitions) runs once per sprite — `RenderFogEffect` emits up to hundreds per frame — and is immediately superseded by the flush force. aap does the colour math alone; that part should be kept unchanged. |
| Per-draw `ALPHAFUNC=GREATEREQUAL / ALPHAREF=0 / SEPARATEALPHABLENDENABLE=0 / COLORWRITEENABLE=0xF` (3012-3021) | **REGRESSION RISK** | It is applied to **every** sprite batch of **every** producer (`force=true` ignores `g_inCoronasRender`). Under `ONE/ONE` it is harmless; under any residual non-additive draw it *guarantees* the transparent-black border is accepted (`0 >= 0` passes every texel), removing the only mechanism that could hide it. Vanilla's per-producer `ALPHAREF` (e.g. 2, 64, 128) differs. |
| `CoronaWindow_Close` (3465-3530) | **REQUIRED as a flag reset; REGRESSION RISK as a state restore** | Clearing `g_inCoronasRender/g_coronaBodyDone` at frame boundaries is necessary (SP-deferred windows can stall). But it replays `g_coronaPreBlend/Src/Dst/ZWrite` **captured from the device at CCoronas entry** (4510-4514). The field log proves the device can be at `blend=0` on entry, so the "close" can re-install REPLACE at every frame boundary. Restore a canonical 5/6 + `blend=TRUE` instead. |
| Canonical 5/6 re-sync at window close (3375-3411) | **REGRESSION RISK (look) / REQUIRED for prop ghosting** | Vanilla `CCoronas::Render` and `RenderFogEffect` leak `ONE/ONE`; our resync hands the next consumer `5/6 + ZWRITE=TRUE`, which is *not* what the Sept-3 build left. It fixes the ghosted props, but it is a look change on the same frame the cones are drawn — and it fires **mid-body** on the first flush of `RenderFogEffect` (bodyDone armed pre-call), i.e. between cone appends. |
| `CClouds__Render_hygiene` (5060-5310+) | **REQUIRED for PS/VS + alpha block; REGRESSION RISK for the stage-0 save/restore** | Nulling stale PS/VS at cloud entry (5246-5251) and forcing `MODULATE` (5272-5273) protect the *first* draw of the frame. But restoring the **pre-cloud stage-0 raster** on exit (5146-5152) is a divergence from vanilla, which leaves the cloud raster bound; the PC `CPointLights` path takes its texture from the cloud-written global `[0xC6AA78]`, so a restore that lands before/after the wrong pass can leave the sprite batch sampling a non-cloud raster. `cloudRenderFix=0` already exists as a kill switch. |
| `CCoronas__RenderReflections/SunReflection` alpha block | **REGRESSION RISK, low** | These two vanilla functions *do* restore (per the CCoronas exit comment, 4636), so wrapping them with a 5/6 force adds a state write where the game was already correct. |

### 3.3 Direct answer to the two posed questions

* **Is `0x700817` covered?** No, **not by anything of its own.** Our only
  per-sprite hook is `InjectHook(0x700B6B, …)` (8502) — a *call-site* rewrite
  for the glow-dot branch. The fog-cone branch at `0x700817` still calls
  `0x70E780` directly, so it gets neither the colour math nor
  `coronaDrawRawShield`. It is covered **only transitively** by the flush
  `force=true` pair at the two `0x734EA0` call sites. If that force is skipped
  (empty/incorrect `nSpriteBufferIndex`, install MISMATCH, a raster path that
  is not `0x70CF62/0x70CF9B`), the cones are completely unshielded — while the
  dots are at least colour-fixed.
* **Can the entry shield's `ALPHAREF/ALPHAFUNC` normalisation clip the soft
  falloff?** No. Since the 2026-10-26 change the entry shield does **not**
  write them (4527-4536); the per-draw shield writes
  `ALPHAFUNC=GREATEREQUAL, ALPHAREF=0`, and `GREATEREQUAL@0` accepts *every*
  texel including `alpha=0`, so it cannot clip anything. The residual risk is
  the opposite one: it disables the game's alpha cut for the batch, so the
  transparent black border is never discarded (see §4).

## 4. Top 5 remaining suspects (post ONE/ONE fix)

Baseline assumed fixed: shield default `ONE/ONE` (2956-2957), every non-empty
flush force-covered (3318-3357), effects substitution gated by
`producerOwnsAdditive` (4079-4081). All five below survive those changes.

### #1 — Fog-cone sprites at `0x700817` have no cover of their own
Only `0x700B6B` is rewritten (8502). The directional cone/beam branch draws
through `0x700817 → 0x70E780` **unwrapped**, so it gets none of aap's colour
math (`mult`, `+16`, `f=0xFF`) and no `coronaDrawRawShield`. It is covered
*only* by the shared flush force, and the window that should span the body is
closed by the **first** flush inside it (`g_coronaBodyDone = true` at 3452,
close at 3372-3374). One missed/early flush and the cones draw on inherited
state — the exact REPLACE signature in the field log. Confirm with a receipt at
`0x700817` (count + pre-draw `blend/src/dst`); it is the only sprite path with
zero direct instrumentation.

### #2 — Texture raster + `D3DTSS_*` are outside every shield
The flush draws whatever raster and stage ops are live. `CPointLights` takes
its raster from the cloud-written global `[0xC6AA78]`; `CClouds__Render_hygiene`
restores the **pre-cloud** raster on exit (5146-5152) — a divergence from
vanilla, which leaves the cloud raster bound. The sprite flush has **no**
`COLOROP/ALPHAOP/ARG1/ARG2` normalisation, while `CClouds` explicitly forces
`MODULATE` for exactly this failure mode (5272-5273) because an Im2D
`SELECTARG1/SELECTARG2` leak is already a known class in this build. A wrong
raster or a `SELECTARG` pair renders a flat rectangle with a bright core, and
no blend pair can repair it. Log `GetTexture(0)` + `D3DTSS_COLOROP/ALPHAOP`
inside the flush hook once per second to bisect.

### #3 — `ALPHAREF=0 / GREATEREQUAL` + `COLORWRITEENABLE=0xF` applied to every batch
`coronaDrawRawShield(force=true)` writes the alpha-test pair, `SEPARATEALPHA=0`
and colour-write all for **every** producer's sprite batch (3012-3021), not just
coronas. It cannot clip the falloff (0 accepts all), but it removes the game's
own alpha cut, so the transparent black border is always accepted. Meanwhile
`pipeForceAlphaBlock` bumps the rw `ALPHAREF 0→1` (buildingPipe.cpp:574-576) and
forces `D3DRS_ALPHATESTENABLE=TRUE` (601) at every `RenderEffects` boundary, so
the cache and the per-draw shield disagree each frame. Under perfect `ONE/ONE`
this is invisible; the moment one draw misses the force, the border is painted.

### #4 — `CoronaWindow_Close` / `spriteFlushShieldClose` replay a *captured* broken state
`g_coronaPreBlend/Src/Dst/ZWrite` are read from the **device at CCoronas entry**
(4510-4514). The field log proves the device can be at `blend=0, src=5, dst=6`
on entry; the close then re-installs REPLACE at every frame boundary
(3484-3527). `spriteFlushShieldClose` replays three *different* snapshots (L1
3136-3142, L2 3143, L3 3144) which can deliberately resurrect a
cache↔device split. This is the mechanism that can put the device back on
`blend=0` between the producer body and the deferred raster. Fix: never replay a
captured REPLACE — pin the close to `blend=TRUE, 5/6, ZWRITE=TRUE`.

### #5 — PS/VS left bound at the sprite raster
Nothing in the corona/flush shields touches shaders; `CClouds__Render_hygiene`
nulls them only at cloud **entry** (5246-5251). With SilentPatch deferring the
batch, the flush can execute straight after a PBR building/vehicle draw or an
Im2D postfx pass that left a shader armed. Also unverified: the Im2D/Im3D path
inside the `0x734EA0` stub re-applies ZTEST/ZWRITE/VERTEX-ALPHA from its own
flags, so our force may not be the last writer of `D3DRS_ALPHABLENDENABLE`.
Cheap test: null PS/VS inside the flush force (one line) and log
`GetPixelShader()` once per second at the same point.

Honourable mentions (not top 5): deployed `coronaZtest=1` (aap default is `-1`,
"don't touch"); `CClouds__Render_hygiene`'s `ALPHAFUNC=ALWAYS` entry (5253-5265);
shadow passes that run *after* the corona body and leave `blend=OFF`
(`2026-10-26-shadow-subsystem-audit.md:64-67`).

**Diagnostic gate:** a post-fix log must show `[CoronaDraw] pre-force dev
src=2 dst=2` on the flush path. If it still shows `5/6` or `0`, the force is not
reaching that raster and suspect #1/#4 is the reason.

## 5. Keep / gate / delete + INI kill-switches

Suggested keys follow the existing pattern (`cloudRenderFix`,
`stateBaselineEnable`, `coronaZtest`): bool read in `readIniFile` (config
around 6347/6449/6513), emitted in the generated INI (6959-7096), written back
at 7333/7564 and exposed in the debug menu (1638-class helpers).

| # | Item | Verdict | Kill switch |
|---|---|---|---|
| 1 | Flush-boundary `ONE/ONE` force + capture/close (3049-3357) | **KEEP, unconditional** | `spriteFlushShield=0|1` (0 = vanilla inherited state; 1 = force every non-empty batch, default). Do not offer a "force but don't restore" mode. |
| 2 | `producerOwnsAdditive` gate (4079-4095) | **KEEP, unconditional** | none needed — it only *prevents* a write |
| 3 | `effectsForceAlphaBlock` itself at Clouds / Reflections / SunReflection (4383/4393/5071) | **GATE** | `effectsAlphaBlock=0|1` (default 1). These wrapped functions restore their own states; the sweep is a look change where the game was already correct. Keep the RenderEffects.enter/exit sites (they are the real safety net). |
| 4 | `CCoronas__Render_blendprobe_hook` entry shield (4451-4602) | **GATE / trim** | `coronaEntryShield=0|1` (default 0 = only the `coronaZtest` wiring). The forced `ZWRITE=TRUE` at exit (4664-4678) is the part that changes authored state. |
| 5 | `coronaDrawRawShield("CSprite_70E780")` at 3239 | **DELETE** | (aap parity = colour math only). It churns 9 states per sprite for no raster effect. If kept for one A/B build, gate on `coronaDrawShield=0|1`. |
| 6 | Per-draw `ALPHAREF/ALPHAFUNC/SEPARATEALPHA/COLORWRITEENABLE` (3012-3021) | **DELETE from the per-draw path; move into the flush force only** | `coronaAlphaNormalize=0|1` (default 0 once #1 covers the raster) |
| 7 | `CoronaWindow_Close` flag reset | **KEEP** | none |
| 8 | `CoronaWindow_Close` captured-blend replay (3484-3527) | **DELETE / replace with canonical `blend=TRUE, 5/6, ZWRITE=TRUE`** | `coronaWindowReplay=0|1` (default 0) |
| 9 | Canonical 5/6 re-sync at first flush (3375-3411) | **GATE** | `coronaWindowResync=0|1` (default 1 for the prop-ghosting fix; 0 restores the Sept-3 "leave ONE/ONE" look) |
| 10 | `CPointLights__Render_shield_hook` (3439-3455) | **KEEP** (harmless) but **extend** | `pointLightShield=0|1`; extend it to wrap `0x700817` as well, or accept that #1 is flush-only |
| 11 | `CClouds__Render_hygiene` PS/VS null + `MODULATE` + alpha block | **KEEP** | existing `cloudRenderFix` |
| 12 | `CClouds__Render_hygiene` stage-0 raster save/restore (5123-5152) | **GATE** | `cloudTexRestore=0|1` (default 0 = vanilla "leave the cloud raster bound"); it is the only addition that changes what sprite batches sample |
| 13 | `CCoronas__RenderReflections/SunReflection` wrappers (4378-4396) | **GATE** | covered by `effectsAlphaBlock` (#3) |

**Do not touch:** `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect` colour
math (3228-3232) — identical to aap/junior and the only reference-verified
point-light behaviour. Only remove the shield call, keep the log.

**Ordering note for whoever gates this:** `spriteFlushShield` (item 1) must
stay ON for any verdict on items 4-9 to be meaningful; with it OFF the vanilla
REPLACE leak returns and the symptom is expected.

---

### Appendix — receipts to read on the next run

```
[corona_entry_install] 0x6FAEC0 bytes=… OK|MISMATCH
[corona_install] flush draw sites: 0x70CF62 … OK | 0x70CF9B … OK
[pointlights_install] 0x53E21D … OK | 0x53DE6B … OK
[CoronaDraw] pre-force dev blend=… src=… dst=… at SpriteFlush_70CF20/<corona|clouds>
[CloudShield] flush batch=<sprites|clouds> forced ONE/ONE
[FxAlpha] … blend_subst            ← must be 0 inside a producer window
[CoronaResync] window closed …     ← fires mid-body for RenderFogEffect by design
```
If `MISMATCH` appears on either flush site, items #1-#5 are moot: the flush
force never runs and the fix is not installed.
