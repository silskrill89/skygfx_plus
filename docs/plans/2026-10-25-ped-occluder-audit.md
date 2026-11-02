# Ped Occluder Audit — 2026-10-25

**Scope:** decide whether peds are *not drawn* or *drawn then covered* at the
angles that produce the giant opaque flat quad (RGB ≈ `(32,40,30)` / `#20281E`,
razor edges, 13–25% screen coverage, cutting CJ at chest height in one frame and
covering a corpse in another). Read-only audit; only this file was written.

**Evidence base**

| Source | What it gives |
|---|---|
| `E:\games\gtasa_skygfx_plus\gta_sa.exe` (14,383,616 bytes, PE image base `0x400000`) | authoritative bytes for `0x53E0C3`, `0x708300`, `0x53DF40` (CScene::Render), `0x707F40`, `0x70A960` |
| `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (63,421 lines) | the **pre-fix** run: one `[BUILD]` at line 27, `22:35:22.707`; last line `22:42:48.161` |
| `src/core/main.cpp` (8,043 lines, mtime 22:51) | current source, **newer than the log build** (21:56). The stored-shadow function-entry shield at `main.cpp:7673-7724` is **not** in the log run |
| `SilentPatchSA.asi` (437,248 bytes, preferred base `0x10000000`) | the module that owns the `0x53E0C3` call site; init code at `0x100165A3` proves it |
| `src/rw/MemoryMgr.h` | `InterceptCall` is a raw 5-byte `E8 rel32` rewrite; no trampoline |

**Important timing caveat:** the log is the *before* image for the stored-shadow
entry shield. The current source already implements the recommended
`InjectHook(0x708300, ...)` at `main.cpp:7673-7724`; the log contains no
`[shadow_install] 0x708300 entry hook installed` line, so the logged run still
used the unshielded SilentPatch-owned call site. All log conclusions below are
about the **pre-fix** build unless stated otherwise.

---

## 0. Verdict

| Verdict | Confidence | Why |
|---|---:|---|
| **Drawn then covered** | **0.72** | Peds demonstrably execute (`[PedDual] legacy dual pass restored`, `main.cpp:2032` path), the ped exit leaves the device with blend off / stale alpha in 27/157 `[PedRS]` samples, and the two after-ped shadow paths that are unowned by the state contract are the stored-shadow buffer and the late static-shadow renderer (`0x706AB0` → `0x705900`), both of which can produce a world-space quad. The observed straight edge + partial body is an occlusion signature, not a cull signature. |
| Both per-angle (some angles cull, some cover) | 0.20 | `[Cull]` is not a ped counter, so a frustum/cull component cannot be excluded from the log alone. The A/B below distinguishes it. |
| Not drawn | 0.08 | No `[Cull]`/`[PedRS]` line shows a ped cull decision; `[PedRS]` always shows `zw dev=1` at ped exit, and the ped dual pass receipt proves the skin pipe ran. Culling would remove the whole ped, not cut it at chest height. |

**One-line answer:** the ped is drawn, then a later **world-space shadow quad**
(the stored-shadow buffer or the late static-shadow renderer, both rasterized
outside the shadow state contract) is painted over it. The flat color is
consistent with that quad being rasterized without its shadow texture: the
stored vertex color (the entry's RGBA bytes) is then written directly, and the
observed opacity implies a 255 alpha in the covered region.

### Ranked occluder list

| Rank | Candidate | After peds? | Confidence | Why |
|---:|---|---|---:|---|
| 1 | **Stored shadows** `CShadows::RenderStoredShadows` `0x708300` + its out-of-contract Im2D/water rasterization | yes (`0x53E0C3`, second call `0x53DDE4`) | **0.30** | not wrapped in the log run; entry sets only L1 state; no texture raster set; SilentPatch forces alpha test ALWAYS; function contains **no draw call**, so the rasterization is outside the contract |
| 2 | **Late static-shadow renderer** `0x706AB0` at `0x53EA0D` → `0x707CA0` → `0x705900` | yes (after peds, after stored/extra) | **0.25** | not wrapped; inner draw sets `ZTEST=0`, `blend=0`, `TEXTURERASTER=NULL`, draws 4 vertices, restores ZTEST/blend but **not** the texture; offscreen by design, but a wrong shadow camera/raster binding turns it into a perfect ZTEST-off occluder |
| 3 | **Extra player shadows** `0x70A960` + same shared-buffer rasterization | yes (`0x53E0C8`) | 0.15 | wrapped, but sets `CULLNONE` and never restores it; no texture raster set; same deferred draw boundary |
| 4 | **Water second pass** `RenderWater 0x6EF650` at `0x53E142` | yes (after shadows) | 0.08 | the shared Im2D shadow buffer is also referenced by the water renderer; water is drawn after the shadow block and can inherit a flat state |
| 5 | **Glass/particle Im2D** in `RenderEffects 0x53E170` | yes (`0x53EAD3`) | 0.05 | `[FxAlpha] RenderEffects.enter` shows device blend drift; alpha-blended by design, but a texture/state leak can flatten a quad |
| 6 | **Skidmarks** `0x720640` | yes (inside `RenderEffects`, call `0x53E175`) | 0.03 | ZWRITE off/blend on ground decals; wrong shape for a 13–25% screen quad |
| 7 | **CPlantMgr::Render** `0x5DBAE0` | yes (`0x53E103`) | 0.02 | alpha-tested grass; not a flat dark polygon |
| 8 | **PostFX** `0x53E227` | yes | 0.02 | fullscreen passes, not a world-space polygon; would cover the whole screen, not a chest-height edge |
| — | Mirror/env composite | **no** | 0.00 | `CMirrors::RenderMirrorBuffer` is called at `0x53EABA`, *before* CScene::Render (`0x53EABF`); cannot cover peds drawn later |

The per-occluder numbers are priors, not independent measurements; they sum to
0.90 with the residual 0.10 assigned to "an unidentified Im2D flush / state
consumer" (the draw site is not inside `0x708300`; see §3.4).

---

## 1. Exact frame order

### 1.1 Function boundaries

`CGame::Render` calls `CScene::Render` at **`0x53EABF`**; the call target is
**`0x53DF40`**. Therefore everything from `0x53DF40` to the `ret` before
`0x53EAC4` is `CScene::Render`; `0x53EAD3` (`RenderEffects`) and `0x53E227`
(`CPostEffects::Render`) are *outside* it, in `CGame::Render`, and therefore
run after all of `CScene::Render`.

The stored-shadow function has **two** call sites:

- `0x53E0C3` inside the main `CScene::Render` shadow block, and
- `0x53DDE4` in the earlier (mirror/reflection) shadow block.

`0x53DDE4` is `E8 17 A5 1C 00` → `0x708300`; the source comment at
`main.cpp:7627-7628` already records this second caller. This is why the
**function-entry** hook is the better composition point: it covers both callers
and does not fight SilentPatch for the call site.

### 1.2 Order table

Verified from the call sequence in `CScene::Render` (`0x53DF40`) and
`CGame::Render` (`0x53EABF` onward). Addresses are call sites in `gta_sa.exe`.

| # | Call site | Target | Pass | ZWRITE / ZTEST at draw | After peds? |
|---:|---|---|---|---|---|
| 1 | `0x53EABA` | `0x726090` | `CMirrors::RenderMirrorBuffer` (mirror composite) | mirror target, own state | **no** (before CScene::Render) |
| 2 | `0x53DF9B/0x53DFA0` | `0x713950` | `CClouds::Render` | clouds are blended, ZWRITE off | no (early scene) |
| 3 | `0x53DFCE` | `0x5D5B10` | `CCustomCarEnvMapPipeline::PreRenderUpdate` | pre-render update, no scene draw | no |
| 4 | `0x53DFD3` | `0x553A10` | `CRenderer::RenderRoads` | opaque world/roads, ZWRITE on, ZTEST on | no |
| 5 | `0x53DFD8` | `0x6FB630` | `CCoronas::RenderReflections` | additive/reflection, ZWRITE off | no |
| 6 | `0x53DFDD` | `0x553AA0` | `CRenderer::RenderEverythingBarRoads` | **buildings, vehicles, peds** (opaque), ZWRITE on, ZTEST on | no (this is the ped draw) |
| 7 | `0x53DFE9` | `0x59E6A0` (`push 0`) | font/HUD buffer pass (`0x59E480` calls CFont/Im2D text; not world geometry) | 2D text, blend on | no |
| 8 | `0x53DFEE` | `0x553220` | `CRenderer::RenderFadingInUnderwaterEntities` | opaque/fading entities | no |
| 9 | `0x53DFF7-0x53E009` | `0x6EF650` | `RenderWater` (first call, only when `bl==0`) | water surface; ZWRITE typically on | no / borderline |
| 10 | `0x53E019` | `0x5531E0` | `CRenderer::RenderFadingInEntities` | **fading peds/vehicles**, ZWRITE on | no (ped draw) |
| 11 | **`0x53E0BE`** | `0x707F40` | `CShadows::RenderStaticShadows` | **no draws** — cleanup/update loop only | yes |
| 12 | **`0x53E0C3`** | `0x708300` | `CShadows::RenderStoredShadows` | entry: ZWRITE=FALSE, ZTEST=TRUE, VERTEXALPHA=TRUE, FOG=FALSE, ALPHAREF=0; **no texture raster** | **yes** |
| 13 | **`0x53E0C8`** | `0x70A960` | `CShadows::RenderExtraPlayerShadows` | entry: ZWRITE=FALSE, ZTEST=TRUE, VERTEXALPHA=TRUE, FOG=FALSE, CULLNONE, ALPHAREF=0; **no texture raster**; CULLMODE not restored | yes |
| 14 | `0x53E0F7` | `0x59E6A0` (`push 1`) | font/HUD buffer again | 2D text, blend on | yes |
| 15 | `0x53E103` | `0x5DBAE0` | `CPlantMgr::Render` | alpha-tested grass, ZWRITE on | yes |
| 16 | `0x53E108-0x53E14E` | `0x6FBAA0`, `0x6EF650` | `CCoronas::RenderSunReflection`, `RenderWater` (second call) | additive sun / water surface | yes (after shadows) |
| 16b | `0x53EA0D` | `0x706AB0` | late static-shadow render (16 slots) → `0x707CA0` → `0x705900` | inner draw `ZTEST=0`, `blend=0`, `TEXTURERASTER=NULL`; offscreen shadow raster | **yes** (after peds/shadows) |
| 17 | `0x53EAD3` | `0x53E170` | `RenderEffects` | contains `CSkidmarks::Render` (`0x53E175`), `CCoronas::Render`, `g_fx` particles, glass, weapon FX | **yes** (outside CScene::Render) |
| 18 | `0x53E227` | `0x53E?` | `CPostEffects::Render` | fullscreen postfx, ZWRITE off | yes |
| 19 | `0x53EBE9` | `0x704D1E` etc. | `DrawFinalEffects` / colour filter | fullscreen, ZWRITE off | yes |

**Consequence for the "geometry drawn later with depth writes on"
hypothesis:** all building/vehicle/ped entity draws are in rows 4–10, before the
shadow block. No building or vehicle renderer runs after peds in this exe. The
after-ped passes that can write depth are the stored/extra shadow buffers
(logically `ZWRITE=FALSE`), the second water call (`0x53E142`), and
`CPlantMgr::Render`; the `0x59E6A0` pass is 2D text. A later **world building**
is therefore not the occluder; a later **shadow/water/effects** pass is.

Key corrections to the hypothesis' assumed order:

- **Static shadows at `0x53E0BE` do not draw.** `0x707F40` is a 0x60-byte loop
  that unlinks shadow entries and clears flags; it contains **no render-state
  calls and no draw calls**. The actual static-shadow volume draw is
  `0x707CA0`, called from `0x706B68` during entity rendering (before peds), and
  its solid quad at `0x705900` renders to an offscreen shadow raster with
  `ZTEST=0, blend=0, texture=NULL`. It cannot paint the main screen.
- **Water is drawn twice**, once before `RenderFadingInEntities` (conditional on
  `bl==0`) and once after the shadow block (`0x53E142`). The second call is a
  legitimate "after peds" candidate.
- **Skidmarks are not a separate CScene::Render pass**; `CSkidmarks::Render`
  (`0x720640`) is called inside `RenderEffects` at `0x53E175`, i.e. after all of
  CScene::Render and after peds/shadows.
- **PostFX/final effects are after RenderEffects**, not before it.

### 1.3 Where the peds are actually drawn

`CRenderer::RenderEverythingBarRoads` (`0x553AA0`) and
`CRenderer::RenderFadingInEntities` (`0x5531E0`) dispatch the entity atomics;
the ped skin pipe goes through the game's default callback, hooked at
`0x7491C0` to `myDefaultCallback`. The log receipt that the skin pipe executed
is:

```
[22:35:40.955] [PedDual] legacy dual pass restored (zwriteThresholdPed=128)
[22:35:40.964] [PedDual] first ped atomic alpha probe: material/prelit=0 texture=0
```

The ped entry contract forces `ZWRITE=TRUE` on all three layers
(`main.cpp:1778-1787`), the opaque contract forces `ZENABLE=TRUE`,
`ZWRITE=TRUE`, `ZTEST=TRUE`, `ALPHATESTENABLE=TRUE`, `ALPHAREF=1`
(`pedOpaqueEnforce`, `main.cpp:1077-1121`), and the exit sticky repair leaves
ZWRITE TRUE. `[PedRS]` confirms `zw rw=1 drv=1 dev=1` in all 27 diverged
samples. **The ped depth is in the depth buffer.**

---

## 2. Which pass can paint an opaque quad over a ped

### 2.1 Candidate matrix

`ZWRITE/ZTEST` is the state **at draw time** as best the disassembly and log
allow. "Can win depth" means the pass can pass `ZTEST` against a ped whose
depth was written.

| Candidate | ZWRITE | ZTEST | Can win depth? | Solid-fill path | Rank |
|---|---|---|---|---|---|
| `CShadows::RenderStoredShadows` `0x708300` | entry writes FALSE through **L1 only**; no wrapper in log run | entry writes TRUE through L1 only; log shows `dev ztest=1` at the next pass | yes if the shadow polygon projects nearer than the ped, or if the device ZTEST is stale | **texture raster never set**; vertex color alpha 255; SilentPatch sets `ALPHAFUNC=ALWAYS`; if the bound raster is NULL/wrong the quad is a flat vertex-color fill. Blend-off is a second path (ped exit leaves `dev blend=0` in 15/27 samples) | **1** |
| `CShadows::RenderExtraPlayerShadows` `0x70A960` | entry FALSE through L1, wrapper forces L1+L2+L3 | entry TRUE; wrapper forces TRUE | same geometry risk | texture raster never set; wrapper forces blend on, so flat fill needs a texture/color leak; leaves `CULLMODE=NONE` | 2 |
| `RenderWater` second call `0x6EF650` (`0x53E142`) | water surface; typically on for opaque water | on | yes (surface depth) | not a 4-vertex dark quad; translucent water shader | 3 |
| Glass/particle Im2D in `RenderEffects` | off | on | yes if a sprite is in front | alpha-blended by design; a texture/state leak can flatten one quad | 4 |
| `CSkidmarks::Render` `0x720640` | off | on | yes on the ground | small textured decals; wrong scale/shape | 5 |
| `CPlantMgr::Render` `0x5DBAE0` | on | on | yes | alpha-tested grass blades; not a flat polygon | 6 |
| `CPostEffects::Render` | off | off | no (fullscreen) | fullscreen fill, not a world-space quad | 7 |
| `CMirrors::RenderMirrorBuffer` | mirror target | mirror target | no (before peds) | mirror composite, before peds | — |

The `0x59E6A0` pass at `0x53E0F7` is **not** a world/building pass: its
`0x59E480` helper calls the CFont/Im2D text routines (`0x4A1410`, `0x821D30`,
`0x821E02`), so it renders 2D text quads. It cannot produce a 13–25% flat
world-space polygon and is excluded from the ranked list.

### 2.2 Static shadows: cleanup vs. the late-render caveat

`0x707F40` (called at `0x53E0BE`) disassembles to:

```
00707F40  push ebx
00707F41  mov ecx, 0xC4A034          ; shadow array
00707F46  xor ebx, ebx
00707F48  cmp [ecx], ebx             ; entity pointer?
00707F4A  je 0x707F90
...
00707F8B  mov [ecx], ebx             ; clear entry
00707F90  mov [ecx+0x36], bl
00707F93  add ecx, 0x40
00707F96  cmp ecx, 0xC4AC34
00707F9C  jl 0x707F48
00707F9E  pop ebx
00707F9F  ret
```

No `RwRenderStateSet`, no draw. The wrapper around it (`shadowContractRun`,
`main.cpp:1520-1524`) is forcing state around a **cleanup loop**. The log's
`[shadow_contract] static` lines therefore describe a real state force/restore,
but not a screen draw. The static-shadow volume draw is the
`0x707CA0 → 0x705900` path, which sets `ZTEST=0`, `blend=0`, `texture=NULL`
and draws 4 vertices through `RwIm2DRenderIndexedPrimitive (0x734EA0)` while
bound to a shadow raster (`RwCameraBeginUpdate`/`EndUpdate`). That is an
**offscreen** shadow texture pass; it is not the giant screen quad.

**Late static-shadow renderer (`0x706AB0` at `0x53EA0D`).** The
static-shadow *render* (as opposed to the `0x707F40` cleanup at `0x53E0BE`) is
called at `0x53EA0D`, i.e. **after peds, after the stored/extra passes, and
after the font/water/grass block**. It walks 16 slots and calls `0x707CA0`,
which calls `0x705900`. `0x705900` sets `ZTEST=0`, `blend=0`,
`TEXTURERASTER=NULL`, draws 4 vertices via `0x734EA0`, and restores ZTEST/blend
but **not** the texture raster. The draw is bracketed by
`RwCameraBeginUpdate`/`EndUpdate` on the shadow object's camera/raster, so the
evidence says it is an offscreen shadow-texture pass, not the main screen. If
that camera/raster binding is wrong (a state leak), this is a
ZTEST-off/blend-off/texture-NULL solid quad after peds — a high-severity
occluder. Probe `0x705900`'s camera/raster (`[edi]`, `[edi+0x60]`) to confirm.

### 2.3 The stored/extra pass does not contain its own draw call

This is the central disassembly finding. `0x708300` makes these calls:

| Call site | Target | Purpose |
|---|---|---|
| `0x70831D`, `0x70832A`, `0x708337`, `0x708342`, `0x70834E`, `0x70835B` | `[RwEngineInstance+0x20]` | six `RwRenderStateSet` calls (L1 cache only) |
| `0x7083A4` | `0x707460` | per-shadow `SRCBLEND`/`DESTBLEND` switch |
| `0x70841F` | `0x707850` | project the 4 stored corners to screen space |
| `0x708453`, `0x7084A1`, `0x708638` | `0x7EF450` / `0x7EF550` / `0x7EF520` | RW free-list / buffer management |
| `0x708518` | `0x821B40` | float→int helper |
| `0x7085F9` | `0x7077A0` | rebase the index buffer (`0xC4B958`) to the vertex base (`0xC4D958`) |
| `0x708687`, `0x708693` | `[RwEngineInstance+0x20]` | exit: `VERTEXALPHA=FALSE`, `ZWRITE=TRUE` |

There is **no** `RwIm2DRenderPrimitive` (`0x734E90`),
`RwIm2DRenderIndexedPrimitive` (`0x734EA0`), or register-indirect draw in
`0x708300`, `0x7086B0`, `0x709CF0`, `0x70A040`, `0x70A470`, `0x70A630`, or
`0x70A960`. The pass fills the shared Im2D buffers:

- `0xC4B958` — index buffer (also used by the sprite/water Im2D paths; 79 code
  references),
- `0xC4D958` — vertex buffer (also referenced by the water renderer; 88 code
  references).

The actual rasterization is therefore **outside the shadow function**, in a
shared Im2D/water path that the shadow contract does not wrap. The exact draw
site is unresolved; the leading candidate is the common indexed-draw path
`0x734EA0` used by the sprite/water systems, but it is not called from any of
the shadow functions. This is both:

- why the entry shield can still help (it forces the state during buffer
  construction and, more importantly, can be extended to force the texture), and
- why it is not sufficient by itself: if the flush happens after the function
  returns, the exit restore (`VERTEXALPHA=FALSE`, `ZWRITE=TRUE`) has already
  undone the entry force. **The draw site must be probed before declaring the
  entry shield a complete fix.**

A second confirmation that the draw is shared: `0x734EA0` is a small
thunk (`jmp 0x403927`; the RW entry is behind that address), and its only callers are
`0x701FE9`, `0x7058BC` (the offscreen static volume), `0x70CF62`/`0x70CF9B`
(the sprite flush), `0x726284`, `0x7273FF`. None of those is the stored/extra
shadow function. The shadow buffer is consumed by one of the shared paths, not
by `0x708300`.

---

## 3. The uncontrolled stored-shadow pass

### 3.1 Byte dump at `0x53E0C3` and the rel32 math

PE mapping used: `.text` VA `0x401000`, raw `0x400`; file offset = `VA - 0x400C00`.

```
VA 0x53E0B0: 2B 00 8B 15 3C 70 C1 00 52 E8 D2 00 2B 00
VA 0x53E0BE: E8 7D 9E 1C 00     ; call 0x707F40  RenderStaticShadows
VA 0x53E0C3: E8 38 A2 1C 00     ; call 0x708300  RenderStoredShadows
VA 0x53E0C8: E8 93 C8 1C 00     ; call 0x70A960  RenderExtraPlayerShadows
```

For the stored site:

```
site          = 0x53E0C3
next          = site + 5 = 0x53E0C8
rel32         = 0x001CA238           (little-endian bytes 38 A2 1C 00)
target        = 0x53E0C8 + 0x001CA238 = 0x708300   ✔
```

The previous instruction is the `E8` call at `0x53E0BE`, so no `push` can sit
between the call and this site: the callee is zero-arg. `0x53E0C8` likewise
resolves to `0x70A960`.

### 3.2 Why the log says `orig=68E8DD00` — SilentPatch owns the site

The log line is:

```
[22:35:22.735] [INFO ] :0 () - [shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows (expect op=E8 orig=00708300)
```

The implied rel32 for the logged target is:

```
0x68E8DD00 - 0x53E0C8 = 0x6894FC38  -> bytes 38 FC 94 68
```

The file has `38 A2 1C 00`. Three bytes differ. This is a **runtime rewrite**,
not a different exe/build. The rewriter is SilentPatchSA.asi. Its init code at
VA `0x100165A3` is unambiguous:

```
100165A3  A1 C4 E0 53 00        mov eax, [0x53E0C4]      ; original rel32
100165A8  05 C8 E0 53 00        add eax, 0x53E0C8        ; -> 0x708300
100165AD  A3 BC 79 06 10        mov [0x100679BC], eax    ; save original target in BSS
100165B2  B8 00 DD 00 10        mov eax, 0x1000DD00      ; SilentPatch wrapper VA
100165B7  2D C8 E0 53 00        sub eax, 0x53E0C8
100165BC  A3 C4 E0 53 00        mov [0x53E0C4], eax      ; rewrite call to wrapper
```

SilentPatch's runtime base must therefore be **`0x68E80000`** in this run
(deduced from the exact VA match; its preferred base is `0x10000000`):

```
wrapper in memory = 0x1000DD00 - 0x10000000 + 0x68E80000 = 0x68E8DD00  ✔
```

The skygfx DLL base is `0x68A90000` (`skygfx_dbg.log:4`), image size
`0x3E7000`, ending at `0x68E77000`; `0x68E8DD00` is 0x16D00 past skygfx and
exactly at SilentPatch base + RVA `0xDD00`. The `[0x100679BC]` slot is BSS
(zero in the file), so the original target is filled at runtime. The wrapper at
RVA `0xDD00` is:

```
1000DD38  push 0x1D
1000DD3A  mov eax,[0x100677DC]      ; RwEngineInstance
1000DD3F  call [eax+0x24]           ; GetRenderState(29, &saved)
1000DD4D  push 8
1000DD4F  push 0x1D
1000DD56  call [eax+0x20]           ; SetRenderState(29, ALWAYS)
1000DD58  call [0x100679BC]         ; original 0x708300
1000DD6A  push [ebp-0x14]
1000DD6D  push 0x1D
1000DD74  call [eax+0x20]           ; SetRenderState(29, saved)
```

In higher-level form:

```
GetRenderState(rwRENDERSTATEALPHATESTFUNCTION = 29)  ; save
SetRenderState(29, 8)                                ; rwALPHATESTFUNCTIONALWAYS
call [0x100679BC]                                    ; original 0x708300
SetRenderState(29, saved)                            ; restore
```

It does **not** touch blend, ZTEST, ZWRITE, texture, or colour-write mask. So:

- the `0x53E0C3` "MISMATCH" is **SilentPatch ownership**, not a corrupt exe;
- SilentPatch only makes the stored pass' alpha test **always pass** (removing
  the last chance that the shadow texture's alpha would reject a texel);
- the rest of the device state is still whatever the previous consumer left.

The current source's comment at `main.cpp:7623-7628` already identifies this
address as "already rewritten" and moves the shield to the function entry.
The log predates that change.

### 3.3 `0x708300` state sets

RW state IDs from `external/d3d9/rwplcore.h:5066-5218`; the calls use
`RwEngineInstance->fpRenderStateSet` at `[0xC97B24+0x20]`, i.e. the game's
`RwRenderStateSet` path. That path writes the L1 cache and updates the RW
driver/device only when the cache value changes; it is **not** the mod's
three-layer force (`RwRenderStateSet` + `RwD3D9SetRenderState` + raw device).

| VA | Rw state | Value | Meaning |
|---|---|---|---|
| `0x70830E`/`0x70831D` | `8` | `0` | `ZWRITEENABLE = FALSE` |
| `0x708326`/`0x70832A` | `6` | `1` | `ZTESTENABLE = TRUE` |
| `0x708333`/`0x708337` | `0xC` | `1` | `VERTEXALPHAENABLE = TRUE` (blend on) |
| `0x70833F`/`0x708342` | `0xE` | `0` | `FOGENABLE = FALSE` |
| `0x70834B`/`0x70834E` | `0x1E` (30) | `0` | `ALPHATESTFUNCTIONREF = 0` |
| `0x708357`/`0x70835B` | `9` | `2` | `TEXTUREFILTER = rwFILTERLINEAR` |
| `0x70867E`/`0x708687` | `0xC` | `0` | exit: `VERTEXALPHAENABLE = FALSE` |
| `0x70868A`/`0x708693` | `8` | `1` | exit: `ZWRITEENABLE = TRUE` |

The outer loop walks 8 shadow slots at `0xC4A066..0xC4AC66` (0x40 bytes
each); a slot is skipped when its type byte is 0 or its `+5` 'needs refresh'
byte is non-zero, and each slot's linked list at `+0x54` is walked.

Per-shadow blend switch `0x707460` (called at `0x7083A4`):

| Shadow type | `SRCBLEND` (state 10) | `DESTBLEND` (state 11) |
|---:|---:|---:|
| 1, 4, 5, 6, 8 | `SRCALPHA` (5) | `INVSRCALPHA` (6) |
| 2 | `ONE` (2) | `ONE` (2) |
| 3 | `ZERO` (1) | `INVSRCCOLOR` (4) |
| 7 | unchanged | unchanged |

**Not set anywhere in the pass:** `ALPHATESTENABLE`, `ALPHAFUNC` (SilentPatch
sets it to ALWAYS), `TEXTURERASTER`, `COLORWRITEENABLE`, `STENCILENABLE`,
`CULLMODE`, `BLENDOP`. These are exactly the states that can turn the quad into
a flat fill:

- alpha test: SilentPatch forces ALWAYS → no texel rejected;
- texture: never set → whatever the ped/static/effects pass left;
- colour write: never set → whatever postfx/ped left;
- blend: set to `VERTEXALPHA=TRUE` + normal alpha, but **through L1 only**;
  if the device is drifted behind an unchanged L1 cache, blend can stay off;
- cull: never set → the quad's facing depends on the previous pass.

### 3.4 The stored pass' solid-fill chain

The observed flat color is most simply explained by **missing/wrong texture +
vertex alpha 255**, not necessarily by blend off:

1. The ped pass runs with the ped's textures bound and, at exit, restores
   blend/alpha/cull/ZWRITE but **not** `TEXTURERASTER` or
   `COLORWRITEENABLE` (`main.cpp:2068-2095`; `PedOpaqueState` has no texture
   field, `main.cpp:993-1001`).
2. `0x708300` never binds the shadow texture. It relies on the static shadow
   path (`0x7059F0`/`0x705A00`) or an earlier consumer to have left the shadow
   raster bound.
3. SilentPatch sets `ALPHAFUNC=ALWAYS`, so the shadow texture's alpha cannot
   reject the interior.
4. The stored vertex color is packed at `0x708528-0x708544` from the bytes
   returned by `0x707850`, which are derived from the shadow entry's
   `[ebp+0x32..0x35]`; the alpha byte is typically 255 in the interior. With a
   NULL/wrong texture, the rasterizer writes that flat RGB directly — the
   observed opacity implies 255 in the covered region, and the observed
   `(32,40,30)` / `#20281E` is the candidate vertex color.
5. If the device blend is also stale-off (the ped exit shows `dev blend=0` in
   15/27 `[PedRS]` samples), the quad is opaque even if the texture is correct.
   The flat color is more specific to a missing/wrong texture than to blend-off
   alone, but both are uncontrolled in the log build.

This is a **drawn then covered** chain: the ped is in the depth buffer, the
shadow quad is rasterized later, and the shadow's normal soft-alpha shape is
lost because its texture/alpha contract is absent.

### 3.5 Function-entry `InjectHook(0x708300, ...)` evaluation

The current source already implements it at `main.cpp:7673-7724`:

- verifies the first 5 bytes are `A1 24 7B C9 00` (`mov eax,[0xC97B24]`);
- allocates a 32-byte RWX trampoline;
- copies the 5 stolen bytes + `E9` back to `0x708305`;
- sets `CShadows__RenderStoredShadows_orig = trampoline`;
- `InjectHook(0x708300, CShadows__RenderStoredShadows_hook, PATCH_JUMP)`.

**Pros**

- Works even though SilentPatch owns `0x53E0C3`; no need to touch SilentPatch's
  BSS pointer (`0x100679BC`) or its wrapper.
- Covers both call sites (`0x53DDE4` mirror block and `0x53E0C3` main block) and
  any future/direct caller.
- Composes with SilentPatch: the call chain becomes
  `SilentPatch wrapper (ALPHAFUNC=ALWAYS) → 0x708300 entry hook (force canonical
  state) → trampoline (original body)`. SilentPatch's alpha-test fix is
  overridden by the canonical force, which is harmless because the canonical
  test is `GREATEREQUAL@0` (also effectively "accept everything").
- Load-order robust: SilentPatch captures the original `0x708300` target, not
  the address of the entry hook; whichever module installs last, the call still
  lands on `0x708300` and hits the hook.

**Cons / risks**

- Requires RWX memory and a 5-byte patch. Verify the prologue every launch;
  skip on mismatch (the current code does).
- The trampoline must live for the process lifetime; never `VirtualFree` it.
- `PATCH_JUMP` (E9) is correct; `PATCH_CALL` would corrupt the stack because the
  hook returns normally.
- The hook runs **inside** SilentPatch's wrapper; the order of alpha-test writes
  is `ALWAYS` → `GREATEREQUAL@0` → original body → restore → SilentPatch
  restore. Document this, but it is benign.
- **It does not cover the deferred draw.** If the shared Im2D/water flush
  rasterizes the buffer after `0x708300` returns, the exit restore has already
  undone the force. The current shield is necessary but may not be sufficient.
- It is **unconditional** in the current source. The user asked for an
  INI-gated shield; add `storedShadowContract` and gate the install/run.
- It does not force `TEXTURERASTER`, `COLORWRITEENABLE`, `STENCILENABLE`, or
  `CULLMODE`; those are the remaining flat-fill paths.

**Recommendation:** keep the entry hook, add the INI gate, and extend
`shadowContractForce`/`shadowContractRestore` with texture + colour-write +
cull. Also add a one-shot probe at the real Im2D draw (`0x734E90`/`0x734EA0`)
to prove where the buffer is rasterized. Do **not** replace the entry hook with
a call-site intercept; the call site is SilentPatch-owned and the second caller
(`0x53DDE4`) would be missed.

---

## 4. Per-angle mechanism and visual signature

### 4.1 How a world-space stored shadow can cover a ped only from some angles

A stored shadow is a **world-space quad** (4 corners stored with the caster,
`CShadows::Shadows[]` at `0xC4A034..0xC4AC34`, 0x40-byte entries). At render
time `0x707850` projects the corners to screen space and `0x708300` writes a
4-vertex/2-triangle Im2D primitive. Its screen footprint and its screen-space
depth are functions of the camera position and orientation:

1. **Perspective amplification near the quad plane.** When the camera is close
   to the plane containing the shadow polygon, the projected area grows without
   bound; a shadow that is small on the ground at one yaw can cover 13–25% of
   the screen at another. A corner crossing the near plane produces a straight
   clipped edge across the screen.
2. **Foreground caster.** A building/tree/ped shadow on a surface between the
   camera and CJ can be nearer than CJ's chest. The depth test passes and the
   quad covers the lower/upper body. At another yaw the same shadow projects
   off to the side or behind CJ and fails the depth test.
3. **Wrong depth / stale ZTEST.** If the stored pass inherits a stale device
   `ZTEST` (its entry write goes through L1 only), the quad ignores the ped's
   depth entirely and covers it from any angle where the quad's screen area
   overlaps. The log's `dev ztest=1` at the *next* pass does not prove the
   device was 1 during the stored draw; the entry force is L1-only in the log
   build.
4. **Flat fill.** With the shadow texture missing/wrong (never set by
   `0x708300`) and alpha test forced ALWAYS by SilentPatch, the quad is a flat
   vertex-color fill; the soft alpha shape that normally makes a shadow read as
   a shadow is gone. That is why the artifact has razor polygon edges instead of
   a gradient.
5. **Chest-height cut.** The edge is the polygon's straight edge, not a cull
   plane. It cuts whatever is behind it at that screen line: CJ's chest in one
   frame, a corpse's torso in another.

### 4.2 Distinguishing "covered" from "culled"

| Observation | Drawn then covered | Culled / not drawn |
|---|---|---|
| Ped silhouette | partially visible; straight edge across the body | whole ped vanishes at once |
| Edge shape | straight polygon edge, constant color below/above | no edge; background shows through |
| Camera motion | edge moves smoothly with yaw/pitch; area changes continuously | binary on/off as the bounding sphere leaves the frustum |
| Ped shadow | may remain (the shadow pass still runs) | shadow remains while the ped is gone |
| Depth buffer | ped depth present (`[PedRS] zw dev=1` in all 27 diverged samples) | ped depth absent |
| A/B `pedCullMode=1` (CULLNONE) | no change | whole-ped disappearance changes |
| A/B stored shield off | flat quad disappears / becomes a soft shadow | no change |
| Color | constant `(32,40,30)` over the covered region | background/sky color |

The observed "razor edge + partial body + constant flat color" is the covered
column, not the culled column. The `[Cull]` tag does not contradict this: it is
enter/exit instrumentation, throttled, and only logs rare NONE/FRONT restores;
it never names a ped or a cull decision.

### 4.3 The "both per-angle" possibility

If some angles show a **whole-ped disappearance** (no partial edge) while others
show the flat quad, both mechanisms are present. The log cannot exclude this:
`[Cull]` has 19 `saved=1` (CULLNONE) enters, but those are legitimate
mirror/shadow-volume spans (the code comments at `pipelinecommon.cpp:1256-1264`
say NONE/FRONT are legitimate for stencil volumes and the mirror pass). The
A/B matrix in §7 separates the two.

---

## 5. Log evidence

### 5.1 Build identity

```
[22:35:22.707] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

Only one `[BUILD]` line in 63,421 lines. Session: `22:35:22.707` →
`22:42:48.161` (~7m25s). The source tree's stored-shadow entry shield is newer
(mtime 22:51); the log has no `[shadow_install] 0x708300 entry ...` line.

### 5.2 Counts

| Tag | Total | Sub-counts | At a ped draw? | Divergence evidence |
|---|---:|---|---|---|
| `[shadow_install]` | 4 | 1 OK `0x53E0BE`, 2 for `0x53E0C3` (MISMATCH + SKIPPED), 1 OK `0x53E0C8` | no | stored site skipped |
| `[shadow_contract]` | **212** | **84 static, 128 extra, 0 stored**; drift 1:44, 2:75, 3:45, 4:48 | no (shadow passes after peds) | **all 212 are divergent**; no `stored` line at all |
| `[ShadowHeal]` | **1** | one-shot (`dbglog_once`) | no | `dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)` |
| `[PED]` | **75** | **64 `ZWRITE off/desynced`** (1× `rw=0 dev=0`, 63× `rw=1 dev=0`), 11 `dual pass skipped` (`pipe=048C6410`, default pipe), 0 sticky-repair lines | **yes** (ped entry) | the device ZWRITE was off at 64 ped entries; the contract forces TRUE |
| `[PedRS]` | **157** | **130 ok, 27 DIVERGED** | **yes** (ped exit) | 15× `blend rw=1 drv=1 dev=0 | aref 100/100/100`; 6× `blend 1/0/0 | aref 1/1/1`; 3× `blend 1/1/1 | aref 100/100/1`; 1× `blend 1/1/0 | aref 100/100/0`; 1× `blend 1/0/0 | aref 100/100/1`; 1× `blend 1/1/0 | aref 100/100/1` |
| `[Cull]` | **354** | 144 enter (19× `saved=1 forced=1`, 125× `saved=2 forced=2`), 209 exit (`restored=1`/`2`), 1 `baseline BACK` | **not ped-specific** | no `DIVERGED` line; the tag only logs enter and rare NONE/FRONT restores, throttled; cannot prove a ped cull |
| `[FxAlpha]` | **240** | 212 `PostFx.repaired`, 26 `layer drift BEFORE repair` (10 PostFx.exit, 7 SunReflection, 6 RenderEffects.enter, 3 Coronas.Reflections), 2 `camRas != screen` | no (effects/postfx, after peds) | device `vtx`/`blend`/`ref` drift at the effects boundary |

The `[PED]` sticky-repair line count is **0** even though the code path at
`main.cpp:2076-2082` writes the repair. The line shares the `PedZWrite` throttle
bucket with the entry line, so it can be starved; the `[PedRS]` `zw dev=1` in
all 27 diverged samples is the better proof that ZWRITE was left TRUE.

### 5.3 Representative quotes
Log lines are quoted with the log file's `�` byte normalized to `—` for
readability; the counts and timestamps are unchanged.


Stored-shadow install (the core mismatch):

```
[22:35:22.734] [INFO ] :0 () - [shadow_install] 0x53E0BE op=E8 orig=00707F40 OK zeroarg=1 site=CShadows::RenderStaticShadows (expect op=E8 orig=00707F40)
[22:35:22.735] [INFO ] :0 () - [shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows (expect op=E8 orig=00708300)
[22:35:22.735] [INFO ] :0 () - [shadow_install] 0x53E0C3 SKIPPED — vanilla path kept
[22:35:22.736] [INFO ] :0 () - [shadow_install] 0x53E0C8 op=E8 orig=0070A960 OK zeroarg=1 site=CShadows::RenderExtraPlayerShadows (expect op=E8 orig=0070A960)
```

The only heal line (the contract is alive, but only for the wrapped passes):

```
[22:35:41.044] [INFO ] :0 () - [ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=0)
```

First `[shadow_contract]` for the unshielded stored pass' *successor* (extra),
showing the device still at `blend=0` / `zw=1` / `fog=1` after the stored pass:

```
[22:35:41.042] [INFO ] :0 () - [shadow_contract] extra pre-entry layer drift: drift=2 rw(blend=0 src=5 dst=6 ref=0 ztest=1 zw=1 fog=0) drv(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=0) dev(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=1)
```

Static wrapper receipt (around the cleanup-only pass):

```
[22:35:49.268] [INFO ] :0 () - [shadow_contract] static pre-entry layer drift: drift=1 rw(blend=1 src=5 dst=6 ref=0 ztest=1 zw=1 fog=1) drv(blend=1 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=1) dev(blend=0 src=5 dst=6 ref=0 ztest=1 zfunc=4 zw=1 fog=1)
```

Ped entry and exit divergence:

```
[22:35:40.947] [INFO ] :0 () - [PED] ZWRITE off/desynced on entry (rw=0 dev=0) — forcing TRUE for cb
[22:36:45.034] [INFO ] :0 () - [PedRS] DIVERGED cull rw=2 drv=2 dev=2 | blend rw=1 drv=1 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=5 drv=5 dev=5 | aref rw=100 drv=100 dev=100
```

The `[PedDual]` receipt that proves the skin pipe ran:

```
[22:35:40.955] [INFO ] :0 () - [PedDual] legacy dual pass restored (zwriteThresholdPed=128)
```

Effects boundary drift (after peds, not at the ped draw):

```
[22:36:10.996] [INFO ] :0 () - [FxAlpha] RenderEffects.enter: layer drift BEFORE repair — rw(src=5 dst=6 vtx=0 fn=7 ref=1) d3d(src=5 dst=6 vtx=1 fn=7 ref=1) dev(src=5 dst=6 vtx=0 fn=7 ref=1 op=1)
[22:35:40.973] [INFO ] :0 () - [FxAlpha] Coronas.Reflections: layer drift BEFORE repair — rw(src=5 dst=6 vtx=1 fn=5 ref=2) d3d(src=5 dst=6 vtx=1 fn=5 ref=2) dev(src=5 dst=6 vtx=0 fn=5 ref=2 op=1)
```

Cull instrumentation (no divergence line; NONE spans are legitimate):

```
[22:35:40.943] [INFO ] :0 () - [Cull] enter saved=1 forced=1
[22:35:41.025] [INFO ] :0 () - [Cull] baseline BACK
[22:35:50.660] [INFO ] :0 () - [Cull] exit restored=1
```

### 5.4 What the log proves and does not prove

**Proves**

- Peds execute the skin-pipe dual pass (`[PedDual]`).
- The ped contract sees a device ZWRITE off at 64 entries and forces it TRUE;
  `[PedRS]` shows `zw dev=1` at all 27 diverged exits. Ped depth is written.
- The stored pass is **not** under the shadow contract in the log build
  (`0x53E0C3` skipped; no `stored` line).
- The extra pass is wrapped, but enters with the device at `blend=0` / `zw=1` /
  `fog=1` and `drift=2..4`; the shadow contract's heal is running, but only for
  the wrapped passes.
- The ped exit leaves the device with blend off and/or a stale alpha ref in
  27/157 samples; the next consumer inherits that.

**Does not prove**

- That the stored pass *itself* rasterized with blend off. Its entry writes go
  through L1; the static wrapper immediately before it may have healed the
  device to blend=1. The log's `extra` pre-entry `blend=0` is the stored pass'
  **intentional exit** (`VERTEXALPHA=FALSE`), not necessarily its draw state.
- That the stored pass' draw happened inside `0x708300`; the disassembly says it
  did not.
- That the late static-shadow renderer's `0x705900` draw targets the main screen;
  it is bracketed by `RwCameraBeginUpdate`/`EndUpdate` on the shadow object's
  camera/raster, so it is offscreen unless that binding is wrong.
- That culling is absent. `[Cull]` is not a ped counter; it cannot see a
  `FrustumTestSphere` rejection.

---

## 6. Minimal fix per occluder

Prefer **INI-gated shields**; default the new gates to 1 only after an A/B run
proves no visual regression. The existing `pedOpaqueContract` key is at
`skygfx.default.ini:180`; `pedCullMode` is at `skygfx.h:650`. There is currently
no `storedShadowContract` key.

### P0 — Stored-shadow entry shield + texture/colour-write force

**Already in source:** `main.cpp:7673-7724` installs the entry hook with a
verified 5-byte prologue and a trampoline. **Still missing:**

1. Add `int storedShadowContract;` to `Config` (near `pedOpaqueContract`,
   `skygfx.h:642`), default 1 in `skygfx.default.ini`, parse it in `readIni`,
   and add it to the mode presets.
2. Gate the install or the hook body on `config->storedShadowContract`. Prefer
   **install always, check the flag in the hook** so the trampoline stays valid
   even if the INI is reloaded mid-session:
   ```c
   static void CShadows__RenderStoredShadows_hook(void) {
       if(config && config->storedShadowContract)
           shadowContractRun(CShadows__RenderStoredShadows_orig, "stored");
       else if(CShadows__RenderStoredShadows_orig)
           CShadows__RenderStoredShadows_orig();
   }
   ```
3. Extend `ShadowContractState` (`main.cpp:1240-1243`) with:
   - `RwRaster *rwTexture;` / `IDirect3DBaseTexture9 *devTexture;`
   - `DWORD devColorWrite;`
   - `DWORD devStencil;`
   - `RwUInt32 rwCull; DWORD drvCull, devCull;`
4. Extend `shadowContractForce` (`main.cpp:1345-1383`) to:
   - bind the game's shadow texture raster through `rwRENDERSTATETEXTURERASTER`
     + `RwD3D9SetTexture` + raw `SetTexture` (the raster selected by
     `0x7059F0`/`0x705A00`; if it is NULL, log and skip the texture force
     rather than binding NULL);
   - force `D3DRS_COLORWRITEENABLE = D3DCOLORWRITEENABLE_ALL`;
   - force `D3DRS_STENCILENABLE = FALSE`;
   - force `CULLMODE` to the captured mode or `CULLNONE` (shadows are
     two-sided).
5. Extend `shadowContractRestore` to restore the captured texture, colour-write,
   stencil, and cull on all three layers.
6. Add a one-shot `[StoredShadowShield]` log with the pre-entry device
   `blend/src/dst/ref/ztest/zw/texture/colorwrite` and the shadow raster pointer.

**A/B:** `storedShadowContract=0` should reproduce the flat quad; `=1` should
remove it or turn it into a soft shadow. If the quad persists with `=1`, the
draw is outside the function and P0b is required.

### P0b — Probe and shield the real Im2D draw

Because `0x708300` contains no rasterization call, add a one-shot probe at the
actual draw:

- `0x734EA0` (`RwIm2DRenderIndexedPrimitive`) and `0x734E90`
  (`RwIm2DRenderPrimitive`) are thunks/jumps; hook the implementation behind
  `0x403927` or wrap the thunk with a naked `PATCH_JUMP` trampoline.
- Log the vertex/index pointers, primitive count, current texture raster,
  `ALPHABLENDENABLE`, `SRCBLEND`, `DESTBLEND`, `ALPHATESTENABLE`, `ALPHAREF`,
  `ZWRITEENABLE`, `ZENABLE`, `ZFUNC`, `COLORWRITEENABLE`, and the first vertex
  colour. If the pointer is `0xC4D958` / `0xC4B958`, it is the shadow buffer.
- If the shadow buffer is drawn by the water/sprite flush, force the canonical
  shadow state **at that draw** (not at `0x708300` entry). Gate it with the same
  `storedShadowContract` key.

### P1 — Ped exit texture / colour-write shield

The ped pass is the state donor. Add to `PedOpaqueState`:

- `RwRaster *rwTexture;` + `IDirect3DBaseTexture9 *devTexture;`
- `DWORD devColorWrite;`

Capture at ped entry, restore at ped exit through L1+L2+L3 (after
`pedOpaqueRestore`, `main.cpp:2095`). Add `int pedTextureShield;` to
`Config` / INI, default 1. This alone may not remove the quad, but it removes
the "ped texture on a shadow quad" path.

### P2 — Extra player shadow: restore CULLMODE + force texture

`0x70A960` sets `CULLMODE=NONE` at entry (`0x70A9C1-0x70A9C5`) and never
restores it; its exit (`0x70B6E1-0x70B6FA`) restores only `VERTEXALPHA`,
`ZWRITE`, `ZTEST`. Add `CULLMODE` to the shadow contract state and restore it.
Force the shadow texture as in P0. This closes the `CULLNONE` leak into the
water/effects phase.

### P2b — Late static-shadow renderer: restore the texture raster

`0x705900` sets `TEXTURERASTER=NULL` and restores only ZTEST/blend. If the
`0x705900` camera/raster probe shows the draw targets the main screen, escalate
to a full shield (ZTEST on, blend on, texture = shadow raster) at that draw.
Otherwise, minimally restore the texture raster after the draw (hook
`0x705900` exit or `0x707CA0` exit) and gate it with
`staticShadowTextureShield` (default 1). This closes the NULL-texture leak into
the next shadow/effect consumer.

### P3 — Effects texture / colour-write shield

`RenderEffects` (`0x53EAD3`) and the corona/sprite flush already repair the
alpha block (`effectsForceAlphaBlock`, `SpriteFlushIndexedDraw_hook`), but they
do not save/restore the texture raster or colour-write mask. Add those to the
effects entry/exit (same pattern as P1), gated by `int effectsTextureShield;`
default 0 until the A/B is clean.

### P4 — Low-priority shields (only if P0–P3 do not remove the quad)

| Pass | Hook | Gate | Force |
|---|---|---|---|
| `RenderWater` second call `0x6EF650` | `InterceptCall` at `0x53E142` | `waterShield` (0) | save/restore texture, blend, ZWRITE; do not change water shader state |
| `CSkidmarks::Render` `0x720640` | entry `InjectHook` | `skidmarkShield` (0) | blend on, ZWRITE off, ZTEST on, texture = skidmark raster |
| `CPlantMgr::Render` `0x5DBAE0` | entry `InjectHook` | `grassShield` (0) | alpha test on, blend off, ZWRITE on |
| `CPostEffects::Render` | already hooked | none | no action; fullscreen, not a polygon |

### P5 — Logging hygiene

- Replace the `0x53E0C3` `MISMATCH` text with
  `owner=SilentPatch wrapper=68E8DD00` when the target is outside the exe and
  inside the SilentPatch range. The current message reads like a corrupt exe and
  has already caused a wrong "different build" conclusion in the baseline doc.
- Add `[StoredShadowShield]` one-shot lines for pre-entry and post-restore.
- Add a `[PedTex]` one-shot line with the texture raster at ped entry/exit.

---

## 7. Next experiment (minimal, decisive)

1. **Rebuild** the current tree and confirm the new install receipt:
   ```
   [shadow_install] 0x708300 entry bytes=A1 24 7B C9 00 OK
   [shadow_install] 0x708300 entry hook installed (tramp=...)
   ```
   The log should now have a `[shadow_contract] stored ...` line, unlike the
   0-stored-line run.
2. **A/B matrix** (one variable at a time, same camera/angle as the artifact):

   | `storedShadowContract` | `pedTextureShield` | Expected |
   |---|---|---|
   | 0 | 0 | reproduce the flat quad (baseline) |
   | 1 | 0 | quad disappears → stored pass is the occluder |
   | 0 | 1 | quad disappears → ped texture leak is the donor |
   | 1 | 1 | clean; keep both gates on |
   | 0 | 0, `pedCullMode=1` | whole-ped disappearance changes → culling component exists |

Also toggle `staticShadowTextureShield` (P2b) independently: if `=1` removes
the quad while `storedShadowContract=0`, the late static renderer's
NULL-texture leak is the donor.

3. **Probe the draw** (P0b) at `0x734EA0`/`0x734E90`: log the buffer pointers
   and state. If the shadow buffer is drawn after `0x708300` returns, move the
   state force to that draw; the entry shield alone is then insufficient.
4. **Visual check:** the fixed shadow should be a soft, semi-transparent blob
   (road visible through it), not a `(32,40,30)` flat fill. If it becomes a
   soft blob but the ped is still cut, the occluder is a different pass; move to
   P2/P3.

**What would flip the verdict**

- A `[Cull]`/frustum counter showing a ped rejected at the same angle, with the
  whole ped missing and no flat-color region → "both per-angle" or "not drawn".
- A stored-shadow draw probe showing the shadow buffer drawn **before** peds
  (impossible given the call order) or with a correct texture and blend →
  stored shadow is exonerated.
- A `0x705900` camera/raster probe showing the late static-shadow draw targets
  the main camera → the static renderer becomes the #1 occluder (ZTEST off,
  blend off, texture NULL, after peds).
- A texture/colour-write probe showing the flat color is not the stored vertex
  color → re-rank toward glass/particles or water.

---

## Appendix A — addresses and file offsets

| VA | File offset | Bytes / meaning |
|---:|---:|---|
| `0x53DF40` | `0x13D340` | `CScene::Render` entry (`d9 05 2c 13 c8 00 ...`) |
| `0x53DDE4` | `0x13D1E4` | `E8 17 A5 1C 00` → `0x708300` (second stored caller, mirror block) |
| `0x53E0BE` | `0x13D4BE` | `E8 7D 9E 1C 00` → `0x707F40` |
| `0x53E0C3` | `0x13D4C3` | `E8 38 A2 1C 00` → `0x708300` |
| `0x53E0C8` | `0x13D4C8` | `E8 93 C8 1C 00` → `0x70A960` |
| `0x706AB0` | `0x305EB0` | static-shadow render loop (16 slots), called at `0x53EA0D` after peds |
| `0x707F40` | `0x307340` | `CShadows::RenderStaticShadows` (cleanup loop, no draws) |
| `0x708300` | `0x307700` | `CShadows::RenderStoredShadows` (state + buffer build) |
| `0x70A960` | `0x309D60` | `CShadows::RenderExtraPlayerShadows` (state + buffer build) |
| `0x707460` | `0x307060` | per-shadow `SRCBLEND`/`DESTBLEND` switch |
| `0x707850` | `0x307450` | project stored corners to screen |
| `0x7077A0` | `0x3073A0` | rebase index buffer |
| `0x708290` | `0x307690` | reserve shadow Im2D buffer (`0xC4B958`/`0xC4D958`) |
| `0x705900` | `0x305500` | static shadow offscreen volume draw (`ZTEST=0`, `blend=0`, `texture=NULL`) |
| `0x734E90` | `0x334A90` | `RwIm2DRenderPrimitive` thunk |
| `0x734EA0` | `0x334AA0` | `RwIm2DRenderIndexedPrimitive` thunk (`jmp 0x403927`) |
| `0x70CF20` | `0x30CB20` | sprite Im2D flush (uses `0xC5F958`/`0xC4B958`) |
| `0x6EF650` | `0x2EF250` | `RenderWater` (called at `0x53E004` and `0x53E142`) |
| `0x720640` | `0x31F840` | `CSkidmarks::Render` (called at `0x53E175`) |
| `0x53E170` | `0x13D470` | `RenderEffects` entry |
| `0x53EAD3` | `0x13DED3` | `RenderEffects` call site in `CGame::Render` |
| `0x53E227` | `0x13D627` | `CPostEffects::Render` call site |
| `0x53EBE9` | `0x13DFE9` | `DrawFinalEffects` hook |
| `0x100165A3` | `0x159A3` | SilentPatch init: rewrite `0x53E0C3` |
| `0x1000DD00` | `0xD100` | SilentPatch stored-shadow wrapper (RVA `0xDD00`) |
| `0x100679BC` | BSS | SilentPatch's saved original `0x708300` pointer |

PE mapping: `.text` VA `0x401000`, raw `0x400`; file offset = `VA - 0x400C00`.
SilentPatch: `.text` VA `0x10001000`, raw `0x400`; file offset = `VA - 0x10000C00`.

## Appendix B — RW state ID map used above

From `external/d3d9/rwplcore.h:5066-5218`:

| ID | State |
|---:|---|
| 1 | `TEXTURERASTER` |
| 2 | `TEXTUREADDRESS` |
| 6 | `ZTESTENABLE` |
| 8 | `ZWRITEENABLE` |
| 9 | `TEXTUREFILTER` |
| 10 | `SRCBLEND` |
| 11 | `DESTBLEND` |
| 12 | `VERTEXALPHAENABLE` |
| 14 | `FOGENABLE` |
| 20 | `CULLMODE` |
| 29 | `ALPHATESTFUNCTION` |
| 30 | `ALPHATESTFUNCTIONREF` |

`rwALPHATESTFUNCTIONGREATEREQUAL = 7`; `rwALPHATESTFUNCTIONALWAYS = 8`;
`rwCULLMODECULLNONE = 1`, `rwCULLMODECULLBACK = 2`,
`rwCULLMODECULLFRONT = 3`.
