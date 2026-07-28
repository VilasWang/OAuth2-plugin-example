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

# M5 Task 31 (authforge-sdk-refactor, design.md F9 / §9.1): conditional-
# compilation options for the optional AuthForge feature areas. Declared at
# the TOP level (before any add_subdirectory) so every lib's own option()
# calls resolve to the same value (option() is a no-op when the variable is
# already set). Conan's matching with_identity/with_social/with_webauthn
# options are threaded into these as WITH_* cache vars by conanfile.py's
# generate(). All default ON to preserve today's build behavior; turning
# WITH_SOCIAL/WITH_WEBAUTHN OFF shrinks the dependency surface (the social
# controllers + WebAuthn controllers/services drop out of the build, and
# libcbor is no longer required).
#
# WITH_IDENTITY is declared for symmetry with conanfile.py's `with_identity`
# and currently defaults ON; flipping it OFF is NOT yet a working
# configuration -- authforge::identity is consumed by storage-postgres,
# OAuth2Plugin, and several controllers (Session/Mfa/WebAuthn/Organization),
# so disabling it requires broader wiring that is out of scope for this task
# (tracked as follow-up). libs/identity itself always builds today.
option(WITH_IDENTITY "Build the authforge::identity SDK module" ON)
option(WITH_SOCIAL "Build social-login (Google/WeChat/GitHub) controllers + services" ON)
option(WITH_WEBAUTHN "Build the WebAuthn/FIDO2 controller + service" ON)

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

# libs/identity (authforge::identity, M2.5 Task 19): Domain-layer identity
# & authentication package. Handles user auth, MFA, WebAuthn, social login,
# session management, RBAC. Implements IRoleProvider, ISubjectResolver,
# IUserInfoProvider ports. Depends only on libs/common (arch-guard
# enforced: never on libs/oauth2). Added BEFORE libs/storage-postgres
# because storage-postgres's PostgresIdentityRepository (Task 19)
# implements identity's repository interfaces and therefore links
# authforge::identity.
add_subdirectory(${OAUTH2_PATH_LIBS_IDENTITY_DIR})

# libs/storage-postgres (authforge::storage::postgres, M2b Task 18):
# drogon::orm-generated ORM model classes, relocated out of OAuth2Plugin
# (design.md §4.1/§5.5: ORM models must not live in the Domain layer;
# this is their designated Adapter-layer home). OAuth2Plugin now depends
# on this package instead of owning the models itself.
add_subdirectory(${OAUTH2_PATH_LIBS_STORAGE_POSTGRES_DIR})

# libs/storage-memory (authforge::storage::memory, M8 Task 40): in-memory
# repository implementations relocated out of OAuth2Plugin/storage/ (design.md
# §5.4/§5.5). Pure-Domain adapters (std + jsoncpp, zero Drogon). Added BEFORE
# OAuth2Plugin because the plugin now links this package (the Memory
# RepositoryBundle is the memory-storage backend selected by storage_type=memory).
add_subdirectory(${OAUTH2_PATH_LIBS_STORAGE_MEMORY_DIR})

# libs/storage-redis (authforge::storage::redis, Phase 2 of the oauth2 storage
# refactor): Redis-backed oauth2 repository implementations
# (RedisRepositoryBase / Redis{Client,Grant,Token,Consent}Repository /
# RedisRepositoryBundle) + CachedClientRepository (cache decorator), relocated
# out of OAuth2Plugin/storage/. Added BEFORE OAuth2Plugin because the plugin
# links this package (RedisRepositoryBundle is the redis-storage backend
# selected by storage_type=redis).
add_subdirectory(${OAUTH2_PATH_LIBS_STORAGE_REDIS_DIR})

add_subdirectory(${OAUTH2_PATH_OAUTH2_PLUGIN_DIR})

# libs/drogon (authforge::drogon, M3 Task 20): Adapter-layer package for
# Drogon-coupled code (plugin/controllers/filters/views). Added AFTER
# OAuth2Plugin (unlike the Domain-layer libs/* above, which are added
# BEFORE) because this slice's HttpResponder.cc links against the
# OAuth2Plugin target for the not-yet-relocated common::error machinery
# (see libs/drogon/CMakeLists.txt's own header comment) -- the OAuth2Plugin
# CMake target must already exist when this add_subdirectory runs.
add_subdirectory(${OAUTH2_PATH_LIBS_DROGON_DIR})

add_subdirectory(${OAUTH2_PATH_OAUTH2_SERVER_DIR})

# examples/third-party-host (B8b / Task 28b, design.md §1.1/§6): minimal SDK
# consumer that links ONLY the authforge SDK packages (no OAuth2Plugin /
# libs/drogon / libs/storage-postgres) and drives the authorization-code flow
# through the Domain-layer engine -- the smoke gate proving the SDK is
# independently consumable. Gated by BUILD_EXAMPLES (default ON so CI runs it).
option(BUILD_EXAMPLES "Build the SDK consumer examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples/third-party-host)
endif()
