@echo off
REM =====================================================================
REM  Build a truly portable GTA SAS 1987 Installer folder.
REM
REM  This produces a single self-contained folder that can be zipped and
REM  shared - no Python install required on the target machine.
REM
REM  Output: .\dist\GTA_SAS_1987_Installer_Portable\
REM =====================================================================
setlocal
cd /d "%~dp0"

echo.
echo ============================================
echo  Building portable installer folder
echo ============================================
echo.

REM --- 1. Find a real Python interpreter (skip Windows Store stub) ---
set "PY_EXE="

for /f "delims=" %%i in ('where python 2^>nul') do (
    echo %%i | findstr /i /c:"WindowsApps" >nul || (
        set "PY_EXE=%%i"
        goto :found_python
    )
)

where py >nul 2>&1
if %errorlevel%==0 (
    set "PY_EXE=py"
    goto :found_python
)

echo [ERROR] Python 3.10+ is not installed ^(or only the Windows Store stub is present^).
echo Install real Python from https://www.python.org/downloads/
echo Make sure to tick "Add Python to PATH" during install.
pause
exit /b 1

:found_python
echo Using Python: %PY_EXE%

REM Verify Python actually works (the stub returns nothing)
%PY_EXE% -c "import sys; print(f'Python {sys.version}')" 2>nul
if errorlevel 1 (
    echo [ERROR] Python at "%PY_EXE%" did not respond. Install real Python from
    echo https://www.python.org/downloads/ ^(tick "Add Python to PATH"^).
    pause
    exit /b 1
)

REM --- 2. Ensure PyInstaller + deps ---
echo Installing dependencies...
%PY_EXE% -m pip install --upgrade pip >nul
%PY_EXE% -m pip install -r requirements.txt
%PY_EXE% -m pip install "pyinstaller>=6.0"

if errorlevel 1 (
    echo [ERROR] Failed to install dependencies.
    pause
    exit /b 1
)

REM --- 3. Clean previous build ---
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist

REM --- 4. Run PyInstaller (single-file .exe) ---
echo Building .exe with PyInstaller...
%PY_EXE% -m PyInstaller ^
    --noconfirm ^
    --onefile ^
    --windowed ^
    --name "GTA_SAS_1987_Installer" ^
    --add-data "data;data" ^
    --add-data "fonts;fonts" ^
    --add-data "src/ui/splash;splash" ^
    --add-data "INSTALLER_GUIDE.pdf;." ^
    --add-data "CREDITS.md;." ^
    --hidden-import "py7zr" ^
    --hidden-import "rarfile" ^
    --hidden-import "bs4" ^
    --collect-all "PyQt5" ^
    installer.py

if errorlevel 1 (
    echo [ERROR] PyInstaller build failed.
    pause
    exit /b 1
)

REM --- 5. Assemble portable folder ---
set "OUT=dist\GTA_SAS_1987_Installer_Portable"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"

copy "dist\GTA_SAS_1987_Installer.exe" "%OUT%\"
copy "README.md" "%OUT%\"
copy "CREDITS.md" "%OUT%\"
copy "INSTALLER_GUIDE.pdf" "%OUT%\"
copy "requirements.txt" "%OUT%\"
copy "run.bat" "%OUT%\"
copy "diagnose.bat" "%OUT%\"
xcopy /E /I /Y "data" "%OUT%\data" >nul
xcopy /E /I /Y "fonts" "%OUT%\fonts" >nul

echo.
echo ============================================
echo  Portable folder ready:
echo  %CD%\%OUT%
echo.
echo  Zip it up and share. Run GTA_SAS_1987_Installer.exe
echo  on any Windows 10/11 machine - no install needed.
echo ============================================
pause
