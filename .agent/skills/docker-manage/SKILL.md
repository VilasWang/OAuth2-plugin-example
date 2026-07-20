---
name: docker-manage
description: 管理 OAuth2 项目的 Docker 容器和服务，包括启动、停止、清理和状态检查
disable-model-invocation: true
---

# Docker 管理技能

这个技能帮助你管理 OAuth2 项目的 Docker 开发环境。

## 使用时机

当需要启动、停止、清理或检查 Docker 容器状态时使用 `/docker-manage`。

## 可用操作

### 启动开发环境
启动 PostgreSQL、Redis 和 OAuth2 服务：
```bash
# 启动所有服务
docker-compose -f docker-compose.yml up -d

# 启动调试环境（包含构建工具）
docker-compose -f docker-compose.debug.yml up -d
```

### 停止服务
```bash
# 停止所有服务
docker-compose -f docker-compose.yml down

# 停止并删除卷
docker-compose -f docker-compose.yml down -v
```

### 查看状态
```bash
# 查看容器状态
docker-compose ps

# 查看服务日志
docker-compose logs -f oauth2-server

# 查看特定服务日志
docker-compose logs -f oauth2-postgres
docker-compose logs -f oauth2-redis
```

### 清理资源
```bash
# 停止并删除所有容器、网络、卷
docker-compose down -v

# 清理未使用的镜像
docker image prune -a

# 清理未使用的容器
docker container prune

# 清理未使用的卷
docker volume prune
```

### 进入容器
```bash
# 进入 PostgreSQL 容器
docker-compose exec oauth2-postgres psql -U oauth2_user oauth2_db

# 进入 Redis 容器
docker-compose exec oauth2-redis redis-cli -a 123456

# 进入应用容器（用于调试）
docker-compose exec oauth2-server bash
```

## 服务端口

| 服务 | 端口 | 用途 |
|------|------|------|
| OAuth2 Server | 5555 | HTTP API |
| PostgreSQL | 5432 | 数据库连接 |
| Redis | 6379 | 缓存连接 |

## 健康检查

```bash
# 检查服务健康状态
docker-compose ps

# 检查 PostgreSQL 连接
docker-compose exec oauth2-postgres pg_isready -U oauth2_user

# 检查 Redis 连接
docker-compose exec oauth2-redis redis-cli ping
```

## 故障排查

### 容器无法启动
```bash
# 查看详细日志
docker-compose logs [service-name]

# 检查容器状态
docker-compose ps -a

# 重新构建并启动
docker-compose up -d --build
```

### 端口冲突
```bash
# 检查端口占用
netstat -ano | findstr :5555
netstat -ano | findstr :5432
netstat -ano | findstr :6379

# 修改端口（编辑 docker-compose.yml）
ports:
  - "5556:5555"  # 将主机端口改为 5556
```

### 数据持久化问题
```bash
# 查看卷状态
docker volume ls

# 清理并重新创建卷
docker-compose down -v
docker-compose up -d
```

## 注意事项

- **开发环境**：使用 `docker-compose.yml` 或 `docker-compose.debug.yml`
- **数据持久化**：使用 Docker 卷，容器重启后数据保留
- **性能考虑**：调试镜像较慢，生产环境应使用优化镜像
- **资源限制**：可在 `docker-compose.yml` 中设置资源限制
