#!/usr/bin/env pwsh
# Reset account lockout for all users
# Usage: .\reset-account-lockout.ps1 [username]
# 
# For local PostgreSQL, set environment variable first:
#   $env:PGPASSWORD = "your_password"
#   .\reset-account-lockout.ps1
# Or pass connection string:
#   .\reset-account-lockout.ps1 -Username admin -DbHost localhost -DbUser fulla_user -DbPassword your_password

param(
    [string]$Username = "",
    [string]$DbHost = "localhost",
    [string]$DbUser = "fulla_user",
    [string]$DbName = "fulla_db",
    [string]$DbPassword = "123456"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Yellow
Write-Host "Reset Account Lockout" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

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

if ($Username) {
    # Reset specific user
    Write-Host "Resetting lockout for user: $Username" -ForegroundColor Cyan
    $query = "UPDATE users SET failed_login_count = 0, locked_until = 0 WHERE username='$Username';"
    
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
    
    # Show result
    $checkQuery = "SELECT username, failed_login_count, locked_until FROM users WHERE username='$Username';"
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
} else {
    # Reset all users
    Write-Host "Resetting lockout for ALL users" -ForegroundColor Cyan
    $query = "UPDATE users SET failed_login_count = 0, locked_until = 0;"
    
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
    
    # Show result
    $checkQuery = "SELECT username, failed_login_count, locked_until FROM users ORDER BY username;"
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
}

Write-Host ""
Write-Host "Account lockout reset completed successfully" -ForegroundColor Green
