# manage.ps1 - Unified project management script for Windows
# Cross-platform parity with manage.sh (20 commands)

$Action = $args[0]
$Config = "Release"

# Parse global options
foreach ($arg in $args) {
    if ($arg -eq "-debug" -or $arg -eq "--debug") { $Config = "Debug" }
}

function Show-Help {
    Write-Host "Usage: .\manage.ps1 <command> [options]"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  build-backend [-debug]       Build the C++ project (Plugin + Server)"
    Write-Host "  test-backend [-debug]        Run backend tests"
    Write-Host "  build-frontend               Build the Vue frontend"
    Write-Host "  dev-frontend                 Run frontend in dev mode"
    Write-Host "  build-admin                  Build the admin frontend"
    Write-Host "  dev-admin                    Run admin frontend in dev mode"
    Write-Host "  run-backend [-debug]         Start the fulla-server binary"
    Write-Host "  setup-db                     Create database and run migrations"
    Write-Host "  generate-models              Generate Drogon ORM models"
    Write-Host "  reset-password               Reset admin password to default"
    Write-Host "  reset-lockout                Reset account lockout counters"
    Write-Host "  test-admin-endpoints         Run admin API endpoint tests"
    Write-Host "  test-oauth2-endpoints        Run OAuth2 endpoint tests"
    Write-Host "  e2e-admin                    Full test (build + unit + admin API)"
    Write-Host "  e2e-frontend                 Full test with Docker"
    Write-Host "  full-test                    Full build + test + API test cycle"
    Write-Host "  docker-up                    Start the full stack with Docker Compose"
    Write-Host "  docker-down                  Stop the Docker Compose stack"
    Write-Host "  clean                        Clean build artifacts"
    Write-Host "  help                         Show this help"
}

if (-not $Action) {
    Show-Help
    exit 0
}

$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }

# Load paths.env - single source of truth for source/build/SQL/config paths
# (Task 5 / M0, design.md review H2). Later milestones only need to update
# paths.env; this script reads the resulting variables instead of
# hardcoding directory names.
. "$ScriptDir\scripts\backend\paths-env.ps1"
$Paths = Import-PathsEnv -PathsEnvFile (Join-Path $ScriptDir "paths.env")
$ComposeFileAbs = Join-Path $ScriptDir $Paths["COMPOSE_FILE_REL"]

switch ($Action) {
    "build-backend" {
        & "$ScriptDir\scripts\backend\build.bat" "-$($Config.ToLower())"
    }
    "test-backend" {
        & "$ScriptDir\scripts\backend\test.bat" "-$($Config.ToLower())"
    }
    "build-frontend" {
        Push-Location "$ScriptDir\$($Paths['FULLA_FRONTEND_DIR'])"
        try {
            npm install
            npm run build
        } finally { Pop-Location }
    }
    "dev-frontend" {
        Push-Location "$ScriptDir\$($Paths['FULLA_FRONTEND_DIR'])"
        try {
            npm install
            npm run dev
        } finally { Pop-Location }
    }
    "build-admin" {
        Push-Location "$ScriptDir\$($Paths['FULLA_ADMIN_DIR'])"
        try {
            npm install
            npm run build
        } finally { Pop-Location }
    }
    "dev-admin" {
        Push-Location "$ScriptDir\$($Paths['FULLA_ADMIN_DIR'])"
        try {
            npm install
            npm run dev
        } finally { Pop-Location }
    }
    "run-backend" {
        & "$ScriptDir\scripts\backend\run_server.bat" "-$($Config.ToLower())"
    }
    "setup-db" {
        & "$ScriptDir\scripts\backend\setup_database.bat"
    }
    "generate-models" {
        & "$ScriptDir\scripts\backend\generate_models.bat" "-y"
    }
    "reset-password" {
        & "$ScriptDir\scripts\backend\reset-admin-password.ps1"
    }
    "reset-lockout" {
        & "$ScriptDir\scripts\backend\reset-account-lockout.ps1"
    }
    "test-admin-endpoints" {
        & "$ScriptDir\scripts\backend\test-admin-endpoints.ps1"
    }
    "test-oauth2-endpoints" {
        & "$ScriptDir\scripts\backend\test-oauth2-endpoints.ps1"
    }
    "e2e-admin" {
        & "$ScriptDir\scripts\backend\full_test.bat" "-$($Config.ToLower())"
    }
    "e2e-frontend" {
        & "$ScriptDir\scripts\backend\full_test_docker.bat" "-$($Config.ToLower())"
    }
    "full-test" {
        & "$ScriptDir\scripts\backend\full_test.bat" "-$($Config.ToLower())"
    }
    "docker-up" {
        Push-Location $ScriptDir
        try {
            docker compose -f $ComposeFileAbs --project-directory . up -d
        } finally { Pop-Location }
    }
    "docker-down" {
        Push-Location $ScriptDir
        try {
            docker compose -f $ComposeFileAbs --project-directory . down
        } finally { Pop-Location }
    }
    "clean" {
        if (Test-Path "$ScriptDir\$($Paths['BUILD_DIR'])") {
            Remove-Item -Recurse -Force "$ScriptDir\$($Paths['BUILD_DIR'])"
        }
        if (Test-Path "$ScriptDir\$($Paths['FULLA_FRONTEND_DIR'])\dist") {
            Remove-Item -Recurse -Force "$ScriptDir\$($Paths['FULLA_FRONTEND_DIR'])\dist"
        }
        if (Test-Path "$ScriptDir\$($Paths['FULLA_ADMIN_DIR'])\dist") {
            Remove-Item -Recurse -Force "$ScriptDir\$($Paths['FULLA_ADMIN_DIR'])\dist"
        }
        Write-Host "Cleaned build artifacts."
    }
    "help" {
        Show-Help
    }
    default {
        Write-Host "Unknown command: $Action"
        Show-Help
        exit 1
    }
}
