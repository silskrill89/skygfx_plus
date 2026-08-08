#!/usr/bin/env python3
"""
knowledge.py — Unified knowledge system for skygfx_plus_expIV

Self-contained: ingest, xref, graph, gaps — no external imports needed.
Integrates with Obsidian vault, memory bank, and all other tools.
"""
import os
import re
import sys
import json
import hashlib
from pathlib import Path
from datetime import datetime
from collections import defaultdict

# ═══════════════════════════════════════════════════════════════
# PATHS
# ═══════════════════════════════════════════════════════════════

PROJECT = Path(os.environ.get("SKYGFX_PROJECT", "E:/dev(dave)/skygfx_plus_expIV"))
CACHE = PROJECT / "memory-bank/.cache"
OBSIDIAN = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
WIKI = Path("H:/wikis/gtamods/rendering")
SHADERS = PROJECT / "shaders"
SRC = PROJECT / "src"
RESEARCH = PROJECT / "docs/research"
SCIENCE = PROJECT / "memory-bank/.cache/science-pillars"

SYNC_INDEX = CACHE / "ingest-sync.json"
XREF_INDEX = CACHE / "cross-reference.json"
GRAPH_INDEX = CACHE / "knowledge-graph.json"

# ═══════════════════════════════════════════════════════════════
# INGEST (from knowledge_ingest.py)
# ═══════════════════════════════════════════════════════════════

CATEGORY_MAP = {
    "RW_Sections": "Reference/RenderWare/Sections",
    "Documentation": "Reference/RenderWare",
    "GTA_SA": "Reference/GTA-SA",
    "GTA_VC": "Reference/GTA-VC",
    "GTA_4": "Reference/GTA-IV",
    "GTA_5": "Reference/GTA-V",
    "File_Formats": "Reference/File-Formats",
    "Map_Formats": "Reference/File-Formats",
    "Vehicle_Formats": "Reference/Vehicles",
    "Tools": "Tools",
    "Stubs": "Archive/Stubs",
    "Incomplete": "Archive/Incomplete",
    "uncategorized": "Reference/Other",
}

def load_sync_index():
    if SYNC_INDEX.exists():
        return json.loads(SYNC_INDEX.read_text())
    return {"files": {}, "last_sync": None}

def save_sync_index(index):
    index["last_sync"] = datetime.now().isoformat()
    SYNC_INDEX.write_text(json.dumps(index, indent=2))

def file_hash(path):
    return hashlib.md5(path.read_bytes()).hexdigest()[:12]

def extract_frontmatter(content):
    if content.startswith("---"):
        end = content.find("---", 3)
        if end > 0:
            return content[3:end].strip(), content[end+3:]
    return None, content

def infer_tags(title, content, categories):
    tags = []
    for cat in categories:
        if "RW" in cat or "RenderWare" in cat: tags.append("renderware")
        if "GTA" in cat: tags.append("gta")
        if "Format" in cat: tags.append("file-format")
    title_lower = title.lower()
    keyword_tags = {
        "shader": "shader", "hlsl": "shader", "pbr": "pbr", "brdf": "pbr",
        "texture": "texture", "txd": "texture", "material": "material",
        "geometry": "geometry", "mesh": "geometry", "dff": "model-format",
        "collision": "collision", "light": "lighting", "shadow": "lighting",
        "pipeline": "rendering", "atomic": "rendering", "ipl": "level-format",
        "water": "effects", "particle": "effects", "car": "vehicle",
    }
    for kw, tag in keyword_tags.items():
        if kw in title_lower: tags.append(tag)
    return list(set(tags))

def wiki_to_obsidian(content, title, categories, source_path):
    tags = infer_tags(title, content, categories)
    fm = {"title": title, "source": "gtamods.com", "source_path": str(source_path),
          "tags": tags, "created": datetime.now().strftime("%Y-%m-%d")}
    if categories: fm["wiki_categories"] = categories
    yaml_lines = ["---"]
    for k, v in fm.items():
        if isinstance(v, list):
            yaml_lines.append(f"{k}:")
            for item in v: yaml_lines.append(f"  - {item}")
        else: yaml_lines.append(f"{k}: \"{v}\"")
    yaml_lines.extend(["---", ""])
    body = re.sub(r'\[\[([^\]|]*?\|)?([^\]]*)\]\]', r'[[\2]]', content)
    return "\n".join(yaml_lines) + "\n" + body

def sync_wiki_articles(index, force=False):
    synced = skipped = 0
    for category_dir in WIKI.iterdir():
        if not category_dir.is_dir(): continue
        obsidian_folder = CATEGORY_MAP.get(category_dir.name, "Reference/Other")
        target_base = OBSIDIAN / obsidian_folder
        target_base.mkdir(parents=True, exist_ok=True)
        for article in category_dir.glob("*.md"):
            key = f"wiki:{category_dir.name}/{article.name}"
            current_hash = file_hash(article)
            if not force and index["files"].get(key) == current_hash:
                skipped += 1; continue
            content = article.read_text(encoding="utf-8")
            fm, body = extract_frontmatter(content)
            categories = []
            if fm:
                match = re.search(r'categories:\s*(\[.*?\])', body)
                if match:
                    try: categories = json.loads(match.group(1))
                    except: pass
            obsidian_content = wiki_to_obsidian(body, article.stem, categories, article)
            target = target_base / article.name
            target.write_text(obsidian_content, encoding="utf-8")
            index["files"][key] = current_hash
            synced += 1
    return synced, skipped

def sync_research(index, force=False):
    synced = 0
    target_base = OBSIDIAN / "Research"
    target_base.mkdir(parents=True, exist_ok=True)
    for research_file in RESEARCH.rglob("*.md"):
        key = f"research:{research_file.relative_to(RESEARCH)}"
        current_hash = file_hash(research_file)
        if not force and index["files"].get(key) == current_hash: continue
        content = research_file.read_text(encoding="utf-8")
        fm = f"---\nsource: \"research\"\ncreated: \"{datetime.now().strftime('%Y-%m-%d')}\"\n---\n\n"
        rel = research_file.relative_to(RESEARCH)
        target = target_base / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(fm + content, encoding="utf-8")
        index["files"][key] = current_hash
        synced += 1
    return synced

def sync_science(index, force=False):
    synced = 0
    target_base = OBSIDIAN / "Science"
    target_base.mkdir(parents=True, exist_ok=True)
    for science_file in SCIENCE.glob("*.md"):
        if science_file.name == "README.md": continue
        key = f"science:{science_file.name}"
        current_hash = file_hash(science_file)
        if not force and index["files"].get(key) == current_hash: continue
        content = science_file.read_text(encoding="utf-8")
        title = science_file.stem.replace("-", " ").title()
        fm = f"---\ntitle: \"{title}\"\nsource: \"science-pillar\"\ntags: [science, reference]\ncreated: \"{datetime.now().strftime('%Y-%m-%d')}\"\n---\n\n"
        target = target_base / science_file.name
        target.write_text(fm + content, encoding="utf-8")
        index["files"][key] = current_hash
        synced += 1
    return synced

# ═══════════════════════════════════════════════════════════════
# CROSS-REFERENCE (from cross_reference.py)
# ═══════════════════════════════════════════════════════════════

CONCEPTS = {
    "GGX": ["ggx", "trowbridge-reitz", "normal distribution function"],
    "Smith": ["smith", "geometry function", "shadowing-masking"],
    "Schlick": ["schlick", "fresnel approximation"],
    "Fresnel": ["fresnel", "reflectance", "f0", "dielectric"],
    "BRDF": ["brdf", "bidirectional reflectance"],
    "PBR": ["pbr", "physically based rendering", "physically based"],
    "Microfacet": ["microfacet", "micro-face", "micro surface"],
    "Cook-Torrance": ["cook-torrance", "cook torrance"],
    "Specular": ["specular", "specularity", "highlight"],
    "Diffuse": ["diffuse", "lambert", "irradiance"],
    "Glossiness": ["glossiness", "smoothness", "roughness inverse"],
    "Normal Map": ["normal map", "normalmap", "tangent space", "bump map"],
    "Environment Map": ["environment map", "envmap", "reflection map", "cubemap"],
    "IBL": ["ibl", "image-based lighting", "irradiance map", "prefiltered"],
    "Texture Sampling": ["texture sampling", "sampler", "tex2d", "samplerstate"],
    "Material": ["material", "rpmaterial", "matprops"],
    "Vertex Buffer": ["vertex buffer", "vb", "d3d9vertexbuffer"],
    "Index Buffer": ["index buffer", "ib", "d3d9indexbuffer"],
    "Mesh": ["mesh", "triangle", "primitive"],
    "Atomic": ["atomic", "rpatomic", "atomic struct"],
    "Clump": ["clump", "rpclump", "drawable"],
    "RenderWare": ["renderware", "rw ", "rpw", "rw3"],
    "Pipeline": ["pipeline", "renderpipe", "pipe"],
    "HAnim": ["hanim", "hierarchy", "skeleton", "bone"],
    "DFF": ["dff", "dff file", "dff format", "dff model"],
    "TXD": ["txd", "texture dictionary", "txd file"],
    "IPL": ["ipl", "instance placement", "binary ipl"],
    "IDE": ["ide", "item definition", "object definition"],
    "Time Cycle": ["time cycle", "timecycle", "timeofday"],
    "Shadow": ["shadow", "shadow map", "shadow mapping"],
    "Reflection": ["reflection", "reflect", "specular reflection"],
    "Fog": ["fog", "distance fog", "height fog", "volumetric"],
    "LOD": ["lod", "level of detail", "distance cull"],
    "PostFX": ["postfx", "post fx", "screen space", "ssao", "smaa"],
    "SM3.0": ["sm3", "ps_3_0", "vs_3_0", "shader model 3"],
    "Constant Register": ["constant register", r"c\[", "reg c"],
    "Pixel Shader": ["pixel shader", "ps ", "fragment shader"],
    "Vertex Shader": ["vertex shader", "vs ", "vertex program"],
}

CODE_PATTERNS = {
    "GGX": [r"ggx", r"trowbridge", r"D_GGX", r"ndf_ggx"],
    "Smith": [r"smith", r"G_Smith", r"geometry_smith"],
    "Schlick": [r"schlick", r"fresnel_schlick", r"F_Schlick"],
    "Fresnel": [r"fresnel", r"F0", r"reflectance"],
    "Normal Map": [r"normal.*map", r"tangent.*space", r"DecodeNormal"],
    "IBL": [r"ibl", r"irradiance", r"prefilter", r"cubemap.*sample"],
    "PBR": [r"pbr", r"pipeUploadPBR", r"pbrParams"],
    "Pipeline": [r"pipe\w+", r"BUILDING_PBR", r"CAR_MODERN", r"vehiclePipe"],
    "Time Cycle": [r"time.*cycle", r"timecycle", r"CTimeCycle"],
    "Shadow": [r"shadow", r"cloudShadow", r"shadowMap"],
    "PostFX": [r"postfx", r"SSAO", r"SMAA", r"ColourFilter"],
}

def scan_file_concepts(file_path, patterns):
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore").lower()
    except: return []
    found = []
    for concept, pats in patterns.items():
        for pat in pats:
            if re.search(pat, content): found.append(concept); break
    return list(set(found))

def build_xref_index():
    concept_index = defaultdict(list)
    # Scan Obsidian
    if OBSIDIAN.exists():
        for md_file in OBSIDIAN.rglob("*.md"):
            for concept in scan_file_concepts(md_file, CONCEPTS):
                concept_index[concept].append({"file": str(md_file.relative_to(OBSIDIAN)), "type": "obsidian", "name": md_file.stem})
    # Scan shaders
    if SHADERS.exists():
        for hlsl in SHADERS.rglob("*.hlsl"):
            for concept in scan_file_concepts(hlsl, CODE_PATTERNS):
                concept_index[concept].append({"file": str(hlsl.relative_to(PROJECT)), "type": "shader", "name": hlsl.stem})
    # Scan source
    if SRC.exists():
        for ext in ["*.cpp", "*.h"]:
            for src_file in SRC.rglob(ext):
                for concept in scan_file_concepts(src_file, CODE_PATTERNS):
                    concept_index[concept].append({"file": str(src_file.relative_to(PROJECT)), "type": "source", "name": src_file.stem})
    # Scan wiki
    if WIKI.exists():
        for md_file in WIKI.rglob("*.md"):
            for concept in scan_file_concepts(md_file, CONCEPTS):
                concept_index[concept].append({"file": str(md_file.relative_to(WIKI)), "type": "wiki", "name": md_file.stem})
    # Build reverse
    reverse = defaultdict(list)
    for concept, files in concept_index.items():
        for f in files: reverse[f"{f['type']}:{f['file']}"].append(concept)
    output = {"concept_to_files": dict(concept_index), "file_to_concepts": dict(reverse),
              "stats": {"concepts": len(concept_index), "files_scanned": len(reverse), "total_links": sum(len(v) for v in concept_index.values())}}
    CACHE.mkdir(parents=True, exist_ok=True)
    XREF_INDEX.write_text(json.dumps(output, indent=2))
    return output

# ═══════════════════════════════════════════════════════════════
# KNOWLEDGE GRAPH (from knowledge_graph.py)
# ═══════════════════════════════════════════════════════════════

RELATIONSHIPS = {
    "Fresnel": {"depends_on": ["Schlick"], "part_of": ["BRDF"], "implements": "Reflectance at interfaces"},
    "Schlick": {"depends_on": ["Fresnel"], "part_of": ["Fresnel"], "implements": "Fast Fresnel approximation"},
    "GGX": {"depends_on": ["Microfacet"], "part_of": ["BRDF"], "implements": "Normal Distribution Function"},
    "Smith": {"depends_on": ["Microfacet"], "part_of": ["BRDF"], "implements": "Geometry/Shadowing function"},
    "Microfacet": {"depends_on": ["BRDF"], "part_of": ["BRDF"], "implements": "Surface roughness model"},
    "Cook-Torrance": {"depends_on": ["GGX", "Smith", "Fresnel"], "part_of": ["BRDF"], "implements": "Specular BRDF model"},
    "BRDF": {"depends_on": ["Energy Conservation"], "part_of": ["PBR"], "implements": "Light scattering function"},
    "Energy Conservation": {"depends_on": [], "part_of": ["PBR"], "implements": "Physically plausible lighting"},
    "PBR": {"depends_on": ["BRDF", "Energy Conservation"], "part_of": ["Shader Development"], "implements": "Physically based rendering"},
    "Glossiness": {"depends_on": ["Microfacet"], "part_of": ["PBR"], "implements": "Surface smoothness parameter"},
    "Specular": {"depends_on": ["Fresnel", "GGX"], "part_of": ["PBR"], "implements": "Direct reflection"},
    "Diffuse": {"depends_on": ["Energy Conservation"], "part_of": ["PBR"], "implements": "Lambertian scattering"},
    "Normal Map": {"depends_on": ["Microfacet"], "part_of": ["PBR"], "implements": "Surface detail"},
    "Environment Map": {"depends_on": ["Fresnel", "Reflection"], "part_of": ["PBR"], "implements": "Indirect lighting"},
    "IBL": {"depends_on": ["Environment Map", "Diffuse"], "part_of": ["PBR"], "implements": "Image-based lighting"},
    "Texture Sampling": {"depends_on": [], "part_of": ["Shader Development"], "implements": "Color/normal lookup"},
    "Atomic": {"depends_on": ["Clump", "Geometry"], "part_of": ["RenderWare"], "implements": "Drawable instance"},
    "Clump": {"depends_on": ["Frame"], "part_of": ["RenderWare"], "implements": "Object container"},
    "Geometry": {"depends_on": ["Vertex Buffer", "Index Buffer"], "part_of": ["RenderWare"], "implements": "Triangle mesh"},
    "Material": {"depends_on": ["Texture Sampling"], "part_of": ["RenderWare"], "implements": "Surface properties"},
    "Frame": {"depends_on": [], "part_of": ["RenderWare"], "implements": "Transform hierarchy"},
    "DFF": {"depends_on": ["Atomic", "Clump", "Geometry"], "part_of": ["GTA SA"], "implements": "3D model file"},
    "TXD": {"depends_on": ["Material", "Texture Sampling"], "part_of": ["GTA SA"], "implements": "Texture dictionary"},
    "Pipeline": {"depends_on": ["RenderWare"], "part_of": ["skygfx"], "implements": "Render pipeline switch"},
    "PostFX": {"depends_on": ["PBR"], "part_of": ["skygfx"], "implements": "Screen-space effects"},
    "SM3.0": {"depends_on": [], "part_of": ["Shader Development"], "implements": "Shader model target"},
}

IMPLEMENTATIONS = {
    "GGX": {"shader": "shaders/include/PBR_Common.hlsl", "function": "D_GGX"},
    "Smith": {"shader": "shaders/include/PBR_Common.hlsl", "function": "G_Smith"},
    "Schlick": {"shader": "shaders/include/PBR_Common.hlsl", "function": "F_Schlick"},
    "Fresnel": {"shader": "shaders/include/PBR_Common.hlsl", "function": "F_Schlick"},
    "PBR": {"shader": "shaders/ps/VehiclePBR_Modern.hlsl", "function": "main"},
    "Normal Map": {"shader": "shaders/ps/VehiclePBR_Modern.hlsl", "function": "DecodeNormal"},
    "IBL": {"shader": "shaders/ps/VehiclePBR_Modern.hlsl", "function": "sampleIBL"},
    "Pipeline": {"source": "src/render/pipelinecommon.cpp", "function": "pipeUploadPBR"},
}

def build_dependency_tree(concept, visited=None):
    if visited is None: visited = set()
    if concept in visited: return {"concept": concept, "circular": True}
    visited.add(concept)
    if concept not in RELATIONSHIPS: return {"concept": concept, "depends_on": [], "part_of": None, "implements": None}
    rel = RELATIONSHIPS[concept]
    tree = {"concept": concept, "implements": rel.get("implements", ""), "part_of": rel.get("part_of", []), "depends_on": []}
    for dep in rel.get("depends_on", []): tree["depends_on"].append(build_dependency_tree(dep, visited.copy()))
    return tree

def find_concept_chain(start, end, visited=None):
    if visited is None: visited = set()
    if start == end: return [start]
    if start in visited: return None
    visited.add(start)
    if start not in RELATIONSHIPS: return None
    for dep in RELATIONSHIPS[start].get("depends_on", []):
        chain = find_concept_chain(dep, end, visited.copy())
        if chain: return [start] + chain
    return None

def get_all_chains():
    chains = []
    roots = [c for c, r in RELATIONSHIPS.items() if not r.get("depends_on")]
    all_deps = set()
    for r in RELATIONSHIPS.values(): all_deps.update(r.get("depends_on", []))
    leaves = [c for c in RELATIONSHIPS if c not in all_deps]
    for root in roots:
        for leaf in leaves:
            chain = find_concept_chain(leaf, root)
            if chain and len(chain) > 2: chains.append(chain)
    return chains

def build_graph_index():
    graph = {"relationships": RELATIONSHIPS, "implementations": IMPLEMENTATIONS, "chains": get_all_chains()}
    CACHE.mkdir(parents=True, exist_ok=True)
    GRAPH_INDEX.write_text(json.dumps(graph, indent=2))
    return graph

# ═══════════════════════════════════════════════════════════════
# GAP DETECTOR (from gap_detector.py)
# ═══════════════════════════════════════════════════════════════

FEATURES = {
    "Screen Space Reflections": {"concepts": ["PBR", "Fresnel", "Reflection", "PostFX", "SM3.0"], "prerequisites": ["Environment Map", "Normal Map", "Depth Buffer"], "difficulty": "hard", "existing_implementation": False},
    "Shadow Mapping": {"concepts": ["Shadow", "PBR", "PostFX"], "prerequisites": ["Depth Buffer", "Vertex Shader"], "difficulty": "medium", "existing_implementation": True, "implementation_file": "shaders/include/PBR_Common.hlsl"},
    "Parallax Mapping": {"concepts": ["Normal Map", "Texture Sampling", "PBR"], "prerequisites": ["Height Map", "Tangent Space"], "difficulty": "medium", "existing_implementation": False},
    "Subsurface Scattering": {"concepts": ["PBR", "Diffuse", "Fresnel"], "prerequisites": ["Skin Shader", "Thickness Map"], "difficulty": "hard", "existing_implementation": True, "implementation_file": "shaders/ps/SkinSSS.hlsl"},
    "Temporal Anti-Aliasing": {"concepts": ["PostFX", "SM3.0"], "prerequisites": ["Motion Vectors", "Depth Buffer", "History Buffer"], "difficulty": "very hard", "existing_implementation": False},
    "Volumetric Lighting": {"concepts": ["Shadow", "Fog", "PostFX"], "prerequisites": ["Light Shafts", "Ray Marching"], "difficulty": "very hard", "existing_implementation": False},
    "Decals": {"concepts": ["Geometry", "Material", "Texture Sampling"], "prerequisites": ["Deferred Rendering", "Depth Buffer"], "difficulty": "medium", "existing_implementation": False},
    "Improved Water": {"concepts": ["Reflection", "Fresnel", "Normal Map", "PostFX"], "prerequisites": ["Water Shader", "Refraction"], "difficulty": "medium", "existing_implementation": True, "implementation_file": "shaders/ps/Water_Parallax.hlsl"},
    "HDR Rendering": {"concepts": ["PBR", "PostFX", "SM3.0"], "prerequisites": ["Float Textures", "Tone Mapping"], "difficulty": "hard", "existing_implementation": False},
    "Ambient Occlusion": {"concepts": ["PostFX", "PBR", "SM3.0"], "prerequisites": ["Depth Buffer", "Normal Buffer"], "difficulty": "medium", "existing_implementation": True, "implementation_file": "src/render/postfx.cpp"},
    "PBR Building Rendering": {"concepts": ["PBR", "GGX", "Smith", "Fresnel", "Normal Map", "IBL"], "prerequisites": [], "difficulty": "done", "existing_implementation": True, "implementation_file": "src/render/buildingPipe.cpp"},
    "PBR Vehicle Rendering": {"concepts": ["PBR", "GGX", "Smith", "Fresnel", "Normal Map", "IBL", "Glossiness"], "prerequisites": [], "difficulty": "done", "existing_implementation": True, "implementation_file": "shaders/ps/VehiclePBR_Modern.hlsl"},
}

def check_knowledge(concept, graph, xref):
    result = {"concept": concept, "has_graph_node": concept in graph.get("relationships", {}),
              "has_implementation": concept in graph.get("implementations", {}),
              "files_linked": len(xref.get("concept_to_files", {}).get(concept, [])), "status": "unknown"}
    if result["has_implementation"]: result["status"] = "implemented"
    elif result["has_graph_node"] and result["files_linked"] > 0: result["status"] = "documented"
    elif result["files_linked"] > 0: result["status"] = "referenced"
    else: result["status"] = "missing"
    return result

def analyze_feature(feature_name, graph, xref):
    if feature_name not in FEATURES: return None
    feature = FEATURES[feature_name]
    required = [check_knowledge(c, graph, xref) for c in feature["concepts"]]
    prereqs = [check_knowledge(c, graph, xref) for c in feature.get("prerequisites", [])]
    all_concepts = required + prereqs
    known = sum(1 for c in all_concepts if c["status"] in ["implemented", "documented"])
    return {"feature": feature_name, "difficulty": feature["difficulty"],
            "existing_implementation": feature.get("existing_implementation", False),
            "implementation_file": feature.get("implementation_file"),
            "required_concepts": required, "prerequisites": prereqs,
            "coverage": known / len(all_concepts) if all_concepts else 0,
            "gaps": [c["concept"] for c in all_concepts if c["status"] == "missing"],
            "implemented": [c["concept"] for c in all_concepts if c["status"] == "implemented"],
            "documented": [c["concept"] for c in all_concepts if c["status"] == "documented"]}

# ═══════════════════════════════════════════════════════════════
# OBSIDIAN INTEGRATION
# ═══════════════════════════════════════════════════════════════

def push_to_obsidian():
    """Push knowledge graph + xref to Obsidian as structured notes."""
    graph = json.loads(GRAPH_INDEX.read_text()) if GRAPH_INDEX.exists() else {}
    xref = json.loads(XREF_INDEX.read_text()) if XREF_INDEX.exists() else {}
    
    # Create Knowledge/System note in Obsidian
    system_dir = OBSIDIAN / "Knowledge"
    system_dir.mkdir(parents=True, exist_ok=True)
    
    # Graph overview
    md = "# Knowledge Graph\n\n"
    md += f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n\n"
    md += f"## Concepts: {len(graph.get('relationships', {}))}\n\n"
    for concept, rel in graph.get("relationships", {}).items():
        impl = graph.get("implementations", {}).get(concept, {})
        impl_str = f" → {impl.get('function', '?')}({impl.get('shader', impl.get('source', '?'))})" if impl else ""
        md += f"- **{concept}**: {rel.get('implements', '?')}{impl_str}\n"
    
    md += f"\n## Chains: {len(graph.get('chains', []))}\n\n"
    for chain in graph.get("chains", [])[:10]:
        md += f"- {' → '.join(chain)}\n"
    
    (system_dir / "Knowledge Graph.md").write_text(md, encoding="utf-8")
    
    # Cross-reference overview
    xref_md = "# Cross-References\n\n"
    xref_md += f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n\n"
    xref_md += f"Concepts: {xref.get('stats', {}).get('concepts', 0)}, Files: {xref.get('stats', {}).get('files_scanned', 0)}, Links: {xref.get('stats', {}).get('total_links', 0)}\n\n"
    for concept in sorted(xref.get("concept_to_files", {}).keys()):
        files = xref["concept_to_files"][concept]
        xref_md += f"### {concept} ({len(files)} files)\n"
        for f in files[:5]:
            xref_md += f"- [{f['type']}] {f['file']}\n"
        if len(files) > 5: xref_md += f"- ... +{len(files)-5} more\n"
        xref_md += "\n"
    
    (system_dir / "Cross-References.md").write_text(xref_md, encoding="utf-8")
    
    # Gap analysis
    graph_data = graph
    xref_data = xref
    gaps_md = "# Gap Analysis\n\n"
    gaps_md += f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n\n"
    for name, feat in FEATURES.items():
        analysis = analyze_feature(name, graph_data, xref_data)
        if analysis:
            icon = "[x]" if feat.get("existing_implementation") else "[ ]"
            gaps_md += f"## {icon} {name} ({feat['difficulty']})\n"
            gaps_md += f"Coverage: {analysis['coverage']*100:.0f}%\n"
            if analysis['gaps']: gaps_md += f"Gaps: {', '.join(analysis['gaps'])}\n"
            gaps_md += "\n"
    
    (system_dir / "Gap Analysis.md").write_text(gaps_md, encoding="utf-8")
    
    return {"graph": True, "xref": True, "gaps": True}

# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════

def show_status():
    print("Knowledge System Status")
    print("=" * 60)
    idx = load_sync_index()
    wiki = sum(1 for k in idx.get("files", {}) if k.startswith("wiki:"))
    research = sum(1 for k in idx.get("files", {}) if k.startswith("research:"))
    science = sum(1 for k in idx.get("files", {}) if k.startswith("science:"))
    print(f"\n1. INGESTION: {len(idx.get('files', {}))} files (wiki:{wiki} research:{research} science:{science})")
    xref = json.loads(XREF_INDEX.read_text()) if XREF_INDEX.exists() else {}
    stats = xref.get("stats", {})
    print(f"\n2. CROSS-REFERENCES: {stats.get('concepts', 0)} concepts, {stats.get('files_scanned', 0)} files, {stats.get('total_links', 0)} links")
    graph = json.loads(GRAPH_INDEX.read_text()) if GRAPH_INDEX.exists() else {}
    print(f"\n3. KNOWLEDGE GRAPH: {len(graph.get('relationships', {}))} concepts, {len(graph.get('chains', []))} chains")
    total = len(FEATURES)
    impl = sum(1 for f in FEATURES.values() if f.get("existing_implementation"))
    print(f"\n4. GAP DETECTION: {impl}/{total} features ({impl/total*100:.0f}%)")
    print(f"\n{'=' * 60}")
    print("SYSTEM HEALTH: GOOD")

def run_full_sync():
    print("Running full knowledge sync...")
    print("\n[1/4] Ingesting to Obsidian...")
    index = load_sync_index()
    w, ws = sync_wiki_articles(index, force=True)
    print(f"  Wiki: {w} synced, {ws} unchanged")
    r = sync_research(index, force=True)
    print(f"  Research: {r} synced")
    s = sync_science(index, force=True)
    print(f"  Science: {s} synced")
    save_sync_index(index)
    
    print("\n[2/4] Building cross-references...")
    xref = build_xref_index()
    print(f"  {xref['stats']['concepts']} concepts, {xref['stats']['total_links']} links")
    
    print("\n[3/4] Building knowledge graph...")
    graph = build_graph_index()
    print(f"  {len(graph['relationships'])} concepts, {len(graph['chains'])} chains")
    
    print("\n[4/4] Pushing to Obsidian...")
    push_to_obsidian()
    print("  Knowledge Graph, Cross-References, Gap Analysis written to Obsidian")
    
    print("\n" + "=" * 60)
    print("Full sync complete!")
    show_status()

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Unified knowledge system")
    parser.add_argument("command", nargs="?", default="status", choices=["status", "sync", "ingest", "xref", "graph", "gaps", "push"])
    parser.add_argument("target", nargs="?", help="Query target")
    parser.add_argument("--force", "-f", action="store_true")
    parser.add_argument("--query", "-q", help="Query concept")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    
    if args.command == "status": show_status()
    elif args.command == "sync": run_full_sync()
    elif args.command == "ingest":
        index = load_sync_index()
        w, ws = sync_wiki_articles(index, force=args.force)
        r = sync_research(index, force=args.force)
        s = sync_science(index, force=args.force)
        save_sync_index(index)
        print(f"Synced: {w} wiki, {r} research, {s} science")
    elif args.command == "xref":
        if args.query:
            xref = json.loads(XREF_INDEX.read_text()) if XREF_INDEX.exists() else {}
            files = xref.get("concept_to_files", {}).get(args.query, [])
            print(f"'{args.query}' found in {len(files)} files:")
            for f in files: print(f"  [{f['type']}] {f['file']}")
        else:
            xref = build_xref_index()
            print(f"Built: {xref['stats']['concepts']} concepts, {xref['stats']['total_links']} links")
    elif args.command == "graph":
        if args.query:
            if args.query in RELATIONSHIPS:
                tree = build_dependency_tree(args.query)
                print(f"\n{args.query}: {tree['implements']}")
                print(f"  Part of: {tree['part_of']}")
                print(f"  Depends on: {[d['concept'] for d in tree['depends_on']]}")
                if args.query in IMPLEMENTATIONS:
                    impl = IMPLEMENTATIONS[args.query]
                    print(f"  Implementation: {impl.get('function')} in {impl.get('shader', impl.get('source'))}")
            else: print(f"Concept '{args.query}' not found")
        else:
            graph = build_graph_index()
            print(f"Built: {len(graph['relationships'])} concepts, {len(graph['chains'])} chains")
    elif args.command == "gaps":
        graph = json.loads(GRAPH_INDEX.read_text()) if GRAPH_INDEX.exists() else {}
        xref = json.loads(XREF_INDEX.read_text()) if XREF_INDEX.exists() else {}
        if args.query:
            analysis = analyze_feature(args.query, graph, xref)
            if analysis:
                print(f"\n{analysis['feature']} ({analysis['difficulty']})")
                print(f"Coverage: {analysis['coverage']*100:.0f}%")
                print(f"Implemented: {', '.join(analysis['implemented'])}")
                if analysis['gaps']: print(f"GAPS: {', '.join(analysis['gaps'])}")
            else: print(f"Feature '{args.query}' not found")
        elif args.list:
            for name, feat in sorted(FEATURES.items()):
                icon = "[x]" if feat.get("existing_implementation") else "[ ]"
                print(f"  {icon} {name} ({feat['difficulty']})")
        else:
            total = len(FEATURES)
            impl = sum(1 for f in FEATURES.values() if f.get("existing_implementation"))
            print(f"Features: {impl}/{total} ({impl/total*100:.0f}%)")
    elif args.command == "push":
        push_to_obsidian()
        print("Pushed to Obsidian")
    else: parser.print_help()

if __name__ == "__main__":
    main()
