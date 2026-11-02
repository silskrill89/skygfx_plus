#!/usr/bin/env python3
"""doctor.py - one-shot health check for the skygfx_plus_expIV tooling.

Usage:
  python tools/skytool.py doctor            # full check
  python tools/skytool.py doctor --quiet    # only warnings/failures + summary
  python tools/doctor.py

Exit code: 0 = healthy (WARN allowed), 1 = at least one FAIL.

Also provides `logs` (wired as `skytool logs --errors`): scan skygfx_dbg.log
for non-INFO lines, MISMATCH markers and crash/assert tags.
"""
from __future__ import annotations

import argparse
import configparser
import datetime as dt
import importlib
import importlib.metadata as importlib_metadata
import json
import os
import platform
import re
import sqlite3
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _toolutil import (  # noqa: E402
    GAME_DIR,
    LOG_FILE,
    PROJECT_DIR,
    human_size,
    setup_console,
)

PASS, WARN, FAIL = "PASS", "WARN", "FAIL"
REQUIRED_PACKAGES = {
    # distribution name -> import name (mirrors requirements.txt)
    "watchdog": "watchdog",
    "numpy": "numpy",
    "Pillow": "PIL",
    "yt-dlp": "yt_dlp",
    "PyPDF2": "PyPDF2",
}
CRITICAL_INI_KEYS = (
    "pipeline",
    "ivMode",
    "pipeChainReflEnable",
    "texAlphaClassify",
    "profilerEnable",
)
# Documented in docs/ini-reference.md but deliberately absent from
# skygfx.default.ini (see its "Deliberately absent" header).
EXPECTED_INI_DOC_ONLY = {
    ("SkyGfx", "buildingPipe"),
    ("SkyGfx", "vehiclePipe"),
    ("SkyGfx", "stochastic"),
    ("SkyGfx", "stochasticTexturing"),
}
_STAMP_DATE_RE = re.compile(
    rb"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] \d{4}")
_STAMP_TIME_RE = re.compile(rb"(?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d")
KNOWLEDGE_PATHS = (
    (".serena/memories", "dir"),
    (".opencode/memory-bank", "dir"),
    ("docs/plans", "dir"),
    (".sadie/sadie.db", "file"),
    ("E:/dev(dave)/GTA SA Reverse Engineering Documentation", "dir"),
    ("E:/dev(dave)/SKYGFXPLUS_DOCS", "dir"),
    ("H:/wikis/gtamods/rendering", "dir"),
)


class Check:
    __slots__ = ("status", "name", "detail", "fix")

    def __init__(self, status, name, detail="", fix=""):
        self.status, self.name, self.detail, self.fix = status, name, detail, fix


def _mtime(path):
    try:
        return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(path.stat().st_mtime))
    except OSError:
        return "?"


def _run(cmd, cwd=None):
    try:
        return subprocess.run(
            cmd, cwd=str(cwd) if cwd else None, capture_output=True,
            text=True, encoding="utf-8", errors="replace", timeout=20,
        )
    except Exception:  # noqa: BLE001 - report, never crash doctor
        return None


def check_python():
    bits = 64 if sys.maxsize > 2 ** 32 else 32
    ok = sys.version_info >= (3, 8)
    note = "" if bits == 32 else " (32-bit interpreter is the tested default)"
    return Check(PASS if ok else FAIL, "python",
                 f"{platform.python_version()} {bits}-bit {sys.executable}{note}",
                 "" if ok else "install Python 3.10 32-bit and put it on PATH")


def check_packages():
    out = []
    missing = []
    for dist, mod in REQUIRED_PACKAGES.items():
        try:
            importlib.import_module(mod)
            try:
                ver = importlib_metadata.version(dist)
            except Exception:
                ver = "?"
            out.append(Check(PASS, f"dep {dist}", ver))
        except Exception as exc:  # noqa: BLE001
            missing.append(dist)
            out.append(Check(FAIL, f"dep {dist}", f"{type(exc).__name__}: {exc}",
                             "python -m pip install -r requirements.txt"))
    return out


def check_toolchain():
    fxc = Path(r"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe")
    msbuild = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe")
    checks = []
    checks.append(Check(PASS if fxc.is_file() else FAIL, "fxc.exe",
                        str(fxc) if fxc.is_file() else f"missing: {fxc}",
                        "install DirectX SDK (June 2010)"))
    checks.append(Check(PASS if msbuild.is_file() else FAIL, "MSBuild.exe",
                        str(msbuild) if msbuild.is_file() else f"missing: {msbuild}",
                        "install Visual Studio 2022 Enterprise"))
    return checks


def check_deploy():
    checks = []
    if not GAME_DIR.is_dir():
        checks.append(Check(FAIL, "game dir", f"missing: {GAME_DIR}",
                            "create the game directory or set SKYGFX_GAME"))
        checks.append(Check(FAIL, "deploy target", f"missing: {GAME_DIR / 'skygfx.asi'}",
                            "python tools/fast_build.py --fastest"))
        return checks

    checks.append(Check(PASS, "game dir", str(GAME_DIR)))
    asi = GAME_DIR / "skygfx.asi"
    if asi.is_file():
        st = asi.stat()
        checks.append(Check(PASS, "deploy target",
                            f"skygfx.asi {human_size(st.st_size)} mtime {_mtime(asi)}"))
    else:
        checks.append(Check(FAIL, "deploy target", f"missing: {asi}",
                            "python tools/fast_build.py --fastest"))
        return checks

    # Source tree newer than deploy: the exact failure that wastes a test round.
    newest, newest_path = 0.0, None
    roots = [PROJECT_DIR / "src", PROJECT_DIR / "shaders", PROJECT_DIR / "resources"]
    for root in roots:
        if not root.is_dir():
            continue
        for p in root.rglob("*"):
            if not p.is_file():
                continue
            if p.suffix.lower() not in (".cpp", ".h", ".hpp", ".hlsl", ".cso", ".rc"):
                continue
            m = p.stat().st_mtime
            if m > newest:
                newest, newest_path = m, p
    premake = PROJECT_DIR / "premake5.lua"
    if premake.is_file() and premake.stat().st_mtime > newest:
        newest, newest_path = premake.stat().st_mtime, premake

    if newest_path and newest > asi.stat().st_mtime:
        rel = newest_path.relative_to(PROJECT_DIR)
        checks.append(Check(
            WARN, "tree vs deploy",
            f"source newer than skygfx.asi: {rel} {_mtime(newest_path)} > {_mtime(asi)}",
            "python tools/fast_build.py --fastest"))
    else:
        checks.append(Check(PASS, "tree vs deploy", "deployed binary is current"))
    return checks


def check_git():
    if not (PROJECT_DIR / ".git").exists():
        return [Check(WARN, "git", f"not a git repo: {PROJECT_DIR}")]
    branch = _run(["git", "branch", "--show-current"], PROJECT_DIR)
    head = _run(["git", "rev-parse", "--short", "HEAD"], PROJECT_DIR)
    status = _run(["git", "status", "--porcelain=v1"], PROJECT_DIR)
    if status is None or isinstance(status, Exception):
        return [Check(WARN, "git", f"git status failed: {status}")]
    branch_s = (branch.stdout or "?").strip() if branch else "?"
    head_s = (head.stdout or "?").strip() if head else "?"
    lines = [ln for ln in (status.stdout or "").splitlines() if ln.strip()]
    untracked = sum(1 for ln in lines if ln.startswith("??"))
    dirty = len(lines) - untracked
    detail = f"{branch_s} @ {head_s} | {dirty} dirty, {untracked} untracked"
    return [Check(WARN if dirty or untracked else PASS, "git", detail,
                  "git status --short" if dirty or untracked else "")]


def check_knowledge():
    checks = []
    for raw, kind in KNOWLEDGE_PATHS:
        p = PROJECT_DIR / raw if not re.match(r"^[A-Za-z]:", raw) else Path(raw)
        label = raw.replace("\\", "/")
        if kind == "dir":
            if p.is_dir():
                try:
                    n = sum(1 for _ in p.iterdir())
                except OSError:
                    n = 0
                checks.append(Check(PASS, f"knowledge {label}", f"{n} entries"))
            else:
                checks.append(Check(WARN, f"knowledge {label}", f"missing: {p}"))
        else:
            if p.is_file():
                checks.append(Check(PASS, f"knowledge {label}",
                                    f"{human_size(p.stat().st_size)} mtime {_mtime(p)}"))
            else:
                checks.append(Check(WARN, f"knowledge {label}", f"missing: {p}",
                                    "python .sadie/sadie.py init"))
    return checks


def check_opencode_db():
    db = Path.home() / ".local" / "share" / "opencode" / "opencode.db"
    if not db.is_file():
        return [Check(WARN, "opencode db", f"missing: {db}")]
    try:
        con = sqlite3.connect(f"file:{db.as_posix()}?mode=ro", uri=True, timeout=10)
        try:
            con.execute("PRAGMA query_only=1")
            rows = con.execute("SELECT COUNT(*) FROM session").fetchone()[0]
        finally:
            con.close()
        return [Check(PASS, "opencode db",
                      f"read-only OK, {rows} sessions, {human_size(db.stat().st_size)}")]
    except Exception as exc:  # noqa: BLE001
        return [Check(FAIL, "opencode db", f"not readable: {type(exc).__name__}: {exc}",
                      "check the path/ACLs: " + str(db))]


def check_logs():
    checks = []
    if LOG_FILE.is_file():
        checks.append(Check(PASS, "runtime log",
                            f"{LOG_FILE.name} {human_size(LOG_FILE.stat().st_size)} "
                            f"mtime {_mtime(LOG_FILE)}"))
    else:
        checks.append(Check(WARN, "runtime log", f"missing: {LOG_FILE}",
                            "launch the game once: python tools/fast_build.py --launch"))

    premake = PROJECT_DIR / "premake_log.txt"
    if premake.is_file():
        checks.append(Check(PASS, "build log",
                            f"premake_log.txt {human_size(premake.stat().st_size)} "
                            f"mtime {_mtime(premake)}"))
    else:
        checks.append(Check(WARN, "build log", f"missing: {premake}",
                            "run premake: python tools/fast_build.py --rebuild"))

    fp = PROJECT_DIR / ".cache" / "build_fingerprint.json"
    if fp.is_file():
        detail = f"mtime {_mtime(fp)}"
        try:
            data = json.loads(fp.read_text(encoding="utf-8"))
            detail += (f" | src={str(data.get('sources', ''))[:8]}"
                       f" shd={str(data.get('shaders', ''))[:8]}")
        except Exception:
            pass
        checks.append(Check(PASS, "build fingerprint", detail))
    else:
        checks.append(Check(WARN, "build fingerprint", "missing (no successful build recorded)",
                            "python tools/fast_build.py --fastest"))
    return checks


def check_ini():
    ini = GAME_DIR / "skygfx.ini"
    if not ini.is_file():
        return [Check(FAIL, "ini", f"missing: {ini}",
                      "python tools/fast_build.py --deploy")]
    raw = ini.read_bytes()
    text = None
    for enc in ("utf-8-sig", "cp1252", "latin-1"):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    if text is None:
        return [Check(FAIL, "ini", f"cannot decode {ini}",
                      "re-save skygfx.ini as UTF-8")]

    cp = configparser.ConfigParser(strict=False, interpolation=None)
    try:
        cp.read_string(text)
    except Exception as exc:  # noqa: BLE001
        return [Check(FAIL, "ini", f"parse error: {type(exc).__name__}: {exc}",
                      "fix skygfx.ini syntax (or delete it to redeploy a fresh copy)")]
    key_count = sum(len(section) for section in cp.values())
    found, missing = {}, []
    for want in CRITICAL_INI_KEYS:
        for section in cp.sections():
            if cp.has_option(section, want):
                found[want] = cp.get(section, want)
                break
        else:
            missing.append(want)
    detail = f"{key_count} keys | " + ", ".join(f"{k}={found.get(k, 'MISSING')}"
                                                for k in CRITICAL_INI_KEYS)
    if missing:
        return [Check(WARN, "ini", detail,
                      "add missing keys or redeploy the packaged INI")]
    return [Check(PASS, "ini", detail)]


def _import_fast_build():
    """Import the build module (read-only helpers only)."""
    import fast_build  # noqa: PLC0415 - lives next to this file on sys.path
    return fast_build


def _names(items, limit=8):
    items = [str(x) for x in items]
    shown = ", ".join(items[:limit])
    if len(items) > limit:
        shown += f", +{len(items) - limit} more"
    return shown


def _asi_build_stamp(asi):
    """Return (epoch, source) for the ASI's embedded [BUILD] stamp.

    Prefers the compiler's __DATE__/__TIME__ strings next to the
    "[BUILD] asipath=" format string; falls back to the PE COFF
    TimeDateStamp. Epoch is comparable with st_mtime values.
    """
    try:
        data = asi.read_bytes()
    except OSError as exc:
        return None, f"unreadable: {exc}"
    marker = data.find(b"[BUILD] asipath=")
    if marker < 0:
        marker = data.find(b"[BUILD]")
    window = data[max(0, marker - 8192): marker + 8192] if marker >= 0 else data
    date = _STAMP_DATE_RE.search(window)
    clock = _STAMP_TIME_RE.search(window)
    if date and clock:
        raw = date.group(0) + b" " + clock.group(0)
        try:
            parsed = dt.datetime.strptime(raw.decode("ascii"), "%b %d %Y %H:%M:%S")
            return parsed.timestamp(), f"built={raw.decode('ascii')}"
        except ValueError:
            pass
    try:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] == b"PE\x00\x00":
            ts = struct.unpack_from("<I", data, pe + 8)[0]
            if ts:
                return float(ts), "PE TimeDateStamp"
    except (IndexError, struct.error):
        pass
    return None, "[BUILD] stamp not found"


def check_build_stamp():
    """Compare the deployed ASI's [BUILD] stamp with the newest src/shaders mtime."""
    asi = GAME_DIR / "skygfx.asi"
    if not asi.is_file():
        return []  # check_deploy already reports the missing ASI
    stamp, source = _asi_build_stamp(asi)
    if stamp is None:
        return [Check(WARN, "build stamp", f"{asi.name}: {source}")]

    newest, newest_path = 0.0, None
    for root_name in ("src", "shaders"):
        root = PROJECT_DIR / root_name
        if not root.is_dir():
            continue
        for p in root.rglob("*"):
            if p.is_file() and p.suffix.lower() in (".cpp", ".h", ".hpp", ".hlsl"):
                m = p.stat().st_mtime
                if m > newest:
                    newest, newest_path = m, p

    detail = f"{source} ({dt.datetime.fromtimestamp(stamp):%Y-%m-%d %H:%M:%S})"
    if newest_path:
        detail += (f" | newest src/shaders: "
                   f"{newest_path.relative_to(PROJECT_DIR).as_posix()} "
                   f"{_mtime(newest_path)}")
    if newest_path and newest > stamp + 1.0:
        return [Check(WARN, "build stamp", detail + " | STALE deploy",
                      "rebuild+deploy: python tools/fast_build.py --fastest")]
    return [Check(PASS, "build stamp", detail + " | deploy is current")]


def check_shader_census():
    """Report the source->CSO census and the last recorded compile result."""
    try:
        fb = _import_fast_build()
        census = fb.shader_census()
    except Exception as exc:  # noqa: BLE001
        return [Check(WARN, "shader census",
                      f"unavailable: {type(exc).__name__}: {exc}",
                      "python tools/fast_build.py --shaders")]

    parts = [f"{census['producer_sources']}/{census['all_hlsl']} sources -> "
             f"{census['entries']} entries/{len(census['outputs'])} CSOs",
             f"{len(census['unexplained'])} unexplained",
             f"{len(census['orphans'])} CSO no producer"]
    status, fix = PASS, ""

    cache = PROJECT_DIR / ".cache" / "shader_census.json"
    last = None
    if cache.is_file():
        try:
            last = json.loads(cache.read_text(encoding="utf-8"))
        except Exception:
            last = None
    if last is None:
        status = WARN
        fix = "python tools/fast_build.py --shaders"
        parts.append("no compile census cached")
    else:
        parts.append(f"last: {last.get('compiled', '?')} compiled, "
                     f"{last.get('up_to_date', '?')} up-to-date, "
                     f"{last.get('failed', '?')} failed")
        try:
            current = fb.compute_fingerprint(fb.get_all_shader_mtimes())
            if last.get("shader_fingerprint") != current:
                if status != FAIL:
                    status = WARN
                parts.append("census STALE")
                fix = fix or "python tools/fast_build.py --shaders"
        except Exception:  # noqa: BLE001 - staleness is best-effort
            pass
        if last.get("failed"):
            status = FAIL
            fix = "fix failing shaders: python tools/fast_build.py --shaders"

    if census["missing_outputs"]:
        if status != FAIL:
            status = WARN
        parts.append(f"{len(census['missing_outputs'])} expected CSO missing")
        fix = fix or "python tools/fast_build.py --shaders"
    if census["unexplained"]:
        if status != FAIL:
            status = WARN
        parts.append("no-CSO: " + _names(census["unexplained"], 4))
        fix = fix or "add an entry or an intentional-skip reason"
    return [Check(status, "shader census", " | ".join(parts), fix)]


def check_resources():
    """resource.h <-> Resource.rc <-> resources/cso, both directions."""
    try:
        fb = _import_fast_build()
        r = fb.verify_embedded_resources()
    except Exception as exc:  # noqa: BLE001
        return [Check(WARN, "resources", f"unavailable: {type(exc).__name__}: {exc}")]

    hard = []
    if r["missing_rows"]:
        hard.append(f"{len(r['missing_rows'])} define(s) without RC row: "
                    f"{_names(r['missing_rows'])}")
    if r["undefined_rows"]:
        hard.append(f"{len(r['undefined_rows'])} RC row(s) without define: "
                    f"{_names(r['undefined_rows'])}")
    if r["missing_files"]:
        hard.append(f"{len(r['missing_files'])} RC row(s) with missing CSO: "
                    f"{_names(r['missing_files'])}")

    detail = (f"{r['defines']} defines / {r['rows']} rows / "
              f"{r['cso_files']} CSOs | {len(r['no_code'])} IDR unreferenced")
    checks = []
    if hard:
        checks.append(Check(FAIL, "rc <-> cso", detail + " | " + "; ".join(hard),
                            "add the define/RCDATA row or rebuild the CSO"))
    else:
        checks.append(Check(PASS, "rc <-> cso",
                            detail + " | every row resolves to a file"))
    if r["no_idr"]:
        checks.append(Check(WARN, "cso -> rc",
                            f"{len(r['no_idr'])} CSO(s) not embedded: "
                            f"{_names(r['no_idr'])}",
                            "embed them, or confirm they are dev-only/prebuilt"))
    else:
        checks.append(Check(PASS, "cso -> rc",
                            f"all {r['cso_files']} CSOs are embedded"))
    return checks


def _doc_ini_keys(doc_path):
    """(section, key) set from the Keys table in docs/ini-reference.md."""
    text = doc_path.read_text(encoding="utf-8")
    body = text.split("## Keys", 1)[-1].split("## Unresolved", 1)[0]
    section, keys = None, set()
    for line in body.splitlines():
        m = re.match(r"^### `\[(\w+)\]`", line)
        if m:
            section = m.group(1)
            continue
        m = re.match(r"^\| `([^`]+)` \|", line)
        if m and section:
            keys.add((section, m.group(1)))
    return keys


def check_default_ini():
    """skygfx.default.ini must parse and match docs/ini-reference.md."""
    ini = PROJECT_DIR / "skygfx.default.ini"
    doc = PROJECT_DIR / "docs" / "ini-reference.md"
    if not ini.is_file():
        return [Check(FAIL, "default ini", f"missing: {ini}",
                      "restore skygfx.default.ini")]
    if not doc.is_file():
        return [Check(WARN, "default ini keys", f"missing doc: {doc}")]

    try:
        raw = ini.read_text(encoding="utf-8-sig")
    except Exception as exc:  # noqa: BLE001
        return [Check(FAIL, "default ini",
                      f"read error: {type(exc).__name__}: {exc}")]

    cp = configparser.ConfigParser(strict=False, interpolation=None,
                                   inline_comment_prefixes=(";",))
    cp.optionxform = str  # keep camelCase keys
    try:
        cp.read_string(raw)
    except Exception as exc:  # noqa: BLE001
        return [Check(FAIL, "default ini",
                      f"parse error: {type(exc).__name__}: {exc}",
                      "fix skygfx.default.ini syntax")]

    missing_sections = sorted({"General", "SkyGfx", "Modes"} - set(cp.sections()))
    if missing_sections:
        return [Check(FAIL, "default ini",
                      f"missing section(s): {', '.join(missing_sections)}")]
    ini_keys = {(s, k) for s in cp.sections() for k in cp[s]}

    try:
        doc_keys = _doc_ini_keys(doc)
    except Exception as exc:  # noqa: BLE001
        return [Check(WARN, "default ini keys",
                      f"cannot read doc key table: {type(exc).__name__}: {exc}")]
    if not doc_keys:
        return [Check(WARN, "default ini keys",
                      "docs/ini-reference.md key table parsed empty")]

    undocumented = sorted(ini_keys - doc_keys)
    absent = doc_keys - ini_keys
    expected_absent = sorted(absent & EXPECTED_INI_DOC_ONLY)
    unexpected_absent = sorted(absent - EXPECTED_INI_DOC_ONLY)
    detail = (f"{len(ini_keys)} keys | {len(doc_keys)} documented | "
              f"{len(expected_absent)} intentional omission(s)")

    if undocumented:
        return [Check(WARN, "default ini keys",
                      detail + " | undocumented: "
                      + _names([f"[{s}] {k}" for s, k in undocumented]),
                      "add them to docs/ini-reference.md or drop them from the INI")]
    if unexpected_absent:
        return [Check(WARN, "default ini keys",
                      detail + " | documented but absent: "
                      + _names([f"[{s}] {k}" for s, k in unexpected_absent]),
                      "add a default or extend EXPECTED_INI_DOC_ONLY")]
    return [Check(PASS, "default ini keys", detail)]


def run_checks():
    checks = [check_python()]
    checks += check_packages()
    checks += check_toolchain()
    checks += check_deploy()
    checks += check_build_stamp()
    checks += check_shader_census()
    checks += check_resources()
    checks += check_default_ini()
    checks += check_git()
    checks += check_knowledge()
    checks += check_opencode_db()
    checks += check_logs()
    checks += check_ini()
    return checks


def main(argv=None):
    setup_console()
    ap = argparse.ArgumentParser(
        description="Health check for skygfx_plus_expIV tooling",
        epilog="Exit 0 = healthy (warnings allowed), 1 = at least one FAIL.",
    )
    ap.add_argument("--quiet", action="store_true", help="hide PASS lines")
    ap.add_argument("--fix", action="store_true",
                    help="not supported; prints the fix command instead")
    args = ap.parse_args(argv)

    checks = run_checks()
    fails = [c for c in checks if c.status == FAIL]
    warns = [c for c in checks if c.status == WARN]
    passes = [c for c in checks if c.status == PASS]

    print(f"skygfx doctor | {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  repo: {PROJECT_DIR}")
    print(f"  game: {GAME_DIR}")
    for c in checks:
        if args.quiet and c.status == PASS:
            continue
        line = f"[{c.status}] {c.name}"
        if c.detail:
            line += f" - {c.detail}"
        print(line)
        if c.fix and c.status != PASS:
            print(f"       fix: {c.fix}")

    print(f"summary: {len(passes)} pass, {len(warns)} warn, {len(fails)} fail")
    if fails:
        print("FAIL - fix the items above and re-run: python tools/skytool.py doctor")
        if args.fix:
            print("note: --fix is intentionally not implemented; run the fix: commands above")
        return 1
    print("PASS - tooling is ready")
    if args.fix:
        print("note: --fix is intentionally not implemented; nothing to do")
    return 0


LOG_LEVEL_RE = re.compile(r"^\[([^\]]+)\]\s*\[(INFO|WARN|ERROR|FAIL)\s*\]")
LOG_ISSUE_RE = re.compile(
    r"MISMATCH|CRASH|ASSERT|\bERROR\b|UNRESOLVED|DETACH.{0,10}FAIL",
    re.IGNORECASE,
)


def logs_main(argv=None):
    setup_console()
    ap = argparse.ArgumentParser(description="Scan skygfx_dbg.log for problems")
    ap.add_argument("--errors", action="store_true",
                    help="non-INFO + MISMATCH/crash/assert tags (default)")
    ap.add_argument("--all", action="store_true", help="print every line")
    ap.add_argument("--last", "-n", type=int, default=40, help="max lines to print")
    ap.add_argument("--grep", metavar="PAT", help="extra regex filter")
    ap.add_argument("--log", default=str(LOG_FILE), help="log path override")
    args = ap.parse_args(argv)

    log = Path(args.log)
    if not log.is_file():
        print(f"FAIL: log not found: {log}", file=sys.stderr)
        return 2

    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    pat = re.compile(args.grep, re.IGNORECASE) if args.grep else None
    hits = []
    for ln in lines:
        if args.all:
            keep = True
        else:
            m = LOG_LEVEL_RE.match(ln)
            keep = bool(m and m.group(2) != "INFO")
            if not keep and LOG_ISSUE_RE.search(ln):
                keep = True
        if keep and (pat is None or pat.search(ln)):
            hits.append(ln)

    mode = "all" if args.all else "errors"
    print(f"{log.name}: {len(lines)} lines, {len(hits)} matching (mode={mode})")
    for ln in hits[-args.last:]:
        if len(ln) > 280:
            ln = ln[:277] + "..."
        print(ln)
    if not hits:
        print("  (no problem lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
