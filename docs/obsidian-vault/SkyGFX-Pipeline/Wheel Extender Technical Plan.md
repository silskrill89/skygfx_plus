---
tags: [wheels, technical-plan, transfender]
created: 2025-01-02
updated: 2026-07-15
---

# Wheel Extender — Technical Plan

## Current SA Wheel System

### wheels.DFF (models/generic/wheels.DFF)
- 10 wheel styles × 2 LODs = 20 atomics
- Styles: smallcar, offroad, truck, rim, alloy, lightvan, lighttruck, classic, saloon, sport
- Frame hierarchy: Group01 → style → _l0 (LOD0) + _l1 (LOD1)

### Individual Wheel DFFs in gta3.img
- 17 files: wheel_gn1-5, wheel_lr1-5, wheel_or1, wheel_sr1-6
- These are the TransFender upgrade wheels
- Separate from wheels.DFF — loaded on demand

### Wheel Upgrade System
```
ms_numWheelUpgrades[4]     — 4 upgrade classes (short array)
ms_upgradeWheels[15][4]    — 15 wheels per class, 4 classes (short array)
```

**Functions:**
| Function | Address | Purpose |
|----------|---------|---------|
| `AddWheelUpgrade(class, model)` | `0x4C8700` | Register wheel model to upgrade class |
| `GetNumWheelUpgrades(class)` | `0x4C8740` | Get count of wheels in class |
| `GetWheelUpgrade(class, index)` | `0x4C8750` | Get wheel model ID from class |

**Vehicle fields:**
- `m_nWheelModelIndex` (short, offset 0x48) — current wheel model (-1 = baked)
- `m_nWheelUpgradeClass` (byte, offset 0x4F) — upgrade class for TransFender
- `m_fWheelSizeFront` / `m_fWheelSizeRear` — wheel scale

### How Wheels Are Spawned
1. Vehicle DFF has wheel dummy frames: `wheel_lf_dummy`, `wheel_rf_dummy`, `wheel_lr_dummy`, `wheel_rb_dummy`
2. Each dummy frame has child atomic (the wheel mesh)
3. If `m_nWheelModelIndex >= 0`, game loads wheel from wheels.DFF or individual DFFs
4. If `m_nWheelModelIndex == -1`, uses baked-in wheel from vehicle DFF
5. TransFender changes `m_nWheelModelIndex` to swap wheels

## VC Wheel System (from re3-miami)

### Key Differences from SA
- VC has 4 upgrade classes × 15 wheels = 60 total
- SA has 4 upgrade classes × 15 wheels = 60 total (same!)
- VC: `m_wheelId` = wheel model ID from vehicles.ide
- SA: `m_nWheelModelIndex` = same concept
- VC: `ms_upgradeWheels[NUM_WHEEL_UPGRADE_CLASSES][NUM_WHEEL_UPGRADES]`
- SA: `ms_upgradeWheels[15][4]` (same structure)

### VC VehicleModelInfo Wheel Functions
```cpp
void SetWheels(int32 id, float scale, float scaleRear);
int32 GetWheelModelId();
void SetWheelUpgradeClass(int8 wheelClass);
int32 GetWheelUpgradeClass();
static void AddWheelUpgrade(int32 wheelClass, int32 model);
static int32 GetNumWheelUpgrades(int32 wheelClass);
static int32 GetWheelUpgrade(int32 wheelClass, int32 index);
```

## Plan: Extended Wheel System

### Phase 1: Extract + Combine
1. Extract wheels from SA wheels.DFF (10 styles × 2 LODs)
2. Extract individual wheel DFFs from gta3.img (17 files)
3. Extract wheels from VC wheels.DFF
4. Deduplicate by geometry hash
5. Combine into single extended wheels.dff

### Phase 2: Extend Limits
Hook these functions to use our own larger arrays:
```cpp
// Original: 4 classes × 15 wheels
// Extended: 4 classes × 256 wheels

static short g_numWheelUpgrades[4] = {0};
static short g_upgradeWheels[256][4] = {0};

// Hook GetNumWheelUpgrades to return from our array
int Hooked_GetNumWheelUpgrades(int wheelClass) {
    return g_numWheelUpgrades[wheelClass];
}

// Hook GetWheelUpgrade to return from our array
int Hooked_GetWheelUpgrade(int wheelClass, int index) {
    return g_upgradeWheels[index][wheelClass];
}

// Hook AddWheelUpgrade to add to our array
void Hooked_AddWheelUpgrade(int wheelClass, int modelId) {
    int idx = g_numWheelUpgrades[wheelClass];
    if (idx < 256) {
        g_upgradeWheels[idx][wheelClass] = modelId;
        g_numWheelUpgrades[wheelClass]++;
    }
}
```

### Phase 3: TransFender Grid UI
- Same UI as color picker — grid of wheel thumbnails
- Left/right arrow to cycle through available wheels
- Shows wheel name, class, and preview
- Player selects → `m_nWheelModelIndex` updated

### Phase 4: Hash-Based Default + Override
- Default: hash-based selection per vehicle class (from veh_shaders pattern)
- Override: player can pick any wheel from the extended pool
- Persist selection in save file (optional)

## Key Addresses (SA v1.0 US)
| Address | Type | Purpose |
|---------|------|---------|
| `0x4C8700` | Function | `AddWheelUpgrade(class, model)` |
| `0x4C8740` | Function | `GetNumWheelUpgrades(class)` |
| `0x4C8750` | Function | `GetWheelUpgrade(class, index)` |
| `0x7323C0` | Function | `CVisibilityPlugins::RenderWheelAtomicCB` |
| `0xC8B4C0` | Data | `ms_numWheelUpgrades[4]` (short array) |
| `0xC8B4C8` | Data | `ms_upgradeWheels[15][4]` (short array) |

## Related
- [[SkyGFX Pipeline Overview]] — rendering pipeline
- [[Wheel System Architecture]] — Wheel system overview
- [[Weather System Architecture]] — similar hash-based selection pattern
