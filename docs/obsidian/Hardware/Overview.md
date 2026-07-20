# Hardware Platform Deep-Dives

This directory contains detailed architectural documentation for each target platform that SkyGFX Plus emulates. The goal is to document *why* each platform's output looks different and *how* those differences are reproduced in D3D9.

## Platform Docs

| Doc | Platform | GPU | Key Differentiator |
|-----|----------|-----|-------------------|
| [[Hardware/PS2 GS\|PS2 GS.md]] | PlayStation 2 | Graphics Synthesizer | Fixed-function register-based rasterizer, 4 MB eDRAM, no programmable shading |
| [[Hardware/Xbox NV2A\|Xbox NV2A.md]] | Xbox | NVIDIA NV2A (NV20-derivative) | Programmable vertex/texture shaders, 8 register combiner stages, unified memory |
| [[Hardware/Early PC GPUs\|Early PC GPUs.md]] | PC (2001-2005) | GeForce 3/4/FX, Radeon 8500-9800 | DX8/DX9 programmable pipeline, general-purpose shading |

## Common Questions Answered Here

1. **Why can't PC just use the PS2 pipeline directly?** — GS is not programmable; all effects are implicit in register state. PC must reverse-engineer and emulate each register interaction in HLSL.

2. **Why does Xbox look different from PC even though both have programmable shaders?** — NV2A register combiners have unique math (9-bit signed, specific bias/multiply modes) and texture shaders (BRDF, dependent reads) that D3D9 can't replicate 1:1.

3. **What does RenderWare abstract?** — RW hides most hardware differences behind `RpWorld` and `RpAtomic` pipelines. Platform-specific code lives in *custom* R\* callbacks (PS2All pipe, Xbox vehicle/building pipes, PC world/vehicle pipes).

4. **Which differences are "accidental" vs "intentional"?** — Some (COLCLAMP, dither matrix) are PS2 hardware quirks. Others (dual-pass alpha, TFX modulation, per-pixel specular) are deliberate R\* code differences between platform ports.

## GTA SA Cross-Platform Pipeline Matrix

| Pipeline | PS2 | Xbox | PC |
|----------|-----|------|----|
| Vehicle | PS2All pipe (VU1) | Xbox vehicle pipe (register combiners) | PC vehicle pipe (D3D FF or shader) |
| Building (opaque) | PS2All pipe (VU1) | Xbox building pipe | PC building pipe |
| Building (transparent) | PS2All pipe (dual-pass) | Xbox building pipe + alpha | PC building pipe |
| Grass | Custom PS2 (VU1 microcode) | Custom Xbox (combiners) | PC default RW |
| Water | PS2All custom | Custom Xbox | PC default RW |
| Post-FX | GS registers only | Combiners + shaders | D3D9 shaders |

## See Also

- [[Hardware Differences]] — Quick reference comparison
- [[PS2 GS Register Reference (from source)]] — GS register table from game code
- [[PS2 DMA and VU1 System]] — DMA channels, VIF commands, VU1 microprograms
- [[PS2 Xbox Feature Emulation Guide]] — Concrete emulation approaches
