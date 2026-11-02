# Vanilla Skin/Ped Pipeline — Render-State RE

**Date:** 2026-10-25  
**Repo:** `E:\dev(dave)\skygfx_plus_expIV`  
**Target:** `E:\games\gtasa_skygfx_plus\gta_sa.exe` (GTA SA v1.0 US, image base `0x400000`)  
**Method:** Python 3.10.11 + capstone 5.0.1; PE section map read directly; ranges disassembled from the exe; every claim below is tied to file bytes.  
**READ-ONLY outside this file.**

---

## 0. TL;DR

1. `0x7491C0` is `AtomicDefaultRenderCallBack`, not the skin renderer. It only picks the atomic pipeline and calls `RxPipelineExecute` (`0x805710`). It contains **zero** render-state writes.
2. The ped path is the RW D3D9 skin pipeline: node definition `0x8DED08` (name `nodeD3D9SkinAtomicAllInOne.csl`), node body `0x7C7B90`, pipeline-callback installer `0x7CB240`. The render helpers are `0x7C8060` / `0x7C85B0`, the skin instance callback is `0x7CB190`, and the big skin mesh renderer is `0x761170`.
3. **The vanilla skin path sets only a handful of render states itself:** `D3DRS_CLIPPING=TRUE` (`0x7C80B7`), `D3DRS_NORMALIZENORMALS` 0/1 (`0x7C86C6`), `D3DRS_TEXTUREFACTOR` (`0x756F1B`/`0x757144`, FFP path), `D3DRS_DITHERENABLE`/`D3DRS_SHADEMODE` (FFP path, `0x756F23`/`0x756F2C`), and the per-material **alpha-test/blend pair** through the rw vertex-alpha handler `0x7FE0A0` (called from `0x756F0F` in the FFP path and `0x7570F1` in the VS path). It **does not set** CULLMODE, ZWRITE, ZENABLE, ZFUNC, ALPHAFUNC, ALPHAREF, SRCBLEND or DESTBLEND.
4. The **only caller-supplied alpha state** is `ALPHAREF=1`, set by `CPed::Render` (`0x5E7680`) at `0x5E76B8` and restored at `0x5E7968`; `CClothes::Render` (`0x534310`) sets `ALPHAREF=0` for a few special models at `0x53437C` and restores it at `0x5343DE`. `ALPHAFUNC` is always inherited; the alpha-test/blend **enables** are the per-material pair from item 3.
5. **Vanilla ped rendering never sets CULLNONE for skinned atomics.** There is no per-material or per-model cull test in the skin path. The CULLNONE writer near the ped scene is the pass-level setup at `0x70A960` (`0x70A9C3: push 1 / push 0x14 / call [edx+0x20]`), which is not the skin atomic; the same function restores BACK on its normal completion path at `0x70B711` (`push 2 / push 0x14 / call [eax+0x20]`). So the pass does **not** leak NONE unless it early-returns before the restore. The vanilla building callback's per-material byte test (`[eax+0x10] & 1`) is **not** cull: it writes `rwRENDERSTATETEXTUREADDRESS` (state 2), value 1.
6. The vanilla building and vehicle atomic callbacks also **do not set CULLMODE**. There is no “buildings/vehicles set BACK, peds inherit” asymmetry. All three inherit the pass-level cull. The asymmetry is elsewhere: the ped path is the only one that sets `ALPHAREF` itself, and the skin path is the only one with **no `RwRenderStateSet` call at all** in its node/render/instance chain.
7. **CULLMODE is still the only face-selection state**, so it is the #1 state to test for a strictly view-angle-dependent disappearance. But the evidence is nuanced: the vanilla NONE pass restores BACK on its normal completion path at `0x70B711`, and the log's `[Cull] enter saved=1` / `[PedRS] cull rw=1` spans are consistent with the **mirror pass**, where NONE is the correct value (the building/vehicle/ped pipe callbacks run nested inside it). The direct mismatch in the 22:35 session's ped entry is the **Z/ALPHAFUNC** family: vanilla entry `ZWRITE=0 ZENABLE=0 ZFUNC=8` and typical `ALPHAFUNC=5`, while our contract forces `TRUE/TRUE/LESSEQUAL` and `GREATEREQUAL(7)`. The alpha-test **enable** and blend **enable** are set per material by `0x7FE0A0`, so those two are normally overridden at draw time; the remaining alpha mismatch is `ALPHAFUNC`. That is why the ranked list tests cull first (cheap A/B, face selection) but puts Z and ALPHAFUNC immediately after it.
8. Ranked forced states vanilla does not expect, with exact `src/core/main.cpp` lines, are in §6.

---

## 1. Address mapping and hook chain

### 1.1 PE mapping

The first `.text` section is:

| Field | Value |
|---|---|
| RVA | `0x00001000` |
| Virtual size | `0x00456000` |
| Raw offset | `0x00000400` |
| Raw size | `0x00455E00` |
| VA range | `0x401000` – `0x857000` |

Therefore, for the first `.text`:

```
raw = VA - 0x400C00
```

Examples used in this doc:

| VA | raw |
|---|---|
| `0x7491C0` | `0x3485C0` |
| `0x7C818E` | `0x3C758E` |
| `0x7FE420` | `0x3FD820` |

For `.rdata` the mapping is `raw = VA - 0x401800`; for `.data` it is `raw = VA - 0x402400`. Data-side addresses in this doc (node definitions, maps, device table) use those mappings.

### 1.2 Hook target `0x7491C0`

```
007491C0  56                   push esi
007491C1  8B 74 24 08          mov  esi,[esp+8]          ; atomic
007491C5  8B 46 6C             mov  eax,[esi+0x6C]       ; atomic->pipeline
007491C8  85 C0                test eax,eax
007491CA  75 0F                jne  0x7491DB
007491CC  A1 24 7B C9 00       mov  eax,[0xC97B24]       ; RwEngineInstance
007491D1  8B 0D 60 BC C9 00    mov  ecx,[0xC9BC60]
007491D7  8B 44 01 3C          mov  eax,[ecx+eax+0x3C]   ; default pipeline
007491DB  6A 01                push 1
007491DD  56                   push esi                  ; atomic
007491DE  50                   push eax                  ; pipeline
007491DF  E8 2C C5 0B 00       call 0x805710             ; RxPipelineExecute
007491E4  83 C4 0C             add  esp,0xC
007491E7  F7 D8                neg  eax
007491E9  1B C0                sbb  eax,eax
007491EB  23 C6                and  eax,esi
007491ED  5E                   pop  esi
007491EE  C3                   ret
```

Actual bytes, `0x7491C0..0x7491EF`:

```
56 8B 74 24 08 8B 46 6C 85 C0 75 0F A1 24 7B C9 00 8B 0D 60 BC C9 00
8B 44 01 3C 6A 01 56 50 E8 2C C5 0B 00 83 C4 0C F7 D8 1B C0 23 C6 5E C3 90
```

No state writes, no cull, no alpha, no Z. This callback is executed for **every** atomic with the default render callback, including peds and props. The ped distinction is only `atomic->pipeline == skinPipe` (`0xC978C4`), which `myDefaultCallback` checks.

### 1.3 Skin pipeline node graph

The RW skin node definition is in `.data`:

```
VA 0x8DED08 (raw 0x4DC908):
  E8 EC 8D 00   -> name      = 0x8DECE8  ("nodeD3D9SkinAtomicAllInOne.csl")
  90 7B 7C 00   -> nodeBody  = 0x7C7B90
  00 00 00 00   -> nodeInit  = NULL
  00 00 00 00   -> nodeTerm  = NULL
  40 B2 7C 00   -> pipelinePipes/destroy = 0x7CB240
```

Compare the standard D3D9 all-in-one node at `0x8D6318` (raw `0x4D3F18`):

```
20 00 00 00 01 00 00 00 6E 6F 64 65 44 33 44 39 41 74 6F 6D 69 63 41 6C 6C 49 6E 4F 6E 65 2E 63 73 6C 00
...
18 63 8D 00 -> name      = 0x8D6318
F0 75 75 00 -> nodeBody  = 0x7575F0
00 00 00 00 -> nodeInit  = NULL
00 00 00 00 -> nodeTerm  = NULL
90 78 75 00 -> pipelinePipes = 0x757890
```

So the skin pipeline is a distinct node (`0x7C7B90`), not the standard all-in-one body (`0x7575F0`). It re-uses the same D3D9 render helpers and the default render callback.

`0x7CB240` (skin node pipelinePipes) installs the callbacks:

```
007CB240  8B 44 24 04          mov  eax,[esp+4]          ; node
007CB244  33 C9                xor  ecx,ecx
007CB246  8B 40 14             mov  eax,[eax+0x14]       ; node data
007CB249  C7 00 F0 6D 75 00    mov  dword ptr [eax],0x756DF0       ; render callback
007CB24F  C7 40 04 00 74 75 00 mov  dword ptr [eax+4],0x757400     ; lighting callback
007CB256  39 0D E4 78 C9 00    cmp  [0xC978E4],ecx
007CB25C  74 29                je   0x7CB287
007CB25E  C7 40 08 90 B1 7C 00 mov  dword ptr [eax+8],0x7CB190     ; skin instance callback
007CB265  C7 40 0C 70 11 76 00 mov  dword ptr [eax+0xC],0x761170   ; skin mesh render
007CB26C  C7 40 10 10 10 76 00 mov  dword ptr [eax+0x10],0x761010  ; post-draw
007CB273  C7 40 14 30 10 76 00 mov  dword ptr [eax+0x14],0x761030  ; PS unbind
007CB27A  C7 40 18 00 10 76 00 mov  dword ptr [eax+0x18],0x761000  ; no-op
```

Bytes:

```
8B 44 24 04 33 C9 8B 40 14 C7 00 F0 6D 75 00 C7 40 04 00 74 75 00
39 0D E4 78 C9 00 74 29 C7 40 08 90 B1 7C 00 C7 40 0C 70 11 76 00
C7 40 10 10 10 76 00 C7 40 14 30 10 76 00 C7 40 18 00 10 76 00
```

The standard D3D9 all-in-one installer at `0x757890` installs the quartet `instance=0x7578C0`, `reinstance=0x758270`, `lighting=0x757400`, `render=0x756DF0`. The skin installer re-uses `0x756DF0` and `0x757400` and adds the skin-specific callbacks at `+8..+0x18`.

### 1.4 `0x7C818E` — what it actually is

`0x7C818E` is inside `0x7C8060`, the D3D9 render body used by the skin node. It is **not** a render-state setter. It is `IDirect3DDevice9::SetVertexDeclaration` via vtable slot `+0x15C`, gated by the cached declaration at `0x8E2444`:

```
007C8163  8B 45 34                mov  eax,[ebp+0x34]        ; declaration
007C8166  8B 0D 44 24 8E 00       mov  ecx,[0x8E2444]        ; cached decl
007C816C  83 C4 08                add  esp,8
007C816F  3B C8                   cmp  ecx,eax
007C8171  74 21                   je   0x7C8194
007C8173  A3 44 24 8E 00          mov  [0x8E2444],eax
007C8178  A1 28 7C C9 00          mov  eax,[0xC97C28]        ; IDirect3DDevice9*
007C817D  C7 05 40 24 8E 00 FF FF FF FF  mov dword ptr [0x8E2440],-1
007C8187  8B 4D 34                mov  ecx,[ebp+0x34]
007C818A  8B 10                   mov  edx,[eax]
007C818C  51                      push ecx
007C818D  50                      push eax
007C818E  FF 92 5C 01 00 00       call dword ptr [edx+0x15C] ; SetVertexDeclaration
```

Bytes, `0x7C8163..0x7C8193`:

```
8B 45 34 8B 0D 44 24 8E 00 83 C4 08 3B C8 74 21 A3 44 24 8E 00
A1 28 7C C9 00 C7 05 40 24 8E 00 FF FF FF FF 8B 4D 34 8B 10 51
50 FF 92 5C 01 00 00
```

This binds the skin vertex declaration. It changes no render state and cannot cause culling or alpha differences.

---

## 2. Every state write on the vanilla ped/skin path

Ranges scanned for the ped path:

| Range | Function |
|---|---|
| `0x7491C0..0x7491F0` | `AtomicDefaultRenderCallBack` |
| `0x7C7B90..0x7C8060` | skin node body |
| `0x7C8060..0x7C85B0` | D3D9 skin render (VS path) |
| `0x7C85B0..0x7C8700` | D3D9 skin render fallback (FFP path) |
| `0x7C9680..0x7C9DA0` | skin mesh setup |
| `0x761170..0x761C90` | skin mesh draw |
| `0x7CB190..0x7CB240` | skin instance callback |
| `0x756DF0..0x757400` | default render callback used by the skin node |
| `0x5E7680..0x5E7976` | `CPed::Render` |
| `0x534310..0x5343EE` | `CClothes::Render` |

Scanner result:

| Range | `push 0x14` (CULLMODE) | `push 0x0E/0x0F/0x17/0x18/0x19/0x1B` (Z/alpha/blend) | `RwRenderStateSet` (`call [reg+0x20]`) |
|---|---:|---:|---:|
| `0x7491C0..0x7491F0` | 0 | 0 | 0 |
| `0x7C7B90..0x7C8060` | 0 | 0 | 0 |
| `0x7C8060..0x7C85B0` | 0 | 0 | 0 |
| `0x7C85B0..0x7C8700` | 0 | 0 | 0 |
| `0x7C9680..0x7C9DA0` | 0 | 0 | 0 |
| `0x761170..0x761C90` | 0 | 0 | 0 |
| `0x7CB190..0x7CB240` | 0 | 0 | 0 |
| `0x756DF0..0x757400` | 0 | 0 | 0 |
| `0x5E7680..0x5E7976` | 0 | 0 | 2 (both ALPHAREF) |
| `0x534310..0x5343EE` | 0 | 0 | 2 (both ALPHAREF) |

**No CULLMODE write exists anywhere in the skin chain.** The only two `RwRenderStateSet` calls in the ped draw code are the `ALPHAREF` save/set and restore in `CPed::Render`; `CClothes::Render` has the same pair with value 0 for special models. One indirect write is not visible to the direct scan: the per-material alpha-test/blend pair via `0x7FE0A0` (§2.4).

### 2.1 Writes that do exist

| VA | Bytes | Decode |
|---|---|---|
| `0x7C80A0` | `6A 01 68 88 00 00 00` | `push 1; push 0x88` → `RwD3D9SetRenderState(D3DRS_CLIPPING, TRUE)` |
| `0x7C80B7` | `E8 14 42 03 00` | `call 0x7FC2D0` (`RwD3D9SetRenderState`) |
| `0x7C86BB` | `6A 01 EB 02 6A 00 68 8F 00 00 00` | normalize-normals value 1 or 0; state `0x8F` |
| `0x7C86C6` | `E8 05 3C 03 00` | `RwD3D9SetRenderState(D3DRS_NORMALIZENORMALS, 0/1)` |
| `0x756E4E` | `33 D2 3B C7 0F 94 C2 52 68 88 00 00 00 E8 70 54 0A 00` | clipping = (texture==NULL); state `0x88` |
| `0x756F14` | `68 00 00 00 FF 6A 3C E8 B0 53 0A 00` | `TEXTUREFACTOR=0xFF000000` (state `0x3C`), FFP path |
| `0x756F20` | `57 6A 1A E8 A8 53 0A 00` | `DITHERENABLE=0` (state `0x1A`), FFP path |
| `0x756F28` | `6A 01 6A 09 E8 9F 53 0A 00` | `SHADEMODE=1` (state `0x09`), FFP path |
| `0x75707B` | `E8 50 52 0A 00` | restore `DITHERENABLE` |
| `0x757087` | `8B 44 24 28 50 6A 09 E8 44 52 0A 00` | restore `SHADEMODE` |
| `0x757144` | `50 6A 3C E8 87 51 0A 00` | `TEXTUREFACTOR` (VS path material color) |
| `0x5E76B4` | `6A 01 6A 1E FF 52 20` | `RwRenderStateSet(rwALPHATESTFUNCTIONREF=30, 1)` |
| `0x534378` | `6A 00 6A 1E FF 52 20` | `RwRenderStateSet(rwALPHATESTFUNCTIONREF=30, 0)` (special clothes models) |

`0x88` is `D3DRS_CLIPPING`; `0x8F` is `D3DRS_NORMALIZENORMALS`; `0x3C` is `D3DRS_TEXTUREFACTOR`; `0x1A` is `D3DRS_DITHERENABLE`; `0x09` is `D3DRS_SHADEMODE`.

### 2.2 `RwRenderStateSet` is called only for ALPHAREF

`CPed::Render` (`0x5E7680`) save/set:

```
005E769E  8B 0D 24 7B C9 00    mov  ecx,[0xC97B24]        ; RwEngineInstance
005E76A4  8D 44 24 0C          lea  eax,[esp+0xC]         ; &savedAlphaRef
005E76A8  50                   push eax
005E76A9  6A 1E                push 0x1E                  ; rwALPHATESTFUNCTIONREF
005E76AB  FF 51 24             call dword ptr [ecx+0x24]  ; RwRenderStateGet
005E76AE  8B 15 24 7B C9 00    mov  edx,[0xC97B24]
005E76B4  6A 01                push 1                     ; value = 1
005E76B6  6A 1E                push 0x1E                  ; state = ALPHAREF
005E76B8  FF 52 20             call dword ptr [edx+0x20]  ; RwRenderStateSet
005E76BB  83 C4 10             add  esp,0x10
```

Bytes, `0x5E769E..0x5E76BD`:

```
8B 0D 24 7B C9 00 8D 44 24 0C 50 6A 1E FF 51 24 8B 15 24 7B C9 00
6A 01 6A 1E FF 52 20 83 C4 10
```

Restore at `0x5E795C`:

```
005E795C  8B 54 24 14          mov  edx,[esp+0x14]        ; saved ref
005E7960  A1 24 7B C9 00       mov  eax,[0xC97B24]
005E7965  52                   push edx
005E7966  6A 1E                push 0x1E
005E7968  FF 50 20             call dword ptr [eax+0x20]  ; RwRenderStateSet(ALPHAREF, saved)
005E796B  83 C4 08             add  esp,8
```

Bytes:

```
8B 54 24 14 A1 24 7B C9 00 52 6A 1E FF 50 20 83 C4 08
```

`CClothes::Render` (`0x534310`) save/set 0 for the special model path:

```
00534362  8B 0D 24 7B C9 00    mov  ecx,[0xC97B24]
00534368  8D 44 24 04          lea  eax,[esp+4]
0053436C  50                   push eax
0053436D  6A 1E                push 0x1E
0053436F  FF 51 24             call dword ptr [ecx+0x24]  ; Get
00534372  8B 15 24 7B C9 00    mov  edx,[0xC97B24]
00534378  6A 00                push 0                   ; value = 0
0053437A  6A 1E                push 0x1E
0053437C  FF 52 20             call dword ptr [edx+0x20]  ; Set ALPHAREF=0
0053437F  83 C4 10             add  esp,0x10
```

Bytes:

```
8B 0D 24 7B C9 00 8D 44 24 04 50 6A 1E FF 51 24 8B 15 24 7B C9 00
6A 00 6A 1E FF 52 20 83 C4 10
```

Restore at `0x5343D1`:

```
8B 44 24 04 8B 0D 24 7B C9 00 50 6A 1E FF 51 20
```

The driver handler for `rwALPHATESTFUNCTIONREF` is state 30 in the `RwRenderStateSet` jump table, and it forwards to `D3DRS_ALPHAREF` (`0x18`):

```
007FE9DE  8B 54 24 10          mov  edx,[esp+0x10]
007FE9E2  52                   push edx
007FE9E3  6A 18                push 0x18                  ; D3DRS_ALPHAREF
007FE9E5  E8 E6 D8 FF FF       call 0x7FC2D0              ; RwD3D9SetRenderState
```

Bytes:

```
8B 54 24 10 52 6A 18 E8 E6 D8 FF FF
```

So the only vanilla ped-path alpha test state is **ALPHAREF=1**, and it is set and restored by `CPed::Render`, not by the skin pipeline.

### 2.3 Skin instance callback reads fog, does not set it

`0x7CB190`:

```
007CB1D3  8B 0D 24 7B C9 00    mov  ecx,[0xC97B24]
007CB1D9  8D 44 24 10          lea  eax,[esp+0x10]
007CB1DD  50                   push eax
007CB1DE  6A 0E                push 0x0E                  ; rwRENDERSTATEFOGENABLE
007CB1E0  FF 51 24             call dword ptr [ecx+0x24]  ; RwRenderStateGet
...
007CB1EE  A1 24 7B C9 00       mov  eax,[0xC97B24]
007CB1F3  8D 54 24 08          lea  edx,[esp+8]
007CB1F7  52                   push edx
007CB1F8  6A 10                push 0x10                  ; rwRENDERSTATEFOGTYPE
007CB1FA  FF 50 24             call dword ptr [eax+0x24]  ; RwRenderStateGet
```

Bytes:

```
8B 0D 24 7B C9 00 8D 44 24 10 50 6A 0E FF 51 24 8B 44 24 18 83 C4 08
85 C0 74 43 A1 24 7B C9 00 8D 54 24 08 52 6A 10 FF 50 24
```

Only `Get`, never `Set`.

### 2.4 Per-material alpha-test / blend pair: `0x7FE0A0`

The default render callback `0x756DF0` calls the rw vertex-alpha handler `0x7FE0A0` per mesh:

```
00756F0E  57                   push edi                  ; FFP path: edi = 0
00756F0F  E8 8C 71 0A 00       call 0x7FE0A0
...
007570EF  6A 01                push 1                    ; VS path: material vertex alpha
007570F1  E8 AA 6F 0A 00       call 0x7FE0A0
```

Bytes:

```
57 E8 8C 71 0A 00
6A 01 E8 AA 6F 0A 00
```

`0x7FE0A0` is the handler for `rwRENDERSTATEVERTEXALPHAENABLE` (state 12). It queues a **pair** of D3D states:

- value 0 → `D3DRS_ALPHABLENDENABLE=0` and `D3DRS_ALPHATESTENABLE=1` (alpha test on, blend off);
- value 1 → `D3DRS_ALPHABLENDENABLE=1` and `D3DRS_ALPHATESTENABLE=0` (blend on, alpha test off).

The queue writes:

```
007FE0EA  C7 04 8D 40 8B C9 00 1B 00 00 00  mov dword ptr [ecx*4+0xC98B40],0x1B   ; D3DRS_ALPHABLENDENABLE
007FE178  C7 04 8D 40 8B C9 00 0F 00 00 00  mov dword ptr [ecx*4+0xC98B40],0x0F   ; D3DRS_ALPHATESTENABLE
```

The handler is cache-gated on `0xC9A4E8` (the rw vertex-alpha value):

```
007FE0A0  8B 4C 24 04          mov  ecx,[esp+4]          ; requested value
007FE0A4  33 D2                xor  edx,edx
007FE0A6  3B CA                cmp  ecx,edx
007FE0A8  8B 0D E8 A4 C9 00    mov  ecx,[0xC9A4E8]       ; cached vertex alpha
007FE0AE  B8 01 00 00 00       mov  eax,1
007FE0B3  74 61                je   0x7FE116            ; value 0 path
007FE0B5  3B CA                cmp  ecx,edx
007FE0B7  0F 85 CD 00 00 00    jne  0x7FE18A            ; already cached -> skip pair
```

Bytes:

```
8B 4C 24 04 33 D2 3B CA 8B 0D E8 A4 C9 00 B8 01 00 00 00 74 61
3B CA 0F 85 CD 00 00 00
```

This is the only place the vanilla skin path sets `ALPHATESTENABLE` and `ALPHABLENDENABLE`. It is a **per-material** pair, not a global contract. It does **not** set `ALPHAFUNC` or `ALPHAREF`; those remain inherited (`ALPHAREF` is supplied by `CPed::Render`).

---

## 3. The state table

`D3D` values are the real Direct3D 9 render-state numbers; `rw` values are the RenderWare state numbers. The two are different namespaces and are called out explicitly.

### 3.1 Compact requested table

| State | Vanilla value | Setter address | What our ped contract forces | Mismatch? |
|---|---|---|---|---|
| CULLMODE | inherit; pass NONE at `0x70A9C3`, BACK restore at `0x70B711` | none on ped path (pass-level) | saved L1 / `pedCullMode` | **Y conditional** |
| ALPHATESTENABLE | per material: 0→ON, 1→OFF | `0x7FE0A0` via `0x756F0F` / `0x7570F1` | `TRUE` | **N at draw time** (per-material override) |
| ALPHAFUNC | inherit; typical 5 (`GREATER`) | none on ped path | 7 (`GREATEREQUAL`) | **Y** |
| ALPHAREF | 1 (`CPed`); 0 for special `CClothes` models | `0x5E76B8`; `0x53437C` | 1 | **N** for peds; **Y** for CClothes 0 sub-pass |
| VERTEXALPHAENABLE | per material flag | `0x7FE0A0` | `FALSE` | **N at draw time** (per-material override) |
| ALPHABLENDENABLE | per material: 1→ON, 0→OFF | `0x7FE0A0` | `FALSE` | **N at draw time** (per-material override) |
| SRCBLEND | inherit | none on ped path | 5 (`SRCALPHA`) | **Y conditional** |
| DESTBLEND | inherit | none on ped path | 6 (`INVSRCALPHA`) | **Y conditional** |
| BLENDOP | inherit | none on ped path | 1 (`ADD`) | **Y conditional** |
| ZWRITE | inherit; log shows 0 | none on ped path | `TRUE` | **Y conditional** |
| ZENABLE | inherit; log shows 0 | none on ped path | `TRUE` | **Y conditional** |
| ZFUNC | inherit; log shows 8 (`ALWAYS`) | none on ped path | 4 (`LESSEQUAL`) | **Y conditional** |
| CLIPPING | `TRUE` | `0x7C80B7` / `0x756E5B` | untouched | **N** |
| NORMALIZENORMALS | 0/1 (scale test) | `0x7C86C6` | untouched | **N** |
| FOGENABLE | read only | `0x7CB1DE` (Get) | untouched | **N** |
| FOGTYPE | read only | `0x7CB1F8` (Get) | untouched | **N** |
| SEPARATEALPHABLENDENABLE | inherit | none on ped path | `FALSE` | **Y conditional** |
| COLORWRITEENABLE | inherit | none on ped path | `0xF` | **Y conditional** |

### 3.2 Detailed table with evidence

| State | Vanilla ped path value / setter | What our ped contract forces | Mismatch? |
|---|---|---|---|
| **CULLMODE** (`D3DRS_CULLMODE=0x16`, `rwRENDERSTATECULLMODE=20`) | **Inherited.** No write in `0x7491C0`, `0x7C7B90`, `0x7C8060`, `0x7C85B0`, `0x7C9680`, `0x761170`, `0x7CB190`, `0x756DF0`, `0x5E7680`, `0x534310`. Pass-level writers near the ped scene: `0x70A9C3` sets NONE (1); the same pass restores BACK (2) on its normal exit at `0x70B711`; `0x7114BD` sets FRONT (3), then `0x7114F3` / `0x71155C` / `0x7116CD` set BACK (2). | `saved` L1 cull (normally 2 BACK; 1 NONE if inside the mirror pass) via `pipeEnterCullMode()` at `main.cpp:1736`, force-written by `pipelinecommon.cpp:1278`; or `pedCullMode` 1/2/3 via `main.cpp:1758`. | **YES — conditional; #1 face-selection test.** Only face-selection state. |
| **ALPHATESTENABLE** (`D3DRS_ALPHATESTENABLE=0x0F`, rw has no twin) | **Set per material** by the rw vertex-alpha handler `0x7FE0A0` (called at `0x756F0F` FFP and `0x7570F1` VS): value 0 → test ON; value 1 → test OFF. | `TRUE` at `main.cpp:1101` (L2) and `main.cpp:1114` (L3) when `pedAlphaTestEnable` is 1 (default); `FALSE` at `main.cpp:1105` when the lever is 0. | **N at draw time normally** (the per-material callback overrides). Low risk unless the handler cache-skips or the callback is bypassed. |
| **ALPHAFUNC** (`D3DRS_ALPHAFUNC=0x19`, `rwALPHATESTFUNCTION=29`) | **Inherited.** `0x7FE0A0` does not touch it. Log shows typical value 5 (`GREATER`). | `GREATEREQUAL` (7) at `main.cpp:1088` (L1) and `main.cpp:1102` (L2), plus L3 `main.cpp:1115`. | **YES** (5 → 7). This is the remaining alpha mismatch. |
| **ALPHAREF** (`D3DRS_ALPHAREF=0x18`, `rwALPHATESTFUNCTIONREF=30`) | **Set by the caller:** `CPed::Render` saves and sets **1** at `0x5E76B8`, restores at `0x5E7968`. `CClothes::Render` saves and sets **0** at `0x53437C` for special models, restores at `0x5343DE`. | `1` at `main.cpp:1089` (L1), `main.cpp:1103` (L2), `main.cpp:1116` (L3). | **N for normal peds** (both 1). **Y for the CClothes 0 sub-pass** (our 1 overrides the caller's 0). |
| **VERTEXALPHAENABLE** (`rwRENDERSTATEVERTEXALPHAENABLE=12`) | **Set per material** through `0x7FE0A0`; the value is the material's vertex-alpha flag (0 or 1). | `FALSE` at `main.cpp:1084` (L1), plus `pipeEnterAlphaMode` at `pipelinecommon.cpp:1627`. | **N at draw time normally** (per-material override). |
| **ALPHABLENDENABLE** (`D3DRS_ALPHABLENDENABLE=0x1B`) | **Set per material** by `0x7FE0A0`: value 1 → blend ON; value 0 → blend OFF. | `FALSE` at `main.cpp:1093` (L2) and L3 `main.cpp:1109`. | **N at draw time normally** (per-material override). |
| **SRCBLEND** (`D3DRS_SRCBLEND=0x13`, `rwRENDERSTATESRCBLEND=10`) | **Inherited.** No setter. | `SRCALPHA` (5) at `pipelinecommon.cpp:1613`, called from `main.cpp:1769`. | **YES** if inherited different. |
| **DESTBLEND** (`D3DRS_DESTBLEND=0x14`, `rwRENDERSTATEDESTBLEND=11`) | **Inherited.** No setter. | `INVSRCALPHA` (6) at `pipelinecommon.cpp:1614`, called from `main.cpp:1769`. | **YES** if inherited different. |
| **BLENDOP** (`D3DRS_BLENDOP=0xAB`) | **Inherited.** No setter. | `ADD` (1) via `pipeForceAlphaBlock()` at `main.cpp:1792` (`buildingPipe.cpp:600`). | **YES** if inherited different. |
| **ZWRITE** (`D3DRS_ZWRITEENABLE=0x0E`, rw `ZWRITEENABLE=8`) | **Inherited.** No setter. Log shows `zw=0` on entry. | `TRUE` at `main.cpp:1085` (L1), `main.cpp:1094` (L2), L3 `main.cpp:1110`. | **YES** if inherited FALSE. |
| **ZTEST/ZENABLE** (`D3DRS_ZENABLE=0x07`, rw `ZTESTENABLE=6`) | **Inherited.** No setter. Log shows `zen=0` on entry. | `TRUE` at `main.cpp:1086` (L1 ZTEST), `main.cpp:1095` (L2 ZENABLE), L3 `main.cpp:1111`. | **YES** if inherited FALSE. |
| **ZFUNC** (`D3DRS_ZFUNC=0x17`, rw has no twin) | **Inherited.** No setter. Log shows `zfunc=8` (`ALWAYS`) on entry. | `LESSEQUAL` (4) at `main.cpp:1096` (L2), L3 `main.cpp:1112`. | **YES** if inherited ALWAYS. |
| **CLIPPING** (`D3DRS_CLIPPING=0x88`) | **Set TRUE** by the skin render at `0x7C80B7`; also by `0x756E5B` and the building/vehicle callbacks via `0x756D90`. | Untouched. | **N** (match). |
| **NORMALIZENORMALS** (`D3DRS_NORMALIZENORMALS=0x8F`) | **Set 0/1** at `0x7C86C6` based on the atomic basis scale (0.9..1.1 window). | Untouched. | **N** (match). |
| **FOGENABLE** (`rwRENDERSTATEFOGENABLE=14`) | **Read only** at `0x7CB1DE`. | Untouched. | **N**. |
| **FOGTYPE** (`rwRENDERSTATEFOGTYPE=16`) | **Read only** at `0x7CB1F8`. | Untouched. | **N**. |
| **SEPARATEALPHABLENDENABLE** (`0xCE`) | **Inherited.** No setter. | `FALSE` at `pipelinecommon.cpp:1684`, called from `main.cpp:1769`. | **YES** if inherited TRUE. |
| **COLORWRITEENABLE** (`0xA8`) | **Inherited.** No setter. | `ALL` (0xF) at `pipelinecommon.cpp:1688`, called from `main.cpp:1769`. | **YES** if inherited masked. |

The log corroboration (deployed `skygfx_dbg.log`, 22:35–22:42 on 2026-10-25) shows the exact inherited states:

```
[22:35:40.941] [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0
    ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0 | rw ref=2 fn=5 vtx=0 zw=0 ztest=1
    | drv ref=2 fn=5 blend=0 zw=0
[22:42:20.215] [PedRS] ok cull rw=2 drv=2 dev=2 | blend rw=0 drv=0 dev=0
    | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1
    | afunc rw=7 drv=7 dev=7 | aref rw=1 drv=1 dev=1
[22:42:30.202] [Cull] enter saved=1 forced=1
[22:35:40.970] [PedRS] ok cull rw=1 drv=1 dev=1 | ... | afunc rw=5 ... aref rw=2
```

The first line is the pre-entry world state reaching the ped callback: **alpha test off**, **Z off / ZFUNC ALWAYS**, **vtx=0**, **ALPHAREF=2**. `CPed::Render` then sets `ALPHAREF=1`, and the default render callback sets the per-material `ALPHABLENDENABLE`/`ALPHATESTENABLE` pair through `0x7FE0A0`. The later `[PedRS]` lines are after our contract and show the forced canonical states; they do not prove what the per-material callback did during the draw. The `[Cull] enter saved=1` and `[PedRS] cull=1` lines show NONE spans are live in the session, but the `[Cull]` lines are untagged.

---

## 4. Direct answers

### Q1. What does the vanilla ped/skin path set?

- `D3DRS_CLIPPING = TRUE` at `0x7C80B7` (skin render `0x7C8060`): `push 1; push 0x88; call RwD3D9SetRenderState` (full contiguous bytes in §2.1 / appendix B).
- `D3DRS_NORMALIZENORMALS = 0/1` at `0x7C86C6`: `6A 01 EB 02 6A 00 68 8F 00 00 00 E8 05 3C 03 00`.
- `D3DRS_TEXTUREFACTOR` (0x3C), `D3DRS_DITHERENABLE` (0x1A), `D3DRS_SHADEMODE` (0x09) in the FFP path of the default render callback: `0x756F1B`, `0x756F23`, `0x756F2C`, restored at `0x75707B`/`0x757087`.
- `rwRENDERSTATEVERTEXALPHAENABLE` (the material's vertex-alpha flag) plus the `D3DRS_ALPHABLENDENABLE` / `D3DRS_ALPHATESTENABLE` pair, all through `0x7FE0A0`, called at `0x756F0F` (FFP, value 0) and `0x7570F1` (VS, value 0/1): value 0 → blend OFF + test ON; value 1 → blend ON + test OFF.
- `ALPHAREF=1` via `RwRenderStateSet` at `0x5E76B8` (caller `CPed::Render`), and `ALPHAREF=0` at `0x53437C` (special `CClothes::Render` path).
- **Nothing else.** In particular no CULLMODE, ZWRITE, ZENABLE, ZFUNC, ALPHAFUNC, SRCBLEND, DESTBLEND. `ALPHAFUNC` and the blend factors are inherited; `ALPHAREF` is supplied by the caller; the alpha-test/blend enables are the per-material pair above.

The skin instance callback only reads fog: `RwRenderStateGet(FOGENABLE=14)` at `0x7CB1DE` and `RwRenderStateGet(FOGTYPE=16)` at `0x7CB1F8`.

### Q2. Does vanilla ped rendering ever set CULLNONE for skinned atomics?

**No.** The skin node (`0x7C7B90`), its render helpers (`0x7C8060`, `0x7C85B0`), the skin mesh renderer (`0x761170`), the skin instance callback (`0x7CB190`), `CPed::Render` (`0x5E7680`) and `CClothes::Render` (`0x534310`) all contain **zero** `push 0x14` / `push 0x16` / `[reg+0x20]`-state-20 writes. The ped path draws with **whatever cull mode the previous pass left**.

The CULLNONE writer that matters for the ped scene is the pass-level block at `0x70A960`:

```
0070A9AE  8B 0D 24 7B C9 00    mov  ecx,[0xC97B24]
0070A9B4  6A 03                push 3
0070A9B6  6A 02                push 2                  ; rwRENDERSTATETEXTUREADDRESS
0070A9B8  FF 51 20             call [ecx+0x20]
0070A9BB  8B 15 24 7B C9 00    mov edx,[0xC97B24]
0070A9C1  6A 01                push 1                  ; value = rwCULLMODECULLNONE
0070A9C3  6A 14                push 0x14               ; rwRENDERSTATECULLMODE
0070A9C5  FF 52 20             call [edx+0x20]
```

Bytes, `0x70A9AE..0x70A9C7`:

```
8B 0D 24 7B C9 00 6A 03 6A 02 FF 51 20 8B 15 24 7B C9 00 6A 01 6A 14 FF 52 20
```

That is a full pass setup (it also sets `ZWRITE=0`, `ZTEST=1`, `VERTEXALPHA=1`, `FOG=0`, `TEXTUREADDRESS=3`, `ALPHAREF=0`, `TEXTUREFILTER=2`), not the skin atomic. The same function **restores BACK on its normal completion path** at `0x70B711`:

```
0070B70A  A1 24 7B C9 00       mov  eax,[0xC97B24]
0070B70F  6A 02                push 2                  ; rwCULLMODECULLBACK
0070B711  6A 14                push 0x14               ; rwRENDERSTATECULLMODE
0070B713  FF 50 20             call [eax+0x20]
0070B716  83 C4 28             add  esp,0x28
...
0070B727  C3                   ret
```

Bytes, `0x70B70A..0x70B713`:

```
A1 24 7B C9 00 6A 02 6A 14 FF 50 20
```

So the pass is balanced: NONE for the pass body, BACK at exit. The pass also sets `TEXTUREADDRESS=3` (mirror) at `0x70A9B4`, which is consistent with the mirror pass. A ped callback that runs **inside** the pass legitimately sees NONE and must restore NONE; a ped callback after the pass sees BACK. There is no vanilla `CULLNONE` contract to preserve outside the pass.

The two-sided pass at `0x7114B3` does use FRONT and BACK:

```
007114B9  6A 03                push 3                  ; rwCULLMODECULLFRONT
007114BB  6A 14                push 0x14
007114BD  FF 51 20             call [ecx+0x20]
...
007114EF  6A 02                push 2                  ; rwCULLMODECULLBACK
007114F1  6A 14                push 0x14
007114F3  FF 50 20             call [eax+0x20]
...
0071155A  6A 14                push 0x14
0071155C  FF 50 20             call [eax+0x20]         ; BACK again
...
007116CB  6A 14                push 0x14
007116CD  FF 51 20             call [ecx+0x20]         ; BACK at exit
```

This pass leaves BACK. So the only persistent NONE source is `0x70A9C3`.

**Per-material/per-model flag:** there is **no** cull flag in the ped path. The only material bit the vanilla code tests is in the **building** callback at `0x5D6559`:

```
005D6559  8A 58 10             mov  bl,[eax+0x10]      ; per-material record byte +0x10
005D655C  8B 34 01             mov  esi,[ecx+eax]
005D6565  80 E3 01             and  bl,1
...
005D657B  84 DB                test bl,bl
005D657D  0F 84 1B 01 00 00    je   0x5D669E
005D6583  8B 15 24 7B C9 00    mov  edx,[0xC97B24]
005D6589  6A 01                push 1
005D658B  6A 02                push 2                  ; rwRENDERSTATETEXTUREADDRESS
005D658D  FF 52 20             call [edx+0x20]
```

Bytes, `0x5D6559..0x5D6590`:

```
8A 58 10 8B 34 01 6A 01 6A 01 6A 01 80 E3 01 E8 D3 5D 22 00
6A 01 6A 01 6A 02 E8 C8 5D 22 00 83 C4 18 84 DB 0F 84 1B 01 00 00
8B 15 24 7B C9 00 6A 01 6A 02 FF 52 20
```

State **2** is `rwRENDERSTATETEXTUREADDRESS`, not cull. The driver handler for state 2 ends in `call [edx+0x114]` (`IDirect3DDevice9::SetSamplerState`), proving it:

```
007FE6DE  A1 0C A5 C9 00       mov  eax,[0xC9A50C]
...
007FE6F1  8B 04 B5 3C 49 88 00 mov  eax,[esi*4+0x88493C]
...
007FE70F  FF 92 14 01 00 00    call dword ptr [edx+0x114] ; SetSamplerState
```

Bytes:

```
A1 0C A5 C9 00 56 8B 74 24 14 BF 01 00 00 00 3B F0 74 24 8B 04 B5
3C 49 88 00 8B 0D 28 7C C9 00 50 89 35 0C A5 C9 00 A3 3C 89 C9 00
8B 11 57 53 51 FF 92 14 01 00 00
```

So the building “material bit 0” is a texture-address flag, not a two-sided cull flag. Do **not** translate it into `CULLNONE` for peds.

If you want a real two-sided test from C++, there is no vanilla RW pedestrian flag to read. The only per-geometry bit the skin code tests is `geometry+0x08 & 0x10` (`rpGEOMETRYNORMALS`) at `0x7C7CF4`/`0x7C7CFD`, and that selects the vertex format, not culling:

```
007C7CF4  8A 4E 08             mov  cl,[esi+8]          ; geometry flags
007C7CFD  F6 C1 10             test cl,0x10             ; rpGEOMETRYNORMALS
007C7D00  89 54 24 38          mov  [esp+0x38],edx
007C7D04  74 09                je   0x7C7D0F
```

### Q3. Does the skin path set its own ALPHAREF, or inherit it?

Both, depending on what “skin path” means:

- The **skin pipeline itself** (node body, render helpers, skin instance callback, skin mesh renderer) does **not** touch `ALPHAREF`; it inherits.
- The default render callback **does** set `ALPHATESTENABLE`/`ALPHABLENDENABLE` per material through `0x7FE0A0`, but it never touches `ALPHAREF` or `ALPHAFUNC`.
- The **ped draw caller** `CPed::Render` does save and set `ALPHAREF=1` (`0x5E76A4..0x5E76BB`), then restores the pre-entry value at `0x5E795C..0x5E796B`. So the skin draw executes with **ALPHAREF=1**.
- `CClothes::Render` has a special-model branch that saves and sets `ALPHAREF=0` (`0x534362..0x53437F`) and restores it at `0x5343D1..0x5343DE`. That sub-draw executes with 0.
- `ALPHAFUNC` is always inherited; the log's typical inherited value is 5 (`GREATER`), and our contract changes it to 7 (`GREATEREQUAL`).

### Q4. Does it set ZWRITE itself, or inherit it?

**Inherits.** There is no `RwRenderStateSet(rwRENDERSTATEZWRITEENABLE)` and no `RwD3D9SetRenderState(D3DRS_ZWRITEENABLE)` anywhere in the skin pipeline, `CPed::Render`, or `CClothes::Render`. `ZENABLE` and `ZFUNC` are also inherited. The log shows the vanilla entry can be `ZWRITE=0`, `ZENABLE=0`, `ZFUNC=8` (`ALWAYS`); our contract forces `TRUE / TRUE / LESSEQUAL`.

### Q5. What CULLMODE do the vanilla building and vehicle draws set?

**They do not set CULLMODE either.** There is no BACK-vs-inherit asymmetry.

Building render callback `0x5D6480` (installed at `0x5D67F3`):

- Only `RwRenderStateSet` call: `0x5D6589..0x5D658D` → `state=2` (`rwRENDERSTATETEXTUREADDRESS`), `value=1` (WRAP), for per-material records whose byte `+0x10` bit 0 is set.
- `RwD3D9SetRenderState` calls: `0x5D64D1` / `0x5D6622` (`D3DRS_TEXTUREFACTOR=0x3C`), `0x756D90` (`D3DRS_CLIPPING=0x88`), plus texture-stage states.
- It calls `RwD3D9GetRenderState(D3DRS_LIGHTING=0x89)` at `0x5D649F..0x5D64A4`. No cull.

Vehicle render callback `0x5D9900` (installed at `0x5D9FE3`):

- Only `RwRenderStateSet` calls: `0x5D9B54` and `0x5D9CC1` → `state=2`, `value=1` (texture address), same as building.
- `RwD3D9SetRenderState` calls: `0x5D997B` / `0x5D9C2D` (`D3DRS_TEXTUREFACTOR=0x3C`), `0x5D9ACB` / `0x5D9AD7` / `0x5D9AE3` / `0x5D9B0D` (various texture/lighting states), plus texture-stage states. No cull.

Bytes for the vehicle state-2 calls:

```
005D9B4A  8B 0D 24 7B C9 00 6A 01 6A 02 FF 51 20
005D9CB8  A1 24 7B C9 00 6A 01 6A 02 FF 50 20
```

So the asymmetry is **not** “buildings set BACK, peds inherit.” All three inherit the pass-level cull. The real asymmetries are:

1. Peds are the only pipe group that sets `ALPHAREF` itself (`CPed::Render`), because skin textures need the cutout threshold.
2. The skin pipeline is the only pipe group with **no `RwRenderStateSet` call at all** in its node/render/instance chain; it relies on the world state for Z, cull, alpha enable/func, vertex alpha and blend.
3. Building/vehicle callbacks do per-material texture-address/stage setup and have their own lighting/render callbacks; the skin path does per-material texture-stage setup in `0x761170` but no render-state repair.

That means a forced state that happens to be harmless for buildings/vehicles (because their callbacks overwrite it per material) can be fatal for peds, which never overwrite it.

---

## 5. How the driver maps rw state 20 to D3D

`RwRenderStateSet` itself is the jump-table dispatcher at `0x7FE420`:

```
007FE420  8B 4C 24 04          mov  ecx,[esp+4]          ; rw state
007FE424  53                   push ebx
007FE425  33 DB                xor  ebx,ebx
007FE427  33 C0                xor  eax,eax
007FE429  49                   dec  ecx
007FE42A  57                   push edi
007FE42B  83 F9 1D             cmp  ecx,0x1D
007FE42E  0F 87 BE 05 00 00    ja   0x7FE9F2
007FE434  FF 24 8D F8 E9 7F 00 jmp  dword ptr [ecx*4+0x7FE9F8]
```

Bytes:

```
8B 4C 24 04 53 33 DB 33 C0 49 57 83 F9 1D 0F 87 BE 05 00 00
FF 24 8D F8 E9 7F 00
```

The jump table at `0x7FE9F8` (bytes `D1 E7 7F 00 DE E6 7F 00 ...`):

| rw state | handler | meaning |
|---:|---|---|
| 2 | `0x7FE6DE` | `rwRENDERSTATETEXTUREADDRESS` (SetSamplerState) |
| 6 | `0x7FE7F2` | `rwRENDERSTATEZTESTENABLE` |
| 8 | `0x7FE7E2` | `rwRENDERSTATEZWRITEENABLE` |
| 10 | `0x7FE802` | `rwRENDERSTATESRCBLEND` |
| 11 | `0x7FE851` | `rwRENDERSTATEDESTBLEND` |
| 12 | `0x7FE8AF` | `rwRENDERSTATEVERTEXALPHAENABLE` |
| 14 | `0x7FE43B` | `rwRENDERSTATEFOGENABLE` |
| 16 | `0x7FE515` | `rwRENDERSTATEFOGTYPE` |
| 20 | `0x7FE8DD` | `rwRENDERSTATECULLMODE` |
| 29 | `0x7FE96D` | `rwRENDERSTATEALPHATESTFUNCTION` |
| 30 | `0x7FE9DE` | `rwRENDERSTATEALPHATESTFUNCTIONREF` |

State 20 handler:

```
007FE8DD  8B 4C 24 10          mov  ecx,[esp+0x10]       ; value
007FE8E1  51                   push ecx
007FE8E2  E8 19 04 00 00       call 0x7FED00             ; queue cull
```

`0x7FED00`:

```
007FED00  8B 4C 24 04          mov  ecx,[esp+4]          ; rw cull
007FED04  A1 F0 A4 C9 00       mov  eax,[0xC9A4F0]       ; cached cull
007FED09  3B C8                cmp  ecx,eax
007FED0B  74 3B                je   0x7FED48
007FED0D  8B 04 8D 90 49 88 00 mov  eax,[ecx*4+0x884990]  ; rw->D3D map
007FED14  A3 80 92 C9 00       mov  [0xC99280],eax       ; pending cull
007FED19  A1 84 92 C9 00       mov  eax,[0xC99284]
007FED1E  85 C0                test eax,eax
007FED20  75 20                jne  0x7FED42
007FED22  A1 EC A5 C9 00       mov  eax,[0xC9A5EC]
007FED27  C7 05 84 92 C9 00 01 00 00 00 mov dword ptr [0xC99284],1
007FED31  C7 04 85 40 8B C9 00 16 00 00 00 mov dword ptr [eax*4+0xC98B40],0x16 ; D3DRS_CULLMODE
007FED3C  40                   inc  eax
007FED3D  A3 EC A5 C9 00       mov  [0xC9A5EC],eax
007FED42  89 0D F0 A4 C9 00    mov  [0xC9A4F0],ecx
007FED48  B8 01 00 00 00       mov  eax,1
007FED4D  C3                   ret
```

The map at `0x884990` is identity for the three valid modes: `[0]=0, [1]=1, [2]=2, [3]=3`. The flush writes `D3DRS_CULLMODE` (`0x16`):

```
007FD769  8B 0D F0 A4 C9 00    mov  ecx,[0xC9A4F0]
007FD76F  8B 14 8D 90 49 88 00 mov  edx,[ecx*4+0x884990]
007FD776  52                   push edx
007FD777  6A 16                push 0x16
007FD779  E8 52 EB FF FF       call 0x7FC2D0
```

Bytes:

```
8B 0D F0 A4 C9 00 8B 14 8D 90 49 88 00 52 6A 16 E8 52 EB FF FF
```

This confirms:
- rw state 20 = CULLMODE, D3D state 0x16.
- The mapping is identity, so `rwCULLBACK(2)` → `D3DCULL_CW(2)`, `rwCULLNONE(1)` → `D3DCULL_NONE(1)`, `rwCULLFRONT(3)` → `D3DCULL_CCW(3)`.

The device pointer table at `0x8E24A8` holds `fpRenderStateSet = 0x7FE420`:

```
008E24A8 = 0x007FE420
008E24AC = 0x007FD810   ; fpRenderStateGet
```

This is why every inlined `RwRenderStateSet` in game code appears as `call [reg+0x20]` (RwEngineInstance + `dOpenDevice.fpRenderStateSet`) and every `RwRenderStateGet` as `call [reg+0x24]`.

---

## 6. Ranked “forced states vanilla does not expect”

The ranking is by likelihood of the reported **view-angle-dependent** ped invisibility, not by raw mismatch count. The first item is the only state that changes face rasterization; the later items change cutouts, depth testing, or blending and are listed for completeness.

### #1 — CULLMODE (prime suspect)

**Why:** It is the only state in the table that selects which geometric faces are rasterized; the other states change cutouts, depth testing, or blending. Vanilla never writes it on the ped path; it inherits the pass-level value. Our contract calls `pipeEnterCullMode()` even when `pedCullMode == -1`, and that function **force-writes the L1 rw-cache value to L2 and L3** (`pipelinecommon.cpp:1278`). There are two ways that becomes a bug: (a) the user sets `pedCullMode=1` (force NONE), or (b) the L1 cache is stale while the device is BACK, and the force flips the device to the stale value.

The vanilla pass at `0x70A960` is balanced — NONE at `0x70A9C3` for the pass body, BACK at `0x70B711` at exit. Therefore the runtime log's `[Cull] enter saved=1` spans are **consistent with** the mirror pass (the pass sets `TEXTUREADDRESS=3` at `0x70A9B4`), where NONE is correct; the `[Cull]` lines are untagged, so they are not by themselves evidence of a leak. The `[PedRS] cull rw=1` line at 22:35:40.970 is a ped span, but it may be a mirrored ped. The cull hypothesis is still #1 because it is the only face-selection state and the A/B lever makes it cheap to falsify, but it requires `pedCullMode=1` or a skipped/stale restore.

**Evidence:**
- No cull write in any ped function (scanner: 0 `push 0x14` hits and 0 state-20 `RwRenderStateSet` calls across `0x7C7B90..0x7C89C0`, `0x7C9680..0x7C9DA0`, `0x761170..0x761C90`, `0x756DF0..0x757400`, `0x5E7680..0x5E7976`, `0x534310..0x5343EE`). The compiler uses `6A 14` for every observed rwCULLMODE write in this exe, so the scan is reliable.
- `pipeEnterCullMode()` always calls `pipeForceCullMode(forced)` at `pipelinecommon.cpp:1278`, even when the saved value is the same as the device.
- Pass-level NONE writer at `0x70A9C3` (`6A 01 6A 14 FF 52 20`) and the BACK restore at `0x70B711` (`6A 02 6A 14 FF 50 20`).
- Runtime log: `[Cull] enter saved=1 forced=1`, `[Cull] exit restored=1`, and `[PedRS] ok cull rw=1 drv=1 dev=1` exist in the deployed log. The `[Cull]` lines are not tagged by pipe/atomic, so they cannot be assigned to a specific draw with certainty; the `[PedRS]` line at 22:35:40.970 is definitely a ped span and shows **cull=1**. The main-view ped spans in the 22:42 session are `cull rw=2 drv=2 dev=2`. Add a pipe/atomic tag and the device `D3DRS_CULLMODE` to the receipt before drawing conclusions from the NONE spans.

**Exact `src/core/main.cpp` lines to change:**
- `src/core/main.cpp:1736` — `savedCull = pipeEnterCullMode();`
- `src/core/main.cpp:1758` — `pipeForceCullMode(pedCullForced);` (the `pedCullMode` override)

**Minimal fix to test:** for `isPedPipe` and `pedCullMode == -1`, do **not** call `pipeEnterCullMode()`. Instead:

```cpp
RwUInt32 savedCull = rwCULLMODECULLBACK;
RwRenderStateGet(rwRENDERSTATECULLMODE, &savedCull);   // capture only
// no pipeForceCullMode here; leave L2/L3 alone
```

and only call `pipeForceCullMode` when `pedCullMode` is explicitly 1/2/3. If you prefer to keep the helper, change `pipeEnterCullMode` to skip the force when the caller is the ped pipe and the lever is `-1`; the call site line to change is `main.cpp:1736`, the helper line is `pipelinecommon.cpp:1278`.

**Diagnostic:** add a one-shot pre-entry log of `d3d9device->GetRenderState(D3DRS_CULLMODE, &devCull)` alongside the current rw saved value. The current `[Cull] enter` log prints only the L1 cache value (`saved=...`); it cannot distinguish “L1 NONE, device NONE” from “L1 NONE, device BACK.”

### #2 — ZENABLE / ZFUNC / ZWRITE (direct log mismatch)

**Why:** The log is unambiguous that the vanilla world can hand the ped callback `ZWRITE=0`, `ZENABLE=0`, `ZFUNC=8` (`ALWAYS`) — see `[PedOpaque] pre-entry dev ... ZWRITE=0 ZENABLE=0 ZFUNC=8` at 22:35:40.941. Our contract forces `TRUE / TRUE / LESSEQUAL`. Forcing `ZWRITE` on is usually safe; forcing `ZENABLE=TRUE` and `ZFUNC=LESSEQUAL` against a depth buffer that the game deliberately left untested can reject ped fragments. The rejection depends on camera depth, so this is the strongest **view-angle/depth-dependent** mismatch in the logged main-view spans.

**Exact lines:**
- `src/core/main.cpp:1085` — rw ZWRITE
- `src/core/main.cpp:1086` — rw ZTEST
- `src/core/main.cpp:1094` — L2 ZWRITE
- `src/core/main.cpp:1095` — L2 ZENABLE
- `src/core/main.cpp:1096` — L2 ZFUNC
- `src/core/main.cpp:1110` — L3 ZWRITE
- `src/core/main.cpp:1111` — L3 ZENABLE
- `src/core/main.cpp:1112` — L3 ZFUNC

**Minimal test:** snapshot the inherited Z trio in the same style as `savedOpaque` and restore it at exit; force only ZWRITE, not ZENABLE/ZFUNC. Or add an INI lever for `pedZContract=0` to compare.

### #3 — ALPHAFUNC

**Why:** The alpha-test **enable** is set per material by `0x7FE0A0`, but `ALPHAFUNC` is not: it is inherited. The log's typical inherited value is 5 (`GREATER`), while our contract forces 7 (`GREATEREQUAL`). That changes which texels pass the test at the alpha==ref edge; because the enable is per material, this is the remaining alpha mismatch at draw time.

**Exact line:** `src/core/main.cpp:1102` (L2 `D3DRS_ALPHAFUNC`), `main.cpp:1088` (L1 `rwRENDERSTATEALPHATESTFUNCTION`), `main.cpp:1115` (L3).

**Minimal fix:** preserve the inherited `ALPHAFUNC` instead of forcing 7; only repair it if it is out of domain (`< NEVER || > ALWAYS`). The `pedAlphaTestEnable=0` lever also stops our func/ref writes, but the render callback still sets the enable per material.

### #4 — SRCBLEND / DESTBLEND / BLENDOP / SEPARATEALPHA / COLORWRITE

**Why:** Vanilla does not set the blend **factors**; `0x7FE0A0` only sets the enable. Our `pipeEnterAlphaMode()` / `pipeForceAlphaBlock()` force `SRCALPHA/INVSRCALPHA/ADD`, separate alpha off, color write all. Usually safe, but can change additive ped effects or transparent overlays. The enable itself is per material and normally overrides our `ALPHABLENDENABLE` force.

**Exact call sites:** `src/core/main.cpp:1769` (`savedAlpha = pipeEnterAlphaMode();`), `src/core/main.cpp:1792` (`pipeForceAlphaBlock();`). Values in `src/render/pipelinecommon.cpp:1613-1614` (src/dst), `pipelinecommon.cpp:1684` (separate alpha), `pipelinecommon.cpp:1688` (color write), and `buildingPipe.cpp:600` (`pipeForceAlphaBlock`, BLENDOP/ALPHATEST).

**Minimal test:** keep the ped contract but skip `pipeEnterAlphaMode()`/`pipeForceAlphaBlock()` for `isPedPipe` with a new lever, or preserve the pre-entry blend tuple.

### #5 — ALPHAREF

**Why:** Vanilla `CPed::Render` itself sets `ALPHAREF=1` (`0x5E76B8`), so our `1` at `main.cpp:1089`/`1103`/`1116` is a **match** for normal peds. It only overrides the `CClothes::Render` special-model `0` (`0x53437C`). Low risk; do not change unless the CClothes sub-pass shows artifacts.

**Exact lines:** `src/core/main.cpp:1089` (L1), `src/core/main.cpp:1103` (L2), `src/core/main.cpp:1116` (L3).

**Not ranked:** `ALPHATESTENABLE`, `ALPHABLENDENABLE`, and `VERTEXALPHAENABLE` are set per material by `0x7FE0A0` at `0x756F0F`/`0x7570F1`; our entry force is normally overwritten before the draw. They are not draw-time mismatches unless the handler cache-skips or the callback is bypassed.

---

## 7. Verification plan

1. **Cull A/B (highest priority)**
   - Set `pedCullMode=2` (force BACK) and run the same view/route that shows the symptom.
   - Set `pedCullMode=1` (force NONE) and repeat.
   - Set `pedCullMode=-1` (current default) and repeat.
   - If `2` fixes but `1` reproduces, cull is confirmed. If all three behave the same, cull is not the cause and move to #2.
   - Add the device-cull log described in #1; correlate `[PedRS] cull rw=1` frames with the invisibility screenshots.

2. **Z A/B**
   - Add a temporary `pedZContract=0` that captures `D3DRS_ZWRITEENABLE`, `D3DRS_ZENABLE`, `D3DRS_ZFUNC` at entry and restores them at exit without forcing. Compare.
   - If forcing Z is the trigger, keep ZWRITE repair but preserve the inherited ZENABLE/ZFUNC.

3. **ALPHAFUNC A/B**
   - The alpha-test **enable** is per material (`0x7FE0A0`), so `pedAlphaTestEnable=0` only removes our `ALPHAFUNC`/`ALPHAREF` writes; it does not disable the material's alpha test.
   - Compare the inherited `ALPHAFUNC` (typically 5/GREATER) against our 7/GREATEREQUAL, and look for edge/cutout differences.

4. **Blend-factor A/B**
   - The blend **enable** is per material (`0x7FE0A0`); our `ALPHABLENDENABLE` force is normally overwritten. Compare only the factors (`SRCBLEND`/`DESTBLEND`/`BLENDOP`) and the separate-alpha/color-write block for translucent clothing/hair.

5. **Receipts to add**
   - `[PedCull]` currently logs only when `pedCullMode` changes; extend it to log the **pre-entry device** `D3DRS_CULLMODE` and the **post-force** value, plus whether the L1 cache matched the device.
   - `[PedRS]` already logs rw/drv/dev for cull/blend/src/dst/zw/afunc/aref; add `zen`/`zfunc` and `atest` so the Z/alpha force is visible at exit.
   - Add the per-material `0x7FE0A0` result (blend/test pair) to the draw receipt; the current exit receipt cannot show what the render callback did per mesh.

6. **Acceptance**
   - Ped still visible from all camera angles with `pedOpaqueContract=1`, `pedAlphaTestEnable=1`, `pedCullMode=-1` (i.e., no visual regression), **or** the minimal contract change (skip cull force / preserve inherited Z and ALPHAFUNC) is proven to restore visibility without reintroducing the original black/ghost peds.

---

## 8. Appendix — byte evidence index

All code addresses are first `.text` (`raw = VA - 0x400C00`). Data addresses use the section map.

### A. Hook target and pipeline

| Claim | VA | Bytes |
|---|---|---|
| `AtomicDefaultRenderCallBack` | `0x7491C0` | `56 8B 74 24 08 8B 46 6C 85 C0 75 0F A1 24 7B C9 00 8B 0D 60 BC C9 00 8B 44 01 3C 6A 01 56 50 E8 2C C5 0B 00 83 C4 0C F7 D8 1B C0 23 C6 5E C3 90` |
| `RxPipelineExecute` call | `0x7491DF` | `E8 2C C5 0B 00` |
| skin node body call (VS) | `0x7C8028` | `E8 33 00 00 00` → `0x7C8060` |
| skin node body call (fallback) | `0x7C8049` | `E8 62 05 00 00` → `0x7C85B0` |
| skin node definition | `0x8DED08` | `E8 EC 8D 00 90 7B 7C 00 00 00 00 00 00 00 00 00 40 B2 7C 00` |
| standard node definition | `0x8D6318` | name `0x8D6318`, body `0x7575F0`, pipes `0x757890` (see §1.3) |
| skin callback installer | `0x7CB240` | `8B 44 24 04 33 C9 8B 40 14 C7 00 F0 6D 75 00 C7 40 04 00 74 75 00 ...` |

### B. State writes that do exist

| Claim | VA | Bytes |
|---|---|---|
| CLIPPING=TRUE (skin) | `0x7C80A0` | `6A 01 68 88 00 00 00 8A 4B 10 02 C9 32 C8 80 E1 0E 32 C1 88 44 24 2A E8 14 42 03 00` |
| NORMALIZENORMALS=0/1 | `0x7C86BB` | `6A 01 EB 02 6A 00 68 8F 00 00 00 E8 05 3C 03 00` |
| per-material alpha pair call (FFP) | `0x756F0F` | `E8 8C 71 0A 00` → `0x7FE0A0` |
| per-material alpha pair call (VS) | `0x7570F1` | `E8 AA 6F 0A 00` → `0x7FE0A0` |
| handler queues ALPHABLENDENABLE | `0x7FE0EA` | `C7 04 8D 40 8B C9 00 1B 00 00 00` |
| handler queues ALPHATESTENABLE | `0x7FE178` | `C7 04 8D 40 8B C9 00 0F 00 00 00` |
| CLIPPING in default cb | `0x756E4E` | `33 D2 3B C7 0F 94 C2 52 68 88 00 00 00 E8 70 54 0A 00` |
| TEXTUREFACTOR (FFP) | `0x756F14` | `68 00 00 00 FF 6A 3C E8 B0 53 0A 00` |
| DITHERENABLE=0 (FFP) | `0x756F20` | `57 6A 1A E8 A8 53 0A 00` |
| SHADEMODE=1 (FFP) | `0x756F28` | `6A 01 6A 09 E8 9F 53 0A 00` |
| restore DITHERENABLE | `0x75707B` | `E8 50 52 0A 00` |
| restore SHADEMODE | `0x757087` | `8B 44 24 28 50 6A 09 E8 44 52 0A 00` |
| TEXTUREFACTOR (VS) | `0x757144` | `50 6A 3C E8 87 51 0A 00` |
| `SetVertexDeclaration` | `0x7C818E` | `FF 92 5C 01 00 00` |
| CPed ALPHAREF get/set | `0x5E769E` | `8B 0D 24 7B C9 00 8D 44 24 0C 50 6A 1E FF 51 24 8B 15 24 7B C9 00 6A 01 6A 1E FF 52 20 83 C4 10` |
| CPed ALPHAREF restore | `0x5E795C` | `8B 54 24 14 A1 24 7B C9 00 52 6A 1E FF 50 20 83 C4 08` |
| CClothes ALPHAREF=0 | `0x534362` | `8B 0D 24 7B C9 00 8D 44 24 04 50 6A 1E FF 51 24 8B 15 24 7B C9 00 6A 00 6A 1E FF 52 20 83 C4 10` |
| CClothes ALPHAREF restore | `0x5343D1` | `8B 44 24 04 8B 0D 24 7B C9 00 50 6A 1E FF 51 20` |
| fog get (FOGENABLE) | `0x7CB1D3` | `8B 0D 24 7B C9 00 8D 44 24 10 50 6A 0E FF 51 24` |
| fog get (FOGTYPE) | `0x7CB1EE` | `A1 24 7B C9 00 8D 54 24 08 52 6A 10 FF 50 24` |

### C. The building / vehicle “two-sided” branch is texture address, not cull

| Claim | VA | Bytes |
|---|---|---|
| building material bit | `0x5D6559` | `8A 58 10 ... 80 E3 01` |
| building state 2 = 1 | `0x5D6583` | `8B 15 24 7B C9 00 6A 01 6A 02 FF 52 20` |
| vehicle state 2 = 1 (a) | `0x5D9B4A` | `8B 0D 24 7B C9 00 6A 01 6A 02 FF 51 20` |
| vehicle state 2 = 1 (b) | `0x5D9CB8` | `A1 24 7B C9 00 6A 01 6A 02 FF 50 20` |
| state 2 handler (SetSamplerState) | `0x7FE6DE` | `... FF 92 14 01 00 00` |

### D. Pass-level cull

| Claim | VA | Bytes |
|---|---|---|
| pass setup, ZWRITE=0 | `0x70A977` | `53 6A 08 FF 50 20` |
| pass setup, ZTEST=1 | `0x70A98F` | `6A 01 6A 06 FF 51 20` |
| pass setup, VERTEXALPHA=1 | `0x70A99C` | `6A 01 6A 0C FF 52 20` |
| pass setup, FOG=0 | `0x70A9A8` | `53 6A 0E FF 50 20` |
| pass setup, TEXTUREADDRESS=3 | `0x70A9B4` | `6A 03 6A 02 FF 51 20` |
| pass setup, **CULLNONE=1** | `0x70A9C1` | `6A 01 6A 14 FF 52 20` |
| pass exit, **BACK restore=2** | `0x70B711` | `6A 02 6A 14 FF 50 20` |
| pass setup, ALPHAREF=0 | `0x70A9CD` | `53 6A 1E FF 50 20` |
| pass setup, TEXTUREFILTER=2 | `0x70A9D9` | `6A 02 6A 09 FF 51 20` |
| two-sided pass, FRONT=3 | `0x7114B9` | `6A 03 6A 14 FF 51 20` |
| two-sided pass, BACK=2 | `0x7114EF` | `6A 02 6A 14 FF 50 20` |
| two-sided pass, BACK exit | `0x7116C9` | `6A 02 6A 14 FF 51 20` |

### E. rw state 20 → D3D CULLMODE

| Claim | VA | Bytes |
|---|---|---|
| `RwRenderStateSet` dispatcher | `0x7FE420` | `8B 4C 24 04 53 33 DB 33 C0 49 57 83 F9 1D 0F 87 BE 05 00 00 FF 24 8D F8 E9 7F 00` |
| jump table base | `0x7FE9F8` | `D1 E7 7F 00 DE E6 7F 00 48 E7 7F 00 83 E7 7F 00 CF E8 7F 00 F2 E7 7F 00 9F E8 7F 00 E2 E7 7F 00 C1 E7 7F 00 02 E8 7F 00 51 E8 7F 00 AF E8 7F 00 BF E8 7F 00 3B E4 7F 00 CD E4 7F 00 15 E5 7F 00 80 E6 7F 00 F2 E9 7F 00 F2 E9 7F 00 DD E8 7F 00` |
| state 20 handler | `0x7FE8DD` | `8B 4C 24 10 51 E8 19 04 00 00 83` |
| cull map | `0x884990` | `00 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00` |
| cull queue + D3DRS_CULLMODE | `0x7FED00` | `8B 4C 24 04 A1 F0 A4 C9 00 3B C8 74 3B 8B 04 8D 90 49 88 00 A3 80 92 C9 00 ... C7 04 85 40 8B C9 00 16 00 00 00 ...` |
| cull flush | `0x7FD769` | `8B 0D F0 A4 C9 00 8B 14 8D 90 49 88 00 52 6A 16 E8 52 EB FF FF` |
| state 30 → D3DRS_ALPHAREF | `0x7FE9DE` | `8B 54 24 10 52 6A 18 E8 E6 D8 FF FF` |
| device table fpRenderStateSet | `0x8E24A8` | `20 E4 7F 00` = `0x7FE420` |

### F. Current contract values (source, not exe)

| State | Forced value | Exact source line |
|---|---|---|
| VERTEXALPHAENABLE | FALSE | `src/core/main.cpp:1084` |
| ZWRITE | TRUE | `src/core/main.cpp:1085`, `1094`, `1110` |
| ZTEST/ZENABLE | TRUE | `src/core/main.cpp:1086` (L1 ZTEST), `1095` (L2), `1111` (L3) |
| ZFUNC | LESSEQUAL (4) | `src/core/main.cpp:1096` (L2), `1112` (L3) |
| ALPHATESTENABLE | TRUE (lever 1) | `src/core/main.cpp:1101` (L2), `1114` (L3) |
| ALPHAFUNC | GREATEREQUAL (7) | `src/core/main.cpp:1102` (L2), `1115` (L3) |
| ALPHAREF | 1 | `src/core/main.cpp:1089` (L1), `1103` (L2), `1116` (L3) |
| ALPHABLENDENABLE | FALSE | `src/core/main.cpp:1093` (L2), `1109` (L3) |
| SRCBLEND | SRCALPHA (5) | `pipelinecommon.cpp:1613`, called from `main.cpp:1769` |
| DESTBLEND | INVSRCALPHA (6) | `pipelinecommon.cpp:1614`, called from `main.cpp:1769` |
| BLENDOP | ADD (1) | `buildingPipe.cpp:600` via `main.cpp:1792` |
| SEPARATEALPHABLENDENABLE | FALSE | `pipelinecommon.cpp:1684`, called from `main.cpp:1769` |
| COLORWRITEENABLE | ALL (0xF) | `pipelinecommon.cpp:1688`, called from `main.cpp:1769` |
| CULLMODE | saved L1 / pedCullMode | `main.cpp:1736` / `main.cpp:1758`; helper `pipelinecommon.cpp:1278` |

Note: `ALPHATESTENABLE` / `ALPHABLENDENABLE` / `VERTEXALPHAENABLE` are normally overwritten per material by `0x7FE0A0` after these entry writes; the table shows the entry values, not the final per-mesh values.

---

## 9. One-line conclusion

Vanilla peds **do not establish a full render-state contract**: they set `ALPHAREF=1` in `CPed::Render`, CLIPPING/NORMALIZENORMALS in the skin render, and a per-material `ALPHABLENDENABLE`/`ALPHATESTENABLE` pair through `0x7FE0A0`. They **inherit** CULLMODE, ZWRITE, ZENABLE, ZFUNC, ALPHAFUNC, and the blend factors. Our ped contract force-writes all of those. `CULLMODE` is the only face-selection state and therefore the #1 test, but the vanilla NONE pass restores BACK at `0x70B711`; the direct draw-time mismatches in the 22:35 session are the Z trio (`ZWRITE=0 ZENABLE=0 ZFUNC=8` on entry) and `ALPHAFUNC` (5 → 7). Test `pedCullMode=2`, then `pedZContract=0`, then preserve the inherited `ALPHAFUNC`. If cull is the cause, stop force-writing cull for peds when `pedCullMode == -1`.
