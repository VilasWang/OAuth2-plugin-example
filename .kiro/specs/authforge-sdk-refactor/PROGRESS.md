# authforge-sdk-refactor 执行进度记录

> 本文件用于在上下文压缩/session 中断后快速恢复状态。每个里程碑/Task 完成后更新。

## 分支与 PR

- 分支：`feat/m0-conan-migration`
- PR：#11（GitHub）
- **本地已提交但尚未推送**的 commit 从 `0c0ec9d`（Task 13）到 `d435c88`（Task 17 slice 12 + 清理）
- **用户指示（加快节奏）**："每个task完成后，编译通过即可，几个相关的task完成后可以一起测" —— 后续 slice 采用编译验证+攒批测试模式，减少单 slice 全量 ctest 频率
- 上次推送到远端的 commit：`e5c097d`（M1 saveTokenPair 修复）
- 用户指示："CI 不急，按计划往后推进" —— 暂不推送，先把 M2a/M2b 等后续任务在本地做完再统一验证推送

## 里程碑完成状态

- [x] M0（Task 1-6）：Conan 迁移、drogon_ctl、OpenSSL 3.5、CMakePresets、paths.env、三平台 CI — 已推送，CI 全绿
- [x] M1（Task 7-12）：仓储接口拆分、缓存装饰器、契约测试套件 — 已推送，CI 全绿（含 3 个真实生产缺陷修复：Redis validateClient 崩溃、hasUserConsent 逻辑反、saveTokenPair 竞态）
- [x] M2a（Task 13-16）：libs/common + 端口 + 去 drogon::utils + 测试链接过渡 — **本地完成，未推送**
- [x] M2b（Task 17-18）：libs/oauth2 + ORM 归位 — **本地完成**（Task 17 AuthorizationService/TokenService 主体逻辑仍留在 OAuth2Plugin，具体仓储实现未迁移命名空间——这是已知的、可接受的范围边界，不阻塞 M3；Task 18 ORM 模型 + 调用点已全部迁移并提交）
- [x] M2.5（Task 19，**范围受限**）：libs/identity — 2026-07 继续执行时发现此前提交（`e4eea5e`）虽然创建了目录结构，但 `AuthService.cc`/`MfaService.cc`/`SessionManager.cc`/`WebAuthnService.cc` 等全部是空壳占位（`TODO` 注释 + `callback(std::nullopt)`），且 `IRoleProvider`/`ISubjectResolver`/`IUserInfoProvider` 是与 `common::ports` 同名但签名不同的重复声明，违反设计 §5.2 方案 A（端口应下沉到 common，identity 应实现该端口而非自建竞争接口）。
  - **用户明确约束范围**：仅完成 AuthService 真实迁移 + RBAC/subject 绑定，其余控制器（MFA/WebAuthn/Social/Session）保持现状不动，留给未来独立任务。
  - 已完成：删除重复端口声明；`AuthService.cc` 真实实现（validateUser/registerUser/getUserInfo，语义对齐 `OAuth2Server/AuthService.cc`，含 PBKDF2 哈希格式字节级兼容、legacy hash 校验后升级、账户锁定进度退避）；`RoleProvider`/`SubjectResolver`/`UserInfoProvider` 三个薄适配器（真实实现 `common::ports` 接口，非占位）；新增 `libs/storage-postgres/PostgresIdentityRepository`（Adapter 层，实现 identity 的三个仓储接口）；19 个新 gtest 单测。
  - **仍是占位/未做**（如实记录，不夸大）：MFA/WebAuthn/Social（Google/WeChat/GitHub）/Session 五个服务的 `.cc` 仍是 TODO 占位；`PostgresIdentityRepository` 是全新实现，与 `OAuth2Plugin` 现有的 `oauth2::Postgres{User,Role,SubjectMapping}Repository` 是两套并行实现（故意不统一，统一需要迁移 `IdentityService` 的调用方，超出本次范围）；新 `AuthService`/`RoleProvider` 等尚未被任何生产代码（`OAuth2Server` controllers）实际调用——它们目前只是「libs/identity 内可独立编译验证」的构件，产品装配（把它们接入 `SessionController` 等）是 M3 Task 24 的工作。
  - 验收核对：✅ 独立编译；✅ 不依赖 `libs/oauth2`（grep 确认零 include）；✅ identity 单测通过（19/19）。
- [ ] M3（Task 20-26）：**进行中**，见下

## M2a 详细完成内容（Task 13-16）

### Task 13：libs/common
- `Result<T,E>`、框架无关 ErrorCatalog/Error（从 oauth2::error 移植，25 条目 + 13 个 OAuth2 协议码，逐字节保留）
- 7 个值对象：Subject/Scope/ClientId/RedirectUri/PkceChallenge/TokenValue/TenantId
- 9 个端口接口（全部异步回调风格，与现有仓储一致）：ISubjectResolver/IRoleProvider/IUserInfoProvider/IClock/ICryptoProvider/IUuidGenerator/ILogger/IEmailSender/IMetrics
- AuditEvent 观测模型
- 40 个 gtest 单测
- 零 Drogon 依赖已验证（grep 确认）

### Task 14：去 drogon::utils（8 个切片，逐步小步提交）
- 切片1：OpenSslCryptoProvider/OpenSslUuidGenerator/SystemClock Adapter 实现（放 OAuth2Plugin/{include,src}/adapters/，因为 libs/drogon 还不存在）+ 19 个交叉验证测试
- 切片2：RequestId.cc/AuditLogger.cc 的 IUuidGenerator 调用点迁移
- 切片3：CryptoUtils.h（14 处 include 的核心头文件）彻底去 Drogon，公开 API 不变
- 切片4：PasswordHasher.cc/TotpUtils.cc 的 secureRandomBytes/getSha256 迁移
- 切片5：JwkManager::base64UrlEncode 迁移（RS256 签名逻辑本身已是纯 OpenSSL，未改）
- 切片6：TokenService::generateSha256Hash 迁移（**发现但未修复的严重 PKCE RFC7636 不合规缺陷**，见下）
- 切片7：新增 DrogonLogger（ILogger 的 Drogon 实现）+ JwkManager/TokenService 的 LOG_* 迁移，JwkManager.cc 现在零 Drogon 依赖
- 切片8：AuditLogger.cc 的 4 处 LOG_* 迁移（DB/HTTP 依赖本身保留，只迁移日志调用）
- **未处理**（合理超出范围）：storage/controllers/filters 里约 400 处 LOG_*，这些是 Adapter 层代码（M3 才处理）

**发现的 pre-existing 缺陷**：
1. `oauth2::utils::sha256()` (CryptoUtils.h) 十六进制大小写处理错误 —— 确认为死代码，零生产调用点，**未修复**（不在任何任务范围内，无生产影响）
2. **`TokenService::generateSha256Hash` 不符合 RFC 7636**——对十六进制字符串本身做 base64 编码，而非对原始摘要字节编码。**已在 Task 17 slice 4 修复**（commit `70aba78`），见下方"Task 17 slice 4"小节。

### Task 15：假实现（测试支持库）
- 新建 `libs/common/testing`（authforge::common::testing），与 libs/common（仅端口）和 OAuth2Plugin 生产 Adapter 三方分离
- 9 个 Fake：FakeClock/FakeCryptoProvider/FakeUuidGenerator/FakeLogger/FakeEmailSender/FakeSubjectResolver/FakeRoleProvider/FakeUserInfoProvider/FakeMetrics
- FakeCryptoProvider 只假随机数（xorshift64*，可复现），其余全是真 OpenSSL 实现
- 38 个 gtest 单测

### Task 16：测试链接过渡 + 路径同步
- 验证 paths.env 新增的 LIBS_COMMON_DIR/LIBS_COMMON_TESTING_DIR 被三种脚本 loader（bash/PowerShell/batch）正确解析，无需改动脚本
- **发现并修复一个真实 bug**：`OAuth2Server/test/contract/ContractFixtures.h` 的 `getPostgresClientOrNull()`/`getRedisClientOrNull()` 在 memory-only 配置（`config.ci.json`，redis_clients/db_clients 为空数组）下会导致进程崩溃——Drogon 的 `RedisClientManager::getRedisClient()` 内部用 `assert()`（非 throw），try/catch 完全防不住。修复：在 try/catch 之前先检查 `plugin->getStorageType() == "memory"`，与现有 `RedisStorageTest.cc`/`PostgresStorageTest.cc` 的既有模式一致。
- 验证方式：完整跑通 `manage.ps1 build-backend -debug` + `manage.ps1 test-backend -debug`（传统非 preset 的 build/ 目录，从零配置），两套配置（标准 config.json 走 Postgres+Redis、config.ci.json 走 memory-only）均 100% 通过

## 本机环境注意事项

- `drogon_ctl.exe` 不在系统 PATH，需要手动加：
  ```powershell
  $env:PATH = "C:\Users\vilas\.conan2\p\b\drogo3ea9a05eb3db3\p\bin;" + $env:PATH
  ```
  （这是 pre-existing 已知问题，CI 里有专门步骤加 PATH，本地脚本没有，不在当前任务范围内修）
- 用户本机独立安装了 Redis 服务（非 Docker），**不要用 Docker 里的 Redis/Postgres**，会冲突。之前误启动过 docker compose 已停止。
- 主要验证目录：`build/windows-msvc-asan`（CMake preset，一直在用）+ `build/`（传统 manage.ps1 流程，Task 16 验证时用过一次）

## M2b 详细完成内容（Task 17，进行中）

### Task 17 slice 1：libs/oauth2 骨架 + oauth2::pkce（commit `440b028`）
- 新建 `libs/oauth2`（authforge::oauth2），依赖仅 `authforge::common`（值对象 + `ICryptoProvider` 端口），零 Drogon、零 OAuth2Plugin 依赖
- `oauth2::pkce` 纯函数模块：`computeCodeChallenge`/`verifyCodeVerifier`/`isValidCodeVerifierFormat`/`isValidCodeChallengeFormat`
- 接入顶层构建：`paths.env` 新增 `LIBS_OAUTH2_DIR=libs/oauth2`；顶层 `CMakeLists.txt` 在 `libs/common`/`libs/common/testing` 之后新增 `add_subdirectory`
- `libs/oauth2/test`（gtest，仿 `libs/common/test` 模式，非 DROGON_TEST）：13 个测试，含 RFC 7636 Appendix B 官方已知向量（用 `FakeCryptoProvider` 的真 OpenSSL SHA-256/base64url 验证，非 stub 断言）
- **故意实现正确算法**（S256 = base64url(原始摘要字节)），与 Task 14 发现的 `TokenService::generateSha256Hash` 缺陷完全独立、不受影响——**当前无任何生产代码调用此新模块**，把 TokenService/AuthorizationService 迁移到调用它是后续 slice 的决定点
- 验证：全量 `cmake --build build/windows-msvc-asan` 编译通过；`ctest -C Debug` 137 个测试（含新增 13 个 PkceTest #79-91）100% 通过，零回归

### Task 17 slice 2：Domain DTO 迁移（commit `c665648`）
- 新建 `libs/oauth2/include/authforge/oauth2/model/{ClientType,Dto}.h`：把嵌套在 `IOAuth2Storage.h` 里的 `OAuth2Client`/`OAuth2AuthCode`/`OAuth2AccessToken`/`OAuth2RefreshToken`/`TokenIntrospection`/`AuthorizationTransaction` 逐字段原样迁移到 `authforge::oauth2::model`（design.md 明确指出迁移源是 `IOAuth2Storage.h` 而非 `OAuth2Types.h`）
- `ClientType` 迁移，`GrantType`/`OAuth2Error` **不迁移**（Task 13 已在 `common::error::ErrorCatalog` 给 OAuth2 协议错误码建了新家，迁旧枚举会造成两套竞争定义）
- 故意**不**把字段包成值对象（Scope/ClientId/RedirectUri/PkceChallenge/TokenValue）——那需要改动所有生产调用点，是更大的独立 slice
- 9 个新 gtest（ClientType 往返/非法输入、DTO 默认值、TokenIntrospection::toJson 的 active/inactive 字段发射）
- 验证：全量编译 + `ctest -C Debug` 146/146 通过，零回归

### Task 17 slice 3：M1 仓储接口迁移（commit `60373d0`）
- 新建 `libs/oauth2/include/authforge/oauth2/model/UserRef.h`：迁移 F4 的透明用户引用占位类型
- 新建 `libs/oauth2/include/authforge/oauth2/repository/{IClientRepository,IGrantRepository,ITokenRepository,IConsentRepository}.h`：迁移 M1 的 4 个仓储接口，依赖改为新迁移的 `authforge::oauth2::model` DTO（而非旧的通过 `IOAuth2Storage.h` 引用），彻底与 `OAuth2Plugin` 解耦
- 方法签名、文档、`saveTokenPair` 默认顺序执行体、`supportsTransactions()`/`supportsCas()` 能力标志契约、`IClientRepository`/`IConsentRepository` 故意不加 `purgeExpired()` 的决策——全部原样保留
- 5 个新 gtest（用最小内存假实现验证每个接口可通过基类指针虚派发调用，外加验证 `saveTokenPair` 默认体确实按 access→refresh 顺序执行）——这些是接口形状测试，不是完整契约测试；现有 `OAuth2Server/test/contract/*` 仍在测试 `OAuth2Plugin` 侧实现，直到后续 slice 把具体实现迁移过来
- 目前存在**两套并行的**这 4 个接口（`OAuth2Plugin` 的 `oauth2::` 命名空间 + 新的 `authforge::oauth2::`），直到后续 slice 迁移具体实现并淘汰旧的
- 验证：全量编译 + `ctest -C Debug` 151/151 通过，零回归

### Task 17 slice 4：PKCE RFC 7636 缺陷修复（commit `70aba78`）
- **用户明确指示修复**（"PKCE 修复代价如何，代价小的话一起做，不用考虑已经部署的客户端"）。排查后确认代价低：① 前端 SPA 当前完全没有走 PKCE（`PRD/mfa_auth_code_pkce_design.md` §6.1 现状澄清已确认），无真实客户端依赖旧值 ② 现有单测 `P0FunctionalityTest.cc::Unit_P0_PKCE_Legacy_Hashing` 自洽（同一函数生成+验证），换算法后仍通过 ③ 唯一钉住旧字节序列的 golden test 已同步更新
- `TokenService::generateSha256Hash`（`OAuth2Plugin/src/services/TokenService.cc`）改为调用 `oauth2::pkce::computeCodeChallenge(input, "S256", crypto)`（Task 17 slice 1 已建好的正确实现），不再自己拼接错误逻辑
- `OAuth2Plugin`（`CMakeLists.txt`）与 `OAuth2Server/test`（`CMakeLists.txt`）新增链接 `authforge::oauth2`——方向正确（Adapter 层的 OAuth2Plugin 依赖 Domain 层的 libs/oauth2，反向不成立）
- `OpenSslCryptoProviderTest.cc` 的 golden test 从"钉住迁移前旧算法字节"改为两个新测试：断言新算法符合 RFC 7636（独立重新实现对比）+ 跟 RFC 7636 Appendix B 官方已知向量交叉验证（与 `libs/oauth2/test/PkceTest.cc` 断言的同一个向量）
- `P0FunctionalityTest.cc` 无需改动（自洽，不受算法变化影响）
- 验证：全量编译通过；`ctest -C Debug` 151/151 通过；`OAuth2Test_test.exe` 直接跑显示 311 个 DROGON_TEST（57114 断言）全过（比修复前多 1 个新测试）；`authforge-oauth2-test` 27/27 通过。零回归

### Task 17 slice 5：IAuditSink 端口 + DrogonAuditSink 适配器（commit `f20f64d`）
- **动机**：`TokenService` 迁移的阻塞项之一——现有 `TokenService.cc` 直接调用 `oauth2::observability::AuditLogger::log()`，后者依赖 `drogon::app().getDbClient()`/`drogon::orm::DrogonDbException`，Domain 层不能依赖
- design.md §3.3 只说了"模型进 common，导出器留在适配器"，但没有为 sink 侧定义端口（因为当时没有 Domain 代码需要发审计事件）。现在补上
- 新建 `libs/common/include/authforge/common/ports/IAuditSink.h`：单方法端口（`record(AuditEvent)`），仿 `IMetrics.h` 的形状，不是照搬 `AuditLogger` 的具体方法名
- 新建 `libs/common/testing/.../FakeAuditSink.h` + 4 个测试（仿 `FakeLogger.h` 模式）
- 新建 `OAuth2Plugin/include/oauth2/adapters/DrogonAuditSink.h` + `.cc`：薄适配器，把 `common::observability::AuditEvent` 逐字段转换成旧的 `oauth2::observability::AuditEvent` 并转发给现有 `AuditLogger::log()`——`AuditLogger` 本身未改动，没有重复的写库逻辑
- 验证：全量编译通过，`ctest -C Debug` 155/155 通过，零回归

### Task 17 slice 6：Client/AuthorizationGrant/TokenPair 聚合（commit `f8a1cb8`）
- design.md 的 Data Models 表定义了三个聚合，之前只搬了裸 DTO，聚合本身没建
- `model/Client.h`：包一层 `OAuth2Client`，加 `isRegisteredRedirectUri()`/`allowsScope()`/`allowsAllScopes()`——现有调用点（如 `TokenService.cc` 里裸的 `clientType==PUBLIC` 判断）都是手写线性查找，这里统一收口
- `model/AuthorizationGrant.h`：包一层 `AuthorizationTransaction`，加 `isExpired()`/`hasPkceChallenge()`/`verifyPkceCodeVerifier()`（后者直接调用 Task 17 slice 1 的 `oauth2::pkce::verifyCodeVerifier`）
- `model/TokenPair.h`：包一层 access+refresh token DTO，**构造时校验配对不变量**（`refreshToken.accessToken` 必须引用 `accessToken.token`；`clientId`/`userId` 必须一致）——这是所有生产调用点一直隐含遵守但从未显式校验过的不变量
- 三者都是刻意的薄包装（不重新用值对象建模字段），跟 Dto.h 保持一致的克制
- 14 个新测试，含用 RFC 7636 Appendix B 向量走通 `AuthorizationGrant::verifyPkceCodeVerifier` 的往返验证、`TokenPair` 全部 3 种不变量违反场景
- 验证：全量编译通过，`ctest -C Debug` 169/169 通过，零回归

### Task 17 slice 7：oauth2::access 决策引擎（commit `40ea196`）
- design.md 目录结构里的 `access/`（consent + scope 分层策略 + 决策引擎）之前完全没建
- 现有生产逻辑把"client 允许→admin 角色→用户 consent"三级决策拆成分散的服务调用（`ClientService::validateClientScopes`、`IdentityService::validateUserRolesForScopes`+`scopeRequiresAdminRole`、`OAuth2StandardController::checkUserConsentAndProceed` 的递归 `hasUserConsent` 调用链），没有一个统一的类型化决策结果
- `access/ScopeDecision.h`：三态枚举（`Valid`/`Invalid`/`ConsentRequired`）+ `ScopeCheckResult`（单 scope 结果+可机读 reason code）+ `ScopeValidationSummary`（汇总，带 `canProceed()`/`needsConsent()`/`hasErrors()`）
- `access/ScopeDecisionEngine.h/.cc`：`isAdminScope()` 精确复刻 `IdentityService::scopeRequiresAdminRole` 的硬编码列表和前缀匹配语义；`evaluateScope()`/`evaluateScopes()` 是**纯函数、同步**求值（不自己做角色/consent的异步查询，那是调用方——未来的 `AuthorizationService`——的职责），刻意保持引擎本身可测试、跟异步编排策略解耦
- 18 个新测试，含短路验证（已 Invalid 的 scope 不应触发 consent 查询回调）
- 验证：全量编译通过，`ctest -C Debug` 182/182 通过，零回归

### Task 17 slice 8-9：JwkManager 去 OAuth2Plugin 依赖（commit `d91237c`, `efb242c`）
- slice 8：构造函数新增可选 `ILogger*` 注入参数（默认 nullptr 回退到共享 `DrogonLogger` 实例），8 处 `logger().log(` 调用改成成员 `log(`
- slice 9：`base64UrlEncode` 不再依赖 `oauth2::adapters::OpenSslCryptoProvider`（Adapter 层类），改成独立实现（跟 `OpenSslCryptoProvider`/`FakeCryptoProvider` 里同一段算法第三次重复，各自为了保持依赖方向正确）
- 验证：两个 slice 各自全量编译+`ctest`182/182+`OAuth2Test_test.exe`311个DROGON_TEST全过，零回归

### Task 17 slice 10：JwkManager 整体迁移到 `libs/oauth2`（commit `911965d`, `1e316f9`）
- 新建 `libs/oauth2/include/authforge/oauth2/jwk/JwkManager.h` + `src/jwk/JwkManager.cc`：行为不变，仅去掉了硬编码 Drogon 回退（未注入 logger 时 `log()` 是安全 no-op）
- `libs/oauth2/CMakeLists.txt` 新增 `find_package(OpenSSL)` + 链接 `OpenSSL::Crypto`
- **旧位置 `OAuth2Plugin/include/oauth2/utils/JwkManager.h` 变成兼容 shim**（`using JwkManager = authforge::oauth2::JwkManager`），现有全部 `oauth2::JwkManager` 调用点不用改——完整命名空间统一是 M8 Task 40 的事，不在本 slice
- `OAuth2Plugin.cc` 显式构造一个 `static DrogonLogger` 传给 `JwkManager` 构造函数，保住生产环境原有日志输出
- `TokenService.h` 的 `class JwkManager;` 前向声明跟新 alias 冲突，改成 `#include` shim 头
- 6 个新 smoke test（`libs/oauth2/test/JwkManagerTest.cc`）
- 验证：全量编译通过，`ctest -C Debug` 182/182，`OAuth2Test_test.exe` 311 DROGON_TEST 全过，零回归

### Task 17 slice 11：TokenService issuer 改构造参数注入（commit `d92b7d1`）
- 去掉 `drogon::app().getCustomConfig()` 在签发 id_token 时的调用，issuer 改为构造参数（默认值/config 路径不变，只是读取时机从"每次签发"挪到"启动时构造"）
- 验证：全量编译 + `ctest` 188/188，零回归

### Task 17 slice 12 + 杂项清理（commit `ead1eda`, `d435c88`，编译验证+批测）
- 新建 `OAuth2Plugin/include/oauth2/adapters/StorageRoleProvider.h`：`IRoleProvider` 的首个生产实现（薄转发到 `IOAuth2Storage::getUserRoles(int32_t,...)`），在 `OAuth2Plugin::initAndStart` 里实例化，**尚未接入任何调用点**——`IdentityService` 的角色查询是按 subject 字符串键的，这个端口按 `internalUserId` 键，真正接入还需要 `ISubjectResolver`
- 顺手清理 `ClientService.cc` 里未使用的 `#include <drogon/drogon.h>`（死代码，无 `drogon::` 符号使用）
- 验证：全量编译 + `ctest -C Debug` 188/188，批量验证 slice 11+12+清理，零回归

### Task 17 剩余 slice（未完成，下一步；已按用户要求加快节奏——单个 slice 编译过即可，攒几个一起测）
- **`AuthorizationService`/`TokenService` 完整迁移到 `libs/oauth2`**：阻塞项基本清空（PKCE/AuditLogger端口/仓储接口/聚合/决策引擎/JwkManager/issuer 均已就位），剩下主要是把类本体搬过去 + 切换到新仓储接口 + 接入 IAuditSink/IRoleProvider
- `IRoleProvider` 已有生产实现（slice 12）但尚未接入任何调用点，需要 `ISubjectResolver` 配合才能真正替换 `IdentityService::getUserRoles`
- 把具体仓储实现（`MemoryClientRepository`/`RedisClientRepository`/`PostgresClientRepository` 等）迁移到 `authforge::oauth2::` 命名空间，生产代码切换、淘汰旧接口——尚未开始

## M3 详细完成内容（Task 20，进行中）

### Task 20 slice 1：libs/drogon 骨架 + validation/ + RequestValidationFilter（本地未提交前，待 commit）

- 新建 `libs/drogon`（authforge::drogon），Adapter 层包，**允许依赖 Drogon**（design.md §4.1 规则 1 只禁 Domain 层依赖 Drogon；common/oauth2/identity 才受限，libs/drogon 和 libs/storage-* 是 Drogon 依赖的指定落点）
- 迁移范围（本 slice，故意最小）：`oauth2::validation::{Rules,RuleEngine,RuleSet,HttpResponder}` 四个类 + `RequestValidationFilter`——**这批代码零依赖 `OAuth2Plugin` 单例**（`drogon::app().getPlugin<OAuth2Plugin>()`），是 M3 Drogon 绑定迁移里风险最低的切片；真正依赖插件单例的 `AuthorizationFilter`/`OAuth2AuthFilter`/controllers/plugin 本体留给后续 slice（需与 Task 21 插件注册决策、Task 23 去单例化协调顺序，避免过早引入循环依赖）
- 命名空间：`authforge::drogon::validation` / `authforge::drogon::filters`（design.md §6 命名空间同步）
- **发现并撤回的循环依赖风险**：最初尝试把 `OAuth2Plugin/include/oauth2/validation/*.h` 四个头文件改成指向新位置的兼容 shim（仿 Task 17 slice 10 的 JwkManager.h 模式），但 `HttpResponder` 依赖的 `common::error::{Error,ErrorContext,ErrorHandler,RequestId}` 本身还留在 `OAuth2Plugin`（且这些类型本身依赖 `drogon::HttpRequestPtr`/`DrogonDbException`，其实也该属于 Adapter 层，只是尚未迁移）。若 `libs/drogon` 反向依赖 `OAuth2Plugin` 获取这些符号，而 `OAuth2Plugin` 的 controllers 又需要 `RuleSet`/`HttpResponder`（一旦改成 shim 指回新位置）依赖 `libs/drogon`，就会形成 `OAuth2Plugin` ⇄ `authforge-drogon` 循环依赖。**已撤回 shim 改动**（`git checkout` 还原四个旧头文件到原始内容），改为两套并行实现共存（同 Task 17 slice 1-3 的既有模式："新增不动旧，旧调用点淘汰是后续 slice 的事"）。`common::error` 模块自身的 Adapter 层归位（迁入 `libs/drogon` 或类似位置）是清空这个反向依赖的前提，留作后续 slice。
- `libs/drogon` 目前对 `OAuth2Plugin` 有一条**临时的**编译期依赖（`target_link_libraries(authforge-drogon PUBLIC ... OAuth2Plugin)`，仅为拿到 `common::error` 的头文件+实现）——顶层 `CMakeLists.txt` 因此把 `add_subdirectory(libs/drogon)` 放在 `add_subdirectory(OAuth2Plugin)` **之后**（不同于其他 Domain 层 libs/* 放在 OAuth2Plugin **之前**的顺序）。这条依赖边随 `common::error` 归位而清空，不违反 libs/drogon 自身"允许依赖 Drogon"的 Adapter 层定位。
- 踩坑记录：新代码写在 `namespace authforge::drogon::validation` 内部时，裸写 `drogon::HttpRequestPtr` 会被编译器解析成 `authforge::drogon::drogon::HttpRequestPtr`（当前命名空间找不到子命名空间 `drogon` 报错），必须显式 `::drogon::` 全局限定——四个新文件均已改用 `::drogon::`。
- paths.env 新增 `LIBS_DROGON_DIR=libs/drogon`
- 验证：`authforge-drogon` 独立编译通过；全量 `cmake --build build/windows-msvc-asan` 编译通过（含 OAuth2Plugin/OAuth2Server/OAuth2Test_test 等既有目标零回归）；`ctest -C Debug` 207/207 全绿（既有测试路径未改动，新库尚无独立测试可执行体——`DROGON_TEST` 框架的独立 test_main 基础设施比预期复杂，评估后决定暂不为这个小 slice 单独搭建，靠现有 207 个测试 + 编译期验证作为本 slice 的回归防线；后续 slice 迁移量变大后再补）

### Task 20 slice 2：AuthorizationFilter + OAuth2AuthFilter（本地未提交前，待 commit）

- 迁移 `oauth2::filters::{AuthorizationFilter,OAuth2AuthFilter}` 到 `authforge::drogon::filters`——这两个**确实**依赖 `drogon::app().getPlugin<OAuth2Plugin>()` 单例查找，但因为 `libs/drogon` 自 slice 1 起已经对 `OAuth2Plugin` 有临时编译期依赖（拿 `common::error`），这不是新增依赖边，只是使用了已存在的那条边，可以安全迁移，不用等 Task 21/23
- 同 slice 1：**不**把旧 `oauth2::filters::{AuthorizationFilter,OAuth2AuthFilter}` 头文件改成 shim，两套实现并行——旧位置留给未迁移的调用点（`OrganizationController.h` 等 controller 的 `ADD_METHOD_TO(..., "oauth2::filters::AuthorizationFilter")` 字符串引用、`OAuth2Server/main.cc` 的 `#include <oauth2/filters/OAuth2AuthFilter.h>`、以及好几个测试文件），等 controllers 本体迁移时再统一切换调用点、淘汰旧位置
- 验证：`authforge-drogon` 独立编译通过；全量编译零回归；`ctest -C Debug` 207/207 全绿

### Task 20 剩余 slice（未完成，下一步）

- controllers（`OAuth2StandardController` + `OAuth2Server/controllers/*` 15 个文件，AdminController 单文件 2896 行）、views（`login.csp`/`consent.csp`）、`OAuth2Plugin.cc` 本体（退化为装配器）——按文件规模需继续拆分为多个 slice。controllers 迁移时机机应与 filters 一致的调用点切换（把 `ADD_METHOD_TO` 字符串里的 `"oauth2::filters::..."` 改成新命名空间、main.cc/CMakeLists 的 include 路径切换）一并完成，避免旧/新 filter 长期并存导致维护混乱
- `common::error` 模块（`ErrorTypes`/`ErrorCatalog`/`ErrorContext`/`ErrorHandler`/`ErrorResponder`/`RequestId`/`OAuth2ErrorHandler`）本身的 Adapter 层归位，用于清空 libs/drogon → OAuth2Plugin 的临时依赖边——注意 `libs/common/include/authforge/common/error/` 已有 `ErrorTypes.h`/`ErrorCatalog.h` 的框架无关版本（Task 13 产出），这是**不同**的一套（`authforge::common::error` vs 现有 `common::error`），本任务的 error 模块归位目标位置待定（可能是 libs/drogon，因为 ErrorResponder/ErrorHandler/RequestId 均依赖 Drogon 类型）

## Task 21：插件注册与 config 加载迁移决策（design.md §5.7）

**决策：方案 A（低破坏）。** 保留 `OAuth2Plugin` 类名与 4 份 `config.*.json` 的 `plugins` 块（`storage_type`/`clients`/`admin_users`/`tokens` 等业务配置 schema 不变），插件本体继续是 `drogon::Plugin<OAuth2Plugin>` CRTP 反射注册的类，随后续 slice 逐步退化为「读配置 → 构造 Adapter 实现 → 注入 Domain 服务 → 注册 controller/filter」的薄装配器（design.md 用语）。4 份 config **零改动**。

依据：

- Task 20 slice 1-2 已经隐含选择了这个方向——迁移 filters/validation 时全程保留 `OAuth2Plugin` 类名、`#include <oauth2/plugin/OAuth2Plugin.h>` 路径和 `drogon::app().getPlugin<OAuth2Plugin>()` 调用点不变，从未触碰插件本体或其在 `OAuth2Plugin/CMakeLists.txt` 里的 **OBJECT 库**类型（非 STATIC——OBJECT 库的目标文件是逐个直接链接进消费者的，不存在链接器按需抽取导致注册符号被丢弃的问题，这也是方案 A 里"用 whole-archive 保活插件自注册符号"这条风险在**当前阶段尚未出现**的原因：真正的风险点是 Task 22/H5 提到的"改为静态库形态分发"时才会浮现，届时需要给 whole-archive 链接补上，不是现在）
- 验收标准「config 驱动的插件实例化成功」已被现有测试套件隐式覆盖：`OAuth2Server/test/test_main.cc` 通过 `common::config::ConfigManager::load()` 加载 `config.json`（含 `"plugins":[{"name":"OAuth2Plugin","config":{...}}]` 反射块）后调用 `drogon::app().run()`；本仓库现有 207 个测试里有 **30+ 处**直接调用 `drogon::app().getPlugin<OAuth2Plugin>()` 并断言/使用返回的非空指针（`RateLimiterTest`/`ConfigMigrationTest`/`PluginTest`/`P1FeatureTest`/`AuthServiceGetUserInfoTest`/多个 `Property*_MfaCrossClientAuthFix_*` 等），这些测试全绿即是「按名反射加载 + config 业务块解析成功」的现成证明，本次 M3 slice 未改动这条路径
- 4 份 config 的 `plugins[].name`（`OAuth2Plugin`/`drogon::plugin::PromExporter`/dev 的 `AccessLogger`/prod 的 `Hodor`）保持不变，符合方案 A 的定义

**遗留给 Task 22/23 的部分**：本决策只解决"插件类名/config 反射是否变"这一层；「改为静态库形态分发时的 whole-archive 链接」（Task 22）和「controller/filter 去 `getPlugin<OAuth2Plugin>()` 单例查找、改构造注入」（Task 23）仍是独立且尚未开始的工作——方案 A 选定后，Task 23 的"去单例化"目标是**调用方式**（构造注入 vs 全局查找），不是要求插件本身消失或改名，这与方案 A 并不矛盾（design.md §5.7 决策段最后一句："无论哪种：Task 22 验收须包含 config 驱动的插件实例化成功"，本决策已满足）。

### 重要发现：controllers/views/plugin 迁移必须与 Task 22（whole-archive）同步做，不能再走"slice 1/2 那种共存"模式

尝试迁移 `HealthController` 到 `libs/drogon` 作为 Task 20 slice 3 时发现并撤回：

- `drogon::HttpController<T>` / `drogon::Plugin<T>` 通过 `ADD_METHOD_TO`/`METHOD_LIST_BEGIN`（controller）和 CRTP 基类模板实例化（plugin）做**静态自注册**——路由/插件在全局静态对象构造时把自己塞进 Drogon 内部注册表，没有任何一个"正常"调用点会显式引用这些类的符号
- 这与 slice 1/2 迁移的 filters/validation **本质不同**：filter 类是被 controller 的 `ADD_METHOD_TO(..., "命名空间::ClassName")` **按字符串**引用或被测试直接 `new` 出来用的，编译器/链接器有真实引用链，两套并行实现（新旧文件都编译进各自目标）不会冲突，因为它们是不同的 C++ 类型，谁被引用就链接谁
- controller/plugin 没有这种"谁引用谁链接"的保护：如果同一个最终可执行文件（如未来的 `OAuth2Server`）**同时**链接了含旧 `HealthController` 的源码编译单元和含新 `authforge::drogon::controllers::HealthController` 的 `libs/drogon` 静态库，两者会同时自注册 `/health` 路由，Drogon 在启动时对重复路由要么报错要么产生未定义的"哪个生效"行为——这是**启动期失败**，比 F1 的"链接器丢弃符号导致 404"更早更严重
- 反过来，如果只迁移到 `libs/drogon` 且从 `OAuth2Server` 侧删除旧文件（原子切换，不共存），又直接撞上 design.md F1/H5 描述的静态库符号丢弃问题：`libs/drogon` 一旦变成 STATIC 库，`OAuth2Server` 常规链接会因为链接器认为 controller 的自注册静态对象"未被引用"而整体丢弃该 `.o`，导致路由 404——这正是 Task 22 的 whole-archive 链接要解决的问题
- **结论**：controllers、views（`login.csp`/`consent.csp`）、`OAuth2Plugin.cc` 插件本体这三类"自注册"代码的迁移，必须与 Task 22（whole-archive 链接配置）**在同一个原子提交批次**内完成（迁移 + 删除旧文件 + whole-archive 链接配置 + 验证路由可达，四件事一起做，不能分散成"先迁移再补链接"的多个 slice），区别于 filters/validation 这种可以安全共存过渡的迁移。这也解释了 design.md 为什么把 Task 20（创建 libs/drogon）和 Task 22（whole-archive）分开列但反复强调"扩展验收"要一起看。
- 已清理本次撤回的 slice 3 尝试（`libs/drogon/include/authforge/drogon/controllers/HealthController.h` 未提交，已删除），未污染仓库历史

## 下一步

- **Task 20 controllers/views/plugin 本体 + Task 22 whole-archive 必须合并成一个更大的原子工作批次**（见上一节的发现），这比原计划的"逐个 slice 迁移"更复杂，需要：
  1. 一次性把全部 controllers（`OAuth2StandardController` + `OAuth2Server/controllers/*` 15 个文件）+ views + `OAuth2Plugin.cc` 迁入 `libs/drogon`
  2. 同时从 `OAuth2Server`/`OAuth2Server/test` 的 CMakeLists 里移除对应的旧源码编译（`aux_source_directory(controllers ...)`/`file(GLOB PLUGIN_CTL_SRC ...)` 等）
  3. 同时给 `OAuth2Server`/`OAuth2Test_test` 对 `authforge-drogon` 的链接配置 whole-archive（`$<LINK_LIBRARY:WHOLE_ARCHIVE,authforge-drogon>` 或 MSVC `/WHOLEARCHIVE:`）
  4. 验证路由可达（非 404）+ config 驱动插件实例化仍成功 + 全部 207 个现有测试通过
  - 由于规模巨大（AdminController 单文件 2896 行），这个批次内部仍可以按"一个 controller 文件一个提交"的粒度推进，但**最后一步（whole-archive 配置生效 + 移除旧编译单元）必须是所有 controller 都迁移完之后的单次原子切换**，中间过程 `libs/drogon` 会短暂处于"新代码已加入但因未启用 whole-archive/旧编译单元未移除而实际未被使用"的状态（这本身不违反"每步可编译"，只是功能上是过渡态）
- Task 23（controller/filter 去单例化）：受影响清单见 tasks.md（9 处生产 + ~38 处测试），可以作为上面这个大批次内部的一个环节一起处理（去单例化 + 迁移到 libs/drogon 一起做，减少重复修改同一批文件的次数）
- 已完成且可安全独立推进的部分：filters/validation（slice 1/2，已完成）——这些不受上述限制，因为它们没有自注册行为
- Task 17 的 `AuthorizationService`/`TokenService` 主体、具体仓储实现迁移到 `authforge::oauth2::` 命名空间仍未完成——不阻塞 M3（M3 的装配器可以继续依赖现有 `OAuth2Plugin` 服务），留作后续任务
- libs/identity 的 MFA/WebAuthn/Social/Session 迁移留作后续独立任务（用户明确约束范围）

## 关键提醒

- 每个 slice/task 完成后必须跑全量测试套件（当前 124 个 CTest cases: 40 authforge-common-test + 38 authforge-common-testing-test + 1 OAuth2Tests(内含 310 DROGON_TEST cases/57113 assertions) + 45 Contract）确认零回归
- 遵循 H6 指导：小步提交，每个 commit message 详细记录做了什么、为什么、验证了什么
- 发现的生产缺陷要记录但不擅自修复（除非明确要求或修复成本极低且不改变行为契约）
