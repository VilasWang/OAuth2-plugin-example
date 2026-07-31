@echo off
setlocal enabledelayedexpansion

call "%~dp0\env_setup.bat"
if %errorlevel% neq 0 exit /b 1

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%~dp0..\.."
set BUILD_TYPE=Release

:parse_args
if "%1"=="" goto end_parse
if /i "%1"=="-debug" (
    set BUILD_TYPE=Debug
    shift
    goto parse_args
)
if /i "%1"=="-release" (
    set BUILD_TYPE=Release
    shift
    goto parse_args
)
shift
goto parse_args
:end_parse

REM Map the build configuration to its Conan-installed CMakePresets.json
REM preset directory (build/<preset>), matching what build.bat produced.
call "%SCRIPT_DIR%resolve_preset.bat" %BUILD_TYPE%
set "PRESET_DIR=%PROJECT_DIR%\%BUILD_DIR%\%CMAKE_PRESET%"

REM Check for Conan environment script
if exist "%PRESET_DIR%\conanrun.bat" (
    call "%PRESET_DIR%\conanrun.bat"
) else (
    echo [Warning] conanrun.bat not found in build directory.
)

set "EXE_PATH=%PRESET_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%\%SERVER_BINARY_NAME%.exe"
if exist "%EXE_PATH%" (
    echo Starting %SERVER_BINARY_NAME% (%BUILD_TYPE%)
    cd /d "%PRESET_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%"
    %SERVER_BINARY_NAME%.exe
) else (
    echo [Error] %SERVER_BINARY_NAME%.exe not found at %EXE_PATH%.
    echo Please run build.bat first.
    exit /b 1
)
endlocal
