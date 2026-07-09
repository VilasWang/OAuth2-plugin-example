cmake_minimum_required(VERSION 3.16)

# libs/oauth2/test -- pure unit tests, no DB/no Drogon (mirrors
# libs/common/test's rationale: design.md §6 keeps Domain-layer tests on
# gtest, not the DROGON_TEST framework the rest of this repo's backend
# tests use, since DROGON_TEST requires linking Drogon
# (<drogon/drogon_test.h>), which this target must not depend on per
# design.md §4.1 rule 1).

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE AUTHFORGE_OAUTH2_TEST_SOURCES "*.cc")

add_executable(authforge-oauth2-test ${AUTHFORGE_OAUTH2_TEST_SOURCES})

target_link_libraries(authforge-oauth2-test
    PRIVATE
        authforge::oauth2
        authforge::common::testing
        GTest::gtest
        GTest::Main
)

set_target_properties(authforge-oauth2-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

oauth2_apply_compat(authforge-oauth2-test)

include(GoogleTest)
gtest_discover_tests(authforge-oauth2-test)
