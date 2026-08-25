# Requirements Document

## Introduction

本特性的目标是统一 OAuth2 平台前后端的错误提示机制，建立一套稳定、可文档化、可测试的「错误码 + 错误信息」体系，达到发版（生产可用）要求。

当前代码库中并存至少四种互不兼容的错误响应格式：

1. **数值错误体系**（`OAuth2Plugin/.../error/ErrorTypes.h` + `ErrorHandler.cc`）：`enum ErrorCode`（按段位分组：Network 1000-1099、Database 2000-2099、Validation 3000-3099、Authentication 4000-4099、Authorization 5000-5099、Internal 6000-6099），`Error::toJson()` 产出 `{ "error": { "code": <int>, "category": <STRING>, "message", "details", "request_id" } }`。
2. **RFC 6749 OAuth2 错误**（`OAuth2ErrorHandler.cc`）：字符串错误码（`invalid_request`、`invalid_client`、`invalid_grant` 等），产出 `{ "error": <string>, "error_description", "error_uri" }`，并设置 `Cache-Control: no-store`、`Pragma: no-cache`。
3. **校验错误响应器**（`validation/HttpResponder.cc`）：产出第三种格式 `{ "error": { "code": "VALIDATION_ERROR", "message", "details" | "reason", "timestamp" } }`，并通过 `detailedErrorsAllowed()` 在生产环境隐藏字段级细节。
4. **控制器即兴格式**（如 `UserSelfServiceController.cc`、`WebAuthnController.cc`、`WeChatController.cc`、Admin API）：散落使用 `{ "error": "not_found", "error_description": ... }`、`{ "message": "..." }`，甚至纯文本响应体。

前端（`OAuth2Frontend` 与 `OAuth2Admin`，Vue 3 + axios）没有集中式的「错误码 → 用户可读信息」映射：`http.ts` 仅在 401 时刷新令牌并 reject 原始 axios 错误；各组件以 `e.response?.data?.message`、`e.response?.data?.error_description`、`e.response?.data`、`e.message` 等不一致的方式各自取值，且无 i18n。

本需求文档定义统一的后端错误响应封套（Error Envelope）、稳定的错误码目录（Error Catalog）、内部细节与客户端可见信息的安全隔离、请求关联 ID、前端统一消费与本地化展示，以及向后兼容迁移与发版就绪标准。RFC 6749/6750/7009/7662/8628 等协议端点必须保持协议合规。

## Glossary

- **Backend**: OAuth2 后端服务（`OAuth2Server` 可执行体与 `OAuth2Plugin` 共享库的总称）。
- **Frontend_App**: 面向终端用户的前端应用 `OAuth2Frontend`（Vue 3 + axios）。
- **Admin_App**: 面向管理员的前端控制台 `OAuth2Admin`（Vue 3 + axios）。
- **Error_Envelope**: 后端为 Application_Endpoint 返回的统一错误 JSON 结构，顶层键为 `error`，包含字段 `code`、`category`、`message`、`request_id`，以及可选字段 `numeric_code`、`details`、`timestamp`。
- **Error_Code**: 错误的稳定字符串标识符（例如 `AUTH_INVALID_CREDENTIALS`），跨版本不变，作为前端映射键。
- **Numeric_Error_Code**: 与 Error_Code 一一对应的整数码（沿用现有段位：Network 1000-1099、Database 2000-2099、Validation 3000-3099、Authentication 4000-4099、Authorization 5000-5099、Internal 6000-6099）。
- **Error_Category**: 错误分类枚举，取值为 `NETWORK`、`DATABASE`、`VALIDATION`、`AUTHENTICATION`、`AUTHORIZATION`、`INTERNAL`、`UNKNOWN`。
- **Error_Catalog**: 错误码目录，登记每个 Error_Code 对应的 Numeric_Error_Code、Error_Category、HTTP 状态码、默认客户端可见信息（Client_Safe_Message），以及简短说明。
- **HTTP_Status_Code**: HTTP 响应状态码（整数）。
- **OAuth2_Protocol_Endpoint**: 实现 RFC 6749/6750/7009/7662/8628 等 OAuth2/OIDC 协议语义的端点，包括 `/oauth2/token`、`/oauth2/authorize`、`/oauth2/introspect`、`/oauth2/revoke`、设备授权端点，以及其错误体须遵循 RFC 6749 §5.2 的端点。
- **Application_Endpoint**: 除 OAuth2_Protocol_Endpoint 之外的全部返回 JSON 的业务端点（含 Admin API、用户自助 API、WebAuthn 业务端点等）。
- **Client_Safe_Message**: 可安全返回给客户端的错误信息，不包含堆栈、SQL 语句、数据库内部错误、内部主机名或其他敏感实现细节。
- **Internal_Detail**: 仅用于服务端日志的诊断信息（如异常 `what()`、SQL 片段、堆栈摘要）。
- **Production_Mode**: 后端以生产配置运行的状态（非 `DEBUG` 构建且未显式开启详细错误开关）。
- **Request_ID**: 单次请求的关联标识符（现有实现形如 `req_` 前缀的十六进制串），用于跨日志与客户端反馈进行问题定位。
- **Frontend_Error_Module**: 前端共享的错误处理模块，负责解析 Error_Envelope 与 OAuth2 协议错误体，并将 Error_Code 映射为本地化用户信息。
- **Error_Message_Catalog_FE**: 前端维护的「Error_Code → 本地化用户信息」资源表（支持 i18n）。

## Requirements

### Requirement 1: 统一错误响应封套（Error Envelope）

**User Story:** 作为前端开发者，我希望所有业务端点返回统一结构的错误响应，以便用一套解析逻辑处理全部错误。

#### Acceptance Criteria

1. WHEN 一个 Application_Endpoint 返回错误响应，THE Backend SHALL 输出 Error_Envelope，且 JSON 响应体顶层为单一对象，其唯一键为 `error`（值为 JSON 对象），不包含任何其他顶层键。
2. WHEN 一个 Application_Endpoint 返回 Error_Envelope，THE Backend SHALL 在 `error` 对象中包含非空字符串字段 `code`（Error_Code）、非空字符串字段 `category`（取值属于 Error_Category 枚举集合 {`NETWORK`、`DATABASE`、`VALIDATION`、`AUTHENTICATION`、`AUTHORIZATION`、`INTERNAL`、`UNKNOWN`}）、字符串字段 `message`（Client_Safe_Message，长度为 1 至 500 个字符）、非空字符串字段 `request_id`。
3. WHERE 存在对应的整数码，THE Backend SHALL 在 Error_Envelope 中包含 `numeric_code` 字段，取值等于该 Error_Code 在 Error_Catalog 中登记的 Numeric_Error_Code。
4. THE Backend SHALL 为每个 Error_Envelope 设置 `Content-Type: application/json` 响应头。
5. WHEN 一个 Error_Envelope 被序列化为 JSON 后再被反序列化，THE Backend SHALL 还原出 `code`、`category`、`message`、`numeric_code`、`request_id` 字段且取值与序列化前相等（round-trip 属性）。
6. THE Backend SHALL 使 Error_Envelope 中 `code` 的取值属于 Error_Catalog 中已登记的 Error_Code 集合。
7. IF 某 Error_Code 在 Error_Catalog 中未登记对应的 Numeric_Error_Code，THEN THE Backend SHALL 在该 Error_Envelope 中省略 `numeric_code` 字段，而非以 null 或空字符串表示。

### Requirement 2: 保持 OAuth2 协议端点的 RFC 合规

**User Story:** 作为集成方，我希望 OAuth2 协议端点的错误响应继续符合 RFC 6749，以便现有标准客户端不受影响。

#### Acceptance Criteria

1. WHEN 一个 OAuth2_Protocol_Endpoint 返回错误，THE Backend SHALL 输出符合 RFC 6749 §5.2 的 JSON 错误体、设置响应头 `Content-Type: application/json`，使顶层必含字符串字段 `error` 取标准协议错误码，并在包含可选字段 `error_uri` 时使其取值为字符串。
2. THE Backend SHALL 使 OAuth2_Protocol_Endpoint 错误体的 `error` 取值属于集合 {`invalid_request`, `invalid_client`, `invalid_grant`, `unauthorized_client`, `unsupported_grant_type`, `invalid_scope`, `server_error`, `temporarily_unavailable`} 或相应 RFC（RFC 7009/7662/8628）定义的错误码，且 SHALL NOT 取该并集之外的取值。
3. WHEN 一个 OAuth2_Protocol_Endpoint 返回错误，THE Backend SHALL 设置响应头 `Cache-Control: no-store` 与 `Pragma: no-cache`。
4. WHEN 一个 OAuth2_Protocol_Endpoint 返回 `invalid_client` 错误，THE Backend SHALL 设置 HTTP_Status_Code 为 401。
5. THE Backend SHALL NOT 将 Requirement 1 定义的 Error_Envelope 结构用于 OAuth2_Protocol_Endpoint 的协议错误响应。
6. THE Error_Catalog SHALL 为每个 OAuth2 协议字符串错误码登记一条映射，记录其对应的 HTTP_Status_Code、默认 `error_description`（Client_Safe_Message），以及可选的 `error_uri`。
7. THE Backend SHALL 使任一 OAuth2_Protocol_Endpoint 错误响应的 HTTP_Status_Code 等于该协议错误码在 Error_Catalog 中登记的 HTTP_Status_Code。
8. WHEN 一个 OAuth2_Protocol_Endpoint 返回错误，THE Backend SHALL 在错误体的 `error_description` 字段中包含该协议错误码在 Error_Catalog 中登记的默认 `error_description`，且该取值为 Client_Safe_Message（不含 Internal_Detail）。
9. IF 一个 OAuth2_Protocol_Endpoint 返回 `invalid_client` 错误且客户端通过 `Authorization` 请求头进行客户端认证，THEN THE Backend SHALL 设置与所用认证方案匹配的 `WWW-Authenticate` 响应头。

### Requirement 3: 完整且稳定的错误码目录（Error Catalog）

**User Story:** 作为后端开发者与发版负责人，我希望有一份完整、文档化的错误码目录，以便全平台引用同一套错误码。

#### Acceptance Criteria

1. THE Error_Catalog SHALL 为每个 Error_Code 登记以下属性且取值均符合约束：Error_Code（非空字符串）、Numeric_Error_Code（整数）、Error_Category（取值属于 Error_Category 枚举集合）、HTTP_Status_Code（整数，取值在 100 至 599 之间）、默认 Client_Safe_Message（非空且不含 Internal_Detail）、简短说明（长度为 1 至 200 个字符）。
2. THE Error_Catalog SHALL 使每个 Numeric_Error_Code 落在其 Error_Category 对应的现有段位区间内（Network 1000-1099、Database 2000-2099、Validation 3000-3099、Authentication 4000-4099、Authorization 5000-5099、Internal 6000-6099）。
3. THE Error_Catalog SHALL 使 Error_Code 在目录内唯一，且 Numeric_Error_Code 在目录内唯一。
4. IF 现有代码中存在重复的 Numeric_Error_Code，THEN THE Error_Catalog SHALL 在该码所属的 Error_Category 段位区间内重新分配整数取值以消除重复。
5. THE Backend SHALL 以单一权威来源（single source of truth）定义 Error_Catalog，使后端代码引用的每个 Error_Code 的 Numeric_Error_Code、Error_Category 与 HTTP_Status_Code 等于该权威来源登记的取值，且发布文档由该权威来源生成或对其校验。
6. WHERE 已有数值错误码存在（`CONNECTION_FAILED=1001`、`TIMEOUT=1002`、`DB_CONNECTION_ERROR=2001`、`DB_QUERY_ERROR=2002`、`DB_CONSTRAINT_VIOLATION=2003`、`INVALID_INPUT=3001`、`MISSING_REQUIRED_FIELD=3002`、`FORMAT_ERROR=3003`、`INVALID_CREDENTIALS=4001`、`TOKEN_EXPIRED=4002`、`TOKEN_INVALID=4003`、`ACCESS_DENIED=5001`、`INSUFFICIENT_PERMISSIONS=5002`、`INTERNAL=6001`）且彼此不冲突，THE Error_Catalog SHALL 保留这些整数取值不变。
7. THE Error_Catalog SHALL 以文档形式发布于仓库（与 `docs/backend/api-reference.md` 的「通用错误码」章节一致或由其引用）。
8. THE Error_Catalog SHALL 为 Backend 在任一 Application_Endpoint 上可能返回的每个 Error_Code 登记恰好一条目，且 Backend SHALL NOT 返回未在 Error_Catalog 中登记的 Error_Code。
9. WHERE 某 Error_Code 字符串已在既往版本的 Error_Catalog 中发布，THE Error_Catalog SHALL 在后续版本中保持该 Error_Code 字符串及其 Numeric_Error_Code 与 Error_Category 不变。

### Requirement 4: 错误码到 HTTP 状态码的一致映射

**User Story:** 作为 API 消费者，我希望相同类别的错误返回一致的 HTTP 状态码，以便依据状态码做统一处理。

#### Acceptance Criteria

1. WHEN Backend 返回 Error_Category 为 `VALIDATION` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 400。
2. WHEN Backend 返回 Error_Category 为 `AUTHENTICATION` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 401。
3. WHEN Backend 返回 Error_Category 为 `AUTHORIZATION` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 403。
4. WHEN Backend 返回 Error_Category 为 `DATABASE` 或 `INTERNAL` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 500。
5. WHEN Backend 返回 Numeric_Error_Code 为 `TIMEOUT`（1002）的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 504。
6. WHEN Backend 返回 Error_Category 为 `NETWORK` 且 Numeric_Error_Code 非 `TIMEOUT` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 502。
7. THE Backend SHALL 使任一 Error_Code 返回的 HTTP_Status_Code 等于该 Error_Code 在 Error_Catalog 中登记的 HTTP_Status_Code；且 Error_Catalog 中每个 Error_Code 登记的 HTTP_Status_Code SHALL 与本需求第 1–6 条及第 8 条定义的 Error_Category 与 Numeric_Error_Code 映射规则一致（运行时以 Error_Catalog 登记值为权威取值）。
8. WHEN Backend 返回 Error_Category 为 `UNKNOWN` 的错误，THE Backend SHALL 设置 HTTP_Status_Code 为 500。
9. THE Backend SHALL 使属于同一 Error_Category 的全部 Error_Code 返回相同的 HTTP_Status_Code（`NETWORK` 类别按第 5–6 条依 Numeric_Error_Code 区分为 504 或 502 除外），且同一 Error_Code 在任意一次调用下返回的 HTTP_Status_Code 保持相同。

### Requirement 5: 内部细节与客户端可见信息的安全隔离

**User Story:** 作为安全负责人，我希望生产环境的错误响应不泄露内部实现细节，以便降低信息泄露风险。

#### Acceptance Criteria

1. WHILE Backend 运行于 Production_Mode，THE Backend SHALL 在错误响应中仅返回 Client_Safe_Message，并使 Error_Envelope 完全不含 `details` 键（而非以空值表示）。
2. WHEN Backend 处理任一异常或数据库错误，THE Backend SHALL 将 Internal_Detail（异常 `what()`、SQL 片段、堆栈摘要）记录到服务端日志，并在日志中包含对应的 Request_ID。
3. WHILE Backend 运行于 Production_Mode，THE Backend SHALL NOT 在 Error_Envelope 任一返回给客户端的字符串字段（包括 `message` 与 `details`）中包含 SQL 语句、数据库驱动错误文本、文件系统路径或堆栈跟踪。
4. WHERE 非 Production_Mode 或显式开启详细错误开关，THE Backend SHALL 在 Error_Envelope 的 `details` 字段中包含附加诊断信息（VALIDATION 类错误为触发失败的字段名称与失败原因；其他类错误为异常诊断摘要）。
5. WHEN 一个未在 Error_Catalog 中登记映射的异常被捕获，THE Backend SHALL 返回 Error_Category 为 `INTERNAL` 且 Error_Code 为内部错误（对应 Numeric_Error_Code 6001）的 Error_Envelope，并使其 `message` 等于该 Error_Code 在 Error_Catalog 中登记的默认 Client_Safe_Message。
6. WHILE Backend 运行于 Production_Mode，THE Backend SHALL 使 Error_Envelope 的 `message` 字段取值等于该 Error_Code 在 Error_Catalog 中登记的默认 Client_Safe_Message，而非原始异常文本或数据库错误文本。

### Requirement 6: 请求关联 ID（Request ID）

**User Story:** 作为支持工程师，我希望每个错误响应携带可追踪的请求 ID，以便据此在日志中定位问题。

#### Acceptance Criteria

1. WHEN Backend 返回任一 Error_Envelope，THE Backend SHALL 在 `request_id` 字段中包含一个长度为 1 至 128 个字符的非空 Request_ID。
2. WHEN Backend 记录某次请求的错误日志，THE Backend SHALL 在日志条目中包含与该请求 Error_Envelope 相同的 Request_ID。
3. WHERE 入站请求携带关联 ID 请求头且其取值非空、长度不超过 128 个字符、仅由 ASCII 字母数字及 `-`、`_` 组成，THE Backend SHALL 复用该请求头取值作为本次请求的 Request_ID。
4. WHERE 入站请求未携带关联 ID 请求头，THE Backend SHALL 生成一个长度为 1 至 128 个字符、非空且在同一 Backend 实例上跨请求互不相同的 Request_ID。
5. IF 入站请求携带的关联 ID 请求头取值为空、超过 128 个字符或包含约定字符集之外的字符，THEN THE Backend SHALL 忽略该取值并生成一个新的 Request_ID。

### Requirement 7: 后端各端点迁移至统一错误处理

**User Story:** 作为后端开发者，我希望所有控制器通过统一入口产生错误响应，以便消除即兴格式。

#### Acceptance Criteria

1. THE Backend SHALL 使每个 Application_Endpoint 经由统一错误处理入口产生错误响应，输出 Requirement 1 定义的 Error_Envelope。
2. THE Backend SHALL 使每个 OAuth2_Protocol_Endpoint 经由 OAuth2 错误处理入口产生错误响应，输出 Requirement 2 定义的 RFC 6749 错误体。
3. THE Backend SHALL NOT 在 Application_Endpoint 的错误响应中返回 `Content-Type` 非 `application/json` 的响应体（包括纯文本、HTML 错误页等任何非 JSON 格式）。
4. WHEN 现有的校验错误（来自 validation 响应器，原 `{ "error": { "code": "VALIDATION_ERROR", ... } }`）被返回，THE Backend SHALL 将其转换为 Error_Category 为 `VALIDATION` 且 HTTP_Status_Code 为 400 的 Error_Envelope。
5. THE Backend SHALL 使每个 Application_Endpoint 错误响应的 `error` 对象仅包含 Error_Envelope 定义的字段名（`code`、`category`、`message`、`request_id`，以及可选 `numeric_code`、`details`、`timestamp`），且 SHALL NOT 使用 `error_description`、`reason` 或其他别名表示相同语义。
6. WHERE 非 Production_Mode 或显式开启详细错误开关，THE Backend SHALL 在由校验错误转换得到的 VALIDATION Error_Envelope 的 `details` 字段中包含触发失败的字段名称与对应的失败原因。
7. IF 某 Application_Endpoint 在处理过程中抛出未被捕获的异常或触发框架默认错误处理，THEN THE Backend SHALL 经由统一错误处理入口返回 Requirement 1 定义的 Error_Envelope，而非框架默认的 HTML 或纯文本响应体。

### Requirement 8: 前端统一错误解析与码到信息映射

**User Story:** 作为前端开发者，我希望有一个共享模块解析后端错误并映射为用户信息，以便组件不再各自取值。

#### Acceptance Criteria

1. THE Frontend_Error_Module SHALL 提供单一函数，接收 axios 错误对象并始终返回（不抛出异常）规范化错误结构，包含字段 `code`、非空字符串 `message`、`request_id`（无该值时为空字符串）、`httpStatus`（无 HTTP 响应时为 0）。
2. WHEN Frontend_Error_Module 接收到符合 Error_Envelope 的响应体（顶层为对象且 `error` 对象含字符串 `code`），THE Frontend_Error_Module SHALL 从 `error.code` 提取 Error_Code，并据 Error_Message_Catalog_FE 映射为本地化用户信息。
3. WHEN Frontend_Error_Module 接收到符合 RFC 6749 的协议错误体（顶层含字符串字段 `error`），THE Frontend_Error_Module SHALL 从顶层 `error` 字符串提取错误码，并映射为对应的本地化用户信息。
4. IF 错误响应体不符合 Error_Envelope 也不符合 RFC 6749 错误体，THEN THE Frontend_Error_Module SHALL 返回一个通用的本地化用户信息，并将 `code` 置为通用未知错误码。
5. IF axios 错误不含任何响应体（网络故障或超时且无 HTTP 响应），THEN THE Frontend_Error_Module SHALL 返回网络类通用本地化用户信息，并将 `code` 置为网络类回退错误码、`httpStatus` 置为 0。
6. THE Frontend_Error_Module SHALL 同时被 Frontend_App 与 Admin_App 复用（同一实现或共享来源）。

### Requirement 9: 前端错误信息本地化（i18n）

**User Story:** 作为终端用户，我希望看到清晰的本地化错误提示，以便理解发生了什么并采取行动。

#### Acceptance Criteria

1. THE Error_Message_Catalog_FE SHALL 为 Error_Catalog 中登记的每个 Error_Code 以及每个 OAuth2 协议字符串错误码各提供一条非空的、用户可读的本地化信息条目（不含未替换的占位符标记）。
2. WHERE 当前界面语言为简体中文，THE Frontend_Error_Module SHALL 返回该 Error_Code 对应的简体中文非空用户信息。
3. IF 某 Error_Code 在 Error_Message_Catalog_FE 中无对应条目，THEN THE Frontend_Error_Module SHALL 返回与 Requirement 8 第 4 条一致的通用未知错误本地化用户信息（非空），并在前端控制台记录该缺失的 Error_Code。
4. THE Error_Message_Catalog_FE SHALL 使展示给用户的信息（含通用回退信息）不包含 Request_ID 之外的后端 Internal_Detail。
5. WHERE 错误响应包含 Request_ID，THE Frontend_Error_Module SHALL 在其返回的规范化错误结构中提供该 Request_ID 取值，使界面可向用户展示以便反馈。
6. IF 当前界面语言在 Error_Message_Catalog_FE 中缺少该 Error_Code 的对应语言条目，THEN THE Frontend_Error_Module SHALL 返回默认语言（简体中文）的对应非空信息条目。

### Requirement 10: 前端各应用一致地呈现错误

**User Story:** 作为终端用户与管理员，我希望两个前端应用以一致的方式展示错误，以便获得统一体验。

#### Acceptance Criteria

1. WHEN Frontend_App 中某视图发起的 API 请求返回错误（HTTP_Status_Code 不属于 200-299）或因网络故障/超时而无 HTTP 响应，THE Frontend_App SHALL 通过 Frontend_Error_Module 解析该 axios 错误，并在该视图中展示 Frontend_Error_Module 返回的本地化用户信息。
2. WHEN Admin_App 中某视图发起的 API 请求返回错误（HTTP_Status_Code 不属于 200-299）或因网络故障/超时而无 HTTP 响应，THE Admin_App SHALL 通过 Frontend_Error_Module 解析该 axios 错误，并在该视图中展示 Frontend_Error_Module 返回的本地化用户信息。
3. THE Frontend_App 与 Admin_App SHALL NOT 直接读取原始 axios 错误的 `e.response.data.message`、`e.response.data.error_description` 或 `e.response.data` 来展示给用户。
4. WHEN HTTP_Status_Code 为 401 且令牌刷新失败，THE Frontend_App 与 Admin_App SHALL 通过 Frontend_Error_Module 展示会话失效的本地化用户信息。
5. WHEN HTTP_Status_Code 为 401 且令牌刷新失败，THE Frontend_App 与 Admin_App SHALL 将界面导航至登录视图。
6. THE Admin_App SHALL NOT 使用浏览器原生 `alert` 展示后端错误信息。
7. WHEN Frontend_App 与 Admin_App 接收到携带相同 Error_Code 的错误响应且界面语言相同，THE Frontend_App 与 Admin_App SHALL 展示相同的本地化用户信息。

### Requirement 11: 向后兼容与迁移安全

**User Story:** 作为发版负责人，我希望迁移到统一格式时不破坏现有合约，以便平滑发布。

#### Acceptance Criteria

1. THE Backend SHALL 保持所有现有端点的 HTTP 路径与 HTTP 方法与迁移前基线（本特性实施前同一端点的行为）完全一致，不新增、删除或重命名既有路径与方法。
2. WHEN 某现有端点返回成功响应（HTTP_Status_Code 在 200 至 299 之间），THE Backend SHALL 保持其响应体的顶层 JSON 键集合、字段名、字段类型与嵌套层级与迁移前基线一致。
3. THE Backend SHALL 保持 OAuth2_Protocol_Endpoint 错误响应的字段名、字段类型与错误码取值与 RFC 6749 §5.2 基线一致，不新增、删除或重命名 `error`、`error_description`、`error_uri` 字段。
4. WHEN 某 Application_Endpoint 的错误格式由旧格式迁移为 Error_Envelope，THE Backend SHALL 保持该错误对应的 HTTP_Status_Code 与迁移前基线相同。
5. THE Backend SHALL 保留现有且彼此不冲突的 Numeric_Error_Code 整数取值，以及全部 Error_Category 枚举名称，与迁移前基线一致，以兼容已依赖这些取值的测试与客户端。
6. IF 某 Numeric_Error_Code 依 Requirement 3 第 4 条因重复而必须重新分配，THEN THE Backend SHALL 仅在该码所属 Error_Category 段位区间内改变其整数取值，并保持其对应的 Error_Code 字符串标识符不变。

### Requirement 12: 发版就绪标准（文档、测试与一致性）

**User Story:** 作为发版负责人，我希望统一错误体系具备文档与测试覆盖，以便达到生产可用标准。

#### Acceptance Criteria

1. THE Error_Catalog SHALL 以仓库内文档形式发布，且为全部 Error_Code 及全部 OAuth2 协议错误码各登记一条目，每条目至少包含该错误码标识、对应 HTTP_Status_Code 与 Error_Category（OAuth2 协议错误码至少包含其字符串错误码与对应 HTTP_Status_Code）。
2. THE Backend SHALL 提供自动化测试，枚举 Error_Catalog 中登记的每个 Error_Code，验证其 HTTP_Status_Code 映射与 Error_Category 与 Error_Catalog 登记值一致，并在存在任一不一致时判定测试失败。
3. THE Backend SHALL 提供自动化测试，验证 Error_Envelope 序列化后再反序列化的 round-trip 一致性（对应 Requirement 1 第 5 条）。
4. THE Backend SHALL 提供自动化测试，验证错误响应不包含 SQL 语句、堆栈跟踪或文件系统路径（对应 Requirement 5 第 3 条），且该验证在所有运行环境（开发、预发布、生产）下均执行。
5. THE Frontend_Error_Module SHALL 提供自动化测试，验证 Error_Envelope 与 RFC 6749 错误体两种输入均被映射为非空本地化用户信息。
6. THE Backend SHALL 提供自动化测试，枚举全部 Application_Endpoint 的错误响应体，验证每个响应体均可被解析为 Error_Envelope，并在存在任一纯文本或不可解析为 Error_Envelope 的响应时判定测试失败。
7. WHEN 执行发版前的持续集成流水线，THE Backend SHALL 运行本需求第 2 至第 6 条定义的全部自动化测试，并在任一测试未通过时判定发版就绪检查失败。
