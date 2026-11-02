#!/usr/bin/env python3
"""Shared paths / console helpers for the project tools.

All paths are resolved from this file (or env overrides), so every tool can be
launched from any working directory:

  SKYGFX_PROJECT  repo root   (default: parent of tools/)
  SKYGFX_GAME     game dir    (default: E:/games/gtasa_skygfx_plus)
"""
import os
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = Path(os.environ.get("SKYGFX_PROJECT") or TOOLS_DIR.parent)
GAME_DIR = Path(os.environ.get("SKYGFX_GAME") or r"E:\games\gtasa_skygfx_plus")
LOG_FILE = GAME_DIR / "skygfx_dbg.log"


def setup_console():
    """Make stdout/stderr safe for non-ASCII on cp1252 consoles.

    Python 3.10 on Windows defaults to cp1252 for pipes. Printing the arrow in
    skytool help or a moon emoji then raises UnicodeEncodeError. Reconfigure
    UTF-8 with errors='replace' so tools never crash on display text.
    """
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def human_size(n):
    """Compact human-readable byte size."""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} TB"
