# Color Grading

#color #grading #postfx

## Overview

Three-point color grading system for global color correction. Runs in the `gradingPS` shader. Used by Mobile and Modern color filters.

## Controls

- **Top** — Brights/Highlights
- **Mid** — Midtones
- **Bot** — Shadows/Lows

Each point has RGB + intensity controls, plus midpoint parameters for interpolation.

## Config

```ini
gradTopR, gradTopG, gradTopB
gradMidR, gradMidG, gradMidB
gradBotR, gradBotG, gradBotB
gradMidpoint, gradTopMidMidpoint, gradMidBotMidpoint
```

## Colorcycle Integration

The Mobile and Modern color filters use a `colorcycle.dat` file for time-of-day color grading. The system interpolates between 8 time slots × 23 weather types to produce per-frame grading values.

## Usage

Grading runs in the unified pipeline post-pass, after SSAO and SMAA. Also used standalone by Mobile and Modern color filters via `gradingPS` shader.

## See Also

- [[Unified Pipeline]] — Where grading runs in unified mode
- [[PostFX Pipeline]] — Other effects
- [[Weather Timecycle]] — Time-of-day system
