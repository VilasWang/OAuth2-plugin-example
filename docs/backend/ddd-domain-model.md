# 开源 IAM 系统 DDD 领域模型设计说明

> 状态：Draft v1.0（设计提案，未经评审）
> 日期：2026-08-21
> 范围：面向开源 IAM（身份与访问管理）系统的领域驱动设计（DDD）完整方案 ——
> 战略设计（子域/限界上下文/上下文映射）+ 战术设计（聚合/实体/值对象/领域服务/领域事件）
> + 分层架构与跨上下文集成机制。
> 附录给出与 fulla 仓库现状的映射，供演进参考；正文设计独立成立，不依赖仓库现状。

## 0. 设计目标与原则

本设计面向一个开源 IAM 系统，覆盖认证、授权、用户管理、客户端接入、访问控制、多租户与审计。采用 DDD 的完整方法论：先做**战略设计**（子域与限界上下文划分、上下文映射），再做**战术设计**（聚合、实体、值对象、领域服务、领域事件），最后给出**四层架构**与落地约束。

核心原则：

1. **协议即公开语言（Published Language）**：OAuth 2.0/2.1、OIDC、JWT、Scope 字符串本身就是标准化契约，上下文对外暴露的模型尽量与协议对齐，不发明私有术语。
2. **聚合边界 = 一致性边界 = 事务边界**：一个事务只修改一个聚合；跨聚合、跨上下文一律通过领域事件达成最终一致。
3. **聚合间只通过 ID 引用**：禁止对象图跨聚合导航、禁止跨上下文 SQL JOIN，各自拥有数据。
4. **领域层零外部依赖**：不依赖框架、数据库、HTTP；仓储接口定义在领域层、实现在基础设施层（依赖倒置）。
5. **安全属性是领域不变量，不是横切补丁**：PKCE 强制、密钥只存哈希、授权码单次使用、刷新令牌轮换盗用检测，全部建模为聚合不变量。

---

## 1. 战略设计：领域、子域与限界上下文

### 1.1 领域与子域划分

**领域**：数字身份与访问管理（IAM）。

| 子域 | 类型 | 划分理由 |
|---|---|---|
| 认证（Authentication） | **核心** | 密码/Passkey/MFA/登录策略是 IAM 产品竞争力的直接来源，必须自研并深度建模 |
| 授权（Authorization，OAuth2/OIDC 协议域） | **核心** | 令牌签发、同意管理、协议合规度是第二核心竞争力 |
| 身份管理（Identity Management） | 支撑 | 用户档案与生命周期，为两个核心子域提供主体数据 |
| 访问控制（Access Control，RBAC/Scope） | 支撑 | 角色、权限、作用域注册表，为核心子域供给判定素材 |
| 客户端管理（Client Management） | 支撑 | 接入方（RP）注册与配置，模型相对稳定 |
| 会话管理（Session Management） | 支撑 | SSO 会话、登出传播（backchannel logout） |
| 租户管理（Tenancy） | 通用* | 多租户隔离与配额，*若多租户是商业差异化点则升为支撑/核心 |
| 审计与合规（Audit） | 通用 | 追加式事件日志，可被 ELK/SIEM 方案替代部分能力 |
| 密钥管理（Key Management） | 通用 | 签名密钥与 JWKS 轮换 |
| 通知（Notification） | 通用 | 验证邮件、密码重置送达 |

### 1.2 限界上下文

每个子域对应一个限界上下文（模型边界 + 数据所有权 + 团队/模块边界）：

| # | 限界上下文 | 回答的问题 | 拥有的核心数据 |
|---|---|---|---|
| BC1 | **身份上下文**（Identity） | 这个主体是谁？ | 用户档案、账号状态 |
| BC2 | **认证上下文**（Authentication） | 如何证明是本人？ | 凭据、MFA 因子、登录会话 |
| BC3 | **授权上下文**（Authorization） | 允许做什么？发放什么凭证？ | 授权会话、令牌、同意记录 |
| BC4 | **客户端上下文**（Client） | 接入方是谁、允许什么 | OAuth 客户端注册信息 |
| BC5 | **访问控制上下文**（Access Control） | 有哪些角色/权限/作用域 | 角色、权限、分配关系 |
| BC6 | **租户上下文**（Tenancy） | 属于哪个租户、配额几何 | 租户、组织、邀请 |
| BC7 | **审计上下文**（Audit） | 发生过什么 | 不可变活动记录 |

**两个关键切分决策（为什么这样切）**：

- **身份与认证分离**：「用户是谁」（档案、状态）与「用户如何自证」（凭据、登录过程）变化速率不同——档案字段频繁演进，认证方式从密码到 TOTP 到 Passkey 更是独立演进；而且账号锁定、登录尝试计数属于认证策略，不该污染档案聚合。两上下文仅以 `UserId` 关联。
- **客户端独立于授权**：客户端注册是管理员低频写、授权流程高频读（可缓存），与授权上下文的高频令牌读写生命周期完全不同，独立出来才能各自演进（如未来支持动态客户端注册 RFC 7591）。

**共享内核（Shared Kernel）刻意最小化**：仅包含标识类型（`UserId`、`ClientId`、`TenantId`）与领域事件契约。协议层语义（JWT claims、scope 字符串）作为公开语言由授权上下文对外发布，不进入共享内核。

### 1.3 上下文映射（Context Map）

```
                              ┌──────────────────┐
                              │   租户上下文 BC6   │  TenantId 贯穿所有上下文
                              └────────┬─────────┘  （共享内核标识 + 行级隔离）
                                       │ 事件: QuotaExceeded / TenantSuspended
        ┌──────────────────────────────┼──────────────────────────────┐
        │                              │                              │
        ▼                              ▼                              ▼
┌───────────────┐  UHS+事件   ┌────────────────┐   Conformist   ┌───────────┐
│ 身份上下文 BC1 │◄────────────│   认证上下文 BC2 │◄────ACL────── │ 外部 IdP   │
│ 用户档案/状态   │            │ 凭据/MFA/登录会话│               │ LDAP/社交  │
└───────┬───────┘             └───────┬────────┘                └───────────┘
        │                             │ 会话交接(SSO Session)
        │                             ▼
        │                      ┌────────────────┐◄──Customer/Supplier──┌───────────────┐
        │                      │   授权上下文 BC3 │    claims 增强       │ 访问控制上下文 │
        │                      │  授权会话/令牌/   │─────────────────────│   BC5 RBAC    │
        │                      │  同意/协议端点    │                     └───────────────┘
        │                      └───────┬────────┘◄──Conformist+缓存──┌───────────┐
        │                              │                           │ 客户端上下文│
        │                              │                           │    BC4     │
        │                              │ Open Host Service
        │                              │ (OIDC 标准端点 = 公开语言)
        │                              ▼
        │                        依赖方应用(RP)/前端
        │
        ▼  所有上下文的领域事件 (Outbox → 事件总线 → 最终一致)
┌───────────────┐    ┌──────────────┐    ┌──────────────┐
│  审计上下文 BC7 │    │  通知(通用)   │    │  缓存失效处理   │
└───────────────┘    └──────────────┘    └──────────────┘
```

上下文关系明细：

| 关系（上游 → 下游） | 映射模式 | 集成方式 |
|---|---|---|
| 身份 BC1 → 认证 BC2 | Customer-Supplier | 进程内 API 同步读 + 事件（`UserSuspended` 终止会话） |
| 认证 BC2 → 授权 BC3 | Customer-Supplier | 已认证会话交接（session id / `iss` 声明校验） |
| 客户端 BC4 → 授权 BC3 | Conformist + 只读缓存 | 授权侧以客户端公开语言（ClientId、redirectUris、allowedScopes）工作；`ClientUpdated` 事件失效缓存 |
| 访问控制 BC5 → 授权 BC3 | Customer-Supplier | 令牌 claims 增强（同步读模型或事件喂缓存） |
| 租户 BC6 → 全部 | Shared Kernel + 事件 | `TenantId` 行级过滤；配额/停用事件广播 |
| 全部 → 审计 BC7 | Published Language | Outbox + 事件总线，最终一致 |
| 外部 IdP → 认证 BC2 | **防腐层（ACL）** | 适配器把外部身份翻译为本地 `AuthResult`，外部模型绝不渗透 |
| 授权 BC3 → 依赖方/前端 | **开放主机服务（OHS）** | OIDC/OAuth2 标准端点本身就是公开语言 |

---

## 2. 战术设计：各限界上下文详细模型

以下每个上下文给出：聚合根（含关键不变量）、实体、值对象、领域服务、领域事件、仓储接口。

### 2.1 身份上下文（BC1）

> 职责：用户的档案与账号生命周期。**不含**凭据与登录过程。

**聚合根：`User`（用户）**
- 标识：`UserId`
- 组成：`Username`、`Email`、`PhoneNumber`、`UserProfile`（昵称/头像/时区/语言）、`AccountStatus`
- 行为：`register()`、`activate()`、`verifyEmail()`、`updateProfile()`、`suspend()`、`reinstate()`、`requestDeletion()`
- 不变量：
  - 账号状态机合法转换（`pending → active ⇄ locked/disabled → deleted`，不可跳转）；
  - `deleted` 状态下档案字段全部匿名化（合规：可保留审计引用，不可保留 PII）；
  - username/email 全局唯一（跨聚合查询，由领域服务 `UserUniquenessService` 在注册/变更时校验，聚合无法自证）。

**实体**：`EmailVerificationToken`（属于 User 聚合外缘的独立小聚合或实体——一次性、绑定 email、带 TTL）。

**值对象**：`UserId`、`Username`、`Email`（格式+规范化）、`PhoneNumber`（E.164）、`UserProfile`、`AccountStatus`（枚举）。

**领域服务**：`UserUniquenessService`（唯一性校验，封装跨聚合查询）。

**领域事件**：`UserRegistered`、`UserActivated`、`EmailVerified`、`UserProfileUpdated`、`UserSuspended`、`UserReinstated`、`UserDeleted`。

**仓储接口**：`IUserRepository`。

### 2.2 认证上下文（BC2）

> 职责：凭据管理、登录过程编排、MFA、账号锁定。**不含** OAuth 令牌（那是 BC3 的事）。

**聚合根 1：`AuthenticationSession`（登录会话）**
- 组成：sessionId、`UserId?`（认证成功前为空）、`LoginContext`（IP/UA/设备指纹）、状态、失败计数、租户、过期时间
- 状态机：`pending → password_verified → mfa_challenged → completed | failed | expired | locked`
- 行为：`begin()`、`recordPasswordResult()`、`challengeMfa()`、`verifyMfa()`、`complete()`、`fail()`
- 不变量：
  - 失败次数超过 `LockoutPolicy` 阈值 → 置 `locked` 并发出 `LockoutApplied`（由 BC1 消费置账号锁定，认证侧不直接改档案）；
  - `completed` 前必须已解析出 `UserId`。

**聚合根 2：`Credential`（凭据，多态）**
- 变体：`PasswordCredential` / `TOTPCredential` / `WebAuthnCredential` / `BackupCodeCredential`
- 组成：credentialId、`UserId`（引用）、`CredentialType`、状态（`pending_verification → active → revoked`）、类型化元数据
- 行为：`beginEnrollment()`、`confirmEnrollment()`、`verify()`、`revoke()`、`rotate()`
- 不变量：
  - 每用户至多一个 `active` 密码凭据；
  - WebAuthn 的 `signCount` 必须单调递增（克隆检测）；
  - `TOTPSecret` 加密落库，**任何读路径都不返回明文**；密码/备份码只存哈希；
  - 新凭据必须先经历 `pending_verification`（防枚举与半注册攻击）。

**值对象**：`PasswordHash`、`TOTPSecret`（密文）、`WebAuthnCredentialId`、`ChallengeNonce`（一次性）、`LoginContext`、`LockoutState`、`AuthenticationResult`。

**领域服务**：`PasswordHashingService`（接口，argon2/bcrypt 实现在基础设施）、`WebAuthnCeremonyService`（挑战生成与断言验证）、`MfaPolicyService`（依据租户/用户配置判定必需因子）、`LockoutPolicy`（策略对象）。

**领域事件**：`LoginSucceeded`、`LoginFailed`、`LockoutApplied`、`CredentialEnrolled`、`CredentialRevoked`、`PasswordChanged`、`PasswordResetRequested`、`PasswordResetCompleted`、`MfaVerified`、`AuthenticationSessionExpired`。

**仓储接口**：`ICredentialRepository`、`IAuthenticationSessionRepository`。

### 2.3 授权上下文（BC3）——OAuth2/OIDC 协议域

> 职责：授权码/隐式/客户端凭据等授权流程、令牌签发与吊销、同意管理、协议发现端点。这是最核心的上下文。

**聚合根 1：`AuthorizationGrant`（授权会话）**
- 组成：grantId、`ClientId`（引用）、`UserId`（引用，client-credentials 流为空）、`ScopeSet`、`ResourceIndicator` 列表（RFC 8707）、`AuthorizationCode`（值对象，可空）、状态、consent 状态
- 行为：`request()`（校验 client/redirect/scope 后建立）、`bindConsent()`、`issueCode()`、`consumeCode()`、`revoke()`
- 不变量（安全关键，直接建模进聚合而非散落在控制器）：
  - 授权码**单次使用**，TTL 短（建议 ≤ 60s–5min）；二次呈递即 `AuthorizationCodeReuseDetected` 并吊销该 grant 全部令牌；
  - 授权码绑定 client_id + redirect_uri + PKCE challenge + nonce，`consumeCode()` 时逐一比对；
  - 公共客户端**强制 PKCE**（RFC 7636，仅 `S256`）；
  - 令牌 scope ⊆ grant scope ⊆ client allowed scope（scope 只能在授权链上收窄）。

**聚合根 2：`RefreshToken`（刷新令牌族）**
- 组成：tokenId、familyId、grantId（引用）、状态（`active/rotated/revoked`）、轮换链
- 行为：`rotate()`、`revoke()`、`detectReuse()`
- 不变量：
  - 轮换后旧 token 立即失效；
  - 已轮换 token 再次被使用 → **吊销整个 family**（令牌盗用检测，RFC 6819 的领域化实现）。

**聚合根 3：`Consent`（用户同意）**
- 组成：consentId、`UserId`、`ClientId`、`ScopeSet`、状态、时间戳
- 独立聚合的理由：同意必须**跨登录会话持久**（不能每次登录重新点同意），且撤销同意要级联吊销该用户在该客户端的所有令牌（事件驱动，见 §3）。
- 行为：`grant()`、`amend()`（只能显式追加 scope，追加需用户再次确认）、`revoke()`

**实体**：无——授权码、claims 均为值对象，保证不可变。

**值对象**：`Scope`（自带 RFC 6749 §3.3 语法解析与校验，消除裸 `std::string` 的注入/拼接缺陷）、`ScopeSet`、`RedirectUri`（精确匹配语义内聚）、`PKCECodeChallenge`（+method）、`Nonce`、`AuthorizationCodeValue`、`Jti`、`TokenClaims`、`ResourceIndicator`、`IdTokenClaims`（OIDC 标准声明集——公开语言）。

**领域服务**：
- `TokenIssuanceService`（按 grant_type 签发：auth-code / refresh / client-credentials）
- `TokenIntrospectionService`（RFC 7662）
- `TokenRevocationService`（RFC 7009，含级联规则：吊销 refresh → 级联其签发的 access）
- `ScopeResolutionService`（Tier-1 客户端允许域 ∩ Tier-2 用户角色可授域，与 BC5 协作）
- `DiscoveryService` / `JWKSService`（`/.well-known/*` 公开语言出口）

**领域事件**：`AuthorizationRequested`、`ConsentRequired`、`ConsentGranted`、`ConsentRevoked`、`AuthorizationCodeIssued`、`AuthorizationCodeConsumed`、`AuthorizationCodeReuseDetected`、`AccessTokenIssued`、`RefreshTokenRotated`、`RefreshTokenReuseDetected`、`TokenRevoked`、`BackchannelLogoutDispatched`。

**仓储接口**：`IAuthorizationGrantRepository`、`IRefreshTokenRepository`、`IConsentRepository`、`IAccessTokenStore`（可选——访问令牌默认**无状态 JWT**，不建聚合；仅引用型令牌（introspection 需要）才有此存储，作为读模型）。

### 2.4 客户端上下文（BC4）

**聚合根：`Client`（OAuth 客户端）**
- 组成：`ClientId`、名称、`ClientType`（confidential/public）、状态、`ClientSecret` 实体列表、allowedGrantTypes、allowedScopes、redirectUris、`TokenEndpointAuthMethod`、令牌 TTL 策略、backchannelLogoutUri、所属 `TenantId`
- 行为：`register()`、`rotateSecret()`、`addRedirectUri()`、`requirePkce()`、`disable()`
- 不变量：
  - **公共客户端不得持有 secret，且必须强制 PKCE**；
  - redirect_uri 严格精确匹配（不前缀、不通配）；
  - secret 只存 `hash+salt`，支持多把共存直至旧过期（平滑轮换）；任何 API 不回显。

**实体**：`ClientSecret`（可轮换、带过期时间，多把共存）。

**值对象**：`ClientId`、`ClientType`、`ClientSecretHash`、`GrantType`、`TokenEndpointAuthMethod`、`TokenTTLPolicy`。

**领域服务**：`ClientSecretHashingService`。

**领域事件**：`ClientRegistered`、`ClientSecretRotated`、`ClientUpdated`、`ClientDisabled`（`ClientUpdated/Disabled` 被 BC3 消费以失效客户端缓存）。

**仓储接口**：`IClientRepository`。

### 2.5 访问控制上下文（BC5）

**聚合根 1：`Role`（角色）**：roleId、名称、权限键集合、租户；行为 `grantPermission()`/`revokePermission()`。

**聚合根 2：`Permission` / `ScopeDefinition`（权限/作用域注册表）**：key、显示名、层级（basic/elevated/admin）、状态；行为 `register()`/`deprecate()`。注册表驱动 Tier-2 授权（作用域是否需要管理员角色等由数据而非硬编码决定）。

**聚合根 3：`RoleAssignment`（角色分配）**
- 组成：assignmentId、`RoleId`（引用）、主体（userId | clientId | groupId）、作用域（租户/组织/资源）、状态、`expiresAt`
- **独立于 Role 的理由**：分配是高频写、带审计与到期语义；若放进 Role 聚合，海量用户的分配会让聚合边界爆炸，也违反"小聚合"原则。

**领域服务**：`PolicyDecisionService`（PDP：subject + action + resource → allow/deny）、`ClaimsEnrichmentService`（为 BC3 的令牌注入 roles/permissions 声明）。

**领域事件**：`RoleCreated`、`PermissionRegistered`、`RoleAssigned`、`RoleAssignmentRevoked`、`ScopeTierChanged`（BC3 消费以失效 claims 缓存）。

**仓储接口**：`IRoleRepository`、`IPermissionRepository`、`IRoleAssignmentRepository`。

### 2.6 租户上下文（BC6）

**聚合根**：
- `Tenant`：tenantId、slug、名称、状态、`PlanQuota`（用户数/客户端数/令牌速率）；行为 `create()`/`suspend()`/`changeQuota()`；不变量：slug 全局唯一。
- `Organization`（可选）：树形组织单元， membership 引用 `UserId`。
- `Invitation`：email、tenantId、预置角色、状态、过期；行为 `invite()`/`accept()`/`expire()`。

**领域事件**：`TenantCreated`、`TenantSuspended`、`QuotaExceeded`、`InvitationAccepted`。

**集成方式**：`TenantId` 进共享内核，所有上下文的仓储查询隐式带租户过滤（行级隔离）；隔离策略升级（行级 → schema → 独立库）是**基础设施决策**，领域模型不变。

### 2.7 审计上下文（BC7）

- **聚合根**：`AuditEntry`（追加式）：entryId、`ActorId`、`Action`、`TargetRef`、`Outcome`、`ContextSnapshot`（IP/UA/traceId）、occurredAt
- 不变量：**不可变、只增、有序**；无 update/delete 行为；按保留策略归档。
- 消费**所有**上下文的领域事件（经事件总线），同时对外提供 SIEM 订阅接口。
- 该上下文没有引发下游变化的领域事件（终端上下文）。

### 2.8 通用子域（简述）

- **密钥管理**：`SigningKey` 聚合（kid、alg、状态、生效窗口、轮换）；JWKS 端点由 BC3 的 DiscoveryService 代理出口。
- **通知**：无丰富模型，消费 `UserRegistered`/`PasswordResetRequested` 等事件触发邮件/短信，是典型的通用子域。

---

## 3. 跨上下文集成与一致性

### 3.1 领域事件目录（集成契约）

| 事件 | 生产者 | 消费者 | 触发的下游行为 |
|---|---|---|---|
| `UserRegistered` | BC1 | BC7、通知 | 审计记录、发欢迎/验证邮件 |
| `UserSuspended` | BC1 | BC2、BC3、BC7 | 终止登录会话；**流程管理器吊销全部令牌**；审计 |
| `LockoutApplied` | BC2 | BC1、BC7 | 账号状态置 locked；审计 |
| `LoginSucceeded` / `LoginFailed` | BC2 | BC7 | 审计（失败用于风控分析） |
| `CredentialRevoked` | BC2 | BC3、BC7 | 该用户下次需重新认证 |
| `ConsentRevoked` | BC3 | BC3 内部流程管理器、BC7 | **级联吊销**该 user+client 全部 access/refresh 令牌 |
| `AuthorizationCodeReuseDetected` | BC3 | BC3 内部、BC7 | 吊销 grant 全部令牌；告警 |
| `RefreshTokenReuseDetected` | BC3 | BC3 内部、BC7 | 吊销整个令牌族；告警 |
| `ClientSecretRotated` / `ClientUpdated` / `ClientDisabled` | BC4 | BC3、BC7 | 失效客户端缓存；Disabled 则拒绝新授权 |
| `RoleAssigned` / `RoleAssignmentRevoked` | BC5 | BC3、BC7 | 失效该用户 claims 缓存（下次签发生效） |
| `QuotaExceeded` / `TenantSuspended` | BC6 | BC2、BC3、BC7 | 限制新会话/新客户端注册 |
| `TokenRevoked`（含 logout） | BC3 | BC7、RP | 审计；backchannel logout 通知依赖方 |

### 3.2 集成机制与一致性策略

| 层级 | 一致性 | 机制 |
|---|---|---|
| 聚合内 | **强一致** | 单事务提交聚合整体 |
| 上下文内跨聚合 | 准强一致 | 同库事务 + 领域事件（进程内分发） |
| 跨上下文 | **最终一致**（通常亚秒级） | **事务发件箱（Outbox）模式**：业务写入与事件写入同一事务，后台中继发布到事件总线 |
| 同步查询 | 读时一致 | 进程内 API（模块化单体阶段）/ REST（微服务阶段），高频只读数据配缓存 + 事件失效 |

三个工程要点：

1. **Outbox 是跨上下文事件的唯一发布通道**——避免"业务提交了、事件丢了"或"事件发了、业务回滚了"。
2. **安全敏感级联用流程管理器（Saga）保证完成**：`UserSuspended → 吊销令牌`、`ConsentRevoked → 级联吊销` 这类链路必须有重试与对账，不能依赖尽力而为的通知。
3. **演进路径**：单体阶段上下文 = 模块边界（进程内接口 + 同库 outbox）；拆分阶段同一事件契约直接上消息总线，**领域模型不改**——这是上下文映射先行的回报。

---

## 4. 分层架构与各层职责

```
┌───────────────────────────────────────────────────────────────┐
│ 接口层 Interface / Presentation                                 │
│  OIDC/OAuth2 协议端点(/authorize /token /introspect /revocation │
│  /userinfo /.well-known/*)、管理端 REST、请求 DTO 与参数校验、   │
│  限流、协议错误映射(RFC 6749 error 格式)                        │
├───────────────────────────────────────────────────────────────┤
│ 应用层 Application                                             │
│  用例服务(LoginUseCase / AuthorizeUseCase / IssueTokenUseCase…)│
│  编排聚合与领域服务、事务边界+Outbox、领域事件发布入口、         │
│  访问控制检查(调 PDP)、CQRS 命令/查询分离、DTO↔领域转换          │
├───────────────────────────────────────────────────────────────┤
│ 领域层 Domain（每个限界上下文一个模块）                          │
│  聚合根/实体/值对象、领域服务、仓储接口、领域事件、               │
│  策略(密码/锁定/TTL)、工厂。零框架依赖                          │
├───────────────────────────────────────────────────────────────┤
│ 基础设施层 Infrastructure                                      │
│  仓储实现(ORM/SQL)、缓存(Redis)、事件总线与Outbox中继、          │
│  密码哈希/JWT签名、时间与ID生成、外部IdP适配器(ACL)、邮件适配器  │
└───────────────────────────────────────────────────────────────┘
依赖方向：接口层 → 应用层 → 领域层 ← 基础设施层（依赖倒置）
```

**各层职责与禁区**：

- **接口层**：只做协议翻译。参数校验（格式层）、认证结果到 HTTP 状态/错误码的映射、DTO 组装。**禁止**出现任何业务判断（如"scope 是否允许"是聚合的行为，不是控制器的 if）。
- **应用层**：用例编排。一个用例 = 一个事务 = 修改一个聚合（+outbox 记录）。负责调用 PDP 做功能级访问控制、触发领域事件外发、组织 CQRS 读模型查询。**禁止**包含领域规则（不变量全在聚合/领域服务里），也**禁止**直接触碰 SQL/ORM。
- **领域层**：业务规则的唯一居所。聚合不变量、跨聚合纯逻辑（领域服务）、策略对象、仓储**接口**与领域事件定义。**禁止** import 任何框架/DB/HTTP 类型。
- **基础设施层**：实现领域层接口 + 提供技术能力。仓储实现（每个上下文独立的存储模块）、Redis 缓存装饰器（如客户端缓存）、密码哈希与 JWT 签名实现、事件中继、外部 IdP 的防腐适配器、时钟与随机源（领域层通过接口消费，保证可测试）。

**CQRS（轻量）**：命令侧走聚合（写模型，强一致）；查询侧（管理端的用户分页/搜索、审计检索、已授权应用列表）直接走**读模型**（SQL 投影），绕过聚合以获得性能——聚合为写一致性而设，不该被列表查询拖累。

**代码组织（按上下文分包，而非按层分包）**：

```
libs/
  identity/          # BC1: model/ repository/ service/ events/
  authentication/    # BC2
  oauth2/            # BC3 (授权协议域)
  clients/           # BC4
  access-control/    # BC5
  tenancy/           # BC6
  audit/             # BC7
  common/            # 共享内核: 标识类型 + 事件契约
apps/server/         # 组装根(组合根): 依赖注入、上下文接线
frontends/           # admin / user 前端(经 OHS 消费)
```

---

## 5. 端到端示例：授权码 + PKCE 登录（贯穿全部上下文与层次）

```
浏览器         接口层              应用层                      领域层(各上下文)
  │ GET /authorize?client_id&redirect_uri&scope&code_challenge
  ├──────────────►│ AuthorizeEndpoint 校验参数格式
  │               │ ──AuthorizeUseCase──► ①BC4: Client.isRegisteredRedirectUri()
  │               │                         allowsAllScopes()   [读缓存]
  │               │                    ②BC2: 无SSO会话 → AuthenticationSession.begin()
  │ ◄─302 /login──│
  │ POST 密码+TOTP │ LoginEndpoint           ③BC2: Credential.verify()
  │               │                         AuthenticationSession 状态机推进
  │               │                    ④BC1: 读 User 状态(active? email verified?)
  │               │                    ⑤BC3: Consent 命中? 否→ ConsentRequired
  │ ◄─302 同意页──│                         用户确认 → Consent.grant()
  │               │                    ⑥BC3: AuthorizationGrant.issueCode()
  │               │                         [code 绑定 PKCE challenge+nonce, TTL 60s]
  │ ◄─302 redirect?code=…──│               事件: AuthorizationCodeIssued → Outbox
  │ POST /token(code+verifier)             ⑦BC3: consumeCode() [单次使用+绑定比对]
  │               │                         PKCE S256 校验 → ScopeResolutionService
  │               │                         TokenIssuanceService → access(JWT)+refresh(新族)
  │ ◄─200 tokens──│                         事件: AccessTokenIssued → Outbox → BC7 审计
  │ 后续: 刷新/吊销/introspection / userinfo / backchannel-logout 全在 BC3 内闭环
```

对应的查询侧例子：管理端用户列表（分页+搜索）走应用层 `ListUsersQuery` → 读模型 SQL 投影，不实例化任何 `User` 聚合。

---

## 6. 附录：与 fulla 现状的映射

本仓库的模块化重构已在朝这个模型演进，映射与差距如下（基于当前 `libs/` 结构核实）：

| 目标模型 | fulla 现状 | 差距 / 建议方向 |
|---|---|---|
| BC3 授权上下文 | `libs/oauth2`（`protocol/ClientService`、`repository/IConsentRepository`、TokenService、consent/grant 相关） | 已具雏形；`AuthorizationGrant` 的不变量（码单次使用、PKCE 绑定）多散在服务里，可上收进聚合 |
| BC4 客户端聚合 | `libs/oauth2/model/Client.h` 自述为 "the Client aggregate"（含 `isRegisteredRedirectUri`/`allowsAllScopes` 不变量） | 方向一致；目前是 DTO 薄包装，`ClientSecret` 轮换实体可显式化 |
| BC1+BC2 | `libs/identity`（AuthService、SessionManager、MfaService、IUserRepository 等） | 身份与认证尚未物理分上下文；`SessionManager` 的登录策略判定（`evaluateLoginPolicy`）正是 BC2 领域服务 |
| 跨上下文边界 | `libs/drogon` 的 `IdentityService` 同时做角色查询、subject 映射、consent 读写 | 按目标模型应拆：consent 归 BC3、角色归 BC5、subject 映射归 BC1/BC2 桥接 |
| 接口层 | `libs/drogon/.../controllers`（TokenEndpointController、SessionController） | 符合"协议翻译"定位；控制器内业务 if 链可继续下沉 |
| 基础设施层 | `libs/storage-memory` / `storage-postgres` / `storage-redis`（+客户端缓存装饰器） | 符合"仓储实现在基础设施"的依赖倒置 |
| 领域事件 + Outbox | 目前为隐式集成（如 `IBackchannelLogoutNotifier` 是对外适配器） | 最大缺口：无统一事件契约与发件箱，跨上下文级联（停用→吊销令牌）尚无最终一致机制 |
| BC6 租户上下文 | 暂无 | 未来多租户改造时按 §2.6 引入，`TenantId` 先进共享内核 |
