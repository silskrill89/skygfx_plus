#!/usr/bin/env python3
"""
SkyGFX Plus - Optimized Build Script
Parallel shader compilation, cached environment, fast iteration modes.

Usage:
  python tools/fast_build.py              # Full build (like fix_build.py but faster)
  python tools/fast_build.py --fast       # Skip checks, incremental build, deploy
  python tools/fast_build.py --rebuild    # Clean + build + deploy
  python tools/fast_build.py --shaders    # Only compile shaders
  python tools/fast_build.py --deploy     # Only deploy existing DLL
  python tools/fast_build.py --launch     # Build + launch game
  python tools/fast_build.py --watch      # Auto-rebuild on file changes
  python tools/fast_build.py --clean      # Clean build (no incremental)
  python tools/fast_build.py --reset-env  # Invalidate VS env cache and re-cache
  python tools/fast_build.py --open       # Open game directory after deploy
  python tools/fast_build.py --no-deploy  # Build without deploying
"""

import os
import re
import sys
import time
import shutil
import subprocess
import hashlib
import json
import signal
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

def get_source_hash():
    """Get hash of all source files to detect changes."""
    hasher = hashlib.md5()
    src_dir = PROJECT_DIR / 'src'
    if not src_dir.exists():
        return None
    
    files = []
    for ext in ['*.cpp', '*.h', '*.hpp']:
        files.extend(src_dir.rglob(ext))
    
    for f in sorted(files):
        stat = f.stat()
        hasher.update(f"{f}:{stat.st_mtime}:{stat.st_size}\n".encode())
    
    return hasher.hexdigest()

def sources_changed():
    """Check if sources changed since last build."""
    cache_file = CACHE_DIR / 'source_hash.txt'
    current_hash = get_source_hash()
    
    if cache_file.exists():
        with open(cache_file, 'r') as f:
            old_hash = f.read().strip()
        if old_hash == current_hash:
            return False
    
    with open(cache_file, 'w') as f:
        f.write(current_hash)
    return True

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

def compile_single_shader(args):
    """Compile a single shader. Called by multiprocessing Pool."""
    fxc, hlsl_path, cso_path, profile, entry = args

    # Skip if up-to-date
    if os.path.exists(cso_path):
        if os.path.getmtime(cso_path) >= os.path.getmtime(hlsl_path):
            return ('skip', hlsl_path)

    result = subprocess.run(
        [fxc, '/T', profile, '/nologo', '/E', entry, '/Fo', cso_path, hlsl_path],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return ('ok', hlsl_path)
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
    ]

    for profile, src_file, entry, out_cso in multi_entry:
        # Check both root and subdir
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

    # Use all cores for parallel compilation
    num_workers = min(cpu_count(), len(shaders))
    print(f"  Compiling {len(shaders)} shaders with {num_workers} workers...")

    results = {'ok': 0, 'skip': 0, 'fail': 0}
    failures = []

    with Pool(num_workers) as pool:
        for result in pool.imap_unordered(compile_single_shader, shaders):
            status = result[0]
            name = Path(result[1]).name
            if status == 'ok':
                results['ok'] += 1
                print(f"  OK:   {name}")
            elif status == 'skip':
                results['skip'] += 1
            elif status == 'fail':
                results['fail'] += 1
                err = result[2] if len(result) > 2 else 'Unknown'
                failures.append((name, err))
                print(f"  FAIL: {name} - {err}")

    print(f"  {results['ok']} compiled, {results['skip']} up-to-date, {results['fail']} failed")
    timer.end()
    return results['fail'] == 0

# ============================================================
# Build
# ============================================================

def normalize_env(env):
    """Normalize environment variables to avoid case-insensitive duplicates."""
    normalized = {}
    seen = set()
    for key, value in env.items():
        key_lower = key.lower()
        if key_lower in seen:
            continue
        seen.add(key_lower)
        normalized[key] = value
    return normalized

def build(clean=False, retry_on_env_error=True):
    """Build project via MSBuild. Auto-retries on env collision errors."""
    timer.begin("MSBuild")

    proj = BUILD_DIR / 'skygfx.vcxproj'
    if not proj.exists():
        print(f"  FAIL: {proj} not found")
        timer.end()
        return False

    vs_env = get_vs_env_cached()
    build_env = os.environ.copy()
    if vs_env:
        build_env.update(vs_env)
    build_env = normalize_env(build_env)

    if clean:
        # Force a REAL rebuild: delete outputs + Clean target + Rebuild target
        print("  Clean build requested - removing all outputs")
        import shutil
        for out_dir in [BUILD_DIR / 'obj' / 'Release', BUILD_DIR / 'bin',
                        PROJECT_DIR / 'bin' / 'Release']:
            if out_dir.exists():
                shutil.rmtree(out_dir, ignore_errors=True)
                print(f"    Removed: {out_dir}")

        # Run Clean + Rebuild in one MSBuild call
        cmd = [MSBUILD, str(proj), '/p:Configuration=Release', '/p:Platform=Win32',
               '/t:Clean;Rebuild', '/nologo', '/v:minimal', '/m']
        print(f"  MSBuild Release|Win32 (clean+rebuild)")
    else:
        cmd = [MSBUILD, str(proj), '/p:Configuration=Release', '/p:Platform=Win32',
               '/nologo', '/v:minimal', '/m']
        print(f"  MSBuild Release|Win32 (incremental)")

    result = subprocess.run(cmd, capture_output=True, text=True,
                           cwd=str(PROJECT_DIR), env=build_env)

    # Check for env collision error - auto-retry with fresh cache
    env_collision = 'PROGRAMW6432' in result.stdout or 'ProgramW6432' in result.stdout
    if env_collision and retry_on_env_error:
        print("  ENV COLLISION DETECTED - resetting cache and retrying...")
        get_vs_env_cached(force_refresh=True)
        timer.end()
        return build(clean=clean, retry_on_env_error=False)

    # Check for errors
    has_error = False
    for line in result.stdout.split('\n'):
        s = line.strip()
        if 'error ' in s.lower() and 'copy' not in s.lower():
            print(f"  ERROR: {s}")
            has_error = True

    if has_error:
        print("  Last output lines:")
        for l in result.stdout.split('\n')[-10:]:
            if l.strip():
                print(f"    {l.strip()}")
        timer.end()
        return False

    # Check output
    dll_path = GAME_DIR / 'skygfx.dll'
    if dll_path.exists():
        print(f"  Output: {dll_path} ({dll_path.stat().st_size:,} bytes)")
        timer.end()
        return True

    alt = PROJECT_DIR / 'bin' / 'Release' / 'skygfx.dll'
    if alt.exists():
        print(f"  Output: {alt} ({alt.stat().st_size:,} bytes)")
        timer.end()
        return True

    print("  WARNING: Output DLL not found")
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
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler

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
    fast = '--fast' in args
    rebuild = '--rebuild' in args
    shaders_only = '--shaders' in args
    deploy_only = '--deploy' in args
    launch = '--launch' in args
    watch = '--watch' in args
    clean = '--clean' in args or rebuild
    force = '--force' in args
    reset_env = '--reset-env' in args
    open_dir = '--open' in args
    no_deploy = '--no-deploy' in args

    print("=" * 50)
    print("  SkyGFX Plus - Fast Build")
    print("=" * 50)

    if reset_env:
        reset_env_cache()
        if not any([fast, rebuild, shaders_only, deploy_only, watch, clean]):
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

    # Fast mode: skip checks, just compile + build + deploy
    if not fast and not rebuild:
        timer.begin("Pre-flight Checks")
        for name, path in [('fxc', FXC), ('msbuild', MSBUILD), ('vcvarsall', VCVARSALL)]:
            if not os.path.exists(path):
                print(f"  MISS: {name}")
                sys.exit(1)
        print("  SDKs OK")
        timer.end()

    # Skip build if sources unchanged (unless clean/rebuild)
    if not clean and not rebuild and not sources_changed():
        print("\n  No source changes detected - skipping build")
        if not no_deploy:
            deploy(force=force)
        if launch:
            launch_game()
        if open_dir:
            subprocess.Popen(['explorer', str(GAME_DIR)])
        timer.summary()
        return

    # Compile shaders in parallel
    if not compile_shaders_parallel():
        print("\n  SHADER COMPILATION FAILED")
        sys.exit(1)

    # Build
    if not build(clean=clean):
        print("\n  BUILD FAILED")
        sys.exit(1)

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
