#!/bin/bash
# Naming validator for DROGON_TEST
# Usage: ./naming_validator.sh <directory_to_scan>

TARGET_DIR=${1:-"tests"}

echo "Scanning directory: $TARGET_DIR for naming violations..."

# Extract every DROGON_TEST name. Multiline-aware: clang-format may wrap the
# name onto the line after `DROGON_TEST(`, so a line-by-line grep|sed both
# misses those names and emits garbage. -z treats the file as one buffer,
# (?m)^\s* anchors the macro at line start (skips // comment mentions), and
# \K keeps only the identifier token. Each record is `file.cc:TestName`, so
# the compliance filter anchors on the colon (keeping the file for diagnostics).
invalid_names=$(grep -rzoP '(?m)^\s*DROGON_TEST\(\s*\K\w+' "$TARGET_DIR" --include="*.cc" | \
    tr '\0' '\n' | \
    grep -vE ':(Unit|Integration|E2E|Performance|Security|API|Database|Acceptance)_P[0-3]_')

if [ -n "$invalid_names" ]; then
    echo ""
    echo "[ERROR] 命名规范检查失败! 发现以下不合规测试 (Naming violation detected!):"
    echo "--------------------------------------------------------"
    echo "$invalid_names"
    echo "--------------------------------------------------------"
    echo "期望格式 (Expected format): [Category]_[Priority]_[Module]_[Feature]_[Scenario]"
    echo "  Category : Unit, Integration, E2E, Performance, Security, API, Database, Acceptance"
    echo "  Priority : P0, P1, P2, P3"
    echo "  Example  : Unit_P0_Validator_ClientId_InvalidFormat_ReturnsError"
    exit 1
else
    echo "[PASS] 命名规范检查通过 (All test names follow convention!)"
    exit 0
fi
