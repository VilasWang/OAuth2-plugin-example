---
description: 一键启动前后端服务
---

# 启动服务

## 启动后端服务

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release
Start-Process -FilePath ".\OAuth2Server.exe" -PassThru
```

## 启动前端服务

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Frontend
npm run dev
```

## 验证服务状态

// turbo

```powershell
Write-Host "后端: http://localhost:5555"
Write-Host "前端: http://localhost:5173"
```

## 服务端口

| 服务 | 端口 | 用途 |
|------|------|------|
| 后端 | 5555 | OAuth2 API |
| 前端 | 5173 | Vue 开发服务器 |
| PostgreSQL | 5432 | 数据库 |
| Redis | 6379 | 缓存（可选） |
