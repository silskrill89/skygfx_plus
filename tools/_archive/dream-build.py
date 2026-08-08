#!/usr/bin/env python3
"""
dream-build.py — Build dream context from ALL sources with dual tracks

DUAL TRACKS:
  1. SOLAR (Day): Logarithmic tiers — calendar-based consolidation
  2. LUNAR (Night): Moon phase-based — natural rhythm consolidation

  Day work → solar clock (logarithmic tiers)
  Night work → lunar clock (moon phases)

Combines git history, documentation, external repos, and memory files.
Supports logarithmic tier system with contrast pairs AND lunar phases.

Usage:
    python tools/dream-build.py                    # Build from all sources
    python tools/dream-build.py --force            # Force rebuild (ignore cache)
    python tools/dream-build.py --days 30          # Only last N days
    python tools/dream-build.py --tier 3month      # Build for specific solar tier
    python tools/dream-build.py --lunar            # Build lunar phase context
    python tools/dream-build.py --dual             # Build both solar + lunar
    python tools/dream-build.py --contrast         # Build contrast pair analysis
    python tools/dream-build.py --output <path>    # Custom output path

Output: Comprehensive dream context file for memory consolidation.
"""
import subprocess
import json
import sys
import os
from pathlib import Path
from datetime import datetime, timedelta
from collections import defaultdict
import re

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = PROJECT_ROOT / "memory-bank" / ".cache"
MEMORY_BANK = PROJECT_ROOT / "memory-bank"
EXTERNAL_DIR = CACHE_DIR / "external-repos"

# Logarithmic tier definitions
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
    is_night = hour < 6 or hour >= 20
    
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

def run_git(args, cwd=None):
    """Run git command and return output."""
    result = subprocess.run(
        ["git"] + args,
        cwd=cwd or PROJECT_ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    return result.stdout.strip()

def build_git_timeline(days=None):
    """Build timeline from git commits."""
    cmd = ["log", "--all", "--format=%H|%ai|%s"]
    if days:
        since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
        cmd.extend(["--since", since])
    
    output = run_git(cmd)
    commits = []
    for line in output.split("\n"):
        if "|" in line:
            parts = line.split("|", 2)
            if len(parts) == 3:
                commits.append({
                    "hash": parts[0][:8],
                    "date": parts[1][:10],
                    "message": parts[2]
                })
    
    # Group by date
    daily = defaultdict(list)
    for c in commits:
        daily[c["date"]].append(c)
    
    # Extract feature areas from commit messages
    features = defaultdict(int)
    for c in commits:
        msg = c["message"]
        if ":" in msg:
            feature = msg.split(":")[0].strip()
            features[feature] += 1
    
    return {
        "total_commits": len(commits),
        "date_range": {
            "start": commits[-1]["date"] if commits else None,
            "end": commits[0]["date"] if commits else None
        },
        "daily_activity": {
            date: {
                "count": len(comms),
                "messages": [c["message"] for c in comms[:5]]
            }
            for date, comms in sorted(daily.items(), reverse=True)[:14]
        },
        "feature_areas": dict(sorted(features.items(), key=lambda x: -x[1])[:15])
    }

def analyze_external_repos():
    """Analyze cloned external repositories."""
    repos = []
    if EXTERNAL_DIR.exists():
        for repo_dir in EXTERNAL_DIR.iterdir():
            if repo_dir.is_dir() and (repo_dir / ".git").exists():
                # Get commit history
                output = run_git(
                    ["log", "--all", "--format=%ai|%s", "--since=2015-01-01"],
                    cwd=repo_dir
                )
                
                commits = []
                for line in output.split("\n"):
                    if "|" in line:
                        parts = line.split("|", 1)
                        if len(parts) == 2:
                            commits.append({
                                "date": parts[0][:10],
                                "message": parts[1]
                            })
                
                # Get file count
                file_count = len(list(repo_dir.rglob("*"))) if repo_dir.exists() else 0
                
                repos.append({
                    "name": repo_dir.name,
                    "commit_count": len(commits),
                    "file_count": file_count,
                    "date_range": {
                        "start": commits[-1]["date"] if commits else None,
                        "end": commits[0]["date"] if commits else None
                    },
                    "key_commits": [c["message"] for c in commits[:10]]
                })
    
    return repos

def read_memory_bank():
    """Read current memory bank state."""
    files = {}
    for md_file in MEMORY_BANK.glob("*.md"):
        if md_file.name != "README.md":
            content = md_file.read_text(encoding="utf-8", errors="replace")
            files[md_file.name] = {
                "lines": len(content.split("\n")),
                "words": len(content.split()),
                "first_section": content[:500]
            }
    return files

def get_tier_context(tier_name):
    """Get tier-specific context."""
    tier_info = next((t for t in LOG_TIERS if t[0] == tier_name), None)
    if not tier_info:
        return f"Unknown tier: {tier_name}"
    
    _, days, desc = tier_info
    context = []
    context.append(f"=== {tier_name.upper()} ({days} days) — {desc} ===")
    context.append("")
    
    # Get git commits for this tier
    cmd = ["log", "--all", "--oneline"]
    since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
    cmd.extend(["--since", since])
    
    output = run_git(cmd)
    commits = [l.strip() for l in output.strip().split("\n") if l.strip()]
    
    context.append(f"Git commits: {len(commits)}")
    for c in commits[:10]:
        context.append(f"  {c}")
    
    return "\n".join(context)

def build_contrast_analysis(tier_a, tier_b):
    """Build contrast pair analysis."""
    context_a = get_tier_context(tier_a)
    context_b = get_tier_context(tier_b)
    
    analysis = []
    analysis.append(f"=== CONTRAST PAIR ANALYSIS: {tier_a.upper()} vs {tier_b.upper()} ===")
    analysis.append("")
    analysis.append("--- TIER A ---")
    analysis.append(context_a)
    analysis.append("")
    analysis.append("--- TIER B ---")
    analysis.append(context_b)
    analysis.append("")
    analysis.append("=== ANALYSIS QUESTIONS ===")
    analysis.append("1. What trends cross natural boundaries?")
    analysis.append("2. Are there acceleration/deceleration patterns?")
    analysis.append("3. Which tier provides better signal-to-noise?")
    analysis.append("4. What changed in the extra time period?")
    
    return "\n".join(analysis)

def generate_dream_context(timeline, repos, memory_files, docs_context=None, tier=None, lunar=False):
    """Generate comprehensive dream context."""
    lines = []
    
    lines.append("# Dream Context — Built from All Sources")
    lines.append(f"Generated: {datetime.now().isoformat()[:10]}")
    if tier:
        lines.append(f"Logarithmic Tier: {tier}")
    if lunar:
        lunar_ctx = get_lunar_context()
        lines.append(f"Lunar Phase: {lunar_ctx['emoji']} {lunar_ctx['phase'].replace('_', ' ').title()}")
        lines.append(f"Active Track: {lunar_ctx['track_label']}")
    lines.append("")
    
    # Git timeline summary
    lines.append("## Project Timeline")
    lines.append(f"Commits: {timeline['total_commits']} | "
                f"Range: {timeline['date_range']['start']} to {timeline['date_range']['end']}")
    lines.append("")
    
    lines.append("### Recent Activity (last 14 days)")
    for date, day in list(timeline["daily_activity"].items())[:7]:
        lines.append(f"**{date}** ({day['count']} commits):")
        for msg in day["messages"][:3]:
            lines.append(f"  - {msg}")
    lines.append("")
    
    lines.append("### Feature Areas (by commit count)")
    for feature, count in list(timeline["feature_areas"].items())[:10]:
        lines.append(f"- {feature}: {count} commits")
    lines.append("")
    
    # External repositories
    if repos:
        lines.append("## External Repository Context")
        for repo in repos:
            lines.append(f"### {repo['name']}")
            lines.append(f"Commits: {repo['commit_count']} | Files: {repo['file_count']}")
            lines.append(f"Range: {repo['date_range']['start']} to {repo['date_range']['end']}")
            if repo.get("key_commits"):
                lines.append("Key changes:")
                for msg in repo["key_commits"][:5]:
                    lines.append(f"  - {msg}")
            lines.append("")
    
    # Memory bank state
    lines.append("## Current Memory Bank State")
    for filename, info in memory_files.items():
        lines.append(f"- {filename}: {info['lines']} lines, {info['words']} words")
    lines.append("")
    
    # Documentation context if available
    if docs_context:
        lines.append("## External Documentation")
        lines.append(docs_context[:1000])
        lines.append("")
    
    # Logarithmic tier context
    if tier:
        lines.append("## Logarithmic Tier Context")
        lines.append(get_tier_context(tier))
        lines.append("")
    
    # Lunar phase context
    if lunar:
        lines.append("## Lunar Phase Context")
        lunar_ctx = get_lunar_context()
        lines.append(f"Phase: {lunar_ctx['emoji']} {lunar_ctx['phase'].replace('_', ' ').title()}")
        lines.append(f"Day: {lunar_ctx['day_in_cycle']} of {MOON_CYCLE_DAYS:.1f}")
        lines.append(f"Progress: {lunar_ctx['cycle_progress']}")
        lines.append(f"Memory Weight: {get_lunar_memory_weight(lunar_ctx['phase'])}")
        lines.append(f"Description: {lunar_ctx['description']}")
        lines.append("")
    
    # Consolidation hints
    lines.append("## Consolidation Hints")
    lines.append("- Focus on durable patterns, not transient state")
    lines.append("- Merge similar memories into single entries")
    lines.append("- Archive completed work, keep active issues prominent")
    lines.append("- Update technical context with latest build paths/commands")
    lines.append("- For contrast pairs: analyze trends that cross natural boundaries")
    if lunar:
        lines.append("- For lunar: phase-based memory weighting (new_moon=1.0x, full_moon=0.9x, waning=0.7x)")
    lines.append("")
    
    return "\n".join(lines)

def get_lunar_memory_weight(phase):
    """Get memory weight based on lunar phase."""
    weights = {
        "new_moon": 1.0,        # Peak freshness
        "waxing_crescent": 0.8, # Building
        "first_quarter": 0.85,  # Consolidating
        "waxing_gibbous": 0.9,  # Approaching peak
        "full_moon": 0.95,      # Peak clarity
        "waning_gibbous": 0.85, # Reflecting
        "third_quarter": 0.7,   # Deep work
        "waning_crescent": 0.6, # Release
    }
    return weights.get(phase, 0.8)

def main():
    args = sys.argv[1:]
    force = "--force" in args
    days = None
    output_path = None
    tier = None
    lunar = "--lunar" in args
    dual = "--dual" in args
    contrast = "--contrast" in args
    
    if "--days" in args:
        idx = args.index("--days")
        if idx + 1 < len(args):
            days = int(args[idx + 1])
    
    if "--tier" in args:
        idx = args.index("--tier")
        if idx + 1 < len(args):
            tier = args[idx + 1]
            if tier not in TIER_NAMES:
                print(f"Unknown tier: {tier}")
                print(f"Available tiers: {', '.join(TIER_NAMES)}")
                return
    
    if "--output" in args:
        idx = args.index("--output")
        if idx + 1 < len(args):
            output_path = Path(args[idx + 1])
    
    print("=" * 60)
    print("Building Dream Context from ALL Sources")
    if tier:
        print(f"Logarithmic Tier: {tier}")
    if lunar:
        lunar_ctx = get_lunar_context()
        print(f"Lunar Phase: {lunar_ctx['emoji']} {lunar_ctx['phase'].replace('_', ' ').title()}")
    if dual:
        print("Dual-Track Mode (Solar + Lunar)")
    if contrast:
        print("Contrast Pair Analysis Mode")
    print("=" * 60)
    
    # Check cache
    cache_file = CACHE_DIR / "dream-context-full.md"
    if not force and cache_file.exists():
        cache_age = datetime.now().timestamp() - cache_file.stat().st_mtime
        if cache_age < 3600:  # Less than 1 hour old
            print(f"Using cached dream context ({int(cache_age/60)} minutes old)")
            print("Use --force to rebuild")
            print(cache_file.read_text())
            return
    
    # Build timeline
    print("\n[1/4] Building git timeline...")
    timeline = build_git_timeline(days)
    print(f"  {timeline['total_commits']} commits from {timeline['date_range']['start']} to {timeline['date_range']['end']}")
    
    # Analyze external repos
    print("\n[2/4] Analyzing external repositories...")
    repos = analyze_external_repos()
    print(f"  {len(repos)} repositories found")
    
    # Read memory bank
    print("\n[3/4] Reading memory bank...")
    memory_files = read_memory_bank()
    print(f"  {len(memory_files)} memory files loaded")
    
    # Check for docs context
    docs_context = None
    doc_context_file = CACHE_DIR / "doc-dream-context.md"
    if doc_context_file.exists():
        docs_context = doc_context_file.read_text(encoding="utf-8")
        print("\n[4/4] Loaded documentation context")
    else:
        print("\n[4/4] No documentation context found (run doc-ingest.py first)")
    
    # Generate context
    print("\nGenerating dream context...")
    context = generate_dream_context(timeline, repos, memory_files, docs_context, tier, lunar)
    
    # Build contrast pair analysis if requested
    if contrast:
        print("\nBuilding contrast pair analysis...")
        contrast_pairs = [
            ("3month", "4month"),
            ("6month", "8month"),
        ]
        for tier_a, tier_b in contrast_pairs:
            analysis = build_contrast_analysis(tier_a, tier_b)
            context += "\n\n" + analysis
    
    # Save outputs
    output = output_path or cache_file
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(context)
    print(f"\nDream context saved to: {output}")
    
    # Also save full JSON
    full_data = {
        "timeline": timeline,
        "repos": repos,
        "memory_files": memory_files,
        "tier": tier,
        "lunar": lunar,
        "generated": datetime.now().isoformat()
    }
    json_path = CACHE_DIR / "dream-context-full.json"
    json_path.write_text(json.dumps(full_data, indent=2, default=str))
    print(f"Full data saved to: {json_path}")
    
    # Print summary
    print("\n" + "=" * 60)
    print(context)
    
    print("\n" + "=" * 60)
    print("Done! Use this context with /dream to consolidate memory.")

if __name__ == "__main__":
    main()
