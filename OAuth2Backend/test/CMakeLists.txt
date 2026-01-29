cmake_minimum_required(VERSION 3.5)
project(OAuth2Test_test CXX)

# Collect source files
file(GLOB TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
# set(TEST_SRC "test_main.cc" "MemoryStorageTest.cc" "PluginTest.cc" "ConfigTest.cc" "RedisStorageTest.cc" "PostgresStorageTest.cc" "AdvancedStorageTest.cc") # DEBUG: Isolate ConfigTest
file(GLOB PLUGIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/*.cc")
file(GLOB STORAGE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../storage/*.cc")
file(GLOB SERVICE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../services/*.cc")
file(GLOB CTL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../controllers/*.cc")
file(GLOB FILTER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../filters/*.cc")
file(GLOB MODEL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../models/*.cc") # Uncomment if models exist

# Explicitly list sources to avoid picking up broken UserTest.cc
set(TEST_SRC 
    "test_main.cc" 
    "MemoryStorageTest.cc" 
    "PluginTest.cc" 
    "ConfigTest.cc" 
    "RedisStorageTest.cc" 
    "PostgresStorageTest.cc" 
    "AdvancedStorageTest.cc"
    "SchemaSetup.cc"
    "UserTest.cc"
    "IntegrationE2ETest.cc"
    "RateLimiterTest.cc"
    "EnvConfigTest.cc"
)

add_executable(${PROJECT_NAME} ${TEST_SRC} ${PLUGIN_SRC} ${STORAGE_SRC} ${SERVICE_SRC} ${MODEL_SRC} ${CTL_SRC} ${FILTER_SRC})

# Include parent project directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/../storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../services
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/../models
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
# If using shared metrics or other libs, link them here too

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /FI"${CMAKE_CURRENT_SOURCE_DIR}/../models/orm_compat.h")
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

# ParseAndAddDrogonTests(${PROJECT_NAME})
add_test(NAME OAuth2Tests COMMAND ${PROJECT_NAME} WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>)
