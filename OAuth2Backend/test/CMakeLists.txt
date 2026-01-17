cmake_minimum_required(VERSION 3.5)
project(OAuth2Test_test CXX)

# Collect source files
file(GLOB TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
# set(TEST_SRC "test_main.cc" "MemoryStorageTest.cc" "PluginTest.cc" "ConfigTest.cc" "RedisStorageTest.cc" "PostgresStorageTest.cc" "AdvancedStorageTest.cc") # DEBUG: Isolate ConfigTest
file(GLOB PLUGIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/*.cc")
# file(GLOB CTL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../controllers/*.cc") # Controller tests require views/mocking
# file(GLOB MODEL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../models/*.cc") # Uncomment if models exist

add_executable(${PROJECT_NAME} ${TEST_SRC} ${PLUGIN_SRC})

# Include parent project directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/../models
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
# If using shared metrics or other libs, link them here too

ParseAndAddDrogonTests(${PROJECT_NAME})
