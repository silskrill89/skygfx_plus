"""Shared cache manager.

The cache lives in a `cache/` subfolder next to the installer (NOT in
%LOCALAPPDATA%). This keeps the install fully portable — extract the ZIP
anywhere, run it, and all downloaded archives / extractions / backups /
logs stay in that folder. Delete the folder to fully uninstall.

Layout:
    cache/downloads/    (downloaded archives)
    cache/extracted/    (extracted mod files)
    cache/backups/      (originals backup .zip)
    cache/logs/         (install logs)

Re-installs reuse cached archives/extracted dirs so the user doesn't have
to re-download or re-extract on every run.
"""
from __future__ import annotations

import hashlib
import os
import shutil
from typing import Optional

from . import config

# Re-export the cache path constants here so callers can do
# `from .. import cache` and reference `cache.CACHE_ROOT` etc.
CACHE_ROOT = config.CACHE_ROOT
CACHE_DOWNLOADS = config.CACHE_DOWNLOADS
CACHE_EXTRACTED = config.CACHE_EXTRACTED
CACHE_BACKUPS = config.CACHE_BACKUPS
CACHE_LOGS = config.CACHE_LOGS


def cache_path_for_url(url: str, ext: str = "") -> str:
    """Return the on-disk cache path for a download URL.

    The filename is derived from a short SHA-1 of the URL plus the original
    extension, so re-downloads hit the cache instead of the network.
    """
    h = hashlib.sha1(url.encode("utf-8")).hexdigest()[:12]
    if not ext and "." in os.path.basename(url.split("?")[0]):
        ext = "." + os.path.basename(url.split("?")[0]).rsplit(".", 1)[1].lower()
    return os.path.join(config.CACHE_DOWNLOADS, f"{h}{ext}")


def is_cached(url: str, ext: str = "", min_size: int = 1024) -> bool:
    """True if a non-trivial cached file exists for this URL."""
    p = cache_path_for_url(url, ext)
    return os.path.isfile(p) and os.path.getsize(p) >= min_size


def extracted_dir_for(mod_id: str) -> str:
    """Where extracted files for a given mod id live in the shared cache."""
    return os.path.join(config.CACHE_EXTRACTED, mod_id)


def is_extracted(mod_id: str) -> bool:
    """True if the mod has already been extracted into the shared cache."""
    d = extracted_dir_for(mod_id)
    return os.path.isdir(d) and bool(os.listdir(d))


def clear_extracted(mod_id: str) -> None:
    """Remove a previously extracted mod from the cache (forces re-extract)."""
    d = extracted_dir_for(mod_id)
    if os.path.isdir(d):
        shutil.rmtree(d, ignore_errors=True)


def backup_path(label: str = "backup") -> str:
    """Return a timestamped backup .zip path. Does NOT create the file."""
    from datetime import datetime
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(config.CACHE_BACKUPS, f"{label}_{ts}.zip")


def latest_backup() -> Optional[str]:
    """Return the most recent backup .zip, or None if there isn't one."""
    if not os.path.isdir(config.CACHE_BACKUPS):
        return None
    files = [
        os.path.join(config.CACHE_BACKUPS, f)
        for f in os.listdir(config.CACHE_BACKUPS)
        if f.lower().endswith(".zip")
    ]
    if not files:
        return None
    return max(files, key=os.path.getmtime)


def log_path() -> str:
    """Return the install log file path."""
    from datetime import datetime
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(config.CACHE_LOGS, f"install_{ts}.log")


def ensure_dirs() -> None:
    """Create every cache directory (called once at app startup)."""
    config.ensure_cache_dirs()
