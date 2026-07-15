---
tags: [wheels, hash, shared-pool]
created: 2025-01-02
updated: 2026-07-15
---

# Wheel System Architecture

> [!info] Full documentation
> See `docs/wheel_lod_system.md`, `docs/wheel_naming_convention.md`, `docs/wheel_texture_atlas.md` for wheel system docs.

## VC-style shared wheels + hash-based selection

### GTA SA wheels.DFF Structure (Analyzed)
```
models/generic/wheels.DFF (129,843 bytes)
├── Clump (20 atomics, 31 frames, 20 geometries)
│   ├── Frame[0]: 'Group01' (root, parent=-1)
│   │   ├── Frame[1]: 'wheel_smallcar' (parent=0)
│   │   │   ├── Frame[29]: 'wheel_smallcar_l1' → Atomic → Geom[0]
│   │   │   └── Frame[30]: 'wheel_smallcar_l0' → Atomic → Geom[1]
│   │   ├── Frame[2]: 'wheel_offroad' (parent=0)
│   │   │   ├── Frame[27]: 'wheel_offroad_l1' → Atomic → Geom[2]
│   │   │   └── Frame[28]: 'wheel_offroad_l0' → Atomic → Geom[3]
│   │   ├── Frame[3]: 'wheel_truck' (parent=0)
│   │   │   ├── Frame[25]: 'wheel_truck_l1' → Atomic → Geom[4]
│   │   │   └── Frame[26]: 'wheel_truck_l0' → Atomic → Geom[5]
│   │   ├── Frame[4]: 'wheel_rim' (parent=0)
│   │   │   ├── Frame[23]: 'wheel_rim_l1' → Atomic → Geom[6]
│   │   │   └── Frame[24]: 'wheel_rim_l0' → Atomic → Geom[7]
│   │   ├── Frame[5]: 'wheel_alloy' (parent=0)
│   │   │   ├── Frame[21]: 'wheel_alloy_l1' → Atomic → Geom[8]
│   │   │   └── Frame[22]: 'wheel_alloy_l0' → Atomic → Geom[9]
│   │   ├── Frame[6]: 'wheel_lightvan' (parent=0)
│   │   │   ├── Frame[19]: 'wheel_lightvan_l1' → Atomic → Geom[10]
│   │   │   └── Frame[20]: 'wheel_lightvan_l0' → Atomic → Geom[11]
│   │   ├── Frame[7]: 'wheel_lighttruck' (parent=0)
│   │   │   ├── Frame[17]: 'wheel_lighttruck_l1' → Atomic → Geom[12]
│   │   │   └── Frame[18]: 'wheel_lighttruck_l0' → Atomic → Geom[13]
│   │   ├── Frame[8]: 'wheel_classic' (parent=0)
│   │   │   ├── Frame[15]: 'wheel_classic_l1' → Atomic → Geom[14]
│   │   │   └── Frame[16]: 'wheel_classic_l0' → Atomic → Geom[15]
│   │   ├── Frame[9]: 'wheel_saloon' (parent=0)
│   │   │   ├── Frame[13]: 'wheel_saloon_l1' → Atomic → Geom[16]
│   │   │   └── Frame[14]: 'wheel_saloon_l0' → Atomic → Geom[17]
│   │   └── Frame[10]: 'wheel_sport' (parent=0)
│   │       ├── Frame[11]: 'wheel_sport_l1' → Atomic → Geom[18]
│   │       └── Frame[12]: 'wheel_sport_l0' → Atomic → Geom[19]
```

### 10 Wheel Styles (SA stock)
| Style | Frame Name | Description |
|-------|-----------|-------------|
| 0 | wheel_smallcar | Small compact wheels |
| 1 | wheel_offroad | Off-road knobby tires |
| 2 | wheel_truck | Heavy-duty truck wheels |
| 3 | wheel_rim | Basic steel rims |
| 4 | wheel_alloy | Alloy wheels |
| 5 | wheel_lightvan | Light van wheels |
| 6 | wheel_lighttruck | Light truck wheels |
| 7 | wheel_classic | Classic car wheels |
| 8 | wheel_saloon | Sedan/saloon wheels |
| 9 | wheel_sport | Sport/performance wheels |

### How SA Actually Works
1. **Each vehicle DFF has wheels baked in** as atomics with frame names `wheel_lf`, `wheel_rf`, `wheel_lr`, `wheel_rr`
2. **vehicles.ide** `wheelModel` field:
   - `-1` = use baked-in wheels from vehicle DFF (most SA vehicles)
   - `0-9` = use shared wheels.DFF (only some vehicles)
3. **TransFender upgrades** change `m_nWheelModelIndex` to swap to shared wheels.DFF
4. **Most SA vehicles have UNIQUE wheel meshes** baked into their DFF — not from the shared pool
5. **`m_nWheelUpgradeClass`** determines which wheel set is available at TransFender
6. **`m_fWheelSizeFront/Rear`** from vehicles.ide sets wheel scale

### Plan: Extract + Extend + Replace

#### Phase 1: Extract Baked Wheels
1. ✅ Parse wheels.DFF — found 10 styles × 2 LODs
2. ✅ Analyze DFF binary format
3. Extract wheel atomics (frame=`wheel_lf/rf/lr/rr`) from ALL vehicle DFFs
4. Each extracted wheel = one atomic with geometry + materials
5. Tag each with source vehicle name (e.g., `wheel_cadrona_lf`)
6. Group: standard wheels (4 per car), special wheels (planes, bikes, etc.)
7. Create `extended_wheels.dff` with all extracted wheels

#### Phase 2: VC-style Shared System
1. Extended wheels.dff contains:
   - SA's 10 original styles (smallcar, offroad, truck, rim, alloy, lightvan, lighttruck, classic, saloon, sport)
   - All unique wheels extracted from vehicle DFFs (wheel_cadrona, wheel_infernus, etc.)
   - Special wheels (plane landing gear, bike wheels, etc.) in last groups
2. Hash-based wheel selector per vehicle class
3. Keep baked-in wheels for legacy (optional strip later)
4. TransFender uses extended pool

#### Phase 3: Strip + Replace
1. Strip wheel meshes from vehicle DFFs (optional)
2. All wheels from shared extended_wheels.dff
3. Hash-based selection with more variety
4. Per-vehicle wheel overrides via config

### Hash-Based Selection Pattern (Same as Paint/Tire)
```cpp
unsigned int hash = modelID * 2654435761u;
float r = (float)(hash & 0xFFFF) / 65535.0f;
// Select wheel class based on vehicle group weights
// Then select specific wheel within class
```

### Vehicle Group → Wheel Class Mapping
| Vehicle Group | Sport | Muscle | SUV | Sedan | Bike | Truck | Lowrider | Tuner |
|--------------|-------|--------|-----|-------|------|-------|----------|-------|
| Standard     | 5%    | 10%    | 10% | 50%   | 0%   | 10%   | 5%       | 10%   |
| Sport        | 50%   | 15%    | 0%  | 5%    | 0%   | 0%    | 0%       | 30%   |
| Muscle       | 10%   | 50%    | 5%  | 10%   | 0%   | 5%    | 10%      | 10%   |
| Lowrider     | 0%    | 5%     | 0%  | 10%   | 0%   | 0%    | 75%      | 10%   |
| Truck        | 0%    | 5%     | 40% | 10%   | 0%   | 40%   | 0%       | 5%    |
| Bike         | 0%    | 0%     | 0%  | 0%    | 100% | 0%    | 0%       | 0%    |

## Related
- [[Weather System Architecture]] — similar hash-based selection pattern
- [[SkyGFX Pipeline Overview]] — vehicle rendering pipeline
- [[Wheel Extender Technical Plan]] — Extended wheel system details
