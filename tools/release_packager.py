#!/usr/bin/env python3
"""
SkyGFX Plus - Release Packager
Creates versioned distributable zip files with DLL, INI, and optional content.

Usage:
  python tools/release_packager.py                  # Package current build
  python tools/release_packager.py --with-wheels    # Include packed wheels DFF
  python tools/release_packager.py --with-bridge    # Include 64-bit bridge
  python tools/release_packager.py --wheels-only    # Stripped wheels-only mod
  python tools/release_packager.py --clean          # Clean old releases
  python tools/release_packager.py --list           # List all releases
  python tools/release_packager.py --version 1.0.0  # Custom version
"""

import os
import sys
import shutil
import zipfile
import json
import time
import hashlib
import struct
from pathlib import Path
from datetime import datetime

# ============================================================
# Paths
# ============================================================

PROJECT_DIR = Path(r'E:\dev(dave)\skygfx_plus_expIV')
BIN_DIR = PROJECT_DIR / 'bin' / 'Release'
GAME_DIR = Path(r'E:\games\gtasa_skygfx_plus')
RELEASE_DIR = PROJECT_DIR / 'release'
WHEELS_SOURCE_DIR = GAME_DIR / 'models' / 'wheels'
RESOURCES_DIR = PROJECT_DIR / 'resources'

RELEASE_DIR.mkdir(exist_ok=True)

# ============================================================
# Version
# ============================================================

def get_version():
    """Read version from premake5.lua"""
    premake = PROJECT_DIR / 'premake5.lua'
    if premake.exists():
        with open(premake, 'r') as f:
            content = f.read()
        # Find rsc_FileVersion("X.X.X.X")
        import re
        m = re.search(r'rsc_FileVersion\s*=\s*"([^"]+)"', content)
        if m:
            v = m.group(1)
            # Convert 4.2.0.0 to 4.2.0
            parts = v.split('.')
            return '.'.join(parts[:3])
    return '1.0.0'

# ============================================================
# Wheels Packer
# ============================================================

def pack_wheels(verbose=True):
    """
    Pack individual wheel DFFs into a single binary archive.
    
    Format:
      Header: 'SWPK' (4 bytes) + count (4 bytes LE)
      Entries: [size (4 bytes LE) + data] repeated
      Footer: offset table (count * 4 bytes) + table_offset (4 bytes LE)
    
    The mod reads this at init and unpacks into memory.
    This reduces file I/O from N opens to 1 open.
    """
    if not WHEELS_SOURCE_DIR.exists():
        if verbose: print(f"  No wheels at {WHEELS_SOURCE_DIR}")
        return None
    
    wheel_files = sorted(WHEELS_SOURCE_DIR.glob('*.dff'))
    if not wheel_files:
        if verbose: print("  No wheel DFFs found")
        return None
    
    RESOURCES_DIR.mkdir(exist_ok=True)
    packed_path = RESOURCES_DIR / 'wheels_packed.bin'
    
    if verbose: print(f"  Packing {len(wheel_files)} wheel DFFs...")
    
    with open(packed_path, 'wb') as out:
        # Header
        out.write(b'SWPK')  # Magic: Sky Wheels PacK
        out.write(struct.pack('<I', len(wheel_files)))
        
        # Write each wheel with size prefix
        offsets = []
        for wheel in wheel_files:
            offsets.append(out.tell())
            data = wheel.read_bytes()
            out.write(struct.pack('<I', len(data)))
            out.write(data)
        
        # Footer: offset table
        table_offset = out.tell()
        for off in offsets:
            out.write(struct.pack('<I', off))
        out.write(struct.pack('<I', table_offset))
    
    if verbose: print(f"  Packed: {packed_path.name} ({packed_path.stat().st_size // 1024} KB)")
    return packed_path

# ============================================================
# File Utilities
# ============================================================

def get_file_hash(filepath):
    """Get SHA256 hash of file"""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            sha256.update(chunk)
    return sha256.hexdigest()[:16]

def get_dll_size():
    dll = BIN_DIR / 'skygfx.dll'
    return dll.stat().st_size // 1024 if dll.exists() else 0

# ============================================================
# Release Builder
# ============================================================

def create_zip(source_dir, zip_path):
    """Create zip from directory"""
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for f in sorted(source_dir.rglob('*')):
            if f.is_file():
                zf.write(f, f.relative_to(source_dir))

def create_readme(release_path, version, has_wheels, has_bridge, wheels_only):
    """Create README"""
    title = "SkyGFX Plus - Wheels Only Mod" if wheels_only else "SkyGFX Plus"
    
    readme = f"""# {title} v{version}

{"Stripped wheels-only mod for GTA San Andreas." if wheels_only else "Advanced PBR rendering mod for GTA San Andreas."}

## Installation

1. Copy `skygfx.asi` to your GTA San Andreas directory
2. Copy `skygfx.ini` to your GTA San Andreas directory
"""
    if has_wheels:
        readme += "3. Place `wheels_packed.bin` in your GTA San Andreas `models/wheels/` directory\n"
    if has_bridge:
        readme += "4. Copy `skygfx_bridge.asi` for 64-bit bridge support\n"
    
    readme += """
## Configuration

Edit `skygfx.ini`:
- `pipeline` - PBR, PS2, Xbox, Mobile, GTAIV
- `qualityPreset` - 0=LOW, 1=MEDIUM, 2=HIGH, 3=ULTRA

## Support

GitHub: https://github.com/aap/skygfx
"""
    (release_path / 'README.md').write_text(readme)

def build_release(version=None, include_wheels=False, include_bridge=False, 
                 wheels_only=False, clean=False):
    """Build a release zip"""
    
    if clean:
        clean_old_releases()
    
    if version is None:
        version = get_version()
    
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = '-wheels-only' if wheels_only else ''
    release_name = f"SkyGFX-Plus-v{version}{suffix}_{timestamp}"
    release_path = RELEASE_DIR / release_name
    release_path.mkdir(exist_ok=True)
    
    print(f"\n{'='*60}")
    print(f"  Building: {release_name}")
    print(f"{'='*60}")
    print(f"  DLL size: {get_dll_size()} KB")
    
    # Main ASI (renamed DLL)
    src = BIN_DIR / 'skygfx.dll'
    if src.exists():
        dst = release_path / 'skygfx.asi'
        shutil.copy2(src, dst)
        print(f"  + skygfx.asi ({dst.stat().st_size // 1024} KB)")
    else:
        print("  ! skygfx.dll not found in bin/Release!")
        return None
    
    # INI config
    ini_src = BIN_DIR / 'skygfx.ini'
    if ini_src.exists():
        shutil.copy2(ini_src, release_path / 'skygfx.ini')
        print(f"  + skygfx.ini")
    
    # Bridge (optional)
    if include_bridge:
        bridge_src = BIN_DIR / 'skygfx_bridge.dll'
        if bridge_src.exists():
            shutil.copy2(bridge_src, release_path / 'skygfx_bridge.asi')
            print(f"  + skygfx_bridge.asi (64-bit bridge)")
        else:
            print("  ! skygfx_bridge.dll not found")
    
    # Wheels (optional)
    if include_wheels:
        packed = pack_wheels()
        if packed and packed.exists():
            shutil.copy2(packed, release_path / 'wheels_packed.bin')
            print(f"  + wheels_packed.bin ({packed.stat().st_size // 1024} KB)")
        else:
            print("  ! No wheels to pack")
    
    # README
    create_readme(release_path, version, include_wheels, include_bridge, wheels_only)
    
    # Create zip
    zip_path = RELEASE_DIR / f"{release_name}.zip"
    print(f"\n  Creating zip: {zip_path.name}")
    create_zip(release_path, zip_path)
    
    # Cleanup
    shutil.rmtree(release_path)
    
    # Save info
    info = {
        'name': release_name,
        'version': version,
        'date': timestamp,
        'zip': zip_path.name,
        'size_kb': zip_path.stat().st_size // 1024,
        'dll_hash': get_file_hash(BIN_DIR / 'skygfx.dll') if (BIN_DIR / 'skygfx.dll').exists() else None,
        'includes': {
            'wheels': include_wheels,
            'bridge': include_bridge,
            'wheels_only': wheels_only,
        },
    }
    info_path = RELEASE_DIR / f"{release_name}.json"
    with open(info_path, 'w') as f:
        json.dump(info, f, indent=2)
    
    print(f"\n{'='*60}")
    print(f"  RELEASE READY")
    print(f"{'='*60}")
    print(f"  Zip:    {zip_path}")
    print(f"  Size:   {zip_path.stat().st_size // 1024} KB")
    print(f"  Info:   {info_path}")
    print(f"{'='*60}\n")
    
    return zip_path

def clean_old_releases(keep=5):
    """Remove old release zips"""
    zips = sorted(RELEASE_DIR.glob('SkyGFX-Plus-v*.zip'),
                  key=lambda p: p.stat().st_mtime, reverse=True)
    
    for old in zips[keep:]:
        print(f"  Removing: {old.name}")
        old.unlink()
        jp = old.with_suffix('.json')
        if jp.exists():
            jp.unlink()

def list_releases():
    """List all releases"""
    if not RELEASE_DIR.exists():
        print("No releases yet")
        return
    
    zips = sorted(RELEASE_DIR.glob('SkyGFX-Plus-v*.zip'), reverse=True)
    if not zips:
        print("No releases yet")
        return
    
    print(f"\n{'='*60}")
    print(f"  {len(zips)} release(s)")
    print(f"{'='*60}")
    for zp in zips:
        jp = zp.with_suffix('.json')
        if jp.exists():
            with open(jp) as f:
                info = json.load(f)
            print(f"  v{info['version']} | {info['date']} | {info['size_kb']} KB | {zp.name}")
        else:
            print(f"  {zp.name} ({zp.stat().st_size // 1024} KB)")

# ============================================================
# Main
# ============================================================

def main():
    args = sys.argv[1:]
    
    if '--clean' in args:
        clean_old_releases()
        return
    
    if '--list' in args:
        list_releases()
        return
    
    version = None
    for i, arg in enumerate(args):
        if arg == '--version' and i + 1 < len(args):
            version = args[i + 1]
    
    include_wheels = '--with-wheels' in args
    include_bridge = '--with-bridge' in args
    wheels_only = '--wheels-only' in args
    clean = '--clean' in args
    
    build_release(
        version=version,
        include_wheels=include_wheels,
        include_bridge=include_bridge,
        wheels_only=wheels_only,
        clean=clean,
    )

if __name__ == '__main__':
    main()
