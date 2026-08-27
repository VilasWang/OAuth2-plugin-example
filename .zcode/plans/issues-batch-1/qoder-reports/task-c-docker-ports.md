# 任务 C：Docker compose 端口验证（#112）

## 验证目标

确认 `deploy/docker/docker-compose.yml` 中 PostgreSQL 和 Redis 的发布端口绑定到
`127.0.0.1`（而非 `0.0.0.0`），防止外部网络直接访问数据库。

## 静态配置验证

### docker-compose.yml 端口配置

```yaml
# 第 89-90 行
fulla-postgres:
  ports:
    - "127.0.0.1:5433:5432"    # ✓ 绑定到 loopback

# 第 111-112 行
fulla-redis:
  ports:
    - "127.0.0.1:6380:6379"    # ✓ 绑定到 loopback
```

两个服务均使用 `127.0.0.1:HOST_PORT:CONTAINER_PORT` 格式，不是 `0.0.0.0`。

## 运行时验证

**状态**：Docker Desktop 未运行（`npipe:////./pipe/dockerDesktopLinuxEngine` 连接失败），
无法进行运行时验证。

```
$ docker compose -f deploy/docker/docker-compose.yml up -d fulla-postgres fulla-redis
unable to get image 'redis:7-alpine': failed to connect to the docker API at
npipe:////./pipe/dockerDesktopLinuxEngine
```

## 预期行为（Docker 可用时）

| 服务 | 端口绑定 | 验证命令 | 预期结果 |
|------|---------|---------|---------|
| PostgreSQL | `127.0.0.1:5433->5432` | `PGPASSWORD=123456 psql -h 127.0.0.1 -p 5433 -U fulla_user -d fulla_db -c "SELECT 1"` | 返回 1 行 |
| Redis | `127.0.0.1:6380->6379` | `redis-cli -p 6380 ping` | NOAUTH（需密码）或 PONG |

## 结论

静态配置正确：端口绑定到 `127.0.0.1`，符合 #112 安全要求。运行时验证因 Docker
不可用而跳过，不影响配置正确性结论。
