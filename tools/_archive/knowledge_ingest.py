#!/usr/bin/env python3
"""
Knowledge Ingestion Pipeline
Syncs wiki articles, research docs, and science pillars into Obsidian vault + memory system.
Supports bidirectional sync, tagging, and cross-referencing.
"""
import os
import re
import json
import hashlib
import shutil
from pathlib import Path
from datetime import datetime

# Paths
PROJECT = Path("E:/dev(dave)/skygfx_plus_expIV")
OBSIDIAN = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
WIKI = Path("H:/wikis/gtamods/rendering")
RESEARCH = PROJECT / "docs/research"
SCIENCE = PROJECT / "memory-bank/.cache/science-pillars"
MEMORY = PROJECT / "memory-bank"
SYNC_INDEX = PROJECT / "memory-bank/.cache/ingest-sync.json"

# Category → Obsidian folder mapping
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
    """Extract YAML frontmatter from markdown."""
    if content.startswith("---"):
        end = content.find("---", 3)
        if end > 0:
            return content[3:end].strip(), content[end+3:]
    return None, content

def extract_wiki_categories(content):
    """Extract categories from wiki markdown."""
    match = re.search(r'categories:\s*(\[.*?\])', content)
    if match:
        try:
            return json.loads(match.group(1))
        except:
            pass
    return []

def infer_tags(title, content, categories):
    """Infer tags from content."""
    tags = []
    
    # From categories
    for cat in categories:
        if "RW" in cat or "RenderWare" in cat:
            tags.append("renderware")
        if "GTA" in cat:
            tags.append("gta")
        if "Format" in cat:
            tags.append("file-format")
    
    # From title keywords
    title_lower = title.lower()
    keyword_tags = {
        "shader": "shader", "hlsl": "shader", "pixel": "shader", "vertex": "shader",
        "texture": "texture", "raster": "texture", "txd": "texture",
        "material": "material", "reflect": "material",
        "geometry": "geometry", "mesh": "geometry", "vertex": "geometry",
        "collision": "collision", "bound": "collision",
        "light": "lighting", "ambient": "lighting", "shadow": "lighting",
        "camera": "camera", "frame": "transform",
        "atomic": "rendering", "pipeline": "rendering",
        "dff": "model-format", "ipl": "level-format", "ide": "definition",
        "pbr": "pbr", "brdf": "pbr", "ggx": "pbr",
        "water": "effects", "particle": "effects", "fx": "effects",
        "car": "vehicle", "vehicle": "vehicle", "ped": "character",
    }
    for kw, tag in keyword_tags.items():
        if kw in title_lower:
            tags.append(tag)
    
    return list(set(tags))

def wiki_to_obsidian(content, title, categories, source_path):
    """Convert wiki article to Obsidian-compatible format."""
    tags = infer_tags(title, content, categories)
    
    # Build frontmatter
    fm = {
        "title": title,
        "source": "gtamods.com",
        "source_path": str(source_path),
        "tags": tags,
        "created": datetime.now().strftime("%Y-%m-%d"),
    }
    if categories:
        fm["wiki_categories"] = categories
    
    # Build YAML
    yaml_lines = ["---"]
    for k, v in fm.items():
        if isinstance(v, list):
            yaml_lines.append(f"{k}:")
            for item in v:
                yaml_lines.append(f"  - {item}")
        else:
            yaml_lines.append(f"{k}: \"{v}\"")
    yaml_lines.append("---")
    yaml_lines.append("")
    
    # Add wikilinks for cross-referencing
    body = content
    # Convert [[link]] to Obsidian wikilinks
    body = re.sub(r'\[\[([^\]|]*?\|)?([^\]]*)\]\]', r'[[\2]]', body)
    
    return "\n".join(yaml_lines) + "\n" + body

def research_to_obsidian(content, title, source_path):
    """Convert research doc to Obsidian format."""
    tags = ["research"]
    title_lower = title.lower()
    if "youtube" in str(source_path).lower():
        tags.append("youtube")
    if any(k in title_lower for k in ["pbr", "brdf", "rendering"]):
        tags.append("rendering")
    
    fm = f"""---
title: "{title}"
source: "research"
source_path: "{source_path}"
tags: {json.dumps(tags)}
created: "{datetime.now().strftime('%Y-%m-%d')}"
---

"""
    return fm + content

def science_to_obsidian(content, title, source_path):
    """Convert science pillar to Obsidian format."""
    tags = ["science", "reference"]
    title_lower = title.lower()
    if "brdf" in title_lower or "pbr" in title_lower:
        tags.extend(["pbr", "brdf"])
    if "atmos" in title_lower:
        tags.append("atmosphere")
    if "history" in title_lower:
        tags.append("history")
    
    fm = f"""---
title: "{title}"
source: "science-pillar"
source_path: "{source_path}"
tags: {json.dumps(tags)}
created: "{datetime.now().strftime('%Y-%m-%d')}"
---

"""
    return fm + content

def sync_wiki_articles(index, force=False):
    """Sync wiki articles to Obsidian."""
    synced = 0
    skipped = 0
    
    for category_dir in WIKI.iterdir():
        if not category_dir.is_dir():
            continue
        
        obsidian_folder = CATEGORY_MAP.get(category_dir.name, "Reference/Other")
        target_base = OBSIDIAN / obsidian_folder
        target_base.mkdir(parents=True, exist_ok=True)
        
        for article in category_dir.glob("*.md"):
            key = f"wiki:{category_dir.name}/{article.name}"
            current_hash = file_hash(article)
            
            if not force and index["files"].get(key) == current_hash:
                skipped += 1
                continue
            
            # Read and convert
            content = article.read_text(encoding="utf-8")
            fm, body = extract_frontmatter(content)
            categories = extract_wiki_categories(body) if fm else []
            title = article.stem
            
            obsidian_content = wiki_to_obsidian(body, title, categories, article)
            
            # Save
            target = target_base / article.name
            target.write_text(obsidian_content, encoding="utf-8")
            
            index["files"][key] = current_hash
            synced += 1
    
    return synced, skipped

def sync_research(index, force=False):
    """Sync research docs to Obsidian."""
    synced = 0
    target_base = OBSIDIAN / "Research"
    target_base.mkdir(parents=True, exist_ok=True)
    
    for research_file in RESEARCH.rglob("*.md"):
        key = f"research:{research_file.relative_to(RESEARCH)}"
        current_hash = file_hash(research_file)
        
        if not force and index["files"].get(key) == current_hash:
            continue
        
        content = research_file.read_text(encoding="utf-8")
        title = research_file.stem
        
        obsidian_content = research_to_obsidian(content, title, research_file)
        
        # Maintain subfolder structure
        rel = research_file.relative_to(RESEARCH)
        target = target_base / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(obsidian_content, encoding="utf-8")
        
        index["files"][key] = current_hash
        synced += 1
    
    return synced

def sync_science(index, force=False):
    """Sync science pillars to Obsidian."""
    synced = 0
    target_base = OBSIDIAN / "Science"
    target_base.mkdir(parents=True, exist_ok=True)
    
    for science_file in SCIENCE.glob("*.md"):
        if science_file.name == "README.md":
            continue
        
        key = f"science:{science_file.name}"
        current_hash = file_hash(science_file)
        
        if not force and index["files"].get(key) == current_hash:
            continue
        
        content = science_file.read_text(encoding="utf-8")
        title = science_file.stem.replace("-", " ").title()
        
        obsidian_content = science_to_obsidian(content, title, science_file)
        
        target = target_base / science_file.name
        target.write_text(obsidian_content, encoding="utf-8")
        
        index["files"][key] = current_hash
        synced += 1
    
    return synced

def generate_wikilinks():
    """Scan all Obsidian files and add cross-reference wikilinks."""
    linked = 0
    
    # Build concept index
    concepts = {}
    for md_file in OBSIDIAN.rglob("*.md"):
        title = md_file.stem
        concepts[title.lower()] = md_file
    
    # Scan for cross-reference opportunities
    concept_keywords = {
        "ggx": ["GGX", "microfacet", "Normal Distribution Function"],
        "smith": ["Smith", "geometry function", "shadowing"],
        "schlick": ["Schlick", "Fresnel"],
        "fresnel": ["Fresnel", "reflectance", "F0"],
        "brdf": ["BRDF", "reflectance"],
        "pbr": ["PBR", "physically based"],
        "rw": ["RenderWare", "RW"],
        "dff": ["DFF", "model format"],
        "txd": ["TXD", "texture dictionary"],
        "atomic": ["Atomic", "RpAtomic"],
        "geometry": ["Geometry", "RpGeometry"],
        "material": ["Material", "RpMaterial"],
        "frame": ["Frame", "RwFrame"],
        "pipeline": ["pipeline", "render pipeline"],
    }
    
    return linked

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Knowledge Ingestion Pipeline")
    parser.add_argument("--force", "-f", action="store_true", help="Force re-sync all")
    parser.add_argument("--wiki-only", action="store_true", help="Sync wiki only")
    parser.add_argument("--research-only", action="store_true", help="Sync research only")
    parser.add_argument("--science-only", action="store_true", help="Sync science only")
    parser.add_argument("--status", action="store_true", help="Show sync status")
    args = parser.parse_args()
    
    index = load_sync_index()
    
    if args.status:
        print("Sync Status:")
        print(f"  Last sync: {index.get('last_sync', 'never')}")
        print(f"  Tracked files: {len(index['files'])}")
        
        wiki_count = sum(1 for k in index['files'] if k.startswith('wiki:'))
        research_count = sum(1 for k in index['files'] if k.startswith('research:'))
        science_count = sum(1 for k in index['files'] if k.startswith('science:'))
        print(f"  Wiki: {wiki_count}")
        print(f"  Research: {research_count}")
        print(f"  Science: {science_count}")
        
        # Count Obsidian files
        obsidian_count = len(list(OBSIDIAN.rglob("*.md")))
        print(f"\n  Obsidian vault: {obsidian_count} files")
        return
    
    print(f"Knowledge Ingestion Pipeline")
    print(f"{'='*50}")
    
    total_synced = 0
    
    if not args.research_only and not args.science_only:
        w, ws = sync_wiki_articles(index, args.force)
        print(f"  Wiki articles: {w} synced, {ws} unchanged")
        total_synced += w
    
    if not args.wiki_only and not args.science_only:
        r = sync_research(index, args.force)
        print(f"  Research docs: {r} synced")
        total_synced += r
    
    if not args.wiki_only and not args.research_only:
        s = sync_science(index, args.force)
        print(f"  Science pillars: {s} synced")
        total_synced += s
    
    save_sync_index(index)
    
    print(f"\n{'='*50}")
    print(f"Total: {total_synced} files synced")
    print(f"Obsidian vault: {OBSIDIAN}")

if __name__ == "__main__":
    main()
