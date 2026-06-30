#!/usr/bin/env python3
"""
SkyGFX Plus - Build Script
Auto-detects SDK paths, compiles shaders, builds project, deploys to game.
Run: python tools/fix_build.py
"""

import os
import re
import subprocess
import sys
import glob as globmod
import shutil

PROJECT_DIR = r'E:\dev(dave)\skygfx_plus_expIV'
BUILD_DIR = os.path.join(PROJECT_DIR, 'build')
GAME_DIR = r'E:\games\San Andreas Retro Revised'

# ============================================================
# SDK Auto-Detection
# ============================================================

def find_latest_msvc():
    """Find latest MSVC toolset installed"""
    msvc_root = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC'
    if not os.path.isdir(msvc_root):
        print(f"  WARNING: MSVC root not found: {msvc_root}")
        return None
    versions = sorted(os.listdir(msvc_root), reverse=True)
    for v in versions:
        path = os.path.join(msvc_root, v, 'bin', 'HostX86', 'x86', 'cl.exe')
        if os.path.exists(path):
            print(f"  MSVC: {v}")
            return os.path.join(msvc_root, v)
    if versions:
        print(f"  MSVC: {versions[0]} (fallback)")
        return os.path.join(msvc_root, versions[0])
    return None

def find_latest_winsdk():
    """Find latest Windows SDK with um/d3d9helper.h"""
    sdk_root = r'C:\Program Files (x86)\Windows Kits\10\Include'
    if not os.path.isdir(sdk_root):
        print(f"  WARNING: Windows SDK root not found: {sdk_root}")
        return None
    versions = sorted(os.listdir(sdk_root), reverse=True)
    for v in versions:
        if os.path.exists(os.path.join(sdk_root, v, 'um', 'd3d9helper.h')):
            print(f"  Windows SDK: {v}")
            return v
    if versions:
        print(f"  Windows SDK: {versions[0]} (fallback, no d3d9helper.h)")
        return versions[0]
    return None

SDKS = {
    'imgui':       r'E:\SDKs\imgui-master',
    'imgui_be':    r'E:\SDKs\imgui-master\backends',
    'plugin_sdk':  r'E:\SDKs\plugin-sdk-master\plugin_sa',
    'normalmap':   r'E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources',
    'gta_sa_re':   r'E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source',
    'dxsdk':       r'C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)',
    'msbuild':     r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe',
}

# Auto-detect MSVC and Windows SDK
MSVC_PATH = find_latest_msvc()
WINSDK_VER = find_latest_winsdk()
WINSDK_ROOT = r'C:\Program Files (x86)\Windows Kits\10'

REQUIRED_SDK_PATHS = {
    'imgui':       ('imgui.h', r'E:\SDKs\imgui-master\imgui.h'),
    'imgui_be':    ('imgui_impl_dx9.h', r'E:\SDKs\imgui-master\backends\imgui_impl_dx9.h'),
    'plugin_sdk':  ('plugin_sa.h', r'E:\SDKs\plugin-sdk-master\plugin_sa\plugin_sa.h'),
    'normalmap':   ('sources', r'E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources'),
    'gta_sa_re':   ('GTA SA RE', r'E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source'),
    'injector':    ('injector.hpp', os.path.join(PROJECT_DIR, r'external\injector\include\injector\injector.hpp')),
    'rw37':        ('rwcore.h', r'E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9\rwcore.h'),
    'dxsdk':       ('d3d9.h', os.path.join(SDKS['dxsdk'], 'Include', 'd3d9.h')),
    'd3dx9':       ('d3dx9.h', os.path.join(SDKS['dxsdk'], 'Include', 'd3dx9.h')),
    'fxc':         ('fxc.exe', os.path.join(SDKS['dxsdk'], 'Utilities', 'bin', 'x86', 'fxc.exe')),
    'msbuild':     ('MSBuild.exe', SDKS['msbuild']),
    'd3d9helper':  ('d3d9helper.h', os.path.join(WINSDK_ROOT, 'Include', WINSDK_VER or '10.0.26100.0', 'um', 'd3d9helper.h')) if WINSDK_VER else None,
}

# ============================================================
# Pre-flight checks
# ============================================================

def check_sdks():
    """Verify all required SDK paths exist"""
    print("\n=== Checking SDK dependencies ===")
    all_ok = True
    for name, info in REQUIRED_SDK_PATHS.items():
        if info is None:
            print(f"  SKIP: {name} (auto-detect failed)")
            continue
        label, path = info
        if os.path.exists(path):
            print(f"  OK:   {name} -> {label}")
        else:
            print(f"  MISS: {name} -> {path}")
            all_ok = False
    return all_ok

# ============================================================
# Merge conflict detection
# ============================================================

def check_merge_conflicts():
    """Find merge conflicts in source files"""
    print("\n=== Checking for merge conflicts ===")
    conflicts = []
    for root, dirs, files in os.walk(os.path.join(PROJECT_DIR, 'src')):
        for f in files:
            if f.endswith(('.cpp', '.h', '.rc')):
                path = os.path.join(root, f)
                with open(path, 'r', errors='ignore') as fh:
                    content = fh.read()
                    if '<<<<<<< HEAD' in content or '>>>>>>>' in content:
                        conflicts.append(path)
                        print(f"  CONFLICT: {path}")
    if not conflicts:
        print("  No merge conflicts found")
    return conflicts

# ============================================================
# Shader compilation
# ============================================================

def compile_shaders():
    """Compile all HLSL shaders to CSO files"""
    print("\n=== Compiling shaders ===")
    fxc = os.path.join(SDKS['dxsdk'], 'Utilities', 'bin', 'x86', 'fxc.exe')
    if not os.path.exists(fxc):
        print(f"  FAIL: fxc.exe not found at {fxc}")
        return False

    shaders_dir = os.path.join(PROJECT_DIR, 'shaders')
    cso_dir = os.path.join(PROJECT_DIR, 'resources', 'cso')
    os.makedirs(cso_dir, exist_ok=True)

    compiled = 0
    failed = 0
    skipped = 0

    # Compile ps/ and vs/ directories
    for subdir, profile in [('ps', 'ps_3_0'), ('vs', 'vs_3_0')]:
        shader_dir = os.path.join(shaders_dir, subdir)
        if not os.path.exists(shader_dir):
            continue
        for root, dirs, files in os.walk(shader_dir):
            for f in files:
                if not f.endswith('.hlsl'):
                    continue
                # Skip GTAIV VS shaders (use vs_main entrypoint, not main)
                if subdir == 'vs' and 'GTAIV' in f:
                    continue
                hlsl_path = os.path.join(root, f)
                cso_name = os.path.splitext(f)[0] + '.cso'
                cso_path = os.path.join(cso_dir, cso_name)

                # Skip if CSO is newer than HLSL
                if os.path.exists(cso_path):
                    if os.path.getmtime(cso_path) >= os.path.getmtime(hlsl_path):
                        skipped += 1
                        continue

                result = subprocess.run(
                    [fxc, '/T', profile, '/nologo', '/E', 'main', '/Fo', cso_path, hlsl_path],
                    capture_output=True, text=True
                )
                if result.returncode == 0:
                    compiled += 1
                    print(f"  OK:   {subdir}/{f}")
                else:
                    failed += 1
                    print(f"  FAIL: {subdir}/{f}")
                    for line in result.stderr.strip().split('\n')[:3]:
                        print(f"        {line}")

    # Also compile subdirectory shaders (ps/2_a/)
    sub_ps = os.path.join(shaders_dir, 'ps', '2_a')
    if os.path.exists(sub_ps):
        for f in os.listdir(sub_ps):
            if not f.endswith('.hlsl'):
                continue
            hlsl_path = os.path.join(sub_ps, f)
            cso_name = os.path.splitext(f)[0] + '.cso'
            cso_path = os.path.join(cso_dir, cso_name)

            if os.path.exists(cso_path):
                if os.path.getmtime(cso_path) >= os.path.getmtime(hlsl_path):
                    skipped += 1
                    continue

            result = subprocess.run(
                [fxc, '/T', 'ps_3_0', '/nologo', '/E', 'main', '/Fo', cso_path, hlsl_path],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                compiled += 1
                print(f"  OK:   ps/2_a/{f}")
            else:
                failed += 1
                print(f"  FAIL: ps/2_a/{f}")
                for line in result.stderr.strip().split('\n')[:3]:
                    print(f"        {line}")

    print(f"  Shaders: {compiled} compiled, {failed} failed, {skipped} up-to-date")
    return failed == 0

# ============================================================
# MSBuild
# ============================================================

def get_vs_env():
    """Get environment from vcvarsall.bat for x86 build"""
    vcvarsall = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
    if not os.path.exists(vcvarsall):
        print(f"  WARNING: vcvarsall.bat not found at {vcvarsall}")
        return None

    # Run vcvarsall.bat and capture the environment
    cmd = f'"{vcvarsall}" x86 && set'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  WARNING: vcvarsall.bat failed")
        return None

    env = {}
    for line in result.stdout.splitlines():
        if '=' in line:
            key, _, value = line.partition('=')
            env[key.upper()] = value
    return env

def build():
    """Build project via MSBuild with VS environment"""
    print("\n=== Building project ===")
    msbuild = SDKS['msbuild']
    if not os.path.exists(msbuild):
        print(f"  FAIL: MSBuild not found at {msbuild}")
        return False

    proj = os.path.join(BUILD_DIR, 'skygfx.vcxproj')
    if not os.path.exists(proj):
        print(f"  FAIL: Project file not found at {proj}")
        return False

    # Get VS environment for cl.exe
    vs_env = get_vs_env()
    if not vs_env:
        print("  WARNING: Could not get VS environment, trying MSBuild directly")

    # Build env dict for subprocess
    build_env = os.environ.copy()
    if vs_env:
        build_env.update(vs_env)

    cmd = [msbuild, proj, '/p:Configuration=Release', '/p:Platform=Win32', '/nologo', '/v:minimal']
    print(f"  Running: MSBuild Release|Win32")

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=PROJECT_DIR, env=build_env)

    # Parse output
    lines = result.stdout.split('\n')
    error_lines = []
    success_line = None
    for line in lines:
        stripped = line.strip()
        if 'error ' in stripped.lower() and 'copy' not in stripped.lower():
            error_lines.append(stripped)
        if 'skygfx.dll' in stripped and 'error' not in stripped.lower():
            success_line = stripped
        if 'Build succeeded' in stripped:
            success_line = stripped

    if error_lines:
        print("  Build ERRORS:")
        for e in error_lines[:15]:
            print(f"    {e}")
        print("  Last lines:")
        for l in lines[-10:]:
            if l.strip():
                print(f"    {l.strip()}")
        return False

    if success_line:
        print(f"  {success_line}")

    # Verify output DLL exists
    dll_path = os.path.join(GAME_DIR, 'skygfx.dll')
    if os.path.exists(dll_path):
        size = os.path.getsize(dll_path)
        print(f"  Output: {dll_path} ({size:,} bytes)")
        return True
    else:
        print(f"  WARNING: Output DLL not found at {dll_path}")
        alt = os.path.join(GAME_DIR, 'skygfx.asi')
        if os.path.exists(alt):
            size = os.path.getsize(alt)
            print(f"  Found ASI: {alt} ({size:,} bytes)")
            return True
        return False

# ============================================================
# Deploy
# ============================================================

def deploy():
    """Copy ASI + INI to game directory"""
    print("\n=== Deploying to game ===")

    # Find ASI
    asi_candidates = [
        os.path.join(GAME_DIR, 'skygfx.asi'),
        os.path.join(GAME_DIR, 'skygfx.dll'),
        os.path.join(PROJECT_DIR, 'bin', 'Release', 'skygfx.dll'),
    ]
    asi_src = None
    for c in asi_candidates:
        if os.path.exists(c):
            asi_src = c
            break

    # Find INI
    ini_candidates = [
        os.path.join(PROJECT_DIR, 'bin', 'Release', 'skygfx.ini'),
        os.path.join(GAME_DIR, 'skygfx.ini'),
    ]
    ini_src = None
    for c in ini_candidates:
        if os.path.exists(c):
            ini_src = c
            break

    asi_dst = os.path.join(GAME_DIR, 'skygfx.asi')
    ini_dst = os.path.join(GAME_DIR, 'skygfx.ini')

    if asi_src:
        if os.path.normpath(asi_src) != os.path.normpath(asi_dst):
            shutil.copy2(asi_src, asi_dst)
            print(f"  ASI: {asi_src} -> {asi_dst}")
        else:
            print(f"  ASI: already at {asi_dst}")
    else:
        print("  WARNING: No ASI found to deploy")

    if ini_src:
        if os.path.normpath(ini_src) != os.path.normpath(ini_dst):
            shutil.copy2(ini_src, ini_dst)
            print(f"  INI: {ini_src} -> {ini_dst}")
        else:
            print(f"  INI: already at {ini_dst}")
    else:
        print("  WARNING: No INI found to deploy")

# ============================================================
# Main
# ============================================================

def main():
    print("=" * 60)
    print("  SkyGFX Plus - Build Script")
    print("=" * 60)

    # 1. Check SDKs
    check_sdks()

    # 2. Check merge conflicts
    check_merge_conflicts()

    # 3. Compile shaders
    shaders_ok = compile_shaders()

    # 4. Build
    build_ok = build()

    # 5. Deploy
    if build_ok:
        deploy()
        print("\n" + "=" * 60)
        print("  BUILD SUCCESSFUL")
        print("=" * 60)
    else:
        print("\n" + "=" * 60)
        print("  BUILD FAILED - check errors above")
        print("=" * 60)
        sys.exit(1)

if __name__ == '__main__':
    main()
