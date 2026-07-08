# IOAuth2Storage → 目标仓储 完整映射表（Task 7 + Task 8）

## 放置位置说明

放在 `OAuth2Plugin/include/oauth2/storage/` 下而非 design.md 内，理由：

- 这是**逐方法级**的实现清单（面向 Task 9/10/11 的执行者），比 design.md §7.1
  的架构级归属表（哪个新接口归哪个包）更细，塞进 design.md 会让设计文档变成
  施工清单。
- 与新增的 7 个接口头文件（Task 7 的 4 个 oauth2 仓储 + Task 8 的 3 个
  identity 仓储）放在同目录，方便日后 Task 9/10/11 实现类的作者对照，也方便
  M2b（Task 17）迁移 `libs/oauth2` / M2.5（Task 19）迁移 `libs/identity` 时
  整目录一起搬，不需要从 design.md 里再摘录。
- design.md §7.1 仍保留架构级表格（新接口 → 归属包 → 是否必须实现），本文件与
  之互补、不重复：本文件是"32 个原方法逐条落地去哪"，design.md 是"4+3 个新接口
  长什么样"。

## 方法计数说明

现有 `IOAuth2Storage.h`（`OAuth2Plugin/include/oauth2/storage/IOAuth2Storage.h`）
实测共 **32 个方法**：30 个纯虚 + 2 个带默认实现（`saveTokenPair`、
`createUserForExternalLogin`）。design.md §7 文中写的是「30 个方法（28 纯虚 + 2
带默认实现）」——与实际头文件逐条清点后的 32/30/2 不一致（少数 2 个大概率是把
`getUserRoles`/`getUserInfo` 的两个重载各算作 1 个方法而非 2 个）。本表以**实际
头文件**为准（32 个方法），design.md 的数字视为约数，不影响拆分方案本身。

## 完整映射表（32/32，零丢失）

| # | 原方法 | 签名要点 | 目标仓储 | 备注 |
|---|--------|---------|---------|------|
| 1 | `getClient` | `(clientId, ClientCallback)` | `IClientRepository` | |
| 2 | `validateClient` | `(clientId, clientSecret, BoolCallback)` | `IClientRepository` | |
| 3 | `saveAuthCode` | `(OAuth2AuthCode, VoidCallback)` | `IGrantRepository` | |
| 4 | `getAuthCode` | `(code, AuthCodeCallback)` | `IGrantRepository` | |
| 5 | `markAuthCodeUsed` | `(code, VoidCallback)` | `IGrantRepository` | |
| 6 | `consumeAuthCode` | `(code, redirectUri, AuthCodeCallback)` | `IGrantRepository` | 保留 RFC 6749 §4.1.3 redirect_uri 校验语义（单次消费 + 校验，见接口注释） |
| 7 | `saveAccessToken` | `(OAuth2AccessToken, VoidCallback)` | `ITokenRepository` | |
| 8 | `getAccessToken` | `(token, AccessTokenCallback)` | `ITokenRepository` | |
| 9 | `saveTokenPair` | `(at, rt, VoidCallback)`，**带默认实现**（顺序调用 saveAccessToken→saveRefreshToken） | `ITokenRepository` | 事务契约保留：默认顺序、Postgres 覆写用 DB 事务（design.md §7.2） |
| 10 | `saveRefreshToken` | `(OAuth2RefreshToken, VoidCallback)` | `ITokenRepository` | |
| 11 | `getRefreshToken` | `(token, RefreshTokenCallback)` | `ITokenRepository` | |
| 12 | `revokeRefreshToken` | `(token, VoidCallback)` | `ITokenRepository` | |
| 13 | `atomicRevokeRefreshToken` | `(token, RefreshTokenCallback)`，CAS 语义 | `ITokenRepository` | 由 `supportsCas()` 声明能力，供 F5 契约分档测试 |
| 14 | `revokeTokenFamily` | `(familyId, VoidCallback)`，级联撤销 | `ITokenRepository` | 事务契约保留（design.md §7.2） |
| 15 | `getUserRoles(const std::string&, StringListCallback)` | 按外部 userId 查角色 | `IRoleRepository` | design.md §5.3：「RBAC 数据（roles/permissions/user-role）→ identity（实现 common::ports::IRoleProvider）」 |
| 16 | `getUserRoles(int32_t, StringListCallback)` | 按内部 internalUserId 查角色 | `IRoleRepository` | 同上，两个重载都归 `IRoleRepository` |
| 17 | `getUserInfo(const std::string&, OptionalJsonCallback)` | 按外部 userId 查 userinfo | `IUserRepository` | design.md §7.1：`IUserRepository` 承接 getUserInfo |
| 18 | `getUserInfo(int32_t, OptionalJsonCallback)` | 按内部 internalUserId 查 userinfo | `IUserRepository` | 同上 |
| 19 | `getInternalUserId` | `(subject, provider, OptionalIntCallback)` | `ISubjectMappingRepository` | design.md §7.1 明确列出该方法归 `ISubjectMappingRepository` |
| 20 | `createSubjectMapping` | `(subject, internalUserId, provider, BoolCallback)` | `ISubjectMappingRepository` | 同上 |
| 21 | `createUserForExternalLogin` | `(externalId, provider, OptionalIntCallback)`，**带默认实现**（返回 nullopt） | `ISubjectMappingRepository` | 同上；默认实现语义（"Memory/Redis 不支持"）已在 `ISubjectMappingRepository.h` 中原样保留 |
| 22 | `saveAuthorizationTransaction` | `(AuthorizationTransaction, BoolCallback)` | `IGrantRepository` | |
| 23 | `getAuthorizationTransaction` | `(transactionId, TransactionCallback)` | `IGrantRepository` | |
| 24 | `deleteAuthorizationTransaction` | `(transactionId, VoidCallback)` | `IGrantRepository` | |
| 25 | `markTransactionConsumed` | `(transactionId, BoolCallback)` | `IGrantRepository` | |
| 26 | `hasUserConsent` | `(internalUserId, clientId, scope, BoolCallback)` | `IConsentRepository` | **F4**：签名改为 `UserRef` 代替裸 `int32_t internalUserId`（见 `UserRef.h`） |
| 27 | `saveUserConsent` | `(internalUserId, clientId, scope, BoolCallback)` | `IConsentRepository` | 同上，签名改用 `UserRef` |
| 28 | `revokeUserConsent` | `(internalUserId, clientId, scope, VoidCallback)` | `IConsentRepository` | 同上，签名改用 `UserRef` |
| 29 | `introspectToken` | `(token, TokenIntrospectionCallback)`，RFC 7662 | `ITokenRepository` | 易漏项，已核对（task 描述特别提醒） |
| 30 | `incrementIntrospectCount` | `(token, VoidCallback)` | `ITokenRepository` | 易漏项，已核对 |
| 31 | `revokeAccessToken` | `(token, revokedBy, VoidCallback)`，RFC 7009 | `ITokenRepository` | 易漏项，已核对 |
| 32 | `deleteExpiredData` | `()`，无参数，同步 | **拆分**：`IGrantRepository::purgeExpired()` + `ITokenRepository::purgeExpired()` | 见下方决策说明；`IClientRepository`/`IConsentRepository` 不加此方法 |

统计核对：`IClientRepository` 2 个（#1-2）；`IGrantRepository` 8 个（#3-6,
22-25）+ 1 个 purgeExpired；`ITokenRepository` 11 个（#7-14, 29-31）+ 2 个能力
标志 + 1 个 purgeExpired；`IConsentRepository` 3 个（#26-28）；`IRoleRepository`
2 个（#15-16）；`IUserRepository` 2 个（#17-18）；`ISubjectMappingRepository`
3 个（#19-21）。2 + 8 + 11 + 3 + 2 + 2 + 3 = 31，加上 #32
`deleteExpiredData`（拆给两个仓储，不重复计入原方法总数）= 32。**零丢失**。

## `deleteExpiredData` 拆分决策（#32）

原 `IOAuth2Storage::deleteExpiredData()` 是无参数、同步、"删除所有过期数据"的
单一方法，三个现有实现（Postgres/Redis/Memory）内部各自枚举过期的 auth code、
access token、refresh token 逐一清理。按聚合语义逐个仓储判断是否需要
`purgeExpired()`：

| 目标仓储 | 是否加 `purgeExpired()` | 理由 |
|---------|:---:|------|
| `IClientRepository` | **否** | `OAuth2Client` 结构无 `expiresAt` 字段，客户端注册不是"会过期"的数据，三个现有实现均未对 client 做过期清理 |
| `IGrantRepository` | **是**（纯虚，必须实现） | `OAuth2AuthCode.expiresAt` / `AuthorizationTransaction.expiresAt` 都是明确的过期字段，现有 `deleteExpiredData()` 在三个实现里都清理了过期 auth code |
| `ITokenRepository` | **是**（纯虚，必须实现） | `OAuth2AccessToken.expiresAt` / `OAuth2RefreshToken.expiresAt` 都是明确的过期字段，现有 `deleteExpiredData()` 在三个实现里都清理了过期 access/refresh token |
| `IConsentRepository` | **否** | consent 记录没有 TTL/expiresAt 语义，一旦授予就一直有效直到被 `revokeUserConsent` 显式撤销；三个现有实现的 `deleteExpiredData()` 均未触碰 consent 数据 |
| `IUserRepository`（Task 8） | **否** | 用户记录无 `expiresAt`/TTL 语义，三个现有实现均未清理用户行 |
| `IRoleRepository`（Task 8） | **否** | 角色分配数据无 `expiresAt`/TTL 语义，三个现有实现均未清理角色分配数据（已实际核对 Postgres/Redis/Memory 三份 `deleteExpiredData()` 源码，均只涉及 auth code / access token / refresh token） |
| `ISubjectMappingRepository`（Task 8） | **否** | subject mapping 一旦创建即永久有效（直到用户被删除，当前模型未涉及），无 `expiresAt`/TTL 语义，三个现有实现均未清理该数据（同上，已实际核对源码） |

编排方式：未来产品层 `CleanupService`（不在 Task 7/8 范围内实现）分别调用
`IGrantRepository::purgeExpired()` 与 `ITokenRepository::purgeExpired()`，
替代原来"一个方法清理一切"的做法。

## 给 Task 9/10/11/19 的衔接提示

- **Task 8**（identity 仓储接口，已完成）：#15-21 共 7 个方法（`getUserRoles`
  ×2 / `getUserInfo` ×2 / `getInternalUserId` / `createSubjectMapping` /
  `createUserForExternalLogin`）已落地为三个接口：`IUserRepository.h`
  （getUserInfo ×2）、`IRoleRepository.h`（getUserRoles ×2）、
  `ISubjectMappingRepository.h`（getInternalUserId /
  createSubjectMapping / createUserForExternalLogin）。三者均在
  `OAuth2Plugin/include/oauth2/storage/` 下、命名空间仍为 `oauth2`（物理迁移到
  `libs/identity` / `authforge::identity` 是 Task 19 的范围，见各文件头注释）。
  `createUserForExternalLogin` 的默认实现（返回 `nullopt`，"Memory/Redis 不
  支持"）已在 `ISubjectMappingRepository.h` 中原样保留，不强制要求所有实现都
  支持外部登录建号。三者均未加 `purgeExpired()`（已实际核对三份现有
  `deleteExpiredData()` 源码，均未清理用户/角色/subject mapping 数据，见上方
  决策表）。
- **Task 9**（拆分 `PostgresOAuth2Storage`）：需要同时实现 Task 7 的 4 个
  oauth2 仓储接口 + Task 8 的 3 个 identity 仓储接口（`IUserRepository` /
  `IRoleRepository` / `ISubjectMappingRepository`，接口已存在，直接实现即
  可）——原 Postgres 实现是唯一同时覆盖 32 个方法的类。`AuthorizationTransaction`
  的真实持久化在现有 `PostgresOAuth2Storage.cc` 里标注为"placeholder
  implementation"（见 `getAuthorizationTransaction`/
  `deleteAuthorizationTransaction` 的 `LOG_DEBUG` 注释），拆分时如实保留这个
  现状（不要在拆分过程中"顺手"补全，那是另一个任务的范围）。
- **Task 10**（拆分 Redis/Memory，已完成）：`RedisClientRepository` /
  `RedisGrantRepository` / `RedisTokenRepository` / `RedisConsentRepository`
  / `RedisUserRepository` / `RedisRoleRepository` /
  `RedisSubjectMappingRepository`（共享 mixin `RedisRepositoryBase`、聚合类
  `RedisRepositoryBundle`）与对应的 7 个 `MemoryXxxRepository` +
  `MemoryRepositoryBundle` 均已落地，均在 `OAuth2Plugin/include/oauth2/storage/`
  + `OAuth2Plugin/src/storage/` 下、命名空间仍为 `oauth2`（同 Task 9，物理迁移
  到 `libs/*` 是 M2b/M2.5/M3 的范围）。这些都是**新增**，未改动原有
  `RedisOAuth2Storage`/`MemoryOAuth2Storage`/`IOAuth2Storage`（现有生产路径
  仍在用旧接口，现有测试全绿）。
  - `supportsTransactions()`/`supportsCas()` 按后端真实能力老实声明——
    Redis 两者均声明 `false`（`saveTokenPair` 默认顺序实现且
    `saveRefreshToken` 现状是 no-op，`atomicRevokeRefreshToken` 是"先 get 后
    revoke"非原子两步）；Memory 两者均声明 `true`（`saveAccessToken`/
    `saveRefreshToken` 共享同一把 `recursive_mutex` 且回调在锁释放前同步内联
    触发，`saveTokenPair` 默认顺序实现在锁的保护下不会被其他线程观察到中间
    态；`atomicRevokeRefreshToken` 的 check+set 在同一把锁内完成，是真正的
    进程内 CAS）。详细推理见各接口头文件的类注释。
  - Memory 各拆分类的状态归属：`MemoryClientRepository`（`clients_` +
    `initFromConfig(clientsConfig)`）、`MemoryGrantRepository`（`authCodes_`
    + `transactions_`）、`MemoryTokenRepository`（`accessTokens_` +
    `refreshTokens_`）、`MemoryConsentRepository`（`userConsents_`）、
    `MemoryUserRepository`（无状态，占位 JSON 直接合成）、
    `MemoryRoleRepository`（`userRoles_` + `initFromConfig(adminConfig)`）、
    `MemorySubjectMappingRepository`（`subjectMappings_`）。原
    `MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig)` 的两个
    参数被拆到两个类：client 半部分归 `MemoryClientRepository`，admin/role
    半部分归 `MemoryRoleRepository`；`MemoryRepositoryBundle::initFromConfig`
    把两次调用重新聚合成一个入口，签名与原函数一致。
  - `purgeExpired()`：Redis 两个（Grant/Token）均保留原 `deleteExpiredData()`
    "no-op，依赖 Redis TTL"的现状；Memory 按原 `deleteExpiredData()` 实际清理
    的字段拆分：`MemoryGrantRepository::purgeExpired()` 只扫 `authCodes_`
    （`transactions_` 沿用原有的"读时惰性过期"，原 `deleteExpiredData()` 也
    从未主动扫过 `transactions_`），`MemoryTokenRepository::purgeExpired()`
    扫 `accessTokens_`/`refreshTokens_`。
- **Task 11**（缓存装饰器重构）：`IClientRepository` 是 per-repository 缓存
  的首选目标（读多写少）；`ITokenRepository`/`IGrantRepository` 按 design.md
  §7.4 建议不缓存或仅缓存否定结果，避免强一致性数据被缓存装饰器污染。
  `IUserRepository`/`IRoleRepository` 同样读多写少，也是候选缓存目标，具体
  策略留给 Task 11 执行者决定。
- **Task 19**（M2.5 `libs/identity` 迁移）：`IUserRepository.h` /
  `IRoleRepository.h` / `ISubjectMappingRepository.h` 三个文件届时需要整体从
  `OAuth2Plugin/include/oauth2/storage/` 迁移到 `libs/identity` 下、命名空间
  从 `oauth2` 改为 `authforge::identity`（或最终确定的命名空间），并把对
  `IOAuth2Storage::StringListCallback` / `OptionalJsonCallback` /
  `OptionalIntCallback` / `BoolCallback` 的复用替换为 identity 自己的类型定义
  （届时 identity 不应再依赖 oauth2 头文件）。`IRoleRepository` 的实现同时
  应考虑对接 design.md §5.2 提到的 `common::ports::IRoleProvider` 端口；
  `ISubjectMappingRepository` 的实现同时应考虑对接
  `common::ports::ISubjectResolver` 端口。
