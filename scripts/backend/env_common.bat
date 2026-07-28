@echo off
REM Common path validation for all backend scripts.
REM Loads paths.env (repo root) - single source of truth for source/build
REM /SQL/config paths (Task 5 / M0, design.md review H2).
call "%~dp0paths_env.bat"
if errorlevel 1 exit /b 1

if not exist "%~dp0..\..\%OAUTH2_SERVER_DIR%" (
    echo [Error] Script must be run from 'scripts/backend' directory.
    exit /b 1
)
