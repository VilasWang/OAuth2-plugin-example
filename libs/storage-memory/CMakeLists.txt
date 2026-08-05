cmake_minimum_required(VERSION 3.16)

# libs/storage-memory (authforge::storage::memory) -- M8 Task 40 (authforge-sdk-refactor,
# design.md §5.4/§5.5/§6). Holds the 4 in-memory oauth2 repository implementations
# (Memory{Client,Grant,Token,Consent}Repository implementing
# authforge::oauth2::repository::*) relocated out of OAuth2Plugin/storage/. The 3
# identity memory repos (Memory{Role,User,SubjectMapping}Repository, which implement
# the legacy ::oauth2::I*Repository interfaces still owned by OAuth2Plugin) and the
# MemoryRepositoryBundle aggregator STAY under OAuth2Plugin/storage/ until Task 39
# migrates the identity interfaces into libs/identity (moving them now would create a
# circular storage-memory <-> OAuth2Plugin dependency).
#
# These 4 repos use Drogon's LOG_* macros for diagnostics, so this package links
# Drogon (like storage-postgres).

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
if(NOT DEFINED OAUTH2_PROJECT_VERSION)
    set(OAUTH2_PROJECT_VERSION 1.0.0)
endif()
if(NOT COMMAND oauth2_apply_compat)
    include(Compatibility)
endif()
if(NOT COMMAND oauth2_apply_warnings)
    include(Warnings)
endif()

project(authforge-storage-memory VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC)
add_library(authforge::storage::memory ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

file(GLOB AUTHFORGE_STORAGE_MEMORY_SOURCES "src/*.cc")
target_sources(${PROJECT_NAME} PRIVATE ${AUTHFORGE_STORAGE_MEMORY_SOURCES})

# Memory repositories implement authforge::oauth2::repository::* interfaces
# (IClientRepository/IGrantRepository/ITokenRepository/IConsentRepository), so this
# package depends on the Domain-layer libs/oauth2 (correct direction per design.md
# §4.1: an Adapter-layer storage package is free to implement Domain interfaces;
# the Domain has zero dependency back).
#
# Phase 1.5c (Task 39): MemoryIdentityRepository implements the identity
# repository interfaces (IUser/IRole/ISubjectMapping), so this package also
# depends on libs/identity (same Adapter->Domain direction).
target_link_libraries(${PROJECT_NAME} PUBLIC Drogon::Drogon JsonCpp::JsonCpp authforge::oauth2 authforge::identity)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

oauth2_apply_compat(${PROJECT_NAME})
oauth2_apply_warnings(${PROJECT_NAME})
oauth2_apply_gcov(${PROJECT_NAME})

include(GNUInstallDirs)
install(DIRECTORY include/authforge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# SDK packaging: install-tree + build-tree find_package() config carrying the
# transitive find_dependency() closure (authforge-sdk-refactor Task 28a).
if(NOT COMMAND authforge_package)
    include(AuthForgePackage)
endif()
authforge_package(
    TARGET ${PROJECT_NAME}
    PACKAGE authforge-storage-memory
    EXPORT_NAME storage::memory
    DEPENDENCIES Drogon jsoncpp authforge-oauth2 authforge-identity
)
