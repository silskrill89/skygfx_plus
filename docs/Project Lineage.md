# Project Lineage

#history #architecture

## Version Evolution
```
aap (original) → junior (fork) → zeneric → plk4 → expIV (current)
```

## Lineage Summary

### aap (Original SkyGFX)
- ~500 lines, minimal dependencies
- PS2/PC/Xbox building and vehicle pipelines
- Basic color filters, radiosity
- No SSAO, SMAA, or PBR

### junior (Fork)
- ~1000 lines, added C++ stdlib
- Added SPEC, MOBILE, NEO, LCS, VCS, ENV vehicle pipes
- Wind shaders, stochastic texturing
- GTA IV building pipeline (basic)

### expIV (Current)
- ~15,000 lines, full RW ecosystem
- GTA IV vehicle pipe, Modern PBR vehicle pipe
- Full SSAO, SMAA, motion blur, SSS
- GTA IV filmic tonemapping
- Weather/timecycle expansion (GTA V style)
- Normal buffer, pipe chain
- Per-vehicle classification system
- Quality presets (LOW/MED/HIGH/ULTRA)
- Consolidated shaders (VehiclePBR Modern, buildingPipePS)

## Key Technical Evolution

| Feature | aap | junior | expIV |
|---------|-----|--------|-------|
| Car Pipelines | 3 | 8 | **11** |
| Building Pipelines | 2 | 2 | **3** |
| Config Fields | ~24 | ~24 | **239+** |
| Color Filters | 2 | 2 | **8** |
| HLSL Shaders | ~50 | ~60 | **80+ (consolidated)** |
| PostFX Effects | 3 | 3 | **15+** |

## See Also
- [[Implemented Features]] — What's built on this foundation
- [[Credits]] — Who contributed
