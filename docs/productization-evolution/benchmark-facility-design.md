# AuthForge HTTP 性能基准设施设计

> **版本**: v1.0
> **日期**: 2026-08-05
> **文档性质**: 技术设计（Phase 0 落地蓝图，**非代码**——实施见 §七 milestone，各 milestone 单独立项）
> **上游规划**: [演进方案 §三 Phase 0](productization-evolution-plan.md)
> **承重背景**: 本设施是验证 [调研报告](productization-research.md) §3.1/§3.2 性能声明的唯一手段——代码库当前无任何 HTTP 级基准（见 §二背景）。

---

## 零、TL;DR

- **建什么**：一个顶级 `benchmarks/` 目录，用 **wrk** 对 AuthForge 的 6 个核心 OAuth2 端点做阶梯式加压，产出 QPS / P50/P95/P99 / 错误率 / 稳态内存，结果以 JSON 入仓并生成"承重假设验证报告"。
- **打谁**：**postgres+redis 全栈**（`deploy/docker/docker-compose.yml` 起 backend + PG15 + Redis7 + Prometheus），**不是 memory 模式**——memory 模式无用户存储，login/userinfo/refresh 不可达。
- **不打谁**：本设施 **Phase 0 不含竞品对比**（Keycloak/Ory/Zitadel 后置到 Phase 0.5）。
- **核心约束**：客户端在进程外（用生产镜像，不在 `authforge-tests` 进程内打）；每档前 warmup；稳态门槛错误率 < 0.01%；driver CPU < 80%。
- **验收**：第三方按 `benchmarks/README.md` 在标准云主机上复现误差 < 15%；6 场景各有一次稳态记录；承重假设 4 数字逐条标"达成/未达成/修正"。

---

## 一、目标与非目标

### 1.1 目标

| # | 目标 | 衡量 |
|---|------|------|
| G1 | **验证承重假设**：对照调研报告 §3.1 的 4 个 AuthForge 数字（单机 QPS ~10万+、内存 50–120MB、P99 <2ms、冷启动 ~5s），逐条给出"实测达成 / 未达成 / 修正为 X" | §六验收 ✅ 承重假设验证报告 |
| G2 | **可复现**：任一第三方按文档能在标准云主机跑出误差 < 15% 的同构数字 | §六验收 ✅ 复现门槛 |
| G3 | **回归守护**：CI 里一个轻量门，防止性能悄悄退化 | §八 CI 集成设计 |
| G4 | **诚实的数据发布**：供 README 徽章 / 博客 / TechEmpower 提交引用的单一可信源 | `benchmarks/results/` + 报告 |

### 1.2 非目标

| # | 非目标 | 为什么 / 归属 |
|---|--------|--------------|
| N1 | 竞品对比（Keycloak / Ory / Zitadel） | 用户决策：Phase 0 仅自测。后置到 **Phase 0.5**，复用本设施的 wrk + 阶梯方法论，加 `benchmarks/competitors/` 目录 |
| N2 | 进程内微基准（SubjectGenerator 等） | 已有 `tests/performance/benchmark/PerformanceBenchmark.cc`，是不同关注点（单元回归），不在本设施范围 |
| N3 | ABI / 编译期基准 | 与 HTTP 吞吐量无关 |
| N4 | 前端（admin/user SPA）性能 | 前端是 nginx 静态托管，与后端性能叙事无关 |
| N5 | 修改产品代码以提速 | 本设施只测不优；优化是后续独立任务（由本设施数据驱动） |

---

## 二、背景：为什么需要这个设施

### 2.1 承重假设未经验证

调研报告 §3.1/§3.2/§5.3 的全部对外信息矩阵都引用：单机 10万+ QPS、P99 <2ms、内存 50–120MB、零 GC 抖动、冷启动 ~5s。**这些是断言，不是测量**。

### 2.2 代码库现状（已核实，2026-08-05）

| 现状 | 证据 |
|------|------|
| **无 HTTP 级基准** | `tests/performance/benchmark/PerformanceBenchmark.cc` 只测进程内 `SubjectGenerator::forLocalUser/parse` 的纳秒级字符串操作，不启动监听器、不触 Drogon HTTP 栈、不碰 DB/Redis |
| **CI 的 "Performance Report" 步骤输出虚构测试名** | `.github/workflows/_build-test.yml:400+` 写入 `performance_report.txt`，声称跑了 "OAuth2 Flow Performance" / "Storage Throughput"，但**这些测试不存在**——报告是装饰性的 |
| **无任何负载工具** | 全仓 grep `wrk/vegeta/k6/hey/ab/locust/artillery/gatling` 零命中（仅设计文档里提到） |
| **无 `benchmarks/` 目录** | 确认不存在 |

**结论**：HTTP 端到端基准是确认真空白。承重商业叙事压在零证据上——这是本设施要解决的核心问题。

### 2.3 现有可复用资产

本设施**不是从零造轮子**，复用以下已落地能力（详见调研报告 §3.4）：

| 资产 | 复用方式 |
|------|---------|
| `deploy/docker/docker-compose.yml`（backend + PG15 + Redis7 + Prometheus） | 直接作为压测 target 的起停栈 |
| `apps/server/seed/*.sql`（backend-svc、vue-client 客户端） | **客户端**种子数据。⚠️ **docker-compose 栈不自动 seed**（PG 不递归 initdb.d 子目录、应用 MigrationRunner 只跑迁移）——本设施 `setup.sh` 显式 `psql -f` 注入。⚠️ `admin/admin` 用户**仅冒烟用，不用于压测**——需**新增 N 个压测用户**（见 D4，lockout 脆弱） |
| `scripts/smoke-parity.{sh,ps1}` 的 boot→`/health/ready`→teardown 模式 | setup/teardown 生命周期模板 |
| `apps/server/src/main.cc` 的 `--migrate-only` + `OAUTH2_AUTO_MIGRATE=true` | schema 复现的确定性门 |
| `/metrics`（Prometheus exporter，`config.json:135-138`）+ compose 的 `oauth2-prometheus` 服务 | 压测期间的资源观测（标签 `{endpoint}`/`{client_id}`/`{error}`，见 §5.4） |
| `scripts/backend/test-oauth2-endpoints.sh` + `common-test-functions.sh` | **仅借鉴成功路径的请求形态**（Test 3/4/5/6/7/9/10/11）；⚠️ **不照搬场景定义**——脚本是正确性测试，选点逻辑与性能基准不同（见 §4.1） |
| `manage.sh docker-up/down` | 栈编排 |

---

## 三、关键约束与设计决策

每条决策带代码库 `file:line` 依据。

### D1 — 目标系统 = postgres + redis 全栈（**不是** memory 模式）

**依据**：`MemoryIdentityRepository`（`libs/drogon/...`，由 `OAuth2Plugin.cc:267-289` 装配）**只有 admin 角色映射，无用户存储**——用户凭证、subject 映射、userinfo 均不持久化。

**后果**：
- memory 模式下 `/oauth2/login`（对用户）失败、`/oauth2/userinfo` 无数据、`refresh_token` 无源 token → **login/userinfo/refresh 三个场景在 memory 模式不可达**。
- memory 模式也没有 `backend-svc` 这个 confidential 客户端（只在 seed SQL 里），故 client_credentials 在 memory 模式同样不可达（除非手动往 config `clients` 块加 confidential client）。

**决策**：基准 target 固定为 **`apps/server/config/config.json`（`storage_type=postgres`，10 连接 PG + 10 连接 Redis）+ `deploy/docker/docker-compose.yml` 全栈**。

**memory 模式的有限用途**：仅用于 **discovery / jwks** 这类无状态端点的"纯框架 Drogon 吞吐量"基线（剔除 DB/Redis 噪声，看框架本身天花板）。作为**补充基线**，不作为主场景。

### D2 — 负载工具 = wrk

**依据**：用户决策（wrk，推荐项）。

**理由**：
- C 实现、事件驱动、单线程异步，**客户端开销最低**——能真正打满宣称的 10万+ QPS 的 C++ 服务器（Go/Node 实现的 k6/locust 在打高速服务器时自身先成瓶颈）。
- **TechEmpower Framework Benchmarks 同款**，对外数据可比、可信。
- 单静态二进制，CI 一行 `apt-get install -y wrk` 装好。
- Lua 脚本处理多步流（login → 捕获 code → token）、动态 body、请求头。

**代价**：Lua 有学习曲线；多步流的状态管理（token 池）需小心（见 §九风险）。

### D3 — 客户端在进程外（用生产 Docker 镜像，**不**用 in-process test boot）

**依据**：现有 `tests/test_main.cc:383-426` 的 in-process boot（Drogon 跑在测试进程的后台线程）是**功能测试模型**——测试请求经同进程 HttpClient 发出，客户端与 server 事件循环争抢 CPU，且无法体现真实网络栈与连接管理。

**决策**：target = 生产镜像 `ghcr.io/.../authforge-backend:<ver>`（或本地 `deploy/docker/Dockerfile` build），由 `docker-compose.yml` 起；driver = 独立机器/容器跑 wrk。

**好处**：target 行为与生产一致；driver 不干扰 target 的 CPU/内存；网络栈真实。

### D4 — 种子数据：复用 SQL 客户端 + **新增 N 个压测专用用户**（不能只用 admin）

**依据（关键约束）**：
1. **账户锁定已实现且会触发**：`libs/drogon/src/AuthService.cc:149-157` 实现渐进锁定——5 次失败→1 分钟、10→5 分钟、15→30 分钟、20+→1 小时。`scripts/backend/common-test-functions.sh` 的 `reset_admin_account` 在测试前后都清 `failed_login_count=0`，恰恰证明**单 admin 在重复登录下是脆弱的**。基准在高并发下，即便成功率 99.9%，单一 admin 的偶发失败（网络抖动、超时）累计会很快撞 5 次阈值，导致 login 场景跑几秒就锁死、基准崩溃。
2. **refresh token 旋转家族**：V008 迁移引入家族旋转——旧 RT 用一次即作废**整个家族**。脚本 Test 9 只调用一次所以无碍；基准若多 VU 共享一个 RT，第二次起全失败。
3. **seed SQL 注入机制（⚠️ 评审修正 2026-08-08）**：原版误称"PG initdb 自动执行 seed"。**实际**：`deploy/docker/docker-compose.yml:82-83` 将 `seed/` 挂到 PG 的 `/docker-entrypoint-initdb.d/seed:ro`，但 PG entrypoint **不递归子目录**（注释 `:79-81` 明确说明这是 no-op），且应用内 `MigrationRunner`（`OAUTH2_AUTO_MIGRATE=true`）**只跑 schema 迁移、不跑 seed**。故 **docker-compose 栈不会自动 seed**——seed 必须显式 `psql -f seed/*.sql` 注入（参照 `scripts/backend/setup-database.sh` 与 `deploy/docker/docker-quick-verify-debug.sh:62-64`）。本设施的 `setup.sh` 已实现此显式 seed 步骤（带迁移竞态重试）。

**决策**：
- **客户端凭证**：复用 `seed/*.sql`（`backend-svc`/`vue-client`）—— 客户端无锁定问题，直接用。
- **用户**：**预 seed N 个压测专用独立用户**（如 `bench_user_0000..9999`，N ≥ 预期并发 VU 数 × 轮换系数），每 VU 绑定不同用户。两种 seed 方式：
  - **方式 A（推荐，确定性）**：写一个 `apps/server/seed/bench_users.sql`（参照 `dev_admin_user.sql` 的 `INSERT ... ON CONFLICT DO NOTHING` 模式），由 `setup.sh` 显式 `psql` 注入（**不是** initdb 自动）。密码用与 admin 相同的 legacy SHA256+salt 哈希（`PasswordHasher.cc` 接受，首次登录后透明 rehash 为 PBKDF2）——但**注意**：批量 seed 的用户首次 login 会触发 PBKDF2 rehash，给首档引入噪声，故 **warmup 阶段必须先把这些用户各登录一次完成 rehash**。
  - **方式 B（灵活）**：基准 setup 阶段调 `POST /api/register` 批量注册 N 用户（脚本 Test 13 已验证此端点可用），密码统一。更慢但不需要改 seed SQL。
- **每次压测前用 fresh volume**（`docker compose down -v` 清卷再 `up`），保证 schema + 种子确定性。运行中重置用 `scripts/backend/setup-database.sh`（drop+recreate+migrate+seed）。
- **refresh token 池**：每 VU 维护独立 RT 池，**每 RT 仅用一次**；池预填充经 S4 流获取的 RT，大小 ≥ 单档测量期内的刷新次数。

**种子速查**（见附录 A 详情）：

| 凭证 | 值 | 用途 | 出处 |
|------|-----|------|------|
| 压测用户 | `bench_user_NNNN` / `<统一密码>` | S4 auth_code 登录、S6 userinfo | **新增** `seed/bench_users.sql`（方式 A）或 register 批量（方式 B） |
| client_credentials 客户端 | `backend-svc` / `test-secret`，scope `read` | S2 client_credentials | `seed/dev_backend_client.sql:7-22` |
| auth_code 客户端（PUBLIC） | `vue-client`，secret `123456`（PUBLIC 忽略），redirect `http://localhost:5173/callback` | S4/S5/S6 | `seed/dev_vue_client.sql:5-21` |
| ⚠️ `admin` / `admin` | —— | **仅用于冒烟验证，不用于压测** | `seed/dev_admin_user.sql`（锁定脆弱） |

### D5 — 主基准 auth_code 场景**必须发 PKCE**（已按代码库现状修正）

> **⚠️ 评审修正（2026-08-08 核实）**：本节原版（2026-08-05）误称"PKCE 默认 OFF"，与代码库相反。OAuth2/OIDC 合规批修复（F-011 / RFC 9700 §2.1.1）已把 PKCE 设为 PUBLIC 客户端强制。已据实重写。

**依据**（已逐条核实，branch `fix/oauth-oidc-compliance-batch-0-1` @ `34b33e0`）：
- `custom_config.auth.require_pkce_for_public` 在**全部四个** shipped config 中**显式为 `true`**：`config.json:174`、`config.dev.json:187`、`config.ci.json:143`、`config.prod.json:226`。
- 代码默认值也是 `true`：`SessionController.cc:596` `bool requirePkce = true;`（注释 "F-011 (RFC 9700 §2.1.1): PKCE is MANDATORY"）、`AuthorizationEndpointController.cc:649` 同理。仅当 config 显式 `false` 时才 opt-out。
- 强制点：`SessionController.cc:603` `if (requirePkce && codeChallenge.empty())` 直接返回错误——**不发自 code_challenge 的 login 请求会被拒**。

**决策**（已修正）：
- 主基准的 auth_code 场景（S4）**默认发 S256 PKCE**（`code_challenge` + 换 token 时带 `code_verifier`），与真实部署配置一致。
- 另设补充场景 `s4b-no-pkce`：把 PKCE 开销隔离测出来——但需**临时设 `require_pkce_for_public=false`** 才能跑（改 config 或注入专用 config），**结果必须标注"非默认配置下的基线"**，不能与默认配置的 S4 混淆。

---

## 四、基准场景矩阵

### 4.1 选点方法论（为什么不是照搬正确性测试脚本）

⚠️ **关键纠正**：本节场景**不是** `scripts/backend/test-oauth2-endpoints.sh` 的照搬。该脚本是**功能正确性测试**（56 个测试，覆盖大量 400/401 错误路径、auth 门、反枚举、幂等性），其选点逻辑与性能基准**根本不同**：

| 维度 | 正确性脚本 | 性能基准（本节） |
|------|-----------|-----------------|
| 目的 | 验证端点返回正确状态码/字段（含大量错误路径） | 测真实负载下的吞吐/延迟 |
| 路径选择 | 大量"无 auth 期望 401""无效 token 期望 400"——这些是**早返回快路径** | 必须用**成功路径**为主（错误路径走快路径，吞吐虚高且无业务意义） |
| 覆盖 | 所有端点（含 WebAuthn/social/device/admin 的错误路径） | 只测**核心 OAuth2 流的吞吐瓶颈** |
| 种子 | 单一 admin 用户、状态串行依赖 | 需 **N 个独立用户 + token 池**（避免 lockout / 状态污染） |

**脚本中可直接借鉴的只有"成功路径的请求形态"**（Test 3/4/5/6/7/9/10/11 的 curl 参数）——这是其价值；但**场景定义、种子策略、请求编排必须重新设计**。下表逐条标注每个场景对脚本的取舍。

### 4.2 主场景矩阵（S1–S6）

| # | 场景 | 端点 (方法) | 请求形态 | 种子数据 | 测什么 | 对脚本的取舍 |
|---|------|------------|---------|---------|--------|------------|
| S1 | **discovery** | `GET /.well-known/openid-configuration` + `GET /.well-known/jwks.json` | 无 header / 无 body | 无 | 纯框架吞吐量（JSON 构造 + RSA 公钥读）；无 DB/Redis，看 Drogon 天花板。**可同时在 memory 模式跑作对比基线** | ✅ 直接用 Test 3/4 形态 |
| S2 | **client_credentials** | `POST /oauth2/token` | header `Authorization: Basic base64("backend-svc:test-secret")`；body: `grant_type=client_credentials&scope=read` | seed SQL 的 `backend-svc` | 最简单的 token 签发：客户端认证 + RS256 签发 access_token。单步、无用户态依赖，最接近"签发吞吐量"。⚠️ **必须 HTTP Basic**（F-017：backend-svc 声明 client_secret_basic，body-post secret 返回 401） | ✅ 用 Test 10 形态；⚠️ **改 Basic 认证**（见 B.2） |
| S3 | **introspect (active token)** | `POST /oauth2/introspect` | body: `token=<活跃AT>&client_id=vue-client&client_secret=123456` | 需先取**活跃** AT（S2 或 S4 产出） | token 验签（RS256）+ 查活跃状态。⚠️ **必须用活跃 token**——无效 token 早返回 `{active:false}` 是快路径，会虚高（脚本 Test 42 测的就是这条快路径，**基准弃用**） | ✅ 用 Test 11（active）形态；❌ 弃 Test 42（malformed） |
| S4 | **auth_code (两步，含 PKCE)** | `POST /oauth2/login` → `POST /oauth2/token` | Step1: `username=<bench_user_N>&password=<pw>&client_id=vue-client&redirect_uri=http://127.0.0.1:5173/callback&scope=openid+profile&state=<每VU唯一>&code_challenge=<S256>&code_challenge_method=S256&json=true` → 解析 `.code`；Step2: `grant_type=authorization_code&code=<code>&redirect_uri=...&client_id=vue-client&client_secret=123456&code_verifier=<verifier>` | **N 个预 seed 独立用户**（见 D4），不是单一 admin | **最重路径**：密码验签（PBKDF2，`AuthService.cc`）+ auth_code 签发与存储 + token 签发。Lua 串两步，`json=true` 拿 JSON 避免跟 302。⚠️ **必须发 PKCE**（见 D5 修正：`require_pkce_for_public=true` 是 shipped 默认） | ⚠️ 用 Test 5/6 **形态**；❌ **不用 admin/admin 单用户**（见 D4 lockout）；⚠️ **必须带 `code_challenge`/`code_verifier`**（见 D5） |
| S5 | **refresh_token** | `POST /oauth2/token` | body: `grant_type=refresh_token&refresh_token=<RT>&client_id=vue-client&client_secret=123456` | 每 VU 独立 RT 池，**每 RT 仅用一次**（旋转家族） | refresh 旋转（V008 迁移家族逻辑）+ 新 token 签发 | ⚠️ 用 Test 9 **形态**；❌ **不用单一 RT 串行**（见 D4 家族作废） |
| S6 | **userinfo** | `GET /oauth2/userinfo` | header: `Authorization: Bearer <活跃用户AT>` | 需经 S4 拿**用户** AT（client_credentials 的 AT subject=`client:<id>`，userinfo 无对应记录） | bearer 过滤（`OAuth2AuthFilter`）+ 用户记录查询。挂 filter，比 introspect 多一层 | ✅ 用 Test 7 形态 |

**补充场景**（次要，数据驱动后决定是否纳入主报告）：
- **s4b-no-pkce**（S4 变体，**不发** PKCE）：隔离 PKCE 的额外开销——⚠️ 需临时设 `require_pkce_for_public=false`（见 D5 修正），结果是"非默认配置下的基线"，不能与默认配置的 S4 混淆。
- **discovery_memory**（S1 在 `config.ci.json` memory 模式下）：纯框架基线，对照 DB/Redis 的开销占比。
- **revoke**（`POST /oauth2/revoke`）：补 RFC 7009 覆盖；属"写"端点，优先级低于核心流。

**明确弃用的脚本测试**（错误路径/非核心，对吞吐叙事无价值）：
- 所有 "No auth → 401" 类（17b/17c/20b/20c/21b/22b/23/25b/26b/27b/28c/33/40）—— 正确性测试，早返回快路径。
- 错误码路径（Test 12 revoke 单次、41 过期 code、42 malformed token、44 缺 client 凭证）—— 同上。
- WebAuthn / social / device 的错误路径（29-38）—— 非核心 OAuth2 流，且多为外部依赖（GitHub/Google/WeChat API）不可压测。
- `/api/*` 应用层端点（admin dashboard、用户自服务、MFA、邮件验证）—— 不属核心 OAuth2 吞吐叙事。

> **场景取舍原则**：主报告聚焦 S1–S6（覆盖全部核心 OAuth2 端点 + 最重路径，全成功路径）；补充场景按"数据是否改变结论"决定是否纳入。

### 4.3 关键端点形态说明（避免踩坑）

- **`/oauth2/token` 只吃 form-encoded，不吃 JSON**（`TokenEndpointController.cc:601-625`）。发 JSON 会静默得到空参数。`/oauth2/login` 和 `/api/register` 才吃 JSON。
- **`/oauth2/login` 用 `json=true`**（`SessionController.cc:612-620`）返回 `{code, location}` JSON 而非 302——headless 压测必须如此才能不跟重定向。
- **`state` 必传且长度 8–512**、不含 `? # &`（`AuthorizationEndpointController.cc:140-189` 在 `/authorize` 强制；`/login` 不强制但建议每 VU 唯一以便排查）。
- **introspect 用调用方的 client 凭证**，不是 bearer（`TokenEndpointController.cc:233-289`）。
- **client_credentials 的 AT subject = `client:<id>`**，不能用于 userinfo（S6 必须用 S4 的用户 AT）。
- **introspect 的活跃/无效是两条路径**：活跃 token 走完整验签+查表（慢，S3 测这条）；无效 token 早返回（快，脚本 Test 42 测这条，基准弃用）。

---

## 五、测试策略（核心）

### 5.1 工作负载模型：阶梯式加并发

**目标**：找到每个场景的**拐点**（QPS 不再随并发增长）与**稳态**（错误率 < 0.01%）。

```
并发连接: 2 → 4 → 8 → 16 → 32 → 64 → 128 → 256 （每连接 N 线程，wrk -t 与 -c 配比见 5.2）
每档:    warmup 10s → 稳态测量 30s → 记录
```

**每档记录**（wrk 原生 + 补充采集）：
- QPS（requests/sec）
- 延迟：P50 / P95 / P99 / max（wrk `--latency`）
- 错误率（非 2xx 占比 + 连接错误）
- target 侧：RSS 内存、CPU%（`docker stats` 采样）、PG/Redis 连接数
- driver 侧：CPU%（必须 < 80%，否则数字不可信）

**拐点判定**：连续两档 QPS 增幅 < 5% 且 P99 开始陡升 → 到达拐点，记录该档为"峰值 QPS"。拐点后继续加 1–2 档确认退化曲线。

**稳态判定**：错误率 < 0.01% 的最高档 = "稳态容量"（对外报告用这个，不是峰值）。

### 5.2 wrk 调用约定

```bash
# 典型单档（示例，非最终脚本）
wrk -t<threads> -c<conns> -d30s -L --latency \
    -s <scenario>.lua \
    -H "Content-Type: application/x-www-form-urlencoded" \
    http://<target>:5555/oauth2/token
```
- `-t`（线程）= min(CPU 核数, 并发/16) 上取整，避免线程过多上下文切换噪声。
- `-c`（连接）= 当前档位（2/4/.../256）。
- `-d30s` 稳态测量，前接 10s warmup（`-d10s` 单独跑丢弃）。
- `-L` / `--latency` 出 P50/P95/P99 + 直方图。
- Lua 脚本（`-s`）负责动态 body（如注入上一步的 code）、多步流（S4）、token 池管理（S3/S5/S6）。

### 5.3 warmup（每档前）

每档测量前先打 **~5% 该档并发** 持续 10s，**丢弃结果**。目的：
- 预热 Drogon 连接池、PG prepared statement cache、JWK 缓存（`JwkManager` init-once）。
- 避免首档因冷效应数字偏低。

### 5.4 资源观测（与压测并行）

| 维度 | 采集方式 | 指标 |
|------|---------|------|
| target 应用 | `/metrics`（Prometheus） | `oauth2_requests_total{endpoint}`、`oauth2_introspect_requests_total{client_id}`、`oauth2_introspect_errors_total{client_id,error}`、`oauth2_revocation_requests_total`、`oauth2_active_tokens`（gauge）（实际标签见 `TokenEndpointController.cc` 的 `incrementCounter`/`setGauge` 调用，标签为 `{endpoint}`/`{client_id}`/`{error}`，**非** `{http_status}`） |
| target 容器 | `docker stats --format ...`（1s 采样） | RSS 内存、CPU%、网络 IO |
| 后端依赖 | PG `pg_stat_activity` / Redis `INFO` | 活跃连接数、命中率（判断是否测到了 DB 瓶颈而非 server） |
| driver | `top`/`mpstat` | CPU%（门槛 < 80%） |

**稳态内存定义**：去掉冷启动峰值后的稳态测量窗口 RSS 均值（不是 `docker stats` 的瞬时峰值）。冷启动峰值单独记录（见 5.5）。

### 5.5 冷启动（单独测，不与吞吐混）

```
docker compose up -d backend   # 计时起点
循环 curl /health/ready 直到 200   # 计时终点 = 冷启动时间
记录: 冷启动秒数 + 此时 RSS 峰值
```
覆盖 `OAUTH2_AUTO_MIGRATE=true`（含迁移）与 `--migrate-only` 预跑两种模式（后者冷启动不含迁移时间，公平比较）。

### 5.6 隔离与环境记录

- **target 与 driver 物理隔离**：不同云主机，或同机不同容器（仅低端基准可同机，需声明）。记录 target/driver 各自的 vCPU/RAM/型号。
- **环境元数据入结果**（每份结果 JSON 必带）：日期、Git SHA、target 镜像 tag、config 版本、PG/Redis 版本、target 规格、driver 规格、网络（同机/跨机）、wrk 版本。
- **网络验证**：先跑 S1 discovery 拿"纯网络 + 纯框架"基线，确认网络不是瓶颈（localhost 跨容器应 > 50万 QPS 量级）。

### 5.7 重复性与误差

- 每个场景的稳态档**连跑 3 次取中位数**，记录三次的离散度（max/min）。
- 离散度 > 10% 说明环境抖动，需排查（其他进程抢 CPU、PG vacuum、Redis 持久化等）。

---

## 六、验收标准（可勾选）

| # | 验收项 | 衡量 |
|---|--------|------|
| ✅ AC1 | **复现性**：任一第三方按 `benchmarks/README.md` 在同规格云主机跑 S1/S2，QPS 与入仓结果误差 < 15%，P99 同量级 | 复现报告 |
| ✅ AC2 | **场景覆盖**：S1–S6 每个场景至少有一次稳态记录（3 次中位数）入仓 `benchmarks/results/` | results 目录 |
| ✅ AC3 | **承重假设验证报告**：对照调研报告 §3.1 的 4 个 AuthForge 数字（QPS / 内存 / P99 / 冷启动），逐条标"✅ 实测达成 / ❌ 未达成（实际 X） / ⚠️ 修正为 X"，并修订报告 | 报告文档 + 调研报告更新 |
| ✅ AC4 | **driver 可信度**：所有场景最高档并发下 driver CPU < 80%（wrk 进程）；超阈值则标注"driver 受限，数字为下限" | 每份结果含 driver CPU |
| ✅ AC5 | **稳态门槛**：报告的"稳态容量"档错误率 < 0.01%；峰值档允许更高错误率但须标注 | 每份结果含错误率 |
| ✅ AC6 | **后端不是隐藏瓶颈**：压测期间 PG/Redis CPU 未持续 > 90%（否则说明测的是 DB 不是 server，须声明） | 资源观测记录 |
| ✅ AC7 | **一页摘要**：`benchmarks/results/SUMMARY.md` 给 6 场景 × 关键数字的一表，供外部读者 30 秒看懂 | SUMMARY.md |

---

## 七、实施计划（4 个 milestone）

每 milestone 是后续**单独的 `openspec/changes/` 或 `.kiro/specs/` 立项**；本设计文档不展开实现细节。

### M1 — 骨架 + 最简两场景（验证管线）
- **做**：
  - 建 `benchmarks/` 顶级目录（与 `tests/performance/` 边界声明：见 §八）。
  - `benchmarks/authforge/` 下：`setup.sh`/`teardown.sh`（复用 `manage.sh docker-up/down` + `/health/ready` 门 + `docker compose down -v` 清卷确定性）。
  - **预 seed N 个压测用户**（D4 方式 A 或 B）+ warmup rehash 钩子——虽 S1/S2 不需要用户，但用户池基建在此落地，避免 M2 才暴露问题。
  - S1（discovery）+ S2（client_credentials）的 wrk Lua + 阶梯 runner shell。
  - 结果落盘格式（JSON schema：数字 + §5.6 元数据）。
  - 骨架版 `benchmarks/README.md`（一键复现 S1/S2）。
- **验收**：AC1（S1/S2 复现误差 < 15%）+ AC2（S1/S2 稳态入仓）+ AC4/AC5。
- **依赖**：无（最简单两场景，先打通 target-boot → wrk → 结果落盘全链路）。

### M2 — 多步流场景（引入用户池与 token 池）
- **做**：S4（auth_code 两步，Lua 串 login→token，**每 VU 用不同压测用户**）+ S3（introspect，**只用活跃 token**，需种子 AT 池）。
- **关键难点**：Lua 多步流的状态管理；user-pool 轮换避免 lockout；token 池的预热与回收。
- **验收**：AC2（S3/S4 稳态入仓）；压测期间无 lockout 触发（监控 `oauth2_*_errors_total` 无异常飙升）。
- **依赖**：M1 管线 + 用户池基建就绪。

### M3 — 剩余场景 + 资源观测
- **做**：S5（refresh_token，⚠️ 旋转家族 → 每 VU 独立 RT 池，**每 RT 仅用一次**）+ S6（userinfo，须用户 AT）。
  - 资源观测：`/metrics` 抓取器（实际标签 `{endpoint}`/`{client_id}`/`{error}`）+ `docker stats` 采样器，与 wrk 并行落盘。
  - 冷启动测试（§5.5）。
- **验收**：AC2（S5/S6 稳态入仓）+ AC6（PG/Redis 非瓶颈）；S5 无家族作废导致的批量失败。
- **依赖**：M2。

### M4 — 报告生成 + 承重验证 + 文档完稿
- **做**：
  - 复现报告生成器：`results/*.json` → `results/SUMMARY.md`（AC7）。
  - **承重假设验证报告**（AC3）：对照调研报告 §3.1 四数字，逐条裁决；据实**修订调研报告 §3.1/§3.2**（上调或下调，诚实优先）。
  - `benchmarks/README.md` 完稿（含环境要求、复现步骤、结果解读、已知限制）。
  - 数据发布页（docs 站或 GitHub Pages）。
- **验收**：AC3 + AC7。
- **依赖**：M1–M3 全部数据。

### 跨 milestone 的 CI 集成（见 §八，在 M1 后逐步上线）

---

## 八、`benchmarks/` 目录结构设计（仅设计）

```
benchmarks/
├── README.md                      # 一键复现指引（环境要求/步骤/结果解读/限制）
├── authforge/                     # AuthForge 自测
│   ├── setup.sh                   # 起 target 栈 + 健康门 + 种子校验 + 预 seed N 压测用户 + warmup rehash
│   ├── teardown.sh                # 停栈 + 清卷（确定性）
│   ├── run-scenario.sh            # 阶梯 runner：接 <scenario>，跑 2..256 档
│   ├── scenarios/
│   │   ├── s1-discovery.lua
│   │   ├── s2-client-credentials.lua
│   │   ├── s3-introspect.lua      # ⚠️ 只用活跃 token
│   │   ├── s4-auth-code.lua       # 多步：login(每VU不同bench用户) → token
│   │   ├── s5-refresh-token.lua   # ⚠️ 每 VU 独立 RT，每 RT 仅用一次
│   │   ├── s6-userinfo.lua
│   │   └── (补充: s4b-auth-code-pkce.lua 等)
│   ├── lib/
│   │   ├── user-pool.lua          # 压测用户轮换（避免单用户 lockout）
│   │   ├── token-pool.lua         # 种子 AT/RT 管理（S3/S5/S6 用）
│   │   └── stats.lua              # 通用统计辅助
│   └── observe/                   # 与 wrk 并行的资源采集
│       ├── scrape-metrics.sh      # /metrics 抓取
│       └── docker-stats.sh        # 容器 RSS/CPU 采样
├── competitors/                   # 【Phase 0.5，本期不建】Keycloak/Ory/Zitadel
├── results/                       # 历次结果（JSON + 元数据）
│   ├── <date>-<sha>-<scenario>.json
│   └── SUMMARY.md                 # 生成的一页摘要（M4）
└── reporting/
    └── gen-summary.py             # results/*.json → SUMMARY.md + 承重验证报告
```

**与 `tests/performance/` 的边界声明**：
- `tests/performance/benchmark/` = **进程内单元微基准**（SubjectGenerator 等），随 `authforge-tests` 跑，属回归测试。
- `benchmarks/` = **进程外 HTTP 端到端基准**（wrk 打生产镜像），属对外可复现基准。
- 两者不互相依赖，关注点不同。`tests/performance/` 的 CI "Performance Report" 虚构测试名问题（§2.2）应在 M1 一并修正（让该步骤真实反映微基准结果或移除），属附带清理。

---

## 九、CI 集成设计（仅设计，不实现）

**演进方案 §4.1 要求**：轻量回归门，防止性能悄悄退化。

### 9.1 设计

- **新工作流** `.github/workflows/benchmarks-regression.yml`：PR 触发（或手动）。
- **跑什么**：仅 **S1 discovery + S2 client_credentials**（最便宜、最稳定两场景），固定并发（如 c=64），短跑（d=15s），单档。
- **对比基线**：`benchmarks/results/` 里最近一次 master 上的同场景同配置结果，回归 > 10% 报警（评论 PR，不 fail build——避免噪声误伤）。
- **不跑**：S3–S6（依赖种子 token、多步、慢）；竞品对比；高并发档。

### 9.2 限制声明

- CI runner 规格固定但共享，数字绝对值不可信（受邻接作业干扰），**仅看相对回归**。
- 对外发布的绝对数字必须来自**专用基准机器**（非 CI），结果入仓后在 README/docs 站引用。

---

## 十、风险与缓解

| 风险 | 等级 | 影响 | 缓解 |
|------|------|------|------|
| **driver 先于 target 打满**（wrk 单机打不到 10万 QPS） | 高 | 数字为下限，证伪不了承重假设 | AC4 监控 driver CPU；超 80% 则换更强 driver 或分布式打（多 wrk 实例聚合）；声明"driver 受限" |
| **承重数字被证伪**（如 P99 实测 >2ms） | 高 | 调研报告卖点失实 | 这正是本设施的价值——**诚实修正**调研报告 §3.1，把卖点收敛到真正领先维度（如稳态内存、QPS/瓦）；绝不对外夸大 |
| **⚠️ login 场景账户锁定**（渐进锁定 5/10/15/20 次失败→1m/5m/30m/1h，`AuthService.cc:149-157`） | **高** | 单一/少数用户在高并发下偶发失败累计→锁定→login 场景崩溃，数字失效 | D4 决策：**预 seed N 个独立压测用户**（≥ 并发 VU 数 × 轮换系数），每 VU 绑不同用户；失败计数自然分散；监控 `oauth2_*_errors_total{error}` 异常飙升即中止 |
| **⚠️ refresh token 旋转家族作废**（V008：旧 RT 复用→整家族失效） | **高** | 多 VU 共享 RT 或单 RT 重复用→第二次起全失败，S5 场景数字为 0 | 每 VU 独立 RT 池，**每 RT 仅用一次**；池预填充 ≥ 单档测量期刷新次数 |
| **⚠️ introspect 测到快路径**（无效 token 早返回 `{active:false}`，非完整验签） | **中** | S3 若混入无效 token，吞吐虚高、不反映真实验签开销 | S3 **只用活跃 token**（S2/S4 产出）；明确弃用脚本的 malformed-token 路径（Test 42）；脚本里校验 `active:true` 的 Test 11 形态才是对的 |
| **PG/Redis 成瓶颈，测的是 DB 不是 server** | 中 | 数字反映 DB 配置非 server | AC6 监控后端 CPU；固定 PG 连接池（config 默认 10）；记录后端规格；必要时声明"DB-bound" |
| **批量 seed 用户首次登录触发 PBKDF2 rehash 噪声** | 中 | 首档数字偏低（rehash 是 CPU 密集） | warmup 阶段先把所有 seed 用户各登录一次完成 rehash；或 seed SQL 直接写 PBKDF2 哈希（更复杂但无 rehash） |
| **Lua 多步流状态管理复杂**（S4/S5） | 中 | 脚本脆弱、结果不稳 | M2 专项攻关；token 池预热与回收；S5 每 VU 独立 RT |
| **环境抖动致结果离散 >10%** | 中 | 复现门槛 AC1 达不到 | 连跑 3 次取中位数（§5.7）；排查 PG vacuum / Redis 持久化 / 邻接进程；专用基准机 |
| **冷启动数字受迁移影响** | 低 | 冷启动比较不公平 | 分两种模式测（含/不含 `--migrate-only` 预跑）分别报告 |
| **wrk 不支持某些复杂场景**（如严格 RFC 的 register） | 低 | 补充场景覆盖不全 | 主场景 S1–S6 wrk 足够；复杂场景若需可用 k6 补充（Phase 0.5 评估） |

---

## 附录 A：种子数据速查表

| 凭证 | 值 | 用途 | 出处 |
|------|-----|------|------|
| **压测用户（必需）** | `bench_user_NNNN` / `<统一密码>`，N 个（≥ 并发 VU × 轮换系数） | **S4 登录、S6 userinfo**（每 VU 绑不同用户，避免 lockout） | **新增** `seed/bench_users.sql`（方式 A，见 D4）或 `/api/register` 批量（方式 B） |
| ⚠️ 管理员用户 | `admin` / `admin` | **仅冒烟验证，不用于压测**（渐进锁定脆弱，见 D4） | `apps/server/seed/dev_admin_user.sql:5-7`（legacy SHA256+"admin_salt"） |
| client_credentials 客户端 | `backend-svc` / `test-secret`，scope `read`（或 `write`） | S2 client_credentials | `seed/dev_backend_client.sql:7-22`（CONFIDENTIAL，secret 存 SHA256+salt） |
| auth_code 客户端（PUBLIC） | `vue-client`，secret `123456`（PUBLIC 忽略），redirect `http://localhost:5173/callback`（或 `:8080/callback`），scope `openid profile email` | S4/S5/S6 | `seed/dev_vue_client.sql:5-21` + `config.json:152-159` |
| admin-console 客户端 | `admin-console`，redirect `http://localhost:5174/admin/callback`，scope `openid profile admin` | （补充：register 场景需 admin token） | `seed/dev_admin_console_client.sql:5-20` |
| Token TTL | access 3600s，refresh 2592000s（30d），auth code 600s | 种子 token 有效期预算；S5 的 RT 池需在 30d 内有效 | `config.json:163-167` |

**种子注入机制（⚠️ 评审修正 2026-08-08）**：docker-compose 栈**不会**自动跑 `seed/*.sql`——PG entrypoint 把 `seed/` 挂到 `/docker-entrypoint-initdb.d/seed:ro` 但**不递归子目录**（`docker-compose.yml:79-81` 明示 no-op），应用内 `MigrationRunner` 只跑 schema 迁移不跑 seed。seed 必须显式注入：本设施 `benchmarks/authforge/setup.sh` 用 `docker exec ... psql -f seed/*.sql`（带迁移竞态重试）；或 `scripts/backend/setup-database.sh`（drop+recreate+migrate+seed，运行中重置）。

**⚠️ 压测用户 rehash 注意**：若用方式 A（SQL 直插 legacy SHA256 哈希），首次登录会触发 PBKDF2 rehash（CPU 密集，`AuthService.cc:120-145`），给首档引入噪声——**warmup 必须先把所有压测用户各登录一次完成 rehash**，或 seed SQL 直接写 PBKDF2 哈希。

---

## 附录 B：6 端点精确请求形态速查

> **形态参考**自 `scripts/backend/test-oauth2-endpoints.sh`（成功路径的 Test 3/4/5/6/7/9/10/11），但**场景定义与种子策略已重新设计**（见 §4.1 选点方法论）——尤其 S3 必须用活跃 token、S4 用预 seed 压测用户而非 admin、S5 每 VU 独立 RT。Content-Type 默认 `application/x-www-form-urlencoded`。

### B.1 S1 discovery（无 auth）
```
GET /.well-known/openid-configuration
GET /.well-known/jwks.json
```

### B.2 S2 client_credentials（⚠️ 必须用 HTTP Basic，非 body post secret）
```
POST /oauth2/token
Authorization: Basic base64("backend-svc:test-secret")    # = YmFja2VuZC1zdmM6dGVzdC1zZWNyZXQ=
Content-Type: application/x-www-form-urlencoded
Body: grant_type=client_credentials&scope=read
→ 200 {access_token, token_type:"Bearer", expires_in, scope}   # 无 refresh_token
⚠️ seed 的 backend-svc 声明 token_endpoint_auth_method=client_secret_basic，F-017 强制要求
   HTTP Basic 认证——把 client_secret 放 body 里会返回 401 invalid_client
   （2026-08-09 实测确认；原版误用 body-post 形式）
```

### B.3 S3 introspect（⚠️ 必须用**活跃** token；调用方 client 凭证，非 bearer）
```
POST /oauth2/introspect
Body: token=<活跃AT>&client_id=vue-client&client_secret=123456
（或 header: Authorization: Basic base64("backend-svc:test-secret")，body 仅 token=<活跃AT>）
→ 200 {active:true, client_id, exp, sub, scope, ...}    # 必须验 active:true（脚本 Test 11）
⚠️ 不要用无效/malformed token（走早返回快路径，吞吐虚高；脚本 Test 42 测的是这条，基准弃用）
```

### B.4 S4 auth_code 两步（⚠️ 用预 seed 压测用户，**不是 admin**；**默认发 PKCE**）
```
# Step 1: 拿 code（username 用 bench_user_NNNN，每 VU 不同，避免 lockout）
# ⚠️ 必须带 PKCE（D5 修正：require_pkce_for_public=true 是 shipped 默认）
POST /oauth2/login
Body: username=bench_user_<N>&password=<统一密码>&client_id=vue-client
      &redirect_uri=http://127.0.0.1:5173/callback
      &scope=openid+profile&state=<每VU唯一, 8-512 chars, 无 ?#&>
      &code_challenge=<S256 哈希>&code_challenge_method=S256&json=true
→ 200 {"code":"...", "location":"..."}     # json=true 避免 302

# Step 2: 换 token（带 code_verifier，服务器重算 challenge 比对）
POST /oauth2/token
Body: grant_type=authorization_code&code=<code>
      &redirect_uri=http://127.0.0.1:5173/callback
      &client_id=vue-client&client_secret=123456
      &code_verifier=<原始 verifier, 43-128 字符>
→ 200 {access_token, refresh_token, id_token, ...}
```
（补充场景 s4b-no-pkce：**不发** PKCE 以隔离其开销——⚠️ 需临时设 `require_pkce_for_public=false`，结果标注"非默认配置基线"，见 D5 修正）
（⚠️ `admin/admin` 仅用于冒烟验证，不用于压测——渐进锁定 5/10/15/20 次失败即触发，见 D4）

### B.5 S5 refresh_token（⚠️ 每 VU 独立 RT，每 RT 仅用一次）
```
POST /oauth2/token
Body: grant_type=refresh_token&refresh_token=<RT>&client_id=vue-client&client_secret=123456
→ 200 {access_token(新), refresh_token(新, 旋转), ...}
⚠️ 旧 RT 作废整个家族（V008），每 RT 仅用一次；多 VU 不可共享 RT
```

### B.6 S6 userinfo（bearer，须**用户** AT）
```
GET /oauth2/userinfo
Authorization: Bearer <AT>     # 必须是 S4 的用户 AT，非 S2 的 client_credentials AT（后者 subject=client:<id> 无用户记录）
→ 200 {sub, name, username?, email?, roles[]}
```

---

## 附录 C：承重假设验证报告模板（M4 产出）

```markdown
# 承重假设验证报告（<日期>, <Git SHA>, <target 规格>）

## 对照调研报告 §3.1

| 声明 | 实测 | 裁决 | 说明 |
|------|------|------|------|
| 单机 QPS ~100,000+ | <S2 稳态 QPS> | ✅达成 / ❌未达成(实际X) / ⚠️修正 | <场景/并发/配置> |
| 内存 50–120MB | <稳态 RSS> | ... | <是否含 PG/Redis> |
| P99 < 2ms | <S2 稳态 P99> | ... | <场景> |
| 冷启动 ~5s | <冷启动秒数> | ... | <含/不含迁移> |

## 结论
- 卖点保留：<哪些维度实测领先>
- 卖点修正：<哪些维度需下调或换措辞>
- 建议修订：调研报告 §3.1 <具体改动>
```

---

## 附录 D：与上游文档的关系

| 上游 | 关系 |
|------|------|
| [调研报告](productization-research.md) §3.1/§3.2 | 本设施的**验证对象**；M4 据实测修订其数字 |
| [演进方案](productization-evolution-plan.md) §三 Phase 0 | 本设施是 Phase 0 的**落地设计**；本文档 §七 milestone = Phase 0 的实施分解 |
| 演进方案 §4.1 Benchmark 工作流 | 本文档 §四/§五是其展开 |
| 演进方案 §四 立即动作 1 | 本文档 = 该动作的详细蓝图 |

---

*本设计基于代码库 v1.0.0 现状（2026-08-05 核实）编制。实施为后续 4 个 milestone（各单独立项），本文档是它们的共同蓝图与约束源。Phase 0 数据落地后，承重假设验证报告（附录 C）将驱动调研报告 §3.1 的修订。*
