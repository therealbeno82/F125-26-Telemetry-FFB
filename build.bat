@echo off
setlocal EnableDelayedExpansion
title F1 FFB Build

echo ============================================================
echo   F1 FFB -- C++ Build Script
echo ============================================================
echo.

:: ── Find CMake ───────────────────────────────────────────────────────────────
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found.
    echo.
    echo Please install CMake from: https://cmake.org/download/
    echo Make sure to tick "Add CMake to PATH" during install.
    echo Then re-run this script.
    echo.
    pause
    exit /b 1
)
echo [OK] CMake found: 
cmake --version | head -n 1

:: ── Find MSVC (via vswhere) ───────────────────────────────────────────────────
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

set VS_PATH=
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VS_PATH=%%i
    )
)

if "!VS_PATH!"=="" (
    echo [WARN] Visual Studio with C++ tools not found.
    echo.
    echo Please install one of:
    echo   A) Visual Studio 2022 Community (free):
    echo      https://visualstudio.microsoft.com/
    echo      During install, select "Desktop development with C++"
    echo.
    echo   B) Build Tools for Visual Studio 2022 (smaller, no IDE):
    echo      https://aka.ms/vs/17/release/vs_BuildTools.exe
    echo      Select "C++ build tools" workload
    echo.
    echo After installing, re-run this script.
    pause
    exit /b 1
)
echo [OK] Visual Studio found at: !VS_PATH!

:: ── Configure ─────────────────────────────────────────────────────────────────
echo.
echo [1/2] Configuring with CMake...
if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_BUILD_TYPE=Release ^
         -DFETCHCONTENT_QUIET=ON

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] CMake configure failed.
    echo Check the output above for details.
    cd ..
    pause
    exit /b 1
)

:: ── Build ─────────────────────────────────────────────────────────────────────
echo.
echo [2/2] Building (this may take a few minutes first time)...
cmake --build . --config Release --parallel

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed. Check output above.
    cd ..
    pause
    exit /b 1
)

cd ..

:: ── Done ──────────────────────────────────────────────────────────────────────
echo.
echo ============================================================
echo   Build successful!
echo   Run:  F1FFB.exe
echo ============================================================
echo.
echo Starting F1FFB.exe...
start "" F1FFB.exe
