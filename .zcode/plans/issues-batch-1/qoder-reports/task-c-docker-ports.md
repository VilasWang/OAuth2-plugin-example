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

### 启动容器

```
$ docker compose -f deploy/docker/docker-compose.yml up -d fulla-postgres fulla-redis
Container fulla-postgres Started
Container fulla-redis Started
```

### docker ps 端口绑定确认

```
$ docker ps --filter "name=fulla-postgres" --filter "name=fulla-redis"
NAMES            PORTS                      STATUS
fulla-postgres   127.0.0.1:5433->5432/tcp   Up (healthy)
fulla-redis      127.0.0.1:6380->6379/tcp   Up
```

**确认**：两个端口均绑定 `127.0.0.1`，不是 `0.0.0.0`。

### PostgreSQL 可达性

```
$ psql -h 127.0.0.1 -p 5433 -U fulla_user -d fulla_db -c "SELECT 1"
 ?column?
----------
        1
(1 行记录)
```

**确认**：PostgreSQL 通过 loopback 端口 5433 可达，查询正常返回。

### Redis 可达性

```
$ Test-NetConnection -ComputerName 127.0.0.1 -Port 6380
TcpTestSucceeded: True
```

**确认**：Redis 通过 loopback 端口 6380 TCP 可达。

### 容器内验证

```
$ docker exec fulla-postgres psql -U fulla_user -d fulla_db -c "SELECT 1"
 ?column?
----------
        1
(1 row)

$ docker exec fulla-redis redis-cli -a redis_secret_pass ping
PONG
```

### 清理

```
$ docker compose -f deploy/docker/docker-compose.yml down
Container fulla-postgres Stopped
Container fulla-redis Stopped
Network docker_oauth2-net Removed
```

## 结论

**全部通过** ✓

| 验证项 | 结果 |
|--------|------|
| PostgreSQL 端口绑定 `127.0.0.1:5433` | ✓ |
| Redis 端口绑定 `127.0.0.1:6380` | ✓ |
| PostgreSQL loopback 可达（`SELECT 1`） | ✓ |
| Redis loopback TCP 可达 | ✓ |
| 容器清理完成 | ✓ |
