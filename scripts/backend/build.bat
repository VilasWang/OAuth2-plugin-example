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
copy "..\%OAUTH2_SERVER_DIR%\%CONFIG_FILE%" ".\%OAUTH2_SERVER_DIR%\%BUILD_TYPE%\" /Y
copy "..\%OAUTH2_SERVER_DIR%\%CONFIG_FILE%" ".\%OAUTH2_SERVER_DIR%\test\%BUILD_TYPE%\" /Y

echo Build completed successfully!
endlocal
