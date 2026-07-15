---
tags: [reference, comparison, architecture, lineage]
created: 2025-01-02
updated: 2026-07-15
---

# Three-Codebase Comparison

> [!info] Full documentation
> See `docs/Three-Codebase Comparison.md` for the complete 595-line file-by-file mapping across all three codebases.

Comprehensive comparison of all three skygfx codebases — the foundation of SkyGFX Plus development.

## Lineage

```
aap (original v4.2b) → junior_dr (fork) → skygfx_plus (expIV rewrite)
```

## Size Comparison

| Metric | aap Original | junior Fork | skygfx_plus (expIV) |
|--------|-------------|------------|-------------------|
| Lines of Code | ~500 | ~1,500 | ~15,000 |
| Source Files | 22 | 23 | 73 |
| Directory | Flat | Flat | Organized subdirs |
| Vehicle Pipes | 9 | 10 | 11 |
| Building Pipes | 2 | 2 | 4 |
| Shader Model | ps_2_0 | ps_2_0/3_0 | ps_3_0 |

## Key Differences

### aap Original (v4.2b)
- Minimal, clean codebase
- 9 vehicle pipes, 2 building pipes
- Basic post-processing
- No PBR, no SSAO, no SMAA

### junior Fork
- Added VCS/III/VC color filters
- Added stochastic sampling, wind animation
- Added radiosity, trails, grading
- YCbCr correction
- ~3x code growth

### skygfx_plus (expIV)
- Full PBR pipeline (GGX/Smith/Schlick)
- SMAA, SSAO, motion blur, SSS
- 64-bit bridge architecture
- Weather system with multi-timecyc
- Wheel extender system
- ~10x code growth from aap

## Related

- [[SkyGFX Pipeline Overview]] — current rendering architecture
- [[Decided Architecture]] — architecture decisions quiz
- [[64-bit Bridge Architecture]] — future 64-bit bridge plan
- [[Weather System Architecture]] — multi-timecyc weather
- [[Wheel System Architecture]] — extended wheel system

## Main Docs

- `docs/Project Lineage.md` — aap → junior → expIV evolution
