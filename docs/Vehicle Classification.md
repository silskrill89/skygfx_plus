# Vehicle Classification

#vehicles #classification

## Overview
Per-vehicle classification system for paint types, light tints, glass tints, tire properties, and wheel selection. Classification is based on vehicle model ID lookup from `vehicles.ide` and `handling.cfg` data files.

The system uses three independent classification axes:
- **Vehicle Group** — vehicle type/category (14 groups)
- **Vehicle Era** — production era (4 eras)
- **Vehicle Drive** — drivetrain type (3 types)

## Architecture

```
vehicles.ide + handling.cfg
        ↓
   vehicles.cpp (registry)
        ↓
   veh_shaders.cpp (bridge)
        ↓
   vehiclePipe.cpp (rendering)
```

## Vehicle Groups (14)

| Group | Enum | Model IDs | Notes |
|-------|------|-----------|-------|
| Standard | `VGROUP_STANDARD` | Default fallback | Common traffic |
| Sport | `VGROUP_SPORT` | 411, 415, 451, 480, 506, 541, 555, 560, 562, 565, 559, 558, 568, 429, 502 | Sports cars |
| Muscle | `VGROUP_MUSCLE` | 402, 475, 518, 439, 600, 542, 602, 502 | Muscle cars |
| Classic | `VGROUP_CLASSIC` | 466, 467, 567, 576, 575, 518, 447, 527, 419 | Pre-1980s style |
| Lowrider | `VGROUP_LOWRIDER` | 534, 535, 536, 567, 576 | Lowriders |
| Luxury | `VGROUP_LUXURY` | 405, 421, 492, 517, 551 + `richfamily`/`executive` class | Luxury vehicles |
| Truck | `VGROUP_TRUCK` | 489, 490, 505, 400, 579, 470, 554, 422, 478 + `worker`+`truck` anims | Trucks and SUVs |
| Van | `VGROUP_VAN` | 483, 609 + `van` anims | Vans |
| Utility | `VGROUP_UTILITY` | 530, 572, 532 | Utility vehicles |
| Emergency | `VGROUP_EMERGENCY` | 596, 597, 598, 427, 490, 528, 599, 433, 432, 407, 416 | Emergency vehicles |
| Military | `VGROUP_MILITARY` | 433, 432, 470 | Military vehicles |
| Bike | `VGROUP_BIKE` | 461, 462, 463, 468, 521, 522, 581, 448, 509, 510, 481 | Bicycles and motorcycles |
| Boat | `VGROUP_BOAT` | 472, 473, 493, 595, 484 | Boats |
| Aircraft | `VGROUP_AIRCRAFT` | 592, 577, 511, 512, 593, 520, 553, 476, 519, 460, 513 | Aircraft |

## Vehicle Eras (4)

| Era | Enum | Characteristics | Examples |
|-----|------|-----------------|----------|
| Pre-1980s | `VERA_PRE80` | Yellow sealed-beam headlights, orangey taillights | Glendale, Oceanic, Clover, Buccaneer |
| 1980s | `VERA_80S` | Warm halogen headlights, standard red taillights | Most default vehicles |
| 1990s | `VERA_90S` | Clear white headlights, deep red taillights | Sultan, Elegy, Jester, Infernus, Turismo |
| Utility | `VERA_UTILITY` | Amber headlights, utilitarian red taillights | Forklift, Tractor, Combine Harvester |

## Vehicle Drive Types (3)

| Drive | Enum | Handling.cfg Value |
|-------|------|-------------------|
| RWD | `VDRIVE_RWD` | `R` (default) |
| FWD | `VDRIVE_FWD` | `F` |
| AWD | `VDRIVE_AWD` | `4` |

## Paint Type System (5 types)

Paint types are selected per-vehicle using hash-based deterministic randomization.

| Type | Index | Specular | Glossiness | Description |
|------|-------|----------|------------|-------------|
| Gloss | 0 | 0.06 | 0.90 | Clear coat, mirror-smooth |
| Metallic | 1 | 0.70 | 0.88 | Metallic flake, shiny |
| Matte | 2 | 0.04 | 0.30 | Flat, no reflection |
| Satin | 3 | 0.05 | 0.60 | Semi-gloss |
| Clearcoat | 4 | 0.06 | 0.95 | Grid-style, very smooth |

**Paint weights by vehicle group** (`veh_shaders.cpp`):

| Group | Gloss | Metallic | Matte | Satin | Clearcoat |
|-------|-------|----------|-------|-------|-----------|
| Standard | 35% | 30% | 10% | 10% | 15% |
| Sport | 15% | 25% | 5% | 5% | 50% |
| Muscle | 25% | 35% | 10% | 10% | 20% |
| Classic | 40% | 25% | 10% | 10% | 15% |
| Lowrider | 15% | 40% | 5% | 15% | 25% |
| Luxury | 15% | 35% | 2% | 5% | 43% |
| Truck | 35% | 20% | 20% | 10% | 15% |
| Van | 40% | 15% | 20% | 10% | 15% |
| Utility | 40% | 10% | 30% | 10% | 10% |
| Emergency | 50% | 15% | 10% | 10% | 15% |
| Military | 25% | 10% | 45% | 10% | 10% |
| Bike | 25% | 30% | 10% | 10% | 25% |
| Boat | 35% | 20% | 15% | 10% | 20% |
| Aircraft | 30% | 20% | 25% | 10% | 15% |

## Light Tints by Era

| Era | Headlight RGB | Taillight RGB |
|-----|---------------|---------------|
| Pre-1980s | (1.00, 0.82, 0.45) — yellow sealed beam | (1.00, 0.35, 0.10) — orangey |
| 1980s | (1.00, 0.90, 0.65) — warm halogen | (1.00, 0.22, 0.10) — standard red |
| 1990s | (1.00, 0.97, 0.92) — clear white | (0.90, 0.05, 0.04) — deep red |
| Utility | (1.00, 0.85, 0.55) — amber | (1.00, 0.30, 0.12) — utilitarian |

## Glass Tint System

| Class | RGB | Strength | Vehicles |
|-------|-----|----------|----------|
| Default | (0.18, 0.19, 0.22) | 0.15 | Most vehicles |
| Cop | (0.06, 0.07, 0.10) | 0.50 | Police cars (596, 597, 598, 427, 490, 528) |
| Taxi | (0.35, 0.28, 0.15) | 0.30 | Taxis (420, 438) |
| FWD | (0.10, 0.22, 0.30) | 0.25 | Front-wheel drive vehicles |

## Tire Properties by Era

| Era | Specular | Glossiness | Tint RGB |
|-----|----------|------------|----------|
| Pre-1980s | 0.04 | 0.10 | (0.08, 0.08, 0.08) — worn |
| 1980s | 0.04 | 0.12 | (0.06, 0.06, 0.06) — standard |
| 1990s | 0.04 | 0.15 | (0.05, 0.05, 0.05) — newer, darker |
| Utility | 0.04 | 0.08 | (0.10, 0.09, 0.08) — off-road |

## Texture Name Detection

`veh_shaders.cpp` identifies mesh types from texture names:

| Surface Type | Detection Patterns |
|-------------|-------------------|
| Chrome | `chrome`, `bumper_chrome` |
| Tire | `tire`, `tyre`, `wheel_rubber` |
| Wheel | `wheel`, `alloy`, `rim` |
| Headlight | `headlight`, `light_front` |
| Taillight | `taillight`, `light_rear` |
| Carbon | `carbon` |
| Leather | `leather` |
| Fabric | `fabric`, `seat` |
| Glass | `windscreen`, `window`, `glass` |
| Plastic | `trim`, `plastic_interior` |
| Rubber | `rubber_seal`, `seal` |
| Dirt | `vehiclegrunge`, `grunge`, `dirt` |
| Rust | `rust` |
| Body | *(default for unknown vehicle textures)* |

## Area-Based Color Saturation

GTA SA map zones filter vehicle colors by saturation:

| Area | Min Saturation | Max Brightness | Region |
|------|---------------|----------------|--------|
| Default | 0.15 | 0.85 | Standard LA |
| Richman | 0.30 | 0.95 | Northwest hills |
| Rodeo | 0.40 | 1.00 | Beverly Hills |
| Hollywood | 0.55 | 1.00 | North-central |
| Industrial | 0.05 | 0.80 | East/south |

## See Also
- [[Glass Shader]] — Tint rendering details
- [[Vehicle Pipeline]] — All pipe modes
- [[INI Configuration]] — Related config keys
