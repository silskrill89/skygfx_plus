# Future Features

#status #deferred #roadmap

## Deferred (Not in Current Roadmap)

### ImGui Debug Menu
- **Status:** Deferred
- **Reason:** Requires ImGui SDK integration, low priority vs rendering features
- **Dependencies:** ImGui library (E:\SDKs\imgui-master)
- **Notes:** Code exists in debugmenu_ui.cpp but excluded from build

### Normal Map Plugin Integration
- **Status:** Deferred
- **Reason:** Requires DK22Pac normalmap SDK, complex dependencies
- **Dependencies:** normalmap_byDK SDK (E:\SDKs\normalmap_byDK_1.01)
- **Notes:** Code exists in normalmap.cpp/normalmap_plugin.cpp but excluded from build

### Edge Tessellation
- **Status:** Experimental
- **Reason:** Can cause visual artifacts, needs more work
- **Notes:** Shader exists (EdgeTessellationVS.hlsl) but disabled in config

### Multi-pass Vehicle Glass with Parallax
- **Status:** Partially implemented
- **Notes:** Glass shader exists, needs POM integration for lens details

### Collision-based Edge Detection
- **Status:** Not started
- **Notes:** Would improve SMAA edge detection using collision geometry

## Potential Future Work
- Cloud bleeding on trees fix (IBL buffer projecting onto vegetation)
- Wire skin/hair/vegetation enhancement passes into pipeline
- Wire normal buffer into building pipe for enhanced AO
- Forward+ pipeline migration

## See Also
- [[Implemented Features]] — What's done
- [[Build System]] — How to build
