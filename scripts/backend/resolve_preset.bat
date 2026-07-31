@echo off
REM resolve_preset.bat - Map a build configuration to a CMakePresets.json
REM preset name (Windows). Sets CMAKE_PRESET in the CALLER's environment
REM (intentionally no setlocal, mirroring paths_env.bat).
REM
REM Usage: call "%~dp0resolve_preset.bat" <Release|Debug>
REM
REM Windows uses the Visual Studio (multi-config) generator, so Release and
REM Debug each get their own Conan-installed preset directory
REM (build/windows-msvc and build/windows-msvc-debug) to keep the resolved
REM Conan dependencies build-type-correct.
if /i "%~1"=="Debug" (
    set "CMAKE_PRESET=windows-msvc-debug"
) else (
    set "CMAKE_PRESET=windows-msvc"
)
