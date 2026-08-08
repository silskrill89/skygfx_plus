#!/usr/bin/env python3
"""
doc-ingest.py — Universal documentation ingestion for dream consolidation
Reads PDFs, git repos, websites, text files and creates searchable indexes.

Usage:
    python tools/doc-ingest.py --pdf <file.pdf>           # Extract PDF text
    python tools/doc-ingest.py --repo <url> [path]        # Clone + analyze git repo
    python tools/doc-ingest.py --website <url>            # Fetch + extract web content
    python tools/doc-ingest.py --text <file.txt>          # Index plain text
    python tools/doc-ingest.py --all                       # Ingest everything in docs/
    python tools/doc-ingest.py --dream-context            # Generate dream context from all sources

Output: JSON index + markdown summary for dream agent consumption.
"""
import subprocess
import json
import sys
import os
import hashlib
from pathlib import Path
from datetime import datetime
from collections import defaultdict
import re

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = PROJECT_ROOT / "memory-bank" / ".cache"
DOCS_DIR = PROJECT_ROOT / "docs"
EXTERNAL_DIR = CACHE_DIR / "external-repos"

def ensure_dirs():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    EXTERNAL_DIR.mkdir(parents=True, exist_ok=True)

def extract_pdf(pdf_path):
    """Extract text from PDF using PyPDF2."""
    try:
        from PyPDF2 import PdfReader
        reader = PdfReader(pdf_path)
        text_parts = []
        for page in reader.pages:
            text = page.extract_text()
            if text:
                text_parts.append(text)
        return {
            "path": str(pdf_path),
            "pages": len(reader.pages),
            "text": "\n".join(text_parts),
            "metadata": reader.metadata if reader.metadata else {}
        }
    except Exception as e:
        return {"path": str(pdf_path), "error": str(e)}

def extract_pdf_metadata(pdf_path):
    """Extract PDF metadata without full text extraction."""
    try:
        from PyPDF2 import PdfReader
        reader = PdfReader(pdf_path)
        return {
            "path": str(pdf_path),
            "pages": len(reader.pages),
            "title": reader.metadata.get("/Title", "") if reader.metadata else "",
            "author": reader.metadata.get("/Author", "") if reader.metadata else "",
            "subject": reader.metadata.get("/Subject", "") if reader.metadata else ""
        }
    except Exception as e:
        return {"path": str(pdf_path), "error": str(e)}

def clone_repo(url, name=None):
    """Clone a git repository for analysis."""
    if not name:
        name = url.split("/")[-1].replace(".git", "")
    
    repo_dir = EXTERNAL_DIR / name
    if repo_dir.exists():
        print(f"  Repository already cloned: {repo_dir}")
        return repo_dir
    
    print(f"  Cloning {url}...")
    result = subprocess.run(
        ["git", "clone", "--depth=100", url, str(repo_dir)],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"  Error cloning: {result.stderr}")
        return None
    
    return repo_dir

def analyze_repo(repo_dir):
    """Analyze a git repository's history and structure."""
    repo_dir = Path(repo_dir)
    
    # Get commit history
    result = subprocess.run(
        ["git", "log", "--all", "--format=%H|%ai|%s", "--since=2020-01-01"],
        cwd=repo_dir,
        capture_output=True,
        text=True
    )
    
    commits = []
    for line in result.stdout.strip().split("\n"):
        if "|" in line:
            parts = line.split("|", 2)
            if len(parts) == 3:
                commits.append({
                    "hash": parts[0][:8],
                    "date": parts[1][:10],
                    "message": parts[2]
                })
    
    # Get file structure
    files = []
    for f in repo_dir.rglob("*"):
        if f.is_file() and not any(skip in str(f) for skip in [".git", "node_modules", "__pycache__"]):
            files.append({
                "path": str(f.relative_to(repo_dir)),
                "size": f.stat().st_size,
                "modified": datetime.fromtimestamp(f.stat().st_mtime).isoformat()
            })
    
    # Get README if exists
    readme = ""
    for name in ["README.md", "README.txt", "README"]:
        readme_path = repo_dir / name
        if readme_path.exists():
            readme = readme_path.read_text(encoding="utf-8", errors="replace")[:2000]
            break
    
    return {
        "name": repo_dir.name,
        "path": str(repo_dir),
        "commit_count": len(commits),
        "recent_commits": commits[:50],
        "file_count": len(files),
        "files": files[:200],  # Limit for token efficiency
        "readme": readme
    }

def extract_website(url):
    """Fetch and extract content from a website."""
    try:
        import urllib.request
        import html.parser
        
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=10) as response:
            html = response.read().decode("utf-8", errors="replace")
        
        # Simple text extraction
        text = re.sub(r"<[^>]+>", " ", html)
        text = re.sub(r"\s+", " ", text).strip()
        
        return {
            "url": url,
            "text": text[:5000],  # Limit for token efficiency
            "length": len(text)
        }
    except Exception as e:
        return {"url": url, "error": str(e)}

def index_text_file(filepath):
    """Index a plain text file."""
    filepath = Path(filepath)
    try:
        content = filepath.read_text(encoding="utf-8", errors="replace")
        return {
            "path": str(filepath),
            "lines": len(content.split("\n")),
            "words": len(content.split()),
            "text": content[:3000],  # First 3000 chars
            "extension": filepath.suffix
        }
    except Exception as e:
        return {"path": str(filepath), "error": str(e)}

def scan_docs_dir():
    """Scan the docs directory for all documentation."""
    docs = []
    if DOCS_DIR.exists():
        for f in DOCS_DIR.rglob("*"):
            if f.is_file():
                docs.append({
                    "path": str(f.relative_to(PROJECT_ROOT)),
                    "size": f.stat().st_size,
                    "extension": f.suffix,
                    "modified": datetime.fromtimestamp(f.stat().st_mtime).isoformat()
                })
    return docs

def generate_dream_context(sources):
    """Generate rich context for dream agent from all sources."""
    lines = []
    lines.append("# Documentation Sources Context")
    lines.append(f"Generated: {datetime.now().isoformat()[:10]}")
    lines.append("")
    
    # Summary
    total_sources = sum(len(v) for v in sources.values())
    lines.append(f"## Summary: {total_sources} sources ingested")
    for source_type, items in sources.items():
        lines.append(f"- {source_type}: {len(items)} items")
    lines.append("")
    
    # PDFs
    if sources.get("pdfs"):
        lines.append("## PDF Documents")
        for pdf in sources["pdfs"]:
            if "error" not in pdf:
                lines.append(f"- `{pdf['path']}` ({pdf.get('pages', '?')} pages)")
                if pdf.get("metadata", {}).get("title"):
                    lines.append(f"  Title: {pdf['metadata']['title']}")
        lines.append("")
    
    # External repos
    if sources.get("repos"):
        lines.append("## External Repositories")
        for repo in sources["repos"]:
            if "error" not in repo:
                lines.append(f"- **{repo['name']}**: {repo['commit_count']} commits, {repo['file_count']} files")
                if repo.get("readme"):
                    # First 200 chars of README
                    readme_preview = repo["readme"][:200].replace("\n", " ")
                    lines.append(f"  README: {readme_preview}...")
        lines.append("")
    
    # Websites
    if sources.get("websites"):
        lines.append("## Web Documentation")
        for web in sources["websites"]:
            if "error" not in web:
                lines.append(f"- `{web['url']}` ({web.get('length', 0)} chars)")
        lines.append("")
    
    # Project docs
    if sources.get("project_docs"):
        lines.append("## Project Documentation")
        for doc in sources["project_docs"][:20]:
            lines.append(f"- `{doc['path']}` ({doc.get('size', 0)} bytes)")
        lines.append("")
    
    return "\n".join(lines)

def main():
    ensure_dirs()
    args = sys.argv[1:]
    
    sources = {
        "pdfs": [],
        "repos": [],
        "websites": [],
        "texts": [],
        "project_docs": []
    }
    
    # Parse arguments
    i = 0
    while i < len(args):
        if args[i] == "--pdf" and i + 1 < len(args):
            pdf_path = Path(args[i + 1])
            if pdf_path.exists():
                print(f"Extracting PDF: {pdf_path}")
                sources["pdfs"].append(extract_pdf(pdf_path))
            i += 2
        elif args[i] == "--repo" and i + 1 < len(args):
            url = args[i + 1]
            name = args[i + 2] if i + 2 < len(args) and not args[i + 2].startswith("--") else None
            repo_dir = clone_repo(url, name)
            if repo_dir:
                print(f"Analyzing repo: {repo_dir}")
                sources["repos"].append(analyze_repo(repo_dir))
            i += 2 if not name else 3
        elif args[i] == "--website" and i + 1 < len(args):
            url = args[i + 1]
            print(f"Fetching website: {url}")
            sources["websites"].append(extract_website(url))
            i += 2
        elif args[i] == "--text" and i + 1 < len(args):
            text_path = Path(args[i + 1])
            if text_path.exists():
                print(f"Indexing text: {text_path}")
                sources["texts"].append(index_text_file(text_path))
            i += 2
        elif args[i] == "--all":
            print("Scanning docs directory...")
            sources["project_docs"] = scan_docs_dir()
            i += 1
        elif args[i] == "--dream-context":
            # Generate from cached data
            i += 1
        else:
            i += 1
    
    # Always scan project docs
    if not sources["project_docs"]:
        sources["project_docs"] = scan_docs_dir()
    
    # Generate outputs
    print("\nGenerating outputs...")
    
    # Save full index
    index_path = CACHE_DIR / "doc-index.json"
    index_path.write_text(json.dumps(sources, indent=2, default=str))
    print(f"Index saved to: {index_path}")
    
    # Generate dream context
    context = generate_dream_context(sources)
    context_path = CACHE_DIR / "doc-dream-context.md"
    context_path.write_text(context)
    print(f"Dream context saved to: {context_path}")
    
    # Print summary
    print("\n" + context)
    
    print("\nDone!")

if __name__ == "__main__":
    main()
