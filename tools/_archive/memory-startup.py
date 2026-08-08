#!/usr/bin/env python3
"""
memory-startup.py — Dual-track memory system for skygfx_plus_expIV

DUAL TRACKS:
  1. SOLAR (Day): Logarithmic tiers — calendar-based consolidation
  2. LUNAR (Night): Moon phase-based — natural rhythm consolidation

  Day work → solar clock (logarithmic tiers)
  Night work → lunar clock (moon phases)

  Some memories consolidate faster (solar), others follow the slower
  lunar rhythm. Different weighting on recall based on which track.

SOLAR TIERS (Logarithmic Scale, base ~2):
  Tier          | Interval | Days  | Data Source
  --------------|----------|-------|------------------------------------------
  daily         | 1 day    | 1     | Memory bank + agent memory
  weekly        | 7 days   | 7     | Memory bank + agent memory
  monthly       | 30 days  | 30    | Git + memory bank
  3-month       | 90 days  | 90    | Git + memory bank (contrast pair A)
  4-month       | 120 days | 120   | Git + memory bank (contrast pair A)
  6-month       | 180 days | 180   | Git + memory bank (contrast pair B)
  8-month       | 240 days | 240   | Git + memory bank (contrast pair B)
  yearly        | 365 days | 365   | Git + memory bank
  18-month      | 545 days | 545   | Git history
  2-year        | 730 days | 730   | Git history
  3-year        | 1095 days| 1095  | Git history
  5-year        | 1825 days| 1825  | Git history + aap/skygfx
  8-year        | 2920 days| 2920  | Git history + aap/skygfx + plugin-sdk
  10-year       | 3650 days| 3650  | Plugin-sdk + librw + RW SDK docs
  20-year       | 7300 days| 7300  | RW SDK docs + changelogs

LUNAR TIERS (Moon Phase, ~29.5 day cycle):
  Phase          | Day Range | Purpose
  ---------------|-----------|------------------------------------------
  new_moon       | 0-3.7     | Fresh impressions, raw events
  waxing_crescent| 3.7-7.4   | Early patterns forming
  first_quarter  | 7.4-11.1  | First consolidation
  waxing_gibbous | 11.1-14.8 | Building toward peak
  full_moon      | 14.8-18.5 | Peak clarity, review
  waning_gibbous | 18.5-22.2 | Reflection, integration
  third_quarter  | 22.2-25.8 | Deep consolidation
  waning_crescent| 25.8-29.5 | Release, prepare for new

CONTRAST PAIRS (Solar):
  - 3-month vs 4-month: Compare quarterly vs slightly-over-quarterly
  - 6-month vs 8-month: Compare half-year vs slightly-over-half-year

Usage:
    python tools/memory-startup.py --compact        # <500 tokens
    python tools/memory-startup.py --tier 3month    # Specific solar tier
    python tools/memory-startup.py --lunar          # Lunar phase status
    python tools/memory-startup.py --all            # All tiers
    python tools/memory-startup.py --contrast       # Show contrast pairs
"""
import os
import sys
import json
from pathlib import Path
from datetime import datetime, timedelta

PROJECT_ROOT = Path(__file__).resolve().parent.parent
MEMORY_BANK = PROJECT_ROOT / "memory-bank"
CACHE_DIR = MEMORY_BANK / ".cache"
EXTERNAL_DIR = CACHE_DIR / "external-repos"
VAULT_DIR = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
RW_SDK_DIR = Path("E:/SDKs/RW36_031126")
RW_SDK37_DIR = Path("E:/SDKs/Renderware 3.7 SDK (For Windows) Full")

# Logarithmic tier definitions: (name, days, description)
LOG_TIERS = [
    ("daily", 1, "Today's work"),
    ("weekly", 7, "Last 7 days"),
    ("monthly", 30, "Last month"),
    ("3month", 90, "Last quarter (contrast pair A)"),
    ("4month", 120, "Last 4 months (contrast pair A)"),
    ("6month", 180, "Last half-year (contrast pair B)"),
    ("8month", 240, "Last 8 months (contrast pair B)"),
    ("yearly", 365, "Last year"),
    ("18month", 545, "Last 18 months"),
    ("2year", 730, "Last 2 years"),
    ("3year", 1095, "Last 3 years"),
    ("5year", 1825, "Last 5 years"),
    ("8year", 2920, "Last 8 years"),
    ("10year", 3650, "Last decade"),
    ("20year", 7300, "Last 2 decades"),
]

TIER_NAMES = [t[0] for t in LOG_TIERS]

# Lunar phase definitions
MOON_CYCLE_DAYS = 29.53058770576
REFERENCE_NEW_MOON = datetime(2000, 1, 6, 18, 14, 0)

LUNAR_PHASES = [
    ("new_moon",        0.00,  3.69,  "🌑", "Fresh impressions, raw events"),
    ("waxing_crescent", 3.69,  7.38,  "🌒", "Early patterns forming"),
    ("first_quarter",   7.38,  11.07, "🌓", "First consolidation checkpoint"),
    ("waxing_gibbous",  11.07, 14.76, "🌔", "Building toward peak clarity"),
    ("full_moon",       14.76, 18.46, "🌕", "Peak clarity, full review"),
    ("waning_gibbous",  18.46, 22.15, "🌖", "Reflection, deeper understanding"),
    ("third_quarter",   22.15, 25.84, "🌗", "Deep consolidation, extract patterns"),
    ("waning_crescent", 25.84, 29.53, "🌘", "Release, prepare for renewal"),
]

def moon_phase_days(date=None):
    """Calculate days into the current moon cycle."""
    if date is None:
        date = datetime.now()
    delta = date - REFERENCE_NEW_MOON
    total_days = delta.total_seconds() / 86400.0
    return total_days % MOON_CYCLE_DAYS

def get_lunar_phase_info(phase_days):
    """Get full lunar phase info from days into cycle."""
    for name, start, end, emoji, description in LUNAR_PHASES:
        if start <= phase_days < end:
            progress = (phase_days - start) / (end - start)
            return {
                "name": name,
                "emoji": emoji,
                "description": description,
                "day_in_phase": phase_days - start,
                "phase_length": end - start,
                "progress": progress,
                "day_in_cycle": phase_days,
                "cycle_progress": phase_days / MOON_CYCLE_DAYS,
            }
    return LUNAR_PHASES[0]

def get_lunar_context():
    """Get current lunar phase context for memory consolidation."""
    phase_days = moon_phase_days()
    info = get_lunar_phase_info(phase_days)
    
    # Determine memory track based on time of day
    now = datetime.now()
    hour = now.hour
    is_night = hour < 6 or hour >= 20  # Night: 8pm-6am
    
    track = "lunar" if is_night else "solar"
    track_label = f"{info['emoji']} {info['name'].replace('_', ' ').title()}" if is_night else "☀️ Solar (day)"
    
    return {
        "track": track,
        "track_label": track_label,
        "phase": info["name"],
        "emoji": info["emoji"],
        "day_in_cycle": round(phase_days, 1),
        "cycle_progress": f"{info['cycle_progress'] * 100:.1f}%",
        "phase_progress": f"{info['progress'] * 100:.0f}%",
        "description": info["description"],
    }

def format_lunar_status():
    """Format lunar phase status for display."""
    lunar = get_lunar_context()
    lines = [
        f"=== DUAL-TRACK MEMORY STATUS ===",
        f"",
        f"Active Track: {lunar['track_label']}",
        f"  Phase: {lunar['emoji']} {lunar['phase'].replace('_', ' ').title()}",
        f"  Day {lunar['day_in_cycle']} of {MOON_CYCLE_DAYS:.1f} cycle",
        f"  Phase progress: {lunar['phase_progress']}",
        f"  Cycle progress: {lunar['cycle_progress']}",
        f"  {lunar['description']}",
        f"",
        f"Lunar Memory Weights:",
        f"  new_moon: 1.0x (peak freshness)",
        f"  waxing: 0.8x (building)",
        f"  full_moon: 0.9x (peak clarity)",
        f"  waning: 0.7x (consolidating)",
        f"  third_quarter: 0.6x (deep work)",
    ]
    return "\n".join(lines)

def estimate_tokens(text):
    return len(text) // 4

def get_git_commits(since_days, until_days=0, repo_path=None):
    """Get git commits within a day range."""
    if repo_path is None:
        repo_path = PROJECT_ROOT
    
    since_date = (datetime.now() - timedelta(days=since_days)).strftime("%Y-%m-%d")
    if until_days > 0:
        until_date = (datetime.now() - timedelta(days=until_days)).strftime("%Y-%m-%d")
        until_arg = f"--until={until_date}"
    else:
        until_arg = ""
    
    cmd = ["git", "log", "--all", "--oneline", f"--since={since_date}"]
    if until_arg:
        cmd.append(until_arg)
    
    import subprocess
    result = subprocess.run(cmd, cwd=repo_path, capture_output=True, text=True, 
                          encoding="utf-8", errors="replace")
    commits = [l.strip() for l in result.stdout.strip().split("\n") if l.strip()]
    return commits

def get_memory_bank_summary():
    """Get memory bank file summaries."""
    summary = []
    for md_file in sorted(MEMORY_BANK.glob("*.md")):
        if md_file.name == "README.md":
            continue
        with open(md_file, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        summary.append(f"  {md_file.name}: {len(lines)} lines")
    return summary

def get_tier_context(tier_name):
    """Get context for a specific logarithmic tier."""
    tier_info = next((t for t in LOG_TIERS if t[0] == tier_name), None)
    if not tier_info:
        return f"Unknown tier: {tier_name}"
    
    _, days, desc = tier_info
    context = []
    context.append(f"=== {tier_name.upper()} ({days} days) — {desc} ===")
    context.append("")
    
    # 20-year tier: RW SDK era
    if tier_name == "20year":
        context.append("--- RW SDK Era (2003-2006) ---")
        
        # RW 3.6 changelog
        changelog_path = RW_SDK_DIR / "changelog.txt"
        if changelog_path.exists():
            with open(changelog_path, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
            context.append(f"RW 3.6 Changelog ({len(lines)} lines, Nov 2001-Nov 2003):")
            context.append(f"  Key: D3D9 cubemaps, PS2 dithering, RpSkin bone weights,")
            context.append(f"  RpMatFX dual pass, RpUVAnim copying, camera textures")
            context.append(f"  Platforms: PS2, Xbox, D3D8, D3D9, OpenGL, GCN")
        
        # RW 3.7 docs
        docs_dir = RW_SDK37_DIR / "Documents"
        if docs_dir.exists():
            pdfs = list(docs_dir.glob("*.pdf"))
            context.append(f"\nRW 3.7 SDK Documents ({len(pdfs)} PDFs):")
            for pdf in pdfs:
                size_kb = pdf.stat().st_size // 1024
                context.append(f"  - {pdf.name} ({size_kb}KB)")
        
        # RW headers
        headers_dir = RW_SDK37_DIR / "RWSDK37" / "Graphics" / "rwsdk" / "include" / "d3d9"
        if headers_dir.exists():
            headers = list(headers_dir.glob("*.h"))
            context.append(f"\nRW 3.7 Headers ({len(headers)} files):")
            context.append(f"  Core: rwcore.h, rwplcore.h, rwversion.h")
            context.append(f"  Plugins: rpworld.h, rphanim.h, rpskin.h, rpmatfx.h")
        
        return "\n".join(context)
    
    # 10-year tier: Post-RW era
    if tier_name == "10year":
        context.append("--- Post-RW Era (2006-2016) ---")
        
        plugin_sdk = Path("E:/SDKs/plugin-sdk-master")
        if plugin_sdk.exists():
            rw_dir = plugin_sdk / "plugin_sa" / "game_sa" / "rw"
            if rw_dir.exists():
                headers = list(rw_dir.glob("*.h"))
                context.append(f"plugin-sdk-master RW headers ({len(headers)} files):")
                for h in sorted(headers)[:10]:
                    context.append(f"  - {h.name}")
        
        librw = Path("E:/SDKs/librw")
        if librw.exists():
            src_dir = librw / "src"
            if src_dir.exists():
                files = list(src_dir.glob("*.cpp")) + list(src_dir.glob("*.h"))
                context.append(f"\nlibrw source ({len(files)} files):")
                context.append(f"  Key: camera.cpp, base.cpp, clump.cpp")
                context.append(f"  D3D9: d3d9.cpp, d3d9render.cpp, d3d9skin.cpp")
        
        return "\n".join(context)
    
    # 5-year tier: skygfx origins
    if tier_name == "5year":
        context.append("--- skygfx Origins (2017-2021) ---")
        
        skygfx_repo = EXTERNAL_DIR / "aap-skygfx"
        if skygfx_repo.exists():
            commits = get_git_commits(days, repo_path=skygfx_repo)
            context.append(f"aap/skygfx commits: {len(commits)}")
            for c in commits[:20]:
                context.append(f"  {c}")
        
        return "\n".join(context)
    
    # 3-year, 2-year, 18-month tiers: skygfx_plus evolution
    if tier_name in ("3year", "2year", "18month"):
        context.append(f"--- skygfx_plus Evolution ({days} days) ---")
        
        commits = get_git_commits(days)
        context.append(f"skygfx_plus commits: {len(commits)}")
        for c in commits[:15]:
            context.append(f"  {c}")
        
        return "\n".join(context)
    
    # Yearly, 8-month tiers: Git + memory bank
    if tier_name in ("yearly", "8month"):
        context.append(f"--- Recent Project ({days} days) ---")
        
        commits = get_git_commits(days)
        context.append(f"Git commits: {len(commits)}")
        
        context.append("\nMemory Bank:")
        context.extend(get_memory_bank_summary())
        
        return "\n".join(context)
    
    # Contrast pair tiers (3month, 4month, 6month): Git + memory bank
    if tier_name in ("3month", "4month", "6month"):
        context.append(f"--- Contrast Period ({days} days) ---")
        
        commits = get_git_commits(days)
        context.append(f"Git commits: {len(commits)}")
        for c in commits[:10]:
            context.append(f"  {c}")
        
        context.append("\nMemory Bank:")
        context.extend(get_memory_bank_summary())
        
        return "\n".join(context)
    
    # Recent tiers (monthly, weekly, daily): Git + memory bank + agent memory
    if tier_name in ("monthly", "weekly", "daily"):
        context.append(f"--- Recent Work ({days} days) ---")
        
        commits = get_git_commits(days)
        context.append(f"Git commits: {len(commits)}")
        
        context.append("\nMemory Bank:")
        context.extend(get_memory_bank_summary())
        
        # Agent memory summary
        try:
            from agentmemory import search_observations
            results = search_observations("", limit=5)
            if results:
                context.append("\nAgent Memory (recent):")
                for r in results[:5]:
                    context.append(f"  - {r.get('observation', '')[:80]}...")
        except:
            pass
        
        return "\n".join(context)
    
    return f"No handler for tier: {tier_name}"

def build_compact_reference():
    """Build ultra-compact reference (<500 tokens)"""
    lines = []
    lines.append("=== skygfx_plus_expIV Compact Reference ===")
    lines.append("")
    
    # Key files
    lines.append("Key Files:")
    lines.append("  postfx.cpp: SMAA(3037), ColourFilter(1535), UpdateFrontBuffer(241)")
    lines.append("  vehiclePipe.cpp: Vehicle callbacks")
    lines.append("  buildingPipe.cpp: Building callbacks")
    lines.append("  pipelinecommon.cpp: Shader loading, pipeUploadPBR")
    lines.append("  main.cpp: DLL entry, INI, hooks")
    lines.append("  skygfx.h: Config struct, enums")
    lines.append("")
    
    # Pipeline mapping
    lines.append("Pipeline Mapping:")
    lines.append("  PBR(0) -> building=3, vehicle=10, filter=MODERN(8)")
    lines.append("  PS2(1) -> building=0, vehicle=0")
    lines.append("  XBOX(2) -> building=1, vehicle=2")
    lines.append("  MOBILE(3) -> vehicle=4")
    lines.append("  GTAIV(4) -> building=2, vehicle=9")
    lines.append("")
    
    # Active issues
    lines.append("Active Issues:")
    lines.append("  1. SMAA black screen (RT state drift)")
    lines.append("  2. Forward+ CollectLights() stub")
    lines.append("  3. SSAO testing (fix-13 deployed)")
    lines.append("")
    
    # Build commands
    lines.append("Build:")
    lines.append("  python tools/fast_build.py")
    lines.append("  python tools/fast_build.py --shaders")
    lines.append("  python tools/fast_build.py --launch")
    
    return "\n".join(lines)

def show_contrast_pairs():
    """Show contrast pairs side by side."""
    pairs = [
        ("3month", "4month", "Quarterly vs 4-month"),
        ("6month", "8month", "Half-year vs 8-month"),
    ]
    
    for tier_a, tier_b, label in pairs:
        print(f"\n{'='*60}")
        print(f"CONTRAST PAIR: {label}")
        print(f"{'='*60}")
        print(f"\n--- {tier_a.upper()} ---")
        print(get_tier_context(tier_a))
        print(f"\n--- {tier_b.upper()} ---")
        print(get_tier_context(tier_b))

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Dual-track memory system (solar + lunar)")
    parser.add_argument("--compact", action="store_true", help="Ultra-compact reference (<500 tokens)")
    parser.add_argument("--tier", choices=TIER_NAMES, help="Specific solar tier")
    parser.add_argument("--lunar", action="store_true", help="Show lunar phase status")
    parser.add_argument("--all", action="store_true", help="All tiers")
    parser.add_argument("--contrast", action="store_true", help="Show contrast pairs")
    parser.add_argument("--list", action="store_true", help="List all tiers")
    args = parser.parse_args()
    
    if args.list:
        print("=== SOLAR TIERS (Logarithmic) ===")
        print("-" * 50)
        for name, days, desc in LOG_TIERS:
            print(f"  {name:12} | {days:5} days | {desc}")
        print()
        print("=== LUNAR TIERS (Moon Phase) ===")
        print("-" * 50)
        for name, start, end, emoji, desc in LUNAR_PHASES:
            print(f"  {name:18} | {emoji} | {desc}")
        return
    
    if args.compact:
        print(build_compact_reference())
        tokens = estimate_tokens(build_compact_reference())
        print(f"\nTotal: ~{tokens} tokens (Target: <500)")
        return
    
    if args.lunar:
        print(format_lunar_status())
        return
    
    if args.contrast:
        show_contrast_pairs()
        return
    
    if args.tier:
        tiers = [args.tier]
    elif args.all:
        tiers = TIER_NAMES
    else:
        tiers = ["compact"]
    
    for tier in tiers:
        if tier == "compact":
            print(build_compact_reference())
        else:
            print(get_tier_context(tier))

if __name__ == "__main__":
    main()
