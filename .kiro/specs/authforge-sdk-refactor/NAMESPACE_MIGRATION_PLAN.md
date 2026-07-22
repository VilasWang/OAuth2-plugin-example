# 命名空间统一迁移计划（Task 40 / B6）

> 状态：**计划已定稿，待执行**（M8 原子操作，迁移前打 tag，整体成功或整体回退）。
> 配套：design.md §5.1/§5.4/§5.8、§14.1（M8 原子性）、§5.7（H1 插件按名反射）。
> 起草于 A1-A3 完成后（commit `e721d57`，IOAuth2Storage.h 已删除）。

## 1. 目标命名空间结构（来自 design §5.1/§5.4/§5.8）

| 层 | 目标命名空间 | 目录 |
|----|------------|------|
| 共享内核 | `authforge::common`（+`::error`/`::config`/`::ports`/`::model`/`::testing`/`::utils`/`::observability`） | `libs/common` |
| OAuth2 SDK | `authforge::oauth2`（+`::protocol`/`::model`/`::repository`/`::access`/`::pkce`/`::jwk`） | `libs/oauth2` |
| Identity SDK | `authforge::identity` | `libs/identity` |
| Postgres 存储 | `authforge::storage::postgres` | `libs/storage-postgres` |
| Redis 存储 | `authforge::storage::redis` | `libs/storage-redis`（暂不存在，预留） |
| 内存存储 | `authforge::storage::memory` | （内存实现在 OAuth2Plugin/ 下，见下） |
| Drogon 绑定 | `authforge::drogon`（+`::controllers`/`::filters`/`::validation`/`::admin`/`::adapters`/`::observability`/`::services`） | `libs/drogon` |
| 产品 | （app，无命名空间导出） | `OAuth2Server/` |

## 2. 已定的 3 个决策点

- **决策 1（OAuth2Plugin 类）**：**保持全局命名空间**。design §5.7/H1 明确「保留 OAuth2Plugin 类名 + config plugins 块」——config 按字符串 `"OAuth2Plugin"` 反射加载插件，改名会破坏 4 份 config + 所有 `getPlugin<OAuth2Plugin>()`。`OAuth2Plugin` 类本身（全局）+ 其头 `oauth2/plugin/OAuth2Plugin.h` 名字都不动。
- **决策 2（identity 仓储）**：**拆分**——接口（`IRoleRepository`/`IUserRepository`/`ISubjectMappingRepository`）→ `authforge::identity::`；后端实现（`Memory/Postgres/Redis Role/User/SubjectMapping Repository`）→ `authforge::storage::{memory,postgres,redis}::`。镜像 oauth2 仓储的分层（接口在 `authforge::oauth2::repository`，实现在 storage 包）。
- **决策 3（legacy IdentityService）**：→ `authforge::identity::IdentityService`（与其他域服务命名空间方式一致——protocol 服务在 `authforge::oauth2::protocol`，identity 服务在 `authforge::identity::`）。文件位置暂不动（目录迁移是 Task 39）。
- **observability**（次要决策）：`oauth2::observability` → `authforge::common::observability`（AuditEvent 已在 common；AuditLogger 虽 Drogon 耦合，但归 common 与 AuditEvent 一致，Drogon 依赖在 common 已允许——common 可依赖 jsoncpp/Drogon 工具）。**评审风险见 §5**。

## 3. 完整遗留→目标映射表

### 3.1 `common::*`（加 `authforge::` 前缀）

| 遗留命名空间 | 目标 | 范围 |
|----|----|----|
| `common::error` | `authforge::common::error` | `OAuth2Plugin/include/oauth2/error/` + 全仓引用 |
| `common::config` | `authforge::common::config` | `OAuth2Plugin/include/oauth2/config/` + 全仓引用 |

### 3.2 `oauth2::<sub>` 子命名空间（机械化）

| 遗留 | 目标 | 目录 |
|----|----|----|
| `oauth2::adapters` | `authforge::drogon::adapters` | `OAuth2Plugin/include/oauth2/adapters/`、`src/adapters/`（OpenSslCryptoProvider/DrogonAuditSink/DrogonLogger/StorageRoleProvider） |
| `oauth2::filters` | `authforge::drogon::filters` | `OAuth2Plugin/include/oauth2/filters/`、`src/filters/`（AuthorizationFilter/OAuth2AuthFilter）**+ 33 处 `ADD_METHOD_TO("...oauth2::filters::AuthorizationFilter")` 字符串引用**（见 §6 运行时风险） |
| `oauth2::validation` | `authforge::drogon::validation` | `OAuth2Plugin/include/oauth2/validation/`、`src/validation/`（Rules/RuleSet/RuleEngine/HttpResponder） |
| `oauth2::observability` | `authforge::common::observability` | `OAuth2Plugin/include/oauth2/observability/`（AuditLogger/Metrics） |
| `oauth2::utils`（纯函数：EmailNormalizer/SubjectGenerator/TotpUtils/PasswordHasher） | `authforge::common::utils` | `OAuth2Plugin/include/oauth2/utils/` 的纯函数子集 |
| `oauth2::utils`（Drogon 耦合：CryptoUtils.h 用 drogon::utils） | `authforge::drogon::utils` | `CryptoUtils.h`（**单独目标**——Drogon 耦合，不进 common） |
| `oauth2::utils`（JwkManager shim） | **删除**（A4） | 6 个调用方迁到 `authforge::oauth2::JwkManager` 后删 shim |

### 3.3 顶层 `oauth2::`（按文件/类决定，非全局替换）

| 文件/类 | 目标 | 说明 |
|----|----|----|
| `OAuth2Plugin`（plugin/OAuth2Plugin.h） | **保持全局** | 决策 1 |
| `OAuth2CleanupService`（plugin/OAuth2CleanupService.h） | `authforge::drogon::` | Drogon 事件循环（timer）编排 |
| `IdentityService`（services/IdentityService.h） | `authforge::identity::` | 决策 3 |
| **oauth2 仓储**（storage/Memory|Postgres|Redis + Client|Grant|Token|Consent Repository + RepositoryBundle） | `authforge::storage::{memory,postgres,redis}::` | 实现新 `authforge::oauth2::repository::*` 接口；归存储层 |
| **identity 仓储接口**（storage/IRoleRepository.h/IUserRepository.h/ISubjectMappingRepository.h） | `authforge::identity::` | 决策 2 |
| **identity 仓储实现**（storage/Memory|Postgres|Redis + Role|User|SubjectMapping Repository） | `authforge::storage::{memory,postgres,redis}::` | 决策 2 |
| `StorageCallbacks.h`（storage/） | `authforge::identity::` | 随 identity 仓储走 |
| `UserRef.h`（storage/） | `authforge::oauth2::model`（与 libs/oauth2 的 UserRef.h 合并） | 现有两个 UserRef.h（OAuth2Plugin + libs/oauth2）；本任务合并 |
| `OAuth2Types.h`（types/，定义 ClientType/GrantType/OAuth2Error） | **删除** | 已被 `authforge::oauth2::model::ClientType`（libs/oauth2）+ `authforge::common::error::ErrorCatalog` 取代；SessionController.cc 的 include 是死引用，一并清掉 |
| `oauth2::controllers`（libs/drogon 的 OAuth2StandardController.h/.cc） | `authforge::drogon::controllers` | 2 个文件（其余 39 个 controller 已在该命名空间） |

## 4. 执行步骤（M8 原子）

1. **迁移前打 tag**（design §14.1）：`git tag pre-namespace-unification`。
2. **脚本化重命名**（per-file 命名空间目标，非全局替换）：
   - §3.1/3.2 的子命名空间 + `common::*`：全局 `s/old/new/`（含 `using namespace`、限定名、字符串里的 `"oauth2::filters::..."`）。
   - §3.3 的顶层 `oauth2::`：按文件/类指定目标（脚本以文件路径 → 目标命名空间映射表驱动）。
3. **删除**：`OAuth2Types.h`、`oauth2::JwkManager` shim（迁完调用方后）、重复的 `UserRef.h`（合并）。
4. **构建**：`manage.ps1 build-backend -debug`，零错。
5. **ctest**：`ctest -C Debug`，290/290。
6. **运行时验证（关键）**：启动真实服务器，curl 受 AuthorizationFilter 保护的路由（如 `/api/admin/dashboard`），确认 **401（filter 链活，非 500/路由丢失）**——验证 §6 的 filter 字符串已正确迁移（ctest 的 route-manifest golden 只验路由存在，不验 filter 反射实例化）。
7. **更新 config**：若有 config 文件引用旧 filter 字符串（已核实 4 份 config 用 `"oauth2::filters::AuthorizationFilter"` 在 ADD_METHOD_TO 里，不在 config.json），同步。
8. **整体成功**：单 commit；**整体失败**：`git reset --hard pre-namespace-unification`。

## 5. 方案全局一致性自评审

### 5.1 与 design §5.1/§5.4 一致性
- ✅ 三 Domain 包命名空间（`authforge::{common,oauth2,identity}`）对齐。
- ✅ 存储后端命名空间（`authforge::storage::{postgres,redis,memory}`）对齐 §5.4。
- ✅ Drogon 绑定（`authforge::drogon` + 子）对齐 §5.4。
- ⚠️ **内存存储实现在 OAuth2Plugin/storage/ 而非 libs/storage-memory/**：design §5.4 设想 `libs/storage-memory` 目录，但当前内存实现在 OAuth2Plugin/。**本任务只改命名空间（→ `authforge::storage::memory::`），目录迁移（→ `libs/storage-memory/`）是 Task 39（M8 目录迁移），不在本任务。** 文件位置与命名空间暂时不一致——可接受（Task 39 会统一），但需在 PROGRESS.md 记一笔。

### 5.2 跨层依赖一致性（design §4.1 分层规则）
- ✅ identity 仓储接口进 `authforge::identity::`，oauth2 不依赖 identity（设计 §5.2）。
- ⚠️ **`authforge::common::observability` 里的 AuditLogger 依赖 Drogon（HttpRequestPtr）**：common 层 design §4.1 规则 1 禁 Drogon（仅允许 jsoncpp）。**冲突点**：若 AuditLogger 进 common，common 就有了 Drogon 依赖，违反分层。**两个解法**：
  - (a) AuditLogger 留在 Drogon 层 → `authforge::drogon::observability`（但 AuditEvent 在 common，会割裂）。
  - (b) 把 AuditLogger 改为依赖 `authforge::common::ports::IAuditSink`（已存在），Drogon 实现进 drogon 层——这才是 §5.2 的端口解耦正道。**推荐 (b)**，但属额外工作（重构 AuditLogger 签名）。
  - **决策修正建议**：observability → **`authforge::drogon::observability`**（而非 common），避免 common 引入 Drogon 依赖。Metrics 同理。AuditEvent（纯模型）留 common。这与 §5.4 的「Drogon 绑定含 observability」更一致。
- ✅ CryptoUtils（Drogon 耦合）→ `authforge::drogon::utils`（不进 common）——已正确分流。

### 5.3 反射字符串一致性（design §5.7/H1）
- ✅ OAuth2Plugin 类名/全局命名空间保留（config 反射不破）。
- ⚠️ **filter 字符串 `"oauth2::filters::AuthorizationFilter"`（33 处 ADD_METHOD_TO）必须同步改 `"authforge::drogon::filters::AuthorizationFilter"`**——否则 Drogon 的 `DrClassMap` 按名反射找不到 filter，**运行时 500（非编译期错误）**。这是本任务最大风险点（ctest 不一定覆盖——见 §6）。**执行时必须 grep 确认零残留 `oauth2::filters::` 字符串引用**。

### 5.4 命名一致性（design §5.8，B10 范围）
- 本任务**只改命名空间，不改类名/文件名**。类名/文件名一致性（OAuth2StandardController 拆分等）是 B10（Task 45），在本任务之后。两个任务解耦，避免一次改太多。

### 5.5 与 M8 原子性（design §14.1）一致性
- ✅ 单 commit + tag + 整体回退，符合 §14.1。
- ⚠️ **本任务 + B10（Task 45）+ Task 39（目录迁移）都是 M8**：design 把 M8 定义为「原子切换」。是否本任务、B10、39 必须同一个原子窗口？**建议**：本任务（命名空间）单独原子提交，B10（命名）单独原子提交，39（目录）单独原子提交——三者都是 pre-v1.0.0、可独立验证，不强制同 commit。但都在 v1.0.0 冻结（Task 41）前完成。

## 6. 关键风险

1. **filter 反射字符串**（§5.3）：33 处 ADD_METHOD_TO 字符串若漏改 → 运行时 500。**验证必须含真实服务器 curl**（ctest route-manifest 不够）。
2. **顶层 `oauth2::` 非全局替换**（§3.3）：`storage/` 一个目录里同时有 oauth2 仓储（→ storage::）和 identity 仓储（→ identity::/storage::），脚本必须按**类名/文件名**精确分流，不能按目录一刀切。bundle 的 `clientRepository()` 返回 oauth2 仓储（→ storage::），`roleRepository()` 返回 identity 仓储（→ storage:: backend）——两边都进 storage，但接口在不同命名空间。
3. **UserRef.h 重复**（§3.3）：OAuth2Plugin/storage/UserRef.h 与 libs/oauth2/.../model/UserRef.h 并存；合并时确认字段一致。
4. **identity 仓储接口迁 `authforge::identity::` 后，bundle 的 `roleRepository()` 等返回类型变化**：bundle 头（Memory/Postgres/RedisRepositoryBundle.h）的 accessor 返回类型 + IdentityService 的 Repos struct + StorageRoleProvider 都要同步改限定名。
5. **跨包 include 路径**：本任务不改 include 路径（文件位置不动），只改命名空间——降低了风险（include 仍是 `oauth2/storage/...`）。Task 39 才改路径。

## 7. 范围边界（本任务不做）

- 不改类名/文件名（B10 / Task 45）。
- 不迁移文件目录（Task 39）。
- 不做 OAuth2StandardController 拆分（B10）。
- 不做版本重置（Task 41）。
