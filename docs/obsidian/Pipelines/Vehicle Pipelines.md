# Vehicle Pipelines

SkyGFX Plus implements multiple vehicle rendering pipelines, each recreating the look of a different platform version. The pipeline is selected with `vehiclePipe` in the INI.

## Available Pipelines

### PS2
- Full EnvMap1 and EnvMap2 support
- Specular lighting via [vehiclespecdot64](http://gta.rockstarvision.com/skygfx/guide_files/vehiclespecdot64.png) texture
- Most accurate platform recreation

### PC (Default)
- EnvMap1 and EnvMap2
- EnvMap2 quality is lower due to hardware limitations
- Lighting is [broken due to timecycle issues](../Timecycle%20Fix)

### Xbox
- EnvMap1 and EnvMap2
- Not lit by extra directional lights
- Diffuse and specular light are *averaged* (not added), resulting in darker colour

### Specular
- Like PS2 but uses specular lighting instead of a dot texture
- Cleaner, more physically plausible reflections

### Mobile
- Both EnvMaps rendered with sphere mapping
- No separate textures needed
- Specular settings from EnvMap plugin data
- Effects are less cleanly separated

### Neo
- From Xbox III/VC (RAGE engine)
- Fake sphere map applied with fresnel effect
- Modulated with [CarReflectionMask](http://gta.rockstarvision.com/skygfx/guide_files/CarReflectionMask_SA.png)
- Controlled by `carTweakingTable.dat`

### LCS / VCS
- From PS2 LCS and VCS (RSL engine)
- LCS: env map from current camera with coronas for specular approximation; added to diffuse
- VCS: like LCS but env map is alpha blended instead of added

### Env
- Custom environment mapping pipeline
- Configurable shininess, specularity, power, fresnel
- Works with `envMapSize`, `envMapUseLODs`, `envMapFarClipMult`

### GTA IV
- Forward rendering with per-pixel lighting
- GGX normal distribution, Smith visibility, Schlick Fresnel
- Burley diffuse (vehicles) / Oren-Nayar diffuse (buildings)
- Up to 8 dynamic lights per tile
- Environment map with wet road reflections

## Environment Map Textures

| Texture | Used By | Description |
|---------|---------|-------------|
| `vehicleenvmap128` | EnvMap1 (bikes, planes, boats) | Standard env map |
| `xvehicleenv128` | EnvMap2 (cars) | Requires second UV set |

## INI Settings

```ini
[SkyGfx]
vehiclePipe=PS2          ; PS2, PC, Xbox, Spec, Mobile, Neo, LCS, VCS, Env, GTAIV
dualPassVehicle=1        ; PS2 alpha test emulation
neoShininessMult=1.0     ; Neo pipeline
neoSpecularityMult=1.0   ; Neo pipeline
leedsShininessMult=1.0   ; Leeds/LCS pipeline
envShininessMult=1.0     ; Env pipeline
envSpecularityMult=1.0   ; Env pipeline
envPower=20.0            ; Env specular power
envFresnel=0.7           ; Env fresnel factor
envMapSize=256           ; Env map render target (64/128/256/512)
envMapFarClipMult=1.0    ; Env map far clip
envMapUseLODs=0          ; Use LODs for env map
fixPcCarLight=0          ; Fix PC directional light
```

## See Also

- [[Hardware Differences#PS2 Texture Modulation|PS2 Texture Modulation]]
- [[Hardware Differences#PS2 Alpha Test (Dual Pass)|PS2 Alpha Test]]
- [[Pipelines/Grass Rendering|Grass Pipeline]]
