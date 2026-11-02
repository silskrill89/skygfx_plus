# Wrapper Migration Plan — RW clump/atomic/geometry/material (2026-11-02)

Follow-up pass plan for migrating TU-local address casts/typedefs onto the proper
`WRAPPER` definitions in `src/rw/gta.cpp`. Additive pass only — no call sites were
migrated in the 2026-11-02 wrapper pass (sftbdyphsx.cpp is owned by another lane).

## What changed in gta.cpp (this pass)

Fixed the bogus `0x748xxx` wrapper block (all four were **mid-function game code**,
byte-verified against the 1.0 US exe — none were RW function entries):

| Wrapper | Old (wrong) | New (byte-verified) | Note |
|---------|------------|--------------------|------|
| `RpClumpStreamRead` | 0x748A10 | **0x74B420** | plugin-sdk `RenderWare.cpp:2361`; prologue `83 ec 44 …` |
| `RpClumpDestroy` | 0x748C50 | **0x74A310** | plugin-sdk `RenderWare.cpp:2288`; prologue pushes clump + plugin-dtor reg 0x8D6264 |
| `RpClumpForAllAtomics` | 0x748E90 | **0x749B70** | plugin-sdk `RenderWare.cpp:2240`; prologue walks list at clump+8 |

New wrappers added (all byte-verified: exe prologue + plugin-sdk `RenderWare.cpp`
+ RE vault `[[Tools and SDKs/plugin-sdk Reference]]`):

| Wrapper | Address | Signature (matches rpworld.h decl, extern "C" inherited) |
|---------|---------|----------------------------------------------------------|
| `RpClumpAddAtomic` | 0x74A490 | `RpClump *(RpClump*, RpAtomic*)` |
| `RpClumpRemoveAtomic` | 0x74A4C0 | `RpClump *(RpClump*, RpAtomic*)` |
| `RpAtomicCreate` | 0x749C50 | `RpAtomic *(void)` |
| `RpAtomicDestroy` | 0x749DC0 | `RwBool(RpAtomic*)` |
| `RpAtomicSetGeometry` | 0x749D40 | `RpAtomic *(RpAtomic*, RpGeometry*, RwUInt32)` |
| `RpGeometryCreate` | 0x74CA90 | `RpGeometry *(RwInt32, RwInt32, RwUInt32)` |
| `RpGeometryDestroy` | 0x74CCC0 | `RwBool(RpGeometry*)` |
| `RpMaterialCreate` | 0x74D990 | `RpMaterial *(void)` |

Already existed with matching tree-verified addresses (no change):
`RwFrameCreate` 0x7F0410 (gta.cpp:178), `RwFrameUpdateObjects` 0x7F0910
(gta.cpp:194), `_rwObjectHasFrameSetFrame` 0x804EF0 (gta.cpp:179).

## What the wrong addresses actually are (byte forensics, 1.0 US exe)

- **0x748C50** — NOT `RpAtomicGetClump` (that is a macro in rpworld.h reading
  `atomic->clump`; the SA exe has no standalone getter — fully inlined). It is
  mid-function game code: bit-test-and-store
  (`mov eax,[eax+24h]; shr eax,11h; and al,1; mov [0xC87FFC],al; jmp +13Eh`)
  inside a float-math (FLD/FSTP) game routine. Calling it as `RpClumpDestroy`
  executes arbitrary mid-function code with a garbage "clump" argument → UB.
- **0x748E90** — mid-function game code (`add esp,8; push eax; call [0x858270]`
  — an IAT import call) inside float-math game code. Not `RpClumpForAllAtomics`.
- **0x748A10** — mid-function float-math game code. Not `RpClumpStreamRead`.
- **0x749AB0** — mid-function inside the routine starting ~0x749A72 (reads
  object privateFlags `[esi+3]`, copies matrix rows) in the clump-TU region
  (vault map: clump TU 0x749970–0x74A4C0, `RpClumpRender` 0x749B20). Not
  `RpAtomicGetGeometry`.

Why nothing crashed from the wrong `RpClumpDestroy`/`RpClumpStreamRead`/
`RpClumpForAllAtomics` wrappers: **zero consumers** of those gta.cpp wrappers
(grep-verified). The live wrong-target calls are the TU-local casts listed below.

## Migration targets (mechanical follow-up)

### P0 — wrong-address casts, fix before/with migration (live UB)

| File:Line | Site | Current target | Correct target |
|-----------|------|----------------|----------------|
| src/entities/sftbdyphsx.cpp:629 | `#define RpClumpDestroy(c) ((int(*)(void*))0x748C50)(c)` | 0x748C50 (mid-function game code) | gta.cpp wrapper `RpClumpDestroy` → 0x74A310 |
| src/entities/sftbdyphsx.cpp:719, 738, 760, 1064 | call sites of that macro | — | inherit fix |
| src/entities/sftbdyphsx.cpp:781 | `((ForAllAtomics_t)0x748E90)(pedClump, cb0, &firstAtomic)` | 0x748E90 (mid-function) | gta.cpp wrapper `RpClumpForAllAtomics` → 0x749B70 |
| src/entities/sftbdyphsx.cpp:1147 | `((ForAllAtomics_t)0x748E90)(g->clump, cbRend, &firstAtomic)` | 0x748E90 (mid-function) | same |
| src/texdefs.cpp:811 | `game_RpAtomicGetGeometry = (…)0x749AB0` (call site :922) | 0x749AB0 (mid-function) | no SA exe function exists (inlined) — use the rpworld.h macro `RpAtomicGetGeometry(atomic)` / `atomic->geometry` as texdefs.cpp:915 already does |

### P1 — correct-address TU-local macros → migrate to gta.cpp wrappers

sftbdyphsx.cpp macro block (lines 613–640) — delete macro + typedef, call the
wrapper directly (signatures already match rpworld.h):

| Line | Macro | Address | Wrapper |
|------|-------|---------|---------|
| 614 | `RpGeometryCreate(nv,nt,fmt)` | 0x74CA90 | `RpGeometryCreate` (new) |
| 616 | `RpGeometryDestroy(g)` | 0x74CCC0 | `RpGeometryDestroy` (new) |
| 623 | `RpAtomicCreate()` | 0x749C50 | `RpAtomicCreate` (new) |
| 625 | `RpAtomicDestroy(a)` | 0x749DC0 | `RpAtomicDestroy` (new) |
| 632 | `RwFrameCreate()` | 0x7F0410 | `RwFrameCreate` (gta.cpp:178) |
| 634 | `RwFrameUpdateObjects(f)` | 0x7F0910 | `RwFrameUpdateObjects` (gta.cpp:194) |
| 637 | `RpMaterialCreate()` | 0x74D990 | `RpMaterialCreate` (new) |
| 640 | `RpAtomicSetGeometry(a,g,f)` | 0x749D40 | `RpAtomicSetGeometry` (new) |

sftbdyphsx.cpp raw casts:

| Line | Cast | Address | Wrapper |
|------|------|---------|---------|
| 968 | `((ObjFrameSet_t)0x804EF0)(atomic, frame)` | 0x804EF0 | `_rwObjectHasFrameSetFrame` (gta.cpp:179) |

texdefs.cpp static function-pointer casts:

| Line | Var | Address | Wrapper |
|------|-----|---------|---------|
| 850 | `game_RwFrameCreate` | 0x7F0410 | `RwFrameCreate` (gta.cpp:178) |
| 852 | `game_RwObjectHasFrameSetFrame` | 0x804EF0 | `_rwObjectHasFrameSetFrame` (gta.cpp:179) |
| 853 | `game_RwFrameUpdateObjects` | 0x7F0910 | `RwFrameUpdateObjects` (gta.cpp:194) |
| 812 | `game_GetPipelineID` | 0x72FC40 | `gtaGetPipelineID` (gta.cpp:158) |
| 814 | `game_RpMatFXMaterialGetEffects` | 0x812140 | `RpMatFXMaterialGetEffects` (gta.cpp:96) |
| 815 | `game_RpGeometryForAllMaterials` | 0x74C790 | `RpGeometryForAllMaterials` (gta.cpp:206) |

### P2 — optional / needs new wrappers first

| File:Line | Site | Address | Status |
|-----------|------|---------|--------|
| src/entities/sftbdyphsx.cpp:618 | `RpGeometryLock(g,m)` macro | 0x74C7D0 | byte-verified prologue + plugin-sdk:2039; add `WRAPPER RpGeometry *RpGeometryLock(RpGeometry*, RwInt32)` |
| src/entities/sftbdyphsx.cpp:620 | `RpGeometryUnlock(g)` macro | 0x74C800 | byte-verified prologue + plugin-sdk:2043; add `WRAPPER RpGeometry *RpGeometryUnlock(RpGeometry*)` |
| src/entities/sftbdyphsx.cpp:627 | `RpClumpCreate()` macro | 0x74A290 | byte-verified prologue + plugin-sdk:2291; add `WRAPPER RpClump *RpClumpCreate(void)` |
| src/entities/sftbdyphsx.cpp:645–646 | `IMM_STORE()/IMM_SET()` macros | 0x700CC0 / 0x700D70 | game immediate-mode RS helpers; verify before wrapping |
| src/texdefs.cpp:813 | `game_RpMatFXAtomicQueryEffects` | 0x811C30 | unverified; check before wrapping |
| src/core/main.cpp:4281 | `CreateRoadsignAtomicA_RpAtomicSetGeometry_orig` EAXJMP | 0x749D40 | correct; may delegate to the new wrapper (hook-orig trampoline, low priority) |

### Getter macros (no exe function exists — do NOT wrap)

`RpAtomicGetClump`, `RpAtomicGetFrame`, `RpClumpGetFrame`, `RpAtomicGetGeometry`
are macros in rpworld.h (direct field reads); the SA exe inlined them (no
standalone getter found 0x744000–0x757000). Callers already use the macro form
(vehiclePipe.cpp:415/447, buildingPipe.cpp:206, chars.cpp:69, texdefs.cpp:793).
Keep as macros; wrapping them is impossible without a verified address.

## Verification done this pass

- Every new/changed address: prologue byte-check in `E:\games\gtasa_skygfx_plus\gta_sa.exe`
  (PE-mapped, cross-checked against 6 known-good anchors incl. 0x7EE190
  RwCameraBeginUpdate jump-thunk) + plugin-sdk `RenderWare.cpp` + RE vault
  `[[Tools and SDKs/plugin-sdk Reference]]` / `[[RenderWare/RenderWare SDK Notes]]`.
- `cl /Zs` syntax pass on gta.cpp + consumer TUs (see commit/PR notes).
