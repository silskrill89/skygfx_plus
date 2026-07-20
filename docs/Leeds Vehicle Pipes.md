# Leeds Vehicle Pipes

#vehicles #leeds #vcs #lcs

## Overview

Leeds-era vehicle pipes from GTA Liberty City Stories and Vice City Stories. Both use the same render callback (`_CB_leeds`) with mode-specific blend behavior. Implemented in `vehiclePipe.cpp`.

## Key Differences

### LCS (`CAR_LCS`)
- Environment map rendering via `leedsCarFxVS`
- Reflection texture blended with `rwBLENDONE` (additive)
- Shininess multiplied by 3.0× `leedsShininessMult`
- Base rendering via `vehiclePipeVS` + `simplePS`

### VCS (`CAR_VCS`)
- Same env map rendering as LCS
- Reflection texture blended with `rwBLENDINVSRCALPHA` (alpha-modulated)
- Same shininess formula as LCS
- Different blend mode produces more subtle reflections

## Rendering

```
_CB_leeds()
  ├── Pass 1: vehiclePipeVS + simplePS (base rendering)
  └── FX Pass: leedsCarFxVS (env map blend)
       ├── LCS: src=SRCALPHA, dest=ONE (additive)
       └── VCS: src=SRCALPHA, dest=INVSRCALPHA (alpha blend)
```

## Config

```ini
vehiclePipe=LCS
vehiclePipe=VCS
leedsShininessMult=1.0
```

## See Also

- [[Vehicle Pipeline]] — All pipe modes
- [[Backwards Compatibility]] — Verified settings
