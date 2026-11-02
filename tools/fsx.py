#!/usr/bin/env python3
"""
fsx - bulk file I/O for large-file workflows. Stdlib only.

Two modes:
  CLI:  python tools/fsx.py <cmd> [args]
  MCP:  python tools/fsx.py --serve        (stdio JSON-RPC 2.0)

Why it exists: the assistant's built-in read/write tools cap at 2000 lines and
require file bodies to travel through the model context. fsx reads/writes by
line range, by patches and by *reference to other files*, so multi-thousand-line
edits never cost context and never truncate.

Guarantees
  * EOL preserved per file (CRLF vs LF detected, honored on write; `--eol` overrides)
  * every write auto-backs up to _backup/fsx/<YYYYMMDD>/<relpath>
  * refuses paths outside the repo unless --allow-outside
  * no third-party imports anywhere
"""
import sys
import os
import json
import re
import fnmatch
import hashlib
import datetime
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAX_OUT = 60000  # bytes of text returned to a caller


# ---------------------------------------------------------------- path helpers
def resolve(p, allow_outside=False):
    p = (p or "").replace("\\", "/")
    if p.startswith("~"):
        p = os.path.expanduser(p)
    ap = os.path.normpath(p if os.path.isabs(p) else os.path.join(ROOT, p))
    if not allow_outside:
        root = os.path.realpath(ROOT).lower()
        real = os.path.realpath(ap).lower()
        if not (real == root or real.startswith(root + os.sep)):
            raise FsxError("refusing path outside repo: %s (pass allow_outside)" % p)
    return ap


def rel(ap):
    try:
        return os.path.relpath(ap, ROOT).replace("\\", "/")
    except ValueError:
        return ap.replace("\\", "/")


def read_bytes(ap):
    with open(ap, "rb") as f:
        return f.read()


def decode(b):
    if b.startswith(b"\xef\xbb\xbf"):
        b = b[3:]
    return b.decode("utf-8", "replace")


def split_lines(text):
    """Return list of lines with no trailing EOL, plus the EOL used."""
    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    eol = "\r\n" if crlf >= lf else "\n"
    # normalize for slicing
    norm = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = norm.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
        trailing = True
    else:
        trailing = False
    return lines, eol, trailing


def detect_eol(ap, override=None):
    if override:
        return "\r\n" if override.lower() in ("crlf", "win", "dos") else "\n"
    try:
        b = read_bytes(ap)
        crlf = b.count(b"\r\n")
        lf = b.count(b"\n") - crlf
        return "\r\n" if crlf >= lf else "\n"
    except OSError:
        s = ap.replace("\\", "/").lower()
        crlf_dirs = ("/src/", "/build/", "/resources/")
        crlf_files = ("premake5.lua", "fix_build.py", "debugmenu_ui.cpp", "skygfx.h")
        if any(d in s for d in crlf_dirs) or s.endswith(crlf_files):
            return "\r\n"
        return "\n"


def backup(ap):
    day = datetime.date.today().strftime("%Y%m%d")
    dst = os.path.join(ROOT, "_backup", "fsx", day, rel(ap))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(ap):
        shutil.copy2(ap, dst)
    return rel(dst)


def write_text(ap, text, eol=None, do_backup=True):
    os.makedirs(os.path.dirname(ap) or ROOT, exist_ok=True)
    e = detect_eol(ap, eol)
    body = text.replace("\r\n", "\n").replace("\r", "\n")
    if e != "\n":
        body = body.replace("\n", e)
    bk = backup(ap) if do_backup else None
    with open(ap, "wb") as f:
        f.write(body.encode("utf-8"))
    return bk


class FsxError(Exception):
    pass


# ------------------------------------------------------------------ operations
def op_stat(path):
    ap = resolve(path)
    if not os.path.exists(ap):
        raise FsxError("missing: %s" % path)
    b = read_bytes(ap)
    lines, eol, trailing = split_lines(decode(b))
    st = os.stat(ap)
    return {
        "path": rel(ap),
        "bytes": len(b),
        "lines": len(lines),
        "eol": "CRLF" if eol == "\r\n" else "LF",
        "trailing_newline": trailing,
        "md5": hashlib.md5(b).hexdigest(),
        "mtime": datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
    }


def op_read(path, start=1, end=None, numbered=False, max_lines=400):
    ap = resolve(path)
    lines, eol, _ = split_lines(decode(read_bytes(ap)))
    total = len(lines)
    start = max(1, int(start))
    end = total if end in (None, 0) else min(total, int(end))
    if end - start + 1 > max_lines:
        end = start + max_lines - 1
    out = []
    for i in range(start - 1, end):
        out.append(("%d: %s" % (i + 1, lines[i])) if numbered else lines[i])
    head = "// %s lines %d-%d of %d (%s)\n" % (rel(ap), start, end, total, "CRLF" if eol == "\r\n" else "LF")
    return head + "\n".join(out)


def op_head(path, n=40):
    return op_read(path, 1, int(n), numbered=True)


def op_tail(path, n=40):
    total = op_stat(path)["lines"]
    return op_read(path, max(1, total - int(n) + 1), total, numbered=True)


def op_grep(pattern, paths, regex=False, ignore_case=True, context=0, max_hits=200, glob=None):
    rx = re.compile(pattern if regex else re.escape(pattern),
                    (re.I if ignore_case else 0))
    targets = []
    for p in paths:
        ap = resolve(p)
        if os.path.isdir(ap):
            for dirpath, _dirs, files in os.walk(ap):
                if any(s in dirpath.replace("\\", "/") for s in ("/_backup/", "/.git/", "/build/obj/", "/node_modules/")):
                    continue
                for fn in files:
                    if glob and not fnmatch.fnmatch(fn, glob):
                        continue
                    targets.append(os.path.join(dirpath, fn))
        else:
            targets.append(ap)
    hits = []
    for ap in targets:
        try:
            lines, _eol, _t = split_lines(decode(read_bytes(ap)))
        except OSError:
            continue
        for i, line in enumerate(lines):
            if rx.search(line):
                lo = max(0, i - context)
                hi = min(len(lines), i + context + 1)
                block = []
                for j in range(lo, hi):
                    block.append("%s:%d:%s %s" % (rel(ap), j + 1, ":" if j == i else " ", lines[j][:400]))
                hits.append("\n".join(block))
                if len(hits) >= max_hits:
                    return "// %d hits (capped at %d)\n%s" % (len(hits), max_hits, "\n".join(hits))
    return "// %d hits\n%s" % (len(hits), "\n".join(hits))


def op_find(name, path=".", max_hits=200):
    ap = resolve(path)
    out = []
    for dirpath, _dirs, files in os.walk(ap):
        p = dirpath.replace("\\", "/")
        if any(s in p for s in ("/_backup/", "/.git/", "/build/obj/", "/node_modules/")):
            continue
        for fn in files:
            if fnmatch.fnmatch(fn.lower(), name.lower()):
                out.append(rel(os.path.join(dirpath, fn)))
                if len(out) >= max_hits:
                    return "\n".join(out)
    return "\n".join(out) if out else "(no matches)"


def _load_source(from_file, literal):
    if literal is not None:
        return literal
    if from_file in (None, "-"):
        return sys.stdin.read()
    return decode(read_bytes(resolve(from_file)))


def op_write(path, from_file=None, literal=None, eol=None):
    text = _load_source(from_file, literal)
    ap = resolve(path)
    bk = write_text(ap, text, eol)
    st = op_stat(path)
    return "wrote %s (%d lines, %d bytes, %s)%s" % (
        st["path"], st["lines"], st["bytes"], st["eol"],
        ("\\nbackup: " + bk) if bk else "")


def op_append(path, from_file=None, literal=None, eol=None):
    ap = resolve(path)
    text = _load_source(from_file, literal)
    if os.path.exists(ap):
        cur = decode(read_bytes(ap))
        e = detect_eol(ap, eol)
        sep = "" if cur.endswith("\n") else ("\r\n" if e == "\r\n" else "\n")
        write_text(ap, cur + sep + text, eol)
    else:
        write_text(ap, text, eol)
    st = op_stat(path)
    return "appended to %s -> %d lines" % (st["path"], st["lines"])


def op_insert(path, after, from_file=None, literal=None, eol=None):
    ap = resolve(path)
    lines, eol0, _t = split_lines(decode(read_bytes(ap)))
    new = _load_source(from_file, literal).replace("\r\n", "\n").replace("\r", "\n").rstrip("\n").split("\n")
    n = int(after)
    if n < 0 or n > len(lines):
        raise FsxError("after must be 0..%d, got %d" % (len(lines), n))
    merged = lines[:n] + new + lines[n:]
    write_text(ap, "\n".join(merged) + "\n", eol0)
    st = op_stat(path)
    return "inserted %d lines after %d in %s -> %d lines" % (len(new), n, st["path"], st["lines"])


def op_delete_range(path, start, end, eol=None):
    ap = resolve(path)
    lines, eol0, _t = split_lines(decode(read_bytes(ap)))
    s = max(1, int(start))
    e = min(len(lines), int(end))
    removed = lines[s - 1:e]
    merged = lines[:s - 1] + lines[e:]
    write_text(ap, ("\n".join(merged) + "\n") if merged else "", eol0)
    st = op_stat(path)
    return "deleted %d lines (%d-%d) from %s -> %d lines" % (len(removed), s, e, st["path"], st["lines"])


def op_replace(path, find_file=None, replace_file=None, find_literal=None,
               replace_literal=None, regex=False, count=0, eol=None):
    ap = resolve(path)
    text = decode(read_bytes(ap))
    old = find_literal if find_literal is not None else decode(read_bytes(resolve(find_file)))
    new = replace_literal if replace_literal is not None else (
        decode(read_bytes(resolve(replace_file))) if replace_file else "")
    # EOL-insensitive matching: normalize both sides to \n for the search
    norm = text.replace("\r\n", "\n")
    oldn = old.replace("\r\n", "\n")
    newn = new.replace("\r\n", "\n")
    if regex:
        hits = len(re.findall(oldn, norm))
        out = re.sub(oldn, newn, norm, count=count)
    else:
        hits = norm.count(oldn)
        out = norm.replace(oldn, newn, count if count else -1)
    if hits == 0:
        raise FsxError("pattern not found in %s (%d bytes searched)" % (rel(ap), len(norm)))
    write_text(ap, out, eol)
    st = op_stat(path)
    return "replaced %d occurrence(s) in %s -> %d lines" % (hits, st["path"], st["lines"])


def op_apply_edits(edits, eol=None):
    """edits: [{"path":..., "find_file":..., "replace_file":...}, ...]"""
    log = []
    for i, e in enumerate(edits, 1):
        try:
            log.append("[%d] " % i + op_replace(e["path"],
                                                find_file=e.get("find_file"),
                                                replace_file=e.get("replace_file"),
                                                find_literal=e.get("find"),
                                                replace_literal=e.get("replace"),
                                                regex=e.get("regex", False),
                                                eol=eol))
        except FsxError as ex:
            log.append("[%d] SKIP %s: %s" % (i, e.get("path"), ex))
    return "\n".join(log)


def op_diff(a, b, max_hunks=40):
    ap, bp = resolve(a), resolve(b)
    la, _e1, _t1 = split_lines(decode(read_bytes(ap)))
    lb, _e2, _t2 = split_lines(decode(read_bytes(bp)))
    import difflib
    d = list(difflib.unified_diff(la, lb, rel(ap), rel(bp), lineterm="", n=2))
    if not d:
        return "identical (%d lines each)" % len(la)
    hunks = sum(1 for x in d if x.startswith("@@"))
    body = "\n".join(d[:max_hunks * 12])
    return "// %d changed hunks\n%s" % (hunks, body)


def op_eol(path, convert=None):
    ap = resolve(path)
    st = op_stat(path)
    if not convert:
        return "%s: %s" % (st["path"], st["eol"])
    text = decode(read_bytes(ap))
    write_text(ap, text, convert)
    return "%s: %s -> %s" % (rel(ap), st["eol"], op_stat(path)["eol"])


def op_hash(paths):
    out = []
    for p in paths:
        ap = resolve(p)
        if os.path.isdir(ap):
            for dirpath, _d, files in os.walk(ap):
                for fn in sorted(files):
                    f = os.path.join(dirpath, fn)
                    out.append("%s %s" % (hashlib.md5(read_bytes(f)).hexdigest(), rel(f)))
        else:
            out.append("%s %s" % (hashlib.md5(read_bytes(ap)).hexdigest(), rel(ap)))
    return "\n".join(out)


# ------------------------------------------------------------------------ CLI
USAGE = """fsx - bulk file I/O (stdlib only)

READ
  stat PATH...                      bytes/lines/EOL/md5/mtime
  head PATH [N]                     first N lines, numbered
  tail PATH [N]                     last N lines, numbered
  read PATH [START] [END]           line range (default 1..400 cap), numbered
  grep PATTERN PATH... [--regex] [--glob *.cpp] [--context N] [--max N]
  find NAME_GLOB [DIR]              filename glob search
  diff A B                          unified diff summary
  eol PATH [crlf|lf]                report or convert EOL
  hash PATH...                      md5 list

WRITE (auto-backup + EOL preserve)
  write PATH --from FILE|-          whole-file write from a file/stdin
  append PATH --from FILE|-         append
  insert PATH --after N --from FILE insert block after line N
  delrange PATH START END           delete a line range
  replace PATH --find-file F --replace-file R [--count N] [--regex]
  replace PATH --find "LIT" --replace "LIT"
  apply PATH edits.json              batch [{path,find_file,replace_file},...]

FLAGS: --allow-outside  --eol crlf|lf  --json
"""


def _arg(argv, i, name):
    if i >= len(argv):
        raise FsxError("%s needs a value" % name)
    return argv[i]


def cli(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(USAGE)
        return 0
    cmd, a = argv[0], argv[1:]
    allow = "--allow-outside" in a
    if allow:
        a = [x for x in a if x != "--allow-outside"]
    eol = None
    if "--eol" in a:
        i = a.index("--eol")
        eol = _arg(a, i + 1, "--eol")
        del a[i:i + 2]
    as_json = "--json" in a
    if as_json:
        a = [x for x in a if x != "--json"]

    def opt(name, default=None):
        if name in a:
            i = a.index(name)
            v = _arg(a, i + 1, name)
            del a[i:i + 2]
            return v
        return default

    globals()["_ALLOW"] = allow

    if cmd == "stat":
        rows = [op_stat(p) for p in a]
        print(json.dumps(rows, indent=2) if as_json else
              "\n".join("%s  %d lines  %d bytes  %s  %s" % (r["path"], r["lines"], r["bytes"], r["eol"], r["mtime"]) for r in rows))
    elif cmd == "head":
        print(op_head(a[0], int(a[1]) if len(a) > 1 else 40))
    elif cmd == "tail":
        print(op_tail(a[0], int(a[1]) if len(a) > 1 else 40))
    elif cmd == "read":
        print(op_read(a[0], int(a[1]) if len(a) > 1 else 1, int(a[2]) if len(a) > 2 else None, numbered=True))
    elif cmd == "grep":
        pat = a[0]
        paths = [x for x in a[1:] if not x.startswith("--")]
        print(op_grep(pat, paths or ["."], regex="--regex" in a,
                      context=int(opt("--context", 0) or 0),
                      max_hits=int(opt("--max", 200) or 200),
                      glob=opt("--glob")))
    elif cmd == "find":
        print(op_find(a[0], a[1] if len(a) > 1 else "."))
    elif cmd == "diff":
        print(op_diff(a[0], a[1]))
    elif cmd == "eol":
        print(op_eol(a[0], a[1] if len(a) > 1 else None))
    elif cmd == "hash":
        print(op_hash(a))
    elif cmd == "write":
        print(op_write(a[0], from_file=opt("--from", "-"), literal=opt("--literal"), eol=eol))
    elif cmd == "append":
        print(op_append(a[0], from_file=opt("--from", "-"), literal=opt("--literal"), eol=eol))
    elif cmd == "insert":
        print(op_insert(a[0], int(opt("--after", 0)), from_file=opt("--from", "-"), eol=eol))
    elif cmd == "delrange":
        print(op_delete_range(a[0], a[1], a[2], eol=eol))
    elif cmd == "replace":
        print(op_replace(a[0], find_file=opt("--find-file"),
                         replace_file=opt("--replace-file"),
                         find_literal=opt("--find"),
                         replace_literal=opt("--replace"),
                         regex="--regex" in a,
                         count=int(opt("--count", 0) or 0), eol=eol))
    elif cmd == "apply":
        edits = json.loads(decode(read_bytes(resolve(a[0]))))
        print(op_apply_edits(edits, eol=eol))
    else:
        print("unknown command %r\n%s" % (cmd, USAGE))
        return 2
    return 0


# ------------------------------------------------------------------ MCP server
TOOLS = [
    ("fsx_stat", "Size/lines/EOL/md5/mtime for files.", {"paths": {"type": "array", "items": {"type": "string"}}}, ["paths"]),
    ("fsx_read", "Read a line range of a file (default 1..400, numbered).", {"path": {"type": "string"}, "start": {"type": "integer"}, "end": {"type": "integer"}, "max_lines": {"type": "integer"}}, ["path"]),
    ("fsx_head", "First N lines of a file.", {"path": {"type": "string"}, "n": {"type": "integer"}}, ["path"]),
    ("fsx_tail", "Last N lines of a file.", {"path": {"type": "string"}, "n": {"type": "integer"}}, ["path"]),
    ("fsx_grep", "Regex/literal search with context; compact path:line output.", {"pattern": {"type": "string"}, "paths": {"type": "array", "items": {"type": "string"}}, "regex": {"type": "boolean"}, "context": {"type": "integer"}, "glob": {"type": "string"}, "max_hits": {"type": "integer"}}, ["pattern"]),
    ("fsx_find", "Filename glob search below a directory.", {"name": {"type": "string"}, "path": {"type": "string"}}, ["name"]),
    ("fsx_write", "Overwrite a file from a source file (or literal). Preserves EOL, auto-backups.", {"path": {"type": "string"}, "from_file": {"type": "string"}, "literal": {"type": "string"}, "eol": {"type": "string"}}, ["path"]),
    ("fsx_append", "Append a source file (or literal) to a file.", {"path": {"type": "string"}, "from_file": {"type": "string"}, "literal": {"type": "string"}}, ["path"]),
    ("fsx_insert", "Insert a source file's lines after line N.", {"path": {"type": "string"}, "after": {"type": "integer"}, "from_file": {"type": "string"}, "literal": {"type": "string"}}, ["path", "after"]),
    ("fsx_delete_range", "Delete lines START..END (1-based, inclusive).", {"path": {"type": "string"}, "start": {"type": "integer"}, "end": {"type": "integer"}}, ["path", "start", "end"]),
    ("fsx_replace", "Replace an exact block (from files/literals) in a file. EOL-insensitive.", {"path": {"type": "string"}, "find_file": {"type": "string"}, "replace_file": {"type": "string"}, "find": {"type": "string"}, "replace": {"type": "string"}, "regex": {"type": "boolean"}, "count": {"type": "integer"}}, ["path"]),
    ("fsx_apply", "Apply a batch of replacements from a JSON file.", {"edits_file": {"type": "string"}}, ["edits_file"]),
    ("fsx_diff", "Unified diff summary between two files.", {"a": {"type": "string"}, "b": {"type": "string"}}, ["a", "b"]),
    ("fsx_eol", "Report or convert a file's line endings.", {"path": {"type": "string"}, "convert": {"type": "string"}}, ["path"]),
    ("fsx_hash", "md5 of files/directories.", {"paths": {"type": "array", "items": {"type": "string"}}}, ["paths"]),
]


def _tool_call(name, args):
    A = args or {}
    if name == "fsx_stat":
        return "\n".join(json.dumps(op_stat(p)) for p in A.get("paths", []))
    if name == "fsx_read":
        return op_read(A["path"], A.get("start", 1), A.get("end"), numbered=True,
                       max_lines=int(A.get("max_lines", 400)))
    if name == "fsx_head":
        return op_head(A["path"], int(A.get("n", 40)))
    if name == "fsx_tail":
        return op_tail(A["path"], int(A.get("n", 40)))
    if name == "fsx_grep":
        return op_grep(A["pattern"], A.get("paths") or ["."],
                       regex=A.get("regex", False),
                       context=int(A.get("context", 0)),
                       glob=A.get("glob"),
                       max_hits=int(A.get("max_hits", 200)))
    if name == "fsx_find":
        return op_find(A["name"], A.get("path", "."))
    if name == "fsx_write":
        return op_write(A["path"], from_file=A.get("from_file"), literal=A.get("literal"),
                        eol=A.get("eol"))
    if name == "fsx_append":
        return op_append(A["path"], from_file=A.get("from_file"), literal=A.get("literal"))
    if name == "fsx_insert":
        return op_insert(A["path"], int(A["after"]), from_file=A.get("from_file"),
                         literal=A.get("literal"))
    if name == "fsx_delete_range":
        return op_delete_range(A["path"], A["start"], A["end"])
    if name == "fsx_replace":
        return op_replace(A["path"], find_file=A.get("find_file"),
                          replace_file=A.get("replace_file"),
                          find_literal=A.get("find"), replace_literal=A.get("replace"),
                          regex=A.get("regex", False), count=int(A.get("count", 0)))
    if name == "fsx_apply":
        edits = json.loads(decode(read_bytes(resolve(A["edits_file"]))))
        return op_apply_edits(edits)
    if name == "fsx_diff":
        return op_diff(A["a"], A["b"])
    if name == "fsx_eol":
        return op_eol(A["path"], A.get("convert"))
    if name == "fsx_hash":
        return op_hash(A.get("paths", []))
    raise FsxError("unknown tool %s" % name)


def serve():
    """Minimal MCP stdio server (JSON-RPC 2.0, newline-delimited)."""
    out = sys.stdout
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            msg = json.loads(raw)
        except ValueError:
            continue
        mid = msg.get("id")
        method = msg.get("method", "")
        params = msg.get("params") or {}
        if method in ("notifications/initialized", "notifications/cancelled"):
            continue
        if mid is None and method.startswith("notifications/"):
            continue
        try:
            if method == "initialize":
                result = {
                    "protocolVersion": params.get("protocolVersion", "2024-11-05"),
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "fsx", "version": "1.0.0"},
                }
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": [
                    {"name": n, "description": d,
                     "inputSchema": {"type": "object", "properties": pr,
                                     "required": rq, "additionalProperties": False}}
                    for (n, d, pr, rq) in TOOLS]}
            elif method == "tools/call":
                name = params.get("name")
                text = _tool_call(name, params.get("arguments"))
                if len(text) > MAX_OUT:
                    text = text[:MAX_OUT] + "\n// truncated at %d bytes" % MAX_OUT
                result = {"content": [{"type": "text", "text": text}]}
            else:
                out.write(json.dumps({"jsonrpc": "2.0", "id": mid,
                                      "error": {"code": -32601,
                                                "message": "method not found: %s" % method}}) + "\n")
                out.flush()
                continue
            out.write(json.dumps({"jsonrpc": "2.0", "id": mid, "result": result}) + "\n")
        except FsxError as ex:
            out.write(json.dumps({"jsonrpc": "2.0", "id": mid,
                                  "result": {"content": [{"type": "text", "text": "fsx error: %s" % ex}],
                                             "isError": True}}) + "\n")
        except Exception as ex:  # never let the server die on one bad call
            out.write(json.dumps({"jsonrpc": "2.0", "id": mid,
                                  "result": {"content": [{"type": "text", "text": "fsx crash: %r" % (ex,)}],
                                             "isError": True}}) + "\n")
        out.flush()


if __name__ == "__main__":
    if "--serve" in sys.argv:
        serve()
    else:
        try:
            sys.exit(cli(sys.argv[1:]))
        except FsxError as ex:
            print("fsx: %s" % ex, file=sys.stderr)
            sys.exit(1)
