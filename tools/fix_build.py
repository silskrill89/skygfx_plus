#!/usr/bin/env python3
"""
SkyGFX Plus - Build Script / Bug-Fix Checklist
Auto-detects SDK paths, compiles shaders, builds project, deploys to game.

Run: python tools/fix_build.py

PBR build bug notes (add new ones here as we hit them):
  1. vcxproj ImportLibrary must NOT point to "E:\games\San Andreas Retro Revised\skygfx.lib".
     That path is a stale leftover; .lib/.exp should go to OutDir/IntDir.
  2. rpnormmap.lib needs /SAFESEH:NO via <ImageHasSafeExceptionHandlers>false>,
     otherwise LNK2026/LNK1281 fails the link.
  3. src/normalmap.cpp must be compiled in vcxproj (normalmap_plugin.cpp stays off).
  4. src/normmap_stubs.cpp must ALSO be compiled; it provides the RW SDK symbols that
     rpnormmap.lib needs without linking rwcore.lib/rpworld.lib (which duplicate symbols
     already defined in gta.cpp/pipelinecommon.cpp).
  5. src/config.cpp must be COMMENTED OUT in vcxproj; main.cpp owns INI reading now.
  6. src/brdfLibrary.h must exist and define CryEngine-style Diffuse/Specular/Gloss BRDFs.
  7. vehiclePipe.cpp, buildingPipe.cpp, veh_shaders.cpp, main.cpp must exist and
     reference the unified BRDF / surface-detection helpers.
  8. PBR c22/c23 constant layout MUST use pipeUploadPBR() from pipelinecommon.cpp.
     NEVER upload c22/c23 manually — the order is {glossiness, specular, ...} and
     getting it backwards produces completely flat/dark car paint (roughness 0.96).
     If vehicles look flat grey with no specular, check the c22 ordering first.
"""

import os
import re
import subprocess
import sys
import glob as globmod
import shutil
import time
import json
import hashlib
from pathlib import Path
from multiprocessing import Pool, cpu_count

PROJECT_DIR = r'E:\dev(dave)\skygfx_plus_expIV'
BUILD_DIR = os.path.join(PROJECT_DIR, 'build')
GAME_DIR = r'E:\games\gtasa_skygfx_plus'
CACHE_DIR = os.path.join(PROJECT_DIR, '.cache')
os.makedirs(CACHE_DIR, exist_ok=True)

_timer_start = time.time()
_timer_phases = {}

def _t_begin(name):
    _timer_phases[name] = time.time()
    print(f"\n--- {name} ---")

def _t_end(name):
    elapsed = time.time() - _timer_phases[name]
    _timer_phases[name] = elapsed
    print(f"    ({elapsed:.2f}s)")

def _t_summary():
    total = time.time() - _timer_start
    print(f"\n{'='*50}")
    print(f"Build Summary ({total:.2f}s total):")
    for phase, t in _timer_phases.items():
        print(f"  {phase:20s} {t:6.2f}s")
    print(f"{'='*50}")

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

def check_process_conflict():
    """Check if gta_sa.exe is already running (common cause of crashes that look like hook conflicts)"""
    print("\n=== Checking for running game process ===")
    import subprocess
    try:
        result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq gta_sa.exe'],
                              capture_output=True, text=True, timeout=5)
        if 'gta_sa.exe' in result.stdout:
            lines = [l for l in result.stdout.strip().split('\n')
                    if 'gta_sa.exe' in l.lower()]
            print(f"  WARNING: gta_sa.exe is already running ({len(lines)} instance(s))!")
            print("  This causes crashes that look like MoonLoader D3D9 hook conflicts")
            print("  but are actually just the old process holding files locks.")
            print("  Kill the existing process before launching again.")
            return True
        else:
            print("  No running gta_sa.exe found - OK")
            return False
    except Exception as e:
        print(f"  Could not check: {e}")
        return False

# ============================================================
# PBR / build integrity checks (historical bug list)
# ============================================================

def check_pbr_build_integrity():
    """Check fragile settings that historically broke PBR builds."""
    print("\n=== PBR build integrity checks ===")
    issues = []
    proj_path = os.path.join(BUILD_DIR, 'skygfx.vcxproj')

    if os.path.exists(proj_path):
        with open(proj_path, 'r', errors='ignore') as fh:
            proj_lines = fh.readlines()
        proj_text = ''.join(proj_lines)

        # 1. stale ImportLibrary path
        if 'San Andreas Retro Revised' in proj_text:
            issues.append("vcxproj ImportLibrary/exp path points to 'San Andreas Retro Revised' (stale output dir)")

        # 2. SAFESEH must be disabled for rpnormmap.lib
        if 'ImageHasSafeExceptionHandlers>false' not in proj_text:
            issues.append("vcxproj missing <ImageHasSafeExceptionHandlers>false> (rpnormmap.lib SAFESEH LNK2026)")

        # 3. normalmap.cpp + normmap_stubs.cpp must be compiled
        def _is_active_cpp(name):
            for line in proj_lines:
                if name in line and f'<ClCompile Include="..\\src\\{name}"' in line:
                    return not line.strip().startswith('<!--')
            return False

        if not _is_active_cpp('normalmap.cpp'):
            issues.append("normalmap.cpp is not compiled in vcxproj")
        if not _is_active_cpp('normmap_stubs.cpp'):
            issues.append("normmap_stubs.cpp is not compiled in vcxproj (rpnormmap.lib needs it)")

        # 4. do NOT link rwcore/rpworld - they duplicate symbols from gta.cpp
        if 'rwcore.lib' in proj_text or 'rpworld.lib' in proj_text:
            issues.append("vcxproj links rwcore.lib/rpworld.lib; use normmap_stubs.cpp instead")

        # 5. both pipes must use pipeUploadPBR, not manual c22/c23 upload
        vpipe = os.path.join(PROJECT_DIR, 'src', 'vehiclePipe.cpp')
        bpipe = os.path.join(PROJECT_DIR, 'src', 'buildingPipe.cpp')
        for ppath, pname in [(vpipe, 'vehiclePipe'), (bpipe, 'buildingPipe')]:
            if os.path.exists(ppath):
                src = open(ppath, 'r', errors='ignore').read()
                if 'pipeUploadPBR' not in src:
                    issues.append(f"{pname}.cpp does not use pipeUploadPBR (manual c22/c23 upload risks param-order bug)")

        # 6. config.cpp must be commented out (main.cpp owns INI now)
        for line in proj_lines:
            if 'config.cpp' in line and '<ClCompile Include="..\\src\\config.cpp"' in line:
                if not line.strip().startswith('<!--'):
                    issues.append("config.cpp is active in vcxproj; comment it out so main.cpp reads INI")

        # 6. required PBR source files
        for f in ['brdfLibrary.h', 'vehiclePipe.cpp', 'buildingPipe.cpp',
                  'veh_shaders.cpp', 'main.cpp']:
            p = os.path.join(PROJECT_DIR, 'src', f)
            if not os.path.exists(p):
                issues.append(f"Missing {p}")

    else:
        issues.append(f"Project file not found: {proj_path}")

    if issues:
        print("  ISSUES FOUND:")
        for i in issues:
            print(f"    - {i}")
    else:
        print("  PBR build integrity OK")
    return not issues

# ============================================================
# Parallel Shader Compilation
# ============================================================

def _compile_one(args):
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
        err = result.stderr.strip().split('\n')[0] if result.stderr else 'Unknown'
        return ('fail', hlsl_path, err)

def _get_shader_list():
    """Build list of all shaders to compile."""
    shaders = []
    fxc = os.path.join(SDKS['dxsdk'], 'Utilities', 'bin', 'x86', 'fxc.exe')
    shaders_dir = os.path.join(PROJECT_DIR, 'shaders')
    cso_dir = os.path.join(PROJECT_DIR, 'resources', 'cso')

    # Single-entry shaders (ps/ and vs/)
    for subdir, profile in [('ps', 'ps_3_0'), ('vs', 'vs_3_0')]:
        shader_dir = os.path.join(shaders_dir, subdir)
        if not os.path.exists(shader_dir):
            continue
        for root, dirs, files in os.walk(shader_dir):
            for f in files:
                if not f.endswith('.hlsl'):
                    continue
                if subdir == 'vs' and 'GTAIV' in f:
                    continue
                hlsl_path = os.path.join(root, f)
                cso_path = os.path.join(cso_dir, os.path.splitext(f)[0] + '.cso')
                shaders.append((fxc, hlsl_path, cso_path, profile, 'main'))

    # Multi-entry consolidated shaders
    multi_entry = [
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_vehiclePBR', 'vehiclePBRVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_ps2CarFx', 'ps2CarFxVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_specCarFx', 'specCarFxVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_xboxCar', 'xboxCarVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_leedsCarFx', 'leedsCarFxVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_mobileVehicle', 'mobileVehicleVS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_neoPass1', 'neoVehiclePass1VS.cso', True),
        ('vs_3_0', 'vehiclePipeVS.hlsl', 'main_neoPass2', 'neoVehiclePass2VS.cso', True),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_specCarFx', 'specCarFxPS.cso', False),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_mobileVehicle', 'mobileVehiclePS.cso', False),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso', False),
        ('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_building', 'BuildingPBRPS.cso', False),
    ]
    for profile, src_file, entry, out_cso, in_root in multi_entry:
        hlsl_path = os.path.join(shaders_dir, src_file) if in_root else os.path.join(shaders_dir, 'ps', src_file)
        if not os.path.exists(hlsl_path):
            continue
        shaders.append((fxc, hlsl_path, os.path.join(cso_dir, out_cso), profile, entry))
    return shaders

def compile_shaders():
    """Compile all HLSL shaders to CSO in parallel."""
    _t_begin("Shader Compilation")
    cso_dir = os.path.join(PROJECT_DIR, 'resources', 'cso')
    os.makedirs(cso_dir, exist_ok=True)
    shaders = _get_shader_list()
    if not shaders:
        print("  No shaders found")
        _t_end("Shader Compilation")
        return True

    workers = min(cpu_count(), len(shaders))
    print(f"  Compiling {len(shaders)} shaders with {workers} workers...")
    results = {'ok': 0, 'skip': 0, 'fail': 0}
    failures = []
    with Pool(workers) as pool:
        for result in pool.imap_unordered(_compile_one, shaders):
            status = result[0]
            name = os.path.basename(result[1])
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

    # Copy pre-compiled GTAIV CSOs
    for cso_name in ['GTAIVVehicle_vs.cso', 'GTAIVVehicle_ps.cso',
                     'GTAIVBuilding_vs.cso', 'GTAIVBuilding_ps.cso']:
        for subdir in ['vs', 'ps']:
            src = os.path.join(PROJECT_DIR, 'shaders', subdir, cso_name)
            dst = os.path.join(cso_dir, cso_name)
            if os.path.exists(src) and not os.path.exists(dst):
                shutil.copy2(src, dst)
                print(f"  COPY: {cso_name}")

    print(f"  {results['ok']} compiled, {results['skip']} up-to-date, {results['fail']} failed")
    _t_end("Shader Compilation")
    return results['fail'] == 0

# ============================================================
# MSBuild
# ============================================================

def get_vs_env():
    """Get environment from vcvarsall.bat for x86 build (cached)."""
    cache_file = os.path.join(CACHE_DIR, 'vs_env.json')
    # Use cache if < 1 hour old
    if os.path.exists(cache_file):
        age = time.time() - os.path.getmtime(cache_file)
        if age < 3600:
            with open(cache_file, 'r') as f:
                return json.load(f)

    vcvarsall = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
    if not os.path.exists(vcvarsall):
        print(f"  WARNING: vcvarsall.bat not found at {vcvarsall}")
        return None
    print("  Caching VS environment...")
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
    with open(cache_file, 'w') as f:
        json.dump(env, f)
    return env

def build():
    """Build project via MSBuild with VS environment"""
    _t_begin("MSBuild")
    msbuild = SDKS['msbuild']
    if not os.path.exists(msbuild):
        print(f"  FAIL: MSBuild not found at {msbuild}")
        _t_end("MSBuild")
        return False

    proj = os.path.join(BUILD_DIR, 'skygfx.vcxproj')
    if not os.path.exists(proj):
        print(f"  FAIL: Project file not found at {proj}")
        _t_end("MSBuild")
        return False

    vs_env = get_vs_env()
    if not vs_env:
        print("  WARNING: Could not get VS environment, trying MSBuild directly")

    build_env = os.environ.copy()
    if vs_env:
        build_env.update(vs_env)
    # Deduplicate case-insensitive keys to prevent MSB6001
    deduped = {}
    seen = set()
    for k, v in build_env.items():
        kl = k.lower()
        if kl not in seen:
            seen.add(kl)
            deduped[k] = v
    build_env = deduped

    cmd = [msbuild, proj, '/p:Configuration=Release', '/p:Platform=Win32',
           '/nologo', '/v:minimal', '/m']  # /m = parallel build
    print(f"  Running: MSBuild Release|Win32 (parallel)")

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=PROJECT_DIR, env=build_env)

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
        _t_end("MSBuild")
        return False

    if success_line:
        print(f"  {success_line}")

    dll_path = os.path.join(GAME_DIR, 'skygfx.dll')
    if os.path.exists(dll_path):
        size = os.path.getsize(dll_path)
        print(f"  Output: {dll_path} ({size:,} bytes)")
        _t_end("MSBuild")
        return True
    else:
        alt = os.path.join(GAME_DIR, 'skygfx.asi')
        if os.path.exists(alt):
            size = os.path.getsize(alt)
            print(f"  Found ASI: {alt} ({size:,} bytes)")
            _t_end("MSBuild")
            return True
        _t_end("MSBuild")
        return False

# ============================================================
# Deploy
# ============================================================

def deploy():
    """Copy ASI + INI to game directory. Always overwrites ASI."""
    _t_begin("Deploy")

    dll_candidates = [
        os.path.join(GAME_DIR, 'skygfx.dll'),
        os.path.join(PROJECT_DIR, 'bin', 'Release', 'skygfx.dll'),
    ]
    dll_src = None
    for c in dll_candidates:
        if os.path.exists(c):
            dll_src = c
            break

    ini_candidates = [
        os.path.join(PROJECT_DIR, 'bin', 'Release', 'skygfx.ini'),
        os.path.join(GAME_DIR, 'skygfx.ini'),
    ]
    ini_src = None
    for c in ini_candidates:
        if os.path.exists(c):
            ini_src = c
            break

    if dll_src:
        asi_dst = os.path.join(GAME_DIR, 'skygfx.asi')
        if os.path.exists(asi_dst):
            backup_dir = os.path.join(PROJECT_DIR, 'backups')
            os.makedirs(backup_dir, exist_ok=True)
            from datetime import datetime
            ts = datetime.now().strftime('%Y%m%d_%H%M%S')
            shutil.copy2(asi_dst, os.path.join(backup_dir, f'skygfx_{ts}.asi'))
        for old in [asi_dst, asi_dst + '.disabled', asi_dst + '.dl_l']:
            if os.path.exists(old):
                os.remove(old)
        shutil.copy2(dll_src, asi_dst)
        print(f"  ASI deployed ({os.path.getsize(asi_dst):,} bytes)")

    if ini_src:
        ini_dst = os.path.join(GAME_DIR, 'skygfx.ini')
        if os.path.exists(ini_dst):
            print(f"  INI already exists in game directory — preserving user settings")
        elif os.path.abspath(ini_src) == os.path.abspath(ini_dst):
            print(f"  INI already in game directory")
        else:
            shutil.copy2(ini_src, ini_dst)
            print(f"  INI deployed (first-time copy)")

    _t_end("Deploy")

# ============================================================
# Main
# ============================================================

def main():
    print("=" * 60)
    print("  SkyGFX Plus - Build Script")
    print("=" * 60)

    # 1. Check SDKs
    _t_begin("Pre-flight")
    check_sdks()
    check_merge_conflicts()
    check_process_conflict()
    check_pbr_build_integrity()
    _t_end("Pre-flight")

    # 2. Compile shaders (parallel)
    shaders_ok = compile_shaders()

    # 3. Build (parallel MSBuild)
    build_ok = build()

    # 4. Deploy
    if build_ok:
        deploy()
        _t_summary()
        print("\n" + "=" * 60)
        print("  BUILD SUCCESSFUL")
        print("=" * 60)
    else:
        _t_summary()
        print("\n" + "=" * 60)
        print("  BUILD FAILED - check errors above")
        print("=" * 60)
        sys.exit(1)

if __name__ == '__main__':
    main()
