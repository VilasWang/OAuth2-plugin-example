# OAuth2 安全架构设计 (Security Architecture)

本文档详细描述了本系统的安全威胁模型及相应的防御机制，涵盖 Token 生命周期管理、加密存储及防攻击策略。

## 1. 威胁模型与防御 (Threat Model)

| 威胁类型 | 描述 | 防御机制 | 对应文档 |
|----------|------|----------|----------|
| **Replay Attack** (重放攻击) | 攻击者截获 Auth Code 并在合法客户端之前或之后尝试兑换。 | **Atomic Consume** (原子消费) + **One-Time Use Enforcement**。 | [Data Consistency](data-consistency.md) |
| **Credential Leakage** (凭据泄露) | 数据库被拖库导致 Client Secret 泄露。 | **SHA256 Salted Hash**。数据库仅存 Hash 值，绝不存明文。 | [Data Persistence](data-persistence.md) |
| **Token Theft** (令牌窃取) | Access Token 被截获。 | **Short-lived Token** (1小时) + **Refresh Token Rotation** (轮转机制)。 | 本文档 |
| **CSRF** | 攻击者诱导用户进行非预期的授权。 | 强制校验 **state** 参数 (推荐客户端实现)。 | [API Reference](api-reference.md) |

## 2. Token 生命周期管理

### 2.1 Access Token

- **有效期**: 1小时。
- **用途**: 访问受保护资源（如 `/userinfo`）。
- **验证**: 无状态（JWT）或有状态（DB 查询）。本项目采用 **有状态** 验证，支持即时撤销。

### 2.2 Refresh Token

- **有效期**: 30天。
- **用途**: 在 Access Token 过期后换取新 Token。
- **安全机制: 轮转 (Rotation)**
  - 每次刷新时，不仅颁发新的 Access Token，也会 **颁发新的 Refresh Token**。
  - 旧的 Refresh Token 立即失效。
  - **检测机制**: 如果检测到旧 Refresh Token 被再次使用，系统可视为 Token 泄露，并级联撤销该 `token_family` 下的所有关联 Token（已实现，见 §7.2）。

## 3. 密钥管理 (Secrets Management)

### 3.1 Client Secrets

- **存储**: `sha256(secret + salt)`
- **传输**: 仅在 POST body 中通过 HTTPS 传输。

### 3.2 配置文件

- 敏感信息（如 DB 密码、Redis 密码）建议通过 **环境变量** 注入，而非硬编码在 `config.json` 中。
- 生产环境部署时，应确保配置文件权限严格限制。

## 4. 最佳实践建议

- **HTTPS**: 生产环境 **必须** 启用 HTTPS/TLS，否则 OAuth2 毫无安全性可言。
- **PKCE**: 对于移动端/SPA 客户端，建议开启 PKCE (Proof Key for Code Exchange) 模式（本后端已实现并对 PUBLIC 客户端默认强制启用，支持 `plain` 与 `S256`）。
- **IP 白名单**: 对于高权限 Client，建议限制 Token 兑换的源 IP。

## 5. Token 存储安全 (Token Storage)

所有 Token（Access Token、Refresh Token）在数据库中 **仅存储 SHA-256 哈希值**，绝不存储明文。

- **存储格式**: `SHA-256(token_value)`
- **验证流程**: 客户端提交 Token → 服务端计算哈希 → 与数据库哈希比对
- **优势**: 即使数据库泄露，攻击者无法还原出有效 Token

## 6. 密码哈希策略 (Password Hashing)

### 6.1 当前标准 (OWASP 2023)

- **算法**: PBKDF2-SHA256
- **迭代次数**: 310,000 次（符合 OWASP 2023 推荐）
- **Salt**: 每用户独立随机 Salt（16 字节）
- **输出**: 32 字节密钥

### 6.2 遗留密码迁移

系统支持从旧版 SHA-256 单次哈希渐进式迁移到 PBKDF2：

1. 用户登录时，系统检测密码哈希格式
2. 若为旧格式（SHA-256），验证通过后自动升级为 PBKDF2
3. 迁移对用户透明，无需重置密码

## 7. Refresh Token 轮转与家族追踪 (Refresh Token Rotation)

### 7.1 家族追踪机制 (Family-Based Tracking)

每个 Refresh Token 链共享一个 `token_family` 标识符：

- 首次颁发 RT 时生成唯一 `token_family` ID
- 后续轮转的 RT 继承同一 `token_family`
- 系统可追踪整个 Token 链的生命周期

### 7.2 重用检测与级联撤销

当检测到已撤销的 Refresh Token 被再次使用时：

1. **检测**: 收到的 RT 在数据库中标记为已撤销
2. **判定**: 视为 Token 泄露（攻击者持有旧 RT）
3. **响应**: 级联撤销该 `token_family` 下的 **所有** Token
4. **结果**: 合法用户和攻击者均需重新认证

### 7.3 时序图

```
用户 → 服务器: 使用 RT-1 刷新
服务器: 撤销 RT-1, 颁发 RT-2 (同 family)
攻击者 → 服务器: 使用 RT-1 刷新 (重用!)
服务器: 检测到 RT-1 已撤销 → 级联撤销 family 所有 Token
用户: 下次请求失败, 需重新登录
```

## 8. Subject 隐私保护 (Subject Privacy)

### 8.1 UUID public_sub

- **外部标识**: 使用 UUID v4 作为 `public_sub`（公开主体标识符）
- **内部标识**: 数据库自增 ID 仅用于内部关联
- **防枚举**: UUID 不可预测，攻击者无法通过递增 ID 枚举用户
- **OIDC 兼容**: `id_token` 中的 `sub` claim 使用 `public_sub`

### 8.2 对比

| 方案 | 可枚举 | 信息泄露 | OIDC 兼容 |
|------|--------|----------|-----------|
| 自增 ID | ✗ 可预测 | 泄露用户数量 | ✓ |
| UUID public_sub | ✓ 不可预测 | 无信息泄露 | ✓ |
