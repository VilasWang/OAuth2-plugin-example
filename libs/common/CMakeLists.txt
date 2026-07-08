cmake_minimum_required(VERSION 3.16)

# libs/common (authforge::common) -- Task 13, authforge-sdk-refactor
# (design.md §4.1/§5.1/§6). Domain-layer shared kernel: Result<T,Error>,
# framework-agnostic ErrorCatalog/ErrorEnvelope, value objects (Subject,
# Scope, ClientId, RedirectUri, PkceChallenge, TokenValue, TenantId), ports
# (ISubjectResolver, IRoleProvider, IUserInfoProvider, IClock,
# ICryptoProvider, IUuidGenerator, IEmailSender, ILogger, IMetrics), and the
# AuditEvent observability model.
#
# Dependency rule (design.md §4.1 rule 1): this target MUST NOT depend on
# Drogon. It is allowed to depend on jsoncpp (Json::Value appears in
# Error::toJson/AuditEvent::details/IUserInfoProvider's claims bag).
#
# Self-contained bootstrap: mirrors OAuth2Plugin/CMakeLists.txt's pattern so
# this directory can also be added standalone (e.g. from
# examples/third-party-host in a later milestone) without requiring the
# repo root's shared CMake modules to already be included.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
if(NOT DEFINED OAUTH2_PROJECT_VERSION)
    set(OAUTH2_PROJECT_VERSION 1.0.0)
endif()
if(NOT COMMAND oauth2_apply_compat)
    include(Compatibility)
endif()

project(authforge-common VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

find_package(jsoncpp CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC)
add_library(authforge::common ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

file(GLOB_RECURSE AUTHFORGE_COMMON_SOURCES "src/*.cc")
target_sources(${PROJECT_NAME} PRIVATE ${AUTHFORGE_COMMON_SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC JsonCpp::JsonCpp)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Compiler compatibility (shared module) -- same convention as
# OAuth2Plugin/OAuth2Server, even though libs/common's own sources do not
# use ORM-generated code; keeps compile flags (e.g. /utf-8 on MSVC)
# consistent across every target in the tree.
oauth2_apply_compat(${PROJECT_NAME})

# --- arch-guard note (enforced by tooling in a later M6 task, Task 33) ---
# This target must never gain a Drogon dependency. There is no automated
# check yet (tools/arch-guard is a later milestone); until then, this is a
# manually-enforced rule reviewers should watch for on any PR touching
# libs/common.

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    EXPORT AuthForgeCommonTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/authforge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT AuthForgeCommonTargets
    FILE authforge-commonConfig.cmake
    NAMESPACE authforge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/authforge-common
)

# --- Tests (pure unit, no DB/no Drogon; design.md §6's libs/common/test/) ---
add_subdirectory(test)
