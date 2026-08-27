# cmake/Sanitizers.cmake
# Optional sanitizer instrumentation for the concurrency & lifetime safety
# audit (and, since #104, for product libraries + CI).
#
# Cache option (comma-separated list; TSan is mutually exclusive with the
# others because its runtime cannot be composed with ASan/UBSan):
#   FULLA_SANITIZER = off (default) | thread | address | undefined
#                     | address,undefined
#
#   thread           -> -fsanitize=thread  -g -fno-omit-frame-pointer
#   address          -> -fsanitize=address -g -fno-omit-frame-pointer
#   undefined        -> -fsanitize=undefined -fno-sanitize-recover=undefined
#                       -g -fno-omit-frame-pointer
#   address,undefined-> both of the above (the recommended CI combination;
#                       ASan + UBSan compose on GCC/Clang)
#
# Rules:
#   * GCC/Clang only. MSVC does not accept the GCC/Clang `-fsanitize=` flags and
#     has no ThreadSanitizer runtime; Clang targeting the MSVC ABI
#     (clang-cl / *-pc-windows-msvc) has no TSan runtime either. In those cases
#     the option is ignored with a WARNING so the normal build still succeeds.
#   * Intended for Debug builds only (Sanitizer 仅用于 Debug 构建). A WARNING is
#     emitted for non-Debug build types.
#   * `thread` combined with anything else is a FATAL configuration error.
#
# Provides function oauth2_apply_sanitizer(target) which appends the selected
# `-fsanitize` flags to BOTH the COMPILE and LINK options of `target`.
#
# Since #104 the function is applied per-target to every first-party library
# (mirroring oauth2_apply_gcov's pattern) plus the test executable, so product
# code — not just the test harness — is instrumented.
#
# _Spec: concurrency-lifetime-safety-audit, Task 0 (TSan/ASan scaffolding)_
# _Requirements: 2.4, 2.5, 2.6, 2.8, 2.9, 2.10, 2.11_

set(FULLA_SANITIZER "off" CACHE STRING
    "Sanitizer(s) to apply: off | thread | address | undefined | address,undefined")
set_property(CACHE FULLA_SANITIZER PROPERTY STRINGS
             off thread address undefined address,undefined)

function(oauth2_apply_sanitizer target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "oauth2_apply_sanitizer: target '${target}' does not exist")
    endif()

    string(TOLOWER "${FULLA_SANITIZER}" _san)

    # Default: no sanitizer, no-op.
    if(_san STREQUAL "off" OR _san STREQUAL "")
        return()
    endif()

    # Parse the comma-separated list; validate every element up front.
    string(REPLACE "," ";" _san_list "${_san}")
    set(_kinds "")
    foreach(_entry IN LISTS _san_list)
        string(STRIP "${_entry}" _entry)
        if(NOT (_entry STREQUAL "thread" OR _entry STREQUAL "address"
                OR _entry STREQUAL "undefined"))
            message(FATAL_ERROR
                "FULLA_SANITIZER must be a comma-separated list of: off | thread | address | "
                "undefined (got '${FULLA_SANITIZER}')")
        endif()
        list(APPEND _kinds "${_entry}")
    endforeach()

    # TSan cannot be composed with ASan/UBSan runtimes.
    list(LENGTH _kinds _kinds_len)
    if("thread" IN_LIST _kinds AND NOT _kinds_len EQUAL 1)
        message(FATAL_ERROR
            "FULLA_SANITIZER: 'thread' cannot be combined with other sanitizers "
            "(got '${FULLA_SANITIZER}'); TSan's runtime is exclusive. Build TSan separately.")
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
            "Sanitizer runtimes are unsupported on this target. Sanitizer NOT applied "
            "to '${target}'. Use a Linux/macOS GCC or Clang toolchain for sanitizer builds.")
        return()
    endif()

    # Debug-only guidance (non-fatal).
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING
            "FULLA_SANITIZER=${_san} is intended for Debug builds; "
            "current CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'.")
    endif()

    # Compose the flag set. UBSan runs with -fno-sanitize-recover so UB
    # aborts instead of limping on (CI must see it).
    set(_compile_flags -g -fno-omit-frame-pointer)
    set(_link_flags)
    foreach(_kind IN LISTS _kinds)
        if(_kind STREQUAL "thread")
            list(APPEND _compile_flags -fsanitize=thread)
            list(APPEND _link_flags -fsanitize=thread)
        elseif(_kind STREQUAL "address")
            list(APPEND _compile_flags -fsanitize=address)
            list(APPEND _link_flags -fsanitize=address)
        else() # undefined
            list(APPEND _compile_flags -fsanitize=undefined -fno-sanitize-recover=undefined)
            list(APPEND _link_flags -fsanitize=undefined)
        endif()
    endforeach()

    message(STATUS
        "FULLA_SANITIZER=${_san}: applying compile '${_compile_flags}' (PRIVATE) and "
        "link '${_link_flags}' (PUBLIC) to '${target}'")
    # Compile PRIVATE: only this target's own sources get instrumented.
    # Link PUBLIC: an instrumented static library's objects reference the
    # sanitizer runtimes, so EVERY consumer (per-lib test executables, the
    # main test binary, the server, examples) must link them too --
    # INTERFACE_LINK_OPTIONS on the library propagates exactly that. (The
    # first sanitizer CI run failed with undefined __asan_*/__ubsan_* refs
    # in fulla-common-test: PRIVATE link flags never reached the consumer.)
    target_compile_options(${target} PRIVATE ${_compile_flags})
    target_link_options(${target} PUBLIC ${_link_flags})
endfunction()
