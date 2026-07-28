cmake_minimum_required(VERSION 3.16)
# Phase 5 (A7, directory restructure): OAuth2Server/ became apps/server/ and
# the target/binary was renamed OAuth2Server -> authforge-server. Sources now
# live under src/ (main.cc, SchemaManager.*, bootstrap/, organization/);
# config files under config/; versioned SQL under migrations/ + seed/.
project(authforge-server VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

# Find Dependencies
find_package(Drogon CONFIG REQUIRED)
# libcurl: kept explicit (also reaches here transitively via
# authforge::drogon since Phase 4 merged the old OAuth2Plugin OBJECT lib
# into that STATIC lib, PUBLIC-linking CURL::libcurl).
find_package(CURL REQUIRED)

# M3 Task 25 (authforge-sdk-refactor, design.md §6): main.cc's former
# monolithic body has been split into apps/server-style bootstrap/ modules
# (CorsSetup/SecurityHeaders/ExceptionHandlerSetup/OpenApiSetup/
# MigrationRunner/ControllerRegistration); main() itself now only wires
# them together.
file(GLOB BOOTSTRAP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/bootstrap/*.cc")

# M5 Task 30 (authforge-sdk-refactor): product-level Organization management
# lives under apps/server/src/organization/ (design.md §5.4: Organization CRUD
# is a PRODUCT concern, not part of the reusable SDK). Same product-app
# compilation model as bootstrap/ -- globbed into the server executable.
file(GLOB ORGANIZATION_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/organization/*.cc")

add_executable(${PROJECT_NAME}
    src/main.cc
    src/SchemaManager.cc
    ${BOOTSTRAP_SRC}
    ${ORGANIZATION_SRC}
)

# Views
drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})

# authforge::drogon (M3 Task 20, verified mechanism -- see PROGRESS.md):
# linked as a PLAIN static library dependency, NOT whole-archive. All 15
# controllers + 2 filters + AuthService now live there with
# AutoCreation=false + explicit drogon::app().registerController() calls
# in main.cc (below), which establishes a real linker-visible reference so
# each controller's .obj is not dropped even without whole-archive -- this
# was verified end-to-end with a running OAuth2Server.exe and real HTTP
# requests (/health, /login, /api/admin/dashboard, /api/google/login all
# behave correctly).
# Phase 4 (A6): the bare OAuth2Plugin link is gone -- the plugin class
# merged into authforge::drogon; bootstrap's getPlugin<::OAuth2Plugin>()
# calls reference its symbols, which keeps the plugin TU (and its Drogon
# self-registration) alive out of the static lib.
target_link_libraries(${PROJECT_NAME} PRIVATE authforge::drogon CURL::libcurl)

# Additional includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/organization
)

# Compiler compatibility (shared module)
oauth2_apply_compat(${PROJECT_NAME})

# Copy Swagger UI to build directory (repo-root docs/, two levels up from
# apps/server/)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/backend/api/swagger-ui"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/docs/api/swagger-ui"
)

# Tests moved to top-level tests/ (Phase 7); mounted from the root
# CMakeLists.txt via add_subdirectory(tests).
