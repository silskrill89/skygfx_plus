@echo off
REM SkyGFX Plus - Build Wrapper
REM Usage: build [fast|shaders|deploy|launch|clean|watch]
REM Default: full build with checks

setlocal
cd /d E:\dev(dave)\skygfx_plus_expIV

if "%1"=="fast" (
    python tools\fast_build.py --fast
    goto end
)
if "%1"=="shaders" (
    python tools\fast_build.py --shaders
    goto end
)
if "%1"=="deploy" (
    python tools\fast_build.py --deploy
    goto end
)
if "%1"=="launch" (
    python tools\fast_build.py --fast --launch
    goto end
)
if "%1"=="clean" (
    python tools\fast_build.py --fast --clean
    goto end
)
if "%1"=="watch" (
    python tools\fast_build.py --watch
    goto end
)

REM Default: full build with all checks
python tools\fix_build.py

:end
endlocal
