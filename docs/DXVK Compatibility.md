# DXVK Compatibility

#dxvk #vulkan #compatibility #config

## Overview

[DXVK](https://github.com/doitsujin/dxvk) translates D3D9 calls to Vulkan, enabling modern GPU driver paths for GTA San Andreas. skygfx hooks **RW/game functions** (not `d3d9.dll` exports), so it is compatible with DXVK — the mod never touches D3D9 API entry points directly. This document covers installation, recommended settings, and known issues.

## Installation

> **Important**: DXVK **must** be placed in the game root directory (next to `gta_sa.exe`). Do **not** put DXVK in an ASI loader `plugins` folder — ASI loaders inject into the game process, but DXVK needs to intercept `d3d9.dll` at load time via DLL search order.

### With the GTA Bridge Launcher (recommended)

The bridge launcher already deploys DXVK as `vulkan.dll` in the game root and the bridge `d3d9.dll` proxy forwards all D3D9 exports to it. No manual setup needed.

### Manual installation

1. Download the latest DXVK release from [github.com/doitsujin/dxvk/releases](https://github.com/doitsujin/dxvk/releases).
2. Extract `d3d9.dll` from the x32 (32-bit) folder.
3. Rename it to `vulkan.dll` (the name expected by the bridge proxy).
4. Place `vulkan.dll` and `dxvk.conf` (from this repo's `resources/`) in the game root next to `gta_sa.exe`.

**Without the bridge proxy**: You can install DXVK as `d3d9.dll` directly, but this will conflict if any other mod (e.g., SilentPatch, ReShade overlay) also expects `d3d9.dll`. The bridge proxy (FusionFix pattern) is the recommended stacking solution.

### File layout (correct)

```
E:\games\gtasa_skygfx_plus\
├── gta_sa.exe
├── d3d9.dll          ← bridge proxy (forwards to vulkan.dll)
├── vulkan.dll         ← DXVK (renamed)
├── dxvk.conf          ← settings (this file)
├── skygfx.asi         ← skygfx mod
└── ...
```

## Recommended Settings

| Setting | Value | Purpose |
|---------|-------|---------|
| `d3d9.extraFrontbuffer` | `True` | Enables the extra front buffer needed by skygfx post-FX (trails, motion blur, radiosity) that read/write the front buffer. Without this, those effects produce black output or crash. |
| `d3d9.floatEmulation` | `Strict` | Squashes NaN from PBR shader divisions (GGX/Smith denominator, unguarded `normalize()`) to zero. SM3.0 NaN propagation shows as black. |
| `d3d9.presentInterval` | `-1` | Lets the game control vsync. Avoids forcing triple-buffering when the game uses `D3DPRESENT_INTERVAL_DEFAULT`. |
| `d3d9.maxFrameLatency` | `1` | Minimises input lag by limiting the GPU queue depth. |
| `d3d9.supportDFFormats` | `True` | Keeps DF16/DF24 depth formats available for post-FX depth reads (SSAO INTZ path). |
| `d3d9.supportD32` | `True` | D32 fallback when DF24 is unavailable. |
| `d3d9.useD32forD24` | `False` | Keep native D24S8 where the game expects it. |
| `d3d9.deviceLossOnFocusLoss` | `False` | **Critical**: Do NOT destroy the device on focus loss — GTA SA handles alt-tab internally and crashes if DXVK destroys the device underneath it (dxvk#5106). |

## Known Issues

### Dual-pass flicker (dxvk#2283)

Some dual-pass effects in skygfx (e.g., certain refraction or blend operations) may flicker on older DXVK versions due to incorrect sRGB read/write handling for the extra front buffer. **Fix**: Update to the latest DXVK release — this is fixed in recent builds.

### Front-buffer effects cost

`extraFrontbuffer = True` incurs a small performance cost (driver must allocate and manage a second front buffer). This is negligible on modern GPUs but may be measurable on integrated or very old hardware. skygfx features that rely on front-buffer access: motion blur trails, radiosity accumulation, certain blend-state readbacks.

### Depth StretchRect blocked by design

DXVK does not support `StretchRect` between depth surfaces with different formats — it is [blocked by design](https://github.com/doitsujin/dxvk#depth-stretchrect) because the Vulkan driver cannot reliably emulate D3D9 behaviour here. skygfx has a work-in-progress depth-access redesign to avoid relying on this path. If you see depth-copy failures in the log, this is the cause.

### DXVK version recommendations

| Scenario | Recommended DXVK |
|----------|-----------------|
| Normal play | Latest stable release (≥2.5) |
| Using front-buffer effects | Latest stable or latest git (has fix for dxvk#2283) |
| Debugging shader issues | Latest git (has improved NaN handling) |

## Technical Notes

- **skygfx hooks RW/game functions**, not `d3d9.dll` exports. This means DXVK's translation layer is transparent to skygfx — the mod sees D3D9 interfaces as normal.
- **DXVK unlocks >SM3.0 VRAM budgets** on modern GPUs that lack native D3D9 driver support (NVIDIA Turing/Ampere, AMD RDNA2+). The bridge proxy + DXVK combo is the primary target for modern-hardware deployment.
- The `dxvk.conf` values above are the **known-good set** for the `skygfx_plus_expIV` PBR pipeline. Other values should be left at DXVK defaults unless you have a specific reason to change them.