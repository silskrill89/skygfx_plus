# Wheel Naming Convention

## Format
`wheel_{class}_{style}_{variant}`

## Class Types
- **steel** - Basic steel wheels (economy cars, trucks)
- **alloy** - Standard alloy wheels (sedans, luxury)
- **sport** - Performance wheels (sports cars)
- **muscle** - Muscle car wheels (wide, aggressive)
- **offroad** - Off-road/truck wheels (large, rugged)
- **luxury** - Premium wheels (high-end vehicles)
- **tuner** - Aftermarket/tuner wheels (custom styles)

## Style Codes
- **01-99** - Numeric style identifier within class

## Variant Codes
- **fl** - Front left
- **fr** - Front right
- **rl** - Rear left
- **rr** - Rear right
- **uni** - Universal (same for all positions)

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

## Vehicle Class Mapping
- **Economy** (Bravura, Premier, Washington) → steel_01-05
- **Luxury** (Elegant, Emperor, Greenwood) → luxury_01-10, alloy_01-05
- **Sports** (Cheetah, Banshee, Infernus) → sport_01-15
- **Muscle** (Clover, Manana, Tampa, Sabre, Buccaneer, Phoenix) → muscle_01-10
- **Trucks** (Flatbed, Rancher, Walton) → offroad_01-08, steel_06-10
- **Tuner** (Sultan, Elegy, Jester) → tuner_01-20

## Implementation Notes
- Wheel DFF files should follow this naming convention
- Metadata JSON should map original vehicle wheels to new names
- Hash-based selection uses class to determine wheel pool
- LOD variants: add `_lod1`, `_lod2` suffix for distance-based switching
