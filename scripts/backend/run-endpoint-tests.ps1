<#
.SYNOPSIS
CMake/ctest wrapper for the out-of-process endpoint tests (方案 A).

.DESCRIPTION
Starts the authforge server as a background process, waits for health
readiness, runs the OAuth2 + Admin endpoint test scripts against it, then
stops the server. Returns a non-zero exit code if any endpoint test failed.

Registered as a single ctest entry (EndpointTests_OutOfProcess) so that
`ctest` alone exercises the full out-of-process HTTP stack -- the tests
formerly reachable only via full_test.bat Step 6-7.

Prerequisites (NOT managed by this script -- must be satisfied by the
environment, same as full_test.bat):
  - PostgreSQL + Redis running
  - Database migrated + seeded (setup_database.bat)
  - Server binary built
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$ServerExe,
    [string]$BaseUrl = "http://127.0.0.1:5555"
)
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$result = 0

# Kill any stale server instance on the fixed port.
Stop-Process -Name "authforge-server" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$serverDir = Split-Path -Parent $ServerExe
Write-Host "[endpoint-wrapper] Starting server: $ServerExe"
Write-Host "[endpoint-wrapper] Working dir:   $serverDir"
$proc = Start-Process -FilePath $ServerExe -WorkingDirectory $serverDir -PassThru -WindowStyle Hidden

try {
    # Wait for health readiness (up to 30s).
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        try {
            $r = Invoke-RestMethod -Uri "$BaseUrl/health/live" -Method Get -TimeoutSec 2
            if ($r.status -eq "ok") { $ready = $true; break }
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    if (-not $ready) {
        Write-Error "[endpoint-wrapper] Server did not become ready within 30s"
        return 1
    }
    Write-Host "[endpoint-wrapper] Server ready (PID $($proc.Id))"

    # Run the endpoint test scripts.
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$ScriptDir/test-oauth2-endpoints.ps1" -BaseUrl $BaseUrl
    if ($LASTEXITCODE -ne 0) { $result = 1 }

    & powershell -NoProfile -ExecutionPolicy Bypass -File "$ScriptDir/test-admin-endpoints.ps1" -BaseUrl $BaseUrl
    if ($LASTEXITCODE -ne 0) { $result = 1 }
}
finally {
    Write-Host "[endpoint-wrapper] Stopping server (PID $($proc.Id))"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

if ($result -eq 0) {
    Write-Host "[endpoint-wrapper] ALL endpoint tests PASSED" -ForegroundColor Green
} else {
    Write-Host "[endpoint-wrapper] Some endpoint tests FAILED" -ForegroundColor Red
}
exit $result
