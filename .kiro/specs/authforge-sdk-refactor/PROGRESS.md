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

## 重大修正：AutoCreation=false + registerController/registerFilter/registerPlugin 可能完全避免 whole-archive（用户指出，已核实源码）

用户指出：`class AdminController : public drogon::HttpController<AdminController, false>` 这种写法下 controller **不会静态自注册路由**，需改用 `drogon::app().registerController(ctrlPtr)` 显式注册。核实 Drogon 源码（`drogo3ea9a05eb3db3` conan 包）后确认，这比上一节"必须与 whole-archive 同批次"的结论更精确，需要修正：

### 核实到的确切机制

- `HttpController<T, AutoCreation>`/`HttpFilter<T, AutoCreation>` 的 `AutoCreation` 模板参数（默认 `true`）只控制 **是否自动调用 `T::initPathRouting()`挂路由**（`methodRegistrator` 构造体里 `if (AutoCreation) T::initPathRouting();`），**不影响** `DrObject<T>` 层面的类型注册（`DrClassMap::registerClass`，在 `DrObject<T>::alloc_` 静态成员构造时发生，只要 `T` 满足 `std::is_default_constructible`，与 `AutoCreation` 无关）。
- `HttpAppFramework::registerController<T>(shared_ptr<T>)` 有 `static_assert(!T::isAutoCreation, ...)`——**只能**用于 `AutoCreation=false` 的类；调用体是 `DrClassMap::setSingleInstance(ctrlPtr); T::initPathRouting();`——即显式提供实例 + 显式触发路由挂载。`registerFilter`/推测的 filter 对应方法同构。
- **`drogon::Plugin<T>` 没有 `AutoCreation` 模板参数**——插件的注册/查找（`getPlugin<T>()`）是纯 `DrClassMap` 机制（同 `DrObject<T>::alloc_`），Drogon 框架内部的 `PluginsManager`（未导出头文件，无法直接确认，但从 `PluginBase`/`getPlugin` 的公开行为推断）按 config 里的类名字符串查找并构造——**插件本身没有"AutoCreation=false + 手动 registerPlugin"这种口子**，config 反射加载是唯一途径（design.md §5.7 的 H1 风险分析对插件仍然完全成立，不受本节修正影响）。
- **本质结论**：`AutoCreation=false` 把"路由挂载"从"静态对象副作用（隐式，链接器可能连带丢弃）"变成"显式函数调用（`registerController` 内部调 `T::initPathRouting()`，是真实的、编译器/链接器能看到的调用链）"——**只要 `main.cc`/装配代码里显式调用了 `make_shared<T>()` + `registerController(ptr)`，这个符号引用链就是真实的，静态库的这个 `.o` 就不会被链接器当作"未引用"丢弃**，从而绕开 F1/H5 描述的"controller 类注册符号被 whole-archive 丢弃导致路由 404"问题——因为问题的根源（隐式静态初始化、无显式引用）被消除了，不是靠 whole-archive 强行保留，而是从设计上不再需要它。

### 对迁移策略的影响

- **filters**：`registerFilter` 同理存在（`static_assert(!T::isAutoCreation, ...)` 紧跟在 `registerController` 后面，见 HttpAppFramework.h）。但 slice 1/2 已经迁移的 3 个 filter 全部保持 `AutoCreation=true`（默认）——它们目前是被 **`ADD_METHOD_TO(..., "命名空间::ClassName")` 字符串引用**的被动过滤器（controller 按名查找 filter 类并通过 `DrClassMap` 实例化），不是本节讨论的"自己挂路由"的主动注册模式，这条路径本来就没有 whole-archive 问题（DrClassMap 按名查找本身就是显式调用链）。slice 1/2 的迁移结论不变，不需要回滚或调整。
- **controllers**（本节的新发现真正影响的部分）：把 15+1 个 controller 逐个改成 `HttpController<T, false>` + 在装配代码（未来的 `apps/server` bootstrap，当前是 `main.cc`/`OAuth2Plugin::initAndStart`）里显式 `registerController`，**可以把"迁移到 libs/drogon"和"whole-archive 链接配置"两件事解耦**——迁移到 `libs/drogon` 静态库后，只要 main.cc 显式 `make_shared` + `registerController` 了每一个 controller，就不需要 whole-archive；如果嫌一次性改全部 15+1 个文件的构造方式工作量大，也可以先维持 `AutoCreation=true` 原样迁移（沿用上一节"迁移+移除旧文件+whole-archive 同批次"的保守路径）——**两条路径都可行，`AutoCreation=false` 路径的额外收益是顺带完成 Task 23 的"去单例化"精神**（因为改成显式构造注册后，可以同时把构造函数改造为接受 Domain 服务依赖注入，而不是构造后仍然全局 `getPlugin<OAuth2Plugin>()` 查找）——但这也意味着 `AutoCreation=false` 路径工作量并不比"去单例化"任务本身小，只是把 Task 20 controllers 迁移、Task 22 whole-archive 判断、Task 23 去单例化三件事合并成一次改造，减少重复touch同一批文件的次数。
- **plugin 本体**（`OAuth2Plugin.cc`）：不受本节修正影响，仍然是 config 反射加载，Task 21 已确认的方案 A（保留类名/config块，OBJECT库形态不变）继续有效；如果 Task 20 controllers 迁移改用 `AutoCreation=false` 路径，`OAuth2Plugin` 本身要不要迁移到 `libs/drogon`、要不要保持独立于 controllers 之外单独处理，是下一步需要具体规划的点。

### 验证结果：机制确认有效，whole-archive 不再是 controllers 迁移的必需品（Task 20 slice 3，已完成）

**实测方式**：把 `HealthController` 完整迁移到 `libs/drogon`（`authforge::drogon::controllers::HealthController`），改成 `AutoCreation=false`，删除 `OAuth2Server/controllers/` 下的旧文件（原子切换，不共存——这类自注册类不能像 filter 那样共存，见上一节），`OAuth2Server`/`OAuth2Test_test` 的 `CMakeLists.txt` 对 `authforge::drogon` 用**普通** `target_link_libraries`（明确没有 whole-archive/`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`），`main.cc`/`test_main.cc` 加一行显式 `drogon::app().registerController(std::make_shared<authforge::drogon::controllers::HealthController>())`。

**验证步骤与结果**：

1. 全量编译（`authforge-drogon`/`OAuth2Server`/`OAuth2Test_test`）：全部通过，零错误
2. 启动真实 `OAuth2Server.exe`（`config.ci.json`，memory 存储，无需数据库），用 `curl` 直接发 HTTP 请求：
   - `GET /health/live` → `{"status":"ok"}` HTTP 200
   - `GET /health` → `{"database":"connected","service":"OAuth2 Server","status":"ok","storage_type":"memory",...}` HTTP 200（包含成功调用 `drogon::app().getPlugin<OAuth2Plugin>()` 拿到非空插件、读取 `storage_type` 的完整链路，证明与 Task 21 方案 A 决策兼容）
   - （`GET /health/ready` 触发一个**预先存在、与本次改动无关**的 `dbClientsMap_` 断言崩溃——`getDbClient()`/`getRedisClient()` 在纯 memory 配置下调用会崩，这是 Task 16 已经记录过的同类已知问题，未在本次范围内修，用 `/health`/`/health/live` 两个不触发 DB 调用的路由已足以证明核心假设）
3. `ctest -C Debug`：**207/207 全绿**，含 `Integration_P0_SuccessBodyShape_GoldenSnapshot`（`make_shared<HealthController>()` 直接调方法，不经路由——`AutoCreation=false` 不影响这种用法）

**结论确认**：`AutoCreation=false` + 显式 `registerController` 让 controller 的路由挂载从"隐式静态初始化副作用"变为"真实的、编译器/链接器可见的函数调用链"，**controller 迁入 STATIC 库（`libs/drogon`）后，只要消费者显式构造+注册了它，就不需要 whole-archive 链接**——用户提出的方法完全成立，已用真实可执行文件的 HTTP 请求验证（不是仅靠单测断言），这比 design.md 原方案（F1/H5 要求 whole-archive）成本更低。

**过程中修复的一个小问题**：`AuthorizationFilter.cc`/`OAuth2AuthFilter.cc`/`HttpResponder.cc`（Task 20 slice 1/2 遗留）里裸写的 `common::error::Xxx` 在本次改动后编译失败——因为 `HealthController.cc` 新 include 的 `oauth2/plugin/OAuth2Plugin.h` 链式引入了 `authforge/common/ports/IRoleProvider.h`，使 `authforge::common` 命名空间对同一个静态库内的其他编译单元可见，C++ 命名空间查找规则下 `namespace authforge::drogon::{filters,validation} { ... common::error::Xxx ... }` 里的裸 `common::` 现在优先匹配到父级链上的 `authforge::common`（存在但没有 `error` 子命名空间）而非期望的全局 `::common::error`。修复：这三个文件里所有 `common::error::` 改成显式 `::common::error::` 全局限定。**这是个通用的坑**——以后在 `authforge::drogon::*` 命名空间内写代码，任何引用全局 `common::` / `oauth2::`（非 `authforge::` 前缀）命名空间的地方都要留意这个歧义，优先加 `::` 前缀。

### 后续 controllers 迁移的标准流程（取代之前"必须与 whole-archive 同批次"的保守结论）

单个 controller 迁移到 `libs/drogon` 的标准步骤（照 `HealthController` slice 3 的模式）：

1. 在 `libs/drogon/include/authforge/drogon/controllers/` + `libs/drogon/src/controllers/` 创建新文件，命名空间 `authforge::drogon::controllers`，类模板参数改 `HttpController<T, false>`
2. **原子删除**旧位置的 `.h`/`.cc`（不能共存，见上一节的自注册冲突分析）
3. 更新所有 `#include` 调用点（其他 controller 间的相互引用、测试文件的直接实例化用法）到新路径+命名空间
4. 在装配点（当前是 `OAuth2Server/main.cc` + `OAuth2Server/test/test_main.cc`，未来是 `apps/server` bootstrap）添加一行 `drogon::app().registerController(std::make_shared<authforge::drogon::controllers::Xxx>())`
5. 编译验证 + 用真实可执行文件 HTTP 请求验证关键路由（不能只靠单测——单测很多是直接调方法，绕过路由分发，见上面`Integration_P0_SuccessBodyShape_GoldenSnapshot`的例子）
6. 全量 `ctest` 验证零回归

**已知例外/待定**：`ADD_METHOD_TO(..., "命名空间::FilterClassName")` 字符串引用的过滤器（`AuthorizationFilter`/`OAuth2AuthFilter`，Task 20 slice 2 已迁移）不需要改 `AutoCreation`——它们是被 DrClassMap 按名动态查找实例化的被动组件，不是本节讨论的"主动挂路由"模式，这条路径本来就没有 whole-archive 问题（`ADD_METHOD_TO` 里的字符串本身就是一个真实的、编译期确认过的引用意图，即使运行时是按名查找）。

### Task 20 全部 controllers 迁移完成（用户要求加快节奏，一次性批量完成）

按用户明确指示"全部controller全部一次性迁移，再一起编译测试"，把剩余全部 14 个 controller（`GoogleController`/`WeChatController`/`OrganizationController`/`ClientRegistrationController`/`ApiDocController`/`DeviceAuthController`/`EmailVerificationController`/`GitHubController`/`MfaController`/`PasswordResetController`/`SessionController`/`UserSelfServiceController`/`WebAuthnController`/`AdminController`，含 2688 行的 `AdminController.cc`）一次性迁移到 `libs/drogon`，而不是逐个 slice 提交。

**执行方式**：

- 中小文件（<800 行）逐个手写迁移（新建 `.h`/`.cc`，命名空间 `authforge::drogon::controllers`，`HttpController<T, false>`，`::drogon::` 全限定），原子删除旧文件
- `AdminController`（2688 行）用 PowerShell 正则批量转换（自动包裹 namespace、限定 `HttpRequestPtr`/`HttpResponsePtr`/`Get`/`Post`/`Put`/`Delete`/`drogon::app()`/`drogon::orm::`/`k201Created` 等符号），而非逐行手写——大幅提速，转换后人工核查开头/结尾正确性
- `AuthService.h/.cc`（`SessionController` 依赖）一并迁移到 `libs/drogon/{include,src}/authforge/drogon/`（命名空间 `authforge::drogon::services`），避免 `libs/drogon` 反向依赖 `OAuth2Server`（这是这批迁移里唯一一个"被拖带迁移"的非 controller 文件，因为 `SessionController::login/registerUser` 直接调用它）
- `main.cc` + `test/test_main.cc` 同步添加全部 21 个 `drogon::app().registerController(...)` 调用（15 controller + 之前 slice 1/2 的机制验证未涉及 filter，filter 走 `ADD_METHOD_TO` 字符串查找不需要显式注册）

**踩坑与修复（两类命名空间歧义，均已确认是通用规律并记入本节供后续参考）**：

1. **`common::error::` 歧义**（重复第一次踩坑，出现在 `AuthorizationFilter.cc`/`OAuth2AuthFilter.cc`/`HttpResponder.cc`/`AdminController.cc`）：一旦某个 `.cc` 的 include 链间接引入了 `authforge::common` 命名空间（哪怕只是 `authforge::common::ports::IRoleProvider` 这种深层头文件），`namespace authforge::drogon::xxx { ... common::error::Yyy ... }` 内的裸 `common::` 会优先匹配到同级的 `authforge::common`（存在但没有 `error` 子命名空间）而不是全局 `::common::error`，导致编译失败。**修复模式**：所有 `common::error::` 改成 `::common::error::`。
2. **`oauth2::`/`drogon::` 歧义**（本批新发现的同类问题，规模更大）：`namespace authforge::drogon::controllers { ... }` 内部裸写 `oauth2::observability::...`/`drogon::app()`/`drogon::orm::Result` 等，一旦 include 链带入 `authforge::oauth2` 或触发 `authforge::drogon` 自身的命名空间可见性，会被优先解析到 `authforge::oauth2::`/`authforge::drogon::` 而不是全局 `::oauth2::`/`::drogon::`。**修复模式**：用 PowerShell 正则 `(?<!::)(?<!authforge::)\boauth2::` → `::oauth2::`（同理 `drogon::`）批量修复全部 14 个新迁移的 `.cc` 文件 + `AuthService.cc`。
3. **通用规律总结**：任何写在 `authforge::drogon::*`（或未来 `authforge::oauth2::*`/`authforge::identity::*`）命名空间内部的代码，凡是引用不带 `authforge::` 前缀的全局命名空间（`drogon::`/`oauth2::`/`common::`/`services::`等 pre-refactor 遗留命名空间），**必须**显式加 `::` 前缀，不能依赖"反正没有同名子命名空间"的假设——因为 include 链的传递性会不可预测地引入 `authforge::` 子命名空间的可见性，这个问题只有在编译时才会暴露，且报错信息（C3083/C2039）不会直接提示"这是命名空间歧义"，需要经验识别。

**验证结果**：

- 全量 `cmake --build` (`authforge-drogon` + `OAuth2Server` + `OAuth2Test_test`)：编译通过，零错误
- `ctest -C Debug`：**207/207 全绿**
- 真实可执行文件端到端验证（`OAuth2Server.exe` + `config.ci.json` memory 存储 + `curl`）：
  - `GET /health` → 200，含 `getPlugin<OAuth2Plugin>()` 成功
  - `GET /login?...` → 200，`SessionController::showLoginPage` 成功渲染 `login.csp` 视图
  - `GET /api/admin/dashboard`（无 Authorization header）→ 401 `AUTH_TOKEN_INVALID`，证明 `AdminController` + `AuthorizationFilter`（仍是旧 `oauth2::filters::` 位置，按名字符串查找）链路完整
  - `POST /api/google/login`（缺 code 参数）→ 400 `VALIDATION_MISSING_REQUIRED_FIELD`，证明 `GoogleController` 正常
- `OAuth2Server/controllers/`、`OAuth2Server/filters/` 现在是空目录（`.gitkeep` 保留），`OAuth2Server/CMakeLists.txt`/`test/CMakeLists.txt` 的 `aux_source_directory`/`file(GLOB)` 调用返回空列表（无害，已加注释说明）

**Task 20 至此全部完成**：全部 15 个 controller + 2 个主动挂路由的 filter（`AuthorizationFilter`/`OAuth2AuthFilter`）+ `RequestValidationFilter`（被动查找）+ `AuthService` 均已迁入 `libs/drogon`。**且全程未使用 whole-archive**，证实了用户提出的 `AutoCreation=false` 方案完全可行，比 design.md 原定的 F1/H5 whole-archive 方案更简单。

### 遗留、未动的部分（明确记录，避免误判为遗漏）

- **`OAuth2StandardController`**（在 `OAuth2Plugin/`，不是 `OAuth2Server/`）：这是核心 OAuth2 协议端点控制器（`/oauth2/authorize`、`/oauth2/token` 等），design.md §5.8 提到未来要拆分改名（`AuthorizationEndpointController`/`TokenEndpointController`/`DiscoveryController`），**本次未触碰**——它跟 `OAuth2Plugin` 本体耦合更深（直接持有 `plugin->getTokenService()` 等），且属于协议引擎核心而非产品外围功能，按 design.md 的既定优先级应该晚于本批"外围 controller"处理，留给后续任务（可能是 M3 的下一个 slice，或者结合 Task 23 去单例化一起做）
- **`oauth2::filters::AuthorizationFilter`/`OAuth2AuthFilter` 旧位置**：本批迁移的 controller 里 `ADD_METHOD_TO` 字符串仍然写的是 `"oauth2::filters::AuthorizationFilter"`（旧命名空间），没有切换成 `"authforge::drogon::filters::AuthorizationFilter"`——因为 DrClassMap 的按名反射查找机制下，新旧两个 filter 类目前都存在且都能被查到，两边都能工作，暂不强制统一（等 M8 Task 40 命名空间统一扫尾时处理，或者提前作为一个独立小 slice 处理也可以）
- **Plugin 本体**（`OAuth2Plugin.cc`）：仍在 `OAuth2Plugin` OBJECT 库，未迁移，Task 21 方案 A 决策继续有效
- **Views**（`login.csp`/`consent.csp`）：物理文件仍在 `OAuth2Server/views/`，`drogon_create_views` 在 `OAuth2Server/CMakeLists.txt`/`test/CMakeLists.txt` 里生成，未迁移到 `libs/drogon`——验证过 `SessionController::showLoginPage`（已迁移到 `libs/drogon`）调用 `HttpResponse::newHttpViewResponse("login", data)` 能正常工作（上面的 `/login` 200 测试），说明 view 的加载不依赖 controller 所在的库，可以继续留在原位，不阻塞

### OAuth2StandardController 迁移完成（核心协议控制器）

把最后一个、也是耦合最深的控制器 `OAuth2StandardController`（`/oauth2/authorize`、`/oauth2/token`、`/oauth2/userinfo`、`/oauth2/introspect`、`/oauth2/revoke`、OIDC discovery、JWKS，1562 行）从 `OAuth2Plugin/` 迁移到 `libs/drogon`。

**决策：命名空间保持 `oauth2::controllers` 不变**（不改成 `authforge::drogon::controllers`）——因为已有 4 处调用点（`OAuth2Server/main.cc`、`OAuth2Plugin.cc`、`OAuth2FlowE2ETest.cc`、`CategoryA_InitOrderSnapshotTest.cc`）引用 `oauth2::controllers::OAuth2StandardController`，保持命名空间可以避免改动这些点；design.md §5.8 提到的拆分改名（`AuthorizationEndpointController`/`TokenEndpointController`/`DiscoveryController`）留给后续任务。物理位置迁移和命名空间迁移是两件独立的事，不需要绑定一起做。

**发现并解决的真实循环依赖**：`OAuth2Plugin.cc`（`initAndStart()`）原本直接调用 `oauth2::controllers::OAuth2StandardController::initApiDocs()`。若该类迁入 `libs/drogon`，`OAuth2Plugin.cc` 就需要 `#include <authforge/drogon/controllers/OAuth2StandardController.h>`，形成 `OAuth2Plugin → libs/drogon → OAuth2Plugin`（后者是 slice 1 就有的、为 `common::error` 建立的既有依赖边）的真循环。**修复**：确认 `OAuth2Server/main.cc` 已经在 `drogon::app().run()` 之前显式调用了同一个 `initApiDocs()`（`call_once` 保护，重复调用本来就安全——这是当年"消除 SIOF 静态初始化依赖"设计里预留的冗余调用保护），因此可以安全**删除** `OAuth2Plugin.cc` 里的调用，只保留 `main.cc`/`test_main.cc` 的显式调用（`test_main.cc` 之前没有这行，本次补上）。这样 `OAuth2Plugin.cc` 不再需要 include `libs/drogon` 的任何头文件，循环依赖自然消失。

**验证结果**：

- 全量 `cmake --build`（含 `OAuth2Plugin`/`authforge-drogon`/`OAuth2Server`/`OAuth2Test_test`）：编译通过，零错误，**确认无循环依赖**
- `ctest -C Debug`：207/207 全绿
- 真实服务器 + curl 验证核心 OAuth2/OIDC 协议端点：
  - `GET /.well-known/openid-configuration` → 200，完整 OIDC discovery 文档（`authorization_endpoint`/`token_endpoint`/`jwks_uri`/`grant_types_supported` 等字段齐全）
  - `GET /.well-known/jwks.json` → 200
  - `GET /oauth2/authorize`（缺参数）→ 400 `VALIDATION_INVALID_INPUT`，证明请求验证链路完整

**Task 20 现已彻底完成**：`OAuth2Plugin`/`OAuth2Server` 目录下所有 controller、filter（主动挂路由的两个）、`AuthService` 均已迁入 `libs/drogon`，全部使用 `AutoCreation=false` + 显式注册，全程未用 whole-archive。

### 下一步

1. Task 23（controller/filter 去单例化）：现在全部 controller 已经在 `libs/drogon`，去单例化可以作为一个独立的后续批次
2. Task 22（whole-archive）：**大概率不再需要**——`AutoCreation=false` 方案已验证完全替代其作用；仍需评估的是 **views**（`login.csp`/`consent.csp`，`drogon_create_views` 生成的类）和**插件类本身**（`OAuth2Plugin`，`Plugin<T>` 没有 `AutoCreation` 参数）是否有类似的免 whole-archive 路径，或者这两类天生需要不同的处理方式
3. Plugin 本体退化为装配器（Task 21 方案 A 已定，尚未真正做"退化"这个动作，目前只是删除了一行冗余调用，插件本体基本保持原样）
4. `OAuth2StandardController` 后续可能的拆分改名（design.md §5.8，非阻塞项）
- Task 17 的 `AuthorizationService`/`TokenService` 主体、具体仓储实现迁移到 `authforge::oauth2::` 命名空间仍未完成——不阻塞 M3（M3 的装配器可以继续依赖现有 `OAuth2Plugin` 服务），留作后续任务
- libs/identity 的 MFA/WebAuthn/Social/Session 迁移留作后续独立任务（用户明确约束范围）

## Task 25：拆分 main.cc 为 bootstrap 模块（design.md §6 "apps/server/src/main.cc # 仅装配"）

新建 `OAuth2Server/bootstrap/` 目录，把 `main.cc` 里原本混在一起的横切关注点拆成 6 个独立模块：

- `CorsSetup.h/.cc`：CORS 处理
- `SecurityHeaders.h/.cc`：安全响应头
- `ExceptionHandlerSetup.h/.cc`：全局异常处理
- `OpenApiSetup.h/.cc`：OpenAPI 生成配置
- `MigrationRunner.h/.cc`：数据库迁移（`OAUTH2_AUTO_MIGRATE` 开关）
- `ControllerRegistration.h/.cc`：全部 15+1 个 controller 的 `registerController` 调用集中封装（`bootstrap::registerAllControllers()`）

`main.cc` 重写为精简装配版：加载配置 → `registerAllControllers()` → `setupCors/setupSecurityHeaders/setupExceptionHandler` → ErrorCatalog invariant 校验 advice → Hodor rate-limit advice → `initApiDocs()` + `setupOpenApi()` → `setupMigrations()` → `drogon::app().run()`。`OAuth2Server/CMakeLists.txt` 新增 `file(GLOB BOOTSTRAP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/*.cc")` 并加入 `add_executable`。

**踩坑与修复**：`main.cc` 精简后仍直接调用 `oauth2::controllers::OAuth2StandardController::initApiDocs()`（Task 20 决定保留的旧命名空间），但拆分时漏加了对应 include，导致 `error C2653: "oauth2" 不是类或命名空间名称` / `C3861: "initApiDocs" 找不到标识符`。修复：补上 `#include <authforge/drogon/controllers/OAuth2StandardController.h>`。

**范围说明**：本任务只针对 `OAuth2Server/main.cc`（design.md Task 25 明确指向 `apps/server/src/main.cc`）。`OAuth2Server/test/test_main.cc` 未拆分，也不在本任务范围——它有自己独立的初始化逻辑，不调用 `main.cc` 的任何函数，`bootstrap/*.cc` 只加入了 `OAuth2Server` 的 `add_executable`，未加入 `test/CMakeLists.txt`，`OAuth2Test_test` 编译验证确认不受影响。

**验证结果**：

- 全量编译（`OAuth2Server` + `OAuth2Test_test`）：通过，零错误（含 include 修复后的重新编译确认）
- `ctest -C Debug`：207/207 全绿，零回归
- 真实服务器 + curl 端到端验证（`OAuth2Server.exe` + `config.ci.json`，端口 5555）：
  - `GET /health` → 200 `{"status":"ok",...}`
  - `GET /.well-known/openid-configuration` → 200，完整 OIDC discovery 文档
  - `GET /login` → 200

**Task 25 完成**。main.cc 现在仅做装配，符合 design.md §6 的分层要求。

## 关键提醒

- 每个 slice/task 完成后必须跑全量测试套件（当前 124 个 CTest cases: 40 authforge-common-test + 38 authforge-common-testing-test + 1 OAuth2Tests(内含 310 DROGON_TEST cases/57113 assertions) + 45 Contract）确认零回归
- 遵循 H6 指导：小步提交，每个 commit message 详细记录做了什么、为什么、验证了什么
- 发现的生产缺陷要记录但不擅自修复（除非明确要求或修复成本极低且不改变行为契约）
