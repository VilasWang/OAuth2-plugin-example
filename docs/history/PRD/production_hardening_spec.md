# OAuth2 Plugin 生产级加固规范 (Production Hardening Spec)

> **版本**: v1.0  
> **创建日期**: 2026-05-19  
> **目标**: 将 authforge 从"演示级 IdP"升级为可独立部署的生产级授权服务器  
> **决策**: Access Token 保持 Opaque 模式（不引入 JWT），通过 Introspection 端点供资源服务器验证

---

## 一、总体目标

将现有 OAuth2Plugin + OAuth2Server 加固为满足以下标准的独立授权服务器：

1. 协议合规：完整支持 OAuth 2.1 核心 + OpenID Connect Core 1.0
2. 安全基线：满足 OWASP OAuth Security Best Practices
3. 运营就绪：具备管理后台、审计、监控、schema 迁移能力
4. 身份生命周期：覆盖注册→验证→登录→MFA→密码重置→注销全流程

---

## 二、功能需求清单

### P0 — 不做不能上线（安全底线）

#### P0-1: Token 安全加固

**需求描述**:
- Access Token / Refresh Token / Auth Code 改用密码学安全随机数（≥256 bit, base64url 编码）替代 UUID
- 数据库中 token 字段存储 `SHA-256(token)` 而非明文，查询时先 hash 再匹配
- 对外返回原始 token，内部只持有 hash

**验收标准**:
- [ ] `drogon::utils::getUuid()` 全部替换为 `drogon::utils::secureRandomString(32)` 或等价实现
- [ ] `oauth2_access_tokens.token` / `oauth2_refresh_tokens.token` / `oauth2_codes.code` 列存储 SHA-256 hex
- [ ] 现有测试全部通过，E2E 流程正常

---

#### P0-2: 密码哈希升级

**需求描述**:
- 将 `SHA-256 + salt` 替换为 **Argon2id**（首选）或 bcrypt
- 支持渐进式迁移：验证时识别旧 hash 格式，验证通过后自动升级为新格式
- 参数可配置（memory cost, time cost, parallelism）

**验收标准**:
- [ ] 新注册用户使用 Argon2id
- [ ] 旧用户登录成功后 password_hash 自动升级
- [ ] `users.password_hash` 字段长度扩展至 256（Argon2id 输出较长）
- [ ] 配置项 `auth.password_hash_algorithm` 支持 `argon2id` / `bcrypt` / `sha256`（向后兼容）

---

#### P0-3: Refresh Token Reuse Detection（令牌族追踪）

**需求描述**:
- 引入 `refresh_token_family_id` 字段，同一授权码颁发的所有 RT 共享同一 family
- 当检测到已撤销的 RT 被再次使用时，**级联撤销该 family 下所有 token**（access + refresh）
- 刷新操作使用原子 CAS：`UPDATE ... WHERE token=$1 AND revoked=false RETURNING *`

**验收标准**:
- [ ] `oauth2_refresh_tokens` 新增 `family_id VARCHAR(64) NOT NULL` 列
- [ ] 并发刷新只有一个成功，另一个收到 `invalid_grant`
- [ ] 已撤销 RT 被重用时，同 family 所有 token 被撤销 + 返回 `invalid_grant`
- [ ] 审计日志记录 "refresh_token_reuse_detected" 事件

---

#### P0-4: Auth Code 原子消费 + Token 颁发事务化

**需求描述**:
- `consumeAuthCode` 改为单条 SQL：`UPDATE oauth2_codes SET used=true WHERE code=$1 AND used=false RETURNING *`
- `exchangeCodeForToken` 中 saveAccessToken + saveRefreshToken 放入同一数据库事务

**验收标准**:
- [ ] 并发 token 请求使用同一 code，只有一个成功
- [ ] 事务中任一步失败，整体回滚，不留孤儿 token
- [ ] 性能测试：100 并发兑换同一 code，成功恰好 1 次

---

#### P0-5: Subject 标识安全化

**需求描述**:
- `users` 表新增 `public_sub UUID DEFAULT gen_random_uuid()` 列
- OAuth2 流程中 subject 使用 `public_sub` 而非自增 `id`
- 对外接口（userinfo、introspect、id_token）中 `sub` 字段使用 UUID

**验收标准**:
- [ ] `/oauth2/userinfo` 返回的 `sub` 是 UUID 格式
- [ ] 自增 ID 不出现在任何对外响应中
- [ ] 现有 subject_mappings 表兼容新格式

---

#### P0-6: 生产 Schema 安全化

**需求描述**:
- 拆分 SQL 为 `schema/` (DDL, idempotent) 和 `seed/` (dev 数据)
- 生产 schema 禁止 `DROP TABLE`，使用 `CREATE TABLE IF NOT EXISTS` + `ALTER TABLE ... ADD COLUMN IF NOT EXISTS`
- 引入 schema version 表 + 迁移工具（推荐 golang-migrate 或自研简易版）
- 移除生产 seed 中的默认 admin/admin 账号和 vue-client

**验收标准**:
- [ ] `schema/` 目录下的 SQL 可重复执行不报错
- [ ] 新增 `schema_migrations` 表记录已执行版本
- [ ] CI 中验证 schema 幂等性
- [ ] 生产配置不包含任何默认凭据

---

#### P0-7: 配置安全与 HTTPS 强制

**需求描述**:
- 所有敏感配置（DB 密码、Redis 密码、Client Secret）通过环境变量注入
- `ConfigManager::validate` 增加生产模式校验：
  - issuer 必须是 `https://`
  - DB/Redis 密码不能是默认值
  - redirect_uri 不能包含 `localhost`（生产环境）
- metadata endpoint 的 issuer 与配置严格一致

**验收标准**:
- [ ] `config.prod.json` 中无明文密码（全部 `${ENV_VAR}` 占位）
- [ ] 启动时若 issuer 非 https 且 env=production，拒绝启动
- [ ] metadata 返回的 issuer 与 token introspection 的 iss 一致

---

#### P0-8: AuthorizationFilter 默认拒绝

**需求描述**:
- `AuthorizationFilter::checkAccess` 当路径匹配不到任何规则时，默认返回 403
- 新增 `public_paths` 配置项，显式声明无需鉴权的路径

**验收标准**:
- [ ] 未配置规则的路径返回 403
- [ ] `public_paths` 中的路径不经过 RBAC 检查
- [ ] 现有测试适配新行为

---

#### P0-9: Health Check 真实探活

**需求描述**:
- `/health/live`：进程存活（始终 200）
- `/health/ready`：DB `SELECT 1` + Redis `PING` 均成功才 200，否则 503
- 分离 liveness 和 readiness 供 K8s 使用

**验收标准**:
- [ ] DB 断开时 `/health/ready` 返回 503
- [ ] Redis 断开时 `/health/ready` 返回 503（降级模式可配置）
- [ ] `/health/live` 始终 200

---

#### P0-10: handleFirstTimeLogin 修复

**需求描述**:
- 移除 `static int32_t nextUserId = 1000`
- 改为从 `users` 表 INSERT 获取真实自增 ID（或使用 DB sequence）

**验收标准**:
- [ ] 多进程部署不会产生 ID 冲突
- [ ] 新用户首次第三方登录后 `users` 表有对应记录

---

### P1 — 独立 IdP 可用（功能完整性）

#### P1-1: OpenID Connect Core

**需求描述**:
- 实现 `id_token` 签发（RS256，密钥对由配置管理）
- `/.well-known/openid-configuration` 返回完整 OIDC Discovery 文档
- `/.well-known/jwks.json` 暴露公钥
- 支持 `nonce` 参数（防重放）
- `/oauth2/userinfo` 返回标准 OIDC claims（sub, name, email, email_verified, picture 等）
- `response_type=code` 时 token 响应包含 `id_token`

**验收标准**:
- [ ] 使用标准 OIDC 客户端库（如 oidc-client-ts）可完成完整流程
- [ ] id_token 可被 `/.well-known/jwks.json` 中的公钥验证
- [ ] nonce 不匹配时拒绝
- [ ] Conformance test（oidc-provider-conformance-tests）核心用例通过

---

#### P1-2: Client Credentials Grant（M2M）

**需求描述**:
- `POST /oauth2/token` 支持 `grant_type=client_credentials`
- 仅限 CONFIDENTIAL 类型客户端
- 颁发 access_token（无 refresh_token），scope 受 client 允许范围限制

**验收标准**:
- [ ] M2M 客户端可直接获取 access_token
- [ ] PUBLIC 客户端使用此 grant 返回 `unauthorized_client`
- [ ] metadata 中 `grant_types_supported` 包含 `client_credentials`

---

#### P1-3: 密码重置流程

**需求描述**:
- `POST /api/password-reset/request`：生成一次性 token（≥256 bit, 15 分钟有效），发送邮件
- `POST /api/password-reset/confirm`：验证 token + 设置新密码
- 重置成功后撤销该用户所有 access_token 和 refresh_token
- 邮件发送抽象为接口（支持 SMTP / SendGrid / 阿里云邮件推送）

**验收标准**:
- [ ] token 过期后不可用
- [ ] token 使用后不可重用
- [ ] 重置后旧 token 全部失效
- [ ] 邮件模板可配置

---

#### P1-4: 邮箱验证

**需求描述**:
- 注册后发送验证邮件，含一次性链接
- 未验证用户不能通过 OAuth2 授权流程（可配置是否强制）
- `users` 表新增 `email_verified BOOLEAN DEFAULT FALSE`

**验收标准**:
- [ ] 未验证用户登录时返回 "email_not_verified" 错误
- [ ] 验证链接点击后 `email_verified = true`
- [ ] 可配置 `auth.require_email_verification: true/false`

---

#### P1-5: MFA（TOTP）

**需求描述**:
- 支持 TOTP（RFC 6238）作为第二因素
- 用户可在"安全设置"中启用/禁用
- 启用时生成 secret + 备份码（一次性，10 个）
- 登录流程：密码验证通过 → 要求输入 TOTP → 颁发 auth code
- 可按 client 配置是否强制 MFA

**验收标准**:
- [ ] Google Authenticator / Authy 可扫码绑定
- [ ] TOTP 错误时拒绝登录
- [ ] 备份码可用且一次性
- [ ] `users` 表新增 `mfa_secret`, `mfa_enabled`, `mfa_backup_codes` 字段

---

#### P1-6: 结构化审计日志

**需求描述**:
- 新增 `audit_logs` 表：`id, timestamp, actor_type, actor_id, action, target_type, target_id, outcome, ip, user_agent, request_id, details_json`
- 关键事件必须记录：login_success, login_failure, token_issued, token_revoked, token_refreshed, refresh_token_reuse, password_changed, mfa_enabled, client_created, consent_granted, consent_revoked, admin_action
- 异步写入（不阻塞主流程）
- 保留期可配置（默认 90 天）

**验收标准**:
- [ ] 每次登录在 audit_logs 中有记录
- [ ] 审计日志不可通过普通 API 删除
- [ ] 提供 Admin API 查询审计日志（分页 + 过滤）
- [ ] 清理任务按保留期自动归档/删除

---

#### P1-7: Admin REST API

**需求描述**:
- Client 管理：CRUD + 重置 secret + 启用/禁用
- Scope 管理：CRUD + 关联 client
- User 管理：列表 + 详情 + 禁用 + 重置密码 + 分配角色
- Role/Permission 管理：CRUD + 关联
- Consent 管理：查看 + 撤销
- 所有操作需 admin 角色 + 审计日志

**验收标准**:
- [ ] Swagger/OpenAPI 文档覆盖所有 Admin 端点
- [ ] 非 admin 角色访问返回 403
- [ ] 每个操作在 audit_logs 中有记录

---

#### P1-8: 用户自服务

**需求描述**:
- `GET /api/me`：当前用户信息
- `PUT /api/me/password`：修改密码（需旧密码验证）
- `GET /api/me/sessions`：查看活跃会话
- `DELETE /api/me/sessions/:id`：终止指定会话
- `GET /api/me/authorized-apps`：已授权的第三方应用列表
- `DELETE /api/me/authorized-apps/:clientId`：撤销对某 client 的授权（删 consent + 撤销 token）
- `DELETE /api/me`：注销账号（软删除 + 撤销所有 token + 30 天后硬删）

**验收标准**:
- [ ] 修改密码后旧 token 全部失效
- [ ] 撤销授权后该 client 的 token 立即失效
- [ ] 注销后无法登录

---

#### P1-9: PKCE 对 PUBLIC Client 强制

**需求描述**:
- 当 `client_type = PUBLIC` 时，`/oauth2/authorize` 必须携带 `code_challenge`
- 缺少时返回 `invalid_request` + 描述信息
- CONFIDENTIAL 客户端 PKCE 可选（推荐但不强制）

**验收标准**:
- [ ] PUBLIC client 不带 code_challenge 时返回 400
- [ ] CONFIDENTIAL client 不带 code_challenge 时正常通过
- [ ] metadata 中 `code_challenge_methods_supported` 包含 `S256`

---

#### P1-10: CachedStorage 写后失效

**需求描述**:
- 所有写操作（saveClient, revokeToken, saveConsent, revokeConsent 等）完成后主动 evict 对应的 L1 + L2 缓存
- Client 信息变更时广播失效（多实例场景用 Redis Pub/Sub）

**验收标准**:
- [ ] 修改 client redirect_uri 后立即生效（无需等缓存过期）
- [ ] 撤销 token 后缓存中不再命中
- [ ] 多实例部署时一个实例的写操作能通知其他实例失效缓存

---

#### P1-11: Schema Migration 工具

**需求描述**:
- 集成轻量级迁移工具（推荐 golang-migrate CLI 或自研 C++ 版本）
- 迁移文件按版本号排序：`V001__initial_schema.sql`, `V002__add_mfa_fields.sql` ...
- 启动时自动检测并执行未应用的迁移
- 支持 dry-run 模式

**验收标准**:
- [ ] 从空库启动可自动建表
- [ ] 已有库启动只执行增量迁移
- [ ] `schema_migrations` 表记录每次迁移的版本、时间、checksum
- [ ] CI 中验证迁移脚本可正向执行

---

### P2 — 企业级增强（按需实施）

#### P2-1: Dynamic Client Registration (RFC 7591/7592)
- `POST /oauth2/register`：注册新 client
- `GET/PUT/DELETE /oauth2/register/:client_id`：管理已注册 client
- 需要 initial_access_token 或 admin 权限

#### P2-2: Device Authorization Grant (RFC 8628)
- `POST /oauth2/device_authorization`
- `POST /oauth2/token` with `grant_type=urn:ietf:params:oauth:grant-type:device_code`
- 用户码 + 验证 URI + 轮询机制

#### P2-3: Backchannel Logout (OIDC)
- 支持 `backchannel_logout_uri` 配置
- 用户登出时向所有已授权 RP 发送 logout_token

#### P2-4: 多租户 (Organization)
- `organizations` 表 + tenant 隔离
- 每个 tenant 独立的 client、scope、branding、用户池
- 支持 tenant-aware issuer

#### P2-5: WebAuthn / Passkey
- 替代或补充 TOTP 的无密码认证
- 支持 platform authenticator + roaming authenticator

#### P2-6: 账号锁定与渐进退避
- 按用户维度的失败计数（独立于 IP 维度的 Hodor）
- N 次失败后锁定 M 分钟，指数退避
- 解锁方式：等待超时 / 邮件验证 / 管理员手动

#### P2-7: 分布式 Cleanup 锁
- 多实例部署时 CleanupService 使用 Redis 分布式锁或 PG advisory lock
- 保证同一时刻只有一个实例执行清理

#### P2-8: Token 表分区与归档
- `oauth2_access_tokens` 按月分区
- 过期 token 归档到冷存储
- 查询只扫描活跃分区

---

## 三、非功能需求

### 性能
- Token 验证（含缓存命中）p99 < 5ms
- 授权码流程端到端 p99 < 200ms
- 支持 10,000 QPS token 验证（单实例，4 核）

### 可用性
- 单点故障不导致服务不可用（DB 主从 + Redis Sentinel）
- 优雅降级：Redis 不可用时回退到 DB 直查

### 安全
- 所有对外通信 TLS 1.2+
- 密钥轮转支持（OIDC signing key rotation）
- 敏感日志脱敏（token 只显示前 8 字符）

### 可观测性
- 结构化日志（JSON 格式）
- Prometheus 指标：token_issued_total, token_validated_total, login_attempts_total, active_sessions_gauge
- 请求级 correlation ID 贯穿

---

## 四、实施路线图

```
Phase 1 (P0): 安全底线        预计 2-3 周
  ├── P0-1 ~ P0-4: Token/密码/事务安全
  ├── P0-5 ~ P0-6: Subject + Schema
  ├── P0-7 ~ P0-8: 配置 + 鉴权默认拒绝
  └── P0-9 ~ P0-10: 健康检查 + ID 修复

Phase 2 (P1): 功能完整        预计 4-6 周
  ├── P1-1: OIDC (id_token + discovery + JWKS)
  ├── P1-2: Client Credentials
  ├── P1-3 ~ P1-5: 密码重置 + 邮箱验证 + MFA
  ├── P1-6 ~ P1-7: 审计 + Admin API
  ├── P1-8: 用户自服务
  └── P1-9 ~ P1-11: PKCE 强制 + 缓存失效 + Migration

Phase 3 (P2): 企业增强        按需
  └── 动态注册 / Device Code / Backchannel Logout / 多租户 / WebAuthn
```

---

## 五、技术决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| Access Token 格式 | **Opaque (随机字符串)** | 即时撤销、无密钥管理、introspection 已实现；JWT 仅在跨服务无回调场景有优势，当前架构不需要 |
| 密码哈希 | **Argon2id** | 抗 GPU/ASIC，OWASP 推荐，参数可调 |
| Schema 迁移 | **版本化 SQL + 自动执行** | 轻量、无额外运行时依赖、CI 友好 |
| id_token 签名 | **RS256** | 公钥可分发，支持密钥轮转，OIDC 生态标准 |
| 审计日志存储 | **PostgreSQL 独立表 + 异步写入** | 与业务同库简化部署，异步不阻塞主流程 |
| 缓存失效 | **写后 evict + Redis Pub/Sub** | 强一致、多实例友好 |

---

## 六、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Argon2id 引入新依赖 | 编译复杂度增加 | 使用 libsodium（已广泛可用）或 argon2 reference impl |
| OIDC id_token 需要 RSA 密钥管理 | 运维复杂度 | 支持从文件/环境变量加载 PEM；提供密钥生成脚本 |
| Schema 迁移失败导致启动阻塞 | 服务不可用 | dry-run 模式 + CI 预验证 + 回滚脚本 |
| Refresh Token Family 级联撤销影响正常用户 | 用户体验 | 仅在检测到 reuse 时触发；正常轮转不受影响 |
| 多实例缓存失效延迟 | 短暂不一致 | Redis Pub/Sub 延迟 < 10ms，可接受 |

---

## 七、与现有代码的兼容性

- **向后兼容**：P0 阶段的 token hash 化需要迁移期（双读：先查 hash，miss 后查明文，命中则升级）
- **API 兼容**：所有现有端点保持不变，新增端点不影响旧客户端
- **配置兼容**：新增配置项均有合理默认值，旧 config.json 可继续使用（dev 模式）
- **数据库兼容**：所有 schema 变更通过 `ALTER TABLE ... ADD COLUMN IF NOT EXISTS`，不破坏现有数据
