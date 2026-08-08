#!/usr/bin/env py -3.13
"""
YouTube Research Scraper
Fetches transcripts, chapters, subtitles, and metadata from YouTube videos.

Usage:
    python tools/youtube_scraper.py <video_url_or_id>
    python tools/youtube_scraper.py --search "query" [--limit N]
    python tools/youtube_scraper.py --channel <channel_url> [--limit N]
    python tools/youtube_scraper.py --batch urls.txt
"""

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

try:
    import yt_dlp
except ImportError:
    print("ERROR: yt-dlp not installed. Run: py -3.13 -m pip install yt-dlp")
    sys.exit(1)

DEFAULT_OUTPUT = Path(__file__).parent.parent / "research" / "youtube-transcripts"


def extract_video_id(url_or_id: str) -> str:
    """Extract video ID from URL or return as-is."""
    if re.match(r'^[a-zA-Z0-9_-]{11}$', url_or_id):
        return url_or_id
    patterns = [
        r'(?:youtube\.com/watch\?v=)([a-zA-Z0-9_-]{11})',
        r'(?:youtu\.be/)([a-zA-Z0-9_-]{11})',
        r'(?:youtube\.com/embed/)([a-zA-Z0-9_-]{11})',
    ]
    for p in patterns:
        m = re.search(p, url_or_id)
        if m:
            return m.group(1)
    return None


def format_timestamp(seconds: float) -> str:
    """Convert seconds to HH:MM:SS."""
    td = timedelta(seconds=int(seconds))
    total_seconds = int(td.total_seconds())
    hours, remainder = divmod(total_seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours > 0:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes:02d}:{secs:02d}"


def fetch_video_data(video_id: str, proxy: str = None, write_dir: Path = None) -> dict:
    """Fetch full video data using yt-dlp: metadata, chapters, subtitles, description."""
    url = f"https://www.youtube.com/watch?v={video_id}"
    
    ydl_opts = {
        'quiet': True,
        'no_warnings': True,
        'skip_download': True,
        'writesubtitles': True,
        'writeautomaticsub': True,
        'subtitleslangs': ['en'],
        'writeinfojson': False,
    }
    
    if proxy:
        ydl_opts['proxy'] = proxy
    
    if write_dir:
        ydl_opts['outtmpl'] = str(write_dir / f"{video_id}.%(ext)s")
    
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(url, download=False)
            
            # Extract chapters
            chapters = []
            if info.get('chapters'):
                for ch in info['chapters']:
                    chapters.append({
                        'start': ch.get('start_time', 0),
                        'end': ch.get('end_time', 0),
                        'title': ch.get('title', ''),
                    })
            
            # Extract subtitles if available
            subs = info.get('subtitles', {})
            auto_subs = info.get('automatic_captions', {})
            
            # Parse description for timestamps
            description = info.get('description', '')
            desc_timestamps = parse_description_timestamps(description)
            
            return {
                'video_id': video_id,
                'title': info.get('title', ''),
                'channel': info.get('channel', '') or info.get('uploader', ''),
                'channel_id': info.get('channel_id', ''),
                'upload_date': info.get('upload_date', ''),
                'duration': info.get('duration', 0),
                'view_count': info.get('view_count', 0),
                'like_count': info.get('like_count', 0),
                'description': description,
                'tags': info.get('tags', []),
                'categories': info.get('categories', []),
                'chapters': chapters,
                'has_manual_subs': bool(subs.get('en')),
                'has_auto_subs': bool(auto_subs.get('en')),
                'subtitles_available': list(subs.keys()) if subs else [],
                'auto_subtitles_available': list(auto_subs.keys())[:10] if auto_subs else [],
                'desc_timestamps': desc_timestamps,
            }
    except Exception as e:
        return {
            'video_id': video_id,
            'error': str(e),
        }


def parse_description_timestamps(description: str) -> list:
    """Extract timestamps from video description."""
    timestamps = []
    # Match patterns like "0:00", "1:23", "12:34", "1:23:45"
    pattern = r'^(\d{1,2}:\d{2}(?::\d{2})?)\s+(.+)$'
    
    for line in description.split('\n'):
        line = line.strip()
        m = re.match(pattern, line)
        if m:
            ts_str = m.group(1)
            title = m.group(2).strip()
            
            # Convert to seconds
            parts = ts_str.split(':')
            if len(parts) == 3:
                seconds = int(parts[0]) * 3600 + int(parts[1]) * 60 + int(parts[2])
            elif len(parts) == 2:
                seconds = int(parts[0]) * 60 + int(parts[1])
            else:
                continue
            
            timestamps.append({
                'start': seconds,
                'title': title,
            })
    
    return timestamps


def format_chapters_text(data: dict) -> str:
    """Format video with chapters for easy reading."""
    lines = []
    
    lines.append(f"# {data.get('title', 'Unknown')}")
    lines.append(f"Channel: {data.get('channel', 'Unknown')}")
    lines.append(f"Duration: {format_timestamp(data.get('duration', 0))}")
    lines.append(f"URL: https://www.youtube.com/watch?v={data['video_id']}")
    lines.append("")
    
    # Chapters
    chapters = data.get('chapters', [])
    desc_ts = data.get('desc_timestamps', [])
    
    if chapters:
        lines.append("## Chapters")
        for ch in chapters:
            ts = format_timestamp(ch['start'])
            lines.append(f"- [{ts}] {ch['title']}")
        lines.append("")
    elif desc_ts:
        lines.append("## Timestamps (from description)")
        for ts in desc_ts:
            t = format_timestamp(ts['start'])
            lines.append(f"- [{t}] {ts['title']}")
        lines.append("")
    
    # Description
    if data.get('description'):
        lines.append("## Description")
        lines.append(data['description'][:2000])
        lines.append("")
    
    # Subtitle info
    lines.append("## Subtitles")
    lines.append(f"Manual: {'Yes' if data.get('has_manual_subs') else 'No'}")
    lines.append(f"Auto-generated: {'Yes' if data.get('has_auto_subs') else 'No'}")
    if data.get('subtitles_available'):
        lines.append(f"Languages: {', '.join(data['subtitles_available'][:10])}")
    
    return "\n".join(lines)


def download_subtitles(video_id: str, output_dir: Path, proxy: str = None) -> Path:
    """Download subtitles for a video using yt-dlp."""
    url = f"https://www.youtube.com/watch?v={video_id}"
    
    ydl_opts = {
        'quiet': True,
        'no_warnings': True,
        'skip_download': True,
        'writesubtitles': True,
        'writeautomaticsub': True,
        'subtitleslangs': ['en'],
        'subtitlesformat': 'srt/best',
        'outtmpl': str(output_dir / f"{video_id}.%(ext)s"),
    }
    
    if proxy:
        ydl_opts['proxy'] = proxy
    
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(url, download=False)
            # Download the subtitles
            ydl.download([url])
            
            # Find the downloaded subtitle file
            for ext in ['en.srt', 'en.vtt', 'en.json3']:
                sub_file = output_dir / f"{video_id}.{ext}"
                if sub_file.exists():
                    return sub_file
            
            return None
    except Exception as e:
        print(f"  Subtitle download error: {e}")
        return None


def format_srt_to_text(srt_path: Path) -> str:
    """Convert SRT subtitle file to clean text with timestamps."""
    content = srt_path.read_text(encoding='utf-8')
    lines = []
    
    # Parse SRT blocks
    blocks = re.split(r'\n\s*\n', content.strip())
    
    for block in blocks:
        lines_block = block.strip().split('\n')
        if len(lines_block) >= 3:
            # Line 1: index, Line 2: timestamps, Line 3+: text
            timestamp_line = lines_block[1]
            text = ' '.join(lines_block[2:])
            
            # Extract start time
            ts_match = re.match(r'(\d{2}):(\d{2}):(\d{2}),(\d{3})', timestamp_line)
            if ts_match:
                h, m, s, ms = ts_match.groups()
                seconds = int(h) * 3600 + int(m) * 60 + int(s)
                ts = format_timestamp(seconds)
                lines.append(f"[{ts}] {text}")
    
    return "\n".join(lines)


def format_subtitles_text(data: dict, sub_text: str) -> str:
    """Format full transcript from subtitles."""
    lines = []
    
    lines.append(f"# Transcript: {data.get('title', 'Unknown')}")
    lines.append(f"Channel: {data.get('channel', 'Unknown')}")
    lines.append(f"Duration: {format_timestamp(data.get('duration', 0))}")
    lines.append(f"URL: https://www.youtube.com/watch?v={data['video_id']}")
    lines.append("")
    
    # Chapters as section headers
    chapters = data.get('chapters', [])
    if chapters:
        lines.append("## Chapter Index")
        for ch in chapters:
            ts = format_timestamp(ch['start'])
            lines.append(f"- [{ts}] {ch['title']}")
        lines.append("")
        lines.append("---")
        lines.append("")
    
    lines.append("## Transcript")
    lines.append("")
    lines.append(sub_text)
    
    return "\n".join(lines)


def search_videos(query: str, limit: int = 10, proxy: str = None) -> list:
    """Search YouTube for videos."""
    url = f"ytsearch{limit}:{query}"
    
    ydl_opts = {
        'quiet': True,
        'no_warnings': True,
        'skip_download': True,
        'extract_flat': True,
    }
    
    if proxy:
        ydl_opts['proxy'] = proxy
    
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            result = ydl.extract_info(url, download=False)
            videos = []
            for entry in result.get('entries', []):
                videos.append({
                    'video_id': entry.get('id', ''),
                    'title': entry.get('title', ''),
                    'channel': entry.get('channel', '') or entry.get('uploader', ''),
                    'url': f"https://www.youtube.com/watch?v={entry.get('id', '')}",
                    'duration': entry.get('duration', 0),
                })
            return videos
    except Exception as e:
        print(f"Search error: {e}")
        return []


def get_channel_videos(channel_url: str, limit: int = 20, proxy: str = None) -> list:
    """Get recent videos from a channel."""
    try:
        ydl_opts = {
            'quiet': True,
            'no_warnings': True,
            'skip_download': True,
            'extract_flat': True,
            'playlistend': limit,
        }
        
        if proxy:
            ydl_opts['proxy'] = proxy
        
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            result = ydl.extract_info(channel_url + "/videos", download=False)
            videos = []
            for entry in result.get('entries', []):
                videos.append({
                    'video_id': entry.get('id', ''),
                    'title': entry.get('title', ''),
                    'url': f"https://www.youtube.com/watch?v={entry.get('id', '')}",
                })
            return videos
    except Exception as e:
        print(f"Channel error: {e}")
        return []


def process_video(video_id: str, output_dir: Path, proxy: str = None,
                   download_subs: bool = True) -> dict:
    """Process a single video: fetch metadata, chapters, subtitles."""
    
    # Fetch video data
    data = fetch_video_data(video_id, proxy)
    
    if data.get('error'):
        print(f"  ERROR: {data['error']}")
        return data
    
    print(f"  Title: {data['title']}")
    print(f"  Duration: {format_timestamp(data['duration'])}")
    print(f"  Chapters: {len(data.get('chapters', []))}")
    print(f"  Subtitles: manual={data.get('has_manual_subs')}, auto={data.get('has_auto_subs')}")
    
    # Save chapter/metadata file
    chapters_file = output_dir / f"{video_id}_chapters.txt"
    chapters_file.write_text(format_chapters_text(data), encoding='utf-8')
    print(f"  Saved: {chapters_file.name}")
    
    # Download and format subtitles if available
    if download_subs and (data.get('has_manual_subs') or data.get('has_auto_subs')):
        print(f"  Downloading subtitles...")
        sub_file = download_subtitles(video_id, output_dir, proxy)
        
        if sub_file and sub_file.exists():
            sub_text = format_srt_to_text(sub_file)
            transcript = format_subtitles_text(data, sub_text)
            
            transcript_file = output_dir / f"{video_id}_transcript.txt"
            transcript_file.write_text(transcript, encoding='utf-8')
            print(f"  Saved: {transcript_file.name}")
            
            # Clean up raw subtitle file
            sub_file.unlink()
        else:
            print(f"  No subtitles downloaded")
    
    # Save JSON metadata
    json_file = output_dir / f"{video_id}_meta.json"
    # Remove long description from JSON to save space
    json_data = {k: v for k, v in data.items() if k != 'description'}
    json_data['description_preview'] = data.get('description', '')[:500]
    json_file.write_text(json.dumps(json_data, indent=2, default=str), encoding='utf-8')
    
    return data


def main():
    parser = argparse.ArgumentParser(description='YouTube Research Scraper')
    parser.add_argument('urls', nargs='*', help='Video URLs or IDs')
    parser.add_argument('--output', '-o', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--search', '-s', help='Search YouTube')
    parser.add_argument('--channel', '-c', help='Channel URL')
    parser.add_argument('--batch', '-b', type=Path, help='File with URLs')
    parser.add_argument('--limit', '-l', type=int, default=10)
    parser.add_argument('--proxy', '-p', help='Proxy URL (socks5://... or http://...)')
    parser.add_argument('--no-subs', action='store_true', help='Skip subtitle download')
    parser.add_argument('--delay', '-d', type=float, default=3.0, help='Delay between requests')
    
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    
    video_ids = []
    
    # Collect video IDs from various sources
    if args.search:
        print(f"Searching: {args.search}...")
        videos = search_videos(args.search, args.limit, args.proxy)
        print(f"Found {len(videos)} videos:")
        for v in videos:
            dur = format_timestamp(v.get('duration', 0))
            print(f"  {v['video_id']} | [{dur}] {v['title']}")
        video_ids = [v['video_id'] for v in videos]
        
        # Save search results
        index_file = args.output / f"search_{args.search[:30].replace(' ', '_')}.json"
        with open(index_file, 'w', encoding='utf-8') as f:
            json.dump({'query': args.search, 'results': videos}, f, indent=2)
    
    elif args.channel:
        print(f"Fetching channel: {args.channel}...")
        videos = get_channel_videos(args.channel, args.limit, args.proxy)
        print(f"Found {len(videos)} videos:")
        for v in videos:
            print(f"  {v['video_id']} | {v['title']}")
        video_ids = [v['video_id'] for v in videos]
    
    elif args.batch:
        urls = args.batch.read_text(encoding='utf-8').strip().split('\n')
        for line in urls:
            line = line.strip()
            if line and not line.startswith('#'):
                vid = extract_video_id(line)
                if vid:
                    video_ids.append(vid)
        print(f"Batch: {len(video_ids)} videos from file")
    
    elif args.urls:
        for url in args.urls:
            vid = extract_video_id(url)
            if vid:
                video_ids.append(vid)
    
    else:
        parser.print_help()
        return
    
    # Process all videos
    if video_ids:
        print(f"\nProcessing {len(video_ids)} videos...\n")
        results = []
        
        for i, vid in enumerate(video_ids):
            if i > 0:
                time.sleep(args.delay)
            
            print(f"[{i+1}/{len(video_ids)}] {vid}")
            result = process_video(vid, args.output, args.proxy, not args.no_subs)
            results.append(result)
        
        # Summary
        succeeded = len([r for r in results if not r.get('error')])
        failed = len([r for r in results if r.get('error')])
        print(f"\n{'='*50}")
        print(f"Done: {succeeded} succeeded, {failed} failed")
        print(f"Output: {args.output}")


if __name__ == '__main__':
    main()
