# Color Grading

#color #grading #postfx

## Overview
Three-point color grading system for global color correction.

## Controls
- **Top** — Brights/Highlights
- **Mid** — Midtones
- **Bot** — Shadows/Lows

Each point has RGB + intensity controls.

## INI
```ini
gradingEnable=0
gradingTopR, gradingTopG, gradingTopB, gradingTopInten
gradingMidR, gradingMidG, gradingMidB, gradingMidInten
gradingBotR, gradingBotG, gradingBotB, gradingBotInten
gradingSatBoost=0
```

## Usage
Grading runs in the unified pipeline post-pass, after SSAO and SMAA.

## See Also
- [[Unified Pipeline]] — Where grading runs
- [[PostFX Pipeline]] — Other effects
