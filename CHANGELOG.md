# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For the versioning policy (when to cut, what to bump, why), see
[Versioning & Release](docs/contribute/versioning-and-release.md).

## [1.1.0] - 2026-09-01

### Security

- **⚠️ Breaking (security hardening) — legacy 密码哈希默认拒绝（#103）**:`auth.allow_legacy_hash` 现默认 **false**（配置键缺失亦视为 false）；无盐 SHA256 哈希在所有路径（含改密旧口令校验）被直接拒绝。存量 legacy 用户请走**邮件/管理员重置**（重置即写入 PBKDF2 完成迁移）或临时重开窗口（登录即透明重哈希）；注意**只改密无法自迁移**（旧口令校验同样走已退役分支）。策略性拒绝不计入账户锁定计数。dev 种子与全部脚本改用 PBKDF2（口令不变）；`bench_users.sql` 移至 `benchmarks/fulla/seed`（不再随生产镜像分发）。
- **⚠️ Breaking (security hardening) — WebAuthn 真实验签（#142）**:注册/认证改为 W3C WebAuthn L2 真实验证（仅 ES256、fmt="none"；挑战绑定+300s TTL+无条件消费；UV 强制；signCount 克隆检测）。**V028 清空既有凭证表**（存量行均为未验证的客户端自报材料，用户需重新注册 passkey）；**webauthn.rp_origins 为必配项**（缺省时 finish 端点 fail-closed）；旧请求体契约（`{credential_id, public_key}` / 裸 credential_id 登录）被拒绝;凭 credential_id 即可冒充的漏洞被消除。新错误码 `WEBAUTHN_INVALID_ATTESTATION`(3015)、`WEBAUTHN_CHALLENGE_MISMATCH`(3016)。
- **⚠️ Breaking (security hardening) — 社交登录响应形态变更（#70）**:`/api/{google,wechat}/login` 由返回 profile 改为发放首方 token 对（token 行存 **public_sub**——同步修复 GitHub 既有 token 在 /api/me 族 404 的缺陷）;GitHub 端点 spec 同步修正。三提供商统一首登自动建户（全局开关 `external_auth.auto_create_on_first_login`,默认 true;关闭时未绑定 → 403 `AUTH_SOCIAL_ACCOUNT_NOT_LINKED`(5003)）;发放留痕走 `SOCIAL_LOGIN_TOKEN_ISSUED` 审计事件（非 consent 行）。

### Fixed

- **自注册/管理端创建用户 authorize→consent 500（#143）**:两条创建路径现在都会写入 (local, id) subject mapping（consent 的 getInternalUserId 唯一解析路径）;V027 一次性回填 + 启动自愈把"每个用户必有 local 映射"变成启动不变量。
- WebAuthn 加固（#142 评审轮）:crossOrigin=true 断言拒绝（本 RP 不支持跨源仪式）；注册挑战存储惰性回收（begin 后放弃的条目不再驻留）；signCount 克隆检测改为原子 compare-and-set（并发同凭证断言不再双双通过）；`social_token_client_id` 发放前校验已注册。
- AdminBootstrapper 首启建户补 `salt`（users.salt NOT NULL，此前真·净库首启路径必败——潜在缺陷被新测试暴露）。
- `setup-database` 根因分类与自愈:按"不可达/认证失败（密码错或角色不存在，防枚举同型）/库删不掉（占用或非 owner）/缺 CREATEDB"给出精确修复命令，Linux/WSL 下可经 `sudo -u postgres psql` 自动补 CREATEDB 并重试；测试脚本对缺失的暂存配置给出明确跳过指引。

### Added

- Google/WeChat 登录闭环（#70）:服务层四态建户/绑定、`SocialCallbackPage`（/callback/google、/callback/wechat）、登录页 Google 按钮（`VITE_GOOGLE_CLIENT_ID` 门控）。
- WebAuthn 支持面与运维文档（#142）:rp_id/rp_origins 配置、单实例挑战存储限制、cookie 契约（认证流需 credentials:'include'）。
- helm `secrets.bootstrapAdminPassword` 透传（#103）。

## [1.0.1] - 2026-08-30

### Security

- **License change: MIT -> AGPL-3.0**. Starting with this version the project
  is licensed under the GNU Affero General Public License v3.0; the v1.0.0
  release remains MIT. The core stays fully open source — individuals,
  study, self-hosting and internal use are unaffected. Vendors offering the
  software (modified or not) as a hosted service must release their
  modifications per AGPL-3.0 §13, or obtain a commercial license
  (open-core: enterprise features are distributed separately). A CLA is
  required for contributions so the project can keep dual-licensing.

### Security

- **consent 端点认证闭环(F1)**:`POST /oauth2/consent` 原先完全无认证(知道 user_id 即可代批/代拒)。现在强制四道门:已认证会话、user_id 与会话身份绑定、服务端随机一次性 CSRF nonce(TTL 10 分钟)、deny 分支 redirect_uri 注册校验(修复开放重定向)。SPA ConsentPage 回传 `consent_csrf`。新增错误码 `AUTH_SESSION_REQUIRED`(4007)、`VALIDATION_PASSWORD_TOO_SHORT`(3014)。
- **bootstrap 管理员(#103)**:服务首次启动在无 admin 角色用户时自动创建 `admin`(PBKDF2-SHA256;密码取 `FULLA_BOOTSTRAP_ADMIN_PASSWORD` 或随机生成并在日志打印一次)。dev seed `admin/admin` 仍仅用于开发环境。
- **密码策略(#103)**:注册新增服务端最小长度校验(`auth.min_password_length`,默认 8;改密/重置的既有 8 位硬编码统一走同一配置);新错误码 `VALIDATION_PASSWORD_TOO_SHORT`。
- **遗留哈希迁移门(#103)**:`auth.allow_legacy_hash`(默认 true,迁移窗口)控制无盐 SHA256 校验路径,确认全量迁移后可关闭。

### Changed

- `POST /oauth2/consent` 响应新增 400/401/403(openapi 同步);deny 对未注册 redirect_uri 返回 400 而非 302。
- `/api/register` password 参数新增 minLength 8;改密/重置密码过短错误码由 `VALIDATION_FORMAT_ERROR` 改为 `VALIDATION_PASSWORD_TOO_SHORT`。
- 文档:README/部署指南移除公开凭证示例,管理员创建改为 bootstrap 流程。

### 文档

- **文档治理收官**（PR #114/#116，治理文档 v4，[docs/documentation-governance.md](docs/documentation-governance.md)）：
  用户向文档树按受众七区重构（evaluate / integrate / deep dives / operate /
  contribute / benchmark / adr），12 篇 ADR + 三份信任档案（合规尽调、改名
  影响、仓库审计）入库；fulla.dev 上线为 Docusaurus 双语站——**英文主站**
  （`docs/` 为英文正本）+ `/zh-CN` 中文站（译本树
  `website/i18n/zh-CN/`，同构同 URL，导航栏切换）；新增三篇深潜
  （令牌生命周期 / 会话管理 / 多租户）；站点带离线中文搜索与死链门。
  文档改动义务：**同 PR 双写**（en/zh 同步，见 CONTRIBUTING）。

## [1.0.0] - 2026-08-26

> **版本序列重置说明**：项目由 **authforge 更名为 fulla**（北欧神话中 Frigg
> 的秘库守钥女神 Fulla），版本序列自本版起**重置为 1.0.0**——改名视为新产品
> 身份，SemVer 约束包身份而非仓库（PyPI 包 `fulla-oauth2`、镜像
> `ghcr.io/voidvec/fulla-*`、CMake 包 `fulla` 均为全新身份）。此前历史见
> 下方 authforge 时代的 [1.0.0]–[1.4.1] 条目（git 提交历史完整保留）。

### 更名与品牌

- **全库更名 authforge → fulla**：C++ 命名空间与公共头路径
  （`#include <fulla/...>`）、CMake 包与目标（`fulla::*`）、二进制
  （`fulla-server` / `fulla-tests`）、Go module path、Helm chart、
  benchmark 设施；api-diff 基线按新符号面重生成（179 头文件，`--force`
  批准，PR #94）。
- **环境变量前缀统一 `FULLA_*`**（原 `OAUTH2_*`，903 处；协议类名如
  `OAuth2Plugin` 保持不变）。
- **基础设施命名规范化**：数据库/角色 `fulla_db` / `fulla_user`、容器名
  `fulla-*`、Redis 键前缀 `fulla:cache:`、带 authforge 前缀的 Prometheus
  指标改 `fulla_*`（`oauth2_*` 功能指标名保持不变）
  （升级即全量缓存失效，属预期一次性行为）。
- **前端品牌**：Fulla Admin / Fulla，包名 `fulla-admin` / `fulla-user`。
- **专业仓库治理**（PR #93）：AI 工具工作区出库、kiro 设计文档迁入
  `docs/history/design/kiro-specs/`、`.claude/` 为唯一权威规则源。

### 兼容性

- **C++ SDK 消费方**：破坏性（include 路径/命名空间/CMake 包名全变）；
  迁移即一段 sed（映射表见 `docs/adr/rename-impact-fulla.md` §3）。
- **存量 JWT / 会话 / 数据库 schema**：完全兼容（协议面无项目名字样，
  表名与迁移不变）。
- Python SDK 包名变更为 `fulla-oauth2`；旧 `authforge-oauth2` 停更。

## [1.4.1] - 2026-08-24

安全与缺陷修复版（无新 API 面）：一批 High 级安全修复 + 社交会话修复 +
缓存一致性加固。全部为防御性收紧或既有功能的修复，无破坏性变更。

### 安全修复

- **#78 [High] `/oauth2/end_session` 不再信任未验签的 id_token_hint**：
  hint 现在必须通过 `JwkManager::verifyJwt`（新导出的对称验签入口：严格
  RS256、kid 匹配、EVP_DigestVerify、iss/exp/sub 策略）才能影响登出决策；
  验签失败 / 过期 / issuer 不符 / 与浏览器会话主体不一致 → 400
  `AUTH_INVALID_ID_TOKEN_HINT`（新 ErrorCatalog 4006，Error Envelope），
  在任何 fan-out 之前返回。此前知道任意用户公开 sub 即可强制其全 RP 登出。
  注意：严格语义下，重启（临时密钥）或密钥轮换后重放旧 id_token_hint 会
  400 —— 合法 RP 应在登出前持有未过期 token（窗口以 access TTL 为界）。
- **#79 [High] Redis cache-aside 回填竞态**：写路径失效改为共享的
  **延迟双删**（立即 DEL + 事件循环上 200ms 后二次 DEL，
  `cache.invalidation_double_delete_delay_ms` 可配，钳位 [50,2000]），
  清除"读 miss 的旧行回填落在立即 DEL 之后"导致 secret/角色钉死整个
  TTL 的窗口；回填侧维持 plain SET（NX 无增益，理由见设计文档 §5.4 增补）。
- **#54 [High] 软删除用户的社交登录绕过**：主体修复已于 89c96341 落地
  （四态 SocialLinkStatus、deleted_at 过滤、locked 拒绝、MFA/自服务/consent
  链全覆盖），本版随测试回归一并关闭。
- **#80 [Medium] 失效 DEL 失败可观测**：每次失败的 DEL 尝试计数到
  `authforge_cache_invalidation_failures_total{kind}`（IMetrics 端口）并
  WARN→重试→ERROR；soft-fail 语义不变。

### 缺陷修复

- **#69 GitHub 社交 token 哈希口径统一**：社交签发的 access/refresh token
  改为哈希存储（与 token endpoint 同口径），社交会话从"每次请求 401"
  恢复为可用（userinfo / introspect / refresh / revokeTokenFamily 全通）。
  存量原文 token 本就不可用，直接失效（无迁移）。
- **#75 /api/me/social 数字分发分支 e2e 覆盖**：随 #69 解锁，新增社交
  token 驱动的 link → list → unlink 全链路 HTTP 测试。
- **memory-storage `client_type` 配置键**：`MemoryClientRepository` 只读
  `"type"` 而全部五份配置写 `"client_type"`，memory 模式下配置为 PUBLIC 的
  vue-client 静默变 CONFIDENTIAL、拒绝空 secret 的 token 请求。加载器改为
  优先读规范键（`"type"` 保留回退）。

### SDK / API 面

- `JwkManager::verifyJwt`（新导出方法）与
  `authforge/storage/redis/DelayedDoubleDelete.h`（新导出头）均为
  additive，api-baseline 已批准；无 breaking 变更。
- `openapi.yaml`：`/oauth2/end_session` 描述与 400 响应更新为验签语义
  （ErrorEnvelope schema 引用），版本号同步 1.4.1。

## [1.4.0] - 2026-08-24

v1.3.0 以来的第四个正式发布，核心主线是**性能工程化**（PR #64 合并链）：
非代码性能方案（bench 档 PG 实例调优 / 池 64 / cache-on / session TTL=30
留存有界）+ wave-1/2 读缓存代码优化（validateClient 缓存行校验、用户
profile/roles 读缓存、token 撤销+读值单 EVAL 往返、userinfo 读对 MGET
搭车）+ V025/V026 schema 优化 + 部署默认 PG 15→17 + 竞品基准设施
M0–M3（Keycloak 26.7.1 / Ory Hydra v26.2.0 / Zitadel v4.17.1 同环境套件
与四产品对比）。**2026-08-23 TTL=30 口径四产品重刷：五个对比场景全部
领先**（S1 2.1x / S2 2.6x / S3 2.0x / S5 1.9x / S6 1.5x，见
`benchmarks/competitors/results/COMPARISON.md`）。

### ⚠️ Breaking / 升级必读

- **部署默认 PostgreSQL 15 → 17**（`deploy/docker/docker-compose{,.prod,.debug}.yml`、
  Helm `values.yaml`）：升级动机是与基准环境对齐（四产品同 PG 版本的公平性 D1）
  及服务端 libpq 17.x 客户端对齐。**存量部署的 15 数据卷无法直接在 17 上启动**
  （PG 大版本拒绝挂载旧数据目录，数据库容器会循环重启）。升级前必须按
  [docs/ops/postgresql-major-upgrade.md](docs/operate/postgresql-major-upgrade.md)
  做 dump/restore（或 pg_upgrade）。AuthForge 自身在 15 vs 17 的基准差异在
  噪声带内，本次升级对自身吞吐无诉求；不急于升级的部署可把镜像 tag 钉回
  `postgres:15-alpine`（应用与 15 完全兼容）。
- **`AuditLogs` ORM 模型签名变更**（V025 `audit_logs` 分区）：主键从 `id`
  单列变为 `(id, timestamp)` 复合键；drogon 生成的模型 `getPrimaryKey()` 返回
  `std::tuple<int64_t, int64_t>`（原 `int64_t`）、`primaryKeyName` 变为
  `std::vector<std::string>`（原 `std::string`）、`setTimestampToNull()` 移除
  （`timestamp` 现为 NOT NULL）。仓库内零调用方受影响（api-diff 已按
  `--force` 批准并留档）；仅当外部代码直接编译 `storage-postgres` 模型头时
  才是破坏性变更。

> **版本号取舍（显式记录）**：版本政策 §2 的"依赖大版本 → MAJOR"行针对
> 编译/链接面断代；本次 PG17 是**部署默认对齐**（应用与 15/17 均兼容，
> runbook 提供回钉路径），AuditLogs 签名变更仓库内零调用方——两者均不构成
> SDK 源码级 API 破坏（api-diff v1.3.0 基线零漂移）。经决策按政策 §3 的
> 显式取舍精神在 **MINOR** 内推进并显著标注（2026-08-24）。

### Added

- **竞品同环境基准设施（M0–M3）**：参数化共享设施 + Keycloak / Ory Hydra /
  Zitadel 三套官方推荐配置套件（S1 discovery / S2 client_credentials /
  S3 introspect / S5 refresh / S6 userinfo 阶梯 + GC 抖动长跑 + 冷启动 +
  RSS 采样），`run-comparison.sh` 一键四产品串行 + `gen-comparison.py`
  无手填聚合报告。诚实裁决随数据入仓（S5 口径伪影修复、GC 抖动主张
  关闭——四家同款宿主噪声、全栈 RSS 口径警示）。
- **wave-2 读缓存代码优化**：`validateClient` 对缓存行直接校验（P0）；
  用户 profile/roles Redis 读缓存（P1）；token 撤销标记+读值合并为单次
  EVAL 往返（P2）；userinfo 读对 MGET 搭车 memo（P4）。A/B 实测 S3/S6
  显著改善（S6 对比表口径 18.1k→49.3k QPS）。
- **写路径缓存失效钩子**：admin 用户/客户端写路径、GitHub 绑定路径在
  DB 提交后失效对应缓存键（双 subject 形态、提交时序对齐、
  `resetClientSecret` 覆盖）。
- **V025 `audit_logs` 月度 RANGE 分区 + BRIN**：含 DEFAULT 分区兜底与
  `ensure_audit_partitions()` 滚动维护函数（幂等，db-reset 全链重放安全）。
- **V026 token 表冗余索引清理**：DROP 与 PK 重复的三个单列 token 索引
  （INSERT 索引维护 8→6；评审确认无查询模式依赖）。
- **opt-in LTO 构建预设**：`linux-release-lto` 等预设 + bench 镜像 preset
  透传（`AUTHFORGE_CMAKE_PRESET`）；默认预设不受影响。
- **非代码性能方案落地**（bench 档）：PG 实例调优（-c flags 覆盖）、
  PG/Redis 池 25→64（池扫描定案）、cache-on、`reuse_port=true`、
  session TTL 120→30 留存有界档案（session 留存税 -54%，RSS 减半）、
  `auto_batch=true` 同日 A/B 定案。
- **四产品对比 2026-08-23 TTL=30 重刷**：对外表格从 session 口径遗留数字
  迁移到 TTL=30 + 生产镜像 LTO 口径；Keycloak S6 池过期结构性缺陷修复
  （`run-all.sh` S6 前重铸 user 池）。
- **README 性能徽章 + "如何复现"小节**（P1 对外传播项）：双语徽章链接
  COMPARISON.md 单一溯源，五场景表 + 诚实限定（WSL2 下限、SDK/容器口径
  区分、GC 抖动不主张），复现命令与方法论文档入口。

### Changed

- **CI Linux 矩阵腿服务容器 PostgreSQL 15 → 17**（`_build-test.yml`）：
  CI 从此覆盖部署目标大版本（含 V025/V026 DDL 在 17 上的行为）；
  `docs/backend/ci-cd-guide.md` 服务表同步。

### Fixed

- 文档漂移清理（issue #84）：技术栈表与部署验证清单的 PG15 残留、两份
  SDK README 的"首发前从源码安装"过期注记（PyPI/嵌套 tag 自 v1.3.0 已
  上线）、wave-2 方案风险表"EVAL 单键无跨槽"误记（实现为双键，
  Redis Cluster 下 CROSSSLOT——集群化前需 hash-tag 同槽或拆分）。
- V025 分区边界 UTC 锚定——创建/滚动函数不再受会话时区影响（047c4d8f）。
- 池扫描脚本恢复被换出的配置；overlay 提升 `max_connections`（23ccdc44）。
- docker-stats 采集 bash 5.2 glob 回归；同会话 RSS 过滤口径修正（6951be0d、3ca37211）。
- CI naming validator 要求测试名含 `[Priority]` 段（68b919fb）。

## [1.3.0] - 2026-08-22

v1.2.0 以来的第三个正式发布，21 个 commit。双主线：**C1 客户端 SDK**
（PR #65：Python `authforge-oauth2` + Go —— openapi 生成面 + 手写认证层
[client_credentials 自动刷新 / authorization_code+PKCE]，`regen_clients.py`
漂移门 + `clients-sdk.yml` CI + release 发布接线）与 **B2 社交账号
link/unlink**（PR #68：自助门户 `GET/POST/DELETE /api/me/social/links[/{provider}]`
三端点 + identity 层 `SocialLinkService` 编排 + 最后凭证守卫 + user 门户
Connected Accounts 卡片；含两轮评审修复轮）。纯增量 minor，无破坏性变更。

### Added

- **clients**: `regen_clients.py` —— SDK 再生成 + 漂移门工具（pin 生成器版本、
  `--check`、pyproject↔cmake 版本联动校验）
- **clients**: Python 客户端 SDK（M1）—— openapi-python-client 生成面 + 手写
  认证层（client_credentials 自动刷新/401 重试、authorization_code+PKCE、
  introspect Basic）
- **clients**: Go 客户端 SDK（M2）—— oapi-codegen 生成面 + x/oauth2
  clientcredentials（AuthStyleInHeader）+ authcode/PKCE
- **identity**: `SocialLinkService` + `ISocialAccountRepository` link/unlink
  仓储方法（listForUser 排除 `provider='local'` 种子行；27 单测）
- **self-service**: `/api/me/social/links` 三端点（profile scope +
  `WITH_SOCIAL` 条件注册；numeric-dispatch 用户解析使 GitHub 社交会话可用；
  11 HTTP 集成测试 + 4 审计事件）
- **openapi**: 社交链接端点入 spec + SDK 再生成（SocialLinkEntry/List/Result
  schema；oasdiff 纯新增；v1.3.0 五处版本源同步）
- **user-portal**: Connected Accounts 卡片 + GitHub link 回调分支（`state=link`
  先于登录短路 + 整页往返后的会话恢复；6 e2e）

### Fixed

- **clients**: Go I3 处理生成 discovery 模型中指针类型的 Issuer
- **clients**: m2m 客户端的收尾关闭路径（PR 评审）
- **self-service**: link 仅接受 JSON body 的 `code`（不进访问日志）；unlink
  响应补 `subject`
- **identity**: `SocialLinkService` 空 repo 依赖与非法 provider 的状态分离
  （装配缺陷 → 500 类，#74）
- **social-links**: 独立评审轮 W1–W4 + S1–S4 —— link 分支防跌落登录（不再
  自动开户）、Google/WeChat 空 subject 防护、openapi 披露解绑不吊销既有会话、
  `linked_at` ISO-8601（Safari 可解析）、UNIQUE 冲突按约束名（locale 无关）判定

### CI/Build

- `clients-sdk.yml`（新）：PR 路径过滤的 SDK 漂移门 + 双语言单测；
  `release.yml` 新增 sdk-python job（PyPI 发布 tag+secret 双门控——首发需
  人工注册项目并配置 `PYPI_API_TOKEN`），github-release job 推送 Go 嵌套 tag
  `clients/go/v<version>`（子目录模块对根 tag 不可见）

### Test-infra

- `drogon_macro_bool_check.py`：drogon `CHECK`/`REQUIRE` 宏内裸 `||`/`&&` 的
  静态检查入 CI static-checks（#76，PR #77）；测试指南新增 [MUST] 断言书写
  规范

## [1.2.0] - 2026-08-17

v1.1.0 以来的第二个正式发布，61 个 commit。主线：#43 资源-作用域授权
模型、用户管理 CRUD 补全（A2，PR #52）、OIDC Back-channel Logout 后端
（B1，PR #50）、OpenAPI spec 治理与破坏性变更门（A1/M0，PR #63）、
benchmark M2–M4 承重验证（PR #48）、#53–#60 用户管理安全加固批次
（PR #62）。

### ⚠️ Breaking (security hardening)

> 收紧了原本宽松（或错误）的行为。按版本策略（[§3 灰色地带取舍]
> (docs/backend/versioning-and-release.md#3-安全-hardening-的灰色地带显式取舍声明)）
> 在 MINOR 内推进，不升 MAJOR。依赖旧行为的下游需对照迁移。

- **`/api/admin/*` 现在要求 `admin` scope**（#43，F-010）：管理面路由由
  声明式 `(path,method)→scope` 注册表门禁。仅持有 RBAC admin 角色但
  access token 缺 `admin` scope 的调用返回 403（RFC 6750
  `insufficient_scope`）。迁移：管理面客户端在 token 请求中申请
  `admin` scope。
- **软删除契约全链路强制**（#54）：已软删除的用户不再能通过社交登录、
  MFA 登录补全、自服务等路径获得新 token 或会话。
- **`backchannel_logout_uri` 校验收紧**（#57）：强制 https；通知器
  crash-safe（传输失败记录错误而非中断登出）。
- **OpenAPI spec 死端点移除**（A1/M0）：`/api/orgs*`、
  `/oauth2/device/verify`、旧 `/oauth2/mfa/*` 路径从 spec 删除——这些
  端点在服务端早已不存在（调用一直 404），spec 只是回归真实。oasdiff
  破坏性变更门就位后，未来的 HTTP 面破坏性变更需升 MAJOR 或在
  `tools/openapi-governance/oasdiff-breaking-ignore.md` 显式豁免。

### Added

- **#43 资源-作用域授权模型**：声明式 `(path,method)→required_scopes`
  注册表（`ResourceScopeRegistry`，42 条 scope-gated 路由）+ scope 继承
  （`impliedBy`，如 `admin` 蕴含 `roles:read/write`）+ DB 驱动的 admin
  角色解析 + `/api/admin/scopes/resources` 发现端点。含 V023 迁移。
- **用户管理 CRUD 补全**（A2，PR #52）：`GET /api/admin/users` 分页
  （page/per_page）与过滤（q/role/locked）；`createUser`（username vs
  email UNIQUE 冲突区分 → 409；角色落库结果回报 roles_assigned /
  roles_failed）；`updateUser` 扩展（org_id 支持整数置值与 null 清空）；
  `deleteUser` 软删除（V024 `deleted_at` 迁移，删除时吊销存量 token 并
  回报 `tokens_revoked`；最后活跃 admin 保护 → 409）。
- **OIDC Back-channel Logout 后端**（B1，PR #50）：logout_token JWT
  构造器 + 通知器接入登出流程（`/oauth2/logout` 与 `end_session` 均触发
  通知，#55）；admin API 与 admin UI 表单配置
  `backchannel_logout_uri`；discovery 广告 `backchannel_logout_supported`；
  validator 单测 D1–D6 + admin/discovery 端点测试。
- **OpenAPI spec 治理 M0**（A1，PR #63）：三层端点对账（routes 82 =
  docs 80 = yaml 78，模两条例外清单）；P0 schema 按控制器实测契约补齐
  （token/introspect/revoke/login 表单与 JSON 请求体、RFC 6749 与应用
  双错误形态、discovery/JWKS 全字段）；`info.version` 与
  `cmake/Version.cmake` 联动（1.2.0）。CI 新增三层一致性门
  （`tools/openapi-governance/check_spec_governance.py`）+ oasdiff
  破坏性变更门（PR vs master，v1.29.1 pinned）。验收：生成的 Python
  客户端实调 token / introspect / discovery 全通过（客户端 SDK 工作
  解除阻塞）。
- **benchmark M2–M4**（PR #48）：S3–S6 场景（introspect / revoke /
  userinfo / discovery 无状态端点）；`config.bench.json` 集中配置；
  40 份结果 JSON + SUMMARY.md 承重裁决——内存 SDK 口径实测 **2.5 MB
  peak WS**（远优于 50–120 MB 声称），冷启动 ~4s 观测达成，P99 低并发
  1–4ms，discovery ~86k QPS @ 8 vCPU。
- 端点测试（59 OAuth2 + 52 Admin）集成进 ctest，纳入平台 CI 门禁。

### Fixed

- **#53–#60 用户管理加固批次**（PR #62）：admin 面输入校验、错误码
  语义与并发行为修复；#59 org_id 清空发送 JSON null 的前端修复。
- 社交登录默认角色授予未真正落库（role_id 未置）——PR #62 review 修复。
- `createUser`/`updateUser` 的 UNIQUE 冲突返回 409 而非 500（PR #52）。
- benchmark：容器解析改用 `docker ps`（compose ps 输出漂移）；PR #48
  review 修复（config 交换检测、observer 计时）；内存裁决口径修正
  （metric 用错而非未达标）。
- 测试基建：endpoint wrapper `pkill -f` 自杀 bug、ctest 管道继承挂起、
  服务器不可用时 exit 77 跳过、CI psql 兜底误抓任意 postgres 容器。

### Changed

- `IOAuthHttpClient` 家族从 `WITH_SOCIAL` 条件编译中解除（backchannel
  logout 等核心路径需要 HTTP 客户端，与社交登录编译开关解耦）。
- refactor-baseline 端点签名基线再生（78 行，收敛 master 存量漂移）。

### Documentation & CI

- 产品化演进文档同步（A2/B1/A1 交付状态、benchmark 裁决、issues
  #53–60 设计与实施计划）；openapi-update skill 四处镜像更新为治理门
  工作流；CI 增加 openapi-governance workflow。api-diff 基线按 SOP 对
  #43 内部引擎与 ORM 再生漂移 ratify。

## [1.1.0] - 2026-08-12

v1.0.0 以来的首次正式發布。涵蓋 842 個 commit，包含完整的 OIDC Core
支持、OAuth/OIDC 合規審計修復（31 項 finding 全部 remediated）、Redis 緩存
層、WebAuthn/Passkey、Admin 控制台，以及 SDK 庫層重構。

### ⚠️ Breaking (security hardening)

> 以下變更收緊了原本寬鬆（且多為 spec 違規）的行為。按版本策略
> ([§3 ](docs/contribute/versioning-and-release.md#3-安全-hardening-的灰色地带显式取舍声明))
> 在 MINOR 內推進，不升 MAJOR。依賴舊寬鬆行為的下游需對照遷移。

- **PKCE 對 PUBLIC 客戶端強制**（RFC 9700 §2.1.1）：`require_pkce_for_public`
  預設改為 `true`（`config.json` / `config.dev.json` / `config.ci.json` /
  `config.prod.json` 顯式設定）。遷移：PUBLIC 客戶端必須發送 `code_challenge`
  / `code_verifier`。
- **redirect_uri 強制 https**（RFC 8252 §7.3）：豁免僅 `http://127.0.0.1` 與
  `http://[::1]` loopback IP 字面量（端口通配，`localhost` **不**豁免）。
  新增配置開關 `auth.allow_http_redirect_uri`（dev 開 / prod 關）。遷移：將
  seed/測試中的 `localhost` redirect_uri 改為 `127.0.0.1`。
- **refresh_token grant 強制客戶端認證**（RFC 6749 §3.2.1/§6）：CONFIDENTIAL
  客戶端缺失或錯誤 `client_secret` 返回 401 `invalid_client`（含
  `WWW-Authenticate: Basic`）。遷移：CONFIDENTIAL 客戶端 refresh 請求須帶
  客戶端憑證。
- **token_endpoint_auth_method 持久化並強制**：`oauth2_clients` 新增該列。
  token/introspect/revoke 按聲明方法強制（`client_secret_basic` 僅接 Basic
  頭 / `client_secret_post` 僅接 body / `none` 拒絕任何 secret）。NULL 保留
  舊寬容 Basic→body 回退。遷移：建議經管理端顯式賦值。
- **userinfo 要求 openid scope**：access token scope 不含 openid 返回 403 +
  `WWW-Authenticate: Bearer error="insufficient_scope"`；M2M token
  （subject `client:*`）直接拒。
- **最低路徑 required-scope 強制**：`/oauth2/userinfo`→`openid`、`/api/me`
  與 `/api/me/*`→`profile`、`/api/admin/*`→`admin` scope。
- **獨立 Redis 存儲模式正式棄用**：啟動時 LOG_ERROR 明示，該模式下
  refresh_token grant 返回 `unsupported_grant_type`。目標架構為 Postgres
  存儲 + Redis 緩存。見 `docs/backend/configuration-guide.md` §3。
- **內部限流**：`/oauth2/token`、`/oauth2/introspect`、`/oauth2/revoke` 與
  device_code 輪詢共享滑動窗口限流（默認 30 次失敗/60s，可經
  `custom_config["auth"]["rate_limit"]` 配置），達閾值 429 + `Retry-After`。
  僅計失敗，成功清零。
- **SDK 命名空間重構**：`oauth2::*` → `authforge::drogon::*`，`common::*` →
  `authforge::common::*`。扁平 `#include <oauth2/Foo.h>` 路徑不再有效，改用
  語義子目錄路徑（如 `<authforge/drogon/plugin/OAuth2Plugin.h>`）。SDK 消費
  者需更新 include 路徑與命名空間限定符。

### Added

#### OAuth2 / OIDC 協議

- **OpenID Connect Core**：`id_token` 簽發（HS256/RS256）、`/.well-known/
  openid-configuration` discovery、JWKS 端點、`userinfo`、`prompt` / `max_age`
  / `auth_time` / `acr` / `amr` 全量支持。
- **RP-Initiated Logout**（OIDC）：`/oauth2/end_session`（GET+POST），校驗
  `id_token_hint` 與 `post_logout_redirect_uri`，回顯 `state`。
- **Backchannel Logout**（OIDC）：`backchannel_logout` 支持。
- **Device Authorization Grant**（RFC 8628）：`/oauth2/device_authorization` 端
  點，按 client_type 分支認證；輪詢過快返回 `slow_down`（interval 遞增 5 秒並
  持久化）。
- **Dynamic Client Registration**（RFC 7591）：經 `/api/admin/clients/*`
  （admin-only）。
- **Client Credentials Grant**（M2M）：machine-to-machine token 簽發。
- **Token Introspection**（RFC 7662）：`/oauth2/introspect`。
- **Token Revocation**（RFC 7009）：`/oauth2/revoke`，含 refresh-token 撤銷。
- **Authorization Server Metadata**（RFC 8414）：
  `/.well-known/oauth-authorization-server`。

#### 身份與安全特性

- **密碼雜湊升級**：PBKDF2-SHA256（取代舊 SHA-256）。
- **Subject UUID**：`public_sub` 取代自增 ID 作為對外 subject。
- **Refresh Token 重用檢測**：family cascade 失效（被盜 token 重用 → 整族失效）。
- **授權碼原子消費** + token pair 事務性保存。
- **MFA / TOTP**（RFC 6238）與 **WebAuthn / Passkey** 支持。
- **賬戶鎖定**：漸進退避（progressive backoff）。
- **Email 驗證** + **密碼重置**流程。
- **結構化審計日誌**（`audit_logs` 表 + AuditService）。
- **多租戶基礎**（organizations）。
- **SMTP 郵件服務**（163 / Gmail / SendGrid 通用）。
- **社交登錄**：GitHub、Google。

#### 基礎設施

- **Redis L2 緩存**：client lookup 與 token 緩存的 write-through 裝飾器
  （Postgres 存儲 + Redis 緩存架構），含 admin-mutation 失效。
- **SchemaManager**：編號式自動 migration（`V0NN_*.sql`，22 個），單事務執行。
- **多平台 CI/CD**：Linux（Ubuntu 22.04）、Windows（MSVC 2022）、macOS
  （ARM64）。
- **Tag-driven release 流水線**：SDK tarball + 多架構鏡像（amd64/arm64）+
  cosign keyless 簽名 + SPDX SBOM + git-cliff release notes。
- **API 表面守衛**：`api-diff` 工具在 CI 強制 SDK 公共頭的 SemVer（breaking
  必須升 major）。
- **arch-guard**：Domain 層邊界檢查（禁止 include drogon 頭等）。
- **Helm chart** + 生產 Compose 加固 + 版本化 migration runner。
- **HTTP 性能基準設施**（benchmark facility）。

#### 前端

- **生產級 SPA**：AuthForge user frontend（Vue），含 PKCE、token lifecycle、
  consent 頁、email-first 登錄/註冊。
- **Admin 控制台**：clients/users/scopes/tokens/audit 全套管理頁，OIDC 簽名
  金鑰展示。
- **設計系統重構**：統一 UI 設計語言。

#### SDK 庫重構

- 分層庫結構：`libs/common`（Domain 共享內核）、`libs/oauth2`、`libs/identity`、
  `libs/storage-{postgres,redis,memory}`、`libs/drogon`（Adapter）。
- Domain 服務經端口（`ICryptoProvider` / `IUuidGenerator` / `IClock` /
  `ILogger` / `IAuditSink`）與 Drogon 解耦，支持脫離 Drogon 宿主消費。
- SDK 打包：`find_package(authforge-*)` 源碼集成 + 安裝消費 smoke 測試。

### Fixed

#### OAuth/OIDC 合規審計（31 項 finding 全部 remediated）

- **F-002**：`client_secret` 哈希寫入路徑統一為「有鹽小寫 SHA-256」，與校驗
  路徑一致（此前寫入用無鹽大寫 SHA-256，動態註冊/管理端創建的客戶端永遠無法
  認證）。
- **F-004**：Redis 後端 `client_secret` 比較改為常量時間比較；三後端統一
  `constantTimeMemcmp`；刪除洩漏比較結果的 LOG_DEBUG。
- **F-016**：issuer 一致性修復——access token 簽發時寫入配置的 issuer（此前
  從不寫入）；刪除三後端硬編碼的 `https://oauth.example.com`；introspect `iss`
  與 discovery `issuer` 字節一致（OIDC Discovery §3）。
- **F-007**：授權端點錯誤按 RFC 6749 §4.1.2.1 分流——client_id 未知 /
  redirect_uri 無效直接 4xx，其餘錯誤 302 重定向並回顯 state。
- **F-006**：資源端點 401 發 RFC 6750 §3 `WWW-Authenticate: Bearer ...
  error="invalid_token"` 挑戰。
- **F-008/F-009/F-013**：token 端點 validation gate 發 RFC 6749 §5.2
  `error: invalid_request` 信封；authorization_code 兌換時 redirect_uri 強制
  匹配；authorize 端校驗 `code_challenge_method ∈ {plain, S256}`。
- **F-019/F-020**：token/introspect/revoke 成功响应加 `Cache-Control: no-store`
  （RFC 6749 §5.1 / RFC 7009 §2.2.1）；authorize 终态重定向的 `state`/`code`
  经 urlEncode。
- **完整审计报告**：见
  `docs/adr/oauth-oidc-compliance-audit.md`。

#### 其他修復

- **Linux teardown 崩潰**：`OAuth2CleanupService` 析構訪問已銷毀事件循環 →
  加 `stopped_` 標誌防止重複清理，乾淨退出無需 `std::_Exit(0)`。
- **refresh-token revoke 空操作**（C3）：revoke 路徑未對 token 哈希即比對，
  導致 RFC 7009 §2.1 撤銷無效——修復為先哈希再比對。
- **SchemaManager 單事務**：full migration pass 在單一事務內執行（#46）。
- **keepalive logout revoke**（C5）：keep-alive 會話登出時正確觸發 token 撤銷。
- **OpenAPI 文檔**：security field 形狀、PKCE/MFA/revoke 參數註冊等多項修正。
- **MSVC `/WX`**：解決 `sharedCb` 變量遮蔽（C4458）導致的 Windows CI 失敗。

### Changed

- **Drogon**：v1.9.10 → v1.9.13。`drogon_ctl` 產生的 ORM 驗證碼改用
  `std::wstring_convert<std::codecvt_utf8_utf16<...>>`（由 `orm_compat.h` 處理
  C++20 棄用）。
- **依賴管理**：引入 Conan（`conanfile.py` + `conan.lock`）作為 C++ 依賴管理。
- **限流**：遷移至 Drogon Hodor 插件（token bucket），移除 Redis 依賴。
- **token endpoint 錯誤響應**：統一為 RFC 6749 §5.2 OAuth2 錯誤信封（此前用
  應用內部信封）。
- **DB schema**：新增 14 個 migration（V009–V022），含 refresh_token_family、
  mfa_support、account_lockout、device_codes、backchannel_logout、
  token_partitioning_prep、multi_tenant、webauthn 等。

### Security

- OAuth/OIDC 合規審計 31 項 finding 全部 remediated（3 個 batch）。
- 安全 hardening：SQL injection / XSS / command injection 防護、CORS / CSP /
  HSTS 頭、token 撤銷、賬戶鎖定。
- 三存儲後端 `client_secret` 常量時間比較。

---

## [1.0.0] - 2026-01-29

首個正式發布。OAuth2.0 Authorization Code Grant、access/refresh token、客戶端
管理、用戶認證、Drogon 插件架構、Vue 前端、PostgreSQL/Redis 持久化、RBAC、
審計日誌、WeChat 登錄。

### Added

- OAuth2.0 Authorization Code Grant flow；access token 與 refresh token 支持。
- 客戶端註冊與管理；用戶認證（用戶名/密碼）。
- Drogon 插件架構；controller-based HTTP 端點；filter 中間件；JSON 配置。
- PostgreSQL 持久化（ORM 遷移）與 Redis 持久化；同步寫入；SHA-256 雜湊。
- 原子消費操作；client secret hash 校驗；PostgreSQL 事務支持。
- 用戶賬戶系統；ORM 遷移；UUID salt 支持。
- Vue.js SPA 前端；OAuth2 登錄流程；受保護 API 訪問；用戶資料展示。
- WeChat 開放平台 API 與 QR code 登錄。
- RBAC 權限系統基礎；Prometheus metrics；結構化審計日誌。
- 單元測試、整合測試（Redis/PostgreSQL）、E2E 整合測試、直接 controller 測試。

### Security

- 基礎認證（用戶名/密碼）；client secret 雜湊（SHA-256）；CORS 配置。
- SQL injection 防護；輸入校驗與淨化。

---

[1.0.1]: https://github.com/voidvec/fulla/compare/v1.0.0...v1.0.1
[Unreleased]: https://github.com/voidvec/fulla/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/voidvec/fulla/compare/v1.0.1...v1.1.0
[1.0.0]: https://github.com/voidvec/fulla/releases/tag/v1.0.0
