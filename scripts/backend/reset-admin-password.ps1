#!/usr/bin/env pwsh
# Reset admin password to default 'admin' (PBKDF2-SHA256 hash)
# Usage: .\reset-admin-password.ps1

param(
    [string]$DbHost = "localhost",
    [string]$DbUser = "fulla_user",
    [string]$DbName = "fulla_db",
    [string]$DbPassword = "123456"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Yellow
Write-Host "Reset Admin Password to Default" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

# Default admin password hash (PBKDF2-SHA256, salt embedded in the hash
# Password: 'admin'
$defaultHash = '$pbkdf2-sha256$310000$61646d696e5f736565645f73616c74$6c0307305e1390e1214b15f1f4d0250b2de86aa0e8aa0e008e5cca03084d3d62'
$defaultSalt = ""

# Try Docker first
$containers = docker ps --format "{{.Names}}" 2>$null | Select-String -Pattern "postgres"
$useDocker = $false

if ($containers) {
    $containerName = $containers[0].ToString()
    Write-Host "Found postgres container: $containerName" -ForegroundColor Cyan
    $useDocker = $true
} else {
    Write-Host "No Docker container found, using local PostgreSQL" -ForegroundColor Cyan
    Write-Host "Host: $DbHost, User: $DbUser, Database: $DbName" -ForegroundColor Gray
    
    # Check if psql is available
    $psqlPath = Get-Command psql -ErrorAction SilentlyContinue
    if (-not $psqlPath) {
        Write-Host "Error: psql command not found. Please install PostgreSQL client tools." -ForegroundColor Red
        Write-Host "Or add PostgreSQL bin directory to PATH" -ForegroundColor Yellow
        exit 1
    }
}

Write-Host ""
Write-Host "Resetting admin password to default 'admin'..." -ForegroundColor Cyan

$query = "UPDATE users SET password_hash = '$defaultHash', salt = '$defaultSalt', failed_login_count = 0, locked_until = 0 WHERE username = 'admin';"

if ($useDocker) {
    docker exec $containerName psql -U $DbUser -d $DbName -c $query
} else {
    if ($DbPassword) {
        $env:PGPASSWORD = $DbPassword
    }
    psql -U $DbUser -d $DbName -h $DbHost -c $query
    if ($DbPassword) {
        $env:PGPASSWORD = $null
    }
}

# Verify
$checkQuery = "SELECT username, LEFT(password_hash, 20) as hash_prefix, salt, failed_login_count, locked_until FROM users WHERE username = 'admin';"
Write-Host ""
Write-Host "Current status:" -ForegroundColor Green

if ($useDocker) {
    docker exec $containerName psql -U $DbUser -d $DbName -c $checkQuery
} else {
    if ($DbPassword) {
        $env:PGPASSWORD = $DbPassword
    }
    psql -U $DbUser -d $DbName -h $DbHost -c $checkQuery
    if ($DbPassword) {
        $env:PGPASSWORD = $null
    }
}

Write-Host ""
Write-Host "Admin password reset completed successfully" -ForegroundColor Green
Write-Host "Username: admin" -ForegroundColor White
Write-Host "Password: admin" -ForegroundColor White
Write-Host ""
Write-Host "WARNING: This is a development-only password. DO NOT use in production!" -ForegroundColor Yellow
