#!/usr/bin/env python3
"""
memory-optimize.py — Compact Index Generator for 5-Layer Memory System

Generates pre-computed indexes that reduce query costs to ~200 tokens each.

Usage:
    python tools/memory-optimize.py all        # Generate all indexes
    python tools/memory-optimize.py agent      # Agent memory compact index
    python tools/memory-optimize.py codebase   # Codebase graph compact index
    python tools/memory-optimize.py vault      # Obsidian vault compact index
    python tools/memory-optimize.py compress   # Generate compressed conversation templates
"""

import json
import os
import sys
from pathlib import Path
from datetime import datetime

PROJECT_ROOT = Path(__file__).parent.parent
MEMORY_BANK = PROJECT_ROOT / "memory-bank"
CACHE_DIR = MEMORY_BANK / ".cache"

# Agent Memory Compact Index
AGENT_INDEX = {
    "patterns": {
        "smaa-black-screen": {
            "symptoms": "SMAA completely black, blur shows UI",
            "root-cause": "D3D9 RT still bound after DrawSMAA EndUpdate",
            "fix-attempted": "SetRenderTarget(NULL) before inline copy — failed",
            "next-steps": "Trace SMAA shader output or source raster state",
            "file": "src/render/postfx.cpp:3051-3301"
        },
        "ssao-auto-disable": {
            "symptoms": "SSAO checkbox auto-disables every frame",
            "root-cause": "DrawSSAO/DrawSSAO_Overhaul sets config->ssaoEnable=0 on error",
            "fix": "fix-13: Return silently instead of disabling",
            "file": "src/render/postfx.cpp"
        },
        "camera-state-machine": {
            "pattern": "BeginUpdate/EndUpdate must be balanced",
            "rule": "UpdateFrontBuffer leaves camera in BeginUpdate",
            "smaa-flow": "3 passes: EndUpdate→SetRaster→BeginUpdate each",
            "file": "src/render/postfx.cpp:1535-1801"
        },
        "pbr-params": {
            "c22-c23": "ALWAYS use pipeUploadPBR() — NEVER manual",
            "order": "{glossiness, specular, ...}",
            "consequence": "Manual upload produces flat/dark output"
        },
        "division-safety": {
            "pattern": "Guard all normalize() and 1.0/x",
            "epsilon": "1e-7 minimum",
            "consequence": "NaN propagates as black in SM3.0"
        }
    },
    "sessions": {
        "recent": ["fix-13", "gen-4", "exp-3", "exp-4", "ora-1", "ora-2"],
        "reusable": {
            "fixer": ["fix-8", "fix-13"],
            "explorer": ["exp-3", "exp-4"],
            "oracle": ["ora-1", "ora-2"],
            "librarian": ["lib-5", "lib-6"],
            "general": ["gen-2", "gen-4"]
        }
    },
    "bugs-fixed": [
        "NULL frame crash (RwFrameGetLTM)",
        "RwTexDictionaryFindNamedTexture hook corruption",
        "SilentPatch compatibility",
        "PDB contention (C1041)",
        "UI invisible (SMAA dirty state)",
        "SSAO checkbox auto-disable (fix-13)",
        "Vehicle tire rubber PBR mismatch",
        "Building PBR 5 cascading bugs"
    ]
}

# Codebase Graph Compact Index
CODEBASE_INDEX = {
    "key-functions": {
        "postfx": {
            "DrawSMAA": "postfx.cpp:3051-3301",
            "ColourFilter_switch": "postfx.cpp:1535-1801",
            "ColourFilter_Modern": "postfx.cpp:957-1100",
            "UpdateFrontBuffer": "postfx.cpp:241-285",
            "DrawSSAO_Overhaul": "postfx.cpp:2000-2200",
            "RenderIBLBuffer": "postfx.cpp:2670-2790"
        },
        "pipeline": {
            "CreateShaders": "pipelinecommon.cpp:1-100",
            "pipeUploadPBR": "pipelinecommon.cpp:300-400",
            "pipeUploadVehicle": "pipelinecommon.cpp:400-500"
        },
        "vehicle": {
            "DrawModernCarPBR": "vehiclePipe.cpp:1-100",
            "DrawPS2Car": "vehiclePipe.cpp:200-300",
            "DrawXboxCar": "vehiclePipe.cpp:400-500"
        },
        "building": {
            "main_building": "buildingPipe.cpp:1-100",
            "DrawBuildingPS2": "buildingPipe.cpp:200-300"
        },
        "main": {
            "DllMain": "main.cpp:1-50",
            "readIni": "main.cpp:100-300",
            "Pipeline_switch": "main.cpp:400-500"
        }
    },
    "call-chains": {
        "postfx-chain": "NormalBuffer→VelocityBuffer→SSAO_Overhaul→DrawPipeChain→ColourFilter_switch→MotionBlur→HeightFog→GodRays→UpdateFrontBuffer→SSS",
        "smaa-chain": "DrawSMAA(3 passes)→inline copy→ColourFilter_Modern",
        "pbr-chain": "pipeUploadPBR()→shader constants→DrawModernCarPBR"
    },
    "hooks": {
        "0x704D1E": "ColourFilter_switch",
        "0x53EBE9": "DrawFinalEffects",
        "0x5D7100": "Building pipe creation",
        "0x5D9FE9": "Vehicle pipe creation"
    },
    "registers": {
        "vs-c0-c3": "WVP matrix",
        "vs-c4": "ambient color",
        "vs-c5-c11": "7 direct light colors",
        "vs-c12-c18": "7 direct light directions",
        "vs-c19": "material color",
        "vs-c20": "surface properties",
        "vs-c24-c27": "world matrix",
        "vs-c29": "shader params (color scale)",
        "vs-c30-c31": "day/night params",
        "vs-c32-c35": "texture transform",
        "vs-c36": "eye position",
        "ps-c0": "surfProps {ambient,0,diffuse,prelit_flag}",
        "ps-c1": "fxParams {fresnel,shininess,specularity,lightmult}",
        "ps-c2": "eyePos",
        "ps-c3": "iblParams",
        "ps-c4": "cloudShadow",
        "ps-c22": "pbrParams — MUST use pipeUploadPBR()",
        "ps-c23": "paintNoise — MUST use pipeUploadPBR()",
        "ps-c24": "ambientColor"
    }
}

# Obsidian Vault Compact Index
VAULT_INDEX = {
    "docs": {
        "Home.md": "Quick start, features, source tree, key addresses",
        "Index.md": "Phase 0-3+F status, architecture, features, build",
        "Build Guide.md": "fast_build.py workflow (6 commands), troubleshooting",
        "Configuration Reference.md": "All INI settings, velocity buffer, forward+",
        "Debugging Guide.md": "9 common issues, SMAA black screen, SSAO",
        "Forward Plus Rendering.md": "PBR, light culling, performance",
        "Hardware Differences.md": "PS2 vs Xbox vs PC rendering",
        "Hook Architecture.md": "Game function hooks, addresses",
        "RenderWare SDK Architecture.md": "RW 3.6/3.7 integration",
        "D3D9 Shader Architecture.md": "Shader pipeline, compilation"
    },
    "postfx": {
        "PostEffects/Overview.md": "13 effects, status, PostFX chain",
        "PostEffects/SSAO.md": "Overhaul, fix-13, debugging",
        "PostEffects/SMAA.md": "Black screen, 3-pass, camera state",
        "PostEffects/Velocity Buffer.md": "V1, RG16F, motion vectors",
        "PostEffects/Colour Filter.md": "CMODERN, grading",
        "PostEffects/Radiosity.md": "Radiosity implementation"
    },
    "pipelines": {
        "Pipelines/Building Pipelines.md": "PS2/Xbox/PBR building rendering",
        "Pipelines/Vehicle Pipelines.md": "PS2/Xbox/PBR/modern vehicles",
        "Pipelines/Grass Rendering.md": "Grass shader pipeline"
    },
    "shaders": {
        "Shaders/Shader Reference.md": "All HLSL shaders, entries, registers"
    },
    "build": {
        "Build/Build Scripts.md": "fast_build.py, proto.py, tools",
        "Build/Python Tools.md": "42 scripts, categories, usage"
    },
    "structures": {
        "Structures/Entity System.md": "Game entity architecture",
        "Structures/ImGui Menu.md": "17 sections, Ctrl+4, menu tree"
    }
}

# Compressed Conversation Templates
CONVERSATION_TEMPLATES = {
    "smaa-debug": {
        "context": "SMAA black screen (postfx.cpp:3051-3301)",
        "known": "RT unbinding failed, camera state correct",
        "next": "Trace shader output or source raster state",
        "tokens": 50
    },
    "ssao-test": {
        "context": "SSAO fix-13 deployed (removed ssaoEnable=0)",
        "known": "3 error paths fixed, awaiting test",
        "next": "Test with fix-13, verify checkbox stays enabled",
        "tokens": 40
    },
    "forward-plus": {
        "context": "Forward+ CollectLights stub (forwardplus.cpp:58)",
        "known": "CPU-side 16×16 tiled light culling designed",
        "next": "Implement light culling algorithm",
        "tokens": 40
    },
    "build-deploy": {
        "context": "Build system ready",
        "known": "fast_build.py, MSBuild, FXC paths",
        "next": "python tools/fast_build.py --launch",
        "tokens": 30
    }
}

def generate_agent_index():
    """Generate compact agent memory index."""
    print("Generating agent memory compact index...")
    CACHE_DIR.mkdir(exist_ok=True)
    
    with open(CACHE_DIR / "agent-index.json", "w") as f:
        json.dump(AGENT_INDEX, f, indent=2)
    
    print(f"  Saved: {CACHE_DIR / 'agent-index.json'}")
    print(f"  Patterns: {len(AGENT_INDEX['patterns'])}")
    print(f"  Sessions: {len(AGENT_INDEX['sessions']['recent'])}")
    print(f"  Bugs fixed: {len(AGENT_INDEX['bugs-fixed'])}")

def generate_codebase_index():
    """Generate compact codebase graph index."""
    print("Generating codebase graph compact index...")
    CACHE_DIR.mkdir(exist_ok=True)
    
    with open(CACHE_DIR / "codebase-index.json", "w") as f:
        json.dump(CODEBASE_INDEX, f, indent=2)
    
    print(f"  Saved: {CACHE_DIR / 'codebase-index.json'}")
    print(f"  Key functions: {sum(len(v) for v in CODEBASE_INDEX['key-functions'].values())}")
    print(f"  Call chains: {len(CODEBASE_INDEX['call-chains'])}")
    print(f"  Hooks: {len(CODEBASE_INDEX['hooks'])}")
    print(f"  Registers: {len(CODEBASE_INDEX['registers'])}")

def generate_vault_index():
    """Generate compact vault index."""
    print("Generating vault compact index...")
    CACHE_DIR.mkdir(exist_ok=True)
    
    with open(CACHE_DIR / "vault-index.json", "w") as f:
        json.dump(VAULT_INDEX, f, indent=2)
    
    print(f"  Saved: {CACHE_DIR / 'vault-index.json'}")
    print(f"  Docs: {len(VAULT_INDEX['docs'])}")
    print(f"  PostFX: {len(VAULT_INDEX['postfx'])}")
    print(f"  Pipelines: {len(VAULT_INDEX['pipelines'])}")

def generate_conversation_templates():
    """Generate compressed conversation templates."""
    print("Generating conversation templates...")
    CACHE_DIR.mkdir(exist_ok=True)
    
    with open(CACHE_DIR / "conversation-templates.json", "w") as f:
        json.dump(CONVERSATION_TEMPLATES, f, indent=2)
    
    print(f"  Saved: {CACHE_DIR / 'conversation-templates.json'}")
    print(f"  Templates: {len(CONVERSATION_TEMPLATES)}")
    total_tokens = sum(t['tokens'] for t in CONVERSATION_TEMPLATES.values())
    print(f"  Total tokens: {total_tokens}")

def generate_compact_reference():
    """Generate ultra-compact reference card."""
    print("Generating compact reference card...")
    CACHE_DIR.mkdir(exist_ok=True)
    
    reference = """# skygfx_plus_expIV — Compact Reference

## Build
```bash
python tools/fast_build.py              # Incremental
python tools/fast_build.py --launch     # Build + game
python tools/fast_build.py --shaders    # HLSL only
python tools/proto.py shader <name>     # Single shader
```

## Key Files
- postfx.cpp:3051 — DrawSMAA (3-pass)
- postfx.cpp:1535 — ColourFilter_switch
- postfx.cpp:241 — UpdateFrontBuffer
- vehiclePipe.cpp:1 — Vehicle callbacks
- buildingPipe.cpp:1 — Building callbacks
- pipelinecommon.cpp:1 — Shader loading, pipeUploadPBR
- main.cpp:1 — DLL entry, INI, hooks
- skygfx.h:1 — Config struct, enums
- forwardplus.cpp:58 — CollectLights stub

## PostFX Chain
NormalBuffer→VelocityBuffer→SSAO_Overhaul→DrawPipeChain→ColourFilter_switch→MotionBlur→HeightFog→GodRays→UpdateFrontBuffer→SSS

## Hooks
- 0x704D1E → ColourFilter_switch
- 0x53EBE9 → DrawFinalEffects
- 0x5D7100 → Building pipe
- 0x5D9FE9 → Vehicle pipe

## Critical Rules
- c22/c23: ALWAYS pipeUploadPBR() — NEVER manual
- SM3.0 only: ps_3_0 / vs_3_0
- Division: 1e-7 epsilon minimum
- BeginUpdate/EndUpdate: must be balanced

## Active Issues
1. SMAA black screen (CRITICAL) — postfx.cpp:3051
2. SSAO fix-13 deployed — awaiting test
3. Forward+ CollectLights stub — forwardplus.cpp:58
```
"""
    
    with open(CACHE_DIR / "compact-reference.md", "w", encoding="utf-8") as f:
        f.write(reference)
    
    print(f"  Saved: {CACHE_DIR / 'compact-reference.md'}")
    print(f"  Size: {len(reference)} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)
    
    cmd = sys.argv[1]
    if cmd == "all":
        generate_agent_index()
        generate_codebase_index()
        generate_vault_index()
        generate_conversation_templates()
        generate_compact_reference()
    elif cmd == "agent":
        generate_agent_index()
    elif cmd == "codebase":
        generate_codebase_index()
    elif cmd == "vault":
        generate_vault_index()
    elif cmd == "compress":
        generate_conversation_templates()
        generate_compact_reference()
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
