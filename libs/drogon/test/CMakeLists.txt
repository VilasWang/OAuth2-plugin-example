cmake_minimum_required(VERSION 3.16)

# libs/drogon/test -- Adapter-layer unit tests. Unlike the Domain-layer test
# dirs (libs/{common,oauth2,identity}/test) these tests DO link Drogon (via
# fulla::drogon), because the units under test (e.g.
# BackchannelLogoutNotifier) are Drogon-coupled adapters. gtest (not
# DROGON_TEST) so binaries run with no event loop; tests here must not require
# a running server or database (DB-touching paths are covered by the
# integration tests under tests/integration/).

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE FULLA_DROGON_TEST_SOURCES "*.cc")

add_executable(fulla-drogon-test ${FULLA_DROGON_TEST_SOURCES})

target_link_libraries(fulla-drogon-test
    PRIVATE
        fulla::drogon
        fulla::common::testing
        GTest::gtest
)

# NOTE: deliberately NOT linking GTest::Main. fulla::drogon pulls Boost
# transitively, whose umbrella links Boost::test_exec_monitor (a main() that
# calls test_main). With GTest::Main (a .lib), MSVC can resolve main() from
# the boost lib first, leaving test_main undefined. Providing main() as a
# compiled object below (processed before any .lib) -- exactly how the main
# app's main.cc avoids this -- sidesteps it.

set_target_properties(fulla-drogon-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

oauth2_apply_compat(fulla-drogon-test)
oauth2_apply_gcov(fulla-drogon-test)

include(GoogleTest)
gtest_discover_tests(fulla-drogon-test)
