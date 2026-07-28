@echo off
setlocal enabledelayedexpansion

REM ========================================
REM One-Click Build and Test Script
REM ========================================

call "%~dp0\env_common.bat"
if errorlevel 1 exit /b 1

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%~dp0..\.."
set BUILD_TYPE=Release
set BUILD_ARG=-release
set "FINAL_RESULT=0"

:parse_args
if "%1"=="" goto end_parse
if /i "%1"=="-debug" (
    set BUILD_TYPE=Debug
    set BUILD_ARG=-debug
    shift
    goto parse_args
)
if /i "%1"=="-release" (
    set BUILD_TYPE=Release
    set BUILD_ARG=-release
    shift
    goto parse_args
)
shift
goto parse_args
:end_parse

echo.
echo ========================================
echo One-Click Build and Test (%BUILD_TYPE%)
echo ========================================
echo.

REM ========================================
REM Step 1: Reinitialize Database
REM ========================================
echo ========================================
echo Step 1: Reinitializing oauth2_db database
echo ========================================
call "%SCRIPT_DIR%setup_database.bat"
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] Database initialization failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] Database initialized
echo.

REM ========================================
REM Step 2: Regenerate ORM Models
REM ========================================
echo ========================================
echo Step 2: Regenerating ORM models
echo ========================================
call "%SCRIPT_DIR%generate_models.bat" -y
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] ORM model generation failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] ORM models regenerated
echo.

REM ========================================
REM Step 3: Rebuild Project
REM ========================================
echo ========================================
echo Step 3: Rebuilding project
echo ========================================
call "%SCRIPT_DIR%build.bat" %BUILD_ARG%
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] Build failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] Project built
echo.

REM ========================================
REM Step 4: Run Tests
REM ========================================
echo ========================================
echo Step 4: Running tests
echo ========================================
call "%SCRIPT_DIR%test.bat" %BUILD_ARG%
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] Tests failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] All tests passed
echo.

REM ========================================
REM Step 5: Start Server
REM ========================================
echo ========================================
echo Step 5: Starting OAuth2 server
echo ========================================

set "SERVER_EXE=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%\%SERVER_BINARY_NAME%.exe"
if not exist "%SERVER_EXE%" (
    echo [FAILED] Server executable not found at %SERVER_EXE%
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)

REM Phase 5 restructure: run from the binary's build dir (same convention
REM as run_server.bat). main.cc has no -c flag -- it probes ./config.json
REM relative to CWD, and build.bat copies config.json next to the exe. The
REM old CWD (apps/server) no longer has a root config.json (configs moved
REM into apps/server/config/).
set "SERVER_RUN_DIR=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%"
echo Starting server from %SERVER_RUN_DIR% ...
pushd "%SERVER_RUN_DIR%"
start "" "%SERVER_EXE%"
popd

REM Wait for server to start
REM Use 'ping' instead of 'timeout /t': ping has no name clash with MSYS
REM (Unix 'timeout' shadows the Windows builtin when this .bat runs via
REM bash/MSYS, breaking the wait). ping sends 8 pings at 1s intervals ~ 8s.
echo Waiting for server to start...
ping 127.0.0.1 -n 9 >nul

REM Check if server is running
REM Use 'findstr' instead of 'find': MSYS has no 'findstr', so it always
REM resolves to the Windows builtin even when this .bat runs via bash/MSYS
REM (unlike 'find', which the Unix 'find' shadows).
tasklist /FI "IMAGENAME eq %SERVER_BINARY_NAME%.exe" 2>NUL | findstr /I "%SERVER_BINARY_NAME%.exe">NUL
if !errorlevel! neq 0 (
    echo [FAILED] Server failed to start or crashed. Check logs in %SERVER_RUN_DIR%\logs
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] Server started
echo.

REM ========================================
REM Step 6: Test OAuth2 Endpoints
REM ========================================
echo ========================================
echo Step 6: Testing OAuth2 endpoints
echo ========================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%test-oauth2-endpoints.ps1" -BaseUrl "http://127.0.0.1:5555"
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] OAuth2 endpoint tests failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] OAuth2 endpoint tests passed
echo.

REM ========================================
REM Step 7: Test Admin Endpoints
REM ========================================
echo ========================================
echo Step 7: Testing Admin endpoints
echo ========================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%test-admin-endpoints.ps1" -BaseUrl "http://127.0.0.1:5555"
if !errorlevel! neq 0 (
    echo.
    echo [FAILED] Admin endpoint tests failed
    set "FINAL_RESULT=1"
    goto cleanup_and_exit
)
echo [SUCCESS] Admin endpoint tests passed
echo.

REM ========================================
REM Step 8: Stop Server
REM ========================================
echo ========================================
echo Step 8: Stopping OAuth2 server
echo ========================================
taskkill /F /IM %SERVER_BINARY_NAME%.exe >nul 2>&1
echo [SUCCESS] Server stopped
echo.

REM ========================================
REM Success Summary
REM ========================================
echo ========================================
echo ALL STEPS COMPLETED SUCCESSFULLY!
echo ========================================
echo.
echo Summary:
echo   [1/8] Database initialization    - PASS
echo   [2/8] ORM model generation       - PASS
echo   [3/8] Project build              - PASS
echo   [4/8] Unit tests                 - PASS
echo   [5/8] Server startup             - PASS
echo   [6/8] OAuth2 endpoint tests      - PASS
echo   [7/8] Admin endpoint tests       - PASS
echo   [8/8] Server shutdown            - PASS
echo.

:cleanup_and_exit
REM Ensure server is stopped even on failure
REM Use 'findstr' instead of 'find': MSYS has no 'findstr', so it always
REM resolves to the Windows builtin even when this .bat runs via bash/MSYS
REM (unlike 'find', which the Unix 'find' shadows).
tasklist /FI "IMAGENAME eq %SERVER_BINARY_NAME%.exe" 2>NUL | findstr /I "%SERVER_BINARY_NAME%.exe">NUL
if "!errorlevel!"=="0" (
    taskkill /F /IM %SERVER_BINARY_NAME%.exe >nul 2>&1
)

if !FINAL_RESULT! neq 0 (
    echo.
    echo ========================================
    echo FULL TEST FAILED - see errors above
    echo ========================================
)

echo Press any key to exit...
pause >nul
endlocal
exit /b %FINAL_RESULT%
