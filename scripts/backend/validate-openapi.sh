#!/bin/bash
# validate-openapi.sh - CI script to validate OpenAPI specification

set -e

# Robustly find common environment script
# Handle both direct execution and symbolic links (like git hooks)
REAL_PATH=$(readlink -f "$0" 2>/dev/null || perl -e 'use Cwd "abs_path"; print abs_path($ARGV[0])' -- "$0")
SCRIPT_DIR="$(dirname "$REAL_PATH")"
source "$SCRIPT_DIR/env_common.sh"

echo "[INFO] CI OpenAPI Validation"
echo "========================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

FAILURE=0

# Function to print section header
print_section() {
    echo ""
    echo "=== $1 ==="
    echo "--------------------------------"
}

# Function to check command result
check_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}[PASS] $2${NC}"
        return 0
    else
        echo -e "${RED}[ERROR] $2${NC}"
        FAILURE=1
        return 1
    fi
}

# 1. Build project (if needed)
print_section "Building Project"
cd "$PROJECT_DIR"
if ./scripts/backend/build.sh --debug; then
    check_result 0 "Build successful"
else
    check_result 1 "Build failed"
fi

# 2. Run OpenAPI tests
print_section "Running OpenAPI Tests"
cd "$BUILD_ABS_DIR"
if ctest -C Debug --output-on-failure; then
    check_result 0 "Tests passed"
else
    check_result 1 "Tests failed"
fi

# 3. Validate OpenAPI JSON structure
print_section "Validating OpenAPI JSON"

# Find openapi.json in common build locations
SEARCH_PATHS=(
    "$BUILD_ABS_DIR/$FULLA_SERVER_DIR/test/Debug/docs/api/openapi.json"
    "$BUILD_ABS_DIR/$FULLA_SERVER_DIR/test/docs/api/openapi.json"
    "$BUILD_ABS_DIR/$FULLA_SERVER_DIR/docs/api/openapi.json"
    "$FULLA_SERVER_ABS_DIR/docs/api/openapi.json"
)

OPENAPI_FILE=""
for path in "${SEARCH_PATHS[@]}"; do
    if [ -f "$path" ]; then
        OPENAPI_FILE="$path"
        break
    fi
done

if [ -n "$OPENAPI_FILE" ]; then
    echo "[INFO] Found OpenAPI file at: $OPENAPI_FILE"
    
    # Validate JSON
    if command -v jq &> /dev/null; then
        if jq . "$OPENAPI_FILE" > /dev/null 2>&1; then
            check_result 0 "OpenAPI JSON is valid (jq)"
        else
            check_result 1 "OpenAPI JSON is invalid (jq)"
        fi
    elif command -v python3 &> /dev/null; then
        if python3 -m json.tool "$OPENAPI_FILE" > /dev/null 2>&1; then
            check_result 0 "OpenAPI JSON is valid (python)"
        else
            check_result 1 "OpenAPI JSON is invalid (python)"
        fi
    fi

    # Check required fields
    if command -v jq &> /dev/null; then
        REQUIRED_FIELDS=("openapi" "info" "paths" "servers")
        for field in "${REQUIRED_FIELDS[@]}"; do
            if jq -e ".${field}" "$OPENAPI_FILE" > /dev/null 2>&1; then
                check_result 0 "Required field '${field}' exists"
            else
                check_result 1 "Required field '${field}' is missing"
            fi
        done
    fi
else
    echo -e "${RED}[ERROR] OpenAPI file not found!${NC}"
    echo "Make sure the server or tests generated the file."
    FAILURE=1
fi

# Final summary
print_section "Validation Summary"
if [ $FAILURE -eq 0 ]; then
    echo -e "${GREEN}[+] All OpenAPI validations passed${NC}"
    exit 0
else
    echo -e "${RED}[-] Some OpenAPI validations failed${NC}"
    exit 1
fi
