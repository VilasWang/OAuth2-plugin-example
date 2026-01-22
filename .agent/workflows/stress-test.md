---
description: 使用 drogon_ctl press 执行负载与限流压力测试
---

# 压力测试 (Stress Test)

验证系统在高并发下的稳定性及限流机制的有效性。

## 1. 基础设施检查

> 确保 Redis 和 Postgres 正常运行。

// turbo

```powershell
$services = @(
    @{"Name"="Redis"; "Port"=6379},
    @{"Name"="PostgreSQL"; "Port"=5432}
)
foreach ($svc in $services) {
    if (-not (Test-NetConnection -ComputerName localhost -Port $svc.Port).TcpTestSucceeded) {
        Write-Error "CRITICAL: $($svc.Name) service is down."
        exit 1
    }
}
Write-Host "Infrastructure Check Passed."
```

## 2. 启动服务器

> 在后台启动 OAuth2Server。

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release
Start-Process -FilePath ".\OAuth2Server.exe" -PassThru
Write-Host "Server starting..."
Start-Sleep -Seconds 5
if (-not (Test-NetConnection -ComputerName localhost -Port 5555).TcpTestSucceeded) {
    Write-Error "Server failed to start on port 5555"
    exit 1
}
Write-Host "Server is UP."
```

## 3. 基准性能测试 (Baseline)

> 测试静态页面 `/` 的吞吐量，验证非限流路径的基础性能。
> 参数: -n 100000 (请求数), -c 100 (并发数), -t 4 (线程数)

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example
.\drogon_ctl.exe press -n 100000 -c 100 -t 4 http://127.0.0.1:5555/
```

## 4. 限流机制验证 (Rate Limiting)

> 持续请求 `/oauth2/token` (POST)，预期触发 429 Too Many Requests。
> 参数: -n 1000 (请求数), -c 50 (并发数) -m POST

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example
.\drogon_ctl.exe press -n 1000 -c 50 -t 2 -m POST http://127.0.0.1:5555/oauth2/token
```

## 5. 停止服务器

// turbo

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null
Write-Host "Test complete. Server stopped."
```
