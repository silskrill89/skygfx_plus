REM SkyGfx Plus expIV Build Script
REM Compile the Release version with normal map plugin support

REM Set environment variables
SET RWSDK37_PATH=E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk
SET DXSDK_PATH=C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)
SET SKYGFX_PROJECT=E:\dev\dave\skygfx_plus_expIV\build\skygfx.vcxproj

REM Create build directory if it doesn't exist
if not exist "build" mkdir build

REM Compile with Visual Studio's latest
REM For Windows, we can try using cl.exe directly

REM Change to skygfx_plus_expIV directory
cd /d E:\dev\dave\skygfx_plus_expIV

REM Show current configuration
whereis cl.exe

REM Check for Visual Studio installation
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe" (
    SET CL_EXE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe"
) else (
    SET CL_EXE=cl.exe
)

REM Show compile status
echo.
echo Looking for compiler...
echo CL_EXE=%CL_EXE%
echo.

REM Try to compile a simple test file to see if we have the compiler
test_compile.cpp
int main() { return 0; }