#!/usr/bin/env python3
"""
Build fix script for skygfx_plus
Handles common build issues like missing SDK paths, merge conflicts, duplicate symbols
"""

import os
import re
import subprocess
import sys

PROJECT_DIR = r'E:\dev(dave)\skygfx_plus_expIV'
BUILD_DIR = os.path.join(PROJECT_DIR, 'build')

# SDK paths
SDKS = {
    'imgui': r'E:\SDKs\imgui-master',
    'imgui_backends': r'E:\SDKs\imgui-master\backends',
    'plugin_sdk': r'E:\SDKs\plugin-sdk-master\plugin_sa',
    'normalmap': r'E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources',
    'gta_sa_re': r'E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source',
    'msvc': r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.16.27023',
    'windows_sdk': r'C:\Program Files (x86)\Windows Kits\10',
    'dxsdk': r'C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)',
}

def find_merge_conflicts():
    """Find and list merge conflicts in source files"""
    print("\n=== Checking for merge conflicts ===")
    conflicts = []
    for root, dirs, files in os.walk(os.path.join(PROJECT_DIR, 'src')):
        for f in files:
            if f.endswith(('.cpp', '.h', '.rc')):
                path = os.path.join(root, f)
                with open(path, 'r', errors='ignore') as file:
                    content = file.read()
                    if '<<<<<<< HEAD' in content or '>>>>>>>' in content:
                        conflicts.append(path)
                        print(f"  CONFLICT: {path}")
    if not conflicts:
        print("  No merge conflicts found")
    return conflicts

def fix_merge_conflicts():
    """Fix merge conflicts by keeping HEAD version"""
    conflicts = find_merge_conflicts()
    for path in conflicts:
        print(f"  Fixing: {path}")
        with open(path, 'r', errors='ignore') as f:
            content = f.read()
        
        # Remove conflict markers, keep HEAD version
        pattern = r'<<<<<<< HEAD\n(.*?)\n=======\n.*?\n>>>>>>> master'
        content = re.sub(pattern, r'\1', content, flags=re.DOTALL)
        
        with open(path, 'w') as f:
            f.write(content)
        print(f"  Fixed: {path}")

def check_missing_files():
    """Check for missing source files in vcxproj"""
    print("\n=== Checking for missing source files ===")
    vcxproj = os.path.join(BUILD_DIR, 'skygfx.vcxproj')
    with open(vcxproj, 'r') as f:
        content = f.read()
    
    # Check required source files
    required_files = [
        'main.cpp', 'postfx.cpp', 'pipelinecommon.cpp', 'vehiclePipe.cpp',
        'buildingPipe.cpp', 'debugmenu_ui.cpp', 'neoCarpipe.cpp', 'neo.cpp',
        'gta.cpp', 'config.cpp', 'normalmap.cpp', 'envmap.cpp'
    ]
    
    for f in required_files:
        if f in content:
            print(f"  OK: {f}")
        else:
            print(f"  MISSING: {f}")

def check_include_paths():
    """Check and fix include paths in vcxproj"""
    print("\n=== Checking include paths ===")
    vcxproj = os.path.join(BUILD_DIR, 'skygfx.vcxproj')
    with open(vcxproj, 'r') as f:
        content = f.read()
    
    required_paths = [
        SDKS['imgui'],
        SDKS['imgui_backends'],
        SDKS['plugin_sdk'],
        SDKS['normalmap'],
        SDKS['gta_sa_re'],
    ]
    
    for path in required_paths:
        if path in content:
            print(f"  OK: {os.path.basename(path)}")
        else:
            print(f"  MISSING: {os.path.basename(path)}")

def setup_build_environment():
    """Setup build environment variables"""
    print("\n=== Setting up build environment ===")
    
    sdk_base = os.path.join(SDKS['windows_sdk'], 'Include', '10.0.26100.0')
    msvc_base = SDKS['msvc']
    dxsdk = SDKS['dxsdk']
    
    include_paths = [
        os.path.join(msvc_base, 'include'),
        os.path.join(sdk_base, 'ucrt'),
        os.path.join(sdk_base, 'shared'),
        os.path.join(sdk_base, 'um'),
        os.path.join(dxsdk, 'Include'),
    ]
    
    sdk_lib = os.path.join(SDKS['windows_sdk'], 'Lib', '10.0.26100.0')
    lib_paths = [
        os.path.join(msvc_base, 'lib', 'x86'),
        os.path.join(sdk_lib, 'ucrt', 'x86'),
        os.path.join(sdk_lib, 'um', 'x86'),
        os.path.join(dxsdk, 'Lib', 'x86'),
    ]
    
    sdk_bin = os.path.join(SDKS['windows_sdk'], 'bin', '10.0.26100.0', 'x86')
    
    os.environ['INCLUDE'] = ';'.join(include_paths)
    os.environ['LIB'] = ';'.join(lib_paths)
    os.environ['PATH'] = os.path.join(msvc_base, 'bin', 'HostX86', 'x86') + ';' + sdk_bin + ';' + os.environ.get('PATH', '')
    
    print("  Environment configured")
    return True

def build():
    """Build the project"""
    print("\n=== Building project ===")
    msbuild = r'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
    proj = os.path.join(BUILD_DIR, 'skygfx.vcxproj')
    
    result = subprocess.run(
        [msbuild, proj, '/p:Configuration=Release', '/p:Platform=Win32', '/nologo', '/v:minimal'],
        capture_output=True, text=True, cwd=PROJECT_DIR
    )
    
    lines = result.stdout.split('\n')
    errors = [l.strip() for l in lines if 'error' in l.lower() and 'copy' not in l.lower()]
    
    if errors:
        print("  Build errors:")
        for e in errors[:10]:
            print(f"    {e}")
        return False
    else:
        for l in lines:
            if 'skygfx.dll' in l:
                print(f"  {l.strip()}")
                break
        return True

def main():
    print("=== SkyGFX Plus Build Fix Script ===")
    
    # Fix merge conflicts
    fix_merge_conflicts()
    
    # Check missing files
    check_missing_files()
    
    # Check include paths
    check_include_paths()
    
    # Setup environment and build
    if setup_build_environment():
        success = build()
        if success:
            print("\n=== Build successful! ===")
        else:
            print("\n=== Build failed ===")
    else:
        print("\n=== Failed to setup build environment ===")

if __name__ == '__main__':
    main()
