@echo off
setlocal enabledelayedexpansion

call "%~dp0\env_common.bat"
if errorlevel 1 exit /b 1

set "PROJECT_DIR=%~dp0..\.."
set BUILD_TYPE=Release
set VERBOSE=--output-on-failure

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
if /i "%1"=="-q" (
    set VERBOSE=
    shift
    goto parse_args
)
shift
goto parse_args
:end_parse

echo ========================================
echo Running OAuth2 Tests (Dual-Config)
echo ========================================
echo Build Type: %BUILD_TYPE%

if not exist "%PROJECT_DIR%\%BUILD_DIR%" (
    echo [Error] Build directory not found. Please run build.bat first.
    exit /b 1
)

set "TEST_WORK_DIR=%PROJECT_DIR%\%BUILD_DIR%\%OAUTH2_SERVER_DIR%\test\%BUILD_TYPE%"

cd /d "%PROJECT_DIR%\%BUILD_DIR%"

REM --- Run 1: Standard config.json ---
echo.
echo [1/2] Running tests with standard %CONFIG_FILE%...
ctest -V -C %BUILD_TYPE% %VERBOSE%
if !errorlevel! neq 0 (
    echo [FAIL] Tests failed with standard %CONFIG_FILE%
    exit /b 1
)
echo [PASS] Standard config tests successful.

REM --- Run 2: config.ci.json ---
echo.
echo [2/2] Running tests with %CONFIG_CI_FILE%...
if not exist "%PROJECT_DIR%\%OAUTH2_SERVER_DIR%\%CONFIG_CI_FILE%" (
    echo [SKIP] %CONFIG_CI_FILE% not found, skipping second run.
    goto done
)

if not exist "%TEST_WORK_DIR%" (
    echo [Error] Test work dir not found: %TEST_WORK_DIR%
    exit /b 1
)

REM Backup original and use CI config
copy /Y "%TEST_WORK_DIR%\%CONFIG_FILE%" "%TEST_WORK_DIR%\%CONFIG_FILE%.bak" >nul
copy /Y "%PROJECT_DIR%\%OAUTH2_SERVER_DIR%\%CONFIG_CI_FILE%" "%TEST_WORK_DIR%\%CONFIG_FILE%" >nul

ctest -V -C %BUILD_TYPE% %VERBOSE%
set "CI_EXIT=!errorlevel!"

REM Restore original config immediately
copy /Y "%TEST_WORK_DIR%\%CONFIG_FILE%.bak" "%TEST_WORK_DIR%\%CONFIG_FILE%" >nul
del "%TEST_WORK_DIR%\%CONFIG_FILE%.bak" >nul 2>&1

if !CI_EXIT! neq 0 (
    echo [FAIL] Tests failed with %CONFIG_CI_FILE%
    exit /b 1
)
echo [PASS] CI config tests successful.

:done
echo.
echo ========================================
echo All test runs completed successfully
echo ========================================

endlocal
exit /b 0
