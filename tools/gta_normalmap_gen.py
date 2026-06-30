#!/usr/bin/env python3
"""
GTA SA Texture Normal Map Generator
Extracts textures from TXD files and generates normal maps

Usage:
  python gta_normalmap_gen.py extract <txd_file> <output_dir>
  python gta_normalmap_gen.py generate <input_texture> <output_normal> [strength]
  python gta_normalmap_gen.py batch <input_dir> <output_dir> [strength]
"""

import sys
import os
import struct
import math
import glob
from pathlib import Path

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Installing required packages...")
    os.system("pip install Pillow numpy")
    from PIL import Image
    import numpy as np

# TXD file format constants
TXD_HEADER = 0x16
TXD_TEXTURE = 0x02
TXD_STRUCT = 0x01

def read_txd_header(data, offset):
    """Read TXD section header"""
    if offset + 12 > len(data):
        return None, None, None
    section_type = struct.unpack('<I', data[offset:offset+4])[0]
    section_size = struct.unpack('<I', data[offset+4:offset+8])[0]
    version = struct.unpack('<I', data[offset+8:offset+12])[0]
    return section_type, section_size, version

def extract_texture_from_data(data, width, height, bpp, name):
    """Extract texture data from raw bytes"""
    try:
        if bpp == 32:
            # RGBA
            img = Image.frombytes('RGBA', (width, height), data[:width*height*4])
        elif bpp == 24:
            # RGB
            img = Image.frombytes('RGB', (width, height), data[:width*height*3])
        elif bpp == 8:
            # Grayscale
            img = Image.frombytes('L', (width, height), data[:width*height])
        else:
            print(f"  Unsupported BPP: {bpp}")
            return None
        return img
    except Exception as e:
        print(f"  Error extracting texture: {e}")
        return None

def extract_txd(txd_path, output_dir):
    """Extract all textures from a TXD file"""
    print(f"Extracting: {txd_path}")
    
    with open(txd_path, 'rb') as f:
        data = f.read()
    
    os.makedirs(output_dir, exist_ok=True)
    
    offset = 0
    texture_count = 0
    
    while offset < len(data):
        section_type, section_size, version = read_txd_header(data, offset)
        
        if section_type is None:
            break
            
        if section_type == TXD_HEADER:
            # Texture dictionary header
            offset += 12
            continue
        
        elif section_type == TXD_TEXTURE:
            # Texture data
            texture_count += 1
            
            # Read texture info
            tex_offset = offset + 12
            
            # Texture name (32 bytes)
            name = data[tex_offset:tex_offset+32].split(b'\x00')[0].decode('ascii', errors='ignore')
            
            # Skip to texture data
            data_offset = tex_offset + 76  # Skip name + other fields
            
            # Read dimensions and format
            width = struct.unpack('<H', data[tex_offset+44:tex_offset+46])[0]
            height = struct.unpack('<H', data[tex_offset+46:tex_offset+48])[0]
            bpp = struct.unpack('<B', data[tex_offset+50:tex_offset+51])[0]
            
            print(f"  Texture {texture_count}: {name} ({width}x{height}, {bpp}bpp)")
            
            # Extract texture data
            tex_data_size = width * height * (bpp // 8)
            if data_offset + tex_data_size <= len(data):
                img = extract_texture_from_data(data[data_offset:], width, height, bpp, name)
                if img:
                    output_path = os.path.join(output_dir, f"{name}.png")
                    img.save(output_path)
                    print(f"    Saved: {output_path}")
        
        offset += section_size + 12
    
    print(f"Extracted {texture_count} textures")

def generate_normal_map(input_path, output_path, strength=2.0):
    """Generate normal map from a texture"""
    print(f"Generating normal map: {input_path}")
    
    # Load image
    img = Image.open(input_path).convert('L')
    heightmap = np.array(img, dtype=np.float32) / 255.0
    h, w = heightmap.shape
    
    print(f"  Size: {w}x{h}")
    
    # Apply Sobel filter
    dx = np.zeros_like(heightmap)
    dy = np.zeros_like(heightmap)
    
    for y in range(1, h-1):
        for x in range(1, w-1):
            # Sobel X
            dx[y, x] = (
                -heightmap[y-1, x-1] + heightmap[y-1, x+1]
                - 2*heightmap[y, x-1] + 2*heightmap[y, x+1]
                - heightmap[y+1, x-1] + heightmap[y+1, x+1]
            )
            # Sobel Y
            dy[y, x] = (
                -heightmap[y-1, x-1] - 2*heightmap[y-1, x] - heightmap[y-1, x+1]
                + heightmap[y+1, x-1] + 2*heightmap[y+1, x] + heightmap[y+1, x+1]
            )
    
    # Calculate normal vectors
    nx = -dx * strength
    ny = -dy * strength
    nz = np.ones_like(heightmap)
    
    # Normalize
    length = np.sqrt(nx*nx + ny*ny + nz*nz)
    nx /= length
    ny /= length
    nz /= length
    
    # Convert to [0, 255]
    normal_map = np.zeros((h, w, 3), dtype=np.uint8)
    normal_map[:, :, 0] = np.clip((nx * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    normal_map[:, :, 1] = np.clip((ny * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    normal_map[:, :, 2] = np.clip((nz * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    
    # Save
    img = Image.fromarray(normal_map, 'RGB')
    img.save(output_path)
    print(f"  Saved: {output_path}")

def batch_generate(input_dir, output_dir, strength=2.0):
    """Generate normal maps for all textures in a directory"""
    os.makedirs(output_dir, exist_ok=True)
    
    texture_files = glob.glob(os.path.join(input_dir, "*.png"))
    texture_files += glob.glob(os.path.join(input_dir, "*.bmp"))
    texture_files += glob.glob(os.path.join(input_dir, "*.tga"))
    
    print(f"Found {len(texture_files)} textures")
    
    for i, texture_path in enumerate(texture_files, 1):
        filename = os.path.basename(texture_path)
        name = os.path.splitext(filename)[0]
        output_path = os.path.join(output_dir, f"{name}_n.png")
        
        print(f"[{i}/{len(texture_files)}] {filename}")
        try:
            generate_normal_map(texture_path, output_path, strength)
        except Exception as e:
            print(f"  Error: {e}")

def main():
    if len(sys.argv) < 2:
        print("GTA SA Texture Normal Map Generator")
        print("=" * 40)
        print()
        print("Commands:")
        print("  extract <txd_file> <output_dir>")
        print("    Extract all textures from a TXD file")
        print()
        print("  generate <input_texture> <output_normal> [strength]")
        print("    Generate normal map from a single texture")
        print("    strength: 0.5-5.0 (default: 2.0)")
        print()
        print("  batch <input_dir> <output_dir> [strength]")
        print("    Generate normal maps for all textures in directory")
        print()
        print("Examples:")
        print("  python gta_normalmap_gen.py extract plant1.txd textures/")
        print("  python gta_normalmap_gen.py generate water.png water_n.png 2.5")
        print("  python gta_normalmap_gen.py batch textures/ normals/")
        sys.exit(1)
    
    command = sys.argv[1]
    
    if command == "extract":
        if len(sys.argv) < 4:
            print("Usage: extract <txd_file> <output_dir>")
            sys.exit(1)
        extract_txd(sys.argv[2], sys.argv[3])
    
    elif command == "generate":
        if len(sys.argv) < 4:
            print("Usage: generate <input_texture> <output_normal> [strength]")
            sys.exit(1)
        strength = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
        generate_normal_map(sys.argv[2], sys.argv[3], strength)
    
    elif command == "batch":
        if len(sys.argv) < 4:
            print("Usage: batch <input_dir> <output_dir> [strength]")
            sys.exit(1)
        strength = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
        batch_generate(sys.argv[2], sys.argv[3], strength)
    
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)

if __name__ == "__main__":
    main()
