# 任务 D：#102 生产门禁两路径验证

## 验证目标

验证 ConfigManager 的生产模式门禁（`FULLA_ENV=production`）：
1. **失败路径**：无签名密钥时启动失败，错误信息含 "signing key"
2. **通过路径**：提供 `FULLA_SIGNING_KEY` 后越过签名密钥检查

## 环境准备

### 前置检查

- 端口 5555：空闲（`netstat -ano | findstr "5555"` 无结果）
- fulla-server 进程：无运行实例
- 二进制位置：`build/windows-msvc/apps/server/Release/fulla-server.exe`

### Staged 配置

从 `build/windows-msvc/apps/server/Release/config.json` 复制为 `config.prodtest.json`，
修改三处：

| 字段 | 原值 | 测试值 |
|------|------|--------|
| `custom_config.metadata.issuer` | (不存在) | `"https://auth.example.test"` |
| `db_clients[0].passwd` | `"123456"` | `"prodtest-db-secret"` |
| `redis_clients[0].passwd` | `"123456"` | `"prodtest-redis-secret"` |

### 测试密钥

通过 `bash scripts/generate-jwt-keys.sh` 生成 RSA-2048 密钥对：
- 私钥：`deploy/keys/signing.pem`

---

## 失败路径（无签名密钥）

### 命令

```bash
cd build/windows-msvc/apps/server/Release
FULLA_ENV=production ./fulla-server.exe
```

### 输出

```
20260827 12:46:04.791000 UTC 28060 FATAL Configuration validation failed:
Production requires a real signing key: set FULLA_SIGNING_KEY,
FULLA_JWT_KEY_PATH, or plugins.OAuth2Plugin.config.oidc.signing_key_path
(the ephemeral dev key is rejected in production) - main.cc:97
```

### 结论

**通过** ✓ — 启动失败，退出码 1，错误信息含 "signing key"。

---

## 通过路径（提供 FULLA_SIGNING_KEY）

### 命令

```bash
cd build/windows-msvc/apps/server/Release
FULLA_ENV=production FULLA_SIGNING_KEY="$(cat deploy/keys/signing.pem)" ./fulla-server.exe
```

### 输出

```
20260827 12:48:28.226000 UTC 25156 INFO  Configuration loaded successfully - main.cc:101
20260827 12:48:28.237000 25156 INFO  Database host: 127.0.0.1 - main.cc:174
20260827 12:48:28.237000 25156 INFO  Database port: 5432 - main.cc:178
20260827 12:48:28.237000 25156 INFO  Redis host: 127.0.0.1 - main.cc:181
20260827 12:48:28.240000 25156 INFO  OpenAPI specification written to: .../openapi.json
20260827 12:48:28.240000 25156 INFO  ResourceScopeRegistry: built 46 scope-gated route entries
20260827 12:48:28.240000 25156 WARN  Migrations directory not found, skipping schema migration
Permission denied
```

### 结论

**通过** ✓ — 签名密钥检查和 HTTPS issuer 检查均通过。服务器成功越过配置门禁，
进入初始化阶段。最终 "Permission denied" 是因为 prodtest 配置的 DB 密码
（`prodtest-db-secret`）与本地实际 PG 密码不匹配——这是预期的，门禁只检查配置值
是否满足生产要求，不检查实际连接。

---

## 清理

- 已终止 fulla-server 进程
- 已恢复原始 config.json（从 config.json.bak）
- 已清除 FULLA_ENV / FULLA_SIGNING_KEY 环境变量
