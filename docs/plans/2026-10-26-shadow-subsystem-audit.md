# Shadow Subsystem Audit — 2026-10-26

**Build:** 2,395,136 (2026-10-26 01:11:06)
**Run log:** `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, snapshot = first **30,661** lines,
01:12:26.118 → 01:19:31.145.
**Config:** Xbox mode, `fixShadows=0`, `pedShadows=-1`, `stencilShadows=-1`, all postfx off.
**Method:** source read of `src/core/main.cpp`; static disassembly of
`E:\games\gtasa_skygfx_plus\gta_sa.exe` (PE `.text` VA `0x401000`, raw `0x400`,
file offset = VA − `0x400C00`); log analysis of the 30,661-line prefix only. The
file kept growing after 01:19:31 (37,354 lines by 01:21:10); later lines are called
out explicitly. No source files were modified.

> **Snapshot integrity:** the stated counts match the prefix exactly:
> `[shadow_contract]` 110, `[ShadowHeal]` 1, `[shadow_install]` 4, `ZWRITE` 91,
> `[PED]` 77, `[BUILDING]` 66, `[VEHICLE]` 17. The stored-exit receipt
> (`stored exit healed=1`) is at **01:20:41.121**, after the stated run.

---

## 0. Executive summary

1. **The static shield wraps the wrong function.** `0x707F40` is the 0x60-byte
   cleanup/unlink loop. It contains **no draw and no render-state call**. The
   real static volume render is `0x706AB0` at `0x53EA0D`; it is **not hooked**.
2. **The stored and extra hooks do cover their own draws.** Both functions call
   `RwIm3DTransform` → `RwIm3DRenderIndexedPrimitive` → `RwIm3DEnd` internally
   (stored: `0x708453`/`0x70846C`/`0x708474` and `0x708638`/`0x708652`/`0x70865A`;
   extra: `0x70B689`/`0x70B6A2`/`0x70B6AA`). The prior "no draw call / deferred
   flush" conclusion is **wrong**. The entry shield does sit around the draw.
3. **The contract never heals the RW driver cache (L2).** `shadowContractDrift`
   compares L1 only to L3; `shadowContractRestore` writes L2 back from its own
   captured value. In 39/106 pre-entry receipts L2 disagrees with L1; in 3
   `drift=0` static receipts the layer split is still present. The contract
   reports "clean" while preserving the split.
4. **The contract treats a polluted L1 cache as truth.** 46/110 receipts carry
   `ref=100` in **all three layers**, and 36 carry `rw blend=0` while L2 says
   blend=1. Heal-to-L1 therefore canonicalises the leak instead of removing it.
5. **All shadow draws write stage-0 texture raster; none of the game paths
   restores it, and the contract does not capture it.** Stored/extra bind the
   per-slot shadow texture; the static-volume draw at `0x705790` binds **NULL**;
   the auxiliary draw at `0x705660` also binds **NULL**. After the static pass,
   the next consumer that does not set stage 0 inherits NULL.
6. **The one-shot `[ShadowHeal]` did not pin the values it printed.** The printed
   dev and rw values are identical (`ref=0 blend=0 zw=1 fog=1`). The actual
   repaired state was `ALPHAFUNC` (SilentPatch ALWAYS → rw GREATEREQUAL), which
   the receipt does not print.
7. **Decisive answer:** the **ring** is the unshielded static-volume polyline at
   `0x7058BC` (`primType=2` = `rwPRIMTYPEPOLYLINE`, 4 vertices / 5 indices = a
   closed line loop), with `ZTEST=0`, blend OFF, `TEXTURE=NULL`. The **disc** is
   the textured stored/extra shadow quad (`0x708300`/`0x70A960`). The `0x707F40`
   cleanup draws neither. Opacity is a state problem (blend / alpha / texture /
   colour-write), and the contract has no draw-time probe to name it.

---

## 1. Every shadow draw path we touch or should touch

Addresses are VAs in the live `gta_sa.exe`. "Texture stage 0" means
`rwRENDERSTATETEXTURERASTER` (state 1).

| # | Entry / call site | Real role | Texture stage 0 | Blend / ZTEST / ZWRITE / alpha | Restores? | Shielded? |
|---|---|---|---|---|---|---|
| 1 | `CShadows::RenderStaticShadows` `0x707F40` at `0x53E0BE` | cleanup/unlink of 48 entries, `0xC4A034..0xC4AC34`; **no draw, no state** | none | none set | n/a | Wrapped by `shadowContractRun("static")` — around a no-op |
| 2 | `CShadows::RenderStoredShadows` `0x708300`, function entry | fills `0xC4D958` / `0xC4B958` and **draws** batches | per-slot `[[slot+0x2C]]` texture→raster at `0x7083A9-0x7083B6` | ZWRITE=0 (`0x70831D`), ZTEST=1 (`0x70832A`), VERTEXALPHA=1 (`0x708337`), FOG=0 (`0x708342`), ALPHAREF=0 (`0x70834E`), FILTER=LINEAR (`0x70835B`); ALPHATESTENABLE/ALPHAFUNC inherited (SilentPatch forces ALWAYS); SRC/DST per type via `0x707460` (5/6 default) | exit: VERTEXALPHA=0, ZWRITE=1 only (`0x708687`, `0x708693`); **no** texture/ZTEST/FOG/ref/blend-factor restore | Entry PATCH_JUMP at `0x708300`; full function incl. draws covered |
| 3 | `CShadows::RenderExtraPlayerShadows` `0x70A960` at `0x53E0C8` | fills the same shared buffers and **draws** | per-slot `[[0xC40454+idx]]` texture→raster at `0x70AA2F-0x70AA3C` | ZWRITE=0, ZTEST=1, VERTEXALPHA=1, FOG=0 (`0x70A977-0x70A9D0`), TEXTUREADDRESS=3/CLAMP (`0x70A9B4`), CULLMODE=NONE (`0x70A9C1`), ALPHAREF=0 (`0x70A9CE`), FILTER=LINEAR (`0x70A9DD`); SRC/DST per type via `0x707460` | exit: VERTEXALPHA=0, ZWRITE=1, ZTEST=1, address=WRAP, CULL=BACK (`0x70B6D8-0x70B713`); **no** texture/FOG/ref/blend-factor restore; CULL restored to BACK, not the pre-entry mode | Call-site intercept; full function incl. draws covered |
| 4 | `0x706AB0` at `0x53EA0D` → `0x707CA0` → `0x705A00` → **`0x705790` draw at `0x7058BC`** | real static volume rasterisation, 16 slots (`ecx=0xC40350`) | **NULL** at `0x7058A9` | ZTEST=0 (`0x705890`), VERTEXALPHA=0/blend OFF (`0x70589C`), ZWRITE inherited, ALPHATEST/FUNC inherited; draw `RwIm2DRenderIndexedPrimitive(primType=2 /* rwPRIMTYPEPOLYLINE */, verts, 4, indices, 5)` at `0x7058BC` — a closed line loop (ring/outline), not a filled quad; camera bracketed by `RwCameraBeginUpdate`/`EndUpdate` (`0x705879`/`0x7058DD`) | ZTEST=1, VERTEXALPHA=1 hardcoded (`0x7058C8`/`0x7058D7`); **texture never restored** | **No** |
| 5 | `0x705660`, called from `0x705C57` / `0x705D02` / `0x705F21` (auxiliary shadow-camera draw) | 4-vertex draw `RwIm2DRenderPrimitive` at `0x70573E` | **NULL** at `0x70570C` | ZTEST=0 (`0x705700`), VERTEXALPHA=1, SRC=INVDESTCOLOR (10) / DST=ZERO (1) (`0x705719-0x705732`); draw `0x734E90` | ZTEST=1, SRC=5, DST=6 (`0x705749-0x705769`); **texture/blend-enable/ZWRITE not restored** | **No** |

### 1.1 Correction to the previously assumed static path

`0x705900` is **not** the rasteriser. It is a projection/math helper called from
`0x707CA0` at `0x707E4F`; it contains no render-state or draw call. The draw is
`0x705790` (reached via `0x705A00`, which is a `jmp 0x705790`), and its actual
draw call is at `0x7058BC` (`call 0x734EA0`). The live path is:

```
0x53EA0D  call 0x706AB0          ; 16-slot outer loop
0x706B29  call 0x706600          ; shadow camera/raster setup
0x706B68  call 0x707CA0          ; per-slot projection
0x707DE2  call 0x7059F0          ; slot/vertex helper
0x707E04  call 0x705A00          ; jmp 0x705790
0x7058BC  call 0x734EA0          ; RwIm2DRenderIndexedPrimitive
```

The previously assumed `0x707CA0 → 0x705900` edge does exist, but it is the math
helper, not the draw.

---

## 2. The contract in `src/core/main.cpp`

Line numbers are current source.

| Function | Lines | What it does |
|---|---|---|
| `ShadowContractState` | 1240-1249 | POD capture of L1 (rw), L2 (RW driver), L3 (raw device): blend, src, dst, ztest/zfunc, zwrite, alphatest/afunc/aref, fog |
| `shadowContractCapture` | 1251-1323 | Pre-seeds canonical shadow values (blend on, SRCALPHA/INVSRCALPHA, ztest on, zwrite off, GREATEREQUAL@0, fog off), then `RwRenderStateGet` / `RwD3D9GetRenderState` / `device->GetRenderState` for each layer. Sanitises out-of-range values |
| `shadowContractDrift` | 1330-1341 | Counts only **L3 vs L1**: blend, src, dst, zwrite, ztest, alpha func, alpha ref, fog. **Does not count L2 at all.** ZFUNC/ALPHATESTENABLE have no rw twin and are never counted |
| `shadowContractForce` | 1345-1382 | Forces canonical values through **L1 + L2 + L3**: ALPHABLENDENABLE=TRUE, SRCALPHA/INVSRCALPHA, ZENABLE=TRUE, ZFUNC=LESSEQUAL, ZWRITE=FALSE, ALPHATESTENABLE=TRUE, ALPHAFUNC=GREATEREQUAL, ALPHAREF=0, FOGENABLE=FALSE |
| `shadowContractRestore` | 1386-1465 | Restores L1 from capture; L2 from its **own** capture; L3 verbatim if `drift==0`, else heals L3 from L1 for blend/src/dst/zw/ztest/afunc/aref/fog, keeps captured device ZFUNC and ALPHATESTENABLE. One-shot `[ShadowHeal]` at 1456-1463 |
| `shadowContractRun` | 1475-1517 | Capture → drift/divergence receipt (`shadow_contract`) → force → `orig()` → restore → exit receipt (`shadow_contract_exit`) |
| Static hook | 1519-1523 | `shadowContractRun(orig, "static")` |
| Stored hook | 1525-1529 | `shadowContractRun(orig, "stored")` |
| Extra hook | 1531-1535 | `shadowContractRun(orig, "extra")` |
| Install | 7771-7877 | Two call-site intercepts (`0x53E0BE`, `0x53E0C8`) byte-verified E8 + target + previous-E8; stored `0x708300` verifies `A1 24 7B C9 00`, copies 5 bytes to an RWX trampoline, `InjectHook(PATCH_JUMP)` |

### 2.1 What is captured, forced, healed

**Captured/forced/healed:** ALPHABLENDENABLE, SRCBLEND, DESTBLEND, ZENABLE/ZTEST,
ZFUNC (L2/L3 only), ZWRITE, ALPHATESTENABLE (L2/L3 only), ALPHAFUNC, ALPHAREF,
FOGENABLE. The canonical shadow state is blend ON, SRCALPHA/INVSRCALPHA, ztest ON,
zfunc LESSEQUAL, zwrite OFF, alpha test ON, GREATEREQUAL@0, fog OFF.

**Healed:** only L3, only when `drift>0`; L1 is the truth. L2 is restored to its
own captured value, never reconciled. The four printed values in `[ShadowHeal]`
are ref/blend/zw/fog; ZFUNC/ALPHATESTENABLE are explicitly not healed.

**NOT captured / NOT forced / NOT restored:**

- `rwRENDERSTATETEXTURERASTER` / `D3DRS_TEXTURE*` / stage-0 sampler state.
- `CULLMODE` (the extra pass sets NONE and restores BACK hardcoded).
- `COLORWRITEENABLE`, `STENCILENABLE`, `STENCILFUNC`, `STENCILMASK`, `STENCILWRITEMASK`.
- `BLENDOP` / `SEPARATEALPHABLENDENABLE` (only the blend pair is forced, not the op).
- `TEXTUREADDRESS`, `TEXTUREFILTER`, `TEXTUREFACTOR`.
- Shader/vertex-declaration/stream binding.
- The real static draw (`0x706AB0`) and auxiliary `0x705660`.

### 2.2 Coverage verdict

- `0x707F40` "static": wrapped, but the function draws nothing. The shield runs
  its force/restore around a cleanup loop.
- `0x708300` "stored": wrapped at function entry; its internal Im3D draws are
  inside the force/restore window. Coverage is good for the states it knows.
- `0x70A960` "extra": wrapped at the call site; its internal Im3D draws are
  inside the window. CULLMODE and texture are not covered.
- `0x706AB0` real static volume: **not covered**.
- `0x705660` auxiliary shadow-camera draw: **not covered**.

---

## 3. Texture-raster trace (stage 0)

Every write to state 1 in the shadow block:

| Address | Path | Value | Restored? |
|---|---|---|---|
| `0x7083A9` / `0x7083B6` | stored `0x708300` per-slot | `[[slot+0x2C]]` = slot `RwTexture*` → raster | **No** |
| `0x70AA2F` / `0x70AA3C` | extra `0x70A960` per-slot | `[[0xC40454+idx]]` = per-slot `RwTexture*` → raster | **No** |
| `0x7058A9` | static volume `0x705790` | **NULL** | **No** |
| `0x70570C` | auxiliary `0x705660` | **NULL** | **No** |
| `0x707F40` | static cleanup | no state writes | n/a |

The shadow itself therefore renders against:

- **Stored/extra:** the per-slot shadow texture raster bound immediately before
  the draw. This is a real textured quad; the texture supplies the soft blob/ring
  shape and alpha.
- **Static volume `0x705790`:** **no texture**. The draw is a closed
  `rwPRIMTYPEPOLYLINE` line loop (4 vertices / 5 indices) with blend OFF and
  ZTEST OFF, so it is a solid vertex-colour **ring/outline** if it reaches the
  screen. It is bracketed by `RwCameraBeginUpdate`/`EndUpdate` on the camera
  obtained from the shadow object (`[edi]`), so it is intended to be offscreen;
  that target is **not probed**.
- **Auxiliary `0x705660`:** **no texture**, 4-vertex fill with
  SRC=INVDESTCOLOR/ZERO and ZTEST off, also inside a shadow-camera Begin/End.
  If the camera is wrong, this is a darkening quad.

**Consequence of inheriting NULL:** after the static-volume draw, stage 0 is
NULL. A later consumer that does not set its own texture will sample nothing
(white/untextured or vertex colour). After stored/extra, stage 0 is the last
shadow texture; a later consumer that does not rebind will sample the shadow
blob. The contract cannot fix either because it neither captures nor restores
state 1.

---

## 4. Log forensics (30,661-line snapshot)

### 4.1 Counts

| Tag | Count |
|---|---|
| `[shadow_contract]` | 110 (106 pre-entry + 4 exit) |
| `[ShadowHeal]` | 1 |
| `[shadow_install]` | 4 (`0x53E0BE OK`, `0x53E0C8 OK`, `0x708300 entry bytes OK`, `0x708300 hook installed`) |
| `ZWRITE` | 91 (90 entry-desync + 1 `[PedOpaque]` pre-entry line) |
| `[PED]` / `[BUILDING]` / `[VEHICLE]` | 77 / 66 / 17 |

### 4.2 Distinct `[shadow_contract]` signatures

Normalised after the timestamp. `b/s/d/r/z/w/f` = blend, src, dst, alpha-ref,
ztest, zwrite, fog; `zf` = zfunc. Counts are exact for the snapshot.

```
33  static  drift=1  rw(b=0 s=5 d=6 r=1   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=1   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1)
21  static  drift=2  rw(b=1 s=5 d=6 r=100 z=1 w=1 f=1) drv(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=100 z=1 zf=4 w=0 f=1)
19  stored  drift=1  rw(b=1 s=5 d=6 r=100 z=1 w=1 f=1) drv(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1) dev(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1)
10  static  drift=1  rw(b=1 s=5 d=6 r=0   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=0   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1)
 4  stored  drift=1  rw(b=0 s=5 d=6 r=0   z=1 w=1 f=1) drv(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1)
 4  static  drift=2  rw(b=1 s=5 d=6 r=2   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=2   z=1 zf=4 w=1 f=1) dev(b=1 s=5 d=6 r=0   z=1 zf=4 w=0 f=1)
 4  static  drift=1  rw(b=1 s=5 d=6 r=100 z=1 w=1 f=1) drv(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=100 z=1 zf=4 w=1 f=1)
 3  static  drift=1  rw(b=1 s=5 d=6 r=0   z=1 w=1 f=1) drv(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=1 f=1)
 3  static  drift=0  rw(b=0 s=5 d=6 r=1   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=1   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=1   z=1 zf=4 w=1 f=1)
 2  static  exit healed=2
 1  stored  drift=1  rw(b=1 s=5 d=6 r=2   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=2   z=1 zf=4 w=1 f=1) dev(b=1 s=5 d=6 r=2   z=1 zf=4 w=1 f=1)
 1  stored  drift=1  rw(b=0 s=5 d=6 r=1   z=1 w=1 f=1) drv(b=0 s=5 d=6 r=1   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=1   z=1 zf=4 w=1 f=1)
 1  static  drift=3  rw(b=1 s=5 d=6 r=100 z=1 w=1 f=1) drv(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=0 f=1)
 1  static  drift=2  rw(b=1 s=5 d=6 r=0   z=1 w=1 f=1) drv(b=1 s=5 d=6 r=0   z=1 zf=4 w=1 f=1) dev(b=0 s=5 d=6 r=0   z=1 zf=4 w=0 f=1)
 1  static  drift=1  rw(b=1 s=5 d=6 r=100 z=1 w=1 f=1) drv(b=1 s=5 d=6 r=100 z=1 zf=4 w=1 f=1) dev(b=1 s=5 d=6 r=100 z=1 zf=4 w=0 f=1)
 1  static  exit healed=0
 1  extra   exit healed=0
```

**Drift distribution:** 0 → 3, 1 → 76, 2 → 26, 3 → 1. **103/106 carry
`drift>0`; 3 carry `drift=0`.**

**Repeating triplets (top three):**

1. 33× static `rw(blend 0 / ref 1) drv(blend 1 / ref 1) dev(blend 0 / ref 0)` —
   L2 blend split, device ref split.
2. 21× static `rw/drv(blend 1 / ref 100 / zw 1) dev(blend 0 / ref 100 / zw 0)` —
   device blend + zwrite lost; L1/L2 agree.
3. 19× stored `rw/drv/dev(blend 1 / ref 100 / zw 1)` — displayed states agree;
   the single drift is the unprinted ALPHAFUNC.

**Layer-split statistics:** 39/106 have L1↔L2 split (all blend; src/dst/ref/ztest
never split). 77/106 have L1↔L3 split on the displayed states. 26/106 have all
three layers equal on the displayed states (25 stored + 1 static), and all of
those carry a hidden ALPHAFUNC drift. The remaining 3 static lines have L1=L3 but
L2 split.

### 4.3 `[ShadowHeal]`

One receipt, `01:13:03.013`:

```
[ShadowHeal] L3 pinned to cache: dev(ref=0 blend=0 zw=1 fog=1) -> rw(ref=0 blend=0 zw=1 fog=1)
```

The immediately preceding stored receipt (`01:13:03.011`, line 299) is
`drift=1` with **all displayed states equal**:

```
[shadow_contract] stored pre-entry layer drift: drift=1
  rw(blend=0 ... ref=0 ...)
  drv(blend=0 ... ref=0 ...)
  dev(blend=0 ... ref=0 ...)
```

Because `shadowContractDrift` counts ALPHAFUNC but the heal receipt prints only
ref/blend/zw/fog, the actual pinned state was the **alpha function**
(SilentPatch's ALWAYS → rw GREATEREQUAL), not any of the printed fields. The
heal receipt is therefore misleading. This also explains all 25 stored receipts
being `drift=1`: SilentPatch owns `0x53E0C3` and forces ALPHAFUNC=ALWAYS before
our entry hook sees the device.

### 4.4 ZWRITE entry-desync lines by `rw=/dev=` combination

| Subsystem | Line source | Combination | Count | Meaning |
|---|---|---|---:|---|
| PED | `main.cpp:1821` | `rw=1 dev=0` | 25 | **real desync**: device ZWRITE off while rw says on |
| PED | `main.cpp:1821` | `rw=0 dev=0` | 1 | consistent off (not desync) |
| BUILDING | `buildingPipe.cpp:3049` | `rw=0 drv=0 dev=0` | 46 | consistent off (not desync) |
| BUILDING | `buildingPipe.cpp:3049` | `rw=1 drv=1 dev=0` | 1 | **real desync** |
| VEHICLE | `vehiclePipe.cpp:3620` | `rw=1 cache=1 dev=0` | 17 | **real desync** |
| `[PedOpaque] pre-entry` | (different format) | `ZWRITE=0` | 1 | first-frame loading state |
| **Total** | | | **91** | matching the log count |

Only **43** of the 90 entry-desync lines are true device-vs-cache splits
(25 ped + 17 vehicle + 1 building); 47 are all-layers-off receipts that the tag
still calls "off/desynced".

### 4.5 `ref=100`

**46/110** `[shadow_contract]` lines still carry `ref=100`; first at
`01:13:39.033`, last at `01:19:29.619`. It is not a residual of the one-shot heal
(the heal fired at `01:13:03.013`, before ref=100 appears). In all 46 lines the
**rw cache itself is 100**, and in the 19 stored lines all three layers are 100.
This is a cache-level leak, prime suspect the vehicle/env-map path: the first
ref=100 static receipt is ~2.6 s before the first `[VEHICLE]` receipt
(`01:13:41.657`), and the leak persists to the end of the run. The contract's
heal-to-L1 doctrine treats this value as truth and restores it.

---

## 5. Ranked defect list

Each entry: severity — evidence — file:line — one-line fix.

1. **P0 — Static shield wraps the wrong function; the real static draw is
   unshielded.** `0x707F40` is a cleanup loop with no draw/state; `0x706AB0` at
   `0x53EA0D` is the real 16-slot render, and its draw at `0x7058BC` sets
   ZTEST=0, blend OFF, texture=NULL and uses `primType=2`
   (`rwPRIMTYPEPOLYLINE`), 4 vertices / 5 indices = a closed line loop (ring).
   Evidence: disassembly; 78 static receipts all describe a function that cannot
   draw. `src/core/main.cpp:1520-1523`, install `:7800-7801`; game code
   `0x706AB0`/`0x705790`.
   **Fix:** byte-verify and entry-hook `0x706AB0`, wrap it in
   `shadowContractRun("static_volume")`, and probe `0x705879`/`0x7058BC`.

2. **P0 — L2 (RW driver cache) is never healed.** `shadowContractDrift` compares
   only L1↔L3 (`:1330-1341`); restore writes L2 from its own capture
   (`:1399-1408`). Evidence: 39/106 receipts have L1↔L2 blend split; 3
   `drift=0` static receipts still show `rw blend=0 drv blend=1 dev blend=0`.
   **Fix:** count L1↔L2 in `shadowContractDrift` and heal L2 from the same truth
   in the `drift>0` path.

3. **P0 — L1 is treated as truth, so cache-level leaks are canonicalised.**
   The heal doctrine at `:1410-1414` and `:1436-1453` pins L3 to L1. Evidence:
   46/110 lines have `ref=100` in L1; 36 have `rw blend=0` while L2 says 1.
   **Fix:** restore from an explicit known-good baseline (or a frame-start
   snapshot), not from the current L1; separately find and fix the upstream
   ref=100/blend=0 setter.

4. **P1 — No texture-raster / cull / colour-write / stencil / blend-op coverage.**
   The state struct captures only blend/Z/alpha/fog (`:1240-1249`); force
   (`:1345-1382`) and restore (`:1386-1465`) never touch state 1, CULLMODE,
   COLORWRITEENABLE, STENCIL*, or BLENDOP. Evidence: every draw path writes
   state 1 (stored `0x7083B6`, extra `0x70AA3C`, static `0x7058A9`, auxiliary
   `0x70570C`), and none restores it; the static path binds NULL. The extra
   pass sets CULLNONE and restores BACK hardcoded (`0x70A9C1`/`0x70B713`).
   **Fix:** extend `ShadowContractState`/force/restore with texture raster, cull,
   colour-write, stencil, and BLENDOP; capture the pre-entry values.

5. **P1 — Static-volume draw restores hardcoded values, not the pre-entry
   state.** `0x705790` sets ZTEST=0, VERTEXALPHA=0, TEXTURE=NULL and restores
   ZTEST=1/VERTEXALPHA=1 (`0x7058C1-0x7058D7`); texture and ZWRITE are never
   restored. If the camera target is the scene camera, this is an opaque
   textureless **ring** (`primType=2`, 4 verts / 5 indices). `src/core/main.cpp`
   has no hook for it.
   **Fix:** include it in the shield; if offscreen is proven, add a probe and a
   texture/ZWRITE save-restore at minimum.

6. **P1 — Extra pass can leak CULLMODE=BACK.** Entry sets NONE, exit sets BACK
   (`0x70A9C1`/`0x70B713`); the contract does not capture cull. If the pre-entry
   mode was NONE or FRONT, the next consumer inherits BACK.
   **Fix:** capture/force/restore `rwRENDERSTATECULLMODE` in the contract.

7. **P1 — Shadow exit telemetry is starved.** `shadowContractRun` uses the shared
   `shadow_contract_exit` throttle (`:1515-1516`); only 4 exit receipts exist for
   106 entries, and the first `stored exit` does not appear until `01:20:41`,
   after the stated run. Heal counts cannot be inferred from the log.
   **Fix:** use a per-pass, first-hit-unthrottled exit receipt (or a dedicated
   1024-bucket map).

8. **P2 — `[ShadowHeal]` receipt hides the state it repaired.** It prints only
   ref/blend/zw/fog (`:1456-1463`); the only heal in this run repaired
   ALPHAFUNC but printed identical dev/rw values.
   **Fix:** print alpha func, zfunc, alphatest, and L2/L3 before/after.

9. **P2 — Contract is unconditional; `fixShadows=0` does not disable it.**
   Install block `:7771-7877` runs without an INI gate; `fixShadows` only patches
   `0x709B2D` etc. (`:7605-7621`). Evidence: 110 receipts in a `fixShadows=0`
   run.
   **Fix:** add a `shadowContract`/`fixShadows` gate and log the gate decision.

10. **P2 — Stored/extra leave the last shadow texture bound.** State 1 is written
    per slot and never restored (`0x7083B6`, `0x70AA3C`). A later consumer that
    assumes a different stage-0 bind samples the shadow blob. The static and
    auxiliary paths leave NULL instead.
    **Fix:** capture/restore `rwRENDERSTATETEXTURERASTER` and the raw device
    stage-0 texture in the contract.

11. **P2 — Auxiliary shadow-camera draw `0x705660` is unshielded.** It sets
    ZTEST=0, TEXTURE=NULL, VERTEXALPHA=1, SRC=INVDESTCOLOR/DST=ZERO and draws
    `RwIm2DRenderPrimitive` at `0x70573E`; it restores only ZTEST/SRC/DST.
    If its target is the main screen, it darkens.
    **Fix:** prove the target with a camera/raster probe; otherwise hook and
    save/restore its state.

12. **P2 — SilentPatch ALPHAFUNC interaction is undocumented in telemetry.**
    SilentPatch's wrapper at `0x1000DD00` forces ALPHAFUNC=ALWAYS before our
    entry hook; `shadowContractDrift` sees ALPHAFUNC=ALWAYS vs rw GREATEREQUAL
    and every stored receipt is `drift=1` (25/25).
    **Fix:** document the composition and include ALPHAFUNC in the receipt; do
    not treat this drift as evidence of the shadow blob.

---

## 6. Decisive answer: what draws the opaque/dark disc or ring?

**Yes — the shadow subsystem owns both shapes, and they come from two different
paths.**

- **The ring** is the unshielded static-volume polyline: `0x706AB0` →
  `0x707CA0` → `0x705A00` → `0x705790`, draw at **`0x7058BC`**. The call passes
  `primType=2` (`rwPRIMTYPEPOLYLINE`), 4 vertices and 5 indices — a closed line
  loop. It sets `ZTEST=0`, blend OFF and `TEXTURERASTER=NULL`; it restores
  ZTEST/blend to hardcoded TRUE and never restores the texture. If this camera is
  the scene camera, it paints a black outline ring with no alpha. This path is
  **not hooked**; the current "static" hook wraps `0x707F40`, which draws nothing.
- **The disc** is the textured stored/extra shadow quad: `0x708300` or
  `0x70A960`, both of which bind a per-slot shadow texture at stage 0 and draw
  via `RwIm3DRenderIndexedPrimitive`. The texture supplies the soft disc
  silhouette.

It is **not** the `0x707F40` pass. The static-volume path is bracketed by a
shadow-camera Begin/End update, so it is *probably* offscreen, but that is the
single most important unverified fact in this audit: a probe at `0x705879`
(RwCameraBeginUpdate argument) and `0x7058BC` (draw target) is required. Until
that probe exists, the ring cannot be ruled out, and the code path that produces
a ring-shaped primitive is sitting outside every shield.

**Why the opacity is wrong:** the contract controls only blend/Z/alpha/fog. It
does not capture stage-0 texture, cull, colour-write, stencil, or blend-op, it
never heals L2, and it treats a polluted L1 cache as truth (46/110 receipts
carry `ref=100`). The one heal in the run repaired only the unprinted ALPHAFUNC.
So a shadow can still be drawn with the wrong texture/alpha/colour mask while the
contract reports `healed`. A draw-time probe at `0x7EF550`, `0x734EA0`, and
`0x7058BC` is the missing evidence.

**Short verdict:** the ring is the **unshielded `0x7058BC` polyline**; the disc is
the **textured stored/extra quad**. Both are shadow passes. The opacity is a
state-coverage failure, not a building/ped material problem.

---

## Appendix A — Disassembly receipts

```
0x707F40  push ebx; mov ecx,0xC4A034; ... loop; ret        ; no RwRenderStateSet, no draw
0x708300  mov eax,[0xC97B24]; ... state 8=0, 6=1, 0xC=1, 0xE=0, 0x1E=0, 9=2
0x7083A9  mov ecx,[edi-6]; mov edx,[ecx]; push edx; push 1; call [eax+0x20]  ; TEXTURERASTER
0x708453  RwIm3DTransform(0xC4D958, n, 0, 1)
0x70846C  RwIm3DRenderIndexedPrimitive(3, 0xC4B958, n)
0x708474  RwIm3DEnd()
0x708638/0x708652/0x70865A  final Im3D flush
0x70A960  state 8=0, 6=1, 0xC=1, 0xE=0, 2=3, 0x14=1, 0x1E=0, 9=2
0x70AA2F  mov eax,[esi]; mov ecx,[eax]; push ecx; push 1; call [edx+0x20]  ; TEXTURERASTER
0x70B689/0x70B6A2/0x70B6AA  Im3D flush
0x70B6D8  state 0xC=0, 8=1, 6=1, 2=1, 0x14=2
0x706AB0  16-slot loop; call 0x707CA0 at 0x706B68
0x707CA0  call 0x7059F0; call 0x705A00; call 0x705900 (math); call 0x7076C0; call 0x707390
0x705A00  jmp 0x705790
0x705790  state 6=0, 0xC=0, 1=0; call 0x734EA0 at 0x7058BC; state 6=1, 0xC=1
0x705660  state 6=0, 1=0, 0xC=1, 0xA=10, 0xB=1; call 0x734E90 at 0x70573E; state 6=1, 0xA=5, 0xB=6
```

`0x53EA0D: E8 9E 80 1C 00` → `0x706AB0`; `0x706B68: E8 33 11 00 00` →
`0x707CA0`. `0x705900` is called at `0x707E4F`, not from the draw point.
