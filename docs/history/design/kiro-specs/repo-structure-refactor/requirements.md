# Requirements Document

## Introduction

> 引言 / Introduction

本需求文档由 `design.md`（设计先行 / Design-First 工作流）反向派生而来，用于把仓库结构重构（spec 名 `repo-structure-refactor`）的技术决策固化为可机械验证的验收标准。改造目标仓库为 `authforge`（Drogon C++ OAuth2 Plugin + Server + Admin Vue3 控制台 + Frontend Vue3 用户站点），重构覆盖：公共 include 镜像化、验证层 4 类整合、Filter / Middleware 命名统一、Admin / OAuth2 controller 合并与拆分、可观测性子层抽取、CMake 兼容层抽取、部署文件 (`Dockerfile` / `docker-compose*.yml` / `prometheus.yml`) 进入 `deploy/`、Windows / Linux / macOS 脚本对等、文档 kebab-case 重组、`.gitignore` 整合，以及 forwarding shim 的延迟移除（详见 design §1.1 / §1.2 / §2.8）。

为什么做：当前公共头扁平、命名重叠（`Validator` / `ValidationHelper` / `ValidatorHelper` / `ValidationFilter` 四件套语义模糊；`AdminController` 与 `AdminApiController` 同存；`OAuth2Controller`(Server) 与 `OAuth2StandardController`(Plugin) 边界不清）、部署文件散落根目录、脚本仅 Windows 一等公民、文档碎片严重，导致新贡献者上手成本高且 CI / 文档极易产生悬挂引用（design §2.1 痛点）。重构目标态严格遵循依赖单向、`include/oauth2/**` 镜像 `src/**`、`deploy/` 集中、跨平台脚本对等（design §2.2 / §2.3 / §2.6）。

范围（In-scope）：仅做结构性 / 命名 / 部署 / 文档 / 脚本 / 构建基础设施层面的搬迁与命名整合；按 12 个独立可绿阶段 P0–P12 推进，每阶段 ctest + e2e + docker compose 三项独立通过（design §2.8 / §12.6）。非目标（Non-Goals，design §1.2）：不修改 ORM 自动生成的 14 个模型类（`Oauth2*` / `Users` / `Roles` / `Permissions` / `Organizations` 等）；不引入新的 RFC / 管理 API / UI 页面；不切换主要依赖版本；不重构 SQL migration 历史；不调整数据库 schema；不引入 monorepo 工具链。

零行为回归底线（design §1.1.5 / §6.4.4 / §6.5.3）：所有 RFC 6749 / 6750 / 7009 / 7517 / 7591 / 7662 / 8414 / 8628 + OIDC Discovery 端点的 path / status code / Content-Type / JSON 响应体保持完全等价；Linux / macOS / Windows × Debug / Release 6 格 ctest 矩阵保留；OAuth2Admin / OAuth2Frontend Playwright e2e 全过；纯本地与纯 Docker 两套环境的烟测全部通过。本需求文档中 R1（ORM 豁免）与 R15（HTTP API 行为零回归）为最高优先级的一等公民约束。

## Glossary

> 术语表 / Glossary

- **ORM-exempt**：design §0 列出的 14 个 Drogon ORM 自动生成模型类（`Oauth2AccessTokens` / `Oauth2Clients` / `Oauth2Codes` / `Oauth2RefreshTokens` / `Oauth2Scopes` / `Oauth2ClientScopes` / `Oauth2SubjectMappings` / `Oauth2UserConsents` / `Organizations` / `Permissions` / `Roles` / `RolePermissions` / `UserRoles` / `Users`）及其头文件、`model.json` / `model-postgresql.json` / `.clang-format` 配置文件，禁止重命名 / 改命名空间 / 拆并。
- **Plugin**：CMake target `OAuth2Plugin`（OBJECT 库），位于 `OAuth2Plugin/`，对外公共表面为 `OAuth2Plugin/include/oauth2/**`。
- **Server**：CMake target `OAuth2Server`（可执行），位于 `OAuth2Server/`，链接 `OAuth2Plugin` OBJECT 库。
- **Admin**：Vue3 管理控制台子项目 `OAuth2Admin/`，含 Playwright e2e。
- **Frontend**：Vue3 用户前台子项目 `OAuth2Frontend/`，含 Playwright e2e。
- **Forwarding_Shim**：在 P1 / P2 / P3 / P4 / P6 落地新路径或新名称后，于旧 include 路径 / 旧符号位置保留的过渡 `#include` 头或 `using` typedef，仅用于过渡期保护下游编译；P11 一次性删除（design §2.8 / §6.1.6 / §6.2.6 / §6.6.4）。
- **Phase_Gate**：design §12.6 规定的每个阶段（P0–P12）必跑测试与通过条件的集合，构成阶段验收门。
- **Manage_Entrypoint**：根目录单一命令面 `manage.ps1`（Windows）+ `manage.sh`（Linux/macOS），暴露 design §6.7.3 列出的命令子集，二者命令集必须完全一致。
- **Validation_Pipeline**：design §6.1 重组后的四类协作链：`oauth2::validation::Rules`（数据）+ `oauth2::validation::RuleEngine`（单字段静态校验）+ `oauth2::validation::RuleSet`（HttpRequest 抽取与场景化组合）+ `oauth2::validation::HttpResponder`（错误转 HTTP 响应）+ `oauth2::filters::RequestValidationFilter`（Drogon 路由前自动校验过滤器）。
- **OAuth2AuthFilter**：design §6.2 中 `OAuth2Middleware` 改名后的目标类，命名空间 `oauth2::filters`，职责为 Bearer access token 校验。
- **AuthorizationFilter_Merged**：design §6.2 中归位到命名空间 `oauth2::filters` 的 RBAC 过滤器，类名保留为 `AuthorizationFilter`。
- **AdminController_Merged**：design §6.3 中由 `AdminController`（dashboard 单端点）+ `AdminApiController`（30 个端点）合并并整体保留 `AdminController` 名称的最终类，共 31 个端点。
- **SessionController**：design §6.4 中由 `OAuth2Server/controllers/OAuth2Controller` 改名拆分后的类，承载 `/login`(GET)、`/oauth2/login`(POST)、`/oauth2/consent`(POST)、`/oauth2/logout`(POST) 四条路由。
- **HealthController**：design §6.4 中新设的探针 controller，承载 `/health`、`/health/live`、`/health/ready` 三条 GET 路由。
- **Deploy_Directory**：仓库根下 `deploy/` 子目录树，含 `deploy/docker/`、`deploy/observability/`、`deploy/env/`、`deploy/nginx/`、`deploy/keys/`（design §2.6）。
- **Doc_Link_Check**：脚本 `scripts/check-doc-links.sh`，遍历仓库内所有 markdown 链接、对相对文件路径做存在性校验，CI 在 P9 之后必跑（design §10.4 / §12.6）。
- **ORM_Regeneration_Test**：在 P11 之前每个阶段执行 `./manage.sh generate-models` 后，`git diff` 仅含时间戳级别变化的不变性校验（design §0.3 / §12.7）。
- **Manage_Parity_Check**：脚本 `tools/manage-parity-check.sh`，机械比对 `manage.ps1` 与 `manage.sh` 命令集对等（design §6.7.4 / §8.3）。
- **Smoke_Parity**：跨平台烟测脚本对 `scripts/smoke-parity.sh` 与 `scripts/smoke-parity.ps1`，覆盖 build → test → run → docker-up → 健康检查 → docker-down 全链路（design §8.4）。
- **Reviewer_Agent**：sub-agent 复审者（首选 `code-reviewer`，备选 `compliance-checker`），按 design §13.3 checklist A–G 给出首行 `APPROVED` 或 `REJECTED`（design §13.2 / §13.4）。
- **Allowlist_Linux_Only**：design §8.1 显式标注的 "Linux only by design" 脚本（`rebuild-debug-image.sh` / `install-hooks.sh` / `validate-openapi.sh`），不强制 `.ps1` 等价物，且不通过 `manage` 入口暴露。
- **Baseline_Tag**：P0 阶段在 git 上推送的 `refactor-baseline` tag，冻结重构前的 ctest / e2e 输出与 SHA-256 快照（design §2.8 / §12.6）。

## Requirements

### Requirement 1: ORM 自动生成代码豁免 / ORM-Generated Files Exemption

**User Story:** As a 维护者, I want ORM 自动生成的 14 个模型类与其头文件、配置文件全程保留原命名 / 路径 / 命名空间, so that `drogon_ctl` 重新生成时只产生时间戳级别 diff 且不破坏生成器幂等性。

#### Acceptance Criteria

1. THE Refactor SHALL preserve the class names of all 14 ORM-exempt classes listed in design §0.1 (`Oauth2AccessTokens`, `Oauth2Clients`, `Oauth2Codes`, `Oauth2RefreshTokens`, `Oauth2Scopes`, `Oauth2ClientScopes`, `Oauth2SubjectMappings`, `Oauth2UserConsents`, `Organizations`, `Permissions`, `Roles`, `RolePermissions`, `UserRoles`, `Users`) byte-for-byte unchanged.
2. THE Refactor SHALL keep all 14 ORM-exempt headers under `OAuth2Plugin/include/oauth2/models/` with filenames identical to design §0.1.
3. THE Refactor SHALL keep all 14 ORM-exempt sources under `OAuth2Plugin/src/models/` with filenames identical to design §0.1.
4. THE Refactor SHALL preserve the `drogon_model::*` namespace of every ORM-exempt class without modification.
5. THE Refactor SHALL keep `OAuth2Plugin/src/models/model.json`, `OAuth2Plugin/src/models/model-postgresql.json`, and `OAuth2Plugin/src/models/.clang-format` unchanged in path and content.
6. WHEN `scripts/backend/generate_models.bat` is executed against the refactored repository, THE ORM_Regeneration_Test SHALL produce a `git diff` containing only timestamp-level differences as defined in design §0.3.
7. WHEN `scripts/backend/generate-models.sh` (added in P8) is executed against the refactored repository, THE ORM_Regeneration_Test SHALL produce a `git diff` containing only timestamp-level differences as defined in design §0.3.
8. IF any ORM-exempt class signature, namespace, or header path is changed by a phase commit, THEN THE Phase_Gate for that phase SHALL fail per design §0.3.
9. THE Refactor SHALL NOT split, merge, or move any ORM-exempt model class out of `models/`.
10. WHERE a phase commit touches the `OAuth2Plugin/src/models/` or `OAuth2Plugin/include/oauth2/models/` directories, THE CI SHALL run `tools/check-orm-exempt.sh` (design Property 1) before merge.

### Requirement 2: 公共 include 严格镜像 src / Public Include Mirrors src

**User Story:** As a Plugin 下游消费者, I want `OAuth2Plugin/include/oauth2/**` 严格镜像 `OAuth2Plugin/src/**` 的子目录布局, so that 任何头文件查找都能直接对应到源目录、不再有扁平 `.h` 散落根 include 目录。

#### Acceptance Criteria

1. WHEN P11 is complete, THE `OAuth2Plugin/include/oauth2/` directory SHALL contain exactly the subdirectories `config/`, `controllers/`, `error/`, `filters/`, `models/`, `observability/`, `plugin/`, `services/`, `storage/`, `types/`, `utils/`, `validation/` per design §5.
2. WHEN P11 is complete, THE `OAuth2Plugin/include/oauth2/` directory SHALL contain zero flat `.h` files at its root level per design §5.
3. THE Refactor SHALL place every header listed in design §4.1.3 / §4.1.4 / §4.1.5 / §4.1.6 / §4.1.7 / §4.1.8 / §4.1.9 / §4.1.10 / §4.1.11 under the subdirectory specified by that table.
4. THE Refactor SHALL use the exact final include layout drawn in design §5 for the validation, observability, plugin, types, and storage subtrees.
5. WHILE Phases P1–P10 are in progress, THE Refactor SHALL keep Forwarding_Shim headers at the legacy flat paths so that downstream `#include <oauth2/Foo.h>` continues to compile.
6. WHEN P11 is complete, THE Refactor SHALL delete all Forwarding_Shim headers added in P1–P10.
7. WHEN `tools/check-include-mirror.sh` (design Property 7) is executed after P11, THE check SHALL exit with code 0.
8. THE Refactor SHALL NOT introduce any header in `OAuth2Plugin/include/oauth2/` whose corresponding source under `OAuth2Plugin/src/` is in a different subdirectory.
9. WHERE a header is part of the storage layer, THE Refactor SHALL place it under `OAuth2Plugin/include/oauth2/storage/` exposing concrete classes (`CachedOAuth2Storage`, `MemoryOAuth2Storage`, `PostgresOAuth2Storage`, `RedisOAuth2Storage`) per design §4.1.8.

### Requirement 3: 验证层命名整合 / Validation Layer Consolidation

**User Story:** As a 后端开发者, I want validation 层的四类一明确各自职责（数据 / 静态校验 / HTTP 抽取 / HTTP 响应）并落到 `oauth2::validation` 命名空间下, so that 阅读校验代码时不再需要在 `Validator` / `ValidatorHelper` / `ValidationHelper` / `ValidationFilter` 四个含义模糊的类之间来回切换。

#### Acceptance Criteria

1. WHEN P3 is complete, THE Plugin SHALL expose `oauth2::validation::RuleEngine` defined in `OAuth2Plugin/include/oauth2/validation/RuleEngine.h` per design §6.1.3.
2. WHEN P3 is complete, THE Plugin SHALL expose `oauth2::validation::RuleSet` defined in `OAuth2Plugin/include/oauth2/validation/RuleSet.h` per design §6.1.3.
3. WHEN P3 is complete, THE Plugin SHALL expose `oauth2::validation::HttpResponder` defined in `OAuth2Plugin/include/oauth2/validation/HttpResponder.h` per design §6.1.3.
4. WHEN P3 is complete, THE Plugin SHALL expose `oauth2::validation::Rule`, `oauth2::validation::Result`, and `oauth2::validation::RuleType` defined in `OAuth2Plugin/include/oauth2/validation/Rules.h` per design §6.1.3.
5. WHEN P3 is complete, THE Plugin SHALL expose `oauth2::filters::RequestValidationFilter` (renamed from `::ValidationFilter`) defined in `OAuth2Plugin/include/oauth2/filters/RequestValidationFilter.h` per design §6.1.3.
6. THE Refactor SHALL apply the rename mapping listed in design §6.1.5 in full (`Validator` → `RuleEngine`, `ValidatorHelper` → `RuleSet`, `ValidationHelper` → `HttpResponder`, `ValidationRuleConfig` → `Rule`, `ValidationRuleType` → `RuleType`, `ValidationResult` → `Result`, `ValidationFilter` → `RequestValidationFilter`, plus the 9 listed method renames).
7. WHEN P3 is complete, THE `oauth2::validation::RuleType` enum SHALL use PascalCase enumerators (`NotEmpty`, `LengthLimit`, `Regex`, `NumericRange`, `UrlFormat`, `EmailFormat`) per design §6.1.3.
8. WHILE Phases P3–P10 are in progress, THE Plugin SHALL provide a Forwarding_Shim header `OAuth2Plugin/include/oauth2/validation/legacy.h` with `using` aliases per design §6.1.6 (`common::validation::Validator` = `RuleEngine`, etc.).
9. THE `oauth2::validation::RuleEngine` class SHALL NOT take any Drogon HTTP type as a parameter so that it remains usable from non-HTTP callers per design §6.1.4.
10. WHEN P11 is complete, THE Refactor SHALL delete `OAuth2Plugin/include/oauth2/validation/legacy.h` and remove all references to the `common::validation::*` namespace.
11. WHEN P11 is complete, `grep -RIn "common::validation::"` over `OAuth2Plugin/` and `OAuth2Server/` SHALL return zero matches per design §6.1.7.
12. WHEN `test-oauth2-endpoints.{ps1,sh}` is executed against the refactored repository, validation-failure responses SHALL preserve the same JSON field names and shape as before per design §6.1.7.

### Requirement 4: Filter / Middleware 边界统一 / Filter–Middleware Boundary

**User Story:** As a 后端开发者, I want `OAuth2Middleware` / `AuthorizationFilter` / `ValidationFilter` 三个 Drogon `HttpFilter` 子类统一后缀为 `*Filter` 并归到 `oauth2::filters` 命名空间, so that 同一管道里的拦截器不再概念分裂。

#### Acceptance Criteria

1. WHEN P4 is complete, THE Plugin SHALL define class `oauth2::filters::OAuth2AuthFilter` in `OAuth2Plugin/include/oauth2/filters/OAuth2AuthFilter.h` (renamed from `OAuth2Middleware`) per design §6.2.3.
2. WHEN P4 is complete, THE Plugin SHALL place class `AuthorizationFilter` under namespace `oauth2::filters` in `OAuth2Plugin/include/oauth2/filters/AuthorizationFilter.h` per design §6.2.3.
3. WHEN P4 is complete, THE Plugin SHALL define class `oauth2::filters::RequestValidationFilter` in `OAuth2Plugin/include/oauth2/filters/RequestValidationFilter.h` per design §6.2.3.
4. WHEN P4 is complete, every `ADD_METHOD_TO(...)` filter-name string in any controller header SHALL reference the new fully-qualified names `"oauth2::filters::OAuth2AuthFilter"`, `"oauth2::filters::AuthorizationFilter"`, or `"oauth2::filters::RequestValidationFilter"` per design §6.2.5.
5. THE Refactor SHALL update the filter-name string in every `ADD_METHOD_TO` callsite atomically inside the same P4 commit per design §6.2.6 (Drogon does not support filter-name aliasing).
6. WHILE Phases P4–P10 are in progress, THE Plugin SHALL provide deprecated `using` typedefs for `OAuth2Middleware`, top-level `AuthorizationFilter`, and top-level `ValidationFilter` per design §6.2.6.
7. WHEN P11 is complete, THE Refactor SHALL remove the deprecated typedefs added in P4 per design §6.2.7.
8. WHEN P11 is complete, `grep -RIn "OAuth2Middleware"` over `OAuth2Plugin/` and `OAuth2Server/` SHALL return zero matches outside the deleted shim per design §6.2.7.
9. WHEN `test-oauth2-endpoints.{ps1,sh}` is executed after P4, THE Bearer authentication, RBAC denial, and parameter validation response behaviors SHALL remain unchanged per design §6.2.7.
10. WHEN Playwright admin login flow is executed after P4, THE flow SHALL pass per design §6.2.7.

### Requirement 5: AdminController 合并 / AdminController Merge

**User Story:** As a 后端开发者, I want `AdminController`（dashboard 单端点）和 `AdminApiController`（30 个端点）合并为单一 `AdminController` 共 31 个端点, so that controller 列表不再因纯路由分组而拥有冗余拆分。

#### Acceptance Criteria

1. WHEN P5 is complete, THE Server SHALL expose a single `AdminController` class in `OAuth2Server/controllers/AdminController.{h,cc}` containing exactly 31 endpoints per design §6.3.3 / §6.3.4.
2. WHEN P5 is complete, THE Server SHALL no longer contain the file `OAuth2Server/controllers/AdminApiController.h` or `AdminApiController.cc` per design §6.3.6.
3. WHEN P5 is complete, THE merged `AdminController` SHALL retain the route `GET /api/admin/dashboard` (from legacy `AdminController::dashboard`) per design §6.3.3.
4. WHEN P5 is complete, THE merged `AdminController` SHALL retain the route `GET /api/admin/dashboard/stats` (from legacy `AdminApiController::getDashboardStats`) per design §6.3.3.
5. WHEN P5 is complete, every HTTP path under `/api/admin/**` SHALL respond with the same status code, Content-Type, and JSON shape as the baseline captured in P0 per design §6.3.8.
6. THE Refactor SHALL replace every reference to symbol `AdminApiController` with `AdminController` across `OAuth2Server/`, `OAuth2Admin/`, and `OAuth2Frontend/` per design §6.3.6.
7. WHEN P5 is complete, `grep -RIn "AdminApiController"` over `OAuth2Server/`, `OAuth2Admin/`, and `OAuth2Frontend/` SHALL return zero matches per design §6.3.8.
8. THE Refactor SHALL NOT introduce a Forwarding_Shim alias for `AdminApiController` per design §6.3.7 (boundary-internal merge exemption).
9. WHEN `scripts/backend/test-admin-endpoints.{ps1,sh}` is executed after P5, THE script SHALL exit with code 0 per design §6.3.8.
10. WHEN OAuth2Admin Playwright e2e is executed after P5, THE login → dashboard load → `/api/admin/clients` GET → `/api/admin/users` PUT-disable scenarios SHALL all pass per design §6.3.8.

### Requirement 6: OAuth2Controller 职责拆分 / OAuth2Controller Split

**User Story:** As a 后端开发者, I want `OAuth2Server/controllers/OAuth2Controller` 拆分为 `SessionController` + `HealthController` + 把 `/api/register` 并入 `UserSelfServiceController`, so that 类名忠实反映职责，避免读者把会话 / 注册 / 健康误读为 RFC 标准 OAuth2 端点。

#### Acceptance Criteria

1. WHEN P5 is complete, THE Server SHALL expose `SessionController` in `OAuth2Server/controllers/SessionController.{h,cc}` per design §6.4.5.
2. WHEN P5 is complete, THE `SessionController` SHALL serve exactly the routes `GET /login`, `POST /oauth2/login`, `POST /oauth2/consent`, and `POST /oauth2/logout` per design §6.4.4.
3. WHEN P5 is complete, THE Server SHALL expose a new `HealthController` in `OAuth2Server/controllers/HealthController.{h,cc}` serving `GET /health`, `GET /health/live`, `GET /health/ready` per design §6.4.5.
4. WHEN P5 is complete, THE Server SHALL extend `UserSelfServiceController` with the route `POST /api/register` (relocated from legacy `OAuth2Controller::registerUser`) per design §6.4.4 / §6.4.7.
5. WHEN P5 is complete, THE Server SHALL no longer contain the file `OAuth2Server/controllers/OAuth2Controller.h` or `OAuth2Controller.cc` per design §6.4.7.
6. WHEN P5 is complete, `grep -RIn "class OAuth2Controller"` over `OAuth2Server/` SHALL return zero matches per design §6.4.9.
7. WHEN P5 is complete, `grep -RIn "OAuth2Controller::"` over `OAuth2Server/` SHALL return zero matches per design §6.4.9.
8. THE Refactor SHALL NOT introduce a `using OAuth2Controller = SessionController` alias per design §6.4.8.
9. WHEN P5 is complete, THE views path `OAuth2Server/views/{login.csp,consent.csp}` SHALL remain at its existing location with `drogon_create_views` paths unchanged per design §4.2.
10. WHEN P5 is complete, every HTTP path served by the new controllers SHALL respond with the same status code, Content-Type, and response body shape as the baseline captured in P0 per design §6.4.9.
11. WHEN OAuth2Admin Playwright e2e login + consent flow is executed after P5, THE flow SHALL pass per design §6.4.9.
12. THE `/oauth2/{authorize,token,userinfo,introspect,revoke}` and `/.well-known/*` routes SHALL continue to be served by Plugin `OAuth2StandardController` and SHALL NOT migrate to any Server controller per design §6.4.4.

### Requirement 7: Plugin 与 Server controller 边界 / Plugin–Server Controller Boundary

**User Story:** As a 系统架构师, I want Plugin 仅承载 RFC / OIDC 标准协议端点 + 元数据发布，Server 承载会话 / 注册 / 健康 / 社交登录 / 管理后台 / DCR / Device Authorization, so that Plugin 不会被 UI / 审核状态机依赖污染。

#### Acceptance Criteria

1. THE Plugin SHALL contain exactly one `drogon::HttpController` subclass `oauth2::controllers::OAuth2StandardController` per design §6.5.2.
2. THE `OAuth2StandardController` SHALL serve the union of routes `/oauth2/{authorize,token,userinfo,introspect,revoke}` and `/.well-known/{oauth-authorization-server,openid-configuration,jwks.json}` per design §6.5.2.
3. WHERE a controller depends on UI rendering or human-review state machines, THE Refactor SHALL keep that controller in `OAuth2Server/controllers/` per design §6.5.5.
4. THE Refactor SHALL keep `ClientRegistrationController` (RFC 7591 DCR) in `OAuth2Server/controllers/` per design §6.5.4.
5. THE Refactor SHALL keep `DeviceAuthController` (RFC 8628) in `OAuth2Server/controllers/` per design §6.5.4.
6. WHEN refactor is complete, `grep -lR "drogon::HttpController" OAuth2Plugin/include OAuth2Plugin/src` SHALL only match `OAuth2StandardController.h` and `OAuth2StandardController.cc` per design §6.5.6.
7. THE Refactor SHALL NOT place any Server controller (including `SessionController`, `HealthController`, `AdminController`, social-login controllers) under `OAuth2Plugin/` per design §6.5.6.

### Requirement 8: Observability 子层抽取 / Observability Sublayer

**User Story:** As a 后端开发者, I want `AuditLogger` / `OAuth2Metrics` / `OpenApiGenerator` 从 `src/common/` 抽到 `src/observability/`（`OpenApiGenerator` 进 `observability/openapi/`）并把命名空间收敛到 `oauth2::observability(::openapi)`, so that 横切关注点不再共用 `common/` 垃圾桶目录。

#### Acceptance Criteria

1. WHEN P2 is complete, THE Plugin SHALL place `AuditLogger.cc` under `OAuth2Plugin/src/observability/` and its header under `OAuth2Plugin/include/oauth2/observability/AuditLogger.h` per design §4.1.11.
2. WHEN P2 is complete, THE Plugin SHALL place `OAuth2Metrics.cc` under `OAuth2Plugin/src/observability/` and its header under `OAuth2Plugin/include/oauth2/observability/OAuth2Metrics.h` per design §4.1.11.
3. WHEN P2 is complete, THE Plugin SHALL place `OpenApiGenerator.cc` under `OAuth2Plugin/src/observability/openapi/` and its header under `OAuth2Plugin/include/oauth2/observability/openapi/OpenApiGenerator.h` per design §4.1.11.
4. WHEN P2 is complete, THE class `OpenApiGenerator` SHALL be declared inside namespace `oauth2::observability::openapi` per design §6.6.3.
5. WHILE Phases P2–P10 are in progress, THE Plugin SHALL provide Forwarding_Shim headers `<oauth2/AuditLogger.h>`, `<oauth2/OAuth2Metrics.h>`, and `<oauth2/OpenApiGenerator.h>` that re-include the new paths per design §6.6.4.
6. WHEN P11 is complete, `grep -RIn "include <oauth2/AuditLogger.h>"`, `"include <oauth2/OAuth2Metrics.h>"`, and `"include <oauth2/OpenApiGenerator.h>"` SHALL return zero matches per design §6.6.5.
7. WHEN P10 is complete, THE directories `OAuth2Plugin/src/common/`, `OAuth2Plugin/src/common/documentation/`, and `OAuth2Plugin/src/common/types/` SHALL no longer exist per design §4.1.12.
8. THE `/metrics` HTTP endpoint behavior SHALL remain unchanged after the relocation per design §14.1.

### Requirement 9: CMake 构建基础设施统一 / CMake Build Infrastructure

**User Story:** As a 构建工程师, I want 抽取 `cmake/Compatibility.cmake` 与 `cmake/Version.cmake`，统一项目名为 `oauth2-plugin-example`，并保持 OBJECT 库决策与 `drogon_create_views` 路径, so that 跨编译器（MSVC `/FI` 与 GCC/Clang `-include`）逻辑只维护一份。

#### Acceptance Criteria

1. WHEN P6 is complete, THE Repo SHALL contain the file `cmake/Compatibility.cmake` defining function `oauth2_apply_compat(target)` per design §7.3.
2. WHEN P6 is complete, THE Repo SHALL contain the file `cmake/Version.cmake` defining `OAUTH2_PROJECT_VERSION` per design §7.2.
3. WHEN P6 is complete, THE root `CMakeLists.txt` SHALL invoke `project(oauth2-plugin-example VERSION ${OAUTH2_PROJECT_VERSION} LANGUAGES CXX ...)` per design §7.1.
4. WHEN P6 is complete, both `OAuth2Plugin/CMakeLists.txt` and `OAuth2Server/CMakeLists.txt` SHALL call `oauth2_apply_compat(${PROJECT_NAME})` and SHALL NOT contain duplicate `if(MSVC) ... else() ... endif()` `/FI` blocks per design §7.4 / §7.5.
5. WHEN P6 is complete, THE `OAuth2Plugin` CMake target SHALL remain an `OBJECT` library per design §7.7.
6. WHEN P6 is complete, THE `OAuth2Plugin/CMakeLists.txt` SHALL NOT contain `target_include_directories(... PRIVATE)` entries pointing at `src/storage`, `src/models`, `src/services`, `src/common`, or `include/oauth2/models` per design §7.4.
7. WHEN P6 is complete, THE `drogon_create_views` invocation in `OAuth2Server/CMakeLists.txt` SHALL still reference `${CMAKE_CURRENT_SOURCE_DIR}/views` per design §7.5.
8. WHEN P6 is complete, `cmake -S . -B build && cmake --build build --config Debug` SHALL succeed on Linux, macOS, and Windows per design §7.8.
9. WHEN P6 is complete, `cmake --build build --config Release` SHALL succeed on Linux, macOS, and Windows per design §7.8.
10. WHEN `cmake_minimum_required` is invoked in the root `CMakeLists.txt`, THE required version SHALL be at least `3.16` per design §7.1.

### Requirement 10: 部署文件集中化 / Deployment File Centralization

**User Story:** As a 运维工程师, I want `Dockerfile` / `docker-compose*.yml` / `prometheus.yml` / `.env.docker.example` / `docker-quick-verify-debug.sh` 全部迁入 `deploy/`, so that 仓库根目录不再被部署文件污染，且 build context 仍可使用仓库根。

#### Acceptance Criteria

1. WHEN P7 is complete, THE Repo SHALL contain `deploy/docker/Dockerfile` (relocated from `./Dockerfile`) per design §4.3.
2. WHEN P7 is complete, THE Repo SHALL contain `deploy/docker/docker-compose.yml`, `deploy/docker/docker-compose.debug.yml`, and `deploy/docker/docker-compose.prod.yml` (relocated from repo root) per design §4.3.
3. WHEN P7 is complete, THE Repo SHALL contain `deploy/observability/prometheus.yml` (relocated from `./prometheus.yml`) per design §4.3.
4. WHEN P7 is complete, THE Repo SHALL contain `deploy/env/docker.env.example` (relocated from `./.env.docker.example`) and `deploy/env/server.env.example` (relocated from `OAuth2Server/.env.example`) per design §4.2 / §4.3.
5. WHEN P7 is complete, THE Repo SHALL contain `deploy/docker/docker-quick-verify-debug.sh` (relocated from repo root) per design §4.3.
6. WHEN P7 is complete, every `services.*.build` block in `deploy/docker/docker-compose*.yml` SHALL set `context: ../..` so that the Docker build context resolves to the repo root per design §2.6.1.
7. WHEN P7 is complete, every Prometheus-related volume mount in `deploy/docker/docker-compose*.yml` SHALL reference `../observability/prometheus.yml:/etc/prometheus/prometheus.yml` per design §2.6.3.
8. THE Repo SHALL keep a copy of `.dockerignore` at the repository root per design §2.6.4 (Docker requires it at the build context root).
9. WHEN P7 is complete, THE Repo SHALL contain `deploy/docker/.dockerignore` whose content is byte-identical to the root `.dockerignore` per design §4.3.
10. WHEN P7 is complete, THE Repo SHALL contain `tools/check-dockerignore-sync.sh` and THE CI SHALL run it after P7 per design §2.6.4.
11. WHEN `manage.{ps1,sh} docker-up` is invoked, THE Manage_Entrypoint SHALL execute `docker compose -f deploy/docker/docker-compose.yml --project-directory . up -d` per design §2.6.2.
12. WHEN `docker compose -f deploy/docker/docker-compose.yml up -d` is run, all services SHALL become healthy within 90 seconds per design Property 4.

### Requirement 11: 跨平台脚本对等 / Cross-Platform Script Parity

**User Story:** As a Linux/macOS 用户, I want 所有正常开发流程下当前仅有 Windows `.bat` / `.ps1` 的脚本都新增 `.sh` 等价物，并通过统一 `manage.sh` 入口暴露, so that 我可以在没有 PowerShell 的环境里完成 build / test / docker / 重置密码 / 端点回归等全部任务。

#### Acceptance Criteria

1. WHEN P8 is complete, THE Repo SHALL contain `scripts/backend/test.sh`, `run-server.sh`, `setup-database.sh`, `generate-models.sh`, `full-test.sh`, `full-test-docker.sh`, `docker-postgres-start.sh`, `docker-postgres-stop.sh` per design §8.1 / §4.5.
2. WHEN P8 is complete, THE Repo SHALL contain `scripts/backend/test-admin-endpoints.sh`, `test-oauth2-endpoints.sh`, `common-test-functions.sh`, `reset-admin-password.sh`, `reset-account-lockout.sh` per design §8.1.
3. WHEN P8 is complete, THE Repo SHALL contain `manage.sh` at the repository root per design §4.5.
4. WHEN P8 is complete, THE command set exposed by `manage.sh` SHALL equal the command set exposed by `manage.ps1` per design §6.7.4 / Property 5.
5. THE Manage_Entrypoint command set SHALL contain exactly the commands listed in design §6.7.3 (`build-backend`, `test-backend`, `build-frontend`, `dev-frontend`, `build-admin`, `dev-admin`, `run-backend`, `setup-db`, `generate-models`, `reset-password`, `reset-lockout`, `test-admin-endpoints`, `test-oauth2-endpoints`, `e2e-admin`, `e2e-frontend`, `full-test`, `docker-up`, `docker-down`, `clean`, `help`).
6. WHEN `manage.ps1 help` and `manage.sh help` are executed, THE printed command list SHALL be identical except for shell-prompt formatting per design §6.7.5.
7. WHEN P8 is complete, THE Repo SHALL contain `tools/manage-parity-check.sh` and THE CI SHALL run it after P8 per design §6.7.4 / §8.3.
8. WHEN `tools/manage-parity-check.sh` is executed against the refactored repository, THE script SHALL exit with code 0 per design §8.5.
9. WHEN P8 is complete, THE Repo SHALL contain `scripts/smoke-parity.sh` and `scripts/smoke-parity.ps1` per design §4.5 / §8.4.
10. WHEN `scripts/smoke-parity.sh` is executed on a Linux runner, THE script SHALL exit with code 0 per design §8.5.
11. WHEN `scripts/smoke-parity.ps1` is executed on a Windows runner, THE script SHALL exit with code 0 per design §8.5.
12. WHERE a script is in the Allowlist_Linux_Only set (`rebuild-debug-image.sh`, `install-hooks.sh`, `validate-openapi.sh`), THE Refactor SHALL NOT require a `.ps1` equivalent and `tools/manage-parity-check.sh` SHALL NOT report it as missing per design §8.1.
13. THE Refactor SHALL NOT rename existing `.bat` files to kebab-case per design §6.7.2.
14. WHEN P8 is complete, every newly added `.sh` file SHALL be marked executable in git (mode `0755`) per design §8.5.

### Requirement 12: 文档全量同步与无悬挂引用 / Documentation Sync and Zero Dangling Links

**User Story:** As a 文档维护者, I want 所有引用被搬迁路径或被重命名符号的文档（README / docs / PRD / `.agent` / `.claude` / `CLAUDE.md` / `CHANGELOG`）一次性同步, so that 重构后没有任何相对链接或符号引用是悬挂的。

#### Acceptance Criteria

1. WHEN P9 is complete, THE `docs/` directory SHALL contain the subdirectories `backend/`, `admin/`, `frontend/`, `ops/`, `design/` per design §10.1.
2. WHEN P9 is complete, THE Refactor SHALL apply every filename rename listed in design §10.3.3 (e.g., `docs/ACCOUNT_LOCKOUT.md` → `docs/ops/account-lockout.md`, `docs/backend/api_reference.md` → `docs/backend/api-reference.md`) per design §10.3.3.
3. WHEN P9 is complete, THE Refactor SHALL merge `docs/superpowers/` and `PRD/superpowers/` into `docs/design/superpowers/` with duplicates removed per design §10.1.
4. THE Refactor SHALL keep all PRD product-requirement filenames (`admin_console_design.md`, `frontend_design.md`, `production_hardening_*.md`, `PROGRESS.md`, etc.) unchanged per design §10.1.
5. WHEN P9 is complete, THE Repo SHALL contain `tools/refactor-doc-paths.py` that operates only on non-fenced markdown segments per design §10.3.1.
6. WHEN `tools/refactor-doc-paths.py --selftest` is executed, THE script SHALL pass a fixture covering the `\|` table-pipe escape per design §10.3.2.
7. WHEN P9 is complete, every regex replacement listed in design §10.3.2 SHALL be applied to every documentation file in the repository.
8. WHEN P9 is complete, THE Repo SHALL contain `scripts/check-doc-links.sh` (Doc_Link_Check) that validates every relative filesystem link in markdown files per design Property 6 / §12.6.
9. WHEN `scripts/check-doc-links.sh` is executed against the refactored repository, THE script SHALL exit with code 0 per design Property 6.
10. THE manual-review list in design §10.5 (`docs/backend/architecture-overview.md`, `docs/backend/plugin-integration.md`, `docs/backend/testing-guide.md`, `README.md`, `CLAUDE.md`, the seven `.agent/workflows/*.md` files, the two `PRD/superpowers/...` ASCII-tree files) SHALL be reviewer-approved before P9 merges per design §10.5.
11. WHEN P9 is complete, every reference inside `.agent/workflows/*.md` and `.claude/skills/*/SKILL.md` to relocated paths or renamed symbols SHALL be updated per design §10.2.
12. WHEN P9 is complete, THE `OAuth2Server/openapi.yaml` `description` fields SHALL reference current controller class names (e.g., `AdminController`, `SessionController`) per design §10.2.
13. WHEN P11 is complete, THE `CHANGELOG.md` SHALL contain an `[Unreleased]` section describing the refactor per design §14.3.

### Requirement 13: .gitignore 整合与工作树清理 / `.gitignore` Consolidation

**User Story:** As a 仓库维护者, I want `.gitignore` 由根级 catch-all 持有跨项目模式，子目录仅持有本目录特有规则, so that 已被 ignored 但仍跟踪的产物（例如 `models_backup/` / `logs/` / `uploads/`）被一次性清理。

#### Acceptance Criteria

1. WHEN P10 is complete, THE root `.gitignore` SHALL contain the cross-project patterns listed in design §11.2 (`**/node_modules/`, `**/dist/`, `**/.venv/`, `**/test-results/`, `**/__pycache__/`, `**/*.pyc`, `build/`, `build-*/`, `OAuth2Plugin/models_backup/`, `**/logs/`, `**/uploads/`, secret patterns, etc.).
2. WHEN P10 is complete, THE `OAuth2Server/.gitignore` SHALL contain only local-runtime override entries per design §11.2.
3. WHEN P10 is complete, THE `OAuth2Admin/.gitignore` and `OAuth2Frontend/.gitignore` SHALL contain only local environment override entries per design §11.2.
4. WHEN P10 is complete, `git ls-files OAuth2Plugin/models_backup/` SHALL return an empty list per design §11.4.
5. WHEN P10 is complete, `git ls-files OAuth2Server/logs/ OAuth2Server/uploads/` SHALL return an empty list per design §11.3 / §11.4.
6. WHEN P10 is complete, `git check-ignore -v build/ OAuth2Plugin/models_backup/ OAuth2Server/logs/` SHALL match the root `.gitignore` per design §11.4.
7. WHEN P10 is complete, `git status` against a clean checkout SHALL be empty per design §11.4.
8. THE Refactor SHALL preserve `deploy/keys/.gitkeep` so that the deployment keys directory continues to exist in checkouts per design §2.6.

### Requirement 14: 阶段独立可绿与回滚 / Per-Phase Atomic Greenness and Rollback

**User Story:** As a 发布工程师, I want 12 个阶段（P0–P12）每个都独立可绿、独立可 `git revert`, so that 任何阶段出现问题都能不牵连其它阶段地回滚。

#### Acceptance Criteria

1. WHEN P0 is complete, THE Repo SHALL have a git tag `refactor-baseline` recording the pre-refactor SHA, baseline ctest output, and baseline e2e output under `tools/refactor-baseline/` per design §2.8 / §12.6.
2. WHEN any single phase Pn (n in 1..11) commit is reverted via `git revert`, THE remaining phases that were not reverted SHALL still satisfy their own Phase_Gate criteria per design §2.9.
3. WHILE Phases P1–P10 are in progress, THE Refactor SHALL keep every Forwarding_Shim required by design §2.8 (header re-includes, `using` typedefs) until P11 per design §2.8.
4. WHEN P11 is complete, THE Refactor SHALL delete every Forwarding_Shim listed in design §2.8 in a single phase per design §2.8.
5. WHEN any phase Pn ends, THE following three checks SHALL all pass per design Property 8 / §12.6: (a) ctest matrix on 6 cells, (b) the phase-specific acceptance gate from design §12.6, (c) the project still builds.
6. THE Refactor SHALL NOT bundle changes belonging to two distinct phases (per design §2.8 phase scope) into a single commit.
7. THE Refactor SHALL document the rollback recipe for each phase in the risk register table at design §2.9.

### Requirement 15: HTTP API 行为零回归 / Zero HTTP API Regression

**User Story:** As an API consumer, I want 重构前后的每一个 HTTP 端点（Plugin RFC 端点 + Server 应用端点）的 status code、Content-Type、JSON 响应体形状完全一致, so that 前端 / Admin / 第三方集成 / Playwright e2e 不受重构影响。

#### Acceptance Criteria

1. THE Refactor SHALL preserve, for every endpoint in the baseline endpoint list captured at P0 per design Property 2, the tuple (HTTP path, accepted HTTP method set, accepted request content-type set, response status code, response header set, response body schema-level shape) such that the post-refactor tuple is identical to the P0 baseline tuple element-by-element.
2. WHEN any endpoint in the baseline list is invoked after a phase commit with a request whose method, path, headers, query parameters, and body bytes are identical to a baseline test fixture, THE response status code SHALL equal the baseline status code recorded for that fixture per design Property 2.
3. WHEN any endpoint in the baseline list is invoked after a phase commit with a baseline test fixture, THE response headers `Content-Type`, `Cache-Control`, `Vary`, `WWW-Authenticate`, `Location`, and `Set-Cookie` SHALL each be present if and only if present in the baseline response, AND each present header value SHALL equal the baseline value, with the sole exception of timestamp-derived or nonce-derived sub-fields documented in the P0 baseline snapshot per design Property 2.
4. WHEN any endpoint in the baseline list returns a body with `Content-Type: application/json` for any 2xx, 4xx, or 5xx response, THE response body schema-level shape (set of field names, nesting structure, JSON value types per field) SHALL equal the baseline shape recorded for that status class per design Property 2.
5. THE Plugin SHALL continue to serve `/oauth2/authorize`, `/oauth2/token`, `/oauth2/userinfo`, `/oauth2/introspect`, `/oauth2/revoke`, `/.well-known/oauth-authorization-server`, `/.well-known/openid-configuration`, `/.well-known/jwks.json` per design §6.4.4 / §6.5.2, with the accepted HTTP method set and accepted parameter source set (query string, `application/x-www-form-urlencoded` body, `application/json` body) for each endpoint identical to the P0 baseline.
6. THE Server SHALL continue to serve `/login`, `/oauth2/login`, `/oauth2/consent`, `/oauth2/logout`, `/health`, `/health/live`, `/health/ready`, `/api/register`, every `/api/admin/**` route, social-login routes, MFA / WebAuthn / Email / Password / Organization / DCR / Device-Authorization routes per design §6.5.3, with the accepted HTTP method set for each endpoint identical to the P0 baseline.
7. WHEN `scripts/backend/test-admin-endpoints.{ps1,sh}` is executed against any phase end-state, THE script SHALL exit with code 0 within 600 seconds per design §12.2.
8. WHEN `scripts/backend/test-oauth2-endpoints.{ps1,sh}` is executed against any phase end-state, THE script SHALL exit with code 0 within 600 seconds per design §12.2.
9. WHEN `tools/diff-endpoint-baseline.py` is executed against any phase end-state, THE script SHALL exit with code 0 AND report zero schema-level diff against the P0 baseline snapshot per design Property 2.
10. WHEN `tools/api-test-coverage-check.py` is executed at phase P8 or any later phase, THE set of endpoint path-and-method pairs covered by `test-*-endpoints.ps1` SHALL equal the set covered by `test-*-endpoints.sh`, AND the script SHALL exit with code 0 per design §12.2.
11. THE Refactor SHALL NOT change any HTTP path string, path parameter name, or path parameter ordering for any endpoint in the baseline list.
12. THE Refactor SHALL NOT change the JSON field names, field nesting, or field value types emitted by `oauth2::validation::HttpResponder::buildErrorResponse` relative to the legacy `ValidationHelper::createValidationErrorResponse` baseline output captured at P0 per design §6.1.7.
13. WHEN any endpoint in the baseline list returns an OAuth2 error response under RFC 6749 §5.2, THE response body SHALL contain the field `error` with a value drawn from the identical baseline-recorded set of RFC 6749 §5.2 / RFC 8628 §3.5 / RFC 7009 §2.2.1 error codes, AND the optional fields `error_description`, `error_uri`, and `state` SHALL be present if and only if present in the baseline response for the same input fixture.
14. IF any baseline response for an endpoint in the baseline list includes a `Set-Cookie` header, THEN the post-refactor response for the same input fixture SHALL emit the same cookie name with the identical attribute set (`Path`, `Domain`, `Max-Age` or `Expires` window, `HttpOnly`, `Secure`, `SameSite`), with the cookie value treated as opaque-equal-by-format per the P0 baseline snapshot.
15. WHEN `/.well-known/jwks.json` is requested after a phase commit, THE response SHALL contain a JWKS whose set of `kid` values is a superset of the P0 baseline `kid` set, AND every JWT issued by `/oauth2/token` for a baseline grant fixture SHALL contain the same set of claim names with the same JSON value types as the baseline, with `exp` and `iat` formatted as numeric seconds-since-epoch identical to the baseline format.

### Requirement 16: 双环境功能正常（本地 + Docker）/ Dual-Environment Functionality

**User Story:** As a 开发者, I want 在纯本地环境（直接 `manage.{sh,ps1} run-backend`）和纯 Docker 环境（`docker compose up`）下，关键端点全部 200, so that 重构不会让其中任一环境破裂。

#### Acceptance Criteria

1. WHEN `manage.{ps1,sh} build-backend` followed by `manage.{ps1,sh} setup-db` followed by `manage.{ps1,sh} run-backend` is executed against the refactored repository, THE backend SHALL bind to its configured port within 30 seconds per design §12.4.
2. WHEN the local backend is running per AC 16.1, `curl -fsS http://localhost:5555/health/live` SHALL return HTTP 200 per design §12.4.
3. WHEN the local backend is running per AC 16.1, `curl -fsS http://localhost:5555/health/ready` SHALL return HTTP 200 per design §12.4.
4. WHEN the local backend is running per AC 16.1, `curl -fsS http://localhost:5555/.well-known/openid-configuration` SHALL return HTTP 200 per design §12.4.
5. WHEN the local backend is running per AC 16.1, `curl -fsS http://localhost:5555/.well-known/jwks.json` SHALL return HTTP 200 per design §12.4.
6. WHEN the local backend is running per AC 16.1, `curl -fsS http://localhost:5555/metrics` SHALL return HTTP 200 per design §12.4.
7. WHEN the local backend is running per AC 16.1, an `/oauth2/token` password-grant request with valid credentials SHALL return HTTP 200 with a JSON body containing `access_token` per design §12.4.
8. WHEN `manage.{ps1,sh} docker-up` is executed against the refactored repository, all services in `deploy/docker/docker-compose.yml` SHALL reach `healthy` status within 90 seconds per design Property 4.
9. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:5555/health/ready` SHALL return HTTP 200 per design §12.5.
10. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:5555/.well-known/openid-configuration` SHALL return HTTP 200 per design §12.5.
11. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:5555/metrics` SHALL return HTTP 200 per design §12.5.
12. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:9090/-/healthy` (Prometheus) SHALL return HTTP 200 per design §12.5.
13. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:8080/` (OAuth2Frontend) SHALL return HTTP 200 per design §12.5.
14. WHEN the docker stack is healthy per AC 16.8, `curl -fsS http://localhost:8081/` (OAuth2Admin) SHALL return HTTP 200 per design §12.5.
15. WHEN `manage.{ps1,sh} docker-up -debug` is executed against the refactored repository, all services in `deploy/docker/docker-compose.debug.yml` SHALL reach `healthy` status within 90 seconds AND `curl -fsS http://localhost:5555/health/ready` SHALL return HTTP 200 per design §6.7.3 / Property 4.
16. WHEN `manage.{ps1,sh} docker-up -prod` is executed against the refactored repository, all services in `deploy/docker/docker-compose.prod.yml` SHALL reach `healthy` status within 90 seconds AND `curl -fsS http://localhost:5555/health/ready` SHALL return HTTP 200 per design §6.7.3 / Property 4.

### Requirement 17: 多 config CTest 矩阵保留 / Multi-Config CTest Matrix Preservation

**User Story:** As a CI 工程师, I want Linux × Debug、Linux × Release、macOS × Debug、macOS × Release、Windows × Debug、Windows × Release 共 6 格 ctest 矩阵在重构期间全程保留并保持单调（测试集合不缩减）, so that 现有 CI 不会因重构静默丢弃测试用例。

#### Acceptance Criteria

1. WHEN any phase ends, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build -C Debug --output-on-failure` SHALL exit with code 0 on Linux per design §12.1.
2. WHEN any phase ends, the same Debug ctest pipeline SHALL exit with code 0 on macOS per design §12.1.
3. WHEN any phase ends, `cmake -S . -B build && cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure` SHALL exit with code 0 on Windows per design §12.1.
4. WHEN any phase ends, the same matrix executed with configuration `Release` SHALL exit with code 0 on Linux, macOS, and Windows per design §12.1.
5. THE set of ctest test names produced after any phase SHALL be a superset of the baseline ctest test names captured at P0 per design Property 3.
6. WHEN `tools/check-ctest-coverage.sh` is executed against any phase end-state, THE script SHALL exit with code 0 per design Property 3.
7. THE Refactor SHALL NOT modify the structure of `.github/workflows/*.yml` other than the precise adjustments enumerated in design §9 (compose path, parity-check step, doc-link-check step).
8. THE Refactor SHALL keep the ctest invocation surface (`ctest -C <Cfg> --output-on-failure`) unchanged in CI per design §9.

### Requirement 18: 前后端 Playwright e2e 通过 / Playwright E2E Coverage

**User Story:** As a 前端测试工程师, I want 重构后 OAuth2Admin 与 OAuth2Frontend 的 Playwright e2e 全部通过, so that 控制台与用户前台对后端结构变化无感。

#### Acceptance Criteria

1. WHEN P5 onward, `(cd OAuth2Admin && npm ci && npx playwright install --with-deps && npx playwright test)` SHALL exit with code 0 per design §12.3.
2. WHEN P5 onward, `(cd OAuth2Frontend && npm ci && npx playwright install --with-deps && npx playwright test)` SHALL exit with code 0 per design §12.3.
3. WHEN `manage.{ps1,sh} e2e-admin` is invoked, THE Manage_Entrypoint SHALL run the OAuth2Admin Playwright suite per design §6.7.3 / §12.3.
4. WHEN `manage.{ps1,sh} e2e-frontend` is invoked, THE Manage_Entrypoint SHALL run the OAuth2Frontend Playwright suite per design §6.7.3 / §12.3.
5. THE Refactor SHALL NOT modify `OAuth2Admin/playwright.config.ts` or `OAuth2Frontend/playwright.config.ts` per design §4.6.
6. WHEN P9 is complete, THE OAuth2Admin Playwright tests SHALL load the relocated documentation paths only via `docs/admin/e2e-testing-guide.md` references per design §4.6.

### Requirement 19: 双重 sub-agent 复审 / Dual Sub-Agent Review

**User Story:** As a spec 维护者, I want `design.md` 与后续 `tasks.md` 各自通过 reviewer agent 一次明确 `APPROVED`, so that 设计与实施计划在进入实施前都获得机器可校验的复审签字。

#### Acceptance Criteria

1. WHEN the design phase is closed, THE Reviewer_Agent SHALL emit a response whose first line is exactly `APPROVED` for `design.md` per design §13.4.
2. WHEN the tasks phase is closed, THE Reviewer_Agent SHALL emit a response whose first line is exactly `APPROVED` for `tasks.md` per design §13.4 / §13.5.
3. THE Reviewer_Agent prompt SHALL include the absolute path of the document under review and the full §13.3 checklist A–G per design §13.2.
4. WHEN the Reviewer_Agent emits `REJECTED`, THE spec author SHALL incorporate the failing checklist items and resubmit per design §13.4.
5. THE `tasks.md` review checklist SHALL include at minimum the four items listed in design §13.5 (each task references a design section, each PBT/test task is executable, the task order matches §2.8, every phase ends with a §12.6 acceptance-gate task).
6. THE Reviewer_Agent SHALL be invoked using `code-reviewer` first and `compliance-checker` as backup per design §13.2.

### Requirement 20: 命令面单一入口 manage.{ps1,sh} / Single Command-Surface Entrypoint

**User Story:** As a 多平台开发者, I want 仓库根的 `manage.ps1` 和 `manage.sh` 提供同一组命令子集且 `help` 输出一致, so that 我无需记两套脚本调用面。

#### Acceptance Criteria

1. WHEN P8 is complete, THE Repo SHALL contain `manage.ps1` and `manage.sh` at the repository root per design §6.7.3.
2. THE command set parsed from `manage.ps1` `switch ($Action)` SHALL equal the set parsed from `manage.sh` `case "$action" in ... esac` per design §6.7.4.
3. THE Manage_Entrypoint SHALL expose the 20-command list specified in design §6.7.3 (see Requirement 11 AC 11.5) and SHALL NOT expose any command outside that list.
4. WHEN any command in the Manage_Entrypoint command set is invoked with no arguments, THE entrypoint SHALL forward to the underlying script listed in design §6.7.3 with platform-correct path separators.
5. WHEN `manage.{ps1,sh} help` is invoked, THE output SHALL list every command in the Manage_Entrypoint command set per design §6.7.5.
6. WHEN `manage.{ps1,sh} docker-up` is invoked with no flag, THE entrypoint SHALL execute `docker compose -f deploy/docker/docker-compose.yml --project-directory . up -d` per design §6.7.3 / §2.6.2.
7. WHEN `manage.{ps1,sh} docker-up -debug` is invoked, THE entrypoint SHALL use `deploy/docker/docker-compose.debug.yml` per design §6.7.3.
8. WHEN `manage.{ps1,sh} docker-up -prod` is invoked, THE entrypoint SHALL use `deploy/docker/docker-compose.prod.yml` per design §6.7.3.
9. THE Manage_Entrypoint SHALL NOT expose `rebuild-debug-image`, `install-hooks`, or `validate-openapi` per design §8.1 (allowlist exemption).
10. WHEN `tools/manage-parity-check.sh` is executed against the refactored repository, THE script SHALL exit with code 0 per design §6.7.5 / §8.5.
