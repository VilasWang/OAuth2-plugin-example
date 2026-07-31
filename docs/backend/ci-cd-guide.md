# CI/CD 流水线指南 (CI/CD Guide)

本文档说明项目的持续集成与持续交付（CI/CD）机制，基于 **GitHub Actions**。

---

## 1. 流水线概览

CI 配置位于 `.github/workflows/ci.yml`，由两个 Job 组成：

```
Push/PR 到 master
        │
        ├── Job 1: build-and-test (ubuntu-latest)
        │     ├── 安装系统依赖
        │     ├── 安装 Conan (依赖管理)
        │     ├── 配置并构建 Drogon (带缓存)
        │     ├── 编译项目 (Release)
        │     ├── 等待 Postgres/Redis Service 就绪
        │     ├── 初始化数据库 Schema
        │     ├── 运行 ctest
        │     └── [失败时] 上传测试日志 Artifact
        │
        └── Job 2: docker-build (ubuntu-latest)
              └── docker build（验证 Dockerfile 可正常构建）
```

---

## 2. 触发条件

```yaml
on:
  push:
    branches: ["master"]
  pull_request:
    branches: ["master"]
```

- **Push to master**：每次合并到 master 后自动触发全量检查。
- **Pull Request**：每次 PR 创建/更新时在合并前自动触发，作为门禁检查。

---

## 3. 核心 Job 详解：`build-and-test`

### 3.1 Service Containers

CI 在同一 Job 中自动启动 Postgres 和 Redis 容器，并通过健康检查确保服务就绪：

| Service | 镜像 | 端口 | 密码 |
|---|---|---|---|
| PostgreSQL | `postgres:15-alpine` | `5432` | `123456` |
| Redis | `redis:alpine` | `6379` | 无（CI 环境简化配置）|

> [WARNING]️ **注意**：CI 中 Redis 无密码，因此测试配置通过环境变量 `OAUTH2_REDIS_PASSWORD=""` 覆盖。

### 3.2 构建缓存策略

为加速 CI 构建速度，设置了两个缓存层：

| 缓存 | 缓存键 | 内容 |
|---|---|---|
| **Conan 依赖缓存** | `conan-{OS}-{conanfile.txt hash}` | `~/.conan2` 目录（第三方依赖）|
| **Drogon 构建缓存** | `drogon-{OS}-{DROGON_VERSION}-{BUILD_TYPE}` | Drogon 编译产物 |

初次构建约需 **15-20 分钟**；缓存命中后降至 **3-5 分钟**。

### 3.3 数据库初始化

测试前执行 migration 脚本初始化数据库：

```bash
# 按顺序执行所有 migration 文件
for f in apps/server/migrations/V*.sql; do
    psql -h localhost -U oauth2_user -d oauth2_db -f "$f"
done

# 执行 seed 数据（开发/测试环境）
for f in apps/server/seed/*.sql; do
    psql -h localhost -U oauth2_user -d oauth2_db -f "$f"
done
```

> **注意**: 旧的 `sql/001_*.sql` ~ `sql/004_*.sql` 文件已废弃并删除，所有 schema 定义统一在 `apps/server/migrations/` 目录中管理。

### 3.4 测试执行

```bash
ctest -V -C Release --output-on-failure --timeout 120
```

- `-V` : 详细输出
- `--output-on-failure` : 失败时打印测试标准输出
- `--timeout 120` : 单个测试最长 2 分钟

### 3.5 失败日志上传

测试失败时，CI 会自动打包并上传以下内容作为 Artifact（保留 7 天）：
- `build/Testing/` — CTest 测试报告
- `apps/server/logs/` — 应用运行日志

---

## 4. Job 2: `docker-build`

验证 `Dockerfile` 在每次 CI 运行时均可成功构建镜像，防止 Dockerfile 残留导致部署失败：

```yaml
- name: Build the Docker image
  run: docker build . --file Dockerfile --tag oauth2-backend:v1.9.13
```

此 Job **不推送**镜像到 Registry，仅验证构建可行性。

---

## 5. 本地复现 CI 环境

如需本地模拟 CI 行为：

```powershell
# 1. 启动基础设施（CI 中使用 Service Container，本地用 Docker）
docker run -d -p 5432:5432 -e POSTGRES_USER=test -e POSTGRES_PASSWORD=123456 -e POSTGRES_DB=oauth2_db postgres:15-alpine
docker run -d -p 6379:6379 redis:alpine

# 2. 初始化数据库
$env:PGPASSWORD = "123456"
Get-ChildItem "apps\server\migrations\V*.sql" | Sort-Object Name | ForEach-Object {
    psql -h localhost -U oauth2_user -d oauth2_db -f $_.FullName
}
Get-ChildItem "apps\server\seed\*.sql" | ForEach-Object {
    psql -h localhost -U oauth2_user -d oauth2_db -f $_.FullName
}

# 3. 构建并运行测试（build.bat 走 Conan + cmake --preset，Release 落到 build/windows-msvc）
.\scripts\backend\build.bat -release
cd build\windows-msvc
$env:OAUTH2_REDIS_PASSWORD = ""
ctest -V -C Release --output-on-failure
```

---

## 6. Multi-Platform CI

The project now supports comprehensive multi-platform CI/CD. See [Multi-Platform CI Design](../design/superpowers/specs/2026-04-14-multiplatform-ci-design.md) for detailed information.

### Quick Reference

- **Workflow File:** `.github/workflows/ci-multiplatform.yml`
- **Platforms:** Linux (ubuntu-22.04), Windows (windows-2022), macOS (macos-14)
- **Trigger:** Push to master, pull requests, manual workflow dispatch
- **Runtime:** ~15-20 minutes cold cache, ~3-5 minutes warm cache per platform

### Platform-Specific Features

Each platform includes optimized caching and dependency management:

- **Linux:** System dependencies via apt, Docker services for PostgreSQL/Redis
- **Windows:** Conan package management, MSVC 2022 compiler
- **macOS:** Homebrew dependencies, architecture-specific builds (x86_64), OpenSSL@1.1

### Troubleshooting

For common issues, debugging tips, and performance optimization guidance, refer to the [Multi-Platform CI Design](../design/superpowers/specs/2026-04-14-multiplatform-ci-design.md).

---

## 7. 未来扩展建议

| 功能 | 说明 |
|---|---|
| **Docker 镜像发布** | 在 `docker-build` Job 中添加 `docker push` 步骤，推送至 GitHub Container Registry (ghcr.io) |
| **E2E 浏览器测试** | 添加 Job 运行 Playwright/Cypress 对前端 OAuth2 流程做端到端测试 |
| **安全扫描** | 集成 `trivy` 对 Docker 镜像进行漏洞扫描 |
| **Release 自动化** | 通过 `release.yml` 在打 Git tag 时自动创建 GitHub Release 并上传构建产物 |
