#!/usr/bin/env py -3.13
"""
Research Ingest — Unified content ingestion pipeline.

Takes any URL (YouTube, arxiv, articles, GitHub, Shadertoy, etc.)
Parses content → saves to research/ + memory-bank/ + docs/

Usage:
    python tools/research_ingest.py <url> [--tags "pbr,gi,shadows"]
    python tools/research_ingest.py --file urls.txt [--tags "rendering"]
    python tools/research_ingest.py --youtube <channel_url> [--limit 20]
"""

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path
from urllib.parse import urlparse

# Paths
PROJECT = Path(__file__).parent.parent
RESEARCH_DIR = PROJECT / "research" / "ingested"
MEMORY_DIR = PROJECT / "memory-bank" / ".cache" / "research-pillars"
DOCS_DIR = PROJECT / "docs" / "research"

# Ensure dirs exist
for d in [RESEARCH_DIR, MEMORY_DIR, DOCS_DIR]:
    d.mkdir(parents=True, exist_ok=True)


def identify_source(url: str) -> str:
    """Identify what kind of URL this is."""
    parsed = urlparse(url)
    host = parsed.hostname or ""
    
    if any(yt in host for yt in ['youtube.com', 'youtu.be']):
        return 'youtube'
    if 'arxiv.org' in host:
        return 'arxiv'
    if 'github.com' in host:
        return 'github'
    if 'shadertoy.com' in host:
        return 'shadertoy'
    if 'drive.google.com' in host:
        return 'gdrive'
    if any(wiki in host for wiki in ['radiance.wiki', 'wikipedia.org']):
        return 'wiki'
    if any(doc in host for doc in ['learnopengl.com', 'gpuopen.com', 'advances.realtimerendering.com']):
        return 'article'
    if any(blog in host for blog in ['mini.gmshaders.com', 'tmpvar.com', '80.lv', 'jason.today']):
        return 'article'
    return 'web'


def slugify(text: str, max_len: int = 60) -> str:
    """Make a filename-safe slug from text."""
    text = re.sub(r'[^\w\s-]', '', text)
    text = re.sub(r'\s+', '-', text.strip())
    return text[:max_len].rstrip('-')


def fetch_youtube(url: str) -> dict:
    """Fetch YouTube video data: metadata, chapters, subtitles."""
    import yt_dlp
    
    video_id = None
    # Extract video ID
    for pattern in [
        r'(?:youtube\.com/watch\?v=)([a-zA-Z0-9_-]{11})',
        r'(?:youtu\.be/)([a-zA-Z0-9_-]{11})',
    ]:
        m = re.search(pattern, url)
        if m:
            video_id = m.group(1)
            break
    
    if not video_id:
        return {'error': f'Could not extract video ID from {url}'}
    
    ydl_opts = {
        'quiet': True,
        'no_warnings': True,
        'skip_download': True,
        'writesubtitles': True,
        'writeautomaticsub': True,
        'subtitleslangs': ['en'],
    }
    
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=False)
            
            chapters = []
            if info.get('chapters'):
                chapters = [{'start': ch.get('start_time', 0), 'title': ch.get('title', '')} for ch in info['chapters']]
            
            # Parse description timestamps
            desc = info.get('description', '')
            desc_ts = []
            for line in desc.split('\n'):
                m = re.match(r'^(\d{1,2}:\d{2}(?::\d{2})?)\s+(.+)$', line.strip())
                if m:
                    parts = m.group(1).split(':')
                    secs = int(parts[0]) * 3600 + int(parts[1]) * 60 + (int(parts[2]) if len(parts) > 2 else 0)
                    desc_ts.append({'start': secs, 'title': m.group(2).strip()})
            
            return {
                'type': 'youtube',
                'video_id': video_id,
                'title': info.get('title', ''),
                'channel': info.get('channel', '') or info.get('uploader', ''),
                'duration': info.get('duration', 0),
                'url': url,
                'description': desc[:3000],
                'chapters': chapters if chapters else desc_ts,
                'has_subs': bool(info.get('subtitles', {}).get('en') or info.get('automatic_captions', {}).get('en')),
            }
    except Exception as e:
        return {'type': 'youtube', 'video_id': video_id, 'error': str(e)}


def fetch_web(url: str) -> dict:
    """Fetch web page content."""
    try:
        import subprocess
        result = subprocess.run(
            ['py', '-3.13', '-c', f'''
import requests
from html.parser import HTMLParser

class TextExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self.text = []
        self.skip = False
        self.skip_tags = {{'script', 'style', 'nav', 'footer', 'header'}}
    def handle_starttag(self, tag, attrs):
        if tag in self.skip_tags:
            self.skip = True
    def handle_endtag(self, tag):
        if tag in self.skip_tags:
            self.skip = False
    def handle_data(self, data):
        if not self.skip:
            t = data.strip()
            if t:
                self.text.append(t)

r = requests.get("{url}", timeout=15, headers={{"User-Agent": "Mozilla/5.0"}})
ext = TextExtractor()
ext.feed(r.text)
print("\\n".join(ext.text[:500]))
'''],
            capture_output=True, text=True, timeout=20
        )
        
        if result.returncode == 0 and result.stdout.strip():
            content = result.stdout.strip()
            # Try to get title from first line or meta
            title_match = re.search(r'<title>(.*?)</title>', content, re.IGNORECASE)
            title = title_match.group(1) if title_match else url.split('/')[-1]
            
            return {
                'type': 'web',
                'title': title,
                'url': url,
                'content': content[:10000],
                'content_length': len(content),
            }
        else:
            return {'type': 'web', 'url': url, 'error': result.stderr[:500] if result.stderr else 'Empty response'}
    except Exception as e:
        return {'type': 'web', 'url': url, 'error': str(e)}


def format_research_entry(data: dict, tags: list = None) -> str:
    """Format parsed data into a research document."""
    lines = []
    
    title = data.get('title', 'Unknown')
    source_type = data.get('type', 'unknown')
    url = data.get('url', '')
    
    lines.append(f"# {title}")
    lines.append("")
    lines.append(f"- **Source**: {source_type}")
    lines.append(f"- **URL**: {url}")
    lines.append(f"- **Ingested**: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    if tags:
        lines.append(f"- **Tags**: {', '.join(tags)}")
    lines.append("")
    
    if source_type == 'youtube':
        dur = data.get('duration', 0)
        mins, secs = divmod(dur, 60)
        hours, mins = divmod(mins, 60)
        dur_str = f"{hours}:{mins:02d}:{secs:02d}" if hours else f"{mins}:{secs:02d}"
        
        lines.append(f"**Channel**: {data.get('channel', 'Unknown')}")
        lines.append(f"**Duration**: {dur_str}")
        lines.append("")
        
        chapters = data.get('chapters', [])
        if chapters:
            lines.append("## Chapters")
            for ch in chapters:
                ts = ch.get('start', 0)
                m, s = divmod(int(ts), 60)
                h, m = divmod(m, 60)
                ts_str = f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"
                lines.append(f"- [{ts_str}] {ch.get('title', '')}")
            lines.append("")
        
        desc = data.get('description', '')
        if desc:
            lines.append("## Description")
            lines.append(desc[:2000])
            lines.append("")
    
    elif source_type == 'web':
        content = data.get('content', '')
        if content:
            lines.append("## Content")
            lines.append(content[:5000])
    
    elif source_type == 'arxiv':
        lines.append("## Paper")
        lines.append(f"URL: {url}")
    
    return "\n".join(lines)


def save_to_memory(data: dict, tags: list = None):
    """Save research to memory-bank for agent access."""
    title = data.get('title', 'unknown')
    slug = slugify(title)
    source_type = data.get('type', 'web')
    
    # Create a compact memory entry
    entry = {
        'title': title,
        'source': source_type,
        'url': data.get('url', ''),
        'ingested': datetime.now().isoformat(),
        'tags': tags or [],
    }
    
    # Add type-specific data
    if source_type == 'youtube':
        entry['channel'] = data.get('channel', '')
        entry['duration'] = data.get('duration', 0)
        entry['chapters'] = [ch.get('title', '') for ch in data.get('chapters', [])]
        entry['has_subs'] = data.get('has_subs', False)
    
    # Save to research index
    index_file = MEMORY_DIR / "research-index.json"
    index = []
    if index_file.exists():
        try:
            index = json.loads(index_file.read_text(encoding='utf-8'))
        except:
            index = []
    
    # Avoid duplicates
    existing_urls = {e.get('url') for e in index}
    if entry['url'] not in existing_urls:
        index.append(entry)
        index_file.write_text(json.dumps(index, indent=2, default=str), encoding='utf-8')
    
    return entry


def save_to_docs(data: dict, tags: list = None):
    """Save research to docs/ for Obsidian/manual access."""
    title = data.get('title', 'unknown')
    slug = slugify(title)
    source_type = data.get('type', 'web')
    
    content = format_research_entry(data, tags)
    
    # Determine subdirectory
    subdir = {
        'youtube': 'youtube',
        'arxiv': 'papers',
        'github': 'github',
        'shadertoy': 'shadertoy',
        'wiki': 'wiki',
    }.get(source_type, 'articles')
    
    target_dir = DOCS_DIR / subdir
    target_dir.mkdir(parents=True, exist_ok=True)
    
    filepath = target_dir / f"{slug}.md"
    filepath.write_text(content, encoding='utf-8')
    
    return filepath


def ingest_url(url: str, tags: list = None, verbose: bool = True) -> dict:
    """Main ingestion pipeline: URL → parse → save to memory + docs."""
    
    source_type = identify_source(url)
    
    if verbose:
        print(f"  Source type: {source_type}")
    
    # Fetch content based on type
    if source_type == 'youtube':
        data = fetch_youtube(url)
    else:
        data = fetch_web(url)
    
    if data.get('error'):
        if verbose:
            print(f"  ERROR: {data['error']}")
        return data
    
    # Save to memory
    memory_entry = save_to_memory(data, tags)
    
    # Save to docs
    doc_path = save_to_docs(data, tags)
    
    if verbose:
        print(f"  Title: {data.get('title', 'Unknown')}")
        if source_type == 'youtube':
            print(f"  Channel: {data.get('channel', '')}")
            print(f"  Chapters: {len(data.get('chapters', []))}")
        print(f"  Saved to: {doc_path}")
    
    return {
        'success': True,
        'data': data,
        'doc_path': str(doc_path),
        'memory_entry': memory_entry,
    }


def main():
    parser = argparse.ArgumentParser(description='Research Ingest Pipeline')
    parser.add_argument('urls', nargs='*', help='URLs to ingest')
    parser.add_argument('--file', '-f', type=Path, help='File with URLs (one per line)')
    parser.add_argument('--tags', '-t', help='Comma-separated tags')
    parser.add_argument('--quiet', '-q', action='store_true')
    
    args = parser.parse_args()
    
    tags = args.tags.split(',') if args.tags else []
    
    urls = list(args.urls)
    
    if args.file:
        lines = args.file.read_text(encoding='utf-8').strip().split('\n')
        for line in lines:
            line = line.strip()
            if line and not line.startswith('#') and line.startswith('http'):
                urls.append(line)
    
    if not urls:
        parser.print_help()
        return
    
    print(f"Ingesting {len(urls)} URLs...\n")
    
    results = []
    for i, url in enumerate(urls):
        url = url.strip()
        if not url:
            continue
        
        print(f"[{i+1}/{len(urls)}] {url[:80]}...")
        result = ingest_url(url, tags, not args.quiet)
        results.append(result)
        
        # Rate limiting for YouTube
        if 'youtube' in url and i < len(urls) - 1:
            time.sleep(3)
    
    # Summary
    succeeded = len([r for r in results if r.get('success')])
    failed = len([r for r in results if not r.get('success')])
    
    print(f"\n{'='*50}")
    print(f"Done: {succeeded} succeeded, {failed} failed")
    print(f"Docs: {DOCS_DIR}")
    print(f"Memory: {MEMORY_DIR}")


if __name__ == '__main__':
    main()
