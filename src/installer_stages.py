"""High-level install stages for the v2 installer:

    1. detect_version    — hash + size detection of source gta_sa.exe
    2. copy_sa           — clone the source SA folder to the destination
    3. apply_nocd        — if needed, replace dest's gta_sa.exe with the
                            HOODLUM No-CD patched exe (from GCW or user-provided)
    4. backup            — back up the freshly-prepared dest folder
    5. install_mods      — download/locate + extract + merge each mod

The original source SA folder is NEVER modified — everything happens in the
destination folder. This produces a fully portable, standalone modded SA
install that the user can move, copy, or share.
"""
from __future__ import annotations

import logging
import os
import shutil
from dataclasses import dataclass, field
from typing import Callable, Optional

from . import backup, cache, config, downloader, extractor, gamecopyworld, sa_hashes
from . import sa_detector

log = logging.getLogger(__name__)

# Progress callback: (stage_id, stage_message, percent_0_100) -> None
ProgressCb = Callable[[str, str, int], None]


# ----------------------------------------------------------------------
# Install context
# ----------------------------------------------------------------------
@dataclass
class InstallContext:
    # Source: where the user's vanilla SA install lives
    source_sa_root: str
    # Destination: where the wizard creates the standalone modded SA folder
    dest_sa_root: str

    # Mod source choice
    have_local_mods: bool = False
    archives_folder: Optional[str] = None       # only set if have_local_mods

    # NO-CD / downgrade
    needs_downgrade: bool = False
    nocd_patch_path: Optional[str] = None       # user-provided patch archive
    auto_download_nocd: bool = True             # try GCW first

    # Backup
    skip_backup: bool = False

    # Mod selection
    enabled_mod_ids: list[str] = field(default_factory=list)
    mod_sources: list[config.ModSource] = field(default_factory=list)

    # Filled in during the run
    backup_path: Optional[str] = None
    installed_mods: list[str] = field(default_factory=list)
    failed_mods: list[str] = field(default_factory=list)
    detection_result: Optional[sa_hashes.DetectionResult] = None


# ----------------------------------------------------------------------
# Stage: detect version
# ----------------------------------------------------------------------
def stage_detect_version(ctx: InstallContext, progress: ProgressCb) -> bool:
    """Compute SHA-1 + size of source gta_sa.exe and look up the version."""
    progress("detect", "Detecting San Andreas version...", 10)

    # Find the exe
    exe_path: Optional[str] = None
    for name in ("gta_sa.exe", "gta-sa.exe"):
        p = os.path.join(ctx.source_sa_root, name)
        if os.path.isfile(p):
            exe_path = p
            break
    if not exe_path:
        progress("detect", f"ERROR: no gta_sa.exe found in {ctx.source_sa_root}", 100)
        return False

    progress("detect", "Hashing gta_sa.exe (SHA-1)...", 30)
    result = sa_hashes.detect(exe_path)
    ctx.detection_result = result

    if result.matched:
        v = result.matched
        progress("detect",
                 f"Detected: {v.label} (by {result.match_method}). "
                 f"Compatible={v.mod_compatible}, Needs downgrade={v.needs_downgrade}",
                 100)
        ctx.needs_downgrade = v.needs_downgrade
    else:
        # Heuristic fallback
        if result.is_v10:
            progress("detect",
                     f"Likely v1.0 (size={result.file_size:,} bytes, hash not in table). "
                     "Proceeding without downgrade.",
                     100)
            ctx.needs_downgrade = False
        else:
            progress("detect",
                     f"Unknown version (size={result.file_size:,} bytes, hash not in table). "
                     "Will likely need downgrade. The downgrade page will let you pick a "
                     "NO-CD patch manually.",
                     100)
            ctx.needs_downgrade = True

    return True


# ----------------------------------------------------------------------
# Stage: copy vanilla SA to destination
# ----------------------------------------------------------------------
# Files/folders we copy from the source SA install to the destination.
# Everything except user-specific save files and the uninstaller.
COPY_IGNORE_PATTERNS = shutil.ignore_patterns(
    "*.log", "*.tmp", "uninstall*", "Uninstall*", "config.dat",
    "gta_sa.set",  # user settings file — regenerated on first launch
)

COPY_SIZE_THRESHOLD = 100 * 1024 * 1024  # warn if a single file is > 100MB


def stage_copy_sa(ctx: InstallContext, progress: ProgressCb) -> bool:
    """Copy the entire source SA folder to ctx.dest_sa_root.

    This produces a complete, standalone SA install in the destination
    that we can then mod without touching the original.
    """
    src = ctx.source_sa_root
    dst = ctx.dest_sa_root
    if not src or not os.path.isdir(src):
        progress("copy", f"ERROR: source SA folder missing: {src}", 100)
        return False
    if not dst:
        progress("copy", "ERROR: destination folder not set.", 100)
        return False

    # Count files first for progress reporting
    progress("copy", "Counting source files...", 5)
    total = 0
    for _root, _dirs, files in os.walk(src):
        total += len(files)
    if total == 0:
        progress("copy", "ERROR: source SA folder is empty.", 100)
        return False

    os.makedirs(dst, exist_ok=True)
    progress("copy", f"Copying {total:,} files from source SA to destination...", 10)

    # Use shutil.copytree with dirs_exist_ok=True to merge into an existing dest.
    # We do our own file-by-file copy so we can report progress.
    copied = 0
    for root, dirs, files in os.walk(src):
        rel_root = os.path.relpath(root, src)
        target_dir = dst if rel_root == "." else os.path.join(dst, rel_root)
        os.makedirs(target_dir, exist_ok=True)

        # Filter ignored patterns
        kept_files = [f for f in files if not _is_ignored(f)]
        for name in kept_files:
            src_file = os.path.join(root, name)
            dst_file = os.path.join(target_dir, name)
            try:
                if os.path.isfile(dst_file) and os.path.getsize(dst_file) == os.path.getsize(src_file):
                    # Skip identical files (e.g. re-running the wizard)
                    copied += 1
                    continue
                shutil.copy2(src_file, dst_file)
            except Exception as e:
                log.warning("Failed to copy %s: %s", src_file, e)
            copied += 1
            if copied % 50 == 0:
                pct = 10 + int(copied * 80 / max(total, 1))
                progress("copy", f"Copied {copied:,}/{total:,} files...", pct)

    progress("copy", f"Done: copied {copied:,} files to {dst}", 100)
    return True


def _is_ignored(filename: str) -> bool:
    """True if the file matches our copy-ignore patterns."""
    lower = filename.lower()
    if lower.endswith(".log") or lower.endswith(".tmp"):
        return True
    if lower.startswith("uninstall"):
        return True
    if lower == "gta_sa.set":
        return True
    return False


# ----------------------------------------------------------------------
# Stage: apply NO-CD patch (downgrade)
# ----------------------------------------------------------------------
def stage_apply_nocd(ctx: InstallContext, progress: ProgressCb) -> bool:
    """If a NO-CD patch was provided or auto-downloaded, apply it to the dest exe.

    The patch is a .zip / .rar containing a replacement gta_sa.exe. We extract
    it, find the .exe inside, and overwrite the one in the dest folder.
    """
    if not ctx.needs_downgrade:
        progress("nocd", "No downgrade needed — skipping.", 100)
        return True

    # Locate the patch archive
    patch_path = ctx.nocd_patch_path
    if not patch_path and ctx.auto_download_nocd:
        progress("nocd", "Fetching NO-CD patch list from GameCopyWorld...", 10)
        try:
            patches = gamecopyworld.fetch_patches(timeout=20)
            recommended = gamecopyworld.recommended_patch(patches)
        except Exception as e:
            progress("nocd", f"GCW fetch failed: {e}", 100)
            patches = []
            recommended = None

        if not recommended:
            progress("nocd",
                     "Could not auto-find NO-CD patch on GCW (Cloudflare?). "
                     "Download the v1.0 [ALL] No-CD/Fixed EXE manually from "
                     f"{config.GAMECOPYWORLD_SA_URL} and re-run with the patch archive selected.",
                     100)
            return False

        progress("nocd", f"Downloading: {recommended.name} ({recommended.file_size})...", 30)
        patch_path = os.path.join(cache.CACHE_DOWNLOADS, "nocd_patch.zip")
        try:
            gamecopyworld.download_patch(
                recommended, patch_path,
                progress=lambda d, t: progress("nocd",
                    f"Downloading patch... {d // 1024} KiB" + (f"/{t // 1024} KiB" if t else ""),
                    30 + int(d * 40 / max(t or d, 1)))
            )
        except Exception as e:
            progress("nocd", f"Patch download failed: {e}", 100)
            return False

    if not patch_path or not os.path.isfile(patch_path):
        progress("nocd", "No NO-CD patch available — skipping (mods may not work).", 100)
        # Non-fatal: let the user continue and see what happens
        return True

    # Extract the patch into a temp dir
    patch_extract_dir = os.path.join(cache.CACHE_EXTRACTED, "nocd_patch")
    cache.clear_extracted("nocd_patch")
    os.makedirs(patch_extract_dir, exist_ok=True)
    progress("nocd", "Extracting patch...", 75)
    try:
        extractor.extract(patch_path, patch_extract_dir)
    except Exception as e:
        progress("nocd", f"Patch extraction failed: {e}", 100)
        return False

    # Find the patched gta_sa.exe inside
    patched_exe = _find_exe_in_dir(patch_extract_dir)
    if not patched_exe:
        progress("nocd",
                 "Patch archive did not contain a gta_sa.exe. "
                 "Try a different patch.",
                 100)
        return False

    # Overwrite the dest gta_sa.exe
    dest_exe = os.path.join(ctx.dest_sa_root, "gta_sa.exe")
    if not os.path.isfile(dest_exe):
        dest_exe = os.path.join(ctx.dest_sa_root, "gta-sa.exe")
    if not os.path.isfile(dest_exe):
        progress("nocd", "Destination has no gta_sa.exe to replace.", 100)
        return False

    try:
        # Back up the original dest exe first
        backup_exe = dest_exe + ".orig"
        if not os.path.isfile(backup_exe):
            shutil.copy2(dest_exe, backup_exe)
        shutil.copy2(patched_exe, dest_exe)
    except Exception as e:
        progress("nocd", f"Failed to replace exe: {e}", 100)
        return False

    progress("nocd", f"Applied NO-CD patch: {os.path.basename(patched_exe)}", 100)
    return True


def _find_exe_in_dir(d: str) -> Optional[str]:
    """Find the first .exe file in a directory tree (BFS, prefers gta_sa.exe)."""
    if not os.path.isdir(d):
        return None
    # First pass: look for gta_sa.exe by name
    for root, _dirs, files in os.walk(d):
        for name in files:
            if name.lower() in ("gta_sa.exe", "gta-sa.exe"):
                return os.path.join(root, name)
    # Second pass: any .exe
    for root, _dirs, files in os.walk(d):
        for name in files:
            if name.lower().endswith(".exe"):
                return os.path.join(root, name)
    return None


# ----------------------------------------------------------------------
# Stage: backup
# ----------------------------------------------------------------------
def stage_backup(ctx: InstallContext, progress: ProgressCb) -> bool:
    """Back up the freshly-prepared destination folder before mods are applied."""
    if ctx.skip_backup:
        progress("backup", "Skipped (user choice).", 100)
        return True

    progress("backup", "Backing up destination folder...", 0)
    try:
        path = backup.backup_sa_root(ctx.dest_sa_root, progress=lambda c, t: progress(
            "backup", f"Backing up... {c}/{t} files", int(c * 100 / max(t, 1))
        ))
    except Exception as e:
        progress("backup", f"Backup failed: {e}", 100)
        log.exception("Backup failed")
        return False

    ctx.backup_path = path
    if path:
        progress("backup", f"Backup saved: {os.path.basename(path)}", 100)
    else:
        progress("backup", "Nothing to back up.", 100)
    return True


# ----------------------------------------------------------------------
# Stage: install a single mod
# ----------------------------------------------------------------------
def stage_install_mod(
    mod: config.ModSource,
    ctx: InstallContext,
    progress: ProgressCb,
) -> bool:
    """Download (or pull from cache/archives) → extract → merge into dest root."""
    stage_id = f"mod:{mod.id}"

    if mod.id not in ctx.enabled_mod_ids and not mod.is_main_mod:
        progress(stage_id, f"Skipped: {mod.name} (disabled).", 100)
        return True

    # ---- 1. Locate the archive -----------------------------------------
    archive_path: Optional[str] = None

    # 1a. User's local archives folder (only if they said they have mods)
    if ctx.have_local_mods and ctx.archives_folder:
        archives = extractor.scan_archives(ctx.archives_folder)
        hit = extractor.find_mod_in_archives(archives, mod.id, mod.name)
        if hit:
            progress(stage_id, f"Found local archive: {os.path.basename(hit)}", 20)
            archive_path = hit

    # 1b. Shared cache (already-downloaded)
    if not archive_path:
        ext = ""
        if mod.url:
            base = mod.url.split("?")[0].split("/")[-1]
            if "." in base:
                ext = "." + base.rsplit(".", 1)[1].lower()
        if cache.is_cached(mod.url, ext=ext):
            archive_path = cache.cache_path_for_url(mod.url, ext=ext)
            progress(stage_id, "Using cached download.", 20)

    # 1c. Download (only if user opted to download — i.e. not have_local_mods,
    #     OR they have local mods but we couldn't find this one locally)
    if not archive_path:
        if ctx.have_local_mods:
            progress(stage_id,
                     f"Not found in your archives folder. Skipping {mod.name}. "
                     "(Untick 'I have mods' on the mod-source page to download instead.)",
                     100)
            ctx.failed_mods.append(mod.id)
            return False

        ext = ""
        if mod.url:
            base = mod.url.split("?")[0].split("/")[-1]
            if "." in base:
                ext = "." + base.rsplit(".", 1)[1].lower()
        dest = cache.cache_path_for_url(mod.url, ext=ext)
        progress(stage_id, f"Downloading {mod.name}...", 25)
        try:
            def _dl_progress(done, total):
                if total:
                    pct = 25 + int(done * 50 / max(total, 1))
                else:
                    pct = 25
                progress(stage_id, f"Downloading {mod.name}... {done // 1024} KiB", pct)

            downloader.download(
                mod.url,
                dest,
                progress=_dl_progress,
                is_mediafire=mod.is_mediafire,
                is_github_release=mod.is_github_release,
                is_article_page=mod.is_article_page,
                is_libertycity=mod.is_libertycity,
            )
            archive_path = dest
        except Exception as e:
            hint = ""
            if mod.is_article_page:
                hint = (
                    f"  (Article-page mods like {mod.name} often sit behind "
                    "Cloudflare bot protection. Download the .zip manually "
                    f"from {mod.url}, then drop it in your archives folder "
                    "and re-run the wizard.)"
                )
            progress(stage_id, f"Download failed: {e}{hint}", 100)
            log.exception("Download failed for %s", mod.id)
            ctx.failed_mods.append(mod.id)
            return False

    # ---- 2. Extract ----------------------------------------------------
    extracted_dir = cache.extracted_dir_for(mod.id)
    if not cache.is_extracted(mod.id):
        cache.clear_extracted(mod.id)
        progress(stage_id, f"Extracting {mod.name}...", 75)
        try:
            extractor.extract(archive_path, extracted_dir)
        except Exception as e:
            progress(stage_id, f"Extraction failed: {e}", 100)
            log.exception("Extraction failed for %s", mod.id)
            ctx.failed_mods.append(mod.id)
            return False
    else:
        progress(stage_id, f"Using cached extraction for {mod.name}.", 75)

    # ---- 3. Merge into DEST root (not source!) ------------------------
    progress(stage_id, f"Installing {mod.name} into destination...", 90)
    try:
        count = extractor.merge_into_sa_root(
            extracted_dir,
            ctx.dest_sa_root,
            pick_paths=mod.pick_paths or None,
        )
    except Exception as e:
        progress(stage_id, f"Install failed: {e}", 100)
        log.exception("Merge failed for %s", mod.id)
        ctx.failed_mods.append(mod.id)
        return False

    progress(stage_id, f"Done: {mod.name} ({count} files).", 100)
    ctx.installed_mods.append(mod.id)
    return True


# ----------------------------------------------------------------------
# Stage: install all mods in order
# ----------------------------------------------------------------------
def stage_install_all_mods(ctx: InstallContext, progress: ProgressCb) -> bool:
    """Run every mod in install_order. Failures of optional mods are non-fatal."""
    mods = ctx.mod_sources or config.ALL_MODS
    to_run = [m for m in mods if m.is_main_mod or m.id in ctx.enabled_mod_ids]
    total = len(to_run)
    if not total:
        progress("mods", "No mods selected.", 100)
        return True

    for i, mod in enumerate(to_run):
        overall_pct = int(i * 100 / total)
        progress("mods", f"[{i+1}/{total}] {mod.name}", overall_pct)

        def _wrapped(stage_id, msg, pct, _base=overall_pct, _span=int(100/total)):
            progress(stage_id, msg, min(100, _base + int(pct * _span / 100)))

        ok = stage_install_mod(mod, ctx, _wrapped)
        if not ok and not mod.optional:
            progress("mods", f"Aborting: required mod {mod.name} failed.", 100)
            return False

    progress("mods", f"All mods processed. OK={len(ctx.installed_mods)} FAIL={len(ctx.failed_mods)}", 100)
    return True


# ----------------------------------------------------------------------
# Full pipeline
# ----------------------------------------------------------------------
def run_full_install(ctx: InstallContext, progress: ProgressCb) -> bool:
    """Run every stage in order. Returns True if all critical stages passed."""
    cache.ensure_dirs()

    if not stage_detect_version(ctx, progress):
        return False

    if not stage_copy_sa(ctx, progress):
        return False

    if not stage_apply_nocd(ctx, progress):
        # Non-fatal: warn but continue
        log.warning("NO-CD patch stage failed — continuing anyway.")

    if not stage_backup(ctx, progress):
        # Non-fatal: warn but continue
        log.warning("Backup stage failed — continuing anyway.")

    if not stage_install_all_mods(ctx, progress):
        return False

    return True
