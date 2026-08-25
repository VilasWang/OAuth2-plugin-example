#!/usr/bin/env bash
# tools/refactor-baseline/capture.sh
#
# P0 baseline snapshot entry point for the repo-structure-refactor spec.
#
# Subcommands:
#   setup       Create the subdirectory layout (ctest/, playwright/, endpoints/).
#               Idempotent. Implemented in task 1.1.
#   ctest       Run `ctest -N` + `ctest --output-on-failure` against the local
#               build tree, parse to deterministic text, and write
#               tools/refactor-baseline/ctest/<os>-<cfg>.txt. Task 1.2.
#               Per-host: each platform/config runner produces 1 file.
#               Optional flags:
#                 --config <Debug|Release>    (default: $FULLA_CTEST_CONFIG or Debug)
#                 --build-dir <path>          (default: ./build)
#                 --from-fixture              use bundled sample output instead
#                                             of running ctest (offline self-test)
#   playwright  Capture frontends/admin + frontends/user Playwright PASS list. Task 1.3.
#   endpoints   Capture HTTP endpoint snapshots. Task 1.4.
#   all         Run setup + ctest + playwright + endpoints in sequence.
#   verify      Assert each baseline subdirectory is non-empty. Used by P0 gate (task 1.7).
#   selftest    Run parse_ctest.py --selftest (no real build needed).
#   help        Print this help.
#
# Spec references:
#   _Design: repo-structure-refactor §2.8 P0, §12.1, §12.6, Property 3_
#   _Requirements: 14.1, 17.5_

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASELINE_DIR="${SCRIPT_DIR}"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PARSER="${SCRIPT_DIR}/parse_ctest.py"
FIXTURE_DIR="${SCRIPT_DIR}/fixtures"

log()  { printf '[baseline] %s\n' "$*"; }
warn() { printf '[baseline][warn] %s\n' "$*" >&2; }
err()  { printf '[baseline][err]  %s\n' "$*" >&2; }

resolve_python() {
    if command -v python3 >/dev/null 2>&1; then
        echo python3
    elif command -v python >/dev/null 2>&1; then
        echo python
    else
        err "python3 not found in PATH (needed by parse_ctest.py)"
        return 1
    fi
}

resolve_os_tag() {
    case "$(uname -s 2>/dev/null || echo Unknown)" in
        Linux*)  echo linux  ;;
        Darwin*) echo macos  ;;
        MINGW*|MSYS*|CYGWIN*) echo windows ;;
        *) echo unknown ;;
    esac
}

resolve_cfg() {
    local cfg="${1:-${FULLA_CTEST_CONFIG:-Debug}}"
    case "${cfg}" in
        Debug|Release|RelWithDebInfo|MinSizeRel) echo "${cfg}" ;;
        debug)   echo Debug   ;;
        release) echo Release ;;
        *) err "Unsupported ctest config: ${cfg}"; return 1 ;;
    esac
}

cmd_setup() {
    log "Creating baseline subdirectory layout under: ${BASELINE_DIR}"
    mkdir -p "${BASELINE_DIR}/ctest"
    mkdir -p "${BASELINE_DIR}/playwright"
    mkdir -p "${BASELINE_DIR}/endpoints"
    : > "${BASELINE_DIR}/ctest/.gitkeep"
    : > "${BASELINE_DIR}/playwright/.gitkeep"
    : > "${BASELINE_DIR}/endpoints/.gitkeep"
    log "setup done."
}

cmd_ctest() {
    local cfg=""
    local build_dir="${REPO_ROOT}/build"
    local from_fixture=0

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --config)
                cfg="$2"; shift 2 ;;
            --build-dir)
                build_dir="$2"; shift 2 ;;
            --from-fixture)
                from_fixture=1; shift ;;
            *)
                err "Unknown flag for ctest: $1"; return 64 ;;
        esac
    done

    cfg="$(resolve_cfg "${cfg}")"
    local os_tag
    os_tag="$(resolve_os_tag)"
    local out_file="${BASELINE_DIR}/ctest/${os_tag}-${cfg,,}.txt"
    local py
    py="$(resolve_python)"

    mkdir -p "${BASELINE_DIR}/ctest"

    local listing_tmp run_tmp
    listing_tmp="$(mktemp -t ctest-N.XXXXXX.txt)"
    run_tmp="$(mktemp -t ctest-run.XXXXXX.txt)"
    trap 'rm -f "${listing_tmp}" "${run_tmp}"' EXIT

    if [[ "${from_fixture}" -eq 1 ]]; then
        log "ctest: using bundled fixtures (offline self-test mode)"
        cp "${FIXTURE_DIR}/ctest-N.sample.txt"    "${listing_tmp}"
        cp "${FIXTURE_DIR}/ctest-pass.sample.txt" "${run_tmp}"
    else
        if [[ ! -d "${build_dir}" ]]; then
            err "build dir not found: ${build_dir}"
            err "configure first (cmake --preset <preset>, e.g. linux-release) then pass --build-dir build/<preset>"
            err "or rerun with --from-fixture to capture against bundled fixtures."
            return 1
        fi
        if ! command -v ctest >/dev/null 2>&1; then
            err "ctest not found in PATH"
            return 1
        fi
        log "ctest -N -C ${cfg} (build_dir=${build_dir})"
        ( cd "${build_dir}" && ctest -N -C "${cfg}" ) > "${listing_tmp}"
        log "ctest -C ${cfg} --output-on-failure (build_dir=${build_dir})"
        # Allow non-zero exit so we still capture the run trace; parser will
        # surface FAILED entries explicitly.
        ( cd "${build_dir}" && ctest -C "${cfg}" --output-on-failure ) > "${run_tmp}" || \
            warn "ctest exited non-zero; run record will contain FAILED rows"
    fi

    "${py}" "${PARSER}" --listing "${listing_tmp}" --run "${run_tmp}" --out "${out_file}"
    log "ctest baseline written: ${out_file}"
}

cmd_playwright() {
    local app=""
    local from_fixture=0
    local mode="auto"   # auto: try --list; else require --json or --list path

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --app) app="$2"; shift 2 ;;
            --from-fixture) from_fixture=1; shift ;;
            --json) mode="json-file"; local json_path="$2"; shift 2 ;;
            --list) mode="list-file"; local list_path="$2"; shift 2 ;;
            *) err "Unknown flag for playwright: $1"; return 64 ;;
        esac
    done

    local apps=()
    if [[ -z "${app}" || "${app}" == "all" ]]; then
        apps=(admin frontend)
    else
        apps=("${app}")
    fi

    local py
    py="$(resolve_python)"
    local parser="${SCRIPT_DIR}/parse_playwright.py"

    mkdir -p "${BASELINE_DIR}/playwright"

    local a
    for a in "${apps[@]}"; do
        local subdir
        case "${a}" in
            admin)    subdir="frontends/admin" ;;
            frontend) subdir="frontends/user" ;;
            *) err "Unknown app: ${a} (expected admin|frontend|all)"; return 64 ;;
        esac

        local out_file="${BASELINE_DIR}/playwright/${a}.txt"
        local tmp
        tmp="$(mktemp -t pw-${a}.XXXXXX.txt)"

        if [[ "${from_fixture}" -eq 1 ]]; then
            log "playwright[${a}]: using bundled --list fixture"
            cp "${FIXTURE_DIR}/playwright-list.sample.txt" "${tmp}"
            "${py}" "${parser}" --list "${tmp}" --out "${out_file}"
        elif [[ "${mode}" == "json-file" ]]; then
            log "playwright[${a}]: parsing pre-collected json: ${json_path}"
            "${py}" "${parser}" --json "${json_path}" --out "${out_file}"
        elif [[ "${mode}" == "list-file" ]]; then
            log "playwright[${a}]: parsing pre-collected list: ${list_path}"
            "${py}" "${parser}" --list "${list_path}" --out "${out_file}"
        else
            local app_dir="${REPO_ROOT}/${subdir}"
            if [[ ! -d "${app_dir}/node_modules/.bin" ]]; then
                err "playwright[${a}]: ${app_dir}/node_modules not found; run npm ci first"
                err "or rerun with --from-fixture for offline self-test."
                return 1
            fi
            log "playwright[${a}]: ${subdir} -> playwright test --list --reporter=json"
            ( cd "${app_dir}" && ./node_modules/.bin/playwright test --list --reporter=json ) > "${tmp}"
            "${py}" "${parser}" --json "${tmp}" --out "${out_file}"
        fi

        rm -f "${tmp}"
        log "playwright baseline written: ${out_file}"
    done
}

cmd_endpoints() {
    local from_fixture=0
    local openapi_path=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --from-fixture) from_fixture=1; shift ;;
            --openapi) openapi_path="$2"; shift 2 ;;
            *) err "Unknown flag for endpoints: $1"; return 64 ;;
        esac
    done

    local py
    py="$(resolve_python)"
    local parser="${SCRIPT_DIR}/parse_endpoints.py"
    local out_dir="${BASELINE_DIR}/endpoints"
    mkdir -p "${out_dir}"

    if [[ "${from_fixture}" -eq 1 ]]; then
        log "endpoints: using bundled OpenAPI fixture (offline)"
        local out_file="${out_dir}/openapi-fixture.signature.txt"
        "${py}" "${parser}" --openapi "${FIXTURE_DIR}/openapi-mini.yaml" --out "${out_file}"
        log "endpoints baseline written: ${out_file}"
        return 0
    fi

    if [[ -z "${openapi_path}" ]]; then
        openapi_path="${REPO_ROOT}/apps/server/openapi.yaml"
    fi
    if [[ ! -f "${openapi_path}" ]]; then
        err "OpenAPI file not found: ${openapi_path}"
        err "(P0 scaffolding mode B: extract static endpoint signatures from"
        err " apps/server/openapi.yaml. Live status/headers/body capture is"
        err " backfilled in P7 once docker compose smoke is reachable.)"
        return 1
    fi

    local out_file="${out_dir}/openapi.signature.txt"
    "${py}" "${parser}" --openapi "${openapi_path}" --out "${out_file}"
    log "endpoints baseline written: ${out_file}"
    log "(scaffolding mode B; live response capture deferred to P7)"
}

cmd_all() {
    cmd_setup
    cmd_ctest
    cmd_playwright
    cmd_endpoints
}

cmd_verify() {
    log "Verifying baseline directories are non-empty (task 1.7 gate input)..."
    local fail=0
    for sub in ctest playwright endpoints; do
        local dir="${BASELINE_DIR}/${sub}"
        if [[ ! -d "${dir}" ]]; then
            err "Missing directory: ${dir}"
            fail=1
            continue
        fi
        local count
        count=$(find "${dir}" -mindepth 1 -maxdepth 1 ! -name '.gitkeep' | wc -l | tr -d ' ')
        if [[ "${count}" -eq 0 ]]; then
            err "Empty baseline subdirectory: ${dir} (only .gitkeep present)"
            fail=1
        else
            log "OK  ${dir} (${count} entries)"
        fi
    done

    log "Running tools/check-orm-exempt.sh ..."
    if ! bash "${REPO_ROOT}/tools/check-orm-exempt.sh"; then
        fail=1
    fi

    if [[ -f "${BASELINE_DIR}/endpoints/openapi.signature.txt" ]]; then
        log "Running tools/diff-endpoint-baseline.py ..."
        local py
        py="$(resolve_python)"
        if ! "${py}" "${REPO_ROOT}/tools/diff-endpoint-baseline.py"; then
            fail=1
        fi
    else
        warn "endpoints/openapi.signature.txt absent; skipping diff-endpoint-baseline check"
    fi

    return "${fail}"
}

cmd_selftest() {
    local py
    py="$(resolve_python)"
    "${py}" "${PARSER}" --selftest
    "${py}" "${SCRIPT_DIR}/parse_playwright.py" --selftest
    "${py}" "${SCRIPT_DIR}/parse_endpoints.py" --selftest
}

cmd_help() {
    cat <<'USAGE'
Usage: capture.sh <subcommand> [flags]

Subcommands:
  setup        Create baseline subdirectories (idempotent, task 1.1).
  ctest        Capture ctest baseline for the current host. Task 1.2.
               Flags: --config <Debug|Release>   (default: Debug or
                                                  $FULLA_CTEST_CONFIG)
                      --build-dir <path>         (default: ./build)
                      --from-fixture             use bundled sample (offline)
  playwright   Capture Playwright baseline. Task 1.3.
               Flags: --app admin|frontend|all   (default: all)
                      --from-fixture             use bundled list fixture (offline)
                      --json <file>              parse reporter=json output
                      --list <file>              parse --list output file
  endpoints    Capture HTTP endpoint baseline. Task 1.4 (scheme B: static
               OpenAPI signatures from apps/server/openapi.yaml; live
               response capture is backfilled in P7).
               Flags: --openapi <path>          override the input OpenAPI
                      --from-fixture            use bundled mini OpenAPI fixture
  all          setup + ctest + playwright + endpoints.
  verify       Assert all baseline subdirectories are non-empty (P0 gate).
  selftest     Run parse_ctest.py + parse_playwright.py self-tests.
  help         Show this message.

The 6-cell ctest matrix (Linux/macOS/Windows x Debug/Release) is captured by
running this script on each runner; each invocation emits one file
`ctest/<os>-<cfg>.txt`.

Playwright baselines: tools/refactor-baseline/playwright/{admin,frontend}.txt.
USAGE
}

main() {
    local sub="${1:-help}"
    shift || true
    case "${sub}" in
        setup)      cmd_setup "$@" ;;
        ctest)      cmd_ctest "$@" ;;
        playwright) cmd_playwright "$@" ;;
        endpoints)  cmd_endpoints "$@" ;;
        all)        cmd_all "$@" ;;
        verify)     cmd_verify "$@" ;;
        selftest)   cmd_selftest "$@" ;;
        help|-h|--help) cmd_help ;;
        *)
            err "Unknown subcommand: ${sub}"
            cmd_help >&2
            return 64
            ;;
    esac
}

main "$@"
