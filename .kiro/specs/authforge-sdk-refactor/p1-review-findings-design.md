# P1 评审问题点修复设计

> 对 `评审问题点有效性分析报告.md` 中 **P1 三项**（#3 device code 竞态、#5
> device 流客户端认证、#6 TTL 硬编码）的实现设计，附带 **P 级纯注释改进**
> （#4 `[this]` 捕获、#8 validateAccessToken self、#9 JWKS 空响应缓存头）。
>
> P0（#1 PKCE/nonce、#2 client_credentials scope）已在前序提交修复，不在本设计范围。
> P2（#10 issuedAt）留待后续单独处理。

## 背景与决策

| 问题 | 评审级别 | 修复决策（已确认） |
|------|---------|------------------|
| #6 TTL 硬编码（6 处，含报告新发现的 id_token exp、device refresh 30 天） | High | 统一改用配置注入值；默认配置下行为不变 |
| #5 device 流客户端认证（按 client_type 区分） | High | **方案 1**：CONFIDENTIAL 兑换强制 `validateClient`；PUBLIC 维持现状 |
| #3 device code 兑换竞态（check-then-delete 非原子） | High（建议从 Critical 降级） | **原子条件 UPDATE**：`status='approved' → 'consumed'`，按影响行数判定 |
| #4 `[this]` 捕获（无效，不修代码） | — | 仅加注释引用 AuthorizationFilter 的论证 |
| #8 validateAccessToken 缺 self（无效，不修代码） | — | 仅加注释说明回调不引用 this |
| #9 JWKS 空响应无缓存头（基本无效，且评审方向有害） | Info | 仅加注释说明为何空响应不缓存 |

**关键事实**：
- 所有 4 份配置（`config{,.dev,.ci,.prod}.json`）均为 `access_token_ttl=3600`、
  `refresh_token_ttl=2592000`，与现有硬编码字面量完全一致 ⇒ 默认配置下行为不变。
- `OAuth2Plugin` 已持有 `accessTokenTtl_`/`refreshTokenTtl_`（private 成员，
  `initAndStart()` 中从配置读取一次），但未对外暴露 ⇒ 需新增公开访问器。
- `TokenService` 构造时已注入 `accessTokenTtl_`/`refreshTokenTtl_`。
- device code 操作**不走** `ITokenRepository`，直接用 `Mapper<Oauth2DeviceCodes>`
  + `drogon_model::oauth2_db::Oauth2DeviceCodes`。

---

## 修复 1：#6 — TTL 硬编码统一改用配置值

### 1.1 `TokenService.cc`（3 处，已有 TTL 成员）

| 行 | 现状 | 改为 |
|----|------|------|
| L274 | `json["expires_in"] = (Json::Int64)(3600);`（auth_code 签发） | `(Json::Int64)(accessTokenTtl_)` |
| L288 | `idTokenClaims["exp"] = (Json::Int64)(now + 3600);` | `(Json::Int64)(now + accessTokenTtl_)` |
| L399 | `json["expires_in"] = (Json::Int64)3600;`（refresh 签发） | `(Json::Int64)accessTokenTtl_` |

> 设计取舍：id_token 的 `exp` 跟随 access token TTL（不引入独立的
> `id_token_ttl` 配置项 —— YAGNI；OIDC Core §2 只要求 exp 是真实过期时刻，
> 跟随 access token TTL 即满足）。

### 1.2 `OAuth2Plugin.h` — 新增公开 TTL 访问器

成员 `accessTokenTtl_`/`refreshTokenTtl_` 已存在（L426-427），仅是 private。
在 "Service Accessors" 区域新增：
```cpp
long long getAccessTokenTtl() const noexcept { return accessTokenTtl_; }
long long getRefreshTokenTtl() const noexcept { return refreshTokenTtl_; }
```
（`initAndStart()` 在请求处理前完成写入，happens-before 保证线程安全 ——
与现有成员注释一致。）

### 1.3 `TokenEndpointController.cc`（5 处，从 plugin 取值）

`client_credentials` 分支：
- L828 `token.expiresAt = now + 3600;` → `now + resolvePlugin()->getAccessTokenTtl()`
- L836 `json["expires_in"] = 3600;` → `resolvePlugin()->getAccessTokenTtl()`

`device_code` 分支：
- L1033 `accessToken.expiresAt = now + 3600;` → `now + getAccessTokenTtl()`
- L1041 `refreshToken.expiresAt = now + (3600 * 24 * 30);` → `now + getRefreshTokenTtl()`
- L1071 `json["expires_in"] = 3600;` → `getAccessTokenTtl()`

（在分支入口取一次 `auto plugin = resolvePlugin();` 复用，避免重复调用。）

---

## 修复 2：#5 — device 流按 client_type 认证

在 device_code 分支（`TokenEndpointController.cc` L852–877 区域，校验
`device_code`/`client_id` 非空之后、现有 `Mapper::findBy` 之前）插入异步
客户端认证：

```
plugin->getClient(clientId, [&cb](client) {
  if (!client)              → invalid_client (401)
  if (client->clientType == CONFIDENTIAL)
      plugin->validateClient(clientId, clientSecret, [&](valid) {
          if (!valid)       → invalid_client (401)
          else              → 进入 device code 查询续体（现有 findBy）
      });
  else  // PUBLIC
      → 进入 device code 查询续体（现有 findBy），行为完全不变
});
```

- CONFIDENTIAL + 空/错 secret → `validateClient` 内部 hash 比对失败 → 拒绝。
- PUBLIC 路径：仅 client_id 与 device code 行绑定（现状）。
- 现有的 `Mapper<Oauth2DeviceCodes>` 查询逻辑整体移入上述成功续体（`clientSecret`
  需在 `token()` 顶部已解析；当前 device 分支未读 `client_secret`，需补读：
  Basic Auth 已在 L576-599 解析，POST body 已在 L601-613 覆盖，故 `clientSecret`
  变量已就绪 —— 直接复用）。

---

## 修复 3：#3 — device code 兑换竞态（原子消费）

把"签发后 deleteBy"改为"签发前原子条件 UPDATE"。

在现有 `findBy` 读到 `status` 并走完 pending/denied/expired 早返回分支后，
到达"判定为 approved 准备签发"处，**不再直接信任内存中的 `status=="approved"` 读**，
而是执行：

```sql
UPDATE oauth2_device_codes
SET status='consumed'
WHERE device_code_hash = $1 AND status='approved'
RETURNING device_code_hash
```

通过 `execSqlAsync`（裸 SQL，**附注释说明豁免理由**：Mapper 无法表达
`WHERE status='approved'` 的条件式原子状态转移；采用 `UPDATE ... RETURNING`
形态以契合 `.claude/rules/db-operations.md` 明列的 raw-SQL 豁免项之一）。
按回调返回的结果集行数分支：

- **结果集非空（≥1 行）** → 占用成功，继续签发（该行已 `consumed`，并发失败方
  随后的 UPDATE 命中 0 行、返回空结果集）。
- **结果集为空（0 行）** → 返回 `invalid_grant`
  （"device code already consumed or no longer approved"）。**Fail-closed**
  （安全侧偏差，可接受）。

错误回调 → `server_error`（500）。删除原有的"签发后 deleteBy"代码块（L1050-1066）。

> 规则对齐说明：原本考虑用"普通 `UPDATE` + affected 行数"判定占用，但
> `db-operations.md` 的 raw-SQL 豁免明列的是 `UPDATE ... RETURNING`（非
> 普通 UPDATE）。故采用 `RETURNING` 形态——`RETURNING` 的结果集是否非空即
> 原子占用的成败信号，语义等价且合规。

状态判断顺序：`pending` → `authorization_pending`；`denied` → `access_denied`；
过期 → `expired_token`；**然后**原子消费（替代原先直接信任 `status=="approved"`）。
`status='consumed'` 行保留作审计痕迹（不再删除）。

> Mapper 构造防护：按 `.claude/rules/db-operations.md` 要求，新的
> `execSqlAsync` 调用及任何 `Mapper<...>(dbClient)` 构造所在代码块须置于
> `try/catch`，catch 中调用 `(*sharedCb)(errorResult)` 上报失败（禁止仅 LOG 后 return）。

---

## 注释改进（不修代码逻辑）

### #4 `[this]` 捕获（`TokenEndpointController.cc` userInfo L1159）
在 `[this, userId, callback]` 处加注释，引用 `AuthorizationFilter.cc` L90-95
已论证的"Drogon 控制器为进程级单例，`this` 跨异步回调存活"结论，防止后续评审重复报告。

### #8 validateAccessToken self（`TokenService.cc` L420-421）
在 `tokens_->getAccessToken(... [callback] ...)` 处加注释：回调体仅用参数 `t`
与自由函数 `nowSeconds()`，不引用 `this`/成员，故故意不捕获 `self`；提醒未来若往
回调里加成员访问需改为捕获 `shared_from_this()`。

### #9 JWKS 空响应（`DiscoveryController.cc` L230-237）
在空分支（`!plugin || !getJwkManager()`）加注释：该分支生产不可达（jwkManager
在 `initAndStart` 无条件构造），即便可达，空 keys 集合**不应**补 `Cache-Control:
max-age`（会让下游 RP/网关缓存"无验签密钥"一小时，服务恢复后验签持续失败）；
当前"无缓存头"是正确的（HTTP 默认启发式对无验证器 JSON 响应基本不缓存）。

---

## 测试

沿用 `tests/integration/token/` 的 live-server 模式（单一 Drogon 测试二进制，
`OAUTH2_MEMORY_TESTS_ONLY=ON` 下由 CMake 跳过，Linux CI 跑 PG+Redis）。
照搬 `ClientCredentialsScopeValidationTest.cc` 的 `serverReachable()` 守卫 +
seeding 模式，服务器未起时测试干净跳过。

1. **`TokenExpiryTtlConfigTest.cc`**（#6）：
   - 断言 client_credentials / authorization_code / refresh 三条路径返回的
     `expires_in` **等于服务器实际加载的 `access_token_ttl` 配置值**
     （从 `config.dev.json` 读取预期，而非硬编码 3600 —— 这样改配置测试仍正确）。
   - *局限*：默认配置全是 3600，无法直接证明"非默认值生效"；但断言绑定配置值
     能防止未来回归到字面量。会在测试注释中如实说明此局限。

2. **`DeviceCodeClientAuthTest.cc`**（#5）：
   - CONFIDENTIAL 客户端无 secret 兑换 approved device_code → `invalid_client`（401）。
   - CONFIDENTIAL 带正确 secret → 成功。
   - PUBLIC 客户端仅 client_id → 成功。
   - 需 seed 一个 PUBLIC 设备流客户端 + 一个 CONFIDENTIAL 设备流客户端及对应 device_code 行。

3. **`DeviceCodeRaceConditionTest.cc`**（#3）：
   - 同一 approved device_code **两次并发**兑换 → 恰好一次成功（拿到 token），
     另一次 `invalid_grant`。
   - 验证 fail-closed：先成功的 UPDATE 把行置 `consumed`，后到的 UPDATE 命中 0 行。

> seed 依赖：device_code 测试需要向 `oauth2_device_codes` 插入 `approved` 行
> （含正确 `device_code_hash`）。用 `TestBase.h` 的 `TestTransaction`（RAII 回滚）
> 包裹，使每条测试回滚自身 DB 变更。

---

## 验证

- 构建：`./manage.sh build-backend`（Linux）或 Windows Conan 构建
  （`scripts/backend/build.bat`）。
- 单元/集成测试：`./manage.sh test-backend`；按标签 `ctest -R Integration`。
- 无外部 DB 构建（Windows smoke）：`-DOAUTH2_MEMORY_TESTS_ONLY=ON`（集成测试被跳过，
  确保编译通过 + 现有 unit 契约测试不回归）。
- 端点 API 测试：`scripts/backend/test-oauth2-endpoints.{sh,ps1}`（device/code 流在 17 项核心内）。

## 风险与回滚

- #6：默认配置下零行为变化；改配置后客户端收到的 `expires_in` 才与真实寿命一致（更正确）。
- #5：现有 PUBLIC 设备流客户端行为不变；CONFIDENTIAL 设备流客户端此前无 secret 也能兑换，
  修复后必须带 secret —— **属于收紧**，符合 RFC。需确认现有 seed/集成环境中的
  CONFIDENTIAL 设备流客户端（若有）携带正确 secret。
- #3：fail-closed —— 极端情况下 `saveTokenPair` 失败会"烧掉"device code（用户需重新授权），
  这是可接受的安全侧偏差。
- 改动集中在 `TokenService.cc`、`OAuth2Plugin.h`、`TokenEndpointController.cc`、
  `DiscoveryController.cc`、`OAuth2AuthFilter`/`AuthorizationFilter`（仅注释），
  单文件可回滚。
