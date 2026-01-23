---
description: 在 Windows 上使用 Docker Desktop 验证 Linux 编译
---

# Docker Verification Workflow

此工作流用于在 Windows + Docker Desktop 环境下验证项目的 Linux 编译与运行能力。

## 1. 构建 Docker 镜像

> 使用项目根目录的 Dockerfile 构建镜像。这会验证完整的 Linux 编译链。

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example
docker build -t oauth2-server .
```

## 2. 运行并验证

> 启动容器并映射端口，验证服务能否正常启动。

```powershell
docker run -d --name oauth2-test -p 15555:5555 oauth2-server
Start-Sleep -Seconds 5
Get-Content -Path "d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\logs\oauth2_prod.log" -ErrorAction SilentlyContinue
Invoke-WebRequest -Uri "http://localhost:15555/"
```

## 3. 清理

```powershell
docker stop oauth2-test
docker rm oauth2-test
```
