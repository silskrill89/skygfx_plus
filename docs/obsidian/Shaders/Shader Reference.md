# Shader Reference

## Register Layout Conventions

SkyGFX Plus shaders follow a consistent register layout across all pipeline variants.

### Vertex Shader Constants (c0-c29+)

| Register | Content | Used By |
|----------|---------|---------|
| c0-c3 | World-View-Projection (WVP) matrix | All VS |
| c4-c7 | World matrix (rows 0-3) | All VS |
| c8-c11 | View matrix (rows 0-3) | GTAIV VS |
| c12-c15 | Projection matrix | GTAIV VS |
| c16 | UV Scale/Offset (xy=scale, zw=offset) | Vehicle, Building VS |
| c17 | Time-of-day interpolation (x) | Building DN VS |
| c18 | Fog params (x=density, y=start, z=end, w=unused) | All VS |
| c19 | Shininess/Spec power (x), Fresnel (y) | Vehicle VS |
| c20-c23 | Light 0 Direction (xyz, w=type) | Vehicle VS |
| c24-c27 | Light 1 Direction | Vehicle VS |
| c28-c29 | Env map view vector | Env car VS |

### Pixel Shader Constants (c0-c31)

| Register | Content | Used By |
|----------|---------|---------|
| c0-c3 | WVP inverse/transpose | Building PS |
| c4 | Ambient colour (rgba) | All PS |
| c5-c7 | Directional light colours (3 lights) | Vehicle PS |
| c8-c10 | Directional light directions | Vehicle PS |
| c11-c13 | Extra directional colours | Vehicle PS |
| c14 | Material colour (rgba) | All PS |
| c15 | UV Scale/Offset | Building PS |
| c16 | Fog colour (rgb), Fog density (a) | All PS |
| c17 | Time-of-day interpolation | Building DN PS |
| c18 | Shininess, SpecPower, Fresnel, EnvIntensity | Vehicle PS |
| c19 | PS2 modulation flags (x), TFX mode (y) | PS2 PS |
| c20-c23 | SSAO params (radius, power, kernel, samples) | SSAO PS |
| c24 | SMAA params (threshold, corner, search steps) | SMAA PS |
| c25-c31 | GTA IV postFX (desat, gamma, vignette, bloom, exposure) | GTAIV PS |

> [!warning] Historical sketch vs. current PBR contract
> The c0-c31 table above is a generic/historical SM3.0 sketch, not the live PBR layout. Current PBR contract: `c22`/`c23` carry the material parameter blocks and MUST be written only through `pipeUploadPBR()` (`src/render/pipelinecommon.cpp`), never uploaded manually. `s9` + `c49-c52` are reserved for the packed noise system. The next free PS constant is `c53`; the next free sampler is `s10`. See the "Noise-Detail Building Variants" section below.

## Shader Compilation Targets

| Profile | Max Instructions | Interpolators | Temp Regs | Flow Control | Samples |
|---------|-----------------|---------------|-----------|--------------|---------|
| vs_3_0 | 512+ | 10 | 32 | Dynamic | — |
| ps_3_0 | 512+ | 10 | 32 | Dynamic | 16 |
| ps_2_0 | 96 (64 ALU + 32 TEX) | 8 | 12 | Static only | 16 |

SkyGFX Plus targets **ps_3_0 / vs_3_0** only — the current build has no ps_2_0 fallback path.

## Key Shader Implementations

### Simple Pipeline (simplePS.hlsl)

```hlsl
// Default building/texture passthrough
float4 main(float2 tex : TEXCOORD0, float4 color : COLOR0) : COLOR {
    return tex2D(diffuseSampler, tex) * color;
}
```

**Used by**: Default pipeline, fallback rendering.

### PS2 Building Pipeline (ps2BuildingPS)

Key differences from PC:
- `MODULATE2X` equivalent: `(tex * color) * 2`
- RGB channel swap for PS2 swizzle correction
- Dither emulation via screen-space position hash
- Per-vertex fog applied after modulation

### GTA IV Forward+ (GTAIVForwardPlus_ps.hlsl)

The most complex shader in the system:

```hlsl
// Light loop: up to 8 dynamic lights per tile
for (int i = 0; i < lightCount; i++) {
    Light light = lights[i];
    float3 L = normalize(light.position - worldPos);
    float3 H = normalize(V + L);

    // GGX normal distribution
    float NDF = GGX(N, H, roughness);

    // Smith correlated visibility
    float G = Smith(N, V, L, roughness);

    // Schlick Fresnel
    float3 F = Schlick(F0, H, V);

    // Cook-Torrance BRDF
    float3 spec = (NDF * G * F) / (4 * dot(N,V) * dot(N,L) + 0.0001);

    // Burley diffuse (vehicles) / Oren-Nayar (buildings)
    float3 diff = BurleyDiffuse(N, L, V, roughness);

    result += (diff * albedo + spec) * light.color * attenuation;
}
```

**Performance**: ~40 ALU instructions per light. With 8 lights = ~320 ALU + texture lookups. Balanced for SM 3.0.

### SSAO (SSAO_ps20.hlsl)

Two code paths via `SHADER_MODEL` define:

| Path | Samples | Quality |
|------|---------|---------|
| SM 3.0 | 16 | Full, with random rotation |
| SM 2.0 | 4 | Reduced quality, no rotation |

Implementation:
1. Reconstruct world position from depth buffer
2. Sample hemisphere around normal
3. Compare sample depth vs stored depth
4. Accumulate occlusion factor
5. Apply bilateral blur (separated passes)

### SMAA (SMAA_ps20.hlsl)

Subpixel Morphological Anti-Aliasing implementation:

1. **Edge detection**: Luma/color threshold (configurable `smaaThreshold`)
2. **Pattern calculation**: Search pattern along detected edges
3. **Blending weights**: Calculate coverage from pattern
4. **Corner detection**: Handle L-shaped patterns (`smaaCornerRounding`)
5. **Temporal AA** (optional): Subpixel jitter + history blend

Preset quality levels:

| Preset | Search Steps | Diagonal Detection | Corner Rounding |
|--------|-------------|-------------------|-----------------|
| LOW (0) | 4 | Off | 0 |
| MEDIUM (1) | 8 | Off | 25 |
| HIGH (2) | 16 | On | 50 |
| ULTRA (3) | 32 | On | 100 |

### Noise-Detail Building Variants (noiseDetailBuildingPS.hlsl)

`shaders/noiseDetailBuildingPS.hlsl` compiles three `ps_3_0` entries (multi-entry list in `tools/fast_build.py`):

| Entry point | CSO | Resource ID |
|-------------|-----|-------------|
| `main_simpleNoiseDetail` | `simpleNoiseDetailPS.cso` | `IDR_SIMPLENOISEDETAILPS` (278) |
| `main_simpleDetailNoiseDetail` | `simpleDetailNoiseDetailPS.cso` | `IDR_SIMPLEDETAILNOISEDETAILPS` (231) |
| `main_xboxBuildingNoiseDetail` | `xboxBuildingNoiseDetailPS.cso` | `IDR_XBOXBUILDINGNOISEDETAILPS` (227) |

- `shaders/include/Noise.hlsl` provides the packed noise tile, bound at `s9` with WRAP / LINEAR / no mips. Channel packing: `.r` Perlin fBm, `.g` Worley F1, `.b` white-noise hash, `.a` blue noise.
- The jitter samples the blue-noise channel through `NoiseBlue()` at `uv * 100` (second axis swizzled and offset by `0.37`), converts each channel to a zero-mean offset `(n - 0.5) * c52.x`, and adds that offset to the UV before the diffuse tap. `main_simpleDetailNoiseDetail` reuses one jitter for both taps so the detail map cannot slide against the base; the Xbox entry jitters only the diffuse tap and leaves the env tap unjittered.
- Register contract: `s9` + `c49-c52` are reserved for the noise system. `c49 {noiseScale, worleyScale, coverage, erosion}`, `c50 {whiteScale, blueScale, timeSeconds, frameIndex}`, `c51 {cloudScaleA, cloudScaleB, cloudScaleC, cloudDensity}`, `c52 {jitterAmount, ditherAmount, detailStrength, unused}`. Next free PS constant is `c53`; next free sampler is `s10`.
- Fallback: an unbound `s9` and/or `c52.x == 0` collapses the jitter to zero, so the shader degrades to a deterministic single tap matching the non-noise-detail shader. No branch is needed.
- Gated by `noiseDetail=1` (canonical INI key; `stochastic` / `stochasticTexturing` are deprecated aliases) and by the per-texture `noiseDetail=1` flag in `texdb.txt`.

### Subsurface Scattering (SubsurfaceScattering.hlsl)

From GPU Gems 3 / Crysis technique:

```hlsl
// Separable SSS via blur in texture space
// Pre-integrated skin LUT approximation
float3 sss = tex2D(skinLUT, float2(NdotL, curvature));
result = lerp(diffuse, sss, scatterWidth);
```

**Cost**: 2 texture lookups + 5 ALU instructions. Applied after main lighting.

## Sampler Bindings

| Sampler | Register | Texture | Used By |
|---------|----------|---------|---------|
| diffuseSampler | s0 | Main diffuse texture | All PS |
| detailSampler | s1 | Detail/overlay texture | Building, Detail PS |
| specularSampler | s2 | Specular map | Vehicle PS |
| normalSampler | s3 | Normal map | GTAIV, NormalMap PS |
| envSampler | s4 | Environment cube map | Vehicle, Env PS |
| shadowSampler | s5 | Shadow depth map | GTAIV PS |
| SSAOSampler | s6 | SSAO occlusion buffer | PostFX PS |

> [!warning] Historical sampler sketch
> The table above mixes several generations of shaders. Live material-pipe bindings: s0 diffuse, s1 environment map, s2 mask, s3 IBL cubemap, s4 screen-space normal buffer, s8 normal map (NM lane), s9 packed noise tile (`shaders/include/Noise.hlsl`). Next free sampler: `s10`.

## See Also

- [[Shader Architecture]]
- [[Pipelines/Overview]]
- [[PostEffects/Overview]]
