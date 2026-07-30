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

## Ragdoll Physics Restoration

**Status:** Planned — roadmap created
**Plan:** `docs/plans/2026-07-30-ragdoll-restoration.md`
**Approach:** RW SDK bone-node-based (NOT RAGE/NaturalMotion)

### Phase 1: MVP (Basic Ragdoll)
- Create `Ragdoll.h` class declarations
- Extend BoneNode_c with velocity + keyframe quaternion (wrapper struct)
- Hard-code `ms_boneInfos[32]` bone hierarchy data table
- Hard-code `aBONETAG_ENUM_TAB[32]` lookup
- Add WRAPPER declarations for RpHAnim*/RtQuat* functions
- Implement `Ragdoll.cpp` from gtasa_src-main prototype (fix: process ALL bones, fix friction)

### Phase 2: Polish
- Ground collision (bone positions vs terrain)
- Joint limit enforcement (rotation clamping per bone)
- Angular velocity damping

### Phase 3: Integration
- Init in `InjectDelayedPatches()` after pipe hooks
- Update in game loop (`CGame::Process` or ped update)
- Trigger on ped death (`CPed::SetDie` hook)
- Blend-out after 3-5 seconds

### Phase 4: Enhancement
- Vehicle impact forces
- Weapon knockback
- Pose correction (from RAGE reference)
- Writhe simulation

### Source Availability
- BoneNode_c/OpenSA: COMPLETE (65+232 lines)
- IKChain_c/OpenSA: COMPLETE (71+297 lines)
- Ragdoll.cpp/gtasa_src-main: COMPLETE but commented out (435 lines)
- MISSING: Ragdoll.h, bone data table, HAnim/Quat WRAPPER declarations

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
