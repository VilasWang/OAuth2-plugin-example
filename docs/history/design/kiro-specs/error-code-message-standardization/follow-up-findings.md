# 后续待办：error-code-message-standardization 漏网之鱼

> 本文件记录该特性 PR（分支 `feature/error-code-message-standardization`，base=`master`）
> 经系统性深查后发现、但**尚未修复**的遗留问题。深查方式：以 feat 提交 `077248c` 为界，
> 用 `git show 077248c^:<path>` 对照「迁移前基线」与「迁移后」的错误响应行为，并逐条核对
> requirements.md / design.md 的落地情况、测试盲区与文档一致性。
>
> 已在本 PR 内修复、无需再处理的两类回归（仅作模式参考）：
> 1. 前端 `auth.spec` e2e mock 仍用旧错误体 + 旧英文断言 —— 已修。
> 2. 控制器 404（资源不存在）/409（名称冲突）被压成 400/500，违反 Req 11.4 ——
>    已新增 `VALIDATION_RESOURCE_NOT_FOUND(3004→404)` / `VALIDATION_RESOURCE_CONFLICT(3005→409)`
>    并修复 Admin/Organization/UserSelfService/EmailVerification/ApiDoc 五个控制器。

状态图例：⬜ 待处理 ｜ 🟦 进行中 ｜ ✅ 已完成

---

## 🔴 Major

### F1 ✅ 两个鉴权过滤器从未迁移，受保护端点返回旧格式 / 纯文本错误
- **严重度**：Major（契约破坏，影响面大）
- **位置**：
  - `OAuth2Plugin/src/filters/AuthorizationFilter.cc`
    - `:97-99` → `{"error":"unauthorized"}`（401）
    - `:132-134` → `{"error":"invalid_token"}`（401）
    - `:150-153` → `{"error":"forbidden","message":"Insufficient permissions"}`（403）
    - `:108-113` → `CT_TEXT_PLAIN` 纯文本 500
  - `OAuth2Plugin/src/filters/OAuth2AuthFilter.cc`
    - `:29-30` / `:46-47` → `setBody("Missing or invalid Authorization header")` / `"Invalid or expired token"`（纯文本 401）
- **影响范围**：这两个过滤器经 `ADD_METHOD_TO(..., "oauth2::filters::AuthorizationFilter")` / `OAuth2AuthFilter`
  守护着几乎所有 Application 端点（AdminController 全量、ClientRegistration、Organization、
  DeviceAuth、Mfa、EmailVerification 等）。这些端点在「token 缺失 / 无效 / 越权」时返回的
  是旧即兴格式 + 纯文本，而非统一 Error Envelope。
- **为何是问题**：违反 Req 7.1（每个 Application_Endpoint 经统一入口）、7.3（禁止非 JSON 错误体）、
  7.5（禁止 `error` / `message` 别名）。前端 `normalizeError` 遇纯文本体会落入「通用未知错误」分支，
  丢失「会话失效 / 权限不足」语义（纯 `forbidden` 文本无法区分）。
- **基线 vs 迁移后**：基线即如此；本特性迁移只覆盖了控制器层，**漏掉了过滤器层**（既非回归，
  也是本特性「统一所有 Application 端点」目标的未覆盖缺口）。
- **建议修复**：两过滤器改经 `common::error::ErrorResponder::buildResponse(req, error)` 构体后 `fcb(resp)`：
  - 401 token 缺失/无效 → `AUTH_TOKEN_INVALID`
  - 403 越权 → `AUTHZ_INSUFFICIENT_PERMISSIONS`
  - 500 plugin 缺失 → `INTERNAL_ERROR`
  注意过滤器回调签名与控制器不同，需用 `buildResponse(req, Error)` 直接构体（而非 `respond(...)` 的 cb 形态）。

### F2 ✅ Req 2.9 `WWW-Authenticate` 能力空转（introspect/revoke 不传 authScheme）
- **严重度**：Major（验收点未真正落地）
- **位置**：`OAuth2Plugin/src/controllers/OAuth2StandardController.cc`
  introspect `:357` / `:395`、revoke `:480` / `:517` 四处 `invalid_client` 调用。
- **现状**：`OAuth2ErrorHandler::sendErrorResponse` 已新增第 5 参 `authScheme`
  （`OAuth2ErrorHandler.h:44`），`invalid_client` 时据其设 `WWW-Authenticate`。但上述四处调用
  **均未传** `authScheme`（只传到第 3 参 description）。而 `extractClientCredentials(:309-341)`
  明确优先解析 `Authorization: Basic` 头 —— 即客户端确经 Authorization 头认证，Req 2.9 前置条件成立，
  本应回 `WWW-Authenticate: Basic`，实际从不设置。
- **为何是问题**：Req 2.9 验收点空转；RFC 6749 §5.2 要求 401 + Authorization 头认证失败时返回匹配的
  `WWW-Authenticate` challenge。
- **建议修复**：在这 4 处（以及检测到 Basic 头时）传 `"Basic"`；可由 `extractClientCredentials` 顺带返回所用 scheme。

### F3 ✅ Req 2.9 真实控制器路径无集成测试（致 F2 漏网）
- **严重度**：Major（测试盲区）
- **位置**：`OAuth2Server/test/unit/error/OAuth2InvalidClientHeaderTest.cc`
- **现状**：该测试只**直接调** `OAuth2ErrorHandler` 并传 `authScheme` 验证 header 生成，
  **未经** `OAuth2StandardController` 的 introspect/revoke 端点验证「带 Basic 头认证失败时实际是否回
  `WWW-Authenticate`」。正因缺此端点级测试，F2 的空转才漏网。
- **建议修复**：补一条端点级集成测试：对 introspect/revoke 发带错误 `Authorization: Basic` 的请求，
  断言响应含 `WWW-Authenticate: Basic ...` 且 HTTP 401。修 F2 后此测试应转绿。

---

## 🟡 Minor

### F4 ✅ 校验错误响应不复用入站 X-Request-ID
- **严重度**：Minor（Req 6.3 部分未满足）
- **位置**：`OAuth2Plugin/src/validation/HttpResponder.cc:50`（`buildErrorJson`）
- **现状**：用 `RequestId::generate()`（新 UUID）而非 `RequestId::resolve(req)`，因此校验错误响应
  不复用入站 `X-Request-ID` 头。根因：`buildErrorJson` / `buildErrorResponse` 签名不接 `req`。
- **建议修复**：透传 `req` 或新增接收 `req` 的重载，改用 `RequestId::resolve(req)`。

### F5 ✅ Admin 四个数据页失败时不展示本地化错误
- **严重度**：Minor（Req 10.1/10.2 的 UX 覆盖缺口；未违反 10.3）
- **位置**：`OAuth2Admin/src/pages/` 下 `dashboard/DashboardPage.vue`、`logs/LogsPage.vue`、
  `settings/SettingsPage.vue`、`tokens/TokensPage.vue`
- **现状**：catch 后仅 `console.error` 静默，不向用户展示错误（没有直读 `e.response.data.*`，
  故不违反 10.3，但失败时用户看不到任何本地化提示）。
- **建议修复**：至少给这 4 个数据页加 `normalizeError(e)` 展示（inline banner / toast）。
- 说明：`OAuth2Frontend` 的 `ForgotPasswordPage.vue`（catch 后强制 success，反枚举）与
  `oauth/ConsentPage.vue`（catch 仅处理 302 重定向）属合理设计，无需改。

### F6 ✅ api-reference §5.3 速查表 invalid_client 状态码错误
- **严重度**：Minor（文档一致性）
- **位置**：`docs/backend/api-reference.md`（§5.3「HTTP 状态码速查」，约 `:266` 行 `400` 一行）
- **现状**：§5.3 把 `invalid_client` 列在 `400` 行的原因示例里，但 §5.2 表与 `ErrorCatalog`
  （`rawOAuthEntries` `{"invalid_client", 401, ...}`）均为 `401`。§5.3 是人工维护的速查表，
  **未被** `ErrorCatalogDocTest` 校验（该测试只校 §5.1 / §5.2 表），故不一致逃过 CI。
- **建议修复**：把 `invalid_client` 从 §5.3 的 400 行移到 401 行。

---

## 🔴 Major（来源：Claude Code 评审，已逐条核实代码确认真实存在）

### F7 ✅ token 端点本地 `getHttpStatusCodeForError()` 把 `unauthorized_client` 映射为 401，与 ErrorCatalog 的 400 矛盾
- **严重度**：Major（与单一权威来源矛盾，违反 Req 2.2 / 2.7）
- **位置**：`OAuth2Plugin/src/controllers/OAuth2StandardController.cc`
  - 本地函数 `getHttpStatusCodeForError()` `:26-34`：`if (errorCode == "invalid_client" || errorCode == "unauthorized_client") return k401Unauthorized; return k400BadRequest;`
  - 调用点 `:1171`、`:1194`（token 路径：exchangeCodeForToken / refreshAccessToken 的 result；client_credentials 亦经此路径）
- **核实**：`ErrorCatalog.cc:126` 登记 `{"unauthorized_client", 400, ...}`；OAuth2 协议状态码应取自 Catalog（单一权威来源）。本地函数把 `unauthorized_client` 判为 401，与 Catalog 的 400 直接冲突。
  - 注：`invalid_client` 在两处都是 401（Catalog 也是 401），属巧合一致，**只有 `unauthorized_client` 真出问题**。
- **为何是问题**：违反 Req 2.2/2.7（协议错误响应的 HTTP 状态码须等于 Catalog 登记值）；同一协议码在不同端点（token vs introspect/revoke 经 `OAuth2ErrorHandler`）状态码不一致。
- **建议修复**：删除本地 `getHttpStatusCodeForError()`，token 路径改用 `OAuth2ErrorHandler::getHttpStatusCode()`（查 Catalog）或直接经 `OAuth2ErrorHandler::sendErrorResponse()` 产出协议错误体。

### F8 ✅ `authorize()` 对"用户缺少角色"硬编码 `unauthorized_client` + HTTP 403（状态码不一致 + 语义误用）
- **严重度**：Major（状态码与 Catalog 不一致 + RFC 语义误用）
- **位置**：`OAuth2Plugin/src/controllers/OAuth2StandardController.cc:955-962`
  —— 用户角色校验失败时 `jsonErr["error"]="unauthorized_client"; resp->setStatusCode(k403Forbidden);`
- **核实**：两层问题：
  1. **状态码**：硬编码 403，而 Catalog 登记 `unauthorized_client→400`，且未经 `OAuth2ErrorHandler` 统一入口。
  2. **语义**：RFC 6749 中 `unauthorized_client` = "客户端无权使用该授权类型"，而此处实际是"**用户**缺少所需角色"，语义错配。更贴切的是 `access_denied`（授权端点错误码），或按授权端点错误约定走重定向回 `redirect_uri?error=...`。
- **为何是问题**：违反 Req 2.7 状态码一致性；协议错误码语义误用会误导集成方。
- **建议修复**：改经 `OAuth2ErrorHandler::sendErrorResponse()` 保持一致；并复核应使用的协议码（倾向 `access_denied`，而非 `unauthorized_client`）。注意 `authorize` 是重定向端点，错误处理方式与 token 端点不同，需确认是直接 JSON 错误体还是重定向错误参数。

### F9 ✅ `ErrorHandler::generateRequestId()` 格式与 `RequestId::generate()` 不一致，且 DB/校验异常路径绕过 `RequestId::resolve()`
- **严重度**：Major（违反 Req 6 / Req 6.3；与 F4 同根因的不同站点）
- **位置**：`OAuth2Plugin/src/error/ErrorHandler.cc`
  - `generateRequestId()` `:224-234`：产出 `"req_" + 8 位十六进制`
  - `handleDbException()` `:255`：`Error::fromCode(code, generateRequestId())`
  - `handleValidationError()` `:262`：`Error::fromCode("VALIDATION_INVALID_INPUT", generateRequestId())`
- **核实**：
  1. **格式不一致**：`generateRequestId()` 产出短十六进制 `req_xxxxxxxx`，而新的 `RequestId::generate()` 产出 UUID。同一系统出现两种 Request_ID 格式。
  2. **跳过头部复用**：`handleDbException` / `handleValidationError` 无 `req` 参数，用 `generateRequestId()` 而非 `RequestId::resolve(req)`，因此**不复用入站 `X-Request-ID`**（Req 6.3 未满足）。
- **为何是问题**：违反 Req 6（统一 Request_ID 生成/复用）；客户端在请求头带的关联 ID 在这些错误路径上丢失，跨日志追踪断链。
- **建议修复**：统一到 `RequestId`：让 `handleDbException` / `handleValidationError` 接收 `req` 并改用 `RequestId::resolve(req)`；`generateRequestId()` 要么内部委托 `RequestId::generate()` 以统一格式，要么标记弃用。与 F4（`validation/HttpResponder.cc:50` 同样用 `generate()` 而非 `resolve(req)`）合并处理。

---

## 已查且确认无问题的维度（覆盖面记录）
- **维度 A 状态码回归**：全部被改后端控制器（含 Mfa/WebAuthn/Session/DeviceAuth/WeChat/Google/
  GitHub/PasswordReset/ClientRegistration/Health/ApiDoc）的基线非 400/401 状态码（403/404/409/502/503）
  均忠实保留，除已修的 404/409 外无其它平压残留。基线全仓无 429/限流错误体。
- **维度 B（控制器层）**：无 Application 端点漏迁、无 OAuth2 协议端点误迁成 Envelope、无业务字段
  （retry_after / mfa_required 等）丢失；控制器内残留的 `["error"]=` / `["message"]=` 均为
  **成功响应体**或 OpenAPI 文档示例。
- **维度 C-Req9**：前端 i18n 覆盖全部后端 Error_Code + 协议码（含 3004/3005），两 app 的
  `zh-CN.ts` 逐键 lockstep 完全一致，无占位符 / 无 Internal_Detail。
- **维度 C-Req6（Envelope 主路径）**：所有经 `ErrorResponder` 的 Envelope 均注入非空 request_id
  且复用 `X-Request-ID`（仅校验响应器例外，见 F4）。
- **维度 C-Req10（核心）**：拦截器 401 刷新失败跳转登录、视图无直读 `e.response.data.*` 展示、
  Admin 无 `alert()`。
- **维度 D**：3004/3005 + 条目级 httpStatus 覆盖机制已被 Property 4 覆盖；Property 4 放宽严格限定
  两个具名码，未放过真 bug；e2e mock 除已修 auth.spec 外无旧错误体残留；CI 门禁完整。
- **维度 E（catalog 一致性）**：代码实际使用码集合 == catalog 16 条登记码集合，无孤儿码、
  无未登记码 fallback；§5.1 / §5.2 文档表与 `allEntries()` / `allOAuthEntries()` 一致。
- **main.cc 全局异常处理器（Req 7.7）**：按路径正确分流 OAuth2 `server_error` vs Application
  Envelope，保留 CORS 头注入。

---

_生成时间：本 PR 深查阶段。建议优先处理 F1（影响面最大）与 F2/F3（同一验收点的实现+测试）。_

---

# 修复方案（Remediation Plan）

> 原则：每条修复都「保留迁移前的状态码语义（Req 11.4）+ 走单一权威来源 ErrorCatalog（Req 3.5）+
> 复用统一 RequestId（Req 6）」。改动尽量最小、与同文件既有约定一致，并配套测试。
> 建议分组提交，便于评审与回退。

## 分组与优先级

| 批次 | 包含 | 主题 | 风险 |
|---|---|---|---|
| 批次 1 | F7、F8、F6 | OAuth2 协议码状态码一致性（token/authorize）+ 文档速查表 | 中（含 Catalog 新增协议码） |
| 批次 2 | F9、F4 | RequestId 统一（DB/校验/HttpResponder 复用 resolve + 格式统一） | 中（改函数签名，需全量调用点核查） |
| 批次 3 | F2、F3 | introspect/revoke 传 authScheme + Req 2.9 端点级集成测试 | 低 |
| 批次 4 | F1 | 两个鉴权过滤器迁移到 Error Envelope | 中高（影响面最大，单独成批） |
| 批次 5 | F5 | Admin 四数据页失败展示本地化错误 | 低（纯前端 UX） |

---

## 批次 1：F7 + F8 + F6（协议码状态码一致性）

### F7 — 删除本地 `getHttpStatusCodeForError()`，状态码改由 Catalog 驱动
- 改 `OAuth2Plugin/src/controllers/OAuth2StandardController.cc`：
  1. 删除匿名命名空间内的 `getHttpStatusCodeForError()`（`:26-34`）。
  2. token 路径两处（`:1171`、`:1194`）把
     `getHttpStatusCodeForError(errorCode)` 改为
     `common::error::OAuth2ErrorHandler::getHttpStatusCode(errorCode)`
     （该函数查 `ErrorCatalog::findOAuth`，是单一权威来源）。
- 效果：`unauthorized_client` → 400（与 Catalog 一致），`invalid_client` → 401（不变）。
- 风险点：确认 plugin 返回的 result.error 取值都在协议码集合内；未登记码 `getHttpStatusCode` 回退 400（与原默认一致），安全。

### F8 — authorize 用户角色校验失败：改用 `access_denied` 且状态码经 Catalog
- 语义：此处是「用户缺少所需角色」，RFC 6749 §4.1.2.1 对应 `access_denied`，**非** `unauthorized_client`。
- **前置：Catalog 需新增协议码**（当前 `rawOAuthEntries` 无 `access_denied`）：
  在 `OAuth2Plugin/src/error/ErrorCatalog.cc` 的 `rawOAuthEntries()` 加
  `{"access_denied", 403, "授权请求被拒绝（用户无权或拒绝授权）", ""}`；
  并把 `access_denied` 加入 `ErrorCatalog.cc` 内 `validateInvariants()` 的 `kRequiredOAuthCodes`
  与 `ErrorCatalogPropertyTest.cc`、`ErrorCatalogRegressionTest.cc` 的 required 协议码数组（数量 12→13）。
- 改 `OAuth2StandardController.cc:955-962`：协议码由 `unauthorized_client` 改为 `access_denied`；
  状态码不再硬编码 403，改为经 `OAuth2ErrorHandler::getHttpStatusCode("access_denied")`（=403）。
  - 说明：保持当前 authorize 错误「直接 JSON 响应」的既有风格（同函数 `invalid_scope`、`invalid client_id`
    分支均为直接 JSON/文本，不重定向），以与上下文一致、避免引入开放重定向校验。若后续要严格遵循
    §4.1.2.1 的「重定向回 redirect_uri?error=access_denied」，另开任务并补开放重定向防护。
- **文档**：`access_denied` 加入 api-reference §5.2 OAuth2 协议错误码表（403），保持与 Catalog 一致
  （`ErrorCatalogDocTest` 会校验）。
- **i18n**：两个前端 `messages/zh-CN.ts` 各加 `access_denied: '授权请求被拒绝'`，并把它加入
  `messageCatalog.property.test.ts` / `crossAppConsistency.property.test.ts` 的协议码数组（保持覆盖与一致）。

### F6 — api-reference §5.3 速查表修正
- `docs/backend/api-reference.md` §5.3：把 `invalid_client` 从 `400` 行移到 `401` 行原因示例。
- 顺带把 `unauthorized_client`（400）与新增 `access_denied`（403）的速查行补齐（可选）。

### 批次 1 测试
- 新增/扩展协议端点状态码断言：`unauthorized_client→400`、`access_denied→403`、`invalid_client→401`
  全部取自 Catalog（可加入 `OAuth2ErrorPropertyTest.cc` 的协议码遍历，已覆盖则自动生效）。
- `ErrorCatalogPropertyTest`（Property 5）会自动校验新增 `access_denied` 条目的完整性。
- 端点脚本 `test-oauth2-endpoints.ps1` 视情况补一条 `unauthorized_client`/`access_denied` 状态码断言。

---

## 批次 2：F9 + F4（RequestId 统一）

### F9 — `handleDbException` / `handleValidationError` 复用 `RequestId::resolve(req)`，统一格式
- 改 `OAuth2Plugin/include/oauth2/error/ErrorHandler.h` + `src/error/ErrorHandler.cc`：
  - 给两个函数增加可选 `const drogon::HttpRequestPtr &req = nullptr` 末参；
    内部：`req ? RequestId::resolve(req) : RequestId::generate()`（不再用本地 `generateRequestId()`）。
  - `generateRequestId()`：内部改为 `return RequestId::generate();`（统一为 UUID 格式）或标记
    `[[deprecated]]`，消除 `req_` 短十六进制这一第二格式。
- 调用点核查（已 grep）：生产调用仅这两个函数自身；测试 `ErrorHandlerTest.cc:54` 直调
  `handleValidationError(field, reason)`（无 req）——默认参兼容，无需改测试（但断言里若校验 request_id
  格式需同步）。`.review_baseline/` 为评审基线副本，忽略。
- 风险：低（默认参保证源码兼容）。

### F4 — `validation/HttpResponder` 复用入站 X-Request-ID
- 改 `OAuth2Plugin/src/validation/HttpResponder.cc:50`：`buildErrorJson` 当前用 `RequestId::generate()`。
  给 `buildErrorJson` / `buildErrorResponse` / `respondWith*` 增加可选 `req` 透传，
  改用 `RequestId::resolve(req)`；无 req 时退化为 `generate()`（保持现有无-req 调用点兼容）。
- 调用点：`RequestValidationFilter.cc`、`OAuth2StandardController.cc`、`SessionController.cc`
  能拿到 req 的传入，拿不到的保持默认。

### 批次 2 测试
- 扩展 `RequestIdPropertyTest` 或新增用例：构造带合法 `X-Request-ID` 的请求，经
  `handleDbException`/`handleValidationError`/`HttpResponder` 路径，断言 Envelope 的 request_id == 头值；
  无头时断言为合法 UUID（`RequestId::isValid` 通过）。
- 断言系统内不再出现 `req_` 前缀格式。

---

## 批次 3：F2 + F3（WWW-Authenticate）

### F2 — introspect/revoke 在 `invalid_client` 时传 authScheme
- 改 `OAuth2StandardController.cc`：让 `extractClientCredentials`(`:309-341`) 顺带返回所用认证方案
  （检测到 `Authorization: Basic` 头时为 `"Basic"`，否则空）。
- introspect（`:357`、`:395`）、revoke（`:480`、`:517`）四处 `invalid_client` 调用，把该 scheme
  作为第 5 参传给 `OAuth2ErrorHandler::sendErrorResponse(...)`，使其设置 `WWW-Authenticate: Basic ...`、保持 401。

### F3 — 补 Req 2.9 端点级集成测试
- 新增 `OAuth2Server/test/integration/error/` 下用例：对 introspect/revoke 发送带错误
  `Authorization: Basic <bad>` 的请求，断言响应含 `WWW-Authenticate: Basic ...` 且 HTTP 401、
  body 为 RFC 6749 §5.2 形态。修 F2 后此测试转绿（可先写为预期失败再修 F2）。

---

## 批次 4：F1（过滤器迁移，单独成批）

- 改 `OAuth2Plugin/src/filters/AuthorizationFilter.cc` 与 `OAuth2AuthFilter.cc`：
  把所有错误出口（缺 token / 无效 token / 越权 / plugin 缺失 / 纯文本）改为经
  `common::error::ErrorResponder::buildResponse(req, error)` 产出 Error Envelope 后 `fcb(resp)`：
  - 缺/无效 token（401）→ `AUTH_TOKEN_INVALID`
  - 越权（403）→ `AUTHZ_INSUFFICIENT_PERMISSIONS`
  - plugin 缺失（500）→ `INTERNAL_ERROR`
- 注意：过滤器回调签名（`FilterCallback`）与控制器不同，用 `buildResponse(req, Error)` 直接构体，
  而非 `respond(req, cb, ...)`。
- 状态码须与基线一致：401→401、403→403、500→500（均无平压）。
- 测试：补过滤器级集成测试——未带 token / 越权访问受保护端点，断言响应是 Error Envelope
  （顶层单一 error 对象、Content-Type application/json、对应状态码），覆盖之前 D 维度指出的盲区。

---

## 批次 5：F5（Admin 数据页 UX）

- 给 `OAuth2Admin/src/pages/` 的 `dashboard/DashboardPage.vue`、`logs/LogsPage.vue`、
  `settings/SettingsPage.vue`、`tokens/TokensPage.vue` 的 catch 分支加 `normalizeError(e)` 展示
  （inline banner / 现有 toast 机制），替换当前的纯 `console.error` 静默。

---

## 验证清单（全部批次完成后）
- 后端：构建 `OAuth2Test_test` 并全量运行（Property 1-10、Catalog/Doc 一致性、Application 枚举、
  新增协议码 access_denied 相关断言、RequestId 复用断言、过滤器 Envelope 断言）。
- 前端：两 app `npm run test:unit`（i18n 覆盖含 access_denied、跨应用一致）。
- e2e：两 app Playwright 全量；端点脚本 `test-oauth2-endpoints.ps1` / `test-admin-endpoints.ps1`
  连真实 server 跑通（含 unauthorized_client→400、access_denied→403、WWW-Authenticate 断言）。
- 文档：`ErrorCatalogDocTest` 通过（§5.1/§5.2 与 Catalog 一致）。
