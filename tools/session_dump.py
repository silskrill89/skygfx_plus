#!/usr/bin/env python3
"""session_dump.py — export OpenCode agent sessions to readable markdown backups.

Reads the OpenCode session store (SQLite: session/message/part tables) and writes one
markdown file per session under .serena/agent-reports/sessions/<session-id>.md.

Usage:
  python tools/session_dump.py --latest 3          # 3 most recently updated sessions
  python tools/session_dump.py --session <id>      # one specific session
  python tools/session_dump.py --all               # every session (1449+ can take a while)
  python tools/session_dump.py --latest 5 --max-chars 2000

Idempotent: re-running overwrites the same files safely. Stdlib only.
Storage discovery order:
  %XDG_DATA_HOME%/opencode/opencode.db  ->  %USERPROFILE%/.local/share/opencode/opencode.db
  %LOCALAPPDATA%/opencode/opencode.db   ->  %APPDATA%/opencode/opencode.db
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

OUT_DIR = Path(".serena/agent-reports/sessions")

DEFAULT_TEXT_CHARS = 8000
DEFAULT_TOOL_CHARS = 800


def find_db() -> Path:
    candidates = []
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        candidates.append(Path(xdg) / "opencode/opencode.db")
    candidates.append(Path.home() / ".local/share/opencode/opencode.db")
    la = os.environ.get("LOCALAPPDATA")
    if la:
        candidates.append(Path(la) / "opencode/opencode.db")
    ad = os.environ.get("APPDATA")
    if ad:
        candidates.append(Path(ad) / "opencode/opencode.db")
    for c in candidates:
        if c.is_file():
            return c
    raise SystemExit("FAIL: opencode.db not found. Tried:\n  " + "\n  ".join(map(str, candidates)))


def iso(ms: int | None) -> str:
    if not ms:
        return "?"
    try:
        return datetime.fromtimestamp(ms / 1000, tz=timezone.utc).astimezone().strftime("%Y-%m-%d %H:%M:%S")
    except (OverflowError, OSError, ValueError):
        return str(ms)


def hhmmss(ms: int | None) -> str:
    if not ms:
        return "??:??:??"
    try:
        return datetime.fromtimestamp(ms / 1000, tz=timezone.utc).astimezone().strftime("%H:%M:%S")
    except (OverflowError, OSError, ValueError):
        return "?"


def jload(raw: str | None) -> dict:
    if not raw:
        return {}
    try:
        obj = json.loads(raw)
        return obj if isinstance(obj, dict) else {}
    except (json.JSONDecodeError, TypeError):
        return {}


def clip(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + f"\n… [truncated {len(text) - limit} chars]"


def summarize_input(state_input: dict, limit: int) -> str:
    """Compact tool input: short JSON, clipped."""
    try:
        s = json.dumps(state_input, ensure_ascii=False, default=str)
    except (TypeError, ValueError):
        s = str(state_input)
    return clip(s, limit)


def render_parts(parts: list[dict], out: list[str], max_chars: int) -> None:
    for p in parts:
        ptype = p.get("type", "?")
        if ptype == "text":
            text = (p.get("text") or "").strip()
            if text:
                out.append(clip(text, max_chars))
                out.append("")
        elif ptype == "reasoning":
            text = (p.get("text") or "").strip()
            if text:
                out.append("> **reasoning:** " + clip(text.replace("\n", "\n> "), max_chars))
                out.append("")
        elif ptype == "tool":
            tool = p.get("tool", "?")
            st = p.get("state") or {}
            status = st.get("status", "?")
            inp = summarize_input(st.get("input") or {}, DEFAULT_TOOL_CHARS)
            out.append(f"- **tool `{tool}`** [{status}] input: `{inp}`")
            output = st.get("output")
            if isinstance(output, str) and output.strip():
                out.append("  ```")
                for line in clip(output.strip(), DEFAULT_TOOL_CHARS).splitlines():
                    out.append("  " + line)
                out.append("  ```")
            elif isinstance(output, dict):
                out.append("  ```json")
                out.append("  " + clip(json.dumps(output, ensure_ascii=False, default=str), DEFAULT_TOOL_CHARS))
                out.append("  ```")
        elif ptype in ("step-start", "step-finish"):
            continue  # noise
        else:
            # file/snapshot/agent/subtask parts etc.
            keys = ", ".join(sorted(p.keys()))
            out.append(f"- *[{ptype}]* ({keys})")


def dump_session(con: sqlite3.Connection, sess: dict, max_chars: int) -> str:
    sid = sess["id"]
    lines: list[str] = []
    title = (sess.get("title") or "(untitled)").replace("\n", " ")
    lines.append(f"# {title}")
    lines.append("")
    lines.append(f"- **session id**: `{sid}`")
    if sess.get("parent_id"):
        lines.append(f"- **parent session**: `{sess['parent_id']}` (subagent)")
    if sess.get("agent"):
        lines.append(f"- **agent**: {sess['agent']}")
    sess_model = sess.get("model")
    if isinstance(sess_model, str) and sess_model.startswith("{"):
        mj = jload(sess_model)
        sess_model = mj.get("id") or mj.get("modelID") or sess_model
    if sess_model:
        lines.append(f"- **model**: {sess_model}")
    if sess.get("directory"):
        lines.append(f"- **directory**: `{sess['directory']}`")
    lines.append(f"- **created**: {iso(sess.get('time_created'))}")
    lines.append(f"- **updated**: {iso(sess.get('time_updated'))}")
    if sess.get("tokens_input") or sess.get("tokens_output"):
        lines.append(
            f"- **tokens**: in={sess.get('tokens_input', 0)} out={sess.get('tokens_output', 0)} "
            f"cache_read={sess.get('tokens_cache_read', 0)} cost={sess.get('cost', 0):.4f}"
        )
    lines.append("")
    lines.append("## Messages")
    lines.append("")

    msgs = con.execute(
        "SELECT id, time_created, data FROM message WHERE session_id=? ORDER BY time_created, id",
        (sid,),
    ).fetchall()
    parts_by_msg: dict[str, list[dict]] = {}
    for pid, mid, _, _, pdata in con.execute(
        "SELECT id, message_id, time_created, time_updated, data FROM part "
        "WHERE session_id=? ORDER BY time_created, id",
        (sid,),
    ).fetchall():
        parts_by_msg.setdefault(mid, []).append(jload(pdata))

    if not msgs:
        lines.append("*(no messages)*")
    for mid, tcreate, mdata in msgs:
        info = jload(mdata)
        role = info.get("role", "?")
        model = info.get("modelID")
        if not model:
            m = info.get("model")
            if isinstance(m, dict):
                model = m.get("modelID") or m.get("id")
            elif isinstance(m, str):
                model = m
        agent = info.get("agent")
        head = f"### [{hhmmss(tcreate)}] {role}"
        meta = []
        if agent:
            meta.append(f"agent={agent}")
        if model and role == "assistant":
            meta.append(f"model={model}")
        if meta:
            head += " (" + ", ".join(meta) + ")"
        lines.append(head)
        render_parts(parts_by_msg.get(mid, []), lines, max_chars)
    lines.append("")
    lines.append("---")
    lines.append(f"*dumped {iso(int(datetime.now().timestamp() * 1000))} by tools/session_dump.py*")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sel = ap.add_mutually_exclusive_group(required=True)
    sel.add_argument("--latest", type=int, metavar="N", help="dump N most recently updated sessions")
    sel.add_argument("--session", metavar="ID", help="dump one session by id")
    sel.add_argument("--all", action="store_true", help="dump every session")
    ap.add_argument("--max-chars", type=int, default=DEFAULT_TEXT_CHARS, help="clip text/reasoning parts to N chars")
    ap.add_argument("--out", default=str(OUT_DIR), help="output directory (default .serena/agent-reports/sessions)")
    args = ap.parse_args()

    db = find_db()
    print(f"store: {db} ({db.stat().st_size / 1024**2:.1f} MB)")
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    con = sqlite3.connect(rf"file:{db.as_posix()}?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    try:
        if args.session:
            rows = con.execute("SELECT * FROM session WHERE id=?", (args.session,)).fetchall()
            if not rows:
                print(f"FAIL: session {args.session!r} not found", file=sys.stderr)
                return 1
        elif args.latest:
            rows = con.execute(
                "SELECT * FROM session ORDER BY time_updated DESC LIMIT ?", (args.latest,)
            ).fetchall()
        else:
            rows = con.execute("SELECT * FROM session ORDER BY time_updated DESC").fetchall()

        print(f"exporting {len(rows)} session(s) -> {out_dir}/")
        ok = 0
        for row in rows:
            sess = dict(row)
            sid = sess["id"]
            try:
                md = dump_session(con, sess, args.max_chars)
                (out_dir / f"{sid}.md").write_text(md, encoding="utf-8")
                ok += 1
                print(f"  OK  {sid}  {md.splitlines()[0][:70]}")
            except Exception as e:  # noqa: BLE001 — keep going on single-session errors
                print(f"  ERR {sid}: {e}", file=sys.stderr)
        print(f"done: {ok}/{len(rows)} written (idempotent overwrite)")
        return 0 if ok == len(rows) else 2
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
