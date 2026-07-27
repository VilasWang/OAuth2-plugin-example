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
- **ORM 模型重复**：`OAuth2Plugin/src/models/` + `OAuth2Plugin/include/oauth2/storage/` 的 16-17 个模型与 `libs/storage-postgres/.../models/` 同类名两份——Task 18 已迁，`OAuth2Plugin/` 那份是死副本。

## 阶段执行（A1→A9 逐 commit，每步 build 绿；运行时 gate 在风险 phase 末尾；全量 ctest 仅 Phase 7 末一次）

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

### Phase 1 — 删死代码（零风险清场）= A1
**删 `OAuth2Plugin/` 的重复 ORM 模型副本**：
- 删 `OAuth2Plugin/src/models/*.cc` + `OAuth2Plugin/src/models/*.h`（16-17 对）+ `OAuth2Plugin/include/oauth2/storage/` 下的 ORM model 头（与 `libs/storage-postgres/.../models/` 重复的部分）
- 核实：`OAuth2Plugin/CMakeLists.txt` 的 GLOB 去掉 models 后仍能编译；所有 `#include <oauth2/.../Oauth2Clients.h>` 等改为指向 `libs/storage-postgres` 的 `<authforge/storage/postgres/models/...>`
- 验证：build 绿
- **风险**：若有代码仍 include 旧路径，需逐点改。先 grep `<oauth2/` + 模型类名找全调用点。

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
- `IdentityService.h/.cc`（services/）迁 `libs/drogon/services/` 或 `apps/server/`
- 消费者改链：`OAuth2Server` + `OAuth2Server/test` 的 `target_link_libraries` 去掉裸 `OAuth2Plugin`，靠 `authforge::drogon` 传递
- **Phase 4 末运行时 gate（强制）**：起服务器确认 config 反射仍加载 `OAuth2Plugin` 插件——启动日志必须含 `OAuth2Plugin initialized with storage type: postgres` + `Controller/filter plugin dependencies wired`；curl `/health`→200、`/login`→200（login.csp 渲染）、`/.well-known/openid-configuration`→200。**这是整个计划的关键 gate**——OBJECT→STATIC 合并后符号可见性若出问题，build 完全正常但启动「plugin not found」崩溃，只有运行时能抓到。若失败，回退到 Phase 3 末 tag，改用保守方案（OAuth2Plugin 保留为独立 OBJECT 库，仅迁物理位置不合并）。

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

### Phase 6 — 命名一致性收尾 = A9
- `OAuth2Metrics.h` → 类名 `Metrics`：改名文件为 `Metrics.h` 或类改 `OAuth2Metrics`（按 §5.8「文件名=主类名」定）
- `ScopeDecision.h`：核实 `ScopeDecision` 类型是否存在；若无，文件改名匹配真实主类（`ScopeValidationSummary`？）
- `Dto.h`：6 DTO 聚合，按 §5.8 判断是否拆分或保留聚合头
- `Rules.h` → `Rule.h`（单复数对齐）
- 其余 §5.8 残余

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

## Critical files（按阶段）

- **Phase 1**：删 `OAuth2Plugin/{src/models,include/oauth2/storage/}` 重复模型；改模型 include 调用点
- **Phase 2**：新建 `libs/storage-redis/`；迁 `OAuth2Plugin/{src,include/oauth2}/storage/{Postgres,Redis,Memory,Cached}*`；identity 仓储接口→`libs/identity`；**末运行时 gate**
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
