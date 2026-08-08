#!/usr/bin/env python3
"""
memory.py — Unified memory system for skygfx_plus_expIV

Consolidates: memory-startup, memory-sync, memory-optimize, dream-build, moon_phase, git-history-dream

Commands:
  status      Show memory system status
  compact     Show compact reference (<500 tokens)
  tier <name> Show specific tier context
  lunar       Show lunar phase status
  dream       Build dream consolidation context
  optimize    Optimize memory indexes
  sync        Sync all memory layers
  history     Show git history timeline
"""
import os
import sys
import json
import math
import subprocess
from pathlib import Path
from datetime import datetime, timedelta
from collections import defaultdict

# Paths
PROJECT = Path(os.environ.get("SKYGFX_PROJECT", "E:/dev(dave)/skygfx_plus_expIV"))
MEMORY_BANK = PROJECT / "memory-bank"
CACHE = MEMORY_BANK / ".cache"
OBSIDIAN = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
EXTERNAL_REPOS = {
    "aap-skygfx": Path("E:/dev(dave)/aap-skygfx"),
    "plugin-sdk": Path("E:/dev(dave)/plugin-sdk"),
    "librw": Path("E:/dev(dave)/librw"),
}

# ═══════════════════════════════════════════════════════════════
# MOON / LUNAR (from moon_phase.py)
# ═══════════════════════════════════════════════════════════════

MOON_SYNODIC = 29.53058770576
PHASE_NAMES = [
    (0.0, "New Moon", "🌑"),
    (1.85, "Waxing Crescent", "🌒"),
    (3.70, "First Quarter", "🌓"),
    (5.55, "Waxing Gibbous", "🌔"),
    (7.38, "Full Moon", "🌕"),
    (9.23, "Waning Gibbous", "🌖"),
    (11.08, "Third Quarter", "🌗"),
    (12.92, "Waning Crescent", "🌘"),
    (14.76, "New Moon", "🌑"),
]

MOON_MEMORY_WEIGHTS = {
    "new_moon": 1.0,
    "waxing_crescent": 0.95,
    "first_quarter": 0.9,
    "waxing_gibbous": 0.85,
    "full_moon": 0.95,
    "waning_gibbous": 0.9,
    "third_quarter": 0.7,
    "waning_crescent": 0.6,
}

def moon_phase_days(date=None):
    if date is None:
        date = datetime.now()
    known_new = datetime(2000, 1, 6, 18, 14)
    diff = date - known_new
    return diff.total_seconds() / 86400 % MOON_SYNODIC

def get_lunar_phase_info(phase_days):
    phase_idx = int(phase_days / MOON_SYNODIC * 8) % 8
    name, emoji = PHASE_NAMES[phase_idx][1], PHASE_NAMES[phase_idx][2]
    progress = (phase_days % MOON_SYNODIC) / MOON_SYNODIC * 100
    return {"phase_days": phase_days, "phase_name": name, "emoji": emoji, "progress": progress, "weight": MOON_MEMORY_WEIGHTS.get(name.lower().replace(" ", "_"), 0.8)}

def format_lunar_status():
    pd = moon_phase_days()
    info = get_lunar_phase_info(pd)
    return f"{info['emoji']} {info['phase_name']} (day {pd:.1f}/29.5, {info['progress']:.0f}%)"

# ═══════════════════════════════════════════════════════════════
# GIT (from git-history-dream.py)
# ═══════════════════════════════════════════════════════════════

def run_git(args, cwd=None):
    if cwd is None:
        cwd = str(PROJECT)
    result = subprocess.run(["git"] + args, capture_output=True, text=True, cwd=cwd)
    return result.stdout.strip() if result.returncode == 0 else ""

def get_git_commits(since_days, until_days=0, repo_path=None):
    since = (datetime.now() - timedelta(days=since_days)).strftime("%Y-%m-%d")
    until = (datetime.now() - timedelta(days=until_days)).strftime("%Y-%m-%d")
    log = run_git(["log", f"--since={since}", f"--until={until}", "--oneline", "--no-merges"], cwd=repo_path)
    commits = []
    for line in log.split("\n"):
        if line.strip():
            parts = line.split(" ", 1)
            if len(parts) == 2:
                commits.append({"hash": parts[0], "message": parts[1]})
    return commits

def get_file_changes(days=30):
    since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
    log = run_git(["log", f"--since={since}", "--name-only", "--pretty=format:"])
    files = defaultdict(int)
    for line in log.split("\n"):
        if line.strip() and "." in line:
            ext = Path(line).suffix
            files[ext] += 1
    return dict(files)

# ═══════════════════════════════════════════════════════════════
# SOLAR TIERS (from memory-startup.py)
# ═══════════════════════════════════════════════════════════════

TIERS = [
    ("daily", 1), ("weekly", 7), ("monthly", 30),
    ("3month", 90), ("4month", 120), ("6month", 180), ("8month", 240),
    ("yearly", 365), ("18month", 545), ("2year", 730), ("3year", 1095),
    ("5year", 1825), ("8year", 2920), ("10year", 3650), ("20year", 7300),
]

CONTRAST_PAIRS = [("3month", "4month"), ("6month", "8month")]

def get_tier_context(tier_name):
    days = dict(TIERS).get(tier_name, 30)
    commits = get_git_commits(days)
    summary_parts = []
    if commits:
        summary_parts.append(f"{len(commits)} commits")
    if days > 365:
        summary_parts.append(f"spanning {days // 365}+ years")
    elif days > 30:
        summary_parts.append(f"spanning {days // 30} months")
    return {"tier": tier_name, "days": days, "commits": len(commits), "summary": ", ".join(summary_parts) if summary_parts else "no recent activity"}

def get_memory_bank_summary():
    summaries = {}
    for f in ["activeContext.md", "techContext.md", "progress.md", "systemPatterns.md"]:
        fp = MEMORY_BANK / f
        if fp.exists():
            content = fp.read_text(encoding="utf-8", errors="ignore")[:500]
            summaries[f] = content
    return summaries

def build_compact_reference():
    parts = ["# skygfx Compact Reference\n"]
    parts.append(f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    parts.append(f"Lunar: {format_lunar_status()}\n")
    parts.append("## Active Context")
    ctx = get_memory_bank_summary()
    if "activeContext.md" in ctx:
        parts.append(ctx["activeContext.md"][:300])
    parts.append("\n## Git Stats")
    commits_7d = get_git_commits(7)
    commits_30d = get_git_commits(30)
    parts.append(f"- Last 7 days: {len(commits_7d)} commits")
    parts.append(f"- Last 30 days: {len(commits_30d)} commits")
    refs = run_git(["remote", "-v"])
    if refs:
        parts.append(f"- Remotes: {refs.split(chr(10))[0]}")
    return "\n".join(parts)

# ═══════════════════════════════════════════════════════════════
# DREAM (from dream-build.py)
# ═══════════════════════════════════════════════════════════════

def analyze_external_repos():
    repos = {}
    for name, path in EXTERNAL_REPOS.items():
        if path.exists():
            commits = get_git_commits(90, repo_path=str(path))
            repos[name] = {"path": str(path), "commits_90d": len(commits), "status": "active" if commits else "inactive"}
    return repos

def build_contrast_analysis(tier_a, tier_b):
    a = get_tier_context(tier_a)
    b = get_tier_context(tier_b)
    return {"tier_a": a, "tier_b": b, "contrast": f"{a['commits']} vs {b['commits']} commits"}

def load_knowledge_stats():
    """Load stats from ALL memory systems."""
    stats = {}
    
    # 1. Cross-reference system
    xref_path = CACHE / "cross-reference.json"
    if xref_path.exists():
        xref = json.loads(xref_path.read_text())
        stats["xref_concepts"] = xref.get("stats", {}).get("concepts", 0)
        stats["xref_files"] = xref.get("stats", {}).get("files_scanned", 0)
        stats["xref_links"] = xref.get("stats", {}).get("total_links", 0)
    
    # 2. Knowledge graph
    graph_path = CACHE / "knowledge-graph.json"
    if graph_path.exists():
        graph = json.loads(graph_path.read_text())
        stats["graph_concepts"] = len(graph.get("relationships", {}))
        stats["graph_chains"] = len(graph.get("chains", []))
        stats["graph_implementations"] = len(graph.get("implementations", {}))
    
    # 3. Ingest sync
    ingest_path = CACHE / "ingest-sync.json"
    if ingest_path.exists():
        ingest = json.loads(ingest_path.read_text())
        stats["ingest_files"] = len(ingest.get("files", {}))
        stats["ingest_last"] = ingest.get("last_sync", "never")
    
    # 4. Knowledge timeline
    timeline_path = CACHE / "knowledge-timeline.md"
    if timeline_path.exists():
        content = timeline_path.read_text()
        stats["timeline_known"] = content.count("[x]")
        stats["timeline_missing"] = content.count("[ ]")
        stats["timeline_total"] = stats["timeline_known"] + stats["timeline_missing"]
        stats["timeline_coverage"] = stats["timeline_known"] / stats["timeline_total"] if stats["timeline_total"] > 0 else 0
    
    # 5. Science pillars
    science_dir = CACHE / "science-pillars"
    if science_dir.exists():
        stats["science_pillars"] = len(list(science_dir.glob("*.md"))) - 1  # minus README
    
    # 6. Research corpus
    research_dir = PROJECT / "docs/research"
    if research_dir.exists():
        stats["research_files"] = len(list(research_dir.rglob("*.md")))
    
    # 7. GTAMods wiki
    wiki_dir = Path("H:/wikis/gtamods/rendering")
    if wiki_dir.exists():
        stats["wiki_articles"] = len(list(wiki_dir.rglob("*.md")))
    
    # 8. Obsidian vault
    obsidian_dir = Path("E:/dev(dave)/SKYGFXPLUS_DOCS")
    if obsidian_dir.exists():
        stats["obsidian_docs"] = len(list(obsidian_dir.rglob("*.md")))
    
    # 9. Memory bank files
    stats["memory_bank_files"] = len(list(MEMORY_BANK.glob("*.md")))
    
    # 10. Gap detector (from knowledge.py — no _archive import needed)
    try:
        # Import directly from knowledge module (self-contained)
        sys.path.insert(0, str(PROJECT / "tools"))
        from knowledge import FEATURES
        stats["features_total"] = len(FEATURES)
        stats["features_implemented"] = sum(1 for f in FEATURES.values() if f.get("existing_implementation"))
        stats["features_coverage"] = stats["features_implemented"] / stats["features_total"] if stats["features_total"] > 0 else 0
    except:
        pass
    
    return stats

def load_active_work():
    """Load current work from activeContext.md."""
    active_path = MEMORY_BANK / "activeContext.md"
    if active_path.exists():
        content = active_path.read_text(encoding="utf-8", errors="ignore")
        # Extract key sections
        work = {"raw": content[:2000]}
        if "Active Issues" in content:
            start = content.find("Active Issues")
            end = content.find("\n##", start + 10)
            work["issues"] = content[start:end if end > 0 else start + 500]
        if "Next Steps" in content:
            start = content.find("Next Steps")
            work["next_steps"] = content[start:start + 500]
        return work
    return {}

def generate_daily_dream():
    """Daily dream: quick consolidation of ALL systems."""
    lunar = get_lunar_phase_info(moon_phase_days())
    commits = get_git_commits(1)
    knowledge = load_knowledge_stats()
    active = load_active_work()
    
    dream = {
        "type": "daily",
        "generated": datetime.now().isoformat(),
        "lunar": lunar,
        "git_today": len(commits),
        "commits": [c["message"] for c in commits[:10]],
        "knowledge": knowledge,
        "active_work": active,
        "memory_bank_files": list(get_memory_bank_summary().keys()),
    }
    
    # Save JSON
    CACHE.mkdir(parents=True, exist_ok=True)
    (CACHE / "dream-daily.json").write_text(json.dumps(dream, indent=2))
    
    # Generate markdown
    md = f"# Daily Dream — {datetime.now().strftime('%Y-%m-%d')}\n\n"
    md += f"**Lunar:** {format_lunar_status()} (weight: {lunar.get('weight', 0.8)})\n\n"
    
    md += f"## Today's Work\n"
    md += f"- Commits: {len(commits)}\n"
    for c in commits[:5]:
        md += f"  - {c['message']}\n"
    
    md += f"\n## All Memory Systems\n"
    md += f"| System | Status |\n|--------|--------|\n"
    md += f"| Cross-Reference | {knowledge.get('xref_concepts', 0)} concepts, {knowledge.get('xref_links', 0)} links |\n"
    md += f"| Knowledge Graph | {knowledge.get('graph_concepts', 0)} concepts, {knowledge.get('graph_chains', 0)} chains |\n"
    md += f"| Ingestion | {knowledge.get('ingest_files', 0)} files |\n"
    md += f"| Timeline | {knowledge.get('timeline_known', 0)}/{knowledge.get('timeline_total', 0)} known ({knowledge.get('timeline_coverage', 0)*100:.0f}%) |\n"
    md += f"| Science Pillars | {knowledge.get('science_pillars', 0)} docs |\n"
    md += f"| Research | {knowledge.get('research_files', 0)} docs |\n"
    md += f"| GTAMods Wiki | {knowledge.get('wiki_articles', 0)} articles |\n"
    md += f"| Obsidian Vault | {knowledge.get('obsidian_docs', 0)} docs |\n"
    md += f"| Memory Bank | {knowledge.get('memory_bank_files', 0)} files |\n"
    md += f"| Gap Coverage | {knowledge.get('features_implemented', 0)}/{knowledge.get('features_total', 0)} features ({knowledge.get('features_coverage', 0)*100:.0f}%) |\n"
    
    md += f"\n## Active Work\n"
    if active.get("issues"):
        md += f"{active['issues'][:300]}\n"
    if active.get("next_steps"):
        md += f"{active['next_steps'][:300]}\n"
    
    (CACHE / "dream-daily.md").write_text(md, encoding="utf-8")
    
    return dream

def generate_weekly_dream():
    """Weekly dream: big consolidation with ALL systems + lunar cycle."""
    lunar = get_lunar_phase_info(moon_phase_days())
    
    # Get all tier contexts
    tiers = {}
    for name, days in TIERS[:8]:
        tiers[name] = get_tier_context(name)
    
    # Get contrast analysis
    contrasts = {}
    for a, b in CONTRAST_PAIRS:
        contrasts[f"{a}_vs_{b}"] = build_contrast_analysis(a, b)
    
    # Get ALL knowledge stats
    knowledge = load_knowledge_stats()
    
    # Get active work
    active = load_active_work()
    
    # Get git history
    commits_week = get_git_commits(7)
    commits_month = get_git_commits(30)
    
    # Get external repos
    repos = analyze_external_repos()
    
    # Get memory bank summaries
    memory_summaries = get_memory_bank_summary()
    
    dream = {
        "type": "weekly",
        "generated": datetime.now().isoformat(),
        "lunar": lunar,
        "lunar_weight": lunar.get("weight", 0.8),
        "lunar_phase": lunar.get("phase_name", "unknown"),
        "tiers": tiers,
        "contrasts": contrasts,
        "knowledge": knowledge,
        "active_work": active,
        "git_week": len(commits_week),
        "git_month": len(commits_month),
        "recent_commits": [c["message"] for c in commits_week[:20]],
        "repos": repos,
        "memory_summaries": {k: v[:200] for k, v in memory_summaries.items()},
    }
    
    # Save JSON
    CACHE.mkdir(parents=True, exist_ok=True)
    (CACHE / "dream-weekly.json").write_text(json.dumps(dream, indent=2))
    
    # Generate markdown
    md = f"# Weekly Dream — {datetime.now().strftime('%Y-%m-%d')}\n\n"
    md += f"**Lunar:** {format_lunar_status()} (weight: {lunar.get('weight', 0.8)})\n\n"
    
    md += f"## Lunar Context\n"
    md += f"- Phase: {lunar.get('phase_name', 'unknown')}\n"
    md += f"- Weight: {lunar.get('weight', 0.8)}\n"
    consolidation = 'peak' if lunar.get('weight', 0) > 0.9 else 'building' if lunar.get('weight', 0) > 0.7 else 'resting'
    md += f"- Consolidation: {consolidation}\n\n"
    
    md += f"## This Week\n"
    md += f"- Commits: {len(commits_week)}\n"
    md += f"- Month commits: {len(commits_month)}\n\n"
    
    md += f"## Tier Analysis\n"
    for name, ctx in tiers.items():
        md += f"- {name}: {ctx['commits']} commits ({ctx['days']}d)\n"
    
    md += f"\n## Contrast Pairs\n"
    for pair, data in contrasts.items():
        md += f"- {pair}: {data['contrast']}\n"
    
    md += f"\n## ALL Memory Systems\n"
    md += f"| System | Status |\n|--------|--------|\n"
    md += f"| Cross-Reference | {knowledge.get('xref_concepts', 0)} concepts, {knowledge.get('xref_files', 0)} files, {knowledge.get('xref_links', 0)} links |\n"
    md += f"| Knowledge Graph | {knowledge.get('graph_concepts', 0)} concepts, {knowledge.get('graph_chains', 0)} chains, {knowledge.get('graph_implementations', 0)} impl |\n"
    md += f"| Ingestion | {knowledge.get('ingest_files', 0)} files (last: {knowledge.get('ingest_last', 'never')}) |\n"
    md += f"| Timeline | {knowledge.get('timeline_known', 0)}/{knowledge.get('timeline_total', 0)} known ({knowledge.get('timeline_coverage', 0)*100:.0f}%) |\n"
    md += f"| Science Pillars | {knowledge.get('science_pillars', 0)} docs |\n"
    md += f"| Research | {knowledge.get('research_files', 0)} docs |\n"
    md += f"| GTAMods Wiki | {knowledge.get('wiki_articles', 0)} articles |\n"
    md += f"| Obsidian Vault | {knowledge.get('obsidian_docs', 0)} docs |\n"
    md += f"| Memory Bank | {knowledge.get('memory_bank_files', 0)} files |\n"
    md += f"| Gap Coverage | {knowledge.get('features_implemented', 0)}/{knowledge.get('features_total', 0)} features ({knowledge.get('features_coverage', 0)*100:.0f}%) |\n"
    
    md += f"\n## External Repos\n"
    for name, info in repos.items():
        md += f"- {name}: {info['commits_90d']} commits ({info['status']})\n"
    
    md += f"\n## Active Work\n"
    if active.get("issues"):
        md += f"{active['issues'][:500]}\n"
    if active.get("next_steps"):
        md += f"{active['next_steps'][:500]}\n"
    
    (CACHE / "dream-weekly.md").write_text(md, encoding="utf-8")
    
    return dream

def generate_dream_context(tier=None, lunar=False):
    """Legacy wrapper - now generates daily or weekly dream."""
    # Check if it's weekly (Sunday) or daily
    is_weekly = datetime.now().weekday() == 6  # Sunday
    
    if is_weekly:
        return generate_weekly_dream()
    else:
        return generate_daily_dream()

# ═══════════════════════════════════════════════════════════════
# OPTIMIZE (from memory-optimize.py)
# ═══════════════════════════════════════════════════════════════

def generate_compact_index():
    index = {"generated": datetime.now().isoformat(), "layers": {}}
    memory_files = list(MEMORY_BANK.glob("*.md"))
    index["layers"]["memory_bank"] = {"count": len(memory_files), "files": [f.name for f in memory_files]}
    if OBSIDIAN.exists():
        obsidian_files = list(OBSIDIAN.rglob("*.md"))
        index["layers"]["obsidian"] = {"count": len(obsidian_files)}
    cache_files = list(CACHE.glob("*.json"))
    index["layers"]["cache"] = {"count": len(cache_files), "files": [f.name for f in cache_files]}
    CACHE.mkdir(parents=True, exist_ok=True)
    (CACHE / "compact-index.json").write_text(json.dumps(index, indent=2))
    return index

# ═══════════════════════════════════════════════════════════════
# SYNC (from memory-sync.py)
# ═══════════════════════════════════════════════════════════════

def sync_all():
    results = {"compact_reference": False, "index": False, "dream_context": False}
    try:
        ref = build_compact_reference()
        CACHE.mkdir(parents=True, exist_ok=True)
        (CACHE / "compact-reference.md").write_text(ref)
        results["compact_reference"] = True
    except Exception as e:
        results["compact_reference_error"] = str(e)
    try:
        generate_compact_index()
        results["index"] = True
    except Exception as e:
        results["index_error"] = str(e)
    try:
        ctx = generate_dream_context()
        (CACHE / "dream-context-full.json").write_text(json.dumps(ctx, indent=2))
        results["dream_context"] = True
    except Exception as e:
        results["dream_context_error"] = str(e)
    return results

# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Unified memory system")
    parser.add_argument("command", nargs="?", default="status", choices=["status", "compact", "tier", "lunar", "dream", "optimize", "sync", "history"])
    parser.add_argument("target", nargs="?", help="Tier name, date range, etc.")
    parser.add_argument("--lunar", action="store_true", help="Include lunar context")
    parser.add_argument("--days", type=int, default=30, help="Days for history")
    parser.add_argument("--output", "-o", help="Output file")
    args = parser.parse_args()

    if args.command == "status":
        print("Memory System Status")
        print("=" * 50)
        print(f"  Lunar: {format_lunar_status()}")
        commits_7 = get_git_commits(7)
        commits_30 = get_git_commits(30)
        print(f"  Git (7d): {len(commits_7)} commits")
        print(f"  Git (30d): {len(commits_30)} commits")
        for name, days in TIERS[:6]:
            ctx = get_tier_context(name)
            print(f"  Tier {name}: {ctx['commits']} commits ({ctx['days']}d)")
        repos = analyze_external_repos()
        for name, info in repos.items():
            print(f"  Repo {name}: {info['commits_90d']} commits ({info['status']})")

    elif args.command == "compact":
        ref = build_compact_reference()
        if args.output:
            Path(args.output).write_text(ref)
            print(f"Saved to {args.output}")
        else:
            print(ref)

    elif args.command == "tier":
        tier = args.target or "monthly"
        ctx = get_tier_context(tier)
        print(f"Tier: {ctx['tier']} ({ctx['days']} days)")
        print(f"Commits: {ctx['commits']}")
        print(f"Summary: {ctx['summary']}")

    elif args.command == "lunar":
        print(format_lunar_status())

    elif args.command == "dream":
        dream_type = args.target or "daily"
        if dream_type == "daily":
            ctx = generate_daily_dream()
        elif dream_type == "weekly":
            ctx = generate_weekly_dream()
        else:
            ctx = generate_dream_context()
        if args.output:
            Path(args.output).write_text(json.dumps(ctx, indent=2))
            print(f"Saved to {args.output}")
        else:
            print(json.dumps(ctx, indent=2))

    elif args.command == "optimize":
        idx = generate_compact_index()
        print(f"Index generated: {sum(v['count'] for v in idx['layers'].values())} files")

    elif args.command == "sync":
        results = sync_all()
        for k, v in results.items():
            status = "OK" if v is True else ("ERROR" if "error" in k else "SKIP")
            print(f"  {k}: {status}")

    elif args.command == "history":
        commits = get_git_commits(args.days)
        print(f"Git history ({args.days} days):")
        for c in commits[:30]:
            print(f"  {c['hash']} {c['message']}")

if __name__ == "__main__":
    main()
