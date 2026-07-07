@echo off
setlocal enabledelayedexpansion

call "%~dp0\env_setup.bat"
if %errorlevel% neq 0 exit /b 1

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

REM Check for Conan environment script
if exist "%PROJECT_DIR%\%BUILD_DIR%\conanrun.bat" (
    call "%PROJECT_DIR%\%BUILD_DIR%\conanrun.bat"
) else (
    echo [Warning] conanrun.bat not found in build directory.
)

set "EXE_PATH=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%\%SERVER_BINARY_NAME%.exe"
if exist "%EXE_PATH%" (
    echo Starting %SERVER_BINARY_NAME% (%BUILD_TYPE%)
    cd /d "%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%"
    %SERVER_BINARY_NAME%.exe
) else (
    echo [Error] %SERVER_BINARY_NAME%.exe not found at %EXE_PATH%.
    echo Please run build.bat first.
    exit /b 1
)
endlocal
