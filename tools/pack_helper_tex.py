#!/usr/bin/env python3
# ============================================================================
# pack_helper_tex.py — build helper_tex.txd (RW 3.6 stream TXD, GTA SA PC)
#
# Packs every texture staged in assets/helper_tex/ (PNG, BMP or DDS) into ONE
# RenderWare 3.6 tex dictionary named helper_tex.txd, which the ASI loads at
# init (see docs/helper_tex.md). Texture names inside the TXD are the exact
# names shaders will sample (file stem, or manifest "name" override).
#
# TOOL PATH TAKEN (honest summary):
#   * Primary: native pure-Python TXD writer in THIS file. No third-party TXD
#     library is used (none is installed; and the Magic.TXD checkout at
#     E:\SDKs\Magic.TXD is a Qt GUI + magictxd.lib embedding API — its
#     src/main.cpp has no CLI entry point). The byte layout is implemented
#     from the Magic.TXD vendored magic-rw serializer
#     (vendor/rwlib/src/txdwrite.cpp, txdwrite.d3d9.cpp, txdread.d3d9.hxx)
#     and cross-checked byte-for-byte against real GTA SA TXDs that this very
#     mod loads at runtime (models/grass/plant1.txd, neo/neo.txd,
#     models/Mobile_details.txd) — chunk version 0x1803FFFF, dict deviceID 2,
#     88-byte D3D9 native header, per-mip u32 size + data, empty EXTENSION
#     chunks. `--verify` runs a built-in round-trip parser.
#   * Fallback: `--emit-magic-script` writes a Magic.TXD GUI import checklist
#     (human/agent driven) for anyone who wants to rebuild/inspect the TXD
#     with the GUI tool instead. It is NOT automated — no CLI exists in this
#     Magic.TXD tree.
#   * In-game validation is NOT done here (no game run in this lane); the
#     output format is validated against the parser + the real TXDs above.
#
# Formats:
#   PNG  -> uncompressed: RGBA32 (A8R8G8B8, BGRA byte order), L8 (grayscale),
#           A8L8 (grayscale+alpha). Optional box-filter mip chain via
#           manifest "mips": true.
#   DDS  -> passed through: DXT1/DXT3/DXT5 (all provided mips) and
#           uncompressed L8 / A8L8 / A8R8G8B8 / B8G8R8A8 / X8R8G8B8 /
#           565 / 1555 / 4444. DXGI (DX10 header) and volume/cube DDS are
#           rejected with a clear error (RW 3.6 TXDs have no volume rasters —
#           flatten a 3D volume to a 2D slice/atlas first, see
#           --gen-placeholders for how bluenoisevolumeL8.dds is handled).
#
# Usage:
#   python tools/pack_helper_tex.py                     # pack (default)
#   python tools/pack_helper_tex.py --list              # inventory only
#   python tools/pack_helper_tex.py --verify            # parse back --out
#   python tools/pack_helper_tex.py --verify FILE.txd   # parse any TXD
#   python tools/pack_helper_tex.py --gen-placeholders  # write TBD placeholder art
#   python tools/pack_helper_tex.py --emit-magic-script out.txt
#   python tools/pack_helper_tex.py --strict            # fail on missing slots
#
# (c) skygfx_plus_expIV — tooling lane m0061. No game assets are modified.
# ============================================================================

import argparse
import json
import math
import struct
import sys
from pathlib import Path

# ----------------------------------------------------------------------------
# RW 3.6 (GTA SA PC) constants — all empirically verified against real TXDs
# ----------------------------------------------------------------------------

CHUNK_STRUCT = 0x01
CHUNK_EXTENSION = 0x03
CHUNK_TEXTURENATIVE = 0x15
CHUNK_TEXDICTIONARY = 0x16

# Every chunk header in a GTA SA PC TXD carries this library ID (RW 3.6.0.3).
# Verified: plant1.txd, neo.txd, Mobile_details.txd — all 0x1803FFFF.
RW_VERSION = 0x1803FFFF

PLATFORM_D3D9 = 9     # native struct platformDescriptor
DEVICE_ID = 2         # dict struct "recommended platform" (real SA files: 2)
RASTER_TYPE_TEX = 4   # rwRASTERTYPETEXTURE

# Raster format number (bits 8..11 of the rasterFormat flags word):
FMT_1555, FMT_565, FMT_4444, FMT_LUM8, FMT_8888, FMT_888 = 1, 2, 3, 4, 5, 6
FLAG_MIPMAP = 0x8000  # rwRASTERFORMATMIPMAP

# D3DFORMAT values (d3dFormat field)
D3DFMT_A8R8G8B8 = 21
D3DFMT_X8R8G8B8 = 22
D3DFMT_R5G6B5 = 23
D3DFMT_A1R5G5B5 = 25
D3DFMT_A4R4G4B4 = 26
D3DFMT_L8 = 50
D3DFMT_A8L8 = 51
D3DFMT_DXT1 = 0x31545844
D3DFMT_DXT3 = 0x33545844
D3DFMT_DXT5 = 0x35545844

# Filter/address modes (RW SDK enums — plant1/neo use filter 2/6, addr 1)
FILTER_NEAREST, FILTER_LINEAR, FILTER_LINEARMIPLINEAR = 1, 2, 6
ADDR_WRAP, ADDR_MIRROR, ADDR_CLAMP, ADDR_BORDER = 1, 2, 3, 4
FILTER_NAMES = {"nearest": FILTER_NEAREST, "linear": FILTER_LINEAR,
                "linearmip": FILTER_LINEARMIPLINEAR}
ADDR_NAMES = {"wrap": ADDR_WRAP, "mirror": ADDR_MIRROR,
              "clamp": ADDR_CLAMP, "border": ADDR_BORDER}

NAME_MAX = 31  # RW name field is 32 bytes incl. NUL

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = REPO_ROOT / "assets" / "helper_tex"
DEFAULT_OUT = DEFAULT_SRC / "helper_tex.txd"

# Texture kind -> (formatNum, depth, d3dFormat, is_not_rw_compatible)
KIND_INFO = {
    "rgba8888": (FMT_8888, 32, D3DFMT_A8R8G8B8, False),
    "xrgb8888": (FMT_8888, 32, D3DFMT_X8R8G8B8, False),
    "l8":       (FMT_LUM8, 8, D3DFMT_L8, False),
    "a8l8":     (FMT_LUM8, 16, D3DFMT_A8L8, False),
    "rgb565":   (FMT_565, 16, D3DFMT_R5G6B5, False),
    "argb1555": (FMT_1555, 16, D3DFMT_A1R5G5B5, False),
    "argb4444": (FMT_4444, 16, D3DFMT_A4R4G4B4, False),
    "dxt1":     (FMT_1555, 8, D3DFMT_DXT1, True),   # DXT1 convention: 1555/8
    "dxt3":     (FMT_4444, 16, D3DFMT_DXT3, True),  # DXT3/5: 4444/16 (matches
    "dxt5":     (FMT_4444, 16, D3DFMT_DXT5, True),  #  plant1/neo real headers)
}
KIND_LABEL = {
    "rgba8888": "RGBA32", "xrgb8888": "XRGB32", "l8": "L8", "a8l8": "A8L8",
    "rgb565": "565", "argb1555": "1555", "argb4444": "4444",
    "dxt1": "DXT1", "dxt3": "DXT3", "dxt5": "DXT5",
}


def align4(n):
    return (n + 3) & ~3


def chunk(chunk_id, body):
    return struct.pack("<III", chunk_id, len(body), RW_VERSION) + body


# ----------------------------------------------------------------------------
# DDS input
# ----------------------------------------------------------------------------

DDPF_ALPHAPIXELS = 0x1
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
DDPF_LUMINANCE = 0x20000
DDSCAPS2_VOLUME = 0x200000
DDSCAPS2_CUBEMAP = 0x200


class TexError(Exception):
    pass


def parse_dds(path):
    """Parse a DDS file -> (width, height, kind, has_alpha, levels=[bytes,...])."""
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"DDS ":
        raise TexError(f"{path.name}: not a DDS file")
    (size,) = struct.unpack_from("<I", data, 4)
    if size != 124:
        raise TexError(f"{path.name}: bad DDS header size {size}")
    height, width, _pitch, depth, mipcount = struct.unpack_from("<5I", data, 12)
    pf_size, pf_flags = struct.unpack_from("<2I", data, 76)
    fourcc = data[84:88]
    rgb_bits, rmask, gmask, bmask, amask = struct.unpack_from("<5I", data, 88)
    _caps, caps2 = struct.unpack_from("<2I", data, 108)
    if caps2 & DDSCAPS2_CUBEMAP:
        raise TexError(f"{path.name}: cube-map DDS not supported (RW TXD has no "
                       f"cube rasters in SA; export one face as a plain 2D DDS)")
    if caps2 & DDSCAPS2_VOLUME:
        raise TexError(f"{path.name}: volume/3D DDS not supported (RW 3.6 TXD has "
                       f"no volume raster). Flatten to a 2D slice or slice atlas "
                       f"first (see --gen-placeholders for bluenoisevolumeL8.dds).")
    if mipcount == 0:
        mipcount = 1

    if pf_flags & DDPF_FOURCC:
        fcc = fourcc.decode("latin-1")
        kind = {"DXT1": "dxt1", "DXT3": "dxt3", "DXT5": "dxt5"}.get(fcc)
        if kind is None:
            if fcc == "DX10":
                raise TexError(f"{path.name}: DXGI DX10-header DDS not supported; "
                               f"convert to a legacy DXT1/DXT3/DXT5 or 32-bit DDS")
            raise TexError(f"{path.name}: unsupported FourCC {fcc!r}")
        block_bytes = 8 if kind == "dxt1" else 16
        bpp_hint = None
    else:
        block_bytes = None
        if pf_flags & DDPF_LUMINANCE:
            if rgb_bits == 8:
                kind = "l8"
            elif rgb_bits == 16 and (pf_flags & DDPF_ALPHAPIXELS):
                kind = "a8l8"
            else:
                raise TexError(f"{path.name}: unsupported luminance DDS ({rgb_bits} bits)")
        elif pf_flags & DDPF_RGB:
            if rgb_bits == 32 and (r, g, b, a) == (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000):
                kind = "rgba8888"
            elif rgb_bits == 32 and (r, g, b, a) == (0x00FF0000, 0x0000FF00, 0x000000FF, 0):
                kind = "xrgb8888"
            elif rgb_bits == 16 and (r, g, b, a) == (0xF800, 0x07E0, 0x001F, 0):
                kind = "rgb565"
            elif rgb_bits == 16 and (r, g, b, a) == (0x7C00, 0x03E0, 0x001F, 0x8000):
                kind = "argb1555"
            elif rgb_bits == 16 and (r, g, b, a) == (0x0F00, 0x00F0, 0x000F, 0xF000):
                kind = "argb4444"
            else:
                raise TexError(f"{path.name}: unsupported RGB DDS "
                               f"({rgb_bits} bits, masks {r:#x}/{g:#x}/{b:#x}/{a:#x})")
            bpp_hint = rgb_bits // 8
        else:
            raise TexError(f"{path.name}: unsupported DDS pixel format flags {pf_flags:#x}")

    # has_alpha: DXT3/5 carry alpha; DXT1 only if the DDS declares alpha pixels.
    if kind == "dxt5" or kind == "dxt3":
        has_alpha = True
    elif kind == "dxt1":
        has_alpha = bool(pf_flags & DDPF_ALPHAPIXELS)
    elif kind == "l8":
        has_alpha = False
    elif kind == "a8l8":
        has_alpha = True
    else:
        has_alpha = bool(pf_flags & DDPF_ALPHAPIXELS) or kind == "rgba8888"

    # Extract mip levels (rows are DWORD-padded in DDS -> unpaddle to tight rows)
    levels = []
    off = 128
    w, h = width, height
    for _ in range(mipcount):
        if block_bytes is not None:
            bw = max(1, (w + 3) // 4)
            bh = max(1, (h + 3) // 4)
            nbytes = bw * bh * block_bytes
        else:
            row = w * bpp_hint
            nbytes = align4(row) * h
        if off + nbytes > len(data):
            raise TexError(f"{path.name}: truncated DDS (mip {len(levels)} needs "
                           f"{nbytes} bytes at offset {off}, file is {len(data)})")
        raw = data[off:off + nbytes]
        off += nbytes
        if block_bytes is None:
            row = w * bpp_hint
            if align4(row) != row:  # unpad DDS DWORD rows -> tight TXD rows
                raw = b"".join(raw[y * align4(row):y * align4(row) + row]
                               for y in range(h))
        levels.append(raw)
        w = max(1, w // 2)
        h = max(1, h // 2)
    return width, height, kind, has_alpha, levels


# ----------------------------------------------------------------------------
# PNG input (Pillow)
# ----------------------------------------------------------------------------

def png_to_levels(path, want_mips, want_kind):
    try:
        from PIL import Image
    except ImportError:
        raise TexError("Pillow is required for PNG input (pip install Pillow)")
    img = Image.open(path)
    if want_kind in (None, "l8", "a8l8"):
        if img.mode in ("L", "LA") or want_kind in ("l8", "a8l8"):
            pass  # decided below by actual mode
    if img.mode == "L":
        kind = "l8"
        base = img
    elif img.mode == "LA":
        kind = "a8l8"
        base = img
    else:
        kind = "rgba8888"
        base = img.convert("RGBA")
    has_alpha = kind in ("a8l8",) or (kind == "rgba8888" and
                                      base.getchannel("A").getextrema()[0] < 255)

    def encode(im):
        if kind == "l8":
            return im.convert("L").tobytes()
        if kind == "a8l8":
            l = im.convert("L")
            a = im.convert("LA").getchannel("A")
            la = Image.merge("LA", (l, a))
            return la.tobytes()          # tight L,A interleaved bytes
        rgba = im.convert("RGBA")
        raw = bytearray(rgba.tobytes())  # R,G,B,A
        raw[0::4], raw[2::4] = raw[2::4], raw[0::4]  # -> B,G,R,A (A8R8G8B8)
        return bytes(raw)

    levels = [encode(base)]
    if want_mips:
        w, h = base.size
        im = base
        while w > 1 or h > 1:
            w, h = max(1, w // 2), max(1, h // 2)
            im = im.resize((w, h), Image.BOX)
            levels.append(encode(im))
    return base.size[0], base.size[1], kind, has_alpha, levels


# ----------------------------------------------------------------------------
# Staging scan + manifest
# ----------------------------------------------------------------------------

def load_manifest(src):
    mf = src / "manifest.json"
    if not mf.exists():
        return {"textures": {}, "reserved": {}}
    return json.loads(mf.read_text(encoding="utf-8"))


def collect_textures(src, manifest, strict):
    """Return list of texture dicts: name, levels, kind, has_alpha, filter, addr, tbd."""
    entries = manifest.get("textures", {})
    reserved = manifest.get("reserved", {})
    files = sorted([p for p in src.iterdir()
                    if p.suffix.lower() in (".png", ".dds", ".bmp")])
    by_name = {}
    problems = []
    for path in files:
        # manifest lookup by explicit "file" or by stem-as-name
        mentry = None
        for name, e in entries.items():
            if e.get("file", name + path.suffix.lower()) == path.name:
                mentry = (name, e)
                break
        if mentry is None and path.stem in entries:
            mentry = (path.stem, entries[path.stem])
        if mentry is None and path.stem in reserved:
            mentry = (path.stem, reserved[path.stem])
        name = mentry[0] if mentry else path.stem
        e = mentry[1] if mentry else {}

        if not (1 <= len(name) <= NAME_MAX) or not all(
                c.isalnum() or c == "_" for c in name):
            problems.append(f"{path.name}: texture name {name!r} invalid "
                            f"(1..{NAME_MAX} chars of [A-Za-z0-9_])")
            continue
        if name in by_name:
            problems.append(f"{path.name}: duplicate texture name {name!r}")
            continue

        want_mips = bool(e.get("mips", False))
        try:
            if path.suffix.lower() == ".dds":
                w, h, kind, has_alpha, levels = parse_dds(path)
                if want_mips and len(levels) == 1:
                    problems.append(f"{path.name}: manifest 'mips' requested but DDS "
                                    f"has a single level; add mips to the DDS or "
                                    f"ship a PNG")
            else:
                w, h, kind, has_alpha, levels = png_to_levels(path, want_mips,
                                                              e.get("format"))
        except TexError as exc:
            problems.append(str(exc))
            continue

        # mipmap count consistency: if manifest asked for mips we generated a
        # full chain; DDS may carry its own chain. Filter/address defaults.
        mips = len(levels)
        filt = FILTER_NAMES.get(e.get("filter", "").lower()) or \
            (FILTER_LINEARMIPLINEAR if mips > 1 else FILTER_LINEAR)
        addr = ADDR_NAMES.get(e.get("addressing", "wrap").lower(), ADDR_WRAP)

        by_name[name] = {
            "name": name, "file": path.name, "w": w, "h": h, "kind": kind,
            "has_alpha": has_alpha, "levels": levels, "mips": mips,
            "filter": filt, "addr": addr, "tbd": bool(e.get("tbd", False)),
            "note": e.get("note", ""),
        }

    missing = [n for n in list(entries) + list(reserved) if n not in by_name]
    for n in missing:
        slot = reserved.get(n, entries.get(n, {}))
        note = slot.get("note", slot.get("source", ""))
        print(f"  [TBD slot, not packed] {n}  {note}")
        if strict:
            problems.append(f"missing texture for slot {n!r} ({note})")
    if problems:
        for p in problems:
            print(f"  ERROR: {p}", file=sys.stderr)
        if strict:
            raise SystemExit(1)
        raise SystemExit("aborting (use --strict to fail on missing slots only)")
    return [by_name[k] for k in sorted(by_name)]


# ----------------------------------------------------------------------------
# TXD writer
# ----------------------------------------------------------------------------

def write_txd(textures):
    dict_struct = chunk(CHUNK_STRUCT, struct.pack("<HH", len(textures), DEVICE_ID))
    body = [dict_struct]
    for t in textures:
        fmt_num, depth, d3dfmt, not_rw = KIND_INFO[t["kind"]]
        rast_flags = (fmt_num << 8) | (FLAG_MIPMAP if t["mips"] > 1 else 0)
        texfmt = t["filter"] | (t["addr"] << 8) | (t["addr"] << 12)
        flags = (1 if t["has_alpha"] else 0) | (0x8 if not_rw else 0)  # bit3 =
        # isNotRwCompatible — set for DXT (rasterFormat is a stand-in), exactly
        # like the real files: DXT plant1/neo headers have 0x8, A8L8 does not.

        name = t["name"].encode("latin-1")[:NAME_MAX]
        header = struct.pack(
            "<II32s32sIIHHBBBB",
            PLATFORM_D3D9, texfmt,
            name.ljust(32, b"\0"), b"\0" * 32,
            rast_flags, d3dfmt,
            t["w"], t["h"], depth, t["mips"], RASTER_TYPE_TEX, flags,
        )
        native = header
        for raw in t["levels"]:
            size = align4(len(raw))
            native += struct.pack("<I", size) + raw.ljust(size, b"\0")
        tex_chunk = chunk(CHUNK_TEXTURENATIVE,
                          chunk(CHUNK_STRUCT, native) + chunk(CHUNK_EXTENSION, b""))
        body.append(tex_chunk)
    body.append(chunk(CHUNK_EXTENSION, b""))
    return chunk(CHUNK_TEXDICTIONARY, b"".join(body))


# ----------------------------------------------------------------------------
# TXD reader (verify mode — also used to validate our own output)
# ----------------------------------------------------------------------------

def verify_txd(path):
    data = Path(path).read_bytes()
    print(f"== {path} ({len(data)} bytes)")
    rows = []
    errors = []

    def walk(off, end, depth):
        while off + 12 <= end:
            ctype, csize, cver = struct.unpack_from("<III", data, off)
            if cver != RW_VERSION:
                errors.append(f"@{off}: chunk 0x{ctype:x} version 0x{cver:08x} != 0x1803FFFF")
            if off + 12 + csize > end:
                errors.append(f"@{off}: chunk 0x{ctype:x} size {csize} overruns parent")
                return
            if ctype == CHUNK_STRUCT and depth == 1 and csize == 4:
                cnt, dev = struct.unpack_from("<HH", data, off + 12)
                print(f"  texdict: {cnt} textures, deviceID={dev}")
            if ctype == CHUNK_STRUCT and depth == 2 and csize >= 88:
                plat, texfmt = struct.unpack_from("<II", data, off + 12)
                name = data[off + 20:off + 52].split(b"\0")[0].decode("latin-1")
                rast, d3dfmt = struct.unpack_from("<II", data, off + 84)
                w, h = struct.unpack_from("<HH", data, off + 92)
                depth_bpp, mips = data[off + 96], data[off + 97]
                rt, fl = data[off + 98], data[off + 99]
                fmt_num = (rast >> 8) & 0xF
                dxt = {D3DFMT_DXT1: "DXT1", D3DFMT_DXT3: "DXT3",
                       D3DFMT_DXT5: "DXT5"}.get(d3dfmt)
                label = dxt or {1: "1555", 2: "565", 3: "4444", 4: "LUM",
                                5: "8888", 6: "888"}.get(fmt_num, f"fmt{fmt_num}")
                # walk mip sizes
                p = off + 12 + 88
                want_sum = 0
                ok = True
                for i in range(mips):
                    if p + 4 > off + 12 + csize:
                        errors.append(f"{name}: mip {i} header past struct end")
                        ok = False
                        break
                    (msz,) = struct.unpack_from("<I", data, p)
                    p += 4 + msz
                    want_sum += msz
                if ok and p != off + 12 + csize:
                    errors.append(f"{name}: mip data ends at +{p - off}, struct says "
                                  f"+{12 + csize} ({p - (off + 12 + csize):+d} bytes)")
                filt = texfmt & 0xFF
                u = (texfmt >> 8) & 0xF
                v = (texfmt >> 12) & 0xF
                rows.append((name, label, f"{w}x{h}", str(mips),
                             "yes" if fl & 1 else "no",
                             f"{filt}/{u},{v}", f"{want_sum}B"))
                if plat != PLATFORM_D3D9:
                    errors.append(f"{name}: platform {plat} != 9")
                if rt != RASTER_TYPE_TEX:
                    errors.append(f"{name}: rasterType {rt} != 4")
            if ctype in (CHUNK_TEXDICTIONARY, CHUNK_TEXTURENATIVE) and depth < 3:
                walk(off + 12, off + 12 + csize, depth + 1)
            off += 12 + csize

    walk(0, len(data), 0)
    hdr = ("name", "format", "dims", "mips", "alpha", "flt/u,v", "texels")
    widths = [max(len(str(r[i])) for r in rows + [hdr]) for i in range(len(hdr))]
    for r in [hdr] + rows:
        print("  " + "  ".join(str(c).ljust(w) for c, w in zip(r, widths)))
    for e in errors:
        print(f"  VERIFY-ERROR: {e}")
    print(f"  verify: {'FAILED' if errors else 'OK'} "
          f"({len(rows)} textures, {len(errors)} errors)")
    return not errors


# ----------------------------------------------------------------------------
# Placeholder art (--gen-placeholders)
# ----------------------------------------------------------------------------

def _bayer(n):
    if n == 1:
        return [[0]]
    s = _bayer(n // 2)
    t = [[v * 4 for v in row] for row in s]
    top = [row + [v + 2 for v in row] for row in t]
    bot = [[v + 3 for v in row] + [v + 1 for v in row] for row in t]
    return top + bot


def _tileable_perlin(size, period, seed):
    import numpy as np
    rng = np.random.default_rng(seed)
    ang = rng.uniform(0, 2 * math.pi, (period, period))
    gx, gy = np.cos(ang), np.sin(ang)
    ys, xs = np.mgrid[0:size, 0:size]
    fx = xs * period / size
    fy = ys * period / size
    x0, y0 = np.floor(fx).astype(int) % period, np.floor(fy).astype(int) % period
    x1, y1 = (x0 + 1) % period, (y0 + 1) % period
    tx, ty = fx - np.floor(fx), fy - np.floor(fy)
    sx = tx * tx * (3 - 2 * tx)
    sy = ty * ty * (3 - 2 * ty)

    def dot(gx_, gy_, dx, dy):
        return gx_ * dx + gy_ * dy
    n00 = dot(gx[y0, x0], gy[y0, x0], tx, ty)
    n10 = dot(gx[y0, x1], gy[y0, x1], tx - 1, ty)
    n01 = dot(gx[y1, x0], gy[y1, x0], tx, ty - 1)
    n11 = dot(gx[y1, x1], gy[y1, x1], tx - 1, ty - 1)
    nx0 = n00 + sx * (n10 - n00)
    nx1 = n01 + sx * (n11 - n01)
    val = nx0 + sy * (nx1 - nx0)
    return (val - val.min()) / max(1e-9, np.ptp(val))


def _tileable_worley(size, cells, seed):
    import numpy as np
    rng = np.random.default_rng(seed)
    pts = rng.uniform(0, 1, (cells, cells, 2))
    ys, xs = np.mgrid[0:size, 0:size]
    fx = xs * cells / size
    fy = ys * cells / size
    cx, cy = np.floor(fx).astype(int), np.floor(fy).astype(int)
    best = np.full((size, size), 1e9)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            gx = (cx + dx) % cells
            gy = (cy + dy) % cells
            px = (cx + dx) + pts[gy, gx, 0]
            py = (cy + dy) + pts[gy, gx, 1]
            d = np.hypot(px - fx, py - fy)
            best = np.minimum(best, d)
    return np.clip(best, 0, 1)


def _fbm(size, seed, base=4, octaves=5):
    import numpy as np
    out = None
    amp = 1.0
    total = 0.0
    for o in range(octaves):
        n = _tileable_perlin(size, base * (2 ** o), seed + o * 977)
        out = n * amp if out is None else out + n * amp
        total += amp
        amp *= 0.5
    out /= total
    return (out - out.min()) / max(1e-9, np.ptp(out))


def gen_placeholders(src):
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        raise SystemExit("--gen-placeholders needs numpy + Pillow")
    src.mkdir(parents=True, exist_ok=True)
    written = []

    def save(name, mode, arr):
        p = src / name
        Image.fromarray(arr, mode).save(p)
        written.append(p.name)

    # -- cloud layer cards (stacked clouds): TBD art placeholders -----------
    for i in range(3):
        size = 512
        shape = _fbm(size, seed=100 + i * 31, base=2 + i, octaves=5)
        shape = np.clip((shape - (0.42 + 0.08 * i)) * 2.6, 0, 1) ** (1.3 + 0.2 * i)
        rgb = (200 + 55 * _fbm(size, seed=900 + i, base=8, octaves=3)).astype(np.uint8)
        a = (shape * 255).astype(np.uint8)
        rgba = np.dstack([rgb, rgb, rgb, a])
        save(f"cloud_layer{i}.png", "RGBA", rgba)

    # -- cloud_noise: channel-packed per effects-menu-overhaul.md §4 --------
    # R = density (tileable Perlin fBm), G = thickness (tileable Worley F1),
    # B = edge detail (high-freq noise), A = ordered dither (Bayer 8x8).
    # ART IS TBD — functional placeholder only.
    size = 256
    dens = (_fbm(size, seed=7, base=4, octaves=5) * 255).astype(np.uint8)
    thick = ((1.0 - _tileable_worley(size, 8, seed=11)) * 255).astype(np.uint8)
    edge = (_fbm(size, seed=13, base=16, octaves=2) * 255).astype(np.uint8)
    bayer = np.array(_bayer(8), dtype=np.uint8) * 4 + 2  # 0..63 -> 0..255-ish
    dith = np.tile(bayer, (size // 8, size // 8))
    save("cloud_noise.png", "RGBA", np.dstack([dens, thick, edge, dith]))

    # -- utility noise tile (REAL packing — mirrors src/render/utilitynoise.cpp)
    # R = tileable Perlin fBm, G = tileable Worley F1, B = white noise,
    # A = Bayer 8x8 dither. Seamless at rwTEXTUREADDRESSWRAP.
    size = 256
    r = (_fbm(size, seed=21, base=4, octaves=6) * 255).astype(np.uint8)
    g = ((1.0 - _tileable_worley(size, 8, seed=23)) * 255).astype(np.uint8)
    rng = np.random.default_rng(29)
    b = rng.integers(0, 256, (size, size), dtype=np.uint8)
    a = np.tile(bayer, (size // 8, size // 8))
    save("utility_noise.png", "RGBA", np.dstack([r, g, b, a]))

    # -- blue noise 64x64 L8 tile: extracted slice of FusionFix's 64^3 ------
    # bluenoisevolumeL8.dds (RW has no volume rasters -> ship 2D slices).
    vol = Path(r"E:\SDKs\GTAIV.EFLC.FusionFix\source\resources\bluenoisevolumeL8.dds")
    arr = None
    if vol.exists():
        try:
            # read the volume DDS directly (parse_dds rejects volume rasters by
            # design — here we deliberately take slice 0 of the 64^3 volume)
            data = vol.read_bytes()
            if len(data) >= 128 + 64 * 64 * 64 and data[:4] == b"DDS ":
                h, w, _pitch, depth, mips = struct.unpack_from("<5I", data, 12)
                _pfs, pff = struct.unpack_from("<2I", data, 76)
                bits, = struct.unpack_from("<I", data, 88)
                if (pff & DDPF_LUMINANCE) and bits == 8 and w == 64 and h == 64 \
                        and depth == 64 and mips == 1:
                    arr = np.frombuffer(data[128:128 + 64 * 64],
                                        dtype=np.uint8).reshape(64, 64)
                    print(f"  blue_noise_64: extracted slice 0 of {vol}")
        except Exception as exc:
            print(f"  blue_noise_64: cannot read volume ({exc})")
    if arr is None:
        rng = np.random.default_rng(31)
        white = rng.uniform(0, 1, (64, 64))
        # crude blue-noise proxy: high-pass filtered white noise
        k = np.array([[0, -1, 0], [-1, 4, -1], [0, -1, 0]], dtype=float)
        hp = np.zeros_like(white)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                hp += np.roll(np.roll(white, dy, 0), dx, 1) * k[dy + 1, dx + 1]
        arr = np.clip(hp * 0.5 + 0.5, 0, 1)
        arr = (arr * 255).astype(np.uint8)
        print("  blue_noise_64: FusionFix volume not found -> generated proxy")
    save("blue_noise_64.png", "L", arr)

    print(f"  wrote {len(written)} placeholder/staging PNGs to {src}")
    for n in written:
        print(f"    {n}")


# ----------------------------------------------------------------------------
# Magic.TXD fallback script (GUI checklist — honest: not automatable here)
# ----------------------------------------------------------------------------

def emit_magic_script(out_path, textures):
    lines = [
        "Magic.TXD import checklist for helper_tex.txd",
        "=" * 60,
        "",
        "NOTE: this checkout of Magic.TXD (E:\\SDKs\\Magic.TXD) is a Qt GUI plus",
        "the magictxd.lib embedding API (magic_api/) — src/main.cpp has no CLI",
        "entry point, so this 'script' is a GUI checklist, not an automated",
        "batch. The primary tool path is the native writer in",
        "tools/pack_helper_tex.py (see --verify).",
        "",
        "GUI steps:",
        "  1. Open Magic.TXD -> File -> New TXD (platform: D3D9 / SA).",
        "  2. For each texture below: Import -> select the file",
        "     -> name it exactly as listed -> keep the listed format.",
        "  3. Set filtering: Linear (1 mip) / Linear-mip-linear (mips).",
        "     Addressing: Wrap (or Clamp where noted).",
        "  4. Save As -> helper_tex.txd (version 3.6.0.3 / SA).",
        "  5. Diff against our output: python tools/pack_helper_tex.py --verify",
        "",
        "Textures:",
        "",
    ]
    for t in textures:
        lines.append(f"  {t['name']:<20} {KIND_LABEL[t['kind']]:<8} "
                     f"{t['w']}x{t['h']:<6} mips={t['mips']} "
                     f"src=assets/helper_tex/{t['file']}"
                     + ("   [TBD ART]" if t["tbd"] else ""))
    Path(out_path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({len(textures)} textures listed)")


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Pack assets/helper_tex/*.{png,dds,bmp} into helper_tex.txd "
                    "(GTA SA PC / RW 3.6 D3D9 texture dictionary).",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC,
                    help=f"staging folder (default {DEFAULT_SRC})")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help=f"output TXD (default {DEFAULT_OUT})")
    ap.add_argument("--list", action="store_true", help="inventory only, write nothing")
    ap.add_argument("--verify", nargs="?", const="AUTO", metavar="TXD",
                    help="parse a TXD and print its texture table")
    ap.add_argument("--gen-placeholders", action="store_true",
                    help="write placeholder/TBD staging art into --src")
    ap.add_argument("--emit-magic-script", metavar="FILE",
                    help="write a Magic.TXD GUI import checklist (fallback path)")
    ap.add_argument("--strict", action="store_true",
                    help="fail if any manifest/reserved slot has no file")
    args = ap.parse_args()

    if args.gen_placeholders:
        gen_placeholders(args.src)
        return 0

    if args.verify:
        path = args.out if args.verify == "AUTO" else Path(args.verify)
        if not path.exists():
            print(f"{path} does not exist — pack first", file=sys.stderr)
            return 1
        return 0 if verify_txd(path) else 1

    manifest = load_manifest(args.src)
    print(f"staging: {args.src}")
    textures = collect_textures(args.src, manifest, args.strict)
    if not textures:
        print("no textures staged (add PNG/DDS or run --gen-placeholders)")
        return 1

    print(f"packing {len(textures)} textures -> {args.out}")
    for t in textures:
        tag = " [TBD ART]" if t["tbd"] else ""
        print(f"  {t['name']:<20} {KIND_LABEL[t['kind']]:<8} {t['w']}x{t['h']:<6} "
              f"mips={t['mips']:<2} from {t['file']}{tag}")
    blob = write_txd(textures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f"wrote {args.out} ({len(blob)} bytes)")

    if args.emit_magic_script:
        emit_magic_script(args.emit_magic_script, textures)

    print("verifying output round-trip:")
    return 0 if verify_txd(args.out) else 1


if __name__ == "__main__":
    sys.exit(main())
