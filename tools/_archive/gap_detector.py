#!/usr/bin/env python3
"""
Gap Detector
Answers: "What do I need to know to implement feature X?"
Breaks features into concepts, checks knowledge coverage, suggests gaps.
"""
import json
from pathlib import Path

PROJECT = Path("E:/dev(dave)/skygfx_plus_expIV")
GRAPH_PATH = PROJECT / "memory-bank/.cache/knowledge-graph.json"
XREF_PATH = PROJECT / "memory-bank/.cache/cross-reference.json"
TIMELINE_PATH = PROJECT / "memory-bank/.cache/knowledge-timeline.md"

# Feature definitions: feature → required concepts + prerequisites
FEATURES = {
    "Screen Space Reflections": {
        "concepts": ["PBR", "Fresnel", "Reflection", "PostFX", "SM3.0"],
        "prerequisites": ["Environment Map", "Normal Map", "Depth Buffer"],
        "implementation_notes": "Requires depth buffer access, most common in SM4+ but possible in SM3.0 with tricks",
        "difficulty": "hard",
        "existing_implementation": False,
    },
    "Shadow Mapping": {
        "concepts": ["Shadow", "PBR", "PostFX"],
        "prerequisites": ["Depth Buffer", "Vertex Shader"],
        "implementation_notes": "Cloud shadow already exists in PBR_Common.hlsl",
        "difficulty": "medium",
        "existing_implementation": True,
        "implementation_file": "shaders/include/PBR_Common.hlsl",
    },
    "Parallax Mapping": {
        "concepts": ["Normal Map", "Texture Sampling", "PBR"],
        "prerequisites": ["Height Map", "Tangent Space"],
        "implementation_notes": "Can be done in SM3.0, needs height map texture",
        "difficulty": "medium",
        "existing_implementation": False,
    },
    "Subsurface Scattering": {
        "concepts": ["PBR", "Diffuse", "Fresnel"],
        "prerequisites": ["Skin Shader", "Thickness Map"],
        "implementation_notes": "SkinSSS.hlsl already has basic SSS",
        "difficulty": "hard",
        "existing_implementation": True,
        "implementation_file": "shaders/ps/SkinSSS.hlsl",
    },
    "Temporal Anti-Aliasing": {
        "concepts": ["PostFX", "SM3.0"],
        "prerequisites": ["Motion Vectors", "Depth Buffer", "History Buffer"],
        "implementation_notes": "SM4+ feature, needs MRT",
        "difficulty": "very hard",
        "existing_implementation": False,
    },
    "Volumetric Lighting": {
        "concepts": ["Shadow", "Fog", "Atmosphere", "PostFX"],
        "prerequisites": ["Light Shafts", "Ray Marching", "Fog"],
        "implementation_notes": "Expensive, needs ray marching in fog",
        "difficulty": "very hard",
        "existing_implementation": False,
    },
    "Decals": {
        "concepts": ["Geometry", "Material", "Texture Sampling"],
        "prerequisites": ["Deferred Rendering", "Depth Buffer"],
        "implementation_notes": "Can be done with forward rendering tricks",
        "difficulty": "medium",
        "existing_implementation": False,
    },
    "Improved Water": {
        "concepts": ["Reflection", "Fresnel", "Normal Map", "PostFX"],
        "prerequisites": ["Water Shader", "Refraction", "Caustics"],
        "implementation_notes": "Water_Parallax.hlsl already has basic water",
        "difficulty": "medium",
        "existing_implementation": True,
        "implementation_file": "shaders/ps/Water_Parallax.hlsl",
    },
    "HDR Rendering": {
        "concepts": ["PBR", "PostFX", "SM3.0"],
        "prerequisites": ["Float Textures", "Tone Mapping"],
        "implementation_notes": "SM3.0 has limited float texture support",
        "difficulty": "hard",
        "existing_implementation": False,
    },
    "Ambient Occlusion": {
        "concepts": ["PostFX", "PBR", "SM3.0"],
        "prerequisites": ["Depth Buffer", "Normal Buffer"],
        "implementation_notes": "SSAO already in postfx.cpp",
        "difficulty": "medium",
        "existing_implementation": True,
        "implementation_file": "src/render/postfx.cpp",
    },
    "PBR Building Rendering": {
        "concepts": ["PBR", "GGX", "Smith", "Fresnel", "Normal Map", "IBL"],
        "prerequisites": ["Material System", "Light Upload"],
        "implementation_notes": "buildingPBRVS/buildingPBRPS already exist",
        "difficulty": "done",
        "existing_implementation": True,
        "implementation_file": "src/render/buildingPipe.cpp",
    },
    "PBR Vehicle Rendering": {
        "concepts": ["PBR", "GGX", "Smith", "Fresnel", "Normal Map", "IBL", "Glossiness"],
        "prerequisites": ["Material System", "Light Upload", "Paint System"],
        "implementation_notes": "VehiclePBR_Modern.hlsl is the main shader",
        "difficulty": "done",
        "existing_implementation": True,
        "implementation_file": "shaders/ps/VehiclePBR_Modern.hlsl",
    },
}

def load_graph():
    if GRAPH_PATH.exists():
        return json.loads(GRAPH_PATH.read_text())
    return {"relationships": {}, "implementations": {}}

def load_xref():
    if XREF_PATH.exists():
        return json.loads(XREF_PATH.read_text())
    return {"concept_to_files": {}, "file_to_concepts": {}}

def check_knowledge(concept, graph, xref):
    """Check if we have knowledge for a concept."""
    result = {
        "concept": concept,
        "has_graph_node": concept in graph.get("relationships", {}),
        "has_implementation": concept in graph.get("implementations", {}),
        "files_linked": len(xref.get("concept_to_files", {}).get(concept, [])),
        "status": "unknown"
    }
    
    if result["has_implementation"]:
        result["status"] = "implemented"
    elif result["has_graph_node"] and result["files_linked"] > 0:
        result["status"] = "documented"
    elif result["files_linked"] > 0:
        result["status"] = "referenced"
    else:
        result["status"] = "missing"
    
    return result

def analyze_feature(feature_name, graph, xref):
    """Analyze knowledge gaps for a feature."""
    if feature_name not in FEATURES:
        return None
    
    feature = FEATURES[feature_name]
    
    # Check required concepts
    required = []
    for concept in feature["concepts"]:
        required.append(check_knowledge(concept, graph, xref))
    
    # Check prerequisites
    prereqs = []
    for concept in feature.get("prerequisites", []):
        prereqs.append(check_knowledge(concept, graph, xref))
    
    # Calculate coverage
    all_concepts = required + prereqs
    known = sum(1 for c in all_concepts if c["status"] in ["implemented", "documented"])
    total = len(all_concepts)
    coverage = known / total if total > 0 else 0
    
    return {
        "feature": feature_name,
        "difficulty": feature["difficulty"],
        "existing_implementation": feature.get("existing_implementation", False),
        "implementation_file": feature.get("implementation_file"),
        "implementation_notes": feature.get("implementation_notes", ""),
        "required_concepts": required,
        "prerequisites": prereqs,
        "coverage": coverage,
        "gaps": [c["concept"] for c in all_concepts if c["status"] == "missing"],
        "documented": [c["concept"] for c in all_concepts if c["status"] == "documented"],
        "implemented": [c["concept"] for c in all_concepts if c["status"] == "implemented"],
    }

def suggest_research(gaps):
    """Suggest what to research for gaps."""
    suggestions = []
    
    research_map = {
        "Depth Buffer": "Search for 'depth buffer access SM3.0' or 'reverse z technique'",
        "Motion Vectors": "Search for 'motion vectors without MRT' or 'temporal AA SM3.0'",
        "History Buffer": "Search for 'temporal reprojection' or 'history buffer reuse'",
        "Height Map": "Search for 'height map parallax mapping tutorial'",
        "Tangent Space": "Search for 'tangent space calculation' or 'TBN matrix'",
        "Thickness Map": "Search for 'screen space thickness estimation'",
        "Skin Shader": "Search for 'screen space subsurface scattering'",
        "Light Shafts": "Search for 'god rays screen space' or 'crepuscular rays'",
        "Ray Marching": "Search for 'screen space ray marching' or 'march along depth'",
        "Water Shader": "Search for 'realistic water rendering' or 'refraction caustics'",
        "Float Textures": "Search for 'SM3.0 half precision' or 'HDR without MRT'",
        "Tone Mapping": "Search for 'tone mapping operator' or 'ACES tonemap'",
        "Normal Buffer": "Search for 'G-buffer normal encoding' or 'normal render target'",
        "Deferred Rendering": "Search for 'deferred shading forward hybrid'",
        "Caustics": "Search for 'water caustics screen space'",
        "Refraction": "Search for 'screen space refraction' or 'distortion buffer'",
        "Material System": "Check existing material upload in pipelinecommon.cpp",
        "Light Upload": "Check existing light upload in vehiclePipe.cpp",
        "Paint System": "Check existing paint noise in VehiclePBR_Modern.hlsl",
    }
    
    for gap in gaps:
        suggestion = research_map.get(gap, f"Search for '{gap} implementation real-time rendering'")
        suggestions.append({
            "concept": gap,
            "suggestion": suggestion,
            "priority": "high" if gap in ["Depth Buffer", "Motion Vectors", "Normal Buffer"] else "medium"
        })
    
    return suggestions

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Gap Detector")
    parser.add_argument("--list", action="store_true", help="List all features")
    parser.add_argument("--query", "-q", help="Analyze feature X")
    parser.add_argument("--all", action="store_true", help="Analyze all features")
    parser.add_argument("--status", action="store_true", help="Show overall status")
    args = parser.parse_args()
    
    graph = load_graph()
    xref = load_xref()
    
    if args.status:
        print("Gap Detector Status:")
        print(f"  Features tracked: {len(FEATURES)}")
        print(f"  Concepts in graph: {len(graph.get('relationships', {}))}")
        print(f"  Cross-references: {len(xref.get('concept_to_files', {}))}")
        
        # Overall coverage
        total = 0
        known = 0
        for feature in FEATURES:
            analysis = analyze_feature(feature, graph, xref)
            if analysis:
                total += len(analysis["required_concepts"]) + len(analysis["prerequisites"])
                known += len(analysis["implemented"]) + len(analysis["documented"])
        
        print(f"\n  Overall coverage: {known}/{total} ({known/total*100:.0f}%)")
        return
    
    if args.list:
        print("Features:")
        for name, feat in sorted(FEATURES.items()):
            impl = "[x]" if feat.get("existing_implementation") else "[ ]"
            print(f"  {impl} {name} ({feat['difficulty']})")
        return
    
    if args.all:
        for name in sorted(FEATURES.keys()):
            analysis = analyze_feature(name, graph, xref)
            if analysis:
                print(f"\n{'='*50}")
                print(f"Feature: {analysis['feature']}")
                print(f"Difficulty: {analysis['difficulty']}")
                print(f"Coverage: {analysis['coverage']*100:.0f}%")
                print(f"Existing implementation: {analysis['existing_implementation']}")
                if analysis['implementation_file']:
                    print(f"File: {analysis['implementation_file']}")
                print(f"Required: {', '.join(analysis['implemented'])}")
                print(f"Documented: {', '.join(analysis['documented'])}")
                if analysis['gaps']:
                    print(f"GAPS: {', '.join(analysis['gaps'])}")
                    suggestions = suggest_research(analysis['gaps'])
                    for s in suggestions:
                        print(f"  -> {s['concept']}: {s['suggestion']}")
        return
    
    if args.query:
        feature = args.query
        analysis = analyze_feature(feature, graph, xref)
        if analysis:
            print(f"\nFeature: {analysis['feature']}")
            print(f"Difficulty: {analysis['difficulty']}")
            print(f"Coverage: {analysis['coverage']*100:.0f}%")
            print(f"Existing implementation: {analysis['existing_implementation']}")
            if analysis['implementation_file']:
                print(f"File: {analysis['implementation_file']}")
            print(f"\nNotes: {analysis['implementation_notes']}")
            print(f"\nRequired concepts:")
            for c in analysis['required_concepts']:
                print(f"  [{c['status']}] {c['concept']} ({c['files_linked']} files)")
            print(f"\nPrerequisites:")
            for c in analysis['prerequisites']:
                print(f"  [{c['status']}] {c['concept']} ({c['files_linked']} files)")
            if analysis['gaps']:
                print(f"\nGAPS ({len(analysis['gaps'])}):")
                suggestions = suggest_research(analysis['gaps'])
                for s in suggestions:
                    print(f"  [{s['priority']}] {s['concept']}: {s['suggestion']}")
            else:
                print(f"\nNo gaps found!")
        else:
            print(f"Feature '{feature}' not found. Use --list to see available features.")
        return
    
    print("Use --list, --query, --all, or --status")

if __name__ == "__main__":
    main()
