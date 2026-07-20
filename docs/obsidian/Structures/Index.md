# Structures/Index

Complete GTA San Andreas game data structures with sizes and offsets.

## Overview

This document contains the complete data structure definitions for GTA San Andreas, reconstructed from reverse engineering efforts. These structures are essential for understanding the game's architecture and for developing mods that interact with the game's internals.

## Entity System

### CPlaceable
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x14 | `matrix` | Matrix pointer |

### CEntity
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x50 | Type | Entity type |
| 0x04 | 0x? | Status | Entity status |
| 0x08 | 0x? | Render CB | Render callback pointer |

### CPhysical
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x138 | Physics | Physics simulation data |
| 0x138 | 0x? | Collision | Collision information |

### CPed
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x7C0 | Health | Health points |
| 0x7C0 | 0x? | Armor | Armor points |
| 0x7C8 | 0x? | Weapons | Weapon info |

### CVehicle
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0xA18 | Engine health | Engine health |
| 0xA18 | 0x? | Handling | Handling data reference |

### CAutomobile
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherit CPed | - | - | - |

## Model System

### CBaseModelInfo
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| 0x00 | 0x? | Model ID | Unique model identifier |
| 0x04 | 0x? | Lod Distance | Level of detail distance |

### CVehicleModelInfo
| Offset | Size | Field | Description |
|--------|------|--------|-------------|
| Inherit CBaseModelInfo | - | - | - |
| 0x?? | 0x4 | handlingId | Handling data reference |
| 0x?? | 0x4 | vehicleType | Vehicle type enumeration |
| 0x?? | 0x4 | wheelModelId | Wheel model ID |
| 0x?? | 0x4 | wheelUpgradeClass | Wheel upgrade classification |
| 0x?? | 0x8 | handlingFlags | Handling flags |
| 0x?? | 0x8 | damageFlags | Damage flags |

## File Formats

### DFF (RenderWare Model)

**Structure:**
- Texture Dictionary
- Geometry List
  - Geometry (vertices, normals, UVs, material list)
- Atomic (geometry + frame + pipeline)
- Frame Hierarchy

### TXD (Texture Dictionary)

**Structure:**
- Texture List
  - Texture (raster data, mipmap levels)
- Native textures (platform-specific)

### IMG (Archive)

**Structure:**
- Directory (2400 entries max)
  - Entry (name, offset, size)
- File data (sector-aligned)

### IPL (Instance Pool)

**Structure:**
- Objects (position, rotation, model ID)
- Cars (position, rotation, model ID, color)
- Pickups
- Zones

### IDE (Item Definition)

**Structure:**
- OBJS (object definitions)
- TOBJ (timed objects)
- PATH (path nodes)
- TXDT (texture dictionaries)
- CULL (cull zones)

---

*Last Updated: 2026-06-30*
*Maintained: SKYGFXPLUS Team*