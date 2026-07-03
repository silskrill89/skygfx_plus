# BRDF Reference

#brdf #pbr #disney #callisto

## Disney BSDF (SIGGRAPH 2012/2015)
Based on Brent Burley's work at Disney. Not strictly physically based but designed with PBR principles.

### Key Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| baseColor | RGB | Surface albedo |
| metallic | 0-1 | Dielectric vs metallic blend |
| roughness | 0-1 | Surface roughness (1=glossiness) |
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
roughness = 1.0 - glossiness

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

## References
- Disney BRDF Explorer (2012): https://disney-animation.s3.amazonaws.com/library/BRDFExplorer/index.html
- Burley 2015: Extending Disney BRDF to BSDF with SSS
- O3DE Enhanced PBR: https://github.com/o3de/o3de/blob/development/Gems/Atom/Feature/Common/Assets/Materials/Types/EnhancedPBR.materialtype
- Schuttejoe Disney BSDF: https://schuttejoe.github.io/post/disneybsdf/
- Callisto Protocol: SIGGRAPH 2023 Advances

## See Also
- [[Feature Hook Pattern]] — How to add new PBR features
- [[Shader Architecture]] — How shaders compile
