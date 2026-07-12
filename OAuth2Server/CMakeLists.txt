cmake_minimum_required(VERSION 3.16)
project(OAuth2Server VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

# Find Dependencies
find_package(Drogon CONFIG REQUIRED)
# libcurl: OAuth2Plugin is an OBJECT library, so the SMTP code in
# EmailService.cc is compiled into THIS executable; link libcurl here too.
find_package(CURL REQUIRED)

# M3 Task 25 (authforge-sdk-refactor, design.md §6): main.cc's former
# monolithic body has been split into apps/server-style bootstrap/ modules
# (CorsSetup/SecurityHeaders/ExceptionHandlerSetup/OpenApiSetup/
# MigrationRunner/ControllerRegistration); main() itself now only wires
# them together.
file(GLOB BOOTSTRAP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/*.cc")

# M5 Task 30 (authforge-sdk-refactor): product-level Organization management
# lives under apps/server/src/organization/ (design.md §5.4: Organization CRUD
# is a PRODUCT concern, not part of the reusable SDK). Same product-app
# compilation model as bootstrap/ -- globbed into the server executable.
file(GLOB ORGANIZATION_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/organization/*.cc")

add_executable(${PROJECT_NAME}
    main.cc
    SchemaManager.cc
    ${BOOTSTRAP_SRC}
    ${ORGANIZATION_SRC}
)

# M3 Task 20 (authforge-sdk-refactor): OAuth2Server/controllers/* and
# OAuth2Server/filters/* have all been relocated into libs/drogon
# (authforge::drogon::controllers / authforge::drogon::filters). These two
# directories are now empty (kept for potential future product-specific
# controllers); aux_source_directory returns an empty list here, which is
# harmless.
aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)

# Views
drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})

target_sources(${PROJECT_NAME} PRIVATE 
    ${CTL_SRC} 
    ${FILTER_SRC}
)

# Link against the Plugin library (defined in root CMakeLists.txt)
# authforge::drogon (M3 Task 20, verified mechanism -- see PROGRESS.md):
# linked as a PLAIN static library dependency, NOT whole-archive. All 15
# controllers + 2 filters + AuthService now live there with
# AutoCreation=false + explicit drogon::app().registerController() calls
# in main.cc (below), which establishes a real linker-visible reference so
# each controller's .obj is not dropped even without whole-archive -- this
# was verified end-to-end with a running OAuth2Server.exe and real HTTP
# requests (/health, /login, /api/admin/dashboard, /api/google/login all
# behave correctly).
target_link_libraries(${PROJECT_NAME} PRIVATE OAuth2Plugin authforge::drogon CURL::libcurl)

# Additional includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/organization
)

# Compiler compatibility (shared module)
oauth2_apply_compat(${PROJECT_NAME})

# Copy Swagger UI to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../docs/backend/api/swagger-ui"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/docs/api/swagger-ui"
)

# Enable testing
enable_testing()
add_subdirectory(test)
