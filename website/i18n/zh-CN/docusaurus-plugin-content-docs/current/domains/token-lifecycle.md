---
sidebar_position: 5
---

# 令牌生命周期

fulla 中令牌如何签发、存活与消亡：三类令牌、真正落库的是什么、刷新轮换如何
检测窃取、以及控制生命周期的每个开关。存储 schema 见
[数据与持久化](../architecture/data-persistence.md)；HTTP 契约见
[API 参考](api-reference.md)与 [ADR-0004](../adr/ADR-0004.md)。

## 1. 三类令牌，三种形态

| 令牌 | 形态 | 校验方式 | 默认生命周期 |
|---|---|---|---|
| Access token | **不透明随机串**（`generateSecureToken`） | 服务端状态（introspection / userinfo）——不是自包含 JWT | 3600 秒（`access_token_ttl`） |
| Refresh token | 不透明随机串 + **家族 id** | 服务端状态；每次使用即轮换 | 30 天（可配置） |
| id_token | **RS256 JWT**，经 JWKS 签名（`kid` 发布于 `/.well-known/jwks.json`） | 客户端侧验签（标准 OIDC） | 与 access token 相同；仅 `openid` scope 时签发 |

设计理由（[ADR-0004](../adr/ADR-0004.md)）：不透明 access token 让服务器保持
完全控制——撤销即时且有状态，密钥泄露的令牌不会活过服务器端的记录。JWT
能力保留给协议要求它的 OIDC `id_token`。

一个常见混淆：令牌响应中携带的 **roles** 属于 JSON 信封
（`"roles": [...]`）与 id_token claims——不透明 access token 本身不携带任何
需要解码的内容。

## 2. 签发路径

所有授权类型汇合到 `TokenService`（libs/oauth2）：

| 授权类型 | 签发 | 说明 |
|---|---|---|
| `authorization_code`（+ PKCE，PUBLIC 客户端强制） | access + refresh（含 `openid` 时加 id_token） | 授权码单次使用、原子消费（见 §5） |
| `refresh_token` | 新 access + **新** refresh | 旧 refresh 被撤销；家族 id 继承（§4） |
| `client_credentials` | 仅 access（M2M，无用户） | 仅 CONFIDENTIAL 客户端 |
| `device_code` | 用户批准后签发 access + refresh | 按 RFC 8628 轮询 |

每个响应中的 `expires_in` 宣告的是**配置的** access-token 生命周期——不是
硬编码 3600（RFC 6749 §5.1）。

## 3. 落库的是什么（以及不落库什么）

- access 与 refresh token 仅以 **SHA-256 哈希**持久化（写入仓储前经
  `hashToken()`）——拖库拿不到可用凭据（[ADR-0004](../adr/ADR-0004.md)）。
- refresh token 行额外存储 `token_family` id、关联的 access-token 哈希、
  `revoked`/`revoked_at`/`revoked_by`。
- 过期行由 `OAuth2CleanupService`（`cleanup_interval_seconds`，默认 3600 秒）
  清理——Postgres 定期 DELETE、Redis 历史上靠 TTL、memory 定期扫描。

## 4. 刷新轮换、重用检测与家族撤销

每次刷新签发**新的** refresh token 并继承同一 `token_family`（V008）。若已被
撤销的 refresh token 再次被提交，视为窃取：服务器**级联撤销该家族全部令牌**
——攻击者与合法用户都需重新认证。完整威胁模型推演（含时序图）见
[安全架构 §7](../architecture/security-architecture.md)。

## 5. 授权码单次使用

`consumeAuthCode` 在各后端均原子：Postgres 用
`UPDATE ... WHERE consumed = false ... RETURNING`、Redis 历史上用 Lua 脚本、
memory 用互斥锁——竞态中被重放的窃取授权码必然失败。该契约由
`GrantRepositoryContractTest`（`ctest -L Contract`）跨三实现强制守护。

## 6. 撤销面

| 面 | 粒度 |
|---|---|
| `POST /oauth2/revoke`（RFC 7009） | 被提交的令牌 |
| 管理 API `DELETE /api/admin/tokens/{prefix}` | 按令牌前缀 |
| 管理令牌面 | 按**客户端**或按**用户**撤销——清掉该主体的全部令牌 |

撤销是有状态且即时的：下一次携带已撤销令牌的 introspection 或 userinfo 调用
即返回 `{"active": false}` / 401。

## 7. 与缓存的联动

启用 Redis L2 缓存（`cache.enabled`）后，token 与 client 读路径先命中
`fulla:cache:*` 再回源 Postgres。每条写路径（签发、刷新、撤销）经**延迟双删**
失效（立即 DEL + 约 200ms 后二次 DEL，
`cache.invalidation_double_delete_delay_ms`），封住并发读者在写与首删之间回填
旧值的竞态。二次删除失败计入
`fulla_cache_invalidation_failures_total`——见[可观测性](../operate/observability.md)。
细节见[数据与持久化 · 缓存一致性](../architecture/data-persistence.md)。

## 8. 配置参考

| 键 | 默认 | 含义 |
|---|---|---|
| `access_token_ttl` | 3600 | access-token 生命周期（秒）；在 `expires_in` 中宣告 |
| refresh-token TTL | 30 天 | refresh-token 生命周期 |
| `cleanup_interval_seconds` | 3600 | `OAuth2CleanupService` 清理过期行的周期 |
| `cache.enabled` / `cache.ttl_seconds` | false / — | token/client 读的 L2 缓存（[配置指南](../operate/configuration-guide.md)） |
