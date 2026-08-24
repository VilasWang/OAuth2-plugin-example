# CI/CD 流水线指南 (CI/CD Guide)

本文档说明项目的持续集成与持续交付（CI/CD）机制，基于 **GitHub Actions**。

---

## 1. 流水线概览

CI 配置位于 `.github/workflows/ci.yml`，由三个 fail-fast 串联的 Job（含一条可复用工作流矩阵）组成：

```
Push/PR 到 master (及 workflow_dispatch)
        │
        ├── FAST gate
        │     ├── static-checks (ubuntu-22.04) — 源码级守卫：
        │     │     arch-guard / migration-check / api-diff /
        │     │     测试命名 / manage 脚本对等 / OpenAPI 校验 /
        │     │     OpenAPI 治理门（三层一致性 + 版本同步）
        │     └── frontend (_frontend.yml) — 前端属性测试
        │
        ├── openapi-governance (openapi-governance.yml, PR 触发) —
        │     oasdiff 破坏性变更门（base vs PR 的 openapi.yaml；
        │     豁免清单 tools/openapi-governance/oasdiff-breaking-ignore.md）
        │
        ├── MAIN gate
        │     └── build-test (_build-test.yml × {linux, windows, macos} 矩阵)
        │           ├── 安装系统依赖 / Conan
        │           ├── 配置并构建 (Conan + cmake --preset，带缓存)
        │           ├── [linux] 启动 Postgres/Redis 容器并等待就绪
        │           ├── [linux] 初始化数据库 Schema
        │           ├── 运行 ctest + 命名发布门禁
        │           └── [失败时] 上传测试日志 Artifact
        │
        └── RELEASE gate
              └── sdk-smoke (_sdk-smoke.yml) — 全栈 find_package 冒烟
```

---

## 2. 触发条件

```yaml
on:
  push:
    branches: ["master"]
  pull_request:
    branches: ["master"]
  workflow_dispatch:

concurrency:
  group: ci-${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

- **Push to master**：每次合并到 master 后自动触发全量检查。
- **Pull Request**：每次 PR 创建/更新时在合并前自动触发，作为门禁检查。
- **workflow_dispatch**：支持手动触发。
- **并发控制**：同一分支的新运行会取消其进行中的旧运行。

---

## 3. 核心 Job 详解：`build-test`

`build-test` 是一条可复用工作流（`_build-test.yml`），由 `ci.yml` 以 `{linux, windows, macos}` 矩阵调用，三个平台执行同一套 Conan + `cmake --preset` 构建与 CTest 测试，仅在需要时通过矩阵输入启用数据库。

### 3.1 Service Containers（仅 linux 矩阵腿）

CI 在 Linux 矩阵腿中用 Docker 容器启动 Postgres 和 Redis，并通过真实查询（非仅 `pg_isready`）确保就绪：

| Service | 镜像 | 端口 | 密码 |
|---|---|---|---|
| PostgreSQL | `postgres:17-alpine` | `5432` | `123456` |
| Redis | `redis:7-alpine` | `6379` | 无（CI 环境简化配置）|

> PostgreSQL 镜像与 deploy 默认（`postgres:17-alpine`，2026-08-18 起）对齐——CI 覆盖的
> 就是部署目标大版本（含 V025 分区/V026 等 migration 在 17 上的行为）。

> [WARNING]️ **注意**：CI 中 Redis 无密码，因此测试配置通过环境变量 `OAUTH2_REDIS_PASSWORD=""` 覆盖。Windows/macOS 矩阵腿 `use_database=false`，改用内存存储配置（`config.ci.json`）。

### 3.2 构建缓存策略

为加速 CI 构建速度，对 Conan 依赖做缓存：

| 缓存 | 缓存键 | 内容 |
|---|---|---|
| **Conan 依赖缓存** | `conan-{OS}-v1-cpp17-{conanfile.py + conan.lock hash}` | `~/.conan2` 目录（第三方依赖，含 Drogon）|

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

## 4. 镜像构建与签名

CI 流水线本身不构建 Docker 镜像。容器镜像的多架构构建、推送 GHCR、cosign 签名与 syft SBOM 由 `release.yml` 在打 SemVer Tag（`vX.Y.Z`）时完成。详见 [Releases & Supply Chain Security](../../README.md#releases--supply-chain-security)。

---

## 5. 本地复现 CI 环境

如需本地模拟 CI 行为：

```powershell
# 1. 启动基础设施（CI 中使用 Service Container，本地用 Docker）
docker run -d -p 5432:5432 -e POSTGRES_USER=oauth2_user -e POSTGRES_PASSWORD=123456 -e POSTGRES_DB=oauth2_db postgres:17-alpine
docker run -d -p 6379:6379 redis:7-alpine

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

## 6. 多平台矩阵

多平台 CI 已合并进 `ci.yml` 的 `build-test` Job，通过 `include` 矩阵在同一套可复用工作流（`_build-test.yml`）上跑三个平台。历史设计文档见 [Multi-Platform CI Design](../history/design/superpowers/specs/2026-04-14-multiplatform-ci-design.md)（已归档）。

### Quick Reference

- **Workflow File:** `.github/workflows/ci.yml`（调用 `_build-test.yml`）
- **Platforms:** Linux (ubuntu-22.04), Windows (windows-2022), macOS (macos-14)
- **Trigger:** Push to master, pull requests, manual workflow dispatch
- **Runtime:** ~15-20 minutes cold cache, ~3-5 minutes warm cache per platform

### Platform-Specific Features

每个平台的差异仅通过矩阵输入表达（无复制粘贴的流水线）：

- **Linux:** 系统依赖经 apt 安装；用 Docker 容器跑 PostgreSQL/Redis；执行数据库初始化与命名发布门禁
- **Windows:** Conan 依赖管理，MSVC 2022 编译器；内存存储配置（`use_ci_config`），无外部 DB
- **macOS:** Homebrew（仅 `brew update`）；arm64 构建（`-s arch=armv8`，runner 为 `macos-14`）；内存存储配置

---
