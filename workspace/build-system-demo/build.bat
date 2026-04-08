@echo off
REM Source Forest Build System Entry
REM Simulates game engine build.bat

setlocal EnableDelayedExpansion

REM Default configuration
set "CONFIG_FILE=configs/full_build.json"
set "BUILD_DIR=build"
set "NOPAUSE=0"

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_parse
if "%~1"=="--config" (
    set "CONFIG_FILE=%~2"
    shift
    shift
    goto :parse_args
)
if "%~1"=="--build-dir" (
    set "BUILD_DIR=%~2"
    shift
    shift
    goto :parse_args
)
if "%~1"=="--nopause" (
    set "NOPAUSE=1"
    shift
    goto :parse_args
)
echo Unknown option: %~1
exit /b 1

:done_parse

echo ========================================
echo   Source Forest Build System Demo
echo ========================================
echo.

REM Step 1: Generate Source Tree (setup temporary kitchen)
echo [Step 1] Generating Source Tree...
echo   Config: %CONFIG_FILE%
python launch/generator.py --config "%CONFIG_FILE%" --output "%BUILD_DIR%/sourcetree"
if errorlevel 1 (
    echo [Error] Failed to generate source tree
    goto :error
)
echo.

REM Step 2: Run CMake configuration (prepare cooking)
echo [Step 2] Configuring with CMake...
cmake -S "%BUILD_DIR%/sourcetree" -B "%BUILD_DIR%"
if errorlevel 1 (
    echo [Error] CMake configuration failed
    goto :error
)
echo.

REM Step 3: Build (start cooking)
echo [Step 3] Building...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo [Error] Build failed
    goto :error
)
echo.

echo ========================================
echo   Build Completed Successfully!
echo ========================================
echo.
echo Output: %BUILD_DIR%/bin/

if exist "%BUILD_DIR%/bin/GameEngine.exe" (
    echo.
    echo Run: %BUILD_DIR%/bin/GameEngine.exe
)
if exist "%BUILD_DIR%/bin/EngineCore.exe" (
    echo.
    echo Run: %BUILD_DIR%/bin/EngineCore.exe
)

goto :end

:error
echo.
echo [Build Failed]
exit /b 1

:end
if "%NOPAUSE%"=="0" (
    echo.
    pause
)
endlocal
