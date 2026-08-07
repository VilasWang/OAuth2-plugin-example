# 身份认证 (authforge::identity)

<cite>
**本文引用的文件**
- [README.md](file://README.md)
- [V004__users_table.sql](file://apps/server/migrations/V004__users_table.sql)
- [V005__rbac_schema.sql](file://apps/server/migrations/V005__rbac_schema.sql)
- [V011__mfa_support.sql](file://apps/server/migrations/V011__mfa_support.sql)
- [V012__audit_logs.sql](file://apps/server/migrations/V012__audit_logs.sql)
- [V013__account_lockout.sql](file://apps/server/migrations/V013__account_lockout.sql)
- [V018__webauthn.sql](file://apps/server/migrations/V018__webauthn.sql)
- [AuthService.cc](file://libs/identity/src/AuthService.cc)
- [MfaService.cc](file://libs/identity/src/mfa/MfaService.cc)
- [GoogleAuthService.cc](file://libs/identity/src/social/GoogleAuthService.cc)
- [GitHubAuthService.cc](file://libs/identity/src/social/GitHubAuthService.cc)
- [SocialAuthService.h](file://libs/identity/include/authforge/identity/SocialAuthService.h)
- [IOAuthHttpClient.h](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h)
- [PostgresIdentityRepository.cc](file://libs/storage-postgres/src/PostgresIdentityRepository.cc)
- [PostgresSocialAccountRepository.cc](file://libs/storage-postgres/src/PostgresSocialAccountRepository.cc)
- [TokenEndpointController.cc](file://libs/drogon/src/controllers/TokenEndpointController.cc)
- [UserSelfServiceController.cc](file://libs/drogon/src/controllers/UserSelfServiceController.cc)
- [openapi.json](file://apps/server/docs/api/openapi.json)
- [rbac-guide.md](file://docs/backend/rbac-guide.md)
</cite>

## 目录
1. [简介](#简介)
2. [项目结构](#项目结构)
3. [核心组件](#核心组件)
4. [架构总览](#架构总览)
5. [详细组件分析](#详细组件分析)
6. [依赖关系分析](#依赖关系分析)
7. [性能考量](#性能考量)
8. [故障排查指南](#故障排查指南)
9. [结论](#结论)
10. [附录](#附录)

## 简介
本文件面向 authforge::identity 身份认证库，系统性阐述用户生命周期管理、密码哈希、会话与令牌、多因素认证（TOTP/WebAuthn）、基于角色的访问控制（RBAC）、社交登录集成与安全策略（防暴力破解、账户锁定、审计日志）等能力。文档同时提供架构图、时序图、流程图以及可操作的集成建议与安全最佳实践，帮助读者快速理解并安全落地。

## 项目结构
authforge 采用分层 SDK 设计，identity 层负责“认证、MFA、WebAuthn、RBAC”等身份域能力，并通过端口（如 IOAuthHttpClient、ISocialAccountRepository、ICryptoProvider、IClock 等）与存储实现解耦；上层 Drogon 插件/控制器负责 HTTP 编排与协议交互；存储层通过 storage-postgres/memory/redis 等适配器对接持久化。

```mermaid
graph TB
subgraph "应用层"
S["authforge-server<br/>apps/server"]
end
subgraph "SDK 层"
D["authforge::drogon<br/>插件/控制器/过滤器"]
I["authforge::identity<br/>认证·MFA·WebAuthn·RBAC"]
O["authforge::oauth2<br/>OAuth2/OIDC 引擎"]
C["authforge::common<br/>共享内核/端口"]
end
subgraph "存储层"
PG["storage::postgres<br/>ORM 模型"]
RM["storage::memory"]
RS["storage::redis"]
end
S --> D
D --> I
D --> O
D --> RM
D --> RS
D --> PG
I --> C
O --> C
PG --> I
```

图示来源
- [README.md:31-49](file://README.md#L31-L49)

章节来源
- [README.md:16-51](file://README.md#L16-L51)

## 核心组件
- 用户服务（AuthService）：注册、密码哈希、默认角色分配、错误分类。
- MFA 服务（MfaService）：TOTP 密钥生成、二维码 URI、校验启用、备份码。
- WebAuthn：凭据表结构与索引，支持 Passkey/FIDO2。
- RBAC：角色/权限表、用户-角色关联、URL 规则匹配。
- 社交登录：Google/GitHub 等第三方授权码交换与本地账户映射。
- 会话与令牌：刷新令牌家族、撤销与审计、前端恢复会话。
- 安全策略：账户锁定、审计日志、速率限制（Hodor）。

章节来源
- [AuthService.cc:247-286](file://libs/identity/src/AuthService.cc#L247-L286)
- [MfaService.cc:1-51](file://libs/identity/src/mfa/MfaService.cc#L1-L51)
- [V018__webauthn.sql:1-16](file://apps/server/migrations/V018__webauthn.sql#L1-L16)
- [V005__rbac_schema.sql:1-59](file://apps/server/migrations/V005__rbac_schema.sql#L1-L59)
- [SocialAuthService.h:25-44](file://libs/identity/include/authforge/identity/SocialAuthService.h#L25-L44)
- [TokenEndpointController.cc:1201-1223](file://libs/drogon/src/controllers/TokenEndpointController.cc#L1201-L1223)
- [V012__audit_logs.sql:1-23](file://apps/server/migrations/V012__audit_logs.sql#L1-L23)
- [V013__account_lockout.sql:1-7](file://apps/server/migrations/V013__account_lockout.sql#L1-L7)

## 架构总览
身份认证由“控制器/过滤器 -> identity 服务 -> 存储适配 -> 外部系统（社交提供商/设备）”构成。identity 层不直接依赖框架类型，通过端口注入实现可测试性与可替换性。

```mermaid
sequenceDiagram
participant U as "客户端"
participant C as "Drogon 控制器"
participant I as "identity 服务"
participant R as "存储适配(POSTGRES)"
participant E as "外部系统(社交/TOTP)"
U->>C : "登录/注册/设置MFA/社交回调"
C->>I : "调用业务方法(验证/创建/校验)"
I->>R : "读写用户/角色/凭据/审计"
I->>E : "调用外部接口(如 Google/GitHub/TOTP)"
E-->>I : "返回结果"
I-->>C : "返回领域结果"
C-->>U : "HTTP 响应(含令牌/状态)"
```

图示来源
- [SocialAuthService.h:25-44](file://libs/identity/include/authforge/identity/SocialAuthService.h#L25-L44)
- [IOAuthHttpClient.h:1-26](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h#L1-L26)
- [PostgresIdentityRepository.cc:276-317](file://libs/storage-postgres/src/PostgresIdentityRepository.cc#L276-L317)

## 详细组件分析

### 用户生命周期与密码哈希
- 注册流程：生成 PBKDF2 密码哈希（嵌入盐），写入用户记录，并分配默认角色（user）。
- 登录验证：读取数据库中的哈希与盐，使用 PasswordHasher 进行验证；成功后重置失败计数与锁定状态。
- 密码更新：校验旧密码后生成新哈希并更新，同时使相关刷新令牌失效并记录审计事件。

```mermaid
flowchart TD
Start(["入口"]) --> CheckInput["校验输入参数"]
CheckInput --> HashPwd["计算密码哈希(PBKDF2)"]
HashPwd --> CreateUser["创建用户并分配默认角色"]
CreateUser --> ReturnOk{"是否成功?"}
ReturnOk --> |是| EndOK["返回成功"]
ReturnOk --> |否| HandleErr["返回结构化错误码"]
HandleErr --> EndErr["结束"]
EndOK --> End
```

图示来源
- [AuthService.cc:247-286](file://libs/identity/src/AuthService.cc#L247-L286)
- [PostgresIdentityRepository.cc:276-317](file://libs/storage-postgres/src/PostgresIdentityRepository.cc#L276-L317)
- [V004__users_table.sql:1-11](file://apps/server/migrations/V004__users_table.sql#L1-L11)

章节来源
- [AuthService.cc:247-286](file://libs/identity/src/AuthService.cc#L247-L286)
- [PostgresIdentityRepository.cc:276-317](file://libs/storage-postgres/src/PostgresIdentityRepository.cc#L276-L317)
- [UserSelfServiceController.cc:197-300](file://libs/drogon/src/controllers/UserSelfServiceController.cc#L197-L300)
- [V004__users_table.sql:1-11](file://apps/server/migrations/V004__users_table.sql#L1-L11)

### 多因素认证（TOTP 与 WebAuthn）
- TOTP：生成随机密钥与 OTP Auth URI，存储密钥与备份码；登录时校验一次性验证码，通过后标记 MFA 已验证。
- WebAuthn：以 webauthn_credentials 表存储凭据 ID、公钥、签名次数、传输方式等，支持 FIDO2/Passkey 无密码登录。

```mermaid
sequenceDiagram
participant U as "用户"
participant A as "应用/前端"
participant S as "MfaService"
participant DB as "存储"
participant APP as "认证流程"
U->>A : "请求开启 MFA"
A->>S : "setupSecret(userId, label)"
S->>DB : "保存密钥/备份码"
DB-->>S : "成功"
S-->>A : "返回 secret + otpAuthUri"
U->>A : "输入 TOTP 验证码"
A->>S : "verifyAndEnable(userId, code)"
S->>DB : "校验并启用 MFA"
DB-->>S : "成功"
S-->>APP : "返回 mfa_verified=true"
```

图示来源
- [MfaService.cc:1-51](file://libs/identity/src/mfa/MfaService.cc#L1-L51)
- [V011__mfa_support.sql:1-6](file://apps/server/migrations/V011__mfa_support.sql#L1-L6)
- [V018__webauthn.sql:1-16](file://apps/server/migrations/V018__webauthn.sql#L1-L16)
- [openapi.json:1761-1824](file://apps/server/docs/api/openapi.json#L1761-L1824)

章节来源
- [MfaService.cc:1-51](file://libs/identity/src/mfa/MfaService.cc#L1-L51)
- [V011__mfa_support.sql:1-6](file://apps/server/migrations/V011__mfa_support.sql#L1-L6)
- [V018__webauthn.sql:1-16](file://apps/server/migrations/V018__webauthn.sql#L1-L16)
- [openapi.json:1761-1824](file://apps/server/docs/api/openapi.json#L1761-L1824)

### 基于角色的访问控制（RBAC）
- 数据模型：roles、permissions、user_roles、role_permissions，内置 admin/user 角色及基础权限。
- 访问控制：通过 URL 正则规则匹配（如 /api/admin/.* 需要 admin），结合 Token 中 userId 查询当前角色进行鉴权。
- 管理端：提供角色/权限 CRUD 与用户角色分配能力。

```mermaid
classDiagram
class Roles {
+int id
+string name
+string description
+timestamp created_at
+timestamp updated_at
}
class Permissions {
+int id
+string name
+string description
+timestamp created_at
}
class UserRoles {
+int user_id
+int role_id
+timestamp assigned_at
}
class RolePermissions {
+int role_id
+int permission_id
}
Roles "1" -- "*" UserRoles : "被分配给"
Permissions "1" -- "*" RolePermissions : "被赋予"
```

图示来源
- [V005__rbac_schema.sql:1-59](file://apps/server/migrations/V005__rbac_schema.sql#L1-L59)
- [rbac-guide.md:1-83](file://docs/backend/rbac-guide.md#L1-L83)

章节来源
- [V005__rbac_schema.sql:1-59](file://apps/server/migrations/V005__rbac_schema.sql#L1-L59)
- [rbac-guide.md:1-83](file://docs/backend/rbac-guide.md#L1-L83)

### 社交登录集成（Google、WeChat、GitHub）
- 统一抽象：通过 IOAuthHttpClient 端口封装出站 HTTP（表单 POST 换 token、带 Bearer 的 GET 拉取用户信息）。
- 具体实现：GoogleAuthService/GitHubAuthService 将授权码交换为 access_token，获取用户资料，并在本地 find-or-create 用户、绑定 subject、分配默认角色。
- 边界：identity 层不签发 OAuth2 令牌，仅完成“识别/创建本地账户”，令牌颁发由上层 oauth2 域处理。

```mermaid
sequenceDiagram
participant FE as "前端"
participant G as "GoogleAuthService"
participant H as "IOAuthHttpClient"
participant P as "PostgresSocialAccountRepository"
participant O as "OAuth2 令牌服务"
FE->>G : "login(code)"
G->>H : "POST 换 token"
H-->>G : "access_token"
G->>H : "GET userinfo(Bearer)"
H-->>G : "profile"
G->>P : "find-or-create 用户并绑定 subject"
P-->>G : "返回本地 userId"
G-->>FE : "返回本地用户标识(不含令牌)"
FE->>O : "走 OAuth2 流程换取令牌"
```

图示来源
- [SocialAuthService.h:25-44](file://libs/identity/include/authforge/identity/SocialAuthService.h#L25-L44)
- [IOAuthHttpClient.h:1-26](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h#L1-L26)
- [GoogleAuthService.cc:1-47](file://libs/identity/src/social/GoogleAuthService.cc#L1-L47)
- [GitHubAuthService.cc:1-44](file://libs/identity/src/social/GitHubAuthService.cc#L1-L44)
- [PostgresSocialAccountRepository.cc:77-109](file://libs/storage-postgres/src/PostgresSocialAccountRepository.cc#L77-L109)

章节来源
- [SocialAuthService.h:25-44](file://libs/identity/include/authforge/identity/SocialAuthService.h#L25-L44)
- [IOAuthHttpClient.h:1-26](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h#L1-L26)
- [GoogleAuthService.cc:1-47](file://libs/identity/src/social/GoogleAuthService.cc#L1-L47)
- [GitHubAuthService.cc:1-44](file://libs/identity/src/social/GitHubAuthService.cc#L1-L44)
- [PostgresSocialAccountRepository.cc:77-109](file://libs/storage-postgres/src/PostgresSocialAccountRepository.cc#L77-L109)

### 会话管理与令牌刷新
- 刷新令牌家族：refresh_token 采用哈希存储，支持家族级撤销与复用检测。
- 前端恢复：前端持久化 refresh_token，页面刷新时尝试恢复会话，失败则视为未认证。
- 审计：令牌发放、撤销、复用等关键动作均记录审计日志。

```mermaid
sequenceDiagram
participant FE as "前端"
participant TE as "TokenEndpoint"
participant TS as "TokenService"
participant ST as "存储"
participant AU as "审计"
FE->>TE : "grant_type=refresh_token"
TE->>TS : "refreshAccessToken(rt, client)"
TS->>ST : "atomicRevokeRefreshToken(哈希)"
ST-->>TS : "返回原 RT 或家族信息"
TS->>AU : "记录 token_issued/token_reuse_detected"
TS-->>TE : "返回新 access_token/refresh_token"
TE-->>FE : "响应"
```

图示来源
- [TokenEndpointController.cc:1201-1223](file://libs/drogon/src/controllers/TokenEndpointController.cc#L1201-L1223)
- [V012__audit_logs.sql:1-23](file://apps/server/migrations/V012__audit_logs.sql#L1-L23)

章节来源
- [TokenEndpointController.cc:1201-1223](file://libs/drogon/src/controllers/TokenEndpointController.cc#L1201-L1223)
- [V012__audit_logs.sql:1-23](file://apps/server/migrations/V012__audit_logs.sql#L1-L23)

### 安全策略：防暴力破解、账户锁定、审计日志
- 账户锁定：users 表新增 failed_login_count、locked_until、last_failed_login，达到阈值后锁定一段时间。
- 审计日志：结构化记录 actor/action/target/outcome/ip/user_agent/request_id/details，便于合规与排障。
- 速率限制：通过 Hodor 插件对敏感接口实施每 IP/每用户的速率限制，触发 429。

```mermaid
flowchart TD
A["登录尝试"] --> B{"失败次数 < 阈值?"}
B --> |是| C["增加失败计数"]
B --> |否| D["锁定账户至 locked_until"]
C --> E{"是否超过更高阈值?"}
E --> |是| D
E --> |否| F["允许重试"]
D --> G["记录审计日志"]
F --> G
```

图示来源
- [V013__account_lockout.sql:1-7](file://apps/server/migrations/V013__account_lockout.sql#L1-L7)
- [V012__audit_logs.sql:1-23](file://apps/server/migrations/V012__audit_logs.sql#L1-L23)

章节来源
- [V013__account_lockout.sql:1-7](file://apps/server/migrations/V013__account_lockout.sql#L1-L7)
- [V012__audit_logs.sql:1-23](file://apps/server/migrations/V012__audit_logs.sql#L1-L23)

## 依赖关系分析
- identity 层依赖 common 提供的通用工具与端口（ICryptoProvider、IClock、IOAuthHttpClient 等），避免耦合框架。
- storage-postgres 实现 identity 的持久化接口，提供用户、角色、MFA、WebAuthn、社交账户等表的 ORM 操作。
- drogon 层作为控制器/过滤器，编排 identity 与 oauth2 服务，并负责 HTTP 协议细节。

```mermaid
graph LR
Common["common"] --> Identity["identity"]
Identity --> StoragePG["storage::postgres"]
Drogon["drogon"] --> Identity
Drogon --> OAuth2["oauth2"]
OAuth2 --> Common
```

图示来源
- [README.md:31-49](file://README.md#L31-L49)
- [IOAuthHttpClient.h:1-26](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h#L1-L26)
- [PostgresIdentityRepository.cc:276-317](file://libs/storage-postgres/src/PostgresIdentityRepository.cc#L276-L317)

章节来源
- [README.md:31-49](file://README.md#L31-L49)
- [IOAuthHttpClient.h:1-26](file://libs/identity/include/authforge/identity/IOAuthHttpClient.h#L1-L26)
- [PostgresIdentityRepository.cc:276-317](file://libs/storage-postgres/src/PostgresIdentityRepository.cc#L276-L317)

## 性能考量
- 密码哈希：PBKDF2 强度适中，首次登录可能触发 rehash，建议在预热阶段完成以避免冷启动抖动。
- 刷新令牌家族：复用检测会触发家族级撤销，需确保客户端正确轮换 refresh_token，避免批量失效。
- 审计日志：高频事件（token_issued、token_reuse_detected）应异步落盘并考虑分片/归档策略。
- 速率限制：针对 /oauth2/login、/oauth2/token、/api/register 等敏感路径配置更严格的 per-IP/per-user 限制。

[本节为通用指导，无需特定文件引用]

## 故障排查指南
- 登录失败导致锁定：检查 users.failed_login_count 与 locked_until，确认是否达到阈值；成功后应重置计数。
- 密码修改失败：核对旧密码校验逻辑与新哈希生成，关注审计日志中的 password_change_failed。
- 社交登录异常：确认 IOAuthHttpClient 可达、client_id/secret/redirect_uri 配置正确；查看社交回调与本地账户映射步骤。
- 令牌刷新失败：检查 refresh_token 是否过期或被撤销；若检测到复用，确认是否触发了家族级撤销。

章节来源
- [V013__account_lockout.sql:1-7](file://apps/server/migrations/V013__account_lockout.sql#L1-L7)
- [UserSelfServiceController.cc:197-300](file://libs/drogon/src/controllers/UserSelfServiceController.cc#L197-L300)
- [PostgresSocialAccountRepository.cc:77-109](file://libs/storage-postgres/src/PostgresSocialAccountRepository.cc#L77-L109)
- [TokenEndpointController.cc:1201-1223](file://libs/drogon/src/controllers/TokenEndpointController.cc#L1201-L1223)

## 结论
authforge::identity 提供了完整且可插拔的身份认证能力：从用户注册与密码哈希、到 TOTP/WebAuthn 的多因素认证、再到 RBAC 与社交登录集成，配合审计日志与账户锁定等安全机制，满足生产环境的高可用与高安全要求。通过清晰的端口设计与分层架构，开发者可以按需组合功能模块，并以最小依赖集成到自有系统中。

[本节为总结，无需特定文件引用]

## 附录
- 集成示例（概念性）
  - 注册新用户：调用 identity 注册接口，传入 username/password/email；系统生成 PBKDF2 哈希并分配 user 角色。
  - 开启 TOTP：调用 setupSecret 获取 OTP URI，用户扫码后提交验证码完成启用。
  - 社交登录：前端引导用户至 Google/GitHub 授权，回调后 exchange code 并建立本地账户。
  - 刷新令牌：前端在页面加载时携带 refresh_token 换取新的 access_token，失败则清空会话。
- 安全最佳实践
  - 强制 HTTPS，启用 HSTS（仅在 HTTPS 场景）。
  - 合理配置速率限制与账户锁定阈值，防止暴力破解。
  - 定期轮换密钥与证书，最小化权限原则（RBAC）。
  - 审计日志集中采集与告警，关注异常模式（大量失败、令牌复用等）。

[本节为通用指导，无需特定文件引用]