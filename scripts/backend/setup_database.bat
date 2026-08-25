@echo off
setlocal

call "%~dp0\env_common.bat"
if errorlevel 1 exit /b 1

REM Check for PostgreSQL client
where psql >nul 2>&1
if errorlevel 1 (
    echo [Error] psql not found in PATH.
    exit /b 1
)

set PROJECT_DIR=%~dp0..\..
set MIGRATIONS_DIR=%PROJECT_DIR%\%FULLA_SERVER_DIR%\%SQL_MIGRATIONS_REL_DIR%
set SEED_DIR=%PROJECT_DIR%\%FULLA_SERVER_DIR%\%SQL_SEED_REL_DIR%

REM DB connection settings: honour env vars (the server / CI may override
REM these, e.g. a non-default role or password), falling back to the local
REM dev defaults. Mirrors setup-database.sh.
if not defined FULLA_DB_USER set "FULLA_DB_USER=fulla_user"
if not defined FULLA_DB_NAME set "FULLA_DB_NAME=fulla_db"
if not defined FULLA_DB_PASSWORD set "FULLA_DB_PASSWORD=123456"
if not defined FULLA_DB_HOST set "FULLA_DB_HOST=localhost"
if not defined FULLA_DB_PORT set "FULLA_DB_PORT=5432"

echo Setting up %FULLA_DB_NAME% database ^(role %FULLA_DB_USER%@%FULLA_DB_HOST%:%FULLA_DB_PORT%^)...

set "PGPASSWORD=%FULLA_DB_PASSWORD%"
set PGCLIENTENCODING=UTF8

echo Dropping existing database...
psql -U %FULLA_DB_USER% -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -d postgres -c "DROP DATABASE IF EXISTS %FULLA_DB_NAME%;" >nul 2>&1

echo Creating new database...
psql -U %FULLA_DB_USER% -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -d postgres -c "CREATE DATABASE %FULLA_DB_NAME%;"
if errorlevel 1 (
    echo [Error] Failed to create database "%FULLA_DB_NAME%" as role "%FULLA_DB_USER%".
    echo         Verify the role exists, FULLA_DB_PASSWORD is correct, and that
    echo         PostgreSQL is reachable at %FULLA_DB_HOST%:%FULLA_DB_PORT%.
    exit /b 1
)

REM Apply migrations
if exist "%MIGRATIONS_DIR%" (
    echo Applying migrations from %MIGRATIONS_DIR%...
    for %%f in ("%MIGRATIONS_DIR%\V*.sql") do (
        echo   Applying %%~nxf...
        psql -U %FULLA_DB_USER% -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -d %FULLA_DB_NAME% -f "%%f"
        if errorlevel 1 (
            echo [Error] Failed to apply %%~nxf
            exit /b 1
        )
    )
) else (
    echo [Error] Migrations directory not found: %MIGRATIONS_DIR%
    exit /b 1
)

REM Apply seed data (dev/test only)
if exist "%SEED_DIR%" (
    echo Applying seed data from %SEED_DIR%...
    for %%f in ("%SEED_DIR%\*.sql") do (
        echo   Applying %%~nxf...
        psql -U %FULLA_DB_USER% -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -d %FULLA_DB_NAME% -f "%%f"
        if errorlevel 1 (
            echo [Error] Failed to apply seed %%~nxf
            exit /b 1
        )
    )
)

echo Database setup complete!
endlocal
exit /b 0
