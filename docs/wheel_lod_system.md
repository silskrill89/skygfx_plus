# Wheel LOD System

## Status: Planned / Not Yet Implemented

> **Note**: This document describes the planned LOD system. The current implementation in `wheels.cpp` does not perform distance-based LOD switching. Wheels are loaded from DFF files and selected via hash-based class matching, but all wheels render at a single detail level.

## Overview
Wheels will use distance-based Level of Detail (LOD) switching to optimize performance. Three LOD levels based on camera distance from wheel.

## SA Stock Wheel Structure

GTA SA's `models/generic/wheels.DFF` contains 10 wheel styles, each with 2 LODs:
- `_l0` = LOD0 (high detail)
- `_l1` = LOD1 (low detail)

Frame hierarchy:
```
Group01
├── wheel_smallcar → _l0, _l1
├── wheel_offroad → _l0, _l1
├── wheel_truck → _l0, _l1
├── wheel_rim → _l0, _l1
├── wheel_alloy → _l0, _l1
├── wheel_lightvan → _l0, _l1
├── wheel_lighttruck → _l0, _l1
├── wheel_classic → _l0, _l1
├── wheel_saloon → _l0, _l1
└── wheel_sport → _l0, _l1
```

## Planned LOD Levels

### LOD0 (High Detail)
- **Distance**: 0-15 meters
- **Geometry**: Full detail (500-2000 triangles)
- **Textures**: 512x512 or higher
- **Features**: Full spoke detail, bolt/nut geometry, brake disc, tire tread

### LOD1 (Medium Detail)
- **Distance**: 15-40 meters
- **Geometry**: Reduced detail (200-500 triangles)
- **Textures**: 256x256
- **Features**: Simplified spokes, no bolts, simplified brake disc

### LOD2 (Low Detail)
- **Distance**: 40+ meters
- **Geometry**: Minimal (50-100 triangles)
- **Textures**: 128x128
- **Features**: Basic circle for rim, simple tire cylinder

## Planned DFF Structure

Each extended wheel DFF would contain multiple atomics with LOD suffixes:
```
wheel_sport_01_uni         (LOD0 - high detail)
wheel_sport_01_uni_lod1    (LOD1 - medium detail)
wheel_sport_01_uni_lod2    (LOD2 - low detail)
```

## Current Implementation

`wheels.cpp` implements:
- Hash-based wheel selection per vehicle class (`Wheels_SelectForVehicle`)
- Wheel class weights per vehicle group
- Loading wheel metadata from `wheels_meta.json`
- Wheel classification by vehicle name heuristic

`wheels_extender.cpp` implements:
- Loading wheel DFFs from a configurable directory
- Hash-based wheel class selection
- Configuration via `wheels_extender.ini`
- Render hook (geometry swap disabled — causes crash mid-frame)

Neither file implements distance-based LOD switching yet.

## See Also
- [[Wheel System Architecture]] — Full wheel system overview
- [[Wheel Extender Technical Plan]] — Extender implementation plan
- [[wheel_naming_convention.md]] — Naming conventions for wheel DFFs
