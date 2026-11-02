# assets/helper_tex/ — staging area for helper_tex.txd (directive m0061)

Drop source textures here, run the packer, ship ONE `helper_tex.txd`
(hosts the stacked-cloud cards + every custom helper texture the effects
need: noise tiles, LUTs, BRDF LUT, ...).

## Add a texture

1. Put the file in this folder: `mytexture.png` (or `.dds` / `.bmp`).
2. Add a slot to `manifest.json` under `"textures"`:

   ```json
   "mytexture": { "file": "mytexture.png", "mips": true,
                  "filter": "linearmip", "addressing": "wrap",
                  "note": "what it is / which shader samples it" }
   ```

   The slot key becomes the texture name inside the TXD — that is the exact
   string shaders/C++ will look up (`RwTextureRead("mytexture", NULL)`).
   If you skip the manifest entry, the file stem is used as-is.
3. Pack: `python tools/pack_helper_tex.py` (or `python tools/fast_build.py
   --helper-tex`). Output: `assets/helper_tex/helper_tex.txd`.

## Naming rules (enforced by the packer)

- **1..31 characters of `[A-Za-z0-9_]`** — RW stores the name in a 32-byte
  field incl. NUL. No spaces (so `lens dirt.png` → `lens_dirt.png`).
- One name per texture; duplicates abort the pack.
- Max practical size: power-of-two dimensions (RW/D3D9 friendly, required
  for a clean mip chain); the packer will happily pack NPOT but the game
  prefers POT.

## Input formats

| Input | Result |
|-------|--------|
| PNG RGBA | A8R8G8B8 (D3DFMT 21), B,G,R,A byte order in the TXD |
| PNG L / LA | L8 / A8L8 |
| DDS DXT1/3/5 | passed through with all stored mips |
| DDS uncompressed 565/1555/4444/8888/L8/A8L8 | passed through |
| DXGI (DX10 header), cube, volume DDS | rejected with an error — flatten first |

Manifest keys per slot: `file`, `mips` (box-filter mip chain generated),
`format` (`rgba8888|l8|a8l8`), `filter` (`nearest|linear|linearmip`),
`addressing` (`wrap|mirror|clamp|border`), `tbd` (placeholder art),
`note`.

## Current inventory

| Texture | Size | Status |
|---------|------|--------|
| cloud_layer0/1/2 | 512² RGBA, 10 mips | [TBD ART] stacked-cloud cards (procedural placeholder) |
| cloud_noise | 256² RGBA | [TBD ART] R=density G=thickness B=edge A=dither |
| utility_noise | 256² RGBA | functional: R=Perlin G=Worley B=white A=Bayer |
| blue_noise_64 | 64² L8 | FusionFix volume slice 0 (or proxy) |

Reserved (declared, not yet packed): `ssr_brdf_lut`, `tbn_noise`,
`smaa_area_tex` — see `manifest.json`.

Regenerate the placeholder art with:

```bash
python tools/pack_helper_tex.py --gen-placeholders
```

(no-op if you already have real art — just overwrite the PNGs and repack).

See `docs/helper_tex.md` for the full pipeline + how the TXD is loaded.
