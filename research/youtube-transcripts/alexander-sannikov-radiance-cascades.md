# Alexander Sannikov — Radiance Cascades Research

## Overview
Alexander Sannikov is a Senior Programmer at Grinding Gear Games (Path of Exile).
Creator of Radiance Cascades (RC) — a novel real-time global illumination technique
used in Path of Exile 2.

## YouTube Channel
- **Channel**: https://www.youtube.com/user/DonXenapo
- **GitHub**: https://github.com/Raikiri

## Key Videos (DonXenapo)

### Radiance Cascades Family
| ID | Title | Duration | Description |
|----|-------|----------|-------------|
| `o2kgW2TBpUo` | 2d Full GI #2 | 2:34 | Working principle and perf tests. Cascade-based sampling: farther cascades = more ray directions, less origin info |
| `eS0zd_8nfhk` | 2d Full GI #1 | 0:50 | Proof of concept, no denoising, interactive fps |
| `5Ua-h1pg6yM` | Hierarchical radiance cascades GI | 1:11 | Expanded 2D GI to 3D. Spreading cascade calc over multiple frames |
| `AhbnwZFp5iE` | Polygonal Radiance Cascades | 0:53 | Alias-free light from single pixel source. Interactive demo: radiance-cascades.github.io |
| `rG2aok2SdbU` | Sparse Radiance Cascades | 1:31 | Goodbye screenspace. HW-accelerated raytracer replaces screenspace raymarcher |
| `TGLAxW0xVDU` | Sparse 3D Radiance Cascades | 2:15 | Full 3D world space RC with sparse GPU hashmap. 60-100fps on laptop |
| `xT_7JtYXdNc` | Exploring 3D holographic radiance cascades | 0:37 | Sponza stored in HRC, rendered directly |
| `xkJ6i2N32Pc` | Radiance Cascades Rendered Directly | 5:24 | Explore data stored in RC by observing it directly. Pre-calculated scene cross-section |
| `N6RP3r1M9g4` | Single shot GI with no denoiser | 2:37 | Cascaded image space GI. Converges ~5ms, no denoising needed |

### Other Rendering Work
| ID | Title | Duration |
|----|-------|----------|
| `b4jpSx5FA7c` | Rendering fragment shader grass in O(1) | — |
| `aBPwnzUtfkA` | O(1) precomputed raymarching | — |
| `7JEHPvSGaX8` | Cluster-based rendering, Nanite-style | — |
| `x4M65rI0AKc` | Sparse voxel signed distance octree ray-guided GPU streaming | — |

## Key Talks

### ExileCon 2023 — Rendering Path of Exile 2 (`TrHHTQqmAaM`)
- Duration: 52:56
- Chapters: SUBSURFACE SHADOWS (6:44), BENT NORMALS SHADOWS (15:39), GLOBAL ILLUMINATION (26:52)
- First public presentation of Radiance Cascades
- Covers predecessor techniques: HSSVGI, hierarchical screenspace shadow cascades
- Shadertoy demo: https://www.shadertoy.com/view/mlscz8
- Paper (Google Drive): https://drive.google.com/file/d/1L6v1_7HY2X-LV3Ofb6oyTIxgEaP4LOI6/view

### ExileCon 2019 — Evolving Path of Exile's Renderer (`whyJzrVEgVc`)
- Duration: 54:37, 33 chapters
- PBR, Shadows (SSHVSM), GI (HBVSSGI), Subsurface refraction, Grass rendering, Vectorization

## The Paper

### Radiance Cascades: A Novel Approach to Calculating Global Illumination
- **Author**: Alexander Sannikov, Grinding Gear Games
- **Status**: WIP / preprint (CC BY-ND 3.0)
- **GitHub**: https://github.com/Raikiri/RadianceCascadesPaper
- **PDF**: https://github.com/Raikiri/RadianceCascadesPaper/blob/main/out_latexmk2/RadianceCascades.pdf
- **Wiki**: https://radiance.wiki/papers/sannikov-original

### Core Concepts

#### Penumbra Hypothesis
Resolving light from an object requires:
- **Higher spatial resolution** (more probes) near the object
- **Higher angular resolution** (more rays) far from the object

This inverse relationship is exploited by RC: each cascade level doubles ray count
while halving probe density, then merging reconstructs full radiance field cheaply.

#### Cascade Data Structure
- Decomposes radiance field into multiple ranges (near-field to far-field)
- Each range stored in its own cascade
- Cascade i: linear step ~2^i, angular step ~1/2^i
- Memory for all cascades in 2D bounded by 2× memory of cascade 0

#### Radiance Intervals
- Each probe stores radiance for a specific range [a, b], not full radiance
- Intervals can be extended with itself (doubling range)
- Allows replacing linear-time raymarcher with short-range + exponential extension

#### Scaling Properties
- Geometry-agnostic: constant cost regardless of scene complexity
- Scene-independent: same time for 2, 102, or 1002 particles
- For 2D: ~30ms on GTX3060, ~0.3ms on GTX970 (simple scenes)
- "Casting infinitely many rays in finite time with finite memory"

### PoE2 Implementation
- Uses screenspace radiance probe cascades (flatland-like)
- Populated via screenspace raymarching (fixed camera advantage)
- 4 rays raymarched simultaneously, occlusion stored in 128-bit bitmask
- Equivalent to 128 binary occlusion rays per cascade texel

## Holographic Radiance Cascades (HRC)
- **Paper**: https://arxiv.org/abs/2505.02041
- **Authors**: Rouli Freeman, Alexander Sannikov, Adrian Margel
- Extension of RC removing redundancies in radiance field encoding
- Handles hard shadows better
- Specialized acceleration structure (no empty space skipping)
- 1.85ms for 512×512, 7.67ms for 1024×1024 on RTX 3080
- Handles detailed volumetrics efficiently (no slowdown vs empty scenes)

## Related Resources

### Tutorials
- **GM Shaders Part 1**: https://mini.gmshaders.com/p/radiance-cascades
- **GM Shaders Part 2**: https://mini.gmshaders.com/p/radiance-cascades2
- **Jason McGhee**: https://jason.today/rc
- **SimonDev**: https://www.youtube.com/watch?v=3so7xdZHKxw

### Implementations
- **Unity URP**: https://github.com/alexmalyutindev/unity-urp-radiance-cascades
- **GameMaker**: https://github.com/Yaazarai/GMShaders-Radiance-Cascades
- **Shadertoy**: https://www.shadertoy.com/view/mlscz8
- **LegitScriptEditor**: https://radiance-cascades.github.io/LegitScriptEditor/

### Key Optimizations (from community)
1. **Pre-Averaging**: Cast 4x rays, average → 1/4 angular resolution stored
2. **Direction-First Probes**: Enable hardware interpolation for merge
3. **Single-Tap Merge**: From 16 texture samples → 1 sample
4. **Bilinear Fix**: Smooths cascade transitions, fixes ringing artifacts

## 2D vs 3D
- **2D (Flatland)**: Mature, noiseless, constant cost. Production-ready.
- **3D Screenspace**: Used in PoE2. Fixed camera assumption.
- **3D World Space**: Experimental. Volume texture probes. Expensive storage but allows atmospheric scattering, participating media.
- **Sparse 3D**: Only visible probes stored in GPU hashmap. 60-100fps.
