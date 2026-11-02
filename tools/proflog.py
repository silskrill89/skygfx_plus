#!/usr/bin/env python3
"""
proflog.py - skygfx_dbg.log frame/pass analyser (stdlib only).

Parses the skygfx debug log, rebuilds the per-frame PostFX pass chain from
log records (PERF timing lines, PostFX-REPORT per-frame dumps, [PostFX]
entry lines, [SMAA-DIAG] pass lines, [FBWP] framebuffer writes, [Tonemap]
lines, building pipe draw events, and generic PF/SCOPE/waterchain/bpipe
tagged markers), then reports:

  summary    - totals, config timeline, pass frequency
  chain      - per-frame pass ORDER, sampled every N frames
  anomalies  - chain changes vs previous frame, duplicates, gaps, missing
               passes, zero-size rasters, fault/guard/FAIL keywords,
               sceneLuma==0, non-screen RT during pass
  blackscan  - heuristic full-screen-black-frame signature detection

Usage:
  python tools/proflog.py [logfile] [--frames N] [--grep PAT] [--summary]
                          [--blackscan] [--every N] [--out FILE] [-q]

Default logfile: E:\\games\\gtasa_skygfx_plus\\skygfx_dbg.log
"""

import argparse
import os
import re
import sys
from collections import Counter, OrderedDict

DEFAULT_LOG = os.path.join(os.environ.get("SKYGFX_GAME") or r"E:\games\gtasa_skygfx_plus",
                            "skygfx_dbg.log")

# ---------------------------------------------------------------- line model
LINE_RE = re.compile(
    r"^\[(\d\d:\d\d:\d\d\.\d+)\]\s+\[(\w+)\s*\]\s*:\d+\s*\(\)\s*-\s*(.*)$"
)

REPORT_RE = re.compile(r"\[PostFX-REPORT\]\s+(.*)$")
PERF_RE = re.compile(r"^PERF \[([^\]]+)\]\s+([0-9.]+)\s*ms$")
KV_RE = re.compile(r"(\w+)=([^\s]+)")

SWITCH_RE = re.compile(r"\[PostFX\] ColourFilter_switch:\s+(.*)$")
CFMODERN_RE = re.compile(
    r"\[PostFX\] ColourFilter_Modern ENTER rgba1=\((\d+),(\d+),(\d+),(\d+)\) "
    r"rgba2=\((\d+),(\d+),(\d+),(\d+)\) pRFB=([0-9A-F]+) gradingPS=([0-9A-F]+) camRas=([0-9A-F]+)"
)
UPDATEFB_RE = re.compile(r"\[PostFX\] UpdateFrontBuffer pRasFB=(\S+) camRas=(\S+)")
HOOK_RE = re.compile(r"ColourFilter_switch (enter|done)")  # raw (no [PostFX] tag)
TONEMAP_TC_RE = re.compile(r"\[Tonemap\] TC sceneLuma=([0-9.]+)")
FBWP_RE = re.compile(r"\[FBWP\] WRITE to Scene\.camera->frameBuffer addr=(\S+) by EIP=(\S+)")
SMAA_PASS_RE = re.compile(r"\[SMAA-DIAG\] (Pre-PASS|PRE-PASS|Pass\d+):\s*(.*)$")
FB_SIZE_RE = re.compile(r"(\d+)x(\d+)")

# pass-name normalisation: distinct log lines -> ordered chain pass names
PASS_MAP = [
    (re.compile(r"^PERF \[(.*)\]"), "perf"),
    (re.compile(r"\[SMAA-DIAG\] PRE-PASS"), "smaa_prepass"),
    (re.compile(r"\[SMAA-DIAG\] Pass\d+"), "smaa_pass"),
    (re.compile(r"\[PostFX\] UpdateFrontBuffer"), "UpdateFrontBuffer"),
    (re.compile(r"\[PostFX\] ColourFilter_Modern ENTER"), "ColourFilter_Modern"),
    (re.compile(r"ColourFilter_switch: done"), "cf_switch_done"),
    (re.compile(r"ColourFilter_switch enter"), "cf_switch_enter"),
]

BAD_KEYWORDS = re.compile(
    r"\b(fault|guard|FAIL|failed|NaN|nan|-1\b|crash|assert|FATAL|panic)\b", re.I
)
# keyword substrings that are known-benign in this log (game opcodes,
# render-target readback probes used by postfxDumpDebug, etc.)
BENIGN_RE = re.compile(
    r"sa\.json|op=|TASK_|IS_PERFORMING|GetRenderTargetData|0x8876086C")
# generic tagged markers we still want to surface even in --summary
TAG_RE = re.compile(r"\b(PF|SCOPE|waterchain|bpipe)\b")

RASTER_NAME_HINTS = {
    "camRas": "camera",
    "pRasFB": "frontbuffer",
}

# Passes that the shader emits AFTER the PostFX-REPORT line are the TAIL of
# the *reported* frame (the report is printed mid-chain, right before the
# final grade/blit); everything else seen after a report is the HEAD of the
# next frame and must be held in the pending buffer.
TAIL_NAMES = frozenset((
    "PERF:IVGrade", "PERF:MotionBlur", "PERF:HeightFog", "PERF:GodRays",
    "PERF:FXAA3", "cf_switch_done",
))
PAREN_SIZE_RE = re.compile(r"\((\d+)x(\d+)\)")
HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")


class Frame(object):
    __slots__ = ("fid", "line", "ts", "report", "passes", "perf", "events",
                 "counts", "bld", "va", "keywords", "fbwrites", "zeros")

    def __init__(self, fid, line, ts):
        self.fid = fid
        self.line = line
        self.ts = ts
        self.report = None
        self.passes = []      # ordered (passname, line, detail)
        self.perf = {}        # passname -> [ms, ...]
        self.events = []      # misc notable events
        self.counts = Counter()
        self.bld = 0          # building-pipe draw events
        self.va = 0           # building-pipe vanishing-test events
        self.keywords = []    # (line, text)
        self.fbwrites = []    # FBWP events
        self.zeros = []       # zero-size raster hits

    # -- derived ----------------------------------------------------------
    def pass_names(self):
        return [p[0] for p in self.passes]

    def has(self, name):
        return name in set(self.pass_names())


def parse(path, verbose_skips=True):
    frames = []
    pre = []  # records before first PostFX-REPORT
    current = None
    cur_bucket = pre  # where to attach non-frame lines
    stats = Counter()
    timeline = []  # (fid, line, ts, {config fields})
    lines_total = 0
    first_ts = last_ts = None
    keyword_hits = []
    pending = []  # head passes of the frame currently being reported
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            lines_total += 1
            line = raw.rstrip("\n")
            m = LINE_RE.match(line)
            if not m:
                stats["unparsed"] += 1
                continue
            ts, level, msg = m.group(1), m.group(2), m.group(3)
            if first_ts is None:
                first_ts = ts
            last_ts = ts
            body = msg

            # --- new frame boundary -------------------------------------
            rm = REPORT_RE.search(body)
            if rm:
                kv = dict(KV_RE.findall(rm.group(1)))
                try:
                    fid = int(kv.get("frame", "-1"))
                except ValueError:
                    fid = -1
                current = Frame(fid, lineno, ts)
                current.report = kv
                frames.append(current)
                timeline.append((fid, lineno, ts, kv))
                stats["reports"] += 1
                # flush pending (head passes of the new frame) in order
                for pname, plineno, pdetail in pending:
                    current.passes.append((pname, plineno, pdetail))
                    current.counts[pname] += 1
                del pending[:]
                # zero-size rasters straight off the report: pRasFB/camRas
                # are logged as HEX(WxH); only the parenthesised size counts
                for key in ("pRasFB", "camRas"):
                    val = kv.get(key, "")
                    pm2 = PAREN_SIZE_RE.search(val)
                    if pm2:
                        w, h = int(pm2.group(1)), int(pm2.group(2))
                        if w == 0 or h == 0:
                            current.zeros.append((lineno, "%s=(%dx%d)" % (key, w, h)))
                continue

            bucket = current if current is not None else None
            def add_pass(pname, plineno, pdetail):
                stats["pass:" + pname] += 1
                if pname in TAIL_NAMES and bucket is not None:
                    bucket.passes.append((pname, plineno, pdetail))
                    bucket.counts[pname] += 1
                else:
                    # head pass of the NEXT reported frame
                    pending.append((pname, plineno, pdetail))

            # --- pass timing ---------------------------------------------
            pm = PERF_RE.match(body)
            if pm:
                stats["perf"] += 1
                name = "PERF:" + pm.group(1)
                add_pass(name, lineno, pm.group(2) + "ms")
                if bucket is not None:
                    bucket.perf.setdefault(pm.group(1), []).append(float(pm.group(2)))
                continue

            # --- building pipe draw events -------------------------------
            if body.startswith("XboxBuilding: frame") or body.startswith("PBRBuilding: frame"):
                stats["bld"] += 1
                if bucket is not None:
                    bucket.bld += 1
                continue
            if body.startswith("BUILDING_BUILDING:") or body.startswith("BUILDING_vehicle:"):
                stats["va"] += 1
                if bucket is not None:
                    bucket.va += 1
                continue

            # --- tagged markers / misc ------------------------------------
            # (order matters: matching pass map first, then catch-alls)
            matched = False
            for rx, name in PASS_MAP:
                if rx.search(body):
                    add_pass(name, lineno, body)
                    matched = True
                    break
            if matched:
                continue

            sm = SMAA_PASS_RE.search(body)
            if sm:
                name = "smaa_prepass" if "PRE" in sm.group(1).upper() else "smaa_pass"
                add_pass(name, lineno, sm.group(2))
                continue

            if FBWP_RE.search(body):
                f = FBWP_RE.search(body)
                stats["fbwp"] += 1
                if bucket is not None:
                    bucket.fbwrites.append(f.group(1))
                continue

            tm = TONEMAP_TC_RE.search(body)
            if tm:
                stats["tonemap_tc"] += 1
                if bucket is not None:
                    luma = float(tm.group(1))
                    bucket.events.append(("tonemap_tc", lineno, body, luma))
                continue

            # switch lines / misc [PostFX] info
            sw = SWITCH_RE.search(body)
            if sw:
                kv = dict(KV_RE.findall(sw.group(1)))
                if bucket is not None:
                    bucket.events.append(("cf_switch", lineno, kv, None))
                if timeline:
                    pass
                stats["cf_switch"] += 1
                continue

            if TAG_RE.search(body) or body.startswith("[PF") or body.startswith("PF "):
                stats["tagged"] += 1
                if bucket is not None:
                    bucket.events.append(("tag", lineno, body, None))
                continue

            if body.startswith("[SMAA-DIAG]") or body.startswith("[PostFX]"):
                stats["postfx_info"] += 1
                if bucket is not None:
                    bucket.events.append(("info", lineno, body, None))
                continue

            # non-screen rasters / zero sizes anywhere (strip hex handles
            # first: 0x26x06 style false hits)
            for mm in FB_SIZE_RE.finditer(HEX_RE.sub("", body)):
                w, h = int(mm.group(1)), int(mm.group(2))
                if w == 0 or h == 0:
                    stats["zero_raster"] += 1
                    if bucket is not None:
                        bucket.zeros.append((lineno, mm.group(0)))
                break

            # keyword sweep (skip benign)
            if not BENIGN_RE.search(body) and BAD_KEYWORDS.search(body):
                kw = BAD_KEYWORDS.search(body).group(0)
                stats["kw:" + kw.lower()] += 1
                keyword_hits.append((lineno, body))
                if bucket is not None:
                    bucket.keywords.append((kw, lineno, body))

    return {
        "path": path,
        "lines": lines_total,
        "first_ts": first_ts,
        "last_ts": last_ts,
        "frames": frames,
        "pre": pre,
        "stats": stats,
        "timeline": timeline,
        "keyword_hits": keyword_hits,
        "lines_total": lines_total,
    }


# ---------------------------------------------------------------- analysis
def diff_chain(prev_names, cur_names):
    """ordered diff: missing, extra, moved."""
    ps, cs = prev_names, cur_names
    missing = [p for p in ps if p not in set(cs)]
    extra = [p for p in cs if p not in set(ps)]
    moved = []
    for i in range(min(len(ps), len(cs))):
        if ps[i] != cs[i] and ps[i] in set(cs) and cs[i] in set(ps):
            moved.append((ps[i], cs[i]))
    return missing, extra, moved


def normalise(names):
    """collapse consecutive duplicates for shape comparison"""
    out = []
    for n in names:
        if not out or out[-1] != n:
            out.append(n)
    return out


def find_anomalies(data, every=1, limit=200):
    frames = data["frames"]
    out = []
    out.append("== ANOMALY SCAN ==")
    idcount = Counter(f.fid for f in frames)
    dups = [fid for fid, c in idcount.items() if c > 1]
    if dups:
        out.append("  duplicate frame ids: %s (first %d)" % (
            sorted(dups)[:10], len(dups)))
    # id gaps
    ids = [f.fid for f in frames if f.fid > 0]
    gaps = []
    for a, b in zip(ids, ids[1:]):
        if b != a + 1:
            gaps.append((a, b))
    if gaps:
        out.append("  frame-id gaps (%d):" % len(gaps))
        for a, b in gaps[:20]:
            out.append("    frame %d -> %d (missing %d)" % (a, b, b - a - 1))
    else:
        if ids:
            out.append("  frame-id gaps: none (ids contiguous %d..%d)" % (ids[0], ids[-1]))
        else:
            out.append("  frame-id gaps: n/a (no PostFX-REPORT frames found)")

    chain_changes = []
    prev = None
    for f in frames:
        names = normalise(f.pass_names())
        if prev is not None and names != prev:
            missing, extra, moved = diff_chain(prev, names)
            chain_changes.append((f.fid, f.line, missing, extra, moved))
        prev = names
    out.append("  chain-shape changes vs previous frame: %d" % len(chain_changes))
    patterns = OrderedDict()
    for fid, line, missing, extra, moved in chain_changes:
        key = (tuple(missing), tuple(extra))
        if key not in patterns:
            patterns[key] = [0, fid, line]
        patterns[key][0] += 1
    for (missing, extra), (cnt, fid, line) in patterns.items():
        note = ""
        if not missing and not extra:
            for cid, ln, mi, ex, mv in chain_changes:
                if cid == fid and mv:
                    note = " (reorder: %s)" % "; ".join("%s<->%s" % m for m in mv[:3])
                    break
        out.append("    x%-4d first at frame %d (line %d): -missing=%s +extra=%s%s" % (
            cnt, fid, line, list(missing) or "-", list(extra) or "-", note))

    zero_frames = [f for f in frames if f.zeros]
    out.append("  zero-size rasters in reports: %d frame(s)" % len(zero_frames))
    for f in zero_frames[:10]:
        out.append("    frame %d line %d: %s" % (f.fid, f.line, f.zeros[:3]))

    kw_frames = [f for f in frames if f.keywords]
    out.append("  fault/guard/FAIL/NaN keyword hits: %d (in %d frames)" % (
        sum(len(f.keywords) for f in kw_frames), len(kw_frames)))
    for f in kw_frames[:10]:
        for kw, line, text in f.keywords[:3]:
            out.append("    frame %d line %d [%s] %s" % (f.fid, line, kw, text[:150]))

    tc = [f for f in frames if any(e[0] == "tonemap_tc" for e in f.events)]
    zero_luma = []
    for f in tc:
        for e in f.events:
            if e[0] == "tonemap_tc" and e[3] == 0.0:
                zero_luma.append((f.fid, f.line, e[3]))
    out.append("  [Tonemap] TC lines: %d frames; sceneLuma==0: %d" % (len(tc), len(zero_luma)))
    for fid, line, luma in zero_luma[:10]:
        out.append("    frame %d line %d sceneLuma=%.3f  <-- BLACK INPUT SIGNATURE" % (fid, line, luma))
    if tc:
        sample = next((e[2] for e in tc[0].events if e[0] == "tonemap_tc"), "")
        out.append("    sample: frame %d: %s" % (tc[0].fid, sample[:160]))

    out.append("  building-pipe draw events (XboxBuilding/PBRBuilding): %d total; "
               "per-frame min/max: %s" % (
                   data["stats"]["bld"],
                   bite_stats(frames)))
    return out


def bite_stats(frames):
    vals = [f.bld for f in frames] or [0]
    return "min=%d max=%d" % (min(vals), max(vals))


def blackscan(data, limit=100):
    """heuristic full-screen-black-frame detection.

    Black signature = a frame where NONE of the writers that put lit content
    into the visible chain ran (ColourFilter_Modern / UpdateFrontBuffer /
    SMAA pre-pass / IVGrade / Tonemap / FXAA3 blit), or where the tonemapper
    measured sceneLuma==0, or where nothing was drawn at all (no building
    pipe events, no PERF chain)."""
    frames = data["frames"]
    out = ["== BLACKSCAN (heuristic) =="]
    writers = ("PERF:FXAA3", "PERF:IVGrade", "ColourFilter_Modern",
               "UpdateFrontBuffer", "smaa_prepass", "PERF:Tonemap")
    sig = []
    for f in frames:
        names = set(f.pass_names())
        reasons = []
        if not (names & set(writers)):
            reasons.append("no visible-chain writer")
        if not f.passes:
            reasons.append("empty pass chain")
        for e in f.events:
            if e[0] == "tonemap_tc" and e[3] == 0.0:
                reasons.append("sceneLuma=0")
        if f.fid > 0 and f.report and f.report.get("camRas", "").endswith("x0"):
            reasons.append("zero camera raster")
        if reasons:
            sig.append((f.fid, f.line, reasons, sorted(names)[:8]))
    out.append("  candidate black frames: %d / %d" % (len(sig), len(frames)))
    for fid, line, reasons, names in sig[:limit]:
        out.append("    frame %d (line %d): %s" % (fid, line, "; ".join(reasons)))
    if not sig:
        out.append("  none detected by heuristic")

    # Sky-only speck pattern (proxy): scene drew nothing but the tonemapper
    # still measured light -> HUD/2D-only content on a black scene. This log
    # emits no HUD draw markers, so bld==0 is treated as unreliable and only
    # reported for context, never as a primary signature.
    speck = [f for f in frames if f.bld == 0]
    out.append("  frames with 0 building-pipe draws (NOT a black signal on its "
               "own - interiors/loading): %d" % len(speck))
    return out


# ---------------------------------------------------------------- reporting
def fmt_report(data, args):
    L = []
    w = L.append
    w("proflog.py - %s" % data["path"])
    w("lines=%d  frames=%d  span=%s .. %s" % (
        data["lines"], len(data["frames"]), data["first_ts"], data["last_ts"]))
    st = data["stats"]
    w("records: reports=%d perf=%d fbwp=%d cf_switch=%d tonemap_tc=%d "
      "bld_events=%d unparsed=%d" % (
          st.get("reports", 0), st.get("perf", 0), st.get("fbwp", 0),
          st.get("cf_switch", 0), st.get("tonemap_tc", 0), st.get("bld", 0),
          st.get("unparsed", 0)))

    # config timeline
    if not args.blackscan:
        w("")
        w("== CONFIG TIMELINE (from PostFX-REPORT) ==")
        last = None
        for fid, line, ts, kv in data["timeline"]:
            cur = (kv.get("filter"), kv.get("pipeline"), kv.get("smaa"),
                   kv.get("SSAO"), kv.get("normalBuf"), kv.get("pipeChain"))
            if cur != last:
                w("  frame %-6d line %-6d %s  filter=%s pipeline=%s smaa=%s "
                  "ssao=%s normalBuf=%s pipeChain=%s" % (
                      fid, line, ts, kv.get("filter"), kv.get("pipeline"),
                      kv.get("smaa"), kv.get("SSAO"), kv.get("normalBuf"),
                      kv.get("pipeChain")))
                last = cur

    # pass frequency
    if not args.blackscan:
        w("")
        w("== PASS FREQUENCY (per-frame occurrences) ==")
        pf = Counter()
        for f in data["frames"]:
            for k in f.counts:
                pf[k] += 1
        for name, cnt in pf.most_common(40):
            w("  %-28s %6d frames" % (name, cnt))

    if not args.blackscan and not args.summary:
        # sampled chain (pass ORDER per frame)
        w("")
        w("== FRAME CHAIN SAMPLE (every %d frames) ==" % args.frames)
        n = 0
        for f in data["frames"]:
            if f.fid % args.frames != 0 and f.fid != 1:
                continue
            n += 1
            if n > args.limit:
                w("  ... capped at %d sampled frames" % args.limit)
                break
            names = normalise(f.pass_names())
            w("  frame %-6d %s  n=%d: %s" % (
                f.fid, f.ts, len(names), " > ".join(names) if names else "(no passes)"))

        w("")
        L.extend(find_anomalies(data, args.frames))

    L.extend(blackscan(data, args.limit))

    kw = data["keyword_hits"]
    w("")
    w("== KEYWORD SWEEP (fault/guard/FAIL/NaN) ==")
    w("  total hits: %d" % len(kw))
    uniq = OrderedDict()
    for line, text in kw:
        key = text[:120]
        if key in uniq:
            uniq[key][1] += 1
            uniq[key][2] = line
        else:
            uniq[key] = [line, 1, line]
    shown = 0
    for text, (firstline, cnt, lastline) in uniq.items():
        w("    x%-5d first line %d (last %d): %s" % (cnt, firstline, lastline, text))
        shown += 1
        if shown >= args.limit:
            break
    if len(uniq) > shown:
        w("    (+%d more unique messages)" % (len(uniq) - shown))
    return "\n".join(L)


def do_grep(data, pat, limit=200):
    rx = re.compile(pat, re.I)
    out = ["== GREP '%s' on %s ==" % (pat, data["path"])]
    n = 0
    with open(data["path"], "r", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            if rx.search(raw):
                # attach frame context
                fid = 0
                for f in data["frames"]:
                    if f.line > lineno:
                        break
                    fid = f.fid
                out.append("  line %-6d (frame ~%s) %s" % (lineno, fid, raw.rstrip()[:200]))
                n += 1
                if n >= limit:
                    break
    if n == 0:
        out.append("  (no matches)")
    out.append("  total shown: %d" % n)
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="skygfx_dbg.log per-frame pass chain analyser")
    ap.add_argument("logfile", nargs="?", default=DEFAULT_LOG)
    ap.add_argument("--frames", type=int, default=250,
                    help="stride for chain sample: print pass chain every Nth frame")
    ap.add_argument("--every", type=int, default=None,
                    help=argparse.SUPPRESS)  # deprecated alias of --frames
    ap.add_argument("--grep", metavar="PAT", help="grep mode (regex, case-insensitive)")
    ap.add_argument("--summary", action="store_true", help="totals only, no chain/anomaly detail")
    ap.add_argument("--blackscan", action="store_true", help="black-frame heuristics only")
    ap.add_argument("--limit", type=int, default=100, help="detail list cap")
    ap.add_argument("-o", "--out", metavar="FILE", help="write report to file instead of stdout")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)
    if args.every is not None:
        args.frames = args.every  # backwards-compatible alias

    if not os.path.isfile(args.logfile):
        print("log not found: %s" % args.logfile, file=sys.stderr)
        return 2
    if not args.quiet:
        print("parsing %s ..." % args.logfile, file=sys.stderr)
    data = parse(args.logfile)

    if args.grep:
        text = do_grep(data, args.grep, args.limit)
    else:
        text = fmt_report(data, args)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        print("wrote %s (%d bytes)" % (args.out, len(text)))
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
