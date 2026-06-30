@echo off
REM =====================================================================
REM  GTA San Andreas Stories 1987 - Installer Wizard (portable launcher)
REM
REM  This launcher:
REM    1. Scans Windows for any Python 3.10+ install (not just PATH)
REM    2. Verifies the candidate actually runs (skips Windows Store stub)
REM    3. Bootstraps pip via ensurepip if missing
REM    4. Installs dependencies with full visible output (3 fallbacks)
REM    5. Launches the wizard with stdout+stderr captured to wizard_output.log
REM
REM  If the wizard crashes, look for:
REM    wizard_output.log   — full stdout/stderr from the wizard run
REM    crash_*.log         — Python traceback (if the wizard caught it)
REM    cache/logs/install_*.log — install log (if logging started)
REM =====================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ============================================
echo  GTA SAS 1987 Installer - Portable Launcher
echo ============================================
echo.

REM --- 1. Find a Python interpreter ---
set "PY_EXE="

REM 1a. Prefer a bundled python.exe (place one in .\python\python.exe to skip detection)
if exist ".\python\python.exe" (
    set "PY_EXE=.\python\python.exe"
    echo [found] Bundled Python: !PY_EXE!
    goto :verify_python
)

echo Scanning for Python installs...
echo.

REM 1b. Try `where python` (PATH) — but skip the Windows Store stub
for /f "delims=" %%i in ('where python 2^>nul') do (
    echo %%i | findstr /i /c:"WindowsApps" >nul && (
        echo   [skip] Windows Store stub: %%i
    ) || (
        set "PY_EXE=%%i"
        echo   [found] On PATH: !PY_EXE!
        goto :verify_python
    )
)

REM 1c. Try `py` launcher (official Python launcher, always non-stub)
for /f "delims=" %%i in ('where py 2^>nul') do (
    set "PY_EXE=py"
    echo   [found] Python launcher: !PY_EXE!
    goto :verify_python
)

REM 1d. Scan common install locations
echo   Scanning common install folders...

REM Check C:\Python* and C:\Program Files\Python*
for %%P in (
    "C:\Python3*"
    "C:\Program Files\Python3*"
    "C:\Program Files (x86)\Python3*"
) do (
    for %%F in (%%P) do (
        if exist "%%F\python.exe" (
            set "PY_EXE=%%F\python.exe"
            echo   [found] Install folder: !PY_EXE!
            goto :verify_python
        )
    )
)

REM Check per-user install: %LOCALAPPDATA%\Programs\Python\Python3*
if exist "%LOCALAPPDATA%\Programs\Python" (
    for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
        if exist "%%D\python.exe" (
            set "PY_EXE=%%D\python.exe"
            echo   [found] Per-user install: !PY_EXE!
            goto :verify_python
        )
    )
)

REM Check Anaconda / Miniconda installs
for %%P in (
    "%LOCALAPPDATA%\Anaconda3"
    "%LOCALAPPDATA%\miniconda3"
    "C:\ProgramData\Anaconda3"
    "C:\ProgramData\miniconda3"
    "C:\Anaconda3"
    "C:\miniconda3"
) do (
    if exist "%%~P\python.exe" (
        set "PY_EXE=%%~P\python.exe"
        echo   [found] Conda install: !PY_EXE!
        goto :verify_python
    )
)

REM Check common conda envs locations
if exist "%LOCALAPPDATA%\Anaconda3\envs" (
    for /d %%D in ("%LOCALAPPDATA%\Anaconda3\envs\*") do (
        if exist "%%D\python.exe" (
            set "PY_EXE=%%D\python.exe"
            echo   [found] Conda env: !PY_EXE!
            goto :verify_python
        )
    )
)
if exist "%LOCALAPPDATA%\miniconda3\envs" (
    for /d %%D in ("%LOCALAPPDATA%\miniconda3\envs\*") do (
        if exist "%%D\python.exe" (
            set "PY_EXE=%%D\python.exe"
            echo   [found] Conda env: !PY_EXE!
            goto :verify_python
        )
    )
)

REM Check Microsoft Store real install (not the stub)
if exist "%LOCALAPPDATA%\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3*" (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3*") do (
        if exist "%%D\python.exe" (
            set "PY_EXE=%%D\python.exe"
            echo   [found] Microsoft Store real install: !PY_EXE!
            goto :verify_python
        )
    )
)

REM 1e. Query the registry for Python install paths
echo   Scanning registry...
for %%K in (HKCU HKLM) do (
    for %%R in ("Software\Python\PythonCore" "Software\WOW6432Node\Python\PythonCore") do (
        for /f "tokens=*" %%V in ('reg query "%%K\%%R" 2^>nul') do (
            for /f "tokens=2,*" %%A in ('reg query "%%K\%%R\%%V\InstallPath" /ve 2^>nul') do (
                if exist "%%Bpython.exe" (
                    set "PY_EXE=%%Bpython.exe"
                    echo   [found] Registry: !PY_EXE!
                    goto :verify_python
                )
            )
        )
    )
)

echo.
echo [ERROR] No Python 3.10+ installation found on this PC.
echo.
echo Scanned locations:
echo   - PATH ^(where python^)
echo   - Python launcher ^(where py^)
echo   - C:\Python3*
echo   - C:\Program Files\Python3* and ^(x86^)
echo   - %LOCALAPPDATA%\Programs\Python\Python3*
echo   - Anaconda3 / Miniconda3 ^(system + per-user + envs^)
echo   - Microsoft Store real Python install
echo   - Registry: HKCU/HKLM\Software\Python\PythonCore\*\InstallPath
echo.
echo Install real Python from https://www.python.org/downloads/
echo ^(tick "Add Python to PATH" on the first installer page^)
echo.
pause
exit /b 1

:verify_python
echo.
echo Using Python: %PY_EXE%

REM --- 2. Verify Python actually runs (skip stubs that just open Store) ---
echo Verifying Python responds...
%PY_EXE% -c "import sys; print(f'  Python {sys.version}'); print(f'  Executable: {sys.executable}')" 2>nul
if errorlevel 1 (
    echo.
    echo [ERROR] Python at "%PY_EXE%" did not respond. It may be the Windows Store stub.
    echo Install real Python from https://www.python.org/downloads/
    pause
    exit /b 1
)

REM --- 3. Verify pip is installed (bootstrap via ensurepip if missing) ---
echo.
echo Checking pip...
%PY_EXE% -m pip --version >nul 2>&1
if errorlevel 1 (
    echo   pip not found. Bootstrapping via ensurepip...
    %PY_EXE% -m ensurepip --upgrade
    if errorlevel 1 (
        echo.
        echo [ERROR] Could not bootstrap pip.
        echo Download get-pip.py from https://bootstrap.pypa.io/get-pip.py
        echo Then run: %PY_EXE% get-pip.py
        pause
        exit /b 1
    )
)
echo   pip OK.

REM --- 4. Check if dependencies are already installed ---
echo.
echo Checking dependencies...
%PY_EXE% -c "import PyQt5, requests, bs4" 2>nul
if not errorlevel 1 (
    echo   All dependencies already installed.
    goto :launch
)

REM --- 5. Install dependencies (3 fallback strategies, full visible output) ---
echo.
echo ============================================
echo  Installing dependencies (one-time setup)
echo  This may take 2-5 minutes on first run.
echo  Full output is shown below.
echo ============================================
echo.

echo --- Attempt 1: standard pip install ---
%PY_EXE% -m pip install --disable-pip-version-check -r requirements.txt
if not errorlevel 1 goto :deps_ok

echo.
echo --- Attempt 1 failed. Trying with --user flag ^(permission issue?^) ---
%PY_EXE% -m pip install --user --disable-pip-version-check -r requirements.txt
if not errorlevel 1 goto :deps_ok

echo.
echo --- Attempt 2 failed. Trying with --no-cache-dir ^(corrupted cache?^) ---
%PY_EXE% -m pip install --user --no-cache-dir --disable-pip-version-check -r requirements.txt
if not errorlevel 1 goto :deps_ok

echo.
echo ============================================
echo  [ERROR] All dependency install attempts failed.
echo.
echo  Common causes:
echo    1. No internet connection
echo    2. Corporate firewall blocking pip
echo    3. Outdated pip — try: %PY_EXE% -m pip install --upgrade pip
echo    4. Python version too old — need 3.10+
echo.
echo  Manual install:
echo    %PY_EXE% -m pip install -r requirements.txt
echo.
echo  Or run diagnose.bat for more info.
echo ============================================
pause
exit /b 1

:deps_ok
echo.
echo Dependencies installed successfully.

:launch
REM --- 6. Launch the wizard with stdout+stderr captured to wizard_output.log ---
echo.
echo ============================================
echo  Launching wizard...
echo  Output is being logged to: %~dp0wizard_output.log
echo ============================================
echo.

REM Truncate the log file (start fresh each run)
echo GTA SAS 1987 Installer - Wizard Output Log > wizard_output.log
echo Started: %DATE% %TIME% >> wizard_output.log
echo Python: %PY_EXE% >> wizard_output.log
echo ============================================ >> wizard_output.log
echo. >> wizard_output.log

REM Run the wizard with stderr merged into stdout, teeing to the log file.
REM We use a PowerShell one-liner for tee because cmd has no native tee.
powershell -NoProfile -Command "& { %PY_EXE% '%~dp0installer.py' 2>&1 | Tee-Object -FilePath '%~dp0wizard_output.log' -Append }"

set EXITCODE=%errorlevel%
echo.
echo ============================================
echo  Wizard exited with code %EXITCODE%
echo.
if exist "%~dp0crash_*.log" (
    echo  [!] Crash log(s) found:
    dir /b "%~dp0crash_*.log" 2>nul
    echo  Please share these files when reporting the bug.
) else (
    echo  No crash logs found.
)
echo.
echo  Full output saved to: %~dp0wizard_output.log
echo  Install logs saved to: %~dp0cache\logs\
echo ============================================
pause
exit /b %EXITCODE%
