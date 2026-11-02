#!/usr/bin/env python3
"""Sync the newest 20 gta_sa_* screenshots into repo screenshots/ + update README image section.

Usage: python tools/shot_sync.py
Sources: C:/Users/aaaaaaaaa/Documents/ShareX/Screenshots/2026-11 (fallback 2026-10, then 2026-12)
Only files whose name contains 'gta_sa' are considered (ShareX random-suffix names).
"""
import os, re, shutil, glob

DEST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "screenshots")
SRC_DIRS = [
    r"C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-11",
    r"C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10",
    r"C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-12",
]
COUNT = 20

def main():
    os.makedirs(DEST, exist_ok=True)
    shots = []
    for d in SRC_DIRS:
        for f in glob.glob(os.path.join(d, "*")):
            if os.path.isfile(f) and "gta_sa" in os.path.basename(f):
                shots.append((os.path.getmtime(f), f))
    shots.sort(reverse=True)
    picked = shots[:COUNT]
    copied = []
    for mtime, src in picked:
        dst = os.path.join(DEST, os.path.basename(src))
        if not os.path.exists(dst) or os.path.getmtime(dst) != mtime:
            shutil.copy2(src, dst)
        copied.append(os.path.basename(src))
    print(f"synced {len(copied)} shots -> {DEST}")
    for name in copied:
        print("  " + name)
    # Update README image section if present
    readme = os.path.join(os.path.dirname(DEST), "readme.md")
    if os.path.exists(readme):
        with open(readme, "r", encoding="utf-8", errors="replace") as fh:
            txt = fh.read()
        block = "## Screenshots\n\nLatest test captures (auto-synced by tools/shot_sync.py):\n\n" + "\n".join(
            f"![{os.path.splitext(n)[0]}](screenshots/{n})" for n in copied
        ) + "\n"
        new_txt, n = re.subn(r"## Screenshots\n.*?(?=\n## |\Z)", block + "\n", txt, count=1, flags=re.S)
        if n == 0:
            new_txt = txt.rstrip() + "\n\n" + block
        with open(readme, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(new_txt)
        print("readme.md screenshots section updated")

if __name__ == "__main__":
    main()
