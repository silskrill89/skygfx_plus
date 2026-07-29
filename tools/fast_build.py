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
  python tools/fast_build.py --launch     # Build + launch game
  python tools/fast_build.py --watch      # Auto-rebuild on file changes
  python tools/fast_build.py --clean      # Clean build (delete outputs, full rebuild)
  python tools/fast_build.py --reset-env  # Invalidate VS env cache and re-cache
  python tools/fast_build.py --open       # Open game directory after deploy
  python tools/fast_build.py --no-deploy  # Build without deploying

Build Tiers:
  --fastest  : No detection. Compile shaders + MSBuild incremental + deploy. Fastest iteration.
  --fast     : Smart detection. Skip build if no source/shader changes since last build.
  (default)  : Same as --fastest (always build). MSBuild handles C++ incremental compilation.
  --rebuild  : Delete outputs, clean + rebuild from scratch, deploy.
  --clean    : Alias for --rebuild.
"""

import os
import re
import sys
import time
import shutil
import subprocess
import hashlib
import json
from pathlib import Path
from multiprocessing import Pool, cpu_count
from datetime import datetime

# ============================================================
# Paths
# ============================================================

PROJECT_DIR = Path(r'E:\dev(dave)\skygfx_plus_expIV')
BUILD_DIR = PROJECT_DIR / 'build'
GAME_DIR = Path(r'E:\games\gtasa_skygfx_plus')
SHADERS_DIR = PROJECT_DIR / 'shaders'
CSO_DIR = PROJECT_DIR / 'resources' / 'cso'
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
    """Get mtime fingerprint of all shader source files."""
    fingerprint = {}
    for subdir in ['ps', 'vs']:
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

    with open(cache_file, 'r') as f:
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

    with open(cache_file, 'w') as f:
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
            with open(cache_file, 'r') as f:
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

    with open(cache_file, 'w') as f:
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

def _read_shader_hash_cache():
    """Load the shader content hash cache (md5 of HLSL source)."""
    cache_file = CACHE_DIR / 'shader_hashes.json'
    if cache_file.exists():
        try:
            with open(cache_file, 'r') as f:
                return json.load(f)
        except Exception:
            pass
    return {}

def _write_shader_hash_cache(cache):
    """Write shader content hash cache."""
    cache_file = CACHE_DIR / 'shader_hashes.json'
    with open(cache_file, 'w') as f:
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

    # Skip if HLSL content hasn't changed (content hash, not mtime)
    hlsl_hash = _file_md5(hlsl_path)
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
        return ('fail', hlsl_path, result.stderr.strip().split('\n')[0] if result.stderr else 'Unknown error')

def get_shader_list():
    """Get list of all shaders to compile."""
    shaders = []

    # Single-entry shaders
    for subdir, profile in [('ps', 'ps_3_0'), ('vs', 'vs_3_0')]:
        shader_dir = SHADERS_DIR / subdir
        if not shader_dir.exists():
            continue
        for f in shader_dir.rglob('*.hlsl'):
            if 'GTAIV' in f.name and subdir == 'vs':
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
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_neoPass1', 'neoVehiclePass1VS.cso'),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_neoPass2', 'neoVehiclePass2VS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_specCarFx', 'specCarFxPS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_mobileVehicle', 'mobileVehiclePS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_building', 'BuildingPBRPS.cso'),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso'),
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

def compile_shaders_parallel():
    """Compile all shaders in parallel using multiprocessing."""
    timer.begin("Shader Compilation")

    CSO_DIR.mkdir(exist_ok=True)
    shaders = get_shader_list()

    if not shaders:
        print("  No shaders found")
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
                err = result[2] if len(result) > 2 else 'Unknown'
                failures.append((name, err))
                print(f"  FAIL: {name} - {err}")

    # Write updated cache once after all workers finish
    _write_shader_hash_cache(updated_cache)

    print(f"  {results['ok']} compiled, {results['skip']} up-to-date, {results['fail']} failed")
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

def deploy(force=False):
    """Copy ASI + INI to game directory. If force=True, skip backups and just replace."""
    timer.begin("Deploy")

    kill_game_process()

    # Find DLL
    dll_src = None
    for p in [GAME_DIR / 'skygfx.dll', PROJECT_DIR / 'bin' / 'Release' / 'skygfx.dll']:
        if p.exists():
            dll_src = p
            break

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

    ini_dst = GAME_DIR / 'skygfx.ini'
    if not ini_dst.exists():
        ini_src = PROJECT_DIR / 'bin' / 'Release' / 'skygfx.ini'
        if ini_src.exists():
            shutil.copy2(ini_src, ini_dst)
            print(f"  INI deployed (new)")
    else:
        print(f"  INI preserved (exists)")

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

    print("=" * 50)
    print("  SkyGFX Plus - Fast Build")
    print("=" * 50)

    if reset_env:
        reset_env_cache()
        if not any([fastest, fast, rebuild, shaders_only, deploy_only, watch]):
            timer.summary()
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
