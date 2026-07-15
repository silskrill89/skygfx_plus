# Night and Infrared Vision

#night #infrared #postfx

## Overview

Night vision and infrared vision modes. Each has a standard version and a PS2-accurate version. Implemented in `postfx.cpp`.

## Night Vision

- Green-tinted overlay with grain noise
- Switch-on flash effect (configurable via `m_fNightVisionSwitchOnFXCount`)
- PS2 version (`NightVision_PS2`): Uses `SetFilterMainColour_PS2` for color overlay
- Standard version (`NightVision`): Uses game's original function

## Infrared Vision

- Heat-signature visualization with blur radius
- Grain noise overlay
- PS2 version (`InfraredVision_PS2`): Uses offset quads for blur sampling
- Standard version (`InfraredVision`): Uses game's original function

## Config

```ini
infraredVision=0         # 0=PS2 style, 1=standard
nightVision=0            # 0=PS2 style, 1=standard
infraredVisionR=255      # IR color (R/G/B)
infraredVisionG=0
infraredVisionB=0
nightVisionR=0           # Night vision color (R/G/B)
nightVisionG=255
nightVisionB=0
```

## See Also

- [[PostFX Pipeline]] — Other effects
