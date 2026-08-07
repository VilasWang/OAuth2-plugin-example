---
kind: error_handling
name: 统一错误处理体系：ErrorCatalog + Error Envelope + Drogon 全局异常处理器
category: error_handling
scope:
    - '**'
source_files:
    - libs/common/include/authforge/common/error/ErrorTypes.h
    - libs/common/include/authforge/common/error/ErrorCatalog.h
    - libs/drogon/include/authforge/drogon/error/ErrorResponder.h
    - libs/drogon/include/authforge/drogon/error/ErrorHandler.h
    - libs/drogon/include/authforge/drogon/error/OAuth2ErrorHandler.h
    - apps/server/src/bootstrap/ExceptionHandlerSetup.cc
    - libs/common/include/authforge/common/result/Result.h
    - tests/unit/error/ErrorCatalogPropertyTest.cc
    - tests/unit/error/ErrorEnvelopePropertyTest.cc
    - tests/unit/error/ErrorResponderTest.cc
    - tests/unit/error/OAuth2ErrorPropertyTest.cc
    - tests/unit/error/OAuth2InvalidClientHeaderTest.cc
---

## 1. 整体方案

AuthForge 在后端（C++ / Drogon）实现了一套**集中式、目录化**的错误处理体系，核心思想是：**所有业务/协议错误都归一为 `authforge::common::error::Error` 对象，通过单一来源的 `ErrorCatalog` 解析稳定字符串码与 HTTP 状态码，再由 Drogon 层的 `ErrorResponder` / `OAuth2ErrorHandler` 渲染为标准 JSON 响应；未捕获异常由启动时注册的 Drogon 全局异常处理器兜底。**

该体系分为三层：
- **领域层（libs/common）**：定义 `ErrorCategory`、`Error`、`Result<T,E>`、`ErrorCatalog`，不依赖框架。
- **框架适配层（libs/drogon）**：提供 `ErrorHandler`、`ErrorResponder`、`OAuth2ErrorHandler`、`RequestId`，把领域错误映射到 Drogon `HttpResponse`。
- **应用入口（apps/server/bootstrap）**：在启动阶段注册全局异常处理器，按请求路径区分 OAuth2 协议端点与应用端点，分别走 RFC 6749 或统一 Error Envelope 分支。

## 2. 关键文件与职责

| 文件 | 职责 |
|---|---|
| `libs/common/include/authforge/common/error/ErrorTypes.h` | 定义 `ErrorCategory`（NETWORK/DATABASE/VALIDATION/AUTHENTICATION/AUTHORIZATION/INTERNAL/UNKNOWN）和 `Error` 结构体（code/category/message/details/requestId），并提供 `fromCode` / `fromException` / `toJson` / `toHttpStatusCode` |
| `libs/common/include/authforge/common/error/ErrorCatalog.h` | 单一定义的 `CatalogEntry`（Application 错误码）与 `OAuthCatalogEntry`（RFC 6749 §5.2 协议错误码），提供 `find` / `findByNumeric` / `allEntries` / `validateInvariants` 等静态接口 |
| `libs/drogon/include/authforge/drogon/error/ErrorResponder.h` | 统一 Application_Endpoint 错误响应器：`respond` / `respondValidation` / `respondException` / `buildResponse`，负责注入 Request_ID、生产模式隐藏 details、设置 Content-Type: application/json |
| `libs/drogon/include/authforge/drogon/error/OAuth2ErrorHandler.h` | 专门处理 OAuth2.0 协议错误（RFC 6749 §5.2），支持 `invalid_client` 时附加 `WWW-Authenticate` 挑战头 |
| `libs/drogon/include/authforge/drogon/error/ErrorHandler.h` | 将 Drogon ORM 异常、验证失败转换为领域 `Error` |
| `apps/server/src/bootstrap/ExceptionHandlerSetup.cc` | 启动时调用 `drogon::app().setExceptionHandler(...)`，根据 path 前缀 `/oauth2/`、`/.well-known/oauth-authorization-server`、`/.well-known/openid-configuration`、`/.well-known/jwks.json` 判断是否走 OAuth2 协议分支 |
| `libs/common/include/authforge/common/result/Result.h` | 领域层同步返回值的 `Result<T, E>` 类型（默认 `E = Error`），对误用 `value()` / `error()` 抛出 `BadResultAccess`（编程错误，非可恢复条件） |

## 3. 架构与约定

### 3.1 错误码与分类
- 每个错误都有**稳定的字符串 `Error_Code`**（如 `AUTH_INVALID_CREDENTIALS`）和对应的 `ErrorCategory`。
- `ErrorCatalog` 是唯一来源，维护 code → numeric_code → category → http_status → default message → description 的映射，并在启动时通过 `validateInvariants()` 断言唯一性、数值段范围、HTTP 状态码合法性、描述长度等不变量。
- `Error::fromCode(code, requestId)` 会从 Catalog 填充 category、默认消息与 HTTP 状态码；未注册码回退到 INTERNAL_ERROR（numeric 6001）。
- `Error::fromException(e, category, requestId)` 捕获异常文本作为 `details`，未映射异常同样回退到 INTERNAL_ERROR。

### 3.2 响应格式
- **Application_Endpoint**：统一返回 `{ error: { code, category, message, [details], request_id } }`，HTTP 状态码来自 Catalog。
- **OAuth2 协议端点**：返回 RFC 6749 §5.2 形状 `{ error, [error_description], [error_uri] }`，由 `OAuth2ErrorHandler::sendErrorResponse` 驱动，始终设置 `Cache-Control: no-store`、`Pragma: no-cache`、`Content-Type: application/json`。
- 生产模式下 `details` 字段被完全省略；仅在允许详细错误的环境（非 Production_Mode）才包含。

### 3.3 全局异常兜底
`ExceptionHandlerSetup::setupExceptionHandler()` 注册 Drogon 全局异常处理器：
- 记录未处理异常日志。
- 若请求路径属于 OAuth2 协议端点，发送 `server_error` 协议错误。
- 否则构造 INTERNAL_ERROR 并通过 `ErrorResponder::buildResponse` 输出统一 Error Envelope。
- 两个分支都会保留 CORS 头注入逻辑。

### 3.4 领域层同步错误：`Result<T,E>`
领域服务方法使用 `Result<T, E>` 表达成功/失败，避免在同步路径抛异常。访问错误分支时使用 `isError()` / `ok()` 检查；误用 `value()` / `error()` 会抛出 `BadResultAccess`（继承自 `std::logic_error`），视为编程错误而非运行时异常。

### 3.5 值对象输入校验
模型类（如 `ClientId`、`PkceChallenge`、`RedirectUri`、`Scope`、`Subject`、`TokenValue`）在构造函数中对空值等非法输入直接 `throw std::invalid_argument(...)`，由上层通过 `ErrorResponder::respondException` 或 `ErrorHandler::handleValidationError` 转为 VALIDATION 类别的 Error Envelope。

## 4. 约束与规则（来自代码注释与实现）

- **单一定义原则**：`ErrorCatalog` 是所有 Application 错误码与 OAuth2 协议错误码的唯一来源；所有运行时入口、文档生成与测试都从这些表读取，禁止散落硬编码映射。
- **未注册码安全降级**：`ErrorResponder::respond` 对未注册 code 永不抛出、永不泄漏，仅记录 LOG_ERROR 并回退到 INTERNAL_ERROR。
- **异常→错误转换**：`Error::fromException` 与 `ErrorResponder::respondException` 对所有未映射异常统一回退到 INTERNAL_ERROR，并把异常文本写入 Internal_Detail。
- **OAuth2 协议兼容性**：`OAuth2ErrorHandler::sendErrorResponse` 在 `invalid_client` 且提供 `authScheme` 时必须附加匹配的 `WWW-Authenticate` 挑战头（RFC 6749 §5.2）。
- **启动期不变量校验**：`ErrorCatalog::validateInvariants()` 必须通过，否则以 `std::logic_error` 抛出（Domain 层不依赖 Drogon 日志宏），由服务端 bootstrap 捕获并终止启动。
- **Request_ID 贯穿**：所有 Error Envelope 必须携带 `request_id`，由 `RequestId::resolve(req)` 注入，用于关联请求与日志。
- **结果访问契约**：`Result<T,E>::value()` / `error()` 在错误变体上调用抛出 `BadResultAccess`，调用方必须先检查 `ok()` / `isError()`。

## 5. 测试覆盖

单元测试集中在 `tests/unit/error/`，包括 `ErrorCatalogPropertyTest`、`ErrorEnvelopePropertyTest`、`ErrorResponderTest`、`OAuth2ErrorPropertyTest`、`OAuth2InvalidClientHeaderTest`、`RequestIdPropertyTest` 等，对 Catalog 不变量、Envelope 结构、OAuth2 协议错误及 WWW-Authenticate 行为进行属性/回归测试。

## 6. 适用性说明

本仓库是一个 C++ / Drogon 后端项目，错误处理体系完整且跨模块一致，因此本类别**适用**，置信度为 **high**。