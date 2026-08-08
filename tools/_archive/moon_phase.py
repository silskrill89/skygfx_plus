#!/usr/bin/env python3
"""Moon phase calculator and lunar memory tracker.

Calculates moon phase for any date using a simplified astronomical algorithm.
The moon cycle is ~29.53 days.

Lunar Memory Tiers:
- new_moon:        0-3.69 days  (0-12.5%) — Fresh impressions, raw events
- waxing_crescent: 3.69-7.38 days (12.5-25%) — Early patterns forming
- first_quarter:   7.38-11.07 days (25-37.5%) — First consolidation
- waxing_gibbous:  11.07-14.76 days (37.5-50%) — Building toward peak
- full_moon:       14.76-18.46 days (50-62.5%) — Peak clarity, review
- waning_gibbous:  18.46-22.15 days (62.5-75%) — Reflection, integration
- third_quarter:   22.15-25.84 days (75-87.5%) — Deep consolidation
- waning_crescent: 25.84-29.53 days (87.5-100%) — Release, prepare for new

Usage:
    python tools/moon_phase.py                    # Current phase
    python tools/moon_phase.py --date 2026-08-07  # Phase for date
    python tools/moon_phase.py --upcoming 5       # Next 5 phases
    python tools/moon_phase.py --memory-sync      # Show memory sync status
    python tools/moon_phase.py --tier             # Current lunar memory tier
    python tools/moon_phase.py --history          # Phase history for memory periods
"""

import sys
import math
from datetime import datetime, timedelta
from pathlib import Path

# Moon phase names and their day ranges within a 29.53-day cycle
LUNAR_PHASES = [
    ("new_moon",        0.00,  3.69,  "🌑", "Fresh — raw events, new impressions"),
    ("waxing_crescent", 3.69,  7.38,  "🌒", "Forming — early patterns, initial learning"),
    ("first_quarter",   7.38,  11.07, "🌓", "Consolidating — first review, integrate"),
    ("waxing_gibbous",  11.07, 14.76, "🌔", "Building — approaching peak clarity"),
    ("full_moon",       14.76, 18.46, "🌕", "Peak — maximum clarity, full review"),
    ("waning_gibbous",  18.46, 22.15, "🌖", "Reflecting — deeper understanding"),
    ("third_quarter",   22.15, 25.84, "🌗", "Deep — consolidation, extract patterns"),
    ("waning_crescent", 25.84, 29.53, "🌘", "Release — let go, prepare for renewal"),
]

MOON_CYCLE_DAYS = 29.53058770576  # Synodic period

# Known new moon reference (Jan 6, 2000 18:14 UTC)
REFERENCE_NEW_MOON = datetime(2000, 1, 6, 18, 14, 0)


def moon_phase_days(date: datetime) -> float:
    """Calculate days into the current moon cycle for a given date."""
    delta = date - REFERENCE_NEW_MOON
    total_days = delta.total_seconds() / 86400.0
    return total_days % MOON_CYCLE_DAYS


def get_phase_info(phase_days: float) -> dict:
    """Get full phase info from days into cycle."""
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
    # Fallback (shouldn't happen)
    return LUNAR_PHASES[0]


def days_until_next_phase(phase_days: float) -> float:
    """Days until the next phase boundary."""
    for name, start, end, emoji, description in LUNAR_PHASES:
        if start <= phase_days < end:
            return end - phase_days
    # Fallback: days until new moon (cycle reset)
    return MOON_CYCLE_DAYS - phase_days


def next_phase_date(date: datetime, phase_days: float) -> datetime:
    """Get the date of the next phase transition."""
    days = days_until_next_phase(phase_days)
    return date + timedelta(days=days)


def upcoming_phases(date: datetime, count: int = 5) -> list:
    """Get upcoming phase transitions."""
    phase_days = moon_phase_days(date)
    results = []
    current = date
    remaining = phase_days

    for _ in range(count + 8):
        days_left = days_until_next_phase(remaining)
        if days_left <= 0:
            # Safety: skip to next day
            current += timedelta(days=1)
            remaining = (remaining + 1) % MOON_CYCLE_DAYS
            continue
        
        current += timedelta(days=days_left)
        remaining = (remaining + days_left) % MOON_CYCLE_DAYS
        
        # Ensure remaining is in valid range
        remaining = remaining % MOON_CYCLE_DAYS
        
        info = get_phase_info(remaining)
        if isinstance(info, dict) and len(results) < count:
            results.append({
                "date": current.strftime("%Y-%m-%d"),
                "phase": info["name"],
                "emoji": info["emoji"],
            })

    return results


def memory_sync_status(date: datetime) -> dict:
    """Show how lunar phases align with memory periods."""
    phase_days = moon_phase_days(date)
    info = get_phase_info(phase_days)

    # Memory consolidation points in the lunar cycle
    consolidation_points = {
        "new_moon": 1.5,        # Day 1.5 — reset memory, new cycle
        "first_quarter": 9.2,   # Day 9.2 — first review checkpoint
        "full_moon": 16.6,      # Day 16.6 — peak review checkpoint
        "third_quarter": 24.0,  # Day 24.0 — deep consolidation checkpoint
    }

    checkpoints = []
    for name, target_day in consolidation_points.items():
        diff = target_day - phase_days
        if diff < 0:
            diff += MOON_CYCLE_DAYS
        checkpoints.append({
            "phase": name,
            "days_until": round(diff, 1),
            "date": (date + timedelta(days=diff)).strftime("%Y-%m-%d"),
        })

    return {
        "current_phase": info["name"],
        "emoji": info["emoji"],
        "day_in_cycle": round(phase_days, 1),
        "cycle_progress": f"{info['cycle_progress'] * 100:.1f}%",
        "consolidation_checkpoints": checkpoints,
    }


def lunar_history(start_date: datetime, end_date: datetime) -> list:
    """Show lunar phases across a date range for memory analysis."""
    current = start_date
    results = []
    last_phase = None

    while current <= end_date:
        phase_days = moon_phase_days(current)
        info = get_phase_info(phase_days)

        if info["name"] != last_phase:
            results.append({
                "date": current.strftime("%Y-%m-%d"),
                "phase": info["name"],
                "emoji": info["emoji"],
            })
            last_phase = info["name"]

        current += timedelta(days=1)

    return results


def format_current_phase(date: datetime = None) -> str:
    """Format current moon phase as readable output."""
    if date is None:
        date = datetime.now()

    phase_days = moon_phase_days(date)
    info = get_phase_info(phase_days)

    lines = [
        f"  {info['emoji']} {info['name'].replace('_', ' ').title()}",
        f"  {info['description']}",
        f"  Day {phase_days:.1f} of {MOON_CYCLE_DAYS:.1f} cycle",
        f"  Phase progress: {info['progress'] * 100:.0f}%",
        f"  Cycle progress: {info['cycle_progress'] * 100:.1f}%",
    ]
    return "\n".join(lines)


def format_upcoming(date: datetime, count: int) -> str:
    """Format upcoming phases."""
    phases = upcoming_phases(date, count)
    lines = [f"  {'Date':<12} {'Phase':<18} {'Emoji'}"]
    lines.append(f"  {'─' * 12} {'─' * 18} {'─' * 5}")
    for p in phases:
        lines.append(f"  {p['date']:<12} {p['phase']:<18} {p['emoji']}")
    return "\n".join(lines)


def format_memory_sync(date: datetime) -> str:
    """Format memory sync status."""
    sync = memory_sync_status(date)
    lines = [
        f"  Current: {sync['emoji']} {sync['current_phase'].replace('_', ' ').title()}",
        f"  Cycle: {sync['day_in_cycle']} days ({sync['cycle_progress']})",
        "",
        "  Consolidation Checkpoints:",
    ]
    for cp in sync["consolidation_checkpoints"]:
        lines.append(f"    {cp['phase']:<18} in {cp['days_until']:>5.1f} days ({cp['date']})")
    return "\n".join(lines)


def format_history(start: datetime, end: datetime) -> str:
    """Format lunar history."""
    phases = lunar_history(start, end)
    lines = []
    for p in phases:
        lines.append(f"  {p['date']}  {p['emoji']} {p['phase']}")
    return "\n".join(lines)


def main():
    args = sys.argv[1:]
    now = datetime.now()

    if "--date" in args:
        idx = args.index("--date")
        if idx + 1 < len(args):
            date = datetime.strptime(args[idx + 1], "%Y-%m-%d")
        else:
            print("Error: --date requires YYYY-MM-DD")
            sys.exit(1)
        print(f"\nMoon phase for {date.strftime('%Y-%m-%d')}:")
        print(format_current_phase(date))

    elif "--upcoming" in args:
        idx = args.index("--upcoming")
        count = int(args[idx + 1]) if idx + 1 < len(args) else 5
        print(f"\nUpcoming moon phases (from {now.strftime('%Y-%m-%d')}):")
        print(format_upcoming(now, count))

    elif "--memory-sync" in args:
        print(f"\nLunar Memory Sync ({now.strftime('%Y-%m-%d')}):")
        print(format_memory_sync(now))

    elif "--tier" in args:
        phase_days = moon_phase_days(now)
        info = get_phase_info(phase_days)
        print(info["name"])

    elif "--history" in args:
        if "--from" in args and "--to" in args:
            fi = args.index("--from")
            ti = args.index("--to")
            start = datetime.strptime(args[fi + 1], "%Y-%m-%d")
            end = datetime.strptime(args[ti + 1], "%Y-%m-%d")
        else:
            start = now - timedelta(days=90)
            end = now
        print(f"\nLunar phases {start.strftime('%Y-%m-%d')} to {end.strftime('%Y-%m-%d')}:")
        print(format_history(start, end))

    else:
        print(f"\nCurrent moon phase ({now.strftime('%Y-%m-%d %H:%M')}):")
        print(format_current_phase(now))
        print(f"\nNext phases:")
        print(format_upcoming(now, 3))
        print(f"\nMemory sync:")
        print(format_memory_sync(now))


if __name__ == "__main__":
    main()
