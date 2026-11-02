#!/usr/bin/env python3
"""Static layering gate for skygfx_plus_expIV.

Implements the mechanically enforceable rules from
``docs/plans/2026-10-24-refactor-blueprint.md`` (section 2.4 / the metrics in
section 0).  It catches architectural drift without waiting for review:

  [1] raw device state writes outside the doctrine TU
  [2] cross-TU mutable globals (non-static, non-const file-scope variables)
  [3] throttle-tag hash collisions (FNV-1a / 256 buckets, as in skygfx.h)
  [4] hook-site verification coverage in src/core/main.cpp

Status policy
-------------
* Without a baseline, any finding is FAIL.  The first run on a dirty tree is
  expected to fail; that is the honest baseline.
* ``--write-baseline`` records the current counts in
  ``tools/layering_baseline.json``.  Subsequent runs then ratchet: a count
  above the baseline is FAIL (new drift), a count at/below it is WARN (known
  debt), zero is PASS.  Check 3 always fails on a collision, per the spec.
* ``--strict`` ignores the baseline and fails on any non-zero count.  Use it
  for burn-down runs.

Exit codes: 0 = pass/warn, 1 = fail, 2 = bad configuration/usage.

CI:  python tools/layering_check.py

No files are written unless ``--write-baseline`` is passed.  Runs from any
cwd; paths are derived from this file (or SKYGFX_PROJECT), matching
``tools/_toolutil.py``.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

try:  # reuse the project's path/console helpers when available
    from _toolutil import PROJECT_DIR, setup_console
except ImportError:  # pragma: no cover - direct import failure fallback
    import os

    TOOLS_DIR = Path(__file__).resolve().parent
    PROJECT_DIR = Path(os.environ.get("SKYGFX_PROJECT") or TOOLS_DIR.parent)

    def setup_console():
        for stream in (sys.stdout, sys.stderr):
            try:
                stream.reconfigure(encoding="utf-8", errors="replace")
            except Exception:
                pass


TOOLS_DIR = PROJECT_DIR / "tools"
SRC_DIR = PROJECT_DIR / "src"
ALLOWLIST_FILE = TOOLS_DIR / "layering_allowlist.txt"
BASELINE_FILE = TOOLS_DIR / "layering_baseline.json"
BASELINE_VERSION = 1

#: The only files allowed to touch the raw device directly.
DOCTRINE_FILES = (
    "src/render/pipelinecommon.cpp",
    "src/render/postfx_utils.cpp",
)

DEVICE_METHODS = (
    "SetRenderState",
    "SetTexture",
    "SetSamplerState",
    "SetVertexShader",
    "SetPixelShader",
    "SetVertexDeclaration",
    "SetFVF",
    "SetStreamSource",
    "SetIndices",
    "SetTextureStageState",
)

#: ``d3d9device->SetRenderState(`` and every other raw-device method.  The
#: receiver is any expression ending in ``->``/``.``; all observed receivers
#: are IDirect3DDevice9 pointers (dev, d3d9device, g_d3dDevice, cfDev, ...).
DEVICE_CALL_RE = re.compile(
    r"(?:->|\.)\s*(?:" + "|".join(DEVICE_METHODS) + r")\s*\("
)

#: Newline offsets are cached so line lookup is O(log n), not O(file).
NEWLINE_RE = re.compile(r"\n")

#: A file-scope definition ends with ``;``, ``{`` (initializer list) or ``=``
#: (initializer on the next line).  Requiring a terminator keeps ordinary
#: function declarations and default-argument parameter lines out.
GLOBAL_DECL_RE = re.compile(
    r"^(?P<type>[A-Za-z_][A-Za-z0-9_:<>,\s\*&]*?)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?:\[[^\]]*\]\s*)*"
    r"(?:=|;)"
)

GLOBAL_EXCLUDE_RE = re.compile(
    r"\b(?:static|extern|const|constexpr|consteval|inline|typedef|using|"
    r"template|class|struct|enum|union|namespace|friend|virtual|explicit|"
    r"operator|return)\b"
)

GLOBAL_LEADING_EXCLUDE = (
    "if", "else", "for", "while", "switch", "case", "default", "do",
    "try", "catch", "public", "private", "protected", "return", "goto",
    "break", "continue", "throw", "delete",
)

#: A dbglog call whose first argument contains one or more string literals.
#: Scanning the whole argument expression (instead of only the direct
#: ``dbglog_x("tag")`` form) also catches ternaries such as
#: ``dbglog_once(cond ? "a" : "b")``.  Tags forwarded through a wrapper
#: function (``dbglog_throttle(tag)``) are not visible without data-flow
#: analysis and stay out of scope; the blueprint measured direct calls too.
DBGLOG_CALL_RE = re.compile(r"\bdbglog_(throttle|once|first)\s*\(")
C_STRING_LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')

HOOK_RE = re.compile(r"\b(?:InterceptCall|InjectHook|Patch\s*<|Nop)\s*\(")

#: Markers that indicate the install site was checked before being patched.
VERIFY_RE = re.compile(
    r"\b0x[eE]8\b"          # expected CALL opcode
    r"|op="                  # op=%02X diagnostics
    r"|\bMISMATCH\b"        # failure branch
    r"|\bmemcmp\b"          # byte comparison
    r"|\bs_\w*Expect\b"     # s_*Expect byte tables
    r"|\bexpected(?:Opcode|Target)\b"
)

STATUS_PASS = "PASS"
STATUS_WARN = "WARN"
STATUS_FAIL = "FAIL"


# ---------------------------------------------------------------------------
# text helpers
# ---------------------------------------------------------------------------

def read_text(path: Path) -> str:
    """UTF-8 read with replacement; strips a UTF-8 BOM if present."""
    text = path.read_text(encoding="utf-8", errors="replace")
    if text.startswith("\ufeff"):
        text = text[1:]
    return text


def sanitize(text: str, *, mask_strings: bool) -> str:
    """Strip comments while preserving line numbers.

    Comment characters become spaces, newlines are kept, and string/char
    literals are preserved unless ``mask_strings`` is set (then their bodies
    become spaces as well).  This keeps every downstream line number aligned
    with the original file.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    state = None  # None | "line" | "block" | "str" | "char"

    while i < n:
        ch = text[i]

        if state is None:
            nxt = text[i + 1] if i + 1 < n else ""
            if ch == "/" and nxt == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if ch == '"':
                state = "str"
                out.append(ch)
                i += 1
                continue
            if ch == "'":
                state = "char"
                out.append(ch)
                i += 1
                continue
            out.append(ch)
            i += 1
            continue

        if state == "line":
            if ch == "\n":
                state = None
                out.append("\n")
            else:
                out.append("\t" if ch == "\t" else " ")
            i += 1
            continue

        if state == "block":
            if ch == "*" and i + 1 < n and text[i + 1] == "/":
                state = None
                out.append("  ")
                i += 2
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
            continue

        # string or char literal
        if mask_strings:
            if ch == "\\" and i + 1 < n:
                out.append(" ")
                out.append("\n" if text[i + 1] == "\n" else " ")
                i += 2
                continue
            out.append("\n" if ch == "\n" else " ")
        else:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue

        if (state == "str" and ch == '"') or (state == "char" and ch == "'"):
            state = None
        elif ch == "\n":  # unterminated literal: recover on the next line
            state = None
        i += 1

    return "".join(out)


def make_line_locator(text: str):
    """Return ``line_of(pos)`` for a text, using cached newline offsets."""
    newlines = [m.start() for m in NEWLINE_RE.finditer(text)]

    def line_of(pos: int) -> int:
        return bisect_right(newlines, pos) + 1

    return line_of


def find_matching_paren(text: str, open_pos: int) -> int:
    """Return the index of the ``)`` matching ``text[open_pos] == '('``.

    String and char literals are skipped so parentheses inside a tag or a
    format string cannot unbalance the scan.  Comments are already stripped
    by ``sanitize``.  Returns -1 if the call is unterminated.
    """
    depth = 1
    i = open_pos + 1
    state = None  # None | "str" | "char"
    while i < len(text):
        ch = text[i]
        if state is None:
            if ch == '"':
                state = "str"
            elif ch == "'":
                state = "char"
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return i
        else:
            if ch == "\\":
                i += 2
                continue
            if (state == "str" and ch == '"') or (state == "char" and ch == "'"):
                state = None
            elif ch == "\n":  # unterminated literal
                state = None
        i += 1
    return -1


def rel(path: Path) -> str:
    try:
        return path.relative_to(PROJECT_DIR).as_posix()
    except ValueError:
        return path.as_posix()


def c_string_value(raw: str) -> str:
    """Decode the handful of C escapes that can appear in a log tag."""
    out: list[str] = []
    i = 0
    while i < len(raw):
        ch = raw[i]
        if ch != "\\" or i + 1 >= len(raw):
            out.append(ch)
            i += 1
            continue
        esc = raw[i + 1]
        simple = {"n": "\n", "r": "\r", "t": "\t", "0": "\0",
                  "\\": "\\", '"': '"', "'": "'"}
        if esc in simple:
            out.append(simple[esc])
            i += 2
            continue
        if esc == "x" and i + 3 < len(raw):
            try:
                out.append(chr(int(raw[i + 2:i + 4], 16)))
                i += 4
                continue
            except ValueError:
                pass
        out.append(esc)
        i += 2
    return "".join(out)


def fnv1a_256(tag: str) -> int:
    h = 2166136261
    for byte in tag.encode("utf-8"):
        h ^= byte
        h = (h * 16777619) & 0xFFFFFFFF
    return h & 255


def x31_64(tag: str) -> int:
    """The *actual* hash used by dbglog_first() in skygfx.h (64 buckets)."""
    h = 0
    for byte in tag.encode("utf-8"):
        h = (h * 31 + byte) & 0xFFFFFFFF
    return (h & 0x7FFFFFFF) % 64


# ---------------------------------------------------------------------------
# source discovery / allowlist
# ---------------------------------------------------------------------------

def source_files() -> list[Path]:
    if not SRC_DIR.is_dir():
        return []
    return sorted(
        p for p in SRC_DIR.rglob("*")
        if p.is_file() and p.suffix.lower() in (".cpp", ".h")
    )


def load_allowlist() -> list[str]:
    """Return allowlist patterns.  A missing file is an empty allowlist."""
    patterns: list[str] = []
    if not ALLOWLIST_FILE.is_file():
        return patterns
    for raw in read_text(ALLOWLIST_FILE).splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        line = line.replace("\\", "/")
        if line.startswith("./"):
            line = line[2:]
        if not (line.startswith("src/") or line.startswith("tools/")):
            # Accept absolute paths under the project root, otherwise store
            # them verbatim (they simply will not match).
            p = Path(line)
            if p.is_absolute():
                try:
                    line = p.resolve().relative_to(PROJECT_DIR).as_posix()
                except (ValueError, OSError):
                    pass
        patterns.append(line)
    return patterns


def path_allowed(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pat) for pat in patterns)


# ---------------------------------------------------------------------------
# check 1 - raw device writes
# ---------------------------------------------------------------------------

def check_raw_writes(files: list[Path], allowlist: list[str]) -> dict:
    per_file: dict[str, int] = defaultdict(int)
    sites: list[dict] = []
    doctrine_counts: dict[str, int] = defaultdict(int)
    allowlisted_counts: dict[str, int] = defaultdict(int)
    scanned = 0

    for path in files:
        relpath = rel(path)
        text = sanitize(read_text(path), mask_strings=True)
        scanned += 1
        line_of = make_line_locator(text)
        for match in DEVICE_CALL_RE.finditer(text):
            method = match.group(0)
            method = method[method.index("Set"):].split("(")[0].strip()
            line = line_of(match.start())
            per_file[relpath] += 1
            if relpath in DOCTRINE_FILES:
                doctrine_counts[relpath] += 1
            elif path_allowed(relpath, allowlist):
                allowlisted_counts[relpath] += 1
            else:
                sites.append({"file": relpath, "line": line, "method": method})

    total = sum(per_file.values())
    doctrine_total = sum(doctrine_counts.values())
    allowlisted_total = sum(allowlisted_counts.values())
    violations = len(sites)

    used_allowlist = {
        pat for pat in allowlist
        if any(fnmatch.fnmatchcase(f, pat) for f in per_file)
    }
    return {
        "files_scanned": scanned,
        "total": total,
        "doctrine": doctrine_total,
        "doctrine_by_file": dict(sorted(doctrine_counts.items())),
        "allowlisted": allowlisted_total,
        "allowlisted_by_file": dict(sorted(allowlisted_counts.items())),
        "violations": violations,
        "by_file": dict(sorted(
            ((f, c) for f, c in per_file.items()
             if f not in DOCTRINE_FILES and not path_allowed(f, allowlist)),
            key=lambda kv: (-kv[1], kv[0]),
        )),
        "all_by_file": dict(sorted(per_file.items())),
        "sites": sorted(sites, key=lambda s: (s["file"], s["line"], s["method"])),
        "allowlist_entries": allowlist,
        "unused_allowlist": sorted(pat for pat in allowlist if pat not in used_allowlist),
    }


# ---------------------------------------------------------------------------
# check 2 - cross-TU mutable globals
# ---------------------------------------------------------------------------

def check_cross_tu_globals(files: list[Path]) -> dict:
    findings: list[dict] = []
    per_file: dict[str, int] = defaultdict(int)

    for path in files:
        if path.suffix.lower() != ".cpp":
            continue
        relpath = rel(path)
        text = sanitize(read_text(path), mask_strings=True)
        depth = 0
        in_macro = False
        for lineno, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()

            # Skip preprocessor directives and their backslash continuations;
            # a macro body is not a declaration and its braces must not skew
            # the depth tracker.
            if in_macro:
                in_macro = stripped.endswith("\\")
                continue
            if stripped.startswith("#"):
                in_macro = stripped.endswith("\\")
                continue

            # A real definition ends with ``;``, or opens an initializer list
            # with ``{``/``=``.  This keeps function parameters with default
            # arguments (``DWORD x = 1,``) out of the report.
            ends_ok = stripped.endswith((";", "{", "="))
            if depth == 0 and stripped and ends_ok:
                first = stripped.split(None, 1)[0].rstrip(":{")
                if first not in GLOBAL_LEADING_EXCLUDE:
                    if not GLOBAL_EXCLUDE_RE.search(stripped):
                        match = GLOBAL_DECL_RE.match(stripped)
                        if match:
                            findings.append({
                                "file": relpath,
                                "line": lineno,
                                "type": match.group("type").strip(),
                                "name": match.group("name"),
                                "decl": stripped[:160],
                            })
                            per_file[relpath] += 1
            depth += line.count("{") - line.count("}")
            if depth < 0:  # malformed input; do not lose the rest of the file
                depth = 0

    findings.sort(key=lambda f: (f["file"], f["line"]))
    return {
        "count": len(findings),
        "by_file": dict(sorted(per_file.items(), key=lambda kv: (-kv[1], kv[0]))),
        "findings": findings,
    }


# ---------------------------------------------------------------------------
# check 3 - throttle tag collisions
# ---------------------------------------------------------------------------

def check_tag_collisions(files: list[Path]) -> dict:
    tags: dict[str, dict] = {}
    family_sites: dict[str, int] = defaultdict(int)
    family_tags: dict[str, set] = defaultdict(set)

    for path in files:
        relpath = rel(path)
        text = sanitize(read_text(path), mask_strings=False)
        line_of = make_line_locator(text)
        for call in DBGLOG_CALL_RE.finditer(text):
            family = call.group(1)
            open_pos = call.end() - 1
            close_pos = find_matching_paren(text, open_pos)
            if close_pos < 0:
                continue
            argument = text[open_pos + 1:close_pos]
            for literal in C_STRING_LITERAL_RE.finditer(argument):
                tag = c_string_value(literal.group(0)[1:-1])
                line = line_of(open_pos + 1 + literal.start())
                family_sites[family] += 1
                family_tags[family].add(tag)
                entry = tags.get(tag)
                if entry is None:
                    entry = tags[tag] = {
                        "tag": tag,
                        "bucket": fnv1a_256(tag),
                        "families": set(),
                        "sites": [],
                    }
                entry["families"].add(family)
                entry["sites"].append(
                    {"file": relpath, "line": line, "family": family}
                )

    buckets: dict[int, list[dict]] = defaultdict(list)
    for tag, entry in tags.items():
        entry["families"] = sorted(entry["families"])
        entry["sites"].sort(key=lambda s: (s["file"], s["line"]))
        buckets[entry["bucket"]].append(entry)

    collisions = []
    colliding_tags = 0
    same_family_buckets = 0
    for bucket, entries in sorted(buckets.items()):
        if len(entries) <= 1:
            continue
        entries.sort(key=lambda e: e["tag"])
        colliding_tags += len(entries)
        family_counts: dict[str, int] = defaultdict(int)
        for entry in entries:
            for family in entry["families"]:
                family_counts[family] += 1
        # The three log functions use separate static arrays, so only a
        # collision inside the same family can actually suppress a log line.
        same_family = any(count > 1 for count in family_counts.values())
        if same_family:
            same_family_buckets += 1
        collisions.append({
            "bucket": bucket,
            "same_family": same_family,
            "tags": [
                {
                    "tag": e["tag"],
                    "families": e["families"],
                    "sites": len(e["sites"]),
                    "first": (e["sites"][0]["file"] + ":" +
                              str(e["sites"][0]["line"])),
                }
                for e in entries
            ],
        })

    # Per-family bucket occupancy: these are the collisions that can actually
    # starve a tag at runtime.  dbglog_first() uses x31/64, not FNV/256.
    family_buckets: dict[str, int] = {}
    for family in ("throttle", "once", "first"):
        buckets_of_family: dict[int, set[str]] = defaultdict(set)
        for tag, entry in tags.items():
            if family in entry["families"]:
                buckets_of_family[fnv1a_256(tag)].add(tag)
        family_buckets[family] = sum(
            1 for values in buckets_of_family.values() if len(values) > 1
        )

    first_runtime_buckets: dict[int, set[str]] = defaultdict(set)
    for tag, entry in tags.items():
        if "first" in entry["families"]:
            first_runtime_buckets[x31_64(tag)].add(tag)
    first_runtime_collisions = sum(
        1 for values in first_runtime_buckets.values() if len(values) > 1
    )

    return {
        "unique_tags": len(tags),
        "call_sites": sum(family_sites.values()),
        "buckets_used": len(buckets),
        "bucket_count": 256,
        "colliding_buckets": len(collisions),
        "colliding_tags": colliding_tags,
        "same_family_buckets": same_family_buckets,
        "cross_family_only_buckets": len(collisions) - same_family_buckets,
        "family_colliding_buckets": family_buckets,
        "first_runtime_collisions": first_runtime_collisions,
        "families": {
            fam: {"sites": family_sites.get(fam, 0),
                  "unique": len(family_tags.get(fam, ()))}
            for fam in ("throttle", "once", "first")
        },
        "collisions": collisions,
    }


# ---------------------------------------------------------------------------
# check 4 - hook verification coverage
# ---------------------------------------------------------------------------

def check_hook_verification(window: int) -> dict:
    main_cpp = SRC_DIR / "core" / "main.cpp"
    if not main_cpp.is_file():
        return {
            "available": False,
            "sites": 0,
            "verified": 0,
            "unverified": 0,
            "unverified_sites": [],
        }

    relpath = rel(main_cpp)
    raw_text = read_text(main_cpp)
    raw_lines = raw_text.splitlines()

    # Comments removed, strings kept: "MISMATCH" in a log format string is a
    # real diagnostic; "MISMATCH" in prose is not.
    scan_text = sanitize(raw_text, mask_strings=False)
    scan_lines = scan_text.splitlines()

    verified = 0
    unverified: list[dict] = []
    total = 0

    for lineno, line in enumerate(scan_lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        matches = list(HOOK_RE.finditer(line))
        if not matches:
            continue
        start = max(0, lineno - 1 - window)
        context = scan_lines[start:lineno - 1]
        has_verify = any(VERIFY_RE.search(ctx) for ctx in context)
        snippet = raw_lines[lineno - 1].strip() if lineno <= len(raw_lines) else ""
        for _ in matches:
            total += 1
            if has_verify:
                verified += 1
            else:
                unverified.append({
                    "file": relpath,
                    "line": lineno,
                    "call": snippet[:140],
                })

    unverified.sort(key=lambda s: s["line"])
    return {
        "available": True,
        "window": window,
        "sites": total,
        "verified": verified,
        "unverified": len(unverified),
        "unverified_sites": unverified,
    }


# ---------------------------------------------------------------------------
# baseline / status
# ---------------------------------------------------------------------------

def load_baseline() -> tuple[dict | None, str | None]:
    if not BASELINE_FILE.is_file():
        return None, None
    try:
        data = json.loads(read_text(BASELINE_FILE))
    except (OSError, json.JSONDecodeError) as exc:
        return None, f"cannot parse {rel(BASELINE_FILE)}: {exc}"
    if not isinstance(data, dict) or not isinstance(data.get("counts"), dict):
        return None, f"{rel(BASELINE_FILE)} has no 'counts' object"
    if data.get("version") != BASELINE_VERSION:
        return None, (f"{rel(BASELINE_FILE)} version "
                      f"{data.get('version')!r} != {BASELINE_VERSION}")
    return data, None


def decide(current: int, baseline: dict | None, key: str, *,
           always_fail_positive: bool = False,
           strict: bool = False) -> tuple[str, str]:
    if current == 0:
        return STATUS_PASS, "clean"
    if always_fail_positive:
        return STATUS_FAIL, "collision is always blocking"
    if strict:
        return STATUS_FAIL, "strict mode: non-zero count"
    if baseline is None:
        return STATUS_FAIL, "no baseline: non-zero count"
    recorded = baseline.get(key)
    if not isinstance(recorded, int):
        return STATUS_FAIL, f"baseline has no '{key}' count"
    if current > recorded:
        return STATUS_FAIL, f"regression: {current} > baseline {recorded} (+{current - recorded})"
    return STATUS_WARN, f"known debt: {current} <= baseline {recorded}"


def build_counts(raw: dict, globals_: dict, tags: dict, hooks: dict) -> dict:
    return {
        "raw_write_violations": raw["violations"],
        "raw_write_sites": raw["total"],
        "cross_tu_globals": globals_["count"],
        "tag_collisions": tags["colliding_buckets"],
        "tag_colliding_tags": tags["colliding_tags"],
        "hook_sites": hooks["sites"],
        "hooks_unverified": hooks["unverified"],
    }


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------

def print_header(baseline_path: bool, baseline_counts: dict | None,
                 allowlist: list[str], baseline_error: str | None) -> None:
    print("skygfx layering check")
    print(f"project: {PROJECT_DIR.as_posix()}")
    if baseline_error:
        print(f"baseline: INVALID - {baseline_error}")
    elif baseline_path:
        print(f"baseline: {rel(BASELINE_FILE)} ({json.dumps(baseline_counts, sort_keys=True)})")
    else:
        print("baseline: none (run --write-baseline to ratchet current counts)")
    if ALLOWLIST_FILE.is_file():
        print(f"allowlist: {rel(ALLOWLIST_FILE)} ({len(allowlist)} entr"
              f"{'y' if len(allowlist) == 1 else 'ies'})")
    else:
        print(f"allowlist: {rel(ALLOWLIST_FILE)} missing (treated as empty)")
    print()


def print_check_header(index: int, title: str, status: str, reason: str) -> None:
    dots = "." * max(3, 62 - len(title))
    print(f"[{index}/4] {title} {dots} {status} ({reason})")


def print_raw(raw: dict, status: str, reason: str, max_findings: int) -> None:
    print_check_header(1, "raw device state writes outside doctrine", status, reason)
    print(f"      scanned {raw['files_scanned']} files | total {raw['total']} sites | "
          f"doctrine {raw['doctrine']} | allowlisted {raw['allowlisted']} | "
          f"violations {raw['violations']}")
    if raw["by_file"]:
        print("      violations per file:")
        width = max(len(f) for f in raw["by_file"])
        for path, count in raw["by_file"].items():
            print(f"        {path:<{width}}  {count:>4}")
    if raw["sites"]:
        print("      violation sites (file:line method):")
        shown = raw["sites"] if max_findings <= 0 else raw["sites"][:max_findings]
        for site in shown:
            print(f"        {site['file']}:{site['line']} {site['method']}")
        hidden = len(raw["sites"]) - len(shown)
        if hidden > 0:
            print(f"        ... {hidden} more (use --max-findings 0 for all)")
    if raw["unused_allowlist"]:
        print(f"      unused allowlist entries: {', '.join(raw['unused_allowlist'])}")
    print()


def print_globals(glob: dict, status: str, reason: str) -> None:
    print_check_header(2, "cross-TU mutable globals", status, reason)
    print(f"      count {glob['count']} in {len(glob['by_file'])} file(s)")
    if glob["by_file"]:
        print("      top offenders:")
        width = max(len(f) for f in glob["by_file"])
        for path, count in glob["by_file"].items():
            print(f"        {path:<{width}}  {count:>3}")
    if glob["findings"]:
        print("      declarations:")
        for finding in glob["findings"]:
            print(f"        {finding['file']}:{finding['line']}  {finding['decl']}")
    print()


def print_tags(tags: dict, status: str, reason: str) -> None:
    print_check_header(3, "throttle-tag hash collisions (FNV-1a/256)", status, reason)
    print(f"      {tags['unique_tags']} unique tags across {tags['call_sites']} call "
          f"sites | {tags['buckets_used']}/{tags['bucket_count']} buckets used")
    print(f"      colliding buckets {tags['colliding_buckets']} involving "
          f"{tags['colliding_tags']} tags ({tags['same_family_buckets']} same-family, "
          f"{tags['cross_family_only_buckets']} cross-family only)")
    fam = tags["families"]
    print("      families (sites/tags/colliding buckets, FNV-1a/256): " + ", ".join(
        f"{name} {fam[name]['sites']}/{fam[name]['unique']}/"
        f"{tags['family_colliding_buckets'][name]}"
        for name in ("throttle", "once", "first")
    ))
    print(f"      dbglog_first runtime hash (x31/64): "
          f"{tags['first_runtime_collisions']} colliding bucket(s)")
    if tags["collisions"]:
        print("      colliding buckets:")
        for collision in tags["collisions"]:
            parts = [f"{t['tag']} ({t['first']})" for t in collision["tags"]]
            suffix = "" if collision["same_family"] else "  [cross-family only]"
            print(f"        bucket {collision['bucket']:>3}: " +
                  ", ".join(parts) + suffix)
    print()


def print_hooks(hooks: dict, status: str, reason: str) -> None:
    print_check_header(4, "hook-site verification coverage (main.cpp)", status, reason)
    if not hooks["available"]:
        print("      src/core/main.cpp not found")
        print()
        return
    print(f"      {hooks['sites']} hook call site(s) | verified {hooks['verified']} | "
          f"unverified {hooks['unverified']} (look-back window "
          f"{hooks['window']} lines)")
    if hooks["unverified_sites"]:
        print("      unverified sites:")
        for site in hooks["unverified_sites"]:
            print(f"        {site['file']}:{site['line']}  {site['call']}")
    print()


def print_text(raw, glob, tags, hooks, statuses, baseline_counts, allowlist,
               baseline_error, max_findings) -> None:
    print_header(bool(baseline_counts), baseline_counts, allowlist, baseline_error)
    print_raw(raw, *statuses["raw"], max_findings=max_findings)
    print_globals(glob, *statuses["globals"])
    print_tags(tags, *statuses["tags"])
    print_hooks(hooks, *statuses["hooks"])

    counts = [s[0] for s in statuses.values()]
    fails = counts.count(STATUS_FAIL)
    warns = counts.count(STATUS_WARN)
    passes = counts.count(STATUS_PASS)
    result = STATUS_FAIL if fails else STATUS_PASS
    print(f"RESULT: {result} - {fails} failed, {warns} warned, {passes} passed")
    labels = {
        "raw": "raw device writes",
        "globals": "mutable globals",
        "tags": "tag collisions",
        "hooks": "hook verification",
    }
    if fails:
        print("Blocking checks: " + ", ".join(
            labels[name] for name, (status, _) in statuses.items()
            if status == STATUS_FAIL
        ))


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Static layering gate (raw device writes, mutable globals, "
                    "log-tag collisions, hook verification).",
        epilog="Exit codes: 0 pass/warn, 1 fail, 2 configuration error.",
    )
    parser.add_argument("--json", action="store_true",
                        help="emit a machine-readable JSON report")
    parser.add_argument("--write-baseline", action="store_true",
                        help=f"write current counts to {rel(BASELINE_FILE)} "
                             "(the only file this tool writes)")
    parser.add_argument("--strict", action="store_true",
                        help="fail on any non-zero count, even when a baseline exists")
    parser.add_argument("--hook-window", type=int, default=40, metavar="N",
                        help="look-back lines for hook-site verification (default: 40)")
    parser.add_argument("--max-findings", type=int, default=0, metavar="N",
                        help="cap the raw-write site list in text output "
                             "(0 = all, default: 0)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    setup_console()
    args = parse_args(argv if argv is not None else sys.argv[1:])

    if not SRC_DIR.is_dir():
        print(f"error: source directory not found: {SRC_DIR}", file=sys.stderr)
        return 2

    args.hook_window = max(0, args.hook_window)

    baseline, baseline_error = load_baseline()
    if baseline_error and not args.write_baseline:
        print(f"error: {baseline_error}", file=sys.stderr)
        print("       fix or delete the baseline file, or run --write-baseline",
              file=sys.stderr)
        return 2
    if args.write_baseline:
        # Re-seeding a corrupt baseline is legal: replace it instead of
        # refusing to run.
        baseline, baseline_error = None, None

    allowlist = load_allowlist()
    files = source_files()

    raw = check_raw_writes(files, allowlist)
    glob = check_cross_tu_globals(files)
    tags = check_tag_collisions(files)
    hooks = check_hook_verification(args.hook_window)

    counts = build_counts(raw, glob, tags, hooks)

    # With --write-baseline the newly written counts are the ratchet for this
    # run; without it, the on-disk baseline (if any) applies.
    effective_baseline = counts if args.write_baseline else (
        baseline["counts"] if baseline else None
    )

    if hooks["available"]:
        hook_status = decide(hooks["unverified"], effective_baseline,
                             "hooks_unverified", strict=args.strict)
    else:
        hook_status = (STATUS_FAIL, "src/core/main.cpp not found")

    statuses = {
        "raw": decide(raw["violations"], effective_baseline,
                      "raw_write_violations", strict=args.strict),
        "globals": decide(glob["count"], effective_baseline,
                          "cross_tu_globals", strict=args.strict),
        "tags": decide(tags["colliding_buckets"], effective_baseline,
                       "tag_collisions", always_fail_positive=True,
                       strict=args.strict),
        "hooks": hook_status,
    }

    if args.write_baseline:
        payload = {"version": BASELINE_VERSION, "counts": counts}
        try:
            BASELINE_FILE.parent.mkdir(parents=True, exist_ok=True)
            BASELINE_FILE.write_text(
                json.dumps(payload, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        except OSError as exc:
            print(f"error: cannot write {rel(BASELINE_FILE)}: {exc}",
                  file=sys.stderr)
            return 2
        if not args.json:
            print(f"baseline written: {rel(BASELINE_FILE)}")

    result = STATUS_FAIL if any(
        status == STATUS_FAIL for status, _ in statuses.values()
    ) else STATUS_PASS

    if args.json:
        report = {
            "tool": "layering_check",
            "version": 1,
            "project": PROJECT_DIR.as_posix(),
            "baseline": {
                "path": rel(BASELINE_FILE),
                "present": bool(effective_baseline),
                "counts": effective_baseline,
            },
            "counts": counts,
            "checks": {
                "raw_writes": {
                    "status": statuses["raw"][0],
                    "reason": statuses["raw"][1],
                    **raw,
                },
                "cross_tu_globals": {
                    "status": statuses["globals"][0],
                    "reason": statuses["globals"][1],
                    **glob,
                },
                "tag_collisions": {
                    "status": statuses["tags"][0],
                    "reason": statuses["tags"][1],
                    **tags,
                },
                "hooks": {
                    "status": statuses["hooks"][0],
                    "reason": statuses["hooks"][1],
                    **hooks,
                },
            },
            "result": result,
        }
        print(json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False))
    else:
        print_text(raw, glob, tags, hooks, statuses, effective_baseline,
                   allowlist, baseline_error, args.max_findings)

    return 1 if result == STATUS_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
