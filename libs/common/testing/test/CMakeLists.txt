cmake_minimum_required(VERSION 3.16)

# libs/common/testing/test -- pure gtest unit tests proving the fake
# implementations behave deterministically (Task 15's acceptance
# criterion: "Domain 可用假时钟/假 crypto 做确定性单测").

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE AUTHFORGE_COMMON_TESTING_TEST_SOURCES "*.cc")

add_executable(authforge-common-testing-test ${AUTHFORGE_COMMON_TESTING_TEST_SOURCES})

target_link_libraries(authforge-common-testing-test
    PRIVATE authforge::common::testing GTest::gtest GTest::Main
)

set_target_properties(authforge-common-testing-test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
)

oauth2_apply_compat(authforge-common-testing-test)
oauth2_apply_gcov(authforge-common-testing-test)

include(GoogleTest)
gtest_discover_tests(authforge-common-testing-test)
