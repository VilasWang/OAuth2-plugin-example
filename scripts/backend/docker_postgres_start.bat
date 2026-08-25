@echo off
setlocal

REM ========================================
REM Start PostgreSQL in Docker
REM ========================================

echo Starting PostgreSQL in Docker...

cd /d "%~dp0../.."

REM Stop any existing containers (compose file relocated to deploy\docker\)
echo Stopping existing containers...
docker-compose -f deploy\docker\docker-compose.yml down >nul 2>&1

REM Start PostgreSQL
echo Starting PostgreSQL container...
docker-compose -f deploy\docker\docker-compose.yml up -d fulla-postgres

if %ERRORLEVEL% neq 0 (
    echo [FAILED] Failed to start PostgreSQL
    endlocal
    exit /b 1
)

echo Waiting for PostgreSQL to be ready...
set MAX_WAIT=30
set WAIT_COUNT=0

:wait_loop
docker exec fulla-postgres pg_isready -U fulla_user -d fulla_db >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] PostgreSQL is ready!
    echo.
    echo Connection info:
    echo   Host: 127.0.0.1
    echo   Port: 5432
    echo   Database: fulla_db
    echo   User: fulla_user
    echo   Password: 123456
    echo.
    goto :done
)

set /a WAIT_COUNT+=1
if %WAIT_COUNT% geq %MAX_WAIT% (
    echo.
    echo [FAILED] PostgreSQL did not become ready
    docker-compose -f deploy\docker\docker-compose.yml down
    endlocal
    exit /b 1
)

echo Waiting... (%WAIT_COUNT%/%MAX_WAIT%)
timeout /t 1 /nobreak >nul
goto wait_loop

:done
endlocal
exit /b 0
