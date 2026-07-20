"""SHA-1-based detection of GTA San Andreas exe versions.

Loads the known-versions table from data/exe_hashes.json at startup.
Computes SHA-1 of the user's gta_sa.exe and matches against the table.
Falls back to file-size matching if the hash isn't in the table.

The user can also manually pick a version on the downgrade page if both
automatic methods return "unknown".
"""
from __future__ import annotations

import hashlib
import json
import logging
import os
from dataclasses import dataclass
from typing import List, Optional

from . import config

log = logging.getLogger(__name__)


# ----------------------------------------------------------------------
# Data class
# ----------------------------------------------------------------------
@dataclass
class ExeVersion:
    id: str
    label: str
    region: str
    version_string: str
    file_size_bytes: Optional[int]
    sha1: Optional[str]
    mod_compatible: bool
    needs_downgrade: bool
    source: str
    notes: str = ""


# ----------------------------------------------------------------------
# Load the version table
# ----------------------------------------------------------------------
_TABLE: Optional[List[ExeVersion]] = None


def _resolve_table_path() -> str:
    """Find the exe_hashes.json file, whether running from source or bundled."""
    # When running from source: data/exe_hashes.json next to the project root
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    candidate = os.path.join(here, "data", "exe_hashes.json")
    if os.path.isfile(candidate):
        return candidate
    # When bundled with PyInstaller: data/exe_hashes.json inside _MEIPASS
    meipass = os.environ.get("_MEIPASS")
    if meipass:
        candidate = os.path.join(meipass, "data", "exe_hashes.json")
        if os.path.isfile(candidate):
            return candidate
    # Last resort: next to the .exe
    return candidate


_TABLE_PATH = _resolve_table_path()


def load_table() -> List[ExeVersion]:
    """Load the known-versions table from data/exe_hashes.json (cached)."""
    global _TABLE
    if _TABLE is not None:
        return _TABLE

    if not os.path.isfile(_TABLE_PATH):
        log.warning("exe_hashes.json missing at %s — version detection will be size-only.", _TABLE_PATH)
        _TABLE = []
        return _TABLE

    try:
        with open(_TABLE_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        _TABLE = [
            ExeVersion(
                id=v["id"],
                label=v["label"],
                region=v.get("region", ""),
                version_string=v.get("version_string", ""),
                file_size_bytes=v.get("file_size_bytes"),
                sha1=v.get("sha1"),
                mod_compatible=v.get("mod_compatible", False),
                needs_downgrade=v.get("needs_downgrade", False),
                source=v.get("source", ""),
                notes=v.get("notes", ""),
            )
            for v in data.get("versions", [])
        ]
        log.info("Loaded %d known SA exe versions from %s", len(_TABLE), _TABLE_PATH)
    except Exception as e:
        log.exception("Failed to load exe_hashes.json: %s", e)
        _TABLE = []
    return _TABLE


# ----------------------------------------------------------------------
# Hash computation
# ----------------------------------------------------------------------
def compute_sha1(path: str, chunk_size: int = 1024 * 1024) -> Optional[str]:
    """Compute the SHA-1 of a file. Returns lowercase hex or None on error."""
    if not os.path.isfile(path):
        return None
    h = hashlib.sha1()
    try:
        with open(path, "rb") as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest().lower()
    except Exception as e:
        log.warning("SHA-1 computation failed for %s: %s", path, e)
        return None


# ----------------------------------------------------------------------
# Detection
# ----------------------------------------------------------------------
@dataclass
class DetectionResult:
    """What we know about a given gta_sa.exe."""
    exe_path: str
    file_size: int
    sha1: Optional[str]
    matched: Optional[ExeVersion]   # None if no match
    match_method: str               # "hash" | "size" | "none"
    is_v10: bool
    needs_downgrade: bool


def detect(exe_path: str) -> DetectionResult:
    """Detect the version of a gta_sa.exe file.

    Strategy:
        1. Compute SHA-1, look up in table by hash.
        2. If no hash match, look up by file size.
        3. If neither matches, return matched=None (user must pick manually).
    """
    table = load_table()
    size = os.path.getsize(exe_path) if os.path.isfile(exe_path) else 0
    sha1 = compute_sha1(exe_path)

    # 1. Hash match
    if sha1:
        for v in table:
            if v.sha1 and v.sha1.lower() == sha1:
                log.info("Version detected by SHA-1: %s (%s)", v.id, v.label)
                return DetectionResult(
                    exe_path=exe_path, file_size=size, sha1=sha1,
                    matched=v, match_method="hash",
                    is_v10=v.mod_compatible, needs_downgrade=v.needs_downgrade,
                )

    # 2. Size match
    for v in table:
        if v.file_size_bytes and v.file_size_bytes == size:
            log.info("Version detected by size: %s (%s) — %d bytes",
                     v.id, v.label, size)
            return DetectionResult(
                exe_path=exe_path, file_size=size, sha1=sha1,
                matched=v, match_method="size",
                is_v10=v.mod_compatible, needs_downgrade=v.needs_downgrade,
            )

    # 3. Heuristic fallback using known size buckets from config.py
    is_v10 = size in config.SA_V10_SIZES
    is_steam = size in config.SA_STEAM_SIZES
    log.info("No table match for size=%d sha1=%s — heuristic: v10=%s steam=%s",
             size, sha1, is_v10, is_steam)
    return DetectionResult(
        exe_path=exe_path, file_size=size, sha1=sha1,
        matched=None, match_method="none",
        is_v10=is_v10, needs_downgrade=(is_steam or not is_v10),
    )


def list_known_versions() -> List[ExeVersion]:
    """Return the full known-versions table for the manual-pick dropdown."""
    return load_table()
