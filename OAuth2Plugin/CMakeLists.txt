cmake_minimum_required(VERSION 3.16)

# --- Self-contained bootstrap (PR #2 P2 review) -----------------------------
# OAuth2Plugin is documented (docs/backend/plugin-integration.md) as a standalone
# library usable via `add_subdirectory(OAuth2Plugin)` from a third-party root.
# Such a root will NOT have included the repo's shared CMake modules, so make
# this CMakeLists provide its own fallbacks instead of requiring the repo root:
#   * OAUTH2_PROJECT_VERSION (normally set by the root's Version.cmake)
#   * oauth2_apply_compat()  (normally defined by the root's Compatibility.cmake)
# When configured from the repo root these are already defined/included, so the
# guards below are no-ops and behavior is unchanged.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
if(NOT DEFINED OAUTH2_PROJECT_VERSION)
    set(OAUTH2_PROJECT_VERSION 1.0.0)
endif()
if(NOT COMMAND oauth2_apply_compat)
    include(Compatibility)
endif()

project(OAuth2Plugin VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

# Find Dependencies
find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
# libcurl provides the SMTP/SMTPS transport for SmtpEmailService (PR #2 P1 fix).
# It is built against the same OpenSSL as the rest of the project, so no second
# TLS implementation is introduced.
find_package(CURL REQUIRED)

# Plugin Core Library
add_library(${PROJECT_NAME} OBJECT)

# Include Directories
target_include_directories(${PROJECT_NAME}
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # M2b Task 18: the ORM model headers (and their generator-emitted
        # sibling quote-includes) moved to libs/storage-postgres; this
        # private include path is no longer needed here.
)

# Sources
file(GLOB_RECURSE PLUGIN_SOURCES 
    "src/*.cc"
)

target_sources(${PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})

# Links
# authforge::common (M2a Task 13/14): the OpenSslCryptoProvider/
# OpenSslUuidGenerator/SystemClock Adapter implementations under
# OAuth2Plugin/src/adapters/ implement authforge::common::ports interfaces
# (design.md §5.6), so this target needs the port headers. This does NOT
# make OAuth2Plugin's OWN Domain-layer code (Domain-facing classes under
# oauth2/) depend on common yet -- that call-site migration is a separate,
# incremental step (Task 14's own scope).
#
# authforge::oauth2 (M2b Task 17 slice 4): TokenService::generateSha256Hash
# now delegates to oauth2::pkce::computeCodeChallenge() (the RFC 7636 fix,
# see TokenService.cc's own comment). This is the correct dependency
# direction (design.md §4.1): OAuth2Plugin, an Adapter-layer/production
# target, is allowed to depend on the Domain-layer libs/oauth2; libs/oauth2
# itself has zero dependency back on OAuth2Plugin.
# authforge::storage::postgres (M2b Task 18): ORM models relocated out of
# this target into libs/storage-postgres; every PostgresXxxRepository.cc
# in src/storage/ now includes them from there.
target_link_libraries(${PROJECT_NAME} PUBLIC Drogon::Drogon OpenSSL::Crypto CURL::libcurl authforge::common authforge::oauth2 authforge::storage::postgres authforge::storage::memory)

# Conan provides a STATIC libcurl by default. On Windows the curl headers
# decorate symbols with __declspec(dllimport) UNLESS CURL_STATICLIB is defined,
# which otherwise yields unresolved __imp_curl_* symbols when linking the static
# lib. (find_package(CURL) here resolves via CMake's built-in FindCURL module,
# which exposes CURL::libcurl as an UNKNOWN imported target and does NOT set
# this define, so we set it explicitly.) PUBLIC so consumers of this OBJECT lib
# that include curl headers transitively also get it. No-op on non-Windows.
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CURL_STATICLIB)
endif()

# Compiler compatibility (shared module)
oauth2_apply_compat(${PROJECT_NAME})

# Views (if any core views exist, but currently they are in Server)

# Install Rules
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    EXPORT OAuth2PluginTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/oauth2 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT OAuth2PluginTargets
    FILE OAuth2PluginConfig.cmake
    NAMESPACE OAuth2Plugin::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OAuth2Plugin
)
