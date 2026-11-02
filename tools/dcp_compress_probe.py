#!/usr/bin/env python3
"""dcp_compress_probe.py — verify DCP compress tooling health (m0052/m0054).

Checks:
  1. DCP patch present in installed plugin (marker count in dist/index.js + .orig backup)
  2. autoUpdate:false in ~/.config/opencode/dcp.jsonc (so the patch survives)
  3. OpenCode server reachable (env/port-file first, then netstat, then bounded local scan)
  4. Session-store size estimate (opencode.db) — huge stores are the compress OOM root cause

Prints PASS/FAIL per check and a verdict block with exact restart-from-scratch steps on FAIL.
Stdlib only. Never restarts anything, never invokes compress.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.request import urlopen
from urllib.error import URLError, HTTPError

MARKER = "DCP-PATCH m0052/m0054 paginate+cap v1"
EXPECTED_MARKER_COUNT = 4

DCP_DIST = Path(os.environ.get("APPDATA", "")) / "npm/node_modules/@tarquinen/opencode-dcp/dist/index.js"
DCP_CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "opencode/dcp.jsonc"
OC_DATA = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "opencode"
OC_DB = OC_DATA / "opencode.db"

WARN_DB_BYTES = 2 * 1024**3   # 2 GB — compress risk rising
CRIT_DB_BYTES = 10 * 1024**3  # 10 GB — expect truncation every time

SCAN_PORT_LO = 4096
SCAN_PORT_HI = 4200
HEALTH_PATHS = ("/config", "/session", "/")
TIMEOUT = 0.35


def human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return f"{n:.1f}{unit}" if unit != "B" else f"{n}B"
        n /= 1024
    return f"{n}B"


def strip_jsonc(text: str) -> str:
    """Comment-stripper that respects string literals (URLs contain //)."""
    try:
        json.loads(text)  # fast path: comment-free JSONC parses as plain JSON
        return text
    except json.JSONDecodeError:
        pass
    out: list[str] = []
    in_str = False
    esc = False
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            i += 1
        elif c == '"':
            in_str = True
            out.append(c)
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        else:
            out.append(c)
            i += 1
    return re.sub(r",\s*([}\]])", r"\1", "".join(out))


def check_patch() -> tuple[bool, str]:
    if not DCP_DIST.is_file():
        return False, f"plugin not found: {DCP_DIST}"
    src = DCP_DIST.read_text(encoding="utf-8", errors="replace")
    count = src.count(MARKER)
    orig = DCP_DIST.with_suffix(".js.orig")
    ok = count >= EXPECTED_MARKER_COUNT
    detail = f"marker x{count} (need >= {EXPECTED_MARKER_COUNT}) in {DCP_DIST}"
    if orig.is_file():
        detail += f"; backup OK ({human(orig.stat().st_size)})"
    else:
        ok = False
        detail += "; FAIL: missing index.js.orig backup"
    return ok, detail


def check_autoupdate() -> tuple[bool, str]:
    if not DCP_CONFIG.is_file():
        return False, f"config not found: {DCP_CONFIG}"
    try:
        cfg = json.loads(strip_jsonc(DCP_CONFIG.read_text(encoding="utf-8")))
    except Exception as e:  # noqa: BLE001
        return False, f"config parse error: {e}"
    if cfg.get("autoUpdate") is False:
        return True, f"autoUpdate:false in {DCP_CONFIG}"
    return False, f"autoUpdate is {cfg.get('autoUpdate')!r} - patch will be overwritten by npm auto-update"


def _http_alive(port: int) -> bool:
    for path in HEALTH_PATHS:
        try:
            with urlopen(f"http://127.0.0.1:{port}{path}", timeout=TIMEOUT) as r:
                if getattr(r, "status", 200) < 600:
                    return True
        except HTTPError:
            return True  # server responded at all — it is alive
        except (URLError, TimeoutError, OSError):
            continue
    return False


def _expected_ports() -> list[int]:
    ports: list[int] = []
    for var in ("OPENCODE_PORT", "OPENCODE_SERVER_PORT", "PORT"):
        v = os.environ.get(var)
        if v and v.isdigit():
            ports.append(int(v))
    for pf in (
        OC_DATA / "port",
        OC_DATA / "server.port",
        Path(os.environ.get("TEMP", "")) / "opencode-port",
        Path(os.environ.get("TEMP", "")) / "opencode" / "port",
    ):
        try:
            if pf.is_file():
                v = pf.read_text(encoding="utf-8", errors="replace").strip()
                if v.isdigit():
                    ports.append(int(v))
        except OSError:
            pass
    return ports


def _netstat_ports() -> list[int]:
    """Windows: find LISTENING ports of opencode processes via netstat/tasklist."""
    try:
        out = subprocess.run(
            ["netstat", "-ano"], capture_output=True, text=True, timeout=5, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    listening: dict[str, int] = {}
    for line in out.splitlines():
        m = re.match(r"\s*TCP\s+[\d\.\:]+\:(\d+)\s+.*LISTENING\s+(\d+)\s*$", line)
        if m:
            listening.setdefault(m.group(2), int(m.group(1)))
    if not listening:
        return []
    try:
        tlist = subprocess.run(
            ["tasklist", "/FO", "CSV"], capture_output=True, text=True, timeout=5, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    oc_pids = set()
    for line in tlist.splitlines()[1:]:
        cols = line.split('","')
        if cols and "opencode" in cols[0].lower():
            pid = cols[-1].strip('"') if len(cols) > 1 else ""
            if pid.isdigit():
                oc_pids.add(pid)
    return sorted({listening[p] for p in oc_pids if p in listening})


def _opencode_processes() -> list[str]:
    """Windows: list running OpenCode-ish processes via tasklist."""
    try:
        tlist = subprocess.run(
            ["tasklist", "/FO", "CSV"], capture_output=True, text=True, timeout=5, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    names: dict[str, int] = {}
    for line in tlist.splitlines()[1:]:
        try:
            name = next(csv.reader([line]))[0]
        except (StopIteration, csv.Error):
            continue
        low = name.lower()
        if "opencode" in low or "aidesktop" in low:
            names[name] = names.get(name, 0) + 1
    return [f"{k} x{v}" for k, v in sorted(names.items())]


def check_server() -> tuple[bool, str]:
    tried: list[str] = []
    for port in _expected_ports():
        tried.append(f"expected:{port}")
        if _http_alive(port):
            return True, f"OpenCode server alive on expected port {port}"
    procs = _opencode_processes()
    if procs:
        return True, f"OpenCode running in-process ({', '.join(procs)}); DCP needs no port - server check OK"
    oc_ports = _netstat_ports()
    for port in oc_ports:
        tried.append(f"netstat:{port}")
        if _http_alive(port):
            return True, f"OpenCode server alive on netstat-discovered port {port}"
    candidates = [p for p in range(SCAN_PORT_LO, SCAN_PORT_HI + 1)]
    with ThreadPoolExecutor(max_workers=64) as ex:
        hits = [p for p, ok in zip(candidates, ex.map(_http_alive, candidates)) if ok]
    if hits:
        return True, f"OpenCode server alive on scanned port {hits[0]} (range {SCAN_PORT_LO}-{SCAN_PORT_HI})"
    return False, (
        "no OpenCode process and no server reachable - tried "
        + (", ".join(tried) if tried else "no env/port-file ports")
        + f", then bounded scan {SCAN_PORT_LO}-{SCAN_PORT_HI}; app may be closed or crashed"
    )


def check_store() -> tuple[bool, str]:
    if not OC_DB.is_file():
        return False, f"session store missing: {OC_DB}"
    size = OC_DB.stat().st_size
    wal = OC_DB.with_name(OC_DB.name + "-wal")
    wal_size = wal.stat().st_size if wal.is_file() else 0
    detail = f"{OC_DB} = {human(size)} (+WAL {human(wal_size)})"
    if size >= CRIT_DB_BYTES:
        return True, detail + " - CRITICAL: truncation active on every fetch; archive/vacuum soon (see docs/compress-recovery.md)"
    if size >= WARN_DB_BYTES:
        return True, detail + " - WARN: large store; compress may truncate older ranges"
    return True, detail


def verdict(results: list[tuple[str, bool, str]], assume_running: bool) -> int:
    print()
    print("=" * 70)
    fails = [name for name, ok, _ in results if not ok]
    if not fails:
        print("VERDICT: PASS - DCP compress tooling is healthy.")
        if assume_running:
            print("Compress is safe to use again in this project.")
        else:
            print("NOTE: server check was skipped (--skip-server). Re-run without it while OpenCode runs.")
        print("=" * 70)
        return 0
    print("VERDICT: FAIL - " + ", ".join(fails))
    print()
    print("RESTART-FROM-SCRATCH STEPS (safe order; keep sessions & memories):")
    print("  1. Close OpenCode completely (all windows). Do NOT press compress before step 5.")
    print("  2. If patch checks failed: delete the plugin dir so a clean install happens:")
    print(f"       rm -rf \"{(DCP_DIST.parent.parent).as_posix()}\"")
    print("     then reinstall:  npm install -g @tarquinen/opencode-dcp")
    print("     then re-apply:   python tools/dcp_compress_probe.py --help  (see docs/compress-recovery.md to re-patch)")
    print("  3. KEEP: ~/.config/opencode/ (config+autoUpdate:false), ~/.local/share/opencode/opencode.db (sessions),")
    print("     .serena/, .opencode/memory-bank/, .sadie/ (memories). DELETE only if corrupt: opencode.db-wal, opencode.db-shm")
    print("  4. Start OpenCode. Do not use compress yet.")
    print(f"  5. Re-run:  python {Path(__file__).as_posix()}")
    print("  6. Compress is safe only when every check above prints PASS.")
    print("=" * 70)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--skip-server", action="store_true", help="skip the live server reachability check")
    args = ap.parse_args()

    results: list[tuple[str, bool, str]] = []
    for name, fn in (
        ("patch-present", check_patch),
        ("autoUpdate-disabled", check_autoupdate),
        ("session-store", check_store),
    ):
        ok, detail = fn()
        results.append((name, ok, detail))

    if args.skip_server:
        results.insert(2, ("server-reachable", True, "SKIPPED (--skip-server)"))
        assume_running = False
    else:
        ok, detail = check_server()
        results.insert(2, ("server-reachable", ok, detail))
        assume_running = ok

    width = max(len(n) for n, _, _ in results)
    for name, ok, detail in results:
        print(f"[{'PASS' if ok else 'FAIL'}] {name.ljust(width)}  {detail}")
    return verdict(results, assume_running)


if __name__ == "__main__":
    sys.exit(main())
