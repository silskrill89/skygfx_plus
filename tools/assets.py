#!/usr/bin/env python3
"""
assets.py — Unified asset tools for skygfx_plus_expIV

Consolidates: dff_analyzer, dff_extract, deep_rw_scan, txd_builder,
              normalmap generators, envmap generator, wheel pipeline

Commands:
  analyze <file.dff>      Analyze DFF model structure
  extract <file.dff>      Extract DFF contents
  scan <file>             Deep RW chunk scan (DFF/TXD)
  txd <input_dir> <out>   Build TXD from PNGs
  normalmap <in> <out>    Generate normal map from diffuse
  extract-txd <txd> <dir> Extract textures from TXD
  envmap                  Generate environment maps
  wheels                  Run wheel audit/pipeline
"""
import os
import sys
import struct
from pathlib import Path

# ═══════════════════════════════════════════════════════════════
# RW CHUNK HELPERS (shared across tools)
# ═══════════════════════════════════════════════════════════════

CHUNK_NAMES = {
    0x01: "Struct", 0x02: "String", 0x03: "Extension", 0x04: "Image",
    0x05: "Camera", 0x06: "Camera", 0x08: "World", 0x0A: "Texture",
    0x0B: "Material", 0x0C: "Material List", 0x0D: "Atomic Stand Alone",
    0x0E: "Atomic", 0x0F: "Geometry", 0x10: "Geometry List",
    0x14: "Frame List", 0x15: "Frame", 0x1A: "Geometry",
    0x1B: "HAnim PLG", 0x1C: "User Data PLG", 0x1D: "Material Effects PLG",
    0x20: "Bin Mesh PLG", 0x24: "Skin PLG", 0x25: "Reflection Material PLG",
    0x27: "Mesh Extension PLG", 0x2A: "Anisotropy PLG",
    0x2B: "Animation", 0x2C: "Right To Render PLG",
    0x2E: "Morph PLG", 0x30: "2DFX", 0x31: "Night Vertex Colors",
    0x32: "Collision PLG", 0x34: "Environment Map PLG",
    0x36: "Pipeline Set", 0x37: "Page2D PLG", 0x3B: "TXDP",
    0x3D: "Particle PLG", 0x3E: "2DFX", 0x40: "Decal PLG",
    0x41: "Dictionary PLG", 0x44: "UV Animation Dictionary",
    0x46: "MultiRes PLG", 0x50: "Atomic Visibility Distance",
    0x51: "Normal Map PLG", 0x52: "World PLG",
    0x53: "Mesh PLG", 0x54: "Sky Mipmap Val",
    0x56: "Entity PLG", 0x57: "World PLG",
    0x59: "Metric PLG", 0x5A: "Animation PLG",
    0x69: "HAnim PLG", 0x6E: "Material PLG",
    0x71: "Grass PLG", 0x74: "Comment PLG",
    0x76: "Skin PLG", 0x78: "Vertex Colors",
    0x79: "Mesh PLG", 0x7A: "Meta PLG",
    0x7C: "Skin PLG", 0x7D: "Particle PLG",
    0x7E: "Shadow PLG", 0x7F: "Material PLG",
    0x80: "Animation PLG", 0x81: "Material PLG",
    0x82: "Bearing PLG", 0x83: "Clump PLG",
    0x84: "GTA Proton PLG", 0x85: "Vertex PLG",
    0x86: "Triangle PLG",
}

def chunk_name(t):
    return CHUNK_NAMES.get(t, f"Unknown(0x{t:02X})")

# ═══════════════════════════════════════════════════════════════
# DFF ANALYZE (from dff_analyzer.py)
# ═══════════════════════════════════════════════════════════════

def analyze_dff(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()
    
    print(f"DFF Analysis: {filepath}")
    print(f"File size: {len(data)} bytes")
    
    pos = 0
    while pos < len(data) - 12:
        try:
            struct_type = struct.unpack_from('<I', data, pos)[0]
            size = struct.unpack_from('<I', data, pos + 4)[0]
            
            if size < 12 or size > len(data):
                break
            
            name = chunk_name(struct_type)
            print(f"  @{pos:6d}: {name} (0x{struct_type:02X}) - {size} bytes")
            
            if struct_type == 0x10:  # Geometry List
                count = struct.unpack_from('<I', data, pos + 12)[0]
                print(f"           Geometry count: {count}")
            elif struct_type == 0x0F:  # Geometry
                flags = struct.unpack_from('<H', data, pos + 16)[0]
                print(f"           Flags: 0x{flags:04X}")
            elif struct_type == 0x0E:  # Atomic
                frame_idx = struct.unpack_from('<I', data, pos + 12)[0]
                geom_idx = struct.unpack_from('<I', data, pos + 16)[0]
                print(f"           Frame: {frame_idx}, Geometry: {geom_idx}")
            
            pos += size
        except Exception:
            break
    
    return {"file": filepath, "size": len(data)}

# ═══════════════════════════════════════════════════════════════
# RW SCAN (from deep_rw_scan.py)
# ═══════════════════════════════════════════════════════════════

def scan_rw_file(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()
    
    print(f"RW Scan: {filepath}")
    print(f"File size: {len(data)} bytes")
    
    # Check for TXD
    if data[:4] == b'TXDP':
        print("Format: TXD (Texture Dictionary)")
        return scan_txd(data, filepath)
    else:
        print("Format: DFF (Model)")
        return analyze_dff(filepath)

def scan_txd(data, filename):
    print(f"TXD Scan: {filename}")
    pos = 0
    tex_count = 0
    while pos < len(data) - 12:
        try:
            chunk_type = struct.unpack_from('<I', data, pos)[0]
            chunk_size = struct.unpack_from('<I', data, pos + 4)[0]
            if chunk_size < 12:
                break
            if chunk_type == 0x16:  # Texture Native
                tex_count += 1
                name = data[pos+12:pos+28].split(b'\x00')[0].decode('ascii', errors='ignore')
                print(f"  Texture {tex_count}: {name}")
            pos += chunk_size
        except Exception:
            break
    print(f"Total textures: {tex_count}")
    return {"textures": tex_count}

# ═══════════════════════════════════════════════════════════════
# TXD BUILD (from txd_builder.py)
# ═══════════════════════════════════════════════════════════════

def build_txd(input_dir, output_file, game='SA'):
    from PIL import Image
    input_dir = Path(input_dir)
    output_file = Path(output_file)
    
    textures = list(input_dir.glob("*.png")) + list(input_dir.glob("*.bmp"))
    if not textures:
        print(f"No textures found in {input_dir}")
        return
    
    print(f"Building TXD from {len(textures)} textures...")
    
    # TXD header
    header = bytearray()
    header += struct.pack('<I', 0x16)  # TXD magic
    header += struct.pack('<I', 0x1800FFFF)  # Version
    header += struct.pack('<I', len(textures))  # Count
    
    tex_data = bytearray()
    for tex_path in textures:
        img = Image.open(tex_path)
        name = tex_path.stem[:32].encode('ascii')
        name += b'\x00' * (32 - len(name))
        
        # Simple R8G8B8A8
        pixels = list(img.convert('RGBA').getdata())
        raw = bytearray()
        for r, g, b, a in pixels:
            raw += struct.pack('BBBB', b, g, r, a)
        
        tex_entry = bytearray()
        tex_entry += name
        tex_entry += struct.pack('<HH', img.width, img.height)
        tex_entry += struct.pack('<I', 0x09)  # RGBA32
        tex_entry += struct.pack('<I', len(raw))
        tex_entry += raw
        
        tex_data += struct.pack('<I', 0x16)  # Texture Native
        tex_data += struct.pack('<I', 12 + len(tex_entry))
        tex_data += struct.pack('<I', 0)  # ID
        tex_data += tex_entry
    
    output_file.parent.mkdir(parents=True, exist_ok=True)
    with open(output_file, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', len(header) + len(tex_data) + 12))
        f.write(struct.pack('<I', 0x01))  # Struct chunk
        f.write(struct.pack('<I', 8))
        f.write(tex_data)
    
    print(f"Saved: {output_file} ({output_file.stat().st_size} bytes)")

# ═══════════════════════════════════════════════════════════════
# NORMAL MAP (from gta_normalmap_gen.py)
# ═══════════════════════════════════════════════════════════════

def generate_normalmap(input_path, output_path, strength=2.0):
    from PIL import Image
    import numpy as np
    
    img = Image.open(input_path).convert('L')
    heightmap = np.array(img, dtype=np.float32) / 255.0
    
    # Sobel filter
    gx = np.gradient(heightmap, axis=1) * strength
    gy = np.gradient(heightmap, axis=0) * strength
    
    normal = np.zeros((*heightmap.shape, 3), dtype=np.uint8)
    normal[:, :, 0] = np.clip(128 + gx * 127, 0, 255).astype(np.uint8)  # X
    normal[:, :, 1] = np.clip(128 + gy * 127, 0, 255).astype(np.uint8)  # Y
    normal[:, :, 2] = 255  # Z (up)
    
    out = Image.fromarray(normal, 'RGB')
    out.save(output_path)
    print(f"Normal map: {output_path}")

# ═══════════════════════════════════════════════════════════════
# ENVMAP (from generate_envmaps.py)
# ═══════════════════════════════════════════════════════════════

def generate_envmaps(output_dir):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    import numpy as np
    size = 64
    
    envmaps = {
        "chrome": lambda x, y: np.clip(np.abs(x) * 255, 0, 255).astype(np.uint8),
        "paint": lambda x, y: np.clip((0.5 + 0.3 * x) * 255, 0, 255).astype(np.uint8),
        "rubber": lambda x, y: np.full((size, size), 32, dtype=np.uint8),
        "glass": lambda x, y: np.clip((0.7 + 0.2 * y) * 255, 0, 255).astype(np.uint8),
    }
    
    for name, gen_fn in envmaps.items():
        x = np.linspace(-1, 1, size)
        y = np.linspace(-1, 1, size)
        xx, yy = np.meshgrid(x, y)
        data = gen_fn(xx, yy)
        
        if len(data.shape) == 2:
            data = np.stack([data, data, data], axis=-1)
        
        img = Image.fromarray(data, 'RGB')
        out_path = output_dir / f"{name}_env.raw"
        img.tofile(str(out_path))
        print(f"  {name}: {out_path}")

# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Unified asset tools")
    sub = parser.add_subparsers(dest="command")
    
    a = sub.add_parser("analyze", help="Analyze DFF model")
    a.add_argument("file", help="DFF file path")
    
    s = sub.add_parser("scan", help="Deep RW chunk scan")
    s.add_argument("file", help="DFF or TXD file")
    
    t = sub.add_parser("txd", help="Build TXD from PNGs")
    t.add_argument("input_dir", help="Directory with PNGs")
    t.add_argument("output", help="Output TXD path")
    
    n = sub.add_parser("normalmap", help="Generate normal map")
    n.add_argument("input", help="Diffuse texture")
    n.add_argument("output", help="Output normal map")
    n.add_argument("--strength", type=float, default=2.0)
    
    e = sub.add_parser("envmap", help="Generate environment maps")
    e.add_argument("--output", default="resources/textures")
    
    args = parser.parse_args()
    
    if args.command == "analyze":
        analyze_dff(args.file)
    elif args.command == "scan":
        scan_rw_file(args.file)
    elif args.command == "txd":
        build_txd(args.input_dir, args.output)
    elif args.command == "normalmap":
        generate_normalmap(args.input, args.output, args.strength)
    elif args.command == "envmap":
        generate_envmaps(args.output)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
