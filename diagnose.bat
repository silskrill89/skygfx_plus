@echo off
REM =====================================================================
REM  GTA SAS 1987 Installer - Diagnostic Tool
REM
REM  Run this if run.bat fails. It prints diagnostic info that helps
REM  identify the problem. Copy the output and share it when asking
REM  for help.
REM =====================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ============================================
echo  GTA SAS 1987 Installer - Diagnostics
echo ============================================
echo.

echo === System info ===
ver
echo.

echo === Scanning for Python installs ===
echo.

REM Check PATH
echo --- On PATH (where python) ---
where python 2>nul
echo.

echo --- Python launcher (where py) ---
where py 2>nul
echo.

REM Check common install folders
echo --- Common install folders ---
set "FOUND_COUNT=0"
for %%P in (
    "C:\Python3*"
    "C:\Program Files\Python3*"
    "C:\Program Files (x86)\Python3*"
) do (
    for %%F in (%%P) do (
        if exist "%%F\python.exe" (
            echo   [found] %%F\python.exe
            set /a FOUND_COUNT+=1
        )
    )
)
if exist "%LOCALAPPDATA%\Programs\Python" (
    for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
        if exist "%%D\python.exe" (
            echo   [found] %%D\python.exe
            set /a FOUND_COUNT+=1
        )
    )
)
if "!FOUND_COUNT!"=="0" echo   ^(none found in common folders^)
echo.

REM Check Conda
echo --- Conda / Anaconda / Miniconda ---
set "FOUND_COUNT=0"
for %%P in (
    "%LOCALAPPDATA%\Anaconda3"
    "%LOCALAPPDATA%\miniconda3"
    "C:\ProgramData\Anaconda3"
    "C:\ProgramData\miniconda3"
    "C:\Anaconda3"
    "C:\miniconda3"
) do (
    if exist "%%~P\python.exe" (
        echo   [found] %%~P\python.exe
        set /a FOUND_COUNT+=1
    )
)
if exist "%LOCALAPPDATA%\Anaconda3\envs" (
    for /d %%D in ("%LOCALAPPDATA%\Anaconda3\envs\*") do (
        if exist "%%D\python.exe" (
            echo   [found] %%D\python.exe ^(conda env: %%~nxD^)
            set /a FOUND_COUNT+=1
        )
    )
)
if exist "%LOCALAPPDATA%\miniconda3\envs" (
    for /d %%D in ("%LOCALAPPDATA%\miniconda3\envs\*") do (
        if exist "%%D\python.exe" (
            echo   [found] %%D\python.exe ^(conda env: %%~nxD^)
            set /a FOUND_COUNT+=1
        )
    )
)
if "!FOUND_COUNT!"=="0" echo   ^(no conda installs found^)
echo.

REM Check Microsoft Store real Python (not stub)
echo --- Microsoft Store real Python ---
set "FOUND_COUNT=0"
if exist "%LOCALAPPDATA%\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3*" (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WindowsApps\PythonSoftwareFoundation.Python.3*") do (
        if exist "%%D\python.exe" (
            echo   [found] %%D\python.exe
            set /a FOUND_COUNT+=1
        )
    )
)
if "!FOUND_COUNT!"=="0" echo   ^(no Microsoft Store real Python found^)
echo.

REM Check registry
echo --- Registry Python installs ---
set "FOUND_COUNT=0"
for %%K in (HKCU HKLM) do (
    for %%R in ("Software\Python\PythonCore" "Software\WOW6432Node\Python\PythonCore") do (
        for /f "tokens=*" %%V in ('reg query "%%K\%%R" 2^>nul') do (
            for /f "tokens=2,*" %%A in ('reg query "%%K\%%R\%%V\InstallPath" /ve 2^>nul') do (
                if exist "%%Bpython.exe" (
                    echo   [found] %%Bpython.exe  ^(registry: %%K\%%R\%%V^)
                    set /a FOUND_COUNT+=1
                )
            )
        )
    )
)
if "!FOUND_COUNT!"=="0" echo   ^(no registry Python installs found^)
echo.

echo === Python version test ===
python --version 2>nul
if errorlevel 1 echo   python --version FAILED
py --version 2>nul
if errorlevel 1 echo   py --version FAILED
echo.

echo === pip availability ===
python -m pip --version 2>nul
if errorlevel 1 echo   python -m pip FAILED
py -m pip --version 2>nul
if errorlevel 1 echo   py -m pip FAILED
echo.

echo === Try importing each dependency ===
for %%p in (PyQt5 requests bs4 py7zr rarfile psutil) do (
    python -c "import %%p; print(f'  %%p: OK')" 2>nul
    if errorlevel 1 echo   %%p: MISSING
)
echo.

echo === Network connectivity ===
ping -n 2 pypi.org >nul 2>&1
if errorlevel 1 (
    echo   CANNOT reach pypi.org
) else (
    echo   pypi.org reachable
)
echo.

echo === Crash logs in this folder ===
if exist "%~dp0crash_*.log" (
    dir "%~dp0crash_*.log"
    echo.
    echo --- Contents of most recent crash log ---
    for /f "delims=" %%F in ('dir /b /o-d "%~dp0crash_*.log" 2^>nul') do (
        echo Reading: %%F
        type "%~dp0%%F"
        goto :done_crash
    )
    :done_crash
) else (
    echo   No crash logs found in %~dp0
)
echo.

echo === wizard_output.log (if any) ===
if exist "%~dp0wizard_output.log" (
    echo --- Last 50 lines of wizard_output.log ---
    powershell -NoProfile -Command "Get-Content '%~dp0wizard_output.log' -Tail 50"
) else (
    echo   No wizard_output.log found
)
echo.

echo === Install logs in cache/logs/ ===
if exist "%~dp0cache\logs" (
    dir "%~dp0cache\logs\*.log"
) else (
    echo   No cache/logs/ folder yet
)
echo.

echo ============================================
echo  End of diagnostics. Copy everything above
echo  and share it when asking for help.
echo ============================================
pause
