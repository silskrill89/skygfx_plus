"""Backup original GTA SA files before any mod is applied.

Backs up:
    gta_sa.exe, gta-sa.exe, data/, models/, scripts/, cleo/, modloader/,
    text/, audio/, SAMP/

…to a single timestamped .zip stored in the shared cache so the user can
restore later. Files/folders that don't exist are silently skipped.
"""
from __future__ import annotations

import logging
import os
import zipfile
from typing import Callable, Optional

from . import cache, config

log = logging.getLogger(__name__)

ProgressCb = Callable[[int, int], None]


def backup_sa_root(
    sa_root: str,
    progress: Optional[ProgressCb] = None,
) -> Optional[str]:
    """Create a timestamped backup .zip of the original SA files.

    Returns the .zip path, or None if nothing was backed up.
    """
    if not sa_root or not os.path.isdir(sa_root):
        return None

    # Collect file list first so we can report real progress
    files_to_backup: list[str] = []
    for rel in config.BACKUP_PATHS:
        full = os.path.join(sa_root, rel)
        if os.path.isfile(full):
            files_to_backup.append(rel)
        elif os.path.isdir(full):
            for root, _dirs, files in os.walk(full):
                for name in files:
                    abs_path = os.path.join(root, name)
                    rel_path = os.path.relpath(abs_path, sa_root)
                    files_to_backup.append(rel_path)

    if not files_to_backup:
        log.warning("No files to back up in %s", sa_root)
        return None

    backup_zip = cache.backup_path("backup")
    os.makedirs(os.path.dirname(backup_zip), exist_ok=True)

    total = len(files_to_backup)
    with zipfile.ZipFile(backup_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for i, rel in enumerate(files_to_backup, 1):
            abs_path = os.path.join(sa_root, rel)
            try:
                zf.write(abs_path, arcname=rel)
            except Exception as e:
                log.warning("Skipping %s during backup: %s", rel, e)
            if progress:
                try:
                    progress(i, total)
                except Exception:
                    pass

    log.info("Backed up %d files to %s", total, backup_zip)
    return backup_zip


def restore_from_backup(
    backup_zip: str,
    sa_root: str,
    progress: Optional[ProgressCb] = None,
) -> int:
    """Restore a previously-created backup .zip over the SA root.

    Returns the number of files restored.
    """
    if not os.path.isfile(backup_zip):
        raise FileNotFoundError(backup_zip)

    os.makedirs(sa_root, exist_ok=True)
    restored = 0
    with zipfile.ZipFile(backup_zip) as zf:
        members = zf.namelist()
        total = len(members)
        for i, name in enumerate(members, 1):
            try:
                zf.extract(name, sa_root)
                restored += 1
            except Exception as e:
                log.warning("Failed to restore %s: %s", name, e)
            if progress:
                try:
                    progress(i, total)
                except Exception:
                    pass
    log.info("Restored %d files from %s", restored, backup_zip)
    return restored
