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
set MIGRATIONS_DIR=%PROJECT_DIR%\%OAUTH2_SERVER_DIR%\%SQL_MIGRATIONS_REL_DIR%
set SEED_DIR=%PROJECT_DIR%\%OAUTH2_SERVER_DIR%\%SQL_SEED_REL_DIR%

echo Setting up oauth2_db database...

set PGPASSWORD=123456
set PGCLIENTENCODING=UTF8

echo Dropping existing database...
psql -U oauth2_user -d postgres -c "DROP DATABASE IF EXISTS oauth2_db;" >nul 2>&1

echo Creating new database...
psql -U oauth2_user -d postgres -c "CREATE DATABASE oauth2_db;" >nul 2>&1

REM Apply migrations
if exist "%MIGRATIONS_DIR%" (
    echo Applying migrations from %MIGRATIONS_DIR%...
    for %%f in ("%MIGRATIONS_DIR%\V*.sql") do (
        echo   Applying %%~nxf...
        psql -U oauth2_user -d oauth2_db -f "%%f"
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
        psql -U oauth2_user -d oauth2_db -f "%%f"
        if errorlevel 1 (
            echo [Error] Failed to apply seed %%~nxf
            exit /b 1
        )
    )
)

echo Database setup complete!
endlocal
exit /b 0
