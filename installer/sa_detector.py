"""Detect GTA San Andreas install location and version.

Strategy (in order):
    1. Windows registry (Uninstall keys for retail / Rockstar launcher).
    2. Steam libraryfolders.vdf — scans every Steam library for appid 12120.
    3. Common default install paths (C:\\Program Files\\Rockstar Games\\...,
       ...\\Steam\\steamapps\\common\\...).
    4. Falls back to None — user must browse manually.

Version check: reads the VS_FIXEDFILEINFO from gta_sa.exe's PE version
resource. v1.0 retail reports 1.0.0.0 (or sometimes 0.0.0.0 for the
HOODLUM crack — in which case we fall back to file-size check).
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from typing import Optional

from . import config


# ----------------------------------------------------------------------
# Public data
# ----------------------------------------------------------------------
@dataclass
class SAInstall:
    root: str
    exe_path: str
    exe_size: int
    version_string: str
    is_v10: bool
    is_steam: bool
    source: str   # "registry" | "steam" | "common_path" | "manual"


# ----------------------------------------------------------------------
# Detector
# ----------------------------------------------------------------------
def detect_install() -> Optional[SAInstall]:
    """Try every detection method, return the first hit or None."""
    for fn in (_from_registry, _from_steam, _from_common_paths):
        result = fn()
        if result:
            return result
    return None


def validate_root(root: str) -> Optional[SAInstall]:
    """Given a user-picked folder, validate it contains a real SA install."""
    if not root or not os.path.isdir(root):
        return None
    for exe_name in ("gta_sa.exe", "gta-sa.exe"):
        exe = os.path.join(root, exe_name)
        if os.path.isfile(exe):
            return _build_install(root, exe, source="manual")
    return None


# ----------------------------------------------------------------------
# Per-method detectors
# ----------------------------------------------------------------------
def _from_registry() -> Optional[SAInstall]:
    """Look in Windows uninstall keys for a GTA San Andreas install path."""
    try:
        import winreg  # type: ignore
    except ImportError:
        return None  # not on Windows

    keys_to_check = [
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Rockstar Games\GTA San Andreas"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Rockstar Games\GTA San Andreas"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 12120"),
        (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Valve\Steam\Apps\12120"),
    ]
    for hive, subkey in keys_to_check:
        try:
            with winreg.OpenKey(hive, subkey) as k:
                for value_name in ("InstallPath", "InstallLocation", "Path", "installed", "Location"):
                    try:
                        val, _ = winreg.QueryValueEx(k, value_name)
                        if isinstance(val, str) and os.path.isdir(val):
                            for exe_name in ("gta_sa.exe", "gta-sa.exe"):
                                exe = os.path.join(val, exe_name)
                                if os.path.isfile(exe):
                                    return _build_install(val, exe, source="registry")
                    except FileNotFoundError:
                        continue
        except (FileNotFoundError, OSError):
            continue
    return None


def _from_steam() -> Optional[SAInstall]:
    """Scan Steam libraryfolders.vdf for appid 12120 (GTA SA)."""
    try:
        import winreg  # type: ignore
    except ImportError:
        return None

    steam_path = None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Valve\Steam") as k:
            steam_path, _ = winreg.QueryValueEx(k, "SteamPath")
    except (FileNotFoundError, OSError):
        steam_path = os.environ.get("STEAM_PATH", r"C:\Program Files (x86)\Steam")

    if not steam_path or not os.path.isdir(steam_path):
        return None

    vdf = os.path.join(steam_path, "steamapps", "libraryfolders.vdf")
    if not os.path.isfile(vdf):
        return None

    try:
        with open(vdf, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None

    import re
    paths = re.findall(r'"path"\s+"([^"]+)"', text)
    paths = [p.replace("\\\\", "\\") for p in paths]
    paths.append(steam_path)

    sa_subdir = os.path.join("steamapps", "common", "Grand Theft Auto San Andreas")
    for lib in paths:
        candidate = os.path.join(lib, sa_subdir)
        if os.path.isdir(candidate):
            exe = os.path.join(candidate, "gta_sa.exe")
            if os.path.isfile(exe):
                return _build_install(candidate, exe, source="steam")
    return None


def _from_common_paths() -> Optional[SAInstall]:
    """Last-ditch check of well-known default install folders."""
    candidates = [
        r"C:\Program Files\Rockstar Games\GTA San Andreas",
        r"C:\Program Files (x86)\Rockstar Games\GTA San Andreas",
        r"C:\Program Files\Rockstar Games\Grand Theft Auto San Andreas",
        r"C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto San Andreas",
        r"D:\Steam\steamapps\common\Grand Theft Auto San Andreas",
        r"E:\Steam\steamapps\common\Grand Theft Auto San Andreas",
        os.path.expanduser(r"~\Documents\GTA San Andreas"),
    ]
    for c in candidates:
        if not c or not os.path.isdir(c):
            continue
        for exe_name in ("gta_sa.exe", "gta-sa.exe"):
            exe = os.path.join(c, exe_name)
            if os.path.isfile(exe):
                return _build_install(c, exe, source="common_path")
    return None


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------
def _build_install(root: str, exe: str, source: str) -> SAInstall:
    size = os.path.getsize(exe)
    ver = _read_exe_version(exe)
    is_v10 = _classify_v10(size, ver)
    is_steam = (size in config.SA_STEAM_EXPECTED_SIZES.values()) or (source == "steam" and not is_v10)
    return SAInstall(
        root=root,
        exe_path=exe,
        exe_size=size,
        version_string=ver,
        is_v10=is_v10,
        is_steam=is_steam,
        source=source,
    )


def _read_exe_version(exe_path: str) -> str:
    """Read VS_FIXEDFILEINFO from a PE file. Returns 'X.Y.Z.W' or '' on failure."""
    try:
        import win32api  # type: ignore
        info = win32api.GetFileVersionInfo(exe_path, "\\")
        ms = info.get("FileVersionMS", 0)
        ls = info.get("FileVersionLS", 0)
        if ms == 0 and ls == 0:
            return ""
        return f"{(ms >> 16) & 0xFFFF}.{ms & 0xFFFF}.{(ls >> 16) & 0xFFFF}.{ls & 0xFFFF}"
    except Exception:
        pass

    try:
        return _read_pe_version_manual(exe_path)
    except Exception:
        return ""


def _read_pe_version_manual(exe_path: str) -> str:
    """Minimal PE version reader — works without pywin32."""
    with open(exe_path, "rb") as f:
        data = f.read()
    # UTF-16LE "VS_VERSION_INFO"
    marker = b"V\x00S\x00_\x00V\x00E\x00R\x00S\x00I\x00O\x00N\x00_\x00I\x00N\x00F\x00O\x00"
    idx = data.find(marker)
    if idx < 0:
        return ""
    # VS_FIXEDFILEINFO starts with signature 0xFEEF04BD
    sig = b"\xbd\x04\xef\xfe"
    sig_idx = data.find(sig, idx)
    if sig_idx < 0:
        return ""
    fixed = data[sig_idx:sig_idx + 52]
    if len(fixed) < 52:
        return ""
    file_ms = struct.unpack("<I", fixed[8:12])[0]
    file_ls = struct.unpack("<I", fixed[12:16])[0]
    if file_ms == 0 and file_ls == 0:
        return ""
    return f"{(file_ms >> 16) & 0xFFFF}.{file_ms & 0xFFFF}.{(file_ls >> 16) & 0xFFFF}.{file_ls & 0xFFFF}"


def _classify_v10(size: int, version: str) -> bool:
    """Decide if this is a v1.0 retail exe (the only mod-compatible version)."""
    if size in config.SA_V10_EXPECTED_SIZES.values():
        return True
    if version:
        parts = version.split(".")
        if len(parts) >= 2:
            try:
                major = int(parts[0])
                minor = int(parts[1])
                if major == 1 and minor == 0:
                    return True
            except ValueError:
                pass
    return False
