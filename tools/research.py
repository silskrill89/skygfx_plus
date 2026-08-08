#!/usr/bin/env python3
"""
research.py — Unified research system for skygfx_plus_expIV

Consolidates: youtube_scraper, research_ingest, doc-ingest

Commands:
  yt <url>           Fetch YouTube video/transcript
  yt --search <q>    Search YouTube
  yt --channel <url> Get channel videos
  ingest <url>       Ingest any URL to research docs
  docs               Scan and index document collection
  status             Show research corpus status
"""
import os
import re
import sys
import json
import time
import hashlib
from pathlib import Path
from datetime import datetime
from urllib.request import urlopen, Request
from urllib.parse import quote
from html.parser import HTMLParser

# Paths
PROJECT = Path(os.environ.get("SKYGFX_PROJECT", "E:/dev(dave)/skygfx_plus_expIV"))
RESEARCH_DIR = PROJECT / "docs/research"
DOCS_DIR = PROJECT / "docs"
MEMORY_CACHE = PROJECT / "memory-bank/.cache/research-pillars"
INDEX_FILE = MEMORY_CACHE / "research-index.json"
UA = {"User-Agent": "skygfx-research/1.0"}

# ═══════════════════════════════════════════════════════════════
# YOUTUBE (from youtube_scraper.py)
# ═══════════════════════════════════════════════════════════════

def extract_video_id(url_or_id):
    if re.match(r'^[a-zA-Z0-9_-]{11}$', url_or_id):
        return url_or_id
    patterns = [r'youtube\.com/watch\?v=([a-zA-Z0-9_-]{11})', r'youtu\.be/([a-zA-Z0-9_-]{11})', r'youtube\.com/embed/([a-zA-Z0-9_-]{11})']
    for pat in patterns:
        m = re.search(pat, url_or_id)
        if m:
            return m.group(1)
    return url_or_id

def fetch_video_data(video_id):
    url = f"https://www.youtube.com/watch?v={video_id}"
    req = Request(url, headers=UA)
    try:
        with urlopen(req, timeout=15) as resp:
            html = resp.read().decode('utf-8', errors='ignore')
    except Exception as e:
        return {"error": str(e), "video_id": video_id}
    
    data = {"video_id": video_id, "url": url}
    
    # Extract title
    m = re.search(r'"title"\s*:\s*"([^"]+)"', html)
    if m:
        data["title"] = m.group(1).encode().decode('unicode_escape', errors='ignore')
    
    # Extract channel
    m = re.search(r'"ownerChannelName"\s*:\s*"([^"]+)"', html)
    if m:
        data["channel"] = m.group(1)
    
    # Extract description
    m = re.search(r'"shortDescription"\s*:\s*"([^"]*(?:\\.[^"]*)*)"', html)
    if m:
        desc = m.group(1).encode().decode('unicode_escape', errors='ignore')
        data["description"] = desc
        data["chapters"] = parse_description_timestamps(desc)
    
    # Extract length
    m = re.search(r'"lengthSeconds"\s*:\s*"(\d+)"', html)
    if m:
        data["length_seconds"] = int(m.group(1))
        data["length_text"] = format_timestamp(int(m.group(1)))
    
    return data

def parse_description_timestamps(description):
    chapters = []
    for line in description.split('\n'):
        m = re.match(r'(\d{1,2}:?\d{2}(?::\d{2})?)\s+(.*)', line.strip())
        if m:
            ts = m.group(1)
            title = m.group(2).strip()
            parts = ts.replace(':', ' ').split()
            seconds = 0
            for p in parts:
                seconds = seconds * 60 + int(p)
            chapters.append({"timestamp": ts, "seconds": seconds, "title": title})
    return chapters

def format_timestamp(seconds):
    h = seconds // 3600
    m = (seconds % 3600) // 60
    s = seconds % 60
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"

def download_subtitles(video_id):
    try:
        import yt_dlp
        ydl_opts = {'skip_download': True, 'writesubtitles': True, 'writeautomaticsub': True, 'subtitleslangs': ['en'], 'quiet': True}
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=False)
            subs = info.get('subtitles', {}) or info.get('automatic_captions', {})
            if 'en' in subs:
                sub_url = subs['en'][0]['url']
                req = Request(sub_url, headers=UA)
                with urlopen(req) as resp:
                    return resp.read().decode('utf-8', errors='ignore')
    except Exception:
        pass
    return None

def search_videos(query, limit=10):
    try:
        import yt_dlp
        ydl_opts = {'quiet': True, 'extract_flat': True, 'playlistend': limit}
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            results = ydl.extract_info(f"ytsearch{limit}:{query}", download=False)
            return [{"id": e['id'], "title": e.get('title', ''), "url": f"https://www.youtube.com/watch?v={e['id']}"} for e in results.get('entries', [])]
    except Exception as e:
        return [{"error": str(e)}]

def process_video(video_id, output_dir):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    data = fetch_video_data(video_id)
    if "error" in data:
        return data
    
    subs = download_subtitles(video_id)
    if subs:
        (output_dir / f"{video_id}_subs.txt").write_text(subs, encoding='utf-8')
        data["has_subtitles"] = True
    
    md = f"# {data.get('title', video_id)}\n\n"
    md += f"- **Channel:** {data.get('channel', 'Unknown')}\n"
    md += f"- **Length:** {data.get('length_text', 'Unknown')}\n"
    md += f"- **URL:** {data.get('url', '')}\n\n"
    
    if data.get("chapters"):
        md += "## Chapters\n\n"
        for ch in data["chapters"]:
            md += f"- [{ch['timestamp']}] {ch['title']}\n"
        md += "\n"
    
    if subs:
        md += "## Transcript\n\n"
        md += subs[:10000]
    
    (output_dir / f"{video_id}.md").write_text(md, encoding='utf-8')
    data["output_file"] = str(output_dir / f"{video_id}.md")
    return data

# ═══════════════════════════════════════════════════════════════
# RESEARCH INGEST (from research_ingest.py)
# ═══════════════════════════════════════════════════════════════

def identify_source(url):
    if "youtube.com" in url or "youtu.be" in url:
        return "youtube"
    if "arxiv.org" in url:
        return "arxiv"
    if "github.com" in url:
        return "github"
    return "web"

def fetch_web_content(url):
    req = Request(url, headers=UA)
    try:
        with urlopen(req, timeout=15) as resp:
            return resp.read().decode('utf-8', errors='ignore')
    except Exception as e:
        return f"Error: {e}"

class TextExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self.result = []
        self.skip = False
    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style', 'nav', 'footer', 'header'):
            self.skip = True
    def handle_endtag(self, tag):
        if tag in ('script', 'style', 'nav', 'footer', 'header'):
            self.skip = False
    def handle_data(self, data):
        if not self.skip:
            text = data.strip()
            if text:
                self.result.append(text)
    def get_text(self):
        return '\n'.join(self.result)

def extract_text_from_html(html_content):
    extractor = TextExtractor()
    extractor.feed(html_content)
    return extractor.get_text()

def ingest_url(url, tags=None, verbose=True):
    source_type = identify_source(url)
    data = {"url": url, "source_type": source_type, "ingested": datetime.now().isoformat(), "tags": tags or []}
    
    if source_type == "youtube":
        vid = extract_video_id(url)
        yt_data = fetch_video_data(vid)
        data.update(yt_data)
        subs = download_subtitles(vid)
        if subs:
            data["transcript"] = subs
    else:
        html = fetch_web_content(url)
        if not html.startswith("Error"):
            m = re.search(r'<title>(.*?)</title>', html, re.IGNORECASE)
            data["title"] = m.group(1).strip() if m else url.split('/')[-1]
            data["text"] = extract_text_from_html(html)[:50000]
    
    slug = re.sub(r'[^a-z0-9]+', '_', data.get("title", url).lower())[:60]
    md = f"---\nurl: {url}\ntype: {source_type}\ntags: {json.dumps(tags or [])}\ningested: {data['ingested']}\n---\n\n"
    md += f"# {data.get('title', url)}\n\n"
    if data.get("text"):
        md += data["text"][:10000]
    elif data.get("description"):
        md += data["description"][:50000]
    elif data.get("transcript"):
        md += data["transcript"][:50000]
    
    save_dir = RESEARCH_DIR / source_type
    save_dir.mkdir(parents=True, exist_ok=True)
    filepath = save_dir / f"{slug}.md"
    filepath.write_text(md, encoding='utf-8')
    data["saved_to"] = str(filepath)
    
    # Update index
    idx = {}
    if INDEX_FILE.exists():
        idx = json.loads(INDEX_FILE.read_text())
    idx[url] = {"title": data.get("title", ""), "type": source_type, "file": str(filepath), "ingested": data["ingested"]}
    MEMORY_CACHE.mkdir(parents=True, exist_ok=True)
    INDEX_FILE.write_text(json.dumps(idx, indent=2))
    
    if verbose:
        print(f"Ingested [{source_type}]: {data.get('title', url)}")
        print(f"  Saved to: {filepath}")
    
    return data

# ═══════════════════════════════════════════════════════════════
# DOC MANAGEMENT (from doc-ingest.py)
# ═══════════════════════════════════════════════════════════════

def scan_research_corpus():
    stats = {"total_files": 0, "by_type": {}, "recent": []}
    if not RESEARCH_DIR.exists():
        return stats
    
    for subdir in RESEARCH_DIR.iterdir():
        if subdir.is_dir():
            files = list(subdir.glob("*.md"))
            stats["by_type"][subdir.name] = len(files)
            stats["total_files"] += len(files)
            for f in sorted(files, key=lambda x: x.stat().st_mtime, reverse=True)[:5]:
                stats["recent"].append({"file": f.name, "type": subdir.name, "modified": datetime.fromtimestamp(f.stat().st_mtime).isoformat()})
    
    return stats

# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Unified research system")
    sub = parser.add_subparsers(dest="command")
    
    # YouTube
    yt = sub.add_parser("yt", help="YouTube operations")
    yt.add_argument("urls", nargs="*", help="Video URLs or IDs")
    yt.add_argument("--search", "-s", help="Search query")
    yt.add_argument("--channel", "-c", help="Channel URL")
    yt.add_argument("--limit", "-l", type=int, default=10)
    yt.add_argument("--output", "-o", help="Output directory")
    
    # Ingest
    ing = sub.add_parser("ingest", help="Ingest URL to research docs")
    ing.add_argument("urls", nargs="+", help="URLs to ingest")
    ing.add_argument("--tags", "-t", help="Comma-separated tags")
    
    # Status
    sub.add_parser("status", help="Show research corpus status")
    
    args = parser.parse_args()
    
    if args.command == "yt":
        out = Path(args.output) if args.output else RESEARCH_DIR / "youtube"
        if args.search:
            results = search_videos(args.search, args.limit)
            for r in results:
                if "error" not in r:
                    print(f"  {r['id']}: {r['title']}")
            return
        if args.channel:
            print("Channel scraping requires yt-dlp (not implemented in unified version)")
            return
        for url in args.urls:
            vid = extract_video_id(url)
            print(f"Processing {vid}...")
            data = process_video(vid, out)
            if "error" in data:
                print(f"  Error: {data['error']}")
            else:
                print(f"  Saved: {data.get('output_file', 'unknown')}")
    
    elif args.command == "ingest":
        tags = args.tags.split(",") if args.tags else []
        for url in args.urls:
            ingest_url(url, tags=tags)
    
    elif args.command == "status":
        stats = scan_research_corpus()
        print("Research Corpus Status")
        print("=" * 50)
        print(f"  Total files: {stats['total_files']}")
        for t, count in stats["by_type"].items():
            print(f"  {t}: {count}")
        if stats["recent"]:
            print(f"\n  Recent:")
            for r in stats["recent"][:5]:
                print(f"    [{r['type']}] {r['file']}")
    
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
