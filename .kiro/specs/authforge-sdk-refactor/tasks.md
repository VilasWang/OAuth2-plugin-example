# Implementation Plan: AuthForge 产品化 + SDK 化重构

## Overview

配套设计文档：`design.md`。本计划将现有全栈 OAuth2/OIDC IdP 重构为「可生产交付的产品」+「可独立复用的 SDK 组」双形态，按里程碑 M0–M8 推进。

已纳入两轮独立评审的修订：
- 第一轮 F1–F10：whole-archive 链接（F1）、ORM 归 storage-postgres（F2）、去 drogon::utils 端口化单列（F3）、consent 去内部键（F4）、契约分档（F5）、测试库化 views/白盒（F6）、M2 拆分与可度量验收（F7）、OpenSSL 现状更正（F8）、SDK 运行时契约（F9）、secrets/legacy 迁移（F10）。
- 第二轮 H1–H7/L1-L2：**config 插件按名反射迁移（H1）**、Task 36-38 时序修正 + `paths.env`（H2）、**缓存装饰器 CachedOAuth2Storage 再架构（UAF 已在 HEAD 修复，迁移时保留安全模式，A1）**、**controller 去单例化（H4）**、视图注册符号 whole-archive（H5）、分里程碑可发布性（H6）、PromExporter 去留（H7）、OpenApiGenerator 归属（L1）、前端错误契约共享源（L2）。

原则：每个里程碑结束 CI 必须全绿，可独立评审合并；小步提交；现有测试作回归安全网。图例：任务前的 `[ ]` 表示待办；每个任务标注「产出」与「验收」。

## Task Dependency Graph

```json
{
  "waves": [
    { "wave": 1, "milestone": "M0", "tasks": [1, 2, 3, 4, 5, 6], "dependsOn": [] },
    { "wave": 2, "milestone": "M1", "tasks": [7, 8, 9, 10, 11, 12], "dependsOn": [1, 2] },
    { "wave": 3, "milestone": "M2a", "tasks": [13, 14, 15, 16], "dependsOn": [7, 8] },
    { "wave": 4, "milestone": "M2b", "tasks": [17, 18], "dependsOn": [13, 14, 15, 16] },
    { "wave": 5, "milestone": "M2.5", "tasks": [19], "dependsOn": [17, 18] },
    { "wave": 6, "milestone": "M3", "tasks": [20, 21, 22, 23, 24, 25, 26], "dependsOn": [19] },
    { "wave": 7, "milestone": "M4+M5", "tasks": [27, 28, 29, 30, 31], "dependsOn": [20, 21, 22, 23, 24, 25, 26] },
    { "wave": 8, "milestone": "M6", "tasks": [32, 33, 34, 35], "dependsOn": [27, 28] },
    { "wave": 9, "milestone": "M7", "tasks": [36, 37, 38], "dependsOn": [32] },
    { "wave": 10, "milestone": "M8", "tasks": [39, 40, 41, 42, 43, 44, 45], "dependsOn": [36, 37, 38] }
  ]
}
```

文字概览：`M0 → M1 → M2a → M2b → M2.5 → M3 → { M4, M5 } → M6 → M7 → M8`

- M0 可复现构建地基（无前置；Task 1/2 为 gate；Task 5 建 paths.env）
- M1 拆存储接口 + 缓存装饰器再架构（依赖 M0）
- M2a 抽 common + 端口 + 去 drogon::utils + 测试链接过渡（最高风险）
- M2b 抽 oauth2 Domain + ORM 归 storage-postgres
- M2.5 抽 identity
- M3 Drogon 适配器 + 插件注册迁移 + whole-archive + 去单例化 + 产品瘦身
- M4 测试库化 + SDK 冒烟
- M5 控制器拆分 + 身份内聚（可与 M4 并行）
- M6 CI/CD + 护栏
- M7 发布管线
- M8 目录/命名空间重组 + 脚本/docker/agent 路径对齐

## Tasks

---

## M0 — 可复现构建地基（依赖：无）

目标：三平台统一 Conan，升级 OpenSSL，构建预设入库，建立集中路径定义。Task 1、2 是阻塞后续里程碑的 gate。

- [x] 1. 编写 `conanfile.py` 取代 `conanfile.txt`【gate：阻塞 M1+】
  - 依赖：`drogon/1.9.13`（options：with_orm/with_postgres/with_redis/with_ctl/with_sqlite）、`openssl/3.5.x`、`jsoncpp/1.9.5`、`hiredis`、`libcurl`（with_ssl=openssl）、`gtest`、`brotli`、`zlib`
  - 增加 options：`with_identity`、`with_social`、`with_webauthn`（条件编译与 F9 依赖声明）
  - 产出：`conanfile.py` + `conan.lock`
  - 验收：三平台 `conan install` 成功解析并生成 toolchain；**未通过则不进入 M1**

- [x] 2. 实测 Conan drogon 包的 `drogon_ctl` 可用性【gate：决定 ORM 生成流程，阻塞 Task 9/18】
  - 产出：结论文档 + 若不可用的 `drogon_ctl` 单独安装脚本（三平台）
  - 验收：ORM 生成流程在三平台可复现跑通；结论写入 design §15

- [x] 3. OpenSSL 3.5 迁移：改写 `JwkManager::getPublicKeyComponents()`
  - `EVP_PKEY_get1_RSA + RSA_get0_key + BN_*` → `EVP_PKEY_get_bn_param(OSSL_PKEY_PARAM_RSA_N/E)`
  - 备注：Linux CI 现已在 OpenSSL 3.0.2 构建（F8），本任务只消除弃用告警、对齐 3.5
  - 产出：迁移后的 `JwkManager.cc`
  - 验收：JWKS 输出 `n`/`e` 字节与迁移前逐字节一致（golden）；`-DOPENSSL_NO_DEPRECATED`/`-Werror` 下无弃用告警

- [x] 4. 顶层 `CMakePresets.json` 入库
  - presets：`linux-release` / `windows-msvc` / `macos-arm64` / `*-asan` / `*-tsan`
  - 产出：`CMakePresets.json`
  - 验收：`cmake --preset` 三平台可配置

- [x] 5. 建立 `paths.env` 集中路径定义（评审 H2，新增）
  - 把 scripts/CI/config 里散落的源目录、构建产物路径、SQL/config 路径抽成**单一真实来源**（`paths.env` + CMake 变量），后续里程碑的目录移动只改此一处
  - `manage.*` 与 `scripts/backend/*` 改为读取 `paths.env`（不再硬编码）
  - 产出：`paths.env` + 改造后引用它的脚本
  - 验收：现有 build/test/run 全流程经 `paths.env` 跑通（路径值暂为旧值，仅完成「去硬编码」）

- [x] 6. 改造三平台 CI 使用统一 Conan（取消 Linux 源码编译 Drogon），恢复缓存
  - 产出：更新后的 `ci-*.yml`（临时保持现结构，M6 再重构）；`conan.lock` + 内容哈希缓存键
  - 验收：三平台 CI 全绿，构建时间下降（Windows 侧 `conan install` 步骤已本机复现验证；Linux/macOS 仅静态审阅，需在真实 CI 上确认）

---

## M1 — 拆分存储上帝接口 + 缓存装饰器再架构（P0，依赖：M0 Task1/2）

- [x] 7. 定义 oauth2 仓储接口（暂放现结构，M2b 再迁包）
  - `IClientRepository` / `IGrantRepository` / `ITokenRepository` / `IConsentRepository`
  - 保留 `saveTokenPair`/`revokeTokenFamily` 事务契约、`consumeAuthCode` redirect_uri 校验语义
  - **F4：consent 端口对外用抽象 `UserRef`（经 `ISubjectResolver` 解析），不暴露 `internalUserId`**
  - `ITokenRepository` 增能力标志 `supportsTransactions()` / `supportsCas()`（供 F5 契约分档）
  - **A3：产出「30 个方法 → 目标仓储」完整映射表**，零丢失；`deleteExpiredData` 拆为各仓储 `purgeExpired()` 由产品 `CleanupService` 编排（不放单一仓储）
  - 产出：接口头文件 + 方法映射表；验收：编译通过；映射表覆盖全部 30 方法

- [x] 8. 定义 identity 仓储接口
  - `IUserRepository` / `IRoleRepository` / `ISubjectMappingRepository`；承接 getUserInfo/getUserRoles/getInternalUserId/createSubjectMapping/createUserForExternalLogin
  - 产出：接口头文件；验收：编译通过

- [x] 9. 拆分 `PostgresOAuth2Storage`（1743 行）为多实现文件（ORM 模型暂留原地，M2b 迁移）
  - 各实现一个仓储接口；`PostgresRepositoryBundle` 聚合
  - 产出：拆分后的实现文件；验收：现有测试全绿

- [x] 10. 同步拆分 `RedisOAuth2Storage` / `MemoryOAuth2Storage`
  - 依能力标志声明各自支持的原子性/事务档位
  - 产出：拆分后的实现；验收：现有测试全绿

- [x] 11. 缓存装饰器 `CachedOAuth2Storage` 再架构（评审 H3/A1，新增）
  - 从「包裹整个接口」改为 **per-repository 缓存装饰**（只缓存读多写少/可安全缓存的仓储；令牌/授权码不缓存或仅缓存否定结果）
  - **保留现有并发安全模式（非修缺陷，A1 更正）**：UAF 已在 HEAD 修复（`CachedOAuth2Storage.h:26-27` 已继承 `enable_shared_from_this`，`self` 捕获，提交 `30a1d1e`）——把这套模式**原样保留**到新 per-repository 装饰器，勿丢失
  - 落点：`libs/storage-redis`（或独立 `storage-cache`）
  - 产出：per-repository 缓存装饰实现；验收：`CategoryC_CachedStorageUafTest`（作回归门控）及缓存相关测试全绿

- [x] 12. 编写分档契约测试套件 `tests/contract/`（F5）
  - **功能契约**（所有实现必过）+ **原子性/事务契约**（仅 `supportsTransactions()`/`supportsCas()` 为真的实现运行；Postgres 全过，Memory 尽力而为并标注局限）
  - 产出：分档契约测试 + CTest label `Contract`；验收：各实现按能力档位通过；能力谎报致 CI 失败

---

## M2a — 抽 common + 端口 + 去 drogon::utils（P0，最高风险，依赖：M1）

- [x] 13. 创建 `libs/common`（authforge::common）
  - `Result<T,Error>`、框架无关 `ErrorCatalog`/`ErrorEnvelope`、值对象（Subject/Scope/ClientId/RedirectUri/PkceChallenge/TokenValue/TenantId）
  - 端口：`ISubjectResolver`/`IRoleProvider`/`IUserInfoProvider`/`IClock`/`ICryptoProvider`/`IUuidGenerator`/`IEmailSender`/`ILogger`/`IMetrics`；`AuditEvent` 模型
  - 产出：`libs/common` target + `authforge-common` export；验收：独立编译；纯单测通过；arch-guard 无 `#include <drogon/`

- [ ] 14. 去 `drogon::utils`：实现并替换所有调用点（F3，核心成本，须逐调用点小步提交）
  - 实现 `ICryptoProvider`（`getSha256`/`secureRandomBytes`/base64url/HMAC/PBKDF2/RSA-JWT，OpenSSL 直接实现）、`IUuidGenerator`、`IClock`、`ILogger`
  - 改造调用点：`CryptoUtils.h`（头文件内联，逐 include 点改注入）、`PasswordHasher`、`TotpUtils`、`JwkManager`、`TokenService`、`AuditLogger`、`RequestId`；Domain 内 `LOG_*` 改 `ILogger`
  - **可发布性 + 工作量边界（H6 + 评审 C）**：逐端口逐调用点推进，保证每次提交可编译；`CryptoUtils.h` 有 **14 个 include 者**、Domain 内 **407 处 `LOG_*`**——按「每类端口一个 PR、单 PR 调用点数设上限、超限即停并汇报进度」控制，勿一次性大爆炸
  - 产出：Adapter 默认实现 + Domain 仅依赖端口；验收：Domain 无 `drogon::utils`/`drogon/` 引用；加密单测（PKCE/JWT/密码哈希 golden）逐字节一致

- [x] 15. 端口的 Adapter 实现 + 假实现（测试用）
  - 生产实现放 Adapter；假实现放测试支持库
  - 产出：端口实现 + 假实现；验收：Domain 可用假时钟/假 crypto 做确定性单测

- [ ] 16. 测试链接过渡 + M2a 路径同步（评审 H3-Q3 错配 + H2）
  - Domain 代码在 M2a 迁出 `OAuth2Plugin/`，旧 GLOB 单二进制测试须**同步增量链接新 `libs/*`**（不等 M4）——本任务是**最小过渡桥**（消除时点错配），**Task 27 才是最终形态**（完全移除 `GLOB_RECURSE`），二者不重复造轮子
  - 注意：后端测试用 **`DROGON_TEST` 宏 + `<drogon/drogon_test.h>`**（非 gtest），过渡链接须保留该测试运行器
  - 同步 M2a 引入的构建/源路径变化到 `paths.env` 与本地脚本/agent 入口（不进 CI 的部分须主动改）
  - 产出：过渡期测试 CMake + 路径同步；验收：现有测试在新库结构下全绿；本地 `manage build/test` 可用

---

## M2b — 抽 oauth2 Domain + ORM 归位（依赖：M2a）

- [ ] 17. 创建 `libs/oauth2`（authforge::oauth2），迁入协议逻辑
  - `oauth2::pkce`（纯函数）、`AuthorizationService`/`TokenService`、`access/`（consent+scope 策略+决策引擎）、`model/`（聚合）、DTO 迁入；仓储接口（M1 的 4 个）迁入
  - 产出：`libs/oauth2` target + `authforge-oauth2` export；验收：Domain 纯单测通过；arch-guard 无 drogon 依赖

- [ ] 18. ORM 模型迁 `storage-postgres` + DTO 映射（F2）
  - 14 个生成的 ORM 模型（`Oauth2*`/`Users`/`Roles` 等）迁 `libs/storage-postgres/models/`；补齐 ORM ↔ Domain DTO/值对象双向映射；更新 ORM 生成器输出目录（配合 Task 2）
  - **`models_backup/` 是 ORM 重生成时的临时备份（评审点 1 澄清）**——加入 `.gitignore` 不入库、视为 ephemeral，**不作迁移源、不迁移**；确保迁移/arch-guard 脚本忽略它
  - 产出：storage-postgres 内 models/ + 映射层；验收：Domain 零 `drogon::orm` 引用；DB 集成测试全绿；无 models 重复副本

---

## M2.5 — 抽取 Identity SDK（依赖：M2b）

- [ ] 19. 创建 `libs/identity`（authforge::identity）
  - **实际迁移文件集（评审 A4 更正）**：`AuthService.cc`（365 行，仅 `validateUser`/`registerUser`/`getUserInfo`）+ 分散在各控制器的能力：
    - MFA：`MfaController.cc`（551 行）+ `TotpUtils`
    - WebAuthn：`WebAuthnController.cc`（402 行）
    - Social：`GoogleController.cc` / `WeChatController.cc` / `GitHubController.cc`
    - Session：`SessionController.cc`（855 行）
  - `rbac/` 实现 `IRoleProvider`、`SubjectMapping` 实现 `ISubjectResolver`、userinfo 实现 `IUserInfoProvider`；identity 仓储接口迁入
  - 产出：`libs/identity` target + `authforge-identity` export；验收：独立编译；**不依赖 `libs/oauth2`**（arch-guard 强制）；identity 单测通过

---

## M3 — Drogon 适配器 + 插件注册迁移 + 链接策略 + 产品瘦身（依赖：M2.5）

- [ ] 20. 创建 `libs/drogon`（authforge::drogon）
  - 插件退化为装配器：读配置 → 构造 Adapter 实现 → 注入 Domain 服务 → 注册 controller/filter；迁入 controllers/filters/views
  - 产出：`libs/drogon` target；验收：编译通过

- [ ] 21. 插件注册与 config 加载迁移（评审 H1，新增）
  - 依 design §5.7 抉择方案 A（保留 `OAuth2Plugin` 类名 + config `plugins` 块，插件内部退化装配器）或方案 B（main.cc 显式装配 + config 块迁 `custom_config`）
  - 处理 4 份 `config.*.json` 的 `plugins[].name` 与 `config{}` 业务块（storage_type/clients/admin_users/tokens）兼容
  - 产出：插件注册/配置加载实现 + config 兼容处理
  - 验收：**config 驱动的插件实例化成功**（非仅路由）；4 份 config 启动均不崩

- [ ] 22. whole-archive 链接策略与验证（F1/H5，扩展验收）
  - 产品与测试对 `authforge-drogon`（含 controller/filter/**视图类**/**插件类** 注册符号）用 whole-archive 链接（CMake `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`，回退 `--whole-archive`/`/WHOLEARCHIVE`）
  - 产品启动断言关键路由已注册（fail-fast）
  - 产出：链接配置 + 路由/视图/插件注册自检
  - 验收：静态库形态下 `/oauth2/token` 等路由可访问（非 404）+ `login.csp`/`consent.csp` 可渲染 + config 驱动插件实例化成功

- [ ] 23. controller/filter 去单例化（评审 H4，新增）
  - 把 `drogon::app().getPlugin<OAuth2Plugin>()` 全局查找改为构造注入/桥接
  - **完整受影响清单（评审 B5）**：共 **9 处生产 + ~38 处测试**。注意 grep 须同时匹配 `::OAuth2Plugin` 变体——`OAuth2StandardController.cc`（**位于 `OAuth2Plugin/`**，非 Server）用 `getPlugin<::OAuth2Plugin>` 8 处（行 349/476/569/733/828/1090/1544/1556）；另有 `SessionController`/`HealthController`/`GitHubController`/`DeviceAuthController`
  - 明确 Drogon controller 如何拿到注入实例（工厂 vs 桥接转发）
  - 产出：去单例化后的 controller/filter；验收：Admin API ~52 + OAuth2 ~55 端点测试全绿；Playwright E2E 全绿

- [ ] 24. `apps/server` 装配注入
  - 产品层构造 `identity` 实现，注入 `oauth2` 服务端口（唯一装配点）
  - 产出：装配代码；验收：oauth2 与 identity 零直接编译依赖，功能等价

- [ ] 25. 拆分 `main.cc` bootstrap
  - `CorsSetup`/`SecurityHeaders`/`ExceptionHandlerSetup`/`OpenApiSetup`/`MigrationRunner` 独立；`main` 仅装配
  - **B1：`OpenApiGenerator`（385 行，不依赖 Drogon 路由自省）迁入 `apps/server`**（`OpenApiSetup`），移出 `OAuth2Plugin` 伪领域层
  - 产出：`apps/server/src/bootstrap/`；验收：启动行为等价；OpenAPI 生成正确；E2E 全绿

- [ ] 26. M3 构建产物路径同步（评审 H2，新增）
  - `OAuth2Server`→`apps/server` 目标改名使构建产物路径从 `build/OAuth2Server/{Debug|Release}` 变化——同步更新 `paths.env`、CI、本地脚本（`run-server`/`test`/`smoke-parity`）与 agent workflow
  - 产出：同步后的路径引用；验收：三平台 CI 全绿 + 本地 `manage run-backend/test` 可用 + 抽样 agent workflow 可跑

---

## M4 — 测试消费库化 + SDK 冒烟（依赖：M3）

- [ ] 27. 重构 `tests/` 改为链接库产物（F6）
  - 取消 `GLOB_RECURSE` 全源码编译；按层链接对应库；含注册符号的库 whole-archive
  - **测试框架是 `DROGON_TEST` 宏 + `<drogon/drogon_test.h>`（非 gtest；后端总 319 个 `DROGON_TEST`，`unit/` 139）**——CMake 重构须保留该运行器（勿误按 gtest 组织）
  - 处理 `drogon_create_views` 生成的 view 归属；导出必要测试支持头；剥离对内部私有头的白盒依赖
  - 产出：新测试 CMake；验收：全部测试通过；构建更快；无越界 include 私有头

- [ ] 28. 创建 `examples/third-party-host`（SDK 冒烟，F1/H1/H5 回归防线）
  - 最小 Drogon 宿主，`find_package(authforge-oauth2)` + 实现 3 个端口，whole-archive 链接
  - **实发 HTTP 请求**跑授权码流；断言路由注册 + 视图渲染 + config 驱动插件实例化
  - 产出：样例工程 + CTest label `SdkSmoke`；验收：仅 find_package 集成即可跑通授权码流

---

## M5 — 控制器拆分 + 身份域内聚（依赖：M3，可与 M4 并行）

- [ ] 29. 拆分 `AdminController`（2896 行）
  - → `UserAdminController`/`ClientAdminController`/`RoleScopeAdminController`/`TokenAdminController`/`OrgAdminController`/`AuditController`；业务下沉 application service
  - 产出：6 个薄控制器；验收：Admin API ~52 测试全绿；Playwright E2E 全绿

- [ ] 30. Organization 管理归入 `apps/server/src/organization/`（产品级）
  - 产出：迁移后的组织管理；验收：多租户组织 CRUD 功能等价

- [ ] 31. 社交/邮件/WebAuthn 条件编译（F9 依赖声明）
  - `with_social`/`with_identity`/`with_webauthn` option 控制可选编译单元；显式声明 WebAuthn 的加密/CBOR 依赖
  - 产出：CMake + conanfile option 化；验收：关闭时纯协议引擎依赖面缩小且可编译；开启 WebAuthn 时依赖完整可构建

---

## M6 — CI/CD 重构 + 自动化护栏（依赖：M4）

- [ ] 32. CI 重构为可复用 workflow
  - `ci.yml` + `_build-test.yml` + `_frontend.yml` + `_sdk-smoke.yml`；三阶段门禁（快速门/主门/发布门）；SDK 冒烟纳入发布门
  - 产出：重构后 workflows；验收：三平台矩阵全绿；SDK 冒烟纳入门禁

- [ ] 33. 实现 `tools/arch-guard`
  - 检查 Domain（common/oauth2/identity）禁 `#include <drogon/`（允许 jsoncpp）；oauth2↔identity 无互相 include；Domain 无 `drogon::orm`
  - 产出：arch-guard 脚本 + CI 集成；验收：违规时 CI 失败

- [ ] 34. 实现 `tools/api-diff`
  - SDK 导出头 API 快照 diff，破坏性变更需版本升级
  - 产出：api-diff 工具 + 基线快照；验收：人为破坏 API 时 CI 告警

- [ ] 35. 迁移校验器 + 依赖 EOL 扫描
  - 迁移顺序/幂等/回滚检查；拦截 EOL 依赖（如 OpenSSL 1.1.1）
  - 产出：`tools/migration-check` + `security.yml`；验收：注入 EOL 依赖时 CI 失败

---

## M7 — 发布管线（依赖：M6）

- [ ] 36. `release.yml`：多架构镜像 + SDK 产物
  - buildx amd64+arm64 镜像；SDK 库 + 头 + `authforge-*Config.cmake` 打包
  - SDK 集成文档写明 **whole-archive 链接 `authforge-drogon`**（F1/H5）、**插件注册方式**（H1）与 SDK 运行时契约（线程/ABI/异常/日志，F9）
  - 产出：release workflow + 集成文档；验收：tag 触发产出全部产物；第三方按文档集成可跑通

- [ ] 37. Helm chart + 生产 Compose + 版本化迁移执行器（含 F10）
  - 迁移执行器 + 回滚策略 + 启动自检；secrets 仅经 env/secret store（禁落盘日志）
  - 保留 `PasswordHasher` legacy SHA-256 校验与 `needsRehash` 平滑升级；加迁移兼容测试
  - 产出：`deploy/helm` + 生产 Compose + 迁移器；验收：Helm 部署跑通；升级/回滚可用；legacy 密码可校验并按需 rehash

- [ ] 38. SBOM + 镜像签名 + 自动 CHANGELOG
  - 产出：供应链安全产物 + conventional commits → CHANGELOG；验收：release 附带 SBOM 与签名

---

## M8 — 目录/命名空间整体重组 + 非代码路径对齐（依赖：全部，放最后；原子操作，迁移前打 tag）

- [ ] 39. 目录迁移到 `libs/apps/frontends/tests/examples`
  - 顶层重定位：`OAuth2Server`→`apps/server`、`OAuth2Plugin`→`libs/*`、`OAuth2Admin`→`frontends/admin`、`OAuth2Frontend`→`frontends/user`、`OAuth2Server/sql`→`apps/server/migrations`
  - 代码引用用 IDE 重构 + smart move 自动更新（**仅覆盖代码 import/include，不覆盖脚本/配置/文本路径，见 Task 42-44**）
  - 产出：最终目录结构
  - 验收（可度量，F7）：全量三平台编译通过 + 全部 CTest 标签（Unit/Contract/Integration/E2E/Security/Performance/SdkSmoke）通过 + arch-guard 通过

- [ ] 40. 命名空间统一为 `authforge::{common,oauth2,identity,storage,drogon}`
  - 用语义化重命名，避免手工替换遗漏（类/文件名的梳理见 Task 45 + design §5.8）
  - 产出：统一命名空间；验收：全量编译 + 测试全绿

- [ ] 41. 版本重置 v1.0.0 + 文档更新
  - 统一根 CMake 版本；更新 README/CLAUDE.md/集成文档；补 SDK 运行时契约文档（线程/ABI/异常/日志/whole-archive/插件注册，F9/H1）；前端错误码共享源路径更新（L2）
  - 产出：v1.0.0 + 更新文档；验收：版本一致；集成/契约文档齐备

- [ ] 42. scripts/ 路径与命令维护（评审补充点 1）
  - 经 `paths.env`（Task 5 已建）把最终路径值一次性切到新结构；审计 `manage.ps1/.sh`、`scripts/backend/{build,test,run-server,setup-database,validate-openapi}.{sh,bat}`、`scripts/{smoke-parity.ps1,security-check.sh,test-frontend-url-config.sh}`、`scripts/emoji_manager.py`(默认 `../OAuth2Plugin`) 残留硬编码
  - 产出：更新后脚本；验收：`tools/manage-parity-check.sh` 通过；`manage build/test/run-backend`、setup-database、smoke-parity 三平台实跑通过

- [ ] 43. docker / config 目录引用维护（评审补充点 2 + H1/H7）
  - 更新 `docker-compose*.yml`、各 `Dockerfile`、`.env.docker`、`.dockerignore` 的 build context / COPY 路径
  - 更新应用内路径：`config.json` `document_root`、迁移目录、`MigrationRunner` 相对路径探测（`../../OAuth2Server/sql/migrations`→`apps/server/migrations`）；**`config.*.json` 的 `plugins[].name`（OAuth2Plugin/PromExporter）随 H1 决策处理**
  - 产出：更新后 docker/config 与应用路径逻辑；验收：`docker compose up` 全栈健康检查通过；后端从新路径正确加载 config/迁移/插件

- [ ] 44. .claude / .agent / .vscode 配置与技能维护（评审补充点 3）
  - 更新 `.claude/skills/*/SKILL.md`、`.claude/agents/*.md`、`.claude/MEMORY.md`、`.agent/workflows/*.md`(build/test/start/stop/db-reset)、`.vscode/*`、`.hooks/config.json`、`CLAUDE.md` 的 Repository Layout
  - 产出：更新后 agent/IDE 配置；验收：抽样执行各 workflow/skill 命令可正确运行；MEMORY/CLAUDE 描述与新结构一致

- [ ] 45. 类/文件命名一致性终检（评审建议 3，design §5.8）
  - 按 §5.8 约定核对全部对外类/文件/头命名，修剩余歧义名：`OAuth2StandardController`→`AuthorizationEndpointController`/`TokenEndpointController`/`DiscoveryController`（按职责拆/更名）、澄清 `OAuth2Controller`、`OAuth2Plugin` 仅保留装配器语义
  - 确保「文件名 = 主类名」；rename-on-move 未覆盖的残余用语义化重命名统一
  - **必须在 v1.0.0 冻结（Task 41）前完成**，之后 api-diff 基线（Task 34）才建立在稳定命名上
  - 产出：一致命名 + 遗留死文件清理（含 §5.9 的 `consent.csp` 确认后删除）
  - 验收：全量编译 + 全标签测试全绿；无遗留歧义名/死文件

---

## Notes

- **立即启动 M0 + M1**：M0 的 Task1/2 是编译与 ORM 生成的 gate、Task5 建 `paths.env` 减少后续迁移散点；M1 拆存储接口 + 缓存装饰器再架构是解锁解耦的钥匙。
- **M2a 是真正攻坚点**：去 `drogon::utils`（Task 14）远大于 OpenSSL 迁移，须逐端口逐调用点小步提交保证中间态可编译；独立 PR、最多评审。
- **H1 插件注册是最早的失败点**：早于 F1 的路由 404——config 按类名反射加载，改名/迁库前必须先定 §5.7 方案，Task 21/22 验收含「config 驱动插件实例化成功」。
- **F1/H5 链接陷阱贯穿 M3/M4/M7**：whole-archive 覆盖 controller/filter/视图/插件符号，以 SDK 冒烟 HTTP 断言持续回归。
- **路径引用时序（H2）**：`paths.env`（M0）集中定义 → M2a/M3 构建产物路径变化时同步 → M8 顶层改名最终对齐；**CI 覆盖路径靠 CI 全绿拦截，本地/agent 路径须主动改**。
- **可发布性（H6）**：见 design §14.1；除 M2a 过程中与 M8 原子切换外，各里程碑停点均可交付；M8 迁移前打 tag，整体成功或整体回退。
