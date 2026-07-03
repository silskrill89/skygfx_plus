@echo off
REM SkyGFX Plus - Parallel Shader Compiler
REM Delegates to fast_build.py --shaders for parallel compilation
REM All 68 shaders compiled with multiprocessing (16 workers)

setlocal
cd /d E:\dev(dave)\skygfx_plus_expIV
python tools\fast_build.py --shaders
endlocal
