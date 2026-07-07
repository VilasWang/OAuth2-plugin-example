cmake_minimum_required(VERSION 3.10)

project(oauth2-plugin-example
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "OAuth2 / OIDC Plugin & Reference Server (Drogon)")

# Load shared CMake modules (after project() so toolchain is fully applied)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Version)
include(Compatibility)
include(Sanitizers)
# Paths.cmake parses the top-level paths.env (single source of truth for
# source/build/SQL/config paths, review H2) and exposes OAUTH2_PATH_* vars.
include(Paths)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_subdirectory(${OAUTH2_PATH_OAUTH2_PLUGIN_DIR})
add_subdirectory(${OAUTH2_PATH_OAUTH2_SERVER_DIR})
