#!/usr/bin/env python3
"""
Knowledge Graph
Maps relationships between concepts: what depends on what, what implements what.
Outputs concept chains (A → B → C) and dependency trees.
"""
import json
from pathlib import Path
from collections import defaultdict

PROJECT = Path("E:/dev(dave)/skygfx_plus_expIV")
GRAPH_PATH = PROJECT / "memory-bank/.cache/knowledge-graph.json"

# Concept relationships: child → parents (what it depends on)
RELATIONSHIPS = {
    # Physics → Math
    "Fresnel": {"depends_on": ["Schlick"], "part_of": ["BRDF"], "implements": "Reflectance at interfaces"},
    "Schlick": {"depends_on": ["Fresnel"], "part_of": ["Fresnel"], "implements": "Fast Fresnel approximation"},
    "GGX": {"depends_on": ["Microfacet"], "part_of": ["BRDF"], "implements": "Normal Distribution Function"},
    "Smith": {"depends_on": ["Microfacet"], "part_of": ["BRDF"], "implements": "Geometry/Shadowing function"},
    "Microfacet": {"depends_on": ["BRDF"], "part_of": ["BRDF"], "implements": "Surface roughness model"},
    "Cook-Torrance": {"depends_on": ["GGX", "Smith", "Fresnel"], "part_of": ["BRDF"], "implements": "Specular BRDF model"},
    "BRDF": {"depends_on": ["Energy Conservation"], "part_of": ["PBR"], "implements": "Light scattering function"},
    "Energy Conservation": {"depends_on": [], "part_of": ["PBR"], "implements": "Physically plausible lighting"},
    "PBR": {"depends_on": ["BRDF", "Energy Conservation"], "part_of": ["Shader Development"], "implements": "Physically based rendering"},
    
    # Shader concepts
    "Glossiness": {"depends_on": ["Microfacet"], "part_of": ["PBR"], "implements": "Surface smoothness parameter"},
    "Specular": {"depends_on": ["Fresnel", "GGX"], "part_of": ["PBR"], "implements": "Direct reflection"},
    "Diffuse": {"depends_on": ["Energy Conservation"], "part_of": ["PBR"], "implements": "Lambertian scattering"},
    "Normal Map": {"depends_on": ["Microfacet"], "part_of": ["PBR"], "implements": "Surface detail"},
    "Environment Map": {"depends_on": ["Fresnel", "Reflection"], "part_of": ["PBR"], "implements": "Indirect lighting"},
    "IBL": {"depends_on": ["Environment Map", "Diffuse"], "part_of": ["PBR"], "implements": "Image-based lighting"},
    "Texture Sampling": {"depends_on": [], "part_of": ["Shader Development"], "implements": "Color/normal lookup"},
    
    # RenderWare concepts
    "Atomic": {"depends_on": ["Clump", "Geometry"], "part_of": ["RenderWare"], "implements": "Drawable instance"},
    "Clump": {"depends_on": ["Frame"], "part_of": ["RenderWare"], "implements": "Object container"},
    "Geometry": {"depends_on": ["Vertex Buffer", "Index Buffer"], "part_of": ["RenderWare"], "implements": "Triangle mesh"},
    "Material": {"depends_on": ["Texture Sampling"], "part_of": ["RenderWare"], "implements": "Surface properties"},
    "Frame": {"depends_on": [], "part_of": ["RenderWare"], "implements": "Transform hierarchy"},
    "HAnim": {"depends_on": ["Frame"], "part_of": ["RenderWare"], "implements": "Skeletal animation"},
    "Bin Mesh": {"depends_on": ["Geometry"], "part_of": ["RenderWare"], "implements": "Indexed triangle list"},
    "Vertex Buffer": {"depends_on": [], "part_of": ["RenderWare"], "implements": "Vertex data storage"},
    "Index Buffer": {"depends_on": [], "part_of": ["RenderWare"], "implements": "Triangle index data"},
    "Mesh": {"depends_on": ["Vertex Buffer", "Index Buffer"], "part_of": ["RenderWare"], "implements": "Primitive geometry"},
    
    # GTA SA concepts
    "DFF": {"depends_on": ["Atomic", "Clump", "Geometry"], "part_of": ["GTA SA"], "implements": "3D model file"},
    "TXD": {"depends_on": ["Material", "Texture Sampling"], "part_of": ["GTA SA"], "implements": "Texture dictionary"},
    "IPL": {"depends_on": [], "part_of": ["GTA SA"], "implements": "World placement"},
    "IDE": {"depends_on": [], "part_of": ["GTA SA"], "implements": "Object definitions"},
    "Time Cycle": {"depends_on": [], "part_of": ["GTA SA"], "implements": "Day/night lighting"},
    "Pipeline": {"depends_on": ["RenderWare", "D3D9"], "part_of": ["skygfx"], "implements": "Render pipeline switch"},
    
    # Rendering concepts
    "Shadow": {"depends_on": ["PBR"], "part_of": ["PostFX"], "implements": "Shadow mapping"},
    "Reflection": {"depends_on": ["Fresnel", "Environment Map"], "part_of": ["PBR"], "implements": "Specular reflection"},
    "Fog": {"depends_on": ["Atmosphere"], "part_of": ["PostFX"], "implements": "Distance/haze"},
    "LOD": {"depends_on": ["Atomic"], "part_of": ["Rendering"], "implements": "Level of detail"},
    "Culling": {"depends_on": ["LOD"], "part_of": ["Rendering"], "implements": "Visibility determination"},
    "PostFX": {"depends_on": ["PBR"], "part_of": ["skygfx"], "implements": "Screen-space effects"},
    
    # Shader development
    "SM3.0": {"depends_on": [], "part_of": ["Shader Development"], "implements": "Shader model target"},
    "Constant Register": {"depends_on": ["SM3.0"], "part_of": ["Shader Development"], "implements": "Uniform storage"},
    "Pixel Shader": {"depends_on": ["SM3.0"], "part_of": ["Shader Development"], "implements": "Fragment processing"},
    "Vertex Shader": {"depends_on": ["SM3.0"], "part_of": ["Shader Development"], "implements": "Vertex transform"},
    
    # Atmosphere
    "Atmosphere": {"depends_on": [], "part_of": ["Rendering"], "implements": "Sky rendering"},
    "Weather": {"depends_on": ["Atmosphere", "Time Cycle"], "part_of": ["GTA SA"], "implements": "Weather system"},
}

# Concept → implementation mapping
IMPLEMENTATIONS = {
    "GGX": {
        "shader": "shaders/include/PBR_Common.hlsl",
        "function": "D_GGX",
        "description": "Trowbridge-Reitz NDF"
    },
    "Smith": {
        "shader": "shaders/include/PBR_Common.hlsl",
        "function": "G_Smith",
        "description": "Height-correlated Smith GGX"
    },
    "Schlick": {
        "shader": "shaders/include/PBR_Common.hlsl",
        "function": "F_Schlick",
        "description": "Schlick Fresnel approximation"
    },
    "Fresnel": {
        "shader": "shaders/include/PBR_Common.hlsl",
        "function": "F_Schlick",
        "description": "Fresnel reflectance"
    },
    "PBR": {
        "shader": "shaders/ps/VehiclePBR_Modern.hlsl",
        "function": "main",
        "description": "Full PBR pipeline"
    },
    "Normal Map": {
        "shader": "shaders/ps/VehiclePBR_Modern.hlsl",
        "function": "DecodeNormal",
        "description": "Tangent-space normal decode"
    },
    "IBL": {
        "shader": "shaders/ps/VehiclePBR_Modern.hlsl",
        "function": "sampleIBL",
        "description": "Image-based lighting"
    },
    "Pipeline": {
        "source": "src/render/pipelinecommon.cpp",
        "function": "pipeUploadPBR",
        "description": "PBR parameter upload"
    },
    "Time Cycle": {
        "source": "src/core/main.cpp",
        "function": "CTimeCycle",
        "description": "Day/night cycle"
    },
}

def build_dependency_tree(concept, visited=None):
    """Build full dependency tree for a concept."""
    if visited is None:
        visited = set()
    
    if concept in visited:
        return {"concept": concept, "circular": True}
    
    visited.add(concept)
    
    if concept not in RELATIONSHIPS:
        return {"concept": concept, "depends_on": [], "part_of": None, "implements": None}
    
    rel = RELATIONSHIPS[concept]
    tree = {
        "concept": concept,
        "implements": rel.get("implements", ""),
        "part_of": rel.get("part_of", []),
        "depends_on": []
    }
    
    for dep in rel.get("depends_on", []):
        tree["depends_on"].append(build_dependency_tree(dep, visited.copy()))
    
    return tree

def find_concept_chain(start, end, visited=None):
    """Find chain from start concept to end concept."""
    if visited is None:
        visited = set()
    
    if start == end:
        return [start]
    
    if start in visited:
        return None
    
    visited.add(start)
    
    if start not in RELATIONSHIPS:
        return None
    
    for dep in RELATIONSHIPS[start].get("depends_on", []):
        chain = find_concept_chain(dep, end, visited.copy())
        if chain:
            return [start] + chain
    
    return None

def get_all_chains():
    """Get all concept chains from low-level to high-level."""
    chains = []
    
    # Find root concepts (no dependencies)
    roots = [c for c, r in RELATIONSHIPS.items() if not r.get("depends_on")]
    
    # Find leaf concepts (nothing depends on them)
    all_deps = set()
    for r in RELATIONSHIPS.values():
        all_deps.update(r.get("depends_on", []))
    leaves = [c for c in RELATIONSHIPS if c not in all_deps]
    
    # Build chains from each root to leaves
    for root in roots:
        for leaf in leaves:
            chain = find_concept_chain(leaf, root)
            if chain and len(chain) > 2:
                chains.append(chain)
    
    return chains

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Knowledge Graph")
    parser.add_argument("--status", action="store_true", help="Show graph stats")
    parser.add_argument("--query", "-q", help="Query concept dependencies")
    parser.add_argument("--chain", nargs=2, metavar=("FROM", "TO"), help="Find chain between concepts")
    parser.add_argument("--chains", action="store_true", help="Show all concept chains")
    parser.add_argument("--impl", help="Show implementation details for concept")
    args = parser.parse_args()
    
    # Build graph
    graph = {
        "relationships": RELATIONSHIPS,
        "implementations": IMPLEMENTATIONS,
        "chains": get_all_chains()
    }
    
    GRAPH_PATH.parent.mkdir(parents=True, exist_ok=True)
    GRAPH_PATH.write_text(json.dumps(graph, indent=2))
    
    if args.status:
        print("Knowledge Graph:")
        print(f"  Concepts: {len(RELATIONSHIPS)}")
        print(f"  Implementations tracked: {len(IMPLEMENTATIONS)}")
        print(f"  Concept chains: {len(graph['chains'])}")
        
        # Count by layer
        layers = defaultdict(int)
        for r in RELATIONSHIPS.values():
            for p in r.get("part_of", []):
                layers[p] += 1
        print(f"\n  By layer:")
        for layer, count in sorted(layers.items()):
            print(f"    {layer}: {count}")
        return
    
    if args.query:
        concept = args.query
        if concept in RELATIONSHIPS:
            tree = build_dependency_tree(concept)
            print(f"\n{concept}:")
            print(f"  Implements: {tree['implements']}")
            print(f"  Part of: {tree['part_of']}")
            print(f"  Depends on: {[d['concept'] for d in tree['depends_on']]}")
            
            if concept in IMPLEMENTATIONS:
                impl = IMPLEMENTATIONS[concept]
                print(f"  Implementation: {impl.get('function', 'N/A')} in {impl.get('shader') or impl.get('source', 'N/A')}")
        else:
            print(f"Concept '{concept}' not found")
        return
    
    if args.chain:
        start, end = args.chain
        chain = find_concept_chain(start, end)
        if chain:
            print(f"Chain: {' -> '.join(chain)}")
        else:
            print(f"No chain from '{start}' to '{end}'")
        return
    
    if args.chains:
        print(f"\nConcept Chains ({len(graph['chains'])} total):")
        for chain in sorted(graph['chains'], key=len, reverse=True)[:20]:
            print(f"  {' -> '.join(chain)}")
        return
    
    if args.impl:
        concept = args.impl
        if concept in IMPLEMENTATIONS:
            impl = IMPLEMENTATIONS[concept]
            print(f"\n{concept} implementation:")
            for k, v in impl.items():
                print(f"  {k}: {v}")
        else:
            print(f"No implementation tracked for '{concept}'")
        return
    
    print(f"Graph saved to {GRAPH_PATH}")

if __name__ == "__main__":
    main()
