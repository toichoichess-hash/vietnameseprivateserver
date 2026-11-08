@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo           Building Roblox Fast Launcher (Release x64)
echo ======================================================================

set "VS_PATH="
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
)

if "%VS_PATH%"=="" (
    echo [!] Error: Visual Studio compiler (vcvarsall.bat) not found.
    echo Please install Visual Studio with "Desktop development with C++".
    pause
    exit /b 1
)

call "%VS_PATH%" x64 >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Failed to initialize MSVC x64 build environment.
    pause
    exit /b 1
)

if not exist bin mkdir bin

echo [*] Compiling Windows resource file...
rc.exe /nologo /fo "bin\resource.res" "src\resource.rc"

echo [*] Compiling C++ source code...
cl.exe /nologo /O2 /W3 /DNDEBUG /EHsc /std:c++17 /Fe:"bin\RobloxLauncher.exe" /Fo:"bin\main.obj" "src\main.cpp" "bin\resource.res" /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO user32.lib gdi32.lib comctl32.lib dwmapi.lib shell32.lib

if %errorlevel% equ 0 (
    echo.
    echo ======================================================================
    echo [✓] Build successful!
    echo Output: bin\RobloxLauncher.exe
    echo ======================================================================
) else (
    echo.
    echo [!] Build failed!
)

pause
