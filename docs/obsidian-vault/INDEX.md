# SkyGFX Plus — Obsidian Vault Index

## Vault Structure

```
docs/obsidian-vault/
├── 00-Index/
│   └── Home.md
├── 03-Shaders/
│   └── VehiclePBR Modern.md
├── 06-Technical-Decisions/
│   ├── IBL Env Map Decision.md
│   └── Rubber Shader Merge.md
├── 08-Build-Deploy/
│   ├── Build System.md
│   └── SDK Dependencies.md
├── AntiAliasing/
│   ├── Conservative Morphological AA.md
│   ├── Dynamic Temporal AA Call of Duty.md
│   └── SMAA Enhanced Subpixel Morphological AA.md
├── Architecture/
│   └── 32_to_64_Bit_Transition_Reference.md
├── BRDF/
│   └── Non-Parametric Sparse BRDF.md
├── EPK-Reference/
│   ├── EPK Visual Reference - Quick Look.md
│   └── GTA San Andreas EPK Reference.md
├── RenderWare/
│   ├── GTA IV RAGE Engine Complete Reference.md
│   ├── GTA IV RAGE Engine Format Reference.md
│   └── RenderWare V2.1 API Reference.md
├── SkyGFX-Pipeline/
│   ├── 64-bit Bridge Architecture.md
│   ├── Decided Architecture.md
│   ├── Roadmap.md
│   ├── Signal Splitter Architecture.md
│   ├── SkyGFX Pipeline Overview.md
│   ├── Three Codebase Comparison.md
│   ├── Weather System Architecture.md
│   ├── Wheel Extender Technical Plan.md
│   ├── Wheel System Architecture.md
│   └── Workflow Quiz.md
└── INDEX.md (this file)
```

## Cross-References to Main docs/

The vault contains deep-dive research and architecture documents. For project-level docs, see the parent `docs/` directory.

| Vault Topic | Main docs/ Counterpart |
|-------------|----------------------|
| [[SkyGFX Pipeline Overview]] | `docs/Vehicle Pipeline.md`, `docs/Building Pipeline.md`, `docs/Unified Pipeline.md` |
| [[VehiclePBR Modern]] | `docs/VehiclePBR Modern.md` |
| [[Build System]] | `docs/Build System.md` |
| [[SDK Dependencies]] | `docs/SDK Dependencies.md` |
| [[Decided Architecture]] | `docs/Project Lineage.md` |
| [[Non-Parametric Sparse BRDF]] | `docs/BRDF Reference.md` |
| [[SMAA Enhanced Subpixel Morphological AA]] | `docs/PostFX Pipeline.md` |
| [[RenderWare V2.1 API Reference]] | `docs/RW SDK Reference.md` |
| [[GTA San Andreas EPK Reference]] | — (visual reference only) |
| [[32_to_64_Bit_Transition_Reference]] | — (architecture research) |
| [[Wheel System Architecture]] | `docs/wheel_lod_system.md`, `docs/wheel_naming_convention.md`, `docs/wheel_texture_atlas.md` |
| [[Weather System Architecture]] | `docs/Weather Timecycle.md` |
| [[Three Codebase Comparison]] | `docs/Project Lineage.md` |
| [[Roadmap]] | `docs/Future Features.md`, `docs/Roadmap to Ultimate Mod.md` |

## Source Files

| Document | Source Path |
|----------|-------------|
| BRDF Paper | `E:\docs\TOG2021_non_parametric_sparse_brdf_final.pdf` |
| SMAA Paper | `E:\docs\SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf` |
| COD DTAA | `E:\docs\Dynamic_Temporal_Antialiasing_and_Upsampling_in_Call_of_Duty_v4.pdf` |
| CMAA Paper | `E:\docs\conservative-morphological-anti-aliasing.pdf` |
| RW Docs | `E:\docs\rendfereware\` |
| EPK Screens | `E:\docs\VC and SA EPKs\San Andreas EPK\` |

## How to Use

1. Open this folder in Obsidian (File → Open Vault → `docs/obsidian-vault`)
2. Use graph view to see connections between topics
3. Search for specific materials or techniques
4. All cross-references use `[[wikilink]]` format
5. For project-level docs, navigate up to `docs/` directory
