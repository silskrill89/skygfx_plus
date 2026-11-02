# Near-camera black peds + black shadow-blob sprites — diagnosis and fix

Date: 2026-10-24
Scope: `src/core/main.cpp`, `src/skygfx.h` (no changes to pipes, shaders, postfx,
texalpha, depthhook).

Two independent defects were reported:

1. **Near-camera peds render flat black**, distant peds look normal.
2. **Shadow blobs render as hard black sprites / acne / smears** over the player.

Both are render-state problems, not shading problems. This document records the
proven root causes with log evidence, the exact changes, the shadow call sites
that were byte-verified / installed / skipped, the expected log signatures, and
the residual stencil hypothesis.

---

## A. Near-camera black peds

### Root cause

`myDefaultCallback` (skin pipe path) overrides `pAmbient->color` with
`GetTimecycleAmbientPBR()` for **every** skin-pipe atomic:

```
[Ped] pAmbient override: (0.076,0.043,0.043)
[PBR-Ambient] raw=(0.033,0.000,0.000) floor=0.050
[Tonemap] amb=(0.033,0.000,0.000) dir=(0.039,0.000,0.000)
```

At hour 12 the timecycle ambient is ~0.03, and two of the three channels are
0.000. Vanilla skin-pipe peds do **not** run through Forward+ or the PBR IBL
path — they are lit only by whatever `pAmbient` holds. Distant peds look fine
because their baked vertex prelight survives; near peds (default pipeline draw,
no baked prelight) get ~0.03 ambient and render flat black.

The old code also *replaced* the game's own ambient instead of never going below
it, so the override could be darker than the vanilla value.

### Fix (`src/core/main.cpp:1587-1638`)

1. `base = component-wise max(savedAmb, tcAmbient)` — never darker than the
   game's own ambient.
2. Hue-preserving peak-channel floor:
   * `m = max(base.r, max(base.g, base.b))`
   * `floorV = config ? config->pedAmbientFloor : 0.20f`, clamped `[0..1]`
   * if `m < floorV && m > 1e-6f` → scale all channels by `floorV / m`
   * if `m <= 1e-6f` → set all three channels to `floorV`
3. One-shot comparison receipt `[Ped] ped_amb_cmp` (plain `static bool`, not
   throttle-driven) prints pre/tc/post/floor.
4. The existing `[Ped] pAmbient override` line now prints the **final** values,
   not the raw timecycle value.

With the logged values above the result is
`pre=(0.076,0.043,0.043) tc=(0.033,0.000,0.000) post=(0.200,0.113,0.113)
floor=0.200` — the warm hue is preserved by scaling, instead of a gray wash.

The previous cutscene floor block (0.15 per channel) is untouched and runs
first; the new floor then applies on top of it.

### Config key

| Key | Type | Default | Range | Where |
|-----|------|---------|-------|-------|
| `SkyGfx.pedAmbientFloor` | float | `0.20` | clamped `[0..1]`, `0` = off | `src/skygfx.h:714`, parsed `src/core/main.cpp:5337-5342` |

Wired in all three standard places:
* `ADD_IF_MISSING("SkyGfx", "pedAmbientFloor", "0.20")` — `main.cpp:5795`
* `cfg.set("SkyGfx", "pedAmbientFloor", ...)` — `main.cpp:6253`
* parse + clamp in `readIniFile` — `main.cpp:5336-5342`

`readIniFile` is shared by startup load, named-config load and `refreshIni`, so
every path sees the clamped value.

---

## B. Black shadow blobs — L1-only shadow state

### Root cause

`CShadows::RenderStaticShadows` (0x707F40), `CShadows::RenderStoredShadows`
(0x708300) and `CShadows::RenderExtraPlayerShadows` (0x70A960) set their render
state through `RwRenderStateSet` (**layer 1, the rw cache**) only. On this build
the raw device frequently disagrees with the caches (postfx raw writes,
DepthHook raw `ZENABLE`, Im2D passes). Live log evidence of the drift:

```
[FxAlpha] ... layer drift BEFORE repair — rw(...) d3d(...) dev(...)
[PED] ZWRITE off/desynced (rw=1 dev=0)
```

When the rw cache already holds the value the game sets, RW's value-compare
cache makes the set a no-op and the device keeps the stale value. The shadow
passes therefore inherit a stale `ALPHAREF` (140/1), a stale
`DESTBLEND`/`ZTEST`, or blend OFF — producing hard black blobs, depth acne and
smears over the player.

### Disassembly method (byte-verified)

The exe at `E:\games\gtasa_skygfx_plus\gta_sa.exe` was parsed as a PE:

* `ImageBase = 0x00400000`
* first `.text`: RVA `0x1000`, raw `0x400`
* **file offset = VA - 0x400000 + 0x400** (since RVA 0x1000 ↔ raw 0x400)

Caller bytes around the shadow block (file offsets 0x13D4B0..0x13D4D5):

```
0053E0B0  2B 00 8B 15 3C 70 C1 00 52 E8 D2 00 2B 00 E8 7D
0053E0C0  9E 1C 00 E8 38 A2 1C 00 E8 93 C8 1C 00 A1 3C 70
0053E0D0  C1 00 50 E8 A8 00 2B 00
```

Decoded (rel32 target = site + 5 + rel32):

| Site | File off | Opcode | rel32 | Extracted target | Callee | Previous instr |
|------|----------|--------|-------|------------------|--------|----------------|
| 0x53E0BE | 0x13D4BE | `E8` | 0x001C9E7D | 0x707F40 | CShadows::RenderStaticShadows | `E8` at 0x53E0B9 (call) |
| 0x53E0C3 | 0x13D4C3 | `E8` | 0x001CA238 | 0x708300 | CShadows::RenderStoredShadows | `E8` at 0x53E0BE (call) |
| 0x53E0C8 | 0x13D4C8 | `E8` | 0x001CC893 | 0x70A960 | CShadows::RenderExtraPlayerShadows | `E8` at 0x53E0C3 (call) |

Zero-arg qualification: the byte 5 bytes before each site is `0xE8`, i.e. the
immediately preceding instruction is the previous `call`. No `push` can sit
between a call and this site, so the callee receives no arguments. All three
sites therefore qualify.

Note: the orchestrator's expected-target list mentioned 0x707460. The actual
first call site targets **0x707F40** (byte-verified above), not 0x707460 — see
next section.

### Sites inspected and SKIPPED

| Site | Reason |
|------|--------|
| 0x707460 (function entry) | Takes one int argument: `0F B6 44 24 04` = `movzx eax,[esp+4]`, then `dec eax; cmp eax,7; ja ...; jmp [eax*4+0x7074CC]` (switch helper). Not a zero-arg pass. |
| 0x7083A4 | Internal call to 0x707460; `50` (`push eax`) immediately precedes the call → argument pushed. Not intercepted. |
| 0x70AA2A | Internal call to 0x707460; `52` (`push edx`) immediately precedes the call → argument pushed. Not intercepted. |

The same three zero-arg callees are also called earlier in the same caller at
0x53DDD6 / 0x53DDE4 / 0x53DDF2 (sky/reflection phase). They are structurally
identical and safe, but were **not** in the named scope of this task and were
left untouched; the main shadow block is covered.

### Wrapper behaviour (`src/core/main.cpp:1017-1259`)

`ShadowContractState` is a plain POD struct (scalars only) — C2712-safe, no
`__try`, no C++ objects, so it is safe to call from the hooked game call.

For each of the three installed sites:

1. **Capture** the pre-entry state for `ALPHABLENDENABLE / SRCBLEND / DESTBLEND
   / ZENABLE / ZFUNC / ZWRITE / ALPHATESTENABLE / ALPHAFUNC / ALPHAREF /
   FOGENABLE` from all three layers:
   * L1 `RwRenderStateGet` (rw cache; `ALPHATESTENABLE`/`ZFUNC` have no rw twin)
   * L2 `RwD3D9GetRenderState` (RW driver cache)
   * L3 raw `IDirect3DDevice9::GetRenderState`
   Every out-param is pre-seeded with the canonical shadow value so a failed
   `Get` can never push garbage on restore.
2. **Log drift** (throttle tag `shadow_contract`, only when a layer pair
   disagrees on blend/src/dst/ref/ztest/zfunc).
3. **Force** the canonical shadow state through L1 + L2 + raw L3:
   * blend ON
   * `SRCBLEND = 5` (`SRCALPHA`), `DESTBLEND = 6` (`INVSRCALPHA`)
   * `ZENABLE = TRUE`, `ZFUNC = LESSEQUAL`, `ZWRITE = FALSE`
   * `ALPHATEST = TRUE`, `ALPHAFUNC = GREATEREQUAL`, `ALPHAREF = 0`
   * `FOGENABLE = FALSE`
4. **Call the original** through the saved pointer
   (`CShadows__Render*_orig`, filled by `InterceptCall`).
5. **Restore** each layer's own captured value (L1, L2, L3 separately).

### Install block (`src/core/main.cpp:6808-6857`)

`DllMain` → `InjectDelayedPatches()` installs each site only when the bytes
verify:

* opcode at site == `E8`
* extracted target == expected target
* byte at `site - 5` == `E8` (previous instruction is a call ⇒ zero-arg)

One-shot log per site:

```
[shadow_install] 0x53E0BE op=E8 orig=707F40 OK zeroarg=1 site=CShadows::RenderStaticShadows (expect op=E8 orig=707F40)
[shadow_install] 0x53E0C3 op=E8 orig=708300 OK zeroarg=1 site=CShadows::RenderStoredShadows (expect op=E8 orig=708300)
[shadow_install] 0x53E0C8 op=E8 orig=70A960 OK zeroarg=1 site=CShadows::RenderExtraPlayerShadows (expect op=E8 orig=70A960)
```

On `MISMATCH` the site is skipped (vanilla path kept) and a `SKIPPED — vanilla
path kept` line is logged; a non-1.0 exe can never have unrelated code
rel32-patched.

---

## Exact edits

| File | Lines | Change |
|------|-------|--------|
| `src/skygfx.h` | 704-714 | new `float pedAmbientFloor;` field next to `pbrAmbientFloor` |
| `src/core/main.cpp` | 1017-1259 | shadow state contract: `ShadowContractState`, capture/force/restore, three wrappers |
| `src/core/main.cpp` | 1587-1638 | hue-preserving `pAmbient` floor + `ped_amb_cmp` one-shot |
| `src/core/main.cpp` | 5337-5342 | `pedAmbientFloor` parse (`readfloat`, default 0.20f, clamp [0..1]) |
| `src/core/main.cpp` | 5795 | `ADD_IF_MISSING("SkyGfx", "pedAmbientFloor", "0.20")` |
| `src/core/main.cpp` | 6253 | `saveConfigTo` write-back |
| `src/core/main.cpp` | 6808-6857 | byte-verified install of the three shadow call sites |

No build was run (per task instruction).

---

## Expected log signatures

Ped ambient (first skin-pipe override of the run, one shot):

```
[Ped] ped_amb_cmp pre=(0.076,0.043,0.043) tc=(0.033,0.000,0.000) post=(0.200,0.113,0.113) floor=0.200
[Ped] pAmbient override: (0.200,0.113,0.113)
```

`post` should never be darker than `pre` on any channel, and the ratio between
channels should match `pre`/`tc` (hue preserved). At night with a true black
ambient, `post` is `(floorV,floorV,floorV)`.

Shadow install (one shot each):

```
[shadow_install] 0x53E0BE op=E8 orig=707F40 OK zeroarg=1 ...
[shadow_install] 0x53E0C3 op=E8 orig=708300 OK zeroarg=1 ...
[shadow_install] 0x53E0C8 op=E8 orig=70A960 OK zeroarg=1 ...
```

Shadow state drift (throttled, only when layers disagree):

```
[shadow_contract] stored pre-entry layer drift: rw(blend=.. src=.. dst=.. ref=.. ztest=.. zw=.. fog=..) drv(...) dev(...)
```

---

## Residual hypothesis — INTZ has no usable stencil

`src/render/depthhook.cpp` substitutes a `D3DFMT_INTZ` texture as the scene
depth-stencil (see `CreateINTZResources`). INTZ is a depth-only, sampleable
format: it has **no stencil bits**. Vanilla SA's shadow passes use the stencil
buffer (shadow volume / stencil-marked blobs), so with INTZ bound those passes
have no usable stencil regardless of how clean the blend/depth render state is.

If black blobs persist **after** the log shows three `[shadow_install] ... OK`
lines and `[shadow_contract]` reports a clean (matching) pre-entry state, the
next test is:

1. Create the depth-stencil as `D3DFMT_D24S8` (stencil usable by the shadow
   passes).
2. Keep a **separate** `D3DFMT_INTZ` texture for shader sampling, written via
   `StretchRect` / depth resolve as needed, so the INTZ consumers (velocity
   buffer, SSAO, SMAA motion/depth edge, water depth copy) keep working.

That is a depthhook change and was deliberately **not** made in this patch
(out of write scope).

---

## Verification performed

* Byte verification of every written address (PE parse + extracted rel32,
  logged at install time).
* Brace balance: `{}` delta 0 in both edited files; paren balance of every
  inserted region delta 0.
* CRLF preserved (`main.cpp` 7133 CRLF / 7133 LF; `skygfx.h` 1452/1452).
* All new locals are POD; the shadow wrappers contain no `__try` and no C++
  objects (C2712-safe).
* `git diff --check`: no new whitespace errors in `src/core/main.cpp` or
  `src/skygfx.h` (repo has pre-existing warnings in unrelated build files).
