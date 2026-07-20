"""Archive extractor supporting .zip, .rar, and .7z.

    .zip  -> zipfile (stdlib)
    .7z   -> py7zr
    .rar  -> rarfile (needs unrar.exe or 7z.exe on PATH on Windows)

All extracted files land in a per-mod subfolder under the shared cache so
re-installs skip extraction. The wizard then copies/merges those files
into the SA root.
"""
from __future__ import annotations

import logging
import os
import shutil
import zipfile
from typing import Callable, Optional

from . import cache

log = logging.getLogger(__name__)

ProgressCb = Callable[[int, int], None]   # (current, total) — best-effort


# ----------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------
def extract(
    archive_path: str,
    dest_dir: str,
    progress: Optional[ProgressCb] = None,
) -> str:
    """Extract any supported archive type into dest_dir.

    Returns dest_dir. Raises on unsupported extension or extraction error.
    """
    if not os.path.isfile(archive_path):
        raise FileNotFoundError(archive_path)

    ext = _ext(archive_path)
    os.makedirs(dest_dir, exist_ok=True)

    if ext == ".zip":
        _extract_zip(archive_path, dest_dir, progress)
    elif ext == ".7z":
        _extract_7z(archive_path, dest_dir, progress)
    elif ext == ".rar":
        _extract_rar(archive_path, dest_dir, progress)
    else:
        raise ValueError(f"Unsupported archive type: {ext}")

    return dest_dir


def supported_extensions() -> tuple[str, ...]:
    return (".zip", ".rar", ".7z")


def is_archive(path: str) -> bool:
    return _ext(path) in supported_extensions()


def scan_archives(folder: str) -> list[str]:
    """Return a list of all .zip/.rar/.7z files in `folder` (recursive)."""
    out: list[str] = []
    if not folder or not os.path.isdir(folder):
        return out
    for root, _dirs, files in os.walk(folder):
        for name in files:
            if is_archive(name):
                out.append(os.path.join(root, name))
    return out


def find_mod_in_archives(
    archives: list[str],
    mod_id: str,
    mod_name: str,
) -> Optional[str]:
    """Try to find an archive that looks like it contains the given mod.

    Heuristic: match by mod_id or mod_name appearing in the filename
    (case-insensitive). Returns the archive path or None.
    """
    needles = [mod_id.lower().replace("_", " "), mod_id.lower(), mod_name.lower()]
    for arc in archives:
        base = os.path.basename(arc).lower()
        for n in needles:
            if n and n in base:
                return arc
    return None


# ----------------------------------------------------------------------
# Per-type extractors
# ----------------------------------------------------------------------
def _extract_zip(arc: str, dest: str, progress: Optional[ProgressCb]) -> None:
    with zipfile.ZipFile(arc) as zf:
        members = zf.infolist()
        total = len(members)
        for i, m in enumerate(members, 1):
            zf.extract(m, dest)
            if progress:
                try:
                    progress(i, total)
                except Exception:
                    pass


def _extract_7z(arc: str, dest: str, progress: Optional[ProgressCb]) -> None:
    try:
        import py7zr  # type: ignore
    except ImportError as e:
        raise RuntimeError("py7zr not installed — cannot extract .7z files.") from e
    with py7zr.SevenZipFile(arc, mode="r") as z:
        z.extractall(path=dest)
    if progress:
        try:
            progress(1, 1)
        except Exception:
            pass


def _extract_rar(arc: str, dest: str, progress: Optional[ProgressCb]) -> None:
    try:
        import rarfile  # type: ignore
    except ImportError as e:
        raise RuntimeError("rarfile not installed — cannot extract .rar files.") from e

    # On Windows, rarfile needs unrar.exe or 7z.exe on PATH.
    # Try to auto-find 7-Zip's unrar-free fallback.
    try:
        rarfile.UNRAR_TOOL = _find_unrar_tool()
    except Exception:
        pass

    with rarfile.RarFile(arc) as rf:
        members = rf.infolist()
        total = len(members)
        for i, m in enumerate(members, 1):
            rf.extract(m, dest)
            if progress:
                try:
                    progress(i, total)
                except Exception:
                    pass


def _find_unrar_tool() -> str:
    """Find an unrar-compatible binary on Windows."""
    import shutil as sh
    found = sh.which("unrar") or sh.which("UnRAR")
    if found:
        return found
    # 7-Zip can extract .rar but rarfile needs unrar — try common install paths
    candidates = [
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
        r"C:\Program Files\WinRAR\unrar.exe",
        r"C:\Program Files (x86)\WinRAR\unrar.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return "unrar"  # let rarfile raise a clear error if missing


# ----------------------------------------------------------------------
# Merge helper — copies extracted files into the SA root
# ----------------------------------------------------------------------
def merge_into_sa_root(
    extracted_dir: str,
    sa_root: str,
    pick_paths: Optional[list[str]] = None,
) -> int:
    """Copy extracted files into the SA root.

    If pick_paths is set, only those subpaths (relative to extracted_dir)
    are copied. Otherwise the entire extracted_dir is merged.

    Returns the number of files copied.
    """
    count = 0
    if pick_paths:
        for rel in pick_paths:
            src = os.path.join(extracted_dir, rel)
            if not os.path.exists(src):
                log.warning("pick_path missing in extracted archive: %s", rel)
                continue
            dst = os.path.join(sa_root, rel)
            if os.path.isdir(src):
                count += _copy_tree(src, dst)
            else:
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                count += 1
    else:
        for entry in os.listdir(extracted_dir):
            src = os.path.join(extracted_dir, entry)
            dst = os.path.join(sa_root, entry)
            if os.path.isdir(src):
                count += _copy_tree(src, dst)
            else:
                if os.path.isfile(dst):
                    os.remove(dst)
                shutil.copy2(src, dst)
                count += 1
    return count


def _copy_tree(src: str, dst: str) -> int:
    """Merge src dir into dst (overwrites files, never deletes extras)."""
    count = 0
    os.makedirs(dst, exist_ok=True)
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        target_dir = dst if rel == "." else os.path.join(dst, rel)
        os.makedirs(target_dir, exist_ok=True)
        for name in files:
            src_file = os.path.join(root, name)
            dst_file = os.path.join(target_dir, name)
            if os.path.isfile(dst_file):
                os.remove(dst_file)
            shutil.copy2(src_file, dst_file)
            count += 1
    return count


def _ext(path: str) -> str:
    p = path.lower()
    for e in (".zip", ".rar", ".7z"):
        if p.endswith(e):
            return e
    return os.path.splitext(p)[1]
