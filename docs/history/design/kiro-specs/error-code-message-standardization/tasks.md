# Implementation Plan: 错误码与错误信息标准化（error-code-message-standardization）

## Overview

本实现计划将设计文档转化为一系列可增量执行、可独立验证的编码任务。整体顺序为：先建立后端单一权威来源 `ErrorCatalog`，再重构核心错误类型与上下文工具（`Error`/`ErrorTypes`、`RequestId`、`ErrorContext`），随后落地两条统一入口（`ErrorResponder` 与改造后的 `OAuth2ErrorHandler`、`HttpResponder`），接着收口全局异常处理器与各控制器迁移；前端建立共享 `errorAdapter` 与 `Error_Message_Catalog_FE`（i18n）并改造拦截器与视图；最后补齐由 Catalog 生成/校验的文档、端点级集成测试与 CI 门禁。

设计为纯函数式核心，适合基于属性的测试（PBT）：
- 后端复用 Drogon 自带 `drogon_test.h`（`DROGON_TEST` 宏），在测试体内手写随机生成循环，**每条属性最少迭代 100 次**，失败时打印所用 code/输入与固定种子以便复现；新增测试位于 `OAuth2Server/test/unit/error/`，因 `test/CMakeLists.txt` 使用 `GLOB_RECURSE unit/*.cc` 会被自动纳入。
- 前端在 OAuth2Frontend 与 OAuth2Admin 引入 `vitest` + `fast-check`（`{ numRuns: 100 }`）。
- 每条属性测试以注释标注：`Feature: error-code-message-standardization, Property {number}: {property_text}`。

约定：标注 `*` 的子任务为可选测试任务（单元/属性/集成测试），可在追求 MVP 时跳过；顶层任务不带 `*`，为核心实现，必须实现。

## Tasks

- [x] 1. 建立后端错误码目录（ErrorCatalog，单一权威来源）
  - [x] 1.1 定义 ErrorCatalog 数据结构、静态表与启动期自检
    - 新增 `OAuth2Plugin/include/oauth2/error/ErrorCatalog.h` 与 `OAuth2Plugin/src/error/ErrorCatalog.cc`
    - 定义 `CatalogEntry`（code/numericCode/category/httpStatus/defaultMessage/description）与 `OAuthCatalogEntry`（error/httpStatus/defaultErrorDesc/errorUri）
    - 以编译期静态表登记现有 14 个错误码并**保留其整数取值不变**（`NET_CONNECTION_FAILED`=1001 … `INTERNAL_ERROR`=6001），HTTP 状态码按 Requirement 4 类别/数值规则生成
    - 登记全部 OAuth2 协议错误码（`invalid_request`/`invalid_client`=401 等）及 RFC 7009/7662/8628 相关码，含 HTTP 状态码与默认 `error_description`
    - 实现 `find`/`findByNumeric`/`findOAuth`/`allEntries`/`allOAuthEntries`/`internalError`
    - 实现 `validateInvariants()`：断言 code 唯一、numeric_code 唯一且落在所属类别段位区间、httpStatus∈[100,599]、默认 Client_Safe_Message 非空、说明长度 1..200、协议码集合覆盖恰好一条目；任一不满足则 fail-fast（致命退出）
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.8, 2.6, 11.5, 11.6_

  - [x] 1.2 编写 ErrorCatalog 完整性与唯一性属性测试
    - **Property 5: Error_Catalog 完整性与唯一性**
    - **Validates: Requirements 3.1, 3.2, 3.3, 3.8, 2.6, 11.6**
    - 新增 `OAuth2Server/test/unit/error/ErrorCatalogPropertyTest.cc`，遍历 `allEntries()`/`allOAuthEntries()` 断言上述不变量

  - [x] 1.3 编写现有数值码回归示例测试
    - 验证 14 个现有 Numeric_Error_Code 取值未变、各类别 numeric 落在段位内、协议码集合无遗漏
    - 新增 `OAuth2Server/test/unit/error/ErrorCatalogRegressionTest.cc`
    - _Requirements: 3.6, 11.5_

- [x] 2. 重构后端核心错误类型 Error / ErrorTypes
  - [x] 2.1 将 Error 的整数 code 迁移为字符串 Error_Code 并查表生成 Envelope
    - 修改 `OAuth2Plugin/include/oauth2/error/ErrorTypes.h` 与 `OAuth2Plugin/src/error/ErrorHandler.cc`
    - `Error` 结构改为字符串 `code`，新增 `hasNumericCode()`/`numericCode()`（查 Catalog），`toHttpStatusCode()` 改为查 Catalog
    - `toJson(bool includeDetails)` 产出 Error Envelope：顶层仅 `error` 键；含非空 `code`、枚举 `category`、长度 1..500 的 `message`、非空 `request_id`；`numeric_code` 仅在 Catalog 登记时出现、否则整字段省略；`details` 仅在 includeDetails 时出现
    - 实现 `Error::fromCode(code, requestId)` 与 `Error::fromException(e, category, requestId)`（未登记→`internalError()`）；保留 `ErrorCategory` 枚举名不变
    - _Requirements: 1.2, 1.3, 1.7, 4.7, 5.5, 11.5_

  - [x] 2.2 编写 Error Envelope 序列化 round-trip 属性测试
    - **Property 3: Error Envelope 序列化 round-trip**
    - **Validates: Requirements 1.5, 12.3**
    - 新增/追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，随机构造 Error（含/不含 numeric_code、含/不含 details），编码→解码后比对 `code`/`category`/`message`/`numeric_code`/`request_id`

- [x] 3. 实现 RequestId 与 ErrorContext 工具
  - [x] 3.1 实现 RequestId 解析与生成工具
    - 新增 `OAuth2Plugin/include/oauth2/error/RequestId.h` 与 `OAuth2Plugin/src/error/RequestId.cc`
    - `resolve(req)`：复用合法 `X-Request-ID` 请求头（非空、长度≤128、仅 `[A-Za-z0-9_-]`），否则生成新值；`isValid(v)` 实现校验
    - `generate()` **必须跨请求唯一**：复用 `drogon::utils::getUuid()`（与 `OAuth2Plugin/src/observability/AuditLogger.cc` 保持一致），保证同一实例上互不相同且长度 1..128
    - _Requirements: 6.1, 6.3, 6.4, 6.5_

  - [x] 3.2 编写 Request_ID 解析与生成属性测试
    - **Property 10: Request_ID 解析与生成**
    - **Validates: Requirements 6.1, 6.3, 6.4, 6.5**
    - 新增 `OAuth2Server/test/unit/error/RequestIdPropertyTest.cc`，生成合法/非法（空、超长、越界字符）请求头样本，断言复用/重新生成规则与连续生成互不相同

  - [x] 3.3 实现 ErrorContext 生产模式判定
    - 新增 `OAuth2Plugin/include/oauth2/error/ErrorContext.h` 与 `OAuth2Plugin/src/error/ErrorContext.cc`
    - `detailedErrorsAllowed()`：`#ifdef DEBUG` 或 `DETAILED_VALIDATION_ERRORS` 开关；提供测试可注入开关以切换 Production_Mode
    - _Requirements: 5.1, 5.4_

- [x] 4. 实现 Application 统一入口 ErrorResponder
  - [x] 4.1 实现 ErrorResponder 主入口与便捷入口
    - 新增 `OAuth2Plugin/include/oauth2/error/ErrorResponder.h` 与 `OAuth2Plugin/src/error/ErrorResponder.cc`
    - `respond(req, cb, code, detailForLog, clientDetails)`：查 Catalog→生产环境强制使用默认 Client_Safe_Message→注入 `RequestId::resolve`→按 `ErrorContext` 决定 `details`→设置 Catalog 登记的 HTTP 状态码与 `Content-Type: application/json`→调用日志记录（含 Internal_Detail 与 request_id）
    - `respondValidation(...)`（VALIDATION 类便捷入口）、`respondException(...)`（未登记异常→`INTERNAL_ERROR`）、`buildResponse(req, error)`（供全局异常处理器复用）
    - 未登记 code 时记录 `LOG_ERROR` 并回退 `INTERNAL_ERROR`，绝不抛出或泄露原始 code
    - _Requirements: 7.1, 1.4, 5.2, 5.5, 5.6, 6.2_

  - [x] 4.2 编写 Error Envelope 结构不变量属性测试
    - **Property 1: Error Envelope 结构不变量**
    - **Validates: Requirements 1.1, 1.2, 1.4, 1.6, 7.5**
    - 追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，对任意已登记 code 断言顶层仅 `error` 键、字段类型/取值与键集合白名单、`Content-Type` 为 `application/json`

  - [x] 4.3 编写 numeric_code 正确性与省略属性测试
    - **Property 2: numeric_code 正确性与省略**
    - **Validates: Requirements 1.3, 1.7**
    - 追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，断言登记则等于登记值、未登记则完全省略该键

  - [x] 4.4 编写 HTTP 状态码一致性属性测试
    - **Property 4: HTTP 状态码一致性**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.6, 4.7, 4.8, 4.9, 2.7, 7.4, 12.2**
    - 追加 `OAuth2Server/test/unit/error/ErrorCatalogPropertyTest.cc`，断言运行时状态码等于 Catalog 登记值且满足类别/数值映射规则与同类别一致性

  - [x] 4.5 编写生产模式安全隔离属性测试
    - **Property 6: 生产模式安全隔离**
    - **Validates: Requirements 5.1, 5.3, 5.6, 12.4**
    - 追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，注入含 SQL/路径/堆栈样式片段的恶意文本，断言生产模式下完全不含 `details`、`message` 等于 Catalog 默认值、所有字符串字段不含敏感细节；该断言在开发/预发布/生产配置下均执行

  - [x] 4.6 编写非生产模式诊断信息属性测试
    - **Property 7: 非生产模式诊断信息**
    - **Validates: Requirements 5.4, 7.6**
    - 追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，断言非生产/开关开启时 `details` 存在并含诊断信息，VALIDATION 转换体含字段名与失败原因

  - [x] 4.7 编写未登记异常内部错误兜底属性测试
    - **Property 8: 未登记异常的内部错误兜底**
    - **Validates: Requirements 5.5**
    - 追加 `OAuth2Server/test/unit/error/ErrorEnvelopePropertyTest.cc`，对任意未登记异常断言 `category=INTERNAL`、`code` 对应 numeric 6001、`message` 等于其 Catalog 默认值

- [x] 5. 改造 OAuth2 协议错误入口 OAuth2ErrorHandler
  - [x] 5.1 使协议响应由 Catalog 驱动并补全合规头
    - 修改 `OAuth2Plugin/src/error/OAuth2ErrorHandler.cc` 与 `OAuth2Plugin/include/oauth2/error/OAuth2ErrorHandler.h`
    - `getHttpStatusCode` 改为查 `ErrorCatalog::findOAuth`；`sendErrorResponse` 在 `error_description` 为空时回退 Catalog 默认值；新增可选 `authScheme` 参数，在 `invalid_client` 且客户端经 `Authorization` 头认证时设置匹配的 `WWW-Authenticate`
    - 保留 `Cache-Control: no-store`、`Pragma: no-cache`、`Content-Type: application/json`，并保持 `error`/`error_description`/`error_uri` 字段与取值符合 RFC 6749 §5.2
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.7, 2.8, 2.9, 7.2, 11.3_

  - [x] 5.2 编写 OAuth2 协议端点 RFC 6749 合规属性测试
    - **Property 9: OAuth2 协议端点 RFC 6749 合规**
    - **Validates: Requirements 2.1, 2.2, 2.3, 2.5, 2.8, 11.3**
    - 新增 `OAuth2Server/test/unit/error/OAuth2ErrorPropertyTest.cc`，对任意协议错误码断言顶层 `error` 为允许集合内字符串、`error` 非 Envelope 对象、`error_uri` 为字符串、`error_description` 非空且等于/包含默认值且无 Internal_Detail、缓存头齐全

  - [x] 5.3 编写 invalid_client 的 WWW-Authenticate 头专项测试
    - 验证 Requirement 2.9：当 `invalid_client` 且客户端经 `Authorization` 头认证时，响应设置与所用认证方案匹配的 `WWW-Authenticate` 头，并保持 HTTP 401
    - 新增 `OAuth2Server/test/unit/error/OAuth2InvalidClientHeaderTest.cc`（示例/边界测试）
    - _Requirements: 2.9, 2.4_

- [x] 6. 改造校验响应器 HttpResponder 输出 VALIDATION Envelope
  - [x] 6.1 使校验错误产出 VALIDATION 类 Error Envelope
    - 修改 `OAuth2Plugin/src/validation/HttpResponder.cc` 与 `OAuth2Plugin/include/oauth2/validation/HttpResponder.h`
    - `buildErrorJson` 改为委托 `ErrorResponder` 输出 `code=VALIDATION_INVALID_INPUT`、`category=VALIDATION`、HTTP 400 的 Envelope；移除 `error_description`/`reason`/`VALIDATION_ERROR` 等别名；非生产环境在 `details` 列出字段名与失败原因
    - _Requirements: 7.4, 7.5, 7.6_

  - [x] 6.2 编写校验错误转换单元测试
    - 验证字段级错误转换为 VALIDATION Envelope、状态码 400、生产环境隐藏 `details`、非生产含字段名与原因
    - 新增 `OAuth2Server/test/unit/validation/ValidationEnvelopeTest.cc`
    - _Requirements: 7.4, 7.6_

- [x] 7. 收口全局异常处理器并迁移各控制器
  - [x] 7.1 改造 main.cc 全局异常处理器并接入 Catalog 自检
    - 修改 `OAuth2Server/main.cc`：在 `registerBeginningAdvice` 调用 `ErrorCatalog::validateInvariants()`；`setExceptionHandler` 按请求路径分流——OAuth2 协议路径走 `OAuth2ErrorHandler` 产出 `server_error` 体，其余走 `ErrorResponder::buildResponse(INTERNAL_ERROR)` 产出 Envelope，替换原 `{ "error": "server_error", ... }` 文本；**保留现有 CORS 头注入逻辑**
    - _Requirements: 7.7, 3.5_

  - [x] 7.2 迁移面向用户的控制器至统一入口
    - 修改 `OAuth2Server/controllers/` 下：`UserSelfServiceController.cc`、`EmailVerificationController.cc`、`PasswordResetController.cc`、`SessionController.cc`、`MfaController.cc`、`WebAuthnController.cc`、`WeChatController.cc`、`GoogleController.cc`、`GitHubController.cc`
    - 移除即兴错误格式（`{ "error": "not_found" }`、`{ "message": ... }`、纯文本等），改经 `ErrorResponder` 输出 Envelope；为迁移期发现的即兴错误在 Catalog 内分配条目（整数在对应段位顺序分配，不与现有码冲突）
    - _Requirements: 7.1, 7.3, 7.5, 3.4_

  - [x] 7.3 迁移管理与平台类控制器至统一入口
    - 修改 `OAuth2Server/controllers/` 下：`AdminController.cc`、`ClientRegistrationController.cc`、`OrganizationController.cc`、`ApiDocController.cc`、`DeviceAuthController.cc`、`HealthController.cc`
    - 统一改经 `ErrorResponder` 输出 Envelope，确保无非 JSON（纯文本/HTML）错误响应体
    - _Requirements: 7.1, 7.3, 7.5_

- [x] 8. 检查点 - 确保后端测试通过
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. 建立前端共享错误模块 errorAdapter 与 i18n 目录
  - [x] 9.1 引入 vitest + fast-check 测试基础设施
    - 在 `OAuth2Frontend` 与 `OAuth2Admin` 添加 `vitest` + `fast-check` devDependency、`vitest.config.ts` 与 `test:unit` 脚本（修改各自 `package.json`）
    - _Requirements: 12.5_

  - [x] 9.2 实现共享 errorAdapter（normalizeError，永不抛出）
    - 新增单一来源 `OAuth2Frontend/src/services/errorAdapter.ts`，供两应用同源导入（OAuth2Admin 复用同一实现来源）
    - `normalizeError(err, locale?)` 返回 `{ code, message, request_id, httpStatus }`：解析优先级 Error Envelope→RFC 6749→通用未知→无响应体网络回退（`httpStatus=0`）；全程可选链与类型守卫，永不抛出
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_

  - [x] 9.3 实现 Error_Message_Catalog_FE（i18n，镜像后端码 + 协议码）
    - 新增 `messages/zh-CN.ts`（与 errorAdapter 同源共享），键覆盖后端全部 Error_Code 与全部 OAuth2 协议码，含保留键 `__unknown__`、`__network__`
    - 缺失键回退 `__unknown__` 并 `console.warn`，缺失语言回退默认语言 zh-CN；条目非空、无未替换占位符、不含 Request_ID 之外的 Internal_Detail
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.6_

  - [x] 9.4 编写前端规范化全域性与字段不变量属性测试
    - **Property 11: 前端规范化全域性与字段不变量**
    - **Validates: Requirements 8.1, 8.5, 9.5**
    - 新增 `errorAdapter.property.test.ts`，以 `fc.anything()` 与构造的 axios 错误断言 `normalizeError` 不抛出、字段类型不变量、无响应体时 `code` 为网络回退码且 `httpStatus=0`、含 Request_ID 时透传

  - [x] 9.5 编写前端格式解析与本地化映射属性测试
    - **Property 12: 前端格式解析与本地化映射**
    - **Validates: Requirements 8.2, 8.3, 8.4, 9.2, 9.6, 12.5**
    - 追加 `errorAdapter.property.test.ts`，生成 Envelope/RFC 6749/畸形响应体，断言 `code` 取值来源正确、zh-CN 下任一 code 返回非空本地化信息

  - [x] 9.6 编写前端信息目录覆盖与清洁性属性测试
    - **Property 13: 前端信息目录覆盖与清洁性**
    - **Validates: Requirements 9.1, 9.4**
    - 新增 `messageCatalog.property.test.ts`，遍历后端 Error_Code 与协议码集合断言均有非空、无占位符的条目，且无 SQL/路径/堆栈等 Internal_Detail

  - [x] 9.7 编写跨应用映射确定性一致属性测试
    - **Property 14: 跨应用映射确定性一致**
    - **Validates: Requirements 10.7**
    - 新增 `crossAppConsistency.property.test.ts`，断言两应用经同源 errorAdapter 与目录对同一 code+locale 得到相同 message

- [x] 10. 前端拦截器与视图改造
  - [x] 10.1 改造 OAuth2Frontend 拦截器与视图
    - 修改 `OAuth2Frontend/src/services/http.ts`、`OAuth2Frontend/src/stores/auth.ts` 及相关视图：错误统一经 `normalizeError` 展示，禁止直接读 `e.response.data.message`/`error_description`/`data`；401 刷新失败时展示会话失效信息并跳转登录视图
    - _Requirements: 10.1, 10.3, 10.4, 10.5, 8.6_

  - [x] 10.2 改造 OAuth2Admin 拦截器与页面并移除原生 alert
    - 修改 `OAuth2Admin/src/stores/auth.ts`（axios 拦截器）及 `OAuth2Admin/src/pages/` 下页面（含 `users/UsersPage.vue`、`applications/ApplicationsPage.vue`）：错误统一经 `normalizeError` 展示，移除原生 `alert` 与直接读 `e.response.data.*`；401 刷新失败时展示会话失效信息并跳转登录视图
    - _Requirements: 10.2, 10.3, 10.4, 10.5, 10.6_

- [x] 11. 由 Catalog 生成/校验错误码文档
  - [x] 11.1 实现错误码文档生成/校验并更新 api-reference
    - 新增校验/生成程序（后端测试或脚本），由 `ErrorCatalog::allEntries()`/`allOAuthEntries()` 生成或校验 `docs/backend/api-reference.md` 的「通用错误码」章节，确保每个 Error_Code 与每个协议码各一条目并含 HTTP 状态码与 Error_Category，不一致即失败
    - _Requirements: 3.5, 3.7, 12.1_

- [x] 12. 端点级集成测试与 CI 门禁
  - [x] 12.1 编写 Application_Endpoint 错误响应枚举集成测试
    - 枚举全部 Application_Endpoint 的错误响应体，断言均可解析为 Error Envelope，存在纯文本/不可解析体即失败
    - 新增 `OAuth2Server/test/integration/` 下集成测试
    - _Requirements: 7.1, 7.3, 12.6_

  - [x] 12.2 编写 OAuth2 协议端点 RFC 合规集成测试
    - 验证协议端点错误体符合 RFC 6749 §5.2，且经协议入口产生
    - _Requirements: 7.2, 11.3_

  - [x] 12.3 编写未捕获异常经统一入口集成测试
    - 触发处理过程抛出异常的 Application_Endpoint，断言返回 Envelope 而非框架默认 HTML/纯文本
    - _Requirements: 7.7_

  - [x] 12.4 编写成功响应体黄金快照回归测试
    - 对现有端点成功响应（2xx）与路由清单做黄金快照，保证迁移不改变成功体结构、路径与方法
    - _Requirements: 11.1, 11.2, 11.4_

  - [x] 12.5 接入 CI 门禁
    - 在发版前流水线运行后端属性/一致性/安全测试、前端映射属性测试、Application_Endpoint 枚举集成测试；安全属性（Property 6）在开发/预发布/生产配置下均执行；任一未通过则发版就绪检查失败（修改 CI 配置）
    - _Requirements: 12.7, 12.2, 12.3, 12.4, 12.5, 12.6_

- [x] 13. 最终检查点 - 确保前后端全部测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- 标注 `*` 的子任务为可选测试任务，可在追求 MVP 时跳过；顶层任务与未标注 `*` 的子任务为核心实现，必须实现。
- 每条任务引用具体的需求子条款（granular clause）以保证可追溯性。
- 检查点（任务 8、13）用于增量验证。
- 14 条属性测试逐条独立成子任务，标注其 Property 编号与所验证的需求条款，并尽量靠近其实现任务以尽早发现错误。
- 后端属性测试在 `DROGON_TEST` 内手写随机循环（≥100 次迭代）；前端使用 `vitest` + `fast-check`（`numRuns: 100`）。
- 评审意见已纳入：`RequestId::generate()` 复用 `drogon::utils::getUuid()` 保证跨请求唯一（任务 3.1）；新增 Requirement 2.9 的 `WWW-Authenticate` 专项测试（任务 5.3）。

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "3.1", "3.3", "9.1", "9.2", "9.3"] },
    { "id": 1, "tasks": ["2.1", "1.2", "1.3", "3.2", "5.1", "11.1", "9.4", "9.6", "9.7", "10.1", "10.2"] },
    { "id": 2, "tasks": ["2.2", "4.1", "5.2", "5.3", "9.5"] },
    { "id": 3, "tasks": ["4.2", "4.4", "6.1", "7.1", "7.2", "7.3"] },
    { "id": 4, "tasks": ["4.3", "6.2", "12.1", "12.2", "12.3", "12.4"] },
    { "id": 5, "tasks": ["4.5"] },
    { "id": 6, "tasks": ["4.6"] },
    { "id": 7, "tasks": ["4.7"] },
    { "id": 8, "tasks": ["12.5"] }
  ]
}
```
