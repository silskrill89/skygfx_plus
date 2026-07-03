# SkyGFX Plus - Obsidian Vault Index

## 📁 Vault Structure
```
docs/obsidian-vault/
├── BRDF/
│   └── Non-Parametric Sparse BRDF.md
├── AntiAliasing/
│   ├── SMAA Enhanced Subpixel Morphological AA.md
│   ├── Dynamic Temporal AA Call of Duty.md
│   └── Conservative Morphological AA.md
├── RenderWare/
│   └── RenderWare V2.1 API Reference.md
├── EPK-Reference/
│   └── GTA San Andreas EPK Reference.md
├── SkyGFX-Pipeline/
│   └── SkyGFX Pipeline Overview.md
└── INDEX.md (this file)
```

## 🔗 Cross-References
- All papers link to each other via `[[wikilinks]]`
- BRDF values feed into vehicle/building PBR shaders
- AA papers inform SMAA implementation
- RW docs inform pipeline callback structure
- EPK screenshots are visual target reference

## 📚 Source Files
| Document | Source Path |
|----------|-------------|
| BRDF Paper | `E:\docs\TOG2021_non_parametric_sparse_brdf_final.pdf` |
| SMAA Paper | `E:\docs\SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf` |
| COD DTAA | `E:\docs\Dynamic_Temporal_Antialiasing_and_Upsampling_in_Call_of_Duty_v4.pdf` |
| CMAA Paper | `E:\docs\conservative-morphological-anti-aliasing.pdf` |
| RW Docs | `E:\docs\rendfereware\` |
| EPK Screens | `E:\docs\VC and SA EPKs\San Andreas EPK\` |

## 📖 How to Use
1. Open this folder in Obsidian (File → Open Vault → `docs/obsidian-vault`)
2. Use graph view to see connections between topics
3. Search for specific materials or techniques
4. Link to SkyGFX source files using `[[filename]]` syntax
