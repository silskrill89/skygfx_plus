"""Scrape the official GTA SAS 1987 mod site for the live mod list.

The site (https://gtasas.netlify.app/) is a Vite/React SPA — the HTML
shell has no links; all download URLs live inside the JS bundle
(<script type="module" src="/assets/index-HASH.js">). So we:

    1. GET the site HTML.
    2. Find the JS bundle URL.
    3. GET the JS bundle.
    4. Regex-extract every https URL whose host is a known download host.
    5. Match the URLs back to our known mod IDs by URL pattern.
    6. Override the hardcoded URLs in config.ALL_MODS with the scraped ones.

If the main mod URL is not found on the official site, we also try
LibertyCity.net's search as a fallback source.

If scraping fails at any step, callers fall back to the hardcoded list.
"""
from __future__ import annotations

import logging
import re
from typing import List, Optional
from urllib.parse import quote_plus, urlsplit, urlunsplit

import requests
from bs4 import BeautifulSoup

from . import config

log = logging.getLogger(__name__)


# Known mod display names on the site → our internal mod id.
# We use these to merge scraped URLs with our install_order metadata.
NAME_TO_ID = {
    "cleo 5": "cleo5",
    "cleo5": "cleo5",
    "cleo+": "cleo_plus",
    "cleo plus": "cleo_plus",
    "newopcodes": "newopcodes",
    "new opcodes": "newopcodes",
    "dyom": "dyom",
    "dyom 8.1": "dyom",
    "gta san andreas stories": "gta_sas_1987",
    "gta sas": "gta_sas_1987",
    "gta sas 1987": "gta_sas_1987",
    "gta sas storyline": "gta_sas_1987",
}

# Hosts we consider "download" sources when scanning the JS bundle.
DOWNLOAD_HOSTS = (
    "mediafire.com",
    "mega.nz",
    "drive.google.com",
    "github.com",
    "mixmods.com.br",
    "cleo.li",
    "gtagarage.com",
)


# ----------------------------------------------------------------------
# Public entry point
# ----------------------------------------------------------------------
def fetch_page(url: str = config.MOD_SITE_URL, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Fetch a URL. Returns None on network failure."""
    try:
        resp = requests.get(
            url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
        )
        resp.raise_for_status()
        return resp.text
    except Exception as e:
        log.warning("Failed to fetch %s: %s", url, e)
        return None


def resolve_mod_sources(timeout: int = config.HTTP_TIMEOUT) -> List[config.ModSource]:
    """Fetch the site, parse mod links, and return an ordered ModSource list.

    Falls back to config.ALL_MODS (hardcoded) on any error.
    URLs scraped from the site OVERRIDE the hardcoded ones so the wizard
    always uses the freshest links the mod author has published.
    """
    html = fetch_page(timeout=timeout)
    if not html:
        log.warning("Site fetch failed — falling back to hardcoded mod list.")
        return list(config.ALL_MODS)

    # The site is a React SPA — try to extract URLs from the JS bundle first.
    overrides = _scrape_js_bundle(html, timeout=timeout)
    if not overrides:
        # Fall back to a plain HTML scrape in case the site ever ships
        # server-rendered content.
        overrides = _scrape_html_links(html)

    if not overrides:
        log.warning("Site scrape returned no mod links — falling back to hardcoded list.")
        return list(config.ALL_MODS)

    merged = _merge_with_defaults(overrides)
    log.info("Resolved %d mod sources (%d URLs overridden from site).",
             len(merged), len(overrides))

    # LibertyCity fallback: if the main mod wasn't found on the official site,
    # try searching LibertyCity.net for "San Andreas Stories" and use the
    # first matching file page as an alternative source.
    if "gta_sas_1987" not in overrides:
        lc_url = _search_libertycity("San Andreas Stories", timeout=timeout)
        if lc_url:
            log.info("LibertyCity fallback for main mod: %s", lc_url)
            for i, m in enumerate(merged):
                if m.id == "gta_sas_1987":
                    merged[i] = _with_url(m, lc_url)
                    break

    return merged


# ----------------------------------------------------------------------
# LibertyCity search (fallback when the main mod isn't on gtasas.netlify.app)
# ----------------------------------------------------------------------
_LC_FILE_RE = re.compile(
    r'/files/gta-san-andreas/(\d+)-([a-z0-9-]+)',
    re.IGNORECASE,
)


def _search_libertycity(query: str, timeout: int = config.HTTP_TIMEOUT) -> Optional[str]:
    """Search LibertyCity.net for `query` and return the first matching file URL."""
    url = config.LIBERTYCITY_SEARCH_URL.format(q=quote_plus(query))
    log.info("Searching LibertyCity: %s", url)
    try:
        resp = requests.get(
            url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("LibertyCity search failed: %s", e)
        return None

    soup = BeautifulSoup(resp.text, "html.parser")
    seen = set()
    for a in soup.find_all("a", href=True):
        href = a["href"]
        m = _LC_FILE_RE.search(href)
        if not m:
            continue
        text = a.get_text(strip=True)
        if not text or text in ("More details", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12"):
            continue
        full_url = href if href.startswith("http") else f"https://libertycity.net{href}"
        if full_url in seen:
            continue
        seen.add(full_url)
        # Heuristic: prefer entries with "stories" in the title
        if "stories" in text.lower() or "1987" in text.lower():
            log.info("LibertyCity match: %r -> %s", text, full_url)
            return full_url
    # Fall back to the first found file
    if seen:
        first = next(iter(seen))
        log.info("LibertyCity first-result fallback: %s", first)
        return first
    return None


# ----------------------------------------------------------------------
# JS bundle scraping (primary method for the React SPA)
# ----------------------------------------------------------------------
_JS_BUNDLE_RE = re.compile(
    r'<script[^>]+type="module"[^>]+src="([^"]+\.js)"',
    re.IGNORECASE,
)
_URL_RE = re.compile(r"""https?://[^\s"'`<>)\\]+""")


def _scrape_js_bundle(html: str, timeout: int = config.HTTP_TIMEOUT) -> dict[str, str]:
    """Find the JS bundle, fetch it, extract download URLs.

    Returns a dict {mod_id: url}. Returns {} on failure.
    """
    overrides: dict[str, str] = {}

    m = _JS_BUNDLE_RE.search(html)
    if not m:
        log.info("No module script tag found in HTML — trying plain HTML links.")
        return overrides

    bundle_path = m.group(1)
    if bundle_path.startswith("/"):
        # Resolve against the site origin
        from urllib.parse import urlsplit
        parts = urlsplit(config.MOD_SITE_URL)
        bundle_url = f"{parts.scheme}://{parts.netloc}{bundle_path}"
    else:
        bundle_url = bundle_path

    log.info("Fetching JS bundle: %s", bundle_url)
    try:
        resp = requests.get(
            bundle_url,
            headers={"User-Agent": config.HTTP_USER_AGENT},
            timeout=timeout,
        )
        resp.raise_for_status()
    except Exception as e:
        log.warning("Failed to fetch JS bundle: %s", e)
        return overrides

    js = resp.text
    urls = set(_URL_RE.findall(js))
    log.info("JS bundle contained %d total URLs.", len(urls))

    # Keep only download-host URLs
    download_urls = [u for u in urls if any(h in u for h in DOWNLOAD_HOSTS)]
    log.info("Filtered to %d download-host URLs.", len(download_urls))

    # Match each URL to a mod_id by URL pattern.
    # Prefer GitHub URLs over MixMods article URLs (which are Cloudflare-blocked).
    github_overrides: dict[str, str] = {}
    other_overrides: dict[str, str] = {}
    for url in download_urls:
        mod_id = _classify_url(url)
        if not mod_id:
            continue
        if "github.com" in url.lower() and mod_id not in github_overrides:
            github_overrides[mod_id] = url
            log.info("  matched %s -> %s (GitHub, preferred)", mod_id, url)
        elif mod_id not in other_overrides:
            other_overrides[mod_id] = url
            log.info("  matched %s -> %s", mod_id, url)

    # GitHub overrides win over other scraped URLs
    overrides = {**other_overrides, **github_overrides}
    return overrides


def _classify_url(url: str) -> Optional[str]:
    """Map a scraped URL to one of our known mod IDs (or None)."""
    lower = url.lower()
    # Main mod (MediaFire link with "gta_sas" in the filename)
    if "mediafire.com" in lower and "gta_sas" in lower:
        # Prefer the latest "june_2026" build, but accept any
        if "june_2026" in lower or "gta_sas_june" in lower:
            return "gta_sas_1987"
        # The storyline chapter archive is a different (older) build
        if "storyline" in lower or "chap" in lower:
            return "gta_sas_1987_storyline"
        return "gta_sas_1987"
    # CLEO 5 (GitHub cleolibrary)
    if "cleolibrary/cleo5" in lower or "cleo5" in lower and "github.com" in lower:
        return "cleo5"
    # CLEO+
    if "cleoplus" in lower:
        return "cleo_plus"
    # NewOpcodes
    if "newopcodes" in lower:
        return "newopcodes"
    return None


# ----------------------------------------------------------------------
# HTML link scraping (fallback, in case the site ever ships server-rendered)
# ----------------------------------------------------------------------
def _scrape_html_links(html: str) -> dict[str, str]:
    soup = BeautifulSoup(html, "html.parser")
    overrides: dict[str, str] = {}

    for a in soup.find_all("a", href=True):
        href = a["href"].strip()
        if not any(host in href for host in DOWNLOAD_HOSTS):
            continue
        mod_id = _classify_url(href)
        if mod_id and mod_id not in overrides:
            overrides[mod_id] = href
    return overrides


# ----------------------------------------------------------------------
# Merge
# ----------------------------------------------------------------------
def _with_url(mod: config.ModSource, new_url: str) -> config.ModSource:
    """Return a copy of `mod` with its URL replaced and resolver flags updated."""
    is_mf = "mediafire.com" in new_url and "/file/" in new_url
    is_gh = "github.com" in new_url and "/releases" in new_url
    is_article = "mixmods.com.br" in new_url and "/releases" not in new_url and "/file/" not in new_url
    is_lc = "libertycity.net" in new_url and "/files/" in new_url
    return config.ModSource(
        id=mod.id,
        name=mod.name,
        description=mod.description,
        url=new_url,
        page_url=mod.page_url,
        install_order=mod.install_order,
        extract_to_sa_root=mod.extract_to_sa_root,
        pick_paths=mod.pick_paths,
        is_mediafire=is_mf,
        is_github_release=is_gh,
        is_article_page=is_article,
        is_libertycity=is_lc,
        is_main_mod=mod.is_main_mod,
        optional=mod.optional,
        enabled_by_default=mod.enabled_by_default,
    )


def _merge_with_defaults(overrides: dict[str, str]) -> List[config.ModSource]:
    """Start from config.ALL_MODS, override URLs where scraped.

    Special rule: if the hardcoded URL is GitHub (preferred — direct .zip asset)
    and the scraped URL is MixMods (Cloudflare-blocked), KEEP the hardcoded GitHub URL.
    """
    merged: List[config.ModSource] = []
    for mod in config.ALL_MODS:
        scraped_url = overrides.get(mod.id)
        if scraped_url:
            # Prefer hardcoded GitHub over scraped MixMods
            hardcoded_is_github = "github.com" in mod.url.lower()
            scraped_is_mixmods = "mixmods.com.br" in scraped_url.lower()
            if hardcoded_is_github and scraped_is_mixmods:
                # Keep the hardcoded GitHub URL
                merged.append(mod)
            else:
                merged.append(_with_url(mod, scraped_url))
        else:
            merged.append(mod)

    merged.sort(key=lambda m: m.install_order)
    return merged
