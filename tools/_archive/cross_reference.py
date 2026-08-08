#!/usr/bin/env python3
"""
Cross-Reference System
Links concepts across wiki articles, shader code, and documentation.
Builds a bidirectional map: concept → files, file → concepts.
"""
import os
import re
import json
from pathlib import Path
from collections import defaultdict

# Paths
PROJECT = Path("E:/dev(dave)/skygfx_plus_expIV")
OBSIDIAN = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
SHADERS = PROJECT / "shaders"
SRC = PROJECT / "src"
WIKI = Path("H:/wikis/gtamods/rendering")
XREF_INDEX = PROJECT / "memory-bank/.cache/cross-reference.json"

# Concept definitions: name → keywords to search for
CONCEPTS = {
    # PBR / BRDF
    "GGX": ["ggx", "trowbridge-reitz", "normal distribution function"],
    "Smith": ["smith", "geometry function", "shadowing-masking", "v-cavity"],
    "Schlick": ["schlick", "fresnel approximation"],
    "Fresnel": ["fresnel", "reflectance", "f0", "dielectric"],
    "BRDF": ["brdf", "bidirectional reflectance"],
    "PBR": ["pbr", "physically based rendering", "physically based"],
    "Microfacet": ["microfacet", "micro-face", "micro surface"],
    "Energy Conservation": ["energy conservation", "energy preserving"],
    "Cook-Torrance": ["cook-torrance", "cook torrance"],
    "Specular": ["specular", "specularity", "highlight"],
    "Diffuse": ["diffuse", "lambert", "irradiance"],
    "Glossiness": ["glossiness", "smoothness", "roughness inverse"],
    
    # Textures / Materials
    "Normal Map": ["normal map", "normalmap", "tangent space", "bump map"],
    "Environment Map": ["environment map", "envmap", "reflection map", "cubemap"],
    "IBL": ["ibl", "image-based lighting", "irradiance map", "prefiltered"],
    "Texture Sampling": ["texture sampling", "sampler", "tex2d", "samplerstate"],
    "Material": ["material", "rpmaterial", "matprops"],
    
    # Geometry
    "Vertex Buffer": ["vertex buffer", "vb", "d3d9vertexbuffer"],
    "Index Buffer": ["index buffer", "ib", "d3d9indexbuffer"],
    "Mesh": ["mesh", "triangle", "primitive"],
    "Bin Mesh": ["bin mesh", "binmesh", "rpgeometry"],
    "Atomic": ["atomic", "rpatomic", "atomic struct"],
    "Clump": ["clump", "rpclump", "drawable"],
    
    # RenderWare
    "RenderWare": ["renderware", "rw ", "rpw", "rw3"],
    "RW Section": ["rw section", "section id", "chunk type"],
    "Pipeline": ["pipeline", "renderpipe", "pipe"],
    "HAnim": ["hanim", "hierarchy", "skeleton", "bone"],
    
    # GTA SA specific
    "DFF": ["dff", "dff file", "dff format", "dff model"],
    "TXD": ["txd", "texture dictionary", "txd file"],
    "IPL": ["ipl", "instance placement", "binary ipl"],
    "IDE": ["ide", "item definition", "object definition"],
    "Time Cycle": ["time cycle", "timecycle", "timeofday"],
    "Weather": ["weather", "rain", "fog", "cloud"],
    
    # Rendering concepts
    "Shadow": ["shadow", "shadow map", "shadow mapping"],
    "Reflection": ["reflection", "reflect", "specular reflection"],
    "Fog": ["fog", "distance fog", "height fog", "volumetric"],
    "LOD": ["lod", "level of detail", "distance cull"],
    "Culling": ["cull", "culling", "occlusion", "frustum"],
    "PostFX": ["postfx", "post fx", "screen space", "ssao", "smaa"],
    
    # Shader specific
    "SM3.0": ["sm3", "ps_3_0", "vs_3_0", "shader model 3"],
    "Constant Register": ["constant register", r"c\[", "reg c"],
    "Pixel Shader": ["pixel shader", "ps ", "fragment shader"],
    "Vertex Shader": ["vertex shader", "vs ", "vertex program"],
}

# Source code patterns to find implementations
CODE_PATTERNS = {
    "GGX": [r"ggx", r"trowbridge", r"D_GGX", r"ndf_ggx"],
    "Smith": [r"smith", r"G_Smith", r"geometry_smith", r"v_smith"],
    "Schlick": [r"schlick", r"fresnel_schlick", r"F_Schlick"],
    "Fresnel": [r"fresnel", r"F0", r"reflectance"],
    "Normal Map": [r"normal.*map", r"tangent.*space", r"SampleNormal", r"DecodeNormal"],
    "IBL": [r"ibl", r"irradiance", r"prefilter", r"cubemap.*sample"],
    "PBR": [r"pbr", r"pipeUploadPBR", r"pbrParams"],
    "Pipeline": [r"pipe\w+", r"BUILDING_PBR", r"CAR_MODERN", r"vehiclePipe"],
    "Time Cycle": [r"time.*cycle", r"timecycle", r"CTimeCycle", r"CClock"],
    "Shadow": [r"shadow", r"cloudShadow", r"shadowMap"],
    "PostFX": [r"postfx", r"SSAO", r"SMAA", r"ColourFilter"],
}

def scan_file_concepts(file_path, patterns):
    """Scan a file for concept matches."""
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore")
        content_lower = content.lower()
    except:
        return []
    
    found = []
    for concept, pats in patterns.items():
        for pat in pats:
            if re.search(pat, content_lower):
                found.append(concept)
                break
    
    return list(set(found))

def scan_obsidian_files():
    """Scan Obsidian vault for concepts."""
    results = defaultdict(list)
    
    for md_file in OBSIDIAN.rglob("*.md"):
        concepts = scan_file_concepts(md_file, CONCEPTS)
        for concept in concepts:
            rel = md_file.relative_to(OBSIDIAN)
            results[concept].append({
                "file": str(rel),
                "type": "obsidian",
                "name": md_file.stem
            })
    
    return results

def scan_shader_files():
    """Scan shader source for implementations."""
    results = defaultdict(list)
    
    if not SHADERS.exists():
        return results
    
    for hlsl in SHADERS.rglob("*.hlsl"):
        concepts = scan_file_concepts(hlsl, CODE_PATTERNS)
        for concept in concepts:
            rel = hlsl.relative_to(PROJECT)
            results[concept].append({
                "file": str(rel),
                "type": "shader",
                "name": hlsl.stem
            })
    
    return results

def scan_source_files():
    """Scan C++ source for implementations."""
    results = defaultdict(list)
    
    if not SRC.exists():
        return results
    
    for ext in ["*.cpp", "*.h"]:
        for src_file in SRC.rglob(ext):
            concepts = scan_file_concepts(src_file, CODE_PATTERNS)
            for concept in concepts:
                rel = src_file.relative_to(PROJECT)
                results[concept].append({
                    "file": str(rel),
                    "type": "source",
                    "name": src_file.stem
                })
    
    return results

def scan_wiki_files():
    """Scan wiki articles for concepts."""
    results = defaultdict(list)
    
    if not WIKI.exists():
        return results
    
    for md_file in WIKI.rglob("*.md"):
        concepts = scan_file_concepts(md_file, CONCEPTS)
        for concept in concepts:
            rel = md_file.relative_to(WIKI)
            results[concept].append({
                "file": str(rel),
                "type": "wiki",
                "name": md_file.stem
            })
    
    return results

def build_reverse_index(concept_index):
    """Build file → concepts reverse index."""
    reverse = defaultdict(list)
    for concept, files in concept_index.items():
        for f in files:
            key = f"{f['type']}:{f['file']}"
            reverse[key].append(concept)
    return dict(reverse)

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Cross-Reference System")
    parser.add_argument("--status", action="store_true", help="Show status")
    parser.add_argument("--query", "-q", help="Query concept")
    parser.add_argument("--file", "-f", help="Query file")
    parser.add_argument("--list-concepts", action="store_true", help="List all concepts")
    args = parser.parse_args()
    
    print("Building cross-reference index...")
    
    # Scan all sources
    concept_index = defaultdict(list)
    
    print("  Scanning Obsidian vault...")
    obsidian = scan_obsidian_files()
    for c, files in obsidian.items():
        concept_index[c].extend(files)
    
    print("  Scanning shaders...")
    shaders = scan_shader_files()
    for c, files in shaders.items():
        concept_index[c].extend(files)
    
    print("  Scanning source code...")
    source = scan_source_files()
    for c, files in source.items():
        concept_index[c].extend(files)
    
    print("  Scanning wiki articles...")
    wiki = scan_wiki_files()
    for c, files in wiki.items():
        concept_index[c].extend(files)
    
    # Build reverse index
    reverse_index = build_reverse_index(concept_index)
    
    # Save
    output = {
        "concept_to_files": dict(concept_index),
        "file_to_concepts": reverse_index,
        "stats": {
            "concepts": len(concept_index),
            "files_scanned": len(reverse_index),
            "total_links": sum(len(v) for v in concept_index.values())
        }
    }
    
    XREF_INDEX.parent.mkdir(parents=True, exist_ok=True)
    XREF_INDEX.write_text(json.dumps(output, indent=2))
    
    if args.status or args.list_concepts:
        print(f"\nCross-Reference Index:")
        print(f"  Concepts tracked: {output['stats']['concepts']}")
        print(f"  Files indexed: {output['stats']['files_scanned']}")
        print(f"  Total links: {output['stats']['total_links']}")
        
        if args.list_concepts:
            print(f"\nConcepts:")
            for concept in sorted(concept_index.keys()):
                count = len(concept_index[concept])
                print(f"  {concept}: {count} files")
        
        return
    
    if args.query:
        concept = args.query
        if concept in concept_index:
            print(f"\n'{concept}' found in:")
            for f in concept_index[concept]:
                print(f"  [{f['type']}] {f['file']}")
        else:
            print(f"Concept '{concept}' not found")
        return
    
    if args.file:
        key = args.file
        if key in reverse_index:
            print(f"\n'{key}' references:")
            for concept in reverse_index[key]:
                print(f"  {concept}")
        else:
            print(f"File '{key}' not indexed")
        return
    
    print(f"\nDone. Index saved to {XREF_INDEX}")

if __name__ == "__main__":
    main()
