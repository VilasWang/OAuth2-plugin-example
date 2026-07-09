cmake_minimum_required(VERSION 3.16)

# libs/identity/test -- pure unit tests, no DB/no Drogon (mirrors
# libs/common/test's and libs/oauth2/test's rationale: design.md §6 keeps
# Domain-layer tests on gtest, not DROGON_TEST, since DROGON_TEST requires
# linking Drogon, which this target must not depend on per design.md §4.1
# rule 1).

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE AUTHFORGE_IDENTITY_TEST_SOURCES "*.cc")

add_executable(authforge-identity-test ${AUTHFORGE_IDENTITY_TEST_SOURCES})

target_link_libraries(authforge-identity-test
    PRIVATE
        authforge::identity
        authforge::common::testing
        GTest::gtest
        GTest::Main
)

set_target_properties(authforge-identity-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

oauth2_apply_compat(authforge-identity-test)

include(GoogleTest)
gtest_discover_tests(authforge-identity-test)
