# smoke-parity.ps1 - 5-step smoke test to verify manage.ps1 commands work
$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $PSScriptRoot
$Manage = Join-Path $ProjectDir "manage.ps1"
$Result = 0
$ServerProcess = $null

# Load paths.env (repo root) - single source of truth for source/build/SQL
# /config paths (Task 5 / M0, design.md review H2).
. "$PSScriptRoot\backend\paths-env.ps1"
$Paths = Import-PathsEnv -PathsEnvFile (Join-Path $ProjectDir "paths.env")
$ComposeFileAbs = Join-Path $ProjectDir $Paths["COMPOSE_FILE_REL"]

function Cleanup {
    if ($ServerProcess -and -not $ServerProcess.HasExited) {
        Write-Host "[Cleanup] Stopping server..."
        Stop-Process -Id $ServerProcess.Id -Force -ErrorAction SilentlyContinue
    }
    # Ensure docker is down (compose file relocated to deploy/docker/)
    Set-Location $ProjectDir
    docker-compose -f "$ComposeFileAbs" down 2>$null | Out-Null
}

trap { Cleanup } EXIT

Write-Host "========================================"
Write-Host "Smoke Parity Test (Windows)"
Write-Host "========================================"
Write-Host ""

# Step 1: manage build-backend
Write-Host "[Step 1/5] manage build-backend"
& powershell -NoProfile -ExecutionPolicy Bypass -File $Manage build-backend
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] build-backend" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] build-backend" -ForegroundColor Green
Write-Host ""

# Step 2: manage test-backend
Write-Host "[Step 2/5] manage test-backend"
& powershell -NoProfile -ExecutionPolicy Bypass -File $Manage test-backend
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] test-backend" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] test-backend" -ForegroundColor Green
Write-Host ""

# Step 3: manage run-backend & wait + curl /health/ready
Write-Host "[Step 3/5] manage run-backend + health check"
$ServerExeName = "$($Paths['SERVER_BINARY_NAME']).exe"
$ServerExe = Join-Path $ProjectDir "$($Paths['BUILD_DIR'])\$($Paths['SERVER_BUILD_SUBDIR'])\Release\$ServerExeName"
if (-not (Test-Path $ServerExe)) {
    $ServerExe = Join-Path $ProjectDir "$($Paths['BUILD_DIR'])\$($Paths['SERVER_BUILD_SUBDIR'])\Debug\$ServerExeName"
}
if (-not (Test-Path $ServerExe)) {
    Write-Host "[FAIL] Server executable not found" -ForegroundColor Red
    exit 1
}

$ServerProcess = Start-Process -FilePath $ServerExe -WorkingDirectory (Split-Path $ServerExe) -PassThru -WindowStyle Hidden
Write-Host "  Server PID: $($ServerProcess.Id)"
Write-Host "  Waiting for server startup..."
Start-Sleep -Seconds 8

if ($ServerProcess.HasExited) {
    Write-Host "[FAIL] Server process died" -ForegroundColor Red
    exit 1
}

# Health check
try {
    $health = Invoke-WebRequest -Uri "http://127.0.0.1:5555/health/ready" -UseBasicParsing -TimeoutSec 5
    if ($health.StatusCode -eq 200) {
        Write-Host "  /health/ready returned 200"
        Write-Host "[PASS] run-backend + health" -ForegroundColor Green
    } else {
        Write-Host "[FAIL] /health/ready returned $($health.StatusCode)" -ForegroundColor Red
        $Result = 1
    }
} catch {
    Write-Host "[FAIL] /health/ready failed: $($_.Exception.Message)" -ForegroundColor Red
    $Result = 1
}
Write-Host ""

# Step 4: Kill server
Write-Host "[Step 4/5] Kill server"
if (-not $ServerProcess.HasExited) {
    Stop-Process -Id $ServerProcess.Id -Force
}
Write-Host "[PASS] Server stopped" -ForegroundColor Green
Write-Host ""

# Step 5: docker-up -> health -> docker-down
Write-Host "[Step 5/5] docker-up -> health -> docker-down"
& powershell -NoProfile -ExecutionPolicy Bypass -File $Manage docker-up
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] docker-up" -ForegroundColor Red
    exit 1
}
Start-Sleep -Seconds 5

try {
    $dockerHealth = Invoke-WebRequest -Uri "http://127.0.0.1:5555/health/ready" -UseBasicParsing -TimeoutSec 5
    Write-Host "  Docker health: $($dockerHealth.StatusCode)"
} catch {
    Write-Host "  Docker health: N/A (expected if no server in compose)"
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $Manage docker-down
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] docker-down" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] docker-up/docker-down" -ForegroundColor Green
Write-Host ""

# Summary
Write-Host "========================================"
if ($Result -eq 0) {
    Write-Host "ALL SMOKE TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "SMOKE TESTS FAILED" -ForegroundColor Red
}
Write-Host "========================================"
exit $Result
