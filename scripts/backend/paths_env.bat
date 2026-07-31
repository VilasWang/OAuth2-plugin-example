@echo off
REM paths_env.bat - Load paths.env (repo root) into batch environment
REM variables. Single source of truth for source/build/SQL/config paths
REM (Task 5 / M0, design.md review H2).
REM
REM Usage: call "%~dp0paths_env.bat"  (no setlocal here on purpose, so the
REM variables set below persist in the caller's environment).
REM
REM Each `KEY=VALUE` line in paths.env becomes `set "KEY=VALUE"`. Lines
REM starting with # are comments and blank lines are skipped.

set "_PATHS_ENV_FILE=%~dp0..\..\paths.env"

if not exist "%_PATHS_ENV_FILE%" (
    echo [Error] paths.env not found at %_PATHS_ENV_FILE%
    exit /b 1
)

for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%_PATHS_ENV_FILE%") do (
    if not "%%A"=="" set "%%A=%%B"
)

set "_PATHS_ENV_FILE="
