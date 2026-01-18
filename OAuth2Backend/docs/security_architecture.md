# OAuth2 安全架构设计 (Security Architecture)

本文档详细描述了本系统的安全威胁模型及相应的防御机制，涵盖 Token 生命周期管理、加密存储及防攻击策略。

## 1. 威胁模型与防御 (Threat Model)

| 威胁类型 | 描述 | 防御机制 | 对应文档 |
|----------|------|----------|----------|
| **Replay Attack** (重放攻击) | 攻击者截获 Auth Code 并在合法客户端之前或之后尝试兑换。 | **Atomic Consume** (原子消费) + **One-Time Use Enforcement**。 | [Data Consistency](data_consistency.md) |
| **Credential Leakage** (凭据泄露) | 数据库被拖库导致 Client Secret 泄露。 | **SHA256 Salted Hash**。数据库仅存 Hash 值，绝不存明文。 | [Data Persistence](data_persistence.md) |
| **Token Theft** (令牌窃取) | Access Token 被截获。 | **Short-lived Token** (1小时) + **Refresh Token Rotation** (轮转机制)。 | 本文档 |
| **CSRF** | 攻击者诱导用户进行非预期的授权。 | 强制校验 **state** 参数 (推荐客户端实现)。 | [API Reference](api_reference.md) |

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
  - **检测机制**: 如果检测到旧 Refresh Token 被再次使用，系统可视为 Token 泄露，并级联撤销该用户的所有关联 Token（TODO Feature）。

## 3. 密钥管理 (Secrets Management)

### 3.1 Client Secrets

- **存储**: `sha256(secret + salt)`
- **传输**: 仅在 POST body 中通过 HTTPS 传输。

### 3.2 配置文件

- 敏感信息（如 DB 密码、Redis 密码）建议通过 **环境变量** 注入，而非硬编码在 `config.json` 中。
- 生产环境部署时，应确保配置文件权限严格限制。

## 4. 最佳实践建议

- **HTTPS**: 生产环境 **必须** 启用 HTTPS/TLS，否则 OAuth2 毫无安全性可言。
- **PKCE**: 对于移动端/SPA 客户端，建议开启 PKCE (Proof Key for Code Exchange) 模式（本后端已在数据库预留字段支持，需升级 Plugin 逻辑启用）。
- **IP 白名单**: 对于高权限 Client，建议限制 Token 兑换的源 IP。
