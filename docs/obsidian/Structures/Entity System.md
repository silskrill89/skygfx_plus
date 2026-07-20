# Structures/Entity System

CPlaceable, CEntity, CBuilding, CPhysical hierarchy definitions for GTA San Andreas.

## CPlaceable

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x14 | `matrix` | Matrix pointer |

## CEntity

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x50 | Type | Entity type |
| 0x04 | 0x? | Status | Entity status |
| 0x08 | 0x? | Render CB | Render callback pointer |

## CBuilding

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CEntity | - | - | - |

## CDummy

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CEntity | - | - | - |

## CPhysical

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x138 | Physics | Physics simulation data |
| 0x138 | 0x? | Collision | Collision information |

## CPed

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x7C0 | Health | Health points |
| 0x7C0 | 0x? | Armor | Armor points |
| 0x7C8 | 0x? | Weapons | Weapon information |

### CPedIntelligence

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x? | Current node | Current AI path node |
| 0x04 | 0x? | Destination | Movement destination |

### CPlayerData

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x? | Player ID | Unique player identifier |
| 0x04 | 0x? | Gang | Current gang affiliation |

## CVehicle

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0xA18 | Engine health | Engine health |
| 0xA18 | 0x? | Handling | Handling data reference |

### CAutomobile

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CVehicle | - | - | - |

### CBike

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CVehicle | - | - | - |

### CBoat

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CVehicle | - | - | - |

### CTrain

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CVehicle | - | - | - |

### CHeli

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CVehicle | - | - | - |

## CObject

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x? | Model ID | Model identifier |
| 0x04 | 0x? | Velocity | Movement velocity |
| 0x0C | 0x? | Position | World position |

## CCutsceneObject

| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherits CEntity | - | - | - |

---

*Last Updated: 2026-06-30*
*Maintained: SKYGFXPLUS Team*