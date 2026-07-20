# D3D9 Shader Architecture

> DirectX 9 shader model reference and skygfx_plus shader system architecture.
> Connects RW SDK D3D9 integration to actual shader code.
>
> **Note**: GTA SA uses RW 3.6.0.3. skygfx_plus uses RW 3.7 SDK headers for declarations and backports 3.7 features to the 3.6 game binary.

## DX9 Pipeline Overview (from RW D3D whitepaper)

### How RW Uses D3D9

RenderWare wraps D3D9 API. The D3D9 device is created at `RwEngineStart` and destroyed at `RwEngineStop`.

**Key insight**: Render states are **buffered (lazy updates)** — only the last state change before a draw call is sent to D3D9. This means multiple `RwRenderStateSet()` calls between draws are collapsed.

```cpp
// RW D3D9 state management:
RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);   // Buffered
RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);  // Buffered
RwRenderStateSet(rwRENDERSTATEALPHATEST, (void*)TRUE);     // Buffered
// ... only the last state before draw is sent to D3D9
```

### D3D9 Pipeline Node Architecture

The game's D3D9 pipeline node processes atomics through:
1. **Instance node**: Creates vertex buffers, sets stream sources
2. **Render node**: Sets shaders, textures, constants, draws primitives
3. **Uninstance node**: Cleans up instanced data

```
RxPipelineExecute(pipeline)
  → Node: D3D9AtomicAllInOne
    → Instance: vertex buffer creation, skinning
    → Render: set shaders, draw
    → Uninstance: cleanup
```

### Shader Model Capabilities (from ATI/NVIDIA docs)

#### Vertex Shaders

| Model | Inputs | Temps | Constants | Flow Control | Max Instructions |
|-------|--------|-------|-----------|--------------|-----------------|
| vs_1_1 | 16 (v0-v15) | 12 (r0-r11) | 96 | None | 128 |
| vs_2_0 | 16 | 12 | 256+ | Static (loop/if) | 256 slots / 1024 exec |
| vs_3_0 | 16 + samplers | 12 | 256+ | Dynamic (call/ret) | 512+ |

**vs_2_0 additions over vs_1_1**: `abs`, `crs` (cross), `mova`, `expp`, `lrp`, `nrm`, `pow`, `sgn`, `sincos`

#### Pixel Shaders

| Model | Temps | TC Iterators | Constants | Samplers | Max Instructions |
|-------|-------|-------------|-----------|----------|-----------------|
| ps_1_x | 2 | 2 (2D) | 8 | 4 | 8 |
| ps_2_0 | 12 | 8 (4D) | 32 | 16 | 64 ALU + 32 TEX |
| ps_2_x | 28 | 8 (4D) | 32 | 16 | 512 slots / 1024 exec |
| ps_3_0 | 32 | 10 (4D) | 32 | 16 | 512+ (dynamic flow) |

**Texture sampling**:
- ps_1_x: `tex`, `texbem`, `texbeml`, `texreg2ar`, `texreg2bg`
- ps_2_0: `texld`, `texldp`, `texldb`, `texkill` (4-level dependent read limit)
- ps_2_x: Adds `DSX`, `DSY`, `TEXLDD` (gradient fetches), no dependent read limit

### D3D9 Performance Guidelines (from RW whitepaper)

1. **Enable back face culling** — hardware-accelerated, free performance
2. **Alpha blending is expensive** — cull particles close to camera
3. **Use mipmapping** — `rwFILTERMIPLINEAR` much faster than `rwFILTERLINEAR`
4. **Use compressed textures** — DXT1-DXT5 faster to load and render
5. **Batch >100 triangles** — rendering less is CPU waste
6. **Use indexed triangle strips** — vertex cache reuse
7. **Z buffer optimization** — render front-to-back for occlusion
8. **Alpha test selectively** — disable when not needed
9. **Avoid resource switching** — one large texture per atomic preferred
10. **Group and sort** — opaque front-to-back, transparent back-to-front

## skygfx Plus Shader System

### Shader Compilation

All shaders are compiled as HLSL to CSO (Compiled Shader Object) files:

```
shaders/
├── vs/           # Vertex shaders (.hlsl → .cso)
├── ps/           # Pixel shaders (.hlsl → .cso)
│   ├── 2_a/      # Stochastic variants
│   └── include/  # Shared includes
└── resources/
    └── cso/      # Compiled CSO binaries
```

**Compilation targets**: vs_3_0, ps_3_0 (baseline), ps_2_0 (fallback)

### Register Layout

#### Vertex Shader Constants (c0-c29+)

| Register | Content | Used By |
|----------|---------|---------|
| c0-c3 | World-View-Projection (WVP) matrix | All VS |
| c4-c7 | World matrix (rows 0-3) | All VS |
| c8-c11 | View matrix (rows 0-3) | GTAIV VS |
| c12-c15 | Projection matrix | GTAIV VS |
| c16 | UV Scale/Offset (xy=scale, zw=offset) | Vehicle, Building VS |
| c17 | Time-of-day interpolation (x) | Building DN VS |
| c18 | Fog params (x=density, y=start, z=end) | All VS |
| c19 | Shininess/Spec power (x), Fresnel (y) | Vehicle VS |
| c20-c23 | Light 0 Direction (xyz, w=type) | Vehicle VS |
| c24-c27 | Light 1 Direction | Vehicle VS |
| c28-c29 | Env map view vector | Env car VS |

#### Pixel Shader Constants (c0-c31)

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
| c20-c23 | SSAO params | SSAO PS |
| c24 | SMAA params | SMAA PS |
| c25-c31 | GTA IV postFX params | GTAIV PS |

### Key Shader Implementations

#### Simple Pipeline (simplePS.hlsl)
```hlsl
float4 main(float2 tex : TEXCOORD0, float4 color : COLOR0) : COLOR {
    return tex2D(diffuseSampler, tex) * color;
}
```

#### PS2 Building Pipeline
- `MODULATE2X` equivalent: `(tex * color) * 2`
- RGB channel swap for PS2 swizzle correction
- Dither emulation via screen-space position hash
- Per-vertex fog applied after modulation

#### GTA IV Forward+ (GTAIVForwardPlus_ps.hlsl)
```hlsl
// Light loop: up to 8 dynamic lights per tile
for (int i = 0; i < lightCount; i++) {
    Light light = lights[i];
    float3 L = normalize(light.position - worldPos);
    float3 H = normalize(V + L);
    
    float NDF = GGX(N, H, roughness);      // GGX normal distribution
    float G = Smith(N, V, L, roughness);    // Smith correlated visibility
    float3 F = Schlick(F0, H, V);           // Schlick Fresnel
    
    float3 spec = (NDF * G * F) / (4 * dot(N,V) * dot(N,L) + 0.0001);
    float3 diff = BurleyDiffuse(N, L, V, roughness);
    
    result += (diff * albedo + spec) * light.color * attenuation;
}
```

### Shader Loading

```cpp
// From pipelinecommon.cpp
void CreateShaders() {
    // Load CSO files from resources
    // Create D3D9 vertex/pixel shaders
    // Store in shader arrays
}
```

## HLSL Effect Framework (from D3DX Effects docs)

### Effect File Structure
```hlsl
technique TechniqueName {
    pass PassName {
        VertexShader = compile vs_3_0 VertexFunc();
        PixelShader = compile ps_3_0 PixelFunc();
        
        // Render states
        ZEnable = TRUE;
        ZWriteEnable = TRUE;
        AlphaTestEnable = TRUE;
        CullMode = CW;
    }
}
```

### Key HLSL Intrinsics

| Intrinsic | Description | Shader Model |
|-----------|-------------|--------------|
| `tex2D(s, t)` | 2D texture sample | ps_1_1+ |
| `texCUBE(s, t)` | Cube texture sample | ps_1_1+ |
| `tex2Dbias(s, t)` | Sample with bias | ps_2_0+ |
| `tex2Dgrad(s, t, dx, dy)` | Sample with gradient | ps_2_x+ |
| `lrp(a, b, c)` | Linear interp: a*b + (1-a)*c | vs_2_0+ |
| `crs(a, b)` | Cross product | vs_2_0+ |
| `nrm(a)` | Normalize | vs_2_0+ |
| `pow(a, b)` | Power | vs_2_0+, ps_2_0+ |
| `sincos(s, out s, out c)` | Sine/cosine | vs_2_0+ |
| `abs(a)` | Absolute value | vs_2_0+ |
| `clamp(a, lo, hi)` | Clamp to range | ps_1_1+ |
| `saturate(a)` | Clamp to [0,1] | ps_1_1+ |
| `dot(a, b)` | Dot product | ps_1_1+ |
| `mul(a, b)` | Multiply | vs_1_1+ |

### Precision Notes (ps_2_x+)

- **High precision** (≥fp24): texture coords, fp32 fetches, `half`/`_pp` qualified
- **Low precision** (≥fp16): constants, iterated values
- World-space per-pixel computations cause fp16 precision pitfalls
- **Recommendation**: Transform to light space in VS, compute in PS

## See Also

- [[RenderWare SDK Architecture]]
- [[Shaders/Shader Reference]]
- [[Pipelines/Building Pipelines]]
- [[Pipelines/Vehicle Pipelines]]
