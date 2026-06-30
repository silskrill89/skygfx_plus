#!/usr/bin/env python3
"""
Generate environment map TXD for vehicle materials
Creates chrome, paint, rubber, glass env maps
"""

import struct
import os

# TXD format constants
TEX_DICT_STRUCT = 0x01
TEX_DICT = 0x16
TEX_STRUCT = 0x01
TEX_NAME = 0x02
TEX_DATA = 0x03

def write_txd_header(f):
    """Write TXD file header"""
    # Section type
    f.write(struct.pack('<I', TEX_DICT))
    # Section size (will be updated later)
    f.write(struct.pack('<I', 0))
    # Version
    f.write(struct.pack('<I', 0x36003))
    # Struct section
    f.write(struct.pack('<I', TEX_DICT_STRUCT))
    f.write(struct.pack('<I', 4))
    # Number of textures
    f.write(struct.pack('<I', 4))  # 4 textures: chrome, paint, rubber, glass

def write_texture(f, name, width, height, data):
    """Write a texture to TXD"""
    # Texture struct
    f.write(struct.pack('<I', TEX_STRUCT))
    # Size will be calculated
    f.write(struct.pack('<I', 0))  # placeholder
    
    # Texture name (32 bytes)
    name_bytes = name.encode('ascii')[:31].ljust(32, b'\x00')
    f.write(name_bytes)
    
    # Texture data
    f.write(struct.pack('<I', TEX_DATA))
    f.write(struct.pack('<I', len(data) + 8))
    f.write(data)

def generate_chrome_envmap(width=64, height=64):
    """Generate chrome environment map - highly reflective, smooth"""
    data = bytearray()
    for y in range(height):
        for x in range(width):
            # Create a smooth gradient for chrome reflection
            r = int(200 + 55 * (x / width))
            g = int(200 + 55 * (y / height))
            b = int(200 + 55 * ((x + y) / (width + height)))
            a = 255
            data.extend([r, g, b, a])
    return bytes(data)

def generate_paint_envmap(width=64, height=64):
    """Generate paint environment map - colored reflections"""
    data = bytearray()
    for y in range(height):
        for x in range(width):
            # Create a colored gradient for paint
            r = int(100 + 100 * (x / width))
            g = int(100 + 50 * (y / height))
            b = int(150 + 50 * ((x + y) / (width + height)))
            a = 200
            data.extend([r, g, b, a])
    return bytes(data)

def generate_rubber_envmap(width=64, height=64):
    """Generate rubber environment map - very low reflectivity"""
    data = bytearray()
    for y in range(height):
        for x in range(width):
            # Dark, low reflectivity for rubber
            r = int(20 + 10 * (x / width))
            g = int(20 + 10 * (y / height))
            b = int(20 + 10 * ((x + y) / (width + height)))
            a = 255
            data.extend([r, g, b, a])
    return bytes(data)

def generate_glass_envmap(width=64, height=64):
    """Generate glass environment map - transparent with subtle reflection"""
    data = bytearray()
    for y in range(height):
        for x in range(width):
            # Subtle, transparent reflection for glass
            r = int(180 + 75 * (x / width))
            g = int(180 + 75 * (y / height))
            b = int(200 + 55 * ((x + y) / (width + height)))
            a = 128  # Semi-transparent
            data.extend([r, g, b, a])
    return bytes(data)

def main():
    output_dir = r'E:\dev(dave)\skygfx_plus_expIV\resources\textures'
    os.makedirs(output_dir, exist_ok=True)
    
    # Generate env maps
    chrome_data = generate_chrome_envmap()
    paint_data = generate_paint_envmap()
    rubber_data = generate_rubber_envmap()
    glass_data = generate_glass_envmap()
    
    # Save as individual textures for now
    # (TXD creation would need a proper TXD writer)
    print("Generated environment map data:")
    print(f"  Chrome: {len(chrome_data)} bytes")
    print(f"  Paint:  {len(paint_data)} bytes")
    print(f"  Rubber: {len(rubber_data)} bytes")
    print(f"  Glass:  {len(glass_data)} bytes")
    
    # Save as raw RGBA data files
    with open(os.path.join(output_dir, 'env_chrome.raw'), 'wb') as f:
        f.write(chrome_data)
    with open(os.path.join(output_dir, 'env_paint.raw'), 'wb') as f:
        f.write(paint_data)
    with open(os.path.join(output_dir, 'env_rubber.raw'), 'wb') as f:
        f.write(rubber_data)
    with open(os.path.join(output_dir, 'env_glass.raw'), 'wb') as f:
        f.write(glass_data)
    
    print(f"\nSaved to {output_dir}")
    print("Note: TXD creation requires specialized tools")

if __name__ == '__main__':
    main()
