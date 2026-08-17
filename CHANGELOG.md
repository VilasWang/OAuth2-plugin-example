# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For the versioning policy (when to cut, what to bump, why), see
[Versioning & Release](docs/backend/versioning-and-release.md).

## [Unreleased]

## [1.2.0] - 2026-08-17

v1.1.0 以来的第二个正式发布，61 个 commit。主线：#43 资源-作用域授权
模型、用户管理 CRUD 补全（A2，PR #52）、OIDC Back-channel Logout 后端
（B1，PR #50）、OpenAPI spec 治理与破坏性变更门（A1/M0，PR #63）、
benchmark M2–M4 承重验证（PR #48）、#53–#60 用户管理安全加固批次
（PR #62）。

### ⚠️ Breaking (security hardening)

> 收紧了原本宽松（或错误）的行为。按版本策略（[§3 灰色地带取舍]
> (docs/backend/versioning-and-release.md#3-安全-hardening-的灰色地带显式取舍声明)）
> 在 MINOR 内推进，不升 MAJOR。依赖旧行为的下游需对照迁移。

- **`/api/admin/*` 现在要求 `admin` scope**（#43，F-010）：管理面路由由
  声明式 `(path,method)→scope` 注册表门禁。仅持有 RBAC admin 角色但
  access token 缺 `admin` scope 的调用返回 403（RFC 6750
  `insufficient_scope`）。迁移：管理面客户端在 token 请求中申请
  `admin` scope。
- **软删除契约全链路强制**（#54）：已软删除的用户不再能通过社交登录、
  MFA 登录补全、自服务等路径获得新 token 或会话。
- **`backchannel_logout_uri` 校验收紧**（#57）：强制 https；通知器
  crash-safe（传输失败记录错误而非中断登出）。
- **OpenAPI spec 死端点移除**（A1/M0）：`/api/orgs*`、
  `/oauth2/device/verify`、旧 `/oauth2/mfa/*` 路径从 spec 删除——这些
  端点在服务端早已不存在（调用一直 404），spec 只是回归真实。oasdiff
  破坏性变更门就位后，未来的 HTTP 面破坏性变更需升 MAJOR 或在
  `tools/openapi-governance/oasdiff-breaking-ignore.md` 显式豁免。

### Added

- **#43 资源-作用域授权模型**：声明式 `(path,method)→required_scopes`
  注册表（`ResourceScopeRegistry`，42 条 scope-gated 路由）+ scope 继承
  （`impliedBy`，如 `admin` 蕴含 `roles:read/write`）+ DB 驱动的 admin
  角色解析 + `/api/admin/scopes/resources` 发现端点。含 V023 迁移。
- **用户管理 CRUD 补全**（A2，PR #52）：`GET /api/admin/users` 分页
  （page/per_page）与过滤（q/role/locked）；`createUser`（username vs
  email UNIQUE 冲突区分 → 409；角色落库结果回报 roles_assigned /
  roles_failed）；`updateUser` 扩展（org_id 支持整数置值与 null 清空）；
  `deleteUser` 软删除（V024 `deleted_at` 迁移，删除时吊销存量 token 并
  回报 `tokens_revoked`；最后活跃 admin 保护 → 409）。
- **OIDC Back-channel Logout 后端**（B1，PR #50）：logout_token JWT
  构造器 + 通知器接入登出流程（`/oauth2/logout` 与 `end_session` 均触发
  通知，#55）；admin API 与 admin UI 表单配置
  `backchannel_logout_uri`；discovery 广告 `backchannel_logout_supported`；
  validator 单测 D1–D6 + admin/discovery 端点测试。
- **OpenAPI spec 治理 M0**（A1，PR #63）：三层端点对账（routes 82 =
  docs 80 = yaml 78，模两条例外清单）；P0 schema 按控制器实测契约补齐
  （token/introspect/revoke/login 表单与 JSON 请求体、RFC 6749 与应用
  双错误形态、discovery/JWKS 全字段）；`info.version` 与
  `cmake/Version.cmake` 联动（1.2.0）。CI 新增三层一致性门
  （`tools/openapi-governance/check_spec_governance.py`）+ oasdiff
  破坏性变更门（PR vs master，v1.29.1 pinned）。验收：生成的 Python
  客户端实调 token / introspect / discovery 全通过（客户端 SDK 工作
  解除阻塞）。
- **benchmark M2–M4**（PR #48）：S3–S6 场景（introspect / revoke /
  userinfo / discovery 无状态端点）；`config.bench.json` 集中配置；
  40 份结果 JSON + SUMMARY.md 承重裁决——内存 SDK 口径实测 **2.5 MB
  peak WS**（远优于 50–120 MB 声称），冷启动 ~4s 观测达成，P99 低并发
  1–4ms，discovery ~86k QPS @ 8 vCPU。
- 端点测试（59 OAuth2 + 52 Admin）集成进 ctest，纳入平台 CI 门禁。

### Fixed

- **#53–#60 用户管理加固批次**（PR #62）：admin 面输入校验、错误码
  语义与并发行为修复；#59 org_id 清空发送 JSON null 的前端修复。
- 社交登录默认角色授予未真正落库（role_id 未置）——PR #62 review 修复。
- `createUser`/`updateUser` 的 UNIQUE 冲突返回 409 而非 500（PR #52）。
- benchmark：容器解析改用 `docker ps`（compose ps 输出漂移）；PR #48
  review 修复（config 交换检测、observer 计时）；内存裁决口径修正
  （metric 用错而非未达标）。
- 测试基建：endpoint wrapper `pkill -f` 自杀 bug、ctest 管道继承挂起、
  服务器不可用时 exit 77 跳过、CI psql 兜底误抓任意 postgres 容器。

### Changed

- `IOAuthHttpClient` 家族从 `WITH_SOCIAL` 条件编译中解除（backchannel
  logout 等核心路径需要 HTTP 客户端，与社交登录编译开关解耦）。
- refactor-baseline 端点签名基线再生（78 行，收敛 master 存量漂移）。

### Documentation & CI

- 产品化演进文档同步（A2/B1/A1 交付状态、benchmark 裁决、issues
  #53–60 设计与实施计划）；openapi-update skill 四处镜像更新为治理门
  工作流；CI 增加 openapi-governance workflow。api-diff 基线按 SOP 对
  #43 内部引擎与 ORM 再生漂移 ratify。

## [1.1.0] - 2026-08-12

v1.0.0 以来的首次正式發布。涵蓋 842 個 commit，包含完整的 OIDC Core
支持、OAuth/OIDC 合規審計修復（31 項 finding 全部 remediated）、Redis 緩存
層、WebAuthn/Passkey、Admin 控制台，以及 SDK 庫層重構。

### ⚠️ Breaking (security hardening)

> 以下變更收緊了原本寬鬆（且多為 spec 違規）的行為。按版本策略
> ([§3](docs/backend/versioning-and-release.md#3-安全-hardening-的灰色地带显式取舍声明))
> 在 MINOR 內推進，不升 MAJOR。依賴舊寬鬆行為的下游需對照遷移。

- **PKCE 對 PUBLIC 客戶端強制**（RFC 9700 §2.1.1）：`require_pkce_for_public`
  預設改為 `true`（`config.json` / `config.dev.json` / `config.ci.json` /
  `config.prod.json` 顯式設定）。遷移：PUBLIC 客戶端必須發送 `code_challenge`
  / `code_verifier`。
- **redirect_uri 強制 https**（RFC 8252 §7.3）：豁免僅 `http://127.0.0.1` 與
  `http://[::1]` loopback IP 字面量（端口通配，`localhost` **不**豁免）。
  新增配置開關 `auth.allow_http_redirect_uri`（dev 開 / prod 關）。遷移：將
  seed/測試中的 `localhost` redirect_uri 改為 `127.0.0.1`。
- **refresh_token grant 強制客戶端認證**（RFC 6749 §3.2.1/§6）：CONFIDENTIAL
  客戶端缺失或錯誤 `client_secret` 返回 401 `invalid_client`（含
  `WWW-Authenticate: Basic`）。遷移：CONFIDENTIAL 客戶端 refresh 請求須帶
  客戶端憑證。
- **token_endpoint_auth_method 持久化並強制**：`oauth2_clients` 新增該列。
  token/introspect/revoke 按聲明方法強制（`client_secret_basic` 僅接 Basic
  頭 / `client_secret_post` 僅接 body / `none` 拒絕任何 secret）。NULL 保留
  舊寬容 Basic→body 回退。遷移：建議經管理端顯式賦值。
- **userinfo 要求 openid scope**：access token scope 不含 openid 返回 403 +
  `WWW-Authenticate: Bearer error="insufficient_scope"`；M2M token
  （subject `client:*`）直接拒。
- **最低路徑 required-scope 強制**：`/oauth2/userinfo`→`openid`、`/api/me`
  與 `/api/me/*`→`profile`、`/api/admin/*`→`admin` scope。
- **獨立 Redis 存儲模式正式棄用**：啟動時 LOG_ERROR 明示，該模式下
  refresh_token grant 返回 `unsupported_grant_type`。目標架構為 Postgres
  存儲 + Redis 緩存。見 `docs/backend/configuration-guide.md` §3。
- **內部限流**：`/oauth2/token`、`/oauth2/introspect`、`/oauth2/revoke` 與
  device_code 輪詢共享滑動窗口限流（默認 30 次失敗/60s，可經
  `custom_config["auth"]["rate_limit"]` 配置），達閾值 429 + `Retry-After`。
  僅計失敗，成功清零。
- **SDK 命名空間重構**：`oauth2::*` → `authforge::drogon::*`，`common::*` →
  `authforge::common::*`。扁平 `#include <oauth2/Foo.h>` 路徑不再有效，改用
  語義子目錄路徑（如 `<authforge/drogon/plugin/OAuth2Plugin.h>`）。SDK 消費
  者需更新 include 路徑與命名空間限定符。

### Added

#### OAuth2 / OIDC 協議

- **OpenID Connect Core**：`id_token` 簽發（HS256/RS256）、`/.well-known/
  openid-configuration` discovery、JWKS 端點、`userinfo`、`prompt` / `max_age`
  / `auth_time` / `acr` / `amr` 全量支持。
- **RP-Initiated Logout**（OIDC）：`/oauth2/end_session`（GET+POST），校驗
  `id_token_hint` 與 `post_logout_redirect_uri`，回顯 `state`。
- **Backchannel Logout**（OIDC）：`backchannel_logout` 支持。
- **Device Authorization Grant**（RFC 8628）：`/oauth2/device_authorization` 端
  點，按 client_type 分支認證；輪詢過快返回 `slow_down`（interval 遞增 5 秒並
  持久化）。
- **Dynamic Client Registration**（RFC 7591）：經 `/api/admin/clients/*`
  （admin-only）。
- **Client Credentials Grant**（M2M）：machine-to-machine token 簽發。
- **Token Introspection**（RFC 7662）：`/oauth2/introspect`。
- **Token Revocation**（RFC 7009）：`/oauth2/revoke`，含 refresh-token 撤銷。
- **Authorization Server Metadata**（RFC 8414）：
  `/.well-known/oauth-authorization-server`。

#### 身份與安全特性

- **密碼雜湊升級**：PBKDF2-SHA256（取代舊 SHA-256）。
- **Subject UUID**：`public_sub` 取代自增 ID 作為對外 subject。
- **Refresh Token 重用檢測**：family cascade 失效（被盜 token 重用 → 整族失效）。
- **授權碼原子消費** + token pair 事務性保存。
- **MFA / TOTP**（RFC 6238）與 **WebAuthn / Passkey** 支持。
- **賬戶鎖定**：漸進退避（progressive backoff）。
- **Email 驗證** + **密碼重置**流程。
- **結構化審計日誌**（`audit_logs` 表 + AuditService）。
- **多租戶基礎**（organizations）。
- **SMTP 郵件服務**（163 / Gmail / SendGrid 通用）。
- **社交登錄**：GitHub、Google。

#### 基礎設施

- **Redis L2 緩存**：client lookup 與 token 緩存的 write-through 裝飾器
  （Postgres 存儲 + Redis 緩存架構），含 admin-mutation 失效。
- **SchemaManager**：編號式自動 migration（`V0NN_*.sql`，22 個），單事務執行。
- **多平台 CI/CD**：Linux（Ubuntu 22.04）、Windows（MSVC 2022）、macOS
  （ARM64）。
- **Tag-driven release 流水線**：SDK tarball + 多架構鏡像（amd64/arm64）+
  cosign keyless 簽名 + SPDX SBOM + git-cliff release notes。
- **API 表面守衛**：`api-diff` 工具在 CI 強制 SDK 公共頭的 SemVer（breaking
  必須升 major）。
- **arch-guard**：Domain 層邊界檢查（禁止 include drogon 頭等）。
- **Helm chart** + 生產 Compose 加固 + 版本化 migration runner。
- **HTTP 性能基準設施**（benchmark facility）。

#### 前端

- **生產級 SPA**：AuthForge user frontend（Vue），含 PKCE、token lifecycle、
  consent 頁、email-first 登錄/註冊。
- **Admin 控制台**：clients/users/scopes/tokens/audit 全套管理頁，OIDC 簽名
  金鑰展示。
- **設計系統重構**：統一 UI 設計語言。

#### SDK 庫重構

- 分層庫結構：`libs/common`（Domain 共享內核）、`libs/oauth2`、`libs/identity`、
  `libs/storage-{postgres,redis,memory}`、`libs/drogon`（Adapter）。
- Domain 服務經端口（`ICryptoProvider` / `IUuidGenerator` / `IClock` /
  `ILogger` / `IAuditSink`）與 Drogon 解耦，支持脫離 Drogon 宿主消費。
- SDK 打包：`find_package(authforge-*)` 源碼集成 + 安裝消費 smoke 測試。

### Fixed

#### OAuth/OIDC 合規審計（31 項 finding 全部 remediated）

- **F-002**：`client_secret` 哈希寫入路徑統一為「有鹽小寫 SHA-256」，與校驗
  路徑一致（此前寫入用無鹽大寫 SHA-256，動態註冊/管理端創建的客戶端永遠無法
  認證）。
- **F-004**：Redis 後端 `client_secret` 比較改為常量時間比較；三後端統一
  `constantTimeMemcmp`；刪除洩漏比較結果的 LOG_DEBUG。
- **F-016**：issuer 一致性修復——access token 簽發時寫入配置的 issuer（此前
  從不寫入）；刪除三後端硬編碼的 `https://oauth.example.com`；introspect `iss`
  與 discovery `issuer` 字節一致（OIDC Discovery §3）。
- **F-007**：授權端點錯誤按 RFC 6749 §4.1.2.1 分流——client_id 未知 /
  redirect_uri 無效直接 4xx，其餘錯誤 302 重定向並回顯 state。
- **F-006**：資源端點 401 發 RFC 6750 §3 `WWW-Authenticate: Bearer ...
  error="invalid_token"` 挑戰。
- **F-008/F-009/F-013**：token 端點 validation gate 發 RFC 6749 §5.2
  `error: invalid_request` 信封；authorization_code 兌換時 redirect_uri 強制
  匹配；authorize 端校驗 `code_challenge_method ∈ {plain, S256}`。
- **F-019/F-020**：token/introspect/revoke 成功響應加 `Cache-Control: no-store`
  （RFC 6749 §5.1 / RFC 7009 §2.2.1）；authorize 終態重定向的 `state`/`code`
  經 urlEncode。
- **完整審計報告**：見
  `docs/productization-evolution/done/oauth-oidc-compliance-audit.md`。

#### 其他修復

- **Linux teardown 崩潰**：`OAuth2CleanupService` 析構訪問已銷毀事件循環 →
  加 `stopped_` 標誌防止重複清理，乾淨退出無需 `std::_Exit(0)`。
- **refresh-token revoke 空操作**（C3）：revoke 路徑未對 token 哈希即比對，
  導致 RFC 7009 §2.1 撤銷無效——修復為先哈希再比對。
- **SchemaManager 單事務**：full migration pass 在單一事務內執行（#46）。
- **keepalive logout revoke**（C5）：keep-alive 會話登出時正確觸發 token 撤銷。
- **OpenAPI 文檔**：security field 形狀、PKCE/MFA/revoke 參數註冊等多項修正。
- **MSVC `/WX`**：解決 `sharedCb` 變量遮蔽（C4458）導致的 Windows CI 失敗。

### Changed

- **Drogon**：v1.9.10 → v1.9.13。`drogon_ctl` 產生的 ORM 驗證碼改用
  `std::wstring_convert<std::codecvt_utf8_utf16<...>>`（由 `orm_compat.h` 處理
  C++20 棄用）。
- **依賴管理**：引入 Conan（`conanfile.py` + `conan.lock`）作為 C++ 依賴管理。
- **限流**：遷移至 Drogon Hodor 插件（token bucket），移除 Redis 依賴。
- **token endpoint 錯誤響應**：統一為 RFC 6749 §5.2 OAuth2 錯誤信封（此前用
  應用內部信封）。
- **DB schema**：新增 14 個 migration（V009–V022），含 refresh_token_family、
  mfa_support、account_lockout、device_codes、backchannel_logout、
  token_partitioning_prep、multi_tenant、webauthn 等。

### Security

- OAuth/OIDC 合規審計 31 項 finding 全部 remediated（3 個 batch）。
- 安全 hardening：SQL injection / XSS / command injection 防護、CORS / CSP /
  HSTS 頭、token 撤銷、賬戶鎖定。
- 三存儲後端 `client_secret` 常量時間比較。

---

## [1.0.0] - 2026-01-29

首個正式發布。OAuth2.0 Authorization Code Grant、access/refresh token、客戶端
管理、用戶認證、Drogon 插件架構、Vue 前端、PostgreSQL/Redis 持久化、RBAC、
審計日誌、WeChat 登錄。

### Added

- OAuth2.0 Authorization Code Grant flow；access token 與 refresh token 支持。
- 客戶端註冊與管理；用戶認證（用戶名/密碼）。
- Drogon 插件架構；controller-based HTTP 端點；filter 中間件；JSON 配置。
- PostgreSQL 持久化（ORM 遷移）與 Redis 持久化；同步寫入；SHA-256 雜湊。
- 原子消費操作；client secret hash 校驗；PostgreSQL 事務支持。
- 用戶賬戶系統；ORM 遷移；UUID salt 支持。
- Vue.js SPA 前端；OAuth2 登錄流程；受保護 API 訪問；用戶資料展示。
- WeChat 開放平台 API 與 QR code 登錄。
- RBAC 權限系統基礎；Prometheus metrics；結構化審計日誌。
- 單元測試、整合測試（Redis/PostgreSQL）、E2E 整合測試、直接 controller 測試。

### Security

- 基礎認證（用戶名/密碼）；client secret 雜湊（SHA-256）；CORS 配置。
- SQL injection 防護；輸入校驗與淨化。

---

[Unreleased]: https://github.com/lucaswang420/authforge/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/lucaswang420/authforge/releases/tag/v1.1.0
[1.0.0]: https://github.com/lucaswang420/authforge/releases/tag/v1.0.0
