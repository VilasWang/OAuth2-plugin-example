# Docker 容器和镜像规范指南

本文档规定了本项目在 Docker 环境下的命名规范、构建流程及自动化验证方法。本项目采用统一的多阶段构建 `Dockerfile`。

## 1. 规范化命名 (Standardized Naming)

### 1.1 镜像命名规范

| 镜像用途 | 镜像名称 | 标签 | 构建目标 (--target) | 说明 |
|---------|---------|------|--------------------|---------|
| 生产后端 | `fulla-backend` | `<ver>`（当前 `v1.0.0`） | `backend-runtime` | 仅包含运行时，体积小 |
| 调试后端 | `fulla-backend-debug` | `<ver>`（当前 `v1.0.0`） | `backend-dev` | 包含完整编译工具链 |
| 生产前端 | `fulla-frontend` | `latest` | `frontend-runtime` | Nginx + 静态资源 |

> 版本号由 `cmake/Version.cmake` 统一定义（当前 `FULLA_PROJECT_VERSION = 1.0.0`），生产镜像以 GHCR 多架构镜像发布，详见 [Releases & Supply Chain Security](../../README.md#releases--supply-chain-security)。

### 1.2 容器命名规范

格式：`oauth2-{service}[-debug]`

| 服务类型 | 容器名称 (Release) | 容器名称 (Debug) |
|-----|-------------------|-----------------|
| 后端服务 | `fulla-backend` | `fulla-backend-debug` |
| 前端服务 | `fulla-frontend` | - |
| 数据库 (PostgreSQL) | `fulla-postgres` | `fulla-postgres-debug` |
| 缓存 (Redis) | `fulla-redis` | `fulla-redis-debug` |

### 1.3 网络命名规范
*   **Release 网络**: `oauth2-net`
*   **Debug 网络**: `oauth2-debug-net`

---

## 2. 构建与部署流程 (Build & Deployment)

### 2.1 Docker Compose 配置文件

项目提供三个 Docker Compose 配置文件，位于 `deploy/docker/` 目录：

| 配置文件 | 用途 | 服务数量 | 适用场景 |
| -------- | ---- | -------- | -------- |
| `docker-compose.yml` | 标准开发环境 | 6 | 日常开发和集成测试 |
| `docker-compose.debug.yml` | 调试环境 | 3 | 深度调试和问题排查 |
| `docker-compose.prod.yml` | 生产环境 | 8 | 生产部署（含 Nginx 与 migrate 迁移作业） |

**重要提示**: 所有docker-compose命令需要在**项目根目录**执行，并使用 `-f deploy/docker/docker-compose.yml` 指定配置文件路径。

### 2.2 环境清理

在重新构建前，建议清理旧的容器和未使用的镜像：

```powershell
# 使用脚本一键清理
.\scripts\cleanup-docker.sh

# 或手动清理（在项目根目录）
docker-compose -f deploy/docker/docker-compose.yml down --remove-orphans
docker-compose -f deploy/docker/docker-compose.debug.yml down --remove-orphans
docker image prune -f
```

### 2.3 调试环境 (Debug)

用于开发测试，支持挂载源码、GDB 调试和单元测试。

- **构建镜像**:

    ```powershell
    # 使用统一 Dockerfile 的 backend-dev 目标构建
    docker build -f deploy/docker/Dockerfile --target backend-dev -t fulla-backend-debug:v1.0.0 .
    ```

- **启动服务** (在项目根目录):

    ```powershell
    docker-compose -f deploy/docker/docker-compose.debug.yml up -d
    ```

- **进入容器**:

    ```powershell
    docker-compose -f deploy/docker/docker-compose.debug.yml run --rm debug-env bash
    ```

- **停止服务**:

    ```powershell
    docker-compose -f deploy/docker/docker-compose.debug.yml down
    ```

### 2.4 标准开发环境 (Standard)

用于日常开发和集成测试，包含完整的后端、前端、管理后台和监控服务。

- **仅启动数据库服务** (最常用):

    ```powershell
    # 只启动 PostgreSQL 和 Redis，本地运行后端/前端
    docker-compose -f deploy/docker/docker-compose.yml up -d fulla-postgres fulla-redis
    
    # 验证数据库状态
    docker-compose -f deploy/docker/docker-compose.yml ps
    ```

- **启动所有服务**:

    ```powershell
    # 构建并启动所有服务（后端、前端、管理后台、数据库、Redis、Prometheus）
    docker-compose -f deploy/docker/docker-compose.yml up -d
    ```

- **查看日志**:

    ```powershell
    # 查看所有服务日志
    docker-compose -f deploy/docker/docker-compose.yml logs -f
    
    # 查看特定服务日志
    docker-compose -f deploy/docker/docker-compose.yml logs -f fulla-backend
    ```

- **停止服务**:

    ```powershell
    # 停止并删除容器
    docker-compose -f deploy/docker/docker-compose.yml down
    
    # 停止并删除容器和数据卷（会清空数据库）
    docker-compose -f deploy/docker/docker-compose.yml down -v
    ```

### 2.5 生产环境 (Production)

用于模拟真实部署，镜像体积小，安全性高，包含 Nginx 反向代理。

- **准备环境变量文件**:

    ```powershell
    # 复制示例配置文件
    cp deploy/env/docker.env.example .env.docker
    
    # 编辑 .env.docker 文件，设置生产环境参数
    # 必需的环境变量：
    # - FULLA_DB_HOST, FULLA_DB_NAME, FULLA_DB_PASSWORD
    # - FULLA_REDIS_HOST, FULLA_REDIS_PASSWORD
    # - POSTGRES_USER, POSTGRES_PASSWORD, POSTGRES_DB
    # - REDIS_PASSWORD
    ```

- **启动生产环境**:

    ```powershell
    # 使用环境变量文件启动
    docker-compose -f deploy/docker/docker-compose.prod.yml --env-file .env.docker up -d
    ```

- **访问服务**:
  - 前端: http://localhost:8080
  - 管理后台: http://localhost:8081
  - 后端API: http://localhost:5555
  - Prometheus: http://localhost:9090

---

## 3. 自动化验证 (Automated Verification)

### 3.1 调试环境验证 (`docker-quick-verify-debug.sh`)

该脚本在容器内部运行，涵盖从依赖检查到编译测试的全流程。

- **运行命令** (已针对 Windows 路径/换行符优化):

    ```powershell
    docker-compose -f deploy/docker/docker-compose.debug.yml run --rm debug-env bash -c "find scripts -name '*.sh' -exec sed -i 's/\r//' {} + && tr -d '\r' < /app/deploy/docker/docker-quick-verify-debug.sh > /tmp/v.sh && bash /tmp/v.sh"
    ```

- **验证步骤**:
  1. 检查 Drogon 框架是否存在。
  2. 等待 PostgreSQL 和 Redis 就绪。
  3. 执行数据库初始化。
  4. 并行编译项目 (`cmake --build . --parallel $(nproc)`)。
  5. 运行所有单元测试。

### 3.2 一键测试脚本

项目提供了便捷的一键测试脚本 `full_test_docker.bat`，自动完成构建和测试全流程：

- **运行命令** (在项目根目录):

    ```powershell
    # Windows
    .\scripts\backend\full_test_docker.bat -debug
    ```

- **执行步骤**:
  1. 启动 PostgreSQL 和 Redis 容器
  2. 等待数据库就绪
  3. 初始化数据库（迁移和种子数据）
  4. 重新生成 ORM 模型
  5. 编译项目
  6. 运行单元测试
  7. 启动服务器
  8. 测试 OAuth2 端点
  9. 测试管理端点
  10. 清理和停止服务

---

## 4. 常见使用场景 (Common Use Cases)

### 4.1 本地开发 (推荐)

最常见的开发模式：Docker运行数据库，本地运行后端和前端。

```powershell
# 1. 启动数据库和Redis
docker-compose -f deploy/docker/docker-compose.yml up -d fulla-postgres fulla-redis

# 2. 本地运行后端
.\scripts\backend\run_server.bat -debug

# 3. 本地运行前端（新终端）
cd frontends/user
npm run dev

# 4. 本地运行管理后台（新终端）
cd frontends/admin
npm run dev
```

访问地址：
- 前端: http://localhost:5173
- 管理后台: http://localhost:5174
- 后端API: http://localhost:5555

### 4.2 完整集成测试

使用Docker运行完整的服务栈：

```powershell
# 启动所有服务
docker-compose -f deploy/docker/docker-compose.yml up -d

# 查看服务状态
docker-compose -f deploy/docker/docker-compose.yml ps

# 查看后端日志
docker-compose -f deploy/docker/docker-compose.yml logs -f fulla-backend
```

### 4.3 调试模式

需要进入容器调试或运行GDB：

```powershell
# 启动调试环境
docker-compose -f deploy/docker/docker-compose.debug.yml up -d

# 进入容器
docker-compose -f deploy/docker/docker-compose.debug.yml exec debug-env bash

# 在容器内编译和调试（与宿主机一致，走 Conan + cmake --preset）
conan install . --output-folder=build/linux-debug -s build_type=Debug -s compiler.cppstd=17 --build=missing
cmake --preset linux-debug
cmake --build --preset linux-debug
gdb build/linux-debug/apps/server/fulla-server
```

---

## 5. 故障排查 (Troubleshooting)

### 5.1 路径问题

**问题**: `cannot find file specified` 或路径解析错误

**解决方案**:
- 确保在**项目根目录**执行命令
- 使用完整路径 `-f deploy/docker/docker-compose.yml`
- Git Bash 用户添加 `MSYS_NO_PATHCONV=1` 前缀

### 5.2 端口冲突

**问题**: `port is already allocated`

**解决方案**:

```powershell
# 检查端口占用
netstat -ano | findstr "5555"
netstat -ano | findstr "5433"

# 停止占用端口的容器
docker-compose -f deploy/docker/docker-compose.yml down
```

### 5.3 换行符问题

**问题**: `$'\r': command not found`

**解决方案**: 确保脚本以 `LF` 格式保存，或使用 `sed` 转换命令（已在验证脚本中内置）。

### 5.4 构建性能

**建议**: 
- 分配 CPU 4+，内存 4GB+
- 使用 `DOCKER_BUILDKIT=1` 加速构建
- 清理未使用的镜像和卷

### 5.5 数据库连接失败

**检查步骤**:

```powershell
# 1. 验证容器运行状态
docker-compose -f deploy/docker/docker-compose.yml ps

# 2. 检查PostgreSQL健康状态
docker exec fulla-postgres pg_isready -U fulla_user -d fulla_db

# 3. 查看数据库日志
docker-compose -f deploy/docker/docker-compose.yml logs fulla-postgres
```

---

## 6. 快速参考 (Quick Reference)

### 6.1 常用命令

```powershell
# === 在项目根目录执行 ===

# 启动数据库（本地开发）
docker-compose -f deploy/docker/docker-compose.yml up -d fulla-postgres fulla-redis

# 启动所有服务（集成测试）
docker-compose -f deploy/docker/docker-compose.yml up -d

# 查看服务状态
docker-compose -f deploy/docker/docker-compose.yml ps

# 查看日志
docker-compose -f deploy/docker/docker-compose.yml logs -f [service_name]

# 停止服务
docker-compose -f deploy/docker/docker-compose.yml down

# 停止并删除数据
docker-compose -f deploy/docker/docker-compose.yml down -v

# 重启服务
docker-compose -f deploy/docker/docker-compose.yml restart [service_name]

# 进入调试容器
docker-compose -f deploy/docker/docker-compose.debug.yml exec debug-env bash

# 一键构建和测试
.\scripts\backend\full_test_docker.bat -debug
```

### 6.2 服务端口映射

| 服务 | 容器端口 | 主机端口 | 说明 |
| ---- | -------- | -------- | ---- |
| PostgreSQL | 5432 | 5433 | 生产数据库 |
| PostgreSQL (Debug) | 5432 | 5432 | 调试数据库 |
| Redis | 6379 | 6380 | 缓存服务 |
| Backend API | 5555 | 5555 | 后端API |
| Frontend | 80 | 8080 | 前端应用 |
| Admin | 80 | 8081 | 管理后台 |
| Prometheus | 9090 | 9090 | 监控服务 |
| Nginx (Prod) | 80/443 | 80/443 | 反向代理 |

---

**最后更新**: 2026-06-02  
**版本**: 2.2.0  
**变更**: 修正所有docker-compose文件路径，添加详细的使用场景和故障排查指南
