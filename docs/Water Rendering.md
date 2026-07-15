# Water Rendering

#water #rendering

## Overview

Procedural PBR water rendering with parallax occlusion mapping and refraction. Implemented in `waterPipe.cpp`.

## Water Parallax Shader

**VS**: `Water_VS.hlsl` | **PS**: `Water_Parallax.hlsl`

Fully procedural normals — no normal map texture dependencies. Uses Gerstner wave simulation for wave shape.

### Features
- **Gerstner Waves**: 3 configurable wave directions with steepness and wavelength
- **Parallax Occlusion Mapping**: Wave detail via ray-marched parallax
- **Scene Refraction**: Copies scene render target for underwater refraction
- **Fresnel Reflection**: Configurable fresnel power for view-dependent reflectivity
- **Specular Highlights**: Configurable specular power and intensity
- **Foam**: Threshold-based foam generation with softness control
- **Shallow/Deep Color Blending**: Depth-based color transition from shallow to deep water

### Gerstner Wave Parameters

| Wave | Direction | Steepness | Wavelength |
|------|-----------|-----------|------------|
| 1 | (1.0, 0.0) | 0.25 | 60.0 |
| 2 | (0.3, 1.0) | 0.15 | 31.0 |
| 3 | (-0.2, 0.8) | 0.1 | 18.0 |

## Config

```ini
waterParallaxEnable=1
waterParallaxScale=...
waterNormalStrength=...
waterFresnelPower=...
waterSpecularPower=...
waterSpecularIntensity=...
waterUnderwaterFog=...
waterFoamThreshold=...
waterFoamSoftness=...
waterShallowR, waterShallowG, waterShallowB
waterDeepR, waterDeepG, waterDeepB
waterReflectionFarClip=...
```

## Pipeline

```
waterPipe_setRenderState()
  ├── Copy scene to refraction texture
  ├── Set Water_VS + Water_Parallax shaders
  ├── Upload Gerstner wave constants
  ├── Upload camera/time/sun parameters
  └── Bind scene texture for refraction

waterPipe_restoreRenderState()
  └── Unbind water shaders and textures
```

## See Also

- [[PostFX Pipeline]] — Other effects
- [[INI Configuration]] — Config fields
