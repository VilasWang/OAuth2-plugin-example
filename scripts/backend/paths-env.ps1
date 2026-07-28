# paths-env.ps1 - Load paths.env (repo root) into a hashtable / variables.
# Single source of truth for source/build/SQL/config paths (Task 5 / M0,
# design.md review H2).
#
# Usage:
#   . "$PSScriptRoot\paths-env.ps1"
#   $Paths = Import-PathsEnv
#   $Paths["OAUTH2_SERVER_DIR"]   # e.g. "apps/server"
#
# Each `KEY=VALUE` line in paths.env is parsed into the returned hashtable.
# Lines starting with # are comments and blank lines are skipped. No
# environment variables are mutated - callers read values explicitly, to
# avoid clobbering unrelated process environment.

function Import-PathsEnv {
    param(
        [string]$PathsEnvFile
    )

    if (-not $PathsEnvFile) {
        $repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        $PathsEnvFile = Join-Path $repoRoot "paths.env"
    }

    if (-not (Test-Path $PathsEnvFile)) {
        throw "paths.env not found at $PathsEnvFile"
    }

    $result = @{}
    foreach ($line in Get-Content -LiteralPath $PathsEnvFile) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) {
            continue
        }
        $idx = $trimmed.IndexOf("=")
        if ($idx -lt 1) {
            continue
        }
        $key = $trimmed.Substring(0, $idx).Trim()
        $value = $trimmed.Substring($idx + 1).Trim()
        $result[$key] = $value
    }

    return $result
}
