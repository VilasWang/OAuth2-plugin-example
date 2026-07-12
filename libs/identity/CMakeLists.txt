# authforge-identity library
cmake_minimum_required(VERSION 3.20)

project(authforge-identity LANGUAGES CXX)

# M5 Task 31: WITH_WEBAUTHN / WITH_SOCIAL are now declared at the TOP-LEVEL
# CMakeLists.txt (and threaded from conanfile.py's with_* options). The
# option() calls below are no-ops when the variable is already set, kept so
# this library still configures standalone (add_subdirectory from a foreign
# root, per OAuth2Plugin/CMakeLists.txt's self-contained-bootstrap pattern).
option(WITH_WEBAUTHN "Build WebAuthn identity module" ON)
option(WITH_SOCIAL "Build social auth identity module" ON)

# Find dependencies
#
# Task 19 (authforge-sdk-refactor, design.md §4.1 rule 1): this is a
# Domain-layer package -- it must not depend on Drogon. No source file
# under libs/identity/src includes any <drogon/...> header (verified by
# grep); jsoncpp alone (used directly for Json::Value in
# IUserRepository.h/AuthService.h) is sufficient, matching libs/common's
# and libs/oauth2's own "允许依赖 jsoncpp" allowance.
find_package(jsoncpp REQUIRED)

# M2.5 identity completion: TotpUtils.cc calls OpenSSL's HMAC directly
# (RFC 6238 TOTP over HMAC-SHA1), mirroring libs/oauth2's own
# find_package(OpenSSL REQUIRED) + OpenSSL::Crypto link for JwkManager --
# design.md §4.1 rule 1 allows OpenSSL in the Domain layer as the one
# non-Drogon exception already established by libs/common's
# ICryptoProvider adapters.
find_package(OpenSSL REQUIRED)

# Sources
#
# Task 19 (authforge-sdk-refactor): RoleProvider/SubjectResolver/
# UserInfoProvider are header-only (no corresponding .cc) -- each is a
# thin one-method forwarding adapter over an identity repository
# interface, same pattern as OAuth2Plugin's existing
# adapters/StorageRoleProvider.h. AuthService.cc/MfaService.cc/
# WebAuthnService.cc/social/*.cc are real (non-placeholder)
# implementations.
set(IDENTITY_SOURCES
    src/AuthService.cc
    # MFA
    src/mfa/MfaService.cc
    src/mfa/TotpUtils.cc
    # WebAuthn (conditional)
    $<$<BOOL:${WITH_WEBAUTHN}>:src/webauthn/WebAuthnService.cc>
    # Social (conditional)
    $<$<BOOL:${WITH_SOCIAL}>:src/social/GoogleAuthService.cc>
    $<$<BOOL:${WITH_SOCIAL}>:src/social/WeChatAuthService.cc>
    $<$<BOOL:${WITH_SOCIAL}>:src/social/GitHubAuthService.cc>
    # Session
    src/session/SessionManager.cc
)

# Headers
set(IDENTITY_HEADERS
    include/authforge/identity/AuthService.h
    include/authforge/identity/IUserRepository.h
    include/authforge/identity/IRoleRepository.h
    include/authforge/identity/ISubjectMappingRepository.h
    include/authforge/identity/IMfaRepository.h
    include/authforge/identity/RoleProvider.h
    include/authforge/identity/SubjectResolver.h
    include/authforge/identity/UserInfoProvider.h
    include/authforge/identity/MfaService.h
    include/authforge/identity/TotpUtils.h
    include/authforge/identity/SessionManager.h
    include/authforge/identity/IBackchannelLogoutNotifier.h
    $<$<BOOL:${WITH_WEBAUTHN}>:include/authforge/identity/WebAuthnService.h>
    $<$<BOOL:${WITH_WEBAUTHN}>:include/authforge/identity/IWebAuthnRepository.h>
    $<$<BOOL:${WITH_SOCIAL}>:include/authforge/identity/SocialAuthService.h>
    $<$<BOOL:${WITH_SOCIAL}>:include/authforge/identity/IOAuthHttpClient.h>
    $<$<BOOL:${WITH_SOCIAL}>:include/authforge/identity/ISocialAccountRepository.h>
)

add_library(authforge-identity STATIC
    ${IDENTITY_SOURCES}
)
add_library(authforge::identity ALIAS authforge-identity)

target_include_directories(authforge-identity
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(authforge-identity
    PUBLIC
        authforge-common
    PRIVATE
        JsonCpp::JsonCpp
        OpenSSL::Crypto
)

# Compile definitions for conditional features
if(WITH_WEBAUTHN)
    target_compile_definitions(authforge-identity PUBLIC WITH_WEBAUTHN)
endif()

if(WITH_SOCIAL)
    target_compile_definitions(authforge-identity PUBLIC WITH_SOCIAL)
endif()

# Install rules
install(TARGETS authforge-identity
    EXPORT authforge-identity-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT authforge-identity-targets
    FILE authforge-identityConfig.cmake
    NAMESPACE authforge::
    DESTINATION lib/cmake/authforge-identity
)

# --- Tests (pure unit, no DB/no Drogon) ---
# Task 19: mirrors libs/common/test's and libs/oauth2/test's rationale
# (design.md §6) -- gtest, not DROGON_TEST, since this Domain-layer
# target must not depend on Drogon.
if(NOT COMMAND oauth2_apply_compat)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(Compatibility)
endif()
add_subdirectory(test)
