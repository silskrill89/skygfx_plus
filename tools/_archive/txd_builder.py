
"""
TXD Builder - Creates GTA SA TXD files from PNG images
Based on the RenderWare TXD format
"""

import struct
import os
from PIL import Image

# TXD Section IDs
rwID_TEXDICTIONARY = 0x16
rwID_STRUCT = 0x01
rwID_STRING = 0x02
rwID_TEXTURE = 0x03

# Platform IDs
rwID_D3D8 = 0x08
rwID_D3D9 = 0x09

def write_section(f, section_id, data):
    """Write a RW section to file"""
    f.write(struct.pack('<I', section_id))
    f.write(struct.pack('<I', len(data)))
    f.write(struct.pack('<I', 0x36003))  # Version
    f.write(data)

def create_texture_data(img, name):
    """Create texture data from PIL image"""
    width, height = img.size
    
    # Convert to RGBA
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    pixels = img.tobytes()
    
    # Texture struct data
    data = bytearray()
    
    # Platform ID (D3D9)
    data.extend(struct.pack('<I', rwID_D3D9))
    
    # Filter mode
    data.extend(struct.pack('<I', 0x0101))  # Linear | Linear
    
    # Address U, V
    data.extend(struct.pack('<HH', 1, 1))  # Wrap
    
    # Name (32 bytes)
    name_bytes = name.encode('ascii')[:31].ljust(32, b'\x00')
    data.extend(name_bytes)
    
    # Mask name (32 bytes)
    data.extend(b'\x00' * 32)
    
    # Platform ID again
    data.extend(struct.pack('<I', rwID_D3D9))
    
    # Format flags
    data.extend(struct.pack('<I', 0x0200))  # 32-bit ARGB
    
    # Width, Height
    data.extend(struct.pack('<HH', width, height))
    
    # Depth (32 for ARGB)
    data.extend(struct.pack('<B', 32))
    
    # Num mip levels
    data.extend(struct.pack('<B', 1))
    
    # Type (0 = regular)
    data.extend(struct.pack('<B', 0))
    
    # Flags (has alpha)
    data.extend(struct.pack('<B', 0x04))
    
    # Total size
    data.extend(struct.pack('<I', len(pixels)))
    
    # Mipmap data
    data.extend(pixels)
    
    return bytes(data)

def build_txd(input_dir, output_file, game='SA'):
    """Build TXD file from PNG images in directory"""
    print(f"Building TXD: {output_file}")
    
    # Find all PNG files
    png_files = sorted([f for f in os.listdir(input_dir) if f.lower().endswith('.png')])
    
    if not png_files:
        print("No PNG files found!")
        return False
    
    print(f"Found {len(png_files)} textures")
    
    with open(output_file, 'wb') as f:
        # TXD header
        f.write(struct.pack('<I', rwID_TEXDICTIONARY))
        f.write(struct.pack('<I', 0))  # Size placeholder
        f.write(struct.pack('<I', 0x36003))  # Version
        
        # Struct section
        f.write(struct.pack('<I', rwID_STRUCT))
        f.write(struct.pack('<I', 4))
        f.write(struct.pack('<I', 0x36003))
        f.write(struct.pack('<I', len(png_files)))
        
        # Process each texture
        for png_file in png_files:
            img_path = os.path.join(input_dir, png_file)
            name = os.path.splitext(png_file)[0]
            
            print(f"  Processing: {name}")
            
            try:
                img = Image.open(img_path)
                texture_data = create_texture_data(img, name)
                
                # Write texture section
                f.write(struct.pack('<I', rwID_TEXTURE))
                f.write(struct.pack('<I', len(texture_data)))
                f.write(struct.pack('<I', 0x36003))
                f.write(texture_data)
                
            except Exception as e:
                print(f"  Error: {e}")
                continue
        
        # Update header size
        file_size = f.tell()
        f.seek(4)
        f.write(struct.pack('<I', file_size - 12))
    
    print(f"TXD created: {output_file}")
    return True

def main():
    import sys
    
    if len(sys.argv) < 3:
        print("Usage: python txd_builder.py <input_dir> <output.txd>")
        print("Example: python txd_builder.py textures/ vehicles_effects.txd")
        return
    
    input_dir = sys.argv[1]
    output_file = sys.argv[2]
    
    if not os.path.exists(input_dir):
        print(f"Input directory not found: {input_dir}")
        return
    
    build_txd(input_dir, output_file, 'SA')

if __name__ == '__main__':
    main()
