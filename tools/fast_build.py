#!/usr/bin/env python3
"""
SkyGFX Plus - Optimized Build Script
Parallel shader compilation, cached environment, tiered build modes.

Usage:
  python tools/fast_build.py              # Smart incremental: rebuild only if sources changed
  python tools/fast_build.py --fastest    # Always rebuild: skip all detection, just build+deploy
  python tools/fast_build.py --fast       # Smart: detect changes, skip if nothing modified
  python tools/fast_build.py --rebuild    # Clean + full rebuild + deploy
  python tools/fast_build.py --shaders    # Only compile shaders
  python tools/fast_build.py --deploy     # Only deploy existing DLL
  python tools/fast_build.py --helper-tex # Only pack assets/helper_tex -> helper_tex.txd (m0061)
  python tools/fast_build.py --launch     # Build + launch game
  python tools/fast_build.py --watch      # Auto-rebuild on file changes
  python tools/fast_build.py --clean      # Clean build (delete outputs, full rebuild)
  python tools/fast_build.py --reset-env  # Invalidate VS env cache and re-cache
  python tools/fast_build.py --open       # Open game directory after deploy
  python tools/fast_build.py --no-deploy  # Build without deploying
  python tools/fast_build.py --check-shaders  # Validate all shaders without building DLL
  python tools/fast_build.py --check      # Build: abort before MSBuild if any shader fails validate
  python tools/fast_build.py --verify-embed  # Read-only resource.h/Resource.rc/CSO cross-check

Build Tiers:
  --fastest  : No detection. Compile shaders + MSBuild incremental + deploy. Fastest iteration.
  --fast     : Smart detection. Skip build if no source/shader changes since last build.
  (default)  : Same as --fastest (always build). MSBuild handles C++ incremental compilation.
  --rebuild  : Delete outputs, clean + rebuild from scratch, deploy.
  --clean    : Alias for --rebuild.

Validation:
  --check-shaders  : Run FXC validation on every HLSL entry point (parallel), print pass/fail
                     per shader, exit 1 if any fail. Never writes to resources/cso/.
  --check          : Before building the DLL, run --check-shaders first. Aborts the entire
                     build if any shader fails validation. Saves time by catching HLSL errors
                     early without waiting for MSBuild.
"""

import os
import re
import sys
import time
import shutil
import subprocess
import hashlib
import json
import textwrap
from pathlib import Path
from multiprocessing import Pool, cpu_count
from datetime import datetime

# ============================================================
# Paths
# ============================================================

PROJECT_DIR = Path(os.environ.get('SKYGFX_PROJECT') or Path(__file__).resolve().parent.parent)
BUILD_DIR = PROJECT_DIR / 'build'
GAME_DIR = Path(os.environ.get('SKYGFX_GAME') or r'E:\games\gtasa_skygfx_plus')
SHADERS_DIR = PROJECT_DIR / 'shaders'
CSO_DIR = PROJECT_DIR / 'resources' / 'cso'
RESOURCE_H = PROJECT_DIR / 'resources' / 'resource.h'
RC_FILE = PROJECT_DIR / 'resources' / 'Resource.rc'
CACHE_DIR = PROJECT_DIR / '.cache'
CACHE_DIR.mkdir(exist_ok=True)

# ============================================================
# SDK paths (static - no auto-detect overhead)
# ============================================================

FXC = r'C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe'
MSBUILD = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
VCVARSALL = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'

# ============================================================
# Timing
# ============================================================

class Timer:
    def __init__(self):
        self.phases = {}
        self.current = None
        self.start = time.time()

    def begin(self, name):
        self.current = name
        self._t = time.time()
        print(f"\n--- {name} ---")

    def end(self):
        if self.current:
            elapsed = time.time() - self._t
            self.phases[self.current] = elapsed
            print(f"    ({elapsed:.2f}s)")

    def summary(self):
        total = time.time() - self.start
        print(f"\n{'='*50}")
        print(f"Build Summary ({total:.2f}s total):")
        for phase, t in self.phases.items():
            print(f"  {phase:20s} {t:6.2f}s")
        print(f"{'='*50}")

timer = Timer()

# ============================================================
# Game Process Management
# ============================================================

def kill_game_process():
    """Kill GTA SA process if running to avoid file-in-use errors."""
    try:
        result = subprocess.run(
            ['tasklist', '/FI', 'IMAGENAME eq gta_sa.exe', '/FO', 'CSV', '/NH'],
            capture_output=True, text=True, timeout=5
        )
        if 'gta_sa.exe' in result.stdout.lower():
            subprocess.run(['taskkill', '/F', '/IM', 'gta_sa.exe'],
                         capture_output=True, timeout=5)
            print("  Killed running gta_sa.exe")
            time.sleep(0.5)
    except Exception:
        pass

# ============================================================
# Source Change Detection
# ============================================================

def get_source_mtimes():
    """Get mtime+size fingerprint of all source files. Returns a dict of
    {relative_path: (mtime, size)} for deterministic hashing."""
    src_dir = PROJECT_DIR / 'src'
    if not src_dir.exists():
        return {}

    fingerprint = {}
    for ext in ['*.cpp', '*.h', '*.hpp']:
        for f in src_dir.rglob(ext):
            rel = f.relative_to(PROJECT_DIR)
            stat = f.stat()
            fingerprint[str(rel)] = (stat.st_mtime, stat.st_size)
    return fingerprint

def get_shader_mtimes():
    """Get mtime fingerprint of all shader source files, including shared
    headers under shaders/include/ (editing one changes every dependent CSO)."""
    fingerprint = {}
    for subdir in ['ps', 'vs', 'include']:
        shader_dir = SHADERS_DIR / subdir
        if not shader_dir.exists():
            continue
        for f in shader_dir.rglob('*.hlsl'):
            rel = f.relative_to(PROJECT_DIR)
            stat = f.stat()
            fingerprint[str(rel)] = (stat.st_mtime, stat.st_size)
    # Also check root shaders dir
    for f in SHADERS_DIR.glob('*.hlsl'):
        rel = f.relative_to(PROJECT_DIR)
        stat = f.stat()
        fingerprint[str(rel)] = (stat.st_mtime, stat.st_size)
    return fingerprint

def get_all_shader_mtimes():
    """mtime+size for every .hlsl under shaders/, including include headers.
    Used by the census, which (unlike the build) must see include edits."""
    fingerprint = {}
    if not SHADERS_DIR.is_dir():
        return fingerprint
    for f in SHADERS_DIR.rglob('*.hlsl'):
        rel = f.relative_to(PROJECT_DIR)
        stat = f.stat()
        fingerprint[str(rel)] = (stat.st_mtime, stat.st_size)
    return fingerprint

def compute_fingerprint(data):
    """Compute a deterministic hash from a fingerprint dict."""
    hasher = hashlib.md5()
    for key in sorted(data.keys()):
        mtime, size = data[key]
        hasher.update(f"{key}:{mtime}:{size}\n".encode())
    return hasher.hexdigest()

def check_changes():
    """Check what has changed since the last build. Returns:
    (sources_changed: bool, shaders_changed: bool, any_changed: bool)
    
    Does NOT auto-write the hash — only commit_hash() does that after
    a successful build.
    """
    cache_file = CACHE_DIR / 'build_fingerprint.json'

    src_fp = get_source_mtimes()
    shader_fp = get_shader_mtimes()

    current = {
        'sources': compute_fingerprint(src_fp) if src_fp else '',
        'shaders': compute_fingerprint(shader_fp) if shader_fp else '',
    }

    if not cache_file.exists():
        return True, True, True

    with open(cache_file, 'r', encoding='utf-8') as f:
        saved = json.load(f)

    src_changed = current['sources'] != saved.get('sources', '')
    shd_changed = current['shaders'] != saved.get('shaders', '')
    return src_changed, shd_changed, src_changed or shd_changed

def commit_hash():
    """Write current fingerprint after a successful build. Only call
    when the build actually succeeded — prevents failed builds from
    'consuming' the change detection."""
    cache_file = CACHE_DIR / 'build_fingerprint.json'

    src_fp = get_source_mtimes()
    shader_fp = get_shader_mtimes()

    current = {
        'sources': compute_fingerprint(src_fp) if src_fp else '',
        'shaders': compute_fingerprint(shader_fp) if shader_fp else '',
    }

    with open(cache_file, 'w', encoding='utf-8') as f:
        json.dump(current, f, indent=2)

def sources_changed():
    """Legacy interface: returns True if any sources changed."""
    _, _, any_changed = check_changes()
    return any_changed

# ============================================================
# VS Environment Cache
# ============================================================

def get_vs_env_cached(force_refresh=False):
    """Get VS environment, cached to disk for speed."""
    cache_file = CACHE_DIR / 'vs_env.json'

    if force_refresh and cache_file.exists():
        cache_file.unlink()
        print("  VS env cache invalidated")

    # Check if cache exists and is recent (1 hour)
    if cache_file.exists():
        age = time.time() - cache_file.stat().st_mtime
        if age < 3600:
            with open(cache_file, 'r', encoding='utf-8') as f:
                return json.load(f)

    # Run vcvarsall.bat and cache result
    print("  Caching VS environment...")
    cmd = f'"{VCVARSALL}" x86 && set'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return None

    env = {}
    seen_keys = set()
    for line in result.stdout.splitlines():
        if '=' in line:
            key, _, value = line.partition('=')
            key_lower = key.lower()
            if key_lower in seen_keys:
                continue
            seen_keys.add(key_lower)
            env[key] = value

    with open(cache_file, 'w', encoding='utf-8') as f:
        json.dump(env, f)
    return env

def reset_env_cache():
    """Delete VS env cache file."""
    cache_file = CACHE_DIR / 'vs_env.json'
    if cache_file.exists():
        cache_file.unlink()
        print("  VS env cache deleted")
    else:
        print("  VS env cache already empty")

# ============================================================
# Parallel Shader Compilation
# ============================================================

def _file_md5(path):
    """Compute MD5 hash of a file's content."""
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()

# HLSL include directive: #include "..." (relative to the file or shaders/)
_HLSL_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"',
                              re.MULTILINE)


def _resolve_hlsl_include(name, from_dir):
    """Resolve an #include target relative to the including file and shaders/."""
    for base in (from_dir, SHADERS_DIR):
        try:
            cand = (base / name).resolve()
        except OSError:
            continue
        if cand.is_file():
            return cand
    return None


def _hlsl_include_closure(path, seen=None):
    """Set of resolved files reachable from `path` via transitive #include."""
    path = Path(path).resolve()
    if seen is None:
        seen = set()
    if path in seen:
        return seen
    seen.add(path)
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return seen
    for m in _HLSL_INCLUDE_RE.finditer(text):
        inc = _resolve_hlsl_include(m.group(1), path.parent)
        if inc is not None:
            _hlsl_include_closure(inc, seen)
    return seen


def shader_source_hash(path):
    """MD5 over the entry file's transitive #include closure (path + content).

    Editing shaders/include/*.hlsl therefore invalidates every dependent CSO,
    not just the entry file that names it."""
    h = hashlib.md5()
    for f in sorted(_hlsl_include_closure(path),
                    key=lambda p: p.as_posix()):
        try:
            rel = f.relative_to(PROJECT_DIR).as_posix()
        except ValueError:
            rel = f.as_posix()
        h.update(rel.encode())
        h.update(b'\0')
        try:
            h.update(f.read_bytes())
        except OSError:
            pass
        h.update(b'\0')
    return h.hexdigest()

def _read_shader_hash_cache():
    """Load the shader content hash cache (md5 of HLSL source), pruning
    entries whose source file no longer exists. Silent — a stale key must
    never block a build."""
    cache_file = CACHE_DIR / 'shader_hashes.json'
    if cache_file.exists():
        try:
            with open(cache_file, 'r', encoding='utf-8') as f:
                cache = json.load(f)
        except Exception:
            return {}
        pruned = {}
        for key, value in cache.items():
            # Key format: "<absolute hlsl path>:<entry point>"
            src = key.rsplit(':', 1)[0]
            if os.path.isfile(src):
                pruned[key] = value
        return pruned
    return {}

def _write_shader_hash_cache(cache):
    """Write shader content hash cache."""
    cache_file = CACHE_DIR / 'shader_hashes.json'
    with open(cache_file, 'w', encoding='utf-8') as f:
        json.dump(cache, f, indent=2)

# Module-level cache (loaded once per process in Pool)
_shader_hash_cache = None

def compile_single_shader(args):
    """Compile a single shader. Called by multiprocessing Pool.
    
    Workers only READ the hash cache — the cache is updated once after
    all workers finish to avoid multiprocessing race conditions."""
    global _shader_hash_cache
    fxc, hlsl_path, cso_path, profile, entry = args

    # Load cache once per worker process
    if _shader_hash_cache is None:
        _shader_hash_cache = _read_shader_hash_cache()

    # Skip if HLSL content hasn't changed (content hash, not mtime).
    # The hash covers the entry file's transitive #include closure, so a
    # shaders/include/*.hlsl edit invalidates every dependent CSO.
    hlsl_hash = shader_source_hash(hlsl_path)
    cache_key = f"{hlsl_path}:{entry}"
    if os.path.exists(cso_path) and cache_key in _shader_hash_cache:
        if _shader_hash_cache[cache_key] == hlsl_hash:
            return ('skip', hlsl_path, cache_key, hlsl_hash)

    result = subprocess.run(
        [fxc, '/T', profile, '/nologo', '/E', entry, '/Fo', cso_path, hlsl_path],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return ('ok', hlsl_path, cache_key, hlsl_hash)
    else:
        # Full error text for diagnosis
        err_text = result.stderr.strip() if result.stderr else 'Unknown error'
        return ('fail', hlsl_path, None, err_text)

def validate_single_shader(args):
    """Validate a shader by compiling to a temp target. Never writes to CSO dir.
    Called by multiprocessing Pool. Returns (status, hlsl_path, entry, detail)."""
    fxc, hlsl_path, _cso_path, profile, entry = args
    name = Path(hlsl_path).name

    # Compile to a temp file in system temp directory, then delete it
    import tempfile
    fd, tmp_path = tempfile.mkstemp(suffix='.cso')
    os.close(fd)
    try:
        result = subprocess.run(
            [fxc, '/T', profile, '/nologo', '/E', entry, '/Fo', tmp_path, hlsl_path],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            return ('ok', hlsl_path, entry, None)
        else:
            err_text = result.stderr.strip() if result.stderr else 'Unknown error'
            return ('fail', hlsl_path, entry, err_text)
    finally:
        try:
            os.remove(tmp_path)
        except Exception:
            pass

def get_shader_list():
    """Get list of all shaders to compile."""
    shaders = []

    # Single-entry shaders
    for subdir, profile in [('ps', 'ps_3_0'), ('vs', 'vs_3_0')]:
        shader_dir = SHADERS_DIR / subdir
        if not shader_dir.exists():
            continue
        # Collect all HLSL stems at root level to detect subdir collisions
        root_stems = set()
        for f in shader_dir.glob('*.hlsl'):
            root_stems.add(f.stem)
        for f in shader_dir.rglob('*.hlsl'):
            # GTAIV VS CSOs are prebuilt binaries with no source; compiling a
            # same-named .hlsl would silently replace them. GTAIVBuilding_vs is
            # the ONE exception: it now has an audited source (shaders/vs/
            # GTAIVBuilding_vs.hlsl) that matches what the building callback
            # uploads, so it must be rebuilt into resources/cso/.
            if 'GTAIV' in f.name and subdir == 'vs' and f.name != 'GTAIVBuilding_vs.hlsl':
                continue
            # Skip files handled by multi_entry (dedicated entry points).
            # Includes the legacy standalone building-PS sources: their CSO
            # names are emitted from the consolidated ROOT files
            # (shaders/buildingPipePS.hlsl, shaders/noiseDetailBuildingPS.hlsl)
            # via the multi_entry list below. One producer per CSO — otherwise
            # two pool workers race on the same output file.
            if f.name in ('VehiclePBR_Modern.hlsl', 'mobileVehicleVS.hlsl',
                           'Water_Parallax.hlsl',
                           # buildingPipePS.hlsl main_normMapBuilding emits
                           # normMapBuildingPS.cso (legacy source kept)
                           'normMapBuildingPS.hlsl'):
                continue
            # Skip subdirectrory files whose stem collides with a root file
            # (root file takes priority; multi_entry handles redirects)
            if f.parent != shader_dir and f.stem in root_stems:
                continue
            cso_name = f.stem + '.cso'
            cso_path = CSO_DIR / cso_name
            shaders.append((FXC, str(f), str(cso_path), profile, 'main'))

    # Multi-entry consolidated shaders
    multi_entry = [
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_vehiclePBR', 'vehiclePBRVS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_ps2CarFx', 'ps2CarFxVS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_specCarFx', 'specCarFxVS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_xboxCar', 'xboxCarVS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_leedsCarFx', 'leedsCarFxVS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_mobileVehicle', 'mobileVehicleVS.cso'),
        # neoVehiclePass1VS / neoVehiclePass2VS rows REMOVED — dead build
        # entries: CarPipe::vertexShaderPass1/2 are permanently NULL
        # (neoCarpipe.cpp CreateShaders), nothing loads those CSOs and the
        # VS passes no longer exist. See neoCarpipe.cpp "dead: VS passes
        # removed, see fast_build".
        # VehiclePBR_Modern.hlsl — all 7 entry points from one file
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main', 'VehiclePBR_Modern.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_normMapVehicle', 'normMapVehiclePS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_mobileVehicle', 'mobileVehiclePS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_specCarFx', 'specCarFxPS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_building', 'BuildingPBRPS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_ps2EnvSpecFx', 'ps2EnvSpecFxPS.cso'),
        # Water_Parallax.hlsl — water style entries (waterStyle INI selector)
        ('ps_3_0', 'Water_Parallax.hlsl', 'main_xbox', 'Water_Parallax.cso'),
        ('ps_3_0', 'Water_Parallax.hlsl', 'main_iv', 'Water_IV.cso'),
        ('ps_3_0', 'Water_Parallax.hlsl', 'main_v', 'Water_V.cso'),
        # ---- Root-level consolidated shaders (shaders/*.hlsl) ----
        # buildingPipePS.hlsl — 5 entry points (docs/Building Pipeline.md).
        # CSO names = the legacy per-file names ("Original File" column) that
        # Resource.rc embeds, so no RC changes are needed for these.
        ('ps_3_0', 'buildingPipePS.hlsl', 'main_simple', 'simplePS.cso'),
        ('ps_3_0', 'buildingPipePS.hlsl', 'main_simpleDetail', 'simpleDetailPS.cso'),
        ('ps_3_0', 'buildingPipePS.hlsl', 'main_simpleFog', 'simpleFogPS.cso'),
        ('ps_3_0', 'buildingPipePS.hlsl', 'main_xboxBuilding', 'xboxBuildingPS.cso'),
        ('ps_3_0', 'buildingPipePS.hlsl', 'main_normMapBuilding', 'normMapBuildingPS.cso'),
        # noiseDetailBuildingPS.hlsl — 3 entry points (docs/Building Pipeline.md)
        ('ps_3_0', 'noiseDetailBuildingPS.hlsl', 'main_simpleNoiseDetail', 'simpleNoiseDetailPS.cso'),
        ('ps_3_0', 'noiseDetailBuildingPS.hlsl', 'main_simpleDetailNoiseDetail', 'simpleDetailNoiseDetailPS.cso'),
        ('ps_3_0', 'noiseDetailBuildingPS.hlsl', 'main_xboxBuildingNoiseDetail', 'xboxBuildingNoiseDetailPS.cso'),
        # unifiedPipe.hlsl — all 5 entry functions (WIP forward+ chain, NOT in
        # Resource.rc yet). main is the documented alias of main_pre (/E main =
        # pre-pass); it gets its own CSO so every entry in the file has exactly
        # one output. (Source header suggests pre.cso/occl.cso/post.cso/ibl.cso;
        # prefixed names avoid generic-name collisions in resources/cso/ and
        # give clean names for the future RC wiring.)
        ('ps_3_0', 'unifiedPipe.hlsl', 'main', 'unifiedPipe.cso'),
        ('ps_3_0', 'unifiedPipe.hlsl', 'main_pre', 'unifiedPipePre.cso'),
        ('ps_3_0', 'unifiedPipe.hlsl', 'main_occlusion', 'unifiedPipeOcclusion.cso'),
        ('ps_3_0', 'unifiedPipe.hlsl', 'main_post', 'unifiedPipePost.cso'),
        ('ps_3_0', 'unifiedPipe.hlsl', 'main_ibl', 'unifiedPipeIBL.cso'),
        # CloudsPS.hlsl — the sprite/quad pass needs no row: the single-entry
        # rglob rule above already compiles it with /E main -> CloudsPS.cso
        # (main is a deliberate thin alias of main_clouds). Only the full-screen
        # sky entry needs an explicit row here.
        ('ps_3_0', 'CloudsPS.hlsl', 'main_cloudsSky', 'cloudsSkyPS.cso'),
    ]

    for profile, src_file, entry, out_cso in multi_entry:
        hlsl_path = SHADERS_DIR / src_file
        if not hlsl_path.exists():
            hlsl_path = SHADERS_DIR / 'ps' / src_file
            if not hlsl_path.exists():
                hlsl_path = SHADERS_DIR / 'vs' / src_file
        if hlsl_path.exists():
            cso_path = CSO_DIR / out_cso
            shaders.append((FXC, str(hlsl_path), str(cso_path), profile, entry))

    return shaders


def copy_prebuilt_csos():
    """Seed resources/cso/ with prebuilt CSOs shipped under shaders/.

    .gitignore excludes *.cso, so a fresh clone has no GTAIV CSOs even though
    Resource.rc embeds five of them (rc.exe would fail). Copy any GTAIV*.cso
    and any other prebuilt CSO with no compiler producer when the destination
    is missing. Never overwrites an existing (compiled) CSO."""
    if not SHADERS_DIR.is_dir():
        return 0
    CSO_DIR.mkdir(exist_ok=True)
    produced = {Path(str(e[2])).name for e in get_shader_list()}
    copied = 0
    for src in sorted(SHADERS_DIR.rglob('*.cso'), key=lambda p: p.as_posix()):
        name = src.name
        if 'GTAIV' not in name and name in produced:
            continue  # compiled from source; never seed a stale binary
        dst = CSO_DIR / name
        if dst.exists():
            continue
        try:
            shutil.copy2(src, dst)
            print(f"  COPY: {name} (prebuilt from shaders/{src.parent.name}/)")
            copied += 1
        except OSError as exc:
            print(f"  WARNING: could not copy prebuilt {name} ({exc})")
    return copied

# ============================================================
# Shader Census
# ============================================================

# HLSL sources that intentionally emit no CSO. Each entry mirrors a skip in
# get_shader_list(); a file that is neither a producer nor listed here is
# reported as unexplained.
_CONSOLIDATED_LEGACY_SOURCES = {
    # buildingPipePS.hlsl main_normMapBuilding emits normMapBuildingPS.cso.
    'normMapBuildingPS.hlsl',
    # vehiclePipeVS.hlsl main_mobileVehicle emits mobileVehicleVS.cso.
    'mobileVehicleVS.hlsl',
}


def _no_output_reason(path):
    """Why this HLSL legitimately has no CSO producer, else None."""
    rel = path.relative_to(SHADERS_DIR)
    parts = rel.parts
    if 'include' in parts[:-1]:
        return 'include header'
    name = path.name
    if len(parts) == 3 and parts[0] in ('ps', 'vs') and \
            (SHADERS_DIR / parts[0] / name).exists():
        return 'duplicate of root-level source'
    if name in _CONSOLIDATED_LEGACY_SOURCES:
        return 'legacy source; CSO emitted by consolidated entry'
    if parts[0] == 'vs' and 'GTAIV' in name:
        return 'prebuilt GTAIV binary (no source producer)'
    return None


def shader_census():
    """Static, read-only picture of HLSL sources -> CSO producers."""
    entries = get_shader_list()
    producer_paths = {Path(str(e[1])).resolve() for e in entries}
    outputs = sorted({Path(str(e[2])).name for e in entries})
    all_hlsl = sorted(SHADERS_DIR.rglob('*.hlsl'),
                      key=lambda p: p.as_posix())

    intentional = {}
    unexplained = []
    for p in all_hlsl:
        if p.resolve() in producer_paths:
            continue
        reason = _no_output_reason(p)
        rel = p.relative_to(PROJECT_DIR).as_posix()
        if reason:
            intentional[rel] = reason
        else:
            unexplained.append(rel)

    disk_csos = (sorted(p.name for p in CSO_DIR.glob('*.cso'))
                 if CSO_DIR.is_dir() else [])
    output_set = set(outputs)
    return {
        'entries': len(entries),
        'producer_sources': len(producer_paths),
        'outputs': outputs,
        'all_hlsl': len(all_hlsl),
        'intentional': intentional,
        'unexplained': sorted(unexplained),
        'missing_outputs': sorted(n for n in outputs
                                  if not (CSO_DIR / n).is_file()),
        'orphans': [n for n in disk_csos if n not in output_set],
    }


def _format_names(names, limit=None, width=104, indent='      '):
    """Deterministic, wrapped list. `limit` caps the display (None = all)."""
    names = [str(n) for n in names]
    shown = ', '.join(names if limit is None else names[:limit])
    if limit is not None and len(names) > limit:
        shown += f', +{len(names) - limit} more'
    return textwrap.fill(shown, width=width, subsequent_indent=indent)


def print_shader_census(stats=None):
    """Compact post-compile census. `stats` = {'ok','skip','fail'} for this run."""
    try:
        c = shader_census()
    except Exception as exc:  # noqa: BLE001 - never break a build on the census
        print(f"  Census: unavailable ({type(exc).__name__}: {exc})")
        return None

    stats = stats or {}
    print(f"  Census: {c['entries']} entries | {stats.get('ok', 0)} compiled | "
          f"{stats.get('skip', 0)} up-to-date | {stats.get('fail', 0)} failed")
    print(f"  Census: {c['producer_sources']}/{c['all_hlsl']} HLSL produce a CSO | "
          f"{len(c['intentional'])} intentional skips | "
          f"{len(c['unexplained'])} unexplained")
    if c['unexplained']:
        print(f"  Census: UNEXPLAINED no-CSO HLSL: "
              f"{_format_names(c['unexplained'])}")
    if c['missing_outputs']:
        print(f"  Census: expected CSO missing: "
              f"{_format_names(c['missing_outputs'])}")
    line = f"  Census: {len(c['orphans'])} CSO(s) without producer"
    if c['orphans']:
        line += f": {_format_names(c['orphans'])}"
    print(line)
    return c


def _write_shader_census(stats, census):
    """Persist the last compile result for tools/doctor.py."""
    cache_file = CACHE_DIR / 'shader_census.json'
    payload = {
        'shader_fingerprint': compute_fingerprint(get_all_shader_mtimes()),
        'entries': census['entries'],
        'compiled': stats.get('ok', 0),
        'up_to_date': stats.get('skip', 0),
        'failed': stats.get('fail', 0),
        'unexplained': census['unexplained'],
        'missing_outputs': census['missing_outputs'],
        'orphans': census['orphans'],
    }
    try:
        with open(cache_file, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(payload, f, indent=2, sort_keys=True)
    except OSError as exc:
        print(f"  Census: could not write {cache_file.name}: {exc}")

# ============================================================
# Embedded Resource Verification
# ============================================================

_IDR_DEFINE_RE = re.compile(
    r'^[ \t]*#define[ \t]+(IDR_\w+)[ \t]+(\d+)\b', re.MULTILINE)
_RCDATA_ROW_RE = re.compile(
    r'^[ \t]*(IDR_\w+)[ \t]+RCDATA[ \t]+"([^"]+)"', re.MULTILINE)
_IDR_TOKEN_RE = re.compile(r'\bIDR_[A-Za-z0-9_]+\b')


def _read_text(path):
    return path.read_text(encoding='utf-8-sig', errors='replace')


def verify_embedded_resources():
    """Cross-check resource.h, Resource.rc, resources/cso and src/ references.
    Read-only. The two hard gates are `missing_rows` and `missing_files`."""
    defines = {}
    if RESOURCE_H.is_file():
        for m in _IDR_DEFINE_RE.finditer(_read_text(RESOURCE_H)):
            defines[m.group(1)] = int(m.group(2))

    rows = {}
    if RC_FILE.is_file():
        for m in _RCDATA_ROW_RE.finditer(_read_text(RC_FILE)):
            rows[m.group(1)] = m.group(2)

    cso_files = (sorted(p.name for p in CSO_DIR.glob('*.cso'))
                 if CSO_DIR.is_dir() else [])
    rc_basenames = {Path(ref).name for ref in rows.values()}

    # A CSO has a "producer" if a compile entry emits it or it ships prebuilt
    # under shaders/ (e.g. GTAIV binaries). Only produced-but-unembedded CSOs
    # are actionable; no-producer leftovers are a separate category.
    producer_names = {Path(str(e[2])).name for e in get_shader_list()}
    if SHADERS_DIR.is_dir():
        producer_names.update(p.name for p in SHADERS_DIR.rglob('*.cso'))
    unembedded = [n for n in cso_files if n not in rc_basenames]

    code_refs = set()
    src_dir = PROJECT_DIR / 'src'
    if src_dir.is_dir():
        for ext in ('*.cpp', '*.h', '*.hpp'):
            for p in src_dir.rglob(ext):
                code_refs.update(_IDR_TOKEN_RE.findall(_read_text(p)))

    return {
        'defines': len(defines),
        'rows': len(rows),
        'cso_files': len(cso_files),
        # hard failures (exit code gates)
        'missing_rows': sorted(set(defines) - set(rows)),
        'missing_files': sorted(f'{sym} -> {ref}' for sym, ref in rows.items()
                                if not (PROJECT_DIR / 'resources' / ref).is_file()),
        # soft reports
        'undefined_rows': sorted(set(rows) - set(defines)),
        'no_idr': [n for n in unembedded if n in producer_names],
        'no_producer': [n for n in unembedded if n not in producer_names],
        'no_code': sorted(set(defines) - code_refs),
    }


def print_embed_report(r):
    """Compact embed report. Exit gates: missing_rows / missing_files only."""
    print(f"  embed: {r['defines']} IDR defines | {r['rows']} RCDATA rows | "
          f"{r['cso_files']} CSOs on disk")
    line = f"  defines without RC row: {len(r['missing_rows'])}"
    if r['missing_rows']:
        line += f" ({_format_names(r['missing_rows'])})"
    print(line)
    line = f"  RC rows without file: {len(r['missing_files'])}"
    if r['missing_files']:
        line += f" ({_format_names(r['missing_files'])})"
    print(line)
    line = f"  CSOs produced but not embedded: {len(r['no_idr'])}"
    if r['no_idr']:
        line += f" ({_format_names(r['no_idr'])})"
    print(line)
    line = f"  CSOs with no producer: {len(r['no_producer'])}"
    if r['no_producer']:
        line += f" ({_format_names(r['no_producer'])})"
    print(line)
    line = f"  IDRs with no code ref: {len(r['no_code'])}"
    if r['no_code']:
        line += f" ({_format_names(r['no_code'])})"
    print(line)
    if r['undefined_rows']:
        print(f"  note: {len(r['undefined_rows'])} RC row(s) without a define: "
              f"{_format_names(r['undefined_rows'])}")
    gate = bool(r['missing_rows'] or r['missing_files'])
    print(f"  RESULT: {'FAIL' if gate else 'OK'} "
          f"(exit gate: defines-without-row, rows-without-file)")

def compile_shaders_parallel():
    """Compile all shaders in parallel using multiprocessing."""
    timer.begin("Shader Compilation")

    CSO_DIR.mkdir(exist_ok=True)
    copy_prebuilt_csos()
    shaders = get_shader_list()

    if not shaders:
        print("  No shaders found")
        print_shader_census({'ok': 0, 'skip': 0, 'fail': 0})
        timer.end()
        return True

    num_workers = min(cpu_count(), len(shaders))
    print(f"  Compiling {len(shaders)} shaders with {num_workers} workers...")

    results = {'ok': 0, 'skip': 0, 'fail': 0}
    failures = []
    updated_cache = _read_shader_hash_cache()

    with Pool(num_workers) as pool:
        for result in pool.imap_unordered(compile_single_shader, shaders):
            status = result[0]
            name = Path(result[1]).name
            if status == 'ok':
                results['ok'] += 1
                # Collect hash updates from successful compiles
                cache_key = result[2]
                hlsl_hash = result[3]
                updated_cache[cache_key] = hlsl_hash
                print(f"  OK:   {name}")
            elif status == 'skip':
                results['skip'] += 1
            elif status == 'fail':
                results['fail'] += 1
                err = result[3] if len(result) > 3 else 'Unknown error'
                failures.append((name, err))
                print(f"  FAIL: {name} - {err.split(chr(10))[0] if chr(10) in err else err}")
                # Print full error block indented
                for line in err.split('\n'):
                    print(f"    {line}")

    # Write updated cache once after all workers finish
    _write_shader_hash_cache(updated_cache)

    # Compact census of the source -> CSO graph, then persist the result so
    # tools/doctor.py can report the last compile without recompiling.
    census = print_shader_census(results)
    if census is not None:
        _write_shader_census(results, census)

    print(f"  {results['ok']} compiled, {results['skip']} up-to-date, {results['fail']} failed")
    if results['fail'] > 0:
        print(f"\n  *** {results['fail']} shader(s) FAILED — see errors above ***")
    timer.end()
    return results['fail'] == 0

def validate_shaders_parallel():
    """Validate all shaders via FXC compilation to temp files (no CSO writes).
    Returns True if all pass, False if any fail. Parallel workers."""
    timer.begin("Shader Validation")

    shaders = get_shader_list()

    if not shaders:
        print("  No shaders found to validate")
        timer.end()
        return True

    num_workers = min(cpu_count(), len(shaders))
    print(f"  Validating {len(shaders)} shader entry points with {num_workers} workers...")

    results = {'ok': 0, 'fail': 0}
    failures = []

    with Pool(num_workers) as pool:
        for result in pool.imap_unordered(validate_single_shader, shaders):
            status = result[0]
            name = Path(result[1]).name
            entry = result[2]
            if status == 'ok':
                results['ok'] += 1
                if entry and entry != 'main':
                    print(f"  PASS: {name} [{entry}]")
                else:
                    print(f"  PASS: {name}")
            elif status == 'fail':
                results['fail'] += 1
                err = result[3] if len(result) > 3 else 'Unknown error'
                failures.append((name, entry, err))
                if entry and entry != 'main':
                    print(f"  FAIL: {name} [{entry}]")
                else:
                    print(f"  FAIL: {name}")
                # Print full FXC error block indented for readability
                for line in err.split('\n'):
                    print(f"    {line}")

    print(f"  {results['ok']} passed, {results['fail']} failed")
    timer.end()
    return results['fail'] == 0

# ============================================================
# Build
# ============================================================

def normalize_env(env):
    """Normalize environment variables to avoid case-insensitive duplicates.
    
    Windows env blocks are case-insensitive but Python dicts are case-sensitive.
    This can cause MSBuild/CL.exe to see duplicate keys like PROGRAMW6432 and
    ProgramW6432. We build a case-insensitive dedup dict and return it.
    """
    normalized = {}
    seen = set()
    for key, value in env.items():
        key_lower = key.lower()
        if key_lower in seen:
            continue
        seen.add(key_lower)
        normalized[key] = value
    return normalized

def build(clean=False):
    """Build project via MSBuild using VS env (no os.environ merge to avoid case collisions)."""
    timer.begin("MSBuild")

    # Kill game if running — the ASI may be locked
    kill_game_process()

    proj = BUILD_DIR / 'skygfx.vcxproj'
    if not proj.exists():
        print(f"  FAIL: {proj} not found")
        timer.end()
        return False

    # Ensure rc.exe has every embedded CSO even on a clean clone (no *.cso in git)
    copy_prebuilt_csos()

    vs_env = get_vs_env_cached()
    # Only use VS env — do NOT merge os.environ (Git Bash has PROGRAMW6432,
    # VS has ProgramW6432, case-different keys break CL.exe MSB6001).
    build_env = vs_env if vs_env else os.environ.copy()
    build_env = normalize_env(build_env)

    if clean:
        print("  Clean build - removing all outputs")
        for out_dir in [BUILD_DIR / 'obj' / 'Release', BUILD_DIR / 'bin',
                        PROJECT_DIR / 'bin' / 'Release']:
            if out_dir.exists():
                shutil.rmtree(out_dir, ignore_errors=True)
                print(f"    Removed: {out_dir}")

        cmd = [MSBUILD, str(proj), '/p:Configuration=Release', '/p:Platform=Win32',
               '/t:Clean;Rebuild', '/nologo', '/v:minimal', '/m']
        print(f"  MSBuild Release|Win32 (clean+rebuild)")
    else:
        cmd = [MSBUILD, str(proj), '/p:Configuration=Release', '/p:Platform=Win32',
               '/nologo', '/v:minimal', '/m']
        print(f"  MSBuild Release|Win32 (incremental)")

    result = subprocess.run(cmd, capture_output=True, text=True,
                           cwd=str(PROJECT_DIR), env=build_env)

    # Persist the full MSBuild output so a "GREEN" claim is provable from disk
    # afterwards. Previously stdout was captured and discarded, which made every
    # build assertion unverifiable (see the 2026-10-26 verification wave).
    try:
        _cachedir = PROJECT_DIR / '.cache'
        _cachedir.mkdir(exist_ok=True)
        _msblog = _cachedir / 'msbuild_last.log'
        with open(_msblog, 'w', encoding='utf-8', errors='replace') as _lf:
            _lf.write('# cmd: %s\n' % ' '.join(cmd))
            _lf.write('# returncode: %d\n' % result.returncode)
            _lf.write(result.stdout or '')
            if result.stderr:
                _lf.write('\n# ---- stderr ----\n')
                _lf.write(result.stderr)
        import re as _re
        _mw = _re.search(r'(\d+)\s+Warning\(s\)', result.stdout or '')
        _me = _re.search(r'(\d+)\s+Error\(s\)', result.stdout or '')
        _wn = _mw.group(1) if _mw else '?'
        _en = _me.group(1) if _me else '?'
        print('  MSBuild: %s warning(s), %s error(s)  [.cache/msbuild_last.log]' % (_wn, _en))
        if _en not in ('?', '0'):
            print('  ERROR: MSBuild reported %s error(s) -- see .cache/msbuild_last.log' % _en)
            timer.end()
            return False
    except Exception as _ex:
        print('  WARNING: could not persist MSBuild log (%s)' % _ex)

    # Check for compile/link errors (ignore post-build copy failures —
    # the DLL was already produced, deploy() handles the copy)
    has_error = False
    for line in result.stdout.split('\n'):
        s = line.strip()
        # Ignore MSB3073 (post-build event) and copy errors — DLL was built
        if 'error ' in s.lower() and 'MSB3073' not in s and 'copy ' not in s.lower():
            print(f"  ERROR: {s}")
            has_error = True

    if has_error:
        print("  Last output lines:")
        for l in result.stdout.split('\n')[-10:]:
            if l.strip():
                print(f"    {l.strip()}")
        timer.end()
        return False

    # Check output — post-build copies DLL→ASI then deletes DLL,
    # so check for ASI (primary) then DLL (before post-build runs)
    asi_path = GAME_DIR / 'skygfx.asi'
    dll_path = GAME_DIR / 'skygfx.dll'
    alt_dll = PROJECT_DIR / 'bin' / 'Release' / 'skygfx.dll'

    for p in [asi_path, dll_path, alt_dll]:
        if p.exists():
            print(f"  Output: {p} ({p.stat().st_size:,} bytes)")
            timer.end()
            return True

    print("  WARNING: Output file not found (DLL or ASI)")
    timer.end()
    return False

# ============================================================
# Deploy
# ============================================================

# ------------------------------------------------------------
# Duplicate-ASI quarantine
# ------------------------------------------------------------
# The ASI loader loads EVERY "*.asi" in the game root. A second copy of
# skygfx (e.g. a reference/backup build left behind) double-loads the
# module, double-hooks the exe and fights the live build's state contracts
# (black world, stray legacy DebugMenu, random feature loss).
# Any loadable skygfx*.asi other than skygfx.asi is renamed to
# "<name>.disabled" before deploy. Reversible - nothing is deleted.
ASI_DIRS = ['', 'scripts', 'modloader', 'CLEO']

def quarantine_duplicate_asis(verbose=True):
    """Disable every loadable skygfx*.asi except the canonical skygfx.asi."""
    disabled = []
    for sub in ASI_DIRS:
        d = (GAME_DIR / sub) if sub else GAME_DIR
        if not d.is_dir():
            continue
        for p in sorted(d.glob('skygfx*.asi')):
            if sub == '' and p.name.lower() == 'skygfx.asi':
                continue
            dst = p.with_name(p.name + '.disabled')
            n = 1
            while dst.exists():
                dst = p.with_name('%s.disabled%d' % (p.name, n))
                n += 1
            try:
                p.rename(dst)
                disabled.append((str(p.relative_to(GAME_DIR)), dst.name))
            except OSError as e:
                print('  WARNING: could not disable %s (%s)' % (p.name, e))
    if disabled and verbose:
        print('  *** DUPLICATE SKYGFX ASI DISABLED (would double-load) ***')
        for src, dst in disabled:
            print('    %s  ->  %s' % (src, dst))
    elif verbose:
        print('  duplicate-ASI guard: game root clean (only skygfx.asi)')
    # report, never touch, archived copies living in subfolders the loader ignores
    if verbose:
        arch = [str(p.relative_to(GAME_DIR)) for p in GAME_DIR.rglob('skygfx*.asi')
                if p.is_file() and p.parent != GAME_DIR]
        for a in arch:
            print('  note: archived copy (subfolder, not loaded): %s' % a)

    # The game launcher deploys launcher_data/dlc/<id> payloads with
    # deploy_mode "root" - a packaged skygfx.asi there would overwrite the
    # live build (and its skygfx.ini would wipe every setting). Refuse loudly.
    lz = GAME_DIR / 'launcher_data' / 'dlc'
    if lz.is_dir():
        for man in sorted(lz.glob('*/manifest.json')):
            try:
                import json as _json
                meta = _json.load(open(man, 'r', encoding='utf-8'))
            except Exception:
                continue
            files = [f.get('path', '') for f in meta.get('files', []) or []]
            if not any(f.lower().startswith('skygfx') and f.lower().endswith('.asi') for f in files):
                continue
            tag = 'ENABLED' if meta.get('default_enabled') else 'disabled'
            print('  WARNING: launcher DLC "%s" (%s, deploy_mode=%s) packages a skygfx ASI: %s'
                  % (meta.get('id', man.parent.name), tag, meta.get('deploy_mode'), files))
            if meta.get('default_enabled'):
                print('           -> the launcher would REPLACE the live build; disable it before launching')
    return disabled


def deploy(force=False):
    """Copy ASI + INI to game directory. If force=True, skip backups and just replace."""
    timer.begin("Deploy")

    kill_game_process()
    quarantine_duplicate_asis()

    # Find DLL — post-build copies DLL directly to game dir as .asi
    dll_src = None
    asi_dst = GAME_DIR / 'skygfx.asi'
    for p in [PROJECT_DIR / 'build' / 'Release' / 'skygfx.dll',
              PROJECT_DIR / 'bin' / 'Release' / 'skygfx.dll',
              GAME_DIR / 'skygfx.dll']:
        if p.exists():
            dll_src = p
            break

    # Seed the INI before the early return: on a first install post-build may
    # already have dropped skygfx.asi, and the INI would never be copied.
    ini_dst = GAME_DIR / 'skygfx.ini'
    if not ini_dst.exists():
        ini_src = PROJECT_DIR / 'bin' / 'Release' / 'skygfx.ini'
        if ini_src.exists():
            shutil.copy2(ini_src, ini_dst)
            print(f"  INI deployed (new)")
    else:
        print(f"  INI preserved (exists)")

    # If post-build already placed the ASI in game dir, consider it deployed
    if not dll_src and asi_dst.exists():
        print(f"  ASI already deployed ({asi_dst.stat().st_size:,} bytes - post-build)")
        timer.end()
        return

    asi_dst = GAME_DIR / 'skygfx.asi'

    if dll_src:
        if force:
            shutil.copy2(dll_src, asi_dst)
            print(f"  ASI deployed ({asi_dst.stat().st_size:,} bytes - FORCE REPLACED)")
        else:
            if asi_dst.exists():
                backup_dir = PROJECT_DIR / 'backups'
                backup_dir.mkdir(exist_ok=True)
                ts = datetime.now().strftime('%Y%m%d_%H%M%S')
                shutil.copy2(asi_dst, backup_dir / f'skygfx_{ts}.asi')

            for old in [asi_dst, asi_dst.with_suffix('.disabled'),
                        Path(str(asi_dst) + '.dl_l')]:
                if old.exists():
                    old.unlink()

            shutil.copy2(dll_src, asi_dst)
            print(f"  ASI deployed ({asi_dst.stat().st_size:,} bytes)")
    else:
        print("  No DLL found to deploy")

    timer.end()

# ============================================================
# Launch
# ============================================================

def launch_game():
    """Launch GTA SA."""
    timer.begin("Launch")
    exe = GAME_DIR / 'gta_sa.exe'
    if exe.exists():
        print(f"  Launching {exe}")
        subprocess.Popen([str(exe)], cwd=str(GAME_DIR))
    else:
        print(f"  FAIL: {exe} not found")
    timer.end()

# ============================================================
# Watch Mode
# ============================================================

def watch_mode():
    """Watch for file changes and auto-rebuild."""
    import time
    try:
        from watchdog.observers import Observer
        from watchdog.events import FileSystemEventHandler
    except ImportError:
        print("  watch mode requires: pip install watchdog")
        return

    print("Watch mode - monitoring for changes...")
    print("Press Ctrl+C to stop")

    class ShaderHandler(FileSystemEventHandler):
        def on_modified(self, event):
            if event.src_path.endswith('.hlsl'):
                print(f"\n[CHANGE] {event.src_path}")
                compile_shaders_parallel()

    class SourceHandler(FileSystemEventHandler):
        def on_modified(self, event):
            if event.src_path.endswith(('.cpp', '.h')):
                print(f"\n[CHANGE] {event.src_path}")
                if build():
                    deploy()

    observer = Observer()
    observer.schedule(ShaderHandler(), str(SHADERS_DIR), recursive=True)
    observer.schedule(SourceHandler(), str(PROJECT_DIR / 'src'), recursive=True)
    observer.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
        print("\nWatch stopped")
    observer.join()

# ============================================================
# Main
# ============================================================

def main():
    args = sys.argv[1:]
    if any(a in ('-h', '--help') for a in args):
        print(__doc__)
        return 0
    fastest = '--fastest' in args
    fast = '--fast' in args
    rebuild = '--rebuild' in args or '--clean' in args
    shaders_only = '--shaders' in args
    deploy_only = '--deploy' in args
    launch = '--launch' in args
    watch = '--watch' in args
    force = '--force' in args
    reset_env = '--reset-env' in args
    open_dir = '--open' in args
    no_deploy = '--no-deploy' in args
    check_shaders = '--check-shaders' in args
    check_before_build = '--check' in args
    verify_embed = '--verify-embed' in args
    helper_tex = '--helper-tex' in args

    print("=" * 50)
    print("  SkyGFX Plus - Fast Build")
    print("=" * 50)

    if reset_env:
        reset_env_cache()
        if not any([fastest, fast, rebuild, shaders_only, deploy_only, watch,
                    check_shaders, verify_embed]):
            timer.summary()
            return

    # Read-only embedded-resource cross-check (resource.h / Resource.rc / CSO / src).
    # Exit nonzero only for a define without an RC row or an RC row without a file.
    if verify_embed:
        report = verify_embedded_resources()
        print_embed_report(report)
        if report['missing_rows'] or report['missing_files']:
            sys.exit(1)
        return

    # Check-shaders-only mode: validate all shaders without building
    if check_shaders:
        ok = validate_shaders_parallel()
        timer.summary()
        if not ok:
            print("\n  SHADER VALIDATION FAILED")
            sys.exit(1)
        print("\n  All shaders valid")
        return

    # Helper-texture packaging mode (directive m0061): stage PNG/DDS ->
    # assets/helper_tex/helper_tex.txd. Standalone early-return like --shaders:
    # never touches FXC, MSBuild or deploy. Extra flags (e.g. --verify, --list,
    # --strict) are forwarded to tools/pack_helper_tex.py.
    if helper_tex:
        fast_build_flags = {
            '--fastest', '--fast', '--rebuild', '--clean', '--shaders',
            '--deploy', '--launch', '--watch', '--force', '--reset-env',
            '--open', '--no-deploy', '--check-shaders', '--check',
            '--verify-embed', '--helper-tex',
        }
        forwarded = [a for a in args if a not in fast_build_flags]
        packer = PROJECT_DIR / 'tools' / 'pack_helper_tex.py'
        rc = subprocess.call([sys.executable, str(packer)] + forwarded)
        timer.summary()
        if rc != 0:
            print("\n  HELPER TEXTURE PACKING FAILED")
            sys.exit(rc)
        print("\n  helper_tex packer finished OK (assets/helper_tex/helper_tex.txd)")
        return

    # Deploy-only mode
    if deploy_only:
        deploy(force=force)
        if launch:
            launch_game()
        if open_dir:
            subprocess.Popen(['explorer', str(GAME_DIR)])
        timer.summary()
        return

    # Shaders-only mode
    if shaders_only:
        compile_shaders_parallel()
        timer.summary()
        return

    # Watch mode
    if watch:
        compile_shaders_parallel()
        if build():
            if not no_deploy:
                deploy(force=force)
        watch_mode()
        return

    # --- Build tier selection ---
    #
    # --fastest : Always build. No change detection. Shaders + MSBuild + deploy.
    # --fast    : Smart detection. Skip build if sources AND shaders unchanged.
    # (default) : Same as --fastest (always build). MSBuild handles C++ incremental.
    # --rebuild : Delete outputs, clean + rebuild from scratch.

    do_build = True
    do_clean = rebuild

    if not fastest and not rebuild:
        # Smart mode: check if anything actually changed
        timer.begin("Change Detection")
        src_changed, shd_changed, any_changed = check_changes()

        if not any_changed:
            print(f"  Sources: unchanged, Shaders: unchanged")
            print("  Nothing changed since last build - skipping build")
            do_build = False
        else:
            src_tag = "CHANGED" if src_changed else "ok"
            shd_tag = "CHANGED" if shd_changed else "ok"
            print(f"  Sources: {src_tag}, Shaders: {shd_tag}")
        timer.end()

    if do_build:
        # Pre-build shader validation gate (--check): catch HLSL syntax errors early.
        # When --check is on, validate before actual compilation so we fail fast.
        # Also auto-validate when the shader set changed (nearly free — same FXC call).
        do_validate = check_before_build
        if not check_before_build and not fastest and not rebuild:
            # In smart mode, validate if shaders changed (the real compile would hit them anyway)
            try:
                _, shd_changed, _ = check_changes()
                do_validate = shd_changed
            except Exception:
                pass

        if do_validate:
            print("  PRE-BUILD SHADER VALIDATION:")
            if not validate_shaders_parallel():
                print("\n  SHADER VALIDATION FAILED — fix HLSL errors above before building")
                sys.exit(1)
            print("  Shader validation passed, proceeding to build")

        # Compile shaders
        if not compile_shaders_parallel():
            print("\n  SHADER COMPILATION FAILED")
            sys.exit(1)

        # Build DLL
        if not build(clean=do_clean):
            print("\n  BUILD FAILED")
            sys.exit(1)

        # Commit hash only after successful build
        commit_hash()
        print("  Build fingerprint saved")

    # Deploy
    if not no_deploy:
        deploy(force=force)

    # Launch if requested
    if launch:
        launch_game()

    if open_dir:
        subprocess.Popen(['explorer', str(GAME_DIR)])

    timer.summary()

if __name__ == '__main__':
    main()
