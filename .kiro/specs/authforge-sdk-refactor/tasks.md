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
  - **后续统一（2026-07-28）**：构建基座进一步统一为**全平台 Conan + `cmake --preset`**——`conan install --output-folder=build/<preset>` + `cmake --preset` + `cmake --build --preset`，`build/<preset>` 目录约定 + 各平台 debug preset（`windows-msvc-debug`/`linux-debug`/`macos-arm64-debug`）。三平台 CI 均已切到 preset（`ci-*.yml` 的 env `CMAKE_PRESET`/`PRESET_DIR`）。本地脚本 `build/test/run_server/full_test.bat` 4 个 `.bat` 的 `shift` 破坏 `%~dp0` 回归已修（循环前捕获 `SCRIPT_DIR`）。CI 仍是 3 个独立文件（可复用重构见 Task 32）。

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

- [x] 14. 去 `drogon::utils`：实现并替换所有调用点（F3，核心成本，须逐调用点小步提交）
  - 实现 `ICryptoProvider`（`getSha256`/`secureRandomBytes`/base64url/HMAC/PBKDF2/RSA-JWT，OpenSSL 直接实现）、`IUuidGenerator`、`IClock`、`ILogger`
  - 改造调用点：`CryptoUtils.h`（头文件内联，逐 include 点改注入）、`PasswordHasher`、`TotpUtils`、`JwkManager`、`TokenService`、`AuditLogger`、`RequestId`；Domain 内 `LOG_*` 改 `ILogger`
  - **可发布性 + 工作量边界（H6 + 评审 C）**：逐端口逐调用点推进，保证每次提交可编译；`CryptoUtils.h` 有 **14 个 include 者**、Domain 内 **407 处 `LOG_*`**——按「每类端口一个 PR、单 PR 调用点数设上限、超限即停并汇报进度」控制，勿一次性大爆炸
  - 产出：Adapter 默认实现 + Domain 仅依赖端口；验收：Domain 无 `drogon::utils`/`drogon/` 引用；加密单测（PKCE/JWT/密码哈希 golden）逐字节一致

- [x] 15. 端口的 Adapter 实现 + 假实现（测试用）
  - 生产实现放 Adapter；假实现放测试支持库
  - 产出：端口实现 + 假实现；验收：Domain 可用假时钟/假 crypto 做确定性单测

- [x] 16. 测试链接过渡 + M2a 路径同步（评审 H3-Q3 错配 + H2）
  - Domain 代码在 M2a 迁出 `OAuth2Plugin/`，旧 GLOB 单二进制测试须**同步增量链接新 `libs/*`**（不等 M4）——本任务是**最小过渡桥**（消除时点错配），**Task 27 才是最终形态**（完全移除 `GLOB_RECURSE`），二者不重复造轮子
  - 注意：后端测试用 **`DROGON_TEST` 宏 + `<drogon/drogon_test.h>`**（非 gtest），过渡链接须保留该测试运行器
  - 同步 M2a 引入的构建/源路径变化到 `paths.env` 与本地脚本/agent 入口（不进 CI 的部分须主动改）
  - 产出：过渡期测试 CMake + 路径同步；验收：现有测试在新库结构下全绿；本地 `manage build/test` 可用

---

## M2b — 抽 oauth2 Domain + ORM 归位（依赖：M2a）

- [x] 17. 创建 `libs/oauth2`（authforge::oauth2），迁入协议逻辑
  - `oauth2::pkce`（纯函数）、`AuthorizationService`/`TokenService`、`access/`（consent+scope 策略+决策引擎）、`model/`（聚合）、DTO 迁入；仓储接口（M1 的 4 个）迁入
  - 产出：`libs/oauth2` target + `authforge-oauth2` export；验收：Domain 纯单测通过；arch-guard 无 drogon 依赖
  - **完成说明**：分多个 slice 完成（详见 PROGRESS.md "M2b 详细完成内容"）。`AuthorizationService` 是全新类——设计要求但代码里从未真正存在过，用 `ScopeDecisionEngine`（已建）驱动，自己异步编排 client/role/consent 三级事实获取。`TokenService`/`ClientService` 构造参数从旧的整体接口 `IOAuth2Storage` 换成新拆分的 `IClientRepository`/`IGrantRepository`/`ITokenRepository`，全部方法逐一对照旧实现保持行为一致；PKCE 校验改用已修复的 `oauth2::pkce::verifyCodeVerifier`（RFC 7636 合规）。**尚未接入生产**——`OAuth2Plugin` 仍使用旧的 `oauth2::TokenService`/`ClientService`，真正切换是 Task 24。

- [x] 18. ORM 模型迁 `storage-postgres` + DTO 映射（F2）
  - 14 个生成的 ORM 模型（`Oauth2*`/`Users`/`Roles` 等）迁 `libs/storage-postgres/models/`；补齐 ORM ↔ Domain DTO/值对象双向映射；更新 ORM 生成器输出目录（配合 Task 2）
  - **`models_backup/` 是 ORM 重生成时的临时备份（评审点 1 澄清）**——加入 `.gitignore` 不入库、视为 ephemeral，**不作迁移源、不迁移**；确保迁移/arch-guard 脚本忽略它
  - 产出：storage-postgres 内 models/ + 映射层；验收：Domain 零 `drogon::orm` 引用；DB 集成测试全绿；无 models 重复副本

---

## M2.5 — 抽取 Identity SDK（依赖：M2b）

- [x] 19. 创建 `libs/identity`（authforge::identity）【**已补全**：AuthService + RBAC 绑定为首批范围；MFA/WebAuthn/Social/Session 已在后续批次补齐，详见 PROGRESS.md "M2.5 补全"】
  - **实际迁移文件集（评审 A4 更正）**：`AuthService.cc`（365 行，仅 `validateUser`/`registerUser`/`getUserInfo`）+ 分散在各控制器的能力：
    - MFA：`MfaController.cc`（551 行）+ `TotpUtils` —— **已完成**：`TotpUtils`（RFC 6238，改为纯函数+显式端口参数）+ `MfaService`（setup/verify/enable/disable/login-verify/pending-binding），新增 `IMfaRepository`
    - WebAuthn：`WebAuthnController.cc`（402 行）—— **已完成**：`WebAuthnService`（registerBegin/Finish、authenticateBegin/Finish、listCredentials），新增 `IWebAuthnRepository`；保留现有代码本就没做真正 FIDO2 签名验证的既有简化（非新增缺陷）
    - Social：`GoogleController.cc` / `WeChatController.cc` / `GitHubController.cc` —— **已完成**：`GoogleAuthService`/`WeChatAuthService`/`GitHubAuthService`，新增 `IOAuthHttpClient`（出站 HTTP 端口，Domain 层不碰 `drogon::HttpClient`）+ `ISocialAccountRepository`（GitHub 专属的本地账号查找/创建/关联）
    - Session：`SessionController.cc`（855 行）—— **部分完成**（范围刻意收窄）：`SessionController.cc` 大部分逻辑早已被 `AuthService`（认证）+ oauth2 域的 `TokenService`/`ClientService`（token 签发/consent/撤销）覆盖，本次只补两块之前哪里都没实现过的东西：`SessionManager::evaluateLoginPolicy()`（纯函数登录策略决策：邮箱验证 → MFA 判定，邮箱验证优先级已核对源码保留）+ `IBackchannelLogoutNotifier`（logout 的 backchannel 通知转发端口，替换原来只打日志的 stub）
  - `rbac/` 实现 `IRoleProvider`、`SubjectMapping` 实现 `ISubjectResolver`、userinfo 实现 `IUserInfoProvider`；identity 仓储接口迁入
  - 产出：`libs/identity` target + `authforge-identity` export；验收：独立编译；**不依赖 `libs/oauth2`**（arch-guard 强制，grep 确认零 include）；identity 单测通过（19 → **81 个**）
  - **发现并修复的真实潜在缺陷**：`WITH_WEBAUTHN`/`WITH_SOCIAL` 两个 CMake 变量之前只被 `$<BOOL:...>` 生成器表达式和 `#ifdef` 引用，从未在任何地方 `option()` 声明过，导致这两个变量一直是未定义/OFF，`webauthn/*.cc`、`social/*.cc` 从来没有真正被编译到过。已在 `libs/identity/CMakeLists.txt` 补上 `option(... ON)`。conanfile.py 侧的 `with_webauthn`/`with_social` 选项贯通留给 Task 31。
  - **全部新增代码均未接入生产**：`libs/drogon/src/controllers/{Mfa,WebAuthn,Google,WeChat,GitHub,Session}Controller.cc` 保持原样不动，继续用旧的 `OAuth2Plugin`/inline 逻辑；真正装配是 Task 24。

---

## M3 — Drogon 适配器 + 插件注册迁移 + 链接策略 + 产品瘦身（依赖：M2.5）

- [x] 20. 创建 `libs/drogon`（authforge::drogon）
  - 插件退化为装配器：读配置 → 构造 Adapter 实现 → 注入 Domain 服务 → 注册 controller/filter；迁入 controllers/filters/views
  - 产出：`libs/drogon` target；验收：编译通过
  - **重大突破（用户提出并主导验证，详见 design.md §5.5 更新）**：`HttpController<T, false>`（`AutoCreation=false`）+ 显式 `drogon::app().registerController(...)` 可以让 controller 迁入 STATIC 库后完全不需要 whole-archive 链接。已用真实运行的可执行文件 + curl 验证。**全部 15 个 controller + `OAuth2StandardController`（协议核心）+ 2 个主动挂路由的 filter + `AuthService` 均已迁入 `libs/drogon`**，全程未用 whole-archive。
  - 踩坑记录（已修复，通用规律记入 PROGRESS.md）：`authforge::drogon::*` 命名空间内裸写 `common::error::`/`oauth2::`/`drogon::`（不带 `authforge::` 前缀的全局命名空间）会被 include 链间接引入的 `authforge::` 子命名空间可见性坑到，优先匹配到错误的命名空间——统一改为 `::` 全局限定修复。

- [x] 21. 插件注册与 config 加载迁移（评审 H1，新增）
  - 依 design §5.7 抉择方案 A（保留 `OAuth2Plugin` 类名 + config `plugins` 块，插件内部退化装配器）或方案 B（main.cc 显式装配 + config 块迁 `custom_config`）
  - 处理 4 份 `config.*.json` 的 `plugins[].name` 与 `config{}` 业务块（storage_type/clients/admin_users/tokens）兼容
  - 产出：插件注册/配置加载实现 + config 兼容处理
  - 验收：**config 驱动的插件实例化成功**（非仅路由）；4 份 config 启动均不崩
  - **决策：方案 A**。理由见 design.md §5.7 更新——插件本体是 OBJECT 库（非 STATIC），天然不受链接器丢弃符号影响；验收标准已被现有 30+ 处 `getPlugin<OAuth2Plugin>()` 断言非空的测试隐式覆盖。4 份 config 零改动。

- [x] 22. whole-archive 链接策略与验证（F1/H5，**降级为评估性任务，大概率不再需要**）
  - 产品与测试对 `authforge-drogon`（含 controller/filter/**视图类**/**插件类** 注册符号）用 whole-archive 链接（CMake `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`，回退 `--whole-archive`/`/WHOLEARCHIVE`）
  - 产品启动断言关键路由已注册（fail-fast）
  - 产出：链接配置 + 路由/视图/插件注册自检
  - 验收：静态库形态下 `/oauth2/token` 等路由可访问（非 404）+ `login.csp`/`consent.csp` 可渲染 + config 驱动插件实例化成功
  - **状态更新**：Task 20 完成的 `AutoCreation=false` 方案已验证完全替代 whole-archive 的作用（见 design.md §5.5），controller 迁移全程未用任何形式的 whole-archive 链接。剩余待评估：**views**（`login.csp`/`consent.csp`）和**插件类本身**是否也有类似免 whole-archive 路径——插件类已确认没有（`Plugin<T>` 无 `AutoCreation` 参数，若插件本体未来改为静态库分发仍需要），views 大概率也不需要（渲染是运行期按名查找模板，非链接期符号，已用 `showLoginPage` 验证正常工作）。本任务保留在列表中作为"确认性收尾"，非阻塞项。
  - **评估结论（本次执行确认，无新增代码）**：
    1. **View 类**：`drogon_create_views` 把 `.csp` 编译为 `.cc`/`.h`（`build/OAuth2Server/{login,consent}.{cc,h}`）直接 `target_sources` 进 `OAuth2Server` 可执行体，不是"自注册符号型"目标（没有依赖静态初始化触发注册、也不被链接器按需丢弃的风险），与 controller/plugin 的问题模型不同，天然不需要 whole-archive。
    2. **插件类**：核实 `OAuth2Plugin/CMakeLists.txt:31` 为 `add_library(OAuth2Plugin OBJECT)`（非 STATIC），OBJECT 库的所有 `.obj` 无条件整体链入消费者，不存在链接器按引用丢弃符号的问题；`OAuth2Server/CMakeLists.txt` 对其也是普通 `target_link_libraries`（非 whole-archive）。与 design.md §5.7 记录一致。
    3. **端到端验证**（Debug 构建 + 本机 Postgres/Redis + 真实 `OAuth2Server.exe` + curl，均为普通链接、无任何 whole-archive 配置）：`/health`→200；`/oauth2/token`（client_credentials，无认证）→401（非404，路由存在）；`/oauth2/authorize`（缺 state）→400（非404，路由存在）；`/.well-known/jwks.json`→200；`/login`→200 且返回真实渲染的 `login.csp` HTML（非空壳）；不存在路由 `/this-route-does-not-exist-xyz`→404（确认 404 判定有效）。服务器日志确认 `OAuth2Plugin initialized with storage type: postgres` 与 `Controller/filter plugin dependencies wired`，即 config 驱动的插件实例化成功。`consent.csp` 复核仍为 §5.9 记录的死文件（服务端无渲染入口，走前端重定向），不影响本任务验收范围。
    4. **结论**：全部验收标准在不引入 whole-archive 的现状下已满足，无需补充任何 CMake whole-archive 链接配置。本任务性质确认为"验证现有方案已满足原始验收目标"，不产生新代码变更。

- [x] 23. controller/filter 去单例化（评审 H4，新增）
  - 把 `drogon::app().getPlugin<OAuth2Plugin>()` 全局查找改为构造注入/桥接
  - **完整受影响清单（评审 B5）**：共 **9 处生产 + ~38 处测试**。注意 grep 须同时匹配 `::OAuth2Plugin` 变体——`OAuth2StandardController.cc`（**位于 `OAuth2Plugin/`**，非 Server）用 `getPlugin<::OAuth2Plugin>` 8 处（行 349/476/569/733/828/1090/1544/1556）；另有 `SessionController`/`HealthController`/`GitHubController`/`DeviceAuthController`
  - 明确 Drogon controller 如何拿到注入实例（工厂 vs 桥接转发）
  - 产出：去单例化后的 controller/filter；验收：Admin API ~52 + OAuth2 ~55 端点测试全绿；Playwright E2E 全绿
  - **完成说明（方案与原设想不同）**：原设想"改为构造注入"在框架层面不可行——插件由 config 反射构造，必须晚于 controller 注册完成，构造函数注入要求"依赖先于使用者存在"，这里恰好反了。**实际方案：两阶段装配（setter 注入）**——controller/filter 仍按 Task 20 的 `AutoCreation=false` 机制构造注册；新增 `setPlugin(OAuth2Plugin*)` setter + `resolvePlugin()`（缓存指针优先，未设置时回退到全局查找，向后兼容）；新增 `bootstrap::wireControllerPluginDependencies()`，在 `registerBeginningAdvice` 回调（插件已构造完成的时间点）里用 `drogon::DrClassMap::getSingleInstance<T>()` 取到已注册的同一单例逐个注入。实际受影响：6 个 controller + 2 个 filter（15 处调用点，比预估的 9 处略多，因为逐一核实后发现部分文件有多处调用）。真实服务器验证：启动日志确认 wiring 执行，`/health`/`/login`/`/api/admin/dashboard` 行为不变。

- [x] 24. `apps/server` 装配注入
  - 产品层构造 `identity` 实现，注入 `oauth2` 服务端口（唯一装配点）
  - 产出：装配代码；验收：oauth2 与 identity 零直接编译依赖，功能等价
  - **前置条件已就位**：`libs/oauth2` 的 `TokenService`/`ClientService`/`AuthorizationService`（Task 17）与 `libs/identity` 的 `AuthService`/`MfaService`/`WebAuthnService`/社交登录服务/`SessionManager`（Task 19 补全）均已完成、独立编译、独立测试通过，但均未接入生产。本任务是把它们真正接进 `OAuth2Server`（未来 `apps/server`）的请求路径，替换现在 controller 里对 `OAuth2Plugin`/旧 `oauth2::TokenService` 等的直接调用。
  - **完成说明**：按 6 个 slice 完成（详见 PROGRESS.md "Task 24 切分方案"及各 Slice 完成记录）。Slice 1：`LegacyStorageRepositoryBridge` 桥接旧存储到新仓储接口。Slice 2：`OAuth2Plugin` 内部切换到新 `TokenService`/`ClientService`（合并了原计划的 slice 2+3，因为 `OAuth2Plugin` 本就是转发外壳，controller 调用点无需改动）。Slice 4：`SessionController` 接入 `identity::AuthService`/`SessionManager`（新增 `OAuth2Server/bootstrap/IdentityAssembly.cc` 装配模块），修复了两个真实缺陷（命名空间裸写坑 + `int64_t`/Postgres `int4` 绑定宽度不匹配导致 MFA 分支 `DB_QUERY_ERROR`）。Slice 5：`MfaController`/`WebAuthnController`/`Google|WeChat|GitHubController` 接入对应 identity 服务，新写 `PostgresMfaRepository`/`PostgresWebAuthnRepository`/`PostgresSocialAccountRepository`/`DrogonOAuthHttpClient` 四个 Adapter 实现（Task 19 只写了 Domain 服务未写 Adapter）。Slice 6：清理确认——`OAuth2Plugin` 从未持有 identity 字段无需清理，`libs/oauth2`↔`libs/identity` 零互相依赖已用 grep 核实，`libs/drogon/src/AuthService.cc` 旧 Adapter 有意保留作为未注入场景回退。全部 6 个 slice 均遵循"注入优先，未注入回退旧路径"的兼容约定，未破坏任何既有测试。验证：`ctest` 290/290 全绿 + 多轮真实服务器 curl 端到端验证（login/register/logout/MFA 全流程/WebAuthn 全流程通过；Google/WeChat/GitHub 受限于沙箱网络无法完整验证第三方 API 但确认请求路径不崩溃）。

- [x] 25. 拆分 `main.cc` bootstrap
  - `CorsSetup`/`SecurityHeaders`/`ExceptionHandlerSetup`/`OpenApiSetup`/`MigrationRunner` 独立；`main` 仅装配
  - **B1：`OpenApiGenerator`（385 行，不依赖 Drogon 路由自省）迁入 `apps/server`**（`OpenApiSetup`），移出 `OAuth2Plugin` 伪领域层
  - 产出：`apps/server/src/bootstrap/`；验收：启动行为等价；OpenAPI 生成正确；E2E 全绿
  - **完成说明**：bootstrap 拆分（6 个模块 + `ControllerRegistration`）在 commit `6f250a8` 完成。B1（OpenApiGenerator 迁出）**落点修正为 `libs/drogon`（`authforge::drogon::observability::openapi`）而非字面的 `apps/server`**——理由：全部 16 个 controller 的静态初始化 `XxxControllerDocs` 直接调 `addEndpoint`，真实调用方在 `libs/drogon`；放进 `apps/server` 会让 `libs/drogon` 反向依赖 app（环依赖）。`apps/server` 的 `OpenApiSetup` 仍拥有「配置 server URL + 写盘」编排，端点注册表随调用方留在 `libs/drogon`，达成「迁出 `OAuth2Plugin` 伪领域层」实质目标。详见 PROGRESS.md "Task 25 B1" + design.md §15 item 5。验证：全量编译 + `ctest` **290/290** 全绿（含 4 个 OpenAPI 专属测试）+ 真实服务器 curl `/docs/api/openapi.json` 返回 59 端点的合法 spec。

- [x] 26. M3 构建产物路径同步（评审 H2，新增）
  - `OAuth2Server`→`apps/server` 目标改名使构建产物路径从 `build/OAuth2Server/{Debug|Release}` 变化——同步更新 `paths.env`、CI、本地脚本（`run-server`/`test`/`smoke-parity`）与 agent workflow
  - 产出：同步后的路径引用；验收：三平台 CI 全绿 + 本地 `manage run-backend/test` 可用 + 抽样 agent workflow 可跑
  - **完成说明（评估性任务，无代码改动，同 Task 22 模式）**：M3 期间构建产物路径**实际未变化**——CMake target 仍是 `project(OAuth2Server)`（`OAuth2Server/CMakeLists.txt:2`），二进制仍是 `OAuth2Server.exe`，`paths.env` 的 `SERVER_BUILD_SUBDIR=OAuth2Server`/`SERVER_BINARY_NAME=OAuth2Server`/`OAUTH2_SERVER_DIR=OAuth2Server` 均未变；M2a–M3 新增的 `libs/*` 构建产物是 CMake 内部（`LIBS_*_DIR` 已在 paths.env，顶层 `CMakeLists.txt` `add_subdirectory` 消费），无脚本/CI 直接引用。本任务描述的 `OAuth2Server→apps/server` 改名 + 全量路径同步是 **M8（Task 39 目录迁移 + Task 42-44 脚本/docker/agent 路径维护）**，design.md §14.1 标注 M8 为「原子切换、迁移前打 tag」，design.md 路径时序注（"M2a/M3 构建产物路径变化时同步 → M8 顶层改名最终对齐"）亦确认改名在 M8——**M3 不做改名**，否则破坏 M8 原子性。
  - **验收核对**：① paths.env 驱动的脚本（`manage.ps1`/`manage.sh`/`scripts/backend/*`/`scripts/smoke-parity.*`，经 4 个 loader `cmake/Paths.cmake`/`paths-env.ps1`/`paths_env.bat`/`env_common.sh`）正确解析路径（`Import-PathsEnv` 实测返回 `BUILD_DIR=build SERVER_BUILD_SUBDIR=OAuth2Server ...`）；② agent workflow 引用的构建产物路径存在（`build/OAuth2Server/Debug/OAuth2Server.exe`、`build/OAuth2Server/test/Debug/OAuth2Test_test.exe`）；③ `manage.ps1 test-backend -debug` wrapper 经 paths.env 正确跑通——Run 1（standard config.json/postgres）全绿，Run 2（config.ci.json/memory）的 45 个 `Contract.*` 测试因 `dbClientsMap_` assert 崩溃（**pre-existing memory-config 限制**：Contract 测试构造 Postgres/Redis 存储直连 `getDbClient`，memory 配置无 DB client；`ContractFixtures.h` 的 storage_type=="memory" guard 已核实仍完整；与 Task 25/26 无关，直接 ctest postgres config 290/290 证明零回归）；④ 直接 `ctest`（config.json/postgres）290/290 全绿。
  - **M8 同步目标清单（本任务为 M8 留的清单，已知硬编码 `OAuth2Server`/`build/OAuth2Server` 且不经 paths.env 的文件，Task 42-44 处理）**：CI `.github/workflows/ci-{linux,windows,macos}.yml`；docker `deploy/docker/Dockerfile:54-58`、`docker-compose.yml`、`docker-compose.prod.yml`、`docker-quick-verify-debug.sh`；agent workflows `.agent/workflows/{build,test,test-checklist,stress-test,stop,db-reset,orm-gen,pre-commit}.md`；skills `.claude/skills/{e2e-test,docker-integration-test,orm-gen,db-reset,create-migration,openapi-update,release}/SKILL.md`；agent defs `.claude/agents/*.md`；独立脚本 `scripts/{test-frontend-url-config.sh,security-check.sh}`；hook `.claude/settings.json:33`（pre-commit `cd build/OAuth2Server && ctest`）；文档 `README*.md`/`CLAUDE.md`。**无需改动**：`manage.ps1`/`manage.sh`/`scripts/backend/*`/`scripts/smoke-parity.*`/顶层 `CMakeLists.txt`/`cmake/Paths.cmake`/任何 `libs/` 路径（均 paths.env 驱动或 CMake 内部）。

---

## M4 — 测试消费库化 + SDK 冒烟（依赖：M3）

> **执行顺序调整（2026-07-11，Task 28 调研结论）**：原计划 M4 收尾后进 M5。调研发现 Task 28「纯引擎 SDK 冒烟」被存储接口迁移收尾（新增 Task 27.5，M1/M2b 之间漏项）卡住，详见 Task 28 说明。故调整顺序：**先做 M5（Task 29/30/31，不阻塞、可与 M4 并行、Task 31 直接服务"协议引擎可复用"）→ Task 27.5 存储接口迁移收尾 → Task 28a 全栈 build-tree smoke → M6/M7/M8**。Task 28b 纯引擎形态留到 27.5 完成后升级。

- [x] 27. 重构 `tests/` 改为链接库产物（F6）
  - 取消 `GLOB_RECURSE` 全源码编译；按层链接对应库；含注册符号的库 whole-archive
  - **测试框架是 `DROGON_TEST` 宏 + `<drogon/drogon_test.h>`（非 gtest；后端总 319 个 `DROGON_TEST`，`unit/` 139）**——CMake 重构须保留该运行器（勿误按 gtest 组织）
  - 处理 `drogon_create_views` 生成的 view 归属；导出必要测试支持头；剥离对内部私有头的白盒依赖
  - 产出：新测试 CMake；验收：全部测试通过；构建更快；无越界 include 私有头
  - **完成说明**：`OAuth2Server/test/CMakeLists.txt` 不再直接 GLOB 编译 `OAuth2Plugin/src/*.cc`（59 个）与已空的 `OAuth2Server/{controllers,filters}`——这些是 Task 16 的过渡桥（plugin 源被双编译：一次进 test、一次进 OBJECT 库经 `authforge::drogon` 传递）。改为显式 `target_link_libraries(... OAuth2Plugin ...)`（OBJECT 库，CMake 去重，无双符号）+ `authforge::{common,oauth2,storage::postgres,drogon,identity}`。删除冗余的 `OAuth2Plugin/src` 私有 include 路径（已核实 47 个测试用的 `<oauth2/...>` include 全部解析到 public `include/oauth2/`，零 src-only 私有头）+ 删除 test 目标上冗余的 `find_package(CURL)`/`CURL::libcurl`/`CURL_STATICLIB`（现经 OAuth2Plugin PUBLIC 传递，test 不再自编 EmailService.cc）。**whole-archive 不需要**（AutoCreation=false controller 显式注册 + OBJECT 库，design §5.5 / Task 22 结论）。view 归属：`drogon_create_views`（login.csp/consent.csp）仍在 test 目标（测试经 SessionController 渲染 login.csp），符合 §5.5「view 是运行期模板查找非链接期符号」。保留直编：`SchemaManager.cc` + `bootstrap/{ControllerRegistration,IdentityAssembly}.cc`（apps/server 关注点，尚无独立库；test_main.cc 直接调用）。DROGON_TEST 运行器、contract 测试注册、compile defs、config 拷贝全部不变。
  - **验收**：全量编译通过；`ctest -C Debug` **290/290 全绿**（零回归）；构建更快——test 目标不再编译 59 个 plugin 源（构建输出仅含 test/schema/bootstrap/views），消除双编译；零越界私有头 include（src 路径已移除）。

- [x] 27.5. 存储接口迁移收尾（解锁 Task 28b 纯引擎形态；M1/M2b 之间漏项补回）
  - 现状（2026-07-11 核实）：存储层处于半迁移——旧 `oauth2::storage::*` 接口（`IClientRepository`/`IGrantRepository`/`ITokenRepository`/`IConsentRepository` + `IOAuth2Storage` facade + DTO，均在 `OAuth2Plugin/include/oauth2/storage/`，namespace `oauth2`，M1 Task 7/9/10 产物）与新 `authforge::oauth2::repository::*`（`libs/oauth2/include/authforge/oauth2/repository/`，M2b Task 17 产物，`AuthorizationService` 实际吃的接口）**并存**；Memory 实现（`MemoryClientRepository` 等 9 个，绑旧接口 + namespace `oauth2`）与 Postgres 实现（`PostgresOAuth2Storage` 等，仍在 `OAuth2Plugin/src/storage/`，M2b Task 18 只迁了 ORM 模型 + 4 个 identity 仓储）**均未迁到新接口**。
  - 工作：Memory/Postgres 仓储实现从 `oauth2::storage::*` 迁到 `authforge::oauth2::repository::*`（+ identity 侧 `authforge::identity::*`）；退役 `oauth2::storage::*` 与 `IOAuth2Storage` facade（或保留 facade 作过渡，按迁移阻力定）；DTO 归位（OAuth2Client 等随接口走）。
  - 产出：单一存储接口层（`authforge::oauth2::repository::*`）；验收：全量编译 + ctest 全绿；`oauth2::storage::*` 无残留引用（grep 可证）。
  - 注：这是纯引擎 Task 28b 的真正前置，不是 M8 命名空间统一（Task 40 只改名、不合并两套接口）。
  - **执行决策与可行性核实（2026-07-17，执行前再核）**：
    - **DTO 已确认逐字段相同**：旧 `oauth2::OAuth2Client/AuthCode/AccessToken/...`（IOAuth2Storage.h）与新 `authforge::oauth2::model::*`（Dto.h）字段布局完全一致（clientId/clientType/clientSecretHash/salt/redirectUris/allowedScopes 等），只是 namespace 不同。→ split-repo 迁移是**机械式**（换 include + namespace 限定，无 DTO 字段转换）。早期"snake_case"观察是 JSON 序列化噪音，非 struct 布局。
    - **split-repo 是 contract 测试的对象（非死代码）**：`ClientRepositoryContractTest.cc` 等直接 `make_shared<MemoryClientRepository>()` 等测 16 个 Memory-tier + 对应 Postgres/Redis 用例——这些 split-repo 类是 46 个 contract 测试的安全网，迁移后由它们验证。
    - **生产路径是 god facade**：`OAuth2Plugin` 实际构造 `PostgresOAuth2Storage`/`RedisOAuth2Storage`/`MemoryOAuth2Storage`/`CachedOAuth2Storage`（god 类，30+ 方法），经 `LegacyStorageRepositoryBridge` 适配到新接口。**facade 退役 + bridge 退役 + 服务重连是独立的大块（phase 4）**，与 split-repo 迁移解耦。
    - **分阶段**：phase 1 = Memory split-repo（4 个 oauth2 聚合 + bundle）就地迁新接口（namespace 限定 + 换 include），16 个 Memory-tier contract 测试验证；phase 2 = Postgres split-repo；phase 3 = Redis + CachedOAuth2Storage 重构（§7.4）；phase 4 = 退役 IOAuth2Storage facade + bridge + 服务重连 + 物理迁包。每 phase build + ctest 全绿。
    - **28a 的真实前置**（与本任务并列的另一条线）：各包缺 `Config.cmake.in`/`find_dependency` + build-tree `export()` + OBJECT 库（OAuth2Plugin）分发（§5.7 推迟风险）——这是 28a "外部 find_package 消费"的硬缺口，与 27.5 独立。故按"无前置阻塞先做"规则，**先做 27.5**（自包含、有 contract 测试网），28a 打包地基留后或与 M7 Task 36 合并。
  - **phase 1-3 完成说明（2026-07-17）**：12 个 oauth2 split-repo（Memory/Postgres/Redis × Client/Grant/Token/Consent）+ 3 个 RepositoryBundle 已从旧 `oauth2::storage::*` 迁到新 `authforge::oauth2::repository::*` 接口 + `authforge::oauth2::model::*` DTO。机制：旧/新 DTO **字段完全相同**（`clientId/clientType/clientSecretHash/...`），故每个 repo `.h` 换 include + `using IXxxRepositoryBase = ::authforge::oauth2::repository::IXxxRepository;` + 基类替换 + 内联成员类型全限定（因旧 `oauth2::*` DTO 仍在 IOAuth2Storage.h 共存，namespace 内别名会重定义冲突）；每个 `.cc` 在 namespace 作用域加 callback + DTO 别名（`.cc` 不含 IOAuth2Storage.h，安全）；3 个 bundle 加 4 个 oauth2 接口的 `using` 别名。**关键坑**：`PostgresClientRepository.cc` 直 include `<oauth2/types/OAuth2Types.h>`（旧 `oauth2::ClientType`/`stringToClientType`）→ 删该 include，由新 model 经别名提供；`stringToClientType` 是函数须用 using-declaration（`using ::...::stringToClientType;`）非类型别名。**contract test 耦合**：4 个 contract 文件经同一 helper 跨 3 后端，故 Memory 单独迁会破坏——必须 3 后端同时迁；测试改 `using namespace authforge::oauth2::repository;` + `using namespace authforge::oauth2::model;` + 具体类（`MemoryClientRepository` 等）加 `oauth2::` 限定。验收：`manage.ps1 build-backend -debug` 绿；`ctest -C Debug` **290/290**（45 个 contract 测试全绿，含 Memory/Postgres/Redis 三层；Redis 偶发 flaky 重跑即过）。**facade (`IOAuth2Storage`) + `LegacyStorageRepositoryBridge` 仍在生产路径**（plugin/服务层仍用它们）→ phase 4 退役 + 服务重连。
  - **phase 4 完成说明（2026-07-21，7 commit）**：god facade + bridge 在生产路径**全面退役**。子阶段：4.1+4.2（`9220225`，reroute `incrementIntrospectCount` + `CleanupService`→`purgeExpired`）；4.3（`8780253`，`OAuth2StandardController` token/client 调用经 plugin 转发法 reroute off god facade）；4.4（`71f47d4`，`CachedClientRepository` port 到新接口）；4.5（`7c9aa04`，identity 角色解析——`IRoleProvider` 加 subject-string 重载，`StorageRoleProvider` 实现之，retire `LegacyRoleResolutionBridge` 的 synthetic-id shim，**PluginTest "Verify Admin Roles" 行为零回归**）；4.6a（`36a55d0`，**核心 pivot**——plugin `initStorage` 改构造 per-backend `RepositoryBundle`，`storage_` 成员 + `getStorage()` 双 accessor 删除，bridge 不再构造，`IdentityService` 改持 4 个 identity repo（role/user/subjectMapping/consent，consent 经 `UserRef` 封装 int32），`AccessToken`/`Client` alias 指向新 model）；4.7 part1（删除 `LegacyStorageRepositoryBridge.h` + 其测试）。**删除的测试**（god-facade-based，覆盖已迁/已修）：6 个 CategoryC UAF + Property4 baseline（缺陷 1.8/1.9/1.11 已修、CachedOAuth2Storage/legacy TokenService 路径已删）+ `AdvancedStorageTest` 迁到 plugin 转发法。验收：每子阶段 `manage.ps1 build-backend -debug` 绿 + `ctest -C Debug`（build 根）**290/290 零回归**。**剩余 4.7 part2**：物理删除 god impl 文件（`{Memory,Postgres,Redis}OAuth2Storage.{h,cc}` + `CachedOAuth2Storage.{h,cc}` + `IOAuth2Storage.h`）+ 迁移/删除剩余 god-impl 存储测试（`Memory/Postgres/Redis StorageTest`、`SubjectMappingTest`、`ScopeValidationTest`、`P0FunctionalityTest`、`RedirectUriValidationTest`）。god impls **在生产已死**（plugin 不再构造），仅这几个测试还引用——属收尾清理，contract tests（phase 1-3）已覆盖 split-repo 层。**4.7 part2 收尾确认（2026-07-28，随目录重组完成）**：god impl 文件（`{Memory,Postgres,Redis}OAuth2Storage.{h,cc}` + `CachedOAuth2Storage.{h,cc}` + `IOAuth2Storage.h`）已在重组 Phase 1.5/2 随存储迁移物理删除，全仓 glob/grep 核实零代码引用（仅剩叙述性注释）；原「待迁移/删除」的存储测试已脱离 god impl 并在 `tests/{unit,integration}` 新位置正常编译通过（含在 ctest 276/277 基线内）。Task 27.5 全部子项完结。


- [x] 28. 创建 `examples/third-party-host`（SDK 冒烟，F1/H1/H5 回归防线）—— 拆为 28a（现在可做）+ 28b（纯引擎，gated on 27.5）
  - [x] 28a（完成，2026-07-28）：全栈 build-tree smoke + 各 SDK 包 `Config.cmake.in`/`find_dependency`/configure-time `export()` 打包地基（见下方完成说明）。
  - [x] 28b（完成，`154e656`）：纯引擎形态。`examples/third-party-host` 仅链接 4 个 SDK 包（`authforge::oauth2`/`common`/`common::testing`/`storage::memory`，无 OAuth2Plugin/libs/drogon），真实装配引擎（内存仓储 + `FakeCryptoProvider`）+ 程序化跑授权码流核心步骤（`evaluateScopes` → `generateAuthorizationCode` → `exchangeCodeForToken`）。根 CMake 加 `option(BUILD_EXAMPLES ON)`。**关键坑（已修）**：`TokenService` 继承 `enable_shared_from_this`，`exchangeCodeForToken`/`refreshAccessToken` 调 `shared_from_this()`——必须 `std::make_shared`，栈分配会 `bad_weak_ptr`→terminate（exit 3）。详见 PROGRESS.md "B8b / Task 28b" 段。
  - **调研结论（2026-07-11）**：原任务文本自相矛盾——前半"`find_package(authforge-oauth2)` + 实现 3 个端口"= 纯引擎；后半"断言路由注册 + 视图渲染 + config 驱动插件实例化"= 全产品栈。两者在当前包结构下互斥。根因：纯引擎消费者要喂 `AuthorizationService`（吃 `authforge::oauth2::repository::*` 新接口），但现成 Memory 实现绑在 `oauth2::storage::*` 旧接口上（Task 27.5 才迁移）。另：SDK 各包缺 `Config.cmake.in`/`find_dependency`（外部 `find_package` 跨包消费不通）；`OAuth2Plugin` 是 OBJECT 库，install/静态库分发是 §5.7 明确推迟的风险（line 558：v1.x 仅源码集成）。
  - **28a（现在可做，先做）**：全栈 build-tree smoke。样例以 `find_package` 消费整套已验证产品栈（oauth2+plugin+drogon+identity+storage-postgres+common）经 build-tree（line 558「源码集成」），复用 controller/plugin/view，实发 HTTP 跑授权码流；断言路由注册 + 视图渲染 + config 驱动插件实例化。各包加 `Config.cmake.in`/`find_dependency` + configure-time `export()`（build-tree Config，有界机械）。whole-archive 不需要（§5.5）。产出：样例工程 + CTest label `SdkSmoke`（也满足 M8 Task 39 验收对 SdkSmoke 的依赖）+ 打包地基。
  - **28b（gated on 27.5，后做）**：纯引擎形态升级。消费者实现 3 端口（`ISubjectResolver`/`IRoleProvider`/`IUserInfoProvider`）+ 仓储（迁到新接口后）喂 `AuthorizationService`/`TokenService`，自写薄 `/authorize`、`/token`，不依赖 authforge-drogon。验收："仅 `find_package(authforge-oauth2)` + 实现 3 端口即可跑通授权码流"（§1.1 头号目标的真正证明）。
  - **28a 完成说明（2026-07-28）**：分两段落地并本地实跑验证（Windows/MSVC Release）。
    - **① 打包地基（8 个 SDK 包）**：新增共享 helper `cmake/AuthForgePackage.cmake`（`authforge_package(TARGET/PACKAGE/EXPORT_NAME/DEPENDENCIES)`）+ 模板 `cmake/AuthForgePackageConfig.cmake.in`（`@PACKAGE_INIT@` + `find_dependency` 闭包块 + `include(<pkg>Targets.cmake)` + `check_required_components`）。每个包（common/common-testing/oauth2/identity/storage-{memory,redis,postgres}/drogon）的裸 `install(EXPORT)` 替换为 helper 调用，同时产出 **install-tree**（`lib/cmake/<pkg>/`）与 **build-tree**（`${CMAKE_BINARY_DIR}/authforge-cmake/<pkg>/`）两套 `Config`+`ConfigVersion`+`Targets`。**修复潜在 bug**：旧 `install(EXPORT ... NAMESPACE authforge::)` 未设 `EXPORT_NAME`，导出名会是 `authforge::authforge-<pkg>`（与 in-tree ALIAS `authforge::<alias>` 及 design §5.5 规范名不符）——helper 对每个 target 设 `EXPORT_NAME` 修正为规范名（如 `authforge::common`/`authforge::storage::memory`/`authforge::drogon`）。drogon 包 Config 的 `find_dependency` 闭包含全部 9 项（Drogon/OpenSSL/CURL + 6 个 authforge 包）。**关键坑（已修）**：`include_guard(GLOBAL)` 使模块 body 只在首个 include 的目录作用域跑一次，普通变量在其他包目录调用函数时不可见 → 模块目录改用 `CACHE INTERNAL` 变量存储，否则模板路径解析为空报 `File /AuthForgePackageConfig.cmake.in does not exist`。
    - **② 全栈 HTTP smoke**：新增 `examples/full-stack-host`（**独立工程，非 add_subdirectory**），经 `find_package(authforge-drogon CONFIG REQUIRED)` 从 build-tree 消费整套产品栈（`authforge::drogon` PUBLIC 传递 common/oauth2/identity/三 storage/Drogon/OpenSSL/CURL 全闭包），复用 apps/server 的 `bootstrap::{registerAllControllers,wireControllerPluginDependencies,wireIdentityServices}` + OrganizationController + `drogon_create_views(apps/server/views)`（login.csp/consent.csp），加载 memory 配置起真实 HTTP 服务（127.0.0.1:6789），用 in-process `HttpClient` 断言四点：路由注册（`GET /health` 200）、config 驱动插件实例化（`GET /.well-known/openid-configuration` 200 + issuer，由 config `plugins[].OAuth2Plugin` 反射实例化的 discovery 应答）、视图渲染（`GET /login` 200 且 body 含 login.csp 标题）、授权码流入口（`GET /oauth2/authorge` 未登录 302→login）。存储 memory（hermetic，零 DB/Redis；`wireIdentityServices` 无 DB client 时记警告返回，安全）。
    - **③ CTest 标签 `SdkSmoke`**：根 CMake 用 `add_test(NAME SdkSmoke.FullStack COMMAND ctest --build-and-test ...)` 注册（转发 Conan 工具链 + `-DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/authforge-cmake`，内层工程自带 `enable_testing()`+`add_test` 由外层 `--test-command ctest` 驱动），`set_tests_properties(... LABELS SdkSmoke)`——满足 M8 Task 39 对 SdkSmoke 的依赖。用户已定两分叉决策：memory 存储后端 + CTest `--build-and-test` 独立配置（真 find_package build-tree 消费）。`WITH_SOCIAL`/`WITH_WEBAUTHN` 是 `authforge::drogon` 的 PUBLIC compile-def，内层重编 ControllerRegistration.cc 经导出 target 自动继承（同 tests/ 机制），无需转发。
    - **关键坑（smoke 侧，已修）**：memory 后端 `MemoryClientRepository::initFromConfig` 读的 client 字段是 `type`（默认 CONFIDENTIAL）**非** `client_type`——config 误写 `client_type` 会使 vue-client 变 CONFIDENTIAL，`validateClient` 空 secret 失败返回 `Invalid client_id`；另 `/oauth2/authorize` 的 `state` 强制 8-512 字符。
    - **验收**：`cmake --preset windows-msvc` configure 干净（8 包全产出 build-tree Config/ConfigVersion/Targets，drogon Targets 导出名为规范 `authforge::drogon`，INTERFACE_LINK_LIBRARIES 全为 `authforge::common` 等规范名）；全栈 `cmake --build` 绿；`ctest -C Release -L SdkSmoke --output-on-failure` **SdkSmoke.FullStack 100% 通过**（内层 find_package build-tree 配置+构建+起 HTTP 四断言全绿）。

---

## M5 — 控制器拆分 + 身份域内聚（依赖：M3，可与 M4 并行）

- [x] 29. 拆分 `AdminController`（2914 行；`.h` 401 + `.cc` 2914）
  - → `UserAdminController`/`ClientAdminController`/`RoleScopeAdminController`/`TokenAdminController`/`AuditController`；业务下沉 application service
  - 产出：薄控制器；验收：Admin API ~52 测试全绿；Playwright E2E 全绿
  - **实地摸底（2026-07-11）+ 分阶段决策（已批准）**：
    - 30 路由、handler 行段已记录（`listClients`@326 … `dashboard`@2901）。`.cc` 顶部 ~295 行是 `AdminApiControllerDocs` 静态结构（OpenAPI `addEndpoint`），拆分时按域分发到各控制器。
    - **关键发现**：handler **直连 DB + 大量裸 SQL**（`drogon::app().getDbClient()->execSqlAsync("SELECT ... FROM oauth2_clients ...")`），不经 plugin/仓储，违反项目"禁裸 SQL"铁律 → 是预存技术债。
    - **路由→控制器映射**：Client=8（`/clients*`）、User=7（`/users*`）、RoleScope=8（`/roles*`+`/scopes*`）、Token=4（`/tokens*`）、Audit=1（`/logs`）。
    - **3 决策**：(1) **分阶段**——29a 机械拆分（原样搬，零行为变化，先做）；29b 业务下沉 + 裸 SQL→ORM Mapper（后做，按域分批）。(2) **dashboard（2 路由）→AuditController**、**oidc/keys（1 路由）→TokenAdminController**。(3) **OrgAdminController 本次跳过**——AdminController 无 `/api/admin/org*` 路由，组织管理在别处（Task 30 才迁），不是从本控制器拆出。
    - 29a 执行：按控制器逐个搬（Client→User→RoleScope→Token→Audit），每搬一个 checkpoint（编译通过），全部搬完 + 改 `ControllerRegistration` + 分发 OpenAPI docs 后跑全量 `manage.ps1 test-backend`。注意 `AuthorizationFilter` 字符串引用、AutoCreation=false 显式 registerController（§5.5）。
  - **29a 完成说明（2026-07-11）**：2914 行 `AdminController` 全部拆为 5 个资源控制器并**删除原 AdminController**（`[~]` = 29a 机械拆分完成，29b SQL/服务下沉待做）。产出：`ClientAdminController`(8 路由)、`UserAdminController`(7)、`RoleScopeAdminController`(8)、`TokenAdminController`(5，含 oidc/keys)、`AuditController`(3，logs+dashboard)。全程 verbatim 搬运（含裸 SQL、`respondError` helper、各控制器私有 `XxxControllerDocs` OpenAPI 静态结构）。两处注册点同步：`ControllerRegistration.cc`（apps/server bootstrap）+ `test/test_main.cc`（测试二进制有独立内联注册列表，与 bootstrap 分离——发现并修复：test_main 须逐个 registerController 否则 route-manifest golden 丢路由）。验收：`manage.ps1 build-backend -debug` 绿；`ctest -C Debug` **290/290 零回归**（route-manifest golden 因路由是 sorted set、全部重新注册后无变化）。`AuthorizationFilter` 字符串引用、AutoCreation=false 显式 registerController、whole-archive 不需要（§5.5）均符合预期。OrgAdminController 按决策跳过（AdminController 无 `/api/admin/org*` 路由，组织管理在别处，Task 30 迁）。
  - **29b 执行中（2026-07-20）**：裸 SQL→ORM Mapper + 业务下沉 application service，**按域逐个 commit**。架构决策（已批准）：admin controller 留在 `libs/drogon`（SDK 层），**服务类同层落地 `libs/drogon/src/admin/` + `include/authforge/drogon/admin/`**（避免 SDK→产品依赖环）。服务持 `Mapper<T>`+`Criteria`（db-operations.md 三件套），controller 退化为薄 HTTP 适配器（解析请求→调服务→服务直渲染最终 HttpResponse，错误经 `ErrorResponder`）。**29b 全工程裸 SQL 分布**：16 个 controller 共 101 处 `execSqlAsync`（不止 5 个 admin），其中 5 admin controller = 40 处（Client 12/RoleScope 10/Token 13/User 9/Audit 6）+ Organization 3。RoleScope + UserAdmin 含 JOIN，须按 db-operations.md 拆为多查询。
    - **29b batch 1 完成说明（2026-07-20，ClientAdminController）**：新增 `ClientManagementService`（`libs/drogon/src/admin/ClientManagementService.cc` + 头）——8 个 admin 路由的 DB 访问全部从 `db->execSqlAsync("SELECT/INSERT/UPDATE/DELETE ...")` 换成 `Mapper<Oauth2Clients>`/`Mapper<Oauth2ClientScopes>` + `Criteria`。`updateClientScopes` 的 delete-then-insert 事务语义保留（`Mapper<T>(transaction)` 可用——`Transaction : public DbClient`，`shared_ptr<Transaction>` 隐式转 `DbClientPtr`）。controller 从 762 行缩为纯薄适配器（仅 OpenAPI docs + 转发）。**关键坑（已记入 design §5.5 同类坑）**：在 `namespace authforge::drogon::admin` 内裸写 `using namespace drogon::orm;` 会被解析成 `authforge::drogon::orm`（最近的封闭 `drogon` 命名空间），须全限定 `using namespace ::drogon::orm;`。`Mapper::update` 的成功回调签名是 `CountCallback`（`const size_t`），非 `const T&`。`findOne` 未命中走异常路径（`NoRowsException` 是 `DrogonDbException` 子类），等价原 `affectedRows==0` 分支。验收：`manage.ps1 build-backend -debug` 绿；`ctest -C Debug`（build 根）**290/290 零回归**。**剩余批次**：batch 2 = TokenAdminController（13 处，含 oidc/keys）、batch 3 = OrganizationController（3 处）、batch 4 = RoleScopeAdminController（10 处，含 JOIN→拆分）、batch 5 = UserAdminController（9 处，含 JOIN→拆分）、batch 6 = AuditController（6 处）。
    - **29b batch 2-6 完成说明（2026-07-20，全部完成）**：5 admin controller + Organization 全部迁完。batch 2 TokenAdminController → `TokenManagementService`（listTokens 的动态 WHERE + COUNT + LIMIT/OFFSET 改 `Mapper.count()`+`paginate()`+`orderBy`+`findBy`；EXTRACT(EPOCH FROM NOW()) 移到 C++ `std::time`；revoke LIKE/cascade 用 `deleteBy`）；batch 3 OrganizationController（产品级）→ `OrganizationService`（INSERT...RETURNING 改 `Mapper::insert` 返回插入行）；batch 4 RoleScopeAdminController → `RoleScopeAdminService`（listRoles 的 JOIN+GROUP BY+COUNT(DISTINCT) 拆为 findBy roles + findBy user_roles(In) 内存聚合；delete built-in 用 `Criteria NotIn`）；batch 5 UserAdminController → `UserAdminService`（getUser 3 表 JOIN+json_agg 拆 user 查 + `fetchUserRoleNames` 两查询；assignUserRoles 的 INSERT...SELECT 改 resolve-via-findBy(In names) 再逐行 insert；locked_until 哨兵 9999999999 逐字保留）；batch 6 AuditController → `AuditService`（listLogs 改 `Mapper.paginate().orderBy().findBy`，**修复了 A-LOG-004 过滤器丢失缺陷**——原代码建 WHERE 子句后又跑无过滤查询；getDashboardStats 的 5 子查询复合语句改 5 串 `Mapper::count()`）。**batch 6 前置**：`audit_logs` 表无 ORM 模型（model.json 漏），经批准**重生成 ORM**：model.json 加 `audit_logs`、`drogon_ctl create model` 重生（既有 14 模型逐字节不变，仅新增 `AuditLogs.h/.cc`，`.h` 按 libs 布局移到 include/）。`Timestamp` 列是 `trantor::Date`，logsToday/failuresToday 用 `trantor::Date(epoch*1e6)` 比。**MSVC 嵌套 lambda 捕获坑**：嵌套 lambda 只能捕获直接外层 lambda 已捕获的变量——dashboard 的 5 层 count 链最内层用 `now` 须逐层把 `now` 加进每层捕获列表。验收：每批 `manage.ps1 build-backend -debug` 绿 + `ctest -C Debug`（build 根）**290/290 零回归**。**29b 全部完成**，Task 29 标 `[x]`。注：其余非 admin controller 的裸 SQL（UserSelfService 15 / GitHub 9 / PasswordReset 6 / Mfa 6 / EmailVerification 4 / WebAuthn 4 / DeviceAuth 2 / OAuth2Standard 2 / Session 1 / ClientRegistration 1 / Health 1）不在 Task 29 范围（29 只覆盖原 AdminController 拆出的 5 controller + Organization），留作后续独立技术债清理。

- [x] 30. Organization 管理归入 `apps/server/src/organization/`（产品级）
  - 产出：迁移后的组织管理；验收：多租户组织 CRUD 功能等价
  - **完成说明（2026-07-11）**：`OrganizationController` 从 `libs/drogon`（SDK，namespace `authforge::drogon::controllers`）迁到产品应用 `OAuth2Server/src/organization/`（namespace `organization`，产品级，design §5.4：组织 CRUD 是产品关注点、不属于可复用 SDK）。verbatim 搬运（3 路由 list/create/getBySlug + OpenAPI docs + `respondError` helper + 裸 SQL，与 Task 29b 同类债务留后）。依赖方向正确：产品 → SDK（OpenApiGenerator/ErrorResponder/AuditLogger 经 `authforge::drogon`/`oauth2` 链接）。两处注册点同步（`ControllerRegistration.cc` + `test_main.cc`，`::organization::OrganizationController`）。CMake：server 与 test 二进制都加 `src/organization/*.cc` GLOB + include 路径。验收：`manage.ps1 build-backend -debug` 绿；`ctest -C Debug` **290/290 零回归**；手动 curl `/api/admin/organizations*` 三路由均 401（filter 链活、路由已注册），`/health` 200。

- [x] 31. 社交/邮件/WebAuthn 条件编译（F9 依赖声明）
  - `with_social`/`with_identity`/`with_webauthn` option 控制可选编译单元；显式声明 WebAuthn 的加密/CBOR 依赖
  - 产出：CMake + conanfile option 化；验收：关闭时纯协议引擎依赖面缩小且可编译；开启 WebAuthn 时依赖完整可构建
  - **完成说明（2026-07-11）**：Conan 侧早已声明 `with_identity/with_social/with_webauthn` option + 条件拉 `libcbor/0.13.0`（M0 Task 1）；本任务补齐 **CMake 侧 + Conan→CMake 打通**。(1) `conanfile.py.generate()` 把 `with_*` 映射为 CMake 缓存变量 `WITH_IDENTITY/WITH_SOCIAL/WITH_WEBAUTHN`。(2) 顶层 `CMakeLists.txt` 在所有 `add_subdirectory` 前声明三个 `option(...ON)`（子目录的 `option()` 变 no-op，保留 standalone 配置能力）。(3) `libs/drogon`：`WITH_SOCIAL/WITH_WEBAUTHN` OFF 时从 GLOB 排除 Google/WeChat/GitHub + WebAuthn controller，并按需设 `PUBLIC` 编译定义（与既有 `#ifdef WITH_*` guard 一致）。(4) 注册点 `#ifdef` 守卫：`ControllerRegistration.cc`/`test_main.cc`（include + registerController）/`IdentityAssembly.cc`（include + 构造 social/webauthn 服务 + setter）。(5) `libs/identity/test`：OFF 时排除 `WebAuthnServiceTest.cc`/`SocialAuthServiceTest.cc`。**`WITH_IDENTITY` 声明为 option 但 OFF 暂非可用配置**——`authforge::identity` 被 storage-postgres/OAuth2Plugin/多 controller 深度消费，禁用需更广 wiring（记为 follow-up；本任务范围 social+webauthn，符合任务标题与"依赖面缩小且可编译"验收）。验收：默认（全 ON）`manage.ps1 build-backend -debug` 绿 + `ctest -C Debug` **290/290**；`-DWITH_SOCIAL=OFF -DWITH_WEBAUTHN=OFF`（既有 build/ reconfigure）**全量编译通过、零 link 错**（social/webauthn controller + 服务 + 测试全剔除，依赖面缩小）。

---

## M6 — CI/CD 重构 + 自动化护栏（依赖：M4）

- [x] 32. CI 重构为可复用 workflow（2026-07-28）
  - `ci.yml` + `_build-test.yml` + `_frontend.yml` + `_sdk-smoke.yml`；三阶段门禁（快速门/主门/发布门）；SDK 冒烟纳入发布门
  - 产出：重构后 workflows；验收：三平台矩阵全绿；SDK 冒烟纳入门禁
  - **完成说明（2026-07-28）**：3 个独立 `ci-{linux,windows,macos}.yml` 重构为 1 个编排入口 + 3 个 `workflow_call` 可复用 workflow，串行 fail-fast `needs` 链三阶段门禁。**快速门**：`static-checks`（source-only，无编译——arch-guard/naming_validator/manage-parity-check/OpenAPI 校验，从各平台上移去重）+ `frontend`（`_frontend.yml`，vitest 错误映射属性测试 Req 12.5，单跑一次）。**主门**：`build-test` 用 `strategy.matrix` × {linux,windows,macos} 复用 `_build-test.yml`——单一 job 体经 inputs 参数化（`platform/runs_on/cmake_preset/preset_dir/use_database/use_ci_config/run_named_test_gates/test_bin_subdir/test_exe/...`，input 名全下划线避免 `-` 被解析为减法），忠实合并三平台原逻辑：Linux 真实 DB（Postgres/Redis 由 `docker run` 门控替代 service containers 以支持矩阵条件化）+ Windows/macOS memory config；Req 12.7 具名门（error-standardization/performance/e2e/integration 共 30+ 条 `-r <name>`）由 `run_named_test_gates` 控制仅 Linux/Windows 跑，行为零变更。**发布门**：`sdk-smoke`（`_sdk-smoke.yml`，Linux only，`ctest -L SdkSmoke` 全栈 find_package 冒烟）。追加 `workflow_dispatch` + `concurrency.cancel-in-progress`。顺带修复原 `ci-linux.yml` 性能报告上传的陈旧路径（`build/tests` → `${preset_dir}/tests`）。本地校验：4 文件 YAML 全解析通过；交叉核对无连字符 input、`with:` 键全声明、必填 input 无缺、matrix 覆盖三平台、`uses`/`needs` 链正确。
  - **评审后续——测试命名规范化 + naming 门禁激活（2026-07-28）**：Task 32 评审发现 `naming_validator.sh` 空转（默认扫已不存在的 `OAuth2Backend/test`）且激活即暴露 130 个不合规 `DROGON_TEST` 名。已完成：① tests/ 下 30 个 .cc 的 130 个测试名按 `[Category]_[Priority]_[Module]_[Feature]_[Scenario]` 规范批量重命名（PropertyN token 下沉 Feature 槽，Priority 按家族统一：unit/error 与 integration/error ApplicationEndpoint→P0，auth/concurrency/initorder→P1，Scaffold/utils/validation→P2）；② `naming_validator.sh` 修为默认扫 `tests/` 且跨行感知（`grep -rzoP '(?m)^\s*DROGON_TEST\(\s*\K\w+'`，修复 clang-format 换行漏检 + 注释误报；输出带 `file:name` 前缀，合规过滤锚定冒号），`ci.yml` static-checks 显式传 `tests`；③ 同步 8 处引用：`_build-test.yml` 全部 `-r` 门名、CategoryB_TSanFindings.md 复现命令、mfa spec tasks/design 命名约定描述、sdk-refactor tasks.md L392、docs/design/pr6-review-fixes-plan.md（残留检查发现的第 8 处）。验证：旧名全仓 0 残留、validator 正例 PASS/负例 FAIL、Release 构建通过、`_build-test.yml` 30 个 `-r` 门名对二进制 `-l` 清单全部可达（无幽灵门）、全量 ctest 296/296 通过、两 workflow YAML 解析通过。**遗留标注**：CategoryC_ASanFindings.md 引用的 11 个 `Integration_Concurrency_1_8/1_9/1_10/1_11_*` 复现测试在代码中已不存在（重命名前即已移除），不做替换，仅在文档内加陈旧标注保留为历史记录。

- [x] 33. 实现 `tools/arch-guard`（2026-07-28）
  - 检查 Domain（common/oauth2/identity）禁 `#include <drogon/`（允许 jsoncpp）；oauth2↔identity 无互相 include；Domain 无 `drogon::orm`
  - 产出：arch-guard 脚本 + CI 集成；验收：违规时 CI 失败
  - **完成说明（2026-07-28）**：新增 `tools/arch-guard/arch_guard.py`（Python，跨平台单实现），扫描三个 Domain 库的生产代码（`include/`+`src/`，排除 `test/`/`testing/`——单测可合法 include `<drogon/drogon_test.h>`）。实现 C/C++ 注释与字符串剥离，避免注释中提及规则文字（如 `libs/common` 的 3 处）误报。三条规则 R1（无 `<drogon/`）/R2（oauth2↔identity 互不 include）/R3（无 `drogon::orm`）违规即 `exit 1`。已接入 `ci-linux.yml`（静态检查平台无关，与 naming_validator/parity-check 一致仅 Linux 跑，置于其前）。本地验证：全绿扫描 79 文件 `exit 0`；注入违规夹具三规则全命中 `exit 1`（file:line 精确、注释行不误报），已删除夹具。

- [x] 34. 实现 `tools/api-diff`
  - SDK 导出头 API 快照 diff，破坏性变更需版本升级
  - 产出：api-diff 工具 + 基线快照；验收：人为破坏 API 时 CI 告警
  - **完成说明（2026-07-28）**：`tools/api-diff/api_diff.py`（stdlib-only，与 arch-guard/migration-check 同构惯例）+ `api-baseline.txt` 基线（164 个导出头，v1.0.0）。快照 = 7 个 SDK 库 `libs/*/include` 头文件的"声明骨架"：状态机剥注释（字符串字面量保留——默认参数属 API）→ 反向扫描剥函数体（`{` 回溯至 `)` 判定为函数体，实现级修改不误报）→ 空白归一化。分级：新增头/新增声明行 = ADDITIVE（可 `--update-baseline` 直接确认）；删除头/删除或变更声明行 = BREAKING（同 major 下 `--update-baseline` 拒绝，需先升 major 或 `--force` 走评审提交）。附带三源版本一致性交叉检查（cmake/Version.cmake、根 CMakeLists、conanfile.py，漂移 exit 2）。已接入 ci.yml FAST gate（arch-guard→migration-check→**api-diff**→naming）。验收：正向 164 头零漂移 exit 0；负向四组全命中——注释+函数体内修改零误报（exit 0）、新增导出方法 ADDITIVE exit 1、修改公开签名 BREAKING exit 1 且同 major `--update-baseline` 被 REFUSED、conanfile 版本失谐 exit 2。实现中修复一处自伤 bug：基线元数据注释原用 `#` 前缀导致 `#include`/`#pragma` 骨架行被解析吞掉，改用 `//` 前缀（骨架行经注释剥离后不可能以 `//` 开头，零碰撞）。

- [x] 35. 迁移校验器 + 依赖 EOL 扫描
  - 迁移顺序/幂等/回滚检查；拦截 EOL 依赖（如 OpenSSL 1.1.1）
  - 产出：`tools/migration-check` + `security.yml`；验收：注入 EOL 依赖时 CI 失败
  - **完成说明（2026-07-28）**：**migration-check**——`tools/migration-check/migration_check.py`（与 arch-guard 同构：stdlib-only、argparse、退出码 0/1/2），5 条规则针对两条迁移执行路径（SchemaManager 运行时 + CI psql glob）实际依赖但都不校验的假设：M1 文件名严格 `V<三位零填充>__<snake_case>.sql`（不匹配会被 SchemaManager 静默跳过、破坏 CI 字典序 glob）；M2 版本唯一且连续（SchemaManager 对重复版本按文件系统顺序应用=非确定）；M3 幂等（CREATE TABLE/INDEX 须 IF NOT EXISTS、ADD COLUMN 须 IF NOT EXISTS、顶层 INSERT 须 ON CONFLICT、CREATE FUNCTION 须 OR REPLACE、ADD CONSTRAINT 须同文件 DROP CONSTRAINT IF EXISTS 配对）；M4 前向非破坏（禁 DROP TABLE/COLUMN/SCHEMA/DATABASE、TRUNCATE、顶层 DELETE）；M5 不可变性——SHA-256 基线 `baseline.json`（LF 归一化防 CRLF 平台差异；SchemaManager 只写 checksum 从不回读比对，此为补缺）；注释/字符串/美元引号函数体先剥离再匹配（V016 plpgsql 体内 INSERT/DELETE 不误伤）。**EOL 扫描**——`tools/security/dependency_eol_check.py` 解析 `conan.lock` requires+overrides（20 个锁定引用），对照内置安全下限策略（openssl≥3.0 EOL 线、zlib≥1.2.13 CVE-2022-37434、libcurl≥8.4.0 CVE-2023-38545），确定性离线不依赖外部 feed。**接线**——`security.yml`（push/PR + 每周一 cron 03:00 UTC 捕获时间性 EOL 翻转 + dispatch）双 job：dependency-eol + secret-hygiene（复用既有 `scripts/security-check.sh`）；migration-check 作为第 5 个 step 接入 `ci.yml` FAST gate static-checks。**验收实跑**：现有 22 个迁移 M1-M5 全过零误杀；负向注入 8 类违规（坏名/重复版本/断号/非幂等×2/破坏×2/篡改+删除已基线迁移）全部命中 exit 1；注入 openssl/1.1.1w + zlib/1.2.11 假 lock → exit 1 双命中（验收标准达成）；两 YAML safe_load 通过。

---

## M7 — 发布管线（依赖：M6）

- [x] 36. `release.yml`：多架构镜像 + SDK 产物
  - buildx amd64+arm64 镜像；SDK 库 + 头 + `authforge-*Config.cmake` 打包
  - SDK 集成文档写明 **whole-archive 链接 `authforge-drogon`**（F1/H5）、**插件注册方式**（H1）与 SDK 运行时契约（线程/ABI/异常/日志，F9）
  - 产出：release workflow + 集成文档；验收：tag 触发产出全部产物；第三方按文档集成可跑通
  - **完成说明（2026-07-28，决策：ghcr.io + 原生 ARM runner + SDK 包仅 Linux x64）**：新增 `.github/workflows/release.yml`——严格 SemVer tag（`v[0-9]+.[0-9]+.[0-9]+`，带后缀 tag 如 v1.0.0-skills-modernization 不触发）+ workflow_dispatch 干跑（构建全量、不推送不发 Release）。五 job：①version-check（tag == cmake/Version.cmake + api-diff 三源一致与 API 面冻结校验）；②sdk-package（复刻 _build-test.yml Linux 序列，`cmake --install` 出 8 静态库+头+lib/cmake configs 打 tar + sha256，并用 examples/full-stack-host 对**安装前缀**做 find_package 消费冒烟——比 SdkSmoke 的 build-tree 验证更贴近第三方真实路径；Conan cache key 加 runner.arch 维度防 amd64/arm64 互毁）；③images（backend/frontend/admin 三镜像 × amd64/arm64 矩阵，arm64 用 ubuntu-24.04-arm **原生构建**替代 buildx QEMU——容器内全量 C++ 编译在模拟下不可行；context/target 与 compose 一一对应，推 `<ver>-<arch>` 中间标签）；④manifest（imagetools 合并出 `<ver>` + `latest` 多架构 manifest，命名 `ghcr.io/lucaswang420/authforge-{backend,frontend,admin}`）；⑤github-release（gh release create 挂 SDK tarball）。集成文档 `docs/backend/sdk-integration-guide.md`：产物清单、ABI 前置警示（引契约 §2）、conan install + CMAKE_PREFIX_PATH 集成步骤、8 包/导出目标清单、whole-archive 口径按现状校准（OBJECT 库 + 反射注册当前不需要；消费方自建静态自注册封装才需，F1/H5/H1）、镜像使用、维护者发布流程；双语 README 文档表挂链。本地验收：release.yml YAML 解析通过；`cmake --install`（Windows build 树代跑）产出 8 库 + 8 套 {Config,ConfigVersion,Targets} + 174 头，布局与文档一致。
  - **验收缺口（如实记录）**：tag 触发的端到端实跑（GHCR 推送、arm64 原生构建、Release 附件）待首次真实打 tag 或 workflow_dispatch 干跑验证；分支尚未 push，workflow 未在 GitHub 侧执行过。

- [x] 37. Helm chart + 生产 Compose + 版本化迁移执行器（含 F10）
  - 迁移执行器 + 回滚策略 + 启动自检；secrets 仅经 env/secret store（禁落盘日志）
  - 保留 `PasswordHasher` legacy SHA-256 校验与 `needsRehash` 平滑升级；加迁移兼容测试
  - 产出：`deploy/helm` + 生产 Compose + 迁移器；验收：Helm 部署跑通；升级/回滚可用；legacy 密码可校验并按需 rehash
  - **完成说明（2026-07-30，决策：迁移执行器 = Helm hook Job 复用 SchemaManager；Docker Desktop K8s 实跑验收）**：**迁移执行器**——`authforge-server --migrate-only`（main.cc 解析参数；`MigrationRunner::runMigrateOnly` 用 `DbClient::newPgClient` 自建客户端同步跑 `SchemaManager::migrate`，真实退出码 0/1，连接重试靠 Job activeDeadlineSeconds 兜底）；应用启动侧 `OAUTH2_AUTO_MIGRATE=false` 时改走 **schema 自检**（`countPendingMigrations` to_regclass 探表，pending>0 仅 LOG_ERROR 不退出——滚动升级窗口老副本不 crashloop），`=true` 路径失败从静默继续改为 `LOG_FATAL + exit(1)`（半迁移 schema 带流量比 crashloop 更糟）。顺带修复 `SchemaManager::computeChecksum` 非 Windows 的 std::hash 占位——两平台统一复用 `OpenSslCryptoProvider::sha256Hex`（与旧 BCrypt 输出一致，已记录 checksum 不漂移）。**Helm chart**（`deploy/helm/authforge/`，16 资源）——迁移 = hook Job 三件套（hook-scoped ConfigMap/Secret weight -5 + Job weight 0，`before-hook-creation` 保留上次日志；hook events 条件化：内置 PG→`post-install,pre-upgrade`（pre-install 时 DB 尚不存在），外部 DB→`pre-install,pre-upgrade`）；config/secret 数据用共享 helper（`backendConfigData`/`backendSecretData`）双渲染防 hook 副本漂移；三必填密钥 `required` 强制、secrets 仅经 env/Secret；backend 双探针（/health/live//health/ready）+ config checksum 滚动注解；固定名 alias Service `oauth2-backend`（前端镜像 nginx.conf 硬编码上游，每 namespace 限一 release）；内置 PG/Redis 可切外部；Ingress 路径切分照搬 deploy/nginx。**回滚策略**：迁移前向兼容由 tools/migration-check M4 离线强制 → `helm rollback` 只回退应用版本，schema 永不降级。**生产 Compose**——三服务切 `ghcr.io/lucaswang420/authforge-*:${AUTHFORGE_VERSION:-latest}`（保留 build:）；AUTO_MIGRATE 默认 false + 新增 `migrate` one-shot 服务（`--profile migrate run --rm migrate`）；删镜像内已烧的 migrations/seed 挂载；资源限制 + prometheus 端口收敛 127.0.0.1。**F10**——`AuthServiceTest` 新增 2 端到端连续性测试（rehash 后二次登录走 PBKDF2 且哈希稳定不重复 rehash；密码错误不触发升级、legacy 哈希保持原样），连同既有 `LegacyHashVerifiesAndGetsUpgraded`/PasswordHasher 单测覆盖 F10 全链路；identity 85/85 通过。**验收实跑**（Docker Desktop K8s kind 集群，本地构建 1.0.0 镜像）：`helm install` 成功——hook Job 应用 22 迁移 Completed，全 pod Ready，port-forward `/health/ready` 返回 db+redis connected；`helm upgrade` pre-upgrade hook 幂等重跑（"Database is up to date"）；`helm rollback` 成功（REVISION 3 = Rollback to 1）；自检路径实证（backend 与 post-install hook 并发启动时仅 LOG_ERROR）。`helm lint` 0 failed；分支验证：缺密钥拒渲染 / 外部 DB→pre-install / 内置 DB→post-install。踩坑记录：Docker Desktop kind 集群经 registry-mirror 走 pull 路径取本地镜像，`pullPolicy: Never` 会 ErrImageNeverPull（values-local.yaml 用 IfNotPresent）；backend 镜像无 ENTRYPOINT，Job 须整条 `command` 而非裸 `args`。
  - **验收缺口（如实记录）**：Compose 侧 ghcr 拉取路径与 `--profile migrate` 实跑未验（镜像未推 ghcr，与 Task 36 同一缺口，待首次发布）；Helm 验收在单节点 kind demo 拓扑（内置 PG/Redis、无 Ingress controller、无 jwtKey Secret），外部 DB + pre-install hook 组合仅 template 级验证。


- [x] 38. SBOM + 镜像签名 + 自动 CHANGELOG
  - 产出：供应链安全产物 + conventional commits → CHANGELOG；验收：release 附带 SBOM 与签名
  - **完成说明（2026-07-30，决策：syft/anchore + cosign keyless + git-cliff）**：全部接入既有 `release.yml`，不新增 workflow。**签名**——manifest job 合并多架构 manifest 后，`sigstore/cosign-installer` + `cosign sign --yes` 对三镜像**按 digest**（tag 可变、digest 不可变）keyless 签名（GitHub OIDC 身份，`permissions: id-token: write`，记录 Rekor 透明日志，无私钥可泄露）；验签命令写入 Release notes 与 SDK 集成文档 §6。**SBOM**——syft（`anchore/sbom-action/download-syft`，钉 v1.19.0）产 4 份 SPDX JSON：三镜像（从已推送 manifest 的 linux/amd64 切片；GHCR 凭据复用 job 已有 docker login）+ 源码树（`dir:.` 排除 build/stage，覆盖 conan.lock 与两份前端 package-lock.json 的声明依赖面），全部挂 Release 附件。**CHANGELOG**——根目录新增 `cliff.toml`（conventional commits 分组映射 Keep-a-Changelog 节；`tag_pattern` 只认严格 SemVer tag，与 release.yml 触发规则一致，pre-b10/backup/* 等工具 tag 不切段；docs(spec)/chore(spec)/chore(release) 跳过）；github-release job 用 `orhun/git-cliff-action@v4 --latest --strip header` 生成当次 notes（checkout 改 fetch-depth 0）替换 `--generate-notes`，再追加验证指引（cosign verify + sha256sum -c）。**设计取舍**：CHANGELOG.md 保持人工策展，发布前可选 `git cliff --unreleased --tag vX.Y.Z --prepend CHANGELOG.md` 本地刷新——tag 触发的 workflow 不回推提交（tag ref 无分支上下文，绕过评审直推 main 不可取），流程已写入 SDK 集成文档 §6。本地验收：release.yml YAML 解析通过；git-cliff 2.13.1 实跑 `--unreleased --tag v1.0.0` 全历史正确分组（feat→Added/fix→Fixed 等，spec 类提交被跳过）；notes 追加段 heredoc 在 bash 下渲染验证（变量展开 + 代码围栏转义正确）。
  - **验收缺口（如实记录）**：签名/SBOM/notes 的端到端实跑依赖 tag 触发（GHCR 推送 + OIDC 仅在 GitHub Actions 环境可用），与 Task 36 同一缺口，待首次真实发布验证；cosign verify 的 certificate-identity 正则待首发后按实际 repo 路径核对。

---

## M8 — 目录/命名空间整体重组 + 非代码路径对齐（依赖：全部，放最后；原子操作，迁移前打 tag）

- [x] 39. 目录迁移到 `libs/apps/frontends/tests/examples`
  - 顶层重定位：`OAuth2Server`→`apps/server`、`OAuth2Plugin`→`libs/*`、`OAuth2Admin`→`frontends/admin`、`OAuth2Frontend`→`frontends/user`、`OAuth2Server/sql`→`apps/server/migrations`
  - 代码引用用 IDE 重构 + smart move 自动更新（**仅覆盖代码 import/include，不覆盖脚本/配置/文本路径，见 Task 42-44**）
  - 产出：最终目录结构
  - 验收（可度量，F7）：全量三平台编译通过 + 全部 CTest 标签（Unit/Contract/Integration/E2E/Security/Performance/SdkSmoke）通过 + arch-guard 通过
  - **完成说明（2026-07-28，专项计划 `DIRECTORY_RESTRUCTURE_PLAN.md` Phase 0-7 逐 commit 完成，`321c529`…`45f6681`）**：Phase 0 打 tag `pre-m8-directory-restructure` + ctest 基线 276/277（唯一失败 `Property4_3_1` pre-existing），每 phase 末打回退 tag；Phase 1 删 `OAuth2Plugin/` 内 ORM 模型死副本；Phase 1.5 两套 identity 仓储接口统一（方向 Y：new 接口 int64→int32 对齐 DB）；Phase 2 存储实现迁 `libs/storage-{postgres,redis,memory}`（新建 `libs/storage-redis`）；Phase 3 adapters/error/config/utils/observability/filters/validation 迁 `libs/drogon` + `libs/common`；Phase 4（最高风险）`OAuth2Plugin` 类迁 `libs/drogon/plugin/` + OBJECT→STATIC 合并 + 删 `OAuth2Plugin/` 目录（config 反射插件实例化经运行时 gate 验证存活，未触发回退）；Phase 5 `OAuth2Server`→`apps/server`（target/二进制改名 `authforge-server`；`sql/` 扁平化为 `migrations/`+`seed/`；config 归 `config/`）+ 前端→`frontends/{admin,user}` + `.codebuddy`/`.claude`/CI/docker/.vscode/tools/README 同步；Phase 6 §5.8 命名收尾；Phase 7 `test/`→顶层 `tests/`（79 文件 git mv；`e2e/`→`tests/e2e-backend/`；单二进制 `authforge-tests` + 目录分流，按层拆二进制推迟为遗留事项）+ 测试命名空间统一（`oauth2::test*`→`authforge::test*`）。**附带回归修复**：Phase 1.5d 给 `initAdminRoles` 加的 `isMember("admin_users")` gate 使「缺键时默认注入 admin→{admin,user}」分支变死代码，Phase 7 全量 ctest 暴露 `Integration_P0_Plugin_General_Works` 失败后已修（`OAuth2Plugin.cc` 两处恢复无条件调用）。
  - **验收核对（含如实偏差）**：全量 build 0 error / LNK4006=0 / C4244=0 / 0 warning（Windows MSVC 实跑）；ctest **276/277 与 Phase 0 基线完全吻合**；各风险 phase 末运行时 gate 全绿（`/health`、`/.well-known/openid-configuration`、`/login` 渲染 login.csp 全 200 + 日志 `OAuth2Plugin initialized with storage type: postgres` / `Controller/filter plugin dependencies wired`）。**偏差**：三平台编译本地仅验证 Windows，Linux/macOS 待 CI 实跑确认；CTest 标签 `SdkSmoke`（Task 28a 未做）与 arch-guard（Task 33 未做）尚不存在，无法纳入本次验收，留待各自任务补齐。

- [x] 40. 命名空间统一为 `authforge::{common,oauth2,identity,storage,drogon}`（完成，分 3 阶段提交 `e8c9d9d`+`2882358`+`b500ad3`。计划修正：identity-repo 接口移动推迟到 Task 39；EmailService 保留 `namespace oauth2`；filter 字符串实际在 controller 头不在 config；observability 端口解耦在 Stage 3 完成）
  - 用语义化重命名，避免手工替换遗漏（类/文件名的梳理见 Task 45 + design §5.8）
  - 产出：统一命名空间；验收：全量编译 + 测试全绿

- [x] 41. 版本重置 v1.0.0 + 文档更新
  - 统一根 CMake 版本；更新 README/CLAUDE.md/集成文档；补 SDK 运行时契约文档（线程/ABI/异常/日志/whole-archive/插件注册，F9/H1）；前端错误码共享源路径更新（L2）
  - 产出：v1.0.0 + 更新文档；验收：版本一致；集成/契约文档齐备
  - **现状（2026-07-28）**：`cmake/Version.cmake` 版本已是 **1.0.0**、根 `CMakeLists.txt` `project(... VERSION 1.0.0)`。剩余：项目名仍为 `oauth2-plugin-example`（待改 `authforge`）、README/CLAUDE 更新、SDK 运行时契约文档、前端错误码共享源路径。应在 Task 34 api-diff 建基线前收尾。
  - **完成说明（2026-07-28）**：①根 `project()` 改名 `authforge`（DESCRIPTION 同步），`cmake --preset windows-msvc` 重新 configure 通过，全仓 `oauth2-plugin-example` 零残留；②README 双语同步——标题改 AuthForge、徽章改 ci.yml+security.yml（旧三平台 workflow 已删）、compose 命令补 `-f deploy/docker/docker-compose.yml`、admin 端口 5174→8081、页脚 v6.0.0→v1.0.0；硬编码测试计数（37/17/123/111）与实际已严重漂移，一律移除数字只保留覆盖范围描述（防再腐化）；③CLAUDE.md/CODEBUDDY.md Storage Strategy 段落从旧 `IOAuth2Storage/CachedOAuth2Storage` 改写为 RepositoryBundle + 独立 identity 仓储的现实架构（对照 `OAuth2Plugin::initStorage()` 实码核实），CODEBUDDY Layer Separation 同步；④新建 `docs/backend/sdk-runtime-contract.md`（F9 五契约：线程/ABI/异常/日志/依赖 + H1 插件注册与 whole-archive 边界，with_webauthn 已对照 conanfile.py 核实）；⑤前端错误码共享源路径：`crossAppConsistency.property.test.ts` 动态导入候选 `OAuth2Admin/...`→`admin/...`（M8 后唯一实际断裂点），另 7 文件注释中 `OAuth2Frontend|OAuth2Admin`→`frontends/{user,admin}` 批量归一（git diff 核对非注释变更仅 3 条候选路径）；⑥admin `package.json`+lock 版本 0.0.0→1.0.0；⑦docs/backend 4 文件旧路径修复（ci-cd-guide 迁移 glob→`apps/server/migrations`、api-reference openapi 链接、testing-guide、docker-guide cd 命令）。验收：user 前端 vitest 15/15（含跨应用一致性 2 例）、admin 2/2、CMake configure 通过。历史性文档（PRD/、docs/design/pr6-review-fixes-plan.md、完成说明史实）中的旧名有意不改。

- [x] 42. scripts/ 路径与命令维护（评审补充点 1）
  - 经 `paths.env`（Task 5 已建）把最终路径值一次性切到新结构；审计 `manage.ps1/.sh`、`scripts/backend/{build,test,run-server,setup-database,validate-openapi}.{sh,bat}`、`scripts/{smoke-parity.ps1,security-check.sh,test-frontend-url-config.sh}`、`scripts/emoji_manager.py`(默认 `../OAuth2Plugin`) 残留硬编码
  - 产出：更新后脚本；验收：`tools/manage-parity-check.sh` 通过；`manage build/test/run-backend`、setup-database、smoke-parity 三平台实跑通过
  - **完成说明（2026-07-28，随目录重组 Phase 4/5 同 phase 完成）**：路径切换全部经 `paths.env` 单点落地——`OAUTH2_SERVER_DIR=apps/server`、`SERVER_BUILD_SUBDIR=apps/server`、`SERVER_BINARY_NAME=authforge-server`、前端两 key 重定基到 `frontends/{admin,user}`、`OAUTH2_PLUGIN_DIR`/`SQL_DIR` key 删除（消费者同步清理）、`SQL_MIGRATIONS_REL_DIR=migrations`（`sql/` 包装层扁平化）。`manage.ps1/.sh`、`scripts/backend/*`、`smoke-parity.*` 均经 4 个 loader 读取；grep 终扫确认脚本面零旧路径硬编码（仅剩 3 处叙述性注释：`env_common.sh`/`generate_models.bat`/`generate-models.sh` 记录 Phase 4 删目录史实，有意保留）。
  - **验收缺口（如实记录）**：Windows 侧 build/test 全流程经 paths.env 驱动实跑通过（重组各 phase 的 build + Phase 7 全量 ctest）；`tools/manage-parity-check.sh` 与 Linux/macOS 实跑未执行，待 CI/后续确认。

- [x] 43. docker / config 目录引用维护（评审补充点 2 + H1/H7）
  - 更新 `docker-compose*.yml`、各 `Dockerfile`、`.env.docker`、`.dockerignore` 的 build context / COPY 路径
  - 更新应用内路径：`config.json` `document_root`、迁移目录、`MigrationRunner` 相对路径探测（`../../OAuth2Server/sql/migrations`→`apps/server/migrations`）；**`config.*.json` 的 `plugins[].name`（OAuth2Plugin/PromExporter）随 H1 决策处理**
  - 产出：更新后 docker/config 与应用路径逻辑；验收：`docker compose up` 全栈健康检查通过；后端从新路径正确加载 config/迁移/插件
  - **进展（2026-07-28，路径对齐部分已随目录重组 Phase 5d 完成，实跑验收未做故不勾）**：`Dockerfile`/`docker-compose.yml`/`docker-compose.prod.yml` COPY/build context 已对齐新布局（容器内 `/app/sql/*` 布局有意不变）；`MigrationRunner` 相对路径探测已改为 `migrations` / `sql/migrations`（docker 布局）/ `../../apps/server/migrations` 等新值并在本机实跑验证（启动日志确认迁移目录命中）；4 份 `config.*.json` 迁至 `apps/server/config/`，`plugins[].name` 按 H1 方案 A 保留 `OAuth2Plugin` 类名零改动，config 反射插件实例化经运行时 gate 验证。**剩余**：`docker compose up` 全栈健康检查实跑验收 + docker 深度重做（重组计划明确列为范围外），完成后再勾选。
  - **补充发现（2026-07-28）**：`Dockerfile` 后端段仍 `git clone` 源码编译 Drogon + 裸 cmake（`deploy/docker/Dockerfile:21-44`），**未随本仓「全平台 Conan + cmake --preset」统一迁移**，与 CI 的构建路径分叉。docker 的 Conan 化 + preset 化 + `docker compose up` 实跑验收，见搁置的《Docker Conan Preset 迁移》计划，本任务收尾时一并处理。
  - **完成说明（2026-07-28，Docker 迁 Conan+preset）**：COPY 路径对齐已随目录重组各 phase 完成，本次收尾 Dockerfile 后端构建链迁移：① backend-builder 删除容器内源码编译 Drogon + 裸 cmake 段（原 `ARG DROGON_REPO/DROGON_VERSION`），改为直接 `RUN bash scripts/backend/build.sh Release`（单一构建路径：conan install + `cmake --preset linux-release` + drogon_ctl PATH 注入 + config 落位，与宿主/CI 零分叉），并加 BuildKit cache mount（`/root/.conan2`）使瞬时网络失败不再作废数小时的 `--build=missing` 依赖编译；② backend-base 改 pip 装 conan+cmake（apt cmake 3.22 读不了 Conan 生成的 CMakeUserPresets.json schema v4，需 ≥3.23）；③ backend-runtime 产物路径改 `build/linux-release/apps/server/authforge-server`，apt 运行库清单按镜像内 `ldd` 实测重算——Conan 全静态链接后仅剩 libstdc++/libm/libgcc/libc（基础镜像自带），11 项旧清单（libjsoncpp25/libpq5/libhiredis0.14 等）删除，只留 ca-certificates + curl；④ `docker-quick-verify-debug.sh` 同步 2 处陈旧引用（Drogon 头检查→conan、`build`→`build/linux-debug`）；⑤ 排坑：`.gitattributes` 补 `paths.env text eol=lf`（Windows CRLF checkout 使容器内 `source paths.env` 报 `$'\r': command not found`）。compose 文件核实无 DROGON 参数引用，无需改。验收实跑：镜像构建成功；`ldd /app/authforge-server` 无 missing；`docker compose up oauth2-backend`（含 postgres/redis 依赖）后 `/health` 200（database connected）+ `/health/ready` 200。**全栈补充验收（同日）**：`docker compose up -d --build` 六容器全起（backend/frontend/admin/postgres/redis/prometheus），frontend :8080 → 200、admin :8081 → 302（nginx 正常跳转）、backend `/health` 200，前端镜像 `frontend-runtime` 阶段零改动直接构建通过。范围外遗留：drogon 配方默认 `with_boost=True` 拖入 boost 源码编译，C++17 下可评估 `drogon/*:with_boost=False` 瘦身（改动全平台依赖图，需单独验证）。

- [x] 44. .claude / .agent / .vscode 配置与技能维护（评审补充点 3）
  - 更新 `.claude/skills/*/SKILL.md`、`.claude/agents/*.md`、`.claude/MEMORY.md`、`.agent/workflows/*.md`(build/test/start/stop/db-reset)、`.vscode/*`、`.hooks/config.json`、`CLAUDE.md` 的 Repository Layout
  - 产出：更新后 agent/IDE 配置；验收：抽样执行各 workflow/skill 命令可正确运行；MEMORY/CLAUDE 描述与新结构一致
  - **完成说明（2026-07-28，随目录重组 Phase 5c/5d + Phase 7 完成）**：`.codebuddy` + `.claude` 双侧同步——rules paths 触发器、4 份 settings 的 pre-commit hook（`cd build/apps/server && ctest`）、agents×6 路径表映射到 libs/ 实际布局、skills×16、`CODEBUDDY.md`/`CLAUDE.md` Repository Layout；Phase 7 追加同步 settings×2/agents×2/skills×2/CLAUDE/CODEBUDDY 的测试路径（`tests/`、`authforge-tests`）。`.vscode/*` 在 Phase 5d 对齐。grep 核实 `.hooks/config.json` 与 `.claude/MEMORY.md` 无旧路径残留。**范围偏差（如实记录）**：任务文本中的 `.agent/workflows/*.md` 目录在当前仓库已不存在（早于重组被移除），无对象可更新；验收「抽样执行」以 pre-commit hook 随各 phase commit 实跑 + build/test 全流程实跑覆盖。

- [x] 45. 类/文件命名一致性终检（评审建议 3，design §5.8）—— **B10 完成（commit `2d60d1e` 45.1 + `69c51f1` 45.2）**
  - 按 §5.8 约定核对全部对外类/文件/头命名，修剩余歧义名：`OAuth2StandardController`→`AuthorizationEndpointController`/`TokenEndpointController`/`DiscoveryController`（按职责拆/更名）、澄清 `OAuth2Controller`、`OAuth2Plugin` 仅保留装配器语义
  - 确保「文件名 = 主类名」；rename-on-move 未覆盖的残余用语义化重命名统一
  - **必须在 v1.0.0 冻结（Task 41）前完成**，之后 api-diff 基线（Task 34）才建立在稳定命名上
  - 产出：一致命名 + 遗留死文件清理（含 §5.9 的 `consent.csp` 确认后删除）
  - 验收：全量编译 + 全标签测试全绿；无遗留歧义名/死文件
  - **45.1 完成说明（`2d60d1e`，接入引擎——消除"引擎 authorize 路径在产品里从未装配"死代码债）**：① 新增 `StorageSubjectResolver`（`OAuth2Plugin/include/oauth2/adapters/`，`authforge::drogon::adapters`）实现 `ISubjectResolver::resolve(Subject, cb)`——镜像 `StorageRoleProvider` 写法，经 `SubjectGenerator::parse` 拆 `provider:localId` → `ISubjectMappingRepository::getInternalUserId`，补齐引擎 Tier 2/3 所需的 subject→internalUserId 解析。② `OAuth2Plugin` 装配 `subjectResolver_` + `authorizationService_`（构造参数 `clientRepo_`/`consentRepo_`/`subjectResolver_`/`roleProvider_`）+ accessor。③ `OAuth2StandardController::authorize` 删除内联三层链（`validateClientScopes`→`validateUserRolesForScopes`→`getInternalUserId`→`checkUserConsentAndProceed` 递归）+ 删除 `checkUserConsentAndProceed` helper，替换为单次 `evaluateScopes` 调用，`ScopeValidationSummary` 三分支映射（`hasErrors()`→400 invalid_scope/access_denied；`needsConsent()`→302 /consent；`canProceed()`→generateAuthorizationCode→302 code+state）。未登录分支保留。验证：build 0 错；`ctest` 290/290；运行时 authorize 302/400 行为等价。
  - **45.2 完成说明（`69c51f1`，纯文件重组——零行为变化）**：1765 行 `OAuth2StandardController` 拆为 3 个 `HttpController<T,false>`：`AuthorizationEndpointController`（authorize 1 路由）/`TokenEndpointController`（token/introspect/revoke/userInfo 4 路由 + `createSuccessResponse`/`extractClientCredentials` helper）/`DiscoveryController`（metadata/oidcDiscovery/jwks 3 路由）。各 controller 自有 `plugin_`/`setPlugin()`/`resolvePlugin()`/`initApiDocs()`/`initApiDocsImpl()`（HealthController/SessionController 先例）。删除 `OAuth2StandardController.{h,cc}`。8 处引用点同步：`ControllerRegistration.cc`/`test_main.cc`/`main.cc`（include+registerController+setPlugin+initApiDocs）、`CategoryA_InitOrderSnapshotTest.cc`（expectedEndpoints）、`OAuth2FlowE2ETest.cc`（4 处 make_shared）。验证：build 0 错；`ctest` 276/277（route-manifest golden 不破——keys METHOD+path 不 key 类名；唯一失败的 `Property4_3_1` fingerprint 为 **pre-existing**，git diff 核实 golden 零改动、8 路由 METHOD+path 全保留）；运行时 8 路由全活（authorize 302；token/introspect/revoke/userinfo 400/401；3 discovery 200）。

---

## 遗留事项（backlog，非阻塞；目录重组 Phase 0-7 收官时盘点，2026-07-28）

- [ ] L1. 测试二进制按层拆分：`tests/` 当前为单二进制 `authforge-tests` + 目录分流（Phase 7 用户决策，ctest 名 `OAuth2Tests` 与 50 个 `Contract.` add_test 名单保留保持基线可比）；按 design §10 测试矩阵拆为分层二进制（unit/contract/integration/e2e-backend/security/performance）为独立后续任务
- [x] L2. `tests/services/AuthServiceGetUserInfoTest.cc` 死代码处置：不在任何 CMake GLOB 内（`services/` 不在 GLOB 目录列表），迁移前后均未被编译——修复纳编或删除，二选一
  - **完成说明（2026-07-28）**：覆盖核对发现死文件测的旧 `authforge::drogon::services::AuthService::getUserInfo` 生产中已零调用方（userinfo 端点实际链路为 `TokenEndpointController → OAuth2Plugin::getUserInfo → authforge::identity::IUserRepository`），且其 `sub == 数字id` 断言与 V007 public_sub UUID 模型冲突，原样纳编 = 测死代码 + 必然失败。真实缺口（roles 聚合、name 回退 username→email、无角色空数组、not-found nullopt）在 `IUserRepository::getUserInfoWithRoles` 的 Postgres/Memory 两实现上零测试。处置：新建 `tests/contract/UserInfoRepositoryContractTest.cc`（Postgres ×3 + Memory ×2，含后端差异文档化：Memory 永不返回 nullopt）重安置覆盖到活路径，登记进 `OAUTH2_CONTRACT_TEST_NAMES`（Contract add_test 45→50），删除死文件。验收：新用例 5/5 通过；全量 ctest 295 项仅既有 Property4_3_1（L3）失败，单二进制基线 276/277 → 281/282，零回归。identity 层既有 nullopt/email 覆盖（AuthServiceTest ×2、UserInfoProviderTest ×3 gtest）保持不变；`/oauth2/userinfo` HTTP 200 快乐路径集成测试仍缺（当前仅 401 负例 + 文档/路由存在性断言），不在本项范围内。
- [x] L3. `Integration_P1_OpenApiSpec_Property4_3_1_PathMethodFingerprint_Baseline` pre-existing 失败根因分析（276/277 基线中唯一允许失败项，Task 45.2 时已核实与拆分无关）
  - **完成说明（2026-07-28）**：根因 = commit `9796672`（2026-07-24，标题 "chore: clang-format + include cleanup"，声称仅格式化）实际重写了 `ClientRegistrationController.cc` 的 OpenAPI docs 注册块：`path` 从 `/oauth2/register` 改成 `/api/oauth2/register`、`requiresAuth` 从 `true` 改成 `false`，与真实路由（header `ADD_METHOD_TO "/oauth2/register"` + `AuthorizationFilter`）及 golden route_manifest 双重漂移——指纹基线测试正确地抓住了文档漂移，非测试本身问题。修复：docs 块 path/requiresAuth 改回与路由一致（纯文档输出修正，零行为变更）。验收：Property4_3_1 首次转绿；全量 ctest **295/295 全绿**（单二进制 282/282，基线首次无允许失败项）。
- [x] L4. `scripts/backend/full_test.bat` 全流程（编译→全测试）适配目录重组（Task 42 脚本枚举未覆盖的 full_test 编排链）
  - **完成说明（2026-07-28）**：full_test 编排链（`full_test.bat` + 其调用的 `build.bat`/`test.bat` + `paths.env`）适配 Phase 5 布局——① `paths.env` 新增 `TESTS_BUILD_SUBDIR=tests`（测试二进制自 `OAuth2Server/test` 迁至顶层 `tests/`，build 下为 `build/tests`，运行期 config 被 `tests/CMakeLists` POST_BUILD 扁平化为 `config.json`）；② `build.bat` config 拷贝落点改 `apps/server`→exe 旁 + `tests` build 目录（扁平 `config.json`）；③ `test.bat` `TEST_WORK_DIR` 改用 `TESTS_BUILD_SUBDIR`、`TEST_CONFIG` 指向扁平 `config.json`；④ `full_test.bat` Step 5 服务器改从构建目录 `build/apps/server/Release` 启动并去掉 `-c` flag（对齐 `run_server.bat` 约定：`main.cc` 无 `-c`，探测 `./config.json` 相对 CWD，config 已被 build.bat 拷到 exe 旁；旧 CWD `apps/server` 根级已无 config.json）。**关键发现修复**：cmd 的 `copy` 对 paths.env 正斜杠值（`apps/server`、`config/config.json`）静默拷 0 文件并留 errorlevel 1，build.bat/test.bat 加 `set "VAR=%VAR:/=\%"` 归一化为反斜杠 + errorlevel 检查。
  - **验收核对（含如实偏差）**：Windows pwsh 实跑 full_test.bat，Steps 1-6 全绿——数据库初始化、ORM 模型生成、Release 全量构建（0 error）、单元/集成测试双 config（默认 + CI config）跑 **ctest 295/295**、服务器从构建目录成功启动、OAuth2 端点测试通过。**偏差**：Step 7 Admin 端点测试 24/51 失败（Test 12+ 全 401），经诊断为**既有测试脚本自撤销级联缺陷**——`test-admin-endpoints.ps1` Test 11b `DELETE /api/admin/tokens/:tokenPrefix` 按 prefix 撤销 token 时波及 admin 自身会话 token，导致后续测试全部失去鉴权；`git status` 确认本次仅改脚本/paths.env（未改任何 admin/auth 应用代码，唯一 controller 改动 `ClientRegistrationController.cc` 是 L3 的 OpenAPI docs 元数据修正，与鉴权无关），故该失败与目录重组适配无关，归为既有测试脚本缺陷，留待测试脚本专项修复。Linux/macOS 实跑未执行（同 L1/Task 42 缺口），待 CI 确认。
- 已在各任务内记录的其他 follow-up（不重复建项，指向原记录）：`WITH_IDENTITY=OFF` 暂非可用配置（Task 31）；非 admin controller 残余裸 SQL 清理 ≈51 处（Task 29b 末尾清单）；~~Task 43 的 `docker compose up` 实跑验收~~（✅ 2026-07-28 已完成，见 Task 43 完成说明）；Task 42 的 parity-check + Linux/macOS 实跑。
- [x] L5. 评审问题点 P0 修复（`评审问题点有效性分析报告.md` #1 PKCE/nonce 丢弃 + #2 client_credentials scope 不校验）
  - **完成说明（2026-07-28）**：**P0#1**——`AuthorizationEndpointController::authorize` 原来只读 response_type/client_id/redirect_uri/scope/state，`code_challenge`/`code_challenge_method`/`nonce` 在三个分支全部静默丢弃；现提取三参数并透传：未登录→/login 302 追加、needsConsent→/consent 302 追加（仅非空时）、直发分支传入 `generateAuthorizationCode` 真实值（替代原来的三个 `""`）；直发前补 `require_pkce_for_public` 强制（public client 无 challenge → invalid_request，与 login/consent 路径对齐）。配套链路：`SessionController::showLoginPage` 提取 nonce 进模板数据 → `login.csp` 新增 nonce 隐藏字段 → `ConsentPage.vue` 从 route.query 读 PKCE 三参数并回传 consent 请求。**测试暴露的更深层根因**：`PostgresGrantRepository::saveAuthCode` 从未持久化 code_challenge/code_challenge_method（`getAuthCode` 读回同漏，仅 `consumeAuthCode` 的原生 SQL 正确）——即 Postgres 模式下**所有路径**（含 login/consent）的 PKCE 均被 TokenService 的条件校验（存储 challenge 为空即跳过，RFC 7636 §4.4）静默绕过；已补 saveAuthCode 写入（空→NULL）+ getAuthCode 读回。**P0#2**——`TokenEndpointController` client_credentials 分支原样回显请求 scope（省略时硬编码 "read"）；现以 `Client` 聚合 `allowsAllScopes` 校验注册白名单（RFC 6749 §3.3）：越权（含部分越权，不静默缩窄）→ 400 `invalid_scope`；省略 scope → 授予注册 scope 全集；注册集为空 → `invalid_scope`。seed `dev_backend_client.sql` 补 backend-svc 的 read/write 授权（幂等）。新增集成测试 ×2：`tests/integration/auth/AuthorizePkcePassthroughIntegrationTest.cc`（真实 HTTP 直发路径：seed consent + login 拿 session → authorize 302 直达 redirect_uri 带 code → 无 verifier 交换必须 invalid_grant → 带 verifier 交换成功）、`tests/integration/token/ClientCredentialsScopeValidationTest.cc`（越权/部分越权/合法/省略四段断言，fixture 幂等自给）。
  - **验收核对（含如实残留）**：全量 build 0 error；ctest **295/295 全绿**（两个新用例在单二进制 `OAuth2Tests` 内，用例数 282→284；PKCE 测试曾以 Round A「无 verifier 交换返回 200」精确复现漏洞，saveAuthCode 修复后转绿）。**残留缺口（如实声明，超 P0 范围）**：`oauth2_codes` 表无 nonce 列（V002），整个 libs/storage-postgres 无 nonce 持久化——nonce 现已在 HTTP 层全路径透传但不落库，属 login/consent/直发全路径共有的既有 schema 限制，OIDC nonce 防重放（Core §3.1.3.7）需后续加列 + TokenService id_token 写入 nonce claim 才闭环。

---

## 剩余任务优先级顺序（2026-07-28 盘点，按依赖 + 价值重排）

> 全平台构建基座已统一为 **Conan + `cmake --preset`**（`build/<preset>` 目录约定 + 各平台 debug preset；4 个 `.bat` 的 `shift`/`%~dp0` 回归已修）；CI 已由 3 个独立文件重构为可复用 workflow（Task 32 ✅）。版本号已是 1.0.0。以下为**未完成任务（`[ ]`）**的推进顺序。

**Wave A — 立即可做，护栏 + SDK 地基（无前置阻塞，锁定既有重构成果）**
1. ~~**Task 33 arch-guard**~~ ✅ **已完成（2026-07-28）**：`tools/arch-guard/arch_guard.py` 强制 Domain 禁 `#include <drogon/`、oauth2↔identity 零互依、Domain 无 `drogon::orm`，已接入 `ci-linux.yml`（违规即 CI 失败）。解锁 Task 32 门禁与 Task 39 验收（arch-guard）。
2. ~~**Task 28a SDK build-tree smoke + 打包地基**~~ ✅ **已完成（2026-07-28）**：各 SDK 包补齐 `Config.cmake.in`/`find_dependency`/build-tree `export()`；`examples/third-party-host` 全栈 build-tree smoke 落地，CTest 标签 `SdkSmoke` 已产出并纳入 CI 发布门（详见 Task 28a 完成说明）。

**Wave B — CI / 构建基础设施**
3. ~~**Task 32 CI 重构**~~ ✅ **已完成（2026-07-28）**（依赖 28a + 33）：三独立 workflow → 可复用 `ci.yml` + `_build-test.yml` + `_frontend.yml` + `_sdk-smoke.yml`；三阶段串行 fail-fast 门禁（快速门/主门矩阵/发布门）；`SdkSmoke` + arch-guard 已纳入门禁。
4. ~~**Task 43 docker 迁 Conan+preset + compose 实跑验收**~~ ✅ **已完成（2026-07-28）**：Dockerfile 后端段改 `build.sh Release`（Conan+preset 单一构建路径）+ Conan cache mount；runtime apt 清单按 ldd 实测重算（静态链接后仅剩 ca-certificates+curl）；`docker compose up` 后端 `/health`+`/health/ready` 双 200 实跑通过（详见 Task 43 完成说明）。
5. ~~**Task 35 migration-check + security.yml + 依赖 EOL 扫描**~~ ✅ **已完成（2026-07-28）**：`tools/migration-check`（5 规则：命名/连续/幂等/非破坏/checksum 基线）接入 ci.yml FAST gate；`security.yml`（EOL 扫描 + secret 卫生，周一 cron）；负向注入验收全过（详见 Task 35 完成说明）。M6 全部完成，解锁 Wave D。

**Wave C — 版本冻结 + API 稳定**
6. ~~**Task 41 v1.0.0 收尾 + 文档**（版本号已 1.0.0；剩项目名 `oauth2-plugin-example`→`authforge`、README/CLAUDE、SDK 运行时契约文档、前端错误码共享源路径）。应在 api-diff 建基线前完成。~~ ✅ 完成
7. ~~**Task 34 api-diff**（依赖命名冻结 Task 45 已完成 + Task 41 版本冻结）：SDK 导出头 API 快照 diff + 基线。~~ ✅ 完成

**Wave D — 发布管线（依赖 M6 = Task 32-35）**
8. ~~**Task 36 release.yml**（多架构镜像 + SDK 产物打包，依赖 28a 打包地基）。~~ ✅ 完成（tag 实跑验收待首次发布）
9. ~~**Task 37 Helm + 生产 Compose + 版本化迁移执行器**（含 F10 legacy 密码平滑升级）。~~ ✅ 完成（Docker Desktop K8s 实跑验收通过）
10. ~~**Task 38 SBOM + 镜像签名 + 自动 CHANGELOG**。~~ ✅ 完成（端到端实跑待首次发布，与 Task 36 同缺口）

**Backlog（非阻塞，随时可做）**
- **L1** 测试二进制按层拆分（当前单二进制 `authforge-tests` / ctest 名 `OAuth2Tests`）。
- **Task 29b 尾** 非 admin controller 残余裸 SQL ≈13-14 处（UserSelfService 6 / PasswordReset 4 / EmailVerification 1 / GitHub 1 / TokenEndpoint 1；HealthController 探活可豁免）。
- **WITH_IDENTITY=OFF** 暂非可用配置（Task 31 follow-up）。
- **Task 42** parity-check + Linux/macOS 实跑（待 CI 确认）。

**关键路径**：`{33, 28a}` → `32` → `{36}`；`41` → `34`。Wave A 两项无前置、且分别解锁 CI 门禁与发布打包，建议最先做。

---

## Notes

- **立即启动 M0 + M1**：M0 的 Task1/2 是编译与 ORM 生成的 gate、Task5 建 `paths.env` 减少后续迁移散点；M1 拆存储接口 + 缓存装饰器再架构是解锁解耦的钥匙。
- **M2a 是真正攻坚点**：去 `drogon::utils`（Task 14）远大于 OpenSSL 迁移，须逐端口逐调用点小步提交保证中间态可编译；独立 PR、最多评审。
- **H1 插件注册是最早的失败点**：早于 F1 的路由 404——config 按类名反射加载，改名/迁库前必须先定 §5.7 方案，Task 21/22 验收含「config 驱动插件实例化成功」。
- **F1/H5 链接陷阱贯穿 M3/M4/M7**：whole-archive 覆盖 controller/filter/视图/插件符号，以 SDK 冒烟 HTTP 断言持续回归。
- **路径引用时序（H2）**：`paths.env`（M0）集中定义 → M2a/M3 构建产物路径变化时同步 → M8 顶层改名最终对齐；**CI 覆盖路径靠 CI 全绿拦截，本地/agent 路径须主动改**。
- **可发布性（H6）**：见 design §14.1；除 M2a 过程中与 M8 原子切换外，各里程碑停点均可交付；M8 迁移前打 tag，整体成功或整体回退。
