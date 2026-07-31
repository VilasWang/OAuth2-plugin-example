# Implementation Plan: Fix macOS CI Failure (codecvt_utf8_utf16)

## Problem Description
The macOS CI is failing during the "Build" step with the following error:
`error: no member named 'codecvt_utf8_utf16' in namespace 'std'`
This occurs in the generated Drogon model files (e.g., `Oauth2AccessTokens.cc`).

## Root Cause (Updated 2026-05-18)

1. **Missing Force-Include**: The generated model files use `std::codecvt_utf8_utf16` but do not explicitly include `<codecvt>`. The project has a compatibility header `orm_compat.h` designed to solve this, but it was only force-included for MSVC (Windows) in `CMakeLists.txt`.

2. **Incorrect Fallback Condition**: The fix in commit 2a2ce2c added `|| defined(__APPLE__)` to the `#if` condition in `orm_compat.h`. This caused a **redefinition error** because on macOS with C++17, `<codecvt>` still provides `std::codecvt_utf8_utf16` as a deprecated type. The force-include correctly makes `<codecvt>` available, but the fallback then tries to redefine it.

## Solution

1. **CMake Force-Include** (commit 2a2ce2c, correct):

   - `OAuth2Plugin/CMakeLists.txt` and `OAuth2Server/CMakeLists.txt` use `-include` flag for Clang/GCC to force-include `orm_compat.h` before all source files.
   - This ensures `<codecvt>` is included, providing `std::codecvt_utf8_utf16` on macOS C++17.

2. **Fix Fallback Condition** (this fix):

   - Remove `|| defined(__APPLE__)` from the `#if` condition in `orm_compat.h`.
   - The fallback should only activate for C++20 where `<codecvt>` no longer provides these types.
   - On macOS C++17, the standard `<codecvt>` header provides everything needed (deprecated but available).

## Verification
- Once committed, the GitHub Actions macOS CI should pass the "Build" step.
- Windows and Linux CI should remain passing.
