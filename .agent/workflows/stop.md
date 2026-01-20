---
description: 停止所有服务进程
---

# 停止服务

## 停止后端进程

// turbo

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null
Write-Host "后端服务已停止"
```

## 停止前端进程（Vite）

// turbo

```powershell
# 查找并停止 npm/node 进程（端口 5173）
$proc = Get-NetTCPConnection -LocalPort 5173 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty OwningProcess
if ($proc) { Stop-Process -Id $proc -Force; Write-Host "前端服务已停止" } else { Write-Host "前端服务未运行" }
```

## 验证进程已停止

// turbo

```powershell
$backend = Get-Process -Name OAuth2Server -ErrorAction SilentlyContinue
$frontend = Get-NetTCPConnection -LocalPort 5173 -ErrorAction SilentlyContinue
if (-not $backend -and -not $frontend) { Write-Host "✅ 所有服务已停止" } else { Write-Host "⚠️ 仍有服务运行" }
```
