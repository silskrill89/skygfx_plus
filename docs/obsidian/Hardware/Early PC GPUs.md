# Early PC GPUs (2001-2005)

The PC GPUs that shipped GTA SA in 2005 were a fragmented mix of DX7/DX8/DX9 hardware. Unlike consoles with fixed hardware targets, PC RenderWare had to accommodate everything from a GeForce 2 to a GeForce 6.

## Target GPU Generations for GTA SA

| Generation | Typical Card | DX Level | Pixel Shader | Key Limitation |
|-----------|-------------|----------|-------------|----------------|
| DX7 | GeForce 2 / Radeon 7500 | 7.0 | None (fixed function) | No shaders at all |
| DX8 | GeForce 3/4 Ti / Radeon 8500 | 8.0 | ps_1_1 - ps_1_4 | Limited instruction count, no branching |
| DX8.1 | GeForce 4 Ti / Radeon 9000 | 8.1 | ps_1_4 | Up to 96 instructions, dependent reads |
| DX9 (low) | GeForce FX 5200 / Radeon 9600 | 9.0 | ps_2_0 | 64+ instructions, static branching |
| DX9 (mid) | GeForce FX 5900 / Radeon 9700 | 9.0b | ps_2_a (NV) / ps_2_b (ATI) | More temps/instructions |
| DX9 (high) | GeForce 6800 / Radeon X800 | 9.0c | ps_3_0 | Dynamic branching, 512+ instructions |

GTA SA PC shipped with no pixel shaders — it used D3D fixed-function texturing (stage states) for all rendering. SkyGFX and SkyGFX Plus target **ps_2_0 and ps_3_0** hardware, covering the DX8/DX9 range above.

## DX7 Era (GeForce 2, Radeon 7500)

### Capabilities
- Multi-texturing: up to 4 texture stages (D3D fixed function)
- Alpha blending: D3D fixed function (op + src/dst factors)
- Environment mapping: sphere map via texture coordinate generation
- Fog: table fog or vertex fog
- No pixel shaders, no vertex shaders

### GTA SA on DX7
- RenderWare falls back to FF pipeline
- No normal maps, no per-pixel lighting
- Reflection = sphere map only
- Grass = alpha-tested billboards (no dual-pass)

## DX8 Era (GeForce 3/4 Ti, Radeon 8500-9250)

### ps_1_1 - ps_1_3 (GeForce 3/4 Ti)

- Max 4 texture instructions + 4 arithmetic instructions
- No dependent texture reads (except `texm3x3vspec`/`texm3x3tex`)
- No swizzles or negation beyond basic `.rgb`/`.aaa`
- Limited to `tex`, `texcoord`, `texkill`, `texbem`, `texm3x2pad`, `texm3x2tex`, `texm3x3pad`, `texm3x3tex`, `texm3x3vspec`, `texm3x3spec`, `texdepth`, `bem`, `add`, `sub`, `lrp`, `mov`, `mul`, `dp3`, `dp4`, `mad`

**SkyGFX compatibility**: SkyGFX core supports ps_1_1-1_3. Used as low-end fallback.

### ps_1_4 (Radeon 8500-9250, GeForce FX)

- Up to 96 instructions (32 texture + 64 arithmetic phase)
- Dependent texture reads in phase 2 (uses texture as coordinate)
- Per-phase swizzle control
- `phase` instruction to separate texture and arithmetic passes
- Additional ops: `cnd` (condition), `bias`, `bx2`, `signed scale`, `comparison`

**SkyGFX compatibility**: Phase-based architecture is difficult to target. SkyGFX Plus targets ps_2_0+ instead.

## DX9 Era (GeForce FX, Radeon 9xxx-X800)

### ps_2_0 (DX9 baseline)

- 96 minimum instructions (practically unlimited)
- Up to 32 temporary registers (`r0-r31`)
- [`texld`] with arbitrary coordinates
- Static flow control: `if`/`else`/`endif` with constant bool, `rep`/`endrep`
- No dynamic loops, no subroutines
- All ALU ops available: `abs`, `cmp`, `crs`, `dp2add`, `dst`, `expp`, `frc`, `lit`, `logp`, `lrp`, `m3x2`, `m3x3`, `m4x3`, `m4x4`, `max`, `min`, `mov`, `mul`, `nop`, `nrm`, `pow`, `rcp`, `rsq`, `sge`, `sincos`, `slt`, `texld`, `texkill`

**SkyGFX Plus target**: Minimum shader model. All features work here.

### ps_2_a (NVIDIA, GeForce FX)

- 512+ ALU instructions
- Up to 64 temporary registers (`r0-r63`)
- Up to 8 texture samplers (vs 16 in ps_2_0's binding limit)
- Additional flow control: static `call`/`callnz`/`ret`
- 4 face register (`vFace` in v_pos factor)

### ps_2_b (ATI/AMD, Radeon 9500-9800, X-series)

- Up to 160 ALU instructions
- Up to 96 temporary registers
- Up to 16 texture samplers
- Same flow control as ps_2_0 (limited)

### ps_3_0 (GeForce 6, Radeon X1k)

- Dynamic branching: `if`/`else` with per-pixel predicate
- 512+ ALU instructions, 512+ texture instructions (essentially unlimited)
- Unlimited temporary registers (32 minimum, but effectively unbounded)
- 10 texture samplers minimum
- `ddx`/`ddy` for gradient computation (used in PCF soft shadows)
- `texldd` for explicit LOD/derivative
- `loop`/`endloop` with `aL` register for dynamic iteration
- `vPos` as input register (screen position, used for dither)

**SkyGFX Plus target**: All features (Forward+, SSAO, SMAA, SSS) target ps_3_0.

## RenderWare's D3D Abstraction

RenderWare 3.6 abstracts the GPU as D3D9 device capabilities. When SkyGFX installs custom pipelines, it must check:

```
D3DCAPS9:
  - MaxTextureWidth/Height (typically 2048-4096)
  - MaxTextureStages (min 8, but FF path may limit)
  - VertexShaderVersion / PixelShaderVersion
  - MaxVertexShaderConst (256 for vs_3_0)
  - NumSimultaneousRTs (1 for most cards, 4 for high-end)
```

### FF Pipeline Fallback

If `PixelShaderVersion < D3DPS_VERSION(2,0)`, SkyGFX falls back to:

- **D3DTOP_MODULATE** for texture blending (no MODULATE2X on DX7 cards)
- **D3DTOP_BLENDTEXTUREALPHA** for alpha-tested transparencies
- **D3DTOP_BLENDDIFFUSEALPHA** for vertex alpha with texture
- Fixed-function fog via `D3DRS_FOGENABLE` + `D3DRS_FOGCOLOR`

## Why PC GTA SA Looks Different

### Missing PS2 Effects on PC

| Effect | PS2 | PC DX7-DX9 | Why Missing |
|--------|-----|-----------|-------------|
| `MODULATE2X` | Default TFX | Requires shader or custom FF | GS does MODULATE2X by default; PC FF only does MODULATE |
| Per-pixel alpha test | GS register | Requires dual-pass | PC alpha test discards pixel entirely |
| Radiosity blend | GS ALPHA reg | Requires pixel shader | D3D blend factors can't produce 2× |
| 4×4 dither | GS DIMX/DTHE | Requires shader | PC has no programmable dither |
| COLCLAMP | GS register | Requires shader clamp | PC default clamps to [0,1] automatically |
| Interlace | GS DISP reg | Requires shader | PC progressive only |

### Missing Xbox Effects on PC

| Effect | Xbox NV2A | PC DX9 | Why Missing |
|--------|----------|--------|-------------|
| Texture shader BRDF | Hardware unit | Manual in shader | DX9 has no BRDF texture unit |
| 4× MSAA free | EDRAM | VRAM limited | PC MSAA costs VRAM, no EDRAM |
| Register combiner math | 9-bit unsigned | No equivalent | Replicated with HLSL |
| `BUMPENVMAP` | Texture shader | `texbem` or manual | NV2A does in one cycle |
| Shadow buffer | Depth texture sampled | D3D9 depth map | Requires RTT + shader samplers |

## See Also

- [[Hardware Differences]] — Quick comparison with PS2 and Xbox
- [[Hardware/Overview]] — Platform comparison
- [[Shader Architecture]] — How SkyGFX targets multiple shader models
- [[Performance Tuning]] — Performance implications per GPU tier
- [[Forward Plus Rendering]] — ps_3_0 only
