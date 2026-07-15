# Future Features

## Roadmap Phases

See [[Roadmap to Ultimate Mod]] for the full development roadmap.

### Phase 1: Restore Full Compatibility
- Verify all 9 aap car pipes work
- Verify all 2 aap building pipes work
- Verify all junior features (stochastic, wind, radiosity, trails, grading)
- Fix dead code stubs (UploadUnifiedConstants, wheel extender, env map normals)
- Restore YCbCr correction from junior fork

### Phase 2: Platform/Game Style Selection
- vehicleStyle = PS2 | Xbox | PC | Mobile | GTAIV | Modern
- buildingStyle = PS2 | Xbox | GTAIV | PBR
- colorStyle = PS2 | PC | Mobile | III | VC | VCS | GTAIV | Modern
- Per-pipe quality sliders
- Hook into GTA SA's internal settings menu (CMenuManager)
- INI + debug menu + in-game menu support

### Phase 3: GTA V/IV-Style Settings Menu
- Quality Preset (Low/Medium/High/Very High/Ultra)
- Individual feature toggles
- Per-pipe blend controls
- ImGui-based overlay (existing debugmenu_ui.cpp)

### Phase 4: Forward+ Unified Pipeline
- Complete unifiedPipe.hlsl with all 4 passes
- G-buffer: depth, normal, albedo, material properties
- Tiled forward lighting (8-iteration dynamic light loop)
- IBL with pre-computed environment maps

### Phase 5: SM3.0 Feature Fit
Most features are already implemented (see [[Implemented Features]]). Remaining:
- Wire skin/hair/vegetation enhancement passes into main pipeline
- Wire normal buffer into building pipe for enhanced AO

### Phase 6: Multi-Agent Development Plan
- Parallel feature development
- Dependency mapping
- Agent assignments

## Deferred (Not in Current Roadmap)

### Normal Map Plugin Integration
- **Status:** Deferred
- **Reason:** Requires DK22Pac normalmap SDK, complex dependencies
- **Notes:** Code exists in `src/rw/normalmap.cpp` but excluded from build

### Edge Tessellation
- **Status:** Experimental
- **Reason:** Can cause visual artifacts, needs more work
- **Notes:** Shader exists (`EdgeTessellationVS.hlsl`) but disabled in config

### Multi-pass Vehicle Glass with Parallax
- **Status:** Partially implemented
- **Notes:** Glass shader exists, needs POM integration for lens details

### Collision-based Edge Detection
- **Status:** Not started
- **Notes:** Would improve SMAA edge detection using collision geometry

## Potential Future Work
- Cloud bleeding on trees fix (IBL buffer projecting onto vegetation)
- Ray tracing (needs DXR — out of scope for SM3.0)
- Volumetric fog (simplified version possible)

## See Also
- [[Implemented Features]] — What's done
- [[Build System]] — How to build
- [[Roadmap to Ultimate Mod]] — Full roadmap
- [[File Inventory]] — Source file listing
