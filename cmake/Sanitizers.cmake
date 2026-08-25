# cmake/Sanitizers.cmake
# Optional ThreadSanitizer (TSan) / AddressSanitizer (ASan) toggle for the
# test target of the concurrency & lifetime safety audit.
#
# Cache option (single-value enum -> TSan and ASan are mutually exclusive):
#   FULLA_SANITIZER = off (default) | thread | address
#
#   thread  -> -fsanitize=thread  -g -fno-omit-frame-pointer
#   address -> -fsanitize=address -g -fno-omit-frame-pointer
#
# Rules:
#   * GCC/Clang only. MSVC does not accept the GCC/Clang `-fsanitize=` flags and
#     has no ThreadSanitizer runtime; Clang targeting the MSVC ABI
#     (clang-cl / *-pc-windows-msvc) has no TSan runtime either. In those cases
#     the option is ignored with a WARNING so the normal build still succeeds.
#   * Intended for Debug builds only (Sanitizer 仅用于 Debug 构建). A WARNING is
#     emitted for non-Debug build types.
#   * TSan and ASan cannot be enabled simultaneously — the single-value enum
#     enforces this (you select exactly one of off/thread/address per build).
#     Run two separate builds to cover both.
#
# Provides function oauth2_apply_sanitizer(target) which appends the selected
# `-fsanitize` flags to BOTH the COMPILE and LINK options of `target`.
#
# _Spec: concurrency-lifetime-safety-audit, Task 0 (TSan/ASan scaffolding)_
# _Requirements: 2.4, 2.5, 2.6, 2.8, 2.9, 2.10, 2.11_

set(FULLA_SANITIZER "off" CACHE STRING
    "Enable a sanitizer for the test target: off | thread | address")
set_property(CACHE FULLA_SANITIZER PROPERTY STRINGS off thread address)

function(oauth2_apply_sanitizer target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "oauth2_apply_sanitizer: target '${target}' does not exist")
    endif()

    string(TOLOWER "${FULLA_SANITIZER}" _san)

    # Default: no sanitizer, no-op.
    if(_san STREQUAL "off" OR _san STREQUAL "")
        return()
    endif()

    # Validate the requested value early.
    if(NOT (_san STREQUAL "thread" OR _san STREQUAL "address"))
        message(FATAL_ERROR
            "FULLA_SANITIZER must be one of: off | thread | address (got '${FULLA_SANITIZER}')")
    endif()

    # GCC/Clang only.
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(WARNING
            "FULLA_SANITIZER=${_san} requested, but compiler '${CMAKE_CXX_COMPILER_ID}' "
            "is not GCC/Clang. Sanitizer NOT applied to '${target}'.")
        return()
    endif()

    # Clang on the MSVC ABI (clang-cl or *-pc-windows-msvc) has no TSan runtime
    # and does not accept the GCC/Clang `-fsanitize` driver flags the same way.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
       (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
        message(WARNING
            "FULLA_SANITIZER=${_san} requested, but Clang is targeting the MSVC ABI "
            "(${CMAKE_CXX_COMPILER_ID}, simulate=${CMAKE_CXX_SIMULATE_ID}). "
            "ThreadSanitizer is unsupported on this target. Sanitizer NOT applied to '${target}'. "
            "Use a Linux/macOS GCC or Clang toolchain for TSan/ASan builds.")
        return()
    endif()

    # Debug-only guidance (non-fatal).
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING
            "FULLA_SANITIZER=${_san} is intended for Debug builds; "
            "current CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'.")
    endif()

    if(_san STREQUAL "thread")
        set(_flags -fsanitize=thread -g -fno-omit-frame-pointer)
    else() # address
        set(_flags -fsanitize=address -g -fno-omit-frame-pointer)
    endif()

    message(STATUS
        "FULLA_SANITIZER=${_san}: applying '${_flags}' to '${target}' (compile + link)")
    target_compile_options(${target} PRIVATE ${_flags})
    target_link_options(${target} PRIVATE ${_flags})
endfunction()
