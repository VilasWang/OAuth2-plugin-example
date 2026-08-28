# AGENTS.md — libs/storage-postgres (fulla::storage::postgres)

> 本文件是模块级指令，聚焦 `libs/storage-postgres/` 的职责边界、关键文件路由和开发约束。
> 全局规则（DB 操作、数据访问、ORM 模型、开发流程）见根 `AGENTS.md` 和 `.claude/rules/`。

## 模块职责

**Adapter 层 Postgres 存储包**：实现 Domain 层（`libs/oauth2`、`libs/identity`）的 Repository 接口，基于 Drogon ORM 访问 PostgreSQL。

**职责边界**：
- ✅ ORM 模型类（`models/` 下由 `drogon_ctl` 生成，**禁止手改**）
- ✅ Postgres Repository 实现（实现 `fulla::oauth2::repository::*` 和 `fulla::identity::repository::*` 接口）
- ✅ Repository Bundle（聚合所有 Repository 的工厂类）
- ❌ **禁止** Domain 逻辑（业务规则在 `libs/oauth2`、`libs/identity`）
- ❌ **禁止** Drogon HTTP 依赖（仅允许 `drogon::orm`）

## 高频变更文件 Top 5

| 排名 | 文件 | 职责 | 开发约束 |
|------|------|------|----------|
| 1 | `src/PostgresIdentityRepository.cc` | 实现 `IUserRepository`、`IRoleRepository`、`ISubjectMappingRepository` | async callback + Mapper + Criteria；禁止 JOIN，拆成多查询或 `Criteria::In(...)`；每个 `Mapper<...>(db)` 构造独立 try-catch |
| 2 | `src/PostgresSocialAccountRepository.cc` | 实现 `ISocialAccountRepository`（社交账号绑定/解绑） | 同上；社交账号查询通过 `provider + provider_user_id` 组合 Criteria |
| 3 | `CMakeLists.txt` | 构建配置（ORM 模型源文件、Repository 源文件、链接依赖） | ORM 模型 `.cc` 豁免警告（`/wd4100`、`-Wno-unused-parameter`）；新增 Repository 需同时更新 `target_sources` |
| 4 | `src/PostgresTokenRepository.cc` | 实现 `ITokenRepository`（access token、refresh token CRUD） | Token 存储使用 SHA-256 哈希作为主键；refresh token 家族追踪（`V008__refresh_token_family.sql`） |
| 5 | `src/PostgresGrantRepository.cc` | 实现 `IGrantRepository`（授权码 CRUD） | 授权码一次性使用，换取 token 时必须在同一事务中删除授权码并插入 token |

## 关键文件路由

### ORM 模型（`include/fulla/storage/postgres/models/` + `src/models/*.cc`）
**⚠️ 由 `drogon_ctl` 从 schema 生成，禁止手改。要改模型就改 `apps/server/migrations/*.sql` 再运行 `/orm-gen`。**

- `Users.h/.cc`：用户表（`users`）
- `Oauth2Clients.h/.cc`：OAuth2 客户端（`oauth2_clients`）
- `Oauth2AccessTokens.h/.cc`：访问令牌（`oauth2_access_tokens`）
- `Oauth2RefreshTokens.h/.cc`：刷新令牌（`oauth2_refresh_tokens`）
- `Oauth2Codes.h/.cc`：授权码（`oauth2_codes`）
- `Oauth2Scopes.h/.cc`：scope 定义（`oauth2_scopes`）
- `Oauth2ClientScopes.h/.cc`：客户端授权 scope（`oauth2_client_scopes`）
- `Oauth2UserConsents.h/.cc`：用户授权同意（`oauth2_user_consents`）
- `Oauth2DeviceCodes.h/.cc`：设备授权码（`oauth2_device_codes`）
- `Oauth2SubjectMappings.h/.cc`：用户 sub 映射（`oauth2_subject_mappings`）
- `Roles.h/.cc`、`Permissions.h/.cc`、`RolePermissions.h/.cc`、`UserRoles.h/.cc`：RBAC 表
- `AuditLogs.h/.cc`：审计日志（`audit_logs`，分区表）
- `PasswordResetTokens.h/.cc`：密码重置令牌（`password_reset_tokens`）
- `EmailVerificationTokens.h/.cc`：邮箱验证令牌（`email_verification_tokens`）
- `WebauthnCredentials.h/.cc`：WebAuthn 凭证（`webauthn_credentials`）
- `Organizations.h/.cc`：多租户组织（`organizations`）

### Repository 接口实现（`src/*.cc` + `include/fulla/storage/postgres/*.h`）

#### Identity 层（实现 `libs/identity` 的接口）
- `PostgresIdentityRepository`：用户、角色、sub 映射（`IUserRepository`、`IRoleRepository`、`ISubjectMappingRepository`）
- `PostgresMfaRepository`：MFA 挑战/绑定（`IMfaRepository`）
- `PostgresWebAuthnRepository`：WebAuthn 凭证（`IWebAuthnRepository`）
- `PostgresSocialAccountRepository`：社交账号绑定（`ISocialAccountRepository`）

#### OAuth2 层（实现 `libs/oauth2` 的接口）
- `PostgresClientRepository`：OAuth2 客户端（`IClientRepository`）
- `PostgresGrantRepository`：授权码（`IGrantRepository`）
- `PostgresTokenRepository`：访问/刷新令牌（`ITokenRepository`）
- `PostgresConsentRepository`：用户同意（`IConsentRepository`）

#### 基础设施
- `PostgresRepositoryBase`：所有 Repository 的基类（持有 `drogon::orm::DbClient` 指针，提供通用 Criteria 构建辅助）
- `PostgresRepositoryBundle`：聚合所有 Repository 的工厂（`OAuth2Plugin` 在 `initAndStart()` 中构造）

### models_backup/（临时目录）
- `drogon_ctl` 重新生成的临时目录，**已 gitignore，不是迁移源**。
- 生成完成后手动检查差异，确认无误后删除。

## 开发约束

### 1. ORM 模型禁止手改
- `src/models/*.cc` 和 `include/fulla/storage/postgres/models/*.h` 由 `drogon_ctl` 生成。
- 要修改模型字段：
  1. 编辑 `apps/server/migrations/Vxxx__*.sql`（新增迁移文件）
  2. 运行 `./manage.sh orm-gen`（或 `./manage.ps1 orm-gen`）
  3. 检查 `models_backup/` 的差异，确认无误后删除
  4. 更新 Repository 代码适配新字段

### 2. DB 操作规则（详见 `.claude/rules/db-operations.md`）
- **接口选择**：async callback（首选）→ `Mapper::findBy`-with-future（受限）→ `CoroMapper`（禁止）
- **Callback 生命周期**：`std::make_shared<CallbackType>(std::move(cb))`，按值捕获进每层 lambda
- **Mapper 构造异常防护**：每个 `Mapper<...>(dbClient)` 构造都要独立 `try/catch`，包括嵌套在异步回调内的
- **JOIN 禁用**：拆成多个查询，或用 `Criteria::In(...)`
- **raw SQL 仅 6 种豁免**：DDL / `UPDATE ... RETURNING` / 文档化的批量操作 / `INSERT ... ON CONFLICT` / `SELECT 1` 探活 / 显式事务 `COMMIT`

### 3. 依赖方向
- ✅ `libs/storage-postgres` → `libs/common`、`libs/oauth2`、`libs/identity`（实现 Domain 端口）
- ✅ `libs/storage-postgres` → `Drogon::Drogon`（仅 `drogon::orm`，禁止 `drogon::HttpController` 等）
- ❌ `libs/oauth2`、`libs/identity` → `libs/storage-postgres`（Domain 层禁止存储依赖）

### 4. Repository 接口实现模式
```cpp
// 典型 Repository 方法签名（async callback）
void PostgresClientRepository::findByClientId(
    const std::string &clientId,
    std::function<void(Result<ClientDto>)> &&cb) const
{
    auto sharedCb = std::make_shared<decltype(cb)>(std::move(cb));
    try {
        Mapper<Oauth2Clients> mapper(dbClient_);
        mapper.findBy(
            Criteria(Oauth2Clients::Cols::client_id, clientId),
            [sharedCb](std::vector<Oauth2Clients> clients) {
                if (clients.empty()) {
                    (*sharedCb)(Result<ClientDto>::notFound());
                } else {
                    (*sharedCb)(Result<ClientDto>::success(toDto(clients[0])));
                }
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "DB error: " << e.base().what();
                (*sharedCb)(Result<ClientDto>::dbError(e.base().what()));
            }
        );
    } catch (const DrogonDbException &e) {
        LOG_ERROR << "Mapper construction failed: " << e.base().what();
        (*sharedCb)(Result<ClientDto>::dbError(e.base().what()));
    }
}
```

### 5. DTO 映射
- ORM 模型（`Oauth2Clients`）→ Domain DTO（`fulla::oauth2::Client`）的转换在 Repository 内部完成。
- DTO 定义在 `libs/oauth2/include/fulla/oauth2/model/` 和 `libs/identity/include/fulla/identity/model/`。
- 禁止将 ORM 模型暴露到 Domain 层（Repository 接口签名只使用 DTO）。

### 6. 事务
- 跨表操作（如授权码换 token：删除授权码 + 插入 access token + 插入 refresh token）必须使用显式事务。
- 事务内所有 Mapper 操作共享同一个 `Transaction` 对象。
- 事务失败时自动回滚，callback 返回错误结果。

### 7. 测试
- 存储契约测试在 `tests/contract/`，验证 Repository 接口行为（不依赖具体 DB 实现）。
- 集成测试在 `tests/integration/`，使用真实 Postgres 实例（Docker Compose）。
- 单元测试禁止直接连 DB，使用 Mock Repository。

## 相关规则

- 根 `AGENTS.md`：全局规则索引
- `.claude/rules/db-operations.md`：DB 操作规则（async callback + Mapper + Criteria）
- `.claude/rules/data-access.md`：存储层触发器（规则同上）
- `.claude/rules/orm-models.md`：ORM 模型禁止手改
- `.claude/rules/dev-workflow.md`：构建/测试命令
