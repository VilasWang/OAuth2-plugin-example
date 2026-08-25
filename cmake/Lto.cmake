# cmake/Lto.cmake — opt-in Link-Time Optimization for first-party targets.
#
# OFF by default: every existing preset produces byte-identical build
# behavior. The dedicated *-lto presets (CMakePresets.json) set
# FULLA_ENABLE_LTO=ON for the bench arm of the perf A/B
# (docs/performance-optimization-report.md bottleneck #8; IO-bound server,
# expected 0-3% — managed expectations, verdict by same-day A/B only).
#
# IPO support is checked once via CheckIPOSupported; requesting LTO on a
# toolchain that cannot do it is a hard configure error (better than a silent
# no-op arm). Setting CMAKE_INTERPROCEDURAL_OPTIMIZATION before any
# add_subdirectory() makes every first-party target inherit LTO without
# per-target edits; Conan's prebuilt third-party packages (Drogon, OpenSSL,
# ...) simply link as non-LTO objects, which the final -flto link accepts.
include_guard(GLOBAL)

include(CheckIPOSupported)

option(FULLA_ENABLE_LTO "Enable link-time optimization (LTO) for first-party targets" OFF)

if(FULLA_ENABLE_LTO)
    check_ipo_supported(RESULT _fulla_ipo_supported OUTPUT _fulla_ipo_output)
    if(NOT _fulla_ipo_supported)
        message(FATAL_ERROR
            "FULLA_ENABLE_LTO=ON but interprocedural optimization is not supported "
            "by this toolchain: ${_fulla_ipo_output}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    message(STATUS "LTO: interprocedural optimization enabled")
endif()
