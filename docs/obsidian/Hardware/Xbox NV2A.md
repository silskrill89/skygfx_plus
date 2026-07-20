# Xbox NV2A GPU

The NV2A is an evolution of NVIDIA's NV20 (GeForce 3) architecture with Xbox-specific extensions. It bridges the fixed-function console era and the programmable PC pipeline.

## NV2A vs NV20 (GeForce 3)

| Feature | NV20 (GeForce 3) | NV2A (Xbox) |
|---------|-----------------|-------------|
| Vertex shaders | 1 unit, 128 instructions | 2 units, 136 instructions each |
| Pixel shaders | 4 texture shaders + 8 combiners + final | Same (NV20 plus NV2A extensions) |
| Memory | External AGP (up to 128 MB) | 64 MB unified DDR (GPU direct access) |
| Memory bandwidth | ~8 GB/s (AGP 4×) | ~6.4 GB/s (128-bit DDR @ 200 MHz) |
| EDRAM | No | 10 MB embedded DRAM (color + Z) |
| MSAA | No hardware MSAA | 4× MSAA via EDRAM (at ≤640×480) |
| Pixel shader extensions | None | `ps.1.4`-like features: 12 texture instructions, dependent reads |
| Unique render targets | No | EDRAM surface clip via render target region |
| Vertex texture fetch | No | No (same as NV20) |
| VTF (Vertex Texture Fetch) | No | No |

## EDRAM Architecture

The NV2A has **10 MB of embedded DRAM** on-die, separate from the 64 MB unified main memory. EDRAM stores:

- **Color buffer**: up to 2048 × 2048 × 32-bit (frequently used at 640×480)
- **Z buffer**: up to 2048 × 2048 × 32-bit (shared EDRAM with color)
- **Stencil**: 8 bits per pixel (optional)

### EDRAM Partitioning

EDRAM is organized as 16 tiles of 512 × 512 pixels. Each tile handles a 32×32 pixel region.

```
Total: 2048 × 2048 × 32-bit = 16 MB
Available: 10 MB (6 MB reserved for Z/stencil)
```

The GPU clips rendering to EDRAM boundaries. To render at >640×480 with 4× MSAA, the scene must be tiled — each tile rendered separately and resolved to main memory.

### MSAA Support

| Samples | Resolution Limit | Memory Used (640×480) |
|---------|-----------------|----------------------|
| 1× (no AA) | 2048×2048 | 1.2 MB color + 1.2 MB Z |
| 2× | 1280×1024 | 2.4 MB color + 2.4 MB Z |
| 4× | 640×480 | 4.8 MB color + 4.8 MB Z |

At 4× MSAA, each pixel occupies 4× the EDRAM space. 640×480 × 4 samples × 4 bytes = 4.8 MB for color alone. This is why Xbox GTA SA runs at 640×480 with 4× MSAA.

## Push Buffer (Command Stream)

The NV2A receives rendering commands via a **push buffer** — a ring buffer in main memory filled by the CPU and consumed by the GPU. This is the Xbox equivalent of VIF1/GIF on PS2.

```
CPU writes to push buffer → GPU reads → processes register writes
```

### Push Buffer Commands

| Method | Offset | Purpose |
|--------|--------|---------|
| `NV097_SET_TEXTURE_MODULE` | 0x1D40 | Set texture state (format, address, filter) |
| `NV097_SET_TRANSFORM_CONSTANT` | 0x1E40 | Set vertex shader constant |
| `NV097_SET_VERTEX_SHADER_PROGRAM` | 0x2000 | Upload vertex shader microcode |
| `NV097_SET_PIXEL_SHADER_PROGRAM` | 0x2100 | Upload pixel shader program |
| `NV097_DRAW_ARRAYS` | 0x3000 | Submit indexed/non-indexed primitives |
| `NV097_SET_VIEWPORT_OFFSET` | 0x3008 | Set viewport transform |
| `NV097_SET_COMBINER_ALPHA` | 0x2300 | Register combiner alpha control |
| `NV097_SET_COMBINER_COLOR` | 0x2308 | Register combiner color control |
| `NV097_SET_FOG_ENABLE` | 0x2400 | Fog enable |
| `NV097_SET_ALPHA_TEST_ENABLE` | 0x2414 | Alpha test enable |
| `NV097_SET_ALPHA_REF` | 0x2418 | Alpha reference value |
| `NV097_SET_BLEND_FUNC` | 0x2420 | Blend function (S/D factors) |
| `NV097_SET_SCISSOR_HORIZONTAL` | 0x2440 | Scissor rect X |
| `NV097_SET_SCISSOR_VERTICAL` | 0x2448 | Scissor rect Y |

### DMA Pushing

The CPU writes methods to the push buffer, then writes the `NV_PFIFO` method to signal the GPU. The GPU processes FIFO-style.

```
for each draw call:
    write methods (texture, combiner, shader constants, etc.)
    write NV097_DRAW_ARRAYS with vertex range
    GPU executes asynchronously
```

## Vertex Shader Pair Programming

The two vertex shader units run in **lockstep** — both execute the same program on different vertices simultaneously. This is not dual-issue (two different programs).

### Program Structure

Each shader unit has:
```
c[0-11]:  12 constant registers (96 bytes total)
v[0-15]:  16 vertex attribute registers
t[0-15]:  13 temporary registers
o[0-15]:  16 output registers (oPos, oD0-1, oFog, oT0-7)
a0:       1 address register (for indexed constant access)
```

Instruction set: 32-bit fixed-length. Includes:
- `MOV`, `MUL`, `ADD`, `MAD`, `DP3`, `DP4`, `DST`, `MIN`, `MAX`, `SLT`, `SGE`, `RCP`, `RSQ`, `EXP`, `LOG`, `LIT`, `LRP`

### Branching

- Static branches only (no dynamic flow control)
- Condition codes set by `SLT`/`SGE` comparisons
- Predicated execution via `REP`/`ENDREP` (limited loop count)

### Vertex Constants Upload

Constants are uploaded via push buffer method `NV097_SET_TRANSFORM_CONSTANT`:
```
method[offset] = constant_0_xyzw  // packed as 4 floats
method[offset+1] = constant_1_xyzw
...
```

Each constant is 4 floats (16 bytes). All 12 constants = 192 bytes per draw call worst case.

## Texture Shaders

The NV2A texture shader units sit between vertex shader output and register combiners. They can perform texture lookups and simple math operations.

### Texture Shader Mode Details

`DOT_RFLCT_DIFF` (0x0B): Computes diffuse reflection using cube map
```
texcoord.xyz = normalized vertex normal (from texture coordinates)
tex = cubeMapLookup(texcoord)
// output goes to RGB combiner as dot product
```

`BUMPENVMAP` (0x06): Perturbs texture coordinates for environment mapping
```
rotMatrix = { tex0.a, tex0.b, tex1.a, tex1.b }
offset.xy = (tex2.xy × rotMatrix) + tex1.zw  // per-pixel perturbation
texcoord.xy = baseUV + offset.xy
```

`DOT_STR_3D` (0x0D): 3D normal map dot product
```
result = dot(normal.xyz, lightVec.xyz)
// stored in spare0
```

### Texture Shader Output to Combiners

Each texture shader writes to RGB and alpha outputs that feed into the register combiners:

```
Texture unit 0 → combiner RGB/alpha input stage
Texture unit 1 → combiner RGB/alpha input stage
Texture unit 2 → combiner RGB/alpha input stage
Texture unit 3 → combiner RGB/alpha input stage
```

The combiner can select any texture unit output as its A, B, C, or D input.

## Register Combiner Details

### Per-Stage Math

Each combiner stage computes two parallel results (RGB and alpha):

```
RGB:
  AB  = A_rgb × B_rgb  (optionally + bias)
  CD  = C_rgb × D_rgb  (optionally + bias)
  SUM = (AB + CD) >> 1  (optional divide by 2)
  OUT = SUM × (1 + bump_scale) OR lerp(CD, AB, spare0.alpha) OR (AB × CD_r + CD × AB_r)

Alpha:
  AB_a = A_a × B_a
  CD_a = C_a × D_a
  SUM_a = AB_a + CD_a
  OUT_a = SUM_a
```

### Input Mapping to D3D9 Equivalents

| NV2A Input | NV2A Source | D3D9 Equivalent |
|-----------|-------------|-----------------|
| `A=tex0` | Texture 0 color | `t0.rgb` |
| `B=tex1` | Texture 1 color | `t1.rgb` |
| `C=prev` | Previous combiner output | `r0.rgb` or `r1.rgb` |
| `D=const0` | Per-stage constant | `c[X].rgb` |
| `spare0` | Inter-stage register | `r0` |
| `spare1` | Inter-stage register | `r1` |
| `zero` | (0,0,0) | `0` |
| `discard` | Discard previous/write | N/A |

### Final Combiner Fog

Fog in the final combiner:
```
final.rgb = spare0.rgb × (1 - fog_factor) + fog_color × fog_factor
final.a = spare0.a
```

The fog factor comes from the vertex shader's `oFog` output (per-vertex, interpolated).

### Specular Addition

The final combiner can add specular:
```
final.rgb = spare0.rgb + specular.rgb
```

Specular is computed per-vertex (vertex shader `oD1`), interpolated, and added after all combiner stages.

## Xbox-Specific Extensions

Beyond NV20, the NV2A adds:

1. **12 texture addressing instructions** (vs 4 on NV20) in the pixel shader
2. **Dependent texture reads** — texture shader output can feed back as coordinate input (early `ps.1.4` behavior)
3. **Texture format extensions** — Xbox-specific DXT variants, YUV surfaces
4. **Render target region** — allows sub-rectangle render target within EDRAM
5. **Vertex shader extensions** — 136 instructions vs 128, additional condition codes

## Xbox GTA SA Pipeline Mapping

### Vehicle Pipe

```
Vertex shader:
  - Transform to world/view space
  - Compute per-vertex lighting (ambient + diffuse)
  - Output v0-7 for texture coords, oD0 for color

Texture shaders:
  - t0: Diffuse map (base color)
  - t1: Environment map (cube map for reflections)
  - t2: Specular mask (grayscale, modulates environment)

Register combiners (3 stages):
  Stage 0: A=tex0, B=diffuse_color → modulate base
  Stage 1: A=tex1(env), B=tex2(spec_mask) → environment × specular mask
  Stage 2: lerp(stage0, stage1, spare0.alpha) → blend
  Final: add fog
```

### Building Pipe

```
Vertex shader:
  - Standard transform + fog

Texture shaders:
  - t0: Diffuse map

Register combiners (2 stages):
  Stage 0: A=tex0, B=vertex_color → modulate
  Stage 1: A=prev, B=fog_color, C=fog_factor → fog blend
  Final: output
```

### Grass/Nature Pipe

Uses dual-pass rendering similar to PS2 alpha, but implemented via register combiners:
- Pass 1: alpha test masks transparent pixels, combiners output opaque grass
- Pass 2: alpha blend enabled for edges, combiners blend semi-transparent

## Memory Map

```
0x00000000-0x03FFFFFF  — System RAM (64 MB)
0xF0000000-0xF07FFFFF  — GPU registers
0xF0800000-0xF0FFFFFF  — Push buffer
0xFF000000-0xFFFFFFFF  — BIOS (256 KB)
```

GPU can DMA from any system memory address. Textures, vertex buffers, and index buffers all live in the same 64 MB pool.

### Texture Upload

Textures uploaded via push buffer method `NV097_SET_TEXTURE_MODULE`:
```
method: texture_handle  (0-based texture unit index)
data:   base_address, format, width, height, mip_levels, filter_mode
```

Format parameter encodes: DXT1/3/5, A8R8G8B8, X8R8G8B8, R5G6B5, A4R4G4B4, P8, L8, A8, YUV

### Vertex Buffer Upload

Vertices uploaded as raw arrays via `NV097_DRAW_ARRAYS`:
```
NV097_DRAW_ARRAYS:
  primitive_type (0=triangles, 1=triangle_strip, 2=triangle_fan, 3=quads)
  vertex_count
  vertex_array_base (in GPU-accessible memory)
  stride
  vertex_format_flags (which attributes present)
```

## vs D3D9 Equivalence

| NV2A Feature | D3D9 ps_2_0 Equivalent | Notes |
|-------------|----------------------|-------|
| 8 combiner stages | Multiple `tex` + `mul/add` instructions | D3D9 more flexible |
| Texture shader 12 modes | `texld` + addressing math | Dependent reads similar |
| Final combiner fog | `lerp` with fog factor | Direct |
| Per-stage constants | `def cX, ...` or `SetPixelShaderConstant` | Direct |
| `BUMPENVMAP` | `texbem` / manual perturbation | D3D9 has `texm3x2pad`/`texm3x2tex` |
| `BRDF` mode | Manual Fresnel + env map | NV2A is one-cycle |
| 4 texture units | 8 texture stages (`ps_2_0`) | NV2A more limited |
| No pixel flow control | Static vs dynamic branching | NV2A is fully static per-pixel |
| 10 MB EDRAM | Separate VRAM (system dependent) | EDRAM enables free 4× MSAA |

## See Also

- [[Hardware Differences]] — Quick comparison with PS2 and PC
- [[Hardware/Overview]] — Platform comparison
- [[PS2 Xbox Feature Emulation Guide]] — Emulating Xbox features in D3D9
- [[Shader Architecture]] — SkyGFX shader pipeline
- [[ReVC RenderWare Shader System]] — D3D9 register layout
