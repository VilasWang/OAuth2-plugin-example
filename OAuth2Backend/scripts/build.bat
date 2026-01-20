@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Kill any running OAuth2Server processes
REM ========================================
echo Checking for running OAuth2Server processes...
taskkill /F /IM OAuth2Server.exe >nul 2>&1
if %errorlevel% equ 0 (
    echo Killed running OAuth2Server.exe process
    timeout /t 1 /nobreak >nul
) else (
    echo No running OAuth2Server.exe process found
)

REM Store the script directory and change to parent (OAuth2Backend)
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%.."
set PROJECT_DIR=%CD%
echo Current work directory is "%PROJECT_DIR%"

REM Default build type
set BUILD_TYPE=Release

REM Parse command line arguments
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
echo Unknown option: %1
echo Usage: %0 [-debug|-release] [-t] [-install path] [-doc]
echo   -debug     Build debug version
echo   -release   Build release version (default)
exit /b 1
:end_parse

echo Building Drogon with configuration:
echo   Build Type: %BUILD_TYPE%
echo.

REM Create build directory
if exist build (
    echo Removing existing build directory...
    rmdir /s /q build
)
echo Creating build directory...
mkdir build
cd build

REM Check if conan is available
where conan >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Conan is not installed or not in PATH
    cd ..
    exit /b 1
)

REM Initialize conan profile
echo Initializing Conan profile...
conan profile detect --force
if %errorlevel% neq 0 (
    echo Error: Failed to initialize Conan profile
    cd ..
    exit /b 1
)

REM Install dependencies
echo Installing dependencies with Conan...
conan install .. -s compiler="msvc" -s compiler.version=194 -s compiler.cppstd=20 -s build_type=%BUILD_TYPE% --output-folder . --build=missing
if %errorlevel% neq 0 (
    echo Error: Failed to install dependencies
    cd ..
    exit /b 1
)

REM Configure CMake
echo Configuring CMake...
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_CXX_STANDARD=20  -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW 
if %errorlevel% neq 0 (
    echo Error: CMake configuration failed
    cd ..
    exit /b 1
)

REM Build and install
echo Building and installing Drogon...
cmake --build . --parallel  --config %BUILD_TYPE%
if %errorlevel% neq 0 (
    echo Error: Build failed
    cd ..
    exit /b 1
)

REM Copy config.json to build directory
robocopy .. %BUILD_TYPE% config.json /NFL /NDL /NJH /NJS /NP
if %ERRORLEVEL% GEQ 8 (
  echo Error: copy failed
  cd ..
  exit /b 1
)
robocopy .. test/%BUILD_TYPE% config.json /NFL /NDL /NJH /NJS /NP
if %ERRORLEVEL% GEQ 8 (
  echo Error: copy failed
  cd ..
  exit /b 1
)

echo Build completed successfully!
cd ..