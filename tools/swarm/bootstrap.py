#!/usr/bin/env python3
"""Swarm task-start bootstrapper: assigns the 8 free bots with directives.

Usage:
    python tools/swarm/bootstrap.py [N] [--no-obsidian]

Reads (repo-relative, missing files skipped silently):
  TODO.md, tools/swarm/dispatch-checklist.md, tools/swarm/roles.json,
  tools/swarm/SWARM.md, .serena/memories/*.md, .opencode/memory-bank/*.md
Plus Obsidian Local REST API (self-signed cert -> unverified SSL REQUIRED)
with the Bearer key discovered at runtime (never printed, never stored).

Prints: memory digest + N ready-to-send @mention blocks (default 8),
2 per wave (credit-concurrency limit). Branch rule: very-experimental.
Golden rule: no normal-map `_n` implementation, ever.
Stdlib only. Target runtime <30s.
"""
import json
import os
import re
import ssl
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
MIMCODE = r"C:\Users\aaaaaaaaa\.config\opencode\mimocode.jsonc"
OBSIDIAN_BASE = "https://127.0.0.1:27124"
BRANCH = "very-experimental"

# Priority queue (keep in sync with tools/swarm/dispatch-checklist.md).
# bot = free roster tag (chat @mention). type = task() built-in for reference.
QUEUE = [
    {"prio": "P0", "item": "RenderEnvTex entry guard + envmap raster-NULL check (2 fixes) + build",
     "bot": "crash-triage-free", "type": "fixer", "access": "write (this lane only)",
     "mission": ("In E:\\dev(dave)\\skygfx_plus_expIV finish the guard-fix job (0x7FAD4D class). "
                 "Edit ONLY src/render/neoCarpipe.cpp + src/render/envmap.cpp (tabs). "
                 "FIX1: RenderEnvTex entry add `if(!Scene.camera || !reflectionCam) return;` first "
                 "(confirm void return). FIX2: envmap.cpp:485-490 raster swap — read block, NULL-check "
                 "raster locals pre-swap using EXACT local names. Guards only. Then "
                 "python tools/fast_build.py, report 0 errors + ASI bytes. Do NOT commit."),
     "contract": "lines changed + build result"},
    {"prio": "P0", "item": "VehiclePBR_Modern 7 entries SM3.0 + register layout",
     "bot": "mimo-free-free2", "type": "explorer", "access": "read-only",
     "mission": ("READ-ONLY audit (swarm-007). Verify all 7 entries in "
                 "shaders/ps/VehiclePBR_Modern.hlsl (main, main_specCarFx, main_mobileVehicle, "
                 "main_building, main_rubber, main_normMapVehicle, main_ps2EnvSpecFx) compile ps_3_0 "
                 "and match PS layout c0-c24/s0-s3 (c22+c23 via pipeUploadPBR ONLY)."),
     "contract": "table entry|registers|PASS/FAIL|evidence"},
    {"prio": "P2", "item": "ARCH-4 static-locals re-entrancy audit",
     "bot": "ling-free-free2", "type": "oracle", "access": "read-only",
     "mission": ("READ-ONLY audit (swarm-005). Find static locals in render callbacks "
                 "(postfx.cpp, envmap.cpp, vehiclePipe.cpp, buildingPipe.cpp, neoCarpipe.cpp, "
                 "depthhook.cpp); flag mutation-without-reset, counter-never-reset, "
                 "pointer-cached-across-Reset risks."),
     "contract": "table symbol|file:line|risk|bounded-fix"},
    {"prio": "P1", "item": "SSAO temporal chain verify post-relocation",
     "bot": "shader-ssao-free", "type": "explorer", "access": "read-only",
     "mission": ("READ-ONLY verify: CopyDepthToPrev() called AFTER DrawSSAO_Overhaul in "
                 "ColourFilter_switch (~:2028-2030); s5/s6 uploads (~:3026-3027) + c4History (~:3061) "
                 "match SSAO_Temporal.hlsl (s5, s6, c4 histFlags); velocity pass samples pre-copy depth."),
     "contract": "PASS/FAIL per item + file:line evidence"},
    {"prio": "P2", "item": "ARCH-3 render-state save/restore site gather",
     "bot": "rw-state-free", "type": "explorer", "access": "read-only",
     "mission": ("READ-ONLY gather (swarm-004 minor): every D3D9 state set in postfx.cpp effects "
                 "(SSAO, SMAA, MotionBlur, Velocity, NormalBuffer, PipeChain, HeightFog) with "
                 "file:line + restored-before-exit?"),
     "contract": "table state|effect|file:line|restored?|notes"},
    {"prio": "P1", "item": "Build + ASI state confirm",
     "bot": "verify-build-free", "type": "explorer", "access": "read-only",
     "mission": ("READ-ONLY: run nothing. Read build log/premake_log.txt if present, confirm "
                 "E:\\games\\gtasa_skygfx_plus\\skygfx.asi exists with size+timestamp, report."),
     "contract": "bytes + timestamp + verdict"},
    {"prio": "P3", "item": "Commit finished work to very-experimental",
     "bot": "commit-bot-free", "type": "fixer", "access": "write (git only)",
     "mission": ("COMMIT-ONLY on branch very-experimental. git status/diff; stage ONLY finished "
                 "source+doc+harness files; NEVER stage skygfx_plus_expIV.7z or build/ output; "
                 "commit with descriptive message; report hash + remaining tree."),
     "contract": "hash + remaining-tree status"},
    {"prio": "P3", "item": "Doc hygiene for latest fixes",
     "bot": "docs-sync-free", "type": "explorer", "access": "read-only",
     "mission": ("READ-ONLY: diff latest commits vs TODO.md/WORKFLOW_PLAN.md checkboxes; list which "
                 "checkboxes should tick + stale line refs. Propose exact edits, do NOT apply."),
     "contract": "table file|line|change|evidence"},
]


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def strip_jsonc(text):
    """String-aware comment stripper (never breaks https:// in strings)."""
    out, i, n, s = [], 0, len(text), None
    while i < n:
        c = text[i]
        if s:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == s:
                s = None
        elif c in "\"'":
            s, out = c, out + [c]
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def find_obsidian_key():
    """Discover Bearer key near an obsidian reference; return (key|None, source)."""
    env = os.environ.get("OBSIDIAN_API_KEY")
    if env:
        return env.strip(), "env OBSIDIAN_API_KEY"
    raw = read_text(MIMCODE)
    if not raw:
        return None, "mimocode.jsonc unreadable"
    for m in re.finditer(
            r'"((?:apiKey|api_key|api-key|token|authorization))"\s*:\s*"([^"]+)"',
            raw, re.IGNORECASE):
        start = max(0, m.start() - 300)
        if "obsidian" in raw[start:m.start()].lower():
            return m.group(2), "mimocode.jsonc (obsidian-adjacent)"
    return None, "no obsidian-adjacent key in mimocode.jsonc"


def obsidian_call(key, method, path, payload=None):
    ctx = ssl._create_unverified_context()  # plugin uses self-signed cert
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(
        OBSIDIAN_BASE + path, data=data, method=method,
        headers={"Authorization": "Bearer " + key,
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=6, context=ctx) as r:
        body = r.read().decode("utf-8", errors="replace")
    try:
        return json.loads(body)
    except ValueError:
        return {"_raw": body[:200]}


def obsidian_status(skip):
    if skip:
        return "skipped (--no-obsidian)"
    key, src = find_obsidian_key()
    if not key:
        return "unreachable (%s)" % src
    try:
        obsidian_call(key, "GET", "/")
        res = obsidian_call(key, "POST", "/search/simple",
                            {"query": "skygfx crash"})
        hits = res if isinstance(res, list) else res.get("results", res)
        n = len(hits) if isinstance(hits, list) else "?"
        return "reachable (%s, key via %s; 'skygfx crash' hits: %s)" % (
            OBSIDIAN_BASE, src, n)
    except Exception as e:  # noqa: BLE001 - degrade, never crash
        return "unreachable (%s: %s)" % (src, e)


def layer_digest(root_dir, pattern=None):
    try:
        names = sorted(os.listdir(root_dir))
    except OSError:
        return 0, 0, []
    if pattern:
        names = [x for x in names if x.endswith(pattern)]
    total = 0
    for x in names:
        try:
            total += os.path.getsize(os.path.join(root_dir, x))
        except OSError:
            pass
    return len(names), total, names


def main(argv):
    n = 8
    skip_obs = False
    for a in argv[1:]:
        if a == "--no-obsidian":
            skip_obs = True
        elif a.isdigit():
            n = max(1, min(int(a), len(QUEUE)))
    for p in ["TODO.md", "tools/swarm/dispatch-checklist.md",
              "tools/swarm/roles.json", "tools/swarm/SWARM.md"]:
        read_text(os.path.join(REPO, p))  # task-state warm read
    sn, sb, snames = layer_digest(os.path.join(REPO, ".serena", "memories"), ".md")
    mn, mb, mnames = layer_digest(os.path.join(REPO, ".opencode", "memory-bank"), ".md")
    obs = obsidian_status(skip_obs)
    print("=== SWARM BOOTSTRAP ===")
    print("branch: %s | memory: serena=%d files (%dB) bank=%d files (%dB) | obsidian: %s"
          % (BRANCH, sn, sb, mn, mb, obs))
    if snames:
        print("serena: %s" % ", ".join(snames[:8]))
    if mnames:
        print("bank: %s" % ", ".join(mnames[:8]))
    print("golden rule: NO normal-map `_n` implementation. BLOCKED: TODO.md C, in-game verify.")
    for i, q in enumerate(QUEUE[:n]):
        print("\n--- WAVE %d | %s %s -> @%s [%s, %s] ---"
              % (i // 2 + 1, q["prio"], q["item"], q["bot"], q["type"], q["access"]))
        print("@%s %s Return: %s. Branch %s. Do NOT touch normal-map _n."
              % (q["bot"], q["mission"], q["contract"], BRANCH))
    print("\n=== END (%d blocks) ===" % min(n, len(QUEUE)))


if __name__ == "__main__":
    main(sys.argv)
