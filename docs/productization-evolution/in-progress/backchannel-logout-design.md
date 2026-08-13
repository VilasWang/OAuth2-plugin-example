# OIDC Back-Channel Logout 1.0 实现设计

> **任务**: next-phase-implementation-plan.md §三 B1（全栈纵切，范围 A）
> **状态**: 进行中
> **日期**: 2026-08-13
> **规范**: [OIDC Back-Channel Logout 1.0](https://openid.net/specs/openid-connect-backchannel-1_0.html)
> **上游**: [iam-architecture-audit.md](../iam-architecture-audit.md) §四 P0

---

## 一、目标

用户登出时，向每个【拥有该用户活跃会话且注册了 `backchannel_logout_uri`】的 RP（客户端）POST 一个由 OP 签名的 `logout_token`；并提供从 admin API + 前端 UI 配置该 URI 的能力。

替换现状的桩：`LoggingBackchannelLogoutNotifier`（`apps/server/src/bootstrap/IdentityAssembly.cc`）仅 `LOG_DEBUG`。

---

## 二、现状（代码验证，file:line）

| 组件 | 位置 | 状态 |
|------|------|------|
| 端口 `IBackchannelLogoutNotifier::notify(userId,cb)` | `libs/identity/include/authforge/identity/IBackchannelLogoutNotifier.h` | async 单回调，可用 |
| 触发链 | `SessionController::logout` → revokeAccessToken → `sessionManager->logout(userId)` → `notifier_->notify` | 已就绪 |
| 桩实现 | `LoggingBackchannelLogoutNotifier`（IdentityAssembly.cc 匿名命名空间） | 仅 LOG，需替换 |
| 死 stub | `SessionController.cc` `sendBackchannelLogoutNotifications` | 需删除 |
| 接线点 | `IdentityAssembly.cc` `static auto notifier = ...` | 替换处 |
| DB 字段 | `oauth2_clients.backchannel_logout_uri` / `backchannel_logout_session_required`（迁移 `V015`） | 已存在 |
| ORM 访问器 | `Oauth2Clients::getValueOfBackchannelLogoutUri()` / `setBackchannelLogoutUri()` / `setBackchannelLogoutUriToNull()` | 已生成，**未被应用代码使用** |
| JWT 签名 | `JwkManager::signJwt(Json::Value)`（RS256+kid） | 可复用 |
| issuer | `customConfig["metadata"]["issuer"]`，accessor `OAuth2Plugin::getIssuer()` | 可复用 |
| HTTP 端口 | `IOAuthHttpClient::postForm(url,params,cb)` | 可复用，但被 `#ifdef WITH_SOCIAL` 门控 → **需解门控** |
| admin 写路径 | `ClientManagementService::createClient/updateClient/getClient` | **不读写 backchannel 字段** → 需扩展 |
| 前端 | `ApplicationDetailPage.vue` / `ApplicationsPage.vue` | **无 backchannel 字段** → 需扩展 |

### 关键发现：subject 映射

`oauth2_access_tokens.user_id` 存的是 **public subject（`public_sub`）**（`TokenService.cc:172` `authCode.userId = subject`；`:343` `idTokenClaims["sub"] = authCode.userId`），与 `notify(userId)` 收到的 `userId`（请求 attrs 里的 `userId` = public_sub）同值。

**结论：无需 subject 映射** —— 可直接按 `user_id` 查询 token，并以该 `userId` 作为 `logout_token` 的 `sub`（与 id_token 的 `sub` 一致，RP 可正确匹配）。

---

## 三、三个缺口与桥接

1. **无"用户活跃客户端"查询**（`ITokenRepository` 全是 token 值键控）→ 新增 Mapper 查询：
   - `oauth2_access_tokens WHERE user_id=? AND revoked=false AND expires_at>now()` → C++ 收集 distinct `client_id`
   - `oauth2_clients WHERE client_id IN(...)` → 读 backchannel 字段
   - 全程 async callback + Mapper + Criteria（遵循 `.claude/rules/db-operations.md`），每段 Mapper 构造独立 try-catch，失败回调上层 cb。
2. **`OAuth2Client` DTO 不带 backchannel 字段**（`PostgresClientRepository::getClient` 丢弃）→ **绕过 DTO**：通知器/admin service 直接 `Mapper<Oauth2Clients>` 读写 ORM 字段。不改 DTO、不动 `IClientRepository` 接口。
3. **HTTP 端口 `#ifdef WITH_SOCIAL` 门控** → **解门控** `IOAuthHttpClient` / `DrogonOAuthHttpClient` / `FakeOAuthHttpClient`（三者仅依赖 `drogon::HttpClient`，非 social 专有）。通知器复用 `postForm(uri, {{"logout_token",jwt}}, cb)`；`oauthHttpClient` 在 `IdentityAssembly` 提到 `#ifdef` 外、与 social 共用单例。

---

## 四、`logout_token` 构造（OIDC Back-Channel Logout 1.0 §2.4）

claims：

| claim | 值 | 说明 |
|-------|-----|------|
| `iss` | OP issuer | 来自 `OAuth2Plugin::getIssuer()` |
| `sub` | `userId`（public_sub） | 与 id_token `sub` 一致 |
| `aud` | 该 RP 的 `client_id` | 每个 RP 一个独立 token |
| `iat` | now（秒） | |
| `exp` | `iat + 120` | 短生命周期（规范建议 ≤120s） |
| `jti` | 唯一 id（UUID） | 防重放；每次调用唯一 |
| `events` | `{"http://schemas.openid.net/event/backchannel-logout": {}}` | **必需**，固定 URI |
| `nonce` | — | **禁止**出现 |

`sub`/`sid` 至少需一：本 OP 无 `sid` 概念，统一用 `sub`。用 `JwkManager::signJwt(claims)` 签名（RS256，kid 与 id_token 同密钥）。

提取纯函数 `buildLogoutTokenClaims(iss, sub, aud, nowSecs, ttlSecs, jti)` → `Json::Value`，供单测；`generateJti()` 生成唯一 id。

---

## 五、时序与生命周期

```
notify(userId, cb)
  └─[查询1] oauth2_access_tokens (user_id=userId, revoked=false, expires_at>now) → distinct clientIds
       └─[查询2] oauth2_clients (client_id IN clientIds) → 带 backchannel_uri 的客户端
            └─[dispatch] 对每个有非空 URI 的 RP：
                 ├─ buildLogoutTokenClaims + signJwt
                 ├─ postForm(uri, {logout_token=jwt})  ← fire-and-forget
                 └─ POST 完成 → 按 status 审计 success/failure
            └─ 回调 cb（派发完成后；不等 RP 往返 → 登出响应不被阻塞）
```

- 通知器继承 `enable_shared_from_this`、捕获 `self`（**服务层禁 `[this]`**）。
- 每段 Mapper 构造独立 `try/catch`，catch 内 `(*sharedCb)()` 回调失败（禁止仅 LOG 后 return）。
- POST 为 fire-and-forget：登出 HTTP 响应在派发后立即返回；每个 POST 完成时异步审计。
- 通知器为进程级 static（接线点 `static auto`），late POST 回调引用 `self`/shared 状态安全。

---

## 六、代码位置

| 新增 | 路径 |
|------|------|
| 纯 claim 构造 | `libs/oauth2/include/authforge/oauth2/protocol/LogoutToken.h` + `libs/oauth2/src/protocol/LogoutToken.cc` |
| 真实通知器 | `libs/drogon/include/authforge/drogon/adapters/BackchannelLogoutNotifier.h` + `libs/drogon/src/adapters/BackchannelLogoutNotifier.cc` |
| claim 单测 | `libs/oauth2/test/LogoutTokenTest.cc` |
| dispatch 单测 | `libs/drogon/test/BackchannelLogoutNotifierTest.cc` |
| 端到端集成 | `tests/integration/controllers/BackchannelLogoutTest.cc` |

| 修改 | 改动 |
|------|------|
| `IOAuthHttpClient.h` / `DrogonOAuthHttpClient.{h,cc}` / `FakeOAuthHttpClient.h` | 解门控 `#ifdef WITH_SOCIAL` |
| `libs/{identity,drogon}/CMakeLists.txt` | 新增源文件；http client 无条件编译 |
| `IdentityAssembly.cc` | 替换桩为真实通知器；删 `LoggingBackchannelLogoutNotifier` |
| `SessionController.cc` | 删死 stub `sendBackchannelLogoutNotifications` |
| `ClientManagementService.cc` | create/update/get/list 读写返回 backchannel 字段 + https 校验 |
| `RuleSet.{h,cc}` | `validateBackchannelLogoutUri` |
| `DiscoveryController.cc` | `backchannel_logout_supported:true`、`backchannel_logout_session_supported:false` |
| `openapi.yaml` | create/update client requestBody 字段 |
| `ApplicationDetailPage.vue` / `ApplicationsPage.vue` | 表单 backchannel 字段 |

---

## 七、校验规则

- `backchannel_logout_uri` 必须 **https**（OIDC §2.3）。新增 `RuleSet::validateBackchannelLogoutUri`，复用 redirect_uri 的 loopback/dev override 策略（`auth.allow_http_redirect_uri` 同款开关）。
- create：可选字段，缺省不写（NULL）。
- update：传空串 → `setBackchannelLogoutUriToNull()`（清除）；传值 → 校验 https 后写入。
- 非 https → 400 `VALIDATION_FORMAT_ERROR`。

---

## 八、验收标准（摘要，详见实施计划）

- **logout_token 正确性（单测 U）**：claims 集合精确、events 固定 URI、无 nonce、exp=iat+120、jti 唯一。
- **dispatch（单测 D）**：N 客户端→N POST 且 aud 匹配；空 URI 跳过；transportFailure/非200 审计 failure 不崩溃；签名可经 `getJwks()` 公钥验签。
- **端到端（集成 I）**：配置 URI 的客户端在用户登出时收到 POST；扇出/发起方排除/无活跃 token/空 URI 跳过/session_required/审计/并发/Discovery 字段/回归。
- **工程契约 C**：DB 规则、默认配置零行为变更（安全上线）、构建 clean、§2.3/§2.4 合规。

---

## 九、明确排除（follow-up）

- 登出时跨客户端吊销该用户全部 token（single-logout 完整性）。
- `sid` 签发/支持（`backchannel_logout_session_required=true` 客户端的已知缺口，文档注明）。
- 可配置 HTTP 超时/重试。
- RP 响应 body 校验（仅按 status 判 success/failure）。
- RFC 7591 注册端点加 backchannel 字段（admin 路径优先）。
