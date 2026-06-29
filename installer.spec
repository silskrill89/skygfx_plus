# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for the GTA SAS 1987 Installer.

Build with:
    pyinstaller installer.spec
"""
from PyInstaller.utils.hooks import collect_all

block_cipher = None

datas = [
    ('src/resources', 'resources'),
    ('data', 'data'),
    ('fonts', 'fonts'),
    ('src/ui/splash', 'splash'),
    ('INSTALLER_GUIDE.pdf', '.'),
    ('CREDITS.md', '.'),
]
binaries = []
hiddenimports = ['py7zr', 'rarfile', 'bs4', 'PyQt5', 'PyQt5.QtWidgets', 'PyQt5.QtGui', 'PyQt5.QtCore']

# Collect everything PyQt5 needs
tmp_d, tmp_b, tmp_h = collect_all('PyQt5')
datas += tmp_d
binaries += tmp_b
hiddenimports += tmp_h

a = Analysis(
    ['installer.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['tkinter', 'matplotlib', 'numpy', 'pandas'],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='GTA_SAS_1987_Installer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='src/resources/icon.ico' if __import__('os').path.exists('src/resources/icon.ico') else None,
)
