# cmake/Warnings.cmake
# Warning-level governance for first-party targets (warning phase 2).
#
# Provides function oauth2_apply_warnings(target) which raises the warning
# level on FIRST-PARTY code only:
#   - MSVC:      /W4, plus /external:anglebrackets /external:W0 so headers
#                pulled in from Conan dependencies (Drogon/Trantor/OpenSSL/
#                jsoncpp) do not add third-party noise at /W4
#   - GCC/Clang: -Wall -Wextra (third-party includes already arrive as
#                SYSTEM includes via the imported targets, so they are
#                exempt from these flags)
#
# Option AUTHFORGE_WERROR (default OFF) additionally promotes warnings to
# errors (/WX resp. -Werror). It stays OFF for local developer builds; the
# CI presets turn it ON now that the warning baseline is clean.
#
# Function-style (NOT an INTERFACE library) for the same reason as
# oauth2_apply_compat in Compatibility.cmake: an INTERFACE target that is
# PRIVATE-linked into the exported static libs would be dragged into
# install(EXPORT) and pollute the authforge-*Config.cmake consumer surface.
# All flags below are PRIVATE and leave zero footprint on SDK consumers.

option(AUTHFORGE_WERROR
    "Treat compiler warnings as errors on first-party AuthForge targets" OFF)

function(oauth2_apply_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "oauth2_apply_warnings: target '${target}' does not exist")
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /external:anglebrackets
            /external:W0
        )
        if(AUTHFORGE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        # -Wno-missing-field-initializers: -Wextra flags designated/partial
        # aggregate init (e.g. the OpenAPI descriptor structs), but the
        # remaining members are value-initialized per the standard, so this
        # subset is pure noise rather than a defect signal.
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-missing-field-initializers
        )
        if(AUTHFORGE_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
