# cmake/Coverage.cmake
# Optional gcov code-coverage instrumentation toggle.
#
# Cache option (bool):
#   OAUTH2_TEST_COVERAGE = OFF (default) | ON
#
# When ON, function oauth2_apply_gcov(target) adds gcov coverage flags
# (`-fprofile-arcs -ftest-coverage`, the modern equivalent of `--coverage`)
# to BOTH the COMPILE and LINK options of `target`, producing the .gcno
# (compile-time) and .gcda (runtime) sidecar files gcovr/gcov consume.
#
# Rules (mirror Sanitizers.cmake):
#   * GCC/Clang only. MSVC is ignored with a WARNING so the normal build
#     still succeeds (MSVC uses a different coverage toolchain).
#   * Intended for Debug builds only. A non-fatal WARNING is emitted for
#     other build types (Release inlining distorts line attribution).
#
# IMPORTANT — why this is a function applied PER-TARGET, not a single flag
# on the test binary (which was the original tests/CMakeLists.txt approach):
# the authforge::* libraries are pre-built STATIC archives. Adding `--coverage`
# only to the test executable that LINKS them does NOT instrument the library
# object files (they were compiled without the flag), so coverage of the
# library source itself is effectively zero. Each first-party library must
# call oauth2_apply_gcov(${PROJECT_NAME}) so its own .o files get the
# -fprofile-arcs/-ftest-coverage flags at compile time. See docs/backend/
# testing-guide.md §7 for the build/run/report workflow.
#
# This module is the canonical coverage entry point; the old inline
# if(OAUTH2_TEST_COVERAGE) block that used to live in tests/CMakeLists.txt
# has been replaced by a oauth2_apply_gcov(${PROJECT_NAME}) call there.

option(OAUTH2_TEST_COVERAGE "Enable gcov code coverage instrumentation (GCC/Clang, Debug)" OFF)

function(oauth2_apply_gcov target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "oauth2_apply_gcov: target '${target}' does not exist")
    endif()

    # Default: coverage off, no-op. Lets every target call this unconditionally
    # without polluting non-coverage builds.
    if(NOT OAUTH2_TEST_COVERAGE)
        return()
    endif()

    # GCC/Clang only. MSVC does not accept these flags and has its own
    # coverage toolchain; ignore gracefully so the build still succeeds.
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(WARNING
            "OAUTH2_TEST_COVERAGE=ON requested, but compiler '${CMAKE_CXX_COMPILER_ID}' "
            "is not GCC/Clang. Coverage NOT applied to '${target}'.")
        return()
    endif()

    # Clang on the MSVC ABI (clang-cl) takes different coverage flags
    # (source-based -fprofile-instr-generate); this module targets gcov only.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
       (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
        message(WARNING
            "OAUTH2_TEST_COVERAGE=ON requested, but Clang is targeting the MSVC ABI "
            "(${CMAKE_CXX_COMPILER_ID}, simulate=${CMAKE_CXX_SIMULATE_ID}). "
            "gcov instrumentation is unsupported on this target. Coverage NOT applied "
            "to '${target}'. Use a Linux/macOS GCC toolchain for gcov builds.")
        return()
    endif()

    # Debug-only guidance (non-fatal): Release inlining distorts line attribution.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING
            "OAUTH2_TEST_COVERAGE is intended for Debug builds; "
            "current CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' (line attribution may be distorted).")
    endif()

    # -fprofile-arcs (for .gcda runtime counters) + -ftest-coverage (for .gcno
    # compile-time structure). Together equivalent to --coverage, but explicit
    # so the intent is self-documenting. -g ensures line attribution.
    set(_flags -g -fprofile-arcs -ftest-coverage)

    message(STATUS
        "OAUTH2_TEST_COVERAGE=ON: applying gcov flags to '${target}' (compile + link)")
    target_compile_options(${target} PRIVATE ${_flags})
    target_link_options(${target} PRIVATE ${_flags})
    # Macro gate for gcov-specific code (e.g. tests/test_main.cc's explicit
    # __gcov_dump() flush before std::_Exit). Guarding that code with
    # `defined(__GNUC__)` instead broke non-coverage GCC/Clang builds:
    # AppleClang also defines __GNUC__, but without -fprofile-arcs/-lgcov the
    # __gcov_dump symbol is never linked -> undefined-reference link errors
    # (seen in CI linux/macos jobs). This macro is defined EXACTLY when the
    # instrumentation (and thus the libgcov link below) is active, keeping the
    # symbol reference and its supply in lockstep.
    target_compile_definitions(${target} PRIVATE AUTHFORGE_GCOV_INSTRUMENTED=1)
    # The gcov runtime (libgcov.a, providing __gcov_init/__gcov_exit/
    # __gcov_merge_*). gcc's --coverage driver normally pulls this in
    # implicitly at link time, but link it explicitly via target_link_libraries
    # (NOT target_link_options) so CMake places it AFTER the object files /
    # archives on the link line -- gcc resolves libraries left-to-right, so a
    # `-lgcov` emitted as a link option before the .a files would leave
    # __gcov_* undefined. STATIC libraries have no link step of their own,
    # but every final executable (the *-test binaries and authforge-tests)
    # also calls oauth2_apply_gcov, so the runtime resolves at the
    # executable's link.
    target_link_libraries(${target} PRIVATE gcov)
endfunction()
