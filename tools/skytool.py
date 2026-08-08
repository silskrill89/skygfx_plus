#!/usr/bin/env python3
"""
skytool — Unified CLI for skygfx_plus_expIV
Single entry point for all project tools: build, assets, knowledge, memory, research.
"""
import sys
import os

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

COMMANDS = {
    # Build
    "build": "Fast incremental build",
    "rebuild": "Clean full rebuild + deploy",
    "shaders": "Compile HLSL shaders only",
    "proto": "Prototype iteration (shader/code/ini/restart/kill/log)",
    "fixbuild": "Full build with pre-flight checks",
    
    # Assets
    "dff": "Analyze DFF model file",
    "scan": "Deep RW chunk scan (DFF/TXD)",
    "txd": "Build TXD from PNGs",
    "normalmap": "Generate normal map from diffuse",
    "envmap": "Generate environment maps",
    
    # Knowledge
    "ingest": "Sync wiki/research → Obsidian",
    "xref": "Cross-reference concepts",
    "graph": "Knowledge graph (concept relationships)",
    "gaps": "Gap detector (what do I need for X?)",
    "push": "Push knowledge graph to Obsidian",
    
    # Memory
    "mem": "Memory status/compact",
    "dream": "Build dream consolidation context",
    "moon": "Moon phase status",
    "optimize": "Optimize memory indexes",
    "procedure": "Run full memory audit procedure",
    
    # Research
    "yt": "YouTube transcript scraper",
    "research": "Research URL ingest pipeline",
    
    # System
    "status": "Overall system status",
    "sync": "Full sync: ingest + xref + graph",
    "help": "Show this help",
}

def show_help():
    print("skytool — Unified CLI for skygfx_plus_expIV")
    print("=" * 55)
    
    categories = {
        "Build": ["build", "rebuild", "shaders", "proto", "fixbuild"],
        "Assets": ["dff", "scan", "txd", "normalmap", "envmap"],
        "Knowledge": ["ingest", "xref", "graph", "gaps", "push"],
        "Memory": ["mem", "dream", "moon", "optimize", "procedure"],
        "Research": ["yt", "research"],
        "System": ["status", "sync", "help"],
    }
    
    for cat, cmds in categories.items():
        print(f"\n  {cat}:")
        for cmd in cmds:
            print(f"    {cmd:12s} {COMMANDS[cmd]}")
    
    print(f"\nUsage: skytool <command> [args...]")
    print(f"  Example: skytool build --fast")
    print(f"  Example: skytool gaps --query 'SSR'")
    print(f"  Example: skytool yt --search 'PBR rendering'")

def dispatch(cmd, args):
    # Build commands
    if cmd in ("build", "rebuild", "shaders"):
        from fast_build import main
        sys.argv = ["fast_build.py"] + (
            ["--rebuild"] if cmd == "rebuild" else
            ["--shaders"] if cmd == "shaders" else
            ["--fastest"] + args
        )
        return main()
    
    if cmd == "proto":
        from proto import main
        sys.argv = ["proto.py"] + args
        return main()
    
    if cmd == "fixbuild":
        from fix_build import main
        return main()
    
    # Asset commands
    if cmd in ("dff", "scan", "txd", "normalmap", "envmap"):
        from assets import main
        sys.argv = ["assets.py", cmd] + args
        return main()
    
    # Knowledge commands
    if cmd in ("ingest", "xref", "graph", "gaps", "push"):
        from knowledge import main
        sys.argv = ["knowledge.py", cmd] + args
        return main()
    
    # Memory commands
    if cmd in ("mem", "moon", "optimize"):
        from memory import main
        sys.argv = ["memory.py"] + (
            ["compact"] if cmd == "mem" else
            ["status"] if cmd == "moon" else
            [cmd] + args
        )
        return main()
    
    if cmd == "dream":
        from memory import generate_daily_dream, generate_weekly_dream
        dream_type = args[0] if args else "daily"
        if dream_type == "weekly":
            ctx = generate_weekly_dream()
        else:
            ctx = generate_daily_dream()
        print(f"Dream generated: {ctx['type']}")
        print(f"Lunar: {ctx['lunar']['phase_name']} (weight: {ctx['lunar']['weight']})")
        print(f"Knowledge: {ctx['knowledge'].get('xref_concepts', 0)} concepts, {ctx['knowledge'].get('timeline_coverage', 0)*100:.0f}% timeline")
        return 0
    
    if cmd == "procedure":
        return run_memory_procedure()
    
    # Research commands
    if cmd == "yt":
        from research import main
        sys.argv = ["research.py", "yt"] + args
        return main()
    
    if cmd == "research":
        from research import main
        sys.argv = ["research.py", "ingest"] + args
        return main()
    
    # System commands
    if cmd == "status":
        from knowledge import show_status
        return show_status()
    
    if cmd == "sync":
        from knowledge import run_full_sync
        return run_full_sync()
    
    if cmd == "help":
        return show_help()
    
    print(f"Unknown command: {cmd}")
    print("Run 'skytool help' for available commands")
    return 1

def run_memory_procedure():
    """Run full memory audit procedure."""
    from pathlib import Path
    from datetime import datetime
    import json
    
    PROJECT = Path(os.environ.get("SKYGFX_PROJECT", "E:/dev(dave)/skygfx_plus_expIV"))
    CACHE = PROJECT / "memory-bank/.cache"
    
    print("Memory Procedure — skygfx_plus_expIV")
    print("=" * 60)
    
    # Step 1: Core files
    print("\n[1/10] Core Memory Files")
    core_files = ["activeContext.md", "techContext.md", "progress.md", "systemPatterns.md", "productContext.md", "history.md"]
    for f in core_files:
        fp = PROJECT / "memory-bank" / f
        if fp.exists():
            mtime = datetime.fromtimestamp(fp.stat().st_mtime)
            age = (datetime.now() - mtime).days
            status = "FRESH" if age < 7 else "STALE"
            print(f"  {f}: {status} ({age}d old)")
        else:
            print(f"  {f}: MISSING")
    
    # Step 2: Cache indexes
    print("\n[2/10] Cache Indexes")
    indexes = {
        "compact-reference.md": "Compact reference",
        "knowledge-graph.json": "Knowledge graph",
        "cross-reference.json": "Cross-reference",
        "ingest-sync.json": "Ingest sync",
        "knowledge-timeline.md": "Knowledge timeline",
    }
    for f, desc in indexes.items():
        fp = CACHE / f
        if fp.exists():
            mtime = datetime.fromtimestamp(fp.stat().st_mtime)
            age = (datetime.now() - mtime).days
            status = "FRESH" if age < 1 else "STALE"
            print(f"  {desc}: {status} ({age}d old)")
        else:
            print(f"  {desc}: MISSING")
    
    # Step 3: External sources
    print("\n[3/10] External Sources")
    sources = {
        "H:/wikis/gtamods/rendering": "GTAMods Wiki",
        "E:/dev(dave)/SKYGFXPLUS_DOCS": "Obsidian Vault",
        str(PROJECT / "docs/research"): "Research Docs",
    }
    for path, name in sources.items():
        p = Path(path)
        if p.exists():
            count = len(list(p.rglob("*.md")))
            print(f"  {name}: {count} files")
        else:
            print(f"  {name}: MISSING")
    
    # Step 4: Knowledge stats
    print("\n[4/10] Knowledge System")
    try:
        xref = json.loads((CACHE / "cross-reference.json").read_text())
        graph = json.loads((CACHE / "knowledge-graph.json").read_text())
        ingest = json.loads((CACHE / "ingest-sync.json").read_text())
        print(f"  Cross-reference: {xref['stats']['concepts']} concepts, {xref['stats']['total_links']} links")
        print(f"  Knowledge graph: {len(graph['relationships'])} concepts, {len(graph['chains'])} chains")
        print(f"  Ingestion: {len(ingest['files'])} files tracked")
    except Exception as e:
        print(f"  Error loading indexes: {e}")
    
    # Step 5: Gap coverage
    print("\n[5/10] Gap Analysis")
    try:
        sys.path.insert(0, str(PROJECT / "tools/_archive"))
        from gap_detector import FEATURES
        total = len(FEATURES)
        impl = sum(1 for f in FEATURES.values() if f.get("existing_implementation"))
        print(f"  Features: {total}")
        print(f"  Implemented: {impl}/{total} ({impl/total*100:.0f}%)")
    except Exception as e:
        print(f"  Error: {e}")
    
    # Step 6: Timeline
    print("\n[6/10] Knowledge Timeline")
    timeline = CACHE / "knowledge-timeline.md"
    if timeline.exists():
        content = timeline.read_text()
        known = content.count("[x]")
        missing = content.count("[ ]")
        total = known + missing
        print(f"  Known: {known}/{total} ({known/total*100:.0f}%)")
        print(f"  Missing: {missing}")
    
    # Step 7: Agent memory
    print("\n[7/10] Agent Memory")
    try:
        # Check agentmemory via file
        print(f"  Status: Use agentmemory_memory_sessions() to check")
    except:
        print(f"  Status: Unknown")
    
    # Step 8: Staleness
    print("\n[8/10] Staleness Report")
    stale_count = 0
    for f in core_files + list(indexes.keys()):
        fp = PROJECT / "memory-bank" / f if f in core_files else CACHE / f
        if fp.exists():
            mtime = datetime.fromtimestamp(fp.stat().st_mtime)
            age = (datetime.now() - mtime).days
            if age > 7:
                stale_count += 1
    print(f"  Stale items: {stale_count}")
    
    # Step 9: Lunar
    print("\n[9/10] Lunar Phase")
    try:
        sys.path.insert(0, str(PROJECT / "tools"))
        from memory import format_lunar_status
        print(f"  {format_lunar_status()}")
    except Exception as e:
        print(f"  Error: {e}")
    
    # Step 10: Summary
    print("\n[10/10] Summary")
    print("=" * 60)
    print(f"  Core files: {len(core_files)} checked")
    print(f"  Cache indexes: {len(indexes)} checked")
    print(f"  External sources: {len(sources)} checked")
    print(f"  Stale items: {stale_count}")
    
    if stale_count > 0:
        print(f"\n  ACTION: Run 'skytool sync' to refresh stale indexes")
    
    print(f"\nProcedure complete at {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    return 0

def main():
    if len(sys.argv) < 2:
        show_help()
        return 0
    
    cmd = sys.argv[1]
    args = sys.argv[2:]
    
    return dispatch(cmd, args)

if __name__ == "__main__":
    sys.exit(main() or 0)
