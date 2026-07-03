@echo off
REM SkyGFX Plus - Quick Build Commands
REM Usage: b [fast|shaders|deploy|launch|watch|clean|proto]

if "%1"=="" goto full
if "%1"=="fast" goto fast
if "%1"=="shaders" goto shaders
if "%1"=="deploy" goto deploy
if "%1"=="launch" goto launch
if "%1"=="watch" goto watch
if "%1"=="clean" goto clean
if "%1"=="proto" goto proto
if "%1"=="restart" goto restart
goto full

:full
python tools\fix_build.py
goto end

:fast
python tools\fast_build.py --fast
goto end

:shaders
python tools\fast_build.py --shaders
goto end

:deploy
python tools\fast_build.py --deploy
goto end

:launch
python tools\fast_build.py --fast --launch
goto end

:watch
python tools\fast_build.py --watch
goto end

:clean
python tools\fast_build.py --fast --clean
goto end

:proto
python tools\proto.py %2 %3
goto end

:restart
python tools\proto.py restart
goto end

:end
