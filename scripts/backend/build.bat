@echo off
setlocal enabledelayedexpansion

call "%~dp0\env_setup.bat"
if %errorlevel% neq 0 exit /b 1

echo Checking for running %SERVER_BINARY_NAME% processes...
taskkill /F /IM %SERVER_BINARY_NAME%.exe >nul 2>&1

set PROJECT_DIR=%~dp0..\..
set BUILD_TYPE=Release

:parse_args
if "%1"=="" goto end_parse
if /i "%1"=="-debug" (
    set BUILD_TYPE=Debug
    shift
    goto parse_args
)
shift
goto parse_args
:end_parse

echo Building Project with configuration: %BUILD_TYPE%

if not exist "%PROJECT_DIR%\%BUILD_DIR%" mkdir "%PROJECT_DIR%\%BUILD_DIR%"
cd /d "%PROJECT_DIR%\%BUILD_DIR%"

echo Installing dependencies with Conan...
if not exist "%USERPROFILE%\.conan2\profiles\default" (
    echo Initializing default conan profile...
    conan profile detect
)
conan install .. -s compiler="msvc" -s compiler.version=194 -s compiler.cppstd=17 -s build_type=%BUILD_TYPE% --output-folder . --build=missing

echo Configuring CMake...
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_CXX_STANDARD=17 -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

echo Building...
cmake --build . --parallel --config %BUILD_TYPE%
if %errorlevel% neq 0 (
    echo [Error] Build failed!
    exit /b 1
)

echo Copying config files...
REM cmd's copy mishandles the forward slashes used by paths.env values
REM (apps/server, config/config.json): it silently copies 0 files and
REM leaves errorlevel 1. Normalize to backslashes first.
set "CFG_SRC=..\%OAUTH2_SERVER_DIR%\%CONFIG_FILE%"
set "CFG_SRC=%CFG_SRC:/=\%"
set "SRV_OUT=.\%SERVER_BUILD_SUBDIR%\%BUILD_TYPE%"
set "SRV_OUT=%SRV_OUT:/=\%"
copy "%CFG_SRC%" "%SRV_OUT%\" /Y
if %errorlevel% neq 0 (
    echo [Error] Failed to copy config to %SRV_OUT%
    exit /b 1
)
REM Phase 5 restructure: tests build under BUILD_DIR/tests (was
REM OAuth2Server/test) and their config is a flat config.json there
REM (matches tests/CMakeLists POST_BUILD copy_if_different).
copy "%CFG_SRC%" ".\%TESTS_BUILD_SUBDIR%\%BUILD_TYPE%\config.json" /Y
if %errorlevel% neq 0 (
    echo [Error] Failed to copy config to tests build dir
    exit /b 1
)

echo Build completed successfully!
endlocal
