#!/usr/bin/env python3
"""
SkyGFX Plus - Prototype Iteration Helper
Quick commands for fast prototyping and testing.

Usage:
  python tools/proto.py shader <name>      # Compile single shader, deploy, no build
  python tools/proto.py code               # Build C++ only (skip shaders), deploy
  python tools/proto.py ini                # Deploy INI only
  python tools/proto.py restart            # Kill game, rebuild, deploy, launch
  python tools/proto.py kill               # Kill running game
  python tools/proto.py log                # Tail debug log
  python tools/proto.py diff <file>        # Show git diff for file
"""

import os
import sys
import time
import shutil
import subprocess
from pathlib import Path

PROJECT_DIR = Path(r'E:\dev(dave)\skygfx_plus_expIV')
GAME_DIR = Path(r'E:\games\gtasa_skygfx_plus')
SHADERS_DIR = PROJECT_DIR / 'shaders'
CSO_DIR = PROJECT_DIR / 'resources' / 'cso'
BUILD_DIR = PROJECT_DIR / 'build'

FXC = r'C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe'
MSBUILD = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
VCVARSALL = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'

def kill_game():
    """Kill running GTA SA process."""
    try:
        subprocess.run(['taskkill', '/F', '/IM', 'gta_sa.exe'],
                      capture_output=True, text=True, timeout=5)
        print("Game killed")
    except:
        pass

def is_game_running():
    try:
        result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq gta_sa.exe'],
                              capture_output=True, text=True, timeout=5)
        return 'gta_sa.exe' in result.stdout
    except:
        return False

def compile_single_shader(name):
    """Compile a single shader by name (without extension)."""
    # Search in ps/ and vs/
    for subdir, profile in [('ps', 'ps_3_0'), ('vs', 'vs_3_0')]:
        hlsl = SHADERS_DIR / subdir / f'{name}.hlsl'
        if hlsl.exists():
            cso = CSO_DIR / f'{name}.cso'
            print(f"Compiling {hlsl} -> {cso}")
            result = subprocess.run(
                [FXC, '/T', profile, '/nologo', '/E', 'main', '/Fo', str(cso), str(hlsl)],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                print(f"  OK ({cso.stat().st_size:,} bytes)")
                return True
            else:
                print(f"  FAIL: {result.stderr.strip()}")
                return False

    # Try root-level (vehiclePipeVS.hlsl etc)
    hlsl = SHADERS_DIR / f'{name}.hlsl'
    if hlsl.exists():
        cso = CSO_DIR / f'{name}.cso'
        print(f"Compiling {hlsl} (root) -> {cso}")
        result = subprocess.run(
            [FXC, '/T', 'ps_3_0', '/nologo', '/E', 'main', '/Fo', str(cso), str(hlsl)],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print(f"  OK ({cso.stat().st_size:,} bytes)")
            return True
        else:
            print(f"  FAIL: {result.stderr.strip()}")
            return False

    print(f"Shader not found: {name}")
    return False

def build_code_only():
    """Build C++ only, skip shader compilation."""
    print("Building (C++ only, no shaders)...")

    # Get VS env
    cmd = f'"{VCVARSALL}" x86 && set'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if '=' in line:
            key, _, value = line.partition('=')
            env[key] = value

    proj = BUILD_DIR / 'skygfx.vcxproj'
    cmd = [MSBUILD, str(proj), '/p:Configuration=Release', '/p:Platform=Win32',
           '/nologo', '/v:minimal', '/m']
    result = subprocess.run(cmd, capture_output=True, text=True,
                           cwd=str(PROJECT_DIR), env=env)

    for line in result.stdout.split('\n'):
        s = line.strip()
        if 'error ' in s.lower() and 'copy' not in s.lower():
            print(f"  ERROR: {s}")

    dll = GAME_DIR / 'skygfx.dll'
    if dll.exists():
        print(f"  Build OK ({dll.stat().st_size:,} bytes)")
        return True
    print("  Build FAILED")
    return False

def deploy_asi():
    """Deploy ASI to game directory."""
    dll = GAME_DIR / 'skygfx.dll'
    asi = GAME_DIR / 'skygfx.asi'

    if not dll.exists():
        print("No DLL found")
        return False

    # Backup
    if asi.exists():
        backup_dir = PROJECT_DIR / 'backups'
        backup_dir.mkdir(exist_ok=True)
        ts = time.strftime('%Y%m%d_%H%M%S')
        shutil.copy2(asi, backup_dir / f'skygfx_{ts}.asi')

    shutil.copy2(dll, asi)
    print(f"  Deployed ASI ({asi.stat().st_size:,} bytes)")
    return True

def deploy_ini():
    """Deploy INI only."""
    ini = PROJECT_DIR / 'bin' / 'Release' / 'skygfx.ini'
    if ini.exists():
        shutil.copy2(ini, GAME_DIR / 'skygfx.ini')
        print("  Deployed INI")
    else:
        print("  No INI found")

def launch():
    """Launch game."""
    exe = GAME_DIR / 'gta_sa.exe'
    if exe.exists():
        print(f"  Launching game...")
        subprocess.Popen([str(exe)], cwd=str(GAME_DIR))
    else:
        print(f"  {exe} not found")

def restart_cycle():
    """Kill game, rebuild, deploy, launch - full iteration cycle."""
    print("=== Restart Cycle ===")
    kill_game()
    time.sleep(1)

    if build_code_only():
        deploy_asi()
        deploy_ini()
        launch()
        print("=== Restart complete ===")
    else:
        print("=== Build failed, not launching ===")

def tail_log():
    """Tail the debug log."""
    log = GAME_DIR / 'skygfx_new_dbg.log'
    if not log.exists():
        print(f"  {log} not found")
        return

    print(f"Tailing {log} (Ctrl+C to stop)...")
    try:
        with open(log, 'r') as f:
            f.seek(0, 2)  # Seek to end
            while True:
                line = f.readline()
                if line:
                    print(line.rstrip())
                else:
                    time.sleep(0.1)
    except KeyboardInterrupt:
        print("\nStopped")

def git_diff(filepath):
    """Show git diff for a file."""
    result = subprocess.run(
        ['git', 'diff', '--', filepath],
        capture_output=True, text=True, cwd=str(PROJECT_DIR)
    )
    print(result.stdout if result.stdout else "No changes")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    cmd = sys.argv[1]

    if cmd == 'shader' and len(sys.argv) > 2:
        compile_single_shader(sys.argv[2])
    elif cmd == 'code':
        build_code_only()
    elif cmd == 'ini':
        deploy_ini()
    elif cmd == 'restart':
        restart_cycle()
    elif cmd == 'kill':
        kill_game()
    elif cmd == 'launch':
        launch()
    elif cmd == 'log':
        tail_log()
    elif cmd == 'diff' and len(sys.argv) > 2:
        git_diff(sys.argv[2])
    else:
        print(__doc__)

if __name__ == '__main__':
    main()
