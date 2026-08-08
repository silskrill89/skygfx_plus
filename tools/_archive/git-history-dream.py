#!/usr/bin/env python3
"""
git-history-dream.py — Extract git timeline for dream consolidation
Uses commit history + file dates to create rich temporal context.

Usage:
    python tools/git-history-dream.py                    # Full timeline
    python tools/git-history-dream.py --days 7           # Last N days only
    python tools/git-history-dream.py --branch experimental  # Specific branch
    python tools/git-history-dream.py --summary           # Compact summary
    python tools/git-history-dream.py --output memory-bank/.cache/git-timeline.json

Output: JSON timeline of what happened when, for dream agent consumption.
"""
import subprocess
import json
import sys
from pathlib import Path
from datetime import datetime, timedelta
from collections import defaultdict

PROJECT_ROOT = Path(__file__).resolve().parent.parent

def run_git(args):
    """Run git command and return output."""
    result = subprocess.run(
        ["git"] + args,
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    return result.stdout.strip()

def get_commit_history(branch=None, days=None):
    """Get commit history with dates and messages."""
    cmd = ["log", "--all", "--format=%H|%ai|%s"]
    if branch:
        cmd = ["log", branch, "--format=%H|%ai|%s"]
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
                    "time": parts[1],
                    "message": parts[2]
                })
    return commits

def get_file_changes(branch=None, days=None):
    """Get files changed per commit."""
    cmd = ["log", "--all", "--name-status", "--format=COMMIT|%H|%ai|%s"]
    if branch:
        cmd = ["log", branch, "--name-status", "--format=COMMIT|%H|%ai|%s"]
    if days:
        since = (datetime.now() - timedelta(days=days)).strftime("%Y-%m-%d")
        cmd.extend(["--since", since])
    
    output = run_git(cmd)
    changes = []
    current_commit = None
    
    for line in output.split("\n"):
        if line.startswith("COMMIT|"):
            parts = line.split("|", 3)
            if len(parts) >= 3:
                current_commit = {
                    "hash": parts[1][:8],
                    "date": parts[2][:10],
                    "message": parts[3] if len(parts) > 3 else ""
                }
        elif current_commit and line and not line.startswith("COMMIT"):
            # Parse file change: "M\tfile.cpp" or "A\tnew_file.h"
            parts = line.split("\t", 1)
            if len(parts) == 2:
                status = parts[0]
                filepath = parts[1]
                changes.append({
                    "commit": current_commit["hash"],
                    "date": current_commit["date"],
                    "message": current_commit["message"],
                    "status": status,
                    "file": filepath
                })
    
    return changes

def get_file_dates():
    """Get creation and last modification dates for all source files."""
    # Get all source files
    files = []
    for ext in ["*.cpp", "*.h", "*.hlsl", "*.hlsl", "*.py", "*.md"]:
        files.extend(PROJECT_ROOT.rglob(ext))
    
    file_dates = {}
    for f in files:
        rel = f.relative_to(PROJECT_ROOT)
        # Skip build artifacts and deps
        if any(skip in str(rel) for skip in ["build/", ".cache/", "node_modules/", ".git/"]):
            continue
        
        # Get git blame for first and last commit
        try:
            # First commit (oldest)
            first = run_git(["log", "--diff-filter=A", "--format=%ai", "--", str(rel)])
            # Last commit (newest)
            last = run_git(["log", "-1", "--format=%ai", "--", str(rel)])
            
            file_dates[str(rel)] = {
                "created": first.split("\n")[0] if first else None,
                "modified": last.split("\n")[0] if last else None,
                "extension": f.suffix
            }
        except Exception:
            pass
    
    return file_dates

def build_timeline(commits, changes, file_dates):
    """Build a rich timeline from git data."""
    # Group commits by date
    daily = defaultdict(list)
    for c in commits:
        daily[c["date"]].append(c)
    
    # Group changes by feature area
    features = defaultdict(list)
    for ch in changes:
        # Extract feature from commit message
        msg = ch["message"]
        if ":" in msg:
            feature = msg.split(":")[0].strip()
        else:
            feature = "other"
        features[feature].append(ch)
    
    # Build file lifecycle
    file_lifecycle = {}
    for filepath, dates in file_dates.items():
        # Find which feature area this file belongs to
        feature = "other"
        for f in features:
            if any(ch["file"] == filepath for ch in features[f]):
                feature = f
                break
        
        file_lifecycle[filepath] = {
            **dates,
            "feature": feature
        }
    
    return {
        "summary": {
            "total_commits": len(commits),
            "date_range": {
                "start": commits[-1]["date"] if commits else None,
                "end": commits[0]["date"] if commits else None
            },
            "branches": ["experimental", "master"],
            "total_files_tracked": len(file_dates)
        },
        "daily_activity": {
            date: {
                "commit_count": len(comms),
                "commits": comms,
                "files_changed": len([ch for ch in changes if ch["date"] == date])
            }
            for date, comms in sorted(daily.items())
        },
        "feature_areas": {
            feature: {
                "commit_count": len(chs),
                "files_affected": list(set(ch["file"] for ch in chs)),
                "first_commit": min(ch["date"] for ch in chs),
                "last_commit": max(ch["date"] for ch in chs)
            }
            for feature, chs in sorted(features.items(), key=lambda x: -len(x[1]))
        },
        "file_lifecycle": file_lifecycle,
        "recent_changes": changes[:50]  # Last 50 file changes
    }

def generate_dream_context(timeline):
    """Generate rich context for dream agent from timeline."""
    lines = []
    
    # Summary
    s = timeline["summary"]
    lines.append(f"# Git Timeline Context")
    lines.append(f"Generated: {datetime.now().isoformat()[:10]}")
    lines.append(f"Commits: {s['total_commits']} | Files: {s['total_files_tracked']}")
    lines.append(f"Date range: {s['date_range']['start']} to {s['date_range']['end']}")
    lines.append("")
    
    # Top feature areas
    lines.append("## Major Feature Areas (by commit count)")
    for feature, info in list(timeline["feature_areas"].items())[:10]:
        lines.append(f"- **{feature}**: {info['commit_count']} commits, {len(info['files_affected'])} files")
        lines.append(f"  First: {info['first_commit']} | Last: {info['last_commit']}")
    lines.append("")
    
    # Recent activity (last 7 days)
    lines.append("## Recent Activity (last 7 days)")
    dates = sorted(timeline["daily_activity"].keys(), reverse=True)[:7]
    for date in dates:
        day = timeline["daily_activity"][date]
        lines.append(f"### {date} ({day['commit_count']} commits, {day['files_changed']} files)")
        for c in day["commits"][:5]:
            lines.append(f"  - {c['hash']}: {c['message']}")
    lines.append("")
    
    # File lifecycle hotspots
    lines.append("## Most Active Files")
    files_by_activity = sorted(
        [(k, v) for k, v in timeline["file_lifecycle"].items() if v.get("modified")],
        key=lambda x: x[1].get("modified", ""),
        reverse=True
    )[:20]
    for filepath, info in files_by_activity:
        lines.append(f"- `{filepath}` [{info['feature']}]")
        lines.append(f"  Created: {info.get('created', '?')[:10]} | Modified: {info.get('modified', '?')[:10]}")
    
    return "\n".join(lines)

def main():
    args = sys.argv[1:]
    days = None
    branch = None
    summary_only = "--summary" in args
    output_path = None
    
    if "--days" in args:
        idx = args.index("--days")
        if idx + 1 < len(args):
            days = int(args[idx + 1])
    
    if "--branch" in args:
        idx = args.index("--branch")
        if idx + 1 < len(args):
            branch = args[idx + 1]
    
    if "--output" in args:
        idx = args.index("--output")
        if idx + 1 < len(args):
            output_path = Path(args[idx + 1])
    
    print("Extracting git history...")
    commits = get_commit_history(branch, days)
    print(f"  {len(commits)} commits found")
    
    print("Extracting file changes...")
    changes = get_file_changes(branch, days)
    print(f"  {len(changes)} file changes found")
    
    print("Getting file dates...")
    file_dates = get_file_dates()
    print(f"  {len(file_dates)} files tracked")
    
    print("Building timeline...")
    timeline = build_timeline(commits, changes, file_dates)
    
    # Generate dream context
    context = generate_dream_context(timeline)
    
    if summary_only:
        print("\n" + context)
    else:
        # Save full timeline
        if output_path:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(json.dumps(timeline, indent=2, default=str))
            print(f"\nTimeline saved to: {output_path}")
        else:
            # Default output location
            cache_dir = PROJECT_ROOT / "memory-bank" / ".cache"
            cache_dir.mkdir(parents=True, exist_ok=True)
            timeline_path = cache_dir / "git-timeline.json"
            timeline_path.write_text(json.dumps(timeline, indent=2, default=str))
            print(f"\nTimeline saved to: {timeline_path}")
        
        # Also save dream context
        context_path = cache_dir / "git-dream-context.md"
        context_path.write_text(context)
        print(f"Dream context saved to: {context_path}")
    
    print("\nDone!")

if __name__ == "__main__":
    main()
