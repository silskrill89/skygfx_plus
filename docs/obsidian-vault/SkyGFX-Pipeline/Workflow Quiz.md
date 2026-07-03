# SkyGFX Plus — Workflow Quiz
# 20 questions, 2 answers each. Pick A or B.

---

## Q1: Build & Deploy
A. Always deploy ASI to game folder immediately after every build
B. Only deploy when explicitly told to deploy

## Q2: Shaders
A. Compile shaders manually with fxc.exe from SDK
B. Auto-compile via fix_build.py during build

## Q3: Timecycle Data
A. Use PS2 timecyc values directly (saturated, punchy)
B. Use PC timecyc values (desaturated, gamma-corrected)

## Q4: Sky System
A. DynamicSky shader computes everything procedurally
B. Use GTA SA's native sky system + timecycle blending

## Q5: PBR Lighting
A. Full PBR with GGX/Smith/Schlick in shaders
B. Baked vertex lighting + shader refinement

## Q6: Vehicle Rendering
A. One unified PBR shader for all vehicles
B. Keep legacy pipes + PBR as separate paths

## Q7: Building Rendering
A. One PBR building pipe for all buildings
B. Keep PS2/Xbox pipes + PBR as separate paths

## Q8: Water
A. Procedural Gerstner waves + PBR Fresnel
B. Screen-space reflections + depth buffer

## Q9: Weather System
A. Hash-based weather selection from multiple timecycs
B. Single timecyc with manual overrides

## Q10: Wheel System
A. Shared wheel DFF pool + hash-based class selection
B. Per-vehicle baked-in wheels only

## Q11: INI Config
A. Fresh INI deployed every build with all features ON
B. Preserve existing INI, only add new fields

## Q12: Debug Menu
A. ImGui-based debug menu (F4)
B. CLEO-based debug menu

## Q13: Memory Management
A. 64-bit bridge for scripts/AI (dedicated 4GB)
B. Optimize existing 32-bit address space usage

## Q14: Texture Formats
A. DXT1/DXT5 compressed textures only
B. Support all D3D9 formats (A8R8G8B8, etc.)

## Q15: Normal Mapping
A. Parallax occlusion mapping for all surfaces
B. Standard normal mapping only

## Q16: PostFX Chain
A. SMAA → SSAO → SSS → Grading → Tonemapping
B. FXAA → SSAO → Grading → Tonemapping

## Q17: Audio
A. Keep original SA audio system
B. Enhanced audio with spatial/3D effects

## Q18: Map Streaming
A. Keep original IMG streaming
B. Custom streaming with memory pool optimization

## Q19: CLEO/Scripting
A. Keep CLEO compatibility + add Lua scripting
B. Replace CLEO with custom scripting system

## Q20: Release
A. Single ASI with all features enabled
B. Separate ASIs per feature (modular)
