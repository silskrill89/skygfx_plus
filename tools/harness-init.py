#!/usr/bin/env python3
"""harness-init.py — bootstrap a NEW project with the skygfx_plus_expIV memory+tooling harness.

Ported per user directive (2026-11-08): "look at skygfx plus repo and how the memory
harness works — we want that to be the default set up for projects".

What it does (per .serena/memories portability spec, section B2):
  1. Copy generic, self-locating tooling from SOURCE to <project_root>/tools/ and .sadie/.
  2. Rewrite the hardcoded-path matrix in the COPIES using CLI args (vaults, wiki, game dir).
  3. Seed skeleton layers: .serena/memories/ stubs, .opencode/memory-bank/ 6 files,
     .opencode/agent-logs/, .mimocode/{skills,agents,workflows,plans}, .harness-memory/.
  4. Optional: wire the fsx + obsidian MCP entries into <project_root>/opencode.json.
  5. Run: python .sadie/sadie.py init, sadie_vault_sync.py (if vault configured),
     npx harness-memory init (optional), smoke checks (memory-startup --check).

Usage:
  python harness-init.py --project-root D:/work/newproj \
      --vault-re "E:/dev(dave)/GTA SA Reverse Engineering Documentation" \
      [--vault-skygfx E:/dev(dave)/SKYGFXPLUS_DOCS] [--wiki H:/wikis/gtamods/rendering]
      [--mcp] [--no-swarm] [--dry-run]
"""
import argparse
import io
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# cp1252 console safety: force UTF-8 stdout/stderr
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

THIS_DIR = Path(__file__).resolve().parent            # .../tools
SRC = THIS_DIR.parent                                  # expIV project root

TOOLS_COPY = [
    "memory-startup.py", "memory.py", "memory_octree.py", "knowledge.py",
    "skytool.py", "research.py", "session_dump.py", "agent_coordinator.py",
    "fsx.py", "_toolutil.py",
    "fast_build.py", "fix_build.py", "layering_check.py",
    "pack_helper_tex.py", "gen_ini_ref.py", "release_packager.py",
    "assets.py", "proto.py", "shot_sync.py", "proflog.py",
]
SADIE_COPY = [
    "sadie.py", "sadie_config.py", "sadie_extract.py", "sadie_vault_sync.py",
    "verify.py", "README.md", "quick-ref.md", "sadie-search-skill.md",
]
SADIE_DIRS = ["checks", "commands", "workflows"]
MIMOCODE_DIRS = ["skills", "agents", "workflows"]
SWARM_COPY = ["SWARM.md", "dispatch-checklist.md", "roles.json", "bootstrap.py"]

MEMORY_BANK_FILES = [
    "activeContext.md", "history.md", "productContext.md",
    "progress.md", "systemPatterns.md", "techContext.md",
]
SERENA_STUBS = ["project.md", "build-process.md", "debugging.md", "vault-refs.md",
                "agent-tiers.md", "session-rules.md", "project-status.md"]


def banner(msg: str) -> None:
    print(f"== {msg}")


def rewrite_text(text: str, args) -> tuple[str, int]:
    """Substitute hardcoded E:/dev(dave) paths in copied files."""
    hits = 0

    def sub(pattern: str, repl: str) -> None:
        nonlocal hits, text
        text, n = re.subn(pattern, lambda m: repl, text)
        hits += n

    if args.vault_re:
        v = args.vault_re.replace("\\", "/")
        sub(r'E:(/|\\\\)dev\(dave\)(/|\\\\)GTA SA Reverse Engineering Documentation', v)
        sub(r'E:/dev\(dave\)/GTA SA Reverse Engineering Documentation', v)
    if args.vault_skygfx:
        v = args.vault_skygfx.replace("\\", "/")
        sub(r'E:(/|\\\\)dev\(dave\)(/|\\\\)SKYGFXPLUS_DOCS', v)
        sub(r'E:/dev\(dave\)/SKYGFXPLUS_DOCS', v)
    if args.wiki:
        sub(r'H:/wikis/gtamods/rendering', args.wiki.replace("\\", "/"))
    return text, hits


def copy_and_rewrite(src: Path, dst: Path, args) -> int:
    dst.parent.mkdir(parents=True, exist_ok=True)
    data = src.read_text(encoding="utf-8", errors="replace")
    data, hits = rewrite_text(data, args)
    dst.write_text(data, encoding="utf-8", newline="\n")
    return hits


def seed(path: Path, body: str) -> None:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project-root", required=True)
    ap.add_argument("--vault-re", default=None)
    ap.add_argument("--vault-skygfx", default=None)
    ap.add_argument("--wiki", default=None)
    ap.add_argument("--game-dir", default=None, help="game install dir (else left env-driven)")
    ap.add_argument("--mcp", action="store_true", help="wire fsx+obsidian entries into <root>/opencode.json")
    ap.add_argument("--no-swarm", action="store_true")
    ap.add_argument("--no-harness-memory", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = Path(args.project_root).resolve()
    if not root.exists():
        print(f"ERROR: project root does not exist: {root}")
        return 2
    banner(f"harness-init → {root}  (dry-run={args.dry_run})")

    # 1) generic tooling ------------------------------------------------
    n_hits = 0
    for name in TOOLS_COPY:
        src = SRC / "tools" / name
        if not src.exists():
            print(f"  skip (missing source): tools/{name}")
            continue
        dst = root / "tools" / name
        if not args.dry_run:
            n_hits += copy_and_rewrite(src, dst, args)
        banner_if = "  copy tools/" + name
        print(banner_if)
    for name in SADIE_COPY:
        src = SRC / ".sadie" / name
        if not src.exists():
            continue
        if not args.dry_run:
            n_hits += copy_and_rewrite(src, root / ".sadie" / name, args)
        print(f"  copy .sadie/{name}")
    for d in SADIE_DIRS:
        sdir = SRC / ".sadie" / d
        if not sdir.exists():
            continue
        for f in sdir.iterdir():
            if f.is_file():
                if not args.dry_run:
                    n_hits += copy_and_rewrite(f, root / ".sadie" / d / f.name, args)
                print(f"  copy .sadie/{d}/{f.name}")
    if not args.no_swarm:
        for name in SWARM_COPY:
            src = SRC / "tools" / "swarm" / name
            if not src.exists():
                continue
            if not args.dry_run:
                n_hits += copy_and_rewrite(src, root / "tools" / "swarm" / name, args)
            print(f"  copy tools/swarm/{name}")
    for d in MIMOCODE_DIRS:
        sdir = SRC / ".mimocode" / d
        if not sdir.exists():
            continue
        for f in sorted(sdir.glob("*.md")):
            if not args.dry_run:
                n_hits += copy_and_rewrite(f, root / ".mimocode" / d / f.name, args)
            print(f"  copy .mimocode/{d}/{f.name}")
    wf = SRC / ".mimocode" / "workflows"
    if wf.exists():
        for f in sorted(wf.glob("*.js")):
            if not args.dry_run:
                n_hits += copy_and_rewrite(f, root / ".mimocode" / "workflows" / f.name, args)
            print(f"  copy .mimocode/workflows/{f.name}")
    print(f"== path rewrites applied in copies: {n_hits}")

    # 2) skeleton layers --------------------------------------------------
    if not args.dry_run:
        for name in MEMORY_BANK_FILES:
            seed(root / ".opencode" / "memory-bank" / name,
                 f"# {name}\n\n(RE-AUTHOR for this project — seeded by harness-init {__file__})\n")
        (root / ".opencode" / "agent-logs").mkdir(parents=True, exist_ok=True)
        for name in SERENA_STUBS:
            seed(root / ".serena" / "memories" / name,
                 f"# {name}\n\n(RE-AUTHOR for this project — seeded by harness-init)\n")
        for d in ("docs/plans", ".harness-memory", ".mimocode/plans",
                  ".opencode/commands", ".sadie"):
            (root / d).mkdir(parents=True, exist_ok=True)
        print("== skeleton layers seeded (serena stubs, memory-bank, agent-logs, docs/plans)")

    # 3) optional MCP wiring ---------------------------------------------
    if args.mcp and not args.dry_run:
        oc = root / "opencode.json"
        cfg = json.loads(oc.read_text(encoding="utf-8")) if oc.exists() else {}
        cfg.setdefault("mcp", {})
        cfg["mcp"]["fsx"] = {"type": "local",
                             "command": ["python", str(root / "tools" / "fsx.py"), "--serve"]}
        if args.vault_skygfx or args.vault_re:
            wrapper = root / "tools" / "obsidian-mcp-wrapper.ps1"
            vault = args.vault_skygfx or args.vault_re
            wrapper.write_text(
                "$env:OBSIDIAN_API_KEY = 'REPLACE-KEY'\n"
                f"$env:OBSIDIAN_BASE_URL = 'https://127.0.0.1:27124'\n"
                "$env:OBSIDIAN_VERIFY_SSL = 'false'\n"
                f"$env:OBSIDIAN_VAULT_PATH = '{vault}'\n"
                "& 'C:\\Users\\aaaaaaaaa\\.bun\\bin\\obsidian-mcp-server.exe' @args\n",
                encoding="utf-8")
            cfg["mcp"]["obsidian-mcp"] = {"type": "local",
                "command": ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                            "-File", str(wrapper)]}
            print("== wrote obsidian-mcp-wrapper.ps1 (REPLACE-KEY with your Local REST API key)")
        oc.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
        print("== opencode.json mcp entries written")

    # 4) build index + smoke ----------------------------------------------
    if args.dry_run:
        print("== dry-run complete")
        return 0

    def run(cmd, cwd=root):
        print(f"$ {' '.join(str(c) for c in cmd)}")
        try:
            r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=300)
            print((r.stdout or "")[-1500:])
            if r.returncode != 0:
                print((r.stderr or "")[-1500:])
            return r.returncode
        except Exception as e:
            print(f"  LAUNCH FAIL: {e}")
            return 1

    rc_init = run([sys.executable, ".sadie/sadie.py", "init"])
    if args.vault_skygfx or args.vault_re:
        run([sys.executable, ".sadie/sadie_vault_sync.py"])
    if not args.no_harness_memory:
        run(["npx", "harness-memory", "init", "--db", ".harness-memory/memory.sqlite"])
    run([sys.executable, "tools/memory-startup.py", "--check"])

    banner("DONE — re-author .serena/memories + memory-bank for the new project, then:")
    print("   python .sadie/sadie.py init && python .sadie/sadie_vault_sync.py")
    return 0 if rc_init == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
