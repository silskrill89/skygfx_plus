# Grand Theft Auto: San Andreas Modding Wiki

A comprehensive modding wiki for GTA San Andreas, focusing on the SKYGFXPLUS rendering enhancement project built on RenderWare 3.7.

## Overview

This wiki documents the technical aspects of modding GTA: San Andreas, with particular emphasis on the SKYGFXPLUS project's approach to modernizing the game's rendering pipeline while maintaining compatibility with existing plugins (SilentPatch, modloader, moonloader, CLEO, etc.).

## Main Topics

### 1. PS2 Emulation Infrastructure
Documentation of PS2 hardware rendering techniques including GS register references, DMA/VU1 microprograms, and specific emulation challenges for the PS2 GPU.

### 2. Pipeline Architecture
Porting of GTA IV rendering techniques (SMAA, SSAO, FXAA, Forward+ rendering) to GTA San Andreas

### 3. Lighting & Normal Mapping
Integration of normal mapping support through a unified asi/dll approach

### 4. Model & Atomic Data Structures
Analysis of GTA SA's model system, including LOD handling and streaming

### 5. Shader System & Pipeline Porting
Porting of GTA IV's shader system to SM2.0/3.0 with custom register layouts

### 6. Crash Analysis & Debugging
Documentation of known crashes and debugging techniques used in SKYGFXPLUS

### 7. Build & Configuration
Build system documentation, premake5 configuration, and compatibility matrices

## External References

### GTA Wiki Sources
- Official GTA Wiki (gta.wiki/w/Grand_Theft_Auto:_San_Andreas)
- GTA San Andreas fan site (gta-sanandreas.com)
- GTA Wiki (gta.wiki) - the freedom wiki migration from Fandom

### Reverse Engineering Resources
- GTASource rendering code analysis
- RenderWare SDK documentation
- re3-reVC shader system references
- DK22Pac Plugin-SDK struct layouts

## Current Status

**Key Issues Remaining:**
1. Cannot fix trampoline at 0x5DA610 for proper normal mapping support
2. Need to integrate GTA IV shader system and normal mapping into single DLL
3. Need PS2 emulation foundation

**Build Status:** ✅ Build succeeds, crashes on launch due to trampoline issue

---

*Last Updated: 2026-06-30*
*Maintained: SKYGFXPLUS Team*