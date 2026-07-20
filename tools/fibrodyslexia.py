"""
fibrodyslexia — Garbled text decoder for GTA SA modding.

Handles common encoding mismatches:
  - UTF-8 bytes misread as Latin-1 / CP1252
  - Double-encoded UTF-8
  - Shift-JIS / EUC-KR / GB2312 garble from Asian DFF/TXD tools
  - NULL-byte interleaving (UTF-16LE misread as ASCII)

Usage as module:
    from fibrodyslexia import decode, fix, guess_encoding

Usage as CLI:
    python fibrodyslexia.py "garbled text here"
    python fibrodyslexia.py --file corrupted.txt
    python fibrodyslexia.py --hex "C3 A9"          # raw hex bytes
    python fibrodyslexia.py --batch garbled_list.txt
"""

from __future__ import annotations

import codecs
import re
import sys
from typing import Optional


# ---------------------------------------------------------------------------
# Encoding tables
# ---------------------------------------------------------------------------

# Common double-encoding paths: UTF-8 → misread as $ENCODING → encode as UTF-8
_DOUBLE_ENCODING_PATHS = ["latin-1", "cp1252", "iso-8859-1", "iso-8859-15"]

# Asian encodings that frequently garble DFF/TXD tool text
_ASIAN_ENCODINGS = ["shift_jis", "euc-kr", "gb2312", "gbk", "big5", "euc-jp"]

# All candidate encodings to try
_ALL_ENCODINGS = _DOUBLE_ENCODING_PATHS + _ASIAN_ENCODINGS + ["utf-8", "ascii"]


# ---------------------------------------------------------------------------
# Core detection & repair
# ---------------------------------------------------------------------------

def is_mojibake(text: str) -> bool:
    """Heuristic: does this text look like UTF-8 misinterpreted as Latin-1?

    Signs:
      - Lots of characters in \u0080-\u00FF range (Latin-1 supplement)
      - Sequences that form valid UTF-8 when re-encoded as Latin-1
      - Common mojibake patterns: â€™ (U+2019), Ã© (U+00E9 → é), â€" (U+2014 → —)
    """
    if not text:
        return False

    # Quick signal: common mojibake characters
    mojibake_signs = re.compile(r'[ÃÂ«»¯¨´¸ÆŒæœŠšŽž€™""–—\u00C0-\u00FF]{2,}')
    if mojibake_signs.search(text):
        return True

    # Test: re-encode as latin-1, then decode as UTF-8
    try:
        roundtrip = text.encode("latin-1").decode("utf-8")
        # If roundtrip produces different text AND contains valid unicode, it's mojibake
        if roundtrip != text and any(ord(c) > 127 for c in roundtrip):
            return True
    except (UnicodeDecodeError, UnicodeEncodeError):
        pass

    return False


def fix_latin1_to_utf8(text: str) -> Optional[str]:
    """Fix UTF-8 bytes misread as Latin-1."""
    try:
        fixed = text.encode("latin-1").decode("utf-8")
        return fixed if fixed != text else None
    except (UnicodeDecodeError, UnicodeEncodeError):
        return None


def fix_cp1252_to_utf8(text: str) -> Optional[str]:
    """Fix UTF-8 bytes misread as Windows-1252."""
    try:
        fixed = text.encode("cp1252").decode("utf-8")
        return fixed if fixed != text else None
    except (UnicodeDecodeError, UnicodeEncodeError):
        return None


def fix_double_utf8(text: str) -> Optional[str]:
    """Fix double-encoded UTF-8 (UTF-8 → UTF-8 again)."""
    try:
        fixed = text.encode("utf-8").decode("utf-8")
        # Double-encoding means the text is already valid UTF-8 but represents
        # the wrong characters. Try encoding as latin-1 first.
        fixed2 = text.encode("latin-1").decode("utf-8").encode("latin-1").decode("utf-8")
        return fixed2 if fixed2 != text else None
    except (UnicodeDecodeError, UnicodeEncodeError):
        return None


def fix_utf16_leak(text: str) -> Optional[str]:
    """Fix UTF-16LE bytes where null bytes were stripped (common in hex editors)."""
    # Check for interleaved null pattern: 'H\x00e\x00l\x00l\x00o\x00'
    if "\x00" in text:
        stripped = text.replace("\x00", "")
        if stripped != text and len(stripped) > 0:
            return stripped
    return None


def fix_asian_garble(text: str) -> Optional[str]:
    """Try common Asian encodings that garble when decoded as ASCII/Latin-1."""
    raw = text.encode("latin-1", errors="replace")
    for enc in _ASIAN_ENCODINGS:
        try:
            fixed = raw.decode(enc)
            if fixed and fixed != text and all(c.isprintable() or c in "\n\r\t" for c in fixed):
                return fixed
        except (UnicodeDecodeError, UnicodeEncodeError):
            continue
    return None


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def decode(text: str) -> dict:
    """Attempt all decodings, return candidates sorted by confidence.

    Returns dict: { encoding_name: decoded_text, ... }
    Only includes successful decodings that differ from input.
    """
    if not text:
        return {}

    candidates = {}

    # Latin-1 / CP1252 round-trip
    for fixer_name, fixer in [
        ("latin1→utf8", fix_latin1_to_utf8),
        ("cp1252→utf8", fix_cp1252_to_utf8),
        ("double_utf8", fix_double_utf8),
        ("utf16_leak", fix_utf16_leak),
    ]:
        result = fixer(text)
        if result:
            candidates[fixer_name] = result

    # Asian encodings
    asian = fix_asian_garble(text)
    if asian:
        candidates["asian_garble"] = asian

    return candidates


def fix(text: str) -> str:
    """Auto-fix garbled text. Returns best guess, or original if unfixable."""
    if not text:
        return text

    # Already clean UTF-8?
    try:
        text.encode("utf-8")
    except UnicodeEncodeError:
        pass

    # Try each fixer, prefer the one that produces the most printable ASCII
    candidates = decode(text)
    if not candidates:
        return text

    def score(s: str) -> float:
        """Score: ratio of common printable chars."""
        if not s:
            return 0.0
        printable = sum(1 for c in s if c.isprintable() or c in "\n\r\t")
        return printable / len(s)

    best = max(candidates.values(), key=score)
    return best if score(best) > score(text) else text


def guess_encoding(text: str) -> Optional[str]:
    """Guess the original encoding that was misused."""
    candidates = decode(text)
    if not candidates:
        return None
    return max(candidates.keys(), key=lambda k: len(candidates[k]))


# ---------------------------------------------------------------------------
# Batch processing
# ---------------------------------------------------------------------------

def fix_file(path: str, encoding: str = "utf-8") -> list[dict]:
    """Read a file line-by-line, attempt to fix each line.

    Returns list of { line_num, original, fixed, changed } dicts.
    """
    results = []
    with open(path, "r", encoding=encoding, errors="replace") as f:
        for i, line in enumerate(f, 1):
            line = line.rstrip("\n\r")
            fixed = fix(line)
            results.append({
                "line_num": i,
                "original": line,
                "fixed": fixed,
                "changed": fixed != line,
            })
    return results


def fix_hex(hex_str: str) -> str:
    """Decode from hex string and attempt fix."""
    raw = bytes.fromhex(hex_str.replace(" ", "").replace("0x", ""))
    # Try common decodings
    for enc in ["utf-8", "latin-1", "cp1252", "shift_jis", "euc-kr", "gb2312"]:
        try:
            text = raw.decode(enc)
            fixed = fix(text)
            if fixed != text:
                return fixed
            return text
        except (UnicodeDecodeError, UnicodeEncodeError):
            continue
    return raw.decode("latin-1", errors="replace")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _cli():
    import argparse

    parser = argparse.ArgumentParser(
        description="fibrodyslexia — garbled text decoder for GTA SA modding"
    )
    parser.add_argument("text", nargs="?", help="Garbled text to fix")
    parser.add_argument("--file", "-f", help="File to process line-by-line")
    parser.add_argument("--hex", "-x", help="Hex string to decode (e.g. 'C3 A9')")
    parser.add_argument("--batch", "-b", help="File with one garbled string per line")
    parser.add_argument("--all", "-a", action="store_true", help="Show all candidates")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")

    args = parser.parse_args()

    if args.hex:
        result = fix_hex(args.hex)
        print(f"Hex decode: {result}")
        return

    if args.file:
        results = fix_file(args.file)
        changed = [r for r in results if r["changed"]]
        print(f"Processed {len(results)} lines, fixed {len(changed)}")
        for r in changed:
            print(f"  L{r['line_num']}: {r['original']!r} → {r['fixed']!r}")
        return

    if args.batch:
        with open(args.batch, "r", encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                fixed = fix(line)
                if fixed != line:
                    print(f"L{i}: {line!r} → {fixed!r}")
                elif args.verbose:
                    print(f"L{i}: {line!r} (clean)")
        return

    if args.text:
        text = args.text
    else:
        text = sys.stdin.read().strip()

    if args.all:
        candidates = decode(text)
        if candidates:
            print(f"Original:  {text!r}")
            for name, decoded in candidates.items():
                print(f"  {name}: {decoded!r}")
        else:
            print(f"No candidates for: {text!r}")
    else:
        result = fix(text)
        if result != text:
            print(f"Fixed: {result}")
        else:
            print(f"(clean) {text}")


if __name__ == "__main__":
    _cli()
