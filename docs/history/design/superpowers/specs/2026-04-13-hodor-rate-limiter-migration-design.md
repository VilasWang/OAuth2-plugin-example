# 迁移到Hodor速率限制插件设计文档

**日期**: 2026-04-13
**状态**: 设计阶段
**作者**: Claude Code
**审批状态**: 待审批

---

## 1. 概述

### 1.1 目标

将OAuth2服务器当前的自定义`RateLimiterFilter`替换为Drogon官方的Hodor速率限制插件，提升系统的可维护性、功能性和性能。

### 1.2 当前实现

- **组件**: `RateLimiterFilter`（自定义Filter）
- **算法**: 固定窗口计数器（Fixed Window Counter）
- **存储**: Redis INCR/EXPIRE
- **限制层级**: 仅IP级别
- **配置方式**: 硬编码在C++代码中

**当前限制规则**:
- `/oauth2/login`: 5请求/分钟
- `/oauth2/token`: 10请求/分钟
- `/api/register`: 5请求/分钟

### 1.3 目标实现

- **组件**: `drogon::plugin::Hodor`（官方插件）
- **算法**: Token Bucket（令牌桶）
- **存储**: 内存CacheMap（无Redis依赖）
- **限制层级**: IP + 用户 + 全局
- **配置方式**: JSON配置文件

### 1.4 关键改进

1. [PASS] **多层级限制**: IP、用户、全局三级限制
2. [PASS] **白名单机制**: 内置trust_ips支持，开发/测试环境无限制
3. [PASS] **配置化**: 所有限制规则在config.json中配置，无需重新编译
4. [PASS] **更优算法**: Token Bucket提供更平滑的请求分布
5. [PASS] **消除Redis依赖**: 降低系统复杂度和单点故障风险

---

## 2. 架构设计

### 2.1 架构对比

**当前架构**:
```
Request → RateLimiterFilter (Custom)
           ├── Redis INCR operations
           ├── IP extraction (X-Forwarded-For → X-Real-IP → peerAddr)
           ├── Path-based limit selection (hardcoded)
           └── Decision: pass or 429
```

**新架构**:
```
Request → Hodor Plugin (Global Advice)
           ├── Whitelist check (trust_ips)
           ├── IP extraction (peerAddr or RealIpResolver)
           ├── User ID extraction (callback)
           ├── Multi-level limits (Token Bucket)
           │   ├── Global limit
           │   ├── Per-IP limit
           │   └── Per-user limit
           └── Decision: pass or 429
```

### 2.2 组件关系

```
┌─────────────────────────────────────┐
│         HTTP Request                │
└──────────────┬──────────────────────┘
               │
               ▼
┌──────────────────────────────────────┐
│  Hodor Plugin (Pre-handling Advice) │
│  ┌────────────────────────────────┐ │
│  │ 1. Whitelist Check             │ │
│  │    trust_ips matching          │ │
│  └────────────────────────────────┘ │
│  ┌────────────────────────────────┐ │
│  │ 2. Identity Extraction         │ │
│  │    - IP: peerAddr()            │ │
│  │    - User: userIdGetter_()     │ │
│  └────────────────────────────────┘ │
│  ┌────────────────────────────────┐ │
│  │ 3. Strategy Matching           │ │
│  │    URL regex matching          │ │
│  │    Default + Sub-limits        │ │
│  └────────────────────────────────┘ │
│  ┌────────────────────────────────┐ │
│  │ 4. Token Bucket Checks         │ │
│  │    - Global limiter            │ │
│  │    - Per-IP limiter map        │ │
│  │    - Per-user limiter map      │ │
│  └────────────────────────────────┘ │
└──────────────┬──────────────────────┘
               │
               ▼
┌──────────────────────────────────────┐
│         Controller Layer             │
│  OAuth2Controller                    │
│  (No more "RateLimiterFilter")       │
└──────────────────────────────────────┘
```

### 2.3 数据流

**Hodor处理流程**:

```
Request → Hodor::onHttpRequest()
    ↓
Check whitelist (RealIpResolver::matchCidr)
    ├─ Matched → Skip ALL limits [PASS]
    └─ Not matched → Continue
    ↓
Extract identities
    ├─ IP: req->peerAddr()
    └─ User ID: userIdGetter_(req)
    ↓
For each strategy (specific → general):
    ├─ Check URL regex match
    │   └─ No match → Next strategy
    └─ Matched → Check limits
        ├─ Global limiter
        │   └─ isAllowed()? → No: Reject 429
        ├─ IP limiter (from CacheMap)
        │   └─ isAllowed()? → No: Reject 429
        └─ User limiter (if userId exists)
            └─ isAllowed()? → No: Reject 429
    ↓
All checks passed → chainCallback()
```

---

## 3. 详细配置设计

### 3.1 Hodor配置

**位置**: `config.json` 的 `plugins` 数组

```json
{
    "name": "drogon::plugin::Hodor",
    "dependencies": [],
    "config": {
        "algorithm": "token_bucket",
        "time_unit": 60,
        "capacity": 1000,
        "ip_capacity": 60,
        "user_capacity": 0,
        "use_real_ip_resolver": false,
        "multi_threads": true,
        "rejection_message": "Too Many Requests",
        "limiter_expire_time": 600,
        "urls": ["^/.*"],
        "sub_limits": [
            {
                "urls": ["^/oauth2/login"],
                "capacity": 5000,
                "ip_capacity": 5,
                "user_capacity": 5
            },
            {
                "urls": ["^/oauth2/token"],
                "capacity": 10000,
                "ip_capacity": 10,
                "user_capacity": 10
            },
            {
                "urls": ["^/api/register"],
                "capacity": 5000,
                "ip_capacity": 5,
                "user_capacity": 5
            }
        ],
        "trust_ips": [
            "127.0.0.1",
            "::1",
            "172.16.0.0/12",
            "172.17.0.0/12",
            "192.168.0.0/16"
        ]
    }
}
```

### 3.2 配置说明

| 参数 | 值 | 说明 |
|------|-----|------|
| `algorithm` | `token_bucket` | 令牌桶算法，平滑请求分布 |
| `time_unit` | `60` | 时间窗口：60秒 |
| `capacity` | `1000` | 全局默认限制（所有路径） |
| `ip_capacity` | `60` | 每IP默认限制 |
| `user_capacity` | `0` | 每用户默认限制（0=无限制） |
| `urls` | `["^/.*"]` | 匹配所有URL的正则表达式 |
| `multi_threads` | `true` | 启用线程安全（SafeRateLimiter） |
| `limiter_expire_time` | `600` | 限流器最小过期时间（秒） |

### 3.3 Sub-limits策略

**策略优先级**: 从特定到通用，先匹配者生效

| URL模式 | 全局限制 | 每IP | 每用户 | 说明 |
|---------|---------|------|--------|------|
| `^/oauth2/login` | 5000 | 5 | 5 | 登录接口 |
| `^/oauth2/token` | 10000 | 10 | 10 | Token获取 |
| `^/api/register` | 5000 | 5 | 5 | 用户注册 |
| `^/.*`（默认） | 1000 | 60 | 0 | 其他所有接口 |

### 3.4 白名单配置

**白名单IP范围**:

| IP/CIDR | 说明 | 场景 |
|---------|------|------|
| `127.0.0.1` | IPv4本地回环 | 本机直接访问 |
| `::1` | IPv6本地回环 | IPv6本机访问 |
| `172.16.0.0/12` | Docker Bridge网络 | Docker容器间通信 |
| `172.17.0.0/12` | Docker自定义网络 | Docker Compose环境 |
| `192.168.0.0/16` | 私有网络 | 本地局域网/开发环境 |

**白名单工作原理**:
- Hodor在检查所有限制**之前**，首先调用`RealIpResolver::matchCidr(ip, trustCIDRs_)`
- 如果IP匹配任意`trust_ips`条目，**立即返回true**，跳过所有速率限制检查
- 完全在Hodor内部实现，无需额外代码

---

## 4. 用户识别回调设计

### 4.1 回调实现位置

**文件**: `main.cc`
**时机**: 在`app().run()`之前

### 4.2 代码实现

```cpp
#include <drogon/plugins/Hodor.h>
#include <drogon/utils/Utilities.h>

// 在main函数中，app().run()之前
auto hodor = app().getPlugin<drogon::plugin::Hodor>();

hodor->setUserIdGetter([](const HttpRequestPtr &req) -> std::optional<std::string> {
    std::string authHeader = req->getHeader("Authorization");

    // 1. 检查Bearer Token（适用于/oauth2/userinfo）
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        try {
            auto userIdOpt = req->attributes()->get<std::string>("user_id");
            if (userIdOpt) {
                return userIdOpt;
            }
        } catch (...) {
            // Token无效，继续检查Basic Auth
        }
    }

    // 2. 检查Basic Auth（适用于/oauth2/token）
    if (!authHeader.empty() && authHeader.find("Basic ") == 0) {
        std::string basicAuth = authHeader.substr(6);
        try {
            auto decoded = drogon::utils::base64Decode(basicAuth);
            size_t colonPos = decoded.find(':');
            if (colonPos != std::string::npos) {
                return "client:" + decoded.substr(0, colonPos);
            }
        } catch (...) {
            // 解码失败
        }
    }

    // 3. 未找到有效用户标识，返回nullopt（仅应用IP限制）
    return std::nullopt;
});
```

### 4.3 用户ID格式约定

| 请求类型 | 用户ID格式 | 示例 | 说明 |
|---------|-----------|------|------|
| 客户端认证 | `client:{client_id}` | `client:vue-client` | OAuth2客户端ID |
| 用户认证 | `{user_id}` | `user123@example.com` | 从JWT解析的用户标识 |
| 未认证 | `std::nullopt` | - | 不应用用户限制 |

### 4.4 错误处理

回调中使用try-catch保护：
- 捕获所有异常
- 返回`std::nullopt`（降级为仅IP限制）
- 记录错误日志
- 不影响正常请求处理

---

## 5. 迁移步骤

### 5.1 步骤1：配置文件修改

**编辑**: `config.json`
**操作**: 在`plugins`数组中添加Hodor配置（参见第3.1节）

### 5.2 步骤2：代码修改

**修改1**: `OAuth2Controller.h`

移除`"RateLimiterFilter"`引用：

```cpp
METHOD_LIST_BEGIN
// 移除 "RateLimiterFilter" 参数
ADD_METHOD_TO(OAuth2Controller::token, "/oauth2/token", Post);
ADD_METHOD_TO(OAuth2Controller::login, "/oauth2/login", Post);
ADD_METHOD_TO(OAuth2Controller::registerUser, "/api/register", Post);
METHOD_LIST_END
```

**修改2**: `main.cc`

添加用户ID获取回调（参见第4.2节）

### 5.3 步骤3：删除旧代码并检查引用

**删除文件**:
- `OAuth2Backend/filters/RateLimiterFilter.cc`
- `OAuth2Backend/filters/RateLimiterFilter.h`

**全局搜索检查**:

Windows PowerShell:
```powershell
Select-String -Path "OAuth2Backend\**\*.cc","OAuth2Backend\**\*.h" -Pattern "RateLimiterFilter"
```

Bash:
```bash
grep -r "RateLimiterFilter" --include="*.cc" --include="*.h" .
```

**需要检查的位置**:
- 文档文件: `docs/*.md`
- 测试文件: `test/RateLimiterTest.cc`
- CMakeLists.txt（如果有显式引用）
- 配置文件

**特别处理**: `test/RateLimiterTest.cc`
- 重写为测试Hodor功能，或
- 删除旧的RateLimiterFilter测试

### 5.4 步骤4：重新编译

**Windows**:
```cmd
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Backend
scripts\build.bat
```

**Linux**:
```bash
cd OAuth2Backend
chmod +x scripts/build.sh
./scripts/build.sh
```

**build.bat功能**:
- 自动停止运行中的OAuth2Server.exe
- 清理并重新创建build目录
- 使用Conan安装依赖
- 配置CMake（支持C++20）
- 编译项目（支持并行编译）
- 自动复制config.json到输出目录

### 5.5 步骤5：基本功能验证

1. 启动服务，检查Hodor插件加载日志
2. 测试OAuth2基本流程是否正常
3. 测试速率限制是否生效
4. 验证白名单IP不受限制

### 5.6 步骤6：完整测试套件执行

**单元测试**:
```bash
cd OAuth2Backend/build/test/Release
ctest -V
```

**集成测试**（使用docker-integration-test技能）:
```bash
cd OAuth2Backend
# 使用Docker Compose测试完整OAuth2流程
```

**手动测试关键场景**:
- [ ] `/oauth2/login` - 5次/分钟限制
- [ ] `/oauth2/token` - 10次/分钟限制
- [ ] `/api/register` - 5次/分钟限制
- [ ] 本机IP（白名单）- 无限制
- [ ] 每用户限制（已认证请求）
- [ ] 全局限制（压力测试）

---

## 6. 错误处理

### 6.1 Hodor错误处理机制

**速率限制触发（正常业务逻辑）**:
- 状态码: `429 Too Many Requests`
- Content-Type: `text/plain`
- Body: `"Too Many Requests"`（可配置）
- Connection: `close`

**配置错误处理（启动时）**:

| 配置错误 | Hodor处理 | 日志级别 | 影响 |
|---------|-----------|---------|------|
| `sub_limits.urls`非数组 | 跳过该子限制 | ERROR | 该子限制不生效 |
| `sub_limits`所有capacity为0 | 跳过该子限制 | ERROR | 该子限制不生效 |
| `trust_ips`格式错误 | 抛出异常 | FATAL | 程序启动失败 |
| `algorithm`无效值 | 默认使用token_bucket | WARN | 使用默认算法 |

### 6.2 用户ID获取错误处理

在`setUserIdGetter`回调中使用try-catch:
- 捕获异常返回`std::nullopt`（降级为仅IP限制）
- 不影响正常请求处理

### 6.3 Redis故障处理

**当前RateLimiterFilter**:
- 依赖Redis
- Redis故障时Fail open（无保护）

**Hodor**:
- 使用内存CacheMap，**不依赖Redis**
- 消除Redis单点故障
- 重启服务会丢失限流状态（可接受trade-off）

**对比**:

| 方面 | RateLimiterFilter | Hodor |
|------|-------------------|-------|
| 存储后端 | Redis | 内存CacheMap |
| Redis故障 | Fail open（无保护） | 无影响 [PASS] |
| 重启后状态 | 保留 | 丢失 [WARNING]️ |
| 跨进程共享 | 支持 | 不支持 |

### 6.4 日志记录

**可选的自定义日志**:
```cpp
hodor->setRejectResponseFactory([](const HttpRequestPtr &req) {
    LOG_WARN << "Rate limited request: "
             << "IP=" << req->peerAddr().toIp()
             << ", Path=" << req->path()
             << ", Method=" << req->method().string();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k429TooManyRequests);
    resp->setBody("Too Many Requests");
    return resp;
});
```

---

## 7. 测试策略

### 7.1 单元测试

**文件**: `test/RateLimiterTest.cc`

**需要重写的测试用例**:
```cpp
TEST(RateLimiterTest, WhitelistedIpsBypassLimit)
TEST(RateLimiterTest, PerIpLimitWorks)
TEST(RateLimiterTest, PerUserLimitWorks)
TEST(RateLimiterTest, GlobalLimitWorks)
TEST(RateLimiterTest, TokenBucketAlgorithm)
TEST(RateLimiterTest, SubLimitsPriority)
TEST(RateLimiterTest, LimitExpiration)
```

### 7.2 手动测试场景

**场景1：IP限制验证**

```bash
# 测试/oauth2/login (5次/分钟)
for i in {1..7}; do
  curl -v -X POST http://localhost:5555/oauth2/login \
    -H "Content-Type: application/json" \
    -d '{"email":"test@example.com","password":"wrong"}' \
    2>&1 | grep -E "< HTTP|Too Many"
done

# 预期：前5次成功，第6-7次返回429
```

**场景2：白名单验证**

```bash
# 从本机测试（应该不受限制）
for i in {1..100}; do
  curl -s -X POST http://127.0.0.1:5555/oauth2/login \
    -H "Content-Type: application/json" \
    -d '{"email":"test@example.com","password":"wrong"}' \
    -w "%{http_code}\n" | grep -c "429"
done

# 预期：无429响应
```

**场景3：每用户限制验证**

```bash
# 用户A（client:vue-client）
for i in {1..12}; do
  curl -X POST http://localhost:5555/oauth2/token \
    -u "vue-client:123456" \
    -d "grant_type=client_credentials"
done

# 用户B（client:other-client）
for i in {1..12}; do
  curl -X POST http://localhost:5555/oauth2/token \
    -u "other-client:secret" \
    -d "grant_type=client_credentials"
done

# 预期：每个用户独立计数，前10次成功，第11次429
```

**场景4：全局限制验证**

```bash
# 并发发送大量请求
for i in {1..20}; do
  curl -s http://localhost:5555/oauth2/token \
    -u "client$i:secret" \
    -d "grant_type=client_credentials" &
done
wait

# 预期：总成功请求数 ≤ 全局限制（10000）
```

### 7.3 性能测试

**对比测试（迁移前后）**:

```bash
# 使用ab工具进行压力测试
ab -n 10000 -c 100 \
  -H "Content-Type: application/json" \
  -p login.json \
  http://localhost:5555/oauth2/login
```

**关键指标**:
- Requests per second
- Time per request
- 失败率（429响应）

**预期结果**: Hodor性能 ≥ RateLimiterFilter

### 7.4 测试检查清单

| 测试类型 | 测试内容 | 预期结果 | 状态 |
|---------|---------|---------|------|
| 编译 | 项目编译成功 | 无错误和警告 | ⬜ |
| 单元测试 | 所有单元测试通过 | 100%通过 | ⬜ |
| 集成测试 | OAuth2完整流程 | 正常工作 | ⬜ |
| 速率限制 | 超过限制返回429 | 正确拦截 | ⬜ |
| 白名单 | 本机IP不受限制 | 正常访问 | ⬜ |
| 用户限制 | 每用户限制生效 | 按用户限制 | ⬜ |
| 全局限制 | 高并发时全局限制 | 保护服务 | ⬜ |
| 性能 | 响应时间无劣化 | 性能持平或更好 | ⬜ |
| 回归 | 现有功能 | 完整测试套件通过 | ⬜ |

---

## 8. 回滚计划和应急措施

### 8.1 回滚触发条件

| 问题类型 | 触发条件 | 响应时间 |
|---------|---------|---------|
| 严重错误 | 服务无法启动、核心功能崩溃 | 立即回滚（5分钟内） |
| 功能回归 | OAuth2流程中断、认证失败 | 30分钟内评估是否回滚 |
| 性能劣化 | 响应时间增加50%+、吞吐量下降30%+ | 1小时内评估是否回滚 |
| 速率限制失效 | 限制不生效、被绕过 | 立即回滚（安全风险） |
| 误杀正常请求 | 白名单失效、合法请求被拒 | 立即回滚（业务影响） |

### 8.2 回滚方案

**方案A：Git回滚（推荐，最快）**

```bash
# 1. 停止服务
taskkill /F /IM OAuth2Server.exe

# 2. 回滚到迁移前的commit
cd d:\work\development\Repos\backend\drogon-plugin\authforge
git checkout <migration-commit-hash>~

# 3. 重新编译
cd OAuth2Backend
scripts\build.bat

# 4. 启动服务
cd build\Release
start OAuth2Server.exe
```

**优点**: 最快（5-10分钟），完全恢复到原状态
**缺点**: 丢失迁移后的所有更改

**方案B：配置回滚（快速临时方案）**

```json
// config.json - 临时禁用所有限制
{
    "plugins": [
        {
            "name": "drogon::plugin::Hodor",
            "config": {
                "capacity": 0,
                "ip_capacity": 0,
                "user_capacity": 0,
                "sub_limits": []
            }
        }
    ]
}
```

同时恢复`OAuth2Controller.h`的Filter引用和旧的RateLimiterFilter文件。

**优点**: 快速（2-5分钟），无需重新编译
**缺点**: 仍需最终恢复到RateLimiterFilter

### 8.3 常见问题应急处理

**问题1：Hodor插件加载失败**

```
错误日志：[ERROR] Failed to load plugin: drogon::plugin::Hodor
```

**临时解决方案**: 从config.json的plugins数组移除Hodor，恢复RateLimiterFilter

**问题2：白名单不生效，本机无法访问**

**临时解决方案**:
```json
{
    "config": {
        "ip_capacity": 0,
        "user_capacity": 0,
        "capacity": 0
    }
}
```

**问题3：性能下降严重**

**排查步骤**:
1. 检查CacheMap内存使用
2. 调整`limiter_expire_time`参数
3. 减少sub_limits数量
4. 降级为固定窗口算法

**临时配置调整**:
```json
{
    "config": {
        "algorithm": "fixed_window",
        "limiter_expire_time": 300,
        "multi_threads": false
    }
}
```

### 8.4 预案验证

在正式迁移前，建议在测试环境验证回滚流程：

1. 测试环境部署Hodor
2. 模拟各种故障场景
3. 验证回滚流程是否顺畅
4. 记录回滚时间

**目标**: 确保回滚时间 < 10分钟

---

## 9. 关键差异对比

### 9.1 功能对比

| 特性 | RateLimiterFilter | Hodor |
|------|-------------------|-------|
| **触发时机** | Filter级别 | Global Advice（更早） |
| **算法** | 固定窗口 | Token Bucket |
| **限制层级** | 仅IP | IP + 用户 + 全局 |
| **URL匹配** | 代码if-else | 正则表达式配置 |
| **白名单** | 无 | 支持（trust_ips） |
| **状态存储** | Redis INCR | 内存CacheMap |
| **过期策略** | Redis EXPIRE | CacheMap自动清理 |
| **扩展性** | 需要修改代码 | 修改JSON配置 |
| **Redis依赖** | 是 | 否 |
| **重启后状态** | 保留 | 丢失 |

### 9.2 性能对比

| 指标 | RateLimiterFilter | Hodor | 预期 |
|------|-------------------|-------|------|
| **平均响应时间** | ~50ms | ~40-50ms | 持平或更优 |
| **P99响应时间** | ~100ms | ~80-100ms | 持平或更优 |
| **内存使用** | 低（Redis） | 中等（CacheMap） | 可接受增加 |
| **CPU使用** | 中等 | 低（无网络IO） | 更优 |
| **吞吐量** | 受Redis限制 | 受算法限制 | 更高 |

### 9.3 运维对比

| 方面 | RateLimiterFilter | Hodor |
|------|-------------------|-------|
| **配置修改** | 修改代码→编译→部署 | 修改JSON→重启服务 |
| **限制调整** | 需要重新编译 | 无需重新编译 |
| **故障排查** | Redis日志 + 应用日志 | 应用日志 |
| **监控指标** | Redis INCR统计 | Prometheus集成 |
| **扩展性** | 受Redis集群限制 | 单机内存限制 |

---

## 10. 风险评估

### 10.1 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| Hodor功能缺陷 | 低 | 高 | 充分测试，准备回滚方案 |
| 性能下降 | 低 | 中 | 性能测试对比，优化配置 |
| 白名单失效 | 中 | 高 | 测试环境验证，添加日志 |
| 内存泄漏 | 低 | 中 | 长时间运行测试，监控内存 |
| 限流失效 | 低 | 高 | 集成测试验证，监控告警 |

### 10.2 业务风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 正常请求被拒 | 中 | 高 | 白名单配置，逐步推广 |
| 重启状态丢失 | 高 | 低 | 监控重启频率，降低影响 |
| 配置错误 | 中 | 中 | 配置验证，测试环境先行 |

### 10.3 运维风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 团队不熟悉 | 中 | 中 | 文档完善，培训 |
| 调试困难 | 低 | 中 | 日志完善，监控指标 |
| 回滚复杂度 | 低 | 高 | 自动化回滚脚本 |

---

## 11. 后续改进建议

### 11.1 短期改进（1-2周）

1. **Prometheus集成**
   - 添加Hodor拒绝次数指标
   - 按IP、路径、用户维度打标签
   - 设置告警阈值

2. **日志优化**
   - 结构化日志（JSON格式）
   - 记录被限制请求的详细信息
   - 集成ELK/Loki进行日志分析

3. **监控仪表板**
   - Grafana仪表板展示速率限制状态
   - 实时显示429响应率
   - Top受限IP和用户统计

### 11.2 中期改进（1-2月）

1. **动态配置**
   - 支持运行时调整限制参数
   - 无需重启服务
   - 通过管理接口配置

2. **智能限流**
   - 根据系统负载动态调整限制
   - 熔断机制
   - 优先级队列

3. **多层防护**
   - Nginx层限流（粗粒度）
   - Hodor层限流（中粒度）
   - 应用层限流（细粒度）

### 11.3 长期改进（3-6月）

1. **分布式限流**
   - 跨实例共享限流状态
   - Redis Cluster支持
   - 一致性哈希

2. **机器学习**
   - 异常流量检测
   - 自动调整限制阈值
   - DDoS检测和防护

3. **可视化配置**
   - Web UI配置界面
   - 实时流量监控
   - 配置版本管理

---

## 12. 审批和签署

| 角色 | 姓名 | 审批状态 | 日期 |
|------|------|---------|------|
| 架构师 | | ⬜ 待审批 | |
| 技术负责人 | | ⬜ 待审批 | |
| 运维负责人 | | ⬜ 待审批 | |
| 安全负责人 | | ⬜ 待审批 | |

---

## 13. 附录

### 13.1 参考资料

- [Drogon官方文档 - Hodor插件](https://github.com/drogonframework/drogon/tree/master/plugins)
- [OAuth2 2.0 RFC 6749](https://tools.ietf.org/html/rfc6749)
- [Token Bucket算法](https://en.wikipedia.org/wiki/Token_bucket)
- [Rate Limiting最佳实践](https://cloud.google.com/architecture/rate-limiting-strategies-techniques)

### 13.2 相关文档

- `docs/architecture_overview.md` - 系统架构总览
- `docs/security_hardening.md` - 安全加固指南
- `docs/configuration_guide.md` - 配置指南
- `docs/testing_guide.md` - 测试指南

### 13.3 术语表

| 术语 | 定义 |
|------|------|
| Token Bucket | 令牌桶算法，一种速率限制算法 |
| Fixed Window | 固定窗口算法，一种简单的速率限制算法 |
| CacheMap | Drogon的内存缓存映射，自动过期 |
| Pre-handling Advice | Drogon的AOP机制，在请求处理前执行 |
| CIDR | 无类域间路由，用于IP范围表示 |
| Whitelist | 白名单，不受限制的IP列表 |

---

**文档版本**: 1.0
**最后更新**: 2026-04-13
**状态**: 待审批
