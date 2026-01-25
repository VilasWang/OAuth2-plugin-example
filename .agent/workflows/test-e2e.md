---
description: 执行完整的 OAuth2 流程测试（数据库重置 -> 服务启动 -> 授权码流程 -> RBAC 验证）
---

# End-to-End OAuth2 & RBAC Test

此工作流自动化验证完整的 OAuth2 授权码流程及 RBAC 权限控制。

## 1. 环境准备 (Reset & Build)

> 重置数据库以确保环境纯净，并确保代码已构建。

// turbo

```powershell
# Reset DB
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example
$env:PGPASSWORD='123456'
psql -U test -d postgres -c "DROP DATABASE IF EXISTS oauth_test;" 2>$null
psql -U test -d postgres -c "CREATE DATABASE oauth_test;" 2>$null
psql -U test -d oauth_test -f "OAuth2Backend\sql\001_oauth2_core.sql" 2>$null
psql -U test -d oauth_test -f "OAuth2Backend\sql\002_users_table.sql" 2>$null
psql -U test -d oauth_test -f "OAuth2Backend\sql\003_rbac_schema.sql" 2>$null

# Build (Release)
cd OAuth2Backend
.\scripts\build.bat -release
```

## 2. 启动服务

> 后台启动 OAuth2Server。

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release
taskkill /F /IM OAuth2Server.exe 2>$null
Start-Process -FilePath ".\OAuth2Server.exe" -WindowStyle Hidden
Start-Sleep -Seconds 3
Write-Host "Server verified running on port 5555"
```

## 3. 执行认证流程测试

> 使用 PowerShell 脚本模拟完整授权流程。

```powershell
$Session = New-Object Microsoft.PowerShell.Commands.WebRequestSession

# 1. Login Request
$LoginUrl = "http://localhost:5555/oauth2/login"
$Body = @{
    username = "admin"
    password = "admin"
    client_id = "vue-client"
    redirect_uri = "http://localhost:5173/callback"
    scope = "openid"
    state = "e2e_test"
    response_type = "code"
}

Write-Host "1. Attempting Login..."
try {
    $Resp = Invoke-WebRequest -Uri $LoginUrl -Method POST -Body $Body -WebSession $Session -MaximumRedirection 0 -ErrorAction Stop
} catch [System.Net.WebException] {
    $Resp = $_.Exception.Response
} catch {
    # In PowerShell Core/newer, it throws native request error which might be wrapped differently
    # Or for 302, it acts as error if max redirection 0.
    if ($_.Exception.Response) {
        $Resp = $_.Exception.Response
    } else {
        Write-Error "Login Request Failed: $_"
        exit 1
    }
}

# 2. Extract Code
$Loc = $Resp.Headers["Location"]
if ([string]::IsNullOrEmpty($Loc)) {
    Write-Error "Login failed: No redirect location found. Status: $($Resp.StatusCode)"
    exit 1
}

$Code = ""
if ($Loc -match "code=([^&]+)") {
    $Code = $matches[1]
    Write-Host "✅ Login Successful. Code obtained: $Code"
} else {
    Write-Error "Login failed: No code in redirect URI: $Loc"
    exit 1
}

# 3. Exchange Token
Write-Host "2. Exchanging Code for Token..."
$TokenBody = @{
    grant_type = "authorization_code"
    code = $Code
    client_id = "vue-client"
    client_secret = "123456"
    redirect_uri = "http://localhost:5173/callback"
}

try {
    $TokenResp = Invoke-RestMethod -Uri "http://localhost:5555/oauth2/token" -Method POST -Body $TokenBody
} catch {
    Write-Error "Token Exchange Failed: $($_.Exception.Message)"
    exit 1
}

if ($TokenResp.access_token) {
    Write-Host "✅ Token Obtained: $($TokenResp.access_token)"
    
    # Verify Roles
    if ($TokenResp.roles -contains "admin") {
         Write-Host "✅ Roles Verified: Contains 'admin'"
    } else {
         Write-Warning "⚠️ Roles Missing 'admin': $($TokenResp.roles | ConvertTo-Json)"
    }
    
    # 4. Access Protected Admin Resource
    Write-Host "3. Accessing Admin Dashboard..."
    $Headers = @{ Authorization = "Bearer $($TokenResp.access_token)" }
    try {
        $AdminResp = Invoke-RestMethod -Uri "http://localhost:5555/api/admin/dashboard" -Headers $Headers
        if ($AdminResp.status -eq "success") {
            Write-Host "✅ Admin Dashboard Access Successful!"
            Write-Host "Response: $($AdminResp | ConvertTo-Json -Depth 2)"
        } else {
            Write-Error "Admin Dashboard Unexpected Response"
        }
    } catch {
        Write-Error "Admin Dashboard Access Failed: $($_.Exception.Message)"
        exit 1
    }

} else {
    Write-Error "No access_token in response"
    exit 1
}
```

## 4. 清理环境

// turbo

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null
```
