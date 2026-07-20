# Wheel Naming Convention

## Status: Planned / Convention Reference

> **Note**: This naming convention is the planned standard for extended wheel DFFs. The current SA stock wheels use frame names like `wheel_smallcar`, `wheel_sport`, etc. from `wheels.DFF`.

## Format
```
wheel_{class}_{style}_{variant}
```

## Class Types

| Class | Description | Vehicle Mapping |
|-------|-------------|-----------------|
| `steel` | Basic steel wheels | Economy cars, trucks |
| `alloy` | Standard alloy wheels | Sedans, luxury |
| `sport` | Performance wheels | Sports cars |
| `muscle` | Muscle car wheels (wide, aggressive) | Muscle cars |
| `offroad` | Off-road/truck wheels (large, rugged) | Trucks, SUVs |
| `luxury` | Premium wheels | High-end vehicles |
| `tuner` | Aftermarket/tuner wheels | Custom styles |

## Style Codes
- **01-99** — Numeric style identifier within class

## Variant Codes

| Code | Position |
|------|----------|
| `fl` | Front left |
| `fr` | Front right |
| `rl` | Rear left |
| `rr` | Rear right |
| `uni` | Universal (same for all positions) |

## Examples
```
wheel_steel_01_uni      - Basic steel wheel, universal
wheel_alloy_05_fl       - Alloy wheel style 05, front left
wheel_sport_12_uni      - Sport wheel style 12, universal
wheel_muscle_03_rr      - Muscle wheel style 03, rear right
wheel_offroad_02_uni    - Off-road wheel style 02, universal
wheel_luxury_08_fl      - Luxury wheel style 08, front left
wheel_tuner_15_uni      - Tuner wheel style 15, universal
```

## SA Stock Wheel Classes

| Style | Frame Name | Compatible Classes |
|-------|-----------|-------------------|
| smallcar | wheel_smallcar | steel, alloy |
| offroad | wheel_offroad | offroad |
| truck | wheel_truck | offroad, steel |
| rim | wheel_rim | steel |
| alloy | wheel_alloy | alloy |
| lightvan | wheel_lightvan | steel, alloy |
| lighttruck | wheel_lighttruck | offroad |
| classic | wheel_classic | alloy, luxury |
| saloon | wheel_saloon | alloy, sedan |
| sport | wheel_sport | sport, tuner |

## Vehicle Class Mapping

| Vehicle Category | Recommended Wheel Classes |
|-----------------|--------------------------|
| Economy (Bravura, Premier, Washington) | steel_01-05 |
| Luxury (Elegant, Emperor, Greenwood) | luxury_01-10, alloy_01-05 |
| Sports (Cheetah, Banshee, Infernus) | sport_01-15 |
| Muscle (Clover, Manana, Tampa, Sabre) | muscle_01-10 |
| Trucks (Flatbed, Rancher, Walton) | offroad_01-08, steel_06-10 |
| Tuner (Sultan, Elegy, Jester) | tuner_01-20 |

## LOD Variants
Append `_lod1`, `_lod2` suffix for distance-based switching:
```
wheel_sport_01_uni_lod1    (LOD1 - medium detail)
wheel_sport_01_uni_lod2    (LOD2 - low detail)
```

## Implementation Notes
- Wheel DFF files should follow this naming convention
- Metadata JSON maps original vehicle wheels to new names
- Hash-based selection uses class to determine wheel pool
- See `wheels_extender.cpp` for the selection algorithm

## See Also
- [[wheel_lod_system.md]] — LOD switching details
- [[Wheel System Architecture]] — Full system overview
- [[Wheel Extender Technical Plan]] — Implementation plan
