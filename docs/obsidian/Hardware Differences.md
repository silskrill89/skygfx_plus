# Hardware Differences

Understanding why GTA III-SA look different across platforms requires understanding the hardware differences between PS2, Xbox, and PC.

## Render Pipeline Comparison

| Stage | Early 90s PC | Late 90s PC | Early 00s PC | Xbox | PS2 |
|-------|-------------|-------------|--------------|------|-----|
| T&L | CPU | CPU | GPU fixed | GPU shader | VU |
| Clipping | CPU | CPU | GPU fixed | GPU fixed | VU |
| Projection | CPU | CPU | GPU fixed | GPU fixed | VU |
| Rasterization | CPU | GPU fixed | GPU fixed | GPU fixed | GS fixed |
| Texturing/Fog | CPU | GPU fixed | GPU fixed | GPU shader | GS fixed |
| Z/Alpha/Blend | CPU | GPU fixed | GPU fixed | GPU fixed | GS fixed |

## PS2 Texture Modulation

The PS2 Graphics Synthesizer calculates modulation as:

```
result = (A × B) / 128
```

Compared to D3D:

| D3D Mode | Formula | Brightens? |
|----------|---------|-----------|
| `D3DTOP_MODULATE` | (A × B) / 255 | No |
| `D3DTOP_MODULATE2X` | (A × B) / 128 × 2 | **Yes** |

The PS2 GS is equivalent to `D3DTOP_MODULATE2X`. This means textures modulated with 128 become 255 (full bright). SkyGFX uses `MODULATE2X` where R\* wrote custom PS2 rendering code.

> [!note]
> RenderWare abstracts this difference in most cases. Only custom R\* code (building and grass pipelines in SA) is affected.

## PS2 Alpha Blending

The PS2 blend equation is fundamentally different from D3D:

**PS2**: `dst = (A ⊗ B) × C + D` where `A ⊗ B = (A × B) / 128`
- A, B, D ∈ {src, dst, 0}
- C ∈ {srcAlpha, dstAlpha, constant}

**D3D**: `dst = op(dst × D, src × S)` where op ∈ {add, sub, max, min, ...}

### Standard Alpha Blending

Both can achieve standard linear interpolation:
- PS2: `A=src, B=dst, C=srcAlpha, D=dst` → `(src - dst) × srcAlpha + dst`
- D3D: `op=add, S=srcAlpha, D=1-srcAlpha` → `dst × (1-srcAlpha) + src × srcAlpha`

### Radiosity Blend (PS2 Only)

PS2: `A=dst, B=src, C=128, D=dst` → `(dst - src) × 128 + dst = dst × 2 - src`
**D3D cannot replicate this** — no factor can ever be 2.

This is why radiosity requires the Shader fallback on PC.

## PS2 Alpha Test (Dual Pass)

| Behaviour | PS2 | PC |
|-----------|-----|-----|
| Pixel fails alpha test | Colour written, depth discarded | Pixel completely discarded |
| Control | Per-pixel | Per-drawcall only |

The PC limitation (still exists today) forced R\* to hack workarounds. SkyGFX emulates PS2 with dual pass rendering:

1. Draw pixels **above** reference with Z-write **on**
2. Draw pixels **below** reference with Z-write **off**

Most visible on vegetation edges and vehicle windscreens.

---

# PS2 Graphics Synthesizer (GS) Architecture

The PS2's GPU is the Graphics Synthesizer — a fixed-function rasterizer with no programmable shaders. All effects are achieved through register configuration.

**For the full GS architecture (pipeline stages, memory layout, TEX0-3 registers, alpha blend equations, alpha test), see [[Hardware/PS2 GS]].**

### Key GS Characteristics for SkyGFX

| Feature | Value | SkyGFX Impact |
|---------|-------|---------------|
| Clock | 147 MHz (150 MHz NTSC) | — |
| Framebuffer | 4 MB eDRAM (1024×512 max) | Z-precision limits |
| Texture Modulation | `(A × B) / 128` | Equivalent to D3D `MODULATE2X` |
| Alpha Blend | `(A - B) × C + D` (9-bit signed) | Radiosity impossible on D3D9 |
| Alpha Test | Per-pixel (colour-only discard) | PC is per-drawcall (full discard) |

## Why This Matters for SkyGFX

The PS2's fixed-function pipeline creates specific visual signatures that D3D9's programmable pipeline can't replicate without careful emulation:
1. **Single-cycle modulation** = brighter textures than D3D9
2. **Fixed alpha blending** = different blending math (radiosity impossible on D3D9)
3. **Per-pixel alpha test** = different transparency handling
4. **No shadow maps** = PS2 uses projected shadows only
5. **Limited Z-precision** = no per-pixel depth comparison (no shadow buffer)

---

# Xbox NV2A Architecture

The Xbox uses NVIDIA's NV2A GPU — essentially a modified GeForce3 (NV20) with unique extensions. Unlike the PS2's fixed-function GS, the NV2A has programmable vertex and texture pipelines.

**For the full NV2A architecture (vertex processing, texture shaders, register combiners, shadow buffer), see [[Hardware/Xbox NV2A]].**

### Key NV2A Characteristics for SkyGFX

| Feature | Value | SkyGFX Impact |
|---------|-------|---------------|
| Vertex Units | 2 × 136 instructions | vs D3D vs_1_0–3_0 |
| Combiners | 8 stages + final | ≈ ps_2_0 but different math |
| Texture Shaders | 4 units (BRDF, dependent reads) | More powerful than D3D stages |
| Shadow Buffer | Depth sampled in texture | Xbox-specific, no D3D equivalent |
| EDRAM | 10 MB (color+Z) | 4× MSAA at ≤640×480 |

## Why This Matters for SkyGFX

The NV2A's capabilities differ from D3D9 PC in important ways:
1. **Register combiners** ≈ D3D9 ps\_2\_0 but with different math
2. **Texture shaders** ≈ D3D9 texture stages but more powerful (BRDF, dependent reads)
3. **Shadow buffer** ≈ D3D9 depth textures but NV2A-specific
4. **Per-stage constants** ≈ D3D9 constant registers but with different packing
5. **4 texture units** = same as D3D9, but different configuration model

The Xbox pipeline in SkyGFX emulates these differences using D3D9 pixel shaders (ps\_3\_0).

---

# PC D3D9 vs Console: Summary

| Feature | PS2 | Xbox (NV2A) | PC (D3D9) |
|---------|-----|------------|-----------|
| Vertex Shader | VU0/VU1 | 2 × 136 inst | vs\_1\_0-3\_0 |
| Pixel Shader | None (GS registers) | 8 combiners + final | ps\_1\_0-3\_0 |
| Texture Units | 1 per draw | 4 | 8 stages |
| Blend Equation | `(A-B)×C+D` | Register combiners | Fixed-function or shader |
| Alpha Test | Per-pixel (GS) | Per-pixel | Per-drawcall only |
| Z-Buffer | 32/24+8/16 bit | 32/24+8 bit | Any D3D9 format |
| Shadow Maps | No | Yes (shadow buffer) | Requires shader |
| Normal Maps | No | Yes (dot products) | Requires shader |
| Max Textures | 1 | 4 | 8 |

## See Also

- [[Pipelines/Overview]]
- [[Shader Architecture]]
- [[Hardware/Overview]] — Platform hardware deep-dives
- [[Hardware/PS2 GS]] — Full GS pixel pipeline
- [[Hardware/Xbox NV2A]] — NV2A register combiner details
- [[Hardware/Early PC GPUs]] — DX7-DX9 PC context
