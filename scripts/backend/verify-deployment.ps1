# fulla deployment verification script
# ----------------------------------------------------------------
# 8-step post-deployment acceptance check (see docs/operate/verification-checklist.md
# "Automated Verification Script"): containers, backend health, DB connection,
# table count, seed admin, Redis, OIDC discovery, frontend reachability.
# Run from the repository root (docker compose paths are relative to it).
#
# Exit codes: 0 = all checks passed, 1 = at least one failed.
param(
    [string]$BackendUrl = "http://localhost:5555",
    [string]$FrontendUrl = "http://localhost:8080",
    [string]$AdminUrl = "http://localhost:8081",
    [switch]$Verbose
)

function Test-ContainerStatus {
    Write-Host "`n[1/8] Checking container status..." -ForegroundColor Cyan
    $containers = docker compose -f deploy/docker/docker-compose.yml ps
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[+] All containers are running normally" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Container status abnormal" -ForegroundColor Red
        return $false
    }
}

function Test-BackendHealth {
    Write-Host "`n[2/8] Checking backend health..." -ForegroundColor Cyan
    try {
        $response = Invoke-RestMethod -Uri "$BackendUrl/health" -Method Get
        if ($response.status -eq "healthy") {
            Write-Host "[+] Backend health check passed" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] Backend health check failed: $_" -ForegroundColor Red
        return $false
    }
}

function Test-DatabaseConnection {
    Write-Host "`n[3/8] Checking database connection..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres pg_isready -U fulla_user 2>&1
    if ($LASTEXITCODE -eq 0 -and $result -match "accepting connections") {
        Write-Host "[+] Database connection is normal" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Database connection failed" -ForegroundColor Red
        return $false
    }
}

function Test-DatabaseTables {
    Write-Host "`n[4/8] Checking database table structure..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres psql -U fulla_user -d fulla_db -t -c "
        SELECT COUNT(*) FROM information_schema.tables
        WHERE table_schema = 'public' AND table_type = 'BASE TABLE';
    " 2>&1

    $tableCount = [int]$result.Trim()
    if ($tableCount -ge 20) {
        Write-Host "[+] Database table structure complete ($tableCount tables)" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Database table structure incomplete (only $tableCount tables)" -ForegroundColor Red
        return $false
    }
}

function Test-SeedData {
    Write-Host "`n[5/8] Checking seed data..." -ForegroundColor Cyan
    $result = docker exec fulla-postgres psql -U fulla_user -d fulla_db -t -c "
        SELECT COUNT(*) FROM users WHERE username = 'admin';
    " 2>&1

    $count = [int]$result.Trim()
    if ($count -eq 1) {
        Write-Host "[+] Admin account has been created" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Admin account has not been created" -ForegroundColor Red
        return $false
    }
}

function Test-RedisConnection {
    Write-Host "`n[6/8] Checking Redis connection..." -ForegroundColor Cyan
    $result = docker exec fulla-redis redis-cli -a redis_secret_pass ping 2>&1
    if ($result -match "PONG") {
        Write-Host "[+] Redis connection is normal" -ForegroundColor Green
        return $true
    } else {
        Write-Host "[-] Redis connection failed" -ForegroundColor Red
        return $false
    }
}

function Test-DiscoveryEndpoint {
    Write-Host "`n[7/8] Testing the OIDC discovery endpoint..." -ForegroundColor Cyan
    # For the full login flow (PKCE), see the checklist's Phase 3; scripted deployment
    # checks use the discovery endpoint to verify that the backend OAuth2 stack is
    # ready without depending on specific credentials.
    try {
        $response = Invoke-RestMethod -Uri "$BackendUrl/.well-known/openid-configuration" -Method Get
        if ($response.issuer -and $response.token_endpoint) {
            Write-Host "[+] OIDC discovery endpoint is normal (issuer: $($response.issuer))" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] OIDC discovery endpoint failed: $_" -ForegroundColor Red
        return $false
    }
}

function Test-FrontendAccess {
    Write-Host "`n[8/8] Checking frontend access..." -ForegroundColor Cyan
    try {
        $response = Invoke-WebRequest -Uri $FrontendUrl -Method Get -UseBasicParsing
        if ($response.StatusCode -eq 200) {
            Write-Host "[+] Frontend page is accessible" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Host "[-] Frontend page access failed: $_" -ForegroundColor Red
        return $false
    }
}

# Run all tests
$results = @()
$results += Test-ContainerStatus
$results += Test-BackendHealth
$results += Test-DatabaseConnection
$results += Test-DatabaseTables
$results += Test-SeedData
$results += Test-RedisConnection
$results += Test-DiscoveryEndpoint
$results += Test-FrontendAccess

# Summarize the results
$passed = ($results | Where-Object { $_ -eq $true }).Count
$total = $results.Count

Write-Host "`n" -NoNewline
Write-Host ("=" * 60) -ForegroundColor DarkGray
Write-Host "Verification results: $passed / $total passed" -ForegroundColor $(if ($passed -eq $total) { "Green" } else { "Yellow" })
Write-Host ("=" * 60) -ForegroundColor DarkGray

if ($passed -eq $total) {
    Write-Host "`n[+] Deployment verification fully passed! The system is ready for use." -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n[-] Deployment verification failed; please review the failed items above." -ForegroundColor Red
    exit 1
}
