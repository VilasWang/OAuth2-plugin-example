# authforge → fulla 改名影响范围分析

> 分析日期：2026-08-25（第一轮 + 同日第二轮补充审查，见 §2bis）｜ 方法：`git grep` 全仓实测 + 外部 registry/API 实查
> 前置文档：[rename-candidates.md](rename-candidates.md)（命名调研，fulla 排名第 9）
>
> **版本策略（2026-08-25 已定）**：改名视为**新产品身份**，版本序列**重置为 v1.0.0**（而非 v2.0.0）。依据：SemVer 约束的是包身份而非仓库——`fulla-oauth2`/`fulla-*` 镜像/`fulla` CMake 包均为新身份，1.0.0 起步是唯一自然选择；项目零外部用户，v2.0.0 的"破坏性升级"信号无处安放，反而让新项目首发 2.0 显得来历不明。先例：OpenSearch fork 自 ES 7.10 仍以 1.0.0 首发。直接 1.0.0（不用 0.x）是因为代码库已有 353 ctest + 完整 CI 矩阵 + benchmark 体系，0.x 会低估成熟度。红线（不可随之重置）见 §6 末尾。

## 0. 结论摘要

改名是**大规模但低风险**的工程：全仓 1658 个文件、约 1.1 万处 `authforge` 出现，其中约 **90% 是纯机械替换**（源码标识符、脚本、文档、CI），真正需要**决策和迁移策略**的只有 8 个层面：

| # | 非机械层面 | 性质 |
|---|---|---|
| 1 | Redis 键前缀 `authforge:cache:*` + Prometheus 指标 `authforge_cache_*` | 运行时数据面：前缀变更=升级时缓存整体失效；指标变更=监控面板/告警断点 |
| 2 | PyPI 已发布包 `authforge-oauth2`（1.4.x 在线）+ GHCR 镜像 `voidvec/authforge-*` | 已发布工件：旧名永久存在，需 deprecation + 新名并行 |
| 3 | api-diff 门禁基线（`tools/api-diff/api-baseline.txt`，820 处） | CI 门禁：改名=全局 API 破坏，基线必须重生成并 --force 批准 |
| 4 | C++ 公共 API 面（`#include <authforge/...>` 路径 + `namespace authforge`） | 语义化版本：版本序列重置为 **v1.0.0**（新产品身份，见文首决策） |
| 5 | Go module path `github.com/voidvec/authforge/clients/go` | module 路径=URL：旧路径不可长期依赖 |
| 6 | 本机目录改名 `D:\...\authforge` | 工作区身份/构建缓存/多工具路径全部失效 |
| 7 | `OAUTH2_*` 环境变量前缀（20+ 变量、1260 处、22 个代码文件读取） | 第二套命名体系，规范化决策见 §2bis B |
| 8 | 前端品牌面（文案 + **e2e 断言** + 页面标题 + package.json 名） | UI 品牌替换与测试断言必须同 PR，漏改直接挂 e2e |

**三条好消息**（实测排除的担忧）：

- **数据库零影响**：DB 名/用户是 `oauth2_db`/`oauth2_user`（deploy/docker/docker-compose.debug.yml:7-9），不含项目名；SQL 迁移/种子文件**零命中**；
- **协议面零影响**：cookie 名、issuer、User-Agent 中**无任何 authforge 字样**——JWT/session/OIDC 协议行为完全不变，存量令牌不受影响；
- **运行时配置零耦合**：`apps/server/config/*.json`（5 份）中 0 处 authforge，filters 段为空数组——过滤器名（`"authforge::drogon::filters::..."` 共 56 处字符串字面量）只在代码内自洽，随命名空间一并机械替换即可。

---

## 1. 实测总量（2026-08-25，git 跟踪文件）

- **1658 个文件**含 `authforge`（不区分大小写），合计约 **11,000+ 处**；
- 目录分布：benchmarks 821（其中 ~770 为**历史测量结果数据**，见 §8）、libs 328、.qoder 镜像 wiki 201（可再生）、tests 108、docs 52、apps 21、clients 18、deploy 17、其余为脚本/CI/配置/README；
- 标识符变体 TOP：`authforge`(9826)、`authforge-sdk`(279)、`authforge-server`(161)、`authforge-oauth2`(64)、`authforge-tests`(53)、`authforge-drogon`(43)、`authforgepackage`(41)、`authforge_package`(38) 等。

## 2. 分层影响清单

### L1 品牌与身份资产（改名决策本体）

| 资产 | 现状 | 改为 fulla 的影响 |
|---|---|---|
| GitHub 仓库 | `voidvec/authforge` | Settings 改名后 web/git 链接 301 重定向（issues/PRs/stars/tags 全保留）；**重定向在别人抢注旧名时失效**——改名后旧名空置即有此风险，可接受 |
| GitHub org | `voidvec`（不含项目名） | **无影响**。注意：GitHub 用户名/org `fulla` **已被占用**（早前实测 404 检查 github=200），若想要 fulla 同名 org 需变体（`fulla-iam`、`getfulla` 等）或沿用 voidvec |
| 域名 | — | `fulla.dev` ✅ 已注册（2026-08-26）；`fulla.com` ❌ 已被占 |
| PyPI | `authforge-oauth2` 在线（实测 HTTP 200） | 新包 `fulla-oauth2`（裸名 `fulla` 在 PyPI 也空，可考虑直接占）；旧包可继续存在但应在新版描述中标注 deprecated |
| npm | 无已发布包 | 前端 `oauth2-admin`/`oauth2-frontend` 均非发布包，无影响。注意裸名 `fulla` 在 npm 被 2019 年死包占用——未来若发 JS SDK 用 `fulla-sdk` |
| Go module | `github.com/voidvec/authforge/clients/go` | 必须改为 `github.com/voidvec/fulla/clients/go`；旧路径靠 GitHub 301 短期可解析，**不可长期依赖**，旧版应打 deprecated 注释 |
| Docker 镜像 | `ghcr.io/voidvec/authforge-{backend,frontend,admin}` | GHCR 包名不随仓库改名自动迁移：新构建发布为 `fulla-*`，旧包留在原地；compose 拉取方需改引用 |

### L2 C++ 源码标识符（机械替换的主体，但=公共 API 破坏）

- **命名空间**：`namespace authforge` 覆盖 **350 个文件**（libs/ + apps/ + tests/）；
- **公共头文件路径**：8 个目录 `libs/{common,drogon,identity,oauth2,storage-memory,storage-postgres,storage-redis}/include/authforge/`（含 testing）→ 所有 `#include <authforge/...>` 变更，SDK 消费方全破；
- **CMake**：`project(authforge ...)`（CMakeLists.txt:3）、目标 `authforge::*`（如 `authforge::identity`，CMakeLists.txt:57）、导出包 `cmake/AuthForgePackage.cmake` + `AuthForgePackageConfig.cmake.in`、安装布局 `lib/cmake/authforge-*/`（release.yml:8 注释）；
- **公共 CMake 选项**（下游用户可见）：`AUTHFORGE_WERROR`、`AUTHFORGE_ENABLE_LTO`、`AUTHFORGE_CMAKE_PRESET`、`AUTHFORGE_VERSION` 等；
- **字符串字面量**：Drogon 过滤器注册名 56 处（`"authforge::drogon::filters::AuthorizationFilter"`×39、`OAuth2AuthFilter`×17 等）——与 config.json 零耦合（实测 filters 段为空），代码内自洽；
- **ORM 模型路径**：`paths.env:68` `MODELS_INC_REL_DIR=include/authforge/storage/postgres/models`——orm-gen 流程与生成的 include 路径联动；
- **conanfile.py**：`AuthForgeConan` 类名与包引用。

### L3 构建与二进制名

- `paths.env:46` `SERVER_BINARY_NAME=authforge-server`、测试二进制 `authforge-tests`（CI 多处默认值）+ 各库测试目标 `authforge-common-test`、`authforge-identity-test` 等（add_executable 实测 6+）；
- CMakePresets.json：LTO preset 描述与 `AUTHFORGE_ENABLE_LTO` 缓存变量；
- 所有构建缓存因路径/目标名变化**全部失效，需清空重建**。

### L4 CI / 门禁

- `ci.yml`：测试 exe 名（authforge-tests.exe）、`AUTHFORGE_WERROR`、SDK 头 SemVer 守卫（ci.yml:53，注释明言守卫 `libs/*/include/authforge`）——改名触发该守卫，以 v1.0.0 新版本序列放行；
- `_build-test.yml`：docker 容器名 `authforge-postgres`/`authforge-redis`（仅 CI 内部，无持久化影响）；
- **api-diff 门禁**：`tools/api-diff/api-baseline.txt` 含 820 处 authforge——基线是导出 API 符号快照，改名后 diff 会全量飘红；处理=重新生成基线 + `--force` 批准（先例：backchannel logout PR 已有 drift 批准流程）；
- `release.yml`：安装布局 `lib/cmake/authforge-*/`、可能的 GHCR 发布名；
- `arch_guard.py`、`api_diff.py` 工具自身的路径规则。

### L5 运行时数据面（升级瞬间的影响）

| 项 | 现值 | 影响 |
|---|---|---|
| Redis 键前缀 | `authforge:cache:token:access:` / `token:revoked:` / `token:introspect:` / `client:` / `user:*` 等 | 前缀改 `fulla:` 后旧键全部孤儿化=**一次性全量缓存失效**，升级窗口内回源 DB 有小风暴（QPS 高时注意）；旧键带 TTL 自然过期，无需清理脚本 |
| Prometheus 指标 | `authforge_cache_total`、`authforge_cache_invalidation_failures_total` | 指标名变更= Grafana 面板/告警规则同步改，否则监控盲窗 |
| 数据库 | `oauth2_db` / `oauth2_user` | **零影响** |
| cookie / issuer / JWT / UA | 无 authforge 字样 | **零影响**，存量令牌与会话完全兼容 |

### L6 部署与运维

- `deploy/docker/docker-compose.prod.yml`：三镜像引用 + `AUTHFORGE_VERSION` 环境变量名（5 处）；
- deploy/ 共 17 个文件（compose ×3、k8s manifests 等）；
- k8s 部署中的镜像名/资源名（.qoder wiki 的 Kubernetes 部署页有 79 处，可作清单参考）。

### L7 客户端 SDK（已发布工件）

- **Python**：`clients/python/pyproject.toml` `name = "authforge-oauth2"`（PyPI 实测在线）→ 新包 `fulla-oauth2`；旧包发一个带 deprecation 说明的封版或仅改 README；
- **Go**：module path 变更（见 L1）；Go proxy 缓存旧路径版本，消费方 `go get` 新路径即可，旧模块建议加 `// Deprecated:` 注释；
- **前端**：品牌文案/标题/e2e 断言/package.json 名的完整清单与陷阱见 §2bis A（8 文件 11 处）；

### L8 文档、镜像目录与历史数据

- docs/ 52 个文件 + README×3 + AGENTS.md/CLAUDE.md/CONTRIBUTING/SECURITY：机械替换；
- `.qoder/` 201 个文件是**可再生的镜像 wiki**（含路径含 `(authforge__common)` 的文件名）：整体 sed 或由工具重生成；
- `.codebuddy/`、`.claude/`、`.kiro/`、`.zcode/` 中的规则镜像同步；
- `benchmarks/` 821 个命中文件中约 **770 个是历史测量结果**（benchmarks/results/ 359、baseline、各 sweep）——**不应改写**：它们是"authforge@某 commit"的测量记录；仅 `benchmarks/authforge/` 工具目录（~30 文件）需要 `git mv` 为 `benchmarks/fulla/` + .gitignore:33 路径同步；
- `CHANGELOG.md` 历史条目**不改写**（历史事实）；新条目以新名书写。

### L9 本机与工作区（维护者机器特有，细节不入库档）

- 仓库目录改名后：
  - AI 工具工作区身份 key 随目录名变化，需迁移或重建索引；
  - 辅助工具的本地扫描历史/coverage 路径失效；
  - 辅助克隆（基准环境）的路径同步；
  - 本机 native PostgreSQL 的 DB 名不变（oauth2_*），无数据迁移。

### L10 不可改 / 不应改（负面清单）

1. git 历史与 tag（v1.x.y）——永久保留；
2. CHANGELOG 历史条目、benchmarks/results 历史数据——测量事实；
3. PyPI 旧版本、GHCR 旧镜像——只能 deprecate 不能消除；
4. `docs/branding/rename-candidates.md` 调研报告本身（authforge 是调研对象）。

## 2bis 第二轮补充审查（2026-08-25，五个专项）

### A. 前端品牌面（L7 细化，实测 8 文件 11 处）

- **品牌文案**：`AppLogo.vue`（admin+user）、`LoginPage.vue:37` h1 "AuthForge Admin"、`:94` 页脚 "AuthForge Identity Platform · Enterprise OAuth2/OIDC Server"、user 侧 `AppLayout.vue:130`/`AuthLayout.vue:73` 同款页脚、`design-tokens.css` 注释；
- **e2e 断言联动（陷阱）**：`frontends/admin/tests/e2e/auth.spec.ts:16` `toContainText('AuthForge Admin')` —— 品牌文案改动必须同步此断言，否则 16 条 admin e2e 直接红；
- **页面标题**：admin `index.html` `<title>oauth2admin</title>`、user `<title>OAuth2 App</title>` 及 `frontends/user/.env` `VITE_APP_NAME=OAuth2 App` → 统一 Fulla 系（"Fulla Admin"/"Fulla"）；
- **package.json 名**：`oauth2-admin`/`oauth2-frontend` → `fulla-admin`/`fulla-user`（顺带与目录名 user 对齐；均为非发布包，无 registry 迁移成本）；
- user 有 `.env`/`.env.example`（含 VITE_GITHUB_CLIENT_ID 等与改名无关项，随迁检查），admin 无 .env；admin 有 favicon.svg，**user 无 favicon**——顺手补齐（品牌 logo 机会）。

### B. 基础设施与 DB 命名规范化（借改名窗口从头开始）

无生产环境，方法 = **改配置 + db-reset 重建**，不写 `ALTER DATABASE RENAME` 迁移 SQL（迁移编号红线不变）。对照表：

| 现值 | 建议新值 | 出现点 |
|---|---|---|
| `oauth2_db` / `oauth2_db_prod` | `fulla_db` / `fulla_db_prod` | 5 份 config 的 dbname、compose×3、`_build-test.yml:217` |
| `oauth2_user` | `fulla_user` | 同上 |
| `container_name: oauth2-{admin,frontend,backend,postgres,redis,prometheus}` | `fulla-*` | `deploy/docker/docker-compose.yml:7-115` |
| `OAUTH2_*` 环境变量（20+ 个、1260 处、22 个代码文件读取） | `FULLA_*` | env_common.sh、compose、CI、bench 脚本（注意只改**全大写下划线形态**，见 §3 新增规则） |
| compose 服务键名（admin/frontend/backend/postgres/redis/prometheus） | **保持** | `config.prod.json` 的 db host `"postgres"`/redis host `"redis"` 引用服务键名 |
| 卷名 pgdata/redisdata/promdata | **保持** | compose project 前缀已隔离 |
| redis 密码两套（主 compose `redis_secret_pass` vs debug `123456`） | 顺手统一 | compose×2 |
| `POSTGRES_PASSWORD=123456` | dev 默认可留；prod 已有 `${POSTGRES_PASSWORD}` 注入机制 | — |

`OAUTH2_`→`FULLA_` 的理由：这批变量中 `OAUTH2_PROJECT_VERSION`/`OAUTH2_ENV`/`OAUTH2_SERVER_DIR` 本就是**项目级而非协议级**，前缀实际起品牌作用；一个品牌一个前缀，避免 fulla 时代继续背着 oauth2 前缀的二次不一致。代价：替换面 +1260 处，但模式单一（纯前缀替换）。

### C. README 与仓库门面

- README.md / README.zh-CN.md 当前定位 "Full-Stack OAuth2/OIDC Authorization Server" —— **借机重写**为终极定位（"高性能 C++ 开源 IAM 核心 + 商业增强模块"），更新模块划分图（libs/* SDK 分层、apps/server、frontends、clients）、Quick Start；benchmark 章节与 "5/5 scenarios lead" 徽章内容保留（指向 benchmarks/competitors）；
- **owner 不一致（既有问题，改名时一并修）**：仓库内 `github.com/lucaswang420/authforge` 引用 **36 处**（README 徽章等）vs 实际 remote/go module/GHCR 的 `voidvec` —— 统一为 voidvec（`git remote -v` 实测 origin）；
- **GitHub 仓库元数据**：description 含 "the auth forge for your apps" 双关语需重写；topics 存在拼写错误 `rabc`→`rbac`，可补充 iam/authorization-server 等；homepageUrl 当前为空 → 注册 fulla.dev 后填入。

### D. 五环境配置项审查（config.{json,dev,ci,prod,bench}）

- config.json / dev / bench 三份实质相同（dbname oauth2_db、host 127.0.0.1、port 5555）—— 改名落地后可另行决策是否合并为 overlay，减少三份漂移面（非改名必需）；
- config.prod.json：db host `"postgres"`、redis host `"redis"`（compose 服务键名）—— 服务键名不改则此处只改 dbname；
- config.ci.json：无 dbclient 段，DB 连接由 CI 以 `OAUTH2_DB_*` env 注入 → **前缀改名的联动点**；
- 五份 listener 端口统一 5555，一致性良好，无需动。

## 3. 大小写映射表（机械替换的替换规则）

| 旧 | 新 | 例 |
|---|---|---|
| `authforge` | `fulla` | 命名空间、路径、二进制名 |
| `Authforge` | `Fulla` | `AuthforgePackage` → `FullaPackage` |
| `AuthForge` | `Fulla` | `AuthForgePackage.cmake` → `FullaPackage.cmake`、`AuthForgeConan` → `FullaConan` |
| `AUTHFORGE` | `FULLA` | `AUTHFORGE_WERROR` → `FULLA_WERROR` |
| `OAUTH2_`（全大写下划线前缀） | `FULLA_` | `OAUTH2_DB_HOST` → `FULLA_DB_HOST` 等 20+ 变量（§2bis B） |
| `oauth2_db` / `oauth2_user` / 容器名 `oauth2-*` | `fulla_db` / `fulla_user` / `fulla-*` | 见 §2bis B 对照表 |
| `authforge-oauth2`（PyPI） | `fulla-oauth2` | — |
| `authforge::` | `fulla::` | 过滤器注册字符串同步 |

> `fulla` 是真词（可为子串，如英语 FullAuto），反向替换无风险；但正向替换时注意 `authforgepackage`/`authforge_package` 这类**无分隔符拼接变体**（41+38 处）必须列入替换模式，不能只替裸词。
>
> **关键区分**：只改 `OAUTH2_`（全大写下划线，环境变量前缀）；**不改** `OAuth2` 驼峰形态——`OAuth2Plugin` 插件类名、`OAuth2AuthFilter`、openapi 里的协议词、"Enterprise OAuth2/OIDC Server" 副标题里的 OAuth2 都是**功能/协议名**，不是项目名，保留。
>
> **子串边界教训（Phase 2 实测踩坑，已修复）**：`oauth2_user` 是表名 `oauth2_user_consents` 与 operationId `oauth2_userinfo` 的**子串**——朴素 sed 会把这两类 token 也改掉（32+25 处，含 V006 迁移、model.json、模型 .cc 的 tableName、openapi、Python SDK 函数名），症状是 ORM 重生成后模型类名错乱（FullaUserConsents）与运行时 SQL 表名失配。修复原则：DB 表名、索引名、operationId、URL 路径一律不改；替换后必须按 token 直方图（`grep -hoE 'fulla_[a-z_]+' | sort | uniq -c`）逐类复核边界。

## 4. 兼容性矩阵（谁破谁不破）

| 消费方/资产 | 是否破坏 | 缓解 |
|---|---|---|
| C++ SDK 消费方（include 路径+命名空间+CMake 包） | ❌ 破坏（当前仅内部消费） | fulla v1.0.0 新序列 + 迁移说明（一段 sed 即可） |
| Python SDK 用户 | ❌ 包名变更 | 新包发布 + 旧包 README 标 deprecated |
| Go SDK 用户 | ❌ module 路径变更 | 新路径 + 旧版 Deprecated 注释 |
| Docker 部署方 | ❌ 镜像名变更 | 新镜像 fulla-* + 文档公告 |
| 存量 JWT/session/cookie | ✅ 兼容 | 协议面无项目名（实测） |
| 存量数据库 | ✅ 兼容 | DB 名无项目名（实测） |
| Redis 缓存 | ⚠️ 一次性失效 | 升级窗口回源风暴，TTL 自然清理 |
| Grafana/告警 | ⚠️ 指标断点 | 面板同步改名 |

## 5. 建议执行顺序（13 步）

1. **前置外部资产**：域名与包名等外部资产于改名日前置办理（具体清单见本地维护副本）；
2. 开改名分支，写替换脚本（按 §3 映射表，含拼接变体），先 `git mv` 八个 `include/authforge/` 目录与 `benchmarks/authforge/`；
3. 跑替换（排除 benchmarks/results、CHANGELOG 历史区、docs/branding/）；替换模式 = §3 映射表全表，**含 `OAUTH2_`→`FULLA_` 前缀替换与 §2bis B 的 DB/容器名规范化**（只替全大写 `OAUTH2_`，不动驼峰 `OAuth2` 类名）；
4. 重生成 ORM 模型（orm-gen，路径联动）与 **api-diff 基线**，`--force` 批准记录在 PR 描述；
5. 清空全部构建目录，全量构建 + `full_test` 8 步后端流水线 + 前端（admin 16 e2e + user 8 e2e）；
6. CI 三件套（ci/release/_build-test/_sdk-smoke）中的 exe 名/容器名/环境变量同步；
7. deploy/ 三份 compose + k8s manifests：镜像名 → `ghcr.io/voidvec/fulla-*`，`AUTHFORGE_VERSION` → `FULLA_VERSION`；同时落 §2bis B 的基础设施规范化（DB 名/用户/container_name/redis 密码统一；compose 服务键名与卷名不动）；
8. 清除旧版本序列：删除五个旧 tag（v1.0.0–v1.4.1）及对应 GitHub Releases（`git tag -d` + `git push --delete`，Releases 需在 GitHub 侧显式删除）——git 提交历史与 CHANGELOG.md 历史区**原样保留**，CHANGELOG 顶部加更名分界说明；随后版本定为 **v1.0.0** 走发版六处版本同步流程（openapi.yaml info.title、pyproject 等）+ openapi tags/SDK drift 检查（PR #85 教训：openapi tags 丢失会挂 SDK 门）；
9. PyPI 发布 `fulla-oauth2` 1:1 首版；旧包 README 加 deprecated 指引；
10. GitHub 仓库改名（放最后，重定向即刻生效）；GHCR 新镜像名随 release.yml 首次发布；
11. 前端品牌与仓库门面（§2bis A/C）：AppLogo/LoginPage 文案、index.html 标题、`VITE_APP_NAME`、package.json 名、**auth.spec.ts e2e 断言同步**、user 侧 favicon 补齐；README 双语重写（新定位 + 模块划分 + owner 统一为 voidvec）；GitHub 元数据更新（description、topics 含 `rabc`→`rbac` 修正、homepage=fulla.dev）；
12. 本机目录改名 + ZCode 记忆库迁移 + WSL 克隆路径同步 + Mimosa 重新基线；
13. 旧工件收尾：Grafana 面板、告警规则、外部文档/榜单（若有）公告更名。

## 6. 风险清单（TOP 8）

1. **api-diff 基线重生成**是唯一"改错了会放走真回归"的环节——基线重生成前后各跑一次全量测试；
2. **过滤器注册字符串漏改**（56 处字面量藏在 .cc 深处）会导致运行时过滤器失配启动失败——靠全量 e2e 兜底；
3. **Redis 前缀变更的回源风暴**——高 QPS 生产环境选择低峰升级；
4. **拼接变体漏替换**（authforgepackage 等 80+ 处无分隔符形态）——替换脚本必须含这些模式并 grep 验证归零；
5. **npm/PyPI/GitHub 的 fulla 裸名占用不对称**（PyPI 空、npm 死包、GitHub org 被占）——包名策略先行统一再动手，避免发一半改主意；
6. **前端 e2e 品牌断言**（auth.spec.ts:16 `toContainText('AuthForge Admin')`）与文案不同步会挂 admin e2e —— 文案与断言必须同 PR；
7. **owner 不一致**（lucaswang420×36 处 vs voidvec）——统一方向错了会让 README 徽章/克隆链接全断，以 `git remote -v` 的 voidvec 为准；
8. **`OAUTH2_` 替换误伤**：sed 模式若写成宽松的 `OAUTH2` 会把 `OAuth2Plugin`/协议词一并破坏 —— 前缀替换必须锚定全大写下划线形态（§3 关键区分）。

### 红线（版本重置 ≠ 这些也重置）

1. **DB migration 编号**：schema_migrations 序号是 schema 演进史，与产品版本无关——重置它会让所有已初始化的 dev/测试库校验失败，是整个改名过程中**唯一可能真正搞坏数据**的操作；
2. **git 提交历史**：不 rebase、不 squash——PR #47–#85 的评审与决策历史是工程资产，只动 tag 指针；
3. **CHANGELOG 历史区与 benchmarks/results 历史数据**：authforge 时代的发布与测量事实，原样保留，仅新条目用新名。

## 7. 实施计划（PR 切分与验证门）

> 配套审计：[repo-professionalization-audit.md](repo-professionalization-audit.md)（Phase 1 的逐文件依据）

| Phase | 分支/PR | 内容 | 验证门 | 执行者 |
|---|---|---|---|---|
| **P0** 资产占位 | —（无代码） | 域名与包名等外部资产前置办理 | 资产到手 | **用户手动** |
| **P1** 专业仓库清理 | `chore/professional-repo-cleanup` | 审计清单执行：untrack `.qoder/.codebuddy/.zcode/.kiro`（kiro specs 先迁 docs/history）、删过期 MEMORY.md、.gitignore 增补、AGENTS.md 角色表、绝对路径泛化 | 纯文件操作；`git status` 干净 + CMake configure 冒烟 | 代理可执行 |
| **P2** 改名机械替换 | `feat/rename-fulla`（基于 P1） | §3 映射全表替换（含 `OAUTH2_`→`FULLA_`、§2bis B 基础设施规范化）+ 8×`include/authforge` 与 `benchmarks/authforge` 的 git mv + config 五份 | **Release 构建 + 353 ctest 全绿** + 前端 tsc/build | 代理可执行 |
| **P3** 门禁与基线 | 并入 P2 或紧随 | api-baseline 重生成 + `--force` 批准记录；arch-guard；前端 e2e 16+8（含 auth.spec.ts 断言联动） | CI 全绿 + 本地全量前端测试 | 代理可执行 |
| **P4** 定版与门面 | `release/v1.0.0` | 删旧 tag v1.0.0–v1.4.1 + Releases；v1.0.0 六处版本同步 + openapi tags/SDK drift 检查；README 双语重写；前端品牌文案；GitHub 元数据（description/topics/rbac/homepage） | 发版六点本地预检清单全过 | 代理可执行（tag 删除需用户确认） |
| **P5** 外部与本机收尾 | —（无 PR） | GitHub 仓库改名；GHCR 新镜像首推；PyPI 发布；本机目录改名 + ZCode 工作区迁移 + WSL 克隆同步；Grafana/告警同步 | Release workflow 全绿 | **用户手动**（远程操作+凭据） |

**依赖关系**：P0 与 P1 并行；P2→P3→P4 严格串行；P5 最后。P1 必须先于 P2 合入（否则改名 diff 混入 347 个出库文件的噪音，评审不可读）。

**回滚**：P1–P4 均为可 revert 的普通 PR；P5 的 GitHub 仓库改名可再改回（重定向链保持）。
