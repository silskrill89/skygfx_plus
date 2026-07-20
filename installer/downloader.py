"""HTTP downloader with progress callbacks and four link resolvers:

    1. MediaFire file-page URLs (https://www.mediafire.com/file/<id>/<name>/file)
       — parse the page HTML for the direct-download URL.
    2. GitHub releases URLs (https://github.com/<owner>/<repo>/releases)
       — call api.github.com/repos/<owner>/<repo>/releases/latest and pick
       the first .zip browser_download_url.
    3. MixMods-style article pages (https://www.mixmods.com.br/.../<slug>/)
       — fetch the article HTML and find the first .zip download link.
    4. LibertyCity file pages (https://libertycity.net/files/gta-san-andreas/<id>-<slug>.html)
       — fetch the file page and find the download link.
"""
from __future__ import annotations

import logging
import os
import re
from typing import Callable, Optional
from urllib.parse import urlsplit, urlunsplit

import requests

from . import config

log = logging.getLogger(__name__)

# Type for progress callbacks: (bytes_downloaded, total_bytes_or_None) -> None
ProgressCb = Callable[[int, Optional[int]], None]


# ----------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------
def download(
    url: str,
    dest_path: str,
    progress: Optional[ProgressCb] = None,
    is_mediafire: bool = False,
    is_github_release: bool = False,
    is_article_page: bool = False,
    is_libertycity: bool = False,
    timeout: int = config.HTTP_TIMEOUT,
) -> str:
    """Download a URL to dest_path. Resolves MediaFire / GitHub / MixMods / LibertyCity URLs first.

    Returns the final on-disk path. Raises on network/IO error.
    """
    # Try resolvers in order; only one will fire.
    if is_mediafire or ("mediafire.com" in url and "/file/" in url):
        direct = resolve_mediafire(url, timeout=timeout)
        if direct:
            url = direct
    elif is_github_release or ("github.com" in url and "/releases" in url):
        direct = resolve_github_release(url, timeout=timeout)
        if direct:
            url = direct
    elif is_libertycity or ("libertycity.net" in url and "/files/" in url):
        direct = resolve_libertycity(url, timeout=timeout)
        if direct:
            url = direct
    elif is_article_page or ("mixmods.com.br" in url):
        direct = resolve_article_page(url, timeout=timeout)
        if direct:
            url = direct

    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    headers = {"User-Agent": config.HTTP_USER_AGENT}

    with requests.get(url, headers=headers, stream=True, timeout=timeout, allow_redirects=True) as r:
        r.raise_for_status()
        total = int(r.headers.get("Content-Length", "0")) or None
        downloaded = 0
        with open(dest_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=config.CHUNK_SIZE):
                if not chunk:
                    continue
                f.write(chunk)
                downloaded += len(chunk)
                if progress:
                    try:
                        progress(downloaded, total)
                    except Exception:
                        log.debug("Progress callback raised — ignoring.", exc_info=True)
        if progress:
            try:
                progress(downloaded or os.path.getsize(dest_path), total or downloaded)
            except Exception:
                pass
    return dest_path


# ----------------------------------------------------------------------
# MediaFire resolver
# ----------------------------------------------------------------------
_MEDIAFIRE_DIRECT_RE = re.compile(
    r'https://download\d+\.mediafire\.com/[a-zA-Z0-9_/?=&%.-]+',
    re.IGNORECASE,
)
_MEDIAFIRE_INPUT_RE = re.compile(
    r'<a[^>]+class="[^"]*input[^"]*"[^>]+href="([^"]+)"',
    re.IGNORECASE,
)


def resolve_mediafire(file_page_url: str, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Resolve a MediaFire file-page URL to its direct download URL.

    Returns None if resolution fails (caller may try the page URL directly).
    """
    log.info("Resolving MediaFire URL: %s", file_page_url)
    try:
        resp = requests.get(
            file_page_url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
            allow_redirects=True,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("MediaFire page fetch failed: %s", e)
        return None

    html = resp.text

    m = _MEDIAFIRE_INPUT_RE.search(html)
    if m:
        url = m.group(1).replace("&amp;", "&")
        log.info("MediaFire resolved via input tag: %s", url)
        return url

    m = _MEDIAFIRE_DIRECT_RE.search(html)
    if m:
        url = m.group(0).rstrip("\"'<>")
        log.info("MediaFire resolved via direct URL regex: %s", url)
        return url

    m = re.search(r'"download_link"\s*:\s*"([^"]+)"', html)
    if m:
        url = m.group(1).replace("\\/", "/")
        log.info("MediaFire resolved via JSON: %s", url)
        return url

    log.warning("Could not resolve MediaFire direct download URL.")
    return None


# ----------------------------------------------------------------------
# GitHub releases resolver
# ----------------------------------------------------------------------
_GH_REPO_RE = re.compile(
    r'^https?://github\.com/([^/]+)/([^/]+)(?:/releases)?/?.*$',
    re.IGNORECASE,
)


def resolve_github_release(releases_url: str, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Resolve a GitHub releases page URL to the latest .zip asset URL.

    Accepts any of:
        https://github.com/<owner>/<repo>/releases
        https://github.com/<owner>/<repo>/releases/latest
        https://github.com/<owner>/<repo>
    """
    m = _GH_REPO_RE.match(releases_url)
    if not m:
        log.warning("Could not parse GitHub repo from %s", releases_url)
        return None

    owner, repo = m.group(1), m.group(2)
    api = f"https://api.github.com/repos/{owner}/{repo}/releases/latest"
    log.info("Resolving GitHub release via: %s", api)

    try:
        resp = requests.get(
            api,
            headers={
                "User-Agent": config.HTTP_USER_AGENT,
                "Accept": "application/vnd.github+json",
            },
            timeout=timeout,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("GitHub API call failed: %s", e)
        return None

    try:
        data = resp.json()
    except Exception as e:
        log.warning("GitHub API response not JSON: %s", e)
        return None

    # Prefer the first .zip browser_download_url
    assets = data.get("assets") or []
    for asset in assets:
        url = asset.get("browser_download_url", "")
        if url.lower().endswith(".zip"):
            log.info("GitHub release resolved: %s", url)
            return url

    # Some releases don't have asset .zips — fall back to the source tarball
    if data.get("zipball_url"):
        log.info("GitHub release resolved (zipball): %s", data["zipball_url"])
        return data["zipball_url"]

    log.warning("GitHub release had no .zip asset.")
    return None


# ----------------------------------------------------------------------
# Article-page resolver (MixMods and similar WordPress blogs)
# ----------------------------------------------------------------------
_ZIP_LINK_RE = re.compile(
    r'<a[^>]+href="([^"]+\.zip)"',
    re.IGNORECASE,
)


def resolve_article_page(article_url: str, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Fetch a MixMods-style article page and return the first .zip link.

    Returns None if no .zip download link is found.
    """
    log.info("Resolving article page: %s", article_url)
    try:
        resp = requests.get(
            article_url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
            allow_redirects=True,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("Article page fetch failed: %s", e)
        return None

    html = resp.text
    matches = _ZIP_LINK_RE.findall(html)
    if not matches:
        log.warning("No .zip links found on article page.")
        return None

    # Resolve relative URLs against the article URL
    raw = matches[0]
    if raw.startswith("http"):
        url = raw
    else:
        parts = urlsplit(article_url)
        url = urlunsplit((parts.scheme, parts.netloc, raw, "", ""))

    log.info("Article page resolved: %s", url)
    return url


# ----------------------------------------------------------------------
# LibertyCity resolver (https://libertycity.net/files/.../<id>-<slug>.html)
# ----------------------------------------------------------------------
_LC_DOWNLOAD_LINK_RE = re.compile(
    r'<a[^>]+href="([^"]*(?:download|engine/download)[^"]*)"',
    re.IGNORECASE,
)
_LC_HREF_ZIP_RE = re.compile(
    r'href="([^"]+\.zip)"',
    re.IGNORECASE,
)


def resolve_libertycity(file_page_url: str, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Resolve a LibertyCity file page URL to its direct download URL.

    LibertyCity pages typically require login for direct downloads, and the
    download flow goes through engine/download.php with a referer check.
    Returns None if no download link can be found.
    """
    log.info("Resolving LibertyCity URL: %s", file_page_url)
    try:
        resp = requests.get(
            file_page_url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
            allow_redirects=True,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("LibertyCity page fetch failed: %s", e)
        return None

    html = resp.text

    # Strategy 1: look for an engine/download.php link
    m = _LC_DOWNLOAD_LINK_RE.search(html)
    if m:
        raw = m.group(1).replace("&amp;", "&")
        if raw.startswith("http"):
            url = raw
        else:
            parts = urlsplit(file_page_url)
            url = urlunsplit((parts.scheme, parts.netloc, raw, "", ""))
        log.info("LibertyCity resolved via download link: %s", url)
        return url

    # Strategy 2: any .zip link on the page
    m = _LC_HREF_ZIP_RE.search(html)
    if m:
        raw = m.group(1).replace("&amp;", "&")
        if raw.startswith("http"):
            url = raw
        else:
            parts = urlsplit(file_page_url)
            url = urlunsplit((parts.scheme, parts.netloc, raw, "", ""))
        log.info("LibertyCity resolved via .zip link: %s", url)
        return url

    log.warning("Could not resolve LibertyCity download URL.")
    return None


# ----------------------------------------------------------------------
# Helpers used by the wizard
# ----------------------------------------------------------------------
def head_total_bytes(url: str, timeout: int = 10) -> Optional[int]:
    """HEAD a URL to learn its Content-Length (for progress bar sizing)."""
    try:
        r = requests.head(
            url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
            allow_redirects=True,
        )
        cl = r.headers.get("Content-Length")
        return int(cl) if cl else None
    except Exception:
        return None

