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
    echo Match the psql message above to its fix ^(run the fix from a
    echo superuser shell, then re-run this script^):
    echo.
    echo  1^) "database ... already exists" -- the silent DROP step failed,
    echo     usually an open connection ^(running fulla-server, psql, IDE^)
    echo     holds it open:
    echo       psql -U postgres -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -c "DROP DATABASE %FULLA_DB_NAME% WITH (FORCE);"
    echo.
    echo  2^) "permission denied to create database" -- the role lacks
    echo     CREATEDB ^(DROP only needs ownership^):
    echo       psql -U postgres -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -c "ALTER ROLE %FULLA_DB_USER% CREATEDB;"
    echo     ^(docker: docker exec ^<pg-container^> psql -U postgres -c "ALTER ROLE %FULLA_DB_USER% CREATEDB;"^)
    echo.
    echo  3^) "role ... does not exist" -- create it once with the same
    echo     password FULLA_DB_PASSWORD points at:
    echo       psql -U postgres -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -c "CREATE ROLE %FULLA_DB_USER% LOGIN PASSWORD '<choose-a-password>';"
    echo.
    echo  4^) "password authentication failed" -- set FULLA_DB_PASSWORD to
    echo     this role's real password.
    echo.
    echo  5^) "could not connect" / "Connection refused" -- start PostgreSQL
    echo     or point FULLA_DB_HOST/FULLA_DB_PORT at it.
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

REM Apply seed data (dev/test only; explicit list - benchmark-only seeds live
REM in benchmarks\fulla\seed and never land in a dev/test database)
if exist "%SEED_DIR%" (
    echo Applying seed data from %SEED_DIR%...
    for %%f in ("dev_admin_user.sql" "dev_admin_console_client.sql" "dev_backend_client.sql" "dev_vue_client.sql") do (
        echo   Applying %%~nxf...
        psql -U %FULLA_DB_USER% -h %FULLA_DB_HOST% -p %FULLA_DB_PORT% -d %FULLA_DB_NAME% -f "%SEED_DIR%\%%~nxf"
        if errorlevel 1 (
            echo [Error] Failed to apply seed %%~nxf
            exit /b 1
        )
    )
)

echo Database setup complete!
endlocal
exit /b 0
