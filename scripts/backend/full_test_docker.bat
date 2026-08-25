@echo off
setlocal enabledelayedexpansion

REM ========================================
REM One-Click Build and Test Script (Docker)
REM ========================================
REM This script performs a complete build and test cycle using Docker:
REM 1. Start PostgreSQL in Docker container
REM 2. Wait for database to be ready
REM 3. Reinitialize database
REM 4. Regenerate ORM models
REM 5. Rebuild project
REM 6. Run tests
REM 7. Start server
REM 8. Test Admin endpoints
REM 9. Stop server and cleanup
REM 10. Stop Docker containers
REM ========================================

echo.
echo ========================================
echo One-Click Build and Test (Docker)
echo ========================================
echo.

REM Store the script directory
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%..\.."
set PROJECT_DIR=%CD%
echo Project directory: %PROJECT_DIR%
echo.

REM Load paths.env (repo root) - single source of truth for source/build
REM /SQL/config paths (Task 5 / M0, design.md review H2).
call "%SCRIPT_DIR%paths_env.bat"
if errorlevel 1 exit /b 1

REM ========================================
REM Prerequisites Check
REM ========================================
echo Checking prerequisites...

REM Check if Docker is installed
where docker >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Docker is not installed or not in PATH
    echo Please install Docker Desktop for Windows.
    goto cleanup_and_exit
)

REM Check if Docker is running
docker ps >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Docker is not running
    echo Please start Docker Desktop.
    goto cleanup_and_exit
)

REM Check if docker-compose is available
where docker-compose >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [FAILED] docker-compose is not installed or not in PATH
    goto cleanup_and_exit
)

echo [OK] All prerequisites found
echo.

REM ========================================
REM Step 1: Start PostgreSQL in Docker
REM ========================================
echo ========================================
echo Step 1: Starting PostgreSQL in Docker
echo ========================================

REM Stop any existing containers
echo Stopping existing containers...
docker-compose -f "%PROJECT_DIR%\%COMPOSE_FILE_REL%" down >nul 2>&1

REM Start PostgreSQL and Redis containers
echo Starting PostgreSQL and Redis containers...
docker-compose -f "%PROJECT_DIR%\%COMPOSE_FILE_REL%" up -d fulla-postgres fulla-redis
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Failed to start containers
    goto cleanup_and_exit
)

REM Wait for PostgreSQL to be ready
echo Waiting for PostgreSQL to be ready...
set MAX_WAIT=30
set WAIT_COUNT=0

:wait_postgres
docker exec fulla-postgres pg_isready -U fulla_user -d fulla_db >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] PostgreSQL is ready
    goto postgres_ready
)

set /a WAIT_COUNT+=1
if %WAIT_COUNT% geq %MAX_WAIT% (
    echo [FAILED] PostgreSQL did not become ready in %MAX_WAIT% seconds
    goto cleanup_and_exit
)

echo Waiting... (%WAIT_COUNT%/%MAX_WAIT%)
timeout /t 1 /nobreak >nul
goto wait_postgres

:postgres_ready
echo.

REM ========================================
REM Step 2: Reinitialize Database
REM ========================================
echo ========================================
echo Step 2: Reinitializing fulla_db database
echo ========================================

REM Drop and recreate database using docker exec
echo Dropping existing database...
docker exec fulla-postgres psql -U fulla_user -d postgres -c "DROP DATABASE IF EXISTS fulla_db;"
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Failed to drop database
    goto cleanup_and_exit
)

echo Creating new database...
docker exec fulla-postgres psql -U fulla_user -d postgres -c "CREATE DATABASE fulla_db;"
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Failed to create database
    goto cleanup_and_exit
)

echo Applying migrations...
for %%f in ("%PROJECT_DIR%\%FULLA_SERVER_DIR%\%SQL_MIGRATIONS_REL_DIR%\V*.sql") do (
    echo   Applying %%~nxf...
    docker exec -i fulla-postgres psql -U fulla_user -d fulla_db < "%%f"
    if %ERRORLEVEL% neq 0 (
        echo [FAILED] Failed to apply %%~nxf
        goto cleanup_and_exit
    )
)

echo Applying seed data...
for %%f in ("%PROJECT_DIR%\%FULLA_SERVER_DIR%\%SQL_SEED_REL_DIR%\*.sql") do (
    echo   Applying %%~nxf...
    docker exec -i fulla-postgres psql -U fulla_user -d fulla_db < "%%f"
    if %ERRORLEVEL% neq 0 (
        echo [FAILED] Failed to apply seed %%~nxf
        goto cleanup_and_exit
    )
)

echo [SUCCESS] Database initialized
echo.

REM Capture arguments
set ARGS=%*

REM ========================================
REM Step 3: Regenerate ORM Models
REM ========================================
echo ========================================
echo Step 3: Regenerating ORM models
echo ========================================
call "%SCRIPT_DIR%generate_models.bat" -y
if %ERRORLEVEL% neq 0 (
    echo [FAILED] ORM model generation failed
    goto cleanup_and_exit
)
echo [SUCCESS] ORM models regenerated
echo.

REM ========================================
REM Step 4: Rebuild Project
REM ========================================
echo ========================================
echo Step 4: Rebuilding project
echo ========================================
call "%SCRIPT_DIR%build.bat" %ARGS%
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Build failed
    goto cleanup_and_exit
)
echo [SUCCESS] Project built
echo.

REM ========================================
REM Step 5: Run Tests
REM ========================================
echo ========================================
echo Step 5: Running tests
echo ========================================
call "%SCRIPT_DIR%test.bat" %ARGS%
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Tests failed
    goto cleanup_and_exit
)
echo [SUCCESS] All tests passed
echo.

REM ========================================
REM Step 6: Start Server
REM ========================================
echo ========================================
echo Step 6: Starting OAuth2 server
echo ========================================

REM Determine server executable path
set SERVER_EXE=
set EXE_DIR=
if exist "%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Release\%SERVER_BINARY_NAME%.exe" (
    set SERVER_EXE=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Release\%SERVER_BINARY_NAME%.exe
    set EXE_DIR=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Release
) else if exist "%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Debug\%SERVER_BINARY_NAME%.exe" (
    set SERVER_EXE=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Debug\%SERVER_BINARY_NAME%.exe
    set EXE_DIR=%PROJECT_DIR%\%BUILD_DIR%\%SERVER_BUILD_SUBDIR%\Debug
) else (
    echo [FAILED] Server executable not found
    goto cleanup_and_exit
)

echo Starting server: %SERVER_EXE%
pushd "%EXE_DIR%"
set FULLA_DB_HOST=127.0.0.1
set FULLA_DB_PORT=5433
set FULLA_REDIS_HOST=127.0.0.1
set FULLA_REDIS_PORT=6380
set FULLA_REDIS_PASSWORD=redis_secret_pass
start "" "%SERVER_EXE%" -c "%PROJECT_DIR%\%CONFIG_FILE%"
popd

REM Wait for server to start
echo Waiting for server to start...
timeout /t 3 /nobreak >nul

REM Check if server is running
tasklist /FI "IMAGENAME eq %SERVER_BINARY_NAME%.exe" 2>NUL | find /I /N "%SERVER_BINARY_NAME%.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo [SUCCESS] Server started
) else (
    REM If it failed, try running it again for a moment to see output or check if it crashed
    echo [FAILED] Server failed to start. Current directory is %CD%
    goto cleanup_and_exit
)

REM ========================================
REM Step 7: Test OAuth2 Endpoints
REM ========================================
echo ========================================
echo Step 7: Testing OAuth2 endpoints
echo ========================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%test-oauth2-endpoints.ps1" -BaseUrl "http://127.0.0.1:5555"
if %ERRORLEVEL% neq 0 (
    echo [FAILED] OAuth2 endpoint tests failed
    goto cleanup_and_exit
)
echo [SUCCESS] OAuth2 endpoint tests passed
echo.

REM ========================================
REM Step 8: Test Admin Endpoints
REM ========================================
echo ========================================
echo Step 8: Testing Admin endpoints
echo ========================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%test-admin-endpoints.ps1" -BaseUrl "http://127.0.0.1:5555"
if %ERRORLEVEL% neq 0 (
    echo [FAILED] Admin endpoint tests failed
    goto cleanup_and_exit
)
echo [SUCCESS] Admin endpoint tests passed
echo.

REM ========================================
REM Step 9: Stop Server and Cleanup
REM ========================================
echo ========================================
echo Step 9: Stopping OAuth2 server
echo ========================================

REM Try to stop the server binary
tasklist /FI "IMAGENAME eq %SERVER_BINARY_NAME%.exe" 2>NUL | find /I /N "%SERVER_BINARY_NAME%.exe">NUL
if "%ERRORLEVEL%"=="0" (
    taskkill /F /IM %SERVER_BINARY_NAME%.exe >nul 2>&1
    echo Stopped %SERVER_BINARY_NAME%.exe
)

REM Try to stop OAuth2Backend.exe
tasklist /FI "IMAGENAME eq OAuth2Backend.exe" 2>NUL | find /I /N "OAuth2Backend.exe">NUL
if "%ERRORLEVEL%"=="0" (
    taskkill /F /IM OAuth2Backend.exe >nul 2>&1
    echo Stopped OAuth2Backend.exe
)

echo [SUCCESS] Server stopped
echo.

REM ========================================
REM Step 10: Stop Docker Containers
REM ========================================
echo ========================================
echo Step 10: Stopping Docker containers
echo ========================================
docker-compose -f "%PROJECT_DIR%\%COMPOSE_FILE_REL%" down
echo [SUCCESS] Docker containers stopped
echo.

REM ========================================
REM Success Summary
REM ========================================
echo ========================================
echo ALL STEPS COMPLETED SUCCESSFULLY!
echo ========================================
echo.
echo Summary:
echo   [1/10] PostgreSQL container startup - PASS
echo   [2/10] Database initialization       - PASS
echo   [3/10] ORM model generation          - PASS
echo   [4/10] Project build                 - PASS
echo   [5/10] Unit tests                    - PASS
echo   [6/10] Server startup                - PASS
echo   [7/10] OAuth2 endpoint tests         - PASS
echo   [8/10] Admin endpoint tests          - PASS
echo   [9/10] Server shutdown               - PASS
echo   [10/10] Docker containers cleanup    - PASS
echo.
echo ========================================
echo Docker Build and Test Cycle Complete
echo ========================================
goto cleanup_and_exit

REM ========================================
REM Cleanup and Exit
REM ========================================
:cleanup_and_exit

REM Ensure server is stopped even on failure
echo.
echo Ensuring server is stopped...
tasklist /FI "IMAGENAME eq %SERVER_BINARY_NAME%.exe" 2>NUL | find /I /N "%SERVER_BINARY_NAME%.exe">NUL
if "%ERRORLEVEL%"=="0" (
    taskkill /F /IM %SERVER_BINARY_NAME%.exe >nul 2>&1
)

tasklist /FI "IMAGENAME eq OAuth2Backend.exe" 2>NUL | find /I /N "OAuth2Backend.exe">NUL
if "%ERRORLEVEL%"=="0" (
    taskkill /F /IM OAuth2Backend.exe >nul 2>&1
)

REM Stop Docker containers
echo Stopping Docker containers...
docker-compose -f "%PROJECT_DIR%\%COMPOSE_FILE_REL%" down >nul 2>&1

REM Pause before exit
echo.
echo Press any key to exit...
pause >nul

endlocal
exit /b 0
