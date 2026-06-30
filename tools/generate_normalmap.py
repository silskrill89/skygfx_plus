#!/usr/bin/env python3
"""
Normal Map Generator for GTA SA Textures
Generates normal maps from diffuse textures using Sobel filter
Usage: python generate_normalmap.py input.png output.png [strength]
"""

import sys
import os
import struct
import math

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Installing required packages...")
    os.system("pip install Pillow numpy")
    from PIL import Image
    import numpy as np

def load_image(path):
    """Load image and convert to grayscale float array"""
    img = Image.open(path).convert('L')
    return np.array(img, dtype=np.float32) / 255.0

def sobel_filter(heightmap):
    """Apply Sobel filter to get height gradients"""
    h, w = heightmap.shape
    
    # Sobel kernels
    gx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], dtype=np.float32)
    gy = np.array([[-1, -2, -1], [0, 0, 0], [1, 2, 1]], dtype=np.float32)
    
    # Apply convolution
    dx = np.zeros_like(heightmap)
    dy = np.zeros_like(heightmap)
    
    for y in range(1, h-1):
        for x in range(1, w-1):
            region = heightmap[y-1:y+2, x-1:x+2]
            dx[y, x] = np.sum(region * gx)
            dy[y, x] = np.sum(region * gy)
    
    return dx, dy

def generate_normal_map(heightmap, strength=1.0):
    """Generate normal map from heightmap"""
    h, w = heightmap.shape
    
    # Apply Sobel filter
    dx, dy = sobel_filter(heightmap)
    
    # Calculate normal vectors
    strength = max(0.01, strength)
    
    # Normal = normalize(-dx * strength, -dy * strength, 1.0)
    nx = -dx * strength
    ny = -dy * strength
    nz = np.ones_like(heightmap)
    
    # Normalize
    length = np.sqrt(nx*nx + ny*ny + nz*nz)
    nx /= length
    ny /= length
    nz /= length
    
    # Convert from [-1,1] to [0,255]
    normal_map = np.zeros((h, w, 3), dtype=np.uint8)
    normal_map[:, :, 0] = np.clip((nx * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    normal_map[:, :, 1] = np.clip((ny * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    normal_map[:, :, 2] = np.clip((nz * 0.5 + 0.5) * 255, 0, 255).astype(np.uint8)
    
    return normal_map

def create_txd_with_normalmap(texture_name, normal_data, width, height, output_path):
    """Create a TXD file containing a normal map texture"""
    
    # TXD format header
    # Section ID for TEX_DICTIONARY = 0x16
    # Section ID for TEX_NAMED = 0x02
    
    # For simplicity, we'll create a raw RGBA DDS-like texture
    # and let the game load it through the normal map plugin
    
    # Save as PNG for now - the normalmap plugin can load PNG files
    img = Image.fromarray(normal_data, 'RGB')
    img.save(output_path)
    print(f"Saved normal map: {output_path}")

def main():
    if len(sys.argv) < 3:
        print("Usage: python generate_normalmap.py <input_texture> <output_normal> [strength]")
        print("  input_texture: Path to diffuse texture (PNG, BMP, TGA)")
        print("  output_normal: Path for output normal map")
        print("  strength: Normal map strength (default: 2.0)")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    strength = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
    
    print(f"Loading: {input_path}")
    heightmap = load_image(input_path)
    print(f"Size: {heightmap.shape[1]}x{heightmap.shape[0]}")
    
    print(f"Generating normal map (strength={strength})...")
    normal_map = generate_normal_map(heightmap, strength)
    
    print(f"Saving: {output_path}")
    create_txd_with_normalmap("water_n", normal_map, 
                              heightmap.shape[1], heightmap.shape[0], 
                              output_path)
    
    print("Done!")

if __name__ == "__main__":
    main()
