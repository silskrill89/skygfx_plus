# Wheel Texture Atlas System

## Overview
Combine multiple wheel textures into a single atlas to reduce draw calls and improve performance.
One 512x512 atlas can hold 4-16 wheel textures depending on size.

## Atlas Layout

### 512x512 Atlas (Recommended)
```
+----------------+----------------+
|                |                |
|   Wheel 01     |   Wheel 02     |
|   (256x256)    |   (256x256)    |
|                |                |
+----------------+----------------+
|                |                |
|   Wheel 03     |   Wheel 04     |
|   (256x256)    |   (256x256)    |
|                |                |
+----------------+----------------+
```

### 1024x1024 Atlas (High Detail)
```
+--------+--------+--------+--------+
|  W01   |  W02   |  W03   |  W04   |
| 256x256| 256x256| 256x256| 256x256|
+--------+--------+--------+--------+
|  W05   |  W06   |  W07   |  W08   |
| 256x256| 256x256| 256x256| 256x256|
+--------+--------+--------+--------+
|  W09   |  W10   |  W11   |  W12   |
| 256x256| 256x256| 256x256| 256x256|
+--------+--------+--------+--------+
|  W13   |  W14   |  W15   |  W16   |
| 256x256| 256x256| 256x256| 256x256|
+--------+--------+--------+--------+
```

## Texture Channels

### Diffuse Atlas (wheel_atlas_diffuse.png)
- **R/G/B**: Base color (rim paint, hub color)
- **A**: Specular mask (1.0 = shiny, 0.0 = matte)

### Normal Atlas (wheel_atlas_normal.png)
- **R/G**: XY normal vector
- **B**: Unused (or height map)
- **A**: Unused

### Specular Atlas (wheel_atlas_spec.png)
- **R**: Specular intensity
- **G**: Glossiness (inverse roughness)
- **B**: Unused
- **A**: Unused

## UV Mapping

### Per-Wheel UV Coordinates
Each wheel's UVs must be remapped to atlas quadrant:
```
Original UV: (0.0-1.0, 0.0-1.0)
Atlas UV:    (0.0-0.5, 0.0-0.5)  // Wheel 01 (top-left)
Atlas UV:    (0.5-1.0, 0.0-0.5)  // Wheel 02 (top-right)
Atlas UV:    (0.0-0.5, 0.5-1.0)  // Wheel 03 (bottom-left)
Atlas UV:    (0.5-1.0, 0.5-1.0)  // Wheel 04 (bottom-right)
```

### UV Remapping Formula
```python
def remap_uv(uv, atlas_x, atlas_y, atlas_size):
    """
    uv: original UV coordinate (0.0-1.0)
    atlas_x: atlas quadrant X (0 or 1)
    atlas_y: atlas quadrant Y (0 or 1)
    atlas_size: atlas dimension (e.g., 512)
    """
    quadrant_size = 0.5  # 2x2 grid
    new_u = uv[0] * quadrant_size + atlas_x * quadrant_size
    new_v = uv[1] * quadrant_size + atlas_y * quadrant_size
    return (new_u, new_v)
```

## Material Assignment

### Single Material Per Atlas
All wheels in atlas share one material:
```cpp
Material wheel_atlas_material;
wheel_atlas_material.diffuse_texture = "wheel_atlas_diffuse.png";
wheel_atlas_material.normal_texture = "wheel_atlas_normal.png";
wheel_atlas_material.specular_texture = "wheel_atlas_spec.png";
```

### Per-Wheel UV Offset
Each wheel atomic has unique UV offset to sample correct atlas quadrant:
```cpp
// Wheel 01 (top-left)
atomic->SetUVOffset(0.0, 0.0);

// Wheel 02 (top-right)
atomic->SetUVOffset(0.5, 0.0);

// Wheel 03 (bottom-left)
atomic->SetUVOffset(0.0, 0.5);

// Wheel 04 (bottom-right)
atomic->SetUVOffset(0.5, 0.5);
```

## Creation Pipeline

### Step 1: Extract Individual Textures
```python
for wheel in extracted_wheels:
    texture = extract_texture(wheel.dff)
    texture.save(f"temp/{wheel.name}_diffuse.png")
```

### Step 2: Resize to Uniform Size
```python
for texture in textures:
    resized = texture.resize((256, 256))
    resized.save(f"temp/{texture.name}_256.png")
```

### Step 3: Pack into Atlas
```python
atlas = Image.new('RGB', (512, 512))
atlas.paste(wheel_01, (0, 0))
atlas.paste(wheel_02, (256, 0))
atlas.paste(wheel_03, (0, 256))
atlas.paste(wheel_04, (256, 256))
atlas.save("wheel_atlas_diffuse.png")
```

### Step 4: Remap Wheel UVs
```python
for i, wheel in enumerate(wheels):
    quadrant_x = i % 2
    quadrant_y = i // 2
    remap_wheel_uvs(wheel, quadrant_x, quadrant_y)
```

### Step 5: Assign Atlas Material
```python
for wheel in wheels:
    wheel.SetMaterial(atlas_material)
```

## Performance Benefits

### Draw Call Reduction
- **Before**: 16 wheels × 3 textures = 48 draw calls
- **After**: 1 atlas × 3 textures = 3 draw calls
- **Savings**: 94% reduction

### Memory Usage
- **Before**: 16 × 512x512 = 16MB
- **After**: 1 × 1024x1024 = 4MB
- **Savings**: 75% reduction

### Texture Filtering
- Single atlas = consistent filtering across all wheels
- No seam artifacts between wheels
- Better mipmapping

## Metadata
```json
{
  "atlas": {
    "diffuse": "wheel_atlas_diffuse.png",
    "normal": "wheel_atlas_normal.png",
    "specular": "wheel_atlas_spec.png",
    "size": [512, 512],
    "layout": [
      {"wheel": "wheel_sport_01_uni", "quadrant": [0, 0]},
      {"wheel": "wheel_sport_02_uni", "quadrant": [1, 0]},
      {"wheel": "wheel_alloy_01_uni", "quadrant": [0, 1]},
      {"wheel": "wheel_alloy_02_uni", "quadrant": [1, 1]}
    ]
  }
}
```

## Testing
- Verify all wheels sample correct atlas quadrant
- Check for UV bleeding between quadrants
- Measure draw call reduction
- Validate texture memory savings
- Test mipmapping quality at distance
