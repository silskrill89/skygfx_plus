# SA font/text render path — how the mod can break it

**Date:** 2026-11-01
**Type:** READ-ONLY research (no source changes)
**Target:** `E:\games\gtasa_skygfx_plus\gta_sa.exe` — GTA SA v1.0 US
**Method:** PE map (`pefile`) + linear/convergent disassembly (`capstone`), cross-checked against
the mod's own install-time byte checks. All addresses are VA; file offset =
`0x400 + (va - 0x401000)`.

> Working-tree note: `src/core/main.cpp` was being edited while this was researched. Findings
> describe the tree as read; the "stage-0 family gate" described in §5/§7 is an
> **uncommitted** change (`git status: M src/core/main.cpp`). Everything below is verified
> against the exe, not against the comments in the source (several of those comments are
> wrong — see §1.4).

---

## 1. Map of the game's font/text render path

### 1.1 Call chain (verified)

```
frame glue (CGame::Render / 0x53EA00..)
  0x53EAD3  call 0x53E170   RenderEffects          -> CPostEffects::Render 0x7046E0 (postfx)
  0x53EB8C  call 0x57C290   CHud::Draw             -> CFont::PrintString 0x71A700
  0x53EBA2  call 0x58D490   CMessages::Display     -> CFont::PrintString 0x71A700  (queues text)
  0x53EBB1  call 0x71A210   CFont::RenderFontBuffer (thunk: jmp 0x719840)          <-- FONT DRAW
  0x53EBE4  call 0x532260   DrawFinalEffects       (0x53EBE9 call 0x734640 = camera end)
```

`CMessages::Display` (0x58D490) sets the ambient states the font relies on, immediately before
the flush:

| instruction | RwRenderState | value |
|---|---|---|
| 0x58D495/97 | `rwRENDERSTATETEXTUREFILTER` (9) | 1 = linear |
| 0x58D4A2/A4 | `rwRENDERSTATETEXTUREADDRESS` (2) | 3 = clamp |
| 0x58D4AF/B1 | `rwRENDERSTATEVERTEXALPHAENABLE` (12) | 0 |

Then `CFont::RenderFontBuffer` turns blending back on itself (see 1.4).

### 1.2 CFont::RenderFontBuffer (0x719840, file 0x318C40)

```
0x719840  if (gFontWritePtr [0xC716A8] == gFontBuffer [0xC716B0]) return;   // nothing queued
0x719853  ecx = &CSprite2dArray[fontStyle [0xC71ACC]]   (0xC71AD0 + style*4)
0x719862  call 0x727B30          -> RwRenderStateSet(TEXTURERASTER(1), tex->raster)
0x719867  RwRenderStateSet(VERTEXALPHAENABLE(12), TRUE)          <-- the ONLY blend state font sets
0x719876  loop 0x718E50 over the queued text buffer 0xC716B0
            -> 0x718E50 -> 0x728200  append glyph quad
                 verts = 0xC5F958 + i*0x1C  (RwIm2DVertex, 28 B, vertex-indexed)
                 indices = 0xC4B958 + ...
                 counts: 0xC80458 (verts) / 0xC8045C (indices), capacity 0xFFA / 0x5FC
0x719B13  call 0x727B30          -> rebind font raster (last style wins)
0x719B18  if (writePtr <= 0xC716E0) goto 0x719B26
0x719B26  call 0x70CF20          -> flush the SHARED buffered-sprite batch (count 0xC6A158)
0x719B2B  call 0x7273D0          -> flush the FONT batch (counts 0xC80458/0xC8045C)   <-- hooked by mod
0x719B30  gFontWritePtr = gFontBuffer (reset)
```

`0x7273D0` (file 0x3267D0) is the font glyph draw:

```
0x7273D0  if (*(int*)0xC8045C <= 0) return;
0x7273D9  RwRenderStateSet(...)                 (none — no state writes at all)
0x7273FF  call 0x734EA0(3, 0xC5F958, [0xC80458], 0xC4B958, [0xC8045C])   // trilist, indexed
0x727407  [0xC8045C] = 0 ; [0xC80458] = 0
```

`0x734EA0` (file 0x3342A0) is a thunk → `0x403927` → `0x734EA6 jmp [edx+0x34]` (R*'s
encrypted RW function table) → RW's Im2D indexed renderer at `0x7FC170`, which copies the
vertices to a temp buffer and calls `0x7FB810`.

### 1.3 Im2D backend (0x7FB810, file 0x3FAC10)

```
0x7FB810  RwD3D9SetVertexShader(*(void**)0xC980F4)   // the dedicated Im2D VS (created at 0x7FB480)
0x7FB81B  RwD3D9SetFVF(0)
0x7FB822  push 0
0x7FB824  call 0x7F9FF0                              // RwD3D9SetPixelShader(NULL)  <-- hooked by mod
0x7FB82B/37  texture-stage states 0x88 / 0x89        <-- hooked by mod (0x7FB885 / 0x7FB8A6 hook the
0x7FB841  ...                                        stage-state setter, not these two immediates)
```

Net: **font vertices are transformed by the dedicated Im2D vertex shader and shaded by the
fixed-function pixel pipeline (PS = NULL).**

### 1.4 Why the font lane is fragile (the key fact)

`CFont::RenderFontBuffer` sets **exactly one** render state: `VERTEXALPHAENABLE = TRUE`.
It does **not** set:

* `SRCBLEND` / `DESTBLEND` (inherits whatever the previous pass left),
* `ALPHAFUNC` / `ALPHAREF` / `ALPHATESTENABLE` (inherits; `pipeForceAlphaBlock` forces TRUE/ref≥1),
* `ZTEST` / `ZWRITE`,
* the pixel shader (FFP), the vertex shader (Im2D VS), the texture filter (set by CMessages),
* `COLORWRITEENABLE` / `SEPARATEALPHABLENDENABLE` / `BLENDOP`.

Consequence: **any** hook that changes device blend/alpha-test state and does not restore it
before 0x719B2B changes what text looks like. Text is the single most state-sensitive lane in
the frame, and it is drained at one specific call (`0x7273D0`) that the mod already intercepts.

### 1.5 Atlas / texture identity

* `CFont::Initialise` (0x5BA690, file 0x1B9A90) loads `MODELS\FONTS.TXD` (string 0x86A64F)
  and the texture dictionary `fonts` (0x86A65C).
* Two `CSprite2d`s are created (0x5BA6C7 / 0x5BA6DB) from fonts.txd entries:

| slot | global | texture names |
|---|---|---|
| style 0 | `0xC71AD0` | `font2` / `font2m` |
| style 1 | `0xC71AD4` | `font1` / `font1m` |

* Binding goes through `0x727B30` → `RwRenderStateSet(rwRENDERSTATETEXTURERASTER, tex->raster)`
  (state id 1; argument is `CSprite2d*`, whose `[0]` is the `RwTexture*`).
* The atlas is an 8-bit paletted TXD. D3D9 has no paletted textures, so RW uploads it as
  A8R8G8B8: glyph texels are white (or the FFP modulate colour) with real alpha, transparent
  texels are RGB 0 / A 0. Glyph *colour* comes from the vertex diffuse, not the atlas.

### 1.6 Two different 2D buffers share the same memory

| producer | vertex array | index array | count global | draw site |
|---|---|---|---|---|
| buffered glow sprites (corona / pointlight / cloud / `CSprite::RenderBufferedOneXLUSprite`) | `0xC5F958`, stride 0x70 per sprite (4 verts) | `0xC4B958` | `0xC6A158` (sprite count) | `0x70CF62` / `0x70CF9B` |
| **font glyphs** | `0xC5F958`, stride 0x1C per vertex | `0xC4B958` | `0xC80458` / `0xC8045C` | `0x7273FF` |
| CPostEffects quad writer | `0xC5F958` | `0xC4B958` | `0xC4B950` / `0xC4B954` | `0x701FE9` |

They share the arrays but **not** the counts. Each lane flushes its own count; the font lane
flushes the sprite lane first (`0x70CF20` at 0x719B26) so the arrays are free for glyphs.

---

## 2. Which mod hooks touch this path

| # | Mod hook (install site) | Game site | Contact with the font lane | Can it change text? |
|---|---|---|---|---|
| A | `CloudRegionFlush_7273D0_hook` @ **0x719B2B** | call of the font flush `0x7273D0` | **direct** — wraps the glyph draw itself | **YES — primary** |
| B | `SpriteFlushIndexedDraw_hook` @ **0x70CF62 / 0x70CF9B** | the two draws inside `0x70CF20` | indirect — `0x70CF20` is called at 0x719B26 immediately before the glyph draw | yes, via the state it restores (B1) and via `spriteFlushShieldClose`'s REPLACE rewrite (B2) |
| C | `PostFxFlush_701FE9_hook` @ **0x701FE9** | CPostEffects quad writer `0x701780`, called from `CPostEffects::Render 0x7046E0` @ 0x705116 | none directly (runs in postfx, before HUD) | only by blowing out the frame the text sits on, or by leaking state into the HUD phase |
| D | `Im2dSetPixelShader_hook` @ **0x7FB824** | `RwD3D9SetPixelShader(NULL)` inside the Im2D backend | **every** Im2D draw, including text | **YES if the override is non-NULL** |
| E | `Im2DColorModulationHook` / `Im2DAlphaModulationHook` @ **0x7FB885 / 0x7FB8A6** | Im2D texture-stage-state setter | shared dispatch; font sets no stage states | only on an override leak |
| F | `pipeApplyWorldBaseline` (+`effectsForceAlphaBlock`, `CoronaWindow_Close`) | frame boundaries (SceneStart, RenderEffects enter/exit, PostFx exit, DrawFinalEffects) | runs immediately **before** the HUD/text phase | **YES via inherited blend/alpha-test/depth** |
| G | `DepthHook` (device vtable 39, SetDepthStencilSurface) | binds INTZ as depth-stencil | text draws with `ZTEST` on | possible Z rejection, low |
| H | `0x70E780` wrapper (`CSprite::RenderBufferedOneXLUSprite`) → `coronaDrawRawShield` | glow sprites | none | no |

### A — 0x719B2B (the font flush) — this is the one that mattered

The hook's gate and force:

```
batch = *(int*)0xC8045C ;                       // == the font index count
if (batch > 0 && d3d9device && spriteFlushShield)
    spriteFlushShieldCapture();
    coronaDrawRawShield("CloudFlush_7273D0", force = true);   // ← forced block:
        ALPHABLENDENABLE            = TRUE
        SRCBLEND / DESTBLEND        = ONE / ONE           (D3DBLEND_ONE = 2)
        ZWRITEENABLE                = FALSE
        BLENDOP                     = ADD
        ALPHAFUNC / ALPHAREF        = GREATEREQUAL / 0    (spriteStateShield)
        SEPARATEALPHABLENDENABLE    = 0
        COLORWRITEENABLE            = 0xF
    orig();                                                   // 0x7273D0 draws the glyphs
    spriteFlushShieldClose();
```

`batch` is the **font** index count, so the gate "0xC8045C > 0" passes on every non-empty text
draw (HUD, subtitles, menu, mission text, help boxes). Before the stage-0 family gate was added
(uncommitted change, see below), this forced ONE/ONE onto every glyph batch.

### B — 0x70CF62 / 0x70CF9B

`0x70CF20` draws the *sprite* batch (`0xC6A158`), not glyphs, and the hook's gate is that same
count. So it does not additively blend glyphs. Two residual contacts:

* **B1 — inherited blend pair.** `spriteFlushShieldClose()` replays the nine captured states.
  The glyph draw happens ~5 instructions later and inherits `SRCBLEND`/`DESTBLEND` from it
  (fonts never set them, §1.4). A capture that holds `ONE/ONE` (a corona/cloud leftover) is
  replayed verbatim, so text drawn right after a glow flush inherits additive factors.
* **B2 — REPLACE rewrite.** If the captured `ALPHABLENDENABLE` was FALSE, `spriteFlushShieldClose`
  **rewrites** the snapshot to canonical normal-alpha (`TRUE`, `SRCALPHA`/`INVSRCALPHA`, `ADD`)
  before replaying. That is good for text, but it means the pair the glyph draw sees is not the
  pair the game actually had, i.e. text output depends on the preceding flush's capture.
* **B3 — mid-text canonical re-sync.** The `g_coronaBodyDone` branch (main.cpp ~0x3713-0x3745)
  pushes `blend ON + SRCALPHA/INVSRCALPHA + ZWRITE/ZTEST on` when a deferred corona window
  closes. That close can be triggered by the font's own sprite flush at `0x719B26`, i.e. in the
  middle of a text draw.

### C — 0x701FE9

`0x701780` (file 0x300B80, only caller 0x705116 inside `CPostEffects::Render 0x7046E0`) is the
CPostEffects quad writer. It sets `SRCBLEND = SRCALPHA(5)`, `DESTBLEND = ONE(2)` itself and then
draws `0x734EA0(3, 0xC5F958, [0xC4B950], 0xC4B958, [0xC4B954])`. The hook's force replaces that
authored 5/2 pair with 2/2 whenever the gate passes. It runs before the HUD phase, so it cannot
touch glyphs directly; the risk is a whole-frame blow-out plus any state left behind.

### D — 0x7FB824 (Im2D PS)

The hook replaces the game's `push 0; call RwD3D9SetPixelShader` with
`RwD3D9SetPixelShader(overrideIm2dPixelShader)`. For game Im2D draws the override is NULL, which
is exactly vanilla. **Any non-NULL override at the moment a glyph batch flushes binds a
postfx/PBR pixel shader on the Im2D vertex stream** (which carries only position/UV/diffuse) →
wrong texture reads → garbled or flat glyphs. Override users: `postfx.cpp` (grading, radiosity,
normal buffer, PipeChain, motion blur), `postfx_colourfilter.cpp`, `mirrorPass.cpp`,
`pipelinecommon.cpp guardedIm2DRender`, `chars.cpp` SSS. Resets: per-pass `= nil`, `__finally`
/`__except`, and the `effectsForceAlphaBlock` sweep at RenderEffects enter/exit and PostFx exit
(main.cpp ~4767). The sweep is the only thing standing between a faulted pass and the HUD.

### E — 0x7FB885 / 0x7FB8A6

`Im2DColorModulationHook` / `Im2DAlphaModulationHook` replace the *value* of a texture-stage-state
set with `overrideColorMod` / `overrideAlphaMod` when >= 0. Dangerous values that exist in the
tree: `D3DTOP_SELECTARG1` (colour = mask texel, postfx.cpp:4130), `D3DTOP_SELECTARG2`
(ignore texture colour, postfx_colourfilter.cpp:1100), `D3DTOP_MODULATE2X` (colourfilter:874).
The font draw sets no stage states, so these can only bite if a stage-state set happens while an
override is live *and* text is drawn inside that window (not observed today).

### F — state baseline / alpha sweeps

`pipeApplyWorldBaseline` (pipelinecommon.cpp:2127) runs at RenderScene entry, RenderEffects
enter/exit, PostFx exit and DrawFinalEffects. It pins: blend **OFF**, `SRCALPHA/INVSRCALPHA`,
ZWRITE/ZTEST on, ZFUNC LESSEQUAL, ALPHATEST **TRUE**, CULL CW, `COLORWRITEENABLE 0xF`,
`SEPARATEALPHABLENDENABLE` off. Deliberately *not* set: `ALPHAFUNC`/`ALPHAREF`.

`pipeForceAlphaBlock` (buildingPipe.cpp:531) runs at the same boundaries, preserves the cached
blend pair, forces `ALPHATESTENABLE=TRUE`, `BLENDOP ADD`, `COLORWRITEENABLE ALL`, and bumps
`ALPHAREF 0 -> 1`.

So immediately before the HUD/text phase the device is: blend OFF, alpha test ON @ ref 1,
`SRCALPHA/INVSRCALPHA`, ZWRITE on, CULL CW. Fonts re-enable blending themselves; everything else
is inherited.

### G — DepthHook / INTZ

`DepthHook_Install` vtable-patches `IDirect3DDevice9::SetDepthStencilSurface` (index 39) and
binds an INTZ depth-stencil for the frame. Text draws with `ZTEST` inherited (baseline = on,
LFUNC LESSEQUAL) for the glyph batch (0x7273D0 never touches Z). Im2D z = near-screen-z, so it
should always pass; if a hook leaves `ZFUNC` other than LESSEQUAL/ALWAYS or a non-near z, glyphs
can be depth-rejected.

---

## 3. Does SA use a font pixel shader?

**No.** Verified on the 1.0 US exe:

* The font path (`0x719840` → `0x718E50` → `0x728200` → `0x70CF20`/`0x7273D0` → `0x734EA0`) never
  calls `RwD3D9SetPixelShader` (`0x7F9FF0`). The only PS-related instruction is the mod's own
  install site.
* The Im2D backend `0x7FB810` explicitly sets `RwD3D9SetVertexShader(Im2D VS)` and
  `RwD3D9SetPixelShader(NULL)`. The Im2D vertex shader is created at `0x7FB480` from a 0x228-byte
  VS blob (handles `0xC980F0` / `0xC980F4`); the pixel stage is FFP.
* No `D3DTSS_*` font-specific setup exists either: the only font-adjacent texture-stage activity
  is the ambient filter/address set from `CMessages::Display` (§1.1) — FFP defaults
  (`COLOROP=MODULATE`, `ALPHAOP=MODULATE`, `ARG2=TEXTURE`).

**FFP/PS mismatch consequence:** a PS bound over the Im2D VS sees only the Im2D vertex outputs
(screen position, UV, diffuse). A postfx/PBR shader that expects world position/normals/tangents
reads undefined registers → either flat colour, black, or garbage-striped glyph quads. Because
the texture is still the font atlas, the failure mode is usually "text turns into coloured/
blacked-out blocks" rather than "no text". A *VS* mismatch is not possible here: `0x7FB810`
re-binds the Im2D VS every dispatch.

---

## 4. Ranked break mechanisms

### #1 — Font glyph batch forced additive ONE/ONE at 0x719B2B → 0x7273D0 (primary)

* Sites: hook `CloudRegionFlush_7273D0_hook` installed at **0x719B2B** (off 0x318F2B); forced draw
  at **0x7273FF** (off 0x3267FF) inside **0x7273D0** (off 0x3267D0); called from
  `CFont::RenderFontBuffer` **0x719B26/0x719B2B** (off 0x318F26/0x318F2B).
* Mechanism: `coronaDrawRawShield(force=true)` sets `ALPHABLENDENABLE TRUE`,
  `SRCBLEND=DESTBLEND=ONE`, `ZWRITE FALSE`, `ALPHAFUNC GREATEREQUAL`, `ALPHAREF 0`.
  With `SRCBLEND=ONE` the FFP source term is `vertexDiffuse × textureRGB` **without** the
  texture alpha, so glyph coverage is no longer alpha-blended:
  * antialiased glyph edges lose their coverage → hard, bold, aliased text;
  * text is *added* to the framebuffer → unreadable/washed over bright surfaces, glowing
    at night, and any dark text (shadows, outlines, black HUD text) adds 0 and disappears;
  * `ALPHAREF 0 + GREATEREQUAL` is always-true, so any alpha-test cutout on glyphs is disabled;
  * if a style atlas carries non-black RGB under its transparent texels (the `font*m` masked
    variants are the candidates), the whole glyph cell rasterises as a solid rectangle.
* Gate that should prevent it: the uncommitted `flushBatchIdentity()` check — the force only
  runs when stage-0 matches one of `0xC3E000`, `0xC3E008`, `0xC6AA74`, `0xC6AA78`.
  The font rebinds its atlas at 0x719B13, so the L1 (`RwRenderStateGet(TEXTURERASTER)`) check
  correctly reports "not family".
* **Residual hole in that gate:** `flushBatchIdentity()` ORs the L1 verdict with the *raw
  device* stage-0 (`d3d9device->GetTexture(0)`). Device stage-0 is only updated when a draw
  flushes RW's pending state, so it can still hold the *previous* producer's raster. If the
  last thing drawn before a text pointer was a corona/cloud glow (e.g. a message/mission text
  emitted during the effects phase, or a frame where the HUD sprite draws are skipped),
  a stale device texture equal to a family glow raster yields `family=1` and the force runs on
  **glyphs**. Recommendation: require the L1 cache value to match (it is the value the draw
  will actually use); treat the device only as corroboration, never as an OR.
* Bisect levers: `spriteFlushShield=0` (disables the whole force), `coronaBlendShield=0`
  (blend/ZWRITE only), `spriteStateShield=0` (alpha func/ref/CWR only).

### #2 — Family-state rewrite by `spriteFlushShieldClose` immediately before the glyph draw

* Sites: `SpriteFlushIndexedDraw_hook` at **0x70CF62 / 0x70CF9B** (off 0x30C362 / 0x30C39B) —
  reached from the font function at **0x719B26** (off 0x318F26) — followed by the glyph draw
  at 0x719B2B, ~5 bytes later.
* Mechanism: the glyph draw inherits `SRCBLEND`/`DESTBLEND` (fonts never set them). The close
  path:
  * replays the captured nine-state block verbatim, so an additive pair captured from a
    preceding glow flush is handed straight to the glyph batch (`blend=TRUE, ONE/ONE`) →
    same visual failure as #1 but without any forced-draw log line;
  * rewrites a captured **REPLACE** (`ALPHABLENDENABLE=FALSE`) to canonical normal-alpha — so
    text output depends on whether the previous flush happened to capture blend-off;
  * the `g_coronaBodyDone` branch also pushes `blend ON 5/6`, `ZWRITE/ZTEST on` at window close,
    which can fire between 0x719B26 and 0x719B2B (the font's own sprite flush can be the flush
    that closes a SilentPatch-deferred corona window).
* This is not a forced corruption, but it means the text lane's blend factors are decided by a
  hook that has no knowledge of the font lane. A deterministic fix is for the font hook (or the
  capture/close pair) to pin `SRCALPHA/INVSRCALPHA` (or restore the *font's* contract) around
  every glyph draw instead of replaying whatever the sprite flush captured.

### #3 — `Im2dSetPixelShader_hook` binds a stray pixel shader on the FFP font batch

* Site: **0x7FB824** (off 0x3FAC24), `Im2dSetPixelShader_hook`; dispatch call chain
  `0x7273D0 → 0x734EA0 → 0x7FC170 → 0x7FB810 → 0x7FB824`.
* Mechanism: the hook unconditionally calls `RwD3D9SetPixelShader(overrideIm2dPixelShader)`.
  If the override is non-NULL when a glyph batch flushes, the FFP pixel stage is replaced by a
  postfx/PBR shader over an Im2D vertex stream (position/UV/diffuse only), while the Im2D **VS**
  stays bound. Glyph output becomes flat/black/striped. It cannot happen while every pass resets
  the override, so the realistic trigger is a faulted postfx pass whose `= nil` was skipped (the
  mod's `effectsForceAlphaBlock` sweep sweeps this at RenderEffects enter/exit and PostFx exit,
  but the sweep itself is skipped when `g_inCoronasRender || g_inCloudsRender ||
  s_spriteForceValid`, and it is disabled entirely by `effectsAlphaBlock=0`).
* Related, lower probability: `Im2DColorModulationHook` / `Im2DAlphaModulationHook`
  at **0x7FB885 / 0x7FB8A6** can substitute `D3DTOP_SELECTARG1/SELECTARG2/MODULATE2X` into a
  stage-state set while an override is live; that would make text ignore its atlas (solid
  colour quads) if such a set ever occurs inside a text draw.

### Secondary mechanisms (worth checking if the top three are excluded)

* **S1 — baseline blend-off window.** `pipeApplyWorldBaseline` leaves `ALPHABLENDENABLE=FALSE`
  at PostFx exit. Fonts re-enable it at 0x719867, so this is only a risk for text drawn by a
  path that does not go through `CFont::RenderFontBuffer` (e.g. `CSprite2d`-drawn text such as
  the radar blips or the odd menu label).
* **S2 — inherited ALPHAFUNC/ALPHAREF.** Fonts never set them. `pipeForceAlphaBlock` clamps
  ref 0→1; `CoronaWindow_Close` restores the *pre-corona* func/ref (a captured value, potentially
  a high ref with GREATEREQUAL) at frame boundaries. A high ref erodes glyph antialiasing /
  thin strokes.
* **S3 — COLORWRITEENABLE / SEPARATEALPHABLENDENABLE leak.** If a pass leaves
  `COLORWRITEENABLE` without RGB, text is invisible until the next baseline. The baseline and
  the corona force both pin `0xF`, so this requires both to be off or skipped.
* **S4 — INTZ depth-stencil.** Text Z-tests against the substituted INTZ surface; only a problem
  if `ZFUNC`/z is wrong (§2.G).
* **S5 — Texture-stage override (`overrideAlphaMod`).** Same class as #3's related item.

---

## 5. Verification recipe (no code changes needed)

1. **Is the font lane being forced?** Run with the default config at the main menu / a subtitle.
   Look for:
   * `[cloud_flush_id] 0x7273D0 batch=<n> s0Raster=<font atlas> s0DevTex=<x> family=<0|1> force=<0|1>`
     — `batch > 0` at the menu **is** the font lane; `family=1` on a font atlas is a gate failure.
   * `[cloud_flush] gate shielded=<n> skipped=<n>` — `shielded` must stay 0 during pure HUD/menu
     text; any non-zero counter is text being additively forced.
   * `[probe] cloud_flush 0x7273D0 force=1 pre blend=.. src=.. dst=..` — printed only on force.
2. **Is a stray PS on the Im2D path?** `diagLevel>=1` enables the `[corona_blend]` receipt in
   `Im2dSetPixelShader_hook` (postfx.cpp:473) which prints `dev blend/src/dst`, `rw raster`,
   `dev s0` and `spriteBatch` at rasterisation time. `spriteBatch=0` + a non-null s0 raster is the
   font batch.
3. **Bisect:** `spriteFlushShield=0` → if text is fixed, mechanism #1/#2. `stateBaselineEnable=0`
   → if text changes, mechanism S1/S2. `effectsAlphaBlock=0` (only as a test) → if text breaks,
   the PS-sweep is the only thing protecting it (mechanism #3).
4. **Confirm the lane identity** by breaking at 0x719B2B in a debugger and reading
   `[0xC8045C] > 0`, `[0xC71ACC]` (style), and the L1 texture raster
   (`RwRenderStateGet(1)`), which must equal `*(RwTexture**)0xC71AD0/0xD4` → `->raster`.

---

## 6. Corrections to existing in-repo comments (evidence-backed)

| Comment in `src/core/main.cpp` | Actual |
|---|---|
| "0x7273D0 — cloud-region companion flush … right after the cloud-region 0x70CF20 flush" | 0x7273D0 has **one** caller, 0x719B2B, inside `CFont::RenderFontBuffer` (0x719840). It is the **font glyph flush**; its counters 0xC80458/0xC8045C are written only by the glyph appender 0x728200 (callers 0x718C2A/0x718D05/0x718D7E/0x718E39 — all in the 0x718xxx font module). It draws with the font atlas as stage-0. |
| "the shared TempBuffer is ALSO the game's 2D text/HUD/font lane" | Correct, but precisely: the *arrays* (`0xC5F958`/`0xC4B958`) are shared; the font has its **own counters** and its **own draw site**. 0x70CF20 never draws glyphs. |
| "0x701FE9 … the postfx TempBuffer writer" | 0x701780 is the CPostEffects quad writer (caller 0x705116 in `CPostEffects::Render`); it authors `SRCBLEND=SRCALPHA, DESTBLEND=ONE` itself before drawing. |

---

## 7. One-line answers

* **Font draw path:** `CFont::PrintString 0x71A700` → queue → `CFont::RenderFontBuffer 0x719840`
  → bind `fonts.txd` atlas via `0x727B30` → glyph quads via `0x728200` into `0xC5F958`/`0xC4B958`
  (counts `0xC80458`/`0xC8045C`) → sprite flush `0x70CF20` → **font flush `0x7273D0`** →
  `0x734EA0` → RW Im2D (`0x7FC170` → `0x7FB810`) → DrawPrimitive.
* **Font pixel shader:** none. FFP pixel + dedicated Im2D VS (`0x7FB480`, handle `0xC980F4`).
* **Biggest single risk:** the 0x719B2B hook forcing ONE/ONE on the glyph batch.
* **Second:** `spriteFlushShieldClose` deciding the blend pair the glyphs inherit.
* **Third:** a stale `overrideIm2dPixelShader` binding a postfx PS on the FFP Im2D stream.
