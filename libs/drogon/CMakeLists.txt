cmake_minimum_required(VERSION 3.16)

# libs/drogon (authforge::drogon) -- M3 Task 20, authforge-sdk-refactor
# (design.md §6 "libs/drogon # authforge::drogon [drogon 绑定] ...
# 插件(装配器)/controllers/filters/views"; §5.5 "Drogon 绑定 |
# authforge::drogon | libs/drogon | 是 | Drogon 插件（DI 装配器）、
# controller、filter、view；含 Drogon 自注册符号，消费者链接须
# whole-archive").
#
# This is the Adapter-layer package that holds Drogon-coupled code (the
# plugin degraded to a thin assembler, controllers, filters, views) that
# libs/common/libs/oauth2/libs/identity (the Domain layer) are forbidden
# from depending on. Unlike those three, THIS package is explicitly
# ALLOWED to depend on Drogon (design.md §4.1 rule 1 only forbids Drogon
# in the Domain layer; libs/drogon and libs/storage-* are the designated
# Adapter-layer homes for it).
#
# Scope note (Task 20 slice 1): only the three oauth2::validation::*
# classes (Rules/RuleEngine/RuleSet/HttpResponder) are relocated so far --
# these have zero dependency on the OAuth2Plugin singleton
# (drogon::app().getPlugin<OAuth2Plugin>()) or any Domain service, so they
# are the lowest-risk first slice of the M3 Drogon-binding migration
# (mirrors Task 14/17's own "smallest independently-buildable increment"
# discipline, design.md's H6 guidance). The controllers/filters/plugin
# itself (which DO reach through the OAuth2Plugin singleton) are follow-up
# slices, coordinated with Task 21 (plugin registration) / Task 23
# (de-singletonization) so the migration order doesn't create a
# circular/premature dependency on work those tasks haven't done yet.
#
# Namespace: authforge::drogon::validation (design.md §6 namespace sync).
# The old oauth2::validation:: location becomes a compatibility shim (same
# pattern as libs/oauth2's JwkManager.h shim from Task 17 slice 10) so
# existing call sites (RequestValidationFilter, OAuth2StandardController,
# SessionController, and the unit tests under
# OAuth2Server/test/unit/validation/) keep compiling unchanged until the
# full M8 namespace sweep (Task 40).
#
# HttpResponder.cc still depends on the pre-existing common::error
# machinery (OAuth2Plugin/include/oauth2/error/*, not yet relocated into
# authforge::common::error) -- this target therefore links OAuth2Plugin
# for that header+impl, same as e.g. libs/storage-postgres links
# authforge::identity for its own Adapter-side implementation. This is a
# TEMPORARY reverse-looking dependency edge (Adapter -> Adapter) that
# collapses once the error/ module's own relocation (a separate, later
# slice/task) lands; it does not violate the Domain-layer rule since
# libs/drogon is itself an Adapter-layer package.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
if(NOT DEFINED OAUTH2_PROJECT_VERSION)
    set(OAUTH2_PROJECT_VERSION 1.0.0)
endif()
if(NOT COMMAND oauth2_apply_compat)
    include(Compatibility)
endif()

project(authforge-drogon VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX)

find_package(Drogon CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC)
add_library(authforge::drogon ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

file(GLOB_RECURSE AUTHFORGE_DROGON_SOURCES "src/*.cc")
target_sources(${PROJECT_NAME} PRIVATE ${AUTHFORGE_DROGON_SOURCES})

# OAuth2Plugin (temporary, see header note above): HttpResponder.cc calls
# the pre-existing common::error::{Error,ErrorContext,ErrorHandler,
# RequestId} machinery, whose headers+impl still live under OAuth2Plugin.
#
# Task 24 slice 5: DrogonOAuthHttpClient.cc implements
# authforge::identity::IOAuthHttpClient -- authforge::identity linked
# explicitly here (previously only reached transitively via OAuth2Plugin
# -> authforge::storage::postgres -> authforge::identity) so the
# WITH_SOCIAL PUBLIC compile definition it carries is visible to this
# target without relying on that transitive chain's continued existence.
target_link_libraries(${PROJECT_NAME} PUBLIC Drogon::Drogon OAuth2Plugin authforge::identity)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

oauth2_apply_compat(${PROJECT_NAME})

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    EXPORT AuthForgeDrogonTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/authforge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT AuthForgeDrogonTargets
    FILE authforge-drogonConfig.cmake
    NAMESPACE authforge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/authforge-drogon
)
