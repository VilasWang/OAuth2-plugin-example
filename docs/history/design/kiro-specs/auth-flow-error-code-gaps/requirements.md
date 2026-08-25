# Requirements Document

## Introduction

本文档描述 `auth-flow-error-code-gaps` 特性的需求，该特性用于修复 `error-code-message-standardization` 特性发版后，在注册、登录及相邻认证流程（MFA、WebAuthn、设备授权、密码重置、邮箱验证、限流）中遗留的错误信息缺口。根因是若干控制器把同一代码路径中的多种不同失败原因折叠进同一个笼统 Error_Code，导致用户无法从响应中看出具体失败原因（例如注册时看不出是用户名冲突还是邮箱冲突）。

本特性依赖并扩展 `error-code-message-standardization` 特性已建成的基础设施（Error_Catalog、Error_Responder、Frontend_Error_Module），不重新定义这些基础设施本身的行为，只新增 9 条 Error_Catalog 条目、调整若干控制器/服务层的错误码映射，并同步补充两个前端应用的本地化消息目录。同时明确记录一项刻意保留、不作为缺口处理的设计决策（账户锁定的防枚举行为）。

## Glossary

- **Error_Catalog**: 单一权威错误码目录组件（`common::error::ErrorCatalog`），登记全部 Error_Code 及其 numeric_code、Error_Category、HTTP 状态码、默认 Client_Safe_Message。
- **Error_Code**: Error_Catalog 中登记的结构化错误码字符串（如 `VALIDATION_USERNAME_TAKEN`）。
- **Error_Category**: Error_Code 所属分类枚举（如 VALIDATION、AUTHENTICATION），决定其 numeric_code 段位与默认 HTTP 状态码。
- **Client_Safe_Message**: Error_Catalog 中登记的、面向终端用户展示的默认提示文案，不包含内部实现细节。
- **Error_Responder**: Application 端点统一错误响应入口组件（`common::error::ErrorResponder`），根据传入的 Error_Code 从 Error_Catalog 查找默认 Client_Safe_Message 并生成 Error_Envelope 响应。
- **Error_Envelope**: Error_Responder 生成的标准 JSON 错误响应体格式，包含 code、message、request_id 等字段。
- **Request_ID**: 每个请求的唯一标识符，允许出现在 Error_Envelope 中，不属于 Internal_Detail。
- **Internal_Detail**: 后端内部实现细节（如 SQL 片段、文件路径、堆栈信息），不得出现在展示给用户的 Client_Safe_Message 或前端本地化词条中。
- **AuthService**: 后端认证业务服务组件（`AuthService.cc`），负责用户注册、登录校验等核心逻辑。
- **SessionController**: 处理注册/登录 HTTP 端点的控制器组件（`SessionController.cc`）。
- **MfaController**: 处理多因素认证（MFA）设置与校验 HTTP 端点的控制器组件（`MfaController.cc`）。
- **WebAuthnController**: 处理 WebAuthn 凭据注册 HTTP 端点的控制器组件（`WebAuthnController.cc`）。
- **DeviceAuthController**: 处理设备授权码批准 HTTP 端点的控制器组件（`DeviceAuthController.cc`）。
- **PasswordResetController**: 处理密码重置确认 HTTP 端点的控制器组件（`PasswordResetController.cc`）。
- **EmailVerificationController**: 处理邮箱验证 HTTP 端点的控制器组件（`EmailVerificationController.cc`）。
- **Hodor**: 全局限流插件（`drogon::plugin::Hodor`），作为前置 advice 运行于所有控制器之外。
- **Application**: 后端 HTTP 服务整体（OAuth2Server 进程），涵盖所有控制器、服务与全局插件。
- **RegisterCallback**: `AuthService::registerUser` 的注册结果回调类型，携带一个字符串（空串表示成功，非空表示 Error_Code）。
- **Frontend_Error_Module**: 前端错误规范化与本地化管线，包含 `errorAdapter` 与 Error_Message_Catalog_FE。
- **Error_Message_Catalog_FE**: 前端本地化错误消息目录（`zh-CN.ts`），为每个 Error_Code 提供本地化展示文案。
- **OAuth2Frontend**: 面向终端用户的前端应用。
- **OAuth2Admin**: 面向管理员的前端应用。

## Requirements

### Requirement 1: 注册重复用户名/邮箱错误码精确化

**User Story:** 作为注册用户，我希望在用户名或邮箱已被占用时看到明确指出冲突字段的提示，而不是笼统的"输入参数有误"，以便我知道该修改哪个字段。

#### Acceptance Criteria

1. WHEN 用户使用已存在的用户名提交注册请求，THE SessionController SHALL 返回 HTTP 409 与 Error_Code `VALIDATION_USERNAME_TAKEN`；若提交的用户名和邮箱同时与已有记录冲突，用户名冲突的判定优先级高于邮箱冲突
2. WHEN 用户提交的用户名未被占用但邮箱已被占用的注册请求，THE SessionController SHALL 返回 HTTP 409 与 Error_Code `VALIDATION_EMAIL_TAKEN`
3. IF 注册失败的原因既非用户名冲突也非邮箱冲突，THEN THE SessionController SHALL 返回既有兜底 Error_Code `VALIDATION_INVALID_INPUT`
4. WHEN AuthService 完成一次成功的注册尝试，THE AuthService SHALL 通过 RegisterCallback 传递一个空字符串
5. WHEN AuthService 完成一次失败的注册尝试，THE AuthService SHALL 通过 RegisterCallback 传递一个在 Error_Catalog 中已登记的 Error_Code 字符串
6. IF RegisterCallback 传递的字符串非空，THEN THE SessionController SHALL 将该字符串直接转发给 Error_Responder 作为 Error_Code 参数，不对其做文本判断或硬编码回退
7. WHEN RegisterCallback 传递的字符串为空，THE SessionController SHALL 判定注册成功，且不调用 Error_Responder

### Requirement 2: MFA 校验失败错误码精确化

**User Story:** 作为设置多因素认证的用户，我希望在验证码错误或尚未完成设置时看到与 MFA 场景相关的提示，而不是"用户名或密码错误"这类不相关的提示。

#### Acceptance Criteria

1. WHEN 用户在 MFA 设置校验请求中提交的 TOTP 验证码不正确，THE MfaController SHALL 返回 HTTP 401 与 Error_Code `AUTH_MFA_CODE_INVALID`
2. WHEN 用户账户尚未注册任何 TOTP 密钥（即尚未完成 MFA 设置）的状态下调用 MFA 设置校验接口，THE MfaController SHALL 返回 HTTP 401 与 Error_Code `AUTH_MFA_NOT_CONFIGURED`

### Requirement 3: WebAuthn 凭据重复注册错误码精确化

**User Story:** 作为添加安全密钥的用户，我希望在重复添加同一凭据时看到明确的"已注册"提示，而不是让我误以为服务器故障。

#### Acceptance Criteria

1. WHEN 用户尝试注册的 WebAuthn credential_id 已存在于数据库中，THE WebAuthnController SHALL 返回 HTTP 状态码 409 与 Error_Code `VALIDATION_CREDENTIAL_ALREADY_REGISTERED`
2. IF WebAuthn 凭据写入失败且失败原因不是 credential_id 唯一约束冲突，THEN THE WebAuthnController SHALL 返回既有兜底 Error_Code `DB_QUERY_ERROR`
3. IF 检测到用户尝试注册的 WebAuthn credential_id 与数据库中已存在的凭据记录重复，THEN THE WebAuthnController SHALL 保持该已存在凭据记录不被修改或覆盖

### Requirement 4: 设备授权码失效错误码精确化

**User Story:** 作为批准设备授权的用户，我希望在设备码无效时看到与设备授权场景相关的提示，而不是通用的"输入参数有误"。

#### Acceptance Criteria

1. WHEN 设备授权批准请求中的 user_code 未找到对应记录、对应设备授权记录已处于批准（approved）或拒绝（denied）终态（以下统称"已被处理"）、或已超过其有效期（expired），THE DeviceAuthController SHALL 返回 Error_Code `VALIDATION_DEVICE_CODE_INVALID`
2. THE DeviceAuthController SHALL 对"未找到""已被处理（已批准或已拒绝）""已过期"三种原因返回相同的 Error_Code，不在响应中包含可用于区分具体原因的任何信息
3. IF 设备授权批准请求因 user_code 未找到、已被处理或已过期而被拒绝，THEN THE DeviceAuthController SHALL 保持该 user_code 对应设备授权记录的现有状态不变，不因此次被拒绝的批准请求产生任何状态变更

### Requirement 5: 密码重置与邮箱验证 token 失效错误码精确化

**User Story:** 作为申请密码重置或邮箱验证的用户，我希望在链接失效时看到具体指引（如重新申请/重新发送），同时系统仍不透露链接失效的具体原因，以防止被用于枚举攻击。

#### Acceptance Criteria

1. WHEN 密码重置确认请求中的 token 未被找到、格式错误、已过期或已被使用，THE PasswordResetController SHALL 返回 Error_Code `VALIDATION_RESET_TOKEN_INVALID`
2. WHEN 邮箱验证请求中的 token 未被找到、格式错误、已过期或已被使用，THE EmailVerificationController SHALL 返回 Error_Code `VALIDATION_VERIFICATION_TOKEN_INVALID`
3. THE PasswordResetController SHALL 对"未被找到""格式错误""已过期""已被使用"四种原因返回完全相同的 Error_Code、HTTP 状态码与响应体结构，不做进一步区分
4. THE EmailVerificationController SHALL 对"未被找到""格式错误""已过期""已被使用"四种原因返回完全相同的 Error_Code、HTTP 状态码与响应体结构，不做进一步区分

### Requirement 6: 限流拒绝响应体 Envelope 化

**User Story:** 作为 API 调用方，我希望被限流拒绝时收到的响应体格式与其他错误响应一致（结构化 JSON），以便我的客户端代码能用同一套逻辑处理所有错误。

#### Acceptance Criteria

1. WHEN Hodor 限流插件拒绝一个请求，THE Application SHALL 返回符合 Error_Envelope 格式的 JSON 响应体
2. WHEN Hodor 限流插件拒绝一个请求，THE Application SHALL 在该响应体中使用 Error_Code `VALIDATION_RATE_LIMITED`
3. WHEN Hodor 限流插件拒绝一个请求，THE Application SHALL 返回 HTTP 状态码 429
4. IF Hodor 插件在启动时未加载，THEN THE Application SHALL 完成正常启动流程并进入可接受请求的状态，不因插件未加载而中止启动
5. IF Hodor 插件在启动时未加载，THEN THE Application SHALL 不保证限流拒绝响应符合 Error_Envelope 格式、使用 Error_Code `VALIDATION_RATE_LIMITED` 或返回 HTTP 状态码 429
6. IF Application 无法确定 Hodor 插件是否已加载，THEN THE Application SHALL 继续正常启动并进入可接受请求的状态，不因该不确定性中止启动流程
7. IF Application 无法确定 Hodor 插件是否已加载，THEN THE Application SHALL 不保证限流拒绝响应符合 Error_Envelope 格式、使用 Error_Code `VALIDATION_RATE_LIMITED` 或返回 HTTP 状态码 429

### Requirement 7: 账户锁定防枚举行为保留

**User Story:** 作为安全负责人，我希望账户锁定期间的登录失败提示与普通密码错误在响应上完全不可区分，以防止攻击者借此枚举出哪些用户名对应已存在账户。

#### Acceptance Criteria

1. WHILE 用户账户处于锁定期内，WHEN 该账户收到一次登录尝试（无论本次提交的密码是否正确），THE AuthService SHALL 返回 Error_Code `AUTH_INVALID_CREDENTIALS`
2. THE AuthService SHALL 对"账户锁定期内的登录尝试"与"密码错误的登录尝试"返回完全相同的 Error_Code、HTTP 状态码与响应体结构，其中响应体中除 Request_ID 字段外的其余字段值与结构必须逐一相同；Request_ID 字段因每个请求唯一而允许不同，不计入该一致性比较

### Requirement 8: 新增 Error_Catalog 条目的合法性

**User Story:** 作为维护错误码目录的开发者，我希望本特性新增的错误码条目遵循既有目录的段位规则与字段完整性约束，以保持目录的一致性与可扩展性。

#### Acceptance Criteria

1. THE Error_Catalog SHALL 包含以下 9 条本特性新增的条目：`VALIDATION_USERNAME_TAKEN`（3006）、`VALIDATION_EMAIL_TAKEN`（3007）、`VALIDATION_CREDENTIAL_ALREADY_REGISTERED`（3008）、`VALIDATION_RESET_TOKEN_INVALID`（3009）、`VALIDATION_VERIFICATION_TOKEN_INVALID`（3010）、`VALIDATION_DEVICE_CODE_INVALID`（3011）、`VALIDATION_RATE_LIMITED`（3012）、`AUTH_MFA_CODE_INVALID`（4004）、`AUTH_MFA_NOT_CONFIGURED`（4005）
2. THE Error_Catalog SHALL 为每条新增条目分配落在其所属 Error_Category 对应段位区间内（VALIDATION 3000-3099，AUTHENTICATION 4000-4099）且与目录中既有条目及本特性新增的其他条目均不重复的 numeric_code
3. THE Error_Catalog SHALL 为每条新增条目提供非空、不包含 Internal_Detail 的默认 Client_Safe_Message
4. WHERE 新增条目为 `VALIDATION_USERNAME_TAKEN`、`VALIDATION_EMAIL_TAKEN`、`VALIDATION_CREDENTIAL_ALREADY_REGISTERED` 或 `VALIDATION_RATE_LIMITED`，THE Error_Catalog SHALL 分别以 409、409、409、429 作为该 Error_Code 的响应状态码 override
5. WHERE 新增条目为 `VALIDATION_RESET_TOKEN_INVALID`、`VALIDATION_VERIFICATION_TOKEN_INVALID`、`VALIDATION_DEVICE_CODE_INVALID`、`AUTH_MFA_CODE_INVALID` 或 `AUTH_MFA_NOT_CONFIGURED`，THE Error_Catalog SHALL 不为其定义 HTTP 状态码 override，并使用该条目所属 Error_Category 的默认状态码作为响应状态码
6. THE Error_Catalog SHALL 保持本特性新增前既有条目的 code、numeric_code 与顺序不变

### Requirement 9: 前端消息目录同步

**User Story:** 作为使用 OAuth2Frontend 或 OAuth2Admin 的用户，我希望后端新增的错误码在前端界面上都有对应的中文提示，而不是显示原始错误码或空白信息。

#### Acceptance Criteria

1. THE OAuth2Frontend 的 Error_Message_Catalog_FE（zh-CN）SHALL 为 `VALIDATION_USERNAME_TAKEN`、`VALIDATION_EMAIL_TAKEN`、`VALIDATION_CREDENTIAL_ALREADY_REGISTERED`、`VALIDATION_RESET_TOKEN_INVALID`、`VALIDATION_VERIFICATION_TOKEN_INVALID`、`VALIDATION_DEVICE_CODE_INVALID`、`VALIDATION_RATE_LIMITED`、`AUTH_MFA_CODE_INVALID`、`AUTH_MFA_NOT_CONFIGURED` 各提供一条非空、且文案不等于该 Error_Code 字符串本身的本地化词条
2. THE OAuth2Admin 的 Error_Message_Catalog_FE（zh-CN）SHALL 为 `VALIDATION_USERNAME_TAKEN`、`VALIDATION_EMAIL_TAKEN`、`VALIDATION_CREDENTIAL_ALREADY_REGISTERED`、`VALIDATION_RESET_TOKEN_INVALID`、`VALIDATION_VERIFICATION_TOKEN_INVALID`、`VALIDATION_DEVICE_CODE_INVALID`、`VALIDATION_RATE_LIMITED`、`AUTH_MFA_CODE_INVALID`、`AUTH_MFA_NOT_CONFIGURED` 各提供一条非空、且文案不等于该 Error_Code 字符串本身的本地化词条
3. FOR ALL 本特性新增的 Error_Code，OAuth2Frontend 与 OAuth2Admin 两个 Error_Message_Catalog_FE 中对应词条的文案 SHALL 逐字符完全相同
4. THE OAuth2Frontend 与 OAuth2Admin 的 Error_Message_Catalog_FE SHALL 使新增的本地化词条不包含 Internal_Detail
