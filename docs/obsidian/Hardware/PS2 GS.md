# PlayStation 2 Graphics Synthesizer

The GS is a fixed-function rasterizer with no programmable pixel shaders. All rendering behavior is controlled by register writes submitted through the GIF (Graphics Interface) from VU1 or the EE.

## Architecture

### GS Pipeline Stages

```
VU1/GIF → Pixel Pipeline 1 (64-bit) ─→ Pixel Pipeline 2 (64-bit) ─→ CRTC → Output
                │                              │
          Texture Unit                    Texture Unit
          Alpha/Fog Unit                  Alpha/Fog Unit
          Dither Unit                     Dither Unit
          Z/Stencil Test                  Z/Stencil Test
          Alpha Blend                     Alpha Blend
                │                              │
                └──────────┬──────────────────┘
                     128-bit crossbar
                           │
                   4 MB eDRAM (DRAM)
                           │
                     VRAM Controller
```

The GS has **two independent pixel pipelines**, each 64 bits wide, operating in parallel. Each pipe processes one pixel per cycle at 150 MHz = **300 Mpixel/s fill rate**.

### Local Memory

- **4 MB eDRAM** embedded on the GS die (no external memory bus)
- Bandwidth: 48 GB/s (2560-bit internal bus × 150 MHz)
- Organized as 2048 × 2048 pages of 32 bytes each
- Memory is *not* byte-addressable — minimum access is a 32-byte page (8 pixels at 32-bit, 16 pixels at 16-bit)

### Memory Layout (Page-Based Tiling)

GS memory is organized as a 2D page grid:

```
Page size (CT32): 8×4 pixels = 32 bytes
Page size (CT16): 8×8 pixels = 32 bytes

Image stored in page-swizzled order:
  (0,0)   (8,0)   (16,0)  ...   (2040,0)
  (0,4)   (8,4)   (16,4)  ...   (2040,4)
  (0,8)   (8,8)   (16,8)  ...   (2040,8)
  ...
```

The swizzle converts linear (x, y) to page-ordered addressing:
```
page_x = (x / 8) * 8
page_y = (y / 4) * 4  // CT32
offset_in_page = (x % 8) + (y % 4) * 8
```

This maximizes DRAM page hits during rasterization. Texture swizzling follows the same pattern for texture cache efficiency.

## GS Registers (Detailed Field Layout)

### Context System

The GS has **two contexts** — double-buffered register sets that can be swapped instantly via the `GS_CSR` register or GIF tag `NOP` field. Context 0 is active by default.

Context-switchable registers: `TEX0`, `TEX1`, `TEX2`, `CLAMP`, `MIPTBP`, `SCISSOR`, `ALPHA`, `TEST`, `FRAME`, `ZBUF`, `XYOFFSET`, `PRIM`, `FOG`

Non-context registers (shared): `TEXFLUSH`, `FOGCOL`, `DIMX`, `DTHE`, `COLCLAMP`, `PABE`, `BITBLTBUF`, `TRXPOS`, `TRXREG`, `TRXDIR`, `PMODE`, `SMODE`, `DISP`, `CSR`

### PRIM Register (0x00)

```
Bit 0-2:   Primitive type (0=point, 1=line, 2=line strip, 3=tri, 4=tri strip, 5=tri fan, 6=sprite, 7=line strip)
Bit 3:     IIP (shading: 0=flat, 1=gouraud)
Bit 4:     TME (texture mapping enable)
Bit 5:     FGE (fog enable)
Bit 6:     ABE (alpha blending enable)
Bit 7:     AA1 (anti-aliasing)
Bit 8:     FST (texture coordinate format: 0=STQ, 1=UV)
Bit 9:     CTXT (context: 0/1)
Bit 10:    FIX (fix fragment: 0=per-frag, 1=per-prim)
```

### TEX0 Register (0x06 / 0x3F) — Texture State

```
Bit 0-13:   TBP0 (Texture Base Pointer — 14-bit, aligned to 256-byte blocks)
Bit 14-15:  TBW (Texture Buffer Width — log2(width/64))
Bit 20-21:  PSM (Pixel Storage Mode — 0=CT32, 1=CT24, 2=CT16, 10=RGB16, 19=IDTEX4, 20=IDTEX8)
Bit 23:     TW (Texture Width — log2 of pixel width, max 10=1024)
Bit 25:     TH (Texture Height — log2 of pixel height, max 10=1024)
Bit 26-27:  TCC (Texture Color Component — 0=RGB+A from texture, 1=RGB only, A=CS)
Bit 28-29:  TFX (Texture Function — 0=modulate, 1=decal, 2=highlight, 3=highlight2)
Bit 30-31:  CBP (CLUT Buffer Base Pointer)
Bit 32-33:  CPSM (CLUT Pixel Storage Mode)
Bit 34-37:  CSM (CLUT Storage Mode — 0=CSM1, 1=CSM2)
Bit 38-39:  CSA (CLUT Entry Offset — shifts CLUT by 8 entries per unit)
Bit 41:     CLD (CLUT Load Control — 0=no load, 1=load, 2=load per context swap)
Bit 42:     FCMT (Fragment Color Multiply — 0=off, 1=on)
```

### TEX1 Register (0x14 / 0x35) — Texture Filtering and LOD

```
Bit 0-1:   LCM (LOD Calculation Method — 0=standard K×LOD, 1=orthogonal)
Bit 2-4:   MXL (Maximum LOD Level — 0=LOD 0 only, 1-7=LOD 0-max)
Bit 5-6:   MMAG (Magnification Filter — 0=nearest, 1=linear, 2=nearest, 3=linear)
Bit 7-8:   MMIN (Minification Filter — 0=nearest, 1=linear, 2=nearest mipmap, 3=linear mipmap)
Bit 9-10:  MTBA (Mipmap Base Address — enables separate mip base address via MIPTBP)
Bit 11-12: L (LOD value for fixed-LOD mode)
Bit 13-16: K (LOD bias, signed 4-bit integer)
```

### ALPHA Register (0x42 / 0x43) — Alpha Blending

```
Bit 0-1:   A (source factor — 0=CS+CV, 1=CS-CV, 2=0, 3=CS+CV with FIX mask)
Bit 2-3:   B (destination factor — same encoding as A)
Bit 4-7:   C (blend factor — 0=AS+AV, 1=AS-AV, 2=FIX, 3=CS+CV, 4=AS+AV w/AV)
Bit 8-15:  FIX (fixed blend factor, 8-bit unsigned)

Formula:  result = ((A - B) × C) >> 7 + D
```

Where D is computed from A, B, C via internal logic. The full breakdown:
- If A=0: Cs, A=1: Cd, A=2: 0
- If B=0: Cs, B=1: Cd, B=2: 0
- If C=0: As, C=1: Ad, C=2: FIX, C=3: Cs, C=4: As w/ alpha mask
- D is derived from the A/B selection with the subtraction inverted

### TEST Register (0x47 / 0x3E) — Alpha, Depth, Scissor

```
Bit 0:     ATE (Alpha Test Enable)
Bit 1-2:   ATST (Alpha Test — 0=NEVER, 1=ALWAYS, 2=LESS, 3=LEQUAL, 4=EQUAL, 5=GEQUAL, 6=GREATER, 7=NOTEQUAL)
Bit 3-7:   AREF (Alpha Reference — 8-bit)
Bit 8-9:   AFAIL (Alpha Fail — 0=KEEP, 1=FB_ONLY (write color only), 2=ZB_ONLY (write Z only), 3=RGB_ONLY)
Bit 10:    DATE (Destination Alpha Test Enable)
Bit 11:    DATM (Destination Alpha Test Mode — 0=alpha == 0 pass, 1=alpha != 0 pass)
Bit 12:    ZTE (Depth Test Enable)
Bit 13:    ZTST (Depth Test — 0=ALWAYS, 1=GEQUAL, 2=GREATER, 3=NEVER)
```

### FRAME Register (0x4E / 0x4F) — Framebuffer

```
Bit 0-8:   FBASE (Framebuffer Base Address — in 2048-byte blocks)
Bit 9-15:  FBW (Framebuffer Width — in units of 64 pixels)
Bit 16-19: FPSM (Framebuffer PSM — 0=CT32, 1=CT24, 2=CT16, 10=RGB16)
Bit 24-31: FBMSK (Framebuffer Write Mask — per-byte, 0=write, 1=mask)
```

### ZBUF Register (0x4E / 0x4F) — Depth Buffer

```
Bit 0-8:   ZBP (Z-Base Pointer — in 2048-byte blocks)
Bit 9-10:  ZPSM (Z PSM — 0=Z32, 1=Z24, 2=Z16, 3=Z16S)
Bit 16:    ZMSK (Z Write Mask — 0=write, 1=mask)
```

### FOGCOL Register (0x3D) — Fog Color

```
Bit 0-7:   FCR (Fog Color Red, 8-bit)
Bit 8-15:  FCG (Fog Color Green)
Bit 16-23: FCB (Fog Color Blue)
```

Fog density is per-vertex (interpolated across triangle). GS mixes fog as:
```
result = (src × (256 - fog)) >> 8 + fogcol × fog >> 8
```

### DIMX Register (0x44) — Dither Matrix

```
Bit 0-3:   Row 0 (4-bit signed: -4 to +3 as 4-bit two's complement)
Bit 4-7:   Row 1
Bit 8-11:  Row 2
Bit 12-15: Row 3
Bit 16-19: Column 0 (applied to x=0,4,8...)
Bit 20-23: Column 1 (x=1,5,9...)
Bit 24-27: Column 2 (x=2,6,10...)
Bit 28-31: Column 3 (x=3,7,11...)
```

Standard GTA SA dither matrix:
```
Row:  -4  0 -3  1
       2 -2  3 -1
      -3  1 -4  0
       3 -1  2 -2
```

### DTHE Register (0x45) — Dither Enable

```
Bit 0: DT_ENABLE — 0=off, 1=on
```

## Pixel Pipeline Detail

### Single-Cycle Texture Read

When `TEX0.TME=1`, the GS reads texture data from local memory in one cycle:

1. **Texel address calculation**: swizzled page lookup from UV/ST coordinates
2. **Cache lookup**: 4 KB texture cache per pipe (8 KB total). Cache line = 16 texels (CT32) or 32 texels (CT16).
3. **CLUT lookup** (for IDTEX formats): indexed texture → CLUT base + entry
4. **Filtering**: nearest or bilinear (bilinear costs 2 cycles, reads 4 texels)
5. **TFX application**: combine texel with vertex color per TEX0.TFX mode

### Color Pipeline

```
Vertex Color (RGBAQ) ──┐
Texel Color (TFX)    ──┤→ TFX Unit → Fog Unit → Alpha Test → Z Test → Alpha Blend → Dither → Frame Buffer
Fog Color (FOGCOL)   ──┘
```

### Sub-Pixel Accuracy

GS uses **12.4 fixed-point** for sub-pixel positioning (16 sub-positions per pixel). The draw offset (`XYOFFSET`) is:

```
X = (XYZ2.X / 16) - (XYOFFSET.X received as half-pixel offset)
Y = (XYZ2.Y / 16) - (XYOFFSET.Y)
```

`XYOFFSET` is typically set to -0.5 to align pixel centers.

## Context Swapping

The GS can switch between two register contexts on a per-primitive or per-packet basis:

- **Context 0**: typically used for main scene rendering
- **Context 1**: typically used for post-effects, HUD, or interlace field 2

The `PRIM.CTXT` bit selects the active context. Context swap via `CSR`:

```
CSR.FIELD = 0/1  (current field for interlace)
CSR.SIGNAL = write 1 to acknowledge interrupt
```

GTA SA uses context 1 for the second interlace field when `ps2Interlace` is active.

## 2x MSAA via Page Swizzling

PS2 achieves 2x MSAA without dedicated multisample hardware. Two samples per pixel are stored by rendering to a framebuffer twice the width:

1. Framebuffer is set to 2× horizontal resolution (e.g., 1280×448 instead of 640×448)
2. GS PRIM register forces alternate x-offsets per primitive
3. The display circuit downsamples by skipping every other pixel
4. Result: each on-screen pixel averages two render samples

This is handled entirely through FBW (framebuffer width) and XYOFFSET manipulation — no GS register specifically controls MSAA.

## GS Performance Characteristics

### Fill Rate Limits

| Scenario | Pixels/Cycle | Effective Fill Rate (150 MHz) |
|----------|-------------|------------------------------|
| No texture, no blending | 2 | 300 Mpixel/s |
| Texture + blending | 2 | 300 Mpixel/s |
| Bilinear filter | 1 | 150 Mpixel/s |
| Fog enabled | 2 | 300 Mpixel/s |

### Texture Cache Miss Penalty

- Cache hit: 1 cycle
- Cache miss: ~8-12 cycles (DRAM page open + read)
- Worst case: scattered UV access pattern (cache trashing)

This is why R\* renders full-screen effects as 32-pixel-wide vertical strips — to maximize cache locality.

### Local Memory Bandwidth

- 32 bytes per pixel pipe per cycle = 64 bytes/cycle total
- At 150 MHz: 9.6 GB/s theoretical, ~4-5 GB/s achievable with cache misses

## How GTA SA Uses the GS

### Scene Render Loop

1. **Context 0 setup**: FRAME → main framebuffer, ZBUF → depth buffer
2. **VU1 processes** world/vehicle/grass geometry → GIF → GS
3. **Post-effects** (if any): switch to context 1, render 32-pixel strips
4. **Interlace swap** (if enabled): toggle DISPLAY.DBY for field offset

### Known PS2 GS Quirks in GTA SA

- **COLCLAMP always 1**: unsigned clamping enabled globally
- **DTHE always 1**: dither enabled globally
- **PABE always 0**: post-alpha blend disabled globally
- **Alpha test fails → ZB_ONLY**: on some grass passes, write Z on alpha fail to occlude behind grass
- **VIF1 stall handling**: GS can stall VIF1 when texture cache is busy — profiled via `ClHardwareSample`

## See Also

- [[PS2 GS Register Reference (from source)]] — Register table from GTA SA source
- [[PS2 DMA and VU1 System]] — How VIF/GIF submits to GS
- [[Hardware/Overview]] — Platform comparison
- [[Hardware Differences]] — PS2 vs Xbox vs PC summary
- [[PS2 Xbox Feature Emulation Guide]] — Emulating GS features in HLSL
