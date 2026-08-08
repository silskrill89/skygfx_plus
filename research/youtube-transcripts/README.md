# YouTube Research Scraper — Usage Guide

## Tool Location
`tools/youtube_scraper.py`

## Quick Start

```bash
# Single video — chapters + metadata
py -3.13 tools/youtube_scraper.py <video_id>

# Search YouTube
py -3.13 tools/youtube_scraper.py --search "Path of Exile rendering" --limit 10

# Channel videos
py -3.13 tools/youtube_scraper.py --channel "https://www.youtube.com/@ThreatInteractive"

# Batch file (one URL per line)
py -3.13 tools/youtube_scraper.py --batch urls.txt

# With proxy (when IP blocked)
py -3.13 tools/youtube_scraper.py --proxy "socks5://127.0.0.1:1080" <video_id>
```

## Output Files

Per video, generates:
- `{video_id}_chapters.txt` — Chapters, timestamps, description, metadata
- `{video_id}_transcript.txt` — Full transcript with timestamps (when subtitles available)
- `{video_id}_meta.json` — Structured metadata

## Known YouTube IP Block

YouTube blocks transcript/subtitle downloads after ~30-50 rapid requests.
Workarounds:
1. Wait 15-30 minutes for block to clear
2. Use `--proxy` with a residential proxy
3. Add `--delay 5` between requests
4. Use `--no-subs` to skip subtitle download (metadata/chapters still work)

## Videos Successfully Scraped

### Path of Exile / GGG
- `TrHHTQqmAaM` — ExileCon 2023: Rendering Path of Exile 2 (52:56)
  - Chapters: SUBSURFACE SHADOWS, BENT NORMALS SHADOWS, GLOBAL ILLUMINATION
  - Alexander Sannikov, Grinding Gear Games
- `whyJzrVEgVc` — ExileCon 2019: Evolving Path of Exile's Renderer (54:37)
  - 33 chapters: PBR, Shadows, GI, Subsurface, Grass, Vectorization
  - Alexander Sannikov, Grinding Gear Games
- `EXnoHTqO7TE` — ExileCon: Procedural World Generation
- `ShiFEvlzbew` — Most Performant GI in POE2 Rendering
- `SnNm7rSSvlg` — Threat Interactive: How To Optimize Modern Game Rendering (2:01:25)
  - 15 chapters: Prepass, Deferred, Shadows, GI, SSS, Tone Mapping, AA, Velocity

### SIGGRAPH / Industry
- `GOee6lcEbWg` — Ghost of Tsushima: Lighting, Atmosphere, Tonemapping (59:28)
- `kbQc2cVRLSY` — Far Cry Dunia Engine Shader Pipeline (58:22)
  - 30 chapters: PSO, Shader Variations, Validation, Testing
- `XXT3GZ4dXb0` — SIGGRAPH 2019 Advances in Real-Time Rendering (1:46:09)

### Other
- `ElBUUMi_L5c` — Understanding Crysis 3's Rendering
- `53NvFT37HzQ` — Wicked Engine Render Breakdown
- `e6pNYHDOSAs` — Math Behind Realtime Graphics

## Dependencies
- `py -3.13 -m pip install yt-dlp youtube_transcript_api`
