@echo off
REM audio-io-1.0.0 build script
REM Windows-native only, MSVC required

setlocal enabledelayedexpansion

REM Force English compiler output
set VSLANG=1033
set PreferredUILang=en-US

REM Parse arguments
set BUILD_CONFIG=Release
set CLEAN_BUILD=0

if "%1"=="clean" (
    set CLEAN_BUILD=1
    shift
)

if "%1"=="debug" (
    set BUILD_CONFIG=Debug
)

REM Clean old build if requested
if %CLEAN_BUILD%==1 (
    echo [audio-io] Cleaning old build...
    if exist build rmdir /s /q build
    if exist install rmdir /s /q install
    echo [audio-io] Clean complete.
    exit /b 0
)

echo ========================================
echo audio-io-1.0.0 Build System
echo ========================================
echo Configuration: %BUILD_CONFIG%
echo.

REM Create build and logs directories
if not exist build mkdir build
if not exist logs mkdir logs

REM Timestamp for log file
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"') do set TS=%%i
set LOG=logs\%TS%_build.log

REM Configure CMake
echo [1/3] Configuring CMake...
echo [LOG] %LOG%
cmake -B build -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_CONFIG% ^
    -DCMAKE_INSTALL_PREFIX=%CD%\install ^
    -DCMAKE_VS_GLOBALS="PreferredUILang=en-US" ^
    -DAUDIO_IO_BUILD_TESTS=ON > "%LOG%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed. Check %LOG%
    exit /b 1
)

echo [OK] CMake configured

REM Build
echo.
echo [2/3] Building audio-io...
cmake --build build --config %BUILD_CONFIG% -j %NUMBER_OF_PROCESSORS% >> "%LOG%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed. Check %LOG%
    exit /b 1
)

echo [OK] Build successful

REM Install
echo.
echo [3/3] Installing to install\...
cmake --install build --config %BUILD_CONFIG% >> "%LOG%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Installation failed. Check %LOG%
    exit /b 1
)

echo [OK] Installation successful

echo.
echo ========================================
echo BUILD COMPLETE
echo ========================================
echo Output: %CD%\install\
echo   - bin\audio-io.dll
echo   - lib\audio-io.lib
echo   - include\audio_io\*.h
echo Build Log: %LOG%
echo ========================================

exit /b 0
