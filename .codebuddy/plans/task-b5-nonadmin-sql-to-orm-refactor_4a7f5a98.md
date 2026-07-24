---
name: task-b5-nonadmin-sql-to-orm-refactor
overview: 将10个非管理员控制器中的51处原生SQL查询重构为ORM Mapper + Service层模式。第一步先补全model.json配置（4个缺失表+关系）、更新/orm-gen skill文档、重新生成全部ORM模型文件；然后再创建Service类、重构控制器、验证测试。
todos:
  - id: update-model-json
    content: 补全 OAuth2Server/model.json 和 libs/storage-postgres/src/models/model.json：添加 email_verification_tokens、password_reset_tokens、oauth2_device_codes、webauthn_credentials 四个表 + 四条 relationships，并修正 OAuth2Server/model.json 的 user 字段为 oauth2_user
    status: completed
  - id: update-orm-gen-skill
    content: 更新 .codebuddy/skills/orm-gen/SKILL.md：补充全部19个表到文档中的表列表、预期输出文件数更新为38个（19.h + 19.cc）
    status: completed
  - id: regenerate-orm-models
    content: 使用 [skill:orm-gen] 执行 generate_models.bat 重新生成全部ORM模型，将生成的 .h 和 .cc 文件迁移到 libs/storage-postgres/include/authforge/storage/postgres/models/ 和 libs/storage-postgres/src/models/
    status: completed
    dependencies:
      - update-model-json
      - update-orm-gen-skill
  - id: create-email-verification-service
    content: 创建 EmailVerificationService（email验证令牌CRUD + users.email_verified更新），重构 EmailVerificationController
    status: completed
    dependencies:
      - regenerate-orm-models
  - id: create-password-reset-service
    content: 创建 PasswordResetService（密码重置令牌CRUD + users密码更新 + 批量token吊销豁免），重构 PasswordResetController
    status: completed
    dependencies:
      - regenerate-orm-models
  - id: create-device-code-service
    content: 创建 DeviceCodeService（设备授权码CRUD），重构 DeviceAuthController 和 OAuth2StandardController 中的设备流SQL
    status: completed
    dependencies:
      - regenerate-orm-models
  - id: create-user-self-service
    content: 创建 UserSelfService（用户profile查询、密码修改、账户删除、授权应用管理 + 跨表查询拆分），重构 UserSelfServiceController
    status: completed
    dependencies:
      - regenerate-orm-models
  - id: create-client-registration-service
    content: 创建 ClientRegistrationService（动态客户端注册），重构 ClientRegistrationController
    status: completed
    dependencies:
      - regenerate-orm-models
  - id: refactor-mfa-webauthn-session-github
    content: 重构 MfaController、WebAuthnController、SessionController、GitHubController 中的原生SQL，委托至 UserSelfService 和已有 identity Service
    status: completed
    dependencies:
      - create-user-self-service
  - id: verify-all-tests
    content: 使用 [skill:build-and-test] 编译并运行完整测试套件，确认51个调用点全部通过，然后使用 [skill:code-review] 进行代码审查
    status: completed
    dependencies:
      - create-email-verification-service
      - create-password-reset-service
      - create-device-code-service
      - create-user-self-service
      - create-client-registration-service
      - refactor-mfa-webauthn-session-github
---

## 用户需求

执行任务B5：将非管理员模块的原生SQL查询重构为ORM Mapper并提取相关Service层逻辑。

**第一步已修正为**：

1. 补全项目内所有 model.json（`libs/storage-postgres/src/models/model.json` 和 `OAuth2Server/model.json`），对照 migration 目录下的 .sql 文件，添加全部缺失的表并补全表间关系
2. 更新 `/orm-gen` skill 文档（`.codebuddy/skills/orm-gen/SKILL.md`），补充全部19个表信息
3. 执行 generate_models.bat 重新生成全部 ORM 模型文件，并放入正确的目标目录（`libs/storage-postgres/include/authforge/storage/postgres/models/` 和 `libs/storage-postgres/src/models/`）

## 涉及范围

- **ORM模型生成**：补全 model.json（从15表→19表），新增4表：email_verification_tokens、password_reset_tokens、oauth2_device_codes、webauthn_credentials，添加4条 relationships
- **Service层创建**：5个新 Service 类（EmailVerificationService、PasswordResetService、DeviceCodeService、UserSelfService、ClientRegistrationService）
- **控制器重构**：10个非管理员控制器，51处原生SQL替换为 Mapper+Criteria
- **合法豁免保留**：UPDATE...RETURNING、DELETE...RETURNING、SELECT 1健康检查、批量token吊销UPDATE
- **验收标准**：全部测试通过（Unit + Integration + E2E + Security + Performance）

## 技术栈

- 语言：C++17
- 框架：Drogon (HTTP + ORM)
- 数据库：PostgreSQL (生产) / Memory (测试)
- 构建：CMake + Conan
- 测试：Google Test (drogon_test.h)
- ORM生成：drogon_ctl (通过 generate_models.bat)

## 实现方案

### 阶段一：ORM模型体系补全（第1步）

#### model.json 修改

**主要修改文件**：`OAuth2Server/model.json`（drogon_ctl 实际读取的配置）、`libs/storage-postgres/src/models/model.json`（参考配置）

**OAuth2Server/model.json** 当前13表、user为"test"、8条关系 → 修改为：

- tables 数组追加：`"organizations"`, `"audit_logs"`, `"email_verification_tokens"`, `"password_reset_tokens"`, `"oauth2_device_codes"`, `"webauthn_credentials"`
- user 字段改为 `"oauth2_user"`
- relationships.items 追加4条新关系

**libs/storage-postgres/src/models/model.json** 当前15表、9条关系 → 修改为：

- tables 数组追加：`"email_verification_tokens"`, `"password_reset_tokens"`, `"oauth2_device_codes"`, `"webauthn_credentials"`
- relationships.items 追加同4条关系

#### 新增4条 relationships

```
{"type":"has many","original_table_name":"users","original_table_alias":"user","original_key":"id","target_table_name":"email_verification_tokens","target_table_alias":"emailVerificationTokens","target_key":"user_id","enable_reverse":false}
{"type":"has many","original_table_name":"users","original_table_alias":"user","original_key":"id","target_table_name":"password_reset_tokens","target_table_alias":"passwordResetTokens","target_key":"user_id","enable_reverse":false}
{"type":"has many","original_table_name":"oauth2_clients","original_table_alias":"client","original_key":"client_id","target_table_name":"oauth2_device_codes","target_table_alias":"deviceCodes","target_key":"client_id","enable_reverse":false}
{"type":"has many","original_table_name":"users","original_table_alias":"user","original_key":"id","target_table_name":"webauthn_credentials","target_table_alias":"webauthnCredentials","target_key":"user_id","enable_reverse":false}
```

#### /orm-gen skill 文档更新

`.codebuddy/skills/orm-gen/SKILL.md`：将9表列表扩展为19表完整列表，更新预期输出文件数为38个（19个.h + 19个.cc），添加新增表说明。

#### 执行生成与文件迁移

`generate_models.bat` 读取 `OAuth2Server/model.json`，输出到 `OAuth2Plugin/src/models/`（.cc）和 `OAuth2Plugin/include/oauth2/models/`（.h）。需将生成文件迁移至：

- .h → `libs/storage-postgres/include/authforge/storage/postgres/models/`
- .cc → `libs/storage-postgres/src/models/`

### 阶段二：Service层创建与控制器重构（第2-7步）

遵循 admin 模块已验证的模式（ClientManagementService等）：无状态 Service 类，使用 Mapper+Criteria 三元组。

#### db-operations 规范遵守

- SELECT → `Mapper::findOne(Criteria)` / `Mapper::findBy(Criteria)`
- INSERT → `Mapper::insert(Model)`
- UPDATE → `Mapper::findBy(Criteria)` → `Mapper::update(Model)`（除豁免项外）
- DELETE → `Mapper::findBy(Criteria)` → `Mapper::destroy(Model)`
- JOIN 禁止 → 拆分为多个查询或使用 `Criteria::In`

#### 合法豁免保留

| 位置 | SQL | 豁免理由 |
| --- | --- | --- |
| PasswordResetController:225 | `UPDATE ... RETURNING user_id` | UPDATE...RETURNING |
| EmailVerificationController:138 | `DELETE ... RETURNING user_id, email` | DELETE...RETURNING |
| HealthController:82 | `SELECT 1` | 健康检查 |
| PasswordResetController:260/263 | 批量 `UPDATE SET revoked=true WHERE user_id=$1` | 文档化批量 |
| UserSelfServiceController:252/256等 | 同上批量吊销 | 文档化批量 |


#### Service类设计

| Service | 文件 | 职责 | 方法 |
| --- | --- | --- | --- |
| EmailVerificationService | libs/drogon/src/services/ | 验证令牌CRUD + users.email_verified更新 | sendVerificationEmail, verifyToken, resendVerification |
| PasswordResetService | libs/drogon/src/services/ | 重置令牌CRUD + users密码更新 + 批量吊销豁免 | requestReset, confirmReset |
| DeviceCodeService | libs/drogon/src/services/ | oauth2_device_codes CRUD | createDeviceCode, approveDevice, consumeDeviceCode |
| UserSelfService | libs/drogon/src/services/ | users查询/更新、跨表查询（consents+clients）、MFA/WebAuthn用户操作 | getProfile, changePassword, listAuthorizedApps, revokeAuthorizedApp, deleteAccount |
| ClientRegistrationService | libs/drogon/src/services/ | oauth2_clients INSERT | registerClient |


#### 控制器映射

| 控制器 | 委托Service | SQL数 | 豁免保留 |
| --- | --- | --- | --- |
| EmailVerificationController | EmailVerificationService | 4 | 1 |
| PasswordResetController | PasswordResetService | 6 | 3 |
| DeviceAuthController | DeviceCodeService | 2 | 0 |
| OAuth2StandardController(device) | DeviceCodeService | 2 | 0 |
| UserSelfServiceController | UserSelfService | 13 | 4 |
| ClientRegistrationController | ClientRegistrationService | 1 | 0 |
| HealthController | 无需Service | 1 | 1 |
| SessionController | UserSelfService | 1 | 0 |
| MfaController | UserSelfService | 6 | 0 |
| WebAuthnController | UserSelfService | 4 | 0 |
| GitHubController | UserSelfService | 8 | 0 |


### Service 实现模式

采用 admin 模块的静态方法模式（参考 ClientManagementService）：

```cpp
class UserSelfService {
public:
    using ResponseCallback = std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;
    static void getProfile(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
    static void changePassword(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
    // ...
};
```

每个方法内部：获取 DbClient → 构建 Criteria → 调用 Mapper → 在 async callback 中构建响应。

### 跨表查询处理（listAuthorizedApps）

原SQL为 JOIN 查询（oauth2_user_consents JOIN oauth2_clients），拆分为两步：

1. `Mapper<Oauth2UserConsents>::findBy(Criteria)` → 获取 consent 列表及 client_id
2. 收集所有 client_id → `Mapper<Oauth2Clients>::findBy(Criteria::In(...))` → 获取 client name

## 目录结构

```
libs/storage-postgres/
├── src/models/
│   └── model.json                        # [MODIFY] 补全4表+4关系
└── include/authforge/storage/postgres/models/
    ├── EmailVerificationTokens.h         # [NEW] 自动生成
    ├── Oauth2DeviceCodes.h              # [NEW] 自动生成
    ├── PasswordResetTokens.h            # [NEW] 自动生成
    └── WebauthnCredentials.h            # [NEW] 自动生成

OAuth2Server/
└── model.json                            # [MODIFY] 补全6表+4关系，修正user字段

.codebuddy/skills/orm-gen/
└── SKILL.md                              # [MODIFY] 19表完整列表

libs/drogon/
├── include/authforge/drogon/services/     # [NEW] Service层头文件目录
│   ├── EmailVerificationService.h
│   ├── PasswordResetService.h
│   ├── DeviceCodeService.h
│   ├── UserSelfService.h
│   └── ClientRegistrationService.h
├── src/services/                         # [NEW] Service层实现目录
│   ├── EmailVerificationService.cc
│   ├── PasswordResetService.cc
│   ├── DeviceCodeService.cc
│   ├── UserSelfService.cc
│   └── ClientRegistrationService.cc
└── CMakeLists.txt                        # [MODIFY] 无需修改（GLOB_RECURSE自动包含）

libs/drogon/src/controllers/              # [MODIFY] 10个控制器
├── EmailVerificationController.cc
├── PasswordResetController.cc
├── DeviceAuthController.cc
├── OAuth2StandardController.cc
├── UserSelfServiceController.cc
├── ClientRegistrationController.cc
├── SessionController.cc
├── MfaController.cc
├── WebAuthnController.cc
└── GitHubController.cc
```

## Agent Extensions

### Skill

- **orm-gen**
- 用途：重新生成全部19个表的 Drogon ORM 模型文件
- 预期结果：在 `OAuth2Plugin/src/models/` 下生成19个 .cc 文件，在 `OAuth2Plugin/include/oauth2/models/` 下生成19个 .h 文件，随后迁移至 `libs/storage-postgres/`

- **build-and-test**
- 用途：重构完成后编译后端代码并运行完整测试套件，验证51个调用点全部正常工作
- 预期结果：编译通过，`ctest -R Unit|Integration|E2E|Security|Performance` 全部测试通过

- **code-review**
- 用途：完成所有代码修改后，对重构的 Service 类和修改的控制器进行系统化代码审查
- 预期结果：确认所有修改符合 db-operations 规范、编码规范、安全性和正确性要求