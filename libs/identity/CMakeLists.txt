# authforge-identity library
cmake_minimum_required(VERSION 3.20)

project(authforge-identity LANGUAGES CXX)

# Find dependencies
find_package(Drogon REQUIRED)
find_package(jsoncpp REQUIRED)

# Sources
set(IDENTITY_SOURCES
    src/AuthService.cc
    src/rbac/RoleProvider.cc
    src/SubjectResolver.cc
    src/UserInfoProvider.cc
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
    include/authforge/identity/IRoleProvider.h
    include/authforge/identity/ISubjectResolver.h
    include/authforge/identity/IUserInfoProvider.h
    include/authforge/identity/MfaService.h
    include/authforge/identity/TotpUtils.h
    include/authforge/identity/SessionManager.h
    $<$<BOOL:${WITH_WEBAUTHN}>:include/authforge/identity/WebAuthnService.h>
    $<$<BOOL:${WITH_SOCIAL}>:include/authforge/identity/SocialAuthService.h>
)

add_library(authforge-identity STATIC
    ${IDENTITY_SOURCES}
)

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
        Drogon::Drogon
        JsonCpp::JsonCpp
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
