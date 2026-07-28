cmake_minimum_required(VERSION 3.5)
project(OAuth2Test_test CXX)

# Optional flags
option(OAUTH2_MEMORY_TESTS_ONLY "Build only tests that do not require external databases" OFF)
option(OAUTH2_TEST_COVERAGE "Enable code coverage reporting" OFF)

# M4 Task 27 (authforge-sdk-refactor, design.md F6 / Testing Strategy):
# This test binary used to GLOB the plugin/server sources directly into
# itself (the Task 16 transitional bridge), masking linking-boundary
# problems and slowing the build. The test now consumes library products
# only: it links authforge::* below and no longer compiles any plugin or
# OAuth2Server/{controllers,filters} sources directly. Whole-archive is
# not needed (AutoCreation=false controllers are explicitly registered).
# Phase 4 (A6): the OAuth2Plugin OBJECT library is gone -- the plugin
# class lives in authforge::drogon (STATIC) and its public headers under
# libs/drogon/include, so the former OAuth2Plugin/include path and the
# bare OAuth2Plugin link edge are dropped.

# SchemaManager: needed by the SQL-splitter unit test (unit/schema/).
# Compiled directly into the test target so the splitter can be exercised
# without a running database.
set(SCHEMA_MGR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../SchemaManager.cc")

# M3 Task 23 (authforge-sdk-refactor): test_main.cc now calls
# bootstrap::wireControllerPluginDependencies() (see its own header comment),
# defined in ControllerRegistration.cc alongside registerAllControllers()
# (already called by test_main.cc directly, without going through this
# glob, before this task). Task 24 slice 4 adds IdentityAssembly.cc
# (test_main.cc also calls bootstrap::wireIdentityServices()). These two
# bootstrap/ files are needed by the test target; the others
# (CorsSetup/SecurityHeaders/etc) are apps/server-only concerns
# test_main.cc does not use.
set(BOOTSTRAP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/../bootstrap/ControllerRegistration.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../bootstrap/IdentityAssembly.cc"
)

# M5 Task 30: the product-level OrganizationController (apps/server/src/
# organization/) is registered by test_main.cc, so the test binary must
# compile it too (same pattern as SchemaManager/bootstrap above).
# Task 29b batch 3: OrganizationService extracted alongside (DB access via
# Mapper+Criteria); both compile into the test binary.
set(ORGANIZATION_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/organization/OrganizationController.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/organization/OrganizationService.cc"
)

# Collect hierarchical test sources
file(GLOB_RECURSE UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cc")
file(GLOB_RECURSE INTEGRATION_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cc")
file(GLOB_RECURSE E2E_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/e2e/*.cc")
file(GLOB_RECURSE SECURITY_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/security/*.cc")
file(GLOB_RECURSE PERFORMANCE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/performance/*.cc")
# Task 12 (authforge-sdk-refactor, design.md §7.3 / F5): tiered repository
# contract tests (IClientRepository/IGrantRepository/ITokenRepository/
# IConsentRepository across Postgres/Redis/Memory). Kept under the existing
# OAuth2Server/test/ hierarchy (see contract/ContractFixtures.h header for
# the placement rationale) rather than the tasks.md-referenced top-level
# tests/contract/, which is the M2a-and-later target layout.
file(GLOB_RECURSE CONTRACT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/contract/*.cc")

# Root test sources
set(ROOT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/test_main.cc"
)

if(NOT OAUTH2_MEMORY_TESTS_ONLY)
    list(APPEND ROOT_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/SchemaSetup.cc"
    )
endif()

# Combine all test sources
set(ALL_TEST_SOURCES
    ${ROOT_TEST_SOURCES}
    ${UNIT_TESTS}
    ${INTEGRATION_TESTS}
    ${E2E_TESTS}
    ${SECURITY_TESTS}
    ${PERFORMANCE_TESTS}
    ${CONTRACT_TESTS}
)

add_executable(${PROJECT_NAME}
    ${ALL_TEST_SOURCES}
    ${SCHEMA_MGR_SRC}
    ${BOOTSTRAP_SRC}
    ${ORGANIZATION_SRC}
)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../views
                    ${CMAKE_CURRENT_BINARY_DIR})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/organization
)

# Link libraries
find_package(OpenSSL REQUIRED)
# M4 Task 27: the test consumes library products only. Phase 4 (A6): the
# plugin symbols reach the test through authforge::drogon (which absorbed
# the old OAuth2Plugin OBJECT lib and PUBLIC-provides CURL::libcurl +
# CURL_STATICLIB transitively). Plain link, no whole-archive
# (AutoCreation=false + explicit registration in test_main.cc).
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon OpenSSL::Crypto authforge::common authforge::oauth2 authforge::storage::postgres authforge::storage::memory authforge::drogon authforge::identity)

# Platform compatibility (shared module)
oauth2_apply_compat(${PROJECT_NAME})

# Optional TSan/ASan instrumentation (shared module).
# Controlled by the cache option OAUTH2_SANITIZER = off|thread|address.
# GCC/Clang + Debug only; TSan and ASan are mutually exclusive (two builds).
# The test executable compiles all plugin/server sources directly into itself,
# so instrumenting this single target covers all code exercised by the suite.
oauth2_apply_sanitizer(${PROJECT_NAME})

if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
endif()

# Repo-root documentation path for the ErrorCatalog doc validator test (task 11.1).
# The committed "通用错误码" section of docs/backend/api-reference.md is validated
# against ErrorCatalog::allEntries()/allOAuthEntries() at test time. The test runs
# from the test executable directory, so the absolute source path is injected here
# (CMake normalizes the path with forward slashes, usable by std::ifstream on all
# platforms). Defined as an absolute path derived from this CMakeLists location.
get_filename_component(OAUTH2_REPO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "OAUTH2_DOC_API_REFERENCE=\"${OAUTH2_REPO_ROOT_DIR}/docs/backend/api-reference.md\"")

# Repo-source golden-snapshot directory for the success-response / route-manifest
# regression test (task 12.4). The committed golden files under
# OAuth2Server/test/integration/error/golden/ are read (and self-seeded on first
# run) at test time. Like OAUTH2_DOC_API_REFERENCE above, the test runs from the
# test executable directory, so the absolute source path is injected here.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "OAUTH2_GOLDEN_DIR=\"${OAUTH2_REPO_ROOT_DIR}/OAuth2Server/test/integration/error/golden\"")

# Coverage support
if(OAUTH2_TEST_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE --coverage -fno-inline -fno-inline-small-functions -fno-default-inline)
        target_link_options(${PROJECT_NAME} PRIVATE --coverage)
        message(STATUS "Code coverage enabled for ${PROJECT_NAME}")
    endif()
endif()

add_test(NAME OAuth2Tests COMMAND ${PROJECT_NAME} WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>)

# --- Contract test suite (Task 12, design.md §7.3 / F5): CTest label "Contract" ---
#
# DROGON_TEST's own `-r <name>` test runner does an EXACT name match (see
# drogon_test.cc's `test->name() == targetTest`), not a prefix/substring
# match -- there is no way to select "every test whose name starts with
# Contract_" in one process invocation the way the naming convention might
# suggest. To still get a `ctest -L Contract` entry point (per the task's
# acceptance criterion), each contract DROGON_TEST case below is registered
# as its own individual add_test() (same pattern the rest of this project's
# CI already uses to run named tests individually, e.g. the
# "Run Error-Standardization Backend Tests" step in ci-linux.yml -- this
# just does the equivalent registration at the CMake/CTest level instead of
# a shell script loop), all tagged with LABELS "Contract" so `ctest -L
# Contract` runs exactly this set, `ctest -L Contract -E Postgres` can
# exclude backend tiers, etc.
set(OAUTH2_CONTRACT_TEST_NAMES
    # ClientRepositoryContractTest.cc
    Integration_P0_Contract_Functional_ClientRepository_Postgres_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_ClientRepository_Postgres_PublicClientAcceptsAnySecret
    Integration_P0_Contract_Functional_ClientRepository_Postgres_ConfidentialClientValidatesSecret
    Integration_P0_Contract_Functional_ClientRepository_Redis_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_ClientRepository_Redis_KnownClientValidatesSecret
    Integration_P0_Contract_Functional_ClientRepository_Memory_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_ClientRepository_Memory_PublicClientAcceptsAnySecret
    Integration_P0_Contract_Functional_ClientRepository_Memory_ConfidentialClientValidatesSecret
    # GrantRepositoryContractTest.cc
    Integration_P0_Contract_Functional_GrantRepository_Postgres_SaveGetRoundTrip
    Integration_P0_Contract_Functional_GrantRepository_Postgres_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_GrantRepository_Postgres_ConsumeAuthCode_CorrectRedirectUriSucceeds
    Integration_P0_Contract_Functional_GrantRepository_Postgres_ConsumeAuthCode_WrongRedirectUriFails
    Integration_P0_Contract_Functional_GrantRepository_Postgres_ConsumeAuthCode_SingleUse
    Integration_P0_Contract_Functional_GrantRepository_Redis_SaveGetRoundTrip
    Integration_P0_Contract_Functional_GrantRepository_Redis_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_GrantRepository_Redis_ConsumeAuthCode_CorrectRedirectUriSucceeds
    Integration_P0_Contract_Functional_GrantRepository_Redis_ConsumeAuthCode_WrongRedirectUriFails
    Integration_P0_Contract_Functional_GrantRepository_Redis_ConsumeAuthCode_SingleUse
    Integration_P0_Contract_Functional_GrantRepository_Memory_SaveGetRoundTrip
    Integration_P0_Contract_Functional_GrantRepository_Memory_NotFoundReturnsNullopt
    Integration_P0_Contract_Functional_GrantRepository_Memory_ConsumeAuthCode_CorrectRedirectUriSucceeds
    Integration_P0_Contract_Functional_GrantRepository_Memory_ConsumeAuthCode_WrongRedirectUriFails
    Integration_P0_Contract_Functional_GrantRepository_Memory_ConsumeAuthCode_SingleUse
    # TokenRepositoryContractTest.cc
    Integration_P0_Contract_Functional_TokenRepository_Postgres_AccessTokenSaveGetRoundTrip
    Integration_P0_Contract_Functional_TokenRepository_Postgres_AccessTokenNotFound
    Integration_P0_Contract_Functional_TokenRepository_Redis_AccessTokenSaveGetRoundTrip
    Integration_P0_Contract_Functional_TokenRepository_Redis_AccessTokenNotFound
    Integration_P0_Contract_Functional_TokenRepository_Memory_AccessTokenSaveGetRoundTrip
    Integration_P0_Contract_Functional_TokenRepository_Memory_AccessTokenNotFound
    Integration_P0_Contract_Functional_TokenRepository_Postgres_RefreshTokenSaveGetRoundTrip
    Integration_P0_Contract_Functional_TokenRepository_Memory_RefreshTokenSaveGetRoundTrip
    Integration_P0_Contract_Functional_TokenRepository_Redis_RefreshTokenSaveIsNoOp_GetAlwaysNullopt
    Integration_P0_Contract_Functional_TokenRepository_Postgres_RevokeRefreshToken_GetReturnsRevokedFlagTrue
    Integration_P0_Contract_Functional_TokenRepository_Memory_RevokeRefreshToken_GetReturnsNullopt
    Integration_P0_Contract_Functional_TokenRepository_Memory_ExpiredAccessToken_GetReturnsNullopt
    Integration_P0_Contract_Functional_TokenRepository_Postgres_ExpiredAccessToken_StillReturnedByGet_NoActiveExpiryCheck
    Integration_P0_Contract_Atomicity_TokenRepository_Postgres_AtomicRevokeRefreshToken_ConcurrentCas
    Integration_P0_Contract_Atomicity_TokenRepository_Memory_AtomicRevokeRefreshToken_ConcurrentCas
    Integration_P0_Contract_Atomicity_TokenRepository_Redis_AtomicRevokeRefreshToken_CasTierSkipped
    Integration_P0_Contract_Atomicity_TokenRepository_Postgres_SaveTokenPair_HappyPathBothWritesSucceed
    Integration_P0_Contract_Atomicity_TokenRepository_Postgres_SaveTokenPair_AccessTokenConflict_RefreshTokenNotPersisted
    Integration_P0_Contract_Atomicity_TokenRepository_Memory_SaveTokenPair_HappyPathBothWritesSucceed
    # ConsentRepositoryContractTest.cc
    Integration_P0_Contract_Functional_ConsentRepository_Postgres_SaveHasRevokeRoundTrip
    Integration_P0_Contract_Functional_ConsentRepository_Redis_SaveHasRevokeRoundTrip
    Integration_P0_Contract_Functional_ConsentRepository_Memory_SaveHasRevokeRoundTrip
)

foreach(_contract_test_name ${OAUTH2_CONTRACT_TEST_NAMES})
    add_test(
        NAME "Contract.${_contract_test_name}"
        COMMAND ${PROJECT_NAME} -r ${_contract_test_name}
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    set_tests_properties("Contract.${_contract_test_name}" PROPERTIES LABELS "Contract")
endforeach()

# Copy config to the test executable directory
if(MSVC OR XCODE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../config.json
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json
    )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../config.json
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json
    )
endif()
