@echo off
REM Project environment configuration
REM Loads paths.env (repo root) for BUILD_DIR/CONFIG_FILE - single source
REM of truth for source/build/SQL/config paths (Task 5 / M0, review H2).
call "%~dp0paths_env.bat"
if errorlevel 1 exit /b 1
set DROGON_VERSION=v1.9.13

REM Check if conan is installed
where conan >nul 2>&1
if %errorlevel% neq 0 (
    echo [Error] Conan not found. Please install Conan.
    exit /b 1
)

REM Check if cmake is installed
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [Error] CMake not found. Please install CMake.
    exit /b 1
)
