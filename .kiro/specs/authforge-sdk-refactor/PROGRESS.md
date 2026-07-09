# authforge-sdk-refactor 执行进度记录

> 本文件用于在上下文压缩/session 中断后快速恢复状态。每个里程碑/Task 完成后更新。

## 分支与 PR

- 分支：`feat/m0-conan-migration`
- PR：#11（GitHub）
- **本地已提交但尚未推送**的 commit 从 `0c0ec9d`（Task 13）到 `60373d0`（Task 17 slice 3: 仓储接口迁移）
- 上次推送到远端的 commit：`e5c097d`（M1 saveTokenPair 修复）
- 用户指示："CI 不急，按计划往后推进" —— 暂不推送，先把 M2a/M2b 等后续任务在本地做完再统一验证推送

## 里程碑完成状态

- [x] M0（Task 1-6）：Conan 迁移、drogon_ctl、OpenSSL 3.5、CMakePresets、paths.env、三平台 CI — 已推送，CI 全绿
- [x] M1（Task 7-12）：仓储接口拆分、缓存装饰器、契约测试套件 — 已推送，CI 全绿（含 3 个真实生产缺陷修复：Redis validateClient 崩溃、hasUserConsent 逻辑反、saveTokenPair 竞态）
- [x] M2a（Task 13-16）：libs/common + 端口 + 去 drogon::utils + 测试链接过渡 — **本地完成，未推送**
- [ ] M2b（Task 17-18）：libs/oauth2 + ORM 归位 — **进行中**（Task 17 slice 1 完成，见下）
- [ ] M2.5（Task 19）：libs/identity
- [ ] M3 起：未开始

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

**发现的 pre-existing 缺陷（已记录，未修复，非本任务范围）**：
1. `oauth2::utils::sha256()` (CryptoUtils.h) 十六进制大小写处理错误 —— 确认为死代码，零生产调用点
2. **`TokenService::generateSha256Hash` 不符合 RFC 7636**——对十六进制字符串本身做 base64 编码，而非对原始摘要字节编码。标准 PKCE 客户端会验证失败。已用字节级对比确认这是迁移前就存在的缺陷，非本次引入。**建议后续单独立项修复**。

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

### Task 17 剩余 slice（未完成，下一步）
- **`TokenService` 迁移**（已读取现有实现，规划见下）：
  - 现有 `OAuth2Plugin/include/oauth2/services/TokenService.h` + `.cc` 直接 `#include <drogon/drogon.h>`（`drogon::app().getCustomConfig()` 取 issuer）+ 依赖旧 `IOAuth2Storage`（上帝接口，未拆）+ 直接用 `AuditLogger`（Drogon 相关）+ 直接 new 一个 `static oauth2::adapters::OpenSslCryptoProvider` 实例（而非注入端口）
  - 迁移需要：① 把 `IOAuth2Storage` 依赖换成 4 个新 `authforge::oauth2::repository::I*Repository`（或先只迁移用到的方法子集）② `drogon::app().getCustomConfig()` 取 issuer 需要通过某种配置端口注入（目前 `common::ports` 里没有配置端口，需要新增或者把 issuer 作为构造参数传入——构造参数注入更简单，倾向此方案）③ `AuditLogger::log` 调用需要确认是否已去 Drogon 化（Task 14 slice 8 只迁移了 `AuditLogger.cc` 内部的 LOG_* 调用，`AuditLogger` 本身的 DB/HTTP 依赖被保留，需要重新检查它是否可以在 libs/oauth2 里使用，或者需要经 observability 端口）④ `generateSha256Hash`/`validatePkceCodeVerifier` 应该改为调用新建的 `oauth2::pkce` 模块（Task 17 slice 1）——这会**同时修复**已记录的 RFC7636 缺陷，需要用户确认是否接受此行为变更（有客户端兼容性影响，见下方"发现的缺陷"章节）
  - **建议**：这是本 Task 剩余部分中风险最高、决策点最多的一步，建议先跟用户确认 PKCE 缺陷修复的决策，再动手迁移
- `AuthorizationService`（需新建，目前在 OAuth2Plugin 里没有直接对应文件，需要从 `OAuth2StandardController`/`ClientService` 相关逻辑中提炼）
- `access/`（consent + scope 决策引擎）
- `model/`（聚合，`AuthorizationGrant`/`TokenPair`/`Client` 三个聚合尚未建；目前只有裸 DTO）
- 把具体仓储实现（`MemoryClientRepository`/`RedisClientRepository`/`PostgresClientRepository` 等）从 `oauth2::` 命名空间迁移到 `authforge::oauth2::`，并让生产代码（`OAuth2Plugin.cc`）切换过去，淘汰旧接口——这是让新接口"生效"的最后一步，尚未开始

## 下一步

- 继续 Task 17 剩余 slice（见上）
- Task 18：ORM 模型迁移到 storage-postgres + DTO 映射（14 个 ORM 模型、`models_backup/` 忽略不迁移）
- 这是 M2a 之后风险第二高的任务（真正把 Domain 逻辑从 OAuth2Plugin 迁出）

## 关键提醒

- 每个 slice/task 完成后必须跑全量测试套件（当前 124 个 CTest cases: 40 authforge-common-test + 38 authforge-common-testing-test + 1 OAuth2Tests(内含 310 DROGON_TEST cases/57113 assertions) + 45 Contract）确认零回归
- 遵循 H6 指导：小步提交，每个 commit message 详细记录做了什么、为什么、验证了什么
- 发现的生产缺陷要记录但不擅自修复（除非明确要求或修复成本极低且不改变行为契约）
