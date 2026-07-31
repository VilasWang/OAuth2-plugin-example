# cmake/Paths.cmake
# Single source of truth for repo-wide path values, parsed from the
# top-level `paths.env` file (see .kiro/specs/authforge-sdk-refactor
# tasks.md Task 5, design.md review H2).
#
# CMake cannot `source` a shell env file directly, so this module parses
# paths.env's simple `KEY=VALUE` lines (no `export`, no quoting) with
# file(STRINGS) + a regex, and exposes each key as a CMake variable
# prefixed with `OAUTH2_PATH_` (e.g. `OAUTH2_PATH_OAUTH2_SERVER_DIR`).
#
# This keeps CMake, Bash and PowerShell/Batch scripts all reading the same
# underlying values without requiring a CMake-specific duplicate file.
# When later milestones (M2a/M3/M8) move directories, only `paths.env`
# needs to change -- this module and its consumers stay untouched.
#
# NOTE: values are stored as-is (paths relative to the repo root); combine
# with CMAKE_SOURCE_DIR to build absolute paths where needed.

if(NOT DEFINED OAUTH2_PATHS_ENV_FILE)
    set(OAUTH2_PATHS_ENV_FILE "${CMAKE_SOURCE_DIR}/paths.env")
endif()

if(NOT EXISTS "${OAUTH2_PATHS_ENV_FILE}")
    message(FATAL_ERROR "paths.env not found at ${OAUTH2_PATHS_ENV_FILE}")
endif()

file(STRINGS "${OAUTH2_PATHS_ENV_FILE}" _oauth2_paths_lines)

foreach(_oauth2_paths_line IN LISTS _oauth2_paths_lines)
    string(STRIP "${_oauth2_paths_line}" _oauth2_paths_line)
    if(_oauth2_paths_line STREQUAL "" OR _oauth2_paths_line MATCHES "^#")
        continue()
    endif()
    if(_oauth2_paths_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
        set(OAUTH2_PATH_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()

unset(_oauth2_paths_lines)
unset(_oauth2_paths_line)

# Re-run CMake configure if paths.env changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OAUTH2_PATHS_ENV_FILE}")
