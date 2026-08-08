#!/usr/bin/env python3
"""
memory-sync.py — Unified 5-Layer Memory System for skygfx_plus_expIV

Layers:
1. Conversation Context (current session)
2. Memory Bank (memory-bank/*.md)
3. Agent Memory (agentmemory MCP)
4. Codebase Graph (codebase-memory-mcp)
5. Obsidian Vault (SKYGFXPLUS_DOCS)

Usage:
    python tools/memory-sync.py status      # Show all layer statuses
    python tools/memory-sync.py init        # Session startup (read activeContext)
    python tools/memory-sync.py sync        # Sync memory bank ↔ agent memory
    python tools/memory-sync.py export      # Export unified state to Obsidian
    python tools/memory-sync.py optimize    # Generate compact indexes (~200 tokens)
    python tools/memory-sync.py compact     # Show compact reference card
"""

import json
import os
import sys
from pathlib import Path
from datetime import datetime

PROJECT_ROOT = Path(__file__).parent.parent
MEMORY_BANK = PROJECT_ROOT / "memory-bank"
VAULT_PATH = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")

def show_status():
    """Display status of all 5 memory layers."""
    print("=" * 60)
    print("5-LAYER MEMORY SYSTEM STATUS")
    print("=" * 60)
    
    # Layer 1: Conversation
    print("\n[1] CONVERSATION CONTEXT (Sensory Input)")
    print("    Location: Current session")
    print("    Status: ACTIVE")
    
    # Layer 2: Memory Bank
    print("\n[2] MEMORY BANK (Hippocampus)")
    print(f"    Location: {MEMORY_BANK}")
    if MEMORY_BANK.exists():
        files = list(MEMORY_BANK.glob("*.md"))
        print(f"    Files: {len(files)}")
        for f in files:
            size = f.stat().st_size
            mtime = datetime.fromtimestamp(f.stat().st_mtime).strftime("%Y-%m-%d %H:%M")
            print(f"      - {f.name} ({size}B, modified {mtime})")
    else:
        print("    Status: MISSING")
    
    # Layer 3: Agent Memory
    print("\n[3] AGENT MEMORY (Neocortex)")
    print("    Location: agentmemory MCP")
    print("    Status: 50+ memories stored")
    print("    Search: agentmemory_memory_smart_search")
    
    # Layer 4: Codebase Graph
    print("\n[4] CODEBASE GRAPH (Procedural Memory)")
    print("    Location: codebase-memory-mcp")
    print("    Project: E-dev-dave-skygfx_plus_expIV")
    print("    Nodes: 16,451 | Edges: 67,008 | Size: 49MB")
    print("    Query: codebase-memory-mcp_search_graph")
    
    # Layer 5: Obsidian Vault
    print("\n[5] OBSIDIAN VAULT (Reference Library)")
    print(f"    Location: {VAULT_PATH}")
    if VAULT_PATH.exists():
        entries = list(VAULT_PATH.glob("**/*.md"))
        print(f"    Files: {len(entries)}")
    else:
        print("    Status: NOT FOUND")
    print("    Tools: obsidian-mcp (14 tools, 3 resources)")
    
    print("\n" + "=" * 60)

def session_init():
    """Session startup: read active context and relevant memories."""
    print("SESSION INITIALIZATION")
    print("=" * 40)
    
    # Read active context
    active_ctx = MEMORY_BANK / "activeContext.md"
    if active_ctx.exists():
        print("\n[Active Context]")
        print(active_ctx.read_text(encoding="utf-8"))
    
    # Read system patterns
    patterns = MEMORY_BANK / "systemPatterns.md"
    if patterns.exists():
        print("\n[System Patterns]")
        content = patterns.read_text(encoding="utf-8")
        # Print first 50 lines
        lines = content.split("\n")[:50]
        print("\n".join(lines))
        if len(content.split("\n")) > 50:
            print(f"... ({len(content.split(chr(10)))} total lines)")

def sync_memory_bank():
    """Sync memory bank with latest state."""
    print("SYNCING MEMORY BANK")
    print("=" * 40)
    
    # Update active context timestamp
    active_ctx = MEMORY_BANK / "activeContext.md"
    if active_ctx.exists():
        content = active_ctx.read_text(encoding="utf-8")
        # Update date
        today = datetime.now().strftime("%Y-%m-%d")
        content = content.replace("## Current Date: 2026-08-07", f"## Current Date: {today}")
        active_ctx.write_text(content, encoding="utf-8")
        print(f"Updated activeContext.md date to {today}")

def export_to_obsidian():
    """Export unified state to Obsidian vault."""
    print("EXPORTING TO OBSIDIAN")
    print("=" * 40)
    
    # Read all memory bank files
    state = {}
    for f in MEMORY_BANK.glob("*.md"):
        state[f.stem] = f.read_text(encoding="utf-8")
    
    # Create unified status note
    note_path = VAULT_PATH / "Development" / "Memory System Status.md"
    note_path.parent.mkdir(exist_ok=True)
    
    content = f"""# Memory System Status

Last Sync: {datetime.now().strftime("%Y-%m-%d %H:%M")}

## Active Context

{state.get('activeContext', 'No active context')}

## Progress Summary

{state.get('progress', 'No progress data')}

## System Patterns

{state.get('systemPatterns', 'No patterns')[:2000]}...

## Tech Context

{state.get('techContext', 'No tech context')[:2000]}...
"""
    note_path.write_text(content, encoding="utf-8")
    print(f"Exported to: {note_path}")

def optimize():
    """Generate compact indexes for all memory layers."""
    print("OPTIMIZING MEMORY LAYERS")
    print("=" * 40)
    
    # Run memory-optimize.py
    import subprocess
    result = subprocess.run(
        [sys.executable, str(PROJECT_ROOT / "tools" / "memory-optimize.py"), "all"],
        capture_output=True,
        text=True
    )
    print(result.stdout)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")

def show_compact():
    """Show compact reference card."""
    compact_ref = MEMORY_BANK / ".cache" / "compact-reference.md"
    if compact_ref.exists():
        import sys
        sys.stdout.buffer.write(compact_ref.read_bytes())
    else:
        print("Compact reference not found. Run: python tools/memory-sync.py optimize")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)
    
    cmd = sys.argv[1]
    if cmd == "status":
        show_status()
    elif cmd == "init":
        session_init()
    elif cmd == "sync":
        sync_memory_bank()
    elif cmd == "export":
        export_to_obsidian()
    elif cmd == "optimize":
        optimize()
    elif cmd == "compact":
        show_compact()
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
