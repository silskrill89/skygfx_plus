# helper_tex.txd — helper-texture packaging pipeline (directive m0061)

Single shipped RenderWare texture dictionary hosting **all** custom textures
the effects need (stacked-cloud cards, noise tiles, future LUTs), so they
live in one file that can be dropped into `models/` or modloader.

> Spec (m0061, `.serena/memories/round6-directives-2026-10-08.md`):
> *"if there is any textures you need, find a way to pack them into a new
> tex called 'helper_tex.txd' which we will ship, that can be paste in
> models folder or in modloader and it loads proper and that's where we add
> all the custom stuff like the stacked cloud solution we proposed and all
> other textures that help improve or are requested to make all the effects
> work properly and as efficiently as possible."*

## TL;DR — how to run it

```bash
# 1. stage source art (PNG/DDS/BMP) into assets/helper_tex/
#    (optionally declare slots in assets/helper_tex/manifest.json)

# 2. pack -> assets/helper_tex/helper_tex.txd  (auto-verifies round-trip)
python tools/pack_helper_tex.py                # or:
python tools/fast_build.py --helper-tex        # same thing, build-script hook

# useful variants
python tools/pack_helper_tex.py --list             # inventory only
python tools/pack_helper_tex.py --verify           # parse back the output TXD
python tools/pack_helper_tex.py --verify X.txd     # parse ANY TXD (debugger)
python tools/pack_helper_tex.py --strict           # fail on missing/TBD slots
python tools/pack_helper_tex.py --gen-placeholders # (re)generate placeholder art
python tools/pack_helper_tex.py --emit-magic-script out.txt  # Magic.TXD GUI checklist
```

Output: `assets/helper_tex/helper_tex.txd`. No game/DLL build involved —
the packer is pure Python (needs `Pillow` for PNG input, `numpy` only for
`--gen-placeholders`).

## Pipeline

```
assets/helper_tex/*.png|dds|bmp     manifest.json (slot names, mips, filter, addressing)
        \                               /
         \--> tools/pack_helper_tex.py <--
                    |
                    v
        assets/helper_tex/helper_tex.txd   (RW 3.6 / GTA SA PC TXD)
                    |
        deploy: game models\  OR  modloader folder  (see "Deployment" below)
                    |
                    v
        ASI loader (future lane) / TXD pool lookup by name
```

1. **Stage** — drop PNGs/DDS into `assets/helper_tex/`. Naming: `snake_case`,
   1..31 chars `[A-Za-z0-9_]` (RW name field is 32 bytes incl. NUL).
   `lens dirt.png` → `lens_dirt.png`.
2. **Declare** — add the slot to `manifest.json` (`mips`, `filter`,
   `addressing`, `note`). File-stem fallback works without a manifest entry.
3. **Pack** — `python tools/pack_helper_tex.py`. The tool packs, then always
   round-trips its own output through the built-in parser and fails loudly
   if any chunk size/version disagrees.
4. **Ship** — copy `helper_tex.txd` to the game (see Deployment).

Slot inventory + placeholder art are documented in
`assets/helper_tex/README.md`.

## TXD format used (and evidence it is correct)

The packer writes the **GTA SA PC / RenderWare 3.6.0.3 D3D9 stream TXD**
directly (`tools/pack_helper_tex.py`, pure-Python writer). Layout, all
cross-checked byte-for-byte against real TXDs the mod already loads:

| Field | Value | Evidence |
|---|---|---|
| container chunk | `rwID_TEXDICTIONARY` = **0x16** ("struct/extension/texture-native" stream) | matches `RwStreamFindChunk(rwID_TEXDICTIONARY)` in `src/texdefs.cpp:542` |
| chunk library ID | **0x1803FFFF** on every chunk | `particle.txd`, `Mobile_details.txd`, `plant1.txd` |
| dict struct | `<count:u16, deviceID:u16>`, deviceID **2** (D3D9) | all three real files |
| native struct | 88 bytes: platform **9** (D3D9), name/mask 32+32, rasterFormat, d3dFormat, w, h, depth, mipLevels, rasterType **4** (TEXTURE), flags | Magic.TXD vendored `txdwrite.d3d9.cpp` + real files |
| pixel data | per mip: `u32 size` + data padded to 4 | real files |
| alpha/flags | bit0 = hasAlpha, bit3 = isNotRwCompatible (DXT only) | `plant1.txd` DXT3 sets 0x8, `Mobile_details` A8L8 does not |
| raster formats | A8R8G8B8 (**D3DFMT 21**), L8 (50), A8L8 (51), DXT1/3/5 passed through | `particle.txd` 8888 rows, `Mobile_details` LUM8 mips, `plant1` DXT3 |

**Validation run (2026-10-09):**

```
python tools/pack_helper_tex.py --verify E:/games/gtasa_skygfx_plus/models/particle.txd
    -> verify: OK (44 textures, 0 errors)
python tools/pack_helper_tex.py --verify .../models/Mobile_details.txd
    -> verify: OK (14 textures, 0 errors)
python tools/pack_helper_tex.py --verify .../models/grass/plant1.txd
    -> verify: OK (9 textures, 0 errors)
python tools/pack_helper_tex.py            # pack + self round-trip
    -> wrote helper_tex.txd (4,723,600 bytes); verify: OK (6 textures, 0 errors)
```

The same parser reads our output and shipping game files with zero errors
in both directions — that is the correctness argument. (In-game behaviour
is NOT tested: m0061 status = build/fix only, no game test before user pass.)

Not used: `TWAT` (that is GTA III/VC-era magic — wrong for SA) and the
Magic.TXD GUI (the checkout at `E:\SDKs\Magic.TXD` has no CLI entry point;
`--emit-magic-script` emits a manual import checklist as a fallback).

## How the game loads TXDs today (reference for the loader lane)

skygfx reads TXD files itself with the RW stream API — two identical,
battle-tested examples:

- `src/texdefs.cpp` `TexDefsLoadDatabase()` (:536-566):
  `getpath("models\\Mobile_details.txd")` → `RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path)`
  → `RwStreamFindChunk(stream, rwID_TEXDICTIONARY, ...)` → `RwTexDictionaryStreamRead(stream)`
  → `RwStreamClose` → `RwTexDictionarySetCurrent(detailTxd)` (safe because it
  runs **before `CGame::Initialise`**, which installs `generic` as current) →
  then plain `RwTextureRead("name", NULL)` for each texture.
- `src/render/neo.cpp` `neoInit()` (:8-32): same pattern for `neo\neo.txd`,
  then `RwTextureRead("dropmask", NULL)`.

TXD **pool** path (game-side): skygfx hooks `CTxdStore` in
`texdefs.cpp TexDefsInit()` — addTxdSlot (0x731CCB), setCurrentTxd (0x7319EB),
find callback (0x731FD5) — so any TXD the game loads by name is visible to
`TexDefsFindCB`, and per-TXD `TexInfo` (alphamode/detail/noiseDetail) is
attached there. A TXD sitting in `models\` is only pulled into the pool when
the game/streaming actually requests it (DFF reference or explicit load) —
a brand-new name like `helper_tex` is **not** auto-loaded by the pool.

**Status: no C++ code loads `helper_tex.txd` yet** (verified: no
`helper_tex` reference in `src/`). The natural hookup for the loader lane
mirrors `TexDefsLoadDatabase()`: open `getpath("helper_tex.txd")` (or
`models\\helper_tex.txd`), stream-read the dict, `RwTexDictionarySetCurrent`
early at init (or keep the dict and read from it by name). Until then the
TXD ships and the pipeline that produces it is complete.

## Deployment (per m0061)

- Copy `assets/helper_tex/helper_tex.txd` to
  `E:\games\gtasa_skygfx_plus\models\helper_tex.txd`
  **or** into a modloader folder — spec says either "loads proper" once the
  ASI-side load hook exists.
- Re-ship whenever staging art changes: repack (step 3) then copy again.
- No deploy happened from this lane (build/deploy explicitly out of scope).

## build integration

`tools/fast_build.py --helper-tex` runs the packer as a standalone
early-return mode (same shape as `--shaders` / `--deploy` /
`--check-shaders`): no FXC, no MSBuild, no deploy. Unknown flags are
forwarded to the packer (`--helper-tex --verify`, `--helper-tex --strict`).
Exit code propagates (packing failure fails the build script).
