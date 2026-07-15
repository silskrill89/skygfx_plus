"""Scrape GameCopyWorld for GTA San Andreas NO-CD / Fixed EXE patches.

GCW's SA page lists dozens of patches. We want the v1.0 No-CD/Fixed EXE
(HOODLUM) which is the standard downgrade target for modding.

Each patch entry on GCW looks like:
    <a name="GTA: San Andreas v1.0 [ALL] No-CD/Fixed EXE">...</a>
    ...
    <a href='enable_javascript.shtml' onMouseDown="cbox('https://dl.gamecopyworld.com/?c=19330&b=0&a=0&d=2005&f=hlm-gsaeu!rar' ); return false;">

So we:
    1. Fetch the GCW SA page.
    2. Find all <a name="..."> entries that contain "No-CD/Fixed EXE" or
       "Crash Fix" in their name.
    3. For each, find the nearest following <a onMouseDown="cbox('URL')">.
    4. Extract the dl.gamecopyworld.com URL from the cbox() call.

GCW sits behind Cloudflare but isn't currently bot-wall blocking the SA
page itself. If it ever does, the user can manually download the patch
.zip and point the wizard at it on the downgrade page.
"""
from __future__ import annotations

import logging
import re
from dataclasses import dataclass
from typing import List, Optional

import requests
from bs4 import BeautifulSoup

from . import config

log = logging.getLogger(__name__)


# ----------------------------------------------------------------------
# Public data
# ----------------------------------------------------------------------
@dataclass
class NoCDPatch:
    name: str           # e.g. "GTA: San Andreas v1.0 [ALL] No-CD/Fixed EXE"
    group: str          # e.g. "HOODLUM"
    date: str           # e.g. "08-06-2005"
    file_size: str      # e.g. "[4.5 MB]"
    download_url: str   # e.g. "https://dl.gamecopyworld.com/?c=19330&b=0&a=0&d=2005&f=hlm-gsaeu!rar"
    recommended: bool = False


# ----------------------------------------------------------------------
# Regexes
# ----------------------------------------------------------------------
# Match the cbox('URL') call inside onMouseDown
_CBOX_RE = re.compile(
    r"""cbox\(\s*['"]([^'"]+)['"]\s*\)""",
    re.IGNORECASE,
)

# Match file archive size like "[4.5 MB]"
_SIZE_RE = re.compile(r"\[\s*([\d.]+\s*[KMG]?B)\s*\]", re.IGNORECASE)


# ----------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------
def fetch_patches(timeout: int = config.HTTP_TIMEOUT) -> List[NoCDPatch]:
    """Fetch the GCW SA page and return all No-CD / Fixed EXE patches.

    Returns an empty list on network failure.
    """
    headers = {
        "User-Agent": config.HTTP_USER_AGENT,
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language": "en-US,en;q=0.9",
    }
    try:
        resp = requests.get(config.GAMECOPYWORLD_SA_URL, headers=headers, timeout=timeout)
        resp.raise_for_status()
    except Exception as e:
        log.warning("Failed to fetch GCW SA page: %s", e)
        return []

    return parse_patches(resp.text)


def parse_patches(html: str) -> List[NoCDPatch]:
    """Parse the GCW SA page HTML and return all No-CD / Fixed EXE patches.

    The page structure is:
        <table class="t7">
            <tr><td rowspan="2"><a name="...">patch name</a></td>
                <td>date</td></tr>
            <tr><td>group</td></tr>
        </table>
        <table class="t8">
            ...
            <a href='enable_javascript.shtml' onMouseDown="cbox('URL')">
            ...
            File Archive [4.5 MB]
            ...
        </table>
    """
    soup = BeautifulSoup(html, "html.parser")
    patches: List[NoCDPatch] = []

    # Find every <a name="..."> that names a No-CD/Fixed EXE or Crash Fix
    for anchor in soup.find_all("a", attrs={"name": True}):
        name = anchor.get("name", "").strip()
        if not any(kw in name.lower() for kw in ("no-cd/fixed exe", "crash fix", "fixed exe")):
            continue
        # Skip Definitive Edition entries (different game)
        if "definitive edition" in name.lower():
            continue

        # Find the date and group — they're in the same <table class="t7"> row
        t7_table = anchor.find_parent("table", class_="t7")
        date_str = ""
        group = ""
        if t7_table:
            fonts = t7_table.find_all("font", size="1")
            if len(fonts) >= 1:
                date_str = fonts[0].get_text(strip=True)
            if len(fonts) >= 2:
                group = fonts[1].get_text(strip=True)

        # Find the download URL — it's in the next <table class="t8"> sibling
        t8_table = t7_table.find_next_sibling("table", class_="t8") if t7_table else None
        if not t8_table:
            # Try finding within parent's next siblings
            parent = t7_table.parent if t7_table else None
            if parent:
                t8_table = parent.find("table", class_="t8")

        download_url = ""
        file_size = ""
        if t8_table:
            # Find the <a> with onMouseDown="cbox('...')"
            for a in t8_table.find_all("a"):
                onmousedown = a.get("onmousedown", "")
                m = _CBOX_RE.search(onmousedown)
                if m:
                    download_url = m.group(1).replace("&amp;", "&")
                    break
            # Find the file archive size
            text = t8_table.get_text(" ", strip=True)
            m = _SIZE_RE.search(text)
            if m:
                file_size = f"[{m.group(1)}]"

        if download_url:
            patches.append(NoCDPatch(
                name=name,
                group=group,
                date=date_str,
                file_size=file_size,
                download_url=download_url,
                recommended=_is_recommended(name, group),
            ))

    log.info("GCW: parsed %d No-CD/Fixed EXE patches.", len(patches))
    return patches


def recommended_patch(patches: List[NoCDPatch]) -> Optional[NoCDPatch]:
    """Pick the best No-CD patch for v1.0 downgrading.

    Preference order:
        1. "v1.0 [ALL] No-CD/Fixed EXE" by HOODLUM (universal v1.0 patch)
        2. "v1.0 [EN] No-CD/Fixed EXE" by HOODLUM (English-only v1.0 patch)
        3. Any "v1.0" No-CD/Fixed EXE
        4. The first patch in the list
    """
    if not patches:
        return None

    # Look for exact match
    for p in patches:
        n = p.name.lower()
        if "v1.0" in n and "[all]" in n and "no-cd/fixed exe" in n and "hoodlum" in p.group.lower():
            return p
    for p in patches:
        n = p.name.lower()
        if "v1.0" in n and "[en]" in n and "no-cd/fixed exe" in n and "hoodlum" in p.group.lower():
            return p
    for p in patches:
        n = p.name.lower()
        if "v1.0" in n and "no-cd/fixed exe" in n:
            return p
    return patches[0]


def _is_recommended(name: str, group: str) -> bool:
    """True if this patch is our top recommendation for v1.0 downgrading."""
    n = name.lower()
    g = group.lower()
    return ("v1.0" in n and "[all]" in n and "no-cd/fixed exe" in n and "hoodlum" in g)


# ----------------------------------------------------------------------
# Direct download helper
# ----------------------------------------------------------------------
def download_patch(
    patch: NoCDPatch,
    dest_path: str,
    progress=None,
    timeout: int = config.HTTP_TIMEOUT,
) -> str:
    """Download a GCW patch archive to dest_path.

    GCW's dl.gamecopyworld.com URLs redirect to a CDN. We just follow
    redirects and stream the bytes.
    """
    import os
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    headers = {
        "User-Agent": config.HTTP_USER_AGENT,
        "Referer": config.GAMECOPYWORLD_SA_URL,
    }
    import requests as rq
    with rq.get(patch.download_url, headers=headers, stream=True, timeout=timeout, allow_redirects=True) as r:
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
                        pass
    return dest_path
