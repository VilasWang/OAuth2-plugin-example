# AGENTS.md — fulla 项目指令

> 本文件是 ZCode 的项目级指令文件（`<repo>/AGENTS.md`）。
>
> **项目规则的权威源头是 `.claude/rules/`**（git 跟踪）。本文件只做**索引和导航**，
> 不复述规则正文 —— 这样规则只有一份，不会与 `.claude/rules/` 漂移。当本文件与
> `.claude/rules/` 冲突时，以 `.claude/rules/` 为准。

## 规则索引

| 规则 | 文件 | 适用路径 | 一句话摘要 |
|---|---|---|---|
| DB 操作 | [`.claude/rules/db-operations.md`](.claude/rules/db-operations.md) | `libs/**`、`apps/server/**` | DB 必须 async callback + Mapper + Criteria 三件套；raw SQL 仅 6 种豁免；每个 `Mapper<...>(db)` 构造独立 try-catch；catch 必须 `(*sharedCb)(errorResult)` 回调失败，禁止仅 LOG_ERROR 后 return |
| 数据访问 | [`.claude/rules/data-access.md`](.claude/rules/data-access.md) | `libs/storage-*/**` | 指向 db-operations 的存储层触发器（规则同上） |
| ORM 模型 | [`.claude/rules/orm-models.md`](.claude/rules/orm-models.md) | `**/models/**` | `models/` 下的 ORM 类由 `drogon_ctl` 从 schema 生成，**禁止手改**；要改模型就改 schema 再 `/orm-gen` |
| 开发流程 | [`.claude/rules/dev-workflow.md`](.claude/rules/dev-workflow.md) | `apps/server/**`、`frontends/**` | 优先 `./manage.sh`（Linux/macOS）/ `./manage.ps1`（Windows）；`/build-and-test` 等 skill 是详解，不是首选入口 |

## 异步编程与 DB 访问

**完整规则见 [`.claude/rules/db-operations.md`](.claude/rules/db-operations.md)，本文件不重复。**
涉及异步/DB 代码时务必先读那份文件。关键词速查：

- 接口选择：async callback（首选）→ `Mapper::findBy`-with-future（受限，仅当真需要同步结果）→ `CoroMapper`（禁止）。
- Callback 生命周期：`std::make_shared<CallbackType>(std::move(cb))`，按值捕获进每层 lambda。
- Mapper 构造异常防护：每个 `Mapper<...>(dbClient)` 构造都要独立 `try/catch`，包括嵌套在异步回调内的 —— 外层保护不到内层异步回调，未捕获会逃逸到 Drogon 事件循环导致 SIGABRT。
- JOIN 禁用：拆成多个查询，或用 `Criteria::In(...)`。
- raw SQL 仅 6 种豁免：DDL / `UPDATE ... RETURNING` / 文档化的批量操作 / `INSERT ... ON CONFLICT` / `SELECT 1` 探活 / 显式事务 `COMMIT`。

## `[this]` 捕获（易混淆点，单独说明）

不同层级约定不同，不要混淆：

- **Domain 服务层**（`libs/oauth2`、`libs/identity` 的 service）：**禁止** `[this]` 和 `[&var]`，必须 `auto self = shared_from_this()` 后捕获 `self`。见 db-operations.md。
- **Controller 层**（`libs/drogon/.../controllers`）：`[this]` **允许且普遍**。Drogon `HttpController<T,false>` 是进程级单例，由 Drogon 用裸指针管理，存活整个进程；`shared_from_this()` 不适用（不继承 `enable_shared_from_this`）。范例注释见 `TokenEndpointController.cc:1306-1311`。

> 旧版 `TECH_SPECS.md` 曾把 `[this]` 列为全局禁令，与 controller 层实践冲突，已在本次精简中移除该条。

## 各 AI 工具目录的角色

本仓库的规则与工作流资产以 `.claude/` 为**唯一权威源**（git 跟踪）：

| 目录 | 角色 |
|---|---|
| `.claude/` | **规则源头**（`rules/`）、agents、commands、skills、settings —— git 跟踪，权威 |
| `.codebuddy/`、`.qoder/`、`.kiro/`、`.zcode/`、`.workbuddy/` | 各 AI 工具的**本地工作区**（各自的 skills 镜像/plans/记忆）——不入库（.gitignore 忽略，磁盘保留） |

**改规则就改 `.claude/rules/`**（唯一副本）。如需在其它工具里生效，由各工具目录的本地副本自行同步，不再要求版本库维护多份一致镜像。不要在 AGENTS.md 或 TECH_SPECS.md 里复制规则正文。入库范围的标准见 [docs/branding/repo-professionalization-audit.md](docs/branding/repo-professionalization-audit.md)。

## 其它项目文档

- `TECH_SPECS.md` — 架构、代码质量、安全等**非异步/非DB**的项目契约（异步与 DB 规则已迁移到 `.claude/rules/`，TECH_SPECS 只保留引用指针）。
- `docs/` — 架构概览、SDK 契约、测试指南、运维文档等。
- `README.md` — Quick Start、构建/运行/测试命令。
