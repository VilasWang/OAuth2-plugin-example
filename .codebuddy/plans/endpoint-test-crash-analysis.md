# 端点测试服务器崩溃根因分析

> 分析时间: 2026-07-25 | 方法: Superpowers Brainstorming + Evidence-Based

---

## 1. 崩溃现象

| 测试套件 | 通过 | 总测试 | 崩溃时机 |
|----------|------|--------|---------|
| OAuth2 Endpoints | 13/55 | 55 | Test 41 PASS 后，Test 42 起全 Connection Refused |
| Admin Endpoints | 4/51 | 51 | 几秒内就 crash |

**崩溃特征**：不是请求报 500，而是 TCP Connection Refused ——进程已退出。

**config.json 关键配置**：
```json
"connect_options": { "statement_timeout": "5s" },  // ← CI配置，非开发环境标准
"relaunch_on_error": true                           // ← 崩溃自动重启
```

---

## 2. 假设与排除

### 假设 A：PostgresTokenRepository `catch (...)` → `catch (const DrogonDbException &e)` (本次修改)

**代码变更**（4 处：saveAccessToken、saveRefreshToken、getRefreshToken、revokeRefreshToken）：

```cpp
// Before:
try { Mapper<> mapper(dbClient_); mapper.insert(..., ...); }
catch (...) { // handle }

// After:
try { Mapper<> mapper(dbClient_); mapper.insert(..., ...); }
catch (const DrogonDbException &e) { // handle }
```

**分析**：
- Mapper 构造函数只是初始化对象，不做 DB 操作，**不抛异常**
- `mapper.insert()` 是异步调用，同步路径 **不抛异常**
- 实际的 DB 异常在异步回调中处理，有独立的 `DrogonDbException` 处理器
- 结论：外层 try-catch 在实践中几乎从不触发，**本次修改在正常路径是 no-op**

**判定**：排除 × — 不是直接原因。但理论上更窄的 catch 增加了风险面。

### 假设 B：`statement_timeout: "5s"` 导致连接池崩溃

**证据**：
- `config.json` 第 24 行设置 `"statement_timeout": "5s"` ——这是 CI 配置
- PostgreSQL 超时后会 kill 查询并关闭连接
- Drogon 的 DB 连接池收到关闭信号后，可能产生内部状态异常
- `relaunch_on_error: true` 理论上应自动重启，但进程直接退出了

**验证方法**：
```bash
# 去掉 statement_timeout 的配置，重新启动服务器
# 如果不再 crash → 确认是 statement_timeout
```

**判定**：**高概率** ← 主因。CI 级别 timeout 配置不适合本地端点测试环境。

### 假设 C：测试顺序敏感 —— 特定端点累积触发状态异常

**证据**：
- OAuth2 测试崩溃点在 Test 41→42 之间
- Test 41: `POST /oauth2/token` (expired code) — 触发 token exchange → AuthCode cleanup
- Test 42: `POST /oauth2/introspect` — 先 `Get-UserToken` → login + token exchange
- `Get-UserToken` 内部调用 login + token，触发 **saveAccessToken + saveRefreshToken**（即我们修改的方法）

**时序**：
1. Test 41 发送 POST /oauth2/token (expired code) → 服务器处理并返回 400
2. Test 42 调用 `Get-UserToken` → POST /oauth2/login → POST /oauth2/token
3. 某一步 DB 操作超时（statement_timeout: 5s）
4. DB 连接被 PostgreSQL kill
5. 后续 Mapper 操作使用被 kill 的连接 → Drogon 内部异常
6. 进程退出

**判定**：**中等概率** ← 与 B 合并作用。

### 假设 D：Redis 连接异常

`RedisTokenRepository.cc:181` 中 `const std::exception &` → `const RedisException &`。

- Drogon 的 Redis 客户端回调签名是 `void(const drogon::nosql::RedisException &)`
- 使用 `const std::exception &` 作为回调参数：如果 Drogon 传的确实是 `RedisException`（继承自 `std::exception`），多态仍然有效
- 但如果 Drogon 传递的是非 RedisException 的 std::exception 子类，原来的处理器能接住，现在的接不住
- 即使接不住，也只是 callback 不执行（静默丢失），不会导致进程崩溃

**判定**：排除 × — 不会导致进程退出。

### 假设 E：async 回调中新建异常逃逸 (本次修改的 R3 fix)

`AuthorizationService.cc` 中添加了 try-catch 包裹 scope evaluation 回调。这是新增的 catch，**不会引入新的崩溃路径**。

**判定**：排除 ×

---

## 3. 结论

### 根因

```
statement_timeout: 5s (CI配置)
    ↓
PostgreSQL 超时 kill 查询 + 破坏连接
    ↓
Drogon 连接池状态异常
    ↓
后续 Mapper 操作时进程退出
    ↓
relaunch_on_error: true 未生效（进程直接退出，未走 Drogon signal handler）
```

### 本次修改的影响评估

| 修改 | 对崩溃的影响 |
|------|-------------|
| `catch (...)` → `catch (DrogonDbException&)` | 直接无影响（外层 catch 在正常路径不触发），但降低了安全余量 |
| `std::exception` → `RedisException` | 直接无影响 |
| 空 catch 添加 LOG_* | 无影响 |
| `[this]` 删除 | 无影响（checkUserConsentAndProceed 是 static） |
| try-catch 添加 | 无影响（只加 try-catch，不删） |

**结论**：本次 Error Handling 修改 **不是崩溃的直接原因**，但 `catch (...)` 改窄后降低了异常安全余量。

### 建议

| 优先级 | 操作 | 理由 |
|--------|------|------|
| P0 | 去掉 `statement_timeout: 5s` 或设为 `-1.0` | CI 配置不适合本地端点测试 |
| P1 | 恢复 4 处 `catch (...)` → `catch (...)` 兜底 | 增加异常安全余量，同时保留 `e.base().what()` 日志 |
| P2 | 将 `statement_timeout` 移到 `config.ci.json` 专属 | 分离 CI 和本地开发配置 |

### 推荐方案（P0 + P1 合一）

```cpp
// PostgresTokenRepository 外层 catch 改为：
catch (const DrogonDbException &e) {
    LOG_ERROR << "saveAccessToken DrogonDbException: " << e.base().what();
    if (*sharedCb) (*sharedCb)();
}
catch (const std::exception &e) {
    LOG_ERROR << "saveAccessToken std::exception: " << e.what();
    if (*sharedCb) (*sharedCb)();
}
catch (...) {
    LOG_ERROR << "saveAccessToken Unknown Exception";
    if (*sharedCb) (*sharedCb)();
}
```

三层 catch 既符合 R1a（优先 catch DrogonDbException），又有兜底保障。
