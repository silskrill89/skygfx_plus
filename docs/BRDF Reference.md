# BRDF Reference

#brdf #pbr #disney #callisto #cryengine

## Overview
Combined BRDF reference covering Disney BSDF theory, CryEngine PBR material values, and measured BRDF data. Used for authoring material parameters in the building and vehicle PBR shaders.

---

## Disney BSDF (SIGGRAPH 2012/2015)
Based on Brent Burley's work at Disney. Not strictly physically based but designed with PBR principles.

### Key Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| baseColor | RGB | Surface albedo |
| metallic | 0-1 | Dielectric vs metallic blend |
| roughness | 0-1 | Surface roughness (1=rough, 0=smooth) |
| specTrans | 0-1 | Specular transmission |
| clearcoat | 0-1 | Clear coat layer intensity |
| clearcoatGloss | 0-1 | Clear coat smoothness |
| sheen | 0-1 | Grazing angle retro-reflection |
| sheenTint | 0-1 | Sheen color (0=white, 1=tinted) |
| anisotropic | 0-1 | Anisotropic roughness |
| flatness | 0-1 | Thin surface flatness |
| diffTrans | 0-1 | Diffuse transmission |
| ior | 1.0-2.5 | Index of refraction |

### Glossiness Workflow
Glossiness is the inverse of roughness:
```
roughness = 1.0 - glossiness
```
The building PBR shader uses glossiness: c22.x = glossiness.

### Water PBR
- F0 = 0.02 (IOR 1.333)
- Roughness = 0.02
- Absorption = 0.45
- Shallow: RGB(0.13, 0.45, 0.55)
- Deep: RGB(0.01, 0.03, 0.08)

### BRDF Lobes
1. **Specular BRDF** — GGX/Smith (GTR2 anisotropic)
2. **Diffuse BRDF** — Disney diffuse with retro-reflection
3. **Clearcoat** — GTR1 with fixed roughness 0.25, IOR 1.5
4. **Sheen** — Grazing angle Fresnel (5th power)
5. **Specular BSDF** — Refraction with dielectric Fresnel

### Key Equations
```
F_Schlick(cosTheta, F0) = F0 + (1 - F0) * (1 - cosTheta)^5
D_GGX(NdotH, roughness) = a^2 / (pi * d^2)  where d = NdotH^2 * (a^2 - 1) + 1
V_SmithCorrelated(NdotV, NdotL, roughness) = 0.5 / (sqrt(GGXV) * sqrt(GGXL))
```

### Fresnel Blending
```
R0 = lerp(dielectricF0, baseColor, metallic)
F = lerp(dielectricFresnel, metallicFresnel, metallic)
```

---

## Material Table (Glossiness Workflow)

All dielectric F0 values computed from IOR: `F0 = ((n-1)/(n+1))^2`.

### MAN-MADE SURFACES (buildings, infrastructure)
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Concrete rough       | 0.10       | 0.040       | 0.0       | 0.8     | Cast concrete, unfinished |
| Concrete smooth      | 0.35       | 0.042       | 0.0       | 0.9     | Polished concrete floor |
| Concrete painted     | 0.40       | 0.040       | 0.3       | 1.0     | Painted concrete wall |
| Brick common         | 0.15       | 0.044       | 0.0       | 0.7     | Red brick |
| Brick painted        | 0.30       | 0.040       | 0.2       | 0.8     | Painted brick |
| Stucco               | 0.10       | 0.040       | 0.0       | 0.6     | Rough stucco render |
| Plaster              | 0.25       | 0.040       | 0.0       | 0.7     | Interior plaster wall |
| Tile ceramic         | 0.65       | 0.048       | 0.4       | 1.0     | Bathroom/kitchen tile |
| Tile porcelain       | 0.80       | 0.045       | 0.5       | 1.1     | High-end porcelain |
| Marble polished      | 0.85       | 0.045       | 0.3       | 1.2     | Polished marble |
| Granite polished     | 0.80       | 0.045       | 0.2       | 1.1     | Polished granite counter |
| Stone rough cut      | 0.15       | 0.040       | 0.0       | 0.8     | Rough stone wall |
| Asphalt              | 0.10       | 0.040       | 0.0       | 0.5     | Road surface |
| Asphalt wet          | 0.50       | 0.040       | 0.0       | 0.7     | Wet road |
| Sand                 | 0.10       | 0.040       | 0.0       | 0.6     | Sand surface |
| Dirt                 | 0.05       | 0.040       | 0.0       | 0.5     | Dry dirt |

### GLASS SURFACES
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Glass clear          | 0.95       | 0.040       | 0.0       | 2.0     | Clear window glass |
| Glass tinted         | 0.90       | 0.042       | 0.0       | 1.8     | Tinted window |
| Mirror               | 1.00       | 0.040       | 0.0       | 2.5     | Perfect mirror |
| Plastic opaque       | 0.50       | 0.040       | 0.8       | 1.2     | Opaque plastic (ABS) |

### METAL SURFACES (metalness = 1)
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Steel                | 0.65       | 0.560       | 0.0       | 1.5     | Brushed steel |
| Chrome               | 0.95       | 0.950       | 0.0       | 2.0     | Chrome plating |
| Copper               | 0.60       | 0.950       | 0.0       | 1.5     | Copper surface |
| Gold                 | 0.80       | 0.980       | 0.0       | 1.8     | Gold surface |
| Aluminum             | 0.70       | 0.910       | 0.0       | 1.5     | Brushed aluminum |

### WOOD SURFACES
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Wood painted         | 0.50       | 0.040       | 0.3       | 1.0     | Painted wood |
| Wood varnished       | 0.70       | 0.040       | 0.8       | 1.2     | Varnished/polished wood |
| Wood bare            | 0.25       | 0.038       | 0.0       | 0.7     | Unfinished wood |

### SPECIAL
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Water                | 0.98       | 0.020       | 0.0       | 1.0     | Still water |
| Rubber               | 0.10       | 0.040       | 0.0       | 0.4     | Rubber tire |
| Guardrail            | 0.40       | 0.560       | 0.0       | 1.0     | Metal guardrail |

---

## Shader Constants Layout (Building PBR)
```
c22 = {glossiness, reflectance, clearcoat, subsurface}
c23 = {specularInt, metalness, 0, 0}
```

## GTA SA Texture → BRDF Mapping (pattern-based)
- "glass", "window", "wndw" → Glass clear/tinted
- "concrete", "conc" → Concrete rough/smooth
- "brick", "brk" → Brick
- "metal", "mtl", "steel" → Metal
- "wood", "wd", "plank" → Wood
- "asphalt", "road", "pave" → Asphalt
- "rubber", "tire" → Rubber

---

## References
- Disney BRDF Explorer (2012): https://disney-animation.s3.amazonaws.com/library/BRDFExplorer/index.html
- Burley 2015: Extending Disney BRDF to BSDF with SSS
- CryEngine 5 PBR Texture Guidelines
- Naty Hoffman SIGGRAPH 2013 "Physics and Math of Shading"
- refractiveindex.info (IOR data)
- **TOG2021: "Non-Parametric Sparse BRDF"** — Gaussian basis fitting from MERL measured data (100+ real-world materials)

## See Also
- [[Feature Hook Pattern]] — How to add new PBR features
- [[Shader Architecture]] — How shaders compile
- [[PBR Common]] — Shared functions used in BRDF implementation
