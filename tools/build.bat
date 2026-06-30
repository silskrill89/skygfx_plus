@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
MSBuild.exe "E:\dev(dave)\skygfx_plus_expIV\build\skygfx.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m /nologo > "E:\dev(dave)\skygfx_plus_expIV\tools\build_log.txt" 2>&1
echo EXIT_CODE=%ERRORLEVEL% >> "E:\dev(dave)\skygfx_plus_expIV\tools\build_log.txt"
