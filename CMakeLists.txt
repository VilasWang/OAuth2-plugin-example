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

# libs/common (authforge::common, M2a Task 13): Domain-layer shared kernel.
# Added before OAuth2Plugin/OAuth2Server since it has no dependency on
# either (design.md §4.1: Domain layer must not depend on Drogon) -- this
# is purely additive for now; OAuth2Plugin/OAuth2Server do not yet consume
# it (that migration is a later M2a task).
add_subdirectory(${OAUTH2_PATH_LIBS_COMMON_DIR})

# libs/common/testing (authforge::common::testing, M2a Task 15): fake
# ("test double") implementations of every common::ports interface, kept
# separate from both libs/common (ports only) and OAuth2Plugin's
# production Adapter implementations, so production binaries never link
# test doubles and future standalone Domain packages can depend on this
# for deterministic unit tests without pulling in Drogon.
add_subdirectory(${OAUTH2_PATH_LIBS_COMMON_TESTING_DIR})

# libs/oauth2 (authforge::oauth2, M2b Task 17): Domain-layer OAuth2/OIDC
# protocol package. Added before OAuth2Plugin/OAuth2Server for the same
# reason as libs/common above -- it depends only on libs/common, never on
# Drogon or OAuth2Plugin (design.md §4.1). Purely additive for now:
# OAuth2Plugin/OAuth2Server do not yet consume it.
add_subdirectory(${OAUTH2_PATH_LIBS_OAUTH2_DIR})

# libs/storage-postgres (authforge::storage::postgres, M2b Task 18):
# drogon::orm-generated ORM model classes, relocated out of OAuth2Plugin
# (design.md §4.1/§5.5: ORM models must not live in the Domain layer;
# this is their designated Adapter-layer home). OAuth2Plugin now depends
# on this package instead of owning the models itself.
add_subdirectory(${OAUTH2_PATH_LIBS_STORAGE_POSTGRES_DIR})

# libs/identity (authforge::identity, M2.5 Task 19): Domain-layer identity
# & authentication package. Handles user auth, MFA, WebAuthn, social login,
# session management, RBAC. Implements IRoleProvider, ISubjectResolver,
# IUserInfoProvider ports. Depends only on libs/common and storage interfaces,
# never on OAuth2 domain (arch-guard enforced). Purely additive for now.
add_subdirectory(${OAUTH2_PATH_LIBS_IDENTITY_DIR})

add_subdirectory(${OAUTH2_PATH_OAUTH2_PLUGIN_DIR})
add_subdirectory(${OAUTH2_PATH_OAUTH2_SERVER_DIR})
