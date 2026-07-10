# authforge-identity library
cmake_minimum_required(VERSION 3.20)

project(authforge-identity LANGUAGES CXX)

# M2.5 identity completion (WebAuthn slice): WITH_WEBAUTHN was previously
# referenced (src/webauthn/WebAuthnService.cc's `#ifdef WITH_WEBAUTHN`
# guard, this file's own $<BOOL:${WITH_WEBAUTHN}> generator expressions)
# but never `option()`'d anywhere in the top-level build -- meaning the
# variable was always unset/OFF and src/webauthn/WebAuthnService.cc never
# actually compiled. Declaring the option here (defaulted ON) is what
# makes the real (non-placeholder) WebAuthnService implementation below
# actually build and get tested. Threading this through conanfile.py's
# own `with_webauthn` option is separate, out-of-scope work (tasks.md
# Task 31).
option(WITH_WEBAUTHN "Build WebAuthn identity module" ON)

# M2.5 identity completion (Social auth slice): same rationale as
# WITH_WEBAUTHN above -- WITH_SOCIAL was previously referenced
# (src/social/*.cc's `#ifdef WITH_SOCIAL` guards, this file's own
# $<BOOL:${WITH_SOCIAL}> generator expressions) but never `option()`'d
# anywhere in the top-level build, so src/social/*.cc never actually
# compiled. Declaring the option here (defaulted ON) is what makes the
# real (non-placeholder) Google/WeChat/GitHub auth service
# implementations below actually build and get tested. Threading this
# through conanfile.py's own `with_social` option is separate,
# out-of-scope work (tasks.md Task 31).
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
