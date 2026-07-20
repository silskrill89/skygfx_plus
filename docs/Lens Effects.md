# Lens Effects

#lens #postfx #fx

## Overview

Camera lens simulation effects. Config fields exist in `skygfx.h` but implementation is minimal — these are mostly placeholder configs without full rendering implementations.

## Distortion

```ini
lensDistortionCoeff=0.0
lensDistortionCubeCoeff=0.0
```

## Chromatic Aberration

```ini
lensChromaticAberrationCoeff=0.0
lensChromaticAberrationCubeCoeff=0.0
```

## Lens Artefacts

```ini
lensArtefactsInten=0.0
lensArtefactsIntenMinExp=0.0
lensArtefactsIntenMaxExp=0.0
```

## Grain (Fully Implemented)

```ini
grainFilter=0           # 0=PS2 style, 1=standard
grainStrength=128       # Grain intensity
```

The PS2 grain filter uses a VU-style random number generator (same algorithm as PCSX2) for deterministic noise generation.

## See Also

- [[PostFX Pipeline]] — Other effects
- [[INI Configuration]] — Config fields
