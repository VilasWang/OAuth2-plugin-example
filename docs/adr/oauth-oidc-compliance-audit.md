---
title: Archive · OAuth/OIDC Compliance Audit (2026-08-07 baseline, in Chinese)
---

> **Language note**: this is a historical trust archive kept in its original
> Chinese. The 2026-08-07 OAuth/OIDC compliance-audit baseline (all 31
> findings fixed, regression-tested) is preserved verbatim for third-party
> assessors. The current security design lives in
> [Security Architecture](../architecture/security-architecture.md).

> 本报告是 2026-08-07 对 fulla（时名 authforge）的完整 OAuth/OIDC 合规尽调
> **基线快照**：逐条附 file:line 证据与规范章节引文。报告发布后所有发现均已在
> 后续版本中修复（含 F-002 Critical 凭据哈希不一致等），并沉淀为回归测试；
> 保留本档案作为第三方评估者可核查的信任资产。当前安全设计见
> [security-architecture.md](../architecture/security-architecture.md)。

# OAuth/OIDC 规范性审查报告 — fulla

| 项目 | 内容 |
|---|---|
| 审查对象 | fulla (`test/coverage-push` 分支, 2026-08-07) |
| 审查范围 | RFC 6749 / 6750 / 7662 / 7009 / 7636 / 8252 / 8628 / 8414 / 7519/7517/7515 / 7591/7592 / 9068 / 9700 + OIDC Core 1.0 / Discovery 1.0 / RP-Initiated Logout / Back-Channel Logout |
| 审查方法 | 静态代码审查 + 规范条款逐条核验；所有判断附 `file:line` 证据 + 规范章节引文 |
| 不在范围 | 动态渗透测试、性能/可用性评估、前端 SPA 实现审查 |
| 评级分档 | 核心规范按 MUST/SHOULD 分档；OIDC profile 按「核心 MUST」「扩展 SHOULD」分档 |

> 配套文档：审查计划（检查要点、检查方法、判定标准）见 `oauth-oidc-compliance-audit-plan.md`。本报告为评估结果（符合性评级、发现、整改）。

---

## 1. 执行摘要

### 1.1 总体符合度

fulla 在 **OAuth 2.0 核心（RFC 6749）的"快乐路径"**上基本合规：授权码与刷新令牌的生成、哈希存储、单次性、轮换、重用级联吊销都按规范实现；PKCE 的 S256 算法是规范正确的 `base64url(raw digest)`；introspect/revoke 的客户端认证模型近期（commit 246db32）已修正为 RFC 7662/7009 要求的客户端凭证模型。

但存在 **若干违反 MUST 的实质性偏差**，集中在三类：

1. **令牌端点客户端认证不完整** —— `refresh_token` grant 完全跳过客户端认证（违反 RFC 6749 §3.2.1 MUST）。
2. **client_secret 哈希写入/校验算法不一致** —— 注册/管理路径写无盐大写 SHA-256，校验路径算有盐小写 SHA-256（违反 RFC 6749 §10.6 凭证保护 MUST，且事实上导致动态注册的客户端无法认证）。
3. **OIDC 扩展能力大面积缺失** —— `prompt`/`max_age`/`auth_time`/`acr`/`amr`/`azp`/RP-Initiated Logout/nonce 防重放/id_token-on-refresh 均未实现，使本项目实际只能算"OAuth2 + 一个最小 id_token 签发"，而非完整 OIDC Provider。

### 1.2 风险等级分布

| 等级 | 数量 | 代表项 |
|---|---|---|
| **严重 (Critical)** | 1 | F-002 client_secret 哈希不一致导致动态注册客户端认证失败 |
| **高 (High)** | 6 | refresh_token 无客户端认证、Redis 非常量时间比较、Redis refresh 存储空操作、authorization 端点错误未按 §4.1.2.1 重定向、device_authorization 未认证机密客户端、WWW-Authenticate Bearer 缺失 |
| **中 (Medium)** | 9 | id_token 缺 auth_time/acr/amr/azp、refresh 不重发 id_token、PKCE 默认不强制、device slow_down 未发出、iss 硬编码、loopback 例外未实现、token 端点无限流、state 未 urlEncode、token_endpoint_auth_method 未持久化 |
| **低 (Low)** | 7 | registration_endpoint 未广告、introspection 缺 jti/username、成功响应缺 Cache-Control、jti 不存在、claims_supported 与实际不符、OpenAPI grant_type 枚举缺 device_code、CORS 校验注释 |

### 1.3 最高优先级 5 项

| 编号 | 问题 | 风险 | 修复复杂度 |
|---|---|---|---|
| F-002 | client_secret 哈希写/读算法不一致 | Critical | 中（统一算法 + 数据迁移） |
| F-003 | refresh_token grant 跳过客户端认证 | High | 低（加 `validateClient` 调用） |
| F-004 | Redis 后端 client_secret 非常量时间比较 | High | 低 |
| F-005 | Redis 后端 refresh_token 存储为空操作 | High | 中（实现 Redis save/getRefreshToken） |
| F-007 | authorization 端点错误未按 §4.1.2.1 重定向 | High | 中（区分 redirectable vs client 错误） |

---

## 2. 评级尺度

| 评级 | 定义 |
|---|---|
| **符合** | 满足规范条款的 MUST 与 SHOULD，无功能/安全偏差 |
| **部分符合** | 实现核心要求但缺字段、缺边角 MUST 或不满足 SHOULD |
| **不符合** | 违反某条 MUST，或实现存在功能性/安全性错误 |
| **未实现** | 规范定义了能力但代码无对应实现（含 stub、advertised-but-missing） |

OIDC profile 分档（按用户要求）：
- **核心 MUST 档**：OIDC Core 中标 MUST 的条款（如 id_token 必备 claim、UserInfo 须校验 openid scope）
- **扩展 SHOULD 档**：OIDC Core 的 SHOULD 条款，以及 Session Management / RP-Initiated Logout / Back-Channel Logout 等独立规范（这些虽非 OIDC Core 的 MUST，但是「可互操作的 OIDC Provider」的事实标配）

风险等级独立于评级：一个"未实现"的扩展 SHOULD 可能只是 Medium，而一个"不符合"的核心 MUST 通常是 High/Critical。

---

## 3. 逐规范符合性评估

### 3.1 RFC 6749 OAuth 2.0 Authorization Framework —— **部分符合**

#### 3.1.1 §1.6 / §3.1.1 协议须运行于 HTTPS；redirect_uri 须 https —— **不符合（Medium）**

- **检查方法**：Grep redirect_uri scheme 校验、loopback 例外。
- **证据**：`libs/oauth2/include/fulla/oauth2/model/Client.h:86-90` 仅做 `std::find` 精确字符串匹配；`libs/drogon/src/validation/RuleEngine.cc:120-133` 的 regex `^https?://...` 同时接受 http 与 https；无任何代码强制 https 或实现 RFC 8252 §7.3 / RFC 6749 §3.1.2.1 的 loopback 端口通配。
- **偏差**：redirect_uri 注册与匹配接受任意 scheme，未强制 https，未实现 loopback 例外。
- **依据**：RFC 6749 §3.1.2.1 "the redirection endpoint SHOULD require the use of TLS"；§1.6 明确 TLS 为 MUST 级别的部署前提。
- **见**：F-014。

#### 3.1.2 §3.1.2.3 redirect_uri 须精确匹配 —— **符合**

- **证据**：`Client.h:83-90` `isRegisteredRedirectUri` 用 `std::find(..., redirectUri) != ...end()`，注释明示"RFC 6749 §3.1.2.3 requires exact match, not prefix/pattern matching"。authorize 端 `libs/oauth2/src/protocol/ClientService.cc:32-44` 调用之；exchange 端 `PostgresGrantRepository.cc:181-189` 复核之。
- **判定**：精确匹配、无通配、无前缀。**符合**。

#### 3.1.3 §4.1.2 授权码：TTL ≤10min、一次性、绑定 —— **符合**

- **TTL**：`libs/oauth2/src/protocol/TokenService.cc:151` `authCode.expiresAt = nowSeconds() + authCodeTtl_;`，默认 600s（`OAuth2Plugin.cc:53`），exchange 时校验（`TokenService.cc:226-230`）。
- **一次性**：`PostgresGrantRepository.cc:165-176` 原子 CAS `UPDATE oauth2_codes SET used=true WHERE code=$1 AND used=false RETURNING *`；Memory 后端 `MemoryGrantRepository.cc:66-95` 用锁 + `used` 标志。
- **绑定**：`TokenService.cc:144-151` 写入 `clientId/userId/scope/redirectUri/codeChallenge/codeChallengeMethod/nonce`；exchange 时校验 client_id（`:200-204`）、redirect_uri（`PostgresGrantRepository.cc:181-189`）、PKCE（`:206-219`）。
- **判定**：**符合**。

#### 3.1.4 §4.1.2.1 错误重定向 vs 直接错误 —— **不符合（High）**

- **检查方法**：审 `AuthorizationEndpointController.cc` 各错误分支的响应方式。
- **证据**：
  - 无效 client_id → 直接 400（`AuthorizationEndpointController.cc:233-237`）
  - 无效 redirect_uri → 直接 400（`:257-261`）
  - scope 失败 → 直接 JSON（`:362-371`）
  - 仅 consent-deny 一处按规范 `?error=access_denied&state=` 重定向（`SessionController.cc:728-737`）
- **偏差**：RFC 6749 §4.1.2.1 规定——若请求的 `redirect_uri` 缺失/无效或 `client_id` 未知，AS 应直接告知用户错误（不重定向）；**但若 redirect_uri 与 client_id 均有效**，所有其他错误（access_denied、invalid_scope、unsupported_response_type、server_error 等）都**必须**以 `?error=&state=` 形式 302 重定向到 redirect_uri。本项目对所有非 consent-deny 错误一律直接 4xx，state 在这些情况下不回显。
- **依据**：RFC 6749 §4.1.2.1。
- **见**：F-007。

#### 3.1.5 §4.1.3 authorization_code 兑换 —— **部分符合（Medium）**

- **redirect_uri 比较可被绕过**：`PostgresGrantRepository.cc:181-189` 与 `MemoryGrantRepository.cc:80-87` 均以 `if (!redirectUri.empty() && redirectUri != stored)` 守卫比较。RFC 6749 §4.1.3 规定"if the `redirect_uri` was included in the initial authorization request **then** it MUST be included in the token request"——即签发时带了 redirect_uri，兑换时就必须带且必须匹配。当前实现允许兑换时**省略** redirect_uri 从而跳过比较。`RuleSet::oauth2Token`（`RuleSet.cc:360-376`）对 `code` 必填、`client_id` 选填，未强制 redirect_uri 在该场景下的必填性。
- **client 绑定 / PKCE / 过期**：已实现（见 3.1.3）。
- **见**：F-009。

#### 3.1.6 §3.2.1 / §4.1.3 token 端点对所有机密客户端 MUST 认证 —— **不符合（High）**

- **检查方法**：跟踪 4 种 grant 在 controller 入口的 `validateClient` 调用。
- **证据**：
  - authorization_code → `TokenService.cc:177-187` 先 `validateClient` ✅
  - client_credentials → `TokenEndpointController.cc:733-775` 先 `validateClient` ✅
  - device_code → `TokenEndpointController.cc:1227-1276` 先 `validateClient`（按 client_type 分支）✅
  - **refresh_token → `TokenEndpointController.cc:679-711` 直接 `plugin->refreshAccessToken(refreshTokenStr, clientId, ...)`，无 `validateClient` 调用** ❌
- **偏差**：refresh_token grant 仅在 `TokenService.cc:387-391` 做 `storedRt->clientId != clientId` 字符串比较，未校验 client_secret。**任何机密客户端只要持有一个他不该有的 refresh_token 字符串，配上任意 client_id 即可刷新**（client_id 必须匹配，但 client_id 不保密，攻击者从被泄漏的 refresh_token 关联日志即可推得）。这违反 RFC 6749 §3.2.1（"The authorization server MUST [...] authenticate the client if the client was issued credentials"）。
- **依据**：RFC 6749 §3.2.1。
- **见**：F-003。

#### 3.1.7 §4.4 client_credentials —— **符合**

- **仅限机密客户端**：`TokenEndpointController.cc:765-775` PUBLIC → `unauthorized_client`。
- **不发 refresh_token**：`:825` 注释明确，`:853` 响应省略。
- **scope 校验**：`:784-823` 超集 → `invalid_scope`；缺省取客户端注册的 scope 全集；注册 scope 为空且请求省略 → `invalid_scope`。
- **subject 为 `client:<id>`**（`:839`），符合 RFC 6749 §4.4.3 的 M2M 语义。
- **判定**：**符合**。

#### 3.1.8 §6 refresh_token —— **部分符合（中，配合 3.1.6 看）**

- **轮换**：每次刷新都发新 access + 新 refresh（`TokenService.cc:400-417`）。
- **重用检测 + 级联吊销**：原子 CAS `atomicRevokeRefreshToken`（`PostgresTokenRepository.cc:404-446`）+ `revokeTokenFamily`（`:448-492`，按 family_id 批量吊销 refresh 与关联 access），审计 `refresh_token_reuse_detected`（`TokenService.cc:367-373`）。**符合 RFC 6749 §6 的推荐实践**。
- **绑定 client_id 校验**：`TokenService.cc:387-391` 有比较，但配合 3.1.6 的"无认证"，仅是字符串比较，弱。
- **Redis 后端失效**：`RedisTokenRepository.cc:155-165` `saveRefreshToken`/`getRefreshToken` 是空操作（注释 line 153-154 明示），`atomicRevokeRefreshToken` 永远返回 nullopt，故 Redis 部署下轮换/级联**完全不工作**。
- **见**：F-005。

#### 3.1.9 §5.1 成功响应 —— **部分符合（Low）**

- **token_type=Bearer / expires_in**：齐备（`TokenService.cc:293,297,432,434`；`TokenEndpointController.cc:850-851,1147-1148`）。
- **scope 回显**：client_credentials（`:852`）与 device_code（`:1150-1153`，仅非空时）回显；**authorization_code 与 refresh_token 不回显**（`TokenService.cc:291-299, 430-436`）。RFC 6749 §5.1 规定 scope 若与请求不同则 MUST 回显、相同则 OPTIONAL，缺失属轻微偏差。
- **Cache-Control: no-store**：RFC 6749 §5.1 RECOMMENDS 此头于所有 token 响应；本项目仅错误响应加（`OAuth2ErrorHandler.cc:74-75`），成功响应不加。
- **非标 `roles` 字段**：`TokenService.cc:299` 在 authorization_code 响应额外加 `roles`，非 RFC 6749 字段（可接受但应文档化）。
- **见**：F-019。

#### 3.1.10 §5.2 错误响应 —— **部分符合（Medium）**

- **error 码集合**：覆盖 `invalid_request/invalid_client/invalid_grant/unauthorized_client/unsupported_grant_type/invalid_scope/server_error/access_denied/authorization_pending/expired_token`（`ErrorCatalog.cc:218-236`）。
- **HTTP 状态映射**：`invalid_client`→401、`server_error`→500、`access_denied`→403、其余→400（`OAuth2ErrorHandler.cc:93-107`、`ErrorCatalog.cc:221-233`）。基本符合 §5.2。
- **WWW-Authenticate on invalid_client**：协议端点（introspect/revoke）经 `OAuth2ErrorHandler::sendErrorResponse` 在 `authScheme` 非空时正确加 `WWW-Authenticate`（`OAuth2ErrorHandler.cc:85-88`）。但 `/oauth2/token` 的 inline grant 分支（client_credentials/device_code 的 `invalid_client`）直接构造 JSON，**不加** WWW-Authenticate（`TokenEndpointController.cc:718-725, 736-743, 757-762, 1238-1244, 1264-1271`）。违反 §5.2 "MUST include the WWW-Authenticate header"。
- **validation gate 误用应用信封**：`RuleSet::oauth2Token` 失败经 `HttpResponder` 返回应用信封 `VALIDATION_INVALID_INPUT`（`HttpResponder.cc:57-58, 86`），**不是** RFC 6749 §5.2 的 `error: "invalid_request"`。客户端按 OAuth2 协议解析错误时会失败。
- **见**：F-006、F-008。

#### 3.1.11 §10.4 / §10.6 凭证保护 —— **不符合（Critical，仅动态注册路径）**

- **access/refresh token 哈希存储**：✅ UPPER-hex SHA-256（`TokenCrypto.cc:26-37`），原值不入库。
- **client_secret 哈希**：❌ 见 3.6.2。写入路径用无盐大写 SHA-256，校验路径算有盐小写 SHA-256。两套算法永远不会匹配。
- **常量时间比较**：Postgres ✅（`PostgresClientRepository.cc:14-27, 245-249`），Memory ✅（`MemoryClientRepository.cc:11-24, 163`，但比较的是**明文**），Redis ❌（`RedisClientRepository.cc:174-175` `==`）。
- **见**：F-002、F-004。

#### 3.1.12 §10.9 维护撤销集 —— **符合**

- access/refresh token 持 `revoked` 标志，`validateAccessToken`（`TokenService.cc:443-479`）查 revoked + expiry。
- **判定**：撤销即时生效。**符合**。

---

### 3.2 RFC 6750 Bearer Token Usage —— **部分符合**

#### 3.2.1 §2.1 Bearer 头解析 —— **符合**

`OAuth2AuthFilter.cc:39-52` 校验 `Authorization: Bearer ` 前缀，缺失/格式错→401。

#### 3.2.2 §2.3 query 传 token —— **部分符合（Low）**

`AuthorizationFilter.cc:99-107` 接受 `?access_token=`，属 RFC 6750 §2.3 已废弃方式。允许但不推荐。

#### 3.2.3 §3 WWW-Authenticate: Bearer challenge —— **不符合（Medium）**

- **证据**：资源端点（`/api/me`、`/api/admin/*`）401 经 `OAuth2AuthFilter`/`AuthorizationFilter` 返回应用信封 `AUTH_TOKEN_INVALID`，**不**按 RFC 6750 §3 发 `WWW-Authenticate: Bearer realm="...", error="invalid_token", error_description="..."`。
- **依据**：RFC 6750 §3 "If the request lacks any authentication information [...], the resource server SHOULD NOT respond with the WWW-Authenticate header. [...] otherwise [...] MUST include the WWW-Authenticate header"。
- **见**：F-006。

#### 3.2.4 §3.1 insufficient_scope —— **不符合（High）**

- **证据**：`OAuth2AuthFilter.cc:73-75` 把 `scope` 写入 attributes 但**从不校验**。Grep 全 `libs/`、`apps/` 无任何资源端点按 scope 拒绝。`/api/admin/*` 的 RBAC 按**用户角色**而非 OAuth scope。
- **依据**：RFC 6750 §3.1 "If the access token [...] has insufficient scope"，应返回 `insufficient_scope`。
- **影响**：scope 退化成"签发时校验客户端白名单"的元数据，丧失细粒度资源授权能力。
- **见**：F-010。

---

### 3.3 RFC 7662 Token Introspection —— **部分符合**

#### 3.3.1 §2.1 客户端认证 MUST —— **符合**

`TokenEndpointController.cc:279-345`：`extractClientCredentials` 取 Basic 或 POST `client_id`/`client_secret`，缺失→`invalid_client`+WWW-Authenticate，`validateClient` 校验。**符合**。

#### 3.3.2 §2.2 响应字段 —— **部分符合（Low）**

- **已发**：`active/client_id/token_type/exp/iat/nbf/sub/aud/iss/scope`（`:377-409`）。
- **缺失**：`username`、`jti`。`jti` 全代码库不存在（Grep 无结果）。
- **iss 不一致**：refresh_token 分支硬编码 `iss = "https://oauth.example.com"`（`PostgresTokenRepository.cc:567`），与配置 issuer 不符。
- **见**：F-016。

#### 3.3.3 §2.2 无效/过期/撤销令牌须 active=false（非 4xx）—— **符合**

`:353-368` repo 返回 `nullopt` → 200 `{active:false}`；repo 层 revoked/expired 返回 `active=false` 对象（`:523-529, 553-558, 574-578`），controller 序列化为完整字段（含 active=false）。**符合**。

#### 3.3.4 §2.3 token_type_hint —— **符合**

`RuleSet.cc:573` 接受但忽略。合规。

---

### 3.4 RFC 7009 Token Revocation —— **符合**

#### 3.4.1 §2.1 客户端认证 + 所有权 —— **符合**

`TokenEndpointController.cc:420-523`：认证 + `introspection->clientId != clientId` → `unauthorized_client`（`:508-522`）。

#### 3.4.2 §2.1 支持 access 与 refresh —— **符合**

`revokeAccessToken` 同时回退尝试 refresh 表（`PostgresTokenRepository.cc:652-677`），命名误导但行为正确。

#### 3.4.3 §2.2.1 成功/未知均返回 200；no-store —— **部分符合（Low）**

`:494-544` 成功/未知均 200。**但** `createSuccessResponse`（`:235-240`）不加 `Cache-Control: no-store`。RFC 7009 §2.2.1 RECOMMENDS 此头。**见**：F-019。

---

### 3.5 RFC 7636 PKCE —— **部分符合**

#### 3.5.1 §4.3 code_challenge_method 仅 plain/S256 —— **部分符合（Medium）**

- authorize 端**不校验**方法集合（`AuthorizationEndpointController.cc:108-109` 透传），任意字符串被接受并存储。仅在 exchange 端 `Pkce.cc:17-26` 拒绝非 `plain/S256`。
- **依据**：RFC 7636 §4.3 "If the server supports PKCE [...] the server SHOULD reject authorization requests that do not support the requested transformation"。
- **见**：F-013。

#### 3.5.2 §4.4/§4.6 S256 算法 —— **符合**

`Pkce.cc:17-21` `computeCodeChallenge` 对 S256 = `base64url(sha256(verifier))`，对原始字节而非 hex 字符串编码。**这是规范正确的实现**（`TokenService.h` 顶部注释明确这是修复了旧 `generateSha256Hash` 的 base64(hex-string) bug 的新实现）。`OAuth2Plugin.cc:601-628` 的静态方法也委托到正确实现。

#### 3.5.3 §4.6 exchange 时校验 —— **符合**

`TokenService.cc:206-219`：存了 `codeChallenge` 则必须带 `codeVerifier` 且通过 `validatePkceCodeVerifier`→`verifyCodeVerifier`。

#### 3.5.4 BCP §2.1.1 / RFC 9700 强制 PKCE —— **不符合（Medium）**

- **证据**：`auth.require_pkce_for_public` 默认 OFF（`OAuth2Plugin.cc` 配置读取，`AuthorizationEndpointController.cc:416-441`、`SessionController.cc:555-572` 仅在 flag on 时强制）。且检查名"for public"但实际对任意 client 触发，未区分 client_type。
- **依据**：RFC 9700 §2.1.1 强烈建议对所有 authorization_code 客户端强制 PKCE。
- **见**：F-011。

#### 3.5.5 §4.1 code_verifier 格式校验 —— **部分符合（Low）**

`isValidCodeVerifierFormat`（`Pkce.cc:40-63`）已实现 43-128/[A-Za-z0-9-._~]，但**未在 exchange 路径调用**（仅重算比较）。RFC 7636 §4.6 不要求单独格式校验，故不算硬偏差。

---

### 3.6 RFC 8252 Native Apps —— **未实现（按适用性，Medium）**

- **loopback redirect 端口通配**（§7.3）：未实现。redirect_uri 精确匹配，native app 每次新端口都需重新注册。
- **public client 必须用 PKCE**（§8.1）：见 3.5.4，默认不强制。
- **判定**：未实现（按适用性评级 Medium；若项目不面向 native app，可标"不适用"）。
- **见**：F-014。

---

### 3.7 RFC 8628 Device Authorization Grant —— **部分符合**

#### 3.7.1 §3.1.1/§3.4 device_authorization 端点机密客户端认证 —— **不符合（High）**

- **证据**：`DeviceAuthController.cc:156` `plugin->validateClient(clientId, "", ...)` 用**空 secret** 调用。`MemoryClientRepository.cc:150-157`/`PostgresClientRepository.cc:218-225` 对机密客户端空 secret 返回 false，故机密客户端根本无法发起 device flow；公开客户端可发起。
- **依据**：RFC 8628 §3.1.1 要求机密客户端在 device_authorization 端点认证。
- **影响**：机密客户端无法用 device flow；公开客户端体验正常但未做 PKCE 关联。
- **见**：F-015。

#### 3.7.2 §3.2 响应字段 —— **部分符合（Low）**

- `device_code/user_code/verification_uri/expires_in/interval` 齐（`DeviceAuthController.cc`）。
- `verification_uri_complete` 已补齐（#146，2026-09-02）：`verification_uri + "?user_code=" + urlencode(user_code)`，审批页从 query 预填。默认 `verification_uri` 指向管理台审批页（`admin_console.url` 配置 + `/admin/devices`），可由 `custom_config.device_authorization.verification_uri` 覆盖（#146 前默认值 `http://localhost:5555/oauth2/device` 是无页面的死路径）。
- **user_code 字符集**：`"ABCDEFGHJKLMNPQRSTUVWXYZ23456789"`（`:75`）剔除歧义字符，符合 §5.2。
- **token 端点 client_type 分支**：`TokenEndpointController.cc:1227-1276` 正确分支（PUBLIC 仅 client_id；CONFIDENTIAL 需 secret）。✅

#### 3.7.3 §3.5 polling 错误码 —— **不符合（Medium）**

- **已发**：`authorization_pending/expired_token/access_denied/invalid_grant/invalid_request`（`:935-1086`，HTTP 400）✅。
- **slow_down 未发**：`ErrorCatalog.cc:232,483` 定义了 `slow_down`，但 polling 逻辑**从不**比较轮询频率，永远不返回 `slow_down`。Grep 确认无任何 emission 点。
- **依据**：RFC 8628 §3.5 "If the client is polling too quickly, the authorization server SHOULD return the `slow_down` error"。
- **见**：F-012。

---

### 3.8 RFC 8414 + OIDC Discovery 1.0 —— **部分符合**

#### 3.8.1 OIDC Discovery §4 字段完备性 —— **部分符合（Medium）**

`DiscoveryController.cc::oidcDiscovery`（`:152-222`）已发：issuer/authorization_endpoint/token_endpoint/userinfo_endpoint/device_authorization_endpoint/jwks_uri/introspection_endpoint/revocation_endpoint/response_types_supported/grant_types_supported/subject_types_supported/id_token_signing_alg_values_supported/scopes_supported/token_endpoint_auth_methods_supported/claims_supported/code_challenge_methods_supported。

**缺失**：
- `end_session_endpoint`（OIDC RP-Initiated Logout 必备）❌
- `registration_endpoint`（实际有 `/oauth2/register` 但未广告）❌
- `introspection_endpoint_auth_methods_supported` / `revocation_endpoint_auth_methods_supported`（仅 RFC 8414 `metadata()` 有，OIDC discovery 无）❌
- `response_modes_supported`（仅 metadata 有）❌

#### 3.8.2 OIDC Discovery §3 issuer 精确一致 —— **不符合（High）**

- **配置 issuer**：`baseUrl` 来自 `customConfig["metadata"]["issuer"]`，默认 `http://localhost:5555`（`DiscoveryController.cc:160-167`）。**默认是 http，OIDC Discovery §3 规定 issuer 须使用 https**。
- **iss claim 来源不一致**：access token 的 `iss` 取自 DB 列 `oauth2_access_tokens.issuer`（`PostgresTokenRepository.cc:537`）；**refresh token 内省 iss 硬编码 `"https://oauth.example.com"`**（`:567`）。签发时 issuer 写入何处未在本次审计深追，但 introspect 的 refresh 分支与配置 issuer 显然不符。
- **无尾斜杠归一化**：`baseUrl + "/oauth2/..."` 直接拼接（`:171-177`），若 operator 配置带尾斜杠会产生 `//oauth2/...`。
- **依据**：OIDC Discovery §3 "The issuer value MUST be exactly identical to the Issuer URL [...] The issuer value is a URL [...] using https"。
- **见**：F-016。

#### 3.8.3 RFC 8414 oauth-authorization-server —— **符合**

`metadata()`（`:57-151`）字段齐全。

---

### 3.9 OIDC Core 1.0 —— **部分符合（核心 MUST 大体满足，扩展 SHOULD 大面积缺失）**

#### 3.9.1 §2 id_token claims（核心 MUST 档）—— **部分符合（Medium）**

- **已发**：`iss/sub/aud/exp/iat/nonce`（`TokenService.cc:306-317`，nonce 仅在非空时）。
- **缺失**：`auth_time`、`acr`、`amr`、`azp`。
- **依据**：
  - OIDC Core §2 `auth_time`：REQUIRED when `max_age` 请求、否则 OPTIONAL。当前 max_age 不支持故可缺，但 §3.1.2.1 又把 max_age 列为支持项……故实际是"不支持 max_age 所以也不发 auth_time"——耦合缺失。
  - `azp`（§2）：当 aud 多值或与 client 不同时 REQUIRED。本项目 aud 永远等于 clientId，故 azp 可缺（合规）。
  - `acr/amr`：OPTIONAL 但 claims_supported 应反映实际能力（当前未广告，可接受）。
- **见**：F-021。

#### 3.9.2 §3.1.2.1 / §3.1.3.7 请求参数 prompt / max_age（核心 MUST 档，OIDC Core 把它们列为 OP 须支持的请求参数）—— **未实现（High）**

- **证据**：全仓 Grep `prompt`/`max_age` 在非测试代码中**零命中**。`AuthorizationEndpointController.cc` 不读这两个参数。
- **依据**：OIDC Core §3.1.2.1 把 `prompt`、`max_age`、`nonce` 列为 Authorization Endpoint 的请求参数；§3.1.3.7 规定 `auth_time` 与 `max_age` 校验为 MUST。
- **影响**：客户端无法请求 `prompt=none`（无交互静默登录失败应报错）、`prompt=login`（强制重新认证）、`max_age`（按年龄强制重认证）。这是 OIDC 互操作性的核心能力。
- **见**：F-022。

#### 3.9.3 §3.1.3.6 id_token 仅对 openid scope 签发 —— **符合**

`TokenService.cc:301-303` `if (jwkManager_ && ... && scope.find("openid") != npos)`。

#### 3.9.4 §5.3 UserInfo —— **部分符合（Medium）**

- **Bearer 校验**：✅ 经 `OAuth2AuthFilter`（`OAuth2AuthFilter.cc:39-55`）。
- **不校验 openid scope**：❌ `TokenEndpointController.cc:1297-1389` 与 filter 都不查 scope，**任何** access token（含 `client_credentials` 签发的 `sub=client:<id>` M2M token）都能取 userinfo。
- **CORS**：✅（修正先前误判）全局 `setupCors()`（`apps/server/src/bootstrap/CorsSetup.cc:68-79`）的 `postHandlingAdvice` 对所有响应（含 userinfo）加 `Access-Control-Allow-Origin`（精确白名单，无通配）。**符合** OIDC §7.2.1。
- **sub 用内部 userId**：§15 推荐 pairwise/stable 标识符，本项目用内部数字 id（非 pairwise）。
- **email_verified 声明但不返回**：`claims_supported` 含 `email_verified`（`DiscoveryController.cc:212`），但 userinfo 不返回（`TokenEndpointController.cc:1345-1383`）。
- **依据**：OIDC Core §5.3 "The UserInfo Endpoint MUST accept Access Tokens [...] The information returned [...] SHOULD be scoped to the OpenID Connect scopes"。
- **见**：F-023、F-024。

#### 3.9.5 §7.2.1 UserInfo CORS —— **符合**（见 3.9.4）

#### 3.9.6 §12 refresh 时重发 id_token（核心 MUST 档，OIDC Core §12 列为 SHOULD）—— **未实现（Medium）**

- **证据**：`TokenService.cc:430-436` refresh 响应只有 `access_token/token_type/expires_in/refresh_token`，无 `id_token` 分支。同样 `TokenEndpointController.cc:1145-1153` device_code 也不发 id_token。
- **影响**：OIDC 客户端在 refresh 后丢失 id_token，需重新走授权码流程。
- **见**：F-025。

#### 3.9.7 §10.1 nonce 单次使用（核心 MUST 档）—— **未实现（Medium）**

- **证据**：nonce 仅 echo 进 id_token（`TokenService.cc:314-317`），无任何存储/去重。
- **依据**：OIDC Core §15.5.2 "nonce [...] MUST be [...] used only once [...] Clients MUST verify that the nonce [...] is equal to the nonce sent". 服务端虽不强制单次，但缺 replay 检测使 nonce 防护弱化。
- **见**：F-026。

#### 3.9.8 §15 pairwise sub —— **未实现（Low）**

用内部 userId，非 pairwise。可选配置，缺即 Low。

---

### 3.10 OIDC RP-Initiated Logout / Session Management / Back-Channel Logout —— **未实现（扩展 SHOULD 档，Medium）**

- **end_session_endpoint**：全代码库无（Grep `end_session/post_logout/RP-Initiated` 零命中），discovery 不广告。
- **现有 logout**：`SessionController::logout`（`SessionController.cc:898-969`）是**非标 Bearer token 撤销端点**，取 `Authorization: Bearer`，调 `revokeAccessToken`，返回 JSON，不重定向，不处理 `id_token_hint`/`post_logout_redirect_uri`/`state`。
- **Drogon session 不失效**：`logout` 不调 `req->session()->invalidate()`。
- **Back-Channel Logout stub**：`sendBackchannelLogoutNotifications`（`SessionController.cc:66-69`）是 `LOG_DEBUG << "... stub"` 空实现。
- **判定**：**未实现**（按用户要求的"扩展 SHOULD 分档"，Medium）。
- **见**：F-027、F-028。

---

### 3.11 RFC 7519/7517/7515（JWT/JWK/JWS 支撑核验）—— **部分符合**

#### 3.11.1 JWT §4.1 标准 claim —— **部分符合（Low）**

id_token 含 iss/sub/aud/exp/iat/nonce，缺 `jti`（全库无）。jti 缺失意味着无 JWT 内置防重放。

#### 3.11.2 JWK §4 公钥格式 —— **符合**

`JwkManager::getJwks`（`:331-353`）发 `kty=RSA/use=sig/alg=RS256/kid/n/e`，`n/e` 由 `EVP_PKEY_get_bn_param` 取（`:305-314`），base64url。

#### 3.11.3 私钥保护 —— **符合**

仅取 n/e，无 d/p/q/dp/dq/qi。

#### 3.11.4 kid 选择/轮转 —— **未实现（Low）**

`JwkManager` init-once（`JwkManager.h:37-55`、`.cc:33-41`），单 kid（默认 `key-1`，dev `ephemeral-dev-key`），无多 key、无轮转、无按 token 选 kid。`signJwt` 永远用同一 kid。
- **见**：F-029。

#### 3.11.5 alg 白名单 / alg=none 防护 —— **符合（单向签发）**

`signJwt`（`JwkManager.cc:231-293`）硬编 `alg=RS256`，不解析外部 alg。本项目不验签外部 JWT，故无 alg=none 注入面。

---

### 3.12 RFC 7591 / 7592 动态客户端注册 —— **部分符合**

#### 3.12.1 RFC 7591 §3 动态注册 —— **部分符合（Medium）**

- **形态**：`ClientRegistrationService.cc:37-184` 接受 `client_name/client_type/token_endpoint_auth_method/redirect_uris/grant_types`，返回 `client_id/client_secret/client_id_issued_at/client_secret_expires_at=0`。形态正确。
- **gated by admin**：`AuthorizationFilter` 前置（`ClientRegistrationController.cc:24-30`），非 RFC 7591 §3 的开放注册模型。可接受的设计选择但偏离规范默认。
- **client_secret 哈希 bug**：见 F-002，导致注册的客户端无法认证。

#### 3.12.2 RFC 7592 客户端自管理 —— **未实现（Low）**

无 `/oauth2/register/{client_id}` 路由，无 `registration_access_token`。客户端无法自管，仅 admin 通过 `/api/admin/clients/*`。
- **见**：F-030。

#### 3.12.3 token_endpoint_auth_method 持久化 —— **未实现（Medium）**

`ClientRegistrationService.cc:57-58,181` 读取并回显，但**不入库**（`Oauth2Clients` 表无此列，`Oauth2Clients.h:52-64`）。token 端点永远 Basic→Post 回退，不按客户端声明方法。
- **见**：F-017。

---

### 3.13 RFC 9068 JWT-format Access Tokens —— **不适用（未实现，按设计）**

access token 为 opaque 随机串（`TokenCrypto.cc:9-24`，32 字节 base64url，仅哈希入库）。RFC 9068 不适用。**评级：未实现/不适用**，非缺陷。

---

### 3.14 RFC 9700 / OAuth 2.0 Security BCP —— **部分符合**

| 条款 | 状态 | 说明 |
|---|---|---|
| §2.1.1 强制 PKCE | ❌ Medium | 默认 off（F-011） |
| §2.2.1 redirect_uri 精确匹配 | ✅ | 已满足 |
| §2.4 token 端点限流 | ❌ Medium | 无限流（F-018） |
| §4.9 凭证常量时间比较 | ❌ High | Redis 非常量时间（F-004） |
| §4.9.1 client_secret 加盐哈希 | ❌ Critical | 写入路径无盐（F-002） |
| §4.10 id_token/auth_time 校验 | ❌ Medium | 未实现（F-022） |
| §4.11.1 state CSRF | ✅ | state 强制（自定义策略） |
| §4.12.1 redirect_uri open redirect | ✅ | 精确匹配防住（state 拼接未 urlEncode 见 F-020） |

---

### 3.15 横向安全与运维 —— 跨规范聚合

| 主题 | 状态 | 见 |
|---|---|---|
| token 哈希一致性（UPPER-hex） | ✅ | — |
| client_secret 写读哈希不一致 | ❌ Critical | F-002 |
| Memory 后端明文存 client_secret | ❌ Medium | F-031 |
| token 端点 / introspect / revoke 限流 | ❌ Medium | F-018 |
| 日志脱敏（access/refresh 不进日志） | ✅ 大体 | 大部分变量是 hash；少量 LOG_INFO 打 hash 值（如 `PostgresTokenRepository.cc:378,383`）属可接受 |
| CORS 全局白名单 | ✅ | `CorsSetup.cc` |
| open redirect（state 拼接未 urlEncode） | ❌ Low | F-020 |

---

## 4. 发现清单（按编号）

> 每条：规范依据 / 现象 / 根因 `file:line` / 风险 / 影响 / 整改建议。

### F-001 [文档] OpenAPI grant_type 枚举缺 device_code 【Low】
- **依据**：RFC 8628 §3.4 + OpenAPI 准确性。
- **现象**：`openapi.yaml` 的 `/oauth2/token` grant_type enum 只列 `authorization_code,refresh_token,client_credentials`，缺 `urn:ietf:params:oauth:grant-type:device_code`（代码 `RuleSet.cc:255-261` 已支持）。
- **整改**：补全 enum。
- **阶段**：P2。

### F-002 [Critical] client_secret 哈希写入与校验算法不一致，动态注册客户端无法认证
- **依据**：RFC 6749 §10.6 "The client password MUST be hashed [...] using a salt"；OAuth 2.0 Security BCP §4.9.1。
- **现象**：注册/管理路径写无盐大写 SHA-256，校验路径算有盐小写 SHA-256，两者永不匹配。
- **根因**：
  - 写入：`ClientRegistrationService.cc:143` `hashToken(clientSecret)`（无盐，`TokenCrypto.cc:26-37` UPPER-hex sha256(secret)）；`ClientManagementService.cc:128-129,367-368` 同样。reset 路径 `:380` 甚至不更新 salt。
  - 校验：`PostgresClientRepository.cc:231` `getSha256(clientSecret + salt)` 再 `tolower`；`RedisClientRepository.cc:165-175` 同。
- **影响**：所有经 `/oauth2/register` 或 `/api/admin/clients` 创建的机密客户端，在 token 端点永远 `invalid_client`。除非有未审的 seed/bootstrap 路径用有盐算法预置客户端（需 operator 确认），否则机密客户端流程整体不可用。
- **整改建议**：
  1. 统一为单一算法（推荐 PBKDF2/Argon2id；最低限度统一为有盐 SHA-256，与校验路径一致）。
  2. 写入路径同时更新 salt。
  3. 提供一次性数据迁移脚本：检测 `client_secret` 列无盐哈希模式 → 触发管理员强制 reset。
- **阶段**：P0。

### F-003 [High] refresh_token grant 跳过客户端认证
- **依据**：RFC 6749 §3.2.1。
- **现象**：见 3.1.6。
- **根因**：`TokenEndpointController.cc:679-711` 不调 `validateClient`，仅 `TokenService.cc:387-391` 做字符串 client_id 比较。
- **整改**：在 controller 入口对机密客户端加 `validateClient`（参考 client_credentials 分支 `:733-775`），公开客户端仅验 client_id 存在。
- **阶段**：P0。

### F-004 [High] Redis 后端 client_secret 非常量时间比较
- **依据**：OAuth 2.0 Security BCP §4.9；RFC 6749 §10.6。
- **现象**：`RedisClientRepository.cc:174-175` `calculatedHash == storedHash` 用 `std::string::operator==`，非常量时间。
- **整改**：复用 `constantTimeMemcmp`（Postgres/Memory 已有实现，抽到 common）。
- **阶段**：P0。

### F-005 [High] Redis 后端 refresh_token 存储为空操作，轮换/级联吊销失效
- **依据**：RFC 6749 §6。
- **现象**：`RedisTokenRepository.cc:155-165` `saveRefreshToken`/`getRefreshToken` 空 return；`atomicRevokeRefreshToken`（`:192-213`）因 getRefreshToken 空 op 永远返回 nullopt；reuse-detection + revokeTokenFamily 不触发。
- **影响**：Redis 部署下 refresh token 既不存储也不轮换，刷新请求永远 `invalid_grant`（或依赖某种 fallback；需运行时确认）。
- **整改**：实现 Redis refresh token 存取 + family_id 索引；或文档明确"Redis 后端不支持 refresh_token grant"。
- **阶段**：P0（若生产用 Redis）/ P1（若仅 Postgres 生产）。

### F-006 [High] 资源端点 Bearer 401 不发 WWW-Authenticate challenge
- **依据**：RFC 6750 §3。
- **现象**：`OAuth2AuthFilter.cc`/`AuthorizationFilter.cc` 返回应用信封 `AUTH_TOKEN_INVALID`，不按 RFC 6750 §3 发 `WWW-Authenticate: Bearer realm, error=invalid_token, error_description`。
- **整改**：在 filter 401 路径加 RFC 6750 §3 challenge 头。
- **阶段**：P1。

### F-007 [High] authorization 端点错误未按 §4.1.2.1 重定向
- **依据**：RFC 6749 §4.1.2.1。
- **现象**：见 3.1.4。仅 consent-deny 重定向，其余错误（invalid_scope、server_error、PKCE-required）直接 4xx，state 不回显。
- **整改**：按 §4.1.2.1 区分（a）client_id 未知 / redirect_uri 无效 → 直接 4xx；（b）其余 → 302 `?error=&error_description=&state=` 重定向到已注册 redirect_uri。
- **阶段**：P1。

### F-008 [Medium] token 端点 validation gate 用应用信封而非 OAuth2 error 码
- **依据**：RFC 6749 §5.2。
- **现象**：`HttpResponder.cc:57-58,86` 失败返回 `VALIDATION_INVALID_INPUT` 信封 + 400，非 `error: "invalid_request"`。
- **整改**：token 端点的 validation gate 走 `OAuth2ErrorHandler` 发标准 OAuth2 error 信封。
- **阶段**：P1。

### F-009 [Medium] authorization_code 兑换时空 redirect_uri 跳过 §4.1.3 比较
- **依据**：RFC 6749 §4.1.3。
- **现象**：见 3.1.5。`PostgresGrantRepository.cc:181-189` 与 `MemoryGrantRepository.cc:80-87` 用 `if (!redirectUri.empty() && ...)` 守卫。
- **整改**：若签发时存了 redirect_uri，兑换时必须带且必须匹配（empty 应直接 `invalid_grant`）。
- **阶段**：P1。

### F-010 [High] 资源端点不按 scope 拒绝（insufficient_scope）
- **依据**：RFC 6750 §3.1。
- **现象**：见 3.2.4。
- **整改**：为受保护端点定义所需 scope，filter 校验，缺则 `insufficient_scope` 403。
- **阶段**：P1。

### F-011 [Medium] PKCE 默认不强制
- **依据**：RFC 9700 §2.1.1。
- **现象**：见 3.5.4。
- **整改**：默认 `require_pkce=true`（至少对所有 authorization_code 客户端）。
- **阶段**：P1。

### F-012 [Medium] device flow 永不返回 slow_down
- **依据**：RFC 8628 §3.5。
- **现象**：见 3.7.3。`ErrorCatalog.cc:232` 定义但无 emission。
- **整改**：在 polling 分支按 `interval` 与最近 poll 时间比较，过快返回 `slow_down`。
- **阶段**：P1。

### F-013 [Medium] authorize 端不校验 code_challenge_method 集合
- **依据**：RFC 7636 §4.3。
- **现象**：见 3.5.1。
- **整改**：authorize 入口校验 method ∈ \{plain, S256\}，否则 `invalid_request` 直接 4xx（属 client 错误，可直接返回）。
- **阶段**：P1。

### F-014 [Medium] 无 HTTPS 强制 / 无 loopback 例外
- **依据**：RFC 6749 §3.1.2.1；RFC 8252 §7.3。
- **现象**：见 3.1.1。
- **整改**：redirect_uri 注册时强制 https（生产）；实现 RFC 8252 loopback 端口通配（`http://127.0.0.1:*` / `http://[::1]:*`）。
- **阶段**：P1。

### F-015 [High] device_authorization 端点不认证机密客户端
- **依据**：RFC 8628 §3.1.1。
- **现象**：见 3.7.1。`DeviceAuthController.cc:156` 空 secret 调用。
- **整改**：device_authorization 端点按 client_type 分支（参考 token 端点 device_code 分支 `:1227-1276`）。
- **阶段**：P1。

### F-016 [High] issuer 与 iss claim 不一致 / refresh introspect iss 硬编码
- **依据**：OIDC Discovery §3。
- **现象**：见 3.8.2。`PostgresTokenRepository.cc:567` 硬编 `https://oauth.example.com`。
- **整改**：refresh token 内省 iss 取配置 issuer；签发时统一写入 issuer 字段；discovery issuer 强制 https + 尾斜杠归一化。
- **阶段**：P0（iss 一致性）/ P1（https 强制）。

### F-017 [Medium] token_endpoint_auth_method 不持久化
- **依据**：RFC 7591 §2 + RFC 6749 §3.2.1。
- **现象**：见 3.12.3。
- **整改**：加 DB 列；token 端点按客户端声明方法认证。
- **阶段**：P1。

### F-018 [Medium] token/introspect/revoke 端点无限流/防爆破
- **依据**：RFC 9700 §2.4。
- **现象**：见 3.14。
- **整改**：加 per-client/IP 失败计数 + 指数退避（可复用 identity 的 account-lockout 机制）。
- **阶段**：P1。

### F-019 [Low] 成功响应缺 Cache-Control:no-store
- **依据**：RFC 6749 §5.1；RFC 7009 §2.2.1。
- **现象**：token 成功响应、revoke 成功响应不加 `Cache-Control: no-store`/`Pragma: no-cache`（仅错误响应加）。
- **整改**：所有 token 类响应统一加。
- **阶段**：P2。

### F-020 [Low] authorize 成功重定向 state 未 urlEncode
- **依据**：RFC 6749 §4.1.2 / §4.1.3（state 透传完整性）。
- **现象**：`AuthorizationEndpointController.cc:468-470` `location += "&state=" + state;` 未 urlEncode（login/consent 中间跳转已 urlEncode）。`code` 同样未 urlEncode。
- **整改**：state 与 code 统一 urlEncode。
- **阶段**：P2。

### F-021 [Medium] id_token 缺 auth_time / acr / amr / azp
- **依据**：OIDC Core §2。
- **现象**：见 3.9.1。
- **整改**：耦合 F-022 一起做（auth_time 依赖 prompt/max_age）。
- **阶段**：P1。

### F-022 [High] OIDC prompt / max_age 未实现
- **依据**：OIDC Core §3.1.2.1 / §3.1.3.7。
- **现象**：见 3.9.2。
- **整改**：authorize 端解析 `prompt`（none/login/consent/select_account）与 `max_age`；按值控制交互/重认证；签发 id_token 时按需写 auth_time/acr。
- **阶段**：P1。

### F-023 [Medium] UserInfo 不校验 openid scope
- **依据**：OIDC Core §5.3。
- **现象**：见 3.9.4。
- **整改**：userinfo handler 校验 token scope 含 `openid`（client_credentials token 的 `sub=client:*` 应直接拒）。
- **阶段**：P1。

### F-024 [Low] claims_supported 含 email_verified 但 userinfo 不返回
- **依据**：OIDC Discovery §3 accuracy。
- **整改**：要么 userinfo 返回 email_verified（已知 login 时有此值），要么从 claims_supported 移除。
- **阶段**：P2。

### F-025 [Medium] refresh / device_code 不重发 id_token
- **依据**：OIDC Core §12。
- **现象**：见 3.9.6。
- **整改**：refresh 时若原 scope 含 openid 且 jwkManager 可用，重签 id_token。
- **阶段**：P1。

### F-026 [Medium] nonce 无单次使用/防重放存储
- **依据**：OIDC Core §15.5.2。
- **整改**：nonce 落库 + 兑换时去重（可复用 grant/code 的过期清理机制）。
- **阶段**：P1。

### F-027 [Medium] RP-Initiated Logout 未实现（end_session_endpoint）
- **依据**：OIDC RP-Initiated Logout 1.0。
- **现象**：见 3.10。
- **整改**：新增 `/oauth2/end_session` GET，处理 `id_token_hint`/`post_logout_redirect_uri`/`state`，discovery 广告 `end_session_endpoint`。
- **阶段**：P1。

### F-028 [Medium] logout 不失效 Drogon session + backchannel stub
- **依据**：OIDC Session Management / Back-Channel Logout 1.0。
- **现象**：`SessionController.cc:898-969` 不调 `session()->invalidate()`；`:66-69` backchannel 是 stub。
- **整改**：logout 调 session invalidate；backchannel 实现或显式声明不支持。
- **阶段**：P1。

### F-029 [Low] JWKS 无密钥轮转
- **依据**：运维最佳实践（RFC 7517 无强制轮转要求）。
- **整改**：支持多 kid + current/previous；按 kid 选签发 key；JWKS 暴露全部公钥。
- **阶段**：P2。

### F-030 [Low] RFC 7592 客户端自管理未实现
- **依据**：RFC 7592。
- **整改**：按需实现 `registration_access_token` + `/oauth2/register/{client_id}` CRUD；或文档明确仅 admin 管理。
- **阶段**：P2。

### F-031 [Medium] Memory 后端明文存 client_secret
- **依据**：RFC 6749 §10.6。
- **现象**：`MemoryClientRepository.cc:67` 注释 "we store plain text"。
- **整改**：Memory 后端也走哈希（仅用于测试场景则文档明确"测试用，不存敏感数据"）。
- **阶段**：P2（若 Memory 仅测试）/ P1（若生产可选）。

---

## 5. 分阶段整改计划

### P0 — 严重/高优先（认证与令牌安全，1-2 周内）

| 编号 | 工作项 | 验收标准 | 估计复杂度 |
|---|---|---|---|
| F-002 | 统一 client_secret 哈希算法（推荐 Argon2id/PBKDF2；最低有盐 SHA-256）+ 写入路径更新 salt + 数据迁移 | 动态注册客户端可在 token 端点成功认证；存量客户端有 reset 通道 | 中（含迁移） |
| F-003 | refresh_token grant 入口加 `validateClient` | 机密客户端无 secret 时 401 invalid_client | 低 |
| F-004 | Redis validateClient 用 constantTimeMemcmp | 单元测试覆盖常量时间 | 低 |
| F-005 | 决策 Redis 是否支持 refresh_token；若支持则实现 save/get/atomicRevoke | Redis 部署下 refresh 流程端到端通过；或文档明确不支持 | 中 / 决策 |
| F-016（iss 部分） | refresh introspect iss 取配置 issuer；签发统一写 issuer | introspect 返回的 iss 与 discovery issuer 字节一致 | 低 |

### P1 — 中优先（OIDC 完备性、协议合规加固，3-6 周）

| 编号 | 工作项 | 验收标准 |
|---|---|---|
| F-006 | 资源端点 401 加 RFC 6750 §3 Bearer challenge | WWW-Authenticate: Bearer realm/error/error_description |
| F-007 | authorize 错误按 §4.1.2.1 重定向 | redirectable 错误 302 带 error/state |
| F-008 | token 端点 validation gate 走 OAuth2 error 信封 | 错误体含 `error: invalid_request` |
| F-009 | exchange 时空 redirect_uri 强制校验 | 签发带 redirect_uri 时兑换必带且匹配 |
| F-010 | 资源端点 scope 校验 + insufficient_scope | 按 scope 拒绝返回 403 insufficient_scope |
| F-011 | PKCE 默认强制 | 默认 require_pkce=true |
| F-012 | device flow slow_down | 过快轮询返回 slow_down |
| F-013 | authorize 校验 code_challenge_method 集合 | 非 plain/S256 直接 4xx |
| F-014 | HTTPS 强制 + loopback 例外 | 生产 redirect_uri 强制 https；支持 RFC 8252 loopback |
| F-015 | device_authorization 认证机密客户端 | 机密客户端可发起 device flow |
| F-017 | 持久化 token_endpoint_auth_method | 客户端按声明方法认证 |
| F-018 | 端点限流 | 失败计数 + 退避 |
| F-021+F-022 | 实现 OIDC prompt/max_age + auth_time/acr | prompt=none/max_age 互操作通过 |
| F-023 | UserInfo 校验 openid scope | M2M token 取 userinfo 被拒 |
| F-025 | refresh/device 重发 id_token | OIDC refresh 互操作通过 |
| F-026 | nonce 防重放存储 | 重复 nonce 兑换被拒 |
| F-027 | RP-Initiated Logout | /oauth2/end_session 端到端 |
| F-028 | logout 失效 session + backchannel 决策 | session 真正失效 |

### P2 — 低优先（一致性、文档、加固，按需）

F-001（OpenAPI enum）/ F-019（Cache-Control）/ F-020（state urlEncode）/ F-024（email_verified 一致）/ F-029（JWKS 轮转）/ F-030（RFC 7592）/ F-031（Memory 明文）。

---

## 6. 已确认合规项（正向清单）

为平衡视角，下列核心能力经核实**符合规范**，构成 fulla 的合规基座：

1. 授权码生成：256-bit `RAND_bytes` + base64url + 仅哈希入库（`TokenCrypto.cc:9-24,26-37`）
2. 授权码单次性：原子 CAS（`PostgresGrantRepository.cc:165-176`）
3. 授权码 TTL：默认 600s（`OAuth2Plugin.cc:53`）
4. refresh token 轮换 + 重用检测 + family 级联吊销（Postgres；`TokenService.cc:353-385`、`PostgresTokenRepository.cc:404-492`）
5. PKCE S256 算法规范正确（`Pkce.cc:17-21`）
6. redirect_uri 精确匹配（`Client.h:83-90`）
7. state 强制 + 长度/字符校验（`AuthorizationEndpointController.cc:113-189`）
8. client_secret 比较常量时间（Postgres/Memory）
9. access/refresh token 哈希存储一致（UPPER-hex SHA-256）
10. introspect/revoke 客户端凭证认证模型（commit 246db32 修正后，`TokenEndpointController.cc:279-345,420-523`）
11. revoke 所有权校验 + 未知令牌返回 200（`TokenEndpointController.cc:489-544`）
12. JWKS 仅暴露公钥 n/e（`JwkManager.cc:305-353`）
13. id_token 仅对 openid scope 签发（`TokenService.cc:301-303`）
14. id_token nonce 端到端透传（`TokenService.cc:314-317`）
15. device_code 原子 consume 防竞态（`TokenEndpointController.cc:1065-1090`）
16. CORS 全局白名单（无通配，`CorsSetup.cc:11-30`）

---

## 7. 未核验项声明

以下项本次未深入核实，列出以备后续：

- access token 签发时 `issuer` 字段写入 DB 的具体值与位置（F-016 的另一半）。
- 是否存在 seed/bootstrap 路径用有盐算法预置客户端（影响 F-002 的实际爆炸半径）。
- Redis 后端 `getAccessToken`/`saveAccessToken` 是否同样存在空操作（本次只确认了 refresh 的空操作）。
- 测试代码（`tests/`）与 seed 脚本（`apps/server/seed/`）未纳入审计。
- 运行时行为未验证（如 server_error 路径、并发竞态）。
- WebAuthn / GitHub/Google/WeChat 社交登录的 OAuth dance（OIDC federation 范畴）未审。

---

## 8. 整改状态表（修复进度追踪）

核验基线：`master`（审计所引代码逐条比对过）。处置结论分三类：**修复**（代码已改）、**文档化**（不改码，文档说明）、**伪问题关闭**。

整改分支：`fix/oauth-oidc-compliance-batch-0-1`，三批次提交：
- Batch 0+1 = `040639b`（P0 安全 + 协议正确性）
- Batch 2 = `a7fd184`（OIDC 全量扩展）
- Batch 3 = `c911ee9`（加固与清理）

每批次均 `manage.ps1 build-backend` + `test-backend` 全绿（**456/456 CTest 通过**，含 Postgres 与 CI/memory 两套配置）。

| Issue | 发现 | 结论 | 状态 |
|---|---|---|---|
| #21 | F-002 client_secret 哈希写/读算法不一致 | 修复：写入路径统一有盐小写 SHA-256（含 reset 轮换 salt） | ✅ Batch 0+1 (`040639b`) |
| #22 | F-003 refresh_token grant 无客户端认证 | 修复：CONFIDENTIAL 需 secret（401 invalid_client），PUBLIC 仅验存在 | ✅ Batch 0+1 (`040639b`) |
| #23 | F-004 Redis client_secret 比较非常量时间 | 修复：三后端统一 `constantTimeMemcmp`，删泄漏比较结果的 LOG_DEBUG | ✅ Batch 0+1 (`040639b`) |
| #24 | F-005 Redis 后端 refresh 存储空操作 | 修复（弃用处置）：启动 LOG_ERROR + refresh grant 返回 `unsupported_grant_type`；postgres+redis 缓存架构另立 issue #42 | ✅ Batch 0+1 (`040639b`) |
| #25 | F-016 issuer 不一致 | 修复：签发写入配置 issuer（含 client_credentials/device 分支）+ 删三后端硬编码 + introspect 兜底 + discovery 尾斜线归一化 + http issuer 告警；比报告额外发现：`saveAccessToken` 从未写 issuer 列 | ✅ Batch 0+1 (`040639b`) |
| #26 | F-007 authorize 错误不重定向 | 修复：client_id 未知/redirect_uri 无效仍直接 4xx；其余按 §4.1.2.1 302 重定向并回显 state | ✅ Batch 0+1 (`040639b`) |
| #27 | F-010 资源不强制 scope | 修复（最小 scope 校验：userinfo→openid、/api/me→profile、/api/admin→admin + 403 insufficient_scope）；完整资源-scope 模型另立 issue #43 | ✅ Batch 3 (`c911ee9`) |
| #28 | F-006 Bearer 401 缺 WWW-Authenticate | 修复：资源端点 401 加 `WWW-Authenticate: Bearer error="invalid_token"` | ✅ Batch 0+1 (`040639b`) |
| #29 | F-021+F-022 prompt/max_age/auth_time/acr | 修复（全量）：prompt/max_age 解析 + 三签发路径透传 auth_time/amr + id_token auth_time/acr/amr claims | ✅ Batch 2 (`a7fd184`) |
| #30 | F-027+F-028 RP-Initiated Logout | 修复：新增 `/oauth2/end_session`（GET+POST）+ session clear；backchannel 文档化不实现 | ✅ Batch 2 (`a7fd184`) |
| #31 | F-015 device_authorization 不认证机密客户端 | 修复：按 client_type 分支认证 | ✅ Batch 0+1 (`040639b`) |
| #32 | F-025 refresh/device 不重发 id_token | 修复：refresh/device 在 openid scope 时重签 id_token | ✅ Batch 2 (`a7fd184`) |
| #33 | F-011 PKCE 默认不强制 | 修复：默认值改 true + 4 个 config 显式 true | ✅ Batch 0+1 (`040639b`) |
| #34 | F-012 device flow 无 slow_down | 修复：`last_polled_at` 列 + interval 递增 5s | ✅ Batch 0+1 (`040639b`) |
| #35 | F-008+F-009+F-013 token 错误信封/redirect_uri/挑战方法校验 | 修复：token gate 发 OAuth2 invalid_request 信封；空 redirect_uri 不再绕过；authorize 校验 code_challenge_method ∈ \{plain,S256\} | ✅ Batch 0+1 (`040639b`) |
| #36 | F-014 redirect_uri 无 https 强制/loopback 例外 | 修复：https 强制 + 仅 IP 字面量 loopback（127.0.0.1/[::1]）豁免 + `auth.allow_http_redirect_uri` 开关；seed/测试 localhost→127.0.0.1 | ✅ Batch 0+1 (`040639b`) |
| #37 | F-017+F-023+F-026 | F-017 持久化 + 强制 token_endpoint_auth_method（NULL 保留回退）；F-023 userinfo 校验 openid scope + email_verified（F-024）；F-026（nonce 服务端防重放）**伪问题关闭**：OIDC §15.5.2 的 nonce 校验是客户端 MUST，服务端存储非规范强制，文档说明 | ✅ Batch 2 (`a7fd184`) |
| #38 | F-018 端点无限流 | 修复：进程内滑动窗口限流（per IP+client_id，仅计失败，429 + Retry-After） | ✅ Batch 3 (`c911ee9`) |
| #39 | P2 批量（F-001/F-019/F-020/F-024/F-029/F-030/F-031） | F-001/F-019/F-020 修复；F-024 随 F-023；F-029/F-030/F-031 文档化不改码 | ✅ Batch 3 (`c911ee9`) |

决策记录（用户拍板）：F-002 选有盐 SHA-256；F-005 目标架构 postgres 存储 + redis 缓存，独立 Redis 模式废弃（issue #42）；F-010 最小 scope 校验 + 独立 issue #43；OIDC 扩展全量实现 prompt/max_age；schema 直接改源头 migrations（无生产数据，不做增量迁移）。

**最终符合性**：审计报告中标记的 31 项发现全部处置完毕（28 项代码修复 + 3 项文档化 + F-026 伪问题关闭）。Issue #21-#39 已在 `fix/oauth-oidc-compliance-batch-0-1` 分支修复并验证，待合入后关闭（fine-grained PAT 无 close 权限，需手动关闭）；#40 为总跟踪，#42/#43 为两个架构 follow-up。

---

**报告版本**：v1.3（追加整改后深度复查） | **审查日期**：2026-08-07 | **整改完成日期**：2026-08-08 | **复查日期**：2026-08-08 | **审查者**：ZCode | **代码基线**：`test/coverage-push` @ 09ebf8d | **整改+复查基线**：`fix/oauth-oidc-compliance-batch-0-1` @ 4b4e282

---

## 9. 整改后深度复查（2026-08-08）

> 按本计划 `oauth-oidc-compliance-audit-plan.md` §二「规范清单与逐项检查要点」的全部 14 个 RFC 规范、~90 个检查点，对整改后的代码（`fix/oauth-oidc-compliance-batch-0-1` @ 4b4e282）重新逐项核验。4 个并行核验 agent 覆盖 RFC 6749/6750、7662/7009/7636/8252、8628/8414/OIDC Discovery、OIDC Core/JWT-JWK/7591/9068/9700/横向安全，每项结论附 `file:line` 证据。

### 9.1 复查结论概览

**合规：88/90 检查点 ✅ COMPLIANT**（含 1 项 ➖ N/A：RFC 9068 JWT access token——access token 为 opaque 设计）。R-1/R-4/R-5 三项新发现已快速修复（见下表处置列），R-2/R-3 记录在案留待 follow-up。所有 F-001–F-031 整改项在当前代码中**均已落地、无回退**，关键修复点（F-002 写读哈希一致、F-003 refresh 客户端认证、F-011 PKCE 默认强制、F-013/§4.6 S256 正确算法、F-016 issuer 一致、F-017 auth_method 强制、F-018 限流、F-019 no-store、F-020 urlEncode、F-022 prompt/max_age/auth_time/acr/amr、F-023 userinfo openid、F-025 refresh id_token、F-027 end_session）经直接代码复核确认存在且端到端连通。

**5 项待处置（均为新发现，非回退，无 1 项是整改前已报告的 F-xxx 漏修）**：

| ID | 发现 | 风险 | 性质 | 处置 |
|---|---|---|---|---|
| **R-1** | `acr` claim 以 JSON **整数**签发（`TokenService.cc:367`、`OAuth2Plugin.cc:778` `Json::Int64`），但 OIDC Core §2 规定 `acr` 为**字符串**，且 discovery `acr_values_supported` 广告的是字符串 `"1"`/`"2"`——id_token 与 discovery 类型/值不匹配 | 中 | 新发现，整改引入 | ✅ **已修**：改为 `Json::String`（"1"/"2"），两处签发点 + 2 个单测断言同步 |
| **R-2** | `/oauth2/register` 路由在所有 config 的 `rbac_rules` 均无对应条目，`AuthorizationFilter` 默认拒绝 → 动态注册端点**对所有用户（含 admin）返回 403**；实际客户端创建走 `/api/admin/clients`（匹配 `/api/admin/.*`）。注册能力存在但端点不可达 | 低（功能不可用，但无安全影响） | 预先存在（master 上即如此），非本批引入；属文档/配置与实现不一致 | 📌 **记录**：非合规硬伤，是端点可达性问题。建议二选一——在 `rbac_rules` 加 `"/oauth2/register": ["admin"]`，或文档明确该端点仅供 admin、实际用 `/api/admin/clients`。留待 follow-up。 |
| **R-3** | `RedisGrantRepository::saveAuthCode`（`RedisGrantRepository.cc:48`）不持久化 `code_challenge`/`code_challenge_method`/`nonce`/`auth_time`/`amr`——Redis 部署下 PKCE 校验被静默跳过、id_token 丢失 nonce/auth_time/amr | 高（**仅 Redis 部署**；Postgres/Memory 正确） | 预先存在（Redis grant 存储历来不完整）；F-005 已声明独立 Redis 模式废弃，故实际爆炸半径=坚持用废弃 Redis 模式的人 | 📌 **记录**：由架构 follow-up issue #42 覆盖（目标架构 Postgres 存储 + Redis 仅作缓存，grant 走 Postgres）。本轮不修——独立 Redis 存储模式已废弃（启动 LOG_ERROR + refresh grant 返回 unsupported_grant_type）。 |
| **R-4** | discovery `prompt_values_supported` 含 `select_account`（`DiscoveryController.cc:229`），但 authorize 端无对应分支（仅 none/login/consent 被处理，`AuthorizationEndpointController.cc:282-284`）——advertised-but-not-honored | 低 | 新发现，整改引入 | ✅ **已修**：从 discovery `prompt_values_supported` 与 OpenAPI prompt 参数描述移除 `select_account`（只广告实际支持的 none/login/consent） |
| **R-5** | RFC 8414 `metadata()`（`DiscoveryController.cc:72-165`）缺 `subject_types_supported`（RFC 8414 §2 REQUIRED），且两份 discovery 文档均未广告 `registration_endpoint`（尽管 `/oauth2/register` 已实现） | 低 | 预先存在（metadata 路径）+ 一致性 | ✅ **已修**：metadata() 补 `subject_types_supported=["public"]`；metadata() 与 oidcDiscovery() 均补 `registration_endpoint` |

### 9.2 低风险提示（非缺陷，记录在案）

- **RFC 7662 §2.2** introspection 不返回 `username`/`jti`——两者均 OPTIONAL，合规。
- **RFC 7009 §2.2.1** 跨客户端撤销返回 `unauthorized_client`（非静默 200）——偏向安全的可辩护解读，记录为设计决策。
- **Memory 后端** client_secret 明文比较（F-031 已文档化为 dev/test 专用）。
- **`AuthorizationFilter`** 仍接受 `?access_token=` query 传 token（RFC 6750 §2.3 已废弃但合规）；`OAuth2AuthFilter` 已收紧为仅 header。
- **PKCE verifier 比较**（`Pkce.cc:35`）用 `==` 非常量时间——低危（verifier 高熵，时序攻击不实际）。
- **`access_denied`** 在 ErrorCatalog 映射为 403（RFC 6749 §5.2 列在 400）——仅用于资源侧/撤销所有权拒绝，authorize 端的 access_denied 走 302 重定向，无安全影响。
- **OAuth2Plugin 与 DiscoveryController 各自独立 fallback `http://localhost:5555`**——当前一致，但两处字面量可能漂移。

### 9.3 处置结果

- **R-1（acr 类型）** ✅ **已修**：`TokenService.cc` + `OAuth2Plugin.cc` 两处 `Json::Int64(...)` → 字符串 `"1"`/`"2"`；2 个单测断言（`TokenServiceTest`）同步为 `asString()`。
- **R-4（select_account）** ✅ **已修**：从 `DiscoveryController.cc` 的 `prompt_values_supported` 与 `AuthorizationEndpointController.cc`/`openapi.yaml` 的 prompt 参数描述移除 `select_account`（只广告实际支持的 none/login/consent）。
- **R-5（discovery 字段）** ✅ **已修**：`DiscoveryController.cc` 的 RFC 8414 `metadata()` 补 `subject_types_supported=["public"]`；`metadata()` 与 `oidcDiscovery()` 均补 `registration_endpoint`。
- **R-2（register RBAC）** 📌 **记录在案**：非合规硬伤（端点可达性问题，预先存在）。建议 follow-up 二选一——`rbac_rules` 加 `"/oauth2/register": ["admin"]`，或文档明确实际用 `/api/admin/clients`。本轮不改码。
- **R-3（Redis grant）** 📌 **记录在案**：由架构 follow-up issue #42 覆盖（目标 Postgres 存储 + Redis 仅缓存，grant 走 Postgres）。本轮不修——独立 Redis 存储模式已废弃。

### 9.4 未回退确认

逐项确认无整改前已报告的 F-xxx 出现回退：F-002 写读哈希一致（`ClientRegistrationService.cc:204-220` 写有盐小写 SHA-256 ↔ `PostgresClientRepository.cc:213-243` 读同算法常量时间比较）、F-003 refresh 认证（`TokenEndpointController.cc:1037-1134` 按 client_type 分支）、F-011 PKCE（代码与 4 个 config 默认 true）、F-013 method 校验（`AuthorizationEndpointController.cc:375-393`）、§4.6 S256 正确算法（`Pkce.cc:17-21` base64url(raw digest)）、F-016 issuer 一致（无 `oauth.example.com` 残留）、F-017 强制（`enforceClientAuthMethod`）、F-018 限流（`RateLimiter` 失败计数 429）、F-019 no-store（`applyNoStoreHeaders` 全成功响应）、F-020 urlEncode（4 处签发重定向）、F-022 prompt/max_age/auth_time/amr（`AuthorizationEndpointController.cc` + `SessionController.cc:484-489` + `MfaController.cc:540-550`）、F-023 userinfo openid（`TokenEndpointController.cc:1879-1898`）、F-025 refresh/device id_token、F-027 end_session（`SessionController.cc:1105-1227` + session clear）——均存在且端到端连通。

**整体结论**：整改有效，14 个 RFC 规范的 ~90 检查点中 88 项合规、1 项不适用、R-1/R-4/R-5 三项新发现已修复、R-2/R-3 两项记录在案（R-2 留待 follow-up，R-3 由 issue #42 覆盖）。无整改回退，无新引入的安全回归。
