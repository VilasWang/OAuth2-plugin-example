# 目录结构重组执行计划（authforge-sdk-refactor M8 收尾）

> 权威来源：design.md §6（目标目录）、§5.7（OAuth2Plugin 类名不改，决策 A）、§5.8（命名一致性）。
> 用户决策（已定）：① `OAuth2Plugin` 类迁 `libs/drogon/plugin/`，目录 `OAuth2Plugin/` 删除（决策 1b）；② 分阶段 A1→A9 逐 commit，每步 build 绿，ctest 最后一次；③ `storage-redis` + `observability-prometheus` 现在建。
> 顺序约束：**功能代码（Phase 1-6）优先，测试层（Phase 7）最后**。
> 验证策略：每 commit build 绿；改动链接语义/目录结构的 phase 末尾加运行时 gate（起服务器+curl）；全量 ctest 仅 Phase 7 末一次（决策 2，但运行时 gate 提前到各风险 phase）。
> 性质：design §14.1「M8 原子、迁移前打 tag」——本计划是 M8 的代码层执行步骤。

## 现状一句话

`libs/*` 七个 SDK 库就位且命名/命名空间正确；但 `OAuth2Plugin/` 是「遗产大杂烩」——既装设计要求保留的 DI 装配器（§5.7 类名不改），又囤着该迁未迁、该删未删的遗产（重复 ORM、3 套存储实现、adapters、utils…）。顶层 `apps/`、`frontends/`、`tests/` 三个目标目录完全没建。

## 关键机制点（执行前必读）

- **`OAuth2Plugin` 是 OBJECT 库**，被三方链接：`OAuth2Server`（PRIVATE）、`OAuth2Server/test`（PRIVATE）、`libs/drogon`（PUBLIC）。决策 1b 迁入 `libs/drogon/plugin/` 后，`libs/drogon` 不再「链接自己」——OBJECT 库的 `.obj` 直接合并进 `libs/drogon` target，需从 `libs/drogon` 的 `target_link_libraries` 移除 `OAuth2Plugin` 项，改用 `target_sources(... $<TARGET_OBJECTS:...>)` 或直接 GLOB 那些源进 `libs/drogon`。其余两消费者（server/test）改为链 `authforge::drogon`（已含装配器）。
- **config 反射依赖 `OAuth2Plugin` 类名字符串**（4 份 `config.*.json` 的 `plugins[].name`）——**类名不改、include 路径 `<oauth2/plugin/OAuth2Plugin.h>` 可改**（include 路径是编译期，反射是运行期类名，互不影响）。但 `::OAuth2Plugin`（全局命名空间）必须保留——所有 `getPlugin<::OAuth2Plugin>()` 调用点不变。
- **Redis 实现是完整一套**（9 文件：7 repo + Base + Bundle），可整体迁新建 `libs/storage-redis`。
- **ORM 模型重复**：`OAuth2Plugin/src/models/` + `OAuth2Plugin/include/oauth2/storage/` 的 16-17 个模型与 `libs/storage-postgres/.../models/` 同类名两份——Task 18 已迁，`OAuth2Plugin/` 那份是死副本。（Phase 1 已删）
- **两套 identity 仓储接口并存（Task 39 前置）**：legacy `oauth2::I{User,Role,SubjectMapping}Repository`（int32，与 DB 一致）与 new `authforge::identity::I*Repository`（int64，脱离 DB）并存且类型不兼容。Phase 2 存储搬迁前必须先统一（Phase 1.5 = Task 39）。**决策（方向 Y）**：new 接口收回 int32 对齐 DB/ORM（DB 全 int4，int64 是凭空放宽），不反向改 DB 升 BIGINT。

## 阶段执行（Task 39 + A1→A9 逐 commit，每步 build 绿；运行时 gate 在风险 phase 末尾；全量 ctest 仅 Phase 7 末一次）

> 每个 commit 独立 build 绿。命名空间迁移统一为 `oauth2::` → `authforge::storage::*` / `authforge::drogon::*` / `authforge::common::*`。改 include 路径时同步改所有 `#include` 点（用 grep 找全）。

### 验证策略（修订：build 绿不够，关键节点必须运行时验证）

**仅 build 绿检测不到的 Drogon 运行时特性**：插件反射加载（config `plugins[].name`）、视图模板路径（drogon_create_views 生成的 CSP 定位）、`getPlugin<::OAuth2Plugin>()` 类型注册、filter 反射字符串路由。

故验证分三档：
- **每 commit**：build 绿（最低要求）。
- **改动链接语义 / 目录结构的 phase 末尾**（Phase 2 末、Phase 4 末＝A6 后、Phase 5 末＝apps/server 迁移后）：**运行时 gate**——真起服务器 + curl 验证（`/health` 200 确认插件初始化；`/oauth2/authorize` 缺 state→400 确认路由+filter 链活；`/login`→200 返回渲染的 login.csp HTML 确认视图路径；`/.well-known/openid-configuration`→200 确认 discovery 路由）。启动日志必须含 `OAuth2Plugin initialized` + `Controller/filter plugin dependencies wired`。
- **Phase 7 末**：全量 ctest（核对 276/277 基线）+ 上述运行时 gate。

> 原「ctest 仅最后跑一次」的判断已废弃——那会让 Phase 1-6 累积 namespace/存储/链接/目录变更期间仅靠 build 验证，Drogon 运行时崩溃（反射找不到符号、视图路径失效、类型注册破坏）到 Phase 7 才暴露，定位成本极高。运行时 gate 提前到各风险 phase 末尾。

### Phase 0 — 迁移前打 tag + 现状基线
- `git tag pre-m8-directory-restructure`（M8 原子性回退点）
- 记录当前 build 绿 + ctest 276/277（仅 Property4_3_1 pre-existing 失败）+ 运行时 gate 通过（curl 四路由）作为基线
- 无代码改动

### Phase 1 — 删死代码（零风险清场）= A1 ✅ 已完成
**删 `OAuth2Plugin/` 的重复 ORM 模型副本**：
- 删 `OAuth2Plugin/src/models/*.cc` + `OAuth2Plugin/src/models/*.h`（16-17 对）+ `OAuth2Plugin/include/oauth2/storage/` 下的 ORM model 头（与 `libs/storage-postgres/.../models/` 重复的部分）
- 核实：`OAuth2Plugin/CMakeLists.txt` 的 GLOB 去掉 models 后仍能编译；所有 `#include <oauth2/.../Oauth2Clients.h>` 等改为指向 `libs/storage-postgres` 的 `<authforge/storage/postgres/models/...>`
- 验证：build 绿
- **风险**：若有代码仍 include 旧路径，需逐点改。先 grep `<oauth2/` + 模型类名找全调用点。
- **完成说明（commit 待 Phase 1.5 后一起提交）**：38 个文件（19 对 .h/.cc）删除，全量 diff 确认与 `libs/storage-postgres` 逐字节相同，零外部引用（所有代码已 include 新路径），GLOB 自动排除，reconfigure + 全量 build 绿。

### Phase 1.5 — Task 39：identity 接口统一（Phase 2 的硬前置，方向 Y）

> **为什么插在 Phase 2 前**：Phase 2 要把 9 个 identity 存储实现从 `OAuth2Plugin/` 迁到 `libs/storage-*` 并改命名空间。但这 9 个实现继承 legacy `oauth2::I*Repository`（int32），与 new `authforge::identity::I*Repository`（int64）并存且类型不兼容——不先统一接口，存储搬迁要么制造循环依赖，要么留两套接口永远删不掉。Task 39 消除两套接口后，Phase 2 才能干净地把「单一 identity 接口」的实现归位。design.md Task 39 原本就在 M8，这里把它提前执行。

**根因（方向 Y 决策依据）**：DB 全是 int4（`users.id` SERIAL、`*_user_id`/`internal_user_id` INTEGER，无 BIGINT），ORM getter 全是 `int32_t`（`PrimaryKeyType = int32_t`）。**legacy int32 与 DB 一致、正确**；new 接口作者脱离 DB 凭空用 int64，导致每个实现进 ORM 前 `static_cast<int32_t>`（PostgresIdentityRepository/Mfa/WebAuthn/Social 共 10+ 处）。**根治方向 = 把 new 接口收回 int32，全链路对齐 DB/ORM**（用户选定的方向 Y），而非反向改 DB 升 BIGINT（本末倒置，且 int4 对 user-id 永不溢出）。

**目标终态**：单一 `authforge::identity::I*Repository`（int32，补齐 string 重载 + 写路径）+ 各后端实现（Postgres 已有，补 Redis/Memory）；legacy `oauth2::I*Repository` + 9 实现 + StorageCallbacks.h 全删；`IdentityService`/plugin/adapter 全切到 new 接口；全链路 int32 零 cast。

**子步骤（每步 build 绿，关键节点运行时 gate）**：

**1.5a. 收回 int 类型（方向 Y 核心，纯类型对齐，行为不变）**：
- `libs/identity/include/authforge/identity/`：6 个仓储接口（IUser/IRole/ISubjectMapping/IMfa/IWebAuthn/ISocialAccount）所有 `int64_t userId`/`int64_t internalUserId` → `int32_t`；`UserData.id` int64→int32；`ISubjectMappingRepository::OptionalIntCallback` 的 `optional<int64_t>`→`optional<int32_t>`
- `AuthService::AuthResult.internalId` int64→int32（`AuthService.h:39`，注释「Internal auto-increment ID」）
- 删 `libs/storage-postgres/src/Postgres{Identity,Mfa,WebAuthn,SocialAccount}Repository.cc` 里所有 `static_cast<int32_t>(userId)` + `int32_t userId32 = ...` 中间变量（直接用 int32 参数）
- 改 `libs/identity` 内部消费者（AuthService.cc `result.internalId = user.id`、RoleProvider、SubjectResolver `cb(static_cast<int32_t>(...))` 删 cast）+ 4 个测试 fake（AuthServiceTest 的 `InMemoryUserRepository` 等）
- 验证：build 绿 + `libs/identity` 测试绿 + 运行时 gate（Postgres 配置：登录/注册/MFA 流程不崩，identity 读路径走 int32 无 cast）

**1.5b. 补 new 接口缺的方法（让 new 接口成为 legacy 的超集）**：
- `authforge::identity::IRoleRepository`：加 `getRoles(const std::string &subject, RolesCallback&&)` 重载（legacy `getUserRoles(string)` 的对应，TokenService 偏好的 subject-string 路径）
- `authforge::identity::ISubjectMappingRepository`：加写路径 `createSubjectMapping(subject, int32_t internalUserId, provider, BoolCallback&&)` + `createUserForExternalLogin(externalId, provider, OptionalIntCallback&&)`
- `PostgresIdentityRepository`：实现这两个写方法（从 legacy `PostgresSubjectMappingRepository.cc` 移植逻辑——`createSubjectMapping` 用 `Mapper<Oauth2SubjectMappings>::insert`，`createUserForExternalLogin` 是 raw SQL `INSERT...RETURNING`，按 db-operations.md 核实是否属豁免或改 Mapper）+ 实现 IRoleRepository 的 string 重载（从 `PostgresRoleRepository` 移植 public_sub→int32 解析）
- 验证：build 绿 + 新方法的单元测试（覆盖 create + read 往返）

**1.5c. 补 Redis/Memory new identity 实现（消除 new 路径的后端缺口）**：
- 新建 `RedisIdentityRepository`（`libs/storage-redis/`，本 phase 先建包骨架或临时放 OAuth2Plugin）：镜像 legacy Redis 的占位行为（getUserInfo→nullopt、getRoles→`{"user"}`、getInternalUserId 走 Redis HGET、createSubjectMapping 走 HSET），实现补齐后的 new 接口
- 新建 `MemoryIdentityRepository`（`libs/storage-memory/`）：镜像 legacy Memory 的行为（stateless user、config-driven roles、subjectMappings map），实现 new 接口
- 验证：build 绿 + Memory 实现的单元测试（SubjectMappingTest/P0FunctionalityTest 的逻辑可复用）

**1.5d. 迁消费者到 new 接口**：
- `OAuth2Plugin.h/.cc`：`roleRepo_`/`userRepo_`/`subjectMappingRepo_` 类型 legacy→new（`authforge::identity::I*Repository`）；`initStorage` 的 bundle 选择改用 new 实现的 bundle（或临时 adapter）；`getUserInfo(string)` 转发方法内部改用 new 的 `findById`/`findByPublicSub` 分发
- `IdentityService.h/.cc`：`Repos` 结构 legacy→new；所有方法保持 int32（new 接口已 int32，无 cast）；`getUserRoles(string)`/`getInternalUserId`/`ensureSubjectMapping`/`handleFirstTimeLogin` 调 new 接口
- `StorageRoleProvider`/`StorageSubjectResolver`：持有的 repo 类型 legacy→new；`getRoles(string)` 调 new 的 string 重载；`resolve()` 调 new `getInternalUserId`
- plugin 公共转发方法签名保持 int32（间接消费者 TokenEndpointController/SessionController/AuthorizationFilter 不受影响）
  - **⚠️ 事后修正（Phase 3 期间发现，commit `efd8673` 已修）**：「间接消费者不受影响」只对了一半——编译确实不受影响，但 MfaController/SessionController 保留了 int64 局部变量/lambda 参数（`std::stoll` 解析的 userId、Task 24 slice 4 有意「加宽」的 `onValidated(int64_t internalId)` 桥），喂给已收回 int32 的 new 接口时产生 5 处 C4244 隐式收窄，违反 1.5 目标终态「全链路 int32 零 cast」。修复：两个 controller 的 user-id 局部/参数全部收回 int32_t、`std::stoll`→`std::stoi`（越界即拒为无效 MFA 会话）、删除双向 cast、更新「widened」过时注释。**教训：接口收窄类重构的验收应 grep 消费端残留的宽类型局部变量 + 检查 build log 的 C4244，而非仅确认签名兼容。**
- 验证：build 绿 + **运行时 gate（强制）**：Memory + Postgres 配置各起一次服务器，跑登录/注册/MFA/外部账号绑定全流程，确认 new 接口写路径（createSubjectMapping/createUserForExternalLogin）在产品里首次可用

**1.5e. 删 legacy（Task 39 收尾，清场）**：
- 删 `OAuth2Plugin/include/oauth2/storage/I{User,Role,SubjectMapping}Repository.h` + `StorageCallbacks.h`（只被这 3 个头 include，AuthorizationTransaction 是死副本）
- 删 9 个 legacy 实现：`Postgres/Redis/Memory{User,Role,SubjectMapping}Repository.{h,cc}`
- 删/改 3 个 Bundle 的 identity 部分（Bundle 改为聚合 new 实现）
- 改测试：`SubjectMappingTest.cc`/`P0FunctionalityTest.cc` 改测 new `MemoryIdentityRepository`
- 验证：build 绿 + 运行时 gate + grep 确认 `oauth2::IUserRepository`/`oauth2::IRoleRepository`/`oauth2::ISubjectMappingRepository`/`StorageCallbacks` 零残留

**Phase 1.5 末运行时 gate（强制，Task 39 是行为变更非纯重组）**：起服务器跑完整 identity 流程（注册→登录→拿 token→MFA→社交账号绑定），确认 new 接口全路径可用、legacy 零残留。**这是整个计划中除 Phase 4 外第二个强制运行时 gate 的 phase**——因为涉及写路径迁移，build 绿不能保证运行时行为等价。

### Phase 2 — 存储归位（核心，最大块）= A2 + A3 + 新建包

**2a. 新建 `libs/storage-redis`**（决策 3）：
- 建 `libs/storage-redis/{include/authforge/storage/redis/,src/,CMakeLists.txt,test/}`
- 从 `OAuth2Plugin/src/storage/Redis*` + `OAuth2Plugin/include/oauth2/storage/Redis*`（9+9 文件）迁入
- 命名空间 `oauth2::` → `authforge::storage::redis`；include 路径 `<oauth2/storage/Redis*>` → `<authforge/storage/redis/Redis*>`
- CMake：`project(authforge-storage-redis)`、`STATIC`、alias `authforge::storage::redis`，依赖 `Drogon::Drogon`（Redis client）、`authforge::oauth2`、`authforge::identity`（仓储接口）、`authforge::common`
- 顶层 `CMakeLists.txt` 加 `add_subdirectory(libs/storage-redis)`；`paths.env` 加 `LIBS_STORAGE_REDIS_DIR`
- **插入位置理由**：Redis 实现目前在 `OAuth2Plugin/`，与 Postgres/Memory 并列；先建包再迁代码，让 Phase 2b/2c 有目标落点。

**2b. Postgres 存储实现迁 `libs/storage-postgres`**：
- `OAuth2Plugin/src/storage/Postgres*` + `OAuth2Plugin/include/oauth2/storage/Postgres*`（9+9：Client/Consent/Grant/Role/SubjectMapping/Token/User + Base + Bundle）迁入 `libs/storage-postgres/{src,include/authforge/storage/postgres/}`
- 命名空间 `oauth2::` → `authforge::storage::postgres`；include 路径同步
- identity 仓储接口（`IRoleRepository`/`ISubjectMappingRepository`/`IUserRepository`）若在此，迁 `libs/identity/include/authforge/identity/repository/`

**2c. Memory 存储合并（A3）**：
- `OAuth2Plugin/.../Memory{Role,SubjectMapping,User}Repository` + `MemoryRepositoryBundle`（oauth2 味，`::oauth2`）迁入 `libs/storage-memory`，与现有 `Memory{Client,Consent,Grant,Token}Repository`（`authforge::storage::memory`）合成一套 7 个 repo
- 命名空间统一 `authforge::storage::memory`；`MemoryRepositoryBundle` 扩展聚合全部 7 个

**2d. CachedClientRepository 归位**：迁 `libs/storage-redis`（缓存装饰器属 redis 层，design §7.4）或独立判断。

**每子步 build 绿**。Phase 2 末 `OAuth2Plugin/src/storage/` + `OAuth2Plugin/include/oauth2/storage/` 应清空（仅剩还活着的接口或全删）。
- **Phase 2 末运行时 gate**：存储层是大面积 namespace 迁移 + 包新建，build 绿后必须起服务器 + curl `/health`（插件初始化走存储构造）+ `/oauth2/authorize` 缺 state→400（存储链路活）确认无运行时崩溃。

### Phase 3 — `OAuth2Plugin/` 其余遗产归位（A4 + A5，**不含 A6**）

> A6（OAuth2Plugin 类迁移 + OBJECT→STATIC 合并）是最高风险操作，独立为 Phase 4，与 A4/A5 解耦——失败可干净回退到 Phase 3 末，不牵连 adapter/utils 归位。

**3a. adapters 迁 `libs/drogon/adapters/`（A4）**：
- 8 个 adapter（DrogonAuditSink/Logger/Metrics、OpenSslCrypto/Uuid、StorageRoleProvider、StorageSubjectResolver、SystemClock）从 `OAuth2Plugin/include/oauth2/adapters/` + `src/adapters/` 迁 `libs/drogon/include/authforge/drogon/adapters/` + `src/adapters/`
- include 路径 `<oauth2/adapters/>` → `<authforge/drogon/adapters/>`（消除路径↔命名空间错配）
- StorageRoleProvider/SubjectResolver 对存储仓储的依赖此时已指向新 `authforge::storage::*`（Phase 2 完成）

**3b. utils/error/config/observability/validation/filters 归位（A5）**：
- error (7) → `libs/common/include/authforge/common/error/`（若未在）
- observability (AuditLogger/OAuth2Metrics) → `libs/drogon/observability/` 或 `libs/common/observability/`（按是否 drogon 依赖定）
- validation (RuleEngine/RuleSet/Rules/HttpResponder) → `libs/drogon/validation/`（已部分在，合并）
- filters (3) → `libs/drogon/filters/`（已部分在，合并）
- config (ConfigManager/ConfigTypes) → `apps/server/` 或 `libs/drogon/config/`（产品配置关注点）
- utils (CryptoUtils/EmailNormalizer/EmailService/PasswordHasher/SubjectGenerator/TotpUtils) → 按依赖定：纯逻辑→`libs/common` 或 `libs/identity`，drogon 依赖→`libs/drogon`

**Phase 3 末 build 绿即可**（纯代码归位，未动链接语义/目录结构；OAuth2Plugin 类仍在原位，config 反射不受影响）。A6 独立 phase 才需运行时 gate。

### Phase 4 — A6 独立：`OAuth2Plugin` 类迁移 + OBJECT→STATIC 合并（**最高风险，单独 phase**）

> 此 phase 只做这一件事。前不夹 A4/A5，后不夹目录移动。失败回退干净（回 Phase 3 末 = OAuth2Plugin 类仍在 `OAuth2Plugin/` OBJECT 库原状）。

- `OAuth2Plugin.h/.cc` + `OAuth2CleanupService.h/.cc` 迁 `libs/drogon/{include/authforge/drogon/plugin/,src/plugin/}`
- **类名 `OAuth2Plugin` 保留**（全局命名空间 `::OAuth2Plugin`，config 反射依赖）；include 路径 `<oauth2/plugin/OAuth2Plugin.h>` → `<authforge/drogon/plugin/OAuth2Plugin.h>`
- `libs/drogon/CMakeLists.txt`：把 `OAuth2Plugin` 从 `target_link_libraries(... PUBLIC OAuth2Plugin)` 移除，改 `target_sources` 直接纳入这些源（OBJECT→合并进 STATIC `authforge-drogon`）
- **顶层 `OAuth2Plugin/CMakeLists.txt` 删除**，`OAuth2Plugin/` 目录清空后删
- `IdentityService.h/.cc`（services/）迁 `libs/drogon/services/`（**已定案，排除 apps/server**）
  - 定案依据（design.md 推演）：IdentityService 是横跨 oauth2×identity 两个限界上下文的聚合服务（consent/scope 策略属 oauth2 §5.3，subject 映射/首登属 identity），§4.1 铁律 2「oauth2 与 identity 互不编译依赖」排除两个 Domain 包；唯一消费者 OAuth2Plugin.cc（DI 装配器，Phase 4 后住 libs/drogon）排除 apps/server（库不能依赖应用）——Adapter 层是唯一合法落点
  - 命名空间 `authforge::identity` 本 phase 不动；**Phase 6（A9）改 `authforge::drogon::IdentityService`** 消除路径↔命名空间错配（改动面：自身 2 文件 + OAuth2Plugin.h/.cc 约 12 处，无反射约束）
  - **设计终态备忘（超出本计划范围，显式记录免遗忘）**：按 design.md §5.2 方案 A，IdentityService 应最终解散——consent+scope 策略入 oauth2 决策服务、subject 映射/首次登录入 identity、插件经 `common::ports` 消费、apps/server 做唯一装配点；属行为级重构，待目录重组全部完成后另立任务
- 消费者改链：`OAuth2Server` + `OAuth2Server/test` 的 `target_link_libraries` 去掉裸 `OAuth2Plugin`，靠 `authforge::drogon` 传递
- **Phase 4 末运行时 gate（强制）**：起服务器确认 config 反射仍加载 `OAuth2Plugin` 插件——启动日志必须含 `OAuth2Plugin initialized with storage type: postgres` + `Controller/filter plugin dependencies wired`；curl `/health`→200、`/login`→200（login.csp 渲染）、`/.well-known/openid-configuration`→200。**这是整个计划的关键 gate**——OBJECT→STATIC 合并后符号可见性若出问题，build 完全正常但启动「plugin not found」崩溃，只有运行时能抓到。若失败，回退到 Phase 3 末 tag，改用保守方案（OAuth2Plugin 保留为独立 OBJECT 库，仅迁物理位置不合并）。

> **✅ Phase 4 完成（commit `0699133`，2026-07-28）**：6 文件 git mv（rename 相似度 99-100%）；全仓 `<oauth2/` include 归零（50 文件字节级替换）；OBJECT→STATIC 合并 + 3 处 CMake 改链 + paths.env/脚本 rebase + 功能性残留清零 + `OAuth2Plugin/` 目录删除。全量 build 0 error / LNK4006=0 / C4244=0；**运行时 gate 绿**：启动日志两条关键行均出现，`/health` `/login` `/.well-known/openid-configuration` 全 200——符号保活机制（bootstrap `getPlugin<::OAuth2Plugin>()`）验证成立，未触发回退。回退 tag `restructure-phase3-end` 保留。

### Phase 5 — 顶层目录迁移 = A7 + A8 + .codebuddy/.claude 同步

**5a. `OAuth2Server` → `apps/server`（A7）**：
- 目录移动：`OAuth2Server/{main.cc,bootstrap/,src/organization/,SchemaManager.*,config*.json,model.json,openapi.yaml,sql/,views/,docs/}` → `apps/server/{src/main.cc,src/bootstrap/,src/organization/,config/,migrations/}`
- CMake target 改名 `OAuth2Server` → `authforge-server`（或 `apps-server`）；`project(OAuth2Server)` → `project(authforge-server)`
- `paths.env`：`SERVER_BUILD_SUBDIR`/`SERVER_BINARY_NAME`/`OAUTH2_SERVER_DIR` 改新值
- `views/`（login.csp/consent.csp）落点：`apps/server/views/`（运行期模板查找，路径随 drogon_create_views 配置）

**5b. 前端 → `frontends/`（A8）**：
- `OAuth2Admin/` → `frontends/admin/`、`OAuth2Frontend/` → `frontends/user/`（纯移动，无代码改）
- 前端 build/dev 路径在 docker/nginx/scripts 里对齐（与 Task 42/43 协调）

**5c. `.codebuddy/` + `.claude/` 配置同步（修订：必须在本 phase，不能留最后）**：
- **rules 的 `paths:` 触发器失配修复**（最高优先，否则规则不再自动加载）：
  - `.codebuddy/rules/data-access.md` paths: `OAuth2Plugin/**/storage/**,OAuth2Server/**/*.cc` → 新路径
  - `.codebuddy/rules/db-operations.md` paths: `OAuth2Plugin/**,OAuth2Server/**` → 新路径
  - `.codebuddy/rules/dev-workflow.md` paths: `OAuth2Server/**,OAuth2Admin/**,OAuth2Frontend/**` → `apps/server/**,frontends/**`
  - `.codebuddy/rules/orm-models.md` paths: `OAuth2Plugin/**/models/**,...` → `libs/storage-postgres/**/models/**`
  - `.claude/rules/*.md` 同步（与 .codebuddy 镜像）
- **settings.json pre-commit hook**：`.codebuddy/settings.json:34` + `.claude/settings.json` 的 `cd build/OAuth2Server && ctest` → 新构建产物路径（`build/authforge-server` 或经 paths.env）
- **agents/skills 文件引用**：23 个 `.codebuddy` 文件（skills/agents）+ 对应 `.claude` 文件里的 `OAuth2Server`/`OAuth2Plugin` 路径引用全部改为新路径
- `.codebuddy` 与 `.claude` 结构镜像，两套必须**同步改**（漏一套会导致两个 agent 环境规则不一致）
- **Phase 5 末运行时 gate**：目录迁移 + target 改名 + paths.env 变更后，起服务器确认构建产物路径正确、视图模板路径仍有效（`/login`→200 渲染 login.csp）、pre-commit hook 路径不失配。

**5d. scripts/CI/docker 路径对齐**（Task 42/43 范围）：`.github/workflows/ci-*.yml`、`deploy/docker/Dockerfile`、`docker-compose*.yml`、`scripts/backend/*` 里的硬编码 `OAuth2Server`/`build/OAuth2Server` 改新路径（`.codebuddy`/`.claude` 已在 5c 处理，不重复）。这些是 CI/部署关注点，可在本 phase 同 commit 顺带改，或单独 commit。

> **✅ Phase 5 完成（2026-07-28）**：5a `OAuth2Server`→`apps/server`（src/、config/、migrations/、seed/、views/、test/ 内部重组）+ target/产物改名 `authforge-server` + paths.env 全部值更新（`SQL_DIR` key 删除，消费者同步清理）；5b 前端→`frontends/{admin,user}`；5c `.codebuddy`+`.claude` 双侧同步（rules paths 触发器、4 份 settings hook `cd build/apps/server`、agents×6 路径表映射到 libs/实际布局、skills×16 + CODEBUDDY.md/CLAUDE.md；`getPlugin<OAuth2Plugin>()` 类名保留）；5d CI×3/Dockerfile/compose×2/.vscode/tools/README 对齐（容器内 `/app/sql/*` 布局不变）。全量 build 0 error / LNK4006=0 / C4244=0；**运行时 gate ✅**：`/health` 200（DB connected）、`/login` 200 渲染 login.csp、`/.well-known/openid-configuration` 200。回退 tag `restructure-phase4-end`。

### Phase 6 — 命名一致性收尾 = A9
- `OAuth2Metrics.h` → 类名 `Metrics`：改名文件为 `Metrics.h` 或类改 `OAuth2Metrics`（按 §5.8「文件名=主类名」定）
- `ScopeDecision.h`：核实 `ScopeDecision` 类型是否存在；若无，文件改名匹配真实主类（`ScopeValidationSummary`？）
- `Dto.h`：6 DTO 聚合，按 §5.8 判断是否拆分或保留聚合头
- `Rules.h` → `Rule.h`（单复数对齐）
- 其余 §5.8 残余

> **✅ Phase 6 完成（2026-07-28）**：`OAuth2Metrics.h/.cc` → `Metrics.h/.cc`（类名 `Metrics` 不变，唯一真实 includer 为 .cc 自身，另修 3 处活代码注释路径）；`Rules.h` → `Rule.h`（主类 `struct Rule`，3 个 includer 同步：RuleSet.h/RuleEngine.h/EmailNormalizerTest.cc）；`ScopeDecision.h` 核实 `enum class ScopeDecision` 存在 → no-op；`Dto.h` 用户决策保留聚合头 + 文件头追加 §5.8 豁免注释（已澄清：手写领域 DTO，非 drogon_ctl ORM 模型）。§5.8 残余 sweep：扫描 libs 174 个头文件，11 个标记项全部判定无需改名（6 个自由函数工具头 + 4 个有意聚合头 ErrorTypes/EmailService/SocialAuthService/ConfigTypes + Dto.h 已豁免）。CMake GLOB_RECURSE 无需改，reconfigure 即可。全量 build 0 error / LNK4006=0 / C4244=0；**运行时 gate ✅**：`/health` 200（DB connected）、`/.well-known/openid-configuration` 200、`/login` 200 渲染 login.csp。旧名 grep 仅剩 docs/design 历史存档（豁免，与 Phase 5 一致）。回退 tag `restructure-phase5-end`。

### Phase 7 — 测试层（最后）= B1 + B2
**一次性 ctest 验证在此阶段末执行**（用户决策 2；前 6 个 phase 末尾的运行时 gate 已覆盖 Drogon 运行时风险，ctest 在此集中验证全部单元/契约/集成测试零回归）。

**7a. `OAuth2Server/test/` → `tests/`（B1）**：
- 74 文件按 design §6 分流：`contract/`→`tests/contract/`、`integration/`→`tests/integration/`、`e2e/`→`tests/e2e-backend/`、`unit/` 里属 libs 自带测试的→各 `libs/*/test/`、`security/`→`tests/security/`、`performance/`→`tests/performance/`
- 单个 `OAuth2Test_test` GLOB 二进制 → 按层拆分（design §10 测试矩阵）；DROGON_TEST 运行器保留
- `test_main.cc` 的内联 controller 注册块随目录迁移
- CMake：顶层 `tests/CMakeLists.txt` 编排

**7b. 测试命名空间统一（B2）**：
- 25 个测试文件的 `namespace oauth2` 引用随 A 层迁移一起改（多数在 Phase 2-3 改存储/adapters 时已连带，这里收尾 TestBase.h/test_categories.h/contract/*.cc）

**7c. 终验**：build 绿 + **ctest 全量**（核对 276/277 基线，仅 Property4_3_1 pre-existing 允许失败）+ 运行时 curl 全路由存活 + config 反射插件加载成功。

> **✅ Phase 7 完成（2026-07-28）**：7a 79 文件 git mv（R 状态）`apps/server/test/` → 顶层 `tests/`（`e2e/`→`tests/e2e-backend/`；用户决策：**单二进制 + 目录分流**，二进制改名 `authforge-tests`，ctest 测试名 `OAuth2Tests` 与 45 个 `Contract.` add_test 名单保留不变，按层拆二进制推迟为后续独立任务；用户决策：27 个 unit 测试全部迁 `tests/unit/`，DROGON_TEST 框架不改写）。CMake：顶层 `tests/CMakeLists.txt` 自编排（需自带 `find_package(Drogon)`——imported target 目录作用域，tests/ 已是 apps/server 的兄弟目录）；apps/server 挂载点删除、根 CMakeLists 挂 `add_subdirectory(tests)`。7b：测试侧 `oauth2::test*` → `authforge::test*`（6 头 + 10 cc）；生产侧 EmailService `namespace oauth2` → `authforge::drogon::utils`（5 文件）。**命名空间遮蔽陷阱**：`authforge::test::*` 块内非全限定 `drogon::` 被解析为 `authforge::drogon`，ContractFixtures.h/TestBase.h 内部引用改 `::drogon::` 全限定。外部引用面：CI×3、settings×2、agents×2、skills×2、CLAUDE/CODEBUDDY、docs×4、findings md×2、scripts README、libs 注释侧全量同步（PRD/docs/design/openspec/.kiro 历史存档豁免）。**附带回归修复**：Task 39（58aa738）给 `initAdminRoles` 加的 `isMember("admin_users")` gate 使 legacy「缺键时默认注入 admin→{admin,user}」分支变死代码，`Integration_P0_Plugin_General_Works` 失败——去掉 gate 恢复无条件调用（OAuth2Plugin.cc 两处），该回归因全量 ctest 推迟至 Phase 7 才暴露。全量 build 0 error / LNK4006=0 / C4244=0；**ctest 276/277 基线完全吻合**（唯一失败 = Property4_3_1 pre-existing）；**运行时 gate ✅**：`/health` `/.well-known/openid-configuration` `/login`（渲染 login.csp）全 200，日志含 `OAuth2Plugin initialized with storage type: postgres` + `Controller/filter plugin dependencies wired`。已知注记：`tests/services/AuthServiceGetUserInfoTest.cc` 为死代码（不在任何 GLOB，迁移前后均不编译）。回退 tag `restructure-phase6-end`。

## Critical files（按阶段）

- **Phase 1**（✅）：删 `OAuth2Plugin/{src/models,include/oauth2/storage/}` 重复模型；改模型 include 调用点
- **Phase 1.5（Task 39，行为变更）**：6 个 identity 仓储接口 int64→int32 对齐 DB；补 new 接口缺方法（Role string 重载 + SubjectMapping 写路径）；补 Redis/Memory new identity 实现；迁 IdentityService/plugin/adapter 消费者；删 legacy 9 实现 + 3 接口 + StorageCallbacks.h；**末强制运行时 gate（identity 全流程）**
- **Phase 2**：新建 `libs/storage-redis/`；迁 `OAuth2Plugin/{src,include/oauth2}/storage/{Postgres,Redis,Memory,Cached}*`（Task 39 后 identity 实现已统一，无循环依赖）；**末运行时 gate**
- **Phase 3**：迁 `OAuth2Plugin/{adapters,utils,error,config,observability,validation,filters,services}` → 各 `libs/*`（A4+A5，build 绿即可）
- **Phase 4（A6 独立，最高风险）**：`OAuth2Plugin` 类→`libs/drogon/plugin/`；OBJECT→STATIC 合并；删 `OAuth2Plugin/` 目录；**末强制运行时 gate（config 反射）**
- **Phase 5**：`OAuth2Server/`→`apps/server/`（target 改名）；前端→`frontends/`；`.codebuddy`+`.claude` rules paths + hook 同步；**末运行时 gate**
- **Phase 6**：§5.8 改名
- **Phase 7**：`OAuth2Server/test/`→`tests/`；测试命名空间统一；**终验 ctest**

## 范围外（本计划不做）

- Task 33 arch-guard / Task 34 api-diff / Task 35 migration-check（M6 工具，独立任务）
- Task 36 release / Task 37 helm / Task 38 SBOM（M7 发布）
- Task 41 版本冻结 v1.0.0（命名稳定后做，本计划是它的前置）
- CI workflow 重构（Task 32）、docker/helm 重做（Task 43 深度部分）
- Property4_3_1 fingerprint 测试失败根因（pre-existing，独立分析）

## 风险与回退

- **Phase 4（A6）最大风险**：`OAuth2Plugin` OBJECT→合并进 `libs/drogon` STATIC 后，config 反射能否找到插件符号。**仅 build 检测不到**——必须靠 Phase 4 末运行时 gate（启动日志 + curl）。回退点：Phase 3 末（OAuth2Plugin 类仍在原位）。若反射失败，保守方案：OAuth2Plugin 保留为独立 OBJECT 库（仅迁物理位置到 `libs/drogon/plugin/`，不合并进 STATIC）。
- **Phase 2 命名空间迁移**：112 个文件的 `namespace oauth2` 改动面大，每子步 build 绿 + Phase 2 末运行时 gate 双安全网。
- **Phase 5 target 改名 + rules paths 失配**：破坏构建产物路径 + `.codebuddy`/`.claude` 规则触发器失配（规则不再自动加载）。paths.env + 4 个 rules 的 `paths:` 字段 + pre-commit hook 必须**同 phase 同步**，不能留到最后。回退点：Phase 4 末 tag。
