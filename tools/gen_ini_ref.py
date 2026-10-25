#!/usr/bin/env python3
"""Generate docs/ini-reference.md from the INI reader in the source tree.

Static parse only (no build, no game launch):

  src/core/main.cpp    readIniFile()  -> keys, sections, read type, defaults,
                                         call-site clamps, source lines
  src/skygfx.h         struct Config  -> field types
  src/core/modeManager.cpp kValues*   -> which profiles own a field

Anything the parser cannot resolve is emitted in an explicit unresolved table
instead of being guessed. Output is deterministic (no timestamp; a source
fingerprint instead), so running the tool twice produces byte-identical text.

Usage:
    python tools/gen_ini_ref.py              # write docs/ini-reference.md
    python tools/gen_ini_ref.py --check      # exit 1 if the file is stale
    python tools/gen_ini_ref.py --stdout     # print instead of writing
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from _toolutil import PROJECT_DIR, setup_console
except Exception:  # tool must stay standalone if the helper moves
    PROJECT_DIR = Path(__file__).resolve().parent.parent

    def setup_console():
        pass

MAIN_CPP = PROJECT_DIR / "src" / "core" / "main.cpp"
SKYGFX_H = PROJECT_DIR / "src" / "skygfx.h"
MODE_MANAGER = PROJECT_DIR / "src" / "core" / "modeManager.cpp"
OUT_DOC = PROJECT_DIR / "docs" / "ini-reference.md"

READ_FUNCS = ("readint", "readfloat", "readhex")


# ---------------------------------------------------------------------------
# source loading
# ---------------------------------------------------------------------------

def read_source(path: Path) -> str:
    # utf-8-sig: main.cpp carries a BOM; errors=replace keeps a stray byte in a
    # comment from killing the generator on a legacy console.
    return path.read_text(encoding="utf-8-sig", errors="replace")


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def find_function_body(text: str, signature: str) -> tuple[str, int]:
    """Return (body including braces, 1-based line of the opening brace)."""
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[brace : pos + 1], line_of(text, brace)
    raise ValueError(f"unterminated function body for {signature!r}")


# ---------------------------------------------------------------------------
# Config struct
# ---------------------------------------------------------------------------

TYPE_MAP = {
    "int": "int",
    "float": "float",
    "double": "float",
    "bool": "bool",
    "RwBool": "bool (RwBool)",
    "RwInt32": "int",
    "RwUInt32": "int",
    "unsigned": "int",
}


def parse_config_fields(header: str) -> dict[str, str]:
    start = header.index("struct Config {")
    end = header.index("\n};", start)
    body = header[start:end]
    fields: dict[str, str] = {}
    decl = re.compile(
        r"^\s*(int|float|double|bool|RwBool|RwInt32|RwUInt32|unsigned(?:\s+int)?)"
        r"\s+([A-Za-z_]\w*(?:\s*\[[^\]]*\])?(?:\s*,\s*[A-Za-z_]\w*(?:\s*\[[^\]]*\])?)*)\s*;",
        re.M,
    )
    for m in decl.finditer(body):
        ctype = TYPE_MAP.get(m.group(1), m.group(1))
        for raw_name in m.group(2).split(","):
            name = raw_name.strip()
            if "[" in name:
                name = name[: name.index("[")]
                ctype_use = "hex (DWORD)"
            else:
                ctype_use = ctype
            if name:
                fields[name] = ctype_use
    return fields


# ---------------------------------------------------------------------------
# mode ownership
# ---------------------------------------------------------------------------

def parse_mode_ownership(mm: str) -> dict[str, list[str]]:
    """field -> list of mode display names that own it in kValues*."""
    mode_names = {
        "kValuesPS2": "PS2",
        "kValuesXBOX": "Xbox",
        "kValuesPCVintage": "Legacy PC",
        "kValuesGTAIV": "GTA IV",
        "kValuesGTAV": "GTA V",
        "kValuesModern": "Modern",
        "kValuesMobile": "Mobile",
    }
    owned: dict[str, list[str]] = {}
    for table, display in mode_names.items():
        m = re.search(r"static const ModeValue " + table + r"\[\] = \{(.*?)\n\};", mm, re.S)
        if not m:
            continue
        for fm in re.finditer(r"MV_[A-Z_]+\(\s*([A-Za-z_]\w*)", m.group(1)):
            owned.setdefault(fm.group(1), []).append(display)
    return owned


# ---------------------------------------------------------------------------
# StrAssoc maps (defaults for keyword keys)
# ---------------------------------------------------------------------------

def parse_strassoc_maps(text: str) -> dict[str, dict]:
    maps: dict[str, dict] = {}
    for m in re.finditer(r"static StrAssoc (\w+)\[\] = \{(.*?)\n\t\};", text, re.S):
        entries: dict[str, str] = {}
        for em in re.finditer(r'\{\s*"([^"]*)"\s*,\s*([^}]+?)\s*\}', m.group(2)):
            entries[em.group(1)] = em.group(2).strip()
        maps[m.group(1)] = entries
    return maps


# ---------------------------------------------------------------------------
# clamps
# ---------------------------------------------------------------------------

NUM = r"([-+]?(?:\d+\.?\d*[fF]?|\.\d+[fF]?))"


def _num(raw: str) -> str:
    raw = raw.strip()
    if raw.endswith(("f", "F")):
        raw = raw[:-1]
    return raw


def parse_clamp_helpers(text: str) -> dict[str, tuple[str, str, str]]:
    """field -> (lo, hi, helper name) from clamp*(Config *c) bodies."""
    ranges: dict[str, tuple[str, str, str]] = {}
    for fm in re.finditer(
        r"static void\s+(clamp\w+)\(Config \*c\)\s*\{(.*?)\n\}", text, re.S
    ):
        name, body = fm.group(1), fm.group(2)
        # if(c->FIELD < A || c->FIELD > B) ... ternary clamps
        for cm in re.finditer(
            r"c->(\w+)\s*<\s*" + NUM + r"\s*\|\|\s*c->\1\s*>\s*" + NUM, body
        ):
            ranges[cm.group(1)] = (_num(cm.group(2)), _num(cm.group(3)), name)
        # table form: { &c->FIELD, A, B, "tag" }
        for tm in re.finditer(
            r"\{\s*&c->(\w+)\s*,\s*" + NUM + r"\s*,\s*" + NUM + r"\s*,", body
        ):
            ranges[tm.group(1)] = (_num(tm.group(2)), _num(tm.group(3)), name)
    return ranges


def _fmt_bounds(lo: str, hi: str) -> str:
    return f"[{lo}, {hi}]"


def discover_clamps(body: str, field: str, helpers: dict[str, tuple[str, str, str]]) -> str:
    """Best-effort call-site clamp description for one Config field."""
    f = re.escape(field)
    pieces: list[str] = []

    lo: str | None = None
    hi: str | None = None
    for m in re.finditer(
        r"c->" + f + r"\s*<\s*" + NUM + r"\s*\|\|\s*c->" + f + r"\s*>\s*" + NUM,
        body,
    ):
        lo, hi = _num(m.group(1)), _num(m.group(2))
    lower = re.search(r"if\s*\(\s*c->" + f + r"\s*<\s*" + NUM + r"\s*\)", body)
    upper = re.search(r"if\s*\(\s*c->" + f + r"\s*>\s*" + NUM + r"\s*\)", body)
    if lower and lo is None:
        lo = _num(lower.group(1))
    if upper and hi is None:
        hi = _num(upper.group(1))
    if lo is not None and hi is not None:
        pieces.append(_fmt_bounds(lo, hi))
    elif lo is not None:
        pieces.append(f"min {lo}")
    elif hi is not None:
        pieces.append(f"max {hi}")

    # {0,2,4,8} snapping
    snap = re.search(
        r"if\s*\(\s*c->" + f + r"\s*!=\s*(\d+)(?:\s*&&\s*c->" + f + r"\s*!=\s*(\d+))"
        r"(?:\s*&&\s*c->" + f + r"\s*!=\s*(\d+))?\s*\)\s*c->" + f + r"\s*=\s*0;",
        body,
    )
    if snap:
        allowed = ["0"] + [g for g in snap.groups() if g]
        pieces.append("snapped {" + ",".join(dict.fromkeys(allowed)) + "}")
    # envMapSize round-up loop
    if re.search(r"while\s*\(\s*i\s*<\s*c->" + f + r"\s*\)\s*i\s*\*=\s*2;\s*c->" + f + r"\s*=\s*i;", body):
        pieces.append("rounded up to next power of two")
    # boolean pin: if(c->F) c->F = 1;
    if re.search(r"if\s*\(\s*c->" + f + r"\s*\)\s*c->" + f + r"\s*=\s*1;", body):
        pieces.append("0|1")
    # helper clamp applied after the read (call exists anywhere in the reader)
    if field in helpers:
        hlo, hhi, name = helpers[field]
        pieces.append(f"helper {name}(): [{hlo}, {hhi}]")
    return "; ".join(pieces) if pieces else "-"


# ---------------------------------------------------------------------------
# read statements
# ---------------------------------------------------------------------------

SPECIAL_FIELDS = {
    # key: (field, type override, extra note)
    "keySwitch": ("keys[0]", "hex (DWORD)", "readhex; hotkey"),
    "keyReload": ("keys[1]", "hex (DWORD)", "readhex; hotkey"),
    "blurLeft": ("offLeft", "int", "4000 = use defaultColourLeftUOffset"),
    "blurTop": ("offTop", "int", "4000 = use defaultColourTopVOffset"),
    "blurRight": ("offRight", "int", "4000 = use defaultColourRightUOffset"),
    "blurBottom": ("offBottom", "int", "4000 = use defaultColourBottomVOffset"),
    "doRadiosity": ("doRadiosity", "bool", "4000 = use original_bRadiosity"),
    "forceWindShader": ("forceWindShader", "bool", "global flag; only ==1 enables"),
    "colorFilterEnable": ("colorFilterEnable", "bool", "records user intent; PBR forces 1"),
    "qualityPreset": ("g_qualityPreset", "int", "file-static; seeds the preset block"),
    "pipeline": ("pipeline", "int", "keyword map; mode profiles may own pipes"),
    "buildingPipe": ("buildingPipe", "int", "explicit override; ignored by pipe-owning modes"),
    "vehiclePipe": ("vehiclePipe", "int", "explicit override; ignored by pipe-owning modes"),
    "colorFilter": ("colorFilter", "int", "keyword map; PBR forces Modern"),
    "pipelineOverride": ("pipelineOverride", "int", "0..4 applied; -1 = disabled"),
    "active": ("s_activeModeIni", "enum alias", "unknown/absent = None; whitespace around '=' is trimmed"),
    "NoiseQuality": ("g_noiseQuality", "int", "0=32, 1=128, 2=512, >=3=1024 utility noise"),
}

LEGACY_ALIASES = {"stochasticTexturing", "stochastic"}

# Keys that are read outside readIniFile() with a Win32 profile API. They are
# globals, not Config fields, but they are still live skygfx.ini keys, so the
# reference must list them instead of silently omitting them.
EXTERNAL_INI_SOURCES = (
    (PROJECT_DIR / "src" / "render" / "forwardplus.cpp", "int"),
    (PROJECT_DIR / "src" / "rw" / "normalmap.cpp", "string"),
)


def parse_add_if_missing(text: str) -> dict[tuple[str, str], str]:
    """(section, key) -> literal default from the brand-new-INI writer."""
    entries: dict[tuple[str, str], str] = {}
    for m in re.finditer(
        r'ADD_IF_MISSING\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"', text
    ):
        entries[(m.group(1), m.group(2))] = m.group(3)
    return entries


def parse_external_ini_keys(read_keys: set[tuple[str, str]]) -> list[dict]:
    """Live keys read by another TU via GetPrivateProfile* (not Config)."""
    found: list[dict] = []
    for path, kind in EXTERNAL_INI_SOURCES:
        if not path.exists():
            continue
        text = read_source(path)
        for m in re.finditer(
            r'GetPrivateProfile(?:Int|String)A\(\s*"([^"]+)"\s*,\s*"([^"]+)"', text
        ):
            section, key = m.group(1), m.group(2)
            if (section, key) in read_keys:
                continue
            line = line_of(text, m.start())
            found.append(
                {
                    "section": section,
                    "key": key,
                    "type": kind,
                    "default": "UNRESOLVED (reader default not parsed)",
                    "clamp": "-",
                    "field": "",
                    "note": (
                        f"{path.name}:{line}; read via GetPrivateProfile*; "
                        "global, not a Config field"
                    ),
                    "line": line,
                }
            )
            read_keys.add((section, key))
    return found


def parse_reads(body: str, body_line: int, maps: dict[str, dict]) -> tuple[list[dict], list[dict], int]:
    """Return (resolved rows, unresolved rows, dynamic-site count)."""
    rows: list[dict] = []
    unresolved: list[dict] = []
    seen: set[tuple[str, str]] = set()

    # Work line-by-line, joining multi-line call statements via paren balance.
    lines = body.split("\n")
    statements: list[tuple[int, str]] = []
    commented_reads: list[tuple[int, str]] = []
    buf = ""
    buf_line = 0
    depth = 0
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if depth == 0 and not buf:
            if not stripped or stripped in ("{", "}"):
                continue
            if stripped.startswith("//"):
                # Keep commented-out read sites as explicit unresolved rows;
                # ordinary comments must not contaminate the next statement.
                if "cfg.get(" in stripped:
                    commented_reads.append((body_line + idx, stripped))
                continue
            buf_line = body_line + idx
        buf += line + "\n"
        depth += line.count("(") - line.count(")")
        if depth <= 0 and ";" in line:
            statements.append((buf_line, buf))
            buf = ""
            depth = 0
    if buf.strip():
        statements.append((buf_line, buf))

    for line_no, cline in commented_reads:
        cm = re.search(r'cfg\.get\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', cline)
        if cm:
            unresolved.append(
                {
                    "section": cm.group(1),
                    "key": cm.group(2),
                    "line": line_no,
                    "reason": "read site is commented out in the current tree",
                }
            )
            seen.add((cm.group(1), cm.group(2)))

    for stmt_line, stmt in statements:
        if "cfg.get(" not in stmt:
            continue
        # Dynamic key inside the parseShadowToggle lambda: resolved from its
        # call sites below, not a key of its own.
        if re.search(r'cfg\.get\(\s*"[^"]*"\s*,\s*key\s*,', stmt):
            continue
        gm = re.search(
            r'cfg\.get\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"[^"]*"\s*\)', stmt
        )
        if not gm:
            continue
        section, key = gm.group(1), gm.group(2)
        if (section, key) in seen:
            continue

        read_kind = next((fn for fn in READ_FUNCS if fn + "(" in stmt), None)
        sm = re.search(r"StrAssoc::get\(\s*(\w+)\s*,", stmt)
        field = ""
        ftype = "int"
        note_bits: list[str] = []
        default_display = ""

        if sm and not read_kind:
            read_kind = "keyword"
            map_name = sm.group(1)
            map_entries = maps.get(map_name, {})
            default = map_entries.get("", "")
            default_display = f"{default} (map default)" if default else "unresolved (no map sentinel)"
            ftype = "enum keyword"
            note_bits.append(f"map {map_name}")
            # lvalue
            lm = re.search(r"(?:c|config)->(\w+)\s*=\s*StrAssoc::get", stmt)
            if lm:
                field = lm.group(1)
            else:
                lm = re.search(r"int\s+(\w+)\s*=\s*StrAssoc::get", stmt)
                if lm:
                    field = lm.group(1)
                else:
                    lm = re.search(r"(\w+)\s*=\s*StrAssoc::get", stmt)
                    if lm:
                        field = lm.group(1)
        else:
            if not read_kind:
                # std::string local feeding readint/readhex (noiseDetail chain,
                # hotkeys). Resolve from the following statements.
                lm = re.search(r"std::string\s+(\w+)\s*=", stmt)
                local = lm.group(1) if lm else ""
                if key in ("keySwitch", "keyReload"):
                    read_kind = "hex"
                    default = "0 (engine default)"
                elif key == "noiseDetail" or key in LEGACY_ALIASES:
                    read_kind = "int"
                    default = "c->noiseDetail (self)"
                    default_display = "c->noiseDetail (field default)"
                    field = "noiseDetail"
                elif local:
                    read_kind = "string"
                    default = local + " (local)"
                else:
                    unresolved.append(
                        {
                            "section": section,
                            "key": key,
                            "line": stmt_line,
                            "reason": "cfg.get() call has no recognised reader",
                        }
                    )
                    seen.add((section, key))
                    continue
            # default = last readint/readfloat argument
            if not default_display:
                dm = re.search(
                    read_kind + r"\(\s*(?:cfg\.get\([^;]*?\)|[A-Za-z_]\w*)\s*,\s*(.+?)\s*\)",
                    stmt,
                    re.S,
                )
                default = dm.group(1).strip() if dm else "unresolved"
                if default.startswith("c->") or default.startswith("config->"):
                    default_display = default + " (field default)"
                elif default in ("nd",):
                    default_display = "c->noiseDetail (self)"
                else:
                    default_display = default
            # lvalue
            if not field:
                lm = re.search(r"(?:c|config)->(\w+)\s*=\s*(?:\w+\()?", stmt)
                if lm:
                    field = lm.group(1)
                else:
                    lm = re.search(r"\b(\w+)\s*=\s*" + read_kind + r"\(", stmt)
                    if lm:
                        field = lm.group(1)
            ftype = "float" if read_kind == "readfloat" else "int"
            if key in ("keySwitch", "keyReload"):
                ftype = "hex (DWORD)"

        # Special overrides -------------------------------------------------
        if key in SPECIAL_FIELDS:
            sfield, stype, snote = SPECIAL_FIELDS[key]
            if sfield:
                field = sfield
            if stype:
                ftype = stype
            note_bits.append(snote)
            if key == "pipeline":
                default_display = "PBR (map default; alias Modern)"
            elif key == "buildingPipe":
                default_display = "-1 (map default = no override)"
            elif key == "vehiclePipe":
                default_display = "-1 (map default = no override)"
            elif key == "colorFilter":
                default_display = "PC (map default); PBR forces Modern"
            elif key == "active":
                default_display = "None (absent/unknown)"
                ftype = "enum alias"
            elif key in ("keySwitch", "keyReload"):
                default_display = "0 (missing key keeps current binding)"
            elif key == "colorFilterEnable":
                ftype = "bool"

        # Normalise default for self-fallbacks
        if isinstance(default_display, str) and default_display.startswith("c->"):
            pass

        if key == "noiseDetail":
            note_bits.append("current key; legacy aliases stochasticTexturing/stochastic still read")
        elif key in LEGACY_ALIASES:
            note_bits.append("LEGACY ALIAS for noiseDetail; read only when noiseDetail is absent")
        if key in ("pedShadows", "stencilShadows"):
            ftype = "int"
            note_bits.append("bool keywords true/false or numeric; clamped [-1, 1]")

        note = f"main.cpp:{stmt_line}"
        if note_bits:
            note += "; " + "; ".join(note_bits)
        rows.append(
            {
                "section": section,
                "key": key,
                "type": ftype,
                "default": default_display,
                "clamp": "",
                "field": field,
                "line": stmt_line,
                "note": note,
            }
        )
        seen.add((section, key))

    # parseShadowToggle("key", "tag") call sites: 2 keys, default -1.
    for tm in re.finditer(r'parseShadowToggle\(\s*"(\w+)"\s*,', body):
        key = tm.group(1)
        if ("SkyGfx", key) in seen:
            continue
        stmt_line = body_line + body.count("\n", 0, tm.start())
        rows.append(
            {
                "section": "SkyGfx",
                "key": key,
                "type": "int",
                "default": "-1",
                "clamp": "[-1, 1]",
                "field": key,
                "line": stmt_line,
                "note": f"main.cpp:{stmt_line}; via parseShadowToggle (bool keywords + numeric); clamped [-1, 1]",
            }
        )
        seen.add(("SkyGfx", key))

    # Remaining cfg.get sites not covered (diagnostic, not a guess).
    for gm in re.finditer(
        r'cfg\.get\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', body
    ):
        section, key = gm.group(1), gm.group(2)
        if (section, key) in seen:
            continue
        line = body_line + body.count("\n", 0, gm.start())
        snippet_start = body.rfind("\n", 0, gm.start()) + 1
        snippet_end = body.find("\n", gm.end())
        snippet = body[snippet_start:snippet_end].strip()
        if snippet.startswith("//"):
            reason = "read site is commented out"
        elif key == "key":
            reason = "dynamic key parameter (parseShadowToggle lambda)"
        else:
            reason = "not matched by the read-statement parser"
        unresolved.append({"section": section, "key": key, "line": line, "reason": reason})
        seen.add((section, key))

    return rows, unresolved, 1 if "cfg.get(\"SkyGfx\", key" in body else 0


# ---------------------------------------------------------------------------
# markdown
# ---------------------------------------------------------------------------

def esc(text: str) -> str:
    return str(text).replace("|", "\\|")


def render(rows: list[dict], unresolved: list[dict], cfg_fields: dict[str, str],
           owned: dict[str, list[str]], helpers: dict[str, tuple[str, str, str]],
           body: str, hashes: dict[str, str], add_stats: tuple[int, int]) -> str:
    # clamp + type resolution
    for row in rows:
        field = row["field"]
        if field and not field.startswith("keys") and field in cfg_fields:
            row["type"] = cfg_fields[field]
        if field and field in helpers and row["clamp"] == "":
            lo, hi, name = helpers[field]
            row["clamp"] = f"helper {name}(): [{lo}, {hi}]"
        if field and field in owned:
            owners = owned[field]
            if len(owners) >= 5:
                row["note"] += f"; mode-owned ({len(owners)} profiles)"
            else:
                row["note"] += "; mode-owned: " + ", ".join(owners)
        if field:
            direct = discover_clamps(body, field, helpers)
            if direct != "-":
                row["clamp"] = direct if row["clamp"] in ("", "-") else row["clamp"] + "; " + direct
            if (
                field not in cfg_fields
                and not field.startswith("keys")
                and "not a Config field" not in row["note"]
            ):
                row["note"] += "; global, not a Config field"
        if not row["clamp"]:
            row["clamp"] = "-"

    resolved_n = len(rows)
    unresolved_n = len(unresolved)

    out: list[str] = []
    out.append("# INI Reference")
    out.append("")
    out.append(
        "> Generated by `tools/gen_ini_ref.py` from `src/core/main.cpp` "
        "(`readIniFile`), `src/skygfx.h` (`struct Config`) and "
        "`src/core/modeManager.cpp` (`kValues*`). **Do not hand-edit** — re-run:"
    )
    out.append(">")
    out.append("> ```bash")
    out.append("> python tools/gen_ini_ref.py")
    out.append("> ```")
    out.append("")
    out.append(
        f"Source fingerprint: `main.cpp {hashes['main']}` · "
        f"`skygfx.h {hashes['h']}` · `modeManager.cpp {hashes['mode']}`"
    )
    out.append("")
    add_found, add_unread = add_stats
    out.append(
        f"**{resolved_n} keys resolved** ({len([r for r in rows if 'field default' in r['default']])} with a "
        f"preset/field-dependent default, shown explicitly) · **{unresolved_n} unresolved** · "
        f"**{len(cfg_fields)} scalar Config fields** parsed from `struct Config`."
    )
    out.append("")
    out.append(
        f"`ADD_IF_MISSING` cross-check: **{add_found}** default-writer keys found, "
        f"**{add_unread}** of them with no matching read site."
    )
    out.append("")
    out.append(
        "Keys marked `global, not a Config field` are read into file-scope globals "
        "(or by another TU via `GetPrivateProfile*`), not into `struct Config`."
    )
    out.append("")
    out.append(
        "A `default` of `c-><field> (field default)` means the source uses the "
        "current Config value, not a literal. Those defaults come from the "
        "`qualityPreset` block or the previous config; they are **not** "
        "reconstructed here. `-` in `clamp` means the read site applies no clamp."
    )
    out.append("")
    out.append("## Reading this table")
    out.append("")
    out.append(
        "- Section/key are the literal `cfg.get(section, key, ...)` arguments from "
        "`main.cpp readIniFile()`."
    )
    out.append(
        "- Mode-owned fields are applied by `ApplyMode()` **after** the INI read, so a "
        "profile value wins while that mode is active. The note names the owning profiles."
    )
    out.append(
        "- `main.cpp:<line>` is the read statement in the current tree; line numbers drift "
        "as the file grows, the key name is stable."
    )
    out.append("")
    out.append("## Keys")
    out.append("")

    section_order = ["Modes", "General", "SkyGfx"]
    seen_sections = {r["section"] for r in rows}
    for section in section_order + sorted(seen_sections - set(section_order)):
        group = [r for r in rows if r["section"] == section]
        if not group:
            continue
        out.append(f"### `[{section}]`")
        out.append("")
        out.append("| Key | Type | Default | Clamp | Note |")
        out.append("|---|---|---|---|---|")
        for r in group:
            out.append(
                f"| `{esc(r['key'])}` | {esc(r['type'])} | `{esc(r['default'])}` | "
                f"{esc(r['clamp'])} | {esc(r['note'])} |"
            )
        out.append("")
        if section == "Modes":
            out.append(
                "`[Modes] active` accepts the aliases `None`, `PS2`, `Xbox`, `PC`, `GTAIV`, "
                "`GTAV`, `Modern`, `Custom`, `Mobile` (case-insensitive `StrAssoc`). "
                "`linb::ini` trims whitespace on both sides of `=`, so `active = Xbox` and "
                "`active=Xbox` are equivalent."
            )
            out.append("")

    out.append("## Unresolved / dynamic reads")
    out.append("")
    if unresolved:
        out.append(
            "These sites were found in `readIniFile()` but were deliberately **not** "
            "turned into keys: the parser could not prove the key name, the read is "
            "inactive, or the site is a dynamic-key mechanism."
        )
        out.append("")
        out.append("| Section | Key / site | Line | Why unresolved |")
        out.append("|---|---|---|---|")
        for u in unresolved:
            out.append(
                f"| `[{esc(u['section'])}]` | `{esc(u['key'])}` | `main.cpp:{u['line']}` | {esc(u['reason'])} |"
            )
    else:
        out.append("None — every `cfg.get` site in the reader resolved to a literal key.")
    out.append("")
    out.append("## Legacy aliases")
    out.append("")
    out.append(
        "`noiseDetail` is the current key for tiled-texture noise-detail jitter. "
        "`stochasticTexturing` and `stochastic` are **legacy aliases** still read as a "
        "fallback; the loader logs `skygfx: INI key '<name>' is deprecated, use 'noiseDetail'` "
        "when one supplies the value."
    )
    out.append("")
    out.append("## Limits of this reference")
    out.append("")
    out.append(
        "- Static parse only: it never launches the game and never compiles shaders."
    )
    out.append(
        "- Preset-tier values (`qualityPreset=0/1/2/3`) are not expanded into the default "
        "column; only the literal default expression at the read site is shown."
    )
    out.append(
        "- Clamps are the ones lexically visible in `readIniFile()` and the "
        "`clamp*()` helpers. Consumers may clamp again (menu ranges, save path, shader)."
    )
    out.append(
        "- A key listed here is *read*, not necessarily *consumed*. Dead-key claims live "
        "in `docs/feature-matrix.md` and must be checked against consumers, not this table."
    )
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def build() -> tuple[str, int, int, int, int, int]:
    main_text = read_source(MAIN_CPP)
    header_text = read_source(SKYGFX_H)
    mode_text = read_source(MODE_MANAGER)

    body, body_line = find_function_body(main_text, "readIniFile(const char *modulePath, Config *c)")
    # Do not parse the "write all defaults for a brand-new INI" block; it only
    # calls cfg.set(), but stopping there keeps the read region unambiguous.
    cutoff = body.find("if(!iniExisted){")
    if cutoff != -1:
        body = body[:cutoff]

    maps = parse_strassoc_maps(main_text)
    cfg_fields = parse_config_fields(header_text)
    owned = parse_mode_ownership(mode_text)
    helpers = parse_clamp_helpers(main_text)
    rows, unresolved, dynamic_sites = parse_reads(body, body_line, maps)

    add_map = parse_add_if_missing(main_text)
    read_keys = {(r["section"], r["key"]) for r in rows}
    add_unread = sum(1 for k in add_map if k not in read_keys)
    rows.extend(parse_external_ini_keys(read_keys))

    hashes = {
        "main": hashlib.sha256(main_text.encode("utf-8", "replace")).hexdigest()[:12],
        "h": hashlib.sha256(header_text.encode("utf-8", "replace")).hexdigest()[:12],
        "mode": hashlib.sha256(mode_text.encode("utf-8", "replace")).hexdigest()[:12],
    }
    doc = render(rows, unresolved, cfg_fields, owned, helpers, body, hashes,
                 (len(add_map), add_unread))
    return doc, len(rows), len(unresolved), len(cfg_fields), len(add_map), add_unread


def main(argv: list[str]) -> int:
    setup_console()
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if docs/ini-reference.md is stale")
    ap.add_argument("--stdout", action="store_true", help="print the document instead of writing it")
    args = ap.parse_args(argv)

    doc, resolved, unresolved, cfg_count, add_count, add_unread = build()
    if args.stdout:
        print(doc)
    elif args.check:
        if not OUT_DOC.exists() or OUT_DOC.read_text(encoding="utf-8") != doc:
            print(f"STALE: {OUT_DOC.relative_to(PROJECT_DIR)}")
            return 1
        print(f"OK: {OUT_DOC.relative_to(PROJECT_DIR)} ({resolved} keys vs {cfg_count} Config fields, "
              f"ADD_IF_MISSING {add_count}/{add_unread} unread)")
    else:
        OUT_DOC.parent.mkdir(parents=True, exist_ok=True)
        OUT_DOC.write_text(doc, encoding="utf-8", newline="\n")
        print(f"wrote {OUT_DOC.relative_to(PROJECT_DIR)}: {resolved} keys resolved, {unresolved} unresolved, "
              f"{cfg_count} Config fields, ADD_IF_MISSING {add_count} ({add_unread} unread)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
