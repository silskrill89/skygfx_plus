# Clouds FFP Audit — `CClouds::Render` State Contract and SM3.0 Replacement Spec

**Status:** investigation complete, READ-ONLY (no source edited)
**Date:** 2026-10-25
**Scope:** Audit of the vanilla cloud rendering path and specification for an SM3.0 replacement.
**Binary:** `E:\games\gtasa_skygfx_plus\gta_sa.exe` (Hoodlum 1.0 US, image base `0x400000`).
All addresses below are **verified by disassembly of that file** (PE `.text` VA `0x1000` / raw `0x400`,
so `raw = VA - 0x400C00`). Every function boundary was confirmed by a caller scan, not by a doc.

---

## 1. Current State Contract of `CClouds::Render`

### 1.1 Entry and identity

| Item | Value |
|---|---|
| Function | `CClouds::Render`, `0x713950` – `0x714640` (single `ret 0` at `0x714640`) |
| Callers (E8 scan, exhaustive) | `0x53DCA2`, `0x53DFA0` — both intercepted (`src/core/main.cpp:7151-7152`) |
| State-set mechanism | `RwD3D9SetRenderState`-equivalent reached through `[[0xC97B24]+0x20]`, i.e. **RW driver cache (L2) + device (L3)** — **never** `RwRenderStateSet` (L1) |
| Game flag it clears | `mov byte [0xC3E030],0` at `0x71396D` |

The L1 bypass is the structural fact that matters: CClouds **does not touch the rw render-state cache**,
so `RwRenderStateGet` reports the pre-cloud value while the device holds ONE/ONE. This is the same
cache↔device split diagnosed for the corona batch (`2026-10-24-corona-blackquad-diagnosis.md` §3 Q5,
root cause 6). The existing hook only survives it because it calls `_rwD3D9RenderStateFlushCache()`
before forcing (`main.cpp:4499`) and restores L1 + L2 + raw L3.

### 1.2 Exact state writes, in order

State ids are `RwRenderState` (`external/d3d9/rwplcore.h`, parsed): 1=TEXTURERASTER, 6=ZTESTENABLE,
8=ZWRITEENABLE, 10=SRCBLEND, 11=DESTBLEND, 12=VERTEXALPHAENABLE (= `D3DRS_ALPHABLENDENABLE` twin).
Blend values are the D3DBLEND numerics that RW passes through (2=ONE, 5=SRCALPHA, 6=INVSRCALPHA,
7=DESTCOLOR).

| Address | Write | Meaning |
|---|---|---|
| `0x713969/0x713974` | state 8 = 0 | **ZWRITE = FALSE** |
| `0x713977/0x713981` | state 6 = 0 | **ZTEST = FALSE** (RW maps to `D3DRS_ZENABLE`) |
| `0x71398A/0x71398E` | state 12 = 1 | **ALPHABLEND ENABLE = TRUE** |
| `0x713991/0x71399A` | state 10 = 2 | **SRCBLEND = ONE** |
| `0x71399D/0x7139A7` | state 11 = 2 | **DESTBLEND = ONE** |
| `0x713ADC/0x713AEE` | state 1 = 0 | texture raster = **NULL** (sun disc pass) |
| `0x713AF7/0x713AFB` | state 10 = 5 | SRCBLEND = **SRCALPHA** (sun disc + glow) |
| `0x713B03/0x713B07` | state 11 = 2 | DESTBLEND = ONE |
| `0x713B79-0x713B8A` | state 1 = `[0xC6AA74]->raster` | **sun corona texture** |
| `0x713B93/0x713B97` | state 10 = 5 | SRCALPHA again (sun glow quad) |
| `0x713C51/0x713C5A` | state 10 = 7 | **SRCBLEND = DESTCOLOR** (multiplicative pass) |
| `0x713C73/0x713C77` | state 8 = 0 | ZWRITE = FALSE |
| `0x713C7A-0x713C8A` | state 1 = `[0xC3E008]->raster` | multiplicative pass texture |
| `0x713D0D/0x713D11` | state 10 = 2 | **back to ONE** |
| `0x713D1A/0x713D1E` | state 11 = 2 | **ONE** (cloud batches live here) |
| `0x713DC1` (read) | raster `[0xC3E000]` | cloud texture A → flush #1 |
| `0x7140CB-0x7140DF` | state 1 = `[0xC6AA78]->raster` | **shared 2D-sprite texture** → flush #2 |
| `0x714246-0x714256` | state 1 = `[0xC3E000]->raster` | cloud texture A → flush #3 |
| `0x714392/0x714396` | state 10 = 5 | **SRCALPHA** (lens-flare block) |
| `0x71439F/0x7143A3` | state 11 = 6 | **INVSRCALPHA** |

It sets **nothing else**: no cull mode, no fog enable, no alpha test enable/func/ref, no texture-stage
state, no sampler state, no shader, no `BLENDOP`, no `COLORWRITEENABLE`. Verified by scanning all 931
decoded instructions in the range: the only state calls are the sites in the table (plus the
`0x7EF450/0x7EF550` Im2D lens-flare draw, which sets its own states inside RW).

### 1.3 Draw sequence / sub-passes

```
0x7139AD  call 0x70CFB0                 ; stub jmp 0x40A196 (camera/frame-buffer query)
0x713AEE..0x713B74   NULL raster + SRCALPHA/ONE  -> 0x70D000  sun DISC
0x713B8D..0x713C4C   sun texture + SRCALPHA/ONE -> 0x70D000  sun CORONA/GLOW
0x713C51..0x713D02   DESTCOLOR/ONE + ZWRITE off -> 0x70D000  multiplicative pass
0x713D07..          ONE/ONE (cloud window)
0x713F0F  append 0x70E4A0 ; 0x714011 0x70D000 ; 0x714019  FLUSH #1
0x7140DF  bind [0xC6AA78] ; 0x714216 append 0x70EAB0 ; 0x71422A  FLUSH #2
0x714246  bind [0xC3E000] ; 0x714262..0x714381 loop esi<6 append 0x70E4A0 ;
0x714387  FLUSH #3
0x714396/0x7143A3   SRCALPHA / INVSRCALPHA
0x714410..0x714631  4-vertex Im2D quad @0xC4D958 -> 0x7EF450/0x7EF550  LENS FLARE
0x714631  jmp 0x7EF520  (tail)
```

`0x70D000` is the **immediate** (non-buffered) `CSprite::RenderOneXLUSprite`; it is not the shared
batch and the flush hook never sees it.

### 1.4 What the hook saves / restores (`CClouds__Render_hygiene`, `main.cpp:4363-4622`)

Saved and restored: `TEXTUREFILTER` (rw + sampler 0 MIN/MAG/MIP raw), `ALPHATESTFUNCTION` +
`ALPHATESTFUNCTIONREF` (rw + `ALPHAFUNC`/`ALPHAREF` raw), `FOGENABLE` (rw + raw), `ZTESTENABLE` (rw) +
`ZENABLE`/`ZFUNC` (raw), `ZWRITEENABLE` (rw+L2+raw), `VERTEXALPHAENABLE`/`ALPHABLENDENABLE`
(rw+L2+raw), `SRCBLEND`, `DESTBLEND` (rw+L2+raw), stage-0 `COLOROP`/`ALPHAOP` (raw), PS/VS (rw+raw),
and it forces `s5` texture to NULL on exit (`main.cpp:4603`).

### 1.5 What still leaks

| Leak | Evidence | Impact |
|---|---|---|
| **Texture raster (state 1)** never saved or restored | hook has no `rwRENDERSTATETEXTURERASTER` / `SetTexture(0,…)` capture (`main.cpp:4431-4495`, `4559-4621`); CClouds leaves `[0xC3E000]` or `[0xC6AA78]` bound (`0x7140DF`, `0x714256`) | The **first world draw** samples a cloud/sun raster, with the hook's restored stage-0 ops pointing at it. World-side bleed, not cloud-side |
| **`D3DRS_ALPHATESTENABLE`** never saved nor forced | entry only forces `ALPHAFUNC`/`ALPHAREF` (`main.cpp:4518`, `4525-4526`) | Harmless while func=ALWAYS/ref=0; becomes lethal the moment any path leaves GREATEREQUAL@high with the test on — but the shield re-writes GREATEREQUAL@**0** at every flush (`main.cpp:2592-2595`) so alpha==0 still passes |
| **`D3DRS_BLENDOP`** captured by nobody, forced to ADD by the shield (`main.cpp:2603-2604`) | no `BLENDOP` save in the hook | Entry BLENDOP is never put back; ADD is canonical, so low risk |
| **`COLORWRITEENABLE` / `SEPARATEALPHABLENDENABLE`** forced to 0xF / 0 at every sprite flush and never restored | `main.cpp:2596-2601` | Any later pass that wanted a channel mask or separate alpha loses it inside the same frame |
| **`cloudRenderFix=0` path restores nothing** | `main.cpp:4418-4429` wraps orig with `g_inCloudsRender` only | Vanilla CClouds leaves ONE/ONE + ZWRITE off **for the rest of the frame** — this is the only path that actually leaks additive factors |
| Cull mode / stage 1+ ops / samplers 1..15 never touched | by design | Out of scope; the cloud draws use stage 0 only |
| **The sky dome is not covered at all** | `CClouds::RenderSkyPolys` = `0x714650`, called from `0x53D81C` (E8 scan) — **before** `0x713950` at `0x53DCA2` | The dome is drawn *before* `effectsForceAlphaBlock("Clouds")` (`main.cpp:4373`) can repair anything |

---

## 2. Geometry + Texture Inventory

### 2.1 Three different geometry classes

| Class | Producer | Draw entry | Vertex source |
|---|---|---|---|
| Buffered cloud sprites (3 batches) | `0x70E4A0` (`CSprite::RenderBufferedOneXLUSprite`, axis-aligned), `0x70EAB0` (rotate variant) | **`0x70CF20`** flush, draw calls `0x70CF62` / `0x70CF9B` → `0x734EA0` (`RwIm2DRenderIndexedPrimitive`) | shared RW TempBuffer `0xC5F958` (verts) / `0xC4B958` (indices) |
| Sun disc / sun corona / multiplicative pass | `0x70D000` (`CSprite::RenderOneXLUSprite`, immediate) | direct | RW Im2D immediate vertex, stream 0 |
| Lens flare | 4 verts hand-built at `0xC4D958` | `0x7EF450` → `0x7EF550` | Im2D |
| Sky dome | `CClouds::RenderSkyPolys` `0x714650` | `0x53D81C` | RW immediate (own path) |

### 2.2 Shared batch layout (verified)

```
0xC5F958  vertex array, RW Im2D vertex, stride 0x1C
0xC4B958  index array, 16-bit, 6 indices per quad
0xC6A158  dword sprite count (quads), NOT uint16
0xC6A15C  dword "corona-style" flag: 1 set by 0x70F440, 0 set by 0x70E4A0/0x70EAB0
          -> only toggles ZTEST in the flush (0x70CF2D..0x70CF40 / 0x70CF67..0x70CF70)
```

* Quad = 4 × 0x1C = **0x70** bytes (`main.cpp:2635-2637`; appender `0x70E4A7` writes the flag, then 4 verts).
* Flush count math: `nverts = count*4` (`0x70CF53`), `nindices = count*6` (`0x70CF48-0x70CF4B`),
  `primType = 3` (triangle list, pushed at `0x70CF60`).
* **Cap = 0x180 = 384 sprites**, verified at `0x70E480`:
  `cmp dword [0xC6A158],0x180 / jl +7 / jmp 0x70CF20` — i.e. **if the batch is full, flush it first,
  then append**. The same guard exists at the sibling overflow tails (`0x70E48C`, `0x70E76F`,
  `0x70EA9F`, `0x70EDD1`).
* FVF: RW's Im2D vertex is `{float x,y,z,w; DWORD diffuse; float u,v}` = **`D3DFVF_XYZRHW(0x004) |
  D3DFVF_DIFFUSE(0x040) | D3DFVF_TEX1(0x100)` = `0x144`**. Screen-space, clip-space w=1, colour already
  baked by the CPU producer, UV already assigned.
* Index count is 16-bit → theoretical ceiling 10922 quads, but the appender caps at 384.

### 2.3 Textures

| Global | Role | Bound at |
|---|---|---|
| `0xC3E000` | cloud texture A (batches #1 and #3) | `0x713DC1`, `0x714246` |
| `0xC6AA78` | **shared 2D sprite texture** — the same global `CPointLights::Render` reads (`0x7002E7-0x700346`, per corona doc §1.2) | `0x7140DF` |
| `0xC3E008` | multiplicative pass texture | `0x713C8A` |
| `0xC6AA74` | sun corona texture | `0x713B79` |

Each global is a `RwTexture*`; the code dereferences `[[global]]` to get the raster (`0x713B7F`,
`0x713C7F`, `0x7140D1`, `0x71424B`) and pushes it as **rw state 1** — it does **not** call
`RwTextureSetRaster`/`SetTexture` directly, so the rw texture-binding cache is never told.

The three "cloud" textures are therefore **not** a dedicated sky texture: batch #2 is drawn with the
generic particle/glow sprite texture shared with street-lamp glows.

---

## 3. Interleave Map — Shared Batches with Corona / CPointLights

### 3.1 All flush entry points of `0x70CF20` (E8 scan of the whole `.text`, exhaustive)

| Site | Producer |
|---|---|
| `0x6FB9C2` | `RenderReflections` (corona batch flush during the reflection/sky phase) |
| `0x714019` | **CClouds flush #1** |
| `0x71422A` | **CClouds flush #2** |
| `0x714387` | **CClouds flush #3** |
| `0x715EFB` | effects/lens-flare batch |
| `0x719B26` | effects/particle batch |
| tail-jmp `0x6FB621` | `CCoronas::Render` vanilla tail (SilentPatch patches it to `RET`) |
| tail-jmp `0x700BCD` | `CPointLights::Render` unconditional tail |
| `0x70E48C` + siblings | appender overflow ("batch full → flush now") |

### 3.2 Consequences of the shared queue

1. The queue is **one** buffer (`0xC5F958`/`0xC6A158`). Any producer's quads are rasterised by
   whichever consumer flushes next.
2. With SilentPatch active (`mov word [0x6FB621],0xE8C3`, `mov dword [0x70F45A],0` — corona doc §1.3),
   the `CCoronas::Render` tail flush becomes a `RET`. **Corona quads therefore survive the function
   and can be rasterised at `0x714019` — the first cloud flush of the *next* frame.**
3. At `0x714019` the device state is: `ZENABLE=0` (`0x713977`), `ZWRITE=0` (`0x713969`),
   `ALPHABLEND=TRUE`, `ONE/ONE`, plus whatever the flush shield forces
   (`main.cpp:2721-2725`: ONE/ONE while `g_inCloudsRender`, `ALPHAFUNC=GREATEREQUAL@0`,
   `ZWRITE=FALSE`, `COLORWRITEENABLE=0xF`, `BLENDOP=ADD`).
4. **The ZTEST re-enable inside the flush is skipped for such a deferred batch.** `0x70CF2D` reads
   `[0xC6A15C]`; a cloud append sets it to **0** (`0x70E4A7`, `0x70EABE`). So when a deferred corona
   batch is mixed with cloud sprites in the same queue, the flush takes the `je 0x70CF81` path and
   never restores `ZTEST=1`. The deferred corona quads are then drawn **untested and unwritten, in the
   sky, before any world geometry**.
5. Because the clouds are the *first* scene draw, "after the geometry it should be behind" is not
   recoverable: the batch that belongs to frame N−1's world is painted into frame N's background and
   then the world (drawn later, with ZWRITE on) covers it. The corollary is the visible artefact class:
   sky-coloured/ghosted coronas and additive bleed at the horizon when the pool is non-empty.
6. `CPointLights::Render`'s early call at `0x53DE6B` runs **before** `RenderScene` and is un-intercepted;
   its tail jmp `0x700BCD` is the other classic carrier of a stale batch into the sky phase
   (corona doc §3 Q1). It is not wrapped by `g_inCloudsRender` and therefore gets the SRCALPHA/ONE pair,
   not ONE/ONE.

### 3.3 Why the producer-aware shield does not fix this

`g_inCloudsRender` is a *producer* flag, but the flush is a *consumer* boundary. A batch that was
produced outside the window (deferred corona, early CPointLights) and consumed inside it is forced to
**the cloud pair**, because the hook has no record of who produced the queued quads
(`main.cpp:2701-2734`). That is the same "producer ≠ rasteriser" trap documented for the corona shield
(corona doc §3 Q5), now inverted: the cloud window overwrites the corona pair.

---

## 4. Insertion Point for a Replacement Shader

### 4.1 The exact seam already exists

`SpriteFlushIndexedDraw_hook` (`src/core/main.cpp:2661-2782`) is installed on **both** draw calls of the
shared batch (`0x70CF62`, `0x70CF9B`, verified `E8 rel32 → 0x734EA0`; install receipt
`[corona_install] ... orig=00734EA0`). Its signature is exactly the rasteriser's:

```c
void __cdecl hook(int primType, void *verts, int nverts, void *indices, int nindices)
```

So the replacement does **not** need a new exe patch. Bind the shader in the hook, immediately after
the shield block (`main.cpp:2734`) and before `SpriteFlushIndexedDraw_orig(...)` at `main.cpp:2735`.
The `verts`/`indices` arguments are the shared buffers; nothing needs to be read from `0xC5F958`.

### 4.2 State that must be saved / restored at that boundary

| State | Why | How |
|---|---|---|
| Pixel shader | the batch is otherwise FFP | save with `GetPixelShader`; restore with `RwD3D9SetPixelShader(NULL)` **and** raw `SetPixelShader(NULL)` |
| RW PS cache `0x8E244C` (`_rwD3D9LastPixelShaderUsed`) | a raw `SetPixelShader` the cache doesn't see makes the next `RwD3D9SetPixelShader` a no-op | always leave the device at NULL through the RW API (the existing hook does this at `main.cpp:4598-4602`) |
| FVF / vertex shader | none may be assumed | if using FVF `0x144` + FF VS, save with `GetFVF`, restore after the draw |
| sampler 9 + `c49..c52` | noise tile | `pipeBindNoise(9)` is idempotent and re-uploads (`noise.cpp:913-940`); the next consumer re-binds |
| s0 texture | **must not be rebound** — batches #1/#3 use `[0xC3E000]`, batch #2 uses `[0xC6AA78]` | read it as-is |
| texture filter | already handled by the cloud hook | don't touch |

No cull, no depth, no blend change is needed: the shield has already put the device on
`ALPHABLEND=TRUE`, `ONE/ONE`, `ZWRITE=FALSE`, `BLENDOP=ADD`, which is what the shader must assume
(premultiplied additive output).

### 4.3 Vertex declaration / FVF

**None is required.** The batch is already in **screen space** with a fully baked colour and UV:

```
offset 0x00  float x, y        (pixels, XYZRHW)
offset 0x08  float z           (0..1)
offset 0x0C  float w           (1.0)
offset 0x10  DWORD diffuse     (ARGB, CPU-baked cloud colour)
offset 0x14  float u, v        (per-quad atlas UV, 0..1)
stride       0x1C
FVF          D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 = 0x144
```

So the minimal replacement is a **pixel shader only**: `SetFVF(0x144)`, no vertex shader, no
declaration, no matrix upload. D3D9 permits a `ps_3_0` shader over the fixed-function vertex pipeline.
(If a VS is ever wanted, the declaration is `POSITIONT`, `COLOR0`/`D3DDECLUSAGE_COLOR`, `TEXCOORD0`.)

### 4.4 Is the UV usable for a cloud-density lookup?

**Yes, and better than the sprite atlas.** The per-vertex UV is the atlas sub-rect of the current cloud
sprite (`v` at +0x14). Independently, because `x,y` are **screen pixels** (XYZRHW), the shader can
reconstruct the view ray with `float2 ndc = (x/W, y/H)*2-1` and no matrices — the same trick
`HeightFog.hlsl`/`CloudsPS.hlsl` use for their full-screen variants. Both are available to the same
pixel shader, so the deck can be projected on a virtual plane and the sprite UV used only as a mask.

Caveat: the batch is **screen-space**, so a "volumetric" reconstruction is per-pixel from the ray, not
per-vertex. That is exactly what `main_cloudsSky` already does.

### 4.5 Discriminating cloud sprites from foreign quads in the same flush

The queue can hold deferred corona / CPointLights quads (§3.2). A shader bound for the whole
`g_inCloudsRender` window would shade them as clouds. Two options:

1. **Coarse (recommended for v1):** arm a new `g_batchIsClouds` at `0x713D07` (after the sun/moon
   states, before the first append) and clear it after `0x714387` — finer than `g_inCloudsRender`
   and excludes the sun disc, sun corona, multiplicative pass and lens flare.
2. **Exact:** set a 1-dword producer tag in a wrapped appender (`0x70E4A0` / `0x70EAB0` are both
   one call site each inside CClouds, but they are also used by other systems) or split the batch by
   rebuilding two index ranges at flush time. Not worth it for v1.

**Residual risk** that no flag can remove: a SilentPatch-deferred corona batch already sitting in the
queue when CClouds appends its first sprite. It will be shaded with the cloud shader. This is inherent
to the shared queue and is the strongest argument for *also* fixing the interleave (§3.2 item 4).

---

## 5. Why the Clouds Are Currently Wrong

The test report is "faint / flattened / washed". The audit says: **the blend pair is not the cause
any more; the remaining causes are state the vanilla path never sets and content the mod overwrites.**

### 5.1 Direct answer to the two questions asked

**Q: Does any remaining path leave the cloud batch on SRCALPHA/ONE?**
**No.** Verified by disassembly:

* the cloud block is bracketed by `0x713D07` (`SRC=2`) / `0x713D1E` (`DEST=2`) and `0x714396`
  (`SRC=5`) / `0x7143A3` (`DEST=6`); between them the **only** writes are `0x7140DF` / `0x714246`
  (texture raster) and the appends/flushes. There is no `SRCBLEND` or `DESTBLEND` write inside
  `0x713D1E … 0x714387`.
* the two `SRCALPHA/ONE` sites (`0x713AF7`, `0x713B93`) belong to the **immediate** sun disc / corona
  (`0x70D000`), which never reaches `0x70CF20`.
* the shield then forces ONE/ONE for every flush inside `g_inCloudsRender` (`main.cpp:2721-2725`),
  which for the cloud path is a **no-op against vanilla** — it cannot wash them out.

**Q: Can the hook's partial restore leave additive factors behind?**
**Only with `cloudRenderFix=0`.** On that path (`main.cpp:4418-4429`) nothing is saved and nothing is
restored: vanilla CClouds returns with `ONE/ONE` + `ZWRITE=FALSE` + `ZENABLE=FALSE`, which then rides
the world's first draws. With the default `cloudRenderFix=1`, `sSrcRw/sDstRw`, `sSrcDrv/sDstDrv`,
`sSrcDev/sDstDev` were captured **after** `effectsForceAlphaBlock("Clouds")` had already seeded
`SRCALPHA/INVSRCALPHA` (canonical substitution, `main.cpp:3385-3397`), so the restore lands on 5/6 at
all three layers. No additive leak.
The genuine restore gaps are listed in §1.5 (texture raster, `BLENDOP`, `ALPHATESTENABLE`,
`COLORWRITEENABLE`/`SEPARATEALPHABLENDENABLE`), and none of them is a *cloud* tint.

### 5.2 Ranked causes

| # | Cause | Confidence | Evidence | Test |
|---|---|---|---|---|
| **1** | **Stage-0 `COLOROP`/`ALPHAOP` are never forced on entry** — only restored on exit | **High** (hole exists) / **Medium** (that it is today's visual) | CClouds writes **no** texture-stage state anywhere in `0x713950-0x714640` (all 22 state calls are ids ≤ 12); the hook's only stage-op work is the *restore* at `main.cpp:4619-4620`. Whatever the previous frame's `DrawFinalEffects` left (`SELECTARG1`, `COMBINEWITH` constant, premultiply) shades the clouds | log `D3DTSS_COLOROP/ALPHAOP` + arg1/arg2 at the cloud flush; then force `MODULATE(TEXTURE,DIFFUSE)` on entry |
| **2** | **The sky dome is outside the hook and is drawn *before* the repair** | **High** | `CClouds::RenderSkyPolys 0x714650` ← `0x53D81C`; `effectsForceAlphaBlock("Clouds")` runs at `main.cpp:4373`, i.e. inside `0x713950` reached at `0x53DCA2` — *after* the dome | A/B with the previous frame's alpha block repaired at `RenderScene_before` instead |
| **3** | **Cloud vertex colour / brightness is CPU weather data the mod rewrites** | **Medium-high** | cloud colour is computed from weather bytes at `0x713D24-0x713D9E` and `0x714090-0x7140C4`; `Weather_Update()` / `Weather_ApplySunConfig()` overwrite `m_CurrentColours` before `RenderScene` (`main.cpp:4685-4690`) | log the 3 colour bytes passed to the appender + `[0xC3E000]` pointer per batch; run with weather override off |
| **4** | **`MIPFILTER` forced `LINEAR`** (and MIN/MAG) for every cloud draw | **Medium** | `main.cpp:4520-4524`, `4606-4608`; the sampler is force-linear even if vanilla inherited POINT/NONE | A/B: force only `MIPFILTER=NONE`, keep MAG/MIN as found |
| **5** | **ONE/ONE against a bright sky has no contrast control** | Medium | the pass is pure additive (`0x713D11/0x713D1E`); there is no darkening counterpart in the cloud block (the `DESTCOLOR/ONE` pass at `0x713C51` runs *before* it and is not part of the clouds) | raise the noise erosion / lower `c51.w` density and compare, or A/B `DESTCOLOR` premultiply |
| **6** | **Texture raster leak** into the first world draw | High (leak) / Low (cloud symptom) | §1.5 | probe `GetTexture(0)` at `RenderScene_before` |
| **7** | The unwired `shaders/ps/CloudsPS.hlsl` | **Not a cause today** | `resources/cso/CloudsPS.cso` exists, but there is **no** `IDR_` in `resources/resource.h`, **no** entry in `resources/resource.rc`, and **zero** references in `src/` — it is never bound | if another lane wires it, double-add is the risk (§6.4) |

### 5.3 What is *not* a cause (ruled out by evidence)

* **Additive `ONE/ONE` leaking into the cloud window** — impossible: the window is the only place the
  vanilla code wants ONE/ONE, and the shield agrees.
* **Alpha test cutting the cloud fringe** — the shield rewrites `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=0`
  at every flush (`main.cpp:2592-2599`), and `alpha >= 0` is always true. The entry force is
  `ALWAYS@0` (`main.cpp:4518`, `4525`). Neither can reject a texel.
* **Fog** — forced off at entry, and clouds are the background, so fog would only have *darkened* them.
* **Blend-off / REPLACE (the corona black-quad mechanism)** — the shield forces
  `ALPHABLENDENABLE=TRUE` at the flush, and `0x71398E` already set it.
* **The multiplicative `DESTCOLOR/ONE` pass** — it is the sun/moon pass, before the cloud block, and
  the cloud batches restore `ONE/ONE` afterwards.

---

## 6. Recommendation

### 6.1 Verdict: replace the rasterisation, not the generator

Keep `CClouds::Render` and its geometry. Replace **only the three buffered-batch rasterisations** with
a `ps_3_0` shader bound inside `SpriteFlushIndexedDraw_hook` when the cloud batch is being drawn.

Why not "keep patching FFP":

* Three of the five ranked causes (#1 stage ops, #4 sampler, #5 additive contrast) are **not
  repairable by the existing state-hygiene approach without adding more forced state to a path that
  already forces 30 writes**. Every added force is another thing to save/restore in a hook that has
  already produced two regressions (black sky, black quads).
* The vanilla path is *screen-space with baked colour and UV*, i.e. the ideal input for a pixel
  shader: zero vertex work, zero matrix, zero new geometry.
* A shader removes the FFP state class **for the cloud batch only** without touching the sun/moon/
  flare/dome, which is a much smaller blast radius than rewriting `CClouds::Render`.

Why not "replace the whole system" (the CryEngine / GTA V volumetric plans):

* P3 of `2026-10-25-gtav-cloud-atmosphere.md` (proper cloud deck, 2–3 altitude shells) is the right
  long-term target, but it is ~200 lines + 2–3 draws and needs P0/P1 (sky model) first. The audit shows
  the current failure is **state/content**, not the absence of a deck. Fixing the rasteriser first is
  the cheapest way to learn whether the geometry/coverage is even right.

### 6.2 Minimal viable replacement (spec)

| Item | Value |
|---|---|
| New file | `shaders/ps/CloudSpritePS.hlsl`, single entry **`main`** → **no** `tools/fast_build.py` edit (the `/E main` rule; same convention `CloudsPS.hlsl` documents at its line 23-27) |
| Profile | `ps_3_0` only |
| Vertex pipeline | **none** — `SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1 = 0x144)` + fixed-function VS |
| Samplers | **s0** = the texture CClouds already bound (do not rebind: `[0xC3E000]` for batches #1/#3, `[0xC6AA78]` for #2); **s9** = noise tile via `pipeBindNoise(9)` (`src/render/noise.cpp:913-940` uploads `c49..c52` and binds the tile) |
| Constants | **`c49..c52` only** — the existing reserved block (`shaders/ps/CloudsPS.hlsl:29-35`): c49 `{noiseScale, worleyScale, coverage, erosion}`, c50 `{.z = timeSeconds, .w = frameIndex}`, c51 `{scaleA, scaleB, scaleC, density}`, c52 `{jitter, dither, detail, unused}`. **No new upload.** Do not touch c0-c4 (they belong to whatever PS the previous pass left) |
| Resource | **`IDR_CLOUDSPRITE 281`** — 255 is reserved by the CryEngine plan for `CloudsVolumetric`, 256-279 are taken, 280 is staged for W8-3. Verify `resources/resource.h` immediately before adding |
| Output | premultiplied `float4(rgb*opacity, opacity)` — required because the *same* flush also serves SRCALPHA/ONE producers outside the cloud window (contract documented in `CloudsPS.hlsl:5-14`) |
| Blend assumption | ONE/ONE (shield already enforced). Do **not** change blend/depth in the shader path |
| Draw order | after the shield (`main.cpp:2734`), before `SpriteFlushIndexedDraw_orig` (`main.cpp:2735`); unbind with `RwD3D9SetPixelShader(NULL)` + raw NULL afterwards |

Shader body (v1, ~25 ALU + 2 fetches):

```
float4 tex  = tex2D(s0, vUV);                     // cloud/sprite texture as bound
float  n    = NoiseFbm3(vUV * c51.x + c50.z*scroll);   // s9.r
float  cov  = saturate((n - c49.z) * c51.z + c49.w);
float3 rgb  = tex.rgb * vColor.rgb * lerp(1, cov, c51.w);
float  a    = tex.a * vColor.a;
// optional: view-ray projection from screen pos for a deck look, then
// sample the same field at the deck UV instead of the sprite UV
return float4(rgb * a, a);                        // premultiplied
```

### 6.3 Cheapest A/B that does not need a shader (10 lines, 20 min)

Before committing to the shader, prove or kill causes #1 and #4 in one build:

1. In `CClouds__Render_hygiene`, force stage 0 on **entry** (raw `SetTextureStageState`):
   `COLOROP=MODULATE`, `COLORARG1=TEXTURE`, `COLORARG2=DIFFUSE`, `ALPHAOP=MODULATE`,
   `ALPHAARG1=TEXTURE`, `ALPHAARG2=DIFFUSE`.
2. Force `D3DSAMP_MIPFILTER = D3DTEXF_NONE` instead of `LINEAR` (`main.cpp:4523` / `4608`).

If the clouds regain contrast, the FFP lane is fixable and the shader becomes an enhancement rather
than a rescue. If they do not, the shader replaces the rasteriser and #3/#5 (content) are the
remaining work.

### 6.4 Cost / benefit

| Option | Work | Risk | Kills |
|---|---|---|---|
| Keep patching FFP (6.3) | ~20 min | low | #1 and #4, maybe |
| **Minimal PS replacement (6.2)** | ~2–3 h incl. build + resource | medium (new shader path, but no exe patch) | #1, #4, #5, and any future stage-op/sampler leak on this batch |
| Full cloud deck (GTA V P3) | days | high | everything, but only after P0/P1 |

**Do not** wire `CloudsPS.hlsl` into this path as-is: it takes a `[0,1]` layer UV and would tile the
noise *per sprite*, and it is owned by another lane. A separate `CloudSpritePS.hlsl` keeps the two
lanes independent.

---

## Appendix A — Address / Symbol Table

| Address | Symbol / role | Evidence |
|---|---|---|
| `0x713950` | `CClouds::Render` | callers `0x53DCA2`, `0x53DFA0` |
| `0x714640` | `CClouds::Render` epilogue `ret 0` | disasm |
| `0x714650` | `CClouds::RenderSkyPolys` | caller `0x53D81C`; `envmap.cpp:432` |
| `0x70CF20` | buffered sprite flush (`0xC6A158` count guard `0x70CF27`) | 8 callers, §3.1 |
| `0x70CF62` / `0x70CF9B` | the only two batch draw calls (`E8 → 0x734EA0`) | hooked, `main.cpp:2658-2661` |
| `0x70E480` | `cmp [0xC6A158],0x180` → flush-then-append (384 cap) | disasm |
| `0x70E4A0` | buffered sprite append (axis-aligned), sets `[0xC6A15C]=0` at `0x70E4A7` | disasm |
| `0x70EAB0` | buffered sprite append (rotated), sets `[0xC6A15C]=0` at `0x70EABE` | disasm |
| `0x70D000` | immediate `RenderOneXLUSprite` (sun/moon/flare, not the batch) | 6 call sites in CClouds |
| `0x70F440` | corona appender, sets `[0xC6A15C]=1` | corona doc §1.2 |
| `0x734EA0` | `RwIm2DRenderIndexedPrimitive` stub | corona doc §1.1 |
| `0xC5F958` / `0xC4B958` / `0xC6A158` / `0xC6A15C` | batch verts / indices / count / producer-flag | §2.2 |
| `0xC3E000` / `0xC6AA78` / `0xC3E008` / `0xC6AA74` | cloud A / shared sprite / multiplicative / sun textures | §2.3 |
| `0xC3E030` | game flag cleared at cloud entry | `0x71396D` |
| `0xC97B24` + `0x20` | RW driver state-set entry used by CClouds (L2+L3, no L1) | §1.1 |

## Appendix B — Open Questions

1. **Identity of `0xC3E000` vs `0xC6AA78`.** Both are `RwTexture*`; `0xC6AA78` is confirmed shared with
   `CPointLights`. `0xC3E000` is assumed to be the cloud-layer texture. One run with
   `[CloudBatch] tex=%p nverts=%d` at each of the three flushes settles it, and is needed before any
   shader that assumes "cloud texture in s0" is tuned.
2. **The `esi < 6` loop at `0x714262-0x714381`** produces batch #3 with the same texture as batch #1.
   Whether that is the high/low cloud layer split is not established here.
3. **Does a deferred corona batch actually land in `0x714019` in the field?** The mechanism is proven
   (`0x6FB621` → `RET`, `[0xC6A15C]=0` skip); the frequency needs the `[CloudBatch]` probe above.
4. **`disableClouds`** jumps `0x714145 → 0x71422A` (`main.cpp:7145`), i.e. it skips straight to flush
   #2. That is a useful bisect lever for "which batch is the flat one", but it also means the
   `disableClouds` path never draws batch #1.
5. **`cloudRenderFix` interaction**: the stage-op A/B in §6.3 must run with `cloudRenderFix=1`,
   otherwise nothing is forced and the result is uninterpretable.

