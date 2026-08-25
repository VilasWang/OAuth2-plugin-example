#!/usr/bin/env bash
# tools/check-orm-exempt.sh
#
# Mechanical guard for design.md §0 (ORM-Generated Files Exemption) and
# requirements.md R1 (R1.1..R1.10). Asserts that the 14 Drogon ORM model
# classes have not been renamed, moved out of `OAuth2Plugin/{src,include/
# oauth2}/models/`, had their `drogon_model::fulla_db` namespace altered,
# or lost their `DO NOT EDIT ... drogon_ctl` provenance comment.
#
# Exit codes:
#   0  every assertion passed.
#   1  at least one assertion failed; specific failures are listed on stderr.
#   2  environment error (missing tooling).
#
# This script is invoked by every phase acceptance gate per tasks.md (R1.10
# and the gate template referencing tools/check-orm-exempt.sh) and is also
# wired into capture.{sh,ps1} verify so the P0 gate (task 1.7) blocks
# regressions early.
#
# Spec references:
#   _Design: §0, §0.1, §0.3, §4.1.1, Property 1_
#   _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.8, 1.9, 1.10_

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

INCLUDE_DIR="${REPO_ROOT}/OAuth2Plugin/include/oauth2/models"
SRC_DIR="${REPO_ROOT}/OAuth2Plugin/src/models"

# Canonical, frozen list from design §0.1 (Source table -> Class name).
EXEMPT_CLASSES=(
    "Oauth2AccessTokens"
    "Oauth2Clients"
    "Oauth2Codes"
    "Oauth2RefreshTokens"
    "Oauth2Scopes"
    "Oauth2ClientScopes"
    "Oauth2SubjectMappings"
    "Oauth2UserConsents"
    "Organizations"
    "Permissions"
    "Roles"
    "RolePermissions"
    "UserRoles"
    "Users"
)

# Configuration files protected by §0.2.
EXEMPT_CONFIGS=(
    "OAuth2Plugin/src/models/model.json"
    "OAuth2Plugin/src/models/model-postgresql.json"
    "OAuth2Plugin/src/models/.clang-format"
)

EXPECTED_NAMESPACE_OUTER="drogon_model"
EXPECTED_NAMESPACE_INNER="fulla_db"
PROVENANCE_REGEX="DO NOT EDIT.*drogon_ctl"

failures=0
checks=0

log()  { printf '[orm-exempt] %s\n' "$*"; }
warn() { printf '[orm-exempt][warn] %s\n' "$*" >&2; }
err()  { printf '[orm-exempt][err]  %s\n' "$*" >&2; failures=$((failures + 1)); }

assert() {
    checks=$((checks + 1))
    local label="$1"
    shift
    if ! "$@"; then
        err "FAIL: ${label}"
        return 1
    fi
    return 0
}

file_contains() {
    local path="$1"
    local pat="$2"
    grep -E -q "${pat}" "${path}"
}

main() {
    if [[ ! -d "${INCLUDE_DIR}" ]]; then
        err "missing include directory: ${INCLUDE_DIR} (R1.2)"
    fi
    if [[ ! -d "${SRC_DIR}" ]]; then
        err "missing src directory: ${SRC_DIR} (R1.3)"
    fi

    # Per-class checks.
    for cls in "${EXEMPT_CLASSES[@]}"; do
        local hdr="${INCLUDE_DIR}/${cls}.h"
        local src="${SRC_DIR}/${cls}.cc"

        # R1.1 + R1.2: header path + presence (implies class name preserved).
        if [[ ! -f "${hdr}" ]]; then
            err "header missing: ${hdr} (R1.1, R1.2)"
            continue
        fi
        # R1.1 + R1.3: source path + presence.
        if [[ ! -f "${src}" ]]; then
            err "source missing: ${src} (R1.1, R1.3)"
        fi

        # R1.1: class declaration `class <cls>` must appear in the header
        # (allow trailing characters for forward declarations or final).
        if ! file_contains "${hdr}" "^class[[:space:]]+${cls}\\b"; then
            err "class declaration missing in header: ${cls} (R1.1)"
        fi

        # R1.4: namespace chain drogon_model::fulla_db must remain intact.
        if ! file_contains "${hdr}" "^namespace[[:space:]]+${EXPECTED_NAMESPACE_OUTER}[[:space:]]*$"; then
            err "outer namespace missing: ${cls} expected 'namespace ${EXPECTED_NAMESPACE_OUTER}' (R1.4)"
        fi
        if ! file_contains "${hdr}" "^namespace[[:space:]]+${EXPECTED_NAMESPACE_INNER}[[:space:]]*$"; then
            err "inner namespace missing: ${cls} expected 'namespace ${EXPECTED_NAMESPACE_INNER}' (R1.4)"
        fi

        # R1.8: provenance comment must stay (gate against accidental hand-edit
        # masquerading as a regenerate).
        if ! file_contains "${hdr}" "${PROVENANCE_REGEX}"; then
            err "provenance comment removed from header: ${cls} (R1.8)"
        fi
        if [[ -f "${src}" ]]; then
            if ! file_contains "${src}" "${PROVENANCE_REGEX}"; then
                err "provenance comment removed from source: ${cls}.cc (R1.8)"
            fi
        fi

        checks=$((checks + 4))   # the 4 per-class file_contains calls above
    done

    # R1.5: model.json / model-postgresql.json / .clang-format paths intact.
    for cfg in "${EXEMPT_CONFIGS[@]}"; do
        local path="${REPO_ROOT}/${cfg}"
        if [[ ! -f "${path}" ]]; then
            err "exempt config missing: ${cfg} (R1.5)"
        fi
        checks=$((checks + 1))
    done

    # R1.9: no ORM-exempt class file exists outside the models/ directories.
    # We grep for stray <Class>.h occurrences anywhere under OAuth2Plugin/
    # other than the canonical paths.
    local stray
    for cls in "${EXEMPT_CLASSES[@]}"; do
        # Find any header / source file matching this class name outside of
        # the canonical models directories (exclude models_backup which is
        # a gitignored legacy artifact, not a real source location).
        while IFS= read -r path; do
            err "ORM-exempt file found outside models/: ${path} (R1.9)"
        done < <(
            find "${REPO_ROOT}/OAuth2Plugin" -type f \
                \( -name "${cls}.h" -o -name "${cls}.cc" \) \
                -not -path "${INCLUDE_DIR}/*" \
                -not -path "${SRC_DIR}/*" \
                -not -path "*/models_backup/*" 2>/dev/null
        )
        checks=$((checks + 1))
    done

    if [[ "${failures}" -eq 0 ]]; then
        log "OK: ${checks} ORM exemption assertions passed (14 classes, ${#EXEMPT_CONFIGS[@]} configs)."
        return 0
    fi
    err "FAILED: ${failures} of ${checks} assertions; see messages above."
    return 1
}

main "$@"
