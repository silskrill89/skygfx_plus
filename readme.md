# SkyGFX Plus

A rewrite of [SkyGFX](https://github.com/aap/skygfx) by aap, bringing modern rendering features to GTA San Andreas while preserving the PS2 aesthetic.

## Screenshots

![Screenshot 1](screenshots/01.jpg)
![Screenshot 2](screenshots/02.jpg)
![Screenshot 3](screenshots/03.jpg)
![Screenshot 4](screenshots/04.jpg)
![Screenshot 5](screenshots/05.jpg)
![Screenshot 6](screenshots/06.jpg)
![Screenshot 7](screenshots/07.jpg)
![Screenshot 8](screenshots/08.jpg)
![Screenshot 9](screenshots/09.jpg)
![Screenshot 10](screenshots/10.jpg)

## Features

### Anti-Aliasing
- **SMAA** (Subpixel Morphological Anti-Aliasing) - 3-pass implementation with edge detection, blend weight calculation, and neighborhood blending
- Presets: LOW, MEDIUM, HIGH, ULTRA
- Configurable threshold and search steps

### Ambient Occlusion
- **SSAO** (Screen-Space Ambient Occlusion) - depth-based with configurable radius, power, and sample count
- Uses INTZ depth format when available

### Color Correction
- PS2 color filter with proper gamma correction for PC (gamma 2.2)
- Cross-mixable color filters (PS2, PC, Mobile, III, VC, VCS, GTAIV)
- GTA IV filmic tonemapping (Uncharted 2/Hable curve)
- YCbCr color correction

### Vehicle Rendering
- **PBR Vehicle Shader** - GGX specular, Fresnel reflection, material classification
- Support for 4 vehicle color channels (MAT1-MAT4)
- Chrome, paint, rubber, and glass material separation
- PS2 spherical environment mapping
- Dirt and rust layers

### Post-Processing
- **Motion Blur** - Burnout Paradise-style speed-based blur
- **SSS** (Subsurface Scattering) - post-process skin translucency
- **Skin Enhancement** - wrap lighting for SSS approximation
- **Hair Enhancement** - anisotropic highlights (Kajiya-Kay)
- **Vegetation Enhancement** - SSS-like translucency for grass
- **Edge Tessellation** - vertex displacement at edges

### Presets
- `gtaiv_modern.ini` - GTA IV style with PBR, all modern features
- `gtav_style.ini` - GTA V style, warm tones, soft reflections
- `classic_sa.ini` - Faithful PS2 look, no modern features
- `balanced.ini` - Best of all worlds, good performance

## Installation

1. Copy `skygfx.asi` to your GTA SA directory
2. Copy `skygfx.ini` to the same directory
3. Copy the `presets/` folder to the same directory (optional, for preset switching)
4. Edit `skygfx.ini` to configure features

## Configuration

Edit `skygfx.ini` to enable/disable features:

```ini
; Select a preset (set to "custom" to use settings below)
presetFile=custom

; Enable SMAA
smaaEnable=1
smaaPreset=3

; Enable SSAO
ssaoEnable=1

; Enable vehicle PBR
vehiclePipe=Neo
```

## Building

Requires:
- Visual Studio 2015+ (v140 toolset)
- DirectX SDK (June 2010)
- Python 3 (for shader compilation)

Build with:
```
msbuild build\skygfx.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Compile shaders with:
```
python tools\compile_shaders.py
```

## Credits

### Core Contributors
- **aap** - Original SkyGFX author
- **Junior (JuniorDjjr)** - JuniorDjjr fork with extensive features
- **DK22Pac** - Normal mapping plugin
- **Silent** - SilentPatch and GTA SA fixes
- **_AG** - RenderWare documentation

### Community
- **The GTA Modding Community** - Reverse engineering, documentation, countless hours of research
- **GTAForums** - Community knowledge base
- **Everyone who contributed** to SkyGFX and its forks

### SMAA Algorithm
- Jorge Jimenez, Jose I. Echevarria, Tiago Sousa, Diego Gutierrez, Fernando Navarro, Belen Masia
- University of Zaragoza, Spain

### FXAA
- Timothy Lottes (NVIDIA)

### Inspiration
- **Crytek** - CryEngine rendering techniques (SSS, GGX BRDF, edge detection)
- **Rockstar Games** - GTA IV, GTA V, RDR2 rendering techniques
- **Criterion Games** - RenderWare Graphics engine
- **O3DE Foundation** - Atom renderer PBR pipeline

### Academic Research
- Jensen et al. (2001) - Subsurface light transport
- Walter et al. (2007) - GGX microfacet models
- Hable (Uncharted 2) - Filmic tonemapping
- Burley (Disney, 2012) - Disney BRDF

### Open Source
- Dear ImGui, stb libraries, Khronos Group, Microsoft DirectX SDK

## Permissions

**No source code from Crytek, Rockstar, or any commercial entity has been used.**

All code is original work inspired by:
- Published academic research
- Public documentation (DirectX 9 SDK, RenderWare docs)
- Community reverse engineering of GTA SA
- Publicly known rendering techniques

## License

Based on SkyGFX by aap. SMAA is BSD licensed. See `SMAA_reference/LICENSE.txt`.
