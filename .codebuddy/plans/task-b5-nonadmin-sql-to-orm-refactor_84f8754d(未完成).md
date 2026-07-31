---
name: task-b5-nonadmin-sql-to-orm-refactor
overview: 将10个非管理员控制器中的51处原生SQL查询重构为ORM Mapper + Service层模式，严格遵循db-operations.md规范。3个缺失ORM模型的表需先通过drogon_ctl生成模型，再创建/扩展对应Service类承接业务逻辑，控制器仅保留请求解析和响应构造。
todos:
  - id: generate-orm-models
    content: 使用 [skill:orm-gen] 为 email_verification_tokens、password_reset_tokens、oauth2_device_codes 三个表生成 ORM 模型文件
    status: pending
  - id: create-email-verification-service
    content: 创建 EmailVerificationService（email验证令牌CRUD + users.email_verified更新），重构 EmailVerificationController
    status: pending
    dependencies:
      - generate-orm-models
  - id: create-password-reset-service
    content: 创建 PasswordResetService（密码重置令牌CRUD + users密码更新 + 批量token吊销豁免），重构 PasswordResetController
    status: pending
    dependencies:
      - generate-orm-models
  - id: create-device-code-service
    content: 创建 DeviceCodeService（设备授权码CRUD），重构 DeviceAuthController 和 OAuth2StandardController 中的设备流SQL
    status: pending
    dependencies:
      - generate-orm-models
  - id: create-user-self-service
    content: 创建 UserSelfService（用户profile查询、密码修改、账户删除、授权应用管理 + 跨表查询拆分），重构 UserSelfServiceController
    status: pending
    dependencies:
      - generate-orm-models
  - id: create-client-registration-service
    content: 创建 ClientRegistrationService（动态客户端注册），重构 ClientRegistrationController
    status: pending
  - id: refactor-mfa-webauthn-session-github
    content: 重构 MfaController、WebAuthnController、SessionController、GitHubController 中的原生SQL，委托至 UserSelfService 和已有 identity Service
    status: pending
    dependencies:
      - create-user-self-service
  - id: verify-all-tests
    content: 使用 [skill:build-and-test] 编译并运行完整测试套件（Unit + Integration + E2E + Security + Performance），确认51个调用点全部通过
    status: pending
    dependencies:
      - create-email-verification-service
      - create-password-reset-service
      - create-device-code-service
      - create-user-self-service
      - create-client-registration-service
      - refactor-mfa-webauthn-session-github
---

## 用户需求

执行任务B5：将非管理员模块的原生SQL查询重构为ORM Mapper并提取相关Service层逻辑。涉及10个控制器、51处原生SQL调用点。

## 产品概述

重构非管理员模块的数据访问层，将控制器中内联的原生SQL全部替换为标准的ORM Mapper操作（async callback + Mapper + Criteria组合），同时将业务逻辑从控制器提取到独立的Service类中。重构后的代码遵循`db-operations.md`和`data-access.md`规范，保持所有接口请求与响应行为不变。

## 核心功能

- 生成3个缺失表的ORM模型（email_verification_tokens、password_reset_tokens、oauth2_device_codes）
- 创建5个新Service类，覆盖10个控制器的51处SQL调用
- 将原生SQL替换为Mapper + Criteria操作，符合db-operations规范
- 保留合规的Raw SQL豁免项（UPDATE...RETURNING、DELETE...RETURNING、健康检查SELECT 1、批量token吊销）
- 控制器重构为纯路由层：解析请求→调用Service→返回响应
- 所有现有测试通过验收

## 技术栈

- 语言：C++17
- 框架：Drogon (HTTP + ORM)
- 数据库：PostgreSQL (生产) / Memory (测试)
- 构建：CMake + Conan
- 测试：Google Test (drogon_test.h)

## 实现方案

### 总体策略

遵循admin模块已验证的重构模式（ClientManagementService等）：创建无状态的Service类，每个方法直接使用`Mapper<T>` + `Criteria`进行数据库操作。Service类位于`libs/drogon/src/`，命名空间`authforge::drogon::services`。控制器变为纯委托层，仅做请求解析和响应返回。

### 数据库操作规范

所有非豁免SQL严格使用三元组：**Async Callback + Mapper API + Criteria**：

- SELECT → `Mapper::findOne(Criteria)` / `Mapper::findBy(Criteria)`
- INSERT → `Mapper::insert(Model)`
- UPDATE → `Mapper::findBy(Criteria)` → 遍历结果 → `Mapper::update(Model)`
- DELETE → `Mapper::findBy(Criteria)` → 遍历结果 → `Mapper::destroy(Model)`
- JOIN禁止 → 拆分为多个查询或使用`Criteria::In`

### Raw SQL豁免项（保留不动）

| 位置 | SQL类型 | 豁免理由 |
| --- | --- | --- |
| PasswordResetController:225 | `UPDATE ... RETURNING user_id` | UPDATE...RETURNING豁免 |
| EmailVerificationController:138 | `DELETE ... RETURNING user_id, email` | DELETE...RETURNING同类模式 |
| HealthController:82 | `SELECT 1` | 健康检查，非表操作 |
| PasswordResetController:260/263 | 批量`UPDATE ... SET revoked = true WHERE user_id = $1` | 文档化批量操作 |
| UserSelfServiceController:252/256等 | 批量`UPDATE ... SET revoked = true WHERE user_id = $1` | 文档化批量操作 |


### 缺失ORM模型生成

以下3个表没有ORM模型，需先通过`drogon_ctl`生成：

- `email_verification_tokens` (token_hash, user_id, email, expires_at, created_at)
- `password_reset_tokens` (token_hash, user_id, expires_at, used, created_at)
- `oauth2_device_codes` (device_code_hash, user_code, client_id, scope, status, user_id, expires_at, interval_seconds)

### Service类设计

#### 1. EmailVerificationService

- 文件：`libs/drogon/src/EmailVerificationService.cc` / `.h`
- 负责email_verification_tokens表的INSERT和users表的email_verified字段更新
- 方法：`sendVerificationEmail(userId, email, callback)`、`verifyToken(token, callback)`、`resendVerification(userId, callback)`

#### 2. PasswordResetService

- 文件：`libs/drogon/src/PasswordResetService.cc` / `.h`
- 负责password_reset_tokens表的INSERT和users表的SELECT/password更新
- 方法：`requestReset(email, callback)`、`confirmReset(token, newPassword, callback)`
- 内部包含UPDATE...RETURNING豁免SQL和批量token吊销豁免SQL

#### 3. DeviceCodeService

- 文件：`libs/drogon/src/DeviceCodeService.cc` / `.h`
- 负责oauth2_device_codes表的CRUD操作
- 方法：`createDeviceCode(...)`、`approveDevice(userCode, userId)`、`consumeDeviceCode(deviceCodeHash)`

#### 4. UserSelfService

- 文件：`libs/drogon/src/UserSelfService.cc` / `.h`
- 负责users表的profile查询、密码更新、账户删除，以及oauth2_user_consents和oauth2_clients的跨表查询
- 方法：`getProfile(userId)`、`changePassword(userId, oldPwd, newPwd)`、`listAuthorizedApps(userId)`、`revokeAuthorizedApp(userId, clientId)`、`deleteAccount(userId)`

#### 5. ClientRegistrationService

- 文件：`libs/drogon/src/ClientRegistrationService.cc` / `.h`
- 负责oauth2_clients表的INSERT操作
- 方法：`registerClient(requestBody, callback)`

### 控制器映射关系

| 控制器 | 调用的Service | SQL调用数 | 豁免保留数 |
| --- | --- | --- | --- |
| EmailVerificationController | EmailVerificationService | 4 | 1 (DELETE...RETURNING) |
| PasswordResetController | PasswordResetService | 6 | 3 (UPDATE...RETURNING + 批量UPDATE x2) |
| DeviceAuthController | DeviceCodeService | 2 | 0 |
| OAuth2StandardController (device部分) | DeviceCodeService | 2 | 0 |
| UserSelfServiceController | UserSelfService | 13 | 4 (批量UPDATE x4) |
| ClientRegistrationController | ClientRegistrationService | 1 | 0 |
| HealthController | 无需Service | 1 | 1 (SELECT 1) |
| SessionController | UserSelfService | 1 | 0 |
| MfaController | UserSelfService (mfa列操作) | 6 | 0 |
| WebAuthnController | UserSelfService (webAuthn列操作) | 4 | 0 |
| GitHubController | UserSelfService (users+映射查询) | 8 | 0 |


### Mapper操作示例（数据库操作三元组）

对于`SELECT id, email FROM users WHERE email = $1`：

```cpp
// Async callback + Mapper API + Criteria 三元组
Mapper<Users> mapper(db);
Criteria crit(Users::Cols::_email, CompareOperator::EQ, email);
mapper.findOne(crit,
  [sharedCb](const Users &user) { /* 处理结果 */ },
  [sharedCb](const DrogonDbException &e) { /* 处理错误 */ }
);
```

对于`INSERT INTO oauth2_device_codes (...) VALUES (...)`：

```cpp
Oauth2DeviceCodes code;
code.setDeviceCodeHash(hash);
code.setUserCode(userCode);
// ... 设置其他字段
Mapper<Oauth2DeviceCodes> mapper(db);
mapper.insert(code,
  [sharedCb](const Oauth2DeviceCodes &inserted) { /* 成功 */ },
  [sharedCb](const DrogonDbException &e) { /* 失败 */ }
);
```

对于`UPDATE users SET email_verified = true WHERE id = $1`：

```cpp
Mapper<Users> mapper(db);
Criteria crit(Users::Cols::_id, CompareOperator::EQ, userId);
mapper.findOne(crit,
  [sharedCb, db](const Users &user) {
    Users updated = user;
    updated.setEmailVerified(true);
    Mapper<Users>(db).update(updated,
      [sharedCb](const size_t) { /* 成功 */ },
      [sharedCb](const DrogonDbException &e) { /* 失败 */ }
    );
  },
  [sharedCb](const DrogonDbException &e) { /* 错误 */ }
);
```

### 跨表查询处理（JOIN替代方案）

对于`UserSelfServiceController::listAuthorizedApps`中的JOIN查询：

```
SELECT DISTINCT c.client_id, c.name
FROM oauth2_user_consents uc
JOIN oauth2_clients c ON uc.client_id = c.client_id
WHERE uc.internal_user_id = (SELECT id FROM users WHERE public_sub::text = $1)
```

替换为两步查询：

1. `Mapper<Oauth2UserConsents>::findBy(Criteria)` → 获取consent列表及client_id
2. 收集所有client_id → `Mapper<Oauth2Clients>::findBy(Criteria::In(...))` → 获取client name

## 架构设计

### 分层架构

```
Controller (HTTP路由层)
    ↓ 委托
Service (libs/drogon/src/) — 无状态，静态方法
    ↓ 使用
Mapper<T> + Criteria (Drogon ORM)
    ↓ 映射
Model (libs/storage-postgres/include/.../models/) — 自动生成，禁止手工编辑
    ↓ 对应
PostgreSQL Tables
```

### 目录结构

```
libs/drogon/
├── include/authforge/drogon/
│   ├── controllers/          # 现有控制器头文件（修改：移除SQL相关私有方法）
│   └── services/             # [NEW] Service层头文件目录
│       ├── EmailVerificationService.h
│       ├── PasswordResetService.h
│       ├── DeviceCodeService.h
│       ├── UserSelfService.h
│       └── ClientRegistrationService.h
├── src/
│   ├── controllers/          # 现有控制器实现（修改：替换SQL为Service调用）
│   │   ├── EmailVerificationController.cc
│   │   ├── PasswordResetController.cc
│   │   ├── DeviceAuthController.cc
│   │   ├── OAuth2StandardController.cc
│   │   ├── UserSelfServiceController.cc
│   │   ├── ClientRegistrationController.cc
│   │   ├── SessionController.cc
│   │   ├── MfaController.cc
│   │   ├── WebAuthnController.cc
│   │   └── GitHubController.cc
│   └── services/             # [NEW] Service层实现
│       ├── EmailVerificationService.cc
│       ├── PasswordResetService.cc
│       ├── DeviceCodeService.cc
│       ├── UserSelfService.cc
│       └── ClientRegistrationService.cc
└── CMakeLists.txt            # [MODIFY] 添加service源文件

libs/storage-postgres/include/authforge/storage/postgres/models/
├── EmailVerificationTokens.h  # [NEW] ORM模型
├── PasswordResetTokens.h      # [NEW] ORM模型
└── Oauth2DeviceCodes.h        # [NEW] ORM模型
```

## 实施说明

### 性能考虑

- 批量UPDATE操作（token吊销）保留为文档化批量豁免，避免N+1次Mapper::update调用
- 跨表查询使用两步查询替代JOIN，O(N+M)复杂度可接受
- Mapper::findOne/findBy都使用异步回调，不阻塞事件循环

### 安全注意事项

- 所有用户输入通过ORM Criteria参数绑定，避免SQL注入
- 不记录密码、token到日志（已有规范）
- 令牌哈希使用SHA-256（已有CryptoUtils）

### 兼容性保障

- 保持所有JSON响应格式不变
- 保持HTTP状态码不变
- 保持错误码（ErrorCatalog中的code）不变
- 保持异步回调签名一致

## Agent Extensions

### Skill

- **orm-gen**
- 用途：为email_verification_tokens、password_reset_tokens、oauth2_device_codes三个表生成ORM模型文件
- 预期结果：在`libs/storage-postgres/include/authforge/storage/postgres/models/`下生成三个新的.h文件，包含完整的Mapper类定义

- **build-and-test**
- 用途：重构完成后编译后端代码并运行全部测试套件，验证所有51个调用点正常工作
- 预期结果：编译通过，`ctest -R Unit|Integration|E2E|Security|Performance`全部测试通过

- **code-review**
- 用途：完成所有代码修改后，对重构的Service类和修改的控制器进行系统化代码审查
- 预期结果：确认所有修改符合OAuth2项目编码规范、db-operations规范、安全性和正确性要求